/*
 * DHD Protocol Module for CDC and BDC.
 *
 * Copyright (C) 1999-2014, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: dhd_cdc.c 498632 2014-08-25 14:09:22Z $
 *
 * BDC is like CDC, except it includes a header for data packets to convey
 * packet priority over the bus, and flags (e.g. to indicate checksum status
 * for dongle offload.)
 */

#include <typedefs.h>
#include <osl.h>

#include <bcmutils.h>
#include <bcmcdc.h>
#include <bcmendian.h>

#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_proto.h>
#include <dhd_bus.h>
#include <dhd_dbg.h>


#ifdef PROP_TXSTATUS
#include <wlfc_proto.h>
#include <dhd_wlfc.h>
#endif


#define RETRIES 2		/* # of retries to retrieve matching ioctl response */
#define BUS_HEADER_LEN	(24+DHD_SDALIGN)	/* Must be at least SDPCM_RESERVE
				 * defined in dhd_sdio.c (amount of header tha might be added)
				 * plus any space that might be needed for alignment padding.
				 */
#define ROUND_UP_MARGIN	2048	/* Biggest SDIO block size possible for
				 * round off at the end of buffer
				 */

typedef struct dhd_prot {
	uint16 reqid;
	uint8 pending;
	uint32 lastcmd;
	uint8 bus_header[BUS_HEADER_LEN];
	cdc_ioctl_t msg;
	unsigned char buf[WLC_IOCTL_MAXLEN + ROUND_UP_MARGIN];
} dhd_prot_t;


static int
dhdcdc_msg(dhd_pub_t *dhd)
{
	int err = 0;
	dhd_prot_t *prot = dhd->prot;
	int len = ltoh32(prot->msg.len) + sizeof(cdc_ioctl_t);

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	DHD_OS_WAKE_LOCK(dhd);

	/* NOTE : cdc->msg.len holds the desired length of the buffer to be
	 *        returned. Only up to CDC_MAX_MSG_SIZE of this buffer area
	 *	  is actually sent to the dongle
	 */
	if (len > CDC_MAX_MSG_SIZE)
		len = CDC_MAX_MSG_SIZE;

	/* Send request */
	err = dhd_bus_txctl(dhd->bus, (uchar*)&prot->msg, len);

	DHD_OS_WAKE_UNLOCK(dhd);
	return err;
}

static int
dhdcdc_cmplt(dhd_pub_t *dhd, uint32 id, uint32 len)
{
	int ret;
	int cdc_len = len + sizeof(cdc_ioctl_t);
	dhd_prot_t *prot = dhd->prot;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));


	do {
		ret = dhd_bus_rxctl(dhd->bus, (uchar*)&prot->msg, cdc_len);
		if (ret < 0)
			break;
	} while (CDC_IOC_ID(ltoh32(prot->msg.flags)) != id);


	return ret;
}

static int
dhdcdc_query_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf, uint len, uint8 action)
{
	dhd_prot_t *prot = dhd->prot;
	cdc_ioctl_t *msg = &prot->msg;
	int ret = 0, retries = 0;
	uint32 id, flags = 0;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));
	DHD_CTL(("%s: cmd %d len %d\n", __FUNCTION__, cmd, len));


	/* Respond "bcmerror" and "bcmerrorstr" with local cache */
	if (cmd == WLC_GET_VAR && buf)
	{
		if (!strcmp((char *)buf, "bcmerrorstr"))
		{
			strncpy((char *)buf, bcmerrorstr(dhd->dongle_error), BCME_STRLEN);
			goto done;
		}
		else if (!strcmp((char *)buf, "bcmerror"))
		{
			*(int *)buf = dhd->dongle_error;
			goto done;
		}
	}

	memset(msg, 0, sizeof(cdc_ioctl_t));

	msg->cmd = htol32(cmd);
	msg->len = htol32(len);
	msg->flags = (++prot->reqid << CDCF_IOC_ID_SHIFT);
	CDC_SET_IF_IDX(msg, ifidx);
	/* add additional action bits */
	action &= WL_IOCTL_ACTION_MASK;
	msg->flags |= (action << CDCF_IOC_ACTION_SHIFT);
	msg->flags = htol32(msg->flags);

	if (buf)
		memcpy(prot->buf, buf, len);

	if ((ret = dhdcdc_msg(dhd)) < 0) {
		if (!dhd->hang_was_sent)
		DHD_ERROR(("dhdcdc_query_ioctl: dhdcdc_msg failed w/status %d\n", ret));
		goto done;
	}

