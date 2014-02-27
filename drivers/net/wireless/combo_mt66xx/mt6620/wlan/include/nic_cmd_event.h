/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/include/nic_cmd_event.h#1 $
*/

/*! \file   "nic_cmd_event.h"
    \brief This file contains the declairation file of the WLAN OID processing routines
           of Windows driver for MediaTek Inc. 802.11 Wireless LAN Adapters.
*/



/*
** $Log: nic_cmd_event.h $
**
** 07 29 2013 yuche.tsai
** [ALPS00818788] [PreRC1][Mobile HotSpot]DUT should support hidden SSID broadcast for Mobile HotSpot
** Support hidden SSID for MT6630.
** FW Label: 20130729_ALPS_WIFI_FW_MT6620_V1_12
 *
 * 03 29 2012 eason.tsai
 * [WCXRP00001216] [MT6628 Wi-Fi][Driver]add conditional define
 * add conditional define.
 *
 * 03 04 2012 eason.tsai
 * NULL
 * modify the cal fail report code.
 *
 * 01 06 2012 wh.su
 * [WCXRP00001153] [MT6620 Wi-Fi][Driver] Adding the get_ch_list and set_tx_power proto type function
 * redefine the CMD_ID_SET_TXPWR_CTRL value.
 *
 * 01 05 2012 wh.su
 * [WCXRP00001153] [MT6620 Wi-Fi][Driver] Adding the get_ch_list and set_tx_power proto type function
 * Adding the related ioctl / wlan oid function to set the Tx power cfg.
 *
 * 11 30 2011 cm.chang
 * [WCXRP00001128] [MT5931 Wi-Fi][FW] Update BB/RF setting based on RF doc v0.7 for LGE spec
 * 1. Add a new CMD for driver to set device mode
 * 2. Update calibration parameters
 *
 * 11 19 2011 yuche.tsai
 * NULL
 * Update RSSI for P2P.
 *
 * 11 18 2011 yuche.tsai
 * NULL
 * CONFIG P2P support RSSI query, default turned off.
 *
 * 11 10 2011 eddie.chen
 * [WCXRP00001096] [MT6620 Wi-Fi][Driver/FW] Enhance the log function (xlog)
 * Add TX_DONE status detail information.
 *
 * 11 08 2011 tsaiyuan.hsu
 * [WCXRP00001083] [MT6620 Wi-Fi][DRV]] dump debug counter or frames when debugging is triggered
 * check if CFG_SUPPORT_SWCR is defined to aoid compiler error.
 *
 * 11 07 2011 tsaiyuan.hsu
 * [WCXRP00001083] [MT6620 Wi-Fi][DRV]] dump debug counter or frames when debugging is triggered
 * add debug counters and periodically dump counters for debugging.
 *
 * 10 26 2011 cp.wu
 * [WCXRP00001065] [MT6620 Wi-Fi][MT5931][FW][DRV] Adding parameter for controlling minimum channel dwell time for scanning
 * add interface for control minimum channel dwell time for scanning.
 *
 * 09 20 2011 cm.chang
 * [WCXRP00000997] [MT6620 Wi-Fi][Driver][FW] Handle change of BSS preamble type and slot time
 * New CMD definition about RLM parameters
 *
 * 08 31 2011 cm.chang
 * [WCXRP00000969] [MT6620 Wi-Fi][Driver][FW] Channel list for 5G band based on country code
 * .
 *
 * 08 25 2011 chinghwa.yu
 * [WCXRP00000612] [MT6620 Wi-Fi] [FW] CSD update SWRDD algorithm
 * Add DFS switch.
 *
 * 08 24 2011 chinghwa.yu
 * [WCXRP00000612] [MT6620 Wi-Fi] [FW] CSD update SWRDD algorithm
 * Update RDD test mode cases.
 *
 * 08 15 2011 cp.wu
 * [WCXRP00000851] [MT6628 Wi-Fi][Driver] Add HIFSYS related definition to driver source tree
 * add MT6628-specific definitions.
 *
 * 08 11 2011 cp.wu
 * [WCXRP00000830] [MT6620 Wi-Fi][Firmware] Use MDRDY counter to detect empty channel for shortening scan time
 * sparse channel detection:
 * driver: collect sparse channel information with scan-done event
 *
 * 08 09 2011 cp.wu
 * [WCXRP00000702] [MT5931][Driver] Modify initialization sequence for E1 ASIC[WCXRP00000913] [MT6620 Wi-Fi] create repository of source code dedicated for MT6620 E6 ASIC
 * add CCK-DSSS TX-PWR control field in NVRAM and CMD definition for MT5931-MP
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
 * 07 28 2011 chinghwa.yu
 * [WCXRP00000063] Update BCM CoEx design and settings
 * Add BWCS cmd and event.
 *
 * 07 22 2011 jeffrey.chang
 * [WCXRP00000864] [MT5931] Add command to adjust OSC stable time
 * add osc stable time command structure
 *
 * 07 22 2011 jeffrey.chang
 * [WCXRP00000864] [MT5931] Add command to adjust OSC stable time
 * modify driver to set OSC stable time after f/w download
 *
 * 07 18 2011 chinghwa.yu
 * [WCXRP00000063] Update BCM CoEx design and settings[WCXRP00000612] [MT6620 Wi-Fi] [FW] CSD update SWRDD algorithm
 * Add CMD/Event for RDD and BWCS.
 *
 * 07 18 2011 cp.wu
 * [WCXRP00000858] [MT5931][Driver][Firmware] Add support for scan to search for more than one SSID in a single scanning request
 * add framework in driver domain for supporting new SCAN_REQ_V2 for more than 1 SSID support as well as uProbeDelay in NDIS 6.x driver model
 *
 * 06 23 2011 cp.wu
 * [WCXRP00000812] [MT6620 Wi-Fi][Driver] not show NVRAM when there is no valid MAC address in NVRAM content
 * check with firmware for valid MAC address.
 *
 * 06 23 2011 cp.wu
 * [WCXRP00000798] [MT6620 Wi-Fi][Firmware] Follow-ups for WAPI frequency offset workaround in firmware SCN module
 * change parameter name from PeerAddr to BSSID
 *
 * 06 20 2011 cp.wu
 * [WCXRP00000798] [MT6620 Wi-Fi][Firmware] Follow-ups for WAPI frequency offset workaround in firmware SCN module
 * 1. specify target's BSSID when requesting channel privilege.
 * 2. pass BSSID information to firmware domain
 *
 * 06 09 2011 tsaiyuan.hsu
 * [WCXRP00000760] [MT5931 Wi-Fi][FW] Refine rxmHandleMacRxDone to reduce code size
 * move send_auth at rxmHandleMacRxDone in firmware to driver to reduce code size.
 *
 * 05 27 2011 cp.wu
 * [WCXRP00000749] [MT6620 Wi-Fi][Driver] Add band edge tx power control to Wi-Fi NVRAM
 * invoke CMD_ID_SET_EDGE_TXPWR_LIMIT when there is valid data exist in NVRAM content.
 *
 * 04 18 2011 terry.wu
 * [WCXRP00000660] [MT6620 Wi-Fi][Driver] Remove flag CFG_WIFI_DIRECT_MOVED
 * Remove flag CFG_WIFI_DIRECT_MOVED.
 *
 * 03 31 2011 chinglan.wang
 * [WCXRP00000613] [MT6620 Wi-Fi] [FW] [Driver] BssInfo can get the security mode which is WPA/WPA2/WAPI or not.
 * .
 *
 * 03 18 2011 cm.chang
 * [WCXRP00000576] [MT6620 Wi-Fi][Driver][FW] Remove P2P compile option in scan req/cancel command
 * As CR title
 *
 * 03 17 2011 yarco.yang
 * [WCXRP00000569] [MT6620 Wi-Fi][F/W][Driver] Set multicast address support current network usage
 * .
 *
 * 03 07 2011 wh.su
 * [WCXRP00000506] [MT6620 Wi-Fi][Driver][FW] Add Security check related code
 * rename the define to anti_pviracy.
 *
 * 03 05 2011 wh.su
 * [WCXRP00000506] [MT6620 Wi-Fi][Driver][FW] Add Security check related code
 * add the code to get the check rsponse and indicate to app.
 *
 * 03 02 2011 wh.su
 * [WCXRP00000506] [MT6620 Wi-Fi][Driver][FW] Add Security check related code
 * Add Security check related code.
 *
 * 03 02 2011 george.huang
 * [WCXRP00000504] [MT6620 Wi-Fi][FW] Support Sigma CAPI for power saving related command
 * Support UAPSD/OppPS/NoA parameter setting
 *
 * 02 16 2011 cm.chang
 * [WCXRP00000447] [MT6620 Wi-Fi][FW] Support new NVRAM update mechanism
 * .
 *
 * 02 10 2011 cp.wu
 * [WCXRP00000434] [MT6620 Wi-Fi][Driver] Obsolete unused event packet handlers
 * EVENT_ID_CONNECTION_STATUS has been obsoleted and no need to handle.
 *
 * 02 08 2011 eddie.chen
 * [WCXRP00000426] [MT6620 Wi-Fi][FW/Driver] Add STA aging timeout and defualtHwRatein AP mode
 * Add event STA agint timeout
 *
 * 01 27 2011 tsaiyuan.hsu
 * [WCXRP00000392] [MT6620 Wi-Fi][Driver] Add Roaming Support
 * add roaming fsm
 * 1. not support 11r, only use strength of signal to determine roaming.
 * 2. not enable CFG_SUPPORT_ROAMING until completion of full test.
 * 3. in 6620, adopt work-around to avoid sign extension problem of cck of hw
 * 4. assume that change of link quality in smooth way.
 *
 * 01 25 2011 yuche.tsai
 * [WCXRP00000352] [Volunteer Patch][MT6620][Driver] P2P Statsion Record Client List Issue
 * Update cmd format of BSS INFO, always sync OwnMac to FW no matter P2P is enabled or not..
 *
 * 01 20 2011 eddie.chen
 * [WCXRP00000374] [MT6620 Wi-Fi][DRV] SW debug control
 * Add Oid for sw control debug command
 *
 * 01 15 2011 puff.wen
 * NULL
 * Add Stress test
 *
 * 01 12 2011 cm.chang
 * [WCXRP00000354] [MT6620 Wi-Fi][Driver][FW] Follow NVRAM bandwidth setting
 * Sync HT operation element information from host to FW
 *
 * 01 12 2011 cm.chang
 * [WCXRP00000354] [MT6620 Wi-Fi][Driver][FW] Follow NVRAM bandwidth setting
 * User-defined bandwidth is for 2.4G and 5G individually
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
 * 12 23 2010 george.huang
 * [WCXRP00000152] [MT6620 Wi-Fi] AP mode power saving function
 * 1. update WMM IE parsing, with ASSOC REQ handling
 * 2. extend U-APSD parameter passing from driver to FW
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
 * 11 29 2010 cm.chang
 * [WCXRP00000210] [MT6620 Wi-Fi][Driver][FW] Set RCPI value in STA_REC for initial TX rate selection of auto-rate algorithm
 * Sync RCPI of STA_REC to FW as reference of initial TX rate
 *
 * 11 08 2010 cm.chang
 * [WCXRP00000169] [MT6620 Wi-Fi][Driver][FW] Remove unused CNM recover message ID
 * Remove CNM channel reover message ID
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
 * 10 25 2010 cp.wu
 * [WCXRP00000133] [MT6620 Wi-Fi] [FW][Driver] Change TX power offset band definition
 * follow-up for CMD_5G_PWR_OFFSET_T definition change
 *
 * 10 20 2010 cp.wu
 * [WCXRP00000117] [MT6620 Wi-Fi][Driver] Add logic for suspending driver when MT6620 is not responding anymore
 * use OID_CUSTOM_TEST_MODE as indication for driver reset
 * by dropping pending TX packets
 *
 * 10 20 2010 wh.su
 * [WCXRP00000124] [MT6620 Wi-Fi] [Driver] Support the dissolve P2P Group
 * Add the code to support disconnect p2p group
 *
 * 09 15 2010 cm.chang
 * NULL
 * Add new CMD for TX power, 5G power offset and power parameters
 *
 * 09 07 2010 yuche.tsai
 * NULL
 * Add a pointer in P2P SCAN RESULT structure. This pointer
 * is pointed to a IE buffer for this P2p device.
 *
 * 09 07 2010 wh.su
 * NULL
 * adding the code for beacon/probe req/ probe rsp wsc ie at p2p.
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 08 23 2010 chinghwa.yu
 * NULL
 * Update for BOW.
 *
 * 08 20 2010 cm.chang
 * NULL
 * Migrate RLM code to host from FW
 *
 * 08 16 2010 george.huang
 * NULL
 * add new CMD ID definition
 *
 * 08 16 2010 yuche.tsai
 * NULL
 * Add a field in BSS INFO cmd to change interface address for P2P. (switching between Device Addr & Interface Addr)
 *
 * 08 12 2010 yuche.tsai
 * NULL
 * Add interface address indication when indicate connection status.
 * It is requested by supplicant to do 4 way handshake.
 *
 * 08 07 2010 wh.su
 * NULL
 * adding the privacy related code for P2P network
 *
 * 08 05 2010 yuche.tsai
 * NULL
 * Change data structure for P2P Device scan result, all channel time for scan command.
 *
 * 08 04 2010 george.huang
 * NULL
 * handle change PS mode OID/ CMD
 *
 * 08 04 2010 yarco.yang
 * NULL
 * Add TX_AMPDU and ADDBA_REJECT command
 *
 * 08 03 2010 george.huang
 * NULL
 * handle event for updating NOA parameters indicated from FW
 *
 * 08 02 2010 george.huang
 * NULL
 * add WMM-PS test related OID/ CMD handlers
 *
 * 07 28 2010 cp.wu
 * NULL
 * sync. CMD_BSS_INFO structure change to CMD-EVENT v0.15.
 *
 * 07 26 2010 yuche.tsai
 *
 * Add P2P Device Found Event.
 * Channel extention option in scan abort command.
 *
 * 07 23 2010 cp.wu
 *
 * add AIS-FSM handling for beacon timeout event.
 *
 * 07 21 2010 yuche.tsai
 *
 * Add for P2P Scan Result Parsing & Saving.
 *
 * 07 20 2010 george.huang
 *
 * DWORD align for the CMD data structure
 *
 * 07 20 2010 cp.wu
 *
 * pass band information for scan in an efficient way by mapping ENUM_BAND_T into UINT_8..
 *
 * 07 19 2010 wh.su
 *
 * update for security supporting.
 *
 * 07 19 2010 cm.chang
 *
 * Set RLM parameters and enable CNM channel manager
 *
 * 07 16 2010 yarco.yang
 *
 * 1. Support BSS Absence/Presence Event
 * 2. Support STA change PS mode Event
 * 3. Support BMC forwarding for AP mode.
 *
 * 07 14 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * pass band with channel number information as scan parameter
 *
 * 07 14 2010 yarco.yang
 *
 * 1. Remove CFG_MQM_MIGRATION
 * 2. Add CMD_UPDATE_WMM_PARMS command
 *
 * 07 09 2010 cp.wu
 *
 * reorder members of CMD_SET_BSS_INFO.
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 07 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * update prStaRecOfAP with BSS-INFO.
 *
 * 07 07 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Support state of STA record change from 1 to 1
 *
 * 07 01 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Support sync command of STA_REC
 *
 * 07 01 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * implementation of DRV-SCN and related mailbox message handling.
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
 * 06 28 2010 george.huang
 * [WPD00001556]Basic power managemenet function
 * Create beacon update path, with expose bssUpdateBeaconContent()
 *
 * 06 22 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) add command warpper for STA-REC/BSS-INFO sync.
 * 2) enhance command packet sending procedure for non-oid part
 * 3) add command packet definitions for STA-REC/BSS-INFO sync.
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add BSS/STA_REC commands for integration.
 *
 * 06 21 2010 yarco.yang
 * [WPD00003837][MT6620]Data Path Refine
 * Add TX Done Event handle entry
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
 * correct OID_802_11_DISASSOCIATE handling.
 *
 * 05 17 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * 1) add timeout handler mechanism for pending command packets
 * 2) add p2p add/removal key
 *
 * 04 13 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support
 * add framework for BT-over-Wi-Fi support.
 *  *  *  *  *  *  *  *  *  * 1) prPendingCmdInfo is replaced by queue for multiple handler capability
 *  *  *  *  *  *  *  *  *  * 2) command sequence number is now increased atomically
 *  *  *  *  *  *  *  *  *  * 3) private data could be hold and taken use for other purpose
 *
 * 04 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * sync statistics data structure definition with firmware implementation
 *
 * 03 30 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * statistics information OIDs are now handled by querying from firmware domain
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
 *  * the frequency is used for adhoc connection only
 *  * 2) update with SD1 v0.9 CMD/EVENT documentation
 *
 * 03 24 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * initial import for Linux port
 *
 * 03 22 2010 cp.wu
 * [WPD00003824][MT6620 Wi-Fi][New Feature] Add support of large scan list
 * Implement feature needed by CR: WPD00003824: refining association command by pasting scanning result
 *
 * 03 19 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) add ACPI D0/D3 state switching support
 *  *  *  *  *  * 2) use more formal way to handle interrupt when the status is retrieved from enhanced RX response
 *
 * 03 15 2010 kevin.huang
 * [WPD00003820][MT6620 Wi-Fi] Modify the code for meet the WHQL test
 * Add event for activate STA_RECORD_T
 *
 * 03 03 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * implement custom OID: EEPROM read/write access
 *
 * 03 03 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * implement OID_802_3_MULTICAST_LIST oid handling
 *
 * 02 26 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * move EVENT_ID_ASSOC_INFO from nic_rx.c to gl_kal_ndis_51.c
 * 'cause it involves OS dependent data structure handling
 *
 * 02 25 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * send CMD_ID_INFRASTRUCTURE when handling OID_802_11_INFRASTRUCTURE_MODE set.
 *
 * 02 09 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1. Permanent and current MAC address are now retrieved by CMD/EVENT packets instead of hard-coded address
 *  *  *  *  * 2. follow MSDN defined behavior when associates to another AP
 *  *  *  *  * 3. for firmware download, packet size could be up to 2048 bytes
 *
 * 01 27 2010 wh.su
 * [WPD00003816][MT6620 Wi-Fi] Adding the security support
 * .
 *
 * 01 27 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1. eliminate improper variable in rHifInfo
 *  *  *  *  *  * 2. block TX/ordinary OID when RF test mode is engaged
 *  *  *  *  *  * 3. wait until firmware finish operation when entering into and leaving from RF test mode
 *  *  *  *  *  * 4. correct some HAL implementation
 *
 * 01 22 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * implement following 802.11 OIDs:
 *  *  * OID_802_11_RSSI,
 *  *  * OID_802_11_RSSI_TRIGGER,
 *  *  * OID_802_11_STATISTICS,
 *  *  * OID_802_11_DISASSOCIATE,
 *  *  * OID_802_11_POWER_MODE
 *
 * 01 21 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * implement OID_802_11_MEDIA_STREAM_MODE
 *
 * 01 21 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * implement OID_802_11_SUPPORTED_RATES / OID_802_11_DESIRED_RATES
 *
 * 12 30 2009 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) According to CMD/EVENT documentation v0.8,
 *  *  *  *  *  * OID_CUSTOM_TEST_RX_STATUS & OID_CUSTOM_TEST_TX_STATUS is no longer used,
 *  *  *  *  *  * and result is retrieved by get ATInfo instead
 *  *  *  *  *  * 2) add 4 counter for recording aggregation statistics
**  \main\maintrunk.MT6620WiFiDriver_Prj\20 2009-12-11 18:35:07 GMT mtk02752
**  add CMD added in CMD/EVEN document v0.8
**  \main\maintrunk.MT6620WiFiDriver_Prj\19 2009-12-10 16:39:37 GMT mtk02752
**  eliminate unused definitions
**  \main\maintrunk.MT6620WiFiDriver_Prj\18 2009-12-10 09:55:11 GMT mtk02752
**  command ID/event ID revised
**  \main\maintrunk.MT6620WiFiDriver_Prj\17 2009-12-09 13:57:37 GMT MTK02468
**  Added event ids (EVENT_ID_RX_ADDBA and EVENT_ID_RX_DELBA)
**  \main\maintrunk.MT6620WiFiDriver_Prj\16 2009-12-08 17:35:39 GMT mtk02752
**  + add event ID for EVENT_ID_TEST_STATUS (rf test)
**  \main\maintrunk.MT6620WiFiDriver_Prj\15 2009-12-07 23:01:09 GMT mtk02752
**  add data structure for RF_TEST
**  \main\maintrunk.MT6620WiFiDriver_Prj\14 2009-12-03 16:22:56 GMT mtk01461
**  Modify the element - i4RSSI in EVENT of SCAN RESULT
**  \main\maintrunk.MT6620WiFiDriver_Prj\13 2009-11-30 10:54:44 GMT mtk02752
**  1st DW of WIFI_CMD_T is shared with HIF_TX_HEADER_T, while 1st DW of WIFI_EVENT_T is shared with HIF_RX_HEADER_T
**  \main\maintrunk.MT6620WiFiDriver_Prj\12 2009-11-26 10:16:58 GMT mtk02752
**  resync EVENT_CONNECTION_STATUS
**  \main\maintrunk.MT6620WiFiDriver_Prj\11 2009-11-25 21:34:01 GMT mtk02752
**  sync. EVENT_SCAN_RESULT_T with firmware
**  \main\maintrunk.MT6620WiFiDriver_Prj\10 2009-11-25 21:03:48 GMT mtk02752
**  refine MGMT_FRAME
**  \main\maintrunk.MT6620WiFiDriver_Prj\9 2009-11-25 18:17:47 GMT mtk02752
**  refine GL_WLAN_INFO_T for buffering scan result and presume max. ie length = 600 bytes
**  \main\maintrunk.MT6620WiFiDriver_Prj\8 2009-11-24 22:41:20 GMT mtk02752
**  add EVENT_SCAN_RESULT_T definition
**  \main\maintrunk.MT6620WiFiDriver_Prj\7 2009-11-23 20:29:16 GMT mtk02752
**   fix typo
**  \main\maintrunk.MT6620WiFiDriver_Prj\6 2009-11-23 14:46:01 GMT mtk02752
**  add new command/event structure upon CM@SD1's documentation
**  \main\maintrunk.MT6620WiFiDriver_Prj\5 2009-11-13 15:13:40 GMT mtk02752
**  add command definition for CMD_BUILD_CONNECTION and EVENT_CONNECTION_STATUS
**  \main\maintrunk.MT6620WiFiDriver_Prj\4 2009-05-20 12:22:22 GMT mtk01461
**  Add SeqNum field to Event Header
**  \main\maintrunk.MT6620WiFiDriver_Prj\3 2009-04-29 15:42:11 GMT mtk01461
**  Update structure of HIF_EVENT_HEADER_T and EVENT_HDR_SIZE
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-04-21 12:10:36 GMT mtk01461
**  Add Common Set CMD Callback for MCR Write and other Set OID
**  \main\maintrunk.MT6620WiFiDriver_Prj\1 2009-04-21 01:40:17 GMT mtk01461
**  Command Done Handler
*/
#ifndef _NIC_CMD_EVENT_H
#define _NIC_CMD_EVENT_H

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
#define CMD_STATUS_SUCCESS      0
#define CMD_STATUS_REJECTED     1
#define CMD_STATUS_UNKNOWN      2

