// SPDX-License-Identifier: GPL-2.0-only
/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 */
#include "qla_def.h"

#include <linux/debugfs.h>
#include <linux/seq_file.h>

static struct dentry *qla2x00_dfs_root;
static atomic_t qla2x00_dfs_root_count;

#define QLA_DFS_RPORT_DEVLOSS_TMO	1

static int
qla_dfs_rport_get(struct fc_port *fp, int attr_id, u64 *val)
{
	switch (attr_id) {
	case QLA_DFS_RPORT_DEVLOSS_TMO:
		/* Only supported for FC-NVMe devices that are registered. */
		if (!(fp->nvme_flag & NVME_FLAG_REGISTERED))
			return -EIO;
		*val = fp->nvme_remote_port->dev_loss_tmo;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int
qla_dfs_rport_set(struct fc_port *fp, int attr_id, u64 val)
{
	switch (attr_id) {
	case QLA_DFS_RPORT_DEVLOSS_TMO:
		/* Only supported for FC-NVMe devices that are registered. */
		if (!(fp->nvme_flag & NVME_FLAG_REGISTERED))
			return -EIO;
#if (IS_ENABLED(CONFIG_NVME_FC))
		return nvme_fc_set_remoteport_devloss(fp->nvme_remote_port,
						      val);
#else /* CONFIG_NVME_FC */
		return -EINVAL;
#endif /* CONFIG_NVME_FC */
	default:
		return -EINVAL;
	}
	return 0;
}

#define DEFINE_QLA_DFS_RPORT_RW_ATTR(_attr_id, _attr)		\
static int qla_dfs_rport_##_attr##_get(void *data, u64 *val)	\
{								\
	struct fc_port *fp = data;				\
	return qla_dfs_rport_get(fp, _attr_id, val);		\
}								\
static int qla_dfs_rport_##_attr##_set(void *data, u64 val)	\
{								\
	struct fc_port *fp = data;				\
	return qla_dfs_rport_set(fp, _attr_id, val);		\
}								\
DEFINE_DEBUGFS_ATTRIBUTE(qla_dfs_rport_##_attr##_fops,		\
		qla_dfs_rport_##_attr##_get,			\
		qla_dfs_rport_##_attr##_set, "%llu\n")

/*
 * Wrapper for getting fc_port fields.
 *
 * _attr    : Attribute name.
 * _get_val : Accessor macro to retrieve the value.
 */
#define DEFINE_QLA_DFS_RPORT_FIELD_GET(_attr, _get_val)			\
static int qla_dfs_rport_field_##_attr##_get(void *data, u64 *val)	\
{									\
	struct fc_port *fp = data;					\
	*val = _get_val;						\
	return 0;							\
}									\
DEFINE_DEBUGFS_ATTRIBUTE(qla_dfs_rport_field_##_attr##_fops,		\
		qla_dfs_rport_field_##_attr##_get,			\
		NULL, "%llu\n")

#define DEFINE_QLA_DFS_RPORT_ACCESS(_attr, _get_val) \
	DEFINE_QLA_DFS_RPORT_FIELD_GET(_attr, _get_val)

#define DEFINE_QLA_DFS_RPORT_FIELD(_attr) \
	DEFINE_QLA_DFS_RPORT_FIELD_GET(_attr, fp->_attr)

DEFINE_QLA_DFS_RPORT_RW_ATTR(QLA_DFS_RPORT_DEVLOSS_TMO, dev_loss_tmo);

DEFINE_QLA_DFS_RPORT_FIELD(disc_state);
DEFINE_QLA_DFS_RPORT_FIELD(scan_state);
DEFINE_QLA_DFS_RPORT_FIELD(fw_login_state);
DEFINE_QLA_DFS_RPORT_FIELD(login_pause);
DEFINE_QLA_DFS_RPORT_FIELD(flags);
DEFINE_QLA_DFS_RPORT_FIELD(nvme_flag);
DEFINE_QLA_DFS_RPORT_FIELD(last_rscn_gen);
DEFINE_QLA_DFS_RPORT_FIELD(rscn_gen);
DEFINE_QLA_DFS_RPORT_FIELD(login_gen);
DEFINE_QLA_DFS_RPORT_FIELD(loop_id);
DEFINE_QLA_DFS_RPORT_FIELD_GET(port_id, fp->d_id.b24);
DEFINE_QLA_DFS_RPORT_FIELD_GET(sess_kref, kref_read(&fp->sess_kref));

void
qla2x00_dfs_create_rport(scsi_qla_host_t *vha, struct fc_port *fp)
{
	char wwn[32];

#define QLA_CREATE_RPORT_FIELD_ATTR(_attr)			\
	debugfs_create_file(#_attr, 0400, fp->dfs_rport_dir,	\
		fp, &qla_dfs_rport_field_##_attr##_fops)

	if (!vha->dfs_rport_root || fp->dfs_rport_dir)
		return;

	sprintf(wwn, "pn-%016llx", wwn_to_u64(fp->port_name));
	fp->dfs_rport_dir = debugfs_create_dir(wwn, vha->dfs_rport_root);
	if (IS_ERR(fp->dfs_rport_dir))
		return;
	if (NVME_TARGET(vha->hw, fp))
		debugfs_create_file("dev_loss_tmo", 0600, fp->dfs_rport_dir,
				    fp, &qla_dfs_rport_dev_loss_tmo_fops);

	QLA_CREATE_RPORT_FIELD_ATTR(disc_state);
	QLA_CREATE_RPORT_FIELD_ATTR(scan_state);
	QLA_CREATE_RPORT_FIELD_ATTR(fw_login_state);
	QLA_CREATE_RPORT_FIELD_ATTR(login_pause);
	QLA_CREATE_RPORT_FIELD_ATTR(flags);
	QLA_CREATE_RPORT_FIELD_ATTR(nvme_flag);
	QLA_CREATE_RPORT_FIELD_ATTR(last_rscn_gen);
	QLA_CREATE_RPORT_FIELD_ATTR(rscn_gen);
	QLA_CREATE_RPORT_FIELD_ATTR(login_gen);
	QLA_CREATE_RPORT_FIELD_ATTR(loop_id);
	QLA_CREATE_RPORT_FIELD_ATTR(port_id);
	QLA_CREATE_RPORT_FIELD_ATTR(sess_kref);
}

void
qla2x00_dfs_remove_rport(scsi_qla_host_t *vha, struct fc_port *fp)
{
	if (!vha->dfs_rport_root || !fp->dfs_rport_dir)
		return;
	debugfs_remove_recursive(fp->dfs_rport_dir);
	fp->dfs_rport_dir = NULL;
}

static int
qla2x00_dfs_tgt_sess_show(struct seq_file *s, void *unused)
{
	scsi_qla_host_t *vha = s->private;
	struct qla_hw_data *ha = vha->hw;
	unsigned long flags;
	struct fc_port *sess = NULL;
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;

	seq_printf(s, "%s\n", vha->host_str);
	if (tgt) {
		seq_puts(s, "Port ID   Port Name                Handle\n");

		spin_lock_irqsave(&ha->tgt.sess_lock, flags);
		list_for_each_entry(sess, &vha->vp_fcports, list)
			seq_printf(s, "%02x:%02x:%02x  %8phC  %d\n",
			    sess->d_id.b.domain, sess->d_id.b.area,
			    sess->d_id.b.al_pa, sess->port_name,
			    sess->loop_id);
		spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(qla2x00_dfs_tgt_sess);

static int
qla2x00_dfs_tgt_port_database_show(struct seq_file *s, void *unused)
{
	scsi_qla_host_t *vha = s->private;
	struct qla_hw_data *ha = vha->hw;
	struct gid_list_info *gid_list;
	dma_addr_t gid_list_dma;
	fc_port_t fc_port;
	char *id_iter;
	int rc, i;
	uint16_t entries, loop_id;

	seq_printf(s, "%s\n", vha->host_str);
	gid_list = dma_alloc_coherent(&ha->pdev->dev,
				      qla2x00_gid_list_size(ha),
				      &gid_list_dma, GFP_KERNEL);
	if (!gid_list) {
		ql_dbg(ql_dbg_user, vha, 0x7018,
		       "DMA allocation failed for %u\n",
		       qla2x00_gid_list_size(ha));
		return 0;
	}

	rc = qla24xx_gidlist_wait(vha, gid_list, gid_list_dma,
				  &entries);
	if (rc != QLA_SUCCESS)
		goto out_free_id_list;

	id_iter = (char *)gid_list;

	seq_puts(s, "Port Name	Port ID		Loop ID\n");

	for (i = 0; i < entries; i++) {
		struct gid_list_info *gid =
			(struct gid_list_info *)id_iter;
		loop_id = le16_to_cpu(gid->loop_id);
		memset(&fc_port, 0, sizeof(fc_port_t));

		fc_port.loop_id = loop_id;

		rc = qla24xx_gpdb_wait(vha, &fc_port, 0);
		seq_printf(s, "%8phC  %02x%02x%02x  %d\n",
			   fc_port.port_name, fc_port.d_id.b.domain,
			   fc_port.d_id.b.area, fc_port.d_id.b.al_pa,
			   fc_port.loop_id);
		id_iter += ha->gid_list_info_size;
	}
out_free_id_list:
	dma_free_coherent(&ha->pdev->dev, qla2x00_gid_list_size(ha),
			  gid_list, gid_list_dma);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(qla2x00_dfs_tgt_port_database);

static int
qla_dfs_fw_resource_cnt_show(struct seq_file *s, void *unused)
{
	struct scsi_qla_host *vha = s->private;
	uint16_t mb[MAX_IOCB_MB_REG];
	int rc;
	struct qla_hw_data *ha = vha->hw;
	u16 iocbs_used, i, exch_used;

	rc = qla24xx_res_count_wait(vha, mb, SIZEOF_IOCB_MB_REG);
	if (rc != QLA_SUCCESS) {
		seq_printf(s, "Mailbox Command failed %d, mb %#x", rc, mb[0]);
	} else {
		seq_puts(s, "FW Resource count\n\n");
		seq_printf(s, "Original TGT exchg count[%d]\n", mb[1]);
		seq_printf(s, "Current TGT exchg count[%d]\n", mb[2]);
		seq_printf(s, "Current Initiator Exchange count[%d]\n", mb[3]);
		seq_printf(s, "Original Initiator Exchange count[%d]\n", mb[6]);
		seq_printf(s, "Current IOCB count[%d]\n", mb[7]);
		seq_printf(s, "Original IOCB count[%d]\n", mb[10]);
		seq_printf(s, "MAX VP count[%d]\n", mb[11]);
		seq_printf(s, "MAX FCF count[%d]\n", mb[12]);
		seq_printf(s, "Current free pageable XCB buffer cnt[%d]\n",
		    mb[20]);
		seq_printf(s, "Original Initiator fast XCB buffer cnt[%d]\n",
		    mb[21]);
		seq_printf(s, "Current free Initiator fast XCB buffer cnt[%d]\n",
		    mb[22]);
		seq_printf(s, "Original Target fast XCB buffer cnt[%d]\n",
		    mb[23]);
	}

	if (ql2xenforce_iocb_limit) {
		/* lock is not require. It's an estimate. */
		iocbs_used = ha->base_qpair->fwres.iocbs_used;
		exch_used = ha->base_qpair->fwres.exch_used;
		for (i = 0; i < ha->max_qpairs; i++) {
			if (ha->queue_pair_map[i]) {
				iocbs_used += ha->queue_pair_map[i]->fwres.iocbs_used;
				exch_used += ha->queue_pair_map[i]->fwres.exch_used;
			}
		}

		seq_printf(s, "Driver: estimate iocb used [%d] high water limit [%d]\n",
			   iocbs_used, ha->base_qpair->fwres.iocbs_limit);

		seq_printf(s, "estimate exchange used[%d] high water limit [%d] n",
			   exch_used, ha->base_qpair->fwres.exch_limit);

		if (ql2xenforce_iocb_limit == 2) {
			iocbs_used = atomic_read(&ha->fwres.iocb_used);
			exch_used  = atomic_read(&ha->fwres.exch_used);
			seq_printf(s, "        estimate iocb2 used [%d] high water limit [%d]\n",
					iocbs_used, ha->fwres.iocb_limit);

			seq_printf(s, "        estimate exchange2 used[%d] high water limit [%d] \n",
					exch_used, ha->fwres.exch_limit);
		}
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(qla_dfs_fw_resource_cnt);

static int
qla_dfs_tgt_counters_show(struct seq_file *s, void *unused)
{
	struct scsi_qla_host *vha = s->private;
	struct qla_qpair *qpair = vha->hw->base_qpair;
	uint64_t qla_core_sbt_cmd, core_qla_que_buf, qla_core_ret_ctio,
		core_qla_snd_status, qla_core_ret_sta_ctio, core_qla_free_cmd,
		num_q_full_sent, num_alloc_iocb_failed, num_term_xchg_sent;
	u16 i;
	fc_port_t *fcport = NULL;

	if (qla2x00_chip_is_down(vha))
		return 0;

	qla_core_sbt_cmd = qpair->tgt_counters.qla_core_sbt_cmd;
	core_qla_que_buf = qpair->tgt_counters.core_qla_que_buf;
	qla_core_ret_ctio = qpair->tgt_counters.qla_core_ret_ctio;
	core_qla_snd_status = qpair->tgt_counters.core_qla_snd_status;
	qla_core_ret_sta_ctio = qpair->tgt_counters.qla_core_ret_sta_ctio;
	core_qla_free_cmd = qpair->tgt_counters.core_qla_free_cmd;
	num_q_full_sent = qpair->tgt_counters.num_q_full_sent;
	num_alloc_iocb_failed = qpair->tgt_counters.num_alloc_iocb_failed;
	num_term_xchg_sent = qpair->tgt_counters.num_term_xchg_sent;

	for (i = 0; i < vha->hw->max_qpairs; i++) {
		qpair = vha->hw->queue_pair_map[i];
		if (!qpair)
			continue;
		qla_core_sbt_cmd += qpair->tgt_counters.qla_core_sbt_cmd;
		core_qla_que_buf += qpair->tgt_counters.core_qla_que_buf;
		qla_core_ret_ctio += qpair->tgt_counters.qla_core_ret_ctio;
		core_qla_snd_status += qpair->tgt_counters.core_qla_snd_status;
		qla_core_ret_sta_ctio +=
		    qpair->tgt_counters.qla_core_ret_sta_ctio;
		core_qla_free_cmd += qpair->tgt_counters.core_qla_free_cmd;
		num_q_full_sent += qpair->tgt_counters.num_q_full_sent;
		num_alloc_iocb_failed +=
		    qpair->tgt_counters.num_alloc_iocb_failed;
		num_term_xchg_sent += qpair->tgt_counters.num_term_xchg_sent;
	}

	seq_puts(s, "Target Counters\n");
	seq_printf(s, "qla_core_sbt_cmd = %lld\n",
		qla_core_sbt_cmd);
	seq_printf(s, "qla_core_ret_sta_ctio = %lld\n",
		qla_core_ret_sta_ctio);
	seq_printf(s, "qla_core_ret_ctio = %lld\n",
		qla_core_ret_ctio);
	seq_printf(s, "core_qla_que_buf = %lld\n",
		core_qla_que_buf);
	seq_printf(s, "core_qla_snd_status = %lld\n",
		core_qla_snd_status);
	seq_printf(s, "core_qla_free_cmd = %lld\n",
		core_qla_free_cmd);
	seq_printf(s, "num alloc iocb failed = %lld\n",
		num_alloc_iocb_failed);
	seq_printf(s, "num term exchange sent = %lld\n",
		num_term_xchg_sent);
	seq_printf(s, "num Q full sent = %lld\n",
		num_q_full_sent);

	/* DIF stats */
	seq_printf(s, "DIF Inp Bytes = %lld\n",
		vha->qla_stats.qla_dif_stats.dif_input_bytes);
	seq_printf(s, "DIF Outp Bytes = %lld\n",
		vha->qla_stats.qla_dif_stats.dif_output_bytes);
	seq_printf(s, "DIF Inp Req = %lld\n",
		vha->qla_stats.qla_dif_stats.dif_input_requests);
	seq_printf(s, "DIF Outp Req = %lld\n",
		vha->qla_stats.qla_dif_stats.dif_output_requests);
	seq_printf(s, "DIF Guard err = %d\n",
		vha->qla_stats.qla_dif_stats.dif_guard_err);
	seq_printf(s, "DIF Ref tag err = %d\n",
		vha->qla_stats.qla_dif_stats.dif_ref_tag_err);
	seq_printf(s, "DIF App tag err = %d\n",
		vha->qla_stats.qla_dif_stats.dif_app_tag_err);

	seq_puts(s, "\n");
	seq_puts(s, "Initiator Error Counters\n");
	seq_printf(s, "HW Error Count =		%14lld\n",
		   vha->hw_err_cnt);
	seq_printf(s, "Link Down Count =	%14lld\n",
		   vha->short_link_down_cnt);
	seq_printf(s, "Interface Err Count =	%14lld\n",
		   vha->interface_err_cnt);
	seq_printf(s, "Cmd Timeout Count =	%14lld\n",
		   vha->cmd_timeout_cnt);
	seq_printf(s, "Reset Count =		%14lld\n",
		   vha->reset_cmd_err_cnt);
	seq_puts(s, "\n");

	list_for_each_entry(fcport, &vha->vp_fcports, list) {
		if (!fcport->rport)
			continue;

		seq_printf(s, "Target Num = %7d Link Down Count = %14lld\n",
			   fcport->rport->number, fcport->tgt_short_link_down_cnt);
	}
	seq_puts(s, "\n");

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(qla_dfs_tgt_counters);

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

static int
qla_dfs_naqp_show(struct seq_file *s, void *unused)
{
	struct scsi_qla_host *vha = s->private;
	struct qla_hw_data *ha = vha->hw;

	seq_printf(s, "%d\n", ha->tgt.num_act_qpairs);
	return 0;
}

/*
 * Helper macros for setting up debugfs entries.
 * _name: The name of the debugfs entry
 * _ctx_struct: The context that was passed when creating the debugfs file
 *
 * QLA_DFS_SETUP_RD could be used when there is only a show function.
 * - show function take the name qla_dfs_<sysfs-name>_show
 *
 * QLA_DFS_SETUP_RW could be used when there are both show and write functions.
 * - show function take the name  qla_dfs_<sysfs-name>_show
 * - write function take the name qla_dfs_<sysfs-name>_write
 *
 * To have a new debugfs entry, do:
 * 1. Create a "struct dentry *" in the appropriate structure in the format
 * dfs_<sysfs-name>
 * 2. Setup debugfs entries using QLA_DFS_SETUP_RD / QLA_DFS_SETUP_RW
 * 3. Create debugfs file in qla2x00_dfs_setup() using QLA_DFS_CREATE_FILE
 * or QLA_DFS_ROOT_CREATE_FILE
 * 4. Remove debugfs file in qla2x00_dfs_remove() using QLA_DFS_REMOVE_FILE
 * or QLA_DFS_ROOT_REMOVE_FILE
 *
 * Example for creating "TEST" sysfs file:
 * 1. struct qla_hw_data { ... struct dentry *dfs_TEST; }
 * 2. QLA_DFS_SETUP_RD(TEST);
 * 3. In qla2x00_dfs_setup():
 * QLA_DFS_CREATE_FILE(ha, TEST, 0600, ha->dfs_dir, vha);
 * 4. In qla2x00_dfs_remove():
 * QLA_DFS_REMOVE_FILE(ha, TEST);
 */
#define QLA_DFS_SETUP_RD(_name)	DEFINE_SHOW_ATTRIBUTE(qla_dfs_##_name)

#define QLA_DFS_SETUP_RW(_name)	DEFINE_SHOW_STORE_ATTRIBUTE(qla_dfs_##_name)

#define QLA_DFS_ROOT_CREATE_FILE(_name, _perm, _ctx)			\
	do {								\
		if (!qla_dfs_##_name)					\
			qla_dfs_##_name = debugfs_create_file(#_name,	\
					_perm, qla2x00_dfs_root, _ctx,	\
					&qla_dfs_##_name##_fops);	\
	} while (0)

#define QLA_DFS_ROOT_REMOVE_FILE(_name)					\
	do {								\
		if (qla_dfs_##_name) {					\
			debugfs_remove(qla_dfs_##_name);		\
			qla_dfs_##_name = NULL;				\
		}							\
	} while (0)

#define QLA_DFS_CREATE_FILE(_struct, _name, _perm, _parent, _ctx)	\
	do {								\
		(_struct)->dfs_##_name = debugfs_create_file(#_name,	\
					_perm, _parent, _ctx,		\
					&qla_dfs_##_name##_fops)	\
	} while (0)

#define QLA_DFS_REMOVE_FILE(_struct, _name)				\
	do {								\
		if ((_struct)->dfs_##_name) {				\
			debugfs_remove((_struct)->dfs_##_name);		\
			(_struct)->dfs_##_name = NULL;			\
		}							\
	} while (0)

static ssize_t
qla_dfs_naqp_write(struct file *file, const char __user *buffer,
    size_t count, loff_t *pos)
{
	struct seq_file *s = file->private_data;
	struct scsi_qla_host *vha = s->private;
	struct qla_hw_data *ha = vha->hw;
	char *buf;
	int rc = 0;
	unsigned long num_act_qp;

	if (!(IS_QLA27XX(ha) || IS_QLA83XX(ha) || IS_QLA28XX(ha))) {
		pr_err("host%ld: this adapter does not support Multi Q.",
		    vha->host_no);
		return -EINVAL;
	}

	if (!vha->flags.qpairs_available) {
		pr_err("host%ld: Driver is not setup with Multi Q.",
		    vha->host_no);
		return -EINVAL;
	}
	buf = memdup_user_nul(buffer, count);
	if (IS_ERR(buf)) {
		pr_err("host%ld: fail to copy user buffer.",
		    vha->host_no);
		return PTR_ERR(buf);
	}

	num_act_qp = simple_strtoul(buf, NULL, 0);

	if (num_act_qp >= vha->hw->max_qpairs) {
		pr_err("User set invalid number of qpairs %lu. Max = %d",
		    num_act_qp, vha->hw->max_qpairs);
		rc = -EINVAL;
		goto out_free;
	}

	if (num_act_qp != ha->tgt.num_act_qpairs) {
		ha->tgt.num_act_qpairs = num_act_qp;
		qlt_clr_qp_table(vha);
	}
	rc = count;
out_free:
	kfree(buf);
	return rc;
}
QLA_DFS_SETUP_RW(naqp);

int
qla2x00_dfs_setup(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA25XX(ha) && !IS_QLA81XX(ha) && !IS_QLA83XX(ha) &&
	    !IS_QLA27XX(ha) && !IS_QLA28XX(ha))
		goto out;
	if (!ha->fce)
		goto out;

	if (qla2x00_dfs_root)
		goto create_dir;

	atomic_set(&qla2x00_dfs_root_count, 0);
	qla2x00_dfs_root = debugfs_create_dir(QLA2XXX_DRIVER_NAME, NULL);

create_dir:
	if (ha->dfs_dir)
		goto create_nodes;

	mutex_init(&ha->fce_mutex);
	ha->dfs_dir = debugfs_create_dir(vha->host_str, qla2x00_dfs_root);

	atomic_inc(&qla2x00_dfs_root_count);

create_nodes:
	ha->dfs_fw_resource_cnt = debugfs_create_file("fw_resource_count",
	    S_IRUSR, ha->dfs_dir, vha, &qla_dfs_fw_resource_cnt_fops);

	ha->dfs_tgt_counters = debugfs_create_file("tgt_counters", S_IRUSR,
	    ha->dfs_dir, vha, &qla_dfs_tgt_counters_fops);

	ha->tgt.dfs_tgt_port_database = debugfs_create_file("tgt_port_database",
	    S_IRUSR,  ha->dfs_dir, vha, &qla2x00_dfs_tgt_port_database_fops);

	ha->dfs_fce = debugfs_create_file("fce", S_IRUSR, ha->dfs_dir, vha,
	    &dfs_fce_ops);

	ha->tgt.dfs_tgt_sess = debugfs_create_file("tgt_sess",
		S_IRUSR, ha->dfs_dir, vha, &qla2x00_dfs_tgt_sess_fops);

	if (IS_QLA27XX(ha) || IS_QLA83XX(ha) || IS_QLA28XX(ha)) {
		ha->tgt.dfs_naqp = debugfs_create_file("naqp",
		    0400, ha->dfs_dir, vha, &qla_dfs_naqp_fops);
		if (IS_ERR(ha->tgt.dfs_naqp)) {
			ql_log(ql_log_warn, vha, 0xd011,
			       "Unable to create debugFS naqp node.\n");
			goto out;
		}
	}
	vha->dfs_rport_root = debugfs_create_dir("rports", ha->dfs_dir);
	if (IS_ERR(vha->dfs_rport_root)) {
		ql_log(ql_log_warn, vha, 0xd012,
		       "Unable to create debugFS rports node.\n");
		goto out;
	}
out:
	return 0;
}

int
qla2x00_dfs_remove(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;

	if (ha->tgt.dfs_naqp) {
		debugfs_remove(ha->tgt.dfs_naqp);
		ha->tgt.dfs_naqp = NULL;
	}

	if (ha->tgt.dfs_tgt_sess) {
		debugfs_remove(ha->tgt.dfs_tgt_sess);
		ha->tgt.dfs_tgt_sess = NULL;
	}

	if (ha->tgt.dfs_tgt_port_database) {
		debugfs_remove(ha->tgt.dfs_tgt_port_database);
		ha->tgt.dfs_tgt_port_database = NULL;
	}

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

	if (vha->dfs_rport_root) {
		debugfs_remove_recursive(vha->dfs_rport_root);
		vha->dfs_rport_root = NULL;
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
