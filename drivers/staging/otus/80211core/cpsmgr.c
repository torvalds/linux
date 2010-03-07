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

/**
  *  The power saving manager is to save the power as much as possible.
  *  Generally speaking, it controls:
  *
  *         - when to sleep
  *         -
  *
  */
#include "cprecomp.h"

void zfPowerSavingMgrInit(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    wd->sta.powerSaveMode = ZM_STA_PS_NONE;
    wd->sta.psMgr.state = ZM_PS_MSG_STATE_ACTIVE;
    wd->sta.psMgr.isSleepAllowed = 0;
    wd->sta.psMgr.maxSleepPeriods = 1;
    wd->sta.psMgr.ticks = 0;
    wd->sta.psMgr.sleepAllowedtick = 0;
}

static u16_t zfPowerSavingMgrHandlePsNone(zdev_t* dev, u8_t *isWakeUpRequired)
{
    u16_t ret = 0;
    zmw_get_wlan_dev(dev);

    switch(wd->sta.psMgr.state)
    {
        case ZM_PS_MSG_STATE_ACTIVE:
            *isWakeUpRequired = 0;
            break;

        case ZM_PS_MSG_STATE_T1:
        case ZM_PS_MSG_STATE_T2:
        case ZM_PS_MSG_STATE_SLEEP:
        default:
            *isWakeUpRequired = 1;
zm_debug_msg0("zfPowerSavingMgrHandlePsNone: Wake up now\n");
            if ( zfStaIsConnected(dev) )
            {
                zm_debug_msg0("zfPowerSavingMgrOnHandleT1 send Null data\n");
                //zfSendNullData(dev, 0);
                ret = 1;
            }

            wd->sta.psMgr.state = ZM_PS_MSG_STATE_ACTIVE;
            break;
    }
    return ret;
}

static void zfPowerSavingMgrHandlePs(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    switch(wd->sta.psMgr.state)
    {
        case ZM_PS_MSG_STATE_ACTIVE:
            //zm_debug_msg0("zfPowerSavingMgrHandlePs: Prepare to sleep...\n");
            //wd->sta.psMgr.state = ZM_PS_MSG_STATE_T1;
            break;

        case ZM_PS_MSG_STATE_T1:
        case ZM_PS_MSG_STATE_T2:
        case ZM_PS_MSG_STATE_SLEEP:
        default:
            break;
    }
}

void zfPowerSavingMgrSetMode(zdev_t* dev, u8_t mode)
{
    u16_t sendNull = 0;
    u8_t isWakeUpRequired = 0;

    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zm_debug_msg1("mode = ", mode);

    if (mode > ZM_STA_PS_LIGHT)
    {
        zm_debug_msg0("return - wrong power save mode");
        return;
    }

    zmw_enter_critical_section(dev);

    #if 1
    switch(mode)
    {
        case ZM_STA_PS_NONE:
            sendNull = zfPowerSavingMgrHandlePsNone(dev, &isWakeUpRequired);
            break;

        case ZM_STA_PS_FAST:
        case ZM_STA_PS_LIGHT:
            wd->sta.psMgr.maxSleepPeriods = 1;
            zfPowerSavingMgrHandlePs(dev);
            break;

        case ZM_STA_PS_MAX:
            wd->sta.psMgr.maxSleepPeriods = ZM_PS_MAX_SLEEP_PERIODS;
            zfPowerSavingMgrHandlePs(dev);
            break;
    }
    #else
    switch(wd->sta.psMgr.state)
    {
        case ZM_PS_MSG_STATE_ACTIVE:
            if ( mode != ZM_STA_PS_NONE )
            {
zm_debug_msg0("zfPowerSavingMgrSetMode: switch from ZM_PS_MSG_STATE_ACTIVE to ZM_PS_MSG_STATE_T1\n");
                // Stall the TX & start to wait the pending TX to be completed
                wd->sta.psMgr.state = ZM_PS_MSG_STATE_T1;
            }
            break;

        case ZM_PS_MSG_STATE_SLEEP:
            break;
    }
    #endif

    wd->sta.powerSaveMode = mode;
    zmw_leave_critical_section(dev);

    if ( isWakeUpRequired )
    {
        zfHpPowerSaveSetState(dev, 0);
        wd->sta.psMgr.tempWakeUp = 0;
    }

    if ( zfStaIsConnected(dev)
         && (wd->wlanMode == ZM_MODE_INFRASTRUCTURE) )
    {
        switch(mode)
        {
            case ZM_STA_PS_NONE:
                zfHpPowerSaveSetMode(dev, 0, 0, wd->beaconInterval);
                break;

            case ZM_STA_PS_FAST:
            case ZM_STA_PS_MAX:
            case ZM_STA_PS_LIGHT:
                zfHpPowerSaveSetMode(dev, 0, 1, wd->beaconInterval);
                break;

            default:
                zfHpPowerSaveSetMode(dev, 0, 0, wd->beaconInterval);
                break;
        }
    }

    if (sendNull == 1)
    {
        zfSendNullData(dev, 0);
    }

    return;
}

