/*
 * Copyright(c) 2007 Intel Corporation. All rights reserved.
 * Copyright(c) 2008 Red Hat, Inc.  All rights reserved.
 * Copyright(c) 2008 Mike Christie
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

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/scatterlist.h>
#include <linux/err.h>
#include <linux/crc32.h>

#include <scsi/scsi_tcq.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>

#include <scsi/fc/fc_fc2.h>

#include <scsi/libfc.h>
#include <scsi/fc_encode.h>

MODULE_AUTHOR("Open-FCoE.org");
MODULE_DESCRIPTION("libfc");
MODULE_LICENSE("GPL v2");

static int fc_fcp_debug;

#define FC_DEBUG_FCP(fmt...)			\
	do {					\
		if (fc_fcp_debug)		\
			FC_DBG(fmt);		\
	} while (0)

static struct kmem_cache *scsi_pkt_cachep;

/* SRB state definitions */
#define FC_SRB_FREE		0		/* cmd is free */
#define FC_SRB_CMD_SENT		(1 << 0)	/* cmd has been sent */
#define FC_SRB_RCV_STATUS	(1 << 1)	/* response has arrived */
#define FC_SRB_ABORT_PENDING	(1 << 2)	/* cmd abort sent to device */
#define FC_SRB_ABORTED		(1 << 3)	/* abort acknowleged */
#define FC_SRB_DISCONTIG	(1 << 4)	/* non-sequential data recvd */
#define FC_SRB_COMPL		(1 << 5)	/* fc_io_compl has been run */
#define FC_SRB_FCP_PROCESSING_TMO (1 << 6)	/* timer function processing */
#define FC_SRB_NOMEM		(1 << 7)	/* dropped to out of mem */

#define FC_SRB_READ		(1 << 1)
#define FC_SRB_WRITE		(1 << 0)

/*
 * The SCp.ptr should be tested and set under the host lock. NULL indicates
 * that the command has been retruned to the scsi layer.
 */
#define CMD_SP(Cmnd)		    ((struct fc_fcp_pkt *)(Cmnd)->SCp.ptr)
#define CMD_ENTRY_STATUS(Cmnd)	    ((Cmnd)->SCp.have_data_in)
#define CMD_COMPL_STATUS(Cmnd)	    ((Cmnd)->SCp.this_residual)
#define CMD_SCSI_STATUS(Cmnd)	    ((Cmnd)->SCp.Status)
#define CMD_RESID_LEN(Cmnd)	    ((Cmnd)->SCp.buffers_residual)

struct fc_fcp_internal {
	mempool_t	*scsi_pkt_pool;
	struct list_head scsi_pkt_queue;
	u8		throttled;
};

#define fc_get_scsi_internal(x)	((struct fc_fcp_internal *)(x)->scsi_priv)

/*
 * function prototypes
 * FC scsi I/O related functions
 */
static void fc_fcp_recv_data(struct fc_fcp_pkt *, struct fc_frame *);
static void fc_fcp_recv(struct fc_seq *, struct fc_frame *, void *);
static void fc_fcp_resp(struct fc_fcp_pkt *, struct fc_frame *);
static void fc_fcp_complete_locked(struct fc_fcp_pkt *);
static void fc_tm_done(struct fc_seq *, struct fc_frame *, void *);
static void fc_fcp_error(struct fc_fcp_pkt *fsp, struct fc_frame *fp);
static void fc_timeout_error(struct fc_fcp_pkt *);
static void fc_fcp_timeout(unsigned long data);
static void fc_fcp_rec(struct fc_fcp_pkt *);
static void fc_fcp_rec_error(struct fc_fcp_pkt *, struct fc_frame *);
static void fc_fcp_rec_resp(struct fc_seq *, struct fc_frame *, void *);
static void fc_io_compl(struct fc_fcp_pkt *);

static void fc_fcp_srr(struct fc_fcp_pkt *, enum fc_rctl, u32);
static void fc_fcp_srr_resp(struct fc_seq *, struct fc_frame *, void *);
static void fc_fcp_srr_error(struct fc_fcp_pkt *, struct fc_frame *);

/*
 * command status codes
 */
#define FC_COMPLETE		0
#define FC_CMD_ABORTED		1
#define FC_CMD_RESET		2
#define FC_CMD_PLOGO		3
#define FC_SNS_RCV		4
#define FC_TRANS_ERR		5
#define FC_DATA_OVRRUN		6
#define FC_DATA_UNDRUN		7
#define FC_ERROR		8
#define FC_HRD_ERROR		9
#define FC_CMD_TIME_OUT		10

/*
 * Error recovery timeout values.
 */
#define FC_SCSI_ER_TIMEOUT	(10 * HZ)
#define FC_SCSI_TM_TOV		(10 * HZ)
#define FC_SCSI_REC_TOV		(2 * HZ)
#define FC_HOST_RESET_TIMEOUT	(30 * HZ)

#define FC_MAX_ERROR_CNT	5
#define FC_MAX_RECOV_RETRY	3

#define FC_FCP_DFLT_QUEUE_DEPTH 32

/**
 * fc_fcp_pkt_alloc - allocation routine for scsi_pkt packet
 * @lp:		fc lport struct
 * @gfp:	gfp flags for allocation
 *
 * This is used by upper layer scsi driver.
 * Return Value : scsi_pkt structure or null on allocation failure.
 * Context	: call from process context. no locking required.
 */
static struct fc_fcp_pkt *fc_fcp_pkt_alloc(struct fc_lport *lp, gfp_t gfp)
{
	struct fc_fcp_internal *si = fc_get_scsi_internal(lp);
	struct fc_fcp_pkt *fsp;

	fsp = mempool_alloc(si->scsi_pkt_pool, gfp);
	if (fsp) {
		memset(fsp, 0, sizeof(*fsp));
		fsp->lp = lp;
		atomic_set(&fsp->ref_cnt, 1);
		init_timer(&fsp->timer);
		INIT_LIST_HEAD(&fsp->list);
		spin_lock_init(&fsp->scsi_pkt_lock);
	}
	return fsp;
}

/**
 * fc_fcp_pkt_release() - release hold on scsi_pkt packet
 * @fsp:	fcp packet struct
 *
 * This is used by upper layer scsi driver.
 * Context	: call from process  and interrupt context.
 *		  no locking required
 */
static void fc_fcp_pkt_release(struct fc_fcp_pkt *fsp)
{
	if (atomic_dec_and_test(&fsp->ref_cnt)) {
		struct fc_fcp_internal *si = fc_get_scsi_internal(fsp->lp);

		mempool_free(fsp, si->scsi_pkt_pool);
	}
}

static void fc_fcp_pkt_hold(struct fc_fcp_pkt *fsp)
{
	atomic_inc(&fsp->ref_cnt);
}

/**
 * fc_fcp_pkt_destory() - release hold on scsi_pkt packet
 * @seq:		exchange sequence
 * @fsp:	fcp packet struct
 *
 * Release hold on scsi_pkt packet set to keep scsi_pkt
 * till EM layer exch resource is not freed.
 * Context	: called from from EM layer.
 *		  no locking required
 */
static void fc_fcp_pkt_destroy(struct fc_seq *seq, void *fsp)
{
	fc_fcp_pkt_release(fsp);
}

/**
 * fc_fcp_lock_pkt() - lock a packet and get a ref to it.
 * @fsp:	fcp packet
 *
 * We should only return error if we return a command to scsi-ml before
 * getting a response. This can happen in cases where we send a abort, but
 * do not wait for the response and the abort and command can be passing
 * each other on the wire/network-layer.
 *
 * Note: this function locks the packet and gets a reference to allow
 * callers to call the completion function while the lock is held and
 * not have to worry about the packets refcount.
 *
 * TODO: Maybe we should just have callers grab/release the lock and
 * have a function that they call to verify the fsp and grab a ref if
 * needed.
 */
static inline int fc_fcp_lock_pkt(struct fc_fcp_pkt *fsp)
{
	spin_lock_bh(&fsp->scsi_pkt_lock);
	if (fsp->state & FC_SRB_COMPL) {
		spin_unlock_bh(&fsp->scsi_pkt_lock);
		return -EPERM;
	}

	fc_fcp_pkt_hold(fsp);
	return 0;
}

static inline void fc_fcp_unlock_pkt(struct fc_fcp_pkt *fsp)
{
	spin_unlock_bh(&fsp->scsi_pkt_lock);
	fc_fcp_pkt_release(fsp);
}

static void fc_fcp_timer_set(struct fc_fcp_pkt *fsp, unsigned long delay)
{
	if (!(fsp->state & FC_SRB_COMPL))
		mod_timer(&fsp->timer, jiffies + delay);
}

static int fc_fcp_send_abort(struct fc_fcp_pkt *fsp)
{
	if (!fsp->seq_ptr)
		return -EINVAL;

	fsp->state |= FC_SRB_ABORT_PENDING;
	return fsp->lp->tt.seq_exch_abort(fsp->seq_ptr, 0);
}

/*
 * Retry command.
 * An abort isn't needed.
 */
static void fc_fcp_retry_cmd(struct fc_fcp_pkt *fsp)
{
	if (fsp->seq_ptr) {
		fsp->lp->tt.exch_done(fsp->seq_ptr);
		fsp->seq_ptr = NULL;
	}

	fsp->state &= ~FC_SRB_ABORT_PENDING;
	fsp->io_status = 0;
	fsp->status_code = FC_ERROR;
	fc_fcp_complete_locked(fsp);
}

