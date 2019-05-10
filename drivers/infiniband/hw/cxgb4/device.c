/*
 * Copyright (c) 2009-2010 Chelsio, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/debugfs.h>
#include <linux/vmalloc.h>
#include <linux/math64.h>

#include <rdma/ib_verbs.h>

#include "iw_cxgb4.h"

#define DRV_VERSION "0.1"

MODULE_AUTHOR("Steve Wise");
MODULE_DESCRIPTION("Chelsio T4/T5 RDMA Driver");
MODULE_LICENSE("Dual BSD/GPL");

static int allow_db_fc_on_t5;
module_param(allow_db_fc_on_t5, int, 0644);
MODULE_PARM_DESC(allow_db_fc_on_t5,
		 "Allow DB Flow Control on T5 (default = 0)");

static int allow_db_coalescing_on_t5;
module_param(allow_db_coalescing_on_t5, int, 0644);
MODULE_PARM_DESC(allow_db_coalescing_on_t5,
		 "Allow DB Coalescing on T5 (default = 0)");

int c4iw_wr_log = 0;
module_param(c4iw_wr_log, int, 0444);
MODULE_PARM_DESC(c4iw_wr_log, "Enables logging of work request timing data.");

static int c4iw_wr_log_size_order = 12;
module_param(c4iw_wr_log_size_order, int, 0444);
MODULE_PARM_DESC(c4iw_wr_log_size_order,
		 "Number of entries (log2) in the work request timing log.");

static LIST_HEAD(uld_ctx_list);
static DEFINE_MUTEX(dev_mutex);
static struct workqueue_struct *reg_workq;

#define DB_FC_RESUME_SIZE 64
#define DB_FC_RESUME_DELAY 1
#define DB_FC_DRAIN_THRESH 0

static struct dentry *c4iw_debugfs_root;

struct c4iw_debugfs_data {
	struct c4iw_dev *devp;
	char *buf;
	int bufsize;
	int pos;
};

static int count_idrs(int id, void *p, void *data)
{
	int *countp = data;

	*countp = *countp + 1;
	return 0;
}

static ssize_t debugfs_read(struct file *file, char __user *buf, size_t count,
			    loff_t *ppos)
{
	struct c4iw_debugfs_data *d = file->private_data;

	return simple_read_from_buffer(buf, count, ppos, d->buf, d->pos);
}

void c4iw_log_wr_stats(struct t4_wq *wq, struct t4_cqe *cqe)
{
	struct wr_log_entry le;
	int idx;

	if (!wq->rdev->wr_log)
		return;

	idx = (atomic_inc_return(&wq->rdev->wr_log_idx) - 1) &
		(wq->rdev->wr_log_size - 1);
	le.poll_sge_ts = cxgb4_read_sge_timestamp(wq->rdev->lldi.ports[0]);
	le.poll_host_time = ktime_get();
	le.valid = 1;
	le.cqe_sge_ts = CQE_TS(cqe);
	if (SQ_TYPE(cqe)) {
		le.qid = wq->sq.qid;
		le.opcode = CQE_OPCODE(cqe);
		le.post_host_time = wq->sq.sw_sq[wq->sq.cidx].host_time;
		le.post_sge_ts = wq->sq.sw_sq[wq->sq.cidx].sge_ts;
		le.wr_id = CQE_WRID_SQ_IDX(cqe);
	} else {
		le.qid = wq->rq.qid;
		le.opcode = FW_RI_RECEIVE;
		le.post_host_time = wq->rq.sw_rq[wq->rq.cidx].host_time;
		le.post_sge_ts = wq->rq.sw_rq[wq->rq.cidx].sge_ts;
		le.wr_id = CQE_WRID_MSN(cqe);
	}
	wq->rdev->wr_log[idx] = le;
}

static int wr_log_show(struct seq_file *seq, void *v)
{
	struct c4iw_dev *dev = seq->private;
	ktime_t prev_time;
	struct wr_log_entry *lep;
	int prev_time_set = 0;
	int idx, end;

#define ts2ns(ts) div64_u64((ts) * dev->rdev.lldi.cclk_ps, 1000)

	idx = atomic_read(&dev->rdev.wr_log_idx) &
		(dev->rdev.wr_log_size - 1);
	end = idx - 1;
	if (end < 0)
		end = dev->rdev.wr_log_size - 1;
	lep = &dev->rdev.wr_log[idx];
	while (idx != end) {
		if (lep->valid) {
			if (!prev_time_set) {
				prev_time_set = 1;
				prev_time = lep->poll_host_time;
			}
			seq_printf(seq, "%04u: nsec %llu qid %u opcode "
				   "%u %s 0x%x host_wr_delta nsec %llu "
				   "post_sge_ts 0x%llx cqe_sge_ts 0x%llx "
				   "poll_sge_ts 0x%llx post_poll_delta_ns %llu "
				   "cqe_poll_delta_ns %llu\n",
				   idx,
				   ktime_to_ns(ktime_sub(lep->poll_host_time,
							 prev_time)),
				   lep->qid, lep->opcode,
				   lep->opcode == FW_RI_RECEIVE ?
							"msn" : "wrid",
				   lep->wr_id,
				   ktime_to_ns(ktime_sub(lep->poll_host_time,
							 lep->post_host_time)),
				   lep->post_sge_ts, lep->cqe_sge_ts,
				   lep->poll_sge_ts,
				   ts2ns(lep->poll_sge_ts - lep->post_sge_ts),
				   ts2ns(lep->poll_sge_ts - lep->cqe_sge_ts));
			prev_time = lep->poll_host_time;
		}
		idx++;
		if (idx > (dev->rdev.wr_log_size - 1))
			idx = 0;
		lep = &dev->rdev.wr_log[idx];
	}
#undef ts2ns
	return 0;
}

static int wr_log_open(struct inode *inode, struct file *file)
{
	return single_open(file, wr_log_show, inode->i_private);
}

static ssize_t wr_log_clear(struct file *file, const char __user *buf,
			    size_t count, loff_t *pos)
{
	struct c4iw_dev *dev = ((struct seq_file *)file->private_data)->private;
	int i;

	if (dev->rdev.wr_log)
		for (i = 0; i < dev->rdev.wr_log_size; i++)
			dev->rdev.wr_log[i].valid = 0;
	return count;
}

static const struct file_operations wr_log_debugfs_fops = {
	.owner   = THIS_MODULE,
	.open    = wr_log_open,
	.release = single_release,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.write   = wr_log_clear,
};

static struct sockaddr_in zero_sin = {
	.sin_family = AF_INET,
};

static struct sockaddr_in6 zero_sin6 = {
	.sin6_family = AF_INET6,
};

static void set_ep_sin_addrs(struct c4iw_ep *ep,
			     struct sockaddr_in **lsin,
			     struct sockaddr_in **rsin,
			     struct sockaddr_in **m_lsin,
			     struct sockaddr_in **m_rsin)
{
	struct iw_cm_id *id = ep->com.cm_id;

	*m_lsin = (struct sockaddr_in *)&ep->com.local_addr;
	*m_rsin = (struct sockaddr_in *)&ep->com.remote_addr;
	if (id) {
		*lsin = (struct sockaddr_in *)&id->local_addr;
		*rsin = (struct sockaddr_in *)&id->remote_addr;
	} else {
		*lsin = &zero_sin;
		*rsin = &zero_sin;
	}
}

static void set_ep_sin6_addrs(struct c4iw_ep *ep,
			      struct sockaddr_in6 **lsin6,
			      struct sockaddr_in6 **rsin6,
			      struct sockaddr_in6 **m_lsin6,
			      struct sockaddr_in6 **m_rsin6)
{
	struct iw_cm_id *id = ep->com.cm_id;

	*m_lsin6 = (struct sockaddr_in6 *)&ep->com.local_addr;
	*m_rsin6 = (struct sockaddr_in6 *)&ep->com.remote_addr;
	if (id) {
		*lsin6 = (struct sockaddr_in6 *)&id->local_addr;
		*rsin6 = (struct sockaddr_in6 *)&id->remote_addr;
	} else {
		*lsin6 = &zero_sin6;
		*rsin6 = &zero_sin6;
	}
}

static int dump_qp(int id, void *p, void *data)
{
	struct c4iw_qp *qp = p;
	struct c4iw_debugfs_data *qpd = data;
	int space;
	int cc;

	if (id != qp->wq.sq.qid)
		return 0;

	space = qpd->bufsize - qpd->pos - 1;
	if (space == 0)
		return 1;

	if (qp->ep) {
		struct c4iw_ep *ep = qp->ep;

		if (ep->com.local_addr.ss_family == AF_INET) {
			struct sockaddr_in *lsin;
			struct sockaddr_in *rsin;
			struct sockaddr_in *m_lsin;
			struct sockaddr_in *m_rsin;

			set_ep_sin_addrs(ep, &lsin, &rsin, &m_lsin, &m_rsin);
			cc = snprintf(qpd->buf + qpd->pos, space,
				      "rc qp sq id %u %s id %u state %u "
				      "onchip %u ep tid %u state %u "
				      "%pI4:%u/%u->%pI4:%u/%u\n",
				      qp->wq.sq.qid, qp->srq ? "srq" : "rq",
				      qp->srq ? qp->srq->idx : qp->wq.rq.qid,
				      (int)qp->attr.state,
				      qp->wq.sq.flags & T4_SQ_ONCHIP,
				      ep->hwtid, (int)ep->com.state,
				      &lsin->sin_addr, ntohs(lsin->sin_port),
				      ntohs(m_lsin->sin_port),
				      &rsin->sin_addr, ntohs(rsin->sin_port),
				      ntohs(m_rsin->sin_port));
		} else {
			struct sockaddr_in6 *lsin6;
			struct sockaddr_in6 *rsin6;
			struct sockaddr_in6 *m_lsin6;
			struct sockaddr_in6 *m_rsin6;

			set_ep_sin6_addrs(ep, &lsin6, &rsin6, &m_lsin6,
					  &m_rsin6);
			cc = snprintf(qpd->buf + qpd->pos, space,
				      "rc qp sq id %u rq id %u state %u "
				      "onchip %u ep tid %u state %u "
				      "%pI6:%u/%u->%pI6:%u/%u\n",
				      qp->wq.sq.qid, qp->wq.rq.qid,
				      (int)qp->attr.state,
				      qp->wq.sq.flags & T4_SQ_ONCHIP,
				      ep->hwtid, (int)ep->com.state,
				      &lsin6->sin6_addr,
				      ntohs(lsin6->sin6_port),
				      ntohs(m_lsin6->sin6_port),
				      &rsin6->sin6_addr,
				      ntohs(rsin6->sin6_port),
				      ntohs(m_rsin6->sin6_port));
		}
	} else
		cc = snprintf(qpd->buf + qpd->pos, space,
			     "qp sq id %u rq id %u state %u onchip %u\n",
			      qp->wq.sq.qid, qp->wq.rq.qid,
			      (int)qp->attr.state,
			      qp->wq.sq.flags & T4_SQ_ONCHIP);
	if (cc < space)
		qpd->pos += cc;
	return 0;
}

static int qp_release(struct inode *inode, struct file *file)
{
	struct c4iw_debugfs_data *qpd = file->private_data;
	if (!qpd) {
		pr_info("%s null qpd?\n", __func__);
		return 0;
	}
	vfree(qpd->buf);
	kfree(qpd);
	return 0;
}

static int qp_open(struct inode *inode, struct file *file)
{
	struct c4iw_debugfs_data *qpd;
	int count = 1;

	qpd = kmalloc(sizeof *qpd, GFP_KERNEL);
	if (!qpd)
		return -ENOMEM;

	qpd->devp = inode->i_private;
	qpd->pos = 0;

	spin_lock_irq(&qpd->devp->lock);
	idr_for_each(&qpd->devp->qpidr, count_idrs, &count);
	spin_unlock_irq(&qpd->devp->lock);

	qpd->bufsize = count * 180;
	qpd->buf = vmalloc(qpd->bufsize);
	if (!qpd->buf) {
		kfree(qpd);
		return -ENOMEM;
	}

	spin_lock_irq(&qpd->devp->lock);
	idr_for_each(&qpd->devp->qpidr, dump_qp, qpd);
	spin_unlock_irq(&qpd->devp->lock);

	qpd->buf[qpd->pos++] = 0;
	file->private_data = qpd;
	return 0;
}

static const struct file_operations qp_debugfs_fops = {
	.owner   = THIS_MODULE,
	.open    = qp_open,
	.release = qp_release,
	.read    = debugfs_read,
	.llseek  = default_llseek,
};

static int dump_stag(int id, void *p, void *data)
{
	struct c4iw_debugfs_data *stagd = data;
	int space;
	int cc;
	struct fw_ri_tpte tpte;
	int ret;

	space = stagd->bufsize - stagd->pos - 1;
	if (space == 0)
		return 1;

	ret = cxgb4_read_tpte(stagd->devp->rdev.lldi.ports[0], (u32)id<<8,
			      (__be32 *)&tpte);
	if (ret) {
		dev_err(&stagd->devp->rdev.lldi.pdev->dev,
			"%s cxgb4_read_tpte err %d\n", __func__, ret);
		return ret;
	}
	cc = snprintf(stagd->buf + stagd->pos, space,
		      "stag: idx 0x%x valid %d key 0x%x state %d pdid %d "
		      "perm 0x%x ps %d len 0x%llx va 0x%llx\n",
		      (u32)id<<8,
		      FW_RI_TPTE_VALID_G(ntohl(tpte.valid_to_pdid)),
		      FW_RI_TPTE_STAGKEY_G(ntohl(tpte.valid_to_pdid)),
		      FW_RI_TPTE_STAGSTATE_G(ntohl(tpte.valid_to_pdid)),
		      FW_RI_TPTE_PDID_G(ntohl(tpte.valid_to_pdid)),
		      FW_RI_TPTE_PERM_G(ntohl(tpte.locread_to_qpid)),
		      FW_RI_TPTE_PS_G(ntohl(tpte.locread_to_qpid)),
		      ((u64)ntohl(tpte.len_hi) << 32) | ntohl(tpte.len_lo),
		      ((u64)ntohl(tpte.va_hi) << 32) | ntohl(tpte.va_lo_fbo));
	if (cc < space)
		stagd->pos += cc;
	return 0;
}

static int stag_release(struct inode *inode, struct file *file)
{
	struct c4iw_debugfs_data *stagd = file->private_data;
	if (!stagd) {
		pr_info("%s null stagd?\n", __func__);
		return 0;
	}
	vfree(stagd->buf);
	kfree(stagd);
	return 0;
}

static int stag_open(struct inode *inode, struct file *file)
{
	struct c4iw_debugfs_data *stagd;
	int ret = 0;
	int count = 1;

	stagd = kmalloc(sizeof *stagd, GFP_KERNEL);
	if (!stagd) {
		ret = -ENOMEM;
		goto out;
	}
	stagd->devp = inode->i_private;
	stagd->pos = 0;

	spin_lock_irq(&stagd->devp->lock);
	idr_for_each(&stagd->devp->mmidr, count_idrs, &count);
	spin_unlock_irq(&stagd->devp->lock);

	stagd->bufsize = count * 256;
	stagd->buf = vmalloc(stagd->bufsize);
	if (!stagd->buf) {
		ret = -ENOMEM;
		goto err1;
	}

	spin_lock_irq(&stagd->devp->lock);
	idr_for_each(&stagd->devp->mmidr, dump_stag, stagd);
	spin_unlock_irq(&stagd->devp->lock);

	stagd->buf[stagd->pos++] = 0;
	file->private_data = stagd;
	goto out;
err1:
	kfree(stagd);
out:
	return ret;
}

static const struct file_operations stag_debugfs_fops = {
	.owner   = THIS_MODULE,
	.open    = stag_open,
	.release = stag_release,
	.read    = debugfs_read,
	.llseek  = default_llseek,
};

static char *db_state_str[] = {"NORMAL", "FLOW_CONTROL", "RECOVERY", "STOPPED"};

static int stats_show(struct seq_file *seq, void *v)
{
	struct c4iw_dev *dev = seq->private;

	seq_printf(seq, "   Object: %10s %10s %10s %10s\n", "Total", "Current",
		   "Max", "Fail");
	seq_printf(seq, "     PDID: %10llu %10llu %10llu %10llu\n",
			dev->rdev.stats.pd.total, dev->rdev.stats.pd.cur,
			dev->rdev.stats.pd.max, dev->rdev.stats.pd.fail);
	seq_printf(seq, "      QID: %10llu %10llu %10llu %10llu\n",
			dev->rdev.stats.qid.total, dev->rdev.stats.qid.cur,
			dev->rdev.stats.qid.max, dev->rdev.stats.qid.fail);
	seq_printf(seq, "     SRQS: %10llu %10llu %10llu %10llu\n",
		   dev->rdev.stats.srqt.total, dev->rdev.stats.srqt.cur,
			dev->rdev.stats.srqt.max, dev->rdev.stats.srqt.fail);
	seq_printf(seq, "   TPTMEM: %10llu %10llu %10llu %10llu\n",
			dev->rdev.stats.stag.total, dev->rdev.stats.stag.cur,
			dev->rdev.stats.stag.max, dev->rdev.stats.stag.fail);
	seq_printf(seq, "   PBLMEM: %10llu %10llu %10llu %10llu\n",
			dev->rdev.stats.pbl.total, dev->rdev.stats.pbl.cur,
			dev->rdev.stats.pbl.max, dev->rdev.stats.pbl.fail);
	seq_printf(seq, "   RQTMEM: %10llu %10llu %10llu %10llu\n",
			dev->rdev.stats.rqt.total, dev->rdev.stats.rqt.cur,
			dev->rdev.stats.rqt.max, dev->rdev.stats.rqt.fail);
	seq_printf(seq, "  OCQPMEM: %10llu %10llu %10llu %10llu\n",
			dev->rdev.stats.ocqp.total, dev->rdev.stats.ocqp.cur,
			dev->rdev.stats.ocqp.max, dev->rdev.stats.ocqp.fail);
	seq_printf(seq, "  DB FULL: %10llu\n", dev->rdev.stats.db_full);
	seq_printf(seq, " DB EMPTY: %10llu\n", dev->rdev.stats.db_empty);
	seq_printf(seq, "  DB DROP: %10llu\n", dev->rdev.stats.db_drop);
	seq_printf(seq, " DB State: %s Transitions %llu FC Interruptions %llu\n",
		   db_state_str[dev->db_state],
		   dev->rdev.stats.db_state_transitions,
		   dev->rdev.stats.db_fc_interruptions);
	seq_printf(seq, "TCAM_FULL: %10llu\n", dev->rdev.stats.tcam_full);
	seq_printf(seq, "ACT_OFLD_CONN_FAILS: %10llu\n",
		   dev->rdev.stats.act_ofld_conn_fails);
	seq_printf(seq, "PAS_OFLD_CONN_FAILS: %10llu\n",
		   dev->rdev.stats.pas_ofld_conn_fails);
	seq_printf(seq, "NEG_ADV_RCVD: %10llu\n", dev->rdev.stats.neg_adv);
	seq_printf(seq, "AVAILABLE IRD: %10u\n", dev->avail_ird);
	return 0;
}

static int stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, stats_show, inode->i_private);
}

static ssize_t stats_clear(struct file *file, const char __user *buf,
		size_t count, loff_t *pos)
{
	struct c4iw_dev *dev = ((struct seq_file *)file->private_data)->private;

	mutex_lock(&dev->rdev.stats.lock);
	dev->rdev.stats.pd.max = 0;
	dev->rdev.stats.pd.fail = 0;
	dev->rdev.stats.qid.max = 0;
	dev->rdev.stats.qid.fail = 0;
	dev->rdev.stats.stag.max = 0;
	dev->rdev.stats.stag.fail = 0;
	dev->rdev.stats.pbl.max = 0;
	dev->rdev.stats.pbl.fail = 0;
	dev->rdev.stats.rqt.max = 0;
	dev->rdev.stats.rqt.fail = 0;
	dev->rdev.stats.rqt.max = 0;
	dev->rdev.stats.rqt.fail = 0;
	dev->rdev.stats.ocqp.max = 0;
	dev->rdev.stats.ocqp.fail = 0;
	dev->rdev.stats.db_full = 0;
	dev->rdev.stats.db_empty = 0;
	dev->rdev.stats.db_drop = 0;
	dev->rdev.stats.db_state_transitions = 0;
	dev->rdev.stats.tcam_full = 0;
	dev->rdev.stats.act_ofld_conn_fails = 0;
	dev->rdev.stats.pas_ofld_conn_fails = 0;
	mutex_unlock(&dev->rdev.stats.lock);
	return count;
}

static const struct file_operations stats_debugfs_fops = {
	.owner   = THIS_MODULE,
	.open    = stats_open,
	.release = single_release,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.write   = stats_clear,
};

static int dump_ep(int id, void *p, void *data)
{
	struct c4iw_ep *ep = p;
	struct c4iw_debugfs_data *epd = data;
	int space;
	int cc;

	space = epd->bufsize - epd->pos - 1;
	if (space == 0)
		return 1;

	if (ep->com.local_addr.ss_family == AF_INET) {
		struct sockaddr_in *lsin;
		struct sockaddr_in *rsin;
		struct sockaddr_in *m_lsin;
		struct sockaddr_in *m_rsin;

		set_ep_sin_addrs(ep, &lsin, &rsin, &m_lsin, &m_rsin);
		cc = snprintf(epd->buf + epd->pos, space,
			      "ep %p cm_id %p qp %p state %d flags 0x%lx "
			      "history 0x%lx hwtid %d atid %d "
			      "conn_na %u abort_na %u "
			      "%pI4:%d/%d <-> %pI4:%d/%d\n",
			      ep, ep->com.cm_id, ep->com.qp,
			      (int)ep->com.state, ep->com.flags,
			      ep->com.history, ep->hwtid, ep->atid,
			      ep->stats.connect_neg_adv,
			      ep->stats.abort_neg_adv,
			      &lsin->sin_addr, ntohs(lsin->sin_port),
			      ntohs(m_lsin->sin_port),
			      &rsin->sin_addr, ntohs(rsin->sin_port),
			      ntohs(m_rsin->sin_port));
	} else {
		struct sockaddr_in6 *lsin6;
		struct sockaddr_in6 *rsin6;
		struct sockaddr_in6 *m_lsin6;
		struct sockaddr_in6 *m_rsin6;

		set_ep_sin6_addrs(ep, &lsin6, &rsin6, &m_lsin6, &m_rsin6);
		cc = snprintf(epd->buf + epd->pos, space,
			      "ep %p cm_id %p qp %p state %d flags 0x%lx "
			      "history 0x%lx hwtid %d atid %d "
			      "conn_na %u abort_na %u "
			      "%pI6:%d/%d <-> %pI6:%d/%d\n",
			      ep, ep->com.cm_id, ep->com.qp,
			      (int)ep->com.state, ep->com.flags,
			      ep->com.history, ep->hwtid, ep->atid,
			      ep->stats.connect_neg_adv,
			      ep->stats.abort_neg_adv,
			      &lsin6->sin6_addr, ntohs(lsin6->sin6_port),
			      ntohs(m_lsin6->sin6_port),
			      &rsin6->sin6_addr, ntohs(rsin6->sin6_port),
			      ntohs(m_rsin6->sin6_port));
	}
	if (cc < space)
		epd->pos += cc;
	return 0;
}

static int dump_listen_ep(int id, void *p, void *data)
{
	struct c4iw_listen_ep *ep = p;
	struct c4iw_debugfs_data *epd = data;
	int space;
	int cc;

	space = epd->bufsize - epd->pos - 1;
	if (space == 0)
		return 1;

	if (ep->com.local_addr.ss_family == AF_INET) {
		struct sockaddr_in *lsin = (struct sockaddr_in *)
			&ep->com.cm_id->local_addr;
		struct sockaddr_in *m_lsin = (struct sockaddr_in *)
			&ep->com.cm_id->m_local_addr;

		cc = snprintf(epd->buf + epd->pos, space,
			      "ep %p cm_id %p state %d flags 0x%lx stid %d "
			      "backlog %d %pI4:%d/%d\n",
			      ep, ep->com.cm_id, (int)ep->com.state,
			      ep->com.flags, ep->stid, ep->backlog,
			      &lsin->sin_addr, ntohs(lsin->sin_port),
			      ntohs(m_lsin->sin_port));
	} else {
		struct sockaddr_in6 *lsin6 = (struct sockaddr_in6 *)
			&ep->com.cm_id->local_addr;
		struct sockaddr_in6 *m_lsin6 = (struct sockaddr_in6 *)
			&ep->com.cm_id->m_local_addr;

		cc = snprintf(epd->buf + epd->pos, space,
			      "ep %p cm_id %p state %d flags 0x%lx stid %d "
			      "backlog %d %pI6:%d/%d\n",
			      ep, ep->com.cm_id, (int)ep->com.state,
			      ep->com.flags, ep->stid, ep->backlog,
			      &lsin6->sin6_addr, ntohs(lsin6->sin6_port),
			      ntohs(m_lsin6->sin6_port));
	}
	if (cc < space)
		epd->pos += cc;
	return 0;
}

static int ep_release(struct inode *inode, struct file *file)
{
	struct c4iw_debugfs_data *epd = file->private_data;
	if (!epd) {
		pr_info("%s null qpd?\n", __func__);
		return 0;
	}
	vfree(epd->buf);
	kfree(epd);
	return 0;
}

static int ep_open(struct inode *inode, struct file *file)
{
	struct c4iw_debugfs_data *epd;
	int ret = 0;
	int count = 1;

	epd = kmalloc(sizeof(*epd), GFP_KERNEL);
	if (!epd) {
		ret = -ENOMEM;
		goto out;
	}
	epd->devp = inode->i_private;
	epd->pos = 0;

	spin_lock_irq(&epd->devp->lock);
	idr_for_each(&epd->devp->hwtid_idr, count_idrs, &count);
	idr_for_each(&epd->devp->atid_idr, count_idrs, &count);
	idr_for_each(&epd->devp->stid_idr, count_idrs, &count);
	spin_unlock_irq(&epd->devp->lock);

	epd->bufsize = count * 240;
	epd->buf = vmalloc(epd->bufsize);
	if (!epd->buf) {
		ret = -ENOMEM;
		goto err1;
	}

	spin_lock_irq(&epd->devp->lock);
	idr_for_each(&epd->devp->hwtid_idr, dump_ep, epd);
	idr_for_each(&epd->devp->atid_idr, dump_ep, epd);
	idr_for_each(&epd->devp->stid_idr, dump_listen_ep, epd);
	spin_unlock_irq(&epd->devp->lock);

	file->private_data = epd;
	goto out;
err1:
	kfree(epd);
out:
	return ret;
}

static const struct file_operations ep_debugfs_fops = {
	.owner   = THIS_MODULE,
	.open    = ep_open,
	.release = ep_release,
	.read    = debugfs_read,
};

static void setup_debugfs(struct c4iw_dev *devp)
{
	debugfs_create_file_size("qps", S_IWUSR, devp->debugfs_root,
				 (void *)devp, &qp_debugfs_fops, 4096);

	debugfs_create_file_size("stags", S_IWUSR, devp->debugfs_root,
				 (void *)devp, &stag_debugfs_fops, 4096);

	debugfs_create_file_size("stats", S_IWUSR, devp->debugfs_root,
				 (void *)devp, &stats_debugfs_fops, 4096);

	debugfs_create_file_size("eps", S_IWUSR, devp->debugfs_root,
				 (void *)devp, &ep_debugfs_fops, 4096);

	if (c4iw_wr_log)
		debugfs_create_file_size("wr_log", S_IWUSR, devp->debugfs_root,
					 (void *)devp, &wr_log_debugfs_fops, 4096);
}

void c4iw_release_dev_ucontext(struct c4iw_rdev *rdev,
			       struct c4iw_dev_ucontext *uctx)
{
	struct list_head *pos, *nxt;
	struct c4iw_qid_list *entry;

	mutex_lock(&uctx->lock);
	list_for_each_safe(pos, nxt, &uctx->qpids) {
		entry = list_entry(pos, struct c4iw_qid_list, entry);
		list_del_init(&entry->entry);
		if (!(entry->qid & rdev->qpmask)) {
			c4iw_put_resource(&rdev->resource.qid_table,
					  entry->qid);
			mutex_lock(&rdev->stats.lock);
			rdev->stats.qid.cur -= rdev->qpmask + 1;
			mutex_unlock(&rdev->stats.lock);
		}
		kfree(entry);
	}

	list_for_each_safe(pos, nxt, &uctx->cqids) {
		entry = list_entry(pos, struct c4iw_qid_list, entry);
		list_del_init(&entry->entry);
		kfree(entry);
	}
	mutex_unlock(&uctx->lock);
}

void c4iw_init_dev_ucontext(struct c4iw_rdev *rdev,
			    struct c4iw_dev_ucontext *uctx)
{
	INIT_LIST_HEAD(&uctx->qpids);
	INIT_LIST_HEAD(&uctx->cqids);
	mutex_init(&uctx->lock);
}

/* Caller takes care of locking if needed */
static int c4iw_rdev_open(struct c4iw_rdev *rdev)
{
	int err;
	unsigned int factor;

	c4iw_init_dev_ucontext(rdev, &rdev->uctx);

	/*
	 * This implementation assumes udb_density == ucq_density!  Eventually
	 * we might need to support this but for now fail the open. Also the
	 * cqid and qpid range must match for now.
	 */
	if (rdev->lldi.udb_density != rdev->lldi.ucq_density) {
		pr_err("%s: unsupported udb/ucq densities %u/%u\n",
		       pci_name(rdev->lldi.pdev), rdev->lldi.udb_density,
		       rdev->lldi.ucq_density);
		return -EINVAL;
	}
	if (rdev->lldi.vr->qp.start != rdev->lldi.vr->cq.start ||
	    rdev->lldi.vr->qp.size != rdev->lldi.vr->cq.size) {
		pr_err("%s: unsupported qp and cq id ranges qp start %u size %u cq start %u size %u\n",
		       pci_name(rdev->lldi.pdev), rdev->lldi.vr->qp.start,
		       rdev->lldi.vr->qp.size, rdev->lldi.vr->cq.size,
		       rdev->lldi.vr->cq.size);
		return -EINVAL;
	}

	/* This implementation requires a sge_host_page_size <= PAGE_SIZE. */
	if (rdev->lldi.sge_host_page_size > PAGE_SIZE) {
		pr_err("%s: unsupported sge host page size %u\n",
		       pci_name(rdev->lldi.pdev),
		       rdev->lldi.sge_host_page_size);
		return -EINVAL;
	}

	factor = PAGE_SIZE / rdev->lldi.sge_host_page_size;
	rdev->qpmask = (rdev->lldi.udb_density * factor) - 1;
	rdev->cqmask = (rdev->lldi.ucq_density * factor) - 1;

	pr_debug("dev %s stag start 0x%0x size 0x%0x num stags %d pbl start 0x%0x size 0x%0x rq start 0x%0x size 0x%0x qp qid start %u size %u cq qid start %u size %u srq size %u\n",
		 pci_name(rdev->lldi.pdev), rdev->lldi.vr->stag.start,
		 rdev->lldi.vr->stag.size, c4iw_num_stags(rdev),
		 rdev->lldi.vr->pbl.start,
		 rdev->lldi.vr->pbl.size, rdev->lldi.vr->rq.start,
		 rdev->lldi.vr->rq.size,
		 rdev->lldi.vr->qp.start,
		 rdev->lldi.vr->qp.size,
		 rdev->lldi.vr->cq.start,
		 rdev->lldi.vr->cq.size,
		 rdev->lldi.vr->srq.size);
	pr_debug("udb %pR db_reg %p gts_reg %p qpmask 0x%x cqmask 0x%x\n",
		 &rdev->lldi.pdev->resource[2],
		 rdev->lldi.db_reg, rdev->lldi.gts_reg,
		 rdev->qpmask, rdev->cqmask);

	if (c4iw_num_stags(rdev) == 0)
		return -EINVAL;

	rdev->stats.pd.total = T4_MAX_NUM_PD;
	rdev->stats.stag.total = rdev->lldi.vr->stag.size;
	rdev->stats.pbl.total = rdev->lldi.vr->pbl.size;
	rdev->stats.rqt.total = rdev->lldi.vr->rq.size;
	rdev->stats.srqt.total = rdev->lldi.vr->srq.size;
	rdev->stats.ocqp.total = rdev->lldi.vr->ocq.size;
	rdev->stats.qid.total = rdev->lldi.vr->qp.size;

	err = c4iw_init_resource(rdev, c4iw_num_stags(rdev),
				 T4_MAX_NUM_PD, rdev->lldi.vr->srq.size);
	if (err) {
		pr_err("error %d initializing resources\n", err);
		return err;
	}
	err = c4iw_pblpool_create(rdev);
	if (err) {
		pr_err("error %d initializing pbl pool\n", err);
		goto destroy_resource;
	}
	err = c4iw_rqtpool_create(rdev);
	if (err) {
		pr_err("error %d initializing rqt pool\n", err);
		goto destroy_pblpool;
	}
	err = c4iw_ocqp_pool_create(rdev);
	if (err) {
		pr_err("error %d initializing ocqp pool\n", err);
		goto destroy_rqtpool;
	}
	rdev->status_page = (struct t4_dev_status_page *)
			    __get_free_page(GFP_KERNEL);
	if (!rdev->status_page) {
		err = -ENOMEM;
		goto destroy_ocqp_pool;
	}
	rdev->status_page->qp_start = rdev->lldi.vr->qp.start;
	rdev->status_page->qp_size = rdev->lldi.vr->qp.size;
	rdev->status_page->cq_start = rdev->lldi.vr->cq.start;
	rdev->status_page->cq_size = rdev->lldi.vr->cq.size;
	rdev->status_page->write_cmpl_supported = rdev->lldi.write_cmpl_support;

	if (c4iw_wr_log) {
		rdev->wr_log = kcalloc(1 << c4iw_wr_log_size_order,
				       sizeof(*rdev->wr_log),
				       GFP_KERNEL);
		if (rdev->wr_log) {
			rdev->wr_log_size = 1 << c4iw_wr_log_size_order;
			atomic_set(&rdev->wr_log_idx, 0);
		}
	}

	rdev->free_workq = create_singlethread_workqueue("iw_cxgb4_free");
	if (!rdev->free_workq) {
		err = -ENOMEM;
		goto err_free_status_page_and_wr_log;
	}

	rdev->status_page->db_off = 0;

	init_completion(&rdev->rqt_compl);
	init_completion(&rdev->pbl_compl);
	kref_init(&rdev->rqt_kref);
	kref_init(&rdev->pbl_kref);

	return 0;
err_free_status_page_and_wr_log:
	if (c4iw_wr_log && rdev->wr_log)
		kfree(rdev->wr_log);
	free_page((unsigned long)rdev->status_page);
destroy_ocqp_pool:
	c4iw_ocqp_pool_destroy(rdev);
destroy_rqtpool:
	c4iw_rqtpool_destroy(rdev);
destroy_pblpool:
	c4iw_pblpool_destroy(rdev);
destroy_resource:
	c4iw_destroy_resource(&rdev->resource);
	return err;
}