static void zfPowerSavingMgrNotifyPSToAP(zdev_t *dev)
{
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    if ( (wd->sta.psMgr.tempWakeUp != 1)&&
         (wd->sta.psMgr.lastTxUnicastFrm != wd->commTally.txUnicastFrm ||
          wd->sta.psMgr.lastTxBroadcastFrm != wd->commTally.txBroadcastFrm ||
          wd->sta.psMgr.lastTxMulticastFrm != wd->commTally.txMulticastFrm) )
    {
        zmw_enter_critical_section(dev);
        wd->sta.psMgr.lastTxUnicastFrm = wd->commTally.txUnicastFrm;
        wd->sta.psMgr.lastTxBroadcastFrm = wd->commTally.txBroadcastFrm;
        wd->sta.psMgr.lastTxMulticastFrm = wd->commTally.txMulticastFrm;
        zmw_leave_critical_section(dev);

        zfSendNullData(dev, 1);
    }
}

static void zfPowerSavingMgrOnHandleT1(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    // If the tx Q is not empty...return
    if ( zfIsVtxqEmpty(dev) == FALSE )
    {
        return;
    }

zm_debug_msg0("VtxQ is empty now...Check if HAL TXQ is empty\n");

    // The the HAL TX Q is not empty...return
    if ( zfHpGetFreeTxdCount(dev) != zfHpGetMaxTxdCount(dev) )
    {
        return;
    }

zm_debug_msg0("HAL TXQ is empty now...Could go to sleep...\n");

    zmw_enter_critical_section(dev);

    if (wd->sta.powerSaveMode == ZM_STA_PS_LIGHT)
    {
        if (wd->sta.ReceivedPktRatePerSecond > 200)
        {
            zmw_leave_critical_section(dev);
            return;
        }

        if ( zfStaIsConnected(dev)
             && (wd->wlanMode == ZM_MODE_INFRASTRUCTURE) )
        {
            if (wd->sta.psMgr.sleepAllowedtick) {
                wd->sta.psMgr.sleepAllowedtick--;
                zmw_leave_critical_section(dev);
                return;
            }
        }
    }

    wd->sta.psMgr.state = ZM_PS_MSG_STATE_T2;

    zmw_leave_critical_section(dev);

    // Send the Null pkt to AP to notify that I'm going to sleep
    if ( zfStaIsConnected(dev) )
    {
zm_debug_msg0("zfPowerSavingMgrOnHandleT1 send Null data\n");
        zfPowerSavingMgrNotifyPSToAP(dev);
    }

    // Stall the TX now
    // zfTxEngineStop(dev);
}

static void zfPowerSavingMgrOnHandleT2(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    // Wait until the Null pkt is transmitted
    if ( zfHpGetFreeTxdCount(dev) != zfHpGetMaxTxdCount(dev) )
    {
        return;
    }

    zmw_enter_critical_section(dev);
    wd->sta.psMgr.state = ZM_PS_MSG_STATE_SLEEP;
    wd->sta.psMgr.lastTxUnicastFrm = wd->commTally.txUnicastFrm;
    wd->sta.psMgr.lastTxBroadcastFrm = wd->commTally.txBroadcastFrm;
    wd->sta.psMgr.lastTxMulticastFrm = wd->commTally.txMulticastFrm;
    zmw_leave_critical_section(dev);

    // Let CHIP sleep now
zm_debug_msg0("zfPowerSavingMgrOnHandleT2 zzzz....\n");
    zfHpPowerSaveSetState(dev, 1);
    wd->sta.psMgr.tempWakeUp = 0;
}

