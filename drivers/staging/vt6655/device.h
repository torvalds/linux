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
#include <linux/version.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/if_arp.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <linux/if.h>
//#include <linux/config.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/inetdevice.h>
#include <linux/reboot.h>
#ifdef SIOCETHTOOL
#define DEVICE_ETHTOOL_IOCTL_SUPPORT
#include <linux/ethtool.h>
#else
#undef DEVICE_ETHTOOL_IOCTL_SUPPORT
#endif
/* Include Wireless Extension definition and check version - Jean II */
#include <linux/wireless.h>
#include <net/iw_handler.h>	// New driver API

//2008-0409-07, <Add> by Einsn Liu
#ifndef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
#define WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
#endif
//2008-4-14<add> by chester for led issue
//#define FOR_LED_ON_NOTEBOOK
//


//
// device specific
//

#include "kcompat.h"
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
#include "mac.h"

//PLICE_DEBUG->
//#define		THREAD

//#define	TASK_LET
//PLICE_DEBUG<-


/*---------------------  Export Definitions -------------------------*/

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

#define MAXCHECKHANGCNT         4

#define BB_VGA_LEVEL            4
#define BB_VGA_CHANGE_THRESHOLD 16



#ifndef RUN_AT
#define RUN_AT(x)                       (jiffies+(x))
#endif

// DMA related
#define RESERV_AC0DMA                   4


// BUILD OBJ mode


#define	AVAIL_TD(p,q)	((p)->sOpts.nTxDescs[(q)]-((p)->iTDUsed[(q)]))

//PLICE_DEBUG ->
#define	NUM				64
//PLICE_DEUBG <-



#define PRIVATE_Message                 0

/*---------------------  Export Types  ------------------------------*/


#define DBG_PRT(l, p, args...) {if (l<=msglevel) printk( p ,##args);}
#define PRINT_K(p, args...) {if (PRIVATE_Message) printk( p ,##args);}

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


//++ NDIS related

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
typedef struct __chip_info_tbl{
    CHIP_TYPE   chip_id;
    char*       name;
    int         io_size;
    int         nTxQueue;
    U32         flags;
} CHIP_INFO, *PCHIP_INFO;


typedef enum {
    OWNED_BY_HOST=0,
    OWNED_BY_NIC=1
} DEVICE_OWNER_TYPE, *PDEVICE_OWNER_TYPE;


