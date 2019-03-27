/*
 * Qualcomm Atheros OUI and vendor specific assignments
 * Copyright (c) 2014-2017, Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef QCA_VENDOR_H
#define QCA_VENDOR_H

/*
 * This file is a registry of identifier assignments from the Qualcomm Atheros
 * OUI 00:13:74 for purposes other than MAC address assignment. New identifiers
 * can be assigned through normal review process for changes to the upstream
 * hostap.git repository.
 */

#define OUI_QCA 0x001374

/**
 * enum qca_radiotap_vendor_ids - QCA radiotap vendor namespace IDs
 */
enum qca_radiotap_vendor_ids {
	QCA_RADIOTAP_VID_WLANTEST = 0,
};

/**
 * enum qca_nl80211_vendor_subcmds - QCA nl80211 vendor command identifiers
 *
 * @QCA_NL80211_VENDOR_SUBCMD_UNSPEC: Reserved value 0
 *
 * @QCA_NL80211_VENDOR_SUBCMD_TEST: Test command/event
 *
 * @QCA_NL80211_VENDOR_SUBCMD_ROAMING: Set roaming policy for drivers that use
 *	internal BSS-selection. This command uses
 *	@QCA_WLAN_VENDOR_ATTR_ROAMING_POLICY to specify the new roaming policy
 *	for the current connection (i.e., changes policy set by the nl80211
 *	Connect command). @QCA_WLAN_VENDOR_ATTR_MAC_ADDR may optionally be
 *	included to indicate which BSS to use in case roaming is disabled.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_AVOID_FREQUENCY: Recommendation of frequency
 *	ranges to avoid to reduce issues due to interference or internal
 *	co-existence information in the driver. The event data structure is
 *	defined in struct qca_avoid_freq_list.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_DFS_CAPABILITY: Command to check driver support
 *	for DFS offloading.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_NAN: NAN command/event which is used to pass
 *	NAN Request/Response and NAN Indication messages. These messages are
 *	interpreted between the framework and the firmware component. While
 *	sending the command from userspace to the driver, payload is not
 *	encapsulated inside any attribute. Attribute QCA_WLAN_VENDOR_ATTR_NAN
 *	is used when receiving vendor events in userspace from the driver.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_KEY_MGMT_SET_KEY: Set key operation that can be
 *	used to configure PMK to the driver even when not connected. This can
 *	be used to request offloading of key management operations. Only used
 *	if device supports QCA_WLAN_VENDOR_FEATURE_KEY_MGMT_OFFLOAD.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_KEY_MGMT_ROAM_AUTH: An extended version of
 *	NL80211_CMD_ROAM event with optional attributes including information
 *	from offloaded key management operation. Uses
 *	enum qca_wlan_vendor_attr_roam_auth attributes. Only used
 *	if device supports QCA_WLAN_VENDOR_FEATURE_KEY_MGMT_OFFLOAD.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_DO_ACS: ACS command/event which is used to
 *	invoke the ACS function in device and pass selected channels to
 *	hostapd.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_GET_FEATURES: Command to get the features
 *	supported by the driver. enum qca_wlan_vendor_features defines
 *	the possible features.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_DFS_OFFLOAD_CAC_STARTED: Event used by driver,
 *	which supports DFS offloading, to indicate a channel availability check
 *	start.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_DFS_OFFLOAD_CAC_FINISHED: Event used by driver,
 *	which supports DFS offloading, to indicate a channel availability check
 *	completion.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_DFS_OFFLOAD_CAC_ABORTED: Event used by driver,
 *	which supports DFS offloading, to indicate that the channel availability
 *	check aborted, no change to the channel status.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_DFS_OFFLOAD_CAC_NOP_FINISHED: Event used by
 *	driver, which supports DFS offloading, to indicate that the
 *	Non-Occupancy Period for this channel is over, channel becomes usable.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_DFS_OFFLOAD_RADAR_DETECTED: Event used by driver,
 *	which supports DFS offloading, to indicate a radar pattern has been
 *	detected. The channel is now unusable.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_GET_LOGGER_FEATURE_SET: Get the feature bitmap
 *	based on enum wifi_logger_supported_features. Attributes defined in
 *	enum qca_wlan_vendor_attr_get_logger_features.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_GET_RING_DATA: Get the ring data from a particular
 *	logger ring, QCA_WLAN_VENDOR_ATTR_WIFI_LOGGER_RING_ID is passed as the
 *	attribute for this command. Attributes defined in
 *	enum qca_wlan_vendor_attr_wifi_logger_start.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_TDLS_GET_CAPABILITIES: Get the supported TDLS
 *	capabilities of the driver, parameters includes the attributes defined
 *	in enum qca_wlan_vendor_attr_tdls_get_capabilities.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_OFFLOADED_PACKETS: Vendor command used to offload
 *	sending of certain periodic IP packet to firmware, attributes defined in
 *	enum qca_wlan_vendor_attr_offloaded_packets.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_MONITOR_RSSI: Command used to configure RSSI
 *	monitoring, defines min and max RSSI which are configured for RSSI
 *	monitoring. Also used to notify the RSSI breach and provides the BSSID
 *	and RSSI value that was breached. Attributes defined in
 *	enum qca_wlan_vendor_attr_rssi_monitoring.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_NDP: Command used for performing various NAN
 *	Data Path (NDP) related operations, attributes defined in
 *	enum qca_wlan_vendor_attr_ndp_params.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_ND_OFFLOAD: Command used to enable/disable
 *	Neighbour Discovery offload, attributes defined in
 *	enum qca_wlan_vendor_attr_nd_offload.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_PACKET_FILTER: Used to set/get the various
 *	configuration parameter for BPF packet filter, attributes defined in
 *	enum qca_wlan_vendor_attr_packet_filter.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_GET_BUS_SIZE: Gets the driver-firmware
 *	maximum supported size, attributes defined in
 *	enum qca_wlan_vendor_drv_info.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_GET_WAKE_REASON_STATS: Command to get various
 *	data about wake reasons and datapath IP statistics, attributes defined
 *	in enum qca_wlan_vendor_attr_wake_stats.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_OCB_SET_CONFIG: Command used to set configuration
 *	for IEEE 802.11 communicating outside the context of a basic service
 *	set, called OCB command. Uses the attributes defines in
 *	enum qca_wlan_vendor_attr_ocb_set_config.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_OCB_SET_UTC_TIME: Command used to set OCB
 *	UTC time. Use the attributes defines in
 *	enum qca_wlan_vendor_attr_ocb_set_utc_time.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_OCB_START_TIMING_ADVERT: Command used to start
 *	sending OCB timing advert frames. Uses the attributes defines in
 *	enum qca_wlan_vendor_attr_ocb_start_timing_advert.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_OCB_STOP_TIMING_ADVERT: Command used to stop
 *	OCB timing advert. Uses the attributes defines in
 *	enum qca_wlan_vendor_attr_ocb_stop_timing_advert.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_OCB_GET_TSF_TIMER: Command used to get TSF
 *	timer value. Uses the attributes defines in
 *	enum qca_wlan_vendor_attr_ocb_get_tsf_resp.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_LINK_PROPERTIES: Command/event to update the
 *	link properties of the respective interface. As an event, is used
 *	to notify the connected station's status. The attributes for this
 *	command are defined in enum qca_wlan_vendor_attr_link_properties.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_P2P_LISTEN_OFFLOAD_START: Command used to
 *	start the P2P Listen offload function in device and pass the listen
 *	channel, period, interval, count, device types, and vendor specific
 *	information elements to the device driver and firmware.
 *	Uses the attributes defines in
 *	enum qca_wlan_vendor_attr_p2p_listen_offload.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_P2P_LISTEN_OFFLOAD_STOP: Command/event used to
 *	indicate stop request/response of the P2P Listen offload function in
 *	device. As an event, it indicates either the feature stopped after it
 *	was already running or feature has actually failed to start. Uses the
 *	attributes defines in enum qca_wlan_vendor_attr_p2p_listen_offload.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_SAP_CONDITIONAL_CHAN_SWITCH: After AP starts
 *	beaconing, this sub command provides the driver, the frequencies on the
 *	5 GHz band to check for any radar activity. Driver selects one channel
 *	from this priority list provided through
 *	@QCA_WLAN_VENDOR_ATTR_SAP_CONDITIONAL_CHAN_SWITCH_FREQ_LIST and starts
 *	to check for radar activity on it. If no radar activity is detected
 *	during the channel availability check period, driver internally switches
 *	to the selected frequency of operation. If the frequency is zero, driver
 *	internally selects a channel. The status of this conditional switch is
 *	indicated through an event using the same sub command through
 *	@QCA_WLAN_VENDOR_ATTR_SAP_CONDITIONAL_CHAN_SWITCH_STATUS. Attributes are
 *	listed in qca_wlan_vendor_attr_sap_conditional_chan_switch.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_GPIO_CONFIG_COMMAND: Set GPIO pins. This uses the
 *	attributes defined in enum qca_wlan_gpio_attr.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_GET_HW_CAPABILITY: Fetch hardware capabilities.
 *	This uses @QCA_WLAN_VENDOR_ATTR_GET_HW_CAPABILITY to indicate which
 *	capabilities are to be fetched and other
 *	enum qca_wlan_vendor_attr_get_hw_capability attributes to return the
 *	requested capabilities.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_LL_STATS_EXT: Link layer statistics extension.
 *	enum qca_wlan_vendor_attr_ll_stats_ext attributes are used with this
 *	command and event.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_LOC_GET_CAPA: Get capabilities for
 *	indoor location features. Capabilities are reported in
 *	QCA_WLAN_VENDOR_ATTR_LOC_CAPA.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_FTM_START_SESSION: Start an FTM
 *	(fine timing measurement) session with one or more peers.
 *	Specify Session cookie in QCA_WLAN_VENDOR_ATTR_FTM_SESSION_COOKIE and
 *	peer information in QCA_WLAN_VENDOR_ATTR_FTM_MEAS_PEERS.
 *	On success, 0 or more QCA_NL80211_VENDOR_SUBCMD_FTM_MEAS_RESULT
 *	events will be reported, followed by
 *	QCA_NL80211_VENDOR_SUBCMD_FTM_SESSION_DONE event to indicate
 *	end of session.
 *	Refer to IEEE P802.11-REVmc/D7.0, 11.24.6
 *
 * @QCA_NL80211_VENDOR_SUBCMD_FTM_ABORT_SESSION: Abort a running session.
 *	A QCA_NL80211_VENDOR_SUBCMD_FTM_SESSION_DONE will be reported with
 *	status code indicating session was aborted.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_FTM_MEAS_RESULT: Event with measurement
 *	results for one peer. Results are reported in
 *	QCA_WLAN_VENDOR_ATTR_FTM_MEAS_PEER_RESULTS.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_FTM_SESSION_DONE: Event triggered when
 *	FTM session is finished, either successfully or aborted by
 *	request.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_FTM_CFG_RESPONDER: Configure FTM responder
 *	mode. QCA_WLAN_VENDOR_ATTR_FTM_RESPONDER_ENABLE specifies whether
 *	to enable or disable the responder. LCI/LCR reports can be
 *	configured with QCA_WLAN_VENDOR_ATTR_FTM_LCI and
 *	QCA_WLAN_VENDOR_ATTR_FTM_LCR. Can be called multiple
 *	times to update the LCI/LCR reports.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_AOA_MEAS: Perform a standalone AOA (angle of
 *	arrival) measurement with a single peer. Specify peer MAC address in
 *	QCA_WLAN_VENDOR_ATTR_MAC_ADDR and optionally frequency (MHz) in
 *	QCA_WLAN_VENDOR_ATTR_FREQ (if not specified, locate peer in kernel
 *	scan results cache and use the frequency from there).
 *	Also specify measurement type in QCA_WLAN_VENDOR_ATTR_AOA_TYPE.
 *	Measurement result is reported in
 *	QCA_NL80211_VENDOR_SUBCMD_AOA_MEAS_RESULT event.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_AOA_ABORT_MEAS: Abort an AOA measurement. Specify
 *	peer MAC address in QCA_WLAN_VENDOR_ATTR_MAC_ADDR.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_AOA_MEAS_RESULT: Event that reports
 *	the AOA measurement result.
 *	Peer MAC address reported in QCA_WLAN_VENDOR_ATTR_MAC_ADDR.
 *	success/failure status is reported in
 *	QCA_WLAN_VENDOR_ATTR_LOC_SESSION_STATUS.
 *	Measurement data is reported in QCA_WLAN_VENDOR_ATTR_AOA_MEAS_RESULT.
 *	The antenna array(s) used in the measurement are reported in
 *	QCA_WLAN_VENDOR_ATTR_LOC_ANTENNA_ARRAY_MASK.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_ENCRYPTION_TEST: Encrypt/decrypt the given
 *	data as per the given parameters.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_GET_CHAIN_RSSI: Get antenna RSSI value for a
 *	specific chain.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_DMG_RF_GET_SECTOR_CFG: Get low level
 *	configuration for a DMG RF sector. Specify sector index in
 *	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_INDEX, sector type in
 *	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_TYPE and RF modules
 *	to return sector information for in
 *	QCA_WLAN_VENDOR_ATTR_DMG_RF_MODULE_MASK. Returns sector configuration
 *	in QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_CFG. Also return the
 *	exact time where information was captured in
 *	QCA_WLAN_VENDOR_ATTR_TSF.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_DMG_RF_SET_SECTOR_CFG: Set low level
 *	configuration for a DMG RF sector. Specify sector index in
 *	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_INDEX, sector type in
 *	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_TYPE and sector configuration
 *	for one or more DMG RF modules in
 *	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_CFG.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_DMG_RF_GET_SELECTED_SECTOR: Get selected
 *	DMG RF sector for a station. This is the sector that the HW
 *	will use to communicate with the station. Specify the MAC address
 *	of associated station/AP/PCP in QCA_WLAN_VENDOR_ATTR_MAC_ADDR (not
 *	needed for unassociated	station). Specify sector type to return in
 *	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_TYPE. Returns the selected
 *	sector index in QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_INDEX.
 *	Also return the exact time where the information was captured
 *	in QCA_WLAN_VENDOR_ATTR_TSF.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_DMG_RF_SET_SELECTED_SECTOR: Set the
 *	selected DMG RF sector for a station. This is the sector that
 *	the HW will use to communicate with the station.
 *	Specify the MAC address of associated station/AP/PCP in
 *	QCA_WLAN_VENDOR_ATTR_MAC_ADDR, the sector type to select in
 *	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_TYPE and the sector index
 *	in QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_INDEX.
 *	The selected sector will be locked such that it will not be
 *	modified like it normally does (for example when station
 *	moves around). To unlock the selected sector for a station
 *	pass the special value 0xFFFF in the sector index. To unlock
 *	all connected stations also pass a broadcast MAC address.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_CONFIGURE_TDLS: Configure the TDLS behavior
 *	in the host driver. The different TDLS configurations are defined
 *	by the attributes in enum qca_wlan_vendor_attr_tdls_configuration.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_GET_HE_CAPABILITIES: Query device IEEE 802.11ax HE
 *	capabilities. The response uses the attributes defined in
 *	enum qca_wlan_vendor_attr_get_he_capabilities.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_ABORT_SCAN: Abort an ongoing vendor scan that was
 *	started with QCA_NL80211_VENDOR_SUBCMD_TRIGGER_SCAN. This command
 *	carries the scan cookie of the corresponding scan request. The scan
 *	cookie is represented by QCA_WLAN_VENDOR_ATTR_SCAN_COOKIE.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_SET_SAR_LIMITS: Set the Specific
 *	Absorption Rate (SAR) power limits. A critical regulation for
 *	FCC compliance, OEMs require methods to set SAR limits on TX
 *	power of WLAN/WWAN. enum qca_vendor_attr_sar_limits
 *	attributes are used with this command.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_EXTERNAL_ACS: This command/event is used by the
 *	host driver for offloading the implementation of Auto Channel Selection
 *	(ACS) to an external user space entity. This interface is used as the
 *	event from the host driver to the user space entity and also as the
 *	request from the user space entity to the host driver. The event from
 *	the host driver is used by the user space entity as an indication to
 *	start the ACS functionality. The attributes used by this event are
 *	represented by the enum qca_wlan_vendor_attr_external_acs_event.
 *	User space entity uses the same interface to inform the host driver with
 *	selected channels after the ACS operation using the attributes defined
 *	by enum qca_wlan_vendor_attr_external_acs_channels.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_CHIP_PWRSAVE_FAILURE: Vendor event carrying the
 *	requisite information leading to a power save failure. The information
 *	carried as part of this event is represented by the
 *	enum qca_attr_chip_power_save_failure attributes.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_NUD_STATS_SET: Start/Stop the NUD statistics
 *	collection. Uses attributes defined in enum qca_attr_nud_stats_set.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_NUD_STATS_GET: Get the NUD statistics. These
 *	statistics are represented by the enum qca_attr_nud_stats_get
 *	attributes.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_FETCH_BSS_TRANSITION_STATUS: Sub-command to fetch
 *	the BSS transition status, whether accept or reject, for a list of
 *	candidate BSSIDs provided by the userspace. This uses the vendor
 *	attributes QCA_WLAN_VENDOR_ATTR_BTM_MBO_TRANSITION_REASON and
 *	QCA_WLAN_VENDOR_ATTR_BTM_CANDIDATE_INFO. The userspace shall specify
 *	the attributes QCA_WLAN_VENDOR_ATTR_BTM_MBO_TRANSITION_REASON and an
 *	array of QCA_WLAN_VENDOR_ATTR_BTM_CANDIDATE_INFO_BSSID nested in
 *	QCA_WLAN_VENDOR_ATTR_BTM_CANDIDATE_INFO in the request. In the response
 *	the driver shall specify array of
 *	QCA_WLAN_VENDOR_ATTR_BTM_CANDIDATE_INFO_BSSID and
 *	QCA_WLAN_VENDOR_ATTR_BTM_CANDIDATE_INFO_STATUS pairs nested in
 *	QCA_WLAN_VENDOR_ATTR_BTM_CANDIDATE_INFO.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_SET_TRACE_LEVEL: Set the trace level for a
 *	specific QCA module. The trace levels are represented by
 *	enum qca_attr_trace_level attributes.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_BRP_SET_ANT_LIMIT: Set the Beam Refinement
 *	Protocol antenna limit in different modes. See enum
 *	qca_wlan_vendor_attr_brp_ant_limit_mode.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_SPECTRAL_SCAN_START: Start spectral scan. The scan
 *	parameters are specified by enum qca_wlan_vendor_attr_spectral_scan.
 *	This returns a cookie (%QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_COOKIE)
 *	identifying the operation in success case.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_SPECTRAL_SCAN_STOP: Stop spectral scan. This uses
 *	a cookie (%QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_COOKIE) from
 *	@QCA_NL80211_VENDOR_SUBCMD_SPECTRAL_SCAN_START to identify the scan to
 *	be stopped.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_ACTIVE_TOS: Set the active Type Of Service on the
 *	specific interface. This can be used to modify some of the low level
 *	scan parameters (off channel dwell time, home channel time) in the
 *	driver/firmware. These parameters are maintained within the host driver.
 *	This command is valid only when the interface is in the connected state.
 *	These scan parameters shall be reset by the driver/firmware once
 *	disconnected. The attributes used with this command are defined in
 *	enum qca_wlan_vendor_attr_active_tos.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_HANG: Event indicating to the user space that the
 *	driver has detected an internal failure. This event carries the
 *	information indicating the reason that triggered this detection. The
 *	attributes for this command are defined in
 *	enum qca_wlan_vendor_attr_hang.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_SPECTRAL_SCAN_GET_CONFIG: Get the current values
 *	of spectral parameters used. The spectral scan parameters are specified
 *	by enum qca_wlan_vendor_attr_spectral_scan.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_SPECTRAL_SCAN_GET_DIAG_STATS: Get the debug stats
 *	for spectral scan functionality. The debug stats are specified by
 *	enum qca_wlan_vendor_attr_spectral_diag_stats.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_SPECTRAL_SCAN_GET_CAP_INFO: Get spectral
 *	scan system capabilities. The capabilities are specified
 *	by enum qca_wlan_vendor_attr_spectral_cap.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_SPECTRAL_SCAN_GET_STATUS: Get the current
 *	status of spectral scan. The status values are specified
 *	by enum qca_wlan_vendor_attr_spectral_scan_status.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_PEER_FLUSH_PENDING: Sub-command to flush
 *	peer pending packets. Specify the peer MAC address in
 *	QCA_WLAN_VENDOR_ATTR_PEER_ADDR and the access category of the packets
 *	in QCA_WLAN_VENDOR_ATTR_AC. The attributes are listed
 *	in enum qca_wlan_vendor_attr_flush_pending.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_GET_RROP_INFO: Get vendor specific Representative
 *	RF Operating Parameter (RROP) information. The attributes for this
 *	information are defined in enum qca_wlan_vendor_attr_rrop_info. This is
 *	intended for use by external Auto Channel Selection applications.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_GET_SAR_LIMITS: Get the Specific Absorption Rate
 *	(SAR) power limits. This is a companion to the command
 *	@QCA_NL80211_VENDOR_SUBCMD_SET_SAR_LIMITS and is used to retrieve the
 *	settings currently in use. The attributes returned by this command are
 *	defined by enum qca_vendor_attr_sar_limits.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_WLAN_MAC_INFO: Provides the current behavior of
 *	the WLAN hardware MAC. Also, provides the WLAN netdev interface
 *	information attached to the respective MAC.
 *	This works both as a query (user space asks the current mode) or event
 *	interface (driver advertising the current mode to the user space).
 *	Driver does not trigger this event for temporary hardware mode changes.
 *	Mode changes w.r.t Wi-Fi connection update (VIZ creation / deletion,
 *	channel change, etc.) are updated with this event. Attributes for this
 *	interface are defined in enum qca_wlan_vendor_attr_mac.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_SET_QDEPTH_THRESH: Set MSDU queue depth threshold
 *	per peer per TID. Attributes for this command are define in
 *	enum qca_wlan_set_qdepth_thresh_attr.
 * @QCA_NL80211_VENDOR_SUBCMD_THERMAL_CMD: Provides the thermal shutdown action
 *	guide for WLAN driver. Request to suspend of driver and FW if the
 *	temperature is higher than the suspend threshold; resume action is
 *	requested to driver if the temperature is lower than the resume
 *	threshold. In user poll mode, request temperature data by user. For test
 *	purpose, getting thermal shutdown configuration parameters is needed.
 *	Attributes for this interface are defined in
 *	enum qca_wlan_vendor_attr_thermal_cmd.
 * @QCA_NL80211_VENDOR_SUBCMD_THERMAL_EVENT: Thermal events reported from
 *	driver. Thermal temperature and indication of resume completion are
 *	reported as thermal events. The attributes for this command are defined
 *	in enum qca_wlan_vendor_attr_thermal_event.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_WIFI_TEST_CONFIGURATION: Sub command to set WiFi
 *	test configuration. Attributes for this command are defined in
 *	enum qca_wlan_vendor_attr_wifi_test_config.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_BSS_FILTER: This command is used to configure an
 *	RX filter to receive frames from stations that are active on the
 *	operating channel, but not associated with the local device (e.g., STAs
 *	associated with other APs). Filtering is done based on a list of BSSIDs
 *	and STA MAC addresses added by the user. This command is also used to
 *	fetch the statistics of unassociated stations. The attributes used with
 *	this command are defined in enum qca_wlan_vendor_attr_bss_filter.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_NAN_EXT: An extendable version of NAN vendor
 *	command. The earlier command for NAN, QCA_NL80211_VENDOR_SUBCMD_NAN,
 *	carried a payload which was a binary blob of data. The command was not
 *	extendable to send more information. The newer version carries the
 *	legacy blob encapsulated within an attribute and can be extended with
 *	additional vendor attributes that can enhance the NAN command interface.
 * @QCA_NL80211_VENDOR_SUBCMD_ROAM_SCAN_EVENT: Event to indicate scan triggered
 *	or stopped within driver/firmware in order to initiate roaming. The
 *	attributes used with this event are defined in enum
 *	qca_wlan_vendor_attr_roam_scan. Some drivers may not send these events
 *	in few cases, e.g., if the host processor is sleeping when this event
 *	is generated in firmware.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_PEER_CFR_CAPTURE_CFG: This command is used to
 *	configure parameters per peer to capture Channel Frequency Response
 *	(CFR) and enable Periodic CFR capture. The attributes for this command
 *	are defined in enum qca_wlan_vendor_peer_cfr_capture_attr.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_THROUGHPUT_CHANGE_EVENT: Event to indicate changes
 *	in throughput dynamically. The driver estimates the throughput based on
 *	number of packets being transmitted/received per second and indicates
 *	the changes in throughput to user space. Userspace tools can use this
 *	information to configure kernel's TCP parameters in order to achieve
 *	peak throughput. Optionally, the driver will also send guidance on
 *	modifications to kernel's TCP parameters which can be referred by
 *	userspace tools. The attributes used with this event are defined in enum
 *	qca_wlan_vendor_attr_throughput_change.
 *
 * @QCA_NL80211_VENDOR_SUBCMD_COEX_CONFIG: This command is used to set
 *	priorities among different types of traffic during coex scenarios.
 *	Current supported prioritization is among WLAN/BT/ZIGBEE with different
 *	profiles mentioned in enum qca_coex_config_profiles. The associated
 *	attributes used with this command are defined in enum
 *	qca_vendor_attr_coex_config.
 *
 *	Based on the config provided, FW will boost the weight and prioritize
 *	the traffic for that subsystem (WLAN/BT/Zigbee).
 */
enum qca_nl80211_vendor_subcmds {
	QCA_NL80211_VENDOR_SUBCMD_UNSPEC = 0,
	QCA_NL80211_VENDOR_SUBCMD_TEST = 1,
	/* subcmds 2..8 not yet allocated */
	QCA_NL80211_VENDOR_SUBCMD_ROAMING = 9,
	QCA_NL80211_VENDOR_SUBCMD_AVOID_FREQUENCY = 10,
	QCA_NL80211_VENDOR_SUBCMD_DFS_CAPABILITY =  11,
	QCA_NL80211_VENDOR_SUBCMD_NAN =  12,
	QCA_NL80211_VENDOR_SUBCMD_STATS_EXT = 13,
	QCA_NL80211_VENDOR_SUBCMD_LL_STATS_SET = 14,
	QCA_NL80211_VENDOR_SUBCMD_LL_STATS_GET = 15,
	QCA_NL80211_VENDOR_SUBCMD_LL_STATS_CLR = 16,
	QCA_NL80211_VENDOR_SUBCMD_LL_STATS_RADIO_RESULTS = 17,
	QCA_NL80211_VENDOR_SUBCMD_LL_STATS_IFACE_RESULTS = 18,
	QCA_NL80211_VENDOR_SUBCMD_LL_STATS_PEERS_RESULTS = 19,
	QCA_NL80211_VENDOR_SUBCMD_GSCAN_START = 20,
	QCA_NL80211_VENDOR_SUBCMD_GSCAN_STOP = 21,
	QCA_NL80211_VENDOR_SUBCMD_GSCAN_GET_VALID_CHANNELS = 22,
	QCA_NL80211_VENDOR_SUBCMD_GSCAN_GET_CAPABILITIES = 23,
	QCA_NL80211_VENDOR_SUBCMD_GSCAN_GET_CACHED_RESULTS = 24,
	QCA_NL80211_VENDOR_SUBCMD_GSCAN_SCAN_RESULTS_AVAILABLE = 25,
	QCA_NL80211_VENDOR_SUBCMD_GSCAN_FULL_SCAN_RESULT = 26,
	QCA_NL80211_VENDOR_SUBCMD_GSCAN_SCAN_EVENT = 27,
	QCA_NL80211_VENDOR_SUBCMD_GSCAN_HOTLIST_AP_FOUND = 28,
	QCA_NL80211_VENDOR_SUBCMD_GSCAN_SET_BSSID_HOTLIST = 29,
	QCA_NL80211_VENDOR_SUBCMD_GSCAN_RESET_BSSID_HOTLIST = 30,
	QCA_NL80211_VENDOR_SUBCMD_GSCAN_SIGNIFICANT_CHANGE = 31,
	QCA_NL80211_VENDOR_SUBCMD_GSCAN_SET_SIGNIFICANT_CHANGE = 32,
	QCA_NL80211_VENDOR_SUBCMD_GSCAN_RESET_SIGNIFICANT_CHANGE = 33,
	QCA_NL80211_VENDOR_SUBCMD_TDLS_ENABLE = 34,
	QCA_NL80211_VENDOR_SUBCMD_TDLS_DISABLE = 35,
	QCA_NL80211_VENDOR_SUBCMD_TDLS_GET_STATUS = 36,
	QCA_NL80211_VENDOR_SUBCMD_TDLS_STATE = 37,
	QCA_NL80211_VENDOR_SUBCMD_GET_SUPPORTED_FEATURES = 38,
	QCA_NL80211_VENDOR_SUBCMD_SCANNING_MAC_OUI = 39,
	QCA_NL80211_VENDOR_SUBCMD_NO_DFS_FLAG = 40,
	QCA_NL80211_VENDOR_SUBCMD_GSCAN_HOTLIST_AP_LOST = 41,
	QCA_NL80211_VENDOR_SUBCMD_GET_CONCURRENCY_MATRIX = 42,
	/* 43..49 - reserved for QCA */
	QCA_NL80211_VENDOR_SUBCMD_KEY_MGMT_SET_KEY = 50,
	QCA_NL80211_VENDOR_SUBCMD_KEY_MGMT_ROAM_AUTH = 51,
	QCA_NL80211_VENDOR_SUBCMD_APFIND = 52,
	/* 53 - reserved - was used by QCA, but not in use anymore */
	QCA_NL80211_VENDOR_SUBCMD_DO_ACS = 54,
	QCA_NL80211_VENDOR_SUBCMD_GET_FEATURES = 55,
	QCA_NL80211_VENDOR_SUBCMD_DFS_OFFLOAD_CAC_STARTED = 56,
	QCA_NL80211_VENDOR_SUBCMD_DFS_OFFLOAD_CAC_FINISHED = 57,
	QCA_NL80211_VENDOR_SUBCMD_DFS_OFFLOAD_CAC_ABORTED = 58,
	QCA_NL80211_VENDOR_SUBCMD_DFS_OFFLOAD_CAC_NOP_FINISHED = 59,
	QCA_NL80211_VENDOR_SUBCMD_DFS_OFFLOAD_RADAR_DETECTED = 60,
	QCA_NL80211_VENDOR_SUBCMD_GET_WIFI_INFO = 61,
	QCA_NL80211_VENDOR_SUBCMD_WIFI_LOGGER_START = 62,
	QCA_NL80211_VENDOR_SUBCMD_WIFI_LOGGER_MEMORY_DUMP = 63,
	QCA_NL80211_VENDOR_SUBCMD_ROAM = 64,
	QCA_NL80211_VENDOR_SUBCMD_GSCAN_SET_SSID_HOTLIST = 65,
	QCA_NL80211_VENDOR_SUBCMD_GSCAN_RESET_SSID_HOTLIST = 66,
	QCA_NL80211_VENDOR_SUBCMD_GSCAN_HOTLIST_SSID_FOUND = 67,
	QCA_NL80211_VENDOR_SUBCMD_GSCAN_HOTLIST_SSID_LOST = 68,
	QCA_NL80211_VENDOR_SUBCMD_PNO_SET_LIST = 69,
	QCA_NL80211_VENDOR_SUBCMD_PNO_SET_PASSPOINT_LIST = 70,
	QCA_NL80211_VENDOR_SUBCMD_PNO_RESET_PASSPOINT_LIST = 71,
	QCA_NL80211_VENDOR_SUBCMD_PNO_NETWORK_FOUND = 72,
	QCA_NL80211_VENDOR_SUBCMD_PNO_PASSPOINT_NETWORK_FOUND = 73,
	/* Wi-Fi configuration subcommands */
	QCA_NL80211_VENDOR_SUBCMD_SET_WIFI_CONFIGURATION = 74,
	QCA_NL80211_VENDOR_SUBCMD_GET_WIFI_CONFIGURATION = 75,
	QCA_NL80211_VENDOR_SUBCMD_GET_LOGGER_FEATURE_SET = 76,
	QCA_NL80211_VENDOR_SUBCMD_GET_RING_DATA = 77,
	QCA_NL80211_VENDOR_SUBCMD_TDLS_GET_CAPABILITIES = 78,
	QCA_NL80211_VENDOR_SUBCMD_OFFLOADED_PACKETS = 79,
	QCA_NL80211_VENDOR_SUBCMD_MONITOR_RSSI = 80,
	QCA_NL80211_VENDOR_SUBCMD_NDP = 81,
	QCA_NL80211_VENDOR_SUBCMD_ND_OFFLOAD = 82,
	QCA_NL80211_VENDOR_SUBCMD_PACKET_FILTER = 83,
	QCA_NL80211_VENDOR_SUBCMD_GET_BUS_SIZE = 84,
	QCA_NL80211_VENDOR_SUBCMD_GET_WAKE_REASON_STATS = 85,
	/* 86-90 - reserved for QCA */
	QCA_NL80211_VENDOR_SUBCMD_DATA_OFFLOAD = 91,
	QCA_NL80211_VENDOR_SUBCMD_OCB_SET_CONFIG = 92,
	QCA_NL80211_VENDOR_SUBCMD_OCB_SET_UTC_TIME = 93,
	QCA_NL80211_VENDOR_SUBCMD_OCB_START_TIMING_ADVERT = 94,
	QCA_NL80211_VENDOR_SUBCMD_OCB_STOP_TIMING_ADVERT = 95,
	QCA_NL80211_VENDOR_SUBCMD_OCB_GET_TSF_TIMER = 96,
	QCA_NL80211_VENDOR_SUBCMD_DCC_GET_STATS = 97,
	QCA_NL80211_VENDOR_SUBCMD_DCC_CLEAR_STATS = 98,
	QCA_NL80211_VENDOR_SUBCMD_DCC_UPDATE_NDL = 99,
	QCA_NL80211_VENDOR_SUBCMD_DCC_STATS_EVENT = 100,
	QCA_NL80211_VENDOR_SUBCMD_LINK_PROPERTIES = 101,
	QCA_NL80211_VENDOR_SUBCMD_GW_PARAM_CONFIG = 102,
	QCA_NL80211_VENDOR_SUBCMD_GET_PREFERRED_FREQ_LIST = 103,
	QCA_NL80211_VENDOR_SUBCMD_SET_PROBABLE_OPER_CHANNEL = 104,
	QCA_NL80211_VENDOR_SUBCMD_SETBAND = 105,
	QCA_NL80211_VENDOR_SUBCMD_TRIGGER_SCAN = 106,
	QCA_NL80211_VENDOR_SUBCMD_SCAN_DONE = 107,
	QCA_NL80211_VENDOR_SUBCMD_OTA_TEST = 108,
	QCA_NL80211_VENDOR_SUBCMD_SET_TXPOWER_SCALE = 109,
	/* 110..114 - reserved for QCA */
	QCA_NL80211_VENDOR_SUBCMD_SET_TXPOWER_DECR_DB = 115,
	/* 116..117 - reserved for QCA */
	QCA_NL80211_VENDOR_SUBCMD_SET_SAP_CONFIG = 118,
	QCA_NL80211_VENDOR_SUBCMD_TSF = 119,
	QCA_NL80211_VENDOR_SUBCMD_WISA = 120,
	/* 121 - reserved for QCA */
	QCA_NL80211_VENDOR_SUBCMD_P2P_LISTEN_OFFLOAD_START = 122,
	QCA_NL80211_VENDOR_SUBCMD_P2P_LISTEN_OFFLOAD_STOP = 123,
	QCA_NL80211_VENDOR_SUBCMD_SAP_CONDITIONAL_CHAN_SWITCH = 124,
	QCA_NL80211_VENDOR_SUBCMD_GPIO_CONFIG_COMMAND = 125,
	QCA_NL80211_VENDOR_SUBCMD_GET_HW_CAPABILITY = 126,
	QCA_NL80211_VENDOR_SUBCMD_LL_STATS_EXT = 127,
	/* FTM/indoor location subcommands */
	QCA_NL80211_VENDOR_SUBCMD_LOC_GET_CAPA = 128,
	QCA_NL80211_VENDOR_SUBCMD_FTM_START_SESSION = 129,
	QCA_NL80211_VENDOR_SUBCMD_FTM_ABORT_SESSION = 130,
	QCA_NL80211_VENDOR_SUBCMD_FTM_MEAS_RESULT = 131,
	QCA_NL80211_VENDOR_SUBCMD_FTM_SESSION_DONE = 132,
	QCA_NL80211_VENDOR_SUBCMD_FTM_CFG_RESPONDER = 133,
	QCA_NL80211_VENDOR_SUBCMD_AOA_MEAS = 134,
	QCA_NL80211_VENDOR_SUBCMD_AOA_ABORT_MEAS = 135,
	QCA_NL80211_VENDOR_SUBCMD_AOA_MEAS_RESULT = 136,
	QCA_NL80211_VENDOR_SUBCMD_ENCRYPTION_TEST = 137,
	QCA_NL80211_VENDOR_SUBCMD_GET_CHAIN_RSSI = 138,
	/* DMG low level RF sector operations */
	QCA_NL80211_VENDOR_SUBCMD_DMG_RF_GET_SECTOR_CFG = 139,
	QCA_NL80211_VENDOR_SUBCMD_DMG_RF_SET_SECTOR_CFG = 140,
	QCA_NL80211_VENDOR_SUBCMD_DMG_RF_GET_SELECTED_SECTOR = 141,
	QCA_NL80211_VENDOR_SUBCMD_DMG_RF_SET_SELECTED_SECTOR = 142,
	QCA_NL80211_VENDOR_SUBCMD_CONFIGURE_TDLS = 143,
	QCA_NL80211_VENDOR_SUBCMD_GET_HE_CAPABILITIES = 144,
	QCA_NL80211_VENDOR_SUBCMD_ABORT_SCAN = 145,
	QCA_NL80211_VENDOR_SUBCMD_SET_SAR_LIMITS = 146,
	QCA_NL80211_VENDOR_SUBCMD_EXTERNAL_ACS = 147,
	QCA_NL80211_VENDOR_SUBCMD_CHIP_PWRSAVE_FAILURE = 148,
	QCA_NL80211_VENDOR_SUBCMD_NUD_STATS_SET = 149,
	QCA_NL80211_VENDOR_SUBCMD_NUD_STATS_GET = 150,
	QCA_NL80211_VENDOR_SUBCMD_FETCH_BSS_TRANSITION_STATUS = 151,
	QCA_NL80211_VENDOR_SUBCMD_SET_TRACE_LEVEL = 152,
	QCA_NL80211_VENDOR_SUBCMD_BRP_SET_ANT_LIMIT = 153,
	QCA_NL80211_VENDOR_SUBCMD_SPECTRAL_SCAN_START = 154,
	QCA_NL80211_VENDOR_SUBCMD_SPECTRAL_SCAN_STOP = 155,
	QCA_NL80211_VENDOR_SUBCMD_ACTIVE_TOS = 156,
	QCA_NL80211_VENDOR_SUBCMD_HANG = 157,
	QCA_NL80211_VENDOR_SUBCMD_SPECTRAL_SCAN_GET_CONFIG = 158,
	QCA_NL80211_VENDOR_SUBCMD_SPECTRAL_SCAN_GET_DIAG_STATS = 159,
	QCA_NL80211_VENDOR_SUBCMD_SPECTRAL_SCAN_GET_CAP_INFO = 160,
	QCA_NL80211_VENDOR_SUBCMD_SPECTRAL_SCAN_GET_STATUS = 161,
	/* Flush peer pending data */
	QCA_NL80211_VENDOR_SUBCMD_PEER_FLUSH_PENDING = 162,
	QCA_NL80211_VENDOR_SUBCMD_GET_RROP_INFO = 163,
	QCA_NL80211_VENDOR_SUBCMD_GET_SAR_LIMITS = 164,
	QCA_NL80211_VENDOR_SUBCMD_WLAN_MAC_INFO = 165,
	QCA_NL80211_VENDOR_SUBCMD_SET_QDEPTH_THRESH = 166,
	/* Thermal shutdown commands to protect wifi chip */
	QCA_NL80211_VENDOR_SUBCMD_THERMAL_CMD = 167,
	QCA_NL80211_VENDOR_SUBCMD_THERMAL_EVENT = 168,
	/* Wi-Fi test configuration subcommand */
	QCA_NL80211_VENDOR_SUBCMD_WIFI_TEST_CONFIGURATION = 169,
	/* Frame filter operations for other BSSs/unassociated STAs */
	QCA_NL80211_VENDOR_SUBCMD_BSS_FILTER = 170,
	QCA_NL80211_VENDOR_SUBCMD_NAN_EXT = 171,
	QCA_NL80211_VENDOR_SUBCMD_ROAM_SCAN_EVENT = 172,
	QCA_NL80211_VENDOR_SUBCMD_PEER_CFR_CAPTURE_CFG = 173,
	QCA_NL80211_VENDOR_SUBCMD_THROUGHPUT_CHANGE_EVENT = 174,
	QCA_NL80211_VENDOR_SUBCMD_COEX_CONFIG = 175,
};

