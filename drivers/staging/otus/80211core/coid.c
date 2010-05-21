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
/*  Module Name : iod.c                                                 */
/*                                                                      */
/*  Abstract                                                            */
/*      This module contains OID functions.                             */
/*                                                                      */
/*  NOTES                                                               */
/*      None                                                            */
/*                                                                      */
/************************************************************************/
#include "cprecomp.h"
#include "../hal/hpreg.h"

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfiWlanQueryMacAddress      */
/*      Query OWN MAC address.                                          */
/*                                                                      */
/*    INPUTS                                                            */
/*      addr : for return MAC address                                   */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      None                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2005.10     */
/*                                                                      */
/************************************************************************/
void zfiWlanQueryMacAddress(zdev_t* dev, u8_t* addr)
{
    u16_t vapId = 0;
    zmw_get_wlan_dev(dev);

    vapId = zfwGetVapId(dev);

    addr[0] = (u8_t)(wd->macAddr[0] & 0xff);
    addr[1] = (u8_t)(wd->macAddr[0] >> 8);
    addr[2] = (u8_t)(wd->macAddr[1] & 0xff);
    addr[3] = (u8_t)(wd->macAddr[1] >> 8);
    addr[4] = (u8_t)(wd->macAddr[2] & 0xff);
    if (vapId == 0xffff)
        addr[5] = (u8_t)(wd->macAddr[2] >> 8);
    else
    {
#ifdef ZM_VAPMODE_MULTILE_SSID
        addr[5] = (u8_t)(wd->macAddr[2] >> 8); // Multiple SSID
#else
        addr[5] = vapId + 1 + (u8_t)(wd->macAddr[2] >> 8); //VAP
#endif
    }

    return;
}

void zfiWlanQueryBssList(zdev_t* dev, struct zsBssList* pBssList)
{
    struct zsBssInfo*   pBssInfo;
    struct zsBssInfo*   pDstBssInfo;
    u8_t   i;
    u8_t*  pMemList;
    u8_t*  pMemInfo;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    pMemList = (u8_t*) pBssList;
    pMemInfo = pMemList + sizeof(struct zsBssList);
    pBssList->head = (struct zsBssInfo*) pMemInfo;

    zmw_enter_critical_section(dev);

    pBssInfo = wd->sta.bssList.head;
    pDstBssInfo = (struct zsBssInfo*) pMemInfo;
    pBssList->bssCount = wd->sta.bssList.bssCount;

    for( i=0; i<wd->sta.bssList.bssCount; i++ )
    {
        zfMemoryCopy((u8_t*)pDstBssInfo, (u8_t*)pBssInfo,
                sizeof(struct zsBssInfo));

        if ( pBssInfo->next != NULL )
        {
            pBssInfo = pBssInfo->next;
            pDstBssInfo->next = pDstBssInfo + 1;
            pDstBssInfo++;
        }
        else
        {
            zm_assert(i==(wd->sta.bssList.bssCount-1));
        }
    }

    zmw_leave_critical_section(dev);

    zfScanMgrScanAck(dev);
}

void zfiWlanQueryBssListV1(zdev_t* dev, struct zsBssListV1* bssListV1)
{
    struct zsBssInfo*   pBssInfo;
    //struct zsBssInfo*   pDstBssInfo;
    u8_t   i, j, bdrop = 0, k = 0, Same_Count = 0;
    u8_t   bssid[6];
    //u8_t*  pMemList;
    //u8_t*  pMemInfo;
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    bssListV1->bssCount = wd->sta.bssList.bssCount;

    pBssInfo = wd->sta.bssList.head;
    ZM_MAC_WORD_TO_BYTE(wd->sta.bssid, bssid);

    for( i=0; i<wd->sta.bssList.bssCount; i++ )
    {
        bdrop = 0;
        if ( zfStaIsConnected(dev)
             && (wd->wlanMode == ZM_MODE_INFRASTRUCTURE ) )
        {
			for (j = 0; j < 6; j++)
            {
                if ( pBssInfo->bssid[j] != bssid[j] )
                {
                    break;
                }
            }

            if  ( (j == 6)
                  &&((pBssInfo->ssid[1] == wd->sta.ssidLen) || (pBssInfo->ssid[1] == 0) )&& (pBssInfo->frequency == wd->frequency) )
            {
				if(pBssInfo->ssid[1] == 0)
					pBssInfo->ssid[1] = wd->sta.ssidLen;

				if(Same_Count == 0)
				{//First meet
					Same_Count++;
				}
				else
				{//same one
					bdrop = 1;
					bssListV1->bssCount--;
				}

            }
        }

        if (bdrop == 0)
        {
            zfMemoryCopy((u8_t*)(&bssListV1->bssInfo[k]), (u8_t*)pBssInfo,
                sizeof(struct zsBssInfo));

			if(Same_Count == 1)
			{
				zfMemoryCopy(&(bssListV1->bssInfo[k].ssid[2]), wd->sta.ssid, wd->sta.ssidLen);
				Same_Count++;
			}

			k++;
        }

        if ( pBssInfo->next != NULL )
        {
            pBssInfo = pBssInfo->next;
        }
        else
        {
            zm_assert(i==(wd->sta.bssList.bssCount-1));
        }
    }

    zmw_leave_critical_section(dev);

    zfScanMgrScanAck(dev);
}

void zfiWlanQueryAdHocCreatedBssDesc(zdev_t* dev, struct zsBssInfo *pBssInfo)
{
    zmw_get_wlan_dev(dev);

    zfMemoryCopy((u8_t *)pBssInfo, (u8_t *)&wd->sta.ibssBssDesc, sizeof(struct zsBssInfo));
}

u8_t zfiWlanQueryAdHocIsCreator(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    return wd->sta.ibssBssIsCreator;
}

u32_t zfiWlanQuerySupportMode(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    return wd->supportMode;
}

