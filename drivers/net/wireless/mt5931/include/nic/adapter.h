/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/include/nic/adapter.h#3 $
*/

/*! \file   adapter.h
    \brief  Definition of internal data structure for driver manipulation.

    In this file we define the internal data structure - ADAPTER_T which stands
    for MiniPort ADAPTER(From Windows point of view) or stands for Network ADAPTER.
*/



/*
** $Log: adapter.h $
** 
** 08 31 2012 yuche.tsai
** [ALPS00349585] [6577JB][WiFi direct][KE]Establish p2p connection while both device have connected to AP previously,one device reboots automatically with KE
** Fix possible KE when concurrent & disconnect.
** 
** 07 26 2012 yuche.tsai
** [ALPS00324337] [ALPS.JB][Hot-Spot] Driver update for Hot-Spot
** Update driver code of ALPS.JB for hot-spot.
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Let netdev bring up.
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Compile no error before trial run.
 *
 * 06 13 2012 yuche.tsai
 * NULL
 * Update maintrunk driver.
 * Add support for driver compose assoc request frame.
 *
 * 03 02 2012 terry.wu
 * NULL
 * Snc CFG80211 modification for ICS migration from branch 2.2.
 *
 * 03 02 2012 terry.wu
 * NULL
 * Sync CFG80211 modification from branch 2,2.
 *
 * 01 16 2012 cp.wu
 * [MT6620 Wi-Fi][Driver] API and behavior modification for preferred band configuration with corresponding network configuration 
 * add wlanSetPreferBandByNetwork() for glue layer to invoke for setting preferred band configuration corresponding to network type.
 *
 * 12 13 2011 cm.chang
 * [WCXRP00001136] [All Wi-Fi][Driver] Add wake lock for pending timer
 * Add wake lock if timer timeout value is smaller than 5 seconds
 *
 * 12 02 2011 yuche.tsai
 * NULL
 * Resolve inorder issue under AP mode.
 * 
 * data frame may TX before assoc response frame.
 *
 * 11 19 2011 yuche.tsai
 * NULL
 * Update RSSI for P2P.
 *
 * 11 18 2011 yuche.tsai
 * NULL
 * CONFIG P2P support RSSI query, default turned off.
 *
 * 11 11 2011 yuche.tsai
 * NULL
 * Fix work thread cancel issue.
 *
 * 10 21 2011 eddie.chen
 * [WCXRP00001051] [MT6620 Wi-Fi][Driver/Fw] Adjust the STA aging timeout
 * Add switch to ignore the STA aging timeout.
 *
 * 10 12 2011 wh.su
 * [WCXRP00001036] [MT6620 Wi-Fi][Driver][FW] Adding the 802.11w code for MFP
 * adding the 802.11w related function and define .
 *
 * 09 20 2011 cm.chang
 * [WCXRP00000997] [MT6620 Wi-Fi][Driver][FW] Handle change of BSS preamble type and slot time
 * Remove ERP member in adapter structure
 *
 * 09 14 2011 yuche.tsai
 * NULL
 * Add P2P IE in assoc response.
 *
 * 08 31 2011 cm.chang
 * [WCXRP00000969] [MT6620 Wi-Fi][Driver][FW] Channel list for 5G band based on country code
 * .
 *
 * 07 18 2011 chinghwa.yu
 * [WCXRP00000063] Update BCM CoEx design and settings[WCXRP00000612] [MT6620 Wi-Fi] [FW] CSD update SWRDD algorithm
 * Add CMD/Event for RDD and BWCS.
 *
 * 06 23 2011 cp.wu
 * [WCXRP00000812] [MT6620 Wi-Fi][Driver] not show NVRAM when there is no valid MAC address in NVRAM content
 * check with firmware for valid MAC address.
 *
 * 04 18 2011 terry.wu
 * [WCXRP00000660] [MT6620 Wi-Fi][Driver] Remove flag CFG_WIFI_DIRECT_MOVED
 * Remove flag CFG_WIFI_DIRECT_MOVED.
 *
 * 04 12 2011 cm.chang
 * [WCXRP00000634] [MT6620 Wi-Fi][Driver][FW] 2nd BSS will not support 40MHz bandwidth for concurrency
 * .
 *
 * 04 08 2011 yuche.tsai
 * [WCXRP00000624] [Volunteer Patch][MT6620][Driver] Add device discoverability support for GO.
 * Add device discoverability support.
 * Action frame callback for GO Device Discoverability Req.
 *
 * 04 08 2011 george.huang
 * [WCXRP00000621] [MT6620 Wi-Fi][Driver] Support P2P supplicant to set power mode
 * separate settings of P2P and AIS
 *
 * 04 08 2011 eddie.chen
 * [WCXRP00000617] [MT6620 Wi-Fi][DRV/FW] Fix for sigma
 * Fix for sigma
 *
 * 03 19 2011 yuche.tsai
 * [WCXRP00000584] [Volunteer Patch][MT6620][Driver] Add beacon timeout support for WiFi Direct.
 * Add beacon timeout support for WiFi Direct Network.
 *
 * 03 19 2011 yuche.tsai
 * [WCXRP00000581] [Volunteer Patch][MT6620][Driver] P2P IE in Assoc Req Issue
 * Make assoc req to append P2P IE if wifi direct is enabled.
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
 * 03 10 2011 yuche.tsai
 * [WCXRP00000533] [Volunteer Patch][MT6620][Driver] Provide a P2P function API for Legacy WiFi to query AP mode.
 * Provide an API for Legacy WiFi to query the operation mode..
 *
 * 03 05 2011 wh.su
 * [WCXRP00000506] [MT6620 Wi-Fi][Driver][FW] Add Security check related code
 * add the code to get the check rsponse and indicate to app.
 *
 * 03 02 2011 wh.su
 * [WCXRP00000448] [MT6620 Wi-Fi][Driver] Fixed WSC IE not send out at probe request
 * Add code to send beacon and probe response WSC IE at Auto GO.
 *
 * 03 02 2011 cp.wu
 * [WCXRP00000503] [MT6620 Wi-Fi][Driver] Take RCPI brought by association response as initial RSSI right after connection is built.
 * use RCPI brought by ASSOC-RESP after connection is built as initial RCPI to avoid using a uninitialized MAC-RX RCPI.
 *
 * 02 21 2011 terry.wu
 * [WCXRP00000476] [MT6620 Wi-Fi][Driver] Clean P2P scan list while removing P2P
 * Clean P2P scan list while removing P2P.
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
 * 02 10 2011 yuche.tsai
 * [WCXRP00000431] [Volunteer Patch][MT6620][Driver] Add MLME support for deauthentication under AP(Hot-Spot) mode.
 * Add RX deauthentication & disassociation process under Hot-Spot mode.
 *
 * 02 09 2011 wh.su
 * [WCXRP00000433] [MT6620 Wi-Fi][Driver] Remove WAPI structure define for avoid P2P module with structure miss-align pointer issue
 * always pre-allio WAPI related structure for align p2p module.
 *
 * 02 08 2011 yuche.tsai
 * [WCXRP00000419] [Volunteer Patch][MT6620/MT5931][Driver] Provide function of disconnect to target station for AAA module.
 * Provide disconnect function for AAA module.
 *
 * 02 01 2011 cm.chang
 * [WCXRP00000415] [MT6620 Wi-Fi][Driver] Check if any memory leakage happens when uninitializing in DGB mode
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
 * 01 27 2011 george.huang
 * [WCXRP00000400] [MT6620 Wi-Fi] support CTIA power mode setting
 * Support CTIA power mode setting.
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
 * 12 29 2010 eddie.chen
 * [WCXRP00000322] Add WMM IE in beacon,

Add per station flow control when STA is in PS


 * Add WMM parameter for broadcast.
 *
 * 12 29 2010 eddie.chen
 * [WCXRP00000322] Add WMM IE in beacon,

Add per station flow control when STA is in PS


 * Add CWMin CWMax for AP to generate IE.
 *
 * 12 29 2010 eddie.chen
 * [WCXRP00000322] Add WMM IE in beacon,
Add per station flow control when STA is in PS

 * 1) PS flow control event
 *
 * 2) WMM IE in beacon, assoc resp, probe resp
 *
 * 12 28 2010 cp.wu
 * [WCXRP00000269] [MT6620 Wi-Fi][Driver][Firmware] Prepare for v1.1 branch release
 * report EEPROM used flag via NIC_CAPABILITY
 *
 * 12 28 2010 cp.wu
 * [WCXRP00000269] [MT6620 Wi-Fi][Driver][Firmware] Prepare for v1.1 branch release
 * integrate with 'EEPROM used' flag for reporting correct capability to Engineer Mode/META and other tools
 *
 * 12 07 2010 cm.chang
 * [WCXRP00000238] MT6620 Wi-Fi][Driver][FW] Support regulation domain setting from NVRAM and supplicant
 * 1. Country code is from NVRAM or supplicant
 * 2. Change band definition in CMD/EVENT.
 *
 * 11 01 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check[WCXRP00000150] [MT6620 Wi-Fi][Driver] Add implementation for querying current TX rate from firmware auto rate module
 * 1) Query link speed (TX rate) from firmware directly with buffering mechanism to reduce overhead
 * 2) Remove CNM CH-RECOVER event handling
 * 3) cfg read/write API renamed with kal prefix for unified naming rules.
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
 * 10 08 2010 cp.wu
 * [WCXRP00000084] [MT6620 Wi-Fi][Driver][FW] Add fixed rate support for distance test
 * adding fixed rate support for distance test. (from registry setting)
 *
 * 10 06 2010 cp.wu
 * [WCXRP00000052] [MT6620 Wi-Fi][Driver] Eliminate Linux Compile Warning
 * code reorganization to improve isolation between GLUE and CORE layers.
 *
 * 10 05 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check
 * 1) add NVRAM access API
 * 2) fake scanning result when NVRAM doesn't exist and/or version mismatch. (off by compiler option)
 * 3) add OID implementation for NVRAM read/write service
 *
 * 09 27 2010 chinghwa.yu
 * [WCXRP00000063] Update BCM CoEx design and settings[WCXRP00000065] Update BoW design and settings
 * Update BCM/BoW design and settings.
 *
 * 09 24 2010 cp.wu
 * [WCXRP00000057] [MT6620 Wi-Fi][Driver] Modify online scan to a run-time switchable feature
 * Modify online scan as a run-time adjustable option (for Windows, in registry)
 *
 * 09 23 2010 cp.wu
 * [WCXRP00000051] [MT6620 Wi-Fi][Driver] WHQL test fail in MAC address changed item
 * use firmware reported mac address right after wlanAdapterStart() as permanent address
 *
 * 09 08 2010 cp.wu
 * NULL
 * use static memory pool for storing IEs of scanning result.
 *
 * 09 07 2010 yuche.tsai
 * NULL
 * Add a common IE buffer in P2P INFO structure.
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 09 01 2010 cp.wu
 * NULL
 * restore configuration as before.
 *
 * 09 01 2010 wh.su
 * NULL
 * adding the wapi support for integration test.
 *
 * 08 31 2010 kevin.huang
 * NULL
 * Use LINK LIST operation to process SCAN result
 *
 * 08 29 2010 yuche.tsai
 * NULL
 * Finish SLT TX/RX & Rate Changing Support.
 *
 * 08 25 2010 george.huang
 * NULL
 * update OID/ registry control path for PM related settings
 *
 * 08 24 2010 cm.chang
 * NULL
 * Support RLM initail channel of Ad-hoc, P2P and BOW
 *
 * 08 23 2010 chinghwa.yu
 * NULL
 * Update for BOW.
 *
 * 08 20 2010 cm.chang
 * NULL
 * Migrate RLM code to host from FW
 *
 * 08 16 2010 yuche.tsai
 * NULL
 * Add an intend mode for BSS info.
 * It is used to let P2P BSS Info to know which OP Mode it is going to become.
 *
 * 08 04 2010 george.huang
 * NULL
 * handle change PS mode OID/ CMD
 *
 * 08 02 2010 cp.wu
 * NULL
 * comment out deprecated members in BSS_INFO, which are only used by firmware rather than driver.
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
 * 07 24 2010 wh.su
 *
 * .support the Wi-Fi RSN
 *
 * 07 21 2010 yuche.tsai
 *
 * Add for P2P Scan Result Parsing & Saving.
 *
 * 07 19 2010 wh.su
 *
 * update for security supporting.
 *
 * 07 19 2010 cm.chang
 *
 * Set RLM parameters and enable CNM channel manager
 *
 * 07 19 2010 yuche.tsai
 *
 * Remove BSS info which is redonedent in Wifi Var..
 *
 * 07 16 2010 yarco.yang
 *
 * 1. Support BSS Absence/Presence Event
 * 2. Support STA change PS mode Event
 * 3. Support BMC forwarding for AP mode.
 *
 * 07 14 2010 yarco.yang
 *
 * 1. Remove CFG_MQM_MIGRATION
 * 2. Add CMD_UPDATE_WMM_PARMS command
 *
 * 07 09 2010 george.huang
 *
 * [WPD00001556] Migrate PM variables from FW to driver: for composing QoS Info
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 07 08 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Check draft RLM code for HT cap
 *
 * 06 29 2010 yarco.yang
 * [WPD00003837][MT6620]Data Path Refine
 * replace g_rQM with Adpater->rQM
 *
 * 06 28 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * 1st draft code for RLM module
 *
 * 06 21 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * remove duplicate variable for migration.
 *
 * 06 21 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * modify some code for concurrent network.
 *
 * 06 21 2010 yuche.tsai
 * [WPD00003839][MT6620 5931][P2P] Feature migration
 * Add P2P FSM Info in adapter.
 *
 * 06 21 2010 yarco.yang
 * [WPD00003837][MT6620]Data Path Refine
 * Support CFG_MQM_MIGRATION flag
 *
 * 06 18 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Provide cnmMgtPktAlloc() and alloc/free function of msg/buf
 *
 * 06 18 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * migration the security related function from firmware.
 *
 * 06 17 2010 yuche.tsai
 * [WPD00003839][MT6620 5931][P2P] Feature migration
 * Add P2P related field, additional include p2p_fsm.h if p2p is enabled.
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
 * 1) migrate assoc.c.
 * 2) add ucTxSeqNum for tracking frames which needs TX-DONE awareness
 * 3) add configuration options for CNM_MEM and RSN modules
 * 4) add data path for management frames
 * 5) eliminate rPacketInfo of MSDU_INFO_T
 *
 * 06 10 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add buildable & linkable ais_fsm.c
 *
 * related reference are still waiting to be resolved
 *
 * 06 09 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add definitions for module migration.
 *
 * 06 08 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * cnm_timer has been migrated.
 *
 * 06 08 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * hem_mbox is migrated.
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * merge wifi_var.h, precomp.h, cnm_timer.h (data type only)
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
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
 * 04 27 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * reserve field of privacy filter and RTS threshold setting.
 *
 * 04 13 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support
 * add framework for BT-over-Wi-Fi support.
 *  *  *  *  *  * 1) prPendingCmdInfo is replaced by queue for multiple handler capability
 *  *  *  *  *  * 2) command sequence number is now increased atomically
 *  *  *  *  *  * 3) private data could be hold and taken use for other purpose
 *
 * 04 07 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * rWlanInfo should be placed at adapter rather than glue due to most operations
 *  *  * are done in adapter layer.
 *
 * 04 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * code refine: fgTestMode should be at adapter rather than glue due to the device/fw is also involved
 *
 * 03 31 2010 wh.su
 * [WPD00003816][MT6620 Wi-Fi] Adding the security support
 * modify the wapi related code for new driver's design.
 *
 * 03 19 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) add ACPI D0/D3 state switching support
 *  *  * 2) use more formal way to handle interrupt when the status is retrieved from enhanced RX response
 *
 * 03 03 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * implement OID_802_3_MULTICAST_LIST oid handling
 *
 * 03 02 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) the use of prPendingOid revised, all accessing are now protected by spin lock
 *  *  * 2) ensure wlanReleasePendingOid will clear all command queues
 *
 * 02 09 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * move ucCmdSeqNum as instance variable
 *
 * 01 27 2010 wh.su
 * [WPD00003816][MT6620 Wi-Fi] Adding the security support
 * .
 *
 * 01 27 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1. eliminate improper variable in rHifInfo
 *  *  * 2. block TX/ordinary OID when RF test mode is engaged
 *  *  * 3. wait until firmware finish operation when entering into and leaving from RF test mode
 *  *  * 4. correct some HAL implementation
 *
 * 12 30 2009 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) According to CMD/EVENT documentation v0.8,
 *  *  * OID_CUSTOM_TEST_RX_STATUS & OID_CUSTOM_TEST_TX_STATUS is no longer used,
 *  *  * and result is retrieved by get ATInfo instead
 *  *  * 2) add 4 counter for recording aggregation statistics
 *
 * 12 28 2009 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * eliminate redundant variables for connection_state
**  \main\maintrunk.MT6620WiFiDriver_Prj\13 2009-12-16 18:02:03 GMT mtk02752
**  add external reference to avoid compilation error
**  \main\maintrunk.MT6620WiFiDriver_Prj\12 2009-12-10 16:40:26 GMT mtk02752
**  eliminate unused member
**  \main\maintrunk.MT6620WiFiDriver_Prj\11 2009-12-08 17:36:08 GMT mtk02752
**  add RF test data members into P_ADAPTER_T
**  \main\maintrunk.MT6620WiFiDriver_Prj\10 2009-10-13 21:58:45 GMT mtk01084
**  update for new HW architecture design
**  \main\maintrunk.MT6620WiFiDriver_Prj\9 2009-04-28 10:29:57 GMT mtk01461
**  Add read WTSR for SDIO_STATUS_ENHANCE mode
**  \main\maintrunk.MT6620WiFiDriver_Prj\8 2009-04-21 09:37:35 GMT mtk01461
**  Add prPendingCmdInfoOfOID for temporarily saving the CMD_INFO_T before en-queue to rCmdQueue
**  \main\maintrunk.MT6620WiFiDriver_Prj\7 2009-04-17 19:57:51 GMT mtk01461
**  Add MGMT Buffer Info
**  \main\maintrunk.MT6620WiFiDriver_Prj\6 2009-04-01 10:34:12 GMT mtk01461
**  Add SW pre test CFG_HIF_LOOPBACK_PRETEST
**  \main\maintrunk.MT6620WiFiDriver_Prj\5 2009-03-23 21:41:48 GMT mtk01461
**  Add fgIsWmmAssoc flag for TC assignment
**  \main\maintrunk.MT6620WiFiDriver_Prj\4 2009-03-19 18:32:51 GMT mtk01084
**  update for basic power management functions
**  \main\maintrunk.MT6620WiFiDriver_Prj\3 2009-03-18 20:51:52 GMT mtk01426
**  Add #if CFG_SDIO_RX_ENHANCE related data structure
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-03-10 20:16:17 GMT mtk01426
**  Init for develop
**
*/

