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
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <defs.h>

#include <brcmu_utils.h>
#include <brcmu_wifi.h>

#include "dngl_stats.h"
#include "dhd.h"
#include "dhd_proto.h"
#include "dhd_bus.h"
#include "dhd_dbg.h"

struct brcmf_proto_cdc_ioctl {
	u32 cmd;	/* ioctl command value */
	u32 len;	/* lower 16: output buflen;
			 * upper 16: input buflen (excludes header) */
	u32 flags;	/* flag defns given below */
	u32 status;	/* status code returned from the device */
};

/* Max valid buffer size that can be sent to the dongle */
#define CDC_MAX_MSG_SIZE	(ETH_FRAME_LEN+ETH_FCS_LEN)

/* CDC flag definitions */
#define CDCF_IOC_ERROR		0x01		/* 1=ioctl cmd failed */
#define CDCF_IOC_SET		0x02		/* 0=get, 1=set cmd */
#define CDCF_IOC_IF_MASK	0xF000		/* I/F index */
#define CDCF_IOC_IF_SHIFT	12
#define CDCF_IOC_ID_MASK	0xFFFF0000	/* id an ioctl pairing */
#define CDCF_IOC_ID_SHIFT	16		/* ID Mask shift bits */
#define CDC_IOC_ID(flags)	\
	(((flags) & CDCF_IOC_ID_MASK) >> CDCF_IOC_ID_SHIFT)
#define CDC_SET_IF_IDX(hdr, idx) \
	((hdr)->flags = (((hdr)->flags & ~CDCF_IOC_IF_MASK) | \
	((idx) << CDCF_IOC_IF_SHIFT)))

/*
 * BDC header - Broadcom specific extension of CDC.
 * Used on data packets to convey priority across USB.
 */
#define	BDC_HEADER_LEN		4
#define BDC_PROTO_VER		1	/* Protocol version */
#define BDC_FLAG_VER_MASK	0xf0	/* Protocol version mask */
#define BDC_FLAG_VER_SHIFT	4	/* Protocol version shift */
#define BDC_FLAG_SUM_GOOD	0x04	/* Good RX checksums */
#define BDC_FLAG_SUM_NEEDED	0x08	/* Dongle needs to do TX checksums */
#define BDC_PRIORITY_MASK	0x7
#define BDC_FLAG2_IF_MASK	0x0f	/* packet rx interface in APSTA */
#define BDC_FLAG2_IF_SHIFT	0

#define BDC_GET_IF_IDX(hdr) \
	((int)((((hdr)->flags2) & BDC_FLAG2_IF_MASK) >> BDC_FLAG2_IF_SHIFT))
#define BDC_SET_IF_IDX(hdr, idx) \
	((hdr)->flags2 = (((hdr)->flags2 & ~BDC_FLAG2_IF_MASK) | \
	((idx) << BDC_FLAG2_IF_SHIFT)))

struct brcmf_proto_bdc_header {
	u8 flags;
	u8 priority;	/* 802.1d Priority, 4:7 flow control info for usb */
	u8 flags2;
	u8 rssi;
};


#define RETRIES 2	/* # of retries to retrieve matching ioctl response */
#define BUS_HEADER_LEN	(16+BRCMF_SDALIGN) /* Must be atleast SDPCM_RESERVE
					 * defined in dhd_sdio.c
					 * (amount of header tha might be added)
					 * plus any space that might be needed
					 * for alignment padding.
					 */
#define ROUND_UP_MARGIN	2048	/* Biggest SDIO block size possible for
				 * round off at the end of buffer
				 */

struct brcmf_proto {
	u16 reqid;
	u8 pending;
	u32 lastcmd;
	u8 bus_header[BUS_HEADER_LEN];
	struct brcmf_proto_cdc_ioctl msg;
	unsigned char buf[BRCMF_C_IOCTL_MAXLEN + ROUND_UP_MARGIN];
};

static int brcmf_proto_cdc_msg(dhd_pub_t *dhd)
{
	struct brcmf_proto *prot = dhd->prot;
	int len = le32_to_cpu(prot->msg.len) +
			sizeof(struct brcmf_proto_cdc_ioctl);

	DHD_TRACE(("%s: Enter\n", __func__));

	/* NOTE : cdc->msg.len holds the desired length of the buffer to be
	 *        returned. Only up to CDC_MAX_MSG_SIZE of this buffer area
	 *        is actually sent to the dongle
	 */
	if (len > CDC_MAX_MSG_SIZE)
		len = CDC_MAX_MSG_SIZE;

	/* Send request */
	return brcmf_sdbrcm_bus_txctl(dhd->bus, (unsigned char *)&prot->msg,
				      len);
}

