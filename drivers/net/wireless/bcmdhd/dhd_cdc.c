/*
 * DHD Protocol Module for CDC and BDC.
 *
 * Copyright (C) 1999-2012, Broadcom Corporation
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
 * $Id: dhd_cdc.c 325074 2012-03-31 21:24:57Z $
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
#define BUS_HEADER_LEN	(16+DHD_SDALIGN)	/* Must be at least SDPCM_RESERVE
				 * defined in dhd_sdio.c (amount of header tha might be added)
				 * plus any space that might be needed for alignment padding.
				 */
#define ROUND_UP_MARGIN	2048	/* Biggest SDIO block size possible for
				 * round off at the end of buffer
				 */

#define BUS_RETRIES 1	/* # of retries before aborting a bus tx operation */

#ifdef PROP_TXSTATUS
typedef struct dhd_wlfc_commit_info {
	uint8					needs_hdr;
	uint8					ac_fifo_credit_spent;
	ewlfc_packet_state_t	pkt_type;
	wlfc_mac_descriptor_t*	mac_entry;
	void*					p;
} dhd_wlfc_commit_info_t;
#endif /* PROP_TXSTATUS */

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
	void *info;
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

	/* Check info buffer */
	info = (void*)&msg[1];

	/* Copy info buffer */
	if (buf)
	{
		if (ret < (int)len)
			len = ret;
		memcpy(buf, info, len);
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
#if defined(NDIS630)
	bool acquired = FALSE;
#endif

	if ((dhd->busstate == DHD_BUS_DOWN) || dhd->hang_was_sent) {
		DHD_ERROR(("%s : bus is down. we have nothing to do\n", __FUNCTION__));
		goto done;
	}
#if defined(NDIS630)
	if (dhd_os_proto_block(dhd))
	{
		acquired = TRUE;
	}
	else
	{
		/* attempt to acquire protocol mutex timed out. */
		ret = -1;
		return ret;
	}
#endif /* NDIS630 */

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
#if defined(NDIS630)
	if (acquired)
	   dhd_os_proto_unblock(dhd);
#endif
	return ret;
}

int
dhd_prot_iovar_op(dhd_pub_t *dhdp, const char *name,
                  void *params, int plen, void *arg, int len, bool set)
{
	return BCME_UNSUPPORTED;
}

#ifdef PROP_TXSTATUS
void
dhd_wlfc_dump(dhd_pub_t *dhdp, struct bcmstrbuf *strbuf)
{
	int i;
	uint8* ea;
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)
		dhdp->wlfc_state;
	wlfc_hanger_t* h;
	wlfc_mac_descriptor_t* mac_table;
	wlfc_mac_descriptor_t* interfaces;
	char* iftypes[] = {"STA", "AP", "WDS", "p2pGO", "p2pCL"};

	if (wlfc == NULL) {
		bcm_bprintf(strbuf, "wlfc not initialized yet\n");
		return;
	}
	h = (wlfc_hanger_t*)wlfc->hanger;
	if (h == NULL) {
		bcm_bprintf(strbuf, "wlfc-hanger not initialized yet\n");
	}

	mac_table = wlfc->destination_entries.nodes;
	interfaces = wlfc->destination_entries.interfaces;
	bcm_bprintf(strbuf, "---- wlfc stats ----\n");
	if (h) {
		bcm_bprintf(strbuf, "wlfc hanger (pushed,popped,f_push,"
			"f_pop,f_slot, pending) = (%d,%d,%d,%d,%d,%d)\n",
			h->pushed,
			h->popped,
			h->failed_to_push,
			h->failed_to_pop,
			h->failed_slotfind,
			(h->pushed - h->popped));
	}

	bcm_bprintf(strbuf, "wlfc fail(tlv,credit_rqst,mac_update,psmode_update), "
		"(dq_full,sendq_full, rollback_fail) = (%d,%d,%d,%d), (%d,%d,%d)\n",
		wlfc->stats.tlv_parse_failed,
		wlfc->stats.credit_request_failed,
		wlfc->stats.mac_update_failed,
		wlfc->stats.psmode_update_failed,
		wlfc->stats.delayq_full_error,
		wlfc->stats.sendq_full_error,
		wlfc->stats.rollback_failed);

	bcm_bprintf(strbuf, "SENDQ (len,credit,sent) "
		"(AC0[%d,%d,%d],AC1[%d,%d,%d],AC2[%d,%d,%d],AC3[%d,%d,%d],BC_MC[%d,%d,%d])\n",
		wlfc->SENDQ.q[0].len, wlfc->FIFO_credit[0], wlfc->stats.sendq_pkts[0],
		wlfc->SENDQ.q[1].len, wlfc->FIFO_credit[1], wlfc->stats.sendq_pkts[1],
		wlfc->SENDQ.q[2].len, wlfc->FIFO_credit[2], wlfc->stats.sendq_pkts[2],
		wlfc->SENDQ.q[3].len, wlfc->FIFO_credit[3], wlfc->stats.sendq_pkts[3],
		wlfc->SENDQ.q[4].len, wlfc->FIFO_credit[4], wlfc->stats.sendq_pkts[4]);

#ifdef PROP_TXSTATUS_DEBUG
	bcm_bprintf(strbuf, "SENDQ dropped: AC[0-3]:(%d,%d,%d,%d), (bcmc,atim):(%d,%d)\n",
		wlfc->stats.dropped_qfull[0], wlfc->stats.dropped_qfull[1],
		wlfc->stats.dropped_qfull[2], wlfc->stats.dropped_qfull[3],
		wlfc->stats.dropped_qfull[4], wlfc->stats.dropped_qfull[5]);
#endif

	bcm_bprintf(strbuf, "\n");
	for (i = 0; i < WLFC_MAX_IFNUM; i++) {
		if (interfaces[i].occupied) {
			char* iftype_desc;

			if (interfaces[i].iftype > WLC_E_IF_ROLE_P2P_CLIENT)
				iftype_desc = "<Unknown";
			else
				iftype_desc = iftypes[interfaces[i].iftype];

			ea = interfaces[i].ea;
			bcm_bprintf(strbuf, "INTERFACE[%d].ea = "
				"[%02x:%02x:%02x:%02x:%02x:%02x], if:%d, type: %s\n", i,
				ea[0], ea[1], ea[2], ea[3], ea[4], ea[5],
				interfaces[i].interface_id,
				iftype_desc);

			bcm_bprintf(strbuf, "INTERFACE[%d].DELAYQ(len,state,credit)"
				"= (%d,%s,%d)\n",
				i,
				interfaces[i].psq.len,
				((interfaces[i].state ==
				WLFC_STATE_OPEN) ? " OPEN":"CLOSE"),
				interfaces[i].requested_credit);

			bcm_bprintf(strbuf, "INTERFACE[%d].DELAYQ"
				"(sup,ac0),(sup,ac1),(sup,ac2),(sup,ac3) = "
				"(%d,%d),(%d,%d),(%d,%d),(%d,%d)\n",
				i,
				interfaces[i].psq.q[0].len,
				interfaces[i].psq.q[1].len,
				interfaces[i].psq.q[2].len,
				interfaces[i].psq.q[3].len,
				interfaces[i].psq.q[4].len,
				interfaces[i].psq.q[5].len,
				interfaces[i].psq.q[6].len,
				interfaces[i].psq.q[7].len);
		}
	}

	bcm_bprintf(strbuf, "\n");
	for (i = 0; i < WLFC_MAC_DESC_TABLE_SIZE; i++) {
		if (mac_table[i].occupied) {
			ea = mac_table[i].ea;
			bcm_bprintf(strbuf, "MAC_table[%d].ea = "
				"[%02x:%02x:%02x:%02x:%02x:%02x], if:%d\n", i,
				ea[0], ea[1], ea[2], ea[3], ea[4], ea[5],
				mac_table[i].interface_id);

			bcm_bprintf(strbuf, "MAC_table[%d].DELAYQ(len,state,credit)"
				"= (%d,%s,%d)\n",
				i,
				mac_table[i].psq.len,
				((mac_table[i].state ==
				WLFC_STATE_OPEN) ? " OPEN":"CLOSE"),
				mac_table[i].requested_credit);
#ifdef PROP_TXSTATUS_DEBUG
			bcm_bprintf(strbuf, "MAC_table[%d]: (opened, closed) = (%d, %d)\n",
				i, mac_table[i].opened_ct, mac_table[i].closed_ct);
#endif
			bcm_bprintf(strbuf, "MAC_table[%d].DELAYQ"
				"(sup,ac0),(sup,ac1),(sup,ac2),(sup,ac3) = "
				"(%d,%d),(%d,%d),(%d,%d),(%d,%d)\n",
				i,
				mac_table[i].psq.q[0].len,
				mac_table[i].psq.q[1].len,
				mac_table[i].psq.q[2].len,
				mac_table[i].psq.q[3].len,
				mac_table[i].psq.q[4].len,
				mac_table[i].psq.q[5].len,
				mac_table[i].psq.q[6].len,
				mac_table[i].psq.q[7].len);
		}
	}

