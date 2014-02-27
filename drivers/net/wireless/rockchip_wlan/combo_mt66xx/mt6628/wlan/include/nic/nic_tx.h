/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/include/nic/nic_tx.h#1 $
*/

/*! \file   nic_tx.h
    \brief  Functions that provide TX operation in NIC's point of view.

    This file provides TX functions which are responsible for both Hardware and
    Software Resource Management and keep their Synchronization.

*/



/*
** $Log: nic_tx.h $
 *
 * 11 18 2011 eddie.chen
 * [WCXRP00001096] [MT6620 Wi-Fi][Driver/FW] Enhance the log function (xlog)
 * Add log counter for tx
 *
 * 11 10 2011 eddie.chen
 * [WCXRP00001096] [MT6620 Wi-Fi][Driver/FW] Enhance the log function (xlog)
 * Add TX_DONE status detail information.
 *
 * 08 15 2011 cp.wu
 * [WCXRP00000851] [MT6628 Wi-Fi][Driver] Add HIFSYS related definition to driver source tree
 * add MT6628-specific definitions.
 *
 * 04 12 2011 cp.wu
 * [WCXRP00000631] [MT6620 Wi-Fi][Driver] Add an API for QM to retrieve current TC counter value and processing frame dropping cases for TC4 path
 * 1. add nicTxGetResource() API for QM to make decisions.
 * 2. if management frames is decided by QM for dropping, the call back is invoked to indicate such a case.
 *
 * 03 21 2011 cp.wu
 * [WCXRP00000540] [MT5931][Driver] Add eHPI8/eHPI16 support to Linux Glue Layer
 * portability improvement
 *
 * 02 16 2011 cp.wu
 * [WCXRP00000449] [MT6620 Wi-Fi][Driver] Refine CMD queue handling by adding an extra API for checking availble count and modify behavior
 * 1. add new API: nicTxGetFreeCmdCount()
 * 2. when there is insufficient command descriptor, nicTxEnqueueMsdu() will drop command packets directly
 *
 * 01 24 2011 cp.wu
 * [WCXRP00000382] [MT6620 Wi-Fi][Driver] Track forwarding packet number with notifying tx thread for serving
 * 1. add an extra counter for tracking pending forward frames.
 * 2. notify TX service thread as well when there is pending forward frame
 * 3. correct build errors leaded by introduction of Wi-Fi direct separation module
 *
 * 12 15 2010 yuche.tsai
 * NULL
 * Update SLT Descriptor number configure in driver.
 *
 * 11 16 2010 yarco.yang
 * [WCXRP00000177] [MT5931 F/W] Performance tuning for 1st connection
 * Update TX buffer count
 *
 * 11 03 2010 cp.wu
 * [WCXRP00000083] [MT5931][Driver][FW] Add necessary logic for MT5931 first connection
 * 1) use 8 buffers for MT5931 which is equipped with less memory
 * 2) modify MT5931 debug level to TRACE when download is successful
 *
 * 10 18 2010 cp.wu
 * [WCXRP00000117] [MT6620 Wi-Fi][Driver] Add logic for suspending driver when MT6620 is not responding anymore
 * 1. when wlanAdapterStop() failed to send POWER CTRL command to firmware, do not poll for ready bit dis-assertion
 * 2. shorten polling count for shorter response time
 * 3. if bad I/O operation is detected during TX resource polling, then further operation is aborted as well
 *
 * 10 06 2010 cp.wu
 * [WCXRP00000052] [MT6620 Wi-Fi][Driver] Eliminate Linux Compile Warning
 * code reorganization to improve isolation between GLUE and CORE layers.
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 08 30 2010 cp.wu
 * NULL
 * API added: nicTxPendingPackets(), for simplifying porting layer
 *
 * 07 26 2010 cp.wu
 * 
 * change TC4 initial value from 2 to 4.
 *
 * 07 13 2010 cp.wu
 * 
 * 1) MMPDUs are now sent to MT6620 by CMD queue for keeping strict order of 1X/MMPDU/CMD packets
 * 2) integrate with qmGetFrameAction() for deciding which MMPDU/1X could pass checking for sending
 * 2) enhance CMD_INFO_T descriptor number from 10 to 32 to avoid descriptor underflow under concurrent network operation
 *
 * 07 08 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 07 06 2010 yarco.yang
 * [WPD00003837][MT6620]Data Path Refine 
 * Add MGMT Packet type for HIF_TX_HEADER
 *
 * 06 23 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * integrate .
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * refine TX-DONE callback.
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * TX descriptors are now allocated once for reducing allocation overhead
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * specify correct value for management frames.
 *
 * 06 11 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * 1) migrate assoc.c.
 * 2) add ucTxSeqNum for tracking frames which needs TX-DONE awareness
 * 3) add configuration options for CNM_MEM and RSN modules
 * 4) add data path for management frames
 * 5) eliminate rPacketInfo of MSDU_INFO_T
 *
 * 06 10 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * 1) add flag on MSDU_INFO_T for indicating BIP frame and forceBasicRate
 * 2) add  packet type for indicating management frames
 *
 * 06 09 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * add necessary changes to driver data paths.
 *
 * 06 09 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * add TX_PACKET_MGMT to indicate the frame is coming from management modules
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * merge wlan_def.h.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base 
 * [MT6620 5931] Create driver base
 *
 * 03 30 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP 
 * remove driver-land statistics.
 *
 * 03 24 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP 
 * generate information for OID_GEN_RCV_OK & OID_GEN_XMIT_OK
 *  *  * 
 *
 * 03 10 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP 
 * code clean: removing unused variables and structure definitions
 *
 * 03 02 2010 tehuang.liu
 * [WPD00001943]Create WiFi test driver framework on WinXP 
 * Redistributed the initial TC resources for normal operation
 *
 * 03 02 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add mutex to avoid multiple access to qmTxQueue simultaneously.
 *
 * 02 23 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add new API: wlanProcessQueuedPackets()
 *
 * 02 10 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) remove unused function in nic_rx.c [which has been handled in que_mgt.c]
 *  *  * 2) firmware image length is now retrieved via NdisFileOpen
 *  *  * 3) firmware image is not structured by (P_IMG_SEC_HDR_T) anymore
 *  *  * 4) nicRxWaitResponse() revised
 *  *  * 5) another set of TQ counter default value is added for fw-download state
 *  *  * 6) Wi-Fi load address is now retrieved from registry too
 *
 * 02 09 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1. Permanent and current MAC address are now retrieved by CMD/EVENT packets instead of hard-coded address
 *  *  *  * 2. follow MSDN defined behavior when associates to another AP
 *  *  *  * 3. for firmware download, packet size could be up to 2048 bytes
 *
 * 01 27 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1. eliminate improper variable in rHifInfo
 *  *  *  *  * 2. block TX/ordinary OID when RF test mode is engaged
 *  *  *  *  * 3. wait until firmware finish operation when entering into and leaving from RF test mode
 *  *  *  *  * 4. correct some HAL implementation
 *
 * 01 13 2010 tehuang.liu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * Enabled the Burst_End Indication mechanism
 *
 * 12 30 2009 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) According to CMD/EVENT documentation v0.8,
 *  *  *  *  * OID_CUSTOM_TEST_RX_STATUS & OID_CUSTOM_TEST_TX_STATUS is no longer used,
 *  *  *  *  * and result is retrieved by get ATInfo instead
 *  *  *  *  * 2) add 4 counter for recording aggregation statistics
**  \main\maintrunk.MT6620WiFiDriver_Prj\24 2009-12-10 16:53:28 GMT mtk02752
**  remove unused API
**  \main\maintrunk.MT6620WiFiDriver_Prj\23 2009-11-27 11:08:00 GMT mtk02752
**  add flush for reset
**  \main\maintrunk.MT6620WiFiDriver_Prj\22 2009-11-24 19:56:49 GMT mtk02752
**  remove redundant eTC
**  \main\maintrunk.MT6620WiFiDriver_Prj\21 2009-11-23 22:01:08 GMT mtk02468
**  Added MSDU_INFO fields for composing HIF TX header
**  \main\maintrunk.MT6620WiFiDriver_Prj\20 2009-11-17 22:40:51 GMT mtk01084
**  \main\maintrunk.MT6620WiFiDriver_Prj\19 2009-11-17 17:35:05 GMT mtk02752
**  + nicTxMsduInfoList() for sending MsduInfoList
**  + NIC_TX_BUFF_COUNT_TC[0~5]
**  \main\maintrunk.MT6620WiFiDriver_Prj\18 2009-11-17 11:07:00 GMT mtk02752
**  add nicTxAdjustTcq() API
**  \main\maintrunk.MT6620WiFiDriver_Prj\17 2009-11-16 22:28:30 GMT mtk02752
**  move aucFreeBufferCount/aucMaxNumOfBuffer into another structure
**  \main\maintrunk.MT6620WiFiDriver_Prj\16 2009-11-16 21:44:50 GMT mtk02752
**  + nicTxReturnMsduInfo()
**  + nicTxFillMsduInfo()
**  + rFreeMsduInfoList field in TX_CTRL
**  \main\maintrunk.MT6620WiFiDriver_Prj\15 2009-11-16 18:00:43 GMT mtk02752
**  use P_PACKET_INFO_T for prPacket to avoid inventing another new structure for packet
**  \main\maintrunk.MT6620WiFiDriver_Prj\14 2009-11-16 15:28:49 GMT mtk02752
**  add ucQueuedPacketNum for indicating how many packets are queued by per STA/AC queue
**  \main\maintrunk.MT6620WiFiDriver_Prj\13 2009-11-16 10:52:01 GMT mtk02752
**  \main\maintrunk.MT6620WiFiDriver_Prj\12 2009-11-14 23:39:24 GMT mtk02752
**  interface structure redefine
**  \main\maintrunk.MT6620WiFiDriver_Prj\11 2009-11-13 21:17:03 GMT mtk02752
**  \main\maintrunk.MT6620WiFiDriver_Prj\10 2009-10-29 19:53:10 GMT mtk01084
**  remove strange code by Frog
**  \main\maintrunk.MT6620WiFiDriver_Prj\9 2009-10-13 21:59:04 GMT mtk01084
**  update for new HW architecture design
**  \main\maintrunk.MT6620WiFiDriver_Prj\8 2009-10-02 13:53:03 GMT mtk01725
**  \main\maintrunk.MT6620WiFiDriver_Prj\7 2009-04-28 10:36:50 GMT mtk01461
**  Add declaration of nicTxReleaseResource()
**  \main\maintrunk.MT6620WiFiDriver_Prj\6 2009-04-17 19:58:39 GMT mtk01461
**  Move CMD_INFO_T related define and function to cmd_buf.h
**  \main\maintrunk.MT6620WiFiDriver_Prj\5 2009-04-01 10:53:53 GMT mtk01461
**  Add function for SDIO_TX_ENHANCE
**  \main\maintrunk.MT6620WiFiDriver_Prj\4 2009-03-23 00:33:27 GMT mtk01461
**  Define constants for TX PATH and add nicTxPollingResource
**  \main\maintrunk.MT6620WiFiDriver_Prj\3 2009-03-16 09:09:32 GMT mtk01461
**  Update TX PATH API
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-03-10 20:16:38 GMT mtk01426
**  Init for develop
**
*/

