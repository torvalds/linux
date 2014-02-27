/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/include/nic/nic_rx.h#1 $
*/

/*! \file   "nic_rx.h"
    \brief  The declaration of the nic rx functions

*/

/*******************************************************************************
* Copyright (c) 2007 MediaTek Inc.
*
* All rights reserved. Copying, compilation, modification, distribution
* or any other use whatsoever of this material is strictly prohibited
* except in accordance with a Software License Agreement with
* MediaTek Inc.
********************************************************************************
*/

/*******************************************************************************
* LEGAL DISCLAIMER
*
* BY OPENING THIS FILE, BUYER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND
* AGREES THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK
* SOFTWARE") RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE
* PROVIDED TO BUYER ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY
* DISCLAIMS ANY AND ALL WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT
* LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
* PARTICULAR PURPOSE OR NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE
* ANY WARRANTY WHATSOEVER WITH RESPECT TO THE SOFTWARE OF ANY THIRD PARTY
* WHICH MAY BE USED BY, INCORPORATED IN, OR SUPPLIED WITH THE MEDIATEK
* SOFTWARE, AND BUYER AGREES TO LOOK ONLY TO SUCH THIRD PARTY FOR ANY
* WARRANTY CLAIM RELATING THERETO. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE
* FOR ANY MEDIATEK SOFTWARE RELEASES MADE TO BUYER'S SPECIFICATION OR TO
* CONFORM TO A PARTICULAR STANDARD OR OPEN FORUM.
*
* BUYER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND CUMULATIVE
* LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL
* BE, AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT
* ISSUE, OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY
* BUYER TO MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
*
* THE TRANSACTION CONTEMPLATED HEREUNDER SHALL BE CONSTRUED IN ACCORDANCE
* WITH THE LAWS OF THE STATE OF CALIFORNIA, USA, EXCLUDING ITS CONFLICT
* OF LAWS PRINCIPLES.  ANY DISPUTES, CONTROVERSIES OR CLAIMS ARISING
* THEREOF AND RELATED THERETO SHALL BE SETTLED BY ARBITRATION IN SAN
* FRANCISCO, CA, UNDER THE RULES OF THE INTERNATIONAL CHAMBER OF COMMERCE
* (ICC).
********************************************************************************
*/