static void c4iw_rdev_close(struct c4iw_rdev *rdev)
{
	kfree(rdev->wr_log);
	c4iw_release_dev_ucontext(rdev, &rdev->uctx);
	free_page((unsigned long)rdev->status_page);
	c4iw_pblpool_destroy(rdev);
	c4iw_rqtpool_destroy(rdev);
	wait_for_completion(&rdev->pbl_compl);
	wait_for_completion(&rdev->rqt_compl);
	c4iw_ocqp_pool_destroy(rdev);
	destroy_workqueue(rdev->free_workq);
	c4iw_destroy_resource(&rdev->resource);
}

void c4iw_dealloc(struct uld_ctx *ctx)
{
	c4iw_rdev_close(&ctx->dev->rdev);
	WARN_ON_ONCE(!idr_is_empty(&ctx->dev->cqidr));
	idr_destroy(&ctx->dev->cqidr);
	WARN_ON_ONCE(!idr_is_empty(&ctx->dev->qpidr));
	idr_destroy(&ctx->dev->qpidr);
	WARN_ON_ONCE(!idr_is_empty(&ctx->dev->mmidr));
	idr_destroy(&ctx->dev->mmidr);
	wait_event(ctx->dev->wait, idr_is_empty(&ctx->dev->hwtid_idr));
	idr_destroy(&ctx->dev->hwtid_idr);
	idr_destroy(&ctx->dev->stid_idr);
	idr_destroy(&ctx->dev->atid_idr);
	if (ctx->dev->rdev.bar2_kva)
		iounmap(ctx->dev->rdev.bar2_kva);
	if (ctx->dev->rdev.oc_mw_kva)
		iounmap(ctx->dev->rdev.oc_mw_kva);
	ib_dealloc_device(&ctx->dev->ibdev);
	ctx->dev = NULL;
}

