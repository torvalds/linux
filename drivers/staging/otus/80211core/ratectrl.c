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

const u32_t zcRateToPhyCtrl[] =
    {
        /* 1M,        2M,         5M,        11M ,  0  1  2  3*/
        0x00000,    0x10000,    0x20000,    0x30000,
        /* 6M         9M          12M        18M ,  4  5  6  7*/
        0xb0001,    0xf0001,    0xa0001,    0xe0001,
        /* 24M         36M        48M        54M ,  8  9  10 11*/
        0x90001,    0xd0001,    0x80001,    0xc0001,
        /* MCS0      MCS1        MCS2        MCS3,  12 13 14 15*/
        0x00002,    0x10002,    0x20002,    0x30002,
        /* MCS4      MCS5        MCS6        MCS7,  16 17 18 19*/
        0x40002,    0x50002,    0x60002,    0x70002,
        /* MCS8      MCS9        MCS10       MCS11, 20 21 22 23*/
        0x80002,    0x90002,    0xa0002,    0xb0002,
        /* MCS12     MCS13       MCS14       MCS15, 24 25 26 27*/
        0xc0002,    0xd0002,    0xe0002,    0xf0002,
        /* MCS14SG,  MCS15SG     MCS7SG           , 28 29, 30*/
        0x800e0002, 0x800f0002, 0x80070002
    };


const u8_t zcHtRateTable[15][4] =
    { /*[5G 20MHz]  [5G 40MHz] [2.4G 20MHz]  [2.4G 40MHz]*/
        {  4,          4,          0,          0},   /*OFDM6M OFDM6M  CCK1M  CCK1M  */
        {  5,          5,          1,          1},   /*OFDM9M OFDM9M  CCK2M  CCK2M  */
        {  13,         12,         2,          2},   /*MCS1   MCS0    CCK5M  CCK5M  */
        {  14,         13,         3,          3},   /*MCS2   MCS1    CCK11M CCK11M */
        {  15,         14,         13,         12},  /*MCS3   MCS2    MCS1   MCS0   */
        {  16,         15,         14,         13},  /*MCS4   MCS3    MCS2   MCS1   */
        {  23,         16,         15,         14},  /*MCS11  MCS4    MCS3   MCS2   */
        {  24,         23,         16,         15},  /*MCS12  MCS11   MCS4   MCS3   */
        {  25,         24,         23,         16},  /*MCS13  MCS12   MCS11  MCS4   */
        {  26,         25,         24,         23},  /*MCS14  MCS13   MCS12  MCS11  */
        {  27,         26,         25,         24},  /*MCS15  MCS14   MCS13  MCS12  */
        {  0,          27,         26,         25},  /*0      MCS15   MCS14  MCS13  */
        {  0,          29,         27,         26},  /*0      MCS15SG MCS15  MCS14  */
        {  0,          0,          0,          28},  /*0      0       0      MCS14SG*/
        {  0,          0,          0,          29}   /*0      0       0      MCS15SG*/
    };

const u8_t zcHtOneTxStreamRateTable[15][4] =
    { /*[5G 20MHz]  [5G 40MHz] [2.4G 20MHz]  [2.4G 40MHz]*/
        {  4,          4,          0,          0},   /*OFDM6M OFDM6M  CCK1M  CCK1M  */
        {  5,          5,          1,          1},   /*OFDM9M OFDM9M  CCK2M  CCK2M  */
        {  13,         12,         2,          2},   /*MCS1   MCS0    CCK5M  CCK5M  */
        {  14,         13,         3,          3},   /*MCS2   MCS1    CCK11M CCK11M */
        {  15,         14,         13,         12},  /*MCS3   MCS2    MCS1   MCS0   */
        {  16,         15,         14,         13},  /*MCS4   MCS3    MCS2   MCS1   */
        {  17,         16,         15,         14},  /*MCS5   MCS4    MCS3   MCS2   */
        {  18,         17,         16,         15},  /*MCS6   MCS5    MCS4   MCS3   */
        {  19,         18,         17,         16},  /*MCS7   MCS6    MCS5   MCS4   */
        {  0,          19,         18,         17},  /*0      MCS7    MCS6   MCS5   */
        {  0,          30,         19,         18},  /*0      MCS7SG  MCS7   MCS6   */
        {  0,          0,          0,          19},  /*0      0       0      MCS7   */
        {  0,          0,          0,          30},  /*0      0       0      MCS7SG */
        {  0,          0,          0,          0 },  /*0      0       0      0      */
        {  0,          0,          0,          0 }   /*0      0       0      0      */
    };