#ifndef _ADAPTER_H
#define _ADAPTER_H

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

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
typedef struct _ENHANCE_MODE_DATA_STRUCT_T SDIO_CTRL_T, *P_SDIO_CTRL_T;

typedef struct _WLAN_INFO_T {
    PARAM_BSSID_EX_T                rCurrBssId;

    // Scan Result
    PARAM_BSSID_EX_T                arScanResult[CFG_MAX_NUM_BSS_LIST];
    PUINT_8                         apucScanResultIEs[CFG_MAX_NUM_BSS_LIST];
    UINT_32                         u4ScanResultNum;

    // IE pool for Scanning Result
    UINT_8                          aucScanIEBuf[CFG_MAX_COMMON_IE_BUF_LEN];
    UINT_32                         u4ScanIEBufferUsage;

    OS_SYSTIME                      u4SysTime;

    // connection parameter (for Ad-Hoc)
    UINT_16                     u2BeaconPeriod;
    UINT_16                     u2AtimWindow;

    PARAM_RATES                 eDesiredRates;
    CMD_LINK_ATTRIB             eLinkAttr;
//    CMD_PS_PROFILE_T         ePowerSaveMode;
    CMD_PS_PROFILE_T         arPowerSaveMode[NETWORK_TYPE_INDEX_NUM];

    // trigger parameter
    ENUM_RSSI_TRIGGER_TYPE      eRssiTriggerType;
    PARAM_RSSI                  rRssiTriggerValue;

    // Privacy Filter
    ENUM_PARAM_PRIVACY_FILTER_T ePrivacyFilter;

    // RTS Threshold
    PARAM_RTS_THRESHOLD         eRtsThreshold;

    // Network Type
    UINT_8                      ucNetworkType;

    // Network Type In Use
    UINT_8                      ucNetworkTypeInUse;

} WLAN_INFO_T, *P_WLAN_INFO_T;

