/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2021 Hisilicon Limited. */

#ifndef __HNS3_DEBUGFS_H
#define __HNS3_DEBUGFS_H

#include "hnae3.h"

#define HNS3_DBG_READ_LEN	65536
#define HNS3_DBG_READ_LEN_128KB	0x20000
#define HNS3_DBG_READ_LEN_1MB	0x100000
#define HNS3_DBG_READ_LEN_4MB	0x400000
#define HNS3_DBG_WRITE_LEN	1024

#define HNS3_DBG_DATA_STR_LEN	32
#define HNS3_DBG_INFO_LEN	256
#define HNS3_DBG_ITEM_NAME_LEN	32
#define HNS3_DBG_FILE_NAME_LEN	16

struct hns3_dbg_item {
	char name[HNS3_DBG_ITEM_NAME_LEN];
	u16 interval; /* blank numbers after the item */
};

struct hns3_dbg_data {
	struct hnae3_handle *handle;
	enum hnae3_dbg_cmd cmd;
	u16 qid;
};

enum hns3_dbg_dentry_type {
	HNS3_DBG_DENTRY_TM,
	HNS3_DBG_DENTRY_TX_BD,
	HNS3_DBG_DENTRY_RX_BD,
	HNS3_DBG_DENTRY_MAC,
	HNS3_DBG_DENTRY_REG,
	HNS3_DBG_DENTRY_QUEUE,
	HNS3_DBG_DENTRY_FD,
	HNS3_DBG_DENTRY_COMMON,
};

struct hns3_dbg_dentry_info {
	const char *name;
	struct dentry *dentry;
};

struct hns3_dbg_cmd_info {
	const char *name;
	enum hnae3_dbg_cmd cmd;
	enum hns3_dbg_dentry_type dentry;
	u32 buf_len;
	int (*init)(struct hnae3_handle *handle, unsigned int cmd);
};

struct hns3_dbg_func {
	enum hnae3_dbg_cmd cmd;
	int (*dbg_dump)(struct hnae3_handle *handle, char *buf, int len);
	int (*dbg_dump_bd)(struct hns3_dbg_data *data, char *buf, int len);
};

struct hns3_dbg_cap_info {
	const char *name;
	enum HNAE3_DEV_CAP_BITS cap_bit;
};

#endif
