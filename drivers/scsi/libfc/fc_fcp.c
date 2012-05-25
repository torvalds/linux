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
#include <linux/slab.h>

#include <scsi/scsi_tcq.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>

#include <scsi/fc/fc_fc2.h>

#include <scsi/libfc.h>
#include <scsi/fc_encode.h>

#include "fc_libfc.h"

static struct kmem_cache *scsi_pkt_cachep;

/* SRB state definitions */
#define FC_SRB_FREE		0		/* cmd is free */
#define FC_SRB_CMD_SENT		(1 << 0)	/* cmd has been sent */
#define FC_SRB_RCV_STATUS	(1 << 1)	/* response has arrived */
#define FC_SRB_ABORT_PENDING	(1 << 2)	/* cmd abort sent to device */
#define FC_SRB_ABORTED		(1 << 3)	/* abort acknowledged */
#define FC_SRB_DISCONTIG	(1 << 4)	/* non-sequential data recvd */
#define FC_SRB_COMPL		(1 << 5)	/* fc_io_compl has been run */
#define FC_SRB_FCP_PROCESSING_TMO (1 << 6)	/* timer function processing */

#define FC_SRB_READ		(1 << 1)
#define FC_SRB_WRITE		(1 << 0)

/*
 * The SCp.ptr should be tested and set under the scsi_pkt_queue lock
 */
#define CMD_SP(Cmnd)		    ((struct fc_fcp_pkt *)(Cmnd)->SCp.ptr)
#define CMD_ENTRY_STATUS(Cmnd)	    ((Cmnd)->SCp.have_data_in)
#define CMD_COMPL_STATUS(Cmnd)	    ((Cmnd)->SCp.this_residual)
#define CMD_SCSI_STATUS(Cmnd)	    ((Cmnd)->SCp.Status)
#define CMD_RESID_LEN(Cmnd)	    ((Cmnd)->SCp.buffers_residual)

/**
 * struct fc_fcp_internal - FCP layer internal data
 * @scsi_pkt_pool: Memory pool to draw FCP packets from
 * @scsi_queue_lock: Protects the scsi_pkt_queue
 * @scsi_pkt_queue: Current FCP packets
 * @last_can_queue_ramp_down_time: ramp down time
 * @last_can_queue_ramp_up_time: ramp up time
 * @max_can_queue: max can_queue size
 */
struct fc_fcp_internal {
	mempool_t		*scsi_pkt_pool;
	spinlock_t		scsi_queue_lock;
	struct list_head	scsi_pkt_queue;
	unsigned long		last_can_queue_ramp_down_time;
	unsigned long		last_can_queue_ramp_up_time;
	int			max_can_queue;
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
static void fc_fcp_error(struct fc_fcp_pkt *, struct fc_frame *);
static void fc_fcp_recovery(struct fc_fcp_pkt *, u8 code);
static void fc_fcp_timeout(unsigned long);
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
#define FC_CRC_ERROR		10
#define FC_TIMED_OUT		11

/*
 * Error recovery timeout values.
 */
#define FC_SCSI_TM_TOV		(10 * HZ)
#define FC_HOST_RESET_TIMEOUT	(30 * HZ)
#define FC_CAN_QUEUE_PERIOD	(60 * HZ)

#define FC_MAX_ERROR_CNT	5
#define FC_MAX_RECOV_RETRY	3

#define FC_FCP_DFLT_QUEUE_DEPTH 32

/**
 * fc_fcp_pkt_alloc() - Allocate a fcp_pkt
 * @lport: The local port that the FCP packet is for
 * @gfp:   GFP flags for allocation
 *
 * Return value: fcp_pkt structure or null on allocation failure.
 * Context:	 Can be called from process context, no lock is required.
 */
static struct fc_fcp_pkt *fc_fcp_pkt_alloc(struct fc_lport *lport, gfp_t gfp)
{
	struct fc_fcp_internal *si = fc_get_scsi_internal(lport);
	struct fc_fcp_pkt *fsp;

