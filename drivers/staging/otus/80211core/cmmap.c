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
/*  Module Name : mm.c                                                  */
/*                                                                      */
/*  Abstract                                                            */
/*      This module contains common functions for handle AP             */
/*      management frame.                                               */
/*                                                                      */
/*  NOTES                                                               */
/*      None                                                            */
/*                                                                      */
/************************************************************************/
#include "cprecomp.h"
#include "ratectrl.h"

extern const u8_t zcUpToAc[];

void zfMmApTimeTick(zdev_t* dev)
{
    u32_t now;
    zmw_get_wlan_dev(dev);

    //zm_debug_msg1("wd->wlanMode : ", wd->wlanMode);
    if (wd->wlanMode == ZM_MODE_AP)
    {
        /* => every 1.28 seconds */
        /* AP : aging STA that does not active for wd->ap.staAgingTime    */
        now = wd->tick & 0x7f;
        if (now == 0x0)
        {
            zfApAgingSta(dev);
        }
        else if (now == 0x1f)
        {
            zfQueueAge(dev, wd->ap.uapsdQ, wd->tick, 10000);
        }
        /* AP : check (wd->ap.protectedObss) and (wd->ap.bStaAssociated)  */
        /*      to enable NonErp and Protection mode                      */
        else if (now == 0x3f)
        {
            //zfApProtctionMonitor(dev);
        }
    }
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfApInitStaTbl              */
/*      Init AP's station table.                                        */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      None                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2005.10     */
/*                                                                      */
/************************************************************************/
void zfApInitStaTbl(zdev_t* dev)
{
    u16_t i;

    zmw_get_wlan_dev(dev);

    for (i=0; i<ZM_MAX_STA_SUPPORT; i++)
    {
        wd->ap.staTable[i].valid = 0;
        wd->ap.staTable[i].state = 0;
        wd->ap.staTable[i].addr[0] = 0;
        wd->ap.staTable[i].addr[1] = 0;
        wd->ap.staTable[i].addr[2] = 0;
        wd->ap.staTable[i].time = 0;
        wd->ap.staTable[i].vap = 0;
        wd->ap.staTable[i].encryMode = ZM_NO_WEP;
    }
    return;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfApFindSta                 */
/*      Find a STA in station table.                                    */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      addr : Target STA address                                       */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      0xffff : fail                                                   */
/*      other : STA table index                                         */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2005.10     */
/*                                                                      */
/************************************************************************/
u16_t zfApFindSta(zdev_t* dev, u16_t* addr)
{
    u16_t i;

    zmw_get_wlan_dev(dev);

    for (i=0; i<ZM_MAX_STA_SUPPORT; i++)
    {
        if (wd->ap.staTable[i].valid == 1)
        {
            if ((wd->ap.staTable[i].addr[0] == addr[0])
                    && (wd->ap.staTable[i].addr[1] == addr[1])
                    && (wd->ap.staTable[i].addr[2] == addr[2]))
            {
                return i;
            }
        }
    }
    return 0xffff;
}

u16_t zfApGetSTAInfo(zdev_t* dev, u16_t* addr, u16_t* state, u8_t* vap)
{
    u16_t id;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    if ((id = zfApFindSta(dev, addr)) != 0xffff)
    {
        *vap = wd->ap.staTable[id].vap;
        *state = wd->ap.staTable[id++].state;
    }

    zmw_leave_critical_section(dev);

    return id;
}


void zfApGetStaQosType(zdev_t* dev, u16_t* addr, u8_t* qosType)
{
    u16_t id;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    if ((id = zfApFindSta(dev, addr)) != 0xffff)
    {
        *qosType = wd->ap.staTable[id].qosType;
    }
    else
    {
        *qosType = 0;
    }

    zmw_leave_critical_section(dev);

    return;
}

void zfApGetStaTxRateAndQosType(zdev_t* dev, u16_t* addr, u32_t* phyCtrl,
                                u8_t* qosType, u16_t* rcProbingFlag)
{
    u16_t id;
    u8_t rate;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    if ((id = zfApFindSta(dev, addr)) != 0xffff)
    {
        rate = (u8_t)zfRateCtrlGetTxRate(dev, &wd->ap.staTable[id].rcCell, rcProbingFlag);
#ifdef ZM_AP_DEBUG
        //rate = 15;
#endif
        *phyCtrl = zcRateToPhyCtrl[rate];
        *qosType = wd->ap.staTable[id].qosType;
    }
    else
    {
        if (wd->frequency < 3000)
        {
            /* CCK 1M */
            //header[2] = 0x0f00;          //PHY control L
            //header[3] = 0x0000;          //PHY control H
            *phyCtrl = 0x00000F00;
        }
        else
        {
            /* CCK 6M */
            //header[2] = 0x0f01;          //PHY control L
            //header[3] = 0x000B;          //PHY control H
            *phyCtrl = 0x000B0F01;
        }
        *qosType = 0;
    }

    zmw_leave_critical_section(dev);

    zm_msg2_mm(ZM_LV_3, "PhyCtrl=", *phyCtrl);
    return;
}

void zfApGetStaEncryType(zdev_t* dev, u16_t* addr, u8_t* encryType)
{
    //struct zsWlanDev* wd = (struct zsWlanDev*) zmw_wlan_dev(dev);
    u16_t id;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    if ((id = zfApFindSta(dev, addr)) != 0xffff)
    {
        *encryType = wd->ap.staTable[id].encryMode;
    }
    else
    {
        *encryType = ZM_NO_WEP;
    }

    zmw_leave_critical_section(dev);

    zm_msg2_mm(ZM_LV_3, "encyrType=", *encryType);
    return;
}

void zfApGetStaWpaIv(zdev_t* dev, u16_t* addr, u16_t* iv16, u32_t* iv32)
{
    //struct zsWlanDev* wd = (struct zsWlanDev*) zmw_wlan_dev(dev);
    u16_t id;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    if ((id = zfApFindSta(dev, addr)) != 0xffff)
    {
        *iv16 = wd->ap.staTable[id].iv16;
        *iv32 = wd->ap.staTable[id].iv32;
    }
    else
    {
        *iv16 = 0;
        *iv32 = 0;
    }

    zmw_leave_critical_section(dev);

    zm_msg2_mm(ZM_LV_3, "iv16=", *iv16);
    zm_msg2_mm(ZM_LV_3, "iv32=", *iv32);
    return;
}

void zfApSetStaWpaIv(zdev_t* dev, u16_t* addr, u16_t iv16, u32_t iv32)
{
    //struct zsWlanDev* wd = (struct zsWlanDev*) zmw_wlan_dev(dev);
    u16_t id;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    if ((id = zfApFindSta(dev, addr)) != 0xffff)
    {
        wd->ap.staTable[id].iv16 = iv16;
        wd->ap.staTable[id].iv32 = iv32;
    }

    zmw_leave_critical_section(dev);

    zm_msg2_mm(ZM_LV_3, "iv16=", iv16);
    zm_msg2_mm(ZM_LV_3, "iv32=", iv32);
    return;
}

void zfApClearStaKey(zdev_t* dev, u16_t* addr)
{
    //struct zsWlanDev* wd = (struct zsWlanDev*) zmw_wlan_dev(dev);
    u16_t bcAddr[3] = { 0xffff, 0xffff, 0xffff };
    u16_t id;

    zmw_get_wlan_dev(dev);

    if (zfMemoryIsEqual((u8_t*)bcAddr, (u8_t*)addr, sizeof(bcAddr)) == TRUE)
    {
        /* Turn off group key information */
    //    zfClearKey(dev, 0);
    }
    else
    {
        zmw_declare_for_critical_section();

        zmw_enter_critical_section(dev);

        if ((id = zfApFindSta(dev, addr)) != 0xffff)
        {
            /* Turn off STA's key information */
            zfHpRemoveKey(dev, id+1);

            /* Update STA's Encryption Type */
            wd->ap.staTable[id].encryMode = ZM_NO_WEP;
        }
        else
        {
            zm_msg0_mm(ZM_LV_3, "Can't find STA address\n");
        }
        zmw_leave_critical_section(dev);
    }
}

#ifdef ZM_ENABLE_CENC
void zfApGetStaCencIvAndKeyIdx(zdev_t* dev, u16_t* addr, u32_t *iv, u8_t *keyIdx)
{
    //struct zsWlanDev* wd = (struct zsWlanDev*) zmw_wlan_dev(dev);
    u16_t id;
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();


    zmw_enter_critical_section(dev);

    if ((id = zfApFindSta(dev, addr)) != 0xffff)
    {
        *iv++ = wd->ap.staTable[id].txiv[0];
        *iv++ = wd->ap.staTable[id].txiv[1];
        *iv++ = wd->ap.staTable[id].txiv[2];
        *iv = wd->ap.staTable[id].txiv[3];
        *keyIdx = wd->ap.staTable[id].cencKeyIdx;
    }
    else
    {
        *iv++ = 0x5c365c37;
        *iv++ = 0x5c365c36;
        *iv++ = 0x5c365c36;
        *iv = 0x5c365c36;
        *keyIdx = 0;
    }

    zmw_leave_critical_section(dev);
    return;
}

void zfApSetStaCencIv(zdev_t* dev, u16_t* addr, u32_t *iv)
{
    //struct zsWlanDev* wd = (struct zsWlanDev*) zmw_wlan_dev(dev);
    u16_t id;
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();


    zmw_enter_critical_section(dev);

    if ((id = zfApFindSta(dev, addr)) != 0xffff)
    {
        wd->ap.staTable[id].txiv[0] = *iv++;
        wd->ap.staTable[id].txiv[1] = *iv++;
        wd->ap.staTable[id].txiv[2] = *iv++;
        wd->ap.staTable[id].txiv[3] = *iv;
    }

    zmw_leave_critical_section(dev);

    return;
}
#endif //ZM_ENABLE_CENC


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfApFlushBufferedPsFrame    */
/*      Free buffered PS frames.                                        */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      None                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2007.1      */
/*                                                                      */
/************************************************************************/
void zfApFlushBufferedPsFrame(zdev_t* dev)
{
    u16_t emptyFlag;
    u16_t freeCount;
    u16_t vap;
    zbuf_t* psBuf = NULL;
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    freeCount = 0;
    emptyFlag = 0;
    while (1)
    {
        psBuf = NULL;
        zmw_enter_critical_section(dev);
        if (wd->ap.uniHead != wd->ap.uniTail)
        {
            psBuf = wd->ap.uniArray[wd->ap.uniHead];
            wd->ap.uniHead = (wd->ap.uniHead + 1) & (ZM_UNI_ARRAY_SIZE - 1);
        }
        else
        {
            emptyFlag = 1;
        }
        zmw_leave_critical_section(dev);

        if (psBuf != NULL)
        {
            zfwBufFree(dev, psBuf, ZM_ERR_FLUSH_PS_QUEUE);
        }
        zm_assert(freeCount++ < (ZM_UNI_ARRAY_SIZE*2));

        if (emptyFlag != 0)
        {
            break;
        }
    }

    for (vap=0; vap<ZM_MAX_AP_SUPPORT; vap++)
    {
        freeCount = 0;
        emptyFlag = 0;
        while (1)
        {
            psBuf = NULL;
            zmw_enter_critical_section(dev);
            if (wd->ap.bcmcHead[vap] != wd->ap.bcmcTail[vap])
            {
                psBuf = wd->ap.bcmcArray[vap][wd->ap.bcmcHead[vap]];
                wd->ap.bcmcHead[vap] = (wd->ap.bcmcHead[vap] + 1)
                        & (ZM_BCMC_ARRAY_SIZE - 1);
            }
            else
            {
                emptyFlag = 1;
            }
            zmw_leave_critical_section(dev);

            if (psBuf != NULL)
            {
                zfwBufFree(dev, psBuf, ZM_ERR_FLUSH_PS_QUEUE);
            }
            zm_assert(freeCount++ < (ZM_BCMC_ARRAY_SIZE*2));

            if (emptyFlag != 0)
            {
                break;
            }
        }
    }
    return;
}


u16_t zfApBufferPsFrame(zdev_t* dev, zbuf_t* buf, u16_t port)
{
    u16_t id;
    u16_t addr[3];
    u16_t vap = 0;
    u8_t up;
    u16_t fragOff;
    u8_t ac;
    u16_t ret;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    if (port < ZM_MAX_AP_SUPPORT)
    {
        vap = port;
    }

    addr[0] = zmw_rx_buf_readh(dev, buf, 0);
    addr[1] = zmw_rx_buf_readh(dev, buf, 2);
    addr[2] = zmw_rx_buf_readh(dev, buf, 4);

    if ((addr[0] & 0x1) == 0x1)
    {
        if (wd->ap.staPowerSaving > 0)
        {
            zmw_enter_critical_section(dev);

            /* Buffer this BC or MC frame */
            if (((wd->ap.bcmcTail[vap]+1)&(ZM_BCMC_ARRAY_SIZE-1))
                    != wd->ap.bcmcHead[vap])
            {
                wd->ap.bcmcArray[vap][wd->ap.bcmcTail[vap]++] = buf;
                wd->ap.bcmcTail[vap] &= (ZM_BCMC_ARRAY_SIZE-1);
                zmw_leave_critical_section(dev);

                zm_msg0_tx(ZM_LV_0, "Buffer BCMC");
            }
            else
            {
                /* bcmcArray full */
                zmw_leave_critical_section(dev);

                zm_msg0_tx(ZM_LV_0, "BCMC buffer full");

                /* free buffer according to buffer type */
                zfwBufFree(dev, buf, ZM_ERR_BCMC_PS_BUFFER_UNAVAILABLE);
            }
            return 1;
        }
    }
    else
    {
        zmw_enter_critical_section(dev);

        if ((id = zfApFindSta(dev, addr)) != 0xffff)
        {
            if (wd->ap.staTable[id].psMode == 1)
            {

                zfTxGetIpTosAndFrag(dev, buf, &up, &fragOff);
                ac = zcUpToAc[up&0x7] & 0x3;

                if ((wd->ap.staTable[id].qosType == 1) &&
                        ((wd->ap.staTable[id].qosInfo & (0x8>>ac)) != 0))
                {
                    ret = zfQueuePutNcs(dev, wd->ap.uapsdQ, buf, wd->tick);
                    zmw_leave_critical_section(dev);
                    if (ret != ZM_SUCCESS)
                    {
                        zfwBufFree(dev, buf, ZM_ERR_AP_UAPSD_QUEUE_FULL);
                    }
                }
                else
                {
                /* Buffer this unicast frame */
                if (((wd->ap.uniTail+1)&(ZM_UNI_ARRAY_SIZE-1))
                        != wd->ap.uniHead)
                {
                    wd->ap.uniArray[wd->ap.uniTail++] = buf;
                    wd->ap.uniTail &= (ZM_UNI_ARRAY_SIZE-1);
                    zmw_leave_critical_section(dev);
                    zm_msg0_tx(ZM_LV_0, "Buffer UNI");

                }
                else
                {
                    /* uniArray full */
                    zmw_leave_critical_section(dev);
                    zm_msg0_tx(ZM_LV_0, "UNI buffer full");
                    /* free buffer according to buffer type */
                    zfwBufFree(dev, buf, ZM_ERR_UNI_PS_BUFFER_UNAVAILABLE);
                }
                }
                return 1;
            } /* if (wd->ap.staTable[id++].psMode == 1) */
        } /* if ((id = zfApFindSta(dev, addr)) != 0xffff) */
        zmw_leave_critical_section(dev);
    }

    return 0;
}

u16_t zfApGetSTAInfoAndUpdatePs(zdev_t* dev, u16_t* addr, u16_t* state,
                                u8_t* vap, u16_t psMode, u8_t* uapsdTrig)
{
    u16_t id;
    u8_t uapsdStaAwake = 0;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

#ifdef ZM_AP_DEBUG
    //psMode=0;
#endif

    if ((id = zfApFindSta(dev, addr)) != 0xffff)
    {
        if (psMode != 0)
        {
            zm_msg0_mm(ZM_LV_0, "psMode = 1");
            if (wd->ap.staTable[id].psMode == 0)
            {
                wd->ap.staPowerSaving++;
            }
            else
            {
                if (wd->ap.staTable[id].qosType == 1)
                {
                    zm_msg0_mm(ZM_LV_0, "UAPSD trigger");
                    *uapsdTrig = wd->ap.staTable[id].qosInfo;
                }
            }
        }
        else
        {
            if (wd->ap.staTable[id].psMode != 0)
            {
                wd->ap.staPowerSaving--;
                if ((wd->ap.staTable[id].qosType == 1) && ((wd->ap.staTable[id].qosInfo&0xf)!=0))
                {
                    uapsdStaAwake = 1;
                }
            }
        }

        wd->ap.staTable[id].psMode = (u8_t) psMode;
        wd->ap.staTable[id].time = wd->tick;
        *vap = wd->ap.staTable[id].vap;
        *state = wd->ap.staTable[id++].state;
    }

    zmw_leave_critical_section(dev);

    if (uapsdStaAwake == 1)
    {
        zbuf_t* psBuf;
        u8_t mb;

        while (1)
        {
            if ((psBuf = zfQueueGetWithMac(dev, wd->ap.uapsdQ, (u8_t*)addr, &mb)) != NULL)
            {
                zfTxSendEth(dev, psBuf, 0, ZM_EXTERNAL_ALLOC_BUF, 0);
            }
            else
            {
                break;
            }
        }
    }

    return id;
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfApGetNewSta               */
/*      Get a new STA from station table.                               */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      0xffff : fail                                                   */
/*      other : STA table index                                         */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2005.10     */
/*                                                                      */
/************************************************************************/
u16_t zfApGetNewSta(zdev_t* dev)
{
    u16_t i;

    zmw_get_wlan_dev(dev);

    for (i=0; i<ZM_MAX_STA_SUPPORT; i++)
    {
        if (wd->ap.staTable[i].valid == 0)
        {
            zm_msg2_mm(ZM_LV_0, "zfApGetNewSta=", i);
            return i;
        }
    }
    return 0xffff;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfApAddSta                  */
/*      Add a STA to station table.                                     */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      addr : STA MAC address                                          */
/*      state : STA state                                               */
/*      apId : Virtual AP ID                                            */
/*      type : 0=>11b, 1=>11g                                           */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      0xffff : fail                                                   */
/*      Other : index                                                   */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2005.10     */
/*                                                                      */
/************************************************************************/
u16_t zfApAddSta(zdev_t* dev, u16_t* addr, u16_t state, u16_t apId, u8_t type,
                 u8_t qosType, u8_t qosInfo)
{
    u16_t index;
    u16_t i;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zm_msg1_mm(ZM_LV_0, "STA type=", type);

    zmw_enter_critical_section(dev);

    if ((index = zfApFindSta(dev, addr)) != 0xffff)
    {
        zm_msg0_mm(ZM_LV_2, "found");
        /* Update STA state */
        if ((state == ZM_STATE_AUTH) || (state == ZM_STATE_PREAUTH))
        {
            wd->ap.staTable[index].state = state;
            wd->ap.staTable[index].time = wd->tick;
            wd->ap.staTable[index].vap = (u8_t)apId;
        }
        else if (state == ZM_STATE_ASOC)
        {
            if ((wd->ap.staTable[index].state == ZM_STATE_AUTH))
                    //&& (wd->ap.staTable[index].vap == apId))
            {
                wd->ap.staTable[index].state = state;
                wd->ap.staTable[index].time = wd->tick;
                wd->ap.staTable[index].qosType = qosType;
                wd->ap.staTable[index].vap = (u8_t)apId;
                wd->ap.staTable[index].staType = type;
                wd->ap.staTable[index].qosInfo = qosInfo;

                if (wd->frequency < 3000)
                {
                    /* Init 11b/g */
                    zfRateCtrlInitCell(dev, &wd->ap.staTable[index].rcCell, type, 1, 1);
                }
                else
                {
                    /* Init 11a */
                    zfRateCtrlInitCell(dev, &wd->ap.staTable[index].rcCell, type, 0, 1);
                }

                if (wd->zfcbApConnectNotify != NULL)
                {
                    wd->zfcbApConnectNotify(dev, (u8_t*)addr, apId);
                }
            }
            else
            {
                index = 0xffff;
            }
        }
    }
    else
    {
        zm_msg0_mm(ZM_LV_2, "Not found");
        if ((state == ZM_STATE_AUTH) || (state == ZM_STATE_PREAUTH))
        {
            /* Get a new STA and update state */
            index = zfApGetNewSta(dev);
            zm_msg2_mm(ZM_LV_1, "new STA index=", index);

            if (index != 0xffff)
            {
                for (i=0; i<3; i++)
                {
                    wd->ap.staTable[index].addr[i] = addr[i];
                }
                wd->ap.staTable[index].state = state;
                wd->ap.staTable[index].valid = 1;
                wd->ap.staTable[index].time = wd->tick;
                wd->ap.staTable[index].vap = (u8_t)apId;
                wd->ap.staTable[index].encryMode = ZM_NO_WEP;
            }
        }
    }

    zmw_leave_critical_section(dev);

    return index;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfApAgingSta                */
/*      Aging STA in station table.                                     */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      number of 11b STA in STA table                                  */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2005.10     */
/*                                                                      */
/************************************************************************/
void zfApAgingSta(zdev_t* dev)
{
    u16_t i;
    u32_t deltaMs;
    u16_t addr[3];
    u16_t txFlag;
    u16_t psStaCount = 0;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    wd->ap.gStaAssociated = wd->ap.bStaAssociated = 0;

    for (i=0; i<ZM_MAX_STA_SUPPORT; i++)
    {
        txFlag = 0;
        zmw_enter_critical_section(dev);
        if (wd->ap.staTable[i].valid == 1)
        {
            addr[0] = wd->ap.staTable[i].addr[0];
            addr[1] = wd->ap.staTable[i].addr[1];
            addr[2] = wd->ap.staTable[i].addr[2];
            /* millisecond */
            deltaMs = (u32_t)((u32_t)wd->tick-(u32_t)wd->ap.staTable[i].time)
                      * ZM_MS_PER_TICK;

            /* preauth */
            if ((wd->ap.staTable[i].state == ZM_STATE_PREAUTH)
                    && (deltaMs > ZM_PREAUTH_TIMEOUT_MS))
            {
                /* Aging STA */
                wd->ap.staTable[i].valid = 0;
                wd->ap.authSharing = 0;
                txFlag = 1;
            }

            /* auth */
            if ((wd->ap.staTable[i].state == ZM_STATE_AUTH)
                    && (deltaMs > ZM_AUTH_TIMEOUT_MS))
            {
                /* Aging STA */
                wd->ap.staTable[i].valid = 0;
                txFlag = 1;
            }

            /* asoc */
            if (wd->ap.staTable[i].state == ZM_STATE_ASOC)
            {
                if (wd->ap.staTable[i].psMode != 0)
                {
                    psStaCount++;
                }

                if (deltaMs > ((u32_t)wd->ap.staAgingTimeSec<<10))
                {
                    /* Aging STA */
                    zm_msg1_mm(ZM_LV_0, "Age STA index=", i);
                    wd->ap.staTable[i].valid = 0;
                    txFlag = 1;
                }
                else if (deltaMs > ((u32_t)wd->ap.staProbingTimeSec<<10))
                {
                    if (wd->ap.staTable[i].psMode == 0)
                    {
                        /* Probing non-PS STA */
                        zm_msg1_mm(ZM_LV_0, "Probing STA index=", i);
                        wd->ap.staTable[i].time +=
                                (wd->ap.staProbingTimeSec * ZM_TICK_PER_SECOND);
                        txFlag = 2;
                    }
                }
            }


        }
        zmw_leave_critical_section(dev);

        if (txFlag == 1)
        {
            /* Send deauthentication management frame */
            zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_DEAUTH, addr, 4, 0, 0);
        }
        else if (txFlag == 2)
        {
            zfSendMmFrame(dev, ZM_WLAN_DATA_FRAME, addr, 0, 0, 0);
        }

    }

    wd->ap.staPowerSaving = psStaCount;

    return;
}

void zfApProtctionMonitor(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    /* 11b STA associated => nonErp, Protect */
    if (wd->ap.bStaAssociated > 0)
    {
        /* Enable NonErp bit in information element */
        wd->erpElement = ZM_WLAN_NON_ERP_PRESENT_BIT
                         | ZM_WLAN_USE_PROTECTION_BIT;

        /* Enable protection mode */
        zfApSetProtectionMode(dev, 1);

    }
    /* 11b STA not associated, protection OBSS present => Protect */
    else if (wd->ap.protectedObss > 2) //Threshold
    {
        if (wd->disableSelfCts == 0)
        {
            /* Disable NonErp bit in information element */
            wd->erpElement = ZM_WLAN_USE_PROTECTION_BIT;

            /* Enable protection mode */
            zfApSetProtectionMode(dev, 1);
        }
    }
    else
    {
        /* Disable NonErp bit in information element */
        wd->erpElement = 0;

        /* Disable protection mode */
        zfApSetProtectionMode(dev, 0);
    }
    wd->ap.protectedObss = 0;
}


void zfApProcessBeacon(zdev_t* dev, zbuf_t* buf)
{
    u16_t offset;
    u8_t ch;

    zmw_get_wlan_dev(dev);

    zm_msg0_mm(ZM_LV_3, "Rx beacon");

    /* update Non-ERP flag(wd->ap.nonErpObss) */
    if ((offset = zfFindElement(dev, buf, ZM_WLAN_EID_ERP)) == 0xffff)
    {
        /* 11b OBSS */
        wd->ap.protectedObss++;
        return;
    }

    ch = zmw_rx_buf_readb(dev, buf, offset+2);
    if ((ch & ZM_WLAN_USE_PROTECTION_BIT) == ZM_WLAN_USE_PROTECTION_BIT)
    {
        /* Protected OBSS */
        wd->ap.protectedObss = 1;
    }

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
void zfApProcessAuth(zdev_t* dev, zbuf_t* buf, u16_t* src, u16_t apId)
{
    u16_t algo, seq, status;
    u8_t authSharing;
    u16_t ret;
    u16_t i;
    u8_t challengePassed = 0;
    u8_t frameCtrl;
    u32_t retAlgoSeq;
    u32_t retStatus;
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();


    frameCtrl = zmw_rx_buf_readb(dev, buf, 1);
    /* AP : Auth share 3 */
    /* shift for WEP IV */
    if ((frameCtrl & 0x40) != 0)
    {
        algo = zmw_rx_buf_readh(dev, buf, 28);
        seq = zmw_rx_buf_readh(dev, buf, 30);
        status = zmw_rx_buf_readh(dev, buf, 32);
    }
    else
    {
        algo = zmw_rx_buf_readh(dev, buf, 24);
        seq = zmw_rx_buf_readh(dev, buf, 26);
        status = zmw_rx_buf_readh(dev, buf, 28);
    }

    zm_msg2_mm(ZM_LV_0, "Rx Auth, seq=", seq);

    /* Set default to authentication algorithm not support */
    retAlgoSeq = 0x20000 | algo;
    retStatus = 13; /* authentication algorithm not support */

    /* AP : Auth open 1 */
    if (algo == 0)
    {
        if (wd->ap.authAlgo[apId] == 0)
        {
            retAlgoSeq = 0x20000;
            if (seq == 1)
            {
                /* AP : update STA to auth */
                if ((ret = zfApAddSta(dev, src, ZM_STATE_AUTH, apId, 0, 0, 0)) != 0xffff)
                {
                    /* AP : call zfwAuthNotify() for host to judge */
                    //zfwAuthNotify(dev, src);

                    /* AP : response Auth seq=2, success */
                    retStatus = 0;

                }
                else
                {
                    /* AP : response Auth seq=2, unspecific error */
                    retStatus = 1;
                }
            }
            else
            {
                /* AP : response Auth seq=2, sequence number out of expected */
                retStatus = 14;
            }
        }
    }
    /* AP : Auth share 1 */
    else if (algo == 1)
    {
        if (wd->ap.authAlgo[apId] == 1)
        {
            if (seq == 1)
            {
                retAlgoSeq = 0x20001;

                /* critical section */
                zmw_enter_critical_section(dev);
                if (wd->ap.authSharing == 1)
                {
                    authSharing = 1;
                }
                else
                {
                    authSharing = 0;
                    wd->ap.authSharing = 1;
                }
                /* end of critical section */
                zmw_leave_critical_section(dev);

                if (authSharing == 1)
                {
                    /* AP : response Auth seq=2, status = fail */
                    retStatus = 1;
                }
                else
                {
                    /* AP : update STA to preauth */
                    zfApAddSta(dev, src, ZM_STATE_PREAUTH, apId, 0, 0, 0);

                    /* AP : call zfwAuthNotify() for host to judge */
                    //zfwAuthNotify(dev, src);

                    /* AP : response Auth seq=2 */
                    retStatus = 0;
                }
            }
            else if (seq == 3)
            {
                retAlgoSeq = 0x40001;

                if (wd->ap.authSharing == 1)
                {
                    /* check challenge text */
                    if (zmw_buf_readh(dev, buf, 30+4) == 0x8010)
                    {
                        for (i=0; i<128; i++)
                        {
                            if (wd->ap.challengeText[i]
                                        != zmw_buf_readb(dev, buf, 32+i+4))
                            {
                                break;
                            }
                        }
                        if (i == 128)
                        {
                            challengePassed = 1;
                        }
                    }

                    if (challengePassed == 1)
                    {
                        /* AP : update STA to auth */
                        zfApAddSta(dev, src, ZM_STATE_AUTH, apId, 0, 0, 0);

                        /* AP : response Auth seq=2 */
                        retStatus = 0;
                    }
                    else
                    {
                        /* AP : response Auth seq=2, challenge failure */
                        retStatus = 15;

                        /* TODO : delete STA */
                    }

                    wd->ap.authSharing = 0;
                }
            }
            else
            {
                retAlgoSeq = 0x40001;
                retStatus = 14;
            }
        }
    }

    zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_AUTH, src, retAlgoSeq,
            retStatus, apId);
    return;
}

void zfApProcessAsocReq(zdev_t* dev, zbuf_t* buf, u16_t* src, u16_t apId)
{
    u16_t aid = 0xffff;
    u8_t frameType;
    u16_t offset;
    u8_t staType = 0;
    u8_t qosType = 0;
    u8_t qosInfo = 0;
    u8_t tmp;
    u16_t i, j, k;
    u16_t encMode = 0;

    zmw_get_wlan_dev(dev);
    /* AP : check SSID */
    if ((offset = zfFindElement(dev, buf, ZM_WLAN_EID_SSID)) != 0xffff)
    {
        k = 0;
        for (j = 0; j < wd->ap.vapNumber; j++)
        {
            if ((tmp = zmw_buf_readb(dev, buf, offset+1))
                        != wd->ap.ssidLen[j])
            {
                k++;
            }
        }
        if (k == wd->ap.vapNumber)
        {
            goto zlDeauth;
        }

        k = 0;
        for (j = 0; j < wd->ap.vapNumber; j++)
        {
            for (i=0; i<wd->ap.ssidLen[j]; i++)
            {
                if ((tmp = zmw_buf_readb(dev, buf, offset+2+i))
                        != wd->ap.ssid[j][i])
                {
                    break;
                }
            }
            if (i == wd->ap.ssidLen[j])
            {
                apId = j;
            }
            else
            {
                k++;
            }
        }
        if (k == wd->ap.vapNumber)
        {
            goto zlDeauth;
        }
    }

    /* TODO : check capability */

    /* AP : check support rate */
    if ((offset = zfFindElement(dev, buf, ZM_WLAN_EID_EXTENDED_RATE)) != 0xffff)
    {
        /* 11g STA */
        staType = 1;
    }
    //CWYang(+)
    if ((offset = zfFindElement(dev, buf, ZM_WLAN_EID_HT_CAPABILITY)) != 0xffff)
    {
        /* 11n STA */
        staType = 2;
    }

    /* TODO : do not allow 11b STA to associated in Pure G mode */
    if (wd->ap.wlanType[apId] == ZM_WLAN_TYPE_PURE_G && staType == 0)
    {
        zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_DEAUTH, src, 3, 0, 0);
        return;
    }

    /* In pure B mode, we set G STA into B mode */
    if (wd->ap.wlanType[apId] == ZM_WLAN_TYPE_PURE_B && staType == 1)
    {
        staType = 0;
    }

    /* AP : check 11i and WPA */
    /* AP : check 11h */

    /* AP : check WME */
    if ((offset = zfFindWifiElement(dev, buf, 2, 0)) != 0xffff)
    {
        /* WME STA */
        qosType = 1;
        zm_msg0_mm(ZM_LV_0, "WME STA");

        if (wd->ap.uapsdEnabled != 0)
        {
            qosInfo = zmw_rx_buf_readb(dev, buf, offset+8);
        }
    }

    if (wd->ap.wpaSupport[apId] == 1)
    {
        if ( (offset = zfFindElement(dev, buf, ZM_WLAN_EID_WPA_IE)) != 0xffff )
        {
            /* get WPA IE */
            u8_t length = zmw_rx_buf_readb(dev, buf, offset+1);
            if (length+2 < ZM_MAX_WPAIE_SIZE)
            {
                zfCopyFromRxBuffer(dev, buf, wd->ap.stawpaIe[apId], offset, length+2);
                wd->ap.stawpaLen[apId] = length+2;
                encMode = 1;


                zm_msg1_mm(ZM_LV_0, "WPA Mode zfwAsocNotify, apId=", apId);

                /* AP : Call zfwAsocNotify() */
                if (wd->zfcbAsocNotify != NULL)
                {
                    wd->zfcbAsocNotify(dev, src, wd->ap.stawpaIe[apId], wd->ap.stawpaLen[apId], apId);
                }
            }
            else
            {
                goto zlDeauth;
            }
        }
        else if ( (offset = zfFindElement(dev, buf, ZM_WLAN_EID_RSN_IE)) != 0xffff )
        {
            /* get RSN IE */
            u8_t length = zmw_rx_buf_readb(dev, buf, offset+1);
            if (length+2 < ZM_MAX_WPAIE_SIZE)
            {
                zfCopyFromRxBuffer(dev, buf, wd->ap.stawpaIe[apId], offset, length+2);
                wd->ap.stawpaLen[apId] = length+2;
                encMode = 1;

                zm_msg1_mm(ZM_LV_0, "RSN Mode zfwAsocNotify, apId=", apId);

                /* AP : Call zfwAsocNotify() */
                if (wd->zfcbAsocNotify != NULL)
                {
                    wd->zfcbAsocNotify(dev, src, wd->ap.stawpaIe[apId], wd->ap.stawpaLen[apId], apId);
                }
            }
            else
            {
                goto zlDeauth;
            }
        }
#ifdef ZM_ENABLE_CENC
        else if ( (offset = zfFindElement(dev, buf, ZM_WLAN_EID_CENC_IE)) != 0xffff )
        {
            /* get CENC IE */
            u8_t length = zmw_rx_buf_readb(dev, buf, offset+1);

            if (length+2 < ZM_MAX_WPAIE_SIZE)
            {
                zfCopyFromRxBuffer(dev, buf, wd->ap.stawpaIe[apId], offset, length+2);
                wd->ap.stawpaLen[apId] = length+2;
                encMode = 1;

                zm_msg1_mm(ZM_LV_0, "CENC Mode zfwAsocNotify, apId=", apId);

                /* AP : Call zfwAsocNotify() */
                if (wd->zfcbCencAsocNotify != NULL)
                {
                    wd->zfcbCencAsocNotify(dev, src, wd->ap.stawpaIe[apId],
                            wd->ap.stawpaLen[apId], apId);
                }
            }
            else
            {
                goto zlDeauth;
            }
        }
#endif //ZM_ENABLE_CENC
        else
        {   /* ap is encryption but sta has no wpa/rsn ie */
            zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_DEAUTH, src, 6, 0, 0);
            return;
        }
    }
    /* sta has wpa/rsn ie but ap is no encryption */
    if ((wd->ap.wpaSupport[apId] == 0) && (encMode == 1))
    {
        zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_DEAUTH, src, 6, 0, 0);
        return;
    }

    /* AP : update STA to asoc */
    aid = zfApAddSta(dev, src, ZM_STATE_ASOC, apId, staType, qosType, qosInfo);

    zfApStoreAsocReqIe(dev, buf, aid);

zlDeauth:
    /* AP : send asoc rsp2 */
    if (aid != 0xffff)
    {
        frameType = zmw_rx_buf_readb(dev, buf, 0);

        if (frameType == ZM_WLAN_FRAME_TYPE_ASOCREQ)
        {
            zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_ASOCRSP, src, 0, aid+1, apId);
        }
        else
        {
            zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_REASOCRSP, src, 0, aid+1, apId);
        }
    }
    else
    {
        /* TODO : send deauthentication */
        zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_DEAUTH, src, 6, 0, 0);
    }

    return;
}

