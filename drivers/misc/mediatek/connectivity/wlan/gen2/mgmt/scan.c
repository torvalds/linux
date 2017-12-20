/*
** Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/mgmt/scan.c#3
*/

/*! \file   "scan.c"
    \brief  This file defines the scan profile and the processing function of
	    scan result for SCAN Module.

    The SCAN Profile selection is part of SCAN MODULE and responsible for defining
    SCAN Parameters - e.g. MIN_CHANNEL_TIME, number of scan channels.
    In this file we also define the process of SCAN Result including adding, searching
    and removing SCAN record from the list.
*/

/*
** Log: scan.c
**
** 01 30 2013 yuche.tsai
** [ALPS00451578] [JB2][WFD][Case Fail][JE][MR1]?????????[Java (JE),660,-1361051648,99,
** /data/core/,0,system_server_crash,system_server]JE happens when try to connect WFD.(4/5)
** Fix possible old scan result indicate to supplicant after formation.
**
** 01 16 2013 yuche.tsai
** [ALPS00431980] [WFD]Aupus one ?play game 10 minitues?wfd connection automaticlly disconnect
** Fix possible FW assert issue.
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Let netdev bring up.
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Compile no error before trial run.
 *
 * 06 25 2012 cp.wu
 * [WCXRP00001258] [MT6620][MT5931][MT6628][Driver] Do not use stale scan result for deciding connection target
 * drop off scan result which is older than 5 seconds when choosing which BSS to join
 *
 * 03 02 2012 terry.wu
 * NULL
 * Sync CFG80211 modification from branch 2,2.
 *
 * 01 16 2012 cp.wu
 * [WCXRP00001169] [MT6620 Wi-Fi][Driver] API and behavior modification for preferred band
 * configuration with corresponding network configuration
 * correct typo.
 *
 * 01 16 2012 cp.wu
 * [MT6620 Wi-Fi][Driver] API and behavior modification for preferred band configuration
 * with corresponding network configuration
 * add wlanSetPreferBandByNetwork() for glue layer to invoke for setting preferred
 * band configuration corresponding to network type.
 *
 * 12 05 2011 cp.wu
 * [WCXRP00001131] [MT6620 Wi-Fi][Driver][AIS] Implement connect-by-BSSID path
 * add CONNECT_BY_BSSID policy
 *
 * 11 23 2011 cp.wu
 * [WCXRP00001123] [MT6620 Wi-Fi][Driver] Add option to disable beacon content change detection
 * add compile option to disable beacon content change detection.
 *
 * 11 04 2011 cp.wu
 * [WCXRP00001085] [MT6628 Wi-Fi][Driver] deprecate old BSS-DESC if timestamp
 * is reset with received beacon/probe response frames
 * deprecate old BSS-DESC when timestamp in received beacon/probe response frames showed a smaller value than before
 *
 * 10 11 2011 cm.chang
 * [WCXRP00001031] [All Wi-Fi][Driver] Check HT IE length to avoid wrong SCO parameter
 * Ignore HT OP IE if its length field is not valid
 *
 * 09 30 2011 cp.wu
 * [WCXRP00001021] [MT5931][Driver] Correct scan result generation for conversion between BSS type and operation mode
 * correct type casting issue.
 *
 * 08 23 2011 yuche.tsai
 * NULL
 * Fix multicast address list issue.
 *
 * 08 11 2011 cp.wu
 * [WCXRP00000830] [MT6620 Wi-Fi][Firmware] Use MDRDY counter to detect empty channel for shortening scan time
 * sparse channel detection:
 * driver: collect sparse channel information with scan-done event
 *
 * 08 10 2011 cp.wu
 * [WCXRP00000922] [MT6620 Wi-Fi][Driver] traverse whole BSS-DESC list for removing
 * traverse whole BSS-DESC list because BSSID is not unique anymore.
 *
 * 07 12 2011 cp.wu
 * [WCXRP00000815] [MT6620 Wi-Fi][Driver] allow single BSSID with multiple
 * SSID settings to work around some tricky AP which use space character as hidden SSID
 * for multiple BSS descriptior detecting issue:
 * 1) check BSSID for infrastructure network
 * 2) check SSID for AdHoc network
 *
 * 07 12 2011 cp.wu
 * [WCXRP00000815] [MT6620 Wi-Fi][Driver] allow single BSSID with multiple
 * SSID settings to work around some tricky AP which use space character as hidden SSID
 * check for BSSID for beacons used to update DTIM
 *
 * 07 12 2011 cp.wu
 * [WCXRP00000815] [MT6620 Wi-Fi][Driver] allow single BSSID with multiple
 * SSID settings to work around some tricky AP which use space character as hidden SSID
 * do not check BSS descriptor for connected flag due to linksys's hidden
 * SSID will use another BSS descriptor and never connected
 *
 * 07 11 2011 cp.wu
 * [WCXRP00000815] [MT6620 Wi-Fi][Driver] allow single BSSID with multiple
 * SSID settings to work around some tricky AP which use space character as hidden SSID
 * just pass beacons with the same BSSID.
 *
 * 07 11 2011 wh.su
 * [WCXRP00000849] [MT6620 Wi-Fi][Driver] Remove some of the WAPI define
 * for make sure the value is initialize, for customer not enable WAPI
 * For make sure wapi initial value is set.
 *
 * 06 28 2011 cp.wu
 * [WCXRP00000815] [MT6620 Wi-Fi][Driver] allow single BSSID with multiple
 * SSID settings to work around some tricky AP which use space character as hidden SSID
 * Do not check for SSID as beacon content change due to the existence of
 * single BSSID with multiple SSID AP configuration
 *
 * 06 27 2011 cp.wu
 * [WCXRP00000815] [MT6620 Wi-Fi][Driver] allow single BSSID with multiple
 * SSID settings to work around some tricky AP which use space character as hidden SSID
 * 1. correct logic
 * 2. replace only BSS-DESC which doesn't have a valid SSID.
 *
 * 06 27 2011 cp.wu
 * [WCXRP00000815] [MT6620 Wi-Fi][Driver] allow single BSSID with multiple SSID
 * settings to work around some tricky AP which use space character as hidden SSID
 * remove unused temporal variable reference.
 *
 * 06 27 2011 cp.wu
 * [WCXRP00000815] [MT6620 Wi-Fi][Driver] allow single BSSID with multiple SSID
 * settings to work around some tricky AP which use space character as hidden SSID
 * allow to have a single BSSID with multiple SSID to be presented in scanning result
 *
 * 06 02 2011 cp.wu
 * [WCXRP00000757] [MT6620 Wi-Fi][Driver][SCN] take use of RLM API to filter out BSS in disallowed channels
 * filter out BSS in disallowed channel by
 * 1. do not add to scan result array if BSS is at disallowed channel
 * 2. do not allow to search for BSS-DESC in disallowed channels
 *
 * 05 02 2011 cm.chang
 * [WCXRP00000691] [MT6620 Wi-Fi][Driver] Workaround about AP's wrong HT capability IE to have wrong channel number
 * Refine range of valid channel number
 *
 * 05 02 2011 cp.wu
 * [MT6620 Wi-Fi][Driver] Take parsed result for channel information instead of
 * hardware channel number passed from firmware domain
 * take parsed result for generating scanning result with channel information.
 *
 * 05 02 2011 cm.chang
 * [WCXRP00000691] [MT6620 Wi-Fi][Driver] Workaround about AP's wrong HT capability IE to have wrong channel number
 * Check if channel is valided before record ing BSS channel
 *
 * 04 18 2011 terry.wu
 * [WCXRP00000660] [MT6620 Wi-Fi][Driver] Remove flag CFG_WIFI_DIRECT_MOVED
 * Remove flag CFG_WIFI_DIRECT_MOVED.
 *
 * 04 14 2011 cm.chang
 * [WCXRP00000634] [MT6620 Wi-Fi][Driver][FW] 2nd BSS will not support 40MHz bandwidth for concurrency
 * .
 *
 * 04 12 2011 eddie.chen
 * [WCXRP00000617] [MT6620 Wi-Fi][DRV/FW] Fix for sigma
 * Fix the sta index in processing security frame
 * Simple flow control for TC4 to avoid mgt frames for PS STA to occupy the TC4
 * Add debug message.
 *
 * 03 25 2011 yuche.tsai
 * NULL
 * Always update Bss Type, for Bss Type for P2P Network is changing every time.
 *
 * 03 23 2011 yuche.tsai
 * NULL
 * Fix concurrent issue when AIS scan result would overwrite p2p scan result.
 *
 * 03 14 2011 cp.wu
 * [WCXRP00000535] [MT6620 Wi-Fi][Driver] Fixed channel operation when AIS and Tethering are operating concurrently
 * filtering out other BSS coming from adjacent channels
 *
 * 03 11 2011 chinglan.wang
 * [WCXRP00000537] [MT6620 Wi-Fi][Driver] Can not connect to 802.11b/g/n mixed AP with WEP security.
 * .
 *
 * 03 11 2011 cp.wu
 * [WCXRP00000535] [MT6620 Wi-Fi][Driver] Fixed channel operation when AIS and Tethering are operating concurrently
 * When fixed channel operation is necessary, AIS-FSM would scan and only connect for BSS on the specific channel
 *
 * 02 24 2011 cp.wu
 * [WCXRP00000490] [MT6620 Wi-Fi][Driver][Win32] modify kalMsleep() implementation because NdisMSleep()
 * won't sleep long enough for specified interval such as 500ms
 * implement beacon change detection by checking SSID and supported rate.
 *
 * 02 22 2011 yuche.tsai
 * [WCXRP00000480] [Volunteer Patch][MT6620][Driver] WCS IE format issue
 * Fix WSC big endian issue.
 *
 * 02 21 2011 terry.wu
 * [WCXRP00000476] [MT6620 Wi-Fi][Driver] Clean P2P scan list while removing P2P
 * Clean P2P scan list while removing P2P.
 *
 * 01 27 2011 yuche.tsai
 * [WCXRP00000399] [Volunteer Patch][MT6620/MT5931][Driver] Fix scan side effect after P2P module separate.
 * Fix scan channel extension issue when p2p module is not registered.
 *
 * 01 26 2011 cm.chang
 * [WCXRP00000395] [MT6620 Wi-Fi][Driver][FW] Search STA_REC with additional net type index argument
 * .
 *
 * 01 21 2011 cp.wu
 * [WCXRP00000380] [MT6620 Wi-Fi][Driver] SSID information should come from buffered
 * BSS_DESC_T rather than using beacon-carried information
 * SSID should come from buffered prBssDesc rather than beacon-carried information
 *
 * 01 14 2011 yuche.tsai
 * [WCXRP00000352] [Volunteer Patch][MT6620][Driver] P2P Statsion Record Client List Issue
 * Fix compile error.
 *
 * 01 14 2011 yuche.tsai
 * [WCXRP00000352] [Volunteer Patch][MT6620][Driver] P2P Statsion Record Client List Issue
 * Memfree for P2P Descriptor & P2P Descriptor List.
 *
 * 01 14 2011 yuche.tsai
 * [WCXRP00000352] [Volunteer Patch][MT6620][Driver] P2P Statsion Record Client List Issue
 * Free P2P Descriptor List & Descriptor under BSS Descriptor.
 *
 * 01 04 2011 cp.wu
 * [WCXRP00000338] [MT6620 Wi-Fi][Driver] Separate kalMemAlloc into kmalloc
 * and vmalloc implementations to ease physically continuous memory demands
 * 1) correct typo in scan.c
 * 2) TX descriptors, RX descriptos and management buffer should use virtually
 * continuous buffer instead of physically continuous one
 *
 * 01 04 2011 cp.wu
 * [WCXRP00000338] [MT6620 Wi-Fi][Driver] Separate kalMemAlloc into kmalloc
 * and vmalloc implementations to ease physically continuous memory demands
 * separate kalMemAlloc() into virtually-continuous and physically-continuous type to ease slab system pressure
 *
 * 12 31 2010 cp.wu
 * [WCXRP00000327] [MT6620 Wi-Fi][Driver] Improve HEC WHQA 6972 workaround coverage in driver side
 * while being unloaded, clear all pending interrupt then set LP-own to firmware
 *
 * 12 21 2010 cp.wu
 * [WCXRP00000280] [MT6620 Wi-Fi][Driver] Enable BSS selection with best RCPI policy in SCN module
 * SCN: enable BEST RSSI selection policy support
 *
 * 11 29 2010 cp.wu
 * [WCXRP00000210] [MT6620 Wi-Fi][Driver][FW] Set RCPI value in STA_REC
 * for initial TX rate selection of auto-rate algorithm
 * update ucRcpi of STA_RECORD_T for AIS when
 * 1) Beacons for IBSS merge is received
 * 2) Associate Response for a connecting peer is received
 *
 * 11 03 2010 wh.su
 * [WCXRP00000124] [MT6620 Wi-Fi] [Driver] Support the dissolve P2P Group
 * Refine the HT rate disallow TKIP pairwise cipher .
 *
 * 10 12 2010 cp.wu
 * [WCXRP00000091] [MT6620 Wi-Fi][Driver] Add scanning logic to filter out
 * beacons which is received on the folding frequency
 * trust HT IE if available for 5GHz band
 *
 * 10 11 2010 cp.wu
 * [WCXRP00000091] [MT6620 Wi-Fi][Driver] Add scanning logic to filter out
 * beacons which is received on the folding frequency
 * add timing and strenght constraint for filtering out beacons with same SSID/TA but received on different channels
 *
 * 10 08 2010 wh.su
 * [WCXRP00000085] [MT6620 Wif-Fi] [Driver] update the modified p2p state machine
 * update the frog's new p2p state machine.
 *
 * 10 01 2010 yuche.tsai
 * NULL
 * [MT6620 P2P] Fix Big Endian Issue when parse P2P device name TLV.
 *
 * 09 24 2010 cp.wu
 * [WCXRP00000052] [MT6620 Wi-Fi][Driver] Eliminate Linux Compile Warning
 * eliminate unused variables which lead gcc to argue
 *
 * 09 08 2010 cp.wu
 * NULL
 * use static memory pool for storing IEs of scanning result.
 *
 * 09 07 2010 yuche.tsai
 * NULL
 * When indicate scan result, append IE buffer information in the scan result.
 *
 * 09 03 2010 yuche.tsai
 * NULL
 * 1. Update Beacon RX count when running SLT.
 * 2. Ignore Beacon when running SLT, would not update information from Beacon.
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 08 31 2010 kevin.huang
 * NULL
 * Use LINK LIST operation to process SCAN result
 *
 * 08 29 2010 yuche.tsai
 * NULL
 * 1. Fix P2P Descriptor List to be a link list, to avoid link corrupt after Bss Descriptor Free.
 * 2.. Fix P2P Device Name Length BE issue.
 *
 * 08 23 2010 yuche.tsai
 * NULL
 * Add P2P Device Found Indication to supplicant
 *
 * 08 20 2010 cp.wu
 * NULL
 * reset BSS_DESC_T variables before parsing IE due to peer might have been reconfigured.
 *
 * 08 20 2010 yuche.tsai
 * NULL
 * Workaround for P2P Descriptor Infinite loop issue.
 *
 * 08 16 2010 cp.wu
 * NULL
 * Replace CFG_SUPPORT_BOW by CFG_ENABLE_BT_OVER_WIFI.
 * There is no CFG_SUPPORT_BOW in driver domain source.
 *
 * 08 16 2010 yuche.tsai
 * NULL
 * Modify code of processing Probe Resonse frame for P2P.
 *
 * 08 12 2010 yuche.tsai
 * NULL
 * Add function to get P2P descriptor of BSS descriptor directly.
 *
 * 08 11 2010 yuche.tsai
 * NULL
 * Modify Scan result processing for P2P module.
 *
 * 08 05 2010 yuche.tsai
 * NULL
 * Update P2P Device Discovery result add function.
 *
 * 08 03 2010 cp.wu
 * NULL
 * surpress compilation warning.
 *
 * 07 26 2010 yuche.tsai
 *
 * Add support for Probe Request & Response parsing.
 *
 * 07 21 2010 cp.wu
 *
 * 1) change BG_SCAN to ONLINE_SCAN for consistent term
 * 2) only clear scanning result when scan is permitted to do
 *
 * 07 21 2010 yuche.tsai
 *
 * Fix compile error for SCAN module while disabling P2P feature.
 *
 * 07 21 2010 yuche.tsai
 *
 * Add P2P Scan & Scan Result Parsing & Saving.
 *
 * 07 19 2010 wh.su
 *
 * update for security supporting.
 *
 * 07 19 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * Add Ad-Hoc support to AIS-FSM
 *
 * 07 19 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * SCN module is now able to handle multiple concurrent scanning requests
 *
 * 07 15 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * driver no longer generates probe request frames
 *
 * 07 14 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * remove timer in DRV-SCN.
 *
 * 07 09 2010 cp.wu
 *
 * 1) separate AIS_FSM state for two kinds of scanning. (OID triggered scan, and scan-for-connection)
 * 2) eliminate PRE_BSS_DESC_T, Beacon/PrebResp is now parsed in single pass
 * 3) implment DRV-SCN module, currently only accepts single scan request,
 * other request will be directly dropped by returning BUSY
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 07 08 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * take use of RLM module for parsing/generating HT IEs for 11n capability
 *
 * 07 05 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) ignore RSN checking when RSN is not turned on.
 * 2) set STA-REC deactivation callback as NULL
 * 3) add variable initialization API based on PHY configuration
 *
 * 07 05 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * correct BSS_DESC_T initialization after allocated.
 *
 * 07 02 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) for event packet, no need to fill RFB.
 * 2) when wlanAdapterStart() failed, no need to initialize state machines
 * 3) after Beacon/ProbeResp parsing, corresponding BSS_DESC_T should be marked as IE-parsed
 *
 * 07 01 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add scan uninitialization procedure
 *
 * 06 30 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * if beacon/probe-resp is received in 2.4GHz bands and there is ELEM_ID_DS_PARAM_SET IE available,
 * trust IE instead of RMAC information
 *
 * 06 29 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) sync to. CMD/EVENT document v0.03
 * 2) simplify DTIM period parsing in scan.c only, bss.c no longer parses it again.
 * 3) send command packet to indicate FW-PM after
 *     a) 1st beacon is received after AIS has connected to an AP
 *     b) IBSS-ALONE has been created
 *     c) IBSS-MERGE has occurred
 *
 * 06 28 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * send MMPDU in basic rate.
 *
 * 06 25 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * modify Beacon/ProbeResp to complete parsing,
 * because host software has looser memory usage restriction
 *
 * 06 23 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * integrate .
 *
 * 06 22 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * comment out RLM APIs by CFG_RLM_MIGRATION.
 *
 * 06 21 2010 yuche.tsai
 * [WPD00003839][MT6620 5931][P2P] Feature migration
 * Update P2P Function call.
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * RSN/PRIVACY compilation flag awareness correction
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * specify correct value for management frames.
 *
 * 06 18 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Provide cnmMgtPktAlloc() and alloc/free function of msg/buf
 *
 * 06 18 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * migration from MT6620 firmware.
 *
 * 06 17 2010 yuche.tsai
 * [WPD00003839][MT6620 5931][P2P] Feature migration
 * Fix compile error when enable P2P function.
 *
 * 06 15 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * correct when ADHOC support is turned on.
 *
 * 06 15 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add scan.c.
 *
 * 06 04 2010 george.huang
 * [BORA00000678][MT6620]WiFi LP integration
 * [PM] Support U-APSD for STA mode
 *
 * 05 28 2010 wh.su
 * [BORA00000680][MT6620] Support the statistic for Micxxsoft os query
 * adding the TKIP disallow join a HT AP code.
 *
 * 05 14 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Add more chance of JOIN retry for BG_SCAN
 *
 * 05 12 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Add Power Management - Legacy PS-POLL support.
 *
 * 04 29 2010 wh.su
 * [BORA00000637][MT6620 Wi-Fi] [Bug] WPA2 pre-authentication timer not correctly initialize
 * adjsut the pre-authentication code.
 *
 * 04 27 2010 kevin.huang
 * [BORA00000663][WIFISYS][New Feature] AdHoc Mode Support
 * Add Set Slot Time and Beacon Timeout Support for AdHoc Mode
 *
 * 04 24 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * g_aprBssInfo[] depends on CFG_SUPPORT_P2P and CFG_SUPPORT_BOW
 *
 * 04 19 2010 kevin.huang
 * [BORA00000714][WIFISYS][New Feature]Beacon Timeout Support
 * Add Beacon Timeout Support and will send Null frame to diagnose connection
 *
 * 04 13 2010 kevin.huang
 * [BORA00000663][WIFISYS][New Feature] AdHoc Mode Support
 * Add new HW CH macro support
 *
 * 04 06 2010 wh.su
 * [BORA00000680][MT6620] Support the statistic for Micxxsoft os query
 * fixed the firmware return the broadcast frame at wrong tc.
 *
 * 03 29 2010 wh.su
 * [BORA00000605][WIFISYS] Phase3 Integration
 * let the rsn wapi IE always parsing.
 *
 * 03 24 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Not carry  HT cap when being associated with b/g only AP
 *
 * 03 18 2010 kevin.huang
 * [BORA00000663][WIFISYS][New Feature] AdHoc Mode Support
 * Solve the compile warning for 'return non-void' function
 *
 * 03 16 2010 kevin.huang
 * [BORA00000663][WIFISYS][New Feature] AdHoc Mode Support
 * Add AdHoc Mode
 *
 * 03 10 2010 kevin.huang
 * [BORA00000654][WIFISYS][New Feature] CNM Module - Ch Manager Support
 *
 *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  * Add Channel Manager for arbitration of JOIN and SCAN Req
 *
 * 03 03 2010 wh.su
 * [BORA00000637][MT6620 Wi-Fi] [Bug] WPA2 pre-authentication timer not correctly initialize
 * move the AIS specific variable for security to AIS specific structure.
 *
 * 03 01 2010 wh.su
 * [BORA00000605][WIFISYS] Phase3 Integration
 * Refine the variable and parameter for security.
 *
 * 02 26 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Fix No PKT_INFO_T issue
 *
 * 02 26 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Update outgoing ProbeRequest Frame's TX data rate
 *
 * 02 23 2010 wh.su
 * [BORA00000592][MT6620 Wi-Fi] Adding the security related code for driver
 * refine the scan procedure, reduce the WPA and WAPI IE parsing, and move the parsing to the time for join.
 *
 * 02 23 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Add support scan channel 1~14 and update scan result's frequency infou1rwduu`wvpghlqg|n`slk+mpdkb
 *
 * 02 04 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Add AAA Module Support, Revise Net Type to Net Type Index for array lookup
 *
 * 01 27 2010 wh.su
 * [BORA00000476][Wi-Fi][firmware] Add the security module initialize code
 * add and fixed some security function.
 *
 * 01 22 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support protection and bandwidth switch
 *
 * 01 20 2010 kevin.huang
 * [BORA00000569][WIFISYS] Phase 2 Integration Test
 * Add PHASE_2_INTEGRATION_WORK_AROUND and CFG_SUPPORT_BCM flags
 *
 * 01 11 2010 kevin.huang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Add Deauth and Disassoc Handler
 *
 * 01 08 2010 kevin.huang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 *
 * Refine Beacon processing, add read RF channel from RX Status
 *
 * 01 04 2010 tehuang.liu
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * For working out the first connection Chariot-verified version
 *
 * 12 18 2009 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * .
 *
 * Dec 12 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Modify u2EstimatedExtraIELen for probe request
 *
 * Dec 9 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add HT cap IE to probe request
 *
 * Dec 7 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Fix lint warning
 *
 *
 * Dec 3 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Update the process of SCAN Result by adding more Phy Attributes
 *
 * Dec 1 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * adjust the function and code for meet the new define
 *
 * Nov 30 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Rename u4RSSI to i4RSSI
 *
 * Nov 30 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Report event of scan result to host
 *
 * Nov 26 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Fix SCAN Record update
 *
 * Nov 24 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Revise MGMT Handler with Retain Status and Integrate with TXM
 *
 * Nov 23 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add (Ext)Support Rate Set IE to ProbeReq
 *
 * Nov 20 2009 mtk02468
 * [BORA00000337] To check in codes for FPGA emulation
 * Removed the use of SW_RFB->u2FrameLength
 *
 * Nov 20 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Fix uninitial aucMacAddress[] for ProbeReq
 *
 * Nov 16 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add scanSearchBssDescByPolicy()
 *
 * Nov 5 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add Send Probe Request Frame
 *
 * Oct 30 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 *
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
#define REPLICATED_BEACON_TIME_THRESHOLD        (3000)
#define REPLICATED_BEACON_FRESH_PERIOD          (10000)
#define REPLICATED_BEACON_STRENGTH_THRESHOLD    (32)

#define ROAMING_NO_SWING_RCPI_STEP              (10)

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
/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used by SCN to initialize its variables
*
* @param (none)
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID scnInit(IN P_ADAPTER_T prAdapter)
{
	P_SCAN_INFO_T prScanInfo;
	P_BSS_DESC_T prBSSDesc;
	PUINT_8 pucBSSBuff;
	UINT_32 i;

	ASSERT(prAdapter);

	prScanInfo = &(prAdapter->rWifiVar.rScanInfo);
	pucBSSBuff = &prScanInfo->aucScanBuffer[0];

	DBGLOG(SCN, INFO, "->scnInit()\n");

	/* 4 <1> Reset STATE and Message List */
	prScanInfo->eCurrentState = SCAN_STATE_IDLE;

	prScanInfo->rLastScanCompletedTime = (OS_SYSTIME) 0;

	LINK_INITIALIZE(&prScanInfo->rPendingMsgList);

	/* 4 <2> Reset link list of BSS_DESC_T */
	kalMemZero((PVOID) pucBSSBuff, SCN_MAX_BUFFER_SIZE);

	LINK_INITIALIZE(&prScanInfo->rFreeBSSDescList);
	LINK_INITIALIZE(&prScanInfo->rBSSDescList);

	for (i = 0; i < CFG_MAX_NUM_BSS_LIST; i++) {

		prBSSDesc = (P_BSS_DESC_T) pucBSSBuff;

		LINK_INSERT_TAIL(&prScanInfo->rFreeBSSDescList, &prBSSDesc->rLinkEntry);

		pucBSSBuff += ALIGN_4(sizeof(BSS_DESC_T));
	}
	/* Check if the memory allocation consist with this initialization function */
	ASSERT(((ULONG) pucBSSBuff - (ULONG)&prScanInfo->aucScanBuffer[0]) == SCN_MAX_BUFFER_SIZE);

	/* reset freest channel information */
	prScanInfo->fgIsSparseChannelValid = FALSE;

	/* reset NLO state */
	prScanInfo->fgNloScanning = FALSE;
	prScanInfo->fgPscnOnnning = FALSE;

	prScanInfo->prPscnParam = kalMemAlloc(sizeof(PSCN_PARAM_T), VIR_MEM_TYPE);
	if (prScanInfo->prPscnParam)
		kalMemZero(prScanInfo->prPscnParam, sizeof(PSCN_PARAM_T));

}				/* end of scnInit() */

