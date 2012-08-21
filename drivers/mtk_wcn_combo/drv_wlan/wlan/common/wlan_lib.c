/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_2/common/wlan_lib.c#4 $
*/
/*! \file   wlan_lib.c
    \brief  Internal driver stack will export the required procedures here for GLUE Layer.

    This file contains all routines which are exported from MediaTek 802.11 Wireless
    LAN driver stack to GLUE Layer.
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
** $Log: wlan_lib.c $
 *
 * 01 20 2012 cp.wu
 * [ALPS00096191] [MT6620 Wi-Fi][Driver][Firmware] Porting to ALPS4.0_DEV branch
 * sync to up-to-date changes including:
 * 1. BOW bugfix
 * 2. XLOG
 *
 * 01 16 2012 cp.wu
 * [WCXRP00001169] [MT6620 Wi-Fi][Driver] API and behavior modification for preferred band configuration with corresponding network configuration
 * correct scan result removing policy.
 *
 * 01 16 2012 cp.wu
 * [MT6620 Wi-Fi][Driver] API and behavior modification for preferred band configuration with corresponding network configuration
 * add wlanSetPreferBandByNetwork() for glue layer to invoke for setting preferred band configuration corresponding to network type.
 *
 * 01 15 2012 yuche.tsai
 * NULL
 * Fix wrong basic rate issue.
 *
 * 01 09 2012 wh.su
 * [WCXRP00001153] [MT6620 Wi-Fi][Driver] Adding the get_ch_list and set_tx_power proto type function
 * Check in the Add Tx power Cmd to driver.
 *
 * 11 28 2011 cp.wu
 * [WCXRP00001125] [MT6620 Wi-Fi][Firmware] Strengthen Wi-Fi power off sequence to have a clearroom environment when returining to ROM code
 * 1. Due to firmware now stops HIF DMA for powering off, do not try to receive any packet from firmware
 * 2. Take use of prAdapter->fgIsEnterD3ReqIssued for tracking whether it is powering off or not
 *
 * 11 14 2011 cm.chang
 * [WCXRP00001104] [All Wi-Fi][FW] Show init process by HW mail-box register
 * Show FW initial ID when timeout to wait for ready bit
 *
 * 11 11 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * modify the xlog related code.
 *
 * 10 25 2011 cp.wu
 * [WCXRP00001057] [MT6620 Wi-Fi][Driver][Firmware] Ensure no pending interrupt status bits remained while switching from RAM code to ROM code
 * 1. [FW] always clear all pending messages while leaving from RAM code section.
 * 2. [DRV] clear interrupt then wait for RDY bit de-assertion
 *
 * 10 19 2011 yuche.tsai
 * [WCXRP00001045] [WiFi Direct][Driver] Check 2.1 branch.
 * Branch 2.1
 * Davinci Maintrunk Label: MT6620_WIFI_DRIVER_FW_TRUNK_MT6620E5_111019_0926.
 *
 * 08 02 2011 yuche.tsai
 * [WCXRP00000896] [Volunteer Patch][WiFi Direct][Driver] GO with multiple client, TX deauth to a disconnecting device issue.
 * Support TX Deauth while tearing down a station connection.
 *
 * 06 23 2011 cp.wu
 * [WCXRP00000812] [MT6620 Wi-Fi][Driver] not show NVRAM when there is no valid MAC address in NVRAM content
 * check with firmware for valid MAC address.
 *
 * 05 31 2011 cp.wu
 * [WCXRP00000749] [MT6620 Wi-Fi][Driver] Add band edge tx power control to Wi-Fi NVRAM
 * changed to use non-zero checking for valid bit in NVRAM content
 *
 * 05 27 2011 cp.wu
 * [WCXRP00000749] [MT6620 Wi-Fi][Driver] Add band edge tx power control to Wi-Fi NVRAM
 * invoke CMD_ID_SET_EDGE_TXPWR_LIMIT when there is valid data exist in NVRAM content.
 *
 * 05 18 2011 cp.wu
 * [WCXRP00000734] [MT6620 Wi-Fi][Driver] Pass PHY_PARAM in NVRAM to firmware domain
 * check-in missed file.
 *
 * 05 11 2011 cp.wu
 * [WCXRP00000718] [MT6620 Wi-Fi] modify the behavior of setting tx power
 * correct assertion.
 *
 * 05 11 2011 cp.wu
 * [WCXRP00000718] [MT6620 Wi-Fi] modify the behavior of setting tx power
 * ACPI APIs migrate to wlan_lib.c for glue layer to invoke.
 *
 * 05 11 2011 cm.chang
 * [WCXRP00000717] [MT5931 Wi-Fi][Driver] Handle wrong NVRAM content about AP bandwidth setting
 * .
 *
 * 04 22 2011 cp.wu
 * [WCXRP00000598] [MT6620 Wi-Fi][Driver] Implementation of interface for communicating with user space process for RESET_START and RESET_END events
 * skip power-off handshaking when RESET indication is received.
 *
 * 04 22 2011 george.huang
 * [WCXRP00000621] [MT6620 Wi-Fi][Driver] Support P2P supplicant to set power mode
 * .
 *
 * 04 15 2011 cp.wu
 * [WCXRP00000654] [MT6620 Wi-Fi][Driver] Add loop termination criterion for wlanAdapterStop().
 * add loop termination criteria for wlanAdapterStop().
 *
 * 04 12 2011 eddie.chen
 * [WCXRP00000617] [MT6620 Wi-Fi][DRV/FW] Fix for sigma
 * Fix the sta index in processing security frame
 * Simple flow control for TC4 to avoid mgt frames for PS STA to occupy the TC4
 * Add debug message.
 *
 * 04 12 2011 cp.wu
 * [WCXRP00000631] [MT6620 Wi-Fi][Driver] Add an API for QM to retrieve current TC counter value and processing frame dropping cases for TC4 path
 * 1. add nicTxGetResource() API for QM to make decisions.
 * 2. if management frames is decided by QM for dropping, the call back is invoked to indicate such a case.
 *
 * 04 06 2011 cp.wu
 * [WCXRP00000616] [MT6620 Wi-Fi][Driver] Free memory to pool and kernel in case any unexpected failure happend inside wlanAdapterStart
 * invoke nicReleaseAdapterMemory() as failure handling in case wlanAdapterStart() failed unexpectedly
 *
 * 03 29 2011 wh.su
 * [WCXRP00000248] [MT6620 Wi-Fi][FW]Fixed the Klockwork error
 * fixed the kclocwork error.
 *
 * 03 18 2011 cp.wu
 * [WCXRP00000577] [MT6620 Wi-Fi][Driver][FW] Create V2.0 branch for firmware and driver
 * create V2.0 driver release based on label "MT6620_WIFI_DRIVER_V2_0_110318_1600" from main trunk
 *
 * 03 15 2011 cp.wu
 * [WCXRP00000559] [MT6620 Wi-Fi][Driver] Combine TX/RX DMA buffers into a single one to reduce physically continuous memory consumption
 * 1. deprecate CFG_HANDLE_IST_IN_SDIO_CALLBACK
 * 2. Use common coalescing buffer for both TX/RX directions
 *
 *
 * 03 10 2011 cp.wu
 * [WCXRP00000532] [MT6620 Wi-Fi][Driver] Migrate NVRAM configuration procedures from MT6620 E2 to MT6620 E3
 * deprecate configuration used by MT6620 E2
 *
 * 03 07 2011 terry.wu
 * [WCXRP00000521] [MT6620 Wi-Fi][Driver] Remove non-standard debug message
 * Toggle non-standard debug messages to comments.
 *
 * 02 25 2011 cp.wu
 * [WCXRP00000496] [MT5931][Driver] Apply host-triggered chip reset before initializing firmware download procedures
 * apply host-triggered chip reset mechanism before initializing firmware download procedures.
 *
 * 02 17 2011 eddie.chen
 * [WCXRP00000458] [MT6620 Wi-Fi][Driver] BOW Concurrent - ProbeResp was exist in other channel
 * 1) Chnage GetFrameAction decision when BSS is absent.
 * 2) Check channel and resource in processing ProbeRequest
 *
 * 02 16 2011 cm.chang
 * [WCXRP00000447] [MT6620 Wi-Fi][FW] Support new NVRAM update mechanism
 * .
 *
 * 02 01 2011 george.huang
 * [WCXRP00000333] [MT5931][FW] support SRAM power control drivers
 * init variable for CTIA.
 *
 * 01 27 2011 george.huang
 * [WCXRP00000355] [MT6620 Wi-Fi] Set WMM-PS related setting with qualifying AP capability
 * Support current measure mode, assigned by registry (XP only).
 *
 * 01 24 2011 cp.wu
 * [WCXRP00000382] [MT6620 Wi-Fi][Driver] Track forwarding packet number with notifying tx thread for serving
 * 1. add an extra counter for tracking pending forward frames.
 * 2. notify TX service thread as well when there is pending forward frame
 * 3. correct build errors leaded by introduction of Wi-Fi direct separation module
 *
 * 01 12 2011 cm.chang
 * [WCXRP00000354] [MT6620 Wi-Fi][Driver][FW] Follow NVRAM bandwidth setting
 * User-defined bandwidth is for 2.4G and 5G individually
 *
 * 01 10 2011 cp.wu
 * [WCXRP00000351] [MT6620 Wi-Fi][Driver] remove from scanning result in OID handling layer when the corresponding BSS is disconnected due to beacon timeout
 * remove from scanning result when the BSS is disconnected due to beacon timeout.
 *
 * 01 04 2011 cp.wu
 * [WCXRP00000338] [MT6620 Wi-Fi][Driver] Separate kalMemAlloc into kmalloc and vmalloc implementations to ease physically continous memory demands
 * separate kalMemAlloc() into virtually-continous and physically-continous type to ease slab system pressure
 *
 * 12 31 2010 cp.wu
 * [WCXRP00000327] [MT6620 Wi-Fi][Driver] Improve HEC WHQA 6972 workaround coverage in driver side
 * while being unloaded, clear all pending interrupt then set LP-own to firmware
 *
 * 12 31 2010 cp.wu
 * [WCXRP00000335] [MT6620 Wi-Fi][Driver] change to use milliseconds sleep instead of delay to avoid blocking to system scheduling
 * change to use msleep() and shorten waiting interval to reduce blocking to other task while Wi-Fi driver is being loaded
 *
 * 12 28 2010 cp.wu
 * [WCXRP00000269] [MT6620 Wi-Fi][Driver][Firmware] Prepare for v1.1 branch release
 * report EEPROM used flag via NIC_CAPABILITY
 *
 * 12 28 2010 cp.wu
 * [WCXRP00000269] [MT6620 Wi-Fi][Driver][Firmware] Prepare for v1.1 branch release
 * integrate with 'EEPROM used' flag for reporting correct capability to Engineer Mode/META and other tools
 *
 * 12 22 2010 eddie.chen
 * [WCXRP00000218] [MT6620 Wi-Fi][Driver] Add auto rate window control in registry
 * Remove controling auto rate from initial setting. The initial setting is defined by FW code.
 *
 * 12 15 2010 cp.wu
 * NULL
 * sync. with ALPS code by enabling interrupt just before leaving wlanAdapterStart()
 *
 * 12 08 2010 yuche.tsai
 * [WCXRP00000245] [MT6620][Driver] Invitation & Provision Discovery Feature Check-in
 * Change Param name for invitation connection.
 *
 * 12 07 2010 cm.chang
 * [WCXRP00000238] MT6620 Wi-Fi][Driver][FW] Support regulation domain setting from NVRAM and supplicant
 * 1. Country code is from NVRAM or supplicant
 * 2. Change band definition in CMD/EVENT.
 *
 * 11 03 2010 cp.wu
 * [WCXRP00000083] [MT5931][Driver][FW] Add necessary logic for MT5931 first connection
 * 1) use 8 buffers for MT5931 which is equipped with less memory
 * 2) modify MT5931 debug level to TRACE when download is successful
 *
 * 11 02 2010 cp.wu
 * [WCXRP00000083] [MT5931][Driver][FW] Add necessary logic for MT5931 first connection
 * for MT5931, adapter initialization is done *after* firmware is downloaded.
 *
 * 11 02 2010 cp.wu
 * [WCXRP00000083] [MT5931][Driver][FW] Add necessary logic for MT5931 first connection
 * correct MT5931 firmware download procedure:
 * MT5931 will download firmware first then acquire LP-OWN
 *
 * 11 02 2010 cp.wu
 * [WCXRP00000083] [MT5931][Driver][FW] Add necessary logic for MT5931 first connection
 * 1) update MT5931 firmware encryption tool. (using 64-bytes unit)
 * 2) update MT5931 firmware download procedure
 *
 * 11 01 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check[WCXRP00000150] [MT6620 Wi-Fi][Driver] Add implementation for querying current TX rate from firmware auto rate module
 * 1) Query link speed (TX rate) from firmware directly with buffering mechanism to reduce overhead
 * 2) Remove CNM CH-RECOVER event handling
 * 3) cfg read/write API renamed with kal prefix for unified naming rules.
 *
 * 11 01 2010 yarco.yang
 * [WCXRP00000149] [MT6620 WI-Fi][Driver]Fine tune performance on MT6516 platform
 * Add code to run WlanIST in SDIO callback.
 *
 * 10 27 2010 george.huang
 * [WCXRP00000127] [MT6620 Wi-Fi][Driver] Add a registry to disable Beacon Timeout function for SQA test by using E1 EVB
 * Support registry option for disable beacon lost detection.
 *
 * 10 26 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check[WCXRP00000137] [MT6620 Wi-Fi] [FW] Support NIC capability query command
 * 1) update NVRAM content template to ver 1.02
 * 2) add compile option for querying NIC capability (default: off)
 * 3) modify AIS 5GHz support to run-time option, which could be turned on by registry or NVRAM setting
 * 4) correct auto-rate compiler error under linux (treat warning as error)
 * 5) simplify usage of NVRAM and REG_INFO_T
 * 6) add version checking between driver and firmware
 *
 * 10 26 2010 eddie.chen
 * [WCXRP00000134] [MT6620 Wi-Fi][Driver] Add a registry to enable auto rate for SQA test by using E1 EVB
 * Add auto rate parameter in registry.
 *
 * 10 25 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check
 * add option for enable/disable TX PWR gain adjustment (default: off)
 *
 * 10 18 2010 cp.wu
 * [WCXRP00000117] [MT6620 Wi-Fi][Driver] Add logic for suspending driver when MT6620 is not responding anymore
 * 1. when wlanAdapterStop() failed to send POWER CTRL command to firmware, do not poll for ready bit dis-assertion
 * 2. shorten polling count for shorter response time
 * 3. if bad I/O operation is detected during TX resource polling, then further operation is aborted as well
 *
 * 10 18 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check[WCXRP00000086] [MT6620 Wi-Fi][Driver] The mac address is all zero at android
 * complete implementation of Android NVRAM access
 *
 * 10 15 2010 cp.wu
 * [WCXRP00000103] [MT6620 Wi-Fi][Driver] Driver crashed when using WZC to connect to AP#B with connection with AP#A
 * bugfix: always reset pointer to IEbuf to zero when keeping scanning result for the connected AP
 *
 * 10 08 2010 cp.wu
 * [WCXRP00000084] [MT6620 Wi-Fi][Driver][FW] Add fixed rate support for distance test
 * adding fixed rate support for distance test. (from registry setting)
 *
 * 10 07 2010 cp.wu
 * [WCXRP00000083] [MT5931][Driver][FW] Add necessary logic for MT5931 first connection
 * add firmware download for MT5931.
 *
 * 10 06 2010 cp.wu
 * [WCXRP00000052] [MT6620 Wi-Fi][Driver] Eliminate Linux Compile Warning
 * divide a single function into 2 part to surpress a weird compiler warning from gcc-4.4.0
 *
 * 10 06 2010 cp.wu
 * [WCXRP00000052] [MT6620 Wi-Fi][Driver] Eliminate Linux Compile Warning
 * code reorganization to improve isolation between GLUE and CORE layers.
 *
 * 10 05 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check
 * load manufacture data when CFG_SUPPORT_NVRAM is set to 1
 *
 * 10 04 2010 cp.wu
 * [WCXRP00000077] [MT6620 Wi-Fi][Driver][FW] Eliminate use of ENUM_NETWORK_TYPE_T and replaced by ENUM_NETWORK_TYPE_INDEX_T only
 * remove ENUM_NETWORK_TYPE_T definitions
 *
 * 09 29 2010 wh.su
 * [WCXRP00000072] [MT6620 Wi-Fi][Driver] Fix TKIP Counter Measure EAPoL callback register issue
 * [MT6620 Wi-Fi][Driver] Fix TKIP Counter Measure EAPoL callback register issue.
 *
 * 09 24 2010 cp.wu
 * [WCXRP00000052] [MT6620 Wi-Fi][Driver] Eliminate Linux Compile Warning
 * eliminate unused variables which lead gcc to argue
 *
 * 09 24 2010 cp.wu
 * [WCXRP00000057] [MT6620 Wi-Fi][Driver] Modify online scan to a run-time switchable feature
 * Modify online scan as a run-time adjustable option (for Windows, in registry)
 *
 * 09 23 2010 cp.wu
 * [WCXRP00000051] [MT6620 Wi-Fi][Driver] WHQL test fail in MAC address changed item
 * use firmware reported mac address right after wlanAdapterStart() as permanent address
 *
 * 09 23 2010 cp.wu
 * [WCXRP00000052] [MT6620 Wi-Fi][Driver] Eliminate Linux Compile Warning
 * eliminate reference of CFG_RESPONSE_MAX_PKT_SIZE
 *
 * 09 21 2010 cp.wu
 * [WCXRP00000053] [MT6620 Wi-Fi][Driver] Reset incomplete and might leads to BSOD when entering RF test with AIS associated
 * Do a complete reset with STA-REC null checking for RF test re-entry
 *
 * 09 21 2010 kevin.huang
 * [WCXRP00000052] [MT6620 Wi-Fi][Driver] Eliminate Linux Compile Warning
 * Eliminate Linux Compile Warning
 *
 * 09 13 2010 cp.wu
 * NULL
 * acquire & release power control in oid handing wrapper.
 *
 * 09 09 2010 cp.wu
 * NULL
 * move IE to buffer head when the IE pointer is not pointed at head.
 *
 * 09 08 2010 cp.wu
 * NULL
 * use static memory pool for storing IEs of scanning result.
 *
 * 09 01 2010 cp.wu
 * NULL
 * HIFSYS Clock Source Workaround
 *
 * 09 01 2010 wh.su
 * NULL
 * adding the wapi support for integration test.
 *
 * 09 01 2010 cp.wu
 * NULL
 * move HIF CR initialization from where after sdioSetupCardFeature() to wlanAdapterStart()
 *
 * 08 30 2010 cp.wu
 * NULL
 * eliminate klockwork errors
 *
 * 08 26 2010 yuche.tsai
 * NULL
 * Add AT GO test configure mode under WinXP.
 * Please enable 1. CFG_ENABLE_WIFI_DIRECT, 2. CFG_TEST_WIFI_DIRECT_GO, 3. CFG_SUPPORT_AAA
 *
 * 08 25 2010 george.huang
 * NULL
 * update OID/ registry control path for PM related settings
 *
 * 08 24 2010 cp.wu
 * NULL
 * 1) initialize variable for enabling short premable/short time slot.
 * 2) add compile option for disabling online scan
 *
 * 08 13 2010 cp.wu
 * NULL
 * correction issue: desired phy type not initialized as ABGN mode.
 *
 * 08 12 2010 cp.wu
 * NULL
 * [AIS-FSM] honor registry setting for adhoc running mode. (A/B/G)
 *
 * 08 10 2010 cm.chang
 * NULL
 * Support EEPROM read/write in RF test mode
 *
 * 08 03 2010 cp.wu
 * NULL
 * surpress compilation warning.
 *
 * 08 03 2010 cp.wu
 * NULL
 * Centralize mgmt/system service procedures into independent calls.
 *
 * 07 30 2010 cp.wu
 * NULL
 * 1) BoW wrapper: use definitions instead of hard-coded constant for error code
 * 2) AIS-FSM: eliminate use of desired RF parameters, use prTargetBssDesc instead
 * 3) add handling for RX_PKT_DESTINATION_HOST_WITH_FORWARD for GO-broadcast frames
 *
 * 07 29 2010 cp.wu
 * NULL
 * eliminate u4FreqInKHz usage, combined into rConnections.ucAdHoc*
 *
 * 07 28 2010 cp.wu
 * NULL
 * 1) eliminate redundant variable eOPMode in prAdapter->rWlanInfo
 * 2) change nicMediaStateChange() API prototype
 *
 * 07 21 2010 cp.wu
 *
 * 1) change BG_SCAN to ONLINE_SCAN for consistent term
 * 2) only clear scanning result when scan is permitted to do
 *
 * 07 19 2010 cm.chang
 *
 * Set RLM parameters and enable CNM channel manager
 *
 * 07 19 2010 jeffrey.chang
 *
 * Linux port modification
 *
 * 07 13 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * Reduce unnecessary type casting
 *
 * 07 13 2010 cp.wu
 *
 * use multiple queues to keep 1x/MMPDU/CMD's strict order even when there is incoming 1x frames.
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
 * 07 05 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) ignore RSN checking when RSN is not turned on.
 * 2) set STA-REC deactivation callback as NULL
 * 3) add variable initialization API based on PHY configuration
 *
 * 07 02 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) for event packet, no need to fill RFB.
 * 2) when wlanAdapterStart() failed, no need to initialize state machines
 * 3) after Beacon/ProbeResp parsing, corresponding BSS_DESC_T should be marked as IE-parsed
 *
 * 07 01 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Support sync command of STA_REC
 *
 * 07 01 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add scan uninitialization procedure
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
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * initialize mbox & ais_fsm in wlanAdapterStart()
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * change MAC address updating logic.
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * simplify timer usage.
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
 * 06 08 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * cnm_timer has been migrated.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 05 28 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * disable interrupt then send power control command packet.
 *
 * 05 24 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) when stopping adapter, wait til RDY bit has been cleaerd.
 * 2) set TASK_OFFLOAD as driver-core OIDs
 *
 * 05 20 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) integrate OID_GEN_NETWORK_LAYER_ADDRESSES with CMD_ID_SET_IP_ADDRESS
 * 2) buffer statistics data for 2 seconds
 * 3) use default value for adhoc parameters instead of 0
 *
 * 05 19 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) do not take timeout mechanism for power mode oids
 * 2) retrieve network type from connection status
 * 3) after disassciation, set radio state to off
 * 4) TCP option over IPv6 is supported
 *
 * 05 17 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add CFG_STARTUP_DEBUG for debugging starting up issue.
 *
 * 05 17 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * 1) add timeout handler mechanism for pending command packets
 * 2) add p2p add/removal key
 *
 * 04 23 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * surpress compiler warning
 *
 * 04 20 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * roll-back to rev.60.
 *
 * 04 20 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) remove redundant firmware image unloading
 * 2) use compile-time macros to separate logic related to accquiring own
 *
 * 04 16 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * treat BUS access failure as kind of card removal.
 *
 * 04 14 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * always set fw-own before driver is unloaded.
 *
 * 04 13 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support
 * add framework for BT-over-Wi-Fi support.
 *  *  * 1) prPendingCmdInfo is replaced by queue for multiple handler capability
 *  *  * 2) command sequence number is now increased atomically
 *  *  * 3) private data could be hold and taken use for other purpose
 *
 * 04 07 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * finish non-glue layer access to glue variables
 *
 * 04 07 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * rWlanInfo should be placed at adapter rather than glue due to most operations
 * are done in adapter layer.
 *
 * 04 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * ePowerCtrl is not necessary as a glue variable.
 *
 * 04 06 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * add timeout check in the kalOidComplete
 *
 * 04 06 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * improve none-glue code portability
 *
 * 04 06 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * improve none-glue code portability
 *
 * 04 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * code refine: fgTestMode should be at adapter rather than glue due to the device/fw is also involved
 *
 * 04 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * eliminate direct access for prGlueInfo->fgIsCardRemoved in non-glue layer
 *
 * 04 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) for some OID, never do timeout expiration
 * 2) add 2 kal API for later integration
 *
 * 04 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) eliminate unused definitions
 * 2) ready bit will be polled for limited iteration
 *
 * 04 06 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * kalOidComplete is not necessary in linux
 *
 * 04 01 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * change to use pass-in prRegInfo instead of accessing prGlueInfo directly
 *
 * 04 01 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * change to use WIFI_TCM_ALWAYS_ON as firmware image
 *
 * 04 01 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * .
 *
 * 03 31 2010 wh.su
 * [WPD00003816][MT6620 Wi-Fi] Adding the security support
 * modify the wapi related code for new driver's design.
 *
 * 03 30 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * adding none-glue code portability
 *
 * 03 30 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * adding non-glue code portability
 *
 * 03 29 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * improve non-glue code portability
 *
 * 03 25 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * firmware download load adress & start address are now configured from config.h
 * due to the different configurations on FPGA and ASIC
 *
 * 03 24 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * [WPD00003826] Initial import for Linux port
 * initial import for Linux port
 *
 * 03 24 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * initial import for Linux port
 *
 * 03 22 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * only send CMD_NIC_POWER_CTRL in wlanAdapterStop() when card is not removed and is not in D3 state
 *
 * 03 22 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * always send CMD_NIC_POWER_CTRL packet when nic is being halted
 *
 * 03 19 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) add ACPI D0/D3 state switching support
 * 2) use more formal way to handle interrupt when the status is retrieved from enhanced RX response
 *
* 03 12 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add two option for ACK and ENCRYPTION for firmware download
 *
 * 03 11 2010 cp.wu
 * [WPD00003821][BUG] Host driver stops processing RX packets from HIF RX0
 * add RX starvation warning debug message controlled by CFG_HIF_RX_STARVATION_WARNING
 *
 * 03 08 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) add another spin-lock to protect MsduInfoList due to it might be accessed by different thread.
 * 2) change own-back acquiring procedure to wait for up to 16.67 seconds
 *
 * 03 03 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * when starting adapter, read local adminsitrated address from registry and send to firmware via CMD_BASIC_CONFIG.
 *
 * 03 02 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) the use of prPendingOid revised, all accessing are now protected by spin lock
 * 2) ensure wlanReleasePendingOid will clear all command queues
 *
 * 03 02 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add mutex to avoid multiple access to qmTxQueue simultaneously.
 *
 * 03 01 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add command/event definitions for initial states
 *
 * 02 24 2010 tehuang.liu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * Added code for QM_TEST_MODE
 *
 * 02 24 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * correct function name ..
 *
 * 02 24 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * separate wlanProcesQueuePacket() into 2 APIs upon request
 *
 * 02 23 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add new API: wlanProcessQueuedPackets()
 *
 * 02 11 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * correct wlanAdapterStart
 *
 * 02 11 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1. add logic for firmware download
 * 2. firmware image filename and start/load address are now retrieved from registry
 *
 * 02 10 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * implement host-side firmware download logic
 *
 * 02 10 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) remove unused function in nic_rx.c [which has been handled in que_mgt.c]
 * 2) firmware image length is now retrieved via NdisFileOpen
 * 3) firmware image is not structured by (P_IMG_SEC_HDR_T) anymore
 * 4) nicRxWaitResponse() revised
 * 5) another set of TQ counter default value is added for fw-download state
 * 6) Wi-Fi load address is now retrieved from registry too
 *
 * 02 09 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1. Permanent and current MAC address are now retrieved by CMD/EVENT packets instead of hard-coded address
 * 2. follow MSDN defined behavior when associates to another AP
 * 3. for firmware download, packet size could be up to 2048 bytes
 *
 * 02 08 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * prepare for implementing fw download logic
 *
 * 02 03 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * wlanoidSetFrequency is now implemented by RF test command.
 *
 * 02 03 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * QueryRssi is no longer w/o hardware access, it is now implemented by command/event handling loop
 *
 * 02 03 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1. clear prPendingCmdInfo properly
 * 2. while allocating memory for cmdinfo, no need to add extra 4 bytes.
 *
 * 01 28 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * allow MCR read/write OIDs in RF test mode
 *
 * 01 27 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) implement timeout mechanism when OID is pending for longer than 1 second
 * 2) allow OID_802_11_CONFIGURATION to be executed when RF test mode is turned on
 *
 * 01 27 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1. eliminate improper variable in rHifInfo
 * 2. block TX/ordinary OID when RF test mode is engaged
 * 3. wait until firmware finish operation when entering into and leaving from RF test mode
 * 4. correct some HAL implementation
 *
 * 01 26 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * Under WinXP with SDIO, use prGlueInfo->rHifInfo.pvInformationBuffer instead of prGlueInfo->pvInformationBuffer
**  \main\maintrunk.MT6620WiFiDriver_Prj\36 2009-12-10 16:54:36 GMT mtk02752
**  code clean
**  \main\maintrunk.MT6620WiFiDriver_Prj\35 2009-12-09 20:04:59 GMT mtk02752
**  only report as connected when CFG_HIF_EMULATION_TEST is set to 1
**  \main\maintrunk.MT6620WiFiDriver_Prj\34 2009-12-08 17:39:41 GMT mtk02752
**  wlanoidRftestQueryAutoTest could be executed without touching hardware
**  \main\maintrunk.MT6620WiFiDriver_Prj\33 2009-12-03 16:10:26 GMT mtk01461
**  Add debug message
**  \main\maintrunk.MT6620WiFiDriver_Prj\32 2009-12-02 22:05:33 GMT mtk02752
**  kalOidComplete() will decrease i4OidPendingCount
**  \main\maintrunk.MT6620WiFiDriver_Prj\31 2009-12-01 23:02:36 GMT mtk02752
**  remove unnecessary spinlock
**  \main\maintrunk.MT6620WiFiDriver_Prj\30 2009-12-01 22:50:38 GMT mtk02752
**  use TC4 for command, maintein i4OidPendingCount
**  \main\maintrunk.MT6620WiFiDriver_Prj\29 2009-11-27 12:45:34 GMT mtk02752
**  prCmdInfo should be freed when invoking wlanReleasePendingOid() to clear pending oid
**  \main\maintrunk.MT6620WiFiDriver_Prj\28 2009-11-24 19:55:51 GMT mtk02752
**  wlanSendPacket & wlanRetransmitOfPendingFrames is only used in old data path
**  \main\maintrunk.MT6620WiFiDriver_Prj\27 2009-11-23 17:59:55 GMT mtk02752
**  clear prPendingOID inside wlanSendCommand() when the OID didn't need to be replied.
**  \main\maintrunk.MT6620WiFiDriver_Prj\26 2009-11-23 14:45:29 GMT mtk02752
**  add another version of wlanSendCommand() for command-sending only without blocking for response
**  \main\maintrunk.MT6620WiFiDriver_Prj\25 2009-11-17 22:40:44 GMT mtk01084
**  \main\maintrunk.MT6620WiFiDriver_Prj\24 2009-11-11 10:14:56 GMT mtk01084
**  modify place to invoke wlanIst
**  \main\maintrunk.MT6620WiFiDriver_Prj\23 2009-10-30 18:17:07 GMT mtk01084
**  fix compiler warning
**  \main\maintrunk.MT6620WiFiDriver_Prj\22 2009-10-29 19:46:15 GMT mtk01084
**  invoke interrupt process routine
**  \main\maintrunk.MT6620WiFiDriver_Prj\21 2009-10-13 21:58:24 GMT mtk01084
**  modify for new HW architecture
**  \main\maintrunk.MT6620WiFiDriver_Prj\20 2009-09-09 17:26:01 GMT mtk01084
**  \main\maintrunk.MT6620WiFiDriver_Prj\19 2009-05-20 12:21:27 GMT mtk01461
**  Add SeqNum check when process Event Packet
**  \main\maintrunk.MT6620WiFiDriver_Prj\18 2009-05-19 10:38:44 GMT mtk01461
**  Add wlanReleasePendingOid() for mpReset() if there is a pending OID and no available TX resource to send it.
**  \main\maintrunk.MT6620WiFiDriver_Prj\17 2009-04-29 15:41:34 GMT mtk01461
**  Add handle of EVENT of CMD Result in wlanSendCommand()
**  \main\maintrunk.MT6620WiFiDriver_Prj\16 2009-04-22 09:11:23 GMT mtk01461
**  Fix wlanSendCommand() for Driver Domain CR
**  \main\maintrunk.MT6620WiFiDriver_Prj\15 2009-04-21 09:33:56 GMT mtk01461
**  Update wlanSendCommand() for Driver Domain Response and handle Event Packet, wlanQuery/SetInformation() for enqueue CMD_INFO_T
**  \main\maintrunk.MT6620WiFiDriver_Prj\14 2009-04-17 20:00:08 GMT mtk01461
**  Update wlanImageSectionDownload for optimized CMD process
**  \main\maintrunk.MT6620WiFiDriver_Prj\13 2009-04-14 20:50:51 GMT mtk01426
**  Fixed compile error
**  \main\maintrunk.MT6620WiFiDriver_Prj\12 2009-04-13 16:38:40 GMT mtk01084
**  add wifi start function
**  \main\maintrunk.MT6620WiFiDriver_Prj\11 2009-04-13 14:26:44 GMT mtk01084
**  modify a parameter about FW download length
**  \main\maintrunk.MT6620WiFiDriver_Prj\10 2009-04-10 21:53:42 GMT mtk01461
**  Update wlanSendCommand()
**  \main\maintrunk.MT6620WiFiDriver_Prj\9 2009-04-08 16:51:04 GMT mtk01084
**  Update for the image download part
**  \main\maintrunk.MT6620WiFiDriver_Prj\8 2009-04-01 10:32:47 GMT mtk01461
**  Add wlanSendLeftClusteredFrames() for SDIO_TX_ENHANCE
**  \main\maintrunk.MT6620WiFiDriver_Prj\7 2009-03-23 21:44:13 GMT mtk01461
**  Refine TC assignment for WmmAssoc flag
**  \main\maintrunk.MT6620WiFiDriver_Prj\6 2009-03-23 16:51:57 GMT mtk01084
**  modify the input argument of caller to RECLAIM_POWER_CONTROL_TO_PM()
**  \main\maintrunk.MT6620WiFiDriver_Prj\5 2009-03-23 00:27:13 GMT mtk01461
**  Add reference code of FW Image Download
**  \main\maintrunk.MT6620WiFiDriver_Prj\4 2009-03-19 18:32:37 GMT mtk01084
**  update for basic power management functions
**  \main\maintrunk.MT6620WiFiDriver_Prj\3 2009-03-16 09:09:08 GMT mtk01461
**  Update TX PATH API
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-03-10 16:28:45 GMT mtk01426
**  Init develop
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
#include "mgmt/ais_fsm.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
/* 6.1.1.2 Interpretation of priority parameter in MAC service primitives */
/* Static convert the Priority Parameter/TID(User Priority/TS Identifier) to Traffic Class */
const UINT_8 aucPriorityParam2TC[] = {
    TC1_INDEX,
    TC0_INDEX,
    TC0_INDEX,
    TC1_INDEX,
    TC2_INDEX,
    TC2_INDEX,
    TC3_INDEX,
    TC3_INDEX
};

