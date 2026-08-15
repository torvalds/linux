/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2023 Hisilicon Limited.
 */

#ifndef __HNS_ROCE_DEBUGFS_H
#define __HNS_ROCE_DEBUGFS_H

/* debugfs seqfile */
struct hns_debugfs_seqfile {
	int (*read)(struct seq_file *seq, void *data);
	ssize_t (*write)(char *buf, size_t count, void *data);
	void *data;
};

struct hns_sw_stat_debugfs {
	struct dentry *root;
	struct hns_debugfs_seqfile sw_stat;
};

struct hns_roce_cc_param_attr {
	const char *name;
	int algo_type;
	u32 offset;
	u32 size;
	u32 max;
	u32 min;
};

struct hns_cc_param_seqfile {
	struct hns_debugfs_seqfile seqfile;
	const struct hns_roce_cc_param_attr *param_attr;
	int index;
};

#define HNS_ROCE_CC_PARAM_MAX_NUM 11

struct hns_cc_param_debugfs {
	struct dentry *root;
	struct hns_cc_param_seqfile params[HNS_ROCE_CC_PARAM_MAX_NUM];
};

#define CONG_TYPE_MAX_NUM 4

/* Debugfs for device */
struct hns_roce_dev_debugfs {
	struct dentry *root;
	struct hns_sw_stat_debugfs sw_stat_root;
	struct hns_cc_param_debugfs cc_param_root[CONG_TYPE_MAX_NUM];
};

struct hns_roce_dev;

void hns_roce_init_debugfs(void);
void hns_roce_cleanup_debugfs(void);
void hns_roce_register_debugfs(struct hns_roce_dev *hr_dev);
void hns_roce_unregister_debugfs(struct hns_roce_dev *hr_dev);

#endif
