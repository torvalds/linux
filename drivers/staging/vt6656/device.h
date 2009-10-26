/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * File: device.h
 *
 * Purpose: MAC Data structure
 *
 * Author: Tevin Chen
 *
 * Date: Mar 17, 1997
 *
 */

#ifndef __DEVICE_H__
#define __DEVICE_H__

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/if_arp.h>
#include <linux/sched.h>
#include <linux/if.h>
#include <linux/rtnetlink.h>//James
#include <linux/proc_fs.h>
#include <linux/inetdevice.h>
#include <linux/reboot.h>
#include <linux/usb.h>
#include <linux/signal.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#ifdef SIOCETHTOOL
#define DEVICE_ETHTOOL_IOCTL_SUPPORT
#include <linux/ethtool.h>
#else
#undef DEVICE_ETHTOOL_IOCTL_SUPPORT
#endif
/* Include Wireless Extension definition and check version - Jean II */
#include <linux/wireless.h>
#include <net/iw_handler.h>	// New driver API

#ifndef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
#define WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
#endif

//2007-0920-01<Add>by MikeLiu
#ifndef SndEvt_ToAPI
#define SndEvt_ToAPI
//please copy below macro to driver_event.c for API
#define RT_INSMOD_EVENT_FLAG                             0x0101
#define RT_UPDEV_EVENT_FLAG                               0x0102
#define RT_DISCONNECTED_EVENT_FLAG               0x0103
#define RT_WPACONNECTED_EVENT_FLAG             0x0104
#define RT_DOWNDEV_EVENT_FLAG                        0x0105
#define RT_RMMOD_EVENT_FLAG                              0x0106
#endif

//
// device specific
//

#include "device_cfg.h"
#include "ttype.h"
#include "80211hdr.h"
#include "tether.h"
#include "wmgr.h"
#include "wcmd.h"
#include "mib.h"
#include "srom.h"
#include "rc4.h"
#include "desc.h"
#include "key.h"
#include "card.h"

/*---------------------  Export Definitions -------------------------*/
#define VNT_USB_VENDOR_ID                     0x160a
#define VNT_USB_PRODUCT_ID                    0x3184

#define MAC_MAX_CONTEXT_REG     (256+128)

#define MAX_MULTICAST_ADDRESS_NUM       32
#define MULTICAST_ADDRESS_LIST_SIZE     (MAX_MULTICAST_ADDRESS_NUM * U_ETHER_ADDR_LEN)


//#define OP_MODE_INFRASTRUCTURE  0
//#define OP_MODE_ADHOC           1
//#define OP_MODE_AP              2

#define DUPLICATE_RX_CACHE_LENGTH       5

#define NUM_KEY_ENTRY                   11

#define TX_WEP_NONE                     0
#define TX_WEP_OTF                      1
#define TX_WEP_SW                       2
#define TX_WEP_SWOTP                    3
#define TX_WEP_OTPSW                    4
#define TX_WEP_SW232                    5

#define KEYSEL_WEP40                    0
#define KEYSEL_WEP104                   1
#define KEYSEL_TKIP                     2
#define KEYSEL_CCMP                     3



#define AUTO_FB_NONE            0
#define AUTO_FB_0               1
#define AUTO_FB_1               2

#define FB_RATE0                0
#define FB_RATE1                1

// Antenna Mode
#define ANT_A                   0
#define ANT_B                   1
#define ANT_DIVERSITY           2
#define ANT_RXD_TXA             3
#define ANT_RXD_TXB             4
#define ANT_UNKNOWN             0xFF
#define ANT_TXA                 0
#define ANT_TXB                 1
#define ANT_RXA                 2
#define ANT_RXB                 3


#define MAXCHECKHANGCNT         4

//Packet type
#define TX_PKT_UNI              0x00
#define TX_PKT_MULTI            0x01
#define TX_PKT_BROAD            0x02

#define BB_VGA_LEVEL            4
#define BB_VGA_CHANGE_THRESHOLD 3



