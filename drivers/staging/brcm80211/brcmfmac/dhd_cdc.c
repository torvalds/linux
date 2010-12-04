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
#include <bcmdefs.h>
#include <osl.h>

#include <bcmutils.h>
#include <bcmcdc.h>
#include <bcmendian.h>

#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_proto.h>
#include <dhd_bus.h>
#include <dhd_dbg.h>
#ifdef CUSTOMER_HW2
int wifi_get_mac_addr(unsigned char *buf);
#endif

extern int dhd_preinit_ioctls(dhd_pub_t *dhd);

/* Packet alignment for most efficient SDIO (can change based on platform) */
#ifndef DHD_SDALIGN
#define DHD_SDALIGN	32
#endif
#if !ISPOWEROF2(DHD_SDALIGN)
#error DHD_SDALIGN is not a power of 2!
#endif

#define RETRIES 2	/* # of retries to retrieve matching ioctl response */
#define BUS_HEADER_LEN	(16+DHD_SDALIGN) /* Must be atleast SDPCM_RESERVE
					 * defined in dhd_sdio.c
					 * (amount of header tha might be added)
					 * plus any space that might be needed
					 * for alignment padding.
					 */
#define ROUND_UP_MARGIN	2048	/* Biggest SDIO block size possible for
				 * round off at the end of buffer
				 */

typedef struct dhd_prot {
	u16 reqid;
	u8 pending;
	u32 lastcmd;
	u8 bus_header[BUS_HEADER_LEN];
	cdc_ioctl_t msg;
	unsigned char buf[WLC_IOCTL_MAXLEN + ROUND_UP_MARGIN];
} dhd_prot_t;

static int dhdcdc_msg(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	int len = ltoh32(prot->msg.len) + sizeof(cdc_ioctl_t);

	DHD_TRACE(("%s: Enter\n", __func__));

	/* NOTE : cdc->msg.len holds the desired length of the buffer to be
	 *        returned. Only up to CDC_MAX_MSG_SIZE of this buffer area
	 *        is actually sent to the dongle
	 */
	if (len > CDC_MAX_MSG_SIZE)
		len = CDC_MAX_MSG_SIZE;

	/* Send request */
	return dhd_bus_txctl(dhd->bus, (unsigned char *)&prot->msg, len);
}

static int dhdcdc_cmplt(dhd_pub_t *dhd, u32 id, u32 len)
{
	int ret;
	dhd_prot_t *prot = dhd->prot;

	DHD_TRACE(("%s: Enter\n", __func__));

	do {
		ret =
		    dhd_bus_rxctl(dhd->bus, (unsigned char *)&prot->msg,
				  len + sizeof(cdc_ioctl_t));
		if (ret < 0)
			break;
	} while (CDC_IOC_ID(ltoh32(prot->msg.flags)) != id);

	return ret;
}

int
dhdcdc_query_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf, uint len)
{
	dhd_prot_t *prot = dhd->prot;
	cdc_ioctl_t *msg = &prot->msg;
	void *info;
	int ret = 0, retries = 0;
	u32 id, flags = 0;

	DHD_TRACE(("%s: Enter\n", __func__));
	DHD_CTL(("%s: cmd %d len %d\n", __func__, cmd, len));

	/* Respond "bcmerror" and "bcmerrorstr" with local cache */
	if (cmd == WLC_GET_VAR && buf) {
		if (!strcmp((char *)buf, "bcmerrorstr")) {
			strncpy((char *)buf, bcmerrorstr(dhd->dongle_error),
				BCME_STRLEN);
			goto done;
		} else if (!strcmp((char *)buf, "bcmerror")) {
			*(int *)buf = dhd->dongle_error;
			goto done;
		}
	}

	memset(msg, 0, sizeof(cdc_ioctl_t));

	msg->cmd = htol32(cmd);
	msg->len = htol32(len);
	msg->flags = (++prot->reqid << CDCF_IOC_ID_SHIFT);
	CDC_SET_IF_IDX(msg, ifidx);
	msg->flags = htol32(msg->flags);

	if (buf)
		memcpy(prot->buf, buf, len);

	ret = dhdcdc_msg(dhd);
	if (ret < 0) {
		DHD_ERROR(("dhdcdc_query_ioctl: dhdcdc_msg failed w/status "
			"%d\n", ret));
		goto done;
	}

retry:
	/* wait for interrupt and get first fragment */
	ret = dhdcdc_cmplt(dhd, prot->reqid, len);
	if (ret < 0)
		goto done;

	flags = ltoh32(msg->flags);
	id = (flags & CDCF_IOC_ID_MASK) >> CDCF_IOC_ID_SHIFT;

	if ((id < prot->reqid) && (++retries < RETRIES))
		goto retry;
	if (id != prot->reqid) {
		DHD_ERROR(("%s: %s: unexpected request id %d (expected %d)\n",
			   dhd_ifname(dhd, ifidx), __func__, id, prot->reqid));
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
		ret = ltoh32(msg->status);
		/* Cache error from dongle */
		dhd->dongle_error = ret;
	}

done:
	return ret;
}