#ifndef _NIC_TX_H
#define _NIC_TX_H

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
#define NIC_TX_RESOURCE_POLLING_TIMEOUT     256
#define NIC_TX_RESOURCE_POLLING_DELAY_MSEC  50

/* Maximum buffer count for individual HIF TCQ */

#if defined(MT6620)
#if CFG_SLT_SUPPORT
    /* 20101215 mtk01725 Redistributed the initial TC resources for SLT operation */
    #define NIC_TX_BUFF_COUNT_TC0       0   // First connection: 0
    #define NIC_TX_BUFF_COUNT_TC1       16   // First connection: 32
    #define NIC_TX_BUFF_COUNT_TC2       0   // First connection: 0
    #define NIC_TX_BUFF_COUNT_TC3       0   // First connection: 0
    #define NIC_TX_BUFF_COUNT_TC4       4   // First connection: 2
    #define NIC_TX_BUFF_COUNT_TC5       0   // First connection: 0
#else
    /* 20100302 mtk02468 Redistributed the initial TC resources for normal operation */
    #define NIC_TX_BUFF_COUNT_TC0       6   // First connection: 0
    #define NIC_TX_BUFF_COUNT_TC1       8   // First connection: 32
    #define NIC_TX_BUFF_COUNT_TC2       8   // First connection: 0
    #define NIC_TX_BUFF_COUNT_TC3       8   // First connection: 0
    #define NIC_TX_BUFF_COUNT_TC4       4   // First connection: 2
    #define NIC_TX_BUFF_COUNT_TC5       2   // First connection: 0
