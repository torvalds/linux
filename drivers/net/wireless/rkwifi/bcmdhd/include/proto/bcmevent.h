/*
 * Broadcom Event  protocol definitions
 *
 * $Copyright Open Broadcom Corporation$
 *
 * Dependencies: proto/bcmeth.h
 *
 * $Id: bcmevent.h 419132 2013-08-19 21:33:05Z $
 *
 */



#ifndef _BCMEVENT_H_
#define _BCMEVENT_H_

#ifndef _TYPEDEFS_H_
#include <typedefs.h>
#endif

#include <proto/bcmeth.h>


#include <packed_section_start.h>

#define BCM_EVENT_MSG_VERSION		2	
#define BCM_MSG_IFNAME_MAX		16	


#define WLC_EVENT_MSG_LINK		0x01	
#define WLC_EVENT_MSG_FLUSHTXQ		0x02	
#define WLC_EVENT_MSG_GROUP		0x04	
#define WLC_EVENT_MSG_UNKBSS		0x08	
#define WLC_EVENT_MSG_UNKIF		0x10	




typedef BWL_PRE_PACKED_STRUCT struct
{
	uint16	version;
	uint16	flags;			
	uint32	event_type;		
	uint32	status;			
	uint32	reason;			
	uint32	auth_type;		
	uint32	datalen;		
	struct ether_addr	addr;	
	char	ifname[BCM_MSG_IFNAME_MAX]; 
} BWL_POST_PACKED_STRUCT wl_event_msg_v1_t;


typedef BWL_PRE_PACKED_STRUCT struct
{
	uint16	version;
	uint16	flags;			
	uint32	event_type;		
	uint32	status;			
	uint32	reason;			
	uint32	auth_type;		
	uint32	datalen;		
	struct ether_addr	addr;	
	char	ifname[BCM_MSG_IFNAME_MAX]; 
	uint8	ifidx;			
	uint8	bsscfgidx;		
} BWL_POST_PACKED_STRUCT wl_event_msg_t;


typedef BWL_PRE_PACKED_STRUCT struct bcm_event {
	struct ether_header eth;
	bcmeth_hdr_t		bcm_hdr;
	wl_event_msg_t		event;
	
} BWL_POST_PACKED_STRUCT bcm_event_t;

#define BCM_MSG_LEN	(sizeof(bcm_event_t) - sizeof(bcmeth_hdr_t) - sizeof(struct ether_header))