/* Session for CONNECTION SETTINGS */
typedef struct _CONNECTION_SETTINGS_T {

    UINT_8                          aucMacAddress[MAC_ADDR_LEN];

    UINT_8                          ucDelayTimeOfDisconnectEvent;

    BOOLEAN                         fgIsConnByBssidIssued;
    UINT_8                          aucBSSID[MAC_ADDR_LEN];

    BOOLEAN                         fgIsConnReqIssued;
    UINT_8                          ucSSIDLen;
    UINT_8                          aucSSID[ELEM_MAX_LEN_SSID];

    ENUM_PARAM_OP_MODE_T            eOPMode;

    ENUM_PARAM_CONNECTION_POLICY_T  eConnectionPolicy;

    ENUM_PARAM_AD_HOC_MODE_T        eAdHocMode;

    ENUM_PARAM_AUTH_MODE_T          eAuthMode;

    ENUM_PARAM_ENCRYPTION_STATUS_T  eEncStatus;

    BOOLEAN                         fgIsScanReqIssued;


    /* MIB attributes */
    UINT_16                         u2BeaconPeriod;

    UINT_16                         u2RTSThreshold; /* User desired setting */

    UINT_16                         u2DesiredNonHTRateSet; /* User desired setting */

    UINT_8                          ucAdHocChannelNum; /* For AdHoc */

    ENUM_BAND_T                     eAdHocBand; /* For AdHoc */

    UINT_32                         u4FreqInKHz; /* Center frequency */

    /* ATIM windows using for IBSS power saving function */
    UINT_16                         u2AtimWindow;

    /* Features */
    BOOLEAN                         fgIsEnableRoaming;

    BOOLEAN                         fgIsAdHocQoSEnable;

    ENUM_PARAM_PHY_CONFIG_T         eDesiredPhyConfig;

    /* Used for AP mode for desired channel and bandwidth */
    UINT_16                         u2CountryCode;
    UINT_8                          uc2G4BandwidthMode; /* 20/40M or 20M only */
    UINT_8                          uc5GBandwidthMode;  /* 20/40M or 20M only */

    BOOLEAN                         fgTxShortGIDisabled;
    BOOLEAN                         fgRxShortGIDisabled;

#if CFG_SUPPORT_802_11D
    BOOLEAN                         fgMultiDomainCapabilityEnabled;
#endif /* CFG_SUPPORT_802_11D*/


#if 1 //CFG_SUPPORT_WAPI
    BOOL                            fgWapiMode;
    UINT_32                         u4WapiSelectedGroupCipher;
    UINT_32                         u4WapiSelectedPairwiseCipher;
    UINT_32                         u4WapiSelectedAKMSuite;
#endif

    /* CR1486, CR1640 */
    /* for WPS, disable the privacy check for AP selection policy */
    BOOLEAN                         fgPrivacyCheckDisable;

    /* b0~3: trigger-en AC0~3. b4~7: delivery-en AC0~3 */
    UINT_8                          bmfgApsdEnAc;

} CONNECTION_SETTINGS_T, *P_CONNECTION_SETTINGS_T;