#define EVENT_HDR_SIZE          OFFSET_OF(WIFI_EVENT_T, aucBuffer[0])

#define MAX_IE_LENGTH       (600)
#define MAX_WSC_IE_LENGTH   (400)

/* Action field in structure CMD_CH_PRIVILEGE_T */
#define CMD_CH_ACTION_REQ           0
#define CMD_CH_ACTION_ABORT         1

/* Status field in structure EVENT_CH_PRIVILEGE_T */
#define EVENT_CH_STATUS_GRANT       0


typedef enum _ENUM_CMD_ID_T {
    CMD_ID_TEST_MODE = 1,               /* 0x01 (Set) */
    CMD_ID_RESET_REQUEST,               /* 0x02 (Set) */
    CMD_ID_BUILD_CONNECTION,            /* 0x03 (Set) */
    CMD_ID_SCAN_REQ_V2,                 /* 0x04 (Set) */
    CMD_ID_NIC_POWER_CTRL,              /* 0x05 (Set) */
    CMD_ID_POWER_SAVE_MODE,             /* 0x06 (Set) */
    CMD_ID_LINK_ATTRIB,                 /* 0x07 (Set) */
    CMD_ID_ADD_REMOVE_KEY,              /* 0x08 (Set) */
    CMD_ID_DEFAULT_KEY_ID,              /* 0x09 (Set) */
    CMD_ID_INFRASTRUCTURE,              /* 0x0a (Set) */
    CMD_ID_SET_RX_FILTER,               /* 0x0b (Set) */
    CMD_ID_DOWNLOAD_BUF,                /* 0x0c (Set) */
    CMD_ID_WIFI_START,                  /* 0x0d (Set) */
    CMD_ID_CMD_BT_OVER_WIFI,            /* 0x0e (Set) */
    CMD_ID_SET_MEDIA_CHANGE_DELAY_TIME, /* 0x0f (Set) */
    CMD_ID_SEND_ADDBA_RSP,              /* 0x10 (Set) */
    CMD_ID_WAPI_MODE,                   /* 0x11 (Set)  (obsolete) */
    CMD_ID_WAPI_ASSOC_INFO,             /* 0x12 (Set)  (obsolete) */
    CMD_ID_SET_DOMAIN_INFO,             /* 0x13 (Set) */
    CMD_ID_SET_IP_ADDRESS,              /* 0x14 (Set) */
    CMD_ID_BSS_ACTIVATE_CTRL,           /* 0x15 (Set) */
    CMD_ID_SET_BSS_INFO,                /* 0x16 (Set) */
    CMD_ID_UPDATE_STA_RECORD,           /* 0x17 (Set) */
    CMD_ID_REMOVE_STA_RECORD,           /* 0x18 (Set) */
    CMD_ID_INDICATE_PM_BSS_CREATED,     /* 0x19 (Set) */
    CMD_ID_INDICATE_PM_BSS_CONNECTED,   /* 0x1a (Set) */
    CMD_ID_INDICATE_PM_BSS_ABORT,       /* 0x1b (Set) */
    CMD_ID_UPDATE_BEACON_CONTENT,       /* 0x1c (Set) */
    CMD_ID_SET_BSS_RLM_PARAM,           /* 0x1d (Set) */
    CMD_ID_SCAN_REQ,                    /* 0x1e (Set) */
    CMD_ID_SCAN_CANCEL,                 /* 0x1f (Set) */
    CMD_ID_CH_PRIVILEGE,                /* 0x20 (Set) */
    CMD_ID_UPDATE_WMM_PARMS,            /* 0x21 (Set) */
    CMD_ID_SET_WMM_PS_TEST_PARMS,       /* 0x22 (Set) */
    CMD_ID_TX_AMPDU,                    /* 0x23 (Set) */
    CMD_ID_ADDBA_REJECT,                /* 0x24 (Set) */
    CMD_ID_SET_PS_PROFILE_ADV,          /* 0x25 (Set) */
    CMD_ID_SET_RAW_PATTERN,             /* 0x26 (Set) */
    CMD_ID_CONFIG_PATTERN_FUNC,         /* 0x27 (Set) */
    CMD_ID_SET_TX_PWR,                  /* 0x28 (Set) */
    CMD_ID_SET_5G_PWR_OFFSET,           /* 0x29 (Set) */
    CMD_ID_SET_PWR_PARAM,               /* 0x2A (Set) */
    CMD_ID_P2P_ABORT,                   /* 0x2B (Set) */
#if CFG_STRESS_TEST_SUPPORT
	CMD_ID_RANDOM_RX_RESET_EN = 0x2C,	/* 0x2C (Set ) */
	CMD_ID_RANDOM_RX_RESET_DE = 0x2D,	/* 0x2D (Set ) */
	CMD_ID_SAPP_EN            = 0x2E,	/* 0x2E (Set ) */
	CMD_ID_SAPP_DE            = 0x2F,	/* 0x2F (Set ) */
#endif
    CMD_ID_ROAMING_TRANSIT  = 0x30,     /* 0x30 (Set) */
    CMD_ID_SET_PHY_PARAM,               /* 0x31 (Set) */
    CMD_ID_SET_NOA_PARAM,               /* 0x32 (Set) */
    CMD_ID_SET_OPPPS_PARAM,             /* 0x33 (Set) */
    CMD_ID_SET_UAPSD_PARAM,             /* 0x34 (Set) */
    CMD_ID_SET_SIGMA_STA_SLEEP,         /* 0x35 (Set) */
    CMD_ID_SET_EDGE_TXPWR_LIMIT,        /* 0x36 (Set) */
    CMD_ID_SET_DEVICE_MODE,             /* 0x37 (Set) */
    CMD_ID_SET_TXPWR_CTRL,              /* 0x38 (Set) */
    CMD_ID_SET_AUTOPWR_CTRL,            /* 0x39 (Set) */
    CMD_ID_SET_WFD_CTRL  = 0x3A,        /* 0x3A (Set) */

    CMD_ID_GET_NIC_CAPABILITY = 0x80,   /* 0x80 (Query) */
    CMD_ID_GET_LINK_QUALITY,            /* 0x81 (Query) */
    CMD_ID_GET_STATISTICS,              /* 0x82 (Query) */
    CMD_ID_GET_CONNECTION_STATUS,       /* 0x83 (Query) */
    CMD_ID_GET_ASSOC_INFO,              /* 0x84 (Query) (obsolete) */
    CMD_ID_GET_STA_STATISTICS = 0x85,   /* 0x85 (Query) */

    CMD_ID_BASIC_CONFIG = 0xc1,         /* 0xc1 (Set / Query) */
    CMD_ID_ACCESS_REG,                  /* 0xc2 (Set / Query) */
    CMD_ID_MAC_MCAST_ADDR,              /* 0xc3 (Set / Query) */
    CMD_ID_802_11_PMKID,                /* 0xc4 (Set / Query) */
    CMD_ID_ACCESS_EEPROM,               /* 0xc5 (Set / Query) */
    CMD_ID_SW_DBG_CTRL,                 /* 0xc6 (Set / Query) */
#if 1/* CFG_SUPPORT_ANTI_PIRACY */
    CMD_ID_SEC_CHECK,                    /* 0xc7 (Set / Query) */
#endif
    CMD_ID_DUMP_MEM,                      /* 0xc8 (Query) */

#if CFG_SUPPORT_RDD_TEST_MODE
    CMD_ID_SET_RDD_CH = 0xE1,
#endif

    CMD_ID_SET_BWCS = 0xF1,
#if CFG_SUPPORT_OSC_SETTING && defined(MT5931)
    CMD_ID_SET_OSC = 0xf2,
#endif

} ENUM_CMD_ID_T, *P_ENUM_CMD_ID_T;