#ifdef PROP_TXSTATUS_DEBUG
	{
		int avg;
		int moving_avg = 0;
		int moving_samples;

		if (wlfc->stats.latency_sample_count) {
			moving_samples = sizeof(wlfc->stats.deltas)/sizeof(uint32);

			for (i = 0; i < moving_samples; i++)
				moving_avg += wlfc->stats.deltas[i];
			moving_avg /= moving_samples;

			avg = (100 * wlfc->stats.total_status_latency) /
				wlfc->stats.latency_sample_count;
			bcm_bprintf(strbuf, "txstatus latency (average, last, moving[%d]) = "
				"(%d.%d, %03d, %03d)\n",
				moving_samples, avg/100, (avg - (avg/100)*100),
				wlfc->stats.latency_most_recent,
				moving_avg);
		}
	}

	bcm_bprintf(strbuf, "wlfc- fifo[0-5] credit stats: sent = (%d,%d,%d,%d,%d,%d), "
		"back = (%d,%d,%d,%d,%d,%d)\n",
		wlfc->stats.fifo_credits_sent[0],
		wlfc->stats.fifo_credits_sent[1],
		wlfc->stats.fifo_credits_sent[2],
		wlfc->stats.fifo_credits_sent[3],
		wlfc->stats.fifo_credits_sent[4],
		wlfc->stats.fifo_credits_sent[5],

		wlfc->stats.fifo_credits_back[0],
		wlfc->stats.fifo_credits_back[1],
		wlfc->stats.fifo_credits_back[2],
		wlfc->stats.fifo_credits_back[3],
		wlfc->stats.fifo_credits_back[4],
		wlfc->stats.fifo_credits_back[5]);
	{
		uint32 fifo_cr_sent = 0;
		uint32 fifo_cr_acked = 0;
		uint32 request_cr_sent = 0;
		uint32 request_cr_ack = 0;
		uint32 bc_mc_cr_ack = 0;

		for (i = 0; i < sizeof(wlfc->stats.fifo_credits_sent)/sizeof(uint32); i++) {
			fifo_cr_sent += wlfc->stats.fifo_credits_sent[i];
		}

		for (i = 0; i < sizeof(wlfc->stats.fifo_credits_back)/sizeof(uint32); i++) {
			fifo_cr_acked += wlfc->stats.fifo_credits_back[i];
		}

		for (i = 0; i < WLFC_MAC_DESC_TABLE_SIZE; i++) {
			if (wlfc->destination_entries.nodes[i].occupied) {
				request_cr_sent +=
					wlfc->destination_entries.nodes[i].dstncredit_sent_packets;
			}
		}
		for (i = 0; i < WLFC_MAX_IFNUM; i++) {
			if (wlfc->destination_entries.interfaces[i].occupied) {
				request_cr_sent +=
				wlfc->destination_entries.interfaces[i].dstncredit_sent_packets;
			}
		}
		for (i = 0; i < WLFC_MAC_DESC_TABLE_SIZE; i++) {
			if (wlfc->destination_entries.nodes[i].occupied) {
				request_cr_ack +=
					wlfc->destination_entries.nodes[i].dstncredit_acks;
			}
		}
		for (i = 0; i < WLFC_MAX_IFNUM; i++) {
			if (wlfc->destination_entries.interfaces[i].occupied) {
				request_cr_ack +=
					wlfc->destination_entries.interfaces[i].dstncredit_acks;
			}
		}
		bcm_bprintf(strbuf, "wlfc- (sent, status) => pq(%d,%d), vq(%d,%d),"
			"other:%d, bc_mc:%d, signal-only, (sent,freed): (%d,%d)",
			fifo_cr_sent, fifo_cr_acked,
			request_cr_sent, request_cr_ack,
			wlfc->destination_entries.other.dstncredit_acks,
			bc_mc_cr_ack,
			wlfc->stats.signal_only_pkts_sent, wlfc->stats.signal_only_pkts_freed);
	}
#endif /* PROP_TXSTATUS_DEBUG */
	bcm_bprintf(strbuf, "\n");
	bcm_bprintf(strbuf, "wlfc- pkt((in,2bus,txstats,hdrpull),(dropped,hdr_only,wlc_tossed)"
		"(freed,free_err,rollback)) = "
		"((%d,%d,%d,%d),(%d,%d,%d),(%d,%d,%d))\n",
		wlfc->stats.pktin,
		wlfc->stats.pkt2bus,
		wlfc->stats.txstatus_in,
		wlfc->stats.dhd_hdrpulls,

		wlfc->stats.pktdropped,
		wlfc->stats.wlfc_header_only_pkt,
		wlfc->stats.wlc_tossed_pkts,

		wlfc->stats.pkt_freed,
		wlfc->stats.pkt_free_err, wlfc->stats.rollback);

	bcm_bprintf(strbuf, "wlfc- suppress((d11,wlc,err),enq(d11,wl,hq,mac?),retx(d11,wlc,hq)) = "
		"((%d,%d,%d),(%d,%d,%d,%d),(%d,%d,%d))\n",

		wlfc->stats.d11_suppress,
		wlfc->stats.wl_suppress,
		wlfc->stats.bad_suppress,

		wlfc->stats.psq_d11sup_enq,
		wlfc->stats.psq_wlsup_enq,
		wlfc->stats.psq_hostq_enq,
		wlfc->stats.mac_handle_notfound,

		wlfc->stats.psq_d11sup_retx,
		wlfc->stats.psq_wlsup_retx,
		wlfc->stats.psq_hostq_retx);
	return;
}

/* Create a place to store all packet pointers submitted to the firmware until
	a status comes back, suppress or otherwise.

	hang-er: noun, a contrivance on which things are hung, as a hook.
*/
static void*
dhd_wlfc_hanger_create(osl_t *osh, int max_items)
{
	int i;
	wlfc_hanger_t* hanger;

	/* allow only up to a specific size for now */
	ASSERT(max_items == WLFC_HANGER_MAXITEMS);

	if ((hanger = (wlfc_hanger_t*)MALLOC(osh, WLFC_HANGER_SIZE(max_items))) == NULL)
		return NULL;

	memset(hanger, 0, WLFC_HANGER_SIZE(max_items));
	hanger->max_items = max_items;

	for (i = 0; i < hanger->max_items; i++) {
		hanger->items[i].state = WLFC_HANGER_ITEM_STATE_FREE;
	}
	return hanger;
}

static int
dhd_wlfc_hanger_delete(osl_t *osh, void* hanger)
{
	wlfc_hanger_t* h = (wlfc_hanger_t*)hanger;

	if (h) {
		MFREE(osh, h, WLFC_HANGER_SIZE(h->max_items));
		return BCME_OK;
	}
	return BCME_BADARG;
}

static uint16
dhd_wlfc_hanger_get_free_slot(void* hanger)
{
	int i;
	wlfc_hanger_t* h = (wlfc_hanger_t*)hanger;

	if (h) {
		for (i = 0; i < h->max_items; i++) {
			if (h->items[i].state == WLFC_HANGER_ITEM_STATE_FREE)
				return (uint16)i;
		}
		h->failed_slotfind++;
	}
	return WLFC_HANGER_MAXITEMS;
}

static int
dhd_wlfc_hanger_pushpkt(void* hanger, void* pkt, uint32 slot_id)
{
	int rc = BCME_OK;
	wlfc_hanger_t* h = (wlfc_hanger_t*)hanger;

	if (h && (slot_id < WLFC_HANGER_MAXITEMS)) {
		if (h->items[slot_id].state == WLFC_HANGER_ITEM_STATE_FREE) {
			h->items[slot_id].state = WLFC_HANGER_ITEM_STATE_INUSE;
			h->items[slot_id].pkt = pkt;
			h->items[slot_id].identifier = slot_id;
			h->pushed++;
		}
		else {
			h->failed_to_push++;
			rc = BCME_NOTFOUND;
		}
	}
	else
		rc = BCME_BADARG;
	return rc;
}

static int
dhd_wlfc_hanger_poppkt(void* hanger, uint32 slot_id, void** pktout, int remove_from_hanger)
{
	int rc = BCME_OK;
	wlfc_hanger_t* h = (wlfc_hanger_t*)hanger;

	/* this packet was not pushed at the time it went to the firmware */
	if (slot_id == WLFC_HANGER_MAXITEMS)
		return BCME_NOTFOUND;

	if (h) {
		if (h->items[slot_id].state == WLFC_HANGER_ITEM_STATE_INUSE) {
			*pktout = h->items[slot_id].pkt;
			if (remove_from_hanger) {
				h->items[slot_id].state =
					WLFC_HANGER_ITEM_STATE_FREE;
				h->items[slot_id].pkt = NULL;
				h->items[slot_id].identifier = 0;
				h->popped++;
			}
		}
		else {
			h->failed_to_pop++;
			rc = BCME_NOTFOUND;
		}
	}
	else
		rc = BCME_BADARG;
	return rc;
}

static int
_dhd_wlfc_pushheader(athost_wl_status_info_t* ctx, void* p, bool tim_signal,
	uint8 tim_bmp, uint8 mac_handle, uint32 htodtag)
{
	uint32 wl_pktinfo = 0;
	uint8* wlh;
	uint8 dataOffset;
	uint8 fillers;
	uint8 tim_signal_len = 0;

	struct bdc_header *h;

	if (tim_signal) {
		tim_signal_len = 1 + 1 + WLFC_CTL_VALUE_LEN_PENDING_TRAFFIC_BMP;
	}

	/* +2 is for Type[1] and Len[1] in TLV, plus TIM signal */
	dataOffset = WLFC_CTL_VALUE_LEN_PKTTAG + 2 + tim_signal_len;
	fillers = ROUNDUP(dataOffset, 4) - dataOffset;
	dataOffset += fillers;

	PKTPUSH(ctx->osh, p, dataOffset);
	wlh = (uint8*) PKTDATA(ctx->osh, p);

	wl_pktinfo = htol32(htodtag);

	wlh[0] = WLFC_CTL_TYPE_PKTTAG;
	wlh[1] = WLFC_CTL_VALUE_LEN_PKTTAG;
	memcpy(&wlh[2], &wl_pktinfo, sizeof(uint32));

	if (tim_signal_len) {
		wlh[dataOffset - fillers - tim_signal_len ] =
			WLFC_CTL_TYPE_PENDING_TRAFFIC_BMP;
		wlh[dataOffset - fillers - tim_signal_len + 1] =
			WLFC_CTL_VALUE_LEN_PENDING_TRAFFIC_BMP;
		wlh[dataOffset - fillers - tim_signal_len + 2] = mac_handle;
		wlh[dataOffset - fillers - tim_signal_len + 3] = tim_bmp;
	}
	if (fillers)
		memset(&wlh[dataOffset - fillers], WLFC_CTL_TYPE_FILLER, fillers);

	PKTPUSH(ctx->osh, p, BDC_HEADER_LEN);
	h = (struct bdc_header *)PKTDATA(ctx->osh, p);
	h->flags = (BDC_PROTO_VER << BDC_FLAG_VER_SHIFT);
	if (PKTSUMNEEDED(p))
		h->flags |= BDC_FLAG_SUM_NEEDED;


	h->priority = (PKTPRIO(p) & BDC_PRIORITY_MASK);
	h->flags2 = 0;
	h->dataOffset = dataOffset >> 2;
	BDC_SET_IF_IDX(h, DHD_PKTTAG_IF(PKTTAG(p)));
	return BCME_OK;
}

static int
_dhd_wlfc_pullheader(athost_wl_status_info_t* ctx, void* pktbuf)
{
	struct bdc_header *h;

	if (PKTLEN(ctx->osh, pktbuf) < BDC_HEADER_LEN) {
		WLFC_DBGMESG(("%s: rx data too short (%d < %d)\n", __FUNCTION__,
		           PKTLEN(ctx->osh, pktbuf), BDC_HEADER_LEN));
		return BCME_ERROR;
	}
	h = (struct bdc_header *)PKTDATA(ctx->osh, pktbuf);

	/* pull BDC header */
	PKTPULL(ctx->osh, pktbuf, BDC_HEADER_LEN);
	/* pull wl-header */
	PKTPULL(ctx->osh, pktbuf, (h->dataOffset << 2));
	return BCME_OK;
}

