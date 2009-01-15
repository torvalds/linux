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
#include "../hal/hpreg.h"

/* TODO : change global variable to constant */
u8_t   zgWpaRadiusOui[] = { 0x00, 0x50, 0xf2, 0x01 };
u8_t   zgWpaAesOui[] = { 0x00, 0x50, 0xf2, 0x04 };
u8_t   zgWpa2RadiusOui[] = { 0x00, 0x0f, 0xac, 0x01 };
u8_t   zgWpa2AesOui[] = { 0x00, 0x0f, 0xac, 0x04 };

const u16_t zcCwTlb[16] = {   0,    1,    3,    7,   15,   31,   63,  127,
                            255,  511, 1023, 2047, 4095, 4095, 4095, 4095};

void zfStaStartConnectCb(zdev_t* dev);

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfStaPutApIntoBlockingList  */
/*      Put AP into blocking AP list.                                   */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      bssid : AP's BSSID                                              */
/*      weight : weight of AP                                           */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      none                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2006.12     */
/*                                                                      */
/************************************************************************/
void zfStaPutApIntoBlockingList(zdev_t* dev, u8_t* bssid, u8_t weight)
{
    u16_t i, j;
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    if (weight > 0)
    {
        zmw_enter_critical_section(dev);
        /*Find same bssid entry first*/
        for (i=0; i<ZM_MAX_BLOCKING_AP_LIST_SIZE; i++)
        {
            for (j=0; j<6; j++)
            {
                if(wd->sta.blockingApList[i].addr[j]!= bssid[j])
                {
                    break;
                }
            }

            if(j==6)
            {
                break;
            }
        }
        /*This bssid doesn't have old record.Find an empty entry*/
        if (i == ZM_MAX_BLOCKING_AP_LIST_SIZE)
        {
            for (i=0; i<ZM_MAX_BLOCKING_AP_LIST_SIZE; i++)
            {
                if (wd->sta.blockingApList[i].weight == 0)
                {
                    break;
                }
            }
        }

        /* If the list is full, pick one entry for replacement */
        if (i == ZM_MAX_BLOCKING_AP_LIST_SIZE)
        {
            i = bssid[5] & (ZM_MAX_BLOCKING_AP_LIST_SIZE-1);
        }

        /* Update AP address and weight */
        for (j=0; j<6; j++)
        {
            wd->sta.blockingApList[i].addr[j] = bssid[j];
        }

        wd->sta.blockingApList[i].weight = weight;
        zmw_leave_critical_section(dev);
    }

    return;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfStaIsApInBlockingList     */
/*      Is AP in blocking list.                                         */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      bssid : AP's BSSID                                              */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      TRUE : AP in blocking list                                      */
/*      FALSE : AP not in blocking list                                 */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2006.12     */
/*                                                                      */
/************************************************************************/
u16_t zfStaIsApInBlockingList(zdev_t* dev, u8_t* bssid)
{
    u16_t i, j;
    zmw_get_wlan_dev(dev);
    //zmw_declare_for_critical_section();

    //zmw_enter_critical_section(dev);
    for (i=0; i<ZM_MAX_BLOCKING_AP_LIST_SIZE; i++)
    {
        if (wd->sta.blockingApList[i].weight != 0)
        {
            for (j=0; j<6; j++)
            {
                if (wd->sta.blockingApList[i].addr[j] != bssid[j])
                {
                    break;
                }
            }
            if (j == 6)
            {
                //zmw_leave_critical_section(dev);
                return TRUE;
            }
        }
    }
    //zmw_leave_critical_section(dev);
    return FALSE;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfStaRefreshBlockList       */
/*      Is AP in blocking list.                                         */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      flushFlag : flush whole blocking list                           */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      none                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2006.12     */
/*                                                                      */
/************************************************************************/
void zfStaRefreshBlockList(zdev_t* dev, u16_t flushFlag)
{
    u16_t i;
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    for (i=0; i<ZM_MAX_BLOCKING_AP_LIST_SIZE; i++)
    {
        if (wd->sta.blockingApList[i].weight != 0)
        {
            if (flushFlag != 0)
            {
                wd->sta.blockingApList[i].weight = 0;
            }
            else
            {
                wd->sta.blockingApList[i].weight--;
            }
        }
    }
    zmw_leave_critical_section(dev);
    return;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfStaConnectFail            */
/*      Handle Connect failure.                                         */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      bssid : BSSID                                                   */
/*      reason : reason of failure                                      */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      none                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2006.12     */
/*                                                                      */
/************************************************************************/
void zfStaConnectFail(zdev_t* dev, u16_t reason, u16_t* bssid, u8_t weight)
{
    zmw_get_wlan_dev(dev);

    /* Change internal state */
    zfChangeAdapterState(dev, ZM_STA_STATE_DISCONNECT);

    /* Improve WEP/TKIP performace with HT AP, detail information please look bug#32495 */
    //zfHpSetTTSIFSTime(dev, 0x8);

    /* Notify wrapper of connection status changes */
    if (wd->zfcbConnectNotify != NULL)
    {
        wd->zfcbConnectNotify(dev, reason, bssid);
    }

    /* Put AP into internal blocking list */
    zfStaPutApIntoBlockingList(dev, (u8_t *)bssid, weight);

    /* Issue another SCAN */
    if ( wd->sta.bAutoReconnect )
    {
        zm_debug_msg0("Start internal scan...");
        zfScanMgrScanStop(dev, ZM_SCAN_MGR_SCAN_INTERNAL);
        zfScanMgrScanStart(dev, ZM_SCAN_MGR_SCAN_INTERNAL);
    }
}

u8_t zfiWlanIBSSGetPeerStationsCount(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    return wd->sta.oppositeCount;
}

u8_t zfiWlanIBSSIteratePeerStations(zdev_t* dev, u8_t numToIterate, zfpIBSSIteratePeerStationCb callback, void *ctx)
{
    u8_t oppositeCount;
    u8_t i;
    u8_t index = 0;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    oppositeCount = wd->sta.oppositeCount;
    if ( oppositeCount > numToIterate )
    {
        oppositeCount = numToIterate;
    }

    for(i=0; i < ZM_MAX_OPPOSITE_COUNT; i++)
    {
        if ( oppositeCount == 0 )
        {
            break;
        }

        if ( wd->sta.oppositeInfo[i].valid == 0 )
        {
            continue;
        }

        callback(dev, &wd->sta.oppositeInfo[i], ctx, index++);
        oppositeCount--;

    }

    zmw_leave_critical_section(dev);

    return index;
}


s8_t zfStaFindFreeOpposite(zdev_t* dev, u16_t *sa, int *pFoundIdx)
{
    int oppositeCount;
    int i;

    zmw_get_wlan_dev(dev);

    oppositeCount = wd->sta.oppositeCount;

    for(i=0; i < ZM_MAX_OPPOSITE_COUNT; i++)
    {
        if ( oppositeCount == 0 )
        {
            break;
        }

        if ( wd->sta.oppositeInfo[i].valid == 0 )
        {
            continue;
        }

        oppositeCount--;
        if ( zfMemoryIsEqual((u8_t*) sa, wd->sta.oppositeInfo[i].macAddr, 6) )
        {
            //wd->sta.oppositeInfo[i].aliveCounter++;
            wd->sta.oppositeInfo[i].aliveCounter = ZM_IBSS_PEER_ALIVE_COUNTER;

            /* it is already stored */
            return 1;
        }
    }

    // Check if there's still space for new comer
    if ( wd->sta.oppositeCount == ZM_MAX_OPPOSITE_COUNT )
    {
        return -1;
    }

    // Find an unused slot for new peer station
    for(i=0; i < ZM_MAX_OPPOSITE_COUNT; i++)
    {
        if ( wd->sta.oppositeInfo[i].valid == 0 )
        {
            break;
        }
    }

    *pFoundIdx = i;
    return 0;
}

s8_t zfStaFindOppositeByMACAddr(zdev_t* dev, u16_t *sa, u8_t *pFoundIdx)
{
    u32_t oppositeCount;
    u32_t i;

    zmw_get_wlan_dev(dev);

    oppositeCount = wd->sta.oppositeCount;

    for(i=0; i < ZM_MAX_OPPOSITE_COUNT; i++)
    {
        if ( oppositeCount == 0 )
        {
            break;
        }

        if ( wd->sta.oppositeInfo[i].valid == 0 )
        {
            continue;
        }

        oppositeCount--;
        if ( zfMemoryIsEqual((u8_t*) sa, wd->sta.oppositeInfo[i].macAddr, 6) )
        {
            *pFoundIdx = (u8_t)i;

            return 0;
        }
    }

    *pFoundIdx = 0;
    return 1;
}

static void zfStaInitCommonOppositeInfo(zdev_t* dev, int i)
{
    zmw_get_wlan_dev(dev);

    /* set the default rate to the highest rate */
    wd->sta.oppositeInfo[i].valid = 1;
    wd->sta.oppositeInfo[i].aliveCounter = ZM_IBSS_PEER_ALIVE_COUNTER;
    wd->sta.oppositeCount++;

#ifdef ZM_ENABLE_IBSS_WPA2PSK
    /* Set parameters for new opposite peer station !!! */
    wd->sta.oppositeInfo[i].camIdx = 0xff;  // Not set key in this location
    wd->sta.oppositeInfo[i].pkInstalled = 0;
    wd->sta.oppositeInfo[i].wpaState = ZM_STA_WPA_STATE_INIT ;  // No encryption
#endif
}

int zfStaSetOppositeInfoFromBSSInfo(zdev_t* dev, struct zsBssInfo* pBssInfo)
{
    int i;
    u8_t*  dst;
    u16_t  sa[3];
    int res;
    u32_t oneTxStreamCap;

    zmw_get_wlan_dev(dev);

    zfMemoryCopy((u8_t*) sa, pBssInfo->macaddr, 6);

    res = zfStaFindFreeOpposite(dev, sa, &i);
    if ( res != 0 )
    {
        goto zlReturn;
    }

    dst = wd->sta.oppositeInfo[i].macAddr;
    zfMemoryCopy(dst, (u8_t *)sa, 6);

    oneTxStreamCap = (zfHpCapability(dev) & ZM_HP_CAP_11N_ONE_TX_STREAM);

    if (pBssInfo->extSupportedRates[1] != 0)
    {
        /* TODO : Handle 11n */
        if (pBssInfo->frequency < 3000)
        {
            /* 2.4GHz */
            if (pBssInfo->EnableHT == 1)
                zfRateCtrlInitCell(dev, &wd->sta.oppositeInfo[i].rcCell, (oneTxStreamCap!=0)?3:2, 1, pBssInfo->SG40);
            else
                zfRateCtrlInitCell(dev, &wd->sta.oppositeInfo[i].rcCell, 1, 1, pBssInfo->SG40);
        }
        else
        {
            /* 5GHz */
            if (pBssInfo->EnableHT == 1)
                zfRateCtrlInitCell(dev, &wd->sta.oppositeInfo[i].rcCell, (oneTxStreamCap!=0)?3:2, 0, pBssInfo->SG40);
            else
                zfRateCtrlInitCell(dev, &wd->sta.oppositeInfo[i].rcCell, 1, 0, pBssInfo->SG40);
        }
    }
    else
    {
        /* TODO : Handle 11n */
        if (pBssInfo->frequency < 3000)
        {
            /* 2.4GHz */
            if (pBssInfo->EnableHT == 1)
                zfRateCtrlInitCell(dev, &wd->sta.oppositeInfo[i].rcCell, (oneTxStreamCap!=0)?3:2, 1, pBssInfo->SG40);
            else
                zfRateCtrlInitCell(dev, &wd->sta.oppositeInfo[i].rcCell, 0, 1, pBssInfo->SG40);
        }
        else
        {
            /* 5GHz */
            if (pBssInfo->EnableHT == 1)
                zfRateCtrlInitCell(dev, &wd->sta.oppositeInfo[i].rcCell, (oneTxStreamCap!=0)?3:2, 0, pBssInfo->SG40);
            else
                zfRateCtrlInitCell(dev, &wd->sta.oppositeInfo[i].rcCell, 1, 0, pBssInfo->SG40);
        }
    }


    zfStaInitCommonOppositeInfo(dev, i);
zlReturn:
    return 0;
}

int zfStaSetOppositeInfoFromRxBuf(zdev_t* dev, zbuf_t* buf)
{
    int   i;
    u8_t*  dst;
    u16_t  sa[3];
    int res = 0;
    u16_t  offset;
    u8_t   bSupportExtRate;
    u32_t rtsctsRate = 0xffffffff; /* CTS:OFDM 6M, RTS:OFDM 6M */
    u32_t oneTxStreamCap;

    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    sa[0] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A2_OFFSET);
    sa[1] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A2_OFFSET+2);
    sa[2] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A2_OFFSET+4);

    zmw_enter_critical_section(dev);

    res = zfStaFindFreeOpposite(dev, sa, &i);
    if ( res != 0 )
    {
        goto zlReturn;
    }

    dst = wd->sta.oppositeInfo[i].macAddr;
    zfCopyFromRxBuffer(dev, buf, dst, ZM_WLAN_HEADER_A2_OFFSET, 6);

    if ( (wd->sta.currentFrequency < 3000) && !(wd->supportMode & (ZM_WIRELESS_MODE_24_54|ZM_WIRELESS_MODE_24_N)) )
    {
        bSupportExtRate = 0;
    } else {
        bSupportExtRate = 1;
    }

    if ( (bSupportExtRate == 1)
         && (wd->sta.currentFrequency < 3000)
         && (wd->wlanMode == ZM_MODE_IBSS)
         && (wd->wfc.bIbssGMode == 0) )
    {
        bSupportExtRate = 0;
    }

    wd->sta.connection_11b = 0;
    oneTxStreamCap = (zfHpCapability(dev) & ZM_HP_CAP_11N_ONE_TX_STREAM);

    if ( ((offset = zfFindElement(dev, buf, ZM_WLAN_EID_EXTENDED_RATE)) != 0xffff)
         && (bSupportExtRate == 1) )
    {
        /* TODO : Handle 11n */
        if (wd->sta.currentFrequency < 3000)
        {
            /* 2.4GHz */
            if (wd->sta.EnableHT == 1)
            {
                //11ng
                zfRateCtrlInitCell(dev, &wd->sta.oppositeInfo[i].rcCell, (oneTxStreamCap!=0)?3:2, 1, wd->sta.SG40);
            }
            else
            {
                //11g
                zfRateCtrlInitCell(dev, &wd->sta.oppositeInfo[i].rcCell, 1, 1, wd->sta.SG40);
            }
            rtsctsRate = 0x00001bb; /* CTS:CCK 1M, RTS:OFDM 6M */
        }
        else
        {
            /* 5GHz */
            if (wd->sta.EnableHT == 1)
            {
                //11na
                zfRateCtrlInitCell(dev, &wd->sta.oppositeInfo[i].rcCell, (oneTxStreamCap!=0)?3:2, 0, wd->sta.SG40);
            }
            else
            {
                //11a
                zfRateCtrlInitCell(dev, &wd->sta.oppositeInfo[i].rcCell, 1, 0, wd->sta.SG40);
            }
            rtsctsRate = 0x10b01bb; /* CTS:OFDM 6M, RTS:OFDM 6M */
        }
    }
    else
    {
        /* TODO : Handle 11n */
        if (wd->sta.currentFrequency < 3000)
        {
            /* 2.4GHz */
            if (wd->sta.EnableHT == 1)
            {
                //11ng
                zfRateCtrlInitCell(dev, &wd->sta.oppositeInfo[i].rcCell, (oneTxStreamCap!=0)?3:2, 1, wd->sta.SG40);
                rtsctsRate = 0x00001bb; /* CTS:CCK 1M, RTS:OFDM 6M */
            }
            else
            {
                //11b
                zfRateCtrlInitCell(dev, &wd->sta.oppositeInfo[i].rcCell, 0, 1, wd->sta.SG40);
                rtsctsRate = 0x0; /* CTS:CCK 1M, RTS:CCK 1M */
                wd->sta.connection_11b = 1;
            }
        }
        else
        {
            /* 5GHz */
            if (wd->sta.EnableHT == 1)
            {
                //11na
                zfRateCtrlInitCell(dev, &wd->sta.oppositeInfo[i].rcCell, (oneTxStreamCap!=0)?3:2, 0, wd->sta.SG40);
            }
            else
            {
                //11a
                zfRateCtrlInitCell(dev, &wd->sta.oppositeInfo[i].rcCell, 1, 0, wd->sta.SG40);
            }
            rtsctsRate = 0x10b01bb; /* CTS:OFDM 6M, RTS:OFDM 6M */
        }
    }

    zfStaInitCommonOppositeInfo(dev, i);

zlReturn:
    zmw_leave_critical_section(dev);

    if (rtsctsRate != 0xffffffff)
    {
        zfHpSetRTSCTSRate(dev, rtsctsRate);
    }
    return res;
}

void zfStaProtErpMonitor(zdev_t* dev, zbuf_t* buf)
{
    u16_t   offset;
    u8_t    erp;
    u8_t    bssid[6];

    zmw_get_wlan_dev(dev);

    if ( (wd->wlanMode == ZM_MODE_INFRASTRUCTURE)&&(zfStaIsConnected(dev)) )
    {
        ZM_MAC_WORD_TO_BYTE(wd->sta.bssid, bssid);

        if (zfRxBufferEqualToStr(dev, buf, bssid, ZM_WLAN_HEADER_A2_OFFSET, 6))
        {
            if ( (offset=zfFindElement(dev, buf, ZM_WLAN_EID_ERP)) != 0xffff )
            {
                erp = zmw_rx_buf_readb(dev, buf, offset+2);

                if ( erp & ZM_BIT_1 )
                {
                    //zm_debug_msg0("protection mode on");
                    if (wd->sta.bProtectionMode == FALSE)
                    {
                        wd->sta.bProtectionMode = TRUE;
                        zfHpSetSlotTime(dev, 0);
                    }
                }
                else
                {
                    //zm_debug_msg0("protection mode off");
                    if (wd->sta.bProtectionMode == TRUE)
                    {
                        wd->sta.bProtectionMode = FALSE;
                        zfHpSetSlotTime(dev, 1);
                    }
                }
            }
        }
		//Check the existence of Non-N AP
		//Follow the check the "pBssInfo->EnableHT"
			if ((offset = zfFindElement(dev, buf, ZM_WLAN_EID_HT_CAPABILITY)) != 0xffff)
			{}
			else if ((offset = zfFindElement(dev, buf, ZM_WLAN_PREN2_EID_HTCAPABILITY)) != 0xffff)
			{}
			else
			{wd->sta.NonNAPcount++;}
    }
}

void zfStaUpdateWmeParameter(zdev_t* dev, zbuf_t* buf)
{
    u16_t   tmp;
    u16_t   aifs[5];
    u16_t   cwmin[5];
    u16_t   cwmax[5];
    u16_t   txop[5];
    u8_t    acm;
    u8_t    ac;
    u16_t   len;
    u16_t   i;
   	u16_t   offset;
    u8_t    rxWmeParameterSetCount;

    zmw_get_wlan_dev(dev);

    /* Update if WME parameter set count is changed */
    /* If connect to WME AP */
    if (wd->sta.wmeConnected != 0)
    {
        /* Find WME parameter element */
        if ((offset = zfFindWifiElement(dev, buf, 2, 1)) != 0xffff)
        {
            if ((len = zmw_rx_buf_readb(dev, buf, offset+1)) >= 7)
            {
                rxWmeParameterSetCount=zmw_rx_buf_readb(dev, buf, offset+8);
                if (rxWmeParameterSetCount != wd->sta.wmeParameterSetCount)
                {
                    zm_msg0_mm(ZM_LV_0, "wmeParameterSetCount changed!");
                    wd->sta.wmeParameterSetCount = rxWmeParameterSetCount;
                    /* retrieve WME parameter and update TxQ parameters */
                    acm = 0xf;
                    for (i=0; i<4; i++)
                    {
                        if (len >= (8+(i*4)+4))
                        {
                            tmp=zmw_rx_buf_readb(dev, buf, offset+10+i*4);
                            ac = (tmp >> 5) & 0x3;
                            if ((tmp & 0x10) == 0)
                            {
                                acm &= (~(1<<ac));
                            }
                            aifs[ac] = ((tmp & 0xf) * 9) + 10;
                            tmp=zmw_rx_buf_readb(dev, buf, offset+11+i*4);
                            /* Convert to 2^n */
                            cwmin[ac] = zcCwTlb[(tmp & 0xf)];
                            cwmax[ac] = zcCwTlb[(tmp >> 4)];
                            txop[ac]=zmw_rx_buf_readh(dev, buf,
                                    offset+12+i*4);
                        }
                    }

                    if ((acm & 0x4) != 0)
                    {
                        cwmin[2] = cwmin[0];
                        cwmax[2] = cwmax[0];
                        aifs[2] = aifs[0];
                        txop[2] = txop[0];
                    }
                    if ((acm & 0x8) != 0)
                    {
                        cwmin[3] = cwmin[2];
                        cwmax[3] = cwmax[2];
                        aifs[3] = aifs[2];
                        txop[3] = txop[2];
                    }
                    cwmin[4] = 3;
                    cwmax[4] = 7;
                    aifs[4] = 28;

                    if ((cwmin[2]+aifs[2]) > ((cwmin[0]+aifs[0])+1))
                    {
                        wd->sta.ac0PriorityHigherThanAc2 = 1;
                    }
                    else
                    {
                        wd->sta.ac0PriorityHigherThanAc2 = 0;
                    }
                    zfHpUpdateQosParameter(dev, cwmin, cwmax, aifs, txop);
                }
            }
        }
    } //if (wd->sta.wmeConnected != 0)
}
/* process 802.11h Dynamic Frequency Selection */
void zfStaUpdateDot11HDFS(zdev_t* dev, zbuf_t* buf)
{
    zmw_get_wlan_dev(dev);

    /*
    Channel Switch Announcement Element Format
    +------+----------+------+-------------------+------------------+--------------------+
    |Format|Element ID|Length|Channel Switch Mode|New Channel Number|Channel Switch Count|
    +------+----------+------+-------------------+------------------+--------------------+
    |Bytes |   1      |  1   |	     1           |       1          |          1         |
    +------+----------+------+-------------------+------------------+--------------------+
    |Value |   37     |  3   |       0 or 1      |unsigned integer  |unsigned integer    |
    +------+----------+------+-------------------+------------------+--------------------+
    */
    //u8_t    length, channel, is5G;
    u16_t   offset;

    /* get EID(Channel Switch Announcement) */
    if ( (offset = zfFindElement(dev, buf, ZM_WLAN_EID_CHANNEL_SWITCH_ANNOUNCE)) == 0xffff )
    {
        //zm_debug_msg0("EID(Channel Switch Announcement) not found");
        return;
    }
    else if ( zmw_rx_buf_readb(dev, buf, offset+1) == 0x3 )
    {
        zm_debug_msg0("EID(Channel Switch Announcement) found");

        //length = zmw_rx_buf_readb(dev, buf, offset+1);
        //zfCopyFromRxBuffer(dev, buf, pBssInfo->supportedRates, offset, length+2);

        //Chanell Switch Mode set to 1, driver should disable transmit immediate
        //we do this by poll CCA high
        if (zmw_rx_buf_readb(dev, buf, offset+2) == 0x1 )
        {
        	//use ZM_OID_INTERNAL_WRITE,ZM_CMD_RESET to notice firmware flush quene and stop dma,
        	//then restart rx dma but not tx dma
        	if (wd->sta.DFSDisableTx != TRUE)
        	{
                /* TODO : zfHpResetTxRx would cause Rx hang */
                //zfHpResetTxRx(dev);
                wd->sta.DFSDisableTx = TRUE;
                /* Trgger Rx DMA */
                zfHpStartRecv(dev);
            }
        	//Adapter->ZD80211HSetting.DisableTxBy80211H=TRUE;
        	//AcquireCtrOfPhyReg(Adapter);
        	//ZD1205_WRITE_REGISTER(Adapter,CR24, 0x0);
        	//ReleaseDoNotSleep(Adapter);
        }

        if (zmw_rx_buf_readb(dev, buf, offset+4) <= 0x2 )
        {
        	//Channel Switch
        	//if Channel Switch Count = 0 , STA should change channel immediately.
        	//if Channel Switch Count > 0 , STA should change channel after TBTT*count
        	//But it won't be accurate to let driver calculate TBTT*count, and the value of
        	//Channel Switch Count will decrease by one each when continue receving beacon
        	//So we change channel here when we receive count <=2.

            zfHpDeleteAllowChannel(dev, wd->sta.currentFrequency);
        	wd->frequency = zfChNumToFreq(dev, zmw_rx_buf_readb(dev, buf, offset+3), 0);
        	//zfHpAddAllowChannel(dev, wd->frequency);
        	zm_debug_msg1("CWY - jump to frequency = ", wd->frequency);
        	zfCoreSetFrequency(dev, wd->frequency);
        	wd->sta.DFSDisableTx = FALSE;
            /* Increase rxBeaconCount to prevent beacon lost */
            if (zfStaIsConnected(dev))
            {
                wd->sta.rxBeaconCount = 1 << 6; // 2 times of check would pass
            }
        	//start tx dma to transmit packet

        	//if (zmw_rx_buf_readb(dev, buf, offset+3) != wd->frequency)
        	//{
        	//	//ZDDbgPrint(("Radar Detect by AP\n"));
        	//	zfCoreSetFrequency();
        	//	ProcessRadarDetectEvent(Adapter);
        	//	Set_RF_Channel(Adapter, SwRfd->Rfd->RxBuffer[index+3], (UCHAR)Adapter->RF_Mode, 1);
        	//	Adapter->CardSetting.Channel = SwRfd->Rfd->RxBuffer[index+3];
        	//	Adapter->SaveChannel = Adapter->CardSetting.Channel;
        	//	Adapter->UtilityChannel = Adapter->CardSetting.Channel;
        	//}
        }
    }

}
/* TODO : process 802.11h Transmission Power Control */
void zfStaUpdateDot11HTPC(zdev_t* dev, zbuf_t* buf)
{
}

