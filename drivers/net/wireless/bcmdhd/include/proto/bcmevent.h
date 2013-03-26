/*
 * Broadcom Event  protocol definitions
 *
 * Copyright (C) 1999-2013, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * Dependencies: proto/bcmeth.h
 *
 * $Id: bcmevent.h 386716 2013-02-21 18:16:10Z $
 *
 */

/*
 * Broadcom Ethernet Events protocol defines
 *
 */

#ifndef _BCMEVENT_H_
#define _BCMEVENT_H_

#ifndef _TYPEDEFS_H_
#include <typedefs.h>
#endif
/* #include <ethernet.h> -- TODO: req., excluded to overwhelming coupling (break up ethernet.h) */
#include <proto/bcmeth.h>

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>

#define BCM_EVENT_MSG_VERSION		2	/* wl_event_msg_t struct version */
#define BCM_MSG_IFNAME_MAX		16	/* max length of interface name */

/* flags */
#define WLC_EVENT_MSG_LINK		0x01	/* link is up */
#define WLC_EVENT_MSG_FLUSHTXQ		0x02	/* flush tx queue on MIC error */
#define WLC_EVENT_MSG_GROUP		0x04	/* group MIC error */
#define WLC_EVENT_MSG_UNKBSS		0x08	/* unknown source bsscfg */
#define WLC_EVENT_MSG_UNKIF		0x10	/* unknown source OS i/f */

/* these fields are stored in network order */

/* version 1 */
typedef BWL_PRE_PACKED_STRUCT struct
{
	uint16	version;
	uint16	flags;			/* see flags below */
	uint32	event_type;		/* Message (see below) */
	uint32	status;			/* Status code (see below) */
	uint32	reason;			/* Reason code (if applicable) */
	uint32	auth_type;		/* WLC_E_AUTH */
	uint32	datalen;		/* data buf */
	struct ether_addr	addr;	/* Station address (if applicable) */
	char	ifname[BCM_MSG_IFNAME_MAX]; /* name of the packet incoming interface */
} BWL_POST_PACKED_STRUCT wl_event_msg_v1_t;

/* the current version */
typedef BWL_PRE_PACKED_STRUCT struct
{
	uint16	version;
	uint16	flags;			/* see flags below */
	uint32	event_type;		/* Message (see below) */
	uint32	status;			/* Status code (see below) */
	uint32	reason;			/* Reason code (if applicable) */
	uint32	auth_type;		/* WLC_E_AUTH */
	uint32	datalen;		/* data buf */
	struct ether_addr	addr;	/* Station address (if applicable) */
	char	ifname[BCM_MSG_IFNAME_MAX]; /* name of the packet incoming interface */
	uint8	ifidx;			/* destination OS i/f index */
	uint8	bsscfgidx;		/* source bsscfg index */
} BWL_POST_PACKED_STRUCT wl_event_msg_t;

/* used by driver msgs */
typedef BWL_PRE_PACKED_STRUCT struct bcm_event {
	struct ether_header eth;
	bcmeth_hdr_t		bcm_hdr;
	wl_event_msg_t		event;
	/* data portion follows */
} BWL_POST_PACKED_STRUCT bcm_event_t;

#define BCM_MSG_LEN	(sizeof(bcm_event_t) - sizeof(bcmeth_hdr_t) - sizeof(struct ether_header))

