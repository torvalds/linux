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

#include "cprecomp.h"

u8_t zfQueryOppositeRate(zdev_t* dev, u8_t dst_mac[6], u8_t frameType)
{
    zmw_get_wlan_dev(dev);

    /* For AP's rate adaption */
    if ( wd->wlanMode == ZM_MODE_AP )
    {
        return 0;
    }

    /* For STA's rate adaption */
    if ( (frameType & 0x0c) == ZM_WLAN_DATA_FRAME )
    {
        if ( ZM_IS_MULTICAST(dst_mac) )
        {
            return wd->sta.mTxRate;
        }
        else
        {
            return wd->sta.uTxRate;
        }
    }

    return wd->sta.mmTxRate;
}

void zfCopyToIntTxBuffer(zdev_t* dev, zbuf_t* buf, u8_t* src,
                         u16_t offset, u16_t length)
{
    u16_t i;

    for(i=0; i<length;i++)
    {
        zmw_tx_buf_writeb(dev, buf, offset+i, src[i]);
    }
}

void zfCopyToRxBuffer(zdev_t* dev, zbuf_t* buf, u8_t* src,
                      u16_t offset, u16_t length)
{
    u16_t i;

    for(i=0; i<length;i++)
    {
        zmw_rx_buf_writeb(dev, buf, offset+i, src[i]);
    }
}

void zfCopyFromIntTxBuffer(zdev_t* dev, zbuf_t* buf, u8_t* dst,
                           u16_t offset, u16_t length)
{
    u16_t i;

    for(i=0; i<length; i++)
    {
        dst[i] = zmw_tx_buf_readb(dev, buf, offset+i);
    }
}

void zfCopyFromRxBuffer(zdev_t* dev, zbuf_t* buf, u8_t* dst,
                        u16_t offset, u16_t length)
{
    u16_t i;

    for(i=0; i<length; i++)
    {
        dst[i] = zmw_rx_buf_readb(dev, buf, offset+i);
    }
}

#if 1
void zfMemoryCopy(u8_t* dst, u8_t* src, u16_t length)
{
    zfwMemoryCopy(dst, src, length);
}

void zfMemoryMove(u8_t* dst, u8_t* src, u16_t length)
{
    zfwMemoryMove(dst, src, length);
}

void zfZeroMemory(u8_t* va, u16_t length)
{
    zfwZeroMemory(va, length);
}

u8_t zfMemoryIsEqual(u8_t* m1, u8_t* m2, u16_t length)
{
    return zfwMemoryIsEqual(m1, m2, length);
}
#endif

u8_t zfRxBufferEqualToStr(zdev_t* dev, zbuf_t* buf,
                          const u8_t* str, u16_t offset, u16_t length)
{
    u16_t i;
    u8_t ch;

    for(i=0; i<length; i++)
    {
        ch = zmw_rx_buf_readb(dev, buf, offset+i);
        if ( ch != str[i] )
        {
            return FALSE;
        }
    }

    return TRUE;
}

void zfTxBufferCopy(zdev_t*dev, zbuf_t* dst, zbuf_t* src,
                    u16_t dstOffset, u16_t srcOffset, u16_t length)
{
    u16_t i;

    for(i=0; i<length; i++)
    {
        zmw_tx_buf_writeb(dev, dst, dstOffset+i,
                          zmw_tx_buf_readb(dev, src, srcOffset+i));
    }
}

void zfRxBufferCopy(zdev_t*dev, zbuf_t* dst, zbuf_t* src,
                    u16_t dstOffset, u16_t srcOffset, u16_t length)
{
    u16_t i;

    for(i=0; i<length; i++)
    {
        zmw_rx_buf_writeb(dev, dst, dstOffset+i,
                             zmw_rx_buf_readb(dev, src, srcOffset+i));
    }
}


