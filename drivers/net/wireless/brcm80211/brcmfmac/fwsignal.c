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
#include <linux/if_ether.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/err.h>
#include <uapi/linux/nl80211.h>

#include <brcmu_utils.h>
#include <brcmu_wifi.h>
#include "dhd.h"
#include "dhd_dbg.h"
#include "fwil.h"
#include "fweh.h"
#include "fwsignal.h"

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
	BRCMF_FWS_TLV_DEF(FIFO_CREDITBACK, 11, 8) \
	BRCMF_FWS_TLV_DEF(PENDING_TRAFFIC_BMP, 12, 2) \
	BRCMF_FWS_TLV_DEF(MAC_REQUEST_PACKET, 13, 3) \
	BRCMF_FWS_TLV_DEF(HOST_REORDER_RXPKTS, 14, 10) \
	BRCMF_FWS_TLV_DEF(TRANS_ID, 18, 6) \
	BRCMF_FWS_TLV_DEF(COMP_TXSTATUS, 19, 1) \
	BRCMF_FWS_TLV_DEF(FILLER, 255, 0)

/**
 * enum brcmf_fws_tlv_type - definition of tlv identifiers.
 */
#define BRCMF_FWS_TLV_DEF(name, id, len) \
	BRCMF_FWS_TYPE_ ## name =  id,
enum brcmf_fws_tlv_type {
	BRCMF_FWS_TLV_DEFLIST
	BRCMF_FWS_TYPE_INVALID
};
#undef BRCMF_FWS_TLV_DEF

#ifdef DEBUG
/**
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

/**
 * flags used to enable tlv signalling from firmware.
 */
#define BRCMF_FWS_FLAGS_RSSI_SIGNALS			0x0001
#define BRCMF_FWS_FLAGS_XONXOFF_SIGNALS			0x0002
#define BRCMF_FWS_FLAGS_CREDIT_STATUS_SIGNALS		0x0004
#define BRCMF_FWS_FLAGS_HOST_PROPTXSTATUS_ACTIVE	0x0008
#define BRCMF_FWS_FLAGS_PSQ_GENERATIONFSM_ENABLE	0x0010
#define BRCMF_FWS_FLAGS_PSQ_ZERO_BUFFER_ENABLE		0x0020
#define BRCMF_FWS_FLAGS_HOST_RXREORDER_ACTIVE		0x0040

#define BRCMF_FWS_HANGER_MAXITEMS			1024
#define BRCMF_FWS_HANGER_ITEM_STATE_FREE		1
#define BRCMF_FWS_HANGER_ITEM_STATE_INUSE		2
#define BRCMF_FWS_HANGER_ITEM_STATE_INUSE_SUPPRESSED	3

#define BRCMF_FWS_STATE_OPEN				1
#define BRCMF_FWS_STATE_CLOSE				2

#define BRCMF_FWS_FCMODE_NONE				0
#define BRCMF_FWS_FCMODE_IMPLIED_CREDIT			1
#define BRCMF_FWS_FCMODE_EXPLICIT_CREDIT		2

#define BRCMF_FWS_MAC_DESC_TABLE_SIZE			32
#define BRCMF_FWS_MAX_IFNUM				16
#define BRCMF_FWS_MAC_DESC_ID_INVALID			0xff

#define BRCMF_FWS_HOSTIF_FLOWSTATE_OFF			0
#define BRCMF_FWS_HOSTIF_FLOWSTATE_ON			1

#define BRCMF_FWS_PSQ_PREC_COUNT		((NL80211_NUM_ACS + 1) * 2)
#define BRCMF_FWS_PSQ_LEN				256

/**
 * enum brcmf_fws_skb_state - indicates processing state of skb.
 */
enum brcmf_fws_skb_state {
	WLFC_PKTTYPE_NEW,
	WLFC_PKTTYPE_DELAYED,
	WLFC_PKTTYPE_SUPPRESSED,
	WLFC_PKTTYPE_MAX
};

/**
 * struct brcmf_skbuff_cb - control buffer associated with skbuff.
 *
 * @if_flags: holds interface index and packet related flags.
 * @da: destination MAC address extracted from skbuff once.
 * @htod: host to device packet identifier (used in PKTTAG tlv).
 * @needs_hdr: the packet does not yet have a BDC header.
 * @state: transmit state of the packet.
 * @mac: descriptor related to destination for this packet.
 *
 * This information is stored in control buffer struct sk_buff::cb, which
 * provides 48 bytes of storage so this structure should not exceed that.
 */