void zfApStoreAsocReqIe(zdev_t* dev, zbuf_t* buf, u16_t aid)
{
    //struct zsWlanAssoFrameHeader* pAssoFrame;
    //u8_t  pBuf[sizeof(struct zsWlanAssoFrameHeader)];
    u16_t offset;
    u32_t i;
    u16_t length;
    u8_t  *htcap;

    zmw_get_wlan_dev(dev);

    for (i=0; i<wd->sta.asocRspFrameBodySize; i++)
    {
        wd->sta.asocRspFrameBody[i] = zmw_rx_buf_readb(dev, buf, i+24);
    }
    /* capability: 2 octets */
    offset = 24;

    /* Listen interval: 2 octets */
    offset = 26;

    /* SSID */
    offset = 28;

    /* supported rates */
    if ((offset = zfFindElement(dev, buf, ZM_WLAN_EID_SUPPORT_RATE)) == 0xffff)
        return;
    length = zmw_rx_buf_readb(dev, buf, offset + 1);

    /* extended supported rates */
    if ((offset = zfFindElement(dev, buf, ZM_WLAN_EID_EXTENDED_RATE)) == 0xffff)
        return;
    length = zmw_rx_buf_readb(dev, buf, offset + 1);

    /* power capability:4 octets */
    offset = offset + 2 + length;

    /* supported channels: 4 octets */
    offset = offset + 2 + 4;

    /* RSN */

    /* QoS */

    /* HT capabilities: 28 octets */
    if ((offset = zfFindElement(dev, buf, ZM_WLAN_EID_HT_CAPABILITY)) != 0xffff) {
        /* atheros pre n */
        htcap = (u8_t *)&wd->ap.ie[aid].HtCap;
        htcap[0] = zmw_rx_buf_readb(dev, buf, offset);
        htcap[1] = 26;
        for (i=1; i<=26; i++)
        {
            htcap[i+1] = zmw_rx_buf_readb(dev, buf, offset + i);
            zm_debug_msg2("ASOC:  HT Capabilities, htcap=", htcap[i+1]);
        }
        return;
    }
    else if ((offset = zfFindElement(dev, buf, ZM_WLAN_PREN2_EID_HTCAPABILITY)) != 0xffff) {
        /* pre n 2.0 standard */
        htcap = (u8_t *)&wd->ap.ie[aid].HtCap;
        for (i=0; i<28; i++)
        {
            htcap[i] = zmw_rx_buf_readb(dev, buf, offset + i);
            zm_debug_msg2("ASOC:  HT Capabilities, htcap=", htcap[i]);
        }
    }
    else {
        /* not 11n AP */
        return;
    }


    /* supported regulatory classes */
    offset = offset + length;
    //length = zmw_rx_buf_readb(dev, buf, offset + 1);
    {
    u8_t *htcap;
    htcap = (u8_t *)&wd->sta.ie.HtInfo;
    //zm_debug_msg2("ASOC:  HT Capabilities info=", ((u16_t *)htcap)[1]);
    //zm_debug_msg2("ASOC:  A-MPDU parameters=", htcap[4]);
    //zm_debug_msg2("ASOC:  Supported MCS set=", ((u32_t *)htcap)[1]>>8);
    }

}