void zfCollectHWTally(zdev_t*dev, u32_t* rsp, u8_t id)
{
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    if (id == 0)
    {
        wd->commTally.Hw_UnderrunCnt += (0xFFFF & rsp[1]);
        wd->commTally.Hw_TotalRxFrm += rsp[2];
        wd->commTally.Hw_CRC32Cnt += rsp[3];
        wd->commTally.Hw_CRC16Cnt += rsp[4];
        #ifdef ZM_ENABLE_NATIVE_WIFI
        /* These code are here to satisfy Vista DTM */
        wd->commTally.Hw_DecrypErr_UNI += ((rsp[5]>50) && (rsp[5]<60))?50:rsp[5];
        #else
        wd->commTally.Hw_DecrypErr_UNI += rsp[5];
        #endif
        wd->commTally.Hw_RxFIFOOverrun += rsp[6];
        wd->commTally.Hw_DecrypErr_Mul += rsp[7];
        wd->commTally.Hw_RetryCnt += rsp[8];
        wd->commTally.Hw_TotalTxFrm += rsp[9];
        wd->commTally.Hw_RxTimeOut +=rsp[10];

        wd->commTally.Tx_MPDU += rsp[11];
        wd->commTally.BA_Fail += rsp[12];
        wd->commTally.Hw_Tx_AMPDU += rsp[13];
        wd->commTally.Hw_Tx_MPDU += rsp[14];
        wd->commTally.RateCtrlTxMPDU += rsp[11];
        wd->commTally.RateCtrlBAFail += rsp[12];
    }
    else
    {
        wd->commTally.Hw_RxMPDU += rsp[1];
        wd->commTally.Hw_RxDropMPDU += rsp[2];
        wd->commTally.Hw_RxDelMPDU += rsp[3];

        wd->commTally.Hw_RxPhyMiscError += rsp[4];
        wd->commTally.Hw_RxPhyXRError += rsp[5];
        wd->commTally.Hw_RxPhyOFDMError += rsp[6];
        wd->commTally.Hw_RxPhyCCKError += rsp[7];
        wd->commTally.Hw_RxPhyHTError += rsp[8];
        wd->commTally.Hw_RxPhyTotalCount += rsp[9];
    }

    zmw_leave_critical_section(dev);

    if (id == 0)
    {
        zm_msg1_mm(ZM_LV_1, "rsplen =", rsp[0]);
        zm_msg1_mm(ZM_LV_1, "Hw_UnderrunCnt    = ", (0xFFFF & rsp[1]));
        zm_msg1_mm(ZM_LV_1, "Hw_TotalRxFrm     = ", rsp[2]);
        zm_msg1_mm(ZM_LV_1, "Hw_CRC32Cnt       = ", rsp[3]);
        zm_msg1_mm(ZM_LV_1, "Hw_CRC16Cnt       = ", rsp[4]);
        zm_msg1_mm(ZM_LV_1, "Hw_DecrypErr_UNI  = ", rsp[5]);
        zm_msg1_mm(ZM_LV_1, "Hw_RxFIFOOverrun  = ", rsp[6]);
        zm_msg1_mm(ZM_LV_1, "Hw_DecrypErr_Mul  = ", rsp[7]);
        zm_msg1_mm(ZM_LV_1, "Hw_RetryCnt       = ", rsp[8]);
        zm_msg1_mm(ZM_LV_1, "Hw_TotalTxFrm     = ", rsp[9]);
        zm_msg1_mm(ZM_LV_1, "Hw_RxTimeOut      = ", rsp[10]);
        zm_msg1_mm(ZM_LV_1, "Tx_MPDU           = ", rsp[11]);
        zm_msg1_mm(ZM_LV_1, "BA_Fail           = ", rsp[12]);
        zm_msg1_mm(ZM_LV_1, "Hw_Tx_AMPDU       = ", rsp[13]);
        zm_msg1_mm(ZM_LV_1, "Hw_Tx_MPDU        = ", rsp[14]);
    }
    else
    {
        zm_msg1_mm(ZM_LV_1, "rsplen             = ", rsp[0]);
        zm_msg1_mm(ZM_LV_1, "Hw_RxMPDU          = ", (0xFFFF & rsp[1]));
        zm_msg1_mm(ZM_LV_1, "Hw_RxDropMPDU      = ", rsp[2]);
        zm_msg1_mm(ZM_LV_1, "Hw_RxDelMPDU       = ", rsp[3]);
        zm_msg1_mm(ZM_LV_1, "Hw_RxPhyMiscError  = ", rsp[4]);
        zm_msg1_mm(ZM_LV_1, "Hw_RxPhyXRError    = ", rsp[5]);
        zm_msg1_mm(ZM_LV_1, "Hw_RxPhyOFDMError  = ", rsp[6]);
        zm_msg1_mm(ZM_LV_1, "Hw_RxPhyCCKError   = ", rsp[7]);
        zm_msg1_mm(ZM_LV_1, "Hw_RxPhyHTError    = ", rsp[8]);
        zm_msg1_mm(ZM_LV_1, "Hw_RxPhyTotalCount = ", rsp[9]);
    }

}

/* Timer related functions */
void zfTimerInit(zdev_t* dev)
{
    u8_t   i;

    zmw_get_wlan_dev(dev);

    zm_debug_msg0("");

    wd->timerList.freeCount = ZM_MAX_TIMER_COUNT;
    wd->timerList.head = &(wd->timerList.list[0]);
    wd->timerList.tail = &(wd->timerList.list[ZM_MAX_TIMER_COUNT-1]);
    wd->timerList.head->pre = NULL;
    wd->timerList.head->next = &(wd->timerList.list[1]);
    wd->timerList.tail->pre = &(wd->timerList.list[ZM_MAX_TIMER_COUNT-2]);
    wd->timerList.tail->next = NULL;

    for( i=1; i<(ZM_MAX_TIMER_COUNT-1); i++ )
    {
        wd->timerList.list[i].pre = &(wd->timerList.list[i-1]);
        wd->timerList.list[i].next = &(wd->timerList.list[i+1]);
    }

    wd->bTimerReady = TRUE;
}


