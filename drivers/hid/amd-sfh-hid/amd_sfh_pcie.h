/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AMD MP2 PCIe communication driver
 * Copyright 2020-2021 Advanced Micro Devices, Inc.
 * Authors: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 *	    Sandeep Singh <Sandeep.singh@amd.com>
 *	    Basavaraj Natikar <Basavaraj.Natikar@amd.com>
 */

#ifndef PCIE_MP2_AMD_H
#define PCIE_MP2_AMD_H

#include "amd_sfh_common.h"

/* MP2 C2P Message Registers */
#define AMD_C2P_MSG0	0x10500
#define AMD_C2P_MSG1	0x10504
#define AMD_C2P_MSG2	0x10508

/* MP2 P2C Message Registers */
#define AMD_P2C_MSG3	0x1068C /* Supported Sensors info */

#define V2_STATUS	0x2

#define HPD_IDX		16

#define SENSOR_DISCOVERY_STATUS_MASK		GENMASK(5, 3)
#define SENSOR_DISCOVERY_STATUS_SHIFT		3

/* SFH Command register */
union sfh_cmd_base {
	u32 ul;
	struct {
		u32 cmd_id : 8;
		u32 sensor_id : 8;
		u32 period : 16;
	} s;
	struct {
		u32 cmd_id : 4;
		u32 intr_disable : 1;
		u32 rsvd1 : 3;
		u32 length : 7;
		u32 mem_type : 1;
		u32 sensor_id : 8;
		u32 period : 8;
	} cmd_v2;
};

union cmd_response {
	u32 resp;
	struct {
		u32 status	: 2;
		u32 out_in_c2p	: 1;
		u32 rsvd1	: 1;
		u32 response	: 4;
		u32 sub_cmd	: 8;
		u32 sensor_id	: 6;
		u32 rsvd2	: 10;
	} response_v2;
};

union sfh_cmd_param {
	u32 ul;
	struct {
		u32 buf_layout : 2;
		u32 buf_length : 6;
		u32 rsvd : 24;
	} s;
};

struct sfh_cmd_reg {
	union sfh_cmd_base cmd_base;
	union sfh_cmd_param cmd_param;
	phys_addr_t phys_addr;
};

enum sensor_idx {
	accel_idx = 0,
	gyro_idx = 1,
	mag_idx = 2,
	als_idx = 19
};

enum mem_use_type {
	USE_DRAM,
	USE_C2P_REG,
};

struct hpd_status {
	union {
		struct {
			u32 object_distance       : 16;
			u32 probablity		  : 8;
			u32 human_presence_actual : 4;
			u32 human_presence_report : 4;
		} shpd;
		u32 val;
	};
};

int amd_mp2_get_sensor_num(struct amd_mp2_dev *privdata, u8 *sensor_id);
int amd_sfh_hid_client_init(struct amd_mp2_dev *privdata);
int amd_sfh_hid_client_deinit(struct amd_mp2_dev *privdata);
void amd_sfh_set_desc_ops(struct amd_mp2_ops *mp2_ops);

#endif
