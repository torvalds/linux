/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2015-2016 Quantenna Communications. All rights reserved. */

#ifndef _QTN_FMAC_SHM_IPC_DEFS_H_
#define _QTN_FMAC_SHM_IPC_DEFS_H_

#include <linux/types.h>

#define QTN_IPC_REG_HDR_SZ	(32)
#define QTN_IPC_REG_SZ		(4096)
#define QTN_IPC_MAX_DATA_SZ	(QTN_IPC_REG_SZ - QTN_IPC_REG_HDR_SZ)

enum qtnf_shm_ipc_region_flags {
	QTNF_SHM_IPC_NEW_DATA		= BIT(0),
	QTNF_SHM_IPC_ACK		= BIT(1),
};

struct qtnf_shm_ipc_region_header {
	__le32 flags;
	__le16 data_len;
} __packed;

union qtnf_shm_ipc_region_headroom {
	struct qtnf_shm_ipc_region_header hdr;
	u8 headroom[QTN_IPC_REG_HDR_SZ];
} __packed;

struct qtnf_shm_ipc_region {
	union qtnf_shm_ipc_region_headroom headroom;
	u8 data[QTN_IPC_MAX_DATA_SZ];
} __packed;

#endif /* _QTN_FMAC_SHM_IPC_DEFS_H_ */