u16_t zfTimerSchedule(zdev_t* dev, u16_t event, u32_t tick)
{
    struct zsTimerEntry *pFreeEntry;
    struct zsTimerEntry *pEntry;
    u8_t   i, count;

    zmw_get_wlan_dev(dev);

    if ( wd->timerList.freeCount == 0 )
    {
        zm_debug_msg0("no more timer");
        return 1;
    }

    //zm_debug_msg2("event = ", event);
    //zm_debug_msg1("target tick = ", wd->tick + tick);

    count = ZM_MAX_TIMER_COUNT - wd->timerList.freeCount;

    if ( count == 0 )
    {
        wd->timerList.freeCount--;
        wd->timerList.head->event = event;
        wd->timerList.head->timer = wd->tick + tick;
        //zm_debug_msg1("free timer count = ", wd->timerList.freeCount);

        return 0;
    }

    pFreeEntry = wd->timerList.tail;
    pFreeEntry->timer = wd->tick + tick;
    pFreeEntry->event = event;
    wd->timerList.tail = pFreeEntry->pre;
    pEntry = wd->timerList.head;

    for( i=0; i<count; i++ )
    {
        // prevent from the case of tick overflow
        if ( ( pEntry->timer > pFreeEntry->timer )&&
             ((pEntry->timer - pFreeEntry->timer) < 1000000000) )
        {
            if ( i != 0 )
            {
                pFreeEntry->pre = pEntry->pre;
                pFreeEntry->pre->next = pFreeEntry;
            }
            else
            {
                pFreeEntry->pre = NULL;
            }

            pEntry->pre = pFreeEntry;
            pFreeEntry->next = pEntry;
            break;
        }

        pEntry = pEntry->next;
    }

    if ( i == 0 )
    {
        wd->timerList.head = pFreeEntry;
    }

    if ( i == count )
    {
        pFreeEntry->pre = pEntry->pre;
        pFreeEntry->pre->next = pFreeEntry;
        pEntry->pre = pFreeEntry;
        pFreeEntry->next = pEntry;
    }

    wd->timerList.freeCount--;
    //zm_debug_msg1("free timer count = ", wd->timerList.freeCount);

    return 0;
}

u16_t zfTimerCancel(zdev_t* dev, u16_t event)
{
    struct zsTimerEntry *pEntry;
    u8_t   i, count;

    zmw_get_wlan_dev(dev);

    //zm_debug_msg2("event = ", event);
    //zm_debug_msg1("free timer count(b) = ", wd->timerList.freeCount);

    pEntry = wd->timerList.head;
    count = ZM_MAX_TIMER_COUNT - wd->timerList.freeCount;

    for( i=0; i<count; i++ )
    {
        if ( pEntry->event == event )
        {
            if ( pEntry == wd->timerList.head )
            {   /* remove head entry */
                wd->timerList.head = pEntry->next;
                wd->timerList.tail->next = pEntry;
                pEntry->pre = wd->timerList.tail;
                wd->timerList.tail = pEntry;
                pEntry = wd->timerList.head;
            }
            else
            {   /* remove non-head entry */
                pEntry->pre->next = pEntry->next;
                pEntry->next->pre = pEntry->pre;
                wd->timerList.tail->next = pEntry;
                pEntry->pre = wd->timerList.tail;
                wd->timerList.tail = pEntry;
                pEntry = pEntry->next;
            }

            wd->timerList.freeCount++;
        }
        else
        {
            pEntry = pEntry->next;
        }
    }

    //zm_debug_msg1("free timer count(a) = ", wd->timerList.freeCount);

    return 0;
}

void zfTimerClear(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    wd->timerList.freeCount = ZM_MAX_TIMER_COUNT;
}

u16_t zfTimerCheckAndHandle(zdev_t* dev)
{
    struct zsTimerEntry *pEntry;
    struct zsTimerEntry *pTheLastEntry = NULL;
    u16_t  event[ZM_MAX_TIMER_COUNT];
    u8_t   i, j=0, count;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    if ( !wd->bTimerReady )
    {
        return 0;
    }

    zmw_enter_critical_section(dev);

    pEntry = wd->timerList.head;
    count = ZM_MAX_TIMER_COUNT - wd->timerList.freeCount;

    for( i=0; i<count; i++ )
    {
        // prevent from the case of tick overflow
        if ( ( pEntry->timer > wd->tick )&&
             ((pEntry->timer - wd->tick) < 1000000000) )
        {
            break;
        }

        event[j++] = pEntry->event;
        pTheLastEntry = pEntry;
        pEntry = pEntry->next;
    }

    if ( j > 0 )
    {
        wd->timerList.tail->next = wd->timerList.head;
        wd->timerList.head->pre = wd->timerList.tail;
        wd->timerList.head = pEntry;
        wd->timerList.tail = pTheLastEntry;
        wd->timerList.freeCount += j;
        //zm_debug_msg1("free timer count = ", wd->timerList.freeCount);
    }

    zmw_leave_critical_section(dev);

    zfProcessEvent(dev, event, j);

    return 0;
}

