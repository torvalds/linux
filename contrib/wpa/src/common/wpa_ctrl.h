/*
 * wpa_supplicant/hostapd control interface library
 * Copyright (c) 2004-2017, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WPA_CTRL_H
#define WPA_CTRL_H

#ifdef  __cplusplus
extern "C" {
#endif

/* wpa_supplicant control interface - fixed message prefixes */

/** Interactive request for identity/password/pin */
#define WPA_CTRL_REQ "CTRL-REQ-"

/** Response to identity/password/pin request */
#define WPA_CTRL_RSP "CTRL-RSP-"

/* Event messages with fixed prefix */
/** Authentication completed successfully and data connection enabled */
#define WPA_EVENT_CONNECTED "CTRL-EVENT-CONNECTED "
/** Disconnected, data connection is not available */
#define WPA_EVENT_DISCONNECTED "CTRL-EVENT-DISCONNECTED "
/** Association rejected during connection attempt */
#define WPA_EVENT_ASSOC_REJECT "CTRL-EVENT-ASSOC-REJECT "
/** Authentication rejected during connection attempt */
#define WPA_EVENT_AUTH_REJECT "CTRL-EVENT-AUTH-REJECT "
/** wpa_supplicant is exiting */
#define WPA_EVENT_TERMINATING "CTRL-EVENT-TERMINATING "
/** Password change was completed successfully */
#define WPA_EVENT_PASSWORD_CHANGED "CTRL-EVENT-PASSWORD-CHANGED "
/** EAP-Request/Notification received */
#define WPA_EVENT_EAP_NOTIFICATION "CTRL-EVENT-EAP-NOTIFICATION "
/** EAP authentication started (EAP-Request/Identity received) */
#define WPA_EVENT_EAP_STARTED "CTRL-EVENT-EAP-STARTED "
/** EAP method proposed by the server */
#define WPA_EVENT_EAP_PROPOSED_METHOD "CTRL-EVENT-EAP-PROPOSED-METHOD "
/** EAP method selected */
#define WPA_EVENT_EAP_METHOD "CTRL-EVENT-EAP-METHOD "
/** EAP peer certificate from TLS */
#define WPA_EVENT_EAP_PEER_CERT "CTRL-EVENT-EAP-PEER-CERT "
/** EAP peer certificate alternative subject name component from TLS */
#define WPA_EVENT_EAP_PEER_ALT "CTRL-EVENT-EAP-PEER-ALT "
/** EAP TLS certificate chain validation error */
#define WPA_EVENT_EAP_TLS_CERT_ERROR "CTRL-EVENT-EAP-TLS-CERT-ERROR "
/** EAP status */
#define WPA_EVENT_EAP_STATUS "CTRL-EVENT-EAP-STATUS "
/** Retransmit the previous request packet */
#define WPA_EVENT_EAP_RETRANSMIT "CTRL-EVENT-EAP-RETRANSMIT "
#define WPA_EVENT_EAP_RETRANSMIT2 "CTRL-EVENT-EAP-RETRANSMIT2 "
/** EAP authentication completed successfully */
#define WPA_EVENT_EAP_SUCCESS "CTRL-EVENT-EAP-SUCCESS "
#define WPA_EVENT_EAP_SUCCESS2 "CTRL-EVENT-EAP-SUCCESS2 "
/** EAP authentication failed (EAP-Failure received) */
#define WPA_EVENT_EAP_FAILURE "CTRL-EVENT-EAP-FAILURE "
#define WPA_EVENT_EAP_FAILURE2 "CTRL-EVENT-EAP-FAILURE2 "
/** EAP authentication failed due to no response received */
#define WPA_EVENT_EAP_TIMEOUT_FAILURE "CTRL-EVENT-EAP-TIMEOUT-FAILURE "
#define WPA_EVENT_EAP_TIMEOUT_FAILURE2 "CTRL-EVENT-EAP-TIMEOUT-FAILURE2 "
#define WPA_EVENT_EAP_ERROR_CODE "EAP-ERROR-CODE "
/** Network block temporarily disabled (e.g., due to authentication failure) */
#define WPA_EVENT_TEMP_DISABLED "CTRL-EVENT-SSID-TEMP-DISABLED "
/** Temporarily disabled network block re-enabled */
#define WPA_EVENT_REENABLED "CTRL-EVENT-SSID-REENABLED "
/** New scan started */
#define WPA_EVENT_SCAN_STARTED "CTRL-EVENT-SCAN-STARTED "
/** New scan results available */
#define WPA_EVENT_SCAN_RESULTS "CTRL-EVENT-SCAN-RESULTS "
/** Scan command failed */
#define WPA_EVENT_SCAN_FAILED "CTRL-EVENT-SCAN-FAILED "
/** wpa_supplicant state change */
#define WPA_EVENT_STATE_CHANGE "CTRL-EVENT-STATE-CHANGE "
/** A new BSS entry was added (followed by BSS entry id and BSSID) */
#define WPA_EVENT_BSS_ADDED "CTRL-EVENT-BSS-ADDED "
/** A BSS entry was removed (followed by BSS entry id and BSSID) */
#define WPA_EVENT_BSS_REMOVED "CTRL-EVENT-BSS-REMOVED "
/** No suitable network was found */
#define WPA_EVENT_NETWORK_NOT_FOUND "CTRL-EVENT-NETWORK-NOT-FOUND "
/** Change in the signal level was reported by the driver */
#define WPA_EVENT_SIGNAL_CHANGE "CTRL-EVENT-SIGNAL-CHANGE "
/** Beacon loss reported by the driver */
#define WPA_EVENT_BEACON_LOSS "CTRL-EVENT-BEACON-LOSS "
/** Regulatory domain channel */
#define WPA_EVENT_REGDOM_CHANGE "CTRL-EVENT-REGDOM-CHANGE "
/** Channel switch (followed by freq=<MHz> and other channel parameters) */
#define WPA_EVENT_CHANNEL_SWITCH "CTRL-EVENT-CHANNEL-SWITCH "
/** SAE authentication failed due to unknown password identifier */
#define WPA_EVENT_SAE_UNKNOWN_PASSWORD_IDENTIFIER \
	"CTRL-EVENT-SAE-UNKNOWN-PASSWORD-IDENTIFIER "