#define WLC_E_SET_SSID		0	
#define WLC_E_JOIN		1	
#define WLC_E_START		2	
#define WLC_E_AUTH		3	
#define WLC_E_AUTH_IND		4	
#define WLC_E_DEAUTH		5	
#define WLC_E_DEAUTH_IND	6	
#define WLC_E_ASSOC		7	
#define WLC_E_ASSOC_IND		8	
#define WLC_E_REASSOC		9	
#define WLC_E_REASSOC_IND	10	
#define WLC_E_DISASSOC		11	
#define WLC_E_DISASSOC_IND	12	
#define WLC_E_QUIET_START	13	
#define WLC_E_QUIET_END		14	
#define WLC_E_BEACON_RX		15	
#define WLC_E_LINK		16	
#define WLC_E_MIC_ERROR		17	
#define WLC_E_NDIS_LINK		18	
#define WLC_E_ROAM		19	
#define WLC_E_TXFAIL		20	
#define WLC_E_PMKID_CACHE	21	
#define WLC_E_RETROGRADE_TSF	22	
#define WLC_E_PRUNE		23	
#define WLC_E_AUTOAUTH		24	
#define WLC_E_EAPOL_MSG		25	
#define WLC_E_SCAN_COMPLETE	26	
#define WLC_E_ADDTS_IND		27	
#define WLC_E_DELTS_IND		28	
#define WLC_E_BCNSENT_IND	29	
#define WLC_E_BCNRX_MSG		30	
#define WLC_E_BCNLOST_MSG	31	
#define WLC_E_ROAM_PREP		32	
#define WLC_E_PFN_NET_FOUND	33	
#define WLC_E_PFN_NET_LOST	34	
#define WLC_E_RESET_COMPLETE	35
#define WLC_E_JOIN_START	36
#define WLC_E_ROAM_START	37
#define WLC_E_ASSOC_START	38
#define WLC_E_IBSS_ASSOC	39
#define WLC_E_RADIO		40
#define WLC_E_PSM_WATCHDOG	41	
#if defined(BCMCCX) && defined(CCX_SDK)
#define WLC_E_CCX_ASSOC_START	42	
#define WLC_E_CCX_ASSOC_ABORT	43	
#endif 
#define WLC_E_PROBREQ_MSG       44      
#define WLC_E_SCAN_CONFIRM_IND  45
#define WLC_E_PSK_SUP		46	
#define WLC_E_COUNTRY_CODE_CHANGED	47
#define	WLC_E_EXCEEDED_MEDIUM_TIME	48	
#define WLC_E_ICV_ERROR		49	
#define WLC_E_UNICAST_DECODE_ERROR	50	
#define WLC_E_MULTICAST_DECODE_ERROR	51	
#define WLC_E_TRACE		52
#ifdef WLBTAMP
#define WLC_E_BTA_HCI_EVENT	53	
#endif
#define WLC_E_IF		54	
#define WLC_E_P2P_DISC_LISTEN_COMPLETE	55	
#define WLC_E_RSSI		56	
#define WLC_E_PFN_BEST_BATCHING     57  
#define WLC_E_PFN_SCAN_COMPLETE	57	
#define WLC_E_EXTLOG_MSG	58
#define WLC_E_ACTION_FRAME      59 	
#define WLC_E_ACTION_FRAME_COMPLETE	60	
#define WLC_E_PRE_ASSOC_IND	61	
#define WLC_E_PRE_REASSOC_IND	62	
#define WLC_E_CHANNEL_ADOPTED	63
#define WLC_E_AP_STARTED	64	
#define WLC_E_DFS_AP_STOP	65	
#define WLC_E_DFS_AP_RESUME	66	
#define WLC_E_WAI_STA_EVENT	67	
#define WLC_E_WAI_MSG 		68	
#define WLC_E_ESCAN_RESULT 	69	
#define WLC_E_ACTION_FRAME_OFF_CHAN_COMPLETE 	70	
#define WLC_E_PROBRESP_MSG	71	
#define WLC_E_P2P_PROBREQ_MSG	72	
#define WLC_E_DCS_REQUEST	73
#define WLC_E_FIFO_CREDIT_MAP	74	
#define WLC_E_ACTION_FRAME_RX	75	
#define WLC_E_WAKE_EVENT	76	
#define WLC_E_RM_COMPLETE	77	
#define WLC_E_HTSFSYNC		78	
#define WLC_E_OVERLAY_REQ	79	
#define WLC_E_CSA_COMPLETE_IND		80	
#define WLC_E_EXCESS_PM_WAKE_EVENT	81	
#define WLC_E_PFN_BSSID_NET_FOUND   82	
#define WLC_E_PFN_SCAN_NONE		82	
#define WLC_E_PFN_BSSID_NET_LOST    83  

#define WLC_E_PFN_SCAN_ALLGONE		83	
#define WLC_E_GTK_PLUMBED		84
#define WLC_E_ASSOC_IND_NDIS		85	
#define WLC_E_REASSOC_IND_NDIS		86	
#define WLC_E_ASSOC_REQ_IE		87
#define WLC_E_ASSOC_RESP_IE		88
#define WLC_E_ASSOC_RECREATED		89	
#define WLC_E_ACTION_FRAME_RX_NDIS	90	
#define WLC_E_AUTH_REQ			91	
#define WLC_E_TDLS_PEER_EVENT		92	
#define WLC_E_SPEEDY_RECREATE_FAIL	93	
#define WLC_E_NATIVE			94	
#define WLC_E_PKTDELAY_IND		95	
#define WLC_E_AWDL_AW			96	
#define WLC_E_AWDL_ROLE			97	
#define WLC_E_AWDL_EVENT		98	
#ifdef WLNIC
#define WLC_E_NIC_AF_TXS		99	
#define WLC_E_NIC_NIC_REPORT		100	
#endif
#define WLC_E_BEACON_FRAME_RX		101
#define WLC_E_SERVICE_FOUND		102	
#define WLC_E_GAS_FRAGMENT_RX		103	
#define WLC_E_GAS_COMPLETE		104	
#define WLC_E_P2PO_ADD_DEVICE		105	
#define WLC_E_P2PO_DEL_DEVICE		106	
#define WLC_E_WNM_STA_SLEEP		107	
#define WLC_E_NONE			108	
#define WLC_E_PROXD			109	
#define WLC_E_IBSS_COALESCE		110	
#define WLC_E_AWDL_AW_EXT_END		111	
#define WLC_E_AWDL_AW_EXT_START		112	
#define WLC_E_AWDL_AW_START		113	
#define WLC_E_AWDL_RADIO_OFF		114	
#define WLC_E_AWDL_PEER_STATE		115	
#define WLC_E_AWDL_SYNC_STATE_CHANGED	116	
#define WLC_E_AWDL_CHIP_RESET		117	
#define WLC_E_AWDL_INTERLEAVED_SCAN_START		118
#define WLC_E_AWDL_INTERLEAVED_SCAN_STOP		119
#define WLC_E_AWDL_PEER_CACHE_CONTROL			120
#define WLC_E_CSA_START_IND		121
#define WLC_E_CSA_DONE_IND		122
#define WLC_E_CSA_FAILURE_IND		123
#define WLC_E_CCA_CHAN_QUAL		124	
#define WLC_E_LAST			125	



