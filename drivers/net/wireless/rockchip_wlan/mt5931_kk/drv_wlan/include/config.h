/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/include/config.h#2 $
*/

/*! \file   "config.h"
    \brief  This file includes the various configurable parameters for customers

    This file ncludes the configurable parameters except the paramters indicate the turning-on/off of some features
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
** $Log: config.h $
 *
 * 07 13 2012 cp.wu
 * [WCXRP00001259] [MT6620 Wi-Fi][Driver][Firmware] Send a signal to firmware for termination after SDIO error has happened
 * [driver domain] add force reset by host-to-device interrupt mechanism
 *
 * 06 13 2012 yuche.tsai
 * NULL
 * Update maintrunk driver.
 * Add support for driver compose assoc request frame.
 *
 * 06 05 2012 tsaiyuan.hsu
 * [WCXRP00001249] [ALPS.ICS] Daily build warning on "wlan/mgmt/swcr.c#1"
 * resolve build waring for "WNM_UNIT_TEST not defined"..
 *
 * 06 04 2012 cp.wu
 * [WCXRP00001245] [MT6620 Wi-Fi][Driver][Firmware] NPS Software Development
 * discussed with WH, privacy bit in associate response is not necessary to be checked, and identified as association failure when mismatching with beacon/probe response
 *
 * 05 11 2012 cp.wu
 * [WCXRP00001237] [MT6620 Wi-Fi][Driver] Show MAC address and MAC address source for ACS's convenience
 * show MAC address & source while initiliazation
 *
 * 04 20 2012 cp.wu
 * [WCXRP00000913] [MT6620 Wi-Fi] create repository of source code dedicated for MT6620 E6 ASIC
 * correct macro
 *
 * 04 12 2012 terry.wu
 * NULL
 * Add AEE message support
 * 1) Show AEE warning(red screen) if SDIO access error occurs
 *
 * 03 29 2012 eason.tsai
 * [WCXRP00001216] [MT6628 Wi-Fi][Driver]add conditional define
 * add conditional define.
 *
 * 03 02 2012 terry.wu
 * NULL
 * Enable CFG80211 Support.
 *
 * 01 05 2012 tsaiyuan.hsu
 * [WCXRP00001157] [MT6620 Wi-Fi][FW][DRV] add timing measurement support for 802.11v
 * add timing measurement support for 802.11v.
 *
 * 11 23 2011 cp.wu
 * [WCXRP00001123] [MT6620 Wi-Fi][Driver] Add option to disable beacon content change detection
 * add compile option to disable beacon content change detection.
 *
 * 11 18 2011 yuche.tsai
 * NULL
 * CONFIG P2P support RSSI query, default turned off.
 *
 * 10 28 2011 cp.wu
 * [MT6620 Wi-Fi][Win32 Driver] Enable 5GHz support as default
 * enable 5GHz as default for DaVinci trunk and V2.1 driver release .
 *
 * 10 18 2011 cp.wu
 * [WCXRP00001022] [MT6628 Driver][Firmware Download] Add multi section independent download functionality
 * surpress compiler warning for MT6628 build
 *
 * 10 12 2011 wh.su
 * [WCXRP00001036] [MT6620 Wi-Fi][Driver][FW] Adding the 802.11w code for MFP
 * adding the 802.11w related function and define .
 *
 * 10 03 2011 cp.wu
 * [WCXRP00001022] [MT6628 Driver][Firmware Download] Add multi section independent download functionality
 * enable divided firmware downloading.
 *
 * 10 03 2011 cp.wu
 * [WCXRP00001022] [MT6628 Driver][Firmware Download] Add multi section independent download functionality
 * add firmware download path in divided scatters.
 *
 * 10 03 2011 cp.wu
 * [MT6628 Driver][Firmware Download] Add multi section independent download functionality
 * add firmware downloading aggregated path.
 *
 * 09 28 2011 tsaiyuan.hsu
 * [WCXRP00000900] [MT5931 Wi-Fi] Improve balance of TX and RX
 * enlarge window size only by 4.
 *
 * 08 15 2011 cp.wu
 * [WCXRP00000851] [MT6628 Wi-Fi][Driver] Add HIFSYS related definition to driver source tree
 * reuse firmware download logic of MT6620 for MT6628.
 *
 * 08 15 2011 cp.wu
 * [WCXRP00000851] [MT6628 Wi-Fi][Driver] Add HIFSYS related definition to driver source tree
 * add MT6628-specific definitions.
 *
 * 08 15 2011 cp.wu
 * [WCXRP00000913] [MT6620 Wi-Fi] create repository of source code dedicated for MT6620 E6 ASIC
 * support to load different firmware image for E3/E4/E5 and E6 ASIC on win32 platforms.
 *
 * 08 12 2011 cp.wu
 * [WCXRP00000913] [MT6620 Wi-Fi] create repository of source code dedicated for MT6620 E6 ASIC
 * load WIFI_RAM_CODE_E6 for MT6620 E6 ASIC.
 *
 * 08 09 2011 chinghwa.yu
 * [WCXRP00000063] Update BCM CoEx design and settings
 * Add BWCS definition for MT6620.
 *
 * 07 28 2011 chinghwa.yu
 * [WCXRP00000063] Update BCM CoEx design and settings
 * Add BWCS cmd and event.
 *
 * 07 22 2011 jeffrey.chang
 * [WCXRP00000864] [MT5931] Add command to adjust OSC stable time
 * modify driver to set OSC stable time after f/w download
 *
 * 07 18 2011 chinghwa.yu
 * [WCXRP00000063] Update BCM CoEx design and settings[WCXRP00000612] [MT6620 Wi-Fi] [FW] CSD update SWRDD algorithm
 * Add CMD/Event for RDD and BWCS.
 *
 * 07 05 2011 yuche.tsai
 * [WCXRP00000821] [Volunteer Patch][WiFi Direct][Driver] WiFi Direct Connection Speed Issue
 * Refine compile flag.
 *
 * 07 05 2011 yuche.tsai
 * [WCXRP00000821] [Volunteer Patch][WiFi Direct][Driver] WiFi Direct Connection Speed Issue
 * Add wifi direct connection enhancement method I, II & VI.
 *
 * 06 24 2011 cp.wu
 * [WCXRP00000702] [MT5931][Driver] Modify initialization sequence for E1 ASIC[WCXRP00000812] [MT6620 Wi-Fi][Driver] not show NVRAM when there is no valid MAC address in NVRAM content
 * increase RX buffer number to have a 2:1 ping-pong ratio
 *
 * 06 23 2011 eddie.chen
 * [WCXRP00000810] [MT5931][DRV/FW] Adjust TxRx Buffer number and Rx buffer size
 * 1. Different TX RX buffer
 * 2. Enlarge RX buffer and increase the number 8->11
 * 3. Seperate the WINSZIE and RX buffer number
 * 4. Fix RX maximum size in MAC
 *
 * 06 20 2011 terry.wu
 * NULL
 * Add BoW Rate Limitation.
 *
 * 06 17 2011 terry.wu
 * NULL
 * .
 *
 * 06 17 2011 terry.wu
 * NULL
 * Add BoW 11N support.
 *
 * 06 07 2011 yuche.tsai
 * [WCXRP00000763] [Volunteer Patch][MT6620][Driver] RX Service Discovery Frame under AP mode Issue
 * Add compile flag for persistent group support.
 *
 * 06 01 2011 cm.chang
 * [WCXRP00000756] [MT6620 Wi-Fi][Driver] 1. AIS follow channel of BOW 2. Provide legal channel function
 * Limit AIS to fixed channel same with BOW
 *
 * 04 18 2011 terry.wu
 * [WCXRP00000660] [MT6620 Wi-Fi][Driver] Remove flag CFG_WIFI_DIRECT_MOVED
 * Remove flag CFG_WIFI_DIRECT_MOVED.
 *
 * 04 14 2011 cm.chang
 * [WCXRP00000634] [MT6620 Wi-Fi][Driver][FW] 2nd BSS will not support 40MHz bandwidth for concurrency
 * Enable RX STBC capability
 *
 * 04 11 2011 george.huang
 * [WCXRP00000628] [MT6620 Wi-Fi][FW][Driver] Modify U-APSD setting to default OFF
 * .
 *
 * 04 08 2011 pat.lu
 * [WCXRP00000623] [MT6620 Wi-Fi][Driver] use ARCH define to distinguish PC Linux driver
 * Use CONFIG_X86 instead of PC_LINUX_DRIVER_USE option to have proper compile settting for PC Linux driver
 *
 * 04 08 2011 cp.wu
 * [WCXRP00000540] [MT5931][Driver] Add eHPI8/eHPI16 support to Linux Glue Layer
 * 1. correction: RX aggregation is not limited to SDIO but for all host interface options
 * 2. add forward declarations for DBG-only symbols
 *
 * 04 06 2011 cp.wu
 * [WCXRP00000540] [MT5931][Driver] Add eHPI8/eHPI16 support to Linux Glue Layer
 * 1. do not check for pvData inside wlanNetCreate() due to it is NULL for eHPI  port
 * 2. update perm_addr as well for MAC address
 * 3. not calling check_mem_region() anymore for eHPI
 * 4. correct MSC_CS macro for 0-based notation
 *
 * 04 01 2011 cp.wu
 * [WCXRP00000540] [MT5931][Driver] Add eHPI8/eHPI16 support to Linux Glue Layer
 * 1. simplify config.h due to aggregation options could be also applied for eHPI/SPI interface
 * 2. use spin-lock instead of semaphore for protecting eHPI access because of possible access from ISR
 * 3. request_irq() API has some changes between linux kernel 2.6.12 and 2.6.26
 *
 * 03 29 2011 cp.wu
 * [WCXRP00000598] [MT6620 Wi-Fi][Driver] Implementation of interface for communicating with user space process for RESET_START and RESET_END events
 * implement kernel-to-userspace communication via generic netlink socket for whole-chip resetting mechanism
 *
 * 03 22 2011 pat.lu
 * [WCXRP00000592] [MT6620 Wi-Fi][Driver] Support PC Linux Environment Driver Build
 * Add a compiler option "PC_LINUX_DRIVER_USE" for building driver in PC Linux environment.
 *
 * 03 18 2011 wh.su
 * [WCXRP00000530] [MT6620 Wi-Fi] [Driver] skip doing p2pRunEventAAAComplete after send assoc response Tx Done
 * enable the Anti_piracy check at driver .
 *
 * 03 17 2011 tsaiyuan.hsu
 * [WCXRP00000517] [MT6620 Wi-Fi][Driver][FW] Fine Tune Performance of Roaming
 * enable roaming feature.
 *
 * 03 17 2011 chinglan.wang
 * [WCXRP00000570] [MT6620 Wi-Fi][Driver] Add Wi-Fi Protected Setup v2.0 feature
 * .
 *
 * 03 15 2011 cp.wu
 * [WCXRP00000559] [MT6620 Wi-Fi][Driver] Combine TX/RX DMA buffers into a single one to reduce physically continuous memory consumption
 * 1. deprecate CFG_HANDLE_IST_IN_SDIO_CALLBACK
 * 2. Use common coalescing buffer for both TX/RX directions
 *
 *
 * 03 15 2011 eddie.chen
 * [WCXRP00000554] [MT6620 Wi-Fi][DRV] Add sw control debug counter
 * Add sw debug counter for QM.
 *
 * 03 07 2011 wh.su
 * [WCXRP00000506] [MT6620 Wi-Fi][Driver][FW] Add Security check related code
 * rename the define to anti_pviracy.
 *
 * 03 06 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Sync BOW Driver to latest person development branch version..
 *
 * 03 02 2011 wh.su
 * [WCXRP00000506] [MT6620 Wi-Fi][Driver][FW] Add Security check related code
 * Add security check code.
 *
 * 03 01 2011 george.huang
 * [WCXRP00000495] [MT6620 Wi-Fi][FW] Support pattern filter for unwanted ARP frames
 * Fix compile issue
 *
 * 02 25 2011 george.huang
 * [WCXRP00000497] [MT6620 Wi-Fi][FW] Change default UAPSD AC assignment
 * Assign all AC default to be U-APSD enabled.
 *
 * 02 14 2011 wh.su
 * [WCXRP00000432] [MT6620 Wi-Fi][Driver] Add STA privacy check at hotspot mode
 * Let the privacy check at hotspot mode default enable.
 *
 * 02 09 2011 wh.su
 * [WCXRP00000432] [MT6620 Wi-Fi][Driver] Add STA privacy check at hotspot mode
 * adding the code for check STA privacy bit at AP mode, .
 *
 * 02 08 2011 cp.wu
 * [WCXRP00000427] [MT6620 Wi-Fi][Driver] Modify veresion information to match with release revision number
 * change version number to v1.2.0.0 for preparing v1.2 software package release.
 *
 * 02 01 2011 yarco.yang
 * [WCXRP00000417] [MT6620 Driver] Chnage CFG_HANDLE_IST_IN_SDIO_CALLBACK from 1 to 0 for Interoperability
 * .
 *
 * 01 27 2011 tsaiyuan.hsu
 * [WCXRP00000392] [MT6620 Wi-Fi][Driver] Add Roaming Support
 * add roaming fsm
 * 1. not support 11r, only use strength of signal to determine roaming.
 * 2. not enable CFG_SUPPORT_ROAMING until completion of full test.
 * 3. in 6620, adopt work-around to avoid sign extension problem of cck of hw
 * 4. assume that change of link quality in smooth way.
 *
 * 01 19 2011 wh.su
 * [WCXRP00000370] [MT6620 Wi-Fi][Driver] Disable Rx RDG for workaround pre-N ccmp issue
 * Not announce support Rx RDG for wokaround pre-N ccmp construct AAD issue..
 *
 * 01 15 2011 puff.wen
 * NULL
 * Add Stress test
 *
 * 01 12 2011 cp.wu
 * [WCXRP00000356] [MT6620 Wi-Fi][Driver] fill mac header length for security frames 'cause hardware header translation needs such information
 * fill mac header length information for 802.1x frames.
 *
 * 01 11 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Support BOW only for Linux.
 *
 * 01 10 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Enable BOW and 4 physical links.
 *
 * 01 08 2011 yuche.tsai
 * [WCXRP00000345] [MT6620][Volunteer Patch] P2P may issue a SSID specified scan request, but the SSID length is still invalid.
 * Modify CFG_SLT_SUPPORT default value.
 *
 * 01 08 2011 yuche.tsai
 * [WCXRP00000341] [MT6620][SLT] Create Branch for SLT SW.
 * Update configure flag.
 *
 * 12 28 2010 cp.wu
 * [WCXRP00000269] [MT6620 Wi-Fi][Driver][Firmware] Prepare for v1.1 branch release
 * report EEPROM used flag via NIC_CAPABILITY
 *
 * 12 28 2010 cp.wu
 * [WCXRP00000269] [MT6620 Wi-Fi][Driver][Firmware] Prepare for v1.1 branch release
 * integrate with 'EEPROM used' flag for reporting correct capability to Engineer Mode/META and other tools
 *
 * 12 15 2010 yuche.tsai
 * NULL
 * Update SLT Descriptor number configure in driver.
 *
 * 12 13 2010 chinglan.wang
 * NULL
 * Add WPS 1.0 feature flag to enable the WPS 1.0 function.
 *
 * 11 23 2010 george.huang
 * [WCXRP00000127] [MT6620 Wi-Fi][Driver] Add a registry to disable Beacon Timeout function for SQA test by using E1 EVB
 * Enable PM function by default
 *
 * 11 15 2010 wh.su
 * [WCXRP00000171] [MT6620 Wi-Fi][Driver] Add message check code same behavior as mt5921
 * use config.mk WAPI config define.
 *
 * 11 08 2010 wh.su
 * [WCXRP00000171] [MT6620 Wi-Fi][Driver] Add message check code same behavior as mt5921
 * use the config.mk define.
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
 * 10 26 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check[WCXRP00000137] [MT6620 Wi-Fi] [FW] Support NIC capability query command
 * 1) update NVRAM content template to ver 1.02
 * 2) add compile option for querying NIC capability (default: off)
 * 3) modify AIS 5GHz support to run-time option, which could be turned on by registry or NVRAM setting
 * 4) correct auto-rate compiler error under linux (treat warning as error)
 * 5) simplify usage of NVRAM and REG_INFO_T
 * 6) add version checking between driver and firmware
 *
 * 10 25 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check
 * add option for enable/disable TX PWR gain adjustment (default: off)
 *
 * 10 20 2010 wh.su
 * [WCXRP00000067] [MT6620 Wi-Fi][Driver] Support the android+ WAPI function
 * enable the WAPI compiling flag as default
 *
 * 10 19 2010 cp.wu
 * [WCXRP00000122] [MT6620 Wi-Fi][Driver] Preparation for YuSu source tree integration
 * remove HIF_SDIO_ONE flags because the settings could be merged for runtime detection instead of compile-time.
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
 * 10 14 2010 wh.su
 * [WCXRP00000102] [MT6620 Wi-Fi] [FW] Add a compiling flag and code for support Direct GO at Android
 * Add a define CFG_TEST_ANDROID_DIRECT_GO compiling flag
 *
 * 10 08 2010 cm.chang
 * NULL
 * Remove unused compiling flags (TX_RDG and TX_SGI)
 *
 * 10 07 2010 cp.wu
 * [WCXRP00000083] [MT5931][Driver][FW] Add necessary logic for MT5931 first connection
 * add firmware download for MT5931.
 *
 * 10 05 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check
 * load manufacture data when CFG_SUPPORT_NVRAM is set to 1
 *
 * 10 05 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check
 * 1) add NVRAM access API
 * 2) fake scanning result when NVRAM doesn't exist and/or version mismatch. (off by compiler option)
 * 3) add OID implementation for NVRAM read/write service
 *
 * 10 05 2010 yarco.yang
 * [WCXRP00000082] [MT6620 Wi-Fi][Driver]High throughput performance tuning
 * Change CFG_IST_LOOP_COUNT from 2 to 1 to reduce unnecessary SDIO bus access
 *
 * 09 24 2010 cp.wu
 * [WCXRP00000057] [MT6620 Wi-Fi][Driver] Modify online scan to a run-time switchable feature
 * Modify online scan as a run-time adjustable option (for Windows, in registry)
 *
 * 09 23 2010 cp.wu
 * [WCXRP00000052] [MT6620 Wi-Fi][Driver] Eliminate Linux Compile Warning
 * eliminate reference of CFG_RESPONSE_MAX_PKT_SIZE
 *
 * 09 20 2010 cm.chang
 * NULL
 * Disable RX STBC by BB HEC based on MT6620E1_PHY_BUG v05.docx
 *
 * 09 17 2010 chinglan.wang
 * NULL
 * Add performance test option
 *
 * 09 10 2010 chinglan.wang
 * NULL
 * Modify for Software Migration Phase 2.10 for E2 FPGA
 *
 * 09 07 2010 yuche.tsai
 * NULL
 * Add a CFG for max common IE buffer size.
 *
 * 09 01 2010 cp.wu
 * NULL
 * restore configuration as before.
 *
 * 09 01 2010 cp.wu
 * NULL
 * HIFSYS Clock Source Workaround
 *
 * 08 31 2010 kevin.huang
 * NULL
 * Use LINK LIST operation to process SCAN result
 *
 * 08 30 2010 chinglan.wang
 * NULL
 * Enable the MT6620_FPGA_BWCS value.
 *
 * 08 30 2010 chinglan.wang
 * NULL
 * Disable the FW encryption.
 *
 * 08 27 2010 chinglan.wang
 * NULL
 * Update configuration for MT6620_E1_PRE_ALPHA_1832_0827_2010
 *
 * 08 26 2010 yuche.tsai
 * NULL
 * Add AT GO test configure mode under WinXP.
 * Please enable 1. CFG_ENABLE_WIFI_DIRECT, 2. CFG_TEST_WIFI_DIRECT_GO, 3. CFG_SUPPORT_AAA
 *
 * 08 25 2010 cp.wu
 * NULL
 * add option for enabling AIS 5GHz scan
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
 * 08 24 2010 cm.chang
 * NULL
 * Support RLM initail channel of Ad-hoc, P2P and BOW
 *
 * 08 23 2010 cp.wu
 * NULL
 * revise constant definitions to be matched with implementation (original cmd-event definition is deprecated)
 *
 * 08 23 2010 chinghwa.yu
 * NULL
 * Disable BOW Test.
 *
 * 08 23 2010 jeffrey.chang
 * NULL
 * fix config.h typo
 *
 * 08 23 2010 chinghwa.yu
 * NULL
 * Update for BOW.
 *
 * 08 21 2010 jeffrey.chang
 * NULL
 * 1) add sdio two setting
 * 2) bug fix of sdio glue
 *
 * 08 09 2010 wh.su
 * NULL
 * let the firmware download default enabled.
 *
 * 08 07 2010 wh.su
 * NULL
 * adding the privacy related code for P2P network
 *
 * 08 05 2010 yuche.tsai
 * NULL
 * Add a configure flag for P2P unitest.
 *
 * 07 23 2010 cp.wu
 *
 * 1) re-enable AIS-FSM beacon timeout handling.
 * 2) scan done API revised
 *
 * 07 23 2010 cp.wu
 *
 * 1) enable Ad-Hoc
 * 2) disable beacon timeout handling temporally due to unexpected beacon timeout event.
 *
 * 07 19 2010 wh.su
 *
 * update for security supporting.
 *
 * 07 19 2010 yuche.tsai
 *
 * Add for SLT support.
 *
 * 07 16 2010 cp.wu
 *
 * remove work-around in case SCN is not available.
 *
 * 07 14 2010 yarco.yang
 *
 * 1. Remove CFG_MQM_MIGRATION
 * 2. Add CMD_UPDATE_WMM_PARMS command
 *
 * 07 13 2010 cp.wu
 *
 * 1) MMPDUs are now sent to MT6620 by CMD queue for keeping strict order of 1X/MMPDU/CMD packets
 * 2) integrate with qmGetFrameAction() for deciding which MMPDU/1X could pass checking for sending
 * 2) enhance CMD_INFO_T descriptor number from 10 to 32 to avoid descriptor underflow under concurrent network operation
 *
 * 07 09 2010 yarco.yang
 *
 * [MT6620 and MT5931] SW Migration: Add ADDBA support
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 07 08 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * take use of RLM module for parsing/generating HT IEs for 11n capability
 *
 * 07 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * for first connection, if connecting failed do not enter into scan state.
 *
 * 06 25 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * modify Beacon/ProbeResp to complete parsing,
 * because host software has looser memory usage restriction
 *
 * 06 23 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) add SCN compilation option.
 * 2) when SCN is not turned on, BSSID_SCAN will generate a fake entry for 1st connection
 *
 * 06 22 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) add command warpper for STA-REC/BSS-INFO sync.
 * 2) enhance command packet sending procedure for non-oid part
 * 3) add command packet definitions for STA-REC/BSS-INFO sync.
 *
 * 06 21 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * set default compiling flag for security disable.
 *
 * 06 21 2010 yarco.yang
 * [WPD00003837][MT6620]Data Path Refine
 * Support CFG_MQM_MIGRATION flag
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * enable RX management frame handling.
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add scan_fsm into building.
 *
 * 06 18 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Provide cnmMgtPktAlloc() and alloc/free function of msg/buf
 *
 * 06 15 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add scan.c.
 *
 * 06 14 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add management dispatching function table.
 *
 * 06 11 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * auth.c is migrated.
 *
 * 06 11 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add bss.c.
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
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add aa_fsm.h, ais_fsm.h, bss.h, mib.h and scan.h.
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * merge wlan_def.h.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 05 31 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * add config option for cfg80211.
 *
 * 05 26 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * set ATIMwindow default value to zero.
 *
 * 05 21 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add option for FPGA_BWCS & FPGA_V5
 *
 * 05 20 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) integrate OID_GEN_NETWORK_LAYER_ADDRESSES with CMD_ID_SET_IP_ADDRESS
 * 2) buffer statistics data for 2 seconds
 * 3) use default value for adhoc parameters instead of 0
 *
 * 05 17 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) enable CMD/EVENT ver 0.9 definition.
 * 2) abandon use of ENUM_MEDIA_STATE
 *
 * 05 17 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add CFG_STARTUP_DEBUG for debugging starting up issue.
 *
 * 05 17 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * add basic handling framework for wireless extension ioctls.
 *
 * 05 11 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * change firmware name to WIFI_RAM_CODE.
 *
 * 05 07 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support
 * disable bt-over-wifi configuration, turn it on after firmware finished implementation
 *
 * 04 27 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support
 * add multiple physical link support
 *
 * 04 23 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * surpress compiler warning
 *
 * 04 22 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * re-enable power management
 *
 * 04 22 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 *
 * 1) modify rx path code for supporting Wi-Fi direct
 * 2) modify config.h since Linux dont need to consider retaining packet
 *
 * 04 22 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * enable TCP/IP checksum offloading by default.
 *
 * 04 19 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * set CFG_ENABLE_FULL_PM to 1 as default to
 * 1) acquire own before hardware access
 * 2) set own back after hardware access
 *
 * 04 15 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * change firmware name
 *
 * 04 07 2010 cp.wu
 * [WPD00003827][MT6620 Wi-Fi] Chariot fail and following ping fail, no pkt send from driver
 * disable RX-enhanced response temporally, it seems the CQ is not resolved yet.
 *
 * 04 06 2010 cp.wu
 * [WPD00003827][MT6620 Wi-Fi] Chariot fail and following ping fail, no pkt send from driver
 * re-enable RX enhanced mode as WPD00003827 is resolved.
 *
 * 04 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * turn off RX_ENHANCE mode by default.
 *
 * 04 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) eliminate unused definitions
 *  * 2) ready bit will be polled for limited iteration
 *
 * 04 02 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * firmware download: Linux uses different firmware path
 *
 * 04 01 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * change to use WIFI_TCM_ALWAYS_ON as firmware image
 *
 * 03 31 2010 wh.su
 * [WPD00003816][MT6620 Wi-Fi] Adding the security support
 * modify the wapi related code for new driver's design.
 *
 * 03 26 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add a temporary flag for integration with CMD/EVENT v0.9.
 *
 * 03 25 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * firmware download load adress & start address are now configured from config.h
 *  * due to the different configurations on FPGA and ASIC
 *
 * 03 24 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add options for full PM support.
 *
 * 03 24 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * [WPD00003826] Initial import for Linux port
 * initial import for Linux port
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
 * 03 16 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * turn on FW-DOWNLOAD as default for release.
 *
 * 03 16 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support
 * build up basic data structure and definitions to support BT-over-WiFi
 *
 * 03 12 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add two option for ACK and ENCRYPTION for firmware download
 *
 * 03 11 2010 cp.wu
 * [WPD00003821][BUG] Host driver stops processing RX packets from HIF RX0
 * add RX starvation warning debug message controlled by CFG_HIF_RX_STARVATION_WARNING
 *
 * 03 10 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * code clean: removing unused variables and structure definitions
 *
 * 03 05 2010 cp.wu
 * [WPD00003821][BUG] Host driver stops processing RX packets from HIF RX0
 * change CFG_NUM_OF_QM_RX_PKT_NUM to 120
 *
 * 03 04 2010 cp.wu
 * [WPD00003821][BUG] Host driver stops processing RX packets from HIF RX0
 * .
 *
 * 03 04 2010 cp.wu
 * [WPD00003821][BUG] Host driver stops processing RX packets from HIF RX0
 * increase RX buffer number to avoid RX buffer starvation.
 *
 * 02 24 2010 tehuang.liu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * Changed the number of STA_RECs to 20
 *
 * 02 23 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add checksum offloading support.
 *
 * 02 11 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1. add logic for firmware download
 *  * 2. firmware image filename and start/load address are now retrieved from registry
 *
 * 02 08 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * prepare for implementing fw download logic
 *
 * 12 30 2009 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) According to CMD/EVENT documentation v0.8,
 *  * OID_CUSTOM_TEST_RX_STATUS & OID_CUSTOM_TEST_TX_STATUS is no longer used,
 *  * and result is retrieved by get ATInfo instead
 *  * 2) add 4 counter for recording aggregation statistics
**  \main\maintrunk.MT6620WiFiDriver_Prj\25 2009-12-16 22:12:28 GMT mtk02752
**  enable interrupt enhanced response, TX/RX Aggregation as default
**  \main\maintrunk.MT6620WiFiDriver_Prj\24 2009-12-10 16:38:43 GMT mtk02752
**  eliminate compile options which are obsolete or for emulation purpose
**  \main\maintrunk.MT6620WiFiDriver_Prj\23 2009-12-09 13:56:26 GMT MTK02468
**  Added RX buffer reordering configurations
**  \main\maintrunk.MT6620WiFiDriver_Prj\22 2009-12-04 12:09:09 GMT mtk02752
**  once enhanced intr/rx reponse is taken, RX must be access in aggregated basis
**  \main\maintrunk.MT6620WiFiDriver_Prj\21 2009-11-23 17:54:50 GMT mtk02752
**  correct a typo
**  \main\maintrunk.MT6620WiFiDriver_Prj\20 2009-11-17 22:40:47 GMT mtk01084
**  add defines
**  \main\maintrunk.MT6620WiFiDriver_Prj\19 2009-11-17 17:33:37 GMT mtk02752
**  add coalescing buffer definition for SD1_SD3_DATAPATH_INTEGRATION
**  \main\maintrunk.MT6620WiFiDriver_Prj\18 2009-11-16 20:32:40 GMT mtk02752
**  add CFG_TX_MAX_PKT_NUM for limiting queued TX packet
**  \main\maintrunk.MT6620WiFiDriver_Prj\17 2009-11-16 13:34:44 GMT mtk02752
**  add SD1_SD3_DATAPATH_INTEGRATION define for source control
**  \main\maintrunk.MT6620WiFiDriver_Prj\16 2009-11-13 13:54:11 GMT mtk01084
**  enable INT enhance mode by default
**  \main\maintrunk.MT6620WiFiDriver_Prj\15 2009-10-30 18:17:14 GMT mtk01084
**  add new define
**  \main\maintrunk.MT6620WiFiDriver_Prj\14 2009-10-29 19:47:36 GMT mtk01084
**  not use HIF loopback mode
**  \main\maintrunk.MT6620WiFiDriver_Prj\13 2009-10-13 21:58:33 GMT mtk01084
**  update for new macro define
**  \main\maintrunk.MT6620WiFiDriver_Prj\12 2009-09-09 17:26:08 GMT mtk01084
**  add CFG_TEST_WITH_MT5921
**  \main\maintrunk.MT6620WiFiDriver_Prj\11 2009-05-18 21:02:30 GMT mtk01426
**  Update CFG_RX_COALESCING_BUFFER_SIZE
**  \main\maintrunk.MT6620WiFiDriver_Prj\10 2009-04-21 09:35:51 GMT mtk01461
**  Add CFG_TX_DBG_MGT_BUF to debug MGMT Buffer depth
**  \main\maintrunk.MT6620WiFiDriver_Prj\9 2009-04-14 15:52:21 GMT mtk01426
**  Add OOB_DATA_PRE_FIXED_LEN define
**  \main\maintrunk.MT6620WiFiDriver_Prj\8 2009-04-08 16:51:08 GMT mtk01084
**  update for FW download part
**  \main\maintrunk.MT6620WiFiDriver_Prj\7 2009-04-01 10:33:37 GMT mtk01461
**  Add SW pre test flag CFG_HIF_LOOPBACK_PRETEST
**  \main\maintrunk.MT6620WiFiDriver_Prj\6 2009-03-23 00:29:18 GMT mtk01461
**  Fix CFG_COALESCING_BUFFER_SIZE if enable the CFG_TX_FRAGMENT
**  \main\maintrunk.MT6620WiFiDriver_Prj\5 2009-03-18 20:58:34 GMT mtk01426
**  Add CFG_HIF_LOOPBACK and CFG_SDIO_RX_ENHANCE
**  \main\maintrunk.MT6620WiFiDriver_Prj\4 2009-03-17 20:17:36 GMT mtk01426
**  Add CMD/Response related configure
**  \main\maintrunk.MT6620WiFiDriver_Prj\3 2009-03-16 09:08:21 GMT mtk01461
**  Update TX PATH API
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-03-10 20:11:21 GMT mtk01426
**  Init for develop
**
*/