/* Event messages */
#define WLC_E_SET_SSID		0	/* indicates status of set SSID */
#define WLC_E_JOIN		1	/* differentiates join IBSS from found (WLC_E_START) IBSS */
#define WLC_E_START		2	/* STA founded an IBSS or AP started a BSS */
#define WLC_E_AUTH		3	/* 802.11 AUTH request */
#define WLC_E_AUTH_IND		4	/* 802.11 AUTH indication */
#define WLC_E_DEAUTH		5	/* 802.11 DEAUTH request */
#define WLC_E_DEAUTH_IND	6	/* 802.11 DEAUTH indication */
#define WLC_E_ASSOC		7	/* 802.11 ASSOC request */
#define WLC_E_ASSOC_IND		8	/* 802.11 ASSOC indication */
#define WLC_E_REASSOC		9	/* 802.11 REASSOC request */
#define WLC_E_REASSOC_IND	10	/* 802.11 REASSOC indication */
#define WLC_E_DISASSOC		11	/* 802.11 DISASSOC request */
#define WLC_E_DISASSOC_IND	12	/* 802.11 DISASSOC indication */
#define WLC_E_QUIET_START	13	/* 802.11h Quiet period started */
#define WLC_E_QUIET_END		14	/* 802.11h Quiet period ended */
#define WLC_E_BEACON_RX		15	/* BEACONS received/lost indication */
#define WLC_E_LINK		16	/* generic link indication */
#define WLC_E_MIC_ERROR		17	/* TKIP MIC error occurred */
#define WLC_E_NDIS_LINK		18	/* NDIS style link indication */
#define WLC_E_ROAM		19	/* roam attempt occurred: indicate status & reason */
#define WLC_E_TXFAIL		20	/* change in dot11FailedCount (txfail) */
#define WLC_E_PMKID_CACHE	21	/* WPA2 pmkid cache indication */
#define WLC_E_RETROGRADE_TSF	22	/* current AP's TSF value went backward */
#define WLC_E_PRUNE		23	/* AP was pruned from join list for reason */
#define WLC_E_AUTOAUTH		24	/* report AutoAuth table entry match for join attempt */
#define WLC_E_EAPOL_MSG		25	/* Event encapsulating an EAPOL message */
#define WLC_E_SCAN_COMPLETE	26	/* Scan results are ready or scan was aborted */
#define WLC_E_ADDTS_IND		27	/* indicate to host addts fail/success */
#define WLC_E_DELTS_IND		28	/* indicate to host delts fail/success */
#define WLC_E_BCNSENT_IND	29	/* indicate to host of beacon transmit */
#define WLC_E_BCNRX_MSG		30	/* Send the received beacon up to the host */
#define WLC_E_BCNLOST_MSG	31	/* indicate to host loss of beacon */
#define WLC_E_ROAM_PREP		32	/* before attempting to roam */
#define WLC_E_PFN_NET_FOUND	33	/* PFN network found event */
#define WLC_E_PFN_NET_LOST	34	/* PFN network lost event */
#define WLC_E_RESET_COMPLETE	35
#define WLC_E_JOIN_START	36
#define WLC_E_ROAM_START	37
#define WLC_E_ASSOC_START	38
#define WLC_E_IBSS_ASSOC	39
#define WLC_E_RADIO		40
#define WLC_E_PSM_WATCHDOG	41	/* PSM microcode watchdog fired */
#define WLC_E_PROBREQ_MSG       44      /* probe request received */
#define WLC_E_SCAN_CONFIRM_IND  45
#define WLC_E_PSK_SUP		46	/* WPA Handshake fail */
#define WLC_E_COUNTRY_CODE_CHANGED	47
#define	WLC_E_EXCEEDED_MEDIUM_TIME	48	/* WMMAC excedded medium time */
#define WLC_E_ICV_ERROR		49	/* WEP ICV error occurred */
#define WLC_E_UNICAST_DECODE_ERROR	50	/* Unsupported unicast encrypted frame */
#define WLC_E_MULTICAST_DECODE_ERROR	51	/* Unsupported multicast encrypted frame */
#define WLC_E_TRACE		52
#define WLC_E_IF		54	/* I/F change (for dongle host notification) */
#define WLC_E_P2P_DISC_LISTEN_COMPLETE	55	/* listen state expires */
#define WLC_E_RSSI		56	/* indicate RSSI change based on configured levels */
#define WLC_E_PFN_SCAN_COMPLETE	57	/* PFN completed scan of network list */
#define WLC_E_EXTLOG_MSG	58
#define WLC_E_ACTION_FRAME      59 	/* Action frame Rx */
#define WLC_E_ACTION_FRAME_COMPLETE	60	/* Action frame Tx complete */
#define WLC_E_PRE_ASSOC_IND	61	/* assoc request received */
#define WLC_E_PRE_REASSOC_IND	62	/* re-assoc request received */
#define WLC_E_CHANNEL_ADOPTED	63
#define WLC_E_AP_STARTED	64	/* AP started */
#define WLC_E_DFS_AP_STOP	65	/* AP stopped due to DFS */
#define WLC_E_DFS_AP_RESUME	66	/* AP resumed due to DFS */
#define WLC_E_WAI_STA_EVENT	67	/* WAI stations event */
#define WLC_E_WAI_MSG 		68	/* event encapsulating an WAI message */
#define WLC_E_ESCAN_RESULT 	69	/* escan result event */
#define WLC_E_ACTION_FRAME_OFF_CHAN_COMPLETE 	70	/* action frame off channel complete */
#define WLC_E_PROBRESP_MSG	71	/* probe response received */
#define WLC_E_P2P_PROBREQ_MSG	72	/* P2P Probe request received */
#define WLC_E_DCS_REQUEST	73
#define WLC_E_FIFO_CREDIT_MAP	74	/* credits for D11 FIFOs. [AC0,AC1,AC2,AC3,BC_MC,ATIM] */
#define WLC_E_ACTION_FRAME_RX	75	/* Received action frame event WITH
					 * wl_event_rx_frame_data_t header
					 */
