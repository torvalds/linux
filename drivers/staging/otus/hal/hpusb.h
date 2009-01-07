/*
 * Copyright (c) 2000-2005 ZyDAS Technology Corporation
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
/*  Module Name : ud_defs.h                                             */
/*                                                                      */
/*  Abstract                                                            */
/*      This module contains USB data structure definitions.            */
/*                                                                      */
/*  NOTES                                                               */
/*      None                                                            */
/*                                                                      */
/************************************************************************/

#ifndef _HPUSB_H
#define _HPUSB_H

#define ZM_OTUS_ENABLE_RETRY_FREQ_CHANGE
#define ZM_BEACON_BUFFER_ADDRESS            0x117900

#define ZM_MAX_CMD_SIZE                     64
#define ZM_HAL_MAX_EEPROM_REQ               510
#define ZM_HAL_MAX_EEPROM_PRQ               2

/* For USB STREAM mode */
#ifdef ZM_DISABLE_AMSDU8K_SUPPORT
#define ZM_MAX_USB_IN_TRANSFER_SIZE         4096
#else
#define ZM_MAX_USB_IN_TRANSFER_SIZE         8192
#endif
#define ZM_USB_STREAM_MODE_TAG_LEN          4
#define ZM_USB_STREAM_MODE_TAG              0x4e00
#define ZM_USB_MAX_EPINT_BUFFER             64

struct zsCmdQ
{
    u16_t src;
    u16_t cmdLen;
    u8_t* buf;
    u32_t cmd[ZM_MAX_CMD_SIZE/4];
};

struct zsCommand
{
    u16_t delayWcmdCount;
    u32_t delayWcmdAddr[(ZM_CMD_QUEUE_SIZE-4)/4];
    u32_t delayWcmdVal[(ZM_CMD_QUEUE_SIZE-4)/4];
};

struct zsHalRxInfo
{
    u32_t currentRSSI[7];       /* RSSI combined */
    u32_t currentRxEVM[14];
    u32_t currentRxDataMT;
    u32_t currentRxDataMCS;
    u32_t currentRxDataBW;
    u32_t currentRxDataSG;
};

struct zsHpPriv
{
    u16_t hwFrequency;
    u8_t  hwBw40;
    u8_t  hwExtOffset;

    u8_t  disableDfsCh;

    u32_t halCapability;

    /* Fortunately the second loop can be disabled with a bit */
    /* called en_pd_dc_offset_thr                             */
    u8_t hwNotFirstInit;

    /* command queue */
    u16_t               cmdHead;
    u16_t               cmdTail;
#ifdef ZM_XP_USB_MULTCMD
    u16_t               cmdSend;  // Used for Mult send USB cmd
#endif
    struct zsCmdQ       cmdQ[ZM_CMD_QUEUE_SIZE];
    u16_t               cmdPending;
    struct zsCommand    cmd; /* buffer for delayed commands */
    u8_t                ledMode[2];
    u32_t               ctlBusy;
    u32_t               extBusy;

    /*
     * ANI & Radar support.
     */
    u32_t   procPhyErr;         /* Process Phy errs */
    u8_t hasHwPhyCounters;   /* Hardware has phy counters */
    u32_t   aniPeriod;          /* ani update list period */
    struct zsAniStats   stats;      /* various statistics */
    struct zsAniState   *curani;    /* cached last reference */
    struct zsAniState   ani[50];   /* per-channel state */

    /*
     * Ani tables that change between the 5416 and 5312.
     * These get set at attach time.
     * XXX don't belong here
     * XXX need better explanation
     */
    s32_t     totalSizeDesired[5];
    s32_t     coarseHigh[5];
    s32_t     coarseLow[5];
    s32_t     firpwr[5];

    /*
     * ANI related PHY register value.
     */
    u32_t regPHYDesiredSZ;
    u32_t regPHYFindSig;
    u32_t regPHYAgcCtl1;
    u32_t regPHYSfcorr;
    u32_t regPHYSfcorrLow;
    u32_t regPHYTiming5;
    u32_t regPHYCckDetect;

    u32_t eepromImage[1024];
    u32_t eepromImageIndex;
    u32_t eepromImageRdReq;

    u8_t  halReInit;

    u8_t  OpFlags;

    u8_t tPow2xCck[4];
    u8_t tPow2x2g[4];
    u8_t tPow2x2g24HeavyClipOffset;
    u8_t tPow2x2gHt20[8];
    u8_t tPow2x2gHt40[8];
    u8_t tPow2x5g[4];
    u8_t tPow2x5gHt20[8];
    u8_t tPow2x5gHt40[8];