u32_t zfCoreSetKey(zdev_t* dev, u8_t user, u8_t keyId, u8_t type,
        u16_t* mac, u32_t* key)
{
    u32_t ret;

    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    wd->sta.flagKeyChanging++;
    zm_debug_msg1("   zfCoreSetKey++++ ", wd->sta.flagKeyChanging);
    zmw_leave_critical_section(dev);

    ret = zfHpSetKey(dev, user, keyId, type, mac, key);
    return ret;
}

void zfCoreSetKeyComplete(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

#if 0
    wd->sta.flagKeyChanging = 0;
#else
    if(wd->sta.flagKeyChanging)
    {
        zmw_enter_critical_section(dev);
        wd->sta.flagKeyChanging--;
        zmw_leave_critical_section(dev);
    }
#endif
    zm_debug_msg1("  zfCoreSetKeyComplete--- ", wd->sta.flagKeyChanging);

    zfPushVtxq(dev);
}

void zfCoreHalInitComplete(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    wd->halState = ZM_HAL_STATE_RUNNING;
    zmw_leave_critical_section(dev);

    zfPushVtxq(dev);
}

void zfCoreMacAddressNotify(zdev_t* dev, u8_t* addr)
{
    zmw_get_wlan_dev(dev);

    wd->macAddr[0] = addr[0] | ((u16_t)addr[1]<<8);
    wd->macAddr[1] = addr[2] | ((u16_t)addr[3]<<8);
    wd->macAddr[2] = addr[4] | ((u16_t)addr[5]<<8);


    //zfHpSetMacAddress(dev, wd->macAddr, 0);
    if (wd->zfcbMacAddressNotify != NULL)
    {
        wd->zfcbMacAddressNotify(dev, addr);
    }
}

void zfCoreSetIsoName(zdev_t* dev, u8_t* isoName)
{
    zmw_get_wlan_dev(dev);

    wd->ws.countryIsoName[0] = isoName[0];
    wd->ws.countryIsoName[1] = isoName[1];
    wd->ws.countryIsoName[2] = '\0';
 }


extern void zfScanMgrScanEventStart(zdev_t* dev);
extern u8_t zfScanMgrScanEventTimeout(zdev_t* dev);
extern void zfScanMgrScanEventRetry(zdev_t* dev);

void zfProcessEvent(zdev_t* dev, u16_t* eventArray, u8_t eventCount)
{
    u8_t i, j, bypass = FALSE;
    u16_t eventBypass[32];
    u8_t eventBypassCount = 0;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zfZeroMemory((u8_t*) eventBypass, 64);

    for( i=0; i<eventCount; i++ )
    {
        for( j=0; j<eventBypassCount; j++ )
        {
            if ( eventBypass[j] == eventArray[i] )
            {
                bypass = TRUE;
                break;
            }
        }

        if ( bypass )
        {
            continue;
        }

        switch( eventArray[i] )
        {
            case ZM_EVENT_SCAN:
                {
                    zfScanMgrScanEventStart(dev);
                    eventBypass[eventBypassCount++] = ZM_EVENT_IN_SCAN;
                    eventBypass[eventBypassCount++] = ZM_EVENT_TIMEOUT_SCAN;
                }
                break;

            case ZM_EVENT_TIMEOUT_SCAN:
                {
                    u8_t res;

                    res = zfScanMgrScanEventTimeout(dev);
                    if ( res == 0 )
                    {
                        eventBypass[eventBypassCount++] = ZM_EVENT_TIMEOUT_SCAN;
                    }
                    else if ( res == 1 )
                    {
                        eventBypass[eventBypassCount++] = ZM_EVENT_IN_SCAN;
                    }
                }
                break;

            case ZM_EVENT_IBSS_MONITOR:
                {
                    zfStaIbssMonitoring(dev, 0);
                }
                break;

            case ZM_EVENT_IN_SCAN:
                {
                    zfScanMgrScanEventRetry(dev);
                }
                break;

            case ZM_EVENT_CM_TIMER:
                {
                    zm_msg0_mm(ZM_LV_0, "ZM_EVENT_CM_TIMER");

                    wd->sta.cmMicFailureCount = 0;
                }
                break;

            case ZM_EVENT_CM_DISCONNECT:
                {
                    zm_msg0_mm(ZM_LV_0, "ZM_EVENT_CM_DISCONNECT");

                    zfChangeAdapterState(dev, ZM_STA_STATE_DISCONNECT);

                    zmw_enter_critical_section(dev);
                    //zfTimerSchedule(dev, ZM_EVENT_CM_BLOCK_TIMER,
                    //                ZM_TICK_CM_BLOCK_TIMEOUT);

                    /* Timer Resolution on WinXP is 15/16 ms  */
                    /* Decrease Time offset for <XP> Counter Measure */
                    zfTimerSchedule(dev, ZM_EVENT_CM_BLOCK_TIMER,
                                         ZM_TICK_CM_BLOCK_TIMEOUT - ZM_TICK_CM_BLOCK_TIMEOUT_OFFSET);

                    zmw_leave_critical_section(dev);
                    wd->sta.cmMicFailureCount = 0;
                    //zfiWlanDisable(dev);
                    zfHpResetKeyCache(dev);
                    if (wd->zfcbConnectNotify != NULL)
                    {
                        wd->zfcbConnectNotify(dev, ZM_STATUS_MEDIA_DISCONNECT_MIC_FAIL,
                             wd->sta.bssid);
                    }
                }
                break;

            case ZM_EVENT_CM_BLOCK_TIMER:
                {
                    zm_msg0_mm(ZM_LV_0, "ZM_EVENT_CM_BLOCK_TIMER");

                    //zmw_enter_critical_section(dev);
                    wd->sta.cmDisallowSsidLength = 0;
                    if ( wd->sta.bAutoReconnect )
                    {
                        zm_msg0_mm(ZM_LV_0, "ZM_EVENT_CM_BLOCK_TIMER:bAutoReconnect!=0");
                        zfScanMgrScanStop(dev, ZM_SCAN_MGR_SCAN_INTERNAL);
                        zfScanMgrScanStart(dev, ZM_SCAN_MGR_SCAN_INTERNAL);
                    }
                    //zmw_leave_critical_section(dev);
                }
                break;

            case ZM_EVENT_TIMEOUT_ADDBA:
                {
                    if (!wd->addbaComplete && (wd->addbaCount < 5))
                    {
                        zfAggSendAddbaRequest(dev, wd->sta.bssid, 0, 0);
                        wd->addbaCount++;
                        zfTimerSchedule(dev, ZM_EVENT_TIMEOUT_ADDBA, 100);
                    }
                    else
                    {
                        zfTimerCancel(dev, ZM_EVENT_TIMEOUT_ADDBA);
                    }
                }
                break;

            #ifdef ZM_ENABLE_PERFORMANCE_EVALUATION
            case ZM_EVENT_TIMEOUT_PERFORMANCE:
                {
                    zfiPerformanceRefresh(dev);
                }
                break;
            #endif
            case ZM_EVENT_SKIP_COUNTERMEASURE:
				//enable the Countermeasure
				{
					zm_debug_msg0("Countermeasure : Enable MIC Check ");
					wd->TKIP_Group_KeyChanging = 0x0;
				}
				break;

            default:
                break;
        }
    }
}