/** IP subnet status change notification
 *
 * When using an offloaded roaming mechanism where driver/firmware takes care
 * of roaming and IP subnet validation checks post-roaming, this event can
 * indicate whether IP subnet has changed.
 *
 * The event has a status=<0/1/2> parameter where
 * 0 = unknown
 * 1 = IP subnet unchanged (can continue to use the old IP address)
 * 2 = IP subnet changed (need to get a new IP address)
 */
#define WPA_EVENT_SUBNET_STATUS_UPDATE "CTRL-EVENT-SUBNET-STATUS-UPDATE "

/** RSN IBSS 4-way handshakes completed with specified peer */
#define IBSS_RSN_COMPLETED "IBSS-RSN-COMPLETED "

/** Notification of frequency conflict due to a concurrent operation.
 *
 * The indicated network is disabled and needs to be re-enabled before it can
 * be used again.
 */
#define WPA_EVENT_FREQ_CONFLICT "CTRL-EVENT-FREQ-CONFLICT "
/** Frequency ranges that the driver recommends to avoid */
#define WPA_EVENT_AVOID_FREQ "CTRL-EVENT-AVOID-FREQ "
/** WPS overlap detected in PBC mode */
#define WPS_EVENT_OVERLAP "WPS-OVERLAP-DETECTED "
/** Available WPS AP with active PBC found in scan results */
#define WPS_EVENT_AP_AVAILABLE_PBC "WPS-AP-AVAILABLE-PBC "
/** Available WPS AP with our address as authorized in scan results */
#define WPS_EVENT_AP_AVAILABLE_AUTH "WPS-AP-AVAILABLE-AUTH "
/** Available WPS AP with recently selected PIN registrar found in scan results
 */
#define WPS_EVENT_AP_AVAILABLE_PIN "WPS-AP-AVAILABLE-PIN "
/** Available WPS AP found in scan results */
#define WPS_EVENT_AP_AVAILABLE "WPS-AP-AVAILABLE "
/** A new credential received */
#define WPS_EVENT_CRED_RECEIVED "WPS-CRED-RECEIVED "
/** M2D received */
#define WPS_EVENT_M2D "WPS-M2D "
/** WPS registration failed after M2/M2D */
#define WPS_EVENT_FAIL "WPS-FAIL "
/** WPS registration completed successfully */
#define WPS_EVENT_SUCCESS "WPS-SUCCESS "
/** WPS enrollment attempt timed out and was terminated */
#define WPS_EVENT_TIMEOUT "WPS-TIMEOUT "
/* PBC mode was activated */
#define WPS_EVENT_ACTIVE "WPS-PBC-ACTIVE "
/* PBC mode was disabled */
#define WPS_EVENT_DISABLE "WPS-PBC-DISABLE "