#ifndef _CONFIG_H
#define _CONFIG_H
/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

#undef MT6620
#undef MT6628


/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
//2 Flags for OS capability

#ifdef LINUX
    #ifdef CONFIG_X86
        #define MTK_WCN_HIF_SDIO        0
    #else
        #define MTK_WCN_HIF_SDIO        0
    #endif
#else
    #define MTK_WCN_HIF_SDIO            0
#endif

#if (CFG_SUPPORT_AEE == 1)
    #define CFG_ENABLE_AEE_MSG          1
#else
    #define CFG_ENABLE_AEE_MSG          0
#endif

//2 Flags for Driver Features
#define CFG_TX_FRAGMENT                 1 /*!< 1: Enable TX fragmentation
                                                       0: Disable */
#define CFG_SUPPORT_PERFORMANCE_TEST    0  /*Only for performance Test*/

#define CFG_COUNTRY_CODE                NULL //"US"

#ifndef LINUX
    #define CFG_FW_FILENAME             L"WIFI_RAM_CODE_MT5931"
    #define CFG_FW_FILENAME_E6          L"WIFI_RAM_CODE_MT5931_E6"
#else
    #define CFG_FW_FILENAME             "WIFI_RAM_CODE_MT5931"
#endif

#define CFG_SUPPORT_802_11D             1 /*!< 1(default): Enable 802.11d
                                                     0: Disable */