static wlfc_mac_descriptor_t*
_dhd_wlfc_find_table_entry(athost_wl_status_info_t* ctx, void* p)
{
	int i;
	wlfc_mac_descriptor_t* table = ctx->destination_entries.nodes;
	uint8 ifid = DHD_PKTTAG_IF(PKTTAG(p));
	uint8* dstn = DHD_PKTTAG_DSTN(PKTTAG(p));

	if (((ctx->destination_entries.interfaces[ifid].iftype == WLC_E_IF_ROLE_STA) ||
		ETHER_ISMULTI(dstn) ||
		(ctx->destination_entries.interfaces[ifid].iftype == WLC_E_IF_ROLE_P2P_CLIENT)) &&
		(ctx->destination_entries.interfaces[ifid].occupied)) {
			return &ctx->destination_entries.interfaces[ifid];
	}

	for (i = 0; i < WLFC_MAC_DESC_TABLE_SIZE; i++) {
		if (table[i].occupied) {
			if (table[i].interface_id == ifid) {
				if (!memcmp(table[i].ea, dstn, ETHER_ADDR_LEN))
					return &table[i];
			}
		}
	}
	return &ctx->destination_entries.other;
}

static int
_dhd_wlfc_rollback_packet_toq(athost_wl_status_info_t* ctx,
	void* p, ewlfc_packet_state_t pkt_type, uint32 hslot)
{
	/*
	put the packet back to the head of queue

	- a packet from send-q will need to go back to send-q and not delay-q
	since that will change the order of packets.
	- suppressed packet goes back to suppress sub-queue
	- pull out the header, if new or delayed packet

	Note: hslot is used only when header removal is done.
	*/
	wlfc_mac_descriptor_t* entry;
	void* pktout;
	int rc = BCME_OK;
	int prec;

	entry = _dhd_wlfc_find_table_entry(ctx, p);
	prec = DHD_PKTTAG_FIFO(PKTTAG(p));
	if (entry != NULL) {
		if (pkt_type == eWLFC_PKTTYPE_SUPPRESSED) {
			/* wl-header is saved for suppressed packets */
			if (WLFC_PKTQ_PENQ_HEAD(&entry->psq, ((prec << 1) + 1), p) == NULL) {
				WLFC_DBGMESG(("Error: %s():%d\n", __FUNCTION__, __LINE__));
				rc = BCME_ERROR;
			}
		}
		else {
			/* remove header first */
			_dhd_wlfc_pullheader(ctx, p);

			if (pkt_type == eWLFC_PKTTYPE_DELAYED) {
				/* delay-q packets are going to delay-q */
				if (WLFC_PKTQ_PENQ_HEAD(&entry->psq, (prec << 1), p) == NULL) {
					WLFC_DBGMESG(("Error: %s():%d\n", __FUNCTION__, __LINE__));
					rc = BCME_ERROR;
				}
			}
			else {
				/* these are going to SENDQ */
				if (WLFC_PKTQ_PENQ_HEAD(&ctx->SENDQ, prec, p) == NULL) {
					WLFC_DBGMESG(("Error: %s():%d\n", __FUNCTION__, __LINE__));
					rc = BCME_ERROR;
				}
			}
			/* free the hanger slot */
			dhd_wlfc_hanger_poppkt(ctx->hanger, hslot, &pktout, 1);

			/* decrement sequence count */
			WLFC_DECR_SEQCOUNT(entry, prec);
		}
		/*
		if this packet did not count against FIFO credit, it must have
		taken a requested_credit from the firmware (for pspoll etc.)
		*/
		if (!DHD_PKTTAG_CREDITCHECK(PKTTAG(p))) {
			entry->requested_credit++;
		}
	}
	else {
		WLFC_DBGMESG(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		rc = BCME_ERROR;
	}
	if (rc != BCME_OK)
		ctx->stats.rollback_failed++;
	else
		ctx->stats.rollback++;

	return rc;
}

static void
_dhd_wlfc_flow_control_check(athost_wl_status_info_t* ctx, struct pktq* pq, uint8 if_id)
{
	if ((pq->len <= WLFC_FLOWCONTROL_LOWATER) && (ctx->hostif_flow_state[if_id] == ON)) {
		/* start traffic */
		ctx->hostif_flow_state[if_id] = OFF;
		/*
		WLFC_DBGMESG(("qlen:%02d, if:%02d, ->OFF, start traffic %s()\n",
		pq->len, if_id, __FUNCTION__));
		*/
		WLFC_DBGMESG(("F"));
		/* dhd_txflowcontrol(ctx->dhdp, if_id, OFF); */
		ctx->toggle_host_if = 0;
	}
	if ((pq->len >= WLFC_FLOWCONTROL_HIWATER) && (ctx->hostif_flow_state[if_id] == OFF)) {
		/* stop traffic */
		ctx->hostif_flow_state[if_id] = ON;
		/*
		WLFC_DBGMESG(("qlen:%02d, if:%02d, ->ON, stop traffic   %s()\n",
		pq->len, if_id, __FUNCTION__));
		*/
		WLFC_DBGMESG(("N"));
		/* dhd_txflowcontrol(ctx->dhdp, if_id, ON); */
		ctx->host_ifidx = if_id;
		ctx->toggle_host_if = 1;
	}
	return;
}

static int
_dhd_wlfc_send_signalonly_packet(athost_wl_status_info_t* ctx, wlfc_mac_descriptor_t* entry,
	uint8 ta_bmp)
{
	int rc = BCME_OK;
	void* p = NULL;
	int dummylen = ((dhd_pub_t *)ctx->dhdp)->hdrlen+ 12;

	/* allocate a dummy packet */
	p = PKTGET(ctx->osh, dummylen, TRUE);
	if (p) {
		PKTPULL(ctx->osh, p, dummylen);
		DHD_PKTTAG_SET_H2DTAG(PKTTAG(p), 0);
		_dhd_wlfc_pushheader(ctx, p, TRUE, ta_bmp, entry->mac_handle, 0);
		DHD_PKTTAG_SETSIGNALONLY(PKTTAG(p), 1);
#ifdef PROP_TXSTATUS_DEBUG
		ctx->stats.signal_only_pkts_sent++;
#endif
		rc = dhd_bus_txdata(((dhd_pub_t *)ctx->dhdp)->bus, p);
		if (rc != BCME_OK) {
			PKTFREE(ctx->osh, p, TRUE);
		}
	}
	else {
		DHD_ERROR(("%s: couldn't allocate new %d-byte packet\n",
		           __FUNCTION__, dummylen));
		rc = BCME_NOMEM;
	}
	return rc;
}

/* Return TRUE if traffic availability changed */
static bool
_dhd_wlfc_traffic_pending_check(athost_wl_status_info_t* ctx, wlfc_mac_descriptor_t* entry,
	int prec)
{
	bool rc = FALSE;

	if (entry->state == WLFC_STATE_CLOSE) {
		if ((pktq_plen(&entry->psq, (prec << 1)) == 0) &&
			(pktq_plen(&entry->psq, ((prec << 1) + 1)) == 0)) {

			if (entry->traffic_pending_bmp & NBITVAL(prec)) {
				rc = TRUE;
				entry->traffic_pending_bmp =
					entry->traffic_pending_bmp & ~ NBITVAL(prec);
			}
		}
		else {
			if (!(entry->traffic_pending_bmp & NBITVAL(prec))) {
				rc = TRUE;
				entry->traffic_pending_bmp =
					entry->traffic_pending_bmp | NBITVAL(prec);
			}
		}
	}
	if (rc) {
		/* request a TIM update to firmware at the next piggyback opportunity */
		if (entry->traffic_lastreported_bmp != entry->traffic_pending_bmp) {
			entry->send_tim_signal = 1;
			_dhd_wlfc_send_signalonly_packet(ctx, entry, entry->traffic_pending_bmp);
			entry->traffic_lastreported_bmp = entry->traffic_pending_bmp;
			entry->send_tim_signal = 0;
		}
		else {
			rc = FALSE;
		}
	}
	return rc;
}

static int
_dhd_wlfc_enque_suppressed(athost_wl_status_info_t* ctx, int prec, void* p)
{
	wlfc_mac_descriptor_t* entry;

	entry = _dhd_wlfc_find_table_entry(ctx, p);
	if (entry == NULL) {
		WLFC_DBGMESG(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_NOTFOUND;
	}
	/*
	- suppressed packets go to sub_queue[2*prec + 1] AND
	- delayed packets go to sub_queue[2*prec + 0] to ensure
	order of delivery.
	*/
	if (WLFC_PKTQ_PENQ(&entry->psq, ((prec << 1) + 1), p) == NULL) {
		ctx->stats.delayq_full_error++;
		/* WLFC_DBGMESG(("Error: %s():%d\n", __FUNCTION__, __LINE__)); */
		WLFC_DBGMESG(("s"));
		return BCME_ERROR;
	}
	/* A packet has been pushed, update traffic availability bitmap, if applicable */
	_dhd_wlfc_traffic_pending_check(ctx, entry, prec);
	_dhd_wlfc_flow_control_check(ctx, &entry->psq, DHD_PKTTAG_IF(PKTTAG(p)));
	return BCME_OK;
}

static int
_dhd_wlfc_pretx_pktprocess(athost_wl_status_info_t* ctx,
	wlfc_mac_descriptor_t* entry, void* p, int header_needed, uint32* slot)
{
	int rc = BCME_OK;
	int hslot = WLFC_HANGER_MAXITEMS;
	bool send_tim_update = FALSE;
	uint32 htod = 0;
	uint8 free_ctr;

	*slot = hslot;

	if (entry == NULL) {
		entry = _dhd_wlfc_find_table_entry(ctx, p);
	}

	if (entry == NULL) {
		WLFC_DBGMESG(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_ERROR;
	}
	if (entry->send_tim_signal) {
		send_tim_update = TRUE;
		entry->send_tim_signal = 0;
		entry->traffic_lastreported_bmp = entry->traffic_pending_bmp;
	}
	if (header_needed) {
		hslot = dhd_wlfc_hanger_get_free_slot(ctx->hanger);
		free_ctr = WLFC_SEQCOUNT(entry, DHD_PKTTAG_FIFO(PKTTAG(p)));
		DHD_PKTTAG_SET_H2DTAG(PKTTAG(p), htod);
	}
	else {
		hslot = WLFC_PKTID_HSLOT_GET(DHD_PKTTAG_H2DTAG(PKTTAG(p)));
		free_ctr = WLFC_PKTID_FREERUNCTR_GET(DHD_PKTTAG_H2DTAG(PKTTAG(p)));
	}
	WLFC_PKTID_HSLOT_SET(htod, hslot);
	WLFC_PKTID_FREERUNCTR_SET(htod, free_ctr);
	DHD_PKTTAG_SETPKTDIR(PKTTAG(p), 1);
	WL_TXSTATUS_SET_FLAGS(htod, WLFC_PKTFLAG_PKTFROMHOST);
	WL_TXSTATUS_SET_FIFO(htod, DHD_PKTTAG_FIFO(PKTTAG(p)));
	WLFC_PKTFLAG_SET_GENERATION(htod, entry->generation);

	if (!DHD_PKTTAG_CREDITCHECK(PKTTAG(p))) {
		/*
		Indicate that this packet is being sent in response to an
		explicit request from the firmware side.
		*/
		WLFC_PKTFLAG_SET_PKTREQUESTED(htod);
	}
	else {
		WLFC_PKTFLAG_CLR_PKTREQUESTED(htod);
	}
	if (header_needed) {
		rc = _dhd_wlfc_pushheader(ctx, p, send_tim_update,
			entry->traffic_lastreported_bmp, entry->mac_handle, htod);
		if (rc == BCME_OK) {
			DHD_PKTTAG_SET_H2DTAG(PKTTAG(p), htod);
			/*
			a new header was created for this packet.
			push to hanger slot and scrub q. Since bus
			send succeeded, increment seq number as well.
			*/
			rc = dhd_wlfc_hanger_pushpkt(ctx->hanger, p, hslot);
			if (rc == BCME_OK) {
				/* increment free running sequence count */
				WLFC_INCR_SEQCOUNT(entry, DHD_PKTTAG_FIFO(PKTTAG(p)));
#ifdef PROP_TXSTATUS_DEBUG
				((wlfc_hanger_t*)(ctx->hanger))->items[hslot].push_time =
					OSL_SYSUPTIME();
#endif
			}
			else {
				WLFC_DBGMESG(("%s() hanger_pushpkt() failed, rc: %d\n",
					__FUNCTION__, rc));
			}
		}
	}
	else {
		/* remove old header */
		_dhd_wlfc_pullheader(ctx, p);

		hslot = WLFC_PKTID_HSLOT_GET(DHD_PKTTAG_H2DTAG(PKTTAG(p)));
		free_ctr = WLFC_PKTID_FREERUNCTR_GET(DHD_PKTTAG_H2DTAG(PKTTAG(p)));
		/* push new header */
		_dhd_wlfc_pushheader(ctx, p, send_tim_update,
			entry->traffic_lastreported_bmp, entry->mac_handle, htod);
	}
	*slot = hslot;
	return rc;
}

static int
_dhd_wlfc_is_destination_closed(athost_wl_status_info_t* ctx,
	wlfc_mac_descriptor_t* entry, int prec)
{
	if (ctx->destination_entries.interfaces[entry->interface_id].iftype ==
		WLC_E_IF_ROLE_P2P_GO) {
		/* - destination interface is of type p2p GO.
		For a p2pGO interface, if the destination is OPEN but the interface is
		CLOSEd, do not send traffic. But if the dstn is CLOSEd while there is
		destination-specific-credit left send packets. This is because the
		firmware storing the destination-specific-requested packet in queue.
		*/
		if ((entry->state == WLFC_STATE_CLOSE) && (entry->requested_credit == 0) &&
			(entry->requested_packet == 0))
			return 1;
	}
	/* AP, p2p_go -> unicast desc entry, STA/p2p_cl -> interface desc. entry */
	if (((entry->state == WLFC_STATE_CLOSE) && (entry->requested_credit == 0) &&
		(entry->requested_packet == 0)) ||
		(!(entry->ac_bitmap & (1 << prec))))
		return 1;

	return 0;
}

static void*
_dhd_wlfc_deque_delayedq(athost_wl_status_info_t* ctx,
	int prec, uint8* ac_credit_spent, uint8* needs_hdr, wlfc_mac_descriptor_t** entry_out)
{
	wlfc_mac_descriptor_t* entry;
	wlfc_mac_descriptor_t* table;
	uint8 token_pos;
	int total_entries;
	void* p = NULL;
	int pout;
	int i;

	*entry_out = NULL;
	token_pos = ctx->token_pos[prec];
	/* most cases a packet will count against FIFO credit */
	*ac_credit_spent = 1;
	*needs_hdr = 1;

	/* search all entries, include nodes as well as interfaces */
	table = (wlfc_mac_descriptor_t*)&ctx->destination_entries;
	total_entries = sizeof(ctx->destination_entries)/sizeof(wlfc_mac_descriptor_t);

	for (i = 0; i < total_entries; i++) {
		entry = &table[(token_pos + i) % total_entries];
		if (entry->occupied) {
			if (!_dhd_wlfc_is_destination_closed(ctx, entry, prec)) {
				p = pktq_mdeq(&entry->psq,
					/* higher precedence will be picked up first,
					i.e. suppressed packets before delayed ones
					*/
					(NBITVAL((prec << 1) + 1) | NBITVAL((prec << 1))),
					&pout);
				if (p != NULL) {
					/* did the packet come from suppress sub-queue? */
					if (pout == ((prec << 1) + 1)) {
						/*
						this packet was suppressed and was sent on the bus
						previously; this already has a header
						*/
						*needs_hdr = 0;
					}
					if (entry->requested_credit > 0) {
						entry->requested_credit--;
#ifdef PROP_TXSTATUS_DEBUG
						entry->dstncredit_sent_packets++;
#endif
						/*
						if the packet was pulled out while destination is in
						closed state but had a non-zero packets requested,
						then this should not count against the FIFO credit.
						That is due to the fact that the firmware will
						most likely hold onto this packet until a suitable
						time later to push it to the appropriate  AC FIFO.
						*/
						if (entry->state == WLFC_STATE_CLOSE)
							*ac_credit_spent = 0;
					}
					else if (entry->requested_packet > 0) {
						entry->requested_packet--;
						DHD_PKTTAG_SETONETIMEPKTRQST(PKTTAG(p));
						if (entry->state == WLFC_STATE_CLOSE)
							*ac_credit_spent = 0;
					}
					/* move token to ensure fair round-robin */
					ctx->token_pos[prec] =
						(token_pos + i + 1) % total_entries;
					*entry_out = entry;
					_dhd_wlfc_flow_control_check(ctx, &entry->psq,
						DHD_PKTTAG_IF(PKTTAG(p)));
					/*
					A packet has been picked up, update traffic
					availability bitmap, if applicable
					*/
					_dhd_wlfc_traffic_pending_check(ctx, entry, prec);
					return p;
				}
			}
		}
	}
	return NULL;
}

static void*
_dhd_wlfc_deque_sendq(athost_wl_status_info_t* ctx, int prec, uint8* ac_credit_spent)
{
	wlfc_mac_descriptor_t* entry;
	void* p;

	/* most cases a packet will count against FIFO credit */
	*ac_credit_spent = 1;

	p = pktq_pdeq(&ctx->SENDQ, prec);
	if (p != NULL) {
		if (ETHER_ISMULTI(DHD_PKTTAG_DSTN(PKTTAG(p))))
			/* bc/mc packets do not have a delay queue */
			return p;

		entry = _dhd_wlfc_find_table_entry(ctx, p);

		if (entry == NULL) {
			WLFC_DBGMESG(("Error: %s():%d\n", __FUNCTION__, __LINE__));
			return p;
		}

		while ((p != NULL) && _dhd_wlfc_is_destination_closed(ctx, entry, prec)) {
			/*
			- suppressed packets go to sub_queue[2*prec + 1] AND
			- delayed packets go to sub_queue[2*prec + 0] to ensure
			order of delivery.
			*/
			if (WLFC_PKTQ_PENQ(&entry->psq, (prec << 1), p) == NULL) {
				WLFC_DBGMESG(("D"));
				/* dhd_txcomplete(ctx->dhdp, p, FALSE); */
				PKTFREE(ctx->osh, p, TRUE);
				ctx->stats.delayq_full_error++;
			}
			/*
			A packet has been pushed, update traffic availability bitmap,
			if applicable
			*/
			_dhd_wlfc_traffic_pending_check(ctx, entry, prec);
			_dhd_wlfc_flow_control_check(ctx, &entry->psq, DHD_PKTTAG_IF(PKTTAG(p)));
			p = pktq_pdeq(&ctx->SENDQ, prec);
			if (p == NULL)
				break;

			entry = _dhd_wlfc_find_table_entry(ctx, p);

			if ((entry == NULL) || (ETHER_ISMULTI(DHD_PKTTAG_DSTN(PKTTAG(p))))) {
				return p;
			}
		}
		if (p) {
			if (entry->requested_packet == 0) {
				if (entry->requested_credit > 0)
					entry->requested_credit--;
			}
			else {
				entry->requested_packet--;
				DHD_PKTTAG_SETONETIMEPKTRQST(PKTTAG(p));
			}
			if (entry->state == WLFC_STATE_CLOSE)
				*ac_credit_spent = 0;
#ifdef PROP_TXSTATUS_DEBUG
			entry->dstncredit_sent_packets++;
#endif
		}
		if (p)
			_dhd_wlfc_flow_control_check(ctx, &ctx->SENDQ, DHD_PKTTAG_IF(PKTTAG(p)));
	}
	return p;
}

static int
_dhd_wlfc_mac_entry_update(athost_wl_status_info_t* ctx, wlfc_mac_descriptor_t* entry,
	ewlfc_mac_entry_action_t action, uint8 ifid, uint8 iftype, uint8* ea)
{
	int rc = BCME_OK;

	if (action == eWLFC_MAC_ENTRY_ACTION_ADD) {
		entry->occupied = 1;
		entry->state = WLFC_STATE_OPEN;
		entry->requested_credit = 0;
		entry->interface_id = ifid;
		entry->iftype = iftype;
		entry->ac_bitmap = 0xff; /* update this when handling APSD */
		/* for an interface entry we may not care about the MAC address */
		if (ea != NULL)
			memcpy(&entry->ea[0], ea, ETHER_ADDR_LEN);
		pktq_init(&entry->psq, WLFC_PSQ_PREC_COUNT, WLFC_PSQ_LEN);
	}
	else if (action == eWLFC_MAC_ENTRY_ACTION_UPDATE) {
		entry->occupied = 1;
		entry->state = WLFC_STATE_OPEN;
		entry->requested_credit = 0;
		entry->interface_id = ifid;
		entry->iftype = iftype;
		entry->ac_bitmap = 0xff; /* update this when handling APSD */
		/* for an interface entry we may not care about the MAC address */
		if (ea != NULL)
			memcpy(&entry->ea[0], ea, ETHER_ADDR_LEN);
	}
	else if (action == eWLFC_MAC_ENTRY_ACTION_DEL) {
		entry->occupied = 0;
		entry->state = WLFC_STATE_CLOSE;
		entry->requested_credit = 0;
		/* enable after packets are queued-deqeued properly.
		pktq_flush(dhd->osh, &entry->psq, FALSE, NULL, 0);
		*/
	}
	return rc;
}

int
_dhd_wlfc_borrow_credit(athost_wl_status_info_t* ctx, uint8 available_credit_map, int borrower_ac)
{
	int lender_ac;
	int rc = BCME_ERROR;

	if (ctx == NULL || available_credit_map == 0) {
		WLFC_DBGMESG(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	/* Borrow from lowest priority available AC (including BC/MC credits) */
	for (lender_ac = 0; lender_ac <= AC_COUNT; lender_ac++) {
		if ((available_credit_map && (1 << lender_ac)) &&
		   (ctx->FIFO_credit[lender_ac] > 0)) {
			ctx->credits_borrowed[borrower_ac][lender_ac]++;
			ctx->FIFO_credit[lender_ac]--;
			rc = BCME_OK;
			break;
		}
	}

	return rc;
}

int
dhd_wlfc_interface_entry_update(void* state,
	ewlfc_mac_entry_action_t action, uint8 ifid, uint8 iftype, uint8* ea)
{
	athost_wl_status_info_t* ctx = (athost_wl_status_info_t*)state;
	wlfc_mac_descriptor_t* entry;

	if (ifid >= WLFC_MAX_IFNUM)
		return BCME_BADARG;

	entry = &ctx->destination_entries.interfaces[ifid];
	return _dhd_wlfc_mac_entry_update(ctx, entry, action, ifid, iftype, ea);
}

int
dhd_wlfc_FIFOcreditmap_update(void* state, uint8* credits)
{
	athost_wl_status_info_t* ctx = (athost_wl_status_info_t*)state;

	/* update the AC FIFO credit map */
	ctx->FIFO_credit[0] = credits[0];
	ctx->FIFO_credit[1] = credits[1];
	ctx->FIFO_credit[2] = credits[2];
	ctx->FIFO_credit[3] = credits[3];
	/* credit for bc/mc packets */
	ctx->FIFO_credit[4] = credits[4];
	/* credit for ATIM FIFO is not used yet. */
	ctx->FIFO_credit[5] = 0;
	return BCME_OK;
}

int
dhd_wlfc_enque_sendq(void* state, int prec, void* p)
{
	athost_wl_status_info_t* ctx = (athost_wl_status_info_t*)state;

	if ((state == NULL) ||
		/* prec = AC_COUNT is used for bc/mc queue */
		(prec > AC_COUNT) ||
		(p == NULL)) {
		WLFC_DBGMESG(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}
	if (FALSE == dhd_prec_enq(ctx->dhdp, &ctx->SENDQ, p, prec)) {
		ctx->stats.sendq_full_error++;
		/*
		WLFC_DBGMESG(("Error: %s():%d, qlen:%d\n",
		__FUNCTION__, __LINE__, ctx->SENDQ.len));
		*/
		WLFC_HOST_FIFO_DROPPEDCTR_INC(ctx, prec);
		WLFC_DBGMESG(("Q"));
		PKTFREE(ctx->osh, p, TRUE);
		return BCME_ERROR;
	}
	ctx->stats.pktin++;
	/* _dhd_wlfc_flow_control_check(ctx, &ctx->SENDQ, DHD_PKTTAG_IF(PKTTAG(p))); */
	return BCME_OK;
}

int
_dhd_wlfc_handle_packet_commit(athost_wl_status_info_t* ctx, int ac,
    dhd_wlfc_commit_info_t *commit_info, f_commitpkt_t fcommit, void* commit_ctx)
{
	uint32 hslot;
	int	rc;

	/*
		if ac_fifo_credit_spent = 0

		This packet will not count against the FIFO credit.
		To ensure the txstatus corresponding to this packet
		does not provide an implied credit (default behavior)
		mark the packet accordingly.

		if ac_fifo_credit_spent = 1

		This is a normal packet and it counts against the FIFO
		credit count.
	*/
	DHD_PKTTAG_SETCREDITCHECK(PKTTAG(commit_info->p), commit_info->ac_fifo_credit_spent);
	rc = _dhd_wlfc_pretx_pktprocess(ctx, commit_info->mac_entry, commit_info->p,
	     commit_info->needs_hdr, &hslot);

	if (rc == BCME_OK)
		rc = fcommit(commit_ctx, commit_info->p);
	else
		ctx->stats.generic_error++;

	if (rc == BCME_OK) {
		ctx->stats.pkt2bus++;
		if (commit_info->ac_fifo_credit_spent) {
			ctx->stats.sendq_pkts[ac]++;
			WLFC_HOST_FIFO_CREDIT_INC_SENTCTRS(ctx, ac);
		}
	}
	else {
		/*
		   bus commit has failed, rollback.
		   - remove wl-header for a delayed packet
		   - save wl-header header for suppressed packets
		*/
		rc = _dhd_wlfc_rollback_packet_toq(ctx,	commit_info->p,
		     (commit_info->pkt_type), hslot);
		if (rc != BCME_OK)
			ctx->stats.rollback_failed++;

		rc = BCME_ERROR;
	}

	return rc;
}

int
dhd_wlfc_commit_packets(void* state, f_commitpkt_t fcommit, void* commit_ctx)
{
	int ac;
	int credit;
	int rc;
	dhd_wlfc_commit_info_t  commit_info;
	athost_wl_status_info_t* ctx = (athost_wl_status_info_t*)state;
	int credit_count = 0;
	int bus_retry_count = 0;
	uint8 ac_available = 0;  /* Bitmask for 4 ACs + BC/MC */

	if ((state == NULL) ||
		(fcommit == NULL)) {
		WLFC_DBGMESG(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	memset(&commit_info, 0, sizeof(commit_info));

	/*
	Commit packets for regular AC traffic. Higher priority first.
	First, use up FIFO credits available to each AC. Based on distribution
	and credits left, borrow from other ACs as applicable

	-NOTE:
	If the bus between the host and firmware is overwhelmed by the
	traffic from host, it is possible that higher priority traffic
	starves the lower priority queue. If that occurs often, we may
	have to employ weighted round-robin or ucode scheme to avoid
	low priority packet starvation.
	*/

	for (ac = AC_COUNT; ac >= 0; ac--) {

		int initial_credit_count = ctx->FIFO_credit[ac];

		for (credit = 0; credit < ctx->FIFO_credit[ac];) {
			commit_info.p = _dhd_wlfc_deque_delayedq(ctx, ac,
			                &(commit_info.ac_fifo_credit_spent),
			                &(commit_info.needs_hdr),
			                &(commit_info.mac_entry));

			if (commit_info.p == NULL)
				break;

			commit_info.pkt_type = (commit_info.needs_hdr) ? eWLFC_PKTTYPE_DELAYED :
				eWLFC_PKTTYPE_SUPPRESSED;

			rc = _dhd_wlfc_handle_packet_commit(ctx, ac, &commit_info,
			     fcommit, commit_ctx);

			/* Bus commits may fail (e.g. flow control); abort after retries */
			if (rc == BCME_OK) {
				if (commit_info.ac_fifo_credit_spent) {
					credit++;
				}
			}
			else {
				bus_retry_count++;
				if (bus_retry_count >= BUS_RETRIES) {
					DHD_ERROR(("dhd_wlfc_commit_packets(): bus error\n"));
					ctx->FIFO_credit[ac] -= credit;
					return rc;
				}
			}
		}

		ctx->FIFO_credit[ac] -= credit;

		/* packets from SENDQ are fresh and they'd need header and have no MAC entry */
		commit_info.needs_hdr = 1;
		commit_info.mac_entry = NULL;
		commit_info.pkt_type = eWLFC_PKTTYPE_NEW;

		for (credit = 0; credit < ctx->FIFO_credit[ac];) {
			commit_info.p = _dhd_wlfc_deque_sendq(ctx, ac,
			                &(commit_info.ac_fifo_credit_spent));
			if (commit_info.p == NULL)
				break;

			rc = _dhd_wlfc_handle_packet_commit(ctx, ac, &commit_info,
			     fcommit, commit_ctx);

			/* Bus commits may fail (e.g. flow control); abort after retries */
			if (rc == BCME_OK) {
				if (commit_info.ac_fifo_credit_spent) {
					credit++;
				}
			}
			else {
				bus_retry_count++;
				if (bus_retry_count >= BUS_RETRIES) {
					DHD_ERROR(("dhd_wlfc_commit_packets(): bus error\n"));
					ctx->FIFO_credit[ac] -= credit;
					return rc;
				}
			}
		}

		ctx->FIFO_credit[ac] -= credit;

		/* If no credits were used, the queue is idle and can be re-used
		   Note that resv credits cannot be borrowed
		   */
		if (initial_credit_count == ctx->FIFO_credit[ac]) {
			ac_available |= (1 << ac);
			credit_count += ctx->FIFO_credit[ac];
		}
	}

	/* We borrow only for AC_BE and only if no other traffic seen for DEFER_PERIOD

	   Note that (ac_available & WLFC_AC_BE_TRAFFIC_ONLY) is done to:
	   a) ignore BC/MC for deferring borrow
	   b) ignore AC_BE being available along with other ACs
		  (this should happen only for pure BC/MC traffic)

	   i.e. AC_VI, AC_VO, AC_BK all MUST be available (i.e. no traffic) and
	   we do not care if AC_BE and BC/MC are available or not
	   */
	if ((ac_available & WLFC_AC_BE_TRAFFIC_ONLY) == WLFC_AC_BE_TRAFFIC_ONLY) {

		if (ctx->allow_credit_borrow) {
			ac = 1;  /* Set ac to AC_BE and borrow credits */
		}
		else {
			int delta;
			int curr_t = OSL_SYSUPTIME();

			if (curr_t > ctx->borrow_defer_timestamp)
				delta = curr_t - ctx->borrow_defer_timestamp;
			else
				delta = 0xffffffff + curr_t - ctx->borrow_defer_timestamp;

			if (delta >= WLFC_BORROW_DEFER_PERIOD_MS) {
				/* Reset borrow but defer to next iteration (defensive borrowing) */
				ctx->allow_credit_borrow = TRUE;
				ctx->borrow_defer_timestamp = 0;
			}
			return BCME_OK;
		}
	}
	else {
		/* If we have multiple AC traffic, turn off borrowing, mark time and bail out */
		ctx->allow_credit_borrow = FALSE;
		ctx->borrow_defer_timestamp = OSL_SYSUPTIME();
		return BCME_OK;
	}

	/* At this point, borrow all credits only for "ac" (which should be set above to AC_BE)
	   Generically use "ac" only in case we extend to all ACs in future
	   */
	for (; (credit_count > 0);) {

		commit_info.p = _dhd_wlfc_deque_delayedq(ctx, ac,
		                &(commit_info.ac_fifo_credit_spent),
		                &(commit_info.needs_hdr),
		                &(commit_info.mac_entry));
		if (commit_info.p == NULL)
			break;

		commit_info.pkt_type = (commit_info.needs_hdr) ? eWLFC_PKTTYPE_DELAYED :
			eWLFC_PKTTYPE_SUPPRESSED;

		rc = _dhd_wlfc_handle_packet_commit(ctx, ac, &commit_info,
		     fcommit, commit_ctx);

		/* Bus commits may fail (e.g. flow control); abort after retries */
		if (rc == BCME_OK) {
			if (commit_info.ac_fifo_credit_spent) {
				(void) _dhd_wlfc_borrow_credit(ctx, ac_available, ac);
				credit_count--;
			}
		}
		else {
			bus_retry_count++;
			if (bus_retry_count >= BUS_RETRIES) {
				DHD_ERROR(("dhd_wlfc_commit_packets(): bus error\n"));
				return rc;
			}
		}
	}

	/* packets from SENDQ are fresh and they'd need header and have no MAC entry */
	commit_info.needs_hdr = 1;
	commit_info.mac_entry = NULL;
	commit_info.pkt_type = eWLFC_PKTTYPE_NEW;

	for (; (credit_count > 0);) {

		commit_info.p = _dhd_wlfc_deque_sendq(ctx, ac,
		                &(commit_info.ac_fifo_credit_spent));
		if (commit_info.p == NULL)
			break;

		rc = _dhd_wlfc_handle_packet_commit(ctx, ac, &commit_info,
		     fcommit, commit_ctx);

		/* Bus commits may fail (e.g. flow control); abort after retries */
		if (rc == BCME_OK) {
			if (commit_info.ac_fifo_credit_spent) {
				(void) _dhd_wlfc_borrow_credit(ctx, ac_available, ac);
				credit_count--;
			}
		}
		else {
			bus_retry_count++;
			if (bus_retry_count >= BUS_RETRIES) {
				DHD_ERROR(("dhd_wlfc_commit_packets(): bus error\n"));
				return rc;
			}
		}
	}

	return BCME_OK;
}

static uint8
dhd_wlfc_find_mac_desc_id_from_mac(dhd_pub_t *dhdp, uint8* ea)
{
	wlfc_mac_descriptor_t* table =
		((athost_wl_status_info_t*)dhdp->wlfc_state)->destination_entries.nodes;
	uint8 table_index;

	if (ea != NULL) {
		for (table_index = 0; table_index < WLFC_MAC_DESC_TABLE_SIZE; table_index++) {
			if ((memcmp(ea, &table[table_index].ea[0], ETHER_ADDR_LEN) == 0) &&
				table[table_index].occupied)
				return table_index;
		}
	}
	return WLFC_MAC_DESC_ID_INVALID;
}

void
dhd_wlfc_txcomplete(dhd_pub_t *dhd, void *txp, bool success)
{
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)
		dhd->wlfc_state;
	void* p;
	int fifo_id;

	if (DHD_PKTTAG_SIGNALONLY(PKTTAG(txp))) {
#ifdef PROP_TXSTATUS_DEBUG
		wlfc->stats.signal_only_pkts_freed++;
#endif
		/* is this a signal-only packet? */
		PKTFREE(wlfc->osh, txp, TRUE);
		return;
	}
	if (!success) {
		WLFC_DBGMESG(("At: %s():%d, bus_complete() failure for %p, htod_tag:0x%08x\n",
			__FUNCTION__, __LINE__, txp, DHD_PKTTAG_H2DTAG(PKTTAG(txp))));
		dhd_wlfc_hanger_poppkt(wlfc->hanger, WLFC_PKTID_HSLOT_GET(DHD_PKTTAG_H2DTAG
			(PKTTAG(txp))), &p, 1);

		/* indicate failure and free the packet */
		dhd_txcomplete(dhd, txp, FALSE);

		/* return the credit, if necessary */
		if (DHD_PKTTAG_CREDITCHECK(PKTTAG(txp))) {
			int lender, credit_returned = 0; /* Note that borrower is fifo_id */

			fifo_id = DHD_PKTTAG_FIFO(PKTTAG(txp));

			/* Return credits to highest priority lender first */
			for (lender = AC_COUNT; lender >= 0; lender--) {
				if (wlfc->credits_borrowed[fifo_id][lender] > 0) {
					wlfc->FIFO_credit[lender]++;
					wlfc->credits_borrowed[fifo_id][lender]--;
					credit_returned = 1;
					break;
				}
			}

			if (!credit_returned) {
				wlfc->FIFO_credit[fifo_id]++;
			}
		}

		PKTFREE(wlfc->osh, txp, TRUE);
	}
	return;
}

/* Handle discard or suppress indication */
static int
dhd_wlfc_txstatus_update(dhd_pub_t *dhd, uint8* pkt_info)
{
	uint8 	status_flag;
	uint32	status;
	int		ret;
	int		remove_from_hanger = 1;
	void*	pktbuf;
	uint8	fifo_id;
	wlfc_mac_descriptor_t* entry = NULL;
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)
		dhd->wlfc_state;

	memcpy(&status, pkt_info, sizeof(uint32));
	status_flag = WL_TXSTATUS_GET_FLAGS(status);
	wlfc->stats.txstatus_in++;

	if (status_flag == WLFC_CTL_PKTFLAG_DISCARD) {
		wlfc->stats.pkt_freed++;
	}

	else if (status_flag == WLFC_CTL_PKTFLAG_D11SUPPRESS) {
		wlfc->stats.d11_suppress++;
		remove_from_hanger = 0;
	}

	else if (status_flag == WLFC_CTL_PKTFLAG_WLSUPPRESS) {
		wlfc->stats.wl_suppress++;
		remove_from_hanger = 0;
	}

	else if (status_flag == WLFC_CTL_PKTFLAG_TOSSED_BYWLC) {
		wlfc->stats.wlc_tossed_pkts++;
	}

	ret = dhd_wlfc_hanger_poppkt(wlfc->hanger,
		WLFC_PKTID_HSLOT_GET(status), &pktbuf, remove_from_hanger);
	if (ret != BCME_OK) {
		/* do something */
		return ret;
	}

	if (!remove_from_hanger) {
		/* this packet was suppressed */

		entry = _dhd_wlfc_find_table_entry(wlfc, pktbuf);
		entry->generation = WLFC_PKTID_GEN(status);
	}

#ifdef PROP_TXSTATUS_DEBUG
	{
		uint32 new_t = OSL_SYSUPTIME();
		uint32 old_t;
		uint32 delta;
		old_t = ((wlfc_hanger_t*)(wlfc->hanger))->items[
			WLFC_PKTID_HSLOT_GET(status)].push_time;


		wlfc->stats.latency_sample_count++;
		if (new_t > old_t)
			delta = new_t - old_t;
		else
			delta = 0xffffffff + new_t - old_t;
		wlfc->stats.total_status_latency += delta;
		wlfc->stats.latency_most_recent = delta;

		wlfc->stats.deltas[wlfc->stats.idx_delta++] = delta;
		if (wlfc->stats.idx_delta == sizeof(wlfc->stats.deltas)/sizeof(uint32))
			wlfc->stats.idx_delta = 0;
	}
#endif /* PROP_TXSTATUS_DEBUG */

	fifo_id = DHD_PKTTAG_FIFO(PKTTAG(pktbuf));

	/* pick up the implicit credit from this packet */
	if (DHD_PKTTAG_CREDITCHECK(PKTTAG(pktbuf))) {
		if (wlfc->proptxstatus_mode == WLFC_FCMODE_IMPLIED_CREDIT) {

			int lender, credit_returned = 0; /* Note that borrower is fifo_id */

			/* Return credits to highest priority lender first */
			for (lender = AC_COUNT; lender >= 0; lender--)	{
				if (wlfc->credits_borrowed[fifo_id][lender] > 0) {
					wlfc->FIFO_credit[lender]++;
					wlfc->credits_borrowed[fifo_id][lender]--;
					credit_returned = 1;
					break;
				}
			}

			if (!credit_returned) {
				wlfc->FIFO_credit[fifo_id]++;
			}
		}
	}
	else {
		/*
		if this packet did not count against FIFO credit, it must have
		taken a requested_credit from the destination entry (for pspoll etc.)
		*/
		if (!entry) {

			entry = _dhd_wlfc_find_table_entry(wlfc, pktbuf);
		}
		if (!DHD_PKTTAG_ONETIMEPKTRQST(PKTTAG(pktbuf)))
			entry->requested_credit++;
#ifdef PROP_TXSTATUS_DEBUG
		entry->dstncredit_acks++;
#endif
	}
	if ((status_flag == WLFC_CTL_PKTFLAG_D11SUPPRESS) ||
		(status_flag == WLFC_CTL_PKTFLAG_WLSUPPRESS)) {
		ret = _dhd_wlfc_enque_suppressed(wlfc, fifo_id, pktbuf);
		if (ret != BCME_OK) {
			/* delay q is full, drop this packet */
			dhd_wlfc_hanger_poppkt(wlfc->hanger, WLFC_PKTID_HSLOT_GET(status),
			&pktbuf, 1);

			/* indicate failure and free the packet */
			dhd_txcomplete(dhd, pktbuf, FALSE);
			PKTFREE(wlfc->osh, pktbuf, TRUE);
		}
	}
	else {
		dhd_txcomplete(dhd, pktbuf, TRUE);
		/* free the packet */
		PKTFREE(wlfc->osh, pktbuf, TRUE);
	}
	return BCME_OK;
}

static int
dhd_wlfc_fifocreditback_indicate(dhd_pub_t *dhd, uint8* credits)
{
	int i;
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)
		dhd->wlfc_state;
	for (i = 0; i < WLFC_CTL_VALUE_LEN_FIFO_CREDITBACK; i++) {
#ifdef PROP_TXSTATUS_DEBUG
		wlfc->stats.fifo_credits_back[i] += credits[i];
#endif
		/* update FIFO credits */
		if (wlfc->proptxstatus_mode == WLFC_FCMODE_EXPLICIT_CREDIT)
		{
			int lender; /* Note that borrower is i */

			/* Return credits to highest priority lender first */
			for (lender = AC_COUNT; (lender >= 0) && (credits[i] > 0); lender--) {
				if (wlfc->credits_borrowed[i][lender] > 0) {
					if (credits[i] >= wlfc->credits_borrowed[i][lender]) {
						credits[i] -= wlfc->credits_borrowed[i][lender];
						wlfc->FIFO_credit[lender] +=
						    wlfc->credits_borrowed[i][lender];
						wlfc->credits_borrowed[i][lender] = 0;
					}
					else {
						wlfc->credits_borrowed[i][lender] -= credits[i];
						wlfc->FIFO_credit[lender] += credits[i];
						credits[i] = 0;
					}
				}
			}

			/* If we have more credits left over, these must belong to the AC */
			if (credits[i] > 0) {
				wlfc->FIFO_credit[i] += credits[i];
			}
		}
	}

	return BCME_OK;
}

static int
dhd_wlfc_rssi_indicate(dhd_pub_t *dhd, uint8* rssi)
{
	(void)dhd;
	(void)rssi;
	return BCME_OK;
}

static int
dhd_wlfc_mac_table_update(dhd_pub_t *dhd, uint8* value, uint8 type)
{
	int rc;
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)
		dhd->wlfc_state;
	wlfc_mac_descriptor_t* table;
	uint8 existing_index;
	uint8 table_index;
	uint8 ifid;
	uint8* ea;

	WLFC_DBGMESG(("%s(), mac [%02x:%02x:%02x:%02x:%02x:%02x],%s,idx:%d,id:0x%02x\n",
		__FUNCTION__, value[2], value[3], value[4], value[5], value[6], value[7],
		((type == WLFC_CTL_TYPE_MACDESC_ADD) ? "ADD":"DEL"),
		WLFC_MAC_DESC_GET_LOOKUP_INDEX(value[0]), value[0]));

	table = wlfc->destination_entries.nodes;
	table_index = WLFC_MAC_DESC_GET_LOOKUP_INDEX(value[0]);
	ifid = value[1];
	ea = &value[2];

	if (type == WLFC_CTL_TYPE_MACDESC_ADD) {
		existing_index = dhd_wlfc_find_mac_desc_id_from_mac(dhd, &value[2]);
		if (existing_index == WLFC_MAC_DESC_ID_INVALID) {
			/* this MAC entry does not exist, create one */
			if (!table[table_index].occupied) {
				table[table_index].mac_handle = value[0];
				rc = _dhd_wlfc_mac_entry_update(wlfc, &table[table_index],
				eWLFC_MAC_ENTRY_ACTION_ADD, ifid,
				wlfc->destination_entries.interfaces[ifid].iftype,
				ea);
			}
			else {
				/* the space should have been empty, but it's not */
				wlfc->stats.mac_update_failed++;
			}
		}
		else {
			/*
			there is an existing entry, move it to new index
			if necessary.
			*/
			if (existing_index != table_index) {
				/* if we already have an entry, free the old one */
				table[existing_index].occupied = 0;
				table[existing_index].state = WLFC_STATE_CLOSE;
				table[existing_index].requested_credit = 0;
				table[existing_index].interface_id = 0;
				/* enable after packets are queued-deqeued properly.
				pktq_flush(dhd->osh, &table[existing_index].psq, FALSE, NULL, 0);
				*/
			}
		}
	}
	if (type == WLFC_CTL_TYPE_MACDESC_DEL) {
		if (table[table_index].occupied) {
				rc = _dhd_wlfc_mac_entry_update(wlfc, &table[table_index],
					eWLFC_MAC_ENTRY_ACTION_DEL, ifid,
					wlfc->destination_entries.interfaces[ifid].iftype,
					ea);
		}
		else {
			/* the space should have been occupied, but it's not */
			wlfc->stats.mac_update_failed++;
		}
	}
	BCM_REFERENCE(rc);
	return BCME_OK;
}

static int
dhd_wlfc_psmode_update(dhd_pub_t *dhd, uint8* value, uint8 type)
{
	/* Handle PS on/off indication */
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)
		dhd->wlfc_state;
	wlfc_mac_descriptor_t* table;
	wlfc_mac_descriptor_t* desc;
	uint8 mac_handle = value[0];
	int i;

	table = wlfc->destination_entries.nodes;
	desc = &table[WLFC_MAC_DESC_GET_LOOKUP_INDEX(mac_handle)];
	if (desc->occupied) {
		/* a fresh PS mode should wipe old ps credits? */
		desc->requested_credit = 0;
		if (type == WLFC_CTL_TYPE_MAC_OPEN) {
			desc->state = WLFC_STATE_OPEN;
			DHD_WLFC_CTRINC_MAC_OPEN(desc);
		}
		else {
			desc->state = WLFC_STATE_CLOSE;
			DHD_WLFC_CTRINC_MAC_CLOSE(desc);
			/*
			Indicate to firmware if there is any traffic pending.
			*/
			for (i = AC_BE; i < AC_COUNT; i++) {
				_dhd_wlfc_traffic_pending_check(wlfc, desc, i);
			}
		}
	}
	else {
		wlfc->stats.psmode_update_failed++;
	}
	return BCME_OK;
}

static int
dhd_wlfc_interface_update(dhd_pub_t *dhd, uint8* value, uint8 type)
{
	/* Handle PS on/off indication */
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)
		dhd->wlfc_state;
	wlfc_mac_descriptor_t* table;
	uint8 if_id = value[0];

	if (if_id < WLFC_MAX_IFNUM) {
		table = wlfc->destination_entries.interfaces;
		if (table[if_id].occupied) {
			if (type == WLFC_CTL_TYPE_INTERFACE_OPEN) {
				table[if_id].state = WLFC_STATE_OPEN;
				/* WLFC_DBGMESG(("INTERFACE[%d] OPEN\n", if_id)); */
			}
			else {
				table[if_id].state = WLFC_STATE_CLOSE;
				/* WLFC_DBGMESG(("INTERFACE[%d] CLOSE\n", if_id)); */
			}
			return BCME_OK;
		}
	}
	wlfc->stats.interface_update_failed++;

	return BCME_OK;
}

static int
dhd_wlfc_credit_request(dhd_pub_t *dhd, uint8* value)
{
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)
		dhd->wlfc_state;
	wlfc_mac_descriptor_t* table;
	wlfc_mac_descriptor_t* desc;
	uint8 mac_handle;
	uint8 credit;

	table = wlfc->destination_entries.nodes;
	mac_handle = value[1];
	credit = value[0];

	desc = &table[WLFC_MAC_DESC_GET_LOOKUP_INDEX(mac_handle)];
	if (desc->occupied) {
		desc->requested_credit = credit;

		desc->ac_bitmap = value[2];
	}
	else {
		wlfc->stats.credit_request_failed++;
	}
	return BCME_OK;
}

static int
dhd_wlfc_packet_request(dhd_pub_t *dhd, uint8* value)
{
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)
		dhd->wlfc_state;
	wlfc_mac_descriptor_t* table;
	wlfc_mac_descriptor_t* desc;
	uint8 mac_handle;
	uint8 packet_count;

	table = wlfc->destination_entries.nodes;
	mac_handle = value[1];
	packet_count = value[0];

	desc = &table[WLFC_MAC_DESC_GET_LOOKUP_INDEX(mac_handle)];
	if (desc->occupied) {
		desc->requested_packet = packet_count;

		desc->ac_bitmap = value[2];
	}
	else {
		wlfc->stats.packet_request_failed++;
	}
	return BCME_OK;
}