/* IBSS power-saving mode */
void zfStaIbssPSCheckState(zdev_t* dev, zbuf_t* buf)
{
    u8_t   i, frameCtrl;

    zmw_get_wlan_dev(dev);

    if ( !zfStaIsConnected(dev) )
    {
        return;
    }

    if ( wd->wlanMode != ZM_MODE_IBSS )
    {
        return ;
    }

    /* check BSSID */
    if ( !zfRxBufferEqualToStr(dev, buf, (u8_t*) wd->sta.bssid,
                               ZM_WLAN_HEADER_A3_OFFSET, 6) )
    {
        return;
    }

    frameCtrl = zmw_rx_buf_readb(dev, buf, 1);

    /* check power management bit */
    if ( frameCtrl & ZM_BIT_4 )
    {
        for(i=1; i<ZM_MAX_PS_STA; i++)
        {
            if ( !wd->sta.staPSList.entity[i].bUsed )
            {
                continue;
            }

            /* check source address */
            if ( zfRxBufferEqualToStr(dev, buf,
                                      wd->sta.staPSList.entity[i].macAddr,
                                      ZM_WLAN_HEADER_A2_OFFSET, 6) )
            {
                return;
            }
        }

        for(i=1; i<ZM_MAX_PS_STA; i++)
        {
            if ( !wd->sta.staPSList.entity[i].bUsed )
            {
                wd->sta.staPSList.entity[i].bUsed = TRUE;
                wd->sta.staPSList.entity[i].bDataQueued = FALSE;
                break;
            }
        }

        if ( i == ZM_MAX_PS_STA )
        {
            /* STA list is full */
            return;
        }

        zfCopyFromRxBuffer(dev, buf, wd->sta.staPSList.entity[i].macAddr,
                           ZM_WLAN_HEADER_A2_OFFSET, 6);

        if ( wd->sta.staPSList.count == 0 )
        {
            // enable ATIM window
            //zfEnableAtimWindow(dev);
        }

        wd->sta.staPSList.count++;
    }
    else if ( wd->sta.staPSList.count )
    {
        for(i=1; i<ZM_MAX_PS_STA; i++)
        {
            if ( wd->sta.staPSList.entity[i].bUsed )
            {
                if ( zfRxBufferEqualToStr(dev, buf,
                                          wd->sta.staPSList.entity[i].macAddr,
                                          ZM_WLAN_HEADER_A2_OFFSET, 6) )
                {
                    wd->sta.staPSList.entity[i].bUsed = FALSE;
                    wd->sta.staPSList.count--;

                    if ( wd->sta.staPSList.entity[i].bDataQueued )
                    {
                        /* send queued data */
                    }
                }
            }
        }

        if ( wd->sta.staPSList.count == 0 )
        {
            /* disable ATIM window */
            //zfDisableAtimWindow(dev);
        }

    }
}

/* IBSS power-saving mode */
u8_t zfStaIbssPSQueueData(zdev_t* dev, zbuf_t* buf)
{
    u8_t   i;
    u16_t  da[3];

    zmw_get_wlan_dev(dev);

    if ( !zfStaIsConnected(dev) )
    {
        return 0;
    }

    if ( wd->wlanMode != ZM_MODE_IBSS )
    {
        return 0;
    }

    if ( wd->sta.staPSList.count == 0 && wd->sta.powerSaveMode <= ZM_STA_PS_NONE )
    {
        return 0;
    }

    /* DA */
#ifdef ZM_ENABLE_NATIVE_WIFI
    da[0] = zmw_tx_buf_readh(dev, buf, ZM_WLAN_HEADER_A1_OFFSET);
    da[1] = zmw_tx_buf_readh(dev, buf, ZM_WLAN_HEADER_A1_OFFSET + 2);
    da[2] = zmw_tx_buf_readh(dev, buf, ZM_WLAN_HEADER_A1_OFFSET + 4);
#else
    da[0] = zmw_tx_buf_readh(dev, buf, 0);
    da[1] = zmw_tx_buf_readh(dev, buf, 2);
    da[2] = zmw_tx_buf_readh(dev, buf, 4);
#endif

    if ( ZM_IS_MULTICAST_OR_BROADCAST(da) )
    {
        wd->sta.staPSList.entity[0].bDataQueued = TRUE;
        wd->sta.ibssPSDataQueue[wd->sta.ibssPSDataCount++] = buf;
        return 1;
    }

    // Unicast packet...

    for(i=1; i<ZM_MAX_PS_STA; i++)
    {
        if ( zfMemoryIsEqual(wd->sta.staPSList.entity[i].macAddr,
                             (u8_t*) da, 6) )
        {
            wd->sta.staPSList.entity[i].bDataQueued = TRUE;
            wd->sta.ibssPSDataQueue[wd->sta.ibssPSDataCount++] = buf;

            return 1;
        }
    }

#if 0
    if ( wd->sta.powerSaveMode > ZM_STA_PS_NONE )
    {
        wd->sta.staPSDataQueue[wd->sta.staPSDataCount++] = buf;

        return 1;
    }
#endif

    return 0;
}

/* IBSS power-saving mode */
void zfStaIbssPSSend(zdev_t* dev)
{
    u8_t   i;
    u16_t  bcastAddr[3] = {0xffff, 0xffff, 0xffff};

    zmw_get_wlan_dev(dev);

    if ( !zfStaIsConnected(dev) )
    {
        return ;
    }

    if ( wd->wlanMode != ZM_MODE_IBSS )
    {
        return ;
    }

    for(i=0; i<ZM_MAX_PS_STA; i++)
    {
        if ( wd->sta.staPSList.entity[i].bDataQueued )
        {
            if ( i == 0 )
            {
                zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_ATIM,
                              bcastAddr,
                              0, 0, 0);
            }
            else if ( wd->sta.staPSList.entity[i].bUsed )
            {
                // Send ATIM to prevent the peer to go to sleep
                zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_ATIM,
                              (u16_t*) wd->sta.staPSList.entity[i].macAddr,
                              0, 0, 0);
            }

            wd->sta.staPSList.entity[i].bDataQueued = FALSE;
        }
    }

    for(i=0; i<wd->sta.ibssPSDataCount; i++)
    {
        zfTxSendEth(dev, wd->sta.ibssPSDataQueue[i], 0,
                    ZM_EXTERNAL_ALLOC_BUF, 0);
    }

    wd->sta.ibssPrevPSDataCount = wd->sta.ibssPSDataCount;
    wd->sta.ibssPSDataCount = 0;
}


void zfStaReconnect(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    if ( wd->wlanMode != ZM_MODE_INFRASTRUCTURE &&
         wd->wlanMode != ZM_MODE_IBSS )
    {
        return;
    }

    if ( (zfStaIsConnected(dev))||(zfStaIsConnecting(dev)) )
    {
        return;
    }

    if ( wd->sta.bChannelScan )
    {
        return;
    }

    /* Recover zero SSID length  */
    if ( (wd->wlanMode == ZM_MODE_INFRASTRUCTURE) && (wd->ws.ssidLen == 0))
    {
        zm_debug_msg0("zfStaReconnect: NOT Support!! Set SSID to any BSS");
        /* ANY BSS */
        zmw_enter_critical_section(dev);
        wd->sta.ssid[0] = 0;
        wd->sta.ssidLen = 0;
        zmw_leave_critical_section(dev);
    }

    // RAY: To ensure no TX pending before re-connecting
    zfFlushVtxq(dev);
    zfWlanEnable(dev);
    zfScanMgrScanAck(dev);
}

void zfStaTimer100ms(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    if ( (wd->tick % 10) == 0 )
    {
        zfPushVtxq(dev);
//        zfPowerSavingMgrMain(dev);
    }
}


void zfStaCheckRxBeacon(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    if (( wd->wlanMode == ZM_MODE_INFRASTRUCTURE ) && (zfStaIsConnected(dev)))
    {
        if (wd->beaconInterval == 0)
        {
            wd->beaconInterval = 100;
        }
        if ( (wd->tick % ((wd->beaconInterval * 10) / ZM_MS_PER_TICK)) == 0 )
        {
            /* Check rxBeaconCount */
            if (wd->sta.rxBeaconCount == 0)
            {
                if (wd->sta.beaconMissState == 1)
                {
            	/*notify AP that we left*/
            	zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_DEAUTH, wd->sta.bssid, 3, 0, 0);
                /* Beacon Lost */
                zfStaConnectFail(dev, ZM_STATUS_MEDIA_DISCONNECT_BEACON_MISS,
                        wd->sta.bssid, 0);
                }
                else
                {
                    wd->sta.beaconMissState = 1;
                    /* Reset channel */
                    zfCoreSetFrequencyExV2(dev, wd->frequency, wd->BandWidth40,
                            wd->ExtOffset, NULL, 1);
                }
            }
            else
            {
                wd->sta.beaconMissState = 0;
            }
            wd->sta.rxBeaconCount = 0;
        }
    }
}



void zfStaCheckConnectTimeout(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    if ( wd->wlanMode != ZM_MODE_INFRASTRUCTURE )
    {
        return;
    }

    if ( !zfStaIsConnecting(dev) )
    {
        return;
    }

    zmw_enter_critical_section(dev);
    if ( (wd->sta.connectState == ZM_STA_CONN_STATE_AUTH_OPEN)||
         (wd->sta.connectState == ZM_STA_CONN_STATE_AUTH_SHARE_1)||
         (wd->sta.connectState == ZM_STA_CONN_STATE_AUTH_SHARE_2)||
         (wd->sta.connectState == ZM_STA_CONN_STATE_ASSOCIATE) )
    {
        if ( (wd->tick - wd->sta.connectTimer) > ZM_INTERVAL_CONNECT_TIMEOUT )
        {
            if ( wd->sta.connectByReasso )
            {
                wd->sta.failCntOfReasso++;
                if ( wd->sta.failCntOfReasso > 2 )
                {
                    wd->sta.connectByReasso = FALSE;
                }
            }

            wd->sta.connectState = ZM_STA_CONN_STATE_NONE;
            zm_debug_msg1("connect timeout, state = ", wd->sta.connectState);
            //zfiWlanDisable(dev);
            goto failed;
        }
    }

    zmw_leave_critical_section(dev);
    return;

failed:
    zmw_leave_critical_section(dev);
    if(wd->sta.authMode == ZM_AUTH_MODE_AUTO)
	{ // Fix some AP not send authentication failed message to sta and lead to connect timeout !
            wd->sta.connectTimeoutCount++;
	}
    zfStaConnectFail(dev, ZM_STATUS_MEDIA_DISCONNECT_TIMEOUT, wd->sta.bssid, 2);
    return;
}

void zfMmStaTimeTick(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    /* airopeek */
    if (wd->wlanMode != ZM_MODE_AP && !wd->swSniffer)
    {
        if ( wd->tick & 1 )
        {
            zfTimerCheckAndHandle(dev);
        }

        zfStaCheckRxBeacon(dev);
        zfStaTimer100ms(dev);
        zfStaCheckConnectTimeout(dev);
        zfPowerSavingMgrMain(dev);
    }

#ifdef ZM_ENABLE_AGGREGATION
    /*
     * add by honda
     */
    zfAggScanAndClear(dev, wd->tick);
#endif
}

void zfStaSendBeacon(zdev_t* dev)
{
    zbuf_t* buf;
    u16_t offset, seq;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    //zm_debug_msg0("\n");

    /* TBD : Maximum size of beacon */
    if ((buf = zfwBufAllocate(dev, 1024)) == NULL)
    {
        zm_debug_msg0("Allocate beacon buffer failed");
        return;
    }

    offset = 0;
    /* wlan header */
    /* Frame control */
    zmw_tx_buf_writeh(dev, buf, offset, 0x0080);
    offset+=2;
    /* Duration */
    zmw_tx_buf_writeh(dev, buf, offset, 0x0000);
    offset+=2;
    /* Address 1 */
    zmw_tx_buf_writeh(dev, buf, offset, 0xffff);
    offset+=2;
    zmw_tx_buf_writeh(dev, buf, offset, 0xffff);
    offset+=2;
    zmw_tx_buf_writeh(dev, buf, offset, 0xffff);
    offset+=2;
    /* Address 2 */
    zmw_tx_buf_writeh(dev, buf, offset, wd->macAddr[0]);
    offset+=2;
    zmw_tx_buf_writeh(dev, buf, offset, wd->macAddr[1]);
    offset+=2;
    zmw_tx_buf_writeh(dev, buf, offset, wd->macAddr[2]);
    offset+=2;
    /* Address 3 */
    zmw_tx_buf_writeh(dev, buf, offset, wd->sta.bssid[0]);
    offset+=2;
    zmw_tx_buf_writeh(dev, buf, offset, wd->sta.bssid[1]);
    offset+=2;
    zmw_tx_buf_writeh(dev, buf, offset, wd->sta.bssid[2]);
    offset+=2;

    /* Sequence number */
    zmw_enter_critical_section(dev);
    seq = ((wd->mmseq++)<<4);
    zmw_leave_critical_section(dev);
    zmw_tx_buf_writeh(dev, buf, offset, seq);
    offset+=2;

    /* 24-31 Time Stamp : hardware will fill this field */
    offset+=8;

    /* Beacon Interval */
    zmw_tx_buf_writeh(dev, buf, offset, wd->beaconInterval);
    offset+=2;

    /* Capability */
    zmw_tx_buf_writeb(dev, buf, offset++, wd->sta.capability[0]);
    zmw_tx_buf_writeb(dev, buf, offset++, wd->sta.capability[1]);

    /* SSID */
    offset = zfStaAddIeSsid(dev, buf, offset);

    if(wd->frequency <= ZM_CH_G_14)  // 2.4 GHz  b+g
    {

    	/* Support Rate */
    	offset = zfMmAddIeSupportRate(dev, buf, offset,
                                  		ZM_WLAN_EID_SUPPORT_RATE, ZM_RATE_SET_CCK);

    	/* DS parameter set */
    	offset = zfMmAddIeDs(dev, buf, offset);

    	offset = zfStaAddIeIbss(dev, buf, offset);

        if( wd->wfc.bIbssGMode
            && (wd->supportMode & (ZM_WIRELESS_MODE_24_54|ZM_WIRELESS_MODE_24_N)) )    // Only accompany with enabling a mode .
        {
      	    /* ERP Information */
       	    wd->erpElement = 0;
       	    offset = zfMmAddIeErp(dev, buf, offset);
       	}

       	/* TODO : country information */
        /* RSN */
        if ( wd->sta.authMode == ZM_AUTH_MODE_WPA2PSK )
        {
            offset = zfwStaAddIeWpaRsn(dev, buf, offset, ZM_WLAN_FRAME_TYPE_AUTH);
        }

        if( wd->wfc.bIbssGMode
            && (wd->supportMode & (ZM_WIRELESS_MODE_24_54|ZM_WIRELESS_MODE_24_N)) )    // Only accompany with enabling a mode .
        {
            /* Enable G Mode */
            /* Extended Supported Rates */
       	    offset = zfMmAddIeSupportRate(dev, buf, offset,
                                   		    ZM_WLAN_EID_EXTENDED_RATE, ZM_RATE_SET_OFDM);
	    }
    }
    else    // 5GHz a
    {
        /* Support Rate a Mode */
    	offset = zfMmAddIeSupportRate(dev, buf, offset,
        	                            ZM_WLAN_EID_SUPPORT_RATE, ZM_RATE_SET_OFDM);

        /* DS parameter set */
    	offset = zfMmAddIeDs(dev, buf, offset);

    	offset = zfStaAddIeIbss(dev, buf, offset);

        /* TODO : country information */
        /* RSN */
        if ( wd->sta.authMode == ZM_AUTH_MODE_WPA2PSK )
        {
            offset = zfwStaAddIeWpaRsn(dev, buf, offset, ZM_WLAN_FRAME_TYPE_AUTH);
        }
    }

    if ( wd->wlanMode != ZM_MODE_IBSS )
    {
        /* TODO : Need to check if it is ok */
        /* HT Capabilities Info */
        offset = zfMmAddHTCapability(dev, buf, offset);

        /* Extended HT Capabilities Info */
        offset = zfMmAddExtendedHTCapability(dev, buf, offset);
    }

    if ( wd->sta.ibssAdditionalIESize )
        offset = zfStaAddIbssAdditionalIE(dev, buf, offset);

    /* 1212 : write to beacon fifo */
    /* 1221 : write to share memory */
    zfHpSendBeacon(dev, buf, offset);

    /* Free beacon buffer */
    //zfwBufFree(dev, buf, 0);
}

void zfStaSignalStatistic(zdev_t* dev, u8_t SignalStrength, u8_t SignalQuality) //CWYang(+)
{
    zmw_get_wlan_dev(dev);

    /* Add Your Code to Do Works Like Moving Average Here */
    wd->SignalStrength = (wd->SignalStrength * 7 + SignalStrength * 3)/10;
    wd->SignalQuality = (wd->SignalQuality * 7 + SignalQuality * 3)/10;

}

struct zsBssInfo* zfStaFindBssInfo(zdev_t* dev, zbuf_t* buf, struct zsWlanProbeRspFrameHeader *pProbeRspHeader)
{
    u8_t    i;
    u8_t    j;
    u8_t    k;
    u8_t    isMatched, length, channel;
    u16_t   offset, frequency;
    struct zsBssInfo* pBssInfo;

    zmw_get_wlan_dev(dev);

    if ((pBssInfo = wd->sta.bssList.head) == NULL)
    {
        return NULL;
    }

    for( i=0; i<wd->sta.bssList.bssCount; i++ )
    {
        //zm_debug_msg2("check pBssInfo = ", pBssInfo);

        /* Check BSSID */
        for( j=0; j<6; j++ )
        {
            if ( pBssInfo->bssid[j] != pProbeRspHeader->bssid[j] )
            {
                break;
            }
        }

		/* Check SSID */
        if (j == 6)
        {
            if (pProbeRspHeader->ssid[1] <= 32)
            {
                /* compare length and ssid */
                isMatched = 1;
				if((pProbeRspHeader->ssid[1] != 0) && (pBssInfo->ssid[1] != 0))
				{
                for( k=1; k<pProbeRspHeader->ssid[1] + 1; k++ )
                {
                    if ( pBssInfo->ssid[k] != pProbeRspHeader->ssid[k] )
                    {
                        isMatched = 0;
                        break;
                    }
                }
            }
            }
            else
            {
                isMatched = 0;
            }
        }
        else
        {
            isMatched = 0;
        }

        /* Check channel */
        /* Add check channel to solve the bug #31222 */
        if (isMatched) {
            if ((offset = zfFindElement(dev, buf, ZM_WLAN_EID_DS)) != 0xffff) {
                if ((length = zmw_rx_buf_readb(dev, buf, offset+1)) == 1) {
                    channel = zmw_rx_buf_readb(dev, buf, offset+2);
                    if (zfHpIsAllowedChannel(dev, zfChNumToFreq(dev, channel, 0)) == 0) {
                        frequency = 0;
                    } else {
                        frequency = zfChNumToFreq(dev, channel, 0);;
                    }
                } else {
                    frequency = 0;
                }
            } else {
                frequency = wd->sta.currentFrequency;
            }

            if (frequency != 0) {
                if ( ((frequency > 3000) && (pBssInfo->frequency > 3000))
                     || ((frequency < 3000) && (pBssInfo->frequency < 3000)) ) {
                    /* redundant */
                    break;
                }
            }
        }

        pBssInfo = pBssInfo->next;
    }

    if ( i == wd->sta.bssList.bssCount )
    {
        pBssInfo = NULL;
    }

    return pBssInfo;
}

u8_t zfStaInitBssInfo(zdev_t* dev, zbuf_t* buf,
        struct zsWlanProbeRspFrameHeader *pProbeRspHeader,
        struct zsBssInfo* pBssInfo, struct zsAdditionInfo* AddInfo, u8_t type)
{
    u8_t    length, channel, is5G;
    u16_t   i, offset;
    u8_t    apQosInfo;
    u16_t    eachIElength = 0;
    u16_t   accumulateLen = 0;

    zmw_get_wlan_dev(dev);

    if ((type == 1) && ((pBssInfo->flag & ZM_BSS_INFO_VALID_BIT) != 0))
    {
        goto zlUpdateRssi;
    }

    /* get SSID */
    if ( (offset = zfFindElement(dev, buf, ZM_WLAN_EID_SSID)) == 0xffff )
    {
        zm_debug_msg0("EID(SSID) not found");
        goto zlError;
    }

    length = zmw_rx_buf_readb(dev, buf, offset+1);

	{
		u8_t Show_Flag = 0;
		zfwGetShowZeroLengthSSID(dev, &Show_Flag);

		if(Show_Flag)
		{
			if (length > ZM_MAX_SSID_LENGTH )
			{
				zm_debug_msg0("EID(SSID) is invalid");
				goto zlError;
			}
		}
		else
		{
    if ( length == 0 || length > ZM_MAX_SSID_LENGTH )
    {
        zm_debug_msg0("EID(SSID) is invalid");
        goto zlError;
    }

		}
	}
    zfCopyFromRxBuffer(dev, buf, pBssInfo->ssid, offset, length+2);

    /* get DS parameter */
    if ( (offset = zfFindElement(dev, buf, ZM_WLAN_EID_DS)) != 0xffff )
    {
        length = zmw_rx_buf_readb(dev, buf, offset+1);
        if ( length != 1 )
        {
            zm_msg0_mm(ZM_LV_0, "Abnormal DS Param Set IE");
            goto zlError;
        }
        channel = zmw_rx_buf_readb(dev, buf, offset+2);

        if (zfHpIsAllowedChannel(dev, zfChNumToFreq(dev, channel, 0)) == 0)
        {
            goto zlError2;
        }

        pBssInfo->frequency = zfChNumToFreq(dev, channel, 0); // auto check
        pBssInfo->channel = channel;


    }
    else
    {
        /* DS parameter not found */
        pBssInfo->frequency = wd->sta.currentFrequency;
        pBssInfo->channel = zfChFreqToNum(wd->sta.currentFrequency, &is5G);
    }

    /* initialize security type */
    pBssInfo->securityType = ZM_SECURITY_TYPE_NONE;

    /* get macaddr */
    for( i=0; i<6; i++ )
    {
        pBssInfo->macaddr[i] = pProbeRspHeader->sa[i];
    }

    /* get bssid */
    for( i=0; i<6; i++ )
    {
        pBssInfo->bssid[i] = pProbeRspHeader->bssid[i];
    }

    /* get timestamp */
    for( i=0; i<8; i++ )
    {
        pBssInfo->timeStamp[i] = pProbeRspHeader->timeStamp[i];
    }

    /* get beacon interval */
    pBssInfo->beaconInterval[0] = pProbeRspHeader->beaconInterval[0];
    pBssInfo->beaconInterval[1] = pProbeRspHeader->beaconInterval[1];

    /* get capability */
    pBssInfo->capability[0] = pProbeRspHeader->capability[0];
    pBssInfo->capability[1] = pProbeRspHeader->capability[1];

