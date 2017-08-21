/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#ifndef HINIC_HW_MGMT_H
#define HINIC_HW_MGMT_H

#include <linux/types.h>

#include "hinic_hw_if.h"
#include "hinic_hw_api_cmd.h"

enum hinic_mgmt_msg_type {
	HINIC_MGMT_MSG_SYNC = 1,
};

enum hinic_cfg_cmd {
	HINIC_CFG_NIC_CAP = 0,
};

struct hinic_pf_to_mgmt {
	struct hinic_hwif               *hwif;

	struct hinic_api_cmd_chain      *cmd_chain[HINIC_API_CMD_MAX];
};

int hinic_msg_to_mgmt(struct hinic_pf_to_mgmt *pf_to_mgmt,
		      enum hinic_mod_type mod, u8 cmd,
		      void *buf_in, u16 in_size, void *buf_out, u16 *out_size,
		      enum hinic_mgmt_msg_type sync);

int hinic_pf_to_mgmt_init(struct hinic_pf_to_mgmt *pf_to_mgmt,
			  struct hinic_hwif *hwif);

void hinic_pf_to_mgmt_free(struct hinic_pf_to_mgmt *pf_to_mgmt);

#endif
