/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/nic/nic.c#1 $
*/

/*! \file   nic.c
    \brief  Functions that provide operation in NIC's (Network Interface Card) point of view.

    This file includes functions which unite multiple hal(Hardware) operations
    and also take the responsibility of Software Resource Management in order
    to keep the synchronization with Hardware Manipulation.
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
** $Log: nic.c $
** 
** 09 04 2012 cp.wu
** [WCXRP00001269] [MT6620 Wi-Fi][Driver] cfg80211 porting merge back to DaVinci
** sync RSSI ignoring when BSS is disconnected
** 
** 08 24 2012 cp.wu
** [WCXRP00001269] [MT6620 Wi-Fi][Driver] cfg80211 porting merge back to DaVinci
** .
** 
** 08 24 2012 cp.wu
** [WCXRP00001269] [MT6620 Wi-Fi][Driver] cfg80211 porting merge back to DaVinci
** cfg80211 support merge back from ALPS.JB to DaVinci - MT6620 Driver v2.3 branch.
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Compile no error before trial run.
 *
 * 05 02 2012 terry.wu
 * NULL
 * Set the default value of AP StaRec index to "STA_REC_INDEX_NOT_FOUND" in update firmware bss command.
 *
 * 03 02 2012 terry.wu
 * NULL
 * Sync CFG80211 modification from branch 2,2.
 *
 * 11 28 2011 cp.wu
 * [WCXRP00001125] [MT6620 Wi-Fi][Firmware] Strengthen Wi-Fi power off sequence to have a clearroom environment when returining to ROM code
 * 1. Due to firmware now stops HIF DMA for powering off, do not try to receive any packet from firmware
 * 2. Take use of prAdapter->fgIsEnterD3ReqIssued for tracking whether it is powering off or not
 *
 * 11 22 2011 tsaiyuan.hsu
 * [WCXRP00001083] [MT6620 Wi-Fi][DRV]] dump debug counter or frames when debugging is triggered
 * keep debug counter setting after wake up.
 *
 * 11 19 2011 yuche.tsai
 * NULL
 * Update RSSI for P2P.
 *
 * 11 18 2011 yuche.tsai
 * NULL
 * CONFIG P2P support RSSI query, default turned off.
 *
 * 11 07 2011 tsaiyuan.hsu
 * [WCXRP00001083] [MT6620 Wi-Fi][DRV]] dump debug counter or frames when debugging is triggered
 * add debug counters and periodically dump counters for debugging.
 *
 * 11 04 2011 cp.wu
 * [WCXRP00001079] [MT5931][Driver] Release pending MMPDU only when BSS is being deactivated
 * pre-check for NULL before calling MMPDU free function
 *
 * 11 03 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * change the DBGLOG for "\n" and "\r\n". LABEL to LOUD for XLOG
 *
 * 11 01 2011 chinglan.wang
 * NULL
 * Modify the Wi-Fi method of the flush TX queue when disconnect the AP.
 * If disconnect the AP and flush all the data frame in the TX queue, WPS cannot do the 4-way handshake to connect to the AP..
 *
 * 10 11 2011 terry.wu
 * NULL
 * Rewrite Assert Dump Function for Portability.
 *
 * 09 20 2011 cm.chang
 * [WCXRP00000997] [MT6620 Wi-Fi][Driver][FW] Handle change of BSS preamble type and slot time
 * New CMD definition about RLM parameters
 *
 * 08 15 2011 cp.wu
 * [WCXRP00000851] [MT6628 Wi-Fi][Driver] Add HIFSYS related definition to driver source tree
 * reuse firmware download logic of MT6620 for MT6628.
 *
 * 08 03 2011 terry.wu
 * [WCXRP00000899] [MT6620] [FW] Reply probe rsp in FW for hotspot mode
 * Reply Probe Rsp in FW for Hotspot Mode.
 *
 *
 *
 * 08 03 2011 terry.wu
 * [WCXRP00000899] [MT6620] [FW] Reply probe rsp in FW for hotspot mode
 * Reply Probe Rsp in FW for Hotspot Mode.
 *
 *
 * 08 03 2011 terry.wu
 * [WCXRP00000899] [MT6620] [FW] Reply probe rsp in FW for hotspot mode
 * Reply Probe Rsp in FW for Hotspot Mode.
 *
 * 08 03 2011 terry.wu
 * [WCXRP00000899] [MT6620] [FW] Reply probe rsp in FW for hotspot mode
 * Reply Probe Rsp in FW for Hotspot Mode.
 *
 * 08 02 2011 yuche.tsai
 * [WCXRP00000896] [Volunteer Patch][WiFi Direct][Driver] GO with multiple client, TX deauth to a disconnecting device issue.
 * Fix GO send deauth frame issue.
 *
 * 07 18 2011 chinghwa.yu
 * [WCXRP00000063] Update BCM CoEx design and settings[WCXRP00000612] [MT6620 Wi-Fi] [FW] CSD update SWRDD algorithm
 * Add CMD/Event for RDD and BWCS.
 *
 * 07 11 2011 wh.su
 * [WCXRP00000849] [MT6620 Wi-Fi][Driver] Remove some of the WAPI define for make sure the value is initialize, for customer not enable WAPI
 * For make sure wapi initial value is set.
 *
 * 06 27 2011 cp.wu
 * [WCXRP00000815] [MT6620 Wi-Fi][Driver] allow single BSSID with multiple SSID settings to work around some tricky AP which use space character as hidden SSID
 * 1. correct logic
 * 2. replace only BSS-DESC which doesn't have a valid SSID.
 *
 * 06 27 2011 cp.wu
 * [WCXRP00000815] [MT6620 Wi-Fi][Driver] allow single BSSID with multiple SSID settings to work around some tricky AP which use space character as hidden SSID
 * allow to have a single BSSID with multiple SSID to be presented in scanning result
 *
 * 05 12 2011 puff.wen
 * NULL
 * FW Assert information dump to driver
 *
 * 04 18 2011 terry.wu
 * [WCXRP00000660] [MT6620 Wi-Fi][Driver] Remove flag CFG_WIFI_DIRECT_MOVED
 * Remove flag CFG_WIFI_DIRECT_MOVED.
 *
 * 04 15 2011 cp.wu
 * [WCXRP00000651] [MT6620 Wi-Fi][Driver] Refine RSSI buffering mechanism
 * ROLLBACK due to the special design is to workaround incorrect initial RCPI value coming from firmware domain.
 *
 * 04 14 2011 cm.chang
 * [WCXRP00000634] [MT6620 Wi-Fi][Driver][FW] 2nd BSS will not support 40MHz bandwidth for concurrency
 * .
 *
 * 04 14 2011 cp.wu
 * [WCXRP00000598] [MT6620 Wi-Fi][Driver] Implementation of interface for communicating with user space process for RESET_START and RESET_END events
 * 1. add code to put whole-chip resetting trigger when abnormal firmware assertion is detected
 * 2. add dummy function for both Win32 and Linux part.
 *
 * 04 12 2011 cp.wu
 * [WCXRP00000635] [MT6620 Wi-Fi][Driver] Clear pending security frames when QM clear pending data frames for dedicated network type
 * clear pending security frames for dedicated network type when BSS is being deactivated/disconnected
 *
 * 04 12 2011 wh.su
 * NULL
 * enable the p2p check the cipher to set the bssInfo auth mode.
 *
 * 04 12 2011 wh.su
 * NULL
 * prepare the code for sync the auth mode and encryption status for P2P and BOW.
 *
 * 04 11 2011 yuche.tsai
 * [WCXRP00000627] [Volunteer Patch][MT6620][Driver] Pending MMPUD of P2P Network may crash system issue.
 * Fix kernel panic issue when MMPDU of P2P is pending in driver.
 *
 * 04 10 2011 george.huang
 * [WCXRP00000621] [MT6620 Wi-Fi][Driver] Support P2P supplicant to set power mode
 * Fix compiler issue.
 *
 * 04 08 2011 george.huang
 * [WCXRP00000621] [MT6620 Wi-Fi][Driver] Support P2P supplicant to set power mode
 * separate settings of P2P and AIS
 *
 * 04 08 2011 eddie.chen
 * [WCXRP00000617] [MT6620 Wi-Fi][DRV/FW] Fix for sigma
 * Fix for sigma
 *
 * 04 07 2011 cp.wu
 * [WCXRP00000616] [MT6620 Wi-Fi][Driver] Free memory to pool and kernel in case any unexpected failure happend inside wlanAdapterStart
 * .
 *
 * 04 07 2011 cp.wu
 * [WCXRP00000616] [MT6620 Wi-Fi][Driver] Free memory to pool and kernel in case any unexpected failure happend inside wlanAdapterStart
 * implementation of internal error handling of nicAllocateAdapterMemory.
 *
 * 03 31 2011 chinglan.wang
 * [WCXRP00000613] [MT6620 Wi-Fi] [FW] [Driver] BssInfo can get the security mode which is WPA/WPA2/WAPI or not.
 * .
 *
 * 03 21 2011 cp.wu
 * [WCXRP00000540] [MT5931][Driver] Add eHPI8/eHPI16 support to Linux Glue Layer
 * portability improvement
 *
 * 03 17 2011 cp.wu
 * [WCXRP00000562] [MT6620 Wi-Fi][Driver] I/O buffer pre-allocation to avoid physically continuous memory shortage after system running for a long period
 * use pre-allocated buffer for storing enhanced interrupt response as well
 *
 * 03 16 2011 cp.wu
 * [WCXRP00000562] [MT6620 Wi-Fi][Driver] I/O buffer pre-allocation to avoid physically continuous memory shortage after system running for a long period
 * 1. pre-allocate physical continuous buffer while module is being loaded
 * 2. use pre-allocated physical continuous buffer for TX/RX DMA transfer
 *
 * The windows part remained the same as before, but added similiar APIs to hide the difference.
 *
 * 03 15 2011 cp.wu
 * [WCXRP00000559] [MT6620 Wi-Fi][Driver] Combine TX/RX DMA buffers into a single one to reduce physically continuous memory consumption
 * 1. deprecate CFG_HANDLE_IST_IN_SDIO_CALLBACK
 * 2. Use common coalescing buffer for both TX/RX directions
 *
 *
 * 03 10 2011 cm.chang
 * [WCXRP00000358] [MT6620 Wi-Fi][Driver] Provide concurrent information for each module
 * Add some functions to let AIS/Tethering or AIS/BOW be the same channel
 *
 * 03 07 2011 terry.wu
 * [WCXRP00000521] [MT6620 Wi-Fi][Driver] Remove non-standard debug message
 * Toggle non-standard debug messages to comments.
 *
 * 03 02 2011 cp.wu
 * [WCXRP00000503] [MT6620 Wi-Fi][Driver] Take RCPI brought by association response as initial RSSI right after connection is built.
 * use RCPI brought by ASSOC-RESP after connection is built as initial RCPI to avoid using a uninitialized MAC-RX RCPI.
 *
 * 02 08 2011 terry.wu
 * [WCXRP00000412] [MT6620 Wi-Fi][FW/Driver] Dump firmware assert info at android kernel log
 * Use kalPrint to print firmware assert info.
 *
 * 02 01 2011 terry.wu
 * [WCXRP00000412] [MT6620 Wi-Fi][FW/Driver] Dump firmware assert info at android kernel log
 * .
 *
 * 02 01 2011 cm.chang
 * [WCXRP00000415] [MT6620 Wi-Fi][Driver] Check if any memory leakage happens when uninitializing in DGB mode
 * .
 *
 * 01 31 2011 terry.wu
 * [WCXRP00000412] [MT6620 Wi-Fi][FW/Driver] Dump firmware assert info at android kernel log
 * Print firmware ASSERT info at Android kernel log, driver side
 *
 * 01 27 2011 tsaiyuan.hsu
 * [WCXRP00000392] [MT6620 Wi-Fi][Driver] Add Roaming Support
 * add roaming fsm
 * 1. not support 11r, only use strength of signal to determine roaming.
 * 2. not enable CFG_SUPPORT_ROAMING until completion of full test.
 * 3. in 6620, adopt work-around to avoid sign extension problem of cck of hw
 * 4. assume that change of link quality in smooth way.
 *
 * 01 26 2011 cm.chang
 * [WCXRP00000395] [MT6620 Wi-Fi][Driver][FW] Search STA_REC with additional net type index argument
 * Allocate system RAM if fixed message or mgmt buffer is not available
 *
 * 01 24 2011 cp.wu
 * [WCXRP00000382] [MT6620 Wi-Fi][Driver] Track forwarding packet number with notifying tx thread for serving
 * 1. add an extra counter for tracking pending forward frames.
 * 2. notify TX service thread as well when there is pending forward frame
 * 3. correct build errors leaded by introduction of Wi-Fi direct separation module
 *
 * 01 19 2011 cp.wu
 * [WCXRP00000372] [MT6620 Wi-Fi][Driver] Check bus access failure inside nicProcessIST()
 * check bus error and/or card removal when retrieving interrupt status from HAL
 *
 * 01 04 2011 cp.wu
 * [WCXRP00000338] [MT6620 Wi-Fi][Driver] Separate kalMemAlloc into kmalloc and vmalloc implementations to ease physically continous memory demands
 * 1) correct typo in scan.c
 * 2) TX descriptors, RX descriptos and management buffer should use virtually continous buffer instead of physically contineous one
 *
 * 01 04 2011 cp.wu
 * [WCXRP00000338] [MT6620 Wi-Fi][Driver] Separate kalMemAlloc into kmalloc and vmalloc implementations to ease physically continous memory demands
 * separate kalMemAlloc() into virtually-continous and physically-continous type to ease slab system pressure
 *
 * 12 30 2010 cp.wu
 * [WCXRP00000327] [MT6620 Wi-Fi][Driver] Improve HEC WHQA 6972 workaround coverage in driver side
 * host driver not to set FW-own when there is still pending interrupts
 *
 * 12 17 2010 cp.wu
 * [WCXRP00000270] [MT6620 Wi-Fi][Driver] Clear issues after concurrent networking support has been merged
 * before BSS disconnection is indicated to firmware, all correlated peer should be cleared and freed
 *
 * 12 07 2010 cm.chang
 * [WCXRP00000239] MT6620 Wi-Fi][Driver][FW] Merge concurrent branch back to maintrunk
 * 1. BSSINFO include RLM parameter
 * 2. free all sta records when network is disconnected
 *
 * 12 02 2010 eddie.chen
 * [WCXRP00000218] [MT6620 Wi-Fi][Driver] Add auto rate window control in registry
 * Add more control value but dont use it now.
 *
 * 11 30 2010 eddie.chen
 * [WCXRP00000218] [MT6620 Wi-Fi][Driver] Add auto rate window control in registry
 * Add auto rate check window in registry
 *
 * 11 10 2010 eddie.chen
 * [WCXRP00000156] [MT6620][FW] Change Auto rate window to 64 and add throughput swcr
 * Use autorate parameter 1 as phy rate mask.
 *
 * 11 08 2010 cp.wu
 * [WCXRP00000166] [MT6620 Wi-Fi][Driver] use SDIO CMD52 for enabling/disabling interrupt to reduce transaction period
 * change to use CMD52 for enabling/disabling interrupt to reduce SDIO transaction time
 *
 * 10 26 2010 eddie.chen
 * [WCXRP00000134] [MT6620 Wi-Fi][Driver] Add a registry to enable auto rate for SQA test by using E1 EVB
 * Add auto rate parameter in registry.
 *
 * 10 18 2010 cp.wu
 * [WCXRP00000053] [MT6620 Wi-Fi][Driver] Reset incomplete and might leads to BSOD when entering RF test with AIS associated
 * 1. remove redundant variables in STA_REC structure
 * 2. add STA-REC uninitialization routine for clearing pending events
 *
 * 10 18 2010 cp.wu
 * [WCXRP00000103] [MT6620 Wi-Fi][Driver] Driver crashed when using WZC to connect to AP#B with connection with AP#A
 * reset ptrs when IEs are going to be dropped
 *
 * 10 12 2010 cp.wu
 * [WCXRP00000084] [MT6620 Wi-Fi][Driver][FW] Add fixed rate support for distance test
 * add HT (802.11n) fixed rate support.
 *
 * 10 08 2010 cp.wu
 * [WCXRP00000084] [MT6620 Wi-Fi][Driver][FW] Add fixed rate support for distance test
 * adding fixed rate support for distance test. (from registry setting)
 *
 * 10 07 2010 cp.wu
 * [WCXRP00000083] [MT5931][Driver][FW] Add necessary logic for MT5931 first connection
 * add firmware download for MT5931.
 *
 * 10 05 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check
 * load manufacture data when CFG_SUPPORT_NVRAM is set to 1
 *
 * 10 04 2010 cp.wu
 * [WCXRP00000077] [MT6620 Wi-Fi][Driver][FW] Eliminate use of ENUM_NETWORK_TYPE_T and replaced by ENUM_NETWORK_TYPE_INDEX_T only
 * remove ENUM_NETWORK_TYPE_T definitions
 *
 * 09 21 2010 cp.wu
 * [WCXRP00000053] [MT6620 Wi-Fi][Driver] Reset incomplete and might leads to BSOD when entering RF test with AIS associated
 * Do a complete reset with STA-REC null checking for RF test re-entry
 *
 * 09 08 2010 cp.wu
 * NULL
 * use static memory pool for storing IEs of scanning result.
 *
 * 09 06 2010 cp.wu
 * NULL
 * Androi/Linux: return current operating channel information
 *
 * 09 01 2010 cp.wu
 * NULL
 * HIFSYS Clock Source Workaround
 *
 * 08 26 2010 yuche.tsai
 * NULL
 * Fix someones coding error while enable WIFI_DIRECT.
 *
 * 08 25 2010 george.huang
 * NULL
 * update OID/ registry control path for PM related settings
 *
 * 08 24 2010 cm.chang
 * NULL
 * Support RLM initail channel of Ad-hoc, P2P and BOW
 *
 * 08 24 2010 chinghwa.yu
 * NULL
 * Update BOW for the 1st time.
 *
 * 08 23 2010 chinghwa.yu
 * NULL
 * Update for BOW.
 *
 * 08 20 2010 yuche.tsai
 * NULL
 * Add state change indication.
 *
 * 08 16 2010 yuche.tsai
 * NULL
 * Add support for P2P BSS update info.
 *
 * 08 12 2010 cp.wu
 * NULL
 * [removing debugging] not to dump beacon content.
 *
 * 08 12 2010 cp.wu
 * NULL
 * [AIS-FSM] honor registry setting for adhoc running mode. (A/B/G)
 *
 * 08 11 2010 cp.wu
 * NULL
 * 1) do not use in-stack variable for beacon updating. (for MAUI porting)
 * 2) extending scanning result to 64 instead of 48
 *
 * 08 04 2010 yarco.yang
 * NULL
 * Add TX_AMPDU and ADDBA_REJECT command
 *
 * 08 03 2010 cp.wu
 * NULL
 * surpress compilation warning.
 *
 * 08 03 2010 cp.wu
 * NULL
 * Centralize mgmt/system service procedures into independent calls.
 *
 * 07 28 2010 cp.wu
 * NULL
 * 1) eliminate redundant variable eOPMode in prAdapter->rWlanInfo
 * 2) change nicMediaStateChange() API prototype
 *
 * 07 28 2010 cp.wu
 * NULL
 * sync. CMD_BSS_INFO structure change to CMD-EVENT v0.15.
 *
 * 07 24 2010 wh.su
 *
 * .support the Wi-Fi RSN
 *
 * 07 23 2010 cp.wu
 *
 * FIX: structure of CMD_SET_BSS_INFO has been changed but no follow-ups are done.
 *
 * 07 22 2010 george.huang
 *
 * .
 *
 * 07 22 2010 george.huang
 *
 * Update fgIsQoS information in BSS INFO by CMD
 *
 * 07 19 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * Add Ad-Hoc support to AIS-FSM
 *
 * 07 14 2010 yarco.yang
 *
 * 1. Remove CFG_MQM_MIGRATION
 * 2. Add CMD_UPDATE_WMM_PARMS command
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 07 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * update prStaRecOfAP with BSS-INFO.
 *
 * 07 06 2010 george.huang
 * [WPD00001556]Basic power managemenet function
 * Update arguments for nicUpdateBeaconIETemplate()
 *
 * 07 06 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * STA-REC is maintained by CNM only.
 *
 * 07 05 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) ignore RSN checking when RSN is not turned on.
 * 2) set STA-REC deactivation callback as NULL
 * 3) add variable initialization API based on PHY configuration
 *
 * 07 01 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Support sync command of STA_REC
 *
 * 06 30 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * sync. with CMD/EVENT document ver0.07.
 *
 * 06 29 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * correct variable naming for 8-bit variable used in CMD_BEACON_TEMPLATE_UPDATE.
 *
 * 06 29 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) sync to. CMD/EVENT document v0.03
 * 2) simplify DTIM period parsing in scan.c only, bss.c no longer parses it again.
 * 3) send command packet to indicate FW-PM after
 *     a) 1st beacon is received after AIS has connected to an AP
 *     b) IBSS-ALONE has been created
 *     c) IBSS-MERGE has occured
 *
 * 06 25 2010 george.huang
 * [WPD00001556]Basic power managemenet function
 * Create beacon update path, with expose bssUpdateBeaconContent()
 *
 * 06 22 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * fill fgIsUapsdConnection when indicating BSS-CREATE with AIS-STA mode.
 *
 * 06 22 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) add command warpper for STA-REC/BSS-INFO sync.
 * 2) enhance command packet sending procedure for non-oid part
 * 3) add command packet definitions for STA-REC/BSS-INFO sync.
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * implement TX_DONE callback path.
 *
 * 06 21 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * remove duplicate variable for migration.
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * TX descriptors are now allocated once for reducing allocation overhead
 *
 * 06 18 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Provide cnmMgtPktAlloc() and alloc/free function of msg/buf
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
 * 1) eliminate CFG_CMD_EVENT_VERSION_0_9
 * 2) when disconnected, indicate nic directly (no event is needed)
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 04 26 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) surpress compiler warning
 * 2) when acqruing LP-own, keep writing WHLPCR whenever OWN is not acquired yet
 *
 * 04 23 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * surpress compiler warning
 *
 * 04 13 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support
 * add framework for BT-over-Wi-Fi support.
 *  *  *  *  *  *  *  *  *  *  *  *  * 1) prPendingCmdInfo is replaced by queue for multiple handler capability
 *  *  *  *  *  *  *  *  *  *  *  *  * 2) command sequence number is now increased atomically
 *  *  *  *  *  *  *  *  *  *  *  *  * 3) private data could be hold and taken use for other purpose
 *
 * 04 12 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add channel frequency <-> number conversion
 *
 * 04 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * eliminate direct access for prGlueInfo->fgIsCardRemoved in non-glue layer
 *
 * 04 06 2010 cp.wu
 * [WPD00003827][MT6620 Wi-Fi] Chariot fail and following ping fail, no pkt send from driver
 * correct nicProcessIST_impl() for interrupt status brought up by RX enhanced response
 *
 * 03 19 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) add ACPI D0/D3 state switching support
 *  *  *  *  *  *  * 2) use more formal way to handle interrupt when the status is retrieved from enhanced RX response
 *
 * 03 17 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * always process TX interrupt first then RX interrupt.
 *
 * 02 25 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * correct behavior to prevent duplicated RX handling for RX0_DONE and RX1_DONE
 *
 * 02 23 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add checksum offloading support.
**  \main\maintrunk.MT6620WiFiDriver_Prj\27 2009-12-16 18:03:43 GMT mtk02752
**  handling enhanced response which fields are fetched at different moments
**  \main\maintrunk.MT6620WiFiDriver_Prj\26 2009-12-15 17:00:29 GMT mtk02752
**  if RX enhanced response is used, D2H interrupt status should be coming from buffered result as well
**  \main\maintrunk.MT6620WiFiDriver_Prj\25 2009-12-15 12:01:55 GMT mtk02752
**  if TX_DONE bit is not set but WTSR0/WTSR1 is non-zero, then set TX_DONE bit due to time latency of interrupt status enhanced response
**  \main\maintrunk.MT6620WiFiDriver_Prj\24 2009-12-10 16:52:52 GMT mtk02752
**  code clean
**  \main\maintrunk.MT6620WiFiDriver_Prj\23 2009-11-24 20:51:01 GMT mtk02752
**  integrate with SD1 by invoking qmHandleMailboxRxMessage()
**  \main\maintrunk.MT6620WiFiDriver_Prj\22 2009-11-16 17:32:33 GMT mtk02752
**  prepare code for invoking rxHandleMailboxRxMessage()
**  \main\maintrunk.MT6620WiFiDriver_Prj\21 2009-11-11 10:36:08 GMT mtk01084
**  \main\maintrunk.MT6620WiFiDriver_Prj\20 2009-11-09 22:56:41 GMT mtk01084
**  modify HW access routines
**  \main\maintrunk.MT6620WiFiDriver_Prj\19 2009-10-30 18:17:20 GMT mtk01084
**  prevent warning
**  \main\maintrunk.MT6620WiFiDriver_Prj\18 2009-10-29 19:54:57 GMT mtk01084
**  init HIF
**  \main\maintrunk.MT6620WiFiDriver_Prj\17 2009-10-23 16:08:30 GMT mtk01084
**  \main\maintrunk.MT6620WiFiDriver_Prj\16 2009-10-13 21:59:12 GMT mtk01084
**  update for new HW design
**  \main\maintrunk.MT6620WiFiDriver_Prj\15 2009-09-09 17:26:15 GMT mtk01084
**  modify for CFG_TEST_WITH_MT5921
**  \main\maintrunk.MT6620WiFiDriver_Prj\14 2009-05-19 10:55:22 GMT mtk01461
**  Unmask the unused HISR
**  \main\maintrunk.MT6620WiFiDriver_Prj\13 2009-05-18 15:59:13 GMT mtk01084
**  remove debug purpose code
**  \main\maintrunk.MT6620WiFiDriver_Prj\12 2009-05-18 14:05:02 GMT mtk01084
**  update for WIFI ownback part on initial
**  \main\maintrunk.MT6620WiFiDriver_Prj\11 2009-05-04 21:32:57 GMT mtk01084
**  add temporarily code to set driver own on adapter initialization
**  \main\maintrunk.MT6620WiFiDriver_Prj\10 2009-04-28 10:35:41 GMT mtk01461
**  Add init of TX aggregation and fix RX STATUS is DW align for SDIO_STATUS_ENHANCE mode
**  \main\maintrunk.MT6620WiFiDriver_Prj\9 2009-04-24 21:12:10 GMT mtk01104
**  Add function nicRestoreSpiDefMode()
**  \main\maintrunk.MT6620WiFiDriver_Prj\8 2009-04-21 09:43:31 GMT mtk01461
**  Revise for MTK coding style - nicInitializeAdapter()
**  \main\maintrunk.MT6620WiFiDriver_Prj\7 2009-04-17 19:52:47 GMT mtk01461
**  Update allocate Adapter Memory for MGMT Memory pool
**  \main\maintrunk.MT6620WiFiDriver_Prj\6 2009-04-01 10:57:08 GMT mtk01461
**  Refine the order of release memory from pucRxCoalescingBufCached
**  \main\maintrunk.MT6620WiFiDriver_Prj\5 2009-03-19 18:32:57 GMT mtk01084
**  update for basic power management functions
**  \main\maintrunk.MT6620WiFiDriver_Prj\4 2009-03-18 21:00:14 GMT mtk01426
**  Add CFG_SDIO_RX_ENHANCE support
**  \main\maintrunk.MT6620WiFiDriver_Prj\3 2009-03-16 09:10:27 GMT mtk01461
**  Update TX PATH API
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-03-10 20:25:59 GMT mtk01426
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
const UINT_8 aucPhyCfg2PhyTypeSet[PHY_CONFIG_NUM] =
{
    PHY_TYPE_SET_802_11ABG,         /* PHY_CONFIG_802_11ABG */
    PHY_TYPE_SET_802_11BG,          /* PHY_CONFIG_802_11BG */
    PHY_TYPE_SET_802_11G,           /* PHY_CONFIG_802_11G */
    PHY_TYPE_SET_802_11A,           /* PHY_CONFIG_802_11A */
    PHY_TYPE_SET_802_11B,           /* PHY_CONFIG_802_11B */
    PHY_TYPE_SET_802_11ABGN,        /* PHY_CONFIG_802_11ABGN */
    PHY_TYPE_SET_802_11BGN,         /* PHY_CONFIG_802_11BGN */
    PHY_TYPE_SET_802_11AN,          /* PHY_CONFIG_802_11AN */
    PHY_TYPE_SET_802_11GN           /* PHY_CONFIG_802_11GN */
};