#endif
#elif defined(MT5931) 
    #define NIC_TX_BUFF_COUNT_TC0       1   // First connection: 0
    #define NIC_TX_BUFF_COUNT_TC1       14  // First connection: 32
    #define NIC_TX_BUFF_COUNT_TC2       1   // First connection: 0
    #define NIC_TX_BUFF_COUNT_TC3       1   // First connection: 0
    #define NIC_TX_BUFF_COUNT_TC4       4   // First connection: 2
    #define NIC_TX_BUFF_COUNT_TC5       1   // First connection: 0
#elif defined(MT6628)
    #define NIC_TX_BUFF_COUNT_TC0       1   // First connection: 0
    #define NIC_TX_BUFF_COUNT_TC1       20  // First connection: 32
    #define NIC_TX_BUFF_COUNT_TC2       1   // First connection: 0
    #define NIC_TX_BUFF_COUNT_TC3       1   // First connection: 0
    #define NIC_TX_BUFF_COUNT_TC4       4   // First connection: 2
    #define NIC_TX_BUFF_COUNT_TC5       1   // First connection: 0

#endif

#define NIC_TX_BUFF_SUM                     (NIC_TX_BUFF_COUNT_TC0 + \
                                            NIC_TX_BUFF_COUNT_TC1 + \
                                            NIC_TX_BUFF_COUNT_TC2 + \
                                            NIC_TX_BUFF_COUNT_TC3 + \
                                            NIC_TX_BUFF_COUNT_TC4 + \
                                            NIC_TX_BUFF_COUNT_TC5)