typedef enum _ENUM_EVENT_ID_T {
    EVENT_ID_CMD_RESULT = 1,            /* 0x01 (Query) */
    EVENT_ID_NIC_CAPABILITY,            /* 0x02 (Query) */
    EVENT_ID_CONNECTION_STATUS,         /* 0x03 (Query / Unsolicited) (obsolete) */
    EVENT_ID_SCAN_RESULT,               /* 0x04 (Query / Unsolicited) (obselete) */
    EVENT_ID_LINK_QUALITY,              /* 0x05 (Query / Unsolicited) */
    EVENT_ID_STATISTICS,                /* 0x06 (Query) */
    EVENT_ID_MIC_ERR_INFO,              /* 0x07 (Unsolicited) */
    EVENT_ID_ASSOC_INFO,                /* 0x08 (Query - CMD_ID_GET_ASSOC_INFO) */
    EVENT_ID_BASIC_CONFIG,              /* 0x09 (Query - CMD_ID_BASIC_CONFIG) */
    EVENT_ID_ACCESS_REG,                /* 0x0a (Query - CMD_ID_ACCESS_REG) */
    EVENT_ID_MAC_MCAST_ADDR,            /* 0x0b (Query - CMD_ID_MAC_MCAST_ADDR) */
    EVENT_ID_802_11_PMKID,              /* 0x0c (Query - CMD_ID_802_11_PMKID) */
    EVENT_ID_ACCESS_EEPROM,             /* 0x0d (Query - CMD_ID_ACCESS_EEPROM) */
    EVENT_ID_SLEEPY_NOTIFY,             /* 0x0e (Query) */
    EVENT_ID_BT_OVER_WIFI,              /* 0x0f (Unsolicited) */
    EVENT_ID_TEST_STATUS,               /* 0x10 (Query - CMD_ID_TEST_MODE) */
    EVENT_ID_RX_ADDBA,                  /* 0x11 (Unsolicited) (obsolete) */
    EVENT_ID_RX_DELBA,                  /* 0x12 (Unsolicited) (obsolete) */
    EVENT_ID_ACTIVATE_STA_REC_T,        /* 0x13 (Unsolicited) */
    EVENT_ID_DEACTIVATE_STA_REC_T,      /* 0x14 (Unsolicited) */
    EVENT_ID_SCAN_DONE,                 /* 0x15 (Unsoiicited) */
    EVENT_ID_RX_FLUSH,                  /* 0x16 (Unsolicited) */
    EVENT_ID_TX_DONE,                   /* 0x17 (Unsolicited) */
    EVENT_ID_CH_PRIVILEGE,              /* 0x18 (Unsolicited) */
    EVENT_ID_BSS_ABSENCE_PRESENCE = 0x19,  /* 0x19 (Unsolicited) */
    EVENT_ID_STA_CHANGE_PS_MODE,        /* 0x1A (Unsolicited) */
    EVENT_ID_BSS_BEACON_TIMEOUT,        /* 0x1B (Unsolicited) */
    EVENT_ID_UPDATE_NOA_PARAMS,         /* 0x1C (Unsolicited) */
    EVENT_ID_AP_OBSS_STATUS,            /* 0x1D (Unsolicited) */
    EVENT_ID_STA_UPDATE_FREE_QUOTA,     /* 0x1E (Unsolicited) */
    EVENT_ID_SW_DBG_CTRL,               /* 0x1F (Query - CMD_ID_SW_DBG_CTRL) */
    EVENT_ID_ROAMING_STATUS,            /* 0x20 (Unsolicited) */
    EVENT_ID_STA_AGING_TIMEOUT,         /* 0x21 (Unsolicited) */
#if 1 /* CFG_SUPPORT_ANTI_PIRACY */
    EVENT_ID_SEC_CHECK_RSP,              /* 0x22 (Unsolicited) */
#endif
    EVENT_ID_SEND_DEAUTH,               /* 0x23 (Unsolicited) */

#if CFG_SUPPORT_RDD_TEST_MODE
    EVENT_ID_UPDATE_RDD_STATUS,          /* 0x24 (Unsolicited) */
#endif

#if CFG_SUPPORT_BCM && CFG_SUPPORT_BCM_BWCS
    EVENT_ID_UPDATE_BWCS_STATUS = 0x25,         /* 0x25 (Unsolicited) */
    EVENT_ID_UPDATE_BCM_DEBUG,                    /* 0x26 (Unsolicited) */
#endif
    EVENT_ID_RX_ERR,
    EVENT_ID_DUMP_MEM,
    EVENT_ID_STA_STATISTICS= 0x29,            /* 0x29 (Query ) */
    EVENT_ID_STA_STATISTICS_UPDATE          /* 0x2A (Unsolicited) */
} ENUM_EVENT_ID_T, *P_ENUM_EVENT_ID_T;