struct brcmf_skbuff_cb {
	u16 if_flags;
	u8 da[ETH_ALEN];
	u32 htod;
	u8 needs_hdr;
	enum brcmf_fws_skb_state state;
	struct brcmf_fws_mac_descriptor *mac;
};

/**
 * macro casting skbuff control buffer to struct brcmf_skbuff_cb.
 */
#define brcmf_skbcb(skb)	((struct brcmf_skbuff_cb *)((skb)->cb))

/**
 * sk_buff control if flags
 *
 *	b[11]  - packet sent upon firmware request.
 *	b[10]  - packet only contains signalling data.
 *	b[9]   - packet is a tx packet.
 *	b[8]   - packet uses FIFO credit (non-pspoll).
 *	b[7]   - interface in AP mode.
 *	b[6:4] - AC FIFO number.
 *	b[3:0] - interface index.
 */
#define BRCMF_SKB_IF_FLAGS_REQUESTED_MASK	0x0800
#define BRCMF_SKB_IF_FLAGS_REQUESTED_SHIFT	11
#define BRCMF_SKB_IF_FLAGS_SIGNAL_ONLY_MASK	0x0400
#define BRCMF_SKB_IF_FLAGS_SIGNAL_ONLY_SHIFT	10
#define BRCMF_SKB_IF_FLAGS_TRANSMIT_MASK        0x0200
#define BRCMF_SKB_IF_FLAGS_TRANSMIT_SHIFT	9
#define BRCMF_SKB_IF_FLAGS_CREDITCHECK_MASK	0x0100
#define BRCMF_SKB_IF_FLAGS_CREDITCHECK_SHIFT	8
#define BRCMF_SKB_IF_FLAGS_IF_AP_MASK		0x0080
#define BRCMF_SKB_IF_FLAGS_IF_AP_SHIFT		7
#define BRCMF_SKB_IF_FLAGS_FIFO_MASK		0x0070
#define BRCMF_SKB_IF_FLAGS_FIFO_SHIFT		4
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

/**
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
#define BRCMF_SKB_HTOD_TAG_FREERUN_SHIFT			0

#define brcmf_skb_htod_tag_set_field(skb, field, value) \
	brcmu_maskset32(&(brcmf_skbcb(skb)->htod_tag), \
			BRCMF_SKB_HTOD_TAG_ ## field ## _MASK, \
			BRCMF_SKB_HTOD_TAG_ ## field ## _SHIFT, (value))
#define brcmf_skb_htod_tag_get_field(skb, field) \
	brcmu_maskget32(brcmf_skbcb(skb)->htod_tag, \
			BRCMF_SKB_HTOD_TAG_ ## field ## _MASK, \
			BRCMF_SKB_HTOD_TAG_ ## field ## _SHIFT)

/**
 * struct brcmf_fws_mac_descriptor - firmware signalling data per node/interface
 *
 * @occupied: slot is in use.
 * @mac_handle: handle for mac entry determined by firmware.
 * @interface_id: interface index.
 * @state: current state.
 * @ac_bitmap: ac queue bitmap.
 * @requested_credit: credits requested by firmware.
 * @ea: ethernet address.
 * @psq: power-save queue.
 */
struct brcmf_fws_mac_descriptor {
	u8 occupied;
	u8 mac_handle;
	u8 interface_id;
	u8 state;
	u8 ac_bitmap;
	u8 requested_credit;
	u8 ea[ETH_ALEN];
	struct pktq psq;
};

struct brcmf_fws_info {
	struct brcmf_pub *drvr;
	struct brcmf_fws_stats stats;
	struct brcmf_fws_mac_descriptor nodes[BRCMF_FWS_MAC_DESC_TABLE_SIZE];
	struct brcmf_fws_mac_descriptor other;
	int fifo_credit[NL80211_NUM_ACS+1+1];
};

/**
 * brcmf_fws_get_tlv_len() - returns defined length for given tlv id.
 */
#define BRCMF_FWS_TLV_DEF(name, id, len) \
	case BRCMF_FWS_TYPE_ ## name: \
		return len;

static int brcmf_fws_get_tlv_len(struct brcmf_fws_info *fws,
				 enum brcmf_fws_tlv_type id)
{
	switch (id) {
	BRCMF_FWS_TLV_DEFLIST
	default:
		brcmf_err("invalid tlv id: %d\n", id);
		fws->stats.tlv_invalid_type++;
		break;
	}
	return -EINVAL;
}
#undef BRCMF_FWS_TLV_DEF