#define WPS_EVENT_ENROLLEE_SEEN "WPS-ENROLLEE-SEEN "

#define WPS_EVENT_OPEN_NETWORK "WPS-OPEN-NETWORK "

/* WPS ER events */
#define WPS_EVENT_ER_AP_ADD "WPS-ER-AP-ADD "
#define WPS_EVENT_ER_AP_REMOVE "WPS-ER-AP-REMOVE "
#define WPS_EVENT_ER_ENROLLEE_ADD "WPS-ER-ENROLLEE-ADD "
#define WPS_EVENT_ER_ENROLLEE_REMOVE "WPS-ER-ENROLLEE-REMOVE "
#define WPS_EVENT_ER_AP_SETTINGS "WPS-ER-AP-SETTINGS "
#define WPS_EVENT_ER_SET_SEL_REG "WPS-ER-AP-SET-SEL-REG "

/* DPP events */
#define DPP_EVENT_AUTH_SUCCESS "DPP-AUTH-SUCCESS "
#define DPP_EVENT_AUTH_INIT_FAILED "DPP-AUTH-INIT-FAILED "
#define DPP_EVENT_NOT_COMPATIBLE "DPP-NOT-COMPATIBLE "
#define DPP_EVENT_RESPONSE_PENDING "DPP-RESPONSE-PENDING "
#define DPP_EVENT_SCAN_PEER_QR_CODE "DPP-SCAN-PEER-QR-CODE "
#define DPP_EVENT_AUTH_DIRECTION "DPP-AUTH-DIRECTION "
#define DPP_EVENT_CONF_RECEIVED "DPP-CONF-RECEIVED "
#define DPP_EVENT_CONF_SENT "DPP-CONF-SENT "
#define DPP_EVENT_CONF_FAILED "DPP-CONF-FAILED "
#define DPP_EVENT_CONFOBJ_AKM "DPP-CONFOBJ-AKM "
#define DPP_EVENT_CONFOBJ_SSID "DPP-CONFOBJ-SSID "
#define DPP_EVENT_CONFOBJ_PASS "DPP-CONFOBJ-PASS "
#define DPP_EVENT_CONFOBJ_PSK "DPP-CONFOBJ-PSK "
#define DPP_EVENT_CONNECTOR "DPP-CONNECTOR "
#define DPP_EVENT_C_SIGN_KEY "DPP-C-SIGN-KEY "
#define DPP_EVENT_NET_ACCESS_KEY "DPP-NET-ACCESS-KEY "
#define DPP_EVENT_MISSING_CONNECTOR "DPP-MISSING-CONNECTOR "
#define DPP_EVENT_NETWORK_ID "DPP-NETWORK-ID "
#define DPP_EVENT_RX "DPP-RX "
#define DPP_EVENT_TX "DPP-TX "
#define DPP_EVENT_TX_STATUS "DPP-TX-STATUS "
#define DPP_EVENT_FAIL "DPP-FAIL "
#define DPP_EVENT_PKEX_T_LIMIT "DPP-PKEX-T-LIMIT "
#define DPP_EVENT_INTRO "DPP-INTRO "
#define DPP_EVENT_CONF_REQ_RX "DPP-CONF-REQ-RX "

/* MESH events */
#define MESH_GROUP_STARTED "MESH-GROUP-STARTED "
#define MESH_GROUP_REMOVED "MESH-GROUP-REMOVED "
#define MESH_PEER_CONNECTED "MESH-PEER-CONNECTED "
#define MESH_PEER_DISCONNECTED "MESH-PEER-DISCONNECTED "
/** Mesh SAE authentication failure. Wrong password suspected. */
#define MESH_SAE_AUTH_FAILURE "MESH-SAE-AUTH-FAILURE "
#define MESH_SAE_AUTH_BLOCKED "MESH-SAE-AUTH-BLOCKED "

/* WMM AC events */
#define WMM_AC_EVENT_TSPEC_ADDED "TSPEC-ADDED "
#define WMM_AC_EVENT_TSPEC_REMOVED "TSPEC-REMOVED "
#define WMM_AC_EVENT_TSPEC_REQ_FAILED "TSPEC-REQ-FAILED "

/** P2P device found */
#define P2P_EVENT_DEVICE_FOUND "P2P-DEVICE-FOUND "