enum qca_wlan_vendor_attr {
	QCA_WLAN_VENDOR_ATTR_INVALID = 0,
	/* used by QCA_NL80211_VENDOR_SUBCMD_DFS_CAPABILITY */
	QCA_WLAN_VENDOR_ATTR_DFS     = 1,
	/* Used only when driver sends vendor events to the userspace under the
	 * command QCA_NL80211_VENDOR_SUBCMD_NAN. Not used when userspace sends
	 * commands to the driver.
	 */
	QCA_WLAN_VENDOR_ATTR_NAN     = 2,
	/* used by QCA_NL80211_VENDOR_SUBCMD_STATS_EXT */
	QCA_WLAN_VENDOR_ATTR_STATS_EXT     = 3,
	/* used by QCA_NL80211_VENDOR_SUBCMD_STATS_EXT */
	QCA_WLAN_VENDOR_ATTR_IFINDEX     = 4,
	/* used by QCA_NL80211_VENDOR_SUBCMD_ROAMING, u32 with values defined
	 * by enum qca_roaming_policy.
	 */
	QCA_WLAN_VENDOR_ATTR_ROAMING_POLICY = 5,
	QCA_WLAN_VENDOR_ATTR_MAC_ADDR = 6,
	/* used by QCA_NL80211_VENDOR_SUBCMD_GET_FEATURES */
	QCA_WLAN_VENDOR_ATTR_FEATURE_FLAGS = 7,
	QCA_WLAN_VENDOR_ATTR_TEST = 8,
	/* used by QCA_NL80211_VENDOR_SUBCMD_GET_FEATURES */
	/* Unsigned 32-bit value. */
	QCA_WLAN_VENDOR_ATTR_CONCURRENCY_CAPA = 9,
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_MAX_CONCURRENT_CHANNELS_2_4_BAND = 10,
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_MAX_CONCURRENT_CHANNELS_5_0_BAND = 11,
	/* Unsigned 32-bit value from enum qca_set_band. */
	QCA_WLAN_VENDOR_ATTR_SETBAND_VALUE = 12,
	/* Dummy (NOP) attribute for 64 bit padding */
	QCA_WLAN_VENDOR_ATTR_PAD = 13,
	/* Unique FTM session cookie (Unsigned 64 bit). Specified in
	 * QCA_NL80211_VENDOR_SUBCMD_FTM_START_SESSION. Reported in
	 * the session in QCA_NL80211_VENDOR_SUBCMD_FTM_MEAS_RESULT and
	 * QCA_NL80211_VENDOR_SUBCMD_FTM_SESSION_DONE.
	 */
	QCA_WLAN_VENDOR_ATTR_FTM_SESSION_COOKIE = 14,
	/* Indoor location capabilities, returned by
	 * QCA_NL80211_VENDOR_SUBCMD_LOC_GET_CAPA.
	 * see enum qca_wlan_vendor_attr_loc_capa.
	 */
	QCA_WLAN_VENDOR_ATTR_LOC_CAPA = 15,
	/* Array of nested attributes containing information about each peer
	 * in FTM measurement session. See enum qca_wlan_vendor_attr_peer_info
	 * for supported attributes for each peer.
	 */
	QCA_WLAN_VENDOR_ATTR_FTM_MEAS_PEERS = 16,
	/* Array of nested attributes containing measurement results for
	 * one or more peers, reported by the
	 * QCA_NL80211_VENDOR_SUBCMD_FTM_MEAS_RESULT event.
	 * See enum qca_wlan_vendor_attr_peer_result for list of supported
	 * attributes.
	 */
	QCA_WLAN_VENDOR_ATTR_FTM_MEAS_PEER_RESULTS = 17,
	/* Flag attribute for enabling or disabling responder functionality. */
	QCA_WLAN_VENDOR_ATTR_FTM_RESPONDER_ENABLE = 18,
	/* Used in the QCA_NL80211_VENDOR_SUBCMD_FTM_CFG_RESPONDER
	 * command to specify the LCI report that will be sent by
	 * the responder during a measurement exchange. The format is
	 * defined in IEEE P802.11-REVmc/D7.0, 9.4.2.22.10.
	 */
	QCA_WLAN_VENDOR_ATTR_FTM_LCI = 19,
	/* Used in the QCA_NL80211_VENDOR_SUBCMD_FTM_CFG_RESPONDER
	 * command to specify the location civic report that will
	 * be sent by the responder during a measurement exchange.
	 * The format is defined in IEEE P802.11-REVmc/D7.0, 9.4.2.22.13.
	 */
	QCA_WLAN_VENDOR_ATTR_FTM_LCR = 20,
	/* Session/measurement completion status code,
	 * reported in QCA_NL80211_VENDOR_SUBCMD_FTM_SESSION_DONE and
	 * QCA_NL80211_VENDOR_SUBCMD_AOA_MEAS_RESULT
	 * see enum qca_vendor_attr_loc_session_status.
	 */
	QCA_WLAN_VENDOR_ATTR_LOC_SESSION_STATUS = 21,
	/* Initial dialog token used by responder (0 if not specified),
	 * unsigned 8 bit value.
	 */
	QCA_WLAN_VENDOR_ATTR_FTM_INITIAL_TOKEN = 22,
	/* AOA measurement type. Requested in QCA_NL80211_VENDOR_SUBCMD_AOA_MEAS
	 * and optionally in QCA_NL80211_VENDOR_SUBCMD_FTM_START_SESSION if
	 * AOA measurements are needed as part of an FTM session.
	 * Reported by QCA_NL80211_VENDOR_SUBCMD_AOA_MEAS_RESULT. See
	 * enum qca_wlan_vendor_attr_aoa_type.
	 */
	QCA_WLAN_VENDOR_ATTR_AOA_TYPE = 23,
	/* A bit mask (unsigned 32 bit value) of antenna arrays used
	 * by indoor location measurements. Refers to the antenna
	 * arrays described by QCA_VENDOR_ATTR_LOC_CAPA_ANTENNA_ARRAYS.
	 */
	QCA_WLAN_VENDOR_ATTR_LOC_ANTENNA_ARRAY_MASK = 24,
	/* AOA measurement data. Its contents depends on the AOA measurement
	 * type and antenna array mask:
	 * QCA_WLAN_VENDOR_ATTR_AOA_TYPE_TOP_CIR_PHASE: array of U16 values,
	 * phase of the strongest CIR path for each antenna in the measured
	 * array(s).
	 * QCA_WLAN_VENDOR_ATTR_AOA_TYPE_TOP_CIR_PHASE_AMP: array of 2 U16
	 * values, phase and amplitude of the strongest CIR path for each
	 * antenna in the measured array(s).
	 */
	QCA_WLAN_VENDOR_ATTR_AOA_MEAS_RESULT = 25,
	/* Used in QCA_NL80211_VENDOR_SUBCMD_GET_CHAIN_RSSI command
	 * to specify the chain number (unsigned 32 bit value) to inquire
	 * the corresponding antenna RSSI value
	 */
	QCA_WLAN_VENDOR_ATTR_CHAIN_INDEX = 26,
	/* Used in QCA_NL80211_VENDOR_SUBCMD_GET_CHAIN_RSSI command
	 * to report the specific antenna RSSI value (unsigned 32 bit value)
	 */
	QCA_WLAN_VENDOR_ATTR_CHAIN_RSSI = 27,
	/* Frequency in MHz, various uses. Unsigned 32 bit value */
	QCA_WLAN_VENDOR_ATTR_FREQ = 28,
	/* TSF timer value, unsigned 64 bit value.
	 * May be returned by various commands.
	 */
	QCA_WLAN_VENDOR_ATTR_TSF = 29,
	/* DMG RF sector index, unsigned 16 bit number. Valid values are
	 * 0..127 for sector indices or 65535 as special value used to
	 * unlock sector selection in
	 * QCA_NL80211_VENDOR_SUBCMD_DMG_RF_SET_SELECTED_SECTOR.
	 */
	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_INDEX = 30,
	/* DMG RF sector type, unsigned 8 bit value. One of the values
	 * in enum qca_wlan_vendor_attr_dmg_rf_sector_type.
	 */
	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_TYPE = 31,
	/* Bitmask of DMG RF modules for which information is requested. Each
	 * bit corresponds to an RF module with the same index as the bit
	 * number. Unsigned 32 bit number but only low 8 bits can be set since
	 * all DMG chips currently have up to 8 RF modules.
	 */
	QCA_WLAN_VENDOR_ATTR_DMG_RF_MODULE_MASK = 32,
	/* Array of nested attributes where each entry is DMG RF sector
	 * configuration for a single RF module.
	 * Attributes for each entry are taken from enum
	 * qca_wlan_vendor_attr_dmg_rf_sector_cfg.
	 * Specified in QCA_NL80211_VENDOR_SUBCMD_DMG_RF_SET_SECTOR_CFG
	 * and returned by QCA_NL80211_VENDOR_SUBCMD_DMG_RF_GET_SECTOR_CFG.
	 */
	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_CFG = 33,
	/* Used in QCA_NL80211_VENDOR_SUBCMD_STATS_EXT command
	 * to report frame aggregation statistics to userspace.
	 */
	QCA_WLAN_VENDOR_ATTR_RX_AGGREGATION_STATS_HOLES_NUM = 34,
	QCA_WLAN_VENDOR_ATTR_RX_AGGREGATION_STATS_HOLES_INFO = 35,
	/* Unsigned 8-bit value representing MBO transition reason code as
	 * provided by the AP used by subcommand
	 * QCA_NL80211_VENDOR_SUBCMD_FETCH_BSS_TRANSITION_STATUS. This is
	 * specified by the userspace in the request to the driver.
	 */
	QCA_WLAN_VENDOR_ATTR_BTM_MBO_TRANSITION_REASON = 36,
	/* Array of nested attributes, BSSID and status code, used by subcommand
	 * QCA_NL80211_VENDOR_SUBCMD_FETCH_BSS_TRANSITION_STATUS, where each
	 * entry is taken from enum qca_wlan_vendor_attr_btm_candidate_info.
	 * The userspace space specifies the list/array of candidate BSSIDs in
	 * the order of preference in the request. The driver specifies the
	 * status code, for each BSSID in the list, in the response. The
	 * acceptable candidates are listed in the order preferred by the
	 * driver.
	 */
	QCA_WLAN_VENDOR_ATTR_BTM_CANDIDATE_INFO = 37,
	/* Used in QCA_NL80211_VENDOR_SUBCMD_BRP_SET_ANT_LIMIT command
	 * See enum qca_wlan_vendor_attr_brp_ant_limit_mode.
	 */
	QCA_WLAN_VENDOR_ATTR_BRP_ANT_LIMIT_MODE = 38,
	/* Used in QCA_NL80211_VENDOR_SUBCMD_BRP_SET_ANT_LIMIT command
	 * to define the number of antennas to use for BRP.
	 * different purpose in each ANT_LIMIT_MODE:
	 * DISABLE - ignored
	 * EFFECTIVE - upper limit to number of antennas to be used
	 * FORCE - exact number of antennas to be used
	 * unsigned 8 bit value
	 */
	QCA_WLAN_VENDOR_ATTR_BRP_ANT_NUM_LIMIT = 39,
	/* Used in QCA_NL80211_VENDOR_SUBCMD_GET_CHAIN_RSSI command
	 * to report the corresponding antenna index to the chain RSSI value
	 */
	QCA_WLAN_VENDOR_ATTR_ANTENNA_INFO = 40,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_MAX	= QCA_WLAN_VENDOR_ATTR_AFTER_LAST - 1,
};

enum qca_roaming_policy {
	QCA_ROAMING_NOT_ALLOWED,
	QCA_ROAMING_ALLOWED_WITHIN_ESS,
};

enum qca_wlan_vendor_attr_roam_auth {
	QCA_WLAN_VENDOR_ATTR_ROAM_AUTH_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_ROAM_AUTH_BSSID,
	QCA_WLAN_VENDOR_ATTR_ROAM_AUTH_REQ_IE,
	QCA_WLAN_VENDOR_ATTR_ROAM_AUTH_RESP_IE,
	QCA_WLAN_VENDOR_ATTR_ROAM_AUTH_AUTHORIZED,
	QCA_WLAN_VENDOR_ATTR_ROAM_AUTH_KEY_REPLAY_CTR,
	QCA_WLAN_VENDOR_ATTR_ROAM_AUTH_PTK_KCK,
	QCA_WLAN_VENDOR_ATTR_ROAM_AUTH_PTK_KEK,
	QCA_WLAN_VENDOR_ATTR_ROAM_AUTH_SUBNET_STATUS,
	/* Indicates the status of re-association requested by user space for
	 * the BSSID specified by QCA_WLAN_VENDOR_ATTR_ROAM_AUTH_BSSID.
	 * Type u16.
	 * Represents the status code from AP. Use
	 * %WLAN_STATUS_UNSPECIFIED_FAILURE if the device cannot give you the
	 * real status code for failures.
	 */
	QCA_WLAN_VENDOR_ATTR_ROAM_AUTH_STATUS,
	/* This attribute indicates that the old association was maintained when
	 * a re-association is requested by user space and that re-association
	 * attempt fails (i.e., cannot connect to the requested BSS, but can
	 * remain associated with the BSS with which the association was in
	 * place when being requested to roam). Used along with
	 * WLAN_VENDOR_ATTR_ROAM_AUTH_STATUS to indicate the current
	 * re-association status. Type flag.
	 * This attribute is applicable only for re-association failure cases.
	 */
	QCA_WLAN_VENDOR_ATTR_ROAM_AUTH_RETAIN_CONNECTION,
	/* This attribute specifies the PMK if one was newly generated during
	 * FILS roaming. This is added to the PMKSA cache and is used in
	 * subsequent connections with PMKSA caching.
	 */
	QCA_WLAN_VENDOR_ATTR_ROAM_AUTH_PMK = 11,
	/* This attribute specifies the PMKID used/generated for the current
	 * FILS roam. This is used in subsequent connections with PMKSA caching.
	 */
	QCA_WLAN_VENDOR_ATTR_ROAM_AUTH_PMKID = 12,
	/* A 16-bit unsigned value specifying the next sequence number to use
	 * in ERP message in the currently associated realm. This is used in
	 * doing subsequent ERP based connections in the same realm.
	 */
	QCA_WLAN_VENDOR_ATTR_ROAM_AUTH_FILS_ERP_NEXT_SEQ_NUM = 13,
	/* keep last */
	QCA_WLAN_VENDOR_ATTR_ROAM_AUTH_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_ROAM_AUTH_MAX =
	QCA_WLAN_VENDOR_ATTR_ROAM_AUTH_AFTER_LAST - 1
};

enum qca_wlan_vendor_attr_p2p_listen_offload {
	QCA_WLAN_VENDOR_ATTR_P2P_LISTEN_OFFLOAD_INVALID = 0,
	/* A 32-bit unsigned value; the P2P listen frequency (MHz); must be one
	 * of the social channels.
	 */
	QCA_WLAN_VENDOR_ATTR_P2P_LISTEN_OFFLOAD_CHANNEL,
	/* A 32-bit unsigned value; the P2P listen offload period (ms).
	 */
	QCA_WLAN_VENDOR_ATTR_P2P_LISTEN_OFFLOAD_PERIOD,
	/* A 32-bit unsigned value; the P2P listen interval duration (ms).
	 */
	QCA_WLAN_VENDOR_ATTR_P2P_LISTEN_OFFLOAD_INTERVAL,
	/* A 32-bit unsigned value; number of interval times the firmware needs
	 * to run the offloaded P2P listen operation before it stops.
	 */
	QCA_WLAN_VENDOR_ATTR_P2P_LISTEN_OFFLOAD_COUNT,
	/* An array of arbitrary binary data with one or more 8-byte values.
	 * The device types include both primary and secondary device types.
	 */
	QCA_WLAN_VENDOR_ATTR_P2P_LISTEN_OFFLOAD_DEVICE_TYPES,
	/* An array of unsigned 8-bit characters; vendor information elements.
	 */
	QCA_WLAN_VENDOR_ATTR_P2P_LISTEN_OFFLOAD_VENDOR_IE,
	/* A 32-bit unsigned value; a control flag to indicate whether listen
	 * results need to be flushed to wpa_supplicant.
	 */
	QCA_WLAN_VENDOR_ATTR_P2P_LISTEN_OFFLOAD_CTRL_FLAG,
	/* A 8-bit unsigned value; reason code for P2P listen offload stop
	 * event.
	 */
	QCA_WLAN_VENDOR_ATTR_P2P_LISTEN_OFFLOAD_STOP_REASON,
	/* keep last */
	QCA_WLAN_VENDOR_ATTR_P2P_LISTEN_OFFLOAD_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_P2P_LISTEN_OFFLOAD_MAX =
	QCA_WLAN_VENDOR_ATTR_P2P_LISTEN_OFFLOAD_AFTER_LAST - 1
};

enum qca_wlan_vendor_attr_acs_offload {
	QCA_WLAN_VENDOR_ATTR_ACS_CHANNEL_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_ACS_PRIMARY_CHANNEL,
	QCA_WLAN_VENDOR_ATTR_ACS_SECONDARY_CHANNEL,
	QCA_WLAN_VENDOR_ATTR_ACS_HW_MODE,
	QCA_WLAN_VENDOR_ATTR_ACS_HT_ENABLED,
	QCA_WLAN_VENDOR_ATTR_ACS_HT40_ENABLED,
	QCA_WLAN_VENDOR_ATTR_ACS_VHT_ENABLED,
	QCA_WLAN_VENDOR_ATTR_ACS_CHWIDTH,
	QCA_WLAN_VENDOR_ATTR_ACS_CH_LIST,
	QCA_WLAN_VENDOR_ATTR_ACS_VHT_SEG0_CENTER_CHANNEL,
	QCA_WLAN_VENDOR_ATTR_ACS_VHT_SEG1_CENTER_CHANNEL,
	QCA_WLAN_VENDOR_ATTR_ACS_FREQ_LIST,
	/* keep last */
	QCA_WLAN_VENDOR_ATTR_ACS_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_ACS_MAX =
	QCA_WLAN_VENDOR_ATTR_ACS_AFTER_LAST - 1
};

enum qca_wlan_vendor_acs_hw_mode {
	QCA_ACS_MODE_IEEE80211B,
	QCA_ACS_MODE_IEEE80211G,
	QCA_ACS_MODE_IEEE80211A,
	QCA_ACS_MODE_IEEE80211AD,
	QCA_ACS_MODE_IEEE80211ANY,
};

/**
 * enum qca_wlan_vendor_features - Vendor device/driver feature flags
 *
 * @QCA_WLAN_VENDOR_FEATURE_KEY_MGMT_OFFLOAD: Device supports key
 *	management offload, a mechanism where the station's firmware
 *	does the exchange with the AP to establish the temporal keys
 *	after roaming, rather than having the user space wpa_supplicant do it.
 * @QCA_WLAN_VENDOR_FEATURE_SUPPORT_HW_MODE_ANY: Device supports automatic
 *	band selection based on channel selection results.
 * @QCA_WLAN_VENDOR_FEATURE_OFFCHANNEL_SIMULTANEOUS: Device supports
 *	simultaneous off-channel operations.
 * @QCA_WLAN_VENDOR_FEATURE_P2P_LISTEN_OFFLOAD: Device supports P2P
 *	Listen offload; a mechanism where the station's firmware takes care of
 *	responding to incoming Probe Request frames received from other P2P
 *	Devices whilst in Listen state, rather than having the user space
 *	wpa_supplicant do it. Information from received P2P requests are
 *	forwarded from firmware to host whenever the host processor wakes up.
 * @QCA_WLAN_VENDOR_FEATURE_OCE_STA: Device supports all OCE non-AP STA
 *	specific features.
 * @QCA_WLAN_VENDOR_FEATURE_OCE_AP: Device supports all OCE AP specific
 *	features.
 * @QCA_WLAN_VENDOR_FEATURE_OCE_STA_CFON: Device supports OCE STA-CFON
 *	specific features only. If a Device sets this bit but not the
 *	%QCA_WLAN_VENDOR_FEATURE_OCE_AP, the userspace shall assume that
 *	this Device may not support all OCE AP functionalities but can support
 *	only OCE STA-CFON functionalities.
 * @QCA_WLAN_VENDOR_FEATURE_SELF_MANAGED_REGULATORY: Device supports self
 *	managed regulatory.
 * @NUM_QCA_WLAN_VENDOR_FEATURES: Number of assigned feature bits
 */
enum qca_wlan_vendor_features {
	QCA_WLAN_VENDOR_FEATURE_KEY_MGMT_OFFLOAD	= 0,
	QCA_WLAN_VENDOR_FEATURE_SUPPORT_HW_MODE_ANY     = 1,
	QCA_WLAN_VENDOR_FEATURE_OFFCHANNEL_SIMULTANEOUS = 2,
	QCA_WLAN_VENDOR_FEATURE_P2P_LISTEN_OFFLOAD	= 3,
	QCA_WLAN_VENDOR_FEATURE_OCE_STA                 = 4,
	QCA_WLAN_VENDOR_FEATURE_OCE_AP                  = 5,
	QCA_WLAN_VENDOR_FEATURE_OCE_STA_CFON            = 6,
	QCA_WLAN_VENDOR_FEATURE_SELF_MANAGED_REGULATORY = 7,
	NUM_QCA_WLAN_VENDOR_FEATURES /* keep last */
};

/**
 * enum qca_wlan_vendor_attr_data_offload_ind - Vendor Data Offload Indication
 *
 * @QCA_WLAN_VENDOR_ATTR_DATA_OFFLOAD_IND_SESSION: Session corresponding to
 *	the offloaded data.
 * @QCA_WLAN_VENDOR_ATTR_DATA_OFFLOAD_IND_PROTOCOL: Protocol of the offloaded
 *	data.
 * @QCA_WLAN_VENDOR_ATTR_DATA_OFFLOAD_IND_EVENT: Event type for the data offload
 *	indication.
 */
enum qca_wlan_vendor_attr_data_offload_ind {
	QCA_WLAN_VENDOR_ATTR_DATA_OFFLOAD_IND_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_DATA_OFFLOAD_IND_SESSION,
	QCA_WLAN_VENDOR_ATTR_DATA_OFFLOAD_IND_PROTOCOL,
	QCA_WLAN_VENDOR_ATTR_DATA_OFFLOAD_IND_EVENT,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_DATA_OFFLOAD_IND_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_DATA_OFFLOAD_IND_MAX =
	QCA_WLAN_VENDOR_ATTR_DATA_OFFLOAD_IND_AFTER_LAST - 1
};

/**
 * enum qca_wlan_vendor_attr_ocb_set_config - Vendor subcmd attributes to set
 *	OCB config
 *
 * @QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_CHANNEL_COUNT: Number of channels in the
 *	configuration
 * @QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_SCHEDULE_SIZE: Size of the schedule
 * @QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_CHANNEL_ARRAY: Array of channels
 * @QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_SCHEDULE_ARRAY: Array of channels to be
 *	scheduled
 * @QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_NDL_CHANNEL_ARRAY: Array of NDL channel
 *	information
 * @QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_NDL_ACTIVE_STATE_ARRAY: Array of NDL
 *	active state configuration
 * @QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_FLAGS: Configuration flags such as
 *	OCB_CONFIG_FLAG_80211_FRAME_MODE
 * @QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_DEF_TX_PARAM: Default TX parameters to
 *	use in the case that a packet is sent without a TX control header
 * @QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_TA_MAX_DURATION: Max duration after the
 *	last TA received that the local time set by TA is synchronous to other
 *	communicating OCB STAs.
 */
enum qca_wlan_vendor_attr_ocb_set_config {
	QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_CHANNEL_COUNT = 1,
	QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_SCHEDULE_SIZE = 2,
	QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_CHANNEL_ARRAY = 3,
	QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_SCHEDULE_ARRAY = 4,
	QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_NDL_CHANNEL_ARRAY = 5,
	QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_NDL_ACTIVE_STATE_ARRAY = 6,
	QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_FLAGS = 7,
	QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_DEF_TX_PARAM = 8,
	QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_TA_MAX_DURATION = 9,
	QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_MAX =
	QCA_WLAN_VENDOR_ATTR_OCB_SET_CONFIG_AFTER_LAST - 1
};

/**
 * enum qca_wlan_vendor_attr_ocb_set_utc_time - Vendor subcmd attributes to set
 *	UTC time
 *
 * @QCA_WLAN_VENDOR_ATTR_OCB_SET_UTC_TIME_VALUE: The UTC time as an array of
 *	10 bytes
 * @QCA_WLAN_VENDOR_ATTR_OCB_SET_UTC_TIME_ERROR: The time error as an array of
 *	5 bytes
 */
enum qca_wlan_vendor_attr_ocb_set_utc_time {
	QCA_WLAN_VENDOR_ATTR_OCB_SET_UTC_TIME_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_OCB_SET_UTC_TIME_VALUE = 1,
	QCA_WLAN_VENDOR_ATTR_OCB_SET_UTC_TIME_ERROR = 2,
	QCA_WLAN_VENDOR_ATTR_OCB_SET_UTC_TIME_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_OCB_SET_UTC_TIME_MAX =
	QCA_WLAN_VENDOR_ATTR_OCB_SET_UTC_TIME_AFTER_LAST - 1
};

/**
 * enum qca_wlan_vendor_attr_ocb_start_timing_advert - Vendor subcmd attributes
 *	to start sending timing advert frames
 *
 * @QCA_WLAN_VENDOR_ATTR_OCB_START_TIMING_ADVERT_CHANNEL_FREQ: Cannel frequency
 *	on which to send the frames
 * @QCA_WLAN_VENDOR_ATTR_OCB_START_TIMING_ADVERT_REPEAT_RATE: Number of times
 *	the frame is sent in 5 seconds
 */
enum qca_wlan_vendor_attr_ocb_start_timing_advert {
	QCA_WLAN_VENDOR_ATTR_OCB_START_TIMING_ADVERT_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_OCB_START_TIMING_ADVERT_CHANNEL_FREQ = 1,
	QCA_WLAN_VENDOR_ATTR_OCB_START_TIMING_ADVERT_REPEAT_RATE = 2,
	QCA_WLAN_VENDOR_ATTR_OCB_START_TIMING_ADVERT_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_OCB_START_TIMING_ADVERT_MAX =
	QCA_WLAN_VENDOR_ATTR_OCB_START_TIMING_ADVERT_AFTER_LAST - 1
};

/**
 * enum qca_wlan_vendor_attr_ocb_stop_timing_advert - Vendor subcmd attributes
 *	to stop timing advert
 *
 * @QCA_WLAN_VENDOR_ATTR_OCB_STOP_TIMING_ADVERT_CHANNEL_FREQ: The channel
 *	frequency on which to stop the timing advert
 */
enum qca_wlan_vendor_attr_ocb_stop_timing_advert {
	QCA_WLAN_VENDOR_ATTR_OCB_STOP_TIMING_ADVERT_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_OCB_STOP_TIMING_ADVERT_CHANNEL_FREQ = 1,
	QCA_WLAN_VENDOR_ATTR_OCB_STOP_TIMING_ADVERT_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_OCB_STOP_TIMING_ADVERT_MAX =
	QCA_WLAN_VENDOR_ATTR_OCB_STOP_TIMING_ADVERT_AFTER_LAST - 1
};

/**
 * enum qca_wlan_vendor_attr_ocb_get_tsf_response - Vendor subcmd attributes to
 *	get TSF timer value
 *
 * @QCA_WLAN_VENDOR_ATTR_OCB_GET_TSF_RESP_TIMER_HIGH: Higher 32 bits of the
 *	timer
 * @QCA_WLAN_VENDOR_ATTR_OCB_GET_TSF_RESP_TIMER_LOW: Lower 32 bits of the timer
 */
enum qca_wlan_vendor_attr_ocb_get_tsf_resp {
	QCA_WLAN_VENDOR_ATTR_OCB_GET_TSF_RESP_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_OCB_GET_TSF_RESP_TIMER_HIGH = 1,
	QCA_WLAN_VENDOR_ATTR_OCB_GET_TSF_RESP_TIMER_LOW = 2,
	QCA_WLAN_VENDOR_ATTR_OCB_GET_TSF_RESP_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_OCB_GET_TSF_RESP_MAX =
	QCA_WLAN_VENDOR_ATTR_OCB_GET_TSF_RESP_AFTER_LAST - 1
};

enum qca_vendor_attr_get_preferred_freq_list {
	QCA_WLAN_VENDOR_ATTR_GET_PREFERRED_FREQ_LIST_INVALID,
	/* A 32-unsigned value; the interface type/mode for which the preferred
	 * frequency list is requested (see enum qca_iface_type for possible
	 * values); used in GET_PREFERRED_FREQ_LIST command from user-space to
	 * kernel and in the kernel response back to user-space.
	 */
	QCA_WLAN_VENDOR_ATTR_GET_PREFERRED_FREQ_LIST_IFACE_TYPE,
	/* An array of 32-unsigned values; values are frequency (MHz); sent
	 * from kernel space to user space.
	 */
	QCA_WLAN_VENDOR_ATTR_GET_PREFERRED_FREQ_LIST,
	/* An array of nested values as per enum qca_wlan_vendor_attr_pcl
	 * attribute. Each element contains frequency (MHz), weight, and flag
	 * bit mask indicating how the frequency should be used in P2P
	 * negotiation; sent from kernel space to user space.
	 */
	QCA_WLAN_VENDOR_ATTR_GET_PREFERRED_FREQ_LIST_WEIGHED_PCL,
	/* keep last */
	QCA_WLAN_VENDOR_ATTR_GET_PREFERRED_FREQ_LIST_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_GET_PREFERRED_FREQ_LIST_MAX =
	QCA_WLAN_VENDOR_ATTR_GET_PREFERRED_FREQ_LIST_AFTER_LAST - 1
};

enum qca_vendor_attr_probable_oper_channel {
	QCA_WLAN_VENDOR_ATTR_PROBABLE_OPER_CHANNEL_INVALID,
	/* 32-bit unsigned value; indicates the connection/iface type likely to
	 * come on this channel (see enum qca_iface_type).
	 */
	QCA_WLAN_VENDOR_ATTR_PROBABLE_OPER_CHANNEL_IFACE_TYPE,
	/* 32-bit unsigned value; the frequency (MHz) of the probable channel */
	QCA_WLAN_VENDOR_ATTR_PROBABLE_OPER_CHANNEL_FREQ,
	/* keep last */
	QCA_WLAN_VENDOR_ATTR_PROBABLE_OPER_CHANNEL_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_PROBABLE_OPER_CHANNEL_MAX =
	QCA_WLAN_VENDOR_ATTR_PROBABLE_OPER_CHANNEL_AFTER_LAST - 1
};

enum qca_iface_type {
	QCA_IFACE_TYPE_STA,
	QCA_IFACE_TYPE_AP,
	QCA_IFACE_TYPE_P2P_CLIENT,
	QCA_IFACE_TYPE_P2P_GO,
	QCA_IFACE_TYPE_IBSS,
	QCA_IFACE_TYPE_TDLS,
};

enum qca_set_band {
	QCA_SETBAND_AUTO,
	QCA_SETBAND_5G,
	QCA_SETBAND_2G,
};

/**
 * enum qca_access_policy - Access control policy
 *
 * Access control policy is applied on the configured IE
 * (QCA_WLAN_VENDOR_ATTR_CONFIG_ACCESS_POLICY_IE).
 * To be set with QCA_WLAN_VENDOR_ATTR_CONFIG_ACCESS_POLICY.
 *
 * @QCA_ACCESS_POLICY_ACCEPT_UNLESS_LISTED: Deny Wi-Fi connections which match
 *	the specific configuration (IE) set, i.e., allow all the
 *	connections which do not match the configuration.
 * @QCA_ACCESS_POLICY_DENY_UNLESS_LISTED: Accept Wi-Fi connections which match
 *	the specific configuration (IE) set, i.e., deny all the
 *	connections which do not match the configuration.
 */
enum qca_access_policy {
	QCA_ACCESS_POLICY_ACCEPT_UNLESS_LISTED,
	QCA_ACCESS_POLICY_DENY_UNLESS_LISTED,
};

/**
 * enum qca_vendor_attr_get_tsf: Vendor attributes for TSF capture
 * @QCA_WLAN_VENDOR_ATTR_TSF_CMD: enum qca_tsf_operation (u32)
 * @QCA_WLAN_VENDOR_ATTR_TSF_TIMER_VALUE: Unsigned 64 bit TSF timer value
 * @QCA_WLAN_VENDOR_ATTR_TSF_SOC_TIMER_VALUE: Unsigned 64 bit Synchronized
 *	SOC timer value at TSF capture
 */
enum qca_vendor_attr_tsf_cmd {
	QCA_WLAN_VENDOR_ATTR_TSF_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_TSF_CMD,
	QCA_WLAN_VENDOR_ATTR_TSF_TIMER_VALUE,
	QCA_WLAN_VENDOR_ATTR_TSF_SOC_TIMER_VALUE,
	QCA_WLAN_VENDOR_ATTR_TSF_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_TSF_MAX =
	QCA_WLAN_VENDOR_ATTR_TSF_AFTER_LAST - 1
};

/**
 * enum qca_tsf_operation: TSF driver commands
 * @QCA_TSF_CAPTURE: Initiate TSF Capture
 * @QCA_TSF_GET: Get TSF capture value
 * @QCA_TSF_SYNC_GET: Initiate TSF capture and return with captured value
 */
enum qca_tsf_cmd {
	QCA_TSF_CAPTURE,
	QCA_TSF_GET,
	QCA_TSF_SYNC_GET,
};

/**
 * enum qca_vendor_attr_wisa_cmd
 * @QCA_WLAN_VENDOR_ATTR_WISA_MODE: WISA mode value (u32)
 * WISA setup vendor commands
 */
enum qca_vendor_attr_wisa_cmd {
	QCA_WLAN_VENDOR_ATTR_WISA_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_WISA_MODE,
	QCA_WLAN_VENDOR_ATTR_WISA_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_WISA_MAX =
	QCA_WLAN_VENDOR_ATTR_WISA_AFTER_LAST - 1
};

/* IEEE 802.11 Vendor Specific elements */

/**
 * enum qca_vendor_element_id - QCA Vendor Specific element types
 *
 * These values are used to identify QCA Vendor Specific elements. The
 * payload of the element starts with the three octet OUI (OUI_QCA) and
 * is followed by a single octet type which is defined by this enum.
 *
 * @QCA_VENDOR_ELEM_P2P_PREF_CHAN_LIST: P2P preferred channel list.
 *	This element can be used to specify preference order for supported
 *	channels. The channels in this list are in preference order (the first
 *	one has the highest preference) and are described as a pair of
 *	(global) Operating Class and Channel Number (each one octet) fields.
 *
 *	This extends the standard P2P functionality by providing option to have
 *	more than one preferred operating channel. When this element is present,
 *	it replaces the preference indicated in the Operating Channel attribute.
 *	For supporting other implementations, the Operating Channel attribute is
 *	expected to be used with the highest preference channel. Similarly, all
 *	the channels included in this Preferred channel list element are
 *	expected to be included in the Channel List attribute.
 *
 *	This vendor element may be included in GO Negotiation Request, P2P
 *	Invitation Request, and Provision Discovery Request frames.
 *
 * @QCA_VENDOR_ELEM_HE_CAPAB: HE Capabilities element.
 *	This element can be used for pre-standard publication testing of HE
 *	before P802.11ax draft assigns the element ID. The payload of this
 *	vendor specific element is defined by the latest P802.11ax draft.
 *	Please note that the draft is still work in progress and this element
 *	payload is subject to change.
 *
 * @QCA_VENDOR_ELEM_HE_OPER: HE Operation element.
 *	This element can be used for pre-standard publication testing of HE
 *	before P802.11ax draft assigns the element ID. The payload of this
 *	vendor specific element is defined by the latest P802.11ax draft.
 *	Please note that the draft is still work in progress and this element
 *	payload is subject to change.
 *
 * @QCA_VENDOR_ELEM_RAPS: RAPS element (OFDMA-based Random Access Parameter Set
 *	element).
 *	This element can be used for pre-standard publication testing of HE
 *	before P802.11ax draft assigns the element ID extension. The payload of
 *	this vendor specific element is defined by the latest P802.11ax draft
 *	(not including the Element ID Extension field). Please note that the
 *	draft is still work in progress and this element payload is subject to
 *	change.
 *
 * @QCA_VENDOR_ELEM_MU_EDCA_PARAMS: MU EDCA Parameter Set element.
 *	This element can be used for pre-standard publication testing of HE
 *	before P802.11ax draft assigns the element ID extension. The payload of
 *	this vendor specific element is defined by the latest P802.11ax draft
 *	(not including the Element ID Extension field). Please note that the
 *	draft is still work in progress and this element payload is subject to
 *	change.
 *
 * @QCA_VENDOR_ELEM_BSS_COLOR_CHANGE: BSS Color Change Announcement element.
 *	This element can be used for pre-standard publication testing of HE
 *	before P802.11ax draft assigns the element ID extension. The payload of
 *	this vendor specific element is defined by the latest P802.11ax draft
 *	(not including the Element ID Extension field). Please note that the
 *	draft is still work in progress and this element payload is subject to
 *	change.
 */
enum qca_vendor_element_id {
	QCA_VENDOR_ELEM_P2P_PREF_CHAN_LIST = 0,
	QCA_VENDOR_ELEM_HE_CAPAB = 1,
	QCA_VENDOR_ELEM_HE_OPER = 2,
	QCA_VENDOR_ELEM_RAPS = 3,
	QCA_VENDOR_ELEM_MU_EDCA_PARAMS = 4,
	QCA_VENDOR_ELEM_BSS_COLOR_CHANGE = 5,
};

