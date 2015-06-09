/*
 * Copyright (c) 2010 Cisco Systems, Inc.
 *
 * Portions based on tcm_loop_fabric_scsi.c and libfc/fc_fcp.c
 *
 * Copyright (c) 2007 Intel Corporation. All rights reserved.
 * Copyright (c) 2008 Red Hat, Inc.  All rights reserved.
 * Copyright (c) 2008 Mike Christie
 * Copyright (c) 2009 Rising Tide, Inc.
 * Copyright (c) 2009 Linux-iSCSI.org
 * Copyright (c) 2009 Nicholas A. Bellinger <nab@linux-iscsi.org>
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
 */

/* XXX TBD some includes may be extraneous */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/utsname.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/configfs.h>
#include <linux/ctype.h>
#include <linux/hash.h>
#include <linux/ratelimit.h>
#include <asm/unaligned.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/libfc.h>
#include <scsi/fc_encode.h>

#include <target/target_core_base.h>
#include <target/target_core_fabric.h>
#include <target/target_core_configfs.h>
#include <target/configfs_macros.h>

#include "tcm_fc.h"

/*
 * Deliver read data back to initiator.
 * XXX TBD handle resource problems later.
 */
int ft_queue_data_in(struct se_cmd *se_cmd)
{
	struct ft_cmd *cmd = container_of(se_cmd, struct ft_cmd, se_cmd);
	struct fc_frame *fp = NULL;
	struct fc_exch *ep;
	struct fc_lport *lport;
	struct scatterlist *sg = NULL;
	size_t remaining;
	u32 f_ctl = FC_FC_EX_CTX | FC_FC_REL_OFF;
	u32 mem_off = 0;
	u32 fh_off = 0;
	u32 frame_off = 0;
	size_t frame_len = 0;
	size_t mem_len = 0;
	size_t tlen;
	size_t off_in_page;
	struct page *page = NULL;
	int use_sg;
	int error;
	void *page_addr;
	void *from;
	void *to = NULL;

	if (cmd->aborted)
		return 0;

	if (se_cmd->scsi_status == SAM_STAT_TASK_SET_FULL)
		goto queue_status;

	ep = fc_seq_exch(cmd->seq);
	lport = ep->lp;
	cmd->seq = lport->tt.seq_start_next(cmd->seq);

	remaining = se_cmd->data_length;

	/*
	 * Setup to use first mem list entry, unless no data.
	 */
	BUG_ON(remaining && !se_cmd->t_data_sg);
	if (remaining) {
		sg = se_cmd->t_data_sg;
		mem_len = sg->length;
		mem_off = sg->offset;
		page = sg_page(sg);
	}

	/* no scatter/gather in skb for odd word length due to fc_seq_send() */
	use_sg = !(remaining % 4);

	while (remaining) {
		struct fc_seq *seq = cmd->seq;

		if (!seq) {
			pr_debug("%s: Command aborted, xid 0x%x\n",
				 __func__, ep->xid);
			break;
		}
		if (!mem_len) {
			sg = sg_next(sg);
			mem_len = min((size_t)sg->length, remaining);
			mem_off = sg->offset;
			page = sg_page(sg);
		}
		if (!frame_len) {
			/*
			 * If lport's has capability of Large Send Offload LSO)
			 * , then allow 'frame_len' to be as big as 'lso_max'
			 * if indicated transfer length is >= lport->lso_max
			 */
			frame_len = (lport->seq_offload) ? lport->lso_max :
							  cmd->sess->max_frame;
			frame_len = min(frame_len, remaining);
			fp = fc_frame_alloc(lport, use_sg ? 0 : frame_len);
			if (!fp)
				return -ENOMEM;
			to = fc_frame_payload_get(fp, 0);
			fh_off = frame_off;
			frame_off += frame_len;
			/*
			 * Setup the frame's max payload which is used by base
			 * driver to indicate HW about max frame size, so that
			 * HW can do fragmentation appropriately based on
			 * "gso_max_size" of underline netdev.
			 */
			fr_max_payload(fp) = cmd->sess->max_frame;
		}
		tlen = min(mem_len, frame_len);

		if (use_sg) {
			off_in_page = mem_off;
			BUG_ON(!page);
			get_page(page);
			skb_fill_page_desc(fp_skb(fp),
					   skb_shinfo(fp_skb(fp))->nr_frags,
					   page, off_in_page, tlen);
			fr_len(fp) += tlen;
			fp_skb(fp)->data_len += tlen;
			fp_skb(fp)->truesize +=
					PAGE_SIZE << compound_order(page);
		} else {
			BUG_ON(!page);
			from = kmap_atomic(page + (mem_off >> PAGE_SHIFT));
			page_addr = from;
			from += mem_off & ~PAGE_MASK;
			tlen = min(tlen, (size_t)(PAGE_SIZE -
						(mem_off & ~PAGE_MASK)));
			memcpy(to, from, tlen);
			kunmap_atomic(page_addr);
			to += tlen;
		}

		mem_off += tlen;
		mem_len -= tlen;
		frame_len -= tlen;
		remaining -= tlen;

		if (frame_len &&
		    (skb_shinfo(fp_skb(fp))->nr_frags < FC_FRAME_SG_LEN))
			continue;
		if (!remaining)
			f_ctl |= FC_FC_END_SEQ;
		fc_fill_fc_hdr(fp, FC_RCTL_DD_SOL_DATA, ep->did, ep->sid,
			       FC_TYPE_FCP, f_ctl, fh_off);
		error = lport->tt.seq_send(lport, seq, fp);
		if (error) {
			pr_info_ratelimited("%s: Failed to send frame %p, "
						"xid <0x%x>, remaining %zu, "
						"lso_max <0x%x>\n",
						__func__, fp, ep->xid,
						remaining, lport->lso_max);
			/*
			 * Go ahead and set TASK_SET_FULL status ignoring the
			 * rest of the DataIN, and immediately attempt to
			 * send the response via ft_queue_status() in order
			 * to notify the initiator that it should reduce it's
			 * per LUN queue_depth.
			 */
			se_cmd->scsi_status = SAM_STAT_TASK_SET_FULL;
			break;
		}
	}
queue_status:
	return ft_queue_status(se_cmd);
}