#define CFG_SUPPORT_SPEC_MGMT       0   /* Spectrum Management (802.11h): TPC and DFS */
#define CFG_SUPPORT_RRM             0   /* Radio Reasource Measurement (802.11k) */
#define CFG_SUPPORT_QUIET           0   /* Quiet (802.11h) */


#define CFG_SUPPORT_RX_RDG          0   /* 11n feature. RX RDG capability */
#define CFG_SUPPORT_MFB             0   /* 802.11n MCS Feedback responder */
#define CFG_SUPPORT_RX_STBC         1   /* 802.11n RX STBC (1SS) */
#define CFG_SUPPORT_RX_SGI          1   /* 802.11n RX short GI for both 20M and 40M BW */
#define CFG_SUPPORT_RX_HT_GF        1   /* 802.11n RX HT green-field capability */

/*------------------------------------------------------------------------------
 * SLT Option
 *------------------------------------------------------------------------------
 */
#define CFG_SLT_SUPPORT                             0

//#define CFG_DUAL_ANTENNA                            0

#define CFG_SUPPORT_RSSI_SMOOTH                     0


#ifdef NDIS60_MINIPORT
    #define CFG_NATIVE_802_11                       1

    #define CFG_TX_MAX_PKT_SIZE                     2304
    #define CFG_TCP_IP_CHKSUM_OFFLOAD_NDIS_60       0 /* !< 1: Enable TCP/IP header checksum offload
                                                            0: Disable */
    #define CFG_TCP_IP_CHKSUM_OFFLOAD               0
    #define CFG_WHQL_DOT11_STATISTICS               1
    #define CFG_WHQL_ADD_REMOVE_KEY                 1
    #define CFG_WHQL_CUSTOM_IE                      1
    #define CFG_WHQL_SAFE_MODE_ENABLED              1