    /* Copy frame body */
    offset = 36;            // Copy from the start of variable IE
    pBssInfo->frameBodysize = zfwBufGetSize(dev, buf)-offset;
    if (pBssInfo->frameBodysize > (ZM_MAX_PROBE_FRAME_BODY_SIZE-1))
    {
        pBssInfo->frameBodysize = ZM_MAX_PROBE_FRAME_BODY_SIZE-1;
    }
    accumulateLen = 0;
    do
    {
        eachIElength = zmw_rx_buf_readb(dev, buf, offset + accumulateLen+1) + 2;  //Len+(EID+Data)

        if ( (eachIElength >= 2)
             && ((accumulateLen + eachIElength) <= pBssInfo->frameBodysize) )
        {
            zfCopyFromRxBuffer(dev, buf, pBssInfo->frameBody+accumulateLen, offset+accumulateLen, eachIElength);
            accumulateLen+=(u16_t)eachIElength;
        }
        else
        {
            zm_msg0_mm(ZM_LV_1, "probersp frameBodysize abnormal");
            break;
        }
    }
    while(accumulateLen < pBssInfo->frameBodysize);
    pBssInfo->frameBodysize = accumulateLen;

    /* get supported rates */
    if ( (offset = zfFindElement(dev, buf, ZM_WLAN_EID_SUPPORT_RATE)) == 0xffff )
    {
        zm_debug_msg0("EID(supported rates) not found");
        goto zlError;
    }

    length = zmw_rx_buf_readb(dev, buf, offset+1);
    if ( length == 0 || length > ZM_MAX_SUPP_RATES_IE_SIZE)
    {
        zm_msg0_mm(ZM_LV_0, "Supported rates IE length abnormal");
        goto zlError;
    }
    zfCopyFromRxBuffer(dev, buf, pBssInfo->supportedRates, offset, length+2);



    /* get Country information */
    if ( (offset = zfFindElement(dev, buf, ZM_WLAN_EID_COUNTRY)) != 0xffff )
    {
        length = zmw_rx_buf_readb(dev, buf, offset+1);
        if (length > ZM_MAX_COUNTRY_INFO_SIZE)
        {
            length = ZM_MAX_COUNTRY_INFO_SIZE;
        }
        zfCopyFromRxBuffer(dev, buf, pBssInfo->countryInfo, offset, length+2);
        /* check 802.11d support data */
        if (wd->sta.b802_11D)
        {
            zfHpGetRegulationTablefromISO(dev, (u8_t *)&pBssInfo->countryInfo, 3);
            /* only set regulatory one time */
            wd->sta.b802_11D = 0;
        }
    }

    /* get ERP information */
    if ( (offset = zfFindElement(dev, buf, ZM_WLAN_EID_ERP)) != 0xffff )
    {
        pBssInfo->erp = zmw_rx_buf_readb(dev, buf, offset+2);
    }

    /* get extended supported rates */
    if ( (offset = zfFindElement(dev, buf, ZM_WLAN_EID_EXTENDED_RATE)) != 0xffff )
    {
        length = zmw_rx_buf_readb(dev, buf, offset+1);
        if (length > ZM_MAX_SUPP_RATES_IE_SIZE)
        {
            zm_msg0_mm(ZM_LV_0, "Extended rates IE length abnormal");
            goto zlError;
        }
        zfCopyFromRxBuffer(dev, buf, pBssInfo->extSupportedRates, offset, length+2);
    }
    else
    {
        pBssInfo->extSupportedRates[0] = 0;
        pBssInfo->extSupportedRates[1] = 0;
    }

    /* get WPA IE */
    if ( (offset = zfFindElement(dev, buf, ZM_WLAN_EID_WPA_IE)) != 0xffff )
    {
        length = zmw_rx_buf_readb(dev, buf, offset+1);
        if (length > ZM_MAX_IE_SIZE)
        {
            length = ZM_MAX_IE_SIZE;
        }
        zfCopyFromRxBuffer(dev, buf, pBssInfo->wpaIe, offset, length+2);
        pBssInfo->securityType = ZM_SECURITY_TYPE_WPA;
    }
    else
    {
        pBssInfo->wpaIe[1] = 0;
    }

    /* get WPS IE */
    if ((offset = zfFindWifiElement(dev, buf, 4, 0xff)) != 0xffff)
    {
        length = zmw_rx_buf_readb(dev, buf, offset+1);
        if (length > ZM_MAX_WPS_IE_SIZE )
        {
            length = ZM_MAX_WPS_IE_SIZE;
        }
        zfCopyFromRxBuffer(dev, buf, pBssInfo->wscIe, offset, length+2);
    }
    else
    {
        pBssInfo->wscIe[1] = 0;
    }

    /* get SuperG IE */
    if ((offset = zfFindSuperGElement(dev, buf, ZM_WLAN_EID_VENDOR_PRIVATE)) != 0xffff)
    {
        pBssInfo->apCap |= ZM_SuperG_AP;
    }

    /* get XR IE */
    if ((offset = zfFindXRElement(dev, buf, ZM_WLAN_EID_VENDOR_PRIVATE)) != 0xffff)
    {
        pBssInfo->apCap |= ZM_XR_AP;
    }

    /* get RSN IE */
    if ( (offset = zfFindElement(dev, buf, ZM_WLAN_EID_RSN_IE)) != 0xffff )
    {
        length = zmw_rx_buf_readb(dev, buf, offset+1);
        if (length > ZM_MAX_IE_SIZE)
        {
            length = ZM_MAX_IE_SIZE;
        }
        zfCopyFromRxBuffer(dev, buf, pBssInfo->rsnIe, offset, length+2);
        pBssInfo->securityType = ZM_SECURITY_TYPE_WPA;
    }
    else
    {
        pBssInfo->rsnIe[1] = 0;
    }
#ifdef ZM_ENABLE_CENC
    /* get CENC IE */
    if ( (offset = zfFindElement(dev, buf, ZM_WLAN_EID_CENC_IE)) != 0xffff )
    {
        length = zmw_rx_buf_readb(dev, buf, offset+1);
        if (length > ZM_MAX_IE_SIZE )
        {
            length = ZM_MAX_IE_SIZE;
        }
        zfCopyFromRxBuffer(dev, buf, pBssInfo->cencIe, offset, length+2);
        pBssInfo->securityType = ZM_SECURITY_TYPE_CENC;
        pBssInfo->capability[0] &= 0xffef;
    }
    else
    {
        pBssInfo->cencIe[1] = 0;
    }
#endif //ZM_ENABLE_CENC
    /* get WME Parameter IE, probe rsp may contain WME parameter element */
    //if ( wd->bQoSEnable )
    {
        if ((offset = zfFindWifiElement(dev, buf, 2, 1)) != 0xffff)
        {
            apQosInfo = zmw_rx_buf_readb(dev, buf, offset+8) & 0x80;
            pBssInfo->wmeSupport = 1 | apQosInfo;
        }
        else if ((offset = zfFindWifiElement(dev, buf, 2, 0)) != 0xffff)
        {
            apQosInfo = zmw_rx_buf_readb(dev, buf, offset+8) & 0x80;
            pBssInfo->wmeSupport = 1  | apQosInfo;
        }
        else
        {
            pBssInfo->wmeSupport = 0;
        }
    }
    //CWYang(+)
    if ((offset = zfFindElement(dev, buf, ZM_WLAN_EID_HT_CAPABILITY)) != 0xffff)
    {
        /* 11n AP */
        pBssInfo->EnableHT = 1;
        if (zmw_rx_buf_readb(dev, buf, offset+1) & 0x02)
        {
            pBssInfo->enableHT40 = 1;
        }
        else
        {
            pBssInfo->enableHT40 = 0;
        }

        if (zmw_rx_buf_readb(dev, buf, offset+1) & 0x40)
        {
            pBssInfo->SG40 = 1;
        }
        else
        {
            pBssInfo->SG40 = 0;
        }
    }
    else if ((offset = zfFindElement(dev, buf, ZM_WLAN_PREN2_EID_HTCAPABILITY)) != 0xffff)
    {
        /* 11n AP */
        pBssInfo->EnableHT = 1;
        pBssInfo->apCap |= ZM_All11N_AP;
        if (zmw_rx_buf_readb(dev, buf, offset+2) & 0x02)
        {
            pBssInfo->enableHT40 = 1;
        }
        else
        {
            pBssInfo->enableHT40 = 0;
        }

        if (zmw_rx_buf_readb(dev, buf, offset+2) & 0x40)
        {
            pBssInfo->SG40 = 1;
        }
        else
        {
            pBssInfo->SG40 = 0;
        }
    }
    else
    {
        pBssInfo->EnableHT = 0;
    }
    /* HT information */
    if ((offset = zfFindElement(dev, buf, ZM_WLAN_EID_EXTENDED_HT_CAPABILITY)) != 0xffff)
    {
        /* atheros pre n */
        pBssInfo->extChOffset = zmw_rx_buf_readb(dev, buf, offset+2) & 0x03;
    }
    else if ((offset = zfFindElement(dev, buf, ZM_WLAN_PREN2_EID_HTINFORMATION)) != 0xffff)
    {
        /* pre n 2.0 standard */
        pBssInfo->extChOffset = zmw_rx_buf_readb(dev, buf, offset+3) & 0x03;
    }
    else
    {
        pBssInfo->extChOffset = 0;
    }

    if ( (pBssInfo->enableHT40 == 1)
         && ((pBssInfo->extChOffset != 1) && (pBssInfo->extChOffset != 3)) )
    {
        pBssInfo->enableHT40 = 0;
    }

    if (pBssInfo->enableHT40 == 1)
    {
        if (zfHpIsAllowedChannel(dev, pBssInfo->frequency+((pBssInfo->extChOffset==1)?20:-20)) == 0)
        {
            /* if extension channel is not an allowed channel, treat AP as non-HT mode */
            pBssInfo->EnableHT = 0;
            pBssInfo->enableHT40 = 0;
            pBssInfo->extChOffset = 0;
        }
    }

    /* get ATH Extended Capability */
    if ( ((offset = zfFindElement(dev, buf, ZM_WLAN_EID_EXTENDED_HT_CAPABILITY)) != 0xffff)&&
        ((offset = zfFindBrdcmMrvlRlnkExtCap(dev, buf)) == 0xffff))

    {
        pBssInfo->athOwlAp = 1;
    }
    else
    {
        pBssInfo->athOwlAp = 0;
    }

    /* get Broadcom Extended Capability */
    if ( (pBssInfo->EnableHT == 1) //((offset = zfFindElement(dev, buf, ZM_WLAN_EID_EXTENDED_HT_CAPABILITY)) != 0xffff)
         && ((offset = zfFindBroadcomExtCap(dev, buf)) != 0xffff) )
    {
        pBssInfo->broadcomHTAp = 1;
    }
    else
    {
        pBssInfo->broadcomHTAp = 0;
    }

    /* get Marvel Extended Capability */
    if ((offset = zfFindMarvelExtCap(dev, buf)) != 0xffff)
    {
        pBssInfo->marvelAp = 1;
    }
    else
    {
        pBssInfo->marvelAp = 0;
    }

    /* get ATIM window */
    if ( (offset = zfFindElement(dev, buf, ZM_WLAN_EID_IBSS)) != 0xffff )
    {
        pBssInfo->atimWindow = zmw_rx_buf_readh(dev, buf,offset+2);
    }

    /* Fit for support mode */
    if (pBssInfo->frequency > 3000) {
        if (wd->supportMode & ZM_WIRELESS_MODE_5_N) {
#if 0
            if (wd->supportMode & ZM_WIRELESS_MODE_5_54) {
                /* support mode: a, n */
                /* do nothing */
            } else {
                /* support mode: n */
                /* reject non-n bss info */
                if (!pBssInfo->EnableHT) {
                    goto zlError2;
                }
            }
#endif
        } else {
            if (wd->supportMode & ZM_WIRELESS_MODE_5_54) {
                /* support mode: a */
                /* delete n mode information */
                pBssInfo->EnableHT = 0;
                pBssInfo->enableHT40 = 0;
                pBssInfo->apCap &= (~ZM_All11N_AP);
                pBssInfo->extChOffset = 0;
                pBssInfo->frameBodysize = zfRemoveElement(dev, pBssInfo->frameBody,
                            pBssInfo->frameBodysize, ZM_WLAN_EID_HT_CAPABILITY);
                pBssInfo->frameBodysize = zfRemoveElement(dev, pBssInfo->frameBody,
                            pBssInfo->frameBodysize, ZM_WLAN_PREN2_EID_HTCAPABILITY);
                pBssInfo->frameBodysize = zfRemoveElement(dev, pBssInfo->frameBody,
                            pBssInfo->frameBodysize, ZM_WLAN_EID_EXTENDED_HT_CAPABILITY);
                pBssInfo->frameBodysize = zfRemoveElement(dev, pBssInfo->frameBody,
                            pBssInfo->frameBodysize, ZM_WLAN_PREN2_EID_HTINFORMATION);
            } else {
                /* support mode: none */
                goto zlError2;
            }
        }
    } else {
        if (wd->supportMode & ZM_WIRELESS_MODE_24_N) {
#if 0
            if (wd->supportMode & ZM_WIRELESS_MODE_24_54) {
                if (wd->supportMode & ZM_WIRELESS_MODE_24_11) {
                    /* support mode: b, g, n */
                    /* do nothing */
                } else {
                    /* support mode: g, n */
                    /* reject b-only bss info */
                    if ( (!pBssInfo->EnableHT)
                         && (pBssInfo->extSupportedRates[1] == 0) ) {
                         goto zlError2;
                    }
                }
            } else {
                if (wd->supportMode & ZM_WIRELESS_MODE_24_11) {
                    /* support mode: b, n */
                    /* 1. reject g-only bss info
                     * 2. if non g-only, delete g mode information
                     */
                    if ( !pBssInfo->EnableHT ) {
                        if ( zfIsGOnlyMode(dev, pBssInfo->frequency, pBssInfo->supportedRates)
                             || zfIsGOnlyMode(dev, pBssInfo->frequency, pBssInfo->extSupportedRates) ) {
                            goto zlError2;
                        } else {
                            zfGatherBMode(dev, pBssInfo->supportedRates,
                                          pBssInfo->extSupportedRates);
                            pBssInfo->erp = 0;

                            pBssInfo->frameBodysize = zfRemoveElement(dev,
                                pBssInfo->frameBody, pBssInfo->frameBodysize,
                                ZM_WLAN_EID_ERP);
                            pBssInfo->frameBodysize = zfRemoveElement(dev,
                                pBssInfo->frameBody, pBssInfo->frameBodysize,
                                ZM_WLAN_EID_EXTENDED_RATE);

                            pBssInfo->frameBodysize = zfUpdateElement(dev,
                                pBssInfo->frameBody, pBssInfo->frameBodysize,
                                pBssInfo->supportedRates);
                        }
                    }
                } else {
                    /* support mode: n */
                    /* reject non-n bss info */
                    if (!pBssInfo->EnableHT) {
                        goto zlError2;
                    }
                }
            }
#endif
        } else {
            /* delete n mode information */
            pBssInfo->EnableHT = 0;
            pBssInfo->enableHT40 = 0;
            pBssInfo->apCap &= (~ZM_All11N_AP);
            pBssInfo->extChOffset = 0;
            pBssInfo->frameBodysize = zfRemoveElement(dev, pBssInfo->frameBody,
                        pBssInfo->frameBodysize, ZM_WLAN_EID_HT_CAPABILITY);
            pBssInfo->frameBodysize = zfRemoveElement(dev, pBssInfo->frameBody,
                        pBssInfo->frameBodysize, ZM_WLAN_PREN2_EID_HTCAPABILITY);
            pBssInfo->frameBodysize = zfRemoveElement(dev, pBssInfo->frameBody,
                        pBssInfo->frameBodysize, ZM_WLAN_EID_EXTENDED_HT_CAPABILITY);
            pBssInfo->frameBodysize = zfRemoveElement(dev, pBssInfo->frameBody,
                        pBssInfo->frameBodysize, ZM_WLAN_PREN2_EID_HTINFORMATION);

            if (wd->supportMode & ZM_WIRELESS_MODE_24_54) {
#if 0
                if (wd->supportMode & ZM_WIRELESS_MODE_24_11) {
                    /* support mode: b, g */
                    /* delete n mode information */
                } else {
                    /* support mode: g */
                    /* delete n mode information */
                    /* reject b-only bss info */
                    if (pBssInfo->extSupportedRates[1] == 0) {
                         goto zlError2;
                    }
                }
#endif
            } else {
                if (wd->supportMode & ZM_WIRELESS_MODE_24_11) {
                    /* support mode: b */
                    /* delete n mode information */
                    if ( zfIsGOnlyMode(dev, pBssInfo->frequency, pBssInfo->supportedRates)
                         || zfIsGOnlyMode(dev, pBssInfo->frequency, pBssInfo->extSupportedRates) ) {
                        goto zlError2;
                    } else {
                        zfGatherBMode(dev, pBssInfo->supportedRates,
                                          pBssInfo->extSupportedRates);
                        pBssInfo->erp = 0;

                        pBssInfo->frameBodysize = zfRemoveElement(dev,
                            pBssInfo->frameBody, pBssInfo->frameBodysize,
                            ZM_WLAN_EID_ERP);
                        pBssInfo->frameBodysize = zfRemoveElement(dev,
                            pBssInfo->frameBody, pBssInfo->frameBodysize,
                            ZM_WLAN_EID_EXTENDED_RATE);

                        pBssInfo->frameBodysize = zfUpdateElement(dev,
                            pBssInfo->frameBody, pBssInfo->frameBodysize,
                            pBssInfo->supportedRates);
                    }
                } else {
                    /* support mode: none */
                    goto zlError2;
                }
            }
        }
    }

    pBssInfo->flag |= ZM_BSS_INFO_VALID_BIT;

zlUpdateRssi:
    /* Update Timer information */
    pBssInfo->tick = wd->tick;

    /* Update ERP information */
    if ( (offset = zfFindElement(dev, buf, ZM_WLAN_EID_ERP)) != 0xffff )
    {
        pBssInfo->erp = zmw_rx_buf_readb(dev, buf, offset+2);
    }

    if( (s8_t)pBssInfo->signalStrength < (s8_t)AddInfo->Tail.Data.SignalStrength1 )
    {
        /* Update signal strength */
        pBssInfo->signalStrength = (u8_t)AddInfo->Tail.Data.SignalStrength1;
        /* Update signal quality */
        pBssInfo->signalQuality = (u8_t)(AddInfo->Tail.Data.SignalStrength1 * 2);

        /* Update the sorting value  */
        pBssInfo->sortValue = zfComputeBssInfoWeightValue(dev,
                                               (pBssInfo->supportedRates[6] + pBssInfo->extSupportedRates[0]),
                                               pBssInfo->EnableHT,
                                               pBssInfo->enableHT40,
                                               pBssInfo->signalStrength);
    }

    return 0;

zlError:

    return 1;

zlError2:

    return 2;
}

void zfStaProcessBeacon(zdev_t* dev, zbuf_t* buf, struct zsAdditionInfo* AddInfo) //CWYang(m)
{
    /* Parse TIM and send PS-POLL in power saving mode */
    struct zsWlanBeaconFrameHeader*  pBeaconHeader;
    struct zsBssInfo* pBssInfo;
    u8_t   pBuf[sizeof(struct zsWlanBeaconFrameHeader)];
    u8_t   bssid[6];
    int    res;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    /* sta routine jobs */
    zfStaProtErpMonitor(dev, buf);  /* check protection mode */

    if (zfStaIsConnected(dev))
    {
        ZM_MAC_WORD_TO_BYTE(wd->sta.bssid, bssid);

        if ( wd->wlanMode == ZM_MODE_INFRASTRUCTURE )
        {
            if ( zfRxBufferEqualToStr(dev, buf, bssid, ZM_WLAN_HEADER_A2_OFFSET, 6) )
            {
                zfPowerSavingMgrProcessBeacon(dev, buf);
                zfStaUpdateWmeParameter(dev, buf);
                if (wd->sta.DFSEnable)
                    zfStaUpdateDot11HDFS(dev, buf);
                if (wd->sta.TPCEnable)
                    zfStaUpdateDot11HTPC(dev, buf);
                /* update signal strength and signal quality */
                zfStaSignalStatistic(dev, AddInfo->Tail.Data.SignalStrength1,
                        AddInfo->Tail.Data.SignalQuality); //CWYang(+)
                wd->sta.rxBeaconCount++;
            }
        }
        else if ( wd->wlanMode == ZM_MODE_IBSS )
        {
            if ( zfRxBufferEqualToStr(dev, buf, bssid, ZM_WLAN_HEADER_A3_OFFSET, 6) )
            {
                int res;
                struct zsPartnerNotifyEvent event;

                zm_debug_msg0("20070916 Receive opposite Beacon!");
                zmw_enter_critical_section(dev);
                wd->sta.ibssReceiveBeaconCount++;
                zmw_leave_critical_section(dev);

                res = zfStaSetOppositeInfoFromRxBuf(dev, buf);
                if ( res == 0 )
                {
                    // New peer station found. Notify the wrapper now
                    zfInitPartnerNotifyEvent(dev, buf, &event);
                    if (wd->zfcbIbssPartnerNotify != NULL)
                    {
                        wd->zfcbIbssPartnerNotify(dev, 1, &event);
                    }
                }
                /* update signal strength and signal quality */
                zfStaSignalStatistic(dev, AddInfo->Tail.Data.SignalStrength1,
                        AddInfo->Tail.Data.SignalQuality); //CWYang(+)
            }
            //else if ( wd->sta.ibssPartnerStatus == ZM_IBSS_PARTNER_LOST )
            // Why does this happen in IBSS?? The impact of Vista since
            // we need to tell it the BSSID
#if 0
            else if ( wd->sta.oppositeCount == 0 )
            {   /* IBSS merge if SSID matched */
                if ( (offset = zfFindElement(dev, buf, ZM_WLAN_EID_SSID)) != 0xffff )
                {
                    if ( (wd->sta.ssidLen == zmw_buf_readb(dev, buf, offset+1))&&
                         (zfRxBufferEqualToStr(dev, buf, wd->sta.ssid,
                                               offset+2, wd->sta.ssidLen)) )
                    {
                        capabilityInfo = zmw_buf_readh(dev, buf, 34);

                        if ( capabilityInfo & ZM_BIT_1 )
                        {
                            if ( (wd->sta.capability[0] & ZM_BIT_4) ==
                                 (capabilityInfo & ZM_BIT_4) )
                            {
                                zm_debug_msg0("IBSS merge");
                                zfCopyFromRxBuffer(dev, buf, bssid,
                                                   ZM_WLAN_HEADER_A3_OFFSET, 6);
                                zfUpdateBssid(dev, bssid);
                            }
                        }
                    }
                }
            }
#endif
        }
    }

    /* return if not channel scan */
    if ( !wd->sta.bChannelScan )
    {
        goto zlReturn;
    }

    zfCopyFromRxBuffer(dev, buf, pBuf, 0, sizeof(struct zsWlanBeaconFrameHeader));
    pBeaconHeader = (struct zsWlanBeaconFrameHeader*) pBuf;

    zmw_enter_critical_section(dev);

    //zm_debug_msg1("bss count = ", wd->sta.bssList.bssCount);

    pBssInfo = zfStaFindBssInfo(dev, buf, pBeaconHeader);

    if ( pBssInfo == NULL )
    {
        /* Allocate a new entry if BSS not in the scan list */
        pBssInfo = zfBssInfoAllocate(dev);
        if (pBssInfo != NULL)
        {
            res = zfStaInitBssInfo(dev, buf, pBeaconHeader, pBssInfo, AddInfo, 0);
            //zfDumpSSID(pBssInfo->ssid[1], &(pBssInfo->ssid[2]));
            if ( res != 0 )
            {
                zfBssInfoFree(dev, pBssInfo);
            }
            else
            {
                zfBssInfoInsertToList(dev, pBssInfo);
            }
        }
    }
    else
    {
        res = zfStaInitBssInfo(dev, buf, pBeaconHeader, pBssInfo, AddInfo, 1);
        if (res == 2)
        {
            zfBssInfoRemoveFromList(dev, pBssInfo);
            zfBssInfoFree(dev, pBssInfo);
        }
        else if ( wd->wlanMode == ZM_MODE_IBSS )
        {
            int idx;

            // It would reset the alive counter if the peer station is found!
            zfStaFindFreeOpposite(dev, (u16_t *)pBssInfo->macaddr, &idx);
        }
    }

    zmw_leave_critical_section(dev);

zlReturn:

    return;
}


