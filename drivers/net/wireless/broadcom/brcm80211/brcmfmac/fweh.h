// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2012 Broadcom Corporation
 */


#ifndef FWEH_H_
#define FWEH_H_

#include <asm/unaligned.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/if.h>

/* formward declarations */
struct brcmf_pub;
struct brcmf_if;
struct brcmf_cfg80211_info;

/* list of firmware events */
#define BRCMF_FWEH_EVENT_ENUM_DEFLIST \
	BRCMF_ENUM_DEF(SET_SSID, 0) \
	BRCMF_ENUM_DEF(JOIN, 1) \
	BRCMF_ENUM_DEF(START, 2) \
	BRCMF_ENUM_DEF(AUTH, 3) \
	BRCMF_ENUM_DEF(AUTH_IND, 4) \
	BRCMF_ENUM_DEF(DEAUTH, 5) \
	BRCMF_ENUM_DEF(DEAUTH_IND, 6) \
	BRCMF_ENUM_DEF(ASSOC, 7) \
	BRCMF_ENUM_DEF(ASSOC_IND, 8) \
	BRCMF_ENUM_DEF(REASSOC, 9) \
	BRCMF_ENUM_DEF(REASSOC_IND, 10) \
	BRCMF_ENUM_DEF(DISASSOC, 11) \
	BRCMF_ENUM_DEF(DISASSOC_IND, 12) \
	BRCMF_ENUM_DEF(QUIET_START, 13) \
	BRCMF_ENUM_DEF(QUIET_END, 14) \
	BRCMF_ENUM_DEF(BEACON_RX, 15) \
	BRCMF_ENUM_DEF(LINK, 16) \
	BRCMF_ENUM_DEF(MIC_ERROR, 17) \
	BRCMF_ENUM_DEF(NDIS_LINK, 18) \
	BRCMF_ENUM_DEF(ROAM, 19) \
	BRCMF_ENUM_DEF(TXFAIL, 20) \
	BRCMF_ENUM_DEF(PMKID_CACHE, 21) \
	BRCMF_ENUM_DEF(RETROGRADE_TSF, 22) \
	BRCMF_ENUM_DEF(PRUNE, 23) \
	BRCMF_ENUM_DEF(AUTOAUTH, 24) \
	BRCMF_ENUM_DEF(EAPOL_MSG, 25) \
	BRCMF_ENUM_DEF(SCAN_COMPLETE, 26) \
	BRCMF_ENUM_DEF(ADDTS_IND, 27) \
	BRCMF_ENUM_DEF(DELTS_IND, 28) \
	BRCMF_ENUM_DEF(BCNSENT_IND, 29) \
	BRCMF_ENUM_DEF(BCNRX_MSG, 30) \
	BRCMF_ENUM_DEF(BCNLOST_MSG, 31) \
	BRCMF_ENUM_DEF(ROAM_PREP, 32) \
	BRCMF_ENUM_DEF(PFN_NET_FOUND, 33) \
	BRCMF_ENUM_DEF(PFN_NET_LOST, 34) \
	BRCMF_ENUM_DEF(RESET_COMPLETE, 35) \
	BRCMF_ENUM_DEF(JOIN_START, 36) \
	BRCMF_ENUM_DEF(ROAM_START, 37) \
	BRCMF_ENUM_DEF(ASSOC_START, 38) \
	BRCMF_ENUM_DEF(IBSS_ASSOC, 39) \
	BRCMF_ENUM_DEF(RADIO, 40) \
	BRCMF_ENUM_DEF(PSM_WATCHDOG, 41) \
	BRCMF_ENUM_DEF(PROBREQ_MSG, 44) \
	BRCMF_ENUM_DEF(SCAN_CONFIRM_IND, 45) \
	BRCMF_ENUM_DEF(PSK_SUP, 46) \
	BRCMF_ENUM_DEF(COUNTRY_CODE_CHANGED, 47) \
	BRCMF_ENUM_DEF(EXCEEDED_MEDIUM_TIME, 48) \
	BRCMF_ENUM_DEF(ICV_ERROR, 49) \
	BRCMF_ENUM_DEF(UNICAST_DECODE_ERROR, 50) \
	BRCMF_ENUM_DEF(MULTICAST_DECODE_ERROR, 51) \
	BRCMF_ENUM_DEF(TRACE, 52) \
	BRCMF_ENUM_DEF(IF, 54) \
	BRCMF_ENUM_DEF(P2P_DISC_LISTEN_COMPLETE, 55) \
	BRCMF_ENUM_DEF(RSSI, 56) \
	BRCMF_ENUM_DEF(EXTLOG_MSG, 58) \
	BRCMF_ENUM_DEF(ACTION_FRAME, 59) \
	BRCMF_ENUM_DEF(ACTION_FRAME_COMPLETE, 60) \
	BRCMF_ENUM_DEF(PRE_ASSOC_IND, 61) \
	BRCMF_ENUM_DEF(PRE_REASSOC_IND, 62) \
	BRCMF_ENUM_DEF(CHANNEL_ADOPTED, 63) \
	BRCMF_ENUM_DEF(AP_STARTED, 64) \
	BRCMF_ENUM_DEF(DFS_AP_STOP, 65) \
	BRCMF_ENUM_DEF(DFS_AP_RESUME, 66) \
	BRCMF_ENUM_DEF(ESCAN_RESULT, 69) \
	BRCMF_ENUM_DEF(ACTION_FRAME_OFF_CHAN_COMPLETE, 70) \
	BRCMF_ENUM_DEF(PROBERESP_MSG, 71) \
	BRCMF_ENUM_DEF(P2P_PROBEREQ_MSG, 72) \
	BRCMF_ENUM_DEF(DCS_REQUEST, 73) \
	BRCMF_ENUM_DEF(FIFO_CREDIT_MAP, 74) \
	BRCMF_ENUM_DEF(ACTION_FRAME_RX, 75) \
	BRCMF_ENUM_DEF(TDLS_PEER_EVENT, 92) \
	BRCMF_ENUM_DEF(BCMC_CREDIT_SUPPORT, 127)