	fsp = mempool_alloc(si->scsi_pkt_pool, gfp);
	if (fsp) {
		memset(fsp, 0, sizeof(*fsp));
		fsp->lp = lport;
		fsp->xfer_ddp = FC_XID_UNKNOWN;
		atomic_set(&fsp->ref_cnt, 1);
		init_timer(&fsp->timer);
		fsp->timer.data = (unsigned long)fsp;
		INIT_LIST_HEAD(&fsp->list);
		spin_lock_init(&fsp->scsi_pkt_lock);
	} else {
		per_cpu_ptr(lport->stats, get_cpu())->FcpPktAllocFails++;
		put_cpu();
	}
	return fsp;
}

/**
 * fc_fcp_pkt_release() - Release hold on a fcp_pkt
 * @fsp: The FCP packet to be released
 *
 * Context: Can be called from process or interrupt context,
 *	    no lock is required.
 */
static void fc_fcp_pkt_release(struct fc_fcp_pkt *fsp)
{
	if (atomic_dec_and_test(&fsp->ref_cnt)) {
		struct fc_fcp_internal *si = fc_get_scsi_internal(fsp->lp);

		mempool_free(fsp, si->scsi_pkt_pool);
	}
}

/**
 * fc_fcp_pkt_hold() - Hold a fcp_pkt
 * @fsp: The FCP packet to be held
 */
static void fc_fcp_pkt_hold(struct fc_fcp_pkt *fsp)
{
	atomic_inc(&fsp->ref_cnt);
}

/**
 * fc_fcp_pkt_destory() - Release hold on a fcp_pkt
 * @seq: The sequence that the FCP packet is on (required by destructor API)
 * @fsp: The FCP packet to be released
 *
 * This routine is called by a destructor callback in the exch_seq_send()
 * routine of the libfc Transport Template. The 'struct fc_seq' is a required
 * argument even though it is not used by this routine.
 *
 * Context: No locking required.
 */
static void fc_fcp_pkt_destroy(struct fc_seq *seq, void *fsp)
{
	fc_fcp_pkt_release(fsp);
}

/**
 * fc_fcp_lock_pkt() - Lock a fcp_pkt and increase its reference count
 * @fsp: The FCP packet to be locked and incremented
 *
 * We should only return error if we return a command to SCSI-ml before
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

/**
 * fc_fcp_unlock_pkt() - Release a fcp_pkt's lock and decrement its
 *			 reference count
 * @fsp: The FCP packet to be unlocked and decremented
 */
static inline void fc_fcp_unlock_pkt(struct fc_fcp_pkt *fsp)
{
	spin_unlock_bh(&fsp->scsi_pkt_lock);
	fc_fcp_pkt_release(fsp);
}

/**
 * fc_fcp_timer_set() - Start a timer for a fcp_pkt
 * @fsp:   The FCP packet to start a timer for
 * @delay: The timeout period in jiffies
 */
static void fc_fcp_timer_set(struct fc_fcp_pkt *fsp, unsigned long delay)
{
	if (!(fsp->state & FC_SRB_COMPL))
		mod_timer(&fsp->timer, jiffies + delay);
}

/**
 * fc_fcp_send_abort() - Send an abort for exchanges associated with a
 *			 fcp_pkt
 * @fsp: The FCP packet to abort exchanges on
 */
static int fc_fcp_send_abort(struct fc_fcp_pkt *fsp)
{
	if (!fsp->seq_ptr)
		return -EINVAL;

	per_cpu_ptr(fsp->lp->stats, get_cpu())->FcpPktAborts++;
	put_cpu();

	fsp->state |= FC_SRB_ABORT_PENDING;
	return fsp->lp->tt.seq_exch_abort(fsp->seq_ptr, 0);
}

/**
 * fc_fcp_retry_cmd() - Retry a fcp_pkt
 * @fsp: The FCP packet to be retried
 *
 * Sets the status code to be FC_ERROR and then calls
 * fc_fcp_complete_locked() which in turn calls fc_io_compl().
 * fc_io_compl() will notify the SCSI-ml that the I/O is done.
 * The SCSI-ml will retry the command.
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

/**
 * fc_fcp_ddp_setup() - Calls a LLD's ddp_setup routine to set up DDP context
 * @fsp: The FCP packet that will manage the DDP frames
 * @xid: The XID that will be used for the DDP exchange
 */
void fc_fcp_ddp_setup(struct fc_fcp_pkt *fsp, u16 xid)
{
	struct fc_lport *lport;

	lport = fsp->lp;
	if ((fsp->req_flags & FC_SRB_READ) &&
	    (lport->lro_enabled) && (lport->tt.ddp_setup)) {
		if (lport->tt.ddp_setup(lport, xid, scsi_sglist(fsp->cmd),
					scsi_sg_count(fsp->cmd)))
			fsp->xfer_ddp = xid;
	}
}

/**
 * fc_fcp_ddp_done() - Calls a LLD's ddp_done routine to release any
 *		       DDP related resources for a fcp_pkt
 * @fsp: The FCP packet that DDP had been used on
 */
void fc_fcp_ddp_done(struct fc_fcp_pkt *fsp)
{
	struct fc_lport *lport;

	if (!fsp)
		return;

	if (fsp->xfer_ddp == FC_XID_UNKNOWN)
		return;

	lport = fsp->lp;
	if (lport->tt.ddp_done) {
		fsp->xfer_len = lport->tt.ddp_done(lport, fsp->xfer_ddp);
		fsp->xfer_ddp = FC_XID_UNKNOWN;
	}
}

/**
 * fc_fcp_can_queue_ramp_up() - increases can_queue
 * @lport: lport to ramp up can_queue
 */
static void fc_fcp_can_queue_ramp_up(struct fc_lport *lport)
{
	struct fc_fcp_internal *si = fc_get_scsi_internal(lport);
	unsigned long flags;
	int can_queue;

	spin_lock_irqsave(lport->host->host_lock, flags);

	if (si->last_can_queue_ramp_up_time &&
	    (time_before(jiffies, si->last_can_queue_ramp_up_time +
			 FC_CAN_QUEUE_PERIOD)))
		goto unlock;

	if (time_before(jiffies, si->last_can_queue_ramp_down_time +
			FC_CAN_QUEUE_PERIOD))
		goto unlock;

	si->last_can_queue_ramp_up_time = jiffies;

	can_queue = lport->host->can_queue << 1;
	if (can_queue >= si->max_can_queue) {
		can_queue = si->max_can_queue;
		si->last_can_queue_ramp_down_time = 0;
	}
	lport->host->can_queue = can_queue;
	shost_printk(KERN_ERR, lport->host, "libfc: increased "
		     "can_queue to %d.\n", can_queue);

unlock:
	spin_unlock_irqrestore(lport->host->host_lock, flags);
}

/**
 * fc_fcp_can_queue_ramp_down() - reduces can_queue
 * @lport: lport to reduce can_queue
 *
 * If we are getting memory allocation failures, then we may
 * be trying to execute too many commands. We let the running
 * commands complete or timeout, then try again with a reduced
 * can_queue. Eventually we will hit the point where we run
 * on all reserved structs.
 */
static void fc_fcp_can_queue_ramp_down(struct fc_lport *lport)
{
	struct fc_fcp_internal *si = fc_get_scsi_internal(lport);
	unsigned long flags;
	int can_queue;

	spin_lock_irqsave(lport->host->host_lock, flags);

	if (si->last_can_queue_ramp_down_time &&
	    (time_before(jiffies, si->last_can_queue_ramp_down_time +
			 FC_CAN_QUEUE_PERIOD)))
		goto unlock;

	si->last_can_queue_ramp_down_time = jiffies;

	can_queue = lport->host->can_queue;
	can_queue >>= 1;
	if (!can_queue)
		can_queue = 1;
	lport->host->can_queue = can_queue;
	shost_printk(KERN_ERR, lport->host, "libfc: Could not allocate frame.\n"
		     "Reducing can_queue to %d.\n", can_queue);

unlock:
	spin_unlock_irqrestore(lport->host->host_lock, flags);
}

/*
 * fc_fcp_frame_alloc() -  Allocates fc_frame structure and buffer.
 * @lport:	fc lport struct
 * @len:	payload length
 *
 * Allocates fc_frame structure and buffer but if fails to allocate
 * then reduce can_queue.
 */
static inline struct fc_frame *fc_fcp_frame_alloc(struct fc_lport *lport,
						  size_t len)
{
	struct fc_frame *fp;

	fp = fc_frame_alloc(lport, len);
	if (likely(fp))
		return fp;

	per_cpu_ptr(lport->stats, get_cpu())->FcpFrameAllocFails++;
	put_cpu();
	/* error case */
	fc_fcp_can_queue_ramp_down(lport);
	return NULL;
}

/**
 * fc_fcp_recv_data() - Handler for receiving SCSI-FCP data from a target
 * @fsp: The FCP packet the data is on
 * @fp:	 The data frame
 */
static void fc_fcp_recv_data(struct fc_fcp_pkt *fsp, struct fc_frame *fp)
{
	struct scsi_cmnd *sc = fsp->cmd;
	struct fc_lport *lport = fsp->lp;
	struct fc_stats *stats;
	struct fc_frame_header *fh;
	size_t start_offset;
	size_t offset;
	u32 crc;
	u32 copy_len = 0;
	size_t len;
	void *buf;
	struct scatterlist *sg;
	u32 nents;
	u8 host_bcode = FC_COMPLETE;

	fh = fc_frame_header_get(fp);
	offset = ntohl(fh->fh_parm_offset);
	start_offset = offset;
	len = fr_len(fp) - sizeof(*fh);
	buf = fc_frame_payload_get(fp, 0);

	/*
	 * if this I/O is ddped then clear it and initiate recovery since data
	 * frames are expected to be placed directly in that case.
	 *
	 * Indicate error to scsi-ml because something went wrong with the
	 * ddp handling to get us here.
	 */
	if (fsp->xfer_ddp != FC_XID_UNKNOWN) {
		fc_fcp_ddp_done(fsp);
		FC_FCP_DBG(fsp, "DDP I/O in fc_fcp_recv_data set ERROR\n");
		host_bcode = FC_ERROR;
		goto err;
	}
	if (offset + len > fsp->data_len) {
		/* this should never happen */
		if ((fr_flags(fp) & FCPHF_CRC_UNCHECKED) &&
		    fc_frame_crc_check(fp))
			goto crc_err;
		FC_FCP_DBG(fsp, "data received past end. len %zx offset %zx "
			   "data_len %x\n", len, offset, fsp->data_len);

		/* Data is corrupted indicate scsi-ml should retry */
		host_bcode = FC_DATA_OVRRUN;
		goto err;
	}
	if (offset != fsp->xfer_len)
		fsp->state |= FC_SRB_DISCONTIG;

	sg = scsi_sglist(sc);
	nents = scsi_sg_count(sc);

	if (!(fr_flags(fp) & FCPHF_CRC_UNCHECKED)) {
		copy_len = fc_copy_buffer_to_sglist(buf, len, sg, &nents,
						    &offset, NULL);
	} else {
		crc = crc32(~0, (u8 *) fh, sizeof(*fh));
		copy_len = fc_copy_buffer_to_sglist(buf, len, sg, &nents,
						    &offset, &crc);
		buf = fc_frame_payload_get(fp, 0);
		if (len % 4)
			crc = crc32(crc, buf + len, 4 - (len % 4));

		if (~crc != le32_to_cpu(fr_crc(fp))) {
crc_err:
			stats = per_cpu_ptr(lport->stats, get_cpu());
			stats->ErrorFrames++;
			/* per cpu count, not total count, but OK for limit */
			if (stats->InvalidCRCCount++ < FC_MAX_ERROR_CNT)
				printk(KERN_WARNING "libfc: CRC error on data "
				       "frame for port (%6.6x)\n",
				       lport->port_id);
			put_cpu();
			/*
			 * Assume the frame is total garbage.
			 * We may have copied it over the good part
			 * of the buffer.
			 * If so, we need to retry the entire operation.
			 * Otherwise, ignore it.
			 */
			if (fsp->state & FC_SRB_DISCONTIG) {
				host_bcode = FC_CRC_ERROR;
				goto err;
			}
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
	return;
err:
	fc_fcp_recovery(fsp, host_bcode);
}

/**
 * fc_fcp_send_data() - Send SCSI data to a target
 * @fsp:      The FCP packet the data is on
 * @sp:	      The sequence the data is to be sent on
 * @offset:   The starting offset for this data request
 * @seq_blen: The burst length for this data request
 *
 * Called after receiving a Transfer Ready data descriptor.
 * If the LLD is capable of sequence offload then send down the
 * seq_blen amount of data in single frame, otherwise send
 * multiple frames of the maximum frame payload supported by
 * the target port.
 */
static int fc_fcp_send_data(struct fc_fcp_pkt *fsp, struct fc_seq *seq,
			    size_t offset, size_t seq_blen)
{
	struct fc_exch *ep;
	struct scsi_cmnd *sc;
	struct scatterlist *sg;
	struct fc_frame *fp = NULL;
	struct fc_lport *lport = fsp->lp;
	struct page *page;
	size_t remaining;
	size_t t_blen;
	size_t tlen;
	size_t sg_bytes;
	size_t frame_offset, fh_parm_offset;
	size_t off;
	int error;
	void *data = NULL;
	void *page_addr;
	int using_sg = lport->sg_supp;
	u32 f_ctl;

	WARN_ON(seq_blen <= 0);
	if (unlikely(offset + seq_blen > fsp->data_len)) {
		/* this should never happen */
		FC_FCP_DBG(fsp, "xfer-ready past end. seq_blen %zx "
			   "offset %zx\n", seq_blen, offset);
		fc_fcp_send_abort(fsp);
		return 0;
	} else if (offset != fsp->xfer_len) {
		/* Out of Order Data Request - no problem, but unexpected. */
		FC_FCP_DBG(fsp, "xfer-ready non-contiguous. "
			   "seq_blen %zx offset %zx\n", seq_blen, offset);
	}

	/*
	 * if LLD is capable of seq_offload then set transport
	 * burst length (t_blen) to seq_blen, otherwise set t_blen
	 * to max FC frame payload previously set in fsp->max_payload.
	 */
	t_blen = fsp->max_payload;
	if (lport->seq_offload) {
		t_blen = min(seq_blen, (size_t)lport->lso_max);
		FC_FCP_DBG(fsp, "fsp=%p:lso:blen=%zx lso_max=0x%x t_blen=%zx\n",
			   fsp, seq_blen, lport->lso_max, t_blen);
	}

	if (t_blen > 512)
		t_blen &= ~(512 - 1);	/* round down to block size */
	sc = fsp->cmd;

	remaining = seq_blen;
	fh_parm_offset = frame_offset = offset;
	tlen = 0;
	seq = lport->tt.seq_start_next(seq);
	f_ctl = FC_FC_REL_OFF;
	WARN_ON(!seq);

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
			fp = fc_frame_alloc(lport, using_sg ? 0 : tlen);
			if (!fp)
				return -ENOMEM;

			data = fc_frame_header_get(fp) + 1;
			fh_parm_offset = frame_offset;
			fr_max_payload(fp) = fsp->max_payload;
		}

		off = offset + sg->offset;
		sg_bytes = min(tlen, sg->length - offset);
		sg_bytes = min(sg_bytes,
			       (size_t) (PAGE_SIZE - (off & ~PAGE_MASK)));
		page = sg_page(sg) + (off >> PAGE_SHIFT);
		if (using_sg) {
			get_page(page);
			skb_fill_page_desc(fp_skb(fp),
					   skb_shinfo(fp_skb(fp))->nr_frags,
					   page, off & ~PAGE_MASK, sg_bytes);
			fp_skb(fp)->data_len += sg_bytes;
			fr_len(fp) += sg_bytes;
			fp_skb(fp)->truesize += PAGE_SIZE;
		} else {
			/*
			 * The scatterlist item may be bigger than PAGE_SIZE,
			 * but we must not cross pages inside the kmap.
			 */
			page_addr = kmap_atomic(page);
			memcpy(data, (char *)page_addr + (off & ~PAGE_MASK),
			       sg_bytes);
			kunmap_atomic(page_addr);
			data += sg_bytes;
		}
		offset += sg_bytes;
		frame_offset += sg_bytes;
		tlen -= sg_bytes;
		remaining -= sg_bytes;

		if ((skb_shinfo(fp_skb(fp))->nr_frags < FC_FRAME_SG_LEN) &&
		    (tlen))
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
		error = lport->tt.seq_send(lport, seq, fp);
		if (error) {
			WARN_ON(1);		/* send error should be rare */
			return error;
		}
		fp = NULL;
	}
	fsp->xfer_len += seq_blen;	/* premature count? */
	return 0;
}

/**
 * fc_fcp_abts_resp() - Receive an ABTS response
 * @fsp: The FCP packet that is being aborted
 * @fp:	 The response frame
 */
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
 * fc_fcp_recv() - Receive an FCP frame
 * @seq: The sequence the frame is on
 * @fp:	 The received frame
 * @arg: The related FCP packet
 *
 * Context: Called from Soft IRQ context. Can not be called
 *	    holding the FCP packet list lock.
 */
static void fc_fcp_recv(struct fc_seq *seq, struct fc_frame *fp, void *arg)
{
	struct fc_fcp_pkt *fsp = (struct fc_fcp_pkt *)arg;
	struct fc_lport *lport = fsp->lp;
	struct fc_frame_header *fh;
	struct fcp_txrdy *dd;
	u8 r_ctl;
	int rc = 0;

	if (IS_ERR(fp)) {
		fc_fcp_error(fsp, fp);
		return;
	}

	fh = fc_frame_header_get(fp);
	r_ctl = fh->fh_r_ctl;

	if (lport->state != LPORT_ST_READY)
		goto out;
	if (fc_fcp_lock_pkt(fsp))
		goto out;

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
		FC_FCP_DBG(fsp, "unexpected frame.  r_ctl %x\n", r_ctl);
	}
unlock:
	fc_fcp_unlock_pkt(fsp);
out:
	fc_frame_free(fp);
}

