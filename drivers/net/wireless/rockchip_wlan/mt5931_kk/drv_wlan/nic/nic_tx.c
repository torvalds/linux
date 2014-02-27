/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/nic/nic_tx.c#2 $
*/

/*! \file   nic_tx.c
    \brief  Functions that provide TX operation in NIC Layer.

    This file provides TX functions which are responsible for both Hardware and
    Software Resource Management and keep their Synchronization.
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
** $Log: nic_tx.c $
**
** 12 27 2012 cp.wu
** no need to do TX checksum offloading for forwarding packets
** 
** 08 28 2012 cp.wu
** [WCXRP00001270] [MT6620 Wi-Fi][Driver] Fix non-aggregated TX path for experimental purpose
** fix: pucTxCoalescingBufPtr is also used by non-aggregated TX path
 *
 * 06 13 2012 yuche.tsai
 * NULL
 * Update maintrunk driver.
 * Add support for driver compose assoc request frame.
 *
 * 11 18 2011 eddie.chen
 * [WCXRP00001096] [MT6620 Wi-Fi][Driver/FW] Enhance the log function (xlog)
 * Add log counter for tx
 *
 * 11 09 2011 eddie.chen
 * [WCXRP00001096] [MT6620 Wi-Fi][Driver/FW] Enhance the log function (xlog)
 * Add xlog for beacon timeout and sta aging timeout.
 *
 * 11 08 2011 eddie.chen
 * [WCXRP00001096] [MT6620 Wi-Fi][Driver/FW] Enhance the log function (xlog)
 * Add xlog function.
 *
 * 05 17 2011 cp.wu
 * [WCXRP00000732] [MT6620 Wi-Fi][AIS] No need to switch back to IDLE state when DEAUTH frame is dropped due to bss disconnection
 * when TX DONE status is TX_RESULT_DROPPED_IN_DRIVER, no need to switch back to IDLE state.
 *
 * 04 12 2011 eddie.chen
 * [WCXRP00000617] [MT6620 Wi-Fi][DRV/FW] Fix for sigma
 * Fix the sta index in processing security frame
 * Simple flow control for TC4 to avoid mgt frames for PS STA to occupy the TC4
 * Add debug message.
 *
 * 04 12 2011 cp.wu
 * [WCXRP00000631] [MT6620 Wi-Fi][Driver] Add an API for QM to retrieve current TC counter value and processing frame dropping cases for TC4 path
 * remove unused variables.
 *
 * 04 12 2011 cp.wu
 * [WCXRP00000631] [MT6620 Wi-Fi][Driver] Add an API for QM to retrieve current TC counter value and processing frame dropping cases for TC4 path
 * 1. add nicTxGetResource() API for QM to make decisions.
 * 2. if management frames is decided by QM for dropping, the call back is invoked to indicate such a case.
 *
 * 03 17 2011 cp.wu
 * [WCXRP00000562] [MT6620 Wi-Fi][Driver] I/O buffer pre-allocation to avoid physically continuous memory shortage after system running for a long period
 * use pre-allocated buffer for storing enhanced interrupt response as well
 *
 * 03 15 2011 cp.wu
 * [WCXRP00000559] [MT6620 Wi-Fi][Driver] Combine TX/RX DMA buffers into a single one to reduce physically continuous memory consumption
 * 1. deprecate CFG_HANDLE_IST_IN_SDIO_CALLBACK
 * 2. Use common coalescing buffer for both TX/RX directions
 * 
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
 * 01 12 2011 cp.wu
 * [WCXRP00000356] [MT6620 Wi-Fi][Driver] fill mac header length for security frames 'cause hardware header translation needs such information
 * fill mac header length information for 802.1x frames.
 *
 * 12 31 2010 cp.wu
 * [WCXRP00000335] [MT6620 Wi-Fi][Driver] change to use milliseconds sleep instead of delay to avoid blocking to system scheduling
 * change to use msleep() and shorten waiting interval to reduce blocking to other task while Wi-Fi driver is being loaded
 *
 * 11 01 2010 yarco.yang
 * [WCXRP00000149] [MT6620 WI-Fi][Driver]Fine tune performance on MT6516 platform
 * Add GPIO debug function
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
 * 09 29 2010 wh.su
 * [WCXRP00000072] [MT6620 Wi-Fi][Driver] Fix TKIP Counter Measure EAPoL callback register issue
 * [MT6620 Wi-Fi][Driver] Fix TKIP Counter Measure EAPoL callback register issue.
 *
 * 09 27 2010 wh.su
 * NULL
 * since the u2TxByteCount_UserPriority will or another setting, keep the overall buffer for avoid error
 *
 * 09 24 2010 wh.su
 * NULL
 * [WCXRP000000058][MT6620 Wi-Fi][Driver] Fail to handshake with WAPI AP due the 802.1x frame send to fw with extra bytes padding.
 *
 * 09 01 2010 cp.wu
 * NULL
 * HIFSYS Clock Source Workaround
 *
 * 08 30 2010 cp.wu
 * NULL
 * API added: nicTxPendingPackets(), for simplifying porting layer
 *
 * 08 30 2010 cp.wu
 * NULL
 * eliminate klockwork errors
 *
 * 08 20 2010 wh.su
 * NULL
 * adding the eapol callback setting.
 *
 * 08 18 2010 yarco.yang
 * NULL
 * 1. Fixed HW checksum offload function not work under Linux issue.
 * 2. Add debug message.
 *
 * 08 05 2010 yuche.tsai
 * NULL
 * .
 *
 * 08 03 2010 cp.wu
 * NULL
 * surpress compilation warning.
 *
 * 08 02 2010 jeffrey.chang
 * NULL
 * 1) modify tx service thread to avoid busy looping
 * 2) add spin lock declartion for linux build
 *
 * 07 29 2010 cp.wu
 * NULL
 * simplify post-handling after TX_DONE interrupt is handled.
 *
 * 07 19 2010 jeffrey.chang
 *
 * Linux port modification
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
 * 06 29 2010 yarco.yang
 * [WPD00003837][MT6620]Data Path Refine
 * replace g_rQM with Adpater->rQM
 *
 * 06 25 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add API in que_mgt to retrieve sta-rec index for security frames.
 *
 * 06 24 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 802.1x and bluetooth-over-Wi-Fi security frames are now delievered to firmware via command path instead of data path.
 *
 * 06 23 2010 yarco.yang
 * [WPD00003837][MT6620]Data Path Refine
 * Merge g_arStaRec[] into adapter->arStaRec[]
 *
 * 06 22 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) add command warpper for STA-REC/BSS-INFO sync.
 * 2) enhance command packet sending procedure for non-oid part
 * 3) add command packet definitions for STA-REC/BSS-INFO sync.
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add checking for TX descriptor poll.
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * TX descriptors are now allocated once for reducing allocation overhead
 *
 * 06 18 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Provide cnmMgtPktAlloc() and alloc/free function of msg/buf
 *
 * 06 15 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * change zero-padding for TX port access to HAL.
 *
 * 06 15 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * .
 *
 * 06 15 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * .
 *
 * 06 14 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * fill extra information for revised HIF_TX_HEADER.
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
 * change to enqueue TX frame infinitely.
 *
 * 06 09 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add TX_PACKET_MGMT to indicate the frame is coming from management modules
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 05 10 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * fill network type field while doing frame identification.
 *
 * 04 23 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * surpress compiler warning
 *
 * 04 06 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * Tag the packet for QoS on Tx path
 *
 * 03 30 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * remove driver-land statistics.
 *
 * 03 29 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * improve none-glue code portability
 *
 * 03 24 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * initial import for Linux port
 *
 * 03 24 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * generate information for OID_GEN_RCV_OK & OID_GEN_XMIT_OK
 *  *  *  *  *
 *
* 03 10 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * code clean: removing unused variables and structure definitions
 *
 * 03 08 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) add another spin-lock to protect MsduInfoList due to it might be accessed by different thread.
 *  *  *  * 2) change own-back acquiring procedure to wait for up to 16.67 seconds
 *
 * 03 02 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add mutex to avoid multiple access to qmTxQueue simultaneously.
 *
 * 02 26 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * avoid refering to NDIS-specific data structure directly from non-glue layer.
 *
 * 02 24 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add Ethernet destination address information in packet info for TX
 *
 * 02 10 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) remove unused function in nic_rx.c [which has been handled in que_mgt.c]
 *  *  *  *  *  * 2) firmware image length is now retrieved via NdisFileOpen
 *  *  *  *  *  * 3) firmware image is not structured by (P_IMG_SEC_HDR_T) anymore
 *  *  *  *  *  * 4) nicRxWaitResponse() revised
 *  *  *  *  *  * 5) another set of TQ counter default value is added for fw-download state
 *  *  *  *  *  * 6) Wi-Fi load address is now retrieved from registry too
 *
 * 02 09 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1. Permanent and current MAC address are now retrieved by CMD/EVENT packets instead of hard-coded address
 *  *  *  *  *  *  *  *  * 2. follow MSDN defined behavior when associates to another AP
 *  *  *  *  *  *  *  *  * 3. for firmware download, packet size could be up to 2048 bytes
 *
 * 02 08 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * prepare for implementing fw download logic
 *
 * 01 27 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1. eliminate improper variable in rHifInfo
 *  *  *  *  *  *  *  *  * 2. block TX/ordinary OID when RF test mode is engaged
 *  *  *  *  *  *  *  *  * 3. wait until firmware finish operation when entering into and leaving from RF test mode
 *  *  *  *  *  *  *  *  * 4. correct some HAL implementation
 *
 * 01 13 2010 tehuang.liu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * Enabled the Burst_End Indication mechanism
 *
 * 01 13 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * TX: fill ucWlanHeaderLength/ucPktFormtId_Flags according to info provided by prMsduInfo
 *
 * 12 30 2009 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) According to CMD/EVENT documentation v0.8,
 *  *  *  *  *  *  *  *  *  * OID_CUSTOM_TEST_RX_STATUS & OID_CUSTOM_TEST_TX_STATUS is no longer used,
 *  *  *  *  *  *  *  *  *  * and result is retrieved by get ATInfo instead
 *  *  *  *  *  *  *  *  *  * 2) add 4 counter for recording aggregation statistics
**  \main\maintrunk.MT6620WiFiDriver_Prj\44 2009-12-10 16:52:15 GMT mtk02752
**  remove unused API
**  \main\maintrunk.MT6620WiFiDriver_Prj\43 2009-12-07 22:44:24 GMT mtk02752
**  correct assertion criterion
**  \main\maintrunk.MT6620WiFiDriver_Prj\42 2009-12-07 21:15:52 GMT mtk02752
**  correct trivial mistake
**  \main\maintrunk.MT6620WiFiDriver_Prj\41 2009-12-04 15:47:21 GMT mtk02752
**  + always append a dword of zero on TX path to avoid TX aggregation to triggered on uninitialized data
**  + add more assertion for packet size check
**  \main\maintrunk.MT6620WiFiDriver_Prj\40 2009-12-04 14:51:55 GMT mtk02752
**  nicTxMsduInfo(): save ptr for next entry before attaching to qDataPort
**  \main\maintrunk.MT6620WiFiDriver_Prj\39 2009-12-04 11:54:54 GMT mtk02752
**  add 2 assertion for size check
**  \main\maintrunk.MT6620WiFiDriver_Prj\38 2009-12-03 16:20:35 GMT mtk01461
**  Add debug message
**  \main\maintrunk.MT6620WiFiDriver_Prj\37 2009-11-30 10:57:10 GMT mtk02752
**  1st DW of WIFI_CMD_T is shared with HIF_TX_HEADER_T
**  \main\maintrunk.MT6620WiFiDriver_Prj\36 2009-11-30 09:20:43 GMT mtk02752
**  use TC4 instead of TC5 for command packet
**  \main\maintrunk.MT6620WiFiDriver_Prj\35 2009-11-27 11:08:11 GMT mtk02752
**  add flush for reset
**  \main\maintrunk.MT6620WiFiDriver_Prj\34 2009-11-26 20:31:22 GMT mtk02752
**  fill prMsduInfo->ucUserPriority
**  \main\maintrunk.MT6620WiFiDriver_Prj\33 2009-11-25 21:04:33 GMT mtk02752
**  fill u2SeqNo
**  \main\maintrunk.MT6620WiFiDriver_Prj\32 2009-11-24 20:52:12 GMT mtk02752
**  integration with SD1's data path API
**  \main\maintrunk.MT6620WiFiDriver_Prj\31 2009-11-24 19:54:25 GMT mtk02752
**  nicTxRetransmitOfOsSendQue & nicTxData but changed to use nicTxMsduInfoList
**  \main\maintrunk.MT6620WiFiDriver_Prj\30 2009-11-23 17:53:18 GMT mtk02752
**  add nicTxCmd() for SD1_SD3_DATAPATH_INTEGRATION, which will append only HIF_TX_HEADER. seqNum, WIFI_CMD_T will be created inside oid handler
**  \main\maintrunk.MT6620WiFiDriver_Prj\29 2009-11-20 15:10:24 GMT mtk02752
**  use TxAccquireResource instead of accessing TCQ directly.
**  \main\maintrunk.MT6620WiFiDriver_Prj\28 2009-11-17 22:40:57 GMT mtk01084
**  \main\maintrunk.MT6620WiFiDriver_Prj\27 2009-11-17 17:35:40 GMT mtk02752
**  add nicTxMsduInfoList () implementation
**  \main\maintrunk.MT6620WiFiDriver_Prj\26 2009-11-17 11:07:10 GMT mtk02752
**  add nicTxAdjustTcq() implementation
**  \main\maintrunk.MT6620WiFiDriver_Prj\25 2009-11-16 22:28:38 GMT mtk02752
**  move aucFreeBufferCount/aucMaxNumOfBuffer into another structure
**  \main\maintrunk.MT6620WiFiDriver_Prj\24 2009-11-16 21:45:32 GMT mtk02752
**  add SD1_SD3_DATAPATH_INTEGRATION data path handling
**  \main\maintrunk.MT6620WiFiDriver_Prj\23 2009-11-13 13:29:56 GMT mtk01084
**  modify TX hdr format, fix tx retransmission issue
**  \main\maintrunk.MT6620WiFiDriver_Prj\22 2009-11-11 10:36:21 GMT mtk01084
**  \main\maintrunk.MT6620WiFiDriver_Prj\21 2009-11-04 14:11:11 GMT mtk01084
**  modify TX SW data structure
**  \main\maintrunk.MT6620WiFiDriver_Prj\20 2009-10-29 19:56:17 GMT mtk01084
**  modify HAL part
**  \main\maintrunk.MT6620WiFiDriver_Prj\19 2009-10-13 21:59:23 GMT mtk01084
**  update for new HW design
**  \main\maintrunk.MT6620WiFiDriver_Prj\18 2009-10-02 14:00:18 GMT mtk01725
**  \main\maintrunk.MT6620WiFiDriver_Prj\17 2009-05-20 12:26:06 GMT mtk01461
**  Assign SeqNum to CMD Packet
**  \main\maintrunk.MT6620WiFiDriver_Prj\16 2009-05-19 10:54:04 GMT mtk01461
**  Add debug message
**  \main\maintrunk.MT6620WiFiDriver_Prj\15 2009-05-12 09:41:55 GMT mtk01461
**  Fix Query Command need resp issue
**  \main\maintrunk.MT6620WiFiDriver_Prj\14 2009-04-29 15:44:38 GMT mtk01461
**  Move OS dependent code to kalQueryTxOOBData()
**  \main\maintrunk.MT6620WiFiDriver_Prj\13 2009-04-28 10:40:03 GMT mtk01461
**  Add nicTxReleaseResource() for SDIO_STATUS_ENHANCE, and also fix the TX aggregation issue for 1x packet to TX1 port
**  \main\maintrunk.MT6620WiFiDriver_Prj\12 2009-04-21 09:50:47 GMT mtk01461
**  Update nicTxCmd() for moving wait RESP function call to wlanSendCommand()
**  \main\maintrunk.MT6620WiFiDriver_Prj\11 2009-04-17 19:56:32 GMT mtk01461
**  Move the CMD_INFO_T related function to cmd_buf.c
**  \main\maintrunk.MT6620WiFiDriver_Prj\10 2009-04-17 18:14:40 GMT mtk01426
**  Update OOB query for TX packet
**  \main\maintrunk.MT6620WiFiDriver_Prj\9 2009-04-14 15:51:32 GMT mtk01426
**  Support PKGUIO
**  \main\maintrunk.MT6620WiFiDriver_Prj\8 2009-04-02 17:26:40 GMT mtk01461
**  Add virtual OOB for HIF LOOPBACK SW PRETEST
**  \main\maintrunk.MT6620WiFiDriver_Prj\7 2009-04-01 10:54:43 GMT mtk01461
**  Add function for SDIO_TX_ENHANCE
**  \main\maintrunk.MT6620WiFiDriver_Prj\6 2009-03-23 21:53:47 GMT mtk01461
**  Add code for retransmit of rOsSendQueue, mpSendPacket(), and add code for TX Checksum offload, Loopback Test.
**  \main\maintrunk.MT6620WiFiDriver_Prj\5 2009-03-23 00:33:51 GMT mtk01461
**  Add code for TX Data & Cmd Packet
**  \main\maintrunk.MT6620WiFiDriver_Prj\4 2009-03-18 20:25:40 GMT mtk01461
**  Fix LINT warning
**  \main\maintrunk.MT6620WiFiDriver_Prj\3 2009-03-16 09:10:30 GMT mtk01461
**  Update TX PATH API
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-03-10 20:26:04 GMT mtk01426
**  Init for develop
**
*/

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "precomp.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

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

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
/*----------------------------------------------------------------------------*/
/*!
* @brief This function will initial all variables in regard to SW TX Queues and
*        all free lists of MSDU_INFO_T and SW_TFCB_T.
*
* @param prAdapter  Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicTxInitialize (
    IN P_ADAPTER_T  prAdapter
    )
{
    P_TX_CTRL_T prTxCtrl;
    PUINT_8 pucMemHandle;
    P_MSDU_INFO_T prMsduInfo;
    UINT_32 i;
    KAL_SPIN_LOCK_DECLARATION();

    DEBUGFUNC("nicTxInitialize");

    ASSERT(prAdapter);
    prTxCtrl = &prAdapter->rTxCtrl;

    //4 <1> Initialization of Traffic Class Queue Parameters
    nicTxResetResource(prAdapter);

#if CFG_SDIO_TX_AGG
    prTxCtrl->pucTxCoalescingBufPtr = prAdapter->pucCoalescingBufCached;
#endif /* CFG_SDIO_TX_AGG */

    // allocate MSDU_INFO_T and link it into rFreeMsduInfoList
    QUEUE_INITIALIZE(&prTxCtrl->rFreeMsduInfoList);

    pucMemHandle = prTxCtrl->pucTxCached;
    for (i = 0 ; i < CFG_TX_MAX_PKT_NUM ; i++) {
        prMsduInfo = (P_MSDU_INFO_T)pucMemHandle;
        kalMemZero(prMsduInfo, sizeof(MSDU_INFO_T));

        KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_MSDU_INFO_LIST);
        QUEUE_INSERT_TAIL(&prTxCtrl->rFreeMsduInfoList, (P_QUE_ENTRY_T)prMsduInfo);
        KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_MSDU_INFO_LIST);

        pucMemHandle += ALIGN_4(sizeof(MSDU_INFO_T));
    }

    ASSERT(prTxCtrl->rFreeMsduInfoList.u4NumElem == CFG_TX_MAX_PKT_NUM);
    /* Check if the memory allocation consist with this initialization function */
    ASSERT((UINT_32)(pucMemHandle - prTxCtrl->pucTxCached) == prTxCtrl->u4TxCachedSize);

    QUEUE_INITIALIZE(&prTxCtrl->rTxMgmtTxingQueue);
    prTxCtrl->i4TxMgmtPendingNum = 0;