/**
 * enum qca_wlan_vendor_attr_scan - Specifies vendor scan attributes
 *
 * @QCA_WLAN_VENDOR_ATTR_SCAN_IE: IEs that should be included as part of scan
 * @QCA_WLAN_VENDOR_ATTR_SCAN_FREQUENCIES: Nested unsigned 32-bit attributes
 *	with frequencies to be scanned (in MHz)
 * @QCA_WLAN_VENDOR_ATTR_SCAN_SSIDS: Nested attribute with SSIDs to be scanned
 * @QCA_WLAN_VENDOR_ATTR_SCAN_SUPP_RATES: Nested array attribute of supported
 *	rates to be included
 * @QCA_WLAN_VENDOR_ATTR_SCAN_TX_NO_CCK_RATE: flag used to send probe requests
 *	at non CCK rate in 2GHz band
 * @QCA_WLAN_VENDOR_ATTR_SCAN_FLAGS: Unsigned 32-bit scan flags
 * @QCA_WLAN_VENDOR_ATTR_SCAN_COOKIE: Unsigned 64-bit cookie provided by the
 *	driver for the specific scan request
 * @QCA_WLAN_VENDOR_ATTR_SCAN_STATUS: Unsigned 8-bit status of the scan
 *	request decoded as in enum scan_status
 * @QCA_WLAN_VENDOR_ATTR_SCAN_MAC: 6-byte MAC address to use when randomisation
 *	scan flag is set
 * @QCA_WLAN_VENDOR_ATTR_SCAN_MAC_MASK: 6-byte MAC address mask to be used with
 *	randomisation
 * @QCA_WLAN_VENDOR_ATTR_SCAN_BSSID: 6-byte MAC address representing the
 *	specific BSSID to scan for.
 */
enum qca_wlan_vendor_attr_scan {
	QCA_WLAN_VENDOR_ATTR_SCAN_INVALID_PARAM = 0,
	QCA_WLAN_VENDOR_ATTR_SCAN_IE = 1,
	QCA_WLAN_VENDOR_ATTR_SCAN_FREQUENCIES = 2,
	QCA_WLAN_VENDOR_ATTR_SCAN_SSIDS = 3,
	QCA_WLAN_VENDOR_ATTR_SCAN_SUPP_RATES = 4,
	QCA_WLAN_VENDOR_ATTR_SCAN_TX_NO_CCK_RATE = 5,
	QCA_WLAN_VENDOR_ATTR_SCAN_FLAGS = 6,
	QCA_WLAN_VENDOR_ATTR_SCAN_COOKIE = 7,
	QCA_WLAN_VENDOR_ATTR_SCAN_STATUS = 8,
	QCA_WLAN_VENDOR_ATTR_SCAN_MAC = 9,
	QCA_WLAN_VENDOR_ATTR_SCAN_MAC_MASK = 10,
	QCA_WLAN_VENDOR_ATTR_SCAN_BSSID = 11,
	QCA_WLAN_VENDOR_ATTR_SCAN_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_SCAN_MAX =
	QCA_WLAN_VENDOR_ATTR_SCAN_AFTER_LAST - 1
};

/**
 * enum scan_status - Specifies the valid values the vendor scan attribute
 *	QCA_WLAN_VENDOR_ATTR_SCAN_STATUS can take
 *
 * @VENDOR_SCAN_STATUS_NEW_RESULTS: implies the vendor scan is successful with
 *	new scan results
 * @VENDOR_SCAN_STATUS_ABORTED: implies the vendor scan was aborted in-between
 */
enum scan_status {
	VENDOR_SCAN_STATUS_NEW_RESULTS,
	VENDOR_SCAN_STATUS_ABORTED,
	VENDOR_SCAN_STATUS_MAX,
};

/**
 * enum qca_vendor_attr_ota_test - Specifies the values for vendor
 *                       command QCA_NL80211_VENDOR_SUBCMD_OTA_TEST
 * @QCA_WLAN_VENDOR_ATTR_OTA_TEST_ENABLE: enable ota test
 */
enum qca_vendor_attr_ota_test {
	QCA_WLAN_VENDOR_ATTR_OTA_TEST_INVALID,
	/* 8-bit unsigned value to indicate if OTA test is enabled */
	QCA_WLAN_VENDOR_ATTR_OTA_TEST_ENABLE,
	/* keep last */
	QCA_WLAN_VENDOR_ATTR_OTA_TEST_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_OTA_TEST_MAX =
	QCA_WLAN_VENDOR_ATTR_OTA_TEST_AFTER_LAST - 1
};

/**
 * enum qca_vendor_attr_txpower_scale - vendor sub commands index
 *
 * @QCA_WLAN_VENDOR_ATTR_TXPOWER_SCALE: scaling value
 */
enum qca_vendor_attr_txpower_scale {
	QCA_WLAN_VENDOR_ATTR_TXPOWER_SCALE_INVALID,
	/* 8-bit unsigned value to indicate the scaling of tx power */
	QCA_WLAN_VENDOR_ATTR_TXPOWER_SCALE,
	/* keep last */
	QCA_WLAN_VENDOR_ATTR_TXPOWER_SCALE_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_TXPOWER_SCALE_MAX =
	QCA_WLAN_VENDOR_ATTR_TXPOWER_SCALE_AFTER_LAST - 1
};

/**
 * enum qca_vendor_attr_txpower_decr_db - Attributes for TX power decrease
 *
 * These attributes are used with QCA_NL80211_VENDOR_SUBCMD_SET_TXPOWER_DECR_DB.
 */
enum qca_vendor_attr_txpower_decr_db {
	QCA_WLAN_VENDOR_ATTR_TXPOWER_DECR_DB_INVALID,
	/* 8-bit unsigned value to indicate the reduction of TX power in dB for
	 * a virtual interface.
	 */
	QCA_WLAN_VENDOR_ATTR_TXPOWER_DECR_DB,
	/* keep last */
	QCA_WLAN_VENDOR_ATTR_TXPOWER_DECR_DB_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_TXPOWER_DECR_DB_MAX =
	QCA_WLAN_VENDOR_ATTR_TXPOWER_DECR_DB_AFTER_LAST - 1
};

/* Attributes for data used by
 * QCA_NL80211_VENDOR_SUBCMD_SET_WIFI_CONFIGURATION and
 * QCA_NL80211_VENDOR_SUBCMD_GET_WIFI_CONFIGURATION subcommands.
 */
enum qca_wlan_vendor_attr_config {
	QCA_WLAN_VENDOR_ATTR_CONFIG_INVALID = 0,
	/* Unsigned 32-bit value to set the DTIM period.
	 * Whether the wifi chipset wakes at every dtim beacon or a multiple of
	 * the DTIM period. If DTIM is set to 3, the STA shall wake up every 3
	 * DTIM beacons.
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_DYNAMIC_DTIM = 1,
	/* Unsigned 32-bit value to set the wifi_iface stats averaging factor
	 * used to calculate statistics like average the TSF offset or average
	 * number of frame leaked.
	 * For instance, upon Beacon frame reception:
	 * current_avg = ((beacon_TSF - TBTT) * factor + previous_avg * (0x10000 - factor) ) / 0x10000
	 * For instance, when evaluating leaky APs:
	 * current_avg = ((num frame received within guard time) * factor + previous_avg * (0x10000 - factor)) / 0x10000
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_STATS_AVG_FACTOR = 2,
	/* Unsigned 32-bit value to configure guard time, i.e., when
	 * implementing IEEE power management based on frame control PM bit, how
	 * long the driver waits before shutting down the radio and after
	 * receiving an ACK frame for a Data frame with PM bit set.
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_GUARD_TIME = 3,
	/* Unsigned 32-bit value to change the FTM capability dynamically */
	QCA_WLAN_VENDOR_ATTR_CONFIG_FINE_TIME_MEASUREMENT = 4,
	/* Unsigned 16-bit value to configure maximum TX rate dynamically */
	QCA_WLAN_VENDOR_ATTR_CONF_TX_RATE = 5,
	/* Unsigned 32-bit value to configure the number of continuous
	 * Beacon Miss which shall be used by the firmware to penalize
	 * the RSSI.
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_PENALIZE_AFTER_NCONS_BEACON_MISS = 6,
	/* Unsigned 8-bit value to configure the channel avoidance indication
	 * behavior. Firmware to send only one indication and ignore duplicate
	 * indications when set to avoid multiple Apps wakeups.
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_CHANNEL_AVOIDANCE_IND = 7,
	/* 8-bit unsigned value to configure the maximum TX MPDU for
	 * aggregation.
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_TX_MPDU_AGGREGATION = 8,
	/* 8-bit unsigned value to configure the maximum RX MPDU for
	 * aggregation.
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_RX_MPDU_AGGREGATION = 9,
	/* 8-bit unsigned value to configure the Non aggregrate/11g sw
	 * retry threshold (0 disable, 31 max).
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_NON_AGG_RETRY = 10,
	/* 8-bit unsigned value to configure the aggregrate sw
	 * retry threshold (0 disable, 31 max).
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_AGG_RETRY = 11,
	/* 8-bit unsigned value to configure the MGMT frame
	 * retry threshold (0 disable, 31 max).
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_MGMT_RETRY = 12,
	/* 8-bit unsigned value to configure the CTRL frame
	 * retry threshold (0 disable, 31 max).
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_CTRL_RETRY = 13,
	/* 8-bit unsigned value to configure the propagation delay for
	 * 2G/5G band (0~63, units in us)
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_PROPAGATION_DELAY = 14,
	/* Unsigned 32-bit value to configure the number of unicast TX fail
	 * packet count. The peer is disconnected once this threshold is
	 * reached.
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_TX_FAIL_COUNT = 15,
	/* Attribute used to set scan default IEs to the driver.
	 *
	 * These IEs can be used by scan operations that will be initiated by
	 * the driver/firmware.
	 *
	 * For further scan requests coming to the driver, these IEs should be
	 * merged with the IEs received along with scan request coming to the
	 * driver. If a particular IE is present in the scan default IEs but not
	 * present in the scan request, then that IE should be added to the IEs
	 * sent in the Probe Request frames for that scan request.
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_SCAN_DEFAULT_IES = 16,
	/* Unsigned 32-bit attribute for generic commands */
	QCA_WLAN_VENDOR_ATTR_CONFIG_GENERIC_COMMAND = 17,
	/* Unsigned 32-bit value attribute for generic commands */
	QCA_WLAN_VENDOR_ATTR_CONFIG_GENERIC_VALUE = 18,
	/* Unsigned 32-bit data attribute for generic command response */
	QCA_WLAN_VENDOR_ATTR_CONFIG_GENERIC_DATA = 19,
	/* Unsigned 32-bit length attribute for
	 * QCA_WLAN_VENDOR_ATTR_CONFIG_GENERIC_DATA
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_GENERIC_LENGTH = 20,
	/* Unsigned 32-bit flags attribute for
	 * QCA_WLAN_VENDOR_ATTR_CONFIG_GENERIC_DATA
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_GENERIC_FLAGS = 21,
	/* Unsigned 32-bit, defining the access policy.
	 * See enum qca_access_policy. Used with
	 * QCA_WLAN_VENDOR_ATTR_CONFIG_ACCESS_POLICY_IE_LIST.
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_ACCESS_POLICY = 22,
	/* Sets the list of full set of IEs for which a specific access policy
	 * has to be applied. Used along with
	 * QCA_WLAN_VENDOR_ATTR_CONFIG_ACCESS_POLICY to control the access.
	 * Zero length payload can be used to clear this access constraint.
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_ACCESS_POLICY_IE_LIST = 23,
	/* Unsigned 32-bit, specifies the interface index (netdev) for which the
	 * corresponding configurations are applied. If the interface index is
	 * not specified, the configurations are attributed to the respective
	 * wiphy.
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_IFINDEX = 24,
	/* 8-bit unsigned value to trigger QPower: 1-Enable, 0-Disable */
	QCA_WLAN_VENDOR_ATTR_CONFIG_QPOWER = 25,
	/* 8-bit unsigned value to configure the driver and below layers to
	 * ignore the assoc disallowed set by APs while connecting
	 * 1-Ignore, 0-Don't ignore
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_IGNORE_ASSOC_DISALLOWED = 26,
	/* 32-bit unsigned value to trigger antenna diversity features:
	 * 1-Enable, 0-Disable
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_ANT_DIV_ENA = 27,
	/* 32-bit unsigned value to configure specific chain antenna */
	QCA_WLAN_VENDOR_ATTR_CONFIG_ANT_DIV_CHAIN = 28,
	/* 32-bit unsigned value to trigger cycle selftest
	 * 1-Enable, 0-Disable
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_ANT_DIV_SELFTEST = 29,
	/* 32-bit unsigned to configure the cycle time of selftest
	 * the unit is micro-second
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_ANT_DIV_SELFTEST_INTVL = 30,
	/* 32-bit unsigned value to set reorder timeout for AC_VO */
	QCA_WLAN_VENDOR_ATTR_CONFIG_RX_REORDER_TIMEOUT_VOICE = 31,
	/* 32-bit unsigned value to set reorder timeout for AC_VI */
	QCA_WLAN_VENDOR_ATTR_CONFIG_RX_REORDER_TIMEOUT_VIDEO = 32,
	/* 32-bit unsigned value to set reorder timeout for AC_BE */
	QCA_WLAN_VENDOR_ATTR_CONFIG_RX_REORDER_TIMEOUT_BESTEFFORT = 33,
	/* 32-bit unsigned value to set reorder timeout for AC_BK */
	QCA_WLAN_VENDOR_ATTR_CONFIG_RX_REORDER_TIMEOUT_BACKGROUND = 34,
	/* 6-byte MAC address to point out the specific peer */
	QCA_WLAN_VENDOR_ATTR_CONFIG_RX_BLOCKSIZE_PEER_MAC = 35,
	/* 32-bit unsigned value to set window size for specific peer */
	QCA_WLAN_VENDOR_ATTR_CONFIG_RX_BLOCKSIZE_WINLIMIT = 36,
	/* 8-bit unsigned value to set the beacon miss threshold in 2.4 GHz */
	QCA_WLAN_VENDOR_ATTR_CONFIG_BEACON_MISS_THRESHOLD_24 = 37,
	/* 8-bit unsigned value to set the beacon miss threshold in 5 GHz */
	QCA_WLAN_VENDOR_ATTR_CONFIG_BEACON_MISS_THRESHOLD_5 = 38,
	/* 32-bit unsigned value to configure 5 or 10 MHz channel width for
	 * station device while in disconnect state. The attribute use the
	 * value of enum nl80211_chan_width: NL80211_CHAN_WIDTH_5 means 5 MHz,
	 * NL80211_CHAN_WIDTH_10 means 10 MHz. If set, the device work in 5 or
	 * 10 MHz channel width, the station will not connect to a BSS using 20
	 * MHz or higher bandwidth. Set to NL80211_CHAN_WIDTH_20_NOHT to
	 * clear this constraint.
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_SUB20_CHAN_WIDTH = 39,
	/* 32-bit unsigned value to configure the propagation absolute delay
	 * for 2G/5G band (units in us)
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_PROPAGATION_ABS_DELAY = 40,
	/* 32-bit unsigned value to set probe period */
	QCA_WLAN_VENDOR_ATTR_CONFIG_ANT_DIV_PROBE_PERIOD = 41,
	/* 32-bit unsigned value to set stay period */
	QCA_WLAN_VENDOR_ATTR_CONFIG_ANT_DIV_STAY_PERIOD = 42,
	/* 32-bit unsigned value to set snr diff */
	QCA_WLAN_VENDOR_ATTR_CONFIG_ANT_DIV_SNR_DIFF = 43,
	/* 32-bit unsigned value to set probe dwell time */
	QCA_WLAN_VENDOR_ATTR_CONFIG_ANT_DIV_PROBE_DWELL_TIME = 44,
	/* 32-bit unsigned value to set mgmt snr weight */
	QCA_WLAN_VENDOR_ATTR_CONFIG_ANT_DIV_MGMT_SNR_WEIGHT = 45,
	/* 32-bit unsigned value to set data snr weight */
	QCA_WLAN_VENDOR_ATTR_CONFIG_ANT_DIV_DATA_SNR_WEIGHT = 46,
	/* 32-bit unsigned value to set ack snr weight */
	QCA_WLAN_VENDOR_ATTR_CONFIG_ANT_DIV_ACK_SNR_WEIGHT = 47,
	/* 32-bit unsigned value to configure the listen interval.
	 * This is in units of beacon intervals. This configuration alters
	 * the negotiated listen interval with the AP during the connection.
	 * It is highly recommended to configure a value less than or equal to
	 * the one negotiated during the association. Configuring any greater
	 * value can have adverse effects (frame loss, AP disassociating STA,
	 * etc.).
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_LISTEN_INTERVAL = 48,
	/*
	 * 8 bit unsigned value that is set on an AP/GO virtual interface to
	 * disable operations that would cause the AP/GO to leave its operating
	 * channel.
	 *
	 * This will restrict the scans to the AP/GO operating channel and the
	 * channels of the other band, if DBS is supported.A STA/CLI interface
	 * brought up after this setting is enabled, will be restricted to
	 * connecting to devices only on the AP/GO interface's operating channel
	 * or on the other band in DBS case. P2P supported channel list is
	 * modified, to only include AP interface's operating-channel and the
	 * channels of the other band if DBS is supported.
	 *
	 * These restrictions are only applicable as long as the AP/GO interface
	 * is alive. If the AP/GO interface is brought down then this
	 * setting/restriction is forgotten.
	 *
	 * If this variable is set on an AP/GO interface while a multi-channel
	 * concurrent session is active, it has no effect on the operation of
	 * the current interfaces, other than restricting the scan to the AP/GO
	 * operating channel and the other band channels if DBS is supported.
	 * However, if the STA is brought down and restarted then the new STA
	 * connection will either be formed on the AP/GO channel or on the
	 * other band in a DBS case. This is because of the scan being
	 * restricted on these channels as mentioned above.
	 *
	 * 1-Restrict / 0-Don't restrict offchannel operations.
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_RESTRICT_OFFCHANNEL = 49,
	/*
	 * 8 bit unsigned value to enable/disable LRO (Large Receive Offload)
	 * on an interface.
	 * 1 - Enable, 0 - Disable.
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_LRO = 50,

	/*
	 * 8 bit unsigned value to globally enable/disable scan
	 * 1 - Enable, 0 - Disable.
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_SCAN_ENABLE = 51,

	/* 8-bit unsigned value to set the total beacon miss count
	 * This parameter will set the total beacon miss count.
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_TOTAL_BEACON_MISS_COUNT = 52,

	/* Unsigned 32-bit value to configure the number of continuous
	 * Beacon Miss which shall be used by the firmware to penalize
	 * the RSSI for BTC.
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_PENALIZE_AFTER_NCONS_BEACON_MISS_BTC = 53,

	/* 8-bit unsigned value to configure the driver and below layers to
	 * enable/disable all FILS features.
	 * 0-enable, 1-disable
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_DISABLE_FILS = 54,

	/* 16-bit unsigned value to configure the level of WLAN latency
	 * module. See enum qca_wlan_vendor_attr_config_latency_level.
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_LATENCY_LEVEL = 55,

	/* 8-bit unsigned value indicating the driver to use the RSNE as-is from
	 * the connect interface. Exclusively used for the scenarios where the
	 * device is used as a test bed device with special functionality and
	 * not recommended for production. This helps driver to not validate the
	 * RSNE passed from user space and thus allow arbitrary IE data to be
	 * used for testing purposes.
	 * 1-enable, 0-disable.
	 * Applications set/reset this configuration. If not reset, this
	 * parameter remains in use until the driver is unloaded.
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_RSN_IE = 56,

	/* 8-bit unsigned value to trigger green Tx power saving.
	 * 1-Enable, 0-Disable
	 */
	QCA_WLAN_VENDOR_ATTR_CONFIG_GTX = 57,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_CONFIG_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_CONFIG_MAX =
	QCA_WLAN_VENDOR_ATTR_CONFIG_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_sap_config - Parameters for AP configuration
 */
enum qca_wlan_vendor_attr_sap_config {
	QCA_WLAN_VENDOR_ATTR_SAP_CONFIG_INVALID = 0,
	/* 1 - reserved for QCA */
	/* List of frequencies on which AP is expected to operate.
	 * This is irrespective of ACS configuration. This list is a priority
	 * based one and is looked for before the AP is created to ensure the
	 * best concurrency sessions (avoid MCC and use DBS/SCC) co-exist in
	 * the system.
	 */
	QCA_WLAN_VENDOR_ATTR_SAP_MANDATORY_FREQUENCY_LIST = 2,

	QCA_WLAN_VENDOR_ATTR_SAP_CONFIG_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_SAP_CONFIG_MAX =
	QCA_WLAN_VENDOR_ATTR_SAP_CONFIG_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_sap_conditional_chan_switch - Parameters for AP
 *					conditional channel switch
 */
enum qca_wlan_vendor_attr_sap_conditional_chan_switch {
	QCA_WLAN_VENDOR_ATTR_SAP_CONDITIONAL_CHAN_SWITCH_INVALID = 0,
	/* Priority based frequency list (an array of u32 values in host byte
	 * order)
	 */
	QCA_WLAN_VENDOR_ATTR_SAP_CONDITIONAL_CHAN_SWITCH_FREQ_LIST = 1,
	/* Status of the conditional switch (u32).
	 * 0: Success, Non-zero: Failure
	 */
	QCA_WLAN_VENDOR_ATTR_SAP_CONDITIONAL_CHAN_SWITCH_STATUS = 2,

	QCA_WLAN_VENDOR_ATTR_SAP_CONDITIONAL_CHAN_SWITCH_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_SAP_CONDITIONAL_CHAN_SWITCH_MAX =
	QCA_WLAN_VENDOR_ATTR_SAP_CONDITIONAL_CHAN_SWITCH_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_gpio_attr - Parameters for GPIO configuration
 */
enum qca_wlan_gpio_attr {
	QCA_WLAN_VENDOR_ATTR_GPIO_PARAM_INVALID = 0,
	/* Unsigned 32-bit attribute for GPIO command */
	QCA_WLAN_VENDOR_ATTR_GPIO_PARAM_COMMAND,
	/* Unsigned 32-bit attribute for GPIO PIN number to configure */
	QCA_WLAN_VENDOR_ATTR_GPIO_PARAM_PINNUM,
	/* Unsigned 32-bit attribute for GPIO value to configure */
	QCA_WLAN_VENDOR_ATTR_GPIO_PARAM_VALUE,
	/* Unsigned 32-bit attribute for GPIO pull type */
	QCA_WLAN_VENDOR_ATTR_GPIO_PARAM_PULL_TYPE,
	/* Unsigned 32-bit attribute for GPIO interrupt mode */
	QCA_WLAN_VENDOR_ATTR_GPIO_PARAM_INTR_MODE,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_GPIO_PARAM_LAST,
	QCA_WLAN_VENDOR_ATTR_GPIO_PARAM_MAX =
	QCA_WLAN_VENDOR_ATTR_GPIO_PARAM_LAST - 1
};

/**
 * qca_wlan_set_qdepth_thresh_attr - Parameters for setting
 * MSDUQ depth threshold per peer per tid in the target
 *
 * Associated Vendor Command:
 * QCA_NL80211_VENDOR_SUBCMD_SET_QDEPTH_THRESH
 */
enum qca_wlan_set_qdepth_thresh_attr {
	QCA_WLAN_VENDOR_ATTR_QDEPTH_THRESH_INVALID = 0,
	/* 6-byte MAC address */
	QCA_WLAN_VENDOR_ATTR_QDEPTH_THRESH_MAC_ADDR,
	/* Unsigned 32-bit attribute for holding the TID */
	QCA_WLAN_VENDOR_ATTR_QDEPTH_THRESH_TID,
	/* Unsigned 32-bit attribute for holding the update mask
	 * bit 0 - Update high priority msdu qdepth threshold
	 * bit 1 - Update low priority msdu qdepth threshold
	 * bit 2 - Update UDP msdu qdepth threshold
	 * bit 3 - Update Non UDP msdu qdepth threshold
	 * rest of bits are reserved
	 */
	QCA_WLAN_VENDOR_ATTR_QDEPTH_THRESH_UPDATE_MASK,
	/* Unsigned 32-bit attribute for holding the threshold value */
	QCA_WLAN_VENDOR_ATTR_QDEPTH_THRESH_VALUE,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_QDEPTH_THRESH_LAST,
	QCA_WLAN_VENDOR_ATTR_QDEPTH_THRESH_MAX =
		QCA_WLAN_VENDOR_ATTR_QDEPTH_THRESH_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_get_hw_capability - Wi-Fi hardware capability
 */
enum qca_wlan_vendor_attr_get_hw_capability {
	QCA_WLAN_VENDOR_ATTR_HW_CAPABILITY_INVALID,
	/* Antenna isolation
	 * An attribute used in the response.
	 * The content of this attribute is encoded in a byte array. Each byte
	 * value is an antenna isolation value. The array length is the number
	 * of antennas.
	 */
	QCA_WLAN_VENDOR_ATTR_ANTENNA_ISOLATION,
	/* Request HW capability
	 * An attribute used in the request.
	 * The content of this attribute is a u32 array for one or more of
	 * hardware capabilities (attribute IDs) that are being requested. Each
	 * u32 value has a value from this
	 * enum qca_wlan_vendor_attr_get_hw_capability
	 * identifying which capabilities are requested.
	 */
	QCA_WLAN_VENDOR_ATTR_GET_HW_CAPABILITY,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_HW_CAPABILITY_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_HW_CAPABILITY_MAX =
	QCA_WLAN_VENDOR_ATTR_HW_CAPABILITY_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_ll_stats_ext - Attributes for MAC layer monitoring
 *    offload which is an extension for LL_STATS.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_CFG_PERIOD: Monitoring period. Unit in ms.
 *    If MAC counters do not exceed the threshold, FW will report monitored
 *    link layer counters periodically as this setting. The first report is
 *    always triggered by this timer.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_CFG_THRESHOLD: It is a percentage (1-99).
 *    For each MAC layer counter, FW holds two copies. One is the current value.
 *    The other is the last report. Once a current counter's increment is larger
 *    than the threshold, FW will indicate that counter to host even if the
 *    monitoring timer does not expire.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_PEER_PS_CHG: Peer STA power state change
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TID: TID of MSDU
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_NUM_MSDU: Count of MSDU with the same
 *    failure code.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_STATUS: TX failure code
 *    1: TX packet discarded
 *    2: No ACK
 *    3: Postpone
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_PEER_MAC_ADDRESS: peer MAC address
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_PEER_PS_STATE: Peer STA current state
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_GLOBAL: Global threshold.
 *    Threshold for all monitored parameters. If per counter dedicated threshold
 *    is not enabled, this threshold will take effect.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_EVENT_MODE: Indicate what triggers this
 *    event, PERORID_TIMEOUT == 1, THRESH_EXCEED == 0.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_IFACE_ID: interface ID
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_PEER_ID: peer ID
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_BITMAP: bitmap for TX counters
 *    Bit0: TX counter unit in MSDU
 *    Bit1: TX counter unit in MPDU
 *    Bit2: TX counter unit in PPDU
 *    Bit3: TX counter unit in byte
 *    Bit4: Dropped MSDUs
 *    Bit5: Dropped Bytes
 *    Bit6: MPDU retry counter
 *    Bit7: MPDU failure counter
 *    Bit8: PPDU failure counter
 *    Bit9: MPDU aggregation counter
 *    Bit10: MCS counter for ACKed MPDUs
 *    Bit11: MCS counter for Failed MPDUs
 *    Bit12: TX Delay counter
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_BITMAP: bitmap for RX counters
 *    Bit0: MAC RX counter unit in MPDU
 *    Bit1: MAC RX counter unit in byte
 *    Bit2: PHY RX counter unit in PPDU
 *    Bit3: PHY RX counter unit in byte
 *    Bit4: Disorder counter
 *    Bit5: Retry counter
 *    Bit6: Duplication counter
 *    Bit7: Discard counter
 *    Bit8: MPDU aggregation size counter
 *    Bit9: MCS counter
 *    Bit10: Peer STA power state change (wake to sleep) counter
 *    Bit11: Peer STA power save counter, total time in PS mode
 *    Bit12: Probe request counter
 *    Bit13: Other management frames counter
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_CCA_BSS_BITMAP: bitmap for CCA
 *    Bit0: Idle time
 *    Bit1: TX time
 *    Bit2: time RX in current bss
 *    Bit3: Out of current bss time
 *    Bit4: Wireless medium busy time
 *    Bit5: RX in bad condition time
 *    Bit6: TX in bad condition time
 *    Bit7: time wlan card not available
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_SIGNAL_BITMAP: bitmap for signal
 *    Bit0: Per channel SNR counter
 *    Bit1: Per channel noise floor counter
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_PEER_NUM: number of peers
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_CHANNEL_NUM: number of channels
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_PEER_AC_RX_NUM: number of RX stats
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_CCA_BSS: per channel BSS CCA stats
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_PEER: container for per PEER stats
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_MSDU: Number of total TX MSDUs
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_MPDU: Number of total TX MPDUs
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_PPDU: Number of total TX PPDUs
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_BYTES: bytes of TX data
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_DROP: Number of dropped TX packets
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_DROP_BYTES: Bytes dropped
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_RETRY: waiting time without an ACK
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_NO_ACK: number of MPDU not-ACKed
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_NO_BACK: number of PPDU not-ACKed
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_AGGR_NUM:
 *    aggregation stats buffer length
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_SUCC_MCS_NUM: length of mcs stats
 *    buffer for ACKed MPDUs.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_FAIL_MCS_NUM: length of mcs stats
 *    buffer for failed MPDUs.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_DELAY_ARRAY_SIZE:
 *    length of delay stats array.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_AGGR: TX aggregation stats
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_SUCC_MCS: MCS stats for ACKed MPDUs
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_FAIL_MCS: MCS stats for failed MPDUs
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_DELAY: tx delay stats
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_MPDU: MPDUs received
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_MPDU_BYTES: bytes received
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_PPDU: PPDU received
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_PPDU_BYTES: PPDU bytes received
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_MPDU_LOST: packets lost
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_MPDU_RETRY: number of RX packets
 *    flagged as retransmissions
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_MPDU_DUP: number of RX packets
 *    flagged as duplicated
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_MPDU_DISCARD: number of RX
 *    packets discarded
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_AGGR_NUM: length of RX aggregation
 *    stats buffer.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_MCS_NUM: length of RX mcs
 *    stats buffer.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_MCS: RX mcs stats buffer
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_AGGR: aggregation stats buffer
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_PEER_PS_TIMES: times STAs go to sleep
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_PEER_PS_DURATION: STAs' total sleep time
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_PROBE_REQ: number of probe
 *    requests received
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_MGMT: number of other mgmt
 *    frames received
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_IDLE_TIME: Percentage of idle time
 *    there is no TX, nor RX, nor interference.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_TIME: percentage of time
 *    transmitting packets.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_TIME: percentage of time
 *    for receiving.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_BUSY: percentage of time
 *    interference detected.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_BAD: percentage of time
 *    receiving packets with errors.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_BAD: percentage of time
 *    TX no-ACK.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_NO_AVAIL: percentage of time
 *    the chip is unable to work in normal conditions.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_IN_BSS_TIME: percentage of time
 *    receiving packets in current BSS.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_OUT_BSS_TIME: percentage of time
 *    receiving packets not in current BSS.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_PEER_ANT_NUM: number of antennas
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_PEER_SIGNAL:
 *    This is a container for per antenna signal stats.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_ANT_SNR: per antenna SNR value
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_ANT_NF: per antenna NF value
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_IFACE_RSSI_BEACON: RSSI of beacon
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_IFACE_SNR_BEACON: SNR of beacon
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_REPORT_TIME: u64
 *    Absolute timestamp from 1970/1/1, unit in ms. After receiving the
 *    message, user layer APP could call gettimeofday to get another
 *    timestamp and calculate transfer delay for the message.
 * @QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_MEASUREMENT_TIME: u32
 *    Real period for this measurement, unit in us.
 */
enum qca_wlan_vendor_attr_ll_stats_ext {
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_INVALID = 0,

	/* Attributes for configurations */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_CFG_PERIOD,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_CFG_THRESHOLD,

	/* Peer STA power state change */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_PEER_PS_CHG,

	/* TX failure event */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TID,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_NUM_MSDU,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_STATUS,

	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_PEER_PS_STATE,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_PEER_MAC_ADDRESS,

	/* MAC counters */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_GLOBAL,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_EVENT_MODE,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_IFACE_ID,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_PEER_ID,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_BITMAP,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_BITMAP,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_CCA_BSS_BITMAP,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_SIGNAL_BITMAP,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_PEER_NUM,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_CHANNEL_NUM,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_CCA_BSS,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_PEER,

	/* Sub-attributes for PEER_AC_TX */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_MSDU,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_MPDU,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_PPDU,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_BYTES,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_DROP,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_DROP_BYTES,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_RETRY,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_NO_ACK,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_NO_BACK,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_AGGR_NUM,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_SUCC_MCS_NUM,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_FAIL_MCS_NUM,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_AGGR,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_SUCC_MCS,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_FAIL_MCS,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_DELAY_ARRAY_SIZE,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_DELAY,

	/* Sub-attributes for PEER_AC_RX */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_MPDU,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_MPDU_BYTES,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_PPDU,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_PPDU_BYTES,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_MPDU_LOST,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_MPDU_RETRY,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_MPDU_DUP,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_MPDU_DISCARD,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_AGGR_NUM,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_MCS_NUM,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_MCS,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_AGGR,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_PEER_PS_TIMES,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_PEER_PS_DURATION,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_PROBE_REQ,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_MGMT,

	/* Sub-attributes for CCA_BSS */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_IDLE_TIME,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_TIME,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_TIME,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_BUSY,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_RX_BAD,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_TX_BAD,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_NO_AVAIL,

	/* sub-attribute for BSS_RX_TIME */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_IN_BSS_TIME,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_OUT_BSS_TIME,

	/* Sub-attributes for PEER_SIGNAL */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_PEER_ANT_NUM,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_PEER_SIGNAL,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_ANT_SNR,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_ANT_NF,

