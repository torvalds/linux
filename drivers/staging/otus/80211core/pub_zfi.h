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

#ifndef _PUB_DEFS_H
#define _PUB_DEFS_H

#include "../oal_dt.h"

/***** Section 1 : Tunable Parameters *****/
/* The defintions in this section are tunabel parameters */

/* Maximum number of BSS that could be scaned */
#define ZM_MAX_BSS                          128

/* Maximum number of WPA2 PMKID that supported */
#define ZM_PMKID_MAX_BSS_CNT               8

/* Enable aggregation and deaggregation */
#define ZM_ENABLE_AGGREGATION

#ifdef ZM_ENABLE_AGGREGATION
    /* Enable BA failed retransmission in firmware */
    #define ZM_ENABLE_FW_BA_RETRANSMISSION
    #define ZM_BYPASS_AGGR_SCHEDULING
    //#define ZM_AGGR_BIT_ON
#endif


#ifndef ZM_FB50
//#define ZM_FB50
#endif

#ifndef ZM_AP_DEBUG
//#define ZM_AP_DEBUG
#endif

//#define ZM_ENABLE_BA_RATECTRL

/***** End of section 1 *****/


/***** Section 2 : Public Definitions, data structures and prototypes *****/
/* function return status */
#define ZM_STATUS_SUCCESS                   0
#define ZM_STATUS_FAILURE                   1

// media connect status
#define ZM_STATUS_MEDIA_CONNECT             0x00
#define ZM_STATUS_MEDIA_DISCONNECT          0x01
#define ZM_STATUS_MEDIA_DISCONNECT_NOT_FOUND    0x02
#define ZM_STATUS_MEDIA_DISABLED            0x03
#define ZM_STATUS_MEDIA_CONNECTION_DISABLED 0x04
#define ZM_STATUS_MEDIA_CONNECTION_RESET    0x05
#define ZM_STATUS_MEDIA_RESET               0x06
#define ZM_STATUS_MEDIA_DISCONNECT_DEAUTH   0x07
#define ZM_STATUS_MEDIA_DISCONNECT_DISASOC  0x08
#define ZM_STATUS_MEDIA_DISCONNECT_TIMEOUT  0x09
#define ZM_STATUS_MEDIA_DISCONNECT_AUTH_FAILED  0x0a
#define ZM_STATUS_MEDIA_DISCONNECT_ASOC_FAILED  0x0b
#define ZM_STATUS_MEDIA_DISCONNECT_MIC_FAIL   0x0c
#define ZM_STATUS_MEDIA_DISCONNECT_UNREACHABLE 0x0d
#define ZM_STATUS_MEDIA_DISCONNECT_BEACON_MISS  0x0e

// Packet Filter
#define ZM_PACKET_TYPE_DIRECTED             0x00000001
#define ZM_PACKET_TYPE_MULTICAST            0x00000002
#define ZM_PACKET_TYPE_ALL_MULTICAST        0x00000004
#define ZM_PACKET_TYPE_BROADCAST            0x00000008
#define ZM_PACKET_TYPE_PROMISCUOUS          0x00000020

/* BSS mode definition */
/* TODO : The definitions here are coupled with XP's NDIS OID. */
/*        We can't be changed them freely, need to disarm this mine */
#define ZM_MODE_IBSS                        0
#define ZM_MODE_INFRASTRUCTURE              1
#define ZM_MODE_UNKNOWN                     2
#define ZM_MODE_INFRASTRUCTURE_MAX          3
#define ZM_MODE_AP                          4
#define ZM_MODE_PSEUDO                      5


/* Authentication mode */
#define ZM_AUTH_MODE_OPEN                   0
#define ZM_AUTH_MODE_SHARED_KEY             1
#define ZM_AUTH_MODE_AUTO                   2
#define ZM_AUTH_MODE_WPA                    3
#define ZM_AUTH_MODE_WPAPSK                 4
#define ZM_AUTH_MODE_WPA_NONE               5
#define ZM_AUTH_MODE_WPA2                   6
#define ZM_AUTH_MODE_WPA2PSK                7
#ifdef ZM_ENABLE_CENC
#define ZM_AUTH_MODE_CENC                   8
#endif //ZM_ENABLE_CENC
#define ZM_AUTH_MODE_WPA_AUTO               9
#define ZM_AUTH_MODE_WPAPSK_AUTO            10

// Encryption mode
#define ZM_NO_WEP                           0x0
#define ZM_AES                              0x4
#define ZM_TKIP                             0x2
#define ZM_WEP64                            0x1
#define ZM_WEP128                           0x5
#define ZM_WEP256                           0x6
#ifdef ZM_ENABLE_CENC
#define ZM_CENC                             0x7
#endif //ZM_ENABLE_CENC

/* Encryption type for wep status */
#define ZM_ENCRYPTION_WEP_DISABLED          0
#define ZM_ENCRYPTION_WEP_ENABLED           1
#define ZM_ENCRYPTION_WEP_KEY_ABSENT        2
#define ZM_ENCRYPTION_NOT_SUPPORTED         3
#define ZM_ENCRYPTION_TKIP                  4
#define ZM_ENCRYPTION_TKIP_KEY_ABSENT       5
#define ZM_ENCRYPTION_AES                   6
#define ZM_ENCRYPTION_AES_KEY_ABSENT        7

#ifdef ZM_ENABLE_CENC
#define ZM_ENCRYPTION_CENC                  8
#endif //ZM_ENABLE_CENC

/* security type */
#define ZM_SECURITY_TYPE_NONE               0
#define ZM_SECURITY_TYPE_WEP                1
#define ZM_SECURITY_TYPE_WPA                2