#ifndef RUN_AT
#define RUN_AT(x)                       (jiffies+(x))
#endif

// DMA related
#define RESERV_AC0DMA                   4

#define PRIVATE_Message                 0

/*---------------------  Export Types  ------------------------------*/

#define DBG_PRT(l, p, args...) {if (l<=msglevel) printk( p ,##args);}
#define PRINT_K(p, args...) {if (PRIVATE_Message) printk( p ,##args);}

typedef enum __device_msg_level {
    MSG_LEVEL_ERR=0,            //Errors that will cause abnormal operation.
    MSG_LEVEL_NOTICE=1,         //Some errors need users to be notified.
    MSG_LEVEL_INFO=2,           //Normal message.
    MSG_LEVEL_VERBOSE=3,        //Will report all trival errors.
    MSG_LEVEL_DEBUG=4           //Only for debug purpose.
} DEVICE_MSG_LEVEL, *PDEVICE_MSG_LEVEL;

typedef enum __device_init_type {
    DEVICE_INIT_COLD=0,         // cold init
    DEVICE_INIT_RESET,          // reset init or Dx to D0 power remain init
    DEVICE_INIT_DXPL            // Dx to D0 power lost init
} DEVICE_INIT_TYPE, *PDEVICE_INIT_TYPE;


//USB

//
// Enum of context types for SendPacket
//
typedef enum _CONTEXT_TYPE {
    CONTEXT_DATA_PACKET = 1,
    CONTEXT_MGMT_PACKET
} CONTEXT_TYPE;




// RCB (Receive Control Block)
typedef struct _RCB
{
    PVOID                   Next;
    LONG                    Ref;
    PVOID                   pDevice;
    struct urb              *pUrb;
    SRxMgmtPacket           sMngPacket;
    struct sk_buff*         skb;
    BOOL                    bBoolInUse;

} RCB, *PRCB;


// used to track bulk out irps
typedef struct _USB_SEND_CONTEXT {
    PVOID           pDevice;
    struct sk_buff *pPacket;
    struct urb      *pUrb;
    UINT            uBufLen;
    CONTEXT_TYPE    Type;
    SEthernetHeader sEthHeader;
    PVOID           Next;
    BOOL            bBoolInUse;
    UCHAR           Data[MAX_TOTAL_SIZE_WITH_ALL_HEADERS];
} USB_SEND_CONTEXT, *PUSB_SEND_CONTEXT;


//structure got from configuration file as user desired default setting.
typedef struct _DEFAULT_CONFIG{
    INT    ZoneType;
    INT    eConfigMode;
    INT    eAuthenMode;    //open/wep/wpa
    INT    bShareKeyAlgorithm;  //open-open/open-sharekey/wep-sharekey
    INT    keyidx;               //wepkey index
    INT    eEncryptionStatus;

}DEFAULT_CONFIG,*PDEFAULT_CONFIG;

//
// Structure to keep track of usb interrupt packets
//
typedef struct {
    UINT            uDataLen;
    PBYTE           pDataBuf;
//    struct urb      *pUrb;
    BOOL            bInUse;
} INT_BUFFER, *PINT_BUFFER;



//0:11A 1:11B 2:11G
typedef enum _VIA_BB_TYPE
{
    BB_TYPE_11A=0,
    BB_TYPE_11B,
    BB_TYPE_11G
} VIA_BB_TYPE, *PVIA_BB_TYPE;

//0:11a,1:11b,2:11gb(only CCK in BasicRate),3:11ga(OFDM in Basic Rate)
typedef enum _VIA_PKT_TYPE
{
    PK_TYPE_11A=0,
    PK_TYPE_11B,
    PK_TYPE_11GB,
    PK_TYPE_11GA
} VIA_PKT_TYPE, *PVIA_PKT_TYPE;




//++ NDIS related

#define NDIS_STATUS     int
#define NTSTATUS        int

