/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * NCI based driver for Samsung S3FWRN5 NFC chip
 *
 * Copyright (C) 2015 Samsung Electrnoics
 * Robert Baldyga <r.baldyga@samsung.com>
 */

#ifndef __LOCAL_S3FWRN5_NCI_H_
#define __LOCAL_S3FWRN5_NCI_H_

#include "s3fwrn5.h"

#define NCI_PROP_SET_RFREG	0x22

struct nci_prop_set_rfreg_cmd {
	__u8 index;
	__u8 data[252];
};

struct nci_prop_set_rfreg_rsp {
	__u8 status;
};

#define NCI_PROP_START_RFREG	0x26

struct nci_prop_start_rfreg_rsp {
	__u8 status;
};

#define NCI_PROP_STOP_RFREG	0x27

struct nci_prop_stop_rfreg_cmd {
	__u16 checksum;
};

struct nci_prop_stop_rfreg_rsp {
	__u8 status;
};

#define NCI_PROP_FW_CFG		0x28

struct nci_prop_fw_cfg_cmd {
	__u8 clk_type;
	__u8 clk_speed;
	__u8 clk_req;
};

struct nci_prop_fw_cfg_rsp {
	__u8 status;
};

void s3fwrn5_nci_get_prop_ops(struct nci_driver_ops **ops, size_t *n);
int s3fwrn5_nci_rf_configure(struct s3fwrn5_info *info, const char *fw_name);

#endif /* __LOCAL_S3FWRN5_NCI_H_ */