static void
dhd_wlfc_reorderinfo_indicate(uint8 *val, uint8 len, uchar *info_buf, uint *info_len)
{
	if (info_len) {
		if (info_buf) {
			bcopy(val, info_buf, len);
			*info_len = len;
		}
		else
			*info_len = 0;
	}
}

static int
dhd_wlfc_parse_header_info(dhd_pub_t *dhd, void* pktbuf, int tlv_hdr_len, uchar *reorder_info_buf,
	uint *reorder_info_len)
{
	uint8 type, len;
	uint8* value;
	uint8* tmpbuf;
	uint16 remainder = tlv_hdr_len;
	uint16 processed = 0;
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)
		dhd->wlfc_state;
	tmpbuf = (uint8*)PKTDATA(dhd->osh, pktbuf);
	if (remainder) {
		while ((processed < (WLFC_MAX_PENDING_DATALEN * 2)) && (remainder > 0)) {
			type = tmpbuf[processed];
			if (type == WLFC_CTL_TYPE_FILLER) {
				remainder -= 1;
				processed += 1;
				continue;
			}

			len  = tmpbuf[processed + 1];
			value = &tmpbuf[processed + 2];

			if (remainder < (2 + len))
				break;

			remainder -= 2 + len;
			processed += 2 + len;
			if (type == WLFC_CTL_TYPE_TXSTATUS)
				dhd_wlfc_txstatus_update(dhd, value);

			else if (type == WLFC_CTL_TYPE_HOST_REORDER_RXPKTS)
				dhd_wlfc_reorderinfo_indicate(value, len, reorder_info_buf,
					reorder_info_len);
			else if (type == WLFC_CTL_TYPE_FIFO_CREDITBACK)
				dhd_wlfc_fifocreditback_indicate(dhd, value);

			else if (type == WLFC_CTL_TYPE_RSSI)
				dhd_wlfc_rssi_indicate(dhd, value);

			else if (type == WLFC_CTL_TYPE_MAC_REQUEST_CREDIT)
				dhd_wlfc_credit_request(dhd, value);

			else if (type == WLFC_CTL_TYPE_MAC_REQUEST_PACKET)
				dhd_wlfc_packet_request(dhd, value);

			else if ((type == WLFC_CTL_TYPE_MAC_OPEN) ||
				(type == WLFC_CTL_TYPE_MAC_CLOSE))
				dhd_wlfc_psmode_update(dhd, value, type);

			else if ((type == WLFC_CTL_TYPE_MACDESC_ADD) ||
				(type == WLFC_CTL_TYPE_MACDESC_DEL))
				dhd_wlfc_mac_table_update(dhd, value, type);

			else if ((type == WLFC_CTL_TYPE_INTERFACE_OPEN) ||
				(type == WLFC_CTL_TYPE_INTERFACE_CLOSE)) {
				dhd_wlfc_interface_update(dhd, value, type);
			}
		}
		if (remainder != 0) {
			/* trouble..., something is not right */
			wlfc->stats.tlv_parse_failed++;
		}
	}
	return BCME_OK;
}