/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used by SCN to uninitialize its variables
*
* @param (none)
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID scnUninit(IN P_ADAPTER_T prAdapter)
{
	P_SCAN_INFO_T prScanInfo;

	ASSERT(prAdapter);
	prScanInfo = &(prAdapter->rWifiVar.rScanInfo);

	DBGLOG(SCN, INFO, "->scnUninit()\n");

	/* 4 <1> Reset STATE and Message List */
	prScanInfo->eCurrentState = SCAN_STATE_IDLE;

	prScanInfo->rLastScanCompletedTime = (OS_SYSTIME) 0;

	/* NOTE(Kevin): Check rPendingMsgList ? */

	/* 4 <2> Reset link list of BSS_DESC_T */
	LINK_INITIALIZE(&prScanInfo->rFreeBSSDescList);
	LINK_INITIALIZE(&prScanInfo->rBSSDescList);

	kalMemFree(prScanInfo->prPscnParam, VIR_MEM_TYPE, sizeof(PSCN_PARAM_T));

}				/* end of scnUninit() */

/*----------------------------------------------------------------------------*/
/*!
* @brief Find the corresponding BSS Descriptor according to given BSSID
*
* @param[in] prAdapter          Pointer to the Adapter structure.
* @param[in] aucBSSID           Given BSSID.
*
* @return   Pointer to BSS Descriptor, if found. NULL, if not found
*/
/*----------------------------------------------------------------------------*/
P_BSS_DESC_T scanSearchBssDescByBssid(IN P_ADAPTER_T prAdapter, IN UINT_8 aucBSSID[])
{
	return scanSearchBssDescByBssidAndSsid(prAdapter, aucBSSID, FALSE, NULL);
}

/*----------------------------------------------------------------------------*/
/*!
* @brief Find the corresponding BSS Descriptor according to given BSSID
*
* @param[in] prAdapter          Pointer to the Adapter structure.
* @param[in] aucBSSID           Given BSSID.
* @param[in] fgCheckSsid        Need to check SSID or not. (for multiple SSID with single BSSID cases)
* @param[in] prSsid             Specified SSID
*
* @return   Pointer to BSS Descriptor, if found. NULL, if not found
*/
/*----------------------------------------------------------------------------*/
P_BSS_DESC_T
scanSearchBssDescByBssidAndSsid(IN P_ADAPTER_T prAdapter,
				IN UINT_8 aucBSSID[], IN BOOLEAN fgCheckSsid, IN P_PARAM_SSID_T prSsid)
{
	P_SCAN_INFO_T prScanInfo;
	P_LINK_T prBSSDescList;
	P_BSS_DESC_T prBssDesc;
	P_BSS_DESC_T prDstBssDesc = (P_BSS_DESC_T) NULL;

	ASSERT(prAdapter);
	ASSERT(aucBSSID);

	prScanInfo = &(prAdapter->rWifiVar.rScanInfo);

	prBSSDescList = &prScanInfo->rBSSDescList;

	/* Search BSS Desc from current SCAN result list. */
	LINK_FOR_EACH_ENTRY(prBssDesc, prBSSDescList, rLinkEntry, BSS_DESC_T) {

		if (EQUAL_MAC_ADDR(prBssDesc->aucBSSID, aucBSSID)) {
			if (fgCheckSsid == FALSE || prSsid == NULL)
				return prBssDesc;

			if (EQUAL_SSID(prBssDesc->aucSSID,
				       prBssDesc->ucSSIDLen, prSsid->aucSsid, prSsid->u4SsidLen)) {
				return prBssDesc;
			} else if (prDstBssDesc == NULL && prBssDesc->fgIsHiddenSSID == TRUE) {
				prDstBssDesc = prBssDesc;
			} else {
				/* 20120206 frog: Equal BSSID but not SSID, SSID not hidden,
				 * SSID must be updated. */
				COPY_SSID(prBssDesc->aucSSID,
					  prBssDesc->ucSSIDLen, prSsid->aucSsid, prSsid->u4SsidLen);
				return prBssDesc;
			}
		}
	}

	return prDstBssDesc;

}				/* end of scanSearchBssDescByBssid() */

/*----------------------------------------------------------------------------*/
/*!
* @brief Find the corresponding BSS Descriptor according to given Transmitter Address.
*
* @param[in] prAdapter          Pointer to the Adapter structure.
* @param[in] aucSrcAddr         Given Source Address(TA).
*
* @return   Pointer to BSS Descriptor, if found. NULL, if not found
*/
/*----------------------------------------------------------------------------*/
P_BSS_DESC_T scanSearchBssDescByTA(IN P_ADAPTER_T prAdapter, IN UINT_8 aucSrcAddr[])
{
	return scanSearchBssDescByTAAndSsid(prAdapter, aucSrcAddr, FALSE, NULL);
}

/*----------------------------------------------------------------------------*/
/*!
* @brief Find the corresponding BSS Descriptor according to given Transmitter Address.
*
* @param[in] prAdapter          Pointer to the Adapter structure.
* @param[in] aucSrcAddr         Given Source Address(TA).
* @param[in] fgCheckSsid        Need to check SSID or not. (for multiple SSID with single BSSID cases)
* @param[in] prSsid             Specified SSID
*
* @return   Pointer to BSS Descriptor, if found. NULL, if not found
*/
/*----------------------------------------------------------------------------*/
P_BSS_DESC_T
scanSearchBssDescByTAAndSsid(IN P_ADAPTER_T prAdapter,
			     IN UINT_8 aucSrcAddr[], IN BOOLEAN fgCheckSsid, IN P_PARAM_SSID_T prSsid)
{
	P_SCAN_INFO_T prScanInfo;
	P_LINK_T prBSSDescList;
	P_BSS_DESC_T prBssDesc;
	P_BSS_DESC_T prDstBssDesc = (P_BSS_DESC_T) NULL;

	ASSERT(prAdapter);
	ASSERT(aucSrcAddr);

	prScanInfo = &(prAdapter->rWifiVar.rScanInfo);

	prBSSDescList = &prScanInfo->rBSSDescList;

	/* Search BSS Desc from current SCAN result list. */
	LINK_FOR_EACH_ENTRY(prBssDesc, prBSSDescList, rLinkEntry, BSS_DESC_T) {

		if (EQUAL_MAC_ADDR(prBssDesc->aucSrcAddr, aucSrcAddr)) {
			if (fgCheckSsid == FALSE || prSsid == NULL)
				return prBssDesc;

			if (EQUAL_SSID(prBssDesc->aucSSID,
				       prBssDesc->ucSSIDLen, prSsid->aucSsid, prSsid->u4SsidLen)) {
				return prBssDesc;
			} else if (prDstBssDesc == NULL && prBssDesc->fgIsHiddenSSID == TRUE) {
				prDstBssDesc = prBssDesc;
			}

		}
	}

	return prDstBssDesc;

}				/* end of scanSearchBssDescByTA() */