retry:
	/* wait for interrupt and get first fragment */
	if ((ret = dhdcdc_cmplt(dhd, prot->reqid, len)) < 0)
		goto done;

	flags = ltoh32(msg->flags);
	id = (flags & CDCF_IOC_ID_MASK) >> CDCF_IOC_ID_SHIFT;

	if ((id < prot->reqid) && (++retries < RETRIES))
		goto retry;
	if (id != prot->reqid) {
		DHD_ERROR(("%s: %s: unexpected request id %d (expected %d)\n",
		           dhd_ifname(dhd, ifidx), __FUNCTION__, id, prot->reqid));
		ret = -EINVAL;
		goto done;
	}

	/* Copy info buffer */
	if (buf)
	{
		if (ret < (int)len)
			len = ret;
		memcpy(buf, (void*) prot->buf, len);
	}

	/* Check the ERROR flag */
	if (flags & CDCF_IOC_ERROR)
	{
		ret = ltoh32(msg->status);
		/* Cache error from dongle */
		dhd->dongle_error = ret;
	}

done:
	return ret;
}


static int
dhdcdc_set_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf, uint len, uint8 action)
{
	dhd_prot_t *prot = dhd->prot;
	cdc_ioctl_t *msg = &prot->msg;
	int ret = 0;
	uint32 flags, id;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));
	DHD_CTL(("%s: cmd %d len %d\n", __FUNCTION__, cmd, len));

	if (dhd->busstate == DHD_BUS_DOWN) {
		DHD_ERROR(("%s : bus is down. we have nothing to do\n", __FUNCTION__));
		return -EIO;
	}

	/* don't talk to the dongle if fw is about to be reloaded */
	if (dhd->hang_was_sent) {
		DHD_ERROR(("%s: HANG was sent up earlier. Not talking to the chip\n",
			__FUNCTION__));
		return -EIO;
	}

	memset(msg, 0, sizeof(cdc_ioctl_t));

	msg->cmd = htol32(cmd);
	msg->len = htol32(len);
	msg->flags = (++prot->reqid << CDCF_IOC_ID_SHIFT);
	CDC_SET_IF_IDX(msg, ifidx);
	/* add additional action bits */
	action &= WL_IOCTL_ACTION_MASK;
	msg->flags |= (action << CDCF_IOC_ACTION_SHIFT) | CDCF_IOC_SET;
	msg->flags = htol32(msg->flags);

	if (buf)
		memcpy(prot->buf, buf, len);

	if ((ret = dhdcdc_msg(dhd)) < 0) {
		DHD_ERROR(("%s: dhdcdc_msg failed w/status %d\n", __FUNCTION__, ret));
		goto done;
	}

	if ((ret = dhdcdc_cmplt(dhd, prot->reqid, len)) < 0)
		goto done;

	flags = ltoh32(msg->flags);
	id = (flags & CDCF_IOC_ID_MASK) >> CDCF_IOC_ID_SHIFT;

	if (id != prot->reqid) {
		DHD_ERROR(("%s: %s: unexpected request id %d (expected %d)\n",
		           dhd_ifname(dhd, ifidx), __FUNCTION__, id, prot->reqid));
		ret = -EINVAL;
		goto done;
	}

	/* Check the ERROR flag */
	if (flags & CDCF_IOC_ERROR)
	{
		ret = ltoh32(msg->status);
		/* Cache error from dongle */
		dhd->dongle_error = ret;
	}

done:
	return ret;
}