int
dhd_wlfc_init(dhd_pub_t *dhd)
{
	char iovbuf[12]; /* Room for "tlv" + '\0' + parameter */
	/* enable all signals & indicate host proptxstatus logic is active */
	uint32 tlv = dhd->wlfc_enabled?
		WLFC_FLAGS_RSSI_SIGNALS |
		WLFC_FLAGS_XONXOFF_SIGNALS |
		WLFC_FLAGS_CREDIT_STATUS_SIGNALS |
		WLFC_FLAGS_HOST_RXRERODER_ACTIVE : 0;
		/* WLFC_FLAGS_HOST_PROPTXSTATUS_ACTIVE | WLFC_FLAGS_HOST_RXRERODER_ACTIVE : 0; */


	/*
	try to enable/disable signaling by sending "tlv" iovar. if that fails,
	fallback to no flow control? Print a message for now.
	*/

	/* enable proptxtstatus signaling by default */
	bcm_mkiovar("tlv", (char *)&tlv, 4, iovbuf, sizeof(iovbuf));
	if (dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0) < 0) {
		DHD_ERROR(("dhd_wlfc_init(): failed to enable/disable bdcv2 tlv signaling\n"));
	}
	else {
		/*
		Leaving the message for now, it should be removed after a while; once
		the tlv situation is stable.
		*/
		DHD_ERROR(("dhd_wlfc_init(): successfully %s bdcv2 tlv signaling, %d\n",
			dhd->wlfc_enabled?"enabled":"disabled", tlv));
	}
	return BCME_OK;
}