#define BRCMF_ENUM_DEF(id, val) \
	BRCMF_E_##id = (val),

/* firmware event codes sent by the dongle */
enum brcmf_fweh_event_code {
	BRCMF_FWEH_EVENT_ENUM_DEFLIST
	/* this determines event mask length which must match
	 * minimum length check in device firmware so it is
	 * hard-coded here.
	 */
	BRCMF_E_LAST = 139
};
#undef BRCMF_ENUM_DEF

#define BRCMF_EVENTING_MASK_LEN		DIV_ROUND_UP(BRCMF_E_LAST, 8)

/* flags field values in struct brcmf_event_msg */
#define BRCMF_EVENT_MSG_LINK		0x01
#define BRCMF_EVENT_MSG_FLUSHTXQ	0x02
#define BRCMF_EVENT_MSG_GROUP		0x04

/* status field values in struct brcmf_event_msg */
#define BRCMF_E_STATUS_SUCCESS			0
#define BRCMF_E_STATUS_FAIL			1
#define BRCMF_E_STATUS_TIMEOUT			2
#define BRCMF_E_STATUS_NO_NETWORKS		3
#define BRCMF_E_STATUS_ABORT			4
#define BRCMF_E_STATUS_NO_ACK			5
#define BRCMF_E_STATUS_UNSOLICITED		6
#define BRCMF_E_STATUS_ATTEMPT			7
#define BRCMF_E_STATUS_PARTIAL			8
#define BRCMF_E_STATUS_NEWSCAN			9
#define BRCMF_E_STATUS_NEWASSOC			10
#define BRCMF_E_STATUS_11HQUIET			11
#define BRCMF_E_STATUS_SUPPRESS			12
#define BRCMF_E_STATUS_NOCHANS			13
#define BRCMF_E_STATUS_CS_ABORT			15
#define BRCMF_E_STATUS_ERROR			16

/* status field values for PSK_SUP event */
#define BRCMF_E_STATUS_FWSUP_WAIT_M1		4
#define BRCMF_E_STATUS_FWSUP_PREP_M2		5
#define BRCMF_E_STATUS_FWSUP_COMPLETED		6
#define BRCMF_E_STATUS_FWSUP_TIMEOUT		7
#define BRCMF_E_STATUS_FWSUP_WAIT_M3		8
#define BRCMF_E_STATUS_FWSUP_PREP_M4		9
#define BRCMF_E_STATUS_FWSUP_WAIT_G1		10
#define BRCMF_E_STATUS_FWSUP_PREP_G2		11