void zfAuthFreqCompleteCb(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    if (wd->sta.connectState == ZM_STA_CONN_STATE_AUTH_COMPLETED)
    {
        zm_debug_msg0("ZM_STA_CONN_STATE_ASSOCIATE");
        wd->sta.connectTimer = wd->tick;
        wd->sta.connectState = ZM_STA_CONN_STATE_ASSOCIATE;
    }

    zmw_leave_critical_section(dev);
    return;
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfProcessAuth               */
/*      Process authenticate management frame.                          */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : auth frame buffer                                         */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      none                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2005.10     */
/*                                                                      */
/************************************************************************/
/* Note : AP allows one authenticating STA at a time, does not          */
/*        support multiple authentication process. Make sure            */
/*        authentication state machine will not be blocked due          */
/*        to incompleted authentication handshake.                      */
void zfStaProcessAuth(zdev_t* dev, zbuf_t* buf, u16_t* src, u16_t apId)
{
    struct zsWlanAuthFrameHeader* pAuthFrame;
    u8_t  pBuf[sizeof(struct zsWlanAuthFrameHeader)];
    u32_t p1, p2;

    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    if ( !zfStaIsConnecting(dev) )
    {
        return;
    }

    pAuthFrame = (struct zsWlanAuthFrameHeader*) pBuf;
    zfCopyFromRxBuffer(dev, buf, pBuf, 0, sizeof(struct zsWlanAuthFrameHeader));

    if ( wd->sta.connectState == ZM_STA_CONN_STATE_AUTH_OPEN )
    {
        if ( (zmw_le16_to_cpu(pAuthFrame->seq) == 2)&&
             (zmw_le16_to_cpu(pAuthFrame->algo) == 0)&&
             (zmw_le16_to_cpu(pAuthFrame->status) == 0) )
        {

            zmw_enter_critical_section(dev);
            wd->sta.connectTimer = wd->tick;
            zm_debug_msg0("ZM_STA_CONN_STATE_AUTH_COMPLETED");
            wd->sta.connectState = ZM_STA_CONN_STATE_AUTH_COMPLETED;
            zmw_leave_critical_section(dev);

            //Set channel according to AP's configuration
            //Move to here because of Cisco 11n AP feature
            zfCoreSetFrequencyEx(dev, wd->frequency, wd->BandWidth40,
                    wd->ExtOffset, zfAuthFreqCompleteCb);

            /* send association frame */
            if ( wd->sta.connectByReasso )
            {
                zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_REASOCREQ,
                              wd->sta.bssid, 0, 0, 0);
            }
            else
            {
                zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_ASOCREQ,
                              wd->sta.bssid, 0, 0, 0);
            }


        }
        else
        {
            zm_debug_msg1("authentication failed, status = ",
                          pAuthFrame->status);

            if (wd->sta.authMode == ZM_AUTH_MODE_AUTO)
            {
                wd->sta.bIsSharedKey = 1;
                zfStaStartConnect(dev, wd->sta.bIsSharedKey);
            }
            else
            {
                zm_debug_msg0("ZM_STA_STATE_DISCONNECT");
                zfStaConnectFail(dev, ZM_STATUS_MEDIA_DISCONNECT_AUTH_FAILED, wd->sta.bssid, 3);
            }
        }
    }
    else if ( wd->sta.connectState == ZM_STA_CONN_STATE_AUTH_SHARE_1 )
    {
        if ( (zmw_le16_to_cpu(pAuthFrame->algo) == 1) &&
             (zmw_le16_to_cpu(pAuthFrame->seq) == 2) &&
             (zmw_le16_to_cpu(pAuthFrame->status) == 0))
              //&& (pAuthFrame->challengeText[1] <= 255) )
        {
            zfMemoryCopy(wd->sta.challengeText, pAuthFrame->challengeText,
                         pAuthFrame->challengeText[1]+2);

            /* send the 3rd authentication frame */
            p1 = 0x30001;
            p2 = 0;
            zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_AUTH,
                          wd->sta.bssid, p1, p2, 0);

            zmw_enter_critical_section(dev);
            wd->sta.connectTimer = wd->tick;

            zm_debug_msg0("ZM_STA_SUB_STATE_AUTH_SHARE_2");
            wd->sta.connectState = ZM_STA_CONN_STATE_AUTH_SHARE_2;
            zmw_leave_critical_section(dev);
        }
        else
        {
            zm_debug_msg1("authentication failed, status = ",
                          pAuthFrame->status);

            zm_debug_msg0("ZM_STA_STATE_DISCONNECT");
            zfStaConnectFail(dev, ZM_STATUS_MEDIA_DISCONNECT_AUTH_FAILED, wd->sta.bssid, 3);
        }
    }
    else if ( wd->sta.connectState == ZM_STA_CONN_STATE_AUTH_SHARE_2 )
    {
        if ( (zmw_le16_to_cpu(pAuthFrame->algo) == 1)&&
             (zmw_le16_to_cpu(pAuthFrame->seq) == 4)&&
             (zmw_le16_to_cpu(pAuthFrame->status) == 0) )
        {
            //Set channel according to AP's configuration
            //Move to here because of Cisco 11n AP feature
            zfCoreSetFrequencyEx(dev, wd->frequency, wd->BandWidth40,
                    wd->ExtOffset, NULL);

            /* send association frame */
            zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_ASOCREQ,
                          wd->sta.bssid, 0, 0, 0);

            zmw_enter_critical_section(dev);
            wd->sta.connectTimer = wd->tick;

            zm_debug_msg0("ZM_STA_SUB_STATE_ASSOCIATE");
            wd->sta.connectState = ZM_STA_CONN_STATE_ASSOCIATE;
            zmw_leave_critical_section(dev);
        }
        else
        {
            zm_debug_msg1("authentication failed, status = ",
                          pAuthFrame->status);

            zm_debug_msg0("ZM_STA_STATE_DISCONNECT");
            zfStaConnectFail(dev, ZM_STATUS_MEDIA_DISCONNECT_AUTH_FAILED, wd->sta.bssid, 3);
        }
    }
    else
    {
        zm_debug_msg0("unknown case");
    }
}

void zfStaProcessAsocReq(zdev_t* dev, zbuf_t* buf, u16_t* src, u16_t apId)
{

    return;
}

void zfStaProcessAsocRsp(zdev_t* dev, zbuf_t* buf)
{
    struct zsWlanAssoFrameHeader* pAssoFrame;
    u8_t  pBuf[sizeof(struct zsWlanAssoFrameHeader)];
    u16_t offset;
    u32_t i;
    u32_t oneTxStreamCap;

    zmw_get_wlan_dev(dev);

    if ( !zfStaIsConnecting(dev) )
    {
        return;
    }

    pAssoFrame = (struct zsWlanAssoFrameHeader*) pBuf;
    zfCopyFromRxBuffer(dev, buf, pBuf, 0, sizeof(struct zsWlanAssoFrameHeader));

    if ( wd->sta.connectState == ZM_STA_CONN_STATE_ASSOCIATE )
    {
        if ( pAssoFrame->status == 0 )
        {
            zm_debug_msg0("ZM_STA_STATE_CONNECTED");

            if (wd->sta.EnableHT == 1)
            {
                wd->sta.wmeConnected = 1;
            }
            if ((wd->sta.wmeEnabled & ZM_STA_WME_ENABLE_BIT) != 0) //WME enabled
            {
                /* Asoc rsp may contain WME parameter element */
                if ((offset = zfFindWifiElement(dev, buf, 2, 1)) != 0xffff)
                {
                    zm_debug_msg0("WME enable");
                    wd->sta.wmeConnected = 1;
                    if ((wd->sta.wmeEnabled & ZM_STA_UAPSD_ENABLE_BIT) != 0)
                    {
                        if ((zmw_rx_buf_readb(dev, buf, offset+8) & 0x80) != 0)
                        {
                            zm_debug_msg0("UAPSD enable");
                            wd->sta.qosInfo = wd->sta.wmeQosInfo;
                        }
                    }

                    zfStaUpdateWmeParameter(dev, buf);
                }
            }


            //Store asoc response frame body, for VISTA only
            wd->sta.asocRspFrameBodySize = zfwBufGetSize(dev, buf)-24;
            if (wd->sta.asocRspFrameBodySize > ZM_CACHED_FRAMEBODY_SIZE)
            {
                wd->sta.asocRspFrameBodySize = ZM_CACHED_FRAMEBODY_SIZE;
            }
            for (i=0; i<wd->sta.asocRspFrameBodySize; i++)
            {
                wd->sta.asocRspFrameBody[i] = zmw_rx_buf_readb(dev, buf, i+24);
            }

            zfStaStoreAsocRspIe(dev, buf);
            if (wd->sta.EnableHT &&
                ((wd->sta.ie.HtCap.HtCapInfo & HTCAP_SupChannelWidthSet) != 0) &&
                (wd->ExtOffset != 0))
            {
                wd->sta.htCtrlBandwidth = 1;
            }
            else
            {
                wd->sta.htCtrlBandwidth = 0;
            }

            //Set channel according to AP's configuration
            //zfCoreSetFrequencyEx(dev, wd->frequency, wd->BandWidth40,
            //        wd->ExtOffset, NULL);

            if (wd->sta.EnableHT == 1)
            {
                wd->addbaComplete = 0;

                if ((wd->sta.SWEncryptEnable & ZM_SW_TKIP_ENCRY_EN) == 0 &&
                    (wd->sta.SWEncryptEnable & ZM_SW_WEP_ENCRY_EN) == 0)
                {
                    wd->addbaCount = 1;
                    zfAggSendAddbaRequest(dev, wd->sta.bssid, 0, 0);
                    zfTimerSchedule(dev, ZM_EVENT_TIMEOUT_ADDBA, 100);
                }
            }

            /* set RIFS support */
            if(wd->sta.ie.HtInfo.ChannelInfo & ExtHtCap_RIFSMode)
            {
                wd->sta.HT2040 = 1;
//                zfHpSetRifs(dev, wd->sta.EnableHT, 1, (wd->sta.currentFrequency < 3000)? 1:0);
            }

            wd->sta.aid = pAssoFrame->aid & 0x3fff;
            wd->sta.oppositeCount = 0;    /* reset opposite count */
            zfStaSetOppositeInfoFromRxBuf(dev, buf);

            wd->sta.rxBeaconCount = 16;

            zfChangeAdapterState(dev, ZM_STA_STATE_CONNECTED);
            wd->sta.connPowerInHalfDbm = zfHpGetTransmitPower(dev);
            if (wd->zfcbConnectNotify != NULL)
            {
                if (wd->sta.EnableHT != 0) /* 11n */
            	{
    		        oneTxStreamCap = (zfHpCapability(dev) & ZM_HP_CAP_11N_ONE_TX_STREAM);
    		        if (wd->sta.htCtrlBandwidth == 1) /* HT40*/
    		        {
    					if(oneTxStreamCap) /* one Tx stream */
    				    {
    				        if (wd->sta.SG40)
    				        {
    				            wd->CurrentTxRateKbps = 150000;
    						    wd->CurrentRxRateKbps = 300000;
    				        }
    				        else
    				        {
    				            wd->CurrentTxRateKbps = 135000;
    						    wd->CurrentRxRateKbps = 270000;
    				        }
    				    }
    				    else /* Two Tx streams */
    				    {
    				        if (wd->sta.SG40)
    				        {
    				            wd->CurrentTxRateKbps = 300000;
    						    wd->CurrentRxRateKbps = 300000;
    				        }
    				        else
    				        {
    				            wd->CurrentTxRateKbps = 270000;
    						    wd->CurrentRxRateKbps = 270000;
    				        }
    				    }
    		        }
    		        else /* HT20 */
    		        {
    		            if(oneTxStreamCap) /* one Tx stream */
    				    {
    				        wd->CurrentTxRateKbps = 650000;
    						wd->CurrentRxRateKbps = 130000;
    				    }
    				    else /* Two Tx streams */
    				    {
    				        wd->CurrentTxRateKbps = 130000;
    					    wd->CurrentRxRateKbps = 130000;
    				    }
    		        }
                }
                else /* 11abg */
                {
                    if (wd->sta.connection_11b != 0)
                    {
                        wd->CurrentTxRateKbps = 11000;
    			        wd->CurrentRxRateKbps = 11000;
                    }
                    else
                    {
                        wd->CurrentTxRateKbps = 54000;
    			        wd->CurrentRxRateKbps = 54000;
    			    }
                }


                wd->zfcbConnectNotify(dev, ZM_STATUS_MEDIA_CONNECT, wd->sta.bssid);
            }
            wd->sta.connectByReasso = TRUE;
            wd->sta.failCntOfReasso = 0;

            zfPowerSavingMgrConnectNotify(dev);

            /* Disable here because fixed rate is only for test, TBD. */
            //if (wd->sta.EnableHT)
            //{
            //    wd->txMCS = 7; //Rate = 65Mbps
            //    wd->txMT = 2; // Ht rate
            //    wd->enableAggregation = 2; // Enable Aggregation
            //}
        }
        else
        {
            zm_debug_msg1("association failed, status = ",
                          pAssoFrame->status);

            zm_debug_msg0("ZM_STA_STATE_DISCONNECT");
            wd->sta.connectByReasso = FALSE;
            zfStaConnectFail(dev, ZM_STATUS_MEDIA_DISCONNECT_ASOC_FAILED, wd->sta.bssid, 3);
        }
    }

}

void zfStaStoreAsocRspIe(zdev_t* dev, zbuf_t* buf)
{
    u16_t offset;
    u32_t i;
    u16_t length;
    u8_t  *htcap;
    u8_t  asocBw40 = 0;
    u8_t  asocExtOffset = 0;

    zmw_get_wlan_dev(dev);

    for (i=0; i<wd->sta.asocRspFrameBodySize; i++)
    {
        wd->sta.asocRspFrameBody[i] = zmw_rx_buf_readb(dev, buf, i+24);
    }

    /* HT capabilities: 28 octets */
    if (    ((wd->sta.currentFrequency > 3000) && !(wd->supportMode & ZM_WIRELESS_MODE_5_N))
         || ((wd->sta.currentFrequency < 3000) && !(wd->supportMode & ZM_WIRELESS_MODE_24_N)) )
    {
        /* not 11n AP */
        htcap = (u8_t *)&wd->sta.ie.HtCap;
        for (i=0; i<28; i++)
        {
            htcap[i] = 0;
        }
        wd->BandWidth40 = 0;
        wd->ExtOffset = 0;
        return;
    }

    if ((offset = zfFindElement(dev, buf, ZM_WLAN_EID_HT_CAPABILITY)) != 0xffff)
    {
        /* atheros pre n */
        zm_debug_msg0("atheros pre n");
        htcap = (u8_t *)&wd->sta.ie.HtCap;
        htcap[0] = zmw_rx_buf_readb(dev, buf, offset);
        htcap[1] = 26;
        for (i=1; i<=26; i++)
        {
            htcap[i+1] = zmw_rx_buf_readb(dev, buf, offset + i);
            zm_msg2_mm(ZM_LV_1, "ASOC:  HT Capabilities, htcap=", htcap[i+1]);
        }
    }
    else if ((offset = zfFindElement(dev, buf, ZM_WLAN_PREN2_EID_HTCAPABILITY)) != 0xffff)
    {
        /* pre n 2.0 standard */
        zm_debug_msg0("pre n 2.0 standard");
        htcap = (u8_t *)&wd->sta.ie.HtCap;
        for (i=0; i<28; i++)
        {
            htcap[i] = zmw_rx_buf_readb(dev, buf, offset + i);
            zm_msg2_mm(ZM_LV_1, "ASOC:  HT Capabilities, htcap=", htcap[i]);
        }
    }
    else
    {
        /* not 11n AP */
        htcap = (u8_t *)&wd->sta.ie.HtCap;
        for (i=0; i<28; i++)
        {
            htcap[i] = 0;
        }
        wd->BandWidth40 = 0;
        wd->ExtOffset = 0;
        return;
    }

    asocBw40 = (u8_t)((wd->sta.ie.HtCap.HtCapInfo & HTCAP_SupChannelWidthSet) >> 1);

    /* HT information */
    if ((offset = zfFindElement(dev, buf, ZM_WLAN_EID_EXTENDED_HT_CAPABILITY)) != 0xffff)
    {
        /* atheros pre n */
        zm_debug_msg0("atheros pre n HTINFO");
        length = 22;
        htcap = (u8_t *)&wd->sta.ie.HtInfo;
        htcap[0] = zmw_rx_buf_readb(dev, buf, offset);
        htcap[1] = 22;
        for (i=1; i<=22; i++)
        {
            htcap[i+1] = zmw_rx_buf_readb(dev, buf, offset + i);
            zm_msg2_mm(ZM_LV_1, "ASOC:  HT Info, htinfo=", htcap[i+1]);
        }
    }
    else if ((offset = zfFindElement(dev, buf, ZM_WLAN_PREN2_EID_HTINFORMATION)) != 0xffff)
    {
        /* pre n 2.0 standard */
        zm_debug_msg0("pre n 2.0 standard HTINFO");
        length = zmw_rx_buf_readb(dev, buf, offset + 1);
        htcap = (u8_t *)&wd->sta.ie.HtInfo;
        for (i=0; i<24; i++)
        {
            htcap[i] = zmw_rx_buf_readb(dev, buf, offset + i);
            zm_msg2_mm(ZM_LV_1, "ASOC:  HT Info, htinfo=", htcap[i]);
        }
    }
    else
    {
        zm_debug_msg0("no HTINFO");
        htcap = (u8_t *)&wd->sta.ie.HtInfo;
        for (i=0; i<24; i++)
        {
            htcap[i] = 0;
        }
    }
    asocExtOffset = wd->sta.ie.HtInfo.ChannelInfo & ExtHtCap_ExtChannelOffsetBelow;

    if ((wd->sta.EnableHT == 1) && (asocBw40 == 1) && ((asocExtOffset == 1) || (asocExtOffset == 3)))
    {
        wd->BandWidth40 = asocBw40;
        wd->ExtOffset = asocExtOffset;
    }
    else
    {
        wd->BandWidth40 = 0;
        wd->ExtOffset = 0;
    }

    return;
}

void zfStaProcessDeauth(zdev_t* dev, zbuf_t* buf)
{
    u16_t apMacAddr[3];

    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    /* STA : if SA=connected AP then disconnect with AP */
    if ( wd->wlanMode == ZM_MODE_INFRASTRUCTURE )
    {
        apMacAddr[0] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A3_OFFSET);
        apMacAddr[1] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A3_OFFSET+2);
        apMacAddr[2] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A3_OFFSET+4);
  	if ((apMacAddr[0] == wd->sta.bssid[0]) && (apMacAddr[1] == wd->sta.bssid[1]) && (apMacAddr[2] == wd->sta.bssid[2]))
        {
            if (zfwBufGetSize(dev, buf) >= 24+2) //not a malformed frame
            {
                if ( zfStaIsConnected(dev) )
                {
                    zfStaConnectFail(dev, ZM_STATUS_MEDIA_DISCONNECT_DEAUTH, wd->sta.bssid, 2);
                }
                else if (zfStaIsConnecting(dev))
                {
                    zfStaConnectFail(dev, ZM_STATUS_MEDIA_DISCONNECT_AUTH_FAILED, wd->sta.bssid, 3);
                }
                else
                {
                }
            }
        }
    }
    else if ( wd->wlanMode == ZM_MODE_IBSS )
    {
        u16_t peerMacAddr[3];
        u8_t  peerIdx;
        s8_t  res;

        if ( zfStaIsConnected(dev) )
        {
            peerMacAddr[0] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A2_OFFSET);
            peerMacAddr[1] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A2_OFFSET+2);
            peerMacAddr[2] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A2_OFFSET+4);

            zmw_enter_critical_section(dev);
            res = zfStaFindOppositeByMACAddr(dev, peerMacAddr, &peerIdx);
            if ( res == 0 )
            {
                wd->sta.oppositeInfo[peerIdx].aliveCounter = 0;
            }
            zmw_leave_critical_section(dev);
        }
    }
}

void zfStaProcessDisasoc(zdev_t* dev, zbuf_t* buf)
{
    u16_t apMacAddr[3];

    zmw_get_wlan_dev(dev);

    /* STA : if SA=connected AP then disconnect with AP */
    if ( wd->wlanMode == ZM_MODE_INFRASTRUCTURE )
    {
        apMacAddr[0] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A3_OFFSET);
        apMacAddr[1] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A3_OFFSET+2);
        apMacAddr[2] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A3_OFFSET+4);

        if ((apMacAddr[0] == wd->sta.bssid[0]) && (apMacAddr[1] == wd->sta.bssid[1]) && (apMacAddr[2] == wd->sta.bssid[2]))
        {
            if (zfwBufGetSize(dev, buf) >= 24+2) //not a malformed frame
            {
                if ( zfStaIsConnected(dev) )
                {
                    zfStaConnectFail(dev, ZM_STATUS_MEDIA_DISCONNECT_DISASOC, wd->sta.bssid, 2);
                }
                else
                {
                    zfStaConnectFail(dev, ZM_STATUS_MEDIA_DISCONNECT_ASOC_FAILED, wd->sta.bssid, 3);
                }
            }
        }
    }
}



/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfProcessProbeReq           */
/*      Process probe request management frame.                         */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : auth frame buffer                                         */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      none                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2005.10     */
/*                                                                      */
/************************************************************************/
void zfStaProcessProbeReq(zdev_t* dev, zbuf_t* buf, u16_t* src)
{
    u16_t offset;
    u8_t len;
    u16_t i, j;
    u16_t sendFlag;

    zmw_get_wlan_dev(dev);

    /* check mode : AP/IBSS */
    if ((wd->wlanMode != ZM_MODE_AP) || (wd->wlanMode != ZM_MODE_IBSS))
    {
        zm_msg0_mm(ZM_LV_3, "Ignore probe req");
        return;
    }

    /* check SSID */
    if ((offset = zfFindElement(dev, buf, ZM_WLAN_EID_SSID)) == 0xffff)
    {
        zm_msg0_mm(ZM_LV_3, "probe req SSID not found");
        return;
    }

    len = zmw_rx_buf_readb(dev, buf, offset+1);

    for (i=0; i<ZM_MAX_AP_SUPPORT; i++)
    {
        if ((wd->ap.apBitmap & (i<<i)) != 0)
        {
            sendFlag = 0;
            /* boardcast SSID */
            if ((len == 0) && (wd->ap.hideSsid[i] == 0))
            {
                sendFlag = 1;
            }
            /* Not broadcast SSID */
            else if (wd->ap.ssidLen[i] == len)
            {
                for (j=0; j<len; j++)
                {
                    if (zmw_rx_buf_readb(dev, buf, offset+1+j)
                            != wd->ap.ssid[i][j])
                    {
                        break;
                    }
                }
                if (j == len)
                {
                    sendFlag = 1;
                }
            }
            if (sendFlag == 1)
            {
                /* Send probe response */
                zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_PROBERSP, src, i, 0, 0);
            }
        }
    }
}

void zfStaProcessProbeRsp(zdev_t* dev, zbuf_t* buf, struct zsAdditionInfo* AddInfo)
{
    /* return if not channel scan */
    // Probe response is sent with unicast. Is this required?
    // IBSS would send probe request and the code below would prevent
    // the probe response from handling.
    #if 0
    zmw_get_wlan_dev(dev);

    if ( !wd->sta.bChannelScan )
    {
        return;
    }
    #endif

    zfProcessProbeRsp(dev, buf, AddInfo);
}

