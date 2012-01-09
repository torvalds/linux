/*
 * Copyright(c) 2007 - 2008 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained at www.Open-FCoE.org
 */

/*
 * Target Discovery
 *
 * This block discovers all FC-4 remote ports, including FCP initiators. It
 * also handles RSCN events and re-discovery if necessary.
 */

/*
 * DISC LOCKING
 *
 * The disc mutex is can be locked when acquiring rport locks, but may not
 * be held when acquiring the lport lock. Refer to fc_lport.c for more
 * details.
 */

#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/export.h>
#include <asm/unaligned.h>

#include <scsi/fc/fc_gs.h>

#include <scsi/libfc.h>

#include "fc_libfc.h"

#define FC_DISC_RETRY_LIMIT	3	/* max retries */
#define FC_DISC_RETRY_DELAY	500UL	/* (msecs) delay */

static void fc_disc_gpn_ft_req(struct fc_disc *);
static void fc_disc_gpn_ft_resp(struct fc_seq *, struct fc_frame *, void *);
static void fc_disc_done(struct fc_disc *, enum fc_disc_event);
static void fc_disc_timeout(struct work_struct *);
static int fc_disc_single(struct fc_lport *, struct fc_disc_port *);
static void fc_disc_restart(struct fc_disc *);

/**
 * fc_disc_stop_rports() - Delete all the remote ports associated with the lport
 * @disc: The discovery job to stop remote ports on
 *
 * Locking Note: This function expects that the lport mutex is locked before
 * calling it.
 */
void fc_disc_stop_rports(struct fc_disc *disc)
{
	struct fc_lport *lport;
	struct fc_rport_priv *rdata;

	lport = fc_disc_lport(disc);

	mutex_lock(&disc->disc_mutex);
	list_for_each_entry_rcu(rdata, &disc->rports, peers)
		lport->tt.rport_logoff(rdata);
	mutex_unlock(&disc->disc_mutex);
}

/**
 * fc_disc_recv_rscn_req() - Handle Registered State Change Notification (RSCN)
 * @disc:  The discovery object to which the RSCN applies
 * @fp:	   The RSCN frame
 *
 * Locking Note: This function expects that the disc_mutex is locked
 *		 before it is called.
 */
static void fc_disc_recv_rscn_req(struct fc_disc *disc, struct fc_frame *fp)
{
	struct fc_lport *lport;
	struct fc_els_rscn *rp;
	struct fc_els_rscn_page *pp;
	struct fc_seq_els_data rjt_data;
	unsigned int len;
	int redisc = 0;
	enum fc_els_rscn_ev_qual ev_qual;
	enum fc_els_rscn_addr_fmt fmt;
	LIST_HEAD(disc_ports);
	struct fc_disc_port *dp, *next;

	lport = fc_disc_lport(disc);

	FC_DISC_DBG(disc, "Received an RSCN event\n");

	/* make sure the frame contains an RSCN message */
	rp = fc_frame_payload_get(fp, sizeof(*rp));
	if (!rp)
		goto reject;
	/* make sure the page length is as expected (4 bytes) */
	if (rp->rscn_page_len != sizeof(*pp))
		goto reject;
	/* get the RSCN payload length */
	len = ntohs(rp->rscn_plen);
	if (len < sizeof(*rp))
		goto reject;
	/* make sure the frame contains the expected payload */
	rp = fc_frame_payload_get(fp, len);
	if (!rp)
		goto reject;
	/* payload must be a multiple of the RSCN page size */
	len -= sizeof(*rp);
	if (len % sizeof(*pp))
		goto reject;

	for (pp = (void *)(rp + 1); len > 0; len -= sizeof(*pp), pp++) {
		ev_qual = pp->rscn_page_flags >> ELS_RSCN_EV_QUAL_BIT;
		ev_qual &= ELS_RSCN_EV_QUAL_MASK;
		fmt = pp->rscn_page_flags >> ELS_RSCN_ADDR_FMT_BIT;
		fmt &= ELS_RSCN_ADDR_FMT_MASK;
		/*
		 * if we get an address format other than port
		 * (area, domain, fabric), then do a full discovery
		 */
		switch (fmt) {
		case ELS_ADDR_FMT_PORT:
			FC_DISC_DBG(disc, "Port address format for port "
				    "(%6.6x)\n", ntoh24(pp->rscn_fid));
			dp = kzalloc(sizeof(*dp), GFP_KERNEL);
			if (!dp) {
				redisc = 1;
				break;
			}
			dp->lp = lport;
			dp->port_id = ntoh24(pp->rscn_fid);
			list_add_tail(&dp->peers, &disc_ports);
			break;
		case ELS_ADDR_FMT_AREA:
		case ELS_ADDR_FMT_DOM:
		case ELS_ADDR_FMT_FAB:
		default:
			FC_DISC_DBG(disc, "Address format is (%d)\n", fmt);
			redisc = 1;
			break;
		}
	}
	lport->tt.seq_els_rsp_send(fp, ELS_LS_ACC, NULL);

	/*
	 * If not doing a complete rediscovery, do GPN_ID on
	 * the individual ports mentioned in the list.
	 * If any of these get an error, do a full rediscovery.
	 * In any case, go through the list and free the entries.
	 */
	list_for_each_entry_safe(dp, next, &disc_ports, peers) {
		list_del(&dp->peers);
		if (!redisc)
			redisc = fc_disc_single(lport, dp);
		kfree(dp);
	}
	if (redisc) {
		FC_DISC_DBG(disc, "RSCN received: rediscovering\n");
		fc_disc_restart(disc);
	} else {
		FC_DISC_DBG(disc, "RSCN received: not rediscovering. "
			    "redisc %d state %d in_prog %d\n",
			    redisc, lport->state, disc->pending);
	}
	fc_frame_free(fp);
	return;
reject:
	FC_DISC_DBG(disc, "Received a bad RSCN frame\n");
	rjt_data.reason = ELS_RJT_LOGIC;
	rjt_data.explan = ELS_EXPL_NONE;
	lport->tt.seq_els_rsp_send(fp, ELS_LS_RJT, &rjt_data);
	fc_frame_free(fp);
}