static int brcmf_proto_cdc_cmplt(dhd_pub_t *dhd, u32 id, u32 len)
{
	int ret;
	struct brcmf_proto *prot = dhd->prot;

	DHD_TRACE(("%s: Enter\n", __func__));

	do {
		ret = brcmf_sdbrcm_bus_rxctl(dhd->bus,
				(unsigned char *)&prot->msg,
				len + sizeof(struct brcmf_proto_cdc_ioctl));
		if (ret < 0)
			break;
	} while (CDC_IOC_ID(le32_to_cpu(prot->msg.flags)) != id);

	return ret;
}

int
brcmf_proto_cdc_query_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf,
			    uint len)
{
	struct brcmf_proto *prot = dhd->prot;
	struct brcmf_proto_cdc_ioctl *msg = &prot->msg;
	void *info;
	int ret = 0, retries = 0;
	u32 id, flags = 0;

	DHD_TRACE(("%s: Enter\n", __func__));
	DHD_CTL(("%s: cmd %d len %d\n", __func__, cmd, len));

	/* Respond "bcmerror" and "bcmerrorstr" with local cache */
	if (cmd == BRCMF_C_GET_VAR && buf) {
		if (!strcmp((char *)buf, "bcmerrorstr")) {
			strncpy((char *)buf, "bcm_error",
				BCME_STRLEN);
			goto done;
		} else if (!strcmp((char *)buf, "bcmerror")) {
			*(int *)buf = dhd->dongle_error;
			goto done;
		}
	}

	memset(msg, 0, sizeof(struct brcmf_proto_cdc_ioctl));

	msg->cmd = cpu_to_le32(cmd);
	msg->len = cpu_to_le32(len);
	msg->flags = (++prot->reqid << CDCF_IOC_ID_SHIFT);
	CDC_SET_IF_IDX(msg, ifidx);
	msg->flags = cpu_to_le32(msg->flags);

	if (buf)
		memcpy(prot->buf, buf, len);

	ret = brcmf_proto_cdc_msg(dhd);
	if (ret < 0) {
		DHD_ERROR(("dhdcdc_query_ioctl: dhdcdc_msg failed w/status "
			"%d\n", ret));
		goto done;
	}

retry:
	/* wait for interrupt and get first fragment */
	ret = brcmf_proto_cdc_cmplt(dhd, prot->reqid, len);
	if (ret < 0)
		goto done;

	flags = le32_to_cpu(msg->flags);
	id = (flags & CDCF_IOC_ID_MASK) >> CDCF_IOC_ID_SHIFT;

	if ((id < prot->reqid) && (++retries < RETRIES))
		goto retry;
	if (id != prot->reqid) {
		DHD_ERROR(("%s: %s: unexpected request id %d (expected %d)\n",
			   brcmf_ifname(dhd, ifidx), __func__, id,
			   prot->reqid));
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
	if (flags & CDCF_IOC_ERROR) {
		ret = le32_to_cpu(msg->status);
		/* Cache error from dongle */
		dhd->dongle_error = ret;
	}

done:
	return ret;
}

int brcmf_proto_cdc_set_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd,
			      void *buf, uint len)
{
	struct brcmf_proto *prot = dhd->prot;
	struct brcmf_proto_cdc_ioctl *msg = &prot->msg;
	int ret = 0;
	u32 flags, id;

	DHD_TRACE(("%s: Enter\n", __func__));
	DHD_CTL(("%s: cmd %d len %d\n", __func__, cmd, len));

	memset(msg, 0, sizeof(struct brcmf_proto_cdc_ioctl));

	msg->cmd = cpu_to_le32(cmd);
	msg->len = cpu_to_le32(len);
	msg->flags = (++prot->reqid << CDCF_IOC_ID_SHIFT) | CDCF_IOC_SET;
	CDC_SET_IF_IDX(msg, ifidx);
	msg->flags = cpu_to_le32(msg->flags);

	if (buf)
		memcpy(prot->buf, buf, len);

	ret = brcmf_proto_cdc_msg(dhd);
	if (ret < 0)
		goto done;

	ret = brcmf_proto_cdc_cmplt(dhd, prot->reqid, len);
	if (ret < 0)
		goto done;

	flags = le32_to_cpu(msg->flags);
	id = (flags & CDCF_IOC_ID_MASK) >> CDCF_IOC_ID_SHIFT;

	if (id != prot->reqid) {
		DHD_ERROR(("%s: %s: unexpected request id %d (expected %d)\n",
			   brcmf_ifname(dhd, ifidx), __func__, id,
			   prot->reqid));
		ret = -EINVAL;
		goto done;
	}

	/* Check the ERROR flag */
	if (flags & CDCF_IOC_ERROR) {
		ret = le32_to_cpu(msg->status);
		/* Cache error from dongle */
		dhd->dongle_error = ret;
	}

done:
	return ret;
}