/*
 * fc_fcp_ddp_setup - calls to LLD's ddp_setup to set up DDP
 * transfer for a read I/O indicated by the fc_fcp_pkt.
 * @fsp: ptr to the fc_fcp_pkt
 *
 * This is called in exch_seq_send() when we have a newly allocated
 * exchange with a valid exchange id to setup ddp.
 *
 * returns: none
 */
void fc_fcp_ddp_setup(struct fc_fcp_pkt *fsp, u16 xid)
{
	struct fc_lport *lp;

	if (!fsp)
		return;

	lp = fsp->lp;
	if ((fsp->req_flags & FC_SRB_READ) &&
	    (lp->lro_enabled) && (lp->tt.ddp_setup)) {
		if (lp->tt.ddp_setup(lp, xid, scsi_sglist(fsp->cmd),
				     scsi_sg_count(fsp->cmd)))
			fsp->xfer_ddp = xid;
	}
}
EXPORT_SYMBOL(fc_fcp_ddp_setup);

/*
 * fc_fcp_ddp_done - calls to LLD's ddp_done to release any
 * DDP related resources for this I/O if it is initialized
 * as a ddp transfer
 * @fsp: ptr to the fc_fcp_pkt
 *
 * returns: none
 */
static void fc_fcp_ddp_done(struct fc_fcp_pkt *fsp)
{
	struct fc_lport *lp;

	if (!fsp)
		return;

	lp = fsp->lp;
	if (fsp->xfer_ddp && lp->tt.ddp_done) {
		fsp->xfer_len = lp->tt.ddp_done(lp, fsp->xfer_ddp);
		fsp->xfer_ddp = 0;
	}
}


/*
 * Receive SCSI data from target.
 * Called after receiving solicited data.
 */
static void fc_fcp_recv_data(struct fc_fcp_pkt *fsp, struct fc_frame *fp)
{
	struct scsi_cmnd *sc = fsp->cmd;
	struct fc_lport *lp = fsp->lp;
	struct fcoe_dev_stats *stats;
	struct fc_frame_header *fh;
	size_t start_offset;
	size_t offset;
	u32 crc;
	u32 copy_len = 0;
	size_t len;
	void *buf;
	struct scatterlist *sg;
	size_t remaining;

	fh = fc_frame_header_get(fp);
	offset = ntohl(fh->fh_parm_offset);
	start_offset = offset;
	len = fr_len(fp) - sizeof(*fh);
	buf = fc_frame_payload_get(fp, 0);

	/* if this I/O is ddped, update xfer len */
	fc_fcp_ddp_done(fsp);

	if (offset + len > fsp->data_len) {
		/* this should never happen */
		if ((fr_flags(fp) & FCPHF_CRC_UNCHECKED) &&
		    fc_frame_crc_check(fp))
			goto crc_err;
		FC_DEBUG_FCP("data received past end. len %zx offset %zx "
			     "data_len %x\n", len, offset, fsp->data_len);
		fc_fcp_retry_cmd(fsp);
		return;
	}
	if (offset != fsp->xfer_len)
		fsp->state |= FC_SRB_DISCONTIG;

	crc = 0;
	if (fr_flags(fp) & FCPHF_CRC_UNCHECKED)
		crc = crc32(~0, (u8 *) fh, sizeof(*fh));

	sg = scsi_sglist(sc);
	remaining = len;

	while (remaining > 0 && sg) {
		size_t off;
		void *page_addr;
		size_t sg_bytes;

		if (offset >= sg->length) {
			offset -= sg->length;
			sg = sg_next(sg);
			continue;
		}
		sg_bytes = min(remaining, sg->length - offset);

		/*
		 * The scatterlist item may be bigger than PAGE_SIZE,
		 * but we are limited to mapping PAGE_SIZE at a time.
		 */
		off = offset + sg->offset;
		sg_bytes = min(sg_bytes, (size_t)
			       (PAGE_SIZE - (off & ~PAGE_MASK)));
		page_addr = kmap_atomic(sg_page(sg) + (off >> PAGE_SHIFT),
					KM_SOFTIRQ0);
		if (!page_addr)
			break;		/* XXX panic? */

		if (fr_flags(fp) & FCPHF_CRC_UNCHECKED)
			crc = crc32(crc, buf, sg_bytes);
		memcpy((char *)page_addr + (off & ~PAGE_MASK), buf,
		       sg_bytes);

		kunmap_atomic(page_addr, KM_SOFTIRQ0);
		buf += sg_bytes;
		offset += sg_bytes;
		remaining -= sg_bytes;
		copy_len += sg_bytes;
	}

	if (fr_flags(fp) & FCPHF_CRC_UNCHECKED) {
		buf = fc_frame_payload_get(fp, 0);
		if (len % 4) {
			crc = crc32(crc, buf + len, 4 - (len % 4));
			len += 4 - (len % 4);
		}

		if (~crc != le32_to_cpu(fr_crc(fp))) {
crc_err:
			stats = fc_lport_get_stats(lp);
			stats->ErrorFrames++;
			/* FIXME - per cpu count, not total count! */
			if (stats->InvalidCRCCount++ < 5)
				printk(KERN_WARNING "CRC error on data frame for port (%6x)\n",
				       fc_host_port_id(lp->host));
			/*
			 * Assume the frame is total garbage.
			 * We may have copied it over the good part
			 * of the buffer.
			 * If so, we need to retry the entire operation.
			 * Otherwise, ignore it.
			 */
			if (fsp->state & FC_SRB_DISCONTIG)
				fc_fcp_retry_cmd(fsp);
			return;
		}
	}

	if (fsp->xfer_contig_end == start_offset)
		fsp->xfer_contig_end += copy_len;
	fsp->xfer_len += copy_len;

	/*
	 * In the very rare event that this data arrived after the response
	 * and completes the transfer, call the completion handler.
	 */
	if (unlikely(fsp->state & FC_SRB_RCV_STATUS) &&
	    fsp->xfer_len == fsp->data_len - fsp->scsi_resid)
		fc_fcp_complete_locked(fsp);
}

/**
 * fc_fcp_send_data() -  Send SCSI data to target.
 * @fsp: ptr to fc_fcp_pkt
 * @sp: ptr to this sequence
 * @offset: starting offset for this data request
 * @seq_blen: the burst length for this data request
 *
 * Called after receiving a Transfer Ready data descriptor.
 * if LLD is capable of seq offload then send down seq_blen
 * size of data in single frame, otherwise send multiple FC
 * frames of max FC frame payload supported by target port.
 *
 * Returns : 0 for success.
 */