/**
 * fc_disc_recv_req() - Handle incoming requests
 * @lport: The local port receiving the request
 * @fp:	   The request frame
 *
 * Locking Note: This function is called from the EM and will lock
 *		 the disc_mutex before calling the handler for the
 *		 request.
 */
static void fc_disc_recv_req(struct fc_lport *lport, struct fc_frame *fp)
{
	u8 op;
	struct fc_disc *disc = &lport->disc;

	op = fc_frame_payload_op(fp);
	switch (op) {
	case ELS_RSCN:
		mutex_lock(&disc->disc_mutex);
		fc_disc_recv_rscn_req(disc, fp);
		mutex_unlock(&disc->disc_mutex);
		break;
	default:
		FC_DISC_DBG(disc, "Received an unsupported request, "
			    "the opcode is (%x)\n", op);
		fc_frame_free(fp);
		break;
	}
}

/**
 * fc_disc_restart() - Restart discovery
 * @disc: The discovery object to be restarted
 *
 * Locking Note: This function expects that the disc mutex
 *		 is already locked.
 */
static void fc_disc_restart(struct fc_disc *disc)
{
	if (!disc->disc_callback)
		return;

	FC_DISC_DBG(disc, "Restarting discovery\n");

	disc->requested = 1;
	if (disc->pending)
		return;

	/*
	 * Advance disc_id.  This is an arbitrary non-zero number that will
	 * match the value in the fc_rport_priv after discovery for all
	 * freshly-discovered remote ports.  Avoid wrapping to zero.
	 */
	disc->disc_id = (disc->disc_id + 2) | 1;
	disc->retry_count = 0;
	fc_disc_gpn_ft_req(disc);
}

/**
 * fc_disc_start() - Start discovery on a local port
 * @lport:	   The local port to have discovery started on
 * @disc_callback: Callback function to be called when discovery is complete
 */
static void fc_disc_start(void (*disc_callback)(struct fc_lport *,
						enum fc_disc_event),
			  struct fc_lport *lport)
{
	struct fc_disc *disc = &lport->disc;

	/*
	 * At this point we may have a new disc job or an existing
	 * one. Either way, let's lock when we make changes to it
	 * and send the GPN_FT request.
	 */
	mutex_lock(&disc->disc_mutex);
	disc->disc_callback = disc_callback;
	fc_disc_restart(disc);
	mutex_unlock(&disc->disc_mutex);
}