/**
 * fc_fcp_resp() - Handler for FCP responses
 * @fsp: The FCP packet the response is for
 * @fp:	 The response frame
 */
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
				}
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
				 * bytes that must be transferred for this
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
		FC_FCP_DBG(fsp, "tgt %6.6x xfer len %zx greater than expected, "
			   "len %x, data len %x\n",
			   fsp->rport->port_id,
			   fsp->xfer_len, expected_len, fsp->data_len);
	}
	fc_fcp_complete_locked(fsp);
	return;

len_err:
	FC_FCP_DBG(fsp, "short FCP response. flags 0x%x len %u respl %u "
		   "snsl %u\n", flags, fr_len(fp), respl, snsl);
err:
	fsp->status_code = FC_ERROR;
	fc_fcp_complete_locked(fsp);
}

/**
 * fc_fcp_complete_locked() - Complete processing of a fcp_pkt with the
 *			      fcp_pkt lock held
 * @fsp: The FCP packet to be completed
 *
 * This function may sleep if a timer is pending. The packet lock must be
 * held, and the host lock must not be held.
 */
static void fc_fcp_complete_locked(struct fc_fcp_pkt *fsp)
{
	struct fc_lport *lport = fsp->lp;
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

			csp = lport->tt.seq_start_next(seq);
			conf_frame = fc_fcp_frame_alloc(fsp->lp, 0);
			if (conf_frame) {
				f_ctl = FC_FC_SEQ_INIT;
				f_ctl |= FC_FC_LAST_SEQ | FC_FC_END_SEQ;
				ep = fc_seq_exch(seq);
				fc_fill_fc_hdr(conf_frame, FC_RCTL_DD_SOL_CTL,
					       ep->did, ep->sid,
					       FC_TYPE_FCP, f_ctl, 0);
				lport->tt.seq_send(lport, csp, conf_frame);
			}
		}
		lport->tt.exch_done(seq);
	}
	/*
	 * Some resets driven by SCSI are not I/Os and do not have
	 * SCSI commands associated with the requests. We should not
	 * call I/O completion if we do not have a SCSI command.
	 */
	if (fsp->cmd)
		fc_io_compl(fsp);
}