    /* hwBBHeavyClip : used compatibility           */
    /*             0 : dongle not support.          */
    /*             !0: support heavy clip.          */
    u8_t hwBBHeavyClip;
    u8_t enableBBHeavyClip; /* 0=>force disable 1=>enable */
    u8_t doBBHeavyClip;     /* set 1 if heavy clip need by each frequency switch */
    u32_t setValueHeavyClip; /* save setting value for heavy clip when completed routine */

    /*
     * Rxdata RSSI, EVM, Rate etc...
     */
    struct zsHalRxInfo halRxInfo;

    u32_t usbSendBytes;
    u32_t usbAcSendBytes[4];

    u16_t aggMaxDurationBE;
    u32_t aggPktNum;

    u16_t txop[4];
    u16_t cwmin[4];
    u16_t cwmax[4];
    u8_t  strongRSSI;
    u8_t  rxStrongRSSI;

    u8_t  slotType;  //0->20us, 1=>9us

#ifdef ZM_OTUS_RX_STREAM_MODE
    u16_t usbRxRemainLen;
    u16_t usbRxPktLen;
    u16_t usbRxPadLen;
    u16_t usbRxTransferLen;
    zbuf_t  *remainBuf;
#endif

    u8_t    dot11Mode;

    u8_t    ibssBcnEnabled;
    u32_t   ibssBcnInterval;

    // For re-issue the frequency change command
    u32_t   latestFrequency;
    u8_t    latestBw40;
    u8_t    latestExtOffset;
    u8_t    freqRetryCounter;

    u8_t    recordFreqRetryCounter;
    u8_t    isSiteSurvey;
    u8_t    coldResetNeedFreq;

    u64_t   camRollCallTable;
    u8_t    currentAckRtsTpc;

    /* #1 Save the initial value of the related RIFS register settings */
    //u32_t   isInitialPhy;
    u32_t   initDesiredSigSize;
    u32_t   initAGC;
    u32_t   initAgcControl;
    u32_t   initSearchStartDelay;
    u32_t   initRIFSSearchParams;
    u32_t   initFastChannelChangeControl;

    /* Dynamic SIFS for retransmission event */
    u8_t    retransmissionEvent;
    u8_t    latestSIFS;
};

extern u32_t zfHpLoadEEPROMFromFW(zdev_t* dev);


typedef u8_t A_UINT8;
typedef s8_t A_INT8;
typedef u16_t A_UINT16;
typedef u32_t A_UINT32;
#define __ATTRIB_PACK

#pragma pack (push, 1)

#define AR5416_EEP_VER               0xE
#define AR5416_EEP_VER_MINOR_MASK    0xFFF
#define AR5416_EEP_NO_BACK_VER       0x1
#define AR5416_EEP_MINOR_VER_2       0x2  // Adds modal params txFrameToPaOn, txFrametoDataStart, ht40PowerInc
#define AR5416_EEP_MINOR_VER_3       0x3  // Adds modal params bswAtten, bswMargin, swSettle and base OpFlags for HT20/40 Disable

// 16-bit offset location start of calibration struct
#define AR5416_EEP_START_LOC         256
#define AR5416_NUM_5G_CAL_PIERS      8
#define AR5416_NUM_2G_CAL_PIERS      4
#define AR5416_NUM_5G_20_TARGET_POWERS  8
#define AR5416_NUM_5G_40_TARGET_POWERS  8
#define AR5416_NUM_2G_CCK_TARGET_POWERS 3
#define AR5416_NUM_2G_20_TARGET_POWERS  4
#define AR5416_NUM_2G_40_TARGET_POWERS  4
#define AR5416_NUM_CTLS              24
#define AR5416_NUM_BAND_EDGES        8
#define AR5416_NUM_PD_GAINS          4
#define AR5416_PD_GAINS_IN_MASK      4
#define AR5416_PD_GAIN_ICEPTS        5
#define AR5416_EEPROM_MODAL_SPURS    5
#define AR5416_MAX_RATE_POWER        63
#define AR5416_NUM_PDADC_VALUES      128
#define AR5416_NUM_RATES             16
#define AR5416_BCHAN_UNUSED          0xFF
#define AR5416_MAX_PWR_RANGE_IN_HALF_DB 64
#define AR5416_OPFLAGS_11A           0x01
#define AR5416_OPFLAGS_11G           0x02
#define AR5416_OPFLAGS_5G_HT40       0x04
#define AR5416_OPFLAGS_2G_HT40       0x08
#define AR5416_OPFLAGS_5G_HT20       0x10
#define AR5416_OPFLAGS_2G_HT20       0x20
#define AR5416_EEPMISC_BIG_ENDIAN    0x01
#define FREQ2FBIN(x,y) ((y) ? ((x) - 2300) : (((x) - 4800) / 5))
#define AR5416_MAX_CHAINS            2
#define AR5416_ANT_16S               25