struct _BSS_INFO_T {

    ENUM_PARAM_MEDIA_STATE_T  eConnectionState;           /* Connected Flag used in AIS_NORMAL_TR */
    ENUM_PARAM_MEDIA_STATE_T  eConnectionStateIndicated;  /* The Media State that report to HOST */

    ENUM_OP_MODE_T          eCurrentOPMode;             /* Current Operation Mode - Infra/IBSS */
#if CFG_ENABLE_WIFI_DIRECT
    ENUM_OP_MODE_T          eIntendOPMode;
#endif

    BOOLEAN                 fgIsNetActive;              /* TRUE if this network has been activated */

    UINT_8                  ucNetTypeIndex;             /* ENUM_NETWORK_TYPE_INDEX_T */

    UINT_8                  ucReasonOfDisconnect;       /* Used by media state indication */

    UINT_8                  ucSSIDLen;                  /* Length of SSID */

#if CFG_ENABLE_WIFI_DIRECT
    ENUM_HIDDEN_SSID_TYPE_T eHiddenSsidType;            /* For Hidden SSID usage. */
#endif

    UINT_8                  aucSSID[ELEM_MAX_LEN_SSID]; /* SSID used in this BSS */

    UINT_8                  aucBSSID[MAC_ADDR_LEN];     /* The BSSID of the associated BSS */

    UINT_8                  aucOwnMacAddr[MAC_ADDR_LEN];/* Owned MAC Address used in this BSS */

    P_STA_RECORD_T          prStaRecOfAP;               /* For Infra Mode, and valid only if
                                                         * eConnectionState == MEDIA_STATE_CONNECTED
                                                         */
    LINK_T                  rStaRecOfClientList;        /* For IBSS/AP Mode, all known STAs in current BSS */

    UINT_16                 u2CapInfo;                  /* Change Detection */

    UINT_16                 u2BeaconInterval;           /* The Beacon Interval of this BSS */


    UINT_16                 u2ATIMWindow;               /* For IBSS Mode */

    UINT_16                 u2AssocId;                  /* For Infra Mode, it is the Assoc ID assigned by AP.
                                                         */


    UINT_8                  ucDTIMPeriod;               /* For Infra/AP Mode */

    UINT_8                  ucDTIMCount;                /* For AP Mode, it is the DTIM value we should carried in
                                                         * the Beacon of next TBTT.
                                                         */

    UINT_8                  ucPhyTypeSet;               /* Available PHY Type Set of this peer
                                                         * (This is deduced from received BSS_DESC_T)
                                                         */

    UINT_8                  ucNonHTBasicPhyType;        /* The Basic PHY Type Index, used to setup Phy Capability */

    UINT_8                  ucConfigAdHocAPMode;        /* The configuration of AdHoc/AP Mode. e.g. 11g or 11b */

    UINT_8                  ucBeaconTimeoutCount;       /* For Infra/AP Mode, it is a threshold of Beacon Lost Count to
                                                           confirm connection was lost */

    BOOLEAN                 fgHoldSameBssidForIBSS;     /* For IBSS Mode, to keep use same BSSID to extend the life cycle of an IBSS */

    BOOLEAN                 fgIsBeaconActivated;        /* For AP/IBSS Mode, it is used to indicate that Beacon is sending */

    P_MSDU_INFO_T           prBeacon;                   /* For AP/IBSS Mode - Beacon Frame */

    BOOLEAN                 fgIsIBSSMaster;             /* For IBSS Mode - To indicate that we can reply ProbeResp Frame.
                                                           In current TBTT interval */