#if CFG_HIF_STATISTICS
    prTxCtrl->u4TotalTxAccessNum = 0;
    prTxCtrl->u4TotalTxPacketNum = 0;
#endif

    prTxCtrl->i4PendingFwdFrameCount = 0;

    qmInit(prAdapter);

    TX_RESET_ALL_CNTS(prTxCtrl);

    return;
} /* end of nicTxInitialize() */


/*----------------------------------------------------------------------------*/
/*!
* \brief Driver maintain a variable that is synchronous with the usage of individual
*        TC Buffer Count. This function will check if has enough TC Buffer for incoming
*        packet and then update the value after promise to provide the resources.
*
* \param[in] prAdapter              Pointer to the Adapter structure.
* \param[in] ucTC                   Specify the resource of TC
*
* \retval WLAN_STATUS_SUCCESS   Resource is available and been assigned.
* \retval WLAN_STATUS_RESOURCES Resource is not available.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicTxAcquireResource (
    IN P_ADAPTER_T  prAdapter,
    IN UINT_8       ucTC
    )
{
    P_TX_CTRL_T prTxCtrl;
    WLAN_STATUS u4Status = WLAN_STATUS_RESOURCES;
    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);
    prTxCtrl = &prAdapter->rTxCtrl;


    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_RESOURCE);

//    DbgPrint("nicTxAcquireResource prTxCtrl->rTc.aucFreeBufferCount[%d]=%d\n", ucTC, prTxCtrl->rTc.aucFreeBufferCount[ucTC]);

    if (prTxCtrl->rTc.aucFreeBufferCount[ucTC]) {

        prTxCtrl->rTc.aucFreeBufferCount[ucTC]--;

        DBGLOG(TX, EVENT, ("Acquire: TC = %d aucFreeBufferCount = %d\n",
            ucTC, prTxCtrl->rTc.aucFreeBufferCount[ucTC]));

        u4Status = WLAN_STATUS_SUCCESS;
    }
    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_RESOURCE);

    return u4Status;

}/* end of nicTxAcquireResourceAndTFCBs() */