#if CFG_ENABLE_FW_DOWNLOAD

    #define NIC_TX_INIT_BUFF_COUNT_TC0               8
    #define NIC_TX_INIT_BUFF_COUNT_TC1               0
    #define NIC_TX_INIT_BUFF_COUNT_TC2               0
    #define NIC_TX_INIT_BUFF_COUNT_TC3               0
    #define NIC_TX_INIT_BUFF_COUNT_TC4               0
    #define NIC_TX_INIT_BUFF_COUNT_TC5               0

    #define NIC_TX_INIT_BUFF_SUM                    (NIC_TX_INIT_BUFF_COUNT_TC0 + \
                                                    NIC_TX_INIT_BUFF_COUNT_TC1 + \
                                                    NIC_TX_INIT_BUFF_COUNT_TC2 + \
                                                    NIC_TX_INIT_BUFF_COUNT_TC3 + \
                                                    NIC_TX_INIT_BUFF_COUNT_TC4 + \
                                                    NIC_TX_INIT_BUFF_COUNT_TC5)

#endif

#if CFG_ENABLE_PKT_LIFETIME_PROFILE
#define NIC_TX_TIME_THRESHOLD                       100     //in unit of ms
#endif

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
//3 /* Session for TX QUEUES */
/* The definition in this ENUM is used to categorize packet's Traffic Class according
 * to the their TID(User Priority).
 * In order to achieve QoS goal, a particular TC should not block the process of
 * another packet with different TC.
 * In current design we will have 5 categories(TCs) of SW resource.
 */
typedef enum _ENUM_TRAFFIC_CLASS_INDEX_T {
    TC0_INDEX = 0,   /* HIF TX0: AC0 packets */
    TC1_INDEX,       /* HIF TX0: AC1 packets & non-QoS packets */
    TC2_INDEX,       /* HIF TX0: AC2 packets */
    TC3_INDEX,       /* HIF TX0: AC3 packets */
    TC4_INDEX,       /* HIF TX1: Command packets or 802.1x packets */
    TC5_INDEX,       /* HIF TX0: BMCAST packets */
    TC_NUM           /* Maximum number of Traffic Classes. */
} ENUM_TRAFFIC_CLASS_INDEX_T;

typedef enum _ENUM_TX_STATISTIC_COUNTER_T {
    TX_MPDU_TOTAL_COUNT = 0,
    TX_INACTIVE_BSS_DROP,
    TX_INACTIVE_STA_DROP,
    TX_FORWARD_OVERFLOW_DROP,
    TX_AP_BORADCAST_DROP,
    TX_STATISTIC_COUNTER_NUM
} ENUM_TX_STATISTIC_COUNTER_T;


typedef struct _TX_TCQ_STATUS_T {
    UINT_8                  aucFreeBufferCount[TC_NUM];
    UINT_8                  aucMaxNumOfBuffer[TC_NUM];
} TX_TCQ_STATUS_T, *P_TX_TCQ_STATUS_T;

