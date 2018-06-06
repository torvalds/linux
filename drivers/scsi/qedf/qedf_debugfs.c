/*
 *  QLogic FCoE Offload Driver
 *  Copyright (c) 2016-2017 QLogic Corporation
 *
 *  This software is available under the terms of the GNU General Public License
 *  (GPL) Version 2, available from the file COPYING in the main directory of
 *  this source tree.
 */
#ifdef CONFIG_DEBUG_FS

#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/module.h>

#include "qedf.h"
#include "qedf_dbg.h"

static struct dentry *qedf_dbg_root;

/**
 * qedf_dbg_host_init - setup the debugfs file for the pf
 * @pf: the pf that is starting up
 **/
void
qedf_dbg_host_init(struct qedf_dbg_ctx *qedf,
		    const struct qedf_debugfs_ops *dops,
		    const struct file_operations *fops)
{
	char host_dirname[32];
	struct dentry *file_dentry = NULL;

	QEDF_INFO(qedf, QEDF_LOG_DEBUGFS, "Creating debugfs host node\n");
	/* create pf dir */
	sprintf(host_dirname, "host%u", qedf->host_no);
	qedf->bdf_dentry = debugfs_create_dir(host_dirname, qedf_dbg_root);
	if (!qedf->bdf_dentry)
		return;

	/* create debugfs files */
	while (dops) {
		if (!(dops->name))
			break;

		file_dentry = debugfs_create_file(dops->name, 0600,
						  qedf->bdf_dentry, qedf,
						  fops);
		if (!file_dentry) {
			QEDF_INFO(qedf, QEDF_LOG_DEBUGFS,
				   "Debugfs entry %s creation failed\n",
				   dops->name);
			debugfs_remove_recursive(qedf->bdf_dentry);
			return;
		}
		dops++;
		fops++;
	}
}

/**
 * qedf_dbg_host_exit - clear out the pf's debugfs entries
 * @pf: the pf that is stopping
 **/
void
qedf_dbg_host_exit(struct qedf_dbg_ctx *qedf)
{
	QEDF_INFO(qedf, QEDF_LOG_DEBUGFS, "Destroying debugfs host "
		   "entry\n");
	/* remove debugfs  entries of this PF */
	debugfs_remove_recursive(qedf->bdf_dentry);
	qedf->bdf_dentry = NULL;
}

/**
 * qedf_dbg_init - start up debugfs for the driver
 **/
void
qedf_dbg_init(char *drv_name)
{
	QEDF_INFO(NULL, QEDF_LOG_DEBUGFS, "Creating debugfs root node\n");

	/* create qed dir in root of debugfs. NULL means debugfs root */
	qedf_dbg_root = debugfs_create_dir(drv_name, NULL);
	if (!qedf_dbg_root)
		QEDF_INFO(NULL, QEDF_LOG_DEBUGFS, "Init of debugfs "
			   "failed\n");
}

/**
 * qedf_dbg_exit - clean out the driver's debugfs entries
 **/
void
qedf_dbg_exit(void)
{
	QEDF_INFO(NULL, QEDF_LOG_DEBUGFS, "Destroying debugfs root "
		   "entry\n");

	/* remove qed dir in root of debugfs */
	debugfs_remove_recursive(qedf_dbg_root);
	qedf_dbg_root = NULL;
}

const struct qedf_debugfs_ops qedf_debugfs_ops[] = {
	{ "fp_int", NULL },
	{ "io_trace", NULL },
	{ "debug", NULL },
	{ "stop_io_on_error", NULL},
	{ "driver_stats", NULL},
	{ "clear_stats", NULL},
	{ "offload_stats", NULL},
	/* This must be last */
	{ NULL, NULL }
};

DECLARE_PER_CPU(struct qedf_percpu_iothread_s, qedf_percpu_iothreads);

static ssize_t
qedf_dbg_fp_int_cmd_read(struct file *filp, char __user *buffer, size_t count,
			 loff_t *ppos)
{
	size_t cnt = 0;
	int id;
	struct qedf_fastpath *fp = NULL;
	struct qedf_dbg_ctx *qedf_dbg =
				(struct qedf_dbg_ctx *)filp->private_data;
	struct qedf_ctx *qedf = container_of(qedf_dbg,
	    struct qedf_ctx, dbg_ctx);

	QEDF_INFO(qedf_dbg, QEDF_LOG_DEBUGFS, "entered\n");

	cnt = sprintf(buffer, "\nFastpath I/O completions\n\n");

	for (id = 0; id < qedf->num_queues; id++) {
		fp = &(qedf->fp_array[id]);
		if (fp->sb_id == QEDF_SB_ID_NULL)
			continue;
		cnt += sprintf((buffer + cnt), "#%d: %lu\n", id,
			       fp->completions);
	}

	cnt = min_t(int, count, cnt - *ppos);
	*ppos += cnt;
	return cnt;
}