int
dhd_prot_ioctl(dhd_pub_t *dhd, int ifidx, wl_ioctl_t * ioc, void * buf, int len)
{
	dhd_prot_t *prot = dhd->prot;
	int ret = -1;
	uint8 action;

	if ((dhd->busstate == DHD_BUS_DOWN) || dhd->hang_was_sent) {
		DHD_ERROR(("%s : bus is down. we have nothing to do\n", __FUNCTION__));
		goto done;
	}

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	ASSERT(len <= WLC_IOCTL_MAXLEN);

	if (len > WLC_IOCTL_MAXLEN)
		goto done;

	if (prot->pending == TRUE) {
		DHD_ERROR(("CDC packet is pending!!!! cmd=0x%x (%lu) lastcmd=0x%x (%lu)\n",
			ioc->cmd, (unsigned long)ioc->cmd, prot->lastcmd,
			(unsigned long)prot->lastcmd));
		if ((ioc->cmd == WLC_SET_VAR) || (ioc->cmd == WLC_GET_VAR)) {
			DHD_TRACE(("iovar cmd=%s\n", (char*)buf));
		}
		goto done;
	}

	prot->pending = TRUE;
	prot->lastcmd = ioc->cmd;
	action = ioc->set;
	if (action & WL_IOCTL_ACTION_SET)
		ret = dhdcdc_set_ioctl(dhd, ifidx, ioc->cmd, buf, len, action);
	else {
		ret = dhdcdc_query_ioctl(dhd, ifidx, ioc->cmd, buf, len, action);
		if (ret > 0)
			ioc->used = ret - sizeof(cdc_ioctl_t);
	}

	/* Too many programs assume ioctl() returns 0 on success */
	if (ret >= 0)
		ret = 0;
	else {
		cdc_ioctl_t *msg = &prot->msg;
		ioc->needed = ltoh32(msg->len); /* len == needed when set/query fails from dongle */
	}

	/* Intercept the wme_dp ioctl here */
	if ((!ret) && (ioc->cmd == WLC_SET_VAR) && (!strcmp(buf, "wme_dp"))) {
		int slen, val = 0;

		slen = strlen("wme_dp") + 1;
		if (len >= (int)(slen + sizeof(int)))
			bcopy(((char *)buf + slen), &val, sizeof(int));
		dhd->wme_dp = (uint8) ltoh32(val);
	}

	prot->pending = FALSE;

done:

	return ret;
}

int
dhd_prot_iovar_op(dhd_pub_t *dhdp, const char *name,
                  void *params, int plen, void *arg, int len, bool set)
{
	return BCME_UNSUPPORTED;
}

void
dhd_prot_dump(dhd_pub_t *dhdp, struct bcmstrbuf *strbuf)
{
	bcm_bprintf(strbuf, "Protocol CDC: reqid %d\n", dhdp->prot->reqid);
#ifdef PROP_TXSTATUS
	dhd_wlfc_dump(dhdp, strbuf);
#endif
}

/*	The FreeBSD PKTPUSH could change the packet buf pinter
	so we need to make it changable
*/
#define PKTBUF pktbuf
void
dhd_prot_hdrpush(dhd_pub_t *dhd, int ifidx, void *PKTBUF)
{
#ifdef BDC
	struct bdc_header *h;
#endif /* BDC */

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

#ifdef BDC
	/* Push BDC header used to convey priority for buses that don't */

	PKTPUSH(dhd->osh, PKTBUF, BDC_HEADER_LEN);

	h = (struct bdc_header *)PKTDATA(dhd->osh, PKTBUF);

	h->flags = (BDC_PROTO_VER << BDC_FLAG_VER_SHIFT);
	if (PKTSUMNEEDED(PKTBUF))
		h->flags |= BDC_FLAG_SUM_NEEDED;


	h->priority = (PKTPRIO(PKTBUF) & BDC_PRIORITY_MASK);
	h->flags2 = 0;
	h->dataOffset = 0;
#endif /* BDC */
	BDC_SET_IF_IDX(h, ifidx);
}
#undef PKTBUF	/* Only defined in the above routine */

int
dhd_prot_hdrpull(dhd_pub_t *dhd, int *ifidx, void *pktbuf, uchar *reorder_buf_info,
	uint *reorder_info_len)
{
#ifdef BDC
	struct bdc_header *h;
#endif
	uint8 data_offset = 0;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

#ifdef BDC
	if (reorder_info_len)
		*reorder_info_len = 0;
	/* Pop BDC header used to convey priority for buses that don't */

	if (PKTLEN(dhd->osh, pktbuf) < BDC_HEADER_LEN) {
		DHD_ERROR(("%s: rx data too short (%d < %d)\n", __FUNCTION__,
		           PKTLEN(dhd->osh, pktbuf), BDC_HEADER_LEN));
		return BCME_ERROR;
	}

	h = (struct bdc_header *)PKTDATA(dhd->osh, pktbuf);

	if (!ifidx) {
		/* for tx packet, skip the analysis */
		data_offset = h->dataOffset;
		PKTPULL(dhd->osh, pktbuf, BDC_HEADER_LEN);
		goto exit;
	}

	*ifidx = BDC_GET_IF_IDX(h);

	if (((h->flags & BDC_FLAG_VER_MASK) >> BDC_FLAG_VER_SHIFT) != BDC_PROTO_VER) {
		DHD_ERROR(("%s: non-BDC packet received, flags = 0x%x\n",
		           dhd_ifname(dhd, *ifidx), h->flags));
		if (((h->flags & BDC_FLAG_VER_MASK) >> BDC_FLAG_VER_SHIFT) == BDC_PROTO_VER_1)
			h->dataOffset = 0;
		else
		return BCME_ERROR;
	}

	if (h->flags & BDC_FLAG_SUM_GOOD) {
		DHD_INFO(("%s: BDC packet received with good rx-csum, flags 0x%x\n",
		          dhd_ifname(dhd, *ifidx), h->flags));
		PKTSETSUMGOOD(pktbuf, TRUE);
	}

	PKTSETPRIO(pktbuf, (h->priority & BDC_PRIORITY_MASK));
	data_offset = h->dataOffset;
	PKTPULL(dhd->osh, pktbuf, BDC_HEADER_LEN);
#endif /* BDC */


#ifdef PROP_TXSTATUS
	if (!DHD_PKTTAG_PKTDIR(PKTTAG(pktbuf))) {
		/*
		- parse txstatus only for packets that came from the firmware
		*/
		dhd_wlfc_parse_header_info(dhd, pktbuf, (data_offset << 2),
			reorder_buf_info, reorder_info_len);

	}
#endif /* PROP_TXSTATUS */

exit:
	PKTPULL(dhd->osh, pktbuf, (data_offset << 2));
	return 0;
}