u8_t zfPowerSavingMgrIsSleeping(zdev_t *dev)
{
    u8_t isSleeping = FALSE;
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    if ( wd->sta.psMgr.state == ZM_PS_MSG_STATE_SLEEP ||
         wd->sta.psMgr.state == ZM_PS_MSG_STATE_T2)
    {
        isSleeping = TRUE;
    }
    zmw_leave_critical_section(dev);
    return isSleeping;
}

static u8_t zfPowerSavingMgrIsIdle(zdev_t *dev)
{
    u8_t isIdle = 0;

    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    if ( zfStaIsConnected(dev) && wd->sta.psMgr.isSleepAllowed == 0 )
    {
        goto done;
    }

    if ( wd->sta.bChannelScan )
    {
        goto done;
    }

    if ( zfStaIsConnecting(dev) )
    {
        goto done;
    }

    if (wd->sta.powerSaveMode == ZM_STA_PS_LIGHT)
    {
        if (wd->sta.ReceivedPktRatePerSecond > 200)
        {
            goto done;
        }

        if ( zfStaIsConnected(dev)
             && (wd->wlanMode == ZM_MODE_INFRASTRUCTURE) )
        {
            if (wd->sta.psMgr.sleepAllowedtick) {
                wd->sta.psMgr.sleepAllowedtick--;
                goto done;
            }
        }
    }

    isIdle = 1;

done:
    zmw_leave_critical_section(dev);

    if ( zfIsVtxqEmpty(dev) == FALSE )
    {
        isIdle = 0;
    }

    return isIdle;
}

static void zfPowerSavingMgrSleepIfIdle(zdev_t *dev)
{
    u8_t isIdle;

    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    isIdle = zfPowerSavingMgrIsIdle(dev);

    if ( isIdle == 0 )
    {
        return;
    }

    zmw_enter_critical_section(dev);

    switch(wd->sta.powerSaveMode)
    {
        case ZM_STA_PS_NONE:
            break;

        case ZM_STA_PS_MAX:
        case ZM_STA_PS_FAST:
        case ZM_STA_PS_LIGHT:
            zm_debug_msg0("zfPowerSavingMgrSleepIfIdle: IDLE so slep now...\n");
            wd->sta.psMgr.state = ZM_PS_MSG_STATE_T1;
            break;
    }

    zmw_leave_critical_section(dev);
}

static void zfPowerSavingMgrDisconnectMain(zdev_t* dev)
{
#ifdef ZM_ENABLE_DISCONNECT_PS
    switch(wd->sta.psMgr.state)
    {
        case ZM_PS_MSG_STATE_ACTIVE:
            zfPowerSavingMgrSleepIfIdle(dev);
            break;

        case ZM_PS_MSG_STATE_SLEEP:
            break;

        case ZM_PS_MSG_STATE_T1:
            zfPowerSavingMgrOnHandleT1(dev);
            break;

        case ZM_PS_MSG_STATE_T2:
            zfPowerSavingMgrOnHandleT2(dev);
            break;
    }
#else
    zfPowerSavingMgrWakeup(dev);
#endif
}

static void zfPowerSavingMgrInfraMain(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    switch(wd->sta.psMgr.state)
    {
        case ZM_PS_MSG_STATE_ACTIVE:
            zfPowerSavingMgrSleepIfIdle(dev);
            break;

        case ZM_PS_MSG_STATE_SLEEP:
            break;

        case ZM_PS_MSG_STATE_T1:
            zfPowerSavingMgrOnHandleT1(dev);
            break;

        case ZM_PS_MSG_STATE_T2:
            zfPowerSavingMgrOnHandleT2(dev);
            break;
    }
}

