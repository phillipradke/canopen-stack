/******************************************************************************
   Copyright 2020 Embedded Office GmbH & Co. KG

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
******************************************************************************/

/******************************************************************************
* INCLUDES
******************************************************************************/

#include "co_core.h"

#if USE_CSDO
/******************************************************************************
* MACROS 
******************************************************************************/
#define WRITE_BITFIELD(offset, mask, value)         (((value)&(mask))<<(offset))
#define CLEAR_BITFIELD(offset,mask)                 SET_BITFIELD(offset,mask,0)
#define CLEAR_WRITE_BITFIELD(offset,mask,value,bitfield)    \
    do{                                                     \
        bitfield &= CLEAR_BITFIELD(offset,mask);            \
        bitfield |= WRITE_BITFIELD(offset,mask,value);      \
    } while(0)
#define READ_BITFIELD(offset, mask, byte)           (((byte)>>(offset))&(mask)) 
 
/******************************************************************************
* PRIVATE CONSTANTS 
******************************************************************************/
// Bit definitions
// Client Command Specifier 
#define CMD_CCS_BIT_OFFSET                                  7
#define CMD_CCS_MASK                                        0b111
#define CMD_CCS_BLOCK_DOWNLOAD                              6
#define CMD_CCS_BLOCK_UPLOAD                                5
#define CMD_CCS_SEGMENT_UPLOAD                              3
#define CMD_CCS_INIT_UPLOAD                                 2
#define CMD_CCS_INIT_DOWNLOAD                               1
#define CMD_CCS_SEGMENT_DOWNLOAD                            0

// Server Command Specifier
#define CMD_SCS_BIT_OFFSET                                  5
#define CMD_SCS_MASK                                        0b111
#define CMD_SCS_BLOCK_UPLOAD                                6
#define CMD_SCS_BLOCK_DOWNLOAD                              5
#define CMD_SCS_INIT_DOWNLOAD                               3
#define CMD_SCS_INIT_UPLOAD                                 2
#define CMD_SCS_SEGMENT_DOWNLOAD                            1
#define CMD_SCS_SEGMENT_UPLOAD                              0

// Client Subcommand download
#define BLOCK_DOWNLOAD_CMD_CS_BIT_OFFSET                    0
#define BLOCK_DOWNLOAD_CMD_CS_MASK                          0b1
#define BLOCK_DOWNLOAD_CMD_CS_INITIATE_DOWNLOAD_REQUEST     0
#define BLOCK_DOWNLOAD_CMD_CS_END_BLOCK_DOWNLOAD_REQUEST    1

// Server Subcommand download
#define BLOCK_DOWNLOAD_CMD_SS_BIT_OFFSET                    0
#define BLOCK_DOWNLOAD_CMD_SS_MASK                          0b11
#define BLOCK_DOWNLOAD_CMD_SS_INITIATE_DOWNLOAD_RESPONSE    0
#define BLOCK_DOWNLOAD_CMD_SS_END_BLOCK_DOWNLOAD_RESPONSE   1
#define BLOCK_DOWNLOAD_CMD_SS_BLOCK_DOWNLOAD_RESPONSE       2

// Client Subcommand Upload
#define BLOCK_UPLOAD_CMD_CS_BIT_OFFSET                      0
#define BLOCK_UPLOAD_CMD_CS_MASK                            0b11
#define BLOCK_UPLOAD_CMD_CS_INITIATE_UPLOAD_REQUEST         0
#define BLOCK_UPLOAD_CMD_CS_END_BLOCK_UPLOAD_REQUEST        1
#define BLOCK_UPLOAD_CMD_CS_BLOCK_UPLOAD_RESPONSE           2
#define BLOCK_UPLOAD_CMD_CS_START_UPLOAD                    3

// Server Subcommand Upload
#define BLOCK_UPLOAD_CMD_SS_BIT_OFFSET                      0
#define BLOCK_UPLOAD_CMD_SS_MASK                            0b1
#define BLOCK_UPLOAD_CMD_SS_INITIATE_UPLOAD_RESPONSE        0
#define BLOCK_UPLOAD_CMD_SS_END_BLOCK_UPLOAD_RESPONSE       1

// Size Indicator
#define CMD_S_BIT_OFFSET                                    1
#define NONBLOCK_CMD_S_BIT_OFFSET                           0
#define CMD_S_MASK                                          0b1
#define CMD_S_DATA_SET_SIZE_NOT_INDICATED                   0
#define CMD_S_DATA_SET_SIZE_IS_INDICATED                    1

// Client CRC support
#define CMD_CC_BIT_OFFSET                                   2
#define CMD_CC_MASK                                         0b1
#define CMD_CC_CLIENT_DOES_NOT_SUPPORT_CRC_GENERATION       0
#define CMD_CC_CLIENT_SUPPORTS_CRC_GENERATION               1

// Continuation Bit
#define CMD_C_BIT_OFFSET                                    7
#define CMD_C_MASK                                          0b1
#define CMD_C_MORE_SEGMENTS_TO_BE_DOWNLOADED                0
#define CMD_C_NO_MORE_SEGMENTS_TO_BE_DOWNLOADED             1

// Sequence Number
#define CMD_SEQNUM_BIT_OFFSET                               0
#define CMD_SEQNUM_MASK                                     0b1111111

// non data bytes in final frame
#define CMD_N_BIT_OFFSET                                    2
#define CMD_N_BIT_MASK                                      3

/***** byte offsets **********************************************************/
#define FRM_CMD_BYTE_OFFSET                                 0
#define FRM_CMD_BYTE_SIZE                                   1

#define FRM_M_BYTE_OFFSET                                   1
#define FRM_M_BYTE_SIZE                                     3

#define BLOCK_FRM_SIZE_BYTE_OFFSET                          4
#define BLOCK_FRM_SIZE_BYTE_SIZE                            4

#define BLOCK_INIT_FRM_BLKSIZE_BYTE_OFFSET                  4
#define BLOCK_INIT_FRM_BLKSIZE_BYTE_SIZE                    1

#define BLOCK_FRM_SEG_DATA_BYTE_OFFSET                      1
#define BLOCK_FRM_SEG_DATA_BYTE_SIZE                        7

#define BLOCK_FRM_ACKSEQ_BYTE_OFFSET                        1
#define BLOCK_FRM_ACKSEQ_BYTE_SIZE                          1

