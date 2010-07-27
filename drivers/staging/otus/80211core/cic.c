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
#include "ratectrl.h"


void zfUpdateBssid(zdev_t* dev, u8_t* bssid)
{

    zmw_get_wlan_dev(dev);

    //zmw_declare_for_critical_section();

    //zmw_enter_critical_section(dev);
    wd->sta.bssid[0] = bssid[0] + (((u16_t) bssid[1]) << 8);
    wd->sta.bssid[1] = bssid[2] + (((u16_t) bssid[3]) << 8);
    wd->sta.bssid[2] = bssid[4] + (((u16_t) bssid[5]) << 8);
    //zmw_leave_critical_section(dev);

    zfHpSetBssid(dev, bssid);

}

/************************************************************************************/
/*                                                                                  */
/*    FUNCTION DESCRIPTION                  zfResetSupportRate                      */
/*      Reset support rate to default value.                                        */
/*                                                                                  */
/*    INPUTS                                                                        */
/*      dev : device pointer                                                        */
/*      type: ZM_DEFAULT_SUPPORT_RATE_ZERO       => reset to zero                   */
/*            ZM_DEFAULT_SUPPORT_RATE_DISCONNECT => reset to disconnect status      */
/*            ZM_DEFAULT_SUPPORT_RATE_IBSS_B     => reset to IBSS creator(b mode)   */
/*            ZM_DEFAULT_SUPPORT_RATE_IBSS_AG    => reset to IBSS creator(a/g mode) */
/*                                                                                  */
/************************************************************************************/
void zfResetSupportRate(zdev_t* dev, u8_t type)
{
    zmw_get_wlan_dev(dev);

    switch(type)
    {
    case ZM_DEFAULT_SUPPORT_RATE_ZERO:
        wd->bRate = 0;
        wd->bRateBasic = 0;
        wd->gRate = 0;
        wd->gRateBasic = 0;
        break;
    case ZM_DEFAULT_SUPPORT_RATE_DISCONNECT:
        wd->bRate = 0xf;
        wd->bRateBasic = 0xf;
        wd->gRate = 0xff;
        wd->gRateBasic = 0x15;
        break;
    case ZM_DEFAULT_SUPPORT_RATE_IBSS_B:
        wd->bRate = 0xf;
        wd->bRateBasic = 0xf;
        wd->gRate = 0;
        wd->gRateBasic = 0;
        break;
    case ZM_DEFAULT_SUPPORT_RATE_IBSS_AG:
        wd->bRate = 0xf;
        wd->bRateBasic = 0xf;
        wd->gRate = 0xff;
        wd->gRateBasic = 0;
        break;
    }
}

void zfUpdateSupportRate(zdev_t* dev, u8_t* rateArray)
{
    u8_t bRate=0, bRateBasic=0, gRate=0, gRateBasic=0;
    u8_t length = rateArray[1];
    u8_t i, j;

    zmw_get_wlan_dev(dev);

    for(i=2; i<length+2; i++)
    {
        for(j=0; j<4; j++)
        {
            if ( (rateArray[i] & 0x7f) == zg11bRateTbl[j] )
            {
                bRate |= (1 << j);
                if ( rateArray[i] & 0x80 )
                {
                    bRateBasic |= (1 << j);
                }
            }
        }

        if ( j == 4 )
        {
            for(j=0; j<8; j++)
            {
                if ( (rateArray[i] & 0x7f) == zg11gRateTbl[j] )
                {
                    gRate |= (1 << j);
                    if ( rateArray[i] & 0x80 )
                    {
                        gRateBasic |= (1 << j);
                    }
                }
            }
        }
    }


    wd->bRate |= bRate;
    wd->bRateBasic |= bRateBasic;
    wd->gRate |= gRate;
    wd->gRateBasic |= gRateBasic;
}