int dhdcdc_set_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf, uint len)
{
	dhd_prot_t *prot = dhd->prot;
	cdc_ioctl_t *msg = &prot->msg;
	int ret = 0;
	u32 flags, id;

	DHD_TRACE(("%s: Enter\n", __func__));
	DHD_CTL(("%s: cmd %d len %d\n", __func__, cmd, len));

	memset(msg, 0, sizeof(cdc_ioctl_t));

	msg->cmd = htol32(cmd);
	msg->len = htol32(len);
	msg->flags = (++prot->reqid << CDCF_IOC_ID_SHIFT) | CDCF_IOC_SET;
	CDC_SET_IF_IDX(msg, ifidx);
	msg->flags = htol32(msg->flags);

	if (buf)
		memcpy(prot->buf, buf, len);

	ret = dhdcdc_msg(dhd);
	if (ret < 0)
		goto done;

	ret = dhdcdc_cmplt(dhd, prot->reqid, len);
	if (ret < 0)
		goto done;

	flags = ltoh32(msg->flags);
	id = (flags & CDCF_IOC_ID_MASK) >> CDCF_IOC_ID_SHIFT;

	if (id != prot->reqid) {
		DHD_ERROR(("%s: %s: unexpected request id %d (expected %d)\n",
			   dhd_ifname(dhd, ifidx), __func__, id, prot->reqid));
		ret = -EINVAL;
		goto done;
	}

	/* Check the ERROR flag */
	if (flags & CDCF_IOC_ERROR) {
		ret = ltoh32(msg->status);
		/* Cache error from dongle */
		dhd->dongle_error = ret;
	}

done:
	return ret;
}