#define WLC_E_WAKE_EVENT	76	/* Wake Event timer fired, used for wake WLAN test mode */
#define WLC_E_RM_COMPLETE	77	/* Radio measurement complete */
#define WLC_E_HTSFSYNC		78	/* Synchronize TSF with the host */
#define WLC_E_OVERLAY_REQ	79	/* request an overlay IOCTL/iovar from the host */
#define WLC_E_CSA_COMPLETE_IND		80	/* 802.11 CHANNEL SWITCH ACTION completed */
#define WLC_E_EXCESS_PM_WAKE_EVENT	81	/* excess PM Wake Event to inform host  */
#define WLC_E_PFN_SCAN_NONE		82	/* no PFN networks around */
#define WLC_E_PFN_SCAN_ALLGONE		83	/* last found PFN network gets lost */
#define WLC_E_GTK_PLUMBED		84
#define WLC_E_ASSOC_IND_NDIS		85	/* 802.11 ASSOC indication for NDIS only */
#define WLC_E_REASSOC_IND_NDIS		86	/* 802.11 REASSOC indication for NDIS only */
#define WLC_E_ASSOC_REQ_IE		87
#define WLC_E_ASSOC_RESP_IE		88
#define WLC_E_ASSOC_RECREATED		89	/* association recreated on resume */
#define WLC_E_ACTION_FRAME_RX_NDIS	90	/* rx action frame event for NDIS only */
#define WLC_E_AUTH_REQ			91	/* authentication request received */
#define WLC_E_TDLS_PEER_EVENT		92	/* discovered peer, connected/disconnected peer */
#define WLC_E_SPEEDY_RECREATE_FAIL	93	/* fast assoc recreation failed */
#define WLC_E_NATIVE			94	/* port-specific event and payload (e.g. NDIS) */
#define WLC_E_PKTDELAY_IND		95	/* event for tx pkt delay suddently jump */
#define WLC_E_AWDL_AW			96	/* AWDL AW period starts */
#define WLC_E_AWDL_ROLE			97	/* AWDL Master/Slave/NE master role event */
#define WLC_E_AWDL_EVENT		98	/* Generic AWDL event */
#ifdef WLNIC
#define WLC_E_NIC_AF_TXS		99	/* NIC AF txstatus */
#define WLC_E_NIC_NIC_REPORT		100	/* NIC period report */
#endif
#define WLC_E_BEACON_FRAME_RX		101
#define WLC_E_SERVICE_FOUND		102	/* desired service found */
#define WLC_E_GAS_FRAGMENT_RX		103	/* GAS fragment received */
#define WLC_E_GAS_COMPLETE		104	/* GAS sessions all complete */
#define WLC_E_P2PO_ADD_DEVICE		105	/* New device found by p2p offload */
#define WLC_E_P2PO_DEL_DEVICE		106	/* device has been removed by p2p offload */
#define WLC_E_WNM_STA_SLEEP		107	/* WNM event to notify STA enter sleep mode */
#define WLC_E_NONE			108	/* event removed, free to be reused */
#define WLC_E_PROXD			109	/* Proximity Detection event */
#define WLC_E_IBSS_COALESCE		110	/* IBSS Coalescing */
#define WLC_E_AWDL_AW_EXT_END		111	/* AWDL extended period ends */
#define WLC_E_AWDL_AW_EXT_START		112	/* SWDL AW extension start */
#define WLC_E_AWDL_AW_START		113	/* AWDL start Event to inform host  */
#define WLC_E_AWDL_RADIO_OFF		114	/* Radio Off  */
#define WLC_E_AWDL_PEER_STATE		115	/* AWDL peer state open/close */
#define WLC_E_AWDL_SYNC_STATE_CHANGED	116	/* AWDL sync role changed */
#define WLC_E_AWDL_CHIP_RESET		117	/* infroms the interface of a chip rest */
#define WLC_E_AWDL_INTERLEAVED_SCAN_START		118
#define WLC_E_AWDL_INTERLEAVED_SCAN_STOP		119
#define WLC_E_AWDL_PEER_CACHE_CONTROL			120
#define WLC_E_CSA_START_IND		121
#define WLC_E_CSA_DONE_IND		122
#define WLC_E_CSA_FAILURE_IND		123
#define WLC_E_CCA_CHAN_QUAL		124	/* CCA based channel quality report */
#define WLC_E_LAST			125	/* highest val + 1 for range checking */