#if (MT6620_E1_ASIC_HIFSYS_WORKAROUND == 1)
    #define REQ_GATING_ENABLE_H2D_INT   BIT(31)
    #define REQ_GATING_DISABLE_H2D_INT  BIT(30)
    #define ACK_GATING_ENABLE_D2H_INT   BIT(31)
    #define ACK_GATING_DISABLE_D2H_INT  BIT(30)

    #define GATING_CONTROL_POLL_LIMIT   64
#endif

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

static INT_EVENT_MAP_T arIntEventMapTable[] = {
    {WHISR_ABNORMAL_INT,                        INT_EVENT_ABNORMAL},
    {WHISR_D2H_SW_INT,                          INT_EVENT_SW_INT},
    {WHISR_TX_DONE_INT,                         INT_EVENT_TX},
    {(WHISR_RX0_DONE_INT | WHISR_RX1_DONE_INT), INT_EVENT_RX}
};

static const UINT_8 ucIntEventMapSize = (sizeof(arIntEventMapTable) / sizeof(INT_EVENT_MAP_T));

static IST_EVENT_FUNCTION apfnEventFuncTable[] = {
    nicProcessAbnormalInterrupt,            /*!< INT_EVENT_ABNORMAL */
    nicProcessSoftwareInterrupt,            /*!< INT_EVENT_SW_INT   */
    nicProcessTxInterrupt,                  /*!< INT_EVENT_TX       */
    nicProcessRxInterrupt,                  /*!< INT_EVENT_RX       */
};
/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/
/*! This macro is used to reduce coding errors inside nicAllocateAdapterMemory()
 * and also enhance the readability.
 */
#define LOCAL_NIC_ALLOCATE_MEMORY(pucMem, u4Size, eMemType, pucComment) \
        { \
            DBGLOG(INIT, INFO, ("Allocating %ld bytes for %s.\n", u4Size, pucComment)); \
            if ((pucMem = (PUINT_8)kalMemAlloc(u4Size, eMemType)) == (PUINT_8)NULL) { \
                DBGLOG(INIT, ERROR, ("Could not allocate %ld bytes for %s.\n", u4Size, pucComment)); \
                break; \
            } \
            ASSERT(((UINT_32)pucMem % 4) == 0); \
            DBGLOG(INIT, INFO, ("Virtual Address = %08lx for %s.\n", (UINT_32)pucMem, pucComment)); \
        }



/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/


/*----------------------------------------------------------------------------*/
/*!
* @brief This routine is responsible for the allocation of the data structures
*        inside the Adapter structure, include:
*        1. SW_RFB_Ts
*        2. Common coalescing buffer for TX PATH.
*
* @param prAdapter Pointer of Adapter Data Structure
*
* @retval WLAN_STATUS_SUCCESS - Has enough memory.
* @retval WLAN_STATUS_RESOURCES - Memory is not enough.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicAllocateAdapterMemory (
    IN P_ADAPTER_T prAdapter
    )
{
    WLAN_STATUS status = WLAN_STATUS_RESOURCES;
    P_RX_CTRL_T prRxCtrl;
    P_TX_CTRL_T prTxCtrl;

    DEBUGFUNC("nicAllocateAdapterMemory");

    ASSERT(prAdapter);
    prRxCtrl = &prAdapter->rRxCtrl;
    prTxCtrl = &prAdapter->rTxCtrl;

    do {
        //4 <0> Reset all Memory Handler
    #if CFG_DBG_MGT_BUF
        prAdapter->u4MemFreeDynamicCount = 0;
        prAdapter->u4MemAllocDynamicCount = 0;
    #endif
        prAdapter->pucMgtBufCached = (PUINT_8)NULL;
        prRxCtrl->pucRxCached = (PUINT_8)NULL;
        prAdapter->prSDIOCtrl = (P_SDIO_CTRL_T)NULL;


        //4 <1> Memory for Management Memory Pool and CMD_INFO_T
        /* Allocate memory for the CMD_INFO_T and its MGMT memory pool. */
        prAdapter->u4MgtBufCachedSize = MGT_BUFFER_SIZE;

        LOCAL_NIC_ALLOCATE_MEMORY(prAdapter->pucMgtBufCached,
                                  prAdapter->u4MgtBufCachedSize,
                                  VIR_MEM_TYPE,
                                  "COMMON MGMT MEMORY POOL");

        //4 <2> Memory for RX Descriptor
        /* Initialize the number of rx buffers we will have in our queue. */
        /* <TODO> We may setup ucRxPacketDescriptors by GLUE Layer, and using
         * this variable directly.
         */
        /* Allocate memory for the SW receive structures. */
        prRxCtrl->u4RxCachedSize = CFG_RX_MAX_PKT_NUM * \
                                   ALIGN_4(sizeof(SW_RFB_T));

        LOCAL_NIC_ALLOCATE_MEMORY(prRxCtrl->pucRxCached,
                                  prRxCtrl->u4RxCachedSize,
                                  VIR_MEM_TYPE,
                                  "SW_RFB_T");

        //4 <3> Memory for TX DEscriptor
        prTxCtrl->u4TxCachedSize = CFG_TX_MAX_PKT_NUM * \
                                   ALIGN_4(sizeof(MSDU_INFO_T));

        LOCAL_NIC_ALLOCATE_MEMORY(prTxCtrl->pucTxCached,
                                  prTxCtrl->u4TxCachedSize,
                                  VIR_MEM_TYPE,
                                  "MSDU_INFO_T");

        //4 <4> Memory for Common Coalescing Buffer
#if CFG_COALESCING_BUFFER_SIZE || CFG_SDIO_RX_AGG
        prAdapter->pucCoalescingBufCached = (PUINT_8)NULL;

        /* Allocate memory for the common coalescing buffer. */
        prAdapter->u4CoalescingBufCachedSize = CFG_COALESCING_BUFFER_SIZE > CFG_RX_COALESCING_BUFFER_SIZE ?
            CFG_COALESCING_BUFFER_SIZE : CFG_RX_COALESCING_BUFFER_SIZE;

        prAdapter->pucCoalescingBufCached =
            kalAllocateIOBuffer(prAdapter->u4CoalescingBufCachedSize);

        if(prAdapter->pucCoalescingBufCached == NULL) {
            DBGLOG(INIT, ERROR, ("Could not allocate %ld bytes for coalescing buffer.\n", prAdapter->u4CoalescingBufCachedSize));
            break;
        }
#endif /* CFG_COALESCING_BUFFER_SIZE */

        //4 <5> Memory for enhanced interrupt response
        prAdapter->prSDIOCtrl = (P_SDIO_CTRL_T)
            kalAllocateIOBuffer(sizeof(ENHANCE_MODE_DATA_STRUCT_T));

        if(prAdapter->prSDIOCtrl == NULL) {
            DBGLOG(INIT, ERROR, ("Could not allocate %ld bytes for interrupt response.\n", sizeof(ENHANCE_MODE_DATA_STRUCT_T)));
            break;
        }

        status = WLAN_STATUS_SUCCESS;

    }
    while(FALSE);

    if(status != WLAN_STATUS_SUCCESS) {
        nicReleaseAdapterMemory(prAdapter);
    }

    return status;

}   /* end of nicAllocateAdapterMemory() */


/*----------------------------------------------------------------------------*/
/*!
* @brief This routine is responsible for releasing the allocated memory by
*        nicAllocatedAdapterMemory().
*
* @param prAdapter Pointer of Adapter Data Structure
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicReleaseAdapterMemory (
    IN P_ADAPTER_T prAdapter
    )
{
    P_TX_CTRL_T prTxCtrl;
    P_RX_CTRL_T prRxCtrl;

    ASSERT(prAdapter);
    prTxCtrl = &prAdapter->rTxCtrl;
    prRxCtrl = &prAdapter->rRxCtrl;

    //4 <5> Memory for enhanced interrupt response
    if (prAdapter->prSDIOCtrl) {
        kalReleaseIOBuffer((PVOID)prAdapter->prSDIOCtrl, sizeof(ENHANCE_MODE_DATA_STRUCT_T));
        prAdapter->prSDIOCtrl = (P_SDIO_CTRL_T)NULL;
    }

    //4 <4> Memory for Common Coalescing Buffer
#if CFG_COALESCING_BUFFER_SIZE || CFG_SDIO_RX_AGG
    if (prAdapter->pucCoalescingBufCached) {
        kalReleaseIOBuffer((PVOID)prAdapter->pucCoalescingBufCached, prAdapter->u4CoalescingBufCachedSize);
        prAdapter->pucCoalescingBufCached = (PUINT_8)NULL;
    }
#endif /* CFG_COALESCING_BUFFER_SIZE */

    //4 <3> Memory for TX Descriptor
    if (prTxCtrl->pucTxCached) {
        kalMemFree((PVOID)prTxCtrl->pucTxCached,
                   VIR_MEM_TYPE,
                   prTxCtrl->u4TxCachedSize);
        prTxCtrl->pucTxCached = (PUINT_8)NULL;
    }

    //4 <2> Memory for RX Descriptor
    if (prRxCtrl->pucRxCached) {
        kalMemFree((PVOID)prRxCtrl->pucRxCached,
                   VIR_MEM_TYPE,
                   prRxCtrl->u4RxCachedSize);
        prRxCtrl->pucRxCached = (PUINT_8)NULL;
    }

    //4 <1> Memory for Management Memory Pool
    if (prAdapter->pucMgtBufCached) {
        kalMemFree((PVOID)prAdapter->pucMgtBufCached,
                   VIR_MEM_TYPE,
                   prAdapter->u4MgtBufCachedSize);
        prAdapter->pucMgtBufCached = (PUINT_8)NULL;
    }