#define BLOCK_FRM_SUBBLOCK_BLKSIZE_BYTE_OFFSET              2
#define BLOCK_FRM_SUBBLOCK_BLKSIZE_BYTE_SIZE                1

#define BLOCK_FRM_CRC_BYTE_OFFSET                           1
#define BLOCK_FRM_CRC_BYTE_SIZE                             2

#define BLOCK_FRM_PST_BYTE_OFFSET                           5
#define BLOCK_FRM_PST_BYTE_SIZE                             2

/***** Other constants *******************************************************/
#define DONT_GENERATE_CRC         CMD_CC_CLIENT_DOES_NOT_SUPPORT_CRC_GENERATION
#define GENERATE_CRC              CMD_CC_CLIENT_SUPPORTS_CRC_GENERATION        

/******************************************************************************
* PRIVATE FUNCTIONS
******************************************************************************/

static void   COCSdoReset                  (CO_CSDO *csdo, uint8_t num, struct CO_NODE_T *node);
static void   COCSdoEnable                 (CO_CSDO *csdo, uint8_t num);
static CO_ERR COCSdoUploadExpedited        (CO_CSDO *csdo);
static CO_ERR COCSdoDownloadExpedited      (CO_CSDO *csdo);
static CO_ERR COCSdoInitUploadSegmented    (CO_CSDO *csdo);
static CO_ERR COCSdoUploadSegmented        (CO_CSDO *csdo);
static CO_ERR COCSdoInitDownloadSegmented  (CO_CSDO *csdo);
static CO_ERR COCSdoDownloadSegmented      (CO_CSDO *csdo);
static CO_ERR COCSdoFinishDownloadSegmented(CO_CSDO *csdo);

static CO_ERR COCSdoInitDownloadBlock      (CO_CSDO *csdo);
static CO_ERR COCSdoDownloadBlockEnd       (CO_CSDO *csdo);
static CO_ERR COCSdoDownloadBlockSendSubBlock(CO_CSDO *csdo);
static CO_ERR COCSdoDownloadBlock          (CO_CSDO *csdo);

static CO_ERR COCSdoInitUploadBlock        (CO_CSDO *csdo);
static CO_ERR COCSdoUploadSubBlock         (CO_CSDO *csdo);
static CO_ERR COCSdoUploadEndBlock         (CO_CSDO *csdo);

static void   COCSdoAbort                  (CO_CSDO *csdo, uint32_t err);
static void   COCSdoTransferFinalize       (CO_CSDO *csdo);
static void   COCSdoTimeout                (void *parg);
static CO_ERR COCSdoRequestDownloadFull(CO_CSDO *csdo,
                             uint32_t key,
                             uint8_t *buffer,
                             uint32_t size,
                             CO_CSDO_CALLBACK_T callback,
                             uint32_t timeout,
                             bool block,
                             bool crc);

/******************************************************************************
* PRIVATE FUNCTIONS
******************************************************************************/

static void COCSdoAbort(CO_CSDO *csdo, uint32_t err)
{
    CO_IF_FRM frm;

    /* store abort code */
    csdo->Tfer.Abort = err;

    /* send the SDO timeout response */
    if (err == CO_SDO_ERR_TIMEOUT) {
        CO_SET_ID  (&frm, csdo->TxId        );
        CO_SET_BYTE(&frm, 0x80,           0u);
        CO_SET_WORD(&frm, csdo->Tfer.Idx, 1u);
        CO_SET_BYTE(&frm, csdo->Tfer.Sub, 3u);
        CO_SET_LONG(&frm, err,            4u);
        CO_SET_DLC (&frm,                 8u);

        (void)COIfCanSend(&csdo->Node->If, &frm);
    }
}

static void COCSdoReset(CO_CSDO *csdo, uint8_t num, struct CO_NODE_T *node)
{
    CO_CSDO *csdonum;
    int16_t  tid;

    ASSERT_PTR(csdo);
    ASSERT_LOWER(num, (uint8_t)CO_CSDO_N);

    if (csdo->State == CO_CSDO_STATE_BUSY) {
        /* Abort ongoing trasfer */
        COCSdoAbort(csdo, CO_SDO_ERR_TOS_STATE);
    }

    /* Reset handle */
    csdonum        = &csdo[num];
    csdonum->Node  = node;
    csdonum->Frm   = NULL;
    csdonum->RxId  = CO_SDO_ID_OFF;
    csdonum->TxId  = CO_SDO_ID_OFF;
    csdonum->State = CO_CSDO_STATE_INVALID;

    /* Reset transfer context */
    csdonum->Tfer.Csdo    = csdonum;
    csdonum->Tfer.Type    = CO_CSDO_TRANSFER_NONE;
    csdonum->Tfer.Abort   = 0;
    csdonum->Tfer.Idx     = 0;
    csdonum->Tfer.Sub     = 0;

    csdonum->Tfer.Tmt     = 0;
    csdonum->Tfer.Call    = NULL;
    csdonum->Tfer.Buf_Idx = 0;
    csdonum->Tfer.TBit    = 0;

    if (csdonum->Tfer.Tmr >= 0) {
        tid = COTmrDelete(&(node->Tmr), csdonum->Tfer.Tmr);
        if (tid < 0) {
            node->Error = CO_ERR_TMR_DELETE;
            return;
        }
    }
}

static void COCSdoEnable(CO_CSDO *csdo, uint8_t num)
{
    uint32_t  rxId;
    uint32_t  txId;
    uint8_t   nodeId;
    CO_NODE  *node;
    CO_CSDO  *csdonum;
    CO_ERR    err;

    ASSERT_LOWER(num, (uint8_t)CO_CSDO_N);

    csdonum        = &csdo[num];
    csdonum->RxId  = CO_SDO_ID_OFF;
    csdonum->TxId  = CO_SDO_ID_OFF;
    csdonum->State = CO_CSDO_STATE_INVALID;

    node = csdo->Node;
    err = CODictRdLong(&node->Dict, CO_DEV((uint32_t)0x1280u + (uint32_t)num, 1u), &txId);
    if (err != CO_ERR_NONE) {
        return;
    }
    err = CODictRdLong(&node->Dict, CO_DEV((uint32_t)0x1280u + (uint32_t)num, 2u), &rxId);
    if (err != CO_ERR_NONE) {
        return;
    }
    err = CODictRdByte(&node->Dict, CO_DEV((uint32_t)0x1280u + (uint32_t)num, 3u), &nodeId);
    if (err != CO_ERR_NONE) {
        return;
    }

    if (((rxId & CO_SDO_ID_OFF) == (uint32_t)0) &&
        ((txId & CO_SDO_ID_OFF) == (uint32_t)0)){
        csdonum->TxId  = txId + nodeId;
        csdonum->RxId  = rxId + nodeId;
        csdonum->State = CO_CSDO_STATE_IDLE;
    }
}

