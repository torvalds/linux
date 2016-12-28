/*
 * QLogic iSCSI Offload Driver
 * Copyright (c) 2016 Cavium Inc.
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#include "qedi.h"
#include "qedi_dbg.h"

#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/module.h>

int do_not_recover;
static struct dentry *qedi_dbg_root;

void
qedi_dbg_host_init(struct qedi_dbg_ctx *qedi,
		   struct qedi_debugfs_ops *dops,
		   const struct file_operations *fops)
{
	char host_dirname[32];
	struct dentry *file_dentry = NULL;

	sprintf(host_dirname, "host%u", qedi->host_no);
	qedi->bdf_dentry = debugfs_create_dir(host_dirname, qedi_dbg_root);
	if (!qedi->bdf_dentry)
		return;

	while (dops) {
		if (!(dops->name))
			break;

		file_dentry = debugfs_create_file(dops->name, 0600,
						  qedi->bdf_dentry, qedi,
						  fops);
		if (!file_dentry) {
			QEDI_INFO(qedi, QEDI_LOG_DEBUGFS,
				  "Debugfs entry %s creation failed\n",
				  dops->name);
			debugfs_remove_recursive(qedi->bdf_dentry);
			return;
		}
		dops++;
		fops++;
	}
}

void
qedi_dbg_host_exit(struct qedi_dbg_ctx *qedi)
{
	debugfs_remove_recursive(qedi->bdf_dentry);
	qedi->bdf_dentry = NULL;
}

void
qedi_dbg_init(char *drv_name)
{
	qedi_dbg_root = debugfs_create_dir(drv_name, NULL);
	if (!qedi_dbg_root)
		QEDI_INFO(NULL, QEDI_LOG_DEBUGFS, "Init of debugfs failed\n");
}

void
qedi_dbg_exit(void)
{
	debugfs_remove_recursive(qedi_dbg_root);
	qedi_dbg_root = NULL;
}

static ssize_t
qedi_dbg_do_not_recover_enable(struct qedi_dbg_ctx *qedi_dbg)
{
	if (!do_not_recover)
		do_not_recover = 1;

	QEDI_INFO(qedi_dbg, QEDI_LOG_DEBUGFS, "do_not_recover=%d\n",
		  do_not_recover);
	return 0;
}

static ssize_t
qedi_dbg_do_not_recover_disable(struct qedi_dbg_ctx *qedi_dbg)
{
	if (do_not_recover)
		do_not_recover = 0;

	QEDI_INFO(qedi_dbg, QEDI_LOG_DEBUGFS, "do_not_recover=%d\n",
		  do_not_recover);
	return 0;
}

static struct qedi_list_of_funcs qedi_dbg_do_not_recover_ops[] = {
	{ "enable", qedi_dbg_do_not_recover_enable },
	{ "disable", qedi_dbg_do_not_recover_disable },
	{ NULL, NULL }
};

struct qedi_debugfs_ops qedi_debugfs_ops[] = {
	{ "gbl_ctx", NULL },
	{ "do_not_recover", qedi_dbg_do_not_recover_ops},
	{ "io_trace", NULL },
	{ NULL, NULL }
};

static ssize_t
qedi_dbg_do_not_recover_cmd_write(struct file *filp, const char __user *buffer,
				  size_t count, loff_t *ppos)
{
	size_t cnt = 0;
	struct qedi_dbg_ctx *qedi_dbg =
			(struct qedi_dbg_ctx *)filp->private_data;
	struct qedi_list_of_funcs *lof = qedi_dbg_do_not_recover_ops;

	if (*ppos)
		return 0;

	while (lof) {
		if (!(lof->oper_str))
			break;

		if (!strncmp(lof->oper_str, buffer, strlen(lof->oper_str))) {
			cnt = lof->oper_func(qedi_dbg);
			break;
		}

		lof++;
	}
	return (count - cnt);
}

static ssize_t
qedi_dbg_do_not_recover_cmd_read(struct file *filp, char __user *buffer,
				 size_t count, loff_t *ppos)
{
	size_t cnt = 0;

	if (*ppos)
		return 0;

	cnt = sprintf(buffer, "do_not_recover=%d\n", do_not_recover);
	cnt = min_t(int, count, cnt - *ppos);
	*ppos += cnt;
	return cnt;
}

static int
qedi_gbl_ctx_show(struct seq_file *s, void *unused)
{
	struct qedi_fastpath *fp = NULL;
	struct qed_sb_info *sb_info = NULL;
	struct status_block *sb = NULL;
	struct global_queue *que = NULL;
	int id;
	u16 prod_idx;
	struct qedi_ctx *qedi = s->private;
	unsigned long flags;

	seq_puts(s, " DUMP CQ CONTEXT:\n");

	for (id = 0; id < MIN_NUM_CPUS_MSIX(qedi); id++) {
		spin_lock_irqsave(&qedi->hba_lock, flags);
		seq_printf(s, "=========FAST CQ PATH [%d] ==========\n", id);
		fp = &qedi->fp_array[id];
		sb_info = fp->sb_info;
		sb = sb_info->sb_virt;
		prod_idx = (sb->pi_array[QEDI_PROTO_CQ_PROD_IDX] &
			    STATUS_BLOCK_PROD_INDEX_MASK);
		seq_printf(s, "SB PROD IDX: %d\n", prod_idx);
		que = qedi->global_queues[fp->sb_id];
		seq_printf(s, "DRV CONS IDX: %d\n", que->cq_cons_idx);
		seq_printf(s, "CQ complete host memory: %d\n", fp->sb_id);
		seq_puts(s, "=========== END ==================\n\n\n");
		spin_unlock_irqrestore(&qedi->hba_lock, flags);
	}
	return 0;
}

static int
qedi_dbg_gbl_ctx_open(struct inode *inode, struct file *file)
{
	struct qedi_dbg_ctx *qedi_dbg = inode->i_private;
	struct qedi_ctx *qedi = container_of(qedi_dbg, struct qedi_ctx,
					     dbg_ctx);

	return single_open(file, qedi_gbl_ctx_show, qedi);
}

static int
qedi_io_trace_show(struct seq_file *s, void *unused)
{
	int id, idx = 0;
	struct qedi_ctx *qedi = s->private;
	struct qedi_io_log *io_log;
	unsigned long flags;

	seq_puts(s, " DUMP IO LOGS:\n");
	spin_lock_irqsave(&qedi->io_trace_lock, flags);
	idx = qedi->io_trace_idx;
	for (id = 0; id < QEDI_IO_TRACE_SIZE; id++) {
		io_log = &qedi->io_trace_buf[idx];
		seq_printf(s, "iodir-%d:", io_log->direction);
		seq_printf(s, "tid-0x%x:", io_log->task_id);
		seq_printf(s, "cid-0x%x:", io_log->cid);
		seq_printf(s, "lun-%d:", io_log->lun);
		seq_printf(s, "op-0x%02x:", io_log->op);
		seq_printf(s, "0x%02x%02x%02x%02x:", io_log->lba[0],
			   io_log->lba[1], io_log->lba[2], io_log->lba[3]);
		seq_printf(s, "buflen-%d:", io_log->bufflen);
		seq_printf(s, "sgcnt-%d:", io_log->sg_count);
		seq_printf(s, "res-0x%08x:", io_log->result);
		seq_printf(s, "jif-%lu:", io_log->jiffies);
		seq_printf(s, "blk_req_cpu-%d:", io_log->blk_req_cpu);
		seq_printf(s, "req_cpu-%d:", io_log->req_cpu);
		seq_printf(s, "intr_cpu-%d:", io_log->intr_cpu);
		seq_printf(s, "blk_rsp_cpu-%d\n", io_log->blk_rsp_cpu);

		idx++;
		if (idx == QEDI_IO_TRACE_SIZE)
			idx = 0;
	}
	spin_unlock_irqrestore(&qedi->io_trace_lock, flags);
	return 0;
}

static int
qedi_dbg_io_trace_open(struct inode *inode, struct file *file)
{
	struct qedi_dbg_ctx *qedi_dbg = inode->i_private;
	struct qedi_ctx *qedi = container_of(qedi_dbg, struct qedi_ctx,
					     dbg_ctx);

	return single_open(file, qedi_io_trace_show, qedi);
}

const struct file_operations qedi_dbg_fops[] = {
	qedi_dbg_fileops_seq(qedi, gbl_ctx),
	qedi_dbg_fileops(qedi, do_not_recover),
	qedi_dbg_fileops_seq(qedi, io_trace),
	{ NULL, NULL },
};