void zfIBSSSetupBssDesc(zdev_t *dev)
{
#ifdef ZM_ENABLE_IBSS_WPA2PSK
    u8_t i;
#endif
    struct zsBssInfo *pBssInfo;
    u16_t offset = 0;

    zmw_get_wlan_dev(dev);

    pBssInfo = &wd->sta.ibssBssDesc;
    zfZeroMemory((u8_t *)pBssInfo, sizeof(struct zsBssInfo));

    pBssInfo->signalStrength = 100;

    zfMemoryCopy((u8_t *)pBssInfo->macaddr, (u8_t *)wd->macAddr,6);
    zfMemoryCopy((u8_t *)pBssInfo->bssid, (u8_t *)wd->sta.bssid, 6);

    pBssInfo->beaconInterval[0] = (u8_t)(wd->beaconInterval) ;
    pBssInfo->beaconInterval[1] = (u8_t)((wd->beaconInterval) >> 8) ;

    pBssInfo->capability[0] = wd->sta.capability[0];
    pBssInfo->capability[1] = wd->sta.capability[1];

    pBssInfo->ssid[0] = ZM_WLAN_EID_SSID;
    pBssInfo->ssid[1] = wd->sta.ssidLen;
    zfMemoryCopy((u8_t *)&pBssInfo->ssid[2], (u8_t *)wd->sta.ssid, wd->sta.ssidLen);
    zfMemoryCopy((u8_t *)&pBssInfo->frameBody[offset], (u8_t *)pBssInfo->ssid,
                 wd->sta.ssidLen + 2);
    offset += wd->sta.ssidLen + 2;

    /* support rate */

    /* DS parameter set */
    pBssInfo->channel = zfChFreqToNum(wd->frequency, NULL);
    pBssInfo->frequency = wd->frequency;
    pBssInfo->atimWindow = wd->sta.atimWindow;

#ifdef ZM_ENABLE_IBSS_WPA2PSK
    if ( wd->sta.authMode == ZM_AUTH_MODE_WPA2PSK )
    {
        u8_t rsn[64]=
        {
                    /* Element ID */
                    0x30,
                    /* Length */
                    0x14,
                    /* Version */
                    0x01, 0x00,
                    /* Group Cipher Suite, default=TKIP */
                    0x00, 0x0f, 0xac, 0x04,
                    /* Pairwise Cipher Suite Count */
                    0x01, 0x00,
                    /* Pairwise Cipher Suite, default=TKIP */
                    0x00, 0x0f, 0xac, 0x02,
                    /* Authentication and Key Management Suite Count */
                    0x01, 0x00,
                    /* Authentication type, default=PSK */
                    0x00, 0x0f, 0xac, 0x02,
                    /* RSN capability */
                    0x00, 0x00
        };

        /* Overwrite Group Cipher Suite by AP's setting */
        zfMemoryCopy(rsn+4, zgWpa2AesOui, 4);

        if ( wd->sta.wepStatus == ZM_ENCRYPTION_AES )
        {
            /* Overwrite Pairwise Cipher Suite by AES */
            zfMemoryCopy(rsn+10, zgWpa2AesOui, 4);
        }

        // RSN element id
        pBssInfo->frameBody[offset++] = ZM_WLAN_EID_RSN_IE ;

        // RSN length
        pBssInfo->frameBody[offset++] = rsn[1] ;

        // RSN information
        for(i=0; i<rsn[1]; i++)
        {
            pBssInfo->frameBody[offset++] = rsn[i+2] ;
        }

        zfMemoryCopy(pBssInfo->rsnIe, rsn, rsn[1]+2);
    }
#endif
}

void zfIbssConnectNetwork(zdev_t* dev)
{
    struct zsBssInfo* pBssInfo;
    struct zsBssInfo tmpBssInfo;
    u8_t   macAddr[6], bssid[6], bssNotFound = TRUE;
    u16_t  i, j=100;
    u16_t  k;
    struct zsPartnerNotifyEvent event;
    u32_t  channelFlags;
    u16_t  oppositeWepStatus;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    /* change state to CONNECTING and stop the channel scanning */
    zfChangeAdapterState(dev, ZM_STA_STATE_CONNECTING);
    zfPowerSavingMgrWakeup(dev);

    /* Set TxQs CWMIN, CWMAX, AIFS and TXO to WME STA default. */
    zfUpdateDefaultQosParameter(dev, 0);

    wd->sta.bProtectionMode = FALSE;
    zfHpSetSlotTime(dev, 1);

    /* ESS bit off */
    wd->sta.capability[0] &= ~ZM_BIT_0;
    /* IBSS bit on */
    wd->sta.capability[0] |= ZM_BIT_1;
    /* not not use short slot time */
    wd->sta.capability[1] &= ~ZM_BIT_2;

    wd->sta.wmeConnected = 0;
    wd->sta.psMgr.tempWakeUp = 0;
    wd->sta.qosInfo = 0;
    wd->sta.EnableHT = 0;
    wd->BandWidth40 = 0;
    wd->ExtOffset = 0;

    if ( wd->sta.bssList.bssCount )
    {
        //Reorder BssList by RSSI--CWYang(+)
        zfBssInfoReorderList(dev);

        zmw_enter_critical_section(dev);

        pBssInfo = wd->sta.bssList.head;

        for(i=0; i<wd->sta.bssList.bssCount; i++)
        {
            // 20070806 #1 Privacy bit
            if ( pBssInfo->capability[0] & ZM_BIT_4 )
            { // Privacy Ibss network
//                zm_debug_msg0("Privacy bit on");
                oppositeWepStatus = ZM_ENCRYPTION_WEP_ENABLED;

                if ( pBssInfo->rsnIe[1] != 0 )
                {
                    if ( (pBssInfo->rsnIe[7] == 0x01) || (pBssInfo->rsnIe[7] == 0x05) )
                    { // WEP-40 & WEP-104
//                        zm_debug_msg0("WEP40 or WEP104");
                        oppositeWepStatus = ZM_ENCRYPTION_WEP_ENABLED;
                    }
                    else if ( pBssInfo->rsnIe[7] == 0x02 )
                    { // TKIP
//                        zm_debug_msg0("TKIP");
                        oppositeWepStatus = ZM_ENCRYPTION_TKIP;
                    }
                    else if ( pBssInfo->rsnIe[7] == 0x04 )
                    { // AES
//                        zm_debug_msg0("CCMP-AES");
                        oppositeWepStatus = ZM_ENCRYPTION_AES;
                    }
                }
            }
            else
            {
//                zm_debug_msg0("Privacy bit off");
                oppositeWepStatus = ZM_ENCRYPTION_WEP_DISABLED;
            }

            if ( (zfMemoryIsEqual(&(pBssInfo->ssid[2]), wd->sta.ssid,
                                  wd->sta.ssidLen))&&
                 (wd->sta.ssidLen == pBssInfo->ssid[1])&&
                 (oppositeWepStatus == wd->sta.wepStatus) )
            {
                /* Check support mode */
                if (pBssInfo->frequency > 3000) {
                    if (   (pBssInfo->EnableHT == 1)
                        || (pBssInfo->apCap & ZM_All11N_AP) ) //11n AP
                    {
                        channelFlags = CHANNEL_A_HT;
                        if (pBssInfo->enableHT40 == 1) {
                            channelFlags |= CHANNEL_HT40;
                        }
                    } else {
                        channelFlags = CHANNEL_A;
                    }
                } else {
                    if (   (pBssInfo->EnableHT == 1)
                        || (pBssInfo->apCap & ZM_All11N_AP) ) //11n AP
                    {
                        channelFlags = CHANNEL_G_HT;
                        if(pBssInfo->enableHT40 == 1) {
                            channelFlags |= CHANNEL_HT40;
                        }
                    } else {
                        if (pBssInfo->extSupportedRates[1] == 0) {
                            channelFlags = CHANNEL_B;
                        } else {
                            channelFlags = CHANNEL_G;
                        }
                    }
                }

                if (   ((channelFlags == CHANNEL_B) && (wd->connectMode & ZM_BIT_0))
                    || ((channelFlags == CHANNEL_G) && (wd->connectMode & ZM_BIT_1))
                    || ((channelFlags == CHANNEL_A) && (wd->connectMode & ZM_BIT_2))
                    || ((channelFlags & CHANNEL_HT20) && (wd->connectMode & ZM_BIT_3)) )
                {
                    pBssInfo = pBssInfo->next;
                    continue;
                }

                /* Bypass DFS channel */
                if (zfHpIsDfsChannelNCS(dev, pBssInfo->frequency))
                {
                    zm_debug_msg0("Bypass DFS channel");
                    continue;
                }

                /* check IBSS bit */
                if ( pBssInfo->capability[0] & ZM_BIT_1 )
                {
                    /* may check timestamp here */
                    j = i;
                    break;
                }
            }

            pBssInfo = pBssInfo->next;
        }

        if ((j < wd->sta.bssList.bssCount) && (pBssInfo != NULL))
        {
            zfwMemoryCopy((u8_t*)&tmpBssInfo, (u8_t*)(pBssInfo), sizeof(struct zsBssInfo));
            pBssInfo = &tmpBssInfo;
        }
        else
        {
            pBssInfo = NULL;
        }

        zmw_leave_critical_section(dev);

        //if ( j < wd->sta.bssList.bssCount )
        if (pBssInfo != NULL)
        {
            int res;

            zm_debug_msg0("IBSS found");

            /* Found IBSS, reset bssNotFoundCount */
            zmw_enter_critical_section(dev);
            wd->sta.bssNotFoundCount = 0;
            zmw_leave_critical_section(dev);

            bssNotFound = FALSE;
            wd->sta.atimWindow = pBssInfo->atimWindow;
            wd->frequency = pBssInfo->frequency;
            //wd->sta.flagFreqChanging = 1;
            zfCoreSetFrequency(dev, wd->frequency);
            zfUpdateBssid(dev, pBssInfo->bssid);
            zfResetSupportRate(dev, ZM_DEFAULT_SUPPORT_RATE_ZERO);
            zfUpdateSupportRate(dev, pBssInfo->supportedRates);
            zfUpdateSupportRate(dev, pBssInfo->extSupportedRates);
            wd->beaconInterval = pBssInfo->beaconInterval[0] +
                                 (((u16_t) pBssInfo->beaconInterval[1]) << 8);

            if (wd->beaconInterval == 0)
            {
                wd->beaconInterval = 100;
            }

            /* rsn information element */
            if ( pBssInfo->rsnIe[1] != 0 )
            {
                zfMemoryCopy(wd->sta.rsnIe, pBssInfo->rsnIe,
                             pBssInfo->rsnIe[1]+2);

#ifdef ZM_ENABLE_IBSS_WPA2PSK
                /* If not use RSNA , run traditional */
                zmw_enter_critical_section(dev);
                wd->sta.ibssWpa2Psk = 1;
                zmw_leave_critical_section(dev);
#endif
            }
            else
            {
                wd->sta.rsnIe[1] = 0;
            }

            /* privacy bit */
            if ( pBssInfo->capability[0] & ZM_BIT_4 )
            {
                wd->sta.capability[0] |= ZM_BIT_4;
            }
            else
            {
                wd->sta.capability[0] &= ~ZM_BIT_4;
            }

            /* preamble type */
            wd->preambleTypeInUsed = wd->preambleType;
            if ( wd->preambleTypeInUsed == ZM_PREAMBLE_TYPE_AUTO )
            {
                if (pBssInfo->capability[0] & ZM_BIT_5)
                {
                    wd->preambleTypeInUsed = ZM_PREAMBLE_TYPE_SHORT;
                }
                else
                {
                    wd->preambleTypeInUsed = ZM_PREAMBLE_TYPE_LONG;
                }
            }

            if (wd->preambleTypeInUsed == ZM_PREAMBLE_TYPE_LONG)
            {
                wd->sta.capability[0] &= ~ZM_BIT_5;
            }
            else
            {
                wd->sta.capability[0] |= ZM_BIT_5;
            }

            wd->sta.beaconFrameBodySize = pBssInfo->frameBodysize + 12;

            if (wd->sta.beaconFrameBodySize > ZM_CACHED_FRAMEBODY_SIZE)
            {
                wd->sta.beaconFrameBodySize = ZM_CACHED_FRAMEBODY_SIZE;
            }

            for (k=0; k<8; k++)
            {
                wd->sta.beaconFrameBody[k] = pBssInfo->timeStamp[k];
            }
            wd->sta.beaconFrameBody[8] = pBssInfo->beaconInterval[0];
            wd->sta.beaconFrameBody[9] = pBssInfo->beaconInterval[1];
            wd->sta.beaconFrameBody[10] = pBssInfo->capability[0];
            wd->sta.beaconFrameBody[11] = pBssInfo->capability[1];
            //for (k=12; k<wd->sta.beaconFrameBodySize; k++)
            for (k=0; k<pBssInfo->frameBodysize; k++)
            {
                wd->sta.beaconFrameBody[k+12] = pBssInfo->frameBody[k];
            }

            zmw_enter_critical_section(dev);
            res = zfStaSetOppositeInfoFromBSSInfo(dev, pBssInfo);
            if ( res == 0 )
            {
                zfMemoryCopy(event.bssid, (u8_t *)(pBssInfo->bssid), 6);
                zfMemoryCopy(event.peerMacAddr, (u8_t *)(pBssInfo->macaddr), 6);
            }
            zmw_leave_critical_section(dev);

            //zfwIbssPartnerNotify(dev, 1, &event);
            goto connect_done;
        }
    }

    /* IBSS not found */
    if ( bssNotFound )
    {
#ifdef ZM_ENABLE_IBSS_WPA2PSK
        u16_t offset ;
#endif
        if ( wd->sta.ibssJoinOnly )
        {
            zm_debug_msg0("IBSS join only...retry...");
            goto retry_ibss;
        }

        if(wd->sta.bssNotFoundCount<2)
        {
            zmw_enter_critical_section(dev);
            zm_debug_msg1("IBSS not found, do sitesurvey!!  bssNotFoundCount=", wd->sta.bssNotFoundCount);
            wd->sta.bssNotFoundCount++;
            zmw_leave_critical_section(dev);
            goto retry_ibss;
        }
        else
        {
            zmw_enter_critical_section(dev);
            /* Fail IBSS found, TODO create IBSS */
            wd->sta.bssNotFoundCount = 0;
            zmw_leave_critical_section(dev);
        }


        if (zfHpIsDfsChannel(dev, wd->frequency))
        {
            wd->frequency = zfHpFindFirstNonDfsChannel(dev, wd->frequency > 3000);
        }

        if( wd->ws.autoSetFrequency == 0 )
        { /* Auto set frequency */
            zm_debug_msg1("Create Ad Hoc Network Band ", wd->ws.adhocMode);
            wd->frequency = zfFindCleanFrequency(dev, wd->ws.adhocMode);
            wd->ws.autoSetFrequency = 0xff;
        }
        zm_debug_msg1("IBSS not found, created one in channel ", wd->frequency);

        wd->sta.ibssBssIsCreator = 1;

        //wd->sta.flagFreqChanging = 1;
        zfCoreSetFrequency(dev, wd->frequency);
        if (wd->sta.bDesiredBssid == TRUE)
        {
            for (k=0; k<6; k++)
            {
                bssid[k] = wd->sta.desiredBssid[k];
            }
        }
        else
        {
            #if 1
            macAddr[0] = (wd->macAddr[0] & 0xff);
            macAddr[1] = (wd->macAddr[0] >> 8);
            macAddr[2] = (wd->macAddr[1] & 0xff);
            macAddr[3] = (wd->macAddr[1] >> 8);
            macAddr[4] = (wd->macAddr[2] & 0xff);
            macAddr[5] = (wd->macAddr[2] >> 8);
            zfGenerateRandomBSSID(dev, (u8_t *)wd->macAddr, (u8_t *)bssid);
            #else
            for (k=0; k<6; k++)
            {
                bssid[k] = (u8_t) zfGetRandomNumber(dev, 0);
            }
            bssid[0] &= ~ZM_BIT_0;
            bssid[0] |= ZM_BIT_1;
            #endif
        }

        zfUpdateBssid(dev, bssid);
        //wd->sta.atimWindow = 0x0a;

        /* rate information */
        if(wd->frequency <= ZM_CH_G_14)  // 2.4 GHz  b+g
        {
            if ( wd->wfc.bIbssGMode
                 && (wd->supportMode & (ZM_WIRELESS_MODE_24_54|ZM_WIRELESS_MODE_24_N)) )
            {
                zfResetSupportRate(dev, ZM_DEFAULT_SUPPORT_RATE_IBSS_AG);
            }
            else
            {
                zfResetSupportRate(dev, ZM_DEFAULT_SUPPORT_RATE_IBSS_B);
            }
        } else {
            zfResetSupportRate(dev, ZM_DEFAULT_SUPPORT_RATE_IBSS_AG);
        }

        if ( wd->sta.wepStatus == ZM_ENCRYPTION_WEP_DISABLED )
        {
            wd->sta.capability[0] &= ~ZM_BIT_4;
        }
        else
        {
            wd->sta.capability[0] |= ZM_BIT_4;
        }

        wd->preambleTypeInUsed = wd->preambleType;
        if (wd->preambleTypeInUsed == ZM_PREAMBLE_TYPE_LONG)
        {
            wd->sta.capability[0] &= ~ZM_BIT_5;
        }
        else
        {
            wd->preambleTypeInUsed = ZM_PREAMBLE_TYPE_SHORT;
            wd->sta.capability[0] |= ZM_BIT_5;
        }

        zfIBSSSetupBssDesc(dev);

#ifdef ZM_ENABLE_IBSS_WPA2PSK

        // 20070411 Add WPA2PSK information to its IBSS network !!!
        offset = 0 ;

        /* timestamp */
        offset += 8 ;

        /* beacon interval */
        wd->sta.beaconFrameBody[offset++] = (u8_t)(wd->beaconInterval) ;
        wd->sta.beaconFrameBody[offset++] = (u8_t)((wd->beaconInterval) >> 8) ;

        /* capability information */
        wd->sta.beaconFrameBody[offset++] = wd->sta.capability[0] ;
        wd->sta.beaconFrameBody[offset++] = wd->sta.capability[1] ;
        #if 0
        /* ssid */
        // ssid element id
        wd->sta.beaconFrameBody[offset++] = ZM_WLAN_EID_SSID ;
        // ssid length
        wd->sta.beaconFrameBody[offset++] = wd->sta.ssidLen ;
        // ssid information
        for(i=0; i<wd->sta.ssidLen; i++)
        {
            wd->sta.beaconFrameBody[offset++] = wd->sta.ssid[i] ;
        }

        /* support rate */
        rateSet = ZM_RATE_SET_CCK ;
        if ( (rateSet == ZM_RATE_SET_OFDM)&&((wd->gRate & 0xff) == 0) )
        {
            offset += 0 ;
        }
        else
        {
            // support rate element id
            wd->sta.beaconFrameBody[offset++] = ZM_WLAN_EID_SUPPORT_RATE ;

            // support rate length
            lenOffset = offset++;

            // support rate information
            for (i=0; i<4; i++)
            {
                if ((wd->bRate & (0x1<<i)) == (0x1<<i))
                {
                    wd->sta.beaconFrameBody[offset++] =
                	    zg11bRateTbl[i]+((wd->bRateBasic & (0x1<<i))<<(7-i)) ;
                    len++;
                }
            }

            // support rate length
            wd->sta.beaconFrameBody[lenOffset] = len ;
        }

        /* DS parameter set */
        // DS parameter set elemet id
        wd->sta.beaconFrameBody[offset++] = ZM_WLAN_EID_DS ;

        // DS parameter set length
        wd->sta.beaconFrameBody[offset++] = 1 ;

        // DS parameter set information
        wd->sta.beaconFrameBody[offset++] =
         	zfChFreqToNum(wd->frequency, NULL) ;

        /* IBSS parameter set */
        // IBSS parameter set element id
        wd->sta.beaconFrameBody[offset++] = ZM_WLAN_EID_IBSS ;

        // IBSS parameter set length
        wd->sta.beaconFrameBody[offset++] = 2 ;

        // IBSS parameter set information
        wd->sta.beaconFrameBody[offset] = wd->sta.atimWindow ;
        offset += 2 ;

        /* ERP Information and Extended Supported Rates */
        if ( wd->wfc.bIbssGMode
             && (wd->supportMode & (ZM_WIRELESS_MODE_24_54|ZM_WIRELESS_MODE_24_N)) )
        {
            /* ERP Information */
            wd->erpElement = 0;
            // ERP element id
            wd->sta.beaconFrameBody[offset++] = ZM_WLAN_EID_ERP ;

            // ERP length
            wd->sta.beaconFrameBody[offset++] = 1 ;

            // ERP information
            wd->sta.beaconFrameBody[offset++] = wd->erpElement ;

            /* Extended Supported Rates */
            if ( (rateSet == ZM_RATE_SET_OFDM)&&((wd->gRate & 0xff) == 0) )
            {
                offset += 0 ;
            }
            else
            {
                len = 0 ;

                // Extended Supported Rates element id
                wd->sta.beaconFrameBody[offset++] = ZM_WLAN_EID_EXTENDED_RATE ;

                // Extended Supported Rates length
                lenOffset = offset++ ;

                // Extended Supported Rates information
                for (i=0; i<8; i++)
                {
                    if ((wd->gRate & (0x1<<i)) == (0x1<<i))
                    {
                        wd->sta.beaconFrameBody[offset++] =
                                     zg11gRateTbl[i]+((wd->gRateBasic & (0x1<<i))<<(7-i));
                        len++;
                    }
                }

                // extended support rate length
            	  wd->sta.beaconFrameBody[lenOffset] = len ;
            }
        }
        #endif

        /* RSN : important information influence the result of creating an IBSS network */
        if ( wd->sta.authMode == ZM_AUTH_MODE_WPA2PSK )
        {
            u8_t frameType = ZM_WLAN_FRAME_TYPE_AUTH ;
            u8_t    rsn[64]=
            {
                        /* Element ID */
                        0x30,
                        /* Length */
                        0x14,
                        /* Version */
                        0x01, 0x00,
                        /* Group Cipher Suite, default=TKIP */
                        0x00, 0x0f, 0xac, 0x04,
                        /* Pairwise Cipher Suite Count */
                        0x01, 0x00,
                        /* Pairwise Cipher Suite, default=TKIP */
                        0x00, 0x0f, 0xac, 0x02,
                        /* Authentication and Key Management Suite Count */
                        0x01, 0x00,
                        /* Authentication type, default=PSK */
                        0x00, 0x0f, 0xac, 0x02,
                        /* RSN capability */
                        0x00, 0x00
            };

            /* Overwrite Group Cipher Suite by AP's setting */
            zfMemoryCopy(rsn+4, zgWpa2AesOui, 4);

            if ( wd->sta.wepStatus == ZM_ENCRYPTION_AES )
            {
                /* Overwrite Pairwise Cipher Suite by AES */
                zfMemoryCopy(rsn+10, zgWpa2AesOui, 4);
            }

            // RSN element id
            wd->sta.beaconFrameBody[offset++] = ZM_WLAN_EID_RSN_IE ;

            // RSN length
            wd->sta.beaconFrameBody[offset++] = rsn[1] ;

            // RSN information
            for(i=0; i<rsn[1]; i++)
                wd->sta.beaconFrameBody[offset++] = rsn[i+2] ;

            zfMemoryCopy(wd->sta.rsnIe, rsn, rsn[1]+2);

#ifdef ZM_ENABLE_IBSS_WPA2PSK
            /* If not use RSNA , run traditional */
            zmw_enter_critical_section(dev);
            wd->sta.ibssWpa2Psk = 1;
            zmw_leave_critical_section(dev);
#endif
        }

        #if 0
        /* HT Capabilities Info */
        {
            u8_t OUI[3] = { 0x0 , 0x90 , 0x4C } ;

            wd->sta.beaconFrameBody[offset++] = ZM_WLAN_EID_WPA_IE ;

            wd->sta.beaconFrameBody[offset++] = wd->sta.HTCap.Data.Length + 4 ;

            for (i = 0; i < 3; i++)
            {
                wd->sta.beaconFrameBody[offset++] = OUI[i] ;
            }

            wd->sta.beaconFrameBody[offset++] = wd->sta.HTCap.Data.ElementID ;

            for (i = 0; i < 26; i++)
            {
                wd->sta.beaconFrameBody[offset++] = wd->sta.HTCap.Byte[i+2] ;
            }
        }

        /* Extended HT Capabilities Info */
        {
            u8_t OUI[3] = { 0x0 , 0x90 , 0x4C } ;

            wd->sta.beaconFrameBody[offset++] = ZM_WLAN_EID_WPA_IE ;

            wd->sta.beaconFrameBody[offset++] = wd->sta.ExtHTCap.Data.Length + 4 ;

            for (i = 0; i < 3; i++)
            {
                wd->sta.beaconFrameBody[offset++] = OUI[i] ;
            }

            wd->sta.beaconFrameBody[offset++] = wd->sta.ExtHTCap.Data.ElementID ;

            for (i = 0; i < 22; i++)
            {
                wd->sta.beaconFrameBody[offset++] = wd->sta.ExtHTCap.Byte[i+2] ;
            }
        }
        #endif

        wd->sta.beaconFrameBodySize = offset ;

        if (wd->sta.beaconFrameBodySize > ZM_CACHED_FRAMEBODY_SIZE)
        {
            wd->sta.beaconFrameBodySize = ZM_CACHED_FRAMEBODY_SIZE;
        }

        // 20070416 Let Create IBSS network could enter the zfwIbssPartnerNotify function
        // bssNotFound = FALSE ;

        printk("The capability info 1 = %02x\n", wd->sta.capability[0]) ;
        printk("The capability info 2 = %02x\n", wd->sta.capability[1]) ;
        for(k=0; k<wd->sta.beaconFrameBodySize; k++)
        {
	     printk("%02x ", wd->sta.beaconFrameBody[k]) ;
        }
        #if 0
        zmw_enter_critical_section(dev);
        zfMemoryCopy(event.bssid, (u8_t *)bssid, 6);
        zfMemoryCopy(event.peerMacAddr, (u8_t *)wd->macAddr, 6);
        zmw_leave_critical_section(dev);
        #endif
#endif

        //zmw_enter_critical_section(dev);
        //wd->sta.ibssPartnerStatus = ZM_IBSS_PARTNER_LOST;
        //zmw_leave_critical_section(dev);
    }
    else
    {
        wd->sta.ibssBssIsCreator = 0;
    }

connect_done:
    zfHpEnableBeacon(dev, ZM_MODE_IBSS, wd->beaconInterval, wd->dtim, (u8_t)wd->sta.atimWindow);
    zfStaSendBeacon(dev); // Refresh Beacon content for ZD1211B HalPlus
    zfHpSetAtimWindow(dev, wd->sta.atimWindow);

    // Start the IBSS timer to monitor for new stations
    zmw_enter_critical_section(dev);
    zfTimerSchedule(dev, ZM_EVENT_IBSS_MONITOR, ZM_TICK_IBSS_MONITOR);
    zmw_leave_critical_section(dev);


    if (wd->zfcbConnectNotify != NULL)
    {
        wd->zfcbConnectNotify(dev, ZM_STATUS_MEDIA_CONNECT, wd->sta.bssid);
    }
    zfChangeAdapterState(dev, ZM_STA_STATE_CONNECTED);
    wd->sta.connPowerInHalfDbm = zfHpGetTransmitPower(dev);

#ifdef ZM_ENABLE_IBSS_DELAYED_JOIN_INDICATION
    if ( !bssNotFound )
    {
        wd->sta.ibssDelayedInd = 1;
        zfMemoryCopy((u8_t *)&wd->sta.ibssDelayedIndEvent, (u8_t *)&event, sizeof(struct zsPartnerNotifyEvent));
    }
#else
    if ( !bssNotFound )
    {
        if (wd->zfcbIbssPartnerNotify != NULL)
        {
            wd->zfcbIbssPartnerNotify(dev, 1, &event);
        }
    }
#endif

    return;

retry_ibss:
    zfChangeAdapterState(dev, ZM_STA_STATE_CONNECTING);
    zfStaConnectFail(dev, ZM_STATUS_MEDIA_DISCONNECT_NOT_FOUND, wd->sta.bssid, 0);
    return;
}

