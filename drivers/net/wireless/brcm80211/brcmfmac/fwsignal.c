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

/**
 * enum brcmf_fws_tlv_len - length values for tlvs.
 */
#define BRCMF_FWS_TLV_DEF(name, id, len) \
	BRCMF_FWS_TYPE_ ## name ## _LEN = len,
enum brcmf_fws_tlv_len {
	BRCMF_FWS_TLV_DEFLIST
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
#define BRCMF_FWS_FLAGS_RSSI_SIGNALS				0x0001
#define BRCMF_FWS_FLAGS_XONXOFF_SIGNALS				0x0002
#define BRCMF_FWS_FLAGS_CREDIT_STATUS_SIGNALS			0x0004
#define BRCMF_FWS_FLAGS_HOST_PROPTXSTATUS_ACTIVE		0x0008
#define BRCMF_FWS_FLAGS_PSQ_GENERATIONFSM_ENABLE		0x0010
#define BRCMF_FWS_FLAGS_PSQ_ZERO_BUFFER_ENABLE			0x0020
#define BRCMF_FWS_FLAGS_HOST_RXREORDER_ACTIVE			0x0040

#define BRCMF_FWS_HANGER_MAXITEMS				1024
#define BRCMF_FWS_HANGER_ITEM_STATE_FREE			1
#define BRCMF_FWS_HANGER_ITEM_STATE_INUSE			2
#define BRCMF_FWS_HANGER_ITEM_STATE_INUSE_SUPPRESSED		3

#define BRCMF_FWS_STATE_OPEN					1
#define BRCMF_FWS_STATE_CLOSE				2

#define BRCMF_FWS_FCMODE_NONE				0
#define BRCMF_FWS_FCMODE_IMPLIED_CREDIT			1
#define BRCMF_FWS_FCMODE_EXPLICIT_CREDIT			2

#define BRCMF_FWS_MAC_DESC_TABLE_SIZE			32
#define BRCMF_FWS_MAX_IFNUM					16
#define BRCMF_FWS_MAC_DESC_ID_INVALID			0xff

#define BRCMF_FWS_HOSTIF_FLOWSTATE_OFF			0
#define BRCMF_FWS_HOSTIF_FLOWSTATE_ON			1

/**
 * FWFC packet identifier
 *
 * 32-bit packet identifier used in PKTTAG tlv from host to dongle.
 *
 * - Generated at the host (e.g. dhd)
 * - Seen as a generic sequence number by wlc except the flags field
 *
 * Generation	: b[31]	=> generation number for this packet [host->fw]
 *			   OR, current generation number [fw->host]
 * Flags	: b[30:27] => command, status flags
 * FIFO-AC	: b[26:24] => AC-FIFO id
 * h-slot	: b[23:8] => hanger-slot
 * freerun	: b[7:0] => A free running counter
 */
#define BRCMF_FWS_PKTTAG_GENERATION_MASK		0x80000000
#define BRCMF_FWS_PKTTAG_GENERATION_SHIFT		31
#define BRCMF_FWS_PKTTAG_FLAGS_MASK			0x78000000
#define BRCMF_FWS_PKTTAG_FLAGS_SHIFT			27
#define BRCMF_FWS_PKTTAG_FIFO_MASK			0x07000000
#define BRCMF_FWS_PKTTAG_FIFO_SHIFT			24
#define BRCMF_FWS_PKTTAG_HSLOT_MASK			0x00ffff00
#define BRCMF_FWS_PKTTAG_HSLOT_SHIFT			8
#define BRCMF_FWS_PKTTAG_FREERUN_MASK			0x000000ff
#define BRCMF_FWS_PKTTAG_FREERUN_SHIFT			0

#define brcmf_fws_pkttag_set_field(var, field, value) \
	brcmu_maskset32((var), BRCMF_FWS_PKTTAG_ ## field ## _MASK, \
			     BRCMF_FWS_PKTTAG_ ## field ## _SHIFT, (value))
#define brcmf_fws_pkttag_get_field(var, field) \
	brcmu_maskget32((var), BRCMF_FWS_PKTTAG_ ## field ## _MASK, \
			     BRCMF_FWS_PKTTAG_ ## field ## _SHIFT)

struct brcmf_fws_info {
	struct brcmf_pub *drvr;
	struct brcmf_fws_stats stats;
};

static int brcmf_fws_rssi_indicate(struct brcmf_fws_info *fws, s8 rssi)
{
	brcmf_dbg(CTL, "rssi %d\n", rssi);
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

int brcmf_fws_init(struct brcmf_pub *drvr)
{
	u32 tlv;
	int rc;

	/* enable rssi signals */
	tlv = drvr->fw_signals ? BRCMF_FWS_FLAGS_RSSI_SIGNALS : 0;

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
	/* set linkage back */
	drvr->fws->drvr = drvr;

	/* create debugfs file for statistics */
	brcmf_debugfs_create_fws_stats(drvr, &drvr->fws->stats);

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
	/* free top structure */
	kfree(drvr->fws);
	drvr->fws = NULL;
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

		brcmf_dbg(INFO, "tlv type=%d (%s), len=%d\n", type,
			  brcmf_fws_get_tlv_name(type), len);
		switch (type) {
		case BRCMF_FWS_TYPE_MAC_OPEN:
		case BRCMF_FWS_TYPE_MAC_CLOSE:
			WARN_ON(len != BRCMF_FWS_TYPE_MAC_OPEN_LEN);
			break;
		case BRCMF_FWS_TYPE_MAC_REQUEST_CREDIT:
			WARN_ON(len != BRCMF_FWS_TYPE_MAC_REQUEST_CREDIT_LEN);
			break;
		case BRCMF_FWS_TYPE_TXSTATUS:
			WARN_ON(len != BRCMF_FWS_TYPE_TXSTATUS_LEN);
			break;
		case BRCMF_FWS_TYPE_PKTTAG:
			WARN_ON(len != BRCMF_FWS_TYPE_PKTTAG_LEN);
			break;
		case BRCMF_FWS_TYPE_MACDESC_ADD:
		case BRCMF_FWS_TYPE_MACDESC_DEL:
			WARN_ON(len != BRCMF_FWS_TYPE_MACDESC_ADD_LEN);
			break;
		case BRCMF_FWS_TYPE_RSSI:
			WARN_ON(len != BRCMF_FWS_TYPE_RSSI_LEN);
			brcmf_fws_rssi_indicate(fws, *(s8 *)data);
			break;
		case BRCMF_FWS_TYPE_INTERFACE_OPEN:
		case BRCMF_FWS_TYPE_INTERFACE_CLOSE:
			WARN_ON(len != BRCMF_FWS_TYPE_INTERFACE_OPEN_LEN);
			break;
		case BRCMF_FWS_TYPE_FIFO_CREDITBACK:
			WARN_ON(len != BRCMF_FWS_TYPE_FIFO_CREDITBACK_LEN);
			break;
		case BRCMF_FWS_TYPE_PENDING_TRAFFIC_BMP:
			WARN_ON(len != BRCMF_FWS_TYPE_PENDING_TRAFFIC_BMP_LEN);
			break;
		case BRCMF_FWS_TYPE_MAC_REQUEST_PACKET:
			WARN_ON(len != BRCMF_FWS_TYPE_MAC_REQUEST_PACKET_LEN);
			break;
		case BRCMF_FWS_TYPE_HOST_REORDER_RXPKTS:
			WARN_ON(len != BRCMF_FWS_TYPE_HOST_REORDER_RXPKTS_LEN);
			break;
		case BRCMF_FWS_TYPE_TRANS_ID:
			WARN_ON(len != BRCMF_FWS_TYPE_TRANS_ID_LEN);
			brcmf_fws_dbg_seqnum_check(fws, data);
			break;
		case BRCMF_FWS_TYPE_COMP_TXSTATUS:
			WARN_ON(len != BRCMF_FWS_TYPE_COMP_TXSTATUS_LEN);
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