#if CFG_DBG_MGT_BUF
    /* Check if all allocated memories are free */
    ASSERT(prAdapter->u4MemFreeDynamicCount ==
           prAdapter->u4MemAllocDynamicCount);
#endif

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief disable global interrupt
*
* @param prAdapter pointer to the Adapter handler
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicDisableInterrupt (
    IN P_ADAPTER_T prAdapter
    )
{
    ASSERT(prAdapter);

    HAL_BYTE_WR(prAdapter, MCR_WHLPCR, WHLPCR_INT_EN_CLR);

    prAdapter->fgIsIntEnable = FALSE;
}

/*----------------------------------------------------------------------------*/
/*!
* @brief enable global interrupt
*
* @param prAdapter pointer to the Adapter handler
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicEnableInterrupt (
    IN P_ADAPTER_T prAdapter
    )
{
    BOOLEAN fgIsIntEnableCache;


    ASSERT(prAdapter);
    fgIsIntEnableCache = prAdapter->fgIsIntEnable;

    prAdapter->fgIsIntEnable = TRUE; // NOTE(Kevin): It must be placed before MCR GINT write.

    /* If need enable INT and also set LPOwn at the same time. */
    if (prAdapter->fgIsIntEnableWithLPOwnSet) {
        prAdapter->fgIsIntEnableWithLPOwnSet = FALSE; /* NOTE(Kevin): It's better to place it
                                                       * before MCR GINT write.
                                                       */
        /* If INT was enabled, only set LPOwn */
        if (fgIsIntEnableCache) {
            HAL_MCR_WR(prAdapter, MCR_WHLPCR, WHLPCR_FW_OWN_REQ_SET);
            prAdapter->fgIsFwOwn = TRUE;
        }
        /* If INT was not enabled, enable it and also set LPOwn now */
        else {
            HAL_MCR_WR(prAdapter, MCR_WHLPCR, WHLPCR_FW_OWN_REQ_SET |
                                              WHLPCR_INT_EN_SET);
            prAdapter->fgIsFwOwn = TRUE;
        }
    }
    /* If INT was not enabled, enable it now */
    else if (!fgIsIntEnableCache) {
        HAL_BYTE_WR(prAdapter, MCR_WHLPCR, WHLPCR_INT_EN_SET);
    }

    return;
} /* end of nicEnableInterrupt() */


#if CFG_SDIO_INTR_ENHANCE
/*----------------------------------------------------------------------------*/
/*!
* @brief For SDIO enhance mode, set the max rx len and tx status
*
* @param prAdapter      a pointer to adapter private data structure.
*
* @return  - none
*/
/*----------------------------------------------------------------------------*/
VOID
nicSDIOInit (
    IN P_ADAPTER_T prAdapter
    )
{
    UINT_32 u4Value = 0;

    ASSERT(prAdapter);

    //4 <1> Check STATUS Buffer is DW alignment.
    ASSERT( IS_ALIGN_4( (UINT_32)&prAdapter->prSDIOCtrl->u4WHISR ) );

    //4 <2> Setup STATUS count.
    {
        HAL_MCR_RD(prAdapter, MCR_WHCR, &u4Value);

        //4 <2.1> Setup the number of maximum RX length to be report
        u4Value &= ~(WHCR_MAX_HIF_RX_LEN_NUM);
        u4Value |= ((SDIO_MAXIMUM_RX_LEN_NUM << WHCR_OFFSET_MAX_HIF_RX_LEN_NUM));

        //4 <2.2> Setup RX enhancement mode
#if CFG_SDIO_RX_ENHANCE
        u4Value |= WHCR_RX_ENHANCE_MODE_EN;
#else
        u4Value &= ~WHCR_RX_ENHANCE_MODE_EN;
#endif /* CFG_SDIO_RX_AGG */

        HAL_MCR_WR(prAdapter, MCR_WHCR, u4Value);
    }

    return;

} /* end of nicSDIOInit() */

/*----------------------------------------------------------------------------*/
/*!
* @brief Read interrupt status from hardware
*
* @param prAdapter pointer to the Adapter handler
* @param the interrupts
*
* @return N/A
*
*/
/*----------------------------------------------------------------------------*/
VOID
nicSDIOReadIntStatus (
    IN P_ADAPTER_T prAdapter,
    OUT PUINT_32   pu4IntStatus
    )
{
    P_SDIO_CTRL_T prSDIOCtrl;

    DEBUGFUNC("nicSDIOReadIntStatus");

    ASSERT(prAdapter);
    ASSERT(pu4IntStatus);

    prSDIOCtrl = prAdapter->prSDIOCtrl;
    ASSERT(prSDIOCtrl);

    HAL_PORT_RD(prAdapter,
                MCR_WHISR,
                sizeof(ENHANCE_MODE_DATA_STRUCT_T),
                (PUINT_8)prSDIOCtrl,
                sizeof(ENHANCE_MODE_DATA_STRUCT_T));

    if(kalIsCardRemoved(prAdapter->prGlueInfo) == TRUE
            || fgIsBusAccessFailed == TRUE) {
        *pu4IntStatus = 0;
        return;
    }

    /* workaround */
    if((prSDIOCtrl->u4WHISR & WHISR_TX_DONE_INT) == 0 &&
            (prSDIOCtrl->rTxInfo.au4WTSR[0] | prSDIOCtrl->rTxInfo.au4WTSR[1])) {
        prSDIOCtrl->u4WHISR |= WHISR_TX_DONE_INT;
    }

    if((prSDIOCtrl->u4WHISR & BIT(31)) == 0 &&
            HAL_GET_MAILBOX_READ_CLEAR(prAdapter) == TRUE &&
            (prSDIOCtrl->u4RcvMailbox0 != 0 || prSDIOCtrl->u4RcvMailbox1 != 0)) {
        prSDIOCtrl->u4WHISR |= BIT(31);
    }

    *pu4IntStatus = prSDIOCtrl->u4WHISR;

    return;
} /* end of nicSDIOReadIntStatus() */
#endif

/*----------------------------------------------------------------------------*/
/*!
* @brief The function used to read interrupt status and then invoking
*        dispatching procedure for the appropriate functions
*        corresponding to specific interrupt bits
*
* @param prAdapter pointer to the Adapter handler
*
* @retval WLAN_STATUS_SUCCESS
* @retval WLAN_STATUS_ADAPTER_NOT_READY
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicProcessIST (
    IN P_ADAPTER_T prAdapter
    )
{
    WLAN_STATUS u4Status = WLAN_STATUS_SUCCESS;
    UINT_32 u4IntStatus = 0;
    UINT_32 i;

    DEBUGFUNC("nicProcessIST");
    //DBGLOG(INIT, LOUD, ("\n"));

    ASSERT(prAdapter);

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in set nicProcessIST! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }

#if (MT6620_E1_ASIC_HIFSYS_WORKAROUND == 1)
    if(prAdapter->fgIsClockGatingEnabled == TRUE) {
        nicDisableClockGating(prAdapter);
    }
#endif

    for (i = 0; i < CFG_IST_LOOP_COUNT; i++) {

#if CFG_SDIO_INTR_ENHANCE
        nicSDIOReadIntStatus(prAdapter, &u4IntStatus);
#else
        HAL_MCR_RD(prAdapter, MCR_WHISR, &u4IntStatus);
#endif /* CFG_SDIO_INTR_ENHANCE */

        //DBGLOG(INIT, TRACE, ("u4IntStatus: 0x%x\n", u4IntStatus));

        if (u4IntStatus & ~(WHIER_DEFAULT | WHIER_FW_OWN_BACK_INT_EN)) {
            DBGLOG(INTR, WARN, ("Un-handled HISR %#x, HISR = %#x (HIER:0x%x)\n",
                (u4IntStatus & ~WHIER_DEFAULT), u4IntStatus, WHIER_DEFAULT));
            u4IntStatus &= WHIER_DEFAULT;
        }

        nicProcessIST_impl(prAdapter, u4IntStatus);

        if(u4IntStatus == 0) {
            if(i == 0) {
                u4Status = WLAN_STATUS_NOT_INDICATING;
            }
            break;
        }
    }

#if (MT6620_E1_ASIC_HIFSYS_WORKAROUND == 1)
    if(prAdapter->fgIsClockGatingEnabled == FALSE) {
        nicEnableClockGating(prAdapter);
    }
#endif

    return u4Status;
} /* end of nicProcessIST() */


/*----------------------------------------------------------------------------*/
/*!
* @brief The function used to dispatch the appropriate functions for specific
*        interrupt bits
*
* @param prAdapter   pointer to the Adapter handler
*        u4IntStatus interrupt status bits
*
* @retval WLAN_STATUS_SUCCESS
* @retval WLAN_STATUS_ADAPTER_NOT_READY
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicProcessIST_impl (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4IntStatus
    )
{
    UINT_32 u4IntCount = 0;
    P_INT_EVENT_MAP_T prIntEventMap = NULL;

    ASSERT(prAdapter);

    prAdapter->u4IntStatus = u4IntStatus;

    /* Process each of the interrupt status consequently */
    prIntEventMap = &arIntEventMapTable[0];
    for (u4IntCount = 0; u4IntCount < ucIntEventMapSize; prIntEventMap++, u4IntCount++) {
        if (prIntEventMap->u4Int & prAdapter->u4IntStatus) {
            if(prIntEventMap->u4Event == INT_EVENT_RX &&
                    prAdapter->fgIsEnterD3ReqIssued == TRUE) {
                // ignore 
            }
            else if (apfnEventFuncTable[prIntEventMap->u4Event] != NULL) {
                apfnEventFuncTable[prIntEventMap->u4Event](prAdapter);
            }
            else {
                DBGLOG(INTR , WARN,
                        ("Empty INTR handler! ISAR bit#: %ld, event:%d, func: 0x%x\n",
                         prIntEventMap->u4Int, prIntEventMap->u4Event, apfnEventFuncTable[prIntEventMap->u4Event]));

                ASSERT(0); // to trap any NULL interrupt handler
            }
            prAdapter->u4IntStatus &= ~prIntEventMap->u4Int;
        }
    }

    return WLAN_STATUS_SUCCESS;
} /* end of nicProcessIST_impl() */


/*----------------------------------------------------------------------------*/
/*!
* @brief Verify the CHIP ID
*
* @param prAdapter      a pointer to adapter private data structure.
*
*
* @retval TRUE          CHIP ID is the same as the setting compiled
* @retval FALSE         CHIP ID is different from the setting compiled
*/
/*----------------------------------------------------------------------------*/
BOOL
nicVerifyChipID (
    IN P_ADAPTER_T prAdapter
    )
{
    UINT_32 u4CIR = 0;

    ASSERT(prAdapter);

    HAL_MCR_RD(prAdapter, MCR_WCIR, &u4CIR );

    DBGLOG(INIT, TRACE,("Chip ID: 0x%x\n", u4CIR & WCIR_CHIP_ID));
    DBGLOG(INIT, TRACE,("Revision ID: 0x%x\n", ((u4CIR & WCIR_REVISION_ID) >> 16)));

    if ((u4CIR & WCIR_CHIP_ID) != MTK_CHIP_REV) {
        return FALSE;
    }

    prAdapter->ucRevID = (UINT_8)(((u4CIR & WCIR_REVISION_ID) >> 16) & 0xF) ;

    return TRUE;
}

/*----------------------------------------------------------------------------*/
/*!
* @brief Initialize the MCR to the appropriate init value, and verify the init
*        value
*
* @param prAdapter      a pointer to adapter private data structure.
*
* @return -
*/
/*----------------------------------------------------------------------------*/
VOID
nicMCRInit (
    IN P_ADAPTER_T prAdapter
    )
{

    ASSERT(prAdapter);

    //4 <0> Initial value
}

VOID
nicHifInit (
    IN P_ADAPTER_T prAdapter
    )
{

    ASSERT(prAdapter);
#if 0
    /* reset event */
    nicPutMailbox(prAdapter, 0, 0x52455345); // RESE
    nicPutMailbox(prAdapter, 1, 0x545F5746); // T_WF
    nicSetSwIntr(prAdapter, BIT(16));
#endif
}


/*----------------------------------------------------------------------------*/
/*!
* @brief Initialize the Adapter soft variable
*
* @param prAdapter pointer to the Adapter handler
*
* @return (none)
*
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicInitializeAdapter (
    IN P_ADAPTER_T prAdapter
    )
{
    WLAN_STATUS u4Status = WLAN_STATUS_SUCCESS;

    ASSERT(prAdapter);

    prAdapter->fgIsIntEnableWithLPOwnSet = FALSE;

    do {
        if (!nicVerifyChipID(prAdapter)) {
            u4Status = WLAN_STATUS_FAILURE;
            break;
        }

        //4 <1> MCR init
        nicMCRInit(prAdapter);

    #if CFG_SDIO_INTR_ENHANCE
        nicSDIOInit(prAdapter);
    #endif /* CFG_SDIO_INTR_ENHANCE */

        HAL_MCR_WR(prAdapter, MCR_WHIER, WHIER_DEFAULT);


        //4 <2> init FW HIF
        nicHifInit(prAdapter);
    }
    while (FALSE);


    return u4Status;
}


#if defined(_HIF_SPI)
/*----------------------------------------------------------------------------*/
/*!
* \brief Restore the SPI Mode Select to default mode,
*        this is important while driver is unload, and this must be last mcr
*        since the operation will let the hif use 8bit mode access
*
* \param[in] prAdapter      a pointer to adapter private data structure.
* \param[in] eGPIO2_Mode    GPIO2 operation mode
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
void
nicRestoreSpiDefMode (
    IN P_ADAPTER_T prAdapter
    )
{
    ASSERT(prAdapter);

    HAL_MCR_WR(prAdapter, MCR_WCSR, SPICSR_8BIT_MODE_DATA);

}
#endif

/*----------------------------------------------------------------------------*/
/*!
* @brief Process rx interrupt. When the rx
*        Interrupt is asserted, it means there are frames in queue.
*
* @param prAdapter      Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicProcessAbnormalInterrupt (
    IN  P_ADAPTER_T prAdapter
    )
{
    UINT_32 u4Value;

    HAL_MCR_RD(prAdapter, MCR_WASR, &u4Value);
    DBGLOG(REQ, WARN, ("MCR_WASR: 0x%x \n", u4Value));
}

/*----------------------------------------------------------------------------*/
/*!
* @brief .
*
* @param prAdapter  Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicProcessFwOwnBackInterrupt(
    IN P_ADAPTER_T prAdapter
    )
{

    return;
} /* end of nicProcessFwOwnBackInterrupt() */

/*----------------------------------------------------------------------------*/
/*!
* @brief .
*
* @param prAdapter  Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicProcessSoftwareInterrupt(
    IN P_ADAPTER_T prAdapter
    )
{
    UINT_32 u4IntrBits;

    ASSERT(prAdapter);

    u4IntrBits = prAdapter->u4IntStatus & BITS(8, 31);

    if((u4IntrBits & WHISR_D2H_SW_ASSERT_INFO_INT) != 0) {
        nicPrintFirmwareAssertInfo(prAdapter);
#if CFG_CHIP_RESET_SUPPORT
        glSendResetRequest();
#endif
    }

#if (MT6620_E1_ASIC_HIFSYS_WORKAROUND == 1)
    ASSERT((u4IntrBits & (ACK_GATING_ENABLE_D2H_INT | ACK_GATING_DISABLE_D2H_INT))
            != (ACK_GATING_ENABLE_D2H_INT | ACK_GATING_DISABLE_D2H_INT));

    if(u4IntrBits & ACK_GATING_ENABLE_D2H_INT) {
        prAdapter->fgIsClockGatingEnabled = TRUE;
    }

    if(u4IntrBits & ACK_GATING_DISABLE_D2H_INT) {
        prAdapter->fgIsClockGatingEnabled = FALSE;

        // Indicate Service Thread for TX
        if(kalGetTxPendingCmdCount(prAdapter->prGlueInfo) > 0
                || wlanGetTxPendingFrameCount(prAdapter) > 0) {
            kalSetEvent(prAdapter->prGlueInfo);
        }
    }
#endif

    DBGLOG(REQ, WARN, ("u4IntrBits: 0x%x \n", u4IntrBits));

    return;
} /* end of nicProcessSoftwareInterrupt() */

VOID
nicPutMailbox (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4MailboxNum,
    IN UINT_32 u4Data
    )
{
    if (u4MailboxNum == 0) {
        HAL_MCR_WR(prAdapter, MCR_H2DSM0R, u4Data);
    } else if (u4MailboxNum == 1) {
        HAL_MCR_WR(prAdapter, MCR_H2DSM1R, u4Data);
    } else {
        ASSERT(0);
    }
}

VOID
nicGetMailbox (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4MailboxNum,
    OUT PUINT_32 pu4Data
    )
{
    if (u4MailboxNum == 0) {
        HAL_MCR_RD(prAdapter, MCR_D2HRM0R, pu4Data);
    } else if (u4MailboxNum == 1) {
        HAL_MCR_RD(prAdapter, MCR_D2HRM1R, pu4Data);
    } else {
        ASSERT(0);
    }
}

VOID
nicSetSwIntr (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4SwIntrBitmap
    )
{
    /* NOTE:
     *  SW interrup in HW bit 16 is mapping to SW bit 0 (shift 16bit in HW transparancy)
     *  SW interrupt valid from b0~b15
     */
    ASSERT((u4SwIntrBitmap & BITS(0, 15)) == 0);
//    DBGLOG(INIT, TRACE, ("u4SwIntrBitmap: 0x%08x\n", u4SwIntrBitmap));

    HAL_MCR_WR(prAdapter, MCR_WSICR, u4SwIntrBitmap);
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This procedure is used to dequeue from prAdapter->rPendingCmdQueue
*        with specified sequential number
*
* @param    prAdapter   Pointer of ADAPTER_T
*           ucSeqNum    Sequential Number
*
* @retval - P_CMD_INFO_T
*/
/*----------------------------------------------------------------------------*/
P_CMD_INFO_T
nicGetPendingCmdInfo (
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 ucSeqNum
    )
{
    P_QUE_T prCmdQue;
    QUE_T rTempCmdQue;
    P_QUE_T prTempCmdQue = &rTempCmdQue;
    P_QUE_ENTRY_T prQueueEntry = (P_QUE_ENTRY_T)NULL;
    P_CMD_INFO_T prCmdInfo = (P_CMD_INFO_T)NULL;

    GLUE_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);

    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_CMD_PENDING);

    prCmdQue = &prAdapter->rPendingCmdQueue;
    QUEUE_MOVE_ALL(prTempCmdQue, prCmdQue);

    QUEUE_REMOVE_HEAD(prTempCmdQue, prQueueEntry, P_QUE_ENTRY_T);
    while (prQueueEntry) {
        prCmdInfo = (P_CMD_INFO_T)prQueueEntry;

        if(prCmdInfo->ucCmdSeqNum == ucSeqNum)
            break;
        else {
            QUEUE_INSERT_TAIL(prCmdQue, prQueueEntry);

            prCmdInfo = NULL;
        }

        QUEUE_REMOVE_HEAD(prTempCmdQue, prQueueEntry, P_QUE_ENTRY_T);
    }
    QUEUE_CONCATENATE_QUEUES(prCmdQue, prTempCmdQue);

    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_CMD_PENDING);

    return prCmdInfo;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This procedure is used to dequeue from prAdapter->rTxCtrl.rTxMgmtTxingQueue