u32_t zfiWlanQueryTransmitPower(zdev_t* dev)
{
    u32_t ret = 0;

    zmw_get_wlan_dev(dev);

    if (zfStaIsConnected(dev)) {
        ret = wd->sta.connPowerInHalfDbm;
    } else {
        ret = zfHpGetTransmitPower(dev);
    }

    return ret;
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfiWlanFlushBssList         */
/*      Flush BSSID List.                                               */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      none                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2006.12     */
/*                                                                      */
/************************************************************************/
void zfiWlanFlushBssList(zdev_t* dev)
{
    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    /* Call zfBssInfoRefresh() twice to remove all entry */
    zfBssInfoRefresh(dev, 1);
    zmw_leave_critical_section(dev);
}

void zfiWlanSetWlanMode(zdev_t* dev, u8_t wlanMode)
{
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    wd->ws.wlanMode = wlanMode;
    zmw_leave_critical_section(dev);
}

void zfiWlanSetAuthenticationMode(zdev_t* dev, u8_t authMode)
{
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    wd->ws.authMode = authMode;
    zmw_leave_critical_section(dev);
}

void zfiWlanSetWepStatus(zdev_t* dev, u8_t wepStatus)
{
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    wd->ws.wepStatus = wepStatus;
    zmw_leave_critical_section(dev);

}

void zfiWlanSetSSID(zdev_t* dev, u8_t* ssid, u8_t ssidLength)
{
    u16_t i;
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    if ( ssidLength <= 32 )
    {
        zmw_enter_critical_section(dev);

        wd->ws.ssidLen = ssidLength;
        zfMemoryCopy(wd->ws.ssid, ssid, ssidLength);

        if ( ssidLength < 32 )
        {
            wd->ws.ssid[ssidLength] = 0;
        }

        wd->ws.probingSsidList[0].ssidLen = ssidLength;
        zfMemoryCopy(wd->ws.probingSsidList[0].ssid, ssid, ssidLength);
        for (i=1; i<ZM_MAX_PROBE_HIDDEN_SSID_SIZE; i++)
        {
            wd->ws.probingSsidList[i].ssidLen = 0;
        }

        zmw_leave_critical_section(dev);
    }
}

void zfiWlanSetFragThreshold(zdev_t* dev, u16_t fragThreshold)
{
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    if (fragThreshold == 0)
    {   /* fragmentation is disabled */
        wd->fragThreshold = 32767;
    }
    else if (fragThreshold < 256)
    {
        /* Minimum fragment threshold */
        wd->fragThreshold = 256;
    }
    else if (fragThreshold > 2346)
    {
        wd->fragThreshold = 2346;
    }
    else
    {
        wd->fragThreshold = fragThreshold & 0xfffe;
    }

    zmw_leave_critical_section(dev);
}

void zfiWlanSetRtsThreshold(zdev_t* dev, u16_t rtsThreshold)
{
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    wd->rtsThreshold = rtsThreshold;
    zmw_leave_critical_section(dev);
}

void zfiWlanSetFrequency(zdev_t* dev, u32_t frequency, u8_t bImmediate)
{
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    if ( bImmediate )
    {
        zmw_enter_critical_section(dev);
        wd->frequency = (u16_t) (frequency/1000);
        zmw_leave_critical_section(dev);
        zfCoreSetFrequency(dev, wd->frequency);
    }
    else
    {
        zmw_enter_critical_section(dev);
        if( frequency == 0 )
        { // Auto select clean channel depend on wireless environment !
            wd->ws.autoSetFrequency = 0;
        }
        wd->ws.frequency = (u16_t) (frequency/1000);
        zmw_leave_critical_section(dev);
    }
}

void zfiWlanSetBssid(zdev_t* dev, u8_t* bssid)
{
    u16_t i;
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    for (i=0; i<6; i++)
    {
        wd->ws.desiredBssid[i] = bssid[i];
    }
    wd->ws.bDesiredBssid = TRUE;
    zmw_leave_critical_section(dev);

}

void zfiWlanSetBeaconInterval(zdev_t* dev,
                              u16_t  beaconInterval,
                              u8_t   bImmediate)
{
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    if ( bImmediate )
    {
        zmw_enter_critical_section(dev);
        wd->beaconInterval = beaconInterval;
        zmw_leave_critical_section(dev);

        /* update beacon interval here */
    }
    else
    {
        zmw_enter_critical_section(dev);
        wd->ws.beaconInterval = beaconInterval;
        zmw_leave_critical_section(dev);
    }
}


void zfiWlanSetDtimCount(zdev_t* dev, u8_t  dtim)
{
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    if (dtim > 0)
    {
        wd->ws.dtim = dtim;
    }
    zmw_leave_critical_section(dev);
}


void zfiWlanSetAtimWindow(zdev_t* dev, u16_t atimWindow, u8_t bImmediate)
{
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    if ( bImmediate )
    {
        zmw_enter_critical_section(dev);
        wd->sta.atimWindow = atimWindow;
        zmw_leave_critical_section(dev);

        /* atim window here */
    }
    else
    {
        zmw_enter_critical_section(dev);
        wd->ws.atimWindow = atimWindow;
        zmw_leave_critical_section(dev);
    }
}


void zfiWlanSetEncryMode(zdev_t* dev, u8_t encryMode)
{
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    if (wd->wlanMode == ZM_MODE_AP)
    {
        /* Hostapd Issue */
        if ((wd->ws.encryMode != ZM_AES) && (wd->ws.encryMode != ZM_TKIP))
            wd->ws.encryMode = encryMode;
    }
    else
        wd->ws.encryMode = encryMode;
    zmw_leave_critical_section(dev);
}

void zfiWlanSetDefaultKeyId(zdev_t* dev, u8_t keyId)
{
    zmw_get_wlan_dev(dev);

    wd->sta.keyId = keyId;
}

u8_t zfiWlanQueryIsPKInstalled(zdev_t *dev, u8_t *staMacAddr)
{
    u8_t isInstalled = 0;

#if 1
//#ifdef ZM_ENABLE_IBSS_WPA2PSK
    u8_t   res, peerIdx;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    res = zfStaFindOppositeByMACAddr(dev, (u16_t *)staMacAddr, &peerIdx);
    if( res == 0 )
    {
        isInstalled = wd->sta.oppositeInfo[peerIdx].pkInstalled;
    }
    zmw_leave_critical_section(dev);
//#endif
#endif

    return isInstalled;
}

u8_t zfiWlanSetKey(zdev_t* dev, struct zsKeyInfo keyInfo)
{
    u16_t  broadcast[3] = {0xffff, 0xffff, 0xffff};
    u32_t* key;
    u8_t   encryMode = ZM_NO_WEP;
#ifdef ZM_ENABLE_IBSS_WPA2PSK
    u8_t   encryType = ZM_NO_WEP;
#endif
    u8_t   micKey[16];
    u16_t  id = 0;
    u8_t   vapId, i, addr[6];
    u8_t   userIdx=0;

#ifdef ZM_ENABLE_IBSS_WPA2PSK
    /* Determine opposite exist or not */
    u8_t   res, peerIdx;
//    u8_t   userIdx=0;

    zmw_get_wlan_dev(dev);

    if ( wd->sta.ibssWpa2Psk == 1 )
    {
        zmw_enter_critical_section(dev);
        res = zfStaFindOppositeByMACAddr(dev, (u16_t*)keyInfo.macAddr, &peerIdx);
        if( res == 0 )
        {
            userIdx = peerIdx;
            if ( wd->sta.oppositeInfo[userIdx].camIdx == 0xff )
                wd->sta.oppositeInfo[userIdx].camIdx = userIdx;
        }
        zmw_leave_critical_section(dev);
    }
#else
    zmw_get_wlan_dev(dev);
#endif

    if ( keyInfo.flag & ZM_KEY_FLAG_AUTHENTICATOR )
    {   /* set key by authenticator */
        /* set pairwise key */
        if (keyInfo.flag & ZM_KEY_FLAG_PK)
        {
            /* Find STA's information */
            id = zfApFindSta(dev, keyInfo.macAddr);
            if (id == 0xffff)
            {
                /* Can't STA in the staTable */
                return ZM_STATUS_FAILURE;
            }

            wd->ap.staTable[id].iv16 = 0;
            wd->ap.staTable[id].iv32 = 0;

            if (keyInfo.keyLength == 32)
            {   /* TKIP */
                //u8_t KeyRsc[6] = {0, 0, 0, 0, 0, 0};

                /* In the current AP mode, we set KeyRsc to zero */
                //zfTkipInit(keyInfo.key, (u8_t*) wd->macAddr,
                //           &(wd->ap.staTable[id].txSeed), KeyRsc);
                //zfTkipInit(keyInfo.key, (u8_t*) keyInfo.macAddr,
                //           &(wd->ap.staTable[id].rxSeed), KeyRsc);
#ifdef ZM_ENABLE_CENC
                if (keyInfo.flag & ZM_KEY_FLAG_CENC)
                {
                    zm_debug_msg0("Set CENC pairwise Key");

                    wd->ap.staTable[id].encryMode = ZM_CENC;

                    /* Reset txiv and rxiv */
                    wd->ap.staTable[id].txiv[0] = 0x5c365c37;
                    wd->ap.staTable[id].txiv[1] = 0x5c365c36;
                    wd->ap.staTable[id].txiv[2] = 0x5c365c36;
                    wd->ap.staTable[id].txiv[3] = 0x5c365c36;

                    wd->ap.staTable[id].rxiv[0] = 0x5c365c36;
                    wd->ap.staTable[id].rxiv[1] = 0x5c365c36;
                    wd->ap.staTable[id].rxiv[2] = 0x5c365c36;
                    wd->ap.staTable[id].rxiv[3] = 0x5c365c36;

                    /* Set Key Index */
                    wd->ap.staTable[id].cencKeyIdx = keyInfo.keyIndex;

                    //zfCoreSetKey(dev, id+1, 1, ZM_CENC, (u16_t *)keyInfo.macAddr,
                    //          (u32_t*) &keyInfo.key[16]);
                }
                else
#endif //ZM_ENABLE_CENC
                {
                    wd->ap.staTable[id].encryMode = ZM_TKIP;

                    zfMemoryCopy(micKey, &keyInfo.key[16], 8);
                    zfMemoryCopy(&micKey[8], &keyInfo.key[24], 8);

                    //zfCoreSetKey(dev, id+1, 1, ZM_TKIP, (u16_t *)keyInfo.macAddr,
                    //           (u32_t*) micKey);

                    /* For fragmentation, we use software MIC */
                    zfMemoryCopy((u8_t *)&(wd->ap.staTable[id].txMicKey), &(keyInfo.key[16]), 8);
                    zfMemoryCopy((u8_t *)&(wd->ap.staTable[id].rxMicKey), &(keyInfo.key[24]), 8);

                }
            }
            else if (keyInfo.keyLength == 16)
            {   /* AES */
                wd->ap.staTable[id].encryMode = ZM_AES;
            }
            else if (keyInfo.keyLength == 0)
            {
                /* Clear Key Info */
                zfApClearStaKey(dev, (u16_t *)keyInfo.macAddr);

                return ZM_STATUS_SUCCESS;
            }
            else
            {
                return ZM_STATUS_FAILURE;
            }

            //zfCoreSetKey(dev, id+1, 0, wd->ap.staTable[id].encryMode,
            //      (u16_t *)keyInfo.macAddr, (u32_t*) keyInfo.key);
            zfHpSetApPairwiseKey(dev, (u16_t *)keyInfo.macAddr,
                    wd->ap.staTable[id].encryMode, (u32_t*) keyInfo.key,
                    (u32_t*) &keyInfo.key[16], id+1);
            wd->ap.staTable[id].keyIdx = id + 1 + 4;
        }
        else if (keyInfo.flag & ZM_KEY_FLAG_GK)
        {
            vapId = keyInfo.vapId;

            wd->ap.iv16[vapId] = 0;
            wd->ap.iv32[vapId] = 0;

            if (keyInfo.keyLength == 32)
            {   /* TKIP */
                //u8_t KeyRsc[6] = {0, 0, 0, 0, 0, 0};

                //zfTkipInit(keyInfo.key, (u8_t*) wd->macAddr,
                //           &(wd->ap.bcSeed), KeyRsc);
#ifdef ZM_ENABLE_CENC
                if (keyInfo.flag & ZM_KEY_FLAG_CENC)
                {
                    encryMode = ZM_CENC;
                    zm_debug_msg0("Set CENC group Key");

                    /* Reset txiv and rxiv */
                    wd->ap.txiv[vapId][0] = 0x5c365c36;
                    wd->ap.txiv[vapId][1] = 0x5c365c36;
                    wd->ap.txiv[vapId][2] = 0x5c365c36;
                    wd->ap.txiv[vapId][3] = 0x5c365c36;

                    //zfCoreSetKey(dev, 0, 1, ZM_CENC, keyInfo.vapAddr,
                    //          (u32_t*) &keyInfo.key[16]);
                    key = (u32_t*) keyInfo.key;
                }
                else
#endif //ZM_ENABLE_CENC
                {
                    encryMode = ZM_TKIP;
                    key = (u32_t *)keyInfo.key;

                    /* set MIC key to HMAC */
                    //zfCoreSetKey(dev, 0, 1, ZM_TKIP, broadcast,
                    //         (u32_t*) (&keyInfo.key[16]));
                    //zfCoreSetKey(dev, 0, 1, ZM_TKIP, keyInfo.vapAddr,
                    //           (u32_t*) (&keyInfo.key[16]));

                    zfMicSetKey(&(keyInfo.key[16]), &(wd->ap.bcMicKey[0]));
                    key = (u32_t*) keyInfo.key;
                }
            }
            else if (keyInfo.keyLength == 16)
            {   /* AES */
                encryMode = ZM_AES;
                key = (u32_t *)keyInfo.key;
                zm_debug_msg0("CWY - Set AES Group Key");
            }
            else if (keyInfo.keyLength == 0)
            {
                /* Clear Key Info */
                zfApClearStaKey(dev, broadcast);

                /* Turn off WEP bit in the capability field */
                wd->ap.capab[vapId] &= 0xffef;

                return ZM_STATUS_SUCCESS;
            }
            else
            {   /* WEP */
                if (keyInfo.keyLength == 5)
                {
                    encryMode = ZM_WEP64;
                }
                else if (keyInfo.keyLength == 13)
                {
                    encryMode = ZM_WEP128;
                }
                else if (keyInfo.keyLength == 29)
                {
                    encryMode = ZM_WEP256;
                }

                key = (u32_t*) keyInfo.key;
            }

            // Modification for CAM not support VAP search
            //zfCoreSetKey(dev, 0, 0, encryMode, broadcast, key);
            //zfCoreSetKey(dev, 0, 0, encryMode, wd->macAddr, key);
            //zfCoreSetKey(dev, 0, 0, encryMode, keyInfo.vapAddr, key);
            zfHpSetApGroupKey(dev, wd->macAddr, encryMode,
                    key, (u32_t*) &keyInfo.key[16], vapId);

            //zfiWlanSetEncryMode(dev, encryMode);
            wd->ws.encryMode = encryMode;

            /* set the multicast address encryption type */
            wd->ap.encryMode[vapId] = encryMode;

            /* set the multicast key index */
            wd->ap.bcKeyIndex[vapId] = keyInfo.keyIndex;
            wd->ap.bcHalKeyIdx[vapId] = vapId + 60;

            /* Turn on WEP bit in the capability field */
            wd->ap.capab[vapId] |= 0x10;
        }
    }
    else
    {   /* set by supplicant */

        if ( keyInfo.flag & ZM_KEY_FLAG_PK )
        {   /* set pairwise key */

            //zfTkipInit(keyInfo.key, (u8_t*) wd->macAddr,
            //           &wd->sta.txSeed, keyInfo.initIv);
            //zfTkipInit(keyInfo.key, (u8_t*) wd->sta.bssid,
            //           &wd->sta.rxSeed[keyInfo.keyIndex], keyInfo.initIv);

#ifdef ZM_ENABLE_IBSS_WPA2PSK
            if ( wd->sta.ibssWpa2Psk == 1 )
            {
                /* unicast -- > pairwise key */
                wd->sta.oppositeInfo[userIdx].iv16 = 0;
                wd->sta.oppositeInfo[userIdx].iv32 = 0;
            }
            else
            {
                wd->sta.iv16 = 0;
                wd->sta.iv32 = 0;
            }

            wd->sta.oppositeInfo[userIdx].pkInstalled = 1;
#else
            wd->sta.iv16 = 0;
            wd->sta.iv32 = 0;

            wd->sta.oppositeInfo[userIdx].pkInstalled = 1;
#endif

            if ( keyInfo.keyLength == 32 )
            {   /* TKIP */
                zfTkipInit(keyInfo.key, (u8_t*) wd->macAddr,
                        &wd->sta.txSeed, keyInfo.initIv);
                zfTkipInit(keyInfo.key, (u8_t*) wd->sta.bssid,
                        &wd->sta.rxSeed[keyInfo.keyIndex], keyInfo.initIv);

#ifdef ZM_ENABLE_CENC
                if (keyInfo.flag & ZM_KEY_FLAG_CENC)
                {
                    zm_debug_msg0("Set CENC pairwise Key");

                    wd->sta.encryMode = ZM_CENC;

                    /* Reset txiv and rxiv */
                    wd->sta.txiv[0] = 0x5c365c36;
                    wd->sta.txiv[1] = 0x5c365c36;
                    wd->sta.txiv[2] = 0x5c365c36;
                    wd->sta.txiv[3] = 0x5c365c36;

                    wd->sta.rxiv[0] = 0x5c365c37;
                    wd->sta.rxiv[1] = 0x5c365c36;
                    wd->sta.rxiv[2] = 0x5c365c36;
                    wd->sta.rxiv[3] = 0x5c365c36;

                    /* Set Key Index */
                    wd->sta.cencKeyId = keyInfo.keyIndex;

                    //zfCoreSetKey(dev, id+1, 1, ZM_CENC, (u16_t *)keyInfo.macAddr,
                    //         (u32_t*) &keyInfo.key[16]);
                }
                else
#endif //ZM_ENABLE_CENC
                {
                    wd->sta.encryMode = ZM_TKIP;

                    //zfCoreSetKey(dev, 0, 1, ZM_TKIP, wd->sta.bssid,
                    //         (u32_t*) &keyInfo.key[16]);

                    zfMicSetKey(&keyInfo.key[16], &wd->sta.txMicKey);
                    zfMicSetKey(&keyInfo.key[24],
                                &wd->sta.rxMicKey[keyInfo.keyIndex]);
                }
            }
            else if ( keyInfo.keyLength == 16 )
            {   /* AES */
#ifdef ZM_ENABLE_IBSS_WPA2PSK
                if ( wd->sta.ibssWpa2Psk == 1 )
                {
                    wd->sta.oppositeInfo[userIdx].encryMode = ZM_AES;
                    encryType = wd->sta.oppositeInfo[userIdx].encryMode;
                }
                else
                {
                    wd->sta.encryMode = ZM_AES;
                    encryType = wd->sta.encryMode;
                }
#else
                wd->sta.encryMode = ZM_AES;
#endif
            }
            else
            {
                return ZM_STATUS_FAILURE;
            }

            /* user 0 */
            //zfCoreSetKey(dev, 0, 0, wd->sta.encryMode,
            //         wd->sta.bssid, (u32_t*) keyInfo.key);
            //zfHpSetStaPairwiseKey(dev, wd->sta.bssid, wd->sta.encryMode,
            //    (u32_t*) keyInfo.key, (u32_t*) &keyInfo.key[16]);

#ifdef ZM_ENABLE_IBSS_WPA2PSK
            if ( (keyInfo.keyLength==16) && (wd->sta.ibssWpa2Psk==1) )
            { /* If not AES-CCMP and ibss network , use traditional */
                zfHpSetPerUserKey(dev,
                                userIdx,
                                keyInfo.keyIndex,  // key id == 0 ( Pairwise key = 0 )
                                (u8_t*)keyInfo.macAddr,   // RX need Source Address ( Address 2 )
                                encryType,
//                              wd->sta.encryMode,
                                (u32_t*) keyInfo.key, (u32_t*) &keyInfo.key[16]);

                wd->sta.oppositeInfo[userIdx].wpaState = ZM_STA_WPA_STATE_PK_OK ;
            }
            else
            {/* Big Endian and Little Endian Compatibility */
                for (i = 0; i < 3; i++)
                {
                    addr[2 * i] = wd->sta.bssid[i] & 0xff;
                    addr[2 * i + 1] = wd->sta.bssid[i] >> 8;
                }
                zfHpSetPerUserKey(dev,
                                    ZM_USER_KEY_PK,   // user id
                                    0,                // key id
                                    addr,//(u8_t *)wd->sta.bssid,
                              wd->sta.encryMode,
                              (u32_t*) keyInfo.key, (u32_t*) &keyInfo.key[16]);

                wd->sta.keyId = 4;
            }
#else
            /* Big Endian and Little Endian Compatibility */
            for (i = 0; i < 3; i++)
            {
                addr[2 * i] = wd->sta.bssid[i] & 0xff;
                addr[2 * i + 1] = wd->sta.bssid[i] >> 8;
            }
            zfHpSetPerUserKey(dev,
                              ZM_USER_KEY_PK,   // user id
                              0,                // key id
                              addr,//(u8_t *)wd->sta.bssid,
                              wd->sta.encryMode,
                              (u32_t*) keyInfo.key, (u32_t*) &keyInfo.key[16]);

            wd->sta.keyId = 4;
#endif

            wd->sta.wpaState = ZM_STA_WPA_STATE_PK_OK;
        }
        else if ( keyInfo.flag & ZM_KEY_FLAG_GK )
        {   /* set group key */

            zfTkipInit(keyInfo.key, (u8_t*) wd->sta.bssid,
                       &wd->sta.rxSeed[keyInfo.keyIndex], keyInfo.initIv);

            if ( keyInfo.keyLength == 32 )
            {   /* TKIP */
#ifdef ZM_ENABLE_CENC
                if (keyInfo.flag & ZM_KEY_FLAG_CENC)
                {
                    encryMode = ZM_CENC;
                    zm_debug_msg0("Set CENC group Key");

                    /* Reset txiv and rxiv */
                    wd->sta.rxivGK[0] = 0x5c365c36;
                    wd->sta.rxivGK[1] = 0x5c365c36;
                    wd->sta.rxivGK[2] = 0x5c365c36;
                    wd->sta.rxivGK[3] = 0x5c365c36;

                    //zfCoreSetKey(dev, 0, 1, ZM_CENC, keyInfo.vapAddr,
                    //         (u32_t*) &keyInfo.key[16]);
                    key = (u32_t*) keyInfo.key;
                }
                else
#endif //ZM_ENABLE_CENC
                {
                    encryMode = ZM_TKIP;
                    key = (u32_t*) wd->sta.rxSeed[keyInfo.keyIndex].tk;

                    if ( !(keyInfo.flag & ZM_KEY_FLAG_INIT_IV) )
                    {
                        wd->sta.rxSeed[keyInfo.keyIndex].iv16 = 0;
                        wd->sta.rxSeed[keyInfo.keyIndex].iv32 = 0;
                    }

                    /* set MIC key to HMAC */
                    //zfCoreSetKey(dev, 8, 1, ZM_TKIP, broadcast,
                    //         (u32_t*) (&keyInfo.key[16]));

                    zfMicSetKey(&keyInfo.key[24],
                                &wd->sta.rxMicKey[keyInfo.keyIndex]);
                }
            }
            else if ( keyInfo.keyLength == 16 )
            {   /* AES */
                encryMode = ZM_AES;
                //key = (u32_t*) wd->sta.rxSeed[keyInfo.keyIndex].tk;
            }
            else
            {   /* WEP */
                if ( keyInfo.keyLength == 5 )
                {
                    encryMode = ZM_WEP64;
                }
                else if ( keyInfo.keyLength == 13 )
                {
                    encryMode = ZM_WEP128;
                }
                else if ( keyInfo.keyLength == 29 )
                {
                    encryMode = ZM_WEP256;
                }

                key = (u32_t*) keyInfo.key;
            }

            /* user 8 */
            //zfCoreSetKey(dev, 8, 0, encryMode, broadcast, key);
            //zfHpSetStaGroupKey(dev, broadcast, encryMode,
            //        (u32_t*) keyInfo.key, (u32_t*) (&keyInfo.key[16]));

#ifdef ZM_ENABLE_IBSS_WPA2PSK
            if ( (keyInfo.keyLength==16) && (wd->sta.ibssWpa2Psk==1) )
            {/* If not AES-CCMP and ibss network , use traditional */
                zfHpSetPerUserKey(dev,
                              userIdx,
                              keyInfo.keyIndex,                // key id
                              // (u8_t *)broadcast,                  // for only 2 stations IBSS netwrl ( A2 )
                              (u8_t*)keyInfo.macAddr,   // for multiple ( > 2 ) stations IBSS network ( A2 )
                              encryMode,
                              (u32_t*) keyInfo.key, (u32_t*) &keyInfo.key[16]);
            }
            else
            {
                zfHpSetPerUserKey(dev,
                                ZM_USER_KEY_GK,   // user id
                                0,                // key id
                                (u8_t *)broadcast,
                                encryMode,
                                (u32_t*) keyInfo.key, (u32_t*) &keyInfo.key[16]);

                wd->sta.wpaState = ZM_STA_WPA_STATE_GK_OK;
            }
#else
            zfHpSetPerUserKey(dev,
                              ZM_USER_KEY_GK,   // user id
                              0,                // key id
                              (u8_t *)broadcast,
                              encryMode,
                              (u32_t*) keyInfo.key, (u32_t*) &keyInfo.key[16]);

            wd->sta.wpaState = ZM_STA_WPA_STATE_GK_OK;
#endif
        }
        else
        {   /* legacy WEP */
            zm_debug_msg0("legacy WEP");

            if ( keyInfo.keyIndex >= 4 )
            {
                return ZM_STATUS_FAILURE;
            }

            if ( keyInfo.keyLength == 5 )
            {
                zm_debug_msg0("WEP 64");

                encryMode = ZM_WEP64;
            }
            else if ( keyInfo.keyLength == 13 )
            {
                zm_debug_msg0("WEP 128");

                encryMode = ZM_WEP128;
            }
            else if ( keyInfo.keyLength == 32 )
            {
                /* TKIP */
                #if 0
                // Don't reset the IV since some AP would fail in IV check and drop our connection
                if ( wd->sta.wpaState != ZM_STA_WPA_STATE_PK_OK )
                {
                    wd->sta.iv16 = 0;
                    wd->sta.iv32 = 0;
                }
                #endif

                encryMode = ZM_TKIP;

                zfTkipInit(keyInfo.key, (u8_t*) wd->sta.bssid,
                           &wd->sta.rxSeed[keyInfo.keyIndex], keyInfo.initIv);
                zfMicSetKey(&keyInfo.key[24],
                           &wd->sta.rxMicKey[keyInfo.keyIndex]);
            }
            else if ( keyInfo.keyLength == 16 )
            {
                /* AES */
                #if 0
                // Don't reset the IV since some AP would fail in IV check and drop our connection
                if ( wd->sta.wpaState != ZM_STA_WPA_STATE_PK_OK )
                {
                    /* broadcast -- > group key */
                    /* Only initialize when set our default key ! */
                    wd->sta.iv16 = 0;
                    wd->sta.iv32 = 0;
                }
                #endif

                encryMode = ZM_AES;
            }
            else if ( keyInfo.keyLength == 29 )
            {
                zm_debug_msg0("WEP 256");

                encryMode = ZM_WEP256;
                //zfCoreSetKey(dev, 64, 1, wd->sta.encryMode,
                //         wd->sta.bssid, (u32_t*) (&keyInfo.key[16]));
            }
            else
            {
                return ZM_STATUS_FAILURE;
            }

            {
                u8_t i;

                zm_debug_msg0("key = ");
                for(i = 0; i < keyInfo.keyLength; i++)
                {
                    zm_debug_msg2("", keyInfo.key[i]);
                }
            }

            if ( keyInfo.flag & ZM_KEY_FLAG_DEFAULT_KEY )
            {
                //for WEP default key 1~3 and ATOM platform--CWYang(+)
                vapId = 0;
                wd->ap.bcHalKeyIdx[vapId] = keyInfo.keyIndex;
                wd->ap.bcKeyIndex[vapId] = keyInfo.keyIndex;
                wd->sta.keyId = keyInfo.keyIndex;
            }

			if(encryMode == ZM_TKIP)
			{
				if(wd->TKIP_Group_KeyChanging == 0x1)
				{
					zm_debug_msg0("Countermeasure : Cancel Old Timer ");
					zfTimerCancel(dev,	ZM_EVENT_SKIP_COUNTERMEASURE);
				}
				else
				{
					zm_debug_msg0("Countermeasure : Create New Timer ");
				}

				wd->TKIP_Group_KeyChanging = 0x1;
				zfTimerSchedule(dev, ZM_EVENT_SKIP_COUNTERMEASURE, 150);
			}



			//------------------------------------------------------------------------

            /* use default key */
            //zfCoreSetKey(dev, ZM_USER_KEY_DEFAULT+keyInfo.keyIndex, 0,
            //         wd->sta.encryMode, wd->sta.bssid, (u32_t*) keyInfo.key);

            if ( encryMode == ZM_TKIP ||
                 encryMode == ZM_AES )
            {
                zfHpSetDefaultKey(dev, keyInfo.keyIndex, encryMode,
                                 (u32_t*) keyInfo.key, (u32_t*) &keyInfo.key[16]);

#ifdef ZM_ENABLE_IBSS_WPA2PSK
            if ( (keyInfo.keyLength==16) && (wd->sta.ibssWpa2Psk==1) )
            {/* If not AES-CCMP and ibss network , use traditional */
                wd->sta.wpaState = ZM_STA_WPA_STATE_PK_OK;
            }
            else
            {
                if (wd->sta.wpaState == ZM_STA_WPA_STATE_PK_OK)
                    wd->sta.wpaState = ZM_STA_WPA_STATE_GK_OK;
                else
                {
                    wd->sta.wpaState = ZM_STA_WPA_STATE_PK_OK;
                    wd->sta.encryMode = encryMode;
                    wd->ws.encryMode = encryMode;
                }
            }
#else
                if (wd->sta.wpaState == ZM_STA_WPA_STATE_PK_OK)
                    wd->sta.wpaState = ZM_STA_WPA_STATE_GK_OK;
                else if ( wd->sta.wpaState == ZM_STA_WPA_STATE_INIT )
                {
                    wd->sta.wpaState = ZM_STA_WPA_STATE_PK_OK;
                    wd->sta.encryMode = encryMode;
                    wd->ws.encryMode = encryMode;
                }
#endif
            }
            else
            {
                zfHpSetDefaultKey(dev, keyInfo.keyIndex, encryMode,
                               (u32_t*) keyInfo.key, NULL);

                /* Save key for software WEP */
                zfMemoryCopy(wd->sta.wepKey[keyInfo.keyIndex], keyInfo.key,
                        keyInfo.keyLength);

                /* TODO: Check whether we need to save the SWEncryMode */
                wd->sta.SWEncryMode[keyInfo.keyIndex] = encryMode;

                wd->sta.encryMode = encryMode;
                wd->ws.encryMode = encryMode;
            }
        }
    }

//    wd->sta.flagKeyChanging = 1;
    return ZM_STATUS_SUCCESS;
}

/* PSEUDO test */
u8_t zfiWlanPSEUDOSetKey(zdev_t* dev, struct zsKeyInfo keyInfo)
{
    //u16_t  broadcast[3] = {0xffff, 0xffff, 0xffff};
    //u32_t* key;
    u8_t   micKey[16];

    zmw_get_wlan_dev(dev);

    switch (keyInfo.keyLength)
    {
        case 5:
            wd->sta.encryMode = ZM_WEP64;
            /* use default key */
            zfCoreSetKey(dev, 64, 0, ZM_WEP64, (u16_t *)keyInfo.macAddr, (u32_t*) keyInfo.key);
		          break;

       	case 13:
            wd->sta.encryMode = ZM_WEP128;
            /* use default key */
            zfCoreSetKey(dev, 64, 0, ZM_WEP128, (u16_t *)keyInfo.macAddr, (u32_t*) keyInfo.key);
          		break;

       	case 29:
            wd->sta.encryMode = ZM_WEP256;
            /* use default key */
            zfCoreSetKey(dev, 64, 1, ZM_WEP256,  (u16_t *)keyInfo.macAddr, (u32_t*) (&keyInfo.key[16]));
            zfCoreSetKey(dev, 64, 0, ZM_WEP256, (u16_t *)keyInfo.macAddr, (u32_t*) keyInfo.key);
		          break;

       	case 16:
            wd->sta.encryMode = ZM_AES;
            //zfCoreSetKey(dev, 0, 0, ZM_AES, (u16_t *)keyInfo.macAddr, (u32_t*) keyInfo.key);
            zfCoreSetKey(dev, 64, 0, ZM_AES, (u16_t *)keyInfo.macAddr, (u32_t*) keyInfo.key);
            break;

       	case 32:
#ifdef ZM_ENABLE_CENC
            if (keyInfo.flag & ZM_KEY_FLAG_CENC)
            {
                u16_t boardcastAddr[3] = {0xffff, 0xffff, 0xffff};
                u16_t Addr_a[] = { 0x0000, 0x0080, 0x0901};
                u16_t Addr_b[] = { 0x0000, 0x0080, 0x0902};
                /* CENC test: user0,1 and user2 for boardcast */
                wd->sta.encryMode = ZM_CENC;
                zfCoreSetKey(dev, 0, 1, ZM_CENC, (u16_t *)Addr_a, (u32_t*) (&keyInfo.key[16]));
                zfCoreSetKey(dev, 0, 0, ZM_CENC, (u16_t *)Addr_a, (u32_t*) keyInfo.key);

                zfCoreSetKey(dev, 1, 1, ZM_CENC, (u16_t *)Addr_b, (u32_t*) (&keyInfo.key[16]));
                zfCoreSetKey(dev, 1, 0, ZM_CENC, (u16_t *)Addr_b, (u32_t*) keyInfo.key);

                zfCoreSetKey(dev, 2, 1, ZM_CENC, (u16_t *)boardcastAddr, (u32_t*) (&keyInfo.key[16]));
                zfCoreSetKey(dev, 2, 0, ZM_CENC, (u16_t *)boardcastAddr, (u32_t*) keyInfo.key);

                /* Initialize PN sequence */
                wd->sta.txiv[0] = 0x5c365c36;
                wd->sta.txiv[1] = 0x5c365c36;
                wd->sta.txiv[2] = 0x5c365c36;
                wd->sta.txiv[3] = 0x5c365c36;
            }
            else
#endif //ZM_ENABLE_CENC
            {
                wd->sta.encryMode = ZM_TKIP;
                zfCoreSetKey(dev, 64, 1, ZM_TKIP, (u16_t *)keyInfo.macAddr, (u32_t*) micKey);
                zfCoreSetKey(dev, 64, 0, ZM_TKIP, (u16_t *)keyInfo.macAddr, (u32_t*) keyInfo.key);
            }
            break;
        default:
            wd->sta.encryMode = ZM_NO_WEP;
    }

    return ZM_STATUS_SUCCESS;
}

void zfiWlanSetPowerSaveMode(zdev_t* dev, u8_t mode)
{
#if 0
    zmw_get_wlan_dev(dev);

    wd->sta.powerSaveMode = mode;

    /* send null data with PwrBit to inform AP */
    if ( mode > ZM_STA_PS_NONE )
    {
        if ( wd->wlanMode == ZM_MODE_INFRASTRUCTURE )
        {
            zfSendNullData(dev, 1);
        }

        /* device into PS mode */
        zfPSDeviceSleep(dev);
    }
#endif

    zfPowerSavingMgrSetMode(dev, mode);
}

void zfiWlanSetMacAddress(zdev_t* dev, u16_t* mac)
{
    zmw_get_wlan_dev(dev);

    wd->macAddr[0] = mac[0];
    wd->macAddr[1] = mac[1];
    wd->macAddr[2] = mac[2];

    zfHpSetMacAddress(dev, mac, 0);
}

u8_t zfiWlanQueryWlanMode(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    return wd->wlanMode;
}

u8_t zfiWlanQueryAdapterState(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    return wd->state;
}

u8_t zfiWlanQueryAuthenticationMode(zdev_t* dev, u8_t bWrapper)
{
    u8_t   authMode;

    zmw_get_wlan_dev(dev);

    if ( bWrapper )
    {
        authMode = wd->ws.authMode;
    }
    else
    {
        //authMode = wd->sta.authMode;
        authMode = wd->sta.currentAuthMode;
    }

    return authMode;
}

u8_t zfiWlanQueryWepStatus(zdev_t* dev, u8_t bWrapper)
{
    u8_t wepStatus;

    zmw_get_wlan_dev(dev);

    if ( bWrapper )
    {
        wepStatus = wd->ws.wepStatus;
    }
    else
    {
        wepStatus = wd->sta.wepStatus;
    }

    return wepStatus;
}

void zfiWlanQuerySSID(zdev_t* dev, u8_t* ssid, u8_t* pSsidLength)
{
    u16_t vapId = 0;
    zmw_get_wlan_dev(dev);

    if (wd->wlanMode == ZM_MODE_AP)
    {
        vapId = zfwGetVapId(dev);

        if (vapId == 0xffff)
        {
            *pSsidLength = wd->ap.ssidLen[0];
            zfMemoryCopy(ssid, wd->ap.ssid[0], wd->ap.ssidLen[0]);
        }
        else
        {
            *pSsidLength = wd->ap.ssidLen[vapId + 1];
            zfMemoryCopy(ssid, wd->ap.ssid[vapId + 1], wd->ap.ssidLen[vapId + 1]);
        }
    }
    else
    {
        *pSsidLength = wd->sta.ssidLen;
        zfMemoryCopy(ssid, wd->sta.ssid, wd->sta.ssidLen);
    }
}

u16_t zfiWlanQueryFragThreshold(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    return wd->fragThreshold;
}

u16_t zfiWlanQueryRtsThreshold(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    return wd->rtsThreshold;
}

u32_t zfiWlanQueryFrequency(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    return (wd->frequency*1000);
}

/***********************************************************
 * Function: zfiWlanQueryCurrentFrequency
 * Return value:
 *   -   0 : no validate current frequency
 *   - (>0): current frequency depend on "qmode"
 * Input:
 *   - qmode:
 *      0: return value depend on the support mode, this
           qmode is use to solve the bug #31223
 *      1: return the actually current frequency
 ***********************************************************/
u32_t zfiWlanQueryCurrentFrequency(zdev_t* dev, u8_t qmode)
{
    u32_t frequency;

    zmw_get_wlan_dev(dev);

    switch (qmode)
    {
    case 0:
        if (wd->sta.currentFrequency > 3000)
        {
            if (wd->supportMode & ZM_WIRELESS_MODE_5)
            {
                frequency = wd->sta.currentFrequency;
            }
            else if (wd->supportMode & ZM_WIRELESS_MODE_24)
            {
                frequency = zfChGetFirst2GhzChannel(dev);
            }
            else
            {
                frequency = 0;
            }
        }
        else
        {
            if (wd->supportMode & ZM_WIRELESS_MODE_24)
            {
                frequency = wd->sta.currentFrequency;
            }
            else if (wd->supportMode & ZM_WIRELESS_MODE_5)
            {
                frequency = zfChGetLast5GhzChannel(dev);
            }
            else
            {
                frequency = 0;
            }
        }
        break;

    case 1:
        frequency = wd->sta.currentFrequency;
        break;

    default:
        frequency = 0;
    }

    return (frequency*1000);
}

u32_t zfiWlanQueryFrequencyAttribute(zdev_t* dev, u32_t freq)
{
    u8_t  i;
    u16_t frequency = (u16_t) (freq/1000);
    u32_t ret = 0;

    zmw_get_wlan_dev(dev);

    for (i = 0; i < wd->regulationTable.allowChannelCnt; i++)
    {
        if ( wd->regulationTable.allowChannel[i].channel == frequency )
        {
            ret = wd->regulationTable.allowChannel[i].channelFlags;
        }
    }

    return ret;
}

/* BandWidth  0=>20  1=>40 */
/* ExtOffset  0=>20  1=>high control 40   3=>low control 40 */
void zfiWlanQueryFrequencyHT(zdev_t* dev, u32_t *bandWidth, u32_t *extOffset)
{
    zmw_get_wlan_dev(dev);

    *bandWidth = wd->BandWidth40;
    *extOffset = wd->ExtOffset;
}

u8_t zfiWlanQueryCWMode(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    return wd->cwm.cw_mode;
}

u32_t zfiWlanQueryCWEnable(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    return wd->cwm.cw_enable;
}

void zfiWlanQueryBssid(zdev_t* dev, u8_t* bssid)
{
    u8_t   addr[6];

    zmw_get_wlan_dev(dev);

    ZM_MAC_WORD_TO_BYTE(wd->sta.bssid, addr);
    zfMemoryCopy(bssid, addr, 6);
}

u16_t zfiWlanQueryBeaconInterval(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    return wd->beaconInterval;
}

u32_t zfiWlanQueryRxBeaconTotal(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);
    wd->sta.rxBeaconTotal += wd->sta.rxBeaconCount;

    return wd->sta.rxBeaconTotal;
}