/* Table of event name strings for UIs and debugging dumps */
typedef struct {
	uint event;
	const char *name;
} bcmevent_name_t;

extern const bcmevent_name_t	bcmevent_names[];
extern const int		bcmevent_names_size;

/* Event status codes */
#define WLC_E_STATUS_SUCCESS		0	/* operation was successful */
#define WLC_E_STATUS_FAIL		1	/* operation failed */
#define WLC_E_STATUS_TIMEOUT		2	/* operation timed out */
#define WLC_E_STATUS_NO_NETWORKS	3	/* failed due to no matching network found */
#define WLC_E_STATUS_ABORT		4	/* operation was aborted */
#define WLC_E_STATUS_NO_ACK		5	/* protocol failure: packet not ack'd */
#define WLC_E_STATUS_UNSOLICITED	6	/* AUTH or ASSOC packet was unsolicited */
#define WLC_E_STATUS_ATTEMPT		7	/* attempt to assoc to an auto auth configuration */
#define WLC_E_STATUS_PARTIAL		8	/* scan results are incomplete */
#define WLC_E_STATUS_NEWSCAN		9	/* scan aborted by another scan */
#define WLC_E_STATUS_NEWASSOC		10	/* scan aborted due to assoc in progress */
#define WLC_E_STATUS_11HQUIET		11	/* 802.11h quiet period started */
#define WLC_E_STATUS_SUPPRESS		12	/* user disabled scanning (WLC_SET_SCANSUPPRESS) */
#define WLC_E_STATUS_NOCHANS		13	/* no allowable channels to scan */
#define WLC_E_STATUS_CS_ABORT		15	/* abort channel select */
#define WLC_E_STATUS_ERROR		16	/* request failed due to error */

/* roam reason codes */
#define WLC_E_REASON_INITIAL_ASSOC	0	/* initial assoc */
#define WLC_E_REASON_LOW_RSSI		1	/* roamed due to low RSSI */
#define WLC_E_REASON_DEAUTH		2	/* roamed due to DEAUTH indication */
#define WLC_E_REASON_DISASSOC		3	/* roamed due to DISASSOC indication */
#define WLC_E_REASON_BCNS_LOST		4	/* roamed due to lost beacons */

/* Roam codes used primarily by CCX */
#define WLC_E_REASON_FAST_ROAM_FAILED	5	/* roamed due to fast roam failure */
#define WLC_E_REASON_DIRECTED_ROAM	6	/* roamed due to request by AP */
#define WLC_E_REASON_TSPEC_REJECTED	7	/* roamed due to TSPEC rejection */
#define WLC_E_REASON_BETTER_AP		8	/* roamed due to finding better AP */
#define WLC_E_REASON_MINTXRATE		9	/* roamed because at mintxrate for too long */
#define WLC_E_REASON_TXFAIL		10	/* We can hear AP, but AP can't hear us */

#define WLC_E_REASON_REQUESTED_ROAM 11	/* roamed due to BSS Mgmt Transition request by AP */


/* prune reason codes */
#define WLC_E_PRUNE_ENCR_MISMATCH	1	/* encryption mismatch */
#define WLC_E_PRUNE_BCAST_BSSID		2	/* AP uses a broadcast BSSID */
#define WLC_E_PRUNE_MAC_DENY		3	/* STA's MAC addr is in AP's MAC deny list */
#define WLC_E_PRUNE_MAC_NA		4	/* STA's MAC addr is not in AP's MAC allow list */
#define WLC_E_PRUNE_REG_PASSV		5	/* AP not allowed due to regulatory restriction */
#define WLC_E_PRUNE_SPCT_MGMT		6	/* AP does not support STA locale spectrum mgmt */
#define WLC_E_PRUNE_RADAR		7	/* AP is on a radar channel of STA locale */
#define WLC_E_RSN_MISMATCH		8	/* STA does not support AP's RSN */
#define WLC_E_PRUNE_NO_COMMON_RATES	9	/* No rates in common with AP */
#define WLC_E_PRUNE_BASIC_RATES		10	/* STA does not support all basic rates of BSS */
#define WLC_E_PRUNE_CIPHER_NA		12	/* BSS's cipher not supported */
#define WLC_E_PRUNE_KNOWN_STA		13	/* AP is already known to us as a STA */
#define WLC_E_PRUNE_WDS_PEER		15	/* AP is already known to us as a WDS peer */
#define WLC_E_PRUNE_QBSS_LOAD		16	/* QBSS LOAD - AAC is too low */
#define WLC_E_PRUNE_HOME_AP		17	/* prune home AP */