static int fc_fcp_send_data(struct fc_fcp_pkt *fsp, struct fc_seq *seq,
			    size_t offset, size_t seq_blen)
{
	struct fc_exch *ep;
	struct scsi_cmnd *sc;
	struct scatterlist *sg;
	struct fc_frame *fp = NULL;
	struct fc_lport *lp = fsp->lp;
	size_t remaining;
	size_t t_blen;
	size_t tlen;
	size_t sg_bytes;
	size_t frame_offset, fh_parm_offset;
	int error;
	void *data = NULL;
	void *page_addr;
	int using_sg = lp->sg_supp;
	u32 f_ctl;

	WARN_ON(seq_blen <= 0);
	if (unlikely(offset + seq_blen > fsp->data_len)) {
		/* this should never happen */
		FC_DEBUG_FCP("xfer-ready past end. seq_blen %zx offset %zx\n",
			     seq_blen, offset);
		fc_fcp_send_abort(fsp);
		return 0;
	} else if (offset != fsp->xfer_len) {
		/* Out of Order Data Request - no problem, but unexpected. */
		FC_DEBUG_FCP("xfer-ready non-contiguous. "
			     "seq_blen %zx offset %zx\n", seq_blen, offset);
	}

	/*
	 * if LLD is capable of seq_offload then set transport
	 * burst length (t_blen) to seq_blen, otherwise set t_blen
	 * to max FC frame payload previously set in fsp->max_payload.
	 */
	t_blen = fsp->max_payload;
	if (lp->seq_offload) {
		t_blen = min(seq_blen, (size_t)lp->lso_max);
		FC_DEBUG_FCP("fsp=%p:lso:blen=%zx lso_max=0x%x t_blen=%zx\n",
			   fsp, seq_blen, lp->lso_max, t_blen);
	}

	WARN_ON(t_blen < FC_MIN_MAX_PAYLOAD);
	if (t_blen > 512)
		t_blen &= ~(512 - 1);	/* round down to block size */
	WARN_ON(t_blen < FC_MIN_MAX_PAYLOAD);	/* won't go below 256 */
	sc = fsp->cmd;

	remaining = seq_blen;
	fh_parm_offset = frame_offset = offset;
	tlen = 0;
	seq = lp->tt.seq_start_next(seq);
	f_ctl = FC_FC_REL_OFF;
	WARN_ON(!seq);

	/*
	 * If a get_page()/put_page() will fail, don't use sg lists
	 * in the fc_frame structure.
	 *
	 * The put_page() may be long after the I/O has completed
	 * in the case of FCoE, since the network driver does it
	 * via free_skb().  See the test in free_pages_check().
	 *
	 * Test this case with 'dd </dev/zero >/dev/st0 bs=64k'.
	 */
	if (using_sg) {
		for (sg = scsi_sglist(sc); sg; sg = sg_next(sg)) {
			if (page_count(sg_page(sg)) == 0 ||
			    (sg_page(sg)->flags & (1 << PG_lru |
						   1 << PG_private |
						   1 << PG_locked |
						   1 << PG_active |
						   1 << PG_slab |
						   1 << PG_swapcache |
						   1 << PG_writeback |
						   1 << PG_reserved |
						   1 << PG_buddy))) {
				using_sg = 0;
				break;
			}
		}
	}
	sg = scsi_sglist(sc);

	while (remaining > 0 && sg) {
		if (offset >= sg->length) {
			offset -= sg->length;
			sg = sg_next(sg);
			continue;
		}
		if (!fp) {
			tlen = min(t_blen, remaining);

			/*
			 * TODO.  Temporary workaround.	 fc_seq_send() can't
			 * handle odd lengths in non-linear skbs.
			 * This will be the final fragment only.
			 */
			if (tlen % 4)
				using_sg = 0;
			if (using_sg) {
				fp = _fc_frame_alloc(lp, 0);
				if (!fp)
					return -ENOMEM;
			} else {
				fp = fc_frame_alloc(lp, tlen);
				if (!fp)
					return -ENOMEM;

				data = (void *)(fr_hdr(fp)) +
					sizeof(struct fc_frame_header);
			}
			fh_parm_offset = frame_offset;
			fr_max_payload(fp) = fsp->max_payload;
		}
		sg_bytes = min(tlen, sg->length - offset);
		if (using_sg) {
			WARN_ON(skb_shinfo(fp_skb(fp))->nr_frags >
				FC_FRAME_SG_LEN);
			get_page(sg_page(sg));
			skb_fill_page_desc(fp_skb(fp),
					   skb_shinfo(fp_skb(fp))->nr_frags,
					   sg_page(sg), sg->offset + offset,
					   sg_bytes);
			fp_skb(fp)->data_len += sg_bytes;
			fr_len(fp) += sg_bytes;
			fp_skb(fp)->truesize += PAGE_SIZE;
		} else {
			size_t off = offset + sg->offset;

			/*
			 * The scatterlist item may be bigger than PAGE_SIZE,
			 * but we must not cross pages inside the kmap.
			 */
			sg_bytes = min(sg_bytes, (size_t) (PAGE_SIZE -
							   (off & ~PAGE_MASK)));
			page_addr = kmap_atomic(sg_page(sg) +
						(off >> PAGE_SHIFT),
						KM_SOFTIRQ0);
			memcpy(data, (char *)page_addr + (off & ~PAGE_MASK),
			       sg_bytes);
			kunmap_atomic(page_addr, KM_SOFTIRQ0);
			data += sg_bytes;
		}
		offset += sg_bytes;
		frame_offset += sg_bytes;
		tlen -= sg_bytes;
		remaining -= sg_bytes;

		if (tlen)
			continue;

		/*
		 * Send sequence with transfer sequence initiative in case
		 * this is last FCP frame of the sequence.
		 */
		if (remaining == 0)
			f_ctl |= FC_FC_SEQ_INIT | FC_FC_END_SEQ;

		ep = fc_seq_exch(seq);
		fc_fill_fc_hdr(fp, FC_RCTL_DD_SOL_DATA, ep->did, ep->sid,
			       FC_TYPE_FCP, f_ctl, fh_parm_offset);

		/*
		 * send fragment using for a sequence.
		 */
		error = lp->tt.seq_send(lp, seq, fp);
		if (error) {
			WARN_ON(1);		/* send error should be rare */
			fc_fcp_retry_cmd(fsp);
			return 0;
		}
		fp = NULL;
	}
	fsp->xfer_len += seq_blen;	/* premature count? */
	return 0;
}

static void fc_fcp_abts_resp(struct fc_fcp_pkt *fsp, struct fc_frame *fp)
{
	int ba_done = 1;
	struct fc_ba_rjt *brp;
	struct fc_frame_header *fh;

	fh = fc_frame_header_get(fp);
	switch (fh->fh_r_ctl) {
	case FC_RCTL_BA_ACC:
		break;
	case FC_RCTL_BA_RJT:
		brp = fc_frame_payload_get(fp, sizeof(*brp));
		if (brp && brp->br_reason == FC_BA_RJT_LOG_ERR)
			break;
		/* fall thru */
	default:
		/*
		 * we will let the command timeout
		 * and scsi-ml recover in this case,
		 * therefore cleared the ba_done flag.
		 */
		ba_done = 0;
	}

	if (ba_done) {
		fsp->state |= FC_SRB_ABORTED;
		fsp->state &= ~FC_SRB_ABORT_PENDING;

		if (fsp->wait_for_comp)
			complete(&fsp->tm_done);
		else
			fc_fcp_complete_locked(fsp);
	}
}

/**
 * fc_fcp_reduce_can_queue() - drop can_queue
 * @lp: lport to drop queueing for
 *
 * If we are getting memory allocation failures, then we may
 * be trying to execute too many commands. We let the running
 * commands complete or timeout, then try again with a reduced
 * can_queue. Eventually we will hit the point where we run
 * on all reserved structs.
 */
static void fc_fcp_reduce_can_queue(struct fc_lport *lp)
{
	struct fc_fcp_internal *si = fc_get_scsi_internal(lp);
	unsigned long flags;
	int can_queue;

	spin_lock_irqsave(lp->host->host_lock, flags);
	if (si->throttled)
		goto done;
	si->throttled = 1;

	can_queue = lp->host->can_queue;
	can_queue >>= 1;
	if (!can_queue)
		can_queue = 1;
	lp->host->can_queue = can_queue;
	shost_printk(KERN_ERR, lp->host, "Could not allocate frame.\n"
		     "Reducing can_queue to %d.\n", can_queue);
done:
	spin_unlock_irqrestore(lp->host->host_lock, flags);
}

/**
 * fc_fcp_recv() - Reveive FCP frames
 * @seq: The sequence the frame is on
 * @fp: The FC frame
 * @arg: The related FCP packet
 *
 * Return   : None
 * Context  : called from Soft IRQ context
 *	      can not called holding list lock
 */
static void fc_fcp_recv(struct fc_seq *seq, struct fc_frame *fp, void *arg)
{
	struct fc_fcp_pkt *fsp = (struct fc_fcp_pkt *)arg;
	struct fc_lport *lp;
	struct fc_frame_header *fh;
	struct fcp_txrdy *dd;
	u8 r_ctl;
	int rc = 0;

	if (IS_ERR(fp))
		goto errout;

	fh = fc_frame_header_get(fp);
	r_ctl = fh->fh_r_ctl;
	lp = fsp->lp;

	if (!(lp->state & LPORT_ST_READY))
		goto out;
	if (fc_fcp_lock_pkt(fsp))
		goto out;
	fsp->last_pkt_time = jiffies;

	if (fh->fh_type == FC_TYPE_BLS) {
		fc_fcp_abts_resp(fsp, fp);
		goto unlock;
	}

	if (fsp->state & (FC_SRB_ABORTED | FC_SRB_ABORT_PENDING))
		goto unlock;

	if (r_ctl == FC_RCTL_DD_DATA_DESC) {
		/*
		 * received XFER RDY from the target
		 * need to send data to the target
		 */
		WARN_ON(fr_flags(fp) & FCPHF_CRC_UNCHECKED);
		dd = fc_frame_payload_get(fp, sizeof(*dd));
		WARN_ON(!dd);

		rc = fc_fcp_send_data(fsp, seq,
				      (size_t) ntohl(dd->ft_data_ro),
				      (size_t) ntohl(dd->ft_burst_len));
		if (!rc)
			seq->rec_data = fsp->xfer_len;
		else if (rc == -ENOMEM)
			fsp->state |= FC_SRB_NOMEM;
	} else if (r_ctl == FC_RCTL_DD_SOL_DATA) {
		/*
		 * received a DATA frame
		 * next we will copy the data to the system buffer
		 */
		WARN_ON(fr_len(fp) < sizeof(*fh));	/* len may be 0 */
		fc_fcp_recv_data(fsp, fp);
		seq->rec_data = fsp->xfer_contig_end;
	} else if (r_ctl == FC_RCTL_DD_CMD_STATUS) {
		WARN_ON(fr_flags(fp) & FCPHF_CRC_UNCHECKED);

		fc_fcp_resp(fsp, fp);
	} else {
		FC_DBG("unexpected frame.  r_ctl %x\n", r_ctl);
	}
unlock:
	fc_fcp_unlock_pkt(fsp);
out:
	fc_frame_free(fp);
errout:
	if (IS_ERR(fp))
		fc_fcp_error(fsp, fp);
	else if (rc == -ENOMEM)
		fc_fcp_reduce_can_queue(lp);
}