u16_t zfiWlanQueryAtimWindow(zdev_t* dev)
{
    u16_t atimWindow;

    zmw_get_wlan_dev(dev);

    atimWindow = wd->sta.atimWindow;

    return atimWindow;
}

u8_t zfiWlanQueryEncryMode(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    if (wd->wlanMode == ZM_MODE_AP)
        return wd->ap.encryMode[0];
    else
        return wd->sta.encryMode;
}

u16_t zfiWlanQueryCapability(zdev_t* dev)
{
    u16_t capability;

    zmw_get_wlan_dev(dev);

    capability = wd->sta.capability[0] +
                 (((u16_t) wd->sta.capability[1]) << 8);

    return capability;

}

u16_t zfiWlanQueryAid(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    return wd->sta.aid;
}

void zfiWlanQuerySupportRate(zdev_t* dev, u8_t* rateArray, u8_t* pLength)
{
    u8_t   i, j=0;

    zmw_get_wlan_dev(dev);

    for( i=0; i<4; i++ )
    {
        if ( wd->bRate & (0x1 << i) )
        {
            rateArray[j] = zg11bRateTbl[i] +
                           ((wd->bRateBasic & (0x1<<i))<<(7-i));
            j++;
        }
    }

    *pLength = j;
}

void zfiWlanQueryExtSupportRate(zdev_t* dev, u8_t* rateArray, u8_t* pLength)
{
    u8_t   i, j=0;

    zmw_get_wlan_dev(dev);

    for( i=0; i<8; i++ )
    {
        if ( wd->gRate & (0x1 << i) )
        {
            rateArray[j] = zg11gRateTbl[i] +
                           ((wd->gRateBasic & (0x1<<i))<<(7-i));
            j++;
        }
    }

    *pLength = j;
}