/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
#ifndef LINUX
typedef UINT_8 CMD_STATUS;
#endif
/* for Event Packet (via HIF-RX) */
    /* following CM's documentation v0.7 */
typedef struct _WIFI_CMD_T {
    UINT_16     u2TxByteCount_UserPriority;
    UINT_8      ucEtherTypeOffset;
    UINT_8      ucResource_PktType_CSflags;
    UINT_8      ucCID;
    UINT_8      ucSetQuery;
    UINT_8      ucSeqNum;
    UINT_8      aucReserved2;

    UINT_8      aucBuffer[0];
} WIFI_CMD_T, *P_WIFI_CMD_T;

/* for Command Packet (via HIF-TX) */
    /* following CM's documentation v0.7 */
typedef struct _WIFI_EVENT_T {
    UINT_16     u2PacketLen;
    UINT_16     u2PacketType;
    UINT_8      ucEID;
    UINT_8      ucSeqNum;
    UINT_8      aucReserved2[2];

    UINT_8      aucBuffer[0];
} WIFI_EVENT_T, *P_WIFI_EVENT_T;

// CMD_ID_TEST_MODE
typedef struct _CMD_TEST_CTRL_T {
    UINT_8      ucAction;
    UINT_8      aucReserved[3];
    union {
        UINT_32                 u4OpMode;
        UINT_32                 u4ChannelFreq;
        PARAM_MTK_WIFI_TEST_STRUC_T rRfATInfo;
    } u;
} CMD_TEST_CTRL_T, *P_CMD_TEST_CTRL_T;

// EVENT_TEST_STATUS
typedef struct _PARAM_CUSTOM_RFTEST_TX_STATUS_STRUC_T {
    UINT_32             u4PktSentStatus;
    UINT_32             u4PktSentCount;
    UINT_16             u2AvgAlc;
    UINT_8              ucCckGainControl;
    UINT_8              ucOfdmGainControl;
} PARAM_CUSTOM_RFTEST_TX_STATUS_STRUC_T, *P_PARAM_CUSTOM_RFTEST_TX_STATUS_STRUC_T;

typedef struct  _PARAM_CUSTOM_RFTEST_RX_STATUS_STRUC_T {
    UINT_32             u4IntRxOk;            /*!< number of packets that Rx ok from interrupt */
    UINT_32             u4IntCrcErr;          /*!< number of packets that CRC error from interrupt */
    UINT_32             u4IntShort;           /*!< number of packets that is short preamble from interrupt */
    UINT_32             u4IntLong;            /*!< number of packets that is long preamble from interrupt */
    UINT_32             u4PauRxPktCount;      /*!< number of packets that Rx ok from PAU */
    UINT_32             u4PauCrcErrCount;     /*!< number of packets that CRC error from PAU */
    UINT_32             u4PauRxFifoFullCount; /*!< number of packets that is short preamble from PAU */
    UINT_32             u4PauCCACount;        /*!< CCA rising edge count */
} PARAM_CUSTOM_RFTEST_RX_STATUS_STRUC_T, *P_PARAM_CUSTOM_RFTEST_RX_STATUS_STRUC_T;

typedef union _EVENT_TEST_STATUS {
    PARAM_MTK_WIFI_TEST_STRUC_T             rATInfo;
//    PARAM_CUSTOM_RFTEST_TX_STATUS_STRUC_T   rTxStatus;
//    PARAM_CUSTOM_RFTEST_RX_STATUS_STRUC_T   rRxStatus;
} EVENT_TEST_STATUS, *P_EVENT_TEST_STATUS;

