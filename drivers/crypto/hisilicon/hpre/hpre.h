/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 HiSilicon Limited. */
#ifndef __HISI_HPRE_H
#define __HISI_HPRE_H

#include <linux/list.h>
#include "../qm.h"

#define HPRE_SQE_SIZE			sizeof(struct hpre_sqe)
#define HPRE_PF_DEF_Q_NUM		64
#define HPRE_PF_DEF_Q_BASE		0

enum {
	HPRE_CLUSTER0,
	HPRE_CLUSTER1,
	HPRE_CLUSTER2,
	HPRE_CLUSTER3,
	HPRE_CLUSTERS_NUM,
};

enum hpre_ctrl_dbgfs_file {
	HPRE_CURRENT_QM,
	HPRE_CLEAR_ENABLE,
	HPRE_CLUSTER_CTRL,
	HPRE_DEBUG_FILE_NUM,
};

enum hpre_dfx_dbgfs_file {
	HPRE_SEND_CNT,
	HPRE_RECV_CNT,
	HPRE_SEND_FAIL_CNT,
	HPRE_SEND_BUSY_CNT,
	HPRE_OVER_THRHLD_CNT,
	HPRE_OVERTIME_THRHLD,
	HPRE_INVALID_REQ_CNT,
	HPRE_DFX_FILE_NUM
};

#define HPRE_DEBUGFS_FILE_NUM    (HPRE_DEBUG_FILE_NUM + HPRE_CLUSTERS_NUM - 1)

struct hpre_debugfs_file {
	int index;
	enum hpre_ctrl_dbgfs_file type;
	spinlock_t lock;
	struct hpre_debug *debug;
};

struct hpre_dfx {
	atomic64_t value;
	enum hpre_dfx_dbgfs_file type;
};

/*
 * One HPRE controller has one PF and multiple VFs, some global configurations
 * which PF has need this structure.
 * Just relevant for PF.
 */
struct hpre_debug {
	struct dentry *debug_root;
	struct hpre_dfx dfx[HPRE_DFX_FILE_NUM];
	struct hpre_debugfs_file files[HPRE_DEBUGFS_FILE_NUM];
};

struct hpre {
	struct hisi_qm qm;
	struct hpre_debug debug;
	unsigned long status;
};

enum hpre_alg_type {
	HPRE_ALG_NC_NCRT = 0x0,
	HPRE_ALG_NC_CRT = 0x1,
	HPRE_ALG_KG_STD = 0x2,
	HPRE_ALG_KG_CRT = 0x3,
	HPRE_ALG_DH_G2 = 0x4,
	HPRE_ALG_DH = 0x5,
};

struct hpre_sqe {
	__le32 dw0;
	__u8 task_len1;
	__u8 task_len2;
	__u8 mrttest_num;
	__u8 resv1;
	__le64 key;
	__le64 in;
	__le64 out;
	__le16 tag;
	__le16 resv2;
#define _HPRE_SQE_ALIGN_EXT	7
	__le32 rsvd1[_HPRE_SQE_ALIGN_EXT];
};

struct hisi_qp *hpre_create_qp(void);
int hpre_algs_register(void);
void hpre_algs_unregister(void);

#endif