void zfiWlanQueryRsnIe(zdev_t* dev, u8_t* ie, u8_t* pLength)
{
    u8_t len;

    zmw_get_wlan_dev(dev);

    len = wd->sta.rsnIe[1] + 2;
    zfMemoryCopy(ie, wd->sta.rsnIe, len);
    *pLength = len;
}

void zfiWlanQueryWpaIe(zdev_t* dev, u8_t* ie, u8_t* pLength)
{
    u8_t len;

    zmw_get_wlan_dev(dev);

    len = wd->sta.wpaIe[1] + 2;
    zfMemoryCopy(ie, wd->sta.wpaIe, len);
    *pLength = len;

}

u8_t zfiWlanQueryMulticastCipherAlgo(zdev_t *dev)
{
    zmw_get_wlan_dev(dev);

    switch( wd->sta.currentAuthMode )
    {
        case ZM_AUTH_MODE_WPA2PSK:
        case ZM_AUTH_MODE_WPA2:
            if ( wd->sta.rsnIe[7] == 2 )
            {
                return ZM_TKIP;
            }
            else
            {
                return ZM_AES;
            }
            break;

        case ZM_AUTH_MODE_WPAPSK:
        case ZM_AUTH_MODE_WPA:
            if ( wd->sta.rsnIe[11] == 2 )
            {
                return ZM_TKIP;
            }
            else
            {
                return ZM_AES;
            }
            break;

        default:
            return wd->sta.encryMode;
    }
}