/**
 * fc_fcp_cleanup_cmd() - Cancel the active exchange on a fcp_pkt
 * @fsp:   The FCP packet whose exchanges should be canceled
 * @error: The reason for the cancellation
 */
static void fc_fcp_cleanup_cmd(struct fc_fcp_pkt *fsp, int error)
{
	struct fc_lport *lport = fsp->lp;

	if (fsp->seq_ptr) {
		lport->tt.exch_done(fsp->seq_ptr);
		fsp->seq_ptr = NULL;
	}
	fsp->status_code = error;
}

/**
 * fc_fcp_cleanup_each_cmd() - Cancel all exchanges on a local port
 * @lport: The local port whose exchanges should be canceled
 * @id:	   The target's ID
 * @lun:   The LUN
 * @error: The reason for cancellation
 *
 * If lun or id is -1, they are ignored.
 */
static void fc_fcp_cleanup_each_cmd(struct fc_lport *lport, unsigned int id,
				    unsigned int lun, int error)
{
	struct fc_fcp_internal *si = fc_get_scsi_internal(lport);
	struct fc_fcp_pkt *fsp;
	struct scsi_cmnd *sc_cmd;
	unsigned long flags;

	spin_lock_irqsave(&si->scsi_queue_lock, flags);
restart:
	list_for_each_entry(fsp, &si->scsi_pkt_queue, list) {
		sc_cmd = fsp->cmd;
		if (id != -1 && scmd_id(sc_cmd) != id)
			continue;

		if (lun != -1 && sc_cmd->device->lun != lun)
			continue;

		fc_fcp_pkt_hold(fsp);
		spin_unlock_irqrestore(&si->scsi_queue_lock, flags);

		if (!fc_fcp_lock_pkt(fsp)) {
			fc_fcp_cleanup_cmd(fsp, error);
			fc_io_compl(fsp);
			fc_fcp_unlock_pkt(fsp);
		}

		fc_fcp_pkt_release(fsp);
		spin_lock_irqsave(&si->scsi_queue_lock, flags);
		/*
		 * while we dropped the lock multiple pkts could
		 * have been released, so we have to start over.
		 */
		goto restart;
	}
	spin_unlock_irqrestore(&si->scsi_queue_lock, flags);
}

/**
 * fc_fcp_abort_io() - Abort all FCP-SCSI exchanges on a local port
 * @lport: The local port whose exchanges are to be aborted
 */
static void fc_fcp_abort_io(struct fc_lport *lport)
{
	fc_fcp_cleanup_each_cmd(lport, -1, -1, FC_HRD_ERROR);
}

/**
 * fc_fcp_pkt_send() - Send a fcp_pkt
 * @lport: The local port to send the FCP packet on
 * @fsp:   The FCP packet to send
 *
 * Return:  Zero for success and -1 for failure
 * Locks:   Called without locks held
 */
static int fc_fcp_pkt_send(struct fc_lport *lport, struct fc_fcp_pkt *fsp)
{
	struct fc_fcp_internal *si = fc_get_scsi_internal(lport);
	unsigned long flags;
	int rc;

	fsp->cmd->SCp.ptr = (char *)fsp;
	fsp->cdb_cmd.fc_dl = htonl(fsp->data_len);
	fsp->cdb_cmd.fc_flags = fsp->req_flags & ~FCP_CFL_LEN_MASK;

	int_to_scsilun(fsp->cmd->device->lun, &fsp->cdb_cmd.fc_lun);
	memcpy(fsp->cdb_cmd.fc_cdb, fsp->cmd->cmnd, fsp->cmd->cmd_len);

	spin_lock_irqsave(&si->scsi_queue_lock, flags);
	list_add_tail(&fsp->list, &si->scsi_pkt_queue);
	spin_unlock_irqrestore(&si->scsi_queue_lock, flags);
	rc = lport->tt.fcp_cmd_send(lport, fsp, fc_fcp_recv);
	if (unlikely(rc)) {
		spin_lock_irqsave(&si->scsi_queue_lock, flags);
		fsp->cmd->SCp.ptr = NULL;
		list_del(&fsp->list);
		spin_unlock_irqrestore(&si->scsi_queue_lock, flags);
	}

	return rc;
}

/**
 * get_fsp_rec_tov() - Helper function to get REC_TOV
 * @fsp: the FCP packet
 *
 * Returns rec tov in jiffies as rpriv->e_d_tov + 1 second
 */
static inline unsigned int get_fsp_rec_tov(struct fc_fcp_pkt *fsp)
{
	struct fc_rport_libfc_priv *rpriv = fsp->rport->dd_data;

	return msecs_to_jiffies(rpriv->e_d_tov) + HZ;
}

/**
 * fc_fcp_cmd_send() - Send a FCP command
 * @lport: The local port to send the command on
 * @fsp:   The FCP packet the command is on
 * @resp:  The handler for the response
 */
static int fc_fcp_cmd_send(struct fc_lport *lport, struct fc_fcp_pkt *fsp,
			   void (*resp)(struct fc_seq *,
					struct fc_frame *fp,
					void *arg))
{
	struct fc_frame *fp;
	struct fc_seq *seq;
	struct fc_rport *rport;
	struct fc_rport_libfc_priv *rpriv;
	const size_t len = sizeof(fsp->cdb_cmd);
	int rc = 0;

	if (fc_fcp_lock_pkt(fsp))
		return 0;

	fp = fc_fcp_frame_alloc(lport, sizeof(fsp->cdb_cmd));
	if (!fp) {
		rc = -1;
		goto unlock;
	}

	memcpy(fc_frame_payload_get(fp, len), &fsp->cdb_cmd, len);
	fr_fsp(fp) = fsp;
	rport = fsp->rport;
	fsp->max_payload = rport->maxframe_size;
	rpriv = rport->dd_data;

	fc_fill_fc_hdr(fp, FC_RCTL_DD_UNSOL_CMD, rport->port_id,
		       rpriv->local_port->port_id, FC_TYPE_FCP,
		       FC_FCTL_REQ, 0);

	seq = lport->tt.exch_seq_send(lport, fp, resp, fc_fcp_pkt_destroy,
				      fsp, 0);
	if (!seq) {
		rc = -1;
		goto unlock;
	}
	fsp->seq_ptr = seq;
	fc_fcp_pkt_hold(fsp);	/* hold for fc_fcp_pkt_destroy */

	setup_timer(&fsp->timer, fc_fcp_timeout, (unsigned long)fsp);
	if (rpriv->flags & FC_RP_FLAGS_REC_SUPPORTED)
		fc_fcp_timer_set(fsp, get_fsp_rec_tov(fsp));

unlock:
	fc_fcp_unlock_pkt(fsp);
	return rc;
}

