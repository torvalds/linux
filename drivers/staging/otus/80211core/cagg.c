/*
 * Copyright (c) 2007-2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*                                                                      */
/*  Module Name : cagg.c                                                */
/*                                                                      */
/*  Abstract                                                            */
/*      This module contains A-MPDU aggregation related functions.      */
/*                                                                      */
/*  NOTES                                                               */
/*      None                                                            */
/*                                                                      */
/************************************************************************/

#include "cprecomp.h"

extern u8_t zcUpToAc[8];
const u8_t pri[] = {3,3,2,3,2,1,3,2,1,0};


u16_t aggr_count;
u32_t success_mpdu;
u32_t total_mpdu;

void zfAggInit(zdev_t* dev)
{
    u16_t i,j;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();
    /*
     * reset sta information
     */

    zmw_enter_critical_section(dev);
    wd->aggInitiated = 0;
    wd->addbaComplete = 0;
    wd->addbaCount = 0;
    wd->reorder = 1;
    for (i=0; i<ZM_MAX_STA_SUPPORT; i++)
    {
        for (j=0; j<ZM_AC; j++)
        {
            //wd->aggSta[i].aggQNumber[j] = ZM_AGG_POOL_SIZE;
            wd->aggSta[i].aggFlag[j] = wd->aggSta[i].count[j] = 0;
            wd->aggSta[i].tid_tx[j] = NULL;
            wd->aggSta[i].tid_tx[j+1] = NULL;

        }
    }

    /*
     * reset Tx/Rx aggregation queue information
     */
    wd->aggState = 0;
    for (i=0; i<ZM_AGG_POOL_SIZE; i++)
    {
        /*
         * reset tx aggregation queue
         */
        wd->aggQPool[i] = zfwMemAllocate(dev, sizeof(struct aggQueue));
        if(!wd->aggQPool[i])
        {
            zmw_leave_critical_section(dev);
            return;
        }
        wd->aggQPool[i]->aggHead = wd->aggQPool[i]->aggTail =
        wd->aggQPool[i]->aggQEnabled = wd->aggQPool[i]->aggReady =
        wd->aggQPool[i]->clearFlag = wd->aggQPool[i]->deleteFlag = 0;
        //wd->aggQPool[i]->aggSize = 16;

        /*
         * reset rx aggregation queue
         */
        wd->tid_rx[i] = zfwMemAllocate(dev, sizeof(struct agg_tid_rx));
        if (!wd->tid_rx[i])
        {
            zmw_leave_critical_section(dev);
            return;
        }
        wd->tid_rx[i]->aid = ZM_MAX_STA_SUPPORT;
        wd->tid_rx[i]->seq_start = wd->tid_rx[i]->baw_head = \
        wd->tid_rx[i]->baw_tail = 0;
        wd->tid_rx[i]->sq_exceed_count = wd->tid_rx[i]->sq_behind_count = 0;
        for (j=0; j<=ZM_AGG_BAW_SIZE; j++)
            wd->tid_rx[i]->frame[j].buf = 0;
        /*
         * reset ADDBA exchange status code
         * 0: NULL
         * 1: ADDBA Request sent/received
         * 2: ACK for ADDBA Request sent/received
         * 3: ADDBA Response sent/received
         * 4: ACK for ADDBA Response sent/received
         */
        wd->tid_rx[i]->addBaExchangeStatusCode = 0;

    }
    zmw_leave_critical_section(dev);
    zfAggTallyReset(dev);
    DESTQ.init = zfAggDestInit;
    DESTQ.init(dev);
    wd->aggInitiated = 1;
    aggr_count = 0;
    success_mpdu = 0;
    total_mpdu = 0;
#ifdef ZM_ENABLE_AGGREGATION
#ifndef ZM_ENABLE_FW_BA_RETRANSMISSION //disable BAW
    BAW = zfwMemAllocate(dev, sizeof(struct baw_enabler));
    if(!BAW)
    {
        return;
    }
    BAW->init = zfBawInit;
    BAW->init(dev);
#endif //disable BAW
#endif
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfAggGetSta                 */
/*      return STA AID.                                                 */
/*      take buf as input, use the dest address of buf as index to      */
/*      search STA AID.                                                 */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : buffer for one particular packet                          */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      AID                                                             */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Honda               ZyDAS Technology Corporation    2006.11     */
/*                                                                      */
/************************************************************************/



u16_t zfAggGetSta(zdev_t* dev, zbuf_t* buf)
{
    u16_t id;
    u16_t dst[3];

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    dst[0] = zmw_rx_buf_readh(dev, buf, 0);
    dst[1] = zmw_rx_buf_readh(dev, buf, 2);
    dst[2] = zmw_rx_buf_readh(dev, buf, 4);

    zmw_enter_critical_section(dev);

    if(wd->wlanMode == ZM_MODE_AP) {
        id = zfApFindSta(dev, dst);
    }
    else {
        id = 0;
    }
    zmw_leave_critical_section(dev);

#if ZM_AGG_FPGA_DEBUG
    id = 0;
#endif

    return id;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfAggTxGetQueue             */
/*      return Queue Pool index.                                        */
/*      take aid as input, look for the queue index associated          */
/*      with this aid.                                                  */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      aid : associated id                                             */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      Queue number                                                    */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Honda               ZyDAS Technology Corporation    2006.11     */
/*                                                                      */
/************************************************************************/
TID_TX zfAggTxGetQueue(zdev_t* dev, u16_t aid, u16_t tid)
{
    //u16_t   i;
    TID_TX  tid_tx;
    zmw_get_wlan_dev(dev);

    //zmw_declare_for_critical_section();

    /*
     * not a STA aid
     */
    if (0xffff == aid)
        return NULL;

    //zmw_enter_critical_section(dev);

    tid_tx = wd->aggSta[aid].tid_tx[tid];
    if (!tid_tx) return NULL;
    if (0 == tid_tx->aggQEnabled)
        return NULL;

    //zmw_leave_critical_section(dev);

    return tid_tx;
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfAggTxNewQueue             */
/*      return Queue Pool index.                                        */
/*      take aid as input, find a new queue for this aid.               */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      aid : associated id                                             */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      Queue number                                                    */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Honda               ZyDAS Technology Corporation    2006.12     */
/*                                                                      */
/************************************************************************/
TID_TX zfAggTxNewQueue(zdev_t* dev, u16_t aid, u16_t tid, zbuf_t* buf)
{
    u16_t   i;
    TID_TX  tid_tx=NULL;
    u16_t   ac = zcUpToAc[tid&0x7] & 0x3;
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    /*
     * not a STA aid
     */
    if (0xffff == aid)
        return NULL;

    zmw_enter_critical_section(dev);

    /*
     * find one new queue for sta
     */
    for (i=0; i<ZM_AGG_POOL_SIZE; i++)
    {
        if (wd->aggQPool[i]->aggQEnabled)
        {
                /*
                 * this q is enabled
                 */
        }
        else
        {
            tid_tx = wd->aggQPool[i];
            tid_tx->aggQEnabled = 1;
            tid_tx->aggQSTA = aid;
            tid_tx->ac = ac;
            tid_tx->tid = tid;
            tid_tx->aggHead = tid_tx->aggTail = tid_tx->size = 0;
            tid_tx->aggReady = 0;
            wd->aggSta[aid].tid_tx[tid] = tid_tx;
            tid_tx->dst[0] = zmw_rx_buf_readh(dev, buf, 0);
            tid_tx->dst[1] = zmw_rx_buf_readh(dev, buf, 2);
            tid_tx->dst[2] = zmw_rx_buf_readh(dev, buf, 4);
            break;
        }
    }

    zmw_leave_critical_section(dev);

    return tid_tx;
}



/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfAggTxEnqueue              */
/*      return Status code ZM_SUCCESS or error code                     */
/*      take (aid,ac,qnum,buf) as input                                 */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      aid : associated id                                             */
/*      ac  : access category                                           */
/*      qnum: the queue number to which will be enqueued                */
/*      buf : the packet to be queued                                   */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      status code                                                     */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Honda               Atheros Communications, INC.    2006.12     */
/*                                                                      */
/************************************************************************/
u16_t zfAggTxEnqueue(zdev_t* dev, zbuf_t* buf, u16_t aid, TID_TX tid_tx)
{
    //u16_t   qlen, frameLen;
    u32_t   time;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    tid_tx->size = zm_agg_qlen(dev, tid_tx->aggHead, tid_tx->aggTail);

    if (tid_tx->size < (ZM_AGGQ_SIZE - 2))
    {
        /* Queue not full */


        /*
         * buffer copy
         * in zfwBufFree will return a ndismsendcomplete
         * to resolve the synchronize problem in aggregate
         */

        u8_t    sendComplete = 0;

        tid_tx->aggvtxq[tid_tx->aggHead].buf = buf;
        time = zm_agg_GetTime();
        tid_tx->aggvtxq[tid_tx->aggHead].arrivalTime = time;
        tid_tx->aggvtxq[tid_tx->aggHead].baw_retransmit = 0;

        tid_tx->aggHead = ((tid_tx->aggHead + 1) & ZM_AGGQ_SIZE_MASK);
        tid_tx->lastArrival = time;
        tid_tx->size++;
        tid_tx->size = zm_agg_qlen(dev, tid_tx->aggHead, tid_tx->aggTail);
        if (buf && (tid_tx->size < (ZM_AGGQ_SIZE - 10))) {
            tid_tx->complete = tid_tx->aggHead;
            sendComplete = 1;
        }
        zmw_leave_critical_section(dev);

        if (!DESTQ.exist(dev, 0, tid_tx->ac, tid_tx, NULL)) {
            DESTQ.insert(dev, 0, tid_tx->ac, tid_tx, NULL);
        }

        zm_msg1_agg(ZM_LV_0, "tid_tx->size=", tid_tx->size);
        //zm_debug_msg1("tid_tx->size=", tid_tx->size);

        if (buf && sendComplete && wd->zfcbSendCompleteIndication) {
            //zmw_leave_critical_section(dev);
            wd->zfcbSendCompleteIndication(dev, buf);
        }

        /*if (tid_tx->size >= 16 && zfHpGetFreeTxdCount(dev) > 20)
            zfAggTxSend(dev, zfHpGetFreeTxdCount(dev), tid_tx);
        */
        return ZM_SUCCESS;
    }
    else
    {
        zm_msg1_agg(ZM_LV_0, "can't enqueue, tid_tx->size=", tid_tx->size);
        /*
         * Queue Full
         */

        /*
         * zm_msg1_agg(ZM_LV_0, "Queue full, qnum = ", qnum);
         * wd->commTally.txQosDropCount[ac]++;
         * zfwBufFree(dev, buf, ZM_SUCCESS);
         * zm_msg1_agg(ZM_LV_1, "Packet discarded, VTXQ full, ac=", ac);
         *
         * return ZM_ERR_EXCEED_PRIORITY_THRESHOLD;
         */
    }

    zmw_leave_critical_section(dev);

    if (!DESTQ.exist(dev, 0, tid_tx->ac, tid_tx, NULL)) {
            DESTQ.insert(dev, 0, tid_tx->ac, tid_tx, NULL);
    }

    return ZM_ERR_EXCEED_PRIORITY_THRESHOLD;
}

u16_t    zfAggDestExist(zdev_t* dev, u16_t Qtype, u16_t ac, TID_TX tid_tx, void* vtxq) {
    struct dest* dest;
    u16_t   exist = 0;
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    if (!DESTQ.Head[ac]) {
        exist = 0;
    }
    else {
        dest = DESTQ.Head[ac];
        if (dest->tid_tx == tid_tx) {
            exist = 1;
        }
        else {
            while (dest->next != DESTQ.Head[ac]) {
                dest = dest->next;
                if (dest->tid_tx == tid_tx){
                    exist = 1;
                    break;
                }
            }
        }
    }

    zmw_leave_critical_section(dev);

    return exist;
}

void    zfAggDestInsert(zdev_t* dev, u16_t Qtype, u16_t ac, TID_TX tid_tx, void* vtxq)
{
    struct dest* new_dest;
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    new_dest = zfwMemAllocate(dev, sizeof(struct dest));
    if(!new_dest)
    {
        return;
    }
    new_dest->Qtype = Qtype;
    new_dest->tid_tx = tid_tx;
    if (0 == Qtype)
        new_dest->tid_tx = tid_tx;
    else
        new_dest->vtxq = vtxq;
    if (!DESTQ.Head[ac]) {

        zmw_enter_critical_section(dev);
        new_dest->next = new_dest;
        DESTQ.Head[ac] = DESTQ.dest[ac] = new_dest;
        zmw_leave_critical_section(dev);
    }
    else {

        zmw_enter_critical_section(dev);
        new_dest->next = DESTQ.dest[ac]->next;
        DESTQ.dest[ac]->next = new_dest;
        zmw_leave_critical_section(dev);
    }


    //DESTQ.size[ac]++;
    return;
}

void    zfAggDestDelete(zdev_t* dev, u16_t Qtype, TID_TX tid_tx, void* vtxq)
{
    struct dest* dest, *temp;
    u16_t   i;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    if (wd->destLock) {
        zmw_leave_critical_section(dev);
        return;
    }


    //zmw_declare_for_critical_section();
    for (i=0; i<4; i++) {
        if (!DESTQ.Head[i]) continue;
        dest = DESTQ.Head[i];
        if (!dest) continue;


        while (dest && (dest->next != DESTQ.Head[i])) {
            if (Qtype == 0 && dest->next->tid_tx == tid_tx){
                break;
            }
            if (Qtype == 1 && dest->next->vtxq == vtxq) {
                break;
            }
            dest = dest->next;
        }

        if ((Qtype == 0 && dest->next->tid_tx == tid_tx) || (Qtype == 1 && dest->next->vtxq == vtxq)) {

            tid_tx->size = zm_agg_qlen(dev, tid_tx->aggHead, tid_tx->aggTail);
            if (tid_tx->size) {
                zmw_leave_critical_section(dev);
                return;
            }
            if (!DESTQ.Head[i]) {
                temp = NULL;
            }
            else {
                temp = dest->next;
                if (temp == dest) {
                    DESTQ.Head[i] = DESTQ.dest[i] = NULL;
                    //DESTQ.size[i] = 0;
                }
                else {
                    dest->next = dest->next->next;
                }
            }

            if (temp == NULL)
                {/* do nothing */} //zfwMemFree(dev, temp, sizeof(struct dest));
            else
                zfwMemFree(dev, temp, sizeof(struct dest));

            /*zmw_enter_critical_section(dev);
            if (DESTQ.size[i] > 0)
                DESTQ.size[i]--;
            zmw_leave_critical_section(dev);
            */
        }

    }
    zmw_leave_critical_section(dev);
    return;
}

void    zfAggDestInit(zdev_t* dev)
{
    u16_t i;
    zmw_get_wlan_dev(dev);

    //zmw_declare_for_critical_section();

    for (i=0; i<4; i++) {
        //wd->destQ.Head[i].next = wd->destQ.Head[i];
        //wd->destQ.dest[i] = wd->destQ.Head[i];
        //DESTQ.size[i] = 0;
        DESTQ.Head[i] = NULL;
    }
    DESTQ.insert  = zfAggDestInsert;
    DESTQ.delete  = zfAggDestDelete;
    DESTQ.init    = zfAggDestInit;
    DESTQ.getNext = zfAggDestGetNext;
    DESTQ.exist   = zfAggDestExist;
    DESTQ.ppri = 0;
    return;
}

struct dest* zfAggDestGetNext(zdev_t* dev, u16_t ac)
{
    struct dest *dest = NULL;
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    if (DESTQ.dest[ac]) {
        dest = DESTQ.dest[ac];
        DESTQ.dest[ac] = DESTQ.dest[ac]->next;
    }
    else {
        dest = NULL;
    }
    zmw_leave_critical_section(dev);

    return dest;
}

#ifdef ZM_ENABLE_AGGREGATION
#ifndef ZM_ENABLE_FW_BA_RETRANSMISSION //disable BAW
u16_t   zfAggTidTxInsertHead(zdev_t* dev, struct bufInfo *buf_info,TID_TX tid_tx)
{
    zbuf_t* buf;
    u32_t time;
    struct baw_header *baw_header;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();


    buf = buf_info->buf;

    zmw_enter_critical_section(dev);
    tid_tx->size = zm_agg_qlen(dev, tid_tx->aggHead, tid_tx->aggTail);
    zmw_leave_critical_section(dev);

    if (tid_tx->size >= (ZM_AGGQ_SIZE - 2)) {
        zfwBufFree(dev, buf, ZM_SUCCESS);
        return 0;
    }

    zmw_enter_critical_section(dev);
    tid_tx->aggTail = (tid_tx->aggTail == 0)? ZM_AGGQ_SIZE_MASK: tid_tx->aggTail - 1;
    tid_tx->aggvtxq[tid_tx->aggTail].buf = buf;
    //time = zm_agg_GetTime();
    tid_tx->aggvtxq[tid_tx->aggTail].arrivalTime = buf_info->timestamp;
    tid_tx->aggvtxq[tid_tx->aggTail].baw_retransmit = buf_info->baw_retransmit;

    baw_header = &tid_tx->aggvtxq[tid_tx->aggTail].baw_header;
    baw_header->headerLen   = buf_info->baw_header->headerLen;
    baw_header->micLen      = buf_info->baw_header->micLen;
    baw_header->snapLen     = buf_info->baw_header->snapLen;
    baw_header->removeLen   = buf_info->baw_header->removeLen;
    baw_header->keyIdx      = buf_info->baw_header->keyIdx;
    zfwMemoryCopy((u8_t *)baw_header->header, (u8_t *)buf_info->baw_header->header, 58);
    zfwMemoryCopy((u8_t *)baw_header->mic   , (u8_t *)buf_info->baw_header->mic   , 8);
    zfwMemoryCopy((u8_t *)baw_header->snap  , (u8_t *)buf_info->baw_header->snap  , 8);

    tid_tx->size++;
    tid_tx->size = zm_agg_qlen(dev, tid_tx->aggHead, tid_tx->aggTail);
    zmw_leave_critical_section(dev);

    //tid_tx->lastArrival = time;
    if (1 == tid_tx->size) {
        DESTQ.insert(dev, 0, tid_tx->ac, tid_tx, NULL);
    }


    zm_msg1_agg(ZM_LV_0, "0xC2:insertHead, tid_tx->size=", tid_tx->size);

    return TRUE;
}
#endif //disable BAW
#endif

void    zfiTxComplete(zdev_t* dev)
{

    zmw_get_wlan_dev(dev);

    //zmw_declare_for_critical_section();

    if( (wd->wlanMode == ZM_MODE_AP) ||
        (wd->wlanMode == ZM_MODE_INFRASTRUCTURE && wd->sta.EnableHT) ||
        (wd->wlanMode == ZM_MODE_PSEUDO) ) {
        zfAggTxScheduler(dev, 0);
    }

    return;
}

TID_TX  zfAggTxReady(zdev_t* dev) {
    //struct dest* dest;
    u16_t   i;
    TID_TX  tid_tx = NULL;
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    for (i=0; i<ZM_AGG_POOL_SIZE; i++)
    {
        if (wd->aggQPool[i]->aggQEnabled)
        {
            if (wd->aggQPool[i]->size >= 16) {
                tid_tx = wd->aggQPool[i];
                break;
            }
        }
        else {
        }
    }
    zmw_leave_critical_section(dev);
    return tid_tx;
}

u16_t   zfAggValidTidTx(zdev_t* dev, TID_TX tid_tx) {
    u16_t   i, valid = 0;
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    for (i=0; i<ZM_AGG_POOL_SIZE; i++)
    {
        if (wd->aggQPool[i] == tid_tx)
        {
            valid = 1;
            break;
        }
        else {
        }
    }
    zmw_leave_critical_section(dev);

    return valid;
}

void    zfAggTxScheduler(zdev_t* dev, u8_t ScanAndClear)
{
    TID_TX  tid_tx = NULL;
    void*   vtxq;
    struct dest* dest;
    zbuf_t*  buf;
    u32_t txql, min_txql;
    //u16_t aggr_size = 1;
    u16_t txq_threshold;
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    if (!wd->aggInitiated)
    {
        return;
    }

    /* debug */
    txql = TXQL;
    min_txql = AGG_MIN_TXQL;

    if(wd->txq_threshold)
        txq_threshold = wd->txq_threshold;
    else
        txq_threshold = AGG_MIN_TXQL;

    tid_tx = zfAggTxReady(dev);
    if (tid_tx) ScanAndClear = 0;
    while (zfHpGetFreeTxdCount(dev) > 20 && (TXQL < txq_threshold || tid_tx)) {
    //while (zfHpGetFreeTxdCount(dev) > 20 && (ScanAndClear || tid_tx)) {
    //while (TXQL < txq_threshold) {
        u16_t i;
        u8_t ac;
        s8_t destQ_count = 0;
    //while ((zfHpGetFreeTxdCount(dev)) > 32) {

        //DbgPrint("zfAggTxScheduler: in while loop");
        for (i=0; i<4; i++) {
            if (DESTQ.Head[i]) destQ_count++;
        }
        if (0 >= destQ_count) break;

        zmw_enter_critical_section(dev);
        ac = pri[DESTQ.ppri]; DESTQ.ppri = (DESTQ.ppri + 1) % 10;
        zmw_leave_critical_section(dev);

        for (i=0; i<10; i++){
            if(DESTQ.Head[ac]) break;

            zmw_enter_critical_section(dev);
            ac = pri[DESTQ.ppri]; DESTQ.ppri = (DESTQ.ppri + 1) % 10;
            zmw_leave_critical_section(dev);
        }
        if (i == 10) break;
        //DbgPrint("zfAggTxScheduler: have dest Q");
        zmw_enter_critical_section(dev);
        wd->destLock = 1;
        zmw_leave_critical_section(dev);

        dest = DESTQ.getNext(dev, ac);
        if (!dest) {
            zmw_enter_critical_section(dev);
            wd->destLock = 0;
            zmw_leave_critical_section(dev);

            DbgPrint("bug report! DESTQ.getNext got nothing!");
            break;
        }
        if (dest->Qtype == 0) {
            tid_tx = dest->tid_tx;

            //DbgPrint("zfAggTxScheduler: have tid_tx Q");

            if(tid_tx && zfAggValidTidTx(dev, tid_tx))
                tid_tx->size = zm_agg_qlen(dev, tid_tx->aggHead, tid_tx->aggTail);
            else {
                zmw_enter_critical_section(dev);
                wd->destLock = 0;
                zmw_leave_critical_section(dev);

                tid_tx = zfAggTxReady(dev);
                continue;
            }

            zmw_enter_critical_section(dev);
            wd->destLock = 0;
            zmw_leave_critical_section(dev);
            //zmw_enter_critical_section(dev);
            if (tid_tx && !tid_tx->size) {

                //zmw_leave_critical_section(dev);
                //DESTQ.delete(dev, 0, tid_tx, NULL);
            }
            else if(wd->aggState == 0){
                //wd->aggState = 1;
                //zmw_leave_critical_section(dev);
                zfAggTxSend(dev, zfHpGetFreeTxdCount(dev), tid_tx);
                //wd->aggState = 0;
            }
            else {
                //zmw_leave_critical_section(dev);
                break;
            }
        }
        else {
            vtxq = dest->vtxq;
            buf = zfGetVtxq(dev, ac);
            zm_assert( buf != 0 );

            zfTxSendEth(dev, buf, 0, ZM_EXTERNAL_ALLOC_BUF, 0);

        }
        /*flush all but < 16 frames in tid_tx to TXQ*/
        tid_tx = zfAggTxReady(dev);
    }

    /*while ((zfHpGetFreeTxdCount(dev)) > 32) {
    //while ((zfHpGetFreeTxdCount(dev)) > 32) {

        destQ_count = 0;
        for (i=0; i<4; i++) destQ_count += wd->destQ.size[i];
        if (0 >= destQ_count) break;

        ac = pri[wd->destQ.ppri]; wd->destQ.ppri = (wd->destQ.ppri + 1) % 10;
        for (i=0; i<10; i++){
            if(wd->destQ.size[ac]!=0) break;
            ac = pri[wd->destQ.ppri]; wd->destQ.ppri = (wd->destQ.ppri + 1) % 10;
        }
        if (i == 10) break;
        dest = wd->destQ.getNext(dev, ac);
        if (dest->Qtype == 0) {
            tid_tx = dest->tid_tx;
            tid_tx->size = zm_agg_qlen(dev, tid_tx->aggHead, tid_tx->aggTail);
            if (!tid_tx->size) {
                wd->destQ.delete(dev, 0, tid_tx, NULL);
                break;
            }
            else if((wd->aggState == 0) && (tid_tx->size >= 16)){
                zfAggTxSend(dev, zfHpGetFreeTxdCount(dev), tid_tx);
            }
            else {
                break;
            }
        }

    }
    */
    return;
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfAggTx                     */
/*      return Status code ZM_SUCCESS or error code                     */
/*      management A-MPDU aggregation function,                         */
/*      management aggregation queue, calculate arrivalrate,            */
/*      add/delete an aggregation queue of a stream,                    */
/*      enqueue packets into responsible aggregate queue.               */
/*      take (dev, buf, ac) as input                                    */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : packet buff                                               */
/*      ac  : access category                                           */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      status code                                                     */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Honda               Atheros Communications, INC.    2006.12     */
/*                                                                      */
/************************************************************************/
u16_t zfAggTx(zdev_t* dev, zbuf_t* buf, u16_t tid)
{
    u16_t aid;
    //u16_t qnum;
    //u16_t aggflag = 0;
    //u16_t arrivalrate = 0;
    TID_TX tid_tx;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    if(!wd->aggInitiated)
    {
        return ZM_ERR_TX_BUFFER_UNAVAILABLE;
    }

    aid = zfAggGetSta(dev, buf);

    //arrivalrate = zfAggTxArrivalRate(dev, aid, tid);

    if (0xffff == aid)
    {
        /*
         * STA not associated, this is a BC/MC or STA->AP packet
         */

        return ZM_ERR_TX_BUFFER_UNAVAILABLE;
    }

    /*
     * STA associated, a unicast packet
     */

    tid_tx = zfAggTxGetQueue(dev, aid, tid);

    /*tid_q.tid_tx = tid_tx;
    wd->destQ.insert = zfAggDestInsert;
    wd->destQ.insert(dev, 0, tid_q);
    */
    if (tid_tx != NULL)
    {
        /*
         * this (aid, ac) is aggregated
         */

        //if (arrivalrate < ZM_AGG_LOW_THRESHOLD)
        if (0)
        {
            /*
             * arrival rate too low
             * delete this aggregate queue
             */

            zmw_enter_critical_section(dev);

            //wd->aggQPool[qnum]->clearFlag = wd->aggQPool[qnum]->deleteFlag =1;

            zmw_leave_critical_section(dev);

        }

        return zfAggTxEnqueue(dev, buf, aid, tid_tx);

    }
    else
    {
        /*
         * this (aid, ac) not yet aggregated
         * queue not found
         */

        //if (arrivalrate > ZM_AGG_HIGH_THRESHOLD)
        if (1)
        {
            /*
             * arrivalrate high enough to get a new agg queue
             */

            tid_tx = zfAggTxNewQueue(dev, aid, tid, buf);

            //zm_msg1_agg(ZM_LV_0, "get new AggQueue qnum = ", tid_tx->);

            if (tid_tx)
            {
                /*
                 * got a new aggregate queue
                 */

                //zmw_enter_critical_section(dev);

                //wd->aggSta[aid].aggFlag[ac] = 1;

                //zmw_leave_critical_section(dev);

                /*
                 * add ADDBA functions here
                 * return ZM_ERR_TX_BUFFER_UNAVAILABLE;
                 */


                //zfAggSendAddbaRequest(dev, tid_tx->dst, tid_tx->ac, tid_tx->tid);
                //zmw_enter_critical_section(dev);

                //wd->aggSta[aid].aggFlag[ac] = 0;

                //zmw_leave_critical_section(dev);

                return zfAggTxEnqueue(dev, buf, aid, tid_tx);

            }
            else
            {
                /*
                 * just can't get a new aggregate queue
                 */

                return ZM_ERR_TX_BUFFER_UNAVAILABLE;
            }
        }
        else
        {
            /*
             * arrival rate is not high enough to get a new agg queue
             */

            return ZM_ERR_TX_BUFFER_UNAVAILABLE;
        }
    }



}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfAggTxReadyCount           */
/*      return counter of ready to aggregate queues.                    */
/*      take (dev, ac) as input, only calculate the ready to aggregate  */
/*      queues of one particular ac.                                    */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      ac  : access category                                           */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      counter of ready to aggregate queues                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Honda               Atheros Communications, INC.    2006.12     */
/*                                                                      */
/************************************************************************/
u16_t zfAggTxReadyCount(zdev_t* dev, u16_t ac)
{
    u16_t i;
    u16_t readycount = 0;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    for (i=0 ; i<ZM_AGG_POOL_SIZE; i++)
    {
        if (wd->aggQPool[i]->aggQEnabled && (wd->aggQPool[i]->aggReady || \
                wd->aggQPool[i]->clearFlag) && ac == wd->aggQPool[i]->ac)
            readycount++;
    }

    zmw_leave_critical_section(dev);

    return readycount;
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfAggTxPartial              */
/*      return the number that Vtxq has to send.                        */
/*      take (dev, ac, readycount) as input, calculate the ratio of     */
/*      Vtxq length to (Vtxq length + readycount) of a particular ac,   */
/*      and returns the Vtxq length * the ratio                         */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      ac  : access category                                           */
/*      readycount: the number of ready to aggregate queues of this ac  */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      Vtxq length * ratio                                             */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Honda               Atheros Communications, INC.    2006.12     */
/*                                                                      */
/************************************************************************/
u16_t zfAggTxPartial(zdev_t* dev, u16_t ac, u16_t readycount)
{
    u16_t qlen;
    u16_t partial;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    qlen = zm_agg_qlen(dev, wd->vtxqHead[ac], wd->vtxqTail[ac]);

    if ((qlen + readycount) > 0)
    {
        partial = (u16_t)( zm_agg_weight(ac) * ((u16_t)qlen/(qlen + \
                        readycount)) );
    }
    else
    {
        partial = 0;
    }

    zmw_leave_critical_section(dev);

    if (partial > qlen)
        partial = qlen;

    return partial;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfAggTxSend                 */
/*      return sentcount                                                */
/*      take (dev, ac, n) as input, n is the number of scheduled agg    */
/*      queues to be sent of the particular ac.                         */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      ac  : access category                                           */
/*      n   : the number of scheduled aggregation queues to be sent     */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      sentcount                                                       */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Honda               Atheros Communications, INC.    2006.12     */
/*                                                                      */
/************************************************************************/
u16_t zfAggTxSend(zdev_t* dev, u32_t freeTxd, TID_TX tid_tx)
{
    //u16_t   qnum;
    //u16_t   qlen;
    u16_t   j;
    //u16_t   sentcount = 0;
    zbuf_t* buf;
    struct  aggControl aggControl;
    u16_t   aggLen;
    //zbuf_t*  newBuf;
    //u16_t   bufLen;
    //TID_BAW tid_baw = NULL;
    //struct bufInfo *buf_info;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    //while (tid_tx->size > 0)

    zmw_enter_critical_section(dev);
    tid_tx->size = zm_agg_qlen(dev, tid_tx->aggHead, tid_tx->aggTail);
    aggLen = zm_agg_min(16, zm_agg_min(tid_tx->size, (u16_t)(freeTxd - 2)));
    zmw_leave_critical_section(dev);

            /*
             * why there have to be 2 free Txd?
             */
    if (aggLen <=0 )
        return 0;


    if (aggLen == 1) {
        buf = zfAggTxGetVtxq(dev, tid_tx);
        if (buf)
            zfTxSendEth(dev, buf, 0, ZM_EXTERNAL_ALLOC_BUF, 0);
        if (tid_tx->size == 0) {
            //DESTQ.delete(dev, 0, tid_tx, NULL);
        }

        return 1;
    }
                /*
                 * Free Txd queue is big enough to put aggregation
                 */
    zmw_enter_critical_section(dev);
    if (wd->aggState == 1) {
        zmw_leave_critical_section(dev);
        return 0;
    }
    wd->aggState = 1;
    zmw_leave_critical_section(dev);


    zm_msg1_agg(ZM_LV_0, "aggLen=", aggLen);
    tid_tx->aggFrameSize = 0;
    for (j=0; j < aggLen; j++) {
        buf = zfAggTxGetVtxq(dev, tid_tx);

        zmw_enter_critical_section(dev);
        tid_tx->size = zm_agg_qlen(dev, tid_tx->aggHead, tid_tx->aggTail);
        zmw_leave_critical_section(dev);

        if ( buf ) {
            //struct aggTally *agg_tal;
            u16_t completeIndex;

            if (0 == j) {
                aggControl.ampduIndication = ZM_AGG_FIRST_MPDU;

            }
            else if ((j == (aggLen - 1)) || tid_tx->size == 0)
            {
                aggControl.ampduIndication = ZM_AGG_LAST_MPDU;
                //wd->aggState = 0;

            }
            else
            {
                aggControl.ampduIndication = ZM_AGG_MIDDLE_MPDU;
                /* the packet is delayed more than 500 ms, drop it */

            }
            tid_tx->aggFrameSize += zfwBufGetSize(dev, buf);
            aggControl.addbaIndication = 0;
            aggControl.aggEnabled = 1;

#ifdef ZM_AGG_TALLY
            agg_tal = &wd->agg_tal;
            agg_tal->sent_packets_sum++;

#endif

            zfAggTxSendEth(dev, buf, 0, ZM_EXTERNAL_ALLOC_BUF, 0, &aggControl, tid_tx);

            zmw_enter_critical_section(dev);
            completeIndex = tid_tx->complete;
            if(zm_agg_inQ(tid_tx, tid_tx->complete))
                zm_agg_plus(tid_tx->complete);
            zmw_leave_critical_section(dev);

            if(zm_agg_inQ(tid_tx, completeIndex) && wd->zfcbSendCompleteIndication
                    && tid_tx->aggvtxq[completeIndex].buf) {
                wd->zfcbSendCompleteIndication(dev, tid_tx->aggvtxq[completeIndex].buf);
                zm_debug_msg0("in queue complete worked!");
            }

        }
        else {
            /*
             * this aggregation queue is empty
             */
            zm_msg1_agg(ZM_LV_0, "aggLen not reached, but no more frame, j=", j);

            break;
        }
    }
    zmw_enter_critical_section(dev);
    wd->aggState = 0;
    zmw_leave_critical_section(dev);

    //zm_acquire_agg_spin_lock(Adapter);
    tid_tx->size = zm_agg_qlen(dev, tid_tx->aggHead, tid_tx->aggTail);
    //zm_release_agg_spin_lock(Adapter);

    if (tid_tx->size == 0) {
        //DESTQ.delete(dev, 0, tid_tx, NULL);
    }



    //zfAggInvokeBar(dev, tid_tx);
    if(j>0) {
        aggr_count++;
        zm_msg1_agg(ZM_LV_0, "0xC2:sent 1 aggr, aggr_count=", aggr_count);
        zm_msg1_agg(ZM_LV_0, "0xC2:sent 1 aggr, aggr_size=", j);
    }
    return j;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfAggTxGetReadyQueue        */
/*      return the number of the aggregation queue                      */
/*      take (dev, ac) as input, find the agg queue with smallest       */
/*      arrival time (waited longest) among those ready or clearFlag    */
/*      set queues.                                                     */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      ac  : access category                                           */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      aggregation queue number                                        */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Honda               Atheros Communications, INC.    2006.12     */
/*                                                                      */
/************************************************************************/
TID_TX zfAggTxGetReadyQueue(zdev_t* dev, u16_t ac)
{
    //u16_t       qnum = ZM_AGG_POOL_SIZE;
    u16_t       i;
    u32_t       time = 0;
    TID_TX      tid_tx = NULL;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    for (i=0 ;i<ZM_AGG_POOL_SIZE; i++)
    {
        if (1 == wd->aggQPool[i]->aggQEnabled && ac == wd->aggQPool[i]->ac &&
                (wd->aggQPool[i]->size > 0))
        {
            if (0 == time || time > wd->aggQPool[i]->aggvtxq[ \
                            wd->aggQPool[i]->aggHead ].arrivalTime)
            {
                tid_tx = wd->aggQPool[i];
                time = tid_tx->aggvtxq[ tid_tx->aggHead ].arrivalTime;
            }
        }
    }

    zmw_leave_critical_section(dev);

    return tid_tx;
}



/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfAggTxGetVtxq              */
/*      return an MSDU                                                  */
/*      take (dev, qnum) as input, return an MSDU out of the agg queue. */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      qnum: queue number                                              */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      a MSDU                                                          */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Honda               Atheros Communications, INC.    2006.12     */
/*                                                                      */
/************************************************************************/
zbuf_t* zfAggTxGetVtxq(zdev_t* dev, TID_TX tid_tx)
{
    zbuf_t* buf = NULL;

    zmw_declare_for_critical_section();

    if (tid_tx->aggHead != tid_tx->aggTail)
    {
        buf = tid_tx->aggvtxq[ tid_tx->aggTail ].buf;

        tid_tx->aggvtxq[tid_tx->aggTail].buf = NULL;

        zmw_enter_critical_section(dev);
        tid_tx->aggTail = ((tid_tx->aggTail + 1) & ZM_AGGQ_SIZE_MASK);
        if(tid_tx->size > 0) tid_tx->size--;
        tid_tx->size = zm_agg_qlen(dev, tid_tx->aggHead, tid_tx->aggTail);
        if (NULL == buf) {
            //tid_tx->aggTail = tid_tx->aggHead = tid_tx->size = 0;
            //zm_msg1_agg(ZM_LV_0, "GetVtxq buf == NULL, tid_tx->size=", tid_tx->size);
        }
        zmw_leave_critical_section(dev);
    }
    else
    {
        /*
         * queue is empty
         */
        zm_msg1_agg(ZM_LV_0, "tid_tx->aggHead == tid_tx->aggTail, tid_tx->size=", tid_tx->size);

    }

    if (zm_agg_qlen(dev, tid_tx->aggHead, tid_tx->aggTail) != tid_tx->size)
        zm_msg1_agg(ZM_LV_0, "qlen!=tid_tx->size! tid_tx->size=", tid_tx->size);
    return buf;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfAggTxDeleteQueue          */
/*      return ZM_SUCCESS (can't fail)                                  */
/*      take (dev, qnum) as input, reset (delete) this aggregate queue, */
/*      this queue is virtually returned to the aggregate queue pool.   */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      qnum: queue number                                              */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      ZM_SUCCESS                                                      */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Honda               Atheros Communications, INC.    2006.12     */
/*                                                                      */
/************************************************************************/
u16_t zfAggTxDeleteQueue(zdev_t* dev, u16_t qnum)
{
    u16_t ac, tid;
    struct aggQueue *tx_tid;
    struct aggSta   *agg_sta;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    tx_tid = wd->aggQPool[qnum];
    agg_sta = &wd->aggSta[tx_tid->aggQSTA];
    ac = tx_tid->ac;
    tid = tx_tid->tid;

    zmw_enter_critical_section(dev);

    tx_tid->aggQEnabled = 0;
    tx_tid->aggHead = tx_tid->aggTail = 0;
    tx_tid->aggReady = 0;
    tx_tid->clearFlag = tx_tid->deleteFlag = 0;
    tx_tid->size = 0;
    agg_sta->count[ac] = 0;

    agg_sta->tid_tx[tid] = NULL;
    agg_sta->aggFlag[ac] = 0;

    zmw_leave_critical_section(dev);

    zm_msg1_agg(ZM_LV_0, "queue deleted! qnum=", qnum);

    return ZM_SUCCESS;
}

#ifdef ZM_ENABLE_AGGREGATION
#ifndef ZM_ENABLE_FW_BA_RETRANSMISSION //disable BAW
void zfBawCore(zdev_t* dev, u16_t baw_seq, u32_t bitmap, u16_t aggLen) {
    TID_BAW tid_baw;
    s16_t i;
    zbuf_t* buf;
    struct bufInfo *buf_info;

    zmw_get_wlan_dev(dev);
    //zmw_declare_for_critical_section();
    tid_baw = BAW->getQ(dev, baw_seq);
    //tid_baw = NULL;
    if (NULL == tid_baw)
        return;

    total_mpdu += aggLen;
    for (i = aggLen - 1; i>=0; i--) {
        if (((bitmap >> i) & 0x1) == 0) {
            buf_info = BAW->pop(dev, i, tid_baw);
            buf = buf_info->buf;
            if (buf) {
                //wd->zfcbSetBawQ(dev, buf, 0);
                zfAggTidTxInsertHead(dev, buf_info, tid_baw->tid_tx);
            }
        }
        else {
            success_mpdu++;
        }
    }
    BAW->disable(dev, tid_baw);
    zfAggTxScheduler(dev);
    zm_debug_msg1("success_mpdu = ", success_mpdu);
    zm_debug_msg1("  total_mpdu = ", total_mpdu);
}

void    zfBawInit(zdev_t* dev) {
    TID_BAW tid_baw;
    u16_t i,j;
    zmw_get_wlan_dev(dev);
    //zmw_declare_for_critical_section();

    for (i=0; i<ZM_BAW_POOL_SIZE; i++){
        tid_baw = &BAW->tid_baw[i];
        for (j=0; j<ZM_VTXQ_SIZE; j++) {
            tid_baw->frame[j].buf = NULL;
        }
        tid_baw->enabled = tid_baw->head = tid_baw->tail = tid_baw->size = 0;
        tid_baw->start_seq = 0;
    }
    BAW->delPoint = 0;
    BAW->core = zfBawCore;
    BAW->getNewQ = zfBawGetNewQ;
    BAW->insert = zfBawInsert;
    BAW->pop = zfBawPop;
    BAW->enable = zfBawEnable;
    BAW->disable = zfBawDisable;
    BAW->getQ = zfBawGetQ;
}



TID_BAW zfBawGetNewQ(zdev_t* dev, u16_t start_seq, TID_TX tid_tx) {
    TID_BAW tid_baw=NULL;
    TID_BAW next_baw=NULL;
    u16_t i;
    zmw_get_wlan_dev(dev);
    //zmw_declare_for_critical_section();

    /*
    for (i=0; i<ZM_BAW_POOL_SIZE; i++){
        tid_baw = &BAW->tid_baw[i];
        if (FALSE == tid_baw->enabled)
            break;
    }
    */

    tid_baw = &BAW->tid_baw[BAW->delPoint];
    i = BAW->delPoint;
    //if (ZM_BAW_POOL_SIZE == i) {
        //return NULL;
    //    u8_t temp = BAW->delPoint;
    //    tid_baw = &BAW->tid_baw[BAW->delPoint];
    //    BAW->disable(dev, tid_baw);
    //    BAW->delPoint = (BAW->delPoint < (ZM_BAW_POOL_SIZE - 1))? (BAW->delPoint + 1): 0;
    //    temp = BAW->delPoint;
    //}

    zm_msg1_agg(ZM_LV_0, "get new tid_baw, index=", i);
    BAW->delPoint = (i < (ZM_BAW_POOL_SIZE -1))? (i + 1): 0;
    next_baw = &BAW->tid_baw[BAW->delPoint];
    if (1 == next_baw->enabled) BAW->disable(dev, next_baw);

    BAW->enable(dev, tid_baw, start_seq);
    tid_baw->tid_tx = tid_tx;

    return tid_baw;
}

u16_t   zfBawInsert(zdev_t* dev, zbuf_t* buf, u16_t baw_seq, TID_BAW tid_baw, u8_t baw_retransmit, struct baw_header_r *header_r) {
    //TID_BAW tid_baw;
    //u16_t   bufLen;

    //zmw_get_wlan_dev(dev);
    //zmw_declare_for_critical_section();

    if(tid_baw->size < (ZM_VTXQ_SIZE - 1)) {
        struct baw_header *baw_header = &tid_baw->frame[tid_baw->head].baw_header;

        baw_header->headerLen   = header_r->headerLen;
        baw_header->micLen      = header_r->micLen;
        baw_header->snapLen     = header_r->snapLen;
        baw_header->removeLen   = header_r->removeLen;
        baw_header->keyIdx      = header_r->keyIdx;
        zfwMemoryCopy((u8_t *)baw_header->header, (u8_t *)header_r->header, 58);
        zfwMemoryCopy((u8_t *)baw_header->mic   , (u8_t *)header_r->mic   , 8);
        zfwMemoryCopy((u8_t *)baw_header->snap  , (u8_t *)header_r->snap  , 8);
        //wd->zfcbSetBawQ(dev, buf, 1);
        tid_baw->frame[tid_baw->head].buf = buf;
        tid_baw->frame[tid_baw->head].baw_seq = baw_seq;
        tid_baw->frame[tid_baw->head].baw_retransmit = baw_retransmit + 1;

        //tid_baw->frame[tid_baw->head].data = pBuf->data;
        tid_baw->head++;
        tid_baw->size++;
    }
    else {
        //wd->zfcbSetBawQ(dev, buf, 0);
        zfwBufFree(dev, buf, ZM_SUCCESS);
        return FALSE;
    }
    return TRUE;
}

struct bufInfo* zfBawPop(zdev_t* dev, u16_t index, TID_BAW tid_baw) {
    //TID_BAW tid_baw;
    //zbuf_t* buf;
    struct bufInfo *buf_info;
    zmw_get_wlan_dev(dev);

    buf_info = &wd->buf_info;
    buf_info->baw_header = NULL;

    if (NULL == (buf_info->buf = tid_baw->frame[index].buf))
        return buf_info;

    buf_info->baw_retransmit = tid_baw->frame[index].baw_retransmit;
    buf_info->baw_header = &tid_baw->frame[index].baw_header;
    buf_info->timestamp = tid_baw->frame[index].timestamp;
    //pBuf->data = pBuf->buffer;
    //wd->zfcbRestoreBufData(dev, buf);
    tid_baw->frame[index].buf = NULL;

    return buf_info;
}

void    zfBawEnable(zdev_t* dev, TID_BAW tid_baw, u16_t start_seq) {
    //TID_BAW tid_baw;

    //zmw_get_wlan_dev(dev);
    //zmw_declare_for_critical_section();

    tid_baw->enabled = TRUE;
    tid_baw->head = tid_baw->tail = tid_baw->size = 0;
    tid_baw->start_seq = start_seq;
}

void    zfBawDisable(zdev_t* dev, TID_BAW tid_baw) {
    //TID_BAW tid_baw;
    u16_t i;

    //zmw_get_wlan_dev(dev);
    //zmw_declare_for_critical_section();
    for (i=0; i<ZM_VTXQ_SIZE; i++) {
        if (tid_baw->frame[i].buf) {

            //wd->zfcbSetBawQ(dev, tid_baw->frame[i].buf, 0);
            zfwBufFree(dev, tid_baw->frame[i].buf, ZM_SUCCESS);
            tid_baw->frame[i].buf = NULL;
        }
    }

    tid_baw->enabled = FALSE;
}

TID_BAW zfBawGetQ(zdev_t* dev, u16_t baw_seq) {
    TID_BAW tid_baw=NULL;
    u16_t i;

    zmw_get_wlan_dev(dev);
    //zmw_declare_for_critical_section();
    for (i=0; i<ZM_BAW_POOL_SIZE; i++){
        tid_baw = &BAW->tid_baw[i];
        if (TRUE == tid_baw->enabled)
        {
            zm_msg1_agg(ZM_LV_0, "get an old tid_baw, baw_seq=", baw_seq);
            zm_msg1_agg(ZM_LV_0, "check a  tid_baw->start_seq=", tid_baw->start_seq);
            if(baw_seq == tid_baw->start_seq)
                break;
        }

    }
    if (ZM_BAW_POOL_SIZE == i)
        return NULL;
    return tid_baw;
}
#endif //disable BAW
#endif

u16_t zfAggTallyReset(zdev_t* dev)
{
    struct aggTally* agg_tal;

    zmw_get_wlan_dev(dev);

    //zmw_declare_for_critical_section();

    agg_tal = &wd->agg_tal;
    agg_tal->got_packets_sum = 0;
    agg_tal->got_bytes_sum = 0;
    agg_tal->sent_bytes_sum = 0;
    agg_tal->sent_packets_sum = 0;
    agg_tal->avg_got_packets = 0;
    agg_tal->avg_got_bytes = 0;
    agg_tal->avg_sent_packets = 0;
    agg_tal->avg_sent_bytes = 0;
    agg_tal->time = 0;
    return 0;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfAggScanAndClear           */
/*      If the packets in a queue have waited for too long, clear and   */
/*      delete this aggregation queue.                                  */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev     : device pointer                                        */
/*      time    : current time                                          */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      ZM_SUCCESS                                                      */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Honda               Atheros Communications, INC.    2006.12     */
/*                                                                      */
/************************************************************************/
u16_t   zfAggScanAndClear(zdev_t* dev, u32_t time)
{
    u16_t i;
    u16_t head;
    u16_t tail;
    u32_t tick;
    u32_t arrivalTime;
    //u16_t aid, ac;
    TID_TX tid_tx;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    if(!(wd->state == ZM_WLAN_STATE_ENABLED)) return 0;
    zfAggTxScheduler(dev, 1);
    tick = zm_agg_GetTime();
    for (i=0; i<ZM_AGG_POOL_SIZE; i++)
    {
        if (!wd->aggQPool[i]) return 0;
        if (1 == wd->aggQPool[i]->aggQEnabled)
        {
            tid_tx = wd->aggQPool[i];
            zmw_enter_critical_section(dev);

            head = tid_tx->aggHead;
            tail = tid_tx->aggTail;

            arrivalTime = (u32_t)tid_tx->aggvtxq[tid_tx->aggTail].arrivalTime;


            if((tick - arrivalTime) <= ZM_AGG_CLEAR_TIME)
            {

            }
            else if((tid_tx->size = zm_agg_qlen(dev, tid_tx->aggHead, tid_tx->aggTail)) > 0)
            {

                tid_tx->clearFlag = 1;

                //zm_msg1_agg(ZM_LV_0, "clear queue    tick =", tick);
                //zm_msg1_agg(ZM_LV_0, "clear queue arrival =", arrivalTime);


                //zmw_leave_critical_section(dev);
                //zfAggTxScheduler(dev);
                //zmw_enter_critical_section(dev);

            }

            if (tid_tx->size == 0)
            {
                /*
                 * queue empty
                 */
                if (tick - tid_tx->lastArrival > ZM_AGG_DELETE_TIME)
                {
                    zm_msg1_agg(ZM_LV_0, "delete queue, idle for n sec. n = ", \
                            ZM_AGG_DELETE_TIME/10);

                    zmw_leave_critical_section(dev);
                    zfAggTxDeleteQueue(dev, i);
                    zmw_enter_critical_section(dev);
                }
            }

            zmw_leave_critical_section(dev);
        }
    }

        zfAggRxClear(dev, time);

#ifdef ZM_AGG_TALLY
    if((wd->tick % 100) == 0) {
        zfAggPrintTally(dev);
    }
#endif

    return ZM_SUCCESS;
}

u16_t   zfAggPrintTally(zdev_t* dev)
{
    struct aggTally* agg_tal;

    zmw_get_wlan_dev(dev);

    //zmw_declare_for_critical_section();

    agg_tal = &wd->agg_tal;

    if(agg_tal->got_packets_sum < 10)
    {
        zfAggTallyReset(dev);
        return 0;
    }

    agg_tal->time++;
    agg_tal->avg_got_packets = (agg_tal->avg_got_packets * (agg_tal->time - 1) +
            agg_tal->got_packets_sum) / agg_tal->time;
    agg_tal->avg_got_bytes = (agg_tal->avg_got_bytes * (agg_tal->time - 1) +
            agg_tal->got_bytes_sum) / agg_tal->time;
    agg_tal->avg_sent_packets = (agg_tal->avg_sent_packets * (agg_tal->time - 1)
            + agg_tal->sent_packets_sum) / agg_tal->time;
    agg_tal->avg_sent_bytes = (agg_tal->avg_sent_bytes * (agg_tal->time - 1) +
            agg_tal->sent_bytes_sum) / agg_tal->time;
    zm_msg1_agg(ZM_LV_0, "got_packets_sum =", agg_tal->got_packets_sum);
    zm_msg1_agg(ZM_LV_0, "  got_bytes_sum =", agg_tal->got_bytes_sum);
    zm_msg1_agg(ZM_LV_0, "sent_packets_sum=", agg_tal->sent_packets_sum);
    zm_msg1_agg(ZM_LV_0, " sent_bytes_sum =", agg_tal->sent_bytes_sum);
    agg_tal->got_packets_sum = agg_tal->got_bytes_sum =agg_tal->sent_packets_sum
                = agg_tal->sent_bytes_sum = 0;
    zm_msg1_agg(ZM_LV_0, "avg_got_packets =", agg_tal->avg_got_packets);
    zm_msg1_agg(ZM_LV_0, "  avg_got_bytes =", agg_tal->avg_got_bytes);
    zm_msg1_agg(ZM_LV_0, "avg_sent_packets=", agg_tal->avg_sent_packets);
    zm_msg1_agg(ZM_LV_0, " avg_sent_bytes =", agg_tal->avg_sent_bytes);
    if ((wd->commTally.BA_Fail == 0) || (wd->commTally.Hw_Tx_MPDU == 0))
    {
        zm_msg1_agg(ZM_LV_0, "Hardware Tx MPDU=", wd->commTally.Hw_Tx_MPDU);
        zm_msg1_agg(ZM_LV_0, "  BA Fail number=", wd->commTally.BA_Fail);
    }
    else
        zm_msg1_agg(ZM_LV_0, "1/(BA fail rate)=", wd->commTally.Hw_Tx_MPDU/wd->commTally.BA_Fail);

    return 0;
}

u16_t zfAggRxClear(zdev_t* dev, u32_t time)
{
    u16_t   i;
    struct agg_tid_rx *tid_rx;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    for (i=0; i<ZM_AGG_POOL_SIZE; i++)
    {
        zmw_enter_critical_section(dev);
        tid_rx = wd->tid_rx[i];
        if (tid_rx->baw_head != tid_rx->baw_tail)
        {
            u16_t j = tid_rx->baw_tail;
            while ((j != tid_rx->baw_head) && !tid_rx->frame[j].buf) {
            	j = (j + 1) & ZM_AGG_BAW_MASK;
            }
            if ((j != tid_rx->baw_head) && (time - tid_rx->frame[j].arrivalTime) >
                    (ZM_AGG_CLEAR_TIME - 5))
            {
                zmw_leave_critical_section(dev);
                zm_msg0_agg(ZM_LV_1, "queue RxFlush by RxClear");
                zfAggRxFlush(dev, 0, tid_rx);
                zmw_enter_critical_section(dev);
            }
        }
        zmw_leave_critical_section(dev);
    }

    return ZM_SUCCESS;
}

struct agg_tid_rx* zfAggRxEnabled(zdev_t* dev, zbuf_t* buf)
{
    u16_t   dst0, src[3], ac, aid, fragOff;
    u8_t    up;
    u16_t   offset = 0;
    u16_t   seq_no;
    u16_t frameType;
    u16_t frameCtrl;
    u16_t frameSubtype;
    u32_t tcp_seq;
    //struct aggSta *agg_sta;
#if ZM_AGG_FPGA_REORDERING
    struct agg_tid_rx *tid_rx;
#endif
    zmw_get_wlan_dev(dev);

    //zmw_declare_for_critical_section();
    seq_no = zmw_rx_buf_readh(dev, buf, 22) >> 4;
    //DbgPrint("Rx seq=%d\n", seq_no);
    if (wd->sta.EnableHT == 0)
    {
        return NULL;
    }

    frameCtrl = zmw_rx_buf_readb(dev, buf, 0);
    frameType = frameCtrl & 0xf;
    frameSubtype = frameCtrl & 0xf0;


    if (frameType != ZM_WLAN_DATA_FRAME) //non-Qos Data? (frameSubtype&0x80)
    {
        return NULL;
    }
#ifdef ZM_ENABLE_PERFORMANCE_EVALUATION
    tcp_seq = zmw_rx_buf_readb(dev, buf, 22+36) << 24;
    tcp_seq += zmw_rx_buf_readb(dev, buf, 22+37) << 16;
    tcp_seq += zmw_rx_buf_readb(dev, buf, 22+38) << 8;
    tcp_seq += zmw_rx_buf_readb(dev, buf, 22+39);
#endif

    ZM_SEQ_DEBUG("In                   %5d, %12u\n", seq_no, tcp_seq);
    dst0 = zmw_rx_buf_readh(dev, buf, offset+4);

    src[0] = zmw_rx_buf_readh(dev, buf, offset+10);
    src[1] = zmw_rx_buf_readh(dev, buf, offset+12);
    src[2] = zmw_rx_buf_readh(dev, buf, offset+14);

#if ZM_AGG_FPGA_DEBUG
    aid = 0;
#else
    aid = zfApFindSta(dev, src);
#endif

    //agg_sta = &wd->aggSta[aid];
    //zfTxGetIpTosAndFrag(dev, buf, &up, &fragOff);
    //ac = zcUpToAc[up&0x7] & 0x3;

    /*
     * Filter unicast frame only, aid == 0 is for debug only
     */
    if ((dst0 & 0x1) == 0 && aid == 0)
    {
#if ZM_AGG_FPGA_REORDERING
        tid_rx = zfAggRxGetQueue(dev, buf) ;
        if(!tid_rx)
            return NULL;
        else
        {
            //if (tid_rx->addBaExchangeStatusCode == ZM_AGG_ADDBA_RESPONSE)
            return tid_rx;
        }
#else
        return NULL;
#endif
    }

    return NULL;
}

u16_t zfAggRx(zdev_t* dev, zbuf_t* buf, struct zsAdditionInfo *addInfo, struct agg_tid_rx *tid_rx)
{
    u16_t   seq_no;
    s16_t   index;
    u16_t   offset = 0;
    zbuf_t* pbuf;
    u8_t    frameSubType;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    ZM_BUFFER_TRACE(dev, buf)

    ZM_PERFORMANCE_RX_REORDER(dev);

    seq_no = zmw_rx_buf_readh(dev, buf, offset+22) >> 4;

    index = seq_no - tid_rx->seq_start;
    /*
     * for debug
     */

    /* zm_msg2_agg(ZM_LV_0, "queue seq = ", seq_no);
     * DbgPrint("%s:%s%lxh %s%lxh\n", __func__, "queue seq=", seq_no,
     *   "; seq_start=", tid_rx->seq_start);
     */

    //DbgPrint("seq_no=%d, seq_start=%d\n", seq_no, tid_rx->seq_start);

    /* In some APs, we found that it might transmit NULL data whose sequence number
       is out or order. In order to avoid this problem, we ignore these NULL data.
     */

    frameSubType = (zmw_rx_buf_readh(dev, buf, 0) & 0xF0) >> 4;

    /* If this is a NULL data instead of Qos NULL data */
    if ((frameSubType & 0x0C) == 0x04)
    {
        s16_t seq_diff;

        seq_diff = (seq_no > tid_rx->seq_start) ?
                       seq_no - tid_rx->seq_start : tid_rx->seq_start - seq_no;

        if (seq_diff > ZM_AGG_BAW_SIZE)
        {
            zm_debug_msg0("Free Rx NULL data in zfAggRx");

            /* Free Rx buffer */
            zfwBufFree(dev, buf, 0);
            return ZM_ERR_OUT_OF_ORDER_NULL_DATA;
        }
    }

    /*
     * sequence number wrap at 4k
     */
    if (tid_rx->seq_start > seq_no)
    {
        //index += 4096;

        zmw_enter_critical_section(dev);
        if (tid_rx->seq_start >= 4096) {
            tid_rx->seq_start = 0;
        }
        zmw_leave_critical_section(dev);

    }

    if (tid_rx->seq_start == seq_no) {
    	zmw_enter_critical_section(dev);
    	if (((tid_rx->baw_head - tid_rx->baw_tail) & ZM_AGG_BAW_MASK) > 0) {
    	    //DbgPrint("head=%d, tail=%d", tid_rx->baw_head, tid_rx->baw_tail);
            tid_rx->baw_tail = (tid_rx->baw_tail + 1) & ZM_AGG_BAW_MASK;
        }
        tid_rx->seq_start = (tid_rx->seq_start + 1) & (4096 - 1);
    	zmw_leave_critical_section(dev);

        ZM_PERFORMANCE_RX_SEQ(dev, buf);

    	if (wd->zfcbRecv80211 != NULL) {
            //seq_no = zmw_rx_buf_readh(dev, buf, offset+22) >> 4;
            //DbgPrint("Recv indicate seq=%d\n", seq_no);
            //DbgPrint("1. seq=%d\n", seq_no);

            wd->zfcbRecv80211(dev, buf, addInfo);
        }
        else {
            zfiRecv80211(dev, buf, addInfo);
        }
    }
    else if (!zfAggRxEnqueue(dev, buf, tid_rx, addInfo))
    {
        /*
         * duplicated packet
         */
        return 1;
    }

    while (tid_rx->baw_head != tid_rx->baw_tail) {// && tid_rx->frame[tid_rx->baw_tail].buf)
        u16_t tailIndex;

        zmw_enter_critical_section(dev);

        tailIndex = tid_rx->baw_tail;
        pbuf = tid_rx->frame[tailIndex].buf;
        tid_rx->frame[tailIndex].buf = 0;
        if (!pbuf)
        {
            zmw_leave_critical_section(dev);
            break;
        }

        tid_rx->baw_tail = (tid_rx->baw_tail + 1) & ZM_AGG_BAW_MASK;
        tid_rx->seq_start = (tid_rx->seq_start + 1) & (4096 - 1);


        //if(pbuf && tid_rx->baw_size > 0)
        //    tid_rx->baw_size--;

        zmw_leave_critical_section(dev);

        ZM_PERFORMANCE_RX_SEQ(dev, pbuf);

        if (wd->zfcbRecv80211 != NULL)
        {
            //seq_no = zmw_rx_buf_readh(dev, pbuf, offset+22) >> 4;
            //DbgPrint("Recv indicate seq=%d\n", seq_no);
            //DbgPrint("1. seq=%d\n", seq_no);
            wd->zfcbRecv80211(dev, pbuf, addInfo);
        }
        else
        {
            //seq_no = zmw_rx_buf_readh(dev, pbuf, offset+22) >> 4;
            //DbgPrint("Recv indicate seq=%d\n", seq_no);
            zfiRecv80211(dev, pbuf, addInfo);
        }
    }

    return 1;
}

struct agg_tid_rx *zfAggRxGetQueue(zdev_t* dev, zbuf_t* buf)
{
    u16_t   src[3];
    u16_t   aid, ac, i;
    u16_t   offset = 0;
    struct agg_tid_rx *tid_rx = NULL;

    zmw_get_wlan_dev(dev);

    //zmw_declare_for_critical_section();

    src[0] = zmw_rx_buf_readh(dev, buf, offset+10);
    src[1] = zmw_rx_buf_readh(dev, buf, offset+12);
    src[2] = zmw_rx_buf_readh(dev, buf, offset+14);
    aid = zfApFindSta(dev, src);

    ac = (zmw_rx_buf_readh(dev, buf, 24) & 0xF);

    // mark by spin lock debug
    //zmw_enter_critical_section(dev);

    for (i=0; i<ZM_AGG_POOL_SIZE ; i++)
    {
        if((wd->tid_rx[i]->aid == aid) && (wd->tid_rx[i]->ac == ac))
        {
            tid_rx = wd->tid_rx[i];
            break;
        }
    }

    // mark by spin lock debug
    //zmw_leave_critical_section(dev);
    return tid_rx;
}


u16_t   zfAggRxEnqueue(zdev_t* dev, zbuf_t* buf, struct agg_tid_rx *tid_rx, struct zsAdditionInfo *addInfo)
{
    u16_t seq_no, offset = 0;
    u16_t q_index;
    s16_t index;
    u8_t  bdropframe = 0;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    ZM_BUFFER_TRACE(dev, buf)

    seq_no = zmw_rx_buf_readh(dev, buf, offset+22) >> 4;
    index  = seq_no - tid_rx->seq_start;

    /*
     * sequence number wrap at 4k
     * -1000: check for duplicate past packet
     */
    bdropframe = 0;
    if (tid_rx->seq_start > seq_no) {
        if ((tid_rx->seq_start > 3967) && (seq_no < 128)) {
            index += 4096;
        } else if (tid_rx->seq_start - seq_no > 70) {
            zmw_enter_critical_section(dev);
            tid_rx->sq_behind_count++;
            if (tid_rx->sq_behind_count > 3) {
                tid_rx->sq_behind_count = 0;
            } else {
                bdropframe = 1;
            }
            zmw_leave_critical_section(dev);
        } else {
            bdropframe = 1;
        }
    } else {
        if (seq_no - tid_rx->seq_start > 70) {
            zmw_enter_critical_section(dev);
            tid_rx->sq_exceed_count++;
            if (tid_rx->sq_exceed_count > 3) {
                tid_rx->sq_exceed_count = 0;
            } else {
                bdropframe = 1;
            }
            zmw_leave_critical_section(dev);
        }
    }

    if (bdropframe == 1) {
        /*if (wd->zfcbRecv80211 != NULL) {
            wd->zfcbRecv80211(dev, buf, addInfo);
        }
        else {
            zfiRecv80211(dev, buf, addInfo);
        }*/

        ZM_PERFORMANCE_FREE(dev, buf);

        zfwBufFree(dev, buf, 0);
        /*zfAggRxFlush(dev, seq_no, tid_rx);
        tid_rx->seq_start = seq_no;
        index = seq_no - tid_rx->seq_start;
        */

        //DbgPrint("Free an old packet, seq_start=%d, seq_no=%d\n", tid_rx->seq_start, seq_no);

        /*
         * duplicate past packet
         * happens only in simulated aggregation environment
         */
        return 0;
    } else {
        zmw_enter_critical_section(dev);
        if (tid_rx->sq_exceed_count > 0){
            tid_rx->sq_exceed_count--;
        }

        if (tid_rx->sq_behind_count > 0) {
            tid_rx->sq_behind_count--;
        }
        zmw_leave_critical_section(dev);
    }

    if (index < 0) {
        zfAggRxFlush(dev, seq_no, tid_rx);
        tid_rx->seq_start = seq_no;
        index = 0;
    }

    //if (index >= (ZM_AGG_BAW_SIZE - 1))
    if (index >= (ZM_AGG_BAW_MASK))
    {
        /*
         * queue full
         */
        //DbgPrint("index >= 64, seq_start=%d, seq_no=%d\n", tid_rx->seq_start, seq_no);
        zfAggRxFlush(dev, seq_no, tid_rx);
        //tid_rx->seq_start = seq_no;
        index = seq_no - tid_rx->seq_start;
        if ((tid_rx->seq_start > seq_no) && (tid_rx->seq_start > 1000) && (tid_rx->seq_start - 1000) > seq_no)
        {
        //index = seq_no - tid_rx->seq_start;
            index += 4096;
        }
        //index = seq_no - tid_rx->seq_start;
        while (index >= (ZM_AGG_BAW_MASK)) {
            //DbgPrint("index >= 64, seq_start=%d, seq_no=%d\n", tid_rx->seq_start, seq_no);
            tid_rx->seq_start = (tid_rx->seq_start + ZM_AGG_BAW_MASK) & (4096 - 1);
            index = seq_no - tid_rx->seq_start;
            if ((tid_rx->seq_start > seq_no) && (tid_rx->seq_start > 1000) && (tid_rx->seq_start - 1000) > seq_no)
            {
                index += 4096;
            }
        }
    }


    q_index = (tid_rx->baw_tail + index) & ZM_AGG_BAW_MASK;
    if (tid_rx->frame[q_index].buf && (((tid_rx->baw_head - tid_rx->baw_tail) & ZM_AGG_BAW_MASK) >
                (((q_index) - tid_rx->baw_tail) & ZM_AGG_BAW_MASK)))
    {

        ZM_PERFORMANCE_DUP(dev, tid_rx->frame[q_index].buf, buf);
        zfwBufFree(dev, buf, 0);
        //DbgPrint("Free a duplicate packet, seq_start=%d, seq_no=%d\n", tid_rx->seq_start, seq_no);
        //DbgPrint("head=%d, tail=%d", tid_rx->baw_head, tid_rx->baw_tail);
        /*
         * duplicate packet
         */
        return 0;
    }

    zmw_enter_critical_section(dev);
    if(tid_rx->frame[q_index].buf) {
        zfwBufFree(dev, tid_rx->frame[q_index].buf, 0);
        tid_rx->frame[q_index].buf = 0;
    }

    tid_rx->frame[q_index].buf = buf;
    tid_rx->frame[q_index].arrivalTime = zm_agg_GetTime();
    zfwMemoryCopy((void*)&tid_rx->frame[q_index].addInfo, (void*)addInfo, sizeof(struct zsAdditionInfo));

    /*
     * for debug simulated aggregation only,
     * should be done in rx of ADDBA Request
     */
    //tid_rx->addInfo = addInfo;


    if (((tid_rx->baw_head - tid_rx->baw_tail) & ZM_AGG_BAW_MASK) <= index)
    {
        //tid_rx->baw_size = index + 1;
        if (((tid_rx->baw_head - tid_rx->baw_tail) & ZM_AGG_BAW_MASK) <=
                //((q_index + 1) & ZM_AGG_BAW_MASK))
                (((q_index) - tid_rx->baw_tail) & ZM_AGG_BAW_MASK))//tid_rx->baw_size )
            tid_rx->baw_head = (q_index + 1) & ZM_AGG_BAW_MASK;
    }
    zmw_leave_critical_section(dev);

    /*
     * success
     */
    //DbgPrint("head=%d, tail=%d, start=%d", tid_rx->baw_head, tid_rx->baw_tail, tid_rx->seq_start);
    return 1;
}

u16_t zfAggRxFlush(zdev_t* dev, u16_t seq_no, struct agg_tid_rx *tid_rx)
{
    zbuf_t* pbuf;
    u16_t   seq;
    struct zsAdditionInfo addInfo;
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    ZM_PERFORMANCE_RX_FLUSH(dev);

    while (1)
    {
        zmw_enter_critical_section(dev);
        if (tid_rx->baw_tail == tid_rx->baw_head) {
            zmw_leave_critical_section(dev);
            break;
        }

        pbuf = tid_rx->frame[tid_rx->baw_tail].buf;
        zfwMemoryCopy((void*)&addInfo, (void*)&tid_rx->frame[tid_rx->baw_tail].addInfo, sizeof(struct zsAdditionInfo));
        tid_rx->frame[tid_rx->baw_tail].buf = 0;
        //if(pbuf && tid_rx->baw_size > 0) tid_rx->baw_size--;
        tid_rx->baw_tail = (tid_rx->baw_tail + 1) & ZM_AGG_BAW_MASK;
        tid_rx->seq_start = (tid_rx->seq_start + 1) & (4096 - 1);
	    zmw_leave_critical_section(dev);

        if (pbuf)
        {

            ZM_PERFORMANCE_RX_SEQ(dev, pbuf);

            if (wd->zfcbRecv80211 != NULL)
            {
                seq = zmw_rx_buf_readh(dev, pbuf, 22) >> 4;
                //DbgPrint("Recv indicate seq=%d\n", seq);
                //DbgPrint("2. seq=%d\n", seq);
                wd->zfcbRecv80211(dev, pbuf, &addInfo);
            }
            else
            {
                seq = zmw_rx_buf_readh(dev, pbuf, 22) >> 4;
                //DbgPrint("Recv indicate seq=%d\n", seq);
                zfiRecv80211(dev, pbuf, &addInfo);
            }
        }
    }

    zmw_enter_critical_section(dev);
    tid_rx->baw_head = tid_rx->baw_tail = 0;
    zmw_leave_critical_section(dev);
    return 1;
}



/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfAggRxFreeBuf              */
/*      Frees all queued packets in buffer when the driver is down.     */
/*      The zfFreeResource() will check if the buffer is all freed.     */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev     : device pointer                                        */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      ZM_SUCCESS                                                      */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Honda               Atheros Communications, INC.    2006.12     */
/*                                                                      */
/************************************************************************/
u16_t   zfAggRxFreeBuf(zdev_t* dev, u16_t destroy)
{
    u16_t   i;
    zbuf_t* buf;
    struct agg_tid_rx *tid_rx;

    TID_TX  tid_tx;
    //struct bufInfo *buf_info;

    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    for (i=0; i<ZM_AGG_POOL_SIZE; i++)
    {
        u16_t j;

        tid_rx = wd->tid_rx[i];

        for(j=0; j <= ZM_AGG_BAW_SIZE; j++)
        {
            zmw_enter_critical_section(dev);
            buf = tid_rx->frame[j].buf;
            tid_rx->frame[j].buf = 0;
            zmw_leave_critical_section(dev);

            if (buf)
            {
                zfwBufFree(dev, buf, 0);
            }
        }

        #if 0
        if ( tid_rx->baw_head != tid_rx->baw_tail )
        {
            while (tid_rx->baw_head != tid_rx->baw_tail)
            {
                buf = tid_rx->frame[tid_rx->baw_tail].buf;
                tid_rx->frame[tid_rx->baw_tail].buf = 0;
                if (buf)
                {
                    zfwBufFree(dev, buf, 0);

                    zmw_enter_critical_section(dev);
                    tid_rx->frame[tid_rx->baw_tail].buf = 0;
                    zmw_leave_critical_section(dev);
                }
                zmw_enter_critical_section(dev);
                //if (tid_rx->baw_size > 0)tid_rx->baw_size--;
                tid_rx->baw_tail = (tid_rx->baw_tail + 1) & ZM_AGG_BAW_MASK;
                tid_rx->seq_start++;
                zmw_leave_critical_section(dev);
            }
        }
        #endif

        zmw_enter_critical_section(dev);
        tid_rx->seq_start = 0;
        tid_rx->baw_head = tid_rx->baw_tail = 0;
        tid_rx->aid = ZM_MAX_STA_SUPPORT;
        zmw_leave_critical_section(dev);

        #ifdef ZM_ENABLE_AGGREGATION
        #ifndef ZM_ENABLE_FW_BA_RETRANSMISSION //disable BAW
        if (tid_baw->enabled) {
            zm_msg1_agg(ZM_LV_0, "Device down, clear BAW queue:", i);
            BAW->disable(dev, tid_baw);
        }
        #endif
        #endif
        if (1 == wd->aggQPool[i]->aggQEnabled) {
            tid_tx = wd->aggQPool[i];
            buf = zfAggTxGetVtxq(dev, tid_tx);
            while (buf) {
                zfwBufFree(dev, buf, 0);
                buf = zfAggTxGetVtxq(dev, tid_tx);
            }
        }

        if(destroy) {
            zfwMemFree(dev, wd->aggQPool[i], sizeof(struct aggQueue));
            zfwMemFree(dev, wd->tid_rx[i], sizeof(struct agg_tid_rx));
        }
    }
    #ifdef ZM_ENABLE_AGGREGATION
    #ifndef ZM_ENABLE_FW_BA_RETRANSMISSION //disable BAW
    if(destroy) zfwMemFree(dev, BAW, sizeof(struct baw_enabler));
    #endif
    #endif
    return ZM_SUCCESS;
}


void zfAggRecvBAR(zdev_t* dev, zbuf_t *buf) {
    u16_t start_seq, len;
    u8_t i, bitmap[8];
    len = zfwBufGetSize(dev, buf);
    start_seq = zmw_rx_buf_readh(dev, buf, len-2);
    DbgPrint("Received a BAR Control frame, start_seq=%d", start_seq>>4);
    /* todo: set the bitmap by reordering buffer! */
    for (i=0; i<8; i++) bitmap[i]=0;
    zfSendBA(dev, start_seq, bitmap);
}

#ifdef ZM_ENABLE_AGGREGATION
#ifndef ZM_ENABLE_FW_BA_RETRANSMISSION //disable BAW
void zfAggTxRetransmit(zdev_t* dev, struct bufInfo *buf_info, struct aggControl *aggControl, TID_TX tid_tx) {
    u16_t removeLen;
    u16_t err;

    zmw_get_wlan_dev(dev);
    if (aggControl && (ZM_AGG_FIRST_MPDU == aggControl->ampduIndication) ) {
        tid_tx->bar_ssn = buf_info->baw_header->header[15];
        aggControl->tid_baw->start_seq = tid_tx->bar_ssn >> 4;
        zm_msg1_agg(ZM_LV_0, "start seq=", tid_tx->bar_ssn >> 4);
    }
    buf_info->baw_header->header[4] |= (1 << 11);
    if (aggControl && aggControl->aggEnabled) {
        //if (wd->enableAggregation==0 && !(buf_info->baw_header->header[6]&0x1))
        //{
            //if (((buf_info->baw_header->header[2] & 0x3) == 2))
            //{
                /* Enable aggregation */
                buf_info->baw_header->header[1] |= 0x20;
                if (ZM_AGG_LAST_MPDU == aggControl->ampduIndication) {
                    buf_info->baw_header->header[1] |= 0x4000;
                }
                else {
                    buf_info->baw_header->header[1] &= ~0x4000;
                    //zm_debug_msg0("ZM_AGG_LAST_MPDU");
                }
            //}
            //else {
            //    zm_debug_msg1("no aggr, header[2]&0x3 = ",buf_info->baw_header->header[2] & 0x3)
            //    aggControl->aggEnabled = 0;
            //}
        //}
        //else {
        //    zm_debug_msg1("no aggr, wd->enableAggregation = ", wd->enableAggregation);
        //    zm_debug_msg1("no aggr, !header[6]&0x1 = ",!(buf_info->baw_header->header[6]&0x1));
        //    aggControl->aggEnabled = 0;
        //}
    }

    /*if (aggControl->tid_baw) {
        struct baw_header_r header_r;

        header_r.header      = buf_info->baw_header->header;
        header_r.mic         = buf_info->baw_header->mic;
        header_r.snap        = buf_info->baw_header->snap;
        header_r.headerLen   = buf_info->baw_header->headerLen;
        header_r.micLen      = buf_info->baw_header->micLen;
        header_r.snapLen     = buf_info->baw_header->snapLen;
        header_r.removeLen   = buf_info->baw_header->removeLen;
        header_r.keyIdx      = buf_info->baw_header->keyIdx;

        BAW->insert(dev, buf_info->buf, tid_tx->bar_ssn >> 4, aggControl->tid_baw, buf_info->baw_retransmit, &header_r);
    }*/

    if ((err = zfHpSend(dev,
                    buf_info->baw_header->header,
                    buf_info->baw_header->headerLen,
                    buf_info->baw_header->snap,
                    buf_info->baw_header->snapLen,
                    buf_info->baw_header->mic,
                    buf_info->baw_header->micLen,
                    buf_info->buf,
                    buf_info->baw_header->removeLen,
                    ZM_EXTERNAL_ALLOC_BUF,
                    (u8_t)tid_tx->ac,
                    buf_info->baw_header->keyIdx)) != ZM_SUCCESS)
    {
        goto zlError;
    }

    return;

zlError:
    zfwBufFree(dev, buf_info->buf, 0);
    return;

}
#endif //disable BAW
#endif
/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfAggTxSendEth              */
/*      Called to transmit Ethernet frame from upper elayer.            */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : buffer pointer                                            */
/*      port : WLAN port, 0=>standard, 0x10-0x17=>VAP, 0x20-0x25=>WDS   */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      error code                                                      */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen, Honda      Atheros Communications, Inc.    2006.12     */
/*                                                                      */
/************************************************************************/
u16_t zfAggTxSendEth(zdev_t* dev, zbuf_t* buf, u16_t port, u16_t bufType, u8_t flag, struct aggControl *aggControl, TID_TX tid_tx)
{
    u16_t err;
    //u16_t addrTblSize;
    //struct zsAddrTbl addrTbl;
    u16_t removeLen;
    u16_t header[(8+30+2+18)/2];    /* ctr+(4+a1+a2+a3+2+a4)+qos+iv */
    u16_t headerLen;
    u16_t mic[8/2];
    u16_t micLen;
    u16_t snap[8/2];
    u16_t snapLen;
    u16_t fragLen;
    u16_t frameLen;
    u16_t fragNum;
    struct zsFrag frag;
    u16_t i, id;
    u16_t da[3];
    u16_t sa[3];
    u8_t up;
    u8_t qosType, keyIdx = 0;
    u16_t fragOff;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zm_msg1_tx(ZM_LV_2, "zfTxSendEth(), port=", port);

    /* Get IP TOS for QoS AC and IP frag offset */
    zfTxGetIpTosAndFrag(dev, buf, &up, &fragOff);

#ifdef ZM_ENABLE_NATIVE_WIFI
    if ( wd->wlanMode == ZM_MODE_INFRASTRUCTURE )
    {
        /* DA */
        da[0] = zmw_tx_buf_readh(dev, buf, 16);
        da[1] = zmw_tx_buf_readh(dev, buf, 18);
        da[2] = zmw_tx_buf_readh(dev, buf, 20);
        /* SA */
        sa[0] = zmw_tx_buf_readh(dev, buf, 10);
        sa[1] = zmw_tx_buf_readh(dev, buf, 12);
        sa[2] = zmw_tx_buf_readh(dev, buf, 14);
    }
    else if ( wd->wlanMode == ZM_MODE_IBSS )
    {
        /* DA */
        da[0] = zmw_tx_buf_readh(dev, buf, 4);
        da[1] = zmw_tx_buf_readh(dev, buf, 6);
        da[2] = zmw_tx_buf_readh(dev, buf, 8);
        /* SA */
        sa[0] = zmw_tx_buf_readh(dev, buf, 10);
        sa[1] = zmw_tx_buf_readh(dev, buf, 12);
        sa[2] = zmw_tx_buf_readh(dev, buf, 14);
    }
    else if ( wd->wlanMode == ZM_MODE_AP )
    {
        /* DA */
        da[0] = zmw_tx_buf_readh(dev, buf, 4);
        da[1] = zmw_tx_buf_readh(dev, buf, 6);
        da[2] = zmw_tx_buf_readh(dev, buf, 8);
        /* SA */
        sa[0] = zmw_tx_buf_readh(dev, buf, 16);
        sa[1] = zmw_tx_buf_readh(dev, buf, 18);
        sa[2] = zmw_tx_buf_readh(dev, buf, 20);
    }
    else
    {
        //
    }
#else
    /* DA */
    da[0] = zmw_tx_buf_readh(dev, buf, 0);
    da[1] = zmw_tx_buf_readh(dev, buf, 2);
    da[2] = zmw_tx_buf_readh(dev, buf, 4);
    /* SA */
    sa[0] = zmw_tx_buf_readh(dev, buf, 6);
    sa[1] = zmw_tx_buf_readh(dev, buf, 8);
    sa[2] = zmw_tx_buf_readh(dev, buf, 10);
#endif
    //Decide Key Index in ATOM, No meaning in OTUS--CWYang(m)
    if (wd->wlanMode == ZM_MODE_AP)
    {
        keyIdx = wd->ap.bcHalKeyIdx[port];
        id = zfApFindSta(dev, da);
        if (id != 0xffff)
        {
            switch (wd->ap.staTable[id].encryMode)
            {
            case ZM_AES:
            case ZM_TKIP:
#ifdef ZM_ENABLE_CENC
            case ZM_CENC:
#endif //ZM_ENABLE_CENC
                keyIdx = wd->ap.staTable[id].keyIdx;
                break;
            }
        }
    }
    else
    {
        switch (wd->sta.encryMode)
        {
        case ZM_WEP64:
        case ZM_WEP128:
        case ZM_WEP256:
            keyIdx = wd->sta.keyId;
            break;
        case ZM_AES:
        case ZM_TKIP:
            if ((da[0]& 0x1))
                keyIdx = 5;
            else
                keyIdx = 4;
            break;
#ifdef ZM_ENABLE_CENC
        case ZM_CENC:
            keyIdx = wd->sta.cencKeyId;
            break;
#endif //ZM_ENABLE_CENC
        }
    }

    /* Create SNAP */
    removeLen = zfTxGenWlanSnap(dev, buf, snap, &snapLen);
    //zm_msg1_tx(ZM_LV_0, "fragOff=", fragOff);

    fragLen = wd->fragThreshold;
    frameLen = zfwBufGetSize(dev, buf);
    frameLen -= removeLen;

#if 0
    /* Create MIC */
    if ( (wd->wlanMode == ZM_MODE_INFRASTRUCTURE)&&
         (wd->sta.encryMode == ZM_TKIP) )
    {
        if ( frameLen > fragLen )
        {
            micLen = zfTxGenWlanTail(dev, buf, snap, snapLen, mic);
        }
        else
        {
            /* append MIC by HMAC */
            micLen = 8;
        }
    }
    else
    {
        micLen = 0;
    }
#else
    if ( frameLen > fragLen )
    {
        micLen = zfTxGenWlanTail(dev, buf, snap, snapLen, mic);
    }
    else
    {
        /* append MIC by HMAC */
        micLen = 0;
    }
#endif

    /* Access Category */
    if (wd->wlanMode == ZM_MODE_AP)
    {
        zfApGetStaQosType(dev, da, &qosType);
        if (qosType == 0)
        {
            up = 0;
        }
    }
    else if (wd->wlanMode == ZM_MODE_INFRASTRUCTURE)
    {
        if (wd->sta.wmeConnected == 0)
        {
            up = 0;
        }
    }
    else
    {
        /* TODO : STA QoS control field */
        up = 0;
    }

    /* Assign sequence number */
    zmw_enter_critical_section(dev);
    frag.seq[0] = ((wd->seq[zcUpToAc[up&0x7]]++) << 4);
    if (aggControl && (ZM_AGG_FIRST_MPDU == aggControl->ampduIndication) ) {
        tid_tx->bar_ssn = frag.seq[0];

        zm_msg1_agg(ZM_LV_0, "start seq=", tid_tx->bar_ssn >> 4);
    }
    //tid_tx->baw_buf[tid_tx->baw_head-1].baw_seq=frag.seq[0];
    zmw_leave_critical_section(dev);


        frag.buf[0] = buf;
        frag.bufType[0] = bufType;
        frag.flag[0] = flag;
        fragNum = 1;

    for (i=0; i<fragNum; i++)
    {
        /* Create WLAN header(Control Setting + 802.11 header + IV) */
        if (up !=0 ) zm_debug_msg1("up not 0, up=",up);
        headerLen = zfTxGenWlanHeader(dev, frag.buf[i], header, frag.seq[i],
                                      frag.flag[i], snapLen+micLen, removeLen,
                                      port, da, sa, up, &micLen, snap, snapLen,
                                      aggControl);

        /* Get buffer DMA address */
        //if ((addrTblSize = zfwBufMapDma(dev, frag.buf[i], &addrTbl)) == 0)
        //if ((addrTblSize = zfwMapTxDma(dev, frag.buf[i], &addrTbl)) == 0)
        //{
        //    err = ZM_ERR_BUFFER_DMA_ADDR;
        //    goto zlError;
        //}

        /* Flush buffer on cache */
        //zfwBufFlush(dev, frag.buf[i]);

#if 0
        zm_msg1_tx(ZM_LV_0, "headerLen=", headerLen);
        zm_msg1_tx(ZM_LV_0, "snapLen=", snapLen);
        zm_msg1_tx(ZM_LV_0, "micLen=", micLen);
        zm_msg1_tx(ZM_LV_0, "removeLen=", removeLen);
        zm_msg1_tx(ZM_LV_0, "addrTblSize=", addrTblSize);
        zm_msg1_tx(ZM_LV_0, "frag.bufType[0]=", frag.bufType[0]);
#endif

        fragLen = zfwBufGetSize(dev, frag.buf[i]);
        if ((da[0]&0x1) == 0)
        {
            wd->commTally.txUnicastFrm++;
            wd->commTally.txUnicastOctets += (fragLen+snapLen);
        }
        else if ((da[0]& 0x1))
        {
            wd->commTally.txBroadcastFrm++;
            wd->commTally.txBroadcastOctets += (fragLen+snapLen);
        }
        else
        {
            wd->commTally.txMulticastFrm++;
            wd->commTally.txMulticastOctets += (fragLen+snapLen);
        }
        wd->ledStruct.txTraffic++;

#if 0 //Who care this?
        if ( (i)&&(i == (fragNum-1)) )
        {
            wd->trafTally.txDataByteCount -= micLen;
        }
#endif

        /*if (aggControl->tid_baw && aggControl->aggEnabled) {
            struct baw_header_r header_r;

            header_r.header      = header;
            header_r.mic         = mic;
            header_r.snap        = snap;
            header_r.headerLen   = headerLen;
            header_r.micLen      = micLen;
            header_r.snapLen     = snapLen;
            header_r.removeLen   = removeLen;
            header_r.keyIdx      = keyIdx;

            BAW->insert(dev, buf, tid_tx->bar_ssn >> 4, aggControl->tid_baw, 0, &header_r);
        }*/

        if ((err = zfHpSend(dev, header, headerLen, snap, snapLen,
                             mic, micLen, frag.buf[i], removeLen,
                             frag.bufType[i], zcUpToAc[up&0x7], keyIdx)) != ZM_SUCCESS)
        {
            goto zlError;
        }


        continue;

zlError:
        if (frag.bufType[i] == ZM_EXTERNAL_ALLOC_BUF)
        {
            zfwBufFree(dev, frag.buf[i], err);
        }
        else if (frag.bufType[i] == ZM_INTERNAL_ALLOC_BUF)
        {
            zfwBufFree(dev, frag.buf[i], 0);
        }
        else
        {
            zm_assert(0);
        }
    } /* for (i=0; i<fragNum; i++) */

    return ZM_SUCCESS;
}

/*
 * zfAggSendADDBA() refers zfSendMmFrame() in cmm.c
 */
u16_t   zfAggSendAddbaRequest(zdev_t* dev, u16_t *dst, u16_t ac, u16_t up)
{
    zbuf_t* buf;
    //u16_t addrTblSize;
    //struct zsAddrTbl addrTbl;
    //u16_t err;
    u16_t offset = 0;
    u16_t hlen = 32;
    u16_t header[(24+25+1)/2];
    u16_t vap = 0;
    u16_t i;
    u8_t encrypt = 0;

    //zmw_get_wlan_dev(dev);

    //zmw_declare_for_critical_section();


    /*
     * TBD : Maximum size of managment frame
     */
    if ((buf = zfwBufAllocate(dev, 1024)) == NULL)
    {
        zm_msg0_mm(ZM_LV_0, "Alloc mm buf Fail!");
        return ZM_SUCCESS;
    }

    /*
     * Reserve room for wlan header
     */
    offset = hlen;

    /*
     * add addba frame body
     */
    offset = zfAggSetAddbaFrameBody(dev, buf, offset, ac, up);


    zfwBufSetSize(dev, buf, offset);

    /*
     * Copy wlan header
     */
    zfAggGenAddbaHeader(dev, dst, header, offset-hlen, buf, vap, encrypt);
    for (i=0; i<(hlen>>1); i++)
    {
        zmw_tx_buf_writeh(dev, buf, i*2, header[i]);
    }

    /* Get buffer DMA address */
    //if ((addrTblSize = zfwBufMapDma(dev, buf, &addrTbl)) == 0)
    //if ((addrTblSize = zfwMapTxDma(dev, buf, &addrTbl)) == 0)
    //{
    //    goto zlError;
    //}

    //zm_msg2_mm(ZM_LV_2, "offset=", offset);
    //zm_msg2_mm(ZM_LV_2, "hlen=", hlen);
    //zm_msg2_mm(ZM_LV_2, "addrTblSize=", addrTblSize);
    //zm_msg2_mm(ZM_LV_2, "addrTbl.len[0]=", addrTbl.len[0]);
    //zm_msg2_mm(ZM_LV_2, "addrTbl.physAddrl[0]=", addrTbl.physAddrl[0]);
    //zm_msg2_mm(ZM_LV_2, "buf->data=", buf->data);

    #if 0
    if ((err = zfHpSend(dev, NULL, 0, NULL, 0, NULL, 0, buf, 0,
            ZM_INTERNAL_ALLOC_BUF, 0, 0xff)) != ZM_SUCCESS)
    {
        goto zlError;
    }
    #else
    zfPutVmmq(dev, buf);
    zfPushVtxq(dev);
    #endif

    return ZM_SUCCESS;

}

u16_t   zfAggSetAddbaFrameBody(zdev_t* dev, zbuf_t* buf, u16_t offset, u16_t ac, u16_t up)
{
    u16_t ba_parameter, start_seq;

    zmw_get_wlan_dev(dev);

    //zmw_declare_for_critical_section();
    /*
     * ADDBA Request frame body
     */

    /*
     * Category
     */
    zmw_tx_buf_writeb(dev, buf, offset++, 3);
    /*
     * Action details = 0
     */
    zmw_tx_buf_writeb(dev, buf, offset++, ZM_WLAN_ADDBA_REQUEST_FRAME);
    /*
     * Dialog Token = nonzero
     * TBD: define how to get dialog token?
     */
    zmw_tx_buf_writeb(dev, buf, offset++, 2);
    /*
     * Block Ack parameter set
     * BA policy = 1 for immediate BA, 0 for delayed BA
     * TID(4bits) & buffer size(4bits) (TID=up & buffer size=0x80)
     * TBD: how to get buffer size?
     * ¢z¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢s¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢s¢w¢w¢w¢w¢w¢w¢w¢w¢s¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢{
     * ¢x    B0    ¢x    B1     ¢x B2  B5 ¢x B6      B15 ¢x
     * ¢u¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢q¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢q¢w¢w¢w¢w¢w¢w¢w¢w¢q¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢t
     * ¢x Reserved ¢x BA policy ¢x  TID   ¢x Buffer size ¢x
     * ¢|¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢r¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢r¢w¢w¢w¢w¢w¢w¢w¢w¢r¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢}
     */
    ba_parameter = 1 << 12;     // buffer size = 0x40(64)
    ba_parameter |= up << 2;    // tid = up
    ba_parameter |= 2;          // ba policy = 1
    zmw_tx_buf_writeh(dev, buf, offset, ba_parameter);
    offset+=2;
    /*
     * BA timeout value
     */
    zmw_tx_buf_writeh(dev, buf, offset, 0);
    offset+=2;
    /*
     * BA starting sequence number
     * ¢z¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢s¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢{
     * ¢x B0       B3 ¢x B4              B15 ¢x
     * ¢u¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢q¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢t
     * ¢x Frag num(0) ¢x BA starting seq num ¢x
     * ¢|¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢r¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢}
     */
    start_seq = ((wd->seq[ac]) << 4) & 0xFFF0;
    zmw_tx_buf_writeh(dev, buf, offset, start_seq);
    offset+=2;

    return offset;
}

u16_t zfAggGenAddbaHeader(zdev_t* dev, u16_t* dst,
        u16_t* header, u16_t len, zbuf_t* buf, u16_t vap, u8_t encrypt)
{
    u8_t  hlen = 32;        // MAC ctrl + PHY ctrl + 802.11 MM header
    //u8_t frameType = ZM_WLAN_FRAME_TYPE_ACTION;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    /*
     * Generate control setting
     */
    //bodyLen = zfwBufGetSize(dev, buf);
    header[0] = 24+len+4;   //Length
    header[1] = 0x8;        //MAC control, backoff + (ack)

#if 0
    /* CCK 1M */
    header[2] = 0x0f00;          //PHY control L
    header[3] = 0x0000;          //PHY control H
#else
    /* OFDM 6M */
    header[2] = 0x0f01;          //PHY control L
    header[3] = 0x000B;          //PHY control H
#endif

    /*
     * Generate WLAN header
     * Frame control frame type and subtype
     */
    header[4+0] = ZM_WLAN_FRAME_TYPE_ACTION;
    /*
     * Duration
     */
    header[4+1] = 0;

    if (wd->wlanMode == ZM_MODE_INFRASTRUCTURE)
    {
        header[4+8] = wd->sta.bssid[0];
        header[4+9] = wd->sta.bssid[1];
        header[4+10] = wd->sta.bssid[2];
    }
    else if (wd->wlanMode == ZM_MODE_PSEUDO)
    {
        /* Address 3 = 00:00:00:00:00:00 */
        header[4+8] = 0;
        header[4+9] = 0;
        header[4+10] = 0;
    }
    else if (wd->wlanMode == ZM_MODE_IBSS)
    {
        header[4+8] = wd->sta.bssid[0];
        header[4+9] = wd->sta.bssid[1];
        header[4+10] = wd->sta.bssid[2];
    }
    else if (wd->wlanMode == ZM_MODE_AP)
    {
        /* Address 3 = BSSID */
        header[4+8] = wd->macAddr[0];
        header[4+9] = wd->macAddr[1];
        header[4+10] = wd->macAddr[2] + (vap<<8);
    }

    /* Address 1 = DA */
    header[4+2] = dst[0];
    header[4+3] = dst[1];
    header[4+4] = dst[2];

    /* Address 2 = SA */
    header[4+5] = wd->macAddr[0];
    header[4+6] = wd->macAddr[1];
    if (wd->wlanMode == ZM_MODE_AP)
    {
        header[4+7] = wd->macAddr[2] + (vap<<8);
    }
    else
    {
        header[4+7] = wd->macAddr[2];
    }

    /* Sequence Control */
    zmw_enter_critical_section(dev);
    header[4+11] = ((wd->mmseq++)<<4);
    zmw_leave_critical_section(dev);


    return hlen;
}


u16_t   zfAggProcessAction(zdev_t* dev, zbuf_t* buf)
{
    u16_t category;

    //zmw_get_wlan_dev(dev);

    //zmw_declare_for_critical_section();

    category = zmw_rx_buf_readb(dev, buf, 24);

    switch (category)
    {
    case ZM_WLAN_BLOCK_ACK_ACTION_FRAME:
        zfAggBlockAckActionFrame(dev, buf);
        break;

    }

    return ZM_SUCCESS;
}


u16_t   zfAggBlockAckActionFrame(zdev_t* dev, zbuf_t* buf)
{
    u8_t action;

    //zmw_get_wlan_dev(dev);

    //zmw_declare_for_critical_section();

    action = zmw_rx_buf_readb(dev, buf, 25);
#ifdef ZM_ENABLE_AGGREGATION
    switch (action)
    {
    case ZM_WLAN_ADDBA_REQUEST_FRAME:
        zm_msg0_agg(ZM_LV_0, "Received BA Action frame is ADDBA request");
        zfAggRecvAddbaRequest(dev, buf);
        break;
    case ZM_WLAN_ADDBA_RESPONSE_FRAME:
        zm_msg0_agg(ZM_LV_0, "Received BA Action frame is ADDBA response");
        zfAggRecvAddbaResponse(dev, buf);
        break;
    case ZM_WLAN_DELBA_FRAME:
        zfAggRecvDelba(dev, buf);
        break;
    }
#endif
    return ZM_SUCCESS;
}

u16_t   zfAggRecvAddbaRequest(zdev_t* dev, zbuf_t* buf)
{
    //u16_t dialog;
    struct aggBaFrameParameter bf;
    u16_t i;
    //zmw_get_wlan_dev(dev);

    //zmw_declare_for_critical_section();

    bf.buf = buf;
    bf.dialog = zmw_rx_buf_readb(dev, buf, 26);
    /*
     * ba parameter set
     */
    bf.ba_parameter = zmw_rx_buf_readh(dev, buf, 27);
    bf.ba_policy   = (bf.ba_parameter >> 1) & 1;
    bf.tid         = (bf.ba_parameter >> 2) & 0xF;
    bf.buffer_size = (bf.ba_parameter >> 6);
    /*
     * BA timeout value
     */
    bf.ba_timeout = zmw_rx_buf_readh(dev, buf, 29);
    /*
     * BA starting sequence number
     */
    bf.ba_start_seq = zmw_rx_buf_readh(dev, buf, 31) >> 4;

    i=26;
    while(i < 32) {
        zm_debug_msg2("Recv ADDBA Req:", zmw_rx_buf_readb(dev,buf,i));
        i++;
    }

    zfAggSendAddbaResponse(dev, &bf);

    zfAggAddbaSetTidRx(dev, buf, &bf);

    return ZM_SUCCESS;
}

u16_t   zfAggAddbaSetTidRx(zdev_t* dev, zbuf_t* buf, struct aggBaFrameParameter *bf)
{
    u16_t i, ac, aid, fragOff;
    u16_t src[3];
    u16_t offset = 0;
    u8_t  up;
    struct agg_tid_rx *tid_rx = NULL;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    src[0] = zmw_rx_buf_readh(dev, buf, offset+10);
    src[1] = zmw_rx_buf_readh(dev, buf, offset+12);
    src[2] = zmw_rx_buf_readh(dev, buf, offset+14);
    aid = zfApFindSta(dev, src);

    zfTxGetIpTosAndFrag(dev, buf, &up, &fragOff);
    ac = zcUpToAc[up&0x7] & 0x3;

    ac = bf->tid;

    for (i=0; i<ZM_AGG_POOL_SIZE ; i++)
    {
        if((wd->tid_rx[i]->aid == aid) && (wd->tid_rx[i]->ac == ac))
        {
            tid_rx = wd->tid_rx[i];
            break;
        }
    }

    if (!tid_rx)
    {
        for (i=0; i<ZM_AGG_POOL_SIZE; i++)
        {
            if (wd->tid_rx[i]->aid == ZM_MAX_STA_SUPPORT)
            {
                tid_rx = wd->tid_rx[i];
                break;
            }
        }
        if (!tid_rx)
            return 0;
    }

    zmw_enter_critical_section(dev);

    tid_rx->aid = aid;
    tid_rx->ac = ac;
    tid_rx->addBaExchangeStatusCode = ZM_AGG_ADDBA_RESPONSE;
    tid_rx->seq_start = bf->ba_start_seq;
    tid_rx->baw_head = tid_rx->baw_tail = 0;
    tid_rx->sq_exceed_count = tid_rx->sq_behind_count = 0;
    zmw_leave_critical_section(dev);

    return 0;
}

u16_t   zfAggRecvAddbaResponse(zdev_t* dev, zbuf_t* buf)
{
    u16_t i,ac, aid=0;
    u16_t src[3];
    struct aggBaFrameParameter bf;

    zmw_get_wlan_dev(dev);

    //zmw_declare_for_critical_section();

    src[0] = zmw_rx_buf_readh(dev, buf, 10);
    src[1] = zmw_rx_buf_readh(dev, buf, 12);
    src[2] = zmw_rx_buf_readh(dev, buf, 14);

    if (wd->wlanMode == ZM_MODE_AP)
        aid = zfApFindSta(dev, src);


    bf.buf = buf;
    bf.dialog = zmw_rx_buf_readb(dev, buf, 26);
    bf.status_code = zmw_rx_buf_readh(dev, buf, 27);
    if (!bf.status_code)
    {
        wd->addbaComplete=1;
    }

    /*
     * ba parameter set
     */
    bf.ba_parameter = zmw_rx_buf_readh(dev, buf, 29);
    bf.ba_policy   = (bf.ba_parameter >> 1) & 1;
    bf.tid         = (bf.ba_parameter >> 2) & 0xF;
    bf.buffer_size = (bf.ba_parameter >> 6);
    /*
     * BA timeout value
     */
    bf.ba_timeout = zmw_rx_buf_readh(dev, buf, 31);

    i=26;
    while(i < 32) {
        zm_debug_msg2("Recv ADDBA Rsp:", zmw_rx_buf_readb(dev,buf,i));
        i++;
    }

    ac = zcUpToAc[bf.tid&0x7] & 0x3;

    //zmw_enter_critical_section(dev);

    //wd->aggSta[aid].aggFlag[ac] = 0;

    //zmw_leave_critical_section(dev);

    return ZM_SUCCESS;
}

u16_t   zfAggRecvDelba(zdev_t* dev, zbuf_t* buf)
{
    //zmw_get_wlan_dev(dev);

    //zmw_declare_for_critical_section();
    return ZM_SUCCESS;
}

u16_t   zfAggSendAddbaResponse(zdev_t* dev, struct aggBaFrameParameter *bf)
{
    zbuf_t* buf;
    //u16_t addrTblSize;
    //struct zsAddrTbl addrTbl;
    //u16_t err;
    u16_t offset = 0;
    u16_t hlen = 32;
    u16_t header[(24+25+1)/2];
    u16_t vap = 0;
    u16_t i;
    u8_t encrypt = 0;
    u16_t dst[3];

    //zmw_get_wlan_dev(dev);

    //zmw_declare_for_critical_section();


    /*
     * TBD : Maximum size of managment frame
     */
    if ((buf = zfwBufAllocate(dev, 1024)) == NULL)
    {
        zm_msg0_mm(ZM_LV_0, "Alloc mm buf Fail!");
        return ZM_SUCCESS;
    }

    /*
     * Reserve room for wlan header
     */
    offset = hlen;

    /*
     * add addba frame body
     */
    offset = zfAggSetAddbaResponseFrameBody(dev, buf, bf, offset);


    zfwBufSetSize(dev, buf, offset);

    /*
     * Copy wlan header
     */

    dst[0] = zmw_rx_buf_readh(dev, bf->buf, 10);
    dst[1] = zmw_rx_buf_readh(dev, bf->buf, 12);
    dst[2] = zmw_rx_buf_readh(dev, bf->buf, 14);
    zfAggGenAddbaHeader(dev, dst, header, offset-hlen, buf, vap, encrypt);
    for (i=0; i<(hlen>>1); i++)
    {
        zmw_tx_buf_writeh(dev, buf, i*2, header[i]);
    }

    /* Get buffer DMA address */
    //if ((addrTblSize = zfwBufMapDma(dev, buf, &addrTbl)) == 0)
    //if ((addrTblSize = zfwMapTxDma(dev, buf, &addrTbl)) == 0)
    //{
    //    goto zlError;
    //}

    //zm_msg2_mm(ZM_LV_2, "offset=", offset);
    //zm_msg2_mm(ZM_LV_2, "hlen=", hlen);
    //zm_msg2_mm(ZM_LV_2, "addrTblSize=", addrTblSize);
    //zm_msg2_mm(ZM_LV_2, "addrTbl.len[0]=", addrTbl.len[0]);
    //zm_msg2_mm(ZM_LV_2, "addrTbl.physAddrl[0]=", addrTbl.physAddrl[0]);
    //zm_msg2_mm(ZM_LV_2, "buf->data=", buf->data);

    #if 0
    if ((err = zfHpSend(dev, NULL, 0, NULL, 0, NULL, 0, buf, 0,
            ZM_INTERNAL_ALLOC_BUF, 0, 0xff)) != ZM_SUCCESS)
    {
        goto zlError;
    }
    #else
    zfPutVmmq(dev, buf);
    zfPushVtxq(dev);
    #endif

    //zfAggSendAddbaRequest(dev, dst, zcUpToAc[bf->tid&0x7] & 0x3, bf->tid);
    return ZM_SUCCESS;

}

u16_t   zfAggSetAddbaResponseFrameBody(zdev_t* dev, zbuf_t* buf,
                struct aggBaFrameParameter *bf, u16_t offset)
{

    //zmw_get_wlan_dev(dev);

    //zmw_declare_for_critical_section();
    /*
     * ADDBA Request frame body
     */

    /*
     * Category
     */
    zmw_tx_buf_writeb(dev, buf, offset++, 3);
    /*
     * Action details = 0
     */
    zmw_tx_buf_writeb(dev, buf, offset++, ZM_WLAN_ADDBA_RESPONSE_FRAME);
    /*
     * Dialog Token = nonzero
     */
    zmw_tx_buf_writeb(dev, buf, offset++, bf->dialog);
    /*
     * Status code
     */
    zmw_tx_buf_writeh(dev, buf, offset, 0);
    offset+=2;
    /*
     * Block Ack parameter set
     * BA policy = 1 for immediate BA, 0 for delayed BA
     * TID(4bits) & buffer size(4bits) (TID=0x1 & buffer size=0x80)
     * TBD: how to get TID number and buffer size?
     * ¢z¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢s¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢s¢w¢w¢w¢w¢w¢w¢w¢w¢s¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢{
     * ¢x    B0    ¢x    B1     ¢x B2  B5 ¢x B6      B15 ¢x
     * ¢u¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢q¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢q¢w¢w¢w¢w¢w¢w¢w¢w¢q¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢t
     * ¢x Reserved ¢x BA policy ¢x  TID   ¢x Buffer size ¢x
     * ¢|¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢r¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢r¢w¢w¢w¢w¢w¢w¢w¢w¢r¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢}
     */
    zmw_tx_buf_writeh(dev, buf, offset, bf->ba_parameter);
    offset+=2;
    /*
     * BA timeout value
     */
    zmw_tx_buf_writeh(dev, buf, offset, bf->ba_timeout);
    offset+=2;

    return offset;
}

void   zfAggInvokeBar(zdev_t* dev, TID_TX tid_tx)
{
    struct aggBarControl aggBarControl;
    //zmw_get_wlan_dev(dev);

    //zmw_declare_for_critical_section();
    //bar_control = aggBarControl->tid_info << 12 | aggBarControl->compressed_bitmap << 2
    //        | aggBarControl->multi_tid << 1 | aggBarControl->bar_ack_policy;
    aggBarControl.bar_ack_policy = 0;
    aggBarControl.multi_tid = 0;
    aggBarControl.compressed_bitmap = 0;
    aggBarControl.tid_info = tid_tx->tid;
    zfAggSendBar(dev, tid_tx, &aggBarControl);

    return;

}
/*
 * zfAggSendBar() refers zfAggSendAddbaRequest()
 */
u16_t   zfAggSendBar(zdev_t* dev, TID_TX tid_tx, struct aggBarControl *aggBarControl)
{
    zbuf_t* buf;
    //u16_t addrTblSize;
    //struct zsAddrTbl addrTbl;
    //u16_t err;
    u16_t offset = 0;
    u16_t hlen = 16+8;  /* mac header + control headers*/
    u16_t header[(8+24+1)/2];
    u16_t vap = 0;
    u16_t i;
    u8_t encrypt = 0;

    //zmw_get_wlan_dev(dev);

    //zmw_declare_for_critical_section();


    /*
     * TBD : Maximum size of managment frame
     */
    if ((buf = zfwBufAllocate(dev, 1024)) == NULL)
    {
        zm_msg0_mm(ZM_LV_0, "Alloc mm buf Fail!");
        return ZM_SUCCESS;
    }

    /*
     * Reserve room for wlan header
     */
    offset = hlen;

    /*
     * add addba frame body
     */
    offset = zfAggSetBarBody(dev, buf, offset, tid_tx, aggBarControl);


    zfwBufSetSize(dev, buf, offset);

    /*
     * Copy wlan header
     */
    zfAggGenBarHeader(dev, tid_tx->dst, header, offset-hlen, buf, vap, encrypt);
    for (i=0; i<(hlen>>1); i++)
    {
        zmw_tx_buf_writeh(dev, buf, i*2, header[i]);
    }

    /* Get buffer DMA address */
    //if ((addrTblSize = zfwBufMapDma(dev, buf, &addrTbl)) == 0)
    //if ((addrTblSize = zfwMapTxDma(dev, buf, &addrTbl)) == 0)
    //{
    //    goto zlError;
    //}

    //zm_msg2_mm(ZM_LV_2, "offset=", offset);
    //zm_msg2_mm(ZM_LV_2, "hlen=", hlen);
    //zm_msg2_mm(ZM_LV_2, "addrTblSize=", addrTblSize);
    //zm_msg2_mm(ZM_LV_2, "addrTbl.len[0]=", addrTbl.len[0]);
    //zm_msg2_mm(ZM_LV_2, "addrTbl.physAddrl[0]=", addrTbl.physAddrl[0]);
    //zm_msg2_mm(ZM_LV_2, "buf->data=", buf->data);

    #if 0
    if ((err = zfHpSend(dev, NULL, 0, NULL, 0, NULL, 0, buf, 0,
            ZM_INTERNAL_ALLOC_BUF, 0, 0xff)) != ZM_SUCCESS)
    {
        goto zlError;
    }
    #else
    zfPutVmmq(dev, buf);
    zfPushVtxq(dev);
    #endif

    return ZM_SUCCESS;

}

u16_t   zfAggSetBarBody(zdev_t* dev, zbuf_t* buf, u16_t offset, TID_TX tid_tx, struct aggBarControl *aggBarControl)
{
    u16_t bar_control, start_seq;

    //zmw_get_wlan_dev(dev);

    //zmw_declare_for_critical_section();
    /*
     * BAR Control frame body
     */

    /*
     * BAR Control Field
     * ¢z¢w¢w¢w¢w¢w¢w¢w¢w¢w¢s¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢s¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢s¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢s¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢{
     * ¢x    B0   ¢x    B1     ¢x     B2     ¢x B3   B11 ¢x B12  B15 ¢x
     * ¢u¢w¢w¢w¢w¢w¢w¢w¢w¢w¢q¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢q¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢q¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢q¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢t
     * ¢x BAR Ack ¢x Multi-TID ¢x Compressed ¢x Reserved ¢x TID_INFO ¢x
     * ¢x  Policy ¢x           ¢x   Bitmap   ¢x          ¢x          ¢x
     * ¢|¢w¢w¢w¢w¢w¢w¢w¢w¢w¢r¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢r¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢r¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢r¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢}
     */
    bar_control = aggBarControl->tid_info << 12 | aggBarControl->compressed_bitmap << 2
            | aggBarControl->multi_tid << 1 | aggBarControl->bar_ack_policy;

    zmw_tx_buf_writeh(dev, buf, offset, bar_control);
    offset+=2;
    if (0 == aggBarControl->multi_tid) {
        /*
         * BA starting sequence number
         * ¢z¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢s¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢{
         * ¢x B0       B3 ¢x B4              B15 ¢x
         * ¢u¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢q¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢t
         * ¢x Frag num(0) ¢x BA starting seq num ¢x
         * ¢|¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢r¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢}
         */
        start_seq = (tid_tx->bar_ssn << 4) & 0xFFF0;
        zmw_tx_buf_writeh(dev, buf, offset, start_seq);
        offset+=2;
    }
    if (1 == aggBarControl->multi_tid && 1 == aggBarControl->compressed_bitmap) {
        /* multi-tid BlockAckReq variant, not implemented*/
    }

    return offset;
}

u16_t zfAggGenBarHeader(zdev_t* dev, u16_t* dst,
        u16_t* header, u16_t len, zbuf_t* buf, u16_t vap, u8_t encrypt)
{
    u8_t  hlen = 16+8;        // MAC ctrl + PHY ctrl + 802.11 MM header
    //u8_t frameType = ZM_WLAN_FRAME_TYPE_ACTION;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    /*
     * Generate control setting
     */
    //bodyLen = zfwBufGetSize(dev, buf);
    header[0] = 16+len+4;   //Length
    header[1] = 0x8;        //MAC control, backoff + (ack)

#if 1
    /* CCK 1M */
    header[2] = 0x0f00;          //PHY control L
    header[3] = 0x0000;          //PHY control H
#else
    /* CCK 6M */
    header[2] = 0x0f01;          //PHY control L
    header[3] = 0x000B;          //PHY control H

#endif
    /*
     * Generate WLAN header
     * Frame control frame type and subtype
     */
    header[4+0] = ZM_WLAN_FRAME_TYPE_BAR;
    /*
     * Duration
     */
    header[4+1] = 0;

    /* Address 1 = DA */
    header[4+2] = dst[0];
    header[4+3] = dst[1];
    header[4+4] = dst[2];

    /* Address 2 = SA */
    header[4+5] = wd->macAddr[0];
    header[4+6] = wd->macAddr[1];
    if (wd->wlanMode == ZM_MODE_AP)
    {
#ifdef ZM_VAPMODE_MULTILE_SSID
        header[4+7] = wd->macAddr[2]; //Multiple SSID
#else
        header[4+7] = wd->macAddr[2] + (vap<<8); //VAP
#endif
    }
    else
    {
        header[4+7] = wd->macAddr[2];
    }

    /* Sequence Control */
    zmw_enter_critical_section(dev);
    header[4+11] = ((wd->mmseq++)<<4);
    zmw_leave_critical_section(dev);


    return hlen;
}