u8_t zfiWlanQueryHTMode(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);
    // 0:Legancy, 1:N
    return wd->sta.EnableHT;
}

u8_t zfiWlanQueryBandWidth40(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);
    // 0:20M, 1:40M
    return wd->BandWidth40;
}

u16_t zfiWlanQueryRegionCode(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    return wd->regulationTable.regionCode;
}
void zfiWlanSetWpaIe(zdev_t* dev, u8_t* ie, u8_t Length)
{
    u16_t vapId = 0;
    zmw_get_wlan_dev(dev);

    if (wd->wlanMode == ZM_MODE_AP) // AP Mode
    {
        vapId = zfwGetVapId(dev);

        if (vapId == 0xffff)
            vapId = 0;
        else
            vapId++;

        zm_assert(Length < ZM_MAX_WPAIE_SIZE);
        if (Length < ZM_MAX_WPAIE_SIZE)
        {
            wd->ap.wpaLen[vapId] = Length;
            zfMemoryCopy(wd->ap.wpaIe[vapId], ie, wd->ap.wpaLen[vapId]);
        }

    }
    else
    {
        wd->sta.wpaLen = Length;
        zfMemoryCopy(wd->sta.wpaIe, ie, wd->sta.wpaLen);
    }
    //zfiWlanSetWpaSupport(dev, 1);
    if (wd->wlanMode == ZM_MODE_AP) // AP Mode
    {
        wd->ap.wpaSupport[vapId] = 1;
    }
    else
    {
        wd->sta.wpaSupport = 1;
    }

}

void zfiWlanSetWpaSupport(zdev_t* dev, u8_t WpaSupport)
{
    u16_t vapId = 0;
    zmw_get_wlan_dev(dev);

    if (wd->wlanMode == ZM_MODE_AP) // AP Mode
    {
        vapId = zfwGetVapId(dev);

        if (vapId == 0xffff)
            vapId = 0;
        else
            vapId++;

        wd->ap.wpaSupport[vapId] = WpaSupport;
    }
    else
    {
        wd->sta.wpaSupport = WpaSupport;
    }

}

void zfiWlanSetProtectionMode(zdev_t* dev, u8_t mode)
{
    zmw_get_wlan_dev(dev);

    wd->sta.bProtectionMode = mode;
    if (wd->sta.bProtectionMode == TRUE)
    {
        zfHpSetSlotTime(dev, 0);
    }
    else
    {
        zfHpSetSlotTime(dev, 1);
    }

    zm_msg1_mm(ZM_LV_1, "wd->protectionMode=", wd->sta.bProtectionMode);
}

void zfiWlanSetBasicRate(zdev_t* dev, u8_t bRateSet, u8_t gRateSet,
                         u32_t nRateSet)
{
    zmw_get_wlan_dev(dev);

    wd->ws.bRateBasic = bRateSet;
    wd->ws.gRateBasic = gRateSet;
    wd->ws.nRateBasic = nRateSet;
}

void zfiWlanSetBGMode(zdev_t* dev, u8_t mode)
{
    zmw_get_wlan_dev(dev);

    wd->ws.bgMode = mode;
}

void zfiWlanSetpreambleType(zdev_t* dev, u8_t type)
{
    zmw_get_wlan_dev(dev);

    wd->ws.preambleType = type;
}

u8_t zfiWlanQuerypreambleType(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    return wd->ws.preambleType;
}

u8_t zfiWlanQueryPowerSaveMode(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    return wd->sta.powerSaveMode;
}