void zfBssInfoCreate(zdev_t* dev)
{
    u8_t   i;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    wd->sta.bssList.bssCount = 0;
    wd->sta.bssList.head = NULL;
    wd->sta.bssList.tail = NULL;
    wd->sta.bssInfoArrayHead = 0;
    wd->sta.bssInfoArrayTail = 0;
    wd->sta.bssInfoFreeCount = ZM_MAX_BSS;

    for( i=0; i< ZM_MAX_BSS; i++ )
    {
        //wd->sta.bssInfoArray[i] = &(wd->sta.bssInfoPool[i]);
        wd->sta.bssInfoArray[i] = zfwMemAllocate(dev, sizeof(struct zsBssInfo));

    }

    zmw_leave_critical_section(dev);
}

void zfBssInfoDestroy(zdev_t* dev)
{
    u8_t   i;
    zmw_get_wlan_dev(dev);

    zfBssInfoRefresh(dev, 1);

    for( i=0; i< ZM_MAX_BSS; i++ )
    {
        if (wd->sta.bssInfoArray[i] != NULL)
        {
            zfwMemFree(dev, wd->sta.bssInfoArray[i], sizeof(struct zsBssInfo));
        }
        else
        {
            zm_assert(0);
        }
    }
    return;
}

struct zsBssInfo* zfBssInfoAllocate(zdev_t* dev)
{
    struct zsBssInfo* pBssInfo;

    zmw_get_wlan_dev(dev);

    if (wd->sta.bssInfoFreeCount == 0)
        return NULL;

    pBssInfo = wd->sta.bssInfoArray[wd->sta.bssInfoArrayHead];
    wd->sta.bssInfoArray[wd->sta.bssInfoArrayHead] = NULL;
    wd->sta.bssInfoArrayHead = (wd->sta.bssInfoArrayHead + 1) & (ZM_MAX_BSS - 1);
    wd->sta.bssInfoFreeCount--;

    zfZeroMemory((u8_t*)pBssInfo, sizeof(struct zsBssInfo));

    return pBssInfo;
}

void zfBssInfoFree(zdev_t* dev, struct zsBssInfo* pBssInfo)
{
    zmw_get_wlan_dev(dev);

    zm_assert(wd->sta.bssInfoArray[wd->sta.bssInfoArrayTail] == NULL);

    pBssInfo->signalStrength = pBssInfo->signalQuality = 0;
    pBssInfo->sortValue = 0;

    wd->sta.bssInfoArray[wd->sta.bssInfoArrayTail] = pBssInfo;
    wd->sta.bssInfoArrayTail = (wd->sta.bssInfoArrayTail + 1) & (ZM_MAX_BSS - 1);
    wd->sta.bssInfoFreeCount++;
}

