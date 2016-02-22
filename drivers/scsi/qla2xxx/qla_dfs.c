/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"

#include <linux/debugfs.h>
#include <linux/seq_file.h>

static struct dentry *qla2x00_dfs_root;
static atomic_t qla2x00_dfs_root_count;

static int
qla_dfs_fw_resource_cnt_show(struct seq_file *s, void *unused)
{
	struct scsi_qla_host *vha = s->private;
	struct qla_hw_data *ha = vha->hw;

	seq_puts(s, "FW Resource count\n\n");
	seq_printf(s, "Original TGT exchg count[%d]\n",
	    ha->orig_fw_tgt_xcb_count);
	seq_printf(s, "current TGT exchg count[%d]\n",
	    ha->cur_fw_tgt_xcb_count);
	seq_printf(s, "original Initiator Exchange count[%d]\n",
	    ha->orig_fw_xcb_count);
	seq_printf(s, "Current Initiator Exchange count[%d]\n",
	    ha->cur_fw_xcb_count);
	seq_printf(s, "Original IOCB count[%d]\n", ha->orig_fw_iocb_count);
	seq_printf(s, "Current IOCB count[%d]\n", ha->cur_fw_iocb_count);
	seq_printf(s, "MAX VP count[%d]\n", ha->max_npiv_vports);
	seq_printf(s, "MAX FCF count[%d]\n", ha->fw_max_fcf_count);

	return 0;
}

static int
qla_dfs_fw_resource_cnt_open(struct inode *inode, struct file *file)
{
	struct scsi_qla_host *vha = inode->i_private;
	return single_open(file, qla_dfs_fw_resource_cnt_show, vha);
}