    BOOLEAN                 fgIsShortPreambleAllowed;   /* From Capability Info. of AssocResp Frame AND of Beacon/ProbeResp Frame */
    BOOLEAN                 fgUseShortPreamble;         /* Short Preamble is enabled in current BSS. */
    BOOLEAN                 fgUseShortSlotTime;         /* Short Slot Time is enabled in current BSS. */

    UINT_16                 u2OperationalRateSet;       /* Operational Rate Set of current BSS */
    UINT_16                 u2BSSBasicRateSet;          /* Basic Rate Set of current BSS */


    UINT_8                  ucAllSupportedRatesLen;     /* Used for composing Beacon Frame in AdHoc or AP Mode */
    UINT_8                  aucAllSupportedRates[RATE_NUM];

    UINT_8                  ucAssocClientCnt;           /* TODO(Kevin): Number of associated clients */

    BOOLEAN                 fgIsProtection;
    BOOLEAN                 fgIsQBSS; /* fgIsWmmBSS; */ /* For Infra/AP/IBSS Mode, it is used to indicate if we support WMM in
                                                         * current BSS. */
    BOOLEAN                 fgIsNetAbsent;              /* TRUE: BSS is absent, FALSE: BSS is present */

    UINT_32                 u4RsnSelectedGroupCipher;
    UINT_32                 u4RsnSelectedPairwiseCipher;
    UINT_32                 u4RsnSelectedAKMSuite;
	UINT_16                 u2RsnSelectedCapInfo;

    /*------------------------------------------------------------------------*/
    /* Power Management related information                                   */
    /*------------------------------------------------------------------------*/
    PM_PROFILE_SETUP_INFO_T rPmProfSetupInfo;


    /*------------------------------------------------------------------------*/
    /* WMM/QoS related information                                            */
    /*------------------------------------------------------------------------*/
    UINT_8                  ucWmmParamSetCount;         /* Used to detect the change of EDCA parameters. For AP mode, the value is used in WMM IE */

    AC_QUE_PARMS_T          arACQueParms[WMM_AC_INDEX_NUM];

    UINT_8                  aucCWminLog2ForBcast[WMM_AC_INDEX_NUM];        /* For AP mode, broadcast the CWminLog2 */
    UINT_8                  aucCWmaxLog2ForBcast[WMM_AC_INDEX_NUM];        /* For AP mode, broadcast the CWmaxLog2 */
    AC_QUE_PARMS_T          arACQueParmsForBcast[WMM_AC_INDEX_NUM];        /* For AP mode, broadcast the value */

    /*------------------------------------------------------------------------*/
    /* 802.11n HT operation IE when (prStaRec->ucPhyTypeSet & PHY_TYPE_BIT_HT)*/
    /* is true. They have the same definition with fields of                  */
    /* information element (CM)                                               */
    /*------------------------------------------------------------------------*/
    ENUM_BAND_T             eBand;
    UINT_8                  ucPrimaryChannel;
    UINT_8                  ucHtOpInfo1;
    UINT_16                 u2HtOpInfo2;
    UINT_16                 u2HtOpInfo3;

    /*------------------------------------------------------------------------*/
    /* Required protection modes (CM)                                         */
    /*------------------------------------------------------------------------*/
    BOOLEAN                 fgErpProtectMode;
    ENUM_HT_PROTECT_MODE_T  eHtProtectMode;
    ENUM_GF_MODE_T          eGfOperationMode;
    ENUM_RIFS_MODE_T        eRifsOperationMode;

    BOOLEAN                 fgObssErpProtectMode;       /* GO only */
    ENUM_HT_PROTECT_MODE_T  eObssHtProtectMode;         /* GO only */
    ENUM_GF_MODE_T          eObssGfOperationMode;       /* GO only */
    BOOLEAN                 fgObssRifsOperationMode;    /* GO only */

    /*------------------------------------------------------------------------*/
    /* OBSS to decide if 20/40M bandwidth is permitted.                       */
    /* The first member indicates the following channel list length.          */
    /*------------------------------------------------------------------------*/
    BOOLEAN                 fgAssoc40mBwAllowed;
    BOOLEAN                 fg40mBwAllowed;
    ENUM_CHNL_EXT_T         eBssSCO;    /* Real setting for HW
                                         * 20/40M AP mode will always set 40M,
                                         * but its OP IE can be changed.
                                         */
    UINT_8                  auc2G_20mReqChnlList[CHNL_LIST_SZ_2G + 1];
    UINT_8                  auc2G_NonHtChnlList[CHNL_LIST_SZ_2G + 1];
    UINT_8                  auc2G_PriChnlList[CHNL_LIST_SZ_2G + 1];
    UINT_8                  auc2G_SecChnlList[CHNL_LIST_SZ_2G + 1];

    UINT_8                  auc5G_20mReqChnlList[CHNL_LIST_SZ_5G + 1];
    UINT_8                  auc5G_NonHtChnlList[CHNL_LIST_SZ_5G + 1];
    UINT_8                  auc5G_PriChnlList[CHNL_LIST_SZ_5G + 1];
    UINT_8                  auc5G_SecChnlList[CHNL_LIST_SZ_5G + 1];

    TIMER_T                 rObssScanTimer;
    UINT_16                 u2ObssScanInterval;     /* in unit of sec */

    BOOLEAN                 fgObssActionForcedTo20M;    /* GO only */
    BOOLEAN                 fgObssBeaconForcedTo20M;    /* GO only */

    /*------------------------------------------------------------------------*/
    /* HW Related Fields (Kevin)                                              */
    /*------------------------------------------------------------------------*/
    UINT_8                  ucHwDefaultFixedRateCode;   /* The default rate code copied to MAC TX Desc */
    UINT_16                 u2HwLPWakeupGuardTimeUsec;


    UINT_8                  ucBssFreeQuota;              /* The value is updated from FW  */
};


struct _AIS_SPECIFIC_BSS_INFO_T {
    UINT_8                  ucRoamingAuthTypes;         /* This value indicate the roaming type used in AIS_JOIN */

    BOOLEAN                 fgIsIBSSActive;

    /*! \brief Global flag to let arbiter stay at standby and not connect to any network */
    BOOLEAN                 fgCounterMeasure;
    UINT_8                  ucWEPDefaultKeyID;
    BOOLEAN                 fgTransmitKeyExist; /* Legacy wep Transmit key exist or not */

    /* While Do CounterMeasure procedure, check the EAPoL Error report have send out */
    BOOLEAN                 fgCheckEAPoLTxDone;

    UINT_32                 u4RsnaLastMICFailTime;

    /* Stored the current bss wpa rsn cap filed, used for roaming policy */
    //UINT_16                 u2RsnCap;
    TIMER_T                 rPreauthenticationTimer;

    /* By the flow chart of 802.11i,
               wait 60 sec before associating to same AP
               or roaming to a new AP
               or sending data in IBSS,
               keep a timer for handle the 60 sec counterMeasure */
    TIMER_T                 rRsnaBlockTrafficTimer;
    TIMER_T                 rRsnaEAPoLReportTimeoutTimer;

