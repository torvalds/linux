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
#include <linux/err.h>
#include <asm/unaligned.h>

#include <scsi/fc/fc_gs.h>

#include <scsi/libfc.h>

#define FC_DISC_RETRY_LIMIT	3	/* max retries */
#define FC_DISC_RETRY_DELAY	500UL	/* (msecs) delay */

static void fc_disc_gpn_ft_req(struct fc_disc *);
static void fc_disc_gpn_ft_resp(struct fc_seq *, struct fc_frame *, void *);
static int fc_disc_new_target(struct fc_disc *, struct fc_rport_priv *,
			      struct fc_rport_identifiers *);
static void fc_disc_done(struct fc_disc *, enum fc_disc_event);
static void fc_disc_timeout(struct work_struct *);
static void fc_disc_single(struct fc_disc *, struct fc_disc_port *);
static void fc_disc_restart(struct fc_disc *);

/**
 * fc_disc_lookup_rport() - lookup a remote port by port_id
 * @lport: Fibre Channel host port instance
 * @port_id: remote port port_id to match
 */
struct fc_rport_priv *fc_disc_lookup_rport(const struct fc_lport *lport,
					   u32 port_id)
{
	const struct fc_disc *disc = &lport->disc;
	struct fc_rport_priv *rdata;

	list_for_each_entry(rdata, &disc->rports, peers) {
		if (rdata->ids.port_id == port_id &&
		    rdata->rp_state != RPORT_ST_DELETE)
			return rdata;
	}
	return NULL;
}

/**
 * fc_disc_stop_rports() - delete all the remote ports associated with the lport
 * @disc: The discovery job to stop rports on
 *
 * Locking Note: This function expects that the lport mutex is locked before
 * calling it.
 */
void fc_disc_stop_rports(struct fc_disc *disc)
{
	struct fc_lport *lport;
	struct fc_rport_priv *rdata, *next;

	lport = disc->lport;

	mutex_lock(&disc->disc_mutex);
	list_for_each_entry_safe(rdata, next, &disc->rports, peers)
		lport->tt.rport_logoff(rdata);
	mutex_unlock(&disc->disc_mutex);
}

/**
 * fc_disc_recv_rscn_req() - Handle Registered State Change Notification (RSCN)
 * @sp: Current sequence of the RSCN exchange
 * @fp: RSCN Frame
 * @lport: Fibre Channel host port instance
 *
 * Locking Note: This function expects that the disc_mutex is locked
 *		 before it is called.
 */
static void fc_disc_recv_rscn_req(struct fc_seq *sp, struct fc_frame *fp,
				  struct fc_disc *disc)
{
	struct fc_lport *lport;
	struct fc_rport_priv *rdata;
	struct fc_els_rscn *rp;
	struct fc_els_rscn_page *pp;
	struct fc_seq_els_data rjt_data;
	unsigned int len;
	int redisc = 0;
	enum fc_els_rscn_ev_qual ev_qual;
	enum fc_els_rscn_addr_fmt fmt;
	LIST_HEAD(disc_ports);
	struct fc_disc_port *dp, *next;

	lport = disc->lport;

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
				    "(%6x)\n", ntoh24(pp->rscn_fid));
			dp = kzalloc(sizeof(*dp), GFP_KERNEL);
			if (!dp) {
				redisc = 1;
				break;
			}
			dp->lp = lport;
			dp->ids.port_id = ntoh24(pp->rscn_fid);
			dp->ids.port_name = -1;
			dp->ids.node_name = -1;
			dp->ids.roles = FC_RPORT_ROLE_UNKNOWN;
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
	lport->tt.seq_els_rsp_send(sp, ELS_LS_ACC, NULL);
	if (redisc) {
		FC_DISC_DBG(disc, "RSCN received: rediscovering\n");
		fc_disc_restart(disc);
	} else {
		FC_DISC_DBG(disc, "RSCN received: not rediscovering. "
			    "redisc %d state %d in_prog %d\n",
			    redisc, lport->state, disc->pending);
		list_for_each_entry_safe(dp, next, &disc_ports, peers) {
			list_del(&dp->peers);
			rdata = lport->tt.rport_lookup(lport, dp->ids.port_id);
			if (rdata) {
				lport->tt.rport_logoff(rdata);
			}
			fc_disc_single(disc, dp);
		}
	}
	fc_frame_free(fp);
	return;