typedef enum __DEVICE_NDIS_STATUS {
    STATUS_SUCCESS=0,
    STATUS_FAILURE,
    STATUS_RESOURCES,
    STATUS_PENDING,
} DEVICE_NDIS_STATUS, *PDEVICE_NDIS_STATUS;


#define MAX_BSSIDINFO_4_PMKID   16
#define MAX_PMKIDLIST           5
//Flags for PMKID Candidate list structure
#define NDIS_802_11_PMKID_CANDIDATE_PREAUTH_ENABLED	0x01

// PMKID Structures
typedef UCHAR   NDIS_802_11_PMKID_VALUE[16];


typedef enum _NDIS_802_11_WEP_STATUS
{
    Ndis802_11WEPEnabled,
    Ndis802_11Encryption1Enabled = Ndis802_11WEPEnabled,
    Ndis802_11WEPDisabled,
    Ndis802_11EncryptionDisabled = Ndis802_11WEPDisabled,
    Ndis802_11WEPKeyAbsent,
    Ndis802_11Encryption1KeyAbsent = Ndis802_11WEPKeyAbsent,
    Ndis802_11WEPNotSupported,
    Ndis802_11EncryptionNotSupported = Ndis802_11WEPNotSupported,
    Ndis802_11Encryption2Enabled,
    Ndis802_11Encryption2KeyAbsent,
    Ndis802_11Encryption3Enabled,
    Ndis802_11Encryption3KeyAbsent
} NDIS_802_11_WEP_STATUS, *PNDIS_802_11_WEP_STATUS,
  NDIS_802_11_ENCRYPTION_STATUS, *PNDIS_802_11_ENCRYPTION_STATUS;


typedef enum _NDIS_802_11_STATUS_TYPE
{
    Ndis802_11StatusType_Authentication,
    Ndis802_11StatusType_MediaStreamMode,
    Ndis802_11StatusType_PMKID_CandidateList,
    Ndis802_11StatusTypeMax    // not a real type, defined as an upper bound
} NDIS_802_11_STATUS_TYPE, *PNDIS_802_11_STATUS_TYPE;

//Added new types for PMKID Candidate lists.
typedef struct _PMKID_CANDIDATE {
    NDIS_802_11_MAC_ADDRESS BSSID;
    ULONG Flags;
} PMKID_CANDIDATE, *PPMKID_CANDIDATE;


typedef struct _BSSID_INFO
{
    NDIS_802_11_MAC_ADDRESS BSSID;
    NDIS_802_11_PMKID_VALUE PMKID;
} BSSID_INFO, *PBSSID_INFO;

typedef struct tagSPMKID {
    ULONG Length;
    ULONG BSSIDInfoCount;
    BSSID_INFO BSSIDInfo[MAX_BSSIDINFO_4_PMKID];
} SPMKID, *PSPMKID;

typedef struct tagSPMKIDCandidateEvent {
    NDIS_802_11_STATUS_TYPE     StatusType;
    ULONG Version;       // Version of the structure
    ULONG NumCandidates; // No. of pmkid candidates
    PMKID_CANDIDATE CandidateList[MAX_PMKIDLIST];
} SPMKIDCandidateEvent, *PSPMKIDCandidateEvent;

//--

//++ 802.11h related
#define MAX_QUIET_COUNT     8

typedef struct tagSQuietControl {
    BOOL        bEnable;
    DWORD       dwStartTime;
    BYTE        byPeriod;
    WORD        wDuration;
} SQuietControl, *PSQuietControl;

//--


// The receive duplicate detection cache entry
typedef struct tagSCacheEntry{
    WORD        wFmSequence;
    BYTE        abyAddr2[U_ETHER_ADDR_LEN];
    WORD        wFrameCtl;
} SCacheEntry, *PSCacheEntry;

typedef struct tagSCache{
/* The receive cache is updated circularly.  The next entry to be written is
 * indexed by the "InPtr".
*/
    UINT            uInPtr;         // Place to use next
    SCacheEntry     asCacheEntry[DUPLICATE_RX_CACHE_LENGTH];
} SCache, *PSCache;