const u16_t zcRate[] =
    {
        1, 2, 5, 11,                  /* 1M, 2M, 5M, 11M          ,  0  1  2  3*/
        6, 9, 12, 18,                 /* 6M  9M  12M  18M         ,  4  5  6  7*/
        24, 36, 48, 54,               /* 24M  36M  48M  54M       ,  8  9 10 11*/
        13, 27, 40, 54,               /* MCS0 MCS1 MCS2 MCS3      , 12 13 14 15*/
        81, 108, 121, 135,            /* MCS4 MCS5 MCS6 MCS7      , 16 17 18 19*/
        27, 54, 81, 108,              /* MCS8 MCS9 MCS10 MCS11    , 20 21 22 23*/
        162, 216, 243, 270,           /* MCS12 MCS13 MCS14 MCS15  , 24 25 26 27*/
        270, 300, 150                 /* MCS14SG, MCS15SG, MCS7SG , 28 29 30*/
    };

const u16_t PERThreshold[] =
    {
        100, 50, 50, 50,    /* 1M, 2M, 5M, 11M          ,  0  1  2  3*/
        50, 50, 30, 30,     /* 6M  9M  12M  18M         ,  4  5  6  7*/
        25, 25, 25, 20,     /* 24M  36M  48M  54M       ,  8  9 10 11*/
        50, 50, 50, 40,    /* MCS0 MCS1 MCS2 MCS3      , 12 13 14 15*/
        30, 30, 30, 30,    /* MCS4 MCS5 MCS6 MCS7      , 16 17 18 19*/
        30, 30, 25, 25,    /* MCS8 MCS9 MCS10 MCS11    , 20 21 22 23*/
        25, 25, 15, 15,     /* MCS12 MCS13 MCS14 MCS15  , 24 25 26 27*/
        15, 15, 10          /* MCS14SG, MCS15SG         , 28 29*/
    };

const u16_t FailDiff[] =
    {
        40, 46, 40, 0,          /* 1M, 2M, 5M, 11M          ,  0  1  2  3*/
        24, 17, 22, 16,         /* 6M  9M  12M  18M         ,  4  5  6  7*/
        19, 13, 5, 0,           /* 24M  36M  48M  54M       ,  8  9 10 11*/
        36, 22, 15, 19,         /* MCS0 MCS1 MCS2 MCS3      , 12 13 14 15*/
        12, 5, 4, 7,            /* MCS4 MCS5 MCS6 MCS7      , 16 17 18 19*/
        0, 0, 0, 0,             /* MCS8 MCS9 MCS10 MCS11    , 20 21 22 23*/
        9, 4, 3, 3,             /* MCS12 MCS13 MCS14 MCS15  , 24 25 26 27*/
        3, 0, 0                 /* MCS14SG, MCS15SG         , 28 29*/
    };


#ifdef ZM_ENABLE_BA_RATECTRL
u32_t TxMPDU[29];
u32_t BAFail[29];
u32_t BAPER[29];
const u16_t BADiff[] =
    {
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        361, 220, 151, 187,
        122, 48, 41, 65,
        0, 0, 0, 0,
        88, 33, 27, 25,
        0
    };