	/* Sub-attributes for IFACE_BSS */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_IFACE_RSSI_BEACON,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_IFACE_SNR_BEACON,

	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_REPORT_TIME,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_MEASUREMENT_TIME,

	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_LAST,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_MAX =
		QCA_WLAN_VENDOR_ATTR_LL_STATS_EXT_LAST - 1
};

/* Attributes for FTM commands and events */

/**
 * enum qca_wlan_vendor_attr_loc_capa - Indoor location capabilities
 *
 * @QCA_WLAN_VENDOR_ATTR_LOC_CAPA_FLAGS: Various flags. See
 *	enum qca_wlan_vendor_attr_loc_capa_flags.
 * @QCA_WLAN_VENDOR_ATTR_FTM_CAPA_MAX_NUM_SESSIONS: Maximum number
 *	of measurement sessions that can run concurrently.
 *	Default is one session (no session concurrency).
 * @QCA_WLAN_VENDOR_ATTR_FTM_CAPA_MAX_NUM_PEERS: The total number of unique
 *	peers that are supported in running sessions. For example,
 *	if the value is 8 and maximum number of sessions is 2, you can
 *	have one session with 8 unique peers, or 2 sessions with 4 unique
 *	peers each, and so on.
 * @QCA_WLAN_VENDOR_ATTR_FTM_CAPA_MAX_NUM_BURSTS_EXP: Maximum number
 *	of bursts per peer, as an exponent (2^value). Default is 0,
 *	meaning no multi-burst support.
 * @QCA_WLAN_VENDOR_ATTR_FTM_CAPA_MAX_MEAS_PER_BURST: Maximum number
 *	of measurement exchanges allowed in a single burst.
 * @QCA_WLAN_VENDOR_ATTR_AOA_CAPA_SUPPORTED_TYPES: Supported AOA measurement
 *	types. A bit mask (unsigned 32 bit value), each bit corresponds
 *	to an AOA type as defined by enum qca_vendor_attr_aoa_type.
 */
enum qca_wlan_vendor_attr_loc_capa {
	QCA_WLAN_VENDOR_ATTR_LOC_CAPA_INVALID,
	QCA_WLAN_VENDOR_ATTR_LOC_CAPA_FLAGS,
	QCA_WLAN_VENDOR_ATTR_FTM_CAPA_MAX_NUM_SESSIONS,
	QCA_WLAN_VENDOR_ATTR_FTM_CAPA_MAX_NUM_PEERS,
	QCA_WLAN_VENDOR_ATTR_FTM_CAPA_MAX_NUM_BURSTS_EXP,
	QCA_WLAN_VENDOR_ATTR_FTM_CAPA_MAX_MEAS_PER_BURST,
	QCA_WLAN_VENDOR_ATTR_AOA_CAPA_SUPPORTED_TYPES,
	/* keep last */
	QCA_WLAN_VENDOR_ATTR_LOC_CAPA_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_LOC_CAPA_MAX =
	QCA_WLAN_VENDOR_ATTR_LOC_CAPA_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_loc_capa_flags: Indoor location capability flags
 *
 * @QCA_WLAN_VENDOR_ATTR_LOC_CAPA_FLAG_FTM_RESPONDER: Set if driver
 *	can be configured as an FTM responder (for example, an AP that
 *	services FTM requests). QCA_NL80211_VENDOR_SUBCMD_FTM_CFG_RESPONDER
 *	will be supported if set.
 * @QCA_WLAN_VENDOR_ATTR_LOC_CAPA_FLAG_FTM_INITIATOR: Set if driver
 *	can run FTM sessions. QCA_NL80211_VENDOR_SUBCMD_FTM_START_SESSION
 *	will be supported if set.
 * @QCA_WLAN_VENDOR_ATTR_LOC_CAPA_FLAG_ASAP: Set if FTM responder
 *	supports immediate (ASAP) response.
 * @QCA_WLAN_VENDOR_ATTR_LOC_CAPA_FLAG_AOA: Set if driver supports standalone
 *	AOA measurement using QCA_NL80211_VENDOR_SUBCMD_AOA_MEAS.
 * @QCA_WLAN_VENDOR_ATTR_LOC_CAPA_FLAG_AOA_IN_FTM: Set if driver supports
 *	requesting AOA measurements as part of an FTM session.
 */
enum qca_wlan_vendor_attr_loc_capa_flags {
	QCA_WLAN_VENDOR_ATTR_LOC_CAPA_FLAG_FTM_RESPONDER = 1 << 0,
	QCA_WLAN_VENDOR_ATTR_LOC_CAPA_FLAG_FTM_INITIATOR = 1 << 1,
	QCA_WLAN_VENDOR_ATTR_LOC_CAPA_FLAG_ASAP = 1 << 2,
	QCA_WLAN_VENDOR_ATTR_LOC_CAPA_FLAG_AOA = 1 << 3,
	QCA_WLAN_VENDOR_ATTR_LOC_CAPA_FLAG_AOA_IN_FTM = 1 << 4,
};

/**
 * enum qca_wlan_vendor_attr_ftm_peer_info: Information about
 *	a single peer in a measurement session.
 *
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_MAC_ADDR: The MAC address of the peer.
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_MEAS_FLAGS: Various flags related
 *	to measurement. See enum qca_wlan_vendor_attr_ftm_peer_meas_flags.
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_MEAS_PARAMS: Nested attribute of
 *	FTM measurement parameters, as specified by IEEE P802.11-REVmc/D7.0
 *	9.4.2.167. See enum qca_wlan_vendor_attr_ftm_meas_param for
 *	list of supported attributes.
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_SECURE_TOKEN_ID: Initial token ID for
 *	secure measurement.
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_AOA_BURST_PERIOD: Request AOA
 *	measurement every <value> bursts. If 0 or not specified,
 *	AOA measurements will be disabled for this peer.
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_FREQ: Frequency in MHz where
 *	the measurement frames are exchanged. Optional; if not
 *	specified, try to locate the peer in the kernel scan
 *	results cache and use frequency from there.
 */
enum qca_wlan_vendor_attr_ftm_peer_info {
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_INVALID,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_MAC_ADDR,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_MEAS_FLAGS,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_MEAS_PARAMS,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_SECURE_TOKEN_ID,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_AOA_BURST_PERIOD,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_FREQ,
	/* keep last */
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_MAX =
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_ftm_peer_meas_flags: Measurement request flags,
 *	per-peer
 *
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_MEAS_FLAG_ASAP: If set, request
 *	immediate (ASAP) response from peer.
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_MEAS_FLAG_LCI: If set, request
 *	LCI report from peer. The LCI report includes the absolute
 *	location of the peer in "official" coordinates (similar to GPS).
 *	See IEEE P802.11-REVmc/D7.0, 11.24.6.7 for more information.
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_MEAS_FLAG_LCR: If set, request
 *	Location civic report from peer. The LCR includes the location
 *	of the peer in free-form format. See IEEE P802.11-REVmc/D7.0,
 *	11.24.6.7 for more information.
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_MEAS_FLAG_SECURE: If set,
 *	request a secure measurement.
 *	QCA_WLAN_VENDOR_ATTR_FTM_PEER_SECURE_TOKEN_ID must also be provided.
 */
enum qca_wlan_vendor_attr_ftm_peer_meas_flags {
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_MEAS_FLAG_ASAP	= 1 << 0,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_MEAS_FLAG_LCI	= 1 << 1,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_MEAS_FLAG_LCR	= 1 << 2,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_MEAS_FLAG_SECURE	= 1 << 3,
};

/**
 * enum qca_wlan_vendor_attr_ftm_meas_param: Measurement parameters
 *
 * @QCA_WLAN_VENDOR_ATTR_FTM_PARAM_MEAS_PER_BURST: Number of measurements
 *	to perform in a single burst.
 * @QCA_WLAN_VENDOR_ATTR_FTM_PARAM_NUM_BURSTS_EXP: Number of bursts to
 *	perform, specified as an exponent (2^value).
 * @QCA_WLAN_VENDOR_ATTR_FTM_PARAM_BURST_DURATION: Duration of burst
 *	instance, as specified in IEEE P802.11-REVmc/D7.0, 9.4.2.167.
 * @QCA_WLAN_VENDOR_ATTR_FTM_PARAM_BURST_PERIOD: Time between bursts,
 *	as specified in IEEE P802.11-REVmc/D7.0, 9.4.2.167. Must
 *	be larger than QCA_WLAN_VENDOR_ATTR_FTM_PARAM_BURST_DURATION.
 */
enum qca_wlan_vendor_attr_ftm_meas_param {
	QCA_WLAN_VENDOR_ATTR_FTM_PARAM_INVALID,
	QCA_WLAN_VENDOR_ATTR_FTM_PARAM_MEAS_PER_BURST,
	QCA_WLAN_VENDOR_ATTR_FTM_PARAM_NUM_BURSTS_EXP,
	QCA_WLAN_VENDOR_ATTR_FTM_PARAM_BURST_DURATION,
	QCA_WLAN_VENDOR_ATTR_FTM_PARAM_BURST_PERIOD,
	/* keep last */
	QCA_WLAN_VENDOR_ATTR_FTM_PARAM_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_FTM_PARAM_MAX =
	QCA_WLAN_VENDOR_ATTR_FTM_PARAM_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_ftm_peer_result: Per-peer results
 *
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_MAC_ADDR: MAC address of the reported
 *	 peer.
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_STATUS: Status of measurement
 *	request for this peer.
 *	See enum qca_wlan_vendor_attr_ftm_peer_result_status.
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_FLAGS: Various flags related
 *	to measurement results for this peer.
 *	See enum qca_wlan_vendor_attr_ftm_peer_result_flags.
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_VALUE_SECONDS: Specified when
 *	request failed and peer requested not to send an additional request
 *	for this number of seconds.
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_LCI: LCI report when received
 *	from peer. In the format specified by IEEE P802.11-REVmc/D7.0,
 *	9.4.2.22.10.
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_LCR: Location civic report when
 *	received from peer. In the format specified by IEEE P802.11-REVmc/D7.0,
 *	9.4.2.22.13.
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_MEAS_PARAMS: Reported when peer
 *	overridden some measurement request parameters. See
 *	enum qca_wlan_vendor_attr_ftm_meas_param.
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_AOA_MEAS: AOA measurement
 *	for this peer. Same contents as @QCA_WLAN_VENDOR_ATTR_AOA_MEAS_RESULT.
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_MEAS: Array of measurement
 *	results. Each entry is a nested attribute defined
 *	by enum qca_wlan_vendor_attr_ftm_meas.
 */
enum qca_wlan_vendor_attr_ftm_peer_result {
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_INVALID,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_MAC_ADDR,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_STATUS,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_FLAGS,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_VALUE_SECONDS,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_LCI,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_LCR,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_MEAS_PARAMS,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_AOA_MEAS,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_MEAS,
	/* keep last */
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_MAX =
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_ftm_peer_result_status
 *
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_STATUS_OK: Request sent ok and results
 *	will be provided. Peer may have overridden some measurement parameters,
 *	in which case overridden parameters will be report by
 *	QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_MEAS_PARAM attribute.
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_STATUS_INCAPABLE: Peer is incapable
 *	of performing the measurement request. No more results will be sent
 *	for this peer in this session.
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_STATUS_FAILED: Peer reported request
 *	failed, and requested not to send an additional request for number
 *	of seconds specified by QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_VALUE_SECONDS
 *	attribute.
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_STATUS_INVALID: Request validation
 *	failed. Request was not sent over the air.
 */
enum qca_wlan_vendor_attr_ftm_peer_result_status {
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_STATUS_OK,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_STATUS_INCAPABLE,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_STATUS_FAILED,
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_STATUS_INVALID,
};

/**
 * enum qca_wlan_vendor_attr_ftm_peer_result_flags: Various flags
 *  for measurement result, per-peer
 *
 * @QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_FLAG_DONE: If set,
 *	measurement completed for this peer. No more results will be reported
 *	for this peer in this session.
 */
enum qca_wlan_vendor_attr_ftm_peer_result_flags {
	QCA_WLAN_VENDOR_ATTR_FTM_PEER_RES_FLAG_DONE = 1 << 0,
};

/**
 * enum qca_vendor_attr_loc_session_status: Session completion status code
 *
 * @QCA_WLAN_VENDOR_ATTR_LOC_SESSION_STATUS_OK: Session completed
 *	successfully.
 * @QCA_WLAN_VENDOR_ATTR_LOC_SESSION_STATUS_ABORTED: Session aborted
 *	by request.
 * @QCA_WLAN_VENDOR_ATTR_LOC_SESSION_STATUS_INVALID: Session request
 *	was invalid and was not started.
 * @QCA_WLAN_VENDOR_ATTR_LOC_SESSION_STATUS_FAILED: Session had an error
 *	and did not complete normally (for example out of resources).
 */
enum qca_vendor_attr_loc_session_status {
	QCA_WLAN_VENDOR_ATTR_LOC_SESSION_STATUS_OK,
	QCA_WLAN_VENDOR_ATTR_LOC_SESSION_STATUS_ABORTED,
	QCA_WLAN_VENDOR_ATTR_LOC_SESSION_STATUS_INVALID,
	QCA_WLAN_VENDOR_ATTR_LOC_SESSION_STATUS_FAILED,
};

/**
 * enum qca_wlan_vendor_attr_ftm_meas: Single measurement data
 *
 * @QCA_WLAN_VENDOR_ATTR_FTM_MEAS_T1: Time of departure (TOD) of FTM packet as
 *	recorded by responder, in picoseconds.
 *	See IEEE P802.11-REVmc/D7.0, 11.24.6.4 for more information.
 * @QCA_WLAN_VENDOR_ATTR_FTM_MEAS_T2: Time of arrival (TOA) of FTM packet at
 *	initiator, in picoseconds.
 *	See IEEE P802.11-REVmc/D7.0, 11.24.6.4 for more information.
 * @QCA_WLAN_VENDOR_ATTR_FTM_MEAS_T3: TOD of ACK packet as recorded by
 *	initiator, in picoseconds.
 *	See IEEE P802.11-REVmc/D7.0, 11.24.6.4 for more information.
 * @QCA_WLAN_VENDOR_ATTR_FTM_MEAS_T4: TOA of ACK packet at
 *	responder, in picoseconds.
 *	See IEEE P802.11-REVmc/D7.0, 11.24.6.4 for more information.
 * @QCA_WLAN_VENDOR_ATTR_FTM_MEAS_RSSI: RSSI (signal level) as recorded
 *	during this measurement exchange. Optional and will be provided if
 *	the hardware can measure it.
 * @QCA_WLAN_VENDOR_ATTR_FTM_MEAS_TOD_ERR: TOD error reported by
 *	responder. Not always provided.
 *	See IEEE P802.11-REVmc/D7.0, 9.6.8.33 for more information.
 * @QCA_WLAN_VENDOR_ATTR_FTM_MEAS_TOA_ERR: TOA error reported by
 *	responder. Not always provided.
 *	See IEEE P802.11-REVmc/D7.0, 9.6.8.33 for more information.
 * @QCA_WLAN_VENDOR_ATTR_FTM_MEAS_INITIATOR_TOD_ERR: TOD error measured by
 *	initiator. Not always provided.
 *	See IEEE P802.11-REVmc/D7.0, 9.6.8.33 for more information.
 * @QCA_WLAN_VENDOR_ATTR_FTM_MEAS_INITIATOR_TOA_ERR: TOA error measured by
 *	initiator. Not always provided.
 *	See IEEE P802.11-REVmc/D7.0, 9.6.8.33 for more information.
 * @QCA_WLAN_VENDOR_ATTR_FTM_MEAS_PAD: Dummy attribute for padding.
 */
enum qca_wlan_vendor_attr_ftm_meas {
	QCA_WLAN_VENDOR_ATTR_FTM_MEAS_INVALID,
	QCA_WLAN_VENDOR_ATTR_FTM_MEAS_T1,
	QCA_WLAN_VENDOR_ATTR_FTM_MEAS_T2,
	QCA_WLAN_VENDOR_ATTR_FTM_MEAS_T3,
	QCA_WLAN_VENDOR_ATTR_FTM_MEAS_T4,
	QCA_WLAN_VENDOR_ATTR_FTM_MEAS_RSSI,
	QCA_WLAN_VENDOR_ATTR_FTM_MEAS_TOD_ERR,
	QCA_WLAN_VENDOR_ATTR_FTM_MEAS_TOA_ERR,
	QCA_WLAN_VENDOR_ATTR_FTM_MEAS_INITIATOR_TOD_ERR,
	QCA_WLAN_VENDOR_ATTR_FTM_MEAS_INITIATOR_TOA_ERR,
	QCA_WLAN_VENDOR_ATTR_FTM_MEAS_PAD,
	/* keep last */
	QCA_WLAN_VENDOR_ATTR_FTM_MEAS_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_FTM_MEAS_MAX =
	QCA_WLAN_VENDOR_ATTR_FTM_MEAS_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_aoa_type - AOA measurement type
 *
 * @QCA_WLAN_VENDOR_ATTR_AOA_TYPE_TOP_CIR_PHASE: Phase of the strongest
 *	CIR (channel impulse response) path for each antenna.
 * @QCA_WLAN_VENDOR_ATTR_AOA_TYPE_TOP_CIR_PHASE_AMP: Phase and amplitude
 *	of the strongest CIR path for each antenna.
 */
enum qca_wlan_vendor_attr_aoa_type {
	QCA_WLAN_VENDOR_ATTR_AOA_TYPE_TOP_CIR_PHASE,
	QCA_WLAN_VENDOR_ATTR_AOA_TYPE_TOP_CIR_PHASE_AMP,
	QCA_WLAN_VENDOR_ATTR_AOA_TYPE_MAX
};

/**
 * enum qca_wlan_vendor_attr_encryption_test - Attributes to
 * validate encryption engine
 *
 * @QCA_WLAN_VENDOR_ATTR_ENCRYPTION_TEST_NEEDS_DECRYPTION: Flag attribute.
 *	This will be included if the request is for decryption; if not included,
 *	the request is treated as a request for encryption by default.
 * @QCA_WLAN_VENDOR_ATTR_ENCRYPTION_TEST_CIPHER: Unsigned 32-bit value
 *	indicating the key cipher suite. Takes same values as
 *	NL80211_ATTR_KEY_CIPHER.
 * @QCA_WLAN_VENDOR_ATTR_ENCRYPTION_TEST_KEYID: Unsigned 8-bit value
 *	Key Id to be used for encryption
 * @QCA_WLAN_VENDOR_ATTR_ENCRYPTION_TEST_TK: Array of 8-bit values.
 *	Key (TK) to be used for encryption/decryption
 * @QCA_WLAN_VENDOR_ATTR_ENCRYPTION_TEST_PN: Array of 8-bit values.
 *	Packet number to be specified for encryption/decryption
 *	6 bytes for TKIP/CCMP/GCMP.
 * @QCA_WLAN_VENDOR_ATTR_ENCRYPTION_TEST_DATA: Array of 8-bit values
 *	representing the 802.11 packet (header + payload + FCS) that
 *	needs to be encrypted/decrypted.
 *	Encrypted/decrypted response from the driver will also be sent
 *	to userspace with the same attribute.
 */
enum qca_wlan_vendor_attr_encryption_test {
	QCA_WLAN_VENDOR_ATTR_ENCRYPTION_TEST_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_ENCRYPTION_TEST_NEEDS_DECRYPTION,
	QCA_WLAN_VENDOR_ATTR_ENCRYPTION_TEST_CIPHER,
	QCA_WLAN_VENDOR_ATTR_ENCRYPTION_TEST_KEYID,
	QCA_WLAN_VENDOR_ATTR_ENCRYPTION_TEST_TK,
	QCA_WLAN_VENDOR_ATTR_ENCRYPTION_TEST_PN,
	QCA_WLAN_VENDOR_ATTR_ENCRYPTION_TEST_DATA,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_ENCRYPTION_TEST_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_ENCRYPTION_TEST_MAX =
	QCA_WLAN_VENDOR_ATTR_ENCRYPTION_TEST_AFTER_LAST - 1
};

/**
 * enum qca_wlan_vendor_attr_dmg_rf_sector_type - Type of
 * sector for DMG RF sector operations.
 *
 * @QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_TYPE_RX: RX sector
 * @QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_TYPE_TX: TX sector
 */
enum qca_wlan_vendor_attr_dmg_rf_sector_type {
	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_TYPE_RX,
	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_TYPE_TX,
	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_TYPE_MAX
};

/**
 * BRP antenna limit mode
 *
 * @QCA_WLAN_VENDOR_ATTR_BRP_ANT_LIMIT_MODE_DISABLE: Disable BRP force
 *	antenna limit, BRP will be performed as usual.
 * @QCA_WLAN_VENDOR_ATTR_BRP_ANT_LIMIT_MODE_EFFECTIVE: Define maximal
 *	antennas limit. the hardware may use less antennas than the
 *	maximum limit.
 * @QCA_WLAN_VENDOR_ATTR_BRP_ANT_LIMIT_MODE_FORCE: The hardware will
 *	use exactly the specified number of antennas for BRP.
 */
enum qca_wlan_vendor_attr_brp_ant_limit_mode {
	QCA_WLAN_VENDOR_ATTR_BRP_ANT_LIMIT_MODE_DISABLE,
	QCA_WLAN_VENDOR_ATTR_BRP_ANT_LIMIT_MODE_EFFECTIVE,
	QCA_WLAN_VENDOR_ATTR_BRP_ANT_LIMIT_MODE_FORCE,
	QCA_WLAN_VENDOR_ATTR_BRP_ANT_LIMIT_MODE_MAX
};

/**
 * enum qca_wlan_vendor_attr_dmg_rf_sector_cfg - Attributes for
 * DMG RF sector configuration for a single RF module.
 * The values are defined in a compact way which closely matches
 * the way it is stored in HW registers.
 * The configuration provides values for 32 antennas and 8 distribution
 * amplifiers, and together describes the characteristics of the RF
 * sector - such as a beam in some direction with some gain.
 *
 * @QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_CFG_MODULE_INDEX: Index
 *	of RF module for this configuration.
 * @QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_CFG_ETYPE0: Bit 0 of edge
 *	amplifier gain index. Unsigned 32 bit number containing
 *	bits for all 32 antennas.
 * @QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_CFG_ETYPE1: Bit 1 of edge
 *	amplifier gain index. Unsigned 32 bit number containing
 *	bits for all 32 antennas.
 * @QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_CFG_ETYPE2: Bit 2 of edge
 *	amplifier gain index. Unsigned 32 bit number containing
 *	bits for all 32 antennas.
 * @QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_CFG_PSH_HI: Phase values
 *	for first 16 antennas, 2 bits per antenna.
 * @QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_CFG_PSH_LO: Phase values
 *	for last 16 antennas, 2 bits per antenna.
 * @QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_CFG_DTYPE_X16: Contains
 *	DTYPE values (3 bits) for each distribution amplifier, followed
 *	by X16 switch bits for each distribution amplifier. There are
 *	total of 8 distribution amplifiers.
 */
enum qca_wlan_vendor_attr_dmg_rf_sector_cfg {
	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_CFG_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_CFG_MODULE_INDEX = 1,
	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_CFG_ETYPE0 = 2,
	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_CFG_ETYPE1 = 3,
	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_CFG_ETYPE2 = 4,
	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_CFG_PSH_HI = 5,
	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_CFG_PSH_LO = 6,
	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_CFG_DTYPE_X16 = 7,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_CFG_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_CFG_MAX =
	QCA_WLAN_VENDOR_ATTR_DMG_RF_SECTOR_CFG_AFTER_LAST - 1
};

enum qca_wlan_vendor_attr_ll_stats_set {
	QCA_WLAN_VENDOR_ATTR_LL_STATS_SET_INVALID = 0,
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_SET_CONFIG_MPDU_SIZE_THRESHOLD = 1,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_SET_CONFIG_AGGRESSIVE_STATS_GATHERING = 2,
	/* keep last */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_SET_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_SET_MAX =
	QCA_WLAN_VENDOR_ATTR_LL_STATS_SET_AFTER_LAST - 1,
};

enum qca_wlan_vendor_attr_ll_stats_clr {
	QCA_WLAN_VENDOR_ATTR_LL_STATS_CLR_INVALID = 0,
	/* Unsigned 32bit bitmap for clearing statistics
	 * All radio statistics                     0x00000001
	 * cca_busy_time (within radio statistics)  0x00000002
	 * All channel stats (within radio statistics) 0x00000004
	 * All scan statistics (within radio statistics) 0x00000008
	 * All interface statistics                     0x00000010
	 * All tx rate statistics (within interface statistics) 0x00000020
	 * All ac statistics (with in interface statistics) 0x00000040
	 * All contention (min, max, avg) statistics (within ac statisctics)
	 * 0x00000080.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_CLR_CONFIG_REQ_MASK = 1,
	/* Unsigned 8 bit value: Request to stop statistics collection */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_CLR_CONFIG_STOP_REQ = 2,

	/* Unsigned 32 bit bitmap: Response from the driver
	 * for the cleared statistics
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_CLR_CONFIG_RSP_MASK = 3,
	/* Unsigned 8 bit value: Response from driver/firmware
	 * for the stop request
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_CLR_CONFIG_STOP_RSP = 4,
	/* keep last */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_CLR_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_CLR_MAX =
	QCA_WLAN_VENDOR_ATTR_LL_STATS_CLR_AFTER_LAST - 1,
};

enum qca_wlan_vendor_attr_ll_stats_get {
	QCA_WLAN_VENDOR_ATTR_LL_STATS_GET_INVALID = 0,
	/* Unsigned 32 bit value provided by the caller issuing the GET stats
	 * command. When reporting the stats results, the driver uses the same
	 * value to indicate which GET request the results correspond to.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_GET_CONFIG_REQ_ID = 1,
	/* Unsigned 32 bit value - bit mask to identify what statistics are
	 * requested for retrieval.
	 * Radio Statistics 0x00000001
	 * Interface Statistics 0x00000020
	 * All Peer Statistics 0x00000040
	 * Peer Statistics     0x00000080
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_GET_CONFIG_REQ_MASK = 2,
	/* keep last */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_GET_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_GET_MAX =
	QCA_WLAN_VENDOR_ATTR_LL_STATS_GET_AFTER_LAST - 1,
};

enum qca_wlan_vendor_attr_ll_stats_results {
	QCA_WLAN_VENDOR_ATTR_LL_STATS_INVALID = 0,
	/* Unsigned 32bit value. Used by the driver; must match the request id
	 * provided with the QCA_NL80211_VENDOR_SUBCMD_LL_STATS_GET command.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RESULTS_REQ_ID = 1,

	/* Unsigned 32 bit value */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_BEACON_RX = 2,
	/* Unsigned 32 bit value */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_MGMT_RX = 3,
	/* Unsigned 32 bit value */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_MGMT_ACTION_RX = 4,
	/* Unsigned 32 bit value */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_MGMT_ACTION_TX = 5,
	/* Signed 32 bit value */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_RSSI_MGMT = 6,
	/* Signed 32 bit value */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_RSSI_DATA = 7,
	/* Signed 32 bit value */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_RSSI_ACK = 8,

	/* Attributes of type QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_INFO_* are
	 * nested within the interface stats.
	 */

	/* Interface mode, e.g., STA, SOFTAP, IBSS, etc.
	 * Type = enum wifi_interface_mode.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_INFO_MODE = 9,
	/* Interface MAC address. An array of 6 Unsigned int8 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_INFO_MAC_ADDR = 10,
	/* Type = enum wifi_connection_state, e.g., DISCONNECTED,
	 * AUTHENTICATING, etc. valid for STA, CLI only.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_INFO_STATE = 11,
	/* Type = enum wifi_roam_state. Roaming state, e.g., IDLE or ACTIVE
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_INFO_ROAMING = 12,
	/* Unsigned 32 bit value. WIFI_CAPABILITY_XXX */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_INFO_CAPABILITIES = 13,
	/* NULL terminated SSID. An array of 33 Unsigned 8bit values */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_INFO_SSID = 14,
	/* BSSID. An array of 6 unsigned 8 bit values */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_INFO_BSSID = 15,
	/* Country string advertised by AP. An array of 3 unsigned 8 bit
	 * values.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_INFO_AP_COUNTRY_STR = 16,
	/* Country string for this association. An array of 3 unsigned 8 bit
	 * values.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_INFO_COUNTRY_STR = 17,

	/* Attributes of type QCA_WLAN_VENDOR_ATTR_LL_STATS_WMM_AC_* could
	 * be nested within the interface stats.
	 */

	/* Type = enum wifi_traffic_ac, e.g., V0, VI, BE and BK */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_WMM_AC_AC = 18,
	/* Unsigned int 32 value corresponding to respective AC */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_WMM_AC_TX_MPDU = 19,
	/* Unsigned int 32 value corresponding to respective AC */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_WMM_AC_RX_MPDU = 20,
	/* Unsigned int 32 value corresponding to respective AC */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_WMM_AC_TX_MCAST = 21,
	/* Unsigned int 32 value corresponding to respective AC */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_WMM_AC_RX_MCAST = 22,
	/* Unsigned int 32 value corresponding to respective AC */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_WMM_AC_RX_AMPDU = 23,
	/* Unsigned int 32 value corresponding to respective AC */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_WMM_AC_TX_AMPDU = 24,
	/* Unsigned int 32 value corresponding to respective AC */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_WMM_AC_MPDU_LOST = 25,
	/* Unsigned int 32 value corresponding to respective AC */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_WMM_AC_RETRIES = 26,
	/* Unsigned int 32 value corresponding to respective AC  */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_WMM_AC_RETRIES_SHORT = 27,
	/* Unsigned int 32 values corresponding to respective AC */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_WMM_AC_RETRIES_LONG = 28,
	/* Unsigned int 32 values corresponding to respective AC */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_WMM_AC_CONTENTION_TIME_MIN = 29,
	/* Unsigned int 32 values corresponding to respective AC */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_WMM_AC_CONTENTION_TIME_MAX = 30,
	/* Unsigned int 32 values corresponding to respective AC */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_WMM_AC_CONTENTION_TIME_AVG = 31,
	/* Unsigned int 32 values corresponding to respective AC */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_WMM_AC_CONTENTION_NUM_SAMPLES = 32,
	/* Unsigned 32 bit value. Number of peers */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_NUM_PEERS = 33,

	/* Attributes of type QCA_WLAN_VENDOR_ATTR_LL_STATS_PEER_INFO_* are
	 * nested within the interface stats.
	 */

	/* Type = enum wifi_peer_type. Peer type, e.g., STA, AP, P2P GO etc. */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_PEER_INFO_TYPE = 34,
	/* MAC addr corresponding to respective peer. An array of 6 unsigned
	 * 8 bit values.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_PEER_INFO_MAC_ADDRESS = 35,
	/* Unsigned int 32 bit value representing capabilities corresponding
	 * to respective peer.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_PEER_INFO_CAPABILITIES = 36,
	/* Unsigned 32 bit value. Number of rates */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_PEER_INFO_NUM_RATES = 37,

	/* Attributes of type QCA_WLAN_VENDOR_ATTR_LL_STATS_RATE_*
	 * are nested within the rate stat.
	 */

	/* Wi-Fi Rate - separate attributes defined for individual fields */

	/* Unsigned int 8 bit value; 0: OFDM, 1:CCK, 2:HT 3:VHT 4..7 reserved */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RATE_PREAMBLE = 38,
	/* Unsigned int 8 bit value; 0:1x1, 1:2x2, 3:3x3, 4:4x4 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RATE_NSS = 39,
	/* Unsigned int 8 bit value; 0:20 MHz, 1:40 MHz, 2:80 MHz, 3:160 MHz */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RATE_BW = 40,
	/* Unsigned int 8 bit value; OFDM/CCK rate code would be as per IEEE Std
	 * in the units of 0.5 Mbps HT/VHT it would be MCS index
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RATE_MCS_INDEX = 41,

	/* Unsigned 32 bit value. Bit rate in units of 100 kbps */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RATE_BIT_RATE = 42,

	/* Attributes of type QCA_WLAN_VENDOR_ATTR_LL_STATS_RATE_STAT_* could be
	 * nested within the peer info stats.
	 */

	/* Unsigned int 32 bit value. Number of successfully transmitted data
	 * packets, i.e., with ACK received corresponding to the respective
	 * rate.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RATE_TX_MPDU = 43,
	/* Unsigned int 32 bit value. Number of received data packets
	 * corresponding to the respective rate.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RATE_RX_MPDU = 44,
	/* Unsigned int 32 bit value. Number of data packet losses, i.e., no ACK
	 * received corresponding to the respective rate.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RATE_MPDU_LOST = 45,
	/* Unsigned int 32 bit value. Total number of data packet retries for
	 * the respective rate.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RATE_RETRIES = 46,
	/* Unsigned int 32 bit value. Total number of short data packet retries
	 * for the respective rate.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RATE_RETRIES_SHORT = 47,
	/* Unsigned int 32 bit value. Total number of long data packet retries
	 * for the respective rate.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RATE_RETRIES_LONG = 48,

	QCA_WLAN_VENDOR_ATTR_LL_STATS_RADIO_ID = 49,
	/* Unsigned 32 bit value. Total number of msecs the radio is awake
	 * accruing over time.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RADIO_ON_TIME = 50,
	/* Unsigned 32 bit value. Total number of msecs the radio is
	 * transmitting accruing over time.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RADIO_TX_TIME = 51,
	/* Unsigned 32 bit value. Total number of msecs the radio is in active
	 * receive accruing over time.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RADIO_RX_TIME = 52,
	/* Unsigned 32 bit value. Total number of msecs the radio is awake due
	 * to all scan accruing over time.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RADIO_ON_TIME_SCAN = 53,
	/* Unsigned 32 bit value. Total number of msecs the radio is awake due
	 * to NAN accruing over time.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RADIO_ON_TIME_NBD = 54,
	/* Unsigned 32 bit value. Total number of msecs the radio is awake due
	 * to GSCAN accruing over time.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RADIO_ON_TIME_GSCAN = 55,
	/* Unsigned 32 bit value. Total number of msecs the radio is awake due
	 * to roam scan accruing over time.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RADIO_ON_TIME_ROAM_SCAN = 56,
	/* Unsigned 32 bit value. Total number of msecs the radio is awake due
	 * to PNO scan accruing over time.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RADIO_ON_TIME_PNO_SCAN = 57,
	/* Unsigned 32 bit value. Total number of msecs the radio is awake due
	 * to Hotspot 2.0 scans and GAS exchange accruing over time.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RADIO_ON_TIME_HS20 = 58,
	/* Unsigned 32 bit value. Number of channels. */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RADIO_NUM_CHANNELS = 59,

	/* Attributes of type QCA_WLAN_VENDOR_ATTR_LL_STATS_CHANNEL_INFO_* could
	 * be nested within the channel stats.
	 */

	/* Type = enum wifi_channel_width. Channel width, e.g., 20, 40, 80 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_CHANNEL_INFO_WIDTH = 60,
	/* Unsigned 32 bit value. Primary 20 MHz channel. */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_CHANNEL_INFO_CENTER_FREQ = 61,
	/* Unsigned 32 bit value. Center frequency (MHz) first segment. */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_CHANNEL_INFO_CENTER_FREQ0 = 62,
	/* Unsigned 32 bit value. Center frequency (MHz) second segment. */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_CHANNEL_INFO_CENTER_FREQ1 = 63,

	/* Attributes of type QCA_WLAN_VENDOR_ATTR_LL_STATS_CHANNEL_* could be
	 * nested within the radio stats.
	 */

	/* Unsigned int 32 bit value representing total number of msecs the
	 * radio is awake on that channel accruing over time, corresponding to
	 * the respective channel.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_CHANNEL_ON_TIME = 64,
	/* Unsigned int 32 bit value representing total number of msecs the CCA
	 * register is busy accruing over time corresponding to the respective
	 * channel.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_CHANNEL_CCA_BUSY_TIME = 65,

	QCA_WLAN_VENDOR_ATTR_LL_STATS_NUM_RADIOS = 66,

	/* Signifies the nested list of channel attributes
	 * QCA_WLAN_VENDOR_ATTR_LL_STATS_CHANNEL_INFO_*
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_CH_INFO = 67,

	/* Signifies the nested list of peer info attributes
	 * QCA_WLAN_VENDOR_ATTR_LL_STATS_PEER_INFO_*
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_PEER_INFO = 68,

	/* Signifies the nested list of rate info attributes
	 * QCA_WLAN_VENDOR_ATTR_LL_STATS_RATE_*
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_PEER_INFO_RATE_INFO = 69,

	/* Signifies the nested list of wmm info attributes
	 * QCA_WLAN_VENDOR_ATTR_LL_STATS_WMM_AC_*
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_WMM_INFO = 70,

	/* Unsigned 8 bit value. Used by the driver; if set to 1, it indicates
	 * that more stats, e.g., peers or radio, are to follow in the next
	 * QCA_NL80211_VENDOR_SUBCMD_LL_STATS_*_RESULTS event.
	 * Otherwise, it is set to 0.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RESULTS_MORE_DATA = 71,

	/* Unsigned 64 bit value */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_AVERAGE_TSF_OFFSET = 72,

	/* Unsigned 32 bit value */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_LEAKY_AP_DETECTED = 73,

	/* Unsigned 32 bit value */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_LEAKY_AP_AVG_NUM_FRAMES_LEAKED = 74,

	/* Unsigned 32 bit value */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_LEAKY_AP_GUARD_TIME = 75,

	/* Unsigned 32 bit value */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_TYPE = 76,

	/* Unsigned 32 bit value */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RADIO_NUM_TX_LEVELS = 77,

	/* Number of msecs the radio spent in transmitting for each power level
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_RADIO_TX_TIME_PER_LEVEL = 78,

	/* Unsigned 32 bit value */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_RTS_SUCC_CNT = 79,
	/* Unsigned 32 bit value */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_RTS_FAIL_CNT = 80,
	/* Unsigned 32 bit value */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_PPDU_SUCC_CNT = 81,
	/* Unsigned 32 bit value */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_IFACE_PPDU_FAIL_CNT = 82,

	/* Unsigned int 32 value.
	 * Pending MSDUs corresponding to respective AC.
	 */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_WMM_AC_PENDING_MSDU = 83,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_LL_STATS_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_LL_STATS_MAX =
	QCA_WLAN_VENDOR_ATTR_LL_STATS_AFTER_LAST - 1,
};

enum qca_wlan_vendor_attr_ll_stats_type {
	QCA_NL80211_VENDOR_SUBCMD_LL_STATS_TYPE_INVALID = 0,
	QCA_NL80211_VENDOR_SUBCMD_LL_STATS_TYPE_RADIO = 1,
	QCA_NL80211_VENDOR_SUBCMD_LL_STATS_TYPE_IFACE = 2,
	QCA_NL80211_VENDOR_SUBCMD_LL_STATS_TYPE_PEERS = 3,

	/* keep last */
	QCA_NL80211_VENDOR_SUBCMD_LL_STATS_TYPE_AFTER_LAST,
	QCA_NL80211_VENDOR_SUBCMD_LL_STATS_TYPE_MAX =
	QCA_NL80211_VENDOR_SUBCMD_LL_STATS_TYPE_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_tdls_configuration - Attributes for
 * TDLS configuration to the host driver.
 *
 * @QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_TRIGGER_MODE: Configure the TDLS trigger
 *	mode in the host driver. enum qca_wlan_vendor_tdls_trigger_mode
 *	represents the different TDLS trigger modes.
 * @QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_TX_STATS_PERIOD: Duration (u32) within
 *      which QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_TX_THRESHOLD number
 *      of packets shall meet the criteria for implicit TDLS setup.
 * @QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_TX_THRESHOLD: Number (u32) of Tx/Rx packets
 *      within a duration QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_TX_STATS_PERIOD
 *      to initiate a TDLS setup.
 * @QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_DISCOVERY_PERIOD: Time (u32) to initiate
 *      a TDLS Discovery to the peer.
 * @QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_MAX_DISCOVERY_ATTEMPT: Max number (u32) of
 *      discovery attempts to know the TDLS capability of the peer. A peer is
 *      marked as TDLS not capable if there is no response for all the attempts.
 * @QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_IDLE_TIMEOUT: Represents a duration (u32)
 *      within which QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_IDLE_PACKET_THRESHOLD
 *      number of TX / RX frames meet the criteria for TDLS teardown.
 * @QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_IDLE_PACKET_THRESHOLD: Minimum number (u32)
 *      of Tx/Rx packets within a duration
 *      QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_IDLE_TIMEOUT to tear down a TDLS link.
 * @QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_SETUP_RSSI_THRESHOLD: Threshold
 *	corresponding to the RSSI of the peer below which a TDLS setup is
 *	triggered.
 * @QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_TEARDOWN_RSSI_THRESHOLD: Threshold
 *	corresponding to the RSSI of the peer above which a TDLS teardown is
 *	triggered.
 */
enum qca_wlan_vendor_attr_tdls_configuration {
	QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_TRIGGER_MODE = 1,