reject:
	FC_DISC_DBG(disc, "Received a bad RSCN frame\n");
	rjt_data.fp = NULL;
	rjt_data.reason = ELS_RJT_LOGIC;
	rjt_data.explan = ELS_EXPL_NONE;
	lport->tt.seq_els_rsp_send(sp, ELS_LS_RJT, &rjt_data);
	fc_frame_free(fp);
}

/**
 * fc_disc_recv_req() - Handle incoming requests
 * @sp: Current sequence of the request exchange
 * @fp: The frame
 * @lport: The FC local port
 *
 * Locking Note: This function is called from the EM and will lock
 *		 the disc_mutex before calling the handler for the
 *		 request.
 */
static void fc_disc_recv_req(struct fc_seq *sp, struct fc_frame *fp,
			     struct fc_lport *lport)
{
	u8 op;
	struct fc_disc *disc = &lport->disc;

	op = fc_frame_payload_op(fp);
	switch (op) {
	case ELS_RSCN:
		mutex_lock(&disc->disc_mutex);
		fc_disc_recv_rscn_req(sp, fp, disc);
		mutex_unlock(&disc->disc_mutex);
		break;
	default:
		FC_DISC_DBG(disc, "Received an unsupported request, "
			    "the opcode is (%x)\n", op);
		break;
	}
}

/**
 * fc_disc_restart() - Restart discovery
 * @lport: FC discovery context
 *
 * Locking Note: This function expects that the disc mutex
 *		 is already locked.
 */
static void fc_disc_restart(struct fc_disc *disc)
{
	struct fc_rport_priv *rdata, *next;
	struct fc_lport *lport = disc->lport;

	FC_DISC_DBG(disc, "Restarting discovery\n");

	list_for_each_entry_safe(rdata, next, &disc->rports, peers)
		lport->tt.rport_logoff(rdata);

	disc->requested = 1;
	if (!disc->pending)
		fc_disc_gpn_ft_req(disc);
}

/**
 * fc_disc_start() - Fibre Channel Target discovery
 * @lport: FC local port
 *
 * Returns non-zero if discovery cannot be started.
 */
static void fc_disc_start(void (*disc_callback)(struct fc_lport *,
						enum fc_disc_event),
			  struct fc_lport *lport)
{
	struct fc_rport_priv *rdata;
	struct fc_disc *disc = &lport->disc;

	/*
	 * At this point we may have a new disc job or an existing
	 * one. Either way, let's lock when we make changes to it
	 * and send the GPN_FT request.
	 */
	mutex_lock(&disc->disc_mutex);

	disc->disc_callback = disc_callback;

	/*
	 * If not ready, or already running discovery, just set request flag.
	 */
	disc->requested = 1;

	if (disc->pending) {
		mutex_unlock(&disc->disc_mutex);
		return;
	}

	/*
	 * Handle point-to-point mode as a simple discovery
	 * of the remote port. Yucky, yucky, yuck, yuck!
	 */
	rdata = disc->lport->ptp_rp;
	if (rdata) {
		kref_get(&rdata->kref);
		if (!fc_disc_new_target(disc, rdata, &rdata->ids)) {
			fc_disc_done(disc, DISC_EV_SUCCESS);
		}
		kref_put(&rdata->kref, rdata->local_port->tt.rport_destroy);
	} else {
		fc_disc_gpn_ft_req(disc);	/* get ports by FC-4 type */
	}

	mutex_unlock(&disc->disc_mutex);
}

/**
 * fc_disc_new_target() - Handle new target found by discovery
 * @lport: FC local port
 * @rdata: The previous FC remote port priv (NULL if new remote port)
 * @ids: Identifiers for the new FC remote port
 *
 * Locking Note: This function expects that the disc_mutex is locked
 *		 before it is called.
 */