#endif

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfRateCtrlInitCell          */
/*      Initialize rate control cell.                                   */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      type : 0=>11b, 1=>11a/g, 2=>11n, 3=>11n one Tx stream           */
/*      gBand : 1=>2.4G, 0=>5G                                          */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      None                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2007.2      */
/*                                                                      */
/************************************************************************/
void zfRateCtrlInitCell(zdev_t* dev, struct zsRcCell* rcCell, u8_t type,
        u8_t gBand, u8_t SG40)
{
    u8_t i;
    u8_t maxrate;
    zmw_get_wlan_dev(dev);

    if (SG40) SG40 = 1;

    if (gBand != 0)
    {
        if (type == 1) //11g
        {
            for (i=0; i<4; i++) //1M 2M 5M 11M
            {
                rcCell->operationRateSet[i] = (u8_t)i;
            }
            for (i=4; i<10; i++) //12M 18M 24M 36M 48M 54M
            {
                rcCell->operationRateSet[i] = 2+i;
            }
            rcCell->operationRateCount = 10;
            rcCell->currentRateIndex = 5; //18M
        }
        else if (type == 2) //11ng
        {
            if (wd->wlanMode == ZM_MODE_AP) //AP 11ng 40M
            {
                for (i=0; i<15; i++)
                {
                    rcCell->operationRateSet[i] = zcHtRateTable[i][3];
                }
                if(!SG40) rcCell->operationRateSet[13] = 27;
                rcCell->operationRateCount = 14+SG40;
                rcCell->currentRateIndex = 10;
            }
            else //STA
            {
                if (wd->sta.htCtrlBandwidth == ZM_BANDWIDTH_40MHZ) //11ng 40M
                {
                    for (i=0; i<15; i++)
                    {
                        rcCell->operationRateSet[i] = zcHtRateTable[i][3];
                    }
                    if(!SG40) rcCell->operationRateSet[13] = 27;
                    rcCell->operationRateCount = 14+SG40;
                    rcCell->currentRateIndex = 10;
                }
                else    //11ng 20M
                {
                    for (i=0; i<13; i++)
                    {
                        rcCell->operationRateSet[i] = zcHtRateTable[i][2];
                    }
                    rcCell->operationRateCount = 13;
                    rcCell->currentRateIndex = 9;
                }
            }
        }
        else if (type == 3) //11ng one Tx stream
        {
                if (wd->sta.htCtrlBandwidth == ZM_BANDWIDTH_40MHZ) //11ng 40M one Tx stream
                {
                    if(SG40 != 0)
                    {
                        maxrate = 13;
                    }
                    else
                    {
                        maxrate = 12;
                    }
                    for (i=0; i<maxrate; i++)
                    {
                        rcCell->operationRateSet[i] = zcHtOneTxStreamRateTable[i][3];
                    }
                    rcCell->operationRateCount = i;
                    rcCell->currentRateIndex = ((i+1)*3)/4;
                }
                else    //11ng 20M
                {
                    for (i=0; i<11; i++)
                    {
                        rcCell->operationRateSet[i] = zcHtOneTxStreamRateTable[i][2];
                    }
                    rcCell->operationRateCount = i;
                    rcCell->currentRateIndex = ((i+1)*3)/4;
                }
        }
        else //if (type == 0) //11b
        {
            for (i=0; i<4; i++)
            {
                rcCell->operationRateSet[i] = (u8_t)i;
            }
            rcCell->operationRateCount = 4;
            rcCell->currentRateIndex = rcCell->operationRateCount-1;
        }
    }
    else
    {
        if (type == 2) //11na
        {
            if (wd->wlanMode == ZM_MODE_AP) //AP 11na 40M
            {
                for (i=0; i<(12+SG40); i++)
                {
                    rcCell->operationRateSet[i] = zcHtRateTable[i][1];
                }
                rcCell->operationRateCount = 12+SG40;
                rcCell->currentRateIndex = 8;
            }
            else //STA
            {
                if (wd->sta.htCtrlBandwidth == ZM_BANDWIDTH_40MHZ) //11na 40M
                {
                    for (i=0; i<(12+SG40); i++)
                    {
                        rcCell->operationRateSet[i] = zcHtRateTable[i][1];
                    }
                    rcCell->operationRateCount = 12+SG40;
                    rcCell->currentRateIndex = 8;
                }
                else    //11na 20M
                {
                    for (i=0; i<11; i++)
                    {
                        rcCell->operationRateSet[i] = zcHtRateTable[i][0];
                    }
                    rcCell->operationRateCount = 11;
                    rcCell->currentRateIndex = 7;
                }
            }
        }
        else if (type == 3) //11na one Tx stream
        {
                if (wd->sta.htCtrlBandwidth == ZM_BANDWIDTH_40MHZ) //11na 40M one Tx stream
                {
                    if(SG40 != 0)
                    {
                        maxrate = 11;
                    }
                    else
                    {
                        maxrate = 10;
                    }
                    for (i=0; i<maxrate; i++)
                    {
                        rcCell->operationRateSet[i] = zcHtOneTxStreamRateTable[i][1];
                    }
                    rcCell->operationRateCount = i;
                    rcCell->currentRateIndex = ((i+1)*3)/4;
                }
                else    //11ng 20M
                {
                    for (i=0; i<9; i++)
                    {
                        rcCell->operationRateSet[i] = zcHtOneTxStreamRateTable[i][0];
                    }
                    rcCell->operationRateCount = i;
                    rcCell->currentRateIndex = ((i+1)*3)/4;
                }
        }
        else //if (type == 1) //11a
        {
            for (i=0; i<8; i++) //6M 9M 12M 18M 24M 36M 48M 54M
            {
                rcCell->operationRateSet[i] = i+4;
            }
            rcCell->operationRateCount = 8;
            rcCell->currentRateIndex = 4;  //24M
        }
    }

    rcCell->flag = 0;
    rcCell->txCount = 0;
    rcCell->failCount = 0;
    rcCell->currentRate = rcCell->operationRateSet[rcCell->currentRateIndex];
    rcCell->lasttxCount = 0;
    rcCell->lastTime    = wd->tick;
    rcCell->probingTime = wd->tick;
    for (i=0; i<ZM_RATE_TABLE_SIZE; i++) {
        wd->PER[i] = 0;
        wd->txMPDU[i] = wd->txFail[i] = 0;
    }
    wd->probeCount = 0;
    wd->probeInterval = 0;
#ifdef ZM_ENABLE_BA_RATECTRL
    for (i=0; i<29; i++) {
        TxMPDU[i]=0;
        BAFail[i]=0;
        BAPER[i]=0;
    }
#endif
    return;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfRateCtrlGetHigherRate     */
/*      Get a higher rate.                                              */
/*                                                                      */
/*    INPUTS                                                            */
/*      rcCell : rate control cell                                      */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      rate                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2007.2      */
/*                                                                      */
/************************************************************************/
u8_t zfRateCtrlGetHigherRate(struct zsRcCell* rcCell)
{
    u8_t rateIndex;

    rateIndex = rcCell->currentRateIndex
            + (((rcCell->currentRateIndex+1) < rcCell->operationRateCount)?1:0);
    return rcCell->operationRateSet[rateIndex];
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfRateCtrlNextLowerRate     */
/*      Get a lower rate.                                               */
/*                                                                      */
/*    INPUTS                                                            */
/*      rcCell : rate control cell                                      */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      rate                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2007.2      */
/*                                                                      */
/************************************************************************/
u8_t zfRateCtrlNextLowerRate(zdev_t* dev, struct zsRcCell* rcCell)
{
    zmw_get_wlan_dev(dev);
    if (rcCell->currentRateIndex > 0)
    {
        rcCell->currentRateIndex--;
        rcCell->currentRate = rcCell->operationRateSet[rcCell->currentRateIndex];
    }
    zm_msg1_tx(ZM_LV_0, "Lower Tx Rate=", rcCell->currentRate);
    //DbgPrint("Lower Tx Rate=%d", rcCell->currentRate);
    rcCell->failCount = rcCell->txCount = 0;
    rcCell->lasttxCount = 0;
    rcCell->lastTime  = wd->tick;
    return rcCell->currentRate;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfRateCtrlRateDiff          */
/*      Rate difference.                                                */
/*                                                                      */
/*    INPUTS                                                            */
/*      rcCell : rate control cell                                      */
/*      retryRate : retry rate                                          */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      rate difference                                                 */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2007.2      */
/*                                                                      */
/************************************************************************/
u8_t zfRateCtrlRateDiff(struct zsRcCell* rcCell, u8_t retryRate)
{
    u16_t i;

    /* Find retryRate in operationRateSet[] */
    for (i=0; i<rcCell->operationRateCount; i++)
    {
        if (retryRate == rcCell->operationRateSet[i])
        {
            if (i < rcCell->currentRateIndex)
            {
                return ((rcCell->currentRateIndex - i)+1)>>1;
            }
            else if (i == rcCell->currentRateIndex == 0)
            {
                return 1;
            }
            else
            {
                return 0;
            }
        }
    }
    /* TODO : retry rate not in operation rate set */
    zm_msg1_tx(ZM_LV_0, "Not in operation rate set:", retryRate);
    return 1;

}

u32_t zfRateCtrlUDPTP(zdev_t* dev, u16_t Rate, u32_t PER) {
    if ((PER < 100) && (Rate > 0) && PER)
        return 1168000/(((12304/Rate)+197)*(100+100*PER/(100-PER)));
    else
        return 0;
}

u8_t zfRateCtrlFindMaxUDPTP(zdev_t* dev, struct zsRcCell* rcCell) {
    u8_t i, maxIndex=0, rateIndex;
    u32_t max=0, UDPThroughput;

    zmw_get_wlan_dev(dev);

    rateIndex = zm_agg_min(rcCell->currentRateIndex+3, rcCell->operationRateCount-1);
    for (i=rcCell->currentRateIndex; i < rateIndex; i++) {
        UDPThroughput = zfRateCtrlUDPTP(dev, zcRate[rcCell->operationRateSet[i]],
            wd->PER[rcCell->operationRateSet[i]]);
        if (max < UDPThroughput) {
            max = UDPThroughput;
            maxIndex = i;
        }
    }

    return rcCell->operationRateSet[maxIndex];
}
/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfRateCtrlGetTxRate         */
/*      Get transmission rate.                                          */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      rcCell : rate control cell                                      */
/*      probing : rate probing flag                                     */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      Tx rate                                                         */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2007.2      */
/*                                                                      */
/************************************************************************/
u16_t zfRateCtrlGetTxRate(zdev_t* dev, struct zsRcCell* rcCell, u16_t* probing)
{
    u8_t newRate, highRate;
    zmw_get_wlan_dev(dev);

    zm_msg1_tx(ZM_LV_3, "txCount=", rcCell->txCount);
    zm_msg1_tx(ZM_LV_3, "probingTime=", rcCell->probingTime);
    zm_msg1_tx(ZM_LV_3, "tick=", wd->tick);
    *probing = 0;
    newRate = rcCell->currentRate;

    if (wd->probeCount && (wd->probeCount < wd->success_probing))
    {
        if (wd->probeInterval < 50)
        {
            wd->probeInterval++;
        }
        else
        {
            wd->probeInterval++;
            if (wd->probeInterval > 52) //probe 51, 52, 53 three packets every 50 packets
            {
                wd->probeInterval = 0;
            }
            newRate=zfRateCtrlGetHigherRate(rcCell);
            *probing = 1;
            wd->probeCount++;
            rcCell->probingTime = wd->tick;
        }
    }
    /* Accumulate at least 1000ms and 8 packets or Accumulate over 1K packets */
    else if ((((wd->tick - rcCell->probingTime) > (ZM_RATE_CTRL_PROBING_INTERVAL_MS/ZM_MS_PER_TICK))
                && (rcCell->txCount >= ZM_RATE_CTRL_MIN_PROBING_PACKET))
        || (rcCell->txCount >= 1000))
    {
#ifndef ZM_DISABLE_RATE_CTRL
        /* PER = fail/total */
        wd->probeCount = 0;
        wd->probeSuccessCount = 0;
        if (wd->txMPDU[rcCell->currentRate] != 0) {
            wd->PER[rcCell->currentRate] = zm_agg_min(100,
                (wd->txFail[rcCell->currentRate]*100)/wd->txMPDU[rcCell->currentRate]);
            if (!wd->PER[rcCell->currentRate]) wd->PER[rcCell->currentRate] ++;
        }

        /* if PER < threshold, do rate probing, return probing rate */
        if ((wd->PER[rcCell->currentRate] <= (ZM_RATE_PROBING_THRESHOLD+15)) ||
            ((rcCell->currentRate <= 16) &&
            ((wd->PER[rcCell->currentRate]/2) <= ZM_RATE_PROBING_THRESHOLD)))
        {
            newRate = zfRateCtrlGetHigherRate(rcCell);
            if (newRate != rcCell->currentRate)
            {
                *probing = 1;
                wd->probeCount++;
                wd->probeInterval = 0;
                wd->success_probing =
                    (rcCell->currentRate <= 16)? (ZM_RATE_SUCCESS_PROBING/2) : ZM_RATE_SUCCESS_PROBING;
                //DbgPrint("Start Probing");
                zm_msg1_tx(ZM_LV_0, "Probing Rate=", newRate);
            }
        }
#endif

        zm_msg0_tx(ZM_LV_1, "Diminish counter");
        rcCell->failCount = rcCell->failCount>>1;
        rcCell->txCount = rcCell->txCount>>1;
        wd->txFail[rcCell->currentRate] = wd->txFail[rcCell->currentRate] >> 1;
        wd->txMPDU[rcCell->currentRate] = wd->txMPDU[rcCell->currentRate] >> 1;


        if (rcCell->currentRate > 15) {
            highRate = zfRateCtrlGetHigherRate(rcCell);
            if ((highRate != rcCell->currentRate) && wd->PER[highRate] &&
                ((wd->PER[rcCell->currentRate] + FailDiff[rcCell->currentRate]) >
                wd->PER[highRate])) {
                //DbgPrint("PER compare force raise rate to %d", highRate);
                wd->probeSuccessCount = wd->probeCount = ZM_RATE_SUCCESS_PROBING;
                zfRateCtrlTxSuccessEvent(dev, rcCell, highRate);
            }
        }
        else {
            highRate = zfRateCtrlFindMaxUDPTP(dev, rcCell);
            if (rcCell->currentRate < highRate) {
                //DbgPrint("UDP Throughput compare force raise rate to %d", highRate);
                wd->probeSuccessCount = wd->probeCount = ZM_RATE_SUCCESS_PROBING;
                zfRateCtrlTxSuccessEvent(dev, rcCell, highRate);
            }
        }
        rcCell->probingTime = wd->tick;
    }

    if( (wd->tick > 1000)
        && ((wd->tick - rcCell->lastTime) > 3840) )
    {
        if (rcCell->lasttxCount < 70)
        {
            rcCell->failCount = rcCell->failCount>>1;
            rcCell->txCount = rcCell->txCount>>1;
            wd->txFail[rcCell->currentRate] = wd->txFail[rcCell->currentRate] >> 1;
            wd->txMPDU[rcCell->currentRate] = wd->txMPDU[rcCell->currentRate] >> 1;

            rcCell->failCount = (rcCell->failCount < rcCell->txCount)?
                                rcCell->failCount : rcCell->txCount;
            wd->txFail[rcCell->currentRate] = (wd->txFail[rcCell->currentRate] < wd->txMPDU[rcCell->currentRate])?
                                              wd->txFail[rcCell->currentRate] : wd->txMPDU[rcCell->currentRate];
        }

        rcCell->lastTime    = wd->tick;
        rcCell->lasttxCount = 0;
    }

    rcCell->txCount++;
    rcCell->lasttxCount++;
    wd->txMPDU[rcCell->currentRate]++;
    zm_msg1_tx(ZM_LV_1, "Get Tx Rate=", newRate);
    return newRate;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfRateCtrlTxFailEvent       */
/*      Tx fail event. Calculate PER and lower Tx rate if under         */
/*      PER under threshold.                                            */
/*                                                                      */
/*    INPUTS                                                            */
/*      rcCell : rate control cell                                      */
/*      retryRate : retry rate                                          */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      None                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2007.2      */
/*                                                                      */
/************************************************************************/
void zfRateCtrlTxFailEvent(zdev_t* dev, struct zsRcCell* rcCell, u8_t aggRate, u32_t retryRate)
{
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

#ifndef ZM_DISABLE_RATE_CTRL
    //DbgPrint("aggRate=%d, retryRate=%d", aggRate, retryRate);
    if (aggRate && (aggRate != rcCell->currentRate)) {
        wd->txFail[aggRate] += retryRate;
        return;
    }

    if (!aggRate) {
        retryRate = (zfRateCtrlRateDiff(rcCell, (u8_t)retryRate)+1)>>1;
        if (rcCell->currentRate <12) //legacy rate
        {
            retryRate*=2;
        }
    }
    rcCell->failCount += retryRate;
    wd->txFail[rcCell->currentRate] += retryRate;

    //DbgPrint("failCount=%d", rcCell->failCount);
    if (rcCell->failCount > ZM_MIN_RATE_FAIL_COUNT)
    {
        if (wd->txMPDU[rcCell->currentRate] != 0) {
            wd->PER[rcCell->currentRate] = zm_agg_min(100,
                (wd->txFail[rcCell->currentRate]*100)/wd->txMPDU[rcCell->currentRate]);
            if (!wd->PER[rcCell->currentRate]) wd->PER[rcCell->currentRate] ++;
        }
        //zm_msg1_tx(ZM_LV_1, "PER=", per);
        //DbgPrint("PER=%d, txFail=%d, txMPDU=%d", wd->PER[rcCell->currentRate], wd->txFail[rcCell->currentRate], wd->txMPDU[rcCell->currentRate]);
        if (wd->PER[rcCell->currentRate] > PERThreshold[rcCell->currentRate])
        {
            /* Lower Tx Rate if PER < THRESHOLD */
            zfRateCtrlNextLowerRate(dev, rcCell);
            rcCell->flag |= ZM_RC_TRAINED_BIT;

            // Resolve compatibility problem with Marvell
            if(rcCell->currentRate == 15)
            {
                zmw_leave_critical_section(dev);
                zfHpSetAggPktNum(dev, 8);
                zmw_enter_critical_section(dev);
            }

            wd->txFail[rcCell->currentRate] = wd->txFail[rcCell->currentRate] >> 1;
            wd->txMPDU[rcCell->currentRate] = wd->txMPDU[rcCell->currentRate] >> 1;

            wd->probeCount = wd->probeSuccessCount = 0;
        }
    }

#endif
    return;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfRateCtrlTxSuccessEvent    */
/*      Tx success event. Raise Tx rate because rate probing success.   */
/*                                                                      */
/*    INPUTS                                                            */
/*      rcCell : rate control cell                                      */
/*      successRate : success rate                                      */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      None                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2007.2      */
/*                                                                      */
/************************************************************************/
void zfRateCtrlTxSuccessEvent(zdev_t* dev, struct zsRcCell* rcCell, u8_t successRate)
{
    /* Raise Tx Rate */
    u16_t i, PERProbe;
    u16_t pcount;
    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    //DbgPrint("Probing successRate=%d", successRate);
    /* Find successRate in operationRateSet[] */
    wd->probeSuccessCount++;
    if (wd->probeCount < wd->success_probing)
    {
        return;
    }

    pcount = wd->probeCount;
    if (pcount != 0)
    {
        PERProbe = wd->probeSuccessCount * 100 / pcount;
    }
    else
    {
        PERProbe = 1;
    }

    if (PERProbe < ((rcCell->currentRate < 16)? 80:100))
    {
        return;
    }
    //DbgPrint("wd->probeCount=%d, wd->probeSuccessCount=%d", wd->probeCount, wd->probeSuccessCount);
    wd->probeCount = wd->probeSuccessCount = 0;
    for (i=0; i<rcCell->operationRateCount; i++)
    {
        if (successRate == rcCell->operationRateSet[i])
        {
            if (i > rcCell->currentRateIndex)
            {
                /* Raise current Tx rate */
                zm_msg1_tx(ZM_LV_0, "Raise Tx Rate=", successRate);
                //DbgPrint("Raise Tx Rate=%d", successRate);

                // Resolve compatibility problem with Marvell
                if((rcCell->currentRate <= 15) && (successRate > 15))
                {
                    zmw_leave_critical_section(dev);
                    zfHpSetAggPktNum(dev, 16);
                    zmw_enter_critical_section(dev);
                }

                rcCell->currentRate = successRate;
                rcCell->currentRateIndex = (u8_t)i;
                rcCell->failCount = rcCell->txCount = 0;
                rcCell->lasttxCount = 0;
                rcCell->lastTime  = wd->tick;
                wd->txFail[rcCell->currentRate] = wd->txFail[rcCell->currentRate] >> 1;
                wd->txMPDU[rcCell->currentRate] = wd->txMPDU[rcCell->currentRate] >> 1;
            }
        }
    }

    return;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfRateCtrlRxRssiEvent       */
/*      Rx RSSI event. Calculate RSSI moving average, accelarate        */
/*      rate probing if RSSI variation over threshold.                  */
/*                                                                      */
/*    INPUTS                                                            */
/*      rcCell : rate control cell                                      */
/*      successRate : success rate                                      */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      None                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2007.2      */
/*                                                                      */
/************************************************************************/
void zfRateCtrlRxRssiEvent(struct zsRcCell* rcCell, u16_t rxRssi)
{
    /* if delta(rcCell->rxRssi, rxRssi) > ZM_RATE_CTRL_RSSI_VARIATION */
    if ((rcCell->rxRssi - rxRssi) > ZM_RATE_CTRL_RSSI_VARIATION)
    {
        /* Accelerate rate probing via decreaing rcCell->probingTime */
        rcCell->probingTime -= ZM_RATE_CTRL_PROBING_INTERVAL_MS/ZM_MS_PER_TICK;
    }

    /* Update RSSI moving average */
    rcCell->rxRssi = (((rcCell->rxRssi*7) + rxRssi)+4) >> 3;
    return;
}


#ifdef ZM_ENABLE_BA_RATECTRL
u8_t HigherRate(u8_t Rate) {
    if (Rate < 28) Rate++; //28=MCS15SG, 27=MCS15, 26=MCS14, 25=MCS13
    if (Rate > 28) Rate = 28;
    while ((Rate >= 20) && (Rate <= 23)) {
        Rate ++;
    }
    return Rate;
}

u8_t LowerRate(u8_t Rate) {
    if (Rate > 1) Rate--;
    while ((Rate >= 20) && (Rate <= 23)) {
        Rate --;
    }
    return Rate;
}

u8_t RateMapToRateIndex(u8_t Rate, struct zsRcCell* rcCell) {
    u8_t i;
    for (i=0; i<rcCell->operationRateCount; i++) {
        if (Rate == rcCell->operationRateSet[i]) {
            return i;
        }
    }
    return 0;
}

void zfRateCtrlAggrSta(zdev_t* dev) {
    u8_t RateIndex, Rate;
    u8_t HRate;
    u8_t LRate;
    u32_t RateCtrlTxMPDU, RateCtrlBAFail;
    zmw_get_wlan_dev(dev);

    RateIndex = wd->sta.oppositeInfo[0].rcCell.currentRateIndex;
    Rate = wd->sta.oppositeInfo[0].rcCell.operationRateSet[RateIndex];

    TxMPDU[Rate] = (TxMPDU[Rate] / 5) + (wd->commTally.RateCtrlTxMPDU * 4 / 5);
    BAFail[Rate] = (BAFail[Rate] / 5) + (wd->commTally.RateCtrlBAFail * 4 / 5);
    RateCtrlTxMPDU = wd->commTally.RateCtrlTxMPDU;
    RateCtrlBAFail = wd->commTally.RateCtrlBAFail;
    wd->commTally.RateCtrlTxMPDU = 0;
    wd->commTally.RateCtrlBAFail = 0;
    if (TxMPDU[Rate] > 0) {
        BAPER[Rate] = BAFail[Rate] * 1000 / TxMPDU[Rate]; //PER*1000
        BAPER[Rate] = (BAPER[Rate]>0)? BAPER[Rate]:1;
    }
    else {
        return;
    }

    HRate = HigherRate(Rate);
    LRate = LowerRate(Rate);
    if (BAPER[Rate]>200) {
        if ((RateCtrlTxMPDU > 100) && (BAPER[Rate]<300) && (HRate != Rate) && BAPER[HRate] &&
            (BAPER[HRate] < BAPER[Rate] + BADiff[Rate])) {
            Rate = HRate;
            //DbgPrint("Rate improved to %d", Rate);
        }
        else {
            Rate = LRate;
            //DbgPrint("Rate decreased to %d", Rate);
        }
    }
    else if (BAPER[Rate] && BAPER[Rate]<100) {
        if (RateCtrlTxMPDU > 100) {
            Rate = HRate;
            //DbgPrint("Rate improved to %d", Rate);
        }
    }
    wd->sta.oppositeInfo[0].rcCell.currentRate = Rate;
    wd->sta.oppositeInfo[0].rcCell.currentRateIndex = RateMapToRateIndex(Rate, &wd->sta.oppositeInfo[0].rcCell);
}
#endif