#define AR5416_NUM_ANT_CHAIN_FIELDS     7
#define AR5416_NUM_ANT_COMMON_FIELDS    4
#define AR5416_SIZE_ANT_CHAIN_FIELD     3
#define AR5416_SIZE_ANT_COMMON_FIELD    4
#define AR5416_ANT_CHAIN_MASK           0x7
#define AR5416_ANT_COMMON_MASK          0xf
#define AR5416_CHAIN_0_IDX              0
#define AR5416_CHAIN_1_IDX              1
#define AR5416_CHAIN_2_IDX              2


/* Capabilities Enum */
typedef enum {
    EEPCAP_COMPRESS_DIS  = 0x0001,
    EEPCAP_AES_DIS       = 0x0002,
    EEPCAP_FASTFRAME_DIS = 0x0004,
    EEPCAP_BURST_DIS     = 0x0008,
    EEPCAP_MAXQCU_M      = 0x01F0,
    EEPCAP_MAXQCU_S      = 4,
    EEPCAP_HEAVY_CLIP_EN = 0x0200,
    EEPCAP_KC_ENTRIES_M  = 0xF000,
    EEPCAP_KC_ENTRIES_S  = 12,
} EEPROM_CAPABILITIES;

typedef enum Ar5416_Rates {
    rate6mb,  rate9mb,  rate12mb, rate18mb,
    rate24mb, rate36mb, rate48mb, rate54mb,
    rate1l,   rate2l,   rate2s,   rate5_5l,
    rate5_5s, rate11l,  rate11s,  rateXr,
    rateHt20_0, rateHt20_1, rateHt20_2, rateHt20_3,
    rateHt20_4, rateHt20_5, rateHt20_6, rateHt20_7,
    rateHt40_0, rateHt40_1, rateHt40_2, rateHt40_3,
    rateHt40_4, rateHt40_5, rateHt40_6, rateHt40_7,
    rateDupCck, rateDupOfdm, rateExtCck, rateExtOfdm,
    Ar5416RateSize
} AR5416_RATES;

typedef struct eepFlags {
    A_UINT8  opFlags;
    A_UINT8  eepMisc;
} __ATTRIB_PACK EEP_FLAGS;

#define AR5416_CHECKSUM_LOCATION (AR5416_EEP_START_LOC + 1)
typedef struct BaseEepHeader {
    A_UINT16  length;
    A_UINT16  checksum;
    A_UINT16  version;
    EEP_FLAGS opCapFlags;
    A_UINT16  regDmn[2];
    A_UINT8   macAddr[6];
    A_UINT8   rxMask;
    A_UINT8   txMask;
    A_UINT16  rfSilent;
    A_UINT16  blueToothOptions;
    A_UINT16  deviceCap;
    A_UINT32  binBuildNumber;
    A_UINT8   deviceType;
    A_UINT8   futureBase[33];
} __ATTRIB_PACK BASE_EEP_HEADER; // 64 B

typedef struct spurChanStruct {
    A_UINT16 spurChan;
    A_UINT8  spurRangeLow;
    A_UINT8  spurRangeHigh;
} __ATTRIB_PACK SPUR_CHAN;

typedef struct ModalEepHeader {
    A_UINT32  antCtrlChain[AR5416_MAX_CHAINS];       // 12
    A_UINT32  antCtrlCommon;                         // 4
    A_INT8    antennaGainCh[AR5416_MAX_CHAINS];      // 3
    A_UINT8   switchSettling;                        // 1
    A_UINT8   txRxAttenCh[AR5416_MAX_CHAINS];        // 3
    A_UINT8   rxTxMarginCh[AR5416_MAX_CHAINS];       // 3
    A_INT8    adcDesiredSize;                        // 1
    A_INT8    pgaDesiredSize;                        // 1
    A_UINT8   xlnaGainCh[AR5416_MAX_CHAINS];         // 3
    A_UINT8   txEndToXpaOff;                         // 1
    A_UINT8   txEndToRxOn;                           // 1
    A_UINT8   txFrameToXpaOn;                        // 1
    A_UINT8   thresh62;                              // 1
    A_INT8    noiseFloorThreshCh[AR5416_MAX_CHAINS]; // 3
    A_UINT8   xpdGain;                               // 1
    A_UINT8   xpd;                                   // 1
    A_INT8    iqCalICh[AR5416_MAX_CHAINS];           // 1
    A_INT8    iqCalQCh[AR5416_MAX_CHAINS];           // 1
    A_UINT8   pdGainOverlap;                         // 1
    A_UINT8   ob;                                    // 1
    A_UINT8   db;                                    // 1
    A_UINT8   xpaBiasLvl;                            // 1
    A_UINT8   pwrDecreaseFor2Chain;                  // 1
    A_UINT8   pwrDecreaseFor3Chain;                  // 1 -> 48 B
    A_UINT8   txFrameToDataStart;                    // 1
    A_UINT8   txFrameToPaOn;                         // 1
    A_UINT8   ht40PowerIncForPdadc;                  // 1
    A_UINT8   bswAtten[AR5416_MAX_CHAINS];           // 3
    A_UINT8   bswMargin[AR5416_MAX_CHAINS];          // 3
    A_UINT8   swSettleHt40;                          // 1
    A_UINT8   futureModal[22];                       //
    SPUR_CHAN spurChans[AR5416_EEPROM_MODAL_SPURS];  // 20 B
} __ATTRIB_PACK MODAL_EEP_HEADER;                    // == 100 B