void zfApProcessAsocRsp(zdev_t* dev, zbuf_t* buf)
{

}

void zfApProcessDeauth(zdev_t* dev, zbuf_t* buf, u16_t* src, u16_t apId)
{
    u16_t aid;
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    /* AP : if SA=associated STA then deauthenticate STA */
    if ((aid = zfApFindSta(dev, src)) != 0xffff)
    {
        /* Clear STA table */
        wd->ap.staTable[aid].valid = 0;
        if (wd->zfcbDisAsocNotify != NULL)
        {
            wd->zfcbDisAsocNotify(dev, (u8_t*)src, apId);
        }
    }
    zmw_leave_critical_section(dev);

}

void zfApProcessDisasoc(zdev_t* dev, zbuf_t* buf, u16_t* src, u16_t apId)
{
    u16_t aid;
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    /* AP : if SA=associated STA then deauthenticate STA */
    if ((aid = zfApFindSta(dev, src)) != 0xffff)
    {
        /* Clear STA table */
        wd->ap.staTable[aid].valid = 0;
        zmw_leave_critical_section(dev);
        if (wd->zfcbDisAsocNotify != NULL)
        {
            wd->zfcbDisAsocNotify(dev, (u8_t*)src, apId);
        }
    }
    zmw_leave_critical_section(dev);

}