/*
** $Log: nic_rx.h $
 *
 * 11 07 2011 tsaiyuan.hsu
 * [WCXRP00001083] [MT6620 Wi-Fi][DRV]] dump debug counter or frames when debugging is triggered
 * add debug counters and periodically dump counters for debugging.
 *
 * 05 05 2011 cp.wu
 * [WCXRP00000702] [MT5931][Driver] Modify initialization sequence for E1 ASIC
 * add delay after whole-chip resetting for MT5931 E1 ASIC.
 *
 * 04 18 2011 terry.wu
 * [WCXRP00000660] [MT6620 Wi-Fi][Driver] Remove flag CFG_WIFI_DIRECT_MOVED
 * Remove flag CFG_WIFI_DIRECT_MOVED.
 *
 * 01 24 2011 cm.chang
 * [WCXRP00000384] [MT6620 Wi-Fi][Driver][FW] Handle 20/40 action frame in AP mode and stop ampdu timer when sta_rec is freed
 * Process received 20/40 coexistence action frame for AP mode
 *
 * 09 08 2010 cp.wu
 * NULL
 * use static memory pool for storing IEs of scanning result.
 *
 * 09 07 2010 yuche.tsai
 * NULL
 * Change prototype of API of adding P2P device to scan result.
 * Additional IE buffer is saved.
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 08 05 2010 yuche.tsai
 * NULL
 * Modify data structure for P2P Scan result.
 *
 * 08 03 2010 cp.wu
 * NULL
 * newly added P2P API should be declared in header file.
 *
 * 07 30 2010 cp.wu
 * NULL
 * 1) BoW wrapper: use definitions instead of hard-coded constant for error code
 * 2) AIS-FSM: eliminate use of desired RF parameters, use prTargetBssDesc instead
 * 3) add handling for RX_PKT_DESTINATION_HOST_WITH_FORWARD for GO-broadcast frames
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 14 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * saa_fsm.c is migrated.
 *
 * 06 14 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add management dispatching function table.
 *
 * 06 11 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) migrate assoc.c.
 * 2) add ucTxSeqNum for tracking frames which needs TX-DONE awareness
 * 3) add configuration options for CNM_MEM and RSN modules
 * 4) add data path for management frames
 * 5) eliminate rPacketInfo of MSDU_INFO_T
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
 *  *
 *
 * 03 11 2010 cp.wu
 * [WPD00003821][BUG] Host driver stops processing RX packets from HIF RX0
 * add RX starvation warning debug message controlled by CFG_HIF_RX_STARVATION_WARNING
 *
 * 03 10 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * code clean: removing unused variables and structure definitions
 *
 * 02 25 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * correct behavior to prevent duplicated RX handling for RX0_DONE and RX1_DONE
 *
 * 02 10 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * implement host-side firmware download logic
 *
 * 02 10 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) remove unused function in nic_rx.c [which has been handled in que_mgt.c]
 *  * 2) firmware image length is now retrieved via NdisFileOpen
 *  * 3) firmware image is not structured by (P_IMG_SEC_HDR_T) anymore
 *  * 4) nicRxWaitResponse() revised
 *  * 5) another set of TQ counter default value is added for fw-download state
 *  * 6) Wi-Fi load address is now retrieved from registry too
 *
 * 12 30 2009 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) According to CMD/EVENT documentation v0.8,
 *  *  *  * OID_CUSTOM_TEST_RX_STATUS & OID_CUSTOM_TEST_TX_STATUS is no longer used,
 *  *  *  * and result is retrieved by get ATInfo instead
 *  *  *  * 2) add 4 counter for recording aggregation statistics
**  \main\maintrunk.MT6620WiFiDriver_Prj\24 2009-12-10 16:49:09 GMT mtk02752
**  code clean
**  \main\maintrunk.MT6620WiFiDriver_Prj\23 2009-12-09 14:02:37 GMT MTK02468
**  Added ucStaRecIdx in SW_RFB_T and HALF_SEQ_NO_COUNT definition (to replace HALF_SEQ_NO_CNOUT)
**  \main\maintrunk.MT6620WiFiDriver_Prj\22 2009-11-27 11:07:54 GMT mtk02752
**  add flush for reset
**  \main\maintrunk.MT6620WiFiDriver_Prj\21 2009-11-25 18:18:09 GMT mtk02752
**  modify nicRxAddScanResult()
**  \main\maintrunk.MT6620WiFiDriver_Prj\20 2009-11-24 22:42:22 GMT mtk02752
**  add nicRxAddScanResult() to prepare to handle SCAN_RESULT event
**  \main\maintrunk.MT6620WiFiDriver_Prj\19 2009-11-24 19:57:06 GMT mtk02752
**  adopt P_HIF_RX_HEADER_T
**  \main\maintrunk.MT6620WiFiDriver_Prj\18 2009-11-16 21:43:04 GMT mtk02752
**  correct ENUM_RX_PKT_DESTINATION_T definitions
**  \main\maintrunk.MT6620WiFiDriver_Prj\17 2009-11-16 15:28:25 GMT mtk02752
**  add ucQueuedPacketNum for indicating how many packet are queued by RX reordering buffer/forwarding path
**  \main\maintrunk.MT6620WiFiDriver_Prj\16 2009-11-16 15:05:01 GMT mtk02752
**  add eTC for SW_RFB_T and structure RX_MAILBOX
**  \main\maintrunk.MT6620WiFiDriver_Prj\15 2009-11-13 21:16:57 GMT mtk02752
**  \main\maintrunk.MT6620WiFiDriver_Prj\14 2009-11-13 16:59:30 GMT mtk02752
**  add handler for event packet
**  \main\maintrunk.MT6620WiFiDriver_Prj\13 2009-11-13 13:45:50 GMT mtk02752
**  add port param for nicRxEnhanceReadBuffer()
**  \main\maintrunk.MT6620WiFiDriver_Prj\12 2009-11-11 10:12:31 GMT mtk02752
**  nicSDIOReadIntStatus() always read sizeof(ENHANCE_MODE_DATA_STRUCT_T) for int response, thus the number should be set to 0(:=16) instead of 10
**  \main\maintrunk.MT6620WiFiDriver_Prj\11 2009-10-29 19:53:32 GMT mtk01084
**  modify structure naming
**  \main\maintrunk.MT6620WiFiDriver_Prj\10 2009-10-23 16:08:23 GMT mtk01084
**  \main\maintrunk.MT6620WiFiDriver_Prj\9 2009-10-13 21:59:01 GMT mtk01084
**  update for new HW architecture design
**  \main\maintrunk.MT6620WiFiDriver_Prj\8 2009-05-20 12:23:33 GMT mtk01461
**  Add u4MaxEventBufferLen parameter to nicRxWaitResponse()
**  \main\maintrunk.MT6620WiFiDriver_Prj\7 2009-05-18 21:00:48 GMT mtk01426
**  Update SDIO_MAXIMUM_RX_STATUS value
**  \main\maintrunk.MT6620WiFiDriver_Prj\6 2009-04-28 10:36:15 GMT mtk01461
**  Remove unused define - SDIO_MAXIMUM_TX_STATUS
**  \main\maintrunk.MT6620WiFiDriver_Prj\5 2009-04-01 10:53:17 GMT mtk01461
**  Add function for HIF_LOOPBACK_PRE_TEST
**  \main\maintrunk.MT6620WiFiDriver_Prj\4 2009-03-18 20:56:19 GMT mtk01426
**  Add to support CFG_HIF_LOOPBACK and CFG_SDIO_RX_ENHANCE
**  \main\maintrunk.MT6620WiFiDriver_Prj\3 2009-03-17 20:19:56 GMT mtk01426
**  Add nicRxWaitResponse function proto type
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-03-10 20:16:35 GMT mtk01426
**  Init for develop
**
*/