static void c4iw_remove(struct uld_ctx *ctx)
{
	pr_debug("c4iw_dev %p\n", ctx->dev);
	c4iw_unregister_device(ctx->dev);
	c4iw_dealloc(ctx);
}

static int rdma_supported(const struct cxgb4_lld_info *infop)
{
	return infop->vr->stag.size > 0 && infop->vr->pbl.size > 0 &&
	       infop->vr->rq.size > 0 && infop->vr->qp.size > 0 &&
	       infop->vr->cq.size > 0;
}

static struct c4iw_dev *c4iw_alloc(const struct cxgb4_lld_info *infop)
{
	struct c4iw_dev *devp;
	int ret;

	if (!rdma_supported(infop)) {
		pr_info("%s: RDMA not supported on this device\n",
			pci_name(infop->pdev));
		return ERR_PTR(-ENOSYS);
	}
	if (!ocqp_supported(infop))
		pr_info("%s: On-Chip Queues not supported on this device\n",
			pci_name(infop->pdev));

	devp = ib_alloc_device(c4iw_dev, ibdev);
	if (!devp) {
		pr_err("Cannot allocate ib device\n");
		return ERR_PTR(-ENOMEM);
	}
	devp->rdev.lldi = *infop;

	/* init various hw-queue params based on lld info */
	pr_debug("Ing. padding boundary is %d, egrsstatuspagesize = %d\n",
		 devp->rdev.lldi.sge_ingpadboundary,
		 devp->rdev.lldi.sge_egrstatuspagesize);