/**
 * fc_disc_done() - Discovery has been completed
 * @disc:  The discovery context
 * @event: The discovery completion status
 *
 * Locking Note: This function expects that the disc mutex is locked before
 * it is called. The discovery callback is then made with the lock released,
 * and the lock is re-taken before returning from this function
 */
static void fc_disc_done(struct fc_disc *disc, enum fc_disc_event event)
{
	struct fc_lport *lport = fc_disc_lport(disc);
	struct fc_rport_priv *rdata;

	FC_DISC_DBG(disc, "Discovery complete\n");

	disc->pending = 0;
	if (disc->requested) {
		fc_disc_restart(disc);
		return;
	}

	/*
	 * Go through all remote ports.	 If they were found in the latest
	 * discovery, reverify or log them in.	Otherwise, log them out.
	 * Skip ports which were never discovered.  These are the dNS port
	 * and ports which were created by PLOGI.
	 */
	list_for_each_entry_rcu(rdata, &disc->rports, peers) {
		if (!rdata->disc_id)
			continue;
		if (rdata->disc_id == disc->disc_id)
			lport->tt.rport_login(rdata);
		else
			lport->tt.rport_logoff(rdata);
	}

	mutex_unlock(&disc->disc_mutex);
	disc->disc_callback(lport, event);
	mutex_lock(&disc->disc_mutex);
}

/**
 * fc_disc_error() - Handle error on dNS request
 * @disc: The discovery context
 * @fp:	  The error code encoded as a frame pointer
 */
static void fc_disc_error(struct fc_disc *disc, struct fc_frame *fp)
{
	struct fc_lport *lport = fc_disc_lport(disc);
	unsigned long delay = 0;

	FC_DISC_DBG(disc, "Error %ld, retries %d/%d\n",
		    PTR_ERR(fp), disc->retry_count,
		    FC_DISC_RETRY_LIMIT);

	if (!fp || PTR_ERR(fp) == -FC_EX_TIMEOUT) {
		/*
		 * Memory allocation failure, or the exchange timed out,
		 * retry after delay.
		 */
		if (disc->retry_count < FC_DISC_RETRY_LIMIT) {
			/* go ahead and retry */
			if (!fp)
				delay = msecs_to_jiffies(FC_DISC_RETRY_DELAY);
			else {
				delay = msecs_to_jiffies(lport->e_d_tov);

				/* timeout faster first time */
				if (!disc->retry_count)
					delay /= 4;
			}
			disc->retry_count++;
			schedule_delayed_work(&disc->disc_work, delay);
		} else
			fc_disc_done(disc, DISC_EV_FAILED);
	}
}

/**
 * fc_disc_gpn_ft_req() - Send Get Port Names by FC-4 type (GPN_FT) request
 * @lport: The discovery context
 *
 * Locking Note: This function expects that the disc_mutex is locked
 *		 before it is called.
 */
static void fc_disc_gpn_ft_req(struct fc_disc *disc)
{
	struct fc_frame *fp;
	struct fc_lport *lport = fc_disc_lport(disc);

	WARN_ON(!fc_lport_test_ready(lport));

	disc->pending = 1;
	disc->requested = 0;

	disc->buf_len = 0;
	disc->seq_count = 0;
	fp = fc_frame_alloc(lport,
			    sizeof(struct fc_ct_hdr) +
			    sizeof(struct fc_ns_gid_ft));
	if (!fp)
		goto err;

	if (lport->tt.elsct_send(lport, 0, fp,
				 FC_NS_GPN_FT,
				 fc_disc_gpn_ft_resp,
				 disc, 3 * lport->r_a_tov))
		return;
err:
	fc_disc_error(disc, NULL);
}

/**
 * fc_disc_gpn_ft_parse() - Parse the body of the dNS GPN_FT response.
 * @lport: The local port the GPN_FT was received on
 * @buf:   The GPN_FT response buffer
 * @len:   The size of response buffer
 *
 * Goes through the list of IDs and names resulting from a request.
 */