/**
 * fc_fcp_error() - Handler for FCP layer errors
 * @fsp: The FCP packet the error is on
 * @fp:	 The frame that has errored
 */
static void fc_fcp_error(struct fc_fcp_pkt *fsp, struct fc_frame *fp)
{
	int error = PTR_ERR(fp);

	if (fc_fcp_lock_pkt(fsp))
		return;

	if (error == -FC_EX_CLOSED) {
		fc_fcp_retry_cmd(fsp);
		goto unlock;
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

/**
 * fc_fcp_pkt_abort() - Abort a fcp_pkt
 * @fsp:   The FCP packet to abort on
 *
 * Called to send an abort and then wait for abort completion
 */
static int fc_fcp_pkt_abort(struct fc_fcp_pkt *fsp)
{
	int rc = FAILED;
	unsigned long ticks_left;

	if (fc_fcp_send_abort(fsp))
		return FAILED;

	init_completion(&fsp->tm_done);
	fsp->wait_for_comp = 1;

	spin_unlock_bh(&fsp->scsi_pkt_lock);
	ticks_left = wait_for_completion_timeout(&fsp->tm_done,
							FC_SCSI_TM_TOV);
	spin_lock_bh(&fsp->scsi_pkt_lock);
	fsp->wait_for_comp = 0;

	if (!ticks_left) {
		FC_FCP_DBG(fsp, "target abort cmd  failed\n");
	} else if (fsp->state & FC_SRB_ABORTED) {
		FC_FCP_DBG(fsp, "target abort cmd  passed\n");
		rc = SUCCESS;
		fc_fcp_complete_locked(fsp);
	}

	return rc;
}

/**
 * fc_lun_reset_send() - Send LUN reset command
 * @data: The FCP packet that identifies the LUN to be reset
 */
static void fc_lun_reset_send(unsigned long data)
{
	struct fc_fcp_pkt *fsp = (struct fc_fcp_pkt *)data;
	struct fc_lport *lport = fsp->lp;

	if (lport->tt.fcp_cmd_send(lport, fsp, fc_tm_done)) {
		if (fsp->recov_retry++ >= FC_MAX_RECOV_RETRY)
			return;
		if (fc_fcp_lock_pkt(fsp))
			return;
		setup_timer(&fsp->timer, fc_lun_reset_send, (unsigned long)fsp);
		fc_fcp_timer_set(fsp, get_fsp_rec_tov(fsp));
		fc_fcp_unlock_pkt(fsp);
	}
}

/**
 * fc_lun_reset() - Send a LUN RESET command to a device
 *		    and wait for the reply
 * @lport: The local port to sent the command on
 * @fsp:   The FCP packet that identifies the LUN to be reset
 * @id:	   The SCSI command ID
 * @lun:   The LUN ID to be reset
 */
static int fc_lun_reset(struct fc_lport *lport, struct fc_fcp_pkt *fsp,
			unsigned int id, unsigned int lun)
{
	int rc;

	fsp->cdb_cmd.fc_dl = htonl(fsp->data_len);
	fsp->cdb_cmd.fc_tm_flags = FCP_TMF_LUN_RESET;
	int_to_scsilun(lun, &fsp->cdb_cmd.fc_lun);

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
		lport->tt.exch_done(fsp->seq_ptr);
		fsp->seq_ptr = NULL;
	}
	fsp->wait_for_comp = 0;
	spin_unlock_bh(&fsp->scsi_pkt_lock);

	if (!rc) {
		FC_SCSI_DBG(lport, "lun reset failed\n");
		return FAILED;
	}

	/* cdb_status holds the tmf's rsp code */
	if (fsp->cdb_status != FCP_TMF_CMPL)
		return FAILED;

	FC_SCSI_DBG(lport, "lun reset to lun %u completed\n", lun);
	fc_fcp_cleanup_each_cmd(lport, id, lun, FC_CMD_ABORTED);
	return SUCCESS;
}

/**
 * fc_tm_done() - Task Management response handler
 * @seq: The sequence that the response is on
 * @fp:	 The response frame
 * @arg: The FCP packet the response is for
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
		goto out;

	/*
	 * raced with eh timeout handler.
	 */
	if (!fsp->seq_ptr || !fsp->wait_for_comp)
		goto out_unlock;

	fh = fc_frame_header_get(fp);
	if (fh->fh_type != FC_TYPE_BLS)
		fc_fcp_resp(fsp, fp);
	fsp->seq_ptr = NULL;
	fsp->lp->tt.exch_done(seq);
out_unlock:
	fc_fcp_unlock_pkt(fsp);
out:
	fc_frame_free(fp);
}

/**
 * fc_fcp_cleanup() - Cleanup all FCP exchanges on a local port
 * @lport: The local port to be cleaned up
 */
static void fc_fcp_cleanup(struct fc_lport *lport)
{
	fc_fcp_cleanup_each_cmd(lport, -1, -1, FC_ERROR);
}

/**
 * fc_fcp_timeout() - Handler for fcp_pkt timeouts
 * @data: The FCP packet that has timed out
 *
 * If REC is supported then just issue it and return. The REC exchange will
 * complete or time out and recovery can continue at that point. Otherwise,
 * if the response has been received without all the data it has been
 * ER_TIMEOUT since the response was received. If the response has not been
 * received we see if data was received recently. If it has been then we
 * continue waiting, otherwise, we abort the command.
 */
static void fc_fcp_timeout(unsigned long data)
{
	struct fc_fcp_pkt *fsp = (struct fc_fcp_pkt *)data;
	struct fc_rport *rport = fsp->rport;
	struct fc_rport_libfc_priv *rpriv = rport->dd_data;

	if (fc_fcp_lock_pkt(fsp))
		return;

	if (fsp->cdb_cmd.fc_tm_flags)
		goto unlock;

	fsp->state |= FC_SRB_FCP_PROCESSING_TMO;

	if (rpriv->flags & FC_RP_FLAGS_REC_SUPPORTED)
		fc_fcp_rec(fsp);
	else if (fsp->state & FC_SRB_RCV_STATUS)
		fc_fcp_complete_locked(fsp);
	else
		fc_fcp_recovery(fsp, FC_TIMED_OUT);
	fsp->state &= ~FC_SRB_FCP_PROCESSING_TMO;
unlock:
	fc_fcp_unlock_pkt(fsp);
}

/**
 * fc_fcp_rec() - Send a REC ELS request
 * @fsp: The FCP packet to send the REC request on
 */
static void fc_fcp_rec(struct fc_fcp_pkt *fsp)
{
	struct fc_lport *lport;
	struct fc_frame *fp;
	struct fc_rport *rport;
	struct fc_rport_libfc_priv *rpriv;

	lport = fsp->lp;
	rport = fsp->rport;
	rpriv = rport->dd_data;
	if (!fsp->seq_ptr || rpriv->rp_state != RPORT_ST_READY) {
		fsp->status_code = FC_HRD_ERROR;
		fsp->io_status = 0;
		fc_fcp_complete_locked(fsp);
		return;
	}

	fp = fc_fcp_frame_alloc(lport, sizeof(struct fc_els_rec));
	if (!fp)
		goto retry;

	fr_seq(fp) = fsp->seq_ptr;
	fc_fill_fc_hdr(fp, FC_RCTL_ELS_REQ, rport->port_id,
		       rpriv->local_port->port_id, FC_TYPE_ELS,
		       FC_FCTL_REQ, 0);
	if (lport->tt.elsct_send(lport, rport->port_id, fp, ELS_REC,
				 fc_fcp_rec_resp, fsp,
				 2 * lport->r_a_tov)) {
		fc_fcp_pkt_hold(fsp);		/* hold while REC outstanding */
		return;
	}
retry:
	if (fsp->recov_retry++ < FC_MAX_RECOV_RETRY)
		fc_fcp_timer_set(fsp, get_fsp_rec_tov(fsp));
	else
		fc_fcp_recovery(fsp, FC_TIMED_OUT);
}

