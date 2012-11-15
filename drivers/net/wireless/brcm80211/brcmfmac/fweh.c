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
#include <linux/netdevice.h>

#include "defs.h"
#include "brcmu_wifi.h"
#include "brcmu_utils.h"

#include "dhd.h"
#include "dhd_dbg.h"
#include "fweh.h"
#include "fwil.h"

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
 * struct brcmf_fweh_queue_item - event item on event queue.
 *
 * @q: list element for queuing.
 * @code: event code.
 * @ifidx: interface index related to this event.
 * @ifaddr: ethernet address for interface.
 * @emsg: common parameters of the firmware event message.
 * @data: event specific data part of the firmware event.
 */
struct brcmf_fweh_queue_item {
	struct list_head q;
	enum brcmf_fweh_event_code code;
	u8 ifidx;
	u8 ifaddr[ETH_ALEN];
	struct brcmf_event_msg_be emsg;
	u8 data[0];
};

/**
 * struct brcmf_fweh_event_name - code, name mapping entry.
 */
struct brcmf_fweh_event_name {
	enum brcmf_fweh_event_code code;
	const char *name;
};

#ifdef DEBUG
/* array for mapping code to event name */
static struct brcmf_fweh_event_name fweh_event_names[] = {
	{ BRCMF_E_SET_SSID, "SET_SSID" },
	{ BRCMF_E_JOIN, "JOIN" },
	{ BRCMF_E_START, "START" },
	{ BRCMF_E_AUTH, "AUTH" },
	{ BRCMF_E_AUTH_IND, "AUTH_IND" },
	{ BRCMF_E_DEAUTH, "DEAUTH" },
	{ BRCMF_E_DEAUTH_IND, "DEAUTH_IND" },
	{ BRCMF_E_ASSOC, "ASSOC" },
	{ BRCMF_E_ASSOC_IND, "ASSOC_IND" },
	{ BRCMF_E_REASSOC, "REASSOC" },
	{ BRCMF_E_REASSOC_IND, "REASSOC_IND" },
	{ BRCMF_E_DISASSOC, "DISASSOC" },
	{ BRCMF_E_DISASSOC_IND, "DISASSOC_IND" },
	{ BRCMF_E_QUIET_START, "START_QUIET" },
	{ BRCMF_E_QUIET_END, "END_QUIET" },
	{ BRCMF_E_BEACON_RX, "BEACON_RX" },
	{ BRCMF_E_LINK, "LINK" },
	{ BRCMF_E_MIC_ERROR, "MIC_ERROR" },
	{ BRCMF_E_NDIS_LINK, "NDIS_LINK" },
	{ BRCMF_E_ROAM, "ROAM" },
	{ BRCMF_E_TXFAIL, "TXFAIL" },
	{ BRCMF_E_PMKID_CACHE, "PMKID_CACHE" },
	{ BRCMF_E_RETROGRADE_TSF, "RETROGRADE_TSF" },
	{ BRCMF_E_PRUNE, "PRUNE" },
	{ BRCMF_E_AUTOAUTH, "AUTOAUTH" },
	{ BRCMF_E_EAPOL_MSG, "EAPOL_MSG" },
	{ BRCMF_E_SCAN_COMPLETE, "SCAN_COMPLETE" },
	{ BRCMF_E_ADDTS_IND, "ADDTS_IND" },
	{ BRCMF_E_DELTS_IND, "DELTS_IND" },
	{ BRCMF_E_BCNSENT_IND, "BCNSENT_IND" },
	{ BRCMF_E_BCNRX_MSG, "BCNRX_MSG" },
	{ BRCMF_E_BCNLOST_MSG, "BCNLOST_MSG" },
	{ BRCMF_E_ROAM_PREP, "ROAM_PREP" },
	{ BRCMF_E_PFN_NET_FOUND, "PNO_NET_FOUND" },
	{ BRCMF_E_PFN_NET_LOST, "PNO_NET_LOST" },
	{ BRCMF_E_RESET_COMPLETE, "RESET_COMPLETE" },
	{ BRCMF_E_JOIN_START, "JOIN_START" },
	{ BRCMF_E_ROAM_START, "ROAM_START" },
	{ BRCMF_E_ASSOC_START, "ASSOC_START" },
	{ BRCMF_E_IBSS_ASSOC, "IBSS_ASSOC" },
	{ BRCMF_E_RADIO, "RADIO" },
	{ BRCMF_E_PSM_WATCHDOG, "PSM_WATCHDOG" },
	{ BRCMF_E_PROBREQ_MSG, "PROBREQ_MSG" },
	{ BRCMF_E_SCAN_CONFIRM_IND, "SCAN_CONFIRM_IND" },
	{ BRCMF_E_PSK_SUP, "PSK_SUP" },
	{ BRCMF_E_COUNTRY_CODE_CHANGED, "COUNTRY_CODE_CHANGED" },
	{ BRCMF_E_EXCEEDED_MEDIUM_TIME, "EXCEEDED_MEDIUM_TIME" },
	{ BRCMF_E_ICV_ERROR, "ICV_ERROR" },
	{ BRCMF_E_UNICAST_DECODE_ERROR, "UNICAST_DECODE_ERROR" },
	{ BRCMF_E_MULTICAST_DECODE_ERROR, "MULTICAST_DECODE_ERROR" },
	{ BRCMF_E_TRACE, "TRACE" },
	{ BRCMF_E_IF, "IF" },
	{ BRCMF_E_RSSI, "RSSI" },
	{ BRCMF_E_PFN_SCAN_COMPLETE, "PFN_SCAN_COMPLETE" },
	{ BRCMF_E_EXTLOG_MSG, "EXTLOG_MSG" },
	{ BRCMF_E_ACTION_FRAME, "ACTION_FRAME" },
	{ BRCMF_E_ACTION_FRAME_COMPLETE, "ACTION_FRAME_COMPLETE" },
	{ BRCMF_E_PRE_ASSOC_IND, "PRE_ASSOC_IND" },
	{ BRCMF_E_PRE_REASSOC_IND, "PRE_REASSOC_IND" },
	{ BRCMF_E_CHANNEL_ADOPTED, "CHANNEL_ADOPTED" },
	{ BRCMF_E_AP_STARTED, "AP_STARTED" },
	{ BRCMF_E_DFS_AP_STOP, "DFS_AP_STOP" },
	{ BRCMF_E_DFS_AP_RESUME, "DFS_AP_RESUME" },
	{ BRCMF_E_ESCAN_RESULT, "ESCAN_RESULT" },
	{ BRCMF_E_ACTION_FRAME_OFF_CHAN_COMPLETE, "ACTION_FRM_OFF_CHAN_CMPLT" },
	{ BRCMF_E_DCS_REQUEST, "DCS_REQUEST" },
	{ BRCMF_E_FIFO_CREDIT_MAP, "FIFO_CREDIT_MAP"}
};