/* reason field values in struct brcmf_event_msg */
#define BRCMF_E_REASON_INITIAL_ASSOC		0
#define BRCMF_E_REASON_LOW_RSSI			1
#define BRCMF_E_REASON_DEAUTH			2
#define BRCMF_E_REASON_DISASSOC			3
#define BRCMF_E_REASON_BCNS_LOST		4
#define BRCMF_E_REASON_MINTXRATE		9
#define BRCMF_E_REASON_TXFAIL			10

#define BRCMF_E_REASON_LINK_BSSCFG_DIS		4
#define BRCMF_E_REASON_FAST_ROAM_FAILED		5
#define BRCMF_E_REASON_DIRECTED_ROAM		6
#define BRCMF_E_REASON_TSPEC_REJECTED		7
#define BRCMF_E_REASON_BETTER_AP		8

#define BRCMF_E_REASON_TDLS_PEER_DISCOVERED	0
#define BRCMF_E_REASON_TDLS_PEER_CONNECTED	1
#define BRCMF_E_REASON_TDLS_PEER_DISCONNECTED	2

/* reason field values for PSK_SUP event */
#define BRCMF_E_REASON_FWSUP_OTHER		0
#define BRCMF_E_REASON_FWSUP_DECRYPT_KEY_DATA	1
#define BRCMF_E_REASON_FWSUP_BAD_UCAST_WEP128	2
#define BRCMF_E_REASON_FWSUP_BAD_UCAST_WEP40	3
#define BRCMF_E_REASON_FWSUP_UNSUP_KEY_LEN	4
#define BRCMF_E_REASON_FWSUP_PW_KEY_CIPHER	5
#define BRCMF_E_REASON_FWSUP_MSG3_TOO_MANY_IE	6
#define BRCMF_E_REASON_FWSUP_MSG3_IE_MISMATCH	7
#define BRCMF_E_REASON_FWSUP_NO_INSTALL_FLAG	8
#define BRCMF_E_REASON_FWSUP_MSG3_NO_GTK	9
#define BRCMF_E_REASON_FWSUP_GRP_KEY_CIPHER	10
#define BRCMF_E_REASON_FWSUP_GRP_MSG1_NO_GTK	11
#define BRCMF_E_REASON_FWSUP_GTK_DECRYPT_FAIL	12
#define BRCMF_E_REASON_FWSUP_SEND_FAIL		13
#define BRCMF_E_REASON_FWSUP_DEAUTH		14
#define BRCMF_E_REASON_FWSUP_WPA_PSK_TMO	15
#define BRCMF_E_REASON_FWSUP_WPA_PSK_M1_TMO	16
#define BRCMF_E_REASON_FWSUP_WPA_PSK_M3_TMO	17

/* action field values for brcmf_ifevent */
#define BRCMF_E_IF_ADD				1
#define BRCMF_E_IF_DEL				2
#define BRCMF_E_IF_CHANGE			3

/* flag field values for brcmf_ifevent */
#define BRCMF_E_IF_FLAG_NOIF			1

/* role field values for brcmf_ifevent */
#define BRCMF_E_IF_ROLE_STA			0
#define BRCMF_E_IF_ROLE_AP			1
#define BRCMF_E_IF_ROLE_WDS			2
#define BRCMF_E_IF_ROLE_P2P_GO			3
#define BRCMF_E_IF_ROLE_P2P_CLIENT		4

/**
 * definitions for event packet validation.
 */
#define BRCM_OUI				"\x00\x10\x18"
#define BCMILCP_BCM_SUBTYPE_EVENT		1
#define BCMILCP_SUBTYPE_VENDOR_LONG		32769

/**
 * struct brcm_ethhdr - broadcom specific ether header.
 *
 * @subtype: subtype for this packet.
 * @length: TODO: length of appended data.
 * @version: version indication.
 * @oui: OUI of this packet.
 * @usr_subtype: subtype for this OUI.
 */
struct brcm_ethhdr {
	__be16 subtype;
	__be16 length;
	u8 version;
	u8 oui[3];
	__be16 usr_subtype;
} __packed;

struct brcmf_event_msg_be {
	__be16 version;
	__be16 flags;
	__be32 event_type;
	__be32 status;
	__be32 reason;
	__be32 auth_type;
	__be32 datalen;
	u8 addr[ETH_ALEN];
	char ifname[IFNAMSIZ];
	u8 ifidx;
	u8 bsscfgidx;
} __packed;