/** P2P device lost */
#define P2P_EVENT_DEVICE_LOST "P2P-DEVICE-LOST "

/** A P2P device requested GO negotiation, but we were not ready to start the
 * negotiation */
#define P2P_EVENT_GO_NEG_REQUEST "P2P-GO-NEG-REQUEST "
#define P2P_EVENT_GO_NEG_SUCCESS "P2P-GO-NEG-SUCCESS "
#define P2P_EVENT_GO_NEG_FAILURE "P2P-GO-NEG-FAILURE "
#define P2P_EVENT_GROUP_FORMATION_SUCCESS "P2P-GROUP-FORMATION-SUCCESS "
#define P2P_EVENT_GROUP_FORMATION_FAILURE "P2P-GROUP-FORMATION-FAILURE "
#define P2P_EVENT_GROUP_STARTED "P2P-GROUP-STARTED "
#define P2P_EVENT_GROUP_REMOVED "P2P-GROUP-REMOVED "
#define P2P_EVENT_CROSS_CONNECT_ENABLE "P2P-CROSS-CONNECT-ENABLE "
#define P2P_EVENT_CROSS_CONNECT_DISABLE "P2P-CROSS-CONNECT-DISABLE "
/* parameters: <peer address> <PIN> */
#define P2P_EVENT_PROV_DISC_SHOW_PIN "P2P-PROV-DISC-SHOW-PIN "
/* parameters: <peer address> */
#define P2P_EVENT_PROV_DISC_ENTER_PIN "P2P-PROV-DISC-ENTER-PIN "
/* parameters: <peer address> */
#define P2P_EVENT_PROV_DISC_PBC_REQ "P2P-PROV-DISC-PBC-REQ "
/* parameters: <peer address> */
#define P2P_EVENT_PROV_DISC_PBC_RESP "P2P-PROV-DISC-PBC-RESP "
/* parameters: <peer address> <status> */
#define P2P_EVENT_PROV_DISC_FAILURE "P2P-PROV-DISC-FAILURE"
/* parameters: <freq> <src addr> <dialog token> <update indicator> <TLVs> */
#define P2P_EVENT_SERV_DISC_REQ "P2P-SERV-DISC-REQ "
/* parameters: <src addr> <update indicator> <TLVs> */
#define P2P_EVENT_SERV_DISC_RESP "P2P-SERV-DISC-RESP "
#define P2P_EVENT_SERV_ASP_RESP "P2P-SERV-ASP-RESP "
#define P2P_EVENT_INVITATION_RECEIVED "P2P-INVITATION-RECEIVED "
#define P2P_EVENT_INVITATION_RESULT "P2P-INVITATION-RESULT "
#define P2P_EVENT_INVITATION_ACCEPTED "P2P-INVITATION-ACCEPTED "
#define P2P_EVENT_FIND_STOPPED "P2P-FIND-STOPPED "
#define P2P_EVENT_PERSISTENT_PSK_FAIL "P2P-PERSISTENT-PSK-FAIL id="
#define P2P_EVENT_PRESENCE_RESPONSE "P2P-PRESENCE-RESPONSE "
#define P2P_EVENT_NFC_BOTH_GO "P2P-NFC-BOTH-GO "
#define P2P_EVENT_NFC_PEER_CLIENT "P2P-NFC-PEER-CLIENT "
#define P2P_EVENT_NFC_WHILE_CLIENT "P2P-NFC-WHILE-CLIENT "
#define P2P_EVENT_FALLBACK_TO_GO_NEG "P2P-FALLBACK-TO-GO-NEG "
#define P2P_EVENT_FALLBACK_TO_GO_NEG_ENABLED "P2P-FALLBACK-TO-GO-NEG-ENABLED "

/* parameters: <PMF enabled> <timeout in ms> <Session Information URL> */
#define ESS_DISASSOC_IMMINENT "ESS-DISASSOC-IMMINENT "
#define P2P_EVENT_REMOVE_AND_REFORM_GROUP "P2P-REMOVE-AND-REFORM-GROUP "

#define P2P_EVENT_P2PS_PROVISION_START "P2PS-PROV-START "
#define P2P_EVENT_P2PS_PROVISION_DONE "P2PS-PROV-DONE "

#define INTERWORKING_AP "INTERWORKING-AP "
#define INTERWORKING_BLACKLISTED "INTERWORKING-BLACKLISTED "
#define INTERWORKING_NO_MATCH "INTERWORKING-NO-MATCH "
#define INTERWORKING_ALREADY_CONNECTED "INTERWORKING-ALREADY-CONNECTED "
#define INTERWORKING_SELECTED "INTERWORKING-SELECTED "