void zfPowerSavingMgrAtimWinExpired(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

//printk("zfPowerSavingMgrAtimWinExpired #1\n");
    if ( wd->sta.powerSaveMode == ZM_STA_PS_NONE )
    {
        return;
    }

//printk("zfPowerSavingMgrAtimWinExpired #2\n");
    // if we received any ATIM window from the others to indicate we have buffered data
    // at the other station, we can't go to sleep
    if ( wd->sta.recvAtim )
    {
        wd->sta.recvAtim = 0;
        zm_debug_msg0("Can't sleep due to receving ATIM window!");
        return;
    }

    // if we are the one to tx beacon during last beacon interval. we can't go to sleep
    // since we need to be alive to respond the probe request!
    if ( wd->sta.txBeaconInd )
    {
        zm_debug_msg0("Can't sleep due to just transmit a beacon!");
        return;
    }

    // If we buffer any data for the other stations. we could not go to sleep
    if ( wd->sta.ibssPrevPSDataCount != 0 )
    {
        zm_debug_msg0("Can't sleep due to buffering data for the others!");
        return;
    }

    // before sleeping, we still need to notify the others by transmitting null
    // pkt with power mgmt bit turned on.
    zfPowerSavingMgrOnHandleT1(dev);
}

static void zfPowerSavingMgrIBSSMain(zdev_t* dev)
{
    // wait for the end of
    // if need to wait to know if we are the one to transmit the beacon
    // during the beacon interval. If it's me, we can't go to sleep.

    zmw_get_wlan_dev(dev);

    switch(wd->sta.psMgr.state)
    {
        case ZM_PS_MSG_STATE_ACTIVE:
        case ZM_PS_MSG_STATE_SLEEP:
        case ZM_PS_MSG_STATE_T1:
            break;

        case ZM_PS_MSG_STATE_T2:
            zfPowerSavingMgrOnHandleT2(dev);
            break;
    }

    return;
}

#if 1
void zfPowerSavingMgrMain(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    switch (wd->sta.adapterState)
    {
    case ZM_STA_STATE_DISCONNECT:
        zfPowerSavingMgrDisconnectMain(dev);
        break;
    case ZM_STA_STATE_CONNECTED:
        {
            if (wd->wlanMode == ZM_MODE_INFRASTRUCTURE) {
                zfPowerSavingMgrInfraMain(dev);
            } else if (wd->wlanMode == ZM_MODE_IBSS) {
                zfPowerSavingMgrIBSSMain(dev);
            }
        }
        break;
    case ZM_STA_STATE_CONNECTING:
    default:
        break;
    }
}
#else
void zfPowerSavingMgrMain(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    if ( wd->wlanMode != ZM_MODE_INFRASTRUCTURE )
    {
        return;
    }

    switch(wd->sta.psMgr.state)
    {
        case ZM_PS_MSG_STATE_ACTIVE:
            goto check_sleep;
            break;

        case ZM_PS_MSG_STATE_SLEEP:
            goto sleeping;
            break;

        case ZM_PS_MSG_STATE_T1:
            zfPowerSavingMgrOnHandleT1(dev);
            break;

        case ZM_PS_MSG_STATE_T2:
            zfPowerSavingMgrOnHandleT2(dev);
            break;
    }

    return;

sleeping:
    return;

check_sleep:
    zfPowerSavingMgrSleepIfIdle(dev);
    return;
}
#endif

#ifdef ZM_ENABLE_POWER_SAVE
void zfPowerSavingMgrWakeup(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

//zm_debug_msg0("zfPowerSavingMgrWakeup");

    //if ( wd->sta.psMgr.state != ZM_PS_MSG_STATE_ACTIVE && ( zfPowerSavingMgrIsIdle(dev) == 0 ))
    if ( wd->sta.psMgr.state != ZM_PS_MSG_STATE_ACTIVE )
    {
        zmw_enter_critical_section(dev);

        wd->sta.psMgr.isSleepAllowed = 0;
        wd->sta.psMgr.state = ZM_PS_MSG_STATE_ACTIVE;

        if ( wd->sta.powerSaveMode > ZM_STA_PS_NONE )
            wd->sta.psMgr.tempWakeUp = 1;

        zmw_leave_critical_section(dev);

        // Wake up the CHIP now!!
        zfHpPowerSaveSetState(dev, 0);
    }
}
#else
void zfPowerSavingMgrWakeup(zdev_t* dev)
{
}
#endif