typedef struct calDataPerFreq {
    A_UINT8 pwrPdg[AR5416_NUM_PD_GAINS][AR5416_PD_GAIN_ICEPTS];
    A_UINT8 vpdPdg[AR5416_NUM_PD_GAINS][AR5416_PD_GAIN_ICEPTS];
} __ATTRIB_PACK CAL_DATA_PER_FREQ;

typedef struct CalTargetPowerLegacy {
    A_UINT8  bChannel;
    A_UINT8  tPow2x[4];
} __ATTRIB_PACK CAL_TARGET_POWER_LEG;

typedef struct CalTargetPowerHt {
    A_UINT8  bChannel;
    A_UINT8  tPow2x[8];
} __ATTRIB_PACK CAL_TARGET_POWER_HT;

#if defined(ARCH_BIG_ENDIAN) || defined(BIG_ENDIAN)
typedef struct CalCtlEdges {
    A_UINT8  bChannel;
    A_UINT8  flag   :2,
             tPower :6;
} __ATTRIB_PACK CAL_CTL_EDGES;
#else
typedef struct CalCtlEdges {
    A_UINT8  bChannel;
    A_UINT8  tPower :6,
             flag   :2;
} __ATTRIB_PACK CAL_CTL_EDGES;
#endif

typedef struct CalCtlData {
    CAL_CTL_EDGES  ctlEdges[AR5416_MAX_CHAINS][AR5416_NUM_BAND_EDGES];
} __ATTRIB_PACK CAL_CTL_DATA;

typedef struct ar5416Eeprom {
    BASE_EEP_HEADER    baseEepHeader;         // 64 B
    A_UINT8   custData[64];                   // 64 B
    MODAL_EEP_HEADER   modalHeader[2];        // 200 B
    A_UINT8            calFreqPier5G[AR5416_NUM_5G_CAL_PIERS];
    A_UINT8            calFreqPier2G[AR5416_NUM_2G_CAL_PIERS];
    CAL_DATA_PER_FREQ  calPierData5G[AR5416_MAX_CHAINS][AR5416_NUM_5G_CAL_PIERS];
    CAL_DATA_PER_FREQ  calPierData2G[AR5416_MAX_CHAINS][AR5416_NUM_2G_CAL_PIERS];
    CAL_TARGET_POWER_LEG calTargetPower5G[AR5416_NUM_5G_20_TARGET_POWERS];
    CAL_TARGET_POWER_HT  calTargetPower5GHT20[AR5416_NUM_5G_20_TARGET_POWERS];
    CAL_TARGET_POWER_HT  calTargetPower5GHT40[AR5416_NUM_5G_40_TARGET_POWERS];
    CAL_TARGET_POWER_LEG calTargetPowerCck[AR5416_NUM_2G_CCK_TARGET_POWERS];
    CAL_TARGET_POWER_LEG calTargetPower2G[AR5416_NUM_2G_20_TARGET_POWERS];
    CAL_TARGET_POWER_HT  calTargetPower2GHT20[AR5416_NUM_2G_20_TARGET_POWERS];
    CAL_TARGET_POWER_HT  calTargetPower2GHT40[AR5416_NUM_2G_40_TARGET_POWERS];
    A_UINT8            ctlIndex[AR5416_NUM_CTLS];
    CAL_CTL_DATA       ctlData[AR5416_NUM_CTLS];
    A_UINT8            padding;
} __ATTRIB_PACK AR5416_EEPROM;

#pragma pack (pop)

typedef enum ConformanceTestLimits {
    FCC        = 0x10,
    MKK        = 0x40,
    ETSI       = 0x30,
    SD_NO_CTL  = 0xE0,
    NO_CTL     = 0xFF,
    CTL_MODE_M = 0xF,
    CTL_11A    = 0,
    CTL_11B    = 1,
    CTL_11G    = 2,
    CTL_TURBO  = 3,
    CTL_108G   = 4,
    CTL_2GHT20 = 5,
    CTL_5GHT20 = 6,
    CTL_2GHT40 = 7,
    CTL_5GHT40 = 8,
} ATH_CTLS;

#endif /* #ifndef _HPUSB_H */
