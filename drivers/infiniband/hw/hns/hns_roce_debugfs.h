/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2023 Hisilicon Limited.
 */

#ifndef __HNS_ROCE_DEBUGFS_H
#define __HNS_ROCE_DEBUGFS_H

/* debugfs seqfile */
struct hns_debugfs_seqfile {
	int (*read)(struct seq_file *seq, void *data);
	void *data;
};

struct hns_sw_stat_debugfs {
	struct dentry *root;
	struct hns_debugfs_seqfile sw_stat;
};

/* Debugfs for device */
struct hns_roce_dev_debugfs {
	struct dentry *root;
	struct hns_sw_stat_debugfs sw_stat_root;
};

struct hns_roce_dev;

void hns_roce_init_debugfs(void);
void hns_roce_cleanup_debugfs(void);
void hns_roce_register_debugfs(struct hns_roce_dev *hr_dev);
void hns_roce_unregister_debugfs(struct hns_roce_dev *hr_dev);

#endif