void zfApProcessProbeRsp(zdev_t* dev, zbuf_t* buf, struct zsAdditionInfo* AddInfo)
{
#if 0
    zmw_get_wlan_dev(dev);

    zm_msg0_mm(ZM_LV_0, "Rx probersp");

    /* Gather scan result */

    //zm_debug_msg1("bssList Count = ", wd->sta.bssList.bssCount);
    /* return if not in scanning */
    if ((wd->heartBeatNotification & ZM_BSSID_LIST_SCAN)
            != ZM_BSSID_LIST_SCAN)
    {
        return;
    }

    //if ( wd->sta.pUpdateBssList->bssCount == ZM_MAX_BSS )
    if ( wd->sta.bssList.bssCount == ZM_MAX_BSS )
    {
        return;
    }

    zfProcessProbeRsp(dev, buf, AddInfo);

#endif
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfApAddIeSsid               */
/*      Add AP information element SSID to buffer.                      */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : buffer to add information element                         */
/*      offset : add information element from this offset               */
/*      vap : virtual AP ID                                             */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      buffer offset after adding information element                  */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2005.11     */
/*                                                                      */
/************************************************************************/
u16_t zfApAddIeSsid(zdev_t* dev, zbuf_t* buf, u16_t offset, u16_t vap)
{
    u16_t i;

    zmw_get_wlan_dev(dev);

    /* Element ID */
    zmw_tx_buf_writeb(dev, buf, offset++, ZM_WLAN_EID_SSID);

    /* Element Length */
    zmw_tx_buf_writeb(dev, buf, offset++, wd->ap.ssidLen[vap]);

    /* Information : SSID */
    for (i=0; i<wd->ap.ssidLen[vap]; i++)
    {
        zmw_tx_buf_writeb(dev, buf, offset++, wd->ap.ssid[vap][i]);
    }

    return offset;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfApAddIeTim                */
/*      Add AP information element TIM to buffer.                       */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : buffer to add information element                         */
/*      offset : add information element from this offset               */
/*      vap : virtual AP ID                                             */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      buffer offset after adding information element                  */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2005.11     */
/*                                                                      */
/************************************************************************/
u16_t zfApAddIeTim(zdev_t* dev, zbuf_t* buf, u16_t offset, u16_t vap)
{
    u8_t uniBitMap[9];
    u16_t highestByte;
    u16_t i;
    u16_t lenOffset;
    u16_t id;
    u16_t dst[3];
    u16_t aid;
    u16_t bitPosition;
    u16_t bytePosition;
    zbuf_t* psBuf;
    zbuf_t* tmpBufArray[ZM_UNI_ARRAY_SIZE];
    u16_t tmpBufArraySize = 0;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    /* Element ID */
    zmw_tx_buf_writeb(dev, buf, offset++, ZM_WLAN_EID_TIM);

    /* offset of Element Length */
    lenOffset = offset++;

    /* Information : TIM */
    /* DTIM count */
    /* TODO : Doesn't work for Virtual AP's case */
    wd->CurrentDtimCount++;
    if (wd->CurrentDtimCount >= wd->dtim)
    {
        wd->CurrentDtimCount = 0;
    }
    zmw_tx_buf_writeb(dev, buf, offset++, wd->CurrentDtimCount);
    /* DTIM period */
    zmw_tx_buf_writeb(dev, buf, offset++, wd->dtim);
    /* bitmap offset */
    zmw_tx_buf_writeb(dev, buf, offset++, 0);

    /* Update BCMC bit */
    if (wd->CurrentDtimCount == 0)
    {
        zmw_enter_critical_section(dev);
        wd->ap.timBcmcBit[vap] = (wd->ap.bcmcTail[vap]!=wd->ap.bcmcHead[vap])?1:0;
        zmw_leave_critical_section(dev);
    }
    else
    {
        wd->ap.timBcmcBit[vap] = 0;
    }

    /* Update Unicast bitmap */
    /* reset bit map */
    for (i=0; i<9; i++)
    {
        uniBitMap[i] = 0;
    }
    highestByte = 0;
#if 1

    zmw_enter_critical_section(dev);

    id = wd->ap.uniHead;
    while (id != wd->ap.uniTail)
    {
        psBuf = wd->ap.uniArray[id];

        /* TODO : Aging PS frame after queuing for more than 10 seconds */

        /* get destination STA's aid */
        dst[0] = zmw_tx_buf_readh(dev, psBuf, 0);
        dst[1] = zmw_tx_buf_readh(dev, psBuf, 2);
        dst[2] = zmw_tx_buf_readh(dev, psBuf, 4);
        if ((aid = zfApFindSta(dev, dst)) != 0xffff)
        {
            if (wd->ap.staTable[aid].psMode != 0)
            {
                zm_msg1_mm(ZM_LV_0, "aid=",aid);
                aid++;
                zm_assert(aid<=64);
                bitPosition = (1 << (aid & 0x7));
                bytePosition = (aid >> 3);
                uniBitMap[bytePosition] |= bitPosition;

                if (bytePosition>highestByte)
                {
                    highestByte = bytePosition;
                }
                id = (id+1) & (ZM_UNI_ARRAY_SIZE-1);
            }
            else
            {
                zm_msg0_mm(ZM_LV_0, "Send PS frame which STA no longer in PS mode");
                /* Send PS frame which STA no longer in PS mode */
                zfApRemoveFromPsQueue(dev, id, dst);
                tmpBufArray[tmpBufArraySize++] = psBuf;
            }
        }
        else
        {
            zm_msg0_mm(ZM_LV_0, "Free garbage PS frame");
            /* Free garbage PS frame */
            zfApRemoveFromPsQueue(dev, id, dst);
            zfwBufFree(dev, psBuf, 0);
        }
    }

    zmw_leave_critical_section(dev);
#endif

    zfQueueGenerateUapsdTim(dev, wd->ap.uapsdQ, uniBitMap, &highestByte);

    zm_msg1_mm(ZM_LV_3, "bm=",uniBitMap[0]);
    zm_msg1_mm(ZM_LV_3, "highestByte=",highestByte);
    zm_msg1_mm(ZM_LV_3, "timBcmcBit[]=",wd->ap.timBcmcBit[vap]);

    /* bitmap */
    zmw_tx_buf_writeb(dev, buf, offset++,
                         uniBitMap[0] | wd->ap.timBcmcBit[vap]);
    for (i=0; i<highestByte; i++)
    {
        zmw_tx_buf_writeb(dev, buf, offset++, uniBitMap[i+1]);
    }

    /* Element Length */
    zmw_tx_buf_writeb(dev, buf, lenOffset, highestByte+4);

    for (i=0; i<tmpBufArraySize; i++)
    {
        /* Put to VTXQ[ac] */
        zfPutVtxq(dev, tmpBufArray[i]);
    }
    /* Push VTXQ[ac] */
    zfPushVtxq(dev);

    return offset;
}



/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfApRemoveFromPsQueue       */
/*      Remove zbuf from PS queue.                                      */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      id : index in ps queue                                          */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      more data bit                                                   */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2007.1      */
/*                                                                      */
/************************************************************************/
u8_t zfApRemoveFromPsQueue(zdev_t* dev, u16_t id, u16_t* addr)
{
    u16_t dst[3];
    u16_t nid;
    u8_t moreData = 0;
    zmw_get_wlan_dev(dev);

    wd->ap.uniTail = (wd->ap.uniTail-1) & (ZM_UNI_ARRAY_SIZE-1);
    while (id != wd->ap.uniTail)
    {
        nid = (id + 1) & (ZM_UNI_ARRAY_SIZE - 1);
        wd->ap.uniArray[id] = wd->ap.uniArray[nid];

        /* Search until tail to config more data bit */
        dst[0] = zmw_buf_readh(dev, wd->ap.uniArray[id], 0);
        dst[1] = zmw_buf_readh(dev, wd->ap.uniArray[id], 2);
        dst[2] = zmw_buf_readh(dev, wd->ap.uniArray[id], 4);
        if ((addr[0] == dst[0]) && (addr[1] == dst[1])
                && (addr[2] == dst[2]))
        {
            moreData = 0x20;
        }

        id = nid;
    }
    return moreData;
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfApAddIeWmePara            */
/*      Add WME Parameter Element to buffer.                            */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : buffer to add information element                         */
/*      offset : add information element from this offset               */
/*      vap : virtual AP ID                                             */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      buffer offset after adding information element                  */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2006.1      */
/*                                                                      */
/************************************************************************/
u16_t zfApAddIeWmePara(zdev_t* dev, zbuf_t* buf, u16_t offset, u16_t vap)
{
    zmw_get_wlan_dev(dev);

    /* Element ID */
    zmw_tx_buf_writeb(dev, buf, offset++, ZM_WLAN_EID_WIFI_IE);

    /* Element Length */
    zmw_tx_buf_writeb(dev, buf, offset++, 24);

    /* OUI */
    zmw_tx_buf_writeb(dev, buf, offset++, 0x00);
    zmw_tx_buf_writeb(dev, buf, offset++, 0x50);
    zmw_tx_buf_writeb(dev, buf, offset++, 0xF2);
    zmw_tx_buf_writeb(dev, buf, offset++, 0x02);
    zmw_tx_buf_writeb(dev, buf, offset++, 0x01);
    zmw_tx_buf_writeb(dev, buf, offset++, 0x01);

    /* QoS Info */
    if (wd->ap.uapsdEnabled)
    {
        zmw_tx_buf_writeb(dev, buf, offset++, 0x81);
    }
    else
    {
    zmw_tx_buf_writeb(dev, buf, offset++, 0x01);
    }

    /* Reserved */
    zmw_tx_buf_writeb(dev, buf, offset++, 0x00);

    /* Best Effort AC parameters */
    zmw_tx_buf_writeb(dev, buf, offset++, 0x03);
    zmw_tx_buf_writeb(dev, buf, offset++, 0xA4);
    zmw_tx_buf_writeb(dev, buf, offset++, 0x00);
    zmw_tx_buf_writeb(dev, buf, offset++, 0x00);
    /* Backfround AC parameters */
    zmw_tx_buf_writeb(dev, buf, offset++, 0x27);
    zmw_tx_buf_writeb(dev, buf, offset++, 0xA4);
    zmw_tx_buf_writeb(dev, buf, offset++, 0x00);
    zmw_tx_buf_writeb(dev, buf, offset++, 0x00);
    /* Video AC parameters */
    zmw_tx_buf_writeb(dev, buf, offset++, 0x42);
    zmw_tx_buf_writeb(dev, buf, offset++, 0x43);
    zmw_tx_buf_writeb(dev, buf, offset++, 0x5E);
    zmw_tx_buf_writeb(dev, buf, offset++, 0x00);
    /* Voice AC parameters */
    zmw_tx_buf_writeb(dev, buf, offset++, 0x62);
    zmw_tx_buf_writeb(dev, buf, offset++, 0x32);
    zmw_tx_buf_writeb(dev, buf, offset++, 0x2F);
    zmw_tx_buf_writeb(dev, buf, offset++, 0x00);

    return offset;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfApSendBeacon              */
/*      Sned AP mode beacon.                                            */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      none                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2005.11     */
/*                                                                      */
/************************************************************************/
void zfApSendBeacon(zdev_t* dev)
{
    zbuf_t* buf;
    u16_t offset;
    u16_t vap;
    u16_t seq;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    wd->ap.beaconCounter++;
    if (wd->ap.beaconCounter >= wd->ap.vapNumber)
    {
        wd->ap.beaconCounter = 0;
    }
    vap = wd->ap.beaconCounter;


    zm_msg1_mm(ZM_LV_2, "Send beacon, vap=", vap);

    /* TBD : Maximum size of beacon */
    if ((buf = zfwBufAllocate(dev, 1024)) == NULL)
    {
        zm_msg0_mm(ZM_LV_0, "Alloc beacon buf Fail!");
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
#ifdef ZM_VAPMODE_MULTILE_SSID
    zmw_tx_buf_writeh(dev, buf, offset, wd->macAddr[2]); //Multiple SSID
#else
    zmw_tx_buf_writeh(dev, buf, offset, (wd->macAddr[2]+(vap<<8))); //VAP
#endif
    offset+=2;
    /* Address 3 */
    zmw_tx_buf_writeh(dev, buf, offset, wd->macAddr[0]);
    offset+=2;
    zmw_tx_buf_writeh(dev, buf, offset, wd->macAddr[1]);
    offset+=2;
#ifdef ZM_VAPMODE_MULTILE_SSID
    zmw_tx_buf_writeh(dev, buf, offset, wd->macAddr[2]); //Multiple SSID
#else
    zmw_tx_buf_writeh(dev, buf, offset, (wd->macAddr[2]+(vap<<8))); //VAP
#endif
    offset+=2;

    /* Sequence number */
    zmw_enter_critical_section(dev);
    seq = ((wd->mmseq++)<<4);
    zmw_leave_critical_section(dev);
    zmw_tx_buf_writeh(dev, buf, offset, seq);
    offset+=2;

    /* 24-31 Time Stamp : hardware will fill this field */
    zmw_tx_buf_writeh(dev, buf, offset, 0);
    zmw_tx_buf_writeh(dev, buf, offset+2, 0);
    zmw_tx_buf_writeh(dev, buf, offset+4, 0);
    zmw_tx_buf_writeh(dev, buf, offset+6, 0);
    offset+=8;

    /* Beacon Interval */
    zmw_tx_buf_writeh(dev, buf, offset, wd->beaconInterval);
    offset+=2;

    /* Capability */
    zmw_tx_buf_writeh(dev, buf, offset, wd->ap.capab[vap]);
    offset+=2;

    /* SSID */
    if (wd->ap.hideSsid[vap] == 0)
    {
        offset = zfApAddIeSsid(dev, buf, offset, vap);
    }
    else
    {
        zmw_tx_buf_writeb(dev, buf, offset++, ZM_WLAN_EID_SSID);
        zmw_tx_buf_writeb(dev, buf, offset++, 0);

    }

    /* Support Rate */
    if ( wd->frequency < 3000 )
    {
    offset = zfMmAddIeSupportRate(dev, buf, offset,
                                  ZM_WLAN_EID_SUPPORT_RATE, ZM_RATE_SET_CCK);
    }
    else
    {
        offset = zfMmAddIeSupportRate(dev, buf, offset,
                                  ZM_WLAN_EID_SUPPORT_RATE, ZM_RATE_SET_OFDM);
    }

    /* DS parameter set */
    offset = zfMmAddIeDs(dev, buf, offset);

    /* TIM */
    offset = zfApAddIeTim(dev, buf, offset, vap);

    /* If WLAN Type is not PURE B */
    if (wd->ap.wlanType[vap] != ZM_WLAN_TYPE_PURE_B)
    {
        if ( wd->frequency < 3000 )
        {
        /* ERP Information */
        offset = zfMmAddIeErp(dev, buf, offset);

        /* Extended Supported Rates */
        offset = zfMmAddIeSupportRate(dev, buf, offset,
                                      ZM_WLAN_EID_EXTENDED_RATE, ZM_RATE_SET_OFDM);
    }
    }

    /* TODO : country information */
    /* TODO : RSN */
    if (wd->ap.wpaSupport[vap] == 1)
    {
        offset = zfMmAddIeWpa(dev, buf, offset, vap);
    }

    /* WME Parameters */
    if (wd->ap.qosMode == 1)
    {
        offset = zfApAddIeWmePara(dev, buf, offset, vap);
    }

    /* HT Capabilities Info */
    offset = zfMmAddHTCapability(dev, buf, offset);

    /* Extended HT Capabilities Info */
    offset = zfMmAddExtendedHTCapability(dev, buf, offset);

    /* 1212 : write to beacon fifo */
    /* 1221 : write to share memory */
    zfHpSendBeacon(dev, buf, offset);

    /* Free beacon buffer */
    /* TODO: In order to fit the madwifi beacon architecture, we need to
       free beacon buffer in the HAL layer.
     */

    //zfwBufFree(dev, buf, 0);
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfIntrabssForward           */
/*      Called to transmit intra-BSS frame from upper layer.            */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : buffer pointer                                            */
/*      vap : virtual AP                                                */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      1 : unicast intras-BSS frame                                    */
/*      0 : other frames                                                */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen             ZyDAS Technology Corporation    2005.11     */
/*                                                                      */
/************************************************************************/
u16_t zfIntrabssForward(zdev_t* dev, zbuf_t* buf, u8_t srcVap)
{
    u16_t err;
    u16_t asocFlag = 0;
    u16_t dst[3];
    u16_t aid;
    u16_t staState;
    zbuf_t* txBuf;
    u16_t len;
    u16_t i;
    u16_t temp;
    u16_t ret;
    u8_t vap = 0;
#ifdef ZM_ENABLE_NATIVE_WIFI
    dst[0] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A3_OFFSET);
    dst[1] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A3_OFFSET+2);
    dst[2] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A3_OFFSET+4);
#else
    dst[0] = zmw_rx_buf_readh(dev, buf, 0);
    dst[1] = zmw_rx_buf_readh(dev, buf, 2);
    dst[2] = zmw_rx_buf_readh(dev, buf, 4);
#endif  // ZM_ENABLE_NATIVE_WIFI

    /* Do Intra-BSS forward(data copy) if necessary*/
    if ((dst[0]&0x1) != 0x1)
    {
        aid = zfApGetSTAInfo(dev, dst, &staState, &vap);
        if ((aid != 0xffff) && (staState == ZM_STATE_ASOC) && (srcVap == vap))
        {
            asocFlag = 1;
            zm_msg0_rx(ZM_LV_2, "Intra-BSS forward : asoc STA");
        }

    }
    else
    {
        vap = srcVap;
        zm_msg0_rx(ZM_LV_2, "Intra-BSS forward : BCorMC");
    }

    /* destination address = associated STA or BC/MC */
    if ((asocFlag == 1) || ((dst[0]&0x1) == 0x1))
    {
        /* Allocate frame */
        if ((txBuf = zfwBufAllocate(dev, ZM_RX_FRAME_SIZE))
                == NULL)
        {
            zm_msg0_rx(ZM_LV_1, "Alloc intra-bss buf Fail!");
            goto zlAllocError;
        }

        /* Copy frame */
        len = zfwBufGetSize(dev, buf);
        for (i=0; i<len; i+=2)
        {
            temp = zmw_rx_buf_readh(dev, buf, i);
            zmw_tx_buf_writeh(dev, txBuf, i, temp);
        }
        zfwBufSetSize(dev, txBuf, len);

#ifdef ZM_ENABLE_NATIVE_WIFI
        /* Tx-A2 = Rx-A1, Tx-A3 = Rx-A2, Tx-A1 = Rx-A3 */
        for (i=0; i<6; i+=2)
        {
            temp = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A1_OFFSET+i);
            zmw_tx_buf_writeh(dev, txBuf, ZM_WLAN_HEADER_A2_OFFSET+i, temp);
            temp = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A2_OFFSET+i);
            zmw_tx_buf_writeh(dev, txBuf, ZM_WLAN_HEADER_A3_OFFSET+i, temp);
            temp = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A3_OFFSET+i);
            zmw_tx_buf_writeh(dev, txBuf, ZM_WLAN_HEADER_A1_OFFSET+i, temp);
        }

        #endif

        /* Transmit frame */
        /* Return error if port is disabled */
        if ((err = zfTxPortControl(dev, txBuf, vap)) == ZM_PORT_DISABLED)
        {
            err = ZM_ERR_TX_PORT_DISABLED;
            goto zlTxError;
        }

#if 1
        /* AP : Buffer frame for power saving STA */
        if ((ret = zfApBufferPsFrame(dev, txBuf, vap)) == 0)
        {
            /* forward frame if not been buffered */
            #if 1
            /* Put to VTXQ[ac] */
            ret = zfPutVtxq(dev, txBuf);
            /* Push VTXQ[ac] */
            zfPushVtxq(dev);
            #else
            zfTxSendEth(dev, txBuf, vap, ZM_INTERNAL_ALLOC_BUF, 0);
            #endif

        }
#endif
    }
    return asocFlag;

zlTxError:
    zfwBufFree(dev, txBuf, 0);
zlAllocError:
    return asocFlag;
}