#if QM_TEST_MODE
extern QUE_MGT_T g_rQM;
#endif
/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/
BOOLEAN fgIsBusAccessFailed = FALSE;

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/
// TODO: Check
/* OID set handlers without the need to access HW register */
PFN_OID_HANDLER_FUNC apfnOidSetHandlerWOHwAccess[] = {
    wlanoidSetChannel,
    wlanoidSetBeaconInterval,
    wlanoidSetAtimWindow,
    wlanoidSetFrequency,
};

// TODO: Check
/* OID query handlers without the need to access HW register */
PFN_OID_HANDLER_FUNC apfnOidQueryHandlerWOHwAccess[] = {
    wlanoidQueryBssid,
    wlanoidQuerySsid,
    wlanoidQueryInfrastructureMode,
    wlanoidQueryAuthMode,
    wlanoidQueryEncryptionStatus,
    wlanoidQueryPmkid,
    wlanoidQueryNetworkTypeInUse,
    wlanoidQueryBssidList,
    wlanoidQueryAcpiDevicePowerState,
    wlanoidQuerySupportedRates,
    wlanoidQueryDesiredRates,
    wlanoidQuery802dot11PowerSaveProfile,
    wlanoidQueryBeaconInterval,
    wlanoidQueryAtimWindow,
    wlanoidQueryFrequency,
};

/* OID set handlers allowed in RF test mode */
PFN_OID_HANDLER_FUNC apfnOidSetHandlerAllowedInRFTest[] = {
    wlanoidRftestSetTestMode,
    wlanoidRftestSetAbortTestMode,
    wlanoidRftestSetAutoTest,
    wlanoidSetMcrWrite,
    wlanoidSetEepromWrite
};

/* OID query handlers allowed in RF test mode */
PFN_OID_HANDLER_FUNC apfnOidQueryHandlerAllowedInRFTest[] = {
    wlanoidRftestQueryAutoTest,
    wlanoidQueryMcrRead,
    wlanoidQueryEepromRead
}
;

PFN_OID_HANDLER_FUNC apfnOidWOTimeoutCheck[] = {
    wlanoidRftestSetTestMode,
    wlanoidRftestSetAbortTestMode,
    wlanoidSetAcpiDevicePowerState,
};


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
* \brief This is a private routine, which is used to check if HW access is needed
*        for the OID query/ set handlers.
*
* \param[IN] pfnOidHandler Pointer to the OID handler.
* \param[IN] fgSetInfo     It is a Set information handler.
*
* \retval TRUE This function needs HW access
* \retval FALSE This function does not need HW access
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
wlanIsHandlerNeedHwAccess (
    IN PFN_OID_HANDLER_FUNC pfnOidHandler,
    IN BOOLEAN              fgSetInfo
    )
{
    PFN_OID_HANDLER_FUNC* apfnOidHandlerWOHwAccess;
    UINT_32 i;
    UINT_32 u4NumOfElem;

    if (fgSetInfo) {
        apfnOidHandlerWOHwAccess = apfnOidSetHandlerWOHwAccess;
        u4NumOfElem = sizeof(apfnOidSetHandlerWOHwAccess) / sizeof(PFN_OID_HANDLER_FUNC);
    }
    else {
        apfnOidHandlerWOHwAccess = apfnOidQueryHandlerWOHwAccess;
        u4NumOfElem = sizeof(apfnOidQueryHandlerWOHwAccess) / sizeof(PFN_OID_HANDLER_FUNC);
    }

    for (i = 0; i < u4NumOfElem; i++) {
        if (apfnOidHandlerWOHwAccess[i] == pfnOidHandler) {
            return FALSE;
        }
    }

    return TRUE;
}   /* wlanIsHandlerNeedHwAccess */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set flag for later handling card
*        ejected event.
*
* \param[in] prAdapter Pointer to the Adapter structure.
*
* \return (none)
*
* \note When surprised removal happens, Glue layer should invoke this
*       function to notify WPDD not to do any hw access.
*/
/*----------------------------------------------------------------------------*/
VOID
wlanCardEjected (
    IN P_ADAPTER_T         prAdapter
    )
{
    DEBUGFUNC("wlanCardEjected");
    //INITLOG(("\n"));

    ASSERT(prAdapter);

     /* mark that the card is being ejected, NDIS will shut us down soon */
    nicTxRelease(prAdapter);

} /* wlanCardEjected */


/*----------------------------------------------------------------------------*/
/*!
* \brief Create adapter object
*
* \param prAdapter This routine is call to allocate the driver software objects.
*                  If fails, return NULL.
* \retval NULL If it fails, NULL is returned.
* \retval NOT NULL If the adapter was initialized successfully.
*/
/*----------------------------------------------------------------------------*/
P_ADAPTER_T
wlanAdapterCreate (
    IN P_GLUE_INFO_T prGlueInfo
    )
{
    P_ADAPTER_T prAdpater = (P_ADAPTER_T)NULL;

    DEBUGFUNC("wlanAdapterCreate");

    do {
        prAdpater = (P_ADAPTER_T) kalMemAlloc(sizeof(ADAPTER_T), VIR_MEM_TYPE);

        if (!prAdpater) {
            DBGLOG(INIT, ERROR, ("Allocate ADAPTER memory ==> FAILED\n"));
            break;
        }

#if QM_TEST_MODE
        g_rQM.prAdapter = prAdpater;
#endif
        kalMemZero(prAdpater, sizeof(ADAPTER_T));
        prAdpater->prGlueInfo = prGlueInfo;

    } while(FALSE);

    return prAdpater;
} /* wlanAdapterCreate */