static void ft_execute_work(struct work_struct *work)
{
	struct ft_cmd *cmd = container_of(work, struct ft_cmd, work);

	target_execute_cmd(&cmd->se_cmd);
}

/*
 * Receive write data frame.
 */
void ft_recv_write_data(struct ft_cmd *cmd, struct fc_frame *fp)
{
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct fc_seq *seq = cmd->seq;
	struct fc_exch *ep;
	struct fc_lport *lport;
	struct fc_frame_header *fh;
	struct scatterlist *sg = NULL;
	u32 mem_off = 0;
	u32 rel_off;
	size_t frame_len;
	size_t mem_len = 0;
	size_t tlen;
	struct page *page = NULL;
	void *page_addr;
	void *from;
	void *to;
	u32 f_ctl;
	void *buf;

	fh = fc_frame_header_get(fp);
	if (!(ntoh24(fh->fh_f_ctl) & FC_FC_REL_OFF))
		goto drop;

	f_ctl = ntoh24(fh->fh_f_ctl);
	ep = fc_seq_exch(seq);
	lport = ep->lp;
	if (cmd->was_ddp_setup) {
		BUG_ON(!ep);
		BUG_ON(!lport);
		/*
		 * Since DDP (Large Rx offload) was setup for this request,
		 * payload is expected to be copied directly to user buffers.
		 */
		buf = fc_frame_payload_get(fp, 1);
		if (buf)
			pr_err("%s: xid 0x%x, f_ctl 0x%x, cmd->sg %p, "
				"cmd->sg_cnt 0x%x. DDP was setup"
				" hence not expected to receive frame with "
				"payload, Frame will be dropped if"
				"'Sequence Initiative' bit in f_ctl is"
				"not set\n", __func__, ep->xid, f_ctl,
				se_cmd->t_data_sg, se_cmd->t_data_nents);
		/*
		 * Invalidate HW DDP context if it was setup for respective
		 * command. Invalidation of HW DDP context is requited in both
		 * situation (success and error).
		 */
		ft_invl_hw_context(cmd);

		/*
		 * If "Sequence Initiative (TSI)" bit set in f_ctl, means last
		 * write data frame is received successfully where payload is
		 * posted directly to user buffer and only the last frame's
		 * header is posted in receive queue.
		 *
		 * If "Sequence Initiative (TSI)" bit is not set, means error
		 * condition w.r.t. DDP, hence drop the packet and let explict
		 * ABORTS from other end of exchange timer trigger the recovery.
		 */
		if (f_ctl & FC_FC_SEQ_INIT)
			goto last_frame;
		else
			goto drop;
	}

	rel_off = ntohl(fh->fh_parm_offset);
	frame_len = fr_len(fp);
	if (frame_len <= sizeof(*fh))
		goto drop;
	frame_len -= sizeof(*fh);
	from = fc_frame_payload_get(fp, 0);
	if (rel_off >= se_cmd->data_length)
		goto drop;
	if (frame_len + rel_off > se_cmd->data_length)
		frame_len = se_cmd->data_length - rel_off;

	/*
	 * Setup to use first mem list entry, unless no data.
	 */
	BUG_ON(frame_len && !se_cmd->t_data_sg);
	if (frame_len) {
		sg = se_cmd->t_data_sg;
		mem_len = sg->length;
		mem_off = sg->offset;
		page = sg_page(sg);
	}

	while (frame_len) {
		if (!mem_len) {
			sg = sg_next(sg);
			mem_len = sg->length;
			mem_off = sg->offset;
			page = sg_page(sg);
		}
		if (rel_off >= mem_len) {
			rel_off -= mem_len;
			mem_len = 0;
			continue;
		}
		mem_off += rel_off;
		mem_len -= rel_off;
		rel_off = 0;

		tlen = min(mem_len, frame_len);

		to = kmap_atomic(page + (mem_off >> PAGE_SHIFT));
		page_addr = to;
		to += mem_off & ~PAGE_MASK;
		tlen = min(tlen, (size_t)(PAGE_SIZE -
					  (mem_off & ~PAGE_MASK)));
		memcpy(to, from, tlen);
		kunmap_atomic(page_addr);

		from += tlen;
		frame_len -= tlen;
		mem_off += tlen;
		mem_len -= tlen;
		cmd->write_data_len += tlen;
	}
last_frame:
	if (cmd->write_data_len == se_cmd->data_length) {
		INIT_WORK(&cmd->work, ft_execute_work);
		queue_work(cmd->sess->tport->tpg->workqueue, &cmd->work);
	}
drop:
	fc_frame_free(fp);
}

/*
 * Handle and cleanup any HW specific resources if
 * received ABORTS, errors, timeouts.
 */
void ft_invl_hw_context(struct ft_cmd *cmd)
{
	struct fc_seq *seq;
	struct fc_exch *ep = NULL;
	struct fc_lport *lport = NULL;

	BUG_ON(!cmd);
	seq = cmd->seq;

	/* Cleanup the DDP context in HW if DDP was setup */
	if (cmd->was_ddp_setup && seq) {
		ep = fc_seq_exch(seq);
		if (ep) {
			lport = ep->lp;
			if (lport && (ep->xid <= lport->lro_xid)) {
				/*
				 * "ddp_done" trigger invalidation of HW
				 * specific DDP context
				 */
				cmd->write_data_len = lport->tt.ddp_done(lport,
								      ep->xid);

				/*
				 * Resetting same variable to indicate HW's
				 * DDP context has been invalidated to avoid
				 * re_invalidation of same context (context is
				 * identified using ep->xid)
				 */
				cmd->was_ddp_setup = 0;
			}
		}
	}
}