/* WPA failure reason codes carried in the WLC_E_PSK_SUP event */
#define WLC_E_SUP_OTHER			0	/* Other reason */
#define WLC_E_SUP_DECRYPT_KEY_DATA	1	/* Decryption of key data failed */
#define WLC_E_SUP_BAD_UCAST_WEP128	2	/* Illegal use of ucast WEP128 */
#define WLC_E_SUP_BAD_UCAST_WEP40	3	/* Illegal use of ucast WEP40 */
#define WLC_E_SUP_UNSUP_KEY_LEN		4	/* Unsupported key length */
#define WLC_E_SUP_PW_KEY_CIPHER		5	/* Unicast cipher mismatch in pairwise key */
#define WLC_E_SUP_MSG3_TOO_MANY_IE	6	/* WPA IE contains > 1 RSN IE in key msg 3 */
#define WLC_E_SUP_MSG3_IE_MISMATCH	7	/* WPA IE mismatch in key message 3 */
#define WLC_E_SUP_NO_INSTALL_FLAG	8	/* INSTALL flag unset in 4-way msg */
#define WLC_E_SUP_MSG3_NO_GTK		9	/* encapsulated GTK missing from msg 3 */
#define WLC_E_SUP_GRP_KEY_CIPHER	10	/* Multicast cipher mismatch in group key */
#define WLC_E_SUP_GRP_MSG1_NO_GTK	11	/* encapsulated GTK missing from group msg 1 */
#define WLC_E_SUP_GTK_DECRYPT_FAIL	12	/* GTK decrypt failure */
#define WLC_E_SUP_SEND_FAIL		13	/* message send failure */
#define WLC_E_SUP_DEAUTH		14	/* received FC_DEAUTH */
#define WLC_E_SUP_WPA_PSK_TMO		15	/* WPA PSK 4-way handshake timeout */

/* Event data for events that include frames received over the air */
/* WLC_E_PROBRESP_MSG
 * WLC_E_P2P_PROBREQ_MSG
 * WLC_E_ACTION_FRAME_RX
 */
#ifdef WLAWDL
#define WLC_E_AWDL_SCAN_START		1	/* Scan start indication to host */
#define WLC_E_AWDL_SCAN_DONE		0	/* Scan Done indication to host */

#define WLC_E_AWDL_RX_ACT_FRAME					1
#define WLC_E_AWDL_RX_PRB_RESP					2

#endif
typedef BWL_PRE_PACKED_STRUCT struct wl_event_rx_frame_data {
	uint16	version;
	uint16	channel;	/* Matches chanspec_t format from bcmwifi_channels.h */
	int32	rssi;
	uint32	mactime;
	uint32	rate;
} BWL_POST_PACKED_STRUCT wl_event_rx_frame_data_t;

#define BCM_RX_FRAME_DATA_VERSION 1

/* WLC_E_IF event data */
typedef struct wl_event_data_if {
	uint8 ifidx;		/* RTE virtual device index (for dongle) */
	uint8 opcode;		/* see I/F opcode */
	uint8 reserved;		/* bit mask (WLC_E_IF_FLAGS_XXX ) */
	uint8 bssidx;		/* bsscfg index */
	uint8 role;		/* see I/F role */
} wl_event_data_if_t;

/* opcode in WLC_E_IF event */
#define WLC_E_IF_ADD		1	/* bsscfg add */
#define WLC_E_IF_DEL		2	/* bsscfg delete */
#define WLC_E_IF_CHANGE		3	/* bsscfg role change */

/* I/F role code in WLC_E_IF event */
#define WLC_E_IF_ROLE_STA		0	/* Infra STA */
#define WLC_E_IF_ROLE_AP		1	/* Access Point */
#define WLC_E_IF_ROLE_WDS		2	/* WDS link */
#define WLC_E_IF_ROLE_P2P_GO		3	/* P2P Group Owner */
#define WLC_E_IF_ROLE_P2P_CLIENT	4	/* P2P Client */