// The receive duplicate detection cache entry
typedef struct tagSCacheEntry{
    WORD        wFmSequence;
    BYTE        abyAddr2[U_ETHER_ADDR_LEN];
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
#define     DEVICE_FLAGS_IP_ALIGN        0x00000001UL
#define     DEVICE_FLAGS_PREAMBLE_TYPE   0x00000002UL
#define     DEVICE_FLAGS_OP_MODE         0x00000004UL
#define     DEVICE_FLAGS_PS_MODE         0x00000008UL
#define		DEVICE_FLAGS_80211h_MODE	 0x00000010UL
#define		DEVICE_FLAGS_DiversityANT	 0x00000020UL

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


//PLICE_DEBUG->


typedef	struct _RxManagementQueue
{
	int	packet_num;
	int	head,tail;
	PSRxMgmtPacket	Q[NUM];
} RxManagementQueue,*PSRxManagementQueue;



//PLICE_DEBUG<-


typedef struct __device_opt {
    int         nRxDescs0;    //Number of RX descriptors0
    int         nRxDescs1;    //Number of RX descriptors1
    int         nTxDescs[2];  //Number of TX descriptors 0, 1
    int         int_works;    //interrupt limits
    int         rts_thresh;   //rts threshold
    int         frag_thresh;
    int         data_rate;
    int         channel_num;
    int         short_retry;
    int         long_retry;
    int         bbp_type;
    U32         flags;
} OPTIONS, *POPTIONS;


typedef struct __device_info {
    struct __device_info*        next;
    struct __device_info*        prev;

    struct pci_dev*             pcid;

#if CONFIG_PM
    u32                         pci_state[16];
#endif

// netdev
    struct net_device*          dev;
    struct net_device*          next_module;
    struct net_device_stats     stats;

//dma addr, rx/tx pool
    dma_addr_t                  pool_dma;
    dma_addr_t                  rd0_pool_dma;
    dma_addr_t                  rd1_pool_dma;

    dma_addr_t                  td0_pool_dma;
    dma_addr_t                  td1_pool_dma;

    dma_addr_t                  tx_bufs_dma0;
    dma_addr_t                  tx_bufs_dma1;
    dma_addr_t                  tx_beacon_dma;

    PBYTE                       tx0_bufs;
    PBYTE                       tx1_bufs;
    PBYTE                       tx_beacon_bufs;

    CHIP_TYPE                   chip_id;

    U32                         PortOffset;
    DWORD                       dwIsr;
    U32                         memaddr;
    U32                         ioaddr;
    U32                         io_size;

    BYTE                        byRevId;
    WORD                        SubSystemID;
    WORD                        SubVendorID;

    int                         nTxQueues;
    volatile int                iTDUsed[TYPE_MAXTD];

    volatile PSTxDesc           apCurrTD[TYPE_MAXTD];
    volatile PSTxDesc           apTailTD[TYPE_MAXTD];

    volatile PSTxDesc           apTD0Rings;
    volatile PSTxDesc           apTD1Rings;

    volatile PSRxDesc           aRD0Ring;
    volatile PSRxDesc           aRD1Ring;
    volatile PSRxDesc           pCurrRD[TYPE_MAXRD];
    SCache                      sDupRxCache;

    SDeFragControlBlock         sRxDFCB[CB_MAX_RX_FRAG];
    UINT                        cbDFCB;
    UINT                        cbFreeDFCB;
    UINT                        uCurrentDFCBIdx;

    OPTIONS                     sOpts;

    U32                         flags;

    U32                         rx_buf_sz;
    int                         multicast_limit;
    BYTE                        byRxMode;

    spinlock_t                  lock;
//PLICE_DEBUG->
	struct	tasklet_struct 		RxMngWorkItem;
	RxManagementQueue	rxManeQueue;
//PLICE_DEBUG<-
//PLICE_DEBUG ->
	pid_t				MLMEThr_pid;
	struct 	completion	notify;
	struct 	semaphore	mlme_semaphore;
//PLICE_DEBUG <-


    U32                         rx_bytes;

    // Version control
    BYTE                        byLocalID;
    BYTE                        byRFType;

    BYTE                        byMaxPwrLevel;
    BYTE                        byZoneType;
    BOOL                        bZoneRegExist;
   BYTE                        byOriginalZonetype;
    BYTE                        abyMacContext[MAC_MAX_CONTEXT_REG];
    BOOL                        bLinkPass;          // link status: OK or fail
    BYTE                        abyCurrentNetAddr[U_ETHER_ADDR_LEN];

    // Adapter statistics
    SStatCounter                scStatistic;
    // 802.11 counter
    SDot11Counters              s802_11Counter;


    // 802.11 management
    PSMgmtObject                pMgmt;
    SMgmtObject                 sMgmtObj;

    // 802.11 MAC specific
    UINT                        uCurrRSSI;
    BYTE                        byCurrSQ;

    DWORD                       dwTxAntennaSel;
    DWORD                       dwRxAntennaSel;
    BYTE                        byAntennaCount;
    BYTE                        byRxAntennaMode;
    BYTE                        byTxAntennaMode;
    BOOL                        bTxRxAntInv;

    PBYTE                       pbyTmpBuff;
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
    CARD_PHY_TYPE               eCurrentPHYType;


    VIA_BB_TYPE                 byBBType; //0: 11A, 1:11B, 2:11G
    VIA_PKT_TYPE                byPacketType; //0:11a,1:11b,2:11gb(only CCK in BasicRate),3:11ga(OFDM in Basic Rate)
    WORD                        wBasicRate;
    BYTE                        byACKRate;
    BYTE                        byTopOFDMBasicRate;
    BYTE                        byTopCCKBasicRate;

    BYTE                        byMinChannel;
    BYTE                        byMaxChannel;
    UINT                        uConnectionRate;

    BYTE                        byPreambleType;
    BYTE                        byShortPreamble;

    WORD                        wCurrentRate;
    WORD                        wRTSThreshold;
    WORD                        wFragmentationThreshold;
    BYTE                        byShortRetryLimit;
    BYTE                        byLongRetryLimit;
    CARD_OP_MODE                eOPMode;
    BYTE                        byOpMode;
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
    BOOL                        bShortSlotTime;
    BOOL                        bProtectMode;
    BOOL                        bNonERPPresent;
    BOOL                        bBarkerPreambleMd;

    BYTE                        byERPFlag;
    WORD                        wUseProtectCntDown;

    BOOL                        bRadioControlOff;
    BOOL                        bRadioOff;
    BOOL                    bEnablePSMode;
    WORD                    wListenInterval;
    BOOL                    bPWBitOn;
    WMAC_POWER_MODE         ePSMode;


    // GPIO Radio Control
    BYTE                    byRadioCtl;
    BYTE                    byGPIO;
    BOOL                    bHWRadioOff;
    BOOL                    bPrvActive4RadioOFF;
    BOOL                    bGPIOBlockRead;

    // Beacon releated
    WORD                    wSeqCounter;
    WORD                    wBCNBufLen;
    BOOL                    bBeaconBufReady;
    BOOL                    bBeaconSent;
    BOOL                    bIsBeaconBufReadySet;
    UINT                    cbBeaconBufReadySetCnt;
    BOOL                    bFixRate;
    BYTE                    byCurrentCh;
    UINT                    uScanTime;

    CMD_STATE               eCommandState;

    CMD_CODE                eCommand;
    BOOL                    bBeaconTx;

    BOOL                    bStopBeacon;
    BOOL                    bStopDataPkt;
    BOOL                    bStopTx0Pkt;
    UINT                    uAutoReConnectTime;

    // 802.11 counter

    CMD_ITEM                eCmdQueue[CMD_Q_SIZE];
    UINT                    uCmdDequeueIdx;
    UINT                    uCmdEnqueueIdx;
    UINT                    cbFreeCmdQueue;
    BOOL                    bCmdRunning;
    BOOL                    bCmdClear;



    BOOL                    bRoaming;
    //WOW
    BYTE                    abyIPAddr[4];

    ULONG                   ulTxPower;
    NDIS_802_11_WEP_STATUS  eEncryptionStatus;
    BOOL                    bTransmitKey;
//2007-0925-01<Add>by MikeLiu
//mike add :save old Encryption
    NDIS_802_11_WEP_STATUS  eOldEncryptionStatus;

    SKeyManagement          sKey;
    DWORD                   dwIVCounter;

    QWORD                   qwPacketNumber; //For CCMP and TKIP as TSC(6 bytes)
    UINT                    uCurrentWEPMode;

    RC4Ext                  SBox;
    BYTE                    abyPRNG[WLAN_WEPMAX_KEYLEN+3];
    BYTE                    byKeyIndex;
    UINT                    uKeyLength;
    BYTE                    abyKey[WLAN_WEP232_KEYLEN];

    BOOL                    bAES;
    BYTE                    byCntMeasure;

    // for AP mode
    UINT                    uAssocCount;
    BOOL                    bMoreData;

    // QoS
    BOOL                    bGrpAckPolicy;

    // for OID_802_11_ASSOCIATION_INFORMATION
    BOOL                    bAssocInfoSet;


    BYTE                    byAutoFBCtrl;

    BOOL                    bTxMICFail;
    BOOL                    bRxMICFail;


    UINT                    uRATEIdx;


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
    SHORT                   sPSetPointCCK;
    SHORT                   sPSetPointOFDMG;
    SHORT                   sPSetPointOFDMA;
    LONG                    lPFormulaOffset;
    SHORT                   sPThreshold;
    CHAR                    cAdjustStep;
    CHAR                    cMinTxAGC;

    // For RF Power table
    BYTE                    byCCKPwr;
    BYTE                    byOFDMPwrG;
    BYTE                    byCurPwr;
    I8                      byCurPwrdBm;
    BYTE                    abyCCKPwrTbl[CB_MAX_CHANNEL_24G+1];
    BYTE                    abyOFDMPwrTbl[CB_MAX_CHANNEL+1];
    I8                      abyCCKDefaultPwr[CB_MAX_CHANNEL_24G+1];
    I8                      abyOFDMDefaultPwr[CB_MAX_CHANNEL+1];
    I8                      abyRegPwr[CB_MAX_CHANNEL+1];
    I8                      abyLocalPwr[CB_MAX_CHANNEL+1];


    // BaseBand Loopback Use
    BYTE                    byBBCR4d;
    BYTE                    byBBCRc9;
    BYTE                    byBBCR88;
    BYTE                    byBBCR09;

    // command timer
    struct timer_list       sTimerCommand;
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


    BYTE                    abyNodeName[17];

    BOOL                    bDiversityRegCtlON;
    BOOL                    bDiversityEnable;
    ULONG                   ulDiversityNValue;
    ULONG                   ulDiversityMValue;
    BYTE                    byTMax;
    BYTE                    byTMax2;
    BYTE                    byTMax3;
    ULONG                   ulSQ3TH;

// ANT diversity
    ULONG                   uDiversityCnt;
    BYTE                    byAntennaState;
    ULONG                   ulRatio_State0;
    ULONG                   ulRatio_State1;

    //SQ3 functions for antenna diversity
    struct timer_list           TimerSQ3Tmax1;
    struct timer_list           TimerSQ3Tmax2;
    struct timer_list           TimerSQ3Tmax3;


    ULONG                   uNumSQ3[MAX_RATE];
    WORD                    wAntDiversityMaxRate;


    SEthernetHeader         sTxEthHeader;
    SEthernetHeader         sRxEthHeader;
    BYTE                    abyBroadcastAddr[U_ETHER_ADDR_LEN];
    BYTE                    abySNAP_RFC1042[U_ETHER_ADDR_LEN];
    BYTE                    abySNAP_Bridgetunnel[U_ETHER_ADDR_LEN];
     BYTE                        abyEEPROM[EEP_MAX_CONTEXT_SIZE];  //DWORD alignment
    // Pre-Authentication & PMK cache
    SPMKID                  gsPMKID;
    SPMKIDCandidateEvent    gsPMKIDCandidate;


    // for 802.11h
    BOOL                    b11hEnable;
    BYTE                    abyCountryCode[3];
    // for 802.11h DFS
    UINT                    uNumOfMeasureEIDs;
    PWLAN_IE_MEASURE_REQ    pCurrMeasureEID;
    BOOL                    bMeasureInProgress;
    BYTE                    byOrgChannel;
    BYTE                    byOrgRCR;
    DWORD                   dwOrgMAR0;
    DWORD                   dwOrgMAR4;
    BYTE                    byBasicMap;
    BYTE                    byCCAFraction;
    BYTE                    abyRPIs[8];
    DWORD                   dwRPIs[8];
    BOOL                    bChannelSwitch;
    BYTE                    byNewChannel;
    BYTE                    byChannelSwitchCount;
    BOOL                    bQuietEnable;
    BOOL                    bEnableFirstQuiet;
    BYTE                    byQuietStartCount;
    UINT                    uQuietEnqueue;
    DWORD                   dwCurrentQuietEndTime;
    SQuietControl           sQuiet[MAX_QUIET_COUNT];
    // for 802.11h TPC
    BOOL                    bCountryInfo5G;
    BOOL                    bCountryInfo24G;

    WORD                    wBeaconInterval;

    //WPA supplicant deamon
	struct net_device       *wpadev;
	BOOL                    bWPADEVUp;
    struct sk_buff          *skb;
#ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
/*
        BOOL                 bwextstep0;
        BOOL                 bwextstep1;
        BOOL                 bwextstep2;
        BOOL                 bwextstep3;
        */
        UINT                   bwextcount;
        BOOL                 bWPASuppWextEnabled;
#endif

    //--
#ifdef HOSTAP
    // user space daemon: hostapd, is used for HOSTAP
	BOOL                    bEnableHostapd;
	BOOL                    bEnable8021x;
	BOOL                    bEnableHostWEP;
	struct net_device       *apdev;
	int (*tx_80211)(struct sk_buff *skb, struct net_device *dev);
#endif
    UINT                    uChannel;
    BOOL                    bMACSuspend;

	struct iw_statistics	wstats;		// wireless stats
    BOOL                    bCommit;

} DEVICE_INFO, *PSDevice;


//PLICE_DEBUG->


 inline  static	VOID   EnQueue (PSDevice pDevice,PSRxMgmtPacket  pRxMgmtPacket)
{
	//printk("Enter EnQueue:tail is %d\n",pDevice->rxManeQueue.tail);
	if ((pDevice->rxManeQueue.tail+1) % NUM == pDevice->rxManeQueue.head)
	{
		//printk("Queue is Full,tail is %d\n",pDevice->rxManeQueue.tail);
		return ;
	}
	else
	{
		pDevice->rxManeQueue.tail = (pDevice->rxManeQueue.tail+1)% NUM;
		pDevice->rxManeQueue.Q[pDevice->rxManeQueue.tail] = pRxMgmtPacket;
		pDevice->rxManeQueue.packet_num++;
		//printk("packet num is %d\n",pDevice->rxManeQueue.packet_num);
	}
}




	inline  static  PSRxMgmtPacket DeQueue (PSDevice pDevice)
{
	PSRxMgmtPacket  pRxMgmtPacket;
	if (pDevice->rxManeQueue.tail == pDevice->rxManeQueue.head)
	{
		printk("Queue is Empty\n");
		return NULL;
	}
	else
	{
		int	x;
		//x=pDevice->rxManeQueue.head = (pDevice->rxManeQueue.head+1)%NUM;
		pDevice->rxManeQueue.head = (pDevice->rxManeQueue.head+1)%NUM;
		x = pDevice->rxManeQueue.head;
		//printk("Enter DeQueue:head is %d\n",x);
		pRxMgmtPacket = pDevice->rxManeQueue.Q[x];
		pDevice->rxManeQueue.packet_num--;
		return pRxMgmtPacket;
	}
}

VOID	InitRxManagementQueue(PSDevice   pDevice);



//PLICE_DEBUG<-






inline static BOOL device_get_ip(PSDevice pInfo) {
    struct in_device* in_dev=(struct in_device*) pInfo->dev->ip_ptr;
    struct in_ifaddr* ifa;

    if (in_dev!=NULL) {
        ifa=(struct in_ifaddr*) in_dev->ifa_list;
        if (ifa!=NULL) {
            memcpy(pInfo->abyIPAddr,&ifa->ifa_address,4);
            return TRUE;
        }
    }
    return FALSE;
}



static inline PDEVICE_RD_INFO alloc_rd_info(void) {
    PDEVICE_RD_INFO  ptr;
    if ((ptr = (PDEVICE_RD_INFO)kmalloc((int)sizeof(DEVICE_RD_INFO), (int)GFP_ATOMIC)) == NULL)
        return NULL;
    else {
        memset(ptr,0,sizeof(DEVICE_RD_INFO));
        return ptr;
    }
}

static inline PDEVICE_TD_INFO alloc_td_info(void) {
    PDEVICE_TD_INFO  ptr;
    if ((ptr = (PDEVICE_TD_INFO)kmalloc((int)sizeof(DEVICE_TD_INFO), (int)GFP_ATOMIC))==NULL)
        return NULL;
    else {
        memset(ptr,0,sizeof(DEVICE_TD_INFO));
        return ptr;
    }
}

/*---------------------  Export Functions  --------------------------*/

BOOL device_dma0_xmit(PSDevice pDevice, struct sk_buff *skb, UINT uNodeIndex);
BOOL device_alloc_frag_buf(PSDevice pDevice, PSDeFragControlBlock pDeF);
int Config_FileOperation(PSDevice pDevice,BOOL fwrite,unsigned char *Parameter);
#endif