u8_t zfiWlanSetPmkidInfo(zdev_t* dev, u16_t* bssid, u8_t* pmkid)
{
    u32_t  i;

    zmw_get_wlan_dev(dev);

    for(i=0; i<wd->sta.pmkidInfo.bssidCount; i++)
    {
        if ( zfMemoryIsEqual((u8_t*) wd->sta.pmkidInfo.bssidInfo[i].bssid,
                             (u8_t*) bssid, 6) )
        {
            /* matched */
            break;
        }
    }

    if ( i < wd->sta.pmkidInfo.bssidCount )
    {
        /* overwrite the original one */
        zfMemoryCopy(wd->sta.pmkidInfo.bssidInfo[i].pmkid, pmkid, 16);
    }
    else
    {
        if ( i < ZM_PMKID_MAX_BSS_CNT )
        {
            wd->sta.pmkidInfo.bssidInfo[i].bssid[0] = bssid[0];
            wd->sta.pmkidInfo.bssidInfo[i].bssid[1] = bssid[1];
            wd->sta.pmkidInfo.bssidInfo[i].bssid[2] = bssid[2];

            zfMemoryCopy(wd->sta.pmkidInfo.bssidInfo[i].pmkid, pmkid, 16);
            wd->sta.pmkidInfo.bssidCount++;
        }
    }

    return 0;
}

u32_t zfiWlanQueryPmkidInfo(zdev_t* dev, u8_t* buf, u32_t len)
{
    //struct zsPmkidInfo* pPmkidInfo = ( struct zsPmkidInfo* ) buf;
    u32_t  size;

    zmw_get_wlan_dev(dev);

    size = sizeof(u32_t) +
           wd->sta.pmkidInfo.bssidCount * sizeof(struct zsPmkidBssidInfo);

    if ( len < size )
    {
        return wd->sta.pmkidInfo.bssidCount;
    }

    zfMemoryCopy(buf, (u8_t*) &wd->sta.pmkidInfo, (u16_t) size);

    return 0;
}

void zfiWlanSetMulticastList(zdev_t* dev, u8_t size, u8_t* pList)
{
    struct zsMulticastAddr* pMacList = (struct zsMulticastAddr*) pList;
    u8_t   i;
    u8_t   bAllMulticast = 0;
    //u32_t  value;

    zmw_get_wlan_dev(dev);

    wd->sta.multicastList.size = size;
    for(i=0; i<size; i++)
    {
        zfMemoryCopy(wd->sta.multicastList.macAddr[i].addr,
                     pMacList[i].addr, 6);
    }

    if ( wd->sta.osRxFilter & ZM_PACKET_TYPE_ALL_MULTICAST )
        bAllMulticast = 1;
    zfHpSetMulticastList(dev, size, pList, bAllMulticast);

}

void zfiWlanRemoveKey(zdev_t* dev, u8_t keyType, u8_t keyId)
{
    u16_t  fakeMacAddr[3] = {0, 0, 0};
    u32_t  fakeKey[4] = {0, 0, 0, 0};

    zmw_get_wlan_dev(dev);

    if ( keyType == 0 )
    {
        /* remove WEP key */
        zm_debug_msg0("remove WEP key");
        zfCoreSetKey(dev, ZM_USER_KEY_DEFAULT+keyId, 0,
                 ZM_NO_WEP, fakeMacAddr, fakeKey);
        wd->sta.encryMode = ZM_NO_WEP;
    }
    else if ( keyType == 1 )
    {
        /* remove pairwise key */
        zm_debug_msg0("remove pairwise key");
        zfHpRemoveKey(dev, ZM_USER_KEY_PK);
        wd->sta.encryMode = ZM_NO_WEP;
    }
    else
    {
        /* remove group key */
        zm_debug_msg0("remove group key");
        zfHpRemoveKey(dev, ZM_USER_KEY_GK);
    }
}


void zfiWlanQueryRegulationTable(zdev_t* dev, struct zsRegulationTable* pEntry)
{
    zmw_get_wlan_dev(dev);

    zfMemoryCopy((u8_t*) pEntry, (u8_t*) &wd->regulationTable,
                 sizeof(struct zsRegulationTable));
}

/* parameter "time" is specified in ms */
void zfiWlanSetScanTimerPerChannel(zdev_t* dev, u16_t time)
{
    zmw_get_wlan_dev(dev);

    zm_debug_msg1("scan time (ms) = ", time);

    wd->sta.activescanTickPerChannel = time / ZM_MS_PER_TICK;
}

void zfiWlanSetAutoReconnect(zdev_t* dev, u8_t enable)
{
    zmw_get_wlan_dev(dev);

    wd->sta.bAutoReconnect = enable;
    //wd->sta.bAutoReconnectEnabled = enable;
}

void zfiWlanSetStaWme(zdev_t* dev, u8_t enable, u8_t uapsdInfo)
{
    zmw_get_wlan_dev(dev);

    wd->ws.staWmeEnabled = enable & 0x3;
    if ((enable & 0x2) != 0)
    {
        wd->ws.staWmeQosInfo = uapsdInfo & 0x6f;
    }
    else
    {
        wd->ws.staWmeQosInfo = 0;
    }
}

void zfiWlanSetApWme(zdev_t* dev, u8_t enable)
{
    zmw_get_wlan_dev(dev);

    wd->ws.apWmeEnabled = enable;
}

u8_t zfiWlanQuerywmeEnable(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    return wd->ws.staWmeEnabled;
}

void zfiWlanSetProbingHiddenSsid(zdev_t* dev, u8_t* ssid, u8_t ssidLen,
    u16_t entry)
{
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();


    if ((ssidLen <= 32) && (entry < ZM_MAX_PROBE_HIDDEN_SSID_SIZE))
    {
        zmw_enter_critical_section(dev);
        wd->ws.probingSsidList[entry].ssidLen = ssidLen;
        zfMemoryCopy(wd->ws.probingSsidList[entry].ssid, ssid, ssidLen);
        zmw_leave_critical_section(dev);
    }

    return;
}

void zfiWlanSetDisableProbingWithSsid(zdev_t* dev, u8_t mode)
{
    zmw_get_wlan_dev(dev);

    wd->sta.disableProbingWithSsid = mode;

    return;
}

void zfiWlanSetDropUnencryptedPackets(zdev_t* dev, u8_t enable)
{
    zmw_get_wlan_dev(dev);

    wd->ws.dropUnencryptedPkts = enable;
}

void zfiWlanSetStaRxSecurityCheckCb(zdev_t* dev, zfpStaRxSecurityCheckCb pStaRxSecurityCheckCb)
{
    zmw_get_wlan_dev(dev);

    wd->sta.pStaRxSecurityCheckCb = pStaRxSecurityCheckCb;
}

