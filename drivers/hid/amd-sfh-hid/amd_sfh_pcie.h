/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AMD MP2 PCIe communication driver
 * Copyright 2020 Advanced Micro Devices, Inc.
 * Authors: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 *	    Sandeep Singh <Sandeep.singh@amd.com>
 */

#ifndef PCIE_MP2_AMD_H
#define PCIE_MP2_AMD_H

#include <linux/pci.h>
#include "amd_sfh_hid.h"

#define PCI_DEVICE_ID_AMD_MP2	0x15E4

#define ENABLE_SENSOR		1
#define DISABLE_SENSOR		2
#define STOP_ALL_SENSORS	8

/* MP2 C2P Message Registers */
#define AMD_C2P_MSG0	0x10500
#define AMD_C2P_MSG1	0x10504
#define AMD_C2P_MSG2	0x10508

#define AMD_C2P_MSG(regno) (0x10500 + ((regno) * 4))
#define AMD_P2C_MSG(regno) (0x10680 + ((regno) * 4))

/* MP2 P2C Message Registers */
#define AMD_P2C_MSG3	0x1068C /* Supported Sensors info */

#define V2_STATUS	0x2

#define SENSOR_ENABLED     4
#define SENSOR_DISABLED    5

#define HPD_IDX		16

#define AMD_SFH_IDLE_LOOP	200

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

struct amd_mp2_dev {
	struct pci_dev *pdev;
	struct amdtp_cl_data *cl_data;
	void __iomem *mmio;
	const struct amd_mp2_ops *mp2_ops;
	struct amd_input_data in_data;
	/* mp2 active control status */
	u32 mp2_acs;
};

struct amd_mp2_sensor_info {
	u8 sensor_idx;
	u32 period;
	dma_addr_t dma_address;
};

enum mem_use_type {
	USE_DRAM,
	USE_C2P_REG,
};

struct hpd_status {
	union {
		struct {
			u32 human_presence_report : 4;
			u32 human_presence_actual : 4;
			u32 probablity		  : 8;
			u32 object_distance       : 16;
		} shpd;
		u32 val;
	};
};

void amd_start_sensor(struct amd_mp2_dev *privdata, struct amd_mp2_sensor_info info);
void amd_stop_sensor(struct amd_mp2_dev *privdata, u16 sensor_idx);
void amd_stop_all_sensors(struct amd_mp2_dev *privdata);
int amd_mp2_get_sensor_num(struct amd_mp2_dev *privdata, u8 *sensor_id);
int amd_sfh_hid_client_init(struct amd_mp2_dev *privdata);
int amd_sfh_hid_client_deinit(struct amd_mp2_dev *privdata);
u32 amd_sfh_wait_for_response(struct amd_mp2_dev *mp2, u8 sid, u32 sensor_sts);
void amd_mp2_suspend(struct amd_mp2_dev *mp2);
void amd_mp2_resume(struct amd_mp2_dev *mp2);

struct amd_mp2_ops {
	 void (*start)(struct amd_mp2_dev *privdata, struct amd_mp2_sensor_info info);
	 void (*stop)(struct amd_mp2_dev *privdata, u16 sensor_idx);
	 void (*stop_all)(struct amd_mp2_dev *privdata);
	 int (*response)(struct amd_mp2_dev *mp2, u8 sid, u32 sensor_sts);
};
#endif