static void COCSdoTransferFinalize(CO_CSDO *csdo)
{
    CO_CSDO_CALLBACK_T call;
    uint16_t           idx;
    uint8_t            sub;
    uint32_t           code;

    if (csdo->State == CO_CSDO_STATE_BUSY) {
        /* Fetch transfer information */
        idx  = csdo->Tfer.Idx;
        sub  = csdo->Tfer.Sub;
        code = csdo->Tfer.Abort;
        call = csdo->Tfer.Call;

        if (call != NULL) {
            call(csdo, idx, sub, code);
        }

        /* Reset finished transfer information */
        csdo->Tfer.Type  = CO_CSDO_TRANSFER_NONE;
        csdo->Tfer.Abort = 0;
        csdo->Tfer.Idx   = 0;
        csdo->Tfer.Sub   = 0;
        csdo->Tfer.Buf   = NULL;
        csdo->Tfer.Size  = 0;
        csdo->Tfer.Tmt   = 0;
        csdo->Tfer.Call  = NULL;
        csdo->Tfer.Tmr   = -1;
        csdo->Tfer.Buf_Idx = 0;
        csdo->Tfer.TBit = 0;

        /* Release SDO client for next request */
        csdo->Frm   = NULL;
        csdo->State = CO_CSDO_STATE_IDLE;
    }
}

static void COCSdoTimeout(void *parg)
{
    CO_CSDO *csdo;

    csdo = (CO_CSDO *)parg;
    if (csdo->State == CO_CSDO_STATE_BUSY) {
        /* Abort SDO transfer because of timeout */
        COCSdoAbort(csdo, CO_SDO_ERR_TIMEOUT);
        /* Finalize aborted transfer */
        COCSdoTransferFinalize(csdo);
    }
}

static CO_ERR COCSdoUploadExpedited(CO_CSDO *csdo)
{
    CO_ERR  result = CO_ERR_SDO_SILENT;
    uint8_t width;
    uint8_t cmd;
    uint8_t n;

    cmd = CO_GET_BYTE(csdo->Frm, 0u);
    width = 4u - ((cmd >> 2u) & 0x03u);
    if (width > 0u) {
        if (width > (uint8_t)csdo->Tfer.Size) {
            COCSdoAbort(csdo, CO_SDO_ERR_MEM);
        } else {
            for (n = 0u; n < width; n++) {
                csdo->Tfer.Buf[n] = CO_GET_BYTE(csdo->Frm, n + 4u);
            }
        }
    }
    COCSdoTransferFinalize(csdo);
    return (result);
}

static CO_ERR COCSdoDownloadExpedited(CO_CSDO *csdo)
{
    COCSdoTransferFinalize(csdo);
    return CO_ERR_SDO_SILENT;
}

static CO_ERR COCSdoInitUploadSegmented(CO_CSDO *csdo)
{
    CO_ERR    result = CO_ERR_SDO_SILENT;
    uint32_t  obj_size;
    uint32_t  ticks;
    uint16_t  Idx;
    uint8_t   Sub;
    CO_IF_FRM frm;

    obj_size = CO_GET_LONG(csdo->Frm, 4u);
    Idx = CO_GET_WORD(csdo->Frm, 1u);
    Sub = CO_GET_BYTE(csdo->Frm, 3u);

    /* verify size, Idx, Sub */
    if ((obj_size == csdo->Tfer.Size) &&
        (Idx == csdo->Tfer.Idx) &&
        (Sub == csdo->Tfer.Sub)) {

        result = CO_ERR_NONE;

        /* setup CAN request */
        CO_SET_ID  (&frm, csdo->TxId);
        CO_SET_DLC (&frm, 8);
        CO_SET_BYTE(&frm, 0x60, 0u);
        CO_SET_WORD(&frm, 0, 1u);
        CO_SET_BYTE(&frm, 0, 3u);
        CO_SET_LONG(&frm, 0, 4u);

        /* refresh timer */
        (void)COTmrDelete(&(csdo->Node->Tmr), csdo->Tfer.Tmr);
        ticks = COTmrGetTicks(&(csdo->Node->Tmr), csdo->Tfer.Tmt, CO_TMR_UNIT_1MS);
        csdo->Tfer.Tmr = COTmrCreate(&(csdo->Node->Tmr), ticks, 0, &COCSdoTimeout, csdo);

        (void)COIfCanSend(&csdo->Node->If, &frm);
    } else {
        COCSdoAbort(csdo, CO_SDO_ERR_LEN);
        COCSdoTransferFinalize(csdo);
    }

    return result;
}

static CO_ERR COCSdoUploadSegmented(CO_CSDO *csdo)
{
    CO_ERR    result = CO_ERR_SDO_SILENT;
    uint32_t  ticks;
    uint8_t   cmd;
    uint8_t   n;
    CO_IF_FRM frm;

    cmd = CO_GET_BYTE(csdo->Frm, 0u);
    if (((cmd >> 4u) & 0x01u) == csdo->Tfer.TBit) {

        for (n = 1u; (n < 8u) && (csdo->Tfer.Buf_Idx < csdo->Tfer.Size); n++) {
            csdo->Tfer.Buf[csdo->Tfer.Buf_Idx] = CO_GET_BYTE(csdo->Frm, n);
            csdo->Tfer.Buf_Idx++;
        }

        if ((cmd & 0x01u) == 0x00u) {
            csdo->Tfer.TBit ^= 0x01u;

            CO_SET_ID  (&frm, csdo->TxId       );
            CO_SET_DLC (&frm, 8u               );

            if (csdo->Tfer.TBit == 0x01u){
                CO_SET_BYTE(&frm, 0x70, 0u);
            } else {
                CO_SET_BYTE(&frm, 0x60, 0u);
            }

            CO_SET_WORD(&frm, 0, 1u);
            CO_SET_BYTE(&frm, 0, 3u);
            CO_SET_LONG(&frm, 0, 4u);

            /* refresh timer */
            (void)COTmrDelete(&(csdo->Node->Tmr), csdo->Tfer.Tmr);
            ticks = COTmrGetTicks(&(csdo->Node->Tmr), csdo->Tfer.Tmt, CO_TMR_UNIT_1MS);
            csdo->Tfer.Tmr = COTmrCreate(&(csdo->Node->Tmr), ticks, 0, &COCSdoTimeout, csdo);

            (void)COIfCanSend(&csdo->Node->If, &frm);
        } else {
            COCSdoTransferFinalize(csdo);
        }
    } else {
        COCSdoAbort(csdo, CO_SDO_ERR_TBIT);
        COCSdoTransferFinalize(csdo);
    }

    return result;
}