typedef struct {
	uint event;
	const char *name;
} bcmevent_name_t;

extern const bcmevent_name_t	bcmevent_names[];
extern const int		bcmevent_names_size;


#define WLC_E_STATUS_SUCCESS		0	
#define WLC_E_STATUS_FAIL		1	
#define WLC_E_STATUS_TIMEOUT		2	
#define WLC_E_STATUS_NO_NETWORKS	3	
#define WLC_E_STATUS_ABORT		4	
#define WLC_E_STATUS_NO_ACK		5	
#define WLC_E_STATUS_UNSOLICITED	6	
#define WLC_E_STATUS_ATTEMPT		7	
#define WLC_E_STATUS_PARTIAL		8	
#define WLC_E_STATUS_NEWSCAN		9	
#define WLC_E_STATUS_NEWASSOC		10	
#define WLC_E_STATUS_11HQUIET		11	
#define WLC_E_STATUS_SUPPRESS		12	
#define WLC_E_STATUS_NOCHANS		13	
#ifdef BCMCCX
#define WLC_E_STATUS_CCXFASTRM		14	
#endif 
#define WLC_E_STATUS_CS_ABORT		15	
#define WLC_E_STATUS_ERROR		16	


#define WLC_E_REASON_INITIAL_ASSOC	0	
#define WLC_E_REASON_LOW_RSSI		1	
#define WLC_E_REASON_DEAUTH		2	
#define WLC_E_REASON_DISASSOC		3	
#define WLC_E_REASON_BCNS_LOST		4	


#define WLC_E_REASON_FAST_ROAM_FAILED	5	
#define WLC_E_REASON_DIRECTED_ROAM	6	
#define WLC_E_REASON_TSPEC_REJECTED	7	
#define WLC_E_REASON_BETTER_AP		8	
#define WLC_E_REASON_MINTXRATE		9	
#define WLC_E_REASON_TXFAIL		10	

#define WLC_E_REASON_REQUESTED_ROAM 11	



#define WLC_E_PRUNE_ENCR_MISMATCH	1	
#define WLC_E_PRUNE_BCAST_BSSID		2	
#define WLC_E_PRUNE_MAC_DENY		3	
#define WLC_E_PRUNE_MAC_NA		4	
#define WLC_E_PRUNE_REG_PASSV		5	
#define WLC_E_PRUNE_SPCT_MGMT		6	
#define WLC_E_PRUNE_RADAR		7	
#define WLC_E_RSN_MISMATCH		8	
#define WLC_E_PRUNE_NO_COMMON_RATES	9	
#define WLC_E_PRUNE_BASIC_RATES		10	
#ifdef BCMCCX
#define WLC_E_PRUNE_CCXFAST_PREVAP	11	
#endif 
#define WLC_E_PRUNE_CIPHER_NA		12	
#define WLC_E_PRUNE_KNOWN_STA		13	
#ifdef BCMCCX
#define WLC_E_PRUNE_CCXFAST_DROAM	14	
#endif 
#define WLC_E_PRUNE_WDS_PEER		15	
#define WLC_E_PRUNE_QBSS_LOAD		16	
#define WLC_E_PRUNE_HOME_AP		17	
#ifdef BCMCCX
#define WLC_E_PRUNE_AP_BLOCKED		18	
#define WLC_E_PRUNE_NO_DIAG_SUPPORT	19	
#endif 


#define WLC_E_SUP_OTHER			0	
#define WLC_E_SUP_DECRYPT_KEY_DATA	1	
#define WLC_E_SUP_BAD_UCAST_WEP128	2	
#define WLC_E_SUP_BAD_UCAST_WEP40	3	
#define WLC_E_SUP_UNSUP_KEY_LEN		4	
#define WLC_E_SUP_PW_KEY_CIPHER		5	
#define WLC_E_SUP_MSG3_TOO_MANY_IE	6	
#define WLC_E_SUP_MSG3_IE_MISMATCH	7	
#define WLC_E_SUP_NO_INSTALL_FLAG	8	
#define WLC_E_SUP_MSG3_NO_GTK		9	
#define WLC_E_SUP_GRP_KEY_CIPHER	10	
#define WLC_E_SUP_GRP_MSG1_NO_GTK	11	
#define WLC_E_SUP_GTK_DECRYPT_FAIL	12	
#define WLC_E_SUP_SEND_FAIL		13	
#define WLC_E_SUP_DEAUTH		14	
#define WLC_E_SUP_WPA_PSK_TMO		15	