int
dhd_wlfc_enable(dhd_pub_t *dhd)
{
	int i;
	athost_wl_status_info_t* wlfc;

	if (!dhd->wlfc_enabled || dhd->wlfc_state)
		return BCME_OK;

	/* allocate space to track txstatus propagated from firmware */
	dhd->wlfc_state = MALLOC(dhd->osh, sizeof(athost_wl_status_info_t));
	if (dhd->wlfc_state == NULL)
		return BCME_NOMEM;

	/* initialize state space */
	wlfc = (athost_wl_status_info_t*)dhd->wlfc_state;
	memset(wlfc, 0, sizeof(athost_wl_status_info_t));

	/* remember osh & dhdp */
	wlfc->osh = dhd->osh;
	wlfc->dhdp = dhd;

	wlfc->hanger =
		dhd_wlfc_hanger_create(dhd->osh, WLFC_HANGER_MAXITEMS);
	if (wlfc->hanger == NULL) {
		MFREE(dhd->osh, dhd->wlfc_state, sizeof(athost_wl_status_info_t));
		dhd->wlfc_state = NULL;
		return BCME_NOMEM;
	}

	/* initialize all interfaces to accept traffic */
	for (i = 0; i < WLFC_MAX_IFNUM; i++) {
		wlfc->hostif_flow_state[i] = OFF;
	}

	/*
	create the SENDQ containing
	sub-queues for all AC precedences + 1 for bc/mc traffic
	*/
	pktq_init(&wlfc->SENDQ, (AC_COUNT + 1), WLFC_SENDQ_LEN);

	wlfc->destination_entries.other.state = WLFC_STATE_OPEN;
	/* bc/mc FIFO is always open [credit aside], i.e. b[5] */
	wlfc->destination_entries.other.ac_bitmap = 0x1f;
	wlfc->destination_entries.other.interface_id = 0;

	wlfc->proptxstatus_mode = WLFC_FCMODE_EXPLICIT_CREDIT;

	wlfc->allow_credit_borrow = TRUE;
	wlfc->borrow_defer_timestamp = 0;

	return BCME_OK;
}