#ifndef _NIC_RX_H
#define _NIC_RX_H

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
#define MAX_SEQ_NO                  4095
#define MAX_SEQ_NO_COUNT            4096
#define HALF_SEQ_NO_CNOUT           2048

#define HALF_SEQ_NO_COUNT           2048

#define MT6620_FIXED_WIN_SIZE         64
#define CFG_RX_MAX_BA_ENTRY            4
#define CFG_RX_MAX_BA_TID_NUM          8

#define RX_STATUS_FLAG_MORE_PACKET    BIT(30)
#define RX_STATUS_CHKSUM_MASK         BITS(0,10)

#define RX_RFB_LEN_FIELD_LEN        4
#define RX_HEADER_OFFSET            2


#if defined(_HIF_SDIO) && defined (WINDOWS_DDK)
/*! On XP, maximum Tx+Rx Statue <= 64-4(HISR)*/
    #define SDIO_MAXIMUM_RX_LEN_NUM              0 /*!< 0~15 (0: un-limited) */
#else
    #define SDIO_MAXIMUM_RX_LEN_NUM              0 /*!< 0~15 (0: un-limited) */
#endif

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
typedef enum _ENUM_RX_STATISTIC_COUNTER_T {
    RX_MPDU_TOTAL_COUNT = 0,
    RX_SIZE_ERR_DROP_COUNT,

    RX_DATA_INDICATION_COUNT,
    RX_DATA_RETURNED_COUNT,
    RX_DATA_RETAINED_COUNT,

    RX_DROP_TOTAL_COUNT,
    RX_TYPE_ERR_DROP_COUNT,
    RX_CLASS_ERR_DROP_COUNT,
    RX_DST_NULL_DROP_COUNT,
    
#if CFG_TCP_IP_CHKSUM_OFFLOAD || CFG_TCP_IP_CHKSUM_OFFLOAD_NDIS_60
    RX_CSUM_TCP_FAILED_COUNT,
    RX_CSUM_UDP_FAILED_COUNT,
    RX_CSUM_IP_FAILED_COUNT,
    RX_CSUM_TCP_SUCCESS_COUNT,
    RX_CSUM_UDP_SUCCESS_COUNT,
    RX_CSUM_IP_SUCCESS_COUNT,
    RX_CSUM_UNKNOWN_L4_PKT_COUNT,
    RX_CSUM_UNKNOWN_L3_PKT_COUNT,
    RX_IP_V6_PKT_CCOUNT,
#endif
    RX_STATISTIC_COUNTER_NUM
} ENUM_RX_STATISTIC_COUNTER_T;

