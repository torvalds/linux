// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2023 Hisilicon Limited.
 */

#include <linux/debugfs.h>
#include <linux/device.h>

#include "hns_roce_device.h"

static struct dentry *hns_roce_dbgfs_root;

static int hns_debugfs_seqfile_open(struct inode *inode, struct file *f)
{
	struct hns_debugfs_seqfile *seqfile = inode->i_private;

	return single_open(f, seqfile->read, seqfile->data);
}

static const struct file_operations hns_debugfs_seqfile_fops = {
	.owner = THIS_MODULE,
	.open = hns_debugfs_seqfile_open,
	.release = single_release,
	.read = seq_read,
	.llseek = seq_lseek
};

static void init_debugfs_seqfile(struct hns_debugfs_seqfile *seq,
				 const char *name, struct dentry *parent,
				 int (*read_fn)(struct seq_file *, void *),
				 void *data)
{
	debugfs_create_file(name, 0400, parent, seq, &hns_debugfs_seqfile_fops);

	seq->read = read_fn;
	seq->data = data;
}

static const char * const sw_stat_info[] = {
	[HNS_ROCE_DFX_AEQE_CNT] = "aeqe",
	[HNS_ROCE_DFX_CEQE_CNT] = "ceqe",
	[HNS_ROCE_DFX_CMDS_CNT] = "cmds",
	[HNS_ROCE_DFX_CMDS_ERR_CNT] = "cmds_err",
	[HNS_ROCE_DFX_MBX_POSTED_CNT] = "posted_mbx",
	[HNS_ROCE_DFX_MBX_POLLED_CNT] = "polled_mbx",
	[HNS_ROCE_DFX_MBX_EVENT_CNT] = "mbx_event",
	[HNS_ROCE_DFX_QP_CREATE_ERR_CNT] = "qp_create_err",
	[HNS_ROCE_DFX_QP_MODIFY_ERR_CNT] = "qp_modify_err",
	[HNS_ROCE_DFX_CQ_CREATE_ERR_CNT] = "cq_create_err",
	[HNS_ROCE_DFX_CQ_MODIFY_ERR_CNT] = "cq_modify_err",
	[HNS_ROCE_DFX_SRQ_CREATE_ERR_CNT] = "srq_create_err",
	[HNS_ROCE_DFX_SRQ_MODIFY_ERR_CNT] = "srq_modify_err",
	[HNS_ROCE_DFX_XRCD_ALLOC_ERR_CNT] = "xrcd_alloc_err",
	[HNS_ROCE_DFX_MR_REG_ERR_CNT] = "mr_reg_err",
	[HNS_ROCE_DFX_MR_REREG_ERR_CNT] = "mr_rereg_err",
	[HNS_ROCE_DFX_AH_CREATE_ERR_CNT] = "ah_create_err",
	[HNS_ROCE_DFX_MMAP_ERR_CNT] = "mmap_err",
	[HNS_ROCE_DFX_UCTX_ALLOC_ERR_CNT] = "uctx_alloc_err",
};

static int sw_stat_debugfs_show(struct seq_file *file, void *offset)
{
	struct hns_roce_dev *hr_dev = file->private;
	int i;

	for (i = 0; i < HNS_ROCE_DFX_CNT_TOTAL; i++)
		seq_printf(file, "%-20s --- %lld\n", sw_stat_info[i],
			   atomic64_read(&hr_dev->dfx_cnt[i]));

	return 0;
}

static void create_sw_stat_debugfs(struct hns_roce_dev *hr_dev,
				   struct dentry *parent)
{
	struct hns_sw_stat_debugfs *dbgfs = &hr_dev->dbgfs.sw_stat_root;

	dbgfs->root = debugfs_create_dir("sw_stat", parent);

	init_debugfs_seqfile(&dbgfs->sw_stat, "sw_stat", dbgfs->root,
			     sw_stat_debugfs_show, hr_dev);
}

/* debugfs for device */
void hns_roce_register_debugfs(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_dev_debugfs *dbgfs = &hr_dev->dbgfs;

	dbgfs->root = debugfs_create_dir(dev_name(&hr_dev->ib_dev.dev),
					 hns_roce_dbgfs_root);

	create_sw_stat_debugfs(hr_dev, dbgfs->root);
}

void hns_roce_unregister_debugfs(struct hns_roce_dev *hr_dev)
{
	debugfs_remove_recursive(hr_dev->dbgfs.root);
}

/* debugfs for hns module */
void hns_roce_init_debugfs(void)
{
	hns_roce_dbgfs_root = debugfs_create_dir("hns_roce", NULL);
}

void hns_roce_cleanup_debugfs(void)
{
	debugfs_remove_recursive(hns_roce_dbgfs_root);
	hns_roce_dbgfs_root = NULL;
}