// CMD_BUILD_CONNECTION
typedef struct _CMD_BUILD_CONNECTION {
    UINT_8      ucInfraMode;
    UINT_8      ucAuthMode;
    UINT_8      ucEncryptStatus;
    UINT_8      ucSsidLen;
    UINT_8      aucSsid[PARAM_MAX_LEN_SSID];
    UINT_8      aucBssid[PARAM_MAC_ADDR_LEN];

    /* Ad-hoc mode */
    UINT_16     u2BeaconPeriod;
    UINT_16     u2ATIMWindow;
    UINT_8      ucJoinOnly;
    UINT_8      ucReserved;
    UINT_32     u4FreqInKHz;

    /* for faster connection */
    UINT_8      aucScanResult[0];
} CMD_BUILD_CONNECTION, *P_CMD_BUILD_CONNECTION;

//CMD_ADD_REMOVE_KEY
typedef struct _CMD_802_11_KEY {
    UINT_8      ucAddRemove;
    UINT_8      ucTxKey;
    UINT_8      ucKeyType;
    UINT_8      ucIsAuthenticator;
    UINT_8      aucPeerAddr[6];
    UINT_8      ucNetType;
    UINT_8      ucAlgorithmId;
    UINT_8      ucKeyId;
    UINT_8      ucKeyLen;
    UINT_8      aucReverved[2];
    UINT_8      aucKeyMaterial[32];
    UINT_8      aucKeyRsc[16];
} CMD_802_11_KEY, *P_CMD_802_11_KEY;

/* WPA2 PMKID cache structure */
typedef struct _PMKID_ENTRY_T {
    PARAM_BSSID_INFO_T  rBssidInfo;
    BOOLEAN             fgPmkidExist;
} PMKID_ENTRY_T, *P_PMKID_ENTRY_T;

typedef struct _CMD_802_11_PMKID
{
    ULONG               u4BSSIDInfoCount;
    P_PMKID_ENTRY_T     arPMKIDInfo[1];
} CMD_802_11_PMKID, *P_CMD_802_11_PMKID;

// CMD_BASIC_CONFIG
typedef struct _CMD_CSUM_OFFLOAD {
    UINT_16     u2RxChecksum; // bit0: IP, bit1: UDP, bit2: TCP
    UINT_16     u2TxChecksum; // bit0: IP, bit1: UDP, bit2: TCP
} CMD_CSUM_OFFLOAD, *P_CMD_CSUM_OFFLOAD;

typedef struct _CMD_BASIC_CONFIG {
    PARAM_MAC_ADDRESS   rMyMacAddr;
    UINT_8              ucNative80211;
    UINT_8              aucReserved[1];

    CMD_CSUM_OFFLOAD    rCsumOffload;
} CMD_BASIC_CONFIG, *P_CMD_BASIC_CONFIG, EVENT_BASIC_CONFIG, *P_EVENT_BASIC_CONFIG;

// CMD_MAC_MCAST_ADDR
typedef struct _CMD_MAC_MCAST_ADDR {
    UINT_32             u4NumOfGroupAddr;
    UINT_8              ucNetTypeIndex;
    UINT_8              aucReserved[3];
    PARAM_MAC_ADDRESS   arAddress[MAX_NUM_GROUP_ADDR];
} CMD_MAC_MCAST_ADDR, *P_CMD_MAC_MCAST_ADDR, EVENT_MAC_MCAST_ADDR, *P_EVENT_MAC_MCAST_ADDR;

// CMD_ACCESS_EEPROM
typedef struct _CMD_ACCESS_EEPROM {
    UINT_16             u2Offset;
    UINT_16             u2Data;
} CMD_ACCESS_EEPROM, *P_CMD_ACCESS_EEPROM, EVENT_ACCESS_EEPROM, *P_EVENT_ACCESS_EEPROM;

typedef struct _CMD_CUSTOM_NOA_PARAM_STRUC_T {
    UINT_32  u4NoaDurationMs;
    UINT_32  u4NoaIntervalMs;
    UINT_32  u4NoaCount;
} CMD_CUSTOM_NOA_PARAM_STRUC_T, *P_CMD_CUSTOM_NOA_PARAM_STRUC_T;

typedef struct _CMD_CUSTOM_OPPPS_PARAM_STRUC_T {
    UINT_32  u4CTwindowMs;
} CMD_CUSTOM_OPPPS_PARAM_STRUC_T, *P_CMD_CUSTOM_OPPPS_PARAM_STRUC_T;

typedef struct _CMD_CUSTOM_UAPSD_PARAM_STRUC_T {
    UINT_8  fgEnAPSD;
    UINT_8  fgEnAPSD_AcBe;
    UINT_8  fgEnAPSD_AcBk;
    UINT_8  fgEnAPSD_AcVo;
    UINT_8  fgEnAPSD_AcVi;
    UINT_8  ucMaxSpLen;
    UINT_8  aucResv[2];
} CMD_CUSTOM_UAPSD_PARAM_STRUC_T, *P_CMD_CUSTOM_UAPSD_PARAM_STRUC_T;

// EVENT_CONNECTION_STATUS
typedef struct _EVENT_CONNECTION_STATUS {
    UINT_8  ucMediaStatus;
    UINT_8  ucReasonOfDisconnect;

    UINT_8  ucInfraMode;
    UINT_8  ucSsidLen;
    UINT_8  aucSsid[PARAM_MAX_LEN_SSID];
    UINT_8  aucBssid[PARAM_MAC_ADDR_LEN];
    UINT_8  ucAuthenMode;
    UINT_8  ucEncryptStatus;
    UINT_16 u2BeaconPeriod;
    UINT_16 u2AID;
    UINT_16 u2ATIMWindow;
    UINT_8  ucNetworkType;
    UINT_8  aucReserved[1];
    UINT_32 u4FreqInKHz;

#if CFG_ENABLE_WIFI_DIRECT
    UINT_8 aucInterfaceAddr[PARAM_MAC_ADDR_LEN];
#endif

} EVENT_CONNECTION_STATUS, *P_EVENT_CONNECTION_STATUS;

// EVENT_NIC_CAPABILITY
typedef struct _EVENT_NIC_CAPABILITY {
    UINT_16     u2ProductID;
    UINT_16     u2FwVersion;
    UINT_16     u2DriverVersion;
    UINT_8      ucHw5GBandDisabled;
    UINT_8      ucEepromUsed;
    UINT_8      ucEfuseValid;
    UINT_8      ucMacAddrValid;
#if CFG_REPORT_RFBB_VERSION
    UINT_8      ucRfVersion;
    UINT_8      ucPhyVersion;
#endif
#if CFG_ENABLE_CAL_LOG
    UINT_8      ucRfCalFail;
    UINT_8      ucBbCalFail;
#endif
    UINT_8      aucReserved[2];
} EVENT_NIC_CAPABILITY, *P_EVENT_NIC_CAPABILITY;

// modified version of WLAN_BEACON_FRAME_BODY_T for simplier buffering
typedef struct _WLAN_BEACON_FRAME_BODY_T_LOCAL {
    /* Beacon frame body */
    UINT_32     au4Timestamp[2];            /* Timestamp */
    UINT_16     u2BeaconInterval;           /* Beacon Interval */
    UINT_16     u2CapInfo;                  /* Capability */
    UINT_8      aucInfoElem[MAX_IE_LENGTH]; /* Various IEs, start from SSID */
    UINT_16     u2IELength;                 /* This field is *NOT* carried by F/W but caculated by nic_rx */
} WLAN_BEACON_FRAME_BODY_T_LOCAL, *P_WLAN_BEACON_FRAME_BODY_T_LOCAL;

// EVENT_SCAN_RESULT
typedef struct _EVENT_SCAN_RESULT_T {
    INT_32                          i4RSSI;
    UINT_32                         u4LinkQuality;
    UINT_32                         u4DSConfig; /* Center frequency */
    UINT_32                         u4DomainInfo; /* Require CM opinion */
    UINT_32                         u4Reserved;
    UINT_8                          ucNetworkType;
    UINT_8                          ucOpMode;
    UINT_8                          aucBssid[MAC_ADDR_LEN];
    UINT_8                          aucRatesEx[PARAM_MAX_LEN_RATES_EX];
    WLAN_BEACON_FRAME_BODY_T_LOCAL  rBeaconFrameBody;
} EVENT_SCAN_RESULT_T, *P_EVENT_SCAN_RESULT_T;

/* event of tkip mic error */
typedef struct _EVENT_MIC_ERR_INFO
{
    UINT_32     u4Flags;
} EVENT_MIC_ERR_INFO, *P_EVENT_MIC_ERR_INFO;

typedef struct _EVENT_PMKID_CANDIDATE_LIST_T
{
    UINT_32     u4Version;            /*!< Version */
    UINT_32     u4NumCandidates;      /*!< How many candidates follow */
    PARAM_PMKID_CANDIDATE_T   arCandidateList[1];
} EVENT_PMKID_CANDIDATE_LIST_T, *P_EVENT_PMKID_CANDIDATE_LIST_T;


typedef struct _EVENT_CMD_RESULT {
	UINT_8		ucCmdID;
	UINT_8		ucStatus;
	UINT_8		aucReserved[2];
} EVENT_CMD_RESULT, *P_EVENT_CMD_RESULT;

#if CFG_SUPPORT_OSC_SETTING && defined(MT5931)
typedef struct _CMD_MCU_LP_PARAM_T {
    UINT_16     u2OscStableTime;
    UINT_8      ucReserved[6];
} CMD_MCU_LP_PARAM_T, *P_CMD_MCU_LP_PARAM_T;
#endif


// CMD_ID_ACCESS_REG & EVENT_ID_ACCESS_REG
typedef struct _CMD_ACCESS_REG {
    UINT_32     u4Address;
    UINT_32     u4Data;
} CMD_ACCESS_REG, *P_CMD_ACCESS_REG;

// CMD_DUMP_MEMORY
typedef struct _CMD_DUMP_MEM {
    UINT_32     u4Address;
    UINT_32     u4Length;
    UINT_32     u4RemainLength;
    UINT_8      ucFragNum;
} CMD_DUMP_MEM, *P_CMD_DUMP_MEM;