static ssize_t
qedf_dbg_fp_int_cmd_write(struct file *filp, const char __user *buffer,
			  size_t count, loff_t *ppos)
{
	if (!count || *ppos)
		return 0;

	return count;
}

static ssize_t
qedf_dbg_debug_cmd_read(struct file *filp, char __user *buffer, size_t count,
			loff_t *ppos)
{
	int cnt;
	struct qedf_dbg_ctx *qedf =
				(struct qedf_dbg_ctx *)filp->private_data;

	QEDF_INFO(qedf, QEDF_LOG_DEBUGFS, "entered\n");
	cnt = sprintf(buffer, "debug mask = 0x%x\n", qedf_debug);

	cnt = min_t(int, count, cnt - *ppos);
	*ppos += cnt;
	return cnt;
}

static ssize_t
qedf_dbg_debug_cmd_write(struct file *filp, const char __user *buffer,
			 size_t count, loff_t *ppos)
{
	uint32_t val;
	void *kern_buf;
	int rval;
	struct qedf_dbg_ctx *qedf =
	    (struct qedf_dbg_ctx *)filp->private_data;

	if (!count || *ppos)
		return 0;

	kern_buf = memdup_user(buffer, count);
	if (IS_ERR(kern_buf))
		return PTR_ERR(kern_buf);

	rval = kstrtouint(kern_buf, 10, &val);
	kfree(kern_buf);
	if (rval)
		return rval;

	if (val == 1)
		qedf_debug = QEDF_DEFAULT_LOG_MASK;
	else
		qedf_debug = val;

	QEDF_INFO(qedf, QEDF_LOG_DEBUGFS, "Setting debug=0x%x.\n", val);
	return count;
}

static ssize_t
qedf_dbg_stop_io_on_error_cmd_read(struct file *filp, char __user *buffer,
				   size_t count, loff_t *ppos)
{
	int cnt;
	struct qedf_dbg_ctx *qedf_dbg =
				(struct qedf_dbg_ctx *)filp->private_data;
	struct qedf_ctx *qedf = container_of(qedf_dbg,
	    struct qedf_ctx, dbg_ctx);

	QEDF_INFO(qedf_dbg, QEDF_LOG_DEBUGFS, "entered\n");
	cnt = sprintf(buffer, "%s\n",
	    qedf->stop_io_on_error ? "true" : "false");

	cnt = min_t(int, count, cnt - *ppos);
	*ppos += cnt;
	return cnt;
}

static ssize_t
qedf_dbg_stop_io_on_error_cmd_write(struct file *filp,
				    const char __user *buffer, size_t count,
				    loff_t *ppos)
{
	void *kern_buf;
	struct qedf_dbg_ctx *qedf_dbg =
				(struct qedf_dbg_ctx *)filp->private_data;
	struct qedf_ctx *qedf = container_of(qedf_dbg, struct qedf_ctx,
	    dbg_ctx);

	QEDF_INFO(qedf_dbg, QEDF_LOG_DEBUGFS, "entered\n");

	if (!count || *ppos)
		return 0;

	kern_buf = memdup_user(buffer, 6);
	if (IS_ERR(kern_buf))
		return PTR_ERR(kern_buf);

	if (strncmp(kern_buf, "false", 5) == 0)
		qedf->stop_io_on_error = false;
	else if (strncmp(kern_buf, "true", 4) == 0)
		qedf->stop_io_on_error = true;
	else if (strncmp(kern_buf, "now", 3) == 0)
		/* Trigger from user to stop all I/O on this host */
		set_bit(QEDF_DBG_STOP_IO, &qedf->flags);

	kfree(kern_buf);
	return count;
}