#define CB_MAX_RX_FRAG                 64
// DeFragment Control Block, used for collecting fragments prior to reassembly
typedef struct tagSDeFragControlBlock
{
    WORD            wSequence;
    WORD            wFragNum;
    BYTE            abyAddr2[U_ETHER_ADDR_LEN];
	UINT            uLifetime;
    struct sk_buff* skb;
    PBYTE           pbyRxBuffer;
    UINT            cbFrameLength;
    BOOL            bInUse;
} SDeFragControlBlock, *PSDeFragControlBlock;



//flags for options
#define     DEVICE_FLAGS_UNPLUG          0x00000001UL
#define     DEVICE_FLAGS_PREAMBLE_TYPE   0x00000002UL
#define     DEVICE_FLAGS_OP_MODE         0x00000004UL
#define     DEVICE_FLAGS_PS_MODE         0x00000008UL
#define		DEVICE_FLAGS_80211h_MODE	 0x00000010UL

//flags for driver status
#define     DEVICE_FLAGS_OPENED          0x00010000UL
#define     DEVICE_FLAGS_WOL_ENABLED     0x00080000UL
//flags for capbilities
#define     DEVICE_FLAGS_TX_ALIGN        0x01000000UL
#define     DEVICE_FLAGS_HAVE_CAM        0x02000000UL
#define     DEVICE_FLAGS_FLOW_CTRL       0x04000000UL

//flags for MII status
#define     DEVICE_LINK_FAIL             0x00000001UL
#define     DEVICE_SPEED_10              0x00000002UL
#define     DEVICE_SPEED_100             0x00000004UL
#define     DEVICE_SPEED_1000            0x00000008UL
#define     DEVICE_DUPLEX_FULL           0x00000010UL
#define     DEVICE_AUTONEG_ENABLE        0x00000020UL
#define     DEVICE_FORCED_BY_EEPROM      0x00000040UL
//for device_set_media_duplex
#define     DEVICE_LINK_CHANGE           0x00000001UL


typedef struct __device_opt {
    int         nRxDescs0;    //Number of RX descriptors0
    int         nTxDescs0;    //Number of TX descriptors 0, 1, 2, 3
    int         rts_thresh;   //rts threshold
    int         frag_thresh;
    int         OpMode;
    int         data_rate;
    int         channel_num;
    int         short_retry;
    int         long_retry;
    int         bbp_type;
    U32         flags;
} OPTIONS, *POPTIONS;