/*----------------------------------------------------------------------------*/
/*!
* \brief Destroy adapter object
*
* \param prAdapter This routine is call to destroy the driver software objects.
*                  If fails, return NULL.
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
wlanAdapterDestroy (
    IN P_ADAPTER_T prAdapter
    )
{

    if (!prAdapter) {
        return;
    }

    kalMemFree(prAdapter, VIR_MEM_TYPE, sizeof(ADAPTER_T));

    return;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief Initialize the adapter. The sequence is
*        1. Disable interrupt
*        2. Read adapter configuration from EEPROM and registry, verify chip ID.
*        3. Create NIC Tx/Rx resource.
*        4. Initialize the chip
*        5. Initialize the protocol
*        6. Enable Interrupt
*
* \param prAdapter      Pointer of Adapter Data Structure
*
* \retval WLAN_STATUS_SUCCESS: Success
* \retval WLAN_STATUS_FAILURE: Failed
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanAdapterStart (
    IN P_ADAPTER_T  prAdapter,
    IN P_REG_INFO_T prRegInfo,
    IN PVOID        pvFwImageMapFile,
    IN UINT_32      u4FwImageFileLength
    )
{
    WLAN_STATUS u4Status = WLAN_STATUS_SUCCESS;
    UINT_32     i, u4Value = 0;
    UINT_32     u4WHISR = 0;
    UINT_8      aucTxCount[8];
#if CFG_ENABLE_FW_DOWNLOAD
    UINT_32     u4FwLoadAddr, u4ImgSecSize;
    #if CFG_ENABLE_FW_DIVIDED_DOWNLOAD
    UINT_32     j;
    P_FIRMWARE_DIVIDED_DOWNLOAD_T prFwHead;
    BOOLEAN fgValidHead;
    const UINT_32 u4CRCOffset = offsetof(FIRMWARE_DIVIDED_DOWNLOAD_T, u4NumOfEntries);
    #endif
#endif
#if (defined(MT5931) && (!CFG_SUPPORT_BCM_BWCS))
    PARAM_PTA_IPC_T rBwcsPta;
    UINT_32 u4SetInfoLen;
#endif

    ASSERT(prAdapter);

    DEBUGFUNC("wlanAdapterStart");

    //4 <0> Reset variables in ADAPTER_T
    prAdapter->fgIsFwOwn = TRUE;
    prAdapter->fgIsEnterD3ReqIssued = FALSE;

    QUEUE_INITIALIZE(&(prAdapter->rPendingCmdQueue));

    /* Initialize rWlanInfo */
    kalMemSet(&(prAdapter->rWlanInfo), 0, sizeof(WLAN_INFO_T));

    //4 <0.1> reset fgIsBusAccessFailed
    fgIsBusAccessFailed = FALSE;

    do {
        if ( (u4Status = nicAllocateAdapterMemory(prAdapter)) != WLAN_STATUS_SUCCESS ) {
            DBGLOG(INIT, ERROR, ("nicAllocateAdapterMemory Error!\n"));
            u4Status = WLAN_STATUS_FAILURE;
            break;
        }

        prAdapter->u4OsPacketFilter = PARAM_PACKET_FILTER_SUPPORTED;

#if defined(MT6620) || defined(MT6628)
        DBGLOG(INIT, TRACE, ("wlanAdapterStart(): Acquiring LP-OWN\n"));
        ACQUIRE_POWER_CONTROL_FROM_PM(prAdapter);
    #if !CFG_ENABLE_FULL_PM
        nicpmSetDriverOwn(prAdapter);
    #endif

        if(prAdapter->fgIsFwOwn == TRUE) {
            DBGLOG(INIT, ERROR, ("nicpmSetDriverOwn() failed!\n"));
            u4Status = WLAN_STATUS_FAILURE;
            break;
        }

        //4 <1> Initialize the Adapter
        if ( (u4Status = nicInitializeAdapter(prAdapter)) != WLAN_STATUS_SUCCESS ) {
            DBGLOG(INIT, ERROR, ("nicInitializeAdapter failed!\n"));
            u4Status = WLAN_STATUS_FAILURE;
            break;
        }
#endif

        //4 <2> Initialize System Service (MGMT Memory pool and STA_REC)
        nicInitSystemService(prAdapter);

        //4 <3> Initialize Tx
        nicTxInitialize(prAdapter);
        wlanDefTxPowerCfg(prAdapter);

        //4 <4> Initialize Rx
        nicRxInitialize(prAdapter);

#if CFG_ENABLE_FW_DOWNLOAD
    #if defined(MT6620) || defined(MT6628)
        if (pvFwImageMapFile) {
            /* 1. disable interrupt, download is done by polling mode only */
            nicDisableInterrupt(prAdapter);

            /* 2. Initialize Tx Resource to fw download state */
            nicTxInitResetResource(prAdapter);

            /* 3. FW download here */
            u4FwLoadAddr = prRegInfo->u4LoadAddress;

        #if CFG_ENABLE_FW_DIVIDED_DOWNLOAD
            // 3a. parse file header for decision of divided firmware download or not
            prFwHead = (P_FIRMWARE_DIVIDED_DOWNLOAD_T)pvFwImageMapFile;

            if(prFwHead->u4Signature == MTK_WIFI_SIGNATURE &&
                    prFwHead->u4CRC == wlanCRC32((PUINT_8)pvFwImageMapFile + u4CRCOffset, u4FwImageFileLength - u4CRCOffset)) {
                fgValidHead = TRUE;
            }
            else {
                fgValidHead = FALSE;
            }

            /* 3b. engage divided firmware downloading */
            if(fgValidHead == TRUE) {
                for(i = 0 ; i < prFwHead->u4NumOfEntries ; i++) {
            #if CFG_ENABLE_FW_DOWNLOAD_AGGREGATION
                    if(wlanImageSectionDownloadAggregated(prAdapter,
                                prFwHead->arSection[i].u4DestAddr,
                                prFwHead->arSection[i].u4Length,
                                (PUINT_8)pvFwImageMapFile + prFwHead->arSection[i].u4Offset) != WLAN_STATUS_SUCCESS) {
                        DBGLOG(INIT, ERROR, ("Firmware scatter download failed!\n"));
                        u4Status = WLAN_STATUS_FAILURE;
                    }
            #else
                    for(j = 0 ; j < prFwHead->arSection[i].u4Length ; j += CMD_PKT_SIZE_FOR_IMAGE) {
                        if(j + CMD_PKT_SIZE_FOR_IMAGE < prFwHead->arSection[i].u4Length)
                            u4ImgSecSize = CMD_PKT_SIZE_FOR_IMAGE;
                        else
                            u4ImgSecSize = prFwHead->arSection[i].u4Length - j;

                        if(wlanImageSectionDownload(prAdapter,
                                    prFwHead->arSection[i].u4DestAddr + j,
                                    u4ImgSecSize,
                                    (PUINT_8)pvFwImageMapFile + prFwHead->arSection[i].u4Offset + j) != WLAN_STATUS_SUCCESS) {
                            DBGLOG(INIT, ERROR, ("Firmware scatter download failed!\n"));
                            u4Status = WLAN_STATUS_FAILURE;
                            break;
                        }
                    }
            #endif

                    /* escape from loop if any pending error occurs */
                    if(u4Status == WLAN_STATUS_FAILURE) {
                        break;
                    }
                }
            }
            else
        #endif
        #if CFG_ENABLE_FW_DOWNLOAD_AGGREGATION
            if(wlanImageSectionDownloadAggregated(prAdapter,
                        u4FwLoadAddr,
                        u4FwImageFileLength,
                        (PUINT_8)pvFwImageMapFile) != WLAN_STATUS_SUCCESS) {
                DBGLOG(INIT, ERROR, ("Firmware scatter download failed!\n"));
                u4Status = WLAN_STATUS_FAILURE;
            }
        #else
            for (i = 0; i < u4FwImageFileLength ; i += CMD_PKT_SIZE_FOR_IMAGE) {
                if(i + CMD_PKT_SIZE_FOR_IMAGE < u4FwImageFileLength)
                    u4ImgSecSize = CMD_PKT_SIZE_FOR_IMAGE;
                else
                    u4ImgSecSize = u4FwImageFileLength - i;

                if(wlanImageSectionDownload(prAdapter,
                        u4FwLoadAddr + i,
                        u4ImgSecSize,
                        (PUINT_8)pvFwImageMapFile + i) != WLAN_STATUS_SUCCESS) {
                    DBGLOG(INIT, ERROR, ("Firmware scatter download failed!\n"));
                    u4Status = WLAN_STATUS_FAILURE;
                    break;
                }
            }
        #endif

            if(u4Status != WLAN_STATUS_SUCCESS) {
                break;
            }

        #if !CFG_ENABLE_FW_DOWNLOAD_ACK
            // Send INIT_CMD_ID_QUERY_PENDING_ERROR command and wait for response
            if(wlanImageQueryStatus(prAdapter) != WLAN_STATUS_SUCCESS) {
                DBGLOG(INIT, ERROR, ("Firmware download failed!\n"));
                u4Status = WLAN_STATUS_FAILURE;
                break;
            }
        #endif
        }
        else {
            DBGLOG(INIT, ERROR, ("No Firmware found!\n"));
            u4Status = WLAN_STATUS_FAILURE;
            break;
        }

        /* 4. send Wi-Fi Start command */
        #if CFG_OVERRIDE_FW_START_ADDRESS
        wlanConfigWifiFunc(prAdapter,
                TRUE,
                prRegInfo->u4StartAddress);
        #else
        wlanConfigWifiFunc(prAdapter,
                FALSE,
                0);
        #endif
    #elif defined(MT5931)
        if (pvFwImageMapFile) {
            DBGLOG(INIT, TRACE, ("Download Address: 0x%08X\n", prRegInfo->u4LoadAddress));
            DBGLOG(INIT, TRACE, ("Firmware Length:  0x%08X\n", u4FwImageFileLength));

            do {
#if CFG_SUPPORT_WHOLE_CHIP_RESET
#define RESET_RDY_INTERVAL (120)

                /* 1.0 whole-chip reset except HIFSYS */
                HAL_MCR_WR(prAdapter, MCR_WMCSR, WMCSR_CHIP_RST);
                HAL_MCR_WR(prAdapter, MCR_WMCSR, 0);

                /* 1.0.1 delay for EEIF ready */
                kalMsleep(RESET_RDY_INTERVAL);
#endif

                /* 1.1 wait for INIT_RDY */
                i = 0;
                while(1) {
                    HAL_MCR_RD(prAdapter, MCR_WMCSR, &u4Value);

                    if (u4Value & WMCSR_INI_RDY) {
                        DBGLOG(INIT, TRACE, ("INIT-RDY detected\n"));
                        break;
                    }
                    else if(kalIsCardRemoved(prAdapter->prGlueInfo) == TRUE
                            || fgIsBusAccessFailed == TRUE) {
                        u4Status = WLAN_STATUS_FAILURE;
                        break;
                    }
                    else if(i >= CFG_RESPONSE_POLLING_TIMEOUT) {
                        DBGLOG(INIT, ERROR, ("Waiting for Init Ready bit: Timeout\n"));
                        u4Status = WLAN_STATUS_FAILURE;
                        break;
                    }
                    else {
                        i++;
                        kalMsleep(10);
                    }
                }

                if(u4Status != WLAN_STATUS_SUCCESS) {
                    break;
                }

                /* 1.2 set KSEL/FLEN */
                HAL_MCR_WR(prAdapter, MCR_FWCFG, u4FwImageFileLength >> 6);

                /* 1.3 enable FWDL_EN */
                HAL_MCR_WR(prAdapter, MCR_WMCSR, WMCSR_FWDLEN);

                /* 1.4 wait for PLL_RDY */
                i = 0;
                while(1) {
                    HAL_MCR_RD(prAdapter, MCR_WMCSR, &u4Value);

                    if (u4Value & WMCSR_PLLRDY) {
                        DBGLOG(INIT, TRACE, ("PLL-RDY detected\n"));
                        break;
                    }
                    else if(kalIsCardRemoved(prAdapter->prGlueInfo) == TRUE
                            || fgIsBusAccessFailed == TRUE) {
                        u4Status = WLAN_STATUS_FAILURE;
                        break;
                    }
                    else if(i >= CFG_RESPONSE_POLLING_TIMEOUT) {
                        DBGLOG(INIT, ERROR, ("Waiting for PLL Ready bit: Timeout\n"));
                        u4Status = WLAN_STATUS_FAILURE;
                        break;
                    }
                    else {
                        i++;
                        kalMsleep(10);
                    }
                }

                if(u4Status != WLAN_STATUS_SUCCESS) {
                    break;
                }

                /* 2.1 turn on HIFSYS firmware download mode */
                HAL_MCR_WR(prAdapter, MCR_FWDLSR, FWDLSR_FWDL_MODE);

                /* 2.2 set starting address */
                u4FwLoadAddr = prRegInfo->u4LoadAddress;
                HAL_MCR_WR(prAdapter, MCR_FWDLDSAR, u4FwLoadAddr);

                /* 3. upload firmware */
                for (i = 0; i < u4FwImageFileLength ; i += CMD_PKT_SIZE_FOR_IMAGE) {
                    if(i + CMD_PKT_SIZE_FOR_IMAGE < u4FwImageFileLength)
                        u4ImgSecSize = CMD_PKT_SIZE_FOR_IMAGE;
                    else
                        u4ImgSecSize = u4FwImageFileLength - i;

                    if(wlanImageSectionDownload(prAdapter,
                                u4FwLoadAddr + i,
                                u4ImgSecSize,
                                (PUINT_8)pvFwImageMapFile + i) != WLAN_STATUS_SUCCESS) {
                        DBGLOG(INIT, ERROR, ("Firmware scatter download failed!\n"));
                        u4Status = WLAN_STATUS_FAILURE;
                        break;
                    }
                }

                if(u4Status != WLAN_STATUS_SUCCESS) {
                    break;
                }

                /* 4.1 poll FWDL_OK & FWDL_FAIL bits */
                i = 0;
                while(1) {
                    HAL_MCR_RD(prAdapter, MCR_WMCSR, &u4Value);

                    if (u4Value & WMCSR_DL_OK) {
                         DBGLOG(INIT, TRACE, ("DL_OK detected\n"));
                        break;
                    }
                    else if(kalIsCardRemoved(prAdapter->prGlueInfo) == TRUE
                            || fgIsBusAccessFailed == TRUE
                            || (u4Value & WMCSR_DL_FAIL)) {
                        DBGLOG(INIT, ERROR, ("DL_FAIL detected: 0x%08X\n", u4Value));
                        u4Status = WLAN_STATUS_FAILURE;
                        break;
                    }
                    else if(i >= CFG_RESPONSE_POLLING_TIMEOUT) {
                        DBGLOG(INIT, ERROR, ("Waiting for DL_OK/DL_FAIL bit: Timeout\n"));
                        u4Status = WLAN_STATUS_FAILURE;
                        break;
                    }
                    else {
                        i++;
                        kalMsleep(10);
                    }
                }

                if(u4Status != WLAN_STATUS_SUCCESS) {
                    break;
                }

                /* 4.2 turn off HIFSYS download mode */
                HAL_MCR_WR(prAdapter, MCR_FWDLSR, 0);

            } while (FALSE);

            if(u4Status != WLAN_STATUS_SUCCESS) {
                break;
            }

            /* 5. disable interrupt */
            nicDisableInterrupt(prAdapter);
        }
        else {
            DBGLOG(INIT, ERROR, ("No Firmware found!\n"));
            u4Status = WLAN_STATUS_FAILURE;
            break;
        }
    #endif
#endif

        DBGLOG(INIT, TRACE, ("wlanAdapterStart(): Waiting for Ready bit..\n"));
        //4 <5> check Wi-Fi FW asserts ready bit
        i = 0;
        while(1) {
            HAL_MCR_RD(prAdapter, MCR_WCIR, &u4Value);

            if (u4Value & WCIR_WLAN_READY) {
                DBGLOG(INIT, TRACE, ("Ready bit asserted\n"));
                break;
            }
            else if(kalIsCardRemoved(prAdapter->prGlueInfo) == TRUE
                    || fgIsBusAccessFailed == TRUE) {
                u4Status = WLAN_STATUS_FAILURE;
                break;
            }
            else if(i >= CFG_RESPONSE_POLLING_TIMEOUT) {
                UINT_32     u4MailBox0;

                nicGetMailbox(prAdapter, 0, &u4MailBox0);
                DBGLOG(INIT, ERROR, ("Waiting for Ready bit: Timeout, ID=%d\n",
                        (u4MailBox0 & 0x0000FFFF)));
                u4Status = WLAN_STATUS_FAILURE;
                break;
            }
            else {
                i++;
                kalMsleep(10);
            }
        }

#if defined(MT5931)
        // Acquire LP-OWN
        DBGLOG(INIT, TRACE, ("wlanAdapterStart(): Acquiring LP-OWN\n"));
        ACQUIRE_POWER_CONTROL_FROM_PM(prAdapter);
    #if !CFG_ENABLE_FULL_PM
        nicpmSetDriverOwn(prAdapter);
    #endif

        if(prAdapter->fgIsFwOwn == TRUE) {
            DBGLOG(INIT, ERROR, ("nicpmSetDriverOwn() failed!\n"));
            u4Status = WLAN_STATUS_FAILURE;
            break;
        }

        //4 <1> Initialize the Adapter
        if ( (u4Status = nicInitializeAdapter(prAdapter)) != WLAN_STATUS_SUCCESS ) {
            DBGLOG(INIT, ERROR, ("nicInitializeAdapter failed!\n"));
            u4Status = WLAN_STATUS_FAILURE;
            break;
        }

        /* post initialization for MT5931 due to some CR is only accessible after driver own */
        nicRxPostInitialize(prAdapter);
#endif

        if(u4Status == WLAN_STATUS_SUCCESS) {
            // 1. reset interrupt status
            HAL_READ_INTR_STATUS(prAdapter, 4, (PUINT_8)&u4WHISR);
            if(HAL_IS_TX_DONE_INTR(u4WHISR)) {
                HAL_READ_TX_RELEASED_COUNT(prAdapter, aucTxCount);
            }

            /* 2. reset TX Resource for normal operation */
            nicTxResetResource(prAdapter);

#if CFG_SUPPORT_OSC_SETTING && defined(MT5931)
            wlanSetMcuOscStableTime(prAdapter, 0);
#endif

            /* 3. query for permanent address by polling */
            wlanQueryPermanentAddress(prAdapter);

#if (CFG_SUPPORT_NIC_CAPABILITY == 1)
            /* 4. query for NIC capability */
            wlanQueryNicCapability(prAdapter);
#endif

            /* 5. Override network address */
            wlanUpdateNetworkAddress(prAdapter);

            /* 6. indicate disconnection as default status */
            kalIndicateStatusAndComplete(prAdapter->prGlueInfo,
                    WLAN_STATUS_MEDIA_DISCONNECT,
                    NULL,
                    0);
        }

        RECLAIM_POWER_CONTROL_TO_PM(prAdapter, FALSE);

        if(u4Status != WLAN_STATUS_SUCCESS) {
            break;
        }

        /* OID timeout timer initialize */
        cnmTimerInitTimer(prAdapter,
                &prAdapter->rOidTimeoutTimer,
                (PFN_MGMT_TIMEOUT_FUNC)wlanReleasePendingOid,
                (UINT_32)NULL);

        /* Power state initialization */
        prAdapter->fgWiFiInSleepyState = FALSE;
        prAdapter->rAcpiState = ACPI_STATE_D0;

        /* Online scan option */
        if(prRegInfo->fgDisOnlineScan == 0) {
            prAdapter->fgEnOnlineScan = TRUE;
        }
        else {
            prAdapter->fgEnOnlineScan = FALSE;
        }

        /* Beacon lost detection option */
        if(prRegInfo->fgDisBcnLostDetection != 0) {
            prAdapter->fgDisBcnLostDetection = TRUE;
        }

        /* Load compile time constant */
        prAdapter->rWlanInfo.u2BeaconPeriod = CFG_INIT_ADHOC_BEACON_INTERVAL;
        prAdapter->rWlanInfo.u2AtimWindow = CFG_INIT_ADHOC_ATIM_WINDOW;

#if 1// set PM parameters
        prAdapter->fgEnArpFilter = prRegInfo->fgEnArpFilter;
        prAdapter->u4PsCurrentMeasureEn = prRegInfo->u4PsCurrentMeasureEn;

        prAdapter->u4UapsdAcBmp = prRegInfo->u4UapsdAcBmp;

        prAdapter->u4MaxSpLen = prRegInfo->u4MaxSpLen;

        DBGLOG(INIT, TRACE, ("[1] fgEnArpFilter:0x%x, u4UapsdAcBmp:0x%x, u4MaxSpLen:0x%x",
                prAdapter->fgEnArpFilter,
                prAdapter->u4UapsdAcBmp,
                prAdapter->u4MaxSpLen));

        prAdapter->fgEnCtiaPowerMode = FALSE;

#endif

        /* MGMT Initialization */
        nicInitMGMT(prAdapter, prRegInfo);

        /* Enable WZC Disassociation */
        prAdapter->rWifiVar.fgSupportWZCDisassociation = TRUE;

        /* Apply Rate Setting */
        if((ENUM_REGISTRY_FIXED_RATE_T)(prRegInfo->u4FixedRate) < FIXED_RATE_NUM) {
            prAdapter->rWifiVar.eRateSetting = (ENUM_REGISTRY_FIXED_RATE_T)(prRegInfo->u4FixedRate);
        }
        else {
            prAdapter->rWifiVar.eRateSetting = FIXED_RATE_NONE;
        }

        if(prAdapter->rWifiVar.eRateSetting == FIXED_RATE_NONE) {
            /* Enable Auto (Long/Short) Preamble */
            prAdapter->rWifiVar.ePreambleType = PREAMBLE_TYPE_AUTO;
        }
        else if((prAdapter->rWifiVar.eRateSetting >= FIXED_RATE_MCS0_20M_400NS &&
                    prAdapter->rWifiVar.eRateSetting <= FIXED_RATE_MCS7_20M_400NS)
                || (prAdapter->rWifiVar.eRateSetting >= FIXED_RATE_MCS0_40M_400NS &&
                        prAdapter->rWifiVar.eRateSetting <= FIXED_RATE_MCS32_400NS)) {
            /* Force Short Preamble */
            prAdapter->rWifiVar.ePreambleType = PREAMBLE_TYPE_SHORT;
        }
        else {
            /* Force Long Preamble */
            prAdapter->rWifiVar.ePreambleType = PREAMBLE_TYPE_LONG;
        }

        /* Disable Hidden SSID Join */
        prAdapter->rWifiVar.fgEnableJoinToHiddenSSID = FALSE;

        /* Enable Short Slot Time */
        prAdapter->rWifiVar.fgIsShortSlotTimeOptionEnable = TRUE;

        /* configure available PHY type set */
        nicSetAvailablePhyTypeSet(prAdapter);

#if 1// set PM parameters
        {
#if  CFG_SUPPORT_PWR_MGT
        prAdapter->u4PowerMode = prRegInfo->u4PowerMode;
        prAdapter->rWlanInfo.arPowerSaveMode[NETWORK_TYPE_P2P_INDEX].ucNetTypeIndex = NETWORK_TYPE_P2P_INDEX;
        prAdapter->rWlanInfo.arPowerSaveMode[NETWORK_TYPE_P2P_INDEX].ucPsProfile = ENUM_PSP_FAST_SWITCH;
#else
        prAdapter->u4PowerMode = ENUM_PSP_CONTINUOUS_ACTIVE;
#endif

        nicConfigPowerSaveProfile(
            prAdapter,
            NETWORK_TYPE_AIS_INDEX, //FIXIT
            prAdapter->u4PowerMode,
            FALSE);
        }

#endif

#if CFG_SUPPORT_NVRAM
        /* load manufacture data */
        wlanLoadManufactureData(prAdapter, prRegInfo);
#endif

#if (defined(MT5931) && (!CFG_SUPPORT_BCM_BWCS))
        //Enable DPD calibration.
        rBwcsPta.u.aucBTPParams[0] = 0x00;
        rBwcsPta.u.aucBTPParams[1] = 0x01;
        rBwcsPta.u.aucBTPParams[2] = 0x00;
        rBwcsPta.u.aucBTPParams[3] = 0x80;

        wlanoidSetBT(prAdapter,
            (PVOID)&rBwcsPta,
            sizeof(PARAM_PTA_IPC_T),
            &u4SetInfoLen);
#endif

#if 0
    /* Update Auto rate parameters in FW */
    nicRlmArUpdateParms(prAdapter,
        prRegInfo->u4ArSysParam0,
        prRegInfo->u4ArSysParam1,
        prRegInfo->u4ArSysParam2,
        prRegInfo->u4ArSysParam3);
#endif


#if (MT6620_E1_ASIC_HIFSYS_WORKAROUND == 1)
        /* clock gating workaround */
        prAdapter->fgIsClockGatingEnabled = FALSE;
#endif

    } while(FALSE);

    if(u4Status == WLAN_STATUS_SUCCESS) {
        // restore to hardware default
        HAL_SET_INTR_STATUS_READ_CLEAR(prAdapter);
        HAL_SET_MAILBOX_READ_CLEAR(prAdapter, FALSE);

        /* Enable interrupt */
        nicEnableInterrupt(prAdapter);

    }
    else {
        // release allocated memory
        nicReleaseAdapterMemory(prAdapter);
    }

    return u4Status;
} /* wlanAdapterStart */