int
dhd_prot_attach(dhd_pub_t *dhd)
{
	dhd_prot_t *cdc;

	if (!(cdc = (dhd_prot_t *)DHD_OS_PREALLOC(dhd, DHD_PREALLOC_PROT, sizeof(dhd_prot_t)))) {
		DHD_ERROR(("%s: kmalloc failed\n", __FUNCTION__));
		goto fail;
	}
	memset(cdc, 0, sizeof(dhd_prot_t));

	/* ensure that the msg buf directly follows the cdc msg struct */
	if ((uintptr)(&cdc->msg + 1) != (uintptr)cdc->buf) {
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
		DHD_OS_PREFREE(dhd, cdc, sizeof(dhd_prot_t));
	return BCME_NOMEM;
}

/* ~NOTE~ What if another thread is waiting on the semaphore?  Holding it? */
void
dhd_prot_detach(dhd_pub_t *dhd)
{
#ifdef PROP_TXSTATUS
	dhd_wlfc_deinit(dhd);
#endif
	DHD_OS_PREFREE(dhd, dhd->prot, sizeof(dhd_prot_t));
	dhd->prot = NULL;
}

void
dhd_prot_dstats(dhd_pub_t *dhd)
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

int
dhd_prot_init(dhd_pub_t *dhd)
{
	int ret = 0;
	wlc_rev_info_t revinfo;
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));


	/* Get the device rev info */
	memset(&revinfo, 0, sizeof(revinfo));
	ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_REVINFO, &revinfo, sizeof(revinfo), FALSE, 0);
	if (ret < 0)
		goto done;


	ret = dhd_preinit_ioctls(dhd);

	/* Always assumes wl for now */
	dhd->iswl = TRUE;

done:
	return ret;
}

void
dhd_prot_stop(dhd_pub_t *dhd)
{
/* Nothing to do for CDC */
}


static void
dhd_get_hostreorder_pkts(void *osh, struct reorder_info *ptr, void **pkt,
	uint32 *pkt_count, void **pplast, uint8 start, uint8 end)
{
	void *plast = NULL, *p;
	uint32 pkt_cnt = 0;

	if (ptr->pend_pkts == 0) {
		DHD_REORDER(("%s: no packets in reorder queue \n", __FUNCTION__));
		*pplast = NULL;
		*pkt_count = 0;
		*pkt = NULL;
		return;
	}
	do {
		p = (void *)(ptr->p[start]);
		ptr->p[start] = NULL;

		if (p != NULL) {
			if (plast == NULL)
				*pkt = p;
			else
				PKTSETNEXT(osh, plast, p);

			plast = p;
			pkt_cnt++;
		}
		start++;
		if (start > ptr->max_idx)
			start = 0;
	} while (start != end);
	*pplast = plast;
	*pkt_count = pkt_cnt;
	ptr->pend_pkts -= (uint8)pkt_cnt;
}