typedef struct _TX_TCQ_ADJUST_T {
    INT_8                   acVariation[TC_NUM];
} TX_TCQ_ADJUST_T, *P_TX_TCQ_ADJUST_T;

typedef struct _TX_CTRL_T {
    UINT_32                 u4TxCachedSize;
    PUINT_8                 pucTxCached;

/* Elements below is classified according to TC (Traffic Class) value. */

    TX_TCQ_STATUS_T         rTc;

    PUINT_8                 pucTxCoalescingBufPtr;

    QUE_T                   rFreeMsduInfoList;

    /* Management Frame Tracking */
    /* number of management frames to be sent */
    INT_32                  i4TxMgmtPendingNum; 

    /* to tracking management frames need TX done callback */
    QUE_T                   rTxMgmtTxingQueue;

#if CFG_HIF_STATISTICS
    UINT_32                 u4TotalTxAccessNum;
    UINT_32                 u4TotalTxPacketNum;
#endif
    UINT_32                 au4Statistics[TX_STATISTIC_COUNTER_NUM];

    /* Number to track forwarding frames */
    INT_32                  i4PendingFwdFrameCount;

} TX_CTRL_T, *P_TX_CTRL_T;

typedef enum _ENUM_TX_PACKET_SRC_T {
    TX_PACKET_OS,
    TX_PACKET_OS_OID,
    TX_PACKET_FORWARDING,
    TX_PACKET_MGMT,
    TX_PACKET_NUM
} ENUM_TX_PACKET_SRC_T;

typedef enum _ENUM_HIF_TX_PACKET_TYPE_T {
    HIF_TX_PACKET_TYPE_DATA = 0,
    HIF_TX_PACKET_TYPE_COMMAND,
    HIF_TX_PACKET_TYPE_HIF_LB,
    HIF_TX_PACKET_TYPE_MGMT
} ENUM_HIF_TX_PACKET_TYPE_T, *P_ENUM_HIF_TX_PACKET_TYPE_T;

typedef enum _ENUM_TX_RESULT_CODE_T {
    TX_RESULT_SUCCESS = 0,
    TX_RESULT_LIFE_TIMEOUT,
    TX_RESULT_RTS_ERROR,
    TX_RESULT_MPDU_ERROR,
    TX_RESULT_AGING_TIMEOUT,
    TX_RESULT_FLUSHED, 
    TX_RESULT_DROPPED_IN_DRIVER = 32,
    TX_RESULT_NUM
} ENUM_TX_RESULT_CODE_T, *P_ENUM_TX_RESULT_CODE_T;

/* TX Call Back Function  */
typedef WLAN_STATUS (*PFN_TX_DONE_HANDLER) (
    IN P_ADAPTER_T              prAdapter,
    IN P_MSDU_INFO_T            prMsduInfo,
    IN ENUM_TX_RESULT_CODE_T    rTxDoneStatus
    );

#if CFG_ENABLE_PKT_LIFETIME_PROFILE
typedef struct _PKT_PROFILE_T {
    BOOLEAN fgIsValid;
#if CFG_PRINT_RTP_PROFILE
    BOOLEAN fgIsPrinted;
    UINT_16 u2IpSn;
    UINT_16 u2RtpSn;
    UINT_8  ucTcxFreeCount;
#endif    
    OS_SYSTIME rHardXmitArrivalTimestamp;
    OS_SYSTIME rEnqueueTimestamp;
    OS_SYSTIME rDequeueTimestamp;
    OS_SYSTIME rHifTxDoneTimestamp;
}PKT_PROFILE_T, *P_PKT_PROFILE_T;
#endif

/* TX transactions could be divided into 4 kinds:
 * 
 * 1) 802.1X / Bluetooth-over-Wi-Fi Security Frames
 *    [CMD_INFO_T] - [prPacket] - in skb or NDIS_PACKET form
 *
 * 2) MMPDU
 *    [CMD_INFO_T] - [prPacket] - [MSDU_INFO_T] - [prPacket] - direct buffer for frame body
 *
 * 3) Command Packets
 *    [CMD_INFO_T] - [pucInfoBuffer] - direct buffer for content of command packet
 *
 * 4) Normal data frame
 *    [MSDU_INFO_T] - [prPacket] - in skb or NDIS_PACKET form
 */