	devp->rdev.hw_queue.t4_eq_status_entries =
		devp->rdev.lldi.sge_egrstatuspagesize / 64;
	devp->rdev.hw_queue.t4_max_eq_size = 65520;
	devp->rdev.hw_queue.t4_max_iq_size = 65520;
	devp->rdev.hw_queue.t4_max_rq_size = 8192 -
		devp->rdev.hw_queue.t4_eq_status_entries - 1;
	devp->rdev.hw_queue.t4_max_sq_size =
		devp->rdev.hw_queue.t4_max_eq_size -
		devp->rdev.hw_queue.t4_eq_status_entries - 1;
	devp->rdev.hw_queue.t4_max_qp_depth =
		devp->rdev.hw_queue.t4_max_rq_size;
	devp->rdev.hw_queue.t4_max_cq_depth =
		devp->rdev.hw_queue.t4_max_iq_size - 2;
	devp->rdev.hw_queue.t4_stat_len =
		devp->rdev.lldi.sge_egrstatuspagesize;

	/*
	 * For T5/T6 devices, we map all of BAR2 with WC.
	 * For T4 devices with onchip qp mem, we map only that part
	 * of BAR2 with WC.
	 */
	devp->rdev.bar2_pa = pci_resource_start(devp->rdev.lldi.pdev, 2);
	if (!is_t4(devp->rdev.lldi.adapter_type)) {
		devp->rdev.bar2_kva = ioremap_wc(devp->rdev.bar2_pa,
			pci_resource_len(devp->rdev.lldi.pdev, 2));
		if (!devp->rdev.bar2_kva) {
			pr_err("Unable to ioremap BAR2\n");
			ib_dealloc_device(&devp->ibdev);
			return ERR_PTR(-EINVAL);
		}
	} else if (ocqp_supported(infop)) {
		devp->rdev.oc_mw_pa =
			pci_resource_start(devp->rdev.lldi.pdev, 2) +
			pci_resource_len(devp->rdev.lldi.pdev, 2) -
			roundup_pow_of_two(devp->rdev.lldi.vr->ocq.size);
		devp->rdev.oc_mw_kva = ioremap_wc(devp->rdev.oc_mw_pa,
			devp->rdev.lldi.vr->ocq.size);
		if (!devp->rdev.oc_mw_kva) {
			pr_err("Unable to ioremap onchip mem\n");
			ib_dealloc_device(&devp->ibdev);
			return ERR_PTR(-EINVAL);
		}
	}