void zfStaProcessAtim(zdev_t* dev, zbuf_t* buf)
{
    zmw_get_wlan_dev(dev);

    zm_debug_msg0("Receiving Atim window notification");

    wd->sta.recvAtim = 1;
}

static struct zsBssInfo* zfInfraFindAPToConnect(zdev_t* dev,
        struct zsBssInfo* candidateBss)
{
    struct zsBssInfo* pBssInfo;
    struct zsBssInfo* pNowBssInfo=NULL;
    u16_t i;
    u16_t ret, apWepStatus;
    u32_t k;
    u32_t channelFlags;

    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    pBssInfo = wd->sta.bssList.head;

    for(i=0; i<wd->sta.bssList.bssCount; i++)
    {
        if ( pBssInfo->capability[0] & ZM_BIT_4 )
        {
            apWepStatus = ZM_ENCRYPTION_WEP_ENABLED;
        }
        else
        {
            apWepStatus = ZM_ENCRYPTION_WEP_DISABLED;
        }

        if ( ((zfMemoryIsEqual(&(pBssInfo->ssid[2]), wd->sta.ssid,
                               wd->sta.ssidLen))&&
              (wd->sta.ssidLen == pBssInfo->ssid[1]))||
             ((wd->sta.ssidLen == 0)&&
               /* connect to any BSS: AP's ans STA's WEP status must match */
              (wd->sta.wepStatus == apWepStatus )&&
              (pBssInfo->securityType != ZM_SECURITY_TYPE_WPA) ))
        {
            if ( wd->sta.ssidLen == 0 )
            {
                zm_debug_msg0("ANY BSS found");
            }

            if ( ((wd->sta.wepStatus == ZM_ENCRYPTION_WEP_DISABLED && apWepStatus == ZM_ENCRYPTION_WEP_ENABLED) ||
                 (wd->sta.wepStatus == ZM_ENCRYPTION_WEP_ENABLED &&
                 (apWepStatus == ZM_ENCRYPTION_WEP_DISABLED && wd->sta.dropUnencryptedPkts == 1))) &&
                 (wd->sta.authMode >= ZM_AUTH_MODE_OPEN && wd->sta.authMode <= ZM_AUTH_MODE_AUTO) )
            {
                zm_debug_msg0("Privacy policy is inconsistent");
                pBssInfo = pBssInfo->next;
                continue;
            }

            /* for WPA negative test */
            if ( !zfCheckAuthentication(dev, pBssInfo) )
            {
                pBssInfo = pBssInfo->next;
                continue;
            }

            /* Check bssid */
            if (wd->sta.bDesiredBssid == TRUE)
            {
                for (k=0; k<6; k++)
                {
                    if (wd->sta.desiredBssid[k] != pBssInfo->bssid[k])
                    {
                        zm_msg0_mm(ZM_LV_1, "desired bssid not matched 1");
                        break;
                    }
                }

                if (k != 6)
                {
                    zm_msg0_mm(ZM_LV_1, "desired bssid not matched 2");
                    pBssInfo = pBssInfo->next;
                    continue;
                }
            }

            /* Check support mode */
            if (pBssInfo->frequency > 3000) {
                if (   (pBssInfo->EnableHT == 1)
                    || (pBssInfo->apCap & ZM_All11N_AP) ) //11n AP
                {
                    channelFlags = CHANNEL_A_HT;
                    if (pBssInfo->enableHT40 == 1) {
                        channelFlags |= CHANNEL_HT40;
                    }
                } else {
                    channelFlags = CHANNEL_A;
                }
            } else {
                if (   (pBssInfo->EnableHT == 1)
                    || (pBssInfo->apCap & ZM_All11N_AP) ) //11n AP
                {
                    channelFlags = CHANNEL_G_HT;
                    if(pBssInfo->enableHT40 == 1) {
                        channelFlags |= CHANNEL_HT40;
                    }
                } else {
                    if (pBssInfo->extSupportedRates[1] == 0) {
                        channelFlags = CHANNEL_B;
                    } else {
                        channelFlags = CHANNEL_G;
                    }
                }
            }

            if (   ((channelFlags == CHANNEL_B) && (wd->connectMode & ZM_BIT_0))
                || ((channelFlags == CHANNEL_G) && (wd->connectMode & ZM_BIT_1))
                || ((channelFlags == CHANNEL_A) && (wd->connectMode & ZM_BIT_2))
                || ((channelFlags & CHANNEL_HT20) && (wd->connectMode & ZM_BIT_3)) )
            {
                pBssInfo = pBssInfo->next;
                continue;
            }

            /* Skip if AP in blocking list */
            if ((ret = zfStaIsApInBlockingList(dev, pBssInfo->bssid)) == TRUE)
            {
                zm_msg0_mm(ZM_LV_0, "Candidate AP in blocking List, skip if there's stilla choice!");
                pNowBssInfo = pBssInfo;
                pBssInfo = pBssInfo->next;
                continue;
            }

            if ( pBssInfo->capability[0] & ZM_BIT_0 )  // check if infra-BSS
            {
                    pNowBssInfo = pBssInfo;
                    wd->sta.apWmeCapability = pBssInfo->wmeSupport;


                    goto done;
            }
        }

        pBssInfo = pBssInfo->next;
    }

done:
    if (pNowBssInfo != NULL)
    {
        zfwMemoryCopy((void*)candidateBss, (void*)pNowBssInfo, sizeof(struct zsBssInfo));
        pNowBssInfo = candidateBss;
    }

    zmw_leave_critical_section(dev);

    return pNowBssInfo;
}


void zfInfraConnectNetwork(zdev_t* dev)
{
    struct zsBssInfo* pBssInfo;
    struct zsBssInfo* pNowBssInfo=NULL;
    struct zsBssInfo candidateBss;
    //u16_t i, j=100, quality=10000;
    //u8_t ret=FALSE, apWepStatus;
    u8_t ret=FALSE;
    u16_t k;
    u8_t density = ZM_MPDU_DENSITY_NONE;

    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    /* Reset bssNotFoundCount for Ad-Hoc:IBSS */
    /* Need review : IbssConn -> InfraConn -> IbssConn etc, flag/counter reset? */
    zmw_enter_critical_section(dev);
    wd->sta.bssNotFoundCount = 0;
    zmw_leave_critical_section(dev);

    /* Set TxQs CWMIN, CWMAX, AIFS and TXO to WME STA default. */
    zfUpdateDefaultQosParameter(dev, 0);

    zfStaRefreshBlockList(dev, 0);

    /* change state to CONNECTING and stop the channel scanning */
    zfChangeAdapterState(dev, ZM_STA_STATE_CONNECTING);
    zfPowerSavingMgrWakeup(dev);

    wd->sta.wmeConnected = 0;
    wd->sta.psMgr.tempWakeUp = 0;
    wd->sta.qosInfo = 0;
    zfQueueFlush(dev, wd->sta.uapsdQ);

    wd->sta.connectState = ZM_STA_CONN_STATE_NONE;

    //Reorder BssList by RSSI--CWYang(+)
    zfBssInfoReorderList(dev);

    pNowBssInfo = zfInfraFindAPToConnect(dev, &candidateBss);

	if (wd->sta.SWEncryptEnable != 0)
	{
	    if (wd->sta.bSafeMode == 0)
	    {
		    zfStaDisableSWEncryption(dev);//Quickly reboot
	    }
	}
    if ( pNowBssInfo != NULL )
    {
        //zm_assert(pNowBssInfo != NULL);

        pBssInfo = pNowBssInfo;
        wd->sta.ssidLen = pBssInfo->ssid[1];
        zfMemoryCopy(wd->sta.ssid, &(pBssInfo->ssid[2]), pBssInfo->ssid[1]);
        wd->frequency = pBssInfo->frequency;
        //wd->sta.flagFreqChanging = 1;

        //zfCoreSetFrequency(dev, wd->frequency);
        zfUpdateBssid(dev, pBssInfo->bssid);
        zfResetSupportRate(dev, ZM_DEFAULT_SUPPORT_RATE_ZERO);
        zfUpdateSupportRate(dev, pBssInfo->supportedRates);
        zfUpdateSupportRate(dev, pBssInfo->extSupportedRates);

        wd->beaconInterval = pBssInfo->beaconInterval[0] +
                             (((u16_t) pBssInfo->beaconInterval[1]) << 8);
        if (wd->beaconInterval == 0)
        {
            wd->beaconInterval = 100;
        }

        /* ESS bit on */
        wd->sta.capability[0] |= ZM_BIT_0;
        /* IBSS bit off */
        wd->sta.capability[0] &= ~ZM_BIT_1;

        /* 11n AP flag */
        wd->sta.EnableHT = pBssInfo->EnableHT;
        wd->sta.SG40 = pBssInfo->SG40;
#ifdef ZM_ENABLE_CENC
        if ( pBssInfo->securityType == ZM_SECURITY_TYPE_CENC )
        {
            wd->sta.wmeEnabled = 0; //Disable WMM in CENC
            cencInit(dev);
            cencSetCENCMode(dev, NdisCENC_PSK);
            wd->sta.wpaState = ZM_STA_WPA_STATE_INIT;
            /* CENC */
            if ( pBssInfo->cencIe[1] != 0 )
            {
                //wd->sta.wepStatus = ZM_ENCRYPTION_CENC;
                //wd->sta.encryMode = ZM_CENC;
                zfwCencHandleBeaconProbrespon(dev, (u8_t *)&pBssInfo->cencIe,
                        (u8_t *)&pBssInfo->ssid, (u8_t *)&pBssInfo->macaddr);
                zfMemoryCopy(wd->sta.cencIe, pBssInfo->cencIe,
                        pBssInfo->cencIe[1]+2);
            }
            else
            {
                wd->sta.cencIe[1] = 0;
            }
        }
#endif //ZM_ENABLE_CENC
        if ( pBssInfo->securityType == ZM_SECURITY_TYPE_WPA )
        {
            wd->sta.wpaState = ZM_STA_WPA_STATE_INIT;

            if ( wd->sta.wepStatus == ZM_ENCRYPTION_TKIP )
            {
                wd->sta.encryMode = ZM_TKIP;

                /* Turn on software encryption/decryption for TKIP */
                if (wd->sta.EnableHT == 1)
                {
                    zfStaEnableSWEncryption(dev, (ZM_SW_TKIP_ENCRY_EN|ZM_SW_TKIP_DECRY_EN));
                }

                /* Do not support TKIP in 11n mode */
                //wd->sta.EnableHT = 0;
                //pBssInfo->enableHT40 = 0;
            }
            else if ( wd->sta.wepStatus == ZM_ENCRYPTION_AES )
            {
                wd->sta.encryMode = ZM_AES;

                /* If AP supports HT mode */
                if (wd->sta.EnableHT)
                {
                    /* Set MPDU density to 8 us*/
                    density = ZM_MPDU_DENSITY_8US;
                }
            }

            if ( pBssInfo->wpaIe[1] != 0 )
            {
                zfMemoryCopy(wd->sta.wpaIe, pBssInfo->wpaIe,
                             pBssInfo->wpaIe[1]+2);
            }
            else
            {
                wd->sta.wpaIe[1] = 0;
            }

            if ( pBssInfo->rsnIe[1] != 0 )
            {
                zfMemoryCopy(wd->sta.rsnIe, pBssInfo->rsnIe,
                             pBssInfo->rsnIe[1]+2);
            }
            else
            {
                wd->sta.rsnIe[1] = 0;
            }
        }



        /* check preamble bit */
        wd->preambleTypeInUsed = wd->preambleType;
        if ( wd->preambleTypeInUsed == ZM_PREAMBLE_TYPE_AUTO )
        {
            if (pBssInfo->capability[0] & ZM_BIT_5)
            {
                wd->preambleTypeInUsed = ZM_PREAMBLE_TYPE_SHORT;
            }
            else
            {
                wd->preambleTypeInUsed = ZM_PREAMBLE_TYPE_LONG;
            }
        }

        if (wd->preambleTypeInUsed == ZM_PREAMBLE_TYPE_LONG)
        {
            wd->sta.capability[0] &= ~ZM_BIT_5;
        }
        else
        {
            wd->sta.capability[0] |= ZM_BIT_5;
        }

        /* check 802.11n 40MHz Setting */
        if ((pBssInfo->enableHT40 == 1) &&
            ((pBssInfo->extChOffset == 1) || (pBssInfo->extChOffset == 3)))
        {
            wd->BandWidth40 = pBssInfo->enableHT40;
            wd->ExtOffset = pBssInfo->extChOffset;
        }
        else
        {
            wd->BandWidth40 = 0;
            wd->ExtOffset = 0;
        }

        /* check 802.11H support bit */

        /* check Owl Ap */
        if ( pBssInfo->athOwlAp & ZM_BIT_0 )
        {
            /* In this function, FW retry will be enable, ZM_MAC_REG_RETRY_MAX
               will be set to 0.
             */
            zfHpDisableHwRetry(dev);
            wd->sta.athOwlAp = 1;
            /* Set MPDU density to 8 us*/
            density = ZM_MPDU_DENSITY_8US;
        }
        else
        {
            /* In this function, FW retry will be disable, ZM_MAC_REG_RETRY_MAX
               will be set to 3.
             */
            zfHpEnableHwRetry(dev);
            wd->sta.athOwlAp = 0;
        }
        wd->reorder = 1;

        /* Set MPDU density */
        zfHpSetMPDUDensity(dev, density);

        /* check short slot time bit */
        if ( pBssInfo->capability[1] & ZM_BIT_2 )
        {
            wd->sta.capability[1] |= ZM_BIT_2;
        }

        if ( pBssInfo->erp & ZM_BIT_1 )
        {
            //zm_debug_msg0("protection mode on");
            wd->sta.bProtectionMode = TRUE;
            zfHpSetSlotTime(dev, 0);
        }
        else
        {
            //zm_debug_msg0("protection mode off");
            wd->sta.bProtectionMode = FALSE;
            zfHpSetSlotTime(dev, 1);
        }

        if (pBssInfo->marvelAp == 1)
        {
            wd->sta.enableDrvBA = 0;
            /*
             * 8701 : NetGear 3500 (MARVELL)
             * Downlink issue : set slottime to 20.
             */
            zfHpSetSlotTimeRegister(dev, 0);
        }
        else
        {
            wd->sta.enableDrvBA = 1;

            /*
             * This is not good for here do reset slot time.
             * I think it should reset when leave MARVELL ap
             * or enter disconnect state etc.
             */
            zfHpSetSlotTimeRegister(dev, 1);
        }

        //Store probe response frame body, for VISTA only
        wd->sta.beaconFrameBodySize = pBssInfo->frameBodysize + 12;
        if (wd->sta.beaconFrameBodySize > ZM_CACHED_FRAMEBODY_SIZE)
        {
            wd->sta.beaconFrameBodySize = ZM_CACHED_FRAMEBODY_SIZE;
        }
        for (k=0; k<8; k++)
        {
            wd->sta.beaconFrameBody[k] = pBssInfo->timeStamp[k];
        }
        wd->sta.beaconFrameBody[8] = pBssInfo->beaconInterval[0];
        wd->sta.beaconFrameBody[9] = pBssInfo->beaconInterval[1];
        wd->sta.beaconFrameBody[10] = pBssInfo->capability[0];
        wd->sta.beaconFrameBody[11] = pBssInfo->capability[1];
        for (k=0; k<(wd->sta.beaconFrameBodySize - 12); k++)
        {
            wd->sta.beaconFrameBody[k+12] = pBssInfo->frameBody[k];
        }

        if ( ( pBssInfo->capability[0] & ZM_BIT_4 )&&
             (( wd->sta.authMode == ZM_AUTH_MODE_OPEN )||
              ( wd->sta.authMode == ZM_AUTH_MODE_SHARED_KEY)||
              (wd->sta.authMode == ZM_AUTH_MODE_AUTO)) )
        {   /* privacy enabled */

            if ( wd->sta.wepStatus == ZM_ENCRYPTION_WEP_DISABLED )
            {
                zm_debug_msg0("Adapter is no WEP, try to connect to WEP AP");
                ret = FALSE;
            }

            /* Do not support WEP in 11n mode */
            if ( wd->sta.wepStatus == ZM_ENCRYPTION_WEP_ENABLED )
            {
                /* Turn on software encryption/decryption for WEP */
                if (wd->sta.EnableHT == 1)
                {
                    zfStaEnableSWEncryption(dev, (ZM_SW_WEP_ENCRY_EN|ZM_SW_WEP_DECRY_EN));
                }

                //wd->sta.EnableHT = 0;
                //wd->BandWidth40 = 0;
                //wd->ExtOffset = 0;
            }

            wd->sta.capability[0] |= ZM_BIT_4;

            if ( wd->sta.authMode == ZM_AUTH_MODE_AUTO )
            { /* Try to use open and shared-key authehtication alternatively */
                if ( (wd->sta.connectTimeoutCount % 2) == 0 )
                    wd->sta.bIsSharedKey = 0;
                else
                    wd->sta.bIsSharedKey = 1;
            }
            else if ( wd->sta.authMode != ZM_AUTH_MODE_SHARED_KEY )
            {   /* open  or auto */
                //zfStaStartConnect(dev, 0);
                wd->sta.bIsSharedKey = 0;
            }
            else if ( wd->sta.authMode != ZM_AUTH_MODE_OPEN )
            {   /* shared key */
                //zfStaStartConnect(dev, 1) ;
                wd->sta.bIsSharedKey = 1;
            }
        }
        else
        {
            if ( (pBssInfo->securityType == ZM_SECURITY_TYPE_WPA)||
                 (pBssInfo->capability[0] & ZM_BIT_4) )
            {
                wd->sta.capability[0] |= ZM_BIT_4;
                /* initialize WPA related parameters */
            }
            else
            {
                wd->sta.capability[0] &= (~ZM_BIT_4);
            }

            /* authentication with open system */
            //zfStaStartConnect(dev, 0);
            wd->sta.bIsSharedKey = 0;
        }

        /* Improve WEP/TKIP performace with HT AP, detail information please look bug#32495 */
        /*
        if ( (pBssInfo->broadcomHTAp == 1)
             && (wd->sta.SWEncryptEnable != 0) )
        {
            zfHpSetTTSIFSTime(dev, 0xa);
        }
        else
        {
            zfHpSetTTSIFSTime(dev, 0x8);
        }
        */
    }
    else
    {
        zm_debug_msg0("Desired SSID not found");
        goto zlConnectFailed;
    }


    zfCoreSetFrequencyV2(dev, wd->frequency, zfStaStartConnectCb);
    return;

zlConnectFailed:
    zfStaConnectFail(dev, ZM_STATUS_MEDIA_DISCONNECT_NOT_FOUND, wd->sta.bssid, 0);
    return;
}

u8_t zfCheckWPAAuth(zdev_t* dev, struct zsBssInfo* pBssInfo)
{
    u8_t   ret=TRUE;
    u8_t   pmkCount;
    u8_t   i;
    u16_t   encAlgoType = 0;

    zmw_get_wlan_dev(dev);

    if ( wd->sta.wepStatus == ZM_ENCRYPTION_TKIP )
    {
        encAlgoType = ZM_TKIP;
    }
    else if ( wd->sta.wepStatus == ZM_ENCRYPTION_AES )
    {
        encAlgoType = ZM_AES;
    }

    switch(wd->sta.authMode)
    {
        case ZM_AUTH_MODE_WPA:
        case ZM_AUTH_MODE_WPAPSK:
            if ( pBssInfo->wpaIe[1] == 0 )
            {
                ret = FALSE;
                break;
            }

            pmkCount = pBssInfo->wpaIe[12];
            for(i=0; i < pmkCount; i++)
            {
                if ( pBssInfo->wpaIe[17 + 4*i] == encAlgoType )
                {
                    ret = TRUE;
                    goto done;
                }
            }

            ret = FALSE;
            break;

        case ZM_AUTH_MODE_WPA2:
        case ZM_AUTH_MODE_WPA2PSK:
            if ( pBssInfo->rsnIe[1] == 0 )
            {
                ret = FALSE;
                break;
            }

            pmkCount = pBssInfo->rsnIe[8];
            for(i=0; i < pmkCount; i++)
            {
                if ( pBssInfo->rsnIe[13 + 4*i] == encAlgoType )
                {
                    ret = TRUE;
                    goto done;
                }
            }

            ret = FALSE;
            break;
    }

done:
    return ret;
}