#else
    #define CFG_TCP_IP_CHKSUM_OFFLOAD               1 /* !< 1: Enable TCP/IP header checksum offload
                                                            0: Disable */
    #define CFG_TCP_IP_CHKSUM_OFFLOAD_NDIS_60       0
    #define CFG_TX_MAX_PKT_SIZE                     1600
    #define CFG_NATIVE_802_11                       0
#endif


//2 Flags for Driver Parameters
/*------------------------------------------------------------------------------
 * Flags for EHPI Interface in Colibri Platform
 *------------------------------------------------------------------------------
 */
#define CFG_EHPI_FASTER_BUS_TIMING                  0 /*!< 1: Do workaround for faster bus timing
                                                           0(default): Disable */

/*------------------------------------------------------------------------------
 * Flags for HIFSYS Interface
 *------------------------------------------------------------------------------
 */
#ifdef _lint
    #define _HIF_SDIO   1
#endif

#define CFG_SDIO_INTR_ENHANCE                        1 /*!< 1(default): Enable SDIO ISR & TX/RX status enhance mode
                                                            0: Disable */
#define CFG_SDIO_RX_ENHANCE                          0 /*!< 1(default): Enable SDIO ISR & TX/RX status enhance mode
                                                            0: Disable */
#define CFG_SDIO_TX_AGG                              1 /*!< 1: Enable SDIO TX enhance mode(Multiple frames in single BLOCK CMD)
                                                            0(default): Disable */

