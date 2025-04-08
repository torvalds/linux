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
#define PCI_DEVICE_ID_AMD_MP2_1_1	0x164A

#define AMD_C2P_MSG(regno) (0x10500 + ((regno) * 4))
#define AMD_P2C_MSG(regno) (0x10680 + ((regno) * 4))

#define AMD_C2P_MSG_V1(regno) (0x10900 + ((regno) * 4))
#define AMD_P2C_MSG_V1(regno) (0x10500 + ((regno) * 4))

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

struct sfh_dev_status {
	bool is_hpd_present;
	bool is_hpd_enabled;
	bool is_als_present;
	bool is_sra_present;
};

struct amd_mp2_dev {
	struct pci_dev *pdev;
	struct amdtp_cl_data *cl_data;
	void __iomem *mmio;
	void __iomem *vsbase;
	const struct amd_sfh1_1_ops *sfh1_1_ops;
	struct amd_mp2_ops *mp2_ops;
	struct amd_input_data in_data;
	/* mp2 active control status */
	u32 mp2_acs;
	struct sfh_dev_status dev_en;
	struct work_struct work;
	u8 init_done;
	u8 rver;
};

struct amd_mp2_ops {
	void (*start)(struct amd_mp2_dev *privdata, struct amd_mp2_sensor_info info);
	void (*stop)(struct amd_mp2_dev *privdata, u16 sensor_idx);
	void (*stop_all)(struct amd_mp2_dev *privdata);
	int (*response)(struct amd_mp2_dev *mp2, u8 sid, u32 sensor_sts);
	void (*clear_intr)(struct amd_mp2_dev *privdata);
	int (*init_intr)(struct amd_mp2_dev *privdata);
	int (*discovery_status)(struct amd_mp2_dev *privdata);
	void (*suspend)(struct amd_mp2_dev *mp2);
	void (*resume)(struct amd_mp2_dev *mp2);
	void (*remove)(void *privdata);
	int (*get_rep_desc)(int sensor_idx, u8 rep_desc[]);
	u32 (*get_desc_sz)(int sensor_idx, int descriptor_name);
	u8 (*get_feat_rep)(int sensor_idx, int report_id, u8 *feature_report);
	u8 (*get_in_rep)(u8 current_index, int sensor_idx, int report_id,
			 struct amd_input_data *in_data);
};

void amd_sfh_work(struct work_struct *work);
void amd_sfh_work_buffer(struct work_struct *work);
void amd_sfh_clear_intr_v2(struct amd_mp2_dev *privdata);
int amd_sfh_irq_init_v2(struct amd_mp2_dev *privdata);
void amd_sfh_clear_intr(struct amd_mp2_dev *privdata);
int amd_sfh_irq_init(struct amd_mp2_dev *privdata);

static inline u64 amd_get_c2p_val(struct amd_mp2_dev *mp2, u32 idx)
{
	return mp2->rver == 1 ? AMD_C2P_MSG_V1(idx) :  AMD_C2P_MSG(idx);
}

static inline u64 amd_get_p2c_val(struct amd_mp2_dev *mp2, u32 idx)
{
	return mp2->rver == 1 ? AMD_P2C_MSG_V1(idx) :  AMD_P2C_MSG(idx);
}
#endif