static void brcmf_fws_init_mac_descriptor(struct brcmf_fws_mac_descriptor *desc,
					  u8 *addr, u8 ifidx)
{
	brcmf_dbg(TRACE, "enter: ea=%pM, ifidx=%u\n", addr, ifidx);
	desc->occupied = 1;
	desc->state = BRCMF_FWS_STATE_OPEN;
	desc->requested_credit = 0;
	/* depending on use may need ifp->bssidx instead */
	desc->interface_id = ifidx;
	desc->ac_bitmap = 0xff; /* update this when handling APSD */
	memcpy(&desc->ea[0], addr, ETH_ALEN);
}

static
void brcmf_fws_clear_mac_descriptor(struct brcmf_fws_mac_descriptor *desc)
{
	brcmf_dbg(TRACE,
		  "enter: ea=%pM, ifidx=%u\n", desc->ea, desc->interface_id);
	desc->occupied = 0;
	desc->state = BRCMF_FWS_STATE_CLOSE;
	desc->requested_credit = 0;
}

static struct brcmf_fws_mac_descriptor *
brcmf_fws_mac_descriptor_lookup(struct brcmf_fws_info *fws, u8 *ea)
{
	struct brcmf_fws_mac_descriptor *entry;
	int i;

	brcmf_dbg(TRACE, "enter: ea=%pM\n", ea);
	if (ea == NULL)
		return ERR_PTR(-EINVAL);

	entry = &fws->nodes[0];
	for (i = 0; i < ARRAY_SIZE(fws->nodes); i++) {
		if (entry->occupied && !memcmp(entry->ea, ea, ETH_ALEN))
			return entry;
		entry++;
	}

	return ERR_PTR(-ENOENT);
}

static void brcmf_fws_mac_desc_cleanup(struct brcmf_fws_mac_descriptor *entry,
				       bool (*fn)(struct sk_buff *, void *),
				       int ifidx)
{
	brcmf_dbg(TRACE, "enter: entry=(ea=%pM,ifid=%d), ifidx=%d\n",
		  entry->ea, entry->interface_id, ifidx);
	if (entry->occupied && (fn == NULL || (ifidx == entry->interface_id))) {
		brcmf_dbg(TRACE, "flush delayQ: ifidx=%d, qlen=%d\n",
			  ifidx, entry->psq.len);
		/* release packets held in DELAYQ */
		brcmu_pktq_flush(&entry->psq, true, fn, &ifidx);
		entry->occupied = !!(entry->psq.len);
	}
}

static bool brcmf_fws_ifidx_match(struct sk_buff *skb, void *arg)
{
	u32 ifidx = brcmf_skb_if_flags_get_field(skb, INDEX);
	return ifidx == *(int *)arg;
}

static void brcmf_fws_cleanup(struct brcmf_fws_info *fws, int ifidx)
{
	int i;
	struct brcmf_fws_mac_descriptor *table;
	bool (*matchfn)(struct sk_buff *, void *) = NULL;

	brcmf_dbg(TRACE, "enter: ifidx=%d\n", ifidx);
	if (fws == NULL)
		return;

	if (ifidx != -1)
		matchfn = brcmf_fws_ifidx_match;

	/* cleanup individual nodes */
	table = &fws->nodes[0];
	for (i = 0; i < ARRAY_SIZE(fws->nodes); i++)
		brcmf_fws_mac_desc_cleanup(&table[i], matchfn, ifidx);

	brcmf_fws_mac_desc_cleanup(&fws->other, matchfn, ifidx);
}

static int brcmf_fws_rssi_indicate(struct brcmf_fws_info *fws, s8 rssi)
{
	brcmf_dbg(CTL, "rssi %d\n", rssi);
	return 0;
}

