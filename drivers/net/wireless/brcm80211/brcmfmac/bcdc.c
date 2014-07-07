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

/*******************************************************************************
 * Communicates with the dongle by using dcmd codes.
 * For certain dcmd codes, the dongle interprets string data from the host.
 ******************************************************************************/

#include <linux/types.h>
#include <linux/netdevice.h>

#include <brcmu_utils.h>
#include <brcmu_wifi.h>

#include "dhd.h"
#include "dhd_bus.h"
#include "fwsignal.h"
#include "dhd_dbg.h"
#include "tracepoint.h"
#include "proto.h"
#include "bcdc.h"

struct brcmf_proto_bcdc_dcmd {
	__le32 cmd;	/* dongle command value */
	__le32 len;	/* lower 16: output buflen;
			 * upper 16: input buflen (excludes header) */
	__le32 flags;	/* flag defns given below */
	__le32 status;	/* status code returned from the device */
};

/* BCDC flag definitions */
#define BCDC_DCMD_ERROR		0x01		/* 1=cmd failed */
#define BCDC_DCMD_SET		0x02		/* 0=get, 1=set cmd */
#define BCDC_DCMD_IF_MASK	0xF000		/* I/F index */
#define BCDC_DCMD_IF_SHIFT	12
#define BCDC_DCMD_ID_MASK	0xFFFF0000	/* id an cmd pairing */
#define BCDC_DCMD_ID_SHIFT	16		/* ID Mask shift bits */
#define BCDC_DCMD_ID(flags)	\
	(((flags) & BCDC_DCMD_ID_MASK) >> BCDC_DCMD_ID_SHIFT)

/*
 * BCDC header - Broadcom specific extension of CDC.
 * Used on data packets to convey priority across USB.
 */
#define	BCDC_HEADER_LEN		4
#define BCDC_PROTO_VER		2	/* Protocol version */
#define BCDC_FLAG_VER_MASK	0xf0	/* Protocol version mask */
#define BCDC_FLAG_VER_SHIFT	4	/* Protocol version shift */
#define BCDC_FLAG_SUM_GOOD	0x04	/* Good RX checksums */
#define BCDC_FLAG_SUM_NEEDED	0x08	/* Dongle needs to do TX checksums */
#define BCDC_PRIORITY_MASK	0x7
#define BCDC_FLAG2_IF_MASK	0x0f	/* packet rx interface in APSTA */
#define BCDC_FLAG2_IF_SHIFT	0

#define BCDC_GET_IF_IDX(hdr) \
	((int)((((hdr)->flags2) & BCDC_FLAG2_IF_MASK) >> BCDC_FLAG2_IF_SHIFT))
#define BCDC_SET_IF_IDX(hdr, idx) \
	((hdr)->flags2 = (((hdr)->flags2 & ~BCDC_FLAG2_IF_MASK) | \
	((idx) << BCDC_FLAG2_IF_SHIFT)))

/**
 * struct brcmf_proto_bcdc_header - BCDC header format
 *
 * @flags: flags contain protocol and checksum info.
 * @priority: 802.1d priority and USB flow control info (bit 4:7).
 * @flags2: additional flags containing dongle interface index.
 * @data_offset: start of packet data. header is following by firmware signals.
 */
struct brcmf_proto_bcdc_header {
	u8 flags;
	u8 priority;
	u8 flags2;
	u8 data_offset;
};

/*
 * maximum length of firmware signal data between
 * the BCDC header and packet data in the tx path.
 */
#define BRCMF_PROT_FW_SIGNAL_MAX_TXBYTES	12

#define RETRIES 2 /* # of retries to retrieve matching dcmd response */
#define BUS_HEADER_LEN	(16+64)		/* Must be atleast SDPCM_RESERVE
					 * (amount of header tha might be added)
					 * plus any space that might be needed
					 * for bus alignment padding.
					 */
struct brcmf_bcdc {
	u16 reqid;
	u8 bus_header[BUS_HEADER_LEN];
	struct brcmf_proto_bcdc_dcmd msg;
	unsigned char buf[BRCMF_DCMD_MAXLEN];
};


static int
brcmf_proto_bcdc_msg(struct brcmf_pub *drvr, int ifidx, uint cmd, void *buf,
		     uint len, bool set)
{
	struct brcmf_bcdc *bcdc = (struct brcmf_bcdc *)drvr->proto->pd;
	struct brcmf_proto_bcdc_dcmd *msg = &bcdc->msg;
	u32 flags;

	brcmf_dbg(BCDC, "Enter\n");

	memset(msg, 0, sizeof(struct brcmf_proto_bcdc_dcmd));

	msg->cmd = cpu_to_le32(cmd);
	msg->len = cpu_to_le32(len);
	flags = (++bcdc->reqid << BCDC_DCMD_ID_SHIFT);
	if (set)
		flags |= BCDC_DCMD_SET;
	flags = (flags & ~BCDC_DCMD_IF_MASK) |
		(ifidx << BCDC_DCMD_IF_SHIFT);
	msg->flags = cpu_to_le32(flags);

	if (buf)
		memcpy(bcdc->buf, buf, len);

	len += sizeof(*msg);
	if (len > BRCMF_TX_IOCTL_MAX_MSG_SIZE)
		len = BRCMF_TX_IOCTL_MAX_MSG_SIZE;

	/* Send request */
	return brcmf_bus_txctl(drvr->bus_if, (unsigned char *)&bcdc->msg, len);
}