static int fc_disc_gpn_ft_parse(struct fc_disc *disc, void *buf, size_t len)
{
	struct fc_lport *lport;
	struct fc_gpn_ft_resp *np;
	char *bp;
	size_t plen;
	size_t tlen;
	int error = 0;
	struct fc_rport_identifiers ids;
	struct fc_rport_priv *rdata;

	lport = fc_disc_lport(disc);
	disc->seq_count++;

	/*
	 * Handle partial name record left over from previous call.
	 */
	bp = buf;
	plen = len;
	np = (struct fc_gpn_ft_resp *)bp;
	tlen = disc->buf_len;
	disc->buf_len = 0;
	if (tlen) {
		WARN_ON(tlen >= sizeof(*np));
		plen = sizeof(*np) - tlen;
		WARN_ON(plen <= 0);
		WARN_ON(plen >= sizeof(*np));
		if (plen > len)
			plen = len;
		np = &disc->partial_buf;
		memcpy((char *)np + tlen, bp, plen);

		/*
		 * Set bp so that the loop below will advance it to the
		 * first valid full name element.
		 */
		bp -= tlen;
		len += tlen;
		plen += tlen;
		disc->buf_len = (unsigned char) plen;
		if (plen == sizeof(*np))
			disc->buf_len = 0;
	}

	/*
	 * Handle full name records, including the one filled from above.
	 * Normally, np == bp and plen == len, but from the partial case above,
	 * bp, len describe the overall buffer, and np, plen describe the
	 * partial buffer, which if would usually be full now.
	 * After the first time through the loop, things return to "normal".
	 */
	while (plen >= sizeof(*np)) {
		ids.port_id = ntoh24(np->fp_fid);
		ids.port_name = ntohll(np->fp_wwpn);

		if (ids.port_id != lport->port_id &&
		    ids.port_name != lport->wwpn) {
			rdata = lport->tt.rport_create(lport, ids.port_id);
			if (rdata) {
				rdata->ids.port_name = ids.port_name;
				rdata->disc_id = disc->disc_id;
			} else {
				printk(KERN_WARNING "libfc: Failed to allocate "
				       "memory for the newly discovered port "
				       "(%6.6x)\n", ids.port_id);
				error = -ENOMEM;
			}
		}

		if (np->fp_flags & FC_NS_FID_LAST) {
			fc_disc_done(disc, DISC_EV_SUCCESS);
			len = 0;
			break;
		}
		len -= sizeof(*np);
		bp += sizeof(*np);
		np = (struct fc_gpn_ft_resp *)bp;
		plen = len;
	}

	/*
	 * Save any partial record at the end of the buffer for next time.
	 */
	if (error == 0 && len > 0 && len < sizeof(*np)) {
		if (np != &disc->partial_buf) {
			FC_DISC_DBG(disc, "Partial buffer remains "
				    "for discovery\n");
			memcpy(&disc->partial_buf, np, len);
		}
		disc->buf_len = (unsigned char) len;
	}
	return error;
}

/**
 * fc_disc_timeout() - Handler for discovery timeouts
 * @work: Structure holding discovery context that needs to retry discovery
 */
static void fc_disc_timeout(struct work_struct *work)
{
	struct fc_disc *disc = container_of(work,
					    struct fc_disc,
					    disc_work.work);
	mutex_lock(&disc->disc_mutex);
	fc_disc_gpn_ft_req(disc);
	mutex_unlock(&disc->disc_mutex);
}

/**
 * fc_disc_gpn_ft_resp() - Handle a response frame from Get Port Names (GPN_FT)
 * @sp:	    The sequence that the GPN_FT response was received on
 * @fp:	    The GPN_FT response frame
 * @lp_arg: The discovery context
 *
 * Locking Note: This function is called without disc mutex held, and
 *		 should do all its processing with the mutex held
 */