	pr_debug("ocq memory: hw_start 0x%x size %u mw_pa 0x%lx mw_kva %p\n",
		 devp->rdev.lldi.vr->ocq.start, devp->rdev.lldi.vr->ocq.size,
		 devp->rdev.oc_mw_pa, devp->rdev.oc_mw_kva);

	ret = c4iw_rdev_open(&devp->rdev);
	if (ret) {
		pr_err("Unable to open CXIO rdev err %d\n", ret);
		ib_dealloc_device(&devp->ibdev);
		return ERR_PTR(ret);
	}

	idr_init(&devp->cqidr);
	idr_init(&devp->qpidr);
	idr_init(&devp->mmidr);
	idr_init(&devp->hwtid_idr);
	idr_init(&devp->stid_idr);
	idr_init(&devp->atid_idr);
	spin_lock_init(&devp->lock);
	mutex_init(&devp->rdev.stats.lock);
	mutex_init(&devp->db_mutex);
	INIT_LIST_HEAD(&devp->db_fc_list);
	init_waitqueue_head(&devp->wait);
	devp->avail_ird = devp->rdev.lldi.max_ird_adapter;

	if (c4iw_debugfs_root) {
		devp->debugfs_root = debugfs_create_dir(
					pci_name(devp->rdev.lldi.pdev),
					c4iw_debugfs_root);
		setup_debugfs(devp);
	}


