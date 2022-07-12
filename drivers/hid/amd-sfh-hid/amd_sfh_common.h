/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AMD MP2 common macros and structures
 *
 * Copyright (c) 2022, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Basavaraj Natikar <Basavaraj.Natikar@amd.com>
 */
#ifndef AMD_SFH_COMMON_H
#define AMD_SFH_COMMON_H

#include <linux/pci.h>
#include "amd_sfh_hid.h"

#define PCI_DEVICE_ID_AMD_MP2		0x15E4

#define AMD_C2P_MSG(regno) (0x10500 + ((regno) * 4))
#define AMD_P2C_MSG(regno) (0x10680 + ((regno) * 4))

#define SENSOR_ENABLED			4
#define SENSOR_DISABLED			5

#define AMD_SFH_IDLE_LOOP		200

enum cmd_id {
	NO_OP,
	ENABLE_SENSOR,
	DISABLE_SENSOR,
	STOP_ALL_SENSORS = 8,
};

struct amd_mp2_sensor_info {
	u8 sensor_idx;
	u32 period;
	dma_addr_t dma_address;
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

struct amd_mp2_ops {
	void (*start)(struct amd_mp2_dev *privdata, struct amd_mp2_sensor_info info);
	void (*stop)(struct amd_mp2_dev *privdata, u16 sensor_idx);
	void (*stop_all)(struct amd_mp2_dev *privdata);
	int (*response)(struct amd_mp2_dev *mp2, u8 sid, u32 sensor_sts);
	void (*clear_intr)(struct amd_mp2_dev *privdata);
	int (*init_intr)(struct amd_mp2_dev *privdata);
	int (*discovery_status)(struct amd_mp2_dev *privdata);
};

#endif
