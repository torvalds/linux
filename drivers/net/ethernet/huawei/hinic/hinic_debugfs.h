/* SPDX-License-Identifier: GPL-2.0-only */
/* Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#ifndef HINIC_DEBUGFS_H
#define HINIC_DEBUGFS_H

#include "hinic_dev.h"

#define    TBL_ID_FUNC_CFG_SM_NODE                      11
#define    TBL_ID_FUNC_CFG_SM_INST                      1

#define HINIC_FUNCTION_CONFIGURE_TABLE_SIZE             64
#define HINIC_FUNCTION_CONFIGURE_TABLE			1

struct hinic_cmd_lt_rd {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	unsigned char node;
	unsigned char inst;
	unsigned char entry_size;
	unsigned char rsvd;
	unsigned int lt_index;
	unsigned int offset;
	unsigned int len;
	unsigned char data[100];
};

struct tag_sml_funcfg_tbl {
	union {
		struct {
			u32 rsvd0            :8;
			u32 nic_rx_mode      :5;
			u32 rsvd1            :18;
			u32 valid            :1;
		} bs;

		u32 value;
	} dw0;

	union {
		struct {
			u32 vlan_id             :12;
			u32 vlan_mode           :3;
			u32 fast_recycled_mode  :1;
			u32 mtu                 :16;
		} bs;

		u32 value;
	} dw1;

	u32 dw2;
	u32 dw3;
	u32 dw4;
	u32 dw5;
	u32 dw6;
	u32 dw7;
	u32 dw8;
	u32 dw9;
	u32 dw10;
	u32 dw11;
	u32 dw12;

	union {
		struct {
			u32 rsvd2               :15;
			u32 cfg_q_num           :9;
			u32 cfg_rq_depth        :6;
			u32 vhd_type            :2;
		} bs;

		u32 value;
	} dw13;

	u32 dw14;
	u32 dw15;
};

int hinic_sq_debug_add(struct hinic_dev *dev, u16 sq_id);

void hinic_sq_debug_rem(struct hinic_sq *sq);

int hinic_rq_debug_add(struct hinic_dev *dev, u16 rq_id);

void hinic_rq_debug_rem(struct hinic_rq *rq);

int hinic_func_table_debug_add(struct hinic_dev *dev);

void hinic_func_table_debug_rem(struct hinic_dev *dev);

void hinic_sq_dbgfs_init(struct hinic_dev *nic_dev);

void hinic_sq_dbgfs_uninit(struct hinic_dev *nic_dev);

void hinic_rq_dbgfs_init(struct hinic_dev *nic_dev);

void hinic_rq_dbgfs_uninit(struct hinic_dev *nic_dev);

void hinic_func_tbl_dbgfs_init(struct hinic_dev *nic_dev);

void hinic_func_tbl_dbgfs_uninit(struct hinic_dev *nic_dev);

void hinic_dbg_init(struct hinic_dev *nic_dev);

void hinic_dbg_uninit(struct hinic_dev *nic_dev);

void hinic_dbg_register_debugfs(const char *debugfs_dir_name);

void hinic_dbg_unregister_debugfs(void);

#endif