void zfiWlanSetIBSSJoinOnly(zdev_t* dev, u8_t joinOnly)
{
    zmw_get_wlan_dev(dev);

    wd->ws.ibssJoinOnly = joinOnly;
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfiConfigWdsPort            */
/*      Configure WDS port.                                             */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      wdsPortId : WDS port ID, start from 0                           */
/*      flag : 0=>disable WDS port, 1=>enable WDS port                  */
/*      wdsAddr : WDS neighbor MAC address                              */
/*      encType : encryption type for WDS port                          */
/*      wdsKey : encryption key for WDS port                            */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      Error code                                                      */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2006.6      */
/*                                                                      */
/************************************************************************/
u16_t zfiConfigWdsPort(zdev_t* dev, u8_t wdsPortId, u16_t flag, u16_t* wdsAddr,
        u16_t encType, u32_t* wdsKey)
{
    u16_t addr[3];
    u32_t key[4];

    zmw_get_wlan_dev(dev);

    if (wdsPortId >= ZM_MAX_WDS_SUPPORT)
    {
        return ZM_ERR_WDS_PORT_ID;
    }

    if (flag == 1)
    {
        /* Enable WDS port */
        wd->ap.wds.macAddr[wdsPortId][0] = wdsAddr[0];
        wd->ap.wds.macAddr[wdsPortId][1] = wdsAddr[1];
        wd->ap.wds.macAddr[wdsPortId][2] = wdsAddr[2];

        wd->ap.wds.wdsBitmap |= (1 << wdsPortId);
        wd->ap.wds.encryMode[wdsPortId] = (u8_t) encType;

        zfCoreSetKey(dev, 10+ZM_MAX_WDS_SUPPORT, 0, (u8_t) encType, wdsAddr, wdsKey);
    }
    else
    {
        /* Disable WDS port */
        addr[0] = addr[1] = addr[2] = 0;
        key[0] = key[1] = key[2] = key[3] = 0;
        wd->ap.wds.wdsBitmap &= (~(1 << wdsPortId));
        zfCoreSetKey(dev, 10+ZM_MAX_WDS_SUPPORT, 0, ZM_NO_WEP, addr, key);
    }

    return ZM_SUCCESS;
}
#ifdef ZM_ENABLE_CENC
/* CENC */
void zfiWlanQueryGSN(zdev_t* dev, u8_t *gsn, u16_t vapId)
{
    //struct zsWlanDev* wd = (struct zsWlanDev*) zmw_wlan_dev(dev);
    u32_t txiv[4];
    zmw_get_wlan_dev(dev);

    /* convert little endian to big endian for 32 bits */
    txiv[3] = wd->ap.txiv[vapId][0];
    txiv[2] = wd->ap.txiv[vapId][1];
    txiv[1] = wd->ap.txiv[vapId][2];
    txiv[0] = wd->ap.txiv[vapId][3];

    zfMemoryCopy(gsn, (u8_t*)txiv, 16);
}
#endif //ZM_ENABLE_CENC
//CWYang(+)
void zfiWlanQuerySignalInfo(zdev_t* dev, u8_t *buffer)
{
    zmw_get_wlan_dev(dev);

    /*Change Signal Strength/Quality Value to Human Sense Here*/

    buffer[0] = wd->SignalStrength;
    buffer[1] = wd->SignalQuality;
}

/* OS-XP */
u16_t zfiStaAddIeWpaRsn(zdev_t* dev, zbuf_t* buf, u16_t offset, u8_t frameType)
{
    return  zfStaAddIeWpaRsn(dev, buf, offset, frameType);
}

/* zfiDebugCmd                                                                        */
/*     cmd       value-description                                                  */
/*         0       schedule timer                                                     */
/*         1       cancel timer                                                         */
/*         2       clear timer                                                           */
/*         3       test timer                                                            */
/*         4                                                                                 */
/*         5                                                                                 */
/*         6       checksum test     0/1                                           */
/*         7       enableProtectionMode                                          */
/*         8       rx packet content dump    0/1                               */

u32_t zfiDebugCmd(zdev_t* dev, u32_t cmd, u32_t value)
{
    u16_t event;
    u32_t tick;
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();


    zmw_enter_critical_section(dev);

    if ( cmd == 0 )
    {   /* schedule timer */
        event = (u16_t) ((value >> 16) & 0xffff);
        tick = value & 0xffff;
        zfTimerSchedule(dev, event, tick);
    }
    else if ( cmd == 1 )
    {   /* cancel timer */
        event = (u16_t) (value & 0xffff);
        zfTimerCancel(dev, event);
    }
    else if ( cmd == 2 )
    {   /* clear timer */
        zfTimerClear(dev);
    }
    else if ( cmd == 3 )
    {   /* test timer */
        zfTimerSchedule(dev, 1,  500);
        zfTimerSchedule(dev, 2, 1000);
        zfTimerSchedule(dev, 3, 1000);
        zfTimerSchedule(dev, 4, 1000);
        zfTimerSchedule(dev, 5, 1500);
        zfTimerSchedule(dev, 6, 2000);
        zfTimerSchedule(dev, 7, 2200);
        zfTimerSchedule(dev, 6, 2500);
        zfTimerSchedule(dev, 8, 2800);
    }
    else if ( cmd == 4)
    {
        zfTimerSchedule(dev, 1,  500);
        zfTimerSchedule(dev, 2, 1000);
        zfTimerSchedule(dev, 3, 1000);
        zfTimerSchedule(dev, 4, 1000);
        zfTimerSchedule(dev, 5, 1500);
        zfTimerSchedule(dev, 6, 2000);
        zfTimerSchedule(dev, 7, 2200);
        zfTimerSchedule(dev, 6, 2500);
        zfTimerSchedule(dev, 8, 2800);
        zfTimerCancel(dev, 1);
        zfTimerCancel(dev, 3);
        zfTimerCancel(dev, 6);
    }
    else if ( cmd == 5 )
    {
        wd->sta.keyId = (u8_t) value;
    }
	else if ( cmd == 6 )
	{
	    /* 0: normal    1: always set TCP/UDP checksum zero */
        wd->checksumTest = value;
	}
	else if ( cmd == 7 )
	{
        wd->enableProtectionMode = value;
   	    zm_msg1_mm(ZM_LV_1, "wd->enableProtectionMode=", wd->enableProtectionMode);
	}
	else if ( cmd == 8 )
	{
        /* rx packet content dump */
        if (value)
        {
            wd->rxPacketDump = 1;
        }
        else
        {
            wd->rxPacketDump = 0;
        }
	}


    zmw_leave_critical_section(dev);

    return 0;
}

#ifdef ZM_ENABLE_CENC
u8_t zfiWlanSetCencPairwiseKey(zdev_t* dev, u8_t keyid, u32_t *txiv, u32_t *rxiv,
        u8_t *key, u8_t *mic)
{
    struct zsKeyInfo keyInfo;
    u8_t cencKey[32];
    u8_t i;
    u16_t macAddr[3];

    zmw_get_wlan_dev(dev);

    for (i = 0; i < 16; i++)
        cencKey[i] = key[i];
    for (i = 0; i < 16; i++)
        cencKey[i + 16] = mic[i];
    keyInfo.key = cencKey;
    keyInfo.keyLength = 32;
    keyInfo.keyIndex = keyid;
    keyInfo.flag = ZM_KEY_FLAG_CENC | ZM_KEY_FLAG_PK;
    for (i = 0; i < 3; i++)
        macAddr[i] = wd->sta.bssid[i];
    keyInfo.macAddr = macAddr;

    zfiWlanSetKey(dev, keyInfo);

    /* Reset txiv and rxiv */
    //wd->sta.txiv[0] = txiv[0];
    //wd->sta.txiv[1] = txiv[1];
    //wd->sta.txiv[2] = txiv[2];
    //wd->sta.txiv[3] = txiv[3];
    //
    //wd->sta.rxiv[0] = rxiv[0];
    //wd->sta.rxiv[1] = rxiv[1];
    //wd->sta.rxiv[2] = rxiv[2];
    //wd->sta.rxiv[3] = rxiv[3];

    return 0;
}

u8_t zfiWlanSetCencGroupKey(zdev_t* dev, u8_t keyid, u32_t *rxiv,
        u8_t *key, u8_t *mic)
{
    struct zsKeyInfo keyInfo;
    u8_t cencKey[32];
    u8_t i;
    u16_t macAddr[6] = {0xffff, 0xffff, 0xffff};

    zmw_get_wlan_dev(dev);

    for (i = 0; i < 16; i++)
        cencKey[i] = key[i];
    for (i = 0; i < 16; i++)
        cencKey[i + 16] = mic[i];
    keyInfo.key = cencKey;
    keyInfo.keyLength = 32;
    keyInfo.keyIndex = keyid;
    keyInfo.flag = ZM_KEY_FLAG_CENC | ZM_KEY_FLAG_GK;
    keyInfo.vapId = 0;
    for (i = 0; i < 3; i++)
        keyInfo.vapAddr[i] = wd->macAddr[i];
    keyInfo.macAddr = macAddr;

    zfiWlanSetKey(dev, keyInfo);

    /* Reset txiv and rxiv */
    wd->sta.rxivGK[0] = ((rxiv[3] >> 24) & 0xFF)
                      + (((rxiv[3] >> 16) & 0xFF) << 8)
                      + (((rxiv[3] >> 8) & 0xFF) << 16)
                      + ((rxiv[3] & 0xFF) << 24);
    wd->sta.rxivGK[1] = ((rxiv[2] >> 24) & 0xFF)
                      + (((rxiv[2] >> 16) & 0xFF) << 8)
                      + (((rxiv[2] >> 8) & 0xFF) << 16)
                      + ((rxiv[2] & 0xFF) << 24);
    wd->sta.rxivGK[2] = ((rxiv[1] >> 24) & 0xFF)
                      + (((rxiv[1] >> 16) & 0xFF) << 8)
                      + (((rxiv[1] >> 8) & 0xFF) << 16)
                      + ((rxiv[1] & 0xFF) << 24);
    wd->sta.rxivGK[3] = ((rxiv[0] >> 24) & 0xFF)
                      + (((rxiv[0] >> 16) & 0xFF) << 8)
                      + (((rxiv[0] >> 8) & 0xFF) << 16)
                      + ((rxiv[0] & 0xFF) << 24);

    wd->sta.authMode = ZM_AUTH_MODE_CENC;
    wd->sta.currentAuthMode = ZM_AUTH_MODE_CENC;

    return 0;
}
#endif //ZM_ENABLE_CENC

u8_t zfiWlanSetDot11DMode(zdev_t* dev, u8_t mode)
{
    u8_t i;

    zmw_get_wlan_dev(dev);

    wd->sta.b802_11D = mode;
    if (mode) //Enable 802.11d
    {
        wd->regulationTable.regionCode = NO_ENUMRD;
        for (i = 0; i < wd->regulationTable.allowChannelCnt; i++)
            wd->regulationTable.allowChannel[i].channelFlags |= ZM_REG_FLAG_CHANNEL_PASSIVE;
    }
    else //Disable
    {
        for (i = 0; i < wd->regulationTable.allowChannelCnt; i++)
            wd->regulationTable.allowChannel[i].channelFlags &= ~ZM_REG_FLAG_CHANNEL_PASSIVE;
    }

    return 0;
}

u8_t zfiWlanSetDot11HDFSMode(zdev_t* dev, u8_t mode)
{
    zmw_get_wlan_dev(dev);

    //zm_debug_msg0("CWY - Enable 802.11h DFS");

    // TODO : DFS Enable in 5250 to 5350 MHz and 5470 to 5725 MHz .
    //if ( Adapter->ZD80211HSupport &&
    //   Adapter->CardSetting.NetworkTypeInUse == Ndis802_11OFDM5 &&
    //   ((ChannelNo >=52 && ChannelNo <= 64)	||				//5250~5350 MHZ
    //    (ChannelNo >=100 && ChannelNo <= 140))) 			//5470~5725 MHZ
    //{
    //   Adapter->ZD80211HSetting.DFSEnable=TRUE;
    //}
    //else
    //{
    //   Adapter->ZD80211HSetting.DFSEnable=FALSE;
    //}

    wd->sta.DFSEnable = mode;
    if (mode)
        wd->sta.capability[1] |= ZM_BIT_0;
    else
        wd->sta.capability[1] &= (~ZM_BIT_0);

    return 0;
}

u8_t zfiWlanSetDot11HTPCMode(zdev_t* dev, u8_t mode)
{
    zmw_get_wlan_dev(dev);

    // TODO : TPC Enable in 5150~5350 MHz and 5470~5725MHz.
    //if ( Adapter->ZD80211HSupport &&
    //   Adapter->CardSetting.NetworkTypeInUse == Ndis802_11OFDM5 &&
    //   ((ChannelNo == 36 || ChannelNo == 40 || ChannelNo == 44 || ChannelNo == 48) ||	//5150~5250 MHZ , Not Japan
    //    (ChannelNo >=52 && ChannelNo <= 64) ||				//5250~5350 MHZ
    //    (ChannelNo >=100 && ChannelNo <= 140))) 			//5470~5725 MHZ
    //{
    //   Adapter->ZD80211HSetting.TPCEnable=TRUE;
    //}
    //else
    //{
    //   Adapter->ZD80211HSetting.TPCEnable=FALSE;
    //}

    wd->sta.TPCEnable = mode;
    if (mode)
        wd->sta.capability[1] |= ZM_BIT_0;
    else
        wd->sta.capability[1] &= (~ZM_BIT_0);

    return 0;
}

u8_t zfiWlanSetAniMode(zdev_t* dev, u8_t mode)
{
    zmw_get_wlan_dev(dev);

    wd->aniEnable = mode;
    if (mode)
        zfHpAniAttach(dev);

    return 0;
}

#ifdef ZM_OS_LINUX_FUNC
void zfiWlanShowTally(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    zm_msg1_mm(ZM_LV_0, "Hw_UnderrunCnt    = ", wd->commTally.Hw_UnderrunCnt);
    zm_msg1_mm(ZM_LV_0, "Hw_TotalRxFrm     = ", wd->commTally.Hw_TotalRxFrm);
    zm_msg1_mm(ZM_LV_0, "Hw_CRC32Cnt       = ", wd->commTally.Hw_CRC32Cnt);
    zm_msg1_mm(ZM_LV_0, "Hw_CRC16Cnt       = ", wd->commTally.Hw_CRC16Cnt);
    zm_msg1_mm(ZM_LV_1, "Hw_DecrypErr_UNI  = ", wd->commTally.Hw_DecrypErr_UNI);
    zm_msg1_mm(ZM_LV_0, "Hw_RxFIFOOverrun  = ", wd->commTally.Hw_RxFIFOOverrun);
    zm_msg1_mm(ZM_LV_1, "Hw_DecrypErr_Mul  = ", wd->commTally.Hw_DecrypErr_Mul);
    zm_msg1_mm(ZM_LV_1, "Hw_RetryCnt       = ", wd->commTally.Hw_RetryCnt);
    zm_msg1_mm(ZM_LV_0, "Hw_TotalTxFrm     = ", wd->commTally.Hw_TotalTxFrm);
    zm_msg1_mm(ZM_LV_0, "Hw_RxTimeOut      = ", wd->commTally.Hw_RxTimeOut);
    zm_msg1_mm(ZM_LV_0, "Tx_MPDU           = ", wd->commTally.Tx_MPDU);
    zm_msg1_mm(ZM_LV_0, "BA_Fail           = ", wd->commTally.BA_Fail);
    zm_msg1_mm(ZM_LV_0, "Hw_Tx_AMPDU       = ", wd->commTally.Hw_Tx_AMPDU);
    zm_msg1_mm(ZM_LV_0, "Hw_Tx_MPDU        = ", wd->commTally.Hw_Tx_MPDU);

    zm_msg1_mm(ZM_LV_1, "Hw_RxMPDU          = ", wd->commTally.Hw_RxMPDU);
    zm_msg1_mm(ZM_LV_1, "Hw_RxDropMPDU      = ", wd->commTally.Hw_RxDropMPDU);
    zm_msg1_mm(ZM_LV_1, "Hw_RxDelMPDU       = ", wd->commTally.Hw_RxDelMPDU);
    zm_msg1_mm(ZM_LV_1, "Hw_RxPhyMiscError  = ", wd->commTally.Hw_RxPhyMiscError);
    zm_msg1_mm(ZM_LV_1, "Hw_RxPhyXRError    = ", wd->commTally.Hw_RxPhyXRError);
    zm_msg1_mm(ZM_LV_1, "Hw_RxPhyOFDMError  = ", wd->commTally.Hw_RxPhyOFDMError);
    zm_msg1_mm(ZM_LV_1, "Hw_RxPhyCCKError   = ", wd->commTally.Hw_RxPhyCCKError);
    zm_msg1_mm(ZM_LV_1, "Hw_RxPhyHTError    = ", wd->commTally.Hw_RxPhyHTError);
    zm_msg1_mm(ZM_LV_1, "Hw_RxPhyTotalCount = ", wd->commTally.Hw_RxPhyTotalCount);

    if (!((wd->commTally.Tx_MPDU == 0) && (wd->commTally.BA_Fail == 0)))
    {
        zm_debug_msg_p("BA Fail Ratio(%)  = ", wd->commTally.BA_Fail * 100,
                (wd->commTally.BA_Fail + wd->commTally.Tx_MPDU));
    }

    if (!((wd->commTally.Hw_Tx_MPDU == 0) && (wd->commTally.Hw_Tx_AMPDU == 0)))
    {
        zm_debug_msg_p("Avg Agg Number    = ",
                wd->commTally.Hw_Tx_MPDU, wd->commTally.Hw_Tx_AMPDU);
    }
}
#endif

void zfiWlanSetMaxTxPower(zdev_t* dev, u8_t power2, u8_t power5)
{
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    wd->maxTxPower2 = power2;
    wd->maxTxPower5 = power5;
    zmw_leave_critical_section(dev);
}

void zfiWlanQueryMaxTxPower(zdev_t* dev, u8_t *power2, u8_t *power5)
{
    zmw_get_wlan_dev(dev);

    *power2 = wd->maxTxPower2;
    *power5 = wd->maxTxPower5;
}

void zfiWlanSetConnectMode(zdev_t* dev, u8_t mode)
{
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    wd->connectMode = mode;
    zmw_leave_critical_section(dev);
}

void zfiWlanSetSupportMode(zdev_t* dev, u32_t mode)
{
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    wd->supportMode = mode;
    zmw_leave_critical_section(dev);
}

void zfiWlanSetAdhocMode(zdev_t* dev, u32_t mode)
{
    zmw_get_wlan_dev(dev);

    wd->ws.adhocMode = mode;
}

u32_t zfiWlanQueryAdhocMode(zdev_t* dev, u8_t bWrapper)
{
    u32_t adhocMode;

    zmw_get_wlan_dev(dev);

    if ( bWrapper )
    {
        adhocMode = wd->ws.adhocMode;
    }
    else
    {
        adhocMode = wd->wfc.bIbssGMode;
    }

    return adhocMode;
}


u8_t zfiWlanSetCountryIsoName(zdev_t* dev, u8_t *countryIsoName, u8_t length)
{
    u8_t buf[5];
    zmw_get_wlan_dev(dev);

    if (length == 4)
    {
        buf[2] = wd->ws.countryIsoName[0] = countryIsoName[2];
        buf[3] = wd->ws.countryIsoName[1] = countryIsoName[1];
        buf[4] = wd->ws.countryIsoName[2] = countryIsoName[0];
    }
    else if (length == 3)
    {
        buf[2] = wd->ws.countryIsoName[0] = countryIsoName[1];
        buf[3] = wd->ws.countryIsoName[1] = countryIsoName[0];
        buf[4] = wd->ws.countryIsoName[2] = '\0';
    }
    else
    {
        return 1;
    }

    return zfHpGetRegulationTablefromISO(dev, buf, length);
}


const char* zfiWlanQueryCountryIsoName(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    return wd->ws.countryIsoName;
}



void zfiWlanSetRegulatory(zdev_t* dev, u8_t CCS, u16_t Code, u8_t bfirstChannel)
{
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    if (CCS)
    {
        /* Reset Regulation Table by Country Code */
        zfHpGetRegulationTablefromCountry(dev, Code);
    }
    else
    {
        /* Reset Regulation Table by Region Code */
        zfHpGetRegulationTablefromRegionCode(dev, Code);
    }

    if (bfirstChannel) {
        zmw_enter_critical_section(dev);
        wd->frequency = zfChGetFirstChannel(dev, NULL);
        zmw_leave_critical_section(dev);
        zfCoreSetFrequency(dev, wd->frequency);
    }
}


const char* zfiHpGetisoNamefromregionCode(zdev_t* dev, u16_t regionCode)
{
    return zfHpGetisoNamefromregionCode(dev, regionCode);
}

u16_t zfiWlanChannelToFrequency(zdev_t* dev, u8_t channel)
{
    return zfChNumToFreq(dev, channel, 0);
}

u8_t zfiWlanFrequencyToChannel(zdev_t* dev, u16_t freq)
{
    u8_t is5GBand = 0;

    return zfChFreqToNum(freq, &is5GBand);
}

void zfiWlanDisableDfsChannel(zdev_t* dev, u8_t disableFlag)
{
    zfHpDisableDfsChannel(dev, disableFlag);
    return;
}

void zfiWlanSetLEDCtrlParam(zdev_t* dev, u8_t type, u8_t flag)
{
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    wd->ledStruct.LEDCtrlType = type;
    wd->ledStruct.LEDCtrlFlagFromReg  = flag;
    zmw_leave_critical_section(dev);
}

void zfiWlanEnableLeapConfig(zdev_t* dev, u8_t leapEnabled)
{
    zmw_get_wlan_dev(dev);

    wd->sta.leapEnabled = leapEnabled;
}

u32_t zfiWlanQueryHwCapability(zdev_t* dev)
{
    return zfHpCapability(dev);
}

u32_t zfiWlanQueryReceivedPacket(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    return wd->sta.ReceivedPktRatePerSecond;
}

void zfiWlanCheckSWEncryption(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    if (wd->sta.SWEncryptEnable != 0)
    {
        zfHpSWDecrypt(dev, 1);
    }
}

u16_t zfiWlanQueryAllowChannels(zdev_t* dev, u16_t *channels)
{
    u16_t ii;
    zmw_get_wlan_dev(dev);

    for (ii = 0; ii < wd->regulationTable.allowChannelCnt; ii++)
    {
        channels[ii] = wd->regulationTable.allowChannel[ii].channel;
    }

    return wd->regulationTable.allowChannelCnt;
}

void zfiWlanSetDynamicSIFSParam(zdev_t* dev, u8_t val)
{
    zmw_get_wlan_dev(dev);

    wd->dynamicSIFSEnable = val;

    zm_debug_msg1("wd->dynamicSIFSEnable = ", wd->dynamicSIFSEnable)
}

u16_t zfiWlanGetMulticastAddressCount(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    return wd->sta.multicastList.size;
}

void zfiWlanGetMulticastList(zdev_t* dev, u8_t* pMCList)
{
    struct zsMulticastAddr* pMacList = (struct zsMulticastAddr*) pMCList;
    u8_t i;

    zmw_get_wlan_dev(dev);

    for ( i=0; i<wd->sta.multicastList.size; i++ )
    {
        zfMemoryCopy(pMacList[i].addr, wd->sta.multicastList.macAddr[i].addr, 6);
    }
}

void zfiWlanSetPacketFilter(zdev_t* dev, u32_t PacketFilter)
{
    u8_t  bAllMulticast = 0;
    u32_t oldFilter;

    zmw_get_wlan_dev(dev);

    oldFilter = wd->sta.osRxFilter;

    wd->sta.osRxFilter = PacketFilter;

    if ((oldFilter & ZM_PACKET_TYPE_ALL_MULTICAST) !=
        (wd->sta.osRxFilter & ZM_PACKET_TYPE_ALL_MULTICAST))
    {
        if ( wd->sta.osRxFilter & ZM_PACKET_TYPE_ALL_MULTICAST )
            bAllMulticast = 1;
        zfHpSetMulticastList(dev, wd->sta.multicastList.size,
                             (u8_t*)wd->sta.multicastList.macAddr, bAllMulticast);
    }
}

u8_t zfiCompareWithMulticastListAddress(zdev_t* dev, u16_t* dstMacAddr)
{
    u8_t i;
    u8_t bIsInMCListAddr = 0;

    zmw_get_wlan_dev(dev);

    for ( i=0; i<wd->sta.multicastList.size; i++ )
    {
    	if ( zfwMemoryIsEqual((u8_t*)dstMacAddr, (u8_t*)wd->sta.multicastList.macAddr[i].addr, 6) )
    	{
            bIsInMCListAddr = 1;
            break;
    	}
    }

    return bIsInMCListAddr;
}

void zfiWlanSetSafeModeEnabled(zdev_t* dev, u8_t safeMode)
{
    zmw_get_wlan_dev(dev);

    wd->sta.bSafeMode = safeMode;

    if ( safeMode )
    	zfStaEnableSWEncryption(dev, 1);
    else
        zfStaDisableSWEncryption(dev);
}

void zfiWlanSetIBSSAdditionalIELength(zdev_t* dev, u32_t ibssAdditionalIESize, u8_t* ibssAdditionalIE)
{
	zmw_get_wlan_dev(dev);

	if ( ibssAdditionalIESize )
    {
	    wd->sta.ibssAdditionalIESize = ibssAdditionalIESize;
        zfMemoryCopy(wd->sta.ibssAdditionalIE, ibssAdditionalIE, (u16_t)ibssAdditionalIESize);
    }
    else
    	wd->sta.ibssAdditionalIESize = 0;
}