static void fc_disc_gpn_ft_resp(struct fc_seq *sp, struct fc_frame *fp,
				void *disc_arg)
{
	struct fc_disc *disc = disc_arg;
	struct fc_ct_hdr *cp;
	struct fc_frame_header *fh;
	enum fc_disc_event event = DISC_EV_NONE;
	unsigned int seq_cnt;
	unsigned int len;
	int error = 0;

	mutex_lock(&disc->disc_mutex);
	FC_DISC_DBG(disc, "Received a GPN_FT response\n");

	if (IS_ERR(fp)) {
		fc_disc_error(disc, fp);
		mutex_unlock(&disc->disc_mutex);
		return;
	}

	WARN_ON(!fc_frame_is_linear(fp));	/* buffer must be contiguous */
	fh = fc_frame_header_get(fp);
	len = fr_len(fp) - sizeof(*fh);
	seq_cnt = ntohs(fh->fh_seq_cnt);
	if (fr_sof(fp) == FC_SOF_I3 && seq_cnt == 0 && disc->seq_count == 0) {
		cp = fc_frame_payload_get(fp, sizeof(*cp));
		if (!cp) {
			FC_DISC_DBG(disc, "GPN_FT response too short, len %d\n",
				    fr_len(fp));
			event = DISC_EV_FAILED;
		} else if (ntohs(cp->ct_cmd) == FC_FS_ACC) {

			/* Accepted, parse the response. */
			len -= sizeof(*cp);
			error = fc_disc_gpn_ft_parse(disc, cp + 1, len);
		} else if (ntohs(cp->ct_cmd) == FC_FS_RJT) {
			FC_DISC_DBG(disc, "GPN_FT rejected reason %x exp %x "
				    "(check zoning)\n", cp->ct_reason,
				    cp->ct_explan);
			event = DISC_EV_FAILED;
			if (cp->ct_reason == FC_FS_RJT_UNABL &&
			    cp->ct_explan == FC_FS_EXP_FTNR)
				event = DISC_EV_SUCCESS;
		} else {
			FC_DISC_DBG(disc, "GPN_FT unexpected response code "
				    "%x\n", ntohs(cp->ct_cmd));
			event = DISC_EV_FAILED;
		}
	} else if (fr_sof(fp) == FC_SOF_N3 && seq_cnt == disc->seq_count) {
		error = fc_disc_gpn_ft_parse(disc, fh + 1, len);
	} else {
		FC_DISC_DBG(disc, "GPN_FT unexpected frame - out of sequence? "
			    "seq_cnt %x expected %x sof %x eof %x\n",
			    seq_cnt, disc->seq_count, fr_sof(fp), fr_eof(fp));
		event = DISC_EV_FAILED;
	}
	if (error)
		fc_disc_error(disc, fp);
	else if (event != DISC_EV_NONE)
		fc_disc_done(disc, event);
	fc_frame_free(fp);
	mutex_unlock(&disc->disc_mutex);
}

/**
 * fc_disc_gpn_id_resp() - Handle a response frame from Get Port Names (GPN_ID)
 * @sp:	       The sequence the GPN_ID is on
 * @fp:	       The response frame
 * @rdata_arg: The remote port that sent the GPN_ID response
 *
 * Locking Note: This function is called without disc mutex held.
 */
static void fc_disc_gpn_id_resp(struct fc_seq *sp, struct fc_frame *fp,
				void *rdata_arg)
{
	struct fc_rport_priv *rdata = rdata_arg;
	struct fc_rport_priv *new_rdata;
	struct fc_lport *lport;
	struct fc_disc *disc;
	struct fc_ct_hdr *cp;
	struct fc_ns_gid_pn *pn;
	u64 port_name;

	lport = rdata->local_port;
	disc = &lport->disc;

	mutex_lock(&disc->disc_mutex);
	if (PTR_ERR(fp) == -FC_EX_CLOSED)
		goto out;
	if (IS_ERR(fp))
		goto redisc;

	cp = fc_frame_payload_get(fp, sizeof(*cp));
	if (!cp)
		goto redisc;
	if (ntohs(cp->ct_cmd) == FC_FS_ACC) {
		if (fr_len(fp) < sizeof(struct fc_frame_header) +
		    sizeof(*cp) + sizeof(*pn))
			goto redisc;
		pn = (struct fc_ns_gid_pn *)(cp + 1);
		port_name = get_unaligned_be64(&pn->fn_wwpn);
		if (rdata->ids.port_name == -1)
			rdata->ids.port_name = port_name;
		else if (rdata->ids.port_name != port_name) {
			FC_DISC_DBG(disc, "GPN_ID accepted.  WWPN changed. "
				    "Port-id %6.6x wwpn %16.16llx\n",
				    rdata->ids.port_id, port_name);
			lport->tt.rport_logoff(rdata);

			new_rdata = lport->tt.rport_create(lport,
							   rdata->ids.port_id);
			if (new_rdata) {
				new_rdata->disc_id = disc->disc_id;
				lport->tt.rport_login(new_rdata);
			}
			goto out;
		}
		rdata->disc_id = disc->disc_id;
		lport->tt.rport_login(rdata);
	} else if (ntohs(cp->ct_cmd) == FC_FS_RJT) {
		FC_DISC_DBG(disc, "GPN_ID rejected reason %x exp %x\n",
			    cp->ct_reason, cp->ct_explan);
		lport->tt.rport_logoff(rdata);
	} else {
		FC_DISC_DBG(disc, "GPN_ID unexpected response code %x\n",
			    ntohs(cp->ct_cmd));
redisc:
		fc_disc_restart(disc);
	}
out:
	mutex_unlock(&disc->disc_mutex);
	kref_put(&rdata->kref, lport->tt.rport_destroy);
}