extern int dhd_bus_interface(struct dhd_bus *bus, uint arg, void *arg2);
int
brcmf_proto_ioctl(dhd_pub_t *dhd, int ifidx, wl_ioctl_t *ioc, void *buf,
		  int len)
{
	struct brcmf_proto *prot = dhd->prot;
	int ret = -1;

	if (dhd->busstate == DHD_BUS_DOWN) {
		DHD_ERROR(("%s : bus is down. we have nothing to do\n",
			   __func__));
		return ret;
	}
	brcmf_os_proto_block(dhd);

	DHD_TRACE(("%s: Enter\n", __func__));

	ASSERT(len <= BRCMF_C_IOCTL_MAXLEN);

	if (len > BRCMF_C_IOCTL_MAXLEN)
		goto done;

	if (prot->pending == true) {
		DHD_TRACE(("CDC packet is pending!!!! cmd=0x%x (%lu) "
			"lastcmd=0x%x (%lu)\n",
			ioc->cmd, (unsigned long)ioc->cmd, prot->lastcmd,
			(unsigned long)prot->lastcmd));
		if ((ioc->cmd == BRCMF_C_SET_VAR) ||
		    (ioc->cmd == BRCMF_C_GET_VAR))
			DHD_TRACE(("iovar cmd=%s\n", (char *)buf));

		goto done;
	}

	prot->pending = true;
	prot->lastcmd = ioc->cmd;
	if (ioc->set)
		ret = brcmf_proto_cdc_set_ioctl(dhd, ifidx, ioc->cmd, buf, len);
	else {
		ret = brcmf_proto_cdc_query_ioctl(dhd, ifidx, ioc->cmd,
						  buf, len);
		if (ret > 0)
			ioc->used = ret - sizeof(struct brcmf_proto_cdc_ioctl);
	}

	/* Too many programs assume ioctl() returns 0 on success */
	if (ret >= 0)
		ret = 0;
	else {
		struct brcmf_proto_cdc_ioctl *msg = &prot->msg;
		/* len == needed when set/query fails from dongle */
		ioc->needed = le32_to_cpu(msg->len);
	}

	/* Intercept the wme_dp ioctl here */
	if (!ret && ioc->cmd == BRCMF_C_SET_VAR &&
	    !strcmp(buf, "wme_dp")) {
		int slen, val = 0;

		slen = strlen("wme_dp") + 1;
		if (len >= (int)(slen + sizeof(int)))
			memcpy(&val, (char *)buf + slen, sizeof(int));
		dhd->wme_dp = (u8) le32_to_cpu(val);
	}

	prot->pending = false;

done:
	brcmf_os_proto_unblock(dhd);

	return ret;
}

#define PKTSUMNEEDED(skb) \
		(((struct sk_buff *)(skb))->ip_summed == CHECKSUM_PARTIAL)
#define PKTSETSUMGOOD(skb, x) \
		(((struct sk_buff *)(skb))->ip_summed = \
		((x) ? CHECKSUM_UNNECESSARY : CHECKSUM_NONE))

/* PKTSETSUMNEEDED and PKTSUMGOOD are not possible because
	skb->ip_summed is overloaded */

int
brcmf_proto_iovar_op(dhd_pub_t *dhdp, const char *name,
		  void *params, int plen, void *arg, int len, bool set)
{
	return -ENOTSUPP;
}

void brcmf_proto_dump(dhd_pub_t *dhdp, struct brcmu_strbuf *strbuf)
{
	brcmu_bprintf(strbuf, "Protocol CDC: reqid %d\n", dhdp->prot->reqid);
}

void brcmf_proto_hdrpush(dhd_pub_t *dhd, int ifidx, struct sk_buff *pktbuf)
{
	struct brcmf_proto_bdc_header *h;

	DHD_TRACE(("%s: Enter\n", __func__));

	/* Push BDC header used to convey priority for buses that don't */

	skb_push(pktbuf, BDC_HEADER_LEN);

	h = (struct brcmf_proto_bdc_header *)(pktbuf->data);

	h->flags = (BDC_PROTO_VER << BDC_FLAG_VER_SHIFT);
	if (PKTSUMNEEDED(pktbuf))
		h->flags |= BDC_FLAG_SUM_NEEDED;

	h->priority = (pktbuf->priority & BDC_PRIORITY_MASK);
	h->flags2 = 0;
	h->rssi = 0;
	BDC_SET_IF_IDX(h, ifidx);
}

