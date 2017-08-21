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

#ifndef HINIC_HW_DEV_H
#define HINIC_HW_DEV_H

#include <linux/pci.h>
#include <linux/types.h>

#include "hinic_hw_if.h"
#include "hinic_hw_eqs.h"
#include "hinic_hw_mgmt.h"

#define HINIC_MAX_QPS   32

struct hinic_cap {
	u16     max_qps;
	u16     num_qps;
};

enum hinic_port_cmd {
	HINIC_PORT_CMD_CHANGE_MTU       = 2,

	HINIC_PORT_CMD_ADD_VLAN         = 3,
	HINIC_PORT_CMD_DEL_VLAN         = 4,

	HINIC_PORT_CMD_SET_MAC          = 9,
	HINIC_PORT_CMD_GET_MAC          = 10,
	HINIC_PORT_CMD_DEL_MAC          = 11,

	HINIC_PORT_CMD_SET_RX_MODE      = 12,

	HINIC_PORT_CMD_GET_LINK_STATE   = 24,

	HINIC_PORT_CMD_SET_PORT_STATE   = 41,

	HINIC_PORT_CMD_FWCTXT_INIT      = 69,

	HINIC_PORT_CMD_SET_FUNC_STATE   = 93,

	HINIC_PORT_CMD_GET_GLOBAL_QPN   = 102,

	HINIC_PORT_CMD_GET_CAP          = 170,
};

struct hinic_hwdev {
	struct hinic_hwif               *hwif;
	struct msix_entry               *msix_entries;

	struct hinic_aeqs               aeqs;

	struct hinic_cap                nic_cap;
};

struct hinic_pfhwdev {
	struct hinic_hwdev              hwdev;

	struct hinic_pf_to_mgmt         pf_to_mgmt;
};

int hinic_port_msg_cmd(struct hinic_hwdev *hwdev, enum hinic_port_cmd cmd,
		       void *buf_in, u16 in_size, void *buf_out,
		       u16 *out_size);

struct hinic_hwdev *hinic_init_hwdev(struct pci_dev *pdev);

void hinic_free_hwdev(struct hinic_hwdev *hwdev);

int hinic_hwdev_num_qps(struct hinic_hwdev *hwdev);

#endif