/*----------------------------------------------------------------------------*/
/*!
* @brief Driver maintain a variable that is synchronous with the usage of individual
*        TC Buffer Count. This function will do polling if FW has return the resource.
*        Used when driver start up before enable interrupt.
*
* @param prAdapter      Pointer to the Adapter structure.
* @param ucTC           Specify the resource of TC
*
* @retval WLAN_STATUS_SUCCESS   Resource is available.
* @retval WLAN_STATUS_FAILURE   Resource is not available.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicTxPollingResource (
    IN P_ADAPTER_T  prAdapter,
    IN UINT_8       ucTC
    )
{
    P_TX_CTRL_T prTxCtrl;
    WLAN_STATUS u4Status = WLAN_STATUS_FAILURE;
    INT_32 i = NIC_TX_RESOURCE_POLLING_TIMEOUT;
    UINT_32 au4WTSR[2];

    ASSERT(prAdapter);
    prTxCtrl = &prAdapter->rTxCtrl;

    if (ucTC >= TC_NUM) {
        return WLAN_STATUS_FAILURE;
    }

    if (prTxCtrl->rTc.aucFreeBufferCount[ucTC] > 0) {
        return WLAN_STATUS_SUCCESS;
    }

    while (i-- > 0) {
        HAL_READ_TX_RELEASED_COUNT(prAdapter, au4WTSR);

        if(kalIsCardRemoved(prAdapter->prGlueInfo) == TRUE
                || fgIsBusAccessFailed == TRUE) {
            u4Status = WLAN_STATUS_FAILURE;
            break;
        }
        else if (nicTxReleaseResource(prAdapter, (PUINT_8)au4WTSR)) {
            if (prTxCtrl->rTc.aucFreeBufferCount[ucTC] > 0) {
                u4Status = WLAN_STATUS_SUCCESS;
                break;
            }
            else {
                kalMsleep(NIC_TX_RESOURCE_POLLING_DELAY_MSEC);
            }
        }
        else {
            kalMsleep(NIC_TX_RESOURCE_POLLING_DELAY_MSEC);
        }
    }

#if DBG
    {
        INT_32 i4Times = NIC_TX_RESOURCE_POLLING_TIMEOUT - (i+1);

        if (i4Times) {
            DBGLOG(TX, TRACE, ("Polling MCR_WTSR delay %d times, %d msec\n",
                i4Times, (i4Times * NIC_TX_RESOURCE_POLLING_DELAY_MSEC)));
        }
    }
#endif /* DBG */

    return u4Status;

} /* end of nicTxPollingResource() */


/*----------------------------------------------------------------------------*/
/*!
* \brief Driver maintain a variable that is synchronous with the usage of individual
*        TC Buffer Count. This function will release TC Buffer count according to
*        the given TX_STATUS COUNTER after TX Done.
*
* \param[in] prAdapter              Pointer to the Adapter structure.
* \param[in] u4TxStatusCnt          Value of TX STATUS
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
nicTxReleaseResource (
    IN P_ADAPTER_T  prAdapter,
    IN UINT_8*      aucTxRlsCnt
    )
{
    PUINT_32 pu4Tmp = (PUINT_32)aucTxRlsCnt;
    P_TX_CTRL_T prTxCtrl;
    BOOLEAN bStatus = FALSE;
    UINT_32 i;

    KAL_SPIN_LOCK_DECLARATION();


    ASSERT(prAdapter);
    prTxCtrl = &prAdapter->rTxCtrl;

    if (pu4Tmp[0] | pu4Tmp[1]) {

        KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_RESOURCE);
        for (i = 0; i < TC_NUM; i++) {
            prTxCtrl->rTc.aucFreeBufferCount[i] += aucTxRlsCnt[i];

            if ((i==1) || (i==5)){
                DBGLOG(TX, EVENT, ("Release: i = %d aucFreeBufferCount = %d\n",
                    i, prTxCtrl->rTc.aucFreeBufferCount[i]));
            }
        }
        KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_RESOURCE);
#if 0
        for (i = 0; i < TC_NUM; i++) {
            DBGLOG(INIT, TRACE, ("aucFreeBufferCount[%d]: %d, aucMaxNumOfBuffer[%d]: %d\n",
                i, prTxCtrl->rTc.aucFreeBufferCount[i], i, prTxCtrl->rTc.aucMaxNumOfBuffer[i]));
        }
        DbgPrint("prTxCtrl->rTc.aucFreeBufferCount[0]=%d\n", prTxCtrl->rTc.aucFreeBufferCount[0]);
        DbgPrint("prTxCtrl->rTc.aucFreeBufferCount[1]=%d\n", prTxCtrl->rTc.aucFreeBufferCount[1]);
        DbgPrint("prTxCtrl->rTc.aucFreeBufferCount[2]=%d\n", prTxCtrl->rTc.aucFreeBufferCount[2]);
        DbgPrint("prTxCtrl->rTc.aucFreeBufferCount[3]=%d\n", prTxCtrl->rTc.aucFreeBufferCount[3]);
        DbgPrint("prTxCtrl->rTc.aucFreeBufferCount[4]=%d\n", prTxCtrl->rTc.aucFreeBufferCount[4]);
        DbgPrint("prTxCtrl->rTc.aucFreeBufferCount[5]=%d\n", prTxCtrl->rTc.aucFreeBufferCount[5]);
#endif
        ASSERT(prTxCtrl->rTc.aucFreeBufferCount[TC0_INDEX] <= prTxCtrl->rTc.aucMaxNumOfBuffer[TC0_INDEX]);
        ASSERT(prTxCtrl->rTc.aucFreeBufferCount[TC1_INDEX] <= prTxCtrl->rTc.aucMaxNumOfBuffer[TC1_INDEX]);
        ASSERT(prTxCtrl->rTc.aucFreeBufferCount[TC2_INDEX] <= prTxCtrl->rTc.aucMaxNumOfBuffer[TC2_INDEX]);
        ASSERT(prTxCtrl->rTc.aucFreeBufferCount[TC3_INDEX] <= prTxCtrl->rTc.aucMaxNumOfBuffer[TC3_INDEX]);
        ASSERT(prTxCtrl->rTc.aucFreeBufferCount[TC4_INDEX] <= prTxCtrl->rTc.aucMaxNumOfBuffer[TC4_INDEX]);
        ASSERT(prTxCtrl->rTc.aucFreeBufferCount[TC5_INDEX] <= prTxCtrl->rTc.aucMaxNumOfBuffer[TC5_INDEX]);
        bStatus = TRUE;
    }

    return bStatus;
} /* end of nicTxReleaseResource() */


/*----------------------------------------------------------------------------*/
/*!
* \brief Reset TC Buffer Count to initialized value
*
* \param[in] prAdapter              Pointer to the Adapter structure.
*
* @return WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicTxResetResource (
    IN P_ADAPTER_T  prAdapter
    )
{
    P_TX_CTRL_T prTxCtrl;

    KAL_SPIN_LOCK_DECLARATION();

    DEBUGFUNC("nicTxResetResource");

    ASSERT(prAdapter);
    prTxCtrl = &prAdapter->rTxCtrl;

    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_RESOURCE);

    prTxCtrl->rTc.aucMaxNumOfBuffer[TC0_INDEX] = NIC_TX_BUFF_COUNT_TC0;
    prTxCtrl->rTc.aucFreeBufferCount[TC0_INDEX] = NIC_TX_BUFF_COUNT_TC0;

    prTxCtrl->rTc.aucMaxNumOfBuffer[TC1_INDEX] = NIC_TX_BUFF_COUNT_TC1;
    prTxCtrl->rTc.aucFreeBufferCount[TC1_INDEX] = NIC_TX_BUFF_COUNT_TC1;

    prTxCtrl->rTc.aucMaxNumOfBuffer[TC2_INDEX] = NIC_TX_BUFF_COUNT_TC2;
    prTxCtrl->rTc.aucFreeBufferCount[TC2_INDEX] = NIC_TX_BUFF_COUNT_TC2;

    prTxCtrl->rTc.aucMaxNumOfBuffer[TC3_INDEX] = NIC_TX_BUFF_COUNT_TC3;
    prTxCtrl->rTc.aucFreeBufferCount[TC3_INDEX] = NIC_TX_BUFF_COUNT_TC3;

    prTxCtrl->rTc.aucMaxNumOfBuffer[TC4_INDEX] = NIC_TX_BUFF_COUNT_TC4;
    prTxCtrl->rTc.aucFreeBufferCount[TC4_INDEX] = NIC_TX_BUFF_COUNT_TC4;

    prTxCtrl->rTc.aucMaxNumOfBuffer[TC5_INDEX] = NIC_TX_BUFF_COUNT_TC5;
    prTxCtrl->rTc.aucFreeBufferCount[TC5_INDEX] = NIC_TX_BUFF_COUNT_TC5;

    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_RESOURCE);

    return WLAN_STATUS_SUCCESS;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief Driver maintain a variable that is synchronous with the usage of individual
*        TC Buffer Count. This function will return the value for other component
*        which needs this information for making decisions
*
* @param prAdapter      Pointer to the Adapter structure.
* @param ucTC           Specify the resource of TC
*
* @retval UINT_8        The number of corresponding TC number
*/
/*----------------------------------------------------------------------------*/
UINT_8
nicTxGetResource (
    IN P_ADAPTER_T  prAdapter,
    IN UINT_8       ucTC
    )
{
    P_TX_CTRL_T prTxCtrl;

    ASSERT(prAdapter);
    prTxCtrl = &prAdapter->rTxCtrl;

    ASSERT(prTxCtrl);

    if (ucTC >= TC_NUM) {
        return 0;
    }
    else {
        return prTxCtrl->rTc.aucFreeBufferCount[ucTC];
    }
}