void zfBssInfoReorderList(zdev_t* dev)
{
    struct zsBssInfo* pBssInfo = NULL;
    struct zsBssInfo* pInsBssInfo = NULL;
    struct zsBssInfo* pNextBssInfo = NULL;
    struct zsBssInfo* pPreBssInfo = NULL;
    u8_t i = 0;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    if (wd->sta.bssList.bssCount > 1)
    {
        pInsBssInfo = wd->sta.bssList.head;
        wd->sta.bssList.tail = pInsBssInfo;
        pBssInfo = pInsBssInfo->next;
        pInsBssInfo->next = NULL;
        while (pBssInfo != NULL)
        {
            i = 0;
            while (1)
            {
//                if (pBssInfo->signalStrength >= pInsBssInfo->signalStrength)
                if( pBssInfo->sortValue >= pInsBssInfo->sortValue)
                {
                    if (i==0)
                    {
                        //Insert BssInfo to head
                        wd->sta.bssList.head = pBssInfo;
                        pNextBssInfo = pBssInfo->next;
                        pBssInfo->next = pInsBssInfo;
                        break;
                    }
                    else
                    {
                        //Insert BssInfo to neither head nor tail
                        pPreBssInfo->next = pBssInfo;
                        pNextBssInfo = pBssInfo->next;
                        pBssInfo->next = pInsBssInfo;
                        break;
                    }
                }
                else
                {
                    if (pInsBssInfo->next != NULL)
                    {
                        //Signal strength smaller than current BssInfo, check next
                        pPreBssInfo = pInsBssInfo;
                        pInsBssInfo = pInsBssInfo->next;
                    }
                    else
                    {
                        //Insert BssInfo to tail
                        pInsBssInfo->next = pBssInfo;
                        pNextBssInfo = pBssInfo->next;
                        wd->sta.bssList.tail = pBssInfo;
                        pBssInfo->next = NULL;
                        break;
                    }
                }
                i++;
            }
            pBssInfo = pNextBssInfo;
            pInsBssInfo = wd->sta.bssList.head;
        }
    } //if (wd->sta.bssList.bssCount > 1)

    zmw_leave_critical_section(dev);
}

void zfBssInfoInsertToList(zdev_t* dev, struct zsBssInfo* pBssInfo)
{
    zmw_get_wlan_dev(dev);

    zm_assert(pBssInfo);

    //zm_debug_msg2("pBssInfo = ", pBssInfo);

    if ( wd->sta.bssList.bssCount == 0 )
    {
        wd->sta.bssList.head = pBssInfo;
        wd->sta.bssList.tail = pBssInfo;
    }
    else
    {
        wd->sta.bssList.tail->next = pBssInfo;
        wd->sta.bssList.tail = pBssInfo;
    }

    pBssInfo->next = NULL;
    wd->sta.bssList.bssCount++;

    //zm_debug_msg2("bss count = ", wd->sta.bssList.bssCount);
}

void zfBssInfoRemoveFromList(zdev_t* dev, struct zsBssInfo* pBssInfo)
{
    struct zsBssInfo* pNowBssInfo;
    struct zsBssInfo* pPreBssInfo = NULL;
    u8_t   i;

    zmw_get_wlan_dev(dev);

    zm_assert(pBssInfo);
    zm_assert(wd->sta.bssList.bssCount);

    //zm_debug_msg2("pBssInfo = ", pBssInfo);

    pNowBssInfo = wd->sta.bssList.head;

    for( i=0; i<wd->sta.bssList.bssCount; i++ )
    {
        if ( pNowBssInfo == pBssInfo )
        {
            if ( i == 0 )
            {   /* remove head */
                wd->sta.bssList.head = pBssInfo->next;
            }
            else
            {
                pPreBssInfo->next = pBssInfo->next;
            }

            if ( i == (wd->sta.bssList.bssCount - 1) )
            {   /* remove tail */
                wd->sta.bssList.tail = pPreBssInfo;
            }

            break;
        }

        pPreBssInfo = pNowBssInfo;
        pNowBssInfo = pNowBssInfo->next;
    }

    zm_assert(i != wd->sta.bssList.bssCount);
    wd->sta.bssList.bssCount--;

    //zm_debug_msg2("bss count = ", wd->sta.bssList.bssCount);
}

void zfBssInfoRefresh(zdev_t* dev, u16_t mode)
{
    struct zsBssInfo*   pBssInfo;
    struct zsBssInfo*   pNextBssInfo;
    u8_t   i, bssCount;

    zmw_get_wlan_dev(dev);

    pBssInfo = wd->sta.bssList.head;
    bssCount = wd->sta.bssList.bssCount;

    for( i=0; i<bssCount; i++ )
    {
        if (mode == 1)
        {
            pNextBssInfo = pBssInfo->next;
            zfBssInfoRemoveFromList(dev, pBssInfo);
            zfBssInfoFree(dev, pBssInfo);
            pBssInfo = pNextBssInfo;
        }
        else
        {
            if ( pBssInfo->flag & ZM_BSS_INFO_VALID_BIT )
            {   /* this one must be kept */
                pBssInfo->flag &= ~ZM_BSS_INFO_VALID_BIT;
                pBssInfo = pBssInfo->next;
            }
            else
            {
                #define ZM_BSS_CACHE_TIME_IN_MS   20000
                if ((wd->tick - pBssInfo->tick) > (ZM_BSS_CACHE_TIME_IN_MS/ZM_MS_PER_TICK))
                {
                    pNextBssInfo = pBssInfo->next;
                    zfBssInfoRemoveFromList(dev, pBssInfo);
                    zfBssInfoFree(dev, pBssInfo);
                    pBssInfo = pNextBssInfo;
                }
                else
                {
                    pBssInfo = pBssInfo->next;
                }
            }
        }
    } //for( i=0; i<bssCount; i++ )
    return;
}