struct zsMicVar* zfApGetRxMicKey(zdev_t* dev, zbuf_t* buf)
{
    u8_t sa[6];
    u16_t id = 0, macAddr[3];

    zmw_get_wlan_dev(dev);

    zfCopyFromRxBuffer(dev, buf, sa, ZM_WLAN_HEADER_A2_OFFSET, 6);

    macAddr[0] = sa[0] + (sa[1] << 8);
    macAddr[1] = sa[2] + (sa[3] << 8);
    macAddr[2] = sa[4] + (sa[5] << 8);

    if ((id = zfApFindSta(dev, macAddr)) != 0xffff)
        return (&wd->ap.staTable[id].rxMicKey);

    return NULL;
}

struct zsMicVar* zfApGetTxMicKey(zdev_t* dev, zbuf_t* buf, u8_t* qosType)
{
    u8_t da[6];
    u16_t id = 0, macAddr[3];

    zmw_get_wlan_dev(dev);

    zfCopyFromIntTxBuffer(dev, buf, da, 0, 6);

    macAddr[0] = da[0] + (da[1] << 8);
    macAddr[1] = da[2] + (da[3] << 8);
    macAddr[2] = da[4] + (da[5] << 8);

    if ((macAddr[0] & 0x1))
    {
        return (&wd->ap.bcMicKey[0]);
    }
    else if ((id = zfApFindSta(dev, macAddr)) != 0xffff)
    {
        *qosType = wd->ap.staTable[id].qosType;
        return (&wd->ap.staTable[id].txMicKey);
    }