#define CFG_SDIO_RX_AGG                              1 /*!< 1: Enable SDIO RX enhance mode(Multiple frames in single BLOCK CMD)
                                                            0(default): Disable */

#if (CFG_SDIO_RX_AGG == 1) && (CFG_SDIO_INTR_ENHANCE == 0)
    #error "CFG_SDIO_INTR_ENHANCE should be 1 once CFG_SDIO_RX_AGG equals to 1"
#elif (CFG_SDIO_INTR_ENHANCE == 1 || CFG_SDIO_RX_ENHANCE == 1) && (CFG_SDIO_RX_AGG == 0)
    #error "CFG_SDIO_RX_AGG should be 1 once CFG_SDIO_INTR_ENHANCE and/or CFG_SDIO_RX_ENHANCE equals to 1"
#endif

#define CFG_SDIO_MAX_RX_AGG_NUM                     0 /*!< 1: Setting the maximum RX aggregation number
                                                           0(default): no limited */

#ifdef WINDOWS_CE
    #define CFG_SDIO_PATHRU_MODE                    1 /*!< 1: Suport pass through (PATHRU) mode
                                                           0: Disable */
#else
    #define CFG_SDIO_PATHRU_MODE                    0 /*!< 0: Always disable if WINDOWS_CE is not defined */
