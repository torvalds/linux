/*
 * Copyright (c) 2010 Broadcom Corporation
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
#include <linux/types.h>
#include <linux/module.h>
#include <linux/if_ether.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <net/cfg80211.h>

#include <brcmu_utils.h>
#include <brcmu_wifi.h>
#include "dhd.h"
#include "dhd_proto.h"
#include "dhd_dbg.h"
#include "dhd_bus.h"
#include "fwil.h"
#include "fwil_types.h"
#include "fweh.h"
#include "fwsignal.h"
#include "p2p.h"
#include "wl_cfg80211.h"

/**
 * DOC: Firmware Signalling
 *
 * Firmware can send signals to host and vice versa, which are passed in the
 * data packets using TLV based header. This signalling layer is on top of the
 * BDC bus protocol layer.
 */

/*
 * single definition for firmware-driver flow control tlv's.
 *
 * each tlv is specified by BRCMF_FWS_TLV_DEF(name, ID, length).
 * A length value 0 indicates variable length tlv.
 */
#define BRCMF_FWS_TLV_DEFLIST \
	BRCMF_FWS_TLV_DEF(MAC_OPEN, 1, 1) \
	BRCMF_FWS_TLV_DEF(MAC_CLOSE, 2, 1) \
	BRCMF_FWS_TLV_DEF(MAC_REQUEST_CREDIT, 3, 2) \
	BRCMF_FWS_TLV_DEF(TXSTATUS, 4, 4) \
	BRCMF_FWS_TLV_DEF(PKTTAG, 5, 4) \
	BRCMF_FWS_TLV_DEF(MACDESC_ADD,	6, 8) \
	BRCMF_FWS_TLV_DEF(MACDESC_DEL, 7, 8) \
	BRCMF_FWS_TLV_DEF(RSSI, 8, 1) \
	BRCMF_FWS_TLV_DEF(INTERFACE_OPEN, 9, 1) \
	BRCMF_FWS_TLV_DEF(INTERFACE_CLOSE, 10, 1) \
	BRCMF_FWS_TLV_DEF(FIFO_CREDITBACK, 11, 6) \
	BRCMF_FWS_TLV_DEF(PENDING_TRAFFIC_BMP, 12, 2) \
	BRCMF_FWS_TLV_DEF(MAC_REQUEST_PACKET, 13, 3) \
	BRCMF_FWS_TLV_DEF(HOST_REORDER_RXPKTS, 14, 10) \
	BRCMF_FWS_TLV_DEF(TRANS_ID, 18, 6) \
	BRCMF_FWS_TLV_DEF(COMP_TXSTATUS, 19, 1) \
	BRCMF_FWS_TLV_DEF(FILLER, 255, 0)

/*
 * enum brcmf_fws_tlv_type - definition of tlv identifiers.
 */
#define BRCMF_FWS_TLV_DEF(name, id, len) \
	BRCMF_FWS_TYPE_ ## name =  id,
enum brcmf_fws_tlv_type {
	BRCMF_FWS_TLV_DEFLIST
	BRCMF_FWS_TYPE_INVALID
};
#undef BRCMF_FWS_TLV_DEF

/*
 * enum brcmf_fws_tlv_len - definition of tlv lengths.
 */
#define BRCMF_FWS_TLV_DEF(name, id, len) \
	BRCMF_FWS_TYPE_ ## name ## _LEN = (len),
enum brcmf_fws_tlv_len {
	BRCMF_FWS_TLV_DEFLIST
};
#undef BRCMF_FWS_TLV_DEF

#ifdef DEBUG
/*
 * brcmf_fws_tlv_names - array of tlv names.
 */
#define BRCMF_FWS_TLV_DEF(name, id, len) \
	{ id, #name },
static struct {
	enum brcmf_fws_tlv_type id;
	const char *name;
} brcmf_fws_tlv_names[] = {
	BRCMF_FWS_TLV_DEFLIST
};
#undef BRCMF_FWS_TLV_DEF

static const char *brcmf_fws_get_tlv_name(enum brcmf_fws_tlv_type id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(brcmf_fws_tlv_names); i++)
		if (brcmf_fws_tlv_names[i].id == id)
			return brcmf_fws_tlv_names[i].name;

	return "INVALID";
}
#else
static const char *brcmf_fws_get_tlv_name(enum brcmf_fws_tlv_type id)
{
	return "NODEBUG";
}
#endif /* DEBUG */

/*
 * flags used to enable tlv signalling from firmware.
 */
#define BRCMF_FWS_FLAGS_RSSI_SIGNALS			0x0001
#define BRCMF_FWS_FLAGS_XONXOFF_SIGNALS			0x0002
#define BRCMF_FWS_FLAGS_CREDIT_STATUS_SIGNALS		0x0004
#define BRCMF_FWS_FLAGS_HOST_PROPTXSTATUS_ACTIVE	0x0008
#define BRCMF_FWS_FLAGS_PSQ_GENERATIONFSM_ENABLE	0x0010
#define BRCMF_FWS_FLAGS_PSQ_ZERO_BUFFER_ENABLE		0x0020
#define BRCMF_FWS_FLAGS_HOST_RXREORDER_ACTIVE		0x0040

#define BRCMF_FWS_MAC_DESC_TABLE_SIZE			32
#define BRCMF_FWS_MAC_DESC_ID_INVALID			0xff

#define BRCMF_FWS_HOSTIF_FLOWSTATE_OFF			0
#define BRCMF_FWS_HOSTIF_FLOWSTATE_ON			1
#define BRCMF_FWS_FLOWCONTROL_HIWATER			128
#define BRCMF_FWS_FLOWCONTROL_LOWATER			64

#define BRCMF_FWS_PSQ_PREC_COUNT		((BRCMF_FWS_FIFO_COUNT + 1) * 2)
#define BRCMF_FWS_PSQ_LEN				256

#define BRCMF_FWS_HTOD_FLAG_PKTFROMHOST			0x01
#define BRCMF_FWS_HTOD_FLAG_PKT_REQUESTED		0x02

#define BRCMF_FWS_RET_OK_NOSCHEDULE	0
#define BRCMF_FWS_RET_OK_SCHEDULE	1

/**
 * enum brcmf_fws_skb_state - indicates processing state of skb.
 *
 * @BRCMF_FWS_SKBSTATE_NEW: sk_buff is newly arrived in the driver.
 * @BRCMF_FWS_SKBSTATE_DELAYED: sk_buff had to wait on queue.
 * @BRCMF_FWS_SKBSTATE_SUPPRESSED: sk_buff has been suppressed by firmware.
 * @BRCMF_FWS_SKBSTATE_TIM: allocated for TIM update info.
 */
enum brcmf_fws_skb_state {
	BRCMF_FWS_SKBSTATE_NEW,
	BRCMF_FWS_SKBSTATE_DELAYED,
	BRCMF_FWS_SKBSTATE_SUPPRESSED,
	BRCMF_FWS_SKBSTATE_TIM
};

/**
 * struct brcmf_skbuff_cb - control buffer associated with skbuff.
 *
 * @if_flags: holds interface index and packet related flags.
 * @htod: host to device packet identifier (used in PKTTAG tlv).
 * @state: transmit state of the packet.
 * @mac: descriptor related to destination for this packet.
 *
 * This information is stored in control buffer struct sk_buff::cb, which
 * provides 48 bytes of storage so this structure should not exceed that.
 */
struct brcmf_skbuff_cb {
	u16 if_flags;
	u32 htod;
	enum brcmf_fws_skb_state state;
	struct brcmf_fws_mac_descriptor *mac;
};

/*
 * macro casting skbuff control buffer to struct brcmf_skbuff_cb.
 */
#define brcmf_skbcb(skb)	((struct brcmf_skbuff_cb *)((skb)->cb))

/*
 * sk_buff control if flags
 *
 *	b[11]  - packet sent upon firmware request.
 *	b[10]  - packet only contains signalling data.
 *	b[9]   - packet is a tx packet.
 *	b[8]   - packet used requested credit
 *	b[7]   - interface in AP mode.
 *	b[3:0] - interface index.
 */
#define BRCMF_SKB_IF_FLAGS_REQUESTED_MASK	0x0800
#define BRCMF_SKB_IF_FLAGS_REQUESTED_SHIFT	11
#define BRCMF_SKB_IF_FLAGS_SIGNAL_ONLY_MASK	0x0400
#define BRCMF_SKB_IF_FLAGS_SIGNAL_ONLY_SHIFT	10
#define BRCMF_SKB_IF_FLAGS_TRANSMIT_MASK        0x0200
#define BRCMF_SKB_IF_FLAGS_TRANSMIT_SHIFT	9
#define BRCMF_SKB_IF_FLAGS_REQ_CREDIT_MASK	0x0100
#define BRCMF_SKB_IF_FLAGS_REQ_CREDIT_SHIFT	8
#define BRCMF_SKB_IF_FLAGS_IF_AP_MASK		0x0080
#define BRCMF_SKB_IF_FLAGS_IF_AP_SHIFT		7
#define BRCMF_SKB_IF_FLAGS_INDEX_MASK		0x000f
#define BRCMF_SKB_IF_FLAGS_INDEX_SHIFT		0