static CO_ERR COCSdoInitDownloadBlock (CO_CSDO *csdo)
{
    CO_ERR    result = CO_ERR_SDO_SILENT;
    uint32_t  ticks;
    uint16_t  Idx;
    uint8_t   Sub;
    CO_IF_FRM frm;

    Idx = CO_GET_WORD(csdo->Frm, 1u);
    Sub = CO_GET_BYTE(csdo->Frm, 3u);
    if ((Idx == csdo->Tfer.Idx) &&
        (Sub == csdo->Tfer.Sub)) {
        
        // read the block size value returned from server before clearing the frame
        csdo->Block.Block_Size = CO_GET_BYTE(csdo->Frm, BLOCK_INIT_FRM_BLKSIZE_BYTE_OFFSET);

        CO_SET_ID  (&frm, csdo->TxId);
        CO_SET_DLC (&frm, 8u);
        CO_SET_LONG(&frm, 0, 0u);
        CO_SET_LONG(&frm, 0, 4u);

        // TODO: handle error or pass it back to caller?
        result = COCSdoDownloadBlockSendSubBlock(csdo);

    } else {
        COCSdoAbort(csdo, CO_SDO_ERR_TBIT); 
        COCSdoTransferFinalize(csdo);
    }

    return result;
}

static CO_ERR COCSdoDownloadBlockSendSubBlock(CO_CSDO *csdo){
    CO_ERR    result = CO_ERR_SDO_SILENT;
    uint32_t  ticks;
    uint8_t   Num_Segs;
    CO_IF_FRM frm;
    uint8_t   cmd = 0;
    CO_CSDO_BLOCK* block = &csdo->Block;

    uint8_t Seg_Num;
    for (Seg_Num = 1; Seg_Num < block->Block_Size; Seg_Num++) { 

        // write the sequence number to the cmd byte
        cmd = (uint8_t)WRITE_BITFIELD(CMD_SEQNUM_BIT_OFFSET, CMD_SEQNUM_MASK, Seg_Num);
        
        // determine size of data to send in segment and if last
        uint32_t size = block->Size - block->Index;
        if (size > BLOCK_FRM_SEG_DATA_BYTE_SIZE) {
            block->C_Bit = CMD_C_MORE_SEGMENTS_TO_BE_DOWNLOADED;
            size = BLOCK_FRM_SEG_DATA_BYTE_SIZE;
        } else {
            block->C_Bit = CMD_C_NO_MORE_SEGMENTS_TO_BE_DOWNLOADED;  
        }
        
        // save the number of data bytes send for use in end stage
        block->Data_Bytes_Frm = size;

        // write C bit to cmd byte
        cmd |= (uint8_t)WRITE_BITFIELD( CMD_C_BIT_OFFSET, CMD_C_MASK, block->C_Bit);

        // write cmd byte to frame
        CO_SET_BYTE(&frm, cmd, FRM_CMD_BYTE_OFFSET);

        // write data to frame
        for (int i = 0; i < size; i++) {
            CO_SET_BYTE(&frm, block->Buf[block->Index++],BLOCK_INIT_FRM_BLKSIZE_BYTE_OFFSET + i );
        }

        // send the frame
        /* refresh timer */
        (void)COTmrDelete(&(csdo->Node->Tmr), csdo->Tfer.Tmr);
        ticks = COTmrGetTicks(&(csdo->Node->Tmr), csdo->Tfer.Tmt, CO_TMR_UNIT_1MS);
        csdo->Tfer.Tmr = COTmrCreate(&(csdo->Node->Tmr), ticks, 0, &COCSdoTimeout, csdo);

        (void)COIfCanSend(&csdo->Node->If, &frm);

        // break if last segment
        if (block->C_Bit == CMD_C_NO_MORE_SEGMENTS_TO_BE_DOWNLOADED) { 
            break;
        }
    }
    return result;
}

static CO_ERR COCSdoDownloadBlock(CO_CSDO *csdo) {
    // SCS and SC bits have already been checked before calling this function

    CO_ERR    result = CO_ERR_SDO_SILENT;
    uint32_t  ticks;
    uint8_t   cmd = 0;
    uint8_t   n;
    CO_IF_FRM frm;
    CO_CSDO_BLOCK   *block = &csdo->Block;

    // read the ackseq and blksize bytes from the frame
    uint8_t ackseq = CO_GET_BYTE(csdo->Frm, BLOCK_FRM_ACKSEQ_BYTE_OFFSET);
    uint8_t blksize = CO_GET_BYTE(csdo->Frm, BLOCK_FRM_SUBBLOCK_BLKSIZE_BYTE_OFFSET);

    block->Index = block->Blk_Offset + (ackseq * BLOCK_FRM_SEG_DATA_BYTE_SIZE);
    if (block->Index >= block->Size) {
        // all data has been successfuly recieved by server, send end frame
        cmd = (uint8_t)WRITE_BITFIELD(
                CMD_CCS_BIT_OFFSET, 
                CMD_CCS_MASK, 
                CMD_CCS_BLOCK_DOWNLOAD);

        // calculate and write the number of bytes that do not contain data in last segment
        n = BLOCK_FRM_SEG_DATA_BYTE_SIZE - block->Data_Bytes_Frm;
        cmd |= (uint8_t)WRITE_BITFIELD(
                CMD_N_BIT_OFFSET, 
                CMD_N_BIT_MASK, 
                n);

        // write the cs bits
        cmd |= (uint8_t)WRITE_BITFIELD(
                BLOCK_DOWNLOAD_CMD_CS_BIT_OFFSET,
                BLOCK_DOWNLOAD_CMD_CS_MASK,
                BLOCK_DOWNLOAD_CMD_CS_END_BLOCK_DOWNLOAD_REQUEST);

        if (block->CRC == GENERATE_CRC) { 
            // TODO: write CRC 
        }
        
        // send the frame
        /* refresh timer */
        (void)COTmrDelete(&(csdo->Node->Tmr), csdo->Tfer.Tmr);
        ticks = COTmrGetTicks(&(csdo->Node->Tmr), csdo->Tfer.Tmt, CO_TMR_UNIT_1MS);
        csdo->Tfer.Tmr = COTmrCreate(&(csdo->Node->Tmr), ticks, 0, &COCSdoTimeout, csdo);

        (void)COIfCanSend(&csdo->Node->If, &frm);

    } else {
        // update the block size just in case it changed
        block->Block_Size = blksize;
        // send the next block
        result = COCSdoDownloadBlockSendSubBlock(csdo);
    }
    return result;
}