#ifdef ZM_ENABLE_CENC
#define ZM_SECURITY_TYPE_CENC               3
#endif //ZM_ENABLE_CENC

/* Encryption Exemption Action Type  */
#define ZM_ENCRYPTION_EXEMPT_NO_EXEMPTION   0
#define ZM_ENCRYPTION_EXEMPT_ALWAYS         1

/* MIC failure */
#define ZM_MIC_PAIRWISE_ERROR               0x06
#define ZM_MIC_GROUP_ERROR                  0x0E


/* power save mode */
#define ZM_STA_PS_NONE                    0
#define ZM_STA_PS_MAX                     1
#define ZM_STA_PS_FAST                    2
#define ZM_STA_PS_LIGHT                   3

/* WME AC Type */
#define ZM_WME_AC_BK                        0       /* Background AC */
#define ZM_WME_AC_BE                        1       /* Best-effort AC */
#define ZM_WME_AC_VIDEO                     2       /* Video AC */
#define ZM_WME_AC_VOICE                     3       /* Voice AC */

/* Preamble type */
#define ZM_PREAMBLE_TYPE_AUTO               0
#define ZM_PREAMBLE_TYPE_LONG               1
#define ZM_PREAMBLE_TYPE_SHORT              2

/* wireless modes constants */
#define ZM_WIRELESS_MODE_5_54        0x01   ///< 5 GHz 54 Mbps
#define ZM_WIRELESS_MODE_5_108       0x02   ///< 5 GHz 108 Mbps
#define ZM_WIRELESS_MODE_24_11       0x04   ///< 2.4 GHz 11 Mbps
#define ZM_WIRELESS_MODE_24_54       0x08   ///< 2.4 GHz 54 Mbps
#define ZM_WIRELESS_MODE_24_108      0x10   ///< 2.4 GHz 108 Mbps
#define ZM_WIRELESS_MODE_49_13      0x100   ///< 4.9 GHz 13.5 Mbps, quarter rate chn-bandwidth = 5
#define ZM_WIRELESS_MODE_49_27      0x200   ///< 4.9 GHz 27 Mbps, half rate chn-bandwidth = 10
#define ZM_WIRELESS_MODE_49_54      0x400   ///< 4.9 GHz 54 Mbps, full rate chn-bandwidth = 20
#define ZM_WIRELESS_MODE_5_300     0x1000   ///< 5 GHz 300 Mbps
#define ZM_WIRELESS_MODE_24_300    0x2000   ///< 2.4 GHz 300 Mbps
#define ZM_WIRELESS_MODE_5_130     0x4000   ///< 5 GHz 130 Mbps
#define ZM_WIRELESS_MODE_24_130    0x8000   ///< 2.4 GHz 130 Mbps

#define ZM_WIRELESS_MODE_24_N      (ZM_WIRELESS_MODE_24_130|ZM_WIRELESS_MODE_24_300)
#define ZM_WIRELESS_MODE_5_N       (ZM_WIRELESS_MODE_5_130|ZM_WIRELESS_MODE_5_300)
#define ZM_WIRELESS_MODE_24        (ZM_WIRELESS_MODE_24_11|ZM_WIRELESS_MODE_24_54|ZM_WIRELESS_MODE_24_N)
#define ZM_WIRELESS_MODE_5         (ZM_WIRELESS_MODE_5_54|ZM_WIRELESS_MODE_5_N)

/* AdHoc Mode with different band */
#define ZM_ADHOCBAND_A         1
#define ZM_ADHOCBAND_B         2
#define ZM_ADHOCBAND_G         3
#define ZM_ADHOCBAND_BG        4
#define ZM_ADHOCBAND_ABG       5

/* Authentication algorithm in the field algNo of authentication frames */
#define ZM_AUTH_ALGO_OPEN_SYSTEM      0x10000    /* Open system */
#define ZM_AUTH_ALGO_SHARED_KEY       0x10001    /* Shared Key */
#define ZM_AUTH_ALGO_LEAP             0x10080    /* Leap */

struct zsScanResult
{
    u32_t reserved;
};


struct zsStastics
{
    u32_t reserved;
};

#define ZM_MAX_SUPP_RATES_IE_SIZE       12
#define ZM_MAX_IE_SIZE                  50 //100
#define ZM_MAX_WPS_IE_SIZE              150
#define ZM_MAX_PROBE_FRAME_BODY_SIZE    512//300
#define	ZM_MAX_COUNTRY_INFO_SIZE		20

#define ZM_MAX_SSID_LENGTH          32
struct zsBssInfo
{
    u8_t   macaddr[6];
    u8_t   bssid[6];
    u8_t   beaconInterval[2];
    u8_t   capability[2];
    u8_t   timeStamp[8];
    u8_t   ssid[ZM_MAX_SSID_LENGTH + 2];   // EID(1) + Length(1) + SSID(32)
    u8_t   supportedRates[ZM_MAX_SUPP_RATES_IE_SIZE + 2]; // EID(1) + Length(1) + supported rates [12]
    u8_t   channel;
    u16_t  frequency;
    u16_t  atimWindow;
    u8_t   erp;
    u8_t   extSupportedRates[ZM_MAX_SUPP_RATES_IE_SIZE + 2]; // EID(1) + Length(1) + extended supported rates [12]
    u8_t   wpaIe[ZM_MAX_IE_SIZE + 2];
    u8_t   wscIe[ZM_MAX_WPS_IE_SIZE + 2];
    u8_t   rsnIe[ZM_MAX_IE_SIZE + 2];
#ifdef ZM_ENABLE_CENC
    u8_t   cencIe[ZM_MAX_IE_SIZE + 2]; /* CENC */ /* half size because of memory exceed 64k boundary */
#endif //ZM_ENABLE_CENC
    u8_t   securityType;
    u8_t   signalStrength;
    u8_t   signalQuality;
    u16_t  sortValue;
    u8_t   wmeSupport;
    u8_t   flag;
    u8_t   EnableHT;
    u8_t   enableHT40;
    u8_t   SG40;
    u8_t   extChOffset;
    u8_t   apCap; // bit0:11N AP
    u16_t  frameBodysize;
    u8_t   frameBody[ZM_MAX_PROBE_FRAME_BODY_SIZE];
    u8_t   countryInfo[ZM_MAX_COUNTRY_INFO_SIZE + 2];
    u16_t  athOwlAp;
    u16_t  marvelAp;
    u16_t  broadcomHTAp;
    u32_t  tick;
    struct zsBssInfo* next;
};