/**
 * brcmf_fweh_event_name() - returns name for given event code.
 *
 * @code: code to lookup.
 */
static const char *brcmf_fweh_event_name(enum brcmf_fweh_event_code code)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(fweh_event_names); i++) {
		if (fweh_event_names[i].code == code)
			return fweh_event_names[i].name;
	}
	return "unknown";
}
#else
static const char *brcmf_fweh_event_name(enum brcmf_fweh_event_code code)
{
	return "nodebug";
}
#endif

/**
 * brcmf_fweh_queue_event() - create and queue event.
 *
 * @ifp: firmware interface object.
 * @code: event code.
 * @pkt: event ether packet.
 */
static void brcmf_fweh_queue_event(struct brcmf_if *ifp,
				   enum brcmf_fweh_event_code code,
				   struct brcmf_event *pkt)
{
	struct brcmf_fweh_info *fweh = &ifp->drvr->fweh;
	struct brcmf_fweh_queue_item *event;
	gfp_t alloc_flag = GFP_KERNEL;
	ulong flags;
	void *data;
	u32 datalen;

	/* determine event data */
	datalen = get_unaligned_be32(&pkt->msg.datalen);
	data = &pkt[1];

	if (!ifp->ndev || (code != BRCMF_E_IF && !fweh->evt_handler[code])) {
		brcmf_dbg(EVENT, "event ignored: code=%d\n", code);
		brcmf_dbg_hex_dump(BRCMF_EVENT_ON(), data, datalen, "event:");
		return;
	}

	if (in_interrupt())
		alloc_flag = GFP_ATOMIC;

	event = kzalloc(sizeof(*event) + datalen, alloc_flag);
	event->code = code;
	event->ifidx = ifp->idx;

	/* use memcpy to get aligned event message */
	memcpy(&event->emsg, &pkt->msg, sizeof(event->emsg));
	memcpy(event->data, data, datalen);
	memcpy(event->ifaddr, pkt->eth.h_dest, ETH_ALEN);

	spin_lock_irqsave(&fweh->evt_q_lock, flags);
	list_add_tail(&event->q, &fweh->event_q);
	spin_unlock_irqrestore(&fweh->evt_q_lock, flags);
	schedule_work(&fweh->event_work);
}

/**
 * brcmf_fweh_process_if_event() - handle IF event.
 *
 * @drvr: driver information object.
 * @item: queue entry.
 * @ifpp: interface object (may change upon ADD action).
 */