	return devp;
}

static void *c4iw_uld_add(const struct cxgb4_lld_info *infop)
{
	struct uld_ctx *ctx;
	static int vers_printed;
	int i;

	if (!vers_printed++)
		pr_info("Chelsio T4/T5 RDMA Driver - version %s\n",
			DRV_VERSION);

	ctx = kzalloc(sizeof *ctx, GFP_KERNEL);
	if (!ctx) {
		ctx = ERR_PTR(-ENOMEM);
		goto out;
	}
	ctx->lldi = *infop;

	pr_debug("found device %s nchan %u nrxq %u ntxq %u nports %u\n",
		 pci_name(ctx->lldi.pdev),
		 ctx->lldi.nchan, ctx->lldi.nrxq,
		 ctx->lldi.ntxq, ctx->lldi.nports);

	mutex_lock(&dev_mutex);
	list_add_tail(&ctx->entry, &uld_ctx_list);
	mutex_unlock(&dev_mutex);

	for (i = 0; i < ctx->lldi.nrxq; i++)
		pr_debug("rxqid[%u] %u\n", i, ctx->lldi.rxq_ids[i]);
out:
	return ctx;
}

static inline struct sk_buff *copy_gl_to_skb_pkt(const struct pkt_gl *gl,
						 const __be64 *rsp,
						 u32 pktshift)
{
	struct sk_buff *skb;

	/*
	 * Allocate space for cpl_pass_accept_req which will be synthesized by
	 * driver. Once the driver synthesizes the request the skb will go
	 * through the regular cpl_pass_accept_req processing.
	 * The math here assumes sizeof cpl_pass_accept_req >= sizeof
	 * cpl_rx_pkt.
	 */
	skb = alloc_skb(gl->tot_len + sizeof(struct cpl_pass_accept_req) +
			sizeof(struct rss_header) - pktshift, GFP_ATOMIC);
	if (unlikely(!skb))
		return NULL;

	__skb_put(skb, gl->tot_len + sizeof(struct cpl_pass_accept_req) +
		  sizeof(struct rss_header) - pktshift);

	/*
	 * This skb will contain:
	 *   rss_header from the rspq descriptor (1 flit)
	 *   cpl_rx_pkt struct from the rspq descriptor (2 flits)
	 *   space for the difference between the size of an
	 *      rx_pkt and pass_accept_req cpl (1 flit)
	 *   the packet data from the gl
	 */
	skb_copy_to_linear_data(skb, rsp, sizeof(struct cpl_pass_accept_req) +
				sizeof(struct rss_header));
	skb_copy_to_linear_data_offset(skb, sizeof(struct rss_header) +
				       sizeof(struct cpl_pass_accept_req),
				       gl->va + pktshift,
				       gl->tot_len - pktshift);
	return skb;
}

static inline int recv_rx_pkt(struct c4iw_dev *dev, const struct pkt_gl *gl,
			   const __be64 *rsp)
{
	unsigned int opcode = *(u8 *)rsp;
	struct sk_buff *skb;

	if (opcode != CPL_RX_PKT)
		goto out;

	skb = copy_gl_to_skb_pkt(gl , rsp, dev->rdev.lldi.sge_pktshift);
	if (skb == NULL)
		goto out;

	if (c4iw_handlers[opcode] == NULL) {
		pr_info("%s no handler opcode 0x%x...\n", __func__, opcode);
		kfree_skb(skb);
		goto out;
	}
	c4iw_handlers[opcode](dev, skb);
	return 1;
out:
	return 0;
}