/*----------------------------------------------------------------------------*/
/*!
* @brief In this function, we'll aggregate frame(PACKET_INFO_T)
* corresponding to HIF TX port
*
* @param prAdapter              Pointer to the Adapter structure.
* @param prMsduInfoListHead     a link list of P_MSDU_INFO_T
*
* @retval WLAN_STATUS_SUCCESS   Bus access ok.
* @retval WLAN_STATUS_FAILURE   Bus access fail.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicTxMsduInfoList (
    IN P_ADAPTER_T      prAdapter,
    IN P_MSDU_INFO_T    prMsduInfoListHead
    )
{
    P_MSDU_INFO_T prMsduInfo, prNextMsduInfo;
    QUE_T qDataPort0, qDataPort1;
    WLAN_STATUS status;

    ASSERT(prAdapter);
    ASSERT(prMsduInfoListHead);

    prMsduInfo = prMsduInfoListHead;

    QUEUE_INITIALIZE(&qDataPort0);
    QUEUE_INITIALIZE(&qDataPort1);

    // Separate MSDU_INFO_T lists into 2 categories: for Port#0 & Port#1
    while(prMsduInfo) {
        prNextMsduInfo = (P_MSDU_INFO_T)QUEUE_GET_NEXT_ENTRY((P_QUE_ENTRY_T)prMsduInfo);
#if DBG && 0
        LOG_FUNC("nicTxMsduInfoList Acquire TC %d net %u mac len %u len %u Type %u 1x %u 11 %u\n",
            prMsduInfo->ucTC,
                prMsduInfo->ucNetworkType,
                prMsduInfo->ucMacHeaderLength,
            prMsduInfo->u2FrameLength,
            prMsduInfo->ucPacketType,
            prMsduInfo->fgIs802_1x,
            prMsduInfo->fgIs802_11 );

        LOG_FUNC("Dest Mac: " MACSTR "\n",
                MAC2STR(prMsduInfo->aucEthDestAddr));
#endif

        switch(prMsduInfo->ucTC) {
        case TC0_INDEX:
        case TC1_INDEX:
        case TC2_INDEX:
        case TC3_INDEX:
        case TC5_INDEX: // Broadcast/multicast data packets
            QUEUE_GET_NEXT_ENTRY((P_QUE_ENTRY_T)prMsduInfo) = NULL;
            QUEUE_INSERT_TAIL(&qDataPort0, (P_QUE_ENTRY_T)prMsduInfo);
            status = nicTxAcquireResource(prAdapter, prMsduInfo->ucTC);
            ASSERT(status == WLAN_STATUS_SUCCESS)

            break;

        case TC4_INDEX: // Command or 802.1x packets
            QUEUE_GET_NEXT_ENTRY((P_QUE_ENTRY_T)prMsduInfo) = NULL;
            QUEUE_INSERT_TAIL(&qDataPort1, (P_QUE_ENTRY_T)prMsduInfo);

            status = nicTxAcquireResource(prAdapter, prMsduInfo->ucTC);
            ASSERT(status == WLAN_STATUS_SUCCESS)

            break;

        default:
            ASSERT(0);
            break;
        }

        prMsduInfo = prNextMsduInfo;
    }

    if(qDataPort0.u4NumElem > 0) {
        nicTxMsduQueue(prAdapter, 0, &qDataPort0);
    }

    if(qDataPort1.u4NumElem > 0) {
        nicTxMsduQueue(prAdapter, 1, &qDataPort1);
    }

    return WLAN_STATUS_SUCCESS;
}

#if CFG_ENABLE_PKT_LIFETIME_PROFILE

PKT_PROFILE_T rPrevRoundLastPkt;

VOID
nicTxReturnMsduInfoProfiling (
    IN P_ADAPTER_T    prAdapter,
    IN P_MSDU_INFO_T  prMsduInfoListHead
    )
{
    P_MSDU_INFO_T prMsduInfo = prMsduInfoListHead, prNextMsduInfo;
    
    P_MSDU_INFO_T prFirstProfileMsduInfo = NULL;
    P_MSDU_INFO_T prPrevProfileMsduInfo = NULL;
    P_PKT_PROFILE_T prPrevRoundLastPkt = &rPrevRoundLastPkt;
    P_PKT_PROFILE_T prPktProfile;
    
    BOOLEAN fgGotFirst = FALSE;
    BOOLEAN fgPrintCurPkt = FALSE;
    BOOLEAN fgIsPrevPrinted = FALSE;

    UINT_32 u4MaxDeltaTime = 50; // in ms

    while(prMsduInfo) {
        prNextMsduInfo = (P_MSDU_INFO_T)QUEUE_GET_NEXT_ENTRY((P_QUE_ENTRY_T)prMsduInfo);
        prPktProfile = &prMsduInfo->rPktProfile;
        fgPrintCurPkt = FALSE;
            
        if(prPktProfile->fgIsValid) {
            prPktProfile->rHifTxDoneTimestamp = kalGetTimeTick();

            //4 1. check delta between current round first pkt and prevous round last pkt
            if(!fgGotFirst) {
                prFirstProfileMsduInfo = prMsduInfo;
                fgGotFirst = TRUE;

                if(prPrevRoundLastPkt->fgIsValid) {
                    if(CHK_PROFILES_DELTA(prPktProfile, prPrevRoundLastPkt, u4MaxDeltaTime)) {
                        PRINT_PKT_PROFILE(prPrevRoundLastPkt, "PR");
                        fgPrintCurPkt = TRUE;
                    }
                }
            }

            //4 2. check delta between current pkt and previous pkt
            if(prPrevProfileMsduInfo) {
                if(CHK_PROFILES_DELTA(prPktProfile, &prPrevProfileMsduInfo->rPktProfile, u4MaxDeltaTime)) {
                    PRINT_PKT_PROFILE(&prPrevProfileMsduInfo->rPktProfile, "P");
                    fgPrintCurPkt = TRUE;
                }
            }

            //4 3. check delta of current pkt lifetime
            if(CHK_PROFILE_DELTA(prPktProfile, u4MaxDeltaTime)) {
                fgPrintCurPkt = TRUE;
            }

            /* Print current pkt profile */
            if(fgPrintCurPkt) {
                PRINT_PKT_PROFILE(prPktProfile, "C");
            }

            fgIsPrevPrinted = fgPrintCurPkt;
            prPrevProfileMsduInfo = prMsduInfo;
        }

        prMsduInfo = prNextMsduInfo;
    };

    //4 4. record the lifetime of current round last pkt
    if(prPrevProfileMsduInfo) {
        prPktProfile = &prPrevProfileMsduInfo->rPktProfile;
        prPrevRoundLastPkt->u2IpSn = prPktProfile->u2IpSn;
        prPrevRoundLastPkt->u2RtpSn = prPktProfile->u2RtpSn;
        prPrevRoundLastPkt->rHardXmitArrivalTimestamp = prPktProfile->rHardXmitArrivalTimestamp;
        prPrevRoundLastPkt->rEnqueueTimestamp = prPktProfile->rEnqueueTimestamp;
        prPrevRoundLastPkt->rDequeueTimestamp = prPktProfile->rDequeueTimestamp;
        prPrevRoundLastPkt->rHifTxDoneTimestamp = prPktProfile->rHifTxDoneTimestamp;
        prPrevRoundLastPkt->fgIsPrinted = prPktProfile->fgIsPrinted;
        prPrevRoundLastPkt->fgIsValid = TRUE;
    }
    
    nicTxReturnMsduInfo(prAdapter, prMsduInfoListHead);

    return;
}
#endif

/*----------------------------------------------------------------------------*/
/*!
* @brief In this function, we'll write frame(PACKET_INFO_T) into HIF.
*
* @param prAdapter              Pointer to the Adapter structure.
* @param ucPortIdx              Port Number
* @param prQue                  a link list of P_MSDU_INFO_T
*
* @retval WLAN_STATUS_SUCCESS   Bus access ok.
* @retval WLAN_STATUS_FAILURE   Bus access fail.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicTxMsduQueue (
    IN P_ADAPTER_T  prAdapter,
    UINT_8          ucPortIdx,
    P_QUE_T         prQue
    )
{
    P_MSDU_INFO_T prMsduInfo, prNextMsduInfo;
    HIF_TX_HEADER_T rHwTxHeader;
    P_NATIVE_PACKET prNativePacket;
    UINT_16 u2OverallBufferLength;
    UINT_8 ucEtherTypeOffsetInWord;
    PUINT_8 pucOutputBuf = (PUINT_8)NULL; /* Pointer to Transmit Data Structure Frame */
    UINT_32 u4TxHdrSize;
    UINT_32 u4ValidBufSize;
    UINT_32 u4TotalLength;
    P_TX_CTRL_T prTxCtrl;
    QUE_T rFreeQueue;
#if CFG_TCP_IP_CHKSUM_OFFLOAD
    UINT_8 ucChksumFlag;
#endif

    ASSERT(prAdapter);
    ASSERT(ucPortIdx < 2);
    ASSERT(prQue);

    prTxCtrl = &prAdapter->rTxCtrl;
    u4ValidBufSize = prAdapter->u4CoalescingBufCachedSize;

#if CFG_HIF_STATISTICS
    prTxCtrl->u4TotalTxAccessNum++;
    prTxCtrl->u4TotalTxPacketNum += prQue->u4NumElem;