struct zsBssList
{
    u8_t bssCount;
    struct zsBssInfo* head;
    struct zsBssInfo* tail;
};

struct zsBssListV1
{
    u8_t bssCount;
    struct zsBssInfo bssInfo[ZM_MAX_BSS];
};

#define ZM_KEY_FLAG_GK                 0x0001
#define ZM_KEY_FLAG_PK                 0X0002
#define ZM_KEY_FLAG_AUTHENTICATOR      0x0004
#define ZM_KEY_FLAG_INIT_IV            0x0008
#define ZM_KEY_FLAG_DEFAULT_KEY        0x0010

#ifdef ZM_ENABLE_CENC
#define ZM_KEY_FLAG_CENC               0x0020
#endif //ZM_ENABLE_CENC

// Comment: For TKIP, key[0]~key[15]  => TKIP key
//                    key[16]~key[23] => Tx MIC key
//                    key[24]~key[31] => Rx MIC key
struct zsKeyInfo
{
    u8_t*   key;
    u8_t    keyLength;
    u8_t    keyIndex;
    u8_t*   initIv;
    u16_t   flag;
    u8_t    vapId;
    u16_t    vapAddr[3];
    u16_t*   macAddr;
};



/*
 * Channels are specified by frequency.
 */
typedef struct {
	u16_t	channel;	/* setting in Mhz */
	u32_t	channelFlags;	/* see below */
	u8_t	privFlags;
	s8_t	maxRegTxPower;	/* max regulatory tx power in dBm */
	s8_t	maxTxPower;	/* max true tx power in 0.25 dBm */
	s8_t	minTxPower;	/* min true tx power in 0.25 dBm */
} ZM_HAL_CHANNEL;

struct zsRegulationTable
{
    u16_t   regionCode;
    u16_t   CurChIndex;
    u16_t   allowChannelCnt;
    ZM_HAL_CHANNEL   allowChannel[60];   /* 2.4GHz: 14 channels, 5 GHz: 31 channels */
};

struct zsPartnerNotifyEvent
{
    u8_t bssid[6];                      // The BSSID of IBSS
    u8_t peerMacAddr[6];                // The MAC address of peer station
};

#define ZM_RC_TRAINED_BIT   0x1
struct zsRcCell
{
    u32_t txCount;
    u32_t failCount;
    u8_t currentRate;
    u8_t currentRateIndex;
    u32_t probingTime;
    u8_t operationRateSet[24];
    u8_t operationRateCount;
    u16_t rxRssi;
    u8_t flag;
    u32_t  lasttxCount;
    u32_t  lastTime;
};

struct zsOppositeInfo
{
    u8_t            macAddr[6];
    struct zsRcCell rcCell;
    u8_t            valid;              // This indicate if this opposite is still valid
    u8_t            aliveCounter;
    u8_t            pkInstalled;

#ifdef ZM_ENABLE_IBSS_WPA2PSK
    /* For WPA2PSK ! */
    u8_t 			wpaState;
    u8_t            camIdx;
    u8_t            encryMode;
    u16_t           iv16;
    u32_t           iv32;
#endif
};

typedef void (*zfpIBSSIteratePeerStationCb)(
    zdev_t* dev, struct zsOppositeInfo *peerInfo, void *ctx, u8_t index);

typedef u16_t (*zfpStaRxSecurityCheckCb)(zdev_t* dev, zbuf_t* buf);


/* Communication Tally data structure */
struct zsCommTally
{
    u32_t txUnicastFrm;		    //  0 txUnicastFrames
    u32_t txMulticastFrm;		//  1 txMulticastFrames
    u32_t txUnicastOctets;	    //  2 txUniOctets  byte size
    u32_t txMulticastOctets;	//  3 txMultiOctets  byte size
    u32_t txFrmUpperNDIS;       //  4
    u32_t txFrmDrvMgt;          //  5
    u32_t RetryFailCnt;		    //  6
    u32_t Hw_TotalTxFrm;		//  7 Hardware total Tx Frame
    u32_t Hw_RetryCnt;		    //  8 txMultipleRetriesFrames
    u32_t Hw_UnderrunCnt;       //  9

    u32_t DriverRxFrmCnt;       // 10
    u32_t rxUnicastFrm;		    // 11 rxUnicastFrames
    u32_t rxMulticastFrm;	    // 12rxMulticastFrames