u8_t zfIsGOnlyMode(zdev_t* dev, u16_t  frequency, u8_t* rateArray)
{
    u8_t length = rateArray[1];
    u8_t i, j;

    if (frequency < 3000) {
        for (i = 2; i < length+2; i++) {
            for (j = 0; j < 8; j++) {
                if ( ((rateArray[i] & 0x7f) == zg11gRateTbl[j])
                     && (rateArray[i] & 0x80) ) {
                    return 1;
                }
            }
        }
    }

    return 0;
}

void zfGatherBMode(zdev_t* dev, u8_t* rateArray, u8_t* extrateArray)
{
    u8_t gatherBMode[ZM_MAX_SUPP_RATES_IE_SIZE + 2];
    u8_t i, j, k = 0;
    u8_t length;

    gatherBMode[0] = ZM_WLAN_EID_SUPPORT_RATE;
    gatherBMode[1] = 0;

    length = rateArray[1];
    for (i = 2; i < length+2; i++) {
        for (j = 0; j < 4; j++) {
            if ( (rateArray[i] & 0x7f) == zg11bRateTbl[j] ) {
                gatherBMode[2+k] = rateArray[i];

                gatherBMode[1]++;
                k++;
            }
        }
    }

    length = extrateArray[1];
    for (i = 2; i < length+2; i++) {
        for (j = 0; j < 4; j++) {
            if ( (extrateArray[i] & 0x7f) == zg11bRateTbl[j] ) {
                gatherBMode[2+k] = extrateArray[i];

                gatherBMode[1]++;
                k++;
            }
        }
    }

    extrateArray[0] = extrateArray[1] = 0;
    zfMemoryCopy(rateArray, gatherBMode, gatherBMode[1]+2);
}

u16_t zfGetRandomNumber(zdev_t* dev, u16_t initValue)
{
#if 0
    /* Compiler/Linker error on Linux */
    if ( initValue )
    {
        srand(initValue);
    }

    return ((u16_t)rand());
#endif
    return 0;
}

u8_t zfPSDeviceSleep(zdev_t* dev)
{
    //zmw_get_wlan_dev(dev);

    /* enter PS mode */

    return 0;
}

u8_t zcOfdmPhyCrtlToRate[] =
{
    /* 0x8=48M, 0x9=24M, 0xa=12M, 0xb=6M, 0xc=54M, 0xd=36M, 0xe=18M, 0xf=9M */
            10,       8,       6,      4,      11,       9,       7,      5
};

u8_t zfPhyCtrlToRate(u32_t phyCtrl)
{
    u32_t mt, mcs, sg;
    u8_t rate = 0;

    mt = phyCtrl & 0x3;
    mcs = (phyCtrl>>18) & 0x3f;
    sg = (phyCtrl>>31) & 0x1;

    if ((mt == 0) && (mcs <=3))
    {
        rate = (u8_t)mcs;
    }
    else if ((mt == 1) && (mcs >= 0x8) && (mcs <= 0xf))
    {
        rate = zcOfdmPhyCrtlToRate[mcs-8];
    }
    else if ((mt == 2) && (mcs <= 15))
    {
        rate = (u8_t)mcs + 12;
        if(sg) {
            if (mcs != 7)
            {
                rate = (u8_t)mcs + 12 + 2;
            }
            else //MCS7-SG
            {
                rate = (u8_t)30;
            }
        }
    }

    return rate;
}