static void fc_fcp_resp(struct fc_fcp_pkt *fsp, struct fc_frame *fp)
{
	struct fc_frame_header *fh;
	struct fcp_resp *fc_rp;
	struct fcp_resp_ext *rp_ex;
	struct fcp_resp_rsp_info *fc_rp_info;
	u32 plen;
	u32 expected_len;
	u32 respl = 0;
	u32 snsl = 0;
	u8 flags = 0;

	plen = fr_len(fp);
	fh = (struct fc_frame_header *)fr_hdr(fp);
	if (unlikely(plen < sizeof(*fh) + sizeof(*fc_rp)))
		goto len_err;
	plen -= sizeof(*fh);
	fc_rp = (struct fcp_resp *)(fh + 1);
	fsp->cdb_status = fc_rp->fr_status;
	flags = fc_rp->fr_flags;
	fsp->scsi_comp_flags = flags;
	expected_len = fsp->data_len;

	/* if ddp, update xfer len */
	fc_fcp_ddp_done(fsp);

	if (unlikely((flags & ~FCP_CONF_REQ) || fc_rp->fr_status)) {
		rp_ex = (void *)(fc_rp + 1);
		if (flags & (FCP_RSP_LEN_VAL | FCP_SNS_LEN_VAL)) {
			if (plen < sizeof(*fc_rp) + sizeof(*rp_ex))
				goto len_err;
			fc_rp_info = (struct fcp_resp_rsp_info *)(rp_ex + 1);
			if (flags & FCP_RSP_LEN_VAL) {
				respl = ntohl(rp_ex->fr_rsp_len);
				if (respl != sizeof(*fc_rp_info))
					goto len_err;
				if (fsp->wait_for_comp) {
					/* Abuse cdb_status for rsp code */
					fsp->cdb_status = fc_rp_info->rsp_code;
					complete(&fsp->tm_done);
					/*
					 * tmfs will not have any scsi cmd so
					 * exit here
					 */
					return;
				} else
					goto err;
			}
			if (flags & FCP_SNS_LEN_VAL) {
				snsl = ntohl(rp_ex->fr_sns_len);
				if (snsl > SCSI_SENSE_BUFFERSIZE)
					snsl = SCSI_SENSE_BUFFERSIZE;
				memcpy(fsp->cmd->sense_buffer,
				       (char *)fc_rp_info + respl, snsl);
			}
		}
		if (flags & (FCP_RESID_UNDER | FCP_RESID_OVER)) {
			if (plen < sizeof(*fc_rp) + sizeof(rp_ex->fr_resid))
				goto len_err;
			if (flags & FCP_RESID_UNDER) {
				fsp->scsi_resid = ntohl(rp_ex->fr_resid);
				/*
				 * The cmnd->underflow is the minimum number of
				 * bytes that must be transfered for this
				 * command.  Provided a sense condition is not
				 * present, make sure the actual amount
				 * transferred is at least the underflow value
				 * or fail.
				 */
				if (!(flags & FCP_SNS_LEN_VAL) &&
				    (fc_rp->fr_status == 0) &&
				    (scsi_bufflen(fsp->cmd) -
				     fsp->scsi_resid) < fsp->cmd->underflow)
					goto err;
				expected_len -= fsp->scsi_resid;
			} else {
				fsp->status_code = FC_ERROR;
			}
		}
	}
	fsp->state |= FC_SRB_RCV_STATUS;

	/*
	 * Check for missing or extra data frames.
	 */
	if (unlikely(fsp->xfer_len != expected_len)) {
		if (fsp->xfer_len < expected_len) {
			/*
			 * Some data may be queued locally,
			 * Wait a at least one jiffy to see if it is delivered.
			 * If this expires without data, we may do SRR.
			 */
			fc_fcp_timer_set(fsp, 2);
			return;
		}
		fsp->status_code = FC_DATA_OVRRUN;
		FC_DBG("tgt %6x xfer len %zx greater than expected len %x. "
		       "data len %x\n",
		       fsp->rport->port_id,
		       fsp->xfer_len, expected_len, fsp->data_len);
	}
	fc_fcp_complete_locked(fsp);
	return;

len_err:
	FC_DBG("short FCP response. flags 0x%x len %u respl %u snsl %u\n",
	       flags, fr_len(fp), respl, snsl);
err:
	fsp->status_code = FC_ERROR;
	fc_fcp_complete_locked(fsp);
}

/**
 * fc_fcp_complete_locked() - complete processing of a fcp packet
 * @fsp:	fcp packet
 *
 * This function may sleep if a timer is pending. The packet lock must be
 * held, and the host lock must not be held.
 */
static void fc_fcp_complete_locked(struct fc_fcp_pkt *fsp)
{
	struct fc_lport *lp = fsp->lp;
	struct fc_seq *seq;
	struct fc_exch *ep;
	u32 f_ctl;

	if (fsp->state & FC_SRB_ABORT_PENDING)
		return;

	if (fsp->state & FC_SRB_ABORTED) {
		if (!fsp->status_code)
			fsp->status_code = FC_CMD_ABORTED;
	} else {
		/*
		 * Test for transport underrun, independent of response
		 * underrun status.
		 */
		if (fsp->xfer_len < fsp->data_len && !fsp->io_status &&
		    (!(fsp->scsi_comp_flags & FCP_RESID_UNDER) ||
		     fsp->xfer_len < fsp->data_len - fsp->scsi_resid)) {
			fsp->status_code = FC_DATA_UNDRUN;
			fsp->io_status = 0;
		}
	}

	seq = fsp->seq_ptr;
	if (seq) {
		fsp->seq_ptr = NULL;
		if (unlikely(fsp->scsi_comp_flags & FCP_CONF_REQ)) {
			struct fc_frame *conf_frame;
			struct fc_seq *csp;

			csp = lp->tt.seq_start_next(seq);
			conf_frame = fc_frame_alloc(fsp->lp, 0);
			if (conf_frame) {
				f_ctl = FC_FC_SEQ_INIT;
				f_ctl |= FC_FC_LAST_SEQ | FC_FC_END_SEQ;
				ep = fc_seq_exch(seq);
				fc_fill_fc_hdr(conf_frame, FC_RCTL_DD_SOL_CTL,
					       ep->did, ep->sid,
					       FC_TYPE_FCP, f_ctl, 0);
				lp->tt.seq_send(lp, csp, conf_frame);
			}
		}
		lp->tt.exch_done(seq);
	}
	fc_io_compl(fsp);
}

static void fc_fcp_cleanup_cmd(struct fc_fcp_pkt *fsp, int error)
{
	struct fc_lport *lp = fsp->lp;

	if (fsp->seq_ptr) {
		lp->tt.exch_done(fsp->seq_ptr);
		fsp->seq_ptr = NULL;
	}
	fsp->status_code = error;
}

/**
 * fc_fcp_cleanup_each_cmd() - Cleanup active commads
 * @lp:		logical port
 * @id:		target id
 * @lun:	lun
 * @error:	fsp status code
 *
 * If lun or id is -1, they are ignored.
 */
static void fc_fcp_cleanup_each_cmd(struct fc_lport *lp, unsigned int id,
				    unsigned int lun, int error)
{
	struct fc_fcp_internal *si = fc_get_scsi_internal(lp);
	struct fc_fcp_pkt *fsp;
	struct scsi_cmnd *sc_cmd;
	unsigned long flags;

	spin_lock_irqsave(lp->host->host_lock, flags);
restart:
	list_for_each_entry(fsp, &si->scsi_pkt_queue, list) {
		sc_cmd = fsp->cmd;
		if (id != -1 && scmd_id(sc_cmd) != id)
			continue;

		if (lun != -1 && sc_cmd->device->lun != lun)
			continue;

		fc_fcp_pkt_hold(fsp);
		spin_unlock_irqrestore(lp->host->host_lock, flags);

		if (!fc_fcp_lock_pkt(fsp)) {
			fc_fcp_cleanup_cmd(fsp, error);
			fc_io_compl(fsp);
			fc_fcp_unlock_pkt(fsp);
		}

		fc_fcp_pkt_release(fsp);
		spin_lock_irqsave(lp->host->host_lock, flags);
		/*
		 * while we dropped the lock multiple pkts could
		 * have been released, so we have to start over.
		 */
		goto restart;
	}
	spin_unlock_irqrestore(lp->host->host_lock, flags);
}

static void fc_fcp_abort_io(struct fc_lport *lp)
{
	fc_fcp_cleanup_each_cmd(lp, -1, -1, FC_HRD_ERROR);
}

/**
 * fc_fcp_pkt_send() - send a fcp packet to the lower level.
 * @lp:		fc lport
 * @fsp:	fc packet.
 *
 * This is called by upper layer protocol.
 * Return   : zero for success and -1 for failure
 * Context  : called from queuecommand which can be called from process
 *	      or scsi soft irq.
 * Locks    : called with the host lock and irqs disabled.
 */
static int fc_fcp_pkt_send(struct fc_lport *lp, struct fc_fcp_pkt *fsp)
{
	struct fc_fcp_internal *si = fc_get_scsi_internal(lp);
	int rc;

	fsp->cmd->SCp.ptr = (char *)fsp;
	fsp->cdb_cmd.fc_dl = htonl(fsp->data_len);
	fsp->cdb_cmd.fc_flags = fsp->req_flags & ~FCP_CFL_LEN_MASK;

	int_to_scsilun(fsp->cmd->device->lun,
		       (struct scsi_lun *)fsp->cdb_cmd.fc_lun);
	memcpy(fsp->cdb_cmd.fc_cdb, fsp->cmd->cmnd, fsp->cmd->cmd_len);
	list_add_tail(&fsp->list, &si->scsi_pkt_queue);

	spin_unlock_irq(lp->host->host_lock);
	rc = lp->tt.fcp_cmd_send(lp, fsp, fc_fcp_recv);
	spin_lock_irq(lp->host->host_lock);
	if (rc)
		list_del(&fsp->list);

	return rc;
}