    u32_t NotifyNDISRxFrmCnt;   // 14
    u32_t rxUnicastOctets;	// 15 rxUniOctets  byte size
    u32_t rxMulticastOctets;	    // 16 rxMultiOctets  byte size
    u32_t DriverDiscardedFrm;       // 17 Discard by ValidateFrame
    u32_t LessThanDataMinLen;       // 18
    u32_t GreaterThanMaxLen;        // 19
    u32_t DriverDiscardedFrmCauseByMulticastList;
    u32_t DriverDiscardedFrmCauseByFrmCtrl;
    u32_t rxNeedFrgFrm;		    // 22 need more frg frm
    u32_t DriverRxMgtFrmCnt;
    u32_t rxBroadcastFrm;	    // 24 Receive broadcast frame count
    u32_t rxBroadcastOctets;	// 25 Receive broadcast frame byte size
    u32_t rx11bDataFrame;		// 26 Measured quality 11b data frame count
    u32_t rxOFDMDataFrame;	    // 27 Measured quality 11g data frame count


    u32_t Hw_TotalRxFrm;        // 28
    u32_t Hw_CRC16Cnt;		    // 29 rxPLCPCRCErrCnt
    u32_t Hw_CRC32Cnt;		    // 30 rxCRC32ErrCnt
    u32_t Hw_DecrypErr_UNI;     // 31
    u32_t Hw_DecrypErr_Mul;     // 32

    u32_t Hw_RxFIFOOverrun;     // 34
    u32_t Hw_RxTimeOut;         // 35
    u32_t LossAP;               // 36

    u32_t Tx_MPDU;              // 37
    u32_t BA_Fail;              // 38
    u32_t Hw_Tx_AMPDU;          // 39
    u32_t Hw_Tx_MPDU;           // 40

    u32_t RateCtrlTxMPDU;
    u32_t RateCtrlBAFail;

    u32_t txQosDropCount[5];    //41 42 43 44 45

	u32_t Hw_RxMPDU;            // 46
	u32_t Hw_RxDropMPDU;        // 47
	u32_t Hw_RxDelMPDU;         // 48

	u32_t Hw_RxPhyMiscError;    // 49
	u32_t Hw_RxPhyXRError;      // 50
    u32_t Hw_RxPhyOFDMError;    // 51
    u32_t Hw_RxPhyCCKError;     // 52
    u32_t Hw_RxPhyHTError;      // 53
    u32_t Hw_RxPhyTotalCount;   // 54

    u32_t swRxFragmentCount;         // 55
    u32_t swRxUnicastMicFailCount;   // 56
    u32_t swRxMulticastMicFailCount; // 57
    u32_t swRxDropUnencryptedCount;  // 58

    u32_t txBroadcastFrm;
    u32_t txBroadcastOctets;
};

/* Traffic Monitor Tally data structure */
struct zsTrafTally
{
    u32_t rxDuplicate;
    u32_t rxSrcIsOwnMac;
    //u32_t rxDataFrameCount;
    //u32_t rxDataByteCount;
    //u32_t rxDataBytesIn1000ms;
    //u32_t rxDataTmpFor1000ms;
    //u32_t rxDataBytesIn2000ms;
    //u32_t rxDataTmpFor2000ms;

    //u32_t txDataFrameCount;
    //u32_t txDataByteCount;
    //u32_t txDataBytesIn1000ms;
    //u32_t txDataTmpFor1000ms;
    u32_t txDataBytesIn2000ms;
    u32_t txDataTmpFor2000ms;
};

/* Hal rx packet moniter information */
struct zsMonHalRxInfo
{
    u32_t currentRSSI[7];
    u32_t currentRxEVM[14];
    u32_t currentRxDataMT;
    u32_t currentRxDataMCS;
    u32_t currentRxDataBW;
    u32_t currentRxDataSG;
};

struct zsTail
{
    u8_t SignalStrength1;
    u8_t SignalStrength2;
    u8_t SignalStrength3;
    u8_t SignalQuality;
    u8_t SAIndex;
    u8_t DAIndex;
    u8_t ErrorIndication;
    u8_t RxMacStatus;
};

union zuTail
{
    struct zsTail Data;
    u8_t Byte[8];
};

struct zsAdditionInfo
{
    u8_t PlcpHeader[12];
    union zuTail   Tail;
};


struct zsPmkidBssidInfo
{
    u16_t      bssid[3];
    u8_t       pmkid[16];
};

struct zsPmkidInfo
{
	   u32_t		bssidCount;
	   struct zsPmkidBssidInfo	bssidInfo[ZM_PMKID_MAX_BSS_CNT];
};


struct zsCbFuncTbl
{
    u16_t (*zfcbAuthNotify)(zdev_t* dev, u16_t* macAddr);
    u16_t (*zfcbAsocNotify)(zdev_t* dev, u16_t* macAddr, u8_t* body,
            u16_t bodySize, u16_t port);
    u16_t (*zfcbDisAsocNotify)(zdev_t* dev, u8_t* macAddr, u16_t port);
    u16_t (*zfcbApConnectNotify)(zdev_t* dev, u8_t* macAddr, u16_t port);
    void (*zfcbConnectNotify)(zdev_t* dev, u16_t status, u16_t* bssid);
    void (*zfcbScanNotify)(zdev_t* dev, struct zsScanResult* result);
    void (*zfcbMicFailureNotify)(zdev_t* dev, u16_t* addr, u16_t status);
    void (*zfcbApMicFailureNotify)(zdev_t* dev, u8_t* addr, zbuf_t* buf);
    void (*zfcbIbssPartnerNotify)(zdev_t* dev, u16_t status,
            struct zsPartnerNotifyEvent *event);
    void (*zfcbMacAddressNotify)(zdev_t* dev, u8_t* addr);
    void (*zfcbSendCompleteIndication)(zdev_t* dev, zbuf_t* buf);
    void (*zfcbRecvEth)(zdev_t* dev, zbuf_t* buf, u16_t port);
    void (*zfcbRecv80211)(zdev_t* dev, zbuf_t* buf, struct zsAdditionInfo* addInfo);
    void (*zfcbRestoreBufData)(zdev_t* dev, zbuf_t* buf);
#ifdef ZM_ENABLE_CENC
    u16_t (*zfcbCencAsocNotify)(zdev_t* dev, u16_t* macAddr, u8_t* body,
            u16_t bodySize, u16_t port);
#endif //ZM_ENABLE_CENC
    u8_t (*zfcbClassifyTxPacket)(zdev_t* dev, zbuf_t* buf);