static int brcmf_fweh_process_if_event(struct brcmf_pub *drvr,
				       struct brcmf_fweh_queue_item *item,
				       struct brcmf_if **ifpp)
{
	struct brcmf_event_msg_be *event = &item->emsg;
	struct brcmf_if_event *ifevent = (struct brcmf_if_event *)item->data;
	struct brcmf_if *ifp;
	int err = 0;

	brcmf_dbg(EVENT, "action: %u idx: %u bsscfg: %u flags: %u\n",
		  ifevent->action, ifevent->ifidx,
		  ifevent->bssidx, ifevent->flags);

	if (ifevent->ifidx >= BRCMF_MAX_IFS) {
		brcmf_dbg(ERROR, "invalid interface index: %u\n",
			  ifevent->ifidx);
		return -EINVAL;
	}

	switch (ifevent->action) {
	case BRCMF_E_IF_ADD:
		brcmf_dbg(EVENT, "adding %s (%pM, %pM)\n", event->ifname,
			  event->addr, item->ifaddr);
		ifp = brcmf_add_if(drvr, ifevent->ifidx, ifevent->bssidx,
				   event->ifname, item->ifaddr);
		if (!IS_ERR(ifp)) {
			*ifpp = ifp;
			err = brcmf_net_attach(ifp);
		} else {
			err = PTR_ERR(ifp);
		}
		break;
	case BRCMF_E_IF_DEL:
		brcmf_del_if(drvr, ifevent->ifidx);
		break;
	case BRCMF_E_IF_CHANGE:
		/* nothing to do here */
		break;
	default:
		brcmf_dbg(ERROR, "unknown event action: %u\n", ifevent->action);
		err = -EBADE;
		break;
	}
	return err;
}

/**
 * brcmf_fweh_dequeue_event() - get event from the queue.
 *
 * @fweh: firmware event handling info.
 */
static struct brcmf_fweh_queue_item *
brcmf_fweh_dequeue_event(struct brcmf_fweh_info *fweh)
{
	struct brcmf_fweh_queue_item *event = NULL;
	ulong flags;

	spin_lock_irqsave(&fweh->evt_q_lock, flags);
	if (!list_empty(&fweh->event_q)) {
		event = list_first_entry(&fweh->event_q,
					 struct brcmf_fweh_queue_item, q);
		list_del(&event->q);
	}
	spin_unlock_irqrestore(&fweh->evt_q_lock, flags);

	return event;
}

/**
 * brcmf_fweh_event_worker() - firmware event worker.
 *
 * @work: worker object.
 */
static void brcmf_fweh_event_worker(struct work_struct *work)
{
	struct brcmf_pub *drvr;
	struct brcmf_if *ifp;
	struct brcmf_fweh_info *fweh;
	struct brcmf_fweh_queue_item *event;
	int err = 0;
	struct brcmf_event_msg_be *emsg_be;
	struct brcmf_event_msg emsg;

	fweh = container_of(work, struct brcmf_fweh_info, event_work);
	drvr = container_of(fweh, struct brcmf_pub, fweh);

	while ((event = brcmf_fweh_dequeue_event(fweh))) {
		ifp = drvr->iflist[event->ifidx];

		brcmf_dbg(EVENT, "event %s (%u) ifidx %u bsscfg %u addr %pM:\n",
			  brcmf_fweh_event_name(event->code), event->code,
			  event->emsg.ifidx, event->emsg.bsscfgidx,
			  event->emsg.addr);

		/* handle interface event */
		if (event->code == BRCMF_E_IF) {
			err = brcmf_fweh_process_if_event(drvr, event, &ifp);
			if (err)
				goto event_free;
		}

		/* convert event message */
		emsg_be = &event->emsg;
		emsg.version = be16_to_cpu(emsg_be->version);
		emsg.flags = be16_to_cpu(emsg_be->flags);
		emsg.event_code = event->code;
		emsg.status = be32_to_cpu(emsg_be->status);
		emsg.reason = be32_to_cpu(emsg_be->reason);
		emsg.auth_type = be32_to_cpu(emsg_be->auth_type);
		emsg.datalen = be32_to_cpu(emsg_be->datalen);
		memcpy(emsg.addr, emsg_be->addr, ETH_ALEN);
		memcpy(emsg.ifname, emsg_be->ifname, sizeof(emsg.ifname));
		emsg.ifidx = emsg_be->ifidx;
		emsg.bsscfgidx = emsg_be->bsscfgidx;

		brcmf_dbg(EVENT, "  version %u flags %u status %u reason %u\n",
			  emsg.version, emsg.flags, emsg.status, emsg.reason);
		brcmf_dbg_hex_dump(BRCMF_EVENT_ON(), event->data,
				   min_t(u32, emsg.datalen, 64),
				   "appended:");

		/* handle the event if valid interface and handler */
		if (ifp->ndev && fweh->evt_handler[event->code])
			err = fweh->evt_handler[event->code](ifp, &emsg,
							     event->data);
		else
			brcmf_dbg(ERROR, "unhandled event %d ignored\n",
				  event->code);
		if (err) {
			brcmf_dbg(ERROR, "event handler failed (%d)\n",
				  event->code);
			err = 0;
		}
event_free:
		kfree(event);
	}
}