static
int brcmf_fws_macdesc_indicate(struct brcmf_fws_info *fws, u8 type, u8 *data)
{
	struct brcmf_fws_mac_descriptor *entry, *existing;
	u8 mac_handle;
	u8 ifidx;
	u8 *addr;

	mac_handle = *data++;
	ifidx = *data++;
	addr = data;

	entry = &fws->nodes[mac_handle & 0x1F];
	if (type == BRCMF_FWS_TYPE_MACDESC_DEL) {
		brcmf_dbg(TRACE, "deleting mac %pM idx %d\n", addr, ifidx);
		if (entry->occupied) {
			entry->occupied = 0;
			entry->state = BRCMF_FWS_STATE_CLOSE;
			entry->requested_credit = 0;
		} else {
			fws->stats.mac_update_failed++;
		}
		return 0;
	}

	brcmf_dbg(TRACE, "add mac %pM idx %d\n", addr, ifidx);
	existing = brcmf_fws_mac_descriptor_lookup(fws, addr);
	if (IS_ERR(existing)) {
		if (!entry->occupied) {
			entry->mac_handle = mac_handle;
			brcmf_fws_init_mac_descriptor(entry, addr, ifidx);
			brcmu_pktq_init(&entry->psq, BRCMF_FWS_PSQ_PREC_COUNT,
					BRCMF_FWS_PSQ_LEN);
		} else {
			fws->stats.mac_update_failed++;
		}
	} else {
		if (entry != existing) {
			brcmf_dbg(TRACE, "relocate mac\n");
			memcpy(entry, existing,
			       offsetof(struct brcmf_fws_mac_descriptor, psq));
			entry->mac_handle = mac_handle;
			brcmf_fws_clear_mac_descriptor(existing);
		} else {
			brcmf_dbg(TRACE, "use existing\n");
			WARN_ON(entry->mac_handle != mac_handle);
			/* TODO: what should we do here: continue, reinit, .. */
		}
	}
	return 0;
}

static int brcmf_fws_dbg_seqnum_check(struct brcmf_fws_info *fws, u8 *data)
{
	__le32 timestamp;

	memcpy(&timestamp, &data[2], sizeof(timestamp));
	brcmf_dbg(INFO, "received: seq %d, timestamp %d\n", data[1],
		  le32_to_cpu(timestamp));
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

static int brcmf_fws_notify_credit_map(struct brcmf_if *ifp,
				       const struct brcmf_event_msg *e,
				       void *data)
{
	struct brcmf_fws_info *fws = ifp->drvr->fws;
	int i;
	ulong flags;
	u8 *credits = data;

	brcmf_fws_lock(ifp->drvr, flags);
	for (i = 0; i < ARRAY_SIZE(fws->fifo_credit); i++)
		fws->fifo_credit[i] = *credits++;
	brcmf_fws_unlock(ifp->drvr, flags);
	return 0;
}

int brcmf_fws_init(struct brcmf_pub *drvr)
{
	u32 tlv = 0;
	int rc;

	/* enable rssi signals */
	if (drvr->fw_signals)
		tlv = BRCMF_FWS_FLAGS_RSSI_SIGNALS |
		      BRCMF_FWS_FLAGS_XONXOFF_SIGNALS;

	spin_lock_init(&drvr->fws_spinlock);

	drvr->fws = kzalloc(sizeof(*(drvr->fws)), GFP_KERNEL);
	if (!drvr->fws) {
		rc = -ENOMEM;
		goto fail;
	}

	/* enable proptxtstatus signaling by default */
	rc = brcmf_fil_iovar_int_set(drvr->iflist[0], "tlv", tlv);
	if (rc < 0) {
		brcmf_err("failed to set bdcv2 tlv signaling\n");
		goto fail;
	}

	if (brcmf_fweh_register(drvr, BRCMF_E_FIFO_CREDIT_MAP,
				brcmf_fws_notify_credit_map)) {
		brcmf_err("register credit map handler failed\n");
		goto fail;
	}

	/* create debugfs file for statistics */
	brcmf_debugfs_create_fws_stats(drvr, &drvr->fws->stats);

	/* set linkage back */
	drvr->fws->drvr = drvr;

	/* TODO: remove upon feature delivery */
	brcmf_err("%s bdcv2 tlv signaling [%x]\n",
		  drvr->fw_signals ? "enabled" : "disabled", tlv);
	return 0;

fail:
	/* disable flow control entirely */
	drvr->fw_signals = false;
	brcmf_fws_deinit(drvr);
	return rc;
}

void brcmf_fws_deinit(struct brcmf_pub *drvr)
{
	struct brcmf_fws_info *fws = drvr->fws;
	ulong flags;

	/* cleanup */
	brcmf_fws_lock(drvr, flags);
	brcmf_fws_cleanup(fws, -1);
	drvr->fws = NULL;
	brcmf_fws_unlock(drvr, flags);

	/* free top structure */
	kfree(fws);
}

int brcmf_fws_hdrpull(struct brcmf_pub *drvr, int ifidx, s16 signal_len,
		      struct sk_buff *skb)
{
	struct brcmf_fws_info *fws = drvr->fws;
	ulong flags;
	u8 *signal_data;
	s16 data_len;
	u8 type;
	u8 len;
	u8 *data;

	brcmf_dbg(TRACE, "enter: ifidx %d, skblen %u, sig %d\n",
		  ifidx, skb->len, signal_len);

	WARN_ON(signal_len > skb->len);

	/* if flow control disabled, skip to packet data and leave */
	if (!signal_len || !drvr->fw_signals) {
		skb_pull(skb, signal_len);
		return 0;
	}

	/* lock during tlv parsing */
	brcmf_fws_lock(drvr, flags);

	fws->stats.header_pulls++;
	data_len = signal_len;
	signal_data = skb->data;

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

		/* abort parsing when length invalid */
		if (data_len < len + 2)
			break;

		if (len != brcmf_fws_get_tlv_len(fws, type))
			break;

		brcmf_dbg(INFO, "tlv type=%d (%s), len=%d\n", type,
			  brcmf_fws_get_tlv_name(type), len);
		switch (type) {
		case BRCMF_FWS_TYPE_MAC_OPEN:
		case BRCMF_FWS_TYPE_MAC_CLOSE:
		case BRCMF_FWS_TYPE_MAC_REQUEST_CREDIT:
		case BRCMF_FWS_TYPE_TXSTATUS:
		case BRCMF_FWS_TYPE_PKTTAG:
		case BRCMF_FWS_TYPE_INTERFACE_OPEN:
		case BRCMF_FWS_TYPE_INTERFACE_CLOSE:
		case BRCMF_FWS_TYPE_FIFO_CREDITBACK:
		case BRCMF_FWS_TYPE_PENDING_TRAFFIC_BMP:
		case BRCMF_FWS_TYPE_MAC_REQUEST_PACKET:
		case BRCMF_FWS_TYPE_HOST_REORDER_RXPKTS:
		case BRCMF_FWS_TYPE_COMP_TXSTATUS:
			break;
		case BRCMF_FWS_TYPE_MACDESC_ADD:
		case BRCMF_FWS_TYPE_MACDESC_DEL:
			brcmf_fws_macdesc_indicate(fws, type, data);
			break;
		case BRCMF_FWS_TYPE_RSSI:
			brcmf_fws_rssi_indicate(fws, *data);
			break;
		case BRCMF_FWS_TYPE_TRANS_ID:
			brcmf_fws_dbg_seqnum_check(fws, data);
			break;
		default:
			fws->stats.tlv_invalid_type++;
			break;
		}

		signal_data += len + 2;
		data_len -= len + 2;
	}

	if (data_len != 0)
		fws->stats.tlv_parse_failed++;

	/* signalling processing result does
	 * not affect the actual ethernet packet.
	 */
	skb_pull(skb, signal_len);

	/* this may be a signal-only packet
	 */
	if (skb->len == 0)
		fws->stats.header_only_pkt++;

	brcmf_fws_unlock(drvr, flags);
	return 0;
}