typedef enum _ENUM_RX_PKT_DESTINATION_T {
    RX_PKT_DESTINATION_HOST,                    /* to OS */
    RX_PKT_DESTINATION_FORWARD,                 /* to TX queue for forward, AP mode */
    RX_PKT_DESTINATION_HOST_WITH_FORWARD,       /* to both TX and OS, AP mode broadcast packet */
    RX_PKT_DESTINATION_NULL,                    /* packet to be freed */
    RX_PKT_DESTINATION_NUM
} ENUM_RX_PKT_DESTINATION_T;

struct _SW_RFB_T {
    QUE_ENTRY_T             rQueEntry;
    PVOID                   pvPacket;      /*!< ptr to rx Packet Descriptor */
    PUINT_8                 pucRecvBuff;   /*!< ptr to receive data buffer */
    P_HIF_RX_HEADER_T       prHifRxHdr;
    UINT_32                 u4HifRxHdrFlag;
    PVOID                   pvHeader;
    UINT_16                 u2PacketLen;
    UINT_16                 u2HeaderLen;
    UINT_16                 u2SSN;
    UINT_8                  ucTid;
    UINT_8                  ucWlanIdx;
    UINT_8                  ucPacketType;
    UINT_8                  ucStaRecIdx;

    ENUM_CSUM_RESULT_T      aeCSUM[CSUM_TYPE_NUM];
    ENUM_RX_PKT_DESTINATION_T   eDst;
    ENUM_TRAFFIC_CLASS_INDEX_T  eTC;        /* only valid when eDst == FORWARD */
};

/*! RX configuration type structure */
typedef struct _RX_CTRL_T {
    UINT_32                 u4RxCachedSize;
    PUINT_8                 pucRxCached;
    QUE_T                   rFreeSwRfbList;
    QUE_T                   rReceivedRfbList;
    QUE_T                   rIndicatedRfbList;

#if CFG_SDIO_RX_AGG
    PUINT_8                 pucRxCoalescingBufPtr;
#endif

    PVOID                   apvIndPacket[CFG_RX_MAX_PKT_NUM];
    PVOID                   apvRetainedPacket[CFG_RX_MAX_PKT_NUM];

    UINT_8                  ucNumIndPacket;
    UINT_8                  ucNumRetainedPacket;
    UINT_64                 au8Statistics[RX_STATISTIC_COUNTER_NUM]; /*!< RX Counters */

#if CFG_HIF_STATISTICS
    UINT_32                 u4TotalRxAccessNum;
    UINT_32                 u4TotalRxPacketNum;
#endif

#if CFG_HIF_RX_STARVATION_WARNING
    UINT_32                 u4QueuedCnt;
    UINT_32                 u4DequeuedCnt;
#endif

#if CFG_RX_PKTS_DUMP
    UINT_32                 u4RxPktsDumpTypeMask;
#endif

} RX_CTRL_T, *P_RX_CTRL_T;

typedef struct _RX_MAILBOX_T {
    UINT_32                 u4RxMailbox[2]; /* for Device-to-Host Mailbox */
} RX_MAILBOX_T, *P_RX_MAILBOX_T;

typedef WLAN_STATUS (*PROCESS_RX_MGT_FUNCTION)(P_ADAPTER_T, P_SW_RFB_T);

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
#define RX_INC_CNT(prRxCtrl, eCounter)              \
    {((P_RX_CTRL_T)prRxCtrl)->au8Statistics[eCounter]++;}

#define RX_ADD_CNT(prRxCtrl, eCounter, u8Amount)    \
    {((P_RX_CTRL_T)prRxCtrl)->au8Statistics[eCounter] += (UINT_64)u8Amount;}

#define RX_GET_CNT(prRxCtrl, eCounter)              \
    (((P_RX_CTRL_T)prRxCtrl)->au8Statistics[eCounter])

#define RX_RESET_ALL_CNTS(prRxCtrl)                 \
    {kalMemZero(&prRxCtrl->au8Statistics[0], sizeof(prRxCtrl->au8Statistics));}

#define RX_STATUS_TEST_MORE_FLAG(flag) \
    ((BOOL)((flag & RX_STATUS_FLAG_MORE_PACKET) ? TRUE : FALSE))

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

VOID
nicRxInitialize (
    IN P_ADAPTER_T prAdapter
    );