int
dhd_process_pkt_reorder_info(dhd_pub_t *dhd, uchar *reorder_info_buf, uint reorder_info_len,
	void **pkt, uint32 *pkt_count)
{
	uint8 flow_id, max_idx, cur_idx, exp_idx;
	struct reorder_info *ptr;
	uint8 flags;
	void *cur_pkt, *plast = NULL;
	uint32 cnt = 0;

	if (pkt == NULL) {
		if (pkt_count != NULL)
			*pkt_count = 0;
		return 0;
	}

	flow_id = reorder_info_buf[WLHOST_REORDERDATA_FLOWID_OFFSET];
	flags = reorder_info_buf[WLHOST_REORDERDATA_FLAGS_OFFSET];

	DHD_REORDER(("flow_id %d, flags 0x%02x, idx(%d, %d, %d)\n", flow_id, flags,
		reorder_info_buf[WLHOST_REORDERDATA_CURIDX_OFFSET],
		reorder_info_buf[WLHOST_REORDERDATA_EXPIDX_OFFSET],
		reorder_info_buf[WLHOST_REORDERDATA_MAXIDX_OFFSET]));

	/* validate flags and flow id */
	if (flags == 0xFF) {
		DHD_ERROR(("%s: invalid flags...so ignore this packet\n", __FUNCTION__));
		*pkt_count = 1;
		return 0;
	}

	cur_pkt = *pkt;
	*pkt = NULL;

	ptr = dhd->reorder_bufs[flow_id];
	if (flags & WLHOST_REORDERDATA_DEL_FLOW) {
		uint32 buf_size = sizeof(struct reorder_info);

		DHD_REORDER(("%s: Flags indicating to delete a flow id %d\n",
			__FUNCTION__, flow_id));

		if (ptr == NULL) {
			DHD_REORDER(("%s: received flags to cleanup, but no flow (%d) yet\n",
				__FUNCTION__, flow_id));
			*pkt_count = 1;
			*pkt = cur_pkt;
			return 0;
		}

		dhd_get_hostreorder_pkts(dhd->osh, ptr, pkt, &cnt, &plast,
			ptr->exp_idx, ptr->exp_idx);
		/* set it to the last packet */
		if (plast) {
			PKTSETNEXT(dhd->osh, plast, cur_pkt);
			cnt++;
		}
		else {
			if (cnt != 0) {
				DHD_ERROR(("%s: del flow: something fishy, pending packets %d\n",
					__FUNCTION__, cnt));
			}
			*pkt = cur_pkt;
			cnt = 1;
		}
		buf_size += ((ptr->max_idx + 1) * sizeof(void *));
		MFREE(dhd->osh, ptr, buf_size);
		dhd->reorder_bufs[flow_id] = NULL;
		*pkt_count = cnt;
		return 0;
	}
	/* all the other cases depend on the existance of the reorder struct for that flow id */
	if (ptr == NULL) {
		uint32 buf_size_alloc = sizeof(reorder_info_t);
		max_idx = reorder_info_buf[WLHOST_REORDERDATA_MAXIDX_OFFSET];

		buf_size_alloc += ((max_idx + 1) * sizeof(void*));
		/* allocate space to hold the buffers, index etc */

		DHD_REORDER(("%s: alloc buffer of size %d size, reorder info id %d, maxidx %d\n",
			__FUNCTION__, buf_size_alloc, flow_id, max_idx));
		ptr = (struct reorder_info *)MALLOC(dhd->osh, buf_size_alloc);
		if (ptr == NULL) {
			DHD_ERROR(("%s: Malloc failed to alloc buffer\n", __FUNCTION__));
			*pkt_count = 1;
			return 0;
		}
		bzero(ptr, buf_size_alloc);
		dhd->reorder_bufs[flow_id] = ptr;
		ptr->p = (void *)(ptr+1);
		ptr->max_idx = max_idx;
	}
	if (flags & WLHOST_REORDERDATA_NEW_HOLE)  {
		DHD_REORDER(("%s: new hole, so cleanup pending buffers\n", __FUNCTION__));
		if (ptr->pend_pkts) {
			dhd_get_hostreorder_pkts(dhd->osh, ptr, pkt, &cnt, &plast,
				ptr->exp_idx, ptr->exp_idx);
			ptr->pend_pkts = 0;
		}
		ptr->cur_idx = reorder_info_buf[WLHOST_REORDERDATA_CURIDX_OFFSET];
		ptr->exp_idx = reorder_info_buf[WLHOST_REORDERDATA_EXPIDX_OFFSET];
		ptr->max_idx = reorder_info_buf[WLHOST_REORDERDATA_MAXIDX_OFFSET];
		ptr->p[ptr->cur_idx] = cur_pkt;
		ptr->pend_pkts++;
		*pkt_count = cnt;
	}
	else if (flags & WLHOST_REORDERDATA_CURIDX_VALID) {
		cur_idx = reorder_info_buf[WLHOST_REORDERDATA_CURIDX_OFFSET];
		exp_idx = reorder_info_buf[WLHOST_REORDERDATA_EXPIDX_OFFSET];


		if ((exp_idx == ptr->exp_idx) && (cur_idx != ptr->exp_idx)) {
			/* still in the current hole */
			/* enqueue the current on the buffer chain */
			if (ptr->p[cur_idx] != NULL) {
				DHD_REORDER(("%s: HOLE: ERROR buffer pending..free it\n",
					__FUNCTION__));
				PKTFREE(dhd->osh, ptr->p[cur_idx], TRUE);
				ptr->p[cur_idx] = NULL;
			}
			ptr->p[cur_idx] = cur_pkt;
			ptr->pend_pkts++;
			ptr->cur_idx = cur_idx;
			DHD_REORDER(("%s: fill up a hole..pending packets is %d\n",
				__FUNCTION__, ptr->pend_pkts));
			*pkt_count = 0;
			*pkt = NULL;
		}
		else if (ptr->exp_idx == cur_idx) {
			/* got the right one ..flush from cur to exp and update exp */
			DHD_REORDER(("%s: got the right one now, cur_idx is %d\n",
				__FUNCTION__, cur_idx));
			if (ptr->p[cur_idx] != NULL) {
				DHD_REORDER(("%s: Error buffer pending..free it\n",
					__FUNCTION__));
				PKTFREE(dhd->osh, ptr->p[cur_idx], TRUE);
				ptr->p[cur_idx] = NULL;
			}
			ptr->p[cur_idx] = cur_pkt;
			ptr->pend_pkts++;

			ptr->cur_idx = cur_idx;
			ptr->exp_idx = exp_idx;

			dhd_get_hostreorder_pkts(dhd->osh, ptr, pkt, &cnt, &plast,
				cur_idx, exp_idx);
			*pkt_count = cnt;
			DHD_REORDER(("%s: freeing up buffers %d, still pending %d\n",
				__FUNCTION__, cnt, ptr->pend_pkts));
		}
		else {
			uint8 end_idx;
			bool flush_current = FALSE;
			/* both cur and exp are moved now .. */
			DHD_REORDER(("%s:, flow %d, both moved, cur %d(%d), exp %d(%d)\n",
				__FUNCTION__, flow_id, ptr->cur_idx, cur_idx,
				ptr->exp_idx, exp_idx));
			if (flags & WLHOST_REORDERDATA_FLUSH_ALL)
				end_idx = ptr->exp_idx;
			else
				end_idx = exp_idx;

			/* flush pkts first */
			dhd_get_hostreorder_pkts(dhd->osh, ptr, pkt, &cnt, &plast,
				ptr->exp_idx, end_idx);

			if (cur_idx == ptr->max_idx) {
				if (exp_idx == 0)
					flush_current = TRUE;
			} else {
				if (exp_idx == cur_idx + 1)
					flush_current = TRUE;
			}
			if (flush_current) {
				if (plast)
					PKTSETNEXT(dhd->osh, plast, cur_pkt);
				else
					*pkt = cur_pkt;
				cnt++;
			}
			else {
				ptr->p[cur_idx] = cur_pkt;
				ptr->pend_pkts++;
			}
			ptr->exp_idx = exp_idx;
			ptr->cur_idx = cur_idx;
			*pkt_count = cnt;
		}
	}
	else {
		uint8 end_idx;
		/* no real packet but update to exp_seq...that means explicit window move */
		exp_idx = reorder_info_buf[WLHOST_REORDERDATA_EXPIDX_OFFSET];

		DHD_REORDER(("%s: move the window, cur_idx is %d, exp is %d, new exp is %d\n",
			__FUNCTION__, ptr->cur_idx, ptr->exp_idx, exp_idx));
		if (flags & WLHOST_REORDERDATA_FLUSH_ALL)
			end_idx =  ptr->exp_idx;
		else
			end_idx =  exp_idx;

		dhd_get_hostreorder_pkts(dhd->osh, ptr, pkt, &cnt, &plast, ptr->exp_idx, end_idx);
		if (plast)
			PKTSETNEXT(dhd->osh, plast, cur_pkt);
		else
			*pkt = cur_pkt;
		cnt++;
		*pkt_count = cnt;
		/* set the new expected idx */
		ptr->exp_idx = exp_idx;
	}
	return 0;
}