#ifdef WLAWDL
#define WLC_E_AWDL_SCAN_START		1	
#define WLC_E_AWDL_SCAN_DONE		0	

#define WLC_E_AWDL_RX_ACT_FRAME					1
#define WLC_E_AWDL_RX_PRB_RESP					2

#endif
typedef BWL_PRE_PACKED_STRUCT struct wl_event_rx_frame_data {
	uint16	version;
	uint16	channel;	
	int32	rssi;
	uint32	mactime;
	uint32	rate;
} BWL_POST_PACKED_STRUCT wl_event_rx_frame_data_t;

#define BCM_RX_FRAME_DATA_VERSION 1


typedef struct wl_event_data_if {
	uint8 ifidx;		
	uint8 opcode;		
	uint8 reserved;		
	uint8 bssidx;		
	uint8 role;		
} wl_event_data_if_t;


#define WLC_E_IF_ADD		1	
#define WLC_E_IF_DEL		2	
#define WLC_E_IF_CHANGE		3	


#define WLC_E_IF_ROLE_STA		0	
#define WLC_E_IF_ROLE_AP		1	
#define WLC_E_IF_ROLE_WDS		2	
#define WLC_E_IF_ROLE_P2P_GO		3	
#define WLC_E_IF_ROLE_P2P_CLIENT	4	
#ifdef WLBTAMP
#define WLC_E_IF_ROLE_BTA_CREATOR	5	
#define WLC_E_IF_ROLE_BTA_ACCEPTOR	6	
#endif


typedef struct wl_event_data_rssi {
	int32 rssi;
	int32 snr;
	int32 noise;
} wl_event_data_rssi_t;


#define WLC_E_IF_FLAGS_BSSCFG_NOIF	0x1	


#define WLC_E_LINK_BCN_LOSS	1	
#define WLC_E_LINK_DISASSOC	2	
#define WLC_E_LINK_ASSOC_REC	3	
#define WLC_E_LINK_BSSCFG_DIS	4	


#define WLC_E_OVL_DOWNLOAD		0	
#define WLC_E_OVL_UPDATE_IND	1	


#define WLC_E_TDLS_PEER_DISCOVERED		0	
#define WLC_E_TDLS_PEER_CONNECTED		1
#define WLC_E_TDLS_PEER_DISCONNECTED	2

#ifdef WLAWDL

#define WLC_E_AWDL_SCAN_STATUS	0
#define WLC_E_AWDL_RX_ACT_FRAME	1
#define WLC_E_AWDL_RX_PRB_RESP	2
#define WLC_E_AWDL_PHYCAL_STATUS	3
#define WLC_E_AWDL_WOWL_NULLPKT	4
#define WLC_E_AWDL_OOB_AF_STATUS	5


#define WLC_E_AWDL_SCAN_START		1	
#define WLC_E_AWDL_SCAN_DONE		0	
#define WLC_E_AWDL_PHYCAL_START		1	
#define WLC_E_AWDL_PHYCAL_DONE		0	
#endif


typedef BWL_PRE_PACKED_STRUCT struct wl_event_gas {
	uint16	channel;		
	uint8	dialog_token;	
	uint8	fragment_id;	
	uint16	status_code;	
	uint16 	data_len;		
	uint8	data[1];		
} BWL_POST_PACKED_STRUCT wl_event_gas_t;


typedef BWL_PRE_PACKED_STRUCT struct wl_sd_tlv {
	uint16	length;			
	uint8	protocol;		
	uint8	transaction_id;		
	uint8	status_code;		
	uint8	data[1];		
} BWL_POST_PACKED_STRUCT wl_sd_tlv_t;


typedef BWL_PRE_PACKED_STRUCT struct wl_event_sd {
	uint16	channel;		
	uint8	count;			
	wl_sd_tlv_t	tlv[1];		
} BWL_POST_PACKED_STRUCT wl_event_sd_t;


#define WLC_E_PROXD_FOUND	1	
#define WLC_E_PROXD_GONE	2	


typedef BWL_PRE_PACKED_STRUCT struct awdl_aws_event_data {
	uint32	fw_time;			
	struct	ether_addr current_master;	
	uint16	aw_counter;			
	uint8	aw_ext_count;			
	uint8	aw_role;			
	uint8	flags;				
	uint16	aw_chan;
} BWL_POST_PACKED_STRUCT awdl_aws_event_data_t;


#define AWDL_AW_LAST_EXT	0x01


#include <packed_section_end.h>

#endif 