#endif

    QUEUE_INITIALIZE(&rFreeQueue);

    if(prQue->u4NumElem > 0) {
        prMsduInfo = (P_MSDU_INFO_T)QUEUE_GET_HEAD(prQue);
        pucOutputBuf = prTxCtrl->pucTxCoalescingBufPtr;
        u4TotalLength = 0;

        while(prMsduInfo) {

            kalMemZero(&rHwTxHeader, sizeof(rHwTxHeader));

            prNativePacket = prMsduInfo->prPacket;

            ASSERT(prNativePacket);

            u4TxHdrSize = TX_HDR_SIZE;

            u2OverallBufferLength = ((prMsduInfo->u2FrameLength + TX_HDR_SIZE) &
                    (UINT_16)HIF_TX_HDR_TX_BYTE_COUNT_MASK);

            rHwTxHeader.u2TxByteCount_UserPriority = u2OverallBufferLength;
            rHwTxHeader.u2TxByteCount_UserPriority |=
                ((UINT_16)prMsduInfo->ucUserPriority << HIF_TX_HDR_USER_PRIORITY_OFFSET);

            if (prMsduInfo->fgIs802_11) {
                ucEtherTypeOffsetInWord =
                    (TX_HDR_SIZE + prMsduInfo->ucMacHeaderLength + prMsduInfo->ucLlcLength) >> 1;
            }
            else {
                ucEtherTypeOffsetInWord =
                    ((ETHER_HEADER_LEN - ETHER_TYPE_LEN) + TX_HDR_SIZE) >> 1;
            }

            rHwTxHeader.ucEtherTypeOffset =
                ucEtherTypeOffsetInWord & HIF_TX_HDR_ETHER_TYPE_OFFSET_MASK;

            rHwTxHeader.ucResource_PktType_CSflags = (prMsduInfo->ucTC) << HIF_TX_HDR_RESOURCE_OFFSET;
            rHwTxHeader.ucResource_PktType_CSflags |=
                (UINT_8)(((prMsduInfo->ucPacketType) << HIF_TX_HDR_PACKET_TYPE_OFFSET) &
                        (HIF_TX_HDR_PACKET_TYPE_MASK));

#if CFG_TCP_IP_CHKSUM_OFFLOAD
            if (prMsduInfo->eSrc == TX_PACKET_OS) {
                if (prAdapter->u4CSUMFlags &
                        (CSUM_OFFLOAD_EN_TX_TCP |
                         CSUM_OFFLOAD_EN_TX_UDP |
                         CSUM_OFFLOAD_EN_TX_IP)) {
                    kalQueryTxChksumOffloadParam(prNativePacket, &ucChksumFlag);

                    if (ucChksumFlag & TX_CS_IP_GEN) {
                        rHwTxHeader.ucResource_PktType_CSflags |= (UINT_8)HIF_TX_HDR_IP_CSUM;
                    }

                    if (ucChksumFlag & TX_CS_TCP_UDP_GEN) {
                        rHwTxHeader.ucResource_PktType_CSflags |= (UINT_8)HIF_TX_HDR_TCP_CSUM;
                    }
                }
            }
#endif /* CFG_TCP_IP_CHKSUM_OFFLOAD */

            rHwTxHeader.u2LLH = prMsduInfo->u2PalLLH;
            rHwTxHeader.ucStaRecIdx = prMsduInfo->ucStaRecIndex;
            rHwTxHeader.ucForwardingType_SessionID_Reserved =
                (prMsduInfo->ucPsForwardingType) | ((prMsduInfo->ucPsSessionID) << HIF_TX_HDR_PS_SESSION_ID_OFFSET)
                | ((prMsduInfo->fgIsBurstEnd)? HIF_TX_HDR_BURST_END_MASK : 0);

            rHwTxHeader.ucWlanHeaderLength = (prMsduInfo->ucMacHeaderLength & HIF_TX_HDR_WLAN_HEADER_LEN_MASK);
            rHwTxHeader.ucPktFormtId_Flags =
                (prMsduInfo->ucFormatID & HIF_TX_HDR_FORMAT_ID_MASK)
                | ((prMsduInfo->ucNetworkType << HIF_TX_HDR_NETWORK_TYPE_OFFSET) & HIF_TX_HDR_NETWORK_TYPE_MASK)
                | ((prMsduInfo->fgIs802_1x << HIF_TX_HDR_FLAG_1X_FRAME_OFFSET) & HIF_TX_HDR_FLAG_1X_FRAME_MASK)
                | ((prMsduInfo->fgIs802_11 << HIF_TX_HDR_FLAG_802_11_FORMAT_OFFSET) & HIF_TX_HDR_FLAG_802_11_FORMAT_MASK);

            rHwTxHeader.u2SeqNo = prMsduInfo->u2AclSN;

            if(prMsduInfo->pfTxDoneHandler) {
                rHwTxHeader.ucPacketSeqNo = prMsduInfo->ucTxSeqNum;
                rHwTxHeader.ucAck_BIP_BasicRate = HIF_TX_HDR_NEED_ACK;
            }
            else {
                rHwTxHeader.ucPacketSeqNo = 0;
                rHwTxHeader.ucAck_BIP_BasicRate = 0;
            }

            if(prMsduInfo->fgIsBIP) {
                rHwTxHeader.ucAck_BIP_BasicRate |= HIF_TX_HDR_BIP;
            }

            if(prMsduInfo->fgIsBasicRate) {
                rHwTxHeader.ucAck_BIP_BasicRate |= HIF_TX_HDR_BASIC_RATE;
            }

#if CFG_ENABLE_PKT_LIFETIME_PROFILE
            if(prMsduInfo->rPktProfile.fgIsValid) {
                prMsduInfo->rPktProfile.rDequeueTimestamp = kalGetTimeTick();
            }
#endif            

#if CFG_SDIO_TX_AGG
            // attach to coalescing buffer
            kalMemCopy(pucOutputBuf + u4TotalLength, &rHwTxHeader, u4TxHdrSize);
            u4TotalLength += u4TxHdrSize;

            if (prMsduInfo->eSrc == TX_PACKET_OS
                    || prMsduInfo->eSrc == TX_PACKET_FORWARDING) {
                kalCopyFrame(prAdapter->prGlueInfo,
                        prNativePacket,
                        pucOutputBuf + u4TotalLength);
            }
            else if(prMsduInfo->eSrc == TX_PACKET_MGMT) {
                kalMemCopy(pucOutputBuf + u4TotalLength,
                        prNativePacket,
                        prMsduInfo->u2FrameLength);
            }
            else {
                ASSERT(0);
            }

            u4TotalLength += ALIGN_4(prMsduInfo->u2FrameLength);

#else
            kalMemCopy(pucOutputBuf, &rHwTxHeader, u4TxHdrSize);

            /* Copy Frame Body */
            if (prMsduInfo->eSrc == TX_PACKET_OS
                    || prMsduInfo->eSrc == TX_PACKET_FORWARDING) {
                kalCopyFrame(prAdapter->prGlueInfo,
                        prNativePacket,
                        pucOutputBuf + u4TxHdrSize);
            }
            else if(prMsduInfo->eSrc == TX_PACKET_MGMT) {
                kalMemCopy(pucOutputBuf + u4TxHdrSize,
                        prNativePacket,
                        prMsduInfo->u2FrameLength);
            }
            else {
                ASSERT(0);
            }

            ASSERT(u2OverallBufferLength <= u4ValidBufSize);

            HAL_WRITE_TX_PORT(prAdapter,
                    ucPortIdx,
                    (UINT_32)u2OverallBufferLength,
                    (PUINT_8)pucOutputBuf,
                    u4ValidBufSize);

            // send immediately
#endif
            prNextMsduInfo = (P_MSDU_INFO_T)
                        QUEUE_GET_NEXT_ENTRY(&prMsduInfo->rQueEntry);

            if (prMsduInfo->eSrc == TX_PACKET_MGMT) {
                GLUE_DEC_REF_CNT(prTxCtrl->i4TxMgmtPendingNum);

                if (prMsduInfo->pfTxDoneHandler == NULL) {
                    cnmMgtPktFree(prAdapter, prMsduInfo);
                }
                else {
                    KAL_SPIN_LOCK_DECLARATION();
                    DBGLOG(INIT, TRACE,("Wait TxSeqNum:%d\n", prMsduInfo->ucTxSeqNum));
                    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TXING_MGMT_LIST);
                    QUEUE_INSERT_TAIL(&(prTxCtrl->rTxMgmtTxingQueue), (P_QUE_ENTRY_T)prMsduInfo);
                    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TXING_MGMT_LIST);
                }
            }
            else {
                /* only free MSDU when it is not a MGMT frame */
                QUEUE_INSERT_TAIL(&rFreeQueue, (P_QUE_ENTRY_T)prMsduInfo);

                if (prMsduInfo->eSrc == TX_PACKET_OS) {
                    kalSendComplete(prAdapter->prGlueInfo,
                            prNativePacket,
                            WLAN_STATUS_SUCCESS);
                }
                else if(prMsduInfo->eSrc == TX_PACKET_FORWARDING) {
                    GLUE_DEC_REF_CNT(prTxCtrl->i4PendingFwdFrameCount);
                }
            }

            prMsduInfo = prNextMsduInfo;
        }

#if CFG_SDIO_TX_AGG
        ASSERT(u4TotalLength <= u4ValidBufSize);

    #if CFG_DBG_GPIO_PINS
        {
            /* Start port write */
            mtk_wcn_stp_debug_gpio_assert(IDX_TX_PORT_WRITE, DBG_TIE_LOW);
            kalUdelay(1);
            mtk_wcn_stp_debug_gpio_assert(IDX_TX_PORT_WRITE, DBG_TIE_HIGH);
        }
    #endif

        // send coalescing buffer
        HAL_WRITE_TX_PORT(prAdapter,
                ucPortIdx,
                u4TotalLength,
                (PUINT_8)pucOutputBuf,
                u4ValidBufSize);
#endif

#if CFG_ENABLE_PKT_LIFETIME_PROFILE
        nicTxReturnMsduInfoProfiling(prAdapter, (P_MSDU_INFO_T)QUEUE_GET_HEAD(&rFreeQueue));
#else
        // return
        nicTxReturnMsduInfo(prAdapter, (P_MSDU_INFO_T)QUEUE_GET_HEAD(&rFreeQueue));
#endif
    }

    return WLAN_STATUS_SUCCESS;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief In this function, we'll write Command(CMD_INFO_T) into HIF.