static int fc_fcp_cmd_send(struct fc_lport *lp, struct fc_fcp_pkt *fsp,
			   void (*resp)(struct fc_seq *,
					struct fc_frame *fp,
					void *arg))
{
	struct fc_frame *fp;
	struct fc_seq *seq;
	struct fc_rport *rport;
	struct fc_rport_libfc_priv *rp;
	const size_t len = sizeof(fsp->cdb_cmd);
	int rc = 0;

	if (fc_fcp_lock_pkt(fsp))
		return 0;

	fp = fc_frame_alloc(lp, sizeof(fsp->cdb_cmd));
	if (!fp) {
		rc = -1;
		goto unlock;
	}

	memcpy(fc_frame_payload_get(fp, len), &fsp->cdb_cmd, len);
	fr_fsp(fp) = fsp;
	rport = fsp->rport;
	fsp->max_payload = rport->maxframe_size;
	rp = rport->dd_data;

	fc_fill_fc_hdr(fp, FC_RCTL_DD_UNSOL_CMD, rport->port_id,
		       fc_host_port_id(rp->local_port->host), FC_TYPE_FCP,
		       FC_FC_FIRST_SEQ | FC_FC_END_SEQ | FC_FC_SEQ_INIT, 0);

	seq = lp->tt.exch_seq_send(lp, fp, resp, fc_fcp_pkt_destroy, fsp, 0);
	if (!seq) {
		fc_frame_free(fp);
		rc = -1;
		goto unlock;
	}
	fsp->last_pkt_time = jiffies;
	fsp->seq_ptr = seq;
	fc_fcp_pkt_hold(fsp);	/* hold for fc_fcp_pkt_destroy */

	setup_timer(&fsp->timer, fc_fcp_timeout, (unsigned long)fsp);
	fc_fcp_timer_set(fsp,
			 (fsp->tgt_flags & FC_RP_FLAGS_REC_SUPPORTED) ?
			 FC_SCSI_REC_TOV : FC_SCSI_ER_TIMEOUT);
unlock:
	fc_fcp_unlock_pkt(fsp);
	return rc;
}

/*
 * transport error handler
 */
static void fc_fcp_error(struct fc_fcp_pkt *fsp, struct fc_frame *fp)
{
	int error = PTR_ERR(fp);

	if (fc_fcp_lock_pkt(fsp))
		return;

	switch (error) {
	case -FC_EX_CLOSED:
		fc_fcp_retry_cmd(fsp);
		goto unlock;
	default:
		FC_DBG("unknown error %ld\n", PTR_ERR(fp));
	}
	/*
	 * clear abort pending, because the lower layer
	 * decided to force completion.
	 */
	fsp->state &= ~FC_SRB_ABORT_PENDING;
	fsp->status_code = FC_CMD_PLOGO;
	fc_fcp_complete_locked(fsp);
unlock:
	fc_fcp_unlock_pkt(fsp);
}

/*
 * Scsi abort handler- calls to send an abort
 * and then wait for abort completion
 */
static int fc_fcp_pkt_abort(struct fc_lport *lp, struct fc_fcp_pkt *fsp)
{
	int rc = FAILED;

	if (fc_fcp_send_abort(fsp))
		return FAILED;

	init_completion(&fsp->tm_done);
	fsp->wait_for_comp = 1;

	spin_unlock_bh(&fsp->scsi_pkt_lock);
	rc = wait_for_completion_timeout(&fsp->tm_done, FC_SCSI_TM_TOV);
	spin_lock_bh(&fsp->scsi_pkt_lock);
	fsp->wait_for_comp = 0;

	if (!rc) {
		FC_DBG("target abort cmd  failed\n");
		rc = FAILED;
	} else if (fsp->state & FC_SRB_ABORTED) {
		FC_DBG("target abort cmd  passed\n");
		rc = SUCCESS;
		fc_fcp_complete_locked(fsp);
	}

	return rc;
}

/*
 * Retry LUN reset after resource allocation failed.
 */
static void fc_lun_reset_send(unsigned long data)
{
	struct fc_fcp_pkt *fsp = (struct fc_fcp_pkt *)data;
	struct fc_lport *lp = fsp->lp;
	if (lp->tt.fcp_cmd_send(lp, fsp, fc_tm_done)) {
		if (fsp->recov_retry++ >= FC_MAX_RECOV_RETRY)
			return;
		if (fc_fcp_lock_pkt(fsp))
			return;
		setup_timer(&fsp->timer, fc_lun_reset_send, (unsigned long)fsp);
		fc_fcp_timer_set(fsp, FC_SCSI_REC_TOV);
		fc_fcp_unlock_pkt(fsp);
	}
}

/*
 * Scsi device reset handler- send a LUN RESET to the device
 * and wait for reset reply
 */
static int fc_lun_reset(struct fc_lport *lp, struct fc_fcp_pkt *fsp,
			unsigned int id, unsigned int lun)
{
	int rc;

	fsp->cdb_cmd.fc_dl = htonl(fsp->data_len);
	fsp->cdb_cmd.fc_tm_flags = FCP_TMF_LUN_RESET;
	int_to_scsilun(lun, (struct scsi_lun *)fsp->cdb_cmd.fc_lun);

	fsp->wait_for_comp = 1;
	init_completion(&fsp->tm_done);

	fc_lun_reset_send((unsigned long)fsp);

	/*
	 * wait for completion of reset
	 * after that make sure all commands are terminated
	 */
	rc = wait_for_completion_timeout(&fsp->tm_done, FC_SCSI_TM_TOV);

	spin_lock_bh(&fsp->scsi_pkt_lock);
	fsp->state |= FC_SRB_COMPL;
	spin_unlock_bh(&fsp->scsi_pkt_lock);

	del_timer_sync(&fsp->timer);

	spin_lock_bh(&fsp->scsi_pkt_lock);
	if (fsp->seq_ptr) {
		lp->tt.exch_done(fsp->seq_ptr);
		fsp->seq_ptr = NULL;
	}
	fsp->wait_for_comp = 0;
	spin_unlock_bh(&fsp->scsi_pkt_lock);

	if (!rc) {
		FC_DBG("lun reset failed\n");
		return FAILED;
	}

	/* cdb_status holds the tmf's rsp code */
	if (fsp->cdb_status != FCP_TMF_CMPL)
		return FAILED;

	FC_DBG("lun reset to lun %u completed\n", lun);
	fc_fcp_cleanup_each_cmd(lp, id, lun, FC_CMD_ABORTED);
	return SUCCESS;
}

/*
 * Task Managment response handler
 */
static void fc_tm_done(struct fc_seq *seq, struct fc_frame *fp, void *arg)
{
	struct fc_fcp_pkt *fsp = arg;
	struct fc_frame_header *fh;

	if (IS_ERR(fp)) {
		/*
		 * If there is an error just let it timeout or wait
		 * for TMF to be aborted if it timedout.
		 *
		 * scsi-eh will escalate for when either happens.
		 */
		return;
	}

	if (fc_fcp_lock_pkt(fsp))
		return;

	/*
	 * raced with eh timeout handler.
	 */
	if (!fsp->seq_ptr || !fsp->wait_for_comp) {
		spin_unlock_bh(&fsp->scsi_pkt_lock);
		return;
	}

	fh = fc_frame_header_get(fp);
	if (fh->fh_type != FC_TYPE_BLS)
		fc_fcp_resp(fsp, fp);
	fsp->seq_ptr = NULL;
	fsp->lp->tt.exch_done(seq);
	fc_frame_free(fp);
	fc_fcp_unlock_pkt(fsp);
}

static void fc_fcp_cleanup(struct fc_lport *lp)
{
	fc_fcp_cleanup_each_cmd(lp, -1, -1, FC_ERROR);
}

/*
 * fc_fcp_timeout: called by OS timer function.
 *
 * The timer has been inactivated and must be reactivated if desired
 * using fc_fcp_timer_set().
 *
 * Algorithm:
 *
 * If REC is supported, just issue it, and return.  The REC exchange will
 * complete or time out, and recovery can continue at that point.
 *
 * Otherwise, if the response has been received without all the data,
 * it has been ER_TIMEOUT since the response was received.
 *
 * If the response has not been received,
 * we see if data was received recently.  If it has been, we continue waiting,
 * otherwise, we abort the command.
 */
static void fc_fcp_timeout(unsigned long data)
{
	struct fc_fcp_pkt *fsp = (struct fc_fcp_pkt *)data;
	struct fc_rport *rport = fsp->rport;
	struct fc_rport_libfc_priv *rp = rport->dd_data;

	if (fc_fcp_lock_pkt(fsp))
		return;

	if (fsp->cdb_cmd.fc_tm_flags)
		goto unlock;

	fsp->state |= FC_SRB_FCP_PROCESSING_TMO;

	if (rp->flags & FC_RP_FLAGS_REC_SUPPORTED)
		fc_fcp_rec(fsp);
	else if (time_after_eq(fsp->last_pkt_time + (FC_SCSI_ER_TIMEOUT / 2),
			       jiffies))
		fc_fcp_timer_set(fsp, FC_SCSI_ER_TIMEOUT);
	else if (fsp->state & FC_SRB_RCV_STATUS)
		fc_fcp_complete_locked(fsp);
	else
		fc_timeout_error(fsp);
	fsp->state &= ~FC_SRB_FCP_PROCESSING_TMO;
unlock:
	fc_fcp_unlock_pkt(fsp);
}

/*
 * Send a REC ELS request
 */
static void fc_fcp_rec(struct fc_fcp_pkt *fsp)
{
	struct fc_lport *lp;
	struct fc_frame *fp;
	struct fc_rport *rport;
	struct fc_rport_libfc_priv *rp;

	lp = fsp->lp;
	rport = fsp->rport;
	rp = rport->dd_data;
	if (!fsp->seq_ptr || rp->rp_state != RPORT_ST_READY) {
		fsp->status_code = FC_HRD_ERROR;
		fsp->io_status = 0;
		fc_fcp_complete_locked(fsp);
		return;
	}
	fp = fc_frame_alloc(lp, sizeof(struct fc_els_rec));
	if (!fp)
		goto retry;

	fr_seq(fp) = fsp->seq_ptr;
	fc_fill_fc_hdr(fp, FC_RCTL_ELS_REQ, rport->port_id,
		       fc_host_port_id(rp->local_port->host), FC_TYPE_ELS,
		       FC_FC_FIRST_SEQ | FC_FC_END_SEQ | FC_FC_SEQ_INIT, 0);
	if (lp->tt.elsct_send(lp, rport, fp, ELS_REC, fc_fcp_rec_resp,
			      fsp, jiffies_to_msecs(FC_SCSI_REC_TOV))) {
		fc_fcp_pkt_hold(fsp);		/* hold while REC outstanding */
		return;
	}
	fc_frame_free(fp);
retry:
	if (fsp->recov_retry++ < FC_MAX_RECOV_RETRY)
		fc_fcp_timer_set(fsp, FC_SCSI_REC_TOV);
	else
		fc_timeout_error(fsp);
}