static int brcmf_proto_bcdc_cmplt(struct brcmf_pub *drvr, u32 id, u32 len)
{
	int ret;
	struct brcmf_bcdc *bcdc = (struct brcmf_bcdc *)drvr->proto->pd;

	brcmf_dbg(BCDC, "Enter\n");
	len += sizeof(struct brcmf_proto_bcdc_dcmd);
	do {
		ret = brcmf_bus_rxctl(drvr->bus_if, (unsigned char *)&bcdc->msg,
				      len);
		if (ret < 0)
			break;
	} while (BCDC_DCMD_ID(le32_to_cpu(bcdc->msg.flags)) != id);

	return ret;
}

static int
brcmf_proto_bcdc_query_dcmd(struct brcmf_pub *drvr, int ifidx, uint cmd,
			    void *buf, uint len)
{
	struct brcmf_bcdc *bcdc = (struct brcmf_bcdc *)drvr->proto->pd;
	struct brcmf_proto_bcdc_dcmd *msg = &bcdc->msg;
	void *info;
	int ret = 0, retries = 0;
	u32 id, flags;

	brcmf_dbg(BCDC, "Enter, cmd %d len %d\n", cmd, len);

	ret = brcmf_proto_bcdc_msg(drvr, ifidx, cmd, buf, len, false);
	if (ret < 0) {
		brcmf_err("brcmf_proto_bcdc_msg failed w/status %d\n",
			  ret);
		goto done;
	}

retry:
	/* wait for interrupt and get first fragment */
	ret = brcmf_proto_bcdc_cmplt(drvr, bcdc->reqid, len);
	if (ret < 0)
		goto done;

	flags = le32_to_cpu(msg->flags);
	id = (flags & BCDC_DCMD_ID_MASK) >> BCDC_DCMD_ID_SHIFT;

	if ((id < bcdc->reqid) && (++retries < RETRIES))
		goto retry;
	if (id != bcdc->reqid) {
		brcmf_err("%s: unexpected request id %d (expected %d)\n",
			  brcmf_ifname(drvr, ifidx), id, bcdc->reqid);
		ret = -EINVAL;
		goto done;
	}

	/* Check info buffer */
	info = (void *)&msg[1];

	/* Copy info buffer */
	if (buf) {
		if (ret < (int)len)
			len = ret;
		memcpy(buf, info, len);
	}

	/* Check the ERROR flag */
	if (flags & BCDC_DCMD_ERROR)
		ret = le32_to_cpu(msg->status);

done:
	return ret;
}

static int
brcmf_proto_bcdc_set_dcmd(struct brcmf_pub *drvr, int ifidx, uint cmd,
			  void *buf, uint len)
{
	struct brcmf_bcdc *bcdc = (struct brcmf_bcdc *)drvr->proto->pd;
	struct brcmf_proto_bcdc_dcmd *msg = &bcdc->msg;
	int ret = 0;
	u32 flags, id;

	brcmf_dbg(BCDC, "Enter, cmd %d len %d\n", cmd, len);

	ret = brcmf_proto_bcdc_msg(drvr, ifidx, cmd, buf, len, true);
	if (ret < 0)
		goto done;

	ret = brcmf_proto_bcdc_cmplt(drvr, bcdc->reqid, len);
	if (ret < 0)
		goto done;

	flags = le32_to_cpu(msg->flags);
	id = (flags & BCDC_DCMD_ID_MASK) >> BCDC_DCMD_ID_SHIFT;

	if (id != bcdc->reqid) {
		brcmf_err("%s: unexpected request id %d (expected %d)\n",
			  brcmf_ifname(drvr, ifidx), id, bcdc->reqid);
		ret = -EINVAL;
		goto done;
	}

	/* Check the ERROR flag */
	if (flags & BCDC_DCMD_ERROR)
		ret = le32_to_cpu(msg->status);

done:
	return ret;
}

static void
brcmf_proto_bcdc_hdrpush(struct brcmf_pub *drvr, int ifidx, u8 offset,
			 struct sk_buff *pktbuf)
{
	struct brcmf_proto_bcdc_header *h;

	brcmf_dbg(BCDC, "Enter\n");

	/* Push BDC header used to convey priority for buses that don't */
	skb_push(pktbuf, BCDC_HEADER_LEN);

	h = (struct brcmf_proto_bcdc_header *)(pktbuf->data);