*
* @param prAdapter      Pointer to the Adapter structure.
* @param prPacketInfo   Pointer of CMD_INFO_T
* @param ucTC           Specify the resource of TC
*
* @retval WLAN_STATUS_SUCCESS   Bus access ok.
* @retval WLAN_STATUS_FAILURE   Bus access fail.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicTxCmd (
    IN P_ADAPTER_T      prAdapter,
    IN P_CMD_INFO_T     prCmdInfo,
    IN UINT_8           ucTC
    )
{
    P_WIFI_CMD_T prWifiCmd;
    UINT_16 u2OverallBufferLength;
    PUINT_8 pucOutputBuf = (PUINT_8)NULL; /* Pointer to Transmit Data Structure Frame */
    UINT_8 ucPortIdx;
    HIF_TX_HEADER_T rHwTxHeader;
    P_NATIVE_PACKET prNativePacket;
    UINT_8 ucEtherTypeOffsetInWord;
    P_MSDU_INFO_T prMsduInfo;
    P_TX_CTRL_T prTxCtrl;

    KAL_SPIN_LOCK_DECLARATION();


    ASSERT(prAdapter);
    ASSERT(prCmdInfo);

    prTxCtrl = &prAdapter->rTxCtrl;
    pucOutputBuf = prTxCtrl->pucTxCoalescingBufPtr;

    // <1> Assign Data Port
    if (ucTC != TC4_INDEX) {
        ucPortIdx = 0;
    }
    else {
        // Broadcast/multicast data frames, 1x frames, command packets, MMPDU
        ucPortIdx = 1;
    }

    if(prCmdInfo->eCmdType == COMMAND_TYPE_SECURITY_FRAME) {
        // <2> Compose HIF_TX_HEADER
        kalMemZero(&rHwTxHeader, sizeof(rHwTxHeader));

        prNativePacket = prCmdInfo->prPacket;

        ASSERT(prNativePacket);

        u2OverallBufferLength = TFCB_FRAME_PAD_TO_DW((prCmdInfo->u2InfoBufLen + TX_HDR_SIZE)
                & (UINT_16)HIF_TX_HDR_TX_BYTE_COUNT_MASK);

        rHwTxHeader.u2TxByteCount_UserPriority = ((prCmdInfo->u2InfoBufLen + TX_HDR_SIZE)
                & (UINT_16)HIF_TX_HDR_TX_BYTE_COUNT_MASK);
        ucEtherTypeOffsetInWord = ((ETHER_HEADER_LEN - ETHER_TYPE_LEN) + TX_HDR_SIZE) >> 1;

        rHwTxHeader.ucEtherTypeOffset =
            ucEtherTypeOffsetInWord & HIF_TX_HDR_ETHER_TYPE_OFFSET_MASK;

        rHwTxHeader.ucResource_PktType_CSflags = (ucTC << HIF_TX_HDR_RESOURCE_OFFSET);

        rHwTxHeader.ucStaRecIdx = prCmdInfo->ucStaRecIndex;
        rHwTxHeader.ucForwardingType_SessionID_Reserved = HIF_TX_HDR_BURST_END_MASK;

        rHwTxHeader.ucWlanHeaderLength = (ETH_HLEN & HIF_TX_HDR_WLAN_HEADER_LEN_MASK);
        rHwTxHeader.ucPktFormtId_Flags =
            (((UINT_8)(prCmdInfo->eNetworkType) << HIF_TX_HDR_NETWORK_TYPE_OFFSET) & HIF_TX_HDR_NETWORK_TYPE_MASK)
            | ((1 << HIF_TX_HDR_FLAG_1X_FRAME_OFFSET) & HIF_TX_HDR_FLAG_1X_FRAME_MASK);

        rHwTxHeader.u2SeqNo = 0;
        rHwTxHeader.ucPacketSeqNo = 0;
        rHwTxHeader.ucAck_BIP_BasicRate = 0;

        // <2.3> Copy HIF TX HEADER
        kalMemCopy((PVOID)&pucOutputBuf[0], (PVOID)&rHwTxHeader, TX_HDR_SIZE);

        // <3> Copy Frame Body Copy
        kalCopyFrame(prAdapter->prGlueInfo,
                prNativePacket,
                pucOutputBuf + TX_HDR_SIZE);
    }
    else if(prCmdInfo->eCmdType == COMMAND_TYPE_MANAGEMENT_FRAME) {
        prMsduInfo = (P_MSDU_INFO_T)prCmdInfo->prPacket;

        ASSERT(prMsduInfo->fgIs802_11 == TRUE);
        ASSERT(prMsduInfo->eSrc == TX_PACKET_MGMT);

        // <2> Compose HIF_TX_HEADER
        kalMemZero(&rHwTxHeader, sizeof(rHwTxHeader));

        u2OverallBufferLength = ((prMsduInfo->u2FrameLength + TX_HDR_SIZE) &
                                  (UINT_16)HIF_TX_HDR_TX_BYTE_COUNT_MASK);

        rHwTxHeader.u2TxByteCount_UserPriority = u2OverallBufferLength;
        rHwTxHeader.u2TxByteCount_UserPriority |=
                ((UINT_16)prMsduInfo->ucUserPriority << HIF_TX_HDR_USER_PRIORITY_OFFSET);

        ucEtherTypeOffsetInWord =
                (TX_HDR_SIZE + prMsduInfo->ucMacHeaderLength + prMsduInfo->ucLlcLength) >> 1;

        rHwTxHeader.ucEtherTypeOffset =
                ucEtherTypeOffsetInWord & HIF_TX_HDR_ETHER_TYPE_OFFSET_MASK;

        rHwTxHeader.ucResource_PktType_CSflags = (prMsduInfo->ucTC) << HIF_TX_HDR_RESOURCE_OFFSET;
        rHwTxHeader.ucResource_PktType_CSflags |=
                (UINT_8)(((prMsduInfo->ucPacketType) << HIF_TX_HDR_PACKET_TYPE_OFFSET) &
                                    (HIF_TX_HDR_PACKET_TYPE_MASK));

        rHwTxHeader.u2LLH = prMsduInfo->u2PalLLH;
        rHwTxHeader.ucStaRecIdx = prMsduInfo->ucStaRecIndex;
        rHwTxHeader.ucForwardingType_SessionID_Reserved =
            (prMsduInfo->ucPsForwardingType) | ((prMsduInfo->ucPsSessionID) << HIF_TX_HDR_PS_SESSION_ID_OFFSET)
            | ((prMsduInfo->fgIsBurstEnd)? HIF_TX_HDR_BURST_END_MASK : 0);

        rHwTxHeader.ucWlanHeaderLength = (prMsduInfo->ucMacHeaderLength & HIF_TX_HDR_WLAN_HEADER_LEN_MASK);
        rHwTxHeader.ucPktFormtId_Flags =
            (prMsduInfo->ucFormatID & HIF_TX_HDR_FORMAT_ID_MASK)
            | ((prMsduInfo->ucNetworkType << HIF_TX_HDR_NETWORK_TYPE_OFFSET) & HIF_TX_HDR_NETWORK_TYPE_MASK)
            | ((prMsduInfo->fgIs802_1x << HIF_TX_HDR_FLAG_1X_FRAME_OFFSET) & HIF_TX_HDR_FLAG_1X_FRAME_MASK)
            | ((prMsduInfo->fgIs802_11 << HIF_TX_HDR_FLAG_802_11_FORMAT_OFFSET) & HIF_TX_HDR_FLAG_802_11_FORMAT_MASK);

        rHwTxHeader.u2SeqNo = prMsduInfo->u2AclSN;

        if(prMsduInfo->pfTxDoneHandler) {
            rHwTxHeader.ucPacketSeqNo = prMsduInfo->ucTxSeqNum;
            rHwTxHeader.ucAck_BIP_BasicRate = HIF_TX_HDR_NEED_ACK;
        }
        else {
            rHwTxHeader.ucPacketSeqNo = 0;
            rHwTxHeader.ucAck_BIP_BasicRate = 0;
        }

        if(prMsduInfo->fgIsBIP) {
            rHwTxHeader.ucAck_BIP_BasicRate |= HIF_TX_HDR_BIP;
        }

        if(prMsduInfo->fgIsBasicRate) {
            rHwTxHeader.ucAck_BIP_BasicRate |= HIF_TX_HDR_BASIC_RATE;
        }

        // <2.3> Copy HIF TX HEADER
        kalMemCopy((PVOID)&pucOutputBuf[0], (PVOID)&rHwTxHeader, TX_HDR_SIZE);

        // <3> Copy Frame Body
        kalMemCopy(pucOutputBuf + TX_HDR_SIZE,
                prMsduInfo->prPacket,
                prMsduInfo->u2FrameLength);

        // <4> Management Frame Post-Processing
        GLUE_DEC_REF_CNT(prTxCtrl->i4TxMgmtPendingNum);

        if (prMsduInfo->pfTxDoneHandler == NULL) {
            cnmMgtPktFree(prAdapter, prMsduInfo);
        }
        else {

            DBGLOG(INIT, TRACE,("Wait Cmd TxSeqNum:%d\n", prMsduInfo->ucTxSeqNum));

            KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TXING_MGMT_LIST);
            QUEUE_INSERT_TAIL(&(prTxCtrl->rTxMgmtTxingQueue), (P_QUE_ENTRY_T)prMsduInfo);
            KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TXING_MGMT_LIST);
        }
    }
    else {
        prWifiCmd = (P_WIFI_CMD_T)prCmdInfo->pucInfoBuffer;

        // <2> Compose the Header of Transmit Data Structure for CMD Packet
        u2OverallBufferLength = TFCB_FRAME_PAD_TO_DW(
               (prCmdInfo->u2InfoBufLen) & (UINT_16)HIF_TX_HDR_TX_BYTE_COUNT_MASK);

        prWifiCmd->u2TxByteCount_UserPriority = u2OverallBufferLength;
        prWifiCmd->ucEtherTypeOffset = 0;
        prWifiCmd->ucResource_PktType_CSflags = (ucTC << HIF_TX_HDR_RESOURCE_OFFSET)
            | (UINT_8)((HIF_TX_PKT_TYPE_CMD << HIF_TX_HDR_PACKET_TYPE_OFFSET) & (HIF_TX_HDR_PACKET_TYPE_MASK));


        // <3> Copy CMD Header to command buffer (by using pucCoalescingBufCached)
        kalMemCopy((PVOID)&pucOutputBuf[0],
                   (PVOID)prCmdInfo->pucInfoBuffer,
                   prCmdInfo->u2InfoBufLen);

        ASSERT(u2OverallBufferLength <= prAdapter->u4CoalescingBufCachedSize);
    }

    // <4> Write frame to data port
    HAL_WRITE_TX_PORT(prAdapter,
            ucPortIdx,
            (UINT_32)u2OverallBufferLength,
            (PUINT_8)pucOutputBuf,
            (UINT_32)prAdapter->u4CoalescingBufCachedSize);

    return WLAN_STATUS_SUCCESS;
} /* end of nicTxCmd() */


/*----------------------------------------------------------------------------*/
/*!
* @brief This function will clean up all the pending frames in internal SW Queues
*        by return the pending TX packet to the system.
*
* @param prAdapter  Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicTxRelease (
    IN P_ADAPTER_T  prAdapter
    )
{
    P_TX_CTRL_T prTxCtrl;
    P_MSDU_INFO_T prMsduInfo;

    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);

    prTxCtrl = &prAdapter->rTxCtrl;

    nicTxFlush(prAdapter);

    // free MSDU_INFO_T from rTxMgmtMsduInfoList
    do {
        KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TXING_MGMT_LIST);
        QUEUE_REMOVE_HEAD(&prTxCtrl->rTxMgmtTxingQueue, prMsduInfo, P_MSDU_INFO_T);
        KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TXING_MGMT_LIST);

        if(prMsduInfo) {
            // the packet must be mgmt frame with tx done callback
            ASSERT(prMsduInfo->eSrc == TX_PACKET_MGMT);
            ASSERT(prMsduInfo->pfTxDoneHandler != NULL);

            // invoke done handler
            prMsduInfo->pfTxDoneHandler(prAdapter, prMsduInfo, TX_RESULT_LIFE_TIMEOUT);

            cnmMgtPktFree(prAdapter, prMsduInfo);
        }
        else {
            break;
        }
    } while(TRUE);

    return;
} /* end of nicTxRelease() */


/*----------------------------------------------------------------------------*/
/*!
* @brief Process the TX Done interrupt and pull in more pending frames in SW
*        Queues for transmission.
*
* @param prAdapter  Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicProcessTxInterrupt(
    IN P_ADAPTER_T prAdapter
    )
{
    P_TX_CTRL_T prTxCtrl;
#if CFG_SDIO_INTR_ENHANCE
    P_SDIO_CTRL_T prSDIOCtrl;
#else
    UINT_32 au4TxCount[2];
#endif /* CFG_SDIO_INTR_ENHANCE */

    ASSERT(prAdapter);

    prTxCtrl = &prAdapter->rTxCtrl;
    ASSERT(prTxCtrl);

     /* Get the TX STATUS */
#if CFG_SDIO_INTR_ENHANCE

    prSDIOCtrl = prAdapter->prSDIOCtrl;
    #if DBG
    //dumpMemory8((PUINT_8)prSDIOCtrl, sizeof(SDIO_CTRL_T));
    #endif

    nicTxReleaseResource(prAdapter, (PUINT_8)&prSDIOCtrl->rTxInfo);
    kalMemZero(&prSDIOCtrl->rTxInfo, sizeof(prSDIOCtrl->rTxInfo));

#else

    HAL_MCR_RD(prAdapter, MCR_WTSR0, &au4TxCount[0]);
    HAL_MCR_RD(prAdapter, MCR_WTSR1, &au4TxCount[1]);
    DBGLOG(EMU, TRACE, ("MCR_WTSR0: 0x%x, MCR_WTSR1: 0x%x\n", au4TxCount[0], au4TxCount[1]));

    nicTxReleaseResource(prAdapter, (PUINT_8)au4TxCount);

#endif /* CFG_SDIO_INTR_ENHANCE */

    nicTxAdjustTcq(prAdapter);

    // Indicate Service Thread
    if(kalGetTxPendingCmdCount(prAdapter->prGlueInfo) > 0
            || wlanGetTxPendingFrameCount(prAdapter) > 0) {
        kalSetEvent(prAdapter->prGlueInfo);
    }

    return;
} /* end of nicProcessTxInterrupt() */