typedef struct _EVENT_DUMP_MEM_T {
    UINT_32     u4Address;
    UINT_32     u4Length;
    UINT_32     u4RemainLength;
    UINT_8      ucFragNum;
    UINT_8      aucBuffer[1];
} EVENT_DUMP_MEM_T, *P_EVENT_DUMP_MEM_T;

typedef struct _CMD_SW_DBG_CTRL_T {
    UINT_32     u4Id;
    UINT_32     u4Data;
    /* Debug Support */
    UINT_32	    u4DebugCnt[64];
} CMD_SW_DBG_CTRL_T, *P_CMD_SW_DBG_CTRL_T;


// CMD_ID_LINK_ATTRIB
typedef struct _CMD_LINK_ATTRIB {
    INT_8       cRssiTrigger;
    UINT_8      ucDesiredRateLen;
    UINT_16     u2DesiredRate[32];
    UINT_8      ucMediaStreamMode;
    UINT_8      aucReserved[1];
} CMD_LINK_ATTRIB, *P_CMD_LINK_ATTRIB;

// CMD_ID_NIC_POWER_CTRL
typedef struct _CMD_NIC_POWER_CTRL {
    UINT_8  ucPowerMode;
    UINT_8  aucReserved[3];
} CMD_NIC_POWER_CTRL, *P_CMD_NIC_POWER_CTRL;

// CMD_ID_POWER_SAVE_MODE
typedef struct _CMD_PS_PROFILE_T {
    UINT_8  ucNetTypeIndex;
    UINT_8  ucPsProfile;
    UINT_8  aucReserved[2];
} CMD_PS_PROFILE_T, *P_CMD_PS_PROFILE_T;


// EVENT_LINK_QUALITY
typedef struct _EVENT_LINK_QUALITY {
    INT_8       cRssi;
    INT_8       cLinkQuality;
    UINT_16     u2LinkSpeed;
    UINT_8      ucMediumBusyPercentage;
} EVENT_LINK_QUALITY, *P_EVENT_LINK_QUALITY;

#if CFG_SUPPORT_P2P_RSSI_QUERY
// EVENT_LINK_QUALITY
typedef struct _EVENT_LINK_QUALITY_EX {
    INT_8       cRssi;
    INT_8       cLinkQuality;
    UINT_16     u2LinkSpeed;
    UINT_8      ucMediumBusyPercentage;
    UINT_8     ucIsLQ0Rdy;
    INT_8       cRssiP2P;   /* For P2P Network. */
    INT_8       cLinkQualityP2P;
    UINT_16     u2LinkSpeedP2P;
    UINT_8      ucMediumBusyPercentageP2P;
    UINT_8     ucIsLQ1Rdy;
} EVENT_LINK_QUALITY_EX, *P_EVENT_LINK_QUALITY_EX;
#endif

// EVENT_ID_STATISTICS
typedef struct _EVENT_STATISTICS {
    LARGE_INTEGER   rTransmittedFragmentCount;
    LARGE_INTEGER   rMulticastTransmittedFrameCount;
    LARGE_INTEGER   rFailedCount;
    LARGE_INTEGER   rRetryCount;
    LARGE_INTEGER   rMultipleRetryCount;
    LARGE_INTEGER   rRTSSuccessCount;
    LARGE_INTEGER   rRTSFailureCount;
    LARGE_INTEGER   rACKFailureCount;
    LARGE_INTEGER   rFrameDuplicateCount;
    LARGE_INTEGER   rReceivedFragmentCount;
    LARGE_INTEGER   rMulticastReceivedFrameCount;
    LARGE_INTEGER   rFCSErrorCount;
} EVENT_STATISTICS, *P_EVENT_STATISTICS;

// EVENT_ID_FW_SLEEPY_NOTIFY
typedef struct _EVENT_SLEEPY_NOTIFY {
    UINT_8  ucSleepyState;
    UINT_8  aucReserved[3];
} EVENT_SLEEPY_NOTIFY, *P_EVENT_SLEEPY_NOTIFY;

typedef struct _EVENT_ACTIVATE_STA_REC_T {
    UINT_8      aucMacAddr[6];
    UINT_8      ucStaRecIdx;
    UINT_8      ucNetworkTypeIndex;
    BOOLEAN     fgIsQoS;
    BOOLEAN     fgIsAP;
    UINT_8      aucReserved[2];
} EVENT_ACTIVATE_STA_REC_T, *P_EVENT_ACTIVATE_STA_REC_T;

typedef struct _EVENT_DEACTIVATE_STA_REC_T {
    UINT_8      ucStaRecIdx;
    UINT_8      aucReserved[3];
} EVENT_DEACTIVATE_STA_REC_T, *P_EVENT_DEACTIVATE_STA_REC_T;

// CMD_BT_OVER_WIFI
typedef struct _CMD_BT_OVER_WIFI {
    UINT_8              ucAction;       /* 0: query, 1: setup, 2: destroy */
    UINT_8              ucChannelNum;
    PARAM_MAC_ADDRESS   rPeerAddr;
    UINT_16             u2BeaconInterval;
    UINT_8              ucTimeoutDiscovery;
    UINT_8              ucTimeoutInactivity;
    UINT_8              ucRole;
    UINT_8              PAL_Capabilities;
    UINT_8              cMaxTxPower;
    UINT_8              ucChannelBand;
    UINT_8              ucReserved[1];
} CMD_BT_OVER_WIFI, *P_CMD_BT_OVER_WIFI;

// EVENT_BT_OVER_WIFI
typedef struct _EVENT_BT_OVER_WIFI {
    UINT_8      ucLinkStatus;
    UINT_8      ucSelectedChannel;
    INT_8       cRSSI;
    UINT_8      ucReserved[1];
} EVENT_BT_OVER_WIFI, *P_EVENT_BT_OVER_WIFI;

// Same with DOMAIN_SUBBAND_INFO
typedef struct _CMD_SUBBAND_INFO {
    UINT_8              ucRegClass;
    UINT_8              ucBand;
    UINT_8              ucChannelSpan;
    UINT_8              ucFirstChannelNum;
    UINT_8              ucNumChannels;
    UINT_8              aucReserved[3];
} CMD_SUBBAND_INFO, *P_CMD_SUBBAND_INFO;

// CMD_SET_DOMAIN_INFO
typedef struct _CMD_SET_DOMAIN_INFO_T {
    UINT_16             u2CountryCode;
    UINT_16             u2Reserved;
    CMD_SUBBAND_INFO    rSubBand[6];

    UINT_8              uc2G4Bandwidth; /* CONFIG_BW_20_40M or CONFIG_BW_20M */
    UINT_8              uc5GBandwidth;  /* CONFIG_BW_20_40M or CONFIG_BW_20M */
    UINT_8              aucReserved[2];
} CMD_SET_DOMAIN_INFO_T, *P_CMD_SET_DOMAIN_INFO_T;

// CMD_SET_IP_ADDRESS
typedef struct _IPV4_NETWORK_ADDRESS {
    UINT_8 aucIpAddr[4];
} IPV4_NETWORK_ADDRESS, *P_IPV4_NETWORK_ADDRESS;

typedef struct _CMD_SET_NETWORK_ADDRESS_LIST {
    UINT_8  ucNetTypeIndex;
    UINT_8  ucAddressCount;
    UINT_8  ucReserved[2];
    IPV4_NETWORK_ADDRESS arNetAddress[1];
} CMD_SET_NETWORK_ADDRESS_LIST, *P_CMD_SET_NETWORK_ADDRESS_LIST;

typedef struct _PATTERN_DESCRIPTION {
    UINT_8      fgCheckBcA1;
    UINT_8      fgCheckMcA1;
    UINT_8      ePatternHeader;
    UINT_8      fgAndOp;
    UINT_8      fgNotOp;
    UINT_8      ucPatternMask;
    UINT_16    ucPatternOffset;
    UINT_8      aucPattern[8];
} PATTERN_DESCRIPTION, *P_PATTERN_DESCRIPTION;

typedef struct _CMD_RAW_PATTERN_CONFIGURATION_T {
    PATTERN_DESCRIPTION arPatternDesc[4];
} CMD_RAW_PATTERN_CONFIGURATION_T, *P_CMD_RAW_PATTERN_CONFIGURATION_T;

typedef struct _CMD_PATTERN_FUNC_CONFIG {
    BOOLEAN      fgBcA1En;
    BOOLEAN      fgMcA1En;
    BOOLEAN      fgBcA1MatchDrop;
    BOOLEAN      fgMcA1MatchDrop;
} CMD_PATTERN_FUNC_CONFIG, *P_CMD_PATTERN_FUNC_CONFIG;


typedef struct _EVENT_TX_DONE_T {
    UINT_8      ucPacketSeq;
	UINT_8		ucStatus;
    UINT_16     u2SequenceNumber;
    UINT_32     au4Reserved1;
    UINT_32     au4Reserved2;
    UINT_32     au4Reserved3;
} EVENT_TX_DONE_T, *P_EVENT_TX_DONE_T;

typedef struct _CMD_BSS_ACTIVATE_CTRL {
    UINT_8  ucNetTypeIndex;
    UINT_8  ucActive;
    UINT_8  aucReserved[2];
} CMD_BSS_ACTIVATE_CTRL, *P_CMD_BSS_ACTIVATE_CTRL;

typedef struct _CMD_SET_BSS_RLM_PARAM_T {
    UINT_8      ucNetTypeIndex;
    UINT_8      ucRfBand;
    UINT_8      ucPrimaryChannel;
    UINT_8      ucRfSco;
    UINT_8      ucErpProtectMode;
    UINT_8      ucHtProtectMode;
    UINT_8      ucGfOperationMode;
    UINT_8      ucTxRifsMode;
    UINT_16     u2HtOpInfo3;
    UINT_16     u2HtOpInfo2;
    UINT_8      ucHtOpInfo1;
    UINT_8      ucUseShortPreamble;
    UINT_8      ucUseShortSlotTime;
    UINT_8      ucCheckId;          /* Fixed value: 0x72 */
} CMD_SET_BSS_RLM_PARAM_T, *P_CMD_SET_BSS_RLM_PARAM_T;