    void (*zfcbHwWatchDogNotify)(zdev_t* dev);
};

extern void zfZeroMemory(u8_t* va, u16_t length);
#define ZM_INIT_CB_FUNC_TABLE(p)        zfZeroMemory((u8_t *)p, sizeof(struct zsCbFuncTbl));

//extern struct zsWlanDev zgWlanDev;

/* Initialize WLAN hardware and software, resource will be allocated */
/* for WLAN operation, must be called first before other function.   */
extern u16_t zfiWlanOpen(zdev_t* dev, struct zsCbFuncTbl* cbFuncTbl);

/* WLAN hardware will be shutdown and all resource will be release */
extern u16_t zfiWlanClose(zdev_t* dev);

/* Enable/disable Wlan operation */
extern u16_t zfiWlanEnable(zdev_t* dev);
extern u16_t zfiWlanDisable(zdev_t* dev, u8_t ResetKeyCache);
extern u16_t zfiWlanResume(zdev_t* dev, u8_t doReconn);
extern u16_t zfiWlanSuspend(zdev_t* dev);

/* Enable/disable ISR interrupt */
extern u16_t zfiWlanInterruptEnable(zdev_t* dev);
extern u16_t zfiWlanInterruptDisable(zdev_t* dev);

/* Do WLAN site survey */
extern u16_t zfiWlanScan(zdev_t* dev);

/* Get WLAN stastics */
extern u16_t zfiWlanGetStatistics(zdev_t* dev);

/* Reset WLAN */
extern u16_t zfiWlanReset(zdev_t* dev);

/* Deauthenticate a STA */
extern u16_t zfiWlanDeauth(zdev_t* dev, u16_t* macAddr, u16_t reason);

extern u16_t zfiTxSendEth(zdev_t* dev, zbuf_t* buf, u16_t port);
extern u8_t zfiIsTxQueueFull(zdev_t* dev);
extern u16_t zfiTxSend80211Mgmt(zdev_t* dev, zbuf_t* buf, u16_t port);

extern void zfiIsrPci(zdev_t* dev);

extern u8_t zfiWlanIBSSGetPeerStationsCount(zdev_t* dev);
extern u8_t zfiWlanIBSSIteratePeerStations(zdev_t* dev, u8_t numToIterate, zfpIBSSIteratePeerStationCb callback, void *ctx);
extern void zfiWlanFlushAllQueuedBuffers(zdev_t* dev);

/* coid.c */
extern void zfiWlanQueryMacAddress(zdev_t* dev, u8_t* addr);

extern u16_t zfiGlobalDataSize(zdev_t* dev);

extern void zfiHeartBeat(zdev_t* dev);

extern void zfiWlanSetWlanMode(zdev_t* dev, u8_t wlanMode);
extern void zfiWlanSetAuthenticationMode(zdev_t* dev, u8_t authMode);
extern void zfiWlanSetWepStatus(zdev_t* dev, u8_t wepStatus);
extern void zfiWlanSetSSID(zdev_t* dev, u8_t* ssid, u8_t ssidLength);
extern void zfiWlanSetFragThreshold(zdev_t* dev, u16_t fragThreshold);
extern void zfiWlanSetRtsThreshold(zdev_t* dev, u16_t rtsThreshold);
extern void zfiWlanSetFrequency(zdev_t* dev, u32_t frequency, u8_t bImmediate);
extern void zfiWlanSetBssid(zdev_t* dev, u8_t* bssid);
extern void zfiWlanSetBeaconInterval(zdev_t* dev, u16_t beaconInterval,
                              u8_t bImmediate);
extern void zfiWlanSetDtimCount(zdev_t* dev, u8_t  dtim);
extern void zfiWlanSetAtimWindow(zdev_t* dev, u16_t atimWindow, u8_t bImmediate);
extern void zfiWlanSetEncryMode(zdev_t* dev, u8_t encryMode);
extern u8_t zfiWlanSetKey(zdev_t* dev, struct zsKeyInfo keyInfo);
extern u8_t zfiWlanPSEUDOSetKey(zdev_t* dev, struct zsKeyInfo keyInfo);
extern void zfiWlanSetPowerSaveMode(zdev_t* dev, u8_t mode);
extern void zfiWlanQueryBssListV1(zdev_t* dev, struct zsBssListV1* bssListV1);
extern void zfiWlanQueryBssList(zdev_t* dev, struct zsBssList* pBssList);
extern void zfiWlanSetProtectionMode(zdev_t* dev, u8_t mode);
extern void zfiWlanFlushBssList(zdev_t* dev);

void zfiWlanDisableDfsChannel(zdev_t* dev, u8_t disableFlag);

extern u8_t zfiWlanQueryWlanMode(zdev_t* dev);
extern u16_t zfiWlanChannelToFrequency(zdev_t* dev, u8_t channel);
extern u8_t zfiWlanFrequencyToChannel(zdev_t* dev, u16_t freq);