/**
 * fc_disc_gpn_id_req() - Send Get Port Names by ID (GPN_ID) request
 * @lport: The local port to initiate discovery on
 * @rdata: remote port private data
 *
 * Locking Note: This function expects that the disc_mutex is locked
 *		 before it is called.
 * On failure, an error code is returned.
 */
static int fc_disc_gpn_id_req(struct fc_lport *lport,
			      struct fc_rport_priv *rdata)
{
	struct fc_frame *fp;

	fp = fc_frame_alloc(lport, sizeof(struct fc_ct_hdr) +
			    sizeof(struct fc_ns_fid));
	if (!fp)
		return -ENOMEM;
	if (!lport->tt.elsct_send(lport, rdata->ids.port_id, fp, FC_NS_GPN_ID,
				  fc_disc_gpn_id_resp, rdata,
				  3 * lport->r_a_tov))
		return -ENOMEM;
	kref_get(&rdata->kref);
	return 0;
}

/**
 * fc_disc_single() - Discover the directory information for a single target
 * @lport: The local port the remote port is associated with
 * @dp:	   The port to rediscover
 *
 * Locking Note: This function expects that the disc_mutex is locked
 *		 before it is called.
 */
static int fc_disc_single(struct fc_lport *lport, struct fc_disc_port *dp)
{
	struct fc_rport_priv *rdata;

	rdata = lport->tt.rport_create(lport, dp->port_id);
	if (!rdata)
		return -ENOMEM;
	rdata->disc_id = 0;
	return fc_disc_gpn_id_req(lport, rdata);
}

/**
 * fc_disc_stop() - Stop discovery for a given lport
 * @lport: The local port that discovery should stop on
 */
void fc_disc_stop(struct fc_lport *lport)
{
	struct fc_disc *disc = &lport->disc;

	if (disc->pending)
		cancel_delayed_work_sync(&disc->disc_work);
	fc_disc_stop_rports(disc);
}

/**
 * fc_disc_stop_final() - Stop discovery for a given lport
 * @lport: The lport that discovery should stop on
 *
 * This function will block until discovery has been
 * completely stopped and all rports have been deleted.
 */
void fc_disc_stop_final(struct fc_lport *lport)
{
	fc_disc_stop(lport);
	lport->tt.rport_flush_queue();
}

/**
 * fc_disc_init() - Initialize the discovery layer for a local port
 * @lport: The local port that needs the discovery layer to be initialized
 */
int fc_disc_init(struct fc_lport *lport)
{
	struct fc_disc *disc;

	if (!lport->tt.disc_start)
		lport->tt.disc_start = fc_disc_start;

	if (!lport->tt.disc_stop)
		lport->tt.disc_stop = fc_disc_stop;

	if (!lport->tt.disc_stop_final)
		lport->tt.disc_stop_final = fc_disc_stop_final;

	if (!lport->tt.disc_recv_req)
		lport->tt.disc_recv_req = fc_disc_recv_req;

	disc = &lport->disc;
	INIT_DELAYED_WORK(&disc->disc_work, fc_disc_timeout);
	mutex_init(&disc->disc_mutex);
	INIT_LIST_HEAD(&disc->rports);

	disc->priv = lport;

	return 0;
}
EXPORT_SYMBOL(fc_disc_init);