extern int dhd_bus_interface(struct dhd_bus *bus, uint arg, void *arg2);
int
dhd_prot_ioctl(dhd_pub_t *dhd, int ifidx, wl_ioctl_t *ioc, void *buf, int len)
{
	dhd_prot_t *prot = dhd->prot;
	int ret = -1;

	if (dhd->busstate == DHD_BUS_DOWN) {
		DHD_ERROR(("%s : bus is down. we have nothing to do\n",
			   __func__));
		return ret;
	}
	dhd_os_proto_block(dhd);

	DHD_TRACE(("%s: Enter\n", __func__));

	ASSERT(len <= WLC_IOCTL_MAXLEN);

	if (len > WLC_IOCTL_MAXLEN)
		goto done;

	if (prot->pending == true) {
		DHD_TRACE(("CDC packet is pending!!!! cmd=0x%x (%lu) "
			"lastcmd=0x%x (%lu)\n",
			ioc->cmd, (unsigned long)ioc->cmd, prot->lastcmd,
			(unsigned long)prot->lastcmd));
		if ((ioc->cmd == WLC_SET_VAR) || (ioc->cmd == WLC_GET_VAR)) {
			DHD_TRACE(("iovar cmd=%s\n", (char *)buf));
		}
		goto done;
	}

	prot->pending = true;
	prot->lastcmd = ioc->cmd;
	if (ioc->set)
		ret = dhdcdc_set_ioctl(dhd, ifidx, ioc->cmd, buf, len);
	else {
		ret = dhdcdc_query_ioctl(dhd, ifidx, ioc->cmd, buf, len);
		if (ret > 0)
			ioc->used = ret - sizeof(cdc_ioctl_t);
	}

	/* Too many programs assume ioctl() returns 0 on success */
	if (ret >= 0)
		ret = 0;
	else {
		cdc_ioctl_t *msg = &prot->msg;
		ioc->needed = ltoh32(msg->len);	/* len == needed when set/query
						 fails from dongle */
	}

	/* Intercept the wme_dp ioctl here */
	if ((!ret) && (ioc->cmd == WLC_SET_VAR) && (!strcmp(buf, "wme_dp"))) {
		int slen, val = 0;

		slen = strlen("wme_dp") + 1;
		if (len >= (int)(slen + sizeof(int)))
			bcopy(((char *)buf + slen), &val, sizeof(int));
		dhd->wme_dp = (u8) ltoh32(val);
	}

	prot->pending = false;

done:
	dhd_os_proto_unblock(dhd);

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
dhd_prot_iovar_op(dhd_pub_t *dhdp, const char *name,
		  void *params, int plen, void *arg, int len, bool set)
{
	return BCME_UNSUPPORTED;
}

void dhd_prot_dump(dhd_pub_t *dhdp, struct bcmstrbuf *strbuf)
{
	bcm_bprintf(strbuf, "Protocol CDC: reqid %d\n", dhdp->prot->reqid);
}

void dhd_prot_hdrpush(dhd_pub_t *dhd, int ifidx, struct sk_buff *pktbuf)
{
#ifdef BDC
	struct bdc_header *h;
#endif				/* BDC */

	DHD_TRACE(("%s: Enter\n", __func__));

#ifdef BDC
	/* Push BDC header used to convey priority for buses that don't */

	skb_push(pktbuf, BDC_HEADER_LEN);

	h = (struct bdc_header *)(pktbuf->data);

	h->flags = (BDC_PROTO_VER << BDC_FLAG_VER_SHIFT);
	if (PKTSUMNEEDED(pktbuf))
		h->flags |= BDC_FLAG_SUM_NEEDED;

	h->priority = (pktbuf->priority & BDC_PRIORITY_MASK);
	h->flags2 = 0;
	h->rssi = 0;
#endif				/* BDC */
	BDC_SET_IF_IDX(h, ifidx);
}

bool dhd_proto_fcinfo(dhd_pub_t *dhd, struct sk_buff *pktbuf, u8 * fcbits)
{
#ifdef BDC
	struct bdc_header *h;

	if (pktbuf->len < BDC_HEADER_LEN) {
		DHD_ERROR(("%s: rx data too short (%d < %d)\n",
			   __func__, pktbuf->len, BDC_HEADER_LEN));
		return BCME_ERROR;
	}

	h = (struct bdc_header *)(pktbuf->data);

	*fcbits = h->priority >> BDC_PRIORITY_FC_SHIFT;
	if ((h->flags2 & BDC_FLAG2_FC_FLAG) == BDC_FLAG2_FC_FLAG)
		return true;
#endif
	return false;
}

int dhd_prot_hdrpull(dhd_pub_t *dhd, int *ifidx, struct sk_buff *pktbuf)
{
#ifdef BDC
	struct bdc_header *h;
#endif

	DHD_TRACE(("%s: Enter\n", __func__));

#ifdef BDC
	/* Pop BDC header used to convey priority for buses that don't */

	if (pktbuf->len < BDC_HEADER_LEN) {
		DHD_ERROR(("%s: rx data too short (%d < %d)\n", __func__,
			   pktbuf->len, BDC_HEADER_LEN));
		return BCME_ERROR;
	}

	h = (struct bdc_header *)(pktbuf->data);

	*ifidx = BDC_GET_IF_IDX(h);
	if (*ifidx >= DHD_MAX_IFS) {
		DHD_ERROR(("%s: rx data ifnum out of range (%d)\n",
			   __func__, *ifidx));
		return BCME_ERROR;
	}

	if (((h->flags & BDC_FLAG_VER_MASK) >> BDC_FLAG_VER_SHIFT) !=
	    BDC_PROTO_VER) {
		DHD_ERROR(("%s: non-BDC packet received, flags 0x%x\n",
			   dhd_ifname(dhd, *ifidx), h->flags));
		return BCME_ERROR;
	}

	if (h->flags & BDC_FLAG_SUM_GOOD) {
		DHD_INFO(("%s: BDC packet received with good rx-csum, "
			"flags 0x%x\n",
			dhd_ifname(dhd, *ifidx), h->flags));
		PKTSETSUMGOOD(pktbuf, true);
	}

	pktbuf->priority = h->priority & BDC_PRIORITY_MASK;

	skb_pull(pktbuf, BDC_HEADER_LEN);
#endif				/* BDC */

	return 0;
}

int dhd_prot_attach(dhd_pub_t *dhd)
{
	dhd_prot_t *cdc;

	cdc = kzalloc(sizeof(dhd_prot_t), GFP_ATOMIC);
	if (!cdc) {
		DHD_ERROR(("%s: kmalloc failed\n", __func__));
		goto fail;
	}

	/* ensure that the msg buf directly follows the cdc msg struct */
	if ((unsigned long)(&cdc->msg + 1) != (unsigned long)cdc->buf) {
		DHD_ERROR(("dhd_prot_t is not correctly defined\n"));
		goto fail;
	}

	dhd->prot = cdc;
#ifdef BDC
	dhd->hdrlen += BDC_HEADER_LEN;
#endif
	dhd->maxctl = WLC_IOCTL_MAXLEN + sizeof(cdc_ioctl_t) + ROUND_UP_MARGIN;
	return 0;

fail:
	if (cdc != NULL)
		kfree(cdc);
	return BCME_NOMEM;
}

/* ~NOTE~ What if another thread is waiting on the semaphore?  Holding it? */
void dhd_prot_detach(dhd_pub_t *dhd)
{
	kfree(dhd->prot);
	dhd->prot = NULL;
}

void dhd_prot_dstats(dhd_pub_t *dhd)
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

int dhd_prot_init(dhd_pub_t *dhd)
{
	int ret = 0;
	char buf[128];

	DHD_TRACE(("%s: Enter\n", __func__));

	dhd_os_proto_block(dhd);

	/* Get the device MAC address */
	strcpy(buf, "cur_etheraddr");
	ret = dhdcdc_query_ioctl(dhd, 0, WLC_GET_VAR, buf, sizeof(buf));
	if (ret < 0) {
		dhd_os_proto_unblock(dhd);
		return ret;
	}
	memcpy(dhd->mac.octet, buf, ETHER_ADDR_LEN);

	dhd_os_proto_unblock(dhd);

#ifdef EMBEDDED_PLATFORM
	ret = dhd_preinit_ioctls(dhd);
#endif				/* EMBEDDED_PLATFORM */

	/* Always assumes wl for now */
	dhd->iswl = true;

	return ret;
}

void dhd_prot_stop(dhd_pub_t *dhd)
{
	/* Nothing to do for CDC */
}