/* Credential block added; parameters: <id> */
#define CRED_ADDED "CRED-ADDED "
/* Credential block modified; parameters: <id> <field> */
#define CRED_MODIFIED "CRED-MODIFIED "
/* Credential block removed; parameters: <id> */
#define CRED_REMOVED "CRED-REMOVED "

#define GAS_RESPONSE_INFO "GAS-RESPONSE-INFO "
/* parameters: <addr> <dialog_token> <freq> */
#define GAS_QUERY_START "GAS-QUERY-START "
/* parameters: <addr> <dialog_token> <freq> <status_code> <result> */
#define GAS_QUERY_DONE "GAS-QUERY-DONE "

/* parameters: <addr> <result> */
#define ANQP_QUERY_DONE "ANQP-QUERY-DONE "

#define RX_ANQP "RX-ANQP "
#define RX_HS20_ANQP "RX-HS20-ANQP "
#define RX_HS20_ANQP_ICON "RX-HS20-ANQP-ICON "
#define RX_HS20_ICON "RX-HS20-ICON "
#define RX_MBO_ANQP "RX-MBO-ANQP "

/* parameters: <Venue Number> <Venue URL> */
#define RX_VENUE_URL "RX-VENUE-URL "

#define HS20_SUBSCRIPTION_REMEDIATION "HS20-SUBSCRIPTION-REMEDIATION "
#define HS20_DEAUTH_IMMINENT_NOTICE "HS20-DEAUTH-IMMINENT-NOTICE "
#define HS20_T_C_ACCEPTANCE "HS20-T-C-ACCEPTANCE "

#define EXT_RADIO_WORK_START "EXT-RADIO-WORK-START "
#define EXT_RADIO_WORK_TIMEOUT "EXT-RADIO-WORK-TIMEOUT "

#define RRM_EVENT_NEIGHBOR_REP_RXED "RRM-NEIGHBOR-REP-RECEIVED "
#define RRM_EVENT_NEIGHBOR_REP_FAILED "RRM-NEIGHBOR-REP-REQUEST-FAILED "

/* hostapd control interface - fixed message prefixes */
#define WPS_EVENT_PIN_NEEDED "WPS-PIN-NEEDED "
#define WPS_EVENT_NEW_AP_SETTINGS "WPS-NEW-AP-SETTINGS "
#define WPS_EVENT_REG_SUCCESS "WPS-REG-SUCCESS "
#define WPS_EVENT_AP_SETUP_LOCKED "WPS-AP-SETUP-LOCKED "
#define WPS_EVENT_AP_SETUP_UNLOCKED "WPS-AP-SETUP-UNLOCKED "
#define WPS_EVENT_AP_PIN_ENABLED "WPS-AP-PIN-ENABLED "
#define WPS_EVENT_AP_PIN_DISABLED "WPS-AP-PIN-DISABLED "
#define AP_STA_CONNECTED "AP-STA-CONNECTED "
#define AP_STA_DISCONNECTED "AP-STA-DISCONNECTED "
#define AP_STA_POSSIBLE_PSK_MISMATCH "AP-STA-POSSIBLE-PSK-MISMATCH "
#define AP_STA_POLL_OK "AP-STA-POLL-OK "

#define AP_REJECTED_MAX_STA "AP-REJECTED-MAX-STA "
#define AP_REJECTED_BLOCKED_STA "AP-REJECTED-BLOCKED-STA "

#define HS20_T_C_FILTERING_ADD "HS20-T-C-FILTERING-ADD "
#define HS20_T_C_FILTERING_REMOVE "HS20-T-C-FILTERING-REMOVE "

#define AP_EVENT_ENABLED "AP-ENABLED "
#define AP_EVENT_DISABLED "AP-DISABLED "

#define INTERFACE_ENABLED "INTERFACE-ENABLED "
#define INTERFACE_DISABLED "INTERFACE-DISABLED "

#define ACS_EVENT_STARTED "ACS-STARTED "
#define ACS_EVENT_COMPLETED "ACS-COMPLETED "
#define ACS_EVENT_FAILED "ACS-FAILED "