/*
 * Receive handler for REC ELS frame
 * if it is a reject then let the scsi layer to handle
 * the timeout. if it is a LS_ACC then if the io was not completed
 * then set the timeout and return otherwise complete the exchange
 * and tell the scsi layer to restart the I/O.
 */
static void fc_fcp_rec_resp(struct fc_seq *seq, struct fc_frame *fp, void *arg)
{
	struct fc_fcp_pkt *fsp = (struct fc_fcp_pkt *)arg;
	struct fc_els_rec_acc *recp;
	struct fc_els_ls_rjt *rjt;
	u32 e_stat;
	u8 opcode;
	u32 offset;
	enum dma_data_direction data_dir;
	enum fc_rctl r_ctl;
	struct fc_rport_libfc_priv *rp;

	if (IS_ERR(fp)) {
		fc_fcp_rec_error(fsp, fp);
		return;
	}

	if (fc_fcp_lock_pkt(fsp))
		goto out;

	fsp->recov_retry = 0;
	opcode = fc_frame_payload_op(fp);
	if (opcode == ELS_LS_RJT) {
		rjt = fc_frame_payload_get(fp, sizeof(*rjt));
		switch (rjt->er_reason) {
		default:
			FC_DEBUG_FCP("device %x unexpected REC reject "
				     "reason %d expl %d\n",
				     fsp->rport->port_id, rjt->er_reason,
				     rjt->er_explan);
			/* fall through */
		case ELS_RJT_UNSUP:
			FC_DEBUG_FCP("device does not support REC\n");
			rp = fsp->rport->dd_data;
			/*
			 * if we do not spport RECs or got some bogus
			 * reason then resetup timer so we check for
			 * making progress.
			 */
			rp->flags &= ~FC_RP_FLAGS_REC_SUPPORTED;
			fc_fcp_timer_set(fsp, FC_SCSI_ER_TIMEOUT);
			break;
		case ELS_RJT_LOGIC:
		case ELS_RJT_UNAB:
			/*
			 * If no data transfer, the command frame got dropped
			 * so we just retry.  If data was transferred, we
			 * lost the response but the target has no record,
			 * so we abort and retry.
			 */
			if (rjt->er_explan == ELS_EXPL_OXID_RXID &&
			    fsp->xfer_len == 0) {
				fc_fcp_retry_cmd(fsp);
				break;
			}
			fc_timeout_error(fsp);
			break;
		}
	} else if (opcode == ELS_LS_ACC) {
		if (fsp->state & FC_SRB_ABORTED)
			goto unlock_out;

		data_dir = fsp->cmd->sc_data_direction;
		recp = fc_frame_payload_get(fp, sizeof(*recp));
		offset = ntohl(recp->reca_fc4value);
		e_stat = ntohl(recp->reca_e_stat);

		if (e_stat & ESB_ST_COMPLETE) {

			/*
			 * The exchange is complete.
			 *
			 * For output, we must've lost the response.
			 * For input, all data must've been sent.
			 * We lost may have lost the response
			 * (and a confirmation was requested) and maybe
			 * some data.
			 *
			 * If all data received, send SRR
			 * asking for response.	 If partial data received,
			 * or gaps, SRR requests data at start of gap.
			 * Recovery via SRR relies on in-order-delivery.
			 */
			if (data_dir == DMA_TO_DEVICE) {
				r_ctl = FC_RCTL_DD_CMD_STATUS;
			} else if (fsp->xfer_contig_end == offset) {
				r_ctl = FC_RCTL_DD_CMD_STATUS;
			} else {
				offset = fsp->xfer_contig_end;
				r_ctl = FC_RCTL_DD_SOL_DATA;
			}
			fc_fcp_srr(fsp, r_ctl, offset);
		} else if (e_stat & ESB_ST_SEQ_INIT) {

			/*
			 * The remote port has the initiative, so just
			 * keep waiting for it to complete.
			 */
			fc_fcp_timer_set(fsp, FC_SCSI_REC_TOV);
		} else {

			/*
			 * The exchange is incomplete, we have seq. initiative.
			 * Lost response with requested confirmation,
			 * lost confirmation, lost transfer ready or
			 * lost write data.
			 *
			 * For output, if not all data was received, ask
			 * for transfer ready to be repeated.
			 *
			 * If we received or sent all the data, send SRR to
			 * request response.
			 *
			 * If we lost a response, we may have lost some read
			 * data as well.
			 */
			r_ctl = FC_RCTL_DD_SOL_DATA;
			if (data_dir == DMA_TO_DEVICE) {
				r_ctl = FC_RCTL_DD_CMD_STATUS;
				if (offset < fsp->data_len)
					r_ctl = FC_RCTL_DD_DATA_DESC;
			} else if (offset == fsp->xfer_contig_end) {
				r_ctl = FC_RCTL_DD_CMD_STATUS;
			} else if (fsp->xfer_contig_end < offset) {
				offset = fsp->xfer_contig_end;
			}
			fc_fcp_srr(fsp, r_ctl, offset);
		}
	}
unlock_out:
	fc_fcp_unlock_pkt(fsp);
out:
	fc_fcp_pkt_release(fsp);	/* drop hold for outstanding REC */
	fc_frame_free(fp);
}

/*
 * Handle error response or timeout for REC exchange.
 */
static void fc_fcp_rec_error(struct fc_fcp_pkt *fsp, struct fc_frame *fp)
{
	int error = PTR_ERR(fp);

	if (fc_fcp_lock_pkt(fsp))
		goto out;

	switch (error) {
	case -FC_EX_CLOSED:
		fc_fcp_retry_cmd(fsp);
		break;

	default:
		FC_DBG("REC %p fid %x error unexpected error %d\n",
		       fsp, fsp->rport->port_id, error);
		fsp->status_code = FC_CMD_PLOGO;
		/* fall through */

	case -FC_EX_TIMEOUT:
		/*
		 * Assume REC or LS_ACC was lost.
		 * The exchange manager will have aborted REC, so retry.
		 */
		FC_DBG("REC fid %x error error %d retry %d/%d\n",
		       fsp->rport->port_id, error, fsp->recov_retry,
		       FC_MAX_RECOV_RETRY);
		if (fsp->recov_retry++ < FC_MAX_RECOV_RETRY)
			fc_fcp_rec(fsp);
		else
			fc_timeout_error(fsp);
		break;
	}
	fc_fcp_unlock_pkt(fsp);
out:
	fc_fcp_pkt_release(fsp);	/* drop hold for outstanding REC */
}

/*
 * Time out error routine:
 * abort's the I/O close the exchange and
 * send completion notification to scsi layer
 */
static void fc_timeout_error(struct fc_fcp_pkt *fsp)
{
	fsp->status_code = FC_CMD_TIME_OUT;
	fsp->cdb_status = 0;
	fsp->io_status = 0;
	/*
	 * if this fails then we let the scsi command timer fire and
	 * scsi-ml escalate.
	 */
	fc_fcp_send_abort(fsp);
}

/*
 * Sequence retransmission request.
 * This is called after receiving status but insufficient data, or
 * when expecting status but the request has timed out.
 */
static void fc_fcp_srr(struct fc_fcp_pkt *fsp, enum fc_rctl r_ctl, u32 offset)
{
	struct fc_lport *lp = fsp->lp;
	struct fc_rport *rport;
	struct fc_rport_libfc_priv *rp;
	struct fc_exch *ep = fc_seq_exch(fsp->seq_ptr);
	struct fc_seq *seq;
	struct fcp_srr *srr;
	struct fc_frame *fp;
	u8 cdb_op;

	rport = fsp->rport;
	rp = rport->dd_data;
	cdb_op = fsp->cdb_cmd.fc_cdb[0];

	if (!(rp->flags & FC_RP_FLAGS_RETRY) || rp->rp_state != RPORT_ST_READY)
		goto retry;			/* shouldn't happen */
	fp = fc_frame_alloc(lp, sizeof(*srr));
	if (!fp)
		goto retry;

	srr = fc_frame_payload_get(fp, sizeof(*srr));
	memset(srr, 0, sizeof(*srr));
	srr->srr_op = ELS_SRR;
	srr->srr_ox_id = htons(ep->oxid);
	srr->srr_rx_id = htons(ep->rxid);
	srr->srr_r_ctl = r_ctl;
	srr->srr_rel_off = htonl(offset);

	fc_fill_fc_hdr(fp, FC_RCTL_ELS4_REQ, rport->port_id,
		       fc_host_port_id(rp->local_port->host), FC_TYPE_FCP,
		       FC_FC_FIRST_SEQ | FC_FC_END_SEQ | FC_FC_SEQ_INIT, 0);

	seq = lp->tt.exch_seq_send(lp, fp, fc_fcp_srr_resp, NULL,
				   fsp, jiffies_to_msecs(FC_SCSI_REC_TOV));
	if (!seq) {
		fc_frame_free(fp);
		goto retry;
	}
	fsp->recov_seq = seq;
	fsp->xfer_len = offset;
	fsp->xfer_contig_end = offset;
	fsp->state &= ~FC_SRB_RCV_STATUS;
	fc_fcp_pkt_hold(fsp);		/* hold for outstanding SRR */
	return;
retry:
	fc_fcp_retry_cmd(fsp);
}