	/* Attributes configuring the TDLS Implicit Trigger */
	QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_TX_STATS_PERIOD = 2,
	QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_TX_THRESHOLD = 3,
	QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_DISCOVERY_PERIOD = 4,
	QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_MAX_DISCOVERY_ATTEMPT = 5,
	QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_IDLE_TIMEOUT = 6,
	QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_IDLE_PACKET_THRESHOLD = 7,
	QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_SETUP_RSSI_THRESHOLD = 8,
	QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_TEARDOWN_RSSI_THRESHOLD = 9,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_MAX =
	QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_AFTER_LAST - 1
};

/**
 * enum qca_wlan_vendor_tdls_trigger_mode: Represents the TDLS trigger mode in
 *	the driver
 *
 * The following are the different values for
 *	QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_TRIGGER_MODE.
 *
 * @QCA_WLAN_VENDOR_TDLS_TRIGGER_MODE_EXPLICIT: The trigger to initiate/teardown
 *	the TDLS connection to a respective peer comes from the user space.
 *	wpa_supplicant provides the commands TDLS_SETUP, TDLS_TEARDOWN,
 *	TDLS_DISCOVER to do this.
 * @QCA_WLAN_VENDOR_TDLS_TRIGGER_MODE_IMPLICIT: Host driver triggers this TDLS
 *	setup/teardown to the eligible peer once the configured criteria
 *	(such as TX/RX threshold, RSSI) is met. The attributes
 *	in QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_IMPLICIT_PARAMS correspond to
 *	the different configuration criteria for the TDLS trigger from the
 *	host driver.
 * @QCA_WLAN_VENDOR_TDLS_TRIGGER_MODE_EXTERNAL: Enables the driver to trigger
 *	the TDLS setup / teardown through the implicit mode only to the
 *	configured MAC addresses (wpa_supplicant, with tdls_external_control=1,
 *	configures the MAC address through TDLS_SETUP / TDLS_TEARDOWN commands).
 *	External mode works on top of the implicit mode. Thus the host driver
 *	is expected to configure in TDLS Implicit mode too to operate in
 *	External mode.
 *	Configuring External mode alone without	Implicit mode is invalid.
 *
 * All the above implementations work as expected only when the host driver
 * advertises the capability WPA_DRIVER_FLAGS_TDLS_EXTERNAL_SETUP - representing
 * that the TDLS message exchange is not internal to the host driver, but
 * depends on wpa_supplicant to do the message exchange.
 */
enum qca_wlan_vendor_tdls_trigger_mode {
	QCA_WLAN_VENDOR_TDLS_TRIGGER_MODE_EXPLICIT = 1 << 0,
	QCA_WLAN_VENDOR_TDLS_TRIGGER_MODE_IMPLICIT = 1 << 1,
	QCA_WLAN_VENDOR_TDLS_TRIGGER_MODE_EXTERNAL = 1 << 2,
};

/**
 * enum qca_vendor_attr_sar_limits_selections - Source of SAR power limits
 * @QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SELECT_BDF0: Select SAR profile #0
 *	that is hard-coded in the Board Data File (BDF).
 * @QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SELECT_BDF1: Select SAR profile #1
 *	that is hard-coded in the Board Data File (BDF).
 * @QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SELECT_BDF2: Select SAR profile #2
 *	that is hard-coded in the Board Data File (BDF).
 * @QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SELECT_BDF3: Select SAR profile #3
 *	that is hard-coded in the Board Data File (BDF).
 * @QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SELECT_BDF4: Select SAR profile #4
 *	that is hard-coded in the Board Data File (BDF).
 * @QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SELECT_NONE: Do not select any
 *	source of SAR power limits, thereby disabling the SAR power
 *	limit feature.
 * @QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SELECT_USER: Select the SAR power
 *	limits configured by %QCA_NL80211_VENDOR_SUBCMD_SET_SAR.
 * @QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SELECT_V2_0: Select the SAR power
 *	limits version 2.0 configured by %QCA_NL80211_VENDOR_SUBCMD_SET_SAR.
 *
 * This enumerates the valid set of values that may be supplied for
 * attribute %QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SELECT in an instance of
 * the %QCA_NL80211_VENDOR_SUBCMD_SET_SAR_LIMITS vendor command or in
 * the response to an instance of the
 * %QCA_NL80211_VENDOR_SUBCMD_GET_SAR_LIMITS vendor command.
 */
enum qca_vendor_attr_sar_limits_selections {
	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SELECT_BDF0 = 0,
	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SELECT_BDF1 = 1,
	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SELECT_BDF2 = 2,
	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SELECT_BDF3 = 3,
	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SELECT_BDF4 = 4,
	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SELECT_NONE = 5,
	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SELECT_USER = 6,
	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SELECT_V2_0 = 7,
};

/**
 * enum qca_vendor_attr_sar_limits_spec_modulations -
 *	SAR limits specification modulation
 * @QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_MODULATION_CCK -
 *	CCK modulation
 * @QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_MODULATION_OFDM -
 *	OFDM modulation
 *
 * This enumerates the valid set of values that may be supplied for
 * attribute %QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_MODULATION in an
 * instance of attribute %QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC in an
 * instance of the %QCA_NL80211_VENDOR_SUBCMD_SET_SAR_LIMITS vendor
 * command or in the response to an instance of the
 * %QCA_NL80211_VENDOR_SUBCMD_GET_SAR_LIMITS vendor command.
 */
enum qca_vendor_attr_sar_limits_spec_modulations {
	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_MODULATION_CCK = 0,
	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_MODULATION_OFDM = 1,
};

/**
 * enum qca_vendor_attr_sar_limits - Attributes for SAR power limits
 *
 * @QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SAR_ENABLE: Optional (u32) value to
 *	select which SAR power limit table should be used. Valid
 *	values are enumerated in enum
 *	%qca_vendor_attr_sar_limits_selections. The existing SAR
 *	power limit selection is unchanged if this attribute is not
 *	present.
 *
 * @QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_NUM_SPECS: Optional (u32) value
 *	which specifies the number of SAR power limit specifications
 *	which will follow.
 *
 * @QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC: Nested array of SAR power
 *	limit specifications. The number of specifications is
 *	specified by @QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_NUM_SPECS. Each
 *	specification contains a set of
 *	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_* attributes. A
 *	specification is uniquely identified by the attributes
 *	%QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_BAND,
 *	%QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_CHAIN, and
 *	%QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_MODULATION and always
 *	contains as a payload the attribute
 *	%QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_POWER_LIMIT,
 *	%QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_POWER_LIMIT_INDEX.
 *	Either %QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_POWER_LIMIT or
 *	%QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_POWER_LIMIT_INDEX is
 *	needed based upon the value of
 *	%QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SAR_ENABLE.
 *
 * @QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_BAND: Optional (u32) value to
 *	indicate for which band this specification applies. Valid
 *	values are enumerated in enum %nl80211_band (although not all
 *	bands may be supported by a given device). If the attribute is
 *	not supplied then the specification will be applied to all
 *	supported bands.
 *
 * @QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_CHAIN: Optional (u32) value
 *	to indicate for which antenna chain this specification
 *	applies, i.e. 1 for chain 1, 2 for chain 2, etc. If the
 *	attribute is not supplied then the specification will be
 *	applied to all chains.
 *
 * @QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_MODULATION: Optional (u32)
 *	value to indicate for which modulation scheme this
 *	specification applies. Valid values are enumerated in enum
 *	%qca_vendor_attr_sar_limits_spec_modulations. If the attribute
 *	is not supplied then the specification will be applied to all
 *	modulation schemes.
 *
 * @QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_POWER_LIMIT: Required (u32)
 *	value to specify the actual power limit value in units of 0.5
 *	dBm (i.e., a value of 11 represents 5.5 dBm).
 *	This is required, when %QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SELECT is
 *	%QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SELECT_USER.
 *
 * @QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_POWER_LIMIT_INDEX: Required (u32)
 *	value to indicate SAR V2 indices (0 - 11) to select SAR V2 profiles.
 *	This is required, when %QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SELECT is
 *	%QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SELECT_V2_0.
 *
 * These attributes are used with %QCA_NL80211_VENDOR_SUBCMD_SET_SAR_LIMITS
 * and %QCA_NL80211_VENDOR_SUBCMD_GET_SAR_LIMITS.
 */
enum qca_vendor_attr_sar_limits {
	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SAR_ENABLE = 1,
	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_NUM_SPECS = 2,
	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC = 3,
	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_BAND = 4,
	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_CHAIN = 5,
	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_MODULATION = 6,
	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_POWER_LIMIT = 7,
	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_SPEC_POWER_LIMIT_INDEX = 8,

	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_MAX =
		QCA_WLAN_VENDOR_ATTR_SAR_LIMITS_AFTER_LAST - 1
};

/**
 * enum qca_wlan_vendor_attr_get_wifi_info: Attributes for data used by
 * QCA_NL80211_VENDOR_SUBCMD_GET_WIFI_INFO sub command.
 */
enum qca_wlan_vendor_attr_get_wifi_info {
	QCA_WLAN_VENDOR_ATTR_WIFI_INFO_GET_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_WIFI_INFO_DRIVER_VERSION = 1,
	QCA_WLAN_VENDOR_ATTR_WIFI_INFO_FIRMWARE_VERSION = 2,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_WIFI_INFO_GET_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_WIFI_INFO_GET_MAX =
	QCA_WLAN_VENDOR_ATTR_WIFI_INFO_GET_AFTER_LAST - 1,
};

/*
 * enum qca_wlan_vendor_attr_wifi_logger_start: Attributes for data used by
 * QCA_NL80211_VENDOR_SUBCMD_WIFI_LOGGER_START sub command.
 */
enum qca_wlan_vendor_attr_wifi_logger_start {
	QCA_WLAN_VENDOR_ATTR_WIFI_LOGGER_START_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_WIFI_LOGGER_RING_ID = 1,
	QCA_WLAN_VENDOR_ATTR_WIFI_LOGGER_VERBOSE_LEVEL = 2,
	QCA_WLAN_VENDOR_ATTR_WIFI_LOGGER_FLAGS = 3,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_WIFI_LOGGER_START_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_WIFI_LOGGER_START_GET_MAX =
	QCA_WLAN_VENDOR_ATTR_WIFI_LOGGER_START_AFTER_LAST - 1,
};

enum qca_wlan_vendor_attr_logger_results {
	QCA_WLAN_VENDOR_ATTR_LOGGER_RESULTS_INVALID = 0,

	/* Unsigned 32-bit value; must match the request Id supplied by
	 * Wi-Fi HAL in the corresponding subcmd NL msg.
	 */
	QCA_WLAN_VENDOR_ATTR_LOGGER_RESULTS_REQUEST_ID = 1,

	/* Unsigned 32-bit value; used to indicate the size of memory
	 * dump to be allocated.
	 */
	QCA_WLAN_VENDOR_ATTR_LOGGER_RESULTS_MEMDUMP_SIZE = 2,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_LOGGER_RESULTS_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_LOGGER_RESULTS_MAX =
	QCA_WLAN_VENDOR_ATTR_LOGGER_RESULTS_AFTER_LAST - 1,
};

enum qca_wlan_vendor_attr_roaming_config_params {
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_INVALID = 0,

	QCA_WLAN_VENDOR_ATTR_ROAMING_SUBCMD = 1,
	QCA_WLAN_VENDOR_ATTR_ROAMING_REQ_ID = 2,

	/* Attributes for wifi_set_ssid_white_list */
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_WHITE_LIST_SSID_NUM_NETWORKS = 3,
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_WHITE_LIST_SSID_LIST = 4,
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_WHITE_LIST_SSID = 5,

	/* Attributes for set_roam_params */
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_A_BAND_BOOST_THRESHOLD = 6,
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_A_BAND_PENALTY_THRESHOLD = 7,
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_A_BAND_BOOST_FACTOR = 8,
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_A_BAND_PENALTY_FACTOR = 9,
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_A_BAND_MAX_BOOST = 10,
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_LAZY_ROAM_HISTERESYS = 11,
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_ALERT_ROAM_RSSI_TRIGGER = 12,

	/* Attribute for set_lazy_roam */
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_SET_LAZY_ROAM_ENABLE = 13,

	/* Attribute for set_lazy_roam with preferences */
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_SET_BSSID_PREFS = 14,
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_SET_LAZY_ROAM_NUM_BSSID = 15,
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_SET_LAZY_ROAM_BSSID = 16,
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_SET_LAZY_ROAM_RSSI_MODIFIER = 17,

	/* Attribute for set_blacklist bssid params */
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_SET_BSSID_PARAMS = 18,
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_SET_BSSID_PARAMS_NUM_BSSID = 19,
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_SET_BSSID_PARAMS_BSSID = 20,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_MAX =
	QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_AFTER_LAST - 1,
};

/*
 * enum qca_wlan_vendor_attr_roam_subcmd: Attributes for data used by
 * QCA_NL80211_VENDOR_SUBCMD_ROAM sub command.
 */
enum qca_wlan_vendor_attr_roam_subcmd {
	QCA_WLAN_VENDOR_ATTR_ROAM_SUBCMD_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_ROAM_SUBCMD_SSID_WHITE_LIST = 1,
	QCA_WLAN_VENDOR_ATTR_ROAM_SUBCMD_SET_GSCAN_ROAM_PARAMS = 2,
	QCA_WLAN_VENDOR_ATTR_ROAM_SUBCMD_SET_LAZY_ROAM = 3,
	QCA_WLAN_VENDOR_ATTR_ROAM_SUBCMD_SET_BSSID_PREFS = 4,
	QCA_WLAN_VENDOR_ATTR_ROAM_SUBCMD_SET_BSSID_PARAMS = 5,
	QCA_WLAN_VENDOR_ATTR_ROAM_SUBCMD_SET_BLACKLIST_BSSID = 6,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_ROAM_SUBCMD_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_ROAM_SUBCMD_MAX =
	QCA_WLAN_VENDOR_ATTR_ROAM_SUBCMD_AFTER_LAST - 1,
};

enum qca_wlan_vendor_attr_gscan_config_params {
	QCA_WLAN_VENDOR_ATTR_GSCAN_SUBCMD_CONFIG_PARAM_INVALID = 0,

	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_SUBCMD_CONFIG_PARAM_REQUEST_ID = 1,

	/* Attributes for data used by
	 * QCA_NL80211_VENDOR_SUBCMD_GSCAN_GET_VALID_CHANNELS sub command.
	 */
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_GET_VALID_CHANNELS_CONFIG_PARAM_WIFI_BAND
	= 2,
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_GET_VALID_CHANNELS_CONFIG_PARAM_MAX_CHANNELS
	= 3,

	/* Attributes for input params used by
	 * QCA_NL80211_VENDOR_SUBCMD_GSCAN_START sub command.
	 */

	/* Unsigned 32-bit value; channel frequency */
	QCA_WLAN_VENDOR_ATTR_GSCAN_CHANNEL_SPEC_CHANNEL = 4,
	/* Unsigned 32-bit value; dwell time in ms. */
	QCA_WLAN_VENDOR_ATTR_GSCAN_CHANNEL_SPEC_DWELL_TIME = 5,
	/* Unsigned 8-bit value; 0: active; 1: passive; N/A for DFS */
	QCA_WLAN_VENDOR_ATTR_GSCAN_CHANNEL_SPEC_PASSIVE = 6,
	/* Unsigned 8-bit value; channel class */
	QCA_WLAN_VENDOR_ATTR_GSCAN_CHANNEL_SPEC_CLASS = 7,

	/* Unsigned 8-bit value; bucket index, 0 based */
	QCA_WLAN_VENDOR_ATTR_GSCAN_BUCKET_SPEC_INDEX = 8,
	/* Unsigned 8-bit value; band. */
	QCA_WLAN_VENDOR_ATTR_GSCAN_BUCKET_SPEC_BAND = 9,
	/* Unsigned 32-bit value; desired period, in ms. */
	QCA_WLAN_VENDOR_ATTR_GSCAN_BUCKET_SPEC_PERIOD = 10,
	/* Unsigned 8-bit value; report events semantics. */
	QCA_WLAN_VENDOR_ATTR_GSCAN_BUCKET_SPEC_REPORT_EVENTS = 11,
	/* Unsigned 32-bit value. Followed by a nested array of
	 * GSCAN_CHANNEL_SPEC_* attributes.
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_BUCKET_SPEC_NUM_CHANNEL_SPECS = 12,

	/* Array of QCA_WLAN_VENDOR_ATTR_GSCAN_CHANNEL_SPEC_* attributes.
	 * Array size: QCA_WLAN_VENDOR_ATTR_GSCAN_BUCKET_SPEC_NUM_CHANNEL_SPECS
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_CHANNEL_SPEC = 13,

	/* Unsigned 32-bit value; base timer period in ms. */
	QCA_WLAN_VENDOR_ATTR_GSCAN_SCAN_CMD_PARAMS_BASE_PERIOD = 14,
	/* Unsigned 32-bit value; number of APs to store in each scan in the
	 * BSSID/RSSI history buffer (keep the highest RSSI APs).
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_SCAN_CMD_PARAMS_MAX_AP_PER_SCAN = 15,
	/* Unsigned 8-bit value; in %, when scan buffer is this much full, wake
	 * up AP.
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_SCAN_CMD_PARAMS_REPORT_THRESHOLD_PERCENT
	= 16,

	/* Unsigned 8-bit value; number of scan bucket specs; followed by a
	 * nested array of_GSCAN_BUCKET_SPEC_* attributes and values. The size
	 * of the array is determined by NUM_BUCKETS.
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_SCAN_CMD_PARAMS_NUM_BUCKETS = 17,

	/* Array of QCA_WLAN_VENDOR_ATTR_GSCAN_BUCKET_SPEC_* attributes.
	 * Array size: QCA_WLAN_VENDOR_ATTR_GSCAN_SCAN_CMD_PARAMS_NUM_BUCKETS
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_BUCKET_SPEC = 18,

	/* Unsigned 8-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_GET_CACHED_SCAN_RESULTS_CONFIG_PARAM_FLUSH
	= 19,
	/* Unsigned 32-bit value; maximum number of results to be returned. */
	QCA_WLAN_VENDOR_ATTR_GSCAN_GET_CACHED_SCAN_RESULTS_CONFIG_PARAM_MAX
	= 20,

	/* An array of 6 x unsigned 8-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_AP_THRESHOLD_PARAM_BSSID = 21,
	/* Signed 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_AP_THRESHOLD_PARAM_RSSI_LOW = 22,
	/* Signed 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_AP_THRESHOLD_PARAM_RSSI_HIGH = 23,
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_AP_THRESHOLD_PARAM_CHANNEL = 24,

	/* Number of hotlist APs as unsigned 32-bit value, followed by a nested
	 * array of AP_THRESHOLD_PARAM attributes and values. The size of the
	 * array is determined by NUM_AP.
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_BSSID_HOTLIST_PARAMS_NUM_AP = 25,

	/* Array of QCA_WLAN_VENDOR_ATTR_GSCAN_AP_THRESHOLD_PARAM_* attributes.
	 * Array size: QCA_WLAN_VENDOR_ATTR_GSCAN_BUCKET_SPEC_NUM_CHANNEL_SPECS
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_AP_THRESHOLD_PARAM = 26,

	/* Unsigned 32-bit value; number of samples for averaging RSSI. */
	QCA_WLAN_VENDOR_ATTR_GSCAN_SIGNIFICANT_CHANGE_PARAMS_RSSI_SAMPLE_SIZE
	= 27,
	/* Unsigned 32-bit value; number of samples to confirm AP loss. */
	QCA_WLAN_VENDOR_ATTR_GSCAN_SIGNIFICANT_CHANGE_PARAMS_LOST_AP_SAMPLE_SIZE
	= 28,
	/* Unsigned 32-bit value; number of APs breaching threshold. */
	QCA_WLAN_VENDOR_ATTR_GSCAN_SIGNIFICANT_CHANGE_PARAMS_MIN_BREACHING = 29,
	/* Unsigned 32-bit value; number of APs. Followed by an array of
	 * AP_THRESHOLD_PARAM attributes. Size of the array is NUM_AP.
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_SIGNIFICANT_CHANGE_PARAMS_NUM_AP = 30,
	/* Unsigned 32-bit value; number of samples to confirm AP loss. */
	QCA_WLAN_VENDOR_ATTR_GSCAN_BSSID_HOTLIST_PARAMS_LOST_AP_SAMPLE_SIZE
	= 31,
	/* Unsigned 32-bit value. If max_period is non zero or different than
	 * period, then this bucket is an exponential backoff bucket.
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_BUCKET_SPEC_MAX_PERIOD = 32,
	/* Unsigned 32-bit value. */
	QCA_WLAN_VENDOR_ATTR_GSCAN_BUCKET_SPEC_BASE = 33,
	/* Unsigned 32-bit value. For exponential back off bucket, number of
	 * scans to perform for a given period.
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_BUCKET_SPEC_STEP_COUNT = 34,
	/* Unsigned 8-bit value; in number of scans, wake up AP after these
	 * many scans.
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_SCAN_CMD_PARAMS_REPORT_THRESHOLD_NUM_SCANS
	= 35,

	/* Attributes for data used by
	 * QCA_NL80211_VENDOR_SUBCMD_GSCAN_SET_SSID_HOTLIST sub command.
	 */
	/* Unsigned 3-2bit value; number of samples to confirm SSID loss. */
	QCA_WLAN_VENDOR_ATTR_GSCAN_SSID_HOTLIST_PARAMS_LOST_SSID_SAMPLE_SIZE
	= 36,
	/* Number of hotlist SSIDs as unsigned 32-bit value, followed by a
	 * nested array of SSID_THRESHOLD_PARAM_* attributes and values. The
	 * size of the array is determined by NUM_SSID.
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_SSID_HOTLIST_PARAMS_NUM_SSID = 37,
	/* Array of QCA_WLAN_VENDOR_ATTR_GSCAN_SSID_THRESHOLD_PARAM_*
	 * attributes.
	 * Array size: QCA_WLAN_VENDOR_ATTR_GSCAN_SSID_HOTLIST_PARAMS_NUM_SSID
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_SSID_THRESHOLD_PARAM = 38,

	/* An array of 33 x unsigned 8-bit value; NULL terminated SSID */
	QCA_WLAN_VENDOR_ATTR_GSCAN_SSID_THRESHOLD_PARAM_SSID = 39,
	/* Unsigned 8-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_SSID_THRESHOLD_PARAM_BAND = 40,
	/* Signed 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_SSID_THRESHOLD_PARAM_RSSI_LOW = 41,
	/* Signed 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_SSID_THRESHOLD_PARAM_RSSI_HIGH = 42,
	/* Unsigned 32-bit value; a bitmask with additional gscan config flag.
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_CONFIGURATION_FLAGS = 43,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_GSCAN_SUBCMD_CONFIG_PARAM_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_GSCAN_SUBCMD_CONFIG_PARAM_MAX =
	QCA_WLAN_VENDOR_ATTR_GSCAN_SUBCMD_CONFIG_PARAM_AFTER_LAST - 1,
};

enum qca_wlan_vendor_attr_gscan_results {
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_INVALID = 0,

	/* Unsigned 32-bit value; must match the request Id supplied by
	 * Wi-Fi HAL in the corresponding subcmd NL msg.
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_REQUEST_ID = 1,

	/* Unsigned 32-bit value; used to indicate the status response from
	 * firmware/driver for the vendor sub-command.
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_STATUS = 2,

	/* GSCAN Valid Channels attributes */
	/* Unsigned 32bit value; followed by a nested array of CHANNELS. */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_NUM_CHANNELS = 3,
	/* An array of NUM_CHANNELS x unsigned 32-bit value integers
	 * representing channel numbers.
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_CHANNELS = 4,

	/* GSCAN Capabilities attributes */
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_CAPABILITIES_MAX_SCAN_CACHE_SIZE = 5,
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_CAPABILITIES_MAX_SCAN_BUCKETS = 6,
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_CAPABILITIES_MAX_AP_CACHE_PER_SCAN
	= 7,
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_CAPABILITIES_MAX_RSSI_SAMPLE_SIZE
	= 8,
	/* Signed 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_CAPABILITIES_MAX_SCAN_REPORTING_THRESHOLD
	= 9,
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_CAPABILITIES_MAX_HOTLIST_BSSIDS = 10,
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_CAPABILITIES_MAX_SIGNIFICANT_WIFI_CHANGE_APS
	= 11,
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_CAPABILITIES_MAX_BSSID_HISTORY_ENTRIES
	= 12,

	/* GSCAN Attributes used with
	 * QCA_NL80211_VENDOR_SUBCMD_GSCAN_SCAN_RESULTS_AVAILABLE sub-command.
	 */

	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_NUM_RESULTS_AVAILABLE = 13,

	/* GSCAN attributes used with
	 * QCA_NL80211_VENDOR_SUBCMD_GSCAN_FULL_SCAN_RESULT sub-command.
	 */

	/* An array of NUM_RESULTS_AVAILABLE x
	 * QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_SCAN_RESULT_*
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_LIST = 14,

	/* Unsigned 64-bit value; age of sample at the time of retrieval */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_SCAN_RESULT_TIME_STAMP = 15,
	/* 33 x unsigned 8-bit value; NULL terminated SSID */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_SCAN_RESULT_SSID = 16,
	/* An array of 6 x unsigned 8-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_SCAN_RESULT_BSSID = 17,
	/* Unsigned 32-bit value; channel frequency in MHz */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_SCAN_RESULT_CHANNEL = 18,
	/* Signed 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_SCAN_RESULT_RSSI = 19,
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_SCAN_RESULT_RTT = 20,
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_SCAN_RESULT_RTT_SD = 21,
	/* Unsigned 16-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_SCAN_RESULT_BEACON_PERIOD = 22,
	/* Unsigned 16-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_SCAN_RESULT_CAPABILITY = 23,
	/* Unsigned 32-bit value; size of the IE DATA blob */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_SCAN_RESULT_IE_LENGTH = 24,
	/* An array of IE_LENGTH x unsigned 8-bit value; blob of all the
	 * information elements found in the beacon; this data should be a
	 * packed list of wifi_information_element objects, one after the
	 * other.
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_SCAN_RESULT_IE_DATA = 25,

	/* Unsigned 8-bit value; set by driver to indicate more scan results are
	 * available.
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_SCAN_RESULT_MORE_DATA = 26,

	/* GSCAN attributes for
	 * QCA_NL80211_VENDOR_SUBCMD_GSCAN_SCAN_EVENT sub-command.
	 */
	/* Unsigned 8-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_SCAN_EVENT_TYPE = 27,
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_SCAN_EVENT_STATUS = 28,

	/* GSCAN attributes for
	 * QCA_NL80211_VENDOR_SUBCMD_GSCAN_HOTLIST_AP_FOUND sub-command.
	 */
	/* Use attr QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_NUM_RESULTS_AVAILABLE
	 * to indicate number of results.
	 * Also, use QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_LIST to indicate the
	 * list of results.
	 */

	/* GSCAN attributes for
	 * QCA_NL80211_VENDOR_SUBCMD_GSCAN_SIGNIFICANT_CHANGE sub-command.
	 */
	/* An array of 6 x unsigned 8-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_SIGNIFICANT_CHANGE_RESULT_BSSID = 29,
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_SIGNIFICANT_CHANGE_RESULT_CHANNEL
	= 30,
	/* Unsigned 32-bit value. */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_SIGNIFICANT_CHANGE_RESULT_NUM_RSSI
	= 31,
	/* A nested array of signed 32-bit RSSI values. Size of the array is
	 * determined by (NUM_RSSI of SIGNIFICANT_CHANGE_RESULT_NUM_RSSI.
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_SIGNIFICANT_CHANGE_RESULT_RSSI_LIST
	= 32,

	/* GSCAN attributes used with
	 * QCA_NL80211_VENDOR_SUBCMD_GSCAN_GET_CACHED_RESULTS sub-command.
	 */
	/* Use attr QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_NUM_RESULTS_AVAILABLE
	 * to indicate number of gscan cached results returned.
	 * Also, use QCA_WLAN_VENDOR_ATTR_GSCAN_CACHED_RESULTS_LIST to indicate
	 *  the list of gscan cached results.
	 */

	/* An array of NUM_RESULTS_AVAILABLE x
	 * QCA_NL80211_VENDOR_ATTR_GSCAN_CACHED_RESULTS_*
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_CACHED_RESULTS_LIST = 33,
	/* Unsigned 32-bit value; a unique identifier for the scan unit. */
	QCA_WLAN_VENDOR_ATTR_GSCAN_CACHED_RESULTS_SCAN_ID = 34,
	/* Unsigned 32-bit value; a bitmask w/additional information about scan.
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_CACHED_RESULTS_FLAGS = 35,
	/* Use attr QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_NUM_RESULTS_AVAILABLE
	 * to indicate number of wifi scan results/bssids retrieved by the scan.
	 * Also, use QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_LIST to indicate the
	 * list of wifi scan results returned for each cached result block.
	 */

	/* GSCAN attributes for
	 * QCA_NL80211_VENDOR_SUBCMD_PNO_NETWORK_FOUND sub-command.
	 */
	/* Use QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_NUM_RESULTS_AVAILABLE for
	 * number of results.
	 * Use QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_LIST to indicate the nested
	 * list of wifi scan results returned for each
	 * wifi_passpoint_match_result block.
	 * Array size: QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_NUM_RESULTS_AVAILABLE.
	 */

	/* GSCAN attributes for
	 * QCA_NL80211_VENDOR_SUBCMD_PNO_PASSPOINT_NETWORK_FOUND sub-command.
	 */
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_PNO_RESULTS_PASSPOINT_NETWORK_FOUND_NUM_MATCHES
	= 36,
	/* A nested array of
	 * QCA_WLAN_VENDOR_ATTR_GSCAN_PNO_RESULTS_PASSPOINT_MATCH_*
	 * attributes. Array size =
	 * *_ATTR_GSCAN_PNO_RESULTS_PASSPOINT_NETWORK_FOUND_NUM_MATCHES.
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_PNO_RESULTS_PASSPOINT_MATCH_RESULT_LIST = 37,

	/* Unsigned 32-bit value; network block id for the matched network */
	QCA_WLAN_VENDOR_ATTR_GSCAN_PNO_RESULTS_PASSPOINT_MATCH_ID = 38,
	/* Use QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_LIST to indicate the nested
	 * list of wifi scan results returned for each
	 * wifi_passpoint_match_result block.
	 */
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_GSCAN_PNO_RESULTS_PASSPOINT_MATCH_ANQP_LEN = 39,
	/* An array size of PASSPOINT_MATCH_ANQP_LEN of unsigned 8-bit values;
	 * ANQP data in the information_element format.
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_PNO_RESULTS_PASSPOINT_MATCH_ANQP = 40,

	/* Unsigned 32-bit value; a GSCAN Capabilities attribute. */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_CAPABILITIES_MAX_HOTLIST_SSIDS = 41,
	/* Unsigned 32-bit value; a GSCAN Capabilities attribute. */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_CAPABILITIES_MAX_NUM_EPNO_NETS = 42,
	/* Unsigned 32-bit value; a GSCAN Capabilities attribute. */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_CAPABILITIES_MAX_NUM_EPNO_NETS_BY_SSID
	= 43,
	/* Unsigned 32-bit value; a GSCAN Capabilities attribute. */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_CAPABILITIES_MAX_NUM_WHITELISTED_SSID
	= 44,

	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_BUCKETS_SCANNED = 45,

	/* Unsigned 32-bit value; a GSCAN Capabilities attribute.
	 * This is used to limit the maximum number of BSSIDs while sending
	 * the vendor command QCA_NL80211_VENDOR_SUBCMD_ROAM with attributes
	 * QCA_WLAN_VENDOR_ATTR_ROAM_SUBCMD_SET_BLACKLIST_BSSID and
	 * QCA_WLAN_VENDOR_ATTR_ROAMING_PARAM_SET_BSSID_PARAMS_NUM_BSSID.
	 */
	QCA_WLAN_VENDOR_ATTR_GSCAN_MAX_NUM_BLACKLISTED_BSSID = 46,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_MAX =
	QCA_WLAN_VENDOR_ATTR_GSCAN_RESULTS_AFTER_LAST - 1,
};

enum qca_wlan_vendor_attr_pno_config_params {
	QCA_WLAN_VENDOR_ATTR_PNO_INVALID = 0,
	/* Attributes for data used by
	 * QCA_NL80211_VENDOR_SUBCMD_PNO_SET_PASSPOINT_LIST sub command.
	 */
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_PNO_PASSPOINT_LIST_PARAM_NUM = 1,
	/* Array of nested QCA_WLAN_VENDOR_ATTR_PNO_PASSPOINT_NETWORK_PARAM_*
	 * attributes. Array size =
	 * QCA_WLAN_VENDOR_ATTR_PNO_PASSPOINT_LIST_PARAM_NUM.
	 */
	QCA_WLAN_VENDOR_ATTR_PNO_PASSPOINT_LIST_PARAM_NETWORK_ARRAY = 2,

	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_PNO_PASSPOINT_NETWORK_PARAM_ID = 3,
	/* An array of 256 x unsigned 8-bit value; NULL terminated UTF-8 encoded
	 * realm, 0 if unspecified.
	 */
	QCA_WLAN_VENDOR_ATTR_PNO_PASSPOINT_NETWORK_PARAM_REALM = 4,
	/* An array of 16 x unsigned 32-bit value; roaming consortium ids to
	 * match, 0 if unspecified.
	 */
	QCA_WLAN_VENDOR_ATTR_PNO_PASSPOINT_NETWORK_PARAM_ROAM_CNSRTM_ID = 5,
	/* An array of 6 x unsigned 8-bit value; MCC/MNC combination, 0s if
	 * unspecified.
	 */
	QCA_WLAN_VENDOR_ATTR_PNO_PASSPOINT_NETWORK_PARAM_ROAM_PLMN = 6,

	/* Attributes for data used by
	 * QCA_NL80211_VENDOR_SUBCMD_PNO_SET_LIST sub command.
	 */
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_PNO_SET_LIST_PARAM_NUM_NETWORKS = 7,
	/* Array of nested
	 * QCA_WLAN_VENDOR_ATTR_PNO_SET_LIST_PARAM_EPNO_NETWORK_*
	 * attributes. Array size =
	 * QCA_WLAN_VENDOR_ATTR_PNO_SET_LIST_PARAM_NUM_NETWORKS.
	 */
	QCA_WLAN_VENDOR_ATTR_PNO_SET_LIST_PARAM_EPNO_NETWORKS_LIST = 8,
	/* An array of 33 x unsigned 8-bit value; NULL terminated SSID */
	QCA_WLAN_VENDOR_ATTR_PNO_SET_LIST_PARAM_EPNO_NETWORK_SSID = 9,
	/* Signed 8-bit value; threshold for considering this SSID as found,
	 * required granularity for this threshold is 4 dBm to 8 dBm.
	 */
	QCA_WLAN_VENDOR_ATTR_PNO_SET_LIST_PARAM_EPNO_NETWORK_RSSI_THRESHOLD
	= 10,
	/* Unsigned 8-bit value; WIFI_PNO_FLAG_XXX */
	QCA_WLAN_VENDOR_ATTR_PNO_SET_LIST_PARAM_EPNO_NETWORK_FLAGS = 11,
	/* Unsigned 8-bit value; auth bit field for matching WPA IE */
	QCA_WLAN_VENDOR_ATTR_PNO_SET_LIST_PARAM_EPNO_NETWORK_AUTH_BIT = 12,
	/* Unsigned 8-bit to indicate ePNO type;
	 * It takes values from qca_wlan_epno_type
	 */
	QCA_WLAN_VENDOR_ATTR_PNO_SET_LIST_PARAM_EPNO_TYPE = 13,

	/* Nested attribute to send the channel list */
	QCA_WLAN_VENDOR_ATTR_PNO_SET_LIST_PARAM_EPNO_CHANNEL_LIST = 14,

	/* Unsigned 32-bit value; indicates the interval between PNO scan
	 * cycles in msec.
	 */
	QCA_WLAN_VENDOR_ATTR_PNO_SET_LIST_PARAM_EPNO_SCAN_INTERVAL = 15,
	QCA_WLAN_VENDOR_ATTR_EPNO_MIN5GHZ_RSSI = 16,
	QCA_WLAN_VENDOR_ATTR_EPNO_MIN24GHZ_RSSI = 17,
	QCA_WLAN_VENDOR_ATTR_EPNO_INITIAL_SCORE_MAX = 18,
	QCA_WLAN_VENDOR_ATTR_EPNO_CURRENT_CONNECTION_BONUS = 19,
	QCA_WLAN_VENDOR_ATTR_EPNO_SAME_NETWORK_BONUS = 20,
	QCA_WLAN_VENDOR_ATTR_EPNO_SECURE_BONUS = 21,
	QCA_WLAN_VENDOR_ATTR_EPNO_BAND5GHZ_BONUS = 22,
	/* Unsigned 32-bit value, representing the PNO Request ID */
	QCA_WLAN_VENDOR_ATTR_PNO_CONFIG_REQUEST_ID = 23,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_PNO_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_PNO_MAX =
	QCA_WLAN_VENDOR_ATTR_PNO_AFTER_LAST - 1,
};

/**
 * qca_wlan_vendor_acs_select_reason: This represents the different reasons why
 * the ACS has to be triggered. These values are used by
 * QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_REASON and
 * QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_CHANNEL_REASON
 */
enum qca_wlan_vendor_acs_select_reason {
	/* Represents the reason that the ACS triggered during the AP start */
	QCA_WLAN_VENDOR_ACS_SELECT_REASON_INIT,
	/* Represents the reason that DFS found with the current channel */
	QCA_WLAN_VENDOR_ACS_SELECT_REASON_DFS,
	/* Represents the reason that LTE co-exist in the current band. */
	QCA_WLAN_VENDOR_ACS_SELECT_REASON_LTE_COEX,
};

/**
 * qca_wlan_vendor_attr_external_acs_policy: Attribute values for
 * QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_POLICY to the vendor subcmd
 * QCA_NL80211_VENDOR_SUBCMD_EXTERNAL_ACS. This represents the
 * external ACS policies to select the channels w.r.t. the PCL weights.
 * (QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_PCL represents the channels and
 * their PCL weights.)
 * @QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_POLICY_PCL_MANDATORY: Mandatory to
 * select a channel with non-zero PCL weight.
 * @QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_POLICY_PCL_PREFERRED: Prefer a
 * channel with non-zero PCL weight.
 *
 */
enum qca_wlan_vendor_attr_external_acs_policy {
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_POLICY_PCL_PREFERRED,
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_POLICY_PCL_MANDATORY,
};

/**
 * qca_wlan_vendor_channel_prop_flags: This represent the flags for a channel.
 * This is used by QCA_WLAN_VENDOR_EXTERNAL_ACS_EVENT_CHAN_INFO_ATTR_FLAGS.
 */
enum qca_wlan_vendor_channel_prop_flags {
	/* Bits 0, 1, 2, and 3 are reserved */

	/* Turbo channel */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_TURBO         = 1 << 4,
	/* CCK channel */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_CCK           = 1 << 5,
	/* OFDM channel */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_OFDM          = 1 << 6,
	/* 2.4 GHz spectrum channel. */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_2GHZ          = 1 << 7,
	/* 5 GHz spectrum channel */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_5GHZ          = 1 << 8,
	/* Only passive scan allowed */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_PASSIVE       = 1 << 9,
	/* Dynamic CCK-OFDM channel */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_DYN           = 1 << 10,
	/* GFSK channel (FHSS PHY) */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_GFSK          = 1 << 11,
	/* Radar found on channel */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_RADAR         = 1 << 12,
	/* 11a static turbo channel only */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_STURBO        = 1 << 13,
	/* Half rate channel */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_HALF          = 1 << 14,
	/* Quarter rate channel */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_QUARTER       = 1 << 15,
	/* HT 20 channel */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_HT20          = 1 << 16,
	/* HT 40 with extension channel above */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_HT40PLUS      = 1 << 17,
	/* HT 40 with extension channel below */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_HT40MINUS     = 1 << 18,
	/* HT 40 intolerant */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_HT40INTOL     = 1 << 19,
	/* VHT 20 channel */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_VHT20         = 1 << 20,
	/* VHT 40 with extension channel above */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_VHT40PLUS     = 1 << 21,
	/* VHT 40 with extension channel below */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_VHT40MINUS    = 1 << 22,
	/* VHT 80 channel */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_VHT80         = 1 << 23,
	/* HT 40 intolerant mark bit for ACS use */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_HT40INTOLMARK = 1 << 24,
	/* Channel temporarily blocked due to noise */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_BLOCKED       = 1 << 25,
	/* VHT 160 channel */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_VHT160        = 1 << 26,
	/* VHT 80+80 channel */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_VHT80_80      = 1 << 27,
	/* HE 20 channel */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_HE20          = 1 << 28,
	/* HE 40 with extension channel above */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_HE40PLUS      = 1 << 29,
	/* HE 40 with extension channel below */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_HE40MINUS     = 1 << 30,
	/* HE 40 intolerant */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_HE40INTOL     = 1 << 31,
};

/**
 * qca_wlan_vendor_channel_prop_flags_2: This represents the flags for a
 * channel, and is a continuation of qca_wlan_vendor_channel_prop_flags. This is
 * used by QCA_WLAN_VENDOR_EXTERNAL_ACS_EVENT_CHAN_INFO_ATTR_FLAGS_2.
 */
enum qca_wlan_vendor_channel_prop_flags_2 {
	/* HE 40 intolerant mark bit for ACS use */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_HE40INTOLMARK = 1 << 0,
	/* HE 80 channel */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_HE80          = 1 << 1,
	/* HE 160 channel */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_HE160         = 1 << 2,
	/* HE 80+80 channel */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_HE80_80       = 1 << 3,
};

/**
 * qca_wlan_vendor_channel_prop_flags_ext: This represent the extended flags for
 * each channel. This is used by
 * QCA_WLAN_VENDOR_EXTERNAL_ACS_EVENT_CHAN_INFO_ATTR_FLAG_EXT.
 */
enum qca_wlan_vendor_channel_prop_flags_ext {
	/* Radar found on channel */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_EXT_RADAR_FOUND     = 1 << 0,
	/* DFS required on channel */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_EXT_DFS             = 1 << 1,
	/* DFS required on channel for 2nd band of 80+80 */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_EXT_DFS_CFREQ2      = 1 << 2,
	/* If channel has been checked for DFS */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_EXT_DFS_CLEAR       = 1 << 3,
	/* Excluded in 802.11d */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_EXT_11D_EXCLUDED    = 1 << 4,
	/* Channel Switch Announcement received on this channel */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_EXT_CSA_RECEIVED    = 1 << 5,
	/* Ad-hoc is not allowed */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_EXT_DISALLOW_ADHOC  = 1 << 6,
	/* Station only channel */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_EXT_DISALLOW_HOSTAP = 1 << 7,
	/* DFS radar history for slave device (STA mode) */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_EXT_HISTORY_RADAR   = 1 << 8,
	/* DFS CAC valid for slave device (STA mode) */
	QCA_WLAN_VENDOR_CHANNEL_PROP_FLAG_EXT_CAC_VALID       = 1 << 9,
};

/**
 * qca_wlan_vendor_external_acs_event_chan_info_attr: Represents per channel
 * information. These attributes are sent as part of
 * QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_CHAN_INFO. Each set of the following
 * attributes correspond to a single channel.
 */
enum qca_wlan_vendor_external_acs_event_chan_info_attr {
	QCA_WLAN_VENDOR_EXTERNAL_ACS_EVENT_CHAN_INFO_ATTR_INVALID = 0,