typedef struct __device_info {

// netdev
	struct usb_device*          usb;
    struct net_device*          dev;
    struct net_device_stats     stats;

    OPTIONS                     sOpts;

	struct tasklet_struct       CmdWorkItem;
	struct tasklet_struct       EventWorkItem;
	struct tasklet_struct       ReadWorkItem;
	struct tasklet_struct       RxMngWorkItem;

    U32                         rx_buf_sz;
    int                         multicast_limit;
    BYTE                        byRxMode;

    spinlock_t                  lock;

    U32                         rx_bytes;

    BYTE                        byRevId;

    U32                         flags;
    ULONG                       Flags;

    SCache                      sDupRxCache;

    SDeFragControlBlock         sRxDFCB[CB_MAX_RX_FRAG];
    UINT                        cbDFCB;
    UINT                        cbFreeDFCB;
    UINT                        uCurrentDFCBIdx;

    // +++USB

    struct urb                  *pControlURB;
    struct urb                  *pInterruptURB;
	struct usb_ctrlrequest      sUsbCtlRequest;

    UINT                        int_interval;
    //
    // Variables to track resources for the BULK In Pipe
    //
    PRCB                        pRCBMem;
    PRCB                        apRCB[CB_MAX_RX_DESC];
    UINT                        cbRD;
    PRCB                        FirstRecvFreeList;
    PRCB                        LastRecvFreeList;
    UINT                        NumRecvFreeList;
    PRCB                        FirstRecvMngList;
    PRCB                        LastRecvMngList;
    UINT                        NumRecvMngList;
    BOOL                        bIsRxWorkItemQueued;
    BOOL                        bIsRxMngWorkItemQueued;
    ULONG                       ulRcvRefCount;      // number of packets that have not been returned back

    //
    //  Variables to track resources for the BULK Out Pipe
    //

    PUSB_SEND_CONTEXT           apTD[CB_MAX_TX_DESC];
    UINT                        cbTD;

    //
    //  Variables to track resources for the Interript In Pipe
    //
    INT_BUFFER                  intBuf;
    BOOL                        fKillEventPollingThread;
    BOOL                        bEventAvailable;


  //default config from file by user setting
    DEFAULT_CONFIG    config_file;


    //
    // Statistic for USB
    // protect with spinlock
    ULONG                       ulBulkInPosted;
    ULONG                       ulBulkInError;
    ULONG                       ulBulkInContCRCError;
    ULONG                       ulBulkInBytesRead;

    ULONG                       ulBulkOutPosted;
    ULONG                       ulBulkOutError;
    ULONG                       ulBulkOutContCRCError;
    ULONG                       ulBulkOutBytesWrite;

    ULONG                       ulIntInPosted;
    ULONG                       ulIntInError;
    ULONG                       ulIntInContCRCError;
    ULONG                       ulIntInBytesRead;


    // Version control
    WORD                        wFirmwareVersion;
    BYTE                        byLocalID;
    BYTE                        byRFType;
    BYTE                        byBBRxConf;


    BYTE                        byZoneType;
    BOOL                        bZoneRegExist;

    BYTE                        byOriginalZonetype;

    BOOL                        bLinkPass;          // link status: OK or fail
    BYTE                        abyCurrentNetAddr[U_ETHER_ADDR_LEN];
    BYTE                        abyPermanentNetAddr[U_ETHER_ADDR_LEN];
    // SW network address
//    BYTE                        abySoftwareNetAddr[U_ETHER_ADDR_LEN];
    BOOL                        bExistSWNetAddr;

    // Adapter statistics
    SStatCounter                scStatistic;
    // 802.11 counter
    SDot11Counters              s802_11Counter;

    //
    // Maintain statistical debug info.
    //
    ULONG                       packetsReceived;
    ULONG                       packetsReceivedDropped;
    ULONG                       packetsReceivedOverflow;
    ULONG                       packetsSent;
    ULONG                       packetsSentDropped;
    ULONG                       SendContextsInUse;
    ULONG                       RcvBuffersInUse;


    // 802.11 management
    SMgmtObject                 sMgmtObj;

    QWORD                       qwCurrTSF;
    UINT                        cbBulkInMax;
    BOOL                        bPSRxBeacon;

    // 802.11 MAC specific
    UINT                        uCurrRSSI;
    BYTE                        byCurrSQ;


    //Antenna Diversity
    BOOL                        bTxRxAntInv;
    DWORD                       dwRxAntennaSel;
    DWORD                       dwTxAntennaSel;
    BYTE                        byAntennaCount;
    BYTE                        byRxAntennaMode;
    BYTE                        byTxAntennaMode;
    BYTE                        byRadioCtl;
    BYTE                        bHWRadioOff;

    //SQ3 functions for antenna diversity
    struct timer_list           TimerSQ3Tmax1;
    struct timer_list           TimerSQ3Tmax2;
    struct timer_list           TimerSQ3Tmax3;

    BOOL                        bDiversityRegCtlON;
    BOOL                        bDiversityEnable;
    ULONG                       ulDiversityNValue;
    ULONG                       ulDiversityMValue;
    BYTE                        byTMax;
    BYTE                        byTMax2;
    BYTE                        byTMax3;
    ULONG                       ulSQ3TH;

    ULONG                       uDiversityCnt;
    BYTE                        byAntennaState;
    ULONG                       ulRatio_State0;
    ULONG                       ulRatio_State1;
    ULONG                       ulSQ3_State0;
    ULONG                       ulSQ3_State1;

    ULONG                       aulSQ3Val[MAX_RATE];
    ULONG                       aulPktNum[MAX_RATE];

    // IFS & Cw
    UINT                        uSIFS;    //Current SIFS
    UINT                        uDIFS;    //Current DIFS
    UINT                        uEIFS;    //Current EIFS
    UINT                        uSlot;    //Current SlotTime
    UINT                        uCwMin;   //Current CwMin
    UINT                        uCwMax;   //CwMax is fixed on 1023.
    // PHY parameter
    BYTE                        bySIFS;
    BYTE                        byDIFS;
    BYTE                        byEIFS;
    BYTE                        bySlot;
    BYTE                        byCWMaxMin;

    // Rate
    VIA_BB_TYPE                 byBBType; //0: 11A, 1:11B, 2:11G
    VIA_PKT_TYPE                byPacketType; //0:11a,1:11b,2:11gb(only CCK in BasicRate),3:11ga(OFDM in Basic Rate)
    WORD                        wBasicRate;
    BYTE                        byACKRate;
    BYTE                        byTopOFDMBasicRate;
    BYTE                        byTopCCKBasicRate;


    DWORD                       dwAotoRateTxOkCnt;
    DWORD                       dwAotoRateTxFailCnt;
    DWORD                       dwErrorRateThreshold[13];
    DWORD                       dwTPTable[MAX_RATE];
    BYTE                        abyEEPROM[EEP_MAX_CONTEXT_SIZE];  //DWORD alignment

    BYTE                        byMinChannel;
    BYTE                        byMaxChannel;
    UINT                        uConnectionRate;

    BYTE                        byPreambleType;
    BYTE                        byShortPreamble;
    // CARD_PHY_TYPE
    BYTE                        eConfigPHYMode;

    // For RF Power table
    BYTE                        byCCKPwr;
    BYTE                        byOFDMPwrG;
    BYTE                        byOFDMPwrA;
    BYTE                        byCurPwr;
    BYTE                        abyCCKPwrTbl[14];
    BYTE                        abyOFDMPwrTbl[14];
    BYTE                        abyOFDMAPwrTbl[42];

    WORD                        wCurrentRate;
    WORD                        wRTSThreshold;
    WORD                        wFragmentationThreshold;
    BYTE                        byShortRetryLimit;
    BYTE                        byLongRetryLimit;
    CARD_OP_MODE                eOPMode;
    BOOL                        bBSSIDFilter;
    WORD                        wMaxTransmitMSDULifetime;
    BYTE                        abyBSSID[U_ETHER_ADDR_LEN];
    BYTE                        abyDesireBSSID[U_ETHER_ADDR_LEN];
    WORD                        wCTSDuration;       // update while speed change
    WORD                        wACKDuration;       // update while speed change
    WORD                        wRTSTransmitLen;    // update while speed change
    BYTE                        byRTSServiceField;  // update while speed change
    BYTE                        byRTSSignalField;   // update while speed change

    DWORD                       dwMaxReceiveLifetime;       // dot11MaxReceiveLifetime

    BOOL                        bCCK;
    BOOL                        bEncryptionEnable;
    BOOL                        bLongHeader;
    BOOL                        bSoftwareGenCrcErr;
    BOOL                        bShortSlotTime;
    BOOL                        bProtectMode;
    BOOL                        bNonERPPresent;
    BOOL                        bBarkerPreambleMd;

    BYTE                        byERPFlag;
    WORD                        wUseProtectCntDown;

    BOOL                        bRadioControlOff;
    BOOL                        bRadioOff;

    // Power save
    BOOL                        bEnablePSMode;
    WORD                        wListenInterval;
    BOOL                        bPWBitOn;
    WMAC_POWER_MODE             ePSMode;
    ULONG                       ulPSModeWaitTx;
    BOOL                        bPSModeTxBurst;

    // Beacon releated
    WORD                    wSeqCounter;
    BOOL                    bBeaconBufReady;
    BOOL                    bBeaconSent;
    BOOL                    bFixRate;
    BYTE                    byCurrentCh;
    UINT                    uScanTime;

    CMD_STATE               eCommandState;

    CMD_CODE                eCommand;
    BOOL                    bBeaconTx;
    BYTE                    byScanBBType;

    BOOL                    bStopBeacon;
    BOOL                    bStopDataPkt;
    BOOL                    bStopTx0Pkt;
    UINT                    uAutoReConnectTime;
    UINT                    uIsroamingTime;

    // 802.11 counter

    CMD_ITEM                eCmdQueue[CMD_Q_SIZE];
    UINT                    uCmdDequeueIdx;
    UINT                    uCmdEnqueueIdx;
    UINT                    cbFreeCmdQueue;
    BOOL                    bCmdRunning;
    BOOL                    bCmdClear;
    BOOL                    bNeedRadioOFF;

    BOOL                    bEnableRoaming;  //DavidWang
    BOOL                    bIsRoaming;  //DavidWang
    BOOL                    bFastRoaming;  //DavidWang
    BYTE                    bSameBSSMaxNum;  //Davidwang
    BYTE                    bSameBSSCurNum;  //DavidWang
    BOOL                    bRoaming;
    BOOL                    b11hEable;
    ULONG                   ulTxPower;

    // Encryption
    NDIS_802_11_WEP_STATUS  eEncryptionStatus;
    BOOL                    bTransmitKey;

//2007-0925-01<Add>by MikeLiu
//mike add :save old Encryption
    NDIS_802_11_WEP_STATUS  eOldEncryptionStatus;

    SKeyManagement          sKey;
    DWORD                   dwIVCounter;


    RC4Ext                  SBox;
    BYTE                    abyPRNG[WLAN_WEPMAX_KEYLEN+3];
    BYTE                    byKeyIndex;

    BOOL                    bAES;
    BYTE                    byCntMeasure;

    UINT                    uKeyLength;
    BYTE                    abyKey[WLAN_WEP232_KEYLEN];

    // for AP mode
    UINT                    uAssocCount;
    BOOL                    bMoreData;

    // QoS
    BOOL                    bGrpAckPolicy;


    BYTE                    byAutoFBCtrl;

    BOOL                    bTxMICFail;
    BOOL                    bRxMICFail;


    // For Update BaseBand VGA Gain Offset
    BOOL                    bUpdateBBVGA;
    UINT                    uBBVGADiffCount;
    BYTE                    byBBVGANew;
    BYTE                    byBBVGACurrent;
    BYTE                    abyBBVGA[BB_VGA_LEVEL];
    LONG                    ldBmThreshold[BB_VGA_LEVEL];

    BYTE                    byBBPreEDRSSI;
    BYTE                    byBBPreEDIndex;


    BOOL                    bRadioCmd;
    DWORD                   dwDiagRefCount;

    // For FOE Tuning
    BYTE                    byFOETuning;

    // For Auto Power Tunning

    BYTE                    byAutoPwrTunning;

    // BaseBand Loopback Use
    BYTE                    byBBCR4d;
    BYTE                    byBBCRc9;
    BYTE                    byBBCR88;
    BYTE                    byBBCR09;

    // command timer
    struct timer_list       sTimerCommand;

//2007-0115-01<Add>by MikeLiu
#ifdef TxInSleep
     struct timer_list       sTimerTxData;
     ULONG                       nTxDataTimeCout;
     BOOL  fTxDataInSleep;
     BOOL  IsTxDataTrigger;
#endif

#ifdef WPA_SM_Transtatus
    BOOL  fWPA_Authened;           //is WPA/WPA-PSK or WPA2/WPA2-PSK authen??
#endif
    BYTE            byReAssocCount;   //mike add:re-association retry times!
    BYTE            byLinkWaitCount;

    SEthernetHeader         sTxEthHeader;
    SEthernetHeader         sRxEthHeader;
    BYTE                    abyBroadcastAddr[U_ETHER_ADDR_LEN];
    BYTE                    abySNAP_RFC1042[U_ETHER_ADDR_LEN];
    BYTE                    abySNAP_Bridgetunnel[U_ETHER_ADDR_LEN];

    // Pre-Authentication & PMK cache
    SPMKID                  gsPMKID;
    SPMKIDCandidateEvent    gsPMKIDCandidate;


    // for 802.11h
    BOOL                    b11hEnable;

    BOOL                    bChannelSwitch;
    BYTE                    byNewChannel;
    BYTE                    byChannelSwitchCount;

    //WPA supplicant daemon
	struct net_device       *wpadev;
	BOOL                    bWPADEVUp;
    struct sk_buff          *skb;
    //--

#ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
        BOOL                 bwextstep0;
        BOOL                 bwextstep1;
        BOOL                 bwextstep2;
        BOOL                 bwextstep3;
        BOOL                 bWPASuppWextEnabled;
#endif

#ifdef HOSTAP
    // user space daemon: hostapd, is used for HOSTAP
	BOOL                    bEnableHostapd;
	BOOL                    bEnable8021x;
	BOOL                    bEnableHostWEP;
	struct net_device       *apdev;
	int (*tx_80211)(struct sk_buff *skb, struct net_device *dev);
#endif
    UINT                    uChannel;

	struct iw_statistics	wstats;		// wireless stats
    BOOL                    bCommit;

} DEVICE_INFO, *PSDevice;




