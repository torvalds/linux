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

/* debugfs for device */
void hns_roce_register_debugfs(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_dev_debugfs *dbgfs = &hr_dev->dbgfs;

	dbgfs->root = debugfs_create_dir(dev_name(&hr_dev->ib_dev.dev),
					 hns_roce_dbgfs_root);
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