#define ZM_WLAN_STATE_OPENED        0
#define ZM_WLAN_STATE_ENABLED       1
#define ZM_WLAN_STATE_DISABLED      2
#define ZM_WLAN_STATE_CLOSEDED      3
extern u8_t zfiWlanQueryAdapterState(zdev_t* dev);
extern u8_t zfiWlanQueryAuthenticationMode(zdev_t* dev, u8_t bWrapper);
extern u8_t zfiWlanQueryWepStatus(zdev_t* dev, u8_t bWrapper);
extern void zfiWlanQuerySSID(zdev_t* dev, u8_t* ssid, u8_t* pSsidLength);
extern u16_t zfiWlanQueryFragThreshold(zdev_t* dev);
extern u16_t zfiWlanQueryRtsThreshold(zdev_t* dev);
extern u32_t zfiWlanQueryFrequency(zdev_t* dev);
extern u32_t zfiWlanQueryCurrentFrequency(zdev_t* dev, u8_t qmode);
extern u32_t zfiWlanQueryFrequencyAttribute(zdev_t* dev, u32_t frequency);
extern void zfiWlanQueryFrequencyHT(zdev_t* dev, u32_t *bandWidth, u32_t *extOffset);
extern u8_t zfiWlanQueryCWMode(zdev_t* dev);
extern u32_t zfiWlanQueryCWEnable(zdev_t* dev);
extern void zfiWlanQueryBssid(zdev_t* dev, u8_t* bssid);
extern u16_t zfiWlanQueryBeaconInterval(zdev_t* dev);
extern u32_t zfiWlanQueryRxBeaconTotal(zdev_t* dev);
extern u16_t zfiWlanQueryAtimWindow(zdev_t* dev);
extern u8_t zfiWlanQueryEncryMode(zdev_t* dev);
extern u16_t zfiWlanQueryCapability(zdev_t* dev);
extern u16_t zfiWlanQueryAid(zdev_t* dev);
extern void zfiWlanQuerySupportRate(zdev_t* dev, u8_t* rateArray, u8_t* pLength);
extern void zfiWlanQueryExtSupportRate(zdev_t* dev, u8_t* rateArray, u8_t* pLength);
extern void zfiWlanQueryRsnIe(zdev_t* dev, u8_t* ie, u8_t* pLength);
extern void zfiWlanQueryWpaIe(zdev_t* dev, u8_t* ie, u8_t* pLength);
extern u8_t zfiWlanQueryHTMode(zdev_t* dev);
extern u8_t zfiWlanQueryBandWidth40(zdev_t* dev);
extern u8_t zfiWlanQueryMulticastCipherAlgo(zdev_t *dev);
extern u16_t zfiWlanQueryRegionCode(zdev_t* dev);
extern void zfiWlanSetWpaIe(zdev_t* dev, u8_t* ie, u8_t Length);
extern void zfiWlanSetWpaSupport(zdev_t* dev, u8_t WpaSupport);
extern void zfiWlanCheckStaWpaIe(zdev_t* dev);
extern void zfiWlanSetBasicRate(zdev_t* dev, u8_t bRateSet, u8_t gRateSet,
                         u32_t nRateSet);
extern void zfiWlanSetBGMode(zdev_t* dev, u8_t mode);
extern void zfiWlanSetpreambleType(zdev_t* dev, u8_t type);
extern u8_t zfiWlanQuerypreambleType(zdev_t* dev);
extern u8_t zfiWlanQueryPowerSaveMode(zdev_t* dev);
extern void zfiWlanSetMacAddress(zdev_t* dev, u16_t* mac);
extern u16_t zfiWlanSetTxRate(zdev_t* dev, u16_t rate);
extern u32_t zfiWlanQueryTxRate(zdev_t* dev);
extern void zfWlanUpdateRxRate(zdev_t* dev, struct zsAdditionInfo* addInfo);
extern u32_t zfiWlanQueryRxRate(zdev_t* dev);
extern u8_t zfiWlanSetPmkidInfo(zdev_t* dev, u16_t* bssid, u8_t* pmkid);
extern u32_t zfiWlanQueryPmkidInfo(zdev_t* dev, u8_t* buf, u32_t len);
extern void zfiWlanSetAllMulticast(zdev_t* dev, u32_t setting);
extern void zfiWlanSetHTCtrl(zdev_t* dev, u32_t *setting, u32_t forceTxTPC);
extern void zfiWlanQueryHTCtrl(zdev_t* dev, u32_t *setting, u32_t *forceTxTPC);
extern void zfiWlanDbg(zdev_t* dev, u8_t setting);

extern void zfiWlanResetTally(zdev_t* dev);
extern void zfiWlanQueryTally(zdev_t* dev, struct zsCommTally *tally);
extern void zfiWlanQueryTrafTally(zdev_t* dev, struct zsTrafTally *tally);
extern void zfiWlanQueryMonHalRxInfo(zdev_t* dev, struct zsMonHalRxInfo *halRxInfo);

extern u32_t zfiFWConfig(zdev_t* dev, u32_t size);

extern void zfiDKEnable(zdev_t* dev, u32_t enable);

extern void zfiWlanSetMulticastList(zdev_t* dev, u8_t size, u8_t* pList);
extern void zfiWlanRemoveKey(zdev_t* dev, u8_t keyType, u8_t keyId);
extern u8_t zfiWlanQueryIsPKInstalled(zdev_t *dev, u8_t *staMacAddr);
extern u32_t zfiWlanQueryPacketTypePromiscuous(zdev_t* dev);
extern void zfiWlanSetPacketTypePromiscuous(zdev_t* dev, u32_t setValue);
extern void zfiSetChannelManagement(zdev_t* dev, u32_t setting);
extern void zfiSetRifs(zdev_t* dev, u16_t setting);
extern void zfiCheckRifs(zdev_t* dev);
extern void zfiSetReorder(zdev_t* dev, u16_t value);
extern void zfiSetSeqDebug(zdev_t* dev, u16_t value);