void zfPowerSavingMgrProcessBeacon(zdev_t* dev, zbuf_t* buf)
{
    u8_t   length, bitmap;
    u16_t  offset, n1, n2, q, r;
    zbuf_t* psBuf;

    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    if ( wd->sta.powerSaveMode == ZM_STA_PS_NONE  )
    //if ( wd->sta.psMgr.state != ZM_PS_MSG_STATE_SLEEP )
    {
        return;
    }

    wd->sta.psMgr.isSleepAllowed = 1;

    if ( (offset=zfFindElement(dev, buf, ZM_WLAN_EID_TIM)) != 0xffff )
    {
        length = zmw_rx_buf_readb(dev, buf, offset+1);

        if ( length > 3 )
        {
            n1 = zmw_rx_buf_readb(dev, buf, offset+4) & (~ZM_BIT_0);
            n2 = length + n1 - 4;
            q = wd->sta.aid >> 3;
            r = wd->sta.aid & 7;

            if ((q >= n1) && (q <= n2))
            {
                bitmap = zmw_rx_buf_readb(dev, buf, offset+5+q-n1);

                if ( (bitmap >> r) &  ZM_BIT_0 )
                {
                    //if ( wd->sta.powerSaveMode == ZM_STA_PS_FAST )
                    if ( 0 )
                    {
                        wd->sta.psMgr.state = ZM_PS_MSG_STATE_S1;
                        //zfSendPSPoll(dev);
                        zfSendNullData(dev, 0);
                    }
                    else
                    {
                        if ((wd->sta.qosInfo&0xf) != 0xf)
                        {
                            /* send ps-poll */
                            //printk("zfSendPSPoll #1\n");

                            wd->sta.psMgr.isSleepAllowed = 0;

                            switch (wd->sta.powerSaveMode)
                            {
                            case ZM_STA_PS_MAX:
                            case ZM_STA_PS_FAST:
                                //zm_debug_msg0("wake up and send PS-Poll\n");
                                zfSendPSPoll(dev);
                                break;
                            case ZM_STA_PS_LIGHT:
                                zm_debug_msg0("wake up and send null data\n");

                                zmw_enter_critical_section(dev);
                                wd->sta.psMgr.sleepAllowedtick = 400;
                                zmw_leave_critical_section(dev);

                                zfSendNullData(dev, 0);
                                break;
                            }

                            wd->sta.psMgr.tempWakeUp = 0;
                        }
                    }
                }
            }
        }
    }

    while ((psBuf = zfQueueGet(dev, wd->sta.uapsdQ)) != NULL)
    {
        zfTxSendEth(dev, psBuf, 0, ZM_EXTERNAL_ALLOC_BUF, 0);
    }

    //printk("zfPowerSavingMgrProcessBeacon #1\n");
    zfPowerSavingMgrMain(dev);
}

void zfPowerSavingMgrConnectNotify(zdev_t *dev)
{
    zmw_get_wlan_dev(dev);

    if ( wd->wlanMode == ZM_MODE_INFRASTRUCTURE )
    {
        switch(wd->sta.powerSaveMode)
        {
            case ZM_STA_PS_NONE:
                zfHpPowerSaveSetMode(dev, 0, 0, wd->beaconInterval);
                break;

            case ZM_STA_PS_FAST:
            case ZM_STA_PS_MAX:
            case ZM_STA_PS_LIGHT:
                zfHpPowerSaveSetMode(dev, 0, 1, wd->beaconInterval);
                break;

            default:
                zfHpPowerSaveSetMode(dev, 0, 0, wd->beaconInterval);
                break;
        }
    }
}

void zfPowerSavingMgrPreTBTTInterrupt(zdev_t *dev)
{
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    /* disable TBTT interrupt when change from connection to disconnect */
    if (zfStaIsDisconnect(dev)) {
        zfHpPowerSaveSetMode(dev, 0, 0, 0);
        zfPowerSavingMgrWakeup(dev);
        return;
    }

    zmw_enter_critical_section(dev);
    wd->sta.psMgr.ticks++;

    if ( wd->sta.psMgr.ticks < wd->sta.psMgr.maxSleepPeriods )
    {
        zmw_leave_critical_section(dev);
        return;
    }
    else
    {
        wd->sta.psMgr.ticks = 0;
    }

    zmw_leave_critical_section(dev);

    zfPowerSavingMgrWakeup(dev);
}

/* Leave an empty line below to remove warning message on some compiler */