#if CFG_SUPPORT_HOTSPOT_2_0
/*----------------------------------------------------------------------------*/
/*!
* @brief Find the corresponding BSS Descriptor according to given BSSID
*
* @param[in] prAdapter          Pointer to the Adapter structure.
* @param[in] aucBSSID           Given BSSID.
* @param[in] fgCheckSsid        Need to check SSID or not. (for multiple SSID with single BSSID cases)
* @param[in] prSsid             Specified SSID
*
* @return   Pointer to BSS Descriptor, if found. NULL, if not found
*/
/*----------------------------------------------------------------------------*/
P_BSS_DESC_T scanSearchBssDescByBssidAndLatestUpdateTime(IN P_ADAPTER_T prAdapter, IN UINT_8 aucBSSID[])
{
	P_SCAN_INFO_T prScanInfo;
	P_LINK_T prBSSDescList;
	P_BSS_DESC_T prBssDesc;
	P_BSS_DESC_T prDstBssDesc = (P_BSS_DESC_T) NULL;
	OS_SYSTIME rLatestUpdateTime = 0;

	ASSERT(prAdapter);
	ASSERT(aucBSSID);

	prScanInfo = &(prAdapter->rWifiVar.rScanInfo);

	prBSSDescList = &prScanInfo->rBSSDescList;

	/* Search BSS Desc from current SCAN result list. */
	LINK_FOR_EACH_ENTRY(prBssDesc, prBSSDescList, rLinkEntry, BSS_DESC_T) {

		if (EQUAL_MAC_ADDR(prBssDesc->aucBSSID, aucBSSID)) {
			if (!rLatestUpdateTime || CHECK_FOR_EXPIRATION(prBssDesc->rUpdateTime, rLatestUpdateTime)) {
				prDstBssDesc = prBssDesc;
				COPY_SYSTIME(rLatestUpdateTime, prBssDesc->rUpdateTime);
			}
		}
	}

	return prDstBssDesc;

}				/* end of scanSearchBssDescByBssid() */
#endif

/*----------------------------------------------------------------------------*/
/*!
* @brief Find the corresponding BSS Descriptor according to
*        given eBSSType, BSSID and Transmitter Address
*
* @param[in] prAdapter  Pointer to the Adapter structure.
* @param[in] eBSSType   BSS Type of incoming Beacon/ProbeResp frame.
* @param[in] aucBSSID   Given BSSID of Beacon/ProbeResp frame.
* @param[in] aucSrcAddr Given source address (TA) of Beacon/ProbeResp frame.
*
* @return   Pointer to BSS Descriptor, if found. NULL, if not found
*/
/*----------------------------------------------------------------------------*/
P_BSS_DESC_T
scanSearchExistingBssDesc(IN P_ADAPTER_T prAdapter,
			  IN ENUM_BSS_TYPE_T eBSSType, IN UINT_8 aucBSSID[], IN UINT_8 aucSrcAddr[])
{
	return scanSearchExistingBssDescWithSsid(prAdapter, eBSSType, aucBSSID, aucSrcAddr, FALSE, NULL);
}

/*----------------------------------------------------------------------------*/
/*!
* @brief Find the corresponding BSS Descriptor according to
*        given eBSSType, BSSID and Transmitter Address
*
* @param[in] prAdapter  Pointer to the Adapter structure.
* @param[in] eBSSType   BSS Type of incoming Beacon/ProbeResp frame.
* @param[in] aucBSSID   Given BSSID of Beacon/ProbeResp frame.
* @param[in] aucSrcAddr Given source address (TA) of Beacon/ProbeResp frame.
* @param[in] fgCheckSsid Need to check SSID or not. (for multiple SSID with single BSSID cases)
* @param[in] prSsid     Specified SSID
*
* @return   Pointer to BSS Descriptor, if found. NULL, if not found
*/
/*----------------------------------------------------------------------------*/
P_BSS_DESC_T
scanSearchExistingBssDescWithSsid(IN P_ADAPTER_T prAdapter,
				  IN ENUM_BSS_TYPE_T eBSSType,
				  IN UINT_8 aucBSSID[],
				  IN UINT_8 aucSrcAddr[], IN BOOLEAN fgCheckSsid, IN P_PARAM_SSID_T prSsid)
{
	P_SCAN_INFO_T prScanInfo;
	P_BSS_DESC_T prBssDesc, prIBSSBssDesc;
	P_LINK_T prBSSDescList;
	P_LINK_T prFreeBSSDescList;


	ASSERT(prAdapter);
	ASSERT(aucSrcAddr);

	prScanInfo = &(prAdapter->rWifiVar.rScanInfo);

	switch (eBSSType) {
	case BSS_TYPE_P2P_DEVICE:
		fgCheckSsid = FALSE;
	case BSS_TYPE_INFRASTRUCTURE:
	case BSS_TYPE_BOW_DEVICE:
		{
			prBssDesc = scanSearchBssDescByBssidAndSsid(prAdapter, aucBSSID, fgCheckSsid, prSsid);

			/* if (eBSSType == prBssDesc->eBSSType) */

			return prBssDesc;
		}

	case BSS_TYPE_IBSS:
		{
			prIBSSBssDesc = scanSearchBssDescByBssidAndSsid(prAdapter, aucBSSID, fgCheckSsid, prSsid);
			prBssDesc = scanSearchBssDescByTAAndSsid(prAdapter, aucSrcAddr, fgCheckSsid, prSsid);

			/* NOTE(Kevin):
			 * Rules to maintain the SCAN Result:
			 * For AdHoc -
			 *    CASE I    We have TA1(BSSID1), but it change its BSSID to BSSID2
			 *              -> Update TA1 entry's BSSID.
			 *    CASE II   We have TA1(BSSID1), and get TA1(BSSID1) again
			 *              -> Update TA1 entry's contain.
			 *    CASE III  We have a SCAN result TA1(BSSID1), and TA2(BSSID2). Sooner or
			 *               later, TA2 merge into TA1, we get TA2(BSSID1)
			 *              -> Remove TA2 first and then replace TA1 entry's TA with TA2,
			 *                 Still have only one entry of BSSID.
			 *    CASE IV   We have a SCAN result TA1(BSSID1), and another TA2 also merge into BSSID1.
			 *              -> Replace TA1 entry's TA with TA2, Still have only one entry.
			 *    CASE V    New IBSS
			 *              -> Add this one to SCAN result.
			 */
			if (prBssDesc) {
				if ((!prIBSSBssDesc) ||	/* CASE I */
				    (prBssDesc == prIBSSBssDesc)) {	/* CASE II */

					return prBssDesc;
				}	/* CASE III */

				prBSSDescList = &prScanInfo->rBSSDescList;
				prFreeBSSDescList = &prScanInfo->rFreeBSSDescList;

				/* Remove this BSS Desc from the BSS Desc list */
				LINK_REMOVE_KNOWN_ENTRY(prBSSDescList, prBssDesc);

				/* Return this BSS Desc to the free BSS Desc list. */
				LINK_INSERT_TAIL(prFreeBSSDescList, &prBssDesc->rLinkEntry);

				return prIBSSBssDesc;
			}

			if (prIBSSBssDesc) {	/* CASE IV */

				return prIBSSBssDesc;
			}
			/* CASE V */
			break;	/* Return NULL; */
		}

	default:
		break;
	}

	return (P_BSS_DESC_T) NULL;

}				/* end of scanSearchExistingBssDesc() */

/*----------------------------------------------------------------------------*/
/*!
* @brief Delete BSS Descriptors from current list according to given Remove Policy.
*
* @param[in] u4RemovePolicy     Remove Policy.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID scanRemoveBssDescsByPolicy(IN P_ADAPTER_T prAdapter, IN UINT_32 u4RemovePolicy)
{
	P_CONNECTION_SETTINGS_T prConnSettings;
	P_SCAN_INFO_T prScanInfo;
	P_LINK_T prBSSDescList;
	P_LINK_T prFreeBSSDescList;
	P_BSS_DESC_T prBssDesc;

	ASSERT(prAdapter);

	prConnSettings = &(prAdapter->rWifiVar.rConnSettings);
	prScanInfo = &(prAdapter->rWifiVar.rScanInfo);
	prBSSDescList = &prScanInfo->rBSSDescList;
	prFreeBSSDescList = &prScanInfo->rFreeBSSDescList;

	/* DBGLOG(SCN, TRACE, ("Before Remove - Number Of SCAN Result = %ld\n", */
	/* prBSSDescList->u4NumElem)); */

	if (u4RemovePolicy & SCN_RM_POLICY_TIMEOUT) {
		P_BSS_DESC_T prBSSDescNext;
		OS_SYSTIME rCurrentTime;

		GET_CURRENT_SYSTIME(&rCurrentTime);

		/* Search BSS Desc from current SCAN result list. */
		LINK_FOR_EACH_ENTRY_SAFE(prBssDesc, prBSSDescNext, prBSSDescList, rLinkEntry, BSS_DESC_T) {

			if ((u4RemovePolicy & SCN_RM_POLICY_EXCLUDE_CONNECTED) &&
			    (prBssDesc->fgIsConnected || prBssDesc->fgIsConnecting)) {
				/* Don't remove the one currently we are connected. */
				continue;
			}

			if (CHECK_FOR_TIMEOUT(rCurrentTime, prBssDesc->rUpdateTime,
					      SEC_TO_SYSTIME(SCN_BSS_DESC_REMOVE_TIMEOUT_SEC))) {

				/* DBGLOG(SCN, TRACE, ("Remove TIMEOUT BSS DESC(%#x):
				 * MAC: %pM, Current Time = %08lx, Update Time = %08lx\n", */
				/* prBssDesc, prBssDesc->aucBSSID, rCurrentTime, prBssDesc->rUpdateTime)); */

				/* Remove this BSS Desc from the BSS Desc list */
				LINK_REMOVE_KNOWN_ENTRY(prBSSDescList, prBssDesc);

				/* Return this BSS Desc to the free BSS Desc list. */
				LINK_INSERT_TAIL(prFreeBSSDescList, &prBssDesc->rLinkEntry);
			}
		}
	} else if (u4RemovePolicy & SCN_RM_POLICY_OLDEST_HIDDEN) {
		P_BSS_DESC_T prBssDescOldest = (P_BSS_DESC_T) NULL;

		/* Search BSS Desc from current SCAN result list. */
		LINK_FOR_EACH_ENTRY(prBssDesc, prBSSDescList, rLinkEntry, BSS_DESC_T) {

			if ((u4RemovePolicy & SCN_RM_POLICY_EXCLUDE_CONNECTED) &&
			    (prBssDesc->fgIsConnected || prBssDesc->fgIsConnecting)) {
				/* Don't remove the one currently we are connected. */
				continue;
			}

			if (!prBssDesc->fgIsHiddenSSID)
				continue;

			if (!prBssDescOldest) {	/* 1st element */
				prBssDescOldest = prBssDesc;
				continue;
			}

			if (TIME_BEFORE(prBssDesc->rUpdateTime, prBssDescOldest->rUpdateTime))
				prBssDescOldest = prBssDesc;
		}

		if (prBssDescOldest) {

			/* DBGLOG(SCN, TRACE, ("Remove OLDEST HIDDEN BSS DESC(%#x):
			 * MAC: %pM, Update Time = %08lx\n", */
			/* prBssDescOldest, prBssDescOldest->aucBSSID, prBssDescOldest->rUpdateTime)); */

			/* Remove this BSS Desc from the BSS Desc list */
			LINK_REMOVE_KNOWN_ENTRY(prBSSDescList, prBssDescOldest);

			/* Return this BSS Desc to the free BSS Desc list. */
			LINK_INSERT_TAIL(prFreeBSSDescList, &prBssDescOldest->rLinkEntry);
		}
	} else if (u4RemovePolicy & SCN_RM_POLICY_SMART_WEAKEST) {
		P_BSS_DESC_T prBssDescWeakest = (P_BSS_DESC_T) NULL;
		P_BSS_DESC_T prBssDescWeakestSameSSID = (P_BSS_DESC_T) NULL;
		UINT_32 u4SameSSIDCount = 0;

		/* Search BSS Desc from current SCAN result list. */
		LINK_FOR_EACH_ENTRY(prBssDesc, prBSSDescList, rLinkEntry, BSS_DESC_T) {

			if ((u4RemovePolicy & SCN_RM_POLICY_EXCLUDE_CONNECTED) &&
			    (prBssDesc->fgIsConnected || prBssDesc->fgIsConnecting)) {
				/* Don't remove the one currently we are connected. */
				continue;
			}

			if ((!prBssDesc->fgIsHiddenSSID) &&
			    (EQUAL_SSID(prBssDesc->aucSSID,
					prBssDesc->ucSSIDLen, prConnSettings->aucSSID, prConnSettings->ucSSIDLen))) {

				u4SameSSIDCount++;

				if (!prBssDescWeakestSameSSID)
					prBssDescWeakestSameSSID = prBssDesc;
				else if (prBssDesc->ucRCPI < prBssDescWeakestSameSSID->ucRCPI)
					prBssDescWeakestSameSSID = prBssDesc;
			}

			if (!prBssDescWeakest) {	/* 1st element */
				prBssDescWeakest = prBssDesc;
				continue;
			}

			if (prBssDesc->ucRCPI < prBssDescWeakest->ucRCPI)
				prBssDescWeakest = prBssDesc;

		}

		if ((u4SameSSIDCount >= SCN_BSS_DESC_SAME_SSID_THRESHOLD) && (prBssDescWeakestSameSSID))
			prBssDescWeakest = prBssDescWeakestSameSSID;

		if (prBssDescWeakest) {

			/* DBGLOG(SCN, TRACE, ("Remove WEAKEST BSS DESC(%#x): MAC: %pM, Update Time = %08lx\n", */
			/* prBssDescOldest, prBssDescOldest->aucBSSID, prBssDescOldest->rUpdateTime)); */

			/* Remove this BSS Desc from the BSS Desc list */
			LINK_REMOVE_KNOWN_ENTRY(prBSSDescList, prBssDescWeakest);

			/* Return this BSS Desc to the free BSS Desc list. */
			LINK_INSERT_TAIL(prFreeBSSDescList, &prBssDescWeakest->rLinkEntry);
		}
	} else if (u4RemovePolicy & SCN_RM_POLICY_ENTIRE) {
		P_BSS_DESC_T prBSSDescNext;

		LINK_FOR_EACH_ENTRY_SAFE(prBssDesc, prBSSDescNext, prBSSDescList, rLinkEntry, BSS_DESC_T) {

			if ((u4RemovePolicy & SCN_RM_POLICY_EXCLUDE_CONNECTED) &&
			    (prBssDesc->fgIsConnected || prBssDesc->fgIsConnecting)) {
				/* Don't remove the one currently we are connected. */
				continue;
			}

			/* Remove this BSS Desc from the BSS Desc list */
			LINK_REMOVE_KNOWN_ENTRY(prBSSDescList, prBssDesc);

			/* Return this BSS Desc to the free BSS Desc list. */
			LINK_INSERT_TAIL(prFreeBSSDescList, &prBssDesc->rLinkEntry);
		}

	}

	return;

}				/* end of scanRemoveBssDescsByPolicy() */

/*----------------------------------------------------------------------------*/
/*!
* @brief Delete BSS Descriptors from current list according to given BSSID.
*
* @param[in] prAdapter  Pointer to the Adapter structure.
* @param[in] aucBSSID   Given BSSID.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID scanRemoveBssDescByBssid(IN P_ADAPTER_T prAdapter, IN UINT_8 aucBSSID[])
{
	P_SCAN_INFO_T prScanInfo;
	P_LINK_T prBSSDescList;
	P_LINK_T prFreeBSSDescList;
	P_BSS_DESC_T prBssDesc = (P_BSS_DESC_T) NULL;
	P_BSS_DESC_T prBSSDescNext;

	ASSERT(prAdapter);
	ASSERT(aucBSSID);

	prScanInfo = &(prAdapter->rWifiVar.rScanInfo);
	prBSSDescList = &prScanInfo->rBSSDescList;
	prFreeBSSDescList = &prScanInfo->rFreeBSSDescList;

	/* Check if such BSS Descriptor exists in a valid list */
	LINK_FOR_EACH_ENTRY_SAFE(prBssDesc, prBSSDescNext, prBSSDescList, rLinkEntry, BSS_DESC_T) {

		if (EQUAL_MAC_ADDR(prBssDesc->aucBSSID, aucBSSID)) {

			/* Remove this BSS Desc from the BSS Desc list */
			LINK_REMOVE_KNOWN_ENTRY(prBSSDescList, prBssDesc);

			/* Return this BSS Desc to the free BSS Desc list. */
			LINK_INSERT_TAIL(prFreeBSSDescList, &prBssDesc->rLinkEntry);

			/* BSSID is not unique, so need to traverse whols link-list */
		}
	}

}				/* end of scanRemoveBssDescByBssid() */