extern u16_t zfiConfigWdsPort(zdev_t* dev, u8_t wdsPortId, u16_t flag, u16_t* wdsAddr,
        u16_t encType, u32_t* wdsKey);
extern void zfiWlanQueryRegulationTable(zdev_t* dev, struct zsRegulationTable* pEntry);
extern void zfiWlanSetScanTimerPerChannel(zdev_t* dev, u16_t time);
extern void zfiWlanSetAutoReconnect(zdev_t* dev, u8_t enable);
extern u32_t zfiDebugCmd(zdev_t* dev, u32_t cmd, u32_t value);
extern void zfiWlanSetProbingHiddenSsid(zdev_t* dev, u8_t* ssid, u8_t ssidLen,
    u16_t entry);
extern void zfiWlanSetDropUnencryptedPackets(zdev_t* dev, u8_t enable);
extern void zfiWlanSetIBSSJoinOnly(zdev_t* dev, u8_t joinOnly);
extern void zfiWlanSetDefaultKeyId(zdev_t* dev, u8_t keyId);
extern void zfiWlanSetDisableProbingWithSsid(zdev_t* dev, u8_t mode);
extern void zfiWlanQueryGSN(zdev_t* dev, u8_t *gsn, u16_t vapId);
extern u16_t zfiStaAddIeWpaRsn(zdev_t* dev, zbuf_t* buf, u16_t offset, u8_t frameType);
extern u8_t zfiWlanSetDot11DMode(zdev_t* dev, u8_t mode);
extern u8_t zfiWlanSetDot11HDFSMode(zdev_t* dev, u8_t mode);
extern u8_t zfiWlanSetDot11HTPCMode(zdev_t* dev, u8_t mode);
extern u8_t zfiWlanSetAniMode(zdev_t* dev, u8_t mode);
extern void zfiWlanSetStaWme(zdev_t* dev, u8_t enable, u8_t uapsdInfo);
extern void zfiWlanSetApWme(zdev_t* dev, u8_t enable);
extern u8_t zfiWlanQuerywmeEnable(zdev_t* dev);
#ifdef ZM_OS_LINUX_FUNC
extern void zfiWlanShowTally(zdev_t* dev);
#endif
#ifdef ZM_ENABLE_CENC
/* CENC */
extern u8_t zfiWlanSetCencPairwiseKey(zdev_t* dev, u8_t keyid, u32_t *txiv, u32_t *rxiv,
        u8_t *key, u8_t *mic);
extern u8_t zfiWlanSetCencGroupKey(zdev_t* dev, u8_t keyid, u32_t *rxiv,
        u8_t *key, u8_t *mic);
#endif //ZM_ENABLE_CENC
extern void zfiWlanQuerySignalInfo(zdev_t* dev, u8_t *buffer);
extern void zfiWlanQueryAdHocCreatedBssDesc(zdev_t* dev, struct zsBssInfo *pBssInfo);
extern u8_t zfiWlanQueryAdHocIsCreator(zdev_t* dev);
extern u32_t zfiWlanQuerySupportMode(zdev_t* dev);
extern u32_t zfiWlanQueryTransmitPower(zdev_t* dev);
extern void zfiWlanEnableLeapConfig(zdev_t* dev, u8_t leapEnabled);

/* returned buffer allocated by driver core */
extern void zfiRecvEthComplete(zdev_t* dev, zbuf_t* buf);

extern void zfiRecv80211(zdev_t* dev, zbuf_t* buf, struct zsAdditionInfo* addInfo);

extern void zfiWlanSetMaxTxPower(zdev_t* dev, u8_t power2, u8_t power5);
extern void zfiWlanQueryMaxTxPower(zdev_t* dev, u8_t *power2, u8_t *power5);
extern void zfiWlanSetConnectMode(zdev_t* dev, u8_t mode);
extern void zfiWlanSetSupportMode(zdev_t* dev, u32_t mode);
extern void zfiWlanSetAdhocMode(zdev_t* dev, u32_t mode);
extern u32_t zfiWlanQueryAdhocMode(zdev_t* dev, u8_t bWrapper);
extern u8_t zfiWlanSetCountryIsoName(zdev_t* dev, u8_t *countryIsoName, u8_t length);
extern const char* zfiWlanQueryCountryIsoName(zdev_t* dev);
extern u8_t zfiWlanQueryregulatoryDomain(zdev_t* dev);
extern u8_t zfiWlanQueryCCS(zdev_t* dev);
extern void zfiWlanSetCCS(zdev_t* dev, u8_t mode);
extern void zfiWlanSetRegulatory(zdev_t* dev, u8_t CCS, u16_t Code, u8_t bfirstChannel);
extern const char* zfiHpGetisoNamefromregionCode(zdev_t* dev, u16_t regionCode);
extern void  zfiWlanSetLEDCtrlParam(zdev_t* dev, u8_t type, u8_t flag);
extern u32_t zfiWlanQueryReceivedPacket(zdev_t* dev);
extern void zfiWlanCheckSWEncryption(zdev_t* dev);
extern u16_t zfiWlanQueryAllowChannels(zdev_t *dev, u16_t *channels);
extern u16_t zfiWlanGetMulticastAddressCount(zdev_t* dev);
extern void zfiWlanGetMulticastList(zdev_t* dev, u8_t* pMCList);
extern void zfiWlanSetPacketFilter(zdev_t* dev, u32_t PacketFilter);
extern u8_t zfiCompareWithMulticastListAddress(zdev_t* dev, u16_t* dstMacAddr);
extern void zfiWlanSetSafeModeEnabled(zdev_t* dev, u8_t safeMode);
extern void zfiWlanSetIBSSAdditionalIELength(zdev_t* dev, u32_t ibssAdditionalIESize,  u8_t* ibssAdditionalIE);
extern void zfiWlanSetXLinkMode(zdev_t* dev, u32_t setValue);