static const struct file_operations dfs_fw_resource_cnt_ops = {
	.open           = qla_dfs_fw_resource_cnt_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static int
qla_dfs_tgt_counters_show(struct seq_file *s, void *unused)
{
	struct scsi_qla_host *vha = s->private;

	seq_puts(s, "Target Counters\n");
	seq_printf(s, "qla_core_sbt_cmd = %lld\n",
		vha->tgt_counters.qla_core_sbt_cmd);
	seq_printf(s, "qla_core_ret_sta_ctio = %lld\n",
		vha->tgt_counters.qla_core_ret_sta_ctio);
	seq_printf(s, "qla_core_ret_ctio = %lld\n",
		vha->tgt_counters.qla_core_ret_ctio);
	seq_printf(s, "core_qla_que_buf = %lld\n",
		vha->tgt_counters.core_qla_que_buf);
	seq_printf(s, "core_qla_snd_status = %lld\n",
		vha->tgt_counters.core_qla_snd_status);
	seq_printf(s, "core_qla_free_cmd = %lld\n",
		vha->tgt_counters.core_qla_free_cmd);
	seq_printf(s, "num alloc iocb failed = %lld\n",
		vha->tgt_counters.num_alloc_iocb_failed);
	seq_printf(s, "num term exchange sent = %lld\n",
		vha->tgt_counters.num_term_xchg_sent);
	seq_printf(s, "num Q full sent = %lld\n",
		vha->tgt_counters.num_q_full_sent);

	return 0;
}

static int
qla_dfs_tgt_counters_open(struct inode *inode, struct file *file)
{
	struct scsi_qla_host *vha = inode->i_private;
	return single_open(file, qla_dfs_tgt_counters_show, vha);
}

static const struct file_operations dfs_tgt_counters_ops = {
	.open           = qla_dfs_tgt_counters_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static int
qla2x00_dfs_fce_show(struct seq_file *s, void *unused)
{
	scsi_qla_host_t *vha = s->private;
	uint32_t cnt;
	uint32_t *fce;
	uint64_t fce_start;
	struct qla_hw_data *ha = vha->hw;

	mutex_lock(&ha->fce_mutex);

	seq_puts(s, "FCE Trace Buffer\n");
	seq_printf(s, "In Pointer = %llx\n\n", (unsigned long long)ha->fce_wr);
	seq_printf(s, "Base = %llx\n\n", (unsigned long long) ha->fce_dma);
	seq_puts(s, "FCE Enable Registers\n");
	seq_printf(s, "%08x %08x %08x %08x %08x %08x\n",
	    ha->fce_mb[0], ha->fce_mb[2], ha->fce_mb[3], ha->fce_mb[4],
	    ha->fce_mb[5], ha->fce_mb[6]);

	fce = (uint32_t *) ha->fce;
	fce_start = (unsigned long long) ha->fce_dma;
	for (cnt = 0; cnt < fce_calc_size(ha->fce_bufs) / 4; cnt++) {
		if (cnt % 8 == 0)
			seq_printf(s, "\n%llx: ",
			    (unsigned long long)((cnt * 4) + fce_start));
		else
			seq_putc(s, ' ');
		seq_printf(s, "%08x", *fce++);
	}

	seq_puts(s, "\nEnd\n");

	mutex_unlock(&ha->fce_mutex);

	return 0;
}

static int
qla2x00_dfs_fce_open(struct inode *inode, struct file *file)
{
	scsi_qla_host_t *vha = inode->i_private;
	struct qla_hw_data *ha = vha->hw;
	int rval;

	if (!ha->flags.fce_enabled)
		goto out;

	mutex_lock(&ha->fce_mutex);

	/* Pause tracing to flush FCE buffers. */
	rval = qla2x00_disable_fce_trace(vha, &ha->fce_wr, &ha->fce_rd);
	if (rval)
		ql_dbg(ql_dbg_user, vha, 0x705c,
		    "DebugFS: Unable to disable FCE (%d).\n", rval);

	ha->flags.fce_enabled = 0;

	mutex_unlock(&ha->fce_mutex);
out:
	return single_open(file, qla2x00_dfs_fce_show, vha);
}

static int
qla2x00_dfs_fce_release(struct inode *inode, struct file *file)
{
	scsi_qla_host_t *vha = inode->i_private;
	struct qla_hw_data *ha = vha->hw;
	int rval;

	if (ha->flags.fce_enabled)
		goto out;

	mutex_lock(&ha->fce_mutex);

	/* Re-enable FCE tracing. */
	ha->flags.fce_enabled = 1;
	memset(ha->fce, 0, fce_calc_size(ha->fce_bufs));
	rval = qla2x00_enable_fce_trace(vha, ha->fce_dma, ha->fce_bufs,
	    ha->fce_mb, &ha->fce_bufs);
	if (rval) {
		ql_dbg(ql_dbg_user, vha, 0x700d,
		    "DebugFS: Unable to reinitialize FCE (%d).\n", rval);
		ha->flags.fce_enabled = 0;
	}

	mutex_unlock(&ha->fce_mutex);
out:
	return single_release(inode, file);
}

static const struct file_operations dfs_fce_ops = {
	.open		= qla2x00_dfs_fce_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= qla2x00_dfs_fce_release,
};

int
qla2x00_dfs_setup(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA25XX(ha) && !IS_QLA81XX(ha) && !IS_QLA83XX(ha) &&
	    !IS_QLA27XX(ha))
		goto out;
	if (!ha->fce)
		goto out;

	if (qla2x00_dfs_root)
		goto create_dir;

	atomic_set(&qla2x00_dfs_root_count, 0);
	qla2x00_dfs_root = debugfs_create_dir(QLA2XXX_DRIVER_NAME, NULL);
	if (!qla2x00_dfs_root) {
		ql_log(ql_log_warn, vha, 0x00f7,
		    "Unable to create debugfs root directory.\n");
		goto out;
	}

create_dir:
	if (ha->dfs_dir)
		goto create_nodes;

	mutex_init(&ha->fce_mutex);
	ha->dfs_dir = debugfs_create_dir(vha->host_str, qla2x00_dfs_root);
	if (!ha->dfs_dir) {
		ql_log(ql_log_warn, vha, 0x00f8,
		    "Unable to create debugfs ha directory.\n");
		goto out;
	}

	atomic_inc(&qla2x00_dfs_root_count);

create_nodes:
	ha->dfs_fw_resource_cnt = debugfs_create_file("fw_resource_count",
	    S_IRUSR, ha->dfs_dir, vha, &dfs_fw_resource_cnt_ops);
	if (!ha->dfs_fw_resource_cnt) {
		ql_log(ql_log_warn, vha, 0x00fd,
		    "Unable to create debugFS fw_resource_count node.\n");
		goto out;
	}

	ha->dfs_tgt_counters = debugfs_create_file("tgt_counters", S_IRUSR,
	    ha->dfs_dir, vha, &dfs_tgt_counters_ops);
	if (!ha->dfs_tgt_counters) {
		ql_log(ql_log_warn, vha, 0xd301,
		    "Unable to create debugFS tgt_counters node.\n");
		goto out;
	}

	ha->dfs_fce = debugfs_create_file("fce", S_IRUSR, ha->dfs_dir, vha,
	    &dfs_fce_ops);
	if (!ha->dfs_fce) {
		ql_log(ql_log_warn, vha, 0x00f9,
		    "Unable to create debugfs fce node.\n");
		goto out;
	}
out:
	return 0;
}

int
qla2x00_dfs_remove(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;

	if (ha->dfs_fw_resource_cnt) {
		debugfs_remove(ha->dfs_fw_resource_cnt);
		ha->dfs_fw_resource_cnt = NULL;
	}

	if (ha->dfs_tgt_counters) {
		debugfs_remove(ha->dfs_tgt_counters);
		ha->dfs_tgt_counters = NULL;
	}

	if (ha->dfs_fce) {
		debugfs_remove(ha->dfs_fce);
		ha->dfs_fce = NULL;
	}

	if (ha->dfs_dir) {
		debugfs_remove(ha->dfs_dir);
		ha->dfs_dir = NULL;
		atomic_dec(&qla2x00_dfs_root_count);
	}

	if (atomic_read(&qla2x00_dfs_root_count) == 0 &&
	    qla2x00_dfs_root) {
		debugfs_remove(qla2x00_dfs_root);
		qla2x00_dfs_root = NULL;
	}

	return 0;
}