	/* A bitmask (u32) with flags specified in
	 * enum qca_wlan_vendor_channel_prop_flags.
	 */
	QCA_WLAN_VENDOR_EXTERNAL_ACS_EVENT_CHAN_INFO_ATTR_FLAGS = 1,
	/* A bitmask (u32) with flags specified in
	 * enum qca_wlan_vendor_channel_prop_flags_ext.
	 */
	QCA_WLAN_VENDOR_EXTERNAL_ACS_EVENT_CHAN_INFO_ATTR_FLAG_EXT = 2,
	/* frequency in MHz (u32) */
	QCA_WLAN_VENDOR_EXTERNAL_ACS_EVENT_CHAN_INFO_ATTR_FREQ = 3,
	/* maximum regulatory transmission power (u32) */
	QCA_WLAN_VENDOR_EXTERNAL_ACS_EVENT_CHAN_INFO_ATTR_MAX_REG_POWER = 4,
	/* maximum transmission power (u32) */
	QCA_WLAN_VENDOR_EXTERNAL_ACS_EVENT_CHAN_INFO_ATTR_MAX_POWER = 5,
	/* minimum transmission power (u32) */
	QCA_WLAN_VENDOR_EXTERNAL_ACS_EVENT_CHAN_INFO_ATTR_MIN_POWER = 6,
	/* regulatory class id (u8) */
	QCA_WLAN_VENDOR_EXTERNAL_ACS_EVENT_CHAN_INFO_ATTR_REG_CLASS_ID = 7,
	/* maximum antenna gain in (u8) */
	QCA_WLAN_VENDOR_EXTERNAL_ACS_EVENT_CHAN_INFO_ATTR_ANTENNA_GAIN = 8,
	/* VHT segment 0 (u8) */
	QCA_WLAN_VENDOR_EXTERNAL_ACS_EVENT_CHAN_INFO_ATTR_VHT_SEG_0 = 9,
	/* VHT segment 1 (u8) */
	QCA_WLAN_VENDOR_EXTERNAL_ACS_EVENT_CHAN_INFO_ATTR_VHT_SEG_1 = 10,
	/* A bitmask (u32) with flags specified in
	 * enum qca_wlan_vendor_channel_prop_flags_2.
	 */
	QCA_WLAN_VENDOR_EXTERNAL_ACS_EVENT_CHAN_INFO_ATTR_FLAGS_2 = 11,

	/* keep last */
	QCA_WLAN_VENDOR_EXTERNAL_ACS_EVENT_CHAN_INFO_ATTR_LAST,
	QCA_WLAN_VENDOR_EXTERNAL_ACS_EVENT_CHAN_INFO_ATTR_MAX =
		QCA_WLAN_VENDOR_EXTERNAL_ACS_EVENT_CHAN_INFO_ATTR_LAST - 1,
};

/**
 * qca_wlan_vendor_attr_pcl: Represents attributes for
 * preferred channel list (PCL). These attributes are sent as part of
 * QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_PCL and
 * QCA_NL80211_VENDOR_SUBCMD_GET_PREFERRED_FREQ_LIST.
 */
enum qca_wlan_vendor_attr_pcl {
	QCA_WLAN_VENDOR_ATTR_PCL_INVALID = 0,

	/* Channel number (u8) */
	QCA_WLAN_VENDOR_ATTR_PCL_CHANNEL = 1,
	/* Channel weightage (u8) */
	QCA_WLAN_VENDOR_ATTR_PCL_WEIGHT = 2,
	/* Channel frequency (u32) in MHz */
	QCA_WLAN_VENDOR_ATTR_PCL_FREQ = 3,
	/* Channel flags (u32)
	 * bit 0 set: channel to be used for GO role,
	 * bit 1 set: channel to be used on CLI role,
	 * bit 2 set: channel must be considered for operating channel
	 *                 selection & peer chosen operating channel should be
	 *                 one of the channels with this flag set,
	 * bit 3 set: channel should be excluded in GO negotiation
	 */
	QCA_WLAN_VENDOR_ATTR_PCL_FLAG = 4,
};

/**
 * qca_wlan_vendor_attr_external_acs_event: Attribute to vendor sub-command
 * QCA_NL80211_VENDOR_SUBCMD_EXTERNAL_ACS. This attribute will be sent by
 * host driver.
 */
enum qca_wlan_vendor_attr_external_acs_event {
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_INVALID = 0,

	/* This reason (u8) refers to enum qca_wlan_vendor_acs_select_reason.
	 * This helps ACS module to understand why ACS needs to be started.
	 */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_REASON = 1,
	/* Flag attribute to indicate if driver supports spectral scanning */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_IS_SPECTRAL_SUPPORTED = 2,
	/* Flag attribute to indicate if 11ac is offloaded to firmware */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_IS_OFFLOAD_ENABLED = 3,
	/* Flag attribute to indicate if driver provides additional channel
	 * capability as part of scan operation
	 */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_ADD_CHAN_STATS_SUPPORT = 4,
	/* Flag attribute to indicate interface status is UP */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_AP_UP = 5,
	/* Operating mode (u8) of interface. Takes one of enum nl80211_iftype
	 * values.
	 */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_SAP_MODE = 6,
	/* Channel width (u8). It takes one of enum nl80211_chan_width values.
	 * This is the upper bound of channel width. ACS logic should try to get
	 * a channel with the specified width and if not found, look for lower
	 * values.
	 */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_CHAN_WIDTH = 7,
	/* This (u8) will hold values of one of enum nl80211_bands */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_BAND = 8,
	/* PHY/HW mode (u8). Takes one of enum qca_wlan_vendor_acs_hw_mode
	 * values
	 */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_PHY_MODE = 9,
	/* Array of (u32) supported frequency list among which ACS should choose
	 * best frequency.
	 */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_FREQ_LIST = 10,
	/* Preferred channel list by the driver which will have array of nested
	 * values as per enum qca_wlan_vendor_attr_pcl attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_PCL = 11,
	/* Array of nested attribute for each channel. It takes attr as defined
	 * in enum qca_wlan_vendor_external_acs_event_chan_info_attr.
	 */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_CHAN_INFO = 12,
	/* External ACS policy such as PCL mandatory, PCL preferred, etc.
	 * It uses values defined in enum
	 * qca_wlan_vendor_attr_external_acs_policy.
	 */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_POLICY = 13,
	/* Reference RF Operating Parameter (RROP) availability information
	 * (u16). It uses values defined in enum
	 * qca_wlan_vendor_attr_rropavail_info.
	 */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_RROPAVAIL_INFO = 14,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_LAST,
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_MAX =
		QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_EVENT_LAST - 1,
};

/**
 * qca_wlan_vendor_attr_external_acs_channels: Attributes to vendor subcmd
 * QCA_NL80211_VENDOR_SUBCMD_EXTERNAL_ACS. This carries a list of channels
 * in priority order as decided after ACS operation in userspace.
 */
enum qca_wlan_vendor_attr_external_acs_channels {
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_CHANNEL_INVALID = 0,

	/* One of reason code (u8) from enum qca_wlan_vendor_acs_select_reason
	 */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_CHANNEL_REASON = 1,

	/* Array of nested values for each channel with following attributes:
	 * QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_CHANNEL_BAND,
	 * QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_CHANNEL_PRIMARY,
	 * QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_CHANNEL_SECONDARY,
	 * QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_CHANNEL_CENTER_SEG0,
	 * QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_CHANNEL_CENTER_SEG1,
	 * QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_CHANNEL_WIDTH
	 */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_CHANNEL_LIST = 2,
	/* This (u8) will hold values of one of enum nl80211_bands */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_CHANNEL_BAND = 3,
	/* Primary channel (u8) */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_CHANNEL_PRIMARY = 4,
	/* Secondary channel (u8) used for HT 40 MHz channels */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_CHANNEL_SECONDARY = 5,
	/* VHT seg0 channel (u8) */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_CHANNEL_CENTER_SEG0 = 6,
	/* VHT seg1 channel (u8) */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_CHANNEL_CENTER_SEG1 = 7,
	/* Channel width (u8). Takes one of enum nl80211_chan_width values. */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_CHANNEL_WIDTH = 8,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_CHANNEL_LAST,
	QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_CHANNEL_MAX =
		QCA_WLAN_VENDOR_ATTR_EXTERNAL_ACS_CHANNEL_LAST - 1
};

enum qca_chip_power_save_failure_reason {
	/* Indicates if the reason for the failure is due to a protocol
	 * layer/module.
	 */
	QCA_CHIP_POWER_SAVE_FAILURE_REASON_PROTOCOL = 0,
	/* Indicates if the reason for the failure is due to a hardware issue.
	 */
	QCA_CHIP_POWER_SAVE_FAILURE_REASON_HARDWARE = 1,
};

/**
 * qca_attr_chip_power_save_failure: Attributes to vendor subcmd
 * QCA_NL80211_VENDOR_SUBCMD_CHIP_PWRSAVE_FAILURE. This carries the requisite
 * information leading to the power save failure.
 */
enum qca_attr_chip_power_save_failure {
	QCA_ATTR_CHIP_POWER_SAVE_FAILURE_INVALID = 0,
	/* Reason to cause the power save failure.
	 * These reasons are represented by
	 * enum qca_chip_power_save_failure_reason.
	 */
	QCA_ATTR_CHIP_POWER_SAVE_FAILURE_REASON = 1,

	/* keep last */
	QCA_ATTR_CHIP_POWER_SAVE_FAILURE_LAST,
	QCA_ATTR_CHIP_POWER_SAVE_FAILURE_MAX =
		QCA_ATTR_CHIP_POWER_SAVE_FAILURE_LAST - 1,
};

/**
 * qca_wlan_vendor_nud_stats_data_pkt_flags: Flag representing the various
 * data types for which the stats have to get collected.
 */
enum qca_wlan_vendor_nud_stats_data_pkt_flags {
	QCA_WLAN_VENDOR_NUD_STATS_DATA_ARP = 1 << 0,
	QCA_WLAN_VENDOR_NUD_STATS_DATA_DNS = 1 << 1,
	QCA_WLAN_VENDOR_NUD_STATS_DATA_TCP_HANDSHAKE = 1 << 2,
	QCA_WLAN_VENDOR_NUD_STATS_DATA_ICMPV4 = 1 << 3,
	QCA_WLAN_VENDOR_NUD_STATS_DATA_ICMPV6 = 1 << 4,
	/* Used by QCA_ATTR_NUD_STATS_PKT_TYPE only in nud stats get
	 * to represent the stats of respective data type.
	 */
	QCA_WLAN_VENDOR_NUD_STATS_DATA_TCP_SYN = 1 << 5,
	QCA_WLAN_VENDOR_NUD_STATS_DATA_TCP_SYN_ACK = 1 << 6,
	QCA_WLAN_VENDOR_NUD_STATS_DATA_TCP_ACK = 1 << 7,
};

enum qca_wlan_vendor_nud_stats_set_data_pkt_info {
	QCA_ATTR_NUD_STATS_DATA_PKT_INFO_INVALID = 0,
	/* Represents the data packet type to be monitored (u32).
	 * Host driver tracks the stats corresponding to each data frame
	 * represented by these flags.
	 * These data packets are represented by
	 * enum qca_wlan_vendor_nud_stats_data_pkt_flags
	 */
	QCA_ATTR_NUD_STATS_DATA_PKT_INFO_TYPE = 1,
	/* Name corresponding to the DNS frame for which the respective DNS
	 * stats have to get monitored (string). Max string length 255.
	 */
	QCA_ATTR_NUD_STATS_DATA_PKT_INFO_DNS_DOMAIN_NAME = 2,
	/* source port on which the respective proto stats have to get
	 * collected (u32).
	 */
	QCA_ATTR_NUD_STATS_DATA_PKT_INFO_SRC_PORT = 3,
	/* destination port on which the respective proto stats have to get
	 * collected (u32).
	 */
	QCA_ATTR_NUD_STATS_DATA_PKT_INFO_DEST_PORT = 4,
	/* IPv4 address for which the destined data packets have to be
	 * monitored. (in network byte order), u32.
	 */
	QCA_ATTR_NUD_STATS_DATA_PKT_INFO_DEST_IPV4 = 5,
	/* IPv6 address for which the destined data packets have to be
	 * monitored. (in network byte order), 16 bytes array.
	 */
	QCA_ATTR_NUD_STATS_DATA_PKT_INFO_DEST_IPV6 = 6,

	QCA_ATTR_NUD_STATS_DATA_PKT_INFO_LAST,
	QCA_ATTR_NUD_STATS_DATA_PKT_INFO_MAX =
		QCA_ATTR_NUD_STATS_DATA_PKT_INFO_LAST - 1,
};

/**
 * qca_wlan_vendor_attr_nud_stats_set: Attributes to vendor subcmd
 * QCA_NL80211_VENDOR_SUBCMD_NUD_STATS_SET. This carries the requisite
 * information to start/stop the NUD statistics collection.
 */
enum qca_attr_nud_stats_set {
	QCA_ATTR_NUD_STATS_SET_INVALID = 0,

	/* Flag to start/stop the NUD statistics collection.
	 * Start - If included, Stop - If not included
	 */
	QCA_ATTR_NUD_STATS_SET_START = 1,
	/* IPv4 address of the default gateway (in network byte order), u32 */
	QCA_ATTR_NUD_STATS_GW_IPV4 = 2,
	/* Represents the list of data packet types to be monitored.
	 * Host driver tracks the stats corresponding to each data frame
	 * represented by these flags.
	 * These data packets are represented by
	 * enum qca_wlan_vendor_nud_stats_set_data_pkt_info
	 */
	QCA_ATTR_NUD_STATS_SET_DATA_PKT_INFO = 3,

	/* keep last */
	QCA_ATTR_NUD_STATS_SET_LAST,
	QCA_ATTR_NUD_STATS_SET_MAX =
		QCA_ATTR_NUD_STATS_SET_LAST - 1,
};

enum qca_attr_nud_data_stats {
	QCA_ATTR_NUD_DATA_STATS_INVALID = 0,
	/* Data packet type for which the stats are collected (u32).
	 * Represented by enum qca_wlan_vendor_nud_stats_data_pkt_flags
	 */
	QCA_ATTR_NUD_STATS_PKT_TYPE = 1,
	/* Name corresponding to the DNS frame for which the respective DNS
	 * stats are monitored (string). Max string length 255.
	 */
	QCA_ATTR_NUD_STATS_PKT_DNS_DOMAIN_NAME = 2,
	/* source port on which the respective proto stats are collected (u32).
	 */
	QCA_ATTR_NUD_STATS_PKT_SRC_PORT = 3,
	/* destination port on which the respective proto stats are collected
	 * (u32).
	 */
	QCA_ATTR_NUD_STATS_PKT_DEST_PORT = 4,
	/* IPv4 address for which the destined data packets have to be
	 * monitored. (in network byte order), u32.
	 */
	QCA_ATTR_NUD_STATS_PKT_DEST_IPV4 = 5,
	/* IPv6 address for which the destined data packets have to be
	 * monitored. (in network byte order), 16 bytes array.
	 */
	QCA_ATTR_NUD_STATS_PKT_DEST_IPV6 = 6,
	/* Data packet Request count received from netdev (u32). */
	QCA_ATTR_NUD_STATS_PKT_REQ_COUNT_FROM_NETDEV = 7,
	/* Data packet Request count sent to lower MAC from upper MAC (u32). */
	QCA_ATTR_NUD_STATS_PKT_REQ_COUNT_TO_LOWER_MAC = 8,
	/* Data packet Request count received by lower MAC from upper MAC
	 * (u32)
	 */
	QCA_ATTR_NUD_STATS_PKT_REQ_RX_COUNT_BY_LOWER_MAC = 9,
	/* Data packet Request count successfully transmitted by the device
	 * (u32)
	 */
	QCA_ATTR_NUD_STATS_PKT_REQ_COUNT_TX_SUCCESS = 10,
	/* Data packet Response count received by lower MAC (u32) */
	QCA_ATTR_NUD_STATS_PKT_RSP_RX_COUNT_BY_LOWER_MAC = 11,
	/* Data packet Response count received by upper MAC (u32) */
	QCA_ATTR_NUD_STATS_PKT_RSP_RX_COUNT_BY_UPPER_MAC = 12,
	/* Data packet Response count delivered to netdev (u32) */
	QCA_ATTR_NUD_STATS_PKT_RSP_COUNT_TO_NETDEV = 13,
	/* Data Packet Response count that are dropped out of order (u32) */
	QCA_ATTR_NUD_STATS_PKT_RSP_COUNT_OUT_OF_ORDER_DROP = 14,

	/* keep last */
	QCA_ATTR_NUD_DATA_STATS_LAST,
	QCA_ATTR_NUD_DATA_STATS_MAX =
		QCA_ATTR_NUD_DATA_STATS_LAST - 1,
};

/**
 * qca_attr_nud_stats_get: Attributes to vendor subcmd
 * QCA_NL80211_VENDOR_SUBCMD_NUD_STATS_GET. This carries the requisite
 * NUD statistics collected when queried.
 */
enum qca_attr_nud_stats_get {
	QCA_ATTR_NUD_STATS_GET_INVALID = 0,
	/* ARP Request count from netdev (u32) */
	QCA_ATTR_NUD_STATS_ARP_REQ_COUNT_FROM_NETDEV = 1,
	/* ARP Request count sent to lower MAC from upper MAC (u32) */
	QCA_ATTR_NUD_STATS_ARP_REQ_COUNT_TO_LOWER_MAC = 2,
	/* ARP Request count received by lower MAC from upper MAC (u32) */
	QCA_ATTR_NUD_STATS_ARP_REQ_RX_COUNT_BY_LOWER_MAC = 3,
	/* ARP Request count successfully transmitted by the device (u32) */
	QCA_ATTR_NUD_STATS_ARP_REQ_COUNT_TX_SUCCESS = 4,
	/* ARP Response count received by lower MAC (u32) */
	QCA_ATTR_NUD_STATS_ARP_RSP_RX_COUNT_BY_LOWER_MAC = 5,
	/* ARP Response count received by upper MAC (u32) */
	QCA_ATTR_NUD_STATS_ARP_RSP_RX_COUNT_BY_UPPER_MAC = 6,
	/* ARP Response count delivered to netdev (u32) */
	QCA_ATTR_NUD_STATS_ARP_RSP_COUNT_TO_NETDEV = 7,
	/* ARP Response count dropped due to out of order reception (u32) */
	QCA_ATTR_NUD_STATS_ARP_RSP_COUNT_OUT_OF_ORDER_DROP = 8,
	/* Flag indicating if the station's link to the AP is active.
	 * Active Link - If included, Inactive link - If not included
	 */
	QCA_ATTR_NUD_STATS_AP_LINK_ACTIVE = 9,
	/* Flag indicating if there is any duplicate address detected (DAD).
	 * Yes - If detected, No - If not detected.
	 */
	QCA_ATTR_NUD_STATS_IS_DAD = 10,
	/* List of Data packet types for which the stats are requested.
	 * This list does not carry ARP stats as they are done by the
	 * above attributes. Represented by enum qca_attr_nud_data_stats.
	 */
	QCA_ATTR_NUD_STATS_DATA_PKT_STATS = 11,

	/* keep last */
	QCA_ATTR_NUD_STATS_GET_LAST,
	QCA_ATTR_NUD_STATS_GET_MAX =
		QCA_ATTR_NUD_STATS_GET_LAST - 1,
};

enum qca_wlan_btm_candidate_status {
	QCA_STATUS_ACCEPT = 0,
	QCA_STATUS_REJECT_EXCESSIVE_FRAME_LOSS_EXPECTED = 1,
	QCA_STATUS_REJECT_EXCESSIVE_DELAY_EXPECTED = 2,
	QCA_STATUS_REJECT_INSUFFICIENT_QOS_CAPACITY = 3,
	QCA_STATUS_REJECT_LOW_RSSI = 4,
	QCA_STATUS_REJECT_HIGH_INTERFERENCE = 5,
	QCA_STATUS_REJECT_UNKNOWN = 6,
};

enum qca_wlan_vendor_attr_btm_candidate_info {
	QCA_WLAN_VENDOR_ATTR_BTM_CANDIDATE_INFO_INVALID = 0,

	/* 6-byte MAC address representing the BSSID of transition candidate */
	QCA_WLAN_VENDOR_ATTR_BTM_CANDIDATE_INFO_BSSID = 1,
	/* Unsigned 32-bit value from enum qca_wlan_btm_candidate_status
	 * returned by the driver. It says whether the BSSID provided in
	 * QCA_WLAN_VENDOR_ATTR_BTM_CANDIDATE_INFO_BSSID is acceptable by
	 * the driver, if not it specifies the reason for rejection.
	 * Note that the user-space can overwrite the transition reject reason
	 * codes provided by driver based on more information.
	 */
	QCA_WLAN_VENDOR_ATTR_BTM_CANDIDATE_INFO_STATUS = 2,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_BTM_CANDIDATE_INFO_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_BTM_CANDIDATE_INFO_MAX =
	QCA_WLAN_VENDOR_ATTR_BTM_CANDIDATE_INFO_AFTER_LAST - 1,
};

enum qca_attr_trace_level {
	QCA_ATTR_TRACE_LEVEL_INVALID = 0,
	/*
	 * Nested array of the following attributes:
	 * QCA_ATTR_TRACE_LEVEL_MODULE,
	 * QCA_ATTR_TRACE_LEVEL_MASK.
	 */
	QCA_ATTR_TRACE_LEVEL_PARAM = 1,
	/*
	 * Specific QCA host driver module. Please refer to the QCA host
	 * driver implementation to get the specific module ID.
	 */
	QCA_ATTR_TRACE_LEVEL_MODULE = 2,
	/* Different trace level masks represented in the QCA host driver. */
	QCA_ATTR_TRACE_LEVEL_MASK = 3,

	/* keep last */
	QCA_ATTR_TRACE_LEVEL_AFTER_LAST,
	QCA_ATTR_TRACE_LEVEL_MAX =
		QCA_ATTR_TRACE_LEVEL_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_get_he_capabilities - IEEE 802.11ax HE capabilities
 */
enum qca_wlan_vendor_attr_get_he_capabilities {
	QCA_WLAN_VENDOR_ATTR_HE_CAPABILITIES_INVALID = 0,
	/* Whether HE capabilities is supported
	 * (u8 attribute: 0 = not supported, 1 = supported)
	 */
	QCA_WLAN_VENDOR_ATTR_HE_SUPPORTED = 1,
	/* HE PHY capabilities, array of 3 u32 values  */
	QCA_WLAN_VENDOR_ATTR_PHY_CAPAB = 2,
	/* HE MAC capabilities (u32 attribute) */
	QCA_WLAN_VENDOR_ATTR_MAC_CAPAB = 3,
	/* HE MCS map (u32 attribute) */
	QCA_WLAN_VENDOR_ATTR_HE_MCS = 4,
	/* Number of SS (u32 attribute) */
	QCA_WLAN_VENDOR_ATTR_NUM_SS = 5,
	/* RU count (u32 attribute) */
	QCA_WLAN_VENDOR_ATTR_RU_IDX_MASK = 6,
	/* PPE threshold data, array of 8 u32 values */
	QCA_WLAN_VENDOR_ATTR_PPE_THRESHOLD = 7,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_HE_CAPABILITIES_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_HE_CAPABILITIES_MAX =
	QCA_WLAN_VENDOR_ATTR_HE_CAPABILITIES_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_spectral_scan - Spectral scan config parameters
 */
enum qca_wlan_vendor_attr_spectral_scan {
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_INVALID = 0,
	/* Number of times the chip enters spectral scan mode before
	 * deactivating spectral scans. When set to 0, chip will enter spectral
	 * scan mode continuously. u32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_SCAN_COUNT = 1,
	/* Spectral scan period. Period increment resolution is 256*Tclk,
	 * where Tclk = 1/44 MHz (Gmode), 1/40 MHz (Amode). u32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_SCAN_PERIOD = 2,
	/* Spectral scan priority. u32 attribute. */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_PRIORITY = 3,
	/* Number of FFT data points to compute. u32 attribute. */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_FFT_SIZE = 4,
	/* Enable targeted gain change before starting the spectral scan FFT.
	 * u32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_GC_ENA = 5,
	/* Restart a queued spectral scan. u32 attribute. */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_RESTART_ENA = 6,
	/* Noise floor reference number for the calculation of bin power.
	 * u32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_NOISE_FLOOR_REF = 7,
	/* Disallow spectral scan triggers after TX/RX packets by setting
	 * this delay value to roughly SIFS time period or greater.
	 * u32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_INIT_DELAY = 8,
	/* Number of strong bins (inclusive) per sub-channel, below
	 * which a signal is declared a narrow band tone. u32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_NB_TONE_THR = 9,
	/* Specify the threshold over which a bin is declared strong (for
	 * scan bandwidth analysis). u32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_STR_BIN_THR = 10,
	/* Spectral scan report mode. u32 attribute. */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_WB_RPT_MODE = 11,
	/* RSSI report mode, if the ADC RSSI is below
	 * QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_RSSI_THR,
	 * then FFTs will not trigger, but timestamps and summaries get
	 * reported. u32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_RSSI_RPT_MODE = 12,
	/* ADC RSSI must be greater than or equal to this threshold (signed dB)
	 * to ensure spectral scan reporting with normal error code.
	 * u32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_RSSI_THR = 13,
	/* Format of frequency bin magnitude for spectral scan triggered FFTs:
	 * 0: linear magnitude, 1: log magnitude (20*log10(lin_mag)).
	 * u32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_PWR_FORMAT = 14,
	/* Format of FFT report to software for spectral scan triggered FFTs.
	 * 0: No FFT report (only spectral scan summary report)
	 * 1: 2-dword summary of metrics for each completed FFT + spectral scan
	 * report
	 * 2: 2-dword summary of metrics for each completed FFT + 1x-oversampled
	 * bins (in-band) per FFT + spectral scan summary report
	 * 3: 2-dword summary of metrics for each completed FFT + 2x-oversampled
	 * bins (all) per FFT + spectral scan summary report
	 * u32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_RPT_MODE = 15,
	/* Number of LSBs to shift out in order to scale the FFT bins.
	 * u32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_BIN_SCALE = 16,
	/* Set to 1 (with spectral_scan_pwr_format=1), to report bin magnitudes
	 * in dBm power. u32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_DBM_ADJ = 17,
	/* Per chain enable mask to select input ADC for search FFT.
	 * u32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_CHN_MASK = 18,
	/* An unsigned 64-bit integer provided by host driver to identify the
	 * spectral scan request. This attribute is included in the scan
	 * response message for @QCA_NL80211_VENDOR_SUBCMD_SPECTRAL_SCAN_START
	 * and used as an attribute in
	 * @QCA_NL80211_VENDOR_SUBCMD_SPECTRAL_SCAN_STOP to identify the
	 * specific scan to be stopped.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_COOKIE = 19,
	/* Skip interval for FFT reports. u32 attribute */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_FFT_PERIOD = 20,
	/* Set to report only one set of FFT results.
	 * u32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_SHORT_REPORT = 21,
	/* Debug level for spectral module in driver.
	 * 0 : Verbosity level 0
	 * 1 : Verbosity level 1
	 * 2 : Verbosity level 2
	 * 3 : Matched filterID display
	 * 4 : One time dump of FFT report
	 * u32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_DEBUG_LEVEL = 22,
	/* Type of spectral scan request. u32 attribute.
	 * It uses values defined in enum
	 * qca_wlan_vendor_attr_spectral_scan_request_type.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_REQUEST_TYPE = 23,

	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_MAX =
		QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CONFIG_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_spectral_diag_stats - Used by the vendor command
 * QCA_NL80211_VENDOR_SUBCMD_SPECTRAL_SCAN_GET_DIAG_STATS.
 */
enum qca_wlan_vendor_attr_spectral_diag_stats {
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_DIAG_INVALID = 0,
	/* Number of spectral TLV signature mismatches.
	 * u64 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_DIAG_SIG_MISMATCH = 1,
	/* Number of spectral phyerror events with insufficient length when
	 * parsing for secondary 80 search FFT report. u64 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_DIAG_SEC80_SFFT_INSUFFLEN = 2,
	/* Number of spectral phyerror events without secondary 80
	 * search FFT report. u64 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_DIAG_NOSEC80_SFFT = 3,
	/* Number of spectral phyerror events with vht operation segment 1 id
	 * mismatches in search fft report. u64 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_DIAG_VHTSEG1ID_MISMATCH = 4,
	/* Number of spectral phyerror events with vht operation segment 2 id
	 * mismatches in search fft report. u64 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_DIAG_VHTSEG2ID_MISMATCH = 5,

	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_DIAG_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_DIAG_MAX =
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_DIAG_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_spectral_cap - Used by the vendor command
 * QCA_NL80211_VENDOR_SUBCMD_SPECTRAL_SCAN_GET_CAP_INFO.
 */
enum qca_wlan_vendor_attr_spectral_cap {
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CAP_INVALID = 0,
	/* Flag attribute to indicate phydiag capability */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CAP_PHYDIAG = 1,
	/* Flag attribute to indicate radar detection capability */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CAP_RADAR = 2,
	/* Flag attribute to indicate spectral capability */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CAP_SPECTRAL = 3,
	/* Flag attribute to indicate advanced spectral capability */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CAP_ADVANCED_SPECTRAL = 4,
	/* Spectral hardware generation. u32 attribute.
	 * It uses values defined in enum
	 * qca_wlan_vendor_spectral_scan_cap_hw_gen.
	 */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CAP_HW_GEN = 5,

	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CAP_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CAP_MAX =
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CAP_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_spectral_scan_status - used by the vendor command
 * QCA_NL80211_VENDOR_SUBCMD_SPECTRAL_SCAN_GET_STATUS.
 */
enum qca_wlan_vendor_attr_spectral_scan_status {
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_STATUS_INVALID = 0,
	/* Flag attribute to indicate whether spectral scan is enabled */
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_STATUS_IS_ENABLED = 1,
	/* Flag attribute to indicate whether spectral scan is in progress*/
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_STATUS_IS_ACTIVE = 2,

	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_STATUS_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_STATUS_MAX =
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_STATUS_AFTER_LAST - 1,
};

/**
 * qca_wlan_vendor_attr_spectral_scan_request_type: Attribute values for
 * QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_REQUEST_TYPE to the vendor subcmd
 * QCA_NL80211_VENDOR_SUBCMD_SPECTRAL_SCAN_START. This represents the
 * spectral scan request types.
 * @QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_REQUEST_TYPE_SCAN_AND_CONFIG: Request to
 * set the spectral parameters and start scan.
 * @QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_REQUEST_TYPE_SCAN: Request to
 * only set the spectral parameters.
 * @QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_REQUEST_TYPE_CONFIG: Request to
 * only start the spectral scan.
 */
enum qca_wlan_vendor_attr_spectral_scan_request_type {
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_REQUEST_TYPE_SCAN_AND_CONFIG,
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_REQUEST_TYPE_SCAN,
	QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_REQUEST_TYPE_CONFIG,
};

/**
 * qca_wlan_vendor_spectral_scan_cap_hw_gen: Attribute values for
 * QCA_WLAN_VENDOR_ATTR_SPECTRAL_SCAN_CAP_HW_GEN to the vendor subcmd
 * QCA_NL80211_VENDOR_SUBCMD_SPECTRAL_SCAN_GET_CAP_INFO. This represents the
 * spectral hardware generation.
 * @QCA_WLAN_VENDOR_SPECTRAL_SCAN_CAP_HW_GEN_1: generation 1
 * @QCA_WLAN_VENDOR_SPECTRAL_SCAN_CAP_HW_GEN_2: generation 2
 * @QCA_WLAN_VENDOR_SPECTRAL_SCAN_CAP_HW_GEN_3: generation 3
 */
enum qca_wlan_vendor_spectral_scan_cap_hw_gen {
	QCA_WLAN_VENDOR_SPECTRAL_SCAN_CAP_HW_GEN_1 = 0,
	QCA_WLAN_VENDOR_SPECTRAL_SCAN_CAP_HW_GEN_2 = 1,
	QCA_WLAN_VENDOR_SPECTRAL_SCAN_CAP_HW_GEN_3 = 2,
};

enum qca_wlan_vendor_tos {
	QCA_WLAN_VENDOR_TOS_BK = 0,
	QCA_WLAN_VENDOR_TOS_BE = 1,
	QCA_WLAN_VENDOR_TOS_VI = 2,
	QCA_WLAN_VENDOR_TOS_VO = 3,
};

/**
 * enum qca_wlan_vendor_attr_active_tos - Used by the vendor command
 * QCA_NL80211_VENDOR_SUBCMD_ACTIVE_TOS.
 */
enum qca_wlan_vendor_attr_active_tos {
	QCA_WLAN_VENDOR_ATTR_ACTIVE_TOS_INVALID = 0,
	/* Type Of Service - Represented by qca_wlan_vendor_tos */
	QCA_WLAN_VENDOR_ATTR_ACTIVE_TOS = 1,
	/* Flag attribute representing the start (attribute included) or stop
	 * (attribute not included) of the respective TOS.
	 */
	QCA_WLAN_VENDOR_ATTR_ACTIVE_TOS_START = 2,
};

enum qca_wlan_vendor_hang_reason {
	/* Unspecified reason */
	QCA_WLAN_HANG_REASON_UNSPECIFIED = 0,
	/* No Map for the MAC entry for the received frame */
	QCA_WLAN_HANG_RX_HASH_NO_ENTRY_FOUND = 1,
	/* Peer deletion timeout happened */
	QCA_WLAN_HANG_PEER_DELETION_TIMEDOUT = 2,
	/* Peer unmap timeout */
	QCA_WLAN_HANG_PEER_UNMAP_TIMEDOUT = 3,
	/* Scan request timed out */
	QCA_WLAN_HANG_SCAN_REQ_EXPIRED = 4,
	/* Consecutive Scan attempt failures */
	QCA_WLAN_HANG_SCAN_ATTEMPT_FAILURES = 5,
	/* Unable to get the message buffer */
	QCA_WLAN_HANG_GET_MSG_BUFF_FAILURE = 6,
	/* Current command processing is timedout */
	QCA_WLAN_HANG_ACTIVE_LIST_TIMEOUT = 7,
	/* Timeout for an ACK from FW for suspend request */
	QCA_WLAN_HANG_SUSPEND_TIMEOUT = 8,
	/* Timeout for an ACK from FW for resume request */
	QCA_WLAN_HANG_RESUME_TIMEOUT = 9,
	/* Transmission timeout for consecutive data frames */
	QCA_WLAN_HANG_TRANSMISSIONS_TIMEOUT = 10,
	/* Timeout for the TX completion status of data frame */
	QCA_WLAN_HANG_TX_COMPLETE_TIMEOUT = 11,
	/* DXE failure for TX/RX, DXE resource unavailability */
	QCA_WLAN_HANG_DXE_FAILURE = 12,
	/* WMI pending commands exceed the maximum count */
	QCA_WLAN_HANG_WMI_EXCEED_MAX_PENDING_CMDS = 13,
};

/**
 * enum qca_wlan_vendor_attr_hang - Used by the vendor command
 * QCA_NL80211_VENDOR_SUBCMD_HANG.
 */
enum qca_wlan_vendor_attr_hang {
	QCA_WLAN_VENDOR_ATTR_HANG_INVALID = 0,
	/* Reason for the hang - u32 attribute with a value from enum
	 * qca_wlan_vendor_hang_reason.
	 */
	QCA_WLAN_VENDOR_ATTR_HANG_REASON = 1,

	QCA_WLAN_VENDOR_ATTR_HANG_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_HANG_MAX =
		QCA_WLAN_VENDOR_ATTR_HANG_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_flush_pending - Attributes for
 * flushing pending traffic in firmware.
 *
 * @QCA_WLAN_VENDOR_ATTR_PEER_ADDR: Configure peer MAC address.
 * @QCA_WLAN_VENDOR_ATTR_AC: Configure access category of the pending
 * packets. It is u8 value with bit 0~3 represent AC_BE, AC_BK,
 * AC_VI, AC_VO respectively. Set the corresponding bit to 1 to
 * flush packets with access category.
 */
enum qca_wlan_vendor_attr_flush_pending {
	QCA_WLAN_VENDOR_ATTR_FLUSH_PENDING_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_PEER_ADDR = 1,
	QCA_WLAN_VENDOR_ATTR_AC = 2,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_FLUSH_PENDING_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_FLUSH_PENDING_MAX =
	QCA_WLAN_VENDOR_ATTR_FLUSH_PENDING_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_rropavail_info - Specifies whether Representative
 * RF Operating Parameter (RROP) information is available, and if so, at which
 * point in the application-driver interaction sequence it can be retrieved by
 * the application from the driver. This point may vary by architecture and
 * other factors. This is a u16 value.
 */
enum qca_wlan_vendor_attr_rropavail_info {
	/* RROP information is unavailable. */
	QCA_WLAN_VENDOR_ATTR_RROPAVAIL_INFO_UNAVAILABLE,
	/* RROP information is available and the application can retrieve the
	 * information after receiving an QCA_NL80211_VENDOR_SUBCMD_EXTERNAL_ACS
	 * event from the driver.
	 */
	QCA_WLAN_VENDOR_ATTR_RROPAVAIL_INFO_EXTERNAL_ACS_START,
	/* RROP information is available only after a vendor specific scan
	 * (requested using QCA_NL80211_VENDOR_SUBCMD_TRIGGER_SCAN) has
	 * successfully completed. The application can retrieve the information
	 * after receiving the QCA_NL80211_VENDOR_SUBCMD_SCAN_DONE event from
	 * the driver.
	 */
	QCA_WLAN_VENDOR_ATTR_RROPAVAIL_INFO_VSCAN_END,
};

/**
 * enum qca_wlan_vendor_attr_rrop_info - Specifies vendor specific
 * Representative RF Operating Parameter (RROP) information. It is sent for the
 * vendor command QCA_NL80211_VENDOR_SUBCMD_GET_RROP_INFO. This information is
 * intended for use by external Auto Channel Selection applications. It provides
 * guidance values for some RF parameters that are used by the system during
 * operation. These values could vary by channel, band, radio, and so on.
 */
enum qca_wlan_vendor_attr_rrop_info {
	QCA_WLAN_VENDOR_ATTR_RROP_INFO_INVALID = 0,

	/* Representative Tx Power List (RTPL) which has an array of nested
	 * values as per attributes in enum qca_wlan_vendor_attr_rtplinst.
	 */
	QCA_WLAN_VENDOR_ATTR_RROP_INFO_RTPL = 1,

	QCA_WLAN_VENDOR_ATTR_RROP_INFO_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_RROP_INFO_MAX =
	QCA_WLAN_VENDOR_ATTR_RROP_INFO_AFTER_LAST - 1
};

/**
 * enum qca_wlan_vendor_attr_rtplinst - Specifies attributes for individual list
 * entry instances in the Representative Tx Power List (RTPL). It provides
 * simplified power values intended for helping external Auto channel Selection
 * applications compare potential Tx power performance between channels, other
 * operating conditions remaining identical. These values are not necessarily
 * the actual Tx power values that will be used by the system. They are also not
 * necessarily the max or average values that will be used. Instead, they are
 * relative, summarized keys for algorithmic use computed by the driver or
 * underlying firmware considering a number of vendor specific factors.
 */
enum qca_wlan_vendor_attr_rtplinst {
	QCA_WLAN_VENDOR_ATTR_RTPLINST_INVALID = 0,

	/* Primary channel number (u8) */
	QCA_WLAN_VENDOR_ATTR_RTPLINST_PRIMARY = 1,
	/* Representative Tx power in dBm (s32) with emphasis on throughput. */
	QCA_WLAN_VENDOR_ATTR_RTPLINST_TXPOWER_THROUGHPUT = 2,
	/* Representative Tx power in dBm (s32) with emphasis on range. */
	QCA_WLAN_VENDOR_ATTR_RTPLINST_TXPOWER_RANGE = 3,