#define DFS_EVENT_RADAR_DETECTED "DFS-RADAR-DETECTED "
#define DFS_EVENT_NEW_CHANNEL "DFS-NEW-CHANNEL "
#define DFS_EVENT_CAC_START "DFS-CAC-START "
#define DFS_EVENT_CAC_COMPLETED "DFS-CAC-COMPLETED "
#define DFS_EVENT_NOP_FINISHED "DFS-NOP-FINISHED "
#define DFS_EVENT_PRE_CAC_EXPIRED "DFS-PRE-CAC-EXPIRED "

#define AP_CSA_FINISHED "AP-CSA-FINISHED "

#define P2P_EVENT_LISTEN_OFFLOAD_STOP "P2P-LISTEN-OFFLOAD-STOPPED "
#define P2P_LISTEN_OFFLOAD_STOP_REASON "P2P-LISTEN-OFFLOAD-STOP-REASON "

/* BSS Transition Management Response frame received */
#define BSS_TM_RESP "BSS-TM-RESP "

/* Collocated Interference Request frame received;
 * parameters: <dialog token> <automatic report enabled> <report timeout> */
#define COLOC_INTF_REQ "COLOC-INTF-REQ "
/* Collocated Interference Report frame received;
 * parameters: <STA address> <dialog token> <hexdump of report elements> */
#define COLOC_INTF_REPORT "COLOC-INTF-REPORT "

/* MBO IE with cellular data connection preference received */
#define MBO_CELL_PREFERENCE "MBO-CELL-PREFERENCE "

/* BSS Transition Management Request received with MBO transition reason */
#define MBO_TRANSITION_REASON "MBO-TRANSITION-REASON "

/* parameters: <STA address> <dialog token> <ack=0/1> */
#define BEACON_REQ_TX_STATUS "BEACON-REQ-TX-STATUS "
/* parameters: <STA address> <dialog token> <report mode> <beacon report> */
#define BEACON_RESP_RX "BEACON-RESP-RX "

/* PMKSA cache entry added; parameters: <BSSID> <network_id> */
#define PMKSA_CACHE_ADDED "PMKSA-CACHE-ADDED "
/* PMKSA cache entry removed; parameters: <BSSID> <network_id> */
#define PMKSA_CACHE_REMOVED "PMKSA-CACHE-REMOVED "

/* FILS HLP Container receive; parameters: dst=<addr> src=<addr> frame=<hexdump>
 */
#define FILS_HLP_RX "FILS-HLP-RX "

/* Event to indicate Probe Request frame;
 * parameters: sa=<STA MAC address> signal=<signal> */
#define RX_PROBE_REQUEST "RX-PROBE-REQUEST "

/* Event to indicate station's HT/VHT operation mode change information */
#define STA_OPMODE_MAX_BW_CHANGED "STA-OPMODE-MAX-BW-CHANGED "
#define STA_OPMODE_SMPS_MODE_CHANGED "STA-OPMODE-SMPS-MODE-CHANGED "
#define STA_OPMODE_N_SS_CHANGED "STA-OPMODE-N_SS-CHANGED "

/* New interface addition or removal for 4addr WDS SDA */
#define WDS_STA_INTERFACE_ADDED "WDS-STA-INTERFACE-ADDED "
#define WDS_STA_INTERFACE_REMOVED "WDS-STA-INTERFACE-REMOVED "

/* BSS command information masks */

#define WPA_BSS_MASK_ALL		0xFFFDFFFF
#define WPA_BSS_MASK_ID			BIT(0)
#define WPA_BSS_MASK_BSSID		BIT(1)
#define WPA_BSS_MASK_FREQ		BIT(2)
#define WPA_BSS_MASK_BEACON_INT		BIT(3)
#define WPA_BSS_MASK_CAPABILITIES	BIT(4)
#define WPA_BSS_MASK_QUAL		BIT(5)
#define WPA_BSS_MASK_NOISE		BIT(6)
#define WPA_BSS_MASK_LEVEL		BIT(7)
#define WPA_BSS_MASK_TSF		BIT(8)
#define WPA_BSS_MASK_AGE		BIT(9)
#define WPA_BSS_MASK_IE			BIT(10)
#define WPA_BSS_MASK_FLAGS		BIT(11)
#define WPA_BSS_MASK_SSID		BIT(12)
#define WPA_BSS_MASK_WPS_SCAN		BIT(13)
#define WPA_BSS_MASK_P2P_SCAN		BIT(14)
#define WPA_BSS_MASK_INTERNETW		BIT(15)
#define WPA_BSS_MASK_WIFI_DISPLAY	BIT(16)
#define WPA_BSS_MASK_DELIM		BIT(17)
#define WPA_BSS_MASK_MESH_SCAN		BIT(18)
#define WPA_BSS_MASK_SNR		BIT(19)
#define WPA_BSS_MASK_EST_THROUGHPUT	BIT(20)
#define WPA_BSS_MASK_FST		BIT(21)
#define WPA_BSS_MASK_UPDATE_IDX		BIT(22)
#define WPA_BSS_MASK_BEACON_IE		BIT(23)
#define WPA_BSS_MASK_FILS_INDICATION	BIT(24)