/*----------------------------------------------------------------------------*/
/*!
* @brief Delete BSS Descriptors from current list according to given band configuration
*
* @param[in] prAdapter  Pointer to the Adapter structure.
* @param[in] eBand      Given band
* @param[in] eNetTypeIndex  AIS - Remove IBSS/Infrastructure BSS
*                           BOW - Remove BOW BSS
*                           P2P - Remove P2P BSS
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
scanRemoveBssDescByBandAndNetwork(IN P_ADAPTER_T prAdapter,
				  IN ENUM_BAND_T eBand, IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex)
{
	P_SCAN_INFO_T prScanInfo;
	P_LINK_T prBSSDescList;
	P_LINK_T prFreeBSSDescList;
	P_BSS_DESC_T prBssDesc = (P_BSS_DESC_T) NULL;
	P_BSS_DESC_T prBSSDescNext;
	BOOLEAN fgToRemove;

	ASSERT(prAdapter);
	ASSERT(eBand <= BAND_NUM);
	ASSERT(eNetTypeIndex <= NETWORK_TYPE_INDEX_NUM);

	prScanInfo = &(prAdapter->rWifiVar.rScanInfo);
	prBSSDescList = &prScanInfo->rBSSDescList;
	prFreeBSSDescList = &prScanInfo->rFreeBSSDescList;

	if (eBand == BAND_NULL)
		return;		/* no need to do anything, keep all scan result */

	/* Check if such BSS Descriptor exists in a valid list */
	LINK_FOR_EACH_ENTRY_SAFE(prBssDesc, prBSSDescNext, prBSSDescList, rLinkEntry, BSS_DESC_T) {
		fgToRemove = FALSE;

		if (prBssDesc->eBand == eBand) {
			switch (eNetTypeIndex) {
			case NETWORK_TYPE_AIS_INDEX:
				if ((prBssDesc->eBSSType == BSS_TYPE_INFRASTRUCTURE)
				    || (prBssDesc->eBSSType == BSS_TYPE_IBSS)) {
					fgToRemove = TRUE;
				}
				break;

			case NETWORK_TYPE_P2P_INDEX:
				if (prBssDesc->eBSSType == BSS_TYPE_P2P_DEVICE)
					fgToRemove = TRUE;
				break;

			case NETWORK_TYPE_BOW_INDEX:
				if (prBssDesc->eBSSType == BSS_TYPE_BOW_DEVICE)
					fgToRemove = TRUE;
				break;

			default:
				ASSERT(0);
				break;
			}
		}

		if (fgToRemove == TRUE) {
			/* Remove this BSS Desc from the BSS Desc list */
			LINK_REMOVE_KNOWN_ENTRY(prBSSDescList, prBssDesc);

			/* Return this BSS Desc to the free BSS Desc list. */
			LINK_INSERT_TAIL(prFreeBSSDescList, &prBssDesc->rLinkEntry);
		}
	}

}				/* end of scanRemoveBssDescByBand() */

/*----------------------------------------------------------------------------*/
/*!
* @brief Clear the CONNECTION FLAG of a specified BSS Descriptor.
*
* @param[in] aucBSSID   Given BSSID.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID scanRemoveConnFlagOfBssDescByBssid(IN P_ADAPTER_T prAdapter, IN UINT_8 aucBSSID[])
{
	P_SCAN_INFO_T prScanInfo;
	P_LINK_T prBSSDescList;
	P_BSS_DESC_T prBssDesc = (P_BSS_DESC_T) NULL;

	ASSERT(prAdapter);
	ASSERT(aucBSSID);

	prScanInfo = &(prAdapter->rWifiVar.rScanInfo);
	prBSSDescList = &prScanInfo->rBSSDescList;

	/* Search BSS Desc from current SCAN result list. */
	LINK_FOR_EACH_ENTRY(prBssDesc, prBSSDescList, rLinkEntry, BSS_DESC_T) {

		if (EQUAL_MAC_ADDR(prBssDesc->aucBSSID, aucBSSID)) {
			prBssDesc->fgIsConnected = FALSE;
			prBssDesc->fgIsConnecting = FALSE;

			/* BSSID is not unique, so need to traverse whols link-list */
		}
	}

	return;

}				/* end of scanRemoveConnectionFlagOfBssDescByBssid() */

/*----------------------------------------------------------------------------*/
/*!
* @brief Allocate new BSS_DESC_T
*
* @param[in] prAdapter          Pointer to the Adapter structure.
*
* @return   Pointer to BSS Descriptor, if has free space. NULL, if has no space.
*/
/*----------------------------------------------------------------------------*/
P_BSS_DESC_T scanAllocateBssDesc(IN P_ADAPTER_T prAdapter)
{
	P_SCAN_INFO_T prScanInfo;
	P_LINK_T prFreeBSSDescList;
	P_BSS_DESC_T prBssDesc;

	ASSERT(prAdapter);
	prScanInfo = &(prAdapter->rWifiVar.rScanInfo);

	prFreeBSSDescList = &prScanInfo->rFreeBSSDescList;

	LINK_REMOVE_HEAD(prFreeBSSDescList, prBssDesc, P_BSS_DESC_T);

	if (prBssDesc) {
		P_LINK_T prBSSDescList;

		kalMemZero(prBssDesc, sizeof(BSS_DESC_T));

#if CFG_ENABLE_WIFI_DIRECT
		LINK_INITIALIZE(&(prBssDesc->rP2pDeviceList));
		prBssDesc->fgIsP2PPresent = FALSE;
#endif /* CFG_ENABLE_WIFI_DIRECT */

		prBSSDescList = &prScanInfo->rBSSDescList;

		/* NOTE(Kevin): In current design, this new empty BSS_DESC_T will be
		 * inserted to BSSDescList immediately.
		 */
		LINK_INSERT_TAIL(prBSSDescList, &prBssDesc->rLinkEntry);
	}

	return prBssDesc;

}				/* end of scanAllocateBssDesc() */