	QCA_WLAN_VENDOR_ATTR_RTPLINST_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_RTPLINST_MAX =
		QCA_WLAN_VENDOR_ATTR_RTPLINST_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_config_latency_level - Level for
 * wlan latency module.
 *
 * There will be various of Wi-Fi functionality like scan/roaming/adaptive
 * power saving which would causing data exchange out of service, this
 * would be a big impact on latency. For latency sensitive applications over
 * Wi-Fi are intolerant to such operations and thus would configure them
 * to meet their respective needs. It is well understood by such applications
 * that altering the default behavior would degrade the Wi-Fi functionality
 * w.r.t the above pointed WLAN operations.
 *
 * @QCA_WLAN_VENDOR_ATTR_CONFIG_LATENCY_LEVEL_NORMAL:
 *	Default WLAN operation level which throughput orientated.
 * @QCA_WLAN_VENDOR_ATTR_CONFIG_LATENCY_LEVEL_MODERATE:
 *	Use moderate level to improve latency by limit scan duration.
 * @QCA_WLAN_VENDOR_ATTR_CONFIG_LATENCY_LEVEL_LOW:
 *	Use low latency level to benifit application like concurrent
 *	downloading or video streaming via constraint scan/adaptive PS.
 * @QCA_WLAN_VENDOR_ATTR_CONFIG_LATENCY_LEVEL_ULTRALOW:
 *	Use ultra low latency level to benefit for gaming/voice
 *	application via constraint scan/roaming/adaptive PS.
 */
enum qca_wlan_vendor_attr_config_latency_level {
	QCA_WLAN_VENDOR_ATTR_CONFIG_LATENCY_LEVEL_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_CONFIG_LATENCY_LEVEL_NORMAL = 1,
	QCA_WLAN_VENDOR_ATTR_CONFIG_LATENCY_LEVEL_MODERATE = 2,
	QCA_WLAN_VENDOR_ATTR_CONFIG_LATENCY_LEVEL_LOW = 3,
	QCA_WLAN_VENDOR_ATTR_CONFIG_LATENCY_LEVEL_ULTRALOW = 4,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_CONFIG_LATENCY_LEVEL_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_CONFIG_LATENCY_LEVEL_MAX =
	QCA_WLAN_VENDOR_ATTR_CONFIG_LATENCY_LEVEL_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_wlan_mac - Used by the vendor command
 * QCA_NL80211_VENDOR_SUBCMD_WLAN_MAC_INFO.
 */
enum qca_wlan_vendor_attr_mac {
	QCA_WLAN_VENDOR_ATTR_MAC_INVALID = 0,

	/* MAC mode info list which has an array of nested values as
	 * per attributes in enum qca_wlan_vendor_attr_mac_mode_info.
	 */
	QCA_WLAN_VENDOR_ATTR_MAC_INFO = 1,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_MAC_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_MAC_MAX =
	QCA_WLAN_VENDOR_ATTR_MAC_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_mac_iface_info - Information of the connected
 *	Wi-Fi netdev interface on a respective MAC.
 *	Used by the attribute QCA_WLAN_VENDOR_ATTR_MAC_IFACE_INFO.
 */
enum qca_wlan_vendor_attr_mac_iface_info {
	QCA_WLAN_VENDOR_ATTR_MAC_IFACE_INFO_INVALID = 0,
	/* Wi-Fi netdev's interface index (u32) */
	QCA_WLAN_VENDOR_ATTR_MAC_IFACE_INFO_IFINDEX = 1,
	/* Associated frequency in MHz of the connected Wi-Fi interface (u32) */
	QCA_WLAN_VENDOR_ATTR_MAC_IFACE_INFO_FREQ = 2,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_MAC_IFACE_INFO_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_MAC_IFACE_INFO_MAX =
	QCA_WLAN_VENDOR_ATTR_MAC_IFACE_INFO_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_mac_info - Points to MAC the information.
 *	Used by the attribute QCA_WLAN_VENDOR_ATTR_MAC_INFO of the
 *	vendor command QCA_NL80211_VENDOR_SUBCMD_WLAN_MAC_INFO.
 */
enum qca_wlan_vendor_attr_mac_info {
	QCA_WLAN_VENDOR_ATTR_MAC_INFO_INVALID = 0,
	/* Hardware MAC ID associated for the MAC (u32) */
	QCA_WLAN_VENDOR_ATTR_MAC_INFO_MAC_ID = 1,
	/* Band supported by the MAC at a given point.
	 * This is a u32 bitmask of BIT(NL80211_BAND_*) as described in %enum
	 * nl80211_band.
	 */
	QCA_WLAN_VENDOR_ATTR_MAC_INFO_BAND = 2,
	/* Refers to list of WLAN netdev interfaces associated with this MAC.
	 * Represented by enum qca_wlan_vendor_attr_mac_iface_info.
	 */
	QCA_WLAN_VENDOR_ATTR_MAC_IFACE_INFO = 3,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_MAC_INFO_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_MAC_INFO_MAX =
	QCA_WLAN_VENDOR_ATTR_MAC_INFO_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_get_logger_features - Used by the vendor command
 * QCA_NL80211_VENDOR_SUBCMD_GET_LOGGER_FEATURE_SET.
 */
enum qca_wlan_vendor_attr_get_logger_features {
	QCA_WLAN_VENDOR_ATTR_LOGGER_INVALID = 0,
	/* Unsigned 32-bit enum value of wifi_logger_supported_features */
	QCA_WLAN_VENDOR_ATTR_LOGGER_SUPPORTED = 1,
	/* keep last */
	QCA_WLAN_VENDOR_ATTR_LOGGER_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_LOGGER_MAX =
		QCA_WLAN_VENDOR_ATTR_LOGGER_AFTER_LAST - 1,
};

/**
 * enum wifi_logger_supported_features - Values for supported logger features
 */
enum wifi_logger_supported_features {
	WIFI_LOGGER_MEMORY_DUMP_FEATURE = (1 << (0)),
	WIFI_LOGGER_PER_PACKET_TX_RX_STATUS_FEATURE = (1 << (1)),
	WIFI_LOGGER_CONNECT_EVENT_FEATURE = (1 << (2)),
	WIFI_LOGGER_POWER_EVENT_FEATURE = (1 << (3)),
	WIFI_LOGGER_WAKE_LOCK_FEATURE = (1 << (4)),
	WIFI_LOGGER_VERBOSE_FEATURE = (1 << (5)),
	WIFI_LOGGER_WATCHDOG_TIMER_FEATURE = (1 << (6)),
	WIFI_LOGGER_DRIVER_DUMP_FEATURE = (1 << (7)),
	WIFI_LOGGER_PACKET_FATE_FEATURE = (1 << (8)),
};

/**
 * enum qca_wlan_tdls_caps_features_supported - Values for TDLS get
 * capabilities features
 */
enum qca_wlan_tdls_caps_features_supported {
	WIFI_TDLS_SUPPORT = (1 << (0)),
	WIFI_TDLS_EXTERNAL_CONTROL_SUPPORT = (1 << (1)),
	WIFI_TDLS_OFFCHANNEL_SUPPORT = (1 << (2))
};

/**
 * enum qca_wlan_vendor_attr_tdls_get_capabilities - Used by the vendor command
 * QCA_NL80211_VENDOR_SUBCMD_TDLS_GET_CAPABILITIES.
 */
enum qca_wlan_vendor_attr_tdls_get_capabilities {
	QCA_WLAN_VENDOR_ATTR_TDLS_GET_CAPS_INVALID = 0,
	/* Indicates the max concurrent sessions */
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_TDLS_GET_CAPS_MAX_CONC_SESSIONS,
	/* Indicates the support for features */
	/* Unsigned 32-bit bitmap qca_wlan_tdls_caps_features_supported
	 */
	QCA_WLAN_VENDOR_ATTR_TDLS_GET_CAPS_FEATURES_SUPPORTED,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_TDLS_GET_CAPS_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_TDLS_GET_CAPS_MAX =
		QCA_WLAN_VENDOR_ATTR_TDLS_GET_CAPS_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_offloaded_packets_sending_control - Offload packets control
 * command used as value for the attribute
 * QCA_WLAN_VENDOR_ATTR_OFFLOADED_PACKETS_SENDING_CONTROL.
 */
enum qca_wlan_offloaded_packets_sending_control {
	QCA_WLAN_OFFLOADED_PACKETS_SENDING_CONTROL_INVALID = 0,
	QCA_WLAN_OFFLOADED_PACKETS_SENDING_START,
	QCA_WLAN_OFFLOADED_PACKETS_SENDING_STOP
};

/**
 * enum qca_wlan_vendor_attr_offloaded_packets - Used by the vendor command
 * QCA_NL80211_VENDOR_SUBCMD_OFFLOADED_PACKETS.
 */
enum qca_wlan_vendor_attr_offloaded_packets {
	QCA_WLAN_VENDOR_ATTR_OFFLOADED_PACKETS_INVALID = 0,
	/* Takes valid value from the enum
	 * qca_wlan_offloaded_packets_sending_control
	 * Unsigned 32-bit value
	 */
	QCA_WLAN_VENDOR_ATTR_OFFLOADED_PACKETS_SENDING_CONTROL,
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_OFFLOADED_PACKETS_REQUEST_ID,
	/* array of u8 len: Max packet size */
	QCA_WLAN_VENDOR_ATTR_OFFLOADED_PACKETS_IP_PACKET_DATA,
	/* 6-byte MAC address used to represent source MAC address */
	QCA_WLAN_VENDOR_ATTR_OFFLOADED_PACKETS_SRC_MAC_ADDR,
	/* 6-byte MAC address used to represent destination MAC address */
	QCA_WLAN_VENDOR_ATTR_OFFLOADED_PACKETS_DST_MAC_ADDR,
	/* Unsigned 32-bit value, in milli seconds */
	QCA_WLAN_VENDOR_ATTR_OFFLOADED_PACKETS_PERIOD,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_OFFLOADED_PACKETS_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_OFFLOADED_PACKETS_MAX =
		QCA_WLAN_VENDOR_ATTR_OFFLOADED_PACKETS_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_rssi_monitoring_control - RSSI control commands used as values
 * by the attribute QCA_WLAN_VENDOR_ATTR_RSSI_MONITORING_CONTROL.
 */
enum qca_wlan_rssi_monitoring_control {
	QCA_WLAN_RSSI_MONITORING_CONTROL_INVALID = 0,
	QCA_WLAN_RSSI_MONITORING_START,
	QCA_WLAN_RSSI_MONITORING_STOP,
};

/**
 * enum qca_wlan_vendor_attr_rssi_monitoring - Used by the vendor command
 * QCA_NL80211_VENDOR_SUBCMD_MONITOR_RSSI.
 */
enum qca_wlan_vendor_attr_rssi_monitoring {
	QCA_WLAN_VENDOR_ATTR_RSSI_MONITORING_INVALID = 0,
	/* Takes valid value from the enum
	 * qca_wlan_rssi_monitoring_control
	 * Unsigned 32-bit value enum qca_wlan_rssi_monitoring_control
	 */
	QCA_WLAN_VENDOR_ATTR_RSSI_MONITORING_CONTROL,
	/* Unsigned 32-bit value */
	QCA_WLAN_VENDOR_ATTR_RSSI_MONITORING_REQUEST_ID,
	/* Signed 8-bit value in dBm */
	QCA_WLAN_VENDOR_ATTR_RSSI_MONITORING_MAX_RSSI,
	/* Signed 8-bit value in dBm */
	QCA_WLAN_VENDOR_ATTR_RSSI_MONITORING_MIN_RSSI,
	/* attributes to be used/received in callback */
	/* 6-byte MAC address used to represent current BSSID MAC address */
	QCA_WLAN_VENDOR_ATTR_RSSI_MONITORING_CUR_BSSID,
	/* Signed 8-bit value indicating the current RSSI */
	QCA_WLAN_VENDOR_ATTR_RSSI_MONITORING_CUR_RSSI,
	/* keep last */
	QCA_WLAN_VENDOR_ATTR_RSSI_MONITORING_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_RSSI_MONITORING_MAX =
		QCA_WLAN_VENDOR_ATTR_RSSI_MONITORING_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_ndp_params - Used by the vendor command
 * QCA_NL80211_VENDOR_SUBCMD_NDP.
 */
enum qca_wlan_vendor_attr_ndp_params {
	QCA_WLAN_VENDOR_ATTR_NDP_PARAM_INVALID = 0,
	/* Unsigned 32-bit value
	 * enum of sub commands values in qca_wlan_ndp_sub_cmd
	 */
	QCA_WLAN_VENDOR_ATTR_NDP_SUBCMD,
	/* Unsigned 16-bit value */
	QCA_WLAN_VENDOR_ATTR_NDP_TRANSACTION_ID,
	/* NL attributes for data used NDP SUB cmds */
	/* Unsigned 32-bit value indicating a service info */
	QCA_WLAN_VENDOR_ATTR_NDP_SERVICE_INSTANCE_ID,
	/* Unsigned 32-bit value; channel frequency in MHz */
	QCA_WLAN_VENDOR_ATTR_NDP_CHANNEL,
	/* Interface Discovery MAC address. An array of 6 Unsigned int8 */
	QCA_WLAN_VENDOR_ATTR_NDP_PEER_DISCOVERY_MAC_ADDR,
	/* Interface name on which NDP is being created */
	QCA_WLAN_VENDOR_ATTR_NDP_IFACE_STR,
	/* Unsigned 32-bit value for security */
	/* CONFIG_SECURITY is deprecated, use NCS_SK_TYPE/PMK/SCID instead */
	QCA_WLAN_VENDOR_ATTR_NDP_CONFIG_SECURITY,
	/* Unsigned 32-bit value for QoS */
	QCA_WLAN_VENDOR_ATTR_NDP_CONFIG_QOS,
	/* Array of u8: len = QCA_WLAN_VENDOR_ATTR_NAN_DP_APP_INFO_LEN */
	QCA_WLAN_VENDOR_ATTR_NDP_APP_INFO,
	/* Unsigned 32-bit value for NDP instance Id */
	QCA_WLAN_VENDOR_ATTR_NDP_INSTANCE_ID,
	/* Array of instance Ids */
	QCA_WLAN_VENDOR_ATTR_NDP_INSTANCE_ID_ARRAY,
	/* Unsigned 32-bit value for initiator/responder NDP response code
	 * accept/reject
	 */
	QCA_WLAN_VENDOR_ATTR_NDP_RESPONSE_CODE,
	/* NDI MAC address. An array of 6 Unsigned int8 */
	QCA_WLAN_VENDOR_ATTR_NDP_NDI_MAC_ADDR,
	/* Unsigned 32-bit value errors types returned by driver
	 * The wifi_nan.h in AOSP project platform/hardware/libhardware_legacy
	 * NanStatusType includes these values.
	 */
	QCA_WLAN_VENDOR_ATTR_NDP_DRV_RESPONSE_STATUS_TYPE,
	/* Unsigned 32-bit value error values returned by driver
	 * The nan_i.h in AOSP project platform/hardware/qcom/wlan
	 * NanInternalStatusType includes these values.
	 */
	QCA_WLAN_VENDOR_ATTR_NDP_DRV_RETURN_VALUE,
	/* Unsigned 32-bit value for Channel setup configuration
	 * The wifi_nan.h in AOSP project platform/hardware/libhardware_legacy
	 * NanDataPathChannelCfg includes these values.
	 */
	QCA_WLAN_VENDOR_ATTR_NDP_CHANNEL_CONFIG,
	/* Unsigned 32-bit value for Cipher Suite Shared Key Type */
	QCA_WLAN_VENDOR_ATTR_NDP_CSID,
	/* Array of u8: len = NAN_PMK_INFO_LEN 32 bytes */
	QCA_WLAN_VENDOR_ATTR_NDP_PMK,
	/* Security Context Identifier that contains the PMKID
	 * Array of u8: len = NAN_SCID_BUF_LEN 1024 bytes
	 */
	QCA_WLAN_VENDOR_ATTR_NDP_SCID,
	/* Array of u8: len = NAN_SECURITY_MAX_PASSPHRASE_LEN 63 bytes */
	QCA_WLAN_VENDOR_ATTR_NDP_PASSPHRASE,
	/* Array of u8: len = NAN_MAX_SERVICE_NAME_LEN 255 bytes */
	QCA_WLAN_VENDOR_ATTR_NDP_SERVICE_NAME,
	/* Unsigned 32-bit bitmap indicating schedule update
	 * BIT_0: NSS Update
	 * BIT_1: Channel list update
	 */
	QCA_WLAN_VENDOR_ATTR_NDP_SCHEDULE_UPDATE_REASON,
	/* Unsigned 32-bit value for NSS */
	QCA_WLAN_VENDOR_ATTR_NDP_NSS,
	/* Unsigned 32-bit value for NUMBER NDP CHANNEL */
	QCA_WLAN_VENDOR_ATTR_NDP_NUM_CHANNELS,
	/* Unsigned 32-bit value for CHANNEL BANDWIDTH
	 * 0:20 MHz, 1:40 MHz, 2:80 MHz, 3:160 MHz
	 */
	QCA_WLAN_VENDOR_ATTR_NDP_CHANNEL_WIDTH,
	/* Array of channel/band width */
	QCA_WLAN_VENDOR_ATTR_NDP_CHANNEL_INFO,
	/* IPv6 address used by NDP (in network byte order), 16 bytes array.
	 * This attribute is used and optional for ndp request, ndp response,
	 * ndp indication, and ndp confirm.
	 */
	QCA_WLAN_VENDOR_ATTR_NDP_IPV6_ADDR = 27,
	/* Unsigned 16-bit value indicating transport port used by NDP.
	 * This attribute is used and optional for ndp response, ndp indication,
	 * and ndp confirm.
	 */
	QCA_WLAN_VENDOR_ATTR_NDP_TRANSPORT_PORT = 28,
	/* Unsigned 8-bit value indicating protocol used by NDP and assigned by
	 * the Internet Assigned Numbers Authority (IANA) as per:
	 * https://www.iana.org/assignments/protocol-numbers/protocol-numbers.xhtml
	 * This attribute is used and optional for ndp response, ndp indication,
	 * and ndp confirm.
	 */
	QCA_WLAN_VENDOR_ATTR_NDP_TRANSPORT_PROTOCOL = 29,
	/* Unsigned 8-bit value indicating if NDP remote peer supports NAN NDPE.
	 * 1:support 0:not support
	 */
	QCA_WLAN_VENDOR_ATTR_PEER_NDPE_SUPPORT = 30,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_NDP_PARAMS_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_NDP_PARAMS_MAX =
		QCA_WLAN_VENDOR_ATTR_NDP_PARAMS_AFTER_LAST - 1,
};

enum qca_wlan_ndp_sub_cmd {
	QCA_WLAN_VENDOR_ATTR_NDP_INVALID = 0,
	/* Command to create a NAN data path interface */
	QCA_WLAN_VENDOR_ATTR_NDP_INTERFACE_CREATE = 1,
	/* Command to delete a NAN data path interface */
	QCA_WLAN_VENDOR_ATTR_NDP_INTERFACE_DELETE = 2,
	/* Command to initiate a NAN data path session */
	QCA_WLAN_VENDOR_ATTR_NDP_INITIATOR_REQUEST = 3,
	/* Command to notify if the NAN data path session was sent */
	QCA_WLAN_VENDOR_ATTR_NDP_INITIATOR_RESPONSE = 4,
	/* Command to respond to NAN data path session */
	QCA_WLAN_VENDOR_ATTR_NDP_RESPONDER_REQUEST = 5,
	/* Command to notify on the responder about the response */
	QCA_WLAN_VENDOR_ATTR_NDP_RESPONDER_RESPONSE = 6,
	/* Command to initiate a NAN data path end */
	QCA_WLAN_VENDOR_ATTR_NDP_END_REQUEST = 7,
	/* Command to notify the if end request was sent */
	QCA_WLAN_VENDOR_ATTR_NDP_END_RESPONSE = 8,
	/* Command to notify the peer about the end request */
	QCA_WLAN_VENDOR_ATTR_NDP_REQUEST_IND = 9,
	/* Command to confirm the NAN data path session is complete */
	QCA_WLAN_VENDOR_ATTR_NDP_CONFIRM_IND = 10,
	/* Command to indicate the peer about the end request being received */
	QCA_WLAN_VENDOR_ATTR_NDP_END_IND = 11,
	/* Command to indicate the peer of schedule update */
	QCA_WLAN_VENDOR_ATTR_NDP_SCHEDULE_UPDATE_IND = 12
};

/**
 * enum qca_wlan_vendor_attr_nd_offload - Used by the vendor command
 * QCA_NL80211_VENDOR_SUBCMD_ND_OFFLOAD.
 */
enum qca_wlan_vendor_attr_nd_offload {
	QCA_WLAN_VENDOR_ATTR_ND_OFFLOAD_INVALID = 0,
	/* Flag to set Neighbour Discovery offload */
	QCA_WLAN_VENDOR_ATTR_ND_OFFLOAD_FLAG,
	/* Keep last */
	QCA_WLAN_VENDOR_ATTR_ND_OFFLOAD_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_ND_OFFLOAD_MAX =
		QCA_WLAN_VENDOR_ATTR_ND_OFFLOAD_AFTER_LAST - 1,
};

/**
 * enum packet_filter_sub_cmd - Packet filter sub commands
 */
enum packet_filter_sub_cmd {
	/**
	 * Write packet filter program and/or data. The driver/firmware should
	 * disable APF before writing into local buffer and re-enable APF after
	 * writing is done.
	 */
	QCA_WLAN_SET_PACKET_FILTER = 1,
	/* Get packet filter feature capabilities from driver */
	QCA_WLAN_GET_PACKET_FILTER = 2,
	/**
	 * Write packet filter program and/or data. User space will send the
	 * %QCA_WLAN_DISABLE_PACKET_FILTER command before issuing this command
	 * and will send the %QCA_WLAN_ENABLE_PACKET_FILTER afterwards. The key
	 * difference from that %QCA_WLAN_SET_PACKET_FILTER is the control over
	 * enable/disable is given to user space with this command. Also,
	 * user space sends the length of program portion in the buffer within
	 * %QCA_WLAN_VENDOR_ATTR_PACKET_FILTER_PROG_LENGTH.
	 */
	QCA_WLAN_WRITE_PACKET_FILTER = 3,
	/* Read packet filter program and/or data */
	QCA_WLAN_READ_PACKET_FILTER = 4,
	/* Enable APF feature */
	QCA_WLAN_ENABLE_PACKET_FILTER = 5,
	/* Disable APF feature */
	QCA_WLAN_DISABLE_PACKET_FILTER = 6,
};

/**
 * enum qca_wlan_vendor_attr_packet_filter - BPF control commands used by
 * vendor QCA_NL80211_VENDOR_SUBCMD_PACKET_FILTER.
 */
enum qca_wlan_vendor_attr_packet_filter {
	QCA_WLAN_VENDOR_ATTR_PACKET_FILTER_INVALID = 0,
	/* Unsigned 32-bit enum passed using packet_filter_sub_cmd */
	QCA_WLAN_VENDOR_ATTR_PACKET_FILTER_SUB_CMD,
	/* Unsigned 32-bit value indicating the packet filter version */
	QCA_WLAN_VENDOR_ATTR_PACKET_FILTER_VERSION,
	/* Unsigned 32-bit value indicating the packet filter id */
	QCA_WLAN_VENDOR_ATTR_PACKET_FILTER_ID,
	/**
	 * Unsigned 32-bit value indicating the packet filter size including
	 * program + data.
	 */
	QCA_WLAN_VENDOR_ATTR_PACKET_FILTER_SIZE,
	/* Unsigned 32-bit value indicating the packet filter current offset */
	QCA_WLAN_VENDOR_ATTR_PACKET_FILTER_CURRENT_OFFSET,
	/* Program and/or data in bytes */
	QCA_WLAN_VENDOR_ATTR_PACKET_FILTER_PROGRAM,
	/* Unsigned 32-bit value of the length of the program section in packet
	 * filter buffer.
	 */
	QCA_WLAN_VENDOR_ATTR_PACKET_FILTER_PROG_LENGTH = 7,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_PACKET_FILTER_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_PACKET_FILTER_MAX =
	QCA_WLAN_VENDOR_ATTR_PACKET_FILTER_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_drv_info - WLAN driver info used by vendor command
 * QCA_NL80211_VENDOR_SUBCMD_GET_BUS_SIZE.
 */
enum qca_wlan_vendor_drv_info {
	QCA_WLAN_VENDOR_ATTR_DRV_INFO_INVALID = 0,
	/* Maximum Message size info between firmware & HOST
	 * Unsigned 32-bit value
	 */
	QCA_WLAN_VENDOR_ATTR_DRV_INFO_BUS_SIZE,
	/* keep last */
	QCA_WLAN_VENDOR_ATTR_DRV_INFO_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_DRV_INFO_MAX =
		QCA_WLAN_VENDOR_ATTR_DRV_INFO_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_wake_stats - Wake lock stats used by vendor
 * command QCA_NL80211_VENDOR_SUBCMD_GET_WAKE_REASON_STATS.
 */
enum qca_wlan_vendor_attr_wake_stats {
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_INVALID = 0,
	/* Unsigned 32-bit value indicating the total count of wake event */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_TOTAL_CMD_EVENT_WAKE,
	/* Array of individual wake count, each index representing wake reason
	 */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_CMD_EVENT_WAKE_CNT_PTR,
	/* Unsigned 32-bit value representing wake count array */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_CMD_EVENT_WAKE_CNT_SZ,
	/* Unsigned 32-bit total wake count value of driver/fw */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_TOTAL_DRIVER_FW_LOCAL_WAKE,
	/* Array of wake stats of driver/fw */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_DRIVER_FW_LOCAL_WAKE_CNT_PTR,
	/* Unsigned 32-bit total wake count value of driver/fw */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_DRIVER_FW_LOCAL_WAKE_CNT_SZ,
	/* Unsigned 32-bit total wake count value of packets received */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_TOTAL_RX_DATA_WAKE,
	/* Unsigned 32-bit wake count value unicast packets received */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_RX_UNICAST_CNT,
	/* Unsigned 32-bit wake count value multicast packets received */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_RX_MULTICAST_CNT,
	/* Unsigned 32-bit wake count value broadcast packets received */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_RX_BROADCAST_CNT,
	/* Unsigned 32-bit wake count value of ICMP packets */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_ICMP_PKT,
	/* Unsigned 32-bit wake count value of ICMP6 packets */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_ICMP6_PKT,
	/* Unsigned 32-bit value ICMP6 router advertisement */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_ICMP6_RA,
	/* Unsigned 32-bit value ICMP6 neighbor advertisement */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_ICMP6_NA,
	/* Unsigned 32-bit value ICMP6 neighbor solicitation */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_ICMP6_NS,
	/* Unsigned 32-bit wake count value of receive side ICMP4 multicast */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_ICMP4_RX_MULTICAST_CNT,
	/* Unsigned 32-bit wake count value of receive side ICMP6 multicast */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_ICMP6_RX_MULTICAST_CNT,
	/* Unsigned 32-bit wake count value of receive side multicast */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_OTHER_RX_MULTICAST_CNT,
	/* Unsigned 32-bit wake count value of a given RSSI breach */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_RSSI_BREACH_CNT,
	/* Unsigned 32-bit wake count value of low RSSI */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_LOW_RSSI_CNT,
	/* Unsigned 32-bit value GSCAN count */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_GSCAN_CNT,
	/* Unsigned 32-bit value PNO complete count */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_PNO_COMPLETE_CNT,
	/* Unsigned 32-bit value PNO match count */
	QCA_WLAN_VENDOR_ATTR_WAKE_STATS_PNO_MATCH_CNT,
	/* keep last */
	QCA_WLAN_VENDOR_GET_WAKE_STATS_AFTER_LAST,
	QCA_WLAN_VENDOR_GET_WAKE_STATS_MAX =
		QCA_WLAN_VENDOR_GET_WAKE_STATS_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_thermal_cmd - Vendor subcmd attributes to set
 * cmd value. Used for NL attributes for data used by
 * QCA_NL80211_VENDOR_SUBCMD_THERMAL_CMD sub command.
 */
enum qca_wlan_vendor_attr_thermal_cmd {
	QCA_WLAN_VENDOR_ATTR_THERMAL_CMD_INVALID = 0,
	/* The value of command, driver will implement different operations
	 * according to this value. It uses values defined in
	 * enum qca_wlan_vendor_attr_thermal_cmd_type.
	 * u32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_THERMAL_CMD_VALUE = 1,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_THERMAL_CMD_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_THERMAL_CMD_MAX =
	QCA_WLAN_VENDOR_ATTR_THERMAL_CMD_AFTER_LAST - 1
};

/**
 * qca_wlan_vendor_attr_thermal_cmd_type: Attribute values for
 * QCA_WLAN_VENDOR_ATTR_THERMAL_CMD_VALUE to the vendor subcmd
 * QCA_NL80211_VENDOR_SUBCMD_THERMAL_CMD. This represents the
 * thermal command types sent to driver.
 * @QCA_WLAN_VENDOR_ATTR_THERMAL_CMD_TYPE_GET_PARAMS: Request to
 * get thermal shutdown configuration parameters for display. Parameters
 * responded from driver are defined in
 * enum qca_wlan_vendor_attr_get_thermal_params_rsp.
 * @QCA_WLAN_VENDOR_ATTR_THERMAL_CMD_TYPE_GET_TEMPERATURE: Request to
 * get temperature. Host should respond with a temperature data. It is defined
 * in enum qca_wlan_vendor_attr_thermal_get_temperature.
 * @QCA_WLAN_VENDOR_ATTR_THERMAL_CMD_TYPE_SUSPEND: Request to execute thermal
 * suspend action.
 * @QCA_WLAN_VENDOR_ATTR_THERMAL_CMD_TYPE_RESUME: Request to execute thermal
 * resume action.
 */
enum qca_wlan_vendor_attr_thermal_cmd_type {
	QCA_WLAN_VENDOR_ATTR_THERMAL_CMD_TYPE_GET_PARAMS,
	QCA_WLAN_VENDOR_ATTR_THERMAL_CMD_TYPE_GET_TEMPERATURE,
	QCA_WLAN_VENDOR_ATTR_THERMAL_CMD_TYPE_SUSPEND,
	QCA_WLAN_VENDOR_ATTR_THERMAL_CMD_TYPE_RESUME,
};

/**
 * enum qca_wlan_vendor_attr_thermal_get_temperature - vendor subcmd attributes
 * to get chip temperature by user.
 * enum values are used for NL attributes for data used by
 * QCA_WLAN_VENDOR_ATTR_THERMAL_CMD_TYPE_GET_TEMPERATURE command for data used
 * by QCA_NL80211_VENDOR_SUBCMD_THERMAL_CMD sub command.
 */
enum qca_wlan_vendor_attr_thermal_get_temperature {
	QCA_WLAN_VENDOR_ATTR_THERMAL_GET_TEMPERATURE_INVALID = 0,
	/* Temperature value (degree Celsius) from driver.
	 * u32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_THERMAL_GET_TEMPERATURE_DATA,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_THERMAL_GET_TEMPERATURE_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_THERMAL_GET_TEMPERATURE_MAX =
	QCA_WLAN_VENDOR_ATTR_THERMAL_GET_TEMPERATURE_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_get_thermal_params_rsp - vendor subcmd attributes
 * to get configuration parameters of thermal shutdown feature. Enum values are
 * used by QCA_WLAN_VENDOR_ATTR_THERMAL_CMD_TYPE_GET_PARAMS command for data
 * used by QCA_NL80211_VENDOR_SUBCMD_THERMAL_CMD sub command.
 */
enum qca_wlan_vendor_attr_get_thermal_params_rsp {
	QCA_WLAN_VENDOR_ATTR_GET_THERMAL_PARAMS_RSP_INVALID = 0,
	/* Indicate if the thermal shutdown feature is enabled.
	 * NLA_FLAG attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_GET_THERMAL_PARAMS_RSP_SHUTDOWN_EN,
	/* Indicate if the auto mode is enabled.
	 * Enable: Driver triggers the suspend/resume action.
	 * Disable: User space triggers the suspend/resume action.
	 * NLA_FLAG attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_GET_THERMAL_PARAMS_RSP_SHUTDOWN_AUTO_EN,
	/* Thermal resume threshold (degree Celsius). Issue the resume command
	 * if the temperature value is lower than this threshold.
	 * u16 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_GET_THERMAL_PARAMS_RSP_RESUME_THRESH,
	/* Thermal warning threshold (degree Celsius). FW reports temperature
	 * to driver if it's higher than this threshold.
	 * u16 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_GET_THERMAL_PARAMS_RSP_WARNING_THRESH,
	/* Thermal suspend threshold (degree Celsius). Issue the suspend command
	 * if the temperature value is higher than this threshold.
	 * u16 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_GET_THERMAL_PARAMS_RSP_SUSPEND_THRESH,
	/* FW reports temperature data periodically at this interval (ms).
	 * u16 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_GET_THERMAL_PARAMS_RSP_SAMPLE_RATE,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_GET_THERMAL_PARAMS_RSP_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_GET_THERMAL_PARAMS_RSP_MAX =
	QCA_WLAN_VENDOR_ATTR_GET_THERMAL_PARAMS_RSP_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_thermal_event - vendor subcmd attributes to
 * report thermal events from driver to user space.
 * enum values are used for NL attributes for data used by
 * QCA_NL80211_VENDOR_SUBCMD_THERMAL_EVENT sub command.
 */
enum qca_wlan_vendor_attr_thermal_event {
	QCA_WLAN_VENDOR_ATTR_THERMAL_EVENT_INVALID = 0,
	/* Temperature value (degree Celsius) from driver.
	 * u32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_THERMAL_EVENT_TEMPERATURE,
	/* Indication of resume completion from power save mode.
	 * NLA_FLAG attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_THERMAL_EVENT_RESUME_COMPLETE,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_THERMAL_EVENT_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_THERMAL_EVENT_MAX =
	QCA_WLAN_VENDOR_ATTR_THERMAL_EVENT_AFTER_LAST - 1,
};

/**
 * enum he_fragmentation_val - HE fragmentation support values
 * Indicates level of dynamic fragmentation that is supported by
 * a STA as a recipient.
 * HE fragmentation values are defined in IEEE P802.11ax/D2.0, 9.4.2.237.2
 * (HE MAC Capabilities Information field) and are used in HE Capabilities
 * element to advertise the support. These values are validated in the driver
 * to check the device capability and advertised in the HE Capabilities
 * element. These values are used to configure testbed device to allow the
 * advertised hardware capabilities to be downgraded for testing purposes.
 *
 * @HE_FRAG_DISABLE: no support for dynamic fragmentation
 * @HE_FRAG_LEVEL1: support for dynamic fragments that are
 *	contained within an MPDU or S-MPDU, no support for dynamic fragments
 *	within an A-MPDU that is not an S-MPDU.
 * @HE_FRAG_LEVEL2: support for dynamic fragments that are
 *	contained within an MPDU or S-MPDU and support for up to one dynamic
 *	fragment for each MSDU, each A-MSDU if supported by the recipient, and
 *	each MMPDU within an A-MPDU or multi-TID A-MPDU that is not an
 *	MPDU or S-MPDU.
 * @HE_FRAG_LEVEL3: support for dynamic fragments that are
 *	contained within an MPDU or S-MPDU and support for multiple dynamic
 *	fragments for each MSDU and for each A-MSDU if supported by the
 *	recipient within an A-MPDU or multi-TID AMPDU and up to one dynamic
 *	fragment for each MMPDU in a multi-TID A-MPDU that is not an S-MPDU.
 */
enum he_fragmentation_val {
	HE_FRAG_DISABLE,
	HE_FRAG_LEVEL1,
	HE_FRAG_LEVEL2,
	HE_FRAG_LEVEL3,
};

/**
 * enum he_mcs_config - HE MCS support configuration
 *
 * Configures the HE Tx/Rx MCS map in HE capability IE for given bandwidth.
 * These values are used in driver to configure the HE MCS map to advertise
 * Tx/Rx MCS map in HE capability and these values are applied for all the
 * streams supported by the device. To configure MCS for different bandwidths,
 * vendor command needs to be sent using this attribute with appropriate value.
 * For example, to configure HE_80_MCS_0_7, send vendor command using HE MCS
 * attribute with HE_80_MCS0_7. And to configure HE MCS for HE_160_MCS0_11
 * send this command using HE MCS config attribute with value HE_160_MCS0_11.
 * These values are used to configure testbed device to allow the advertised
 * hardware capabilities to be downgraded for testing purposes. The enum values
 * are defined such that BIT[1:0] indicates the MCS map value. Values 3,7 and
 * 11 are not used as BIT[1:0] value is 3 which is used to disable MCS map.
 * These values are validated in the driver before setting the MCS map and
 * driver returns error if the input is other than these enum values.
 *
 * @HE_80_MCS0_7: support for HE 80/40/20 MHz MCS 0 to 7
 * @HE_80_MCS0_9: support for HE 80/40/20 MHz MCS 0 to 9
 * @HE_80_MCS0_11: support for HE 80/40/20 MHz MCS 0 to 11
 * @HE_160_MCS0_7: support for HE 160 MHz MCS 0 to 7
 * @HE_160_MCS0_9: support for HE 160 MHz MCS 0 to 9
 * @HE_160_MCS0_11: support for HE 160 MHz MCS 0 to 11
 * @HE_80P80_MCS0_7: support for HE 80p80 MHz MCS 0 to 7
 * @HE_80P80_MCS0_9: support for HE 80p80 MHz MCS 0 to 9
 * @HE_80P80_MCS0_11: support for HE 80p80 MHz MCS 0 to 11
 */
enum he_mcs_config {
	HE_80_MCS0_7 = 0,
	HE_80_MCS0_9 = 1,
	HE_80_MCS0_11 = 2,
	HE_160_MCS0_7 = 4,
	HE_160_MCS0_9 = 5,
	HE_160_MCS0_11 = 6,
	HE_80P80_MCS0_7 = 8,
	HE_80P80_MCS0_9 = 9,
	HE_80P80_MCS0_11 = 10,
};

/**
 * enum qca_wlan_ba_session_config - BA session configuration
 *
 * Indicates the configuration values for BA session configuration attribute.
 *
 * @QCA_WLAN_ADD_BA: Establish a new BA session with given configuration.
 * @QCA_WLAN_DELETE_BA: Delete the existing BA session for given TID.
 */
enum qca_wlan_ba_session_config {
	QCA_WLAN_ADD_BA = 1,
	QCA_WLAN_DELETE_BA = 2,
};

/**
 * enum qca_wlan_ac_type - Access category type
 *
 * Indicates the access category type value.
 *
 * @QCA_WLAN_AC_BE: BE access category
 * @QCA_WLAN_AC_BK: BK access category
 * @QCA_WLAN_AC_VI: VI access category
 * @QCA_WLAN_AC_VO: VO access category
 * @QCA_WLAN_AC_ALL: All ACs
 */
enum qca_wlan_ac_type {
	QCA_WLAN_AC_BE = 0,
	QCA_WLAN_AC_BK = 1,
	QCA_WLAN_AC_VI = 2,
	QCA_WLAN_AC_VO = 3,
	QCA_WLAN_AC_ALL = 4,
};

/**
 * enum qca_wlan_he_ltf_cfg - HE LTF configuration
 *
 * Indicates the HE LTF configuration value.
 *
 * @QCA_WLAN_HE_LTF_AUTO: HE-LTF is automatically set to the mandatory HE-LTF,
 * based on the GI setting
 * @QCA_WLAN_HE_LTF_1X: 1X HE LTF is 3.2us LTF
 * @QCA_WLAN_HE_LTF_2X: 2X HE LTF is 6.4us LTF
 * @QCA_WLAN_HE_LTF_4X: 4X HE LTF is 12.8us LTF
 */
enum qca_wlan_he_ltf_cfg {
	QCA_WLAN_HE_LTF_AUTO = 0,
	QCA_WLAN_HE_LTF_1X = 1,
	QCA_WLAN_HE_LTF_2X = 2,
	QCA_WLAN_HE_LTF_4X = 3,
};

/**
 * enum qca_wlan_he_mac_padding_dur - HE trigger frame MAC padding duration
 *
 * Indicates the HE trigger frame MAC padding duration value.
 *
 * @QCA_WLAN_HE_NO_ADDITIONAL_PROCESS_TIME: no additional time required to
 * process the trigger frame.
 * @QCA_WLAN_HE_8US_OF_PROCESS_TIME: indicates the 8us of processing time for
 * trigger frame.
 * @QCA_WLAN_HE_16US_OF_PROCESS_TIME: indicates the 16us of processing time for
 * trigger frame.
 */
enum qca_wlan_he_mac_padding_dur {
	QCA_WLAN_HE_NO_ADDITIONAL_PROCESS_TIME = 0,
	QCA_WLAN_HE_8US_OF_PROCESS_TIME = 1,
	QCA_WLAN_HE_16US_OF_PROCESS_TIME = 2,
};

/**
 * enum qca_wlan_he_om_ctrl_ch_bw - HE OM control field BW configuration
 *
 * Indicates the HE Operating mode control channel width setting value.
 *
 * @QCA_WLAN_HE_OM_CTRL_BW_20M: Primary 20 MHz
 * @QCA_WLAN_HE_OM_CTRL_BW_40M: Primary 40 MHz
 * @QCA_WLAN_HE_OM_CTRL_BW_80M: Primary 80 MHz
 * @QCA_WLAN_HE_OM_CTRL_BW_160M: 160 MHz and 80+80 MHz
 */
enum qca_wlan_he_om_ctrl_ch_bw {
	QCA_WLAN_HE_OM_CTRL_BW_20M = 0,
	QCA_WLAN_HE_OM_CTRL_BW_40M = 1,
	QCA_WLAN_HE_OM_CTRL_BW_80M = 2,
	QCA_WLAN_HE_OM_CTRL_BW_160M = 3,
};

/* Attributes for data used by
 * QCA_NL80211_VENDOR_SUBCMD_WIFI_TEST_CONFIGURATION
 */
enum qca_wlan_vendor_attr_wifi_test_config {
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_INVALID = 0,
	/* 8-bit unsigned value to configure the driver to enable/disable
	 * WMM feature. This attribute is used to configure testbed device.
	 * 1-enable, 0-disable
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_WMM_ENABLE = 1,

	/* 8-bit unsigned value to configure the driver to accept/reject
	 * the addba request from peer. This attribute is used to configure
	 * the testbed device.
	 * 1-accept addba, 0-reject addba
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_ACCEPT_ADDBA_REQ = 2,

	/* 8-bit unsigned value to configure the driver to send or not to
	 * send the addba request to peer.
	 * This attribute is used to configure the testbed device.
	 * 1-send addba, 0-do not send addba
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_SEND_ADDBA_REQ = 3,

	/* 8-bit unsigned value to indicate the HE fragmentation support.
	 * Uses enum he_fragmentation_val values.
	 * This attribute is used to configure the testbed device to
	 * allow the advertised hardware capabilities to be downgraded
	 * for testing purposes.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_FRAGMENTATION = 4,

	/* 8-bit unsigned value to indicate the HE MCS support.
	 * Uses enum he_mcs_config values.
	 * This attribute is used to configure the testbed device to
	 * allow the advertised hardware capabilities to be downgraded
	 * for testing purposes.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_MCS = 5,

	/* 8-bit unsigned value to configure the driver to allow or not to
	 * allow the connection with WEP/TKIP in HT/VHT/HE modes.
	 * This attribute is used to configure the testbed device.
	 * 1-allow WEP/TKIP in HT/VHT/HE, 0-do not allow WEP/TKIP.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_WEP_TKIP_IN_HE = 6,

	/* 8-bit unsigned value to configure the driver to add a
	 * new BA session or delete the existing BA session for
	 * given TID. ADDBA command uses the buffer size and TID
	 * configuration if user specifies the values else default
	 * value for buffer size is used for all TIDs if the TID
	 * also not specified. For DEL_BA command TID value is
	 * required to process the command.
	 * Uses enum qca_wlan_ba_session_config values.
	 * This attribute is used to configure the testbed device.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_ADD_DEL_BA_SESSION = 7,

	/* 16-bit unsigned value to configure the buffer size in addba
	 * request and response frames.
	 * This attribute is used to configure the testbed device.
	 * The range of the value is 0 to 256.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_ADDBA_BUFF_SIZE = 8,

	/* 8-bit unsigned value to configure the buffer size in addba
	 * request and response frames.
	 * This attribute is used to configure the testbed device.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_BA_TID = 9,

	/* 8-bit unsigned value to configure the no ack policy.
	 * To configure no ack policy, access category value is
	 * required to process the command.
	 * This attribute is used to configure the testbed device.
	 * 1 - enable no ack, 0 - disable no ack.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_ENABLE_NO_ACK = 10,

	/* 8-bit unsigned value to configure the AC for no ack policy
	 * This attribute is used to configure the testbed device.
	 * Uses the enum qca_wlan_ac_type values.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_NO_ACK_AC = 11,

	/* 8-bit unsigned value to configure the HE LTF
	 * This attribute is used to configure the testbed device.
	 * Uses the enum qca_wlan_he_ltf_cfg values.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_LTF = 12,

	/* 8-bit unsigned value to configure the tx beamformee.
	 * This attribute is used to configure the testbed device.
	 * 1-enable, 0-disable.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_ENABLE_TX_BEAMFORMEE = 13,

	/* 8-bit unsigned value to configure the tx beamformee number
	 * of space-time streams.
	 * This attribute is used to configure the testbed device.
	 * The range of the value is 0 to 8.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_TX_BEAMFORMEE_NSTS = 14,

	/* 8-bit unsigned value to configure the MU EDCA params for given AC
	 * This attribute is used to configure the testbed device.
	 * Uses the enum qca_wlan_ac_type values.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_MU_EDCA_AC = 15,

	/* 8-bit unsigned value to configure the MU EDCA AIFSN for given AC
	 * To configure MU EDCA AIFSN value, MU EDCA access category value
	 * is required to process the command.
	 * This attribute is used to configure the testbed device.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_MU_EDCA_AIFSN = 16,

	/* 8-bit unsigned value to configure the MU EDCA ECW min value for
	 * given AC.
	 * To configure MU EDCA ECW min value, MU EDCA access category value
	 * is required to process the command.
	 * This attribute is used to configure the testbed device.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_MU_EDCA_ECWMIN = 17,

	/* 8-bit unsigned value to configure the MU EDCA ECW max value for
	 * given AC.
	 * To configure MU EDCA ECW max value, MU EDCA access category value
	 * is required to process the command.
	 * This attribute is used to configure the testbed device.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_MU_EDCA_ECWMAX = 18,

	/* 8-bit unsigned value to configure the MU EDCA timer for given AC
	 * To configure MU EDCA timer value, MU EDCA access category value
	 * is required to process the command.
	 * This attribute is used to configure the testbed device.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_MU_EDCA_TIMER = 19,

	/* 8-bit unsigned value to configure the HE trigger frame MAC padding
	 * duration.
	 * This attribute is used to configure the testbed device.
	 * Uses the enum qca_wlan_he_mac_padding_dur values.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_MAC_PADDING_DUR = 20,

	/* 8-bit unsigned value to override the MU EDCA params to defaults
	 * regardless of the AP beacon MU EDCA params. If it is enabled use
	 * the default values else use the MU EDCA params from AP beacon.
	 * This attribute is used to configure the testbed device.
	 * 1-enable, 0-disable.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_OVERRIDE_MU_EDCA = 21,

	/* 8-bit unsigned value to configure the support for receiving
	 * an MPDU that contains an operating mode control subfield.
	 * This attribute is used to configure the testbed device.
	 * 1-enable, 0-disable.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_OM_CTRL_SUPP = 22,

	/* Nested attribute values required to setup the TWT session.
	 * enum qca_wlan_vendor_attr_twt_setup provides the necessary
	 * information to set up the session. It contains broadcast flags,
	 * set_up flags, trigger value, flow type, flow ID, wake interval
	 * exponent, protection, target wake time, wake duration, wake interval
	 * mantissa. These nested attributes are used to setup a host triggered
	 * TWT session.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_TWT_SETUP = 23,

	/* This nested attribute is used to terminate the current TWT session.
	 * It does not currently carry any attributes.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_TWT_TERMINATE = 24,

	/* This nested attribute is used to suspend the current TWT session.
	 * It does not currently carry any attributes.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_TWT_SUSPEND = 25,

	/* Nested attribute values to indicate the request for resume.
	 * This attribute is used to resume the TWT session.
	 * enum qca_wlan_vendor_attr_twt_resume provides the necessary
	 * parameters required to resume the TWT session.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_TWT_RESUME = 26,

	/* 8-bit unsigned value to set the HE operating mode control
	 * (OM CTRL) Channel Width subfield.
	 * The Channel Width subfield indicates the operating channel width
	 * supported by the STA for both reception and transmission.
	 * Uses the enum qca_wlan_he_om_ctrl_ch_bw values.
	 * This setting is cleared with the
	 * QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_CLEAR_HE_OM_CTRL_CONFIG
	 * flag attribute to reset defaults.
	 * This attribute is used to configure the testbed device.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_OM_CTRL_BW = 27,

	/* 8-bit unsigned value to configure the number of spatial
	 * streams in HE operating mode control field.
	 * This setting is cleared with the
	 * QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_CLEAR_HE_OM_CTRL_CONFIG
	 * flag attribute to reset defaults.
	 * This attribute is used to configure the testbed device.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_OM_CTRL_NSS = 28,

	/* Flag attribute to configure the UL MU disable bit in
	 * HE operating mode control field.
	 * This setting is cleared with the
	 * QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_CLEAR_HE_OM_CTRL_CONFIG
	 * flag attribute to reset defaults.
	 * This attribute is used to configure the testbed device.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_OM_CTRL_UL_MU_DISABLE = 29,

	/* Flag attribute to clear the previously set HE operating mode
	 * control field configuration.
	 * This attribute is used to configure the testbed device to reset
	 * defaults to clear any previously set HE operating mode control
	 * field configuration.
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_CLEAR_HE_OM_CTRL_CONFIG = 30,

	/* 8-bit unsigned value to configure HE single user PPDU
	 * transmission. By default this setting is disabled and it
	 * is disabled in the reset defaults of the device configuration.
	 * This attribute is used to configure the testbed device.
	 * 1-enable, 0-disable
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_TX_SUPPDU = 31,

	/* 8-bit unsigned value to configure action frame transmission
	 * in HE trigger based PPDU transmission.
	 * By default this setting is disabled and it is disabled in
	 * the reset defaults of the device configuration.
	 * This attribute is used to configure the testbed device.
	 * 1-enable, 0-disable
	 */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_ACTION_TX_TB_PPDU = 32,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_MAX =
	QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_bss_filter - Used by the vendor command
 * QCA_NL80211_VENDOR_SUBCMD_BSS_FILTER.
 * The user can add/delete the filter by specifying the BSSID/STA MAC address in
 * QCA_WLAN_VENDOR_ATTR_BSS_FILTER_MAC_ADDR, filter type in
 * QCA_WLAN_VENDOR_ATTR_BSS_FILTER_TYPE, add/delete action in
 * QCA_WLAN_VENDOR_ATTR_BSS_FILTER_ACTION in the request. The user can get the
 * statistics of an unassociated station by specifying the MAC address in
 * QCA_WLAN_VENDOR_ATTR_BSS_FILTER_MAC_ADDR, station type in
 * QCA_WLAN_VENDOR_ATTR_BSS_FILTER_TYPE, GET action in
 * QCA_WLAN_VENDOR_ATTR_BSS_FILTER_ACTION in the request. The user also can get
 * the statistics of all unassociated stations by specifying the Broadcast MAC
 * address (ff:ff:ff:ff:ff:ff) in QCA_WLAN_VENDOR_ATTR_BSS_FILTER_MAC_ADDR with
 * above procedure. In the response, driver shall specify statistics
 * information nested in QCA_WLAN_VENDOR_ATTR_BSS_FILTER_STA_STATS.
 */
enum qca_wlan_vendor_attr_bss_filter {
	QCA_WLAN_VENDOR_ATTR_BSS_FILTER_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_BSS_FILTER_MAC_ADDR = 1,
	/* Other BSS filter type, unsigned 8 bit value. One of the values
	 * in enum qca_wlan_vendor_bss_filter_type.
	 */
	QCA_WLAN_VENDOR_ATTR_BSS_FILTER_TYPE = 2,
	/* Other BSS filter action, unsigned 8 bit value. One of the values
	 * in enum qca_wlan_vendor_bss_filter_action.
	 */
	QCA_WLAN_VENDOR_ATTR_BSS_FILTER_ACTION = 3,
	/* Array of nested attributes where each entry is the statistics
	 * information of the specified station that belong to another BSS.
	 * Attributes for each entry are taken from enum
	 * qca_wlan_vendor_bss_filter_sta_stats.
	 * Other BSS station configured in
	 * QCA_NL80211_VENDOR_SUBCMD_BSS_FILTER with filter type
	 * QCA_WLAN_VENDOR_BSS_FILTER_TYPE_STA.
	 * Statistics returned by QCA_NL80211_VENDOR_SUBCMD_BSS_FILTER
	 * with filter action QCA_WLAN_VENDOR_BSS_FILTER_ACTION_GET.
	 */
	QCA_WLAN_VENDOR_ATTR_BSS_FILTER_STA_STATS = 4,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_BSS_FILTER_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_BSS_FILTER_MAX =
	QCA_WLAN_VENDOR_ATTR_BSS_FILTER_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_bss_filter_type - Type of
 * filter used in other BSS filter operations. Used by the vendor command
 * QCA_NL80211_VENDOR_SUBCMD_BSS_FILTER.
 *
 * @QCA_WLAN_VENDOR_BSS_FILTER_TYPE_BSSID: BSSID filter
 * @QCA_WLAN_VENDOR_BSS_FILTER_TYPE_STA: Station MAC address filter
 */
enum qca_wlan_vendor_bss_filter_type {
	QCA_WLAN_VENDOR_BSS_FILTER_TYPE_BSSID,
	QCA_WLAN_VENDOR_BSS_FILTER_TYPE_STA,
};

/**
 * enum qca_wlan_vendor_bss_filter_action - Type of
 * action in other BSS filter operations. Used by the vendor command
 * QCA_NL80211_VENDOR_SUBCMD_BSS_FILTER.
 *
 * @QCA_WLAN_VENDOR_BSS_FILTER_ACTION_ADD: Add filter
 * @QCA_WLAN_VENDOR_BSS_FILTER_ACTION_DEL: Delete filter
 * @QCA_WLAN_VENDOR_BSS_FILTER_ACTION_GET: Get the statistics
 */
enum qca_wlan_vendor_bss_filter_action {
	QCA_WLAN_VENDOR_BSS_FILTER_ACTION_ADD,
	QCA_WLAN_VENDOR_BSS_FILTER_ACTION_DEL,
	QCA_WLAN_VENDOR_BSS_FILTER_ACTION_GET,
};

/**
 * enum qca_wlan_vendor_bss_filter_sta_stats - Attributes for
 * the statistics of a specific unassociated station belonging to another BSS.
 * The statistics provides information of the unassociated station
 * filtered by other BSS operation - such as MAC, signal value.
 * Used by the vendor command QCA_NL80211_VENDOR_SUBCMD_BSS_FILTER.
 *
 * @QCA_WLAN_VENDOR_BSS_FILTER_STA_STATS_MAC: MAC address of the station.
 * @QCA_WLAN_VENDOR_BSS_FILTER_STA_STATS_RSSI: Last received signal strength
 *	of the station. Unsigned 8 bit number containing RSSI.
 * @QCA_WLAN_VENDOR_BSS_FILTER_STA_STATS_RSSI_TS: Time stamp of the host
 *	driver for the last received RSSI. Unsigned 64 bit number containing
 *	nanoseconds from the boottime.
 */
enum qca_wlan_vendor_bss_filter_sta_stats {
	QCA_WLAN_VENDOR_BSS_FILTER_STA_STATS_INVALID = 0,
	QCA_WLAN_VENDOR_BSS_FILTER_STA_STATS_MAC = 1,
	QCA_WLAN_VENDOR_BSS_FILTER_STA_STATS_RSSI = 2,
	QCA_WLAN_VENDOR_BSS_FILTER_STA_STATS_RSSI_TS = 3,