/*----------------------------------------------------------------------------*/
/*!
* \brief Uninitialize the adapter
*
* \param prAdapter      Pointer of Adapter Data Structure
*
* \retval WLAN_STATUS_SUCCESS: Success
* \retval WLAN_STATUS_FAILURE: Failed
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanAdapterStop (
    IN P_ADAPTER_T prAdapter
    )
{
    UINT_32 i, u4Value = 0;
    WLAN_STATUS u4Status = WLAN_STATUS_SUCCESS;

    ASSERT(prAdapter);

#if (MT6620_E1_ASIC_HIFSYS_WORKAROUND == 1)
    if(prAdapter->fgIsClockGatingEnabled == TRUE) {
        nicDisableClockGating(prAdapter);
    }
#endif

    /* MGMT - unitialization */
    nicUninitMGMT(prAdapter);

    if(prAdapter->rAcpiState == ACPI_STATE_D0 &&
#if (CFG_CHIP_RESET_SUPPORT == 1)
            kalIsResetting() == FALSE &&
#endif
            kalIsCardRemoved(prAdapter->prGlueInfo) == FALSE) {

        /* 0. Disable interrupt, this can be done without Driver own */
        nicDisableInterrupt(prAdapter);

        ACQUIRE_POWER_CONTROL_FROM_PM(prAdapter);

        /* 1. Set CMD to FW to tell WIFI to stop (enter power off state) */
        if(prAdapter->fgIsFwOwn == FALSE &&
                wlanSendNicPowerCtrlCmd(prAdapter, 1) == WLAN_STATUS_SUCCESS) {
            /* 2. Clear pending interrupt */
            i = 0;
            while(i < CFG_IST_LOOP_COUNT && nicProcessIST(prAdapter) != WLAN_STATUS_NOT_INDICATING) {
                i++;
            };

            /* 3. Wait til RDY bit has been cleaerd */
            i = 0;
            while(1) {
                HAL_MCR_RD(prAdapter, MCR_WCIR, &u4Value);

                if ((u4Value & WCIR_WLAN_READY) == 0)
                    break;
                else if(kalIsCardRemoved(prAdapter->prGlueInfo) == TRUE
                        || fgIsBusAccessFailed == TRUE
                        || i >= CFG_RESPONSE_POLLING_TIMEOUT) {
                    break;
                }
                else {
                    i++;
                    kalMsleep(10);
                }
            }
        }

        /* 4. Set Onwership to F/W */
        nicpmSetFWOwn(prAdapter, FALSE);

        RECLAIM_POWER_CONTROL_TO_PM(prAdapter, FALSE);
    }

    nicRxUninitialize(prAdapter);

    nicTxRelease(prAdapter);

    /* System Service Uninitialization */
    nicUninitSystemService(prAdapter);

    nicReleaseAdapterMemory(prAdapter);

#if defined(_HIF_SPI)
    /* Note: restore the SPI Mode Select from 32 bit to default */
    nicRestoreSpiDefMode(prAdapter);
#endif

    return u4Status;
} /* wlanAdapterStop */


/*----------------------------------------------------------------------------*/
/*!
* \brief This function is called by ISR (interrupt).
*
* \param prAdapter      Pointer of Adapter Data Structure
*
* \retval TRUE: NIC's interrupt
* \retval FALSE: Not NIC's interrupt
*/
/*----------------------------------------------------------------------------*/
BOOL
wlanISR (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgGlobalIntrCtrl
    )
{
    ASSERT(prAdapter);

    if (fgGlobalIntrCtrl) {
        nicDisableInterrupt(prAdapter);

        //wlanIST(prAdapter);
    }

    return TRUE;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This function is called by IST (task_let).
*
* \param prAdapter      Pointer of Adapter Data Structure
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
wlanIST (
    IN P_ADAPTER_T prAdapter
    )
{
    ASSERT(prAdapter);

    ACQUIRE_POWER_CONTROL_FROM_PM(prAdapter);

    nicProcessIST(prAdapter);

    nicEnableInterrupt(prAdapter);

    RECLAIM_POWER_CONTROL_TO_PM(prAdapter, FALSE);

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will check command queue to find out if any could be dequeued
*        and/or send to HIF to MT6620
*
* \param prAdapter      Pointer of Adapter Data Structure
* \param prCmdQue       Pointer of Command Queue (in Glue Layer)
*
* \retval WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanProcessCommandQueue (
    IN P_ADAPTER_T  prAdapter,
    IN P_QUE_T      prCmdQue
    )
{
    WLAN_STATUS rStatus;
    QUE_T rTempCmdQue, rMergeCmdQue, rStandInCmdQue;
    P_QUE_T prTempCmdQue, prMergeCmdQue, prStandInCmdQue;
    P_QUE_ENTRY_T prQueueEntry;
    P_CMD_INFO_T prCmdInfo;
    P_MSDU_INFO_T prMsduInfo;
    ENUM_FRAME_ACTION_T eFrameAction = FRAME_ACTION_DROP_PKT;

    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);
    ASSERT(prCmdQue);

    prTempCmdQue = &rTempCmdQue;
    prMergeCmdQue = &rMergeCmdQue;
    prStandInCmdQue = &rStandInCmdQue;

    QUEUE_INITIALIZE(prTempCmdQue);
    QUEUE_INITIALIZE(prMergeCmdQue);
    QUEUE_INITIALIZE(prStandInCmdQue);

    //4 <1> Move whole list of CMD_INFO to temp queue
    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_CMD_QUE);
    QUEUE_MOVE_ALL(prTempCmdQue, prCmdQue);
    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_CMD_QUE);

    //4 <2> Dequeue from head and check it is able to be sent
    QUEUE_REMOVE_HEAD(prTempCmdQue, prQueueEntry, P_QUE_ENTRY_T);
    while(prQueueEntry) {
        prCmdInfo = (P_CMD_INFO_T)prQueueEntry;

        switch(prCmdInfo->eCmdType) {
        case COMMAND_TYPE_GENERAL_IOCTL:
        case COMMAND_TYPE_NETWORK_IOCTL:
            /* command packet will be always sent */
            eFrameAction = FRAME_ACTION_TX_PKT;
            break;

        case COMMAND_TYPE_SECURITY_FRAME:
            /* inquire with QM */
            eFrameAction = qmGetFrameAction(prAdapter,
                    prCmdInfo->eNetworkType,
                    prCmdInfo->ucStaRecIndex,
                    NULL,
                    FRAME_TYPE_802_1X);
            break;

        case COMMAND_TYPE_MANAGEMENT_FRAME:
            /* inquire with QM */
            prMsduInfo = (P_MSDU_INFO_T)(prCmdInfo->prPacket);

            eFrameAction = qmGetFrameAction(prAdapter,
                    prMsduInfo->ucNetworkType,
                    prMsduInfo->ucStaRecIndex,
                    prMsduInfo,
                    FRAME_TYPE_MMPDU);
            break;

        default:
            ASSERT(0);
            break;
        }

        //4 <3> handling upon dequeue result
        if(eFrameAction == FRAME_ACTION_DROP_PKT) {
            wlanReleaseCommand(prAdapter, prCmdInfo);
        }
        else if(eFrameAction == FRAME_ACTION_QUEUE_PKT) {
            QUEUE_INSERT_TAIL(prMergeCmdQue, prQueueEntry);
        }
        else if(eFrameAction == FRAME_ACTION_TX_PKT) {
            //4 <4> Send the command
            rStatus = wlanSendCommand(prAdapter, prCmdInfo);

            if(rStatus == WLAN_STATUS_RESOURCES) {
                // no more TC4 resource for further transmission
                QUEUE_INSERT_TAIL(prMergeCmdQue, prQueueEntry);
                break;
            }
            else if(rStatus == WLAN_STATUS_PENDING) {
                // command packet which needs further handling upon response
                KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_CMD_PENDING);
                QUEUE_INSERT_TAIL(&(prAdapter->rPendingCmdQueue), prQueueEntry);
                KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_CMD_PENDING);
            }
            else {
                P_CMD_INFO_T prCmdInfo = (P_CMD_INFO_T)prQueueEntry;

                if (rStatus == WLAN_STATUS_SUCCESS) {
                    if (prCmdInfo->pfCmdDoneHandler) {
                        prCmdInfo->pfCmdDoneHandler(prAdapter, prCmdInfo, prCmdInfo->pucInfoBuffer);
                    }
                }
                else {
                    if (prCmdInfo->fgIsOid) {
                        kalOidComplete(prAdapter->prGlueInfo, prCmdInfo->fgSetQuery, prCmdInfo->u4SetInfoLen, rStatus);
                    }
                }

                cmdBufFreeCmdInfo(prAdapter, prCmdInfo);
            }
        }
        else {
            ASSERT(0);
        }

        QUEUE_REMOVE_HEAD(prTempCmdQue, prQueueEntry, P_QUE_ENTRY_T);
    }

    //4 <3> Merge back to original queue
    //4 <3.1> Merge prMergeCmdQue & prTempCmdQue
    QUEUE_CONCATENATE_QUEUES(prMergeCmdQue, prTempCmdQue);

    //4 <3.2> Move prCmdQue to prStandInQue, due to prCmdQue might differ due to incoming 802.1X frames
    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_CMD_QUE);
    QUEUE_MOVE_ALL(prStandInCmdQue, prCmdQue);

    //4 <3.3> concatenate prStandInQue to prMergeCmdQue
    QUEUE_CONCATENATE_QUEUES(prMergeCmdQue, prStandInCmdQue);

    //4 <3.4> then move prMergeCmdQue to prCmdQue
    QUEUE_MOVE_ALL(prCmdQue, prMergeCmdQue);
    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_CMD_QUE);

    return WLAN_STATUS_SUCCESS;
} /* end of wlanProcessCommandQueue() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will take CMD_INFO_T which carry some informations of
*        incoming OID and notify the NIC_TX to send CMD.
*
* \param prAdapter      Pointer of Adapter Data Structure
* \param prCmdInfo      Pointer of P_CMD_INFO_T
*
* \retval WLAN_STATUS_SUCCESS   : CMD was written to HIF and be freed(CMD Done) immediately.
* \retval WLAN_STATUS_RESOURCE  : No resource for current command, need to wait for previous
*                                 frame finishing their transmission.
* \retval WLAN_STATUS_FAILURE   : Get failure while access HIF or been rejected.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanSendCommand (
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo
    )
{
    P_TX_CTRL_T prTxCtrl;
    UINT_8 ucTC; /* "Traffic Class" SW(Driver) resource classification */
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;

    ASSERT(prAdapter);
    ASSERT(prCmdInfo);
    prTxCtrl = &prAdapter->rTxCtrl;

    //DbgPrint("wlanSendCommand()\n");
    //
    //
#if DBG && 0
    LOG_FUNC("wlanSendCommand()\n");
    LOG_FUNC("CmdType %u NetworkType %u StaRecIndex %u Oid %u CID 0x%x SetQuery %u NeedResp %u CmdSeqNum %u\n",
            prCmdInfo->eCmdType,
            prCmdInfo->eNetworkType,
            prCmdInfo->ucStaRecIndex,
            prCmdInfo->fgIsOid,
            prCmdInfo->ucCID,
            prCmdInfo->fgSetQuery,
            prCmdInfo->fgNeedResp,
            prCmdInfo->ucCmdSeqNum);
#endif

#if (MT6620_E1_ASIC_HIFSYS_WORKAROUND == 1)
    if(prAdapter->fgIsClockGatingEnabled == TRUE) {
        nicDisableClockGating(prAdapter);
    }
#endif

    do {
        // <0> card removal check
        if(kalIsCardRemoved(prAdapter->prGlueInfo) == TRUE
                || fgIsBusAccessFailed == TRUE) {
            rStatus = WLAN_STATUS_FAILURE;
            break;
        }

        // <1> Normal case of sending CMD Packet
        if (!prCmdInfo->fgDriverDomainMCR) {
            // <1.1> Assign Traffic Class(TC) = TC4.
            ucTC = TC4_INDEX;

            // <1.2> Check if pending packet or resource was exhausted
            if ((rStatus = nicTxAcquireResource(prAdapter, ucTC)) == WLAN_STATUS_RESOURCES) {
                DbgPrint("NO Resource:%d\n", ucTC);
                break;
            }

            // <1.3> Forward CMD_INFO_T to NIC Layer
            rStatus = nicTxCmd(prAdapter, prCmdInfo, ucTC);

            // <1.4> Set Pending in response to Query Command/Need Response
            if (rStatus == WLAN_STATUS_SUCCESS) {
                if ((!prCmdInfo->fgSetQuery) || (prCmdInfo->fgNeedResp)) {
                    rStatus = WLAN_STATUS_PENDING;
                }
            }
        }
        // <2> Special case for access Driver Domain MCR
        else {
            P_CMD_ACCESS_REG prCmdAccessReg;
            prCmdAccessReg = (P_CMD_ACCESS_REG)(prCmdInfo->pucInfoBuffer + CMD_HDR_SIZE);

            if (prCmdInfo->fgSetQuery) {
                HAL_MCR_WR(prAdapter,
                        (prCmdAccessReg->u4Address & BITS(2,31)), //address is in DWORD unit
                        prCmdAccessReg->u4Data);
            }
            else {
                P_CMD_ACCESS_REG prEventAccessReg;
                UINT_32 u4Address;

                u4Address = prCmdAccessReg->u4Address;
                prEventAccessReg = (P_CMD_ACCESS_REG)prCmdInfo->pucInfoBuffer;
                prEventAccessReg->u4Address = u4Address;

                HAL_MCR_RD(prAdapter,
                       prEventAccessReg->u4Address & BITS(2,31), //address is in DWORD unit
                       &prEventAccessReg->u4Data);
            }
        }

    }
    while (FALSE);

#if (MT6620_E1_ASIC_HIFSYS_WORKAROUND == 1)
    if(prAdapter->fgIsClockGatingEnabled == FALSE) {
        nicEnableClockGating(prAdapter);
    }
#endif

    return rStatus;
} /* end of wlanSendCommand() */


/*----------------------------------------------------------------------------*/
/*!
 * \brief This function will release thd CMD_INFO upon its attribution
 *
 * \param prAdapter  Pointer of Adapter Data Structure
 * \param prCmdInfo  Pointer of CMD_INFO_T
 *
 * \return (none)
 */
/*----------------------------------------------------------------------------*/
VOID
wlanReleaseCommand (
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo
    )
{
    P_TX_CTRL_T prTxCtrl;
    P_MSDU_INFO_T prMsduInfo;

    ASSERT(prAdapter);
    ASSERT(prCmdInfo);

    prTxCtrl = &prAdapter->rTxCtrl;

    switch(prCmdInfo->eCmdType) {
    case COMMAND_TYPE_GENERAL_IOCTL:
    case COMMAND_TYPE_NETWORK_IOCTL:
        if (prCmdInfo->fgIsOid) {
            kalOidComplete(prAdapter->prGlueInfo,
                    prCmdInfo->fgSetQuery,
                    prCmdInfo->u4SetInfoLen,
                    WLAN_STATUS_FAILURE);
        }
        break;

    case COMMAND_TYPE_SECURITY_FRAME:
        kalSecurityFrameSendComplete(prAdapter->prGlueInfo,
                prCmdInfo->prPacket,
                WLAN_STATUS_FAILURE);
        break;

    case COMMAND_TYPE_MANAGEMENT_FRAME:
        prMsduInfo = (P_MSDU_INFO_T)prCmdInfo->prPacket;

        /* invoke callbacks */
        if(prMsduInfo->pfTxDoneHandler != NULL) {
            prMsduInfo->pfTxDoneHandler(prAdapter, prMsduInfo, TX_RESULT_DROPPED_IN_DRIVER);
        }

        GLUE_DEC_REF_CNT(prTxCtrl->i4TxMgmtPendingNum);
        cnmMgtPktFree(prAdapter, prMsduInfo);
        break;

    default:
        ASSERT(0);
        break;
    }

    cmdBufFreeCmdInfo(prAdapter, prCmdInfo);

} /* end of wlanReleaseCommand() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will search the CMD Queue to look for the pending OID and
*        compelete it immediately when system request a reset.
*
* \param prAdapter  ointer of Adapter Data Structure
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
wlanReleasePendingOid (
    IN P_ADAPTER_T  prAdapter,
    IN UINT_32      u4Data
    )
{
    P_QUE_T prCmdQue;
    QUE_T rTempCmdQue;
    P_QUE_T prTempCmdQue = &rTempCmdQue;
    P_QUE_ENTRY_T prQueueEntry = (P_QUE_ENTRY_T)NULL;
    P_CMD_INFO_T prCmdInfo = (P_CMD_INFO_T)NULL;

    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);

    do {
        // 1: Clear Pending OID in prAdapter->rPendingCmdQueue
        KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_CMD_PENDING);

        prCmdQue = &prAdapter->rPendingCmdQueue;
        QUEUE_MOVE_ALL(prTempCmdQue, prCmdQue);

        QUEUE_REMOVE_HEAD(prTempCmdQue, prQueueEntry, P_QUE_ENTRY_T);
        while (prQueueEntry) {
            prCmdInfo = (P_CMD_INFO_T)prQueueEntry;

            if (prCmdInfo->fgIsOid) {
                if (prCmdInfo->pfCmdTimeoutHandler) {
                    prCmdInfo->pfCmdTimeoutHandler(prAdapter, prCmdInfo);
                }
                else
                    kalOidComplete(prAdapter->prGlueInfo,
                            prCmdInfo->fgSetQuery,
                            0,
                            WLAN_STATUS_FAILURE);

                cmdBufFreeCmdInfo(prAdapter, prCmdInfo);
            }
            else {
                QUEUE_INSERT_TAIL(prCmdQue, prQueueEntry);
            }

            QUEUE_REMOVE_HEAD(prTempCmdQue, prQueueEntry, P_QUE_ENTRY_T);
        }

        KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_CMD_PENDING);

        // 2: Clear pending OID in glue layer command queue
        kalOidCmdClearance(prAdapter->prGlueInfo);

        // 3: Clear pending OID queued in pvOidEntry with REQ_FLAG_OID set
        kalOidClearance(prAdapter->prGlueInfo);

    } while(FALSE);

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will search the CMD Queue to look for the pending CMD/OID for specific
*        NETWORK TYPE and compelete it immediately when system request a reset.
*
* \param prAdapter  ointer of Adapter Data Structure
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
wlanReleasePendingCMDbyNetwork (
    IN P_ADAPTER_T  prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetworkType
    )
{
    P_QUE_T prCmdQue;
    QUE_T rTempCmdQue;
    P_QUE_T prTempCmdQue = &rTempCmdQue;
    P_QUE_ENTRY_T prQueueEntry = (P_QUE_ENTRY_T)NULL;
    P_CMD_INFO_T prCmdInfo = (P_CMD_INFO_T)NULL;

    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);

    do {
        // 1: Clear Pending OID in prAdapter->rPendingCmdQueue
        KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_CMD_PENDING);

        prCmdQue = &prAdapter->rPendingCmdQueue;
        QUEUE_MOVE_ALL(prTempCmdQue, prCmdQue);

        QUEUE_REMOVE_HEAD(prTempCmdQue, prQueueEntry, P_QUE_ENTRY_T);
        while (prQueueEntry) {
            prCmdInfo = (P_CMD_INFO_T)prQueueEntry;

            DBGLOG(P2P, TRACE, ("Pending CMD for Network Type:%d \n", prCmdInfo->eNetworkType));

            if (prCmdInfo->eNetworkType == eNetworkType) {
                if (prCmdInfo->pfCmdTimeoutHandler) {
                    prCmdInfo->pfCmdTimeoutHandler(prAdapter, prCmdInfo);
                }
                else
                    kalOidComplete(prAdapter->prGlueInfo,
                            prCmdInfo->fgSetQuery,
                            0,
                            WLAN_STATUS_FAILURE);

                cmdBufFreeCmdInfo(prAdapter, prCmdInfo);
            }
            else {
                QUEUE_INSERT_TAIL(prCmdQue, prQueueEntry);
            }

            QUEUE_REMOVE_HEAD(prTempCmdQue, prQueueEntry, P_QUE_ENTRY_T);
        }

        KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_CMD_PENDING);


    } while(FALSE);

    return;
} /* wlanReleasePendingCMDbyNetwork */