#if defined(MT5931)
VOID
nicRxPostInitialize (
    IN P_ADAPTER_T prAdapter
    );
#endif

VOID
nicRxUninitialize (
    IN P_ADAPTER_T prAdapter
    );

VOID
nicRxProcessRFBs (
    IN  P_ADAPTER_T prAdapter
    );

#if !CFG_SDIO_INTR_ENHANCE
VOID
nicRxReceiveRFBs (
    IN  P_ADAPTER_T prAdapter
    );

WLAN_STATUS
nicRxReadBuffer (
    IN P_ADAPTER_T prAdapter,
    IN OUT P_SW_RFB_T prSwRfb
    );

#else
VOID
nicRxSDIOReceiveRFBs (
    IN  P_ADAPTER_T prAdapter
    );

WLAN_STATUS
nicRxEnhanceReadBuffer (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4DataPort,
    IN UINT_16 u2RxLength,
    IN OUT P_SW_RFB_T prSwRfb
    );
#endif /* CFG_SDIO_INTR_ENHANCE */


#if CFG_SDIO_RX_AGG
VOID
nicRxSDIOAggReceiveRFBs (
    IN  P_ADAPTER_T prAdapter
    );
#endif

WLAN_STATUS
nicRxSetupRFB (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T  prRfb
    );

VOID
nicRxReturnRFB (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T  prRfb
    );

VOID
nicProcessRxInterrupt (
    IN  P_ADAPTER_T prAdapter
    );

VOID
nicRxProcessPktWithoutReorder (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T  prSwRfb
    );

VOID
nicRxProcessForwardPkt (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T  prSwRfb
    );

VOID
nicRxProcessGOBroadcastPkt (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T  prSwRfb
    );


VOID
nicRxFillRFB (
    IN P_ADAPTER_T    prAdapter,
    IN OUT P_SW_RFB_T prSwRfb
    );

VOID
nicRxProcessDataPacket (
    IN P_ADAPTER_T    prAdapter,
    IN OUT P_SW_RFB_T prSwRfb
    );

VOID
nicRxProcessEventPacket (
    IN P_ADAPTER_T    prAdapter,
    IN OUT P_SW_RFB_T prSwRfb
    );

VOID
nicRxProcessMgmtPacket (
    IN P_ADAPTER_T    prAdapter,
    IN OUT P_SW_RFB_T prSwRfb
    );

#if CFG_TCP_IP_CHKSUM_OFFLOAD
VOID
nicRxFillChksumStatus(
    IN  P_ADAPTER_T   prAdapter,
    IN OUT P_SW_RFB_T prSwRfb,
    IN  UINT_32 u4TcpUdpIpCksStatus
    );

VOID
nicRxUpdateCSUMStatistics (
    IN P_ADAPTER_T prAdapter,
    IN const ENUM_CSUM_RESULT_T aeCSUM[]
    );
#endif /* CFG_TCP_IP_CHKSUM_OFFLOAD */


VOID
nicRxQueryStatus (
    IN P_ADAPTER_T prAdapter,
    IN PUINT_8 pucBuffer,
    OUT PUINT_32 pu4Count
    );

VOID
nicRxClearStatistics (
    IN P_ADAPTER_T prAdapter
    );

VOID
nicRxQueryStatistics (
    IN P_ADAPTER_T prAdapter,
    IN PUINT_8 pucBuffer,
    OUT PUINT_32 pu4Count
    );

WLAN_STATUS
nicRxWaitResponse (
    IN P_ADAPTER_T  prAdapter,
    IN UINT_8       ucPortIdx,
    OUT PUINT_8     pucRspBuffer,
    IN UINT_32      u4MaxRespBufferLen,
    OUT PUINT_32    pu4Length
    );

VOID
nicRxEnablePromiscuousMode (
    IN P_ADAPTER_T prAdapter
    );


VOID
nicRxDisablePromiscuousMode (
    IN P_ADAPTER_T prAdapter
    );


WLAN_STATUS
nicRxFlush (
    IN P_ADAPTER_T  prAdapter
    );

WLAN_STATUS
nicRxProcessActionFrame (
    IN P_ADAPTER_T  prAdapter,
    IN P_SW_RFB_T   prSwRfb
    );

#endif /* _NIC_RX_H */