static int
qedf_io_trace_show(struct seq_file *s, void *unused)
{
	int i, idx = 0;
	struct qedf_ctx *qedf = s->private;
	struct qedf_dbg_ctx *qedf_dbg = &qedf->dbg_ctx;
	struct qedf_io_log *io_log;
	unsigned long flags;

	if (!qedf_io_tracing) {
		seq_puts(s, "I/O tracing not enabled.\n");
		goto out;
	}

	QEDF_INFO(qedf_dbg, QEDF_LOG_DEBUGFS, "entered\n");

	spin_lock_irqsave(&qedf->io_trace_lock, flags);
	idx = qedf->io_trace_idx;
	for (i = 0; i < QEDF_IO_TRACE_SIZE; i++) {
		io_log = &qedf->io_trace_buf[idx];
		seq_printf(s, "%d:", io_log->direction);
		seq_printf(s, "0x%x:", io_log->task_id);
		seq_printf(s, "0x%06x:", io_log->port_id);
		seq_printf(s, "%d:", io_log->lun);
		seq_printf(s, "0x%02x:", io_log->op);
		seq_printf(s, "0x%02x%02x%02x%02x:", io_log->lba[0],
		    io_log->lba[1], io_log->lba[2], io_log->lba[3]);
		seq_printf(s, "%d:", io_log->bufflen);
		seq_printf(s, "%d:", io_log->sg_count);
		seq_printf(s, "0x%08x:", io_log->result);
		seq_printf(s, "%lu:", io_log->jiffies);
		seq_printf(s, "%d:", io_log->refcount);
		seq_printf(s, "%d:", io_log->req_cpu);
		seq_printf(s, "%d:", io_log->int_cpu);
		seq_printf(s, "%d:", io_log->rsp_cpu);
		seq_printf(s, "%d\n", io_log->sge_type);

		idx++;
		if (idx == QEDF_IO_TRACE_SIZE)
			idx = 0;
	}
	spin_unlock_irqrestore(&qedf->io_trace_lock, flags);

out:
	return 0;
}

static int
qedf_dbg_io_trace_open(struct inode *inode, struct file *file)
{
	struct qedf_dbg_ctx *qedf_dbg = inode->i_private;
	struct qedf_ctx *qedf = container_of(qedf_dbg,
	    struct qedf_ctx, dbg_ctx);

	return single_open(file, qedf_io_trace_show, qedf);
}

static int
qedf_driver_stats_show(struct seq_file *s, void *unused)
{
	struct qedf_ctx *qedf = s->private;
	struct qedf_rport *fcport;
	struct fc_rport_priv *rdata;

	seq_printf(s, "cmg_mgr free io_reqs: %d\n",
	    atomic_read(&qedf->cmd_mgr->free_list_cnt));
	seq_printf(s, "slow SGEs: %d\n", qedf->slow_sge_ios);
	seq_printf(s, "single SGEs: %d\n", qedf->single_sge_ios);
	seq_printf(s, "fast SGEs: %d\n\n", qedf->fast_sge_ios);

	seq_puts(s, "Offloaded ports:\n\n");

	rcu_read_lock();
	list_for_each_entry_rcu(fcport, &qedf->fcports, peers) {
		rdata = fcport->rdata;
		if (rdata == NULL)
			continue;
		seq_printf(s, "%06x: free_sqes: %d, num_active_ios: %d\n",
		    rdata->ids.port_id, atomic_read(&fcport->free_sqes),
		    atomic_read(&fcport->num_active_ios));
	}
	rcu_read_unlock();

	return 0;
}

static int
qedf_dbg_driver_stats_open(struct inode *inode, struct file *file)
{
	struct qedf_dbg_ctx *qedf_dbg = inode->i_private;
	struct qedf_ctx *qedf = container_of(qedf_dbg,
	    struct qedf_ctx, dbg_ctx);

	return single_open(file, qedf_driver_stats_show, qedf);
}

static ssize_t
qedf_dbg_clear_stats_cmd_read(struct file *filp, char __user *buffer,
				   size_t count, loff_t *ppos)
{
	int cnt = 0;

	/* Essentially a read stub */
	cnt = min_t(int, count, cnt - *ppos);
	*ppos += cnt;
	return cnt;
}

static ssize_t
qedf_dbg_clear_stats_cmd_write(struct file *filp,
				    const char __user *buffer, size_t count,
				    loff_t *ppos)
{
	struct qedf_dbg_ctx *qedf_dbg =
				(struct qedf_dbg_ctx *)filp->private_data;
	struct qedf_ctx *qedf = container_of(qedf_dbg, struct qedf_ctx,
	    dbg_ctx);

	QEDF_INFO(qedf_dbg, QEDF_LOG_DEBUGFS, "Clearing stat counters.\n");

	if (!count || *ppos)
		return 0;

	/* Clear stat counters exposed by 'stats' node */
	qedf->slow_sge_ios = 0;
	qedf->single_sge_ios = 0;
	qedf->fast_sge_ios = 0;

	return count;
}