/*----------------------------------------------------------------------------*/
/*!
* \brief Return the packet buffer and reallocate one to the RFB
*
* \param prAdapter      Pointer of Adapter Data Structure
* \param pvPacket       Pointer of returned packet
*
* \retval WLAN_STATUS_SUCCESS: Success
* \retval WLAN_STATUS_FAILURE: Failed
*/
/*----------------------------------------------------------------------------*/
VOID
wlanReturnPacket (
    IN P_ADAPTER_T prAdapter,
    IN PVOID pvPacket
    )
{
    P_RX_CTRL_T prRxCtrl;
    P_SW_RFB_T prSwRfb = NULL;
    KAL_SPIN_LOCK_DECLARATION();

    DEBUGFUNC("wlanReturnPacket");

    ASSERT(prAdapter);

    prRxCtrl = &prAdapter->rRxCtrl;
    ASSERT(prRxCtrl);

    if (pvPacket) {
        kalPacketFree(prAdapter->prGlueInfo, pvPacket);
        RX_ADD_CNT(prRxCtrl, RX_DATA_RETURNED_COUNT, 1);
#if CFG_NATIVE_802_11
        if (GLUE_TEST_FLAG(prAdapter->prGlueInfo, GLUE_FLAG_HALT)) {
        }
#endif
    }

    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
    QUEUE_REMOVE_HEAD(&prRxCtrl->rIndicatedRfbList, prSwRfb, P_SW_RFB_T);
    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
    if (!prSwRfb){
        ASSERT(0);
        return;
    }

    if (nicRxSetupRFB(prAdapter, prSwRfb)){
        ASSERT(0);
        return;
    }
    nicRxReturnRFB(prAdapter, prSwRfb);
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This function is a required function that returns information about
*        the capabilities and status of the driver and/or its network adapter.
*
* \param[IN] prAdapter        Pointer to the Adapter structure.
* \param[IN] pfnOidQryHandler Function pointer for the OID query handler.
* \param[IN] pvInfoBuf        Points to a buffer for return the query information.
* \param[IN] u4QueryBufferLen Specifies the number of bytes at pvInfoBuf.
* \param[OUT] pu4QueryInfoLen  Points to the number of bytes it written or is needed.
*
* \retval WLAN_STATUS_xxx Different WLAN_STATUS code returned by different handlers.
*
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanQueryInformation (
    IN P_ADAPTER_T          prAdapter,
    IN PFN_OID_HANDLER_FUNC pfnOidQryHandler,
    IN PVOID                pvInfoBuf,
    IN UINT_32              u4InfoBufLen,
    OUT PUINT_32            pu4QryInfoLen
    )
{
    WLAN_STATUS status = WLAN_STATUS_FAILURE;

    ASSERT(prAdapter);
    ASSERT(pu4QryInfoLen);

    // ignore any OID request after connected, under PS current measurement mode
    if (prAdapter->u4PsCurrentMeasureEn &&
        (prAdapter->prGlueInfo->eParamMediaStateIndicated == PARAM_MEDIA_STATE_CONNECTED)) {
        return WLAN_STATUS_SUCCESS; // note: return WLAN_STATUS_FAILURE or WLAN_STATUS_SUCCESS for blocking OIDs during current measurement ??
    }

#if 1
    /* most OID handler will just queue a command packet */
    status = pfnOidQryHandler(prAdapter,
            pvInfoBuf,
            u4InfoBufLen,
            pu4QryInfoLen);
#else
    if (wlanIsHandlerNeedHwAccess(pfnOidQryHandler, FALSE)) {
        ACQUIRE_POWER_CONTROL_FROM_PM(prAdapter);

        /* Reset sleepy state */
        if(prAdapter->fgWiFiInSleepyState == TRUE) {
            prAdapter->fgWiFiInSleepyState = FALSE;
        }

        status = pfnOidQryHandler(prAdapter,
                                    pvInfoBuf,
                                    u4InfoBufLen,
                                    pu4QryInfoLen);

        RECLAIM_POWER_CONTROL_TO_PM(prAdapter, FALSE);
    }
    else {
        status = pfnOidQryHandler(prAdapter,
                                    pvInfoBuf,
                                    u4InfoBufLen,
                                    pu4QryInfoLen);
    }
#endif

    return status;

}

/*----------------------------------------------------------------------------*/
/*!
* \brief This function is a required function that allows bound protocol drivers,
*        or NDIS, to request changes in the state information that the miniport
*        maintains for particular object identifiers, such as changes in multicast
*        addresses.
*
* \param[IN] prAdapter     Pointer to the Glue info structure.
* \param[IN] pfnOidSetHandler     Points to the OID set handlers.
* \param[IN] pvInfoBuf     Points to a buffer containing the OID-specific data for the set.
* \param[IN] u4InfoBufLen  Specifies the number of bytes at prSetBuffer.
* \param[OUT] pu4SetInfoLen Points to the number of bytes it read or is needed.
*
* \retval WLAN_STATUS_xxx Different WLAN_STATUS code returned by different handlers.
*
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanSetInformation (
    IN P_ADAPTER_T          prAdapter,
    IN PFN_OID_HANDLER_FUNC pfnOidSetHandler,
    IN PVOID                pvInfoBuf,
    IN UINT_32              u4InfoBufLen,
    OUT PUINT_32            pu4SetInfoLen
    )
{
    WLAN_STATUS status = WLAN_STATUS_FAILURE;

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    // ignore any OID request after connected, under PS current measurement mode
    if (prAdapter->u4PsCurrentMeasureEn &&
        (prAdapter->prGlueInfo->eParamMediaStateIndicated == PARAM_MEDIA_STATE_CONNECTED)) {
        return WLAN_STATUS_SUCCESS; // note: return WLAN_STATUS_FAILURE or WLAN_STATUS_SUCCESS for blocking OIDs during current measurement ??
    }

#if 1
    /* most OID handler will just queue a command packet
     * for power state transition OIDs, handler will acquire power control by itself
     */
    status = pfnOidSetHandler(prAdapter,
            pvInfoBuf,
            u4InfoBufLen,
            pu4SetInfoLen);
#else
    if (wlanIsHandlerNeedHwAccess(pfnOidSetHandler, TRUE)) {
        ACQUIRE_POWER_CONTROL_FROM_PM(prAdapter);

        /* Reset sleepy state */
        if(prAdapter->fgWiFiInSleepyState == TRUE) {
            prAdapter->fgWiFiInSleepyState = FALSE;
        }

        status = pfnOidSetHandler(prAdapter,
                                    pvInfoBuf,
                                    u4InfoBufLen,
                                    pu4SetInfoLen);

        RECLAIM_POWER_CONTROL_TO_PM(prAdapter, FALSE);
    }
    else {
        status = pfnOidSetHandler(prAdapter,
                                    pvInfoBuf,
                                    u4InfoBufLen,
                                    pu4SetInfoLen);
    }
#endif

    return status;
}


#if CFG_SUPPORT_WAPI
/*----------------------------------------------------------------------------*/
/*!
* \brief This function is a used to query driver's config wapi mode or not
*
* \param[IN] prAdapter     Pointer to the Glue info structure.
*
* \retval TRUE for use wapi mode
*
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
wlanQueryWapiMode (
    IN P_ADAPTER_T          prAdapter
    )
{
    ASSERT(prAdapter);

    return prAdapter->rWifiVar.rConnSettings.fgWapiMode;
}
#endif


/*----------------------------------------------------------------------------*/
/*!
* \brief This function is called to set RX filter to Promiscuous Mode.
*
* \param[IN] prAdapter        Pointer to the Adapter structure.
* \param[IN] fgEnablePromiscuousMode Enable/ disable RX Promiscuous Mode.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
wlanSetPromiscuousMode (
    IN P_ADAPTER_T  prAdapter,
    IN BOOLEAN      fgEnablePromiscuousMode
    )
{
    ASSERT(prAdapter);

}

/*----------------------------------------------------------------------------*/
/*!
* \brief This function is called to set RX filter to allow to receive
*        broadcast address packets.
*
* \param[IN] prAdapter        Pointer to the Adapter structure.
* \param[IN] fgEnableBroadcast Enable/ disable broadcast packet to be received.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
wlanRxSetBroadcast (
    IN P_ADAPTER_T  prAdapter,
    IN BOOLEAN      fgEnableBroadcast
    )
{
    ASSERT(prAdapter);
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This function is called to send out CMD_NIC_POWER_CTRL command packet
*
* \param[IN] prAdapter        Pointer to the Adapter structure.
* \param[IN] ucPowerMode      refer to CMD/EVENT document
*
* \return WLAN_STATUS_SUCCESS
* \return WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanSendNicPowerCtrlCmd (
    IN P_ADAPTER_T  prAdapter,
    IN UINT_8       ucPowerMode
    )
{
    WLAN_STATUS status = WLAN_STATUS_SUCCESS;
    P_GLUE_INFO_T prGlueInfo;
    P_CMD_INFO_T prCmdInfo;
    P_WIFI_CMD_T prWifiCmd;
    UINT_8 ucTC, ucCmdSeqNum;

    ASSERT(prAdapter);

    prGlueInfo = prAdapter->prGlueInfo;

    /* 1. Prepare CMD */
    prCmdInfo = cmdBufAllocateCmdInfo(prAdapter, (CMD_HDR_SIZE + sizeof(CMD_NIC_POWER_CTRL)));
    if (!prCmdInfo) {
        DBGLOG(INIT, ERROR, ("Allocate CMD_INFO_T ==> FAILED.\n"));
        return WLAN_STATUS_FAILURE;
    }

    /* 2.1 increase command sequence number */
    ucCmdSeqNum = nicIncreaseCmdSeqNum(prAdapter);
    DBGLOG(REQ, TRACE, ("ucCmdSeqNum =%d\n", ucCmdSeqNum));

    /* 2.2 Setup common CMD Info Packet */
    prCmdInfo->eCmdType = COMMAND_TYPE_GENERAL_IOCTL;
    prCmdInfo->u2InfoBufLen = (UINT_16)(CMD_HDR_SIZE + sizeof(CMD_NIC_POWER_CTRL));
    prCmdInfo->pfCmdDoneHandler = NULL;
    prCmdInfo->pfCmdTimeoutHandler = NULL;
    prCmdInfo->fgIsOid = TRUE;
    prCmdInfo->ucCID = CMD_ID_NIC_POWER_CTRL;
    prCmdInfo->fgSetQuery = TRUE;
    prCmdInfo->fgNeedResp = FALSE;
    prCmdInfo->fgDriverDomainMCR = FALSE;
    prCmdInfo->ucCmdSeqNum = ucCmdSeqNum;
    prCmdInfo->u4SetInfoLen = sizeof(CMD_NIC_POWER_CTRL);

    /* 2.3 Setup WIFI_CMD_T */
    prWifiCmd = (P_WIFI_CMD_T)(prCmdInfo->pucInfoBuffer);
    prWifiCmd->u2TxByteCount_UserPriority = prCmdInfo->u2InfoBufLen;
    prWifiCmd->ucCID = prCmdInfo->ucCID;
    prWifiCmd->ucSetQuery = prCmdInfo->fgSetQuery;
    prWifiCmd->ucSeqNum = prCmdInfo->ucCmdSeqNum;

    kalMemZero(prWifiCmd->aucBuffer, sizeof(CMD_NIC_POWER_CTRL));
    ((P_CMD_NIC_POWER_CTRL)(prWifiCmd->aucBuffer))->ucPowerMode = ucPowerMode;

    /* 3. Issue CMD for entering specific power mode */
    ucTC = TC4_INDEX;

    while(1) {
        // 3.0 Removal check
        if(kalIsCardRemoved(prAdapter->prGlueInfo) == TRUE
                || fgIsBusAccessFailed == TRUE) {
            status = WLAN_STATUS_FAILURE;
            break;
        }

        // 3.1 Acquire TX Resource
        if (nicTxAcquireResource(prAdapter, ucTC) == WLAN_STATUS_RESOURCES) {
            if (nicTxPollingResource(prAdapter, ucTC) != WLAN_STATUS_SUCCESS) {
                DBGLOG(INIT, ERROR,("Fail to get TX resource return within timeout\n"));
                status = WLAN_STATUS_FAILURE;
                break;
            }
            else {
                continue;
            }
        }

        // 3.2 Send CMD Info Packet
        if (nicTxCmd(prAdapter, prCmdInfo, ucTC) != WLAN_STATUS_SUCCESS) {
            DBGLOG(INIT, ERROR,("Fail to transmit CMD_NIC_POWER_CTRL command\n"));
            status = WLAN_STATUS_FAILURE;
        }

        break;
    };

    // 4. Free CMD Info Packet.
    cmdBufFreeCmdInfo(prAdapter, prCmdInfo);

    // 5. Add flag
    if(ucPowerMode == 1) {
        prAdapter->fgIsEnterD3ReqIssued = TRUE;
    }

    return status;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This function is called to check if it is RF test mode and
*        the OID is allowed to be called or not
*
* \param[IN] prAdapter        Pointer to the Adapter structure.
* \param[IN] fgEnableBroadcast Enable/ disable broadcast packet to be received.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
wlanIsHandlerAllowedInRFTest (
    IN PFN_OID_HANDLER_FUNC pfnOidHandler,
    IN BOOLEAN              fgSetInfo
    )
{
    PFN_OID_HANDLER_FUNC* apfnOidHandlerAllowedInRFTest;
    UINT_32 i;
    UINT_32 u4NumOfElem;

    if (fgSetInfo) {
        apfnOidHandlerAllowedInRFTest = apfnOidSetHandlerAllowedInRFTest;
        u4NumOfElem = sizeof(apfnOidSetHandlerAllowedInRFTest) / sizeof(PFN_OID_HANDLER_FUNC);
    }
    else {
        apfnOidHandlerAllowedInRFTest = apfnOidQueryHandlerAllowedInRFTest;
        u4NumOfElem = sizeof(apfnOidQueryHandlerAllowedInRFTest) / sizeof(PFN_OID_HANDLER_FUNC);
    }

    for (i = 0; i < u4NumOfElem; i++) {
        if (apfnOidHandlerAllowedInRFTest[i] == pfnOidHandler) {
            return TRUE;
        }
    }

    return FALSE;
}

#if CFG_ENABLE_FW_DOWNLOAD
    #if CFG_ENABLE_FW_DOWNLOAD_AGGREGATION
/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to download FW image in an aggregated way
*
* @param prAdapter      Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanImageSectionDownloadAggregated (
    IN P_ADAPTER_T  prAdapter,
    IN UINT_32      u4DestAddr,
    IN UINT_32      u4ImgSecSize,
    IN PUINT_8      pucImgSecBuf
    )
{
        #if defined(MT6620) || defined(MT6628)
    P_CMD_INFO_T prCmdInfo;
    P_INIT_HIF_TX_HEADER_T prInitHifTxHeader;
    P_INIT_CMD_DOWNLOAD_BUF prInitCmdDownloadBuf;
    UINT_8 ucTC, ucCmdSeqNum;
    WLAN_STATUS u4Status = WLAN_STATUS_SUCCESS;
    PUINT_8 pucOutputBuf = (PUINT_8)NULL; /* Pointer to Transmit Data Structure Frame */
    UINT_32 u4PktCnt, u4Offset, u4Length;
    UINT_32 u4TotalLength;

    ASSERT(prAdapter);
    ASSERT(pucImgSecBuf);

    pucOutputBuf = prAdapter->rTxCtrl.pucTxCoalescingBufPtr;

    DEBUGFUNC("wlanImageSectionDownloadAggregated");

    if (u4ImgSecSize == 0) {
        return WLAN_STATUS_SUCCESS;
    }

    // 1. Allocate CMD Info Packet and Pre-fill Headers
    prCmdInfo = cmdBufAllocateCmdInfo(prAdapter,
            sizeof(INIT_HIF_TX_HEADER_T) + sizeof(INIT_CMD_DOWNLOAD_BUF) + CMD_PKT_SIZE_FOR_IMAGE);

    if (!prCmdInfo) {
        DBGLOG(INIT, ERROR, ("Allocate CMD_INFO_T ==> FAILED.\n"));
        return WLAN_STATUS_FAILURE;
    }

    prCmdInfo->u2InfoBufLen =
        sizeof(INIT_HIF_TX_HEADER_T) + sizeof(INIT_CMD_DOWNLOAD_BUF) + CMD_PKT_SIZE_FOR_IMAGE;

    // 2. Use TC0's resource to download image. (only TC0 is allowed)
    ucTC = TC0_INDEX;

    // 3. Setup common CMD Info Packet
    prInitHifTxHeader = (P_INIT_HIF_TX_HEADER_T)(prCmdInfo->pucInfoBuffer);
    prInitHifTxHeader->ucEtherTypeOffset = 0;
    prInitHifTxHeader->ucCSflags = 0;
    prInitHifTxHeader->rInitWifiCmd.ucCID = INIT_CMD_ID_DOWNLOAD_BUF;

    // 4. Setup CMD_DOWNLOAD_BUF
    prInitCmdDownloadBuf = (P_INIT_CMD_DOWNLOAD_BUF)(prInitHifTxHeader->rInitWifiCmd.aucBuffer);
    prInitCmdDownloadBuf->u4DataMode = 0
        #if CFG_ENABLE_FW_ENCRYPTION
        | DOWNLOAD_BUF_ENCRYPTION_MODE
        #endif
        ;

    // 5.0 reset loop control variable
    u4TotalLength = 0;
    u4Offset = u4PktCnt = 0;

    // 5.1 main loop for maximize transmission count per access
    while(u4Offset < u4ImgSecSize) {
        if(nicTxAcquireResource(prAdapter, ucTC) == WLAN_STATUS_SUCCESS) {
            // 5.1.1 calculate u4Length
            if(u4Offset + CMD_PKT_SIZE_FOR_IMAGE < u4ImgSecSize) {
                u4Length = CMD_PKT_SIZE_FOR_IMAGE;
            }
            else {
                u4Length = u4ImgSecSize - u4Offset;
            }

            // 5.1.1 increase command sequence number
            ucCmdSeqNum = nicIncreaseCmdSeqNum(prAdapter);
            prInitHifTxHeader->rInitWifiCmd.ucSeqNum = ucCmdSeqNum;

            // 5.1.2 update HIF TX hardware header
            prInitHifTxHeader->u2TxByteCount = ALIGN_4(sizeof(INIT_HIF_TX_HEADER_T) + sizeof(INIT_CMD_DOWNLOAD_BUF) + (UINT_16)u4Length);

            // 5.1.3 fill command header
            prInitCmdDownloadBuf->u4Address = u4DestAddr + u4Offset;
            prInitCmdDownloadBuf->u4Length = u4Length;
            prInitCmdDownloadBuf->u4CRC32 = wlanCRC32(pucImgSecBuf + u4Offset, u4Length);

            // 5.1.4.1 copy header to coalescing buffer
            kalMemCopy(pucOutputBuf + u4TotalLength,
                    (PVOID)prCmdInfo->pucInfoBuffer,
                    sizeof(INIT_HIF_TX_HEADER_T) + sizeof(INIT_CMD_DOWNLOAD_BUF));

            // 5.1.4.2 copy payload to coalescing buffer
            kalMemCopy(pucOutputBuf + u4TotalLength + sizeof(INIT_HIF_TX_HEADER_T) + sizeof(INIT_CMD_DOWNLOAD_BUF),
                    pucImgSecBuf + u4Offset,
                    u4Length);

            // 5.1.4.3 update length and other variables
            u4TotalLength += ALIGN_4(sizeof(INIT_HIF_TX_HEADER_T) + sizeof(INIT_CMD_DOWNLOAD_BUF) + u4Length);
            u4Offset += u4Length;
            u4PktCnt++;

            if(u4Offset < u4ImgSecSize) {
                continue;
            }
        }
        else if(u4PktCnt == 0) {
            /* no resource, so get some back */
            if (nicTxPollingResource(prAdapter, ucTC) != WLAN_STATUS_SUCCESS) {
                u4Status = WLAN_STATUS_FAILURE;
                DBGLOG(INIT, ERROR,("Fail to get TX resource return within timeout\n"));
                break;
            }
        }

        if(u4PktCnt != 0) {
            // start transmission
            HAL_WRITE_TX_PORT(prAdapter,
                    0,
                    u4TotalLength,
                    (PUINT_8)pucOutputBuf,
                    prAdapter->u4CoalescingBufCachedSize);

            // reset varaibles
            u4PktCnt = 0;
            u4TotalLength = 0;
        }
    }

    // 8. Free CMD Info Packet.
    cmdBufFreeCmdInfo(prAdapter, prCmdInfo);

    return u4Status;

        #else
        #error "Only MT6620/MT6628 supports firmware download in an aggregated way"

    return WLAN_STATUS_FAILURE;

        #endif
}

    #endif
/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to download FW image.
*
* @param prAdapter      Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanImageSectionDownload (
    IN P_ADAPTER_T  prAdapter,
    IN UINT_32      u4DestAddr,
    IN UINT_32      u4ImgSecSize,
    IN PUINT_8      pucImgSecBuf
    )
{
    #if defined(MT6620) || defined(MT6628)

    P_CMD_INFO_T prCmdInfo;
    P_INIT_HIF_TX_HEADER_T prInitHifTxHeader;
    P_INIT_CMD_DOWNLOAD_BUF prInitCmdDownloadBuf;
    UINT_8 ucTC, ucCmdSeqNum;
    WLAN_STATUS u4Status = WLAN_STATUS_SUCCESS;

    ASSERT(prAdapter);
    ASSERT(pucImgSecBuf);
    ASSERT(u4ImgSecSize <= CMD_PKT_SIZE_FOR_IMAGE);

    DEBUGFUNC("wlanImageSectionDownload");

    if (u4ImgSecSize == 0) {
        return WLAN_STATUS_SUCCESS;
    }

    // 1. Allocate CMD Info Packet and its Buffer.
    prCmdInfo = cmdBufAllocateCmdInfo(prAdapter,
            sizeof(INIT_HIF_TX_HEADER_T) + sizeof(INIT_CMD_DOWNLOAD_BUF) + u4ImgSecSize);

    if (!prCmdInfo) {
        DBGLOG(INIT, ERROR, ("Allocate CMD_INFO_T ==> FAILED.\n"));
        return WLAN_STATUS_FAILURE;
    }

    prCmdInfo->u2InfoBufLen =
        sizeof(INIT_HIF_TX_HEADER_T) + sizeof(INIT_CMD_DOWNLOAD_BUF) + (UINT_16)u4ImgSecSize;

    // 2. Use TC0's resource to download image. (only TC0 is allowed)
    ucTC = TC0_INDEX;

    // 3. increase command sequence number
    ucCmdSeqNum = nicIncreaseCmdSeqNum(prAdapter);

    // 4. Setup common CMD Info Packet
    prInitHifTxHeader = (P_INIT_HIF_TX_HEADER_T)(prCmdInfo->pucInfoBuffer);
    prInitHifTxHeader->rInitWifiCmd.ucCID = INIT_CMD_ID_DOWNLOAD_BUF;
    prInitHifTxHeader->rInitWifiCmd.ucSeqNum = ucCmdSeqNum;

    // 5. Setup CMD_DOWNLOAD_BUF
    prInitCmdDownloadBuf = (P_INIT_CMD_DOWNLOAD_BUF)(prInitHifTxHeader->rInitWifiCmd.aucBuffer);
    prInitCmdDownloadBuf->u4Address = u4DestAddr;
    prInitCmdDownloadBuf->u4Length = u4ImgSecSize;
    prInitCmdDownloadBuf->u4CRC32 = wlanCRC32(pucImgSecBuf, u4ImgSecSize);
    prInitCmdDownloadBuf->u4DataMode = 0
        #if CFG_ENABLE_FW_DOWNLOAD_ACK
        | DOWNLOAD_BUF_ACK_OPTION // ACK needed
        #endif
        #if CFG_ENABLE_FW_ENCRYPTION
        | DOWNLOAD_BUF_ENCRYPTION_MODE
        #endif
        ;
    kalMemCopy(prInitCmdDownloadBuf->aucBuffer, pucImgSecBuf, u4ImgSecSize);

    // 6. Send FW_Download command
    while(1) {
        // 6.1 Acquire TX Resource
        if (nicTxAcquireResource(prAdapter, ucTC) == WLAN_STATUS_RESOURCES) {
            if (nicTxPollingResource(prAdapter, ucTC) != WLAN_STATUS_SUCCESS) {
                u4Status = WLAN_STATUS_FAILURE;
                DBGLOG(INIT, ERROR,("Fail to get TX resource return within timeout\n"));
                break;
            }
            else {
                continue;
            }
        }

        // 6.2 Send CMD Info Packet
        if (nicTxInitCmd(prAdapter, prCmdInfo, ucTC) != WLAN_STATUS_SUCCESS) {
            u4Status = WLAN_STATUS_FAILURE;
            DBGLOG(INIT, ERROR,("Fail to transmit image download command\n"));
        }

        break;
    };

        #if CFG_ENABLE_FW_DOWNLOAD_ACK
    // 7. Wait for INIT_EVENT_ID_CMD_RESULT
    u4Status = wlanImageSectionDownloadStatus(prAdapter, ucCmdSeqNum);
        #endif

    // 8. Free CMD Info Packet.
    cmdBufFreeCmdInfo(prAdapter, prCmdInfo);

    return u4Status;

    #elif defined(MT5931)

    UINT_32 i, u4Value;
    P_HIF_HW_TX_HEADER_T prHifTxHeader;

    WLAN_STATUS u4Status = WLAN_STATUS_SUCCESS;

    ASSERT(prAdapter);
    ASSERT(pucImgSecBuf);
    ASSERT(u4ImgSecSize <= CMD_PKT_SIZE_FOR_IMAGE);

    DEBUGFUNC("wlanImageSectionDownload");
    DBGLOG(INIT, TRACE, ("Destination: 0x%08X / Length: 0x%08X\n", u4DestAddr, u4ImgSecSize));

    if (u4ImgSecSize == 0) {
        return WLAN_STATUS_SUCCESS;
    }

    // 1. Use TX coalescing buffer
    prHifTxHeader = (P_HIF_HW_TX_HEADER_T) prAdapter->pucCoalescingBufCached;

    // 2. Setup HIF_TX_HEADER
    prHifTxHeader->u2TxByteCount = (UINT_16)(ALIGN_4(sizeof(HIF_HW_TX_HEADER_T) + u4ImgSecSize));
    prHifTxHeader->ucEtherTypeOffset = 0;
    prHifTxHeader->ucCSflags = 0;

    // 3. Copy payload
    kalMemCopy(prHifTxHeader->aucBuffer, pucImgSecBuf, u4ImgSecSize);

    // 3.1 add 4-bytes zero tail
    kalMemZero(&(prHifTxHeader->aucBuffer[ALIGN_4(u4ImgSecSize)]), sizeof(HIF_HW_TX_HEADER_T));

    // 4. Poll til FWDL_RDY = 1
    i = 0;
    while(1) {
        HAL_MCR_RD(prAdapter, MCR_FWDLSR, &u4Value);

        if (u4Value & FWDLSR_FWDL_RDY) {
            DBGLOG(INIT, TRACE, ("FWDL_RDY detected\n"));
            break;
        }
        else if(kalIsCardRemoved(prAdapter->prGlueInfo) == TRUE
                || fgIsBusAccessFailed == TRUE) {
            u4Status = WLAN_STATUS_FAILURE;
            break;
        }
        else if(i >= CFG_RESPONSE_POLLING_TIMEOUT) {
            DBGLOG(INIT, ERROR, ("Waiting for FWDL_RDY: Timeout (0x%08X)\n", u4Value));
            u4Status = WLAN_STATUS_FAILURE;
            break;
        }
        else {
            i++;
            kalMsleep(10);
        }
    }

    // 5. Send firmware
    HAL_PORT_WR(prAdapter,
            MCR_FWDLDR,
            prHifTxHeader->u2TxByteCount,
            (PUINT_8)prHifTxHeader,
            prAdapter->u4CoalescingBufCachedSize);

    return u4Status;

    #endif
}