#define brcmf_skb_if_flags_set_field(skb, field, value) \
	brcmu_maskset16(&(brcmf_skbcb(skb)->if_flags), \
			BRCMF_SKB_IF_FLAGS_ ## field ## _MASK, \
			BRCMF_SKB_IF_FLAGS_ ## field ## _SHIFT, (value))
#define brcmf_skb_if_flags_get_field(skb, field) \
	brcmu_maskget16(brcmf_skbcb(skb)->if_flags, \
			BRCMF_SKB_IF_FLAGS_ ## field ## _MASK, \
			BRCMF_SKB_IF_FLAGS_ ## field ## _SHIFT)

/*
 * sk_buff control packet identifier
 *
 * 32-bit packet identifier used in PKTTAG tlv from host to dongle.
 *
 * - Generated at the host (e.g. dhd)
 * - Seen as a generic sequence number by firmware except for the flags field.
 *
 * Generation	: b[31]	=> generation number for this packet [host->fw]
 *			   OR, current generation number [fw->host]
 * Flags	: b[30:27] => command, status flags
 * FIFO-AC	: b[26:24] => AC-FIFO id
 * h-slot	: b[23:8] => hanger-slot
 * freerun	: b[7:0] => A free running counter
 */
#define BRCMF_SKB_HTOD_TAG_GENERATION_MASK		0x80000000
#define BRCMF_SKB_HTOD_TAG_GENERATION_SHIFT		31
#define BRCMF_SKB_HTOD_TAG_FLAGS_MASK			0x78000000
#define BRCMF_SKB_HTOD_TAG_FLAGS_SHIFT			27
#define BRCMF_SKB_HTOD_TAG_FIFO_MASK			0x07000000
#define BRCMF_SKB_HTOD_TAG_FIFO_SHIFT			24
#define BRCMF_SKB_HTOD_TAG_HSLOT_MASK			0x00ffff00
#define BRCMF_SKB_HTOD_TAG_HSLOT_SHIFT			8
#define BRCMF_SKB_HTOD_TAG_FREERUN_MASK			0x000000ff
#define BRCMF_SKB_HTOD_TAG_FREERUN_SHIFT		0

#define brcmf_skb_htod_tag_set_field(skb, field, value) \
	brcmu_maskset32(&(brcmf_skbcb(skb)->htod), \
			BRCMF_SKB_HTOD_TAG_ ## field ## _MASK, \
			BRCMF_SKB_HTOD_TAG_ ## field ## _SHIFT, (value))
#define brcmf_skb_htod_tag_get_field(skb, field) \
	brcmu_maskget32(brcmf_skbcb(skb)->htod, \
			BRCMF_SKB_HTOD_TAG_ ## field ## _MASK, \
			BRCMF_SKB_HTOD_TAG_ ## field ## _SHIFT)

#define BRCMF_FWS_TXSTAT_GENERATION_MASK	0x80000000
#define BRCMF_FWS_TXSTAT_GENERATION_SHIFT	31
#define BRCMF_FWS_TXSTAT_FLAGS_MASK		0x78000000
#define BRCMF_FWS_TXSTAT_FLAGS_SHIFT		27
#define BRCMF_FWS_TXSTAT_FIFO_MASK		0x07000000
#define BRCMF_FWS_TXSTAT_FIFO_SHIFT		24
#define BRCMF_FWS_TXSTAT_HSLOT_MASK		0x00FFFF00
#define BRCMF_FWS_TXSTAT_HSLOT_SHIFT		8
#define BRCMF_FWS_TXSTAT_PKTID_MASK		0x00FFFFFF
#define BRCMF_FWS_TXSTAT_PKTID_SHIFT		0

#define brcmf_txstatus_get_field(txs, field) \
	brcmu_maskget32(txs, BRCMF_FWS_TXSTAT_ ## field ## _MASK, \
			BRCMF_FWS_TXSTAT_ ## field ## _SHIFT)

/* How long to defer borrowing in jiffies */
#define BRCMF_FWS_BORROW_DEFER_PERIOD		(HZ / 10)

/**
 * enum brcmf_fws_fifo - fifo indices used by dongle firmware.
 *
 * @BRCMF_FWS_FIFO_FIRST: first fifo, ie. background.
 * @BRCMF_FWS_FIFO_AC_BK: fifo for background traffic.
 * @BRCMF_FWS_FIFO_AC_BE: fifo for best-effort traffic.
 * @BRCMF_FWS_FIFO_AC_VI: fifo for video traffic.
 * @BRCMF_FWS_FIFO_AC_VO: fifo for voice traffic.
 * @BRCMF_FWS_FIFO_BCMC: fifo for broadcast/multicast (AP only).
 * @BRCMF_FWS_FIFO_ATIM: fifo for ATIM (AP only).
 * @BRCMF_FWS_FIFO_COUNT: number of fifos.
 */
enum brcmf_fws_fifo {
	BRCMF_FWS_FIFO_FIRST,
	BRCMF_FWS_FIFO_AC_BK = BRCMF_FWS_FIFO_FIRST,
	BRCMF_FWS_FIFO_AC_BE,
	BRCMF_FWS_FIFO_AC_VI,
	BRCMF_FWS_FIFO_AC_VO,
	BRCMF_FWS_FIFO_BCMC,
	BRCMF_FWS_FIFO_ATIM,
	BRCMF_FWS_FIFO_COUNT
};

/**
 * enum brcmf_fws_txstatus - txstatus flag values.
 *
 * @BRCMF_FWS_TXSTATUS_DISCARD:
 *	host is free to discard the packet.
 * @BRCMF_FWS_TXSTATUS_CORE_SUPPRESS:
 *	802.11 core suppressed the packet.
 * @BRCMF_FWS_TXSTATUS_FW_PS_SUPPRESS:
 *	firmware suppress the packet as device is already in PS mode.
 * @BRCMF_FWS_TXSTATUS_FW_TOSSED:
 *	firmware tossed the packet.
 * @BRCMF_FWS_TXSTATUS_HOST_TOSSED:
 *	host tossed the packet.
 */
enum brcmf_fws_txstatus {
	BRCMF_FWS_TXSTATUS_DISCARD,
	BRCMF_FWS_TXSTATUS_CORE_SUPPRESS,
	BRCMF_FWS_TXSTATUS_FW_PS_SUPPRESS,
	BRCMF_FWS_TXSTATUS_FW_TOSSED,
	BRCMF_FWS_TXSTATUS_HOST_TOSSED
};

enum brcmf_fws_fcmode {
	BRCMF_FWS_FCMODE_NONE,
	BRCMF_FWS_FCMODE_IMPLIED_CREDIT,
	BRCMF_FWS_FCMODE_EXPLICIT_CREDIT
};

enum brcmf_fws_mac_desc_state {
	BRCMF_FWS_STATE_OPEN = 1,
	BRCMF_FWS_STATE_CLOSE
};

/**
 * struct brcmf_fws_mac_descriptor - firmware signalling data per node/interface
 *
 * @occupied: slot is in use.
 * @mac_handle: handle for mac entry determined by firmware.
 * @interface_id: interface index.
 * @state: current state.
 * @suppressed: mac entry is suppressed.
 * @generation: generation bit.
 * @ac_bitmap: ac queue bitmap.
 * @requested_credit: credits requested by firmware.
 * @ea: ethernet address.
 * @seq: per-node free-running sequence.
 * @psq: power-save queue.
 * @transit_count: packet in transit to firmware.
 */
struct brcmf_fws_mac_descriptor {
	char name[16];
	u8 occupied;
	u8 mac_handle;
	u8 interface_id;
	u8 state;
	bool suppressed;
	u8 generation;
	u8 ac_bitmap;
	u8 requested_credit;
	u8 requested_packet;
	u8 ea[ETH_ALEN];
	u8 seq[BRCMF_FWS_FIFO_COUNT];
	struct pktq psq;
	int transit_count;
	int suppr_transit_count;
	bool send_tim_signal;
	u8 traffic_pending_bmp;
	u8 traffic_lastreported_bmp;
};

#define BRCMF_FWS_HANGER_MAXITEMS	1024

/**
 * enum brcmf_fws_hanger_item_state - state of hanger item.
 *
 * @BRCMF_FWS_HANGER_ITEM_STATE_FREE: item is free for use.
 * @BRCMF_FWS_HANGER_ITEM_STATE_INUSE: item is in use.
 * @BRCMF_FWS_HANGER_ITEM_STATE_INUSE_SUPPRESSED: item was suppressed.
 */
enum brcmf_fws_hanger_item_state {
	BRCMF_FWS_HANGER_ITEM_STATE_FREE = 1,
	BRCMF_FWS_HANGER_ITEM_STATE_INUSE,
	BRCMF_FWS_HANGER_ITEM_STATE_INUSE_SUPPRESSED
};


/**
 * struct brcmf_fws_hanger_item - single entry for tx pending packet.
 *
 * @state: entry is either free or occupied.
 * @pkt: packet itself.
 */
struct brcmf_fws_hanger_item {
	enum brcmf_fws_hanger_item_state state;
	struct sk_buff *pkt;
};

/**
 * struct brcmf_fws_hanger - holds packets awaiting firmware txstatus.
 *
 * @pushed: packets pushed to await txstatus.
 * @popped: packets popped upon handling txstatus.
 * @failed_to_push: packets that could not be pushed.
 * @failed_to_pop: packets that could not be popped.
 * @failed_slotfind: packets for which failed to find an entry.
 * @slot_pos: last returned item index for a free entry.
 * @items: array of hanger items.
 */
struct brcmf_fws_hanger {
	u32 pushed;
	u32 popped;
	u32 failed_to_push;
	u32 failed_to_pop;
	u32 failed_slotfind;
	u32 slot_pos;
	struct brcmf_fws_hanger_item items[BRCMF_FWS_HANGER_MAXITEMS];
};

struct brcmf_fws_macdesc_table {
	struct brcmf_fws_mac_descriptor nodes[BRCMF_FWS_MAC_DESC_TABLE_SIZE];
	struct brcmf_fws_mac_descriptor iface[BRCMF_MAX_IFS];
	struct brcmf_fws_mac_descriptor other;
};

struct brcmf_fws_info {
	struct brcmf_pub *drvr;
	struct brcmf_fws_stats stats;
	struct brcmf_fws_hanger hanger;
	enum brcmf_fws_fcmode fcmode;
	bool bcmc_credit_check;
	struct brcmf_fws_macdesc_table desc;
	struct workqueue_struct *fws_wq;
	struct work_struct fws_dequeue_work;
	u32 fifo_enqpkt[BRCMF_FWS_FIFO_COUNT];
	int fifo_credit[BRCMF_FWS_FIFO_COUNT];
	int credits_borrowed[BRCMF_FWS_FIFO_AC_VO + 1];
	int deq_node_pos[BRCMF_FWS_FIFO_COUNT];
	u32 fifo_credit_map;
	u32 fifo_delay_map;
	unsigned long borrow_defer_timestamp;
	bool bus_flow_blocked;
	bool creditmap_received;
};

/*
 * brcmf_fws_prio2fifo - mapping from 802.1d priority to firmware fifo index.
 */
static const int brcmf_fws_prio2fifo[] = {
	BRCMF_FWS_FIFO_AC_BE,
	BRCMF_FWS_FIFO_AC_BK,
	BRCMF_FWS_FIFO_AC_BK,
	BRCMF_FWS_FIFO_AC_BE,
	BRCMF_FWS_FIFO_AC_VI,
	BRCMF_FWS_FIFO_AC_VI,
	BRCMF_FWS_FIFO_AC_VO,
	BRCMF_FWS_FIFO_AC_VO
};

static int fcmode;
module_param(fcmode, int, S_IRUSR);
MODULE_PARM_DESC(fcmode, "mode of firmware signalled flow control");

#define BRCMF_FWS_TLV_DEF(name, id, len) \
	case BRCMF_FWS_TYPE_ ## name: \
		return len;

/**
 * brcmf_fws_get_tlv_len() - returns defined length for given tlv id.
 *
 * @fws: firmware-signalling information.
 * @id: identifier of the TLV.
 *
 * Return: the specified length for the given TLV; Otherwise -EINVAL.
 */
static int brcmf_fws_get_tlv_len(struct brcmf_fws_info *fws,
				 enum brcmf_fws_tlv_type id)
{
	switch (id) {
	BRCMF_FWS_TLV_DEFLIST
	default:
		fws->stats.tlv_invalid_type++;
		break;
	}
	return -EINVAL;
}
#undef BRCMF_FWS_TLV_DEF

static bool brcmf_fws_ifidx_match(struct sk_buff *skb, void *arg)
{
	u32 ifidx = brcmf_skb_if_flags_get_field(skb, INDEX);
	return ifidx == *(int *)arg;
}

static void brcmf_fws_psq_flush(struct brcmf_fws_info *fws, struct pktq *q,
				int ifidx)
{
	bool (*matchfn)(struct sk_buff *, void *) = NULL;
	struct sk_buff *skb;
	int prec;

	if (ifidx != -1)
		matchfn = brcmf_fws_ifidx_match;
	for (prec = 0; prec < q->num_prec; prec++) {
		skb = brcmu_pktq_pdeq_match(q, prec, matchfn, &ifidx);
		while (skb) {
			brcmu_pkt_buf_free_skb(skb);
			skb = brcmu_pktq_pdeq_match(q, prec, matchfn, &ifidx);
		}
	}
}

static void brcmf_fws_hanger_init(struct brcmf_fws_hanger *hanger)
{
	int i;

	memset(hanger, 0, sizeof(*hanger));
	for (i = 0; i < ARRAY_SIZE(hanger->items); i++)
		hanger->items[i].state = BRCMF_FWS_HANGER_ITEM_STATE_FREE;
}

static u32 brcmf_fws_hanger_get_free_slot(struct brcmf_fws_hanger *h)
{
	u32 i;

	i = (h->slot_pos + 1) % BRCMF_FWS_HANGER_MAXITEMS;

	while (i != h->slot_pos) {
		if (h->items[i].state == BRCMF_FWS_HANGER_ITEM_STATE_FREE) {
			h->slot_pos = i;
			goto done;
		}
		i++;
		if (i == BRCMF_FWS_HANGER_MAXITEMS)
			i = 0;
	}
	brcmf_err("all slots occupied\n");
	h->failed_slotfind++;
	i = BRCMF_FWS_HANGER_MAXITEMS;
done:
	return i;
}

static int brcmf_fws_hanger_pushpkt(struct brcmf_fws_hanger *h,
				    struct sk_buff *pkt, u32 slot_id)
{
	if (slot_id >= BRCMF_FWS_HANGER_MAXITEMS)
		return -ENOENT;

	if (h->items[slot_id].state != BRCMF_FWS_HANGER_ITEM_STATE_FREE) {
		brcmf_err("slot is not free\n");
		h->failed_to_push++;
		return -EINVAL;
	}

	h->items[slot_id].state = BRCMF_FWS_HANGER_ITEM_STATE_INUSE;
	h->items[slot_id].pkt = pkt;
	h->pushed++;
	return 0;
}

static int brcmf_fws_hanger_poppkt(struct brcmf_fws_hanger *h,
					  u32 slot_id, struct sk_buff **pktout,
					  bool remove_item)
{
	if (slot_id >= BRCMF_FWS_HANGER_MAXITEMS)
		return -ENOENT;

	if (h->items[slot_id].state == BRCMF_FWS_HANGER_ITEM_STATE_FREE) {
		brcmf_err("entry not in use\n");
		h->failed_to_pop++;
		return -EINVAL;
	}

	*pktout = h->items[slot_id].pkt;
	if (remove_item) {
		h->items[slot_id].state = BRCMF_FWS_HANGER_ITEM_STATE_FREE;
		h->items[slot_id].pkt = NULL;
		h->popped++;
	}
	return 0;
}

static int brcmf_fws_hanger_mark_suppressed(struct brcmf_fws_hanger *h,
					    u32 slot_id)
{
	if (slot_id >= BRCMF_FWS_HANGER_MAXITEMS)
		return -ENOENT;

	if (h->items[slot_id].state == BRCMF_FWS_HANGER_ITEM_STATE_FREE) {
		brcmf_err("entry not in use\n");
		return -EINVAL;
	}

	h->items[slot_id].state = BRCMF_FWS_HANGER_ITEM_STATE_INUSE_SUPPRESSED;
	return 0;
}

static void brcmf_fws_hanger_cleanup(struct brcmf_fws_info *fws,
				     bool (*fn)(struct sk_buff *, void *),
				     int ifidx)
{
	struct brcmf_fws_hanger *h = &fws->hanger;
	struct sk_buff *skb;
	int i;
	enum brcmf_fws_hanger_item_state s;

	for (i = 0; i < ARRAY_SIZE(h->items); i++) {
		s = h->items[i].state;
		if (s == BRCMF_FWS_HANGER_ITEM_STATE_INUSE ||
		    s == BRCMF_FWS_HANGER_ITEM_STATE_INUSE_SUPPRESSED) {
			skb = h->items[i].pkt;
			if (fn == NULL || fn(skb, &ifidx)) {
				/* suppress packets freed from psq */
				if (s == BRCMF_FWS_HANGER_ITEM_STATE_INUSE)
					brcmu_pkt_buf_free_skb(skb);
				h->items[i].state =
					BRCMF_FWS_HANGER_ITEM_STATE_FREE;
			}
		}
	}
}

static void brcmf_fws_macdesc_set_name(struct brcmf_fws_info *fws,
				       struct brcmf_fws_mac_descriptor *desc)
{
	if (desc == &fws->desc.other)
		strlcpy(desc->name, "MAC-OTHER", sizeof(desc->name));
	else if (desc->mac_handle)
		scnprintf(desc->name, sizeof(desc->name), "MAC-%d:%d",
			  desc->mac_handle, desc->interface_id);
	else
		scnprintf(desc->name, sizeof(desc->name), "MACIF:%d",
			  desc->interface_id);
}

static void brcmf_fws_macdesc_init(struct brcmf_fws_mac_descriptor *desc,
				   u8 *addr, u8 ifidx)
{
	brcmf_dbg(TRACE,
		  "enter: desc %p ea=%pM, ifidx=%u\n", desc, addr, ifidx);
	desc->occupied = 1;
	desc->state = BRCMF_FWS_STATE_OPEN;
	desc->requested_credit = 0;
	desc->requested_packet = 0;
	/* depending on use may need ifp->bssidx instead */
	desc->interface_id = ifidx;
	desc->ac_bitmap = 0xff; /* update this when handling APSD */
	if (addr)
		memcpy(&desc->ea[0], addr, ETH_ALEN);
}

static
void brcmf_fws_macdesc_deinit(struct brcmf_fws_mac_descriptor *desc)
{
	brcmf_dbg(TRACE,
		  "enter: ea=%pM, ifidx=%u\n", desc->ea, desc->interface_id);
	desc->occupied = 0;
	desc->state = BRCMF_FWS_STATE_CLOSE;
	desc->requested_credit = 0;
	desc->requested_packet = 0;
}

static struct brcmf_fws_mac_descriptor *
brcmf_fws_macdesc_lookup(struct brcmf_fws_info *fws, u8 *ea)
{
	struct brcmf_fws_mac_descriptor *entry;
	int i;

	if (ea == NULL)
		return ERR_PTR(-EINVAL);

	entry = &fws->desc.nodes[0];
	for (i = 0; i < ARRAY_SIZE(fws->desc.nodes); i++) {
		if (entry->occupied && !memcmp(entry->ea, ea, ETH_ALEN))
			return entry;
		entry++;
	}

	return ERR_PTR(-ENOENT);
}

static struct brcmf_fws_mac_descriptor*
brcmf_fws_macdesc_find(struct brcmf_fws_info *fws, struct brcmf_if *ifp, u8 *da)
{
	struct brcmf_fws_mac_descriptor *entry = &fws->desc.other;
	bool multicast;

	multicast = is_multicast_ether_addr(da);

	/* Multicast destination, STA and P2P clients get the interface entry.
	 * STA/GC gets the Mac Entry for TDLS destinations, TDLS destinations
	 * have their own entry.
	 */
	if (multicast && ifp->fws_desc) {
		entry = ifp->fws_desc;
		goto done;
	}

	entry = brcmf_fws_macdesc_lookup(fws, da);
	if (IS_ERR(entry))
		entry = ifp->fws_desc;

done:
	return entry;
}

static bool brcmf_fws_macdesc_closed(struct brcmf_fws_info *fws,
				     struct brcmf_fws_mac_descriptor *entry,
				     int fifo)
{
	struct brcmf_fws_mac_descriptor *if_entry;
	bool closed;

	/* for unique destination entries the related interface
	 * may be closed.
	 */
	if (entry->mac_handle) {
		if_entry = &fws->desc.iface[entry->interface_id];
		if (if_entry->state == BRCMF_FWS_STATE_CLOSE)
			return true;
	}
	/* an entry is closed when the state is closed and
	 * the firmware did not request anything.
	 */
	closed = entry->state == BRCMF_FWS_STATE_CLOSE &&
		 !entry->requested_credit && !entry->requested_packet;

	/* Or firmware does not allow traffic for given fifo */
	return closed || !(entry->ac_bitmap & BIT(fifo));
}

static void brcmf_fws_macdesc_cleanup(struct brcmf_fws_info *fws,
				      struct brcmf_fws_mac_descriptor *entry,
				      int ifidx)
{
	if (entry->occupied && (ifidx == -1 || ifidx == entry->interface_id)) {
		brcmf_fws_psq_flush(fws, &entry->psq, ifidx);
		entry->occupied = !!(entry->psq.len);
	}
}

static void brcmf_fws_bus_txq_cleanup(struct brcmf_fws_info *fws,
				      bool (*fn)(struct sk_buff *, void *),
				      int ifidx)
{
	struct brcmf_fws_hanger_item *hi;
	struct pktq *txq;
	struct sk_buff *skb;
	int prec;
	u32 hslot;

	txq = brcmf_bus_gettxq(fws->drvr->bus_if);
	if (IS_ERR(txq)) {
		brcmf_dbg(TRACE, "no txq to clean up\n");
		return;
	}

	for (prec = 0; prec < txq->num_prec; prec++) {
		skb = brcmu_pktq_pdeq_match(txq, prec, fn, &ifidx);
		while (skb) {
			hslot = brcmf_skb_htod_tag_get_field(skb, HSLOT);
			hi = &fws->hanger.items[hslot];
			WARN_ON(skb != hi->pkt);
			hi->state = BRCMF_FWS_HANGER_ITEM_STATE_FREE;
			brcmu_pkt_buf_free_skb(skb);
			skb = brcmu_pktq_pdeq_match(txq, prec, fn, &ifidx);
		}
	}
}

static void brcmf_fws_cleanup(struct brcmf_fws_info *fws, int ifidx)
{
	int i;
	struct brcmf_fws_mac_descriptor *table;
	bool (*matchfn)(struct sk_buff *, void *) = NULL;

	if (fws == NULL)
		return;

	if (ifidx != -1)
		matchfn = brcmf_fws_ifidx_match;

	/* cleanup individual nodes */
	table = &fws->desc.nodes[0];
	for (i = 0; i < ARRAY_SIZE(fws->desc.nodes); i++)
		brcmf_fws_macdesc_cleanup(fws, &table[i], ifidx);

	brcmf_fws_macdesc_cleanup(fws, &fws->desc.other, ifidx);
	brcmf_fws_bus_txq_cleanup(fws, matchfn, ifidx);
	brcmf_fws_hanger_cleanup(fws, matchfn, ifidx);
}

static int brcmf_fws_hdrpush(struct brcmf_fws_info *fws, struct sk_buff *skb)
{
	struct brcmf_fws_mac_descriptor *entry = brcmf_skbcb(skb)->mac;
	u8 *wlh;
	u16 data_offset = 0;
	u8 fillers;
	__le32 pkttag = cpu_to_le32(brcmf_skbcb(skb)->htod);

	brcmf_dbg(TRACE, "enter: %s, idx=%d pkttag=0x%08X, hslot=%d\n",
		  entry->name, brcmf_skb_if_flags_get_field(skb, INDEX),
		  le32_to_cpu(pkttag), (le32_to_cpu(pkttag) >> 8) & 0xffff);
	if (entry->send_tim_signal)
		data_offset += 2 + BRCMF_FWS_TYPE_PENDING_TRAFFIC_BMP_LEN;

	/* +2 is for Type[1] and Len[1] in TLV, plus TIM signal */
	data_offset += 2 + BRCMF_FWS_TYPE_PKTTAG_LEN;
	fillers = round_up(data_offset, 4) - data_offset;
	data_offset += fillers;

	skb_push(skb, data_offset);
	wlh = skb->data;

	wlh[0] = BRCMF_FWS_TYPE_PKTTAG;
	wlh[1] = BRCMF_FWS_TYPE_PKTTAG_LEN;
	memcpy(&wlh[2], &pkttag, sizeof(pkttag));
	wlh += BRCMF_FWS_TYPE_PKTTAG_LEN + 2;

	if (entry->send_tim_signal) {
		entry->send_tim_signal = 0;
		wlh[0] = BRCMF_FWS_TYPE_PENDING_TRAFFIC_BMP;
		wlh[1] = BRCMF_FWS_TYPE_PENDING_TRAFFIC_BMP_LEN;
		wlh[2] = entry->mac_handle;
		wlh[3] = entry->traffic_pending_bmp;
		brcmf_dbg(TRACE, "adding TIM info: handle %d bmp 0x%X\n",
			  entry->mac_handle, entry->traffic_pending_bmp);
		wlh += BRCMF_FWS_TYPE_PENDING_TRAFFIC_BMP_LEN + 2;
		entry->traffic_lastreported_bmp = entry->traffic_pending_bmp;
	}
	if (fillers)
		memset(wlh, BRCMF_FWS_TYPE_FILLER, fillers);

	brcmf_proto_hdrpush(fws->drvr, brcmf_skb_if_flags_get_field(skb, INDEX),
			    data_offset >> 2, skb);
	return 0;
}

static bool brcmf_fws_tim_update(struct brcmf_fws_info *fws,
				 struct brcmf_fws_mac_descriptor *entry,
				 int fifo, bool send_immediately)
{
	struct sk_buff *skb;
	struct brcmf_bus *bus;
	struct brcmf_skbuff_cb *skcb;
	s32 err;
	u32 len;

	/* check delayedQ and suppressQ in one call using bitmap */
	if (brcmu_pktq_mlen(&entry->psq, 3 << (fifo * 2)) == 0)
		entry->traffic_pending_bmp &= ~NBITVAL(fifo);
	else
		entry->traffic_pending_bmp |= NBITVAL(fifo);

	entry->send_tim_signal = false;
	if (entry->traffic_lastreported_bmp != entry->traffic_pending_bmp)
		entry->send_tim_signal = true;
	if (send_immediately && entry->send_tim_signal &&
	    entry->state == BRCMF_FWS_STATE_CLOSE) {
		/* create a dummy packet and sent that. The traffic          */
		/* bitmap info will automatically be attached to that packet */
		len = BRCMF_FWS_TYPE_PKTTAG_LEN + 2 +
		      BRCMF_FWS_TYPE_PENDING_TRAFFIC_BMP_LEN + 2 +
		      4 + fws->drvr->hdrlen;
		skb = brcmu_pkt_buf_get_skb(len);
		if (skb == NULL)
			return false;
		skb_pull(skb, len);
		skcb = brcmf_skbcb(skb);
		skcb->mac = entry;
		skcb->state = BRCMF_FWS_SKBSTATE_TIM;
		bus = fws->drvr->bus_if;
		err = brcmf_fws_hdrpush(fws, skb);
		if (err == 0)
			err = brcmf_bus_txdata(bus, skb);
		if (err)
			brcmu_pkt_buf_free_skb(skb);
		return true;
	}
	return false;
}

static void
brcmf_fws_flow_control_check(struct brcmf_fws_info *fws, struct pktq *pq,
			     u8 if_id)
{
	struct brcmf_if *ifp = fws->drvr->iflist[!if_id ? 0 : if_id + 1];

	if (WARN_ON(!ifp))
		return;

	if ((ifp->netif_stop & BRCMF_NETIF_STOP_REASON_FWS_FC) &&
	    pq->len <= BRCMF_FWS_FLOWCONTROL_LOWATER)
		brcmf_txflowblock_if(ifp,
				     BRCMF_NETIF_STOP_REASON_FWS_FC, false);
	if (!(ifp->netif_stop & BRCMF_NETIF_STOP_REASON_FWS_FC) &&
	    pq->len >= BRCMF_FWS_FLOWCONTROL_HIWATER) {
		fws->stats.fws_flow_block++;
		brcmf_txflowblock_if(ifp, BRCMF_NETIF_STOP_REASON_FWS_FC, true);
	}
	return;
}

static int brcmf_fws_rssi_indicate(struct brcmf_fws_info *fws, s8 rssi)
{
	brcmf_dbg(CTL, "rssi %d\n", rssi);
	return 0;
}

/* using macro so sparse checking does not complain
 * about locking imbalance.
 */
#define brcmf_fws_lock(drvr, flags)				\
do {								\
	flags = 0;						\
	spin_lock_irqsave(&((drvr)->fws_spinlock), (flags));	\
} while (0)

/* using macro so sparse checking does not complain
 * about locking imbalance.
 */
#define brcmf_fws_unlock(drvr, flags) \
	spin_unlock_irqrestore(&((drvr)->fws_spinlock), (flags))

static
int brcmf_fws_macdesc_indicate(struct brcmf_fws_info *fws, u8 type, u8 *data)
{
	struct brcmf_fws_mac_descriptor *entry, *existing;
	ulong flags;
	u8 mac_handle;
	u8 ifidx;
	u8 *addr;

	mac_handle = *data++;
	ifidx = *data++;
	addr = data;

	entry = &fws->desc.nodes[mac_handle & 0x1F];
	if (type == BRCMF_FWS_TYPE_MACDESC_DEL) {
		if (entry->occupied) {
			brcmf_dbg(TRACE, "deleting %s mac %pM\n",
				  entry->name, addr);
			brcmf_fws_lock(fws->drvr, flags);
			brcmf_fws_macdesc_cleanup(fws, entry, -1);
			brcmf_fws_macdesc_deinit(entry);
			brcmf_fws_unlock(fws->drvr, flags);
		} else
			fws->stats.mac_update_failed++;
		return 0;
	}

	existing = brcmf_fws_macdesc_lookup(fws, addr);
	if (IS_ERR(existing)) {
		if (!entry->occupied) {
			brcmf_fws_lock(fws->drvr, flags);
			entry->mac_handle = mac_handle;
			brcmf_fws_macdesc_init(entry, addr, ifidx);
			brcmf_fws_macdesc_set_name(fws, entry);
			brcmu_pktq_init(&entry->psq, BRCMF_FWS_PSQ_PREC_COUNT,
					BRCMF_FWS_PSQ_LEN);
			brcmf_fws_unlock(fws->drvr, flags);
			brcmf_dbg(TRACE, "add %s mac %pM\n", entry->name, addr);
		} else {
			fws->stats.mac_update_failed++;
		}
	} else {
		if (entry != existing) {
			brcmf_dbg(TRACE, "copy mac %s\n", existing->name);
			brcmf_fws_lock(fws->drvr, flags);
			memcpy(entry, existing,
			       offsetof(struct brcmf_fws_mac_descriptor, psq));
			entry->mac_handle = mac_handle;
			brcmf_fws_macdesc_deinit(existing);
			brcmf_fws_macdesc_set_name(fws, entry);
			brcmf_fws_unlock(fws->drvr, flags);
			brcmf_dbg(TRACE, "relocate %s mac %pM\n", entry->name,
				  addr);
		} else {
			brcmf_dbg(TRACE, "use existing\n");
			WARN_ON(entry->mac_handle != mac_handle);
			/* TODO: what should we do here: continue, reinit, .. */
		}
	}
	return 0;
}

static int brcmf_fws_macdesc_state_indicate(struct brcmf_fws_info *fws,
					    u8 type, u8 *data)
{
	struct brcmf_fws_mac_descriptor *entry;
	ulong flags;
	u8 mac_handle;
	int ret;

	mac_handle = data[0];
	entry = &fws->desc.nodes[mac_handle & 0x1F];
	if (!entry->occupied) {
		fws->stats.mac_ps_update_failed++;
		return -ESRCH;
	}
	brcmf_fws_lock(fws->drvr, flags);
	/* a state update should wipe old credits */
	entry->requested_credit = 0;
	entry->requested_packet = 0;
	if (type == BRCMF_FWS_TYPE_MAC_OPEN) {
		entry->state = BRCMF_FWS_STATE_OPEN;
		ret = BRCMF_FWS_RET_OK_SCHEDULE;
	} else {
		entry->state = BRCMF_FWS_STATE_CLOSE;
		brcmf_fws_tim_update(fws, entry, BRCMF_FWS_FIFO_AC_BK, false);
		brcmf_fws_tim_update(fws, entry, BRCMF_FWS_FIFO_AC_BE, false);
		brcmf_fws_tim_update(fws, entry, BRCMF_FWS_FIFO_AC_VI, false);
		brcmf_fws_tim_update(fws, entry, BRCMF_FWS_FIFO_AC_VO, true);
		ret = BRCMF_FWS_RET_OK_NOSCHEDULE;
	}
	brcmf_fws_unlock(fws->drvr, flags);
	return ret;
}

static int brcmf_fws_interface_state_indicate(struct brcmf_fws_info *fws,
					      u8 type, u8 *data)
{
	struct brcmf_fws_mac_descriptor *entry;
	ulong flags;
	u8 ifidx;
	int ret;

	ifidx = data[0];

	if (ifidx >= BRCMF_MAX_IFS) {
		ret = -ERANGE;
		goto fail;
	}

	entry = &fws->desc.iface[ifidx];
	if (!entry->occupied) {
		ret = -ESRCH;
		goto fail;
	}

	brcmf_dbg(TRACE, "%s (%d): %s\n", brcmf_fws_get_tlv_name(type), type,
		  entry->name);
	brcmf_fws_lock(fws->drvr, flags);
	switch (type) {
	case BRCMF_FWS_TYPE_INTERFACE_OPEN:
		entry->state = BRCMF_FWS_STATE_OPEN;
		ret = BRCMF_FWS_RET_OK_SCHEDULE;
		break;
	case BRCMF_FWS_TYPE_INTERFACE_CLOSE:
		entry->state = BRCMF_FWS_STATE_CLOSE;
		ret = BRCMF_FWS_RET_OK_NOSCHEDULE;
		break;
	default:
		ret = -EINVAL;
		brcmf_fws_unlock(fws->drvr, flags);
		goto fail;
	}
	brcmf_fws_unlock(fws->drvr, flags);
	return ret;

fail:
	fws->stats.if_update_failed++;
	return ret;
}

static int brcmf_fws_request_indicate(struct brcmf_fws_info *fws, u8 type,
				      u8 *data)
{
	struct brcmf_fws_mac_descriptor *entry;
	ulong flags;

	entry = &fws->desc.nodes[data[1] & 0x1F];
	if (!entry->occupied) {
		if (type == BRCMF_FWS_TYPE_MAC_REQUEST_CREDIT)
			fws->stats.credit_request_failed++;
		else
			fws->stats.packet_request_failed++;
		return -ESRCH;
	}

	brcmf_dbg(TRACE, "%s (%d): %s cnt %d bmp %d\n",
		  brcmf_fws_get_tlv_name(type), type, entry->name,
		  data[0], data[2]);
	brcmf_fws_lock(fws->drvr, flags);
	if (type == BRCMF_FWS_TYPE_MAC_REQUEST_CREDIT)
		entry->requested_credit = data[0];
	else
		entry->requested_packet = data[0];

	entry->ac_bitmap = data[2];
	brcmf_fws_unlock(fws->drvr, flags);
	return BRCMF_FWS_RET_OK_SCHEDULE;
}

static void
brcmf_fws_macdesc_use_req_credit(struct brcmf_fws_mac_descriptor *entry,
				 struct sk_buff *skb)
{
	if (entry->requested_credit > 0) {
		entry->requested_credit--;
		brcmf_skb_if_flags_set_field(skb, REQUESTED, 1);
		brcmf_skb_if_flags_set_field(skb, REQ_CREDIT, 1);
		if (entry->state != BRCMF_FWS_STATE_CLOSE)
			brcmf_err("requested credit set while mac not closed!\n");
	} else if (entry->requested_packet > 0) {
		entry->requested_packet--;
		brcmf_skb_if_flags_set_field(skb, REQUESTED, 1);
		brcmf_skb_if_flags_set_field(skb, REQ_CREDIT, 0);
		if (entry->state != BRCMF_FWS_STATE_CLOSE)
			brcmf_err("requested packet set while mac not closed!\n");
	} else {
		brcmf_skb_if_flags_set_field(skb, REQUESTED, 0);
		brcmf_skb_if_flags_set_field(skb, REQ_CREDIT, 0);
	}
}

static void brcmf_fws_macdesc_return_req_credit(struct sk_buff *skb)
{
	struct brcmf_fws_mac_descriptor *entry = brcmf_skbcb(skb)->mac;

	if ((brcmf_skb_if_flags_get_field(skb, REQ_CREDIT)) &&
	    (entry->state == BRCMF_FWS_STATE_CLOSE))
		entry->requested_credit++;
}

static void brcmf_fws_return_credits(struct brcmf_fws_info *fws,
				     u8 fifo, u8 credits)
{
	int lender_ac;
	int *borrowed;
	int *fifo_credit;

	if (!credits)
		return;

	fws->fifo_credit_map |= 1 << fifo;

	if ((fifo == BRCMF_FWS_FIFO_AC_BE) &&
	    (fws->credits_borrowed[0])) {
		for (lender_ac = BRCMF_FWS_FIFO_AC_VO; lender_ac >= 0;
		     lender_ac--) {
			borrowed = &fws->credits_borrowed[lender_ac];
			if (*borrowed) {
				fws->fifo_credit_map |= (1 << lender_ac);
				fifo_credit = &fws->fifo_credit[lender_ac];
				if (*borrowed >= credits) {
					*borrowed -= credits;
					*fifo_credit += credits;
					return;
				} else {
					credits -= *borrowed;
					*fifo_credit += *borrowed;
					*borrowed = 0;
				}
			}
		}
	}

	fws->fifo_credit[fifo] += credits;
}

static void brcmf_fws_schedule_deq(struct brcmf_fws_info *fws)
{
	/* only schedule dequeue when there are credits for delayed traffic */
	if (fws->fifo_credit_map & fws->fifo_delay_map)
		queue_work(fws->fws_wq, &fws->fws_dequeue_work);
}

static int brcmf_fws_enq(struct brcmf_fws_info *fws,
			 enum brcmf_fws_skb_state state, int fifo,
			 struct sk_buff *p)
{
	int prec = 2 * fifo;
	u32 *qfull_stat = &fws->stats.delayq_full_error;

	struct brcmf_fws_mac_descriptor *entry;

	entry = brcmf_skbcb(p)->mac;
	if (entry == NULL) {
		brcmf_err("no mac descriptor found for skb %p\n", p);
		return -ENOENT;
	}

	brcmf_dbg(DATA, "enter: fifo %d skb %p\n", fifo, p);
	if (state == BRCMF_FWS_SKBSTATE_SUPPRESSED) {
		prec += 1;
		qfull_stat = &fws->stats.supprq_full_error;
	}

	if (brcmu_pktq_penq(&entry->psq, prec, p) == NULL) {
		*qfull_stat += 1;
		return -ENFILE;
	}

	/* increment total enqueued packet count */
	fws->fifo_delay_map |= 1 << fifo;
	fws->fifo_enqpkt[fifo]++;

	/* update the sk_buff state */
	brcmf_skbcb(p)->state = state;

	/*
	 * A packet has been pushed so update traffic
	 * availability bitmap, if applicable
	 */
	brcmf_fws_tim_update(fws, entry, fifo, true);
	brcmf_fws_flow_control_check(fws, &entry->psq,
				     brcmf_skb_if_flags_get_field(p, INDEX));
	return 0;
}

static struct sk_buff *brcmf_fws_deq(struct brcmf_fws_info *fws, int fifo)
{
	struct brcmf_fws_mac_descriptor *table;
	struct brcmf_fws_mac_descriptor *entry;
	struct sk_buff *p;
	int num_nodes;
	int node_pos;
	int prec_out;
	int pmsk;
	int i;

	table = (struct brcmf_fws_mac_descriptor *)&fws->desc;
	num_nodes = sizeof(fws->desc) / sizeof(struct brcmf_fws_mac_descriptor);
	node_pos = fws->deq_node_pos[fifo];

	for (i = 0; i < num_nodes; i++) {
		entry = &table[(node_pos + i) % num_nodes];
		if (!entry->occupied ||
		    brcmf_fws_macdesc_closed(fws, entry, fifo))
			continue;

		if (entry->suppressed)
			pmsk = 2;
		else
			pmsk = 3;
		p = brcmu_pktq_mdeq(&entry->psq, pmsk << (fifo * 2), &prec_out);
		if (p == NULL) {
			if (entry->suppressed) {
				if (entry->suppr_transit_count)
					continue;
				entry->suppressed = false;
				p = brcmu_pktq_mdeq(&entry->psq,
						    1 << (fifo * 2), &prec_out);
			}
		}
		if  (p == NULL)
			continue;

		brcmf_fws_macdesc_use_req_credit(entry, p);

		/* move dequeue position to ensure fair round-robin */
		fws->deq_node_pos[fifo] = (node_pos + i + 1) % num_nodes;
		brcmf_fws_flow_control_check(fws, &entry->psq,
					     brcmf_skb_if_flags_get_field(p,
									  INDEX)
					     );
		/*
		 * A packet has been picked up, update traffic
		 * availability bitmap, if applicable
		 */
		brcmf_fws_tim_update(fws, entry, fifo, false);

		/*
		 * decrement total enqueued fifo packets and
		 * clear delay bitmap if done.
		 */
		fws->fifo_enqpkt[fifo]--;
		if (fws->fifo_enqpkt[fifo] == 0)
			fws->fifo_delay_map &= ~(1 << fifo);
		goto done;
	}
	p = NULL;
done:
	brcmf_dbg(DATA, "exit: fifo %d skb %p\n", fifo, p);
	return p;
}

static int brcmf_fws_txstatus_suppressed(struct brcmf_fws_info *fws, int fifo,
					 struct sk_buff *skb, u32 genbit)
{
	struct brcmf_fws_mac_descriptor *entry = brcmf_skbcb(skb)->mac;
	u32 hslot;
	int ret;
	u8 ifidx;

	hslot = brcmf_skb_htod_tag_get_field(skb, HSLOT);

	/* this packet was suppressed */
	if (!entry->suppressed) {
		entry->suppressed = true;
		entry->suppr_transit_count = entry->transit_count;
		brcmf_dbg(DATA, "suppress %s: transit %d\n",
			  entry->name, entry->transit_count);
	}

	entry->generation = genbit;

	ret = brcmf_proto_hdrpull(fws->drvr, false, &ifidx, skb);
	if (ret == 0)
		ret = brcmf_fws_enq(fws, BRCMF_FWS_SKBSTATE_SUPPRESSED, fifo,
				    skb);
	if (ret != 0) {
		/* suppress q is full or hdrpull failed, drop this packet */
		brcmf_fws_hanger_poppkt(&fws->hanger, hslot, &skb,
					true);
	} else {
		/*
		 * Mark suppressed to avoid a double free during
		 * wlfc cleanup
		 */
		brcmf_fws_hanger_mark_suppressed(&fws->hanger, hslot);
	}

	return ret;
}

static int
brcmf_fws_txs_process(struct brcmf_fws_info *fws, u8 flags, u32 hslot,
			   u32 genbit)
{
	u32 fifo;
	int ret;
	bool remove_from_hanger = true;
	struct sk_buff *skb;
	struct brcmf_skbuff_cb *skcb;
	struct brcmf_fws_mac_descriptor *entry = NULL;

	brcmf_dbg(DATA, "flags %d\n", flags);

	if (flags == BRCMF_FWS_TXSTATUS_DISCARD)
		fws->stats.txs_discard++;
	else if (flags == BRCMF_FWS_TXSTATUS_CORE_SUPPRESS) {
		fws->stats.txs_supp_core++;
		remove_from_hanger = false;
	} else if (flags == BRCMF_FWS_TXSTATUS_FW_PS_SUPPRESS) {
		fws->stats.txs_supp_ps++;
		remove_from_hanger = false;
	} else if (flags == BRCMF_FWS_TXSTATUS_FW_TOSSED)
		fws->stats.txs_tossed++;
	else if (flags == BRCMF_FWS_TXSTATUS_HOST_TOSSED)
		fws->stats.txs_host_tossed++;
	else
		brcmf_err("unexpected txstatus\n");

	ret = brcmf_fws_hanger_poppkt(&fws->hanger, hslot, &skb,
				      remove_from_hanger);
	if (ret != 0) {
		brcmf_err("no packet in hanger slot: hslot=%d\n", hslot);
		return ret;
	}

	skcb = brcmf_skbcb(skb);
	entry = skcb->mac;
	if (WARN_ON(!entry)) {
		brcmu_pkt_buf_free_skb(skb);
		return -EINVAL;
	}
	entry->transit_count--;
	if (entry->suppressed && entry->suppr_transit_count)
		entry->suppr_transit_count--;

	brcmf_dbg(DATA, "%s flags %X htod %X\n", entry->name, skcb->if_flags,
		  skcb->htod);

	/* pick up the implicit credit from this packet */
	fifo = brcmf_skb_htod_tag_get_field(skb, FIFO);
	if ((fws->fcmode == BRCMF_FWS_FCMODE_IMPLIED_CREDIT) ||
	    (brcmf_skb_if_flags_get_field(skb, REQ_CREDIT)) ||
	    (flags == BRCMF_FWS_TXSTATUS_HOST_TOSSED)) {
		brcmf_fws_return_credits(fws, fifo, 1);
		brcmf_fws_schedule_deq(fws);
	}
	brcmf_fws_macdesc_return_req_credit(skb);

	if (!remove_from_hanger)
		ret = brcmf_fws_txstatus_suppressed(fws, fifo, skb, genbit);

	if (remove_from_hanger || ret)
		brcmf_txfinalize(fws->drvr, skb, true);

	return 0;
}

static int brcmf_fws_fifocreditback_indicate(struct brcmf_fws_info *fws,
					     u8 *data)
{
	ulong flags;
	int i;

	if (fws->fcmode != BRCMF_FWS_FCMODE_EXPLICIT_CREDIT) {
		brcmf_dbg(INFO, "ignored\n");
		return BRCMF_FWS_RET_OK_NOSCHEDULE;
	}

	brcmf_dbg(DATA, "enter: data %pM\n", data);
	brcmf_fws_lock(fws->drvr, flags);
	for (i = 0; i < BRCMF_FWS_FIFO_COUNT; i++)
		brcmf_fws_return_credits(fws, i, data[i]);

	brcmf_dbg(DATA, "map: credit %x delay %x\n", fws->fifo_credit_map,
		  fws->fifo_delay_map);
	brcmf_fws_unlock(fws->drvr, flags);
	return BRCMF_FWS_RET_OK_SCHEDULE;
}

static int brcmf_fws_txstatus_indicate(struct brcmf_fws_info *fws, u8 *data)
{
	ulong lflags;
	__le32 status_le;
	u32 status;
	u32 hslot;
	u32 genbit;
	u8 flags;

	fws->stats.txs_indicate++;
	memcpy(&status_le, data, sizeof(status_le));
	status = le32_to_cpu(status_le);
	flags = brcmf_txstatus_get_field(status, FLAGS);
	hslot = brcmf_txstatus_get_field(status, HSLOT);
	genbit = brcmf_txstatus_get_field(status, GENERATION);

	brcmf_fws_lock(fws->drvr, lflags);
	brcmf_fws_txs_process(fws, flags, hslot, genbit);
	brcmf_fws_unlock(fws->drvr, lflags);
	return BRCMF_FWS_RET_OK_NOSCHEDULE;
}

static int brcmf_fws_dbg_seqnum_check(struct brcmf_fws_info *fws, u8 *data)
{
	__le32 timestamp;

	memcpy(&timestamp, &data[2], sizeof(timestamp));
	brcmf_dbg(CTL, "received: seq %d, timestamp %d\n", data[1],
		  le32_to_cpu(timestamp));
	return 0;
}

static int brcmf_fws_notify_credit_map(struct brcmf_if *ifp,
				       const struct brcmf_event_msg *e,
				       void *data)
{
	struct brcmf_fws_info *fws = ifp->drvr->fws;
	int i;
	ulong flags;
	u8 *credits = data;

	if (e->datalen < BRCMF_FWS_FIFO_COUNT) {
		brcmf_err("event payload too small (%d)\n", e->datalen);
		return -EINVAL;
	}
	if (fws->creditmap_received)
		return 0;

	fws->creditmap_received = true;

	brcmf_dbg(TRACE, "enter: credits %pM\n", credits);
	brcmf_fws_lock(ifp->drvr, flags);
	for (i = 0; i < ARRAY_SIZE(fws->fifo_credit); i++) {
		if (*credits)
			fws->fifo_credit_map |= 1 << i;
		else
			fws->fifo_credit_map &= ~(1 << i);
		fws->fifo_credit[i] = *credits++;
	}
	brcmf_fws_schedule_deq(fws);
	brcmf_fws_unlock(ifp->drvr, flags);
	return 0;
}

static int brcmf_fws_notify_bcmc_credit_support(struct brcmf_if *ifp,
						const struct brcmf_event_msg *e,
						void *data)
{
	struct brcmf_fws_info *fws = ifp->drvr->fws;
	ulong flags;

	brcmf_fws_lock(ifp->drvr, flags);
	if (fws)
		fws->bcmc_credit_check = true;
	brcmf_fws_unlock(ifp->drvr, flags);
	return 0;
}

int brcmf_fws_hdrpull(struct brcmf_pub *drvr, int ifidx, s16 signal_len,
		      struct sk_buff *skb)
{
	struct brcmf_fws_info *fws = drvr->fws;
	u8 *signal_data;
	s16 data_len;
	u8 type;
	u8 len;
	u8 *data;
	s32 status;
	s32 err;

	brcmf_dbg(HDRS, "enter: ifidx %d, skblen %u, sig %d\n",
		  ifidx, skb->len, signal_len);

	WARN_ON(signal_len > skb->len);

	/* if flow control disabled, skip to packet data and leave */
	if (!signal_len || !drvr->fw_signals) {
		skb_pull(skb, signal_len);
		return 0;
	}

	fws->stats.header_pulls++;
	data_len = signal_len;
	signal_data = skb->data;

	status = BRCMF_FWS_RET_OK_NOSCHEDULE;
	while (data_len > 0) {
		/* extract tlv info */
		type = signal_data[0];

		/* FILLER type is actually not a TLV, but
		 * a single byte that can be skipped.
		 */
		if (type == BRCMF_FWS_TYPE_FILLER) {
			signal_data += 1;
			data_len -= 1;
			continue;
		}
		len = signal_data[1];
		data = signal_data + 2;

		brcmf_dbg(HDRS, "tlv type=%s (%d), len=%d (%d)\n",
			  brcmf_fws_get_tlv_name(type), type, len,
			  brcmf_fws_get_tlv_len(fws, type));

		/* abort parsing when length invalid */
		if (data_len < len + 2)
			break;

		if (len < brcmf_fws_get_tlv_len(fws, type))
			break;

		err = BRCMF_FWS_RET_OK_NOSCHEDULE;
		switch (type) {
		case BRCMF_FWS_TYPE_HOST_REORDER_RXPKTS:
		case BRCMF_FWS_TYPE_COMP_TXSTATUS:
			break;
		case BRCMF_FWS_TYPE_MACDESC_ADD:
		case BRCMF_FWS_TYPE_MACDESC_DEL:
			brcmf_fws_macdesc_indicate(fws, type, data);
			break;
		case BRCMF_FWS_TYPE_MAC_OPEN:
		case BRCMF_FWS_TYPE_MAC_CLOSE:
			err = brcmf_fws_macdesc_state_indicate(fws, type, data);
			break;
		case BRCMF_FWS_TYPE_INTERFACE_OPEN:
		case BRCMF_FWS_TYPE_INTERFACE_CLOSE:
			err = brcmf_fws_interface_state_indicate(fws, type,
								 data);
			break;
		case BRCMF_FWS_TYPE_MAC_REQUEST_CREDIT:
		case BRCMF_FWS_TYPE_MAC_REQUEST_PACKET:
			err = brcmf_fws_request_indicate(fws, type, data);
			break;
		case BRCMF_FWS_TYPE_TXSTATUS:
			brcmf_fws_txstatus_indicate(fws, data);
			break;
		case BRCMF_FWS_TYPE_FIFO_CREDITBACK:
			err = brcmf_fws_fifocreditback_indicate(fws, data);
			break;
		case BRCMF_FWS_TYPE_RSSI:
			brcmf_fws_rssi_indicate(fws, *data);
			break;
		case BRCMF_FWS_TYPE_TRANS_ID:
			brcmf_fws_dbg_seqnum_check(fws, data);
			break;
		case BRCMF_FWS_TYPE_PKTTAG:
		case BRCMF_FWS_TYPE_PENDING_TRAFFIC_BMP:
		default:
			fws->stats.tlv_invalid_type++;
			break;
		}
		if (err == BRCMF_FWS_RET_OK_SCHEDULE)
			status = BRCMF_FWS_RET_OK_SCHEDULE;
		signal_data += len + 2;
		data_len -= len + 2;
	}

	if (data_len != 0)
		fws->stats.tlv_parse_failed++;

	if (status == BRCMF_FWS_RET_OK_SCHEDULE)
		brcmf_fws_schedule_deq(fws);

	/* signalling processing result does
	 * not affect the actual ethernet packet.
	 */
	skb_pull(skb, signal_len);

	/* this may be a signal-only packet
	 */
	if (skb->len == 0)
		fws->stats.header_only_pkt++;

	return 0;
}

static void brcmf_fws_precommit_skb(struct brcmf_fws_info *fws, int fifo,
				   struct sk_buff *p)
{
	struct brcmf_skbuff_cb *skcb = brcmf_skbcb(p);
	struct brcmf_fws_mac_descriptor *entry = skcb->mac;
	u8 flags;

	brcmf_skb_if_flags_set_field(p, TRANSMIT, 1);
	brcmf_skb_htod_tag_set_field(p, GENERATION, entry->generation);
	flags = BRCMF_FWS_HTOD_FLAG_PKTFROMHOST;
	if (brcmf_skb_if_flags_get_field(p, REQUESTED)) {
		/*
		 * Indicate that this packet is being sent in response to an
		 * explicit request from the firmware side.
		 */
		flags |= BRCMF_FWS_HTOD_FLAG_PKT_REQUESTED;
	}
	brcmf_skb_htod_tag_set_field(p, FLAGS, flags);
	brcmf_fws_hdrpush(fws, p);
}

static void brcmf_fws_rollback_toq(struct brcmf_fws_info *fws,
				   struct sk_buff *skb, int fifo)
{
	struct brcmf_fws_mac_descriptor *entry;
	struct sk_buff *pktout;
	int qidx, hslot;
	int rc = 0;

	entry = brcmf_skbcb(skb)->mac;
	if (entry->occupied) {
		qidx = 2 * fifo;
		if (brcmf_skbcb(skb)->state == BRCMF_FWS_SKBSTATE_SUPPRESSED)
			qidx++;

		pktout = brcmu_pktq_penq_head(&entry->psq, qidx, skb);
		if (pktout == NULL) {
			brcmf_err("%s queue %d full\n", entry->name, qidx);
			rc = -ENOSPC;
		}
	} else {
		brcmf_err("%s entry removed\n", entry->name);
		rc = -ENOENT;
	}

	if (rc) {
		fws->stats.rollback_failed++;
		hslot = brcmf_skb_htod_tag_get_field(skb, HSLOT);
		brcmf_fws_txs_process(fws, BRCMF_FWS_TXSTATUS_HOST_TOSSED,
				      hslot, 0);
	} else {
		fws->stats.rollback_success++;
		brcmf_fws_return_credits(fws, fifo, 1);
		brcmf_fws_macdesc_return_req_credit(skb);
	}
}

static int brcmf_fws_borrow_credit(struct brcmf_fws_info *fws)
{
	int lender_ac;

	if (time_after(fws->borrow_defer_timestamp, jiffies)) {
		fws->fifo_credit_map &= ~(1 << BRCMF_FWS_FIFO_AC_BE);
		return -ENAVAIL;
	}

	for (lender_ac = 0; lender_ac <= BRCMF_FWS_FIFO_AC_VO; lender_ac++) {
		if (fws->fifo_credit[lender_ac]) {
			fws->credits_borrowed[lender_ac]++;
			fws->fifo_credit[lender_ac]--;
			if (fws->fifo_credit[lender_ac] == 0)
				fws->fifo_credit_map &= ~(1 << lender_ac);
			fws->fifo_credit_map |= (1 << BRCMF_FWS_FIFO_AC_BE);
			brcmf_dbg(DATA, "borrow credit from: %d\n", lender_ac);
			return 0;
		}
	}
	fws->fifo_credit_map &= ~(1 << BRCMF_FWS_FIFO_AC_BE);
	return -ENAVAIL;
}

static int brcmf_fws_commit_skb(struct brcmf_fws_info *fws, int fifo,
				struct sk_buff *skb)
{
	struct brcmf_skbuff_cb *skcb = brcmf_skbcb(skb);
	struct brcmf_fws_mac_descriptor *entry;
	struct brcmf_bus *bus = fws->drvr->bus_if;
	int rc;
	u8 ifidx;

	entry = skcb->mac;
	if (IS_ERR(entry))
		return PTR_ERR(entry);

	brcmf_fws_precommit_skb(fws, fifo, skb);
	rc = brcmf_bus_txdata(bus, skb);
	brcmf_dbg(DATA, "%s flags %X htod %X bus_tx %d\n", entry->name,
		  skcb->if_flags, skcb->htod, rc);
	if (rc < 0) {
		brcmf_proto_hdrpull(fws->drvr, false, &ifidx, skb);
		goto rollback;
	}

	entry->transit_count++;
	if (entry->suppressed)
		entry->suppr_transit_count++;
	fws->stats.pkt2bus++;
	fws->stats.send_pkts[fifo]++;
	if (brcmf_skb_if_flags_get_field(skb, REQUESTED))
		fws->stats.requested_sent[fifo]++;

	return rc;

rollback:
	brcmf_fws_rollback_toq(fws, skb, fifo);
	return rc;
}

static int brcmf_fws_assign_htod(struct brcmf_fws_info *fws, struct sk_buff *p,
				  int fifo)
{
	struct brcmf_skbuff_cb *skcb = brcmf_skbcb(p);
	int rc, hslot;

	hslot = brcmf_fws_hanger_get_free_slot(&fws->hanger);
	brcmf_skb_htod_tag_set_field(p, HSLOT, hslot);
	brcmf_skb_htod_tag_set_field(p, FREERUN, skcb->mac->seq[fifo]);
	brcmf_skb_htod_tag_set_field(p, FIFO, fifo);
	rc = brcmf_fws_hanger_pushpkt(&fws->hanger, p, hslot);
	if (!rc)
		skcb->mac->seq[fifo]++;
	else
		fws->stats.generic_error++;
	return rc;
}

int brcmf_fws_process_skb(struct brcmf_if *ifp, struct sk_buff *skb)
{
	struct brcmf_pub *drvr = ifp->drvr;
	struct brcmf_fws_info *fws = drvr->fws;
	struct brcmf_skbuff_cb *skcb = brcmf_skbcb(skb);
	struct ethhdr *eh = (struct ethhdr *)(skb->data);
	ulong flags;
	int fifo = BRCMF_FWS_FIFO_BCMC;
	bool multicast = is_multicast_ether_addr(eh->h_dest);
	bool pae = eh->h_proto == htons(ETH_P_PAE);

	/* determine the priority */
	if (!skb->priority)
		skb->priority = cfg80211_classify8021d(skb);

	drvr->tx_multicast += !!multicast;
	if (pae)
		atomic_inc(&ifp->pend_8021x_cnt);

	if (!brcmf_fws_fc_active(fws)) {
		/* If the protocol uses a data header, apply it */
		brcmf_proto_hdrpush(drvr, ifp->ifidx, 0, skb);

		/* Use bus module to send data frame */
		return brcmf_bus_txdata(drvr->bus_if, skb);
	}

	/* set control buffer information */
	skcb->if_flags = 0;
	skcb->state = BRCMF_FWS_SKBSTATE_NEW;
	brcmf_skb_if_flags_set_field(skb, INDEX, ifp->ifidx);
	if (!multicast)
		fifo = brcmf_fws_prio2fifo[skb->priority];

	brcmf_fws_lock(drvr, flags);
	if (fifo != BRCMF_FWS_FIFO_AC_BE && fifo < BRCMF_FWS_FIFO_BCMC)
		fws->borrow_defer_timestamp = jiffies +
					      BRCMF_FWS_BORROW_DEFER_PERIOD;

	skcb->mac = brcmf_fws_macdesc_find(fws, ifp, eh->h_dest);
	brcmf_dbg(DATA, "%s mac %pM multi %d fifo %d\n", skcb->mac->name,
		  eh->h_dest, multicast, fifo);
	if (!brcmf_fws_assign_htod(fws, skb, fifo)) {
		brcmf_fws_enq(fws, BRCMF_FWS_SKBSTATE_DELAYED, fifo, skb);
		brcmf_fws_schedule_deq(fws);
	} else {
		brcmf_err("drop skb: no hanger slot\n");
		if (pae) {
			atomic_dec(&ifp->pend_8021x_cnt);
			if (waitqueue_active(&ifp->pend_8021x_wait))
				wake_up(&ifp->pend_8021x_wait);
		}
		brcmu_pkt_buf_free_skb(skb);
	}
	brcmf_fws_unlock(drvr, flags);
	return 0;
}

void brcmf_fws_reset_interface(struct brcmf_if *ifp)
{
	struct brcmf_fws_mac_descriptor *entry = ifp->fws_desc;

	brcmf_dbg(TRACE, "enter: idx=%d\n", ifp->bssidx);
	if (!entry)
		return;

	brcmf_fws_macdesc_init(entry, ifp->mac_addr, ifp->ifidx);
}

void brcmf_fws_add_interface(struct brcmf_if *ifp)
{
	struct brcmf_fws_info *fws = ifp->drvr->fws;
	struct brcmf_fws_mac_descriptor *entry;

	if (!ifp->ndev || !ifp->drvr->fw_signals)
		return;

	entry = &fws->desc.iface[ifp->ifidx];
	ifp->fws_desc = entry;
	brcmf_fws_macdesc_init(entry, ifp->mac_addr, ifp->ifidx);
	brcmf_fws_macdesc_set_name(fws, entry);
	brcmu_pktq_init(&entry->psq, BRCMF_FWS_PSQ_PREC_COUNT,
			BRCMF_FWS_PSQ_LEN);
	brcmf_dbg(TRACE, "added %s\n", entry->name);
}

void brcmf_fws_del_interface(struct brcmf_if *ifp)
{
	struct brcmf_fws_mac_descriptor *entry = ifp->fws_desc;
	ulong flags;

	if (!entry)
		return;

	brcmf_fws_lock(ifp->drvr, flags);
	ifp->fws_desc = NULL;
	brcmf_dbg(TRACE, "deleting %s\n", entry->name);
	brcmf_fws_macdesc_deinit(entry);
	brcmf_fws_cleanup(ifp->drvr->fws, ifp->ifidx);
	brcmf_fws_unlock(ifp->drvr, flags);
}

static void brcmf_fws_dequeue_worker(struct work_struct *worker)
{
	struct brcmf_fws_info *fws;
	struct sk_buff *skb;
	ulong flags;
	int fifo;

	fws = container_of(worker, struct brcmf_fws_info, fws_dequeue_work);

	brcmf_fws_lock(fws->drvr, flags);
	for (fifo = BRCMF_FWS_FIFO_BCMC; fifo >= 0 && !fws->bus_flow_blocked;
	     fifo--) {
		while ((fws->fifo_credit[fifo]) || ((!fws->bcmc_credit_check) &&
		       (fifo == BRCMF_FWS_FIFO_BCMC))) {
			skb = brcmf_fws_deq(fws, fifo);
			if (!skb)
				break;
			fws->fifo_credit[fifo]--;
			if (brcmf_fws_commit_skb(fws, fifo, skb))
				break;
			if (fws->bus_flow_blocked)
				break;
		}
		if ((fifo == BRCMF_FWS_FIFO_AC_BE) &&
		    (fws->fifo_credit[fifo] == 0) &&
		    (!fws->bus_flow_blocked)) {
			while (brcmf_fws_borrow_credit(fws) == 0) {
				skb = brcmf_fws_deq(fws, fifo);
				if (!skb) {
					brcmf_fws_return_credits(fws, fifo, 1);
					break;
				}
				if (brcmf_fws_commit_skb(fws, fifo, skb))
					break;
				if (fws->bus_flow_blocked)
					break;
			}
		}
	}
	brcmf_fws_unlock(fws->drvr, flags);
}

int brcmf_fws_init(struct brcmf_pub *drvr)
{
	u32 tlv = BRCMF_FWS_FLAGS_RSSI_SIGNALS;
	int rc;

	if (!drvr->fw_signals)
		return 0;

	spin_lock_init(&drvr->fws_spinlock);

	drvr->fws = kzalloc(sizeof(*(drvr->fws)), GFP_KERNEL);
	if (!drvr->fws) {
		rc = -ENOMEM;
		goto fail;
	}

	/* set linkage back */
	drvr->fws->drvr = drvr;
	drvr->fws->fcmode = fcmode;

	drvr->fws->fws_wq = create_singlethread_workqueue("brcmf_fws_wq");
	if (drvr->fws->fws_wq == NULL) {
		brcmf_err("workqueue creation failed\n");
		rc = -EBADF;
		goto fail;
	}
	INIT_WORK(&drvr->fws->fws_dequeue_work, brcmf_fws_dequeue_worker);

	/* enable firmware signalling if fcmode active */
	if (drvr->fws->fcmode != BRCMF_FWS_FCMODE_NONE)
		tlv |= BRCMF_FWS_FLAGS_XONXOFF_SIGNALS |
		       BRCMF_FWS_FLAGS_CREDIT_STATUS_SIGNALS |
		       BRCMF_FWS_FLAGS_HOST_PROPTXSTATUS_ACTIVE;

	rc = brcmf_fweh_register(drvr, BRCMF_E_FIFO_CREDIT_MAP,
				 brcmf_fws_notify_credit_map);
	if (rc < 0) {
		brcmf_err("register credit map handler failed\n");
		goto fail;
	}
	rc = brcmf_fweh_register(drvr, BRCMF_E_BCMC_CREDIT_SUPPORT,
				 brcmf_fws_notify_bcmc_credit_support);
	if (rc < 0) {
		brcmf_err("register bcmc credit handler failed\n");
		brcmf_fweh_unregister(drvr, BRCMF_E_FIFO_CREDIT_MAP);
		goto fail;
	}

	/* setting the iovar may fail if feature is unsupported
	 * so leave the rc as is so driver initialization can
	 * continue.
	 */
	if (brcmf_fil_iovar_int_set(drvr->iflist[0], "tlv", tlv)) {
		brcmf_err("failed to set bdcv2 tlv signaling\n");
		goto fail_event;
	}

	brcmf_fws_hanger_init(&drvr->fws->hanger);
	brcmf_fws_macdesc_init(&drvr->fws->desc.other, NULL, 0);
	brcmf_fws_macdesc_set_name(drvr->fws, &drvr->fws->desc.other);
	brcmu_pktq_init(&drvr->fws->desc.other.psq, BRCMF_FWS_PSQ_PREC_COUNT,
			BRCMF_FWS_PSQ_LEN);

	/* create debugfs file for statistics */
	brcmf_debugfs_create_fws_stats(drvr, &drvr->fws->stats);

	brcmf_dbg(INFO, "%s bdcv2 tlv signaling [%x]\n",
		  drvr->fw_signals ? "enabled" : "disabled", tlv);
	return 0;

fail_event:
	brcmf_fweh_unregister(drvr, BRCMF_E_BCMC_CREDIT_SUPPORT);
	brcmf_fweh_unregister(drvr, BRCMF_E_FIFO_CREDIT_MAP);
fail:
	brcmf_fws_deinit(drvr);
	return rc;
}

void brcmf_fws_deinit(struct brcmf_pub *drvr)
{
	struct brcmf_fws_info *fws = drvr->fws;
	ulong flags;

	if (!fws)
		return;

	/* disable firmware signalling entirely
	 * to avoid using the workqueue.
	 */
	drvr->fw_signals = false;

	if (drvr->fws->fws_wq)
		destroy_workqueue(drvr->fws->fws_wq);

	/* cleanup */
	brcmf_fws_lock(drvr, flags);
	brcmf_fws_cleanup(fws, -1);
	drvr->fws = NULL;
	brcmf_fws_unlock(drvr, flags);

	/* free top structure */
	kfree(fws);
}

bool brcmf_fws_fc_active(struct brcmf_fws_info *fws)
{
	if (!fws)
		return false;

	return fws->fcmode != BRCMF_FWS_FCMODE_NONE;
}

void brcmf_fws_bustxfail(struct brcmf_fws_info *fws, struct sk_buff *skb)
{
	ulong flags;
	u32 hslot;

	if (brcmf_skbcb(skb)->state == BRCMF_FWS_SKBSTATE_TIM) {
		brcmu_pkt_buf_free_skb(skb);
		return;
	}
	brcmf_fws_lock(fws->drvr, flags);
	hslot = brcmf_skb_htod_tag_get_field(skb, HSLOT);
	brcmf_fws_txs_process(fws, BRCMF_FWS_TXSTATUS_HOST_TOSSED, hslot, 0);
	brcmf_fws_unlock(fws->drvr, flags);
}

void brcmf_fws_bus_blocked(struct brcmf_pub *drvr, bool flow_blocked)
{
	struct brcmf_fws_info *fws = drvr->fws;

	fws->bus_flow_blocked = flow_blocked;
	if (!flow_blocked)
		brcmf_fws_schedule_deq(fws);
	else
		fws->stats.bus_flow_block++;
}