/*
 * Handle response from SRR.
 */
static void fc_fcp_srr_resp(struct fc_seq *seq, struct fc_frame *fp, void *arg)
{
	struct fc_fcp_pkt *fsp = arg;
	struct fc_frame_header *fh;

	if (IS_ERR(fp)) {
		fc_fcp_srr_error(fsp, fp);
		return;
	}

	if (fc_fcp_lock_pkt(fsp))
		goto out;

	fh = fc_frame_header_get(fp);
	/*
	 * BUG? fc_fcp_srr_error calls exch_done which would release
	 * the ep. But if fc_fcp_srr_error had got -FC_EX_TIMEOUT,
	 * then fc_exch_timeout would be sending an abort. The exch_done
	 * call by fc_fcp_srr_error would prevent fc_exch.c from seeing
	 * an abort response though.
	 */
	if (fh->fh_type == FC_TYPE_BLS) {
		fc_fcp_unlock_pkt(fsp);
		return;
	}

	fsp->recov_seq = NULL;
	switch (fc_frame_payload_op(fp)) {
	case ELS_LS_ACC:
		fsp->recov_retry = 0;
		fc_fcp_timer_set(fsp, FC_SCSI_REC_TOV);
		break;
	case ELS_LS_RJT:
	default:
		fc_timeout_error(fsp);
		break;
	}
	fc_fcp_unlock_pkt(fsp);
	fsp->lp->tt.exch_done(seq);
out:
	fc_frame_free(fp);
	fc_fcp_pkt_release(fsp);	/* drop hold for outstanding SRR */
}

static void fc_fcp_srr_error(struct fc_fcp_pkt *fsp, struct fc_frame *fp)
{
	if (fc_fcp_lock_pkt(fsp))
		goto out;
	fsp->lp->tt.exch_done(fsp->recov_seq);
	fsp->recov_seq = NULL;
	switch (PTR_ERR(fp)) {
	case -FC_EX_TIMEOUT:
		if (fsp->recov_retry++ < FC_MAX_RECOV_RETRY)
			fc_fcp_rec(fsp);
		else
			fc_timeout_error(fsp);
		break;
	case -FC_EX_CLOSED:			/* e.g., link failure */
		/* fall through */
	default:
		fc_fcp_retry_cmd(fsp);
		break;
	}
	fc_fcp_unlock_pkt(fsp);
out:
	fc_fcp_pkt_release(fsp);	/* drop hold for outstanding SRR */
}

static inline int fc_fcp_lport_queue_ready(struct fc_lport *lp)
{
	/* lock ? */
	return (lp->state == LPORT_ST_READY) && lp->link_up && !lp->qfull;
}

/**
 * fc_queuecommand - The queuecommand function of the scsi template
 * @cmd:	struct scsi_cmnd to be executed
 * @done:	Callback function to be called when cmd is completed
 *
 * this is the i/o strategy routine, called by the scsi layer
 * this routine is called with holding the host_lock.
 */
int fc_queuecommand(struct scsi_cmnd *sc_cmd, void (*done)(struct scsi_cmnd *))
{
	struct fc_lport *lp;
	struct fc_rport *rport = starget_to_rport(scsi_target(sc_cmd->device));
	struct fc_fcp_pkt *fsp;
	struct fc_rport_libfc_priv *rp;
	int rval;
	int rc = 0;
	struct fcoe_dev_stats *stats;

	lp = shost_priv(sc_cmd->device->host);

	rval = fc_remote_port_chkready(rport);
	if (rval) {
		sc_cmd->result = rval;
		done(sc_cmd);
		goto out;
	}

	if (!*(struct fc_remote_port **)rport->dd_data) {
		/*
		 * rport is transitioning from blocked/deleted to
		 * online
		 */
		sc_cmd->result = DID_IMM_RETRY << 16;
		done(sc_cmd);
		goto out;
	}

	rp = rport->dd_data;

	if (!fc_fcp_lport_queue_ready(lp)) {
		rc = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	}

	fsp = fc_fcp_pkt_alloc(lp, GFP_ATOMIC);
	if (fsp == NULL) {
		rc = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	}

	/*
	 * build the libfc request pkt
	 */
	fsp->cmd = sc_cmd;	/* save the cmd */
	fsp->lp = lp;		/* save the softc ptr */
	fsp->rport = rport;	/* set the remote port ptr */
	sc_cmd->scsi_done = done;

	/*
	 * set up the transfer length
	 */
	fsp->data_len = scsi_bufflen(sc_cmd);
	fsp->xfer_len = 0;

	/*
	 * setup the data direction
	 */
	stats = fc_lport_get_stats(lp);
	if (sc_cmd->sc_data_direction == DMA_FROM_DEVICE) {
		fsp->req_flags = FC_SRB_READ;
		stats->InputRequests++;
		stats->InputMegabytes = fsp->data_len;
	} else if (sc_cmd->sc_data_direction == DMA_TO_DEVICE) {
		fsp->req_flags = FC_SRB_WRITE;
		stats->OutputRequests++;
		stats->OutputMegabytes = fsp->data_len;
	} else {
		fsp->req_flags = 0;
		stats->ControlRequests++;
	}

	fsp->tgt_flags = rp->flags;

	init_timer(&fsp->timer);
	fsp->timer.data = (unsigned long)fsp;

	/*
	 * send it to the lower layer
	 * if we get -1 return then put the request in the pending
	 * queue.
	 */
	rval = fc_fcp_pkt_send(lp, fsp);
	if (rval != 0) {
		fsp->state = FC_SRB_FREE;
		fc_fcp_pkt_release(fsp);
		rc = SCSI_MLQUEUE_HOST_BUSY;
	}
out:
	return rc;
}
EXPORT_SYMBOL(fc_queuecommand);

/**
 * fc_io_compl() -  Handle responses for completed commands
 * @fsp:	scsi packet
 *
 * Translates a error to a Linux SCSI error.
 *
 * The fcp packet lock must be held when calling.
 */
static void fc_io_compl(struct fc_fcp_pkt *fsp)
{
	struct fc_fcp_internal *si;
	struct scsi_cmnd *sc_cmd;
	struct fc_lport *lp;
	unsigned long flags;

	/* release outstanding ddp context */
	fc_fcp_ddp_done(fsp);

	fsp->state |= FC_SRB_COMPL;
	if (!(fsp->state & FC_SRB_FCP_PROCESSING_TMO)) {
		spin_unlock_bh(&fsp->scsi_pkt_lock);
		del_timer_sync(&fsp->timer);
		spin_lock_bh(&fsp->scsi_pkt_lock);
	}

	lp = fsp->lp;
	si = fc_get_scsi_internal(lp);
	spin_lock_irqsave(lp->host->host_lock, flags);
	if (!fsp->cmd) {
		spin_unlock_irqrestore(lp->host->host_lock, flags);
		return;
	}

	/*
	 * if a command timed out while we had to try and throttle IO
	 * and it is now getting cleaned up, then we are about to
	 * try again so clear the throttled flag incase we get more
	 * time outs.
	 */
	if (si->throttled && fsp->state & FC_SRB_NOMEM)
		si->throttled = 0;

	sc_cmd = fsp->cmd;
	fsp->cmd = NULL;

	if (!sc_cmd->SCp.ptr) {
		spin_unlock_irqrestore(lp->host->host_lock, flags);
		return;
	}

	CMD_SCSI_STATUS(sc_cmd) = fsp->cdb_status;
	switch (fsp->status_code) {
	case FC_COMPLETE:
		if (fsp->cdb_status == 0) {
			/*
			 * good I/O status
			 */
			sc_cmd->result = DID_OK << 16;
			if (fsp->scsi_resid)
				CMD_RESID_LEN(sc_cmd) = fsp->scsi_resid;
		} else if (fsp->cdb_status == QUEUE_FULL) {
			struct scsi_device *tmp_sdev;
			struct scsi_device *sdev = sc_cmd->device;

			shost_for_each_device(tmp_sdev, sdev->host) {
				if (tmp_sdev->id != sdev->id)
					continue;

				if (tmp_sdev->queue_depth > 1) {
					scsi_track_queue_full(tmp_sdev,
							      tmp_sdev->
							      queue_depth - 1);
				}
			}
			sc_cmd->result = (DID_OK << 16) | fsp->cdb_status;
		} else {
			/*
			 * transport level I/O was ok but scsi
			 * has non zero status
			 */
			sc_cmd->result = (DID_OK << 16) | fsp->cdb_status;
		}
		break;
	case FC_ERROR:
		sc_cmd->result = DID_ERROR << 16;
		break;
	case FC_DATA_UNDRUN:
		if ((fsp->cdb_status == 0) && !(fsp->req_flags & FC_SRB_READ)) {
			/*
			 * scsi status is good but transport level
			 * underrun.
			 */
			sc_cmd->result = DID_OK << 16;
		} else {
			/*
			 * scsi got underrun, this is an error
			 */
			CMD_RESID_LEN(sc_cmd) = fsp->scsi_resid;
			sc_cmd->result = (DID_ERROR << 16) | fsp->cdb_status;
		}
		break;
	case FC_DATA_OVRRUN:
		/*
		 * overrun is an error
		 */
		sc_cmd->result = (DID_ERROR << 16) | fsp->cdb_status;
		break;
	case FC_CMD_ABORTED:
		sc_cmd->result = (DID_ABORT << 16) | fsp->io_status;
		break;
	case FC_CMD_TIME_OUT:
		sc_cmd->result = (DID_BUS_BUSY << 16) | fsp->io_status;
		break;
	case FC_CMD_RESET:
		sc_cmd->result = (DID_RESET << 16);
		break;
	case FC_HRD_ERROR:
		sc_cmd->result = (DID_NO_CONNECT << 16);
		break;
	default:
		sc_cmd->result = (DID_ERROR << 16);
		break;
	}

	list_del(&fsp->list);
	sc_cmd->SCp.ptr = NULL;
	sc_cmd->scsi_done(sc_cmd);
	spin_unlock_irqrestore(lp->host->host_lock, flags);

	/* release ref from initial allocation in queue command */
	fc_fcp_pkt_release(fsp);
}