    /* For Keep the Tx/Rx Mic key for TKIP SW Calculate Mic */
    /* This is only one for AIS/AP */
    UINT_8                  aucTxMicKey[8];
    UINT_8                  aucRxMicKey[8];

    /* Buffer for WPA2 PMKID */
    /* The PMKID cache lifetime is expire by media_disconnect_indication */
    UINT_32                 u4PmkidCandicateCount;
    PMKID_CANDICATE_T       arPmkidCandicate[CFG_MAX_PMKID_CACHE];
    UINT_32                 u4PmkidCacheCount;
    PMKID_ENTRY_T           arPmkidCache[CFG_MAX_PMKID_CACHE];
    BOOLEAN                 fgIndicatePMKID;
#if CFG_SUPPORT_802_11W
    BOOLEAN                 fgMgmtProtection;
    UINT_32                 u4SaQueryStart;
    UINT_32                 u4SaQueryCount;
    UINT_8                  ucSaQueryTimedOut;
    PUINT_8                 pucSaQueryTransId;
    TIMER_T                 rSaQueryTimer;
    BOOLEAN                 fgBipKeyInstalled;
#endif
};

struct _BOW_SPECIFIC_BSS_INFO_T {
    UINT_16                 u2Reserved; /* Reserved for Data Type Check */
};

#if CFG_SLT_SUPPORT
typedef struct _SLT_INFO_T {

    P_BSS_DESC_T prPseudoBssDesc;
    UINT_16 u2SiteID;
    UINT_8 ucChannel2G4;
    UINT_8 ucChannel5G;
    BOOLEAN fgIsDUT;
    UINT_32 u4BeaconReceiveCnt;
    /////////Deprecated/////////
    P_STA_RECORD_T prPseudoStaRec;
} SLT_INFO_T, *P_SLT_INFO_T;
#endif


/* Major member variables for WiFi FW operation.
   Variables within this region will be ready for access after WIFI function is enabled.
*/
typedef struct _WIFI_VAR_T {
    BOOLEAN                 fgIsRadioOff;

    BOOLEAN                 fgIsEnterD3ReqIssued;

    BOOLEAN                 fgDebugCmdResp;

    CONNECTION_SETTINGS_T   rConnSettings;

    SCAN_INFO_T             rScanInfo;

#if CFG_SUPPORT_ROAMING
    ROAMING_INFO_T          rRoamingInfo;
#endif /* CFG_SUPPORT_ROAMING */

    AIS_FSM_INFO_T          rAisFsmInfo;

    ENUM_PWR_STATE_T        aePwrState[NETWORK_TYPE_INDEX_NUM];

    BSS_INFO_T              arBssInfo[NETWORK_TYPE_INDEX_NUM];

    AIS_SPECIFIC_BSS_INFO_T rAisSpecificBssInfo;

#if CFG_ENABLE_WIFI_DIRECT
    P_P2P_CONNECTION_SETTINGS_T prP2PConnSettings;

    P_P2P_SPECIFIC_BSS_INFO_T prP2pSpecificBssInfo;

    P_P2P_FSM_INFO_T prP2pFsmInfo;
#endif /* CFG_ENABLE_WIFI_DIRECT */

#if CFG_ENABLE_BT_OVER_WIFI
    BOW_SPECIFIC_BSS_INFO_T rBowSpecificBssInfo;
    BOW_FSM_INFO_T rBowFsmInfo;
#endif /* CFG_ENABLE_BT_OVER_WIFI */

    DEAUTH_INFO_T           arDeauthInfo[MAX_DEAUTH_INFO_COUNT];

    /* Current Wi-Fi Settings and Flags */
    UINT_8                  aucPermanentAddress[MAC_ADDR_LEN];
    UINT_8                  aucMacAddress[MAC_ADDR_LEN];
    UINT_8                  aucDeviceAddress[MAC_ADDR_LEN];
    UINT_8                  aucInterfaceAddress[MAC_ADDR_LEN];

    UINT_8                  ucAvailablePhyTypeSet;

    ENUM_PHY_TYPE_INDEX_T   eNonHTBasicPhyType2G4; /* Basic Phy Type used by SCN according
                                                    * to the set of Available PHY Types
                                                    */

    ENUM_PARAM_PREAMBLE_TYPE_T  ePreambleType;
    ENUM_REGISTRY_FIXED_RATE_T  eRateSetting;

    BOOLEAN                 fgIsShortSlotTimeOptionEnable;
                            /* User desired setting, but will honor the capability of AP */

    BOOLEAN                 fgEnableJoinToHiddenSSID;
    BOOLEAN                 fgSupportWZCDisassociation;

    BOOLEAN                 fgSupportQoS;
    BOOLEAN                 fgSupportAmpduTx;
    BOOLEAN                 fgSupportAmpduRx;
    BOOLEAN                 fgSupportTspec;
    BOOLEAN                 fgSupportUAPSD;
    BOOLEAN                 fgSupportULPSMP;

#if CFG_SLT_SUPPORT
    SLT_INFO_T      rSltInfo;
#endif

} WIFI_VAR_T, *P_WIFI_VAR_T;/* end of _WIFI_VAR_T */

/* cnm_timer module */
typedef struct {
    LINK_T              rLinkHead;
    OS_SYSTIME          rNextExpiredSysTime;
    KAL_WAKE_LOCK_T     rWakeLock;
    BOOLEAN             fgWakeLocked;
} ROOT_TIMER, *P_ROOT_TIMER;


/* FW/DRV/NVRAM version information */
typedef struct {

    /* NVRAM or Registry */
    UINT_16     u2Part1CfgOwnVersion;
    UINT_16     u2Part1CfgPeerVersion;
    UINT_16     u2Part2CfgOwnVersion;
    UINT_16     u2Part2CfgPeerVersion;

    /* Firmware */
    UINT_16     u2FwProductID;
    UINT_16     u2FwOwnVersion;
    UINT_16     u2FwPeerVersion;

} WIFI_VER_INFO_T, *P_WIFI_VER_INFO_T;


#if CFG_ENABLE_WIFI_DIRECT
/*
* p2p function pointer structure
*/

typedef struct _P2P_FUNCTION_LINKER {
    P2P_REMOVE                                  prP2pRemove;
//    NIC_P2P_MEDIA_STATE_CHANGE                  prNicP2pMediaStateChange;
//    SCAN_UPDATE_P2P_DEVICE_DESC                 prScanUpdateP2pDeviceDesc;
//    P2P_FSM_RUN_EVENT_RX_PROBE_RESPONSE_FRAME   prP2pFsmRunEventRxProbeResponseFrame;
    P2P_GENERATE_P2P_IE              prP2pGenerateWSC_IEForBeacon;
//    P2P_CALCULATE_WSC_IE_LEN_FOR_PROBE_RSP      prP2pCalculateWSC_IELenForProbeRsp;
//    P2P_GENERATE_WSC_IE_FOR_PROBE_RSP           prP2pGenerateWSC_IEForProbeRsp;
//    SCAN_REMOVE_P2P_BSS_DESC                    prScanRemoveP2pBssDesc;
//    P2P_HANDLE_SEC_CHECK_RSP                    prP2pHandleSecCheckRsp;
    P2P_NET_REGISTER                 prP2pNetRegister;
    P2P_NET_UNREGISTER               prP2pNetUnregister;
    P2P_CALCULATE_P2P_IE_LEN         prP2pCalculateP2p_IELenForAssocReq;   /* All IEs generated from supplicant. */
    P2P_GENERATE_P2P_IE              prP2pGenerateP2p_IEForAssocReq;           /* All IEs generated from supplicant. */
} P2P_FUNCTION_LINKER, *P_P2P_FUNCTION_LINKER;