/* WLC_E_RSSI event data */
typedef struct wl_event_data_rssi {
	int32 rssi;
	int32 snr;
	int32 noise;
} wl_event_data_rssi_t;

/* WLC_E_IF flag */
#define WLC_E_IF_FLAGS_BSSCFG_NOIF	0x1	/* no host I/F creation needed */

/* Reason codes for LINK */
#define WLC_E_LINK_BCN_LOSS	1	/* Link down because of beacon loss */
#define WLC_E_LINK_DISASSOC	2	/* Link down because of disassoc */
#define WLC_E_LINK_ASSOC_REC	3	/* Link down because assoc recreate failed */
#define WLC_E_LINK_BSSCFG_DIS	4	/* Link down due to bsscfg down */

/* reason codes for WLC_E_OVERLAY_REQ event */
#define WLC_E_OVL_DOWNLOAD		0	/* overlay download request */
#define WLC_E_OVL_UPDATE_IND	1	/* device indication of host overlay update */

/* reason codes for WLC_E_TDLS_PEER_EVENT event */
#define WLC_E_TDLS_PEER_DISCOVERED		0	/* peer is ready to establish TDLS */
#define WLC_E_TDLS_PEER_CONNECTED		1
#define WLC_E_TDLS_PEER_DISCONNECTED	2

#ifdef WLAWDL
/* WLC_E_AWDL_EVENT subtypes */
#define WLC_E_AWDL_SCAN_STATUS	0
#define WLC_E_AWDL_RX_ACT_FRAME	1
#define WLC_E_AWDL_RX_PRB_RESP	2
#define WLC_E_AWDL_PHYCAL_STATUS	3
#define WLC_E_AWDL_WOWL_NULLPKT	4
#define WLC_E_AWDL_OOB_AF_STATUS	5

/* WLC_E_AWDL_SCAN_STATUS status values */
#define WLC_E_AWDL_SCAN_START		1	/* Scan start indication to host */
#define WLC_E_AWDL_SCAN_DONE		0	/* Scan Done indication to host */
#define WLC_E_AWDL_PHYCAL_START		1	/* Phy calibration start indication to host */
#define WLC_E_AWDL_PHYCAL_DONE		0	/* Phy calibration done indication to host */
#endif

/* GAS event data */
typedef BWL_PRE_PACKED_STRUCT struct wl_event_gas {
	uint16	channel;		/* channel of GAS protocol */
	uint8	dialog_token;	/* GAS dialog token */
	uint8	fragment_id;	/* fragment id */
	uint16	status_code;	/* status code on GAS completion */
	uint16 	data_len;		/* length of data to follow */
	uint8	data[1];		/* variable length specified by data_len */
} BWL_POST_PACKED_STRUCT wl_event_gas_t;

/* service discovery TLV */
typedef BWL_PRE_PACKED_STRUCT struct wl_sd_tlv {
	uint16	length;			/* length of response_data */
	uint8	protocol;		/* service protocol type */
	uint8	transaction_id;		/* service transaction id */
	uint8	status_code;		/* status code */
	uint8	data[1];		/* response data */
} BWL_POST_PACKED_STRUCT wl_sd_tlv_t;

/* service discovery event data */
typedef BWL_PRE_PACKED_STRUCT struct wl_event_sd {
	uint16	channel;		/* channel */
	uint8	count;			/* number of tlvs */
	wl_sd_tlv_t	tlv[1];		/* service discovery TLV */
} BWL_POST_PACKED_STRUCT wl_event_sd_t;

/* Reason codes for WLC_E_PROXD */
#define WLC_E_PROXD_FOUND	1	/* Found a proximity device */
#define WLC_E_PROXD_GONE	2	/* Lost a proximity device */

/* WLC_E_AWDL_AW event data */
typedef BWL_PRE_PACKED_STRUCT struct awdl_aws_event_data {
	uint32	fw_time;			/* firmware PMU time */
	struct	ether_addr current_master;	/* Current master Mac addr */
	uint16	aw_counter;			/* AW seq# */
	uint8	aw_ext_count;			/* AW extension count */
	uint8	aw_role;			/* AW role */
	uint8	flags;				/* AW event flag */
	uint16	aw_chan;
} BWL_POST_PACKED_STRUCT awdl_aws_event_data_t;

/* For awdl_aws_event_data_t.flags */
#define AWDL_AW_LAST_EXT	0x01

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#endif /* _BCMEVENT_H_ */