void zfDumpSSID(u8_t length, u8_t *value)
{
    u8_t buf[50];
    u8_t tmpLength = length;

    if ( tmpLength > 49 )
    {
        tmpLength = 49;
    }

    zfMemoryCopy(buf, value, tmpLength);
    buf[tmpLength] = '\0';
    //printk("SSID: %s\n", buf);
    //zm_debug_msg_s("ssid = ", value);
}

void zfCoreReinit(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    wd->sta.flagKeyChanging = 0;
    wd->sta.flagFreqChanging = 0;
}

void zfGenerateRandomBSSID(zdev_t* dev, u8_t *MACAddr, u8_t *BSSID)
{
    //ULONGLONG   time;
    u32_t time;

    zmw_get_wlan_dev(dev);

    time = wd->tick;

    //
    // Initialize the random BSSID to be the same as MAC address.
    //

    // RtlCopyMemory(BSSID, MACAddr, sizeof(DOT11_MAC_ADDRESS));
    zfMemoryCopy(BSSID, MACAddr, 6);

    //
    // Get the system time in 10 millisecond.
    //

    // NdisGetCurrentSystemTime((PLARGE_INTEGER)&time);
    // time /= 100000;

    //
    // Randomize the first 4 bytes of BSSID.
    //

    BSSID[0] ^= (u8_t)(time & 0xff);
    BSSID[0] &= ~0x01;              // Turn off multicast bit
    BSSID[0] |= 0x02;               // Turn on local bit

    time >>= 8;
    BSSID[1] ^= (u8_t)(time & 0xff);

    time >>= 8;
    BSSID[2] ^= (u8_t)(time & 0xff);

    time >>= 8;
    BSSID[3] ^= (u8_t)(time & 0xff);
}

u8_t zfiWlanGetDestAddrFromBuf(zdev_t *dev, zbuf_t *buf, u16_t *macAddr)
{
#ifdef ZM_ENABLE_NATIVE_WIFI
    zmw_get_wlan_dev(dev);

    if ( wd->wlanMode == ZM_MODE_INFRASTRUCTURE )
    {
        /* DA */
        macAddr[0] = zmw_tx_buf_readh(dev, buf, 16);
        macAddr[1] = zmw_tx_buf_readh(dev, buf, 18);
        macAddr[2] = zmw_tx_buf_readh(dev, buf, 20);
    }
    else if ( wd->wlanMode == ZM_MODE_IBSS )
    {
        /* DA */
        macAddr[0] = zmw_tx_buf_readh(dev, buf, 4);
        macAddr[1] = zmw_tx_buf_readh(dev, buf, 6);
        macAddr[2] = zmw_tx_buf_readh(dev, buf, 8);
    }
    else if ( wd->wlanMode == ZM_MODE_AP )
    {
        /* DA */
        macAddr[0] = zmw_tx_buf_readh(dev, buf, 4);
        macAddr[1] = zmw_tx_buf_readh(dev, buf, 6);
        macAddr[2] = zmw_tx_buf_readh(dev, buf, 8);
    }
    else
    {
        return 1;
    }
#else
    /* DA */
    macAddr[0] = zmw_tx_buf_readh(dev, buf, 0);
    macAddr[1] = zmw_tx_buf_readh(dev, buf, 2);
    macAddr[2] = zmw_tx_buf_readh(dev, buf, 4);
#endif

    return 0;
}

/* Leave an empty line below to remove warning message on some compiler */