/* PS_FORWARDING_TYPE_NON_PS means that the receiving STA is in Active Mode
*   from the perspective of host driver (maybe not synchronized with FW --> SN is needed)
*/

struct _MSDU_INFO_T {
    QUE_ENTRY_T                 rQueEntry;
    P_NATIVE_PACKET             prPacket;

    ENUM_TX_PACKET_SRC_T        eSrc;       /* specify OS/FORWARD packet */
    UINT_8                      ucUserPriority;

    /* For composing HIF TX header */
    UINT_8  ucTC;                   /* Traffic Class: 0~4 (HIF TX0), 5 (HIF TX1) */
    UINT_8  ucPacketType;           /* 0: Data, 1: Command, 2: HIF Loopback 3: Management Frame */
    UINT_8  ucStaRecIndex;
    UINT_8  ucNetworkType;          /* See ENUM_NETWORK_TYPE_T */
    UINT_8  ucFormatID;             /* 0: MAUI, Linux, Windows NDIS 5.1 */
    BOOLEAN fgIs802_1x;             /* TRUE: 802.1x frame */
    BOOLEAN fgIs802_11;             /* TRUE: 802.11 header is present */
    UINT_16 u2PalLLH;               /* PAL Logical Link Header (for BOW network) */
    UINT_16 u2AclSN;                /* ACL Sequence Number (for BOW network) */
    UINT_8  ucPsForwardingType;     /* See ENUM_PS_FORWARDING_TYPE_T */
    UINT_8  ucPsSessionID;          /* PS Session ID specified by the FW for the STA */
    BOOLEAN fgIsBurstEnd;           /* TRUE means this is the last packet of the burst for (STA, TID) */
    BOOLEAN fgIsBIP;                /* Management Frame Protection */
    BOOLEAN fgIsBasicRate;          /* Force Basic Rate Transmission */

    /* flattened from PACKET_INFO_T */
    UINT_8  ucMacHeaderLength;
    UINT_8  ucLlcLength; /* w/o EtherType */
    UINT_16	u2FrameLength;
    UINT_8  aucEthDestAddr[MAC_ADDR_LEN]; /* Ethernet Destination Address */

    /* for TX done tracking */
    UINT_8                      ucTxSeqNum;
    PFN_TX_DONE_HANDLER         pfTxDoneHandler;

#if CFG_ENABLE_PKT_LIFETIME_PROFILE
    PKT_PROFILE_T               rPktProfile;
#endif
};

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

#define TX_INC_CNT(prTxCtrl, eCounter)              \
    {((P_TX_CTRL_T)prTxCtrl)->au4Statistics[eCounter]++;}

#define TX_ADD_CNT(prTxCtrl, eCounter, u8Amount)    \
    {((P_TX_CTRL_T)prTxCtrl)->au4Statistics[eCounter] += (UINT_32)u8Amount;}

#define TX_GET_CNT(prTxCtrl, eCounter)              \
    (((P_TX_CTRL_T)prTxCtrl)->au4Statistics[eCounter])

#define TX_RESET_ALL_CNTS(prTxCtrl)                 \
    {kalMemZero(&prTxCtrl->au4Statistics[0], sizeof(prTxCtrl->au4Statistics));}

#if CFG_ENABLE_PKT_LIFETIME_PROFILE
#define PRINT_PKT_PROFILE(_pkt_profile, _note) \
    { \
        if(!(_pkt_profile)->fgIsPrinted) { \
            DBGLOG(TX, TRACE, ("X[%lu] E[%lu] D[%lu] HD[%lu] B[%d] RTP[%d] %s\n", \
                    (UINT_32)((_pkt_profile)->rHardXmitArrivalTimestamp), \
                    (UINT_32)((_pkt_profile)->rEnqueueTimestamp), \
                    (UINT_32)((_pkt_profile)->rDequeueTimestamp), \
                    (UINT_32)((_pkt_profile)->rHifTxDoneTimestamp), \
                    (UINT_8)((_pkt_profile)->ucTcxFreeCount), \
                    (UINT_16)((_pkt_profile)->u2RtpSn), \
                    (_note))); \
            (_pkt_profile)->fgIsPrinted = TRUE; \
        } \
    }