void zfCoreEvent(zdev_t* dev, u16_t event, u8_t* rsp)
{
    u16_t i;
    zbuf_t* psBuf;
    u8_t moreData;
    u8_t vap = 0;
    u8_t peerIdx;
    s8_t res;
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();


    if (event == 0) //Beacon Event
    {
        if ( wd->wlanMode == ZM_MODE_AP )
        {
            zfApSendBeacon(dev);

            if (wd->CurrentDtimCount == 0)
            {
                /* TODO : Send queued broadcast frames at BC/MC event */
                do
                {
                    psBuf = NULL;
                    moreData = 0;
                    zmw_enter_critical_section(dev);
                    if (wd->ap.bcmcTail[vap] != wd->ap.bcmcHead[vap])
                    {
                        //zm_msg0_mm(ZM_LV_0, "Send BCMC frames");
                        psBuf = wd->ap.bcmcArray[vap][wd->ap.bcmcHead[vap]];
                        wd->ap.bcmcHead[vap] = (wd->ap.bcmcHead[vap] + 1)
                                & (ZM_BCMC_ARRAY_SIZE - 1);
                        if (wd->ap.bcmcTail[vap] != wd->ap.bcmcHead[vap])
                        {
                            moreData = 0x20;
                        }
                    }
                    zmw_leave_critical_section(dev);
                    if (psBuf != NULL)
                    {
                        /* TODO : config moreData bit */
                        zfTxSendEth(dev, psBuf, 0, ZM_EXTERNAL_ALLOC_BUF,
                                moreData);
                    }
                } while(psBuf != NULL);

            }
        }
        else
        {
            /* STA mode */
            if ( wd->sta.powerSaveMode > ZM_STA_PS_NONE )
            {
                /* send queued packets */
                for(i=0; i<wd->sta.staPSDataCount; i++)
                {
                    zfTxSendEth(dev, wd->sta.staPSDataQueue[i], 0,
                                ZM_EXTERNAL_ALLOC_BUF, 0);
                }

                wd->sta.staPSDataCount = 0;
            }

            if ( wd->wlanMode == ZM_MODE_IBSS )
            {
                zfStaSendBeacon(dev);
                wd->sta.ibssAtimTimer = ZM_BIT_15 | wd->sta.atimWindow;
            }

            zfPowerSavingMgrPreTBTTInterrupt(dev);
        }
    } //if (event == 0) //Beacon Event
    else if (event == 1) //Retry completed event
    {
        u32_t retryRate;

        retryRate = (u32_t)(rsp[6]) + (((u32_t)(rsp[7]))<<8)
                + (((u32_t)(rsp[8]))<<16) + (((u32_t)(rsp[9]))<<24);
        /* Degrade Tx Rate */
        if (wd->wlanMode == ZM_MODE_AP)
        {
            zmw_enter_critical_section(dev);
            i = zfApFindSta(dev, (u16_t*)rsp);
            if (i != 0xffff)
            {
                zfRateCtrlTxFailEvent(dev, &wd->ap.staTable[i].rcCell, 0,(u32_t)zfPhyCtrlToRate(retryRate));
            }
            zmw_leave_critical_section(dev);
        }
        else
        {
            zmw_enter_critical_section(dev);
            res = zfStaFindOppositeByMACAddr(dev, (u16_t*)rsp, &peerIdx);
            if ( res == 0 )
            {
                zfRateCtrlTxFailEvent(dev, &wd->sta.oppositeInfo[peerIdx].rcCell, 0,(u32_t)zfPhyCtrlToRate(retryRate));
            }
            zmw_leave_critical_section(dev);
        }
    } //else if (event == 1) //Retry completed event
    else if (event == 2) //Tx Fail event
    {
        u32_t retryRate;

        retryRate = (u32_t)(rsp[6]) + (((u32_t)(rsp[7]))<<8)
                + (((u32_t)(rsp[8]))<<16) + (((u32_t)(rsp[9]))<<24);

        /* Degrade Tx Rate */
        if (wd->wlanMode == ZM_MODE_AP)
        {
            zmw_enter_critical_section(dev);
            i = zfApFindSta(dev, (u16_t*)rsp);
            if (i != 0xffff)
            {
                zfRateCtrlTxFailEvent(dev, &wd->ap.staTable[i].rcCell, 0,(u32_t)zfPhyCtrlToRate(retryRate));
            }
            zmw_leave_critical_section(dev);

            zfApSendFailure(dev, rsp);
        }
        else
        {
            zmw_enter_critical_section(dev);
            res = zfStaFindOppositeByMACAddr(dev, (u16_t*)rsp, &peerIdx);
            if ( res == 0 )
            {
                zfRateCtrlTxFailEvent(dev, &wd->sta.oppositeInfo[peerIdx].rcCell, 0,(u32_t)zfPhyCtrlToRate(retryRate));
            }
            zmw_leave_critical_section(dev);
        }
    } //else if (event == 2) //Tx Fail event
    else if (event == 3) //Tx Comp event
    {
        u32_t retryRate;

        retryRate = (u32_t)(rsp[6]) + (((u32_t)(rsp[7]))<<8)
                + (((u32_t)(rsp[8]))<<16) + (((u32_t)(rsp[9]))<<24);

        /* TODO : Tx completed, used for rate control probing */
        if (wd->wlanMode == ZM_MODE_AP)
        {
            zmw_enter_critical_section(dev);
            i = zfApFindSta(dev, (u16_t*)rsp);
            if (i != 0xffff)
            {
                zfRateCtrlTxSuccessEvent(dev, &wd->ap.staTable[i].rcCell, zfPhyCtrlToRate(retryRate));
            }
            zmw_leave_critical_section(dev);
        }
        else
        {
            zmw_enter_critical_section(dev);
            res = zfStaFindOppositeByMACAddr(dev, (u16_t*)rsp, &peerIdx);
            if ( res == 0 )
            {
                zfRateCtrlTxSuccessEvent(dev, &wd->sta.oppositeInfo[peerIdx].rcCell, zfPhyCtrlToRate(retryRate));
            }
            zmw_leave_critical_section(dev);
        }
    } //else if (event == 3) //Tx Comp event
    else if (event == 4) //BA failed count
    {
        u32_t fail;
        u32_t rate;
        peerIdx = 0;

        fail=((u32_t*)rsp)[0] & 0xFFFF;
        rate=((u32_t*)rsp)[0] >> 16;

        if (rate > 15) {
            rate = (rate & 0xF) + 12 + 2;
        }
        else {
            rate = rate + 12;
        }

        zmw_enter_critical_section(dev);
        zfRateCtrlTxFailEvent(dev, &wd->sta.oppositeInfo[peerIdx].rcCell, (u8_t)rate, fail);
        zmw_leave_critical_section(dev);
    }
}