u8_t zfCheckAuthentication(zdev_t* dev, struct zsBssInfo* pBssInfo)
{
    u8_t   ret=TRUE;
    u16_t  encAlgoType;
    u16_t UnicastCipherNum;

    zmw_get_wlan_dev(dev);

    /* Connecting to ANY has been checked */
    if ( wd->sta.ssidLen == 0 )
    {
        return ret;
    }


	switch(wd->sta.authMode)
	//switch(wd->ws.authMode)//Quickly reboot
    {
        case ZM_AUTH_MODE_WPA_AUTO:
        case ZM_AUTH_MODE_WPAPSK_AUTO:
            encAlgoType = 0;
            if(pBssInfo->rsnIe[1] != 0)
            {
                UnicastCipherNum = (pBssInfo->rsnIe[8]) +
                                   (pBssInfo->rsnIe[9] << 8);

                /* If there is only one unicast cipher */
                if (UnicastCipherNum == 1)
                {
                    encAlgoType = pBssInfo->rsnIe[13];
                    //encAlgoType = pBssInfo->rsnIe[7];
                }
                else
                {
                    u16_t ii;
                    u16_t desiredCipher = 0;
                    u16_t IEOffSet = 13;

                    /* Enumerate all the supported unicast cipher */
                    for (ii = 0; ii < UnicastCipherNum; ii++)
                    {
                        if (pBssInfo->rsnIe[IEOffSet+ii*4] > desiredCipher)
                        {
                            desiredCipher = pBssInfo->rsnIe[IEOffSet+ii*4];
                        }
                    }

                    encAlgoType = desiredCipher;
                }

                if ( encAlgoType == 0x02 )
                {
    			    wd->sta.wepStatus = ZM_ENCRYPTION_TKIP;

    			    if ( wd->sta.authMode == ZM_AUTH_MODE_WPA_AUTO )
                    {
                        wd->sta.currentAuthMode = ZM_AUTH_MODE_WPA2;
                    }
                    else //ZM_AUTH_MODE_WPAPSK_AUTO
                    {
                        wd->sta.currentAuthMode = ZM_AUTH_MODE_WPA2PSK;
                    }
                }
                else if ( encAlgoType == 0x04 )
                {
                    wd->sta.wepStatus = ZM_ENCRYPTION_AES;

                    if ( wd->sta.authMode == ZM_AUTH_MODE_WPA_AUTO )
                    {
                        wd->sta.currentAuthMode = ZM_AUTH_MODE_WPA2;
                    }
                    else //ZM_AUTH_MODE_WPAPSK_AUTO
                    {
                        wd->sta.currentAuthMode = ZM_AUTH_MODE_WPA2PSK;
                    }
                }
                else
                {
                    ret = FALSE;
                }
            }
            else if(pBssInfo->wpaIe[1] != 0)
            {
                UnicastCipherNum = (pBssInfo->wpaIe[12]) +
                                   (pBssInfo->wpaIe[13] << 8);

                /* If there is only one unicast cipher */
                if (UnicastCipherNum == 1)
                {
                    encAlgoType = pBssInfo->wpaIe[17];
                    //encAlgoType = pBssInfo->wpaIe[11];
                }
                else
                {
                    u16_t ii;
                    u16_t desiredCipher = 0;
                    u16_t IEOffSet = 17;

                    /* Enumerate all the supported unicast cipher */
                    for (ii = 0; ii < UnicastCipherNum; ii++)
                    {
                        if (pBssInfo->wpaIe[IEOffSet+ii*4] > desiredCipher)
                        {
                            desiredCipher = pBssInfo->wpaIe[IEOffSet+ii*4];
                        }
                    }

                    encAlgoType = desiredCipher;
                }

                if ( encAlgoType == 0x02 )
                {
    			    wd->sta.wepStatus = ZM_ENCRYPTION_TKIP;

    			    if ( wd->sta.authMode == ZM_AUTH_MODE_WPA_AUTO )
                    {
                        wd->sta.currentAuthMode = ZM_AUTH_MODE_WPA;
                    }
                    else //ZM_AUTH_MODE_WPAPSK_AUTO
                    {
                        wd->sta.currentAuthMode = ZM_AUTH_MODE_WPAPSK;
                    }
                }
                else if ( encAlgoType == 0x04 )
                {
                    wd->sta.wepStatus = ZM_ENCRYPTION_AES;

                    if ( wd->sta.authMode == ZM_AUTH_MODE_WPA_AUTO )
                    {
                        wd->sta.currentAuthMode = ZM_AUTH_MODE_WPA;
                    }
                    else //ZM_AUTH_MODE_WPAPSK_AUTO
                    {
                        wd->sta.currentAuthMode = ZM_AUTH_MODE_WPAPSK;
                    }
                }
                else
                {
                    ret = FALSE;
                }


            }
            else
            {
                ret = FALSE;
            }

            break;

        case ZM_AUTH_MODE_WPA:
        case ZM_AUTH_MODE_WPAPSK:
        case ZM_AUTH_MODE_WPA_NONE:
        case ZM_AUTH_MODE_WPA2:
        case ZM_AUTH_MODE_WPA2PSK:
            {
                if ( pBssInfo->securityType != ZM_SECURITY_TYPE_WPA )
                {
                    ret = FALSE;
                }

                ret = zfCheckWPAAuth(dev, pBssInfo);
            }
            break;

        case ZM_AUTH_MODE_OPEN:
        case ZM_AUTH_MODE_SHARED_KEY:
        case ZM_AUTH_MODE_AUTO:
            {
                if ( pBssInfo->wscIe[1] )
                {
                    // If the AP is a Jumpstart AP, it's ok!! Ray
                    break;
                }
                else if ( pBssInfo->securityType == ZM_SECURITY_TYPE_WPA )
                {
                    ret = FALSE;
                }
            }
            break;

        default:
            break;
    }

    return ret;
}

u8_t zfStaIsConnected(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    if ( wd->sta.adapterState == ZM_STA_STATE_CONNECTED )
    {
        return TRUE;
    }

    return FALSE;
}

u8_t zfStaIsConnecting(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    if ( wd->sta.adapterState == ZM_STA_STATE_CONNECTING )
    {
        return TRUE;
    }

    return FALSE;
}

u8_t zfStaIsDisconnect(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    if ( wd->sta.adapterState == ZM_STA_STATE_DISCONNECT )
    {
        return TRUE;
    }

    return FALSE;
}

u8_t zfChangeAdapterState(zdev_t* dev, u8_t newState)
{
    u8_t ret = TRUE;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    //if ( newState == wd->sta.adapterState )
    //{
    //    return FALSE;
    //}

    switch(newState)
    {
    case ZM_STA_STATE_DISCONNECT:
        zfResetSupportRate(dev, ZM_DEFAULT_SUPPORT_RATE_DISCONNECT);

        #if 1
            zfScanMgrScanStop(dev, ZM_SCAN_MGR_SCAN_INTERNAL);
        #else
            if ( wd->sta.bChannelScan )
            {
                /* stop the action of channel scanning */
                wd->sta.bChannelScan = FALSE;
                ret =  TRUE;
                break;
            }
        #endif

        break;
    case ZM_STA_STATE_CONNECTING:
        #if 1
            zfScanMgrScanStop(dev, ZM_SCAN_MGR_SCAN_INTERNAL);
        #else
            if ( wd->sta.bChannelScan )
            {
                /* stop the action of channel scanning */
                wd->sta.bChannelScan = FALSE;
                ret =  TRUE;
                break;
            }
        #endif

        break;
    case ZM_STA_STATE_CONNECTED:
        break;
    default:
        break;
    }

    //if ( ret )
    //{
        zmw_enter_critical_section(dev);
        wd->sta.adapterState = newState;
        zmw_leave_critical_section(dev);

        zm_debug_msg1("change adapter state = ", newState);
    //}

    return ret;
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfStaMmAddIeSsid            */
/*      Add information element SSID to buffer.                         */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : buffer to add information element                         */
/*      offset : add information element from this offset               */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      buffer offset after adding information element                  */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Ji-Huang Lee        ZyDAS Technology Corporation    2005.11     */
/*                                                                      */
/************************************************************************/
u16_t zfStaAddIeSsid(zdev_t* dev, zbuf_t* buf, u16_t offset)
{
    u16_t i;

    zmw_get_wlan_dev(dev);

    /* Element ID */
    zmw_tx_buf_writeb(dev, buf, offset++, ZM_WLAN_EID_SSID);

    /* Element Length */
    zmw_tx_buf_writeb(dev, buf, offset++, wd->sta.ssidLen);

    /* Information : SSID */
    for (i=0; i<wd->sta.ssidLen; i++)
    {
        zmw_tx_buf_writeb(dev, buf, offset++, wd->sta.ssid[i]);
    }

    return offset;
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfStaMmAddIeWpa             */
/*      Add information element SSID to buffer.                         */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : buffer to add information element                         */
/*      offset : add information element from this offset               */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      buffer offset after adding information element                  */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Ji-Huang Lee        ZyDAS Technology Corporation    2006.01     */
/*                                                                      */
/************************************************************************/
u16_t zfStaAddIeWpaRsn(zdev_t* dev, zbuf_t* buf, u16_t offset, u8_t frameType)
{
    u32_t  i;
    u8_t    ssn[64]={
                        /* Element ID */
                        0xdd,
                        /* Length */
                        0x18,
                        /* OUI type */
                        0x00, 0x50, 0xf2, 0x01,
                        /* Version */
                        0x01, 0x00,
                        /* Group Cipher Suite, default=TKIP */
                        0x00, 0x50, 0xf2, 0x02,
                        /* Pairwise Cipher Suite Count */
                        0x01, 0x00,
                        /* Pairwise Cipher Suite, default=TKIP */
                        0x00, 0x50, 0xf2, 0x02,
                        /* Authentication and Key Management Suite Count */
                        0x01, 0x00,
                        /* Authentication type, default=PSK */
                        0x00, 0x50, 0xf2, 0x02,
                        /* WPA capability */
                        0x00, 0x00
                    };

    u8_t    rsn[64]={
                        /* Element ID */
                        0x30,
                        /* Length */
                        0x14,
                        /* Version */
                        0x01, 0x00,
                        /* Group Cipher Suite, default=TKIP */
                        0x00, 0x0f, 0xac, 0x02,
                        /* Pairwise Cipher Suite Count */
                        0x01, 0x00,
                        /* Pairwise Cipher Suite, default=TKIP */
                        0x00, 0x0f, 0xac, 0x02,
                        /* Authentication and Key Management Suite Count */
                        0x01, 0x00,
                        /* Authentication type, default=PSK */
                        0x00, 0x0f, 0xac, 0x02,
                        /* RSN capability */
                        0x00, 0x00
                    };

    zmw_get_wlan_dev(dev);

    if ( wd->sta.currentAuthMode == ZM_AUTH_MODE_WPAPSK )
    {
        /* Overwrite Group Cipher Suite by AP's setting */
        zfMemoryCopy(ssn+8, wd->sta.wpaIe+8, 4);

        if ( wd->sta.wepStatus == ZM_ENCRYPTION_AES )
        {
            /* Overwrite Pairwise Cipher Suite by AES */
            zfMemoryCopy(ssn+14, zgWpaAesOui, 4);
        }

        zfCopyToIntTxBuffer(dev, buf, ssn, offset, ssn[1]+2);
        zfMemoryCopy(wd->sta.wpaIe, ssn, ssn[1]+2);
        offset += (ssn[1]+2);
    }
    else if ( wd->sta.currentAuthMode == ZM_AUTH_MODE_WPA )
    {
        /* Overwrite Group Cipher Suite by AP's setting */
        zfMemoryCopy(ssn+8, wd->sta.wpaIe+8, 4);
        /* Overwrite Key Management Suite by WPA-Radius */
        zfMemoryCopy(ssn+20, zgWpaRadiusOui, 4);

        if ( wd->sta.wepStatus == ZM_ENCRYPTION_AES )
        {
            /* Overwrite Pairwise Cipher Suite by AES */
            zfMemoryCopy(ssn+14, zgWpaAesOui, 4);
        }

        zfCopyToIntTxBuffer(dev, buf, ssn, offset, ssn[1]+2);
        zfMemoryCopy(wd->sta.wpaIe, ssn, ssn[1]+2);
        offset += (ssn[1]+2);
    }
    else if ( wd->sta.currentAuthMode == ZM_AUTH_MODE_WPA2PSK )
    {
        /* Overwrite Group Cipher Suite by AP's setting */
        zfMemoryCopy(rsn+4, wd->sta.rsnIe+4, 4);

        if ( wd->sta.wepStatus == ZM_ENCRYPTION_AES )
        {
            /* Overwrite Pairwise Cipher Suite by AES */
            zfMemoryCopy(rsn+10, zgWpa2AesOui, 4);
        }

        if ( frameType == ZM_WLAN_FRAME_TYPE_REASOCREQ )
        {
            for(i=0; i<wd->sta.pmkidInfo.bssidCount; i++)
            {
                if ( zfMemoryIsEqual((u8_t*) wd->sta.pmkidInfo.bssidInfo[i].bssid,
                                     (u8_t*) wd->sta.bssid, 6) )
                {
                    /* matched */
                    break;
                }

                if ( i < wd->sta.pmkidInfo.bssidCount )
                {
                    // Fill PMKID Count in RSN information element
                    rsn[22] = 0x01;
                    rsn[23] = 0x00;

                    // Fill PMKID in RSN information element
                    zfMemoryCopy(rsn+24,
                                 wd->sta.pmkidInfo.bssidInfo[i].pmkid, 16);
			                 rsn[1] += 18;
                }
            }
        }

        zfCopyToIntTxBuffer(dev, buf, rsn, offset, rsn[1]+2);
        zfMemoryCopy(wd->sta.rsnIe, rsn, rsn[1]+2);
        offset += (rsn[1]+2);
    }
    else if ( wd->sta.currentAuthMode == ZM_AUTH_MODE_WPA2 )
    {
        /* Overwrite Group Cipher Suite by AP's setting */
        zfMemoryCopy(rsn+4, wd->sta.rsnIe+4, 4);
        /* Overwrite Key Management Suite by WPA2-Radius */
        zfMemoryCopy(rsn+16, zgWpa2RadiusOui, 4);

        if ( wd->sta.wepStatus == ZM_ENCRYPTION_AES )
        {
            /* Overwrite Pairwise Cipher Suite by AES */
            zfMemoryCopy(rsn+10, zgWpa2AesOui, 4);
        }

        if (( frameType == ZM_WLAN_FRAME_TYPE_REASOCREQ || ( frameType == ZM_WLAN_FRAME_TYPE_ASOCREQ )))
        {

            if (wd->sta.pmkidInfo.bssidCount != 0) {
                // Fill PMKID Count in RSN information element
                rsn[22] = 1;
                rsn[23] = 0;
                /*
                 *  The caller is respnsible to give us the relevant PMKID.
                 *  We'll only accept 1 PMKID for now.
                 */
                for(i=0; i<wd->sta.pmkidInfo.bssidCount; i++)
                {
                    if ( zfMemoryIsEqual((u8_t*) wd->sta.pmkidInfo.bssidInfo[i].bssid, (u8_t*) wd->sta.bssid, 6) )
                    {
                        zfMemoryCopy(rsn+24, wd->sta.pmkidInfo.bssidInfo[i].pmkid, 16);
                        break;
                    }
                }
                rsn[1] += 18;
            }

        }

        zfCopyToIntTxBuffer(dev, buf, rsn, offset, rsn[1]+2);
        zfMemoryCopy(wd->sta.rsnIe, rsn, rsn[1]+2);
        offset += (rsn[1]+2);
    }

    return offset;
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfStaAddIeIbss              */
/*      Add information element IBSS parameter to buffer.               */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : buffer to add information element                         */
/*      offset : add information element from this offset               */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      buffer offset after adding information element                  */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Ji-Huang Lee        ZyDAS Technology Corporation    2005.12     */
/*                                                                      */
/************************************************************************/
u16_t zfStaAddIeIbss(zdev_t* dev, zbuf_t* buf, u16_t offset)
{
    zmw_get_wlan_dev(dev);

    /* Element ID */
    zmw_tx_buf_writeb(dev, buf, offset++, ZM_WLAN_EID_IBSS);

    /* Element Length */
    zmw_tx_buf_writeb(dev, buf, offset++, 2);

    /* ATIM window */
    zmw_tx_buf_writeh(dev, buf, offset, wd->sta.atimWindow);
    offset += 2;

    return offset;
}



/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfStaAddIeWmeInfo           */
/*      Add WME Information Element to buffer.                          */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : buffer to add information element                         */
/*      offset : add information element from this offset               */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      buffer offset after adding information element                  */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2006.6      */
/*                                                                      */
/************************************************************************/
u16_t zfStaAddIeWmeInfo(zdev_t* dev, zbuf_t* buf, u16_t offset, u8_t qosInfo)
{
    /* Element ID */
    zmw_tx_buf_writeb(dev, buf, offset++, ZM_WLAN_EID_WIFI_IE);

    /* Element Length */
    zmw_tx_buf_writeb(dev, buf, offset++, 7);

    /* OUI */
    zmw_tx_buf_writeb(dev, buf, offset++, 0x00);
    zmw_tx_buf_writeb(dev, buf, offset++, 0x50);
    zmw_tx_buf_writeb(dev, buf, offset++, 0xF2);
    zmw_tx_buf_writeb(dev, buf, offset++, 0x02);
    zmw_tx_buf_writeb(dev, buf, offset++, 0x00);
    zmw_tx_buf_writeb(dev, buf, offset++, 0x01);

    /* QoS Info */
    zmw_tx_buf_writeb(dev, buf, offset++, qosInfo);

    return offset;
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfStaAddIePowerCap          */
/*      Add information element Power capability to buffer.             */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : buffer to add information element                         */
/*      offset : add information element from this offset               */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      buffer offset after adding information element                  */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Sharon                                            2007.12       */
/*                                                                      */
/************************************************************************/
u16_t zfStaAddIePowerCap(zdev_t* dev, zbuf_t* buf, u16_t offset)
{
    u8_t MaxTxPower;
    u8_t MinTxPower;

    zmw_get_wlan_dev(dev);

    /* Element ID */
    zmw_tx_buf_writeb(dev, buf, offset++, ZM_WLAN_EID_POWER_CAPABILITY);

    /* Element Length */
    zmw_tx_buf_writeb(dev, buf, offset++, 2);

    MinTxPower = (u8_t)(zfHpGetMinTxPower(dev)/2);
    MaxTxPower = (u8_t)(zfHpGetMaxTxPower(dev)/2);

    /* Min Transmit Power Cap */
    zmw_tx_buf_writeh(dev, buf, offset++, MinTxPower);

    /* Max Transmit Power Cap */
    zmw_tx_buf_writeh(dev, buf, offset++, MaxTxPower);

    return offset;
}
/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfStaAddIeSupportCh              */
/*      Add information element supported channels to buffer.               */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : buffer to add information element                         */
/*      offset : add information element from this offset               */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      buffer offset after adding information element                  */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Sharon            2007.12     */
/*                                                                      */
/************************************************************************/
u16_t zfStaAddIeSupportCh(zdev_t* dev, zbuf_t* buf, u16_t offset)
{

    u8_t   i;
    u16_t  count_24G = 0;
    u16_t  count_5G = 0;
    u16_t  channelNum;
    u8_t   length;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();
    zmw_enter_critical_section(dev);

    for (i = 0; i < wd->regulationTable.allowChannelCnt; i++)
    {
        if (wd->regulationTable.allowChannel[i].channel < 3000)
        { // 2.4Hz
            count_24G++;
        }
        else
        { // 5GHz
            count_5G++;
        }
    }

    length = (u8_t)(count_5G * 2 + 2); //5G fill by pair, 2,4G (continuous channels) fill 2 bytes

    /* Element ID */
    zmw_tx_buf_writeb(dev, buf, offset++, ZM_WLAN_EID_SUPPORTED_CHANNELS );

    /* Element Length */
    zmw_tx_buf_writeb(dev, buf, offset++, length);

    // 2.4GHz (continuous channels)
    /* First channel number */
    zmw_tx_buf_writeh(dev, buf, offset++, 1); //Start from channle 1
    /* Number of channels */
    zmw_tx_buf_writeh(dev, buf, offset++, count_24G);

    for (i = 0; i < wd->regulationTable.allowChannelCnt ; i++)
    {
        if (wd->regulationTable.allowChannel[i].channel > 4000 && wd->regulationTable.allowChannel[i].channel < 5000)
        { // 5GHz 4000 -5000Mhz
            channelNum = (wd->regulationTable.allowChannel[i].channel-4000)/5;
            /* First channel number */
            zmw_tx_buf_writeh(dev, buf, offset++, channelNum);
            /* Number of channels */
            zmw_tx_buf_writeh(dev, buf, offset++, 1);
        }
        else if (wd->regulationTable.allowChannel[i].channel >= 5000)
        { // 5GHz >5000Mhz
            channelNum = (wd->regulationTable.allowChannel[i].channel-5000)/5;
            /* First channel number */
            zmw_tx_buf_writeh(dev, buf, offset++, channelNum);
            /* Number of channels */
            zmw_tx_buf_writeh(dev, buf, offset++, 1);
        }
    }
   zmw_leave_critical_section(dev);

    return offset;
}

void zfStaStartConnectCb(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    zfStaStartConnect(dev, wd->sta.bIsSharedKey);
}

void zfStaStartConnect(zdev_t* dev, u8_t bIsSharedKey)
{
    u32_t p1, p2;
    u8_t newConnState;

    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    /* p1_low = algorithm number, p1_high = transaction sequence number */
    if ( bIsSharedKey )
    {
        //wd->sta.connectState = ZM_STA_CONN_STATE_AUTH_SHARE_1;
        newConnState = ZM_STA_CONN_STATE_AUTH_SHARE_1;
        zm_debug_msg0("ZM_STA_CONN_STATE_AUTH_SHARE_1");
        p1 = ZM_AUTH_ALGO_SHARED_KEY;
    }
    else
    {
        //wd->sta.connectState = ZM_STA_CONN_STATE_AUTH_OPEN;
        newConnState = ZM_STA_CONN_STATE_AUTH_OPEN;
        zm_debug_msg0("ZM_STA_CONN_STATE_AUTH_OPEN");
        if( wd->sta.leapEnabled )
            p1 = ZM_AUTH_ALGO_LEAP;
        else
            p1 = ZM_AUTH_ALGO_OPEN_SYSTEM;
    }

    /* status code */
    p2 = 0x0;

    zmw_enter_critical_section(dev);
    wd->sta.connectTimer = wd->tick;
    wd->sta.connectState = newConnState;
    zmw_leave_critical_section(dev);

    /* send the 1st authentication frame */
    zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_AUTH, wd->sta.bssid, p1, p2, 0);

    return;
}

void zfSendNullData(zdev_t* dev, u8_t type)
{
    zbuf_t* buf;
    //u16_t addrTblSize;
    //struct zsAddrTbl addrTbl;
    u16_t err;
    u16_t hlen;
    u16_t header[(34+8+1)/2];
    u16_t bcastAddr[3] = {0xffff,0xffff,0xffff};
    u16_t *dstAddr;

    zmw_get_wlan_dev(dev);

    if ((buf = zfwBufAllocate(dev, 1024)) == NULL)
    {
        zm_msg0_mm(ZM_LV_0, "Alloc mm buf Fail!");
        return;
    }

    zfwBufSetSize(dev, buf, 0);

    //zm_msg2_mm(ZM_LV_2, "buf->len=", buf->len);

    if ( wd->wlanMode == ZM_MODE_IBSS)
    {
        dstAddr = bcastAddr;
    }
    else
    {
        dstAddr = wd->sta.bssid;
    }

    if (wd->sta.wmeConnected != 0)
    {
        /* If connect to a WMM AP, Send QoS Null data */
        hlen = zfTxGenMmHeader(dev, ZM_WLAN_FRAME_TYPE_QOS_NULL, dstAddr, header, 0, buf, 0, 0);
    }
    else
    {
        hlen = zfTxGenMmHeader(dev, ZM_WLAN_FRAME_TYPE_NULL, dstAddr, header, 0, buf, 0, 0);
    }

    if (wd->wlanMode == ZM_MODE_INFRASTRUCTURE)
    {
        header[4] |= 0x0100; //TODS bit
    }

    if ( type == 1 )
    {
        header[4] |= 0x1000;
    }

    /* Get buffer DMA address */
    //if ((addrTblSize = zfwBufMapDma(dev, buf, &addrTbl)) == 0)
    //if ((addrTblSize = zfwMapTxDma(dev, buf, &addrTbl)) == 0)
    //{
    //    goto zlError;
    //}

    /*increase unicast frame counter*/
    wd->commTally.txUnicastFrm++;

    if ((err = zfHpSend(dev, header, hlen, NULL, 0, NULL, 0, buf, 0,
            ZM_INTERNAL_ALLOC_BUF, 0, 0xff)) != ZM_SUCCESS)
    {
        goto zlError;
    }


    return;

zlError:

    zfwBufFree(dev, buf, 0);
    return;

}

void zfSendPSPoll(zdev_t* dev)
{
    zbuf_t* buf;
    //u16_t addrTblSize;
    //struct zsAddrTbl addrTbl;
    u16_t err;
    u16_t hlen;
    u16_t header[(8+24+1)/2];

    zmw_get_wlan_dev(dev);

    if ((buf = zfwBufAllocate(dev, 1024)) == NULL)
    {
        zm_msg0_mm(ZM_LV_0, "Alloc mm buf Fail!");
        return;
    }

    zfwBufSetSize(dev, buf, 0);

    //zm_msg2_mm(ZM_LV_2, "buf->len=", buf->len);

    zfTxGenMmHeader(dev, ZM_WLAN_FRAME_TYPE_PSPOLL, wd->sta.bssid, header, 0, buf, 0, 0);

    header[0] = 20;
    header[4] |= 0x1000;
    header[5] = wd->sta.aid | 0xc000; //Both bit-14 and bit-15 are 1
    hlen = 16 + 8;

    /* Get buffer DMA address */
    //if ((addrTblSize = zfwBufMapDma(dev, buf, &addrTbl)) == 0)
    //if ((addrTblSize = zfwMapTxDma(dev, buf, &addrTbl)) == 0)
    //{
    //    goto zlError;
    //}

    if ((err = zfHpSend(dev, header, hlen, NULL, 0, NULL, 0, buf, 0,
            ZM_INTERNAL_ALLOC_BUF, 0, 0xff)) != ZM_SUCCESS)
    {
        goto zlError;
    }

    return;

zlError:

    zfwBufFree(dev, buf, 0);
    return;

}

void zfSendBA(zdev_t* dev, u16_t start_seq, u8_t *bitmap)
{
    zbuf_t* buf;
    //u16_t addrTblSize;
    //struct zsAddrTbl addrTbl;
    u16_t err;
    u16_t hlen;
    u16_t header[(8+24+1)/2];
    u16_t i, offset = 0;

    zmw_get_wlan_dev(dev);

    if ((buf = zfwBufAllocate(dev, 1024)) == NULL)
    {
        zm_msg0_mm(ZM_LV_0, "Alloc mm buf Fail!");
        return;
    }

    zfwBufSetSize(dev, buf, 12); // 28 = FC 2 + DU 2 + RA 6 + TA 6 + BAC 2 + SEQ 2 + BitMap 8
                                 // 12 = BAC 2 + SEQ 2 + BitMap 8

    //zm_msg2_mm(ZM_LV_2, "buf->len=", buf->len);

    zfTxGenMmHeader(dev, ZM_WLAN_FRAME_TYPE_BA, wd->sta.bssid, header, 0, buf, 0, 0);

    header[0] = 32; /* MAC header 16 + BA control 2 + BA info 10 + FCS 4*/
    header[1] = 0x4;  /* No ACK */

    /* send by OFDM 6M */
    header[2] = (u16_t)(zcRateToPhyCtrl[4] & 0xffff);
    header[3] = (u16_t)(zcRateToPhyCtrl[4]>>16) & 0xffff;

    hlen = 16 + 8;  /* MAC header 16 + control 8*/
    offset = 0;
    zmw_tx_buf_writeh(dev, buf, offset, 0x05); /*compressed bitmap on*/
    offset+=2;
    zmw_tx_buf_writeh(dev, buf, offset, start_seq);
    offset+=2;

    for (i=0; i<8; i++) {
        zmw_tx_buf_writeb(dev, buf, offset, bitmap[i]);
        offset++;
    }

    if ((err = zfHpSend(dev, header, hlen, NULL, 0, NULL, 0, buf, 0,
            ZM_INTERNAL_ALLOC_BUF, 0, 0xff)) != ZM_SUCCESS)
    {
        goto zlError;
    }

    return;

zlError:

    zfwBufFree(dev, buf, 0);
    return;

}

void zfStaGetTxRate(zdev_t* dev, u16_t* macAddr, u32_t* phyCtrl,
        u16_t* rcProbingFlag)
{
    u8_t   addr[6], i;
    u8_t   rate;
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    ZM_MAC_WORD_TO_BYTE(macAddr, addr);
    *phyCtrl = 0;

    if ( wd->wlanMode == ZM_MODE_INFRASTRUCTURE )
    {
        zmw_enter_critical_section(dev);
        rate = (u8_t)zfRateCtrlGetTxRate(dev, &wd->sta.oppositeInfo[0].rcCell, rcProbingFlag);
//#ifdef ZM_FB50
        //rate = 27;
//#endif
        *phyCtrl = zcRateToPhyCtrl[rate];
        zmw_leave_critical_section(dev);
    }
    else
    {
        zmw_enter_critical_section(dev);
        for(i=0; i<wd->sta.oppositeCount; i++)
        {
            if ( addr[0] && 0x01 == 1 ) // The default beacon transmitted rate is CCK and 1 Mbps , but the a mode should use
                                        // OFDM modulation and 6Mbps to transmit beacon.
            {
                //rate = (u8_t)zfRateCtrlGetTxRate(dev, &wd->sta.oppositeInfo[i].rcCell, rcProbingFlag);
                rate = wd->sta.oppositeInfo[i].rcCell.operationRateSet[0];
                *phyCtrl = zcRateToPhyCtrl[rate];
                break;
            }
            else if ( zfMemoryIsEqual(addr, wd->sta.oppositeInfo[i].macAddr, 6) )
            {
                rate = (u8_t)zfRateCtrlGetTxRate(dev, &wd->sta.oppositeInfo[i].rcCell, rcProbingFlag);
                *phyCtrl = zcRateToPhyCtrl[rate];
                break;
            }
        }
        zmw_leave_critical_section(dev);
    }

    return;
}

struct zsMicVar* zfStaGetRxMicKey(zdev_t* dev, zbuf_t* buf)
{
    u8_t keyIndex;
    u8_t da0;

    zmw_get_wlan_dev(dev);

    /* if need not check MIC, return NULL */
    if ( ((wd->sta.encryMode != ZM_TKIP)&&(wd->sta.encryMode != ZM_AES))||
         (wd->sta.wpaState < ZM_STA_WPA_STATE_PK_OK) )
    {
        return NULL;
    }

    da0 = zmw_rx_buf_readb(dev, buf, ZM_WLAN_HEADER_A1_OFFSET);

    if ((zmw_rx_buf_readb(dev, buf, 0) & 0x80) == 0x80)
        keyIndex = zmw_rx_buf_readb(dev, buf, ZM_WLAN_HEADER_IV_OFFSET+5); /* Qos Packet*/
    else
        keyIndex = zmw_rx_buf_readb(dev, buf, ZM_WLAN_HEADER_IV_OFFSET+3); /* normal Packet*/
    keyIndex = (keyIndex & 0xc0) >> 6;

    return (&wd->sta.rxMicKey[keyIndex]);
}

struct zsMicVar* zfStaGetTxMicKey(zdev_t* dev, zbuf_t* buf)
{
    zmw_get_wlan_dev(dev);

    /* if need not check MIC, return NULL */
    //if ( ((wd->sta.encryMode != ZM_TKIP)&&(wd->sta.encryMode != ZM_AES))||
    //     (wd->sta.wpaState < ZM_STA_WPA_STATE_PK_OK) )
    if ( (wd->sta.encryMode != ZM_TKIP) || (wd->sta.wpaState < ZM_STA_WPA_STATE_PK_OK) )
    {
        return NULL;
    }

    return (&wd->sta.txMicKey);
}

u16_t zfStaRxValidateFrame(zdev_t* dev, zbuf_t* buf)
{
    u8_t   frameType, frameCtrl;
    u8_t   da0;
    //u16_t  sa[3];
    u16_t  ret;
    u16_t  i;
    //u8_t    sa0;

    zmw_get_wlan_dev(dev);

    frameType = zmw_rx_buf_readb(dev, buf, 0);
    da0 = zmw_rx_buf_readb(dev, buf, ZM_WLAN_HEADER_A1_OFFSET);
    //sa0 = zmw_rx_buf_readb(dev, buf, ZM_WLAN_HEADER_A2_OFFSET);

    if ( (!zfStaIsConnected(dev))&&((frameType & 0xf) == ZM_WLAN_DATA_FRAME) )
    {
        return ZM_ERR_DATA_BEFORE_CONNECTED;
    }


    if ( (zfStaIsConnected(dev))&&((frameType & 0xf) == ZM_WLAN_DATA_FRAME) )
    {
        /* check BSSID */
        if ( wd->wlanMode == ZM_MODE_INFRASTRUCTURE )
        {
            /* Big Endian and Little Endian Compatibility */
            u16_t mac[3];
            mac[0] = zmw_cpu_to_le16(wd->sta.bssid[0]);
            mac[1] = zmw_cpu_to_le16(wd->sta.bssid[1]);
            mac[2] = zmw_cpu_to_le16(wd->sta.bssid[2]);
            if ( !zfRxBufferEqualToStr(dev, buf, (u8_t *)mac,
                                       ZM_WLAN_HEADER_A2_OFFSET, 6) )
            {
/*We will get lots of garbage data, especially in AES mode.*/
/*To avoid sending too many deauthentication frames in STA mode, mark it.*/
#if 0
                /* If unicast frame, send deauth to the transmitter */
                if (( da0 & 0x01 ) == 0)
                {
                    for (i=0; i<3; i++)
                    {
                        sa[i] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A2_OFFSET+(i*2));
                    }
					/* If mutilcast address, don't send deauthentication*/
					if (( sa0 & 0x01 ) == 0)
	                  	zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_DEAUTH, sa, 7, 0, 0);
                }
#endif
                return ZM_ERR_DATA_BSSID_NOT_MATCHED;
            }
        }
        else if ( wd->wlanMode == ZM_MODE_IBSS )
        {
            /* Big Endian and Little Endian Compatibility */
            u16_t mac[3];
            mac[0] = zmw_cpu_to_le16(wd->sta.bssid[0]);
            mac[1] = zmw_cpu_to_le16(wd->sta.bssid[1]);
            mac[2] = zmw_cpu_to_le16(wd->sta.bssid[2]);
            if ( !zfRxBufferEqualToStr(dev, buf, (u8_t *)mac,
                                       ZM_WLAN_HEADER_A3_OFFSET, 6) )
            {
                return ZM_ERR_DATA_BSSID_NOT_MATCHED;
            }
        }

        frameCtrl = zmw_rx_buf_readb(dev, buf, 1);

        /* check security bit */
        if ( wd->sta.dropUnencryptedPkts &&
             (wd->sta.wepStatus != ZM_ENCRYPTION_WEP_DISABLED )&&
             ( !(frameCtrl & ZM_BIT_6) ) )
        {   /* security on, but got data without encryption */

            #if 1
            ret = ZM_ERR_DATA_NOT_ENCRYPTED;
            if ( wd->sta.pStaRxSecurityCheckCb != NULL )
            {
                ret = wd->sta.pStaRxSecurityCheckCb(dev, buf);
            }
            else
            {
                ret = ZM_ERR_DATA_NOT_ENCRYPTED;
            }
            if (ret == ZM_ERR_DATA_NOT_ENCRYPTED)
            {
                wd->commTally.swRxDropUnencryptedCount++;
            }
            return ret;
            #else
            if ( (wd->sta.wepStatus != ZM_ENCRYPTION_TKIP)&&
                 (wd->sta.wepStatus != ZM_ENCRYPTION_AES) )
            {
                return ZM_ERR_DATA_NOT_ENCRYPTED;
            }
            #endif
        }
    }

    return ZM_SUCCESS;
}

void zfStaMicFailureHandling(zdev_t* dev, zbuf_t* buf)
{
    u8_t   da0;
    u8_t   micNotify = 1;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    if ( wd->sta.wpaState <  ZM_STA_WPA_STATE_PK_OK )
    {
        return;
    }

    zmw_enter_critical_section(dev);

    wd->sta.cmMicFailureCount++;

    if ( wd->sta.cmMicFailureCount == 1 )
    {
        zm_debug_msg0("get the first MIC failure");
        //zfTimerSchedule(dev, ZM_EVENT_CM_TIMER, ZM_TICK_CM_TIMEOUT);

        /* Timer Resolution on WinXP is 15/16 ms  */
        /* Decrease Time offset for <XP> Counter Measure */
        zfTimerSchedule(dev, ZM_EVENT_CM_TIMER, ZM_TICK_CM_TIMEOUT - ZM_TICK_CM_TIMEOUT_OFFSET);
    }
    else if ( wd->sta.cmMicFailureCount == 2 )
    {
        zm_debug_msg0("get the second MIC failure");
        /* reserve 2 second for OS to send MIC failure report to AP */
        wd->sta.cmDisallowSsidLength = wd->sta.ssidLen;
        zfMemoryCopy(wd->sta.cmDisallowSsid, wd->sta.ssid, wd->sta.ssidLen);
        //wd->sta.cmMicFailureCount = 0;
        zfTimerCancel(dev, ZM_EVENT_CM_TIMER);
        //zfTimerSchedule(dev, ZM_EVENT_CM_DISCONNECT, ZM_TICK_CM_DISCONNECT);

        /* Timer Resolution on WinXP is 15/16 ms  */
        /* Decrease Time offset for <XP> Counter Measure */
        zfTimerSchedule(dev, ZM_EVENT_CM_DISCONNECT, ZM_TICK_CM_DISCONNECT - ZM_TICK_CM_DISCONNECT_OFFSET);
    }
    else
    {
        micNotify = 0;
    }

    zmw_leave_critical_section(dev);

    if (micNotify == 1)
    {
        da0 = zmw_rx_buf_readb(dev, buf, ZM_WLAN_HEADER_A1_OFFSET);
        if ( da0 & 0x01 )
        {
            if (wd->zfcbMicFailureNotify != NULL)
            {
                wd->zfcbMicFailureNotify(dev, wd->sta.bssid, ZM_MIC_GROUP_ERROR);
            }
        }
        else
        {
            if (wd->zfcbMicFailureNotify != NULL)
            {
                wd->zfcbMicFailureNotify(dev, wd->sta.bssid, ZM_MIC_PAIRWISE_ERROR);
            }
        }
    }
}


u8_t zfStaBlockWlanScan(zdev_t* dev)
{
    u8_t   ret=FALSE;

    zmw_get_wlan_dev(dev);

    if ( wd->sta.bChannelScan )
    {
        return TRUE;
    }

    return ret;
}

void zfStaResetStatus(zdev_t* dev, u8_t bInit)
{
    u8_t   i;

    zmw_get_wlan_dev(dev);

    zfHpDisableBeacon(dev);

    wd->dtim = 1;
    wd->sta.capability[0] = 0x01;
    wd->sta.capability[1] = 0x00;
    /* 802.11h */
    if (wd->sta.DFSEnable || wd->sta.TPCEnable)
        wd->sta.capability[1] |= ZM_BIT_0;

    /* release queued packets */
    for(i=0; i<wd->sta.ibssPSDataCount; i++)
    {
        zfwBufFree(dev, wd->sta.ibssPSDataQueue[i], 0);
    }

    for(i=0; i<wd->sta.staPSDataCount; i++)
    {
        zfwBufFree(dev, wd->sta.staPSDataQueue[i], 0);
    }

    wd->sta.ibssPSDataCount = 0;
    wd->sta.staPSDataCount = 0;
    zfZeroMemory((u8_t*) &wd->sta.staPSList, sizeof(struct zsStaPSList));

    wd->sta.wmeConnected = 0;
    wd->sta.psMgr.tempWakeUp = 0;
    wd->sta.qosInfo = 0;
    zfQueueFlush(dev, wd->sta.uapsdQ);

    return;

}

void zfStaIbssMonitoring(zdev_t* dev, u8_t reset)
{
    u16_t i;
    u16_t oppositeCount;
    struct zsPartnerNotifyEvent event;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    //zm_debug_msg1("zfStaIbssMonitoring %d", wd->sta.oppositeCount);

    zmw_enter_critical_section(dev);

    if ( wd->sta.oppositeCount == 0 )
    {
        goto done;
    }

    if ( wd->sta.bChannelScan )
    {
        goto done;
    }

    oppositeCount = wd->sta.oppositeCount;

    for(i=0; i < ZM_MAX_OPPOSITE_COUNT; i++)
    {
        if ( oppositeCount == 0 )
        {
            break;
        }

        if ( reset )
        {
            wd->sta.oppositeInfo[i].valid = 0;
        }

        if ( wd->sta.oppositeInfo[i].valid == 0 )
        {
            continue;
        }

        oppositeCount--;

        if ( wd->sta.oppositeInfo[i].aliveCounter )
        {
            zm_debug_msg1("Setting alive to ", wd->sta.oppositeInfo[i].aliveCounter);

            zmw_leave_critical_section(dev);

            if ( wd->sta.oppositeInfo[i].aliveCounter != ZM_IBSS_PEER_ALIVE_COUNTER )
            {
                zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_PROBEREQ,
                              (u16_t*)wd->sta.oppositeInfo[i].macAddr, 1, 0, 0);
            }

            zmw_enter_critical_section(dev);
            wd->sta.oppositeInfo[i].aliveCounter--;
        }
        else
        {
            zm_debug_msg0("zfStaIbssMonitoring remove the peer station");
            zfMemoryCopy(event.bssid, (u8_t *)(wd->sta.bssid), 6);
            zfMemoryCopy(event.peerMacAddr, wd->sta.oppositeInfo[i].macAddr, 6);

            wd->sta.oppositeInfo[i].valid = 0;
            wd->sta.oppositeCount--;
            if (wd->zfcbIbssPartnerNotify != NULL)
            {
                zmw_leave_critical_section(dev);
                wd->zfcbIbssPartnerNotify(dev, 0, &event);
                zmw_enter_critical_section(dev);
            }
        }
    }

done:
    if ( reset == 0 )
    {
        zfTimerSchedule(dev, ZM_EVENT_IBSS_MONITOR, ZM_TICK_IBSS_MONITOR);
    }

    zmw_leave_critical_section(dev);
}