/* hprw.c */
extern u32_t zfiDbgWriteFlash(zdev_t* dev, u32_t addr, u32_t val);
extern u32_t zfiDbgWriteReg(zdev_t* dev, u32_t addr, u32_t val);
extern u32_t zfiDbgReadReg(zdev_t* dev, u32_t addr);

extern u32_t zfiDbgWriteEeprom(zdev_t* dev, u32_t addr, u32_t val);
extern u32_t zfiDbgBlockWriteEeprom(zdev_t* dev, u32_t addr, u32_t* buf);
extern u32_t zfiDbgBlockWriteEeprom_v2(zdev_t* dev, u32_t addr, u32_t* buf, u32_t wrlen);

extern u16_t zfiDbgChipEraseFlash(zdev_t *dev);
extern u16_t zfiDbgProgramFlash(zdev_t *dev, u32_t offset, u32_t len, u32_t *data);
extern u32_t zfiDbgGetFlashCheckSum(zdev_t *dev, u32_t addr, u32_t len);
extern u32_t zfiDbgReadFlash(zdev_t *dev, u32_t addr, u32_t len);
extern u32_t zfiDownloadFwSet(zdev_t *dev);

extern u32_t zfiDbgDelayWriteReg(zdev_t* dev, u32_t addr, u32_t val);
extern u32_t zfiDbgFlushDelayWrite(zdev_t* dev);

extern u32_t zfiDbgSetIFSynthesizer(zdev_t* dev, u32_t value);
extern u32_t zfiDbgReadTally(zdev_t* dev);

extern u32_t zfiDbgQueryHwTxBusy(zdev_t* dev);

extern u8_t zfiWlanGetDestAddrFromBuf(zdev_t *dev, zbuf_t *buf, u16_t *macAddr);

extern u32_t zfiWlanQueryHwCapability(zdev_t* dev);

extern void zfiWlanSetDynamicSIFSParam(zdev_t* dev, u8_t val);

/***** End of section 2 *****/

/***** section 3 performace evaluation *****/
#ifdef ZM_ENABLE_PERFORMANCE_EVALUATION
extern void zfiTxPerformanceMSDU(zdev_t* dev, u32_t tick);
extern void zfiRxPerformanceMPDU(zdev_t* dev, zbuf_t* buf);
extern void zfiRxPerformanceReg(zdev_t* dev, u32_t reg, u32_t rsp);
#define ZM_PERFORMANCE_INIT(dev)                zfiPerformanceInit(dev);
#define ZM_PERFORMANCE_TX_MSDU(dev, tick)       zfiTxPerformanceMSDU(dev, tick);
#define ZM_PERFORMANCE_RX_MSDU(dev, tick)       zfiRxPerformanceMSDU(dev, tick);
#define ZM_PERFORMANCE_TX_MPDU(dev, tick)       zfiTxPerformanceMPDU(dev, tick);
#define ZM_PERFORMANCE_RX_MPDU(dev, buf)        zfiRxPerformanceMPDU(dev, buf);
#define ZM_PERFORMANCE_RX_SEQ(dev, buf)         zfiRxPerformanceSeq(dev, buf);
#define ZM_PERFORMANCE_REG(dev, reg, rsp)    {if(cmd[1] == reg) zfiRxPerformanceReg(dev, reg, rsp);}
#define ZM_PERFORMANCE_DUP(dev, buf1, buf2)     zfiRxPerformanceDup(dev, buf1, buf2);
#define ZM_PERFORMANCE_FREE(dev, buf)           zfiRxPerformanceFree(dev, buf);
#define ZM_PERFORMANCE_RX_AMSDU(dev, buf, len)  zfiRxPerformanceAMSDU(dev, buf, len);
#define ZM_PERFORMANCE_RX_FLUSH(dev)            zfiRxPerformanceFlush(dev);
#define ZM_PERFORMANCE_RX_CLEAR(dev)            zfiRxPerformanceClear(dev);
#define ZM_SEQ_DEBUG                            if (wd->seq_debug) DbgPrint
#define ZM_PERFORMANCE_RX_REORDER(dev)          zfiRxPerformanceReorder(dev);
#else
#define ZM_PERFORMANCE_INIT(dev)
#define ZM_PERFORMANCE_TX_MSDU(dev, tick)
#define ZM_PERFORMANCE_RX_MSDU(dev, tick)
#define ZM_PERFORMANCE_TX_MPDU(dev, tick)
#define ZM_PERFORMANCE_RX_MPDU(dev, buf)
#define ZM_PERFORMANCE_RX_SEQ(dev, buf)
#define ZM_PERFORMANCE_REG(dev, reg, rsp)
#define ZM_PERFORMANCE_DUP(dev, buf1, buf2)
#define ZM_PERFORMANCE_FREE(dev, buf)
#define ZM_PERFORMANCE_RX_AMSDU(dev, buf, len)
#define ZM_PERFORMANCE_RX_FLUSH(dev)
#define ZM_PERFORMANCE_RX_CLEAR(dev)
#define ZM_SEQ_DEBUG
#define ZM_PERFORMANCE_RX_REORDER(dev)
#endif
/***** End of section 3 *****/
#endif