static int fc_disc_new_target(struct fc_disc *disc,
			      struct fc_rport_priv *rdata,
			      struct fc_rport_identifiers *ids)
{
	struct fc_lport *lport = disc->lport;
	int error = 0;

	if (rdata && ids->port_name) {
		if (rdata->ids.port_name == -1) {
			/*
			 * Set WWN and fall through to notify of create.
			 */
			rdata->ids.port_name = ids->port_name;
			rdata->ids.node_name = ids->node_name;
		} else if (rdata->ids.port_name != ids->port_name) {
			/*
			 * This is a new port with the same FCID as
			 * a previously-discovered port.  Presumably the old
			 * port logged out and a new port logged in and was
			 * assigned the same FCID.  This should be rare.
			 * Delete the old one and fall thru to re-create.
			 */
			lport->tt.rport_logoff(rdata);
			rdata = NULL;
		}
	}
	if (((ids->port_name != -1) || (ids->port_id != -1)) &&
	    ids->port_id != fc_host_port_id(lport->host) &&
	    ids->port_name != lport->wwpn) {
		if (!rdata) {
			rdata = lport->tt.rport_create(lport, ids);
			if (!rdata)
				error = -ENOMEM;
		}
		if (rdata)
			lport->tt.rport_login(rdata);
	}
	return error;
}

/**
 * fc_disc_done() - Discovery has been completed
 * @disc: FC discovery context
 * @event: discovery completion status
 *
 * Locking Note: This function expects that the disc mutex is locked before
 * it is called. The discovery callback is then made with the lock released,
 * and the lock is re-taken before returning from this function
 */
static void fc_disc_done(struct fc_disc *disc, enum fc_disc_event event)
{
	struct fc_lport *lport = disc->lport;

	FC_DISC_DBG(disc, "Discovery complete\n");

	if (disc->requested)
		fc_disc_gpn_ft_req(disc);
	else
		disc->pending = 0;

	mutex_unlock(&disc->disc_mutex);
	disc->disc_callback(lport, event);
	mutex_lock(&disc->disc_mutex);
}

/**
 * fc_disc_error() - Handle error on dNS request
 * @disc: FC discovery context
 * @fp: The frame pointer
 */
static void fc_disc_error(struct fc_disc *disc, struct fc_frame *fp)
{
	struct fc_lport *lport = disc->lport;
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
 * @lport: FC discovery context
 *
 * Locking Note: This function expects that the disc_mutex is locked
 *		 before it is called.
 */
static void fc_disc_gpn_ft_req(struct fc_disc *disc)
{
	struct fc_frame *fp;
	struct fc_lport *lport = disc->lport;

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
				 disc, lport->e_d_tov))
		return;
err:
	fc_disc_error(disc, fp);
}

/**
 * fc_disc_gpn_ft_parse() - Parse the body of the dNS GPN_FT response.
 * @lport: Fibre Channel host port instance
 * @buf: GPN_FT response buffer
 * @len: size of response buffer
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

	lport = disc->lport;

	/*
	 * Handle partial name record left over from previous call.
	 */
	bp = buf;
	plen = len;
	np = (struct fc_gpn_ft_resp *)bp;
	tlen = disc->buf_len;
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
		ids.node_name = -1;
		ids.roles = FC_RPORT_ROLE_UNKNOWN;

		if (ids.port_id != fc_host_port_id(lport->host) &&
		    ids.port_name != lport->wwpn) {
			rdata = lport->tt.rport_create(lport, &ids);
			if (rdata)
				lport->tt.rport_login(rdata);
			else
				printk(KERN_WARNING "libfc: Failed to allocate "
				       "memory for the newly discovered port "
				       "(%6x)\n", ids.port_id);
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
	} else {
		disc->buf_len = 0;
	}
	return error;
}

/**
 * fc_disc_timeout() - Retry handler for the disc component
 * @work: Structure holding disc obj that needs retry discovery
 *
 * Handle retry of memory allocation for remote ports.
 */