#endif

/*
 * Major ADAPTER structure
 * Major data structure for driver operation
 */
struct _ADAPTER_T {
    UINT_8                  ucRevID;

    UINT_16                 u2NicOpChnlNum;

    BOOLEAN                 fgIsEnableWMM;
    BOOLEAN                 fgIsWmmAssoc; /* This flag is used to indicate that WMM is enable in current BSS */

    UINT_32                 u4OsPacketFilter;     // packet filter used by OS


#if CFG_TCP_IP_CHKSUM_OFFLOAD
    UINT_32                 u4CSUMFlags;
#endif /* CFG_TCP_IP_CHKSUM_OFFLOAD */


    ENUM_BAND_T             aePreferBand[NETWORK_TYPE_INDEX_NUM];

    /* ADAPTER flags */
    UINT_32                 u4Flags;
    UINT_32                 u4HwFlags;

    BOOLEAN                 fgIsRadioOff;

    BOOLEAN                 fgIsEnterD3ReqIssued;

    UINT_8                  aucMacAddress[MAC_ADDR_LEN];

    ENUM_PHY_TYPE_INDEX_T   eCurrentPhyType; /* Current selection basing on the set of Available PHY Types */

#if CFG_COALESCING_BUFFER_SIZE || CFG_SDIO_RX_AGG
    UINT_32                 u4CoalescingBufCachedSize;
    PUINT_8                 pucCoalescingBufCached;
#endif /* CFG_COALESCING_BUFFER_SIZE */

    /* Buffer for CMD_INFO_T, Mgt packet and mailbox message */
    BUF_INFO_T              rMgtBufInfo;
    BUF_INFO_T              rMsgBufInfo;
    PUINT_8                 pucMgtBufCached;
    UINT_32                 u4MgtBufCachedSize;
    UINT_8                  aucMsgBuf[MSG_BUFFER_SIZE];
#if CFG_DBG_MGT_BUF
    UINT_32                 u4MemAllocDynamicCount;     /* Debug only */
    UINT_32                 u4MemFreeDynamicCount;      /* Debug only */
#endif

    STA_RECORD_T            arStaRec[CFG_STA_REC_NUM];

    /* Element for TX PATH */
    TX_CTRL_T               rTxCtrl;
    QUE_T                   rFreeCmdList;
    CMD_INFO_T              arHifCmdDesc[CFG_TX_MAX_CMD_PKT_NUM];

    /* Element for RX PATH */
    RX_CTRL_T               rRxCtrl;

    P_SDIO_CTRL_T           prSDIOCtrl;

#if (MT6620_E1_ASIC_HIFSYS_WORKAROUND == 1)
    /* Element for MT6620 E1 HIFSYS workaround */
    BOOLEAN                 fgIsClockGatingEnabled;
#endif

    /* Buffer for Authentication Event */
    /* <Todo> Move to glue layer and refine the kal function */
    /* Reference to rsnGeneratePmkidIndication function at rsn.c */
    UINT_8                  aucIndicationEventBuffer[(CFG_MAX_PMKID_CACHE * 20)  + 8 ];

    UINT_32                 u4IntStatus;

    ENUM_ACPI_STATE_T       rAcpiState;

    BOOLEAN                 fgIsIntEnable;
    BOOLEAN                 fgIsIntEnableWithLPOwnSet;

    BOOLEAN                 fgIsFwOwn;
    BOOLEAN                 fgWiFiInSleepyState;

    UINT_32                 u4PwrCtrlBlockCnt;

    QUE_T                   rPendingCmdQueue;

    P_GLUE_INFO_T           prGlueInfo;

    UINT_8                  ucCmdSeqNum;
    UINT_8                  ucTxSeqNum;

#if 1//CFG_SUPPORT_WAPI
    BOOLEAN                 fgUseWapi;
#endif

    /* RF Test flags */
    BOOLEAN         fgTestMode;

    /* WLAN Info for DRIVER_CORE OID query */
    WLAN_INFO_T     rWlanInfo;

#if CFG_ENABLE_WIFI_DIRECT
    BOOLEAN             fgIsP2PRegistered;
    ENUM_NET_REG_STATE_T rP2PNetRegState;
    BOOLEAN             fgIsWlanLaunched;
    P_P2P_INFO_T        prP2pInfo;
#if CFG_SUPPORT_P2P_RSSI_QUERY
    OS_SYSTIME          rP2pLinkQualityUpdateTime;
    BOOLEAN             fgIsP2pLinkQualityValid;
    EVENT_LINK_QUALITY  rP2pLinkQuality;
#endif
#endif

    /* Online Scan Option */
    BOOLEAN         fgEnOnlineScan;

    /* Online Scan Option */
    BOOLEAN         fgDisBcnLostDetection;

    /* MAC address */
    PARAM_MAC_ADDRESS rMyMacAddr;

    /* Wake-up Event for WOL */
    UINT_32         u4WakeupEventEnable;

    /* Event Buffering */
    EVENT_STATISTICS    rStatStruct;
    OS_SYSTIME          rStatUpdateTime;
    BOOLEAN             fgIsStatValid;

    EVENT_LINK_QUALITY  rLinkQuality;
    OS_SYSTIME          rLinkQualityUpdateTime;
    BOOLEAN             fgIsLinkQualityValid;
    OS_SYSTIME          rLinkRateUpdateTime;
    BOOLEAN             fgIsLinkRateValid;

    /* WIFI_VAR_T */
    WIFI_VAR_T          rWifiVar;

    /* MTK WLAN NIC driver IEEE 802.11 MIB */
    IEEE_802_11_MIB_T   rMib;

    /* Mailboxs for inter-module communication */
    MBOX_T arMbox[MBOX_ID_TOTAL_NUM];

    /* Timers for OID Pending Handling */
    TIMER_T             rOidTimeoutTimer;

    /* Root Timer for cnm_timer module */
    ROOT_TIMER          rRootTimer;

    /* RLM maintenance */
    ENUM_CHNL_EXT_T             eRfSco;
    ENUM_SYS_PROTECT_MODE_T     eSysProtectMode;
    ENUM_GF_MODE_T              eSysHtGfMode;
    ENUM_RIFS_MODE_T            eSysTxRifsMode;
    ENUM_SYS_PCO_PHASE_T        eSysPcoPhase;