*        with specified sequential number
*
* @param    prAdapter   Pointer of ADAPTER_T
*           ucSeqNum    Sequential Number
*
* @retval - P_MSDU_INFO_T
*/
/*----------------------------------------------------------------------------*/
P_MSDU_INFO_T
nicGetPendingTxMsduInfo (
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 ucSeqNum
    )
{
    P_QUE_T prTxingQue;
    QUE_T rTempQue;
    P_QUE_T prTempQue = &rTempQue;
    P_QUE_ENTRY_T prQueueEntry = (P_QUE_ENTRY_T)NULL;
    P_MSDU_INFO_T prMsduInfo = (P_MSDU_INFO_T)NULL;

    GLUE_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);

    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TXING_MGMT_LIST);

    prTxingQue = &(prAdapter->rTxCtrl.rTxMgmtTxingQueue);
    QUEUE_MOVE_ALL(prTempQue, prTxingQue);

    QUEUE_REMOVE_HEAD(prTempQue, prQueueEntry, P_QUE_ENTRY_T);
    while (prQueueEntry) {
        prMsduInfo = (P_MSDU_INFO_T)prQueueEntry;

        if(prMsduInfo->ucTxSeqNum == ucSeqNum)
            break;
        else {
            QUEUE_INSERT_TAIL(prTxingQue, prQueueEntry);

            prMsduInfo = NULL;
        }

        QUEUE_REMOVE_HEAD(prTempQue, prQueueEntry, P_QUE_ENTRY_T);
    }
    QUEUE_CONCATENATE_QUEUES(prTxingQue, prTempQue);

    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TXING_MGMT_LIST);

    return prMsduInfo;
}

P_MSDU_INFO_T
nicGetPendingStaMMPDU (
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 ucStaRecIdx
    )
{
    P_MSDU_INFO_T prMsduInfoListHead = (P_MSDU_INFO_T)NULL;
    P_QUE_T prTxingQue = (P_QUE_T)NULL;
    QUE_T rTempQue;
    P_QUE_T prTempQue = &rTempQue;
    P_QUE_ENTRY_T prQueueEntry = (P_QUE_ENTRY_T)NULL;
    P_MSDU_INFO_T prMsduInfo = (P_MSDU_INFO_T)NULL;

    GLUE_SPIN_LOCK_DECLARATION();

    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TXING_MGMT_LIST);
    do {
        if (prAdapter == NULL) {

            ASSERT(FALSE);
            break;
        }

        prTxingQue = &(prAdapter->rTxCtrl.rTxMgmtTxingQueue);
        QUEUE_MOVE_ALL(prTempQue, prTxingQue);

        QUEUE_REMOVE_HEAD(prTempQue, prQueueEntry, P_QUE_ENTRY_T);
        while (prQueueEntry) {
            prMsduInfo = (P_MSDU_INFO_T)prQueueEntry;

            if ((prMsduInfo->ucStaRecIndex == ucStaRecIdx) && (prMsduInfo->pfTxDoneHandler != NULL)) {
                QM_TX_SET_NEXT_MSDU_INFO(prMsduInfo,
                                    prMsduInfoListHead);
                prMsduInfoListHead = prMsduInfo;
            }
            else {
                QUEUE_INSERT_TAIL(prTxingQue, prQueueEntry);

                prMsduInfo = NULL;
            }


            QUEUE_REMOVE_HEAD(prTempQue, prQueueEntry, P_QUE_ENTRY_T);
        }

    } while (FALSE);
    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TXING_MGMT_LIST);


    return prMsduInfoListHead;
} /* nicGetPendingStaMMPDU */


VOID
nicFreePendingTxMsduInfoByNetwork (
    IN P_ADAPTER_T                  prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T    eNetworkType
    )
{
    P_QUE_T prTxingQue;
    QUE_T rTempQue;
    P_QUE_T prTempQue = &rTempQue;
    P_QUE_ENTRY_T prQueueEntry = (P_QUE_ENTRY_T)NULL;
    P_MSDU_INFO_T prMsduInfoListHead = (P_MSDU_INFO_T)NULL;
    P_MSDU_INFO_T prMsduInfoListTail = (P_MSDU_INFO_T)NULL;
    P_MSDU_INFO_T prMsduInfo = (P_MSDU_INFO_T)NULL;

    GLUE_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);

    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TXING_MGMT_LIST);

    prTxingQue = &(prAdapter->rTxCtrl.rTxMgmtTxingQueue);
    QUEUE_MOVE_ALL(prTempQue, prTxingQue);

    QUEUE_REMOVE_HEAD(prTempQue, prQueueEntry, P_QUE_ENTRY_T);
    while (prQueueEntry) {
        prMsduInfo = (P_MSDU_INFO_T)prQueueEntry;

        if((ENUM_NETWORK_TYPE_INDEX_T)(prMsduInfo->ucNetworkType) == eNetworkType) {
            if(prMsduInfoListHead == NULL) {
                prMsduInfoListHead = prMsduInfoListTail = prMsduInfo;
            }
            else {
                QM_TX_SET_NEXT_MSDU_INFO(prMsduInfoListTail, prMsduInfo);
                prMsduInfoListTail = prMsduInfo;
            }
        }
        else {
            QUEUE_INSERT_TAIL(prTxingQue, prQueueEntry);

            prMsduInfo = NULL;
        }

        QUEUE_REMOVE_HEAD(prTempQue, prQueueEntry, P_QUE_ENTRY_T);
    }
    QUEUE_CONCATENATE_QUEUES(prTxingQue, prTempQue);

    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TXING_MGMT_LIST);

    /* free */
    if(prMsduInfoListHead) {
        nicTxFreeMsduInfoPacket(prAdapter, prMsduInfoListHead);
    }

    return;

} /* end of nicFreePendingTxMsduInfoByNetwork() */



/*----------------------------------------------------------------------------*/
/*!
* @brief This procedure is used to retrieve a CMD sequence number atomically
*
* @param    prAdapter   Pointer of ADAPTER_T
*
* @retval - UINT_8
*/
/*----------------------------------------------------------------------------*/
UINT_8
nicIncreaseCmdSeqNum (
    IN P_ADAPTER_T prAdapter
    )
{
    UINT_8 ucRetval;

    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);

    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_CMD_SEQ_NUM);

    prAdapter->ucCmdSeqNum++;
    ucRetval = prAdapter->ucCmdSeqNum;

    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_CMD_SEQ_NUM);

    return ucRetval;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This procedure is used to retrieve a TX sequence number atomically
*
* @param    prAdapter   Pointer of ADAPTER_T
*
* @retval - UINT_8
*/
/*----------------------------------------------------------------------------*/
UINT_8
nicIncreaseTxSeqNum (
    IN P_ADAPTER_T prAdapter
    )
{
    UINT_8 ucRetval;

    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);

    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_SEQ_NUM);

    prAdapter->ucTxSeqNum++;
    ucRetval = prAdapter->ucTxSeqNum;

    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_SEQ_NUM);

    return ucRetval;
}



/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to handle
*        media state change event
*
* @param
*
* @retval
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicMediaStateChange (
    IN P_ADAPTER_T                  prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T    eNetworkType,
    IN P_EVENT_CONNECTION_STATUS    prConnectionStatus
    )
{
    P_GLUE_INFO_T prGlueInfo;

    ASSERT(prAdapter);
    prGlueInfo = prAdapter->prGlueInfo;

    switch(eNetworkType) {
    case NETWORK_TYPE_AIS_INDEX:
        if (prConnectionStatus->ucMediaStatus == PARAM_MEDIA_STATE_DISCONNECTED) { // disconnected
            if(kalGetMediaStateIndicated(prGlueInfo) != PARAM_MEDIA_STATE_DISCONNECTED) {

                kalIndicateStatusAndComplete(prGlueInfo,
                        WLAN_STATUS_MEDIA_DISCONNECT,
                        NULL,
                        0);

                prAdapter->rWlanInfo.u4SysTime = kalGetTimeTick();
            }

            /* reset buffered link quality information */
            prAdapter->fgIsLinkQualityValid = FALSE;
            prAdapter->fgIsLinkRateValid = FALSE;
        }
        else if(prConnectionStatus->ucMediaStatus == PARAM_MEDIA_STATE_CONNECTED) { // connected
            prAdapter->rWlanInfo.u4SysTime = kalGetTimeTick();

            // fill information for association result
            prAdapter->rWlanInfo.rCurrBssId.rSsid.u4SsidLen
                = prConnectionStatus->ucSsidLen;
            kalMemCopy(prAdapter->rWlanInfo.rCurrBssId.rSsid.aucSsid,
                    prConnectionStatus->aucSsid,
                    prConnectionStatus->ucSsidLen);
            kalMemCopy(prAdapter->rWlanInfo.rCurrBssId.arMacAddress,
                    prConnectionStatus->aucBssid,
                    MAC_ADDR_LEN);
            prAdapter->rWlanInfo.rCurrBssId.u4Privacy
                = prConnectionStatus->ucEncryptStatus; // @FIXME
            prAdapter->rWlanInfo.rCurrBssId.rRssi
                = 0; //@FIXME
            prAdapter->rWlanInfo.rCurrBssId.eNetworkTypeInUse
                = PARAM_NETWORK_TYPE_AUTOMODE; //@FIXME
            prAdapter->rWlanInfo.rCurrBssId.rConfiguration.u4BeaconPeriod
                = prConnectionStatus->u2BeaconPeriod;
            prAdapter->rWlanInfo.rCurrBssId.rConfiguration.u4ATIMWindow
                = prConnectionStatus->u2ATIMWindow;
            prAdapter->rWlanInfo.rCurrBssId.rConfiguration.u4DSConfig
                = prConnectionStatus->u4FreqInKHz;
            prAdapter->rWlanInfo.ucNetworkType
                = prConnectionStatus->ucNetworkType;
            prAdapter->rWlanInfo.rCurrBssId.eOpMode
                = (ENUM_PARAM_OP_MODE_T) prConnectionStatus->ucInfraMode;

            // always indicate to OS according to MSDN (re-association/roaming)
            if(kalGetMediaStateIndicated(prGlueInfo) != PARAM_MEDIA_STATE_CONNECTED) {
                kalIndicateStatusAndComplete(prGlueInfo,
                        WLAN_STATUS_MEDIA_CONNECT,
                        NULL,
                        0);
            }
            else {
                /* connected -> connected : roaming ? */
                kalIndicateStatusAndComplete(prGlueInfo,
                        WLAN_STATUS_ROAM_OUT_FIND_BEST,
                        NULL,
                        0);
            }
        }
        break;

#if CFG_ENABLE_BT_OVER_WIFI
    case NETWORK_TYPE_BOW_INDEX:
        break;
#endif

#if CFG_ENABLE_WIFI_DIRECT
    case NETWORK_TYPE_P2P_INDEX:
        break;
#endif
    default:
        ASSERT(0);
    }

    return WLAN_STATUS_SUCCESS;
} /* nicMediaStateChange */