static CO_ERR COCSdoDownloadBlockEnd(CO_CSDO *csdo) {
    // calling function already checked for scs and ss
    // if we got here, we got a successful download to the server so just need
    // to finalize and close the SDO session. 

    COCSdoTransferFinalize(csdo);
    return CO_ERR_SDO_SILENT;
}

static CO_ERR COCSdoInitUploadBlock(CO_CSDO *csdo){
    CO_ERR    result = CO_ERR_SDO_SILENT;
    uint32_t  ticks;
    uint16_t  Idx;
    uint8_t   Sub;
    uint8_t   n;
    uint8_t   width;
    uint8_t   c_bit = 1;
    uint8_t   cmd;
    CO_IF_FRM frm;

    Idx = CO_GET_WORD(csdo->Frm, 1u);
    Sub = CO_GET_BYTE(csdo->Frm, 3u);
    if ((Idx == csdo->Tfer.Idx) &&
        (Sub == csdo->Tfer.Sub)) {

        cmd = CO_GET_BYTE(csdo->Frm, FRM_CMD_BYTE_OFFSET);
        uin32_t size = CO_GET_LONG(csdo->Frm, BLOCK_FRM_SIZE_BYTE_OFFSET);

        CO_SET_ID  (&frm, csdo->TxId);
        CO_SET_DLC (&frm, 8u);
        CO_SET_LONG(&frm, 0, 0u);
        CO_SET_LONG(&frm, 0, 4u);

 }

static CO_ERR COCSdoInitDownloadSegmented(CO_CSDO *csdo)
{
    CO_ERR    result = CO_ERR_SDO_SILENT;
    uint32_t  ticks;
    uint16_t  Idx;
    uint8_t   Sub;
    uint8_t   n;
    uint8_t   width;
    uint8_t   c_bit = 1;
    uint8_t   cmd;
    CO_IF_FRM frm;

    Idx = CO_GET_WORD(csdo->Frm, 1u);
    Sub = CO_GET_BYTE(csdo->Frm, 3u);
    if ((Idx == csdo->Tfer.Idx) &&
        (Sub == csdo->Tfer.Sub)) {

        CO_SET_ID  (&frm, csdo->TxId);
        CO_SET_DLC (&frm, 8u);
        CO_SET_LONG(&frm, 0, 0u);
        CO_SET_LONG(&frm, 0, 4u);

        width = csdo->Tfer.Size - csdo->Tfer.Buf_Idx;
        if (width > 7u) {
            width = 7u;
            c_bit = 0u;
        }

        for (n = 1; n <= width; n++) {
            CO_SET_BYTE(&frm, csdo->Tfer.Buf[csdo->Tfer.Buf_Idx], n);
            csdo->Tfer.Buf_Idx++;
        }

        cmd = (uint8_t)(csdo->Tfer.TBit << 4u) |
              (uint8_t)(((7u - width) << 1u)) | 
              (uint8_t)(c_bit);
        CO_SET_BYTE(&frm, cmd, 0u);

        /* refresh timer */
        (void)COTmrDelete(&(csdo->Node->Tmr), csdo->Tfer.Tmr);
        ticks = COTmrGetTicks(&(csdo->Node->Tmr), csdo->Tfer.Tmt, CO_TMR_UNIT_1MS);
        csdo->Tfer.Tmr = COTmrCreate(&(csdo->Node->Tmr), ticks, 0, &COCSdoTimeout, csdo);

        (void)COIfCanSend(&csdo->Node->If, &frm);
    } else {
        COCSdoAbort(csdo, CO_SDO_ERR_TBIT); 
        COCSdoTransferFinalize(csdo);
    }

    return result;
}

static CO_ERR COCSdoDownloadSegmented(CO_CSDO *csdo)
{
    CO_ERR    result = CO_ERR_SDO_SILENT;
    uint32_t  ticks;
    uint8_t   cmd;
    uint8_t   n;
    uint8_t   width;
    uint8_t   c_bit = 1;
    CO_IF_FRM frm;

    cmd = CO_GET_BYTE(csdo->Frm, 0u);
    if (((cmd >> 4u) & 0x01u) == csdo->Tfer.TBit) {
        csdo->Tfer.TBit ^= 0x01u;

        CO_SET_ID  (&frm, csdo->TxId       );
        CO_SET_DLC (&frm, 8                );

         /* clean frm data */
        CO_SET_LONG(&frm, 0, 0u);
        CO_SET_LONG(&frm, 0, 4u);

        width = csdo->Tfer.Size - csdo->Tfer.Buf_Idx;

        if (width > 7u) {
            width = 7u;
            c_bit = 0u;
        }
        
        for (n = 1; n <= width; n++) {
            CO_SET_BYTE(&frm, csdo->Tfer.Buf[csdo->Tfer.Buf_Idx], n);
            csdo->Tfer.Buf_Idx++;
        }

        cmd = (uint8_t)(csdo->Tfer.TBit << 4u) |
              (uint8_t)(((7u - width) << 1u)) | 
              (uint8_t)(c_bit);

        CO_SET_BYTE(&frm, cmd, 0u);

         /* refresh timer */
        (void)COTmrDelete(&(csdo->Node->Tmr), csdo->Tfer.Tmr);
        ticks = COTmrGetTicks(&(csdo->Node->Tmr), csdo->Tfer.Tmt, CO_TMR_UNIT_1MS);
        csdo->Tfer.Tmr = COTmrCreate(&(csdo->Node->Tmr), ticks, 0, &COCSdoTimeout, csdo);

        (void)COIfCanSend(&csdo->Node->If, &frm);
    } else {
        COCSdoAbort(csdo, CO_SDO_ERR_TBIT);
        COCSdoTransferFinalize(csdo);
    }

    return result;
}

static CO_ERR COCSdoFinishDownloadSegmented(CO_CSDO *csdo)
{
    COCSdoTransferFinalize(csdo);
    return CO_ERR_SDO_SILENT;
}

/******************************************************************************
* PROTECTED API FUNCTIONS
******************************************************************************/

void COCSdoInit(CO_CSDO *csdo, struct CO_NODE_T *node)
{
    uint8_t n;

    for (n = 0; n < (uint8_t)CO_CSDO_N; n++) {
        node->CSdo[n].Tfer.Tmr = -1;
        COCSdoReset(csdo, n, node);
        COCSdoEnable(csdo, n);
    }
}

CO_CSDO *COCSdoCheck(CO_CSDO *csdo, CO_IF_FRM *frm)
{
    CO_CSDO *result;
    uint8_t  n;

    result = NULL;

    if (frm != NULL) {
        n = 0;
        while ((n       < (uint8_t)CO_CSDO_N) &&
               (result == NULL              )) {
            /*
             * Match configured COB-ID
             * and current client state.
             * Idle client state means
             * that it did not initiate
             * any transfer (or timed out),
             * which means we are not
             * interested in response
             * anymore.
             */
            if ((CO_GET_ID(frm) == csdo[n].RxId) &&
                (csdo[n].State  == CO_CSDO_STATE_BUSY)) {
                /*
                 * Update frame with COB-ID
                 * and return client handle
                 * for further processing.
                 */
                CO_SET_ID(frm, csdo[n].TxId);
                csdo[n].Frm        = frm;
                csdo[n].Tfer.Abort = 0;
                result = &csdo[n];
                break;
            }
            n++;
        }
    }

    return (result);
}

CO_ERR COCSdoResponse(CO_CSDO *csdo)
{
    CO_ERR   result = CO_ERR_SDO_SILENT;
    uint16_t index;
    uint8_t  cmd;
    uint8_t  sub;

    cmd = CO_GET_BYTE(csdo->Frm, 0u);

    if (cmd == 0x80u) {
        /* SDO abort protocol */
        index = CO_GET_WORD(csdo->Frm, 1u);
        sub   = CO_GET_BYTE(csdo->Frm, 3u);
        if ((index == csdo->Tfer.Idx) &&
            (sub   == csdo->Tfer.Sub)) {
            csdo->Tfer.Abort = CO_GET_LONG(csdo->Frm, 4u);
            COCSdoTransferFinalize(csdo);
            return (result);
        }
    }

    uint8_t scs = READ_BITFIELD(CMD_SCS_BIT_OFFSET,
                                CMD_SCS_MASK, 
                                cmd);
    uint8_t ss = READ_BITFIELD( BLOCK_DOWNLOAD_CMD_SS_BIT_OFFSET, 
                                BLOCK_DOWNLOAD_CMD_SS_MASK, 
                                cmd);

    if (csdo->Tfer.Type == CO_CSDO_TRANSFER_UPLOAD_BLOCK) {
        if (scs == CMD_SCS_BLOCK_UPLOAD) {
            if (ss == BLOCK_UPLOAD_CMD_SS_INITIATE_UPLOAD_RESPONSE) {
                (void)COCSdoInitUploadBlock(csdo);
            } else if (ss == BLOCK_UPLOAD_CMD_SS_END_BLOCK_UPLOAD_RESPONSE) {
                (void)COCSdoUploadEndBlock(csdo);
            } else {
                // no SS bit for upload SubBlock
                (void)COCSdoUploadSubBlock(csdo);
        } else if ((scs == CMD_SCS_INIT_UPLOAD) && 
            (CMD_S_DATA_SET_SIZE_IS_INDICATED == READ_BITFIELD(NONBLOCK_CMD_S_BIT_OFFSET,
                                                               CMD_S_MASK,
                                                               cmd))) {
            // server decided to switch to segmented upload
            // TODO: make sure structure is properly initialized for segmented
            csdo->Tfer.Type = CO_CSDO_TRANSFER_UPLOAD_SEGMENT;
            (void)COCSdoInitUploadSegmented(csdo);
        } else {
            COCSdoAbort(csdo, CO_SDO_ERR_CMD);
            COCSdoTransferFinalize(csdo);
        }
    } else if (csdo->Tfer.Type == CO_CSDO_TRANSFER_DOWNLOAD_BLOCK) {
        if (scs == CMD_SCS_BLOCK_DOWNLOAD) {
            if (ss == BLOCK_DOWNLOAD_CMD_SS_INITIATE_DOWNLOAD_RESPONSE) {
                (void)COCSdoInitDownloadBlock(csdo);
            } else if (ss == BLOCK_DOWNLOAD_CMD_SS_BLOCK_DOWNLOAD_RESPONSE) {
                (void)COCSdoDownloadBlock(csdo);
            } else if (ss == BLOCK_DOWNLOAD_CMD_SS_END_BLOCK_DOWNLOAD_RESPONSE) {
                (void)COCSdoDownloadBlockEnd(csdo);
            } else {
                COCSdoAbort(csdo, CO_SDO_ERR_CMD);
                COCSdoTransferFinalize(csdo);
            }
        } else {
            COCSdoAbort(csdo, CO_SDO_ERR_CMD);
            COCSdoTransferFinalize(csdo);
        }
     } else if (csdo->Tfer.Type == CO_CSDO_TRANSFER_UPLOAD_SEGMENT) {
        if (cmd == 0x41u) {
            (void)COCSdoInitUploadSegmented(csdo);
        } else if ((cmd & 0xE0u) == 0x00u) {
            (void)COCSdoUploadSegmented(csdo);
        } else {
            COCSdoAbort(csdo, CO_SDO_ERR_CMD);
            COCSdoTransferFinalize(csdo);
        }
    } else if (csdo->Tfer.Type == CO_CSDO_TRANSFER_DOWNLOAD_SEGMENT) {
        if (cmd == 0x60u) {
            (void)COCSdoInitDownloadSegmented(csdo);
        } else if (((cmd & 0xE0u) ==  0x20u) ) {
            if (csdo->Tfer.Size > csdo->Tfer.Buf_Idx) {
                (void)COCSdoDownloadSegmented(csdo);
            } else {
                /* wait last response */
                (void)COCSdoFinishDownloadSegmented(csdo);
            }
        } else {
            COCSdoAbort(csdo, CO_SDO_ERR_CMD);
            COCSdoTransferFinalize(csdo);
        }
    } else if (cmd == 0x60u) {
        result = COCSdoDownloadExpedited(csdo);
        return (result);
    } else if ((cmd & 0x43u) != 0u) {
        result = COCSdoUploadExpedited(csdo);
        return (result);
    } else {
        COCSdoAbort(csdo, CO_SDO_ERR_PARA_INCOMP);
    }
    
    return (result);
}

/******************************************************************************
* PUBLIC API FUNCTIONS
******************************************************************************/

CO_CSDO *COCSdoFind(struct CO_NODE_T *node, uint8_t num)
{
    CO_CSDO *result;

    ASSERT_PTR_ERR(node, 0);
    ASSERT_LOWER_ERR(num, (uint8_t)CO_CSDO_N, 0);

    result = &node->CSdo[num];
    if (result->State <= CO_CSDO_STATE_INVALID) {
        /* Client is found but not enabled */
        result = NULL;
    }

    return (result);
}

CO_ERR COCSdoRequestUpload(CO_CSDO *csdo,
                           uint32_t key,
                           uint8_t *buf,
                           uint32_t size,
                           CO_CSDO_CALLBACK_T callback,
                           uint32_t timeout){
    COCSdoRequestUploadFull(csdo, key, buf, size, callback, timeout, false, false, 0, 0);
}

CO_ERR COCSdoRequestUploadBlock(CO_CSDO *csdo,
                           uint32_t key,
                           uint8_t *buf,
                           uint32_t size,
                           CO_CSDO_CALLBACK_T callback,
                           uint32_t timeout,
                           bool crc,
                           uint8_t blksize,
                           uint8_t pst){
    COCSdoRequestUploadFull(csdo, key, buf, size, callback, timeout, true, crc, blksize, pst);
}

CO_ERR COCSdoRequestUploadFull(CO_CSDO *csdo,
                           uint32_t key,
                           uint8_t *buf,
                           uint32_t size,
                           CO_CSDO_CALLBACK_T callback,
                           uint32_t timeout,
                           bool block,
                           bool crc,
                           uint8_t blksize,
                           uint8_t pst){
{
    CO_IF_FRM frm;
    uint32_t  ticks;
    uint8_t   cmd;

    ASSERT_PTR_ERR(csdo, CO_ERR_BAD_ARG);
    ASSERT_PTR_ERR(buf, CO_ERR_BAD_ARG);
    ASSERT_NOT_ERR(size, (uint32_t)0, CO_ERR_BAD_ARG);

    if (callback == (CO_CSDO_CALLBACK_T)NULL) {
        /* no callback is given */
        return CO_ERR_BAD_ARG;
    }
    if (csdo->State == CO_CSDO_STATE_INVALID) {
        /* Requested SDO client is disabled */
        return CO_ERR_SDO_OFF;
    }
    if (csdo->State == CO_CSDO_STATE_BUSY) {
        /* Requested SDO client is busy */
        return CO_ERR_SDO_BUSY;
    }

    /* Set client as busy to prevent its usage
     * until requested transfer is complete
     */
    csdo->State = CO_CSDO_STATE_BUSY;

    /* Update transfer info */
    if (block == true) {
        // create and send first block upload frame
        cmd = WRITE_BITFIELD(
                        CMD_CCS_BIT_OFFSET,
                        CMD_CCS_BIT_MASK,
                        CMD_CCS_BLOCK_UPLOAD);
        if (crc == true) {
            cmd |= WRITE_BITFIELD(
                         CMD_CC_BIT_OFFSET                                                           
                         CMD_CC_MASK                                   
                         CMD_CC_CLIENT_SUPPORTs_CRC_GENERATION);
        } else {
            cmd |= WRITE_BITFIELD(
                         CMD_CC_BIT_OFFSET                                                           
                         CMD_CC_MASK                                   
                         CMD_CC_CLIENT_DOES_NOT_SUPPORT_CRC_GENERATION);
        }
        cmd |= WRITE_BITFIELD{
                         BLOCK_UPLOAD_CMD_CS_BIT_OFFSET              
                         BLOCK_UPLOAD_CMD_CS_MASK                    
                         BLOCK_UPLOAD_CMD_CS_INITIATE_UPLOAD_REQUEST 
        }
        csdo->Tfer.Type = CO_CSDO_TRANSFER_UPLOAD_BLOCK;
        
        /* Transmit transfer initiation directly */
        CO_SET_ID  (&frm, csdo->TxId        );
        CO_SET_DLC (&frm, 8u                );
        CO_SET_BYTE(&frm, cmd,              FRM_CMD_BYTE_OFFSET);
        CO_SET_WORD(&frm, csdo->Tfer.Idx,   1u);
        CO_SET_BYTE(&frm, csdo->Tfer.Sub,   3u);
        CO_SET_BYTE(&frm, blksize,          BLOCK_INIT_FRM_BLKSIZE_BYTE_OFFSET);
        CO_SET_BYTE(&frm, pst,              BLOCK_INIT_FRM_PST_BYTE_OFFSET);
        CO_SET_WORD(&frm, 0,                6u);

    } else {
        if (size <= (uint32_t)4) {
            /* expedited transfer */
            csdo->Tfer.Type = CO_CSDO_TRANSFER_UPLOAD;
        } else {
            /* segmented transfer */
            csdo->Tfer.Type = CO_CSDO_TRANSFER_UPLOAD_SEGMENT;
        }
   
        csdo->Tfer.Abort   = 0;
        csdo->Tfer.Idx     = CO_GET_IDX(key);
        csdo->Tfer.Sub     = CO_GET_SUB(key);
        csdo->Tfer.Buf     = buf;
        csdo->Tfer.Size    = size;
        csdo->Tfer.Tmt     = timeout;
        csdo->Tfer.Call    = callback;
        csdo->Tfer.Buf_Idx = 0;
        csdo->Tfer.TBit    = 0;

        /* Transmit transfer initiation directly */
        CO_SET_ID  (&frm, csdo->TxId        );
        CO_SET_DLC (&frm, 8u                );
        CO_SET_BYTE(&frm, 0x40          , 0u);
        CO_SET_WORD(&frm, csdo->Tfer.Idx, 1u);
        CO_SET_BYTE(&frm, csdo->Tfer.Sub, 3u);
        CO_SET_LONG(&frm, 0,              4u);
    }

    ticks = COTmrGetTicks(&(csdo->Node->Tmr), timeout, CO_TMR_UNIT_1MS);
    csdo->Tfer.Tmr = COTmrCreate(&(csdo->Node->Tmr), ticks, 0, &COCSdoTimeout, csdo);

    (void)COIfCanSend(&csdo->Node->If, &frm);

    return CO_ERR_NONE;
}

// legacy segmented / expedited
CO_ERR COCSdoRequestDownload(CO_CSDO *csdo,
                             uint32_t key,
                             uint8_t *buffer,
                             uint32_t size,
                             CO_CSDO_CALLBACK_T callback,
                             uint32_t timeout) {
    return COCSdoRequestDownloadFull(csdo, key, buffer, size, callback, timeout, false, false);
}

// block download
CO_ERR COCSdoRequestDownloadBlock(CO_CSDO *csdo,
                             uint32_t key,
                             uint8_t *buffer,
                             uint32_t size,
                             CO_CSDO_CALLBACK_T callback,
                             uint32_t timeout,
                             bool crc) {
    return COCSdoRequestDownloadFull(csdo, key, buffer, size, callback, timeout, true, crc);
}

static CO_ERR COCSdoRequestDownloadFull(CO_CSDO *csdo,
                             uint32_t key,
                             uint8_t *buffer,
                             uint32_t size,
                             CO_CSDO_CALLBACK_T callback,
                             uint32_t timeout,
                             bool block,
                             bool crc)
{
    CO_IF_FRM frm;
    uint8_t   cmd;
    uint8_t   n;
    uint32_t  num;
    uint32_t  ticks;

    ASSERT_PTR_ERR(csdo, CO_ERR_BAD_ARG);
    ASSERT_PTR_ERR(buffer, CO_ERR_BAD_ARG);
    ASSERT_NOT_ERR(size, (uint32_t)0, CO_ERR_BAD_ARG);

    if (callback == (CO_CSDO_CALLBACK_T)NULL) {
        /* no callback is given */
        return CO_ERR_BAD_ARG;
    }
    if (csdo->State == CO_CSDO_STATE_INVALID) {
        /* Requested SDO client is disabled */
        return CO_ERR_SDO_OFF;
    }
    if (csdo->State == CO_CSDO_STATE_BUSY) {
        /* Requested SDO client is busy */
        return CO_ERR_SDO_BUSY;
    }

    /* Set client as busy to prevent its usage
     * until requested transfer is complete
     */
    csdo->State = CO_CSDO_STATE_BUSY;

    /* Update transfer info */
    csdo->Tfer.Abort   = 0;
    csdo->Tfer.Idx     = CO_GET_IDX(key);
    csdo->Tfer.Sub     = CO_GET_SUB(key);
    csdo->Tfer.Buf     = buffer;
    csdo->Tfer.Size    = size;
    csdo->Tfer.Tmt     = timeout;
    csdo->Tfer.Call    = callback;
    csdo->Tfer.Buf_Idx = 0;
    csdo->Tfer.TBit    = 0;

    if (block== true) {
        csdo->Tfer.Type = CO_CSDO_TRANSFER_DOWNLOAD_BLOCK;
        
        cmd = (uint8_t)WRITE_BITFIELD(   CMD_CCS_BIT_OFFSET,
                                CMD_CCS_MASK,
                                CMD_CCS_BLOCK_DOWNLOAD);
        if (crc == true) {
            cmd |= (uint8_t)WRITE_BITFIELD(  
                                    CMD_CC_BIT_OFFSET,
                                    CMD_CC_MASK,
                                    CMD_CC_CLIENT_SUPPORTS_CRC_GENERATION);
        } else {
            cmd |= (uint8_t)WRITE_BITFIELD(   
                                    CMD_CC_BIT_OFFSET,
                                    CMD_CC_MASK,
                                    CMD_CC_CLIENT_DOES_NOT_SUPPORT_CRC_GENERATION);
        }
        cmd |= (uint8_t)WRITE_BITFIELD(  CMD_S_BIT_OFFSET,
                                CMD_S_MASK,
                                CMD_S_DATA_SET_SIZE_IS_INDICATED);

        cmd |= (uint8_t)WRITE_BITFIELD(  BLOCK_DOWNLOAD_CMD_CS_BIT_OFFSET,
                                BLOCK_DOWNLOAD_CMD_CS_MASK,
                                BLOCK_DOWNLOAD_CMD_CS_INITIATE_DOWNLOAD_REQUEST);
        // set the size bytes
        CO_SET_LONG(&frm, size, BLOCK_FRM_SIZE_BYTE_OFFSET);
    }
    else if (size <= (uint32_t)4u) {
        csdo->Tfer.Type = CO_CSDO_TRANSFER_DOWNLOAD;

        cmd = ((0x23u) | ((4u - (uint8_t)size) << 2u));
        CO_SET_BYTE(&frm, cmd, 0u);

        num = size;
        for (n = 4u; n < 8u; n++) {
            if (num > (uint8_t)0u) {
                CO_SET_BYTE(&frm, *buffer, n);
                num--;
                buffer++;
            } else {
                CO_SET_BYTE(&frm, 0u, n);
            }
        }
    } else {
        csdo->Tfer.Type = CO_CSDO_TRANSFER_DOWNLOAD_SEGMENT;

        cmd = 0x21u;
        CO_SET_BYTE(&frm,  cmd, 0u);
        CO_SET_LONG(&frm, size, 4u);
    }

    /* Transmit transfer initiation directly */
    CO_SET_ID  (&frm, csdo->TxId        );
    CO_SET_DLC (&frm, 8u                );
    CO_SET_WORD(&frm, csdo->Tfer.Idx, 1u);
    CO_SET_BYTE(&frm, csdo->Tfer.Sub, 3u);

    ticks = COTmrGetTicks(&(csdo->Node->Tmr), timeout, CO_TMR_UNIT_1MS);
    csdo->Tfer.Tmr = COTmrCreate(&(csdo->Node->Tmr), ticks, 0, &COCSdoTimeout, csdo);

    (void)COIfCanSend(&csdo->Node->If, &frm);

    return CO_ERR_NONE;
}

#endif