/*----------------------------------------------------------------------------*/
/*!
* @brief This API parses Beacon/ProbeResp frame and insert extracted BSS_DESC_T
*        with IEs into prAdapter->rWifiVar.rScanInfo.aucScanBuffer
*
* @param[in] prAdapter      Pointer to the Adapter structure.
* @param[in] prSwRfb        Pointer to the receiving frame buffer.
*
* @return   Pointer to BSS Descriptor
*           NULL if the Beacon/ProbeResp frame is invalid
*/
/*----------------------------------------------------------------------------*/
P_BSS_DESC_T scanAddToBssDesc(IN P_ADAPTER_T prAdapter, IN P_SW_RFB_T prSwRfb)
{
	P_BSS_DESC_T prBssDesc = NULL;
	UINT_16 u2CapInfo;
	ENUM_BSS_TYPE_T eBSSType = BSS_TYPE_INFRASTRUCTURE;

	PUINT_8 pucIE;
	UINT_16 u2IELength;
	UINT_16 u2Offset = 0;

	P_WLAN_BEACON_FRAME_T prWlanBeaconFrame = (P_WLAN_BEACON_FRAME_T) NULL;
	P_IE_SSID_T prIeSsid = (P_IE_SSID_T) NULL;
	P_IE_SUPPORTED_RATE_T prIeSupportedRate = (P_IE_SUPPORTED_RATE_T) NULL;
	P_IE_EXT_SUPPORTED_RATE_T prIeExtSupportedRate = (P_IE_EXT_SUPPORTED_RATE_T) NULL;
	P_HIF_RX_HEADER_T prHifRxHdr;
	UINT_8 ucHwChannelNum = 0;
	UINT_8 ucIeDsChannelNum = 0;
	UINT_8 ucIeHtChannelNum = 0;
	BOOLEAN fgIsValidSsid = FALSE, fgEscape = FALSE;
	PARAM_SSID_T rSsid;
	UINT_64 u8Timestamp;
	BOOLEAN fgIsNewBssDesc = FALSE;

	UINT_32 i;
	UINT_8 ucSSIDChar;

	UINT_8 ucOuiType;
	UINT_16 u2SubTypeVersion;

	ASSERT(prAdapter);
	ASSERT(prSwRfb);

	prWlanBeaconFrame = (P_WLAN_BEACON_FRAME_T) prSwRfb->pvHeader;

	WLAN_GET_FIELD_16(&prWlanBeaconFrame->u2CapInfo, &u2CapInfo);
	WLAN_GET_FIELD_64(&prWlanBeaconFrame->au4Timestamp[0], &u8Timestamp);

	/* decide BSS type */
	switch (u2CapInfo & CAP_INFO_BSS_TYPE) {
	case CAP_INFO_ESS:
		/* It can also be Group Owner of P2P Group. */
		eBSSType = BSS_TYPE_INFRASTRUCTURE;
		break;

	case CAP_INFO_IBSS:
		eBSSType = BSS_TYPE_IBSS;
		break;
	case 0:
		/* The P2P Device shall set the ESS bit of the Capabilities field
		 * in the Probe Response fame to 0 and IBSS bit to 0. (3.1.2.1.1) */
		eBSSType = BSS_TYPE_P2P_DEVICE;
		break;

#if CFG_ENABLE_BT_OVER_WIFI
		/* @TODO: add rule to identify BOW beacons */
#endif

	default:
		 DBGLOG(SCN, ERROR, "wrong bss type %d\n", (INT_32)(u2CapInfo & CAP_INFO_BSS_TYPE));
		return NULL;
	}

	/* 4 <1.1> Pre-parse SSID IE */
	pucIE = prWlanBeaconFrame->aucInfoElem;
	u2IELength = (prSwRfb->u2PacketLen - prSwRfb->u2HeaderLen) -
	    (UINT_16) OFFSET_OF(WLAN_BEACON_FRAME_BODY_T, aucInfoElem[0]);

	if (u2IELength > CFG_IE_BUFFER_SIZE)
		u2IELength = CFG_IE_BUFFER_SIZE;
	kalMemZero(&rSsid, sizeof(rSsid));
	IE_FOR_EACH(pucIE, u2IELength, u2Offset) {
		switch (IE_ID(pucIE)) {
		case ELEM_ID_SSID:
			if (IE_LEN(pucIE) <= ELEM_MAX_LEN_SSID) {
				ucSSIDChar = '\0';

				/* D-Link DWL-900AP+ */
				if (IE_LEN(pucIE) == 0)
					fgIsValidSsid = FALSE;
				/* Cisco AP1230A - (IE_LEN(pucIE) == 1) && (SSID_IE(pucIE)->aucSSID[0] == '\0') */
				/* Linksys WRK54G/WL520g - (IE_LEN(pucIE) == n) &&
				 * (SSID_IE(pucIE)->aucSSID[0~(n-1)] == '\0') */
				else {
					for (i = 0; i < IE_LEN(pucIE); i++)
						ucSSIDChar |= SSID_IE(pucIE)->aucSSID[i];

					if (ucSSIDChar)
						fgIsValidSsid = TRUE;
				}

				/* Update SSID to BSS Descriptor only if SSID is not hidden. */
				if (fgIsValidSsid == TRUE) {
					COPY_SSID(rSsid.aucSsid,
						  rSsid.u4SsidLen, SSID_IE(pucIE)->aucSSID, SSID_IE(pucIE)->ucLength);
				}
			}
			fgEscape = TRUE;
			break;
		default:
			break;
		}

		if (fgEscape == TRUE)
			break;
	}
	if (fgIsValidSsid)
		DBGLOG(SCN, EVENT, "%s %pM channel %d\n", rSsid.aucSsid, prWlanBeaconFrame->aucBSSID,
				HIF_RX_HDR_GET_CHNL_NUM(prSwRfb->prHifRxHdr));
	else
		DBGLOG(SCN, EVENT, "hidden ssid, %pM channel %d\n", prWlanBeaconFrame->aucBSSID,
				HIF_RX_HDR_GET_CHNL_NUM(prSwRfb->prHifRxHdr));
	/* 4 <1.2> Replace existing BSS_DESC_T or allocate a new one */
	prBssDesc = scanSearchExistingBssDescWithSsid(prAdapter,
						      eBSSType,
						      (PUINT_8) prWlanBeaconFrame->aucBSSID,
						      (PUINT_8) prWlanBeaconFrame->aucSrcAddr,
						      fgIsValidSsid, fgIsValidSsid == TRUE ? &rSsid : NULL);

	if (prBssDesc == (P_BSS_DESC_T) NULL) {
		fgIsNewBssDesc = TRUE;

		do {
			/* 4 <1.2.1> First trial of allocation */
			prBssDesc = scanAllocateBssDesc(prAdapter);
			if (prBssDesc)
				break;
			/* 4 <1.2.2> Hidden is useless, remove the oldest hidden ssid. (for passive scan) */
			scanRemoveBssDescsByPolicy(prAdapter,
						   (SCN_RM_POLICY_EXCLUDE_CONNECTED | SCN_RM_POLICY_OLDEST_HIDDEN));

			/* 4 <1.2.3> Second tail of allocation */
			prBssDesc = scanAllocateBssDesc(prAdapter);
			if (prBssDesc)
				break;
			/* 4 <1.2.4> Remove the weakest one */
			/* If there are more than half of BSS which has the same ssid as connection
			 * setting, remove the weakest one from them.
			 * Else remove the weakest one.
			 */
			scanRemoveBssDescsByPolicy(prAdapter,
						   (SCN_RM_POLICY_EXCLUDE_CONNECTED | SCN_RM_POLICY_SMART_WEAKEST));

			/* 4 <1.2.5> reallocation */
			prBssDesc = scanAllocateBssDesc(prAdapter);
			if (prBssDesc)
				break;
			/* 4 <1.2.6> no space, should not happen */
			DBGLOG(SCN, ERROR, "no bss desc available after remove policy\n");
			return NULL;

		} while (FALSE);

	} else {
		OS_SYSTIME rCurrentTime;

		/* WCXRP00000091 */
		/* if the received strength is much weaker than the original one, */
		/* ignore it due to it might be received on the folding frequency */

		GET_CURRENT_SYSTIME(&rCurrentTime);

		if (prBssDesc->eBSSType != eBSSType) {
			prBssDesc->eBSSType = eBSSType;
		} else if (HIF_RX_HDR_GET_CHNL_NUM(prSwRfb->prHifRxHdr) != prBssDesc->ucChannelNum &&
			   prBssDesc->ucRCPI > prSwRfb->prHifRxHdr->ucRcpi) {
			/* for signal strength is too much weaker and previous beacon is not stale */
			if ((prBssDesc->ucRCPI - prSwRfb->prHifRxHdr->ucRcpi) >= REPLICATED_BEACON_STRENGTH_THRESHOLD &&
				(rCurrentTime - prBssDesc->rUpdateTime) <= REPLICATED_BEACON_FRESH_PERIOD) {
				DBGLOG(SCN, EVENT, "rssi is too much weaker and previous one is fresh\n");
				return prBssDesc;
			}
			/* for received beacons too close in time domain */
			else if (rCurrentTime - prBssDesc->rUpdateTime <= REPLICATED_BEACON_TIME_THRESHOLD) {
				DBGLOG(SCN, EVENT, "receive beacon/probe reponses too close\n");
				return prBssDesc;
			}
		}

		/* if Timestamp has been reset, re-generate BSS DESC 'cause AP should have reset itself */
		if (prBssDesc->eBSSType == BSS_TYPE_INFRASTRUCTURE && u8Timestamp < prBssDesc->u8TimeStamp.QuadPart) {
			BOOLEAN fgIsConnected, fgIsConnecting;

			/* set flag for indicating this is a new BSS-DESC */
			fgIsNewBssDesc = TRUE;

			/* backup 2 flags for APs which reset timestamp unexpectedly */
			fgIsConnected = prBssDesc->fgIsConnected;
			fgIsConnecting = prBssDesc->fgIsConnecting;
			scanRemoveBssDescByBssid(prAdapter, prBssDesc->aucBSSID);

			prBssDesc = scanAllocateBssDesc(prAdapter);
			if (!prBssDesc)
				return NULL;

			/* restore */
			prBssDesc->fgIsConnected = fgIsConnected;
			prBssDesc->fgIsConnecting = fgIsConnecting;
		}
	}
#if 1

	prBssDesc->u2RawLength = prSwRfb->u2PacketLen;
	if (prBssDesc->u2RawLength > CFG_RAW_BUFFER_SIZE)
		prBssDesc->u2RawLength = CFG_RAW_BUFFER_SIZE;
	kalMemCopy(prBssDesc->aucRawBuf, prWlanBeaconFrame, prBssDesc->u2RawLength);
#endif

	/* NOTE: Keep consistency of Scan Record during JOIN process */
	if ((fgIsNewBssDesc == FALSE) && prBssDesc->fgIsConnecting) {
		DBGLOG(SCN, INFO, "we're connecting this BSS(%pM) now, don't update it\n",
				prBssDesc->aucBSSID);
		return prBssDesc;
	}
	/* 4 <2> Get information from Fixed Fields */
	prBssDesc->eBSSType = eBSSType;	/* Update the latest BSS type information. */

	COPY_MAC_ADDR(prBssDesc->aucSrcAddr, prWlanBeaconFrame->aucSrcAddr);

	COPY_MAC_ADDR(prBssDesc->aucBSSID, prWlanBeaconFrame->aucBSSID);

	prBssDesc->u8TimeStamp.QuadPart = u8Timestamp;

	WLAN_GET_FIELD_16(&prWlanBeaconFrame->u2BeaconInterval, &prBssDesc->u2BeaconInterval);

	prBssDesc->u2CapInfo = u2CapInfo;

	/* 4 <2.1> Retrieve IEs for later parsing */
	u2IELength = (prSwRfb->u2PacketLen - prSwRfb->u2HeaderLen) -
	    (UINT_16) OFFSET_OF(WLAN_BEACON_FRAME_BODY_T, aucInfoElem[0]);

	if (u2IELength > CFG_IE_BUFFER_SIZE) {
		u2IELength = CFG_IE_BUFFER_SIZE;
		prBssDesc->fgIsIEOverflow = TRUE;
	} else {
		prBssDesc->fgIsIEOverflow = FALSE;
	}
	prBssDesc->u2IELength = u2IELength;

	kalMemCopy(prBssDesc->aucIEBuf, prWlanBeaconFrame->aucInfoElem, u2IELength);

	/* 4 <2.2> reset prBssDesc variables in case that AP has been reconfigured */
	prBssDesc->fgIsERPPresent = FALSE;
	prBssDesc->fgIsHTPresent = FALSE;
	prBssDesc->eSco = CHNL_EXT_SCN;
	prBssDesc->fgIEWAPI = FALSE;
#if CFG_RSN_MIGRATION
	prBssDesc->fgIERSN = FALSE;
#endif
#if CFG_PRIVACY_MIGRATION
	prBssDesc->fgIEWPA = FALSE;
#endif

	/* 4 <3.1> Full IE parsing on SW_RFB_T */
	pucIE = prWlanBeaconFrame->aucInfoElem;

	IE_FOR_EACH(pucIE, u2IELength, u2Offset) {

		switch (IE_ID(pucIE)) {
		case ELEM_ID_SSID:
			if ((!prIeSsid) &&	/* NOTE(Kevin): for Atheros IOT #1 */
			    (IE_LEN(pucIE) <= ELEM_MAX_LEN_SSID)) {
				BOOLEAN fgIsHiddenSSID = FALSE;

				ucSSIDChar = '\0';

				prIeSsid = (P_IE_SSID_T) pucIE;

				/* D-Link DWL-900AP+ */
				if (IE_LEN(pucIE) == 0)
					fgIsHiddenSSID = TRUE;
				/* Cisco AP1230A - (IE_LEN(pucIE) == 1) && (SSID_IE(pucIE)->aucSSID[0] == '\0') */
				/* Linksys WRK54G/WL520g - (IE_LEN(pucIE) == n) &&
				 * (SSID_IE(pucIE)->aucSSID[0~(n-1)] == '\0') */
				else {
					for (i = 0; i < IE_LEN(pucIE); i++)
						ucSSIDChar |= SSID_IE(pucIE)->aucSSID[i];

					if (!ucSSIDChar)
						fgIsHiddenSSID = TRUE;
				}

				/* Update SSID to BSS Descriptor only if SSID is not hidden. */
				if (!fgIsHiddenSSID) {
					COPY_SSID(prBssDesc->aucSSID,
						  prBssDesc->ucSSIDLen,
						  SSID_IE(pucIE)->aucSSID, SSID_IE(pucIE)->ucLength);
				}
#if 0
				/*
				   After we connect to a hidden SSID, prBssDesc->aucSSID[] will
				   not be empty and prBssDesc->ucSSIDLen will not be 0,
				   so maybe we need to empty prBssDesc->aucSSID[] and set
				   prBssDesc->ucSSIDLen to 0 in prBssDesc to avoid that
				   UI still displays hidden SSID AP in scan list after
				   we disconnect the hidden SSID AP.
				 */
				else {
					prBssDesc->aucSSID[0] = '\0';
					prBssDesc->ucSSIDLen = 0;
				}
#endif

			}
			break;

		case ELEM_ID_SUP_RATES:
			/* NOTE(Kevin): Buffalo WHR-G54S's supported rate set IE exceed 8.
			 * IE_LEN(pucIE) == 12, "1(B), 2(B), 5.5(B), 6(B), 9(B), 11(B),
			 * 12(B), 18(B), 24(B), 36(B), 48(B), 54(B)"
			 */
			/* TP-LINK will set extra and incorrect ie with ELEM_ID_SUP_RATES */
			if ((!prIeSupportedRate) && (IE_LEN(pucIE) <= RATE_NUM))
				prIeSupportedRate = SUP_RATES_IE(pucIE);
			break;

		case ELEM_ID_DS_PARAM_SET:
			if (IE_LEN(pucIE) == ELEM_MAX_LEN_DS_PARAMETER_SET)
				ucIeDsChannelNum = DS_PARAM_IE(pucIE)->ucCurrChnl;
			break;

		case ELEM_ID_TIM:
			if (IE_LEN(pucIE) <= ELEM_MAX_LEN_TIM)
				prBssDesc->ucDTIMPeriod = TIM_IE(pucIE)->ucDTIMPeriod;
			break;

		case ELEM_ID_IBSS_PARAM_SET:
			if (IE_LEN(pucIE) == ELEM_MAX_LEN_IBSS_PARAMETER_SET)
				prBssDesc->u2ATIMWindow = IBSS_PARAM_IE(pucIE)->u2ATIMWindow;
			break;

#if 0				/* CFG_SUPPORT_802_11D */
		case ELEM_ID_COUNTRY_INFO:
			prBssDesc->prIECountry = (P_IE_COUNTRY_T) pucIE;
			break;
#endif

		case ELEM_ID_ERP_INFO:
			if (IE_LEN(pucIE) == ELEM_MAX_LEN_ERP)
				prBssDesc->fgIsERPPresent = TRUE;
			break;

		case ELEM_ID_EXTENDED_SUP_RATES:
			if (!prIeExtSupportedRate)
				prIeExtSupportedRate = EXT_SUP_RATES_IE(pucIE);
			break;

#if CFG_RSN_MIGRATION
		case ELEM_ID_RSN:
			if (rsnParseRsnIE(prAdapter, RSN_IE(pucIE), &prBssDesc->rRSNInfo)) {
				prBssDesc->fgIERSN = TRUE;
				prBssDesc->u2RsnCap = prBssDesc->rRSNInfo.u2RsnCap;
				if (prAdapter->rWifiVar.rConnSettings.eAuthMode == AUTH_MODE_WPA2)
					rsnCheckPmkidCache(prAdapter, prBssDesc);
			}
			break;
#endif

		case ELEM_ID_HT_CAP:
			prBssDesc->fgIsHTPresent = TRUE;
			break;

		case ELEM_ID_HT_OP:
			if (IE_LEN(pucIE) != (sizeof(IE_HT_OP_T) - 2))
				break;

			if ((((P_IE_HT_OP_T) pucIE)->ucInfo1 & HT_OP_INFO1_SCO) != CHNL_EXT_RES) {
				prBssDesc->eSco = (ENUM_CHNL_EXT_T)
				    (((P_IE_HT_OP_T) pucIE)->ucInfo1 & HT_OP_INFO1_SCO);
			}
			ucIeHtChannelNum = ((P_IE_HT_OP_T) pucIE)->ucPrimaryChannel;

			break;

#if CFG_SUPPORT_WAPI
		case ELEM_ID_WAPI:
			if (wapiParseWapiIE(WAPI_IE(pucIE), &prBssDesc->rIEWAPI))
				prBssDesc->fgIEWAPI = TRUE;
			break;
#endif

		case ELEM_ID_VENDOR:	/* ELEM_ID_P2P, ELEM_ID_WMM */
#if CFG_PRIVACY_MIGRATION
			if (rsnParseCheckForWFAInfoElem(prAdapter, pucIE, &ucOuiType, &u2SubTypeVersion)) {
				if ((ucOuiType == VENDOR_OUI_TYPE_WPA) && (u2SubTypeVersion == VERSION_WPA)) {

					if (rsnParseWpaIE(prAdapter, WPA_IE(pucIE), &prBssDesc->rWPAInfo))
						prBssDesc->fgIEWPA = TRUE;
				}
			}
#endif

#if CFG_ENABLE_WIFI_DIRECT
			if (prAdapter->fgIsP2PRegistered) {
				if (p2pFuncParseCheckForP2PInfoElem(prAdapter, pucIE, &ucOuiType)) {
					if (ucOuiType == VENDOR_OUI_TYPE_P2P)
						prBssDesc->fgIsP2PPresent = TRUE;
				}
			}
#endif /* CFG_ENABLE_WIFI_DIRECT */
			break;

			/* no default */
		}
	}

	/* 4 <3.2> Save information from IEs - SSID */
	/* Update Flag of Hidden SSID for used in SEARCH STATE. */

	/* NOTE(Kevin): in current driver, the ucSSIDLen == 0 represent
	 * all cases of hidden SSID.
	 * If the fgIsHiddenSSID == TRUE, it means we didn't get the ProbeResp with
	 * valid SSID.
	 */
	if (prBssDesc->ucSSIDLen == 0)
		prBssDesc->fgIsHiddenSSID = TRUE;
	else
		prBssDesc->fgIsHiddenSSID = FALSE;

	/* 4 <3.3> Check rate information in related IEs. */
	if (prIeSupportedRate || prIeExtSupportedRate) {
		rateGetRateSetFromIEs(prIeSupportedRate,
				      prIeExtSupportedRate,
				      &prBssDesc->u2OperationalRateSet,
				      &prBssDesc->u2BSSBasicRateSet, &prBssDesc->fgIsUnknownBssBasicRate);
	}
	/* 4 <4> Update information from HIF RX Header */
	{
		prHifRxHdr = prSwRfb->prHifRxHdr;

		ASSERT(prHifRxHdr);

		/* 4 <4.1> Get TSF comparison result */
		prBssDesc->fgIsLargerTSF = HIF_RX_HDR_GET_TCL_FLAG(prHifRxHdr);

		/* 4 <4.2> Get Band information */
		prBssDesc->eBand = HIF_RX_HDR_GET_RF_BAND(prHifRxHdr);

		/* 4 <4.2> Get channel and RCPI information */
		ucHwChannelNum = HIF_RX_HDR_GET_CHNL_NUM(prHifRxHdr);

		if (BAND_2G4 == prBssDesc->eBand) {

			/* Update RCPI if in right channel */
			if (ucIeDsChannelNum >= 1 && ucIeDsChannelNum <= 14) {

				/* Receive Beacon/ProbeResp frame from adjacent channel. */
				if ((ucIeDsChannelNum == ucHwChannelNum) || (prHifRxHdr->ucRcpi > prBssDesc->ucRCPI))
					prBssDesc->ucRCPI = prHifRxHdr->ucRcpi;
				/* trust channel information brought by IE */
				prBssDesc->ucChannelNum = ucIeDsChannelNum;
			} else if (ucIeHtChannelNum >= 1 && ucIeHtChannelNum <= 14) {
				/* Receive Beacon/ProbeResp frame from adjacent channel. */
				if ((ucIeHtChannelNum == ucHwChannelNum) || (prHifRxHdr->ucRcpi > prBssDesc->ucRCPI))
					prBssDesc->ucRCPI = prHifRxHdr->ucRcpi;
				/* trust channel information brought by IE */
				prBssDesc->ucChannelNum = ucIeHtChannelNum;
			} else {
				prBssDesc->ucRCPI = prHifRxHdr->ucRcpi;

				prBssDesc->ucChannelNum = ucHwChannelNum;
			}
		}
		/* 5G Band */
		else {
			if (ucIeHtChannelNum >= 1 && ucIeHtChannelNum < 200) {
				/* Receive Beacon/ProbeResp frame from adjacent channel. */
				if ((ucIeHtChannelNum == ucHwChannelNum) || (prHifRxHdr->ucRcpi > prBssDesc->ucRCPI))
					prBssDesc->ucRCPI = prHifRxHdr->ucRcpi;
				/* trust channel information brought by IE */
				prBssDesc->ucChannelNum = ucIeHtChannelNum;
			} else {
				/* Always update RCPI */
				prBssDesc->ucRCPI = prHifRxHdr->ucRcpi;

				prBssDesc->ucChannelNum = ucHwChannelNum;
			}
		}
	}

	/* 4 <5> PHY type setting */
	prBssDesc->ucPhyTypeSet = 0;

	if (BAND_2G4 == prBssDesc->eBand) {
		/* check if support 11n */
		if (prBssDesc->fgIsHTPresent)
			prBssDesc->ucPhyTypeSet |= PHY_TYPE_BIT_HT;

		/* if not 11n only */
		if (!(prBssDesc->u2BSSBasicRateSet & RATE_SET_BIT_HT_PHY)) {
			/* check if support 11g */
			if ((prBssDesc->u2OperationalRateSet & RATE_SET_OFDM) || prBssDesc->fgIsERPPresent)
				prBssDesc->ucPhyTypeSet |= PHY_TYPE_BIT_ERP;

			/* if not 11g only */
			if (!(prBssDesc->u2BSSBasicRateSet & RATE_SET_OFDM)) {
				/* check if support 11b */
				if ((prBssDesc->u2OperationalRateSet & RATE_SET_HR_DSSS))
					prBssDesc->ucPhyTypeSet |= PHY_TYPE_BIT_HR_DSSS;
			}
		}
	} else {		/* (BAND_5G == prBssDesc->eBande) */
		/* check if support 11n */
		if (prBssDesc->fgIsHTPresent)
			prBssDesc->ucPhyTypeSet |= PHY_TYPE_BIT_HT;

		/* if not 11n only */
		if (!(prBssDesc->u2BSSBasicRateSet & RATE_SET_BIT_HT_PHY)) {
			/* Support 11a definitely */
			prBssDesc->ucPhyTypeSet |= PHY_TYPE_BIT_OFDM;

			ASSERT(!(prBssDesc->u2OperationalRateSet & RATE_SET_HR_DSSS));
		}
	}

	/* 4 <6> Update BSS_DESC_T's Last Update TimeStamp. */
	GET_CURRENT_SYSTIME(&prBssDesc->rUpdateTime);

	return prBssDesc;
}

/*----------------------------------------------------------------------------*/
/*!
* @brief Convert the Beacon or ProbeResp Frame in SW_RFB_T to scan result for query
*
* @param[in] prSwRfb            Pointer to the receiving SW_RFB_T structure.
*
* @retval WLAN_STATUS_SUCCESS   It is a valid Scan Result and been sent to the host.
* @retval WLAN_STATUS_FAILURE   It is not a valid Scan Result.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS scanAddScanResult(IN P_ADAPTER_T prAdapter, IN P_BSS_DESC_T prBssDesc, IN P_SW_RFB_T prSwRfb)
{
	P_SCAN_INFO_T prScanInfo;
	UINT_8 aucRatesEx[PARAM_MAX_LEN_RATES_EX];
	P_WLAN_BEACON_FRAME_T prWlanBeaconFrame;
	PARAM_MAC_ADDRESS rMacAddr;
	PARAM_SSID_T rSsid;
	ENUM_PARAM_NETWORK_TYPE_T eNetworkType;
	PARAM_802_11_CONFIG_T rConfiguration;
	ENUM_PARAM_OP_MODE_T eOpMode;
	UINT_8 ucRateLen = 0;
	UINT_32 i;

	ASSERT(prAdapter);
	ASSERT(prSwRfb);

	prScanInfo = &(prAdapter->rWifiVar.rScanInfo);

	if (prBssDesc->eBand == BAND_2G4) {
		if ((prBssDesc->u2OperationalRateSet & RATE_SET_OFDM)
		    || prBssDesc->fgIsERPPresent) {
			eNetworkType = PARAM_NETWORK_TYPE_OFDM24;
		} else {
			eNetworkType = PARAM_NETWORK_TYPE_DS;
		}
	} else {
		ASSERT(prBssDesc->eBand == BAND_5G);
		eNetworkType = PARAM_NETWORK_TYPE_OFDM5;
	}

	if (prBssDesc->eBSSType == BSS_TYPE_P2P_DEVICE) {
		/* NOTE(Kevin): Not supported by WZC(TBD) */
		return WLAN_STATUS_FAILURE;
	}

	prWlanBeaconFrame = (P_WLAN_BEACON_FRAME_T) prSwRfb->pvHeader;
	COPY_MAC_ADDR(rMacAddr, prWlanBeaconFrame->aucBSSID);
	COPY_SSID(rSsid.aucSsid, rSsid.u4SsidLen, prBssDesc->aucSSID, prBssDesc->ucSSIDLen);

	rConfiguration.u4Length = sizeof(PARAM_802_11_CONFIG_T);
	rConfiguration.u4BeaconPeriod = (UINT_32) prWlanBeaconFrame->u2BeaconInterval;
	rConfiguration.u4ATIMWindow = prBssDesc->u2ATIMWindow;
	rConfiguration.u4DSConfig = nicChannelNum2Freq(prBssDesc->ucChannelNum);
	rConfiguration.rFHConfig.u4Length = sizeof(PARAM_802_11_CONFIG_FH_T);

	rateGetDataRatesFromRateSet(prBssDesc->u2OperationalRateSet, 0, aucRatesEx, &ucRateLen);

	/* NOTE(Kevin): Set unused entries, if any, at the end of the array to 0.
	 * from OID_802_11_BSSID_LIST
	 */
	for (i = ucRateLen; i < sizeof(aucRatesEx) / sizeof(aucRatesEx[0]); i++)
		aucRatesEx[i] = 0;

	switch (prBssDesc->eBSSType) {
	case BSS_TYPE_IBSS:
		eOpMode = NET_TYPE_IBSS;
		break;

	case BSS_TYPE_INFRASTRUCTURE:
	case BSS_TYPE_P2P_DEVICE:
	case BSS_TYPE_BOW_DEVICE:
	default:
		eOpMode = NET_TYPE_INFRA;
		break;
	}

	DBGLOG(SCN, TRACE, "ind %s %d\n", prBssDesc->aucSSID, prBssDesc->ucChannelNum);