/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to convert between
*        frequency and channel number
*
* @param u4ChannelNum
*
* @retval - Frequency in unit of KHz, 0 for invalid channel number
*/
/*----------------------------------------------------------------------------*/
UINT_32
nicChannelNum2Freq (
    UINT_32 u4ChannelNum
    )
{
    UINT_32 u4ChannelInMHz;

    if(u4ChannelNum >= 1 && u4ChannelNum <= 13) {
        u4ChannelInMHz = 2412 + (u4ChannelNum - 1) * 5;
    }
    else if(u4ChannelNum == 14) {
        u4ChannelInMHz = 2484;
    }
    else if(u4ChannelNum == 133) {
        u4ChannelInMHz = 3665; // 802.11y
    }
    else if(u4ChannelNum == 137) {
        u4ChannelInMHz = 3685; // 802.11y
    }
    else if(u4ChannelNum >= 34 && u4ChannelNum <= 165) {
        u4ChannelInMHz = 5000 + u4ChannelNum * 5;
    }
    else if(u4ChannelNum >= 183 && u4ChannelNum <= 196) {
        u4ChannelInMHz = 4000 + u4ChannelNum * 5;
    }
    else {
        u4ChannelInMHz = 0;
    }

    return 1000 * u4ChannelInMHz;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to convert between
*        frequency and channel number
*
* @param u4FreqInKHz
*
* @retval - Frequency Number, 0 for invalid freqency
*/
/*----------------------------------------------------------------------------*/
UINT_32
nicFreq2ChannelNum (
    UINT_32 u4FreqInKHz
    )
{
    switch(u4FreqInKHz) {
    case 2412000:
        return 1;
    case 2417000:
        return 2;
    case 2422000:
        return 3;
    case 2427000:
        return 4;
    case 2432000:
        return 5;
    case 2437000:
        return 6;
    case 2442000:
        return 7;
    case 2447000:
        return 8;
    case 2452000:
        return 9;
    case 2457000:
        return 10;
    case 2462000:
        return 11;
    case 2467000:
        return 12;
    case 2472000:
        return 13;
    case 2484000:
        return 14;
    case 3665000:
        return 133; // 802.11y
    case 3685000:
        return 137; // 802.11y
    case 4915000:
        return 183;
    case 4920000:
        return 184;
    case 4925000:
        return 185;
    case 4930000:
        return 186;
    case 4935000:
        return 187;
    case 4940000:
        return 188;
    case 4945000:
        return 189;
    case 4960000:
        return 192;
    case 4980000:
        return 196;
    case 5170000:
        return 34;
    case 5180000:
        return 36;
    case 5190000:
        return 38;
    case 5200000:
        return 40;
    case 5210000:
        return 42;
    case 5220000:
        return 44;
    case 5230000:
        return 46;
    case 5240000:
        return 48;
    case 5250000:
        return 50;
    case 5260000:
        return 52;
    case 5270000:
        return 54;
    case 5280000:
        return 56;
    case 5290000:
        return 58;
    case 5300000:
        return 60;
    case 5320000:
        return 64;
    case 5500000:
        return 100;
    case 5520000:
        return 104;
    case 5540000:
        return 108;
    case 5560000:
        return 112;
    case 5580000:
        return 116;
    case 5600000:
        return 120;
    case 5620000:
        return 124;
    case 5640000:
        return 128;
    case 5660000:
        return 132;
    case 5680000:
        return 136;
    case 5700000:
        return 140;
    case 5745000:
        return 149;
    case 5765000:
        return 153;
    case 5785000:
        return 157;
    case 5805000:
        return 161;
    case 5825000:
        return 165;
    default:
        return 0;
    }
}


/* firmware command wrapper */
/* NETWORK (WIFISYS) */
/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to activate WIFISYS for specified network
*
* @param prAdapter          Pointer of ADAPTER_T
*        eNetworkTypeIdx    Index of network type
*
* @retval -
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicActivateNetwork(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetworkTypeIdx
    )
{
    CMD_BSS_ACTIVATE_CTRL   rCmdActivateCtrl;
    P_BSS_INFO_T            prBssInfo;

    ASSERT(prAdapter);
    ASSERT(eNetworkTypeIdx < NETWORK_TYPE_INDEX_NUM);

    rCmdActivateCtrl.ucNetTypeIndex     = (UINT_8)eNetworkTypeIdx;
    rCmdActivateCtrl.ucActive           = 1;

    if (((UINT_8) eNetworkTypeIdx) < NETWORK_TYPE_INDEX_NUM) {
        prBssInfo = &prAdapter->rWifiVar.arBssInfo[eNetworkTypeIdx];
        prBssInfo->fg40mBwAllowed = FALSE;
        prBssInfo->fgAssoc40mBwAllowed = FALSE;
    }

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_BSS_ACTIVATE_CTRL,
            TRUE,
            FALSE,
            FALSE,
            NULL,
            NULL,
            sizeof(CMD_BSS_ACTIVATE_CTRL),
            (PUINT_8)&rCmdActivateCtrl,
            NULL,
            0);
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to deactivate WIFISYS for specified network
*
* @param prAdapter          Pointer of ADAPTER_T
*        eNetworkTypeIdx    Index of network type
*
* @retval -
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicDeactivateNetwork(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetworkTypeIdx
    )
{
    WLAN_STATUS u4Status;
    CMD_BSS_ACTIVATE_CTRL rCmdActivateCtrl;

    ASSERT(prAdapter);
    ASSERT(eNetworkTypeIdx < NETWORK_TYPE_INDEX_NUM);

    rCmdActivateCtrl.ucNetTypeIndex     = (UINT_8)eNetworkTypeIdx;
    rCmdActivateCtrl.ucActive           = 0;

    u4Status = wlanSendSetQueryCmd(prAdapter,
            CMD_ID_BSS_ACTIVATE_CTRL,
            TRUE,
            FALSE,
            FALSE,
            NULL,
            NULL,
            sizeof(CMD_BSS_ACTIVATE_CTRL),
            (PUINT_8)&rCmdActivateCtrl,
            NULL,
            0);

    /* free all correlated station records */
    cnmStaFreeAllStaByNetType(prAdapter, eNetworkTypeIdx, FALSE);
    qmFreeAllByNetType(prAdapter, eNetworkTypeIdx);
    nicFreePendingTxMsduInfoByNetwork(prAdapter, eNetworkTypeIdx);
    kalClearSecurityFramesByNetType(prAdapter->prGlueInfo, eNetworkTypeIdx);

    return u4Status;
}


/* BSS-INFO */
/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to sync bss info with firmware
*        when a new BSS has been connected or disconnected
*
* @param prAdapter          Pointer of ADAPTER_T
*        eNetworkTypeIdx    Index of BSS-INFO type
*
* @retval -
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicUpdateBss(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetworkTypeIdx
    )
{
    WLAN_STATUS u4Status;
    P_BSS_INFO_T prBssInfo;
    CMD_SET_BSS_INFO rCmdSetBssInfo;

    ASSERT(prAdapter);
    ASSERT(eNetworkTypeIdx < NETWORK_TYPE_INDEX_NUM);

    prBssInfo = &(prAdapter->rWifiVar.arBssInfo[eNetworkTypeIdx]);

    kalMemZero(&rCmdSetBssInfo, sizeof(CMD_SET_BSS_INFO));

    rCmdSetBssInfo.ucNetTypeIndex               = (UINT_8)eNetworkTypeIdx;
    rCmdSetBssInfo.ucConnectionState            = (UINT_8)prBssInfo->eConnectionState;
    rCmdSetBssInfo.ucCurrentOPMode              = (UINT_8)prBssInfo->eCurrentOPMode;
    rCmdSetBssInfo.ucSSIDLen                    = (UINT_8)prBssInfo->ucSSIDLen;
    kalMemCopy(rCmdSetBssInfo.aucSSID, prBssInfo->aucSSID, prBssInfo->ucSSIDLen);
    COPY_MAC_ADDR(rCmdSetBssInfo.aucBSSID, prBssInfo->aucBSSID);
    rCmdSetBssInfo.ucIsQBSS                     = (UINT_8)prBssInfo->fgIsQBSS;
    rCmdSetBssInfo.ucNonHTBasicPhyType          = prBssInfo->ucNonHTBasicPhyType;
    rCmdSetBssInfo.u2OperationalRateSet         = prBssInfo->u2OperationalRateSet;
    rCmdSetBssInfo.u2BSSBasicRateSet            = prBssInfo->u2BSSBasicRateSet;
    rCmdSetBssInfo.ucPhyTypeSet                 = prBssInfo->ucPhyTypeSet;
#if CFG_ENABLE_WIFI_DIRECT
    if(prAdapter->fgIsP2PRegistered) {
        COPY_MAC_ADDR(rCmdSetBssInfo.aucOwnMac, prBssInfo->aucOwnMacAddr);
    }
#endif

    rlmFillSyncCmdParam(&rCmdSetBssInfo.rBssRlmParam, prBssInfo);

    rCmdSetBssInfo.fgWapiMode	                = (UINT_8)FALSE;

    if(rCmdSetBssInfo.ucNetTypeIndex == NETWORK_TYPE_AIS_INDEX) {
        P_CONNECTION_SETTINGS_T prConnSettings = &(prAdapter->rWifiVar.rConnSettings);

        rCmdSetBssInfo.ucAuthMode               = (UINT_8)prConnSettings->eAuthMode;
        rCmdSetBssInfo.ucEncStatus              = (UINT_8)prConnSettings->eEncStatus;
        rCmdSetBssInfo.fgWapiMode               = (UINT_8)prConnSettings->fgWapiMode;
    }
#if CFG_ENABLE_BT_OVER_WIFI
    else if(rCmdSetBssInfo.ucNetTypeIndex == NETWORK_TYPE_BOW_INDEX) {
        //P_CONNECTION_SETTINGS_T prConnSettings = &(prAdapter->rWifiVar.rConnSettings);
        rCmdSetBssInfo.ucAuthMode               = (UINT_8)AUTH_MODE_WPA2_PSK;
        rCmdSetBssInfo.ucEncStatus              = (UINT_8)ENUM_ENCRYPTION3_KEY_ABSENT;
    }
#endif
    else {
#if CFG_ENABLE_WIFI_DIRECT
        if(prAdapter->fgIsP2PRegistered) {
            if (kalP2PGetCipher(prAdapter->prGlueInfo)) {
            rCmdSetBssInfo.ucAuthMode               = (UINT_8)AUTH_MODE_WPA2_PSK;
            rCmdSetBssInfo.ucEncStatus              = (UINT_8)ENUM_ENCRYPTION3_KEY_ABSENT;
        }
        else {
            rCmdSetBssInfo.ucAuthMode               = (UINT_8)AUTH_MODE_OPEN;
            rCmdSetBssInfo.ucEncStatus              = (UINT_8)ENUM_ENCRYPTION_DISABLED;
        }
            rCmdSetBssInfo.fgIsApMode = p2pFuncIsAPMode(prAdapter->rWifiVar.prP2pFsmInfo);
        }
#else
        rCmdSetBssInfo.ucAuthMode               = (UINT_8)AUTH_MODE_WPA2_PSK;
        rCmdSetBssInfo.ucEncStatus              = (UINT_8)ENUM_ENCRYPTION3_KEY_ABSENT;
#endif
    }

    if(eNetworkTypeIdx == NETWORK_TYPE_AIS_INDEX &&
            prBssInfo->eCurrentOPMode == OP_MODE_INFRASTRUCTURE &&
            prBssInfo->prStaRecOfAP != NULL) {
        rCmdSetBssInfo.ucStaRecIdxOfAP          = prBssInfo->prStaRecOfAP->ucIndex;

        cnmAisInfraConnectNotify(prAdapter);
    }
#if CFG_ENABLE_WIFI_DIRECT
    else if ((prAdapter->fgIsP2PRegistered) &&
            (eNetworkTypeIdx == NETWORK_TYPE_P2P_INDEX) &&
            (prBssInfo->eCurrentOPMode == OP_MODE_INFRASTRUCTURE) &&
            (prBssInfo->prStaRecOfAP != NULL)) {
        rCmdSetBssInfo.ucStaRecIdxOfAP = prBssInfo->prStaRecOfAP->ucIndex;
    }
#endif

#if CFG_ENABLE_BT_OVER_WIFI
    else if (eNetworkTypeIdx == NETWORK_TYPE_BOW_INDEX &&
            prBssInfo->eCurrentOPMode == OP_MODE_BOW &&
            prBssInfo->prStaRecOfAP != NULL) {
        rCmdSetBssInfo.ucStaRecIdxOfAP = prBssInfo->prStaRecOfAP->ucIndex;
    }
#endif
    else {
        rCmdSetBssInfo.ucStaRecIdxOfAP              = STA_REC_INDEX_NOT_FOUND;
    }

    u4Status = wlanSendSetQueryCmd(prAdapter,
            CMD_ID_SET_BSS_INFO,
            TRUE,
            FALSE,
            FALSE,
            NULL,
            NULL,
            sizeof(CMD_SET_BSS_INFO),
            (PUINT_8)&rCmdSetBssInfo,
            NULL,
            0);

    /* if BSS-INFO is going to be disconnected state, free all correlated station records */
    if(prBssInfo->eConnectionState == PARAM_MEDIA_STATE_DISCONNECTED) {
        /* clear client list */
        bssClearClientList(prAdapter, prBssInfo);

        /* free all correlated station records */
        cnmStaFreeAllStaByNetType(prAdapter, eNetworkTypeIdx, FALSE);
        qmFreeAllByNetType(prAdapter, eNetworkTypeIdx);
        kalClearSecurityFramesByNetType(prAdapter->prGlueInfo, eNetworkTypeIdx);
    }

    return u4Status;
}


/* BSS-INFO Indication (PM) */
/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to indicate PM that
*        a BSS has been created. (for AdHoc / P2P-GO)
*
* @param prAdapter          Pointer of ADAPTER_T
*        eNetworkTypeIdx    Index of BSS-INFO
*
* @retval -
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicPmIndicateBssCreated(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetworkTypeIdx
    )
{
    P_BSS_INFO_T prBssInfo;
    CMD_INDICATE_PM_BSS_CREATED rCmdIndicatePmBssCreated;

    ASSERT(prAdapter);
    ASSERT(eNetworkTypeIdx < NETWORK_TYPE_INDEX_NUM);

    prBssInfo = &(prAdapter->rWifiVar.arBssInfo[eNetworkTypeIdx]);

    rCmdIndicatePmBssCreated.ucNetTypeIndex     = (UINT_8)eNetworkTypeIdx;
    rCmdIndicatePmBssCreated.ucDtimPeriod       = prBssInfo->ucDTIMPeriod;
    rCmdIndicatePmBssCreated.u2BeaconInterval   = prBssInfo->u2BeaconInterval;
    rCmdIndicatePmBssCreated.u2AtimWindow       = prBssInfo->u2ATIMWindow;

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_INDICATE_PM_BSS_CREATED,
            TRUE,
            FALSE,
            FALSE,
            NULL,
            NULL,
            sizeof(CMD_INDICATE_PM_BSS_CREATED),
            (PUINT_8)&rCmdIndicatePmBssCreated,
            NULL,
            0);
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to indicate PM that
*        a BSS has been connected
*
* @param prAdapter          Pointer of ADAPTER_T
*        eNetworkTypeIdx    Index of BSS-INFO
*
* @retval -
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicPmIndicateBssConnected(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetworkTypeIdx
    )
{
    P_BSS_INFO_T prBssInfo;
    CMD_INDICATE_PM_BSS_CONNECTED rCmdIndicatePmBssConnected;

    ASSERT(prAdapter);
    ASSERT(eNetworkTypeIdx < NETWORK_TYPE_INDEX_NUM);

    prBssInfo = &(prAdapter->rWifiVar.arBssInfo[eNetworkTypeIdx]);

    rCmdIndicatePmBssConnected.ucNetTypeIndex   = (UINT_8)eNetworkTypeIdx;
    rCmdIndicatePmBssConnected.ucDtimPeriod     = prBssInfo->ucDTIMPeriod;
    rCmdIndicatePmBssConnected.u2AssocId        = prBssInfo->u2AssocId;
    rCmdIndicatePmBssConnected.u2BeaconInterval = prBssInfo->u2BeaconInterval;
    rCmdIndicatePmBssConnected.u2AtimWindow     = prBssInfo->u2ATIMWindow;

    rCmdIndicatePmBssConnected.ucBmpDeliveryAC  = prBssInfo->rPmProfSetupInfo.ucBmpDeliveryAC;
    rCmdIndicatePmBssConnected.ucBmpTriggerAC   = prBssInfo->rPmProfSetupInfo.ucBmpTriggerAC;

    //DBGPRINTF("nicPmIndicateBssConnected: ucBmpDeliveryAC:0x%x, ucBmpTriggerAC:0x%x",
            //rCmdIndicatePmBssConnected.ucBmpDeliveryAC,
            //rCmdIndicatePmBssConnected.ucBmpTriggerAC);

    if ((eNetworkTypeIdx == NETWORK_TYPE_AIS_INDEX)
#if CFG_ENABLE_WIFI_DIRECT
        || ((eNetworkTypeIdx == NETWORK_TYPE_P2P_INDEX) && (prAdapter->fgIsP2PRegistered))
#endif
         ) {
        if(prBssInfo->eCurrentOPMode == OP_MODE_INFRASTRUCTURE) {
            rCmdIndicatePmBssConnected.fgIsUapsdConnection  = (UINT_8)prBssInfo->prStaRecOfAP->fgIsUapsdSupported;
        }
        else {
            rCmdIndicatePmBssConnected.fgIsUapsdConnection  = 0; //@FIXME
        }
    }
    else {
        rCmdIndicatePmBssConnected.fgIsUapsdConnection  = 0;
    }

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_INDICATE_PM_BSS_CONNECTED,
            TRUE,
            FALSE,
            FALSE,
            NULL,
            NULL,
            sizeof(CMD_INDICATE_PM_BSS_CONNECTED),
            (PUINT_8)&rCmdIndicatePmBssConnected,
            NULL,
            0);
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to indicate PM that
*        a BSS has been disconnected
*
* @param prAdapter          Pointer of ADAPTER_T
*        eNetworkTypeIdx    Index of BSS-INFO
*
* @retval -
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicPmIndicateBssAbort(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetworkTypeIdx
    )
{
    CMD_INDICATE_PM_BSS_ABORT rCmdIndicatePmBssAbort;

    ASSERT(prAdapter);
    ASSERT(eNetworkTypeIdx < NETWORK_TYPE_INDEX_NUM);

    rCmdIndicatePmBssAbort.ucNetTypeIndex       = (UINT_8)eNetworkTypeIdx;

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_INDICATE_PM_BSS_ABORT,
            TRUE,
            FALSE,
            FALSE,
            NULL,
            NULL,
            sizeof(CMD_INDICATE_PM_BSS_ABORT),
            (PUINT_8)&rCmdIndicatePmBssAbort,
            NULL,
            0);
}

WLAN_STATUS
nicConfigPowerSaveProfile (
    IN  P_ADAPTER_T prAdapter,
    ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    PARAM_POWER_MODE ePwrMode,
    BOOLEAN fgEnCmdEvent
    )
{
    DEBUGFUNC("nicConfigPowerSaveProfile");
    DBGLOG(INIT, TRACE, ("eNetTypeIndex:%d, ePwrMode:%d, fgEnCmdEvent:%d\n",
                    eNetTypeIndex, ePwrMode, fgEnCmdEvent));

    ASSERT(prAdapter);

	if (eNetTypeIndex >= NETWORK_TYPE_INDEX_NUM) {
		ASSERT(0);
		return WLAN_STATUS_NOT_SUPPORTED;
	}

//    prAdapter->rWlanInfo.ePowerSaveMode.ucNetTypeIndex = eNetTypeIndex;
//    prAdapter->rWlanInfo.ePowerSaveMode.ucPsProfile = (UINT_8)ePwrMode;
    prAdapter->rWlanInfo.arPowerSaveMode[eNetTypeIndex].ucNetTypeIndex = eNetTypeIndex;
    prAdapter->rWlanInfo.arPowerSaveMode[eNetTypeIndex].ucPsProfile = (UINT_8)ePwrMode;

	printk("Sigma debug: nicConfigPowerSaveProfile ucNetTypeIndex:%d ucPsProfile:%d\n", 
		prAdapter->rWlanInfo.arPowerSaveMode[eNetTypeIndex].ucNetTypeIndex, 
		prAdapter->rWlanInfo.arPowerSaveMode[eNetTypeIndex].ucPsProfile
		);
    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_POWER_SAVE_MODE,
            TRUE,
            FALSE,
            TRUE,
            (fgEnCmdEvent ? nicCmdEventSetCommon : NULL),
            (fgEnCmdEvent ? nicOidCmdTimeoutCommon : NULL),
            sizeof(CMD_PS_PROFILE_T),
            (PUINT_8)&(prAdapter->rWlanInfo.arPowerSaveMode[eNetTypeIndex]),
            NULL,
            sizeof(PARAM_POWER_MODE)
            );

} /* end of wlanoidSetAcpiDevicePowerStateMode() */