    return NULL;
}

u16_t zfApUpdatePsBit(zdev_t* dev, zbuf_t* buf, u8_t* vap, u8_t* uapsdTrig)
{
    u16_t staState;
    u16_t aid;
    u16_t psBit;
    u16_t src[3];
    u16_t dst[1];
    u16_t i;

    zmw_get_wlan_dev(dev);

    src[0] = zmw_rx_buf_readh(dev, buf, 10);
    src[1] = zmw_rx_buf_readh(dev, buf, 12);
    src[2] = zmw_rx_buf_readh(dev, buf, 14);

    if ((zmw_rx_buf_readb(dev, buf, 1) & 0x3) != 3)
    {
        /* AP */
        dst[0] = zmw_rx_buf_readh(dev, buf, 4);

        psBit = (zmw_rx_buf_readb(dev, buf, 1) & 0x10) >> 4;
        /* Get AID and update STA PS mode */
        aid = zfApGetSTAInfoAndUpdatePs(dev, src, &staState, vap, psBit, uapsdTrig);

        /* if STA not associated, send deauth */
        if ((aid == 0xffff) || (staState != ZM_STATE_ASOC))
        {
            if ((dst[0]&0x1)==0)
            {
                zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_DEAUTH, src, 0x7,
                        0, 0);
            }

            return ZM_ERR_STA_NOT_ASSOCIATED;
        }
    } /* if ((zmw_rx_buf_readb(dev, buf, 1) & 0x3) != 3) */
    else
    {
        /* WDS */
        for (i=0; i<ZM_MAX_WDS_SUPPORT; i++)
        {
            if ((wd->ap.wds.wdsBitmap & (1<<i)) != 0)
            {
                if ((src[0] == wd->ap.wds.macAddr[i][0])
                        && (src[1] == wd->ap.wds.macAddr[i][1])
                        && (src[2] == wd->ap.wds.macAddr[i][2]))
                {
                    *vap = 0x20 + i;
                    break;
                }
            }
        }
    }
    return ZM_SUCCESS;
}