#if (CFG_SUPPORT_TDLS == 1)
	{
		if (flgTdlsTestExtCapElm == TRUE) {
			/* only for RALINK AP */
			UINT8 *pucElm = (UINT8 *) (prSwRfb->pvHeader + prSwRfb->u2PacketLen);

			kalMemCopy(pucElm - 9, aucTdlsTestExtCapElm, 7);
			prSwRfb->u2PacketLen -= 2;
/* prSwRfb->u2PacketLen += 7; */

			DBGLOG(TDLS, INFO,
			       "<tdls> %s: append ext cap element to %pM\n",
				__func__, prBssDesc->aucBSSID);
		}
	}
#endif /* CFG_SUPPORT_TDLS */

	if (prAdapter->rWifiVar.rScanInfo.fgNloScanning &&
				test_bit(SUSPEND_FLAG_CLEAR_WHEN_RESUME, &prAdapter->ulSuspendFlag)) {
			UINT_8 i = 0;
			P_BSS_DESC_T *pprPendBssDesc = &prScanInfo->rNloParam.aprPendingBssDescToInd[0];

			for (; i < SCN_SSID_MATCH_MAX_NUM; i++) {
				if (pprPendBssDesc[i])
					continue;
				DBGLOG(SCN, INFO,
					"indicate bss[%pM] before wiphy resume, need to indicate again after wiphy resume\n",
					prBssDesc->aucBSSID);
				pprPendBssDesc[i] = prBssDesc;
				break;
			}
	}

	kalIndicateBssInfo(prAdapter->prGlueInfo,
			   (PUINT_8) prSwRfb->pvHeader,
			   prSwRfb->u2PacketLen, prBssDesc->ucChannelNum, RCPI_TO_dBm(prBssDesc->ucRCPI));

	nicAddScanResult(prAdapter,
			 rMacAddr,
			 &rSsid,
			 prWlanBeaconFrame->u2CapInfo & CAP_INFO_PRIVACY ? 1 : 0,
			 RCPI_TO_dBm(prBssDesc->ucRCPI),
			 eNetworkType,
			 &rConfiguration,
			 eOpMode,
			 aucRatesEx,
			 prSwRfb->u2PacketLen - prSwRfb->u2HeaderLen,
			 (PUINT_8) ((ULONG) (prSwRfb->pvHeader) + WLAN_MAC_MGMT_HEADER_LEN));

	return WLAN_STATUS_SUCCESS;

}				/* end of scanAddScanResult() */

#if 1

BOOLEAN scanCheckBssIsLegal(IN P_ADAPTER_T prAdapter, P_BSS_DESC_T prBssDesc)
{
	BOOLEAN fgAddToScanResult = FALSE;
	ENUM_BAND_T eBand = 0;
	UINT_8 ucChannel = 0;

	ASSERT(prAdapter);
	/* check the channel is in the legal doamin */
	if (rlmDomainIsLegalChannel(prAdapter, prBssDesc->eBand, prBssDesc->ucChannelNum) == TRUE) {
		/* check ucChannelNum/eBand for adjacement channel filtering */
		if (cnmAisInfraChannelFixed(prAdapter, &eBand, &ucChannel) == TRUE &&
		    (eBand != prBssDesc->eBand || ucChannel != prBssDesc->ucChannelNum)) {
			fgAddToScanResult = FALSE;
		} else {
			fgAddToScanResult = TRUE;
		}
	}
	return fgAddToScanResult;

}

VOID scanReportBss2Cfg80211(IN P_ADAPTER_T prAdapter, IN ENUM_BSS_TYPE_T eBSSType, IN P_BSS_DESC_T SpecificprBssDesc)
{
	P_SCAN_INFO_T prScanInfo = (P_SCAN_INFO_T) NULL;
	P_LINK_T prBSSDescList = (P_LINK_T) NULL;
	P_BSS_DESC_T prBssDesc = (P_BSS_DESC_T) NULL;
	RF_CHANNEL_INFO_T rChannelInfo;

	ASSERT(prAdapter);

	prScanInfo = &(prAdapter->rWifiVar.rScanInfo);

	prBSSDescList = &prScanInfo->rBSSDescList;

	DBGLOG(SCN, TRACE, "scanReportBss2Cfg80211\n");

	if (SpecificprBssDesc) {
		{
			/* check BSSID is legal channel */
			if (!scanCheckBssIsLegal(prAdapter, SpecificprBssDesc)) {
				DBGLOG(SCN, TRACE, "Remove specific SSID[%s %d]\n",
						    SpecificprBssDesc->aucSSID, SpecificprBssDesc->ucChannelNum);
				return;
			}

			DBGLOG(SCN, TRACE, "Report Specific SSID[%s]\n", SpecificprBssDesc->aucSSID);
			if (eBSSType == BSS_TYPE_INFRASTRUCTURE) {

				kalIndicateBssInfo(prAdapter->prGlueInfo,
						   (PUINT_8) SpecificprBssDesc->aucRawBuf,
						   SpecificprBssDesc->u2RawLength,
						   SpecificprBssDesc->ucChannelNum,
						   RCPI_TO_dBm(SpecificprBssDesc->ucRCPI));
			} else {

				rChannelInfo.ucChannelNum = SpecificprBssDesc->ucChannelNum;
				rChannelInfo.eBand = SpecificprBssDesc->eBand;
				kalP2PIndicateBssInfo(prAdapter->prGlueInfo,
						      (PUINT_8) SpecificprBssDesc->aucRawBuf,
						      SpecificprBssDesc->u2RawLength,
						      &rChannelInfo, RCPI_TO_dBm(SpecificprBssDesc->ucRCPI));

			}

#if CFG_ENABLE_WIFI_DIRECT
			SpecificprBssDesc->fgIsP2PReport = FALSE;
#endif
		}
	} else {
		/* Search BSS Desc from current SCAN result list. */
		LINK_FOR_EACH_ENTRY(prBssDesc, prBSSDescList, rLinkEntry, BSS_DESC_T) {

#if CFG_AUTO_CHANNEL_SEL_SUPPORT
			/* 4 Auto Channel Selection:Record the AP Number */
			P_PARAM_CHN_LOAD_INFO prChnLoad;
			UINT_8 ucIdx = 0;

			if (((prBssDesc->ucChannelNum > 0) && (prBssDesc->ucChannelNum <= 48))
			    || (prBssDesc->ucChannelNum >= 147) /*non-DFS Channel */) {
				if (prBssDesc->ucChannelNum <= HW_CHNL_NUM_MAX_2G4) {
					ucIdx = prBssDesc->ucChannelNum - 1;
				} else if (prBssDesc->ucChannelNum <= 48) {
					ucIdx = (UINT_8) (HW_CHNL_NUM_MAX_2G4 + (prBssDesc->ucChannelNum - 34) / 4);
				} else {
					ucIdx =
					    (UINT_8) (HW_CHNL_NUM_MAX_2G4 + 4 + (prBssDesc->ucChannelNum - 149) / 4);
				}

				if (ucIdx < MAX_AUTO_CHAL_NUM) {
					prChnLoad = (P_PARAM_CHN_LOAD_INFO) &
						(prAdapter->rWifiVar.rChnLoadInfo.rEachChnLoad[ucIdx]);
					prChnLoad->ucChannel = prBssDesc->ucChannelNum;
					prChnLoad->u2APNum++;
				} else {
					DBGLOG(SCN, WARN, "ACS: ChIdx > MAX_AUTO_CHAL_NUM\n");
				}

			}
#endif
			/* check BSSID is legal channel */
			if (!scanCheckBssIsLegal(prAdapter, prBssDesc)) {
				DBGLOG(SCN, TRACE, "Remove SSID[%s %d]\n",
						    prBssDesc->aucSSID, prBssDesc->ucChannelNum);

				continue;
			}

			if ((prBssDesc->eBSSType == eBSSType)
#if CFG_ENABLE_WIFI_DIRECT
			    || ((eBSSType == BSS_TYPE_P2P_DEVICE) && (prBssDesc->fgIsP2PReport == TRUE))
#endif
			    ) {

				DBGLOG(SCN, TRACE, "Report ALL SSID[%s %d]\n",
						    prBssDesc->aucSSID, prBssDesc->ucChannelNum);

				if (eBSSType == BSS_TYPE_INFRASTRUCTURE) {
					if (prBssDesc->u2RawLength != 0) {
						kalIndicateBssInfo(prAdapter->prGlueInfo,
								   (PUINT_8) prBssDesc->aucRawBuf,
								   prBssDesc->u2RawLength,
								   prBssDesc->ucChannelNum,
								   RCPI_TO_dBm(prBssDesc->ucRCPI));
						kalMemZero(prBssDesc->aucRawBuf, CFG_RAW_BUFFER_SIZE);
						prBssDesc->u2RawLength = 0;

#if CFG_ENABLE_WIFI_DIRECT
						prBssDesc->fgIsP2PReport = FALSE;
#endif
					}
				} else {
#if CFG_ENABLE_WIFI_DIRECT
					if (prBssDesc->fgIsP2PReport == TRUE) {
#endif
						rChannelInfo.ucChannelNum = prBssDesc->ucChannelNum;
						rChannelInfo.eBand = prBssDesc->eBand;

						kalP2PIndicateBssInfo(prAdapter->prGlueInfo,
								      (PUINT_8) prBssDesc->aucRawBuf,
								      prBssDesc->u2RawLength,
								      &rChannelInfo, RCPI_TO_dBm(prBssDesc->ucRCPI));

						/* do not clear it then we can pass the bss in Specific report */
						/* kalMemZero(prBssDesc->aucRawBuf,CFG_RAW_BUFFER_SIZE); */

						/*
						   the BSS entry will not be cleared after scan done.
						   So if we dont receive the BSS in next scan, we cannot
						   pass it. We use u2RawLength for the purpose.
						 */
						/* prBssDesc->u2RawLength=0; */
#if CFG_ENABLE_WIFI_DIRECT
						prBssDesc->fgIsP2PReport = FALSE;
					}
#endif
				}
			}

		}
#if CFG_AUTO_CHANNEL_SEL_SUPPORT
		prAdapter->rWifiVar.rChnLoadInfo.fgDataReadyBit = TRUE;
#endif

	}

}

#endif

/*----------------------------------------------------------------------------*/
/*!
* @brief Parse the content of given Beacon or ProbeResp Frame.
*
* @param[in] prSwRfb            Pointer to the receiving SW_RFB_T structure.
*
* @retval WLAN_STATUS_SUCCESS           if not report this SW_RFB_T to host
* @retval WLAN_STATUS_PENDING           if report this SW_RFB_T to host as scan result
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS scanProcessBeaconAndProbeResp(IN P_ADAPTER_T prAdapter, IN P_SW_RFB_T prSwRfb)
{
	P_CONNECTION_SETTINGS_T prConnSettings;
	P_BSS_DESC_T prBssDesc = (P_BSS_DESC_T) NULL;
	WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
	P_BSS_INFO_T prAisBssInfo;
	P_WLAN_BEACON_FRAME_T prWlanBeaconFrame = (P_WLAN_BEACON_FRAME_T) NULL;
#if CFG_SLT_SUPPORT
	P_SLT_INFO_T prSltInfo = (P_SLT_INFO_T) NULL;
#endif

	ASSERT(prAdapter);
	ASSERT(prSwRfb);

	/* 4 <0> Ignore invalid Beacon Frame */
	if ((prSwRfb->u2PacketLen - prSwRfb->u2HeaderLen) <
		(TIMESTAMP_FIELD_LEN + BEACON_INTERVAL_FIELD_LEN + CAP_INFO_FIELD_LEN)) {
		/* to debug beacon length too small issue */
		UINT_32 u4MailBox0;

		nicGetMailbox(prAdapter, 0, &u4MailBox0);
		DBGLOG(SCN, WARN, "if conn sys also get less length (0x5a means yes) %x\n", (UINT_32) u4MailBox0);
		DBGLOG(SCN, WARN, "u2PacketLen %d, u2HeaderLen %d, payloadLen %d\n",
			prSwRfb->u2PacketLen, prSwRfb->u2HeaderLen,
			prSwRfb->u2PacketLen - prSwRfb->u2HeaderLen);
		/* dumpMemory8(prSwRfb->pvHeader, prSwRfb->u2PacketLen); */

#ifndef _lint
		ASSERT(0);
#endif /* _lint */
		return rStatus;
	}
#if CFG_SLT_SUPPORT
	prSltInfo = &prAdapter->rWifiVar.rSltInfo;

	if (prSltInfo->fgIsDUT) {
		DBGLOG(SCN, INFO, "\n\rBCN: RX\n");
		prSltInfo->u4BeaconReceiveCnt++;
		return WLAN_STATUS_SUCCESS;
	} else {
		return WLAN_STATUS_SUCCESS;
	}
#endif

	prConnSettings = &(prAdapter->rWifiVar.rConnSettings);
	prAisBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX]);
	prWlanBeaconFrame = (P_WLAN_BEACON_FRAME_T) prSwRfb->pvHeader;

	/*ALPS01475157: don't show SSID on scan list for multicast MAC AP */
	if (IS_BMCAST_MAC_ADDR(prWlanBeaconFrame->aucSrcAddr)) {
		DBGLOG(SCN, WARN, "received beacon/probe response from multicast AP\n");
		return rStatus;
	}

	/* 4 <1> Parse and add into BSS_DESC_T */
	prBssDesc = scanAddToBssDesc(prAdapter, prSwRfb);

	if (prBssDesc) {
		/* 4 <1.1> Beacon Change Detection for Connected BSS */
		if (prAisBssInfo->eConnectionState == PARAM_MEDIA_STATE_CONNECTED &&
		    ((prBssDesc->eBSSType == BSS_TYPE_INFRASTRUCTURE && prConnSettings->eOPMode != NET_TYPE_IBSS)
		     || (prBssDesc->eBSSType == BSS_TYPE_IBSS && prConnSettings->eOPMode != NET_TYPE_INFRA)) &&
		    EQUAL_MAC_ADDR(prBssDesc->aucBSSID, prAisBssInfo->aucBSSID) &&
		    EQUAL_SSID(prBssDesc->aucSSID, prBssDesc->ucSSIDLen, prAisBssInfo->aucSSID,
			       prAisBssInfo->ucSSIDLen)) {
			BOOLEAN fgNeedDisconnect = FALSE;

#if CFG_SUPPORT_BEACON_CHANGE_DETECTION
			/* <1.1.2> check if supported rate differs */
			if (prAisBssInfo->u2OperationalRateSet != prBssDesc->u2OperationalRateSet)
				fgNeedDisconnect = TRUE;
#endif
#if CFG_SUPPORT_DETECT_SECURITY_MODE_CHANGE
			if (
#if CFG_SUPPORT_WAPI
			(prAdapter->rWifiVar.rConnSettings.fgWapiMode == TRUE &&
			!wapiPerformPolicySelection(prAdapter, prBssDesc)) ||
#endif
			rsnCheckSecurityModeChanged(prAdapter, prAisBssInfo, prBssDesc)) {
				DBGLOG(SCN, INFO, "Beacon security mode change detected\n");
				fgNeedDisconnect = FALSE;
				aisBssSecurityChanged(prAdapter);
			}
#endif

			/* <1.1.3> beacon content change detected, disconnect immediately */
			if (fgNeedDisconnect == TRUE)
				aisBssBeaconTimeout(prAdapter);
		}
		/* 4 <1.1> Update AIS_BSS_INFO */
		if (((prBssDesc->eBSSType == BSS_TYPE_INFRASTRUCTURE && prConnSettings->eOPMode != NET_TYPE_IBSS)
		     || (prBssDesc->eBSSType == BSS_TYPE_IBSS && prConnSettings->eOPMode != NET_TYPE_INFRA))) {
			if (prAisBssInfo->eConnectionState == PARAM_MEDIA_STATE_CONNECTED) {
				/* *not* checking prBssDesc->fgIsConnected anymore,
				 * due to Linksys AP uses " " as hidden SSID, and would have different BSS descriptor */
				if ((!prAisBssInfo->ucDTIMPeriod) &&
				    EQUAL_MAC_ADDR(prBssDesc->aucBSSID, prAisBssInfo->aucBSSID) &&
				    (prAisBssInfo->eCurrentOPMode == OP_MODE_INFRASTRUCTURE) &&
				    ((prWlanBeaconFrame->u2FrameCtrl & MASK_FRAME_TYPE) == MAC_FRAME_BEACON)) {

					prAisBssInfo->ucDTIMPeriod = prBssDesc->ucDTIMPeriod;

					/* sync with firmware for beacon information */
					nicPmIndicateBssConnected(prAdapter, NETWORK_TYPE_AIS_INDEX);
				}
			}
#if CFG_SUPPORT_ADHOC
			if (EQUAL_SSID(prBssDesc->aucSSID,
				prBssDesc->ucSSIDLen,
				prConnSettings->aucSSID,
				prConnSettings->ucSSIDLen) &&
			    (prBssDesc->eBSSType == BSS_TYPE_IBSS) && (prAisBssInfo->eCurrentOPMode == OP_MODE_IBSS)) {
				ibssProcessMatchedBeacon(prAdapter, prAisBssInfo, prBssDesc,
							 prSwRfb->prHifRxHdr->ucRcpi);
			}
#endif /* CFG_SUPPORT_ADHOC */
		}

		rlmProcessBcn(prAdapter,
			      prSwRfb,
			      ((P_WLAN_BEACON_FRAME_T) (prSwRfb->pvHeader))->aucInfoElem,
			      (prSwRfb->u2PacketLen - prSwRfb->u2HeaderLen) -
			      (UINT_16) (OFFSET_OF(WLAN_BEACON_FRAME_BODY_T, aucInfoElem[0])));

		/* 4 <3> Send SW_RFB_T to HIF when we perform SCAN for HOST */
		if (prBssDesc->eBSSType == BSS_TYPE_INFRASTRUCTURE || prBssDesc->eBSSType == BSS_TYPE_IBSS) {
			/* for AIS, send to host */
			if (prConnSettings->fgIsScanReqIssued || prAdapter->rWifiVar.rScanInfo.fgNloScanning) {
				BOOLEAN fgAddToScanResult;

				fgAddToScanResult = scanCheckBssIsLegal(prAdapter, prBssDesc);

				if (fgAddToScanResult == TRUE)
					rStatus = scanAddScanResult(prAdapter, prBssDesc, prSwRfb);
			}
		}
#if CFG_ENABLE_WIFI_DIRECT
		if (prAdapter->fgIsP2PRegistered)
			scanP2pProcessBeaconAndProbeResp(prAdapter, prSwRfb, &rStatus, prBssDesc, prWlanBeaconFrame);
#endif
	}

	return rStatus;

}				/* end of scanProcessBeaconAndProbeResp() */