#define CHK_PROFILES_DELTA(_pkt1, _pkt2, _delta) \
           (CHECK_FOR_TIMEOUT((_pkt1)->rHardXmitArrivalTimestamp, (_pkt2)->rHardXmitArrivalTimestamp, (_delta)) || \
            CHECK_FOR_TIMEOUT((_pkt1)->rEnqueueTimestamp, (_pkt2)->rEnqueueTimestamp, (_delta)) || \
            CHECK_FOR_TIMEOUT((_pkt1)->rDequeueTimestamp, (_pkt2)->rDequeueTimestamp, (_delta)) || \
            CHECK_FOR_TIMEOUT((_pkt1)->rHifTxDoneTimestamp, (_pkt2)->rHifTxDoneTimestamp, (_delta)))

#define CHK_PROFILE_DELTA(_pkt, _delta) \
           (CHECK_FOR_TIMEOUT((_pkt)->rEnqueueTimestamp, (_pkt)->rHardXmitArrivalTimestamp, (_delta)) || \
            CHECK_FOR_TIMEOUT((_pkt)->rDequeueTimestamp, (_pkt)->rEnqueueTimestamp, (_delta)) || \
            CHECK_FOR_TIMEOUT((_pkt)->rHifTxDoneTimestamp, (_pkt)->rDequeueTimestamp, (_delta))) 
#endif

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
VOID
nicTxInitialize (
    IN P_ADAPTER_T  prAdapter
    );

WLAN_STATUS
nicTxAcquireResource (
    IN P_ADAPTER_T  prAdapter,
    IN UINT_8       ucTC
    );

WLAN_STATUS
nicTxPollingResource (
    IN P_ADAPTER_T  prAdapter,
    IN UINT_8       ucTC
    );

BOOLEAN
nicTxReleaseResource (
    IN P_ADAPTER_T  prAdapter,
    IN UINT_8*      aucTxRlsCnt
    );

WLAN_STATUS
nicTxResetResource (
    IN P_ADAPTER_T  prAdapter
    );

UINT_8
nicTxGetResource (
    IN P_ADAPTER_T  prAdapter,
    IN UINT_8       ucTC
    );

WLAN_STATUS
nicTxMsduInfoList (
    IN P_ADAPTER_T      prAdapter,
    IN P_MSDU_INFO_T    prMsduInfoListHead
    );

WLAN_STATUS
nicTxMsduQueue (
    IN P_ADAPTER_T  prAdapter,
    UINT_8          ucPortIdx,
    P_QUE_T         prQue
    );

WLAN_STATUS
nicTxCmd (
    IN P_ADAPTER_T      prAdapter,
    IN P_CMD_INFO_T     prCmdInfo,
    IN UINT_8           ucTC
    );

VOID
nicTxRelease (
    IN P_ADAPTER_T  prAdapter
    );

VOID
nicProcessTxInterrupt (
    IN P_ADAPTER_T  prAdapter
    );

VOID
nicTxFreeMsduInfoPacket (
    IN P_ADAPTER_T    prAdapter,
    IN P_MSDU_INFO_T  prMsduInfoListHead
    );

VOID
nicTxReturnMsduInfo (
    IN P_ADAPTER_T    prAdapter,
    IN P_MSDU_INFO_T  prMsduInfoListHead
    );

BOOLEAN
nicTxFillMsduInfo (
    IN P_ADAPTER_T    prAdapter,
    IN P_MSDU_INFO_T  prMsduInfo,
    IN P_NATIVE_PACKET   prNdisPacket
    );

WLAN_STATUS
nicTxAdjustTcq (
    IN P_ADAPTER_T  prAdapter
    );

WLAN_STATUS
nicTxFlush (
    IN P_ADAPTER_T  prAdapter
    );

#if CFG_ENABLE_FW_DOWNLOAD
WLAN_STATUS
nicTxInitCmd (
    IN P_ADAPTER_T      prAdapter,
    IN P_CMD_INFO_T     prCmdInfo,
    IN UINT_8           ucTC
    );

WLAN_STATUS
nicTxInitResetResource (
    IN P_ADAPTER_T  prAdapter
    );
#endif

WLAN_STATUS
nicTxEnqueueMsdu (
    IN P_ADAPTER_T      prAdapter,
    IN P_MSDU_INFO_T    prMsduInfo
    );

UINT_32
nicTxGetFreeCmdCount (
    IN P_ADAPTER_T  prAdapter
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _NIC_TX_H */