static void fc_disc_timeout(struct work_struct *work)
{
	struct fc_disc *disc = container_of(work,
					    struct fc_disc,
					    disc_work.work);
	mutex_lock(&disc->disc_mutex);
	if (disc->requested && !disc->pending)
		fc_disc_gpn_ft_req(disc);
	mutex_unlock(&disc->disc_mutex);
}

/**
 * fc_disc_gpn_ft_resp() - Handle a response frame from Get Port Names (GPN_FT)
 * @sp: Current sequence of GPN_FT exchange
 * @fp: response frame
 * @lp_arg: Fibre Channel host port instance
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
	unsigned int seq_cnt;
	void *buf = NULL;
	unsigned int len;
	int error;

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
	if (fr_sof(fp) == FC_SOF_I3 && seq_cnt == 0 &&
	    disc->seq_count == 0) {
		cp = fc_frame_payload_get(fp, sizeof(*cp));
		if (!cp) {
			FC_DISC_DBG(disc, "GPN_FT response too short, len %d\n",
				    fr_len(fp));
		} else if (ntohs(cp->ct_cmd) == FC_FS_ACC) {

			/* Accepted, parse the response. */
			buf = cp + 1;
			len -= sizeof(*cp);
		} else if (ntohs(cp->ct_cmd) == FC_FS_RJT) {
			FC_DISC_DBG(disc, "GPN_FT rejected reason %x exp %x "
				    "(check zoning)\n", cp->ct_reason,
				    cp->ct_explan);
			fc_disc_done(disc, DISC_EV_FAILED);
		} else {
			FC_DISC_DBG(disc, "GPN_FT unexpected response code "
				    "%x\n", ntohs(cp->ct_cmd));
		}
	} else if (fr_sof(fp) == FC_SOF_N3 &&
		   seq_cnt == disc->seq_count) {
		buf = fh + 1;
	} else {
		FC_DISC_DBG(disc, "GPN_FT unexpected frame - out of sequence? "
			    "seq_cnt %x expected %x sof %x eof %x\n",
			    seq_cnt, disc->seq_count, fr_sof(fp), fr_eof(fp));
	}
	if (buf) {
		error = fc_disc_gpn_ft_parse(disc, buf, len);
		if (error)
			fc_disc_error(disc, fp);
		else
			disc->seq_count++;
	}
	fc_frame_free(fp);

	mutex_unlock(&disc->disc_mutex);
}

/**
 * fc_disc_single() - Discover the directory information for a single target
 * @lport: FC local port
 * @dp: The port to rediscover
 *
 * Locking Note: This function expects that the disc_mutex is locked
 *		 before it is called.
 */
static void fc_disc_single(struct fc_disc *disc, struct fc_disc_port *dp)
{
	struct fc_lport *lport;
	struct fc_rport_priv *rdata;

	lport = disc->lport;

	if (dp->ids.port_id == fc_host_port_id(lport->host))
		goto out;

	rdata = lport->tt.rport_create(lport, &dp->ids);
	if (rdata) {
		kfree(dp);
		lport->tt.rport_login(rdata);
	}
	return;
out:
	kfree(dp);
}

/**
 * fc_disc_stop() - Stop discovery for a given lport
 * @lport: The lport that discovery should stop for
 */
void fc_disc_stop(struct fc_lport *lport)
{
	struct fc_disc *disc = &lport->disc;

	if (disc) {
		cancel_delayed_work_sync(&disc->disc_work);
		fc_disc_stop_rports(disc);
	}
}

/**
 * fc_disc_stop_final() - Stop discovery for a given lport
 * @lport: The lport that discovery should stop for
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
 * fc_disc_init() - Initialize the discovery block
 * @lport: FC local port
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

	if (!lport->tt.rport_lookup)
		lport->tt.rport_lookup = fc_disc_lookup_rport;

	disc = &lport->disc;
	INIT_DELAYED_WORK(&disc->disc_work, fc_disc_timeout);
	mutex_init(&disc->disc_mutex);
	INIT_LIST_HEAD(&disc->rports);

	disc->lport = lport;

	return 0;
}
EXPORT_SYMBOL(fc_disc_init);