#endif

#define CFG_MAX_RX_ENHANCE_LOOP_COUNT               3


/*------------------------------------------------------------------------------
 * Flags and Parameters for Integration
 *------------------------------------------------------------------------------
 */
#if defined(MT6620)
    #define MT6620_FPGA_BWCS    0
    #define MT6620_FPGA_V5      0

    #if (MT6620_FPGA_BWCS == 1) && (MT6620_FPGA_V5 == 1)
        #error
    #endif

    #if (MTK_WCN_HIF_SDIO == 1)
        #define CFG_MULTI_ECOVER_SUPPORT    1
    #elif !defined(LINUX)
        #define CFG_MULTI_ECOVER_SUPPORT    1
    #else
        #define CFG_MULTI_ECOVER_SUPPORT    0
    #endif

    #define CFG_ENABLE_CAL_LOG      0
    #define CFG_REPORT_RFBB_VERSION       0
#elif defined(MT5931)

#define CFG_MULTI_ECOVER_SUPPORT    0
#define CFG_ENABLE_CAL_LOG      0
#define CFG_REPORT_RFBB_VERSION       0

#elif defined(MT6628)

#define CFG_MULTI_ECOVER_SUPPORT    0

#define CFG_ENABLE_CAL_LOG      1
#define CFG_REPORT_RFBB_VERSION       1

#endif

#define CFG_CHIP_RESET_SUPPORT                      0


/*------------------------------------------------------------------------------
 * Flags for workaround
 *------------------------------------------------------------------------------
 */
#if defined(MT6620) && (MT6620_FPGA_BWCS == 0) && (MT6620_FPGA_V5 == 0)
    #define MT6620_E1_ASIC_HIFSYS_WORKAROUND            0
#else
    #define MT6620_E1_ASIC_HIFSYS_WORKAROUND            0
#endif

/*------------------------------------------------------------------------------
 * Flags for driver version
 *------------------------------------------------------------------------------
 */
#define CFG_DRV_OWN_VERSION                     ((UINT_16)((NIC_DRIVER_MAJOR_VERSION << 8) | (NIC_DRIVER_MINOR_VERSION)))
#define CFG_DRV_PEER_VERSION                    ((UINT_16)0x0000)


/*------------------------------------------------------------------------------
 * Flags for TX path which are OS dependent
 *------------------------------------------------------------------------------
 */
/*! NOTE(Kevin): If the Network buffer is non-scatter-gather like structure(without
 * NETIF_F_FRAGLIST in LINUX), then we can set CFG_TX_BUFFER_IS_SCATTER_LIST to "0"
 * for zero copy TX packets.
 * For scatter-gather like structure, we set "1", driver will do copy frame to
 * internal coalescing buffer before write it to FIFO.
 */
#if defined(LINUX)
    #define CFG_TX_BUFFER_IS_SCATTER_LIST       1 /*!< 1: Do frame copy before write to TX FIFO.
                                                        Used when Network buffer is scatter-gather.
                                                     0(default): Do not copy frame */
#else /* WINDOWS/WINCE */
    #define CFG_TX_BUFFER_IS_SCATTER_LIST       1
#endif /* LINUX */


#if CFG_SDIO_TX_AGG || CFG_TX_BUFFER_IS_SCATTER_LIST
    #define CFG_COALESCING_BUFFER_SIZE          (CFG_TX_MAX_PKT_SIZE * NIC_TX_BUFF_SUM)
#else
    #define CFG_COALESCING_BUFFER_SIZE          (CFG_TX_MAX_PKT_SIZE)
#endif /* CFG_SDIO_TX_AGG || CFG_TX_BUFFER_IS_SCATTER_LIST */

/*------------------------------------------------------------------------------
 * Flags and Parameters for TX path
 *------------------------------------------------------------------------------
 */

/*! Maximum number of SW TX packet queue */
#define CFG_TX_MAX_PKT_NUM                      256

/*! Maximum number of SW TX CMD packet buffer */
#define CFG_TX_MAX_CMD_PKT_NUM                  32

/*! Maximum number of associated STAs */
#define CFG_NUM_OF_STA_RECORD                   20

/*------------------------------------------------------------------------------
 * Flags and Parameters for RX path
 *------------------------------------------------------------------------------
 */

/*! Max. descriptor number - sync. with firmware */
#if CFG_SLT_SUPPORT
#define CFG_NUM_OF_RX0_HIF_DESC                 42
#else
#define CFG_NUM_OF_RX0_HIF_DESC                 16
#endif
#define CFG_NUM_OF_RX1_HIF_DESC                 2

/*! Max. buffer hold by QM */
#define CFG_NUM_OF_QM_RX_PKT_NUM                120

/*! Maximum number of SW RX packet buffer */
#define CFG_RX_MAX_PKT_NUM                      ((CFG_NUM_OF_RX0_HIF_DESC + CFG_NUM_OF_RX1_HIF_DESC) * 3 \
                                                + CFG_NUM_OF_QM_RX_PKT_NUM)

#define CFG_RX_REORDER_Q_THRESHOLD              8

#ifndef LINUX
#define CFG_RX_RETAINED_PKT_THRESHOLD           (CFG_NUM_OF_RX0_HIF_DESC + CFG_NUM_OF_RX1_HIF_DESC + CFG_NUM_OF_QM_RX_PKT_NUM)
#else
#define CFG_RX_RETAINED_PKT_THRESHOLD           0
#endif