/* release all packet resources */
void
dhd_wlfc_cleanup(dhd_pub_t *dhd)
{
	int i;
	int total_entries;
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)
		dhd->wlfc_state;
	wlfc_mac_descriptor_t* table;
	wlfc_hanger_t* h;

	if (dhd->wlfc_state == NULL)
		return;

	total_entries = sizeof(wlfc->destination_entries)/sizeof(wlfc_mac_descriptor_t);
	/* search all entries, include nodes as well as interfaces */
	table = (wlfc_mac_descriptor_t*)&wlfc->destination_entries;

	for (i = 0; i < total_entries; i++) {
		if (table[i].occupied) {
			if (table[i].psq.len) {
				WLFC_DBGMESG(("%s(): DELAYQ[%d].len = %d\n",
					__FUNCTION__, i, table[i].psq.len));
				/* release packets held in DELAYQ */
				pktq_flush(wlfc->osh, &table[i].psq, TRUE, NULL, 0);
			}
			table[i].occupied = 0;
		}
	}
	/* release packets held in SENDQ */
	if (wlfc->SENDQ.len)
		pktq_flush(wlfc->osh, &wlfc->SENDQ, TRUE, NULL, 0);
	/* any in the hanger? */
	h = (wlfc_hanger_t*)wlfc->hanger;
	for (i = 0; i < h->max_items; i++) {
		if (h->items[i].state == WLFC_HANGER_ITEM_STATE_INUSE) {
			PKTFREE(wlfc->osh, h->items[i].pkt, TRUE);
		}
	}
	return;
}