    P_DOMAIN_INFO_ENTRY         prDomainInfo;

    /* QM */
    QUE_MGT_T                   rQM;

    CNM_INFO_T                  rCnmInfo;

    UINT_32 u4PowerMode;

    UINT_32 u4CtiaPowerMode;
    BOOLEAN fgEnCtiaPowerMode;

    UINT_32 fgEnArpFilter;

    UINT_32 u4UapsdAcBmp;

    UINT_32 u4MaxSpLen;

    UINT_32 u4PsCurrentMeasureEn;

    /* Version Information */
    WIFI_VER_INFO_T             rVerInfo;

    /* 5GHz support (from F/W) */
    BOOLEAN fgIsHw5GBandDisabled;
    BOOLEAN fgEnable5GBand;
    BOOLEAN fgIsEepromUsed;
    BOOLEAN fgIsEfuseValid;
    BOOLEAN fgIsEmbbededMacAddrValid;

    /* Packet Forwarding Tracking */
    INT_32  i4PendingFwdFrameCount;

#if CFG_SUPPORT_RDD_TEST_MODE
    UINT_8  ucRddStatus;
#endif

    BOOL fgDisStaAgingTimeoutDetection;

};/* end of _ADAPTER_T */

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
/*----------------------------------------------------------------------------*/
/* Macros for BSS_INFO_T - Flag of Net Active                                 */
/*----------------------------------------------------------------------------*/
#define IS_NET_ACTIVE(_prAdapter, _NetTypeIndex) \
                (_prAdapter->rWifiVar.arBssInfo[(_NetTypeIndex)].fgIsNetActive)
#define IS_BSS_ACTIVE(_prBssInfo)     ((_prBssInfo)->fgIsNetActive)

#define IS_AIS_ACTIVE(_prAdapter)     IS_NET_ACTIVE(_prAdapter, NETWORK_TYPE_AIS_INDEX)
#define IS_P2P_ACTIVE(_prAdapter)     IS_NET_ACTIVE(_prAdapter, NETWORK_TYPE_P2P_INDEX)
#define IS_BOW_ACTIVE(_prAdapter)     IS_NET_ACTIVE(_prAdapter, NETWORK_TYPE_BOW_INDEX)

#define SET_NET_ACTIVE(_prAdapter, _NetTypeIndex) \
                {_prAdapter->rWifiVar.arBssInfo[(_NetTypeIndex)].fgIsNetActive = TRUE;}

#define UNSET_NET_ACTIVE(_prAdapter, _NetTypeIndex) \
                {_prAdapter->rWifiVar.arBssInfo[(_NetTypeIndex)].fgIsNetActive = FALSE;}

#define BSS_INFO_INIT(_prAdapter, _NetTypeIndex) \
                {   UINT_8 _aucZeroMacAddr[] = NULL_MAC_ADDR; \
                    P_BSS_INFO_T _prBssInfo = &(_prAdapter->rWifiVar.arBssInfo[(_NetTypeIndex)]); \
                    \
                    _prBssInfo->eConnectionState = PARAM_MEDIA_STATE_DISCONNECTED; \
                    _prBssInfo->eConnectionStateIndicated = PARAM_MEDIA_STATE_DISCONNECTED; \
                    _prBssInfo->eCurrentOPMode = OP_MODE_INFRASTRUCTURE; \
                    _prBssInfo->fgIsNetActive = FALSE; \
                    _prBssInfo->ucNetTypeIndex = (_NetTypeIndex); \
                    _prBssInfo->ucReasonOfDisconnect = DISCONNECT_REASON_CODE_RESERVED; \
                    COPY_MAC_ADDR(_prBssInfo->aucBSSID, _aucZeroMacAddr); \
                    LINK_INITIALIZE(&_prBssInfo->rStaRecOfClientList); \
                    _prBssInfo->fgIsBeaconActivated = FALSE; \
                    _prBssInfo->ucHwDefaultFixedRateCode = RATE_CCK_1M_LONG; \
                    _prBssInfo->fgIsNetAbsent = FALSE; \
                }

#if CFG_ENABLE_BT_OVER_WIFI
#define BOW_BSS_INFO_INIT(_prAdapter, _NetTypeIndex) \
                {  \
                    P_BSS_INFO_T _prBssInfo = &(_prAdapter->rWifiVar.arBssInfo[(_NetTypeIndex)]); \
                    \
                    _prBssInfo->eConnectionState = PARAM_MEDIA_STATE_DISCONNECTED; \
                    _prBssInfo->eConnectionStateIndicated = PARAM_MEDIA_STATE_DISCONNECTED; \
                    _prBssInfo->eCurrentOPMode = OP_MODE_BOW; \
                    _prBssInfo->ucNetTypeIndex = (_NetTypeIndex); \
                    _prBssInfo->ucReasonOfDisconnect = DISCONNECT_REASON_CODE_RESERVED; \
                    LINK_INITIALIZE(&_prBssInfo->rStaRecOfClientList); \
                    _prBssInfo->fgIsBeaconActivated = TRUE; \
                    _prBssInfo->ucHwDefaultFixedRateCode = RATE_CCK_1M_LONG; \
                    _prBssInfo->fgIsNetAbsent = FALSE; \
                }
#endif

/*----------------------------------------------------------------------------*/
/* Macros for Power State                                                     */
/*----------------------------------------------------------------------------*/
#define SET_NET_PWR_STATE_IDLE(_prAdapter, _NetTypeIndex) \
                {_prAdapter->rWifiVar.aePwrState[(_NetTypeIndex)] = PWR_STATE_IDLE;}

#define SET_NET_PWR_STATE_ACTIVE(_prAdapter, _NetTypeIndex) \
                {_prAdapter->rWifiVar.aePwrState[(_NetTypeIndex)] = PWR_STATE_ACTIVE;}

#define SET_NET_PWR_STATE_PS(_prAdapter, _NetTypeIndex) \
                {_prAdapter->rWifiVar.aePwrState[(_NetTypeIndex)] = PWR_STATE_PS;}

#define IS_NET_PWR_STATE_ACTIVE(_prAdapter, _NetTypeIndex) \
                (_prAdapter->rWifiVar.aePwrState[(_NetTypeIndex)] == PWR_STATE_ACTIVE)

#define IS_NET_PWR_STATE_IDLE(_prAdapter, _NetTypeIndex) \
                (_prAdapter->rWifiVar.aePwrState[(_NetTypeIndex)] == PWR_STATE_IDLE)

#define IS_SCN_PWR_STATE_ACTIVE(_prAdapter) \
                (_prAdapter->rWifiVar.rScanInfo.eScanPwrState == SCAN_PWR_STATE_ACTIVE)

#define IS_SCN_PWR_STATE_IDLE(_prAdapter) \
                (_prAdapter->rWifiVar.rScanInfo.eScanPwrState == SCAN_PWR_STATE_IDLE)

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/


/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _ADAPTER_H */


