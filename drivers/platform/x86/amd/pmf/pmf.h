/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AMD Platform Management Framework Driver
 *
 * Copyright (c) 2022, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 */

#ifndef PMF_H
#define PMF_H

/* Message Definitions */
#define SET_SPL				0x03 /* SPL: Sustained Power Limit */
#define SET_SPPT			0x05 /* SPPT: Slow Package Power Tracking */
#define SET_FPPT			0x07 /* FPPT: Fast Package Power Tracking */
#define GET_SPL				0x0B
#define GET_SPPT			0x0D
#define GET_FPPT			0x0F
#define SET_DRAM_ADDR_HIGH	0x14
#define SET_DRAM_ADDR_LOW	0x15
#define SET_TRANSFER_TABLE	0x16
#define SET_STT_MIN_LIMIT	0x18 /* STT: Skin Temperature Tracking */
#define SET_STT_LIMIT_APU	0x19
#define SET_STT_LIMIT_HS2	0x1A
#define SET_SPPT_APU_ONLY	0x1D
#define GET_SPPT_APU_ONLY	0x1E
#define GET_STT_MIN_LIMIT	0x1F
#define GET_STT_LIMIT_APU	0x20
#define GET_STT_LIMIT_HS2	0x21

struct amd_pmf_dev {
	void __iomem *regbase;
	void __iomem *smu_virt_addr;
	void *buf;
	u32 base_addr;
	u32 cpu_id;
	struct device *dev;
	struct mutex lock; /* protects the PMF interface */
};

/* Core Layer */
int amd_pmf_send_cmd(struct amd_pmf_dev *dev, u8 message, bool get, u32 arg, u32 *data);

#endif /* PMF_H */