/*----------------------------------------------------------------------------*/
/*!
* @brief this function frees packet of P_MSDU_INFO_T linked-list
*
* @param prAdapter              Pointer to the Adapter structure.
* @param prMsduInfoList         a link list of P_MSDU_INFO_T
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicTxFreeMsduInfoPacket (
    IN P_ADAPTER_T    prAdapter,
    IN P_MSDU_INFO_T  prMsduInfoListHead
    )
{
    P_NATIVE_PACKET prNativePacket;
    P_MSDU_INFO_T prMsduInfo = prMsduInfoListHead;
    P_TX_CTRL_T prTxCtrl;


    ASSERT(prAdapter);
    ASSERT(prMsduInfoListHead);

    prTxCtrl = &prAdapter->rTxCtrl;

    while(prMsduInfo) {
        prNativePacket = prMsduInfo->prPacket;

        if(prMsduInfo->eSrc == TX_PACKET_OS) {
            kalSendComplete(prAdapter->prGlueInfo,
                    prNativePacket,
                    WLAN_STATUS_FAILURE);
        }
        else if(prMsduInfo->eSrc == TX_PACKET_MGMT) {
            if (prMsduInfo->pfTxDoneHandler) {
                prMsduInfo->pfTxDoneHandler(prAdapter, prMsduInfo, TX_RESULT_DROPPED_IN_DRIVER);
            }
            cnmMemFree(prAdapter, prNativePacket);
        }
        else if(prMsduInfo->eSrc == TX_PACKET_FORWARDING) {
            GLUE_DEC_REF_CNT(prTxCtrl->i4PendingFwdFrameCount);
        }

        prMsduInfo = (P_MSDU_INFO_T)QUEUE_GET_NEXT_ENTRY((P_QUE_ENTRY_T)prMsduInfo);
    }

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief this function returns P_MSDU_INFO_T of MsduInfoList to TxCtrl->rfreeMsduInfoList
*
* @param prAdapter              Pointer to the Adapter structure.
* @param prMsduInfoList         a link list of P_MSDU_INFO_T
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicTxReturnMsduInfo (
    IN P_ADAPTER_T    prAdapter,
    IN P_MSDU_INFO_T  prMsduInfoListHead
    )
{
    P_TX_CTRL_T prTxCtrl;
    P_MSDU_INFO_T prMsduInfo = prMsduInfoListHead, prNextMsduInfo;

    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);

    prTxCtrl = &prAdapter->rTxCtrl;
    ASSERT(prTxCtrl);

    while(prMsduInfo) {
        prNextMsduInfo = (P_MSDU_INFO_T)QUEUE_GET_NEXT_ENTRY((P_QUE_ENTRY_T)prMsduInfo);

        switch(prMsduInfo->eSrc) {
        case TX_PACKET_FORWARDING:
            wlanReturnPacket(prAdapter, prMsduInfo->prPacket);
            break;
        case TX_PACKET_OS:
        case TX_PACKET_OS_OID:
        case TX_PACKET_MGMT:
        default:
            break;
        }

        KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_MSDU_INFO_LIST);
        QUEUE_INSERT_TAIL(&prTxCtrl->rFreeMsduInfoList, (P_QUE_ENTRY_T)prMsduInfo);
        KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_MSDU_INFO_LIST);
        prMsduInfo = prNextMsduInfo;
    };

    return;
}



/*----------------------------------------------------------------------------*/
/*!
* @brief this function fills packet information to P_MSDU_INFO_T
*
* @param prAdapter              Pointer to the Adapter structure.
* @param prMsduInfo             P_MSDU_INFO_T
* @param prPacket               P_NATIVE_PACKET
*
* @retval TRUE      Success to extract information
* @retval FALSE     Fail to extract correct information
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
nicTxFillMsduInfo (
    IN P_ADAPTER_T     prAdapter,
    IN P_MSDU_INFO_T   prMsduInfo,
    IN P_NATIVE_PACKET prPacket
    )
{
    P_GLUE_INFO_T   prGlueInfo;
    UINT_8          ucPriorityParam;
    UINT_8          ucMacHeaderLen;
    UINT_8          aucEthDestAddr[PARAM_MAC_ADDR_LEN];
    BOOLEAN         fgIs1x = FALSE;
    BOOLEAN         fgIsPAL = FALSE;
    UINT_32         u4PacketLen;
    ULONG           u4SysTime;
    UINT_8          ucNetworkType;


    ASSERT(prAdapter);

    prGlueInfo = prAdapter->prGlueInfo;
    ASSERT(prGlueInfo);

    if (kalQoSFrameClassifierAndPacketInfo(prGlueInfo,
                prPacket,
                &ucPriorityParam,
                &u4PacketLen,
                aucEthDestAddr,
                &fgIs1x,
                &fgIsPAL,
                &ucNetworkType) == FALSE) {
        return FALSE;
    }

    #if CFG_ENABLE_PKT_LIFETIME_PROFILE
    do {        
        struct sk_buff *prSkb = (struct sk_buff *) prPacket;
        UINT_16 u2EtherTypeLen;        
        PUINT_8 aucLookAheadBuf = NULL;
        P_PKT_PROFILE_T prPktProfile = &prMsduInfo->rPktProfile;

        UINT_8 ucRtpSnOffset = 30;
        UINT_32 u4RtpSrcPort = 15550;
        

        prPktProfile->fgIsValid = FALSE;

        aucLookAheadBuf = prSkb->data;

        u2EtherTypeLen = (aucLookAheadBuf[ETH_TYPE_LEN_OFFSET] << 8) | (aucLookAheadBuf[ETH_TYPE_LEN_OFFSET + 1]);

        if ((u2EtherTypeLen == ETH_P_IP) &&
            (u4PacketLen >= LOOK_AHEAD_LEN)) {
            PUINT_8 pucIpHdr = &aucLookAheadBuf[ETH_HLEN];
            UINT_8 ucIpVersion;
        
            ucIpVersion = (pucIpHdr[0] & IPVH_VERSION_MASK) >> IPVH_VERSION_OFFSET;
            if (ucIpVersion == IPVERSION) {
                if(pucIpHdr[IPV4_HDR_IP_PROTOCOL_OFFSET] == IP_PROTOCOL_UDP) {

                    /* Enable packet lifetime profiling */
                    prPktProfile->fgIsValid = TRUE;

                    prPktProfile->fgIsPrinted = FALSE;

                    /* RTP SN */
                    prPktProfile->u2RtpSn = pucIpHdr[ucRtpSnOffset] << 8 | pucIpHdr[ucRtpSnOffset + 1];

                    /* IP SN */
                    prPktProfile->u2IpSn = pucIpHdr[IPV4_HDR_IP_IDENTIFICATION_OFFSET] << 8 |
						                    pucIpHdr[IPV4_HDR_IP_IDENTIFICATION_OFFSET + 1];

                    /* Packet arrival time at kernel Hard Xmit */
                    prPktProfile->rHardXmitArrivalTimestamp = GLUE_GET_PKT_ARRIVAL_TIME(prPacket);

                    /* Packet enqueue time */
                    prPktProfile->rEnqueueTimestamp = (OS_SYSTIME)kalGetTimeTick();
                }
            }
        }
    }while(FALSE);
    #endif

    /* Save the value of Priority Parameter */
    GLUE_SET_PKT_TID(prPacket, ucPriorityParam);

    if (fgIs1x) {
        GLUE_SET_PKT_FLAG_1X(prPacket);
    }

    if (fgIsPAL) {
        GLUE_SET_PKT_FLAG_PAL(prPacket);
    }

    ucMacHeaderLen = ETH_HLEN;

    /* Save the value of Header Length */
    GLUE_SET_PKT_HEADER_LEN(prPacket, ucMacHeaderLen);

    /* Save the value of Frame Length */
    GLUE_SET_PKT_FRAME_LEN(prPacket, (UINT_16)u4PacketLen);

    /* Save the value of Arrival Time*/
    u4SysTime = (OS_SYSTIME)kalGetTimeTick();
    GLUE_SET_PKT_ARRIVAL_TIME(prPacket, u4SysTime);

    prMsduInfo->prPacket = prPacket;
    prMsduInfo->fgIs802_1x = fgIs1x;
    prMsduInfo->fgIs802_11 = FALSE;
    prMsduInfo->ucNetworkType = ucNetworkType;
    prMsduInfo->ucUserPriority = ucPriorityParam;
    prMsduInfo->ucMacHeaderLength = ucMacHeaderLen;
    prMsduInfo->u2FrameLength = (UINT_16)u4PacketLen;
    COPY_MAC_ADDR(prMsduInfo->aucEthDestAddr, aucEthDestAddr);

    return TRUE;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief this function update TCQ values by passing current status to txAdjustTcQuotas
*
* @param prAdapter              Pointer to the Adapter structure.
*
* @retval WLAN_STATUS_SUCCESS   Updated successfully
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicTxAdjustTcq (
    IN P_ADAPTER_T  prAdapter
    )
{
    UINT_32 u4Num;
    TX_TCQ_ADJUST_T rTcqAdjust;
    P_TX_CTRL_T prTxCtrl;
    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);

    prTxCtrl = &prAdapter->rTxCtrl;
    ASSERT(prTxCtrl);

    qmAdjustTcQuotas(prAdapter, &rTcqAdjust, &prTxCtrl->rTc);
    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_RESOURCE);

    for (u4Num = 0 ; u4Num < TC_NUM ; u4Num++) {
        prTxCtrl->rTc.aucFreeBufferCount[u4Num] += rTcqAdjust.acVariation[u4Num];
        prTxCtrl->rTc.aucMaxNumOfBuffer[u4Num] += rTcqAdjust.acVariation[u4Num];

        ASSERT(prTxCtrl->rTc.aucFreeBufferCount[u4Num] >= 0);
        ASSERT(prTxCtrl->rTc.aucMaxNumOfBuffer[u4Num] >= 0);
    }

    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_RESOURCE);

    return WLAN_STATUS_SUCCESS;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief this function flushes all packets queued in STA/AC queue
*
* @param prAdapter              Pointer to the Adapter structure.
*
* @retval WLAN_STATUS_SUCCESS   Flushed successfully
*/
/*----------------------------------------------------------------------------*/

WLAN_STATUS
nicTxFlush (
    IN P_ADAPTER_T  prAdapter
    )
{
    P_MSDU_INFO_T prMsduInfo;
    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);

    // ask Per STA/AC queue to be fllushed and return all queued packets
    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_QM_TX_QUEUE);
    prMsduInfo = qmFlushTxQueues(prAdapter);
    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_QM_TX_QUEUE);

    if(prMsduInfo != NULL) {
        nicTxFreeMsduInfoPacket(prAdapter, prMsduInfo);
        nicTxReturnMsduInfo(prAdapter, prMsduInfo);
    }

    return WLAN_STATUS_SUCCESS;
}