/* VENDOR_ELEM_* frame id values */
enum wpa_vendor_elem_frame {
	VENDOR_ELEM_PROBE_REQ_P2P = 0,
	VENDOR_ELEM_PROBE_RESP_P2P = 1,
	VENDOR_ELEM_PROBE_RESP_P2P_GO = 2,
	VENDOR_ELEM_BEACON_P2P_GO = 3,
	VENDOR_ELEM_P2P_PD_REQ = 4,
	VENDOR_ELEM_P2P_PD_RESP = 5,
	VENDOR_ELEM_P2P_GO_NEG_REQ = 6,
	VENDOR_ELEM_P2P_GO_NEG_RESP = 7,
	VENDOR_ELEM_P2P_GO_NEG_CONF = 8,
	VENDOR_ELEM_P2P_INV_REQ = 9,
	VENDOR_ELEM_P2P_INV_RESP = 10,
	VENDOR_ELEM_P2P_ASSOC_REQ = 11,
	VENDOR_ELEM_P2P_ASSOC_RESP = 12,
	VENDOR_ELEM_ASSOC_REQ = 13,
	VENDOR_ELEM_PROBE_REQ = 14,
	NUM_VENDOR_ELEM_FRAMES
};


/* wpa_supplicant/hostapd control interface access */

/**
 * wpa_ctrl_open - Open a control interface to wpa_supplicant/hostapd
 * @ctrl_path: Path for UNIX domain sockets; ignored if UDP sockets are used.
 * Returns: Pointer to abstract control interface data or %NULL on failure
 *
 * This function is used to open a control interface to wpa_supplicant/hostapd.
 * ctrl_path is usually /var/run/wpa_supplicant or /var/run/hostapd. This path
 * is configured in wpa_supplicant/hostapd and other programs using the control
 * interface need to use matching path configuration.
 */
struct wpa_ctrl * wpa_ctrl_open(const char *ctrl_path);

/**
 * wpa_ctrl_open2 - Open a control interface to wpa_supplicant/hostapd
 * @ctrl_path: Path for UNIX domain sockets; ignored if UDP sockets are used.
 * @cli_path: Path for client UNIX domain sockets; ignored if UDP socket
 *            is used.
 * Returns: Pointer to abstract control interface data or %NULL on failure
 *
 * This function is used to open a control interface to wpa_supplicant/hostapd
 * when the socket path for client need to be specified explicitly. Default
 * ctrl_path is usually /var/run/wpa_supplicant or /var/run/hostapd and client
 * socket path is /tmp.
 */
struct wpa_ctrl * wpa_ctrl_open2(const char *ctrl_path, const char *cli_path);


/**
 * wpa_ctrl_close - Close a control interface to wpa_supplicant/hostapd
 * @ctrl: Control interface data from wpa_ctrl_open()
 *
 * This function is used to close a control interface.
 */
void wpa_ctrl_close(struct wpa_ctrl *ctrl);


/**
 * wpa_ctrl_request - Send a command to wpa_supplicant/hostapd
 * @ctrl: Control interface data from wpa_ctrl_open()
 * @cmd: Command; usually, ASCII text, e.g., "PING"
 * @cmd_len: Length of the cmd in bytes
 * @reply: Buffer for the response
 * @reply_len: Reply buffer length
 * @msg_cb: Callback function for unsolicited messages or %NULL if not used
 * Returns: 0 on success, -1 on error (send or receive failed), -2 on timeout
 *
 * This function is used to send commands to wpa_supplicant/hostapd. Received
 * response will be written to reply and reply_len is set to the actual length
 * of the reply. This function will block for up to 10 seconds while waiting
 * for the reply. If unsolicited messages are received, the blocking time may
 * be longer.
 *
 * msg_cb can be used to register a callback function that will be called for
 * unsolicited messages received while waiting for the command response. These
 * messages may be received if wpa_ctrl_request() is called at the same time as
 * wpa_supplicant/hostapd is sending such a message. This can happen only if
 * the program has used wpa_ctrl_attach() to register itself as a monitor for
 * event messages. Alternatively to msg_cb, programs can register two control
 * interface connections and use one of them for commands and the other one for
 * receiving event messages, in other words, call wpa_ctrl_attach() only for
 * the control interface connection that will be used for event messages.
 */