#if !CFG_ENABLE_FW_DOWNLOAD_ACK
/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to confirm previously firmware download is done without error
*
* @param prAdapter      Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanImageQueryStatus(
    IN P_ADAPTER_T  prAdapter
    )
{
    P_CMD_INFO_T prCmdInfo;
    P_INIT_HIF_TX_HEADER_T prInitHifTxHeader;
    UINT_8 aucBuffer[sizeof(INIT_HIF_RX_HEADER_T) + sizeof(INIT_EVENT_PENDING_ERROR)];
    UINT_32 u4RxPktLength;
    P_INIT_HIF_RX_HEADER_T prInitHifRxHeader;
    P_INIT_EVENT_PENDING_ERROR prEventPendingError;
    WLAN_STATUS u4Status = WLAN_STATUS_SUCCESS;
    UINT_8 ucTC, ucCmdSeqNum;

    ASSERT(prAdapter);

    DEBUGFUNC("wlanImageQueryStatus");

    // 1. Allocate CMD Info Packet and it Buffer.
    prCmdInfo = cmdBufAllocateCmdInfo(prAdapter, sizeof(INIT_HIF_TX_HEADER_T));

    if (!prCmdInfo) {
        DBGLOG(INIT, ERROR, ("Allocate CMD_INFO_T ==> FAILED.\n"));
        return WLAN_STATUS_FAILURE;
    }

    kalMemZero(prCmdInfo, sizeof(INIT_HIF_TX_HEADER_T));
    prCmdInfo->u2InfoBufLen = sizeof(INIT_HIF_TX_HEADER_T);

    // 2. Use TC0's resource to download image. (only TC0 is allowed)
    ucTC = TC0_INDEX;

    // 3. increase command sequence number
    ucCmdSeqNum = nicIncreaseCmdSeqNum(prAdapter);

    // 4. Setup common CMD Info Packet
    prInitHifTxHeader = (P_INIT_HIF_TX_HEADER_T)(prCmdInfo->pucInfoBuffer);
    prInitHifTxHeader->rInitWifiCmd.ucCID = INIT_CMD_ID_QUERY_PENDING_ERROR;
    prInitHifTxHeader->rInitWifiCmd.ucSeqNum = ucCmdSeqNum;

    // 5. Send command
    while(1) {
        // 5.1 Acquire TX Resource
        if (nicTxAcquireResource(prAdapter, ucTC) == WLAN_STATUS_RESOURCES) {
            if (nicTxPollingResource(prAdapter, ucTC) != WLAN_STATUS_SUCCESS) {
                u4Status = WLAN_STATUS_FAILURE;
                DBGLOG(INIT, ERROR,("Fail to get TX resource return within timeout\n"));
                break;
            }
            else {
                continue;
            }
        }

        // 5.2 Send CMD Info Packet
        if (nicTxInitCmd(prAdapter, prCmdInfo, ucTC) != WLAN_STATUS_SUCCESS) {
            u4Status = WLAN_STATUS_FAILURE;
            DBGLOG(INIT, ERROR,("Fail to transmit image download command\n"));
        }

        break;
    };

    // 6. Wait for INIT_EVENT_ID_PENDING_ERROR
    do {
        if(kalIsCardRemoved(prAdapter->prGlueInfo) == TRUE
                || fgIsBusAccessFailed == TRUE) {
            u4Status = WLAN_STATUS_FAILURE;
            DBGLOG(INIT, ERROR, ("Bus error(%d)/Card removed(%d)\n", fgIsBusAccessFailed, kalIsCardRemoved(prAdapter->prGlueInfo)));
        }
        else if(nicRxWaitResponse(prAdapter,
                    0,
                    aucBuffer,
                    sizeof(INIT_HIF_RX_HEADER_T) + sizeof(INIT_EVENT_PENDING_ERROR),
                    &u4RxPktLength) != WLAN_STATUS_SUCCESS) {
            u4Status = WLAN_STATUS_FAILURE;
            DBGLOG(INIT, ERROR, ("No RX response\n"));
        }
        else {
            prInitHifRxHeader = (P_INIT_HIF_RX_HEADER_T) aucBuffer;

            // EID / SeqNum check
            if(prInitHifRxHeader->rInitWifiEvent.ucEID != INIT_EVENT_ID_PENDING_ERROR) {
                u4Status = WLAN_STATUS_FAILURE;
                DBGLOG(INIT, ERROR, ("EVENT-ID Mismatch: %d\n", prInitHifRxHeader->rInitWifiEvent.ucEID));
            }
            else if(prInitHifRxHeader->rInitWifiEvent.ucSeqNum != ucCmdSeqNum) {
                u4Status = WLAN_STATUS_FAILURE;
                DBGLOG(INIT, ERROR, ("SEQ-NUM Mismatch: %d (expected: %d)\n", prInitHifRxHeader->rInitWifiEvent.ucSeqNum, ucCmdSeqNum));
            }
            else {
                prEventPendingError = (P_INIT_EVENT_PENDING_ERROR) (prInitHifRxHeader->rInitWifiEvent.aucBuffer);
                if(prEventPendingError->ucStatus != 0) { // 0 for download success
                    u4Status = WLAN_STATUS_FAILURE;
                    DBGLOG(INIT, ERROR, ("ERROR CODE: %d\n", prEventCmdResult->ucStatus));
                }
                else {
                    u4Status = WLAN_STATUS_SUCCESS;
                }
            }
        }
    } while (FALSE);

    // 7. Free CMD Info Packet.
    cmdBufFreeCmdInfo(prAdapter, prCmdInfo);

    return u4Status;
}


#else
/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to confirm the status of
*        previously downloaded firmware scatter
*
* @param prAdapter      Pointer to the Adapter structure.
*        ucCmdSeqNum    Sequence number of previous firmware scatter
*
* @return WLAN_STATUS_SUCCESS
*         WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanImageSectionDownloadStatus (
    IN P_ADAPTER_T  prAdapter,
    IN UINT_8       ucCmdSeqNum
    )
{
    UINT_8 aucBuffer[sizeof(INIT_HIF_RX_HEADER_T) + sizeof(INIT_EVENT_CMD_RESULT)];
    P_INIT_HIF_RX_HEADER_T prInitHifRxHeader;
    P_INIT_EVENT_CMD_RESULT prEventCmdResult;
    UINT_32 u4RxPktLength;
    WLAN_STATUS u4Status;

    ASSERT(prAdapter);

    do {
        if(kalIsCardRemoved(prAdapter->prGlueInfo) == TRUE
                || fgIsBusAccessFailed == TRUE) {
            u4Status = WLAN_STATUS_FAILURE;
            DBGLOG(INIT, ERROR, ("Bus error(%d)/Card removed(%d)\n", fgIsBusAccessFailed, kalIsCardRemoved(prAdapter->prGlueInfo)));
        }
        else if(nicRxWaitResponse(prAdapter,
                    0,
                    aucBuffer,
                    sizeof(INIT_HIF_RX_HEADER_T) + sizeof(INIT_EVENT_CMD_RESULT),
                    &u4RxPktLength) != WLAN_STATUS_SUCCESS) {
            u4Status = WLAN_STATUS_FAILURE;
            DBGLOG(INIT, ERROR, ("No RX response\n"));
        }
        else {
            prInitHifRxHeader = (P_INIT_HIF_RX_HEADER_T) aucBuffer;

            // EID / SeqNum check
            if(prInitHifRxHeader->rInitWifiEvent.ucEID != INIT_EVENT_ID_CMD_RESULT) {
                u4Status = WLAN_STATUS_FAILURE;
                DBGLOG(INIT, ERROR, ("EVENT-ID Mismatch: %d\n", prInitHifRxHeader->rInitWifiEvent.ucEID));
            }
            else if(prInitHifRxHeader->rInitWifiEvent.ucSeqNum != ucCmdSeqNum) {
                u4Status = WLAN_STATUS_FAILURE;
                DBGLOG(INIT, ERROR, ("SEQ-NUM Mismatch: %d\n", prInitHifRxHeader->rInitWifiEvent.ucSeqNum));
            }
            else {
                prEventCmdResult = (P_INIT_EVENT_CMD_RESULT) (prInitHifRxHeader->rInitWifiEvent.aucBuffer);
                if(prEventCmdResult->ucStatus != 0) { // 0 for download success
                    u4Status = WLAN_STATUS_FAILURE;
                    DBGLOG(INIT, ERROR, ("ERROR CODE: %d\n", prEventCmdResult->ucStatus));
                }
                else {
                    u4Status = WLAN_STATUS_SUCCESS;
                }
            }
        }
    } while (FALSE);

    return u4Status;
}