/*----------------------------------------------------------------------------*/
/*!
* \brief Search the Candidate of BSS Descriptor for JOIN(Infrastructure) or
*        MERGE(AdHoc) according to current Connection Policy.
*
* \return   Pointer to BSS Descriptor, if found. NULL, if not found
*/
/*----------------------------------------------------------------------------*/
P_BSS_DESC_T scanSearchBssDescByPolicy(IN P_ADAPTER_T prAdapter, IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex)
{
	P_CONNECTION_SETTINGS_T prConnSettings;
	P_BSS_INFO_T prBssInfo;
	P_AIS_SPECIFIC_BSS_INFO_T prAisSpecBssInfo;
	P_SCAN_INFO_T prScanInfo;

	P_LINK_T prBSSDescList;

	P_BSS_DESC_T prBssDesc = (P_BSS_DESC_T) NULL;
	P_BSS_DESC_T prPrimaryBssDesc = (P_BSS_DESC_T) NULL;
	P_BSS_DESC_T prCandidateBssDesc = (P_BSS_DESC_T) NULL;

	P_STA_RECORD_T prStaRec = (P_STA_RECORD_T) NULL;
	P_STA_RECORD_T prPrimaryStaRec;
	P_STA_RECORD_T prCandidateStaRec = (P_STA_RECORD_T) NULL;

	OS_SYSTIME rCurrentTime;

	/* The first one reach the check point will be our candidate */
	BOOLEAN fgIsFindFirst = (BOOLEAN) FALSE;

	BOOLEAN fgIsFindBestRSSI = (BOOLEAN) FALSE;
	BOOLEAN fgIsFindBestEncryptionLevel = (BOOLEAN) FALSE;
	/* BOOLEAN fgIsFindMinChannelLoad = (BOOLEAN)FALSE; */

	/* TODO(Kevin): Support Min Channel Load */
	/* UINT_8 aucChannelLoad[CHANNEL_NUM] = {0}; */

	BOOLEAN fgIsFixedChannel;
	ENUM_BAND_T eBand = 0;
	UINT_8 ucChannel = 0;

	ASSERT(prAdapter);

	prConnSettings = &(prAdapter->rWifiVar.rConnSettings);
	prBssInfo = &(prAdapter->rWifiVar.arBssInfo[eNetTypeIndex]);

	prAisSpecBssInfo = &(prAdapter->rWifiVar.rAisSpecificBssInfo);

	prScanInfo = &(prAdapter->rWifiVar.rScanInfo);
	prBSSDescList = &prScanInfo->rBSSDescList;

	GET_CURRENT_SYSTIME(&rCurrentTime);

	/* check for fixed channel operation */
	if (eNetTypeIndex == NETWORK_TYPE_AIS_INDEX) {
#if CFG_P2P_LEGACY_COEX_REVISE
		fgIsFixedChannel = cnmAisDetectP2PChannel(prAdapter, &eBand, &ucChannel);
#else
		fgIsFixedChannel = cnmAisInfraChannelFixed(prAdapter, &eBand, &ucChannel);
#endif
	} else {
		fgIsFixedChannel = FALSE;
	}

#if DBG
	if (prConnSettings->ucSSIDLen < ELEM_MAX_LEN_SSID)
		prConnSettings->aucSSID[prConnSettings->ucSSIDLen] = '\0';
#endif

	DBGLOG(SCN, INFO, "SEARCH: Bss Num: %d, Look for SSID: %s, %pM Band=%d, channel=%d\n",
			   (UINT_32) prBSSDescList->u4NumElem, prConnSettings->aucSSID,
			   (prConnSettings->aucBSSID), eBand, ucChannel);

	/* 4 <1> The outer loop to search for a candidate. */
	LINK_FOR_EACH_ENTRY(prBssDesc, prBSSDescList, rLinkEntry, BSS_DESC_T) {

		/* TODO(Kevin): Update Minimum Channel Load Information here */

		DBGLOG(SCN, TRACE, "SEARCH: [ %pM ], SSID:%s\n",
				   prBssDesc->aucBSSID, prBssDesc->aucSSID);

		/* 4 <2> Check PHY Type and attributes */
		/* 4 <2.1> Check Unsupported BSS PHY Type */
		if (!(prBssDesc->ucPhyTypeSet & (prAdapter->rWifiVar.ucAvailablePhyTypeSet))) {
			DBGLOG(SCN, TRACE, "SEARCH: Ignore unsupported ucPhyTypeSet = %x\n", prBssDesc->ucPhyTypeSet);
			continue;
		}
		/* 4 <2.2> Check if has unknown NonHT BSS Basic Rate Set. */
		if (prBssDesc->fgIsUnknownBssBasicRate)
			continue;
		/* 4 <2.3> Check if fixed operation cases should be aware */
		if (fgIsFixedChannel == TRUE && (prBssDesc->eBand != eBand || prBssDesc->ucChannelNum != ucChannel))
			continue;
		/* 4 <2.4> Check if the channel is legal under regulatory domain */
		if (rlmDomainIsLegalChannel(prAdapter, prBssDesc->eBand, prBssDesc->ucChannelNum) == FALSE)
			continue;
		/* 4 <2.5> Check if this BSS_DESC_T is stale */
		if (CHECK_FOR_TIMEOUT(rCurrentTime, prBssDesc->rUpdateTime,
				      SEC_TO_SYSTIME(SCN_BSS_DESC_REMOVE_TIMEOUT_SEC))) {

			BOOLEAN fgIsNeedToCheckTimeout = TRUE;

#if CFG_SUPPORT_ROAMING
			P_ROAMING_INFO_T prRoamingFsmInfo;

			prRoamingFsmInfo = (P_ROAMING_INFO_T) &(prAdapter->rWifiVar.rRoamingInfo);
			if ((prRoamingFsmInfo->eCurrentState == ROAMING_STATE_DISCOVERY) ||
			    (prRoamingFsmInfo->eCurrentState == ROAMING_STATE_ROAM)) {
				if (++prRoamingFsmInfo->RoamingEntryTimeoutSkipCount <
				    ROAMING_ENTRY_TIMEOUT_SKIP_COUNT_MAX) {
					fgIsNeedToCheckTimeout = FALSE;
					DBGLOG(SCN, INFO, "SEARCH: Romaing skip SCN_BSS_DESC_REMOVE_TIMEOUT_SEC\n");
				}
			}
#endif

			if (fgIsNeedToCheckTimeout == TRUE) {
				DBGLOG(SCN, TRACE, "Ignore stale bss %pM\n", prBssDesc->aucBSSID);
				continue;
			}
		}
		/* 4 <3> Check if reach the excessive join retry limit */
		/* NOTE(Kevin): STA_RECORD_T is recorded by TA. */
		prStaRec = cnmGetStaRecByAddress(prAdapter, (UINT_8) eNetTypeIndex, prBssDesc->aucSrcAddr);

		if (prStaRec) {
			/* NOTE(Kevin):
			 * The Status Code is the result of a Previous Connection Request,
			 * we use this as SCORE for choosing a proper
			 * candidate (Also used for compare see <6>)
			 * The Reason Code is an indication of the reason why AP reject us,
			 * we use this Code for "Reject"
			 * a SCAN result to become our candidate(Like a blacklist).
			 */
#if 0				/* TODO(Kevin): */
			if (prStaRec->u2ReasonCode != REASON_CODE_RESERVED) {
				DBGLOG(SCN, INFO, "SEARCH: Ignore BSS with previous Reason Code = %d\n",
						   prStaRec->u2ReasonCode);
				continue;
			} else
#endif
			if (prStaRec->u2StatusCode != STATUS_CODE_SUCCESSFUL) {
				/* NOTE(Kevin): greedy association - after timeout, we'll still
				 * try to associate to the AP whose STATUS of conection attempt
				 * was not success.
				 * We may also use (ucJoinFailureCount x JOIN_RETRY_INTERVAL_SEC) for
				 * time bound.
				 */
				if ((prStaRec->ucJoinFailureCount < JOIN_MAX_RETRY_FAILURE_COUNT) ||
				    (CHECK_FOR_TIMEOUT(rCurrentTime,
						       prStaRec->rLastJoinTime,
						       SEC_TO_SYSTIME(JOIN_RETRY_INTERVAL_SEC)))) {

					/* NOTE(Kevin): Every JOIN_RETRY_INTERVAL_SEC interval, we can retry
					 * JOIN_MAX_RETRY_FAILURE_COUNT times.
					 */
					if (prStaRec->ucJoinFailureCount >= JOIN_MAX_RETRY_FAILURE_COUNT)
						prStaRec->ucJoinFailureCount = 0;
					DBGLOG(SCN, INFO,
					       "SEARCH: Try to join BSS again,Status Code=%d (Curr=%u/Last Join=%u)\n",
						prStaRec->u2StatusCode, rCurrentTime, prStaRec->rLastJoinTime);
				} else {
					DBGLOG(SCN, INFO,
					       "SEARCH: Ignore BSS which reach maximum Join Retry Count = %d\n",
						JOIN_MAX_RETRY_FAILURE_COUNT);
					continue;
				}

			}
		}
		/* 4 <4> Check for various NETWORK conditions */
		if (eNetTypeIndex == NETWORK_TYPE_AIS_INDEX) {

			/* 4 <4.1> Check BSS Type for the corresponding Operation Mode in Connection Setting */
			/* NOTE(Kevin): For NET_TYPE_AUTO_SWITCH, we will always pass following check. */
			if (((prConnSettings->eOPMode == NET_TYPE_INFRA) &&
			     (prBssDesc->eBSSType != BSS_TYPE_INFRASTRUCTURE))
#if CFG_SUPPORT_ADHOC
			     || ((prConnSettings->eOPMode == NET_TYPE_IBSS
			      || prConnSettings->eOPMode == NET_TYPE_DEDICATED_IBSS)
			     && (prBssDesc->eBSSType != BSS_TYPE_IBSS))
#endif
			     ) {

				DBGLOG(SCN, TRACE, "Cur OPMode %d, Ignore eBSSType = %d\n",
						prConnSettings->eOPMode, prBssDesc->eBSSType);
				continue;
			}
			/* 4 <4.2> Check AP's BSSID if OID_802_11_BSSID has been set. */
			if ((prConnSettings->fgIsConnByBssidIssued) &&
				(prBssDesc->eBSSType == BSS_TYPE_INFRASTRUCTURE)) {

				if (UNEQUAL_MAC_ADDR(prConnSettings->aucBSSID, prBssDesc->aucBSSID)) {

					DBGLOG(SCN, TRACE, "SEARCH: Ignore due to BSSID was not matched!\n");
					continue;
				}
			}
#if CFG_SUPPORT_ADHOC
			/* 4 <4.3> Check for AdHoc Mode */
			if (prBssDesc->eBSSType == BSS_TYPE_IBSS) {
				OS_SYSTIME rCurrentTime;

				/* 4 <4.3.1> Check if this SCAN record has been updated recently for IBSS. */
				/* NOTE(Kevin): Because some STA may change its BSSID frequently after it
				 * create the IBSS - e.g. IPN2220, so we need to make sure we get the new one.
				 * For BSS, if the old record was matched, however it won't be able to pass
				 * the Join Process later.
				 */
				GET_CURRENT_SYSTIME(&rCurrentTime);
				if (CHECK_FOR_TIMEOUT(rCurrentTime, prBssDesc->rUpdateTime,
						      SEC_TO_SYSTIME(SCN_ADHOC_BSS_DESC_TIMEOUT_SEC))) {
					DBGLOG(SCN, LOUD,
						"SEARCH: Skip old record of BSS Descriptor - BSSID:[%pM]\n\n",
						prBssDesc->aucBSSID);
					continue;
				}
				/* 4 <4.3.2> Check Peer's capability */
				if (ibssCheckCapabilityForAdHocMode(prAdapter, prBssDesc) == WLAN_STATUS_FAILURE) {

					if (prPrimaryBssDesc)
						DBGLOG(SCN, INFO,
							"SEARCH: BSS DESC MAC: %pM, not supported AdHoc Mode.\n",
							prPrimaryBssDesc->aucBSSID);

					continue;
				}
				/* 4 <4.3.3> Compare TSF */
				if (prBssInfo->fgIsBeaconActivated &&
				    UNEQUAL_MAC_ADDR(prBssInfo->aucBSSID, prBssDesc->aucBSSID)) {

					DBGLOG(SCN, LOUD,
					       "SEARCH: prBssDesc->fgIsLargerTSF = %d\n", prBssDesc->fgIsLargerTSF);

					if (!prBssDesc->fgIsLargerTSF) {
						DBGLOG(SCN, INFO,
						       "SEARCH: Ignore BSS DESC MAC: [ %pM ], Smaller TSF\n",
							prBssDesc->aucBSSID);
						continue;
					}
				}
			}
#endif /* CFG_SUPPORT_ADHOC */

		}
#if 0				/* TODO(Kevin): For IBSS */
		/* 4 <2.c> Check if this SCAN record has been updated recently for IBSS. */
		/* NOTE(Kevin): Because some STA may change its BSSID frequently after it
		 * create the IBSS, so we need to make sure we get the new one.
		 * For BSS, if the old record was matched, however it won't be able to pass
		 * the Join Process later.
		 */
		if (prBssDesc->eBSSType == BSS_TYPE_IBSS) {
			OS_SYSTIME rCurrentTime;

			GET_CURRENT_SYSTIME(&rCurrentTime);
			if (CHECK_FOR_TIMEOUT(rCurrentTime, prBssDesc->rUpdateTime,
					      SEC_TO_SYSTIME(BSS_DESC_TIMEOUT_SEC))) {
				DBGLOG(SCAN, TRACE, "Skip old record of BSS Descriptor - BSSID:[%pM]\n\n",
						     prBssDesc->aucBSSID);
				continue;
			}
		}

		if ((prBssDesc->eBSSType == BSS_TYPE_INFRASTRUCTURE) &&
		    (prAdapter->eConnectionState == MEDIA_STATE_CONNECTED)) {
			OS_SYSTIME rCurrentTime;

			GET_CURRENT_SYSTIME(&rCurrentTime);
			if (CHECK_FOR_TIMEOUT(rCurrentTime, prBssDesc->rUpdateTime,
					      SEC_TO_SYSTIME(BSS_DESC_TIMEOUT_SEC))) {
				DBGLOG(SCAN, TRACE, "Skip old record of BSS Descriptor - BSSID:[%pM]\n\n",
						     (prBssDesc->aucBSSID));
				continue;
			}
		}
		/* 4 <4B> Check for IBSS AdHoc Mode. */
		/* Skip if one or more BSS Basic Rate are not supported by current AdHocMode */
		if (prPrimaryBssDesc->eBSSType == BSS_TYPE_IBSS) {
			/* 4 <4B.1> Check if match the Capability of current IBSS AdHoc Mode. */
			if (ibssCheckCapabilityForAdHocMode(prAdapter, prPrimaryBssDesc) == WLAN_STATUS_FAILURE) {

				DBGLOG(SCAN, TRACE,
				       "Ignore BSS DESC MAC: %pM, Capability not supported for AdHoc Mode.\n",
					prPrimaryBssDesc->aucBSSID);

				continue;
			}
			/* 4 <4B.2> IBSS Merge Decision Flow for SEARCH STATE. */
			if (prAdapter->fgIsIBSSActive &&
			    UNEQUAL_MAC_ADDR(prBssInfo->aucBSSID, prPrimaryBssDesc->aucBSSID)) {

				if (!fgIsLocalTSFRead) {
					NIC_GET_CURRENT_TSF(prAdapter, &rCurrentTsf);

					DBGLOG(SCAN, TRACE,
					       "\n\nCurrent TSF : %08lx-%08lx\n\n",
						rCurrentTsf.u.HighPart, rCurrentTsf.u.LowPart);
				}

				if (rCurrentTsf.QuadPart > prPrimaryBssDesc->u8TimeStamp.QuadPart) {
					DBGLOG(SCAN, TRACE,
					       "Ignore BSS DESC MAC: [%pM], Current BSSID: [%pM].\n",
						prPrimaryBssDesc->aucBSSID, prBssInfo->aucBSSID);

					DBGLOG(SCAN, TRACE,
					       "\n\nBSS's TSF : %08lx-%08lx\n\n",
						prPrimaryBssDesc->u8TimeStamp.u.HighPart,
						prPrimaryBssDesc->u8TimeStamp.u.LowPart);

					prPrimaryBssDesc->fgIsLargerTSF = FALSE;
					continue;
				} else {
					prPrimaryBssDesc->fgIsLargerTSF = TRUE;
				}

			}
		}
		/* 4 <5> Check the Encryption Status. */
		if (rsnPerformPolicySelection(prPrimaryBssDesc)) {

			if (prPrimaryBssDesc->ucEncLevel > 0) {
				fgIsFindBestEncryptionLevel = TRUE;

				fgIsFindFirst = FALSE;
			}
		} else {
			/* Can't pass the Encryption Status Check, get next one */
			continue;
		}

		/* For RSN Pre-authentication, update the PMKID canidate list for
		   same SSID and encrypt status */
		/* Update PMKID candicate list. */
		if (prAdapter->rWifiVar.rConnSettings.eAuthMode == AUTH_MODE_WPA2) {
			rsnUpdatePmkidCandidateList(prPrimaryBssDesc);
			if (prAdapter->rWifiVar.rAisBssInfo.u4PmkidCandicateCount)
				prAdapter->rWifiVar.rAisBssInfo.fgIndicatePMKID = rsnCheckPmkidCandicate();
		}
#endif

		prPrimaryBssDesc = (P_BSS_DESC_T) NULL;

		/* 4 <6> Check current Connection Policy. */
		switch (prConnSettings->eConnectionPolicy) {
		case CONNECT_BY_SSID_BEST_RSSI:
			/* Choose Hidden SSID to join only if the `fgIsEnableJoin...` is TRUE */
			if (prAdapter->rWifiVar.fgEnableJoinToHiddenSSID && prBssDesc->fgIsHiddenSSID) {
				/* NOTE(Kevin): following if () statement means that
				 * If Target is hidden, then we won't connect when user specify SSID_ANY policy.
				 */
				if (prConnSettings->ucSSIDLen) {
					prPrimaryBssDesc = prBssDesc;
					fgIsFindBestRSSI = TRUE;
				}

			} else if (EQUAL_SSID(prBssDesc->aucSSID,
					      prBssDesc->ucSSIDLen,
					      prConnSettings->aucSSID, prConnSettings->ucSSIDLen)) {
				prPrimaryBssDesc = prBssDesc;
				fgIsFindBestRSSI = TRUE;

				DBGLOG(SCN, TRACE, "SEARCH: fgIsFindBestRSSI=TRUE, %d, prPrimaryBssDesc=[ %pM ]\n",
						   prBssDesc->ucRCPI, prPrimaryBssDesc->aucBSSID);
			}
			break;

		case CONNECT_BY_SSID_ANY:
			/* NOTE(Kevin): In this policy, we don't know the desired
			 * SSID from user, so we should exclude the Hidden SSID from scan list.
			 * And because we refuse to connect to Hidden SSID node at the beginning, so
			 * when the JOIN Module deal with a BSS_DESC_T which has fgIsHiddenSSID == TRUE,
			 * then the Connection Settings must be valid without doubt.
			 */
			if (!prBssDesc->fgIsHiddenSSID) {
				prPrimaryBssDesc = prBssDesc;
				fgIsFindFirst = TRUE;
			}
			break;

		case CONNECT_BY_BSSID:
			if (EQUAL_MAC_ADDR(prBssDesc->aucBSSID, prConnSettings->aucBSSID))
				prPrimaryBssDesc = prBssDesc;
			break;

		default:
			break;
		}

		/* Primary Candidate was not found */
		if (prPrimaryBssDesc == NULL)
			continue;
		/* 4 <7> Check the Encryption Status. */
		if (prPrimaryBssDesc->eBSSType == BSS_TYPE_INFRASTRUCTURE) {
#if CFG_SUPPORT_WAPI
			if (prAdapter->rWifiVar.rConnSettings.fgWapiMode) {
				DBGLOG(SCN, TRACE, "SEARCH: fgWapiMode == 1\n");

				if (wapiPerformPolicySelection(prAdapter, prPrimaryBssDesc)) {
					fgIsFindFirst = TRUE;
				} else {
					/* Can't pass the Encryption Status Check, get next one */
					DBGLOG(SCN, TRACE, "SEARCH: WAPI cannot pass the Encryption Status Check!\n");
					continue;
				}
			} else
#endif
#if CFG_RSN_MIGRATION
			if (rsnPerformPolicySelection(prAdapter, prPrimaryBssDesc)) {
				if (prAisSpecBssInfo->fgCounterMeasure) {
					DBGLOG(RSN, INFO, "Skip while at counter measure period!!!\n");
					continue;
				}

				if (prPrimaryBssDesc->ucEncLevel > 0) {
					fgIsFindBestEncryptionLevel = TRUE;
					fgIsFindFirst = FALSE;
				}
#if 0
			/* Update PMKID candicate list. */
			if (prAdapter->rWifiVar.rConnSettings.eAuthMode == AUTH_MODE_WPA2) {
				rsnUpdatePmkidCandidateList(prPrimaryBssDesc);
				if (prAisSpecBssInfo->u4PmkidCandicateCount) {
					if (rsnCheckPmkidCandicate()) {
						DBGLOG(RSN, WARN,
						       "Prepare a timer to indicate candidate %pM\n",
							(prAisSpecBssInfo->arPmkidCache
								[prAisSpecBssInfo->u4PmkidCacheCount].
								rBssidInfo.aucBssid)));
						cnmTimerStopTimer(&prAisSpecBssInfo->rPreauthenticationTimer);
						cnmTimerStartTimer(&prAisSpecBssInfo->rPreauthenticationTimer,
								   SEC_TO_MSEC
								   (WAIT_TIME_IND_PMKID_CANDICATE_SEC));
					}
				}
			}
#endif
			} else {
				/* Can't pass the Encryption Status Check, get next one */
				continue;
			}
#endif
		} else {
			/* Todo:: P2P and BOW Policy Selection */
		}

		prPrimaryStaRec = prStaRec;

		/* 4 <8> Compare the Candidate and the Primary Scan Record. */
		if (!prCandidateBssDesc) {
			prCandidateBssDesc = prPrimaryBssDesc;
			prCandidateStaRec = prPrimaryStaRec;

			/* 4 <8.1> Condition - Get the first matched one. */
			if (fgIsFindFirst)
				break;
		} else {
#if 0				/* TODO(Kevin): For security(TBD) */
	/* 4 <6B> Condition - Choose the one with best Encryption Score. */
	if (fgIsFindBestEncryptionLevel) {
		if (prCandidateBssDesc->ucEncLevel < prPrimaryBssDesc->ucEncLevel) {

			prCandidateBssDesc = prPrimaryBssDesc;
			prCandidateStaRec = prPrimaryStaRec;
			continue;
		}
	}

	/* If reach here, that means they have the same Encryption Score.
	 */

	/* 4 <6C> Condition - Give opportunity to the one we didn't connect before. */
	/* For roaming, only compare the candidates other than current associated BSSID. */
	if (!prCandidateBssDesc->fgIsConnected && !prPrimaryBssDesc->fgIsConnected) {
		if ((prCandidateStaRec != (P_STA_RECORD_T) NULL) &&
		    (prCandidateStaRec->u2StatusCode != STATUS_CODE_SUCCESSFUL)) {

			DBGLOG(SCAN, TRACE,
			       "So far -BSS DESC MAC: %pM has nonzero Status Code = %d\n",
				prCandidateBssDesc->aucBSSID,
				prCandidateStaRec->u2StatusCode);

			if (prPrimaryStaRec != (P_STA_RECORD_T) NULL) {
				if (prPrimaryStaRec->u2StatusCode != STATUS_CODE_SUCCESSFUL) {

					/* Give opportunity to the one with smaller rLastJoinTime */
					if (TIME_BEFORE(prCandidateStaRec->rLastJoinTime,
							prPrimaryStaRec->rLastJoinTime)) {
						continue;
					}
					/* We've connect to CANDIDATE recently,
					 * let us try PRIMARY now */
					else {
						prCandidateBssDesc = prPrimaryBssDesc;
						prCandidateStaRec = prPrimaryStaRec;
						continue;
					}
				}
				/* PRIMARY's u2StatusCode = 0 */
				else {
					prCandidateBssDesc = prPrimaryBssDesc;
					prCandidateStaRec = prPrimaryStaRec;
					continue;
				}
			}
			/* PRIMARY has no StaRec - We didn't connet to PRIMARY before */
			else {
				prCandidateBssDesc = prPrimaryBssDesc;
				prCandidateStaRec = prPrimaryStaRec;
				continue;
			}
		} else {
			if ((prPrimaryStaRec != (P_STA_RECORD_T) NULL) &&
			    (prPrimaryStaRec->u2StatusCode != STATUS_CODE_SUCCESSFUL)) {
				continue;
			}
		}
	}
#endif

			/* 4 <6D> Condition - Visible SSID win Hidden SSID. */
			if (prCandidateBssDesc->fgIsHiddenSSID) {
				if (!prPrimaryBssDesc->fgIsHiddenSSID) {
					prCandidateBssDesc = prPrimaryBssDesc;	/* The non Hidden SSID win. */
					prCandidateStaRec = prPrimaryStaRec;
					continue;
				}
			} else {
				if (prPrimaryBssDesc->fgIsHiddenSSID)
					continue;
			}

			/* 4 <6E> Condition - Choose the one with better RCPI(RSSI). */
			if (fgIsFindBestRSSI) {
				/* TODO(Kevin): We shouldn't compare the actual value, we should
				 * allow some acceptable tolerance of some RSSI percentage here.
				 */
			DBGLOG(SCN, TRACE,
			"Candidate [%pM]: RCPI = %d, joinFailCnt=%d, Primary [%pM]: RCPI = %d, joinFailCnt=%d\n",
					prCandidateBssDesc->aucBSSID,
					prCandidateBssDesc->ucRCPI, prCandidateBssDesc->ucJoinFailureCount,
					prPrimaryBssDesc->aucBSSID,
					prPrimaryBssDesc->ucRCPI, prPrimaryBssDesc->ucJoinFailureCount);

				ASSERT(!(prCandidateBssDesc->fgIsConnected && prPrimaryBssDesc->fgIsConnected));
				if (prPrimaryBssDesc->ucJoinFailureCount >= SCN_BSS_JOIN_FAIL_THRESOLD) {
					/* give a chance to do join if join fail before
					 * SCN_BSS_DECRASE_JOIN_FAIL_CNT_SEC seconds
					 */
					if (CHECK_FOR_TIMEOUT(rCurrentTime, prBssDesc->rJoinFailTime,
							SEC_TO_SYSTIME(SCN_BSS_JOIN_FAIL_CNT_RESET_SEC))) {
						prBssDesc->ucJoinFailureCount = SCN_BSS_JOIN_FAIL_THRESOLD -
										SCN_BSS_JOIN_FAIL_RESET_STEP;
						DBGLOG(SCN, INFO,
						"decrease join fail count for Bss %pM to %u, timeout second %d\n",
							prBssDesc->aucBSSID, prBssDesc->ucJoinFailureCount,
							SCN_BSS_JOIN_FAIL_CNT_RESET_SEC);
					}
				}

				/* NOTE: To prevent SWING,
				 * we do roaming only if target AP has at least 5dBm larger than us. */
				if (prCandidateBssDesc->fgIsConnected) {
					if (prCandidateBssDesc->ucRCPI + ROAMING_NO_SWING_RCPI_STEP <=
						prPrimaryBssDesc->ucRCPI &&
						prPrimaryBssDesc->ucJoinFailureCount < SCN_BSS_JOIN_FAIL_THRESOLD) {

						prCandidateBssDesc = prPrimaryBssDesc;
						prCandidateStaRec = prPrimaryStaRec;
						continue;
					}
				} else if (prPrimaryBssDesc->fgIsConnected) {
					if (prCandidateBssDesc->ucRCPI <
					(prPrimaryBssDesc->ucRCPI + ROAMING_NO_SWING_RCPI_STEP) ||
						(prCandidateBssDesc->ucJoinFailureCount >=
						SCN_BSS_JOIN_FAIL_THRESOLD)) {
						prCandidateBssDesc = prPrimaryBssDesc;
						prCandidateStaRec = prPrimaryStaRec;
						continue;
					}
				} else if (prPrimaryBssDesc->ucJoinFailureCount >= SCN_BSS_JOIN_FAIL_THRESOLD)
					continue;
				else if (prCandidateBssDesc->ucJoinFailureCount >= SCN_BSS_JOIN_FAIL_THRESOLD ||
					prCandidateBssDesc->ucRCPI < prPrimaryBssDesc->ucRCPI) {
					prCandidateBssDesc = prPrimaryBssDesc;
					prCandidateStaRec = prPrimaryStaRec;
					continue;
				}
			}
#if 0
			/* If reach here, that means they have the same Encryption Score, and
			 * both RSSI value are close too.
			 */
			/* 4 <6F> Seek the minimum Channel Load for less interference. */
			if (fgIsFindMinChannelLoad) {
				/* Do nothing */
				/* TODO(Kevin): Check which one has minimum channel load in its channel */
			}
#endif
		}
	}


	if (prCandidateBssDesc != NULL) {
		DBGLOG(SCN, INFO,
		       "SEARCH: Candidate BSS: %pM\n", prCandidateBssDesc->aucBSSID);
	}

	return prCandidateBssDesc;

} /* end of scanSearchBssDescByPolicy() */

