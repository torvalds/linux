/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/common/wlan_oid.c#5 $
*/

/*! \file wlanoid.c
    \brief This file contains the WLAN OID processing routines of Windows driver for
           MediaTek Inc. 802.11 Wireless LAN Adapters.
*/



/*
** $Log: wlan_oid.c $
**
** 07 19 2012 yuche.tsai
** NULL
** Code update for JB.
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Let netdev bring up.
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Compile no error before trial run.
 *
 * 03 02 2012 terry.wu
 * NULL
 * Sync CFG80211 modification from branch 2,2.
 *
 * 01 06 2012 wh.su
 * [WCXRP00001153] [MT6620 Wi-Fi][Driver] Adding the get_ch_list and set_tx_power proto type function
 * using the wlanSendSetQueryCmd to set the tx power control cmd.
 *
 * 01 06 2012 wh.su
 * [WCXRP00001153] [MT6620 Wi-Fi][Driver] Adding the get_ch_list and set_tx_power proto type function
 * change the set tx power cmd name.
 *
 * 01 05 2012 wh.su
 * [WCXRP00001153] [MT6620 Wi-Fi][Driver] Adding the get_ch_list and set_tx_power proto type function
 * Adding the related ioctl / wlan oid function to set the Tx power cfg.
 *
 * 12 20 2011 cp.wu
 * [WCXRP00001144] [MT6620 Wi-Fi][Driver][Firmware] Add RF_FUNC_ID for exposing device and related version information
 * add driver implementations for RF_AT_FUNCID_FW_INFO & RF_AT_FUNCID_DRV_INFO
 * to expose version information
 *
 * 12 05 2011 cp.wu
 * [WCXRP00001131] [MT6620 Wi-Fi][Driver][AIS] Implement connect-by-BSSID path
 * add CONNECT_BY_BSSID policy
 *
 * 11 22 2011 cp.wu
 * [WCXRP00001120] [MT6620 Wi-Fi][Driver] Modify roaming to AIS state transition from synchronous to asynchronous approach to avoid incomplete state termination
 * 1. change RDD related compile option brace position.
 * 2. when roaming is triggered, ask AIS to transit immediately only when AIS is in Normal TR state without join timeout timer ticking
 * 3. otherwise, insert AIS_REQUEST into pending request queue
 *
 * 11 21 2011 cp.wu
 * [WCXRP00001118] [MT6620 Wi-Fi][Driver] Corner case protections to pass Monkey testing
 * 1. wlanoidQueryBssIdList might be passed with a non-zero length but a NULL pointer of buffer
 * add more checking for such cases
 *
 * 2. kalSendComplete() might be invoked with a packet belongs to P2P network right after P2P is unregistered.
 * add some tweaking to protect such cases because that net device has become invalid.
 *
 * 11 15 2011 cm.chang
 * NULL
 * Fix compiling warning
 *
 * 11 11 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * modify the xlog related code.
 *
 * 11 11 2011 tsaiyuan.hsu
 * [WCXRP00001083] [MT6620 Wi-Fi][DRV]] dump debug counter or frames when debugging is triggered
 * add debug counters of bb and ar for xlog.
 *
 * 11 10 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * change the debug module level.
 *
 * 11 09 2011 george.huang
 * [WCXRP00000871] [MT6620 Wi-Fi][FW] Include additional wakeup condition, which is by consequent DTIM unicast indication
 * add XLOG for Set PS mode entry
 *
 * 11 08 2011 tsaiyuan.hsu
 * [WCXRP00001083] [MT6620 Wi-Fi][DRV]] dump debug counter or frames when debugging is triggered
 * check if CFG_SUPPORT_SWCR is defined to aoid compiler error.
 *
 * 11 07 2011 tsaiyuan.hsu
 * [WCXRP00001083] [MT6620 Wi-Fi][DRV]] dump debug counter or frames when debugging is triggered
 * add debug counters and periodically dump counters for debugging.
 *
 * 11 03 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * change the DBGLOG for "\n" and "\r\n". LABEL to LOUD for XLOG
 *
 * 11 02 2011 chinghwa.yu
 * [WCXRP00000612] [MT6620 Wi-Fi] [FW] CSD update SWRDD algorithm
 * Add RDD certification features.
 *
 * 10 21 2011 eddie.chen
 * [WCXRP00001051] [MT6620 Wi-Fi][Driver/Fw] Adjust the STA aging timeout
 * Add switch to ignore the STA aging timeout.
 *
 * 10 12 2011 wh.su
 * [WCXRP00001036] [MT6620 Wi-Fi][Driver][FW] Adding the 802.11w code for MFP
 * adding the 802.11w related function and define .
 *
 * 09 15 2011 tsaiyuan.hsu
 * [WCXRP00000938] [MT6620 Wi-Fi][FW] add system config for CTIA
 * correct fifo full control from query to set operation for CTIA.
 *
 * 08 31 2011 cm.chang
 * [WCXRP00000969] [MT6620 Wi-Fi][Driver][FW] Channel list for 5G band based on country code
 * .
 *
 * 08 17 2011 tsaiyuan.hsu
 * [WCXRP00000938] [MT6620 Wi-Fi][FW] add system config for CTIA
 * add system config for CTIA.
 *
 * 08 15 2011 george.huang
 * [MT6620 Wi-Fi][FW] handle TSF drift for connection detection
 * .
 *
 * 07 28 2011 chinghwa.yu
 * [WCXRP00000063] Update BCM CoEx design and settings
 * Add BWCS cmd and event.
 *
 * 07 18 2011 chinghwa.yu
 * [WCXRP00000063] Update BCM CoEx design and settings[WCXRP00000612] [MT6620 Wi-Fi] [FW] CSD update SWRDD algorithm
 * Add CMD/Event for RDD and BWCS.
 *
 * 07 11 2011 wh.su
 * [WCXRP00000849] [MT6620 Wi-Fi][Driver] Remove some of the WAPI define for make sure the value is initialize, for customer not enable WAPI
 * For make sure wapi initial value is set.
 *
 * 06 23 2011 cp.wu
 * [WCXRP00000812] [MT6620 Wi-Fi][Driver] not show NVRAM when there is no valid MAC address in NVRAM content
 * check with firmware for valid MAC address.
 *
 * 05 02 2011 eddie.chen
 * [WCXRP00000373] [MT6620 Wi-Fi][FW] SW debug control
 * Fix compile warning.
 *
 * 04 29 2011 george.huang
 * [WCXRP00000684] [MT6620 Wi-Fi][Driver] Support P2P setting ARP filter
 * .
 *
 * 04 27 2011 george.huang
 * [WCXRP00000684] [MT6620 Wi-Fi][Driver] Support P2P setting ARP filter
 * add more debug message
 *
 * 04 26 2011 eddie.chen
 * [WCXRP00000373] [MT6620 Wi-Fi][FW] SW debug control
 * Add rx path profiling.
 *
 * 04 12 2011 eddie.chen
 * [WCXRP00000617] [MT6620 Wi-Fi][DRV/FW] Fix for sigma
 * Fix the sta index in processing security frame
 * Simple flow control for TC4 to avoid mgt frames for PS STA to occupy the TC4
 * Add debug message.
 *
 * 04 08 2011 george.huang
 * [WCXRP00000621] [MT6620 Wi-Fi][Driver] Support P2P supplicant to set power mode
 * separate settings of P2P and AIS
 *
 * 03 31 2011 puff.wen
 * NULL
 * .
 *
 * 03 29 2011 puff.wen
 * NULL
 * Add chennel switch for stress test
 *
 * 03 29 2011 cp.wu
 * [WCXRP00000604] [MT6620 Wi-Fi][Driver] Surpress Klockwork Warning
 * surpress klock warning with code path rewritten
 *
 * 03 24 2011 wh.su
 * [WCXRP00000595] [MT6620 Wi-Fi][Driver] at CTIA indicate disconnect to make the ps profile can apply
 * use disconnect event instead of ais abort for CTIA testing.
 *
 * 03 23 2011 george.huang
 * [WCXRP00000586] [MT6620 Wi-Fi][FW] Modify for blocking absence request right after connected
 * revise for CTIA power mode setting
 *
 * 03 22 2011 george.huang
 * [WCXRP00000504] [MT6620 Wi-Fi][FW] Support Sigma CAPI for power saving related command
 * link with supplicant commands
 *
 * 03 17 2011 chinglan.wang
 * [WCXRP00000570] [MT6620 Wi-Fi][Driver] Add Wi-Fi Protected Setup v2.0 feature
 * .
 *
 * 03 17 2011 yarco.yang
 * [WCXRP00000569] [MT6620 Wi-Fi][F/W][Driver] Set multicast address support current network usage
 * .
 *
 * 03 15 2011 george.huang
 * [WCXRP00000557] [MT6620 Wi-Fi] Support current consumption test mode commands
 * Support current consumption measurement mode command
 *
 * 03 15 2011 eddie.chen
 * [WCXRP00000554] [MT6620 Wi-Fi][DRV] Add sw control debug counter
 * Add sw debug counter for QM.
 *
 * 03 10 2011 cp.wu
 * [WCXRP00000532] [MT6620 Wi-Fi][Driver] Migrate NVRAM configuration procedures from MT6620 E2 to MT6620 E3
 * deprecate configuration used by MT6620 E2
 *
 * 03 07 2011 terry.wu
 * [WCXRP00000521] [MT6620 Wi-Fi][Driver] Remove non-standard debug message
 * Toggle non-standard debug messages to comments.
 *
 * 03 04 2011 cp.wu
 * [WCXRP00000515] [MT6620 Wi-Fi][Driver] Surpress compiler warning which is identified by GNU compiler collection
 * surpress compile warning occured when compiled by GNU compiler collection.
 *
 * 03 03 2011 wh.su
 * [WCXRP00000510] [MT6620 Wi-Fi] [Driver] Fixed the CTIA enter test mode issue
 * fixed the enter ctia test mode issue.
 *
 * 03 02 2011 george.huang
 * [WCXRP00000504] [MT6620 Wi-Fi][FW] Support Sigma CAPI for power saving related command
 * Update sigma CAPI for U-APSD setting
 *
 * 03 02 2011 george.huang
 * [WCXRP00000504] [MT6620 Wi-Fi][FW] Support Sigma CAPI for power saving related command
 * Support UAPSD/OppPS/NoA parameter setting
 *
 * 03 02 2011 cp.wu
 * [WCXRP00000503] [MT6620 Wi-Fi][Driver] Take RCPI brought by association response as initial RSSI right after connection is built.
 * use RCPI brought by ASSOC-RESP after connection is built as initial RCPI to avoid using a uninitialized MAC-RX RCPI.
 *
 * 01 27 2011 george.huang
 * [WCXRP00000400] [MT6620 Wi-Fi] support CTIA power mode setting
 * Support CTIA power mode setting.
 *
 * 01 26 2011 wh.su
 * [WCXRP00000396] [MT6620 Wi-Fi][Driver] Support Sw Ctrl ioctl at linux
 * adding the SW cmd ioctl support, use set/get structure ioctl.
 *
 * 01 25 2011 cp.wu
 * [WCXRP00000394] [MT6620 Wi-Fi][Driver] Count space needed for generating error message in scanning list into buffer size checking
 * when doing size prechecking, check illegal MAC address as well
 *
 * 01 20 2011 eddie.chen
 * [WCXRP00000374] [MT6620 Wi-Fi][DRV] SW debug control
 * Add Oid for sw control debug command
 *
 * 01 15 2011 puff.wen
 * NULL
 * Add Stress test
 *
 * 01 12 2011 cp.wu
 * [WCXRP00000358] [MT6620 Wi-Fi][Driver] Provide concurrent information for each module
 * check if allow to switch to IBSS mode via concurrent module before setting to IBSS mode
 *
 * 01 12 2011 cm.chang
 * [WCXRP00000354] [MT6620 Wi-Fi][Driver][FW] Follow NVRAM bandwidth setting
 * User-defined bandwidth is for 2.4G and 5G individually
 *
 * 01 04 2011 cp.wu
 * [WCXRP00000342] [MT6620 Wi-Fi][Driver] show error code in scanning list when MAC address is not correctly configured in NVRAM
 * show error code 0x10 when MAC address in NVRAM is not configured correctly.
 *
 * 01 04 2011 cp.wu
 * [WCXRP00000338] [MT6620 Wi-Fi][Driver] Separate kalMemAlloc into kmalloc and vmalloc implementations to ease physically continous memory demands
 * separate kalMemAlloc() into virtually-continous and physically-continous type to ease slab system pressure
 *
 * 12 28 2010 george.huang
 * [WCXRP00000232] [MT5931 Wi-Fi][FW] Modifications for updated HW power on sequence and related design
 * support WMM-PS U-APSD AC assignment.
 *
 * 12 28 2010 cp.wu
 * [WCXRP00000269] [MT6620 Wi-Fi][Driver][Firmware] Prepare for v1.1 branch release
 * report EEPROM used flag via NIC_CAPABILITY
 *
 * 12 28 2010 cp.wu
 * [WCXRP00000269] [MT6620 Wi-Fi][Driver][Firmware] Prepare for v1.1 branch release
 * integrate with 'EEPROM used' flag for reporting correct capability to Engineer Mode/META and other tools
 *
 * 12 16 2010 cp.wu
 * [WCXRP00000268] [MT6620 Wi-Fi][Driver] correction for WHQL failed items
 * correction for OID_802_11_NETWORK_TYPES_SUPPORTED handlers
 *
 * 12 13 2010 cp.wu
 * [WCXRP00000256] [MT6620 Wi-Fi][Driver] Eliminate potential issues which is identified by Klockwork
 * suppress warning reported by Klockwork.
 *
 * 12 07 2010 cm.chang
 * [WCXRP00000239] MT6620 Wi-Fi][Driver][FW] Merge concurrent branch back to maintrunk
 * 1. BSSINFO include RLM parameter
 * 2. free all sta records when network is disconnected
 *
 * 12 07 2010 cm.chang
 * [WCXRP00000238] MT6620 Wi-Fi][Driver][FW] Support regulation domain setting from NVRAM and supplicant
 * 1. Country code is from NVRAM or supplicant
 * 2. Change band definition in CMD/EVENT.
 *
 * 11 30 2010 cp.wu
 * [WCXRP00000213] [MT6620 Wi-Fi][Driver] Implement scanning with specified SSID for wpa_supplicant with ap_scan=1
 * .
 *
 * 11 26 2010 cp.wu
 * [WCXRP00000209] [MT6620 Wi-Fi][Driver] Modify NVRAM checking mechanism to warning only with necessary data field checking
 * 1. NVRAM error is now treated as warning only, thus normal operation is still available but extra scan result used to indicate user is attached
 * 2. DPD and TX-PWR are needed fields from now on, if these 2 fields are not availble then warning message is shown
 *
 * 11 25 2010 cp.wu
 * [WCXRP00000208] [MT6620 Wi-Fi][Driver] Add scanning with specified SSID to AIS FSM
 * add scanning with specified SSID facility to AIS-FSM
 *
 * 11 21 2010 wh.su
 * [WCXRP00000192] [MT6620 Wi-Fi][Driver] Fixed fail trying to build connection with Security AP while enable WAPI message check
 * Not set the wapi mode while the wapi assoc info set non-wapi ie.
 *
 * 11 05 2010 wh.su
 * [WCXRP00000165] [MT6620 Wi-Fi] [Pre-authentication] Assoc req rsn ie use wrong pmkid value
 * fixed the.pmkid value mismatch issue
 *
 * 11 01 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check[WCXRP00000150] [MT6620 Wi-Fi][Driver] Add implementation for querying current TX rate from firmware auto rate module
 * 1) Query link speed (TX rate) from firmware directly with buffering mechanism to reduce overhead
 * 2) Remove CNM CH-RECOVER event handling
 * 3) cfg read/write API renamed with kal prefix for unified naming rules.
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
 * 10 22 2010 cp.wu
 * [WCXRP00000122] [MT6620 Wi-Fi][Driver] Preparation for YuSu source tree integration
 * dos2unix conversion.
 *
 * 10 20 2010 cp.wu
 * [WCXRP00000117] [MT6620 Wi-Fi][Driver] Add logic for suspending driver when MT6620 is not responding anymore
 * use OID_CUSTOM_TEST_MODE as indication for driver reset
 * by dropping pending TX packets
 *
 * 10 18 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check[WCXRP00000086] [MT6620 Wi-Fi][Driver] The mac address is all zero at android
 * complete implementation of Android NVRAM access
 *
 * 10 06 2010 yuche.tsai
 * NULL
 * Update SLT 5G Test Channel Set.
 *
 * 10 06 2010 cp.wu
 * [WCXRP00000052] [MT6620 Wi-Fi][Driver] Eliminate Linux Compile Warning
 * code reorganization to improve isolation between GLUE and CORE layers.
 *
 * 10 06 2010 yuche.tsai
 * NULL
 * Update For SLT 5G Test Channel Selection Rule.
 *
 * 10 05 2010 cp.wu
 * [WCXRP00000075] [MT6620 Wi-Fi][Driver] Fill query buffer for OID_802_11_BSSID_LIST in 4-bytes aligned form
 * Query buffer size needs to be enlarged due to result is filled in 4-bytes alignment boundary
 *
 * 10 05 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check
 * 1) add NVRAM access API
 * 2) fake scanning result when NVRAM doesn't exist and/or version mismatch. (off by compiler option)
 * 3) add OID implementation for NVRAM read/write service
 *
 * 10 04 2010 cp.wu
 * [WCXRP00000077] [MT6620 Wi-Fi][Driver][FW] Eliminate use of ENUM_NETWORK_TYPE_T and replaced by ENUM_NETWORK_TYPE_INDEX_T only
 * remove ENUM_NETWORK_TYPE_T definitions
 *
 * 10 04 2010 cp.wu
 * [WCXRP00000075] [MT6620 Wi-Fi][Driver] Fill query buffer for OID_802_11_BSSID_LIST in 4-bytes aligned form
 * Extend result length to multiples of 4-bytes
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
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check
 * add skeleton for NVRAM integration
 *
 * 09 08 2010 cp.wu
 * NULL
 * use static memory pool for storing IEs of scanning result.
 *
 * 09 07 2010 yuche.tsai
 * NULL
 * Update SLT due to API change of SCAN module.
 *
 * 09 06 2010 cp.wu
 * NULL
 * Androi/Linux: return current operating channel information
 *
 * 09 06 2010 cp.wu
 * NULL
 * 1) initialize for correct parameter even for disassociation.
 * 2) AIS-FSM should have a limit on trials to build connection
 *
 * 09 03 2010 yuche.tsai
 * NULL
 * Refine SLT IO control handler.
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 09 01 2010 wh.su
 * NULL
 * adding the wapi support for integration test.
 *
 * 08 30 2010 chinglan.wang
 * NULL
 * Modify the rescan condition.
 *
 * 08 29 2010 yuche.tsai
 * NULL
 * Finish SLT TX/RX & Rate Changing Support.
 *
 * 08 27 2010 chinglan.wang
 * NULL
 * Update configuration for MT6620_E1_PRE_ALPHA_1832_0827_2010
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
 * 08 16 2010 george.huang
 * NULL
 * .
 *
 * 08 16 2010 george.huang
 * NULL
 * upate params defined in CMD_SET_NETWORK_ADDRESS_LIST
 *
 * 08 04 2010 cp.wu
 * NULL
 * fix for check build WHQL testing:
 * 1) do not assert query buffer if indicated buffer length is zero
 * 2) sdio.c has bugs which cause freeing same pointer twice
 *
 * 08 04 2010 cp.wu
 * NULL
 * revert changelist #15371, efuse read/write access will be done by RF test approach
 *
 * 08 04 2010 cp.wu
 * NULL
 * add OID definitions for EFUSE read/write access.
 *
 * 08 04 2010 george.huang
 * NULL
 * handle change PS mode OID/ CMD
 *
 * 08 04 2010 cp.wu
 * NULL
 * add an extra parameter to rftestQueryATInfo 'cause it's necessary to pass u4FuncData for query request.
 *
 * 08 04 2010 cp.wu
 * NULL
 * bypass u4FuncData for RF-Test query request as well.
 *
 * 08 04 2010 yarco.yang
 * NULL
 * Add TX_AMPDU and ADDBA_REJECT command
 *
 * 08 03 2010 cp.wu
 * NULL
 * surpress compilation warning.
 *
 * 08 02 2010 george.huang
 * NULL
 * add WMM-PS test related OID/ CMD handlers
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
 * 07 26 2010 cp.wu
 *
 * re-commit code logic being overwriten.
 *
 * 07 24 2010 wh.su
 *
 * .support the Wi-Fi RSN
 *
 * 07 21 2010 cp.wu
 *
 * 1) change BG_SCAN to ONLINE_SCAN for consistent term
 * 2) only clear scanning result when scan is permitted to do
 *
 * 07 20 2010 cp.wu
 *
 * 1) [AIS] when new scan is issued, clear currently available scanning result except the connected one
 * 2) refine disconnection behaviour when issued during BG-SCAN process
 *
 * 07 19 2010 wh.su
 *
 * modify the auth and encry status variable.
 *
 * 07 16 2010 cp.wu
 *
 * remove work-around in case SCN is not available.
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 07 05 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) change fake BSS_DESC from channel 6 to channel 1 due to channel switching is not done yet.
 * 2) after MAC address is queried from firmware, all related variables in driver domain should be updated as well
 *
 * 07 01 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * AIS-FSM integration with CNM channel request messages
 *
 * 07 01 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * implementation of DRV-SCN and related mailbox message handling.
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
 * 06 25 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add API in que_mgt to retrieve sta-rec index for security frames.
 *
 * 06 24 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 802.1x and bluetooth-over-Wi-Fi security frames are now delievered to firmware via command path instead of data path.
 *
 * 06 23 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) add SCN compilation option.
 * 2) when SCN is not turned on, BSSID_SCAN will generate a fake entry for 1st connection
 *
 * 06 23 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * implement SCAN-REQUEST oid as mailbox message dispatching.
 *
 * 06 23 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * integrate .
 *
 * 06 22 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) add command warpper for STA-REC/BSS-INFO sync.
 * 2) enhance command packet sending procedure for non-oid part
 * 3) add command packet definitions for STA-REC/BSS-INFO sync.
 *
 * 06 21 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * remove duplicate variable for migration.
 *
 * 06 21 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * adding the compiling flag for oid pmkid.
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * enable RX management frame handling.
 *
 * 06 18 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * migration the security related function from firmware.
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
 * merge wlan_def.h.
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * merge wifi_var.h, precomp.h, cnm_timer.h (data type only)
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 06 03 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * move timer callback to glue layer.
 *
 * 05 28 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * simplify cmd packet sending for RF test and MCR access OIDs
 *
 * 05 27 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * disable radio even when STA is not associated.
 *
 * 05 27 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * correct 2 OID behaviour to meet WHQL requirement.
 *
 * 05 26 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * 1) Modify set mac address code
 * 2) remove power managment macro
 *
 * 05 25 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * correct BSSID_LIST oid when radio if turned off.
 *
 * 05 24 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) when acquiring LP-own, write for clr-own with lower frequency compared to read poll
 * 2) correct address list parsing
 *
 * 05 24 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * disable wlanoidSetNetworkAddress() temporally.
 *
 * 05 22 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * some OIDs should be DRIVER_CORE instead of GLUE_EXTENSION
 *
 * 05 22 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) disable NETWORK_LAYER_ADDRESSES handling temporally.
 * 2) finish statistics OIDs
 *
 * 05 22 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * change OID behavior to meet WHQL requirement.
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
 * 05 18 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * implement Wakeup-on-LAN except firmware integration part
 *
 * 05 17 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * correct wlanoidSet802dot11PowerSaveProfile implementation.
 *
 * 05 17 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) enable CMD/EVENT ver 0.9 definition.
 * 2) abandon use of ENUM_MEDIA_STATE
 *
 * 05 17 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * correct OID_802_11_DISASSOCIATE handling.
 *
 * 05 17 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * 1) add timeout handler mechanism for pending command packets
 * 2) add p2p add/removal key
 *
 * 05 14 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * Add dissassocation support for wpa supplicant
 *
 * 05 14 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * correct return value.
 *
 * 05 13 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add NULL OID implementation for WOL-related OIDs.
 *
 * 05 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * for disassociation, still use parameter with current setting.
 *
 * 05 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * for disassociation, generate a WZC-compatible invalid SSID.
 *
 * 05 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * associate to illegal SSID when handling OID_802_11_DISASSOCIATE
 *
 * 04 27 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * reserve field of privacy filter and RTS threshold setting.
 *
 * 04 23 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * surpress compiler warning
 *
 * 04 23 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * .
 *
 * 04 22 2010 cp.wu
 * [WPD00003830]add OID_802_11_PRIVACY_FILTER support
 * enable RX filter OID
 *
 * 04 19 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * Add ioctl of power management
 *
 * 04 14 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * information buffer for query oid/ioctl is now buffered in prCmdInfo
 *  * instead of glue-layer variable to improve multiple oid/ioctl capability
 *
 * 04 13 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support
 * add framework for BT-over-Wi-Fi support.
 *  *  *  * 1) prPendingCmdInfo is replaced by queue for multiple handler capability
 *  *  *  * 2) command sequence number is now increased atomically
 *  *  *  * 3) private data could be hold and taken use for other purpose
 *
 * 04 12 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * correct OID_802_11_CONFIGURATION query for infrastructure mode.
 *
 * 04 09 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * 1) remove unused spin lock declaration
 *
 * 04 07 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * finish non-glue layer access to glue variables
 *
 * 04 07 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * rWlanInfo should be placed at adapter rather than glue due to most operations
 *  * are done in adapter layer.
 *
 * 04 07 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * (1)improve none-glue code portability
 * (2) disable set Multicast address during atomic context
 *
 * 04 07 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * eliminate direct access to prGlueInfo->eParamMediaStateIndicated from non-glue layer
 *
 * 04 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * ePowerCtrl is not necessary as a glue variable.
 *
 * 04 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * eliminate direct access to prGlueInfo->rWlanInfo.eLinkAttr.ucMediaStreamMode from non-glue layer.
 *
 * 04 06 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * improve none-glue code portability
 *
 * 04 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * code refine: fgTestMode should be at adapter rather than glue due to the device/fw is also involved
 *
 * 04 01 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * .
 *
 * 03 31 2010 wh.su
 * [WPD00003816][MT6620 Wi-Fi] Adding the security support
 * modify the wapi related code for new driver's design.
 *
 * 03 30 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * statistics information OIDs are now handled by querying from firmware domain
 *
 * 03 28 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * improve glue code portability
 *
 * 03 26 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * indicate media stream mode after set is done
 *
 * 03 26 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add a temporary flag for integration with CMD/EVENT v0.9.
 *
 * 03 25 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) correct OID_802_11_CONFIGURATION with frequency setting behavior.
 * the frequency is used for adhoc connection only
 * 2) update with SD1 v0.9 CMD/EVENT documentation
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
 * 03 24 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * generate information for OID_GEN_RCV_OK & OID_GEN_XMIT_OK
 *
 *
 * 03 22 2010 cp.wu
 * [WPD00003824][MT6620 Wi-Fi][New Feature] Add support of large scan list
 * Implement feature needed by CR: WPD00003824: refining association command by pasting scanning result
 *
 * 03 19 2010 wh.su
 * [WPD00003820][MT6620 Wi-Fi] Modify the code for meet the WHQL test
 * adding the check for pass WHQL test item.
 *
 * 03 19 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) add ACPI D0/D3 state switching support
 *  * 2) use more formal way to handle interrupt when the status is retrieved from enhanced RX response
 *
* 03 16 2010 wh.su
 * [WPD00003820][MT6620 Wi-Fi] Modify the code for meet the WHQL test
 * fixed some whql pre-test fail case.
 *
 * 03 03 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * implement custom OID: EEPROM read/write access
 *
 * 03 03 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * implement OID_802_3_MULTICAST_LIST oid handling
 *
 * 03 02 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) the use of prPendingOid revised, all accessing are now protected by spin lock
 *  * 2) ensure wlanReleasePendingOid will clear all command queues
 *
 * 02 25 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * send CMD_ID_INFRASTRUCTURE when handling OID_802_11_INFRASTRUCTURE_MODE set.
 *
 * 02 24 2010 wh.su
 * [WPD00003820][MT6620 Wi-Fi] Modify the code for meet the WHQL test
 * Don't needed to check the auth mode, WHQL testing not specific at auth wpa2.
 *
 * 02 23 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * do not check SSID validity anymore.
 *
 * 02 23 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add checksum offloading support.
 *
 * 02 09 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1. Permanent and current MAC address are now retrieved by CMD/EVENT packets instead of hard-coded address
 *  * 2. follow MSDN defined behavior when associates to another AP
 *  * 3. for firmware download, packet size could be up to 2048 bytes
 *
 * 02 09 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * move ucCmdSeqNum as instance variable
 *
 * 02 04 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * when OID_CUSTOM_OID_INTERFACE_VERSION is queried, do modify connection states
 *
 * 01 27 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) implement timeout mechanism when OID is pending for longer than 1 second
 *  * 2) allow OID_802_11_CONFIGURATION to be executed when RF test mode is turned on
 *
 * 01 27 2010 wh.su
 * [WPD00003816][MT6620 Wi-Fi] Adding the security support
 * .
 *
 * 01 27 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1. eliminate improper variable in rHifInfo
 *  * 2. block TX/ordinary OID when RF test mode is engaged
 *  * 3. wait until firmware finish operation when entering into and leaving from RF test mode
 *  * 4. correct some HAL implementation
 *
 * 01 22 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * implement following 802.11 OIDs:
 * OID_802_11_RSSI,
 * OID_802_11_RSSI_TRIGGER,
 * OID_802_11_STATISTICS,
 * OID_802_11_DISASSOCIATE,
 * OID_802_11_POWER_MODE
 *
 * 01 21 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * implement OID_802_11_MEDIA_STREAM_MODE
 *
 * 01 21 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * implement OID_802_11_SUPPORTED_RATES / OID_802_11_DESIRED_RATES
 *
 * 01 21 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * do not fill ucJoinOnly currently
 *
 * 01 14 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * enable to connect to ad-hoc network
 *
 * 01 07 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * .implement Set/Query BeaconInterval/AtimWindow
 *
 * 01 07 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * .Set/Get AT Info is not blocked even when driver is not in fg test mode
 *
 * 12 30 2009 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) According to CMD/EVENT documentation v0.8,
 * OID_CUSTOM_TEST_RX_STATUS & OID_CUSTOM_TEST_TX_STATUS is no longer used,
 * and result is retrieved by get ATInfo instead
 * 2) add 4 counter for recording aggregation statistics
 *
 * 12 28 2009 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * eliminate redundant variables for connection_state
**  \main\maintrunk.MT6620WiFiDriver_Prj\32 2009-12-16 22:13:36 GMT mtk02752
**  change hard-coded MAC address to match with FW (temporally)
**  \main\maintrunk.MT6620WiFiDriver_Prj\31 2009-12-10 16:49:50 GMT mtk02752
**  code clean
**  \main\maintrunk.MT6620WiFiDriver_Prj\30 2009-12-08 17:38:49 GMT mtk02752
**  + add OID for RF test
**  * MCR RD/WR are modified to match with cmd/event definition
**  \main\maintrunk.MT6620WiFiDriver_Prj\29 2009-12-08 11:32:20 GMT mtk02752
**  add skeleton for RF test implementation
**  \main\maintrunk.MT6620WiFiDriver_Prj\28 2009-12-03 16:43:24 GMT mtk01461
**  Modify query SCAN list oid by adding prEventScanResult
**
**  \main\maintrunk.MT6620WiFiDriver_Prj\27 2009-12-03 16:39:27 GMT mtk01461
**  Sync CMD data structure in set ssid oid
**  \main\maintrunk.MT6620WiFiDriver_Prj\26 2009-12-03 16:28:22 GMT mtk01461
**  Add invalid check of set SSID oid and fix query scan list oid
**  \main\maintrunk.MT6620WiFiDriver_Prj\25 2009-11-30 17:33:08 GMT mtk02752
**  implement wlanoidSetInfrastructureMode/wlanoidQueryInfrastructureMode
**  \main\maintrunk.MT6620WiFiDriver_Prj\24 2009-11-30 10:53:49 GMT mtk02752
**  1st DW of WIFI_CMD_T is shared with HIF_TX_HEADER_T
**  \main\maintrunk.MT6620WiFiDriver_Prj\23 2009-11-30 09:22:48 GMT mtk02752
**  correct wifi cmd length mismatch
**  \main\maintrunk.MT6620WiFiDriver_Prj\22 2009-11-25 21:34:33 GMT mtk02752
**  sync EVENT_SCAN_RESULT_T with firmware
**  \main\maintrunk.MT6620WiFiDriver_Prj\21 2009-11-25 21:03:27 GMT mtk02752
**  implement wlanoidQueryBssidList()
**  \main\maintrunk.MT6620WiFiDriver_Prj\20 2009-11-25 18:17:17 GMT mtk02752
**  refine GL_WLAN_INFO_T for buffering scan result
**  \main\maintrunk.MT6620WiFiDriver_Prj\19 2009-11-23 20:28:51 GMT mtk02752
**  some OID will be set to WLAN_STATUS_PENDING until it is sent via wlanSendCommand()
**  \main\maintrunk.MT6620WiFiDriver_Prj\18 2009-11-23 17:56:36 GMT mtk02752
**  implement wlanoidSetBssidListScan(), wlanoidSetBssid() and wlanoidSetSsid()
**
**  \main\maintrunk.MT6620WiFiDriver_Prj\17 2009-11-13 17:20:53 GMT mtk02752
**  add Set BSSID/SSID path but disabled temporally due to FW is not ready yet
**  \main\maintrunk.MT6620WiFiDriver_Prj\16 2009-11-13 12:28:58 GMT mtk02752
**  add wlanoidSetBssidListScan -> cmd_info path
**  \main\maintrunk.MT6620WiFiDriver_Prj\15 2009-11-09 22:48:07 GMT mtk01084
**  modify test cases entry
**  \main\maintrunk.MT6620WiFiDriver_Prj\14 2009-11-04 14:10:58 GMT mtk01084
**  add new test interfaces
**  \main\maintrunk.MT6620WiFiDriver_Prj\13 2009-10-30 18:17:10 GMT mtk01084
**  fix compiler warning
**  \main\maintrunk.MT6620WiFiDriver_Prj\12 2009-10-29 19:46:26 GMT mtk01084
**  add test functions
**  \main\maintrunk.MT6620WiFiDriver_Prj\11 2009-10-23 16:07:56 GMT mtk01084
**  include new file
**  \main\maintrunk.MT6620WiFiDriver_Prj\10 2009-10-13 21:58:29 GMT mtk01084
**  modify for new HW architecture
**  \main\maintrunk.MT6620WiFiDriver_Prj\9 2009-10-02 13:48:49 GMT mtk01725
**  \main\maintrunk.MT6620WiFiDriver_Prj\8 2009-09-09 17:26:04 GMT mtk01084
**  \main\maintrunk.MT6620WiFiDriver_Prj\7 2009-04-21 12:09:50 GMT mtk01461
**  Update for MCR Write OID
**  \main\maintrunk.MT6620WiFiDriver_Prj\6 2009-04-21 09:35:18 GMT mtk01461
**  Update wlanoidQueryMcrRead() for composing CMD_INFO_T
**  \main\maintrunk.MT6620WiFiDriver_Prj\5 2009-04-17 18:09:51 GMT mtk01426
**  Remove kalIndicateStatusAndComplete() in wlanoidQueryOidInterfaceVersion()
**  \main\maintrunk.MT6620WiFiDriver_Prj\4 2009-04-14 15:51:50 GMT mtk01426
**  Add MCR read/write support
**  \main\maintrunk.MT6620WiFiDriver_Prj\3 2009-03-19 18:32:40 GMT mtk01084
**  update for basic power management functions
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-03-10 20:06:31 GMT mtk01426
**  Init for develop
**
*/

/******************************************************************************
*                         C O M P I L E R   F L A G S
*******************************************************************************
*/

/******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
*******************************************************************************
*/
#include "precomp.h"
#include "mgmt/rsn.h"

#include <stddef.h>

/******************************************************************************
*                              C O N S T A N T S
*******************************************************************************
*/

/******************************************************************************
*                             D A T A   T Y P E S
*******************************************************************************
*/

/******************************************************************************
*                            P U B L I C   D A T A
*******************************************************************************
*/
#if DBG
extern UINT_8  aucDebugModule[DBG_MODULE_NUM];
extern UINT_32 u4DebugModule;
UINT_32 u4DebugModuleTemp;
#endif /* DBG */

/******************************************************************************
*                           P R I V A T E   D A T A
*******************************************************************************
*/

/******************************************************************************
*                                 M A C R O S
*******************************************************************************
*/

/******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
*******************************************************************************
*/
extern int sprintf(char * buf, const char * fmt, ...);

/******************************************************************************
*                              F U N C T I O N S
*******************************************************************************
*/
#if CFG_ENABLE_STATISTICS_BUFFERING
static BOOLEAN
IsBufferedStatisticsUsable(
    P_ADAPTER_T prAdapter)
{
    ASSERT(prAdapter);

    if(prAdapter->fgIsStatValid == TRUE &&
            (kalGetTimeTick() - prAdapter->rStatUpdateTime) <= CFG_STATISTICS_VALID_CYCLE)
        return TRUE;
    else
        return FALSE;
}
#endif


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the supported physical layer network
*        type that can be used by the driver.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuffer A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufferLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryNetworkTypesSupported (
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    )
{
    UINT_32 u4NumItem = 0;
    ENUM_PARAM_NETWORK_TYPE_T eSupportedNetworks[PARAM_NETWORK_TYPE_NUM];
    PPARAM_NETWORK_TYPE_LIST prSupported;

    /* The array of all physical layer network subtypes that the driver supports. */

    DEBUGFUNC("wlanoidQueryNetworkTypesSupported");

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    /* Init. */
    for (u4NumItem = 0; u4NumItem < PARAM_NETWORK_TYPE_NUM ; u4NumItem++) {
        eSupportedNetworks[u4NumItem] = 0;
    }

    u4NumItem = 0;

    eSupportedNetworks[u4NumItem] = PARAM_NETWORK_TYPE_DS;
    u4NumItem ++;

    eSupportedNetworks[u4NumItem] = PARAM_NETWORK_TYPE_OFDM24;
    u4NumItem ++;

    *pu4QueryInfoLen =
        (UINT_32)OFFSET_OF(PARAM_NETWORK_TYPE_LIST, eNetworkType) +
        (u4NumItem * sizeof(ENUM_PARAM_NETWORK_TYPE_T));

    if (u4QueryBufferLen < *pu4QueryInfoLen) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    prSupported = (PPARAM_NETWORK_TYPE_LIST)pvQueryBuffer;
    prSupported->NumberOfItems = u4NumItem;
    kalMemCopy(prSupported->eNetworkType,
        eSupportedNetworks,
        u4NumItem * sizeof(ENUM_PARAM_NETWORK_TYPE_T));

    DBGLOG(REQ, TRACE, ("NDIS supported network type list: %ld\n",
        prSupported->NumberOfItems));
    DBGLOG_MEM8(REQ, INFO, prSupported, *pu4QueryInfoLen);

    return WLAN_STATUS_SUCCESS;
} /* wlanoidQueryNetworkTypesSupported */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the current physical layer network
*        type used by the driver.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuffer A pointer to the buffer that holds the result of
*             the query.
* \param[in] u4QueryBufferLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                             bytes written into the query buffer. If the
*                             call failed due to invalid length of the query
*                             buffer, returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_BUFFER_TOO_SHORT
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryNetworkTypeInUse (
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    )
{
    // TODO: need to check the OID handler content again!!

    ENUM_PARAM_NETWORK_TYPE_T rCurrentNetworkTypeInUse = PARAM_NETWORK_TYPE_OFDM24;

    DEBUGFUNC("wlanoidQueryNetworkTypeInUse");

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    if (u4QueryBufferLen < sizeof(ENUM_PARAM_NETWORK_TYPE_T)) {
        *pu4QueryInfoLen = sizeof(ENUM_PARAM_NETWORK_TYPE_T);
        return WLAN_STATUS_BUFFER_TOO_SHORT;
    }


    if (kalGetMediaStateIndicated(prAdapter->prGlueInfo) == PARAM_MEDIA_STATE_CONNECTED) {
        rCurrentNetworkTypeInUse =
            (ENUM_PARAM_NETWORK_TYPE_T)(prAdapter->rWlanInfo.ucNetworkType);
    }
    else {
        rCurrentNetworkTypeInUse =
            (ENUM_PARAM_NETWORK_TYPE_T)(prAdapter->rWlanInfo.ucNetworkTypeInUse);
    }

    *(P_ENUM_PARAM_NETWORK_TYPE_T)pvQueryBuffer = rCurrentNetworkTypeInUse;
    *pu4QueryInfoLen = sizeof(ENUM_PARAM_NETWORK_TYPE_T);

    DBGLOG(REQ, TRACE, ("Network type in use: %d\n", rCurrentNetworkTypeInUse));

    return WLAN_STATUS_SUCCESS;
} /* wlanoidQueryNetworkTypeInUse */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set the physical layer network type used
*        by the driver.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns the
*                          amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS The given network type is supported and accepted.
* \retval WLAN_STATUS_INVALID_DATA The given network type is not in the
*                                  supported list.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetNetworkTypeInUse (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    // TODO: need to check the OID handler content again!!

    ENUM_PARAM_NETWORK_TYPE_T eNewNetworkType;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    DEBUGFUNC("wlanoidSetNetworkTypeInUse");

    ASSERT(prAdapter);
    ASSERT(pvSetBuffer);
    ASSERT(pu4SetInfoLen);

    if (u4SetBufferLen < sizeof(ENUM_PARAM_NETWORK_TYPE_T)) {
        *pu4SetInfoLen = sizeof(ENUM_PARAM_NETWORK_TYPE_T);
        return WLAN_STATUS_INVALID_LENGTH;
    }

    eNewNetworkType = *(P_ENUM_PARAM_NETWORK_TYPE_T)pvSetBuffer;
    *pu4SetInfoLen = sizeof(ENUM_PARAM_NETWORK_TYPE_T);

    DBGLOG(REQ,
        INFO,
        ("New network type: %d mode\n", eNewNetworkType));

    switch (eNewNetworkType) {

    case PARAM_NETWORK_TYPE_DS:
        prAdapter->rWlanInfo.ucNetworkTypeInUse = (UINT_8) PARAM_NETWORK_TYPE_DS;
        break;

    case PARAM_NETWORK_TYPE_OFDM5:
        prAdapter->rWlanInfo.ucNetworkTypeInUse = (UINT_8) PARAM_NETWORK_TYPE_OFDM5;
        break;

    case PARAM_NETWORK_TYPE_OFDM24:
        prAdapter->rWlanInfo.ucNetworkTypeInUse = (UINT_8) PARAM_NETWORK_TYPE_OFDM24;
        break;

    case PARAM_NETWORK_TYPE_AUTOMODE:
        prAdapter->rWlanInfo.ucNetworkTypeInUse = (UINT_8) PARAM_NETWORK_TYPE_AUTOMODE;
        break;

    case PARAM_NETWORK_TYPE_FH:
        DBGLOG(REQ, INFO, ("Not support network type: %d\n", eNewNetworkType));
        rStatus = WLAN_STATUS_NOT_SUPPORTED;
        break;

    default:
        DBGLOG(REQ, INFO, ("Unknown network type: %d\n", eNewNetworkType));
        rStatus = WLAN_STATUS_INVALID_DATA;
        break;
    }

    /* Verify if we support the new network type. */
    if (rStatus != WLAN_STATUS_SUCCESS) {
        DBGLOG(REQ, WARN, ("Unknown network type: %d\n", eNewNetworkType));
    }

    return rStatus;
} /* wlanoidSetNetworkTypeInUse */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the current BSSID.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuffer A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufferLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                             bytes written into the query buffer. If the call
*                             failed due to invalid length of the query buffer,
*                             returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_ADAPTER_NOT_READY
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryBssid (
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    )
{
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;

    DEBUGFUNC("wlanoidQueryBssid");

    ASSERT(prAdapter);

    if (u4QueryBufferLen < MAC_ADDR_LEN) {
        ASSERT(pu4QueryInfoLen);
        *pu4QueryInfoLen = MAC_ADDR_LEN;
        return WLAN_STATUS_BUFFER_TOO_SHORT;
    }

    ASSERT(u4QueryBufferLen >= MAC_ADDR_LEN);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }
    ASSERT(pu4QueryInfoLen);

    if(kalGetMediaStateIndicated(prAdapter->prGlueInfo) == PARAM_MEDIA_STATE_CONNECTED) {
        kalMemCopy(pvQueryBuffer, prAdapter->rWlanInfo.rCurrBssId.arMacAddress, MAC_ADDR_LEN);
    }
    else if(prAdapter->rWifiVar.rConnSettings.eOPMode == NET_TYPE_IBSS) {
        PARAM_MAC_ADDRESS aucTemp;         /*!< BSSID */
        COPY_MAC_ADDR(aucTemp, prAdapter->rWlanInfo.rCurrBssId.arMacAddress);
        aucTemp[0] &= ~BIT(0);
        aucTemp[1] |= BIT(1);
        COPY_MAC_ADDR(pvQueryBuffer, aucTemp);
    }
    else {
        rStatus = WLAN_STATUS_ADAPTER_NOT_READY;
    }

    *pu4QueryInfoLen = MAC_ADDR_LEN;
    return rStatus;
} /* wlanoidQueryBssid */



/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the list of all BSSIDs detected by
*        the driver.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuffer A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufferLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                             bytes written into the query buffer. If the call
*                             failed due to invalid length of the query buffer,
*                             returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_ADAPTER_NOT_READY
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryBssidList (
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    )
{
    P_GLUE_INFO_T prGlueInfo;
    UINT_32 i, u4BssidListExLen;
    P_PARAM_BSSID_LIST_EX_T prList;
    P_PARAM_BSSID_EX_T prBssidEx;
    PUINT_8 cp;

    DEBUGFUNC("wlanoidQueryBssidList");

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);

    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);

        if(!pvQueryBuffer) {
            return WLAN_STATUS_INVALID_DATA;
        }
    }

    prGlueInfo = prAdapter->prGlueInfo;

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in qeury BSSID list! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }

    u4BssidListExLen = 0;

    if(prAdapter->fgIsRadioOff == FALSE) {
        for(i = 0 ; i < prAdapter->rWlanInfo.u4ScanResultNum ; i++) {
            u4BssidListExLen += ALIGN_4(prAdapter->rWlanInfo.arScanResult[i].u4Length);
        }
    }

    if(u4BssidListExLen) {
        u4BssidListExLen += 4; // u4NumberOfItems.
    }
    else {
        u4BssidListExLen = sizeof(PARAM_BSSID_LIST_EX_T);
    }

    *pu4QueryInfoLen = u4BssidListExLen;

    if (u4QueryBufferLen < *pu4QueryInfoLen) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    /* Clear the buffer */
    kalMemZero(pvQueryBuffer, u4BssidListExLen);

    prList = (P_PARAM_BSSID_LIST_EX_T) pvQueryBuffer;
    cp = (PUINT_8)&prList->arBssid[0];

    if(prAdapter->fgIsRadioOff == FALSE && prAdapter->rWlanInfo.u4ScanResultNum > 0) {
        // fill up for each entry
        for(i = 0 ; i < prAdapter->rWlanInfo.u4ScanResultNum ; i++) {
            prBssidEx = (P_PARAM_BSSID_EX_T)cp;

            // copy structure
            kalMemCopy(prBssidEx,
                    &(prAdapter->rWlanInfo.arScanResult[i]),
                    OFFSET_OF(PARAM_BSSID_EX_T, aucIEs));

            /*For WHQL test, Rssi should be in range -10 ~ -200 dBm*/
            if(prBssidEx->rRssi > PARAM_WHQL_RSSI_MAX_DBM) {
                prBssidEx->rRssi = PARAM_WHQL_RSSI_MAX_DBM;
            }

            if(prAdapter->rWlanInfo.arScanResult[i].u4IELength > 0) {
                // copy IEs
                kalMemCopy(prBssidEx->aucIEs,
                        prAdapter->rWlanInfo.apucScanResultIEs[i],
                        prAdapter->rWlanInfo.arScanResult[i].u4IELength);
            }

            // 4-bytes alignement
            prBssidEx->u4Length = ALIGN_4(prBssidEx->u4Length);

            cp += prBssidEx->u4Length;
            prList->u4NumberOfItems++;
        }
    }

    return WLAN_STATUS_SUCCESS;
} /* wlanoidQueryBssidList */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to request the driver to perform
*        scanning.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer Pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_ADAPTER_NOT_READY
* \retval WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetBssidListScan (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    P_PARAM_SSID_T prSsid;
    PARAM_SSID_T rSsid;

    DEBUGFUNC("wlanoidSetBssidListScan()");

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in set BSSID list scan! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }

    ASSERT(pu4SetInfoLen);
    *pu4SetInfoLen = 0;

    if (prAdapter->fgIsRadioOff) {
        DBGLOG(REQ, WARN, ("Return from BSSID list scan! (radio off). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        return WLAN_STATUS_SUCCESS;
    }

    if(pvSetBuffer != NULL && u4SetBufferLen != 0) {
        COPY_SSID(rSsid.aucSsid,
                rSsid.u4SsidLen,
                pvSetBuffer,
                u4SetBufferLen);
        prSsid = &rSsid;
    }
    else {
        prSsid = NULL;
    }

#if CFG_SUPPORT_RDD_TEST_MODE
    if (prAdapter->prGlueInfo->rRegInfo.u4RddTestMode) {
        if((prAdapter->fgEnOnlineScan == TRUE) && (prAdapter->ucRddStatus)){
            if (kalGetMediaStateIndicated(prAdapter->prGlueInfo) != PARAM_MEDIA_STATE_CONNECTED){
                aisFsmScanRequest(prAdapter, prSsid, NULL, 0);
            }
        }
    }
    else
#endif
    {
        if(prAdapter->fgEnOnlineScan == TRUE) {
            aisFsmScanRequest(prAdapter, prSsid, NULL, 0);
        }
        else if(kalGetMediaStateIndicated(prAdapter->prGlueInfo) != PARAM_MEDIA_STATE_CONNECTED) {
            aisFsmScanRequest(prAdapter, prSsid, NULL, 0);
        }
    }

    return WLAN_STATUS_SUCCESS;
} /* wlanoidSetBssidListScan */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to request the driver to perform
*        scanning with attaching information elements(IEs) specified from user space
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer Pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_ADAPTER_NOT_READY
* \retval WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetBssidListScanExt (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    P_PARAM_SCAN_REQUEST_EXT_T prScanRequest;
    P_PARAM_SSID_T prSsid;
    PUINT_8 pucIe;
    UINT_32 u4IeLength;

    DEBUGFUNC("wlanoidSetBssidListScanExt()");

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in set BSSID list scan! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }

    ASSERT(pu4SetInfoLen);
    *pu4SetInfoLen = 0;

    if(u4SetBufferLen != sizeof(PARAM_SCAN_REQUEST_EXT_T)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    if (prAdapter->fgIsRadioOff) {
        DBGLOG(REQ, WARN, ("Return from BSSID list scan! (radio off). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        return WLAN_STATUS_SUCCESS;
    }

    if(pvSetBuffer != NULL && u4SetBufferLen != 0) {
        prScanRequest = (P_PARAM_SCAN_REQUEST_EXT_T)pvSetBuffer;
        prSsid = &(prScanRequest->rSsid);
        pucIe = prScanRequest->pucIE;
        u4IeLength = prScanRequest->u4IELength;
    }
    else {
        prScanRequest = NULL;
        prSsid = NULL;
        pucIe = NULL;
        u4IeLength = 0;
    }

#if CFG_SUPPORT_RDD_TEST_MODE
    if (prAdapter->prGlueInfo->rRegInfo.u4RddTestMode) {
        if((prAdapter->fgEnOnlineScan == TRUE) && (prAdapter->ucRddStatus)){
            if (kalGetMediaStateIndicated(prAdapter->prGlueInfo) != PARAM_MEDIA_STATE_CONNECTED){
                aisFsmScanRequest(prAdapter, prSsid, pucIe, u4IeLength);
            }
        }
    }
    else
#endif
    {
        if(prAdapter->fgEnOnlineScan == TRUE) {
            aisFsmScanRequest(prAdapter, prSsid, pucIe, u4IeLength);
        }
        else if(kalGetMediaStateIndicated(prAdapter->prGlueInfo) != PARAM_MEDIA_STATE_CONNECTED) {
            aisFsmScanRequest(prAdapter, prSsid, pucIe, u4IeLength);
        }
    }

    return WLAN_STATUS_SUCCESS;
} /* wlanoidSetBssidListScanWithIE */



/*----------------------------------------------------------------------------*/
/*!
* \brief This routine will initiate the join procedure to attempt to associate
*        with the specified BSSID.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_INVALID_DATA
* \retval WLAN_STATUS_ADAPTER_NOT_READY
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetBssid (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    P_GLUE_INFO_T prGlueInfo;
    P_UINT_8 pAddr;
    UINT_32 i;
    INT_32 i4Idx = -1;
    P_MSG_AIS_ABORT_T prAisAbortMsg;

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = MAC_ADDR_LEN;;
    if (u4SetBufferLen != MAC_ADDR_LEN){
    *pu4SetInfoLen = MAC_ADDR_LEN;
    return WLAN_STATUS_INVALID_LENGTH;
    }
    else if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in set ssid! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }

    prGlueInfo = prAdapter->prGlueInfo;
    pAddr = (P_UINT_8)pvSetBuffer;

    // re-association check
    if(kalGetMediaStateIndicated(prGlueInfo) == PARAM_MEDIA_STATE_CONNECTED) {
        if(EQUAL_MAC_ADDR(prAdapter->rWlanInfo.rCurrBssId.arMacAddress, pAddr)) {
            kalSetMediaStateIndicated(prGlueInfo, PARAM_MEDIA_STATE_TO_BE_INDICATED);
        }
        else {
            kalIndicateStatusAndComplete(prGlueInfo,
                    WLAN_STATUS_MEDIA_DISCONNECT,
                    NULL,
                    0);
        }
    }

    // check if any scanned result matchs with the BSSID
    for(i = 0 ; i < prAdapter->rWlanInfo.u4ScanResultNum ; i++) {
        if(EQUAL_MAC_ADDR(prAdapter->rWlanInfo.arScanResult[i].arMacAddress, pAddr)) {
            i4Idx = (INT_32)i;
            break;
        }
    }

    /* prepare message to AIS */
    if (prAdapter->rWifiVar.rConnSettings.eOPMode == NET_TYPE_IBSS
            || prAdapter->rWifiVar.rConnSettings.eOPMode == NET_TYPE_DEDICATED_IBSS) {
        /* IBSS */ /* beacon period */
        prAdapter->rWifiVar.rConnSettings.u2BeaconPeriod    = prAdapter->rWlanInfo.u2BeaconPeriod;
        prAdapter->rWifiVar.rConnSettings.u2AtimWindow      = prAdapter->rWlanInfo.u2AtimWindow;
    }

    /* Set Connection Request Issued Flag */
    prAdapter->rWifiVar.rConnSettings.fgIsConnReqIssued = TRUE;
    prAdapter->rWifiVar.rConnSettings.eConnectionPolicy = CONNECT_BY_BSSID;

    /* Send AIS Abort Message */
    prAisAbortMsg = (P_MSG_AIS_ABORT_T)cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_AIS_ABORT_T));
    if (!prAisAbortMsg) {
        ASSERT(0);
        return WLAN_STATUS_FAILURE;
    }

    prAisAbortMsg->rMsgHdr.eMsgId = MID_OID_AIS_FSM_JOIN_REQ;
    prAisAbortMsg->ucReasonOfDisconnect = DISCONNECT_REASON_CODE_NEW_CONNECTION;

    if (EQUAL_MAC_ADDR(prAdapter->rWifiVar.rConnSettings.aucBSSID, pAddr)) {
        prAisAbortMsg->fgDelayIndication = TRUE;
    }
    else {
        /* Update the information to CONNECTION_SETTINGS_T */
        prAdapter->rWifiVar.rConnSettings.ucSSIDLen = 0;
        prAdapter->rWifiVar.rConnSettings.aucSSID[0] = '\0';

        COPY_MAC_ADDR(prAdapter->rWifiVar.rConnSettings.aucBSSID, pAddr);
        prAisAbortMsg->fgDelayIndication = FALSE;
    }

    mboxSendMsg(prAdapter,
            MBOX_ID_0,
            (P_MSG_HDR_T) prAisAbortMsg,
            MSG_SEND_METHOD_BUF);

    return WLAN_STATUS_SUCCESS;
} /* end of wlanoidSetBssid() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine will initiate the join procedure to attempt
*        to associate with the new SSID. If the previous scanning
*        result is aged, we will scan the channels at first.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer Pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_DATA
* \retval WLAN_STATUS_ADAPTER_NOT_READY
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetSsid (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    P_GLUE_INFO_T prGlueInfo;
    P_PARAM_SSID_T pParamSsid;
    UINT_32 i;
    INT_32 i4Idx = -1, i4MaxRSSI = INT_MIN;
    P_MSG_AIS_ABORT_T prAisAbortMsg;
    BOOLEAN fgIsValidSsid = TRUE;

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    /* MSDN:
     * Powering on the radio if the radio is powered off through a setting of OID_802_11_DISASSOCIATE
     */
    if(prAdapter->fgIsRadioOff == TRUE) {
        prAdapter->fgIsRadioOff = FALSE;
    }

    if(u4SetBufferLen < sizeof(PARAM_SSID_T) || u4SetBufferLen > sizeof(PARAM_SSID_T)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }
    else if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in set ssid! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }

    pParamSsid = (P_PARAM_SSID_T) pvSetBuffer;

    if (pParamSsid->u4SsidLen > 32) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    prGlueInfo = prAdapter->prGlueInfo;

    // prepare for CMD_BUILD_CONNECTION & CMD_GET_CONNECTION_STATUS
    // re-association check
    if(kalGetMediaStateIndicated(prGlueInfo) == PARAM_MEDIA_STATE_CONNECTED) {
        if(EQUAL_SSID(prAdapter->rWlanInfo.rCurrBssId.rSsid.aucSsid,
                    prAdapter->rWlanInfo.rCurrBssId.rSsid.u4SsidLen,
                    pParamSsid->aucSsid,
                    pParamSsid->u4SsidLen)) {
            kalSetMediaStateIndicated(prGlueInfo, PARAM_MEDIA_STATE_TO_BE_INDICATED);
        }
        else {
            kalIndicateStatusAndComplete(prGlueInfo,
                    WLAN_STATUS_MEDIA_DISCONNECT,
                    NULL,
                    0);
        }
    }

    // check if any scanned result matchs with the SSID
    for(i = 0 ; i < prAdapter->rWlanInfo.u4ScanResultNum ; i++) {
        PUINT_8 aucSsid = prAdapter->rWlanInfo.arScanResult[i].rSsid.aucSsid;
        UINT_8 ucSsidLength = (UINT_8) prAdapter->rWlanInfo.arScanResult[i].rSsid.u4SsidLen;
        INT_32 i4RSSI = prAdapter->rWlanInfo.arScanResult[i].rRssi;

        if(EQUAL_SSID(aucSsid, ucSsidLength, pParamSsid->aucSsid, pParamSsid->u4SsidLen) &&
                i4RSSI >= i4MaxRSSI) {
            i4Idx = (INT_32)i;
            i4MaxRSSI = i4RSSI;
        }
    }

    /* prepare message to AIS */
    if (prAdapter->rWifiVar.rConnSettings.eOPMode == NET_TYPE_IBSS
            || prAdapter->rWifiVar.rConnSettings.eOPMode == NET_TYPE_DEDICATED_IBSS) {
        /* IBSS */ /* beacon period */
        prAdapter->rWifiVar.rConnSettings.u2BeaconPeriod    = prAdapter->rWlanInfo.u2BeaconPeriod;
        prAdapter->rWifiVar.rConnSettings.u2AtimWindow      = prAdapter->rWlanInfo.u2AtimWindow;
    }

    if (prAdapter->rWifiVar.fgSupportWZCDisassociation) {
        if (pParamSsid->u4SsidLen == ELEM_MAX_LEN_SSID) {
            fgIsValidSsid = FALSE;

            for (i = 0; i < ELEM_MAX_LEN_SSID; i++) {
                if ( !((0 < pParamSsid->aucSsid[i]) && (pParamSsid->aucSsid[i] <= 0x1F)) ) {
                    fgIsValidSsid = TRUE;
                    break;
                }
            }
        }
    }

    /* Set Connection Request Issued Flag */
    if (fgIsValidSsid) {
        prAdapter->rWifiVar.rConnSettings.fgIsConnReqIssued = TRUE;

        if(pParamSsid->u4SsidLen) {
            prAdapter->rWifiVar.rConnSettings.eConnectionPolicy = CONNECT_BY_SSID_BEST_RSSI;
        }
        else {
            // wildcard SSID
            prAdapter->rWifiVar.rConnSettings.eConnectionPolicy = CONNECT_BY_SSID_ANY;
        }
    }
    else {
        prAdapter->rWifiVar.rConnSettings.fgIsConnReqIssued = FALSE;
    }

    /* Send AIS Abort Message */
    prAisAbortMsg = (P_MSG_AIS_ABORT_T)cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_AIS_ABORT_T));
    if (!prAisAbortMsg) {
        ASSERT(0);
        return WLAN_STATUS_FAILURE;
    }

    prAisAbortMsg->rMsgHdr.eMsgId = MID_OID_AIS_FSM_JOIN_REQ;
    prAisAbortMsg->ucReasonOfDisconnect = DISCONNECT_REASON_CODE_NEW_CONNECTION;

    if (EQUAL_SSID(prAdapter->rWifiVar.rConnSettings.aucSSID,
                prAdapter->rWifiVar.rConnSettings.ucSSIDLen,
                pParamSsid->aucSsid,
                pParamSsid->u4SsidLen)) {
        prAisAbortMsg->fgDelayIndication = TRUE;
    }
    else {
        /* Update the information to CONNECTION_SETTINGS_T */
        COPY_SSID(prAdapter->rWifiVar.rConnSettings.aucSSID,
                prAdapter->rWifiVar.rConnSettings.ucSSIDLen,
                pParamSsid->aucSsid,
                (UINT_8)pParamSsid->u4SsidLen);

        prAisAbortMsg->fgDelayIndication = FALSE;
    }
    DBGLOG(SCN, INFO, ("SSID %s\n", prAdapter->rWifiVar.rConnSettings.aucSSID));

    mboxSendMsg(prAdapter,
            MBOX_ID_0,
            (P_MSG_HDR_T) prAisAbortMsg,
            MSG_SEND_METHOD_BUF);

    return WLAN_STATUS_SUCCESS;

} /* end of wlanoidSetSsid() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the currently associated SSID.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvQueryBuffer Pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufferLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                             bytes written into the query buffer. If the call
*                             failed due to invalid length of the query buffer,
*                             returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQuerySsid (
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    )
{
    P_PARAM_SSID_T prAssociatedSsid;

    DEBUGFUNC("wlanoidQuerySsid");

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);

    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    *pu4QueryInfoLen = sizeof(PARAM_SSID_T);

    /* Check for query buffer length */
    if (u4QueryBufferLen < *pu4QueryInfoLen) {
        DBGLOG(REQ, WARN, ("Invalid length %lu\n", u4QueryBufferLen));
        return WLAN_STATUS_INVALID_LENGTH;
    }

    prAssociatedSsid = (P_PARAM_SSID_T)pvQueryBuffer;

    kalMemZero(prAssociatedSsid->aucSsid, sizeof(prAssociatedSsid->aucSsid));

    if (kalGetMediaStateIndicated(prAdapter->prGlueInfo) == PARAM_MEDIA_STATE_CONNECTED) {
        prAssociatedSsid->u4SsidLen = prAdapter->rWlanInfo.rCurrBssId.rSsid.u4SsidLen;

        if (prAssociatedSsid->u4SsidLen) {
            kalMemCopy(prAssociatedSsid->aucSsid,
                prAdapter->rWlanInfo.rCurrBssId.rSsid.aucSsid,
                prAssociatedSsid->u4SsidLen);
        }
    }
    else {
        prAssociatedSsid->u4SsidLen = 0;

        DBGLOG(REQ, TRACE, ("Null SSID\n"));
    }

    return WLAN_STATUS_SUCCESS;
} /* wlanoidQuerySsid */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the current 802.11 network type.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvQueryBuffer Pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufferLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                             bytes written into the query buffer. If the call
*                             failed due to invalid length of the query buffer,
*                             returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_BUFFER_TOO_SHORT
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryInfrastructureMode (
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryInfrastructureMode");

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);

    *pu4QueryInfoLen = sizeof(ENUM_PARAM_OP_MODE_T);

    if (u4QueryBufferLen < sizeof(ENUM_PARAM_OP_MODE_T)) {
        return WLAN_STATUS_BUFFER_TOO_SHORT;
    }

    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    *(P_ENUM_PARAM_OP_MODE_T)pvQueryBuffer = prAdapter->rWifiVar.rConnSettings.eOPMode;

    /*
    ** According to OID_802_11_INFRASTRUCTURE_MODE
    ** If there is no prior OID_802_11_INFRASTRUCTURE_MODE,
    ** NDIS_STATUS_ADAPTER_NOT_READY shall be returned.
    */
#if DBG
    switch (*(P_ENUM_PARAM_OP_MODE_T)pvQueryBuffer) {
        case NET_TYPE_IBSS:
             DBGLOG(REQ, INFO, ("IBSS mode\n"));
             break;
        case NET_TYPE_INFRA:
             DBGLOG(REQ, INFO, ("Infrastructure mode\n"));
             break;
        default:
             DBGLOG(REQ, INFO, ("Automatic mode\n"));
    }
#endif

    return WLAN_STATUS_SUCCESS;
}   /* wlanoidQueryInfrastructureMode */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set mode to infrastructure or
*        IBSS, or automatic switch between the two.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer Pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*             bytes read from the set buffer. If the call failed due to invalid
*             length of the set buffer, returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_DATA
* \retval WLAN_STATUS_ADAPTER_NOT_READY
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetInfrastructureMode (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    P_GLUE_INFO_T prGlueInfo;
    ENUM_PARAM_OP_MODE_T eOpMode;

    DEBUGFUNC("wlanoidSetInfrastructureMode");

    ASSERT(prAdapter);
    ASSERT(pvSetBuffer);
    ASSERT(pu4SetInfoLen);

    prGlueInfo = prAdapter->prGlueInfo;

    if (u4SetBufferLen < sizeof(ENUM_PARAM_OP_MODE_T))
        return WLAN_STATUS_BUFFER_TOO_SHORT;

    *pu4SetInfoLen = sizeof(ENUM_PARAM_OP_MODE_T);


    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in set infrastructure mode! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }

    eOpMode = *(P_ENUM_PARAM_OP_MODE_T)pvSetBuffer;
    /* Verify the new infrastructure mode. */
    if (eOpMode >= NET_TYPE_NUM) {
        DBGLOG(REQ, TRACE, ("Invalid mode value %d\n", eOpMode));
        return WLAN_STATUS_INVALID_DATA;
    }

    /* check if possible to switch to AdHoc mode */
    if(eOpMode == NET_TYPE_IBSS || eOpMode == NET_TYPE_DEDICATED_IBSS) {
        if(cnmAisIbssIsPermitted(prAdapter) == FALSE) {
            DBGLOG(REQ, TRACE, ("Mode value %d unallowed\n", eOpMode));
            return WLAN_STATUS_FAILURE;
        }
    }

    /* Save the new infrastructure mode setting. */
    prAdapter->rWifiVar.rConnSettings.eOPMode = eOpMode;

    /* Clean up the Tx key flag */
    prAdapter->rWifiVar.rAisSpecificBssInfo.fgTransmitKeyExist = FALSE;

    prAdapter->rWifiVar.rConnSettings.fgWapiMode = FALSE;
#if CFG_SUPPORT_WAPI
    prAdapter->prGlueInfo->u2WapiAssocInfoIESz = 0;
    kalMemZero(&prAdapter->prGlueInfo->aucWapiAssocInfoIEs, 42);
#endif

#if CFG_SUPPORT_802_11W
    prAdapter->rWifiVar.rAisSpecificBssInfo.fgMgmtProtection = FALSE;
    prAdapter->rWifiVar.rAisSpecificBssInfo.fgBipKeyInstalled = FALSE;
#endif

#if CFG_SUPPORT_WPS2
    kalMemZero(&prAdapter->prGlueInfo->aucWSCAssocInfoIE, 200);
    prAdapter->prGlueInfo->u2WSCAssocInfoIELen = 0;
#endif

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_INFRASTRUCTURE,
            TRUE,
            FALSE,
            TRUE,
            nicCmdEventSetCommon,
            nicOidCmdTimeoutCommon,
            0,
            NULL,
            pvSetBuffer,
            u4SetBufferLen
            );

}   /* wlanoidSetInfrastructureMode */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the current 802.11 authentication
*        mode.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuffer A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufferLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_BUFFER_TOO_SHORT
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryAuthMode (
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryAuthMode");

    ASSERT(prAdapter);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }
    ASSERT(pu4QueryInfoLen);

    *pu4QueryInfoLen = sizeof(ENUM_PARAM_AUTH_MODE_T);

    if (u4QueryBufferLen < sizeof(ENUM_PARAM_AUTH_MODE_T)) {
        return WLAN_STATUS_BUFFER_TOO_SHORT;
    }

    *(P_ENUM_PARAM_AUTH_MODE_T)pvQueryBuffer = prAdapter->rWifiVar.rConnSettings.eAuthMode;

#if DBG
    switch (*(P_ENUM_PARAM_AUTH_MODE_T)pvQueryBuffer) {
    case AUTH_MODE_OPEN:
        DBGLOG(REQ, INFO, ("Current auth mode: Open\n"));
        break;

    case AUTH_MODE_SHARED:
        DBGLOG(REQ, INFO, ("Current auth mode: Shared\n"));
        break;

    case AUTH_MODE_AUTO_SWITCH:
        DBGLOG(REQ, INFO, ("Current auth mode: Auto-switch\n"));
        break;

    case AUTH_MODE_WPA:
        DBGLOG(REQ, INFO, ("Current auth mode: WPA\n"));
        break;

    case AUTH_MODE_WPA_PSK:
        DBGLOG(REQ, INFO, ("Current auth mode: WPA PSK\n"));
        break;

    case AUTH_MODE_WPA_NONE:
        DBGLOG(REQ, INFO, ("Current auth mode: WPA None\n"));
        break;

    case AUTH_MODE_WPA2:
        DBGLOG(REQ, INFO, ("Current auth mode: WPA2\n"));
        break;

    case AUTH_MODE_WPA2_PSK:
        DBGLOG(REQ, INFO, ("Current auth mode: WPA2 PSK\n"));
        break;

    default:
        DBGLOG(REQ, INFO, ("Current auth mode: %d\n",
            *(P_ENUM_PARAM_AUTH_MODE_T)pvQueryBuffer));
    }
#endif
    return WLAN_STATUS_SUCCESS;
} /* wlanoidQueryAuthMode */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set the IEEE 802.11 authentication mode
*        to the driver.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_DATA
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_ADAPTER_NOT_READY
* \retval WLAN_STATUS_NOT_ACCEPTED
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetAuthMode (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    P_GLUE_INFO_T prGlueInfo;
    UINT_32       i, u4AkmSuite;
    P_DOT11_RSNA_CONFIG_AUTHENTICATION_SUITES_ENTRY prEntry;

    DEBUGFUNC("wlanoidSetAuthMode");

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);
    ASSERT(pvSetBuffer);

    prGlueInfo = prAdapter->prGlueInfo;

    *pu4SetInfoLen = sizeof(ENUM_PARAM_AUTH_MODE_T);

    if (u4SetBufferLen < sizeof(ENUM_PARAM_AUTH_MODE_T)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    /* RF Test */
    //if (IS_ARB_IN_RFTEST_STATE(prAdapter)) {
    //  return WLAN_STATUS_SUCCESS;
    //}

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in set Authentication mode! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }

    /* Check if the new authentication mode is valid. */
    if (*(P_ENUM_PARAM_AUTH_MODE_T)pvSetBuffer >= AUTH_MODE_NUM) {
        DBGLOG(REQ, TRACE, ("Invalid auth mode %d\n",
            *(P_ENUM_PARAM_AUTH_MODE_T)pvSetBuffer));
        return WLAN_STATUS_INVALID_DATA;
    }

    switch (*(P_ENUM_PARAM_AUTH_MODE_T)pvSetBuffer) {
    case AUTH_MODE_WPA:
    case AUTH_MODE_WPA_PSK:
    case AUTH_MODE_WPA2:
    case AUTH_MODE_WPA2_PSK:
        /* infrastructure mode only */
        if (prAdapter->rWifiVar.rConnSettings.eOPMode != NET_TYPE_INFRA) {
            return WLAN_STATUS_NOT_ACCEPTED;
        }
        break;

    case AUTH_MODE_WPA_NONE:
        /* ad hoc mode only */
        if (prAdapter->rWifiVar.rConnSettings.eOPMode != NET_TYPE_IBSS) {
            return WLAN_STATUS_NOT_ACCEPTED;
        }
        break;

    default:
        ;
    }

    /* Save the new authentication mode. */
    prAdapter->rWifiVar.rConnSettings.eAuthMode = *(P_ENUM_PARAM_AUTH_MODE_T)pvSetBuffer;

#if DBG
    switch (prAdapter->rWifiVar.rConnSettings.eAuthMode) {
    case AUTH_MODE_OPEN:
        DBGLOG(RSN, TRACE, ("New auth mode: open\n"));
        break;

    case AUTH_MODE_SHARED:
        DBGLOG(RSN, TRACE, ("New auth mode: shared\n"));
        break;

    case AUTH_MODE_AUTO_SWITCH:
        DBGLOG(RSN, TRACE, ("New auth mode: auto-switch\n"));
        break;

    case AUTH_MODE_WPA:
        DBGLOG(RSN, TRACE, ("New auth mode: WPA\n"));
        break;

    case AUTH_MODE_WPA_PSK:
        DBGLOG(RSN, TRACE, ("New auth mode: WPA PSK\n"));
        break;

    case AUTH_MODE_WPA_NONE:
        DBGLOG(RSN, TRACE, ("New auth mode: WPA None\n"));
        break;

    case AUTH_MODE_WPA2:
        DBGLOG(RSN, TRACE, ("New auth mode: WPA2\n"));
        break;

    case AUTH_MODE_WPA2_PSK:
        DBGLOG(RSN, TRACE, ("New auth mode: WPA2 PSK\n"));
        break;

    default:
        DBGLOG(RSN, TRACE, ("New auth mode: unknown (%d)\n",
            prAdapter->rWifiVar.rConnSettings.eAuthMode));
    }
#endif

    if (prAdapter->rWifiVar.rConnSettings.eAuthMode >= AUTH_MODE_WPA) {
        switch(prAdapter->rWifiVar.rConnSettings.eAuthMode) {
        case AUTH_MODE_WPA:
            u4AkmSuite = WPA_AKM_SUITE_802_1X;
            break;

        case AUTH_MODE_WPA_PSK:
            u4AkmSuite = WPA_AKM_SUITE_PSK;
            break;

        case AUTH_MODE_WPA_NONE:
            u4AkmSuite = WPA_AKM_SUITE_NONE;
            break;

        case AUTH_MODE_WPA2:
            u4AkmSuite = RSN_AKM_SUITE_802_1X;
            break;

        case AUTH_MODE_WPA2_PSK:
            u4AkmSuite = RSN_AKM_SUITE_PSK;
            break;

        default:
            u4AkmSuite = 0;
        }
    }
    else {
        u4AkmSuite = 0;
    }

    /* Enable the specific AKM suite only. */
    for (i = 0; i < MAX_NUM_SUPPORTED_AKM_SUITES; i++) {
        prEntry = &prAdapter->rMib.dot11RSNAConfigAuthenticationSuitesTable[i];

        if (prEntry->dot11RSNAConfigAuthenticationSuite == u4AkmSuite) {
            prEntry->dot11RSNAConfigAuthenticationSuiteEnabled = TRUE;
        }
        else {
            prEntry->dot11RSNAConfigAuthenticationSuiteEnabled = FALSE;
        }
#if CFG_SUPPORT_802_11W
        if (kalGetMfpSetting(prAdapter->prGlueInfo) != RSN_AUTH_MFP_DISABLED) {
            if ((u4AkmSuite == RSN_AKM_SUITE_PSK) &&
                prEntry->dot11RSNAConfigAuthenticationSuite == RSN_AKM_SUITE_PSK_SHA256) {
                DBGLOG(RSN, TRACE, ("Enable RSN_AKM_SUITE_PSK_SHA256 AKM support\n"));
                prEntry->dot11RSNAConfigAuthenticationSuiteEnabled = TRUE;

            }
            if ((u4AkmSuite == RSN_AKM_SUITE_802_1X) &&
                prEntry->dot11RSNAConfigAuthenticationSuite == RSN_AKM_SUITE_802_1X_SHA256) {
                DBGLOG(RSN, TRACE, ("Enable RSN_AKM_SUITE_802_1X_SHA256 AKM support\n"));
                prEntry->dot11RSNAConfigAuthenticationSuiteEnabled = TRUE;
            }
        }
#endif
    }


    return WLAN_STATUS_SUCCESS;

} /* wlanoidSetAuthMode */


#if 0
/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the current 802.11 privacy filter
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuffer A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufferLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_BUFFER_TOO_SHORT
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryPrivacyFilter (
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryPrivacyFilter");

    ASSERT(prAdapter);

    ASSERT(pvQueryBuffer);
    ASSERT(pu4QueryInfoLen);

    *pu4QueryInfoLen = sizeof(ENUM_PARAM_PRIVACY_FILTER_T);

    if (u4QueryBufferLen < sizeof(ENUM_PARAM_PRIVACY_FILTER_T)) {
        return WLAN_STATUS_BUFFER_TOO_SHORT;
    }

    *(P_ENUM_PARAM_PRIVACY_FILTER_T)pvQueryBuffer = prAdapter->rWlanInfo.ePrivacyFilter;

#if DBG
    switch (*(P_ENUM_PARAM_PRIVACY_FILTER_T)pvQueryBuffer) {
    case PRIVACY_FILTER_ACCEPT_ALL:
        DBGLOG(REQ, INFO, ("Current privacy mode: open mode\n"));
        break;

    case PRIVACY_FILTER_8021xWEP:
        DBGLOG(REQ, INFO, ("Current privacy mode: filtering mode\n"));
        break;

    default:
        DBGLOG(REQ, INFO, ("Current auth mode: %d\n",
            *(P_ENUM_PARAM_AUTH_MODE_T)pvQueryBuffer));
    }
#endif
    return WLAN_STATUS_SUCCESS;
} /* wlanoidQueryPrivacyFilter */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set the IEEE 802.11 privacy filter
*        to the driver.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_DATA
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_ADAPTER_NOT_READY
* \retval WLAN_STATUS_NOT_ACCEPTED
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetPrivacyFilter (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    P_GLUE_INFO_T prGlueInfo;

    DEBUGFUNC("wlanoidSetPrivacyFilter");

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);
    ASSERT(pvSetBuffer);

    prGlueInfo = prAdapter->prGlueInfo;

    *pu4SetInfoLen = sizeof(ENUM_PARAM_PRIVACY_FILTER_T);

    if (u4SetBufferLen < sizeof(ENUM_PARAM_PRIVACY_FILTER_T)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in set Authentication mode! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }

    /* Check if the new authentication mode is valid. */
    if (*(P_ENUM_PARAM_PRIVACY_FILTER_T)pvSetBuffer >= PRIVACY_FILTER_NUM) {
        DBGLOG(REQ, TRACE, ("Invalid privacy filter %d\n",
            *(P_ENUM_PARAM_PRIVACY_FILTER_T)pvSetBuffer));
        return WLAN_STATUS_INVALID_DATA;
    }

    switch (*(P_ENUM_PARAM_PRIVACY_FILTER_T)pvSetBuffer) {
    default:
        break;
    }

    /* Save the new authentication mode. */
    prAdapter->rWlanInfo.ePrivacyFilter = *(ENUM_PARAM_PRIVACY_FILTER_T)pvSetBuffer;

    return WLAN_STATUS_SUCCESS;

} /* wlanoidSetPrivacyFilter */
#endif


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to reload the available default settings for
*        the specified type field.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_ADAPTER_NOT_READY
* \retval WLAN_STATUS_INVALID_DATA
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetReloadDefaults (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    ENUM_PARAM_NETWORK_TYPE_T eNetworkType;
    UINT_32 u4Len;
    UINT_8 ucCmdSeqNum;


    DEBUGFUNC("wlanoidSetReloadDefaults");

    ASSERT(prAdapter);

    ASSERT(pu4SetInfoLen);
    *pu4SetInfoLen = sizeof(PARAM_RELOAD_DEFAULTS);

    //if (IS_ARB_IN_RFTEST_STATE(prAdapter)) {
    //  return WLAN_STATUS_SUCCESS;
    //}

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in set Reload default! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }

    ASSERT(pvSetBuffer);
    /* Verify the available reload options and reload the settings. */
    switch (*(P_PARAM_RELOAD_DEFAULTS)pvSetBuffer) {
    case ENUM_RELOAD_WEP_KEYS:
        /* Reload available default WEP keys from the permanent
            storage. */
        prAdapter->rWifiVar.rConnSettings.eAuthMode = AUTH_MODE_OPEN;
        prAdapter->rWifiVar.rConnSettings.eEncStatus = ENUM_ENCRYPTION1_KEY_ABSENT;//ENUM_ENCRYPTION_DISABLED;
        {
            P_GLUE_INFO_T         prGlueInfo;
            P_CMD_INFO_T          prCmdInfo;
            P_WIFI_CMD_T          prWifiCmd;
            P_CMD_802_11_KEY      prCmdKey;
            UINT_8                aucBCAddr[] = BC_MAC_ADDR;

            prGlueInfo = prAdapter->prGlueInfo;
            prCmdInfo = cmdBufAllocateCmdInfo(prAdapter, (CMD_HDR_SIZE + sizeof(CMD_802_11_KEY)));

            if (!prCmdInfo) {
                DBGLOG(INIT, ERROR, ("Allocate CMD_INFO_T ==> FAILED.\n"));
                return WLAN_STATUS_FAILURE;
            }
            // increase command sequence number
            ucCmdSeqNum = nicIncreaseCmdSeqNum(prAdapter);

            // compose CMD_802_11_KEY cmd pkt
            prCmdInfo->eCmdType = COMMAND_TYPE_NETWORK_IOCTL;
            prCmdInfo->eNetworkType = NETWORK_TYPE_AIS_INDEX;
            prCmdInfo->u2InfoBufLen = CMD_HDR_SIZE + sizeof(CMD_802_11_KEY);
            prCmdInfo->pfCmdDoneHandler = nicCmdEventSetCommon;
            prCmdInfo->pfCmdTimeoutHandler = nicOidCmdTimeoutCommon;
            prCmdInfo->fgIsOid = TRUE;
            prCmdInfo->ucCID = CMD_ID_ADD_REMOVE_KEY;
            prCmdInfo->fgSetQuery = TRUE;
            prCmdInfo->fgNeedResp = FALSE;
            prCmdInfo->fgDriverDomainMCR = FALSE;
            prCmdInfo->ucCmdSeqNum = ucCmdSeqNum;
            prCmdInfo->u4SetInfoLen = sizeof(PARAM_REMOVE_KEY_T);
            prCmdInfo->pvInformationBuffer = pvSetBuffer;
            prCmdInfo->u4InformationBufferLength = u4SetBufferLen;

            // Setup WIFI_CMD_T
            prWifiCmd = (P_WIFI_CMD_T)(prCmdInfo->pucInfoBuffer);
            prWifiCmd->u2TxByteCount_UserPriority = prCmdInfo->u2InfoBufLen;
            prWifiCmd->ucCID = prCmdInfo->ucCID;
            prWifiCmd->ucSetQuery = prCmdInfo->fgSetQuery;
            prWifiCmd->ucSeqNum = prCmdInfo->ucCmdSeqNum;

            prCmdKey = (P_CMD_802_11_KEY)(prWifiCmd->aucBuffer);

            kalMemZero((PUINT_8)prCmdKey, sizeof(CMD_802_11_KEY));

            prCmdKey->ucAddRemove = 0; /* Remove */
            prCmdKey->ucKeyId = 0;//(UINT_8)(prRemovedKey->u4KeyIndex & 0x000000ff);
            kalMemCopy(prCmdKey->aucPeerAddr, aucBCAddr, MAC_ADDR_LEN);

            ASSERT(prCmdKey->ucKeyId < MAX_KEY_NUM);

            prCmdKey->ucKeyType = 0;

            // insert into prCmdQueue
            kalEnqueueCommand(prGlueInfo, (P_QUE_ENTRY_T)prCmdInfo);

            // wakeup txServiceThread later
            GLUE_SET_EVENT(prGlueInfo);

            return WLAN_STATUS_PENDING;
        }

        break;

    default:
        DBGLOG(REQ, TRACE, ("Invalid reload option %d\n",
            *(P_PARAM_RELOAD_DEFAULTS)pvSetBuffer));
        rStatus = WLAN_STATUS_INVALID_DATA;
    }

    /* OID_802_11_RELOAD_DEFAULTS requiest to reset to auto mode */
    eNetworkType = PARAM_NETWORK_TYPE_AUTOMODE;
    wlanoidSetNetworkTypeInUse(prAdapter, &eNetworkType, sizeof(eNetworkType), &u4Len);

    return rStatus;
} /* wlanoidSetReloadDefaults */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set a WEP key to the driver.
*
* \param[in]  prAdapter Pointer to the Adapter structure.
* \param[in]  pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in]  u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_INVALID_DATA
* \retval WLAN_STATUS_ADAPTER_NOT_READY
* \retval WLAN_STATUS_BUFFER_TOO_SHORT
*/
/*----------------------------------------------------------------------------*/
#ifdef LINUX
UINT_8        keyBuffer[sizeof(PARAM_KEY_T) + 16 /* LEGACY_KEY_MAX_LEN*/];
UINT_8        aucBCAddr[] = BC_MAC_ADDR;
#endif
WLAN_STATUS
wlanoidSetAddWep (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID    pvSetBuffer,
    IN  UINT_32  u4SetBufferLen,
    OUT PUINT_32 pu4SetInfoLen
    )
{
    #ifndef LINUX
    UINT_8        keyBuffer[sizeof(PARAM_KEY_T) + 16 /* LEGACY_KEY_MAX_LEN*/];
    UINT_8        aucBCAddr[] = BC_MAC_ADDR;
    #endif
    P_PARAM_WEP_T prNewWepKey;
    P_PARAM_KEY_T prParamKey = (P_PARAM_KEY_T)keyBuffer;
    UINT_32       u4KeyId, u4SetLen;

    DEBUGFUNC("wlanoidSetAddWep");

    ASSERT(prAdapter);

    *pu4SetInfoLen = OFFSET_OF(PARAM_WEP_T, aucKeyMaterial);

    if (u4SetBufferLen < OFFSET_OF(PARAM_WEP_T, aucKeyMaterial)) {
        ASSERT(pu4SetInfoLen);
        return WLAN_STATUS_BUFFER_TOO_SHORT;
    }

    ASSERT(pvSetBuffer);
    ASSERT(pu4SetInfoLen);

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in set add WEP! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }

    prNewWepKey = (P_PARAM_WEP_T)pvSetBuffer;

    /* Verify the total buffer for minimum length. */
    if (u4SetBufferLen < OFFSET_OF(PARAM_WEP_T, aucKeyMaterial) + prNewWepKey->u4KeyLength) {
        DBGLOG(REQ, WARN, ("Invalid total buffer length (%d) than minimum length (%d)\n",
                          (UINT_8)u4SetBufferLen,
                          (UINT_8)OFFSET_OF(PARAM_WEP_T, aucKeyMaterial)));

        *pu4SetInfoLen = OFFSET_OF(PARAM_WEP_T, aucKeyMaterial);
        return WLAN_STATUS_INVALID_DATA;
    }

    /* Verify the key structure length. */
    if (prNewWepKey->u4Length > u4SetBufferLen) {
        DBGLOG(REQ, WARN, ("Invalid key structure length (%d) greater than total buffer length (%d)\n",
                          (UINT_8)prNewWepKey->u4Length,
                          (UINT_8)u4SetBufferLen));

        *pu4SetInfoLen = u4SetBufferLen;
        return WLAN_STATUS_INVALID_DATA;
    }

    /* Verify the key material length for maximum key material length:16 */
    if (prNewWepKey->u4KeyLength > 16 /* LEGACY_KEY_MAX_LEN */) {
        DBGLOG(REQ, WARN, ("Invalid key material length (%d) greater than maximum key material length (16)\n",
            (UINT_8)prNewWepKey->u4KeyLength));

        *pu4SetInfoLen = u4SetBufferLen;
        return WLAN_STATUS_INVALID_DATA;
    }

    *pu4SetInfoLen = u4SetBufferLen;

    u4KeyId = prNewWepKey->u4KeyIndex & BITS(0,29) /* WEP_KEY_ID_FIELD */;

    /* Verify whether key index is valid or not, current version
       driver support only 4 global WEP keys setting by this OID */
    if (u4KeyId > MAX_KEY_NUM - 1) {
        DBGLOG(REQ, ERROR, ("Error, invalid WEP key ID: %d\n", (UINT_8)u4KeyId));
        return WLAN_STATUS_INVALID_DATA;
    }

    prParamKey->u4KeyIndex = u4KeyId;

    /* Transmit key */
    if (prNewWepKey->u4KeyIndex & IS_TRANSMIT_KEY) {
        prParamKey->u4KeyIndex |= IS_TRANSMIT_KEY;
    }

    /* Per client key */
    if (prNewWepKey->u4KeyIndex & IS_UNICAST_KEY) {
        prParamKey->u4KeyIndex |= IS_UNICAST_KEY;
    }

    prParamKey->u4KeyLength = prNewWepKey->u4KeyLength;

    kalMemCopy(prParamKey->arBSSID, aucBCAddr, MAC_ADDR_LEN);

    kalMemCopy(prParamKey->aucKeyMaterial,
        prNewWepKey->aucKeyMaterial,
        prNewWepKey->u4KeyLength);

    prParamKey->u4Length = OFFSET_OF(PARAM_KEY_T, aucKeyMaterial) + prNewWepKey->u4KeyLength;

    wlanoidSetAddKey(prAdapter,
        (PVOID)prParamKey,
        prParamKey->u4Length,
        &u4SetLen);

    return WLAN_STATUS_PENDING;
} /* wlanoidSetAddWep */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to request the driver to remove the WEP key
*          at the specified key index.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_DATA
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_ADAPTER_NOT_READY
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetRemoveWep (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    UINT_32               u4KeyId, u4SetLen;
    PARAM_REMOVE_KEY_T    rRemoveKey;
    UINT_8                aucBCAddr[] = BC_MAC_ADDR;

    DEBUGFUNC("wlanoidSetRemoveWep");

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(PARAM_KEY_INDEX);

    if (u4SetBufferLen < sizeof(PARAM_KEY_INDEX)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    ASSERT(pvSetBuffer);
    u4KeyId = *(PUINT_32)pvSetBuffer;

    /* Dump PARAM_WEP content. */
    DBGLOG(REQ, INFO, ("Set: Dump PARAM_KEY_INDEX content\n"));
    DBGLOG(REQ, INFO, ("Index : 0x%08lx\n", u4KeyId));

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in set remove WEP! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }

    if (u4KeyId & IS_TRANSMIT_KEY) {
        /* Bit 31 should not be set */
        DBGLOG(REQ, ERROR, ("Invalid WEP key index: 0x%08lx\n", u4KeyId));
        return WLAN_STATUS_INVALID_DATA;
    }

    u4KeyId &= BITS(0,7);

    /* Verify whether key index is valid or not. Current version
        driver support only 4 global WEP keys. */
    if (u4KeyId > MAX_KEY_NUM - 1) {
        DBGLOG(REQ, ERROR, ("invalid WEP key ID %lu\n", u4KeyId));
        return WLAN_STATUS_INVALID_DATA;
    }

    rRemoveKey.u4Length = sizeof(PARAM_REMOVE_KEY_T);
    rRemoveKey.u4KeyIndex = *(PUINT_32)pvSetBuffer;

    kalMemCopy(rRemoveKey.arBSSID, aucBCAddr, MAC_ADDR_LEN);

    wlanoidSetRemoveKey(prAdapter,
        (PVOID)&rRemoveKey,
        sizeof(PARAM_REMOVE_KEY_T),
        &u4SetLen);

    return WLAN_STATUS_PENDING;
} /* wlanoidSetRemoveWep */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set a key to the driver.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_ADAPTER_NOT_READY
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_INVALID_DATA
*
* \note The setting buffer PARAM_KEY_T, which is set by NDIS, is unpacked.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetAddKey (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID    pvSetBuffer,
    IN  UINT_32  u4SetBufferLen,
    OUT PUINT_32 pu4SetInfoLen
    )
{
    P_GLUE_INFO_T         prGlueInfo;
    P_CMD_INFO_T          prCmdInfo;
    P_WIFI_CMD_T          prWifiCmd;
    P_PARAM_KEY_T         prNewKey;
    P_CMD_802_11_KEY      prCmdKey;
    UINT_8 ucCmdSeqNum;

    DEBUGFUNC("wlanoidSetAddKey");
    DBGLOG(REQ, LOUD, ("\n"));

    ASSERT(prAdapter);
    ASSERT(pvSetBuffer);
    ASSERT(pu4SetInfoLen);

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in set add key! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }

    prNewKey = (P_PARAM_KEY_T) pvSetBuffer;

    /* Verify the key structure length. */
    if (prNewKey->u4Length > u4SetBufferLen) {
        DBGLOG(REQ, WARN, ("Invalid key structure length (%d) greater than total buffer length (%d)\n",
                          (UINT_8)prNewKey->u4Length,
                          (UINT_8)u4SetBufferLen));

        *pu4SetInfoLen = u4SetBufferLen;
        return WLAN_STATUS_INVALID_LENGTH;
    }

    /* Verify the key material length for key material buffer */
    if (prNewKey->u4KeyLength > prNewKey->u4Length - OFFSET_OF(PARAM_KEY_T, aucKeyMaterial)) {
        DBGLOG(REQ, WARN, ("Invalid key material length (%d)\n", (UINT_8)prNewKey->u4KeyLength));
        *pu4SetInfoLen = u4SetBufferLen;
        return WLAN_STATUS_INVALID_DATA;
    }

    /* Exception check */
    if (prNewKey->u4KeyIndex & 0x0fffff00) {
        return WLAN_STATUS_INVALID_DATA;
    }

   /* Exception check, pairwise key must with transmit bit enabled */
    if ((prNewKey->u4KeyIndex & BITS(30,31)) == IS_UNICAST_KEY) {
        return WLAN_STATUS_INVALID_DATA;
    }

    if (!(prNewKey->u4KeyLength == WEP_40_LEN || prNewKey->u4KeyLength == WEP_104_LEN ||
          prNewKey->u4KeyLength == CCMP_KEY_LEN || prNewKey->u4KeyLength == TKIP_KEY_LEN))
    {
        return WLAN_STATUS_INVALID_DATA;
    }

    /* Exception check, pairwise key must with transmit bit enabled */
    if ((prNewKey->u4KeyIndex & BITS(30,31)) == BITS(30,31)) {
        if (((prNewKey->u4KeyIndex & 0xff) != 0) ||
            ((prNewKey->arBSSID[0] == 0xff) && (prNewKey->arBSSID[1] == 0xff) && (prNewKey->arBSSID[2] == 0xff) &&
             (prNewKey->arBSSID[3] == 0xff) && (prNewKey->arBSSID[4] == 0xff) && (prNewKey->arBSSID[5] == 0xff))) {
            return WLAN_STATUS_INVALID_DATA;
        }
    }

    *pu4SetInfoLen = u4SetBufferLen;

    /* Dump PARAM_KEY content. */
    DBGLOG(REQ, TRACE, ("Set: Dump PARAM_KEY content\n"));
    DBGLOG(REQ, TRACE, ("Length    : 0x%08lx\n", prNewKey->u4Length));
    DBGLOG(REQ, TRACE, ("Key Index : 0x%08lx\n", prNewKey->u4KeyIndex));
    DBGLOG(REQ, TRACE, ("Key Length: 0x%08lx\n", prNewKey->u4KeyLength));
    DBGLOG(REQ, TRACE, ("BSSID:\n"));
    DBGLOG_MEM8(REQ, TRACE, prNewKey->arBSSID, sizeof(PARAM_MAC_ADDRESS));
    DBGLOG(REQ, TRACE, ("Key RSC:\n"));
    DBGLOG_MEM8(REQ, TRACE, &prNewKey->rKeyRSC, sizeof(PARAM_KEY_RSC));
    DBGLOG(REQ, TRACE, ("Key Material:\n"));
    DBGLOG_MEM8(REQ, TRACE, prNewKey->aucKeyMaterial, prNewKey->u4KeyLength);

    if (prAdapter->rWifiVar.rConnSettings.eAuthMode < AUTH_MODE_WPA) {
        /* Todo:: Store the legacy wep key for OID_802_11_RELOAD_DEFAULTS */
    }

    if (prNewKey->u4KeyIndex & IS_TRANSMIT_KEY)
        prAdapter->rWifiVar.rAisSpecificBssInfo.fgTransmitKeyExist = TRUE;

    prGlueInfo = prAdapter->prGlueInfo;
    prCmdInfo = cmdBufAllocateCmdInfo(prAdapter, (CMD_HDR_SIZE + sizeof(CMD_802_11_KEY)));

    if (!prCmdInfo) {
        DBGLOG(INIT, ERROR, ("Allocate CMD_INFO_T ==> FAILED.\n"));
        return WLAN_STATUS_FAILURE;
    }

    // increase command sequence number
    ucCmdSeqNum = nicIncreaseCmdSeqNum(prAdapter);
    DBGLOG(REQ, INFO, ("ucCmdSeqNum = %d\n", ucCmdSeqNum));

    // compose CMD_802_11_KEY cmd pkt
    prCmdInfo->eCmdType = COMMAND_TYPE_NETWORK_IOCTL;
    prCmdInfo->eNetworkType = NETWORK_TYPE_AIS_INDEX;
    prCmdInfo->u2InfoBufLen = CMD_HDR_SIZE + sizeof(CMD_802_11_KEY);
    prCmdInfo->pfCmdDoneHandler = nicCmdEventSetCommon;
    prCmdInfo->pfCmdTimeoutHandler = nicOidCmdTimeoutCommon;
    prCmdInfo->fgIsOid = TRUE;
    prCmdInfo->ucCID = CMD_ID_ADD_REMOVE_KEY;
    prCmdInfo->fgSetQuery = TRUE;
    prCmdInfo->fgNeedResp = FALSE;
    prCmdInfo->fgDriverDomainMCR = FALSE;
    prCmdInfo->ucCmdSeqNum = ucCmdSeqNum;
    prCmdInfo->u4SetInfoLen = u4SetBufferLen;
    prCmdInfo->pvInformationBuffer = pvSetBuffer;
    prCmdInfo->u4InformationBufferLength = u4SetBufferLen;

    // Setup WIFI_CMD_T
    prWifiCmd = (P_WIFI_CMD_T)(prCmdInfo->pucInfoBuffer);
    prWifiCmd->u2TxByteCount_UserPriority = prCmdInfo->u2InfoBufLen;
    prWifiCmd->ucCID = prCmdInfo->ucCID;
    prWifiCmd->ucSetQuery = prCmdInfo->fgSetQuery;
    prWifiCmd->ucSeqNum = prCmdInfo->ucCmdSeqNum;

    prCmdKey = (P_CMD_802_11_KEY)(prWifiCmd->aucBuffer);

    kalMemZero(prCmdKey, sizeof(CMD_802_11_KEY));

    prCmdKey->ucAddRemove = 1; /* Add */

    prCmdKey->ucTxKey = ((prNewKey->u4KeyIndex & IS_TRANSMIT_KEY) == IS_TRANSMIT_KEY) ? 1 : 0;
    prCmdKey->ucKeyType = ((prNewKey->u4KeyIndex & IS_UNICAST_KEY) == IS_UNICAST_KEY) ? 1 : 0;
    prCmdKey->ucIsAuthenticator = ((prNewKey->u4KeyIndex & IS_AUTHENTICATOR) == IS_AUTHENTICATOR) ? 1 : 0;
    
    kalMemCopy(prCmdKey->aucPeerAddr, (PUINT_8)prNewKey->arBSSID, MAC_ADDR_LEN);

    prCmdKey->ucNetType = 0; /* AIS */

    prCmdKey->ucKeyId = (UINT_8)(prNewKey->u4KeyIndex & 0xff);

    /* Note: adjust the key length for WPA-None */
    prCmdKey->ucKeyLen = (UINT_8)prNewKey->u4KeyLength;

    kalMemCopy(prCmdKey->aucKeyMaterial, (PUINT_8)prNewKey->aucKeyMaterial, prCmdKey->ucKeyLen);

    if (prNewKey->u4KeyLength == 5) {
        prCmdKey->ucAlgorithmId = CIPHER_SUITE_WEP40;
    }
    else if (prNewKey->u4KeyLength == 13) {
        prCmdKey->ucAlgorithmId = CIPHER_SUITE_WEP104;
    }
    else if (prNewKey->u4KeyLength == 16) {
        if (prAdapter->rWifiVar.rConnSettings.eAuthMode < AUTH_MODE_WPA)
            prCmdKey->ucAlgorithmId = CIPHER_SUITE_WEP128;
        else {
#if CFG_SUPPORT_802_11W
            if (prCmdKey->ucKeyId >= 4) {
                prCmdKey->ucAlgorithmId = CIPHER_SUITE_BIP;
                P_AIS_SPECIFIC_BSS_INFO_T  prAisSpecBssInfo;

                prAisSpecBssInfo = &prAdapter->rWifiVar.rAisSpecificBssInfo;
                prAisSpecBssInfo->fgBipKeyInstalled = TRUE;
            }
        else
#endif
            prCmdKey->ucAlgorithmId = CIPHER_SUITE_CCMP;
        }
    }
    else if (prNewKey->u4KeyLength == 32) {
        if (prAdapter->rWifiVar.rConnSettings.eAuthMode == AUTH_MODE_WPA_NONE) {
            if (prAdapter->rWifiVar.rConnSettings.eEncStatus == ENUM_ENCRYPTION2_ENABLED) {
                prCmdKey->ucAlgorithmId = CIPHER_SUITE_TKIP;
            }
            else if (prAdapter->rWifiVar.rConnSettings.eEncStatus == ENUM_ENCRYPTION3_ENABLED) {
                prCmdKey->ucAlgorithmId = CIPHER_SUITE_CCMP;
                prCmdKey->ucKeyLen = CCMP_KEY_LEN;
            }
        }
        else {
            if (rsnCheckPmkidCandicate(prAdapter)) {
                P_AIS_SPECIFIC_BSS_INFO_T  prAisSpecBssInfo;

                prAisSpecBssInfo = &prAdapter->rWifiVar.rAisSpecificBssInfo;
                DBGLOG(RSN, TRACE, ("Add key: Prepare a timer to indicate candidate PMKID Candidate\n"));
                cnmTimerStopTimer(prAdapter, &prAisSpecBssInfo->rPreauthenticationTimer);
                cnmTimerStartTimer(prAdapter, &prAisSpecBssInfo->rPreauthenticationTimer,
                        SEC_TO_MSEC(WAIT_TIME_IND_PMKID_CANDICATE_SEC));
            }
            prCmdKey->ucAlgorithmId = CIPHER_SUITE_TKIP;
        }
    }

    // insert into prCmdQueue
    kalEnqueueCommand(prGlueInfo, (P_QUE_ENTRY_T)prCmdInfo);

    // wakeup txServiceThread later
    GLUE_SET_EVENT(prGlueInfo);

    return WLAN_STATUS_PENDING;
} /* wlanoidSetAddKey */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to request the driver to remove the key at
*        the specified key index.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_DATA
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_ADAPTER_NOT_READY
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetRemoveKey (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    P_GLUE_INFO_T         prGlueInfo;
    P_CMD_INFO_T          prCmdInfo;
    P_WIFI_CMD_T          prWifiCmd;
    P_PARAM_REMOVE_KEY_T  prRemovedKey;
    P_CMD_802_11_KEY      prCmdKey;
    UINT_8                ucCmdSeqNum;

    DEBUGFUNC("wlanoidSetRemoveKey");

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(PARAM_REMOVE_KEY_T);

    if (u4SetBufferLen < sizeof(PARAM_REMOVE_KEY_T)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in set remove key! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }

    ASSERT(pvSetBuffer);
    prRemovedKey = (P_PARAM_REMOVE_KEY_T)pvSetBuffer;

    /* Dump PARAM_REMOVE_KEY content. */
    DBGLOG(REQ, INFO, ("Set: Dump PARAM_REMOVE_KEY content\n"));
    DBGLOG(REQ, INFO, ("Length    : 0x%08lx\n", prRemovedKey->u4Length));
    DBGLOG(REQ, INFO, ("Key Index : 0x%08lx\n", prRemovedKey->u4KeyIndex));
    DBGLOG(REQ, INFO, ("BSSID:\n"));
    DBGLOG_MEM8(REQ, INFO, prRemovedKey->arBSSID, MAC_ADDR_LEN);

    /* Check bit 31: this bit should always 0 */
    if (prRemovedKey->u4KeyIndex & IS_TRANSMIT_KEY) {
        /* Bit 31 should not be set */
        DBGLOG(REQ, ERROR, ("invalid key index: 0x%08lx\n",
            prRemovedKey->u4KeyIndex));
        return WLAN_STATUS_INVALID_DATA;
    }

    /* Check bits 8 ~ 29 should always be 0 */
    if (prRemovedKey->u4KeyIndex & BITS(8, 29)) {
        /* Bit 31 should not be set */
        DBGLOG(REQ, ERROR, ("invalid key index: 0x%08lx\n",
            prRemovedKey->u4KeyIndex));
        return WLAN_STATUS_INVALID_DATA;
    }

    /* Clean up the Tx key flag */
    if (prRemovedKey->u4KeyIndex & IS_UNICAST_KEY) {
        prAdapter->rWifiVar.rAisSpecificBssInfo.fgTransmitKeyExist = FALSE;
    }

    prGlueInfo = prAdapter->prGlueInfo;
    prCmdInfo = cmdBufAllocateCmdInfo(prAdapter, (CMD_HDR_SIZE + sizeof(CMD_802_11_KEY)));

    if (!prCmdInfo) {
        DBGLOG(INIT, ERROR, ("Allocate CMD_INFO_T ==> FAILED.\n"));
        return WLAN_STATUS_FAILURE;
    }

    // increase command sequence number
    ucCmdSeqNum = nicIncreaseCmdSeqNum(prAdapter);

    // compose CMD_802_11_KEY cmd pkt
    prCmdInfo->eCmdType = COMMAND_TYPE_NETWORK_IOCTL;
    prCmdInfo->eNetworkType = NETWORK_TYPE_AIS_INDEX;
    prCmdInfo->u2InfoBufLen = CMD_HDR_SIZE + sizeof(CMD_802_11_KEY);
    prCmdInfo->pfCmdDoneHandler = nicCmdEventSetCommon;
    prCmdInfo->pfCmdTimeoutHandler = nicOidCmdTimeoutCommon;
    prCmdInfo->fgIsOid = TRUE;
    prCmdInfo->ucCID = CMD_ID_ADD_REMOVE_KEY;
    prCmdInfo->fgSetQuery = TRUE;
    prCmdInfo->fgNeedResp = FALSE;
    prCmdInfo->fgDriverDomainMCR = FALSE;
    prCmdInfo->ucCmdSeqNum = ucCmdSeqNum;
    prCmdInfo->u4SetInfoLen = sizeof(PARAM_REMOVE_KEY_T);
    prCmdInfo->pvInformationBuffer = pvSetBuffer;
    prCmdInfo->u4InformationBufferLength = u4SetBufferLen;

    // Setup WIFI_CMD_T
    prWifiCmd = (P_WIFI_CMD_T)(prCmdInfo->pucInfoBuffer);
    prWifiCmd->u2TxByteCount_UserPriority = prCmdInfo->u2InfoBufLen;
    prWifiCmd->ucCID = prCmdInfo->ucCID;
    prWifiCmd->ucSetQuery = prCmdInfo->fgSetQuery;
    prWifiCmd->ucSeqNum = prCmdInfo->ucCmdSeqNum;

    prCmdKey = (P_CMD_802_11_KEY)(prWifiCmd->aucBuffer);

    kalMemZero((PUINT_8)prCmdKey, sizeof(CMD_802_11_KEY));

    prCmdKey->ucAddRemove = 0; /* Remove */
    prCmdKey->ucKeyId = (UINT_8)(prRemovedKey->u4KeyIndex & 0x000000ff);
    kalMemCopy(prCmdKey->aucPeerAddr, (PUINT_8)prRemovedKey->arBSSID, MAC_ADDR_LEN);

#if CFG_SUPPORT_802_11W
    ASSERT(prCmdKey->ucKeyId < MAX_KEY_NUM + 2);
#else
    //ASSERT(prCmdKey->ucKeyId < MAX_KEY_NUM);
#endif

    if (prRemovedKey->u4KeyIndex & IS_UNICAST_KEY) {
        prCmdKey->ucKeyType = 1;
    }

    // insert into prCmdQueue
    kalEnqueueCommand(prGlueInfo, (P_QUE_ENTRY_T)prCmdInfo);

    // wakeup txServiceThread later
    GLUE_SET_EVENT(prGlueInfo);

    return WLAN_STATUS_PENDING;
} /* wlanoidSetRemoveKey */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the current encryption status.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuffer A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufferLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryEncryptionStatus (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    )
{
    BOOLEAN               fgTransmitKeyAvailable = TRUE;
    ENUM_PARAM_ENCRYPTION_STATUS_T eEncStatus = 0;

    DEBUGFUNC("wlanoidQueryEncryptionStatus");

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    *pu4QueryInfoLen = sizeof(ENUM_PARAM_ENCRYPTION_STATUS_T);

    fgTransmitKeyAvailable = prAdapter->rWifiVar.rAisSpecificBssInfo.fgTransmitKeyExist;

    switch (prAdapter->rWifiVar.rConnSettings.eEncStatus) {
    case ENUM_ENCRYPTION3_ENABLED:
        if (fgTransmitKeyAvailable) {
            eEncStatus = ENUM_ENCRYPTION3_ENABLED;
        }
        else {
            eEncStatus = ENUM_ENCRYPTION3_KEY_ABSENT;
        }
        break;

    case ENUM_ENCRYPTION2_ENABLED:
        if (fgTransmitKeyAvailable) {
            eEncStatus = ENUM_ENCRYPTION2_ENABLED;
            break;
        }
        else {
            eEncStatus = ENUM_ENCRYPTION2_KEY_ABSENT;
        }
        break;

    case ENUM_ENCRYPTION1_ENABLED:
        if (fgTransmitKeyAvailable) {
            eEncStatus = ENUM_ENCRYPTION1_ENABLED;
        }
        else {
            eEncStatus = ENUM_ENCRYPTION1_KEY_ABSENT;
        }
        break;

    case ENUM_ENCRYPTION_DISABLED:
        eEncStatus = ENUM_ENCRYPTION_DISABLED;
        break;

    default:
        DBGLOG(REQ, ERROR, ("Unknown Encryption Status Setting:%d\n",
            prAdapter->rWifiVar.rConnSettings.eEncStatus));
    }

#if DBG
    DBGLOG(REQ, INFO,
        ("Encryption status: %d Return:%d\n",
        prAdapter->rWifiVar.rConnSettings.eEncStatus,
        eEncStatus));
#endif

    *(P_ENUM_PARAM_ENCRYPTION_STATUS_T)pvQueryBuffer = eEncStatus;

    return WLAN_STATUS_SUCCESS;
} /* wlanoidQueryEncryptionStatus */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set the encryption status to the driver.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_ADAPTER_NOT_READY
* \retval WLAN_STATUS_NOT_SUPPORTED
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetEncryptionStatus (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    P_GLUE_INFO_T         prGlueInfo;
    WLAN_STATUS           rStatus = WLAN_STATUS_SUCCESS;
    ENUM_PARAM_ENCRYPTION_STATUS_T eEewEncrypt;

    DEBUGFUNC("wlanoidSetEncryptionStatus");

    ASSERT(prAdapter);
    ASSERT(pvSetBuffer);
    ASSERT(pu4SetInfoLen);

    prGlueInfo = prAdapter->prGlueInfo;

    *pu4SetInfoLen = sizeof(ENUM_PARAM_ENCRYPTION_STATUS_T);

    //if (IS_ARB_IN_RFTEST_STATE(prAdapter)) {
    //  return WLAN_STATUS_SUCCESS;
    //}

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in set encryption status! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }

    eEewEncrypt = *(P_ENUM_PARAM_ENCRYPTION_STATUS_T)pvSetBuffer;
    DBGLOG(REQ, TRACE, ("ENCRYPTION_STATUS %d\n", eEewEncrypt));

    switch (eEewEncrypt) {
    case ENUM_ENCRYPTION_DISABLED: /* Disable WEP, TKIP, AES */
        DBGLOG(RSN, TRACE, ("Disable Encryption\n"));
        secSetCipherSuite(prAdapter,
            CIPHER_FLAG_WEP40  |
            CIPHER_FLAG_WEP104 |
            CIPHER_FLAG_WEP128);
        break;

    case ENUM_ENCRYPTION1_ENABLED: /* Enable WEP. Disable TKIP, AES */
        DBGLOG(RSN, TRACE, ("Enable Encryption1\n"));
        secSetCipherSuite(prAdapter,
            CIPHER_FLAG_WEP40  |
            CIPHER_FLAG_WEP104 |
            CIPHER_FLAG_WEP128);
        break;

    case ENUM_ENCRYPTION2_ENABLED: /* Enable WEP, TKIP. Disable AES */
        secSetCipherSuite(prAdapter,
            CIPHER_FLAG_WEP40  |
            CIPHER_FLAG_WEP104 |
            CIPHER_FLAG_WEP128 |
            CIPHER_FLAG_TKIP);
        DBGLOG(RSN, TRACE, ("Enable Encryption2\n"));
        break;

    case ENUM_ENCRYPTION3_ENABLED: /* Enable WEP, TKIP, AES */
        secSetCipherSuite(prAdapter,
            CIPHER_FLAG_WEP40  |
            CIPHER_FLAG_WEP104 |
            CIPHER_FLAG_WEP128 |
            CIPHER_FLAG_TKIP |
            CIPHER_FLAG_CCMP);
        DBGLOG(RSN, TRACE, ("Enable Encryption3\n"));
        break;

    default:
        DBGLOG(RSN, WARN, ("Unacceptible encryption status: %d\n",
            *(P_ENUM_PARAM_ENCRYPTION_STATUS_T)pvSetBuffer));

        rStatus = WLAN_STATUS_NOT_SUPPORTED;
    }

    if (rStatus == WLAN_STATUS_SUCCESS) {
        /* Save the new encryption status. */
        prAdapter->rWifiVar.rConnSettings.eEncStatus =
            *(P_ENUM_PARAM_ENCRYPTION_STATUS_T)pvSetBuffer;
    }

    return rStatus;
} /* wlanoidSetEncryptionStatus */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to test the driver.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_INVALID_DATA
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetTest (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    P_PARAM_802_11_TEST_T prTest;
    PVOID                 pvTestData;
    PVOID                 pvStatusBuffer;
    UINT_32               u4StatusBufferSize;

    DEBUGFUNC("wlanoidSetTest");

    ASSERT(prAdapter);

    ASSERT(pu4SetInfoLen);
    ASSERT(pvSetBuffer);

    *pu4SetInfoLen = u4SetBufferLen;

    prTest = (P_PARAM_802_11_TEST_T)pvSetBuffer;

    DBGLOG(REQ, TRACE, ("Test - Type %ld\n", prTest->u4Type));

    switch (prTest->u4Type) {
    case 1:     /* Type 1: generate an authentication event */
        pvTestData = (PVOID)&prTest->u.AuthenticationEvent;
        pvStatusBuffer = (PVOID)prAdapter->aucIndicationEventBuffer;
        u4StatusBufferSize = prTest->u4Length - 8;
        break;

    case 2:     /* Type 2: generate an RSSI status indication */
        pvTestData = (PVOID)&prTest->u.RssiTrigger;
        pvStatusBuffer = (PVOID)&prAdapter->rWlanInfo.rCurrBssId.rRssi;
        u4StatusBufferSize = sizeof(PARAM_RSSI);
        break;

    default:
        return WLAN_STATUS_INVALID_DATA;
    }

    ASSERT(u4StatusBufferSize <= 180);
    if (u4StatusBufferSize > 180) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    /* Get the contents of the StatusBuffer from the test structure. */
    kalMemCopy(pvStatusBuffer, pvTestData, u4StatusBufferSize);

    kalIndicateStatusAndComplete(prAdapter->prGlueInfo,
        WLAN_STATUS_MEDIA_SPECIFIC_INDICATION,
        pvStatusBuffer,
        u4StatusBufferSize);

    return WLAN_STATUS_SUCCESS;
} /* wlanoidSetTest */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the driver's WPA2 status.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuffer A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufferLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryCapability (
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    )
{
    P_PARAM_CAPABILITY_T  prCap;
    P_PARAM_AUTH_ENCRYPTION_T prAuthenticationEncryptionSupported;

    DEBUGFUNC("wlanoidQueryCapability");

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    *pu4QueryInfoLen = 4 * sizeof(UINT_32) + 14 * sizeof(PARAM_AUTH_ENCRYPTION_T);

    if (u4QueryBufferLen < *pu4QueryInfoLen) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    prCap = (P_PARAM_CAPABILITY_T)pvQueryBuffer;

    prCap->u4Length = *pu4QueryInfoLen;
    prCap->u4Version = 2; /* WPA2 */
    prCap->u4NoOfPMKIDs = CFG_MAX_PMKID_CACHE;
    prCap->u4NoOfAuthEncryptPairsSupported = 14;

    prAuthenticationEncryptionSupported =
        &prCap->arAuthenticationEncryptionSupported[0];

    // fill 14 entries of supported settings
    prAuthenticationEncryptionSupported[0].eAuthModeSupported =
        AUTH_MODE_OPEN;

    prAuthenticationEncryptionSupported[0].eEncryptStatusSupported =
        ENUM_ENCRYPTION_DISABLED;

    prAuthenticationEncryptionSupported[1].eAuthModeSupported =
        AUTH_MODE_OPEN;
    prAuthenticationEncryptionSupported[1].eEncryptStatusSupported =
        ENUM_ENCRYPTION1_ENABLED;

    prAuthenticationEncryptionSupported[2].eAuthModeSupported =
        AUTH_MODE_SHARED;
    prAuthenticationEncryptionSupported[2].eEncryptStatusSupported =
        ENUM_ENCRYPTION_DISABLED;

    prAuthenticationEncryptionSupported[3].eAuthModeSupported =
        AUTH_MODE_SHARED;
    prAuthenticationEncryptionSupported[3].eEncryptStatusSupported =
        ENUM_ENCRYPTION1_ENABLED;

    prAuthenticationEncryptionSupported[4].eAuthModeSupported =
        AUTH_MODE_WPA;
    prAuthenticationEncryptionSupported[4].eEncryptStatusSupported =
        ENUM_ENCRYPTION2_ENABLED;

    prAuthenticationEncryptionSupported[5].eAuthModeSupported =
        AUTH_MODE_WPA;
    prAuthenticationEncryptionSupported[5].eEncryptStatusSupported =
        ENUM_ENCRYPTION3_ENABLED;

    prAuthenticationEncryptionSupported[6].eAuthModeSupported =
        AUTH_MODE_WPA_PSK;
    prAuthenticationEncryptionSupported[6].eEncryptStatusSupported =
        ENUM_ENCRYPTION2_ENABLED;

    prAuthenticationEncryptionSupported[7].eAuthModeSupported =
        AUTH_MODE_WPA_PSK;
    prAuthenticationEncryptionSupported[7].eEncryptStatusSupported =
        ENUM_ENCRYPTION3_ENABLED;

    prAuthenticationEncryptionSupported[8].eAuthModeSupported =
        AUTH_MODE_WPA_NONE;
    prAuthenticationEncryptionSupported[8].eEncryptStatusSupported =
        ENUM_ENCRYPTION2_ENABLED;

    prAuthenticationEncryptionSupported[9].eAuthModeSupported =
        AUTH_MODE_WPA_NONE;
    prAuthenticationEncryptionSupported[9].eEncryptStatusSupported =
        ENUM_ENCRYPTION3_ENABLED;

    prAuthenticationEncryptionSupported[10].eAuthModeSupported =
        AUTH_MODE_WPA2;
    prAuthenticationEncryptionSupported[10].eEncryptStatusSupported =
        ENUM_ENCRYPTION2_ENABLED;

    prAuthenticationEncryptionSupported[11].eAuthModeSupported =
        AUTH_MODE_WPA2;
    prAuthenticationEncryptionSupported[11].eEncryptStatusSupported =
        ENUM_ENCRYPTION3_ENABLED;

    prAuthenticationEncryptionSupported[12].eAuthModeSupported =
        AUTH_MODE_WPA2_PSK;
    prAuthenticationEncryptionSupported[12].eEncryptStatusSupported =
        ENUM_ENCRYPTION2_ENABLED;

    prAuthenticationEncryptionSupported[13].eAuthModeSupported =
        AUTH_MODE_WPA2_PSK;
    prAuthenticationEncryptionSupported[13].eEncryptStatusSupported =
        ENUM_ENCRYPTION3_ENABLED;

    return WLAN_STATUS_SUCCESS;

} /* wlanoidQueryCapability */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the PMKID in the PMK cache.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuffer A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufferLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                             bytes written into the query buffer. If the call
*                             failed due to invalid length of the query buffer,
*                             returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryPmkid (
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    )
{
    UINT_32               i;
    P_PARAM_PMKID_T       prPmkid;
    P_AIS_SPECIFIC_BSS_INFO_T prAisSpecBssInfo;

    DEBUGFUNC("wlanoidQueryPmkid");

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    prAisSpecBssInfo = &prAdapter->rWifiVar.rAisSpecificBssInfo;

    *pu4QueryInfoLen = OFFSET_OF(PARAM_PMKID_T, arBSSIDInfo) +
        prAisSpecBssInfo->u4PmkidCacheCount * sizeof(PARAM_BSSID_INFO_T);

    if (u4QueryBufferLen < *pu4QueryInfoLen) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    prPmkid = (P_PARAM_PMKID_T)pvQueryBuffer;

    prPmkid->u4Length = *pu4QueryInfoLen;
    prPmkid->u4BSSIDInfoCount = prAisSpecBssInfo->u4PmkidCacheCount;

    for (i = 0; i < prAisSpecBssInfo->u4PmkidCacheCount; i++) {
        kalMemCopy(prPmkid->arBSSIDInfo[i].arBSSID,
            prAisSpecBssInfo->arPmkidCache[i].rBssidInfo.arBSSID,
            sizeof(PARAM_MAC_ADDRESS));
        kalMemCopy(prPmkid->arBSSIDInfo[i].arPMKID,
            prAisSpecBssInfo->arPmkidCache[i].rBssidInfo.arPMKID,
            sizeof(PARAM_PMKID_VALUE));
    }

    return WLAN_STATUS_SUCCESS;

} /* wlanoidQueryPmkid */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set the PMKID to the PMK cache in the driver.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_BUFFER_TOO_SHORT
* \retval WLAN_STATUS_INVALID_DATA
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetPmkid (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    UINT_32               i, j;
    P_PARAM_PMKID_T       prPmkid;
    P_AIS_SPECIFIC_BSS_INFO_T prAisSpecBssInfo;

    DEBUGFUNC("wlanoidSetPmkid");

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = u4SetBufferLen;

    /* It's possibble BSSIDInfoCount is zero, because OS wishes to clean PMKID */
    if (u4SetBufferLen < OFFSET_OF(PARAM_PMKID_T, arBSSIDInfo)) {
        return WLAN_STATUS_BUFFER_TOO_SHORT;
    }

    ASSERT(pvSetBuffer);
    prPmkid = (P_PARAM_PMKID_T)pvSetBuffer;

    if (u4SetBufferLen <
            ((prPmkid->u4BSSIDInfoCount * sizeof(PARAM_BSSID_INFO_T)) +
            OFFSET_OF(PARAM_PMKID_T, arBSSIDInfo))) {
        return WLAN_STATUS_INVALID_DATA;
    }

    if (prPmkid->u4BSSIDInfoCount > CFG_MAX_PMKID_CACHE) {
        return WLAN_STATUS_INVALID_DATA;
    }

    DBGLOG(REQ, INFO, ("Count %lu\n", prPmkid->u4BSSIDInfoCount));

    prAisSpecBssInfo = &prAdapter->rWifiVar.rAisSpecificBssInfo;

    /* This OID replace everything in the PMKID cache. */
    if (prPmkid->u4BSSIDInfoCount == 0) {
        prAisSpecBssInfo->u4PmkidCacheCount = 0;
        kalMemZero(prAisSpecBssInfo->arPmkidCache, sizeof(PMKID_ENTRY_T) * CFG_MAX_PMKID_CACHE);
    }
    if ((prAisSpecBssInfo->u4PmkidCacheCount + prPmkid->u4BSSIDInfoCount > CFG_MAX_PMKID_CACHE)) {
        prAisSpecBssInfo->u4PmkidCacheCount = 0;
        kalMemZero(prAisSpecBssInfo->arPmkidCache, sizeof(PMKID_ENTRY_T) * CFG_MAX_PMKID_CACHE);
    }

    /*
    The driver can only clear its PMKID cache whenever it make a media disconnect
    indication. Otherwise, it must change the PMKID cache only when set through this OID.
    */
#if CFG_RSN_MIGRATION
    for (i = 0; i < prPmkid->u4BSSIDInfoCount; i++) {
        /* Search for desired BSSID. If desired BSSID is found,
            then set the PMKID */
        if (!rsnSearchPmkidEntry(prAdapter,
                (PUINT_8)prPmkid->arBSSIDInfo[i].arBSSID,
                &j)) {
            /* No entry found for the specified BSSID, so add one entry */
            if (prAisSpecBssInfo->u4PmkidCacheCount < CFG_MAX_PMKID_CACHE - 1) {
                j = prAisSpecBssInfo->u4PmkidCacheCount;
                kalMemCopy(prAisSpecBssInfo->arPmkidCache[j].rBssidInfo.arBSSID,
                    prPmkid->arBSSIDInfo[i].arBSSID,
                    sizeof(PARAM_MAC_ADDRESS));
                prAisSpecBssInfo->u4PmkidCacheCount++;
            }
            else {
                j = CFG_MAX_PMKID_CACHE;
            }
        }

        if (j < CFG_MAX_PMKID_CACHE) {
            kalMemCopy(prAisSpecBssInfo->arPmkidCache[j].rBssidInfo.arPMKID,
                prPmkid->arBSSIDInfo[i].arPMKID,
                sizeof(PARAM_PMKID_VALUE));
            DBGLOG(RSN, TRACE, ("Add BSSID "MACSTR" idx=%d PMKID value "MACSTR"\n",
                MAC2STR(prAisSpecBssInfo->arPmkidCache[j].rBssidInfo.arBSSID),j,  MAC2STR(prAisSpecBssInfo->arPmkidCache[j].rBssidInfo.arPMKID)));
            prAisSpecBssInfo->arPmkidCache[j].fgPmkidExist = TRUE;
        }
    }
#endif
    return WLAN_STATUS_SUCCESS;

} /* wlanoidSetPmkid */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the set of supported data rates that
*          the radio is capable of running
*
* \param[in] prAdapter Pointer to the Adapter structure
* \param[out] pvQueryBuffer A pointer to the buffer that holds the result of
*                           the query
* \param[in] u4QueryBufferLen The length of the query buffer
* \param[out] pu4QueryInfoLen If the call is successful, returns the number
*                             of bytes written into the query buffer. If the
*                             call failed due to invalid length of the query
*                             buffer, returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQuerySupportedRates (
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    )
{
    PARAM_RATES eRate = {
        // BSSBasicRateSet for 802.11n Non-HT rates
        0x8C, // 6M
        0x92, // 9M
        0x98, // 12M
        0xA4, // 18M
        0xB0, // 24M
        0xC8, // 36M
        0xE0, // 48M
        0xEC  // 54M
    };

    DEBUGFUNC("wlanoidQuerySupportedRates");

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    *pu4QueryInfoLen = sizeof(PARAM_RATES_EX);

    if (u4QueryBufferLen < *pu4QueryInfoLen ) {
        DBGLOG(REQ, WARN, ("Invalid length %ld\n", u4QueryBufferLen));
        return WLAN_STATUS_INVALID_LENGTH;
    }

    kalMemCopy(pvQueryBuffer,
            (PVOID)&eRate,
            sizeof(PARAM_RATES));

    return WLAN_STATUS_SUCCESS;
} /* end of wlanoidQuerySupportedRates() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query current desired rates.
*
* \param[in] prAdapter          Pointer to the Adapter structure.
* \param[in] pvQueryBuffer      Pointer to the buffer that holds the result of
*                               the query.
* \param[in] u4QueryBufferLen   The length of the query buffer.
* \param[out] pu4QueryInfoLen   If the call is successful, returns the number of
*                               bytes written into the query buffer. If the call
*                               failed due to invalid length of the query buffer,
*                               returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryDesiredRates (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryDesiredRates");

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    *pu4QueryInfoLen = sizeof(PARAM_RATES_EX);

    if (u4QueryBufferLen < *pu4QueryInfoLen ) {
        DBGLOG(REQ, WARN, ("Invalid length %ld\n", u4QueryBufferLen));
        return WLAN_STATUS_INVALID_LENGTH;
    }

    kalMemCopy(pvQueryBuffer,
            (PVOID)&(prAdapter->rWlanInfo.eDesiredRates),
            sizeof(PARAM_RATES));

    return WLAN_STATUS_SUCCESS;

} /* end of wlanoidQueryDesiredRates() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to Set the desired rates.
*
* \param[in] prAdapter      Pointer to the Adapter structure.
* \param[in] pvSetBuffer    Pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_INVALID_DATA
* \retval WLAN_STATUS_ADAPTER_NOT_READY
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetDesiredRates (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    UINT_32 i;
    DEBUGFUNC("wlanoidSetDesiredRates");

    ASSERT(prAdapter);
    ASSERT(pvSetBuffer);
    ASSERT(pu4SetInfoLen);

    if (u4SetBufferLen < sizeof(PARAM_RATES)) {
        DBGLOG(REQ, WARN, ("Invalid length %ld\n", u4SetBufferLen));
        return WLAN_STATUS_INVALID_LENGTH;
    }

    *pu4SetInfoLen = sizeof(PARAM_RATES);

    if (u4SetBufferLen < sizeof(PARAM_RATES)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    kalMemCopy((PVOID)&(prAdapter->rWlanInfo.eDesiredRates),
            pvSetBuffer,
            sizeof(PARAM_RATES));

    prAdapter->rWlanInfo.eLinkAttr.ucDesiredRateLen = PARAM_MAX_LEN_RATES;
    for (i = 0 ; i < PARAM_MAX_LEN_RATES ; i++) {
        prAdapter->rWlanInfo.eLinkAttr.u2DesiredRate[i] =
            (UINT_16) (prAdapter->rWlanInfo.eDesiredRates[i]);
    }

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_LINK_ATTRIB,
            TRUE,
            FALSE,
            TRUE,
            nicCmdEventSetCommon,
            nicOidCmdTimeoutCommon,
            sizeof(CMD_LINK_ATTRIB),
            (PUINT_8)&(prAdapter->rWlanInfo.eLinkAttr),
            pvSetBuffer,
            u4SetBufferLen
            );

} /* end of wlanoidSetDesiredRates() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the maximum frame size in bytes,
*        not including the header.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuffer A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufferLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                               bytes written into the query buffer. If the
*                               call failed due to invalid length of the query
*                               buffer, returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryMaxFrameSize (
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryMaxFrameSize");


    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }


    if (u4QueryBufferLen < sizeof(UINT_32)) {
        *pu4QueryInfoLen = sizeof(UINT_32);
        return WLAN_STATUS_INVALID_LENGTH;
    }

    *(PUINT_32)pvQueryBuffer = ETHERNET_MAX_PKT_SZ - ETHERNET_HEADER_SZ;
    *pu4QueryInfoLen = sizeof(UINT_32);

    return WLAN_STATUS_SUCCESS;
} /* wlanoidQueryMaxFrameSize */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the maximum total packet length
*        in bytes.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuffer A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufferLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryMaxTotalSize (
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryMaxTotalSize");

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    if (u4QueryBufferLen < sizeof(UINT_32)) {
        *pu4QueryInfoLen = sizeof(UINT_32);
        return WLAN_STATUS_INVALID_LENGTH;
    }

    *(PUINT_32)pvQueryBuffer = ETHERNET_MAX_PKT_SZ;
    *pu4QueryInfoLen = sizeof(UINT_32);

    return WLAN_STATUS_SUCCESS;
} /* wlanoidQueryMaxTotalSize */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the vendor ID of the NIC.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuffer A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufferLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryVendorId (
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    )
{
#if DBG
    PUINT_8               cp;
#endif
    DEBUGFUNC("wlanoidQueryVendorId");

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    if (u4QueryBufferLen < sizeof(UINT_32)) {
        *pu4QueryInfoLen = sizeof(UINT_32);
        return WLAN_STATUS_INVALID_LENGTH;
    }

    kalMemCopy(pvQueryBuffer, prAdapter->aucMacAddress, 3);
    *((PUINT_8)pvQueryBuffer + 3) = 1;
    *pu4QueryInfoLen = sizeof(UINT_32);

#if DBG
    cp = (PUINT_8)pvQueryBuffer;
    DBGLOG(REQ, LOUD, ("Vendor ID=%02x-%02x-%02x-%02x\n", cp[0], cp[1], cp[2], cp[3]));
#endif

    return WLAN_STATUS_SUCCESS;
} /* wlanoidQueryVendorId */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the current RSSI value.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvQueryBuffer Pointer to the buffer that holds the result of the query.
* \param[in] u4QueryBufferLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*   bytes written into the query buffer. If the call failed due to invalid length of
*   the query buffer, returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_BUFFER_TOO_SHORT
* \retval WLAN_STATUS_ADAPTER_NOT_READY
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryRssi (
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryRssi");

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    *pu4QueryInfoLen = sizeof(PARAM_RSSI);

    /* Check for query buffer length */
    if (u4QueryBufferLen < *pu4QueryInfoLen) {
        DBGLOG(REQ, WARN, ("Too short length %ld\n", u4QueryBufferLen));
        return WLAN_STATUS_BUFFER_TOO_SHORT;
    }

    if (kalGetMediaStateIndicated(prAdapter->prGlueInfo) == PARAM_MEDIA_STATE_DISCONNECTED) {
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }
    else if (prAdapter->fgIsLinkQualityValid == TRUE &&
            (kalGetTimeTick() - prAdapter->rLinkQualityUpdateTime) <= CFG_LINK_QUALITY_VALID_PERIOD) {
        PARAM_RSSI rRssi;

        rRssi = (PARAM_RSSI)prAdapter->rLinkQuality.cRssi; // ranged from (-128 ~ 30) in unit of dBm

        if(rRssi > PARAM_WHQL_RSSI_MAX_DBM)
            rRssi = PARAM_WHQL_RSSI_MAX_DBM;
        else if(rRssi < PARAM_WHQL_RSSI_MIN_DBM)
            rRssi = PARAM_WHQL_RSSI_MIN_DBM;

        kalMemCopy(pvQueryBuffer, &rRssi, sizeof(PARAM_RSSI));
        return WLAN_STATUS_SUCCESS;
    }

    #ifdef LINUX
    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_GET_LINK_QUALITY,
            FALSE,
            TRUE,
            TRUE,
            nicCmdEventQueryLinkQuality,
            nicOidCmdTimeoutCommon,
            *pu4QueryInfoLen,
            pvQueryBuffer,
            pvQueryBuffer,
            u4QueryBufferLen
            );
    #else
    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_GET_LINK_QUALITY,
            FALSE,
            TRUE,
            TRUE,
            nicCmdEventQueryLinkQuality,
            nicOidCmdTimeoutCommon,
            0,
            NULL,
            pvQueryBuffer,
            u4QueryBufferLen
            );

    #endif
} /* end of wlanoidQueryRssi() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the current RSSI trigger value.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvQueryBuffer Pointer to the buffer that holds the result of the query.
* \param[in] u4QueryBufferLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*   bytes written into the query buffer. If the call failed due to invalid length of
*   the query buffer, returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_BUFFER_TOO_SHORT
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryRssiTrigger (
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryRssiTrigger");

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }


    if(prAdapter->rWlanInfo.eRssiTriggerType == ENUM_RSSI_TRIGGER_NONE)
        return WLAN_STATUS_ADAPTER_NOT_READY;

    *pu4QueryInfoLen = sizeof(PARAM_RSSI);

    /* Check for query buffer length */
    if (u4QueryBufferLen < *pu4QueryInfoLen) {
        DBGLOG(REQ, WARN, ("Too short length %ld\n", u4QueryBufferLen));
        return WLAN_STATUS_BUFFER_TOO_SHORT;
    }

    *(PARAM_RSSI *) pvQueryBuffer = prAdapter->rWlanInfo.rRssiTriggerValue;
    DBGLOG(REQ, INFO, ("RSSI trigger: %ld dBm\n", *(PARAM_RSSI *) pvQueryBuffer));

    return WLAN_STATUS_SUCCESS;
}   /* wlanoidQueryRssiTrigger */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set a trigger value of the RSSI event.
*
* \param[in] prAdapter Pointer to the Adapter structure
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns the
*                          amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
*
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetRssiTrigger (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    PARAM_RSSI rRssiTriggerValue;
    DEBUGFUNC("wlanoidSetRssiTrigger");

    ASSERT(prAdapter);
    ASSERT(pvSetBuffer);
    ASSERT(pu4SetInfoLen);


    *pu4SetInfoLen = sizeof(PARAM_RSSI);
    rRssiTriggerValue = *(PARAM_RSSI *) pvSetBuffer;

    if(rRssiTriggerValue > PARAM_WHQL_RSSI_MAX_DBM
            || rRssiTriggerValue < PARAM_WHQL_RSSI_MIN_DBM)
        return

    /* Save the RSSI trigger value to the Adapter structure */
    prAdapter->rWlanInfo.rRssiTriggerValue = rRssiTriggerValue;

    /* If the RSSI trigger value is equal to the current RSSI value, the
     * indication triggers immediately. We need to indicate the protocol
     * that an RSSI status indication event triggers. */
    if (rRssiTriggerValue == (PARAM_RSSI)(prAdapter->rLinkQuality.cRssi)) {
        prAdapter->rWlanInfo.eRssiTriggerType = ENUM_RSSI_TRIGGER_TRIGGERED;

        kalIndicateStatusAndComplete(prAdapter->prGlueInfo,
                WLAN_STATUS_MEDIA_SPECIFIC_INDICATION,
                (PVOID) &prAdapter->rWlanInfo.rRssiTriggerValue, sizeof(PARAM_RSSI));
    }
    else if(rRssiTriggerValue < (PARAM_RSSI)(prAdapter->rLinkQuality.cRssi))
        prAdapter->rWlanInfo.eRssiTriggerType = ENUM_RSSI_TRIGGER_GREATER;
    else if(rRssiTriggerValue > (PARAM_RSSI)(prAdapter->rLinkQuality.cRssi))
        prAdapter->rWlanInfo.eRssiTriggerType = ENUM_RSSI_TRIGGER_LESS;

    return WLAN_STATUS_SUCCESS;
}   /* wlanoidSetRssiTrigger */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set a suggested value for the number of
*        bytes of received packet data that will be indicated to the protocol
*        driver. We just accept the set and ignore this value.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetCurrentLookahead (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    DEBUGFUNC("wlanoidSetCurrentLookahead");

    ASSERT(prAdapter);
    ASSERT(pvSetBuffer);
    ASSERT(pu4SetInfoLen);

    if (u4SetBufferLen < sizeof(UINT_32)) {
        *pu4SetInfoLen = sizeof(UINT_32);
        return WLAN_STATUS_INVALID_LENGTH;
    }

    *pu4SetInfoLen = sizeof(UINT_32);
    return WLAN_STATUS_SUCCESS;
} /* wlanoidSetCurrentLookahead */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the number of frames that the driver
*        receives but does not indicate to the protocols due to errors.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuf A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_ADAPTER_NOT_READY
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryRcvError (
    IN  P_ADAPTER_T         prAdapter,
    IN  PVOID               pvQueryBuffer,
    IN  UINT_32             u4QueryBufferLen,
    OUT PUINT_32            pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryRcvError");
    DBGLOG(REQ, LOUD, ("\n"));

    ASSERT(prAdapter);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }
    ASSERT(pu4QueryInfoLen);

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in query receive error! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        *pu4QueryInfoLen = sizeof(UINT_32);
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }
    else if (u4QueryBufferLen < sizeof(UINT_32)
            || (u4QueryBufferLen > sizeof(UINT_32) && u4QueryBufferLen < sizeof(UINT_64))) {
        *pu4QueryInfoLen = sizeof(UINT_64);
        return WLAN_STATUS_INVALID_LENGTH;
    }

#if CFG_ENABLE_STATISTICS_BUFFERING
    if(IsBufferedStatisticsUsable(prAdapter) == TRUE) {
        // @FIXME, RX_ERROR_DROP_COUNT/RX_FIFO_FULL_DROP_COUNT is not calculated
        if(u4QueryBufferLen == sizeof(UINT_32)) {
            *pu4QueryInfoLen = sizeof(UINT_32);
            *(PUINT_32) pvQueryBuffer = (UINT_32) prAdapter->rStatStruct.rFCSErrorCount.QuadPart;
        }
        else {
            *pu4QueryInfoLen = sizeof(UINT_64);
            *(PUINT_64) pvQueryBuffer = (UINT_64) prAdapter->rStatStruct.rFCSErrorCount.QuadPart;
        }

        return WLAN_STATUS_SUCCESS;
    }
    else
#endif
    {
    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_GET_STATISTICS,
            FALSE,
            TRUE,
            TRUE,
            nicCmdEventQueryRecvError,
            nicOidCmdTimeoutCommon,
            0,
            NULL,
            pvQueryBuffer,
            u4QueryBufferLen
            );
    }
} /* wlanoidQueryRcvError */


/*----------------------------------------------------------------------------*/
/*! \brief This routine is called to query the number of frames that the NIC
*          cannot receive due to lack of NIC receive buffer space.
*
* \param[in] pvAdapter Pointer to the Adapter structure
* \param[in] pvQueryBuf A pointer to the buffer that holds the result of the
*                          query buffer
* \param[in] u4QueryBufLen The length of the query buffer
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS If success;
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_ADAPTER_NOT_READY
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryRcvNoBuffer (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryRcvNoBuffer");
    DBGLOG(REQ, LOUD, ("\n"));

    ASSERT(prAdapter);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }
    ASSERT(pu4QueryInfoLen);

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in query receive error! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        *pu4QueryInfoLen = sizeof(UINT_32);
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }
    else if (u4QueryBufferLen < sizeof(UINT_32)
            || (u4QueryBufferLen > sizeof(UINT_32) && u4QueryBufferLen < sizeof(UINT_64))) {
        *pu4QueryInfoLen = sizeof(UINT_64);
        return WLAN_STATUS_INVALID_LENGTH;
    }

#if CFG_ENABLE_STATISTICS_BUFFERING
    if(IsBufferedStatisticsUsable(prAdapter) == TRUE) {
        if(u4QueryBufferLen == sizeof(UINT_32)) {
            *pu4QueryInfoLen = sizeof(UINT_32);
            *(PUINT_32) pvQueryBuffer = (UINT_32) 0; //@FIXME
        }
        else {
            *pu4QueryInfoLen = sizeof(UINT_64);
            *(PUINT_64) pvQueryBuffer = (UINT_64) 0; //@FIXME
        }

        return WLAN_STATUS_SUCCESS;
    }
    else
#endif
    {
    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_GET_STATISTICS,
            FALSE,
            TRUE,
            TRUE,
            nicCmdEventQueryRecvNoBuffer,
            nicOidCmdTimeoutCommon,
            0,
            NULL,
            pvQueryBuffer,
            u4QueryBufferLen
            );
    }
}   /* wlanoidQueryRcvNoBuffer */


/*----------------------------------------------------------------------------*/
/*! \brief This routine is called to query the number of frames that the NIC
*          received and it is CRC error.
*
* \param[in] pvAdapter Pointer to the Adapter structure
* \param[in] pvQueryBuf A pointer to the buffer that holds the result of the
*                          query buffer
* \param[in] u4QueryBufLen The length of the query buffer
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS If success;
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_ADAPTER_NOT_READY
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryRcvCrcError (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryRcvCrcError");
    DBGLOG(REQ, LOUD, ("\n"));

    ASSERT(prAdapter);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }
    ASSERT(pu4QueryInfoLen);

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in query receive error! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        *pu4QueryInfoLen = sizeof(UINT_32);
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }
    else if (u4QueryBufferLen < sizeof(UINT_32)
            || (u4QueryBufferLen > sizeof(UINT_32) && u4QueryBufferLen < sizeof(UINT_64))) {
        *pu4QueryInfoLen = sizeof(UINT_64);
        return WLAN_STATUS_INVALID_LENGTH;
    }
#if CFG_ENABLE_STATISTICS_BUFFERING
    if(IsBufferedStatisticsUsable(prAdapter) == TRUE) {
        if(u4QueryBufferLen == sizeof(UINT_32)) {
            *pu4QueryInfoLen = sizeof(UINT_32);
            *(PUINT_32) pvQueryBuffer = (UINT_32) prAdapter->rStatStruct.rFCSErrorCount.QuadPart;
        }
        else {
            *pu4QueryInfoLen = sizeof(UINT_64);
            *(PUINT_64) pvQueryBuffer = (UINT_64) prAdapter->rStatStruct.rFCSErrorCount.QuadPart;
        }

        return WLAN_STATUS_SUCCESS;
    }
    else
#endif
    {
    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_GET_STATISTICS,
            FALSE,
            TRUE,
            TRUE,
            nicCmdEventQueryRecvCrcError,
            nicOidCmdTimeoutCommon,
            0,
            NULL,
            pvQueryBuffer,
            u4QueryBufferLen
            );
    }
}   /* wlanoidQueryRcvCrcError */


/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to query the current 802.11 statistics.
*
* \param[in] pvAdapter Pointer to the Adapter structure
* \param[in] pvQueryBuf A pointer to the buffer that holds the result of the
*                          query buffer
* \param[in] u4QueryBufLen The length of the query buffer
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryStatistics (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryStatistics");
    DBGLOG(REQ, LOUD, ("\n"));

    ASSERT(prAdapter);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }
    ASSERT(pu4QueryInfoLen);

    *pu4QueryInfoLen = sizeof(PARAM_802_11_STATISTICS_STRUCT_T);

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in query receive error! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        *pu4QueryInfoLen = sizeof(UINT_32);
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }
    else if (u4QueryBufferLen < sizeof(PARAM_802_11_STATISTICS_STRUCT_T)) {
        DBGLOG(REQ, WARN, ("Too short length %ld\n", u4QueryBufferLen));
        return WLAN_STATUS_INVALID_LENGTH;
    }

#if CFG_ENABLE_STATISTICS_BUFFERING
    if(IsBufferedStatisticsUsable(prAdapter) == TRUE) {
        P_PARAM_802_11_STATISTICS_STRUCT_T prStatistics;

        *pu4QueryInfoLen = sizeof(PARAM_802_11_STATISTICS_STRUCT_T);
        prStatistics = (P_PARAM_802_11_STATISTICS_STRUCT_T) pvQueryBuffer;

        prStatistics->u4Length = sizeof(PARAM_802_11_STATISTICS_STRUCT_T);
        prStatistics->rTransmittedFragmentCount
            = prAdapter->rStatStruct.rTransmittedFragmentCount;
        prStatistics->rMulticastTransmittedFrameCount
            = prAdapter->rStatStruct.rMulticastTransmittedFrameCount;
        prStatistics->rFailedCount
            = prAdapter->rStatStruct.rFailedCount;
        prStatistics->rRetryCount
            = prAdapter->rStatStruct.rRetryCount;
        prStatistics->rMultipleRetryCount
            = prAdapter->rStatStruct.rMultipleRetryCount;
        prStatistics->rRTSSuccessCount
            = prAdapter->rStatStruct.rRTSSuccessCount;
        prStatistics->rRTSFailureCount
            = prAdapter->rStatStruct.rRTSFailureCount;
        prStatistics->rACKFailureCount
            = prAdapter->rStatStruct.rACKFailureCount;
        prStatistics->rFrameDuplicateCount
            = prAdapter->rStatStruct.rFrameDuplicateCount;
        prStatistics->rReceivedFragmentCount
            = prAdapter->rStatStruct.rReceivedFragmentCount;
        prStatistics->rMulticastReceivedFrameCount
            = prAdapter->rStatStruct.rMulticastReceivedFrameCount;
        prStatistics->rFCSErrorCount
            = prAdapter->rStatStruct.rFCSErrorCount;
        prStatistics->rTKIPLocalMICFailures.QuadPart
            = 0;
        prStatistics->rTKIPICVErrors.QuadPart
            = 0;
        prStatistics->rTKIPCounterMeasuresInvoked.QuadPart
            = 0;
        prStatistics->rTKIPReplays.QuadPart
            = 0;
        prStatistics->rCCMPFormatErrors.QuadPart
            = 0;
        prStatistics->rCCMPReplays.QuadPart
            = 0;
        prStatistics->rCCMPDecryptErrors.QuadPart
            = 0;
        prStatistics->rFourWayHandshakeFailures.QuadPart
            = 0;
        prStatistics->rWEPUndecryptableCount.QuadPart
            = 0;
        prStatistics->rWEPICVErrorCount.QuadPart
            = 0;
        prStatistics->rDecryptSuccessCount.QuadPart
            = 0;
        prStatistics->rDecryptFailureCount.QuadPart
            = 0;

        return WLAN_STATUS_SUCCESS;
    }
    else
#endif
    {
    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_GET_STATISTICS,
            FALSE,
            TRUE,
            TRUE,
            nicCmdEventQueryStatistics,
            nicOidCmdTimeoutCommon,
            0,
            NULL,
            pvQueryBuffer,
            u4QueryBufferLen
            );
    }
}   /* wlanoidQueryStatistics */


/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to query current media streaming status.
*
* \param[in] pvAdapter Pointer to the Adapter structure
* \param[in] pvQueryBuf A pointer to the buffer that holds the result of the
*                          query buffer
* \param[in] u4QueryBufLen The length of the query buffer
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryMediaStreamMode(
    IN  P_ADAPTER_T     prAdapter,
    IN  PVOID           pvQueryBuffer,
    IN  UINT_32         u4QueryBufferLen,
    OUT PUINT_32        pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryMediaStreamMode");

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    *pu4QueryInfoLen = sizeof(ENUM_MEDIA_STREAM_MODE);

    if (u4QueryBufferLen < *pu4QueryInfoLen ) {
        DBGLOG(REQ, WARN, ("Invalid length %ld\n", u4QueryBufferLen));
        return WLAN_STATUS_INVALID_LENGTH;
    }

    *(P_ENUM_MEDIA_STREAM_MODE)pvQueryBuffer =
        prAdapter->rWlanInfo.eLinkAttr.ucMediaStreamMode == 0 ?
        ENUM_MEDIA_STREAM_OFF : ENUM_MEDIA_STREAM_ON;

    return WLAN_STATUS_SUCCESS;

}   /* wlanoidQueryMediaStreamMode */

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to enter media streaming mode or exit media streaming mode
*
* \param[in] pvAdapter Pointer to the Adapter structure
* \param[in] pvQueryBuf A pointer to the buffer that holds the result of the
*                          query buffer
* \param[in] u4QueryBufLen The length of the query buffer
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetMediaStreamMode(
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{
    ENUM_MEDIA_STREAM_MODE eStreamMode;

    DEBUGFUNC("wlanoidSetMediaStreamMode");

    ASSERT(prAdapter);
    ASSERT(pvSetBuffer);
    ASSERT(pu4SetInfoLen);

    if (u4SetBufferLen < sizeof(ENUM_MEDIA_STREAM_MODE)) {
        DBGLOG(REQ, WARN, ("Invalid length %ld\n", u4SetBufferLen));
        return WLAN_STATUS_INVALID_LENGTH;
    }

    *pu4SetInfoLen = sizeof(ENUM_MEDIA_STREAM_MODE);

    eStreamMode = *(P_ENUM_MEDIA_STREAM_MODE)pvSetBuffer;

    if(eStreamMode == ENUM_MEDIA_STREAM_OFF)
        prAdapter->rWlanInfo.eLinkAttr.ucMediaStreamMode = 0;
    else
        prAdapter->rWlanInfo.eLinkAttr.ucMediaStreamMode = 1;

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_LINK_ATTRIB,
            TRUE,
            FALSE,
            TRUE,
            nicCmdEventSetMediaStreamMode,
            nicOidCmdTimeoutCommon,
            sizeof(CMD_LINK_ATTRIB),
            (PUINT_8)&(prAdapter->rWlanInfo.eLinkAttr),
            pvSetBuffer,
            u4SetBufferLen
            );
}   /* wlanoidSetMediaStreamMode */

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to query the permanent MAC address of the NIC.
*
* \param[in] pvAdapter Pointer to the Adapter structure
* \param[in] pvQueryBuf A pointer to the buffer that holds the result of the
*                          query buffer
* \param[in] u4QueryBufLen The length of the query buffer
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryPermanentAddr (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID    pvQueryBuffer,
    IN  UINT_32  u4QueryBufferLen,
    OUT PUINT_32 pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryPermanentAddr");
    DBGLOG(INIT, LOUD, ("\n"));

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    if (u4QueryBufferLen < MAC_ADDR_LEN) {
        return WLAN_STATUS_BUFFER_TOO_SHORT;
    }

    COPY_MAC_ADDR(pvQueryBuffer, prAdapter->rWifiVar.aucPermanentAddress);
    *pu4QueryInfoLen = MAC_ADDR_LEN;

    return WLAN_STATUS_SUCCESS;
}   /* wlanoidQueryPermanentAddr */


/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to query the MAC address the NIC is currently using.
*
* \param[in] pvAdapter Pointer to the Adapter structure
* \param[in] pvQueryBuf A pointer to the buffer that holds the result of the
*                          query buffer
* \param[in] u4QueryBufLen The length of the query buffer
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_BUFFER_TOO_SHORT
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryCurrentAddr (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID    pvQueryBuffer,
    IN  UINT_32  u4QueryBufferLen,
    OUT PUINT_32 pu4QueryInfoLen
    )
{
    CMD_BASIC_CONFIG rCmdBasicConfig;

    DEBUGFUNC("wlanoidQueryCurrentAddr");
    DBGLOG(INIT, LOUD, ("\n"));

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    if (u4QueryBufferLen < MAC_ADDR_LEN) {
        return WLAN_STATUS_BUFFER_TOO_SHORT;
    }

    kalMemZero(&rCmdBasicConfig, sizeof(CMD_BASIC_CONFIG));

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_BASIC_CONFIG,
            FALSE,
            TRUE,
            TRUE,
            nicCmdEventQueryAddress,
            nicOidCmdTimeoutCommon,
            sizeof(CMD_BASIC_CONFIG),
            (PUINT_8)&rCmdBasicConfig,
            pvQueryBuffer,
            u4QueryBufferLen
            );

}   /* wlanoidQueryCurrentAddr */


/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to query NIC link speed.
*
* \param[in] pvAdapter Pointer to the Adapter structure
* \param[in] pvQueryBuf A pointer to the buffer that holds the result of the
*                          query buffer
* \param[in] u4QueryBufLen The length of the query buffer
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_BUFFER_TOO_SHORT
*
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryLinkSpeed(
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID    pvQueryBuffer,
    IN  UINT_32  u4QueryBufferLen,
    OUT PUINT_32 pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryLinkSpeed");


    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    *pu4QueryInfoLen = sizeof(UINT_32);

    if (u4QueryBufferLen < sizeof(UINT_32)) {
        return WLAN_STATUS_BUFFER_TOO_SHORT;
    }

    if (kalGetMediaStateIndicated(prAdapter->prGlueInfo) != PARAM_MEDIA_STATE_CONNECTED) {
        *(PUINT_32)pvQueryBuffer = 10000; // change to unit of 100bps
        return WLAN_STATUS_SUCCESS;
    }
    else if (prAdapter->fgIsLinkRateValid == TRUE &&
            (kalGetTimeTick() - prAdapter->rLinkRateUpdateTime) <= CFG_LINK_QUALITY_VALID_PERIOD) {
        *(PUINT_32)pvQueryBuffer = prAdapter->rLinkQuality.u2LinkSpeed * 5000; // change to unit of 100bps
        return WLAN_STATUS_SUCCESS;
    }
    else {
        return wlanSendSetQueryCmd(prAdapter,
                CMD_ID_GET_LINK_QUALITY,
                FALSE,
                TRUE,
                TRUE,
                nicCmdEventQueryLinkSpeed,
                nicOidCmdTimeoutCommon,
                0,
                NULL,
                pvQueryBuffer,
                u4QueryBufferLen
                );
    }
} /* end of wlanoidQueryLinkSpeed() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query MCR value.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuf A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryMcrRead (
    IN P_ADAPTER_T  prAdapter,
    IN PVOID        pvQueryBuffer,
    IN UINT_32      u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    )
{
    P_PARAM_CUSTOM_MCR_RW_STRUC_T prMcrRdInfo;
    CMD_ACCESS_REG rCmdAccessReg;

    DEBUGFUNC("wlanoidQueryMcrRead");
    DBGLOG(INIT, LOUD,("\n"));

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    *pu4QueryInfoLen = sizeof(PARAM_CUSTOM_MCR_RW_STRUC_T);

    if (u4QueryBufferLen < sizeof(PARAM_CUSTOM_MCR_RW_STRUC_T)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    prMcrRdInfo = (P_PARAM_CUSTOM_MCR_RW_STRUC_T)pvQueryBuffer;

    /* 0x9000 - 0x9EFF reserved for FW */
#if CFG_SUPPORT_SWCR
    if((prMcrRdInfo->u4McrOffset >>16) == 0x9F00) {
           swCrReadWriteCmd(prAdapter,
                    SWCR_READ,
                    (UINT_16) (prMcrRdInfo->u4McrOffset & BITS(0,15)),
                    &prMcrRdInfo->u4McrData);
            return WLAN_STATUS_SUCCESS;
    }
#endif /* CFG_SUPPORT_SWCR */

    /* Check if access F/W Domain MCR (due to WiFiSYS is placed from 0x6000-0000*/
    if (prMcrRdInfo->u4McrOffset & 0xFFFF0000){
        // fill command
        rCmdAccessReg.u4Address = prMcrRdInfo->u4McrOffset;
        rCmdAccessReg.u4Data = 0;

        return wlanSendSetQueryCmd(prAdapter,
                CMD_ID_ACCESS_REG,
                FALSE,
                TRUE,
                TRUE,
                nicCmdEventQueryMcrRead,
                nicOidCmdTimeoutCommon,
                sizeof(CMD_ACCESS_REG),
                (PUINT_8)&rCmdAccessReg,
                pvQueryBuffer,
                u4QueryBufferLen
                );
    }
    else {
        HAL_MCR_RD(prAdapter,
               prMcrRdInfo->u4McrOffset & BITS(2,31), //address is in DWORD unit
               &prMcrRdInfo->u4McrData);

        DBGLOG(INIT, TRACE, ("MCR Read: Offset = %#08lx, Data = %#08lx\n",
                    prMcrRdInfo->u4McrOffset, prMcrRdInfo->u4McrData));
        return WLAN_STATUS_SUCCESS;
    }
} /* end of wlanoidQueryMcrRead() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to write MCR and enable specific function.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetMcrWrite (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID    pvSetBuffer,
    IN  UINT_32  u4SetBufferLen,
    OUT PUINT_32 pu4SetInfoLen
    )
{
    P_PARAM_CUSTOM_MCR_RW_STRUC_T prMcrWrInfo;
    CMD_ACCESS_REG rCmdAccessReg;

#if CFG_STRESS_TEST_SUPPORT
    P_AIS_FSM_INFO_T prAisFsmInfo;
    P_BSS_INFO_T prBssInfo = &(prAdapter->rWifiVar.arBssInfo[(NETWORK_TYPE_AIS_INDEX)]);
    P_STA_RECORD_T prStaRec = prBssInfo->prStaRecOfAP;
    UINT_32 u4McrOffset, u4McrData;
#endif

    DEBUGFUNC("wlanoidSetMcrWrite");
    DBGLOG(INIT, LOUD,("\n"));

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(PARAM_CUSTOM_MCR_RW_STRUC_T);

    if (u4SetBufferLen < sizeof(PARAM_CUSTOM_MCR_RW_STRUC_T)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    ASSERT(pvSetBuffer);

    prMcrWrInfo = (P_PARAM_CUSTOM_MCR_RW_STRUC_T)pvSetBuffer;

    /* 0x9000 - 0x9EFF reserved for FW */
    /* 0xFFFE          reserved for FW */

    // -- Puff Stress Test Begin
#if CFG_STRESS_TEST_SUPPORT

    // 0xFFFFFFFE for Control Rate
    if (prMcrWrInfo->u4McrOffset == 0xFFFFFFFE){
        if(prMcrWrInfo->u4McrData < FIXED_RATE_NUM && prMcrWrInfo->u4McrData > 0){
            prAdapter->rWifiVar.eRateSetting = (ENUM_REGISTRY_FIXED_RATE_T)(prMcrWrInfo->u4McrData);
        }
        cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_1);
        cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_3);
        DEBUGFUNC("[Stress Test]Complete Rate is Changed...\n");
        DBGLOG(INIT, TRACE, ("[Stress Test] Rate is Changed to index %d...\n", prAdapter->rWifiVar.eRateSetting));
    }

    // 0xFFFFFFFD for Switch Channel
    else if (prMcrWrInfo->u4McrOffset == 0xFFFFFFFD){
        if(prMcrWrInfo->u4McrData <= 11 && prMcrWrInfo->u4McrData >= 1){
            prBssInfo->ucPrimaryChannel = prMcrWrInfo->u4McrData;
        }
        nicUpdateBss(prAdapter, prBssInfo->ucNetTypeIndex);
        DBGLOG(INIT, TRACE, ("[Stress Test] Channel is switched to %d ...\n", prBssInfo->ucPrimaryChannel));

        return WLAN_STATUS_SUCCESS;
    }

    // 0xFFFFFFFFC for Control RF Band and SCO
    else if(prMcrWrInfo->u4McrOffset == 0xFFFFFFFC){
        // Band
        if(prMcrWrInfo->u4McrData & 0x80000000){
            //prBssInfo->eBand = BAND_5G;
            //prBssInfo->ucPrimaryChannel = 52;  // Bond to Channel 52
        } else {
            prBssInfo->eBand = BAND_2G4;
            prBssInfo->ucPrimaryChannel = 8;   // Bond to Channel 6
        }

        // Bandwidth
        if(prMcrWrInfo->u4McrData & 0x00010000){
            prStaRec->u2HtCapInfo |= HT_CAP_INFO_SUP_CHNL_WIDTH;
            prStaRec->ucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;

            if(prMcrWrInfo->u4McrData == 0x00010002){
                prBssInfo->eBssSCO = CHNL_EXT_SCB; // U20
                prBssInfo->ucPrimaryChannel += 2;
            } else if (prMcrWrInfo->u4McrData == 0x00010001){
                prBssInfo->eBssSCO = CHNL_EXT_SCA; // L20
                prBssInfo->ucPrimaryChannel -= 2;
            } else {
                prBssInfo->eBssSCO = CHNL_EXT_SCA; // 40
            }
        }

        if(prMcrWrInfo->u4McrData & 0x00000000){
            prStaRec->u2HtCapInfo &= ~HT_CAP_INFO_SUP_CHNL_WIDTH;
            prBssInfo->eBssSCO = CHNL_EXT_SCN;
        }
        rlmBssInitForAPandIbss(prAdapter, prBssInfo);
    }

    // 0xFFFFFFFB for HT Capability
    else if (prMcrWrInfo->u4McrOffset == 0xFFFFFFFB){
        /* Enable HT Capability */
        if(prMcrWrInfo->u4McrData & 0x00000001){
            prStaRec->u2HtCapInfo |= HT_CAP_INFO_HT_GF;
            DEBUGFUNC("[Stress Test]Enable HT capability...\n");
        }else{
            prStaRec->u2HtCapInfo &= (~HT_CAP_INFO_HT_GF);
            DEBUGFUNC("[Stress Test]Disable HT capability...\n");
        }
        cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_1);
        cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_3);
    }

    // 0xFFFFFFFA for Enable Random Rx Reset
    else if (prMcrWrInfo->u4McrOffset == 0xFFFFFFFA){
        rCmdAccessReg.u4Address = prMcrWrInfo->u4McrOffset;
        rCmdAccessReg.u4Data = prMcrWrInfo->u4McrData;

        return wlanSendSetQueryCmd(
                prAdapter,
                CMD_ID_RANDOM_RX_RESET_EN,
                TRUE,
                FALSE,
                TRUE,
                nicCmdEventSetCommon,
                nicOidCmdTimeoutCommon,
                sizeof(CMD_ACCESS_REG),
                (PUINT_8)&rCmdAccessReg,
                pvSetBuffer,
                u4SetBufferLen
                );
    }

    // 0xFFFFFFF9 for Disable Random Rx Reset
    else if (prMcrWrInfo->u4McrOffset == 0xFFFFFFF9){
        rCmdAccessReg.u4Address = prMcrWrInfo->u4McrOffset;
        rCmdAccessReg.u4Data = prMcrWrInfo->u4McrData;

        return wlanSendSetQueryCmd(
                prAdapter,
                CMD_ID_RANDOM_RX_RESET_DE,
                TRUE,
                FALSE,
                TRUE,
                nicCmdEventSetCommon,
                nicOidCmdTimeoutCommon,
                sizeof(CMD_ACCESS_REG),
                (PUINT_8)&rCmdAccessReg,
                pvSetBuffer,
                u4SetBufferLen
                );
    }

    // 0xFFFFFFF8 for Enable SAPP
    else if (prMcrWrInfo->u4McrOffset == 0xFFFFFFF8){
        rCmdAccessReg.u4Address = prMcrWrInfo->u4McrOffset;
        rCmdAccessReg.u4Data = prMcrWrInfo->u4McrData;

        return wlanSendSetQueryCmd(
                prAdapter,
                CMD_ID_SAPP_EN,
                TRUE,
                FALSE,
                TRUE,
                nicCmdEventSetCommon,
                nicOidCmdTimeoutCommon,
                sizeof(CMD_ACCESS_REG),
                (PUINT_8)&rCmdAccessReg,
                pvSetBuffer,
                u4SetBufferLen
                );
    }

    // 0xFFFFFFF7 for Disable SAPP
    else if (prMcrWrInfo->u4McrOffset == 0xFFFFFFF7){
        rCmdAccessReg.u4Address = prMcrWrInfo->u4McrOffset;
        rCmdAccessReg.u4Data = prMcrWrInfo->u4McrData;

        return wlanSendSetQueryCmd(
                prAdapter,
                CMD_ID_SAPP_DE,
                TRUE,
                FALSE,
                TRUE,
                nicCmdEventSetCommon,
                nicOidCmdTimeoutCommon,
                sizeof(CMD_ACCESS_REG),
                (PUINT_8)&rCmdAccessReg,
                pvSetBuffer,
                u4SetBufferLen
                );
    }

    else
#endif
    // -- Puff Stress Test End


    /* Check if access F/W Domain MCR */
    if (prMcrWrInfo->u4McrOffset & 0xFFFF0000){

    /* 0x9000 - 0x9EFF reserved for FW */
#if CFG_SUPPORT_SWCR
        if((prMcrWrInfo->u4McrOffset >> 16) == 0x9F00) {
           swCrReadWriteCmd(prAdapter,
                    SWCR_WRITE,
                    (UINT_16) (prMcrWrInfo->u4McrOffset & BITS(0,15)),
                    &prMcrWrInfo->u4McrData);
            return WLAN_STATUS_SUCCESS;
        }
#endif /* CFG_SUPPORT_SWCR */


 #if 1
        // low power test special command
        if (prMcrWrInfo->u4McrOffset == 0x11111110){
            WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
            //DbgPrint("Enter test mode\n");
            prAdapter->fgTestMode = TRUE;
            return rStatus;
        }
        if (prMcrWrInfo->u4McrOffset == 0x11111111){
            //DbgPrint("nicpmSetAcpiPowerD3\n");

            nicpmSetAcpiPowerD3(prAdapter);
            kalDevSetPowerState(prAdapter->prGlueInfo, (UINT_32)ParamDeviceStateD3);
            return WLAN_STATUS_SUCCESS;
        }
        if (prMcrWrInfo->u4McrOffset == 0x11111112){

            //DbgPrint("LP enter sleep\n");

           // fill command
            rCmdAccessReg.u4Address = prMcrWrInfo->u4McrOffset;
            rCmdAccessReg.u4Data = prMcrWrInfo->u4McrData;

            return wlanSendSetQueryCmd(prAdapter,
                    CMD_ID_ACCESS_REG,
                    TRUE,
                    FALSE,
                    TRUE,
                    nicCmdEventSetCommon,
                    nicOidCmdTimeoutCommon,
                    sizeof(CMD_ACCESS_REG),
                    (PUINT_8)&rCmdAccessReg,
                    pvSetBuffer,
                    u4SetBufferLen
                    );
        }
#endif

 #if 1
        // low power test special command
        if (prMcrWrInfo->u4McrOffset == 0x11111110){
            WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
            //DbgPrint("Enter test mode\n");
            prAdapter->fgTestMode = TRUE;
            return rStatus;
        }
        if (prMcrWrInfo->u4McrOffset == 0x11111111){
            //DbgPrint("nicpmSetAcpiPowerD3\n");

            nicpmSetAcpiPowerD3(prAdapter);
            kalDevSetPowerState(prAdapter->prGlueInfo, (UINT_32)ParamDeviceStateD3);
            return WLAN_STATUS_SUCCESS;
        }
        if (prMcrWrInfo->u4McrOffset == 0x11111112){

            //DbgPrint("LP enter sleep\n");

           // fill command
            rCmdAccessReg.u4Address = prMcrWrInfo->u4McrOffset;
            rCmdAccessReg.u4Data = prMcrWrInfo->u4McrData;

            return wlanSendSetQueryCmd(prAdapter,
                    CMD_ID_ACCESS_REG,
                    TRUE,
                    FALSE,
                    TRUE,
                    nicCmdEventSetCommon,
                    nicOidCmdTimeoutCommon,
                    sizeof(CMD_ACCESS_REG),
                    (PUINT_8)&rCmdAccessReg,
                    pvSetBuffer,
                    u4SetBufferLen
                    );
        }

#endif
        // fill command
        rCmdAccessReg.u4Address = prMcrWrInfo->u4McrOffset;
        rCmdAccessReg.u4Data = prMcrWrInfo->u4McrData;

        return wlanSendSetQueryCmd(prAdapter,
                CMD_ID_ACCESS_REG,
                TRUE,
                FALSE,
                TRUE,
                nicCmdEventSetCommon,
                nicOidCmdTimeoutCommon,
                sizeof(CMD_ACCESS_REG),
                (PUINT_8)&rCmdAccessReg,
                pvSetBuffer,
                u4SetBufferLen
                );
    }
    else {
        HAL_MCR_WR(prAdapter,
               (prMcrWrInfo->u4McrOffset & BITS(2,31)), //address is in DWORD unit
               prMcrWrInfo->u4McrData);

        DBGLOG(INIT, TRACE, ("MCR Write: Offset = %#08lx, Data = %#08lx\n",
                    prMcrWrInfo->u4McrOffset, prMcrWrInfo->u4McrData));

        return WLAN_STATUS_SUCCESS;
    }
}   /* wlanoidSetMcrWrite */

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query SW CTRL
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuf A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQuerySwCtrlRead (
    IN P_ADAPTER_T  prAdapter,
    IN PVOID        pvQueryBuffer,
    IN UINT_32      u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    )
{
    P_PARAM_CUSTOM_SW_CTRL_STRUC_T prSwCtrlInfo;
    WLAN_STATUS rWlanStatus;
    UINT_16 u2Id, u2SubId;
    UINT_32 u4Data;

    CMD_SW_DBG_CTRL_T rCmdSwCtrl;

    DEBUGFUNC("wlanoidQuerySwCtrlRead");
    DBGLOG(INIT, LOUD,("\n"));

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    *pu4QueryInfoLen = sizeof(PARAM_CUSTOM_SW_CTRL_STRUC_T);

    if (u4QueryBufferLen < sizeof(PARAM_CUSTOM_SW_CTRL_STRUC_T)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    prSwCtrlInfo = (P_PARAM_CUSTOM_SW_CTRL_STRUC_T)pvQueryBuffer;

    u2Id = (UINT_16)(prSwCtrlInfo->u4Id >> 16);
    u2SubId = (UINT_16)(prSwCtrlInfo->u4Id & BITS(0,15));
    u4Data = 0;
    rWlanStatus = WLAN_STATUS_SUCCESS;

    switch(u2Id) {
    /* 0x9000 - 0x9EFF reserved for FW */
    /* 0xFFFE          reserved for FW */

#if CFG_SUPPORT_SWCR
        case 0x9F00:
           swCrReadWriteCmd(prAdapter,
                    SWCR_READ/* Read */,
                    (UINT_16) u2SubId ,
                    &u4Data);
            break;
#endif /* CFG_SUPPORT_SWCR */

        case 0xFFFF:
            {
                u4Data = 0x5AA56620;
            }
            break;

        case 0x9000:
        default:
            {
                rCmdSwCtrl.u4Id = prSwCtrlInfo->u4Id;
                rCmdSwCtrl.u4Data = 0;
                rWlanStatus = wlanSendSetQueryCmd(prAdapter,
                CMD_ID_SW_DBG_CTRL,
                FALSE,
                TRUE,
                TRUE,
                nicCmdEventQuerySwCtrlRead,
                nicOidCmdTimeoutCommon,
                sizeof(CMD_SW_DBG_CTRL_T),
                (PUINT_8)&rCmdSwCtrl,
                pvQueryBuffer,
                u4QueryBufferLen
                );
            }
    } /* switch(u2Id)*/

    prSwCtrlInfo->u4Data = u4Data;

    return rWlanStatus;

}
 /* end of wlanoidQuerySwCtrlRead() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to write SW CTRL
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetSwCtrlWrite (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID    pvSetBuffer,
    IN  UINT_32  u4SetBufferLen,
    OUT PUINT_32 pu4SetInfoLen
    )
{
    P_PARAM_CUSTOM_SW_CTRL_STRUC_T prSwCtrlInfo;
    CMD_SW_DBG_CTRL_T rCmdSwCtrl;
    WLAN_STATUS rWlanStatus;
    UINT_16 u2Id, u2SubId;
    UINT_32 u4Data;

    DEBUGFUNC("wlanoidSetSwCtrlWrite");
    DBGLOG(INIT, LOUD,("\n"));

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(PARAM_CUSTOM_SW_CTRL_STRUC_T);

    if (u4SetBufferLen < sizeof(PARAM_CUSTOM_SW_CTRL_STRUC_T)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    ASSERT(pvSetBuffer);

    prSwCtrlInfo = (P_PARAM_CUSTOM_SW_CTRL_STRUC_T)pvSetBuffer;

    u2Id = (UINT_16)(prSwCtrlInfo->u4Id >> 16);
    u2SubId = (UINT_16)(prSwCtrlInfo->u4Id & BITS(0,15));
    u4Data = prSwCtrlInfo->u4Data;
    rWlanStatus = WLAN_STATUS_SUCCESS;

    switch(u2Id) {

    /* 0x9000 - 0x9EFF reserved for FW */
    /* 0xFFFE          reserved for FW */

#if CFG_SUPPORT_SWCR
        case 0x9F00:
           swCrReadWriteCmd(prAdapter,
                    SWCR_WRITE,
                    (UINT_16) u2SubId,
                    &u4Data);
                break;
#endif /* CFG_SUPPORT_SWCR */

        case 0x1000:
            if (u2SubId == 0x8000) {
                // CTIA power save mode setting (code: 0x10008000)
                prAdapter->u4CtiaPowerMode = u4Data;
                prAdapter->fgEnCtiaPowerMode = TRUE;

                //
                {
                PARAM_POWER_MODE ePowerMode;

                if (prAdapter->u4CtiaPowerMode == 0) {
                    // force to keep in CAM mode
                    ePowerMode = Param_PowerModeCAM;
                } else if (prAdapter->u4CtiaPowerMode == 1) {
                    ePowerMode = Param_PowerModeMAX_PSP;
                } else {
                    ePowerMode = Param_PowerModeFast_PSP;
                }

                nicConfigPowerSaveProfile(
                    prAdapter,
                    NETWORK_TYPE_AIS_INDEX,
                    ePowerMode,
                    TRUE);
                }
            }
            break;
        case 0x1001:
            if(u2SubId == 0x0) {
                prAdapter->fgEnOnlineScan = (BOOLEAN)u4Data;
            }
            else if(u2SubId == 0x1) {
                prAdapter->fgDisBcnLostDetection = (BOOLEAN)u4Data;
            }
            else if(u2SubId == 0x2) {
                prAdapter->rWifiVar.fgSupportUAPSD =  (BOOLEAN)u4Data;
            }
            else if(u2SubId == 0x3) {
                prAdapter->u4UapsdAcBmp = u4Data & BITS(0,15);
                prAdapter->rWifiVar.arBssInfo[u4Data>>16].rPmProfSetupInfo.ucBmpDeliveryAC = (UINT_8)prAdapter->u4UapsdAcBmp;
                prAdapter->rWifiVar.arBssInfo[u4Data>>16].rPmProfSetupInfo.ucBmpTriggerAC = (UINT_8)prAdapter->u4UapsdAcBmp;
            }
            else if(u2SubId == 0x4) {
                prAdapter->fgDisStaAgingTimeoutDetection = (BOOLEAN)u4Data;
            }
            else if(u2SubId == 0x5) {
                prAdapter->rWifiVar.rConnSettings.uc2G4BandwidthMode = (UINT_8)u4Data;
            }


            break;

#if CFG_SUPPORT_SWCR
        case 0x1002:
            if(u2SubId == 0x0) {
                if (u4Data) {
                    u4Data = BIT(HIF_RX_PKT_TYPE_MANAGEMENT);
                }
                swCrFrameCheckEnable(prAdapter, u4Data);
            }
            else if(u2SubId == 0x1) {
                BOOLEAN fgIsEnable;
                UINT_8 ucType;
                UINT_32 u4Timeout;

                fgIsEnable = (BOOLEAN)(u4Data & 0xff);
                ucType = 0;//((u4Data>>4) & 0xf);
                u4Timeout = ((u4Data>>8) & 0xff);
                swCrDebugCheckEnable(prAdapter, fgIsEnable, ucType, u4Timeout);
            }
            break;
#endif

#if CFG_SUPPORT_802_11W
        case 0x2000:
            DBGLOG(RSN, INFO, ("802.11w test 0x%x\n", u2SubId));
            if (u2SubId == 0x0) {
                rsnStartSaQuery(prAdapter);
            }
            if (u2SubId == 0x1) {
                rsnStopSaQuery(prAdapter);
            }
            if (u2SubId == 0x2) {
                rsnSaQueryRequest(prAdapter, NULL);
            }
            if (u2SubId == 0x3) {
                P_BSS_INFO_T prBssInfo = &(prAdapter->rWifiVar.arBssInfo[(NETWORK_TYPE_AIS_INDEX)]);
                authSendDeauthFrame(prAdapter, prBssInfo->prStaRecOfAP , NULL, 7, NULL);
            }
            /* wext_set_mode */
            /*
            if (u2SubId == 0x3) {
                prAdapter->prGlueInfo->rWpaInfo.u4Mfp = RSN_AUTH_MFP_DISABLED;
            }
            if (u2SubId == 0x4) {
                //prAdapter->rWifiVar.rAisSpecificBssInfo.fgMgmtProtection = TRUE;
                prAdapter->prGlueInfo->rWpaInfo.u4Mfp = RSN_AUTH_MFP_OPTIONAL;
            }
            if (u2SubId == 0x5) {
                //prAdapter->rWifiVar.rAisSpecificBssInfo.fgMgmtProtection = TRUE;
                prAdapter->prGlueInfo->rWpaInfo.u4Mfp = RSN_AUTH_MFP_REQUIRED;
            }
            */
            break;
#endif
        case 0xFFFF:
            {
            CMD_ACCESS_REG rCmdAccessReg;
#if 1 //CFG_MT6573_SMT_TEST
            if (u2SubId == 0x0123) {

                DBGLOG(HAL, INFO, ("set smt fixed rate: %d \n", u4Data));

                if((ENUM_REGISTRY_FIXED_RATE_T)(u4Data) < FIXED_RATE_NUM) {
                    prAdapter->rWifiVar.eRateSetting = (ENUM_REGISTRY_FIXED_RATE_T)(u4Data);
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

                /* abort to re-connect */
#if 1
                kalIndicateStatusAndComplete(prAdapter->prGlueInfo,
                        WLAN_STATUS_MEDIA_DISCONNECT,
                        NULL,
                        0);
#else
                aisBssBeaconTimeout(prAdapter);
#endif

                return WLAN_STATUS_SUCCESS;

            }
            else if (u2SubId == 0x1234) {
                // 1. Disable On-Lin Scan
                prAdapter->fgEnOnlineScan = FALSE;

                // 3. Disable FIFO FULL no ack
                rCmdAccessReg.u4Address = 0x60140028;
                rCmdAccessReg.u4Data = 0x904;
                wlanSendSetQueryCmd(prAdapter,
                        CMD_ID_ACCESS_REG,
                        TRUE, //FALSE,
                        FALSE, //TRUE,
                        FALSE,
                        nicCmdEventSetCommon,
                        nicOidCmdTimeoutCommon,
                        sizeof(CMD_ACCESS_REG),
                        (PUINT_8)&rCmdAccessReg,
                        pvSetBuffer,
                        0
                        );

                // 4. Disable Roaming
                rCmdSwCtrl.u4Id = 0x90000204;
                rCmdSwCtrl.u4Data = 0x0;
                wlanSendSetQueryCmd(prAdapter,
                        CMD_ID_SW_DBG_CTRL,
                        TRUE,
                        FALSE,
                        FALSE,
                        nicCmdEventSetCommon,
                        nicOidCmdTimeoutCommon,
                        sizeof(CMD_SW_DBG_CTRL_T),
                        (PUINT_8)&rCmdSwCtrl,
                        pvSetBuffer,
                        u4SetBufferLen
                        );

                rCmdSwCtrl.u4Id = 0x90000200;
                rCmdSwCtrl.u4Data = 0x820000;
                wlanSendSetQueryCmd(prAdapter,
                        CMD_ID_SW_DBG_CTRL,
                        TRUE,
                        FALSE,
                        FALSE,
                        nicCmdEventSetCommon,
                        nicOidCmdTimeoutCommon,
                        sizeof(CMD_SW_DBG_CTRL_T),
                        (PUINT_8)&rCmdSwCtrl,
                        pvSetBuffer,
                        u4SetBufferLen
                        );

                // Disalbe auto tx power
                //
                rCmdSwCtrl.u4Id = 0xa0100003;
                rCmdSwCtrl.u4Data = 0x0;
                wlanSendSetQueryCmd(prAdapter,
                        CMD_ID_SW_DBG_CTRL,
                        TRUE,
                        FALSE,
                        FALSE,
                        nicCmdEventSetCommon,
                        nicOidCmdTimeoutCommon,
                        sizeof(CMD_SW_DBG_CTRL_T),
                        (PUINT_8)&rCmdSwCtrl,
                        pvSetBuffer,
                        u4SetBufferLen
                        );



                // 2. Keep at CAM mode
                {
                    PARAM_POWER_MODE ePowerMode;

                    prAdapter->u4CtiaPowerMode = 0;
                    prAdapter->fgEnCtiaPowerMode = TRUE;

                    ePowerMode = Param_PowerModeCAM;
                    rWlanStatus = nicConfigPowerSaveProfile(
                        prAdapter,
                        NETWORK_TYPE_AIS_INDEX,
                        ePowerMode,
                        TRUE);
                }

                // 5. Disable Beacon Timeout Detection
                prAdapter->fgDisBcnLostDetection = TRUE;
            }
            else if (u2SubId == 0x1235) {

                // 1. Enaable On-Lin Scan
                prAdapter->fgEnOnlineScan = TRUE;

                // 3. Enable FIFO FULL no ack
                rCmdAccessReg.u4Address = 0x60140028;
                rCmdAccessReg.u4Data = 0x905;
                wlanSendSetQueryCmd(prAdapter,
                        CMD_ID_ACCESS_REG,
                        TRUE, //FALSE,
                        FALSE, //TRUE,
                        FALSE,
                        nicCmdEventSetCommon,
                        nicOidCmdTimeoutCommon,
                        sizeof(CMD_ACCESS_REG),
                        (PUINT_8)&rCmdAccessReg,
                        pvSetBuffer,
                        0
                        );

                // 4. Enable Roaming
                rCmdSwCtrl.u4Id = 0x90000204;
                rCmdSwCtrl.u4Data = 0x1;
                wlanSendSetQueryCmd(prAdapter,
                        CMD_ID_SW_DBG_CTRL,
                        TRUE,
                        FALSE,
                        FALSE,
                        nicCmdEventSetCommon,
                        nicOidCmdTimeoutCommon,
                        sizeof(CMD_SW_DBG_CTRL_T),
                        (PUINT_8)&rCmdSwCtrl,
                        pvSetBuffer,
                        u4SetBufferLen
                        );

                rCmdSwCtrl.u4Id = 0x90000200;
                rCmdSwCtrl.u4Data = 0x820000;
                wlanSendSetQueryCmd(prAdapter,
                        CMD_ID_SW_DBG_CTRL,
                        TRUE,
                        FALSE,
                        FALSE,
                        nicCmdEventSetCommon,
                        nicOidCmdTimeoutCommon,
                        sizeof(CMD_SW_DBG_CTRL_T),
                        (PUINT_8)&rCmdSwCtrl,
                        pvSetBuffer,
                        u4SetBufferLen
                        );

                // Enable auto tx power
                //

                rCmdSwCtrl.u4Id = 0xa0100003;
                rCmdSwCtrl.u4Data = 0x1;
                wlanSendSetQueryCmd(prAdapter,
                        CMD_ID_SW_DBG_CTRL,
                        TRUE,
                        FALSE,
                        FALSE,
                        nicCmdEventSetCommon,
                        nicOidCmdTimeoutCommon,
                        sizeof(CMD_SW_DBG_CTRL_T),
                        (PUINT_8)&rCmdSwCtrl,
                        pvSetBuffer,
                        u4SetBufferLen
                        );


                // 2. Keep at Fast PS
                {
                    PARAM_POWER_MODE ePowerMode;

                    prAdapter->u4CtiaPowerMode = 2;
                    prAdapter->fgEnCtiaPowerMode = TRUE;

                    ePowerMode = Param_PowerModeFast_PSP;
                    rWlanStatus = nicConfigPowerSaveProfile(
                        prAdapter,
                        NETWORK_TYPE_AIS_INDEX,
                        ePowerMode,
                        TRUE);
                }

                // 5. Enable Beacon Timeout Detection
                prAdapter->fgDisBcnLostDetection = FALSE;
            }
#endif
            }
            break;

        case 0x9000:
        default:
            {
                rCmdSwCtrl.u4Id = prSwCtrlInfo->u4Id;
                rCmdSwCtrl.u4Data = prSwCtrlInfo->u4Data;
                rWlanStatus =  wlanSendSetQueryCmd(prAdapter,
                        CMD_ID_SW_DBG_CTRL,
                        TRUE,
                        FALSE,
                        TRUE,
                        nicCmdEventSetCommon,
                        nicOidCmdTimeoutCommon,
                        sizeof(CMD_SW_DBG_CTRL_T),
                        (PUINT_8)&rCmdSwCtrl,
                        pvSetBuffer,
                        u4SetBufferLen
                        );
            }
    } /* switch(u2Id)  */

    return rWlanStatus;
}
   /* wlanoidSetSwCtrlWrite */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query EEPROM value.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuf A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryEepromRead (
    IN  P_ADAPTER_T  prAdapter,
    IN  PVOID        pvQueryBuffer,
    IN  UINT_32      u4QueryBufferLen,
    OUT PUINT_32     pu4QueryInfoLen
    )
{
    P_PARAM_CUSTOM_EEPROM_RW_STRUC_T prEepromRwInfo;
    CMD_ACCESS_EEPROM rCmdAccessEeprom;

    DEBUGFUNC("wlanoidQueryEepromRead");

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    *pu4QueryInfoLen = sizeof(PARAM_CUSTOM_EEPROM_RW_STRUC_T);

    if (u4QueryBufferLen < sizeof(PARAM_CUSTOM_EEPROM_RW_STRUC_T)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    prEepromRwInfo = (P_PARAM_CUSTOM_EEPROM_RW_STRUC_T)pvQueryBuffer;

    kalMemZero(&rCmdAccessEeprom, sizeof(CMD_ACCESS_EEPROM));
    rCmdAccessEeprom.u2Offset = prEepromRwInfo->ucEepromIndex;

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_ACCESS_EEPROM,
            FALSE,
            TRUE,
            TRUE,
            nicCmdEventQueryEepromRead,
            nicOidCmdTimeoutCommon,
            sizeof(CMD_ACCESS_EEPROM),
            (PUINT_8)&rCmdAccessEeprom,
            pvQueryBuffer,
            u4QueryBufferLen
            );

}   /* wlanoidQueryEepromRead */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to write EEPROM value.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetEepromWrite (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{
    P_PARAM_CUSTOM_EEPROM_RW_STRUC_T prEepromRwInfo;
    CMD_ACCESS_EEPROM rCmdAccessEeprom;

    DEBUGFUNC("wlanoidSetEepromWrite");
    DBGLOG(INIT, LOUD,("\n"));

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(PARAM_CUSTOM_EEPROM_RW_STRUC_T);

    if (u4SetBufferLen < sizeof(PARAM_CUSTOM_EEPROM_RW_STRUC_T)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    ASSERT(pvSetBuffer);

    prEepromRwInfo = (P_PARAM_CUSTOM_EEPROM_RW_STRUC_T)pvSetBuffer;

    kalMemZero(&rCmdAccessEeprom, sizeof(CMD_ACCESS_EEPROM));
    rCmdAccessEeprom.u2Offset = prEepromRwInfo->ucEepromIndex;
    rCmdAccessEeprom.u2Data = prEepromRwInfo->u2EepromData;

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_ACCESS_EEPROM,
            TRUE,
            FALSE,
            TRUE,
            nicCmdEventSetCommon,
            nicOidCmdTimeoutCommon,
            sizeof(CMD_ACCESS_EEPROM),
            (PUINT_8)&rCmdAccessEeprom,
            pvSetBuffer,
            u4SetBufferLen
            );

}   /* wlanoidSetEepromWrite */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the number of the successfully transmitted
*        packets.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuf A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryXmitOk (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryXmitOk");
    DBGLOG(REQ, LOUD, ("\n"));

    ASSERT(prAdapter);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }
    ASSERT(pu4QueryInfoLen);

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in query receive error! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        *pu4QueryInfoLen = sizeof(UINT_32);
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }
    else if (u4QueryBufferLen < sizeof(UINT_32)
            || (u4QueryBufferLen > sizeof(UINT_32) && u4QueryBufferLen < sizeof(UINT_64))) {
        *pu4QueryInfoLen = sizeof(UINT_64);
        return WLAN_STATUS_INVALID_LENGTH;
    }

#if CFG_ENABLE_STATISTICS_BUFFERING
    if(IsBufferedStatisticsUsable(prAdapter) == TRUE) {
        if(u4QueryBufferLen == sizeof(UINT_32)) {
            *pu4QueryInfoLen = sizeof(UINT_32);
            *(PUINT_32) pvQueryBuffer = (UINT_32) prAdapter->rStatStruct.rTransmittedFragmentCount.QuadPart;
        }
        else {
            *pu4QueryInfoLen = sizeof(UINT_64);
            *(PUINT_64) pvQueryBuffer = (UINT_64) prAdapter->rStatStruct.rTransmittedFragmentCount.QuadPart;
        }

        return WLAN_STATUS_SUCCESS;
    }
    else
#endif
    {
    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_GET_STATISTICS,
            FALSE,
            TRUE,
            TRUE,
            nicCmdEventQueryXmitOk,
            nicOidCmdTimeoutCommon,
            0,
            NULL,
            pvQueryBuffer,
            u4QueryBufferLen
            );
    }
}   /* wlanoidQueryXmitOk */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the number of the successfully received
*        packets.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuf A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryRcvOk (
    IN  P_ADAPTER_T     prAdapter,
    IN  PVOID           pvQueryBuffer,
    IN  UINT_32         u4QueryBufferLen,
    OUT PUINT_32        pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryRcvOk");
    DBGLOG(REQ, LOUD, ("\n"));

    ASSERT(prAdapter);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }
    ASSERT(pu4QueryInfoLen);

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in query receive error! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        *pu4QueryInfoLen = sizeof(UINT_32);
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }
    else if (u4QueryBufferLen < sizeof(UINT_32)
            || (u4QueryBufferLen > sizeof(UINT_32) && u4QueryBufferLen < sizeof(UINT_64))) {
        *pu4QueryInfoLen = sizeof(UINT_64);
        return WLAN_STATUS_INVALID_LENGTH;
    }

#if CFG_ENABLE_STATISTICS_BUFFERING
    if(IsBufferedStatisticsUsable(prAdapter) == TRUE) {
        if(u4QueryBufferLen == sizeof(UINT_32)) {
            *pu4QueryInfoLen = sizeof(UINT_32);
            *(PUINT_32) pvQueryBuffer = (UINT_32) prAdapter->rStatStruct.rReceivedFragmentCount.QuadPart;
        }
        else {
            *pu4QueryInfoLen = sizeof(UINT_64);
            *(PUINT_64) pvQueryBuffer = (UINT_64) prAdapter->rStatStruct.rReceivedFragmentCount.QuadPart;
        }

        return WLAN_STATUS_SUCCESS;
    }
    else
#endif
    {
    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_GET_STATISTICS,
            FALSE,
            TRUE,
            TRUE,
            nicCmdEventQueryRecvOk,
            nicOidCmdTimeoutCommon,
            0,
            NULL,
            pvQueryBuffer,
            u4QueryBufferLen
            );
    }
}   /* wlanoidQueryRcvOk */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the number of frames that the driver
*        fails to transmit.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuf A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryXmitError (
    IN  P_ADAPTER_T     prAdapter,
    IN  PVOID           pvQueryBuffer,
    IN  UINT_32         u4QueryBufferLen,
    OUT PUINT_32        pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryXmitError");
    DBGLOG(REQ, LOUD, ("\n"));

    ASSERT(prAdapter);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }
    ASSERT(pu4QueryInfoLen);

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in query receive error! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        *pu4QueryInfoLen = sizeof(UINT_32);
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }
    else if (u4QueryBufferLen < sizeof(UINT_32)
            || (u4QueryBufferLen > sizeof(UINT_32) && u4QueryBufferLen < sizeof(UINT_64))) {
        *pu4QueryInfoLen = sizeof(UINT_64);
        return WLAN_STATUS_INVALID_LENGTH;
    }

#if CFG_ENABLE_STATISTICS_BUFFERING
    if(IsBufferedStatisticsUsable(prAdapter) == TRUE) {
        if(u4QueryBufferLen == sizeof(UINT_32)) {
            *pu4QueryInfoLen = sizeof(UINT_32);
            *(PUINT_32) pvQueryBuffer = (UINT_32) prAdapter->rStatStruct.rFailedCount.QuadPart;
        }
        else {
            *pu4QueryInfoLen = sizeof(UINT_64);
            *(PUINT_64) pvQueryBuffer = (UINT_64) prAdapter->rStatStruct.rFailedCount.QuadPart;
        }

        return WLAN_STATUS_SUCCESS;
    }
    else
#endif
    {
    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_GET_STATISTICS,
            FALSE,
            TRUE,
            TRUE,
            nicCmdEventQueryXmitError,
            nicOidCmdTimeoutCommon,
            0,
            NULL,
            pvQueryBuffer,
            u4QueryBufferLen
            );
    }
} /* wlanoidQueryXmitError */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the number of frames successfully
*        transmitted after exactly one collision.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuf A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryXmitOneCollision (
    IN  P_ADAPTER_T     prAdapter,
    IN  PVOID           pvQueryBuffer,
    IN  UINT_32         u4QueryBufferLen,
    OUT PUINT_32        pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryXmitOneCollision");
    DBGLOG(REQ, LOUD, ("\n"));

    ASSERT(prAdapter);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }
    ASSERT(pu4QueryInfoLen);

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in query receive error! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        *pu4QueryInfoLen = sizeof(UINT_32);
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }
    else if (u4QueryBufferLen < sizeof(UINT_32)
            || (u4QueryBufferLen > sizeof(UINT_32) && u4QueryBufferLen < sizeof(UINT_64))) {
        *pu4QueryInfoLen = sizeof(UINT_64);
        return WLAN_STATUS_INVALID_LENGTH;
    }

#if CFG_ENABLE_STATISTICS_BUFFERING
    if(IsBufferedStatisticsUsable(prAdapter) == TRUE) {
        if(u4QueryBufferLen == sizeof(UINT_32)) {
            *pu4QueryInfoLen = sizeof(UINT_32);
            *(PUINT_32) pvQueryBuffer = (UINT_32)
                (prAdapter->rStatStruct.rMultipleRetryCount.QuadPart - prAdapter->rStatStruct.rRetryCount.QuadPart);
        }
        else {
            *pu4QueryInfoLen = sizeof(UINT_64);
            *(PUINT_64) pvQueryBuffer = (UINT_64)
                (prAdapter->rStatStruct.rMultipleRetryCount.QuadPart - prAdapter->rStatStruct.rRetryCount.QuadPart);
        }

        return WLAN_STATUS_SUCCESS;
    }
    else
#endif
    {
    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_GET_STATISTICS,
            FALSE,
            TRUE,
            TRUE,
            nicCmdEventQueryXmitOneCollision,
            nicOidCmdTimeoutCommon,
            0,
            NULL,
            pvQueryBuffer,
            u4QueryBufferLen
            );
    }
} /* wlanoidQueryXmitOneCollision */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the number of frames successfully
*        transmitted after more than one collision.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuf A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryXmitMoreCollisions (
    IN  P_ADAPTER_T     prAdapter,
    IN  PVOID           pvQueryBuffer,
    IN  UINT_32         u4QueryBufferLen,
    OUT PUINT_32        pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryXmitMoreCollisions");
    DBGLOG(REQ, LOUD, ("\n"));

    ASSERT(prAdapter);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }
    ASSERT(pu4QueryInfoLen);

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in query receive error! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        *pu4QueryInfoLen = sizeof(UINT_32);
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }
    else if (u4QueryBufferLen < sizeof(UINT_32)
            || (u4QueryBufferLen > sizeof(UINT_32) && u4QueryBufferLen < sizeof(UINT_64))) {
        *pu4QueryInfoLen = sizeof(UINT_64);
        return WLAN_STATUS_INVALID_LENGTH;
    }

#if CFG_ENABLE_STATISTICS_BUFFERING
    if(IsBufferedStatisticsUsable(prAdapter) == TRUE) {
        if(u4QueryBufferLen == sizeof(UINT_32)) {
            *pu4QueryInfoLen = sizeof(UINT_32);
            *(PUINT_32) pvQueryBuffer = (UINT_32) (prAdapter->rStatStruct.rMultipleRetryCount.QuadPart);
        }
        else {
            *pu4QueryInfoLen = sizeof(UINT_64);
            *(PUINT_64) pvQueryBuffer = (UINT_64) (prAdapter->rStatStruct.rMultipleRetryCount.QuadPart);
        }

        return WLAN_STATUS_SUCCESS;
    }
    else
#endif
    {
    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_GET_STATISTICS,
            FALSE,
            TRUE,
            TRUE,
            nicCmdEventQueryXmitMoreCollisions,
            nicOidCmdTimeoutCommon,
            0,
            NULL,
            pvQueryBuffer,
            u4QueryBufferLen
            );
    }
} /* wlanoidQueryXmitMoreCollisions */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the number of frames
*                not transmitted due to excessive collisions.
*
* \param[in] prAdapter          Pointer to the Adapter structure.
* \param[in] pvQueryBuffer      Pointer to the buffer that holds the result of
*                               the query.
* \param[in] u4QueryBufferLen   The length of the query buffer.
* \param[out] pu4QueryInfoLen   If the call is successful, returns the number of
*                               bytes written into the query buffer. If the call
*                               failed due to invalid length of the query buffer,
*                               returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryXmitMaxCollisions (
    IN   P_ADAPTER_T     prAdapter,
    IN   PVOID           pvQueryBuffer,
    IN   UINT_32         u4QueryBufferLen,
    OUT  PUINT_32        pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryXmitMaxCollisions");
    DBGLOG(REQ, LOUD, ("\n"));

    ASSERT(prAdapter);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }
    ASSERT(pu4QueryInfoLen);

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in query receive error! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        *pu4QueryInfoLen = sizeof(UINT_32);
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }
    else if (u4QueryBufferLen < sizeof(UINT_32)
            || (u4QueryBufferLen > sizeof(UINT_32) && u4QueryBufferLen < sizeof(UINT_64))) {
        *pu4QueryInfoLen = sizeof(UINT_64);
        return WLAN_STATUS_INVALID_LENGTH;
    }

#if CFG_ENABLE_STATISTICS_BUFFERING
    if(IsBufferedStatisticsUsable(prAdapter) == TRUE) {
        if(u4QueryBufferLen == sizeof(UINT_32)) {
            *pu4QueryInfoLen = sizeof(UINT_32);
            *(PUINT_32) pvQueryBuffer = (UINT_32) prAdapter->rStatStruct.rFailedCount.QuadPart;
        }
        else {
            *pu4QueryInfoLen = sizeof(UINT_64);
            *(PUINT_64) pvQueryBuffer = (UINT_64) prAdapter->rStatStruct.rFailedCount.QuadPart;
        }

        return WLAN_STATUS_SUCCESS;
    }
    else
#endif
    {
    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_GET_STATISTICS,
            FALSE,
            TRUE,
            TRUE,
            nicCmdEventQueryXmitMaxCollisions,
            nicOidCmdTimeoutCommon,
            0,
            NULL,
            pvQueryBuffer,
            u4QueryBufferLen
            );
    }
}   /* wlanoidQueryXmitMaxCollisions */


#define MTK_CUSTOM_OID_INTERFACE_VERSION     0x00006620    // for WPDWifi DLL
/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query current the OID interface version,
*        which is the interface between the application and driver.
*
* \param[in] prAdapter          Pointer to the Adapter structure.
* \param[in] pvQueryBuffer      Pointer to the buffer that holds the result of
*                               the query.
* \param[in] u4QueryBufferLen   The length of the query buffer.
* \param[out] pu4QueryInfoLen   If the call is successful, returns the number of
*                               bytes written into the query buffer. If the call
*                               failed due to invalid length of the query buffer,
*                               returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryOidInterfaceVersion (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID    pvQueryBuffer,
    IN  UINT_32  u4QueryBufferLen,
    OUT PUINT_32 pu4QueryInfoLen)
{
    DEBUGFUNC("wlanoidQueryOidInterfaceVersion");

    ASSERT(prAdapter);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }
    ASSERT(pu4QueryInfoLen);

    *(PUINT_32) pvQueryBuffer = MTK_CUSTOM_OID_INTERFACE_VERSION ;
    *pu4QueryInfoLen = sizeof(UINT_32);

    DBGLOG(REQ, WARN, ("Custom OID interface version: %#08lX\n",
        *(PUINT_32) pvQueryBuffer));

    return WLAN_STATUS_SUCCESS;
}   /* wlanoidQueryOidInterfaceVersion */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query current Multicast Address List.
*
* \param[in] prAdapter          Pointer to the Adapter structure.
* \param[in] pvQueryBuffer      Pointer to the buffer that holds the result of
*                               the query.
* \param[in] u4QueryBufferLen   The length of the query buffer.
* \param[out] pu4QueryInfoLen   If the call is successful, returns the number of
*                               bytes written into the query buffer. If the call
*                               failed due to invalid length of the query buffer,
*                               returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_BUFFER_TOO_SHORT
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryMulticastList(
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    )
{
#ifndef LINUX
    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_MAC_MCAST_ADDR,
            FALSE,
            TRUE,
            TRUE,
            nicCmdEventQueryMcastAddr,
            nicOidCmdTimeoutCommon,
            0,
            NULL,
            pvQueryBuffer,
            u4QueryBufferLen
            );
#else
     return WLAN_STATUS_SUCCESS;
#endif
} /* end of wlanoidQueryMulticastList() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set Multicast Address List.
*
* \param[in] prAdapter      Pointer to the Adapter structure.
* \param[in] pvSetBuffer    Pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_ADAPTER_NOT_READY
* \retval WLAN_STATUS_MULTICAST_FULL
*/
/*----------------------------------------------------------------------------*/
 WLAN_STATUS
 wlanoidSetMulticastList(
     IN  P_ADAPTER_T prAdapter,
     IN  PVOID       pvSetBuffer,
     IN  UINT_32     u4SetBufferLen,
     OUT PUINT_32    pu4SetInfoLen
     )
 {
     UINT_8 ucNetTypeIndex = NETWORK_TYPE_AIS_INDEX; /* Caller should provide this information */
     CMD_MAC_MCAST_ADDR  rCmdMacMcastAddr;
     ASSERT(prAdapter);
     ASSERT(pu4SetInfoLen);

     /* The data must be a multiple of the Ethernet address size. */
     if ((u4SetBufferLen % MAC_ADDR_LEN)) {
         DBGLOG(REQ, WARN, ("Invalid MC list length %ld\n", u4SetBufferLen));

         *pu4SetInfoLen = (((u4SetBufferLen + MAC_ADDR_LEN) - 1) /
             MAC_ADDR_LEN) * MAC_ADDR_LEN;

         return WLAN_STATUS_INVALID_LENGTH;
     }

     *pu4SetInfoLen = u4SetBufferLen;

     /* Verify if we can support so many multicast addresses. */
     if ((u4SetBufferLen / MAC_ADDR_LEN) > MAX_NUM_GROUP_ADDR) {
         DBGLOG(REQ, WARN, ("Too many MC addresses\n"));

         return WLAN_STATUS_MULTICAST_FULL;
     }

     /* NOTE(Kevin): Windows may set u4SetBufferLen == 0 &&
      * pvSetBuffer == NULL to clear exist Multicast List.
      */
     if (u4SetBufferLen) {
         ASSERT(pvSetBuffer);
     }

     if (prAdapter->rAcpiState == ACPI_STATE_D3) {
         DBGLOG(REQ, WARN, ("Fail in set multicast list! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                     prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
         return WLAN_STATUS_ADAPTER_NOT_READY;
     }

     rCmdMacMcastAddr.u4NumOfGroupAddr = u4SetBufferLen / MAC_ADDR_LEN;
     rCmdMacMcastAddr.ucNetTypeIndex = ucNetTypeIndex;
     kalMemCopy(rCmdMacMcastAddr.arAddress, pvSetBuffer, u4SetBufferLen);

     return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_MAC_MCAST_ADDR,
            TRUE,
            FALSE,
            TRUE,
            nicCmdEventSetCommon,
            nicOidCmdTimeoutCommon,
            sizeof(CMD_MAC_MCAST_ADDR),
            (PUINT_8)&rCmdMacMcastAddr,
            pvSetBuffer,
            u4SetBufferLen
            );
} /* end of wlanoidSetMulticastList() */



/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set Packet Filter.
*
* \param[in] prAdapter      Pointer to the Adapter structure.
* \param[in] pvSetBuffer    Pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_NOT_SUPPORTED
* \retval WLAN_STATUS_ADAPTER_NOT_READY
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetCurrentPacketFilter (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID    pvSetBuffer,
    IN  UINT_32  u4SetBufferLen,
    OUT PUINT_32 pu4SetInfoLen
    )
{
    UINT_32 u4NewPacketFilter;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;

    DEBUGFUNC("wlanoidSetCurrentPacketFilter");

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    if (u4SetBufferLen < sizeof(UINT_32)) {
        *pu4SetInfoLen = sizeof(UINT_32);
        return WLAN_STATUS_INVALID_LENGTH;
    }
    ASSERT(pvSetBuffer);

    /* Set the new packet filter. */
    u4NewPacketFilter = *(PUINT_32) pvSetBuffer;

    DBGLOG(REQ, INFO, ("New packet filter: %#08lx\n", u4NewPacketFilter));

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in set current packet filter! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }

    do {
        /* Verify the bits of the new packet filter. If any bits are set that
           we don't support, leave. */
        if (u4NewPacketFilter & ~(PARAM_PACKET_FILTER_SUPPORTED)) {
            rStatus = WLAN_STATUS_NOT_SUPPORTED;
            break;
        }

#if DBG
        /* Need to enable or disable promiscuous support depending on the new
           filter. */
        if (u4NewPacketFilter & PARAM_PACKET_FILTER_PROMISCUOUS) {
            DBGLOG(REQ, INFO, ("Enable promiscuous mode\n"));
        }
        else {
            DBGLOG(REQ, INFO, ("Disable promiscuous mode\n"));
        }

        if (u4NewPacketFilter & PARAM_PACKET_FILTER_ALL_MULTICAST) {
            DBGLOG(REQ, INFO, ("Enable all-multicast mode\n"));
        }
        else if (u4NewPacketFilter & PARAM_PACKET_FILTER_MULTICAST) {
            DBGLOG(REQ, INFO, ("Enable multicast\n"));
        }
        else {
            DBGLOG(REQ, INFO, ("Disable multicast\n"));
        }

        if (u4NewPacketFilter & PARAM_PACKET_FILTER_BROADCAST) {
            DBGLOG(REQ, INFO, ("Enable Broadcast\n"));
        }
        else {
            DBGLOG(REQ, INFO, ("Disable Broadcast\n"));
        }
#endif
    } while (FALSE);

    if(rStatus == WLAN_STATUS_SUCCESS) {
        // Store the packet filter

        prAdapter->u4OsPacketFilter &= PARAM_PACKET_FILTER_P2P_MASK;
        prAdapter->u4OsPacketFilter |= u4NewPacketFilter;

        return wlanSendSetQueryCmd(prAdapter,
                CMD_ID_SET_RX_FILTER,
                TRUE,
                FALSE,
                TRUE,
                nicCmdEventSetCommon,
                nicOidCmdTimeoutCommon,
                sizeof(UINT_32),
                (PUINT_8)&prAdapter->u4OsPacketFilter,
                pvSetBuffer,
                u4SetBufferLen
                );
    }
    else {
        return rStatus;
    }
}   /* wlanoidSetCurrentPacketFilter */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query current packet filter.
*
* \param[in] prAdapter          Pointer to the Adapter structure.
* \param[in] pvQueryBuffer      Pointer to the buffer that holds the result of
*                               the query.
* \param[in] u4QueryBufferLen   The length of the query buffer.
* \param[out] pu4QueryInfoLen   If the call is successful, returns the number of
*                               bytes written into the query buffer. If the call
*                               failed due to invalid length of the query buffer,
*                               returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryCurrentPacketFilter (
    IN P_ADAPTER_T  prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryCurrentPacketFilter");
    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);

    *pu4QueryInfoLen = sizeof(UINT_32);

    if (u4QueryBufferLen >= sizeof(UINT_32)) {
        ASSERT(pvQueryBuffer);
        *(PUINT_32) pvQueryBuffer = prAdapter->u4OsPacketFilter;
    }

    return WLAN_STATUS_SUCCESS;
}   /* wlanoidQueryCurrentPacketFilter */

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query ACPI device power state.
*
* \param[in] prAdapter          Pointer to the Adapter structure.
* \param[in] pvQueryBuffer      Pointer to the buffer that holds the result of
*                               the query.
* \param[in] u4QueryBufferLen   The length of the query buffer.
* \param[out] pu4QueryInfoLen   If the call is successful, returns the number of
*                               bytes written into the query buffer. If the call
*                               failed due to invalid length of the query buffer,
*                               returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryAcpiDevicePowerState (
    IN P_ADAPTER_T prAdapter,
    IN  PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    )
{
#if DBG
    PPARAM_DEVICE_POWER_STATE prPowerState;
#endif

    DEBUGFUNC("wlanoidQueryAcpiDevicePowerState");
    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    *pu4QueryInfoLen = sizeof(PARAM_DEVICE_POWER_STATE);

#if DBG
    prPowerState = (PPARAM_DEVICE_POWER_STATE) pvQueryBuffer;
    switch (*prPowerState) {
    case ParamDeviceStateD0:
        DBGLOG(REQ, INFO, ("Query Power State: D0\n"));
        break;
    case ParamDeviceStateD1:
        DBGLOG(REQ, INFO, ("Query Power State: D1\n"));
        break;
    case ParamDeviceStateD2:
        DBGLOG(REQ, INFO, ("Query Power State: D2\n"));
        break;
    case ParamDeviceStateD3:
        DBGLOG(REQ, INFO, ("Query Power State: D3\n"));
        break;
    default:
        break;
    }
#endif

    /* Since we will disconnect the newwork, therefore we do not
       need to check queue empty */
    *(PPARAM_DEVICE_POWER_STATE) pvQueryBuffer = ParamDeviceStateD3;
    //WARNLOG(("Ready to transition to D3\n"));
    return WLAN_STATUS_SUCCESS;

}   /* pwrmgtQueryPower */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set ACPI device power state.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetAcpiDevicePowerState (
    IN P_ADAPTER_T prAdapter,
    IN  PVOID    pvSetBuffer,
    IN  UINT_32  u4SetBufferLen,
    OUT PUINT_32 pu4SetInfoLen
    )
{
    PPARAM_DEVICE_POWER_STATE prPowerState;
    BOOLEAN fgRetValue = TRUE;

    DEBUGFUNC("wlanoidSetAcpiDevicePowerState");
    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(PARAM_DEVICE_POWER_STATE);

    ASSERT(pvSetBuffer);
    prPowerState = (PPARAM_DEVICE_POWER_STATE) pvSetBuffer;
    switch (*prPowerState) {
    case ParamDeviceStateD0:
        DBGLOG(REQ, INFO, ("Set Power State: D0\n"));
        kalDevSetPowerState(prAdapter->prGlueInfo, (UINT_32)ParamDeviceStateD0);
        fgRetValue = nicpmSetAcpiPowerD0(prAdapter);
        break;
    case ParamDeviceStateD1:
        DBGLOG(REQ, INFO, ("Set Power State: D1\n"));
        /* no break here */
    case ParamDeviceStateD2:
        DBGLOG(REQ, INFO, ("Set Power State: D2\n"));
        /* no break here */
    case ParamDeviceStateD3:
        DBGLOG(REQ, INFO, ("Set Power State: D3\n"));
        fgRetValue = nicpmSetAcpiPowerD3(prAdapter);
        kalDevSetPowerState(prAdapter->prGlueInfo, (UINT_32)ParamDeviceStateD3);
        break;
    default:
        break;
    }

    if(fgRetValue == TRUE)
        return WLAN_STATUS_SUCCESS;
    else
        return WLAN_STATUS_FAILURE;
} /* end of wlanoidSetAcpiDevicePowerState() */

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the current fragmentation threshold.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuffer A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufferLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_BUFFER_TOO_SHORT
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryFragThreshold (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryFragThreshold");

    ASSERT(prAdapter);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }
    ASSERT(pu4QueryInfoLen);

    DBGLOG(REQ, LOUD, ("\n"));

#if CFG_TX_FRAGMENT

    return WLAN_STATUS_SUCCESS;

#else

    return WLAN_STATUS_NOT_SUPPORTED;
#endif /* CFG_TX_FRAGMENT */

} /* end of wlanoidQueryFragThreshold() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set a new fragmentation threshold to the
*        driver.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_DATA
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetFragThreshold (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{
#if CFG_TX_FRAGMENT

    return WLAN_STATUS_SUCCESS;

#else

    return WLAN_STATUS_NOT_SUPPORTED;
#endif /* CFG_TX_FRAGMENT */

} /* end of wlanoidSetFragThreshold() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the current RTS threshold.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuffer A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufferLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_BUFFER_TOO_SHORT
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryRtsThreshold (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryRtsThreshold");

    ASSERT(prAdapter);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }
    ASSERT(pu4QueryInfoLen);

    DBGLOG(REQ, LOUD, ("\n"));

    if (u4QueryBufferLen < sizeof(PARAM_RTS_THRESHOLD)) {
        *pu4QueryInfoLen = sizeof(PARAM_RTS_THRESHOLD);
        return WLAN_STATUS_BUFFER_TOO_SHORT;
    }

    *((PARAM_RTS_THRESHOLD *)pvQueryBuffer) = prAdapter->rWlanInfo.eRtsThreshold;

    return WLAN_STATUS_SUCCESS;

} /* wlanoidQueryRtsThreshold */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set a new RTS threshold to the driver.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_DATA
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetRtsThreshold (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{
    PARAM_RTS_THRESHOLD   *prRtsThreshold;

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(PARAM_RTS_THRESHOLD);
    if (u4SetBufferLen < sizeof(PARAM_RTS_THRESHOLD)) {
        DBGLOG(REQ, WARN, ("Invalid length %ld\n", u4SetBufferLen));
        return WLAN_STATUS_INVALID_LENGTH;
    }

    prRtsThreshold = (PARAM_RTS_THRESHOLD *)pvSetBuffer;
    *prRtsThreshold = prAdapter->rWlanInfo.eRtsThreshold;

    return WLAN_STATUS_SUCCESS;

} /* wlanoidSetRtsThreshold */

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to turn radio off.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_ADAPTER_NOT_READY
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetDisassociate (
    IN P_ADAPTER_T        prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    P_MSG_AIS_ABORT_T prAisAbortMsg;

    DEBUGFUNC("wlanoidSetDisassociate");

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = 0;

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in set disassociate! (Adapter not ready). ACPI=D%d, Radio=%d\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }

    /* prepare message to AIS */
    prAdapter->rWifiVar.rConnSettings.fgIsConnReqIssued = FALSE;

    /* Send AIS Abort Message */
    prAisAbortMsg = (P_MSG_AIS_ABORT_T)cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_AIS_ABORT_T));
    if (!prAisAbortMsg) {
        ASSERT(0);
        return WLAN_STATUS_FAILURE;
    }

    prAisAbortMsg->rMsgHdr.eMsgId = MID_OID_AIS_FSM_JOIN_REQ;
    prAisAbortMsg->ucReasonOfDisconnect = DISCONNECT_REASON_CODE_NEW_CONNECTION;
    prAisAbortMsg->fgDelayIndication = FALSE;

    mboxSendMsg(prAdapter,
            MBOX_ID_0,
            (P_MSG_HDR_T) prAisAbortMsg,
            MSG_SEND_METHOD_BUF);

    /* indicate for disconnection */
    if (kalGetMediaStateIndicated(prAdapter->prGlueInfo) == PARAM_MEDIA_STATE_CONNECTED) {
        kalIndicateStatusAndComplete(prAdapter->prGlueInfo,
                WLAN_STATUS_MEDIA_DISCONNECT,
                NULL,
                0);
    }

#if !defined(LINUX)
    prAdapter->fgIsRadioOff = TRUE;
#endif

    return WLAN_STATUS_SUCCESS;
} /* wlanoidSetDisassociate */



/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to query the power save profile.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuf A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \return WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQuery802dot11PowerSaveProfile (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQuery802dot11PowerSaveProfile");

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);

    if (u4QueryBufferLen!=0) {
        ASSERT(pvQueryBuffer);

//        *(PPARAM_POWER_MODE) pvQueryBuffer = (PARAM_POWER_MODE)(prAdapter->rWlanInfo.ePowerSaveMode.ucPsProfile);
        *(PPARAM_POWER_MODE) pvQueryBuffer = (PARAM_POWER_MODE)(prAdapter->rWlanInfo.arPowerSaveMode[NETWORK_TYPE_AIS_INDEX].ucPsProfile);
        *pu4QueryInfoLen = sizeof(PARAM_POWER_MODE);

        // hack for CTIA power mode setting function
        if (prAdapter->fgEnCtiaPowerMode) {
            // set to non-zero value (to prevent MMI query 0, before it intends to set 0, which will skip its following state machine)
            *(PPARAM_POWER_MODE) pvQueryBuffer = (PARAM_POWER_MODE)2;
        }
    }

    return WLAN_STATUS_SUCCESS;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to set the power save profile.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSet802dot11PowerSaveProfile (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{
    WLAN_STATUS status;
    PARAM_POWER_MODE ePowerMode;
    DEBUGFUNC("wlanoidSet802dot11PowerSaveProfile");

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(PARAM_POWER_MODE);
    if (u4SetBufferLen < sizeof(PARAM_POWER_MODE)) {
        DBGLOG(REQ, WARN, ("Invalid length %ld\n", u4SetBufferLen));
        return WLAN_STATUS_INVALID_LENGTH;
    }
    else if (*(PPARAM_POWER_MODE) pvSetBuffer >= Param_PowerModeMax) {
        //WARNLOG(("Invalid power mode %d\n",
                    //*(PPARAM_POWER_MODE) pvSetBuffer));
        return WLAN_STATUS_INVALID_DATA;
    }

    ePowerMode = *(PPARAM_POWER_MODE) pvSetBuffer;

    if (prAdapter->fgEnCtiaPowerMode) {
        if (ePowerMode == Param_PowerModeCAM) {

        } else {
            // User setting to PS mode (Param_PowerModeMAX_PSP or Param_PowerModeFast_PSP)

            if (prAdapter->u4CtiaPowerMode == 0) {
                // force to keep in CAM mode
                ePowerMode = Param_PowerModeCAM;
            } else if (prAdapter->u4CtiaPowerMode == 1) {
                ePowerMode = Param_PowerModeMAX_PSP;
            } else if (prAdapter->u4CtiaPowerMode == 2) {
                ePowerMode = Param_PowerModeFast_PSP;
            }
        }
    }

    status = nicConfigPowerSaveProfile(
        prAdapter,
        NETWORK_TYPE_AIS_INDEX,
        ePowerMode,
        TRUE);

    switch (ePowerMode) {
    case Param_PowerModeCAM:
        DBGLOG(INIT, INFO, ("Set Wi-Fi PS mode to CAM (%d)\n", ePowerMode));
        break;
    case Param_PowerModeMAX_PSP:
        DBGLOG(INIT, INFO, ("Set Wi-Fi PS mode to MAX PS (%d)\n", ePowerMode));
        break;
    case Param_PowerModeFast_PSP:
        DBGLOG(INIT, INFO, ("Set Wi-Fi PS mode to FAST PS (%d)\n", ePowerMode));
        break;
    default:
        DBGLOG(INIT, INFO, ("invalid Wi-Fi PS mode setting (%d)\n", ePowerMode));
        break;
    }

    return status;

} /* end of wlanoidSetAcpiDevicePowerStateMode() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query current status of AdHoc Mode.
*
* \param[in] prAdapter          Pointer to the Adapter structure.
* \param[in] pvQueryBuffer      Pointer to the buffer that holds the result of
*                               the query.
* \param[in] u4QueryBufferLen   The length of the query buffer.
* \param[out] pu4QueryInfoLen   If the call is successful, returns the number of
*                               bytes written into the query buffer. If the call
*                               failed due to invalid length of the query buffer,
*                               returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_BUFFER_TOO_SHORT
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryAdHocMode (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    )
{
    return WLAN_STATUS_SUCCESS;
} /* end of wlanoidQueryAdHocMode() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set AdHoc Mode.
*
* \param[in] prAdapter      Pointer to the Adapter structure.
* \param[in] pvSetBuffer    Pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_INVALID_DATA
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetAdHocMode (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{
    return WLAN_STATUS_SUCCESS;
} /* end of wlanoidSetAdHocMode() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query RF frequency.
*
* \param[in] prAdapter          Pointer to the Adapter structure.
* \param[in] pvQueryBuffer      Pointer to the buffer that holds the result of
*                               the query.
* \param[in] u4QueryBufferLen   The length of the query buffer.
* \param[out] pu4QueryInfoLen   If the call is successful, returns the number of
*                               bytes written into the query buffer. If the call
*                               failed due to invalid length of the query buffer,
*                               returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_BUFFER_TOO_SHORT
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryFrequency (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryFrequency");

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    if (u4QueryBufferLen < sizeof(UINT_32)) {
        return WLAN_STATUS_BUFFER_TOO_SHORT;
    }

    if (prAdapter->rWifiVar.rConnSettings.eOPMode == NET_TYPE_INFRA) {
        if (kalGetMediaStateIndicated(prAdapter->prGlueInfo) == PARAM_MEDIA_STATE_CONNECTED) {
            *(PUINT_32)pvQueryBuffer =
                nicChannelNum2Freq(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX].ucPrimaryChannel);
        }
        else {
            *(PUINT_32)pvQueryBuffer = 0;
        }
    }
    else {
        *(PUINT_32)pvQueryBuffer =
            nicChannelNum2Freq(prAdapter->rWifiVar.rConnSettings.ucAdHocChannelNum);
    }

    return WLAN_STATUS_SUCCESS;
} /* end of wlanoidQueryFrequency() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set RF frequency by User Settings.
*
* \param[in] prAdapter          Pointer to the Adapter structure.
* \param[in] pvQueryBuffer      Pointer to the buffer that holds the result of
*                               the query.
* \param[in] u4QueryBufferLen   The length of the query buffer.
* \param[out] pu4QueryInfoLen   If the call is successful, returns the number of
*                               bytes written into the query buffer. If the call
*                               failed due to invalid length of the query buffer,
*                               returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_INVALID_DATA
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetFrequency (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    PUINT_32 pu4FreqInKHz;

    DEBUGFUNC("wlanoidSetFrequency");

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(UINT_32);

    if (u4SetBufferLen < sizeof(UINT_32)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    ASSERT(pvSetBuffer);
    pu4FreqInKHz = (PUINT_32)pvSetBuffer;

    prAdapter->rWifiVar.rConnSettings.ucAdHocChannelNum
        = (UINT_8)nicFreq2ChannelNum(*pu4FreqInKHz);
    prAdapter->rWifiVar.rConnSettings.eAdHocBand
        = *pu4FreqInKHz < 5000000 ? BAND_2G4 : BAND_5G;

    return WLAN_STATUS_SUCCESS;
} /* end of wlanoidSetFrequency() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set 802.11 channel of the radio frequency.
*        This is a proprietary function call to Lunux currently.
*
* \param[in] prAdapter      Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_INVALID_DATA
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetChannel (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    ASSERT(0); ////

    return WLAN_STATUS_SUCCESS;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the Beacon Interval from User Settings.
*
* \param[in] prAdapter          Pointer to the Adapter structure.
* \param[in] pvQueryBuffer      Pointer to the buffer that holds the result of
*                               the query.
* \param[in] u4QueryBufferLen   The length of the query buffer.
* \param[out] pu4QueryInfoLen   If the call is successful, returns the number of
*                               bytes written into the query buffer. If the call
*                               failed due to invalid length of the query buffer,
*                               returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_BUFFER_TOO_SHORT
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryBeaconInterval (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryBeaconInterval");
    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    *pu4QueryInfoLen = sizeof(UINT_32);

    if (u4QueryBufferLen < sizeof(UINT_32)) {
        return WLAN_STATUS_BUFFER_TOO_SHORT;
    }

    if (kalGetMediaStateIndicated(prAdapter->prGlueInfo) == PARAM_MEDIA_STATE_CONNECTED) {
        if (prAdapter->rWifiVar.rConnSettings.eOPMode == NET_TYPE_INFRA) {
            *(PUINT_32)pvQueryBuffer =
                prAdapter->rWlanInfo.rCurrBssId.rConfiguration.u4BeaconPeriod;
        }
        else {
            *(PUINT_32)pvQueryBuffer =
                (UINT_32) prAdapter->rWlanInfo.u2BeaconPeriod;
        }
    }
    else {
        if (prAdapter->rWifiVar.rConnSettings.eOPMode == NET_TYPE_INFRA) {
            *(PUINT_32)pvQueryBuffer = 0;
        }
        else {
            *(PUINT_32)pvQueryBuffer =
                (UINT_32) prAdapter->rWlanInfo.u2BeaconPeriod;
        }
    }

    return WLAN_STATUS_SUCCESS;
} /* end of wlanoidQueryBeaconInterval() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set the Beacon Interval to User Settings.
*
* \param[in] prAdapter      Pointer to the Adapter structure.
* \param[in] pvSetBuffer    Pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_INVALID_DATA
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetBeaconInterval (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{
    PUINT_32 pu4BeaconInterval;

    DEBUGFUNC("wlanoidSetBeaconInterval");

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(UINT_32);
    if (u4SetBufferLen < sizeof(UINT_32)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    ASSERT(pvSetBuffer);
    pu4BeaconInterval = (PUINT_32)pvSetBuffer;

    if ((*pu4BeaconInterval < DOT11_BEACON_PERIOD_MIN) ||
            (*pu4BeaconInterval > DOT11_BEACON_PERIOD_MAX)) {
        DBGLOG(REQ, TRACE, ("Invalid Beacon Interval = %ld\n", *pu4BeaconInterval));
        return WLAN_STATUS_INVALID_DATA;
    }

    prAdapter->rWlanInfo.u2BeaconPeriod = (UINT_16)*pu4BeaconInterval;

    DBGLOG(REQ, INFO, ("Set beacon interval: %d\n",
                prAdapter->rWlanInfo.u2BeaconPeriod));


    return WLAN_STATUS_SUCCESS;
} /* end of wlanoidSetBeaconInterval() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the ATIM window from User Settings.
*
* \param[in] prAdapter          Pointer to the Adapter structure.
* \param[in] pvQueryBuffer      Pointer to the buffer that holds the result of
*                               the query.
* \param[in] u4QueryBufferLen   The length of the query buffer.
* \param[out] pu4QueryInfoLen   If the call is successful, returns the number of
*                               bytes written into the query buffer. If the call
*                               failed due to invalid length of the query buffer,
*                               returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_BUFFER_TOO_SHORT
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryAtimWindow (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    )
{
    DEBUGFUNC("wlanoidQueryAtimWindow");

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    *pu4QueryInfoLen = sizeof(UINT_32);

    if (u4QueryBufferLen < sizeof(UINT_32)) {
        return WLAN_STATUS_BUFFER_TOO_SHORT;
    }

    if (prAdapter->rWifiVar.rConnSettings.eOPMode == NET_TYPE_INFRA) {
        *(PUINT_32)pvQueryBuffer = 0;
    }
    else {
        *(PUINT_32)pvQueryBuffer =
            (UINT_32) prAdapter->rWlanInfo.u2AtimWindow;
    }

    return WLAN_STATUS_SUCCESS;

} /* end of wlanoidQueryAtimWindow() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set the ATIM window to User Settings.
*
* \param[in] prAdapter      Pointer to the Adapter structure.
* \param[in] pvSetBuffer    Pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_INVALID_DATA
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetAtimWindow (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{
    PUINT_32 pu4AtimWindow;

    DEBUGFUNC("wlanoidSetAtimWindow");

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(UINT_32);

    if (u4SetBufferLen < sizeof(UINT_32)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    ASSERT(pvSetBuffer);
    pu4AtimWindow = (PUINT_32)pvSetBuffer;

    prAdapter->rWlanInfo.u2AtimWindow = (UINT_16)*pu4AtimWindow;

    return WLAN_STATUS_SUCCESS;
} /* end of wlanoidSetAtimWindow() */

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to Set the MAC address which is currently used by the NIC.
*
* \param[in] prAdapter      Pointer to the Adapter structure.
* \param[in] pvSetBuffer    Pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_INVALID_DATA
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetCurrentAddr (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{
    ASSERT(0); ////

    return WLAN_STATUS_SUCCESS;
} /* end of wlanoidSetCurrentAddr() */


#if CFG_TCP_IP_CHKSUM_OFFLOAD
/*----------------------------------------------------------------------------*/
/*!
* \brief Setting the checksum offload function.
*
* \param[in] prAdapter      Pointer to the Adapter structure.
* \param[in] pvSetBuffer    Pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_ADAPTER_NOT_READY
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetCSUMOffload (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{
    UINT_32 i, u4CSUMFlags;
    CMD_BASIC_CONFIG rCmdBasicConfig;

    DEBUGFUNC("wlanoidSetCSUMOffload");
    DBGLOG(INIT, LOUD, ("\n"));

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(UINT_32);

    if (u4SetBufferLen < sizeof(UINT_32)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    ASSERT(pvSetBuffer);
    u4CSUMFlags = *(PUINT_32)pvSetBuffer;

    kalMemZero(&rCmdBasicConfig, sizeof(CMD_BASIC_CONFIG));

    for(i = 0 ; i < 6 ; i++) { // set to broadcast address for not-specified
        rCmdBasicConfig.rMyMacAddr[i] = 0xff;
    }

    rCmdBasicConfig.ucNative80211 = 0; //@FIXME: for Vista

    if(u4CSUMFlags & CSUM_OFFLOAD_EN_TX_TCP)
        rCmdBasicConfig.rCsumOffload.u2TxChecksum |= BIT(2);

    if(u4CSUMFlags & CSUM_OFFLOAD_EN_TX_UDP)
        rCmdBasicConfig.rCsumOffload.u2TxChecksum |= BIT(1);

    if(u4CSUMFlags & CSUM_OFFLOAD_EN_TX_IP)
        rCmdBasicConfig.rCsumOffload.u2TxChecksum |= BIT(0);

    if(u4CSUMFlags & CSUM_OFFLOAD_EN_RX_TCP)
        rCmdBasicConfig.rCsumOffload.u2RxChecksum |= BIT(2);

    if(u4CSUMFlags & CSUM_OFFLOAD_EN_RX_UDP)
        rCmdBasicConfig.rCsumOffload.u2RxChecksum |= BIT(1);

    if(u4CSUMFlags & (CSUM_OFFLOAD_EN_RX_IPv4 | CSUM_OFFLOAD_EN_RX_IPv6))
        rCmdBasicConfig.rCsumOffload.u2RxChecksum |= BIT(0);

    prAdapter->u4CSUMFlags = u4CSUMFlags;

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_BASIC_CONFIG,
            TRUE,
            FALSE,
            TRUE,
            nicCmdEventSetCommon,
            nicOidCmdTimeoutCommon,
            sizeof(CMD_BASIC_CONFIG),
            (PUINT_8)&rCmdBasicConfig,
            pvSetBuffer,
            u4SetBufferLen
            );
}
#endif /* CFG_TCP_IP_CHKSUM_OFFLOAD */


/*----------------------------------------------------------------------------*/
/*!
* \brief Setting the IP address for pattern search function.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \return WLAN_STATUS_SUCCESS
* \return WLAN_STATUS_ADAPTER_NOT_READY
* \return WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetNetworkAddress(
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 i, j;
    P_CMD_SET_NETWORK_ADDRESS_LIST prCmdNetworkAddressList;
    P_PARAM_NETWORK_ADDRESS_LIST prNetworkAddressList = (P_PARAM_NETWORK_ADDRESS_LIST)pvSetBuffer;
    P_PARAM_NETWORK_ADDRESS prNetworkAddress;
    P_PARAM_NETWORK_ADDRESS_IP prNetAddrIp;
    UINT_32 u4IpAddressCount, u4CmdSize;
	PUINT_8 pucBuf = (PUINT_8)pvSetBuffer;

    DEBUGFUNC("wlanoidSetNetworkAddress");
    DBGLOG(INIT, LOUD, ("\n"));

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = 4;

    if (u4SetBufferLen < sizeof(PARAM_NETWORK_ADDRESS_LIST)) {
        return WLAN_STATUS_INVALID_DATA;
    }

    *pu4SetInfoLen = 0;
    u4IpAddressCount = 0;

    prNetworkAddress = prNetworkAddressList->arAddress;
    for ( i = 0 ; i < prNetworkAddressList->u4AddressCount ; i++) {
        if (prNetworkAddress->u2AddressType == PARAM_PROTOCOL_ID_TCP_IP &&
                prNetworkAddress->u2AddressLength == sizeof(PARAM_NETWORK_ADDRESS_IP)) {
            u4IpAddressCount++;
        }

        prNetworkAddress = (P_PARAM_NETWORK_ADDRESS) ((UINT_32) prNetworkAddress +
            (UINT_32) (prNetworkAddress->u2AddressLength + OFFSET_OF(PARAM_NETWORK_ADDRESS, aucAddress)));
    }

    // construct payload of command packet
    u4CmdSize = OFFSET_OF(CMD_SET_NETWORK_ADDRESS_LIST, arNetAddress) +
        sizeof(IPV4_NETWORK_ADDRESS) * u4IpAddressCount;
	if (u4IpAddressCount == 0) {
		u4CmdSize = sizeof(CMD_SET_NETWORK_ADDRESS_LIST);
	}

    prCmdNetworkAddressList = (P_CMD_SET_NETWORK_ADDRESS_LIST) kalMemAlloc(u4CmdSize, VIR_MEM_TYPE);

    if(prCmdNetworkAddressList == NULL)
        return WLAN_STATUS_FAILURE;

    // fill P_CMD_SET_NETWORK_ADDRESS_LIST
    prCmdNetworkAddressList->ucNetTypeIndex = NETWORK_TYPE_AIS_INDEX;

    /* only to set IP address to FW once ARP filter is enabled */
    if (prAdapter->fgEnArpFilter) {
        prCmdNetworkAddressList->ucAddressCount = (UINT_8)u4IpAddressCount;
        prNetworkAddress = prNetworkAddressList->arAddress;

        DBGLOG(REQ, INFO, ("u4IpAddressCount (%d)\n", u4IpAddressCount));

        for (i = 0, j = 0 ; i < prNetworkAddressList->u4AddressCount ; i++) {
            if (prNetworkAddress->u2AddressType == PARAM_PROTOCOL_ID_TCP_IP &&
                    prNetworkAddress->u2AddressLength == sizeof(PARAM_NETWORK_ADDRESS_IP)) {
                prNetAddrIp = (P_PARAM_NETWORK_ADDRESS_IP)prNetworkAddress->aucAddress;

                kalMemCopy(prCmdNetworkAddressList->arNetAddress[j].aucIpAddr,
                        &(prNetAddrIp->in_addr),
                        sizeof(UINT_32));

                j++;

                pucBuf = (PUINT_8)&prNetAddrIp->in_addr;
                DBGLOG(REQ, INFO, ("prNetAddrIp->in_addr:%d:%d:%d:%d\n", pucBuf[0], pucBuf[1],pucBuf[2],pucBuf[3]));
            }

            prNetworkAddress = (P_PARAM_NETWORK_ADDRESS) ((UINT_32) prNetworkAddress +
                (UINT_32) (prNetworkAddress->u2AddressLength + OFFSET_OF(PARAM_NETWORK_ADDRESS, aucAddress)));
        }

    } else {
        prCmdNetworkAddressList->ucAddressCount = 0;
    }

    rStatus = wlanSendSetQueryCmd(prAdapter,
            CMD_ID_SET_IP_ADDRESS,
            TRUE,
            FALSE,
            TRUE,
            nicCmdEventSetIpAddress,
            nicOidCmdTimeoutCommon,
            u4CmdSize,
            (PUINT_8)prCmdNetworkAddressList,
            pvSetBuffer,
            u4SetBufferLen
            );

    kalMemFree(prCmdNetworkAddressList, VIR_MEM_TYPE, u4CmdSize);
    return rStatus;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief Set driver to switch into RF test mode
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set,
*                        should be NULL
* \param[in] u4SetBufferLen The length of the set buffer, should be 0
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \return WLAN_STATUS_SUCCESS
* \return WLAN_STATUS_ADAPTER_NOT_READY
* \return WLAN_STATUS_INVALID_DATA
* \return WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidRftestSetTestMode (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    WLAN_STATUS rStatus;
    CMD_TEST_CTRL_T rCmdTestCtrl;

    DEBUGFUNC("wlanoidRftestSetTestMode");

    ASSERT(prAdapter);
    ASSERT(pvSetBuffer);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = 0;

    if(u4SetBufferLen == 0) {
        if(prAdapter->fgTestMode == FALSE) {
            // switch to RF Test mode
            rCmdTestCtrl.ucAction = 0; // Switch mode
            rCmdTestCtrl.u.u4OpMode = 1; // RF test mode

            rStatus = wlanSendSetQueryCmd(prAdapter,
                    CMD_ID_TEST_MODE,
                    TRUE,
                    FALSE,
                    TRUE,
                    nicCmdEventEnterRfTest,
                    nicOidCmdEnterRFTestTimeout,
                    sizeof(CMD_TEST_CTRL_T),
                    (PUINT_8)&rCmdTestCtrl,
                    pvSetBuffer,
                    u4SetBufferLen);
        }
        else {
            // already in test mode ..
            rStatus = WLAN_STATUS_SUCCESS;
        }
    }
    else {
        rStatus = WLAN_STATUS_INVALID_DATA;
    }

    return rStatus;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief Set driver to switch into normal operation mode from RF test mode
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set
*                        should be NULL
* \param[in] u4SetBufferLen The length of the set buffer, should be 0
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \return WLAN_STATUS_SUCCESS
* \return WLAN_STATUS_ADAPTER_NOT_READY
* \return WLAN_STATUS_INVALID_DATA
* \return WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidRftestSetAbortTestMode (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    WLAN_STATUS rStatus;
    CMD_TEST_CTRL_T rCmdTestCtrl;

    DEBUGFUNC("wlanoidRftestSetTestMode");

    ASSERT(prAdapter);
    ASSERT(pvSetBuffer);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = 0;

    if(u4SetBufferLen == 0) {
        if(prAdapter->fgTestMode == TRUE) {
            // switch to normal mode
            rCmdTestCtrl.ucAction = 0; // Switch mode
            rCmdTestCtrl.u.u4OpMode = 0; // normal mode

            rStatus = wlanSendSetQueryCmd(prAdapter,
                    CMD_ID_TEST_MODE,
                    TRUE,
                    FALSE,
                    TRUE,
                    nicCmdEventLeaveRfTest,
                    nicOidCmdTimeoutCommon,
                    sizeof(CMD_TEST_CTRL_T),
                    (PUINT_8)&rCmdTestCtrl,
                    pvSetBuffer,
                    u4SetBufferLen);
        }
        else {
            // already in normal mode ..
            rStatus = WLAN_STATUS_SUCCESS;
        }
    }
    else {
        rStatus = WLAN_STATUS_INVALID_DATA;
    }

    return rStatus;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief query for RF test parameter
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvQueryBuffer      Pointer to the buffer that holds the result of
*                               the query.
* \param[in] u4QueryBufferLen   The length of the query buffer.
* \param[out] pu4QueryInfoLen   If the call is successful, returns the number of
*                               bytes written into the query buffer. If the call
*                               failed due to invalid length of the query buffer,
*                               returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_BUFFER_TOO_SHORT
* \retval WLAN_STATUS_NOT_SUPPORTED
* \retval WLAN_STATUS_NOT_ACCEPTED
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidRftestQueryAutoTest (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    )
{
    P_PARAM_MTK_WIFI_TEST_STRUC_T  prRfATInfo;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;

    DEBUGFUNC("wlanoidRftestQueryAutoTest");

    ASSERT(prAdapter);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }
    ASSERT(pu4QueryInfoLen);

    *pu4QueryInfoLen = sizeof(PARAM_MTK_WIFI_TEST_STRUC_T);

    if (u4QueryBufferLen != sizeof(PARAM_MTK_WIFI_TEST_STRUC_T)) {
        DBGLOG(REQ, ERROR, ("Invalid data. QueryBufferLen: %ld.\n",
                    u4QueryBufferLen));
        return WLAN_STATUS_INVALID_LENGTH;
    }

    prRfATInfo = (P_PARAM_MTK_WIFI_TEST_STRUC_T)pvQueryBuffer;
    rStatus = rftestQueryATInfo(prAdapter,
            prRfATInfo->u4FuncIndex,
            prRfATInfo->u4FuncData,
            pvQueryBuffer,
            u4QueryBufferLen);

    return rStatus;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief Set RF test parameter
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \return WLAN_STATUS_SUCCESS
* \return WLAN_STATUS_ADAPTER_NOT_READY
* \return WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidRftestSetAutoTest (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{
    P_PARAM_MTK_WIFI_TEST_STRUC_T  prRfATInfo;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;

    DEBUGFUNC("wlanoidRftestSetAutoTest");

    ASSERT(prAdapter);
    ASSERT(pvSetBuffer);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(PARAM_MTK_WIFI_TEST_STRUC_T);

    if (u4SetBufferLen != sizeof(PARAM_MTK_WIFI_TEST_STRUC_T)) {
        DBGLOG(REQ, ERROR, ("Invalid data. SetBufferLen: %ld.\n",
                    u4SetBufferLen));
        return WLAN_STATUS_INVALID_LENGTH;
    }

    prRfATInfo = (P_PARAM_MTK_WIFI_TEST_STRUC_T)pvSetBuffer;
    rStatus = rftestSetATInfo(prAdapter, prRfATInfo->u4FuncIndex, prRfATInfo->u4FuncData);

    return rStatus;
}

/* RF test OID set handler */
WLAN_STATUS
rftestSetATInfo (
    IN P_ADAPTER_T  prAdapter,
    UINT_32         u4FuncIndex,
    UINT_32         u4FuncData
    )
{
    P_GLUE_INFO_T prGlueInfo;
    P_CMD_INFO_T prCmdInfo;
    P_WIFI_CMD_T prWifiCmd;
    P_CMD_TEST_CTRL_T pCmdTestCtrl;
    UINT_8 ucCmdSeqNum;

    ASSERT(prAdapter);

    prGlueInfo = prAdapter->prGlueInfo;
    prCmdInfo = cmdBufAllocateCmdInfo(prAdapter, (CMD_HDR_SIZE + sizeof(CMD_TEST_CTRL_T)));

    if (!prCmdInfo) {
        DBGLOG(INIT, ERROR, ("Allocate CMD_INFO_T ==> FAILED.\n"));
        return WLAN_STATUS_FAILURE;
    }

    // increase command sequence number
    ucCmdSeqNum = nicIncreaseCmdSeqNum(prAdapter);

    // Setup common CMD Info Packet
    prCmdInfo->eCmdType = COMMAND_TYPE_GENERAL_IOCTL;
    prCmdInfo->u2InfoBufLen = CMD_HDR_SIZE + sizeof(CMD_TEST_CTRL_T);
    prCmdInfo->pfCmdDoneHandler = nicCmdEventSetCommon;
    prCmdInfo->pfCmdTimeoutHandler = nicOidCmdTimeoutCommon;
    prCmdInfo->fgIsOid = TRUE;
    prCmdInfo->ucCID = CMD_ID_TEST_MODE;
    prCmdInfo->fgSetQuery = TRUE;
    prCmdInfo->fgNeedResp = FALSE;
    prCmdInfo->fgDriverDomainMCR = FALSE;
    prCmdInfo->ucCmdSeqNum = ucCmdSeqNum;
    prCmdInfo->u4SetInfoLen = sizeof(CMD_TEST_CTRL_T);
    prCmdInfo->pvInformationBuffer = NULL;
    prCmdInfo->u4InformationBufferLength = 0;

    // Setup WIFI_CMD_T (payload = CMD_TEST_CTRL_T)
    prWifiCmd = (P_WIFI_CMD_T)(prCmdInfo->pucInfoBuffer);
    prWifiCmd->u2TxByteCount_UserPriority = prCmdInfo->u2InfoBufLen;
    prWifiCmd->ucCID = prCmdInfo->ucCID;
    prWifiCmd->ucSetQuery = prCmdInfo->fgSetQuery;
    prWifiCmd->ucSeqNum = prCmdInfo->ucCmdSeqNum;

    pCmdTestCtrl = (P_CMD_TEST_CTRL_T)(prWifiCmd->aucBuffer);
    pCmdTestCtrl->ucAction = 1; // Set ATInfo
    pCmdTestCtrl->u.rRfATInfo.u4FuncIndex = u4FuncIndex;
    pCmdTestCtrl->u.rRfATInfo.u4FuncData = u4FuncData;

    // insert into prCmdQueue
    kalEnqueueCommand(prGlueInfo, (P_QUE_ENTRY_T)prCmdInfo);

    // wakeup txServiceThread later
    GLUE_SET_EVENT(prAdapter->prGlueInfo);

    return WLAN_STATUS_PENDING;
}

WLAN_STATUS
rftestQueryATInfo(
    IN P_ADAPTER_T  prAdapter,
    UINT_32         u4FuncIndex,
    UINT_32         u4FuncData,
    OUT PVOID       pvQueryBuffer,
    IN UINT_32      u4QueryBufferLen
    )
{
    P_GLUE_INFO_T prGlueInfo;
    P_CMD_INFO_T prCmdInfo;
    P_WIFI_CMD_T prWifiCmd;
    P_CMD_TEST_CTRL_T pCmdTestCtrl;
    UINT_8 ucCmdSeqNum;
    P_EVENT_TEST_STATUS prTestStatus;

    ASSERT(prAdapter);

    prGlueInfo = prAdapter->prGlueInfo;

    if(u4FuncIndex == RF_AT_FUNCID_FW_INFO) {
        /* driver implementation */
        prTestStatus = (P_EVENT_TEST_STATUS)pvQueryBuffer;

        prTestStatus->rATInfo.u4FuncData =
            (prAdapter->rVerInfo.u2FwProductID << 16) | (prAdapter->rVerInfo.u2FwOwnVersion);
        u4QueryBufferLen = sizeof(EVENT_TEST_STATUS);

        return WLAN_STATUS_SUCCESS;
    }
    else if(u4FuncIndex == RF_AT_FUNCID_DRV_INFO) {
        /* driver implementation */
        prTestStatus = (P_EVENT_TEST_STATUS)pvQueryBuffer;

        prTestStatus->rATInfo.u4FuncData = CFG_DRV_OWN_VERSION;
        u4QueryBufferLen = sizeof(EVENT_TEST_STATUS);

        return WLAN_STATUS_SUCCESS;
    }
    else {
        prCmdInfo = cmdBufAllocateCmdInfo(prAdapter, (CMD_HDR_SIZE + sizeof(CMD_TEST_CTRL_T)));

        if (!prCmdInfo) {
            DBGLOG(INIT, ERROR, ("Allocate CMD_INFO_T ==> FAILED.\n"));
            return WLAN_STATUS_FAILURE;
        }

        // increase command sequence number
        ucCmdSeqNum = nicIncreaseCmdSeqNum(prAdapter);

        // Setup common CMD Info Packet
        prCmdInfo->eCmdType = COMMAND_TYPE_GENERAL_IOCTL;
        prCmdInfo->u2InfoBufLen = CMD_HDR_SIZE + sizeof(CMD_TEST_CTRL_T);
        prCmdInfo->pfCmdDoneHandler = nicCmdEventQueryRfTestATInfo;
        prCmdInfo->pfCmdTimeoutHandler = nicOidCmdTimeoutCommon;
        prCmdInfo->fgIsOid = TRUE;
        prCmdInfo->ucCID = CMD_ID_TEST_MODE;
        prCmdInfo->fgSetQuery = FALSE;
        prCmdInfo->fgNeedResp = TRUE;
        prCmdInfo->fgDriverDomainMCR = FALSE;
        prCmdInfo->ucCmdSeqNum = ucCmdSeqNum;
        prCmdInfo->u4SetInfoLen = sizeof(CMD_TEST_CTRL_T);
        prCmdInfo->pvInformationBuffer = pvQueryBuffer;
        prCmdInfo->u4InformationBufferLength = u4QueryBufferLen;

        // Setup WIFI_CMD_T (payload = CMD_TEST_CTRL_T)
        prWifiCmd = (P_WIFI_CMD_T)(prCmdInfo->pucInfoBuffer);
        prWifiCmd->u2TxByteCount_UserPriority = prCmdInfo->u2InfoBufLen;
        prWifiCmd->ucCID = prCmdInfo->ucCID;
        prWifiCmd->ucSetQuery = prCmdInfo->fgSetQuery;
        prWifiCmd->ucSeqNum = prCmdInfo->ucCmdSeqNum;

        pCmdTestCtrl = (P_CMD_TEST_CTRL_T)(prWifiCmd->aucBuffer);
        pCmdTestCtrl->ucAction = 2; // Get ATInfo
        pCmdTestCtrl->u.rRfATInfo.u4FuncIndex = u4FuncIndex;
        pCmdTestCtrl->u.rRfATInfo.u4FuncData = u4FuncData;

        // insert into prCmdQueue
        kalEnqueueCommand(prGlueInfo, (P_QUE_ENTRY_T)prCmdInfo);

        // wakeup txServiceThread later
        GLUE_SET_EVENT(prAdapter->prGlueInfo);

        return WLAN_STATUS_PENDING;
    }
}

WLAN_STATUS
rftestSetFrequency(
    IN P_ADAPTER_T  prAdapter,
    IN UINT_32      u4FreqInKHz,
    IN PUINT_32     pu4SetInfoLen
    )
{
    CMD_TEST_CTRL_T rCmdTestCtrl;

    ASSERT(prAdapter);

    rCmdTestCtrl.ucAction = 5; // Set Channel Frequency
    rCmdTestCtrl.u.u4ChannelFreq = u4FreqInKHz;

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_TEST_MODE,
            TRUE,
            FALSE,
            TRUE,
            nicCmdEventSetCommon,
            nicOidCmdTimeoutCommon,
            sizeof(CMD_TEST_CTRL_T),
            (PUINT_8)&rCmdTestCtrl,
            NULL,
            0);
}


/*----------------------------------------------------------------------------*/
/*!
* \brief command packet generation utility
*
* \param[in] prAdapter          Pointer to the Adapter structure.
* \param[in] ucCID              Command ID
* \param[in] fgSetQuery         Set or Query
* \param[in] fgNeedResp         Need for response
* \param[in] pfCmdDoneHandler   Function pointer when command is done
* \param[in] u4SetQueryInfoLen  The length of the set/query buffer
* \param[in] pucInfoBuffer      Pointer to set/query buffer
*
*
* \retval WLAN_STATUS_PENDING
* \retval WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanSendSetQueryCmd (
    IN P_ADAPTER_T  prAdapter,
    UINT_8          ucCID,
    BOOLEAN         fgSetQuery,
    BOOLEAN         fgNeedResp,
    BOOLEAN         fgIsOid,
    PFN_CMD_DONE_HANDLER pfCmdDoneHandler,
    PFN_CMD_TIMEOUT_HANDLER pfCmdTimeoutHandler,
    UINT_32         u4SetQueryInfoLen,
    PUINT_8         pucInfoBuffer,
    OUT PVOID       pvSetQueryBuffer,
    IN UINT_32      u4SetQueryBufferLen
    )
{
    P_GLUE_INFO_T prGlueInfo;
    P_CMD_INFO_T prCmdInfo;
    P_WIFI_CMD_T prWifiCmd;
    UINT_8 ucCmdSeqNum;

    prGlueInfo = prAdapter->prGlueInfo;
    prCmdInfo = cmdBufAllocateCmdInfo(prAdapter, (CMD_HDR_SIZE + u4SetQueryInfoLen));

    DEBUGFUNC("wlanSendSetQueryCmd");

    if (!prCmdInfo) {
        DBGLOG(INIT, ERROR, ("Allocate CMD_INFO_T ==> FAILED.\n"));
        return WLAN_STATUS_FAILURE;
    }

    // increase command sequence number
    ucCmdSeqNum = nicIncreaseCmdSeqNum(prAdapter);
    DBGLOG(REQ, TRACE, ("ucCmdSeqNum =%d\n", ucCmdSeqNum));

    // Setup common CMD Info Packet
    prCmdInfo->eCmdType = COMMAND_TYPE_NETWORK_IOCTL;
    prCmdInfo->eNetworkType = NETWORK_TYPE_AIS_INDEX;
    prCmdInfo->u2InfoBufLen = (UINT_16)(CMD_HDR_SIZE + u4SetQueryInfoLen);
    prCmdInfo->pfCmdDoneHandler = pfCmdDoneHandler;
    prCmdInfo->pfCmdTimeoutHandler = pfCmdTimeoutHandler;
    prCmdInfo->fgIsOid = fgIsOid;
    prCmdInfo->ucCID = ucCID;
    prCmdInfo->fgSetQuery = fgSetQuery;
    prCmdInfo->fgNeedResp = fgNeedResp;
    prCmdInfo->fgDriverDomainMCR = FALSE;
    prCmdInfo->ucCmdSeqNum = ucCmdSeqNum;
    prCmdInfo->u4SetInfoLen = u4SetQueryInfoLen;
    prCmdInfo->pvInformationBuffer = pvSetQueryBuffer;
    prCmdInfo->u4InformationBufferLength = u4SetQueryBufferLen;

    // Setup WIFI_CMD_T (no payload)
    prWifiCmd = (P_WIFI_CMD_T)(prCmdInfo->pucInfoBuffer);
    prWifiCmd->u2TxByteCount_UserPriority = prCmdInfo->u2InfoBufLen;
    prWifiCmd->ucCID = prCmdInfo->ucCID;
    prWifiCmd->ucSetQuery = prCmdInfo->fgSetQuery;
    prWifiCmd->ucSeqNum = prCmdInfo->ucCmdSeqNum;

    if(u4SetQueryInfoLen > 0 && pucInfoBuffer != NULL) {
        kalMemCopy(prWifiCmd->aucBuffer, pucInfoBuffer, u4SetQueryInfoLen);
    }

    // insert into prCmdQueue
    kalEnqueueCommand(prGlueInfo, (P_QUE_ENTRY_T)prCmdInfo);

    // wakeup txServiceThread later
    GLUE_SET_EVENT(prGlueInfo);
    return WLAN_STATUS_PENDING;
}



#if CFG_SUPPORT_WAPI
/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called by WAPI ui to set wapi mode, which is needed to info the the driver
*          to operation at WAPI mode while driver initialize.
*
* \param[in] prAdapter Pointer to the Adapter structure
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen The length of the set buffer
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*   bytes read from the set buffer. If the call failed due to invalid length of
*   the set buffer, returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_DATA If new setting value is wrong.
* \retval WLAN_STATUS_INVALID_LENGTH
*
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetWapiMode (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    DEBUGFUNC("wlanoidSetWapiMode");
    DBGLOG(REQ, LOUD, ("\r\n"));

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);
    ASSERT(pvSetBuffer);

    /* Todo:: For support WAPI and Wi-Fi at same driver, use the set wapi assoc ie at the check point */
    /*        The Adapter Connection setting fgUseWapi will cleat whil oid set mode (infra),          */
    /*        And set fgUseWapi True while set wapi assoc ie                                          */
    /*        policay selection, add key all depend on this flag,                                     */
    /*        The fgUseWapi may remove later                                                          */
    if (*(PUINT_32)pvSetBuffer) {
        prAdapter->fgUseWapi = TRUE;
    }
    else {
        prAdapter->fgUseWapi = FALSE;
    }

#if 0
    prCmdInfo = cmdBufAllocateCmdInfo(prAdapter, (CMD_HDR_SIZE + 4));

    if (!prCmdInfo) {
        DBGLOG(INIT, ERROR, ("Allocate CMD_INFO_T ==> FAILED.\n"));
        return WLAN_STATUS_FAILURE;
    }

    // increase command sequence number
    ucCmdSeqNum = nicIncreaseCmdSeqNum(prAdapter);

    // compose CMD_BUILD_CONNECTION cmd pkt
    prCmdInfo->eCmdType = COMMAND_TYPE_NETWORK_IOCTL;
    prCmdInfo->eNetworkType = NETWORK_TYPE_AIS_INDEX;
    prCmdInfo->u2InfoBufLen = CMD_HDR_SIZE + 4;
    prCmdInfo->pfCmdDoneHandler = nicCmdEventSetCommon;
    prCmdInfo->pfCmdTimeoutHandler = NULL;
    prCmdInfo->fgIsOid = TRUE;
    prCmdInfo->ucCID = CMD_ID_WAPI_MODE;
    prCmdInfo->fgSetQuery = TRUE;
    prCmdInfo->fgNeedResp = FALSE;
    prCmdInfo->fgDriverDomainMCR = FALSE;
    prCmdInfo->ucCmdSeqNum = ucCmdSeqNum;
    prCmdInfo->u4SetInfoLen = u4SetBufferLen;
    prCmdInfo->pvInformationBuffer = pvSetBuffer;
    prCmdInfo->u4InformationBufferLength = u4SetBufferLen;

    // Setup WIFI_CMD_T
    prWifiCmd = (P_WIFI_CMD_T)(prCmdInfo->pucInfoBuffer);
    prWifiCmd->u2TxByteCount_UserPriority = prCmdInfo->u2InfoBufLen;
    prWifiCmd->ucCID = prCmdInfo->ucCID;
    prWifiCmd->ucSetQuery = prCmdInfo->fgSetQuery;
    prWifiCmd->ucSeqNum = prCmdInfo->ucCmdSeqNum;

    cp = (PUINT_8)(prWifiCmd->aucBuffer);

    kalMemCopy(cp, (PUINT_8)pvSetBuffer, 4);

    // insert into prCmdQueue
    kalEnqueueCommand(prGlueInfo, (P_QUE_ENTRY_T)prCmdInfo);

    // wakeup txServiceThread later
    GLUE_SET_EVENT(prGlueInfo);

    return WLAN_STATUS_PENDING;
#else
    return WLAN_STATUS_SUCCESS;
#endif
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called by WAPI to set the assoc info, which is needed to add to
*          Association request frame while join WAPI AP.
*
* \param[in] prAdapter Pointer to the Adapter structure
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen The length of the set buffer
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*   bytes read from the set buffer. If the call failed due to invalid length of
*   the set buffer, returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_DATA If new setting value is wrong.
* \retval WLAN_STATUS_INVALID_LENGTH
*
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetWapiAssocInfo (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    P_WAPI_INFO_ELEM_T    prWapiInfo;
    PUINT_8               cp;
    UINT_16               u2AuthSuiteCount = 0;
    UINT_16               u2PairSuiteCount = 0;
    UINT_32               u4AuthKeyMgtSuite = 0;
    UINT_32               u4PairSuite = 0;
    UINT_32               u4GroupSuite = 0;

    ASSERT(prAdapter);
    ASSERT(pvSetBuffer);
    ASSERT(pu4SetInfoLen);

    DEBUGFUNC("wlanoidSetWapiAssocInfo");
    DBGLOG(REQ, LOUD, ("\r\n"));

    if (u4SetBufferLen < 20 /* From EID to Group cipher */) {
        prAdapter->rWifiVar.rConnSettings.fgWapiMode = FALSE;
        return WLAN_STATUS_INVALID_LENGTH;
    }

    prAdapter->rWifiVar.rConnSettings.fgWapiMode = TRUE;

    //if (prWapiInfo->ucElemId != ELEM_ID_WAPI)
    //    DBGLOG(SEC, TRACE, ("Not WAPI IE ?!\n"));

    //if (prWapiInfo->ucLength < 18)
    //    return WLAN_STATUS_INVALID_LENGTH;

    *pu4SetInfoLen = u4SetBufferLen;

    prWapiInfo = (P_WAPI_INFO_ELEM_T)pvSetBuffer;

    if (prWapiInfo->ucElemId != ELEM_ID_WAPI) {
        DBGLOG(SEC, TRACE, ("Not WAPI IE ?! u4SetBufferLen = %d\n", u4SetBufferLen));
        prAdapter->rWifiVar.rConnSettings.fgWapiMode = FALSE;
        return WLAN_STATUS_INVALID_LENGTH;
    }

    if (prWapiInfo->ucLength < 18)
        return WLAN_STATUS_INVALID_LENGTH;

    /* Skip Version check */
    cp = (PUINT_8)&prWapiInfo->u2AuthKeyMgtSuiteCount;

    WLAN_GET_FIELD_16(cp, &u2AuthSuiteCount);

    if (u2AuthSuiteCount>1)
        return WLAN_STATUS_INVALID_LENGTH;

    cp += 2;
    WLAN_GET_FIELD_32(cp, &u4AuthKeyMgtSuite);

    DBGLOG(SEC, TRACE, ("WAPI: Assoc Info auth mgt suite [%d]: %02x-%02x-%02x-%02x\n",
        u2AuthSuiteCount,
        (UCHAR) (u4AuthKeyMgtSuite & 0x000000FF),
        (UCHAR) ((u4AuthKeyMgtSuite >> 8) & 0x000000FF),
        (UCHAR) ((u4AuthKeyMgtSuite >> 16) & 0x000000FF),
        (UCHAR) ((u4AuthKeyMgtSuite >> 24) & 0x000000FF)));

    if (u4AuthKeyMgtSuite != WAPI_AKM_SUITE_802_1X &&
        u4AuthKeyMgtSuite != WAPI_AKM_SUITE_PSK)
        ASSERT(FALSE);

    cp += 4;
    WLAN_GET_FIELD_16(cp, &u2PairSuiteCount);
    if (u2PairSuiteCount>1)
        return WLAN_STATUS_INVALID_LENGTH;

    cp += 2;
    WLAN_GET_FIELD_32(cp, &u4PairSuite);
    DBGLOG(SEC, TRACE, ("WAPI: Assoc Info pairwise cipher suite [%d]: %02x-%02x-%02x-%02x\n",
        u2PairSuiteCount,
        (UCHAR) (u4PairSuite & 0x000000FF),
        (UCHAR) ((u4PairSuite >> 8) & 0x000000FF),
        (UCHAR) ((u4PairSuite >> 16) & 0x000000FF),
        (UCHAR) ((u4PairSuite >> 24) & 0x000000FF)));

    if (u4PairSuite != WAPI_CIPHER_SUITE_WPI)
        ASSERT(FALSE);

    cp += 4;
    WLAN_GET_FIELD_32(cp, &u4GroupSuite);
    DBGLOG(SEC, TRACE, ("WAPI: Assoc Info group cipher suite : %02x-%02x-%02x-%02x\n",
        (UCHAR) (u4GroupSuite & 0x000000FF),
        (UCHAR) ((u4GroupSuite >> 8) & 0x000000FF),
        (UCHAR) ((u4GroupSuite >> 16) & 0x000000FF),
        (UCHAR) ((u4GroupSuite >> 24) & 0x000000FF)));

    if (u4GroupSuite != WAPI_CIPHER_SUITE_WPI)
        ASSERT(FALSE);

    prAdapter->rWifiVar.rConnSettings.u4WapiSelectedAKMSuite = u4AuthKeyMgtSuite;
    prAdapter->rWifiVar.rConnSettings.u4WapiSelectedPairwiseCipher = u4PairSuite;
    prAdapter->rWifiVar.rConnSettings.u4WapiSelectedGroupCipher = u4GroupSuite;

    kalMemCopy(prAdapter->prGlueInfo->aucWapiAssocInfoIEs,  pvSetBuffer, u4SetBufferLen);
    prAdapter->prGlueInfo->u2WapiAssocInfoIESz = (UINT_16)u4SetBufferLen;
    DBGLOG(SEC, TRACE, ("Assoc Info IE sz %ld\n", u4SetBufferLen));

    return WLAN_STATUS_SUCCESS;

}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set the wpi key to the driver.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_ADAPTER_NOT_READY
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_INVALID_DATA
*
* \note The setting buffer P_PARAM_WPI_KEY, which is set by NDIS, is unpacked.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetWapiKey (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    P_GLUE_INFO_T         prGlueInfo;
    P_CMD_INFO_T          prCmdInfo;
    P_WIFI_CMD_T          prWifiCmd;
    P_PARAM_WPI_KEY_T     prNewKey;
    P_CMD_802_11_KEY      prCmdKey;
    PUINT_8               pc;
    UINT_8                ucCmdSeqNum;

    DEBUGFUNC("wlanoidSetWapiKey");
    DBGLOG(REQ, LOUD, ("\r\n"));

    ASSERT(prAdapter);
    ASSERT(pvSetBuffer);
    ASSERT(pu4SetInfoLen);

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail in set add key! (Adapter not ready). ACPI=D%d, Radio=%d\r\n",
                    prAdapter->rAcpiState, prAdapter->fgIsRadioOff));
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }

    prNewKey = (P_PARAM_WPI_KEY_T) pvSetBuffer;

    DBGLOG_MEM8(REQ, TRACE, (PUINT_8)pvSetBuffer, 560);
    pc = (PUINT_8)pvSetBuffer;

    *pu4SetInfoLen = u4SetBufferLen;

    /* Exception check */
    if (prNewKey->ucKeyID != 0x1 ||
        prNewKey->ucKeyID != 0x0) {
        prNewKey->ucKeyID = prNewKey->ucKeyID & BIT(0);
        //DBGLOG(SEC, INFO, ("Invalid WAPI key ID (%d)\r\n", prNewKey->ucKeyID));
    }

    /* Dump P_PARAM_WPI_KEY_T content. */
    DBGLOG(REQ, TRACE, ("Set: Dump P_PARAM_WPI_KEY_T content\r\n"));
    DBGLOG(REQ, TRACE, ("TYPE      : %d\r\n", prNewKey->eKeyType));
    DBGLOG(REQ, TRACE, ("Direction : %d\r\n", prNewKey->eDirection));
    DBGLOG(REQ, TRACE, ("KeyID     : %d\r\n", prNewKey->ucKeyID));
    DBGLOG(REQ, TRACE, ("AddressIndex:\r\n"));
    DBGLOG_MEM8(REQ, TRACE, prNewKey->aucAddrIndex, 12);
    prNewKey->u4LenWPIEK = 16;

    DBGLOG_MEM8(REQ, TRACE, (PUINT_8)prNewKey->aucWPIEK, (UINT_8)prNewKey->u4LenWPIEK);
    prNewKey->u4LenWPICK = 16;

    DBGLOG(REQ, TRACE, ("CK Key(%d):\r\n", (UINT_8)prNewKey->u4LenWPICK));
    DBGLOG_MEM8(REQ, TRACE, (PUINT_8)prNewKey->aucWPICK, (UINT_8)prNewKey->u4LenWPICK);
    DBGLOG(REQ, TRACE, ("PN:\r\n"));
    if (prNewKey->eKeyType == 0){
        prNewKey->aucPN[0] = 0x5c;
        prNewKey->aucPN[1] = 0x36;
        prNewKey->aucPN[2] = 0x5c;
        prNewKey->aucPN[3] = 0x36;
        prNewKey->aucPN[4] = 0x5c;
        prNewKey->aucPN[5] = 0x36;
        prNewKey->aucPN[6] = 0x5c;
        prNewKey->aucPN[7] = 0x36;
        prNewKey->aucPN[8] = 0x5c;
        prNewKey->aucPN[9] = 0x36;
        prNewKey->aucPN[10] = 0x5c;
        prNewKey->aucPN[11] = 0x36;
        prNewKey->aucPN[12] = 0x5c;
        prNewKey->aucPN[13] = 0x36;
        prNewKey->aucPN[14] = 0x5c;
        prNewKey->aucPN[15] = 0x36;
    }

    DBGLOG_MEM8(REQ, TRACE, (PUINT_8)prNewKey->aucPN, 16);

    prGlueInfo = prAdapter->prGlueInfo;

    prCmdInfo = cmdBufAllocateCmdInfo(prAdapter, (CMD_HDR_SIZE + u4SetBufferLen));

    if (!prCmdInfo) {
        DBGLOG(INIT, ERROR, ("Allocate CMD_INFO_T ==> FAILED.\n"));
        return WLAN_STATUS_FAILURE;
    }

    // increase command sequence number
    ucCmdSeqNum = nicIncreaseCmdSeqNum(prAdapter);

    // compose CMD_ID_ADD_REMOVE_KEY cmd pkt
    prCmdInfo->eCmdType = COMMAND_TYPE_NETWORK_IOCTL;
    prCmdInfo->eNetworkType = NETWORK_TYPE_AIS_INDEX;
    prCmdInfo->u2InfoBufLen = CMD_HDR_SIZE + sizeof(CMD_802_11_KEY);
    prCmdInfo->pfCmdDoneHandler = nicCmdEventSetCommon;
    prCmdInfo->pfCmdTimeoutHandler = nicOidCmdTimeoutCommon;
    prCmdInfo->fgIsOid = TRUE;
    prCmdInfo->ucCID = CMD_ID_ADD_REMOVE_KEY;
    prCmdInfo->fgSetQuery = TRUE;
    prCmdInfo->fgNeedResp = FALSE;
    prCmdInfo->fgDriverDomainMCR = FALSE;
    prCmdInfo->ucCmdSeqNum = ucCmdSeqNum;
    prCmdInfo->u4SetInfoLen = u4SetBufferLen;
    prCmdInfo->pvInformationBuffer = pvSetBuffer;
    prCmdInfo->u4InformationBufferLength = u4SetBufferLen;

    // Setup WIFI_CMD_T
    prWifiCmd = (P_WIFI_CMD_T)(prCmdInfo->pucInfoBuffer);
    prWifiCmd->u2TxByteCount_UserPriority = prCmdInfo->u2InfoBufLen;
    prWifiCmd->ucCID = prCmdInfo->ucCID;
    prWifiCmd->ucSetQuery = prCmdInfo->fgSetQuery;
    prWifiCmd->ucSeqNum = prCmdInfo->ucCmdSeqNum;

    prCmdKey = (P_CMD_802_11_KEY)(prWifiCmd->aucBuffer);

    kalMemZero(prCmdKey, sizeof(CMD_802_11_KEY));

    prCmdKey->ucAddRemove = 1; /* Add */

    if (prNewKey->eKeyType == ENUM_WPI_PAIRWISE_KEY) {
        prCmdKey->ucTxKey = 1;
        prCmdKey->ucKeyType = 1;
    }

    kalMemCopy(prCmdKey->aucPeerAddr, (PUINT_8)prNewKey->aucAddrIndex, MAC_ADDR_LEN);

    prCmdKey->ucNetType = 0; /* AIS */

    prCmdKey->ucKeyId = prNewKey->ucKeyID;

    prCmdKey->ucKeyLen = 32;

    prCmdKey->ucAlgorithmId = CIPHER_SUITE_WPI;

    kalMemCopy(prCmdKey->aucKeyMaterial, (PUINT_8)prNewKey->aucWPIEK, 16);

    kalMemCopy(prCmdKey->aucKeyMaterial+16, (PUINT_8)prNewKey->aucWPICK, 16);

    kalMemCopy(prCmdKey->aucKeyRsc, (PUINT_8)prNewKey->aucPN, 16);

    // insert into prCmdQueue
    kalEnqueueCommand(prGlueInfo, (P_QUE_ENTRY_T)prCmdInfo);

    // wakeup txServiceThread later
    GLUE_SET_EVENT(prGlueInfo);

    return WLAN_STATUS_PENDING;
} /* wlanoidSetAddKey */
#endif


#if CFG_SUPPORT_WPS2
/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called by WSC to set the assoc info, which is needed to add to
*          Association request frame while join WPS AP.
*
* \param[in] prAdapter Pointer to the Adapter structure
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen The length of the set buffer
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*   bytes read from the set buffer. If the call failed due to invalid length of
*   the set buffer, returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_DATA If new setting value is wrong.
* \retval WLAN_STATUS_INVALID_LENGTH
*
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetWSCAssocInfo (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    ASSERT(prAdapter);
    ASSERT(pvSetBuffer);
    ASSERT(pu4SetInfoLen);

    DEBUGFUNC("wlanoidSetWSCAssocInfo");
    DBGLOG(REQ, LOUD, ("\r\n"));

    if(u4SetBufferLen == 0)
        return WLAN_STATUS_INVALID_LENGTH;

    *pu4SetInfoLen = u4SetBufferLen;

    kalMemCopy(prAdapter->prGlueInfo->aucWSCAssocInfoIE,  pvSetBuffer, u4SetBufferLen);
    prAdapter->prGlueInfo->u2WSCAssocInfoIELen = (UINT_16)u4SetBufferLen;
    DBGLOG(SEC, TRACE, ("Assoc Info IE sz %ld\n", u4SetBufferLen));

    return WLAN_STATUS_SUCCESS;

}
#endif


#if CFG_ENABLE_WAKEUP_ON_LAN
WLAN_STATUS
wlanoidSetAddWakeupPattern (
    IN  P_ADAPTER_T   prAdapter,
    IN  PVOID         pvSetBuffer,
    IN  UINT_32       u4SetBufferLen,
    OUT PUINT_32      pu4SetInfoLen
    )
{
    P_PARAM_PM_PACKET_PATTERN prPacketPattern;

    DEBUGFUNC("wlanoidSetAddWakeupPattern");
    DBGLOG(REQ, LOUD, ("\r\n"));

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(PARAM_PM_PACKET_PATTERN);

    if (u4SetBufferLen < sizeof(PARAM_PM_PACKET_PATTERN)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    ASSERT(pvSetBuffer);

    prPacketPattern = (P_PARAM_PM_PACKET_PATTERN) pvSetBuffer;

    /* FIXME:
     * Send the struct to firmware */

    return WLAN_STATUS_FAILURE;
}


WLAN_STATUS
wlanoidSetRemoveWakeupPattern (
    IN  P_ADAPTER_T   prAdapter,
    IN  PVOID         pvSetBuffer,
    IN  UINT_32       u4SetBufferLen,
    OUT PUINT_32      pu4SetInfoLen
    )
{
    P_PARAM_PM_PACKET_PATTERN prPacketPattern;

    DEBUGFUNC("wlanoidSetAddWakeupPattern");
    DBGLOG(REQ, LOUD, ("\r\n"));

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(PARAM_PM_PACKET_PATTERN);

    if (u4SetBufferLen < sizeof(PARAM_PM_PACKET_PATTERN)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    ASSERT(pvSetBuffer);

    prPacketPattern = (P_PARAM_PM_PACKET_PATTERN) pvSetBuffer;

    /* FIXME:
     * Send the struct to firmware */

    return WLAN_STATUS_FAILURE;
}


WLAN_STATUS
wlanoidQueryEnableWakeup (
    IN  P_ADAPTER_T   prAdapter,
    OUT PVOID         pvQueryBuffer,
    IN  UINT_32       u4QueryBufferLen,
    OUT PUINT_32      pu4QueryInfoLen
    )
{
    PUINT_32 pu4WakeupEventEnable;

    DEBUGFUNC("wlanoidQueryEnableWakeup");
    DBGLOG(REQ, LOUD, ("\r\n"));

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    *pu4QueryInfoLen = sizeof(UINT_32);

    if (u4QueryBufferLen < sizeof(UINT_32)) {
        return WLAN_STATUS_BUFFER_TOO_SHORT;
    }

    pu4WakeupEventEnable = (PUINT_32)pvQueryBuffer;

    *pu4WakeupEventEnable = prAdapter->u4WakeupEventEnable;

    return WLAN_STATUS_SUCCESS;
}

WLAN_STATUS
wlanoidSetEnableWakeup (
    IN  P_ADAPTER_T   prAdapter,
    IN  PVOID         pvSetBuffer,
    IN  UINT_32       u4SetBufferLen,
    OUT PUINT_32      pu4SetInfoLen
    )
{
    PUINT_32 pu4WakeupEventEnable;

    DEBUGFUNC("wlanoidSetEnableWakup");
    DBGLOG(REQ, LOUD, ("\r\n"));

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(UINT_32);

    if (u4SetBufferLen < sizeof(UINT_32)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    ASSERT(pvSetBuffer);

    pu4WakeupEventEnable = (PUINT_32)pvSetBuffer;
    prAdapter->u4WakeupEventEnable = *pu4WakeupEventEnable;

    /* FIXME:
     * Send Command Event for setting wakeup-pattern / Magic Packet to firmware
     * */

    return WLAN_STATUS_FAILURE;
}
#endif

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to configure PS related settings for WMM-PS test.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetWiFiWmmPsTest (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{
    P_PARAM_CUSTOM_WMM_PS_TEST_STRUC_T prWmmPsTestInfo;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    CMD_SET_WMM_PS_TEST_STRUC_T rSetWmmPsTestParam;
    UINT_16 u2CmdBufLen;
    P_PM_PROFILE_SETUP_INFO_T prPmProfSetupInfo;
    P_BSS_INFO_T prBssInfo;

    DEBUGFUNC("wlanoidSetWiFiWmmPsTest");

    ASSERT(prAdapter);
    ASSERT(pvSetBuffer);
    ASSERT(pu4SetInfoLen);


    *pu4SetInfoLen = sizeof(PARAM_CUSTOM_WMM_PS_TEST_STRUC_T);

    prWmmPsTestInfo = (P_PARAM_CUSTOM_WMM_PS_TEST_STRUC_T) pvSetBuffer;

    rSetWmmPsTestParam.ucNetTypeIndex = NETWORK_TYPE_AIS_INDEX;
    rSetWmmPsTestParam.bmfgApsdEnAc  = prWmmPsTestInfo->bmfgApsdEnAc;
    rSetWmmPsTestParam.ucIsEnterPsAtOnce  = prWmmPsTestInfo->ucIsEnterPsAtOnce;
    rSetWmmPsTestParam.ucIsDisableUcTrigger  = prWmmPsTestInfo->ucIsDisableUcTrigger;

    prBssInfo = &(prAdapter->rWifiVar.arBssInfo[rSetWmmPsTestParam.ucNetTypeIndex]);
    prPmProfSetupInfo = &prBssInfo->rPmProfSetupInfo;
    prPmProfSetupInfo->ucBmpDeliveryAC = (rSetWmmPsTestParam.bmfgApsdEnAc >> 4) & BITS(0, 3);
    prPmProfSetupInfo->ucBmpTriggerAC = rSetWmmPsTestParam.bmfgApsdEnAc & BITS(0, 3);

    u2CmdBufLen = sizeof(CMD_SET_WMM_PS_TEST_STRUC_T);

#if 0
    /* it will apply the disable trig or not immediately */
    if (prPmInfo->ucWmmPsDisableUcPoll && prPmInfo->ucWmmPsConnWithTrig) {
//        NIC_PM_WMM_PS_DISABLE_UC_TRIG(prAdapter, TRUE);
    }
    else {
//        NIC_PM_WMM_PS_DISABLE_UC_TRIG(prAdapter, FALSE);
    }
#endif

    rStatus = wlanSendSetQueryCmd(prAdapter,
            CMD_ID_SET_WMM_PS_TEST_PARMS,
            TRUE,
            FALSE,
            TRUE,
            NULL, // TODO?
            NULL,
            u2CmdBufLen,
            (PUINT_8)&rSetWmmPsTestParam,
            NULL,
            0);


    return rStatus;
}   /* wlanoidSetWiFiWmmPsTest */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to configure enable/disable TX A-MPDU feature.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetTxAmpdu (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    CMD_TX_AMPDU_T rTxAmpdu;
    UINT_16 u2CmdBufLen;
    PBOOLEAN pfgEnable;

    DEBUGFUNC("wlanoidSetTxAmpdu");

    ASSERT(prAdapter);
    ASSERT(pvSetBuffer);
    ASSERT(pu4SetInfoLen);


    *pu4SetInfoLen = sizeof(BOOLEAN);

    pfgEnable = (PBOOLEAN) pvSetBuffer;

    rTxAmpdu.fgEnable = *pfgEnable;

    u2CmdBufLen = sizeof(CMD_TX_AMPDU_T);

    rStatus = wlanSendSetQueryCmd(prAdapter,
            CMD_ID_TX_AMPDU,
            TRUE,
            FALSE,
            TRUE,
            NULL,
            NULL,
            u2CmdBufLen,
            (PUINT_8)&rTxAmpdu,
            NULL,
            0);


    return rStatus;
}   /* wlanoidSetTxAmpdu */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to configure reject/accept ADDBA Request.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetAddbaReject(
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    CMD_ADDBA_REJECT_T rAddbaReject;
    UINT_16 u2CmdBufLen;
    PBOOLEAN pfgEnable;

    DEBUGFUNC("wlanoidSetAddbaReject");

    ASSERT(prAdapter);
    ASSERT(pvSetBuffer);
    ASSERT(pu4SetInfoLen);


    *pu4SetInfoLen = sizeof(BOOLEAN);

    pfgEnable = (PBOOLEAN) pvSetBuffer;

    rAddbaReject.fgEnable = *pfgEnable;

    u2CmdBufLen = sizeof(CMD_ADDBA_REJECT_T);

    rStatus = wlanSendSetQueryCmd(prAdapter,
            CMD_ID_ADDBA_REJECT,
            TRUE,
            FALSE,
            TRUE,
            NULL,
            NULL,
            u2CmdBufLen,
            (PUINT_8)&rAddbaReject,
            NULL,
            0);


    return rStatus;
}   /* wlanoidSetAddbaReject */


#if CFG_SLT_SUPPORT

WLAN_STATUS
wlanoidQuerySLTStatus (
    IN  P_ADAPTER_T   prAdapter,
    OUT PVOID         pvQueryBuffer,
    IN  UINT_32       u4QueryBufferLen,
    OUT PUINT_32      pu4QueryInfoLen
    )
{
    WLAN_STATUS rWlanStatus = WLAN_STATUS_SUCCESS;
    P_PARAM_MTK_SLT_TEST_STRUC_T prMtkSltInfo = (P_PARAM_MTK_SLT_TEST_STRUC_T)NULL;
    P_SLT_INFO_T prSltInfo = (P_SLT_INFO_T)NULL;

    DEBUGFUNC("wlanoidQuerySLTStatus");
    DBGLOG(REQ, LOUD, ("\r\n"));

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);

    *pu4QueryInfoLen = sizeof(PARAM_MTK_SLT_TEST_STRUC_T);

    if (u4QueryBufferLen < sizeof(PARAM_MTK_SLT_TEST_STRUC_T)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    ASSERT(pvQueryBuffer);

    prMtkSltInfo = (P_PARAM_MTK_SLT_TEST_STRUC_T)pvQueryBuffer;

    prSltInfo = &(prAdapter->rWifiVar.rSltInfo);

    switch (prMtkSltInfo->rSltFuncIdx) {
    case ENUM_MTK_SLT_FUNC_LP_SET:
        {
            P_PARAM_MTK_SLT_LP_TEST_STRUC_T prLpSetting = (P_PARAM_MTK_SLT_LP_TEST_STRUC_T)NULL;

            ASSERT(prMtkSltInfo->u4FuncInfoLen == sizeof(PARAM_MTK_SLT_LP_TEST_STRUC_T));

            prLpSetting = (P_PARAM_MTK_SLT_LP_TEST_STRUC_T)&prMtkSltInfo->unFuncInfoContent;

            prLpSetting->u4BcnRcvNum =  prSltInfo->u4BeaconReceiveCnt;
        }
        break;
    default:
        // TBD...
        break;
    }

    return rWlanStatus;
} /* wlanoidQuerySLTStatus */

WLAN_STATUS
wlanoidUpdateSLTMode (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{
    WLAN_STATUS rWlanStatus = WLAN_STATUS_SUCCESS;
    P_PARAM_MTK_SLT_TEST_STRUC_T prMtkSltInfo = (P_PARAM_MTK_SLT_TEST_STRUC_T)NULL;
    P_SLT_INFO_T prSltInfo = (P_SLT_INFO_T)NULL;
    P_BSS_DESC_T prBssDesc = (P_BSS_DESC_T)NULL;
    P_STA_RECORD_T prStaRec = (P_STA_RECORD_T)NULL;
    P_BSS_INFO_T prBssInfo = (P_BSS_INFO_T)NULL;

    /* 1. Action: Update or Initial Set
      * 2. Role.
      * 3. Target MAC address.
      * 4. RF BW & Rate Settings
      */

    DEBUGFUNC("wlanoidUpdateSLTMode");
    DBGLOG(REQ, LOUD, ("\r\n"));

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(PARAM_MTK_SLT_TEST_STRUC_T);

    if (u4SetBufferLen < sizeof(PARAM_MTK_SLT_TEST_STRUC_T)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    ASSERT(pvSetBuffer);

    prMtkSltInfo = (P_PARAM_MTK_SLT_TEST_STRUC_T) pvSetBuffer;

    prSltInfo = &(prAdapter->rWifiVar.rSltInfo);
    prBssInfo = &prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX];

    switch (prMtkSltInfo->rSltFuncIdx) {
    case ENUM_MTK_SLT_FUNC_INITIAL: /* Initialize */
        {
            P_PARAM_MTK_SLT_INITIAL_STRUC_T prMtkSltInit = (P_PARAM_MTK_SLT_INITIAL_STRUC_T)NULL;

            ASSERT(prMtkSltInfo->u4FuncInfoLen == sizeof(PARAM_MTK_SLT_INITIAL_STRUC_T));

            prMtkSltInit = (P_PARAM_MTK_SLT_INITIAL_STRUC_T)&prMtkSltInfo->unFuncInfoContent;

            if (prSltInfo->prPseudoStaRec != NULL) {
                /* The driver has been initialized. */
                prSltInfo->prPseudoStaRec = NULL;
            }


            prSltInfo->prPseudoBssDesc = scanSearchExistingBssDesc(prAdapter,
                                                                          BSS_TYPE_IBSS,
                                                                          prMtkSltInit->aucTargetMacAddr,
                                                                          prMtkSltInit->aucTargetMacAddr);

            prSltInfo->u2SiteID = prMtkSltInit->u2SiteID;

            /* Bandwidth 2.4G: Channel 1~14
              * Bandwidth 5G: *36, 40, 44, 48, 52, 56, 60, 64,
              *                       *100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140,
              *                       149, 153, *157, 161,
              *                       184, 188, 192, 196, 200, 204, 208, 212, *216
              */
            prSltInfo->ucChannel2G4 = 1 + (prSltInfo->u2SiteID % 4) * 5;

            switch (prSltInfo->ucChannel2G4) {
            case 1:
                prSltInfo->ucChannel5G = 36;
                break;
            case 6:
                prSltInfo->ucChannel5G = 52;
                break;
            case 11:
                prSltInfo->ucChannel5G = 104;
                break;
            case 16:
                prSltInfo->ucChannel2G4 = 14;
                prSltInfo->ucChannel5G = 161;
                break;
            default:
                ASSERT(FALSE);
            }

            if (prSltInfo->prPseudoBssDesc == NULL) {
                do {
                    prSltInfo->prPseudoBssDesc = scanAllocateBssDesc(prAdapter);

                    if (prSltInfo->prPseudoBssDesc == NULL) {
                        rWlanStatus = WLAN_STATUS_FAILURE;
                        break;
                    }
                    else {
                        prBssDesc = prSltInfo->prPseudoBssDesc;
                    }
                } while (FALSE);
            }
            else {
                prBssDesc = prSltInfo->prPseudoBssDesc;
            }

            if (prBssDesc) {
                prBssDesc->eBSSType = BSS_TYPE_IBSS;

                COPY_MAC_ADDR(prBssDesc->aucSrcAddr, prMtkSltInit->aucTargetMacAddr);
                COPY_MAC_ADDR(prBssDesc->aucBSSID, prBssInfo->aucOwnMacAddr);

                prBssDesc->u2BeaconInterval = 100;
                prBssDesc->u2ATIMWindow = 0;
                prBssDesc->ucDTIMPeriod = 1;

                prBssDesc->u2IELength = 0;

                prBssDesc->fgIsERPPresent = TRUE;
                prBssDesc->fgIsHTPresent = TRUE;

                prBssDesc->u2OperationalRateSet = BIT(RATE_36M_INDEX);
                prBssDesc->u2BSSBasicRateSet = BIT(RATE_36M_INDEX);
                prBssDesc->fgIsUnknownBssBasicRate = FALSE;

                prBssDesc->fgIsLargerTSF = TRUE;

                prBssDesc->eBand = BAND_2G4;

                prBssDesc->ucChannelNum = prSltInfo->ucChannel2G4;

                prBssDesc->ucPhyTypeSet = PHY_TYPE_SET_802_11ABGN;

                GET_CURRENT_SYSTIME(&prBssDesc->rUpdateTime);
            }
        }
        break;
    case ENUM_MTK_SLT_FUNC_RATE_SET: /* Update RF Settings. */
        if (prSltInfo->prPseudoStaRec == NULL) {
            rWlanStatus = WLAN_STATUS_FAILURE;
        }
        else {
            P_PARAM_MTK_SLT_TR_TEST_STRUC_T prTRSetting = (P_PARAM_MTK_SLT_TR_TEST_STRUC_T)NULL;

            ASSERT(prMtkSltInfo->u4FuncInfoLen == sizeof(PARAM_MTK_SLT_TR_TEST_STRUC_T));

            prStaRec = prSltInfo->prPseudoStaRec;
            prTRSetting = (P_PARAM_MTK_SLT_TR_TEST_STRUC_T)&prMtkSltInfo->unFuncInfoContent;

            if (prTRSetting->rNetworkType == PARAM_NETWORK_TYPE_OFDM5) {
                prBssInfo->eBand = BAND_5G;
                prBssInfo->ucPrimaryChannel = prSltInfo->ucChannel5G;
            }
            if (prTRSetting->rNetworkType == PARAM_NETWORK_TYPE_OFDM24) {
                prBssInfo->eBand = BAND_2G4;
                prBssInfo->ucPrimaryChannel = prSltInfo->ucChannel2G4;
            }

            if ((prTRSetting->u4FixedRate & FIXED_BW_DL40) != 0) {
                /* RF 40 */
                prStaRec->u2HtCapInfo |= HT_CAP_INFO_SUP_CHNL_WIDTH;   /* It would controls RFBW capability in WTBL. */
                prStaRec->ucDesiredPhyTypeSet = PHY_TYPE_BIT_HT;               /* This controls RF BW, RF BW would be 40 only if
                                                                                                                 * 1. PHY_TYPE_BIT_HT is TRUE.
                                                                                                                 * 2. SCO is SCA/SCB.
                                                                                                                 */

                /* U20/L20 Control. */
                switch (prTRSetting->u4FixedRate & 0xC000) {
                case FIXED_EXT_CHNL_U20:
                    prBssInfo->eBssSCO = CHNL_EXT_SCB; // +2
                    if (prTRSetting->rNetworkType == PARAM_NETWORK_TYPE_OFDM5) {
                        prBssInfo->ucPrimaryChannel += 2;
                    }
                    else {
                        if (prBssInfo->ucPrimaryChannel <5) {
                            prBssInfo->ucPrimaryChannel = 8;   // For channel 1, testing L20 at channel 8.
                        }
                    }
                    break;
                case FIXED_EXT_CHNL_L20:
                default:  /* 40M */
                    prBssInfo->eBssSCO = CHNL_EXT_SCA;  // -2
                    if (prTRSetting->rNetworkType == PARAM_NETWORK_TYPE_OFDM5) {
                        prBssInfo->ucPrimaryChannel -= 2;
                    }
                    else {
                        if (prBssInfo->ucPrimaryChannel > 10) {
                            prBssInfo->ucPrimaryChannel = 3;  // For channel 11 / 14. testing U20 at channel 3.
                        }
                    }
                    break;
                }
            }
            else {
                /* RF 20 */
                prStaRec->u2HtCapInfo &= ~HT_CAP_INFO_SUP_CHNL_WIDTH;
                prBssInfo->eBssSCO = CHNL_EXT_SCN;
            }

            prBssInfo->fgErpProtectMode = FALSE;
            prBssInfo->eHtProtectMode = HT_PROTECT_MODE_NONE;
            prBssInfo->eGfOperationMode = GF_MODE_NORMAL;

            nicUpdateBss(prAdapter, prBssInfo->ucNetTypeIndex);

            prStaRec->u2HtCapInfo &= ~(HT_CAP_INFO_SHORT_GI_20M | HT_CAP_INFO_SHORT_GI_40M);

            switch (prTRSetting->u4FixedRate & 0xFF) {
            case RATE_OFDM_54M:
                prStaRec->u2DesiredNonHTRateSet = BIT(RATE_54M_INDEX);
                break;
            case RATE_OFDM_48M:
                prStaRec->u2DesiredNonHTRateSet = BIT(RATE_48M_INDEX);
                break;
            case RATE_OFDM_36M:
                prStaRec->u2DesiredNonHTRateSet = BIT(RATE_36M_INDEX);
                break;
            case RATE_OFDM_24M:
                prStaRec->u2DesiredNonHTRateSet = BIT(RATE_24M_INDEX);
                break;
            case RATE_OFDM_6M:
                prStaRec->u2DesiredNonHTRateSet = BIT(RATE_6M_INDEX);
                break;
            case RATE_CCK_11M_LONG:
                prStaRec->u2DesiredNonHTRateSet = BIT(RATE_11M_INDEX);
                break;
            case RATE_CCK_1M_LONG:
                prStaRec->u2DesiredNonHTRateSet = BIT(RATE_1M_INDEX);
                break;
            case RATE_GF_MCS_0:
                prStaRec->u2DesiredNonHTRateSet = BIT(RATE_HT_PHY_INDEX);
                prStaRec->u2HtCapInfo |= HT_CAP_INFO_HT_GF;
                break;
            case RATE_MM_MCS_7:
                prStaRec->u2DesiredNonHTRateSet = BIT(RATE_HT_PHY_INDEX);
                prStaRec->u2HtCapInfo &= ~HT_CAP_INFO_HT_GF;
#if 0  // Only for Current Measurement Mode.
                prStaRec->u2HtCapInfo |= (HT_CAP_INFO_SHORT_GI_20M | HT_CAP_INFO_SHORT_GI_40M);
#endif
                break;
            case RATE_GF_MCS_7:
                prStaRec->u2DesiredNonHTRateSet = BIT(RATE_HT_PHY_INDEX);
                prStaRec->u2HtCapInfo |= HT_CAP_INFO_HT_GF;
                break;
            default:
                prStaRec->u2DesiredNonHTRateSet = BIT(RATE_36M_INDEX);
                break;
            }

            cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_1);

            cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_3);

        }
        break;
    case ENUM_MTK_SLT_FUNC_LP_SET: /* Reset LP Test Result. */
        {
            P_PARAM_MTK_SLT_LP_TEST_STRUC_T prLpSetting = (P_PARAM_MTK_SLT_LP_TEST_STRUC_T)NULL;

            ASSERT(prMtkSltInfo->u4FuncInfoLen == sizeof(PARAM_MTK_SLT_LP_TEST_STRUC_T));

            prLpSetting = (P_PARAM_MTK_SLT_LP_TEST_STRUC_T)&prMtkSltInfo->unFuncInfoContent;

            if (prSltInfo->prPseudoBssDesc == NULL) {
                /* Please initial SLT Mode first. */
                break;
            }
            else {
                prBssDesc = prSltInfo->prPseudoBssDesc;
            }

            switch (prLpSetting->rLpTestMode) {
            case ENUM_MTK_LP_TEST_NORMAL:
                /* In normal mode, we would use target MAC address to be the BSSID. */
                COPY_MAC_ADDR(prBssDesc->aucBSSID, prBssInfo->aucOwnMacAddr);
                prSltInfo->fgIsDUT = FALSE;
                break;
            case ENUM_MTK_LP_TEST_GOLDEN_SAMPLE:
                /* 1. Lower AIFS of BCN queue.
                  * 2. Fixed Random Number tobe 0.
                  */
                prSltInfo->fgIsDUT = FALSE;
                /* In LP test mode, we would use MAC address of Golden Sample to be the BSSID. */
                COPY_MAC_ADDR(prBssDesc->aucBSSID, prBssInfo->aucOwnMacAddr);
                break;
            case ENUM_MTK_LP_TEST_DUT:
                /* 1. Enter Sleep Mode.
                  * 2. Fix random number a large value & enlarge AIFN of BCN queue.
                  */
                COPY_MAC_ADDR(prBssDesc->aucBSSID, prBssDesc->aucSrcAddr);
                prSltInfo->u4BeaconReceiveCnt = 0;
                prSltInfo->fgIsDUT = TRUE;
                break;
            }

        }

        break;
    default:
        break;
    }




    return WLAN_STATUS_FAILURE;


    return rWlanStatus;
} /* wlanoidUpdateSLTMode */
#endif


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query NVRAM value.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuf A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryNvramRead (
    IN  P_ADAPTER_T  prAdapter,
    OUT PVOID        pvQueryBuffer,
    IN  UINT_32      u4QueryBufferLen,
    OUT PUINT_32     pu4QueryInfoLen
    )
{
    P_PARAM_CUSTOM_NVRAM_RW_STRUCT_T prNvramRwInfo;
    UINT_16     u2Data;
    BOOLEAN     fgStatus;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;

    DEBUGFUNC("wlanoidQueryNvramRead");

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    *pu4QueryInfoLen = sizeof(PARAM_CUSTOM_NVRAM_RW_STRUCT_T);

    if (u4QueryBufferLen < sizeof(PARAM_CUSTOM_NVRAM_RW_STRUCT_T)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    prNvramRwInfo = (P_PARAM_CUSTOM_NVRAM_RW_STRUCT_T)pvQueryBuffer;

    if(prNvramRwInfo->ucEepromMethod == PARAM_EEPROM_READ_METHOD_READ) {
        fgStatus = kalCfgDataRead16(prAdapter->prGlueInfo,
                prNvramRwInfo->ucEepromIndex << 1, /* change to byte offset */
                &u2Data);

        if(fgStatus) {
            prNvramRwInfo->u2EepromData = u2Data;
            DBGLOG(REQ, INFO, ("NVRAM Read: index=%#X, data=%#02X\r\n",
                        prNvramRwInfo->ucEepromIndex, u2Data));
        }
        else{
            DBGLOG(REQ, ERROR, ("NVRAM Read Failed: index=%#x.\r\n",
                        prNvramRwInfo->ucEepromIndex));
            rStatus = WLAN_STATUS_FAILURE;
        }
    }
    else if (prNvramRwInfo->ucEepromMethod == PARAM_EEPROM_READ_METHOD_GETSIZE) {
        prNvramRwInfo->u2EepromData = CFG_FILE_WIFI_REC_SIZE;
        DBGLOG(REQ, INFO, ("EEPROM size =%d\r\n", prNvramRwInfo->u2EepromData));
    }

    *pu4QueryInfoLen = sizeof(PARAM_CUSTOM_EEPROM_RW_STRUC_T);

    return rStatus;
}   /* wlanoidQueryNvramRead */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to write NVRAM value.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetNvramWrite (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{
    P_PARAM_CUSTOM_NVRAM_RW_STRUCT_T prNvramRwInfo;
    BOOLEAN     fgStatus;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;


    DEBUGFUNC("wlanoidSetNvramWrite");
    DBGLOG(INIT, LOUD,("\n"));

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(PARAM_CUSTOM_NVRAM_RW_STRUCT_T);

    if (u4SetBufferLen < sizeof(PARAM_CUSTOM_NVRAM_RW_STRUCT_T)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    ASSERT(pvSetBuffer);

    prNvramRwInfo = (P_PARAM_CUSTOM_NVRAM_RW_STRUCT_T)pvSetBuffer;

    fgStatus = kalCfgDataWrite16(prAdapter->prGlueInfo,
            prNvramRwInfo->ucEepromIndex << 1, /* change to byte offset */
            prNvramRwInfo->u2EepromData
            );

    if(fgStatus == FALSE){
        DBGLOG(REQ, ERROR, ("NVRAM Write Failed.\r\n"));
        rStatus = WLAN_STATUS_FAILURE;
    }

    return rStatus;
}   /* wlanoidSetNvramWrite */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to get the config data source type.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryCfgSrcType(
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    )
{
    ASSERT(prAdapter);

    *pu4QueryInfoLen = sizeof(ENUM_CFG_SRC_TYPE_T);

    if(kalIsConfigurationExist(prAdapter->prGlueInfo) == TRUE) {
        *(P_ENUM_CFG_SRC_TYPE_T)pvQueryBuffer = CFG_SRC_TYPE_NVRAM;
    }
    else {
        *(P_ENUM_CFG_SRC_TYPE_T)pvQueryBuffer = CFG_SRC_TYPE_EEPROM;
    }

    return WLAN_STATUS_SUCCESS;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to get the config data source type.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryEepromType(
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    )
{
    ASSERT(prAdapter);

    *pu4QueryInfoLen = sizeof(P_ENUM_EEPROM_TYPE_T);

#if CFG_SUPPORT_NIC_CAPABILITY
    if(prAdapter->fgIsEepromUsed == TRUE) {
        *( P_ENUM_EEPROM_TYPE_T )pvQueryBuffer = EEPROM_TYPE_PRESENT;
    }
    else {
        *( P_ENUM_EEPROM_TYPE_T )pvQueryBuffer = EEPROM_TYPE_NO;
    }
#else
    *( P_ENUM_EEPROM_TYPE_T )pvQueryBuffer = EEPROM_TYPE_NO;
#endif

    return WLAN_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to get the config data source type.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetCountryCode (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{
    PUINT_8         pucCountry;

    ASSERT(prAdapter);
    ASSERT(pvSetBuffer);
    ASSERT(u4SetBufferLen == 2);

    *pu4SetInfoLen = 2;

    pucCountry = pvSetBuffer;

    prAdapter->rWifiVar.rConnSettings.u2CountryCode =
        (((UINT_16) pucCountry[0]) << 8) | ((UINT_16) pucCountry[1]) ;

    prAdapter->prDomainInfo = NULL; /* Force to re-search country code */
    rlmDomainSendCmd(prAdapter, TRUE);

    return WLAN_STATUS_SUCCESS;
}

#if 0
WLAN_STATUS
wlanoidSetNoaParam (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    P_PARAM_CUSTOM_NOA_PARAM_STRUC_T prNoaParam;
    CMD_CUSTOM_NOA_PARAM_STRUC_T rCmdNoaParam;

    DEBUGFUNC("wlanoidSetNoaParam");
    DBGLOG(INIT, LOUD,("\n"));

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(PARAM_CUSTOM_NOA_PARAM_STRUC_T);

    if (u4SetBufferLen < sizeof(PARAM_CUSTOM_NOA_PARAM_STRUC_T)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    ASSERT(pvSetBuffer);

    prNoaParam = (P_PARAM_CUSTOM_NOA_PARAM_STRUC_T)pvSetBuffer;

    kalMemZero(&rCmdNoaParam, sizeof(CMD_CUSTOM_NOA_PARAM_STRUC_T));
    rCmdNoaParam.u4NoaDurationMs = prNoaParam->u4NoaDurationMs;
    rCmdNoaParam.u4NoaIntervalMs = prNoaParam->u4NoaIntervalMs;
    rCmdNoaParam.u4NoaCount = prNoaParam->u4NoaCount;

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_SET_NOA_PARAM,
            TRUE,
            FALSE,
            TRUE,
            nicCmdEventSetCommon,
            nicOidCmdTimeoutCommon,
            sizeof(CMD_CUSTOM_NOA_PARAM_STRUC_T),
            (PUINT_8)&rCmdNoaParam,
            pvSetBuffer,
            u4SetBufferLen
            );
}

WLAN_STATUS
wlanoidSetOppPsParam (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    P_PARAM_CUSTOM_OPPPS_PARAM_STRUC_T prOppPsParam;
    CMD_CUSTOM_OPPPS_PARAM_STRUC_T rCmdOppPsParam;

    DEBUGFUNC("wlanoidSetOppPsParam");
    DBGLOG(INIT, LOUD,("\n"));

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(PARAM_CUSTOM_OPPPS_PARAM_STRUC_T);

    if (u4SetBufferLen < sizeof(PARAM_CUSTOM_OPPPS_PARAM_STRUC_T)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    ASSERT(pvSetBuffer);

    prOppPsParam = (P_PARAM_CUSTOM_OPPPS_PARAM_STRUC_T)pvSetBuffer;

    kalMemZero(&rCmdOppPsParam, sizeof(CMD_CUSTOM_OPPPS_PARAM_STRUC_T));
    rCmdOppPsParam.u4CTwindowMs = prOppPsParam->u4CTwindowMs;

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_SET_OPPPS_PARAM,
            TRUE,
            FALSE,
            TRUE,
            nicCmdEventSetCommon,
            nicOidCmdTimeoutCommon,
            sizeof(CMD_CUSTOM_OPPPS_PARAM_STRUC_T),
            (PUINT_8)&rCmdOppPsParam,
            pvSetBuffer,
            u4SetBufferLen
            );
}

WLAN_STATUS
wlanoidSetUApsdParam (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    )
{
    P_PARAM_CUSTOM_UAPSD_PARAM_STRUC_T prUapsdParam;
    CMD_CUSTOM_UAPSD_PARAM_STRUC_T rCmdUapsdParam;
    P_PM_PROFILE_SETUP_INFO_T prPmProfSetupInfo;
    P_BSS_INFO_T prBssInfo;


    DEBUGFUNC("wlanoidSetUApsdParam");
    DBGLOG(INIT, LOUD,("\n"));

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(PARAM_CUSTOM_UAPSD_PARAM_STRUC_T);

    if (u4SetBufferLen < sizeof(PARAM_CUSTOM_UAPSD_PARAM_STRUC_T)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    ASSERT(pvSetBuffer);

    prBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_P2P_INDEX]);
    prPmProfSetupInfo = &prBssInfo->rPmProfSetupInfo;

    prUapsdParam = (P_PARAM_CUSTOM_UAPSD_PARAM_STRUC_T)pvSetBuffer;

    kalMemZero(&rCmdUapsdParam, sizeof(CMD_CUSTOM_OPPPS_PARAM_STRUC_T));
    rCmdUapsdParam.fgEnAPSD = prUapsdParam->fgEnAPSD;
    prAdapter->rWifiVar.fgSupportUAPSD = prUapsdParam->fgEnAPSD;

    rCmdUapsdParam.fgEnAPSD_AcBe = prUapsdParam->fgEnAPSD_AcBe;
    rCmdUapsdParam.fgEnAPSD_AcBk = prUapsdParam->fgEnAPSD_AcBk;
    rCmdUapsdParam.fgEnAPSD_AcVo = prUapsdParam->fgEnAPSD_AcVo;
    rCmdUapsdParam.fgEnAPSD_AcVi = prUapsdParam->fgEnAPSD_AcVi;
    prPmProfSetupInfo->ucBmpDeliveryAC  =
        ((prUapsdParam->fgEnAPSD_AcBe << 0) |
        (prUapsdParam->fgEnAPSD_AcBk << 1) |
        (prUapsdParam->fgEnAPSD_AcVi << 2) |
        (prUapsdParam->fgEnAPSD_AcVo << 3));
    prPmProfSetupInfo->ucBmpTriggerAC  =
        ((prUapsdParam->fgEnAPSD_AcBe << 0) |
        (prUapsdParam->fgEnAPSD_AcBk << 1) |
        (prUapsdParam->fgEnAPSD_AcVi << 2) |
        (prUapsdParam->fgEnAPSD_AcVo << 3));

    rCmdUapsdParam.ucMaxSpLen = prUapsdParam->ucMaxSpLen;
    prPmProfSetupInfo->ucUapsdSp  = prUapsdParam->ucMaxSpLen;

    return wlanSendSetQueryCmd(prAdapter,
            CMD_ID_SET_UAPSD_PARAM,
            TRUE,
            FALSE,
            TRUE,
            nicCmdEventSetCommon,
            nicOidCmdTimeoutCommon,
            sizeof(CMD_CUSTOM_OPPPS_PARAM_STRUC_T),
            (PUINT_8)&rCmdUapsdParam,
            pvSetBuffer,
            u4SetBufferLen
            );
}
#endif

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set BT profile or BT information and the
*        driver will set the built-in PTA configuration into chip.
*
*
* \param[in] prAdapter      Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetBT (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{

    P_PTA_IPC_T   prPtaIpc;

    DEBUGFUNC("wlanoidSetBT.\n");

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(PTA_IPC_T);
    if (u4SetBufferLen != sizeof(PTA_IPC_T)) {
        WARNLOG(("Invalid length %ld\n", u4SetBufferLen));
        return WLAN_STATUS_INVALID_LENGTH;
    }

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail to set BT profile because of ACPI_D3\n"));
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }

    ASSERT(pvSetBuffer);
    prPtaIpc = (P_PTA_IPC_T)pvSetBuffer;

#if CFG_SUPPORT_BCM && CFG_SUPPORT_BCM_BWCS && CFG_SUPPORT_BCM_BWCS_DEBUG
    printk(KERN_INFO DRV_NAME "BCM BWCS CMD: BWCS CMD = %02x%02x%02x%02x\n",
            prPtaIpc->u.aucBTPParams[0], prPtaIpc->u.aucBTPParams[1], prPtaIpc->u.aucBTPParams[2], prPtaIpc->u.aucBTPParams[3]);

    printk(KERN_INFO DRV_NAME "BCM BWCS CMD: aucBTPParams[0] = %02x, aucBTPParams[1] = %02x, aucBTPParams[2] = %02x, aucBTPParams[3] = %02x.\n",
            prPtaIpc->u.aucBTPParams[0],
            prPtaIpc->u.aucBTPParams[1],
            prPtaIpc->u.aucBTPParams[2],
            prPtaIpc->u.aucBTPParams[3]);
#endif

    wlanSendSetQueryCmd(prAdapter,
            CMD_ID_SET_BWCS,
            TRUE,
            FALSE,
            FALSE,
            NULL,
            NULL,
            sizeof(PTA_IPC_T),
            (PUINT_8)prPtaIpc,
            NULL,
            0);

    return WLAN_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query current BT profile and BTCR values
*
* \param[in] prAdapter          Pointer to the Adapter structure.
* \param[in] pvQueryBuffer      Pointer to the buffer that holds the result of
*                               the query.
* \param[in] u4QueryBufferLen   The length of the query buffer.
* \param[out] pu4QueryInfoLen   If the call is successful, returns the number of
*                               bytes written into the query buffer. If the call
*                               failed due to invalid length of the query buffer,
*                               returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryBT (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    )
{
//    P_PARAM_PTA_IPC_T prPtaIpc;
//    UINT_32 u4QueryBuffLen;

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    *pu4QueryInfoLen = sizeof(PTA_IPC_T);

    /* Check for query buffer length */
    if (u4QueryBufferLen != sizeof(PTA_IPC_T)) {
        DBGLOG(REQ, WARN, ("Invalid length %lu\n", u4QueryBufferLen));
        return WLAN_STATUS_INVALID_LENGTH;
    }

    ASSERT(pvQueryBuffer);
//    prPtaIpc = (P_PTA_IPC_T)pvQueryBuffer;
//    prPtaIpc->ucCmd = BT_CMD_PROFILE;
//    prPtaIpc->ucLen = sizeof(prPtaIpc->u);
//    nicPtaGetProfile(prAdapter, (PUINT_8)&prPtaIpc->u, &u4QueryBuffLen);

    return WLAN_STATUS_SUCCESS;
}

#if 0
WLAN_STATUS
wlanoidQueryBtSingleAntenna (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    )
{
    P_PTA_INFO_T prPtaInfo;
    PUINT_32 pu4SingleAntenna;

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    *pu4QueryInfoLen = sizeof(UINT_32);

    /* Check for query buffer length */
    if (u4QueryBufferLen != sizeof(UINT_32)) {
        DBGLOG(REQ, WARN, ("Invalid length %lu\n", u4QueryBufferLen));
        return WLAN_STATUS_INVALID_LENGTH;
    }

    ASSERT(pvQueryBuffer);

    prPtaInfo = &prAdapter->rPtaInfo;
    pu4SingleAntenna = (PUINT_32)pvQueryBuffer;

    if(prPtaInfo->fgSingleAntenna) {
        //printk(KERN_WARNING DRV_NAME"Q Single Ant = 1\r\n");
        *pu4SingleAntenna = 1;
    } else {
        //printk(KERN_WARNING DRV_NAME"Q Single Ant = 0\r\n");
        *pu4SingleAntenna = 0;
    }

    return WLAN_STATUS_SUCCESS;
}


WLAN_STATUS
wlanoidSetBtSingleAntenna (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{

    PUINT_32        pu4SingleAntenna;
    UINT_32         u4SingleAntenna;
    P_PTA_INFO_T    prPtaInfo;

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    prPtaInfo = &prAdapter->rPtaInfo;

    *pu4SetInfoLen = sizeof(UINT_32);
    if (u4SetBufferLen != sizeof(UINT_32)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    if (IS_ARB_IN_RFTEST_STATE(prAdapter)) {
        return WLAN_STATUS_SUCCESS;
    }

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail to set antenna because of ACPI_D3\n"));
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }

    ASSERT(pvSetBuffer);
    pu4SingleAntenna = (PUINT_32)pvSetBuffer;
    u4SingleAntenna = *pu4SingleAntenna;

    if (u4SingleAntenna == 0) {
        //printk(KERN_WARNING DRV_NAME"Set Single Ant = 0\r\n");
        prPtaInfo->fgSingleAntenna = FALSE;
    } else {
        //printk(KERN_WARNING DRV_NAME"Set Single Ant = 1\r\n");
        prPtaInfo->fgSingleAntenna = TRUE;
    }
    ptaFsmRunEventSetConfig(prAdapter, &prPtaInfo->rPtaParam);

    return WLAN_STATUS_SUCCESS;
}


#if CFG_SUPPORT_BCM && CFG_SUPPORT_BCM_BWCS
WLAN_STATUS
wlanoidQueryPta (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    )
{
    P_PTA_INFO_T prPtaInfo;
    PUINT_32 pu4Pta;

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    *pu4QueryInfoLen = sizeof(UINT_32);

    /* Check for query buffer length */
    if (u4QueryBufferLen != sizeof(UINT_32)) {
        DBGLOG(REQ, WARN, ("Invalid length %lu\n", u4QueryBufferLen));
        return WLAN_STATUS_INVALID_LENGTH;
    }

    ASSERT(pvQueryBuffer);

    prPtaInfo = &prAdapter->rPtaInfo;
    pu4Pta = (PUINT_32)pvQueryBuffer;

    if(prPtaInfo->fgEnabled) {
        //printk(KERN_WARNING DRV_NAME"PTA = 1\r\n");
        *pu4Pta = 1;
    } else {
        //printk(KERN_WARNING DRV_NAME"PTA = 0\r\n");
        *pu4Pta = 0;
    }

    return WLAN_STATUS_SUCCESS;
}


WLAN_STATUS
wlanoidSetPta (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{
    PUINT_32    pu4PtaCtrl;
    UINT_32     u4PtaCtrl;

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(UINT_32);
    if (u4SetBufferLen != sizeof(UINT_32)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    if (IS_ARB_IN_RFTEST_STATE(prAdapter)) {
        return WLAN_STATUS_SUCCESS;
    }

    if (prAdapter->rAcpiState == ACPI_STATE_D3) {
        DBGLOG(REQ, WARN, ("Fail to set BT setting because of ACPI_D3\n"));
        return WLAN_STATUS_ADAPTER_NOT_READY;
    }

    ASSERT(pvSetBuffer);
    pu4PtaCtrl = (PUINT_32)pvSetBuffer;
    u4PtaCtrl = *pu4PtaCtrl;

    if (u4PtaCtrl == 0) {
        //printk(KERN_WARNING DRV_NAME"Set Pta= 0\r\n");
        nicPtaSetFunc(prAdapter, FALSE);
    } else {
        //printk(KERN_WARNING DRV_NAME"Set Pta= 1\r\n");
        nicPtaSetFunc(prAdapter, TRUE);
    }

    return WLAN_STATUS_SUCCESS;
}
#endif

#endif

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set Tx power profile.
*
*
* \param[in] prAdapter      Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetTxPower (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{
    //P_SET_TXPWR_CTRL_T pTxPwr = (P_SET_TXPWR_CTRL_T)pvSetBuffer;
    //UINT_32 i;
    WLAN_STATUS     rStatus;

    DEBUGFUNC("wlanoidSetTxPower");
    DBGLOG(REQ, LOUD, ("\r\n"));

    ASSERT(prAdapter);
    ASSERT(pvSetBuffer);

#if 0
    printk("c2GLegacyStaPwrOffset=%d\n", pTxPwr->c2GLegacyStaPwrOffset);
    printk("c2GHotspotPwrOffset=%d\n", pTxPwr->c2GHotspotPwrOffset);
    printk("c2GP2pPwrOffset=%d\n", pTxPwr->c2GP2pPwrOffset);
    printk("c2GBowPwrOffset=%d\n", pTxPwr->c2GBowPwrOffset);
    printk("c5GLegacyStaPwrOffset=%d\n", pTxPwr->c5GLegacyStaPwrOffset);
    printk("c5GHotspotPwrOffset=%d\n", pTxPwr->c5GHotspotPwrOffset);
    printk("c5GP2pPwrOffset=%d\n", pTxPwr->c5GP2pPwrOffset);
    printk("c5GBowPwrOffset=%d\n", pTxPwr->c5GBowPwrOffset);
    printk("ucConcurrencePolicy=%d\n", pTxPwr->ucConcurrencePolicy);

    for (i=0; i<14;i++)
        printk("acTxPwrLimit2G[%d]=%d\n", i, pTxPwr->acTxPwrLimit2G[i]);

    for (i=0; i<4;i++)
        printk("acTxPwrLimit5G[%d]=%d\n", i, pTxPwr->acTxPwrLimit5G[i]);
#endif

    rStatus = wlanSendSetQueryCmd (
                prAdapter,                  /* prAdapter */
                CMD_ID_SET_TXPWR_CTRL,      /* ucCID */
                TRUE,                       /* fgSetQuery */
                FALSE,                      /* fgNeedResp */
                TRUE,                       /* fgIsOid */
                NULL,                       /* pfCmdDoneHandler*/
                NULL,                       /* pfCmdTimeoutHandler */
                u4SetBufferLen,             /* u4SetQueryInfoLen */
                (PUINT_8) pvSetBuffer,      /* pucInfoBuffer */
                NULL,                       /* pvSetQueryBuffer */
                0                           /* u4SetQueryBufferLen */
                );

    ASSERT(rStatus == WLAN_STATUS_PENDING);

    return rStatus;

}

WLAN_STATUS
wlanSendMemDumpCmd (
    IN P_ADAPTER_T  prAdapter,
    IN PVOID        pvQueryBuffer,
    IN UINT_32      u4QueryBufferLen
    )
{
    P_PARAM_CUSTOM_MEM_DUMP_STRUC_T prMemDumpInfo;
    P_CMD_DUMP_MEM prCmdDumpMem;
    CMD_DUMP_MEM rCmdDumpMem;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4MemSize = PARAM_MEM_DUMP_MAX_SIZE;

    UINT_32 u4RemainLeng = 0;
    UINT_32 u4CurAddr = 0;
    UINT_8  ucFragNum = 0;

    prCmdDumpMem = &rCmdDumpMem;
    prMemDumpInfo = (P_PARAM_CUSTOM_MEM_DUMP_STRUC_T)pvQueryBuffer;

    u4RemainLeng = prMemDumpInfo->u4RemainLength;
    u4CurAddr = prMemDumpInfo->u4Address + prMemDumpInfo->u4Length;
    ucFragNum = prMemDumpInfo->ucFragNum + 1;

    /* Query. If request length is larger than max length, do it as ping pong.
         * Send a command and wait for a event. Send next command while the event is received.
         *
         */
    do{
        UINT_32 u4CurLeng = 0;

        if(u4RemainLeng > u4MemSize) {
            u4CurLeng = u4MemSize;
            u4RemainLeng -= u4MemSize;
        } else {
            u4CurLeng = u4RemainLeng;
            u4RemainLeng = 0;
        }

        prCmdDumpMem->u4Address = u4CurAddr;
        prCmdDumpMem->u4Length = u4CurLeng;
        prCmdDumpMem->u4RemainLength = u4RemainLeng;
        prCmdDumpMem->ucFragNum = ucFragNum;

        DBGLOG(REQ, TRACE, ("[%d] 0x%X, len %d, remain len %d\n",
            ucFragNum,
            prCmdDumpMem->u4Address,
            prCmdDumpMem->u4Length,
            prCmdDumpMem->u4RemainLength));

        rStatus = wlanSendSetQueryCmd(prAdapter,
                CMD_ID_DUMP_MEM,
                FALSE,
                TRUE,
                TRUE,
                nicCmdEventQueryMemDump,
                nicOidCmdTimeoutCommon,
                sizeof(CMD_DUMP_MEM),
                (PUINT_8)prCmdDumpMem,
                pvQueryBuffer,
                u4QueryBufferLen
                );

    }while(FALSE);

    return rStatus;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to dump memory.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuf A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryMemDump (
    IN P_ADAPTER_T  prAdapter,
    IN PVOID        pvQueryBuffer,
    IN UINT_32      u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    )
{
    P_PARAM_CUSTOM_MEM_DUMP_STRUC_T prMemDumpInfo;

    DEBUGFUNC("wlanoidQueryMemDump");
    DBGLOG(INIT, LOUD,("\n"));

    ASSERT(prAdapter);
    ASSERT(pu4QueryInfoLen);
    if (u4QueryBufferLen) {
        ASSERT(pvQueryBuffer);
    }

    *pu4QueryInfoLen = sizeof(UINT_32);

    prMemDumpInfo = (P_PARAM_CUSTOM_MEM_DUMP_STRUC_T)pvQueryBuffer;
    DBGLOG(REQ, TRACE, ("Dump 0x%X, len %d\n", prMemDumpInfo->u4Address, prMemDumpInfo->u4Length));

    prMemDumpInfo->u4RemainLength = prMemDumpInfo->u4Length;
    prMemDumpInfo->u4Length = 0;
    prMemDumpInfo->ucFragNum = 0;

    return wlanSendMemDumpCmd(
                prAdapter,
                pvQueryBuffer,
                u4QueryBufferLen);

} /* end of wlanoidQueryMcrRead() */


#if CFG_ENABLE_WIFI_DIRECT
/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to set the p2p mode.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetP2pMode (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    )
{
    WLAN_STATUS status;
    P_PARAM_CUSTOM_P2P_SET_STRUC_T prSetP2P = (P_PARAM_CUSTOM_P2P_SET_STRUC_T)NULL;
    //P_MSG_P2P_NETDEV_REGISTER_T prP2pNetdevRegMsg = (P_MSG_P2P_NETDEV_REGISTER_T)NULL;
    DEBUGFUNC("wlanoidSetP2pMode");

    ASSERT(prAdapter);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(PARAM_CUSTOM_P2P_SET_STRUC_T);
    if (u4SetBufferLen < sizeof(PARAM_CUSTOM_P2P_SET_STRUC_T)) {
        DBGLOG(REQ, WARN, ("Invalid length %ld\n", u4SetBufferLen));
        return WLAN_STATUS_INVALID_LENGTH;
    }

    prSetP2P = (P_PARAM_CUSTOM_P2P_SET_STRUC_T) pvSetBuffer;

    DBGLOG(P2P, INFO, ("Set P2P enable[%ld] mode[%ld]\n", prSetP2P->u4Enable, prSetP2P->u4Mode));

    /*
        *    enable = 1, mode = 0  => init P2P network
        *    enable = 1, mode = 1  => init Soft AP network
        *    enable = 0                   => uninit P2P/AP network
        */

    if (prSetP2P->u4Enable) {
        p2pSetMode((prSetP2P->u4Mode == 1)?TRUE:FALSE);

        if (p2pLaunch(prAdapter->prGlueInfo)) {
            ASSERT(prAdapter->fgIsP2PRegistered);
        }

    }
    else {
        if (prAdapter->fgIsP2PRegistered) {
            p2pRemove(prAdapter->prGlueInfo);
        }

    }


#if 0
    prP2pNetdevRegMsg = (P_MSG_P2P_NETDEV_REGISTER_T)cnmMemAlloc(
                                                            prAdapter,
                                                            RAM_TYPE_MSG,
                                                            (sizeof(MSG_P2P_NETDEV_REGISTER_T)));

    if (prP2pNetdevRegMsg == NULL) {
        ASSERT(FALSE);
        status = WLAN_STATUS_RESOURCES;
        return status;
    }


    prP2pNetdevRegMsg->rMsgHdr.eMsgId = MID_MNY_P2P_NET_DEV_REGISTER;
    prP2pNetdevRegMsg->fgIsEnable = (prSetP2P->u4Enable == 1)?TRUE:FALSE;
    prP2pNetdevRegMsg->ucMode = (UINT_8)prSetP2P->u4Mode;

    mboxSendMsg(prAdapter,
            MBOX_ID_0,
            (P_MSG_HDR_T)prP2pNetdevRegMsg,
            MSG_SEND_METHOD_BUF);
#endif

    return status;

}
#endif