#if CFG_ENABLE_FW_DOWNLOAD
/*----------------------------------------------------------------------------*/
/*!
* \brief In this function, we'll write Command(CMD_INFO_T) into HIF.
*        However this function is used for INIT_CMD.
*
*        In order to avoid further maintainance issues, these 2 functions are separated
*
* @param prAdapter      Pointer to the Adapter structure.
* @param prPacketInfo   Pointer of CMD_INFO_T
* @param ucTC           Specify the resource of TC
*
* @retval WLAN_STATUS_SUCCESS   Bus access ok.
* @retval WLAN_STATUS_FAILURE   Bus access fail.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicTxInitCmd (
    IN P_ADAPTER_T      prAdapter,
    IN P_CMD_INFO_T     prCmdInfo,
    IN UINT_8           ucTC
    )
{
    P_INIT_HIF_TX_HEADER_T prInitTxHeader;
    UINT_16 u2OverallBufferLength;
    PUINT_8 pucOutputBuf = (PUINT_8)NULL; /* Pointer to Transmit Data Structure Frame */
    UINT_32 ucPortIdx;
    P_TX_CTRL_T prTxCtrl;

    ASSERT(prAdapter);
    ASSERT(prCmdInfo);
    ASSERT(ucTC == TC0_INDEX);

    prTxCtrl = &prAdapter->rTxCtrl;
    pucOutputBuf = prTxCtrl->pucTxCoalescingBufPtr;
    prInitTxHeader = (P_INIT_HIF_TX_HEADER_T)prCmdInfo->pucInfoBuffer;

    // <1> Compose the Header of Transmit Data Structure for CMD Packet
    u2OverallBufferLength = TFCB_FRAME_PAD_TO_DW(
            (prCmdInfo->u2InfoBufLen) & (UINT_16)HIF_TX_HDR_TX_BYTE_COUNT_MASK);

    prInitTxHeader->u2TxByteCount = u2OverallBufferLength;
    prInitTxHeader->ucEtherTypeOffset = 0;
    prInitTxHeader->ucCSflags = 0;

    // <2> Assign Data Port
    if (ucTC != TC4_INDEX) {
        ucPortIdx = 0;
    }
    else { // Broadcast/multicast data packets
        ucPortIdx = 1;
    }

    // <3> Copy CMD Header to command buffer (by using pucCoalescingBufCached)
    kalMemCopy((PVOID)&pucOutputBuf[0],
               (PVOID)prCmdInfo->pucInfoBuffer,
               prCmdInfo->u2InfoBufLen);

    ASSERT(u2OverallBufferLength <= prAdapter->u4CoalescingBufCachedSize);

    // <4> Write frame to data port
    HAL_WRITE_TX_PORT(prAdapter,
            ucPortIdx,
            (UINT_32)u2OverallBufferLength,
            (PUINT_8)pucOutputBuf,
            (UINT_32)prAdapter->u4CoalescingBufCachedSize);

    return WLAN_STATUS_SUCCESS;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief In this function, we'll reset TX resource counter to initial value used
*        in F/W download state
*
* @param prAdapter      Pointer to the Adapter structure.
*
* @retval WLAN_STATUS_SUCCESS   Reset is done successfully.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicTxInitResetResource (
    IN P_ADAPTER_T  prAdapter
    )
{
    P_TX_CTRL_T prTxCtrl;

    DEBUGFUNC("nicTxInitResetResource");

    ASSERT(prAdapter);
    prTxCtrl = &prAdapter->rTxCtrl;

    prTxCtrl->rTc.aucMaxNumOfBuffer[TC0_INDEX] = NIC_TX_INIT_BUFF_COUNT_TC0;
    prTxCtrl->rTc.aucFreeBufferCount[TC0_INDEX] = NIC_TX_INIT_BUFF_COUNT_TC0;

    prTxCtrl->rTc.aucMaxNumOfBuffer[TC1_INDEX] = NIC_TX_INIT_BUFF_COUNT_TC1;
    prTxCtrl->rTc.aucFreeBufferCount[TC1_INDEX] = NIC_TX_INIT_BUFF_COUNT_TC1;

    prTxCtrl->rTc.aucMaxNumOfBuffer[TC2_INDEX] = NIC_TX_INIT_BUFF_COUNT_TC2;
    prTxCtrl->rTc.aucFreeBufferCount[TC2_INDEX] = NIC_TX_INIT_BUFF_COUNT_TC2;

    prTxCtrl->rTc.aucMaxNumOfBuffer[TC3_INDEX] = NIC_TX_INIT_BUFF_COUNT_TC3;
    prTxCtrl->rTc.aucFreeBufferCount[TC3_INDEX] = NIC_TX_INIT_BUFF_COUNT_TC3;

    prTxCtrl->rTc.aucMaxNumOfBuffer[TC4_INDEX] = NIC_TX_INIT_BUFF_COUNT_TC4;
    prTxCtrl->rTc.aucFreeBufferCount[TC4_INDEX] = NIC_TX_INIT_BUFF_COUNT_TC4;

    prTxCtrl->rTc.aucMaxNumOfBuffer[TC5_INDEX] = NIC_TX_INIT_BUFF_COUNT_TC5;
    prTxCtrl->rTc.aucFreeBufferCount[TC5_INDEX] = NIC_TX_INIT_BUFF_COUNT_TC5;

    return WLAN_STATUS_SUCCESS;

}

#endif


/*----------------------------------------------------------------------------*/
/*!
* \brief this function enqueues MSDU_INFO_T into queue management,
*        or command queue
*
* @param prAdapter      Pointer to the Adapter structure.
*        prMsduInfo     Pointer to MSDU
*
* @retval WLAN_STATUS_SUCCESS   Reset is done successfully.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicTxEnqueueMsdu (
    IN P_ADAPTER_T      prAdapter,
    IN P_MSDU_INFO_T    prMsduInfo
    )
{
    P_TX_CTRL_T prTxCtrl;
    P_MSDU_INFO_T prNextMsduInfo, prRetMsduInfo, prMsduInfoHead;
    QUE_T qDataPort0, qDataPort1;
    P_CMD_INFO_T prCmdInfo;
    WLAN_STATUS u4Status = WLAN_STATUS_SUCCESS;
    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);
    ASSERT(prMsduInfo);

    prTxCtrl = &prAdapter->rTxCtrl;
    ASSERT(prTxCtrl);

    QUEUE_INITIALIZE(&qDataPort0);
    QUEUE_INITIALIZE(&qDataPort1);

    /* check how many management frame are being queued */
    while(prMsduInfo) {
        prNextMsduInfo = (P_MSDU_INFO_T)QUEUE_GET_NEXT_ENTRY((P_QUE_ENTRY_T)prMsduInfo);

        QUEUE_GET_NEXT_ENTRY((P_QUE_ENTRY_T)prMsduInfo) = NULL;

        if(prMsduInfo->eSrc == TX_PACKET_MGMT) {
            // MMPDU: force stick to TC4
            prMsduInfo->ucTC = TC4_INDEX;

            QUEUE_INSERT_TAIL(&qDataPort1, (P_QUE_ENTRY_T)prMsduInfo);
        }
        else {
            QUEUE_INSERT_TAIL(&qDataPort0, (P_QUE_ENTRY_T)prMsduInfo);
        }

        prMsduInfo = prNextMsduInfo;
    }

    if(qDataPort0.u4NumElem) {
        /* send to QM */
        KAL_SPIN_LOCK_DECLARATION();
        KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_QM_TX_QUEUE);
        prRetMsduInfo = qmEnqueueTxPackets(prAdapter, (P_MSDU_INFO_T)QUEUE_GET_HEAD(&qDataPort0));
        KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_QM_TX_QUEUE);

        /* post-process for dropped packets */
        if(prRetMsduInfo != NULL) { // unable to enqueue
            nicTxFreeMsduInfoPacket(prAdapter, prRetMsduInfo);
            nicTxReturnMsduInfo(prAdapter, prRetMsduInfo);
        }
    }

    if(qDataPort1.u4NumElem) {
        prMsduInfoHead = (P_MSDU_INFO_T)QUEUE_GET_HEAD(&qDataPort1);

        if(qDataPort1.u4NumElem > nicTxGetFreeCmdCount(prAdapter)) {
            // not enough descriptors for sending
            u4Status = WLAN_STATUS_FAILURE;

            // free all MSDUs
            while(prMsduInfoHead) {
                prNextMsduInfo = (P_MSDU_INFO_T) QUEUE_GET_NEXT_ENTRY(&prMsduInfoHead->rQueEntry);

                if (prMsduInfoHead->pfTxDoneHandler != NULL) {
                    prMsduInfoHead->pfTxDoneHandler(prAdapter, prMsduInfoHead, TX_RESULT_DROPPED_IN_DRIVER);
                }


                cnmMgtPktFree(prAdapter, prMsduInfoHead);

                prMsduInfoHead = prNextMsduInfo;
            }
        }
        else {
            /* send to command queue */
            while(prMsduInfoHead) {
                prNextMsduInfo = (P_MSDU_INFO_T) QUEUE_GET_NEXT_ENTRY(&prMsduInfoHead->rQueEntry);

                KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_CMD_RESOURCE);
                QUEUE_REMOVE_HEAD(&prAdapter->rFreeCmdList, prCmdInfo, P_CMD_INFO_T);
                KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_CMD_RESOURCE);

                if (prCmdInfo) {
                    GLUE_INC_REF_CNT(prTxCtrl->i4TxMgmtPendingNum);

                    kalMemZero(prCmdInfo, sizeof(CMD_INFO_T));

                    prCmdInfo->eCmdType             = COMMAND_TYPE_MANAGEMENT_FRAME;
                    prCmdInfo->u2InfoBufLen         = prMsduInfoHead->u2FrameLength;
                    prCmdInfo->pucInfoBuffer        = NULL;
                    prCmdInfo->prPacket             = (P_NATIVE_PACKET)prMsduInfoHead;
                    prCmdInfo->ucStaRecIndex        = prMsduInfoHead->ucStaRecIndex;
                    prCmdInfo->eNetworkType         = prMsduInfoHead->ucNetworkType;
                    prCmdInfo->pfCmdDoneHandler     = NULL;
                    prCmdInfo->pfCmdTimeoutHandler  = NULL;
                    prCmdInfo->fgIsOid              = FALSE;
                    prCmdInfo->fgSetQuery           = TRUE;
                    prCmdInfo->fgNeedResp           = FALSE;

                    kalEnqueueCommand(prAdapter->prGlueInfo, (P_QUE_ENTRY_T)prCmdInfo);
                }
                else {
                    /* Cmd free count is larger than expected, but allocation fail. */
                    ASSERT(0);

                    u4Status = WLAN_STATUS_FAILURE;
                    cnmMgtPktFree(prAdapter, prMsduInfoHead);
                }

                prMsduInfoHead = prNextMsduInfo;
            }
        }
    }

    /* indicate service thread for sending */
    if(prTxCtrl->i4TxMgmtPendingNum > 0
            || kalGetTxPendingFrameCount(prAdapter->prGlueInfo) > 0) {
        kalSetEvent(prAdapter->prGlueInfo);
    }

    return u4Status;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief this function returns available count in command queue
*
* @param prAdapter      Pointer to the Adapter structure.
*
* @retval
*/
/*----------------------------------------------------------------------------*/
UINT_32
nicTxGetFreeCmdCount (
    IN P_ADAPTER_T  prAdapter
    )
{
    ASSERT(prAdapter);

    return prAdapter->rFreeCmdList.u4NumElem;
}