#define EnqueueRCB(_Head, _Tail, _RCB)                  \
{                                                       \
    if (!_Head) {                                       \
        _Head = _RCB;                                   \
    }                                                   \
    else {                                              \
        _Tail->Next = _RCB;                             \
    }                                                   \
    _RCB->Next = NULL;                                  \
    _Tail = _RCB;                                       \
}

#define DequeueRCB(Head, Tail)                          \
{                                                       \
    PRCB   RCB = Head;                                  \
    if (!RCB->Next) {                                   \
        Tail = NULL;                                    \
    }                                                   \
    Head = RCB->Next;                                   \
}


#define ADD_ONE_WITH_WRAP_AROUND(uVar, uModulo) {   \
    if ((uVar) >= ((uModulo) - 1))                  \
        (uVar) = 0;                                 \
    else                                            \
        (uVar)++;                                   \
}


#define fMP_RESET_IN_PROGRESS               0x00000001
#define fMP_DISCONNECTED                    0x00000002
#define fMP_HALT_IN_PROGRESS                0x00000004
#define fMP_SURPRISE_REMOVED                0x00000008
#define fMP_RECV_LOOKASIDE                  0x00000010
#define fMP_INIT_IN_PROGRESS                0x00000020
#define fMP_SEND_SIDE_RESOURCE_ALLOCATED    0x00000040
#define fMP_RECV_SIDE_RESOURCE_ALLOCATED    0x00000080
#define fMP_POST_READS                      0x00000100
#define fMP_POST_WRITES                     0x00000200
#define fMP_CONTROL_READS                   0x00000400
#define fMP_CONTROL_WRITES                  0x00000800



#define MP_SET_FLAG(_M, _F)             ((_M)->Flags |= (_F))
#define MP_CLEAR_FLAG(_M, _F)            ((_M)->Flags &= ~(_F))
#define MP_TEST_FLAG(_M, _F)            (((_M)->Flags & (_F)) != 0)
#define MP_TEST_FLAGS(_M, _F)            (((_M)->Flags & (_F)) == (_F))

#define MP_IS_READY(_M)        (((_M)->Flags & \
                                 (fMP_DISCONNECTED | fMP_RESET_IN_PROGRESS | fMP_HALT_IN_PROGRESS | fMP_INIT_IN_PROGRESS | fMP_SURPRISE_REMOVED)) == 0)

/*---------------------  Export Functions  --------------------------*/

//BOOL device_dma0_xmit(PSDevice pDevice, struct sk_buff *skb, UINT uNodeIndex);
BOOL device_alloc_frag_buf(PSDevice pDevice, PSDeFragControlBlock pDeF);

#endif