#endif
/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to start FW normal operation.
*
* @param prAdapter      Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanConfigWifiFunc (
    IN P_ADAPTER_T  prAdapter,
    IN BOOLEAN      fgEnable,
    IN UINT_32      u4StartAddress
    )
{
    P_CMD_INFO_T prCmdInfo;
    P_INIT_HIF_TX_HEADER_T prInitHifTxHeader;
    P_INIT_CMD_WIFI_START prInitCmdWifiStart;
    UINT_8 ucTC, ucCmdSeqNum;
    WLAN_STATUS u4Status = WLAN_STATUS_SUCCESS;

    ASSERT(prAdapter);

    DEBUGFUNC("wlanConfigWifiFunc");

     // 1. Allocate CMD Info Packet and its Buffer.
    prCmdInfo = cmdBufAllocateCmdInfo(prAdapter,
            sizeof(INIT_HIF_TX_HEADER_T) + sizeof(INIT_CMD_WIFI_START));

    if (!prCmdInfo) {
        DBGLOG(INIT, ERROR, ("Allocate CMD_INFO_T ==> FAILED.\n"));
        return WLAN_STATUS_FAILURE;
    }

    kalMemZero(prCmdInfo, sizeof(INIT_HIF_TX_HEADER_T) + sizeof(INIT_CMD_WIFI_START));
    prCmdInfo->u2InfoBufLen =
        sizeof(INIT_HIF_TX_HEADER_T) + sizeof(INIT_CMD_WIFI_START);

    // 2. Always use TC0
    ucTC = TC0_INDEX;

    // 3. increase command sequence number
    ucCmdSeqNum = nicIncreaseCmdSeqNum(prAdapter);

    // 4. Setup common CMD Info Packet
    prInitHifTxHeader = (P_INIT_HIF_TX_HEADER_T)(prCmdInfo->pucInfoBuffer);
    prInitHifTxHeader->rInitWifiCmd.ucCID = INIT_CMD_ID_WIFI_START;
    prInitHifTxHeader->rInitWifiCmd.ucSeqNum = ucCmdSeqNum;

    prInitCmdWifiStart = (P_INIT_CMD_WIFI_START)(prInitHifTxHeader->rInitWifiCmd.aucBuffer);
    prInitCmdWifiStart->u4Override = (fgEnable == TRUE ? 1 : 0);
    prInitCmdWifiStart->u4Address = u4StartAddress;

    // 5. Seend WIFI start command
    while(1) {
        // 5.1 Acquire TX Resource
        if (nicTxAcquireResource(prAdapter, ucTC) == WLAN_STATUS_RESOURCES) {
            if (nicTxPollingResource(prAdapter, ucTC) != WLAN_STATUS_SUCCESS) {
                u4Status = WLAN_STATUS_FAILURE;
                DBGLOG(INIT, ERROR,("Fail to get TX resource return within timeout\n"));
                break;
            }
            else {
                continue;
            }
        }

        // 5.2 Send CMD Info Packet
        if (nicTxInitCmd(prAdapter, prCmdInfo, ucTC) != WLAN_STATUS_SUCCESS) {
            u4Status = WLAN_STATUS_FAILURE;
            DBGLOG(INIT, ERROR,("Fail to transmit WIFI start command\n"));
        }

        break;
    };

    // 6. Free CMD Info Packet.
    cmdBufFreeCmdInfo(prAdapter, prCmdInfo);

    return u4Status;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to generate CRC32 checksum
*
* @param buf Pointer to the data.
* @param len data length
*
* @return crc32 value
*/
/*----------------------------------------------------------------------------*/
UINT_32 wlanCRC32(
    PUINT_8 buf,
    UINT_32 len)
{
    UINT_32 i, crc32 = 0xFFFFFFFF;
    const UINT_32 crc32_ccitt_table[256] = {
        0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419,
        0x706af48f, 0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4,
        0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07,
        0x90bf1d91, 0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
        0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7, 0x136c9856,
        0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
        0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4,
        0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
        0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3,
        0x45df5c75, 0xdcd60dcf, 0xabd13d59, 0x26d930ac, 0x51de003a,
        0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599,
        0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
        0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190,
        0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f,
        0x9fbfe4a5, 0xe8b8d433, 0x7807c9a2, 0x0f00f934, 0x9609a88e,
        0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
        0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed,
        0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
        0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3,
        0xfbd44c65, 0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
        0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a,
        0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5,
        0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa, 0xbe0b1010,
        0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
        0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17,
        0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6,
        0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615,
        0x73dc1683, 0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
        0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1, 0xf00f9344,
        0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
        0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a,
        0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
        0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1,
        0xa6bc5767, 0x3fb506dd, 0x48b2364b, 0xd80d2bda, 0xaf0a1b4c,
        0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef,
        0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
        0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe,
        0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31,
        0x2cd99e8b, 0x5bdeae1d, 0x9b64c2b0, 0xec63f226, 0x756aa39c,
        0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
        0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b,
        0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
        0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1,
        0x18b74777, 0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
        0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45, 0xa00ae278,
        0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7,
        0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc, 0x40df0b66,
        0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
        0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605,
        0xcdd70693, 0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8,
        0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b,
        0x2d02ef8d };

    for (i = 0; i < len; i++)
        crc32 = crc32_ccitt_table[(crc32 ^ buf[i]) & 0xff] ^ (crc32 >> 8);

    return ( ~crc32 );
}
#endif


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to process queued RX packets
*
* @param prAdapter          Pointer to the Adapter structure.
*        prSwRfbListHead    Pointer to head of RX packets link list
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanProcessQueuedSwRfb (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfbListHead
    )
{
    P_SW_RFB_T prSwRfb, prNextSwRfb;
    P_TX_CTRL_T prTxCtrl;
    P_RX_CTRL_T prRxCtrl;

    ASSERT(prAdapter);
    ASSERT(prSwRfbListHead);

    prTxCtrl = &prAdapter->rTxCtrl;
    prRxCtrl = &prAdapter->rRxCtrl;

    prSwRfb = prSwRfbListHead;

    do {
        // save next first
        prNextSwRfb = (P_SW_RFB_T)QUEUE_GET_NEXT_ENTRY((P_QUE_ENTRY_T)prSwRfb);

        switch(prSwRfb->eDst) {
        case RX_PKT_DESTINATION_HOST:
            nicRxProcessPktWithoutReorder(prAdapter, prSwRfb);
            break;

        case RX_PKT_DESTINATION_FORWARD:
            nicRxProcessForwardPkt(prAdapter, prSwRfb);
            break;

        case RX_PKT_DESTINATION_HOST_WITH_FORWARD:
            nicRxProcessGOBroadcastPkt(prAdapter, prSwRfb);
            break;

        case RX_PKT_DESTINATION_NULL:
            nicRxReturnRFB(prAdapter, prSwRfb);
            break;

        default:
            break;
        }

#if CFG_HIF_RX_STARVATION_WARNING
        prRxCtrl->u4DequeuedCnt++;
#endif
        prSwRfb = prNextSwRfb;
    } while(prSwRfb);

    return WLAN_STATUS_SUCCESS;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to purge queued TX packets
*        by indicating failure to OS and returned to free list
*
* @param prAdapter          Pointer to the Adapter structure.
*        prMsduInfoListHead Pointer to head of TX packets link list
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanProcessQueuedMsduInfo (
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfoListHead
    )
{
    ASSERT(prAdapter);
    ASSERT(prMsduInfoListHead);

    nicTxFreeMsduInfoPacket(prAdapter, prMsduInfoListHead);
    nicTxReturnMsduInfo(prAdapter, prMsduInfoListHead);

    return WLAN_STATUS_SUCCESS;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to check if the OID handler needs timeout
*
* @param prAdapter          Pointer to the Adapter structure.
*        pfnOidHandler      Pointer to the OID handler
*
* @return TRUE
*         FALSE
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
wlanoidTimeoutCheck (
    IN P_ADAPTER_T prAdapter,
    IN PFN_OID_HANDLER_FUNC pfnOidHandler
    )
{
    PFN_OID_HANDLER_FUNC* apfnOidHandlerWOTimeoutCheck;
    UINT_32 i;
    UINT_32 u4NumOfElem;

    apfnOidHandlerWOTimeoutCheck = apfnOidWOTimeoutCheck;
    u4NumOfElem = sizeof(apfnOidWOTimeoutCheck) / sizeof(PFN_OID_HANDLER_FUNC);

    for (i = 0; i < u4NumOfElem; i++) {
        if (apfnOidHandlerWOTimeoutCheck[i] == pfnOidHandler) {
            return FALSE;
        }
    }

    // set timer if need timeout check
    cnmTimerStartTimer(prAdapter,
            &(prAdapter->rOidTimeoutTimer),
            1000);

    return TRUE;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to clear any pending OID timeout check
*
* @param prAdapter          Pointer to the Adapter structure.
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID
wlanoidClearTimeoutCheck (
    IN P_ADAPTER_T prAdapter
    )
{
    ASSERT(prAdapter);

    cnmTimerStopTimer(prAdapter, &(prAdapter->rOidTimeoutTimer));
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to set up the MCUSYS's OSC stable time
*
* @param prAdapter          Pointer to the Adapter structure.
*
* @return none
*/
/*----------------------------------------------------------------------------*/

#if CFG_SUPPORT_OSC_SETTING && defined(MT5931)
WLAN_STATUS
wlanSetMcuOscStableTime (
    IN P_ADAPTER_T      prAdapter,
    IN UINT_16          u2OscStableTime
    )
{
    UINT_8                  ucCmdSeqNum = 0;
    P_CMD_INFO_T            prCmdInfo = NULL;
    P_WIFI_CMD_T            prWifiCmd = NULL;
    P_CMD_MCU_LP_PARAM_T    prMcuSetOscCmd = NULL;
    WLAN_STATUS             status = WLAN_STATUS_SUCCESS;

    ASSERT(prAdapter);

    prCmdInfo = cmdBufAllocateCmdInfo(prAdapter,
            CMD_HDR_SIZE + sizeof(CMD_MCU_LP_PARAM_T));

    if (!prCmdInfo) {
        DBGLOG(INIT, ERROR, ("Allocate CMD_INFO_T ==> FAILED.\n"));
        return WLAN_STATUS_FAILURE;
    }

    // increase command sequence number
    ucCmdSeqNum = nicIncreaseCmdSeqNum(prAdapter);

    // compose CMD_MCU_LP_PARAM_T cmd pkt
    prCmdInfo->eCmdType = COMMAND_TYPE_GENERAL_IOCTL;
    prCmdInfo->u2InfoBufLen = CMD_HDR_SIZE + sizeof(CMD_MCU_LP_PARAM_T);
    prCmdInfo->pfCmdDoneHandler = NULL;
    prCmdInfo->pfCmdTimeoutHandler = NULL;
    prCmdInfo->fgIsOid = FALSE;
    prCmdInfo->ucCID = CMD_ID_SET_OSC;
    prCmdInfo->fgSetQuery = TRUE;
    prCmdInfo->fgNeedResp = FALSE;
    prCmdInfo->fgDriverDomainMCR = FALSE;
    prCmdInfo->ucCmdSeqNum = ucCmdSeqNum;
    prCmdInfo->u4SetInfoLen = sizeof(CMD_MCU_LP_PARAM_T);

    // Setup WIFI_CMD_T
    prWifiCmd = (P_WIFI_CMD_T)(prCmdInfo->pucInfoBuffer);
    prWifiCmd->u2TxByteCount_UserPriority = prCmdInfo->u2InfoBufLen;
    prWifiCmd->ucCID = prCmdInfo->ucCID;
    prWifiCmd->ucSetQuery = prCmdInfo->fgSetQuery;
    prWifiCmd->ucSeqNum = prCmdInfo->ucCmdSeqNum;

    // configure CMD_MCU_LP_PARAM_T
    prMcuSetOscCmd = (P_CMD_MCU_LP_PARAM_T)(prWifiCmd->aucBuffer);
    prMcuSetOscCmd->u2OscStableTime = u2OscStableTime;

    status = wlanSendCommand(prAdapter, prCmdInfo);
    cmdBufFreeCmdInfo(prAdapter, prCmdInfo);

    return status;
}
#endif

/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to update network address in firmware domain
*
* @param prAdapter          Pointer to the Adapter structure.
*
* @return WLAN_STATUS_FAILURE   The request could not be processed
*         WLAN_STATUS_PENDING   The request has been queued for later processing
*         WLAN_STATUS_SUCCESS   The request has been processed
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanUpdateNetworkAddress (
    IN P_ADAPTER_T prAdapter
    )
{
    const UINT_8 aucZeroMacAddr[] = NULL_MAC_ADDR;
    PARAM_MAC_ADDRESS rMacAddr;
    UINT_8 ucCmdSeqNum;
    P_CMD_INFO_T prCmdInfo;
    P_WIFI_CMD_T prWifiCmd;
    P_CMD_BASIC_CONFIG prCmdBasicConfig;
    UINT_32 u4SysTime;

    ASSERT(prAdapter);

    if(kalRetrieveNetworkAddress(prAdapter->prGlueInfo, &rMacAddr) == FALSE
            || IS_BMCAST_MAC_ADDR(rMacAddr)
            || EQUAL_MAC_ADDR(aucZeroMacAddr, rMacAddr)) {
        // eFUSE has a valid address, don't do anything
        if(prAdapter->fgIsEmbbededMacAddrValid == TRUE) {
            return WLAN_STATUS_SUCCESS;
        }
        else {
            // dynamic generate
            u4SysTime = (UINT_32) kalGetTimeTick();

            rMacAddr[0] = 0x00;
            rMacAddr[1] = 0x08;
            rMacAddr[2] = 0x22;

            kalMemCopy(&rMacAddr[3], &u4SysTime, 3);
        }
    }

    prCmdInfo = cmdBufAllocateCmdInfo(prAdapter,
            CMD_HDR_SIZE + sizeof(CMD_BASIC_CONFIG));

    if (!prCmdInfo) {
        DBGLOG(INIT, ERROR, ("Allocate CMD_INFO_T ==> FAILED.\n"));
        return WLAN_STATUS_FAILURE;
    }

    // increase command sequence number
    ucCmdSeqNum = nicIncreaseCmdSeqNum(prAdapter);

    // compose CMD_BUILD_CONNECTION cmd pkt
    prCmdInfo->eCmdType = COMMAND_TYPE_GENERAL_IOCTL;
    prCmdInfo->u2InfoBufLen = CMD_HDR_SIZE + sizeof(CMD_BASIC_CONFIG);
    prCmdInfo->pfCmdDoneHandler = NULL;
    prCmdInfo->pfCmdTimeoutHandler = NULL;
    prCmdInfo->fgIsOid = FALSE;
    prCmdInfo->ucCID = CMD_ID_BASIC_CONFIG;
    prCmdInfo->fgSetQuery = TRUE;
    prCmdInfo->fgNeedResp = FALSE;
    prCmdInfo->fgDriverDomainMCR = FALSE;
    prCmdInfo->ucCmdSeqNum = ucCmdSeqNum;
    prCmdInfo->u4SetInfoLen = sizeof(CMD_BASIC_CONFIG);

    // Setup WIFI_CMD_T
    prWifiCmd = (P_WIFI_CMD_T)(prCmdInfo->pucInfoBuffer);
    prWifiCmd->u2TxByteCount_UserPriority = prCmdInfo->u2InfoBufLen;
    prWifiCmd->ucCID = prCmdInfo->ucCID;
    prWifiCmd->ucSetQuery = prCmdInfo->fgSetQuery;
    prWifiCmd->ucSeqNum = prCmdInfo->ucCmdSeqNum;

    // configure CMD_BASIC_CONFIG
    prCmdBasicConfig = (P_CMD_BASIC_CONFIG)(prWifiCmd->aucBuffer);
    kalMemCopy(&(prCmdBasicConfig->rMyMacAddr), &rMacAddr, PARAM_MAC_ADDR_LEN);
    prCmdBasicConfig->ucNative80211 = 0;
    prCmdBasicConfig->rCsumOffload.u2RxChecksum = 0;
    prCmdBasicConfig->rCsumOffload.u2TxChecksum = 0;

#if CFG_TCP_IP_CHKSUM_OFFLOAD
    if(prAdapter->u4CSUMFlags & CSUM_OFFLOAD_EN_TX_TCP)
        prCmdBasicConfig->rCsumOffload.u2TxChecksum |= BIT(2);

    if(prAdapter->u4CSUMFlags & CSUM_OFFLOAD_EN_TX_UDP)
        prCmdBasicConfig->rCsumOffload.u2TxChecksum |= BIT(1);

    if(prAdapter->u4CSUMFlags & CSUM_OFFLOAD_EN_TX_IP)
        prCmdBasicConfig->rCsumOffload.u2TxChecksum |= BIT(0);

    if(prAdapter->u4CSUMFlags & CSUM_OFFLOAD_EN_RX_TCP)
        prCmdBasicConfig->rCsumOffload.u2RxChecksum |= BIT(2);

    if(prAdapter->u4CSUMFlags & CSUM_OFFLOAD_EN_RX_UDP)
        prCmdBasicConfig->rCsumOffload.u2RxChecksum |= BIT(1);

    if(prAdapter->u4CSUMFlags & (CSUM_OFFLOAD_EN_RX_IPv4 | CSUM_OFFLOAD_EN_RX_IPv6))
        prCmdBasicConfig->rCsumOffload.u2RxChecksum |= BIT(0);
#endif

    if(wlanSendCommand(prAdapter, prCmdInfo) == WLAN_STATUS_RESOURCES) {
        prCmdInfo->pfCmdDoneHandler = nicCmdEventQueryAddress;
        kalEnqueueCommand(prAdapter->prGlueInfo, (P_QUE_ENTRY_T)prCmdInfo);

        return WLAN_STATUS_PENDING;
    }
    else {
        nicCmdEventQueryAddress(prAdapter, prCmdInfo, (PUINT_8)prCmdBasicConfig);
        cmdBufFreeCmdInfo(prAdapter, prCmdInfo);

        return WLAN_STATUS_SUCCESS;
    }
}

/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to check if the device is in RF test mode
*
* @param pfnOidHandler      Pointer to the OID handler
*
* @return TRUE
*         FALSE
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
wlanQueryTestMode(
    IN P_ADAPTER_T          prAdapter
    )
{
    ASSERT(prAdapter);

    return prAdapter->fgTestMode;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to identify 802.1x and Bluetooth-over-Wi-Fi
*        security frames, and queued into command queue for strict ordering
*        due to 802.1x frames before add-key OIDs are not to be encrypted
*
* @param prAdapter      Pointer of Adapter Data Structure
* @param prPacket       Pointer of native packet
*
* @return TRUE
*         FALSE
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
wlanProcessSecurityFrame(
    IN P_ADAPTER_T      prAdapter,
    IN P_NATIVE_PACKET  prPacket
    )
{
    UINT_8          ucPriorityParam;
    UINT_8          aucEthDestAddr[PARAM_MAC_ADDR_LEN];
    BOOLEAN         fgIs1x = FALSE;
    BOOLEAN         fgIsPAL = FALSE;
    UINT_32         u4PacketLen;
    ULONG           u4SysTime;
    UINT_8          ucNetworkType;
    P_CMD_INFO_T    prCmdInfo;

    ASSERT(prAdapter);
    ASSERT(prPacket);

    if (kalQoSFrameClassifierAndPacketInfo(prAdapter->prGlueInfo,
                prPacket,
                &ucPriorityParam,
                &u4PacketLen,
                aucEthDestAddr,
                &fgIs1x,
                &fgIsPAL,
                &ucNetworkType) == TRUE) {
        if(fgIs1x == FALSE) {
            return FALSE;
        }
        else {
            KAL_SPIN_LOCK_DECLARATION();
            KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_CMD_RESOURCE);
            QUEUE_REMOVE_HEAD(&prAdapter->rFreeCmdList, prCmdInfo, P_CMD_INFO_T);
            KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_CMD_RESOURCE);

            DBGLOG(RSN, INFO, ("T1X len=%d\n", u4PacketLen));

            if (prCmdInfo) {
                P_STA_RECORD_T          prStaRec;
                // fill arrival time
                u4SysTime = (OS_SYSTIME)kalGetTimeTick();
                GLUE_SET_PKT_ARRIVAL_TIME(prPacket, u4SysTime);

                kalMemZero(prCmdInfo, sizeof(CMD_INFO_T));

                prCmdInfo->eCmdType             = COMMAND_TYPE_SECURITY_FRAME;
                prCmdInfo->u2InfoBufLen         = (UINT_16)u4PacketLen;
                prCmdInfo->pucInfoBuffer        = NULL;
                prCmdInfo->prPacket             = prPacket;
#if 0
                prCmdInfo->ucStaRecIndex        = qmGetStaRecIdx(prAdapter,
                                                                    aucEthDestAddr,
                                                                    (ENUM_NETWORK_TYPE_INDEX_T)ucNetworkType);
#endif
                prStaRec                        = cnmGetStaRecByAddress(prAdapter,
                                                                        (ENUM_NETWORK_TYPE_INDEX_T)ucNetworkType,
                                                                        aucEthDestAddr);
                if(prStaRec) {
                    prCmdInfo->ucStaRecIndex =  prStaRec->ucIndex;
                }
                else {
                    prCmdInfo->ucStaRecIndex =  STA_REC_INDEX_NOT_FOUND;
                }

                prCmdInfo->eNetworkType         = (ENUM_NETWORK_TYPE_INDEX_T)ucNetworkType;
                prCmdInfo->pfCmdDoneHandler     = wlanSecurityFrameTxDone;
                prCmdInfo->pfCmdTimeoutHandler  = wlanSecurityFrameTxTimeout;
                prCmdInfo->fgIsOid              = FALSE;
                prCmdInfo->fgSetQuery           = TRUE;
                prCmdInfo->fgNeedResp           = FALSE;

                kalEnqueueCommand(prAdapter->prGlueInfo, (P_QUE_ENTRY_T)prCmdInfo);

                return TRUE;
            }
            else {
                ASSERT(0);
                return FALSE;
            }
        }
    }
    else {
        return FALSE;
    }
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called when 802.1x or Bluetooth-over-Wi-Fi
*        security frames has been sent to firmware
*
* @param prAdapter      Pointer of Adapter Data Structure
* @param prCmdInfo      Pointer of CMD_INFO_T
* @param pucEventBuf    meaningless, only for API compatibility
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID
wlanSecurityFrameTxDone(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    )
{
    ASSERT(prAdapter);
    ASSERT(prCmdInfo);
    if (prCmdInfo->eNetworkType == NETWORK_TYPE_AIS_INDEX &&
        prAdapter->rWifiVar.rAisSpecificBssInfo.fgCounterMeasure) {
        P_STA_RECORD_T prSta = cnmGetStaRecByIndex(prAdapter, prCmdInfo->ucStaRecIndex);
        if (prSta) {
            kalMsleep(10);
            secFsmEventEapolTxDone(prAdapter, prSta, TX_RESULT_SUCCESS);
        }
    }

    kalSecurityFrameSendComplete(prAdapter->prGlueInfo,
            prCmdInfo->prPacket,
            WLAN_STATUS_SUCCESS);
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called when 802.1x or Bluetooth-over-Wi-Fi
*        security frames has failed sending to firmware
*
* @param prAdapter      Pointer of Adapter Data Structure
* @param prCmdInfo      Pointer of CMD_INFO_T
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID
wlanSecurityFrameTxTimeout(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo
    )
{
    ASSERT(prAdapter);
    ASSERT(prCmdInfo);

    kalSecurityFrameSendComplete(prAdapter->prGlueInfo,
            prCmdInfo->prPacket,
            WLAN_STATUS_FAILURE);
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called before AIS is starting a new scan
*
* @param prAdapter      Pointer of Adapter Data Structure
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID
wlanClearScanningResult(
    IN P_ADAPTER_T  prAdapter
    )
{
    BOOLEAN fgKeepCurrOne = FALSE;
    UINT_32 i;

    ASSERT(prAdapter);

    // clear scanning result
    if(kalGetMediaStateIndicated(prAdapter->prGlueInfo) == PARAM_MEDIA_STATE_CONNECTED) {
        for(i = 0 ; i < prAdapter->rWlanInfo.u4ScanResultNum ; i++) {
            if(EQUAL_MAC_ADDR(prAdapter->rWlanInfo.rCurrBssId.arMacAddress,
                        prAdapter->rWlanInfo.arScanResult[i].arMacAddress)) {
                fgKeepCurrOne = TRUE;

                if(i != 0) {
                    // copy structure
                    kalMemCopy(&(prAdapter->rWlanInfo.arScanResult[0]),
                            &(prAdapter->rWlanInfo.arScanResult[i]),
                            OFFSET_OF(PARAM_BSSID_EX_T, aucIEs));
                }

                if(prAdapter->rWlanInfo.arScanResult[i].u4IELength > 0) {
                    if(prAdapter->rWlanInfo.apucScanResultIEs[i] != &(prAdapter->rWlanInfo.aucScanIEBuf[0])) {
                        // move IEs to head
                        kalMemCopy(prAdapter->rWlanInfo.aucScanIEBuf,
                                prAdapter->rWlanInfo.apucScanResultIEs[i],
                                prAdapter->rWlanInfo.arScanResult[i].u4IELength);
                    }

                    // modify IE pointer
                    prAdapter->rWlanInfo.apucScanResultIEs[0] = &(prAdapter->rWlanInfo.aucScanIEBuf[0]);
                }
                else {
                    prAdapter->rWlanInfo.apucScanResultIEs[0] = NULL;
                }

                break;
            }
        }
    }

    if(fgKeepCurrOne == TRUE) {
        prAdapter->rWlanInfo.u4ScanResultNum = 1;
        prAdapter->rWlanInfo.u4ScanIEBufferUsage =
            ALIGN_4(prAdapter->rWlanInfo.arScanResult[0].u4IELength);
    }
    else {
        prAdapter->rWlanInfo.u4ScanResultNum = 0;
        prAdapter->rWlanInfo.u4ScanIEBufferUsage = 0;
    }

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called when AIS received a beacon timeout event
*
* @param prAdapter      Pointer of Adapter Data Structure
* @param arBSSID        MAC address of the specified BSS
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID
wlanClearBssInScanningResult(
    IN P_ADAPTER_T      prAdapter,
    IN PUINT_8          arBSSID
    )
{
    UINT_32 i, j, u4IELength = 0, u4IEMoveLength;
    PUINT_8 pucIEPtr;

    ASSERT(prAdapter);

    // clear scanning result
    i = 0;
    while(1) {
        if(i >= prAdapter->rWlanInfo.u4ScanResultNum) {
            break;
        }

        if(EQUAL_MAC_ADDR(arBSSID, prAdapter->rWlanInfo.arScanResult[i].arMacAddress)) {
            // backup current IE length
            u4IELength = ALIGN_4(prAdapter->rWlanInfo.arScanResult[i].u4IELength);
            pucIEPtr = prAdapter->rWlanInfo.apucScanResultIEs[i];

            // removed from middle
            for(j = i + 1 ; j < prAdapter->rWlanInfo.u4ScanResultNum ; j++) {
                kalMemCopy(&(prAdapter->rWlanInfo.arScanResult[j-1]),
                        &(prAdapter->rWlanInfo.arScanResult[j]),
                        OFFSET_OF(PARAM_BSSID_EX_T, aucIEs));

                prAdapter->rWlanInfo.apucScanResultIEs[j-1] =
                    prAdapter->rWlanInfo.apucScanResultIEs[j];
            }

            prAdapter->rWlanInfo.u4ScanResultNum--;

            // remove IE buffer if needed := move rest of IE buffer
            if(u4IELength > 0) {
                u4IEMoveLength = prAdapter->rWlanInfo.u4ScanIEBufferUsage -
                    (((UINT_32)pucIEPtr) + u4IELength - ((UINT_32)(&(prAdapter->rWlanInfo.aucScanIEBuf[0]))));

                kalMemCopy(pucIEPtr,
                        (PUINT_8)(((UINT_32)pucIEPtr) + u4IELength),
                        u4IEMoveLength);

                prAdapter->rWlanInfo.u4ScanIEBufferUsage -= u4IELength;

                // correction of pointers to IE buffer
                for(j = 0 ; j < prAdapter->rWlanInfo.u4ScanResultNum ; j++) {
                    if(prAdapter->rWlanInfo.apucScanResultIEs[j] > pucIEPtr) {
                        prAdapter->rWlanInfo.apucScanResultIEs[j] =
                            (PUINT_8)((UINT_32)(prAdapter->rWlanInfo.apucScanResultIEs[j]) - u4IELength);
                    }
                }
            }
        }

        i++;
    }

    return;
}


#if CFG_TEST_WIFI_DIRECT_GO
VOID
wlanEnableP2pFunction (
    IN P_ADAPTER_T prAdapter
    )
{
#if 0
    P_MSG_P2P_FUNCTION_SWITCH_T prMsgFuncSwitch = (P_MSG_P2P_FUNCTION_SWITCH_T)NULL;

    prMsgFuncSwitch = (P_MSG_P2P_FUNCTION_SWITCH_T)cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_P2P_FUNCTION_SWITCH_T));
    if (!prMsgFuncSwitch) {
        ASSERT(FALSE);
        return;
    }


    prMsgFuncSwitch->rMsgHdr.eMsgId = MID_MNY_P2P_FUN_SWITCH;
    prMsgFuncSwitch->fgIsFuncOn = TRUE;


    mboxSendMsg(prAdapter,
                    MBOX_ID_0,
                    (P_MSG_HDR_T)prMsgFuncSwitch,
                    MSG_SEND_METHOD_BUF);
#endif
    return;
}

VOID
wlanEnableATGO (
    IN P_ADAPTER_T prAdapter
    )
{

    P_MSG_P2P_CONNECTION_REQUEST_T prMsgConnReq = (P_MSG_P2P_CONNECTION_REQUEST_T)NULL;
    UINT_8 aucTargetDeviceID[MAC_ADDR_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    prMsgConnReq = (P_MSG_P2P_CONNECTION_REQUEST_T)cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_P2P_CONNECTION_REQUEST_T));
    if (!prMsgConnReq) {
        ASSERT(FALSE);
        return;
    }

    prMsgConnReq->rMsgHdr.eMsgId = MID_MNY_P2P_CONNECTION_REQ;

    /*=====Param Modified for test=====*/
    COPY_MAC_ADDR(prMsgConnReq->aucDeviceID, aucTargetDeviceID);
    prMsgConnReq->fgIsTobeGO = TRUE;
    prMsgConnReq->fgIsPersistentGroup = FALSE;

    /*=====Param Modified for test=====*/

    mboxSendMsg(prAdapter,
                    MBOX_ID_0,
                    (P_MSG_HDR_T)prMsgConnReq,
                    MSG_SEND_METHOD_BUF);

    return;
}
#endif


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to retrieve permanent address from firmware
*
* @param prAdapter      Pointer of Adapter Data Structure
*
* @return WLAN_STATUS_SUCCESS
*         WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanQueryPermanentAddress(
    IN P_ADAPTER_T  prAdapter
    )
{
    UINT_8 ucCmdSeqNum;
    P_CMD_INFO_T prCmdInfo;
    P_WIFI_CMD_T prWifiCmd;
    UINT_32 u4RxPktLength;
    UINT_8 aucBuffer[sizeof(WIFI_EVENT_T) + sizeof(EVENT_BASIC_CONFIG)];
    P_HIF_RX_HEADER_T prHifRxHdr;
    P_WIFI_EVENT_T prEvent;
    P_EVENT_BASIC_CONFIG prEventBasicConfig;

    ASSERT(prAdapter);

    DEBUGFUNC("wlanQueryPermanentAddress");

    // 1. Allocate CMD Info Packet and its Buffer
    prCmdInfo = cmdBufAllocateCmdInfo(prAdapter, CMD_HDR_SIZE + sizeof(CMD_BASIC_CONFIG));
    if (!prCmdInfo) {
        DBGLOG(INIT, ERROR, ("Allocate CMD_INFO_T ==> FAILED.\n"));
        return WLAN_STATUS_FAILURE;
    }

    // increase command sequence number
    ucCmdSeqNum = nicIncreaseCmdSeqNum(prAdapter);

    // compose CMD_BUILD_CONNECTION cmd pkt
    prCmdInfo->eCmdType = COMMAND_TYPE_GENERAL_IOCTL;
    prCmdInfo->u2InfoBufLen = CMD_HDR_SIZE + sizeof(CMD_BASIC_CONFIG);
    prCmdInfo->pfCmdDoneHandler = NULL;
    prCmdInfo->fgIsOid = FALSE;
    prCmdInfo->ucCID = CMD_ID_BASIC_CONFIG;
    prCmdInfo->fgSetQuery = FALSE;
    prCmdInfo->fgNeedResp = TRUE;
    prCmdInfo->fgDriverDomainMCR = FALSE;
    prCmdInfo->ucCmdSeqNum = ucCmdSeqNum;
    prCmdInfo->u4SetInfoLen = sizeof(CMD_BASIC_CONFIG);

    // Setup WIFI_CMD_T
    prWifiCmd = (P_WIFI_CMD_T)(prCmdInfo->pucInfoBuffer);
    prWifiCmd->u2TxByteCount_UserPriority = prCmdInfo->u2InfoBufLen;
    prWifiCmd->ucCID = prCmdInfo->ucCID;
    prWifiCmd->ucSetQuery = prCmdInfo->fgSetQuery;
    prWifiCmd->ucSeqNum = prCmdInfo->ucCmdSeqNum;

    wlanSendCommand(prAdapter, prCmdInfo);
    cmdBufFreeCmdInfo(prAdapter, prCmdInfo);

    if(nicRxWaitResponse(prAdapter,
                1,
                aucBuffer,
                sizeof(WIFI_EVENT_T) + sizeof(EVENT_BASIC_CONFIG),
                &u4RxPktLength) != WLAN_STATUS_SUCCESS) {
        return WLAN_STATUS_FAILURE;
    }

    // header checking ..
    prHifRxHdr = (P_HIF_RX_HEADER_T)aucBuffer;
    if(prHifRxHdr->u2PacketType != HIF_RX_PKT_TYPE_EVENT) {
        return WLAN_STATUS_FAILURE;
    }

    prEvent = (P_WIFI_EVENT_T)aucBuffer;
    if(prEvent->ucEID != EVENT_ID_BASIC_CONFIG) {
        return WLAN_STATUS_FAILURE;
    }

    prEventBasicConfig = (P_EVENT_BASIC_CONFIG)(prEvent->aucBuffer);

    COPY_MAC_ADDR(prAdapter->rWifiVar.aucPermanentAddress, &(prEventBasicConfig->rMyMacAddr));
    COPY_MAC_ADDR(prAdapter->rWifiVar.aucMacAddress, &(prEventBasicConfig->rMyMacAddr));

    return WLAN_STATUS_SUCCESS;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to retrieve NIC capability from firmware
*
* @param prAdapter      Pointer of Adapter Data Structure
*
* @return WLAN_STATUS_SUCCESS
*         WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanQueryNicCapability(
    IN P_ADAPTER_T prAdapter
    )
{
    UINT_8 ucCmdSeqNum;
    P_CMD_INFO_T prCmdInfo;
    P_WIFI_CMD_T prWifiCmd;
    UINT_32 u4RxPktLength;
    UINT_8 aucBuffer[sizeof(WIFI_EVENT_T) + sizeof(EVENT_NIC_CAPABILITY)];
    P_HIF_RX_HEADER_T prHifRxHdr;
    P_WIFI_EVENT_T prEvent;
    P_EVENT_NIC_CAPABILITY prEventNicCapability;

    ASSERT(prAdapter);

    DEBUGFUNC("wlanQueryNicCapability");

    // 1. Allocate CMD Info Packet and its Buffer
    prCmdInfo = cmdBufAllocateCmdInfo(prAdapter, CMD_HDR_SIZE + sizeof(EVENT_NIC_CAPABILITY));
    if (!prCmdInfo) {
        DBGLOG(INIT, ERROR, ("Allocate CMD_INFO_T ==> FAILED.\n"));
        return WLAN_STATUS_FAILURE;
    }

    // increase command sequence number
    ucCmdSeqNum = nicIncreaseCmdSeqNum(prAdapter);

    // compose CMD_BUILD_CONNECTION cmd pkt
    prCmdInfo->eCmdType = COMMAND_TYPE_GENERAL_IOCTL;
    prCmdInfo->u2InfoBufLen = CMD_HDR_SIZE + sizeof(EVENT_NIC_CAPABILITY);
    prCmdInfo->pfCmdDoneHandler = NULL;
    prCmdInfo->fgIsOid = FALSE;
    prCmdInfo->ucCID = CMD_ID_GET_NIC_CAPABILITY;
    prCmdInfo->fgSetQuery = FALSE;
    prCmdInfo->fgNeedResp = TRUE;
    prCmdInfo->fgDriverDomainMCR = FALSE;
    prCmdInfo->ucCmdSeqNum = ucCmdSeqNum;
    prCmdInfo->u4SetInfoLen = 0;

    // Setup WIFI_CMD_T
    prWifiCmd = (P_WIFI_CMD_T)(prCmdInfo->pucInfoBuffer);
    prWifiCmd->u2TxByteCount_UserPriority = prCmdInfo->u2InfoBufLen;
    prWifiCmd->ucCID = prCmdInfo->ucCID;
    prWifiCmd->ucSetQuery = prCmdInfo->fgSetQuery;
    prWifiCmd->ucSeqNum = prCmdInfo->ucCmdSeqNum;

    wlanSendCommand(prAdapter, prCmdInfo);
    cmdBufFreeCmdInfo(prAdapter, prCmdInfo);

    if(nicRxWaitResponse(prAdapter,
                1,
                aucBuffer,
                sizeof(WIFI_EVENT_T) + sizeof(EVENT_NIC_CAPABILITY),
                &u4RxPktLength) != WLAN_STATUS_SUCCESS) {
        return WLAN_STATUS_FAILURE;
    }

    // header checking ..
    prHifRxHdr = (P_HIF_RX_HEADER_T)aucBuffer;
    if(prHifRxHdr->u2PacketType != HIF_RX_PKT_TYPE_EVENT) {
        return WLAN_STATUS_FAILURE;
    }

    prEvent = (P_WIFI_EVENT_T)aucBuffer;
    if(prEvent->ucEID != EVENT_ID_NIC_CAPABILITY) {
        return WLAN_STATUS_FAILURE;
    }

    prEventNicCapability = (P_EVENT_NIC_CAPABILITY)(prEvent->aucBuffer);

    prAdapter->rVerInfo.u2FwProductID     = prEventNicCapability->u2ProductID;
    prAdapter->rVerInfo.u2FwOwnVersion    = prEventNicCapability->u2FwVersion;
    prAdapter->rVerInfo.u2FwPeerVersion   = prEventNicCapability->u2DriverVersion;
    prAdapter->fgIsHw5GBandDisabled       = (BOOLEAN)prEventNicCapability->ucHw5GBandDisabled;
    prAdapter->fgIsEepromUsed             = (BOOLEAN)prEventNicCapability->ucEepromUsed;
    prAdapter->fgIsEfuseValid             = (BOOLEAN)prEventNicCapability->ucEfuseValid;
    prAdapter->fgIsEmbbededMacAddrValid   = (BOOLEAN)prEventNicCapability->ucMacAddrValid;

    return WLAN_STATUS_SUCCESS;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to load manufacture data from NVRAM
* if available and valid
*
* @param prAdapter      Pointer of Adapter Data Structure
* @param prRegInfo      Pointer of REG_INFO_T
*
* @return WLAN_STATUS_SUCCESS
*         WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanLoadManufactureData (
    IN P_ADAPTER_T prAdapter,
    IN P_REG_INFO_T prRegInfo
    )
{
#if CFG_SUPPORT_RDD_TEST_MODE
    CMD_RDD_CH_T rRddParam;
#endif

    ASSERT(prAdapter);

    /* 1. Version Check */
    kalGetConfigurationVersion(prAdapter->prGlueInfo,
            &(prAdapter->rVerInfo.u2Part1CfgOwnVersion),
            &(prAdapter->rVerInfo.u2Part1CfgPeerVersion),
            &(prAdapter->rVerInfo.u2Part2CfgOwnVersion),
            &(prAdapter->rVerInfo.u2Part2CfgPeerVersion));

#if (CFG_SW_NVRAM_VERSION_CHECK == 1)
    if(CFG_DRV_OWN_VERSION < prAdapter->rVerInfo.u2Part1CfgPeerVersion
            || CFG_DRV_OWN_VERSION < prAdapter->rVerInfo.u2Part2CfgPeerVersion
            || prAdapter->rVerInfo.u2Part1CfgOwnVersion < CFG_DRV_PEER_VERSION
            || prAdapter->rVerInfo.u2Part2CfgOwnVersion < CFG_DRV_PEER_VERSION) {
        return WLAN_STATUS_FAILURE;
    }
#endif

    // MT6620 E1/E2 would be ignored directly
    if(prAdapter->rVerInfo.u2Part1CfgOwnVersion == 0x0001) {
        prRegInfo->ucTxPwrValid = 1;
    }
    else {
        /* 2. Load TX power gain parameters if valid */
        if(prRegInfo->ucTxPwrValid != 0) {
            // send to F/W
            nicUpdateTxPower(prAdapter, (P_CMD_TX_PWR_T)(&(prRegInfo->rTxPwr)));
        }
    }

    /* 3. Check if needs to support 5GHz */
    if(prRegInfo->ucEnable5GBand) {
        // check if it is disabled by hardware
        if(prAdapter->fgIsHw5GBandDisabled
                || prRegInfo->ucSupport5GBand == 0) {
            prAdapter->fgEnable5GBand = FALSE;
        }
        else {
            prAdapter->fgEnable5GBand = TRUE;
        }
    }
    else {
        prAdapter->fgEnable5GBand = FALSE;
    }

    /* 4. Send EFUSE data */
    wlanSendSetQueryCmd(prAdapter,
            CMD_ID_SET_PHY_PARAM,
            TRUE,
            FALSE,
            FALSE,
            NULL,
            NULL,
            sizeof(CMD_PHY_PARAM_T),
            (PUINT_8)(prRegInfo->aucEFUSE),
            NULL,
            0);

#if CFG_SUPPORT_RDD_TEST_MODE
    rRddParam.ucRddTestMode = (UINT_8) prRegInfo->u4RddTestMode;
    rRddParam.ucRddShutCh = (UINT_8) prRegInfo->u4RddShutFreq;
    rRddParam.ucRddStartCh = (UINT_8) nicFreq2ChannelNum(prRegInfo->u4RddStartFreq);
    rRddParam.ucRddStopCh = (UINT_8) nicFreq2ChannelNum(prRegInfo->u4RddStopFreq);
    rRddParam.ucRddDfs = (UINT_8) prRegInfo->u4RddDfs;
    prAdapter->ucRddStatus = 0;
    nicUpdateRddTestMode(prAdapter, (P_CMD_RDD_CH_T)(&rRddParam));
#endif

    /* 5. Get 16-bits Country Code and Bandwidth */
    prAdapter->rWifiVar.rConnSettings.u2CountryCode =
        (((UINT_16) prRegInfo->au2CountryCode[0]) << 8) |
        (((UINT_16) prRegInfo->au2CountryCode[1]) & BITS(0,7));

#if 0 /* Bandwidth control will be controlled by GUI. 20110930
       * So ignore the setting from registry/NVRAM
       */
    prAdapter->rWifiVar.rConnSettings.uc2G4BandwidthMode =
            prRegInfo->uc2G4BwFixed20M ? CONFIG_BW_20M : CONFIG_BW_20_40M;
    prAdapter->rWifiVar.rConnSettings.uc5GBandwidthMode =
            prRegInfo->uc5GBwFixed20M ? CONFIG_BW_20M : CONFIG_BW_20_40M;
#endif

    /* 6. Set domain and channel information to chip */
    rlmDomainSendCmd(prAdapter, FALSE);

    /* 7. set band edge tx power if available */
    if(prRegInfo->fg2G4BandEdgePwrUsed) {
        CMD_EDGE_TXPWR_LIMIT_T rCmdEdgeTxPwrLimit;

        rCmdEdgeTxPwrLimit.cBandEdgeMaxPwrCCK
            = prRegInfo->cBandEdgeMaxPwrCCK;
        rCmdEdgeTxPwrLimit.cBandEdgeMaxPwrOFDM20
            = prRegInfo->cBandEdgeMaxPwrOFDM20;
        rCmdEdgeTxPwrLimit.cBandEdgeMaxPwrOFDM40
            = prRegInfo->cBandEdgeMaxPwrOFDM40;

        wlanSendSetQueryCmd(prAdapter,
                CMD_ID_SET_EDGE_TXPWR_LIMIT,
                TRUE,
                FALSE,
                FALSE,
                NULL,
                NULL,
                sizeof(CMD_EDGE_TXPWR_LIMIT_T),
                (PUINT_8)&rCmdEdgeTxPwrLimit,
                NULL,
                0);
    }

    return WLAN_STATUS_SUCCESS;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to check
*        Media Stream Mode is set to non-default value or not,
*        and clear to default value if above criteria is met
*
* @param prAdapter      Pointer of Adapter Data Structure
*
* @return TRUE
*           The media stream mode was non-default value and has been reset
*         FALSE
*           The media stream mode is default value
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
wlanResetMediaStreamMode(
    IN P_ADAPTER_T prAdapter
    )
{
    ASSERT(prAdapter);

    if(prAdapter->rWlanInfo.eLinkAttr.ucMediaStreamMode != 0) {
        prAdapter->rWlanInfo.eLinkAttr.ucMediaStreamMode = 0;

        return TRUE;
    }
    else {
        return FALSE;
    }
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to check if any pending timer has expired
*
* @param prAdapter      Pointer of Adapter Data Structure
*
* @return WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanTimerTimeoutCheck(
    IN P_ADAPTER_T prAdapter
    )
{
    ASSERT(prAdapter);

    cnmTimerDoTimeOutCheck(prAdapter);

    return WLAN_STATUS_SUCCESS;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to check if any pending mailbox message
*        to be handled
*
* @param prAdapter      Pointer of Adapter Data Structure
*
* @return WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanProcessMboxMessage(
    IN P_ADAPTER_T prAdapter
    )
{
    UINT_32 i;

    ASSERT(prAdapter);

    for(i = 0 ; i < MBOX_ID_TOTAL_NUM ; i++) {
       mboxRcvAllMsg(prAdapter , (ENUM_MBOX_ID_T)i);
    }

    return WLAN_STATUS_SUCCESS;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to enqueue a single TX packet into CORE
*
* @param prAdapter      Pointer of Adapter Data Structure
*        prNativePacket Pointer of Native Packet
*
* @return WLAN_STATUS_SUCCESS
*         WLAN_STATUS_RESOURCES
*         WLAN_STATUS_INVALID_PACKET
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanEnqueueTxPacket (
    IN P_ADAPTER_T      prAdapter,
    IN P_NATIVE_PACKET  prNativePacket
    )
{
    P_TX_CTRL_T prTxCtrl;
    P_MSDU_INFO_T prMsduInfo;

    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);

    prTxCtrl = &prAdapter->rTxCtrl;

    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_MSDU_INFO_LIST);
    QUEUE_REMOVE_HEAD(&prTxCtrl->rFreeMsduInfoList, prMsduInfo, P_MSDU_INFO_T);
    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_MSDU_INFO_LIST);

    if(prMsduInfo == NULL) {
        return WLAN_STATUS_RESOURCES;
    }
    else {
        prMsduInfo->eSrc = TX_PACKET_OS;

        if(nicTxFillMsduInfo(prAdapter,
                    prMsduInfo,
                    prNativePacket) == FALSE) { // packet is not extractable
            kalSendComplete(prAdapter->prGlueInfo,
                    prNativePacket,
                    WLAN_STATUS_INVALID_PACKET);

            nicTxReturnMsduInfo(prAdapter, prMsduInfo);

            return WLAN_STATUS_INVALID_PACKET;
        }
        else {
            // enqueue to QM
            nicTxEnqueueMsdu(prAdapter, prMsduInfo);

            return WLAN_STATUS_SUCCESS;
        }
    }
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to flush pending TX packets in CORE
*
* @param prAdapter      Pointer of Adapter Data Structure
*
* @return WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanFlushTxPendingPackets(
    IN P_ADAPTER_T prAdapter
    )
{
    ASSERT(prAdapter);

    return nicTxFlush(prAdapter);
}


/*----------------------------------------------------------------------------*/
/*!
* \brief this function sends pending MSDU_INFO_T to MT6620
*
* @param prAdapter      Pointer to the Adapter structure.
* @param pfgHwAccess    Pointer for tracking LP-OWN status
*
* @retval WLAN_STATUS_SUCCESS   Reset is done successfully.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanTxPendingPackets (
    IN      P_ADAPTER_T prAdapter,
    IN OUT  PBOOLEAN    pfgHwAccess
    )
{
    P_TX_CTRL_T prTxCtrl;
    P_MSDU_INFO_T prMsduInfo;

    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);
    prTxCtrl = &prAdapter->rTxCtrl;

    ASSERT(pfgHwAccess);

    // <1> dequeue packet by txDequeuTxPackets()
    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_QM_TX_QUEUE);
    prMsduInfo = qmDequeueTxPackets(prAdapter, &prTxCtrl->rTc);
    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_QM_TX_QUEUE);

    if(prMsduInfo != NULL) {
        if(kalIsCardRemoved(prAdapter->prGlueInfo) == FALSE) {
            /* <2> Acquire LP-OWN if necessary */
            if(*pfgHwAccess == FALSE) {
                *pfgHwAccess = TRUE;

                wlanAcquirePowerControl(prAdapter);
            }

#if (MT6620_E1_ASIC_HIFSYS_WORKAROUND == 1)
            if(prAdapter->fgIsClockGatingEnabled == TRUE) {
                nicDisableClockGating(prAdapter);
            }
#endif
            // <3> send packets
            nicTxMsduInfoList(prAdapter, prMsduInfo);

            // <4> update TC by txAdjustTcQuotas()
            nicTxAdjustTcq(prAdapter);
        }
        else {
            wlanProcessQueuedMsduInfo(prAdapter, prMsduInfo);
        }
    }

#if (MT6620_E1_ASIC_HIFSYS_WORKAROUND == 1)
    if(prAdapter->fgIsClockGatingEnabled == FALSE) {
        nicEnableClockGating(prAdapter);
    }
#endif

    return WLAN_STATUS_SUCCESS;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to acquire power control from firmware
*
* @param prAdapter      Pointer of Adapter Data Structure
*
* @return WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanAcquirePowerControl(
    IN P_ADAPTER_T prAdapter
    )
{
    ASSERT(prAdapter);

    ACQUIRE_POWER_CONTROL_FROM_PM(prAdapter);

    /* Reset sleepy state */
    if(prAdapter->fgWiFiInSleepyState == TRUE) {
        prAdapter->fgWiFiInSleepyState = FALSE;
    }

    return WLAN_STATUS_SUCCESS;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to release power control to firmware
*
* @param prAdapter      Pointer of Adapter Data Structure
*
* @return WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanReleasePowerControl(
    IN P_ADAPTER_T prAdapter
    )
{
    ASSERT(prAdapter);

    RECLAIM_POWER_CONTROL_TO_PM(prAdapter, FALSE);

    return WLAN_STATUS_SUCCESS;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to report currently pending TX frames count
*        (command packets are not included)
*
* @param prAdapter      Pointer of Adapter Data Structure
*
* @return number of pending TX frames
*/
/*----------------------------------------------------------------------------*/
UINT_32
wlanGetTxPendingFrameCount (
    IN P_ADAPTER_T prAdapter
    )
{
    P_TX_CTRL_T prTxCtrl;
    UINT_32 u4Num;

    ASSERT(prAdapter);
    prTxCtrl = &prAdapter->rTxCtrl;

    u4Num = kalGetTxPendingFrameCount(prAdapter->prGlueInfo) + (UINT_32)(prTxCtrl->i4PendingFwdFrameCount);

    return u4Num;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is to report current ACPI state
*
* @param prAdapter      Pointer of Adapter Data Structure
*
* @return ACPI_STATE_D0 Normal Operation Mode
*         ACPI_STATE_D3 Suspend Mode
*/
/*----------------------------------------------------------------------------*/
ENUM_ACPI_STATE_T
wlanGetAcpiState (
    IN P_ADAPTER_T prAdapter
    )
{
    ASSERT(prAdapter);

    return prAdapter->rAcpiState;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is to update current ACPI state only
*
* @param prAdapter      Pointer of Adapter Data Structure
* @param ePowerState    ACPI_STATE_D0 Normal Operation Mode
*                       ACPI_STATE_D3 Suspend Mode
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID
wlanSetAcpiState (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_ACPI_STATE_T ePowerState
    )
{
    ASSERT(prAdapter);
    ASSERT(ePowerState <= ACPI_STATE_D3);

    prAdapter->rAcpiState = ePowerState;

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is to query ECO version from HIFSYS CR
*
* @param prAdapter      Pointer of Adapter Data Structure
*
* @return zero      Unable to retrieve ECO version information
*         non-zero  ECO version (1-based)
*/
/*----------------------------------------------------------------------------*/
UINT_8
wlanGetEcoVersion(
    IN P_ADAPTER_T prAdapter
    )
{
    ASSERT(prAdapter);

    if(nicVerifyChipID(prAdapter) == TRUE) {
        return (prAdapter->ucRevID + 1);
    }
    else {
        return 0;
    }
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is to setting the default Tx Power configuration
*
* @param prAdapter      Pointer of Adapter Data Structure
*
* @return zero      Unable to retrieve ECO version information
*         non-zero  ECO version (1-based)
*/
/*----------------------------------------------------------------------------*/
VOID
wlanDefTxPowerCfg (
    IN P_ADAPTER_T      prAdapter
    )
{
    UINT_8 i;
    P_GLUE_INFO_T       prGlueInfo = prAdapter->prGlueInfo;
    P_SET_TXPWR_CTRL_T  prTxpwr;

    ASSERT(prGlueInfo);

    prTxpwr = &prGlueInfo->rTxPwr;

    prTxpwr->c2GLegacyStaPwrOffset = 0;
    prTxpwr->c2GHotspotPwrOffset = 0;
    prTxpwr->c2GP2pPwrOffset = 0;
    prTxpwr->c2GBowPwrOffset = 0;
    prTxpwr->c5GLegacyStaPwrOffset = 0;
    prTxpwr->c5GHotspotPwrOffset = 0;
    prTxpwr->c5GP2pPwrOffset = 0;
    prTxpwr->c5GBowPwrOffset = 0;
    prTxpwr->ucConcurrencePolicy = 0;
    for (i=0; i<3;i++)
        prTxpwr->acReserved1[i] = 0;

    for (i=0; i<14;i++)
        prTxpwr->acTxPwrLimit2G[i] = 63;

    for (i=0; i<4;i++)
        prTxpwr->acTxPwrLimit5G[i] = 63;

    for (i=0; i<2;i++)
        prTxpwr->acReserved2[i] = 0;

}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is to
*        set preferred band configuration corresponding to network type
*
* @param prAdapter      Pointer of Adapter Data Structure
* @param eBand          Given band
* @param eNetTypeIndex  Given Network Type
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID
wlanSetPreferBandByNetwork (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_BAND_T eBand,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex
    )
{
    ASSERT(prAdapter);
    ASSERT(eBand <= BAND_NUM);
    ASSERT(eNetTypeIndex <= NETWORK_TYPE_INDEX_NUM);

    /* 1. set prefer band according to network type */
    prAdapter->aePreferBand[eNetTypeIndex] = eBand;

    /* 2. remove buffered BSS descriptors correspondingly */
    if(eBand == BAND_2G4) {
        scanRemoveBssDescByBandAndNetwork(prAdapter, BAND_5G, eNetTypeIndex);
    }
    else if(eBand == BAND_5G) {
        scanRemoveBssDescByBandAndNetwork(prAdapter, BAND_2G4, eNetTypeIndex);
    }

    return;
}