	/* keep last */
	QCA_WLAN_VENDOR_BSS_FILTER_STA_STATS_AFTER_LAST,
	QCA_WLAN_VENDOR_BSS_FILTER_STA_STATS_MAX =
	QCA_WLAN_VENDOR_BSS_FILTER_STA_STATS_AFTER_LAST - 1
};

/* enum qca_wlan_nan_subcmd_type - Type of NAN command used by attribute
 * QCA_WLAN_VENDOR_ATTR_NAN_SUBCMD_TYPE as a part of vendor command
 * QCA_NL80211_VENDOR_SUBCMD_NAN_EXT.
 */
enum qca_wlan_nan_ext_subcmd_type {
	/* Subcmd of type NAN Enable Request */
	QCA_WLAN_NAN_EXT_SUBCMD_TYPE_ENABLE_REQ = 1,
	/* Subcmd of type NAN Disable Request */
	QCA_WLAN_NAN_EXT_SUBCMD_TYPE_DISABLE_REQ = 2,
};

/**
 * enum qca_wlan_vendor_attr_nan_params - Used by the vendor command
 * QCA_NL80211_VENDOR_SUBCMD_NAN_EXT.
 */
enum qca_wlan_vendor_attr_nan_params {
	QCA_WLAN_VENDOR_ATTR_NAN_INVALID = 0,
	/* Carries NAN command for firmware component. Every vendor command
	 * QCA_NL80211_VENDOR_SUBCMD_NAN_EXT must contain this attribute with a
	 * payload containing the NAN command. NLA_BINARY attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_NAN_CMD_DATA = 1,
	/* Indicates the type of NAN command sent with
	 * QCA_NL80211_VENDOR_SUBCMD_NAN_EXT. enum qca_wlan_nan_ext_subcmd_type
	 * describes the possible range of values. This attribute is mandatory
	 * if the command being issued is either
	 * QCA_WLAN_NAN_EXT_SUBCMD_TYPE_ENABLE_REQ or
	 * QCA_WLAN_NAN_EXT_SUBCMD_TYPE_DISABLE_REQ. NLA_U32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_NAN_SUBCMD_TYPE = 2,
	/* Frequency (in MHz) of primary NAN discovery social channel in 2.4 GHz
	 * band. This attribute is mandatory when command type is
	 * QCA_WLAN_NAN_EXT_SUBCMD_TYPE_ENABLE_REQ. NLA_U32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_NAN_DISC_24GHZ_BAND_FREQ = 3,
	/* Frequency (in MHz) of secondary NAN discovery social channel in 5 GHz
	 * band. This attribute is optional and should be included when command
	 * type is QCA_WLAN_NAN_EXT_SUBCMD_TYPE_ENABLE_REQ and NAN discovery
	 * has to be started on 5GHz along with 2.4GHz. NLA_U32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_NAN_DISC_5GHZ_BAND_FREQ = 4,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_NAN_PARAMS_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_NAN_PARAMS_MAX =
		QCA_WLAN_VENDOR_ATTR_NAN_PARAMS_AFTER_LAST - 1
};

/**
 * enum qca_wlan_vendor_attr_twt_setup: Represents attributes for
 * TWT (Target Wake Time) setup request. These attributes are sent as part of
 * %QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_TWT_SETUP and
 * %QCA_NL80211_VENDOR_SUBCMD_WIFI_TEST_CONFIGURATION.
 *
 * @QCA_WLAN_VENDOR_ATTR_TWT_SETUP_BCAST: Flag attribute.
 * Disable (flag attribute not present) - Individual TWT
 * Enable (flag attribute present) - Broadcast TWT.
 * Individual means the session is between the STA and the AP.
 * This session is established using a separate negotiation between
 * STA and AP.
 * Broadcast means the session is across multiple STAs and an AP. The
 * configuration parameters are announced in Beacon frames by the AP.
 *
 * @QCA_WLAN_VENDOR_ATTR_TWT_SETUP_REQ_TYPE: Required (u8).
 * Unsigned 8-bit qca_wlan_vendor_twt_setup_req_type to
 * specify the TWT request type
 *
 * @QCA_WLAN_VENDOR_ATTR_TWT_SETUP_TRIGGER: Flag attribute
 * Enable (flag attribute present) - TWT with trigger support.
 * Disable (flag attribute not present) - TWT without trigger support.
 * Trigger means the AP will send the trigger frame to allow STA to send data.
 * Without trigger, the STA will wait for the MU EDCA timer before
 * transmitting the data.
 *
 * @QCA_WLAN_VENDOR_ATTR_TWT_SETUP_FLOW_TYPE: Required (u8)
 * 0 - Announced TWT - In this mode, STA may skip few service periods to
 * save more power. If STA wants to wake up, it will send a PS-POLL/QoS
 * NULL frame to AP.
 * 1 - Unannounced TWT - The STA will wakeup during every SP.
 *
 * @QCA_WLAN_VENDOR_ATTR_TWT_SETUP_FLOW_ID: Optional (u8)
 * Flow ID is the unique identifier for each TWT session.
 * Currently this is not required and dialog ID will be set to zero.
 *
 * @QCA_WLAN_VENDOR_ATTR_TWT_SETUP_WAKE_INTVL_EXP: Required (u8)
 * This attribute (exp) is used along with the mantissa to derive the
 * wake interval using the following formula:
 * pow(2,exp) = wake_intvl_us/wake_intvl_mantis
 * Wake interval is the interval between 2 successive SP.
 *
 * @QCA_WLAN_VENDOR_ATTR_TWT_SETUP_PROTECTION: Flag attribute
 * Enable (flag attribute present) - Protection required.
 * Disable (flag attribute not present) - Protection not required.
 * If protection is enabled, then the AP will use protection
 * mechanism using RTS/CTS to self to reserve the airtime.
 *
 * @QCA_WLAN_VENDOR_ATTR_TWT_SETUP_WAKE_TIME: Optional (u32)
 * This attribute is used as the SP offset which is the offset from
 * TSF after which the wake happens. The units are in microseconds. If
 * this attribute is not provided, then the value will be set to zero.
 *
 * @QCA_WLAN_VENDOR_ATTR_TWT_SETUP_WAKE_DURATION: Required (u32)
 * This is the duration of the service period. The units are in TU.
 *
 * @QCA_WLAN_VENDOR_ATTR_TWT_SETUP_WAKE_INTVL_MANTISSA: Required (u32)
 * This attribute is used to configure wake interval mantissa.
 * The units are in TU.
 */
enum qca_wlan_vendor_attr_twt_setup {
	QCA_WLAN_VENDOR_ATTR_TWT_SETUP_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_TWT_SETUP_BCAST = 1,
	QCA_WLAN_VENDOR_ATTR_TWT_SETUP_REQ_TYPE = 2,
	QCA_WLAN_VENDOR_ATTR_TWT_SETUP_TRIGGER = 3,
	QCA_WLAN_VENDOR_ATTR_TWT_SETUP_FLOW_TYPE = 4,
	QCA_WLAN_VENDOR_ATTR_TWT_SETUP_FLOW_ID = 5,
	QCA_WLAN_VENDOR_ATTR_TWT_SETUP_WAKE_INTVL_EXP = 6,
	QCA_WLAN_VENDOR_ATTR_TWT_SETUP_PROTECTION = 7,
	QCA_WLAN_VENDOR_ATTR_TWT_SETUP_WAKE_TIME = 8,
	QCA_WLAN_VENDOR_ATTR_TWT_SETUP_WAKE_DURATION = 9,
	QCA_WLAN_VENDOR_ATTR_TWT_SETUP_WAKE_INTVL_MANTISSA = 10,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_TWT_SETUP_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_TWT_SETUP_MAX =
	QCA_WLAN_VENDOR_ATTR_TWT_SETUP_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_twt_resume: Represents attributes for
 * TWT (Target Wake Time) resume request. These attributes are sent as part of
 * %QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_TWT_RESUME and
 * %QCA_NL80211_VENDOR_SUBCMD_WIFI_TEST_CONFIGURATION.
 *
 * @QCA_WLAN_VENDOR_ATTR_TWT_RESUME_NEXT_TWT: Optional (u8)
 * This attribute is used as the SP offset which is the offset from
 * TSF after which the wake happens. The units are in microseconds.
 * If this attribute is not provided, then the value will be set to
 * zero.
 *
 * @QCA_WLAN_VENDOR_ATTR_TWT_RESUME_NEXT_TWT_SIZE: Required (u32)
 * This attribute represents the next TWT subfield size.
 */
enum qca_wlan_vendor_attr_twt_resume {
	QCA_WLAN_VENDOR_ATTR_TWT_RESUME_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_TWT_RESUME_NEXT_TWT = 1,
	QCA_WLAN_VENDOR_ATTR_TWT_RESUME_NEXT_TWT_SIZE = 2,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_TWT_RESUME_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_TWT_RESUME_MAX =
	QCA_WLAN_VENDOR_ATTR_TWT_RESUME_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_twt_setup_req_type - Required (u8)
 * Represents the setup type being requested for TWT.
 * @QCA_WLAN_VENDOR_TWT_SETUP_REQUEST: STA is not specifying all the TWT
 * parameters but relying on AP to fill the parameters during the negotiation.
 * @QCA_WLAN_VENDOR_TWT_SETUP_SUGGEST: STA will provide all the suggested
 * values which the AP may accept or AP may provide alternative parameters
 * which the STA may accept.
 * @QCA_WLAN_VENDOR_TWT_SETUP_DEMAND: STA is not willing to accept any
 * alternate parameters than the requested ones.
 */
enum qca_wlan_vendor_twt_setup_req_type {
	QCA_WLAN_VENDOR_TWT_SETUP_REQUEST = 1,
	QCA_WLAN_VENDOR_TWT_SETUP_SUGGEST = 2,
	QCA_WLAN_VENDOR_TWT_SETUP_DEMAND = 3,
};

/**
 * enum qca_wlan_roam_scan_event_type - Type of roam scan event
 *
 * Indicates the type of roam scan event sent by firmware/driver.
 *
 * @QCA_WLAN_ROAM_SCAN_TRIGGER_EVENT: Roam scan trigger event type.
 * @QCA_WLAN_ROAM_SCAN_STOP_EVENT: Roam scan stopped event type.
 */
enum qca_wlan_roam_scan_event_type {
	QCA_WLAN_ROAM_SCAN_TRIGGER_EVENT = 0,
	QCA_WLAN_ROAM_SCAN_STOP_EVENT = 1,
};

/**
 * enum qca_wlan_roam_scan_trigger_reason - Roam scan trigger reason
 *
 * Indicates the reason for triggering roam scan by firmware/driver.
 *
 * @QCA_WLAN_ROAM_SCAN_TRIGGER_REASON_LOW_RSSI: Due to low RSSI of current AP.
 * @QCA_WLAN_ROAM_SCAN_TRIGGER_REASON_HIGH_PER: Due to high packet error rate.
 */
enum qca_wlan_roam_scan_trigger_reason {
	QCA_WLAN_ROAM_SCAN_TRIGGER_REASON_LOW_RSSI = 0,
	QCA_WLAN_ROAM_SCAN_TRIGGER_REASON_HIGH_PER = 1,
};

/**
 * enum qca_wlan_vendor_attr_roam_scan - Vendor subcmd attributes to report
 * roam scan related details from driver/firmware to user space. enum values
 * are used for NL attributes sent with
 * %QCA_NL80211_VENDOR_SUBCMD_ROAM_SCAN_EVENT sub command.
 */
enum qca_wlan_vendor_attr_roam_scan {
	QCA_WLAN_VENDOR_ATTR_ROAM_SCAN_INVALID = 0,
	/* Encapsulates type of roam scan event being reported. enum
	 * qca_wlan_roam_scan_event_type describes the possible range of
	 * values. u32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_ROAM_SCAN_EVENT_TYPE = 1,
	/* Encapsulates reason for triggering roam scan. enum
	 * qca_wlan_roam_scan_trigger_reason describes the possible range of
	 * values. u32 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_ROAM_SCAN_TRIGGER_REASON = 2,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_ROAM_SCAN_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_ROAM_SCAN_MAX =
	QCA_WLAN_VENDOR_ATTR_ROAM_SCAN_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_cfr_method - QCA vendor CFR methods used by
 * attribute QCA_WLAN_VENDOR_ATTR_PEER_CFR_METHOD as part of vendor
 * command QCA_NL80211_VENDOR_SUBCMD_PEER_CFR_CAPTURE_CFG.
 */
enum qca_wlan_vendor_cfr_method {
	/* CFR method using QOS Null frame */
	QCA_WLAN_VENDOR_CFR_METHOD_QOS_NULL = 0,
};

/**
 * enum qca_wlan_vendor_peer_cfr_capture_attr - Used by the vendor command
 * QCA_NL80211_VENDOR_SUBCMD_PEER_CFR_CAPTURE_CFG to configure peer
 * Channel Frequency Response capture parameters and enable periodic CFR
 * capture.
 */
enum qca_wlan_vendor_peer_cfr_capture_attr {
	QCA_WLAN_VENDOR_ATTR_PEER_CFR_CAPTURE_INVALID = 0,
	/* 6-byte MAC address of the peer.
	 * This attribute is mandatory.
	 */
	QCA_WLAN_VENDOR_ATTR_CFR_PEER_MAC_ADDR = 1,
	/* Enable peer CFR Capture, flag attribute.
	 * This attribute is mandatory to enable peer CFR capture.
	 * If this attribute is not present, peer CFR capture is disabled.
	 */
	QCA_WLAN_VENDOR_ATTR_PEER_CFR_ENABLE = 2,
	/* BW of measurement, attribute uses the values in enum nl80211_chan_width
	 * Supported values: 20, 40, 80, 80+80, 160.
	 * Note that all targets may not support all bandwidths.
	 * u8 attribute. This attribute is mandatory if attribute
	 * QCA_WLAN_VENDOR_ATTR_PEER_CFR_ENABLE is used.
	 */
	QCA_WLAN_VENDOR_ATTR_PEER_CFR_BANDWIDTH = 3,
	/* Periodicity of CFR measurement in msec.
	 * Periodicity should be a multiple of Base timer.
	 * Current Base timer value supported is 10 msecs (default).
	 * 0 for one shot capture. u32 attribute.
	 * This attribute is mandatory if attribute
	 * QCA_WLAN_VENDOR_ATTR_PEER_CFR_ENABLE is used.
	 */
	QCA_WLAN_VENDOR_ATTR_PEER_CFR_PERIODICITY = 4,
	/* Method used to capture Channel Frequency Response.
	 * Attribute uses the values defined in enum qca_wlan_vendor_cfr_method.
	 * u8 attribute. This attribute is mandatory if attribute
	 * QCA_WLAN_VENDOR_ATTR_PEER_CFR_ENABLE is used.
	 */
	QCA_WLAN_VENDOR_ATTR_PEER_CFR_METHOD = 5,
	/* Enable periodic CFR capture, flag attribute.
	 * This attribute is mandatory to enable Periodic CFR capture.
	 * If this attribute is not present, periodic CFR capture is disabled.
	 */
	QCA_WLAN_VENDOR_ATTR_PERIODIC_CFR_CAPTURE_ENABLE = 6,

	/* Keep last */
	QCA_WLAN_VENDOR_ATTR_PEER_CFR_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_PEER_CFR_MAX =
	QCA_WLAN_VENDOR_ATTR_PEER_CFR_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_throughput_level - Current throughput level
 *
 * Indicates the current level of throughput calculated by the driver. The
 * driver may choose different thresholds to decide whether the throughput level
 * is low or medium or high based on variety of parameters like physical link
 * capacity of the current connection, the number of packets being dispatched
 * per second, etc. The throughput level events might not be consistent with the
 * actual current throughput value being observed.
 *
 * @QCA_WLAN_THROUGHPUT_LEVEL_LOW: Low level of throughput
 * @QCA_WLAN_THROUGHPUT_LEVEL_MEDIUM: Medium level of throughput
 * @QCA_WLAN_THROUGHPUT_LEVEL_HIGH: High level of throughput
 */
enum qca_wlan_throughput_level {
	QCA_WLAN_THROUGHPUT_LEVEL_LOW = 0,
	QCA_WLAN_THROUGHPUT_LEVEL_MEDIUM = 1,
	QCA_WLAN_THROUGHPUT_LEVEL_HIGH = 2,
};

/**
 * enum qca_wlan_vendor_attr_throughput_change - Vendor subcmd attributes to
 * report throughput changes from the driver to user space. enum values are used
 * for netlink attributes sent with
 * %QCA_NL80211_VENDOR_SUBCMD_THROUGHPUT_CHANGE_EVENT sub command.
 */
enum qca_wlan_vendor_attr_throughput_change {
	QCA_WLAN_VENDOR_ATTR_THROUGHPUT_CHANGE_INVALID = 0,
	/* Indicates the direction of throughput in which the change is being
	 * reported. u8 attribute. Value is 0 for TX and 1 for RX.
	 */
	QCA_WLAN_VENDOR_ATTR_THROUGHPUT_CHANGE_DIRECTION = 1,
	/* Indicates the newly observed throughput level. enum
	 * qca_wlan_throughput_level describes the possible range of values.
	 * u8 attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_THROUGHPUT_CHANGE_THROUGHPUT_LEVEL = 2,
	/* Indicates the driver's guidance on the new value to be set to
	 * kernel's TCP parameter tcp_limit_output_bytes. u32 attribute. The
	 * driver may optionally include this attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_THROUGHPUT_CHANGE_TCP_LIMIT_OUTPUT_BYTES = 3,
	/* Indicates the driver's guidance on the new value to be set to
	 * kernel's TCP parameter tcp_adv_win_scale. s8 attribute. Possible
	 * values are from -31 to 31. The driver may optionally include this
	 * attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_THROUGHPUT_CHANGE_TCP_ADV_WIN_SCALE = 4,
	/* Indicates the driver's guidance on the new value to be set to
	 * kernel's TCP parameter tcp_delack_seg. u32 attribute. The driver may
	 * optionally include this attribute.
	 */
	QCA_WLAN_VENDOR_ATTR_THROUGHPUT_CHANGE_TCP_DELACK_SEG = 5,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_THROUGHPUT_CHANGE_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_THROUGHPUT_CHANGE_MAX =
	QCA_WLAN_VENDOR_ATTR_THROUGHPUT_CHANGE_AFTER_LAST - 1,
};

/**
 * enum qca_coex_config_profiles - This enum defines different types of
 * traffic streams that can be prioritized one over the other during coex
 * scenarios.
 * The types defined in this enum are categorized in the below manner.
 * 0 - 31 values corresponds to WLAN
 * 32 - 63 values corresponds to BT
 * 64 - 95 values corresponds to Zigbee
 * @QCA_WIFI_STA_DISCOVERY: Prioritize discovery frames for WLAN STA
 * @QCA_WIFI_STA_CONNECTION: Prioritize connection frames for WLAN STA
 * @QCA_WIFI_STA_CLASS_3_MGMT: Prioritize class 3 mgmt frames for WLAN STA
 * @QCA_WIFI_STA_DATA : Prioritize data frames for WLAN STA
 * @QCA_WIFI_STA_ALL: Priritize all frames for WLAN STA
 * @QCA_WIFI_SAP_DISCOVERY: Prioritize discovery frames for WLAN SAP
 * @QCA_WIFI_SAP_CONNECTION: Prioritize connection frames for WLAN SAP
 * @QCA_WIFI_SAP_CLASS_3_MGMT: Prioritize class 3 mgmt frames for WLAN SAP
 * @QCA_WIFI_SAP_DATA: Prioritize data frames for WLAN SAP
 * @QCA_WIFI_SAP_ALL: Prioritize all frames for WLAN SAP
 * @QCA_BT_A2DP: Prioritize BT A2DP
 * @QCA_BT_BLE: Prioritize BT BLE
 * @QCA_BT_SCO: Prioritize BT SCO
 * @QCA_ZB_LOW: Prioritize Zigbee Low
 * @QCA_ZB_HIGH: Prioritize Zigbee High
 */
enum qca_coex_config_profiles {
	/* 0 - 31 corresponds to WLAN */
	QCA_WIFI_STA_DISCOVERY = 0,
	QCA_WIFI_STA_CONNECTION = 1,
	QCA_WIFI_STA_CLASS_3_MGMT = 2,
	QCA_WIFI_STA_DATA = 3,
	QCA_WIFI_STA_ALL = 4,
	QCA_WIFI_SAP_DISCOVERY = 5,
	QCA_WIFI_SAP_CONNECTION = 6,
	QCA_WIFI_SAP_CLASS_3_MGMT = 7,
	QCA_WIFI_SAP_DATA = 8,
	QCA_WIFI_SAP_ALL = 9,
	/* 32 - 63 corresponds to BT */
	QCA_BT_A2DP = 32,
	QCA_BT_BLE = 33,
	QCA_BT_SCO = 34,
	/* 64 - 95 corresponds to Zigbee */
	QCA_ZB_LOW = 64,
	QCA_ZB_HIGH = 65
};

/**
 * enum qca_vendor_attr_coex_config - Specifies vendor coex config attributes
 *
 * @QCA_VENDOR_ATTR_COEX_CONFIG_PROFILES: This attribute contains variable
 * length array of 8-bit values from enum qca_coex_config_profiles.
 * FW will prioritize the profiles in the order given in the array encapsulated
 * in this attribute.
 * For example:
 * -----------------------------------------------------------------------
 * |     1       |       34       |        32         |         65       |
 * -----------------------------------------------------------------------
 * If the attribute contains the values defined in above array then it means
 * 1) Wifi STA connection has priority over BT_SCO, BT_A2DP and ZIGBEE HIGH.
 * 2) BT_SCO has priority over BT_A2DP.
 * 3) BT_A2DP has priority over ZIGBEE HIGH.
 * Profiles which are not listed in this array shall not be preferred over the
 * profiles which are listed in the array as a part of this attribute.
 */
enum qca_vendor_attr_coex_config {
	QCA_VENDOR_ATTR_COEX_CONFIG_INVALID = 0,
	QCA_VENDOR_ATTR_COEX_CONFIG_PROFILES = 1,

	/* Keep last */
	QCA_VENDOR_ATTR_COEX_CONFIG_AFTER_LAST,
	QCA_VENDOR_ATTR_COEX_CONFIG_MAX =
	QCA_VENDOR_ATTR_COEX_CONFIG_AFTER_LAST - 1,
};

/**
 * enum qca_wlan_vendor_attr_link_properties - Represent the link properties.
 *
 * @QCA_WLAN_VENDOR_ATTR_LINK_PROPERTIES_MAC_ADDR: MAC address of the peer
 * (STA/AP) for the connected link.
 * @QCA_WLAN_VENDOR_ATTR_LINK_PROPERTIES_STA_FLAGS: Attribute containing a
 * &struct nl80211_sta_flag_update for the respective connected link. MAC
 * address of the peer represented by
 * QCA_WLAN_VENDOR_ATTR_LINK_PROPERTIES_MAC_ADDR.
 */
enum qca_wlan_vendor_attr_link_properties {
	QCA_WLAN_VENDOR_ATTR_LINK_PROPERTIES_INVALID = 0,
	/* 1 - 3 are reserved */
	QCA_WLAN_VENDOR_ATTR_LINK_PROPERTIES_MAC_ADDR = 4,
	QCA_WLAN_VENDOR_ATTR_LINK_PROPERTIES_STA_FLAGS = 5,

	/* Keep last */
	QCA_VENDOR_ATTR_LINK_PROPERTIES_AFTER_LAST,
	QCA_VENDOR_ATTR_LINK_PROPERTIES_MAX =
	QCA_VENDOR_ATTR_LINK_PROPERTIES_AFTER_LAST - 1,
};

#endif /* QCA_VENDOR_H */