/**
 * fc_fcp_rec_resp() - Handler for REC ELS responses
 * @seq: The sequence the response is on
 * @fp:	 The response frame
 * @arg: The FCP packet the response is on
 *
 * If the response is a reject then the scsi layer will handle
 * the timeout. If the response is a LS_ACC then if the I/O was not completed
 * set the timeout and return. If the I/O was completed then complete the
 * exchange and tell the SCSI layer.
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
	struct fc_rport_libfc_priv *rpriv;

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
			FC_FCP_DBG(fsp, "device %x unexpected REC reject "
				   "reason %d expl %d\n",
				   fsp->rport->port_id, rjt->er_reason,
				   rjt->er_explan);
			/* fall through */
		case ELS_RJT_UNSUP:
			FC_FCP_DBG(fsp, "device does not support REC\n");
			rpriv = fsp->rport->dd_data;
			/*
			 * if we do not spport RECs or got some bogus
			 * reason then resetup timer so we check for
			 * making progress.
			 */
			rpriv->flags &= ~FC_RP_FLAGS_REC_SUPPORTED;
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
			fc_fcp_recovery(fsp, FC_ERROR);
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
			fc_fcp_timer_set(fsp,  get_fsp_rec_tov(fsp));
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

/**
 * fc_fcp_rec_error() - Handler for REC errors
 * @fsp: The FCP packet the error is on
 * @fp:	 The REC frame
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
		FC_FCP_DBG(fsp, "REC %p fid %6.6x error unexpected error %d\n",
			   fsp, fsp->rport->port_id, error);
		fsp->status_code = FC_CMD_PLOGO;
		/* fall through */

	case -FC_EX_TIMEOUT:
		/*
		 * Assume REC or LS_ACC was lost.
		 * The exchange manager will have aborted REC, so retry.
		 */
		FC_FCP_DBG(fsp, "REC fid %6.6x error error %d retry %d/%d\n",
			   fsp->rport->port_id, error, fsp->recov_retry,
			   FC_MAX_RECOV_RETRY);
		if (fsp->recov_retry++ < FC_MAX_RECOV_RETRY)
			fc_fcp_rec(fsp);
		else
			fc_fcp_recovery(fsp, FC_ERROR);
		break;
	}
	fc_fcp_unlock_pkt(fsp);
out:
	fc_fcp_pkt_release(fsp);	/* drop hold for outstanding REC */
}

/**
 * fc_fcp_recovery() - Handler for fcp_pkt recovery
 * @fsp: The FCP pkt that needs to be aborted
 */
static void fc_fcp_recovery(struct fc_fcp_pkt *fsp, u8 code)
{
	fsp->status_code = code;
	fsp->cdb_status = 0;
	fsp->io_status = 0;
	/*
	 * if this fails then we let the scsi command timer fire and
	 * scsi-ml escalate.
	 */
	fc_fcp_send_abort(fsp);
}

/**
 * fc_fcp_srr() - Send a SRR request (Sequence Retransmission Request)
 * @fsp:   The FCP packet the SRR is to be sent on
 * @r_ctl: The R_CTL field for the SRR request
 * This is called after receiving status but insufficient data, or
 * when expecting status but the request has timed out.
 */
static void fc_fcp_srr(struct fc_fcp_pkt *fsp, enum fc_rctl r_ctl, u32 offset)
{
	struct fc_lport *lport = fsp->lp;
	struct fc_rport *rport;
	struct fc_rport_libfc_priv *rpriv;
	struct fc_exch *ep = fc_seq_exch(fsp->seq_ptr);
	struct fc_seq *seq;
	struct fcp_srr *srr;
	struct fc_frame *fp;
	unsigned int rec_tov;

	rport = fsp->rport;
	rpriv = rport->dd_data;

	if (!(rpriv->flags & FC_RP_FLAGS_RETRY) ||
	    rpriv->rp_state != RPORT_ST_READY)
		goto retry;			/* shouldn't happen */
	fp = fc_fcp_frame_alloc(lport, sizeof(*srr));
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
		       rpriv->local_port->port_id, FC_TYPE_FCP,
		       FC_FCTL_REQ, 0);

	rec_tov = get_fsp_rec_tov(fsp);
	seq = lport->tt.exch_seq_send(lport, fp, fc_fcp_srr_resp,
				      fc_fcp_pkt_destroy,
				      fsp, jiffies_to_msecs(rec_tov));
	if (!seq)
		goto retry;

	fsp->recov_seq = seq;
	fsp->xfer_len = offset;
	fsp->xfer_contig_end = offset;
	fsp->state &= ~FC_SRB_RCV_STATUS;
	fc_fcp_pkt_hold(fsp);		/* hold for outstanding SRR */
	return;
retry:
	fc_fcp_retry_cmd(fsp);
}

/**
 * fc_fcp_srr_resp() - Handler for SRR response
 * @seq: The sequence the SRR is on
 * @fp:	 The SRR frame
 * @arg: The FCP packet the SRR is on
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

	switch (fc_frame_payload_op(fp)) {
	case ELS_LS_ACC:
		fsp->recov_retry = 0;
		fc_fcp_timer_set(fsp, get_fsp_rec_tov(fsp));
		break;
	case ELS_LS_RJT:
	default:
		fc_fcp_recovery(fsp, FC_ERROR);
		break;
	}
	fc_fcp_unlock_pkt(fsp);
out:
	fsp->lp->tt.exch_done(seq);
	fc_frame_free(fp);
}

/**
 * fc_fcp_srr_error() - Handler for SRR errors
 * @fsp: The FCP packet that the SRR error is on
 * @fp:	 The SRR frame
 */
static void fc_fcp_srr_error(struct fc_fcp_pkt *fsp, struct fc_frame *fp)
{
	if (fc_fcp_lock_pkt(fsp))
		goto out;
	switch (PTR_ERR(fp)) {
	case -FC_EX_TIMEOUT:
		if (fsp->recov_retry++ < FC_MAX_RECOV_RETRY)
			fc_fcp_rec(fsp);
		else
			fc_fcp_recovery(fsp, FC_TIMED_OUT);
		break;
	case -FC_EX_CLOSED:			/* e.g., link failure */
		/* fall through */
	default:
		fc_fcp_retry_cmd(fsp);
		break;
	}
	fc_fcp_unlock_pkt(fsp);
out:
	fsp->lp->tt.exch_done(fsp->recov_seq);
}

/**
 * fc_fcp_lport_queue_ready() - Determine if the lport and it's queue is ready
 * @lport: The local port to be checked
 */
static inline int fc_fcp_lport_queue_ready(struct fc_lport *lport)
{
	/* lock ? */
	return (lport->state == LPORT_ST_READY) &&
		lport->link_up && !lport->qfull;
}

/**
 * fc_queuecommand() - The queuecommand function of the SCSI template
 * @shost: The Scsi_Host that the command was issued to
 * @cmd:   The scsi_cmnd to be executed
 *
 * This is the i/o strategy routine, called by the SCSI layer.
 */