typedef struct _CMD_SET_BSS_INFO {
    UINT_8  ucNetTypeIndex;
    UINT_8  ucConnectionState;
    UINT_8  ucCurrentOPMode;
    UINT_8  ucSSIDLen;
    UINT_8  aucSSID[32];
    UINT_8  aucBSSID[6];
    UINT_8  ucIsQBSS;
    UINT_8  ucReserved1;
    UINT_16 u2OperationalRateSet;
    UINT_16 u2BSSBasicRateSet;
    UINT_8  ucStaRecIdxOfAP;
    UINT_8  ucReserved2;
    UINT_8  ucReserved3;
    UINT_8  ucNonHTBasicPhyType; /* For Slot Time and CWmin */
    UINT_8  ucAuthMode;
    UINT_8  ucEncStatus;
    UINT_8  ucPhyTypeSet;
    UINT_8  aucOwnMac[6];
	UINT_8  fgWapiMode;
	UINT_8  fgIsApMode;
    UINT_8  fgHiddenSsidMode;
    CMD_SET_BSS_RLM_PARAM_T rBssRlmParam;
} CMD_SET_BSS_INFO, *P_CMD_SET_BSS_INFO;

typedef struct _CMD_UPDATE_STA_RECORD_T {
    UINT_8  ucIndex;
    UINT_8  ucStaType;
    UINT_8  aucMacAddr[MAC_ADDR_LEN];
    UINT_16 u2AssocId;
    UINT_16 u2ListenInterval;
    UINT_8  ucNetTypeIndex;
    UINT_8  ucDesiredPhyTypeSet;
    UINT_16 u2DesiredNonHTRateSet;
    UINT_16 u2BSSBasicRateSet;
    UINT_8  ucIsQoS;
    UINT_8  ucIsUapsdSupported;
    UINT_8  ucStaState;
    UINT_8  ucMcsSet;
    UINT_8  ucSupMcs32;
    UINT_8  ucAmpduParam;
    UINT_16 u2HtCapInfo;
    UINT_16 u2HtExtendedCap;
    UINT_32 u4TxBeamformingCap;
    UINT_8  ucAselCap;
    UINT_8  ucRCPI;
    UINT_8  ucNeedResp;
    UINT_8  ucUapsdAc;  /* b0~3: Trigger enabled, b4~7: Delivery enabled */
    UINT_8  ucUapsdSp;  /* 0: all, 1: max 2, 2: max 4, 3: max 6 */
    UINT_8  aucReserved[3];
    /* TBD */
} CMD_UPDATE_STA_RECORD_T, *P_CMD_UPDATE_STA_RECORD_T;

typedef struct _CMD_REMOVE_STA_RECORD_T {
    UINT_8  ucIndex;
    UINT_8  ucReserved;
    UINT_8  aucMacAddr[MAC_ADDR_LEN];
} CMD_REMOVE_STA_RECORD_T, *P_CMD_REMOVE_STA_RECORD_T;

typedef struct _CMD_INDICATE_PM_BSS_CREATED_T {
    UINT_8  ucNetTypeIndex;
    UINT_8  ucDtimPeriod;
    UINT_16 u2BeaconInterval;
    UINT_16 u2AtimWindow;
    UINT_8  aucReserved[2];
} CMD_INDICATE_PM_BSS_CREATED, *P_CMD_INDICATE_PM_BSS_CREATED;

typedef struct _CMD_INDICATE_PM_BSS_CONNECTED_T {
    UINT_8  ucNetTypeIndex;
    UINT_8  ucDtimPeriod;
    UINT_16 u2AssocId;
    UINT_16 u2BeaconInterval;
    UINT_16 u2AtimWindow;
    UINT_8  fgIsUapsdConnection;
    UINT_8  ucBmpDeliveryAC;
    UINT_8  ucBmpTriggerAC;
    UINT_8  aucReserved[1];
} CMD_INDICATE_PM_BSS_CONNECTED, *P_CMD_INDICATE_PM_BSS_CONNECTED;

typedef struct _CMD_INDICATE_PM_BSS_ABORT {
    UINT_8  ucNetTypeIndex;
    UINT_8  aucReserved[3];
} CMD_INDICATE_PM_BSS_ABORT, *P_CMD_INDICATE_PM_BSS_ABORT;

typedef struct _CMD_BEACON_TEMPLATE_UPDATE {
    UINT_8    ucUpdateMethod; // 0: update randomly, 1: update all, 2: delete all (1 and 2 will update directly without search)
    UINT_8    ucNetTypeIndex;
    UINT_8    aucReserved[2];
    UINT_16   u2Capability;
    UINT_16   u2IELen;
    UINT_8    aucIE[MAX_IE_LENGTH];
} CMD_BEACON_TEMPLATE_UPDATE, *P_CMD_BEACON_TEMPLATE_UPDATE;

typedef struct _CMD_SET_WMM_PS_TEST_STRUC_T {
    UINT_8  ucNetTypeIndex;
    UINT_8  bmfgApsdEnAc;           /* b0~3: trigger-en AC0~3. b4~7: delivery-en AC0~3 */
    UINT_8  ucIsEnterPsAtOnce;      /* enter PS immediately without 5 second guard after connected */
    UINT_8  ucIsDisableUcTrigger;   /* not to trigger UC on beacon TIM is matched (under U-APSD) */
} CMD_SET_WMM_PS_TEST_STRUC_T, *P_CMD_SET_WMM_PS_TEST_STRUC_T;


/* Definition for CHANNEL_INFO.ucBand:
 * 0:       Reserved
 * 1:       BAND_2G4
 * 2:       BAND_5G
 * Others:  Reserved
 */
typedef struct _CHANNEL_INFO_T {
    UINT_8  ucBand;
    UINT_8  ucChannelNum;
} CHANNEL_INFO_T, *P_CHANNEL_INFO_T;

typedef struct _CMD_SCAN_REQ_T {
    UINT_8          ucSeqNum;
    UINT_8          ucNetworkType;
    UINT_8          ucScanType;
    UINT_8          ucSSIDType;     /* BIT(0) wildcard / BIT(1) P2P-wildcard / BIT(2) specific */
    UINT_8          ucSSIDLength;
    UINT_8          aucReserved[1];
    UINT_16         u2ChannelMinDwellTime;
    UINT_8          aucSSID[32];
    UINT_16         u2ChannelDwellTime; /* For P2P */
    UINT_8          ucChannelType;
    UINT_8          ucChannelListNum;
    CHANNEL_INFO_T  arChannelList[32];
    UINT_16         u2IELen;
    UINT_8          aucIE[MAX_IE_LENGTH];
} CMD_SCAN_REQ, *P_CMD_SCAN_REQ;

typedef struct _CMD_SCAN_REQ_V2_T {
    UINT_8          ucSeqNum;
    UINT_8          ucNetworkType;
    UINT_8          ucScanType;
    UINT_8          ucSSIDType;
    PARAM_SSID_T    arSSID[4];
    UINT_16         u2ProbeDelayTime;
    UINT_16         u2ChannelDwellTime; /* For P2P */
    UINT_8          ucChannelType;
    UINT_8          ucChannelListNum;
    CHANNEL_INFO_T  arChannelList[32];
    UINT_16         u2IELen;
    UINT_8          aucIE[MAX_IE_LENGTH];
} CMD_SCAN_REQ_V2, *P_CMD_SCAN_REQ_V2;

typedef struct _CMD_SCAN_CANCEL_T {
    UINT_8          ucSeqNum;
    UINT_8          ucIsExtChannel;     /* For P2P channel extention. */
    UINT_8          aucReserved[2];
} CMD_SCAN_CANCEL, *P_CMD_SCAN_CANCEL;

typedef struct _EVENT_SCAN_DONE_T {
    UINT_8          ucSeqNum;
    UINT_8          ucSparseChannelValid;
    CHANNEL_INFO_T  rSparseChannel;
} EVENT_SCAN_DONE, *P_EVENT_SCAN_DONE;

typedef struct _CMD_CH_PRIVILEGE_T {
    UINT_8      ucNetTypeIndex;
    UINT_8      ucTokenID;
    UINT_8      ucAction;
    UINT_8      ucPrimaryChannel;
    UINT_8      ucRfSco;
    UINT_8      ucRfBand;
    UINT_8      ucReqType;
    UINT_8      ucReserved;
    UINT_32     u4MaxInterval;  /* In unit of ms */
    UINT_8      aucBSSID[6];
    UINT_8      aucReserved[2];
} CMD_CH_PRIVILEGE_T, *P_CMD_CH_PRIVILEGE_T;