/**
 * fc_fcp_complete() - complete processing of a fcp packet
 * @fsp:	fcp packet
 *
 * This function may sleep if a fsp timer is pending.
 * The host lock must not be held by caller.
 */
void fc_fcp_complete(struct fc_fcp_pkt *fsp)
{
	if (fc_fcp_lock_pkt(fsp))
		return;

	fc_fcp_complete_locked(fsp);
	fc_fcp_unlock_pkt(fsp);
}
EXPORT_SYMBOL(fc_fcp_complete);

/**
 * fc_eh_abort() - Abort a command
 * @sc_cmd:	scsi command to abort
 *
 * From scsi host template.
 * send ABTS to the target device  and wait for the response
 * sc_cmd is the pointer to the command to be aborted.
 */
int fc_eh_abort(struct scsi_cmnd *sc_cmd)
{
	struct fc_fcp_pkt *fsp;
	struct fc_lport *lp;
	int rc = FAILED;
	unsigned long flags;

	lp = shost_priv(sc_cmd->device->host);
	if (lp->state != LPORT_ST_READY)
		return rc;
	else if (!lp->link_up)
		return rc;

	spin_lock_irqsave(lp->host->host_lock, flags);
	fsp = CMD_SP(sc_cmd);
	if (!fsp) {
		/* command completed while scsi eh was setting up */
		spin_unlock_irqrestore(lp->host->host_lock, flags);
		return SUCCESS;
	}
	/* grab a ref so the fsp and sc_cmd cannot be relased from under us */
	fc_fcp_pkt_hold(fsp);
	spin_unlock_irqrestore(lp->host->host_lock, flags);

	if (fc_fcp_lock_pkt(fsp)) {
		/* completed while we were waiting for timer to be deleted */
		rc = SUCCESS;
		goto release_pkt;
	}

	rc = fc_fcp_pkt_abort(lp, fsp);
	fc_fcp_unlock_pkt(fsp);

release_pkt:
	fc_fcp_pkt_release(fsp);
	return rc;
}
EXPORT_SYMBOL(fc_eh_abort);

/**
 * fc_eh_device_reset() Reset a single LUN
 * @sc_cmd:	scsi command
 *
 * Set from scsi host template to send tm cmd to the target and wait for the
 * response.
 */
int fc_eh_device_reset(struct scsi_cmnd *sc_cmd)
{
	struct fc_lport *lp;
	struct fc_fcp_pkt *fsp;
	struct fc_rport *rport = starget_to_rport(scsi_target(sc_cmd->device));
	int rc = FAILED;
	struct fc_rport_libfc_priv *rp;
	int rval;

	rval = fc_remote_port_chkready(rport);
	if (rval)
		goto out;

	rp = rport->dd_data;
	lp = shost_priv(sc_cmd->device->host);

	if (lp->state != LPORT_ST_READY)
		return rc;

	fsp = fc_fcp_pkt_alloc(lp, GFP_NOIO);
	if (fsp == NULL) {
		FC_DBG("could not allocate scsi_pkt\n");
		sc_cmd->result = DID_NO_CONNECT << 16;
		goto out;
	}

	/*
	 * Build the libfc request pkt. Do not set the scsi cmnd, because
	 * the sc passed in is not setup for execution like when sent
	 * through the queuecommand callout.
	 */
	fsp->lp = lp;		/* save the softc ptr */
	fsp->rport = rport;	/* set the remote port ptr */

	/*
	 * flush outstanding commands
	 */
	rc = fc_lun_reset(lp, fsp, scmd_id(sc_cmd), sc_cmd->device->lun);
	fsp->state = FC_SRB_FREE;
	fc_fcp_pkt_release(fsp);

out:
	return rc;
}
EXPORT_SYMBOL(fc_eh_device_reset);

/**
 * fc_eh_host_reset() - The reset function will reset the ports on the host.
 * @sc_cmd:	scsi command
 */
int fc_eh_host_reset(struct scsi_cmnd *sc_cmd)
{
	struct Scsi_Host *shost = sc_cmd->device->host;
	struct fc_lport *lp = shost_priv(shost);
	unsigned long wait_tmo;

	lp->tt.lport_reset(lp);
	wait_tmo = jiffies + FC_HOST_RESET_TIMEOUT;
	while (!fc_fcp_lport_queue_ready(lp) && time_before(jiffies, wait_tmo))
		msleep(1000);

	if (fc_fcp_lport_queue_ready(lp)) {
		shost_printk(KERN_INFO, shost, "Host reset succeeded.\n");
		return SUCCESS;
	} else {
		shost_printk(KERN_INFO, shost, "Host reset failed. "
			     "lport not ready.\n");
		return FAILED;
	}
}
EXPORT_SYMBOL(fc_eh_host_reset);

/**
 * fc_slave_alloc() - configure queue depth
 * @sdev:	scsi device
 *
 * Configures queue depth based on host's cmd_per_len. If not set
 * then we use the libfc default.
 */
int fc_slave_alloc(struct scsi_device *sdev)
{
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));
	int queue_depth;

	if (!rport || fc_remote_port_chkready(rport))
		return -ENXIO;

	if (sdev->tagged_supported) {
		if (sdev->host->hostt->cmd_per_lun)
			queue_depth = sdev->host->hostt->cmd_per_lun;
		else
			queue_depth = FC_FCP_DFLT_QUEUE_DEPTH;
		scsi_activate_tcq(sdev, queue_depth);
	}
	return 0;
}
EXPORT_SYMBOL(fc_slave_alloc);

int fc_change_queue_depth(struct scsi_device *sdev, int qdepth)
{
	scsi_adjust_queue_depth(sdev, scsi_get_tag_type(sdev), qdepth);
	return sdev->queue_depth;
}
EXPORT_SYMBOL(fc_change_queue_depth);

int fc_change_queue_type(struct scsi_device *sdev, int tag_type)
{
	if (sdev->tagged_supported) {
		scsi_set_tag_type(sdev, tag_type);
		if (tag_type)
			scsi_activate_tcq(sdev, sdev->queue_depth);
		else
			scsi_deactivate_tcq(sdev, sdev->queue_depth);
	} else
		tag_type = 0;

	return tag_type;
}
EXPORT_SYMBOL(fc_change_queue_type);

void fc_fcp_destroy(struct fc_lport *lp)
{
	struct fc_fcp_internal *si = fc_get_scsi_internal(lp);

	if (!list_empty(&si->scsi_pkt_queue))
		printk(KERN_ERR "Leaked scsi packets.\n");

	mempool_destroy(si->scsi_pkt_pool);
	kfree(si);
	lp->scsi_priv = NULL;
}
EXPORT_SYMBOL(fc_fcp_destroy);

int fc_fcp_init(struct fc_lport *lp)
{
	int rc;
	struct fc_fcp_internal *si;

	if (!lp->tt.fcp_cmd_send)
		lp->tt.fcp_cmd_send = fc_fcp_cmd_send;

	if (!lp->tt.fcp_cleanup)
		lp->tt.fcp_cleanup = fc_fcp_cleanup;

	if (!lp->tt.fcp_abort_io)
		lp->tt.fcp_abort_io = fc_fcp_abort_io;

	si = kzalloc(sizeof(struct fc_fcp_internal), GFP_KERNEL);
	if (!si)
		return -ENOMEM;
	lp->scsi_priv = si;
	INIT_LIST_HEAD(&si->scsi_pkt_queue);

	si->scsi_pkt_pool = mempool_create_slab_pool(2, scsi_pkt_cachep);
	if (!si->scsi_pkt_pool) {
		rc = -ENOMEM;
		goto free_internal;
	}
	return 0;

free_internal:
	kfree(si);
	return rc;
}
EXPORT_SYMBOL(fc_fcp_init);

static int __init libfc_init(void)
{
	int rc;

	scsi_pkt_cachep = kmem_cache_create("libfc_fcp_pkt",
					    sizeof(struct fc_fcp_pkt),
					    0, SLAB_HWCACHE_ALIGN, NULL);
	if (scsi_pkt_cachep == NULL) {
		FC_DBG("Unable to allocate SRB cache...module load failed!");
		return -ENOMEM;
	}

	rc = fc_setup_exch_mgr();
	if (rc)
		goto destroy_pkt_cache;

	rc = fc_setup_rport();
	if (rc)
		goto destroy_em;

	return rc;
destroy_em:
	fc_destroy_exch_mgr();
destroy_pkt_cache:
	kmem_cache_destroy(scsi_pkt_cachep);
	return rc;
}

static void __exit libfc_exit(void)
{
	kmem_cache_destroy(scsi_pkt_cachep);
	fc_destroy_exch_mgr();
	fc_destroy_rport();
}

module_init(libfc_init);
module_exit(libfc_exit);