int fc_queuecommand(struct Scsi_Host *shost, struct scsi_cmnd *sc_cmd)
{
	struct fc_lport *lport = shost_priv(shost);
	struct fc_rport *rport = starget_to_rport(scsi_target(sc_cmd->device));
	struct fc_fcp_pkt *fsp;
	struct fc_rport_libfc_priv *rpriv;
	int rval;
	int rc = 0;
	struct fc_stats *stats;

	rval = fc_remote_port_chkready(rport);
	if (rval) {
		sc_cmd->result = rval;
		sc_cmd->scsi_done(sc_cmd);
		return 0;
	}

	if (!*(struct fc_remote_port **)rport->dd_data) {
		/*
		 * rport is transitioning from blocked/deleted to
		 * online
		 */
		sc_cmd->result = DID_IMM_RETRY << 16;
		sc_cmd->scsi_done(sc_cmd);
		goto out;
	}

	rpriv = rport->dd_data;

	if (!fc_fcp_lport_queue_ready(lport)) {
		if (lport->qfull)
			fc_fcp_can_queue_ramp_down(lport);
		rc = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	}

	fsp = fc_fcp_pkt_alloc(lport, GFP_ATOMIC);
	if (fsp == NULL) {
		rc = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	}

	/*
	 * build the libfc request pkt
	 */
	fsp->cmd = sc_cmd;	/* save the cmd */
	fsp->rport = rport;	/* set the remote port ptr */

	/*
	 * set up the transfer length
	 */
	fsp->data_len = scsi_bufflen(sc_cmd);
	fsp->xfer_len = 0;

	/*
	 * setup the data direction
	 */
	stats = per_cpu_ptr(lport->stats, get_cpu());
	if (sc_cmd->sc_data_direction == DMA_FROM_DEVICE) {
		fsp->req_flags = FC_SRB_READ;
		stats->InputRequests++;
		stats->InputBytes += fsp->data_len;
	} else if (sc_cmd->sc_data_direction == DMA_TO_DEVICE) {
		fsp->req_flags = FC_SRB_WRITE;
		stats->OutputRequests++;
		stats->OutputBytes += fsp->data_len;
	} else {
		fsp->req_flags = 0;
		stats->ControlRequests++;
	}
	put_cpu();

	/*
	 * send it to the lower layer
	 * if we get -1 return then put the request in the pending
	 * queue.
	 */
	rval = fc_fcp_pkt_send(lport, fsp);
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
 * fc_io_compl() - Handle responses for completed commands
 * @fsp: The FCP packet that is complete
 *
 * Translates fcp_pkt errors to a Linux SCSI errors.
 * The fcp packet lock must be held when calling.
 */
static void fc_io_compl(struct fc_fcp_pkt *fsp)
{
	struct fc_fcp_internal *si;
	struct scsi_cmnd *sc_cmd;
	struct fc_lport *lport;
	unsigned long flags;

	/* release outstanding ddp context */
	fc_fcp_ddp_done(fsp);

	fsp->state |= FC_SRB_COMPL;
	if (!(fsp->state & FC_SRB_FCP_PROCESSING_TMO)) {
		spin_unlock_bh(&fsp->scsi_pkt_lock);
		del_timer_sync(&fsp->timer);
		spin_lock_bh(&fsp->scsi_pkt_lock);
	}

	lport = fsp->lp;
	si = fc_get_scsi_internal(lport);

	/*
	 * if can_queue ramp down is done then try can_queue ramp up
	 * since commands are completing now.
	 */
	if (si->last_can_queue_ramp_down_time)
		fc_fcp_can_queue_ramp_up(lport);

	sc_cmd = fsp->cmd;
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
		} else {
			/*
			 * transport level I/O was ok but scsi
			 * has non zero status
			 */
			sc_cmd->result = (DID_OK << 16) | fsp->cdb_status;
		}
		break;
	case FC_ERROR:
		FC_FCP_DBG(fsp, "Returning DID_ERROR to scsi-ml "
			   "due to FC_ERROR\n");
		sc_cmd->result = DID_ERROR << 16;
		break;
	case FC_DATA_UNDRUN:
		if ((fsp->cdb_status == 0) && !(fsp->req_flags & FC_SRB_READ)) {
			/*
			 * scsi status is good but transport level
			 * underrun.
			 */
			if (fsp->state & FC_SRB_RCV_STATUS) {
				sc_cmd->result = DID_OK << 16;
			} else {
				FC_FCP_DBG(fsp, "Returning DID_ERROR to scsi-ml"
					   " due to FC_DATA_UNDRUN (trans)\n");
				sc_cmd->result = DID_ERROR << 16;
			}
		} else {
			/*
			 * scsi got underrun, this is an error
			 */
			FC_FCP_DBG(fsp, "Returning DID_ERROR to scsi-ml "
				   "due to FC_DATA_UNDRUN (scsi)\n");
			CMD_RESID_LEN(sc_cmd) = fsp->scsi_resid;
			sc_cmd->result = (DID_ERROR << 16) | fsp->cdb_status;
		}
		break;
	case FC_DATA_OVRRUN:
		/*
		 * overrun is an error
		 */
		FC_FCP_DBG(fsp, "Returning DID_ERROR to scsi-ml "
			   "due to FC_DATA_OVRRUN\n");
		sc_cmd->result = (DID_ERROR << 16) | fsp->cdb_status;
		break;
	case FC_CMD_ABORTED:
		FC_FCP_DBG(fsp, "Returning DID_ERROR to scsi-ml "
			  "due to FC_CMD_ABORTED\n");
		sc_cmd->result = (DID_ERROR << 16) | fsp->io_status;
		break;
	case FC_CMD_RESET:
		FC_FCP_DBG(fsp, "Returning DID_RESET to scsi-ml "
			   "due to FC_CMD_RESET\n");
		sc_cmd->result = (DID_RESET << 16);
		break;
	case FC_HRD_ERROR:
		FC_FCP_DBG(fsp, "Returning DID_NO_CONNECT to scsi-ml "
			   "due to FC_HRD_ERROR\n");
		sc_cmd->result = (DID_NO_CONNECT << 16);
		break;
	case FC_CRC_ERROR:
		FC_FCP_DBG(fsp, "Returning DID_PARITY to scsi-ml "
			   "due to FC_CRC_ERROR\n");
		sc_cmd->result = (DID_PARITY << 16);
		break;
	case FC_TIMED_OUT:
		FC_FCP_DBG(fsp, "Returning DID_BUS_BUSY to scsi-ml "
			   "due to FC_TIMED_OUT\n");
		sc_cmd->result = (DID_BUS_BUSY << 16) | fsp->io_status;
		break;
	default:
		FC_FCP_DBG(fsp, "Returning DID_ERROR to scsi-ml "
			   "due to unknown error\n");
		sc_cmd->result = (DID_ERROR << 16);
		break;
	}

	if (lport->state != LPORT_ST_READY && fsp->status_code != FC_COMPLETE)
		sc_cmd->result = (DID_TRANSPORT_DISRUPTED << 16);

	spin_lock_irqsave(&si->scsi_queue_lock, flags);
	list_del(&fsp->list);
	sc_cmd->SCp.ptr = NULL;
	spin_unlock_irqrestore(&si->scsi_queue_lock, flags);
	sc_cmd->scsi_done(sc_cmd);

	/* release ref from initial allocation in queue command */
	fc_fcp_pkt_release(fsp);
}

/**
 * fc_eh_abort() - Abort a command
 * @sc_cmd: The SCSI command to abort
 *
 * From SCSI host template.
 * Send an ABTS to the target device and wait for the response.
 */