static int
qedf_offload_stats_show(struct seq_file *s, void *unused)
{
	struct qedf_ctx *qedf = s->private;
	struct qed_fcoe_stats *fw_fcoe_stats;

	fw_fcoe_stats = kmalloc(sizeof(struct qed_fcoe_stats), GFP_KERNEL);
	if (!fw_fcoe_stats) {
		QEDF_ERR(&(qedf->dbg_ctx), "Could not allocate memory for "
		    "fw_fcoe_stats.\n");
		goto out;
	}

	/* Query firmware for offload stats */
	qed_ops->get_stats(qedf->cdev, fw_fcoe_stats);

	seq_printf(s, "fcoe_rx_byte_cnt=%llu\n"
	    "fcoe_rx_data_pkt_cnt=%llu\n"
	    "fcoe_rx_xfer_pkt_cnt=%llu\n"
	    "fcoe_rx_other_pkt_cnt=%llu\n"
	    "fcoe_silent_drop_pkt_cmdq_full_cnt=%u\n"
	    "fcoe_silent_drop_pkt_crc_error_cnt=%u\n"
	    "fcoe_silent_drop_pkt_task_invalid_cnt=%u\n"
	    "fcoe_silent_drop_total_pkt_cnt=%u\n"
	    "fcoe_silent_drop_pkt_rq_full_cnt=%u\n"
	    "fcoe_tx_byte_cnt=%llu\n"
	    "fcoe_tx_data_pkt_cnt=%llu\n"
	    "fcoe_tx_xfer_pkt_cnt=%llu\n"
	    "fcoe_tx_other_pkt_cnt=%llu\n",
	    fw_fcoe_stats->fcoe_rx_byte_cnt,
	    fw_fcoe_stats->fcoe_rx_data_pkt_cnt,
	    fw_fcoe_stats->fcoe_rx_xfer_pkt_cnt,
	    fw_fcoe_stats->fcoe_rx_other_pkt_cnt,
	    fw_fcoe_stats->fcoe_silent_drop_pkt_cmdq_full_cnt,
	    fw_fcoe_stats->fcoe_silent_drop_pkt_crc_error_cnt,
	    fw_fcoe_stats->fcoe_silent_drop_pkt_task_invalid_cnt,
	    fw_fcoe_stats->fcoe_silent_drop_total_pkt_cnt,
	    fw_fcoe_stats->fcoe_silent_drop_pkt_rq_full_cnt,
	    fw_fcoe_stats->fcoe_tx_byte_cnt,
	    fw_fcoe_stats->fcoe_tx_data_pkt_cnt,
	    fw_fcoe_stats->fcoe_tx_xfer_pkt_cnt,
	    fw_fcoe_stats->fcoe_tx_other_pkt_cnt);

	kfree(fw_fcoe_stats);
out:
	return 0;
}

static int
qedf_dbg_offload_stats_open(struct inode *inode, struct file *file)
{
	struct qedf_dbg_ctx *qedf_dbg = inode->i_private;
	struct qedf_ctx *qedf = container_of(qedf_dbg,
	    struct qedf_ctx, dbg_ctx);

	return single_open(file, qedf_offload_stats_show, qedf);
}


const struct file_operations qedf_dbg_fops[] = {
	qedf_dbg_fileops(qedf, fp_int),
	qedf_dbg_fileops_seq(qedf, io_trace),
	qedf_dbg_fileops(qedf, debug),
	qedf_dbg_fileops(qedf, stop_io_on_error),
	qedf_dbg_fileops_seq(qedf, driver_stats),
	qedf_dbg_fileops(qedf, clear_stats),
	qedf_dbg_fileops_seq(qedf, offload_stats),
	/* This must be last */
	{ },
};

#else /* CONFIG_DEBUG_FS */
void qedf_dbg_host_init(struct qedf_dbg_ctx *);
void qedf_dbg_host_exit(struct qedf_dbg_ctx *);
void qedf_dbg_init(char *);
void qedf_dbg_exit(void);
#endif /* CONFIG_DEBUG_FS */
