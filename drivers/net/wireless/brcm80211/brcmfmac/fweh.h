/*
 * Copyright (c) 2012 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */


#ifndef FWEH_H_
#define FWEH_H_

#include <linux/unaligned/access_ok.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/if.h>

/* formward declarations */
struct brcmf_pub;
struct brcmf_if;
struct brcmf_cfg80211_info;
struct brcmf_event;

/* firmware event codes sent by the dongle */
enum brcmf_fweh_event_code {
	BRCMF_E_SET_SSID		= 0,
	BRCMF_E_JOIN			= 1,
	BRCMF_E_START			= 2,
	BRCMF_E_AUTH			= 3,
	BRCMF_E_AUTH_IND		= 4,
	BRCMF_E_DEAUTH			= 5,
	BRCMF_E_DEAUTH_IND		= 6,
	BRCMF_E_ASSOC			= 7,
	BRCMF_E_ASSOC_IND		= 8,
	BRCMF_E_REASSOC			= 9,
	BRCMF_E_REASSOC_IND		= 10,
	BRCMF_E_DISASSOC		= 11,
	BRCMF_E_DISASSOC_IND		= 12,
	BRCMF_E_QUIET_START		= 13,
	BRCMF_E_QUIET_END		= 14,
	BRCMF_E_BEACON_RX		= 15,
	BRCMF_E_LINK			= 16,
	BRCMF_E_MIC_ERROR		= 17,
	BRCMF_E_NDIS_LINK		= 18,
	BRCMF_E_ROAM			= 19,
	BRCMF_E_TXFAIL			= 20,
	BRCMF_E_PMKID_CACHE		= 21,
	BRCMF_E_RETROGRADE_TSF		= 22,
	BRCMF_E_PRUNE			= 23,
	BRCMF_E_AUTOAUTH		= 24,
	BRCMF_E_EAPOL_MSG		= 25,
	BRCMF_E_SCAN_COMPLETE		= 26,
	BRCMF_E_ADDTS_IND		= 27,
	BRCMF_E_DELTS_IND		= 28,
	BRCMF_E_BCNSENT_IND		= 29,
	BRCMF_E_BCNRX_MSG		= 30,
	BRCMF_E_BCNLOST_MSG		= 31,
	BRCMF_E_ROAM_PREP		= 32,
	BRCMF_E_PFN_NET_FOUND		= 33,
	BRCMF_E_PFN_NET_LOST		= 34,
	BRCMF_E_RESET_COMPLETE		= 35,
	BRCMF_E_JOIN_START		= 36,
	BRCMF_E_ROAM_START		= 37,
	BRCMF_E_ASSOC_START		= 38,
	BRCMF_E_IBSS_ASSOC		= 39,
	BRCMF_E_RADIO			= 40,
	BRCMF_E_PSM_WATCHDOG		= 41,
	BRCMF_E_PROBREQ_MSG		= 44,
	BRCMF_E_SCAN_CONFIRM_IND	= 45,
	BRCMF_E_PSK_SUP			= 46,
	BRCMF_E_COUNTRY_CODE_CHANGED	= 47,
	BRCMF_E_EXCEEDED_MEDIUM_TIME	= 48,
	BRCMF_E_ICV_ERROR		= 49,
	BRCMF_E_UNICAST_DECODE_ERROR	= 50,
	BRCMF_E_MULTICAST_DECODE_ERROR	= 51,
	BRCMF_E_TRACE			= 52,
	BRCMF_E_IF			= 54,
	BRCMF_E_RSSI			= 56,
	BRCMF_E_PFN_SCAN_COMPLETE	= 57,
	BRCMF_E_EXTLOG_MSG		= 58,
	BRCMF_E_ACTION_FRAME		= 59,
	BRCMF_E_ACTION_FRAME_COMPLETE	= 60,
	BRCMF_E_PRE_ASSOC_IND		= 61,
	BRCMF_E_PRE_REASSOC_IND		= 62,
	BRCMF_E_CHANNEL_ADOPTED		= 63,
	BRCMF_E_AP_STARTED		= 64,
	BRCMF_E_DFS_AP_STOP		= 65,
	BRCMF_E_DFS_AP_RESUME		= 66,
	BRCMF_E_ESCAN_RESULT		= 69,
	BRCMF_E_ACTION_FRAME_OFF_CHAN_COMPLETE	= 70,
	BRCMF_E_DCS_REQUEST		= 73,
	BRCMF_E_FIFO_CREDIT_MAP		= 74,
	BRCMF_E_LAST
};

/* flags field values in struct brcmf_event_msg */
#define BRCMF_EVENT_MSG_LINK		0x01
#define BRCMF_EVENT_MSG_FLUSHTXQ	0x02
#define BRCMF_EVENT_MSG_GROUP		0x04

/**
 * definitions for event packet validation.
 */
#define BRCMF_EVENT_OUI_OFFSET		19
#define BRCM_OUI			"\x00\x10\x18"
#define DOT11_OUI_LEN			3
#define BCMILCP_BCM_SUBTYPE_EVENT	1


/**
 * struct brcmf_event_msg - firmware event message.
 *
 * @version: version information.
 * @flags: event flags.
 * @event_code: firmware event code.
 * @status: status information.
 * @reason: reason code.
 * @auth_type: authentication type.
 * @datalen: lenght of event data buffer.
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

typedef int (*brcmf_fweh_handler_t)(struct brcmf_if *ifp,
				    const struct brcmf_event_msg *evtmsg,
				    void *data);

/**
 * struct brcmf_fweh_info - firmware event handling information.
 *
 * @event_work: event worker.
 * @evt_q_lock: lock for event queue protection.
 * @event_q: event queue.
 * @evt_handler: registered event handlers.
 */
struct brcmf_fweh_info {
	struct work_struct event_work;
	struct spinlock evt_q_lock;
	struct list_head event_q;
	int (*evt_handler[BRCMF_E_LAST])(struct brcmf_if *ifp,
					 const struct brcmf_event_msg *evtmsg,
					 void *data);
};

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
			      struct brcmf_event *event_packet, u8 *ifidx);

static inline void brcmf_fweh_process_skb(struct brcmf_pub *drvr,
					  struct sk_buff *skb, u8 *ifidx)
{
	struct brcmf_event *event_packet;
	u8 *data;
	u16 usr_stype;

	/* only process events when protocol matches */
	if (skb->protocol != cpu_to_be16(ETH_P_LINK_CTL))
		return;

	/* check for BRCM oui match */
	event_packet = (struct brcmf_event *)skb_mac_header(skb);
	data = (u8 *)event_packet;
	data += BRCMF_EVENT_OUI_OFFSET;
	if (memcmp(BRCM_OUI, data, DOT11_OUI_LEN))
		return;

	/* final match on usr_subtype */
	data += DOT11_OUI_LEN;
	usr_stype = get_unaligned_be16(data);
	if (usr_stype != BCMILCP_BCM_SUBTYPE_EVENT)
		return;

	brcmf_fweh_process_event(drvr, event_packet, ifidx);
}

#endif /* FWEH_H_ */