int fc_eh_abort(struct scsi_cmnd *sc_cmd)
{
	struct fc_fcp_pkt *fsp;
	struct fc_lport *lport;
	struct fc_fcp_internal *si;
	int rc = FAILED;
	unsigned long flags;
	int rval;

	rval = fc_block_scsi_eh(sc_cmd);
	if (rval)
		return rval;

	lport = shost_priv(sc_cmd->device->host);
	if (lport->state != LPORT_ST_READY)
		return rc;
	else if (!lport->link_up)
		return rc;

	si = fc_get_scsi_internal(lport);
	spin_lock_irqsave(&si->scsi_queue_lock, flags);
	fsp = CMD_SP(sc_cmd);
	if (!fsp) {
		/* command completed while scsi eh was setting up */
		spin_unlock_irqrestore(&si->scsi_queue_lock, flags);
		return SUCCESS;
	}
	/* grab a ref so the fsp and sc_cmd cannot be relased from under us */
	fc_fcp_pkt_hold(fsp);
	spin_unlock_irqrestore(&si->scsi_queue_lock, flags);

	if (fc_fcp_lock_pkt(fsp)) {
		/* completed while we were waiting for timer to be deleted */
		rc = SUCCESS;
		goto release_pkt;
	}

	rc = fc_fcp_pkt_abort(fsp);
	fc_fcp_unlock_pkt(fsp);

release_pkt:
	fc_fcp_pkt_release(fsp);
	return rc;
}
EXPORT_SYMBOL(fc_eh_abort);

/**
 * fc_eh_device_reset() - Reset a single LUN
 * @sc_cmd: The SCSI command which identifies the device whose
 *	    LUN is to be reset
 *
 * Set from SCSI host template.
 */
int fc_eh_device_reset(struct scsi_cmnd *sc_cmd)
{
	struct fc_lport *lport;
	struct fc_fcp_pkt *fsp;
	struct fc_rport *rport = starget_to_rport(scsi_target(sc_cmd->device));
	int rc = FAILED;
	int rval;

	rval = fc_block_scsi_eh(sc_cmd);
	if (rval)
		return rval;

	lport = shost_priv(sc_cmd->device->host);

	if (lport->state != LPORT_ST_READY)
		return rc;

	FC_SCSI_DBG(lport, "Resetting rport (%6.6x)\n", rport->port_id);

	fsp = fc_fcp_pkt_alloc(lport, GFP_NOIO);
	if (fsp == NULL) {
		printk(KERN_WARNING "libfc: could not allocate scsi_pkt\n");
		goto out;
	}

	/*
	 * Build the libfc request pkt. Do not set the scsi cmnd, because
	 * the sc passed in is not setup for execution like when sent
	 * through the queuecommand callout.
	 */
	fsp->rport = rport;	/* set the remote port ptr */

	/*
	 * flush outstanding commands
	 */
	rc = fc_lun_reset(lport, fsp, scmd_id(sc_cmd), sc_cmd->device->lun);
	fsp->state = FC_SRB_FREE;
	fc_fcp_pkt_release(fsp);

out:
	return rc;
}
EXPORT_SYMBOL(fc_eh_device_reset);

/**
 * fc_eh_host_reset() - Reset a Scsi_Host.
 * @sc_cmd: The SCSI command that identifies the SCSI host to be reset
 */
int fc_eh_host_reset(struct scsi_cmnd *sc_cmd)
{
	struct Scsi_Host *shost = sc_cmd->device->host;
	struct fc_lport *lport = shost_priv(shost);
	unsigned long wait_tmo;

	FC_SCSI_DBG(lport, "Resetting host\n");

	fc_block_scsi_eh(sc_cmd);

	lport->tt.lport_reset(lport);
	wait_tmo = jiffies + FC_HOST_RESET_TIMEOUT;
	while (!fc_fcp_lport_queue_ready(lport) && time_before(jiffies,
							       wait_tmo))
		msleep(1000);

	if (fc_fcp_lport_queue_ready(lport)) {
		shost_printk(KERN_INFO, shost, "libfc: Host reset succeeded "
			     "on port (%6.6x)\n", lport->port_id);
		return SUCCESS;
	} else {
		shost_printk(KERN_INFO, shost, "libfc: Host reset failed, "
			     "port (%6.6x) is not ready.\n",
			     lport->port_id);
		return FAILED;
	}
}
EXPORT_SYMBOL(fc_eh_host_reset);

/**
 * fc_slave_alloc() - Configure the queue depth of a Scsi_Host
 * @sdev: The SCSI device that identifies the SCSI host
 *
 * Configures queue depth based on host's cmd_per_len. If not set
 * then we use the libfc default.
 */
int fc_slave_alloc(struct scsi_device *sdev)
{
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));

	if (!rport || fc_remote_port_chkready(rport))
		return -ENXIO;

	if (sdev->tagged_supported)
		scsi_activate_tcq(sdev, FC_FCP_DFLT_QUEUE_DEPTH);
	else
		scsi_adjust_queue_depth(sdev, scsi_get_tag_type(sdev),
					FC_FCP_DFLT_QUEUE_DEPTH);

	return 0;
}
EXPORT_SYMBOL(fc_slave_alloc);

/**
 * fc_change_queue_depth() - Change a device's queue depth
 * @sdev:   The SCSI device whose queue depth is to change
 * @qdepth: The new queue depth
 * @reason: The resason for the change
 */
int fc_change_queue_depth(struct scsi_device *sdev, int qdepth, int reason)
{
	switch (reason) {
	case SCSI_QDEPTH_DEFAULT:
		scsi_adjust_queue_depth(sdev, scsi_get_tag_type(sdev), qdepth);
		break;
	case SCSI_QDEPTH_QFULL:
		scsi_track_queue_full(sdev, qdepth);
		break;
	case SCSI_QDEPTH_RAMP_UP:
		scsi_adjust_queue_depth(sdev, scsi_get_tag_type(sdev), qdepth);
		break;
	default:
		return -EOPNOTSUPP;
	}
	return sdev->queue_depth;
}
EXPORT_SYMBOL(fc_change_queue_depth);

/**
 * fc_change_queue_type() - Change a device's queue type
 * @sdev:     The SCSI device whose queue depth is to change
 * @tag_type: Identifier for queue type
 */
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

/**
 * fc_fcp_destory() - Tear down the FCP layer for a given local port
 * @lport: The local port that no longer needs the FCP layer
 */
void fc_fcp_destroy(struct fc_lport *lport)
{
	struct fc_fcp_internal *si = fc_get_scsi_internal(lport);

	if (!list_empty(&si->scsi_pkt_queue))
		printk(KERN_ERR "libfc: Leaked SCSI packets when destroying "
		       "port (%6.6x)\n", lport->port_id);

	mempool_destroy(si->scsi_pkt_pool);
	kfree(si);
	lport->scsi_priv = NULL;
}
EXPORT_SYMBOL(fc_fcp_destroy);

int fc_setup_fcp(void)
{
	int rc = 0;

	scsi_pkt_cachep = kmem_cache_create("libfc_fcp_pkt",
					    sizeof(struct fc_fcp_pkt),
					    0, SLAB_HWCACHE_ALIGN, NULL);
	if (!scsi_pkt_cachep) {
		printk(KERN_ERR "libfc: Unable to allocate SRB cache, "
		       "module load failed!");
		rc = -ENOMEM;
	}

	return rc;
}

void fc_destroy_fcp(void)
{
	if (scsi_pkt_cachep)
		kmem_cache_destroy(scsi_pkt_cachep);
}

/**
 * fc_fcp_init() - Initialize the FCP layer for a local port
 * @lport: The local port to initialize the exchange layer for
 */
int fc_fcp_init(struct fc_lport *lport)
{
	int rc;
	struct fc_fcp_internal *si;

	if (!lport->tt.fcp_cmd_send)
		lport->tt.fcp_cmd_send = fc_fcp_cmd_send;

	if (!lport->tt.fcp_cleanup)
		lport->tt.fcp_cleanup = fc_fcp_cleanup;

	if (!lport->tt.fcp_abort_io)
		lport->tt.fcp_abort_io = fc_fcp_abort_io;

	si = kzalloc(sizeof(struct fc_fcp_internal), GFP_KERNEL);
	if (!si)
		return -ENOMEM;
	lport->scsi_priv = si;
	si->max_can_queue = lport->host->can_queue;
	INIT_LIST_HEAD(&si->scsi_pkt_queue);
	spin_lock_init(&si->scsi_queue_lock);

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