/**
 * struct brcmf_event - contents of broadcom event packet.
 *
 * @eth: standard ether header.
 * @hdr: broadcom specific ether header.
 * @msg: common part of the actual event message.
 */
struct brcmf_event {
	struct ethhdr eth;
	struct brcm_ethhdr hdr;
	struct brcmf_event_msg_be msg;
} __packed;

/**
 * struct brcmf_event_msg - firmware event message.
 *
 * @version: version information.
 * @flags: event flags.
 * @event_code: firmware event code.
 * @status: status information.
 * @reason: reason code.
 * @auth_type: authentication type.
 * @datalen: length of event data buffer.
 * @addr: ether address.
 * @ifname: interface name.
 * @ifidx: interface index.
 * @bsscfgidx: bsscfg index.
 */
struct brcmf_event_msg {
	u16 version;
	u16 flags;
	u32 event_code;
	u32 status;
	u32 reason;
	s32 auth_type;
	u32 datalen;
	u8 addr[ETH_ALEN];
	char ifname[IFNAMSIZ];
	u8 ifidx;
	u8 bsscfgidx;
};

struct brcmf_if_event {
	u8 ifidx;
	u8 action;
	u8 flags;
	u8 bsscfgidx;
	u8 role;
};

typedef int (*brcmf_fweh_handler_t)(struct brcmf_if *ifp,
				    const struct brcmf_event_msg *evtmsg,
				    void *data);

/**
 * struct brcmf_fweh_info - firmware event handling information.
 *
 * @p2pdev_setup_ongoing: P2P device creation in progress.
 * @event_work: event worker.
 * @evt_q_lock: lock for event queue protection.
 * @event_q: event queue.
 * @evt_handler: registered event handlers.
 */
struct brcmf_fweh_info {
	bool p2pdev_setup_ongoing;
	struct work_struct event_work;
	spinlock_t evt_q_lock;
	struct list_head event_q;
	int (*evt_handler[BRCMF_E_LAST])(struct brcmf_if *ifp,
					 const struct brcmf_event_msg *evtmsg,
					 void *data);
};

const char *brcmf_fweh_event_name(enum brcmf_fweh_event_code code);

void brcmf_fweh_attach(struct brcmf_pub *drvr);
void brcmf_fweh_detach(struct brcmf_pub *drvr);
int brcmf_fweh_register(struct brcmf_pub *drvr, enum brcmf_fweh_event_code code,
			int (*handler)(struct brcmf_if *ifp,
				       const struct brcmf_event_msg *evtmsg,
				       void *data));
void brcmf_fweh_unregister(struct brcmf_pub *drvr,
			   enum brcmf_fweh_event_code code);
int brcmf_fweh_activate_events(struct brcmf_if *ifp);
void brcmf_fweh_process_event(struct brcmf_pub *drvr,
			      struct brcmf_event *event_packet,
			      u32 packet_len);
void brcmf_fweh_p2pdev_setup(struct brcmf_if *ifp, bool ongoing);

static inline void brcmf_fweh_process_skb(struct brcmf_pub *drvr,
					  struct sk_buff *skb, u16 stype)
{
	struct brcmf_event *event_packet;
	u16 subtype, usr_stype;

	/* only process events when protocol matches */
	if (skb->protocol != cpu_to_be16(ETH_P_LINK_CTL))
		return;

	if ((skb->len + ETH_HLEN) < sizeof(*event_packet))
		return;

	event_packet = (struct brcmf_event *)skb_mac_header(skb);

	/* check subtype if needed */
	if (unlikely(stype)) {
		subtype = get_unaligned_be16(&event_packet->hdr.subtype);
		if (subtype != stype)
			return;
	}

	/* check for BRCM oui match */
	if (memcmp(BRCM_OUI, &event_packet->hdr.oui[0],
		   sizeof(event_packet->hdr.oui)))
		return;

	/* final match on usr_subtype */
	usr_stype = get_unaligned_be16(&event_packet->hdr.usr_subtype);
	if (usr_stype != BCMILCP_BCM_SUBTYPE_EVENT)
		return;

	brcmf_fweh_process_event(drvr, event_packet, skb->len + ETH_HLEN);
}

#endif /* FWEH_H_ */