int brcmf_proto_hdrpull(dhd_pub_t *dhd, int *ifidx, struct sk_buff *pktbuf)
{
	struct brcmf_proto_bdc_header *h;

	DHD_TRACE(("%s: Enter\n", __func__));

	/* Pop BDC header used to convey priority for buses that don't */

	if (pktbuf->len < BDC_HEADER_LEN) {
		DHD_ERROR(("%s: rx data too short (%d < %d)\n", __func__,
			   pktbuf->len, BDC_HEADER_LEN));
		return -EBADE;
	}

	h = (struct brcmf_proto_bdc_header *)(pktbuf->data);

	*ifidx = BDC_GET_IF_IDX(h);
	if (*ifidx >= DHD_MAX_IFS) {
		DHD_ERROR(("%s: rx data ifnum out of range (%d)\n",
			   __func__, *ifidx));
		return -EBADE;
	}

	if (((h->flags & BDC_FLAG_VER_MASK) >> BDC_FLAG_VER_SHIFT) !=
	    BDC_PROTO_VER) {
		DHD_ERROR(("%s: non-BDC packet received, flags 0x%x\n",
			   brcmf_ifname(dhd, *ifidx), h->flags));
		return -EBADE;
	}

	if (h->flags & BDC_FLAG_SUM_GOOD) {
		DHD_INFO(("%s: BDC packet received with good rx-csum, "
			"flags 0x%x\n",
			brcmf_ifname(dhd, *ifidx), h->flags));
		PKTSETSUMGOOD(pktbuf, true);
	}

	pktbuf->priority = h->priority & BDC_PRIORITY_MASK;

	skb_pull(pktbuf, BDC_HEADER_LEN);

	return 0;
}

int brcmf_proto_attach(dhd_pub_t *dhd)
{
	struct brcmf_proto *cdc;

	cdc = kzalloc(sizeof(struct brcmf_proto), GFP_ATOMIC);
	if (!cdc) {
		DHD_ERROR(("%s: kmalloc failed\n", __func__));
		goto fail;
	}

	/* ensure that the msg buf directly follows the cdc msg struct */
	if ((unsigned long)(&cdc->msg + 1) != (unsigned long)cdc->buf) {
		DHD_ERROR(("struct brcmf_proto is not correctly defined\n"));
		goto fail;
	}

	dhd->prot = cdc;
	dhd->hdrlen += BDC_HEADER_LEN;
	dhd->maxctl = BRCMF_C_IOCTL_MAXLEN +
			sizeof(struct brcmf_proto_cdc_ioctl) + ROUND_UP_MARGIN;
	return 0;

fail:
	kfree(cdc);
	return -ENOMEM;
}

/* ~NOTE~ What if another thread is waiting on the semaphore?  Holding it? */
void brcmf_proto_detach(dhd_pub_t *dhd)
{
	kfree(dhd->prot);
	dhd->prot = NULL;
}

void brcmf_proto_dstats(dhd_pub_t *dhd)
{
	/* No stats from dongle added yet, copy bus stats */
	dhd->dstats.tx_packets = dhd->tx_packets;
	dhd->dstats.tx_errors = dhd->tx_errors;
	dhd->dstats.rx_packets = dhd->rx_packets;
	dhd->dstats.rx_errors = dhd->rx_errors;
	dhd->dstats.rx_dropped = dhd->rx_dropped;
	dhd->dstats.multicast = dhd->rx_multicast;
	return;
}

int brcmf_proto_init(dhd_pub_t *dhd)
{
	int ret = 0;
	char buf[128];

	DHD_TRACE(("%s: Enter\n", __func__));

	brcmf_os_proto_block(dhd);

	/* Get the device MAC address */
	strcpy(buf, "cur_etheraddr");
	ret = brcmf_proto_cdc_query_ioctl(dhd, 0, BRCMF_C_GET_VAR,
					  buf, sizeof(buf));
	if (ret < 0) {
		brcmf_os_proto_unblock(dhd);
		return ret;
	}
	memcpy(dhd->mac, buf, ETH_ALEN);

	brcmf_os_proto_unblock(dhd);

	ret = brcmf_c_preinit_ioctls(dhd);

	/* Always assumes wl for now */
	dhd->iswl = true;

	return ret;
}

void brcmf_proto_stop(dhd_pub_t *dhd)
{
	/* Nothing to do for CDC */
}