/*! Maximum RX packet size, if exceed this value, drop incoming packet */
/* 7.2.3 Maganement frames */
#define CFG_RX_MAX_PKT_SIZE   ( 28 + 2312 + 12 /*HIF_RX_HEADER_T*/ )  //TODO: it should be 4096 under emulation mode

/*! Minimum RX packet size, if lower than this value, drop incoming packet */
#define CFG_RX_MIN_PKT_SIZE                     10 /*!< 802.11 Control Frame is 10 bytes */

#if CFG_SDIO_RX_AGG
    /* extra size for CS_STATUS and enhanced response */
    #define CFG_RX_COALESCING_BUFFER_SIZE       ((CFG_NUM_OF_RX0_HIF_DESC  + 1) \
                                                * CFG_RX_MAX_PKT_SIZE)
#else
    #define CFG_RX_COALESCING_BUFFER_SIZE       (CFG_RX_MAX_PKT_SIZE)
#endif

/*! RX BA capability */
#define CFG_NUM_OF_RX_BA_AGREEMENTS             8
#define CFG_RX_BA_MAX_WINSIZE                   16
#define CFG_RX_BA_INC_SIZE                      4
#define CFG_RX_MAX_BA_TID_NUM                   8
#define CFG_RX_REORDERING_ENABLED               1

/*------------------------------------------------------------------------------
 * Flags and Parameters for CMD/RESPONSE
 *------------------------------------------------------------------------------
 */
#define CFG_RESPONSE_POLLING_TIMEOUT            512


/*------------------------------------------------------------------------------
 * Flags and Parameters for Protocol Stack
 *------------------------------------------------------------------------------
 */
/*! Maximum number of BSS in the SCAN list */
#define CFG_MAX_NUM_BSS_LIST                    64

#define CFG_MAX_COMMON_IE_BUF_LEN         (1500 * CFG_MAX_NUM_BSS_LIST) / 3

/*! Maximum size of IE buffer of each SCAN record */
#define CFG_IE_BUFFER_SIZE                      512

/*! Maximum number of STA records */
#define CFG_MAX_NUM_STA_RECORD                  32



/*------------------------------------------------------------------------------
 * Flags and Parameters for Power management
 *------------------------------------------------------------------------------
 */
#define CFG_ENABLE_FULL_PM                      1
#define CFG_ENABLE_WAKEUP_ON_LAN                0

#define CFG_INIT_POWER_SAVE_PROF                    ENUM_PSP_FAST_SWITCH

#define CFG_INIT_ENABLE_PATTERN_FILTER_ARP                    0

#define CFG_INIT_UAPSD_AC_BMP                    0//(BIT(3) | BIT(2) | BIT(1) | BIT(0))

//#define CFG_SUPPORT_WAPI                        0
#define CFG_SUPPORT_WPS                          1
#define CFG_SUPPORT_WPS2                         1

/*------------------------------------------------------------------------------
 * 802.11i RSN Pre-authentication PMKID cahce maximun number
 *------------------------------------------------------------------------------
 */
#define CFG_MAX_PMKID_CACHE                     16      /*!< max number of PMKID cache
                                                           16(default) : The Max PMKID cache */

/*------------------------------------------------------------------------------
 * Flags and Parameters for Ad-Hoc
 *------------------------------------------------------------------------------
 */
#define CFG_INIT_ADHOC_FREQ                     (2462000)
#define CFG_INIT_ADHOC_MODE                     AD_HOC_MODE_MIXED_11BG
#define CFG_INIT_ADHOC_BEACON_INTERVAL          (100)
#define CFG_INIT_ADHOC_ATIM_WINDOW              (0)


/*------------------------------------------------------------------------------
 * Flags and Parameters for Load Setup Default
 *------------------------------------------------------------------------------
 */

/*------------------------------------------------------------------------------
 * Flags for enable 802.11A Band setting
 *------------------------------------------------------------------------------
 */

/*------------------------------------------------------------------------------
 * Flags and Parameters for Interrupt Process
 *------------------------------------------------------------------------------
 */
#if defined(_HIF_SDIO) && defined(WINDOWS_CE)
    #define CFG_IST_LOOP_COUNT                  1
#else
    #define CFG_IST_LOOP_COUNT                  1
#endif /* _HIF_SDIO */

#define CFG_INT_WRITE_CLEAR                     0

#if defined(LINUX)
#define CFG_DBG_GPIO_PINS                       0 /* if 1, use MT6516 GPIO pin to log TX behavior */
#endif

//2 Flags for Driver Debug Options
/*------------------------------------------------------------------------------
 * Flags of TX Debug Option. NOTE(Kevin): Confirm with SA before modifying following flags.
 *------------------------------------------------------------------------------
 */
#define CFG_DBG_MGT_BUF                         1 /*!< 1: Debug statistics usage of MGMT Buffer
                                                       0: Disable */

#define CFG_HIF_STATISTICS                      0

#define CFG_HIF_RX_STARVATION_WARNING           0

#define CFG_STARTUP_DEBUG                       0

#define CFG_RX_PKTS_DUMP                        1

/*------------------------------------------------------------------------------
 * Flags of Firmware Download Option.
 *------------------------------------------------------------------------------
 */
#define CFG_ENABLE_FW_DOWNLOAD                  1

#define CFG_ENABLE_FW_DOWNLOAD_ACK              1
#define CFG_ENABLE_FW_ENCRYPTION                1

#if defined(MT6620) || defined(MT6628)
    #define CFG_ENABLE_FW_DOWNLOAD_AGGREGATION  0
    #define CFG_ENABLE_FW_DIVIDED_DOWNLOAD      1
#else
    #define CFG_ENABLE_FW_DOWNLOAD_AGGREGATION  0
    #define CFG_ENABLE_FW_DIVIDED_DOWNLOAD      0
#endif



#if defined(MT6620)
    #if MT6620_FPGA_BWCS
        #define CFG_FW_LOAD_ADDRESS                     0x10014000
        #define CFG_OVERRIDE_FW_START_ADDRESS           0
        #define CFG_FW_START_ADDRESS                    0x10014001
    #elif MT6620_FPGA_V5
        #define CFG_FW_LOAD_ADDRESS                     0x10008000
        #define CFG_OVERRIDE_FW_START_ADDRESS           0
        #define CFG_FW_START_ADDRESS                    0x10008001
    #else
        #define CFG_FW_LOAD_ADDRESS                     0x10008000
        #define CFG_OVERRIDE_FW_START_ADDRESS           0
        #define CFG_FW_START_ADDRESS                    0x10008001
    #endif
#elif defined(MT5931)
    #define CFG_FW_LOAD_ADDRESS                     0xFF900000
    #define CFG_FW_START_ADDRESS                    0x00000000
#elif defined(MT6628)
    #define CFG_FW_LOAD_ADDRESS                     0x00060000
    #define CFG_OVERRIDE_FW_START_ADDRESS           0
    #define CFG_FW_START_ADDRESS                    0x00060000
#endif


/*------------------------------------------------------------------------------
 * Flags of Bluetooth-over-WiFi (BT 3.0 + HS) support
 *------------------------------------------------------------------------------
 */