void brcmf_fws_reset_interface(struct brcmf_if *ifp)
{
	struct brcmf_fws_mac_descriptor *entry = ifp->fws_desc;

	brcmf_dbg(TRACE, "enter: idx=%d\n", ifp->bssidx);
	if (!entry)
		return;

	brcmf_fws_init_mac_descriptor(entry, ifp->mac_addr, ifp->ifidx);
}

void brcmf_fws_add_interface(struct brcmf_if *ifp)
{
	struct brcmf_fws_mac_descriptor *entry;

	brcmf_dbg(TRACE, "enter: idx=%d, mac=%pM\n",
		  ifp->bssidx, ifp->mac_addr);
	if (!ifp->drvr->fw_signals)
		return;

	entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
	if (entry) {
		ifp->fws_desc = entry;
		brcmf_fws_init_mac_descriptor(entry, ifp->mac_addr, ifp->ifidx);
		brcmu_pktq_init(&entry->psq, BRCMF_FWS_PSQ_PREC_COUNT,
				BRCMF_FWS_PSQ_LEN);
	} else {
		brcmf_err("no firmware signalling\n");
	}
}

void brcmf_fws_del_interface(struct brcmf_if *ifp)
{
	struct brcmf_fws_mac_descriptor *entry = ifp->fws_desc;

	brcmf_dbg(TRACE, "enter: idx=%d\n", ifp->bssidx);
	if (!entry)
		return;

	ifp->fws_desc = NULL;
	brcmf_fws_clear_mac_descriptor(entry);
	brcmf_fws_cleanup(ifp->drvr->fws, ifp->ifidx);
	kfree(entry);
}