	h->flags = (BCDC_PROTO_VER << BCDC_FLAG_VER_SHIFT);
	if (pktbuf->ip_summed == CHECKSUM_PARTIAL)
		h->flags |= BCDC_FLAG_SUM_NEEDED;

	h->priority = (pktbuf->priority & BCDC_PRIORITY_MASK);
	h->flags2 = 0;
	h->data_offset = offset;
	BCDC_SET_IF_IDX(h, ifidx);
	trace_brcmf_bcdchdr(pktbuf->data);
}

static int
brcmf_proto_bcdc_hdrpull(struct brcmf_pub *drvr, bool do_fws, u8 *ifidx,
			 struct sk_buff *pktbuf)
{
	struct brcmf_proto_bcdc_header *h;

	brcmf_dbg(BCDC, "Enter\n");

	/* Pop BCDC header used to convey priority for buses that don't */
	if (pktbuf->len <= BCDC_HEADER_LEN) {
		brcmf_dbg(INFO, "rx data too short (%d <= %d)\n",
			  pktbuf->len, BCDC_HEADER_LEN);
		return -EBADE;
	}

	trace_brcmf_bcdchdr(pktbuf->data);
	h = (struct brcmf_proto_bcdc_header *)(pktbuf->data);

	*ifidx = BCDC_GET_IF_IDX(h);
	if (*ifidx >= BRCMF_MAX_IFS) {
		brcmf_err("rx data ifnum out of range (%d)\n", *ifidx);
		return -EBADE;
	}
	/* The ifidx is the idx to map to matching netdev/ifp. When receiving
	 * events this is easy because it contains the bssidx which maps
	 * 1-on-1 to the netdev/ifp. But for data frames the ifidx is rcvd.
	 * bssidx 1 is used for p2p0 and no data can be received or
	 * transmitted on it. Therefor bssidx is ifidx + 1 if ifidx > 0
	 */
	if (*ifidx)
		(*ifidx)++;

	if (((h->flags & BCDC_FLAG_VER_MASK) >> BCDC_FLAG_VER_SHIFT) !=
	    BCDC_PROTO_VER) {
		brcmf_err("%s: non-BCDC packet received, flags 0x%x\n",
			  brcmf_ifname(drvr, *ifidx), h->flags);
		return -EBADE;
	}

	if (h->flags & BCDC_FLAG_SUM_GOOD) {
		brcmf_dbg(BCDC, "%s: BDC rcv, good checksum, flags 0x%x\n",
			  brcmf_ifname(drvr, *ifidx), h->flags);
		pktbuf->ip_summed = CHECKSUM_UNNECESSARY;
	}

	pktbuf->priority = h->priority & BCDC_PRIORITY_MASK;

	skb_pull(pktbuf, BCDC_HEADER_LEN);
	if (do_fws)
		brcmf_fws_hdrpull(drvr, *ifidx, h->data_offset << 2, pktbuf);
	else
		skb_pull(pktbuf, h->data_offset << 2);

	if (pktbuf->len == 0)
		return -ENODATA;
	return 0;
}

static int
brcmf_proto_bcdc_txdata(struct brcmf_pub *drvr, int ifidx, u8 offset,
			struct sk_buff *pktbuf)
{
	brcmf_proto_bcdc_hdrpush(drvr, ifidx, offset, pktbuf);
	return brcmf_bus_txdata(drvr->bus_if, pktbuf);
}


int brcmf_proto_bcdc_attach(struct brcmf_pub *drvr)
{
	struct brcmf_bcdc *bcdc;

	bcdc = kzalloc(sizeof(*bcdc), GFP_ATOMIC);
	if (!bcdc)
		goto fail;

	/* ensure that the msg buf directly follows the cdc msg struct */
	if ((unsigned long)(&bcdc->msg + 1) != (unsigned long)bcdc->buf) {
		brcmf_err("struct brcmf_proto_bcdc is not correctly defined\n");
		goto fail;
	}

	drvr->proto->hdrpull = brcmf_proto_bcdc_hdrpull;
	drvr->proto->query_dcmd = brcmf_proto_bcdc_query_dcmd;
	drvr->proto->set_dcmd = brcmf_proto_bcdc_set_dcmd;
	drvr->proto->txdata = brcmf_proto_bcdc_txdata;
	drvr->proto->pd = bcdc;

	drvr->hdrlen += BCDC_HEADER_LEN + BRCMF_PROT_FW_SIGNAL_MAX_TXBYTES;
	drvr->bus_if->maxctl = BRCMF_DCMD_MAXLEN +
			sizeof(struct brcmf_proto_bcdc_dcmd);
	return 0;

fail:
	kfree(bcdc);
	return -ENOMEM;
}

void brcmf_proto_bcdc_detach(struct brcmf_pub *drvr)
{
	kfree(drvr->proto->pd);
	drvr->proto->pd = NULL;
}