void zfBeaconCfgInterrupt(zdev_t* dev, u8_t* rsp)
{
    u32_t txBeaconCounter;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    if ( wd->wlanMode == ZM_MODE_IBSS )
    {
        txBeaconCounter = *((u32_t *)rsp);
        if ( wd->sta.beaconTxCnt != txBeaconCounter )
        {
            wd->sta.txBeaconInd = 1;

            zmw_enter_critical_section(dev);
            wd->tickIbssSendBeacon = 0;
            zmw_leave_critical_section(dev);
        }
        else
        {
            wd->sta.txBeaconInd = 0;
        }

#ifdef ZM_ENABLE_IBSS_DELAYED_JOIN_INDICATION
        if ( wd->sta.txBeaconInd && wd->sta.ibssDelayedInd )
        {
            if (wd->zfcbIbssPartnerNotify != NULL)
            {
                wd->zfcbIbssPartnerNotify(dev, 1, &wd->sta.ibssDelayedIndEvent);
            }

            wd->sta.ibssDelayedInd = 0;
        }
#endif

        wd->sta.beaconTxCnt = txBeaconCounter;

        // Need to check if the time is expired after ATIM window??

        // Check if we have buffered any data for those stations that are sleeping
        // If it's true, then transmitting ATIM pkt to notify them

#ifdef ZM_ENABLE_IBSS_PS
        // TODO: Need to check if the station receive our ATIM pkt???
        zfStaIbssPSSend(dev);

        if ( wd->sta.atimWindow == 0 )
        {
            // We won't receive the end of ATIM isr so we fake it
            zfPowerSavingMgrAtimWinExpired(dev);
        }
#endif
    }
}

void zfEndOfAtimWindowInterrupt(zdev_t* dev)
{
#ifdef ZM_ENABLE_IBSS_PS
    zmw_get_wlan_dev(dev);

    if ( wd->wlanMode == ZM_MODE_IBSS )
    {
        // Transmit any queued pkt for the stations!!
        zfPowerSavingMgrAtimWinExpired(dev);
    }
#endif
}