static int c4iw_uld_rx_handler(void *handle, const __be64 *rsp,
			const struct pkt_gl *gl)
{
	struct uld_ctx *ctx = handle;
	struct c4iw_dev *dev = ctx->dev;
	struct sk_buff *skb;
	u8 opcode;

	if (gl == NULL) {
		/* omit RSS and rsp_ctrl at end of descriptor */
		unsigned int len = 64 - sizeof(struct rsp_ctrl) - 8;

		skb = alloc_skb(256, GFP_ATOMIC);
		if (!skb)
			goto nomem;
		__skb_put(skb, len);
		skb_copy_to_linear_data(skb, &rsp[1], len);
	} else if (gl == CXGB4_MSG_AN) {
		const struct rsp_ctrl *rc = (void *)rsp;

		u32 qid = be32_to_cpu(rc->pldbuflen_qid);
		c4iw_ev_handler(dev, qid);
		return 0;
	} else if (unlikely(*(u8 *)rsp != *(u8 *)gl->va)) {
		if (recv_rx_pkt(dev, gl, rsp))
			return 0;

		pr_info("%s: unexpected FL contents at %p, RSS %#llx, FL %#llx, len %u\n",
			pci_name(ctx->lldi.pdev), gl->va,
			be64_to_cpu(*rsp),
			be64_to_cpu(*(__force __be64 *)gl->va),
			gl->tot_len);

		return 0;
	} else {
		skb = cxgb4_pktgl_to_skb(gl, 128, 128);
		if (unlikely(!skb))
			goto nomem;
	}

	opcode = *(u8 *)rsp;
	if (c4iw_handlers[opcode]) {
		c4iw_handlers[opcode](dev, skb);
	} else {
		pr_info("%s no handler opcode 0x%x...\n", __func__, opcode);
		kfree_skb(skb);
	}

	return 0;
nomem:
	return -1;
}

static int c4iw_uld_state_change(void *handle, enum cxgb4_state new_state)
{
	struct uld_ctx *ctx = handle;

	pr_debug("new_state %u\n", new_state);
	switch (new_state) {
	case CXGB4_STATE_UP:
		pr_info("%s: Up\n", pci_name(ctx->lldi.pdev));
		if (!ctx->dev) {
			ctx->dev = c4iw_alloc(&ctx->lldi);
			if (IS_ERR(ctx->dev)) {
				pr_err("%s: initialization failed: %ld\n",
				       pci_name(ctx->lldi.pdev),
				       PTR_ERR(ctx->dev));
				ctx->dev = NULL;
				break;
			}

			INIT_WORK(&ctx->reg_work, c4iw_register_device);
			queue_work(reg_workq, &ctx->reg_work);
		}
		break;
	case CXGB4_STATE_DOWN:
		pr_info("%s: Down\n", pci_name(ctx->lldi.pdev));
		if (ctx->dev)
			c4iw_remove(ctx);
		break;
	case CXGB4_STATE_FATAL_ERROR:
	case CXGB4_STATE_START_RECOVERY:
		pr_info("%s: Fatal Error\n", pci_name(ctx->lldi.pdev));
		if (ctx->dev) {
			struct ib_event event;

			ctx->dev->rdev.flags |= T4_FATAL_ERROR;
			memset(&event, 0, sizeof event);
			event.event  = IB_EVENT_DEVICE_FATAL;
			event.device = &ctx->dev->ibdev;
			ib_dispatch_event(&event);
			c4iw_remove(ctx);
		}
		break;
	case CXGB4_STATE_DETACH:
		pr_info("%s: Detach\n", pci_name(ctx->lldi.pdev));
		if (ctx->dev)
			c4iw_remove(ctx);
		break;
	}
	return 0;
}

static int disable_qp_db(int id, void *p, void *data)
{
	struct c4iw_qp *qp = p;

	t4_disable_wq_db(&qp->wq);
	return 0;
}

static void stop_queues(struct uld_ctx *ctx)
{
	unsigned long flags;

	spin_lock_irqsave(&ctx->dev->lock, flags);
	ctx->dev->rdev.stats.db_state_transitions++;
	ctx->dev->db_state = STOPPED;
	if (ctx->dev->rdev.flags & T4_STATUS_PAGE_DISABLED)
		idr_for_each(&ctx->dev->qpidr, disable_qp_db, NULL);
	else
		ctx->dev->rdev.status_page->db_off = 1;
	spin_unlock_irqrestore(&ctx->dev->lock, flags);
}

static int enable_qp_db(int id, void *p, void *data)
{
	struct c4iw_qp *qp = p;

	t4_enable_wq_db(&qp->wq);
	return 0;
}

static void resume_rc_qp(struct c4iw_qp *qp)
{
	spin_lock(&qp->lock);
	t4_ring_sq_db(&qp->wq, qp->wq.sq.wq_pidx_inc, NULL);
	qp->wq.sq.wq_pidx_inc = 0;
	t4_ring_rq_db(&qp->wq, qp->wq.rq.wq_pidx_inc, NULL);
	qp->wq.rq.wq_pidx_inc = 0;
	spin_unlock(&qp->lock);
}

static void resume_a_chunk(struct uld_ctx *ctx)
{
	int i;
	struct c4iw_qp *qp;

	for (i = 0; i < DB_FC_RESUME_SIZE; i++) {
		qp = list_first_entry(&ctx->dev->db_fc_list, struct c4iw_qp,
				      db_fc_entry);
		list_del_init(&qp->db_fc_entry);
		resume_rc_qp(qp);
		if (list_empty(&ctx->dev->db_fc_list))
			break;
	}
}

static void resume_queues(struct uld_ctx *ctx)
{
	spin_lock_irq(&ctx->dev->lock);
	if (ctx->dev->db_state != STOPPED)
		goto out;
	ctx->dev->db_state = FLOW_CONTROL;
	while (1) {
		if (list_empty(&ctx->dev->db_fc_list)) {
			WARN_ON(ctx->dev->db_state != FLOW_CONTROL);
			ctx->dev->db_state = NORMAL;
			ctx->dev->rdev.stats.db_state_transitions++;
			if (ctx->dev->rdev.flags & T4_STATUS_PAGE_DISABLED) {
				idr_for_each(&ctx->dev->qpidr, enable_qp_db,
					     NULL);
			} else {
				ctx->dev->rdev.status_page->db_off = 0;
			}
			break;
		} else {
			if (cxgb4_dbfifo_count(ctx->dev->rdev.lldi.ports[0], 1)
			    < (ctx->dev->rdev.lldi.dbfifo_int_thresh <<
			       DB_FC_DRAIN_THRESH)) {
				resume_a_chunk(ctx);
			}
			if (!list_empty(&ctx->dev->db_fc_list)) {
				spin_unlock_irq(&ctx->dev->lock);
				if (DB_FC_RESUME_DELAY) {
					set_current_state(TASK_UNINTERRUPTIBLE);
					schedule_timeout(DB_FC_RESUME_DELAY);
				}
				spin_lock_irq(&ctx->dev->lock);
				if (ctx->dev->db_state != FLOW_CONTROL)
					break;
			}
		}
	}
out:
	if (ctx->dev->db_state != NORMAL)
		ctx->dev->rdev.stats.db_fc_interruptions++;
	spin_unlock_irq(&ctx->dev->lock);
}

struct qp_list {
	unsigned idx;
	struct c4iw_qp **qps;
};