#if CFG_SUPPORT_AGPS_ASSIST
VOID scanReportScanResultToAgps(P_ADAPTER_T prAdapter)
{
	P_LINK_T prBSSDescList = &prAdapter->rWifiVar.rScanInfo.rBSSDescList;
	P_BSS_DESC_T prBssDesc = NULL;
	P_AGPS_AP_LIST_T prAgpsApList;
	P_AGPS_AP_INFO_T prAgpsInfo;
	P_SCAN_INFO_T prScanInfo = &prAdapter->rWifiVar.rScanInfo;
	UINT_8 ucIndex = 0;

	prAgpsApList = kalMemAlloc(sizeof(AGPS_AP_LIST_T), VIR_MEM_TYPE);
	if (!prAgpsApList)
		return;

	prAgpsInfo = &prAgpsApList->arApInfo[0];
	LINK_FOR_EACH_ENTRY(prBssDesc, prBSSDescList, rLinkEntry, BSS_DESC_T) {
		if (prBssDesc->rUpdateTime < prScanInfo->rLastScanCompletedTime)
			continue;
		COPY_MAC_ADDR(prAgpsInfo->aucBSSID, prBssDesc->aucBSSID);
		prAgpsInfo->ePhyType = AGPS_PHY_G;
		prAgpsInfo->u2Channel = prBssDesc->ucChannelNum;
		prAgpsInfo->i2ApRssi = RCPI_TO_dBm(prBssDesc->ucRCPI);
		prAgpsInfo++;
		ucIndex++;
		if (ucIndex == 32)
			break;
	}
	prAgpsApList->ucNum = ucIndex;
	GET_CURRENT_SYSTIME(&prScanInfo->rLastScanCompletedTime);
	/* DBGLOG(SCN, INFO, ("num of scan list:%d\n", ucIndex)); */
	kalIndicateAgpsNotify(prAdapter, AGPS_EVENT_WLAN_AP_LIST, (PUINT_8) prAgpsApList, sizeof(AGPS_AP_LIST_T));
	kalMemFree(prAgpsApList, VIR_MEM_TYPE, sizeof(AGPS_AP_LIST_T));
}
#endif