void zfApProcessPsPoll(zdev_t* dev, zbuf_t* buf)
{
    u16_t src[3];
    u16_t dst[3];
    zbuf_t* psBuf = NULL;
    u16_t id;
    u8_t moreData = 0;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    src[0] = zmw_tx_buf_readh(dev, buf, 10);
    src[1] = zmw_tx_buf_readh(dev, buf, 12);
    src[2] = zmw_tx_buf_readh(dev, buf, 14);

    /* Find ps buffer for PsPoll */
    zmw_enter_critical_section(dev);
    id = wd->ap.uniHead;
    while (id != wd->ap.uniTail)
    {
        psBuf = wd->ap.uniArray[id];

        dst[0] = zmw_tx_buf_readh(dev, psBuf, 0);
        dst[1] = zmw_tx_buf_readh(dev, psBuf, 2);
        dst[2] = zmw_tx_buf_readh(dev, psBuf, 4);

        if ((src[0] == dst[0]) && (src[1] == dst[1]) && (src[2] == dst[2]))
        {
            moreData = zfApRemoveFromPsQueue(dev, id, src);
            break;
        }
        else
        {
            psBuf = NULL;
        }
        id = (id + 1) & (ZM_UNI_ARRAY_SIZE - 1);
    }
    zmw_leave_critical_section(dev);

    /* Send ps buffer */
    if (psBuf != NULL)
    {
        /* Send with more data bit */
        zfTxSendEth(dev, psBuf, 0, ZM_EXTERNAL_ALLOC_BUF, moreData);
    }

    return;
}