#ifdef LINUX
    #ifdef CONFIG_X86
        #define CFG_ENABLE_BT_OVER_WIFI         0
    #else
        #define CFG_ENABLE_BT_OVER_WIFI         1
    #endif
#else
    #define CFG_ENABLE_BT_OVER_WIFI             0
#endif

#define CFG_BOW_SEPARATE_DATA_PATH              1

#define CFG_BOW_PHYSICAL_LINK_NUM               4

#define CFG_BOW_TEST                            0

#define CFG_BOW_LIMIT_AIS_CHNL                  1

#define CFG_BOW_SUPPORT_11N                     0

#define CFG_BOW_RATE_LIMITATION                 1

/*------------------------------------------------------------------------------
 * Flags of Wi-Fi Direct support
 *------------------------------------------------------------------------------
 */
#ifdef LINUX
    #ifdef CONFIG_X86
        #define CFG_ENABLE_WIFI_DIRECT          0
        #define CFG_SUPPORT_802_11W             0
    #else
        #define CFG_ENABLE_WIFI_DIRECT          1
        #define CFG_SUPPORT_802_11W             0 /*!< 0(default): Disable 802.11W */
    #endif
#else
    #define CFG_ENABLE_WIFI_DIRECT              0
    #define CFG_SUPPORT_802_11W                 0 /* Not support at WinXP */
#endif

#define CFG_SUPPORT_PERSISTENT_GROUP     0

#define CFG_TEST_WIFI_DIRECT_GO                 0

#define CFG_TEST_ANDROID_DIRECT_GO              0

#define CFG_UNITEST_P2P                         0

/*
 * Enable cfg80211 option after Android 2.2(Froyo) is suggested,
 * cfg80211 on linux 2.6.29 is not mature yet
 */
#define CFG_ENABLE_WIFI_DIRECT_CFG_80211        1

/*------------------------------------------------------------------------------
 * Configuration Flags (Linux Only)
 *------------------------------------------------------------------------------
 */
#define CFG_SUPPORT_EXT_CONFIG                  0

/*------------------------------------------------------------------------------
 * Statistics Buffering Mechanism
 *------------------------------------------------------------------------------
 */
#if CFG_SUPPORT_PERFORMANCE_TEST
#define CFG_ENABLE_STATISTICS_BUFFERING         1
#else
#define CFG_ENABLE_STATISTICS_BUFFERING         0
#endif
#define CFG_STATISTICS_VALID_CYCLE              2000
#define CFG_LINK_QUALITY_VALID_PERIOD           5000

/*------------------------------------------------------------------------------
 * Migration Option
 *------------------------------------------------------------------------------
 */
#define CFG_SUPPORT_ADHOC                       1
#define CFG_SUPPORT_AAA                         1


#if (defined(MT5931) && defined(LINUX))
#define CFG_SUPPORT_BCM                         1
#define CFG_SUPPORT_BCM_BWCS                    1
#define CFG_SUPPORT_BCM_BWCS_DEBUG              1
#else
#define CFG_SUPPORT_BCM                         0
#define CFG_SUPPORT_BCM_BWCS                    0
#define CFG_SUPPORT_BCM_BWCS_DEBUG              0
#endif

#define CFG_SUPPORT_RDD_TEST_MODE       0

#define CFG_SUPPORT_PWR_MGT                     1

#define CFG_RSN_MIGRATION                       1

#define CFG_PRIVACY_MIGRATION                   1

#define CFG_ENABLE_HOTSPOT_PRIVACY_CHECK        1

#define CFG_MGMT_FRAME_HANDLING                 1

#define CFG_MGMT_HW_ACCESS_REPLACEMENT          0

#if CFG_SUPPORT_PERFORMANCE_TEST

#else

#endif

#define CFG_SUPPORT_AIS_5GHZ                    1
#define CFG_SUPPORT_BEACON_CHANGE_DETECTION     0

/*------------------------------------------------------------------------------
 * Option for NVRAM and Version Checking
 *------------------------------------------------------------------------------
 */
#define CFG_SUPPORT_NVRAM                       1
#define CFG_NVRAM_EXISTENCE_CHECK               1
#define CFG_SW_NVRAM_VERSION_CHECK              1
#define CFG_SUPPORT_NIC_CAPABILITY              1


/*------------------------------------------------------------------------------
 * CONFIG_TITLE : Stress Test Option
 * OWNER        : Puff Wen
 * Description  : For stress test only. DO NOT enable it while normal operation
 *------------------------------------------------------------------------------
 */
#define CFG_STRESS_TEST_SUPPORT                 0

/*------------------------------------------------------------------------------
 * Flags for LINT
 *------------------------------------------------------------------------------
 */
#define LINT_SAVE_AND_DISABLE                   /*lint -save -e* */

#define LINT_RESTORE                            /*lint -restore */

#define LINT_EXT_HEADER_BEGIN                   LINT_SAVE_AND_DISABLE

#define LINT_EXT_HEADER_END                     LINT_RESTORE

/*------------------------------------------------------------------------------
 * Flags of Features
 *------------------------------------------------------------------------------
 */

#define CFG_SUPPORT_QOS             1   /* Enable/disable QoS TX, AMPDU */
#define CFG_SUPPORT_AMPDU_TX        1
#define CFG_SUPPORT_AMPDU_RX        1
#define CFG_SUPPORT_TSPEC           0   /* Enable/disable TS-related Action frames handling */
#define CFG_SUPPORT_UAPSD           1
#define CFG_SUPPORT_UL_PSMP         0

#define CFG_SUPPORT_ROAMING         1  /* Roaming System */
#define CFG_SUPPORT_SWCR            1

#define CFG_SUPPORT_ANTI_PIRACY     1

#define CFG_SUPPORT_OSC_SETTING     1

#if defined(MT5931)
#define CFG_SUPPORT_WHOLE_CHIP_RESET    0  /* for e3 chip only */
#endif

#define CFG_SUPPORT_P2P_RSSI_QUERY        0

#define CFG_SHOW_MACADDR_SOURCE     1

#define CFG_SUPPORT_802_11V                    0  /* Support 802.11v Wireless Network Management */
#define CFG_SUPPORT_802_11V_TIMING_MEASUREMENT 0
#if (CFG_SUPPORT_802_11V_TIMING_MEASUREMENT == 1) && (CFG_SUPPORT_802_11V == 0)
    #error "CFG_SUPPORT_802_11V should be 1 once CFG_SUPPORT_802_11V_TIMING_MEASUREMENT equals to 1"
#endif
#if (CFG_SUPPORT_802_11V == 0)
#define WNM_UNIT_TEST 0
#endif

#define CFG_DRIVER_COMPOSE_ASSOC_REQ        1

#define CFG_STRICT_CHECK_CAPINFO_PRIVACY    0

#define CFG_SUPPORT_WFD                     1

#define CFG_SUPPORT_WFD_COMPOSE_IE          1

#define CFG_ENABLE_PKT_LIFETIME_PROFILE     0

/*------------------------------------------------------------------------------
 * Flags of bus error tolerance
 *------------------------------------------------------------------------------
 */
#define CFG_FORCE_RESET_UNDER_BUS_ERROR     0

/*------------------------------------------------------------------------------
 * Build Date Code Integration
 *------------------------------------------------------------------------------
 */
#define CFG_SUPPORT_BUILD_DATE_CODE         0

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
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
#endif /* _CONFIG_H */