int wpa_ctrl_request(struct wpa_ctrl *ctrl, const char *cmd, size_t cmd_len,
		     char *reply, size_t *reply_len,
		     void (*msg_cb)(char *msg, size_t len));


/**
 * wpa_ctrl_attach - Register as an event monitor for the control interface
 * @ctrl: Control interface data from wpa_ctrl_open()
 * Returns: 0 on success, -1 on failure, -2 on timeout
 *
 * This function registers the control interface connection as a monitor for
 * wpa_supplicant/hostapd events. After a success wpa_ctrl_attach() call, the
 * control interface connection starts receiving event messages that can be
 * read with wpa_ctrl_recv().
 */
int wpa_ctrl_attach(struct wpa_ctrl *ctrl);


/**
 * wpa_ctrl_detach - Unregister event monitor from the control interface
 * @ctrl: Control interface data from wpa_ctrl_open()
 * Returns: 0 on success, -1 on failure, -2 on timeout
 *
 * This function unregisters the control interface connection as a monitor for
 * wpa_supplicant/hostapd events, i.e., cancels the registration done with
 * wpa_ctrl_attach().
 */
int wpa_ctrl_detach(struct wpa_ctrl *ctrl);


/**
 * wpa_ctrl_recv - Receive a pending control interface message
 * @ctrl: Control interface data from wpa_ctrl_open()
 * @reply: Buffer for the message data
 * @reply_len: Length of the reply buffer
 * Returns: 0 on success, -1 on failure
 *
 * This function will receive a pending control interface message. The received
 * response will be written to reply and reply_len is set to the actual length
 * of the reply.

 * wpa_ctrl_recv() is only used for event messages, i.e., wpa_ctrl_attach()
 * must have been used to register the control interface as an event monitor.
 */
int wpa_ctrl_recv(struct wpa_ctrl *ctrl, char *reply, size_t *reply_len);


/**
 * wpa_ctrl_pending - Check whether there are pending event messages
 * @ctrl: Control interface data from wpa_ctrl_open()
 * Returns: 1 if there are pending messages, 0 if no, or -1 on error
 *
 * This function will check whether there are any pending control interface
 * message available to be received with wpa_ctrl_recv(). wpa_ctrl_pending() is
 * only used for event messages, i.e., wpa_ctrl_attach() must have been used to
 * register the control interface as an event monitor.
 */
int wpa_ctrl_pending(struct wpa_ctrl *ctrl);


/**
 * wpa_ctrl_get_fd - Get file descriptor used by the control interface
 * @ctrl: Control interface data from wpa_ctrl_open()
 * Returns: File descriptor used for the connection
 *
 * This function can be used to get the file descriptor that is used for the
 * control interface connection. The returned value can be used, e.g., with
 * select() while waiting for multiple events.
 *
 * The returned file descriptor must not be used directly for sending or
 * receiving packets; instead, the library functions wpa_ctrl_request() and
 * wpa_ctrl_recv() must be used for this.
 */
int wpa_ctrl_get_fd(struct wpa_ctrl *ctrl);

#ifdef ANDROID
/**
 * wpa_ctrl_cleanup() - Delete any local UNIX domain socket files that
 * may be left over from clients that were previously connected to
 * wpa_supplicant. This keeps these files from being orphaned in the
 * event of crashes that prevented them from being removed as part
 * of the normal orderly shutdown.
 */
void wpa_ctrl_cleanup(void);
#endif /* ANDROID */

#ifdef CONFIG_CTRL_IFACE_UDP
/* Port range for multiple wpa_supplicant instances and multiple VIFs */
#define WPA_CTRL_IFACE_PORT 9877
#define WPA_CTRL_IFACE_PORT_LIMIT 50 /* decremented from start */
#define WPA_GLOBAL_CTRL_IFACE_PORT 9878
#define WPA_GLOBAL_CTRL_IFACE_PORT_LIMIT 20 /* incremented from start */

char * wpa_ctrl_get_remote_ifname(struct wpa_ctrl *ctrl);
#endif /* CONFIG_CTRL_IFACE_UDP */


#ifdef  __cplusplus
}
#endif

#endif /* WPA_CTRL_H */