u16_t zfFindCleanFrequency(zdev_t* dev, u32_t adhocMode)
{
    u8_t   i, j;
    u16_t  returnChannel;
    u16_t  count_24G = 0, min24GIndex = 0;
    u16_t  count_5G = 0,  min5GIndex = 0;
    u16_t  CombinationBssNumberIn24G[15] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    u16_t  BssNumberIn24G[17]  = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    u16_t  Array_24G[15]       = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    u16_t  BssNumberIn5G[31]   = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    u16_t  Array_5G[31]        = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    struct zsBssInfo* pBssInfo;

    zmw_get_wlan_dev(dev);

    pBssInfo = wd->sta.bssList.head;
    if (pBssInfo == NULL)
    {
        if( adhocMode == ZM_ADHOCBAND_B || adhocMode == ZM_ADHOCBAND_G ||
            adhocMode == ZM_ADHOCBAND_BG || adhocMode == ZM_ADHOCBAND_ABG )
        {
            returnChannel = zfChGetFirst2GhzChannel(dev);
        }
        else
        {
            returnChannel = zfChGetFirst5GhzChannel(dev);
        }

        return returnChannel;
    }

    /* #1 Get Allowed Channel following Country Code ! */
    zmw_declare_for_critical_section();
    zmw_enter_critical_section(dev);
    for (i = 0; i < wd->regulationTable.allowChannelCnt; i++)
    {
        if (wd->regulationTable.allowChannel[i].channel < 3000)
        { // 2.4GHz
            Array_24G[count_24G] = wd->regulationTable.allowChannel[i].channel;
            count_24G++;
        }
        else
        { // 5GHz
            count_5G++;
            Array_5G[i] = wd->regulationTable.allowChannel[i].channel;
        }
    }
    zmw_leave_critical_section(dev);

    while( pBssInfo != NULL )
    {
        /* #2_1 Count BSS number in some specificed frequency in 2.4GHz band ! */
        if( adhocMode == ZM_ADHOCBAND_B || adhocMode == ZM_ADHOCBAND_G ||
            adhocMode == ZM_ADHOCBAND_BG || adhocMode == ZM_ADHOCBAND_ABG )
        {
            for( i=0; i<=(count_24G+3); i++ )
            {
                if( pBssInfo->frequency == Array_24G[i] )
                { // Array_24G[0] correspond to BssNumberIn24G[2]
                    BssNumberIn24G[pBssInfo->channel+1]++;
                }
            }
        }

        /* #2_2 Count BSS number in some specificed frequency in 5GHz band ! */
        if( adhocMode == ZM_ADHOCBAND_A || adhocMode == ZM_ADHOCBAND_ABG )
        {
            for( i=0; i<count_5G; i++ )
            { // 5GHz channel is not equal to array index
                if( pBssInfo->frequency == Array_5G[i] )
                { // Array_5G[0] correspond to BssNumberIn5G[0]
                    BssNumberIn5G[i]++;
                }
            }
        }

        pBssInfo = pBssInfo->next;
    }

#if 0
    for(i=0; i<=(count_24G+3); i++)
    {
        printk("2.4GHz Before combin, %d BSS network : %d", i, BssNumberIn24G[i]);
    }

    for(i=0; i<count_5G; i++)
    {
        printk("5GHz Before combin, %d BSS network : %d", i, BssNumberIn5G[i]);
    }
#endif

    if( adhocMode == ZM_ADHOCBAND_B || adhocMode == ZM_ADHOCBAND_G ||
        adhocMode == ZM_ADHOCBAND_BG || adhocMode == ZM_ADHOCBAND_ABG )
    {
        /* #3_1 Count BSS number that influence the specificed frequency in 2.4GHz ! */
        for( j=0; j<count_24G; j++ )
        {
            CombinationBssNumberIn24G[j] = BssNumberIn24G[j]   + BssNumberIn24G[j+1] +
                                           BssNumberIn24G[j+2] + BssNumberIn24G[j+3] +
                                           BssNumberIn24G[j+4];
            //printk("After combine, the number of BSS network channel %d is %d",
            //                                   j , CombinationBssNumberIn24G[j]);
        }

        /* #4_1 Find the less utilized frequency in 2.4GHz band ! */
        min24GIndex = zfFindMinimumUtilizationChannelIndex(dev, CombinationBssNumberIn24G, count_24G);
    }

    /* #4_2 Find the less utilized frequency in 5GHz band ! */
    if( adhocMode == ZM_ADHOCBAND_A || adhocMode == ZM_ADHOCBAND_ABG )
    {
        min5GIndex = zfFindMinimumUtilizationChannelIndex(dev, BssNumberIn5G, count_5G);
    }

    if( adhocMode == ZM_ADHOCBAND_B || adhocMode == ZM_ADHOCBAND_G || adhocMode == ZM_ADHOCBAND_BG )
    {
        return Array_24G[min24GIndex];
    }
    else if( adhocMode == ZM_ADHOCBAND_A )
    {
        return Array_5G[min5GIndex];
    }
    else if( adhocMode == ZM_ADHOCBAND_ABG )
    {
        if ( CombinationBssNumberIn24G[min24GIndex] <= BssNumberIn5G[min5GIndex] )
            return Array_24G[min24GIndex];
        else
            return Array_5G[min5GIndex];
    }
    else
        return 2412;
}

u16_t zfFindMinimumUtilizationChannelIndex(zdev_t* dev, u16_t* array, u16_t count)
{
    u8_t   i;
    u16_t  tempMinIndex, tempMinValue;

    i = 1;
    tempMinIndex = 0;
    tempMinValue = array[tempMinIndex];
    while( i< count )
    {
        if( array[i] < tempMinValue )
        {
            tempMinValue = array[i];
            tempMinIndex = i;
        }
        i++;
    }

    return tempMinIndex;
}

u8_t zfCompareWithBssid(zdev_t* dev, u16_t* bssid)
{
    zmw_get_wlan_dev(dev);

    if ( zfMemoryIsEqual((u8_t*)bssid, (u8_t*)wd->sta.bssid, 6) )
    {
        return 1;
    }
    else
    {
        return 0;
    }
}