/**
 * brcmf_fweh_attach() - initialize firmware event handling.
 *
 * @drvr: driver information object.
 */
void brcmf_fweh_attach(struct brcmf_pub *drvr)
{
	struct brcmf_fweh_info *fweh = &drvr->fweh;
	INIT_WORK(&fweh->event_work, brcmf_fweh_event_worker);
	spin_lock_init(&fweh->evt_q_lock);
	INIT_LIST_HEAD(&fweh->event_q);
}

/**
 * brcmf_fweh_detach() - cleanup firmware event handling.
 *
 * @drvr: driver information object.
 */
void brcmf_fweh_detach(struct brcmf_pub *drvr)
{
	struct brcmf_fweh_info *fweh = &drvr->fweh;
	struct brcmf_if *ifp = drvr->iflist[0];
	s8 eventmask[BRCMF_EVENTING_MASK_LEN];

	/* clear all events */
	memset(eventmask, 0, BRCMF_EVENTING_MASK_LEN);
	(void)brcmf_fil_iovar_data_set(ifp, "event_msgs",
				       eventmask, BRCMF_EVENTING_MASK_LEN);

	/* cancel the worker */
	cancel_work_sync(&fweh->event_work);
	WARN_ON(!list_empty(&fweh->event_q));
	memset(fweh->evt_handler, 0, sizeof(fweh->evt_handler));
}

/**
 * brcmf_fweh_register() - register handler for given event code.
 *
 * @drvr: driver information object.
 * @code: event code.
 * @handler: handler for the given event code.
 */
int brcmf_fweh_register(struct brcmf_pub *drvr, enum brcmf_fweh_event_code code,
			brcmf_fweh_handler_t handler)
{
	if (drvr->fweh.evt_handler[code]) {
		brcmf_dbg(ERROR, "event code %d already registered\n", code);
		return -ENOSPC;
	}
	drvr->fweh.evt_handler[code] = handler;
	brcmf_dbg(TRACE, "event handler registered for code %d\n", code);
	return 0;
}

/**
 * brcmf_fweh_unregister() - remove handler for given code.
 *
 * @drvr: driver information object.
 * @code: event code.
 */
void brcmf_fweh_unregister(struct brcmf_pub *drvr,
			   enum brcmf_fweh_event_code code)
{
	brcmf_dbg(TRACE, "event handler cleared for code %d\n", code);
	drvr->fweh.evt_handler[code] = NULL;
}

/**
 * brcmf_fweh_activate_events() - enables firmware events registered.
 *
 * @ifp: primary interface object.
 */
int brcmf_fweh_activate_events(struct brcmf_if *ifp)
{
	int i, err;
	s8 eventmask[BRCMF_EVENTING_MASK_LEN];

	for (i = 0; i < BRCMF_E_LAST; i++) {
		if (ifp->drvr->fweh.evt_handler[i]) {
			brcmf_dbg(EVENT, "enable event %s\n",
				  brcmf_fweh_event_name(i));
			setbit(eventmask, i);
		}
	}

	/* want to handle IF event as well */
	setbit(eventmask, BRCMF_E_IF);

	err = brcmf_fil_iovar_data_set(ifp, "event_msgs",
				       eventmask, BRCMF_EVENTING_MASK_LEN);
	if (err)
		brcmf_dbg(ERROR, "Set event_msgs error (%d)\n", err);

	return err;
}

/**
 * brcmf_fweh_process_event() - process skb as firmware event.
 *
 * @drvr: driver information object.
 * @event_packet: event packet to process.
 * @ifidx: index of the firmware interface (may change).
 *
 * If the packet buffer contains a firmware event message it will
 * dispatch the event to a registered handler (using worker).
 */
void brcmf_fweh_process_event(struct brcmf_pub *drvr,
			      struct brcmf_event *event_packet, u8 *ifidx)
{
	enum brcmf_fweh_event_code code;

	/* determine event code and interface index */
	code = get_unaligned_be32(&event_packet->msg.event_type);
	*ifidx = event_packet->msg.ifidx;

	brcmf_fweh_queue_event(drvr->iflist[*ifidx], code, event_packet);
}