void zfApSetProtectionMode(zdev_t* dev, u16_t mode)
{
    zmw_get_wlan_dev(dev);

    if (mode == 0)
    {
        if (wd->ap.protectionMode != mode)
        {
            /* Write MAC&PHY registers to disable protection */

            wd->ap.protectionMode = mode;
        }

    }
    else
    {
        if (wd->ap.protectionMode != mode)
        {
            /* Write MAC&PHY registers to enable protection */

            wd->ap.protectionMode = mode;
        }
    }
    return;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfApSendFailure             */
/*      Send failure.                                                   */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      addr : receiver address                                         */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      None                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2007.1      */
/*                                                                      */
/************************************************************************/
void zfApSendFailure(zdev_t* dev, u8_t* addr)
{
    u16_t id;
    u16_t staAddr[3];
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    staAddr[0] = addr[0] + (((u16_t)addr[1])<<8);
    staAddr[1] = addr[2] + (((u16_t)addr[3])<<8);
    staAddr[2] = addr[4] + (((u16_t)addr[5])<<8);
    zmw_enter_critical_section(dev);
    if ((id = zfApFindSta(dev, staAddr)) != 0xffff)
    {
        /* Send failture : Add 3 minutes to inactive time that will */
        /*                 will make STA been kicked out soon */
        wd->ap.staTable[id].time -= (3*ZM_TICK_PER_MINUTE);
    }
    zmw_leave_critical_section(dev);
}


void zfApProcessAction(zdev_t* dev, zbuf_t* buf)
{
    u8_t category;

    //zmw_get_wlan_dev(dev);

    //zmw_declare_for_critical_section();

    category = zmw_rx_buf_readb(dev, buf, 24);

    switch (category)
    {
    case ZM_WLAN_BLOCK_ACK_ACTION_FRAME:
        zfAggBlockAckActionFrame(dev, buf);
        break;
    default:
        break;
    }

    return;
}