void zfInitPartnerNotifyEvent(zdev_t* dev, zbuf_t* buf, struct zsPartnerNotifyEvent *event)
{
    u16_t  *peerMacAddr;

    zmw_get_wlan_dev(dev);

    peerMacAddr = (u16_t *)event->peerMacAddr;

    zfMemoryCopy(event->bssid, (u8_t *)(wd->sta.bssid), 6);
    peerMacAddr[0] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A2_OFFSET);
    peerMacAddr[1] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A2_OFFSET + 2);
    peerMacAddr[2] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A2_OFFSET + 4);
}

void zfStaInitOppositeInfo(zdev_t* dev)
{
    int i;

    zmw_get_wlan_dev(dev);

    for(i=0; i<ZM_MAX_OPPOSITE_COUNT; i++)
    {
        wd->sta.oppositeInfo[i].valid = 0;
        wd->sta.oppositeInfo[i].aliveCounter = ZM_IBSS_PEER_ALIVE_COUNTER;
    }
}
#ifdef ZM_ENABLE_CENC
u16_t zfStaAddIeCenc(zdev_t* dev, zbuf_t* buf, u16_t offset)
{
    zmw_get_wlan_dev(dev);

    if (wd->sta.cencIe[1] != 0)
    {
        zfCopyToIntTxBuffer(dev, buf, wd->sta.cencIe, offset, wd->sta.cencIe[1]+2);
        offset += (wd->sta.cencIe[1]+2);
    }
    return offset;
}
#endif //ZM_ENABLE_CENC
u16_t zfStaProcessAction(zdev_t* dev, zbuf_t* buf)
{
    u8_t category, actionDetails;
    zmw_get_wlan_dev(dev);

    category = zmw_rx_buf_readb(dev, buf, 24);
    actionDetails = zmw_rx_buf_readb(dev, buf, 25);
    switch (category)
    {
        case 0:		//Spectrum Management
	        switch(actionDetails)
	        {
	        	case 0:			//Measurement Request
	        		break;
	        	case 1:			//Measurement Report
	        		//ProcessActionSpectrumFrame_MeasurementReport(Adapter,pActionBody+3);
	        		break;
	        	case 2:			//TPC request
                    //if (wd->sta.TPCEnable)
                    //    zfStaUpdateDot11HTPC(dev, buf);
	        		break;
	        	case 3:			//TPC report
                    //if (wd->sta.TPCEnable)
                    //    zfStaUpdateDot11HTPC(dev, buf);
	        		break;
	        	case 4:			//Channel Switch Announcement
                    if (wd->sta.DFSEnable)
                        zfStaUpdateDot11HDFS(dev, buf);
	        		break;
	        	default:
	        		zm_debug_msg1("Action Frame contain not support action field ", actionDetails);
	        		break;
	        }
        	break;
        case ZM_WLAN_BLOCK_ACK_ACTION_FRAME:
            zfAggBlockAckActionFrame(dev, buf);
            break;
        case 17:	//Qos Management
        	break;
    }

    return 0;
}

/* Determine the time not send beacon , if more than some value ,
   re-write the beacon start address */
void zfReWriteBeaconStartAddress(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    wd->tickIbssSendBeacon++;    // Increase 1 per 10ms .
    zmw_leave_critical_section(dev);

    if ( wd->tickIbssSendBeacon == 40 )
    {
//        DbgPrint("20070727");
        zfHpEnableBeacon(dev, ZM_MODE_IBSS, wd->beaconInterval, wd->dtim, (u8_t)wd->sta.atimWindow);
        zmw_enter_critical_section(dev);
        wd->tickIbssSendBeacon = 0;
        zmw_leave_critical_section(dev);
    }
}

struct zsTkipSeed* zfStaGetRxSeed(zdev_t* dev, zbuf_t* buf)
{
    u8_t keyIndex;
    u8_t da0;

    zmw_get_wlan_dev(dev);

    /* if need not check MIC, return NULL */
    if ( ((wd->sta.encryMode != ZM_TKIP)&&(wd->sta.encryMode != ZM_AES))||
         (wd->sta.wpaState < ZM_STA_WPA_STATE_PK_OK) )
    {
        return NULL;
    }

    da0 = zmw_rx_buf_readb(dev, buf, ZM_WLAN_HEADER_A1_OFFSET);

    if ((zmw_rx_buf_readb(dev, buf, 0) & 0x80) == 0x80)
        keyIndex = zmw_rx_buf_readb(dev, buf, ZM_WLAN_HEADER_IV_OFFSET+5); /* Qos Packet*/
    else
        keyIndex = zmw_rx_buf_readb(dev, buf, ZM_WLAN_HEADER_IV_OFFSET+3); /* normal Packet*/
    keyIndex = (keyIndex & 0xc0) >> 6;

    return (&wd->sta.rxSeed[keyIndex]);
}

void zfStaEnableSWEncryption(zdev_t *dev, u8_t value)
{
    zmw_get_wlan_dev(dev);

    wd->sta.SWEncryptEnable = value;
    zfHpSWDecrypt(dev, 1);
    zfHpSWEncrypt(dev, 1);
}

void zfStaDisableSWEncryption(zdev_t *dev)
{
    zmw_get_wlan_dev(dev);

    wd->sta.SWEncryptEnable = 0;
    zfHpSWDecrypt(dev, 0);
    zfHpSWEncrypt(dev, 0);
}

u16_t zfComputeBssInfoWeightValue(zdev_t *dev, u8_t isBMode, u8_t isHT, u8_t isHT40, u8_t signalStrength)
{
	u8_t  weightOfB           = 0;
	u8_t  weightOfAGBelowThr  = 0;
	u8_t  weightOfAGUpThr     = 15;
	u8_t  weightOfN20BelowThr = 15;
	u8_t  weightOfN20UpThr    = 30;
	u8_t  weightOfN40BelowThr = 16;
	u8_t  weightOfN40UpThr    = 32;

    zmw_get_wlan_dev(dev);

    if( isBMode == 0 )
        return (signalStrength + weightOfB);    // pure b mode , do not add the weight value for this AP !
    else
    {
        if( isHT == 0 && isHT40 == 0 )
        { // a , g , b/g mode ! add the weight value 15 for this AP if it's signal strength is more than some value !
            if( signalStrength < 18 ) // -77 dBm
				return signalStrength + weightOfAGBelowThr;
			else
				return (signalStrength + weightOfAGUpThr);
        }
        else if( isHT == 1 && isHT40 == 0 )
        { // 80211n mode use 20MHz
            if( signalStrength < 23 ) // -72 dBm
                return (signalStrength + weightOfN20BelowThr);
            else
                return (signalStrength + weightOfN20UpThr);
        }
        else // isHT == 1 && isHT40 == 1
        { // 80211n mode use 40MHz
            if( signalStrength < 16 ) // -79 dBm
                return (signalStrength + weightOfN40BelowThr);
            else
                return (signalStrength + weightOfN40UpThr);
        }
    }
}

u16_t zfStaAddIbssAdditionalIE(zdev_t* dev, zbuf_t* buf, u16_t offset)
{
	u16_t i;

    zmw_get_wlan_dev(dev);

    for (i=0; i<wd->sta.ibssAdditionalIESize; i++)
    {
        zmw_tx_buf_writeb(dev, buf, offset++, wd->sta.ibssAdditionalIE[i]);
    }

    return offset;
}