/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to indicate firmware domain
*        for beacon generation parameters
*
* @param prAdapter          Pointer of ADAPTER_T
*        eIeUpdMethod,      Update Method
*        eNetTypeIndex      Index of Network
*        u2Capability       Capability
*        aucIe              Pointer to buffer of IEs
*        u2IELen            Length of IEs
*
* @retval - WLAN_STATUS_SUCCESS
*           WLAN_STATUS_FAILURE
*           WLAN_STATUS_PENDING
*           WLAN_STATUS_INVALID_DATA
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicUpdateBeaconIETemplate (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_IE_UPD_METHOD_T eIeUpdMethod,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN UINT_16 u2Capability,
    IN PUINT_8 aucIe,
    IN UINT_16 u2IELen
    )
{
    P_CMD_BEACON_TEMPLATE_UPDATE prCmdBcnUpdate;
    UINT_16 u2CmdBufLen = 0;
    P_GLUE_INFO_T prGlueInfo;
    P_CMD_INFO_T prCmdInfo;
    P_WIFI_CMD_T prWifiCmd;
    UINT_8 ucCmdSeqNum;

    DEBUGFUNC("wlanUpdateBeaconIETemplate");
    DBGLOG(INIT, LOUD, ("\n"));

    ASSERT(prAdapter);
    prGlueInfo = prAdapter->prGlueInfo;

    if (u2IELen > MAX_IE_LENGTH) {
        return WLAN_STATUS_INVALID_DATA;
    }

    if (eIeUpdMethod == IE_UPD_METHOD_UPDATE_RANDOM
            || eIeUpdMethod == IE_UPD_METHOD_UPDATE_ALL) {
        u2CmdBufLen = OFFSET_OF(CMD_BEACON_TEMPLATE_UPDATE, aucIE) + u2IELen;
    }
    else if (eIeUpdMethod == IE_UPD_METHOD_DELETE_ALL) {
        u2CmdBufLen = OFFSET_OF(CMD_BEACON_TEMPLATE_UPDATE, u2IELen);
    }
    else {
        ASSERT(0);
        return WLAN_STATUS_FAILURE;
    }

    // prepare command info
    prCmdInfo = cmdBufAllocateCmdInfo(prAdapter, (CMD_HDR_SIZE + u2CmdBufLen));
    if (!prCmdInfo) {
        DBGLOG(INIT, ERROR, ("Allocate CMD_INFO_T ==> FAILED.\n"));
        return WLAN_STATUS_FAILURE;
    }

    // increase command sequence number
    ucCmdSeqNum = nicIncreaseCmdSeqNum(prAdapter);
    DBGLOG(REQ, TRACE, ("ucCmdSeqNum =%d\n", ucCmdSeqNum));

    // Setup common CMD Info Packet
    prCmdInfo->eCmdType = COMMAND_TYPE_NETWORK_IOCTL;
    prCmdInfo->eNetworkType = eNetTypeIndex;
    prCmdInfo->u2InfoBufLen = (UINT_16)(CMD_HDR_SIZE + u2CmdBufLen);
    prCmdInfo->pfCmdDoneHandler = NULL;     //@FIXME
    prCmdInfo->pfCmdTimeoutHandler = NULL;  //@FIXME
    prCmdInfo->fgIsOid = FALSE;
    prCmdInfo->ucCID = CMD_ID_UPDATE_BEACON_CONTENT;
    prCmdInfo->fgSetQuery = TRUE;
    prCmdInfo->fgNeedResp = FALSE;
    prCmdInfo->fgDriverDomainMCR = FALSE;
    prCmdInfo->ucCmdSeqNum = ucCmdSeqNum;
    prCmdInfo->u4SetInfoLen = u2CmdBufLen;
    prCmdInfo->pvInformationBuffer = NULL;
    prCmdInfo->u4InformationBufferLength = 0;

    // Setup WIFI_CMD_T (no payload)
    prWifiCmd = (P_WIFI_CMD_T)(prCmdInfo->pucInfoBuffer);
    prWifiCmd->u2TxByteCount_UserPriority = prCmdInfo->u2InfoBufLen;
    prWifiCmd->ucCID = prCmdInfo->ucCID;
    prWifiCmd->ucSetQuery = prCmdInfo->fgSetQuery;
    prWifiCmd->ucSeqNum = prCmdInfo->ucCmdSeqNum;

    prCmdBcnUpdate = (P_CMD_BEACON_TEMPLATE_UPDATE)(prWifiCmd->aucBuffer);

    // fill beacon updating command
    prCmdBcnUpdate->ucUpdateMethod  = (UINT_8) eIeUpdMethod;
    prCmdBcnUpdate->ucNetTypeIndex  = (UINT_8) eNetTypeIndex;
    prCmdBcnUpdate->u2Capability    = u2Capability;
    prCmdBcnUpdate->u2IELen         = u2IELen;
    if(u2IELen > 0 ) {
        kalMemCopy(prCmdBcnUpdate->aucIE, aucIe, u2IELen);
    }

    // insert into prCmdQueue
    kalEnqueueCommand(prGlueInfo, (P_QUE_ENTRY_T)prCmdInfo);

    // wakeup txServiceThread later
    GLUE_SET_EVENT(prGlueInfo);
    return WLAN_STATUS_PENDING;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to initialization PHY related
*        varaibles
*
* @param prAdapter  Pointer of ADAPTER_T
*
* @retval none
*/
/*----------------------------------------------------------------------------*/
VOID
nicSetAvailablePhyTypeSet (
    IN P_ADAPTER_T prAdapter
    )
{
    P_CONNECTION_SETTINGS_T prConnSettings;

    ASSERT(prAdapter);

    prConnSettings = &(prAdapter->rWifiVar.rConnSettings);

    if (prConnSettings->eDesiredPhyConfig >= PHY_CONFIG_NUM) {
        ASSERT(0);
        return;
    }

    prAdapter->rWifiVar.ucAvailablePhyTypeSet =
        aucPhyCfg2PhyTypeSet[prConnSettings->eDesiredPhyConfig];

    if (prAdapter->rWifiVar.ucAvailablePhyTypeSet & PHY_TYPE_BIT_ERP) {
        prAdapter->rWifiVar.eNonHTBasicPhyType2G4 = PHY_TYPE_ERP_INDEX;
    }
    /* NOTE(Kevin): Because we don't have N only mode, TBD */
    else /* if (ucNonHTPhyTypeSet & PHY_TYPE_HR_DSSS_INDEX) */ {
        prAdapter->rWifiVar.eNonHTBasicPhyType2G4 = PHY_TYPE_HR_DSSS_INDEX;
    }

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to update WMM Parms
*
* @param prAdapter          Pointer of ADAPTER_T
*        eNetworkTypeIdx    Index of BSS-INFO
*
* @retval -
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicQmUpdateWmmParms(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetworkTypeIdx
    )
{
    P_BSS_INFO_T prBssInfo;
    CMD_UPDATE_WMM_PARMS_T rCmdUpdateWmmParms;

    ASSERT(prAdapter);
    ASSERT(eNetworkTypeIdx < NETWORK_TYPE_INDEX_NUM);

    DBGLOG(QM, EVENT, ("sizeof(AC_QUE_PARMS_T): %d \n", sizeof(AC_QUE_PARMS_T)));
    DBGLOG(QM, EVENT, ("sizeof(CMD_UPDATE_WMM_PARMS): %d \n", sizeof(CMD_UPDATE_WMM_PARMS_T)));
    DBGLOG(QM, EVENT, ("sizeof(WIFI_CMD_T): %d \n", sizeof(WIFI_CMD_T)));

    prBssInfo = &(prAdapter->rWifiVar.arBssInfo[eNetworkTypeIdx]);
    rCmdUpdateWmmParms.ucNetTypeIndex = (UINT_8)eNetworkTypeIdx;
    kalMemCopy(&rCmdUpdateWmmParms.arACQueParms[0], &prBssInfo->arACQueParms[0],
        (sizeof(AC_QUE_PARMS_T)*AC_NUM));

    rCmdUpdateWmmParms.fgIsQBSS = prBssInfo->fgIsQBSS;

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_UPDATE_WMM_PARMS,
            TRUE,
            FALSE,
            FALSE,
            NULL,
            NULL,
            sizeof(CMD_UPDATE_WMM_PARMS_T),
            (PUINT_8)&rCmdUpdateWmmParms,
            NULL,
            0);
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to update TX power gain corresponding to
*        each band/modulation combination
*
* @param prAdapter          Pointer of ADAPTER_T
*        prTxPwrParam       Pointer of TX power parameters
*
* @retval WLAN_STATUS_PENDING
*         WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicUpdateTxPower(
    IN P_ADAPTER_T      prAdapter,
    IN P_CMD_TX_PWR_T   prTxPwrParam
    )
{
    DEBUGFUNC("nicUpdateTxPower");

    ASSERT(prAdapter);

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_SET_TX_PWR,
            TRUE,
            FALSE,
            FALSE,
            NULL,
            NULL,
            sizeof(CMD_TX_PWR_T),
            (PUINT_8)prTxPwrParam,
            NULL,
            0);
}

/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to set auto tx power parameter
*
* @param prAdapter          Pointer of ADAPTER_T
*        prTxPwrParam       Pointer of Auto TX power parameters
*
* @retval WLAN_STATUS_PENDING
*         WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicSetAutoTxPower(
    IN P_ADAPTER_T      prAdapter,
    IN P_CMD_AUTO_POWER_PARAM_T   prAutoPwrParam
    )
{
    DEBUGFUNC("nicSetAutoTxPower");

    ASSERT(prAdapter);

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_SET_AUTOPWR_CTRL,
            TRUE,
            FALSE,
            FALSE,
            NULL,
            NULL,
            sizeof(CMD_AUTO_POWER_PARAM_T),
            (PUINT_8)prAutoPwrParam,
            NULL,
            0);
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to update TX power gain corresponding to
*        each band/modulation combination
*
* @param prAdapter          Pointer of ADAPTER_T
*        prTxPwrParam       Pointer of TX power parameters
*
* @retval WLAN_STATUS_PENDING
*         WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicSetAutoTxPowerControl(
    IN P_ADAPTER_T      prAdapter,
    IN P_CMD_TX_PWR_T   prTxPwrParam
    )
{
    DEBUGFUNC("nicUpdateTxPower");

    ASSERT(prAdapter);

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_SET_TX_PWR,
            TRUE,
            FALSE,
            FALSE,
            NULL,
            NULL,
            sizeof(CMD_TX_PWR_T),
            (PUINT_8)prTxPwrParam,
            NULL,
            0);
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to update power offset around 5GHz band
*
* @param prAdapter          Pointer of ADAPTER_T
*        pr5GPwrOffset      Pointer of 5GHz power offset parameter
*
* @retval WLAN_STATUS_PENDING
*         WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicUpdate5GOffset(
    IN P_ADAPTER_T              prAdapter,
    IN P_CMD_5G_PWR_OFFSET_T    pr5GPwrOffset
    )
{
    DEBUGFUNC("nicUpdate5GOffset");

    ASSERT(prAdapter);

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_SET_5G_PWR_OFFSET,
            TRUE,
            FALSE,
            FALSE,
            NULL,
            NULL,
            sizeof(CMD_5G_PWR_OFFSET_T),
            (PUINT_8)pr5GPwrOffset,
            NULL,
            0);
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to update DPD calibration result
*
* @param prAdapter          Pointer of ADAPTER_T
*        pr5GPwrOffset      Pointer of parameter for DPD calibration result
*
* @retval WLAN_STATUS_PENDING
*         WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicUpdateDPD(
    IN P_ADAPTER_T          prAdapter,
    IN P_CMD_PWR_PARAM_T    prDpdCalResult
    )
{
    DEBUGFUNC("nicUpdateDPD");

    ASSERT(prAdapter);

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_SET_PWR_PARAM,
            TRUE,
            FALSE,
            FALSE,
            NULL,
            NULL,
            sizeof(CMD_PWR_PARAM_T),
            (PUINT_8)prDpdCalResult,
            NULL,
            0);
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function starts system service such as timer and
*        memory pools
*
* @param prAdapter          Pointer of ADAPTER_T
*
* @retval none
*/
/*----------------------------------------------------------------------------*/
VOID
nicInitSystemService (
    IN P_ADAPTER_T prAdapter
    )
{
    ASSERT(prAdapter);

    // <1> Initialize MGMT Memory pool and STA_REC
    cnmMemInit(prAdapter);
    cnmStaRecInit(prAdapter);
    cmdBufInitialize(prAdapter);

    // <2> Mailbox Initialization
    mboxInitialize(prAdapter);

    // <3> Timer Initialization
    cnmTimerInitialize(prAdapter);

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function reset some specific system service,
*        such as STA-REC
*
* @param prAdapter          Pointer of ADAPTER_T
*
* @retval none
*/
/*----------------------------------------------------------------------------*/
VOID
nicResetSystemService (
    IN P_ADAPTER_T prAdapter
    )
{
    ASSERT(prAdapter);
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to update WMM Parms
*
* @param prAdapter          Pointer of ADAPTER_T
*
* @retval none
*/
/*----------------------------------------------------------------------------*/
VOID
nicUninitSystemService (
    IN P_ADAPTER_T prAdapter
    )
{
    ASSERT(prAdapter);

    /* Timer Destruction */
    cnmTimerDestroy(prAdapter);

    /* Mailbox Destruction */
    mboxDestroy(prAdapter);

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to update WMM Parms
*
* @param prAdapter          Pointer of ADAPTER_T
*
* @retval none
*/
/*----------------------------------------------------------------------------*/
VOID
nicInitMGMT (
    IN P_ADAPTER_T prAdapter,
    IN P_REG_INFO_T prRegInfo
    )
{
    ASSERT(prAdapter);

    /* CNM Module - initialization */
    cnmInit(prAdapter);

    /* RLM Module - initialization */
    rlmFsmEventInit(prAdapter);

    /* SCN Module - initialization */
    scnInit(prAdapter);

    /* AIS Module - intiailization */
    aisInitializeConnectionSettings(prAdapter, prRegInfo);
    aisFsmInit(prAdapter);

#if CFG_SUPPORT_ROAMING
    /* Roaming Module - intiailization */
    roamingFsmInit(prAdapter);
#endif /* CFG_SUPPORT_ROAMING */

#if CFG_SUPPORT_SWCR
    swCrDebugInit(prAdapter);
#endif /* CFG_SUPPORT_SWCR */

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to update WMM Parms
*
* @param prAdapter          Pointer of ADAPTER_T
*
* @retval none
*/
/*----------------------------------------------------------------------------*/
VOID
nicUninitMGMT (
    IN P_ADAPTER_T prAdapter
    )
{
    ASSERT(prAdapter);

#if CFG_SUPPORT_SWCR
    swCrDebugUninit(prAdapter);
#endif /* CFG_SUPPORT_SWCR */

#if CFG_SUPPORT_ROAMING
    /* Roaming Module - unintiailization */
    roamingFsmUninit(prAdapter);
#endif /* CFG_SUPPORT_ROAMING */

    /* AIS Module - unintiailization */
    aisFsmUninit(prAdapter);

    /* SCN Module - unintiailization */
    scnUninit(prAdapter);

    /* RLM Module - uninitialization */
    rlmFsmEventUninit(prAdapter);

    /* CNM Module - uninitialization */
    cnmUninit(prAdapter);

    return;
}


#if (MT6620_E1_ASIC_HIFSYS_WORKAROUND == 1)
/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is to inform firmware to enable MCU clock gating
*
* @param prAdapter          Pointer of ADAPTER_T
*
* @retval none
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicEnableClockGating (
    IN P_ADAPTER_T prAdapter
    )
{
    UINT_32 i, u4WHISR = 0;

    ASSERT(prAdapter);

    if(prAdapter->fgIsClockGatingEnabled == TRUE) {
        return WLAN_STATUS_SUCCESS;
    }
    else {
        nicSetSwIntr(prAdapter, REQ_GATING_ENABLE_H2D_INT);

        i = 0;
        while(i < GATING_CONTROL_POLL_LIMIT) {
            if(kalIsCardRemoved(prAdapter->prGlueInfo) == TRUE
                    || fgIsBusAccessFailed == TRUE) {
                return WLAN_STATUS_FAILURE;
            }

            HAL_READ_INTR_STATUS(prAdapter, sizeof(UINT_32), (PUINT_8)&u4WHISR);

            if(u4WHISR & ACK_GATING_ENABLE_D2H_INT) {
                prAdapter->fgIsClockGatingEnabled = TRUE;
                return WLAN_STATUS_SUCCESS;
            }
        }

        ASSERT(0);
        return WLAN_STATUS_PENDING;
    }
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is to inform firmware to disable MCU clock gating
*
* @param prAdapter          Pointer of ADAPTER_T
*
* @retval none
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicDisableClockGating (
    IN P_ADAPTER_T prAdapter
    )
{
    UINT_32 i, u4WHISR = 0;

    ASSERT(prAdapter);

    if(prAdapter->fgIsClockGatingEnabled == FALSE) {
        return WLAN_STATUS_SUCCESS;
    }
    else {
        nicSetSwIntr(prAdapter, REQ_GATING_DISABLE_H2D_INT);

        i = 0;
        while(i < GATING_CONTROL_POLL_LIMIT) {
            if(kalIsCardRemoved(prAdapter->prGlueInfo) == TRUE
                    || fgIsBusAccessFailed == TRUE) {
                return WLAN_STATUS_FAILURE;
            }

            HAL_READ_INTR_STATUS(prAdapter, sizeof(UINT_32), (PUINT_8)&u4WHISR);

            if(u4WHISR & ACK_GATING_DISABLE_D2H_INT) {
                prAdapter->fgIsClockGatingEnabled = FALSE;
                return WLAN_STATUS_SUCCESS;
            }
        }

        ASSERT(0);
        return WLAN_STATUS_PENDING;
    }
}
#endif


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is invoked to buffer scan result
*
* @param prAdapter          Pointer to the Adapter structure.
* @param rMacAddr           BSSID
* @param prSsid             Pointer to SSID
* @param u4Privacy          Privacy settings (0: Open / 1: WEP/WPA/WPA2 enabled)
* @param rRssi              Received Strength (-10 ~ -200 dBm)
* @param eNetworkType       Network Type (a/b/g)
* @param prConfiguration    Network Parameter
* @param eOpMode            Infra/Ad-Hoc
* @param rSupportedRates    Supported basic rates
* @param u2IELength         IE Length
* @param pucIEBuf           Pointer to Information Elements(IEs)
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicAddScanResult (
    IN P_ADAPTER_T                  prAdapter,
    IN PARAM_MAC_ADDRESS            rMacAddr,
    IN P_PARAM_SSID_T               prSsid,
    IN UINT_32                      u4Privacy,
    IN PARAM_RSSI                   rRssi,
    IN ENUM_PARAM_NETWORK_TYPE_T    eNetworkType,
    IN P_PARAM_802_11_CONFIG_T      prConfiguration,
    IN ENUM_PARAM_OP_MODE_T         eOpMode,
    IN PARAM_RATES_EX               rSupportedRates,
    IN UINT_16                      u2IELength,
    IN PUINT_8                      pucIEBuf
    )
{
    BOOLEAN bReplace;
    UINT_32 i;
    UINT_32 u4IdxWeakest = 0;
    PARAM_RSSI rWeakestRssi;
    UINT_32 u4BufferSize;

    ASSERT(prAdapter);

    rWeakestRssi = (PARAM_RSSI)INT_MAX;
    u4BufferSize = sizeof(prAdapter->rWlanInfo.aucScanIEBuf) / sizeof(prAdapter->rWlanInfo.aucScanIEBuf[0]);

    bReplace = FALSE;

    // decide to replace or add
    for(i = 0 ; i < prAdapter->rWlanInfo.u4ScanResultNum ; i++) {
        // find weakest entry && not connected one
        if(UNEQUAL_MAC_ADDR(prAdapter->rWlanInfo.arScanResult[i].arMacAddress, prAdapter->rWlanInfo.rCurrBssId.arMacAddress)
                && prAdapter->rWlanInfo.arScanResult[i].rRssi < rWeakestRssi) {
            u4IdxWeakest = i;
            rWeakestRssi = prAdapter->rWlanInfo.arScanResult[i].rRssi;
        }

        if(prAdapter->rWlanInfo.arScanResult[i].eOpMode == eOpMode &&
                EQUAL_MAC_ADDR(&(prAdapter->rWlanInfo.arScanResult[i].arMacAddress), rMacAddr) &&
                    (EQUAL_SSID(prAdapter->rWlanInfo.arScanResult[i].rSsid.aucSsid,
                            prAdapter->rWlanInfo.arScanResult[i].rSsid.u4SsidLen,
                            prSsid->aucSsid,
                            prSsid->u4SsidLen)
                     || prAdapter->rWlanInfo.arScanResult[i].rSsid.u4SsidLen == 0)) {
            // replace entry
            bReplace = TRUE;

            // free IE buffer then zero
            nicFreeScanResultIE(prAdapter, i);
            kalMemZero(&(prAdapter->rWlanInfo.arScanResult[i]), OFFSET_OF(PARAM_BSSID_EX_T, aucIEs));

            // then fill buffer
            prAdapter->rWlanInfo.arScanResult[i].u4Length =
                OFFSET_OF(PARAM_BSSID_EX_T, aucIEs) + u2IELength;
            COPY_MAC_ADDR(prAdapter->rWlanInfo.arScanResult[i].arMacAddress, rMacAddr);
            COPY_SSID(prAdapter->rWlanInfo.arScanResult[i].rSsid.aucSsid,
                    prAdapter->rWlanInfo.arScanResult[i].rSsid.u4SsidLen,
                    prSsid->aucSsid,
                    prSsid->u4SsidLen);
            prAdapter->rWlanInfo.arScanResult[i].u4Privacy = u4Privacy;
            prAdapter->rWlanInfo.arScanResult[i].rRssi = rRssi;
            prAdapter->rWlanInfo.arScanResult[i].eNetworkTypeInUse = eNetworkType;
            kalMemCopy(&(prAdapter->rWlanInfo.arScanResult[i].rConfiguration),
                    prConfiguration,
                    sizeof(PARAM_802_11_CONFIG_T));
            prAdapter->rWlanInfo.arScanResult[i].eOpMode = eOpMode;
            kalMemCopy((prAdapter->rWlanInfo.arScanResult[i].rSupportedRates),
                    rSupportedRates,
                    sizeof(PARAM_RATES_EX));
            prAdapter->rWlanInfo.arScanResult[i].u4IELength = (UINT_32)u2IELength;

            // IE - allocate buffer and update pointer
            if(u2IELength > 0) {
                if(ALIGN_4(u2IELength) + prAdapter->rWlanInfo.u4ScanIEBufferUsage <= u4BufferSize) {
                    kalMemCopy(&(prAdapter->rWlanInfo.aucScanIEBuf[prAdapter->rWlanInfo.u4ScanIEBufferUsage]),
                            pucIEBuf,
                            u2IELength);

                    prAdapter->rWlanInfo.apucScanResultIEs[i] =
                        &(prAdapter->rWlanInfo.aucScanIEBuf[prAdapter->rWlanInfo.u4ScanIEBufferUsage]);

                    prAdapter->rWlanInfo.u4ScanIEBufferUsage += ALIGN_4(u2IELength);
                }
                else {
                    // buffer is not enough
                    prAdapter->rWlanInfo.arScanResult[i].u4Length -= u2IELength;
                    prAdapter->rWlanInfo.arScanResult[i].u4IELength = 0;
                    prAdapter->rWlanInfo.apucScanResultIEs[i] = NULL;
                }
            }
            else {
                prAdapter->rWlanInfo.apucScanResultIEs[i] = NULL;
            }

            break;
        }
    }

    if (bReplace == FALSE) {
        if (prAdapter->rWlanInfo.u4ScanResultNum < (CFG_MAX_NUM_BSS_LIST - 1)) {
            i = prAdapter->rWlanInfo.u4ScanResultNum;

            // zero
            kalMemZero(&(prAdapter->rWlanInfo.arScanResult[i]),
                    OFFSET_OF(PARAM_BSSID_EX_T, aucIEs));

            // then fill buffer
            prAdapter->rWlanInfo.arScanResult[i].u4Length =
                OFFSET_OF(PARAM_BSSID_EX_T, aucIEs) + u2IELength;
            COPY_MAC_ADDR(prAdapter->rWlanInfo.arScanResult[i].arMacAddress, rMacAddr);
            COPY_SSID(prAdapter->rWlanInfo.arScanResult[i].rSsid.aucSsid,
                    prAdapter->rWlanInfo.arScanResult[i].rSsid.u4SsidLen,
                    prSsid->aucSsid,
                    prSsid->u4SsidLen);
            prAdapter->rWlanInfo.arScanResult[i].u4Privacy = u4Privacy;
            prAdapter->rWlanInfo.arScanResult[i].rRssi = rRssi;
            prAdapter->rWlanInfo.arScanResult[i].eNetworkTypeInUse = eNetworkType;
            kalMemCopy(&(prAdapter->rWlanInfo.arScanResult[i].rConfiguration),
                    prConfiguration,
                    sizeof(PARAM_802_11_CONFIG_T));
            prAdapter->rWlanInfo.arScanResult[i].eOpMode = eOpMode;
            kalMemCopy((prAdapter->rWlanInfo.arScanResult[i].rSupportedRates),
                    rSupportedRates,
                    sizeof(PARAM_RATES_EX));
            prAdapter->rWlanInfo.arScanResult[i].u4IELength = (UINT_32)u2IELength;

            // IE - allocate buffer and update pointer
            if(u2IELength > 0) {
                if(ALIGN_4(u2IELength) + prAdapter->rWlanInfo.u4ScanIEBufferUsage <= u4BufferSize) {
                    kalMemCopy(&(prAdapter->rWlanInfo.aucScanIEBuf[prAdapter->rWlanInfo.u4ScanIEBufferUsage]),
                            pucIEBuf,
                            u2IELength);

                    prAdapter->rWlanInfo.apucScanResultIEs[i] =
                        &(prAdapter->rWlanInfo.aucScanIEBuf[prAdapter->rWlanInfo.u4ScanIEBufferUsage]);

                    prAdapter->rWlanInfo.u4ScanIEBufferUsage += ALIGN_4(u2IELength);
                }
                else {
                    // buffer is not enough
                    prAdapter->rWlanInfo.arScanResult[i].u4Length -= u2IELength;
                    prAdapter->rWlanInfo.arScanResult[i].u4IELength = 0;
                    prAdapter->rWlanInfo.apucScanResultIEs[i] = NULL;
                }
            }
            else {
                prAdapter->rWlanInfo.apucScanResultIEs[i] = NULL;
            }

            prAdapter->rWlanInfo.u4ScanResultNum++;
        }
        else if(rWeakestRssi != (PARAM_RSSI)INT_MAX) {
            // replace weakest one
            i = u4IdxWeakest;

            // free IE buffer then zero
            nicFreeScanResultIE(prAdapter, i);
            kalMemZero(&(prAdapter->rWlanInfo.arScanResult[i]),
                    OFFSET_OF(PARAM_BSSID_EX_T, aucIEs));

            // then fill buffer
            prAdapter->rWlanInfo.arScanResult[i].u4Length =
                OFFSET_OF(PARAM_BSSID_EX_T, aucIEs) + u2IELength;
            COPY_MAC_ADDR(prAdapter->rWlanInfo.arScanResult[i].arMacAddress, rMacAddr);
            COPY_SSID(prAdapter->rWlanInfo.arScanResult[i].rSsid.aucSsid,
                    prAdapter->rWlanInfo.arScanResult[i].rSsid.u4SsidLen,
                    prSsid->aucSsid,
                    prSsid->u4SsidLen);
            prAdapter->rWlanInfo.arScanResult[i].u4Privacy = u4Privacy;
            prAdapter->rWlanInfo.arScanResult[i].rRssi = rRssi;
            prAdapter->rWlanInfo.arScanResult[i].eNetworkTypeInUse = eNetworkType;
            kalMemCopy(&(prAdapter->rWlanInfo.arScanResult[i].rConfiguration),
                    prConfiguration,
                    sizeof(PARAM_802_11_CONFIG_T));
            prAdapter->rWlanInfo.arScanResult[i].eOpMode = eOpMode;
            kalMemCopy((prAdapter->rWlanInfo.arScanResult[i].rSupportedRates),
                    rSupportedRates,
                    sizeof(PARAM_RATES_EX));
            prAdapter->rWlanInfo.arScanResult[i].u4IELength = (UINT_32)u2IELength;

            if(u2IELength > 0) {
                // IE - allocate buffer and update pointer
                if(ALIGN_4(u2IELength) + prAdapter->rWlanInfo.u4ScanIEBufferUsage <= u4BufferSize) {
                    kalMemCopy(&(prAdapter->rWlanInfo.aucScanIEBuf[prAdapter->rWlanInfo.u4ScanIEBufferUsage]),
                            pucIEBuf,
                            u2IELength);

                    prAdapter->rWlanInfo.apucScanResultIEs[i] =
                        &(prAdapter->rWlanInfo.aucScanIEBuf[prAdapter->rWlanInfo.u4ScanIEBufferUsage]);

                    prAdapter->rWlanInfo.u4ScanIEBufferUsage += ALIGN_4(u2IELength);
                }
                else {
                    // buffer is not enough
                    prAdapter->rWlanInfo.arScanResult[i].u4Length -= u2IELength;
                    prAdapter->rWlanInfo.arScanResult[i].u4IELength = 0;
                    prAdapter->rWlanInfo.apucScanResultIEs[i] = NULL;
                }
            }
            else {
                prAdapter->rWlanInfo.apucScanResultIEs[i] = NULL;
            }
        }
    }
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is invoked to free IE buffer for dedicated scan result
*
* @param prAdapter          Pointer to the Adapter structure.
* @param u4Idx              Index of Scan Result
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicFreeScanResultIE (
    IN P_ADAPTER_T  prAdapter,
    IN UINT_32      u4Idx
    )
{
    UINT_32 i;
    PUINT_8 pucPivot, pucMovePivot;
    UINT_32 u4MoveSize, u4FreeSize, u4ReserveSize;

    ASSERT(prAdapter);
    ASSERT(u4Idx < CFG_MAX_NUM_BSS_LIST);

    if(prAdapter->rWlanInfo.arScanResult[u4Idx].u4IELength == 0
            || prAdapter->rWlanInfo.apucScanResultIEs[u4Idx] == NULL) {
        return;
    }

    u4FreeSize = ALIGN_4(prAdapter->rWlanInfo.arScanResult[u4Idx].u4IELength);

    pucPivot = prAdapter->rWlanInfo.apucScanResultIEs[u4Idx];
    pucMovePivot = (PUINT_8)((UINT_32)(prAdapter->rWlanInfo.apucScanResultIEs[u4Idx]) + u4FreeSize);

    u4ReserveSize = ((UINT_32)pucPivot) - (UINT_32)(&(prAdapter->rWlanInfo.aucScanIEBuf[0]));
    u4MoveSize = prAdapter->rWlanInfo.u4ScanIEBufferUsage - u4ReserveSize - u4FreeSize;

    // 1. rest of buffer to move forward
    kalMemCopy(pucPivot, pucMovePivot, u4MoveSize);

    // 1.1 modify pointers
    for(i = 0 ; i < prAdapter->rWlanInfo.u4ScanResultNum ; i++) {
        if(i != u4Idx) {
            if(prAdapter->rWlanInfo.apucScanResultIEs[i] >= pucMovePivot) {
                prAdapter->rWlanInfo.apucScanResultIEs[i] =
                    (PUINT_8)((UINT_32)(prAdapter->rWlanInfo.apucScanResultIEs[i]) - u4FreeSize);
            }
        }
    }

    // 1.2 reset the freed one
    prAdapter->rWlanInfo.arScanResult[u4Idx].u4IELength = 0;
    prAdapter->rWlanInfo.apucScanResultIEs[i] = NULL;

    // 2. reduce IE buffer usage
    prAdapter->rWlanInfo.u4ScanIEBufferUsage -= u4FreeSize;

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is to hack parameters for WLAN TABLE for
*        fixed rate settings
*
* @param prAdapter          Pointer to the Adapter structure.
* @param eRateSetting
* @param pu2DesiredNonHTRateSet,
* @param pu2BSSBasicRateSet,
* @param pucMcsSet
* @param pucSupMcs32
* @param pu2HtCapInfo
*
* @return WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicUpdateRateParams (
    IN P_ADAPTER_T                  prAdapter,
    IN ENUM_REGISTRY_FIXED_RATE_T   eRateSetting,
    IN PUINT_8                      pucDesiredPhyTypeSet,
    IN PUINT_16                     pu2DesiredNonHTRateSet,
    IN PUINT_16                     pu2BSSBasicRateSet,
    IN PUINT_8                      pucMcsSet,
    IN PUINT_8                      pucSupMcs32,
    IN PUINT_16                     pu2HtCapInfo
    )
{
    ASSERT(prAdapter);
    ASSERT(eRateSetting > FIXED_RATE_NONE && eRateSetting < FIXED_RATE_NUM);

    switch(prAdapter->rWifiVar.eRateSetting) {
    case FIXED_RATE_1M:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HR_DSSS;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_1M;
        *pu2BSSBasicRateSet = RATE_SET_BIT_1M;
        *pucMcsSet = 0;
        *pucSupMcs32 = 0;
        *pu2HtCapInfo = 0;
        break;

    case FIXED_RATE_2M:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HR_DSSS;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_2M;
        *pu2BSSBasicRateSet = RATE_SET_BIT_2M;
        *pucMcsSet = 0;
        *pucSupMcs32 = 0;
        *pu2HtCapInfo = 0;
        break;

    case FIXED_RATE_5_5M:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HR_DSSS;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_5_5M;
        *pu2BSSBasicRateSet = RATE_SET_BIT_5_5M;
        *pucMcsSet = 0;
        *pucSupMcs32 = 0;
        *pu2HtCapInfo = 0;
        break;

    case FIXED_RATE_11M:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HR_DSSS;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_11M;
        *pu2BSSBasicRateSet = RATE_SET_BIT_11M;
        *pucMcsSet = 0;
        *pucSupMcs32 = 0;
        *pu2HtCapInfo = 0;
        break;

    case FIXED_RATE_6M:
        if((*pucDesiredPhyTypeSet) | PHY_TYPE_BIT_ERP) {
            *pucDesiredPhyTypeSet = PHY_TYPE_BIT_ERP;
        }
        else if((*pucDesiredPhyTypeSet) | PHY_TYPE_BIT_OFDM) {
            *pucDesiredPhyTypeSet = PHY_TYPE_BIT_OFDM;
        }

        *pu2DesiredNonHTRateSet = RATE_SET_BIT_6M;
        *pu2BSSBasicRateSet = RATE_SET_BIT_6M;
        *pucMcsSet = 0;
        *pucSupMcs32 = 0;
        *pu2HtCapInfo = 0;
        break;

    case FIXED_RATE_9M:
        if((*pucDesiredPhyTypeSet) | PHY_TYPE_BIT_ERP) {
            *pucDesiredPhyTypeSet = PHY_TYPE_BIT_ERP;
        }
        else if((*pucDesiredPhyTypeSet) | PHY_TYPE_BIT_OFDM) {
            *pucDesiredPhyTypeSet = PHY_TYPE_BIT_OFDM;
        }

        *pu2DesiredNonHTRateSet = RATE_SET_BIT_9M;
        *pu2BSSBasicRateSet = RATE_SET_BIT_9M;
        *pucMcsSet = 0;
        *pucSupMcs32 = 0;
        *pu2HtCapInfo = 0;
        break;

    case FIXED_RATE_12M:
        if((*pucDesiredPhyTypeSet) | PHY_TYPE_BIT_ERP) {
            *pucDesiredPhyTypeSet = PHY_TYPE_BIT_ERP;
        }
        else if((*pucDesiredPhyTypeSet) | PHY_TYPE_BIT_OFDM) {
            *pucDesiredPhyTypeSet = PHY_TYPE_BIT_OFDM;
        }

        *pu2DesiredNonHTRateSet = RATE_SET_BIT_12M;
        *pu2BSSBasicRateSet = RATE_SET_BIT_12M;
        *pucMcsSet = 0;
        *pucSupMcs32 = 0;
        *pu2HtCapInfo = 0;
        break;

    case FIXED_RATE_18M:
        if((*pucDesiredPhyTypeSet) | PHY_TYPE_BIT_ERP) {
            *pucDesiredPhyTypeSet = PHY_TYPE_BIT_ERP;
        }
        else if((*pucDesiredPhyTypeSet) | PHY_TYPE_BIT_OFDM) {
            *pucDesiredPhyTypeSet = PHY_TYPE_BIT_OFDM;
        }

        *pu2DesiredNonHTRateSet = RATE_SET_BIT_18M;
        *pu2BSSBasicRateSet = RATE_SET_BIT_18M;
        *pucMcsSet = 0;
        *pucSupMcs32 = 0;
        *pu2HtCapInfo = 0;
        break;

    case FIXED_RATE_24M:
        if((*pucDesiredPhyTypeSet) | PHY_TYPE_BIT_ERP) {
            *pucDesiredPhyTypeSet = PHY_TYPE_BIT_ERP;
        }
        else if((*pucDesiredPhyTypeSet) | PHY_TYPE_BIT_OFDM) {
            *pucDesiredPhyTypeSet = PHY_TYPE_BIT_OFDM;
        }

        *pu2DesiredNonHTRateSet = RATE_SET_BIT_24M;
        *pu2BSSBasicRateSet = RATE_SET_BIT_24M;
        *pucMcsSet = 0;
        *pucSupMcs32 = 0;
        *pu2HtCapInfo = 0;
        break;

    case FIXED_RATE_36M:
        if((*pucDesiredPhyTypeSet) | PHY_TYPE_BIT_ERP) {
            *pucDesiredPhyTypeSet = PHY_TYPE_BIT_ERP;
        }
        else if((*pucDesiredPhyTypeSet) | PHY_TYPE_BIT_OFDM) {
            *pucDesiredPhyTypeSet = PHY_TYPE_BIT_OFDM;
        }

        *pu2DesiredNonHTRateSet = RATE_SET_BIT_36M;
        *pu2BSSBasicRateSet = RATE_SET_BIT_36M;
        *pucMcsSet = 0;
        *pucSupMcs32 = 0;
        *pu2HtCapInfo = 0;
        break;

    case FIXED_RATE_48M:
        if((*pucDesiredPhyTypeSet) | PHY_TYPE_BIT_ERP) {
            *pucDesiredPhyTypeSet = PHY_TYPE_BIT_ERP;
        }
        else if((*pucDesiredPhyTypeSet) | PHY_TYPE_BIT_OFDM) {
            *pucDesiredPhyTypeSet = PHY_TYPE_BIT_OFDM;
        }

        *pu2DesiredNonHTRateSet = RATE_SET_BIT_48M;
        *pu2BSSBasicRateSet = RATE_SET_BIT_48M;
        *pucMcsSet = 0;
        *pucSupMcs32 = 0;
        *pu2HtCapInfo = 0;
        break;

    case FIXED_RATE_54M:
        if((*pucDesiredPhyTypeSet) | PHY_TYPE_BIT_ERP) {
            *pucDesiredPhyTypeSet = PHY_TYPE_BIT_ERP;
        }
        else if((*pucDesiredPhyTypeSet) | PHY_TYPE_BIT_OFDM) {
            *pucDesiredPhyTypeSet = PHY_TYPE_BIT_OFDM;
        }

        *pu2DesiredNonHTRateSet = RATE_SET_BIT_54M;
        *pu2BSSBasicRateSet = RATE_SET_BIT_54M;
        *pucMcsSet = 0;
        *pucSupMcs32 = 0;
        *pu2HtCapInfo = 0;
        break;

    case FIXED_RATE_MCS0_20M_800NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS0_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        break;

    case FIXED_RATE_MCS1_20M_800NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS1_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        break;

    case FIXED_RATE_MCS2_20M_800NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS2_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        break;

    case FIXED_RATE_MCS3_20M_800NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS3_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        break;

    case FIXED_RATE_MCS4_20M_800NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS4_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        break;

    case FIXED_RATE_MCS5_20M_800NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS5_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        break;

    case FIXED_RATE_MCS6_20M_800NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS6_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        break;

    case FIXED_RATE_MCS7_20M_800NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS7_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        break;

    case FIXED_RATE_MCS0_20M_400NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS0_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= HT_CAP_INFO_SHORT_GI_20M;
        break;

    case FIXED_RATE_MCS1_20M_400NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS1_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= HT_CAP_INFO_SHORT_GI_20M;
        break;

    case FIXED_RATE_MCS2_20M_400NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS2_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= HT_CAP_INFO_SHORT_GI_20M;
        break;

    case FIXED_RATE_MCS3_20M_400NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS3_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= HT_CAP_INFO_SHORT_GI_20M;
        break;

    case FIXED_RATE_MCS4_20M_400NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS4_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= HT_CAP_INFO_SHORT_GI_20M;
        break;

    case FIXED_RATE_MCS5_20M_400NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS5_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= HT_CAP_INFO_SHORT_GI_20M;
        break;

    case FIXED_RATE_MCS6_20M_400NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS6_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= HT_CAP_INFO_SHORT_GI_20M;
        break;

    case FIXED_RATE_MCS7_20M_400NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS7_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= HT_CAP_INFO_SHORT_GI_20M;
        break;

    case FIXED_RATE_MCS0_40M_800NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS0_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= HT_CAP_INFO_SUP_CHNL_WIDTH;
        break;

    case FIXED_RATE_MCS1_40M_800NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS1_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= HT_CAP_INFO_SUP_CHNL_WIDTH;
        break;

    case FIXED_RATE_MCS2_40M_800NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS2_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= HT_CAP_INFO_SUP_CHNL_WIDTH;
        break;

    case FIXED_RATE_MCS3_40M_800NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS3_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= HT_CAP_INFO_SUP_CHNL_WIDTH;
        break;

    case FIXED_RATE_MCS4_40M_800NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS4_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= HT_CAP_INFO_SUP_CHNL_WIDTH;
        break;

    case FIXED_RATE_MCS5_40M_800NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS5_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= HT_CAP_INFO_SUP_CHNL_WIDTH;
        break;

    case FIXED_RATE_MCS6_40M_800NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS6_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= HT_CAP_INFO_SUP_CHNL_WIDTH;
        break;

    case FIXED_RATE_MCS7_40M_800NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS7_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= HT_CAP_INFO_SUP_CHNL_WIDTH;
        break;

    case FIXED_RATE_MCS32_800NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS32_INDEX;
        *pucSupMcs32 = 1;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_SHORT_GI_40M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= HT_CAP_INFO_SUP_CHNL_WIDTH;
        break;

    case FIXED_RATE_MCS0_40M_400NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS0_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= (HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_40M);
        break;

    case FIXED_RATE_MCS1_40M_400NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS1_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= (HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_40M);
        break;

    case FIXED_RATE_MCS2_40M_400NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS2_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= (HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_40M);
        break;

    case FIXED_RATE_MCS3_40M_400NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS3_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= (HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_40M);
        break;

    case FIXED_RATE_MCS4_40M_400NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS4_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= (HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_40M);
        break;

    case FIXED_RATE_MCS5_40M_400NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS5_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= (HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_40M);
        break;

    case FIXED_RATE_MCS6_40M_400NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS6_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= (HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_40M);
        break;

    case FIXED_RATE_MCS7_40M_400NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS7_INDEX;
        *pucSupMcs32 = 0;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= (HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_40M);
        break;

    case FIXED_RATE_MCS32_400NS:
        *pucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;
        *pu2DesiredNonHTRateSet = RATE_SET_BIT_HT_PHY;
        *pu2BSSBasicRateSet = RATE_SET_BIT_HT_PHY;
        *pucMcsSet = HT_RATE_MCS32_INDEX;
        *pucSupMcs32 = 1;
        (*pu2HtCapInfo) &= ~(HT_CAP_INFO_SHORT_GI_20M
                | HT_CAP_INFO_HT_GF);
        (*pu2HtCapInfo) |= (HT_CAP_INFO_SUP_CHNL_WIDTH
                | HT_CAP_INFO_SHORT_GI_40M);
        break;

    default:
        ASSERT(0);
    }

    return WLAN_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to write the register
*
* @param u4Address         Register address
*        u4Value           the value to be written
*
* @retval WLAN_STATUS_SUCCESS
*         WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/

WLAN_STATUS
nicWriteMcr (
    IN P_ADAPTER_T  prAdapter,
    IN UINT_32   u4Address,
    IN  UINT_32  u4Value
    )
{
    CMD_ACCESS_REG rCmdAccessReg;

    rCmdAccessReg.u4Address = u4Address;
    rCmdAccessReg.u4Data = u4Value;

    return wlanSendSetQueryCmd(prAdapter,
                CMD_ID_ACCESS_REG,
                TRUE,
                FALSE,
                FALSE,
                NULL,
                NULL,
                sizeof(CMD_ACCESS_REG),
                (PUINT_8)&rCmdAccessReg,
                NULL,
                0
                );

}

/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to modify the auto rate parameters
*
* @param u4ArSysParam0  see description below
*        u4ArSysParam1
*        u4ArSysParam2
*        u4ArSysParam3
*
*
* @retval WLAN_STATUS_SUCCESS
*         WLAN_STATUS_FAILURE
*
* @note
*   ArSysParam0[0:3] -> auto rate version (0:disable 1:version1 2:version2)
*   ArSysParam0[4:5]-> auto bw version (0:disable 1:version1 2:version2)
*   ArSysParam0[6:7]-> auto gi version (0:disable 1:version1 2:version2)
*   ArSysParam0[8:15]-> HT rate clear mask
*   ArSysParam0[16:31]-> Legacy rate clear mask
*   ArSysParam1[0:7]-> Auto Rate check weighting window
*   ArSysParam1[8:15]-> Auto Rate v1 Force Rate down
*   ArSysParam1[16:23]-> Auto Rate v1 PerH
*   ArSysParam1[24:31]-> Auto Rate v1 PerL
*
*   Examples
*   ArSysParam0 = 1,
*   Enable auto rate version 1
*
*   ArSysParam0 = 983041,
*   Enable auto rate version 1
*   Remove CCK 1M, 2M, 5.5M, 11M
*
*   ArSysParam0 = 786433
*   Enable auto rate version 1
*   Remove CCK 5.5M 11M
*/
/*----------------------------------------------------------------------------*/


WLAN_STATUS
nicRlmArUpdateParms(
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4ArSysParam0,
    IN UINT_32 u4ArSysParam1,
    IN UINT_32 u4ArSysParam2,
    IN UINT_32 u4ArSysParam3
    )
{
    UINT_8 ucArVer,ucAbwVer,ucAgiVer;
    UINT_16 u2HtClrMask;
    UINT_16 u2LegacyClrMask;
    UINT_8  ucArCheckWindow;
    UINT_8  ucArPerL;
    UINT_8  ucArPerH;
    UINT_8  ucArPerForceRateDownPer;

    ucArVer = (UINT_8)(u4ArSysParam0 & BITS(0,3));
    ucAbwVer = (UINT_8)((u4ArSysParam0 & BITS(4,5)) >>4);
    ucAgiVer = (UINT_8)((u4ArSysParam0 & BITS(6,7)) >>6);
    u2HtClrMask = (UINT_16) ((u4ArSysParam0 & BITS(8,15)) >> 8);
    u2LegacyClrMask = (UINT_16) ((u4ArSysParam0 & BITS(16,31)) >> 16);

#if 0
    ucArCheckWindow = (UINT_8) (u4ArSysParam1 & BITS(0,7));
    ucArPerForceRateDownPer = (UINT_8) ((u4ArSysParam1 & BITS(8,15)>>8));
    ucArPerH = (UINT_8) ((u4ArSysParam1 & BITS(16,23)) >>16);
    ucArPerL = (UINT_8) ((u4ArSysParam1 & BITS(24,31)) >>24);
#endif

    ucArCheckWindow = (UINT_8) (u4ArSysParam1 & BITS(0,7));
    ucArPerForceRateDownPer = (UINT_8) (((u4ArSysParam1>>8) & BITS(0,7)));
    ucArPerH = (UINT_8) (((u4ArSysParam1>>16) & BITS(0,7)));
    ucArPerL = (UINT_8) (((u4ArSysParam1>>24) & BITS(0,7)));


    DBGLOG(INIT, INFO, ("ArParam %u %u %u %u\n", u4ArSysParam0, u4ArSysParam1, u4ArSysParam2, u4ArSysParam3));
    DBGLOG(INIT, INFO, ("ArVer %u AbwVer %u AgiVer %u\n", ucArVer, ucAbwVer, ucAgiVer));
    DBGLOG(INIT, INFO, ("HtMask %x LegacyMask %x\n", u2HtClrMask, u2LegacyClrMask));
    DBGLOG(INIT, INFO, ("CheckWin %u RateDownPer %u PerH %u PerL %u\n", ucArCheckWindow, ucArPerForceRateDownPer, ucArPerH, ucArPerL));

#define SWCR_DATA_ADDR(MOD,ADDR) (0x90000000+(MOD<<8)+(ADDR))
#define SWCR_DATA_CMD(CATE,WRITE,INDEX,OPT0,OPT1) ( (CATE<<24) | (WRITE<<23) | (INDEX<<16) | (OPT0 <<8) | OPT1  )
#define SWCR_DATA0 0x0
#define SWCR_DATA1 0x4
#define SWCR_DATA2 0x8
#define SWCR_DATA3 0xC
#define SWCR_DATA4 0x10
#define SWCR_WRITE 1
#define SWCR_READ 0

    if(ucArVer > 0) {
       /* dummy = WiFi.WriteMCR(&h90000104, &h00000001) */
       /* dummy = WiFi.WriteMCR(&h90000100, &h00850000)*/

       nicWriteMcr (prAdapter, SWCR_DATA_ADDR(1/*MOD*/,SWCR_DATA1),1);
       nicWriteMcr (prAdapter, SWCR_DATA_ADDR(1/*MOD*/,SWCR_DATA0),SWCR_DATA_CMD(0,SWCR_WRITE,5,0,0));
    }
    else {
       nicWriteMcr (prAdapter, SWCR_DATA_ADDR(1/*MOD*/,SWCR_DATA1),0);
       nicWriteMcr (prAdapter, SWCR_DATA_ADDR(1/*MOD*/,SWCR_DATA0),SWCR_DATA_CMD(0,SWCR_WRITE,5,0,0)) ;
    }

    /* ucArVer 0: none 1:PER 2:Rcpi */
    nicWriteMcr (prAdapter, SWCR_DATA_ADDR(1/*MOD*/,SWCR_DATA1),ucArVer);
    nicWriteMcr (prAdapter, SWCR_DATA_ADDR(1/*MOD*/,SWCR_DATA0),SWCR_DATA_CMD(0,SWCR_WRITE,7,0,0));

    /* Candidate rate Ht mask */
    nicWriteMcr (prAdapter, SWCR_DATA_ADDR(1/*MOD*/,SWCR_DATA1),u2HtClrMask);
    nicWriteMcr (prAdapter, SWCR_DATA_ADDR(1/*MOD*/,SWCR_DATA0),SWCR_DATA_CMD(0,SWCR_WRITE,0x1c,0,0));

    /* Candidate rate legacy mask */
    nicWriteMcr (prAdapter, SWCR_DATA_ADDR(1/*MOD*/,SWCR_DATA1),u2LegacyClrMask);
    nicWriteMcr (prAdapter, SWCR_DATA_ADDR(1/*MOD*/,SWCR_DATA0),SWCR_DATA_CMD(0,SWCR_WRITE,0x1d,0,0));

#if 0
    if(ucArCheckWindow!=0) {
        /* TX DONE MCS INDEX CHECK STA RATE DOWN TH */
        nicWriteMcr (prAdapter, SWCR_DATA_ADDR(1/*MOD*/,SWCR_DATA1),ucArCheckWindow);
        nicWriteMcr (prAdapter, SWCR_DATA_ADDR(1/*MOD*/,SWCR_DATA0),SWCR_DATA_CMD(0,SWCR_WRITE,0x14,0,0));
        nicWriteMcr (prAdapter, SWCR_DATA_ADDR(1/*MOD*/,SWCR_DATA1),ucArCheckWindow);
        nicWriteMcr (prAdapter, SWCR_DATA_ADDR(1/*MOD*/,SWCR_DATA0),SWCR_DATA_CMD(0,SWCR_WRITE,0xc,0,0));
    }

    if(ucArPerForceRateDownPer !=0) {
        nicWriteMcr (prAdapter, SWCR_DATA_ADDR(1/*MOD*/,SWCR_DATA1),ucArPerForceRateDownPer);
        nicWriteMcr (prAdapter, SWCR_DATA_ADDR(1/*MOD*/,SWCR_DATA0),SWCR_DATA_CMD(0,SWCR_WRITE,0x18,0,0));
    }
    if(ucArPerH !=0) {
        nicWriteMcr (prAdapter, SWCR_DATA_ADDR(1/*MOD*/,SWCR_DATA1),ucArPerH);
        nicWriteMcr (prAdapter, SWCR_DATA_ADDR(1/*MOD*/,SWCR_DATA0),SWCR_DATA_CMD(0,SWCR_WRITE,0x1,0,0));
    }
    if(ucArPerL !=0) {
        nicWriteMcr (prAdapter, SWCR_DATA_ADDR(1/*MOD*/,SWCR_DATA1),ucArPerL);
        nicWriteMcr (prAdapter, SWCR_DATA_ADDR(1/*MOD*/,SWCR_DATA0),SWCR_DATA_CMD(0,SWCR_WRITE,0x2,0,0));
    }
#endif



    return WLAN_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*!
* @brief This utility function is used to enable roaming
*
* @param u4EnableRoaming
*
*
* @retval WLAN_STATUS_SUCCESS
*         WLAN_STATUS_FAILURE
*
* @note
*   u4EnableRoaming -> Enable Romaing
*
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicRoamingUpdateParams(
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4EnableRoaming
    )
{
    P_CONNECTION_SETTINGS_T prConnSettings;

    prConnSettings = &(prAdapter->rWifiVar.rConnSettings);
    prConnSettings->fgIsEnableRoaming = ((u4EnableRoaming>0)?(TRUE):(FALSE));

    return WLAN_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief dump firmware Assert message
*
* \param[in]
*           prAdapter
*
* \return
*           TRUE
*           FALSE
*/
/*----------------------------------------------------------------------------*/
VOID
nicPrintFirmwareAssertInfo(
    IN P_ADAPTER_T prAdapter
    )
{
    UINT_32 u4MailBox0, u4MailBox1;
    UINT_32 line = 0;
    UINT_8 aucAssertFile[7];
    UINT_32 u4ChipId;

#if CFG_SDIO_INTR_ENHANCE
    u4MailBox0 = prAdapter->prSDIOCtrl->u4RcvMailbox0;
    u4MailBox1 = prAdapter->prSDIOCtrl->u4RcvMailbox1;
#else
    nicGetMailbox(prAdapter, 0, &u4MailBox0);
    nicGetMailbox(prAdapter, 1, &u4MailBox1);
#endif

    line = u4MailBox0 & 0x0000FFFF;

    u4MailBox0 = ((u4MailBox0 >> 16) & 0x0000FFFF);

    kalMemCopy(&aucAssertFile[0], &u4MailBox0, 2);
    kalMemCopy(&aucAssertFile[2], &u4MailBox1, 4);

    aucAssertFile[6] = '\0';

#if defined(MT6620)
    u4ChipId = 6620;
#elif defined(MT5931)
    u4ChipId = 5931;
#elif defined(MT6628)
    u4ChipId = 6628;
#endif

    kalPrint("\n[MT%ld][wifi][Firmware] Assert at \"%s\" #%ld\n\n",
        u4ChipId,
        aucAssertFile,
		line);

}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to update Link Quality information
*
* @param prAdapter      Pointer of Adapter Data Structure
*        eNetTypeIdx
*        prEventLinkQuality
*        cRssi
*        cLinkQuality
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID
nicUpdateLinkQuality(
    IN P_ADAPTER_T                  prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T    eNetTypeIdx,
    IN P_EVENT_LINK_QUALITY         prEventLinkQuality
    )
{
    ASSERT(prAdapter);
    ASSERT(eNetTypeIdx < NETWORK_TYPE_INDEX_NUM);
    ASSERT(prEventLinkQuality);

    switch(eNetTypeIdx) {
    case NETWORK_TYPE_AIS_INDEX:
        if(prAdapter->rWifiVar.arBssInfo[eNetTypeIdx].eConnectionState == PARAM_MEDIA_STATE_CONNECTED) {
            /* check is to prevent RSSI to be updated by incorrect initial RSSI from hardware */
            /* buffer statistics for further query */
            if(prAdapter->fgIsLinkQualityValid == FALSE
                    || (kalGetTimeTick() - prAdapter->rLinkQualityUpdateTime) > CFG_LINK_QUALITY_VALID_PERIOD) {
                nicUpdateRSSI(prAdapter, eNetTypeIdx, prEventLinkQuality->cRssi, prEventLinkQuality->cLinkQuality);
            }

            if(prAdapter->fgIsLinkRateValid == FALSE
                    || (kalGetTimeTick() - prAdapter->rLinkRateUpdateTime) > CFG_LINK_QUALITY_VALID_PERIOD) {
                nicUpdateLinkSpeed(prAdapter, eNetTypeIdx, prEventLinkQuality->u2LinkSpeed);
            }
        }
        break;
#if CFG_ENABLE_WIFI_DIRECT && CFG_SUPPORT_P2P_RSSI_QUERY
    case NETWORK_TYPE_P2P_INDEX:
        if (prAdapter->fgIsP2pLinkQualityValid == FALSE
                || (kalGetTimeTick() - prAdapter->rP2pLinkQualityUpdateTime) > CFG_LINK_QUALITY_VALID_PERIOD) {
            P_EVENT_LINK_QUALITY_EX prEventLQEx = (P_EVENT_LINK_QUALITY_EX)prEventLinkQuality;

            nicUpdateRSSI(prAdapter, NETWORK_TYPE_P2P_INDEX, prEventLQEx->cRssiP2P, prEventLQEx->cLinkQualityP2P);
        }
        break;
#endif
    default:
        break;

    }

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to update RSSI and Link Quality information
*
* @param prAdapter      Pointer of Adapter Data Structure
*        eNetTypeIdx
*        cRssi
*        cLinkQuality
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID
nicUpdateRSSI(
    IN P_ADAPTER_T                  prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T    eNetTypeIdx,
    IN INT_8                        cRssi,
    IN INT_8                        cLinkQuality
    )
{
    ASSERT(prAdapter);
    ASSERT(eNetTypeIdx < NETWORK_TYPE_INDEX_NUM);

    switch(eNetTypeIdx) {
    case NETWORK_TYPE_AIS_INDEX:
        if(prAdapter->rWifiVar.arBssInfo[eNetTypeIdx].eConnectionState == PARAM_MEDIA_STATE_CONNECTED) {
            prAdapter->fgIsLinkQualityValid = TRUE;
            prAdapter->rLinkQualityUpdateTime = kalGetTimeTick();

            prAdapter->rLinkQuality.cRssi = cRssi;
            prAdapter->rLinkQuality.cLinkQuality = cLinkQuality;

            /* indicate to glue layer */
            kalUpdateRSSI(prAdapter->prGlueInfo,
                    KAL_NETWORK_TYPE_AIS_INDEX,
                    prAdapter->rLinkQuality.cRssi,
                    prAdapter->rLinkQuality.cLinkQuality);
        }

        break;
#if CFG_ENABLE_WIFI_DIRECT
#if CFG_SUPPORT_P2P_RSSI_QUERY
    case NETWORK_TYPE_P2P_INDEX:
        prAdapter->fgIsP2pLinkQualityValid = TRUE;
        prAdapter->rP2pLinkQualityUpdateTime = kalGetTimeTick();

        prAdapter->rP2pLinkQuality.cRssi= cRssi;
        prAdapter->rP2pLinkQuality.cLinkQuality= cLinkQuality;

        kalUpdateRSSI(prAdapter->prGlueInfo,
                KAL_NETWORK_TYPE_P2P_INDEX,
                cRssi,
                cLinkQuality);
        break;
#endif
#endif
    default:
        break;

    }

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to update Link Quality information
*
* @param prAdapter      Pointer of Adapter Data Structure
*        eNetTypeIdx
*        prEventLinkQuality
*        cRssi
*        cLinkQuality
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID
nicUpdateLinkSpeed(
    IN P_ADAPTER_T                  prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T    eNetTypeIdx,
    IN UINT_16                      u2LinkSpeed
    )
{
    ASSERT(prAdapter);
    ASSERT(eNetTypeIdx < NETWORK_TYPE_INDEX_NUM);

    switch(eNetTypeIdx) {
    case NETWORK_TYPE_AIS_INDEX:
        if(prAdapter->rWifiVar.arBssInfo[eNetTypeIdx].eConnectionState == PARAM_MEDIA_STATE_CONNECTED) {
            /* buffer statistics for further query */
            prAdapter->fgIsLinkRateValid = TRUE;
            prAdapter->rLinkRateUpdateTime = kalGetTimeTick();

            prAdapter->rLinkQuality.u2LinkSpeed = u2LinkSpeed;
        }
        break;

    default:
        break;

    }

    return;
}

#if CFG_SUPPORT_RDD_TEST_MODE
WLAN_STATUS
nicUpdateRddTestMode(
    IN P_ADAPTER_T      prAdapter,
    IN P_CMD_RDD_CH_T   prRddChParam
    )
{
    DEBUGFUNC("nicUpdateRddTestMode.\n");

    ASSERT(prAdapter);

//    aisFsmScanRequest(prAdapter, NULL);

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_SET_RDD_CH,
            TRUE,
            FALSE,
            FALSE,
            NULL,
            NULL,
            sizeof(CMD_RDD_CH_T),
            (PUINT_8)prRddChParam,
            NULL,
            0);
}
#endif