static int add_and_ref_qp(int id, void *p, void *data)
{
	struct qp_list *qp_listp = data;
	struct c4iw_qp *qp = p;

	c4iw_qp_add_ref(&qp->ibqp);
	qp_listp->qps[qp_listp->idx++] = qp;
	return 0;
}

static int count_qps(int id, void *p, void *data)
{
	unsigned *countp = data;
	(*countp)++;
	return 0;
}

static void deref_qps(struct qp_list *qp_list)
{
	int idx;

	for (idx = 0; idx < qp_list->idx; idx++)
		c4iw_qp_rem_ref(&qp_list->qps[idx]->ibqp);
}

static void recover_lost_dbs(struct uld_ctx *ctx, struct qp_list *qp_list)
{
	int idx;
	int ret;

	for (idx = 0; idx < qp_list->idx; idx++) {
		struct c4iw_qp *qp = qp_list->qps[idx];

		spin_lock_irq(&qp->rhp->lock);
		spin_lock(&qp->lock);
		ret = cxgb4_sync_txq_pidx(qp->rhp->rdev.lldi.ports[0],
					  qp->wq.sq.qid,
					  t4_sq_host_wq_pidx(&qp->wq),
					  t4_sq_wq_size(&qp->wq));
		if (ret) {
			pr_err("%s: Fatal error - DB overflow recovery failed - error syncing SQ qid %u\n",
			       pci_name(ctx->lldi.pdev), qp->wq.sq.qid);
			spin_unlock(&qp->lock);
			spin_unlock_irq(&qp->rhp->lock);
			return;
		}
		qp->wq.sq.wq_pidx_inc = 0;

		ret = cxgb4_sync_txq_pidx(qp->rhp->rdev.lldi.ports[0],
					  qp->wq.rq.qid,
					  t4_rq_host_wq_pidx(&qp->wq),
					  t4_rq_wq_size(&qp->wq));

		if (ret) {
			pr_err("%s: Fatal error - DB overflow recovery failed - error syncing RQ qid %u\n",
			       pci_name(ctx->lldi.pdev), qp->wq.rq.qid);
			spin_unlock(&qp->lock);
			spin_unlock_irq(&qp->rhp->lock);
			return;
		}
		qp->wq.rq.wq_pidx_inc = 0;
		spin_unlock(&qp->lock);
		spin_unlock_irq(&qp->rhp->lock);

		/* Wait for the dbfifo to drain */
		while (cxgb4_dbfifo_count(qp->rhp->rdev.lldi.ports[0], 1) > 0) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(usecs_to_jiffies(10));
		}
	}
}

static void recover_queues(struct uld_ctx *ctx)
{
	int count = 0;
	struct qp_list qp_list;
	int ret;

	/* slow everybody down */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(usecs_to_jiffies(1000));

	/* flush the SGE contexts */
	ret = cxgb4_flush_eq_cache(ctx->dev->rdev.lldi.ports[0]);
	if (ret) {
		pr_err("%s: Fatal error - DB overflow recovery failed\n",
		       pci_name(ctx->lldi.pdev));
		return;
	}

	/* Count active queues so we can build a list of queues to recover */
	spin_lock_irq(&ctx->dev->lock);
	WARN_ON(ctx->dev->db_state != STOPPED);
	ctx->dev->db_state = RECOVERY;
	idr_for_each(&ctx->dev->qpidr, count_qps, &count);

	qp_list.qps = kcalloc(count, sizeof(*qp_list.qps), GFP_ATOMIC);
	if (!qp_list.qps) {
		spin_unlock_irq(&ctx->dev->lock);
		return;
	}
	qp_list.idx = 0;

	/* add and ref each qp so it doesn't get freed */
	idr_for_each(&ctx->dev->qpidr, add_and_ref_qp, &qp_list);

	spin_unlock_irq(&ctx->dev->lock);

	/* now traverse the list in a safe context to recover the db state*/
	recover_lost_dbs(ctx, &qp_list);

	/* we're almost done!  deref the qps and clean up */
	deref_qps(&qp_list);
	kfree(qp_list.qps);

	spin_lock_irq(&ctx->dev->lock);
	WARN_ON(ctx->dev->db_state != RECOVERY);
	ctx->dev->db_state = STOPPED;
	spin_unlock_irq(&ctx->dev->lock);
}

static int c4iw_uld_control(void *handle, enum cxgb4_control control, ...)
{
	struct uld_ctx *ctx = handle;

	switch (control) {
	case CXGB4_CONTROL_DB_FULL:
		stop_queues(ctx);
		ctx->dev->rdev.stats.db_full++;
		break;
	case CXGB4_CONTROL_DB_EMPTY:
		resume_queues(ctx);
		mutex_lock(&ctx->dev->rdev.stats.lock);
		ctx->dev->rdev.stats.db_empty++;
		mutex_unlock(&ctx->dev->rdev.stats.lock);
		break;
	case CXGB4_CONTROL_DB_DROP:
		recover_queues(ctx);
		mutex_lock(&ctx->dev->rdev.stats.lock);
		ctx->dev->rdev.stats.db_drop++;
		mutex_unlock(&ctx->dev->rdev.stats.lock);
		break;
	default:
		pr_warn("%s: unknown control cmd %u\n",
			pci_name(ctx->lldi.pdev), control);
		break;
	}
	return 0;
}

static struct cxgb4_uld_info c4iw_uld_info = {
	.name = DRV_NAME,
	.nrxq = MAX_ULD_QSETS,
	.ntxq = MAX_ULD_QSETS,
	.rxq_size = 511,
	.ciq = true,
	.lro = false,
	.add = c4iw_uld_add,
	.rx_handler = c4iw_uld_rx_handler,
	.state_change = c4iw_uld_state_change,
	.control = c4iw_uld_control,
};

void _c4iw_free_wr_wait(struct kref *kref)
{
	struct c4iw_wr_wait *wr_waitp;

	wr_waitp = container_of(kref, struct c4iw_wr_wait, kref);
	pr_debug("Free wr_wait %p\n", wr_waitp);
	kfree(wr_waitp);
}

struct c4iw_wr_wait *c4iw_alloc_wr_wait(gfp_t gfp)
{
	struct c4iw_wr_wait *wr_waitp;

	wr_waitp = kzalloc(sizeof(*wr_waitp), gfp);
	if (wr_waitp) {
		kref_init(&wr_waitp->kref);
		pr_debug("wr_wait %p\n", wr_waitp);
	}
	return wr_waitp;
}

static int __init c4iw_init_module(void)
{
	int err;

	err = c4iw_cm_init();
	if (err)
		return err;

	c4iw_debugfs_root = debugfs_create_dir(DRV_NAME, NULL);

	reg_workq = create_singlethread_workqueue("Register_iWARP_device");
	if (!reg_workq) {
		pr_err("Failed creating workqueue to register iwarp device\n");
		return -ENOMEM;
	}

	cxgb4_register_uld(CXGB4_ULD_RDMA, &c4iw_uld_info);

	return 0;
}

static void __exit c4iw_exit_module(void)
{
	struct uld_ctx *ctx, *tmp;

	mutex_lock(&dev_mutex);
	list_for_each_entry_safe(ctx, tmp, &uld_ctx_list, entry) {
		if (ctx->dev)
			c4iw_remove(ctx);
		kfree(ctx);
	}
	mutex_unlock(&dev_mutex);
	flush_workqueue(reg_workq);
	destroy_workqueue(reg_workq);
	cxgb4_unregister_uld(CXGB4_ULD_RDMA);
	c4iw_cm_term();
	debugfs_remove_recursive(c4iw_debugfs_root);
}

module_init(c4iw_init_module);
module_exit(c4iw_exit_module);