void
dhd_wlfc_deinit(dhd_pub_t *dhd)
{
	/* cleanup all psq related resources */
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)
		dhd->wlfc_state;

	if (dhd->wlfc_state == NULL)
		return;

#ifdef PROP_TXSTATUS_DEBUG
	{
		int i;
		wlfc_hanger_t* h = (wlfc_hanger_t*)wlfc->hanger;
		for (i = 0; i < h->max_items; i++) {
			if (h->items[i].state == WLFC_HANGER_ITEM_STATE_INUSE) {
				WLFC_DBGMESG(("%s() pkt[%d] = 0x%p, FIFO_credit_used:%d\n",
					__FUNCTION__, i, h->items[i].pkt,
					DHD_PKTTAG_CREDITCHECK(PKTTAG(h->items[i].pkt))));
			}
		}
	}
#endif
	/* delete hanger */
	dhd_wlfc_hanger_delete(dhd->osh, wlfc->hanger);

	/* free top structure */
	MFREE(dhd->osh, dhd->wlfc_state, sizeof(athost_wl_status_info_t));
	dhd->wlfc_state = NULL;
	return;
}
#endif /* PROP_TXSTATUS */

void
dhd_prot_dump(dhd_pub_t *dhdp, struct bcmstrbuf *strbuf)
{
	bcm_bprintf(strbuf, "Protocol CDC: reqid %d\n", dhdp->prot->reqid);
#ifdef PROP_TXSTATUS
	if (dhdp->wlfc_state)
		dhd_wlfc_dump(dhdp, strbuf);
#endif
}

void
dhd_prot_hdrpush(dhd_pub_t *dhd, int ifidx, void *pktbuf)
{
#ifdef BDC
	struct bdc_header *h;
#endif /* BDC */

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

#ifdef BDC
	/* Push BDC header used to convey priority for buses that don't */

	PKTPUSH(dhd->osh, pktbuf, BDC_HEADER_LEN);

	h = (struct bdc_header *)PKTDATA(dhd->osh, pktbuf);

	h->flags = (BDC_PROTO_VER << BDC_FLAG_VER_SHIFT);
	if (PKTSUMNEEDED(pktbuf))
		h->flags |= BDC_FLAG_SUM_NEEDED;


	h->priority = (PKTPRIO(pktbuf) & BDC_PRIORITY_MASK);
	h->flags2 = 0;
	h->dataOffset = 0;
#endif /* BDC */
	BDC_SET_IF_IDX(h, ifidx);
}

int
dhd_prot_hdrpull(dhd_pub_t *dhd, int *ifidx, void *pktbuf, uchar *reorder_buf_info,
	uint *reorder_info_len)
{
#ifdef BDC
	struct bdc_header *h;
#endif

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

	if ((*ifidx = BDC_GET_IF_IDX(h)) >= DHD_MAX_IFS) {
		DHD_ERROR(("%s: rx data ifnum out of range (%d)\n",
		           __FUNCTION__, *ifidx));
		return BCME_ERROR;
	}

#if defined(NDIS630)
	h->dataOffset = 0;
#endif
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
	PKTPULL(dhd->osh, pktbuf, BDC_HEADER_LEN);
#endif /* BDC */

#if !defined(NDIS630)
	if (PKTLEN(dhd->osh, pktbuf) < (uint32) (h->dataOffset << 2)) {
		DHD_ERROR(("%s: rx data too short (%d < %d)\n", __FUNCTION__,
		           PKTLEN(dhd->osh, pktbuf), (h->dataOffset * 4)));
		return BCME_ERROR;
	}
#endif
#ifdef PROP_TXSTATUS
	if (dhd->wlfc_state &&
		((athost_wl_status_info_t*)dhd->wlfc_state)->proptxstatus_mode
		!= WLFC_FCMODE_NONE &&
		(!DHD_PKTTAG_PKTDIR(PKTTAG(pktbuf)))) {
		/*
		- parse txstatus only for packets that came from the firmware
		*/
		dhd_os_wlfc_block(dhd);
		dhd_wlfc_parse_header_info(dhd, pktbuf, (h->dataOffset << 2),
			reorder_buf_info, reorder_info_len);
		((athost_wl_status_info_t*)dhd->wlfc_state)->stats.dhd_hdrpulls++;
		dhd_wlfc_commit_packets(dhd->wlfc_state, (f_commitpkt_t)dhd_bus_txdata,
			(void *)dhd->bus);
		dhd_os_wlfc_unblock(dhd);
	}
#endif /* PROP_TXSTATUS */
#if !defined(NDIS630)
		PKTPULL(dhd->osh, pktbuf, (h->dataOffset << 2));
#endif
	return 0;
}

int
dhd_prot_attach(dhd_pub_t *dhd)
{
	dhd_prot_t *cdc;

	if (!(cdc = (dhd_prot_t *)DHD_OS_PREALLOC(dhd->osh, DHD_PREALLOC_PROT,
		sizeof(dhd_prot_t)))) {
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
#ifndef CONFIG_DHD_USE_STATIC_BUF
	if (cdc != NULL)
		MFREE(dhd->osh, cdc, sizeof(dhd_prot_t));
#endif /* CONFIG_DHD_USE_STATIC_BUF */
	return BCME_NOMEM;
}

/* ~NOTE~ What if another thread is waiting on the semaphore?  Holding it? */
void
dhd_prot_detach(dhd_pub_t *dhd)
{
#ifdef PROP_TXSTATUS
	dhd_wlfc_deinit(dhd);
#endif
#ifndef CONFIG_DHD_USE_STATIC_BUF
	MFREE(dhd->osh, dhd->prot, sizeof(dhd_prot_t));
#endif /* CONFIG_DHD_USE_STATIC_BUF */
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


#ifdef PROP_TXSTATUS
	ret = dhd_wlfc_init(dhd);
#endif

#if defined(WL_CFG80211)
	if (dhd_download_fw_on_driverload)
#endif /* defined(WL_CFG80211) */
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
	uint i;
	void *plast = NULL, *p;
	uint32 pkt_cnt = 0;

	if (ptr->pend_pkts == 0) {
		DHD_REORDER(("%s: no packets in reorder queue \n", __FUNCTION__));
		*pplast = NULL;
		*pkt_count = 0;
		*pkt = NULL;
		return;
	}
	if (start == end)
		i = ptr->max_idx + 1;
	else {
		if (start > end)
			i = (ptr->max_idx - end) + start;
		else
			i = end - start;
	}
	while (i) {
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
		i--;
		if (start++ == ptr->max_idx)
			start = 0;
	}
	*pplast = plast;
	*pkt_count = (uint32)pkt_cnt;
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
	cur_pkt = *pkt;
	*pkt = NULL;

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

	ptr = dhd->reorder_bufs[flow_id];
	if (flags & WLHOST_REORDERDATA_DEL_FLOW) {
		uint32 buf_size = sizeof(struct reorder_info);

		DHD_REORDER(("%s: Flags indicating to delete a flow id %d\n",
			__FUNCTION__, flow_id));

		if (ptr == NULL) {
			DHD_ERROR(("%s: received flags to cleanup, but no flow (%d) yet\n",
				__FUNCTION__, flow_id));
			*pkt_count = 1;
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
			ptr->pend_pkts -= (uint8)cnt;
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
		ptr->pend_pkts -= (uint8)cnt;
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