typedef struct _CMD_TX_PWR_T {
    INT_8       cTxPwr2G4Cck;		/* signed, in unit of 0.5dBm */
#if defined(MT6620)
    INT_8       acReserved[3];
#elif defined(MT5931) || defined(MT6628)
    INT_8       cTxPwr2G4Dsss;      /* signed, in unit of 0.5dBm */
    INT_8       acReserved[2];
#else
    #error "No valid definition!"
#endif

    INT_8       cTxPwr2G4OFDM_BPSK;
    INT_8       cTxPwr2G4OFDM_QPSK;
    INT_8       cTxPwr2G4OFDM_16QAM;
    INT_8       cTxPwr2G4OFDM_Reserved;
    INT_8       cTxPwr2G4OFDM_48Mbps;
    INT_8       cTxPwr2G4OFDM_54Mbps;

    INT_8       cTxPwr2G4HT20_BPSK;
    INT_8       cTxPwr2G4HT20_QPSK;
    INT_8       cTxPwr2G4HT20_16QAM;
    INT_8       cTxPwr2G4HT20_MCS5;
    INT_8       cTxPwr2G4HT20_MCS6;
    INT_8       cTxPwr2G4HT20_MCS7;

    INT_8       cTxPwr2G4HT40_BPSK;
    INT_8       cTxPwr2G4HT40_QPSK;
    INT_8       cTxPwr2G4HT40_16QAM;
    INT_8       cTxPwr2G4HT40_MCS5;
    INT_8       cTxPwr2G4HT40_MCS6;
    INT_8       cTxPwr2G4HT40_MCS7;

    INT_8       cTxPwr5GOFDM_BPSK;
    INT_8       cTxPwr5GOFDM_QPSK;
    INT_8       cTxPwr5GOFDM_16QAM;
    INT_8       cTxPwr5GOFDM_Reserved;
    INT_8       cTxPwr5GOFDM_48Mbps;
    INT_8       cTxPwr5GOFDM_54Mbps;

    INT_8       cTxPwr5GHT20_BPSK;
    INT_8       cTxPwr5GHT20_QPSK;
    INT_8       cTxPwr5GHT20_16QAM;
    INT_8       cTxPwr5GHT20_MCS5;
    INT_8       cTxPwr5GHT20_MCS6;
    INT_8       cTxPwr5GHT20_MCS7;

    INT_8       cTxPwr5GHT40_BPSK;
    INT_8       cTxPwr5GHT40_QPSK;
    INT_8       cTxPwr5GHT40_16QAM;
    INT_8       cTxPwr5GHT40_MCS5;
    INT_8       cTxPwr5GHT40_MCS6;
    INT_8       cTxPwr5GHT40_MCS7;
} CMD_TX_PWR_T, *P_CMD_TX_PWR_T;

typedef struct _CMD_5G_PWR_OFFSET_T {
    INT_8       cOffsetBand0;       /* 4.915-4.980G */
    INT_8       cOffsetBand1;       /* 5.000-5.080G */
    INT_8       cOffsetBand2;       /* 5.160-5.180G */
    INT_8       cOffsetBand3;       /* 5.200-5.280G */
    INT_8       cOffsetBand4;       /* 5.300-5.340G */
    INT_8       cOffsetBand5;       /* 5.500-5.580G */
    INT_8       cOffsetBand6;       /* 5.600-5.680G */
    INT_8       cOffsetBand7;       /* 5.700-5.825G */
} CMD_5G_PWR_OFFSET_T, *P_CMD_5G_PWR_OFFSET_T;

typedef struct _CMD_PWR_PARAM_T {
    UINT_32     au4Data[28];
    UINT_32     u4RefValue1;
    UINT_32     u4RefValue2;
} CMD_PWR_PARAM_T, *P_CMD_PWR_PARAM_T;


typedef struct _CMD_PHY_PARAM_T {
    UINT_8      aucData[144];           /* eFuse content */
} CMD_PHY_PARAM_T, *P_CMD_PHY_PARAM_T;

typedef struct _CMD_AUTO_POWER_PARAM_T {
    UINT_8      ucType;        /* 0: Disable 1: Enalbe 0x10: Change paramters */
    UINT_8      ucNetTypeIndex;
    UINT_8      aucReserved[2];
    UINT_8      aucLevelRcpiTh[3];
    UINT_8      aucReserved2[1];
    INT_8       aicLevelPowerOffset[3]; 	/* signed, in unit of 0.5dBm */
    UINT_8      aucReserved3[1];
    UINT_8      aucReserved4[8];
} CMD_AUTO_POWER_PARAM_T, *P_CMD_AUTO_POWER_PARAM_T;


typedef struct _EVENT_CH_PRIVILEGE_T {
    UINT_8          ucNetTypeIndex;
    UINT_8          ucTokenID;
    UINT_8          ucStatus;
    UINT_8          ucPrimaryChannel;
    UINT_8          ucRfSco;
    UINT_8          ucRfBand;
    UINT_8          ucReqType;
    UINT_8          ucReserved;
    UINT_32         u4GrantInterval;    /* In unit of ms */
} EVENT_CH_PRIVILEGE_T, *P_EVENT_CH_PRIVILEGE_T;

typedef struct _EVENT_BSS_BEACON_TIMEOUT_T {
    UINT_8  ucNetTypeIndex;
    UINT_8  aucReserved[3];
} EVENT_BSS_BEACON_TIMEOUT_T, *P_EVENT_BSS_BEACON_TIMEOUT_T;

typedef struct _EVENT_STA_AGING_TIMEOUT_T {
    UINT_8  ucStaRecIdx;
    UINT_8  aucReserved[3];
} EVENT_STA_AGING_TIMEOUT_T, *P_EVENT_STA_AGING_TIMEOUT_T;

typedef struct _EVENT_NOA_TIMING_T {
    UINT_8      fgIsInUse;              /* Indicate if this entry is in use or not */
    UINT_8      ucCount;                /* Count */
    UINT_8      aucReserved[2];

    UINT_32     u4Duration;             /* Duration */
    UINT_32     u4Interval;             /* Interval */
    UINT_32     u4StartTime;            /* Start Time */
} EVENT_NOA_TIMING_T, *P_EVENT_NOA_TIMING_T;

typedef struct _EVENT_UPDATE_NOA_PARAMS_T {
    UINT_8      ucNetTypeIndex;
    UINT_8      aucReserved[2];
    UINT_8      fgEnableOppPS;
    UINT_16     u2CTWindow;

    UINT_8              ucNoAIndex;
    UINT_8              ucNoATimingCount; /* Number of NoA Timing */
    EVENT_NOA_TIMING_T  arEventNoaTiming[8/*P2P_MAXIMUM_NOA_COUNT*/];
} EVENT_UPDATE_NOA_PARAMS_T, *P_EVENT_UPDATE_NOA_PARAMS_T;

typedef struct _EVENT_AP_OBSS_STATUS_T {
    UINT_8      ucNetTypeIndex;
    UINT_8      ucObssErpProtectMode;
    UINT_8      ucObssHtProtectMode;
    UINT_8      ucObssGfOperationMode;
    UINT_8      ucObssRifsOperationMode;
    UINT_8      ucObssBeaconForcedTo20M;
    UINT_8      aucReserved[2];
} EVENT_AP_OBSS_STATUS_T, *P_EVENT_AP_OBSS_STATUS_T;

typedef struct _CMD_EDGE_TXPWR_LIMIT_T {
    INT_8       cBandEdgeMaxPwrCCK;
    INT_8       cBandEdgeMaxPwrOFDM20;
    INT_8       cBandEdgeMaxPwrOFDM40;
    INT_8       cReserved;
} CMD_EDGE_TXPWR_LIMIT_T, *P_CMD_EDGE_TXPWR_LIMIT_T;

typedef struct _CMD_SET_DEVICE_MODE_T {
    UINT_16     u2ChipID;
    UINT_16     u2Mode;
} CMD_SET_DEVICE_MODE_T, *P_CMD_SET_DEVICE_MODE_T;


#if CFG_SUPPORT_RDD_TEST_MODE
typedef struct _CMD_RDD_CH_T {
    UINT_8      ucRddTestMode;
    UINT_8      ucRddShutCh;
    UINT_8      ucRddStartCh;
    UINT_8      ucRddStopCh;
    UINT_8      ucRddDfs;
    UINT_8      ucReserved;
    UINT_8      ucReserved1;
    UINT_8      ucReserved2;
} CMD_RDD_CH_T, *P_CMD_RDD_CH_T;

typedef struct _EVENT_RDD_STATUS_T {
    UINT_8  ucRddStatus;
    UINT_8  aucReserved[3];
} EVENT_RDD_STATUS_T, *P_EVENT_RDD_STATUS_T;
#endif

typedef struct _CMD_SET_TXPWR_CTRL_T{
    INT_8    c2GLegacyStaPwrOffset;  /* Unit: 0.5dBm, default: 0*/
    INT_8    c2GHotspotPwrOffset;
    INT_8    c2GP2pPwrOffset;
    INT_8    c2GBowPwrOffset;
    INT_8    c5GLegacyStaPwrOffset;   /* Unit: 0.5dBm, default: 0*/
    INT_8    c5GHotspotPwrOffset;
    INT_8    c5GP2pPwrOffset;
    INT_8    c5GBowPwrOffset;
    UINT_8  ucConcurrencePolicy;   /* TX power policy when concurrence
                                                            in the same channel
                                                            0: Highest power has priority
                                                            1: Lowest power has priority */
    INT_8    acReserved1[3];            /* Must be zero */

    /* Power limit by channel for all data rates */
    INT_8    acTxPwrLimit2G[14];     /* Channel 1~14, Unit: 0.5dBm*/
    INT_8    acTxPwrLimit5G[4];       /* UNII 1~4 */
    INT_8    acReserved2[2];            /* Must be zero */
} CMD_SET_TXPWR_CTRL_T, *P_CMD_SET_TXPWR_CTRL_T;

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
VOID
nicCmdEventQueryMcrRead (
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventQueryMemDump (
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventQuerySwCtrlRead (
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );



VOID
nicCmdEventQueryRfTestATInfo(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventSetCommon (
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventSetDisassociate (
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventSetIpAddress (
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventQueryLinkQuality(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventQueryLinkSpeed(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventQueryStatistics(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventEnterRfTest(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventLeaveRfTest(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventQueryAddress(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventQueryMcastAddr(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventQueryEepromRead(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventSetMediaStreamMode(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

/* Statistics responder */
VOID
nicCmdEventQueryXmitOk(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventQueryRecvOk(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventQueryXmitError(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventQueryRecvError(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventQueryRecvNoBuffer(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventQueryRecvCrcError(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventQueryRecvErrorAlignment(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventQueryXmitOneCollision(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventQueryXmitMoreCollisions(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
nicCmdEventQueryXmitMaxCollisions(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

/* for timeout check */
VOID
nicOidCmdTimeoutCommon (
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo
    );

VOID
nicCmdTimeoutCommon (
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo
    );

VOID
nicOidCmdEnterRFTestTimeout (
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _NIC_CMD_EVENT_H */

