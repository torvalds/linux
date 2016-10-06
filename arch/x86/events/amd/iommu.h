/*
 * Copyright (C) 2013 Advanced Micro Devices, Inc.
 *
 * Author: Steven Kinney <Steven.Kinney@amd.com>
 * Author: Suravee Suthikulpanit <Suraveee.Suthikulpanit@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _PERF_EVENT_AMD_IOMMU_H_
#define _PERF_EVENT_AMD_IOMMU_H_

/* iommu pc mmio region register indexes */
#define IOMMU_PC_COUNTER_REG			0x00
#define IOMMU_PC_COUNTER_SRC_REG		0x08
#define IOMMU_PC_PASID_MATCH_REG		0x10
#define IOMMU_PC_DOMID_MATCH_REG		0x18
#define IOMMU_PC_DEVID_MATCH_REG		0x20
#define IOMMU_PC_COUNTER_REPORT_REG		0x28

/* maximun specified bank/counters */
#define PC_MAX_SPEC_BNKS			64
#define PC_MAX_SPEC_CNTRS			16

/* iommu pc reg masks*/
#define IOMMU_BASE_DEVID			0x0000

/* amd_iommu_init.c external support functions */
extern bool amd_iommu_pc_supported(void);

extern u8 amd_iommu_pc_get_max_banks(u16 devid);

extern u8 amd_iommu_pc_get_max_counters(u16 devid);

extern int amd_iommu_pc_get_set_reg_val(u16 devid, u8 bank, u8 cntr,
			u8 fxn, u64 *value, bool is_write);

#endif /*_PERF_EVENT_AMD_IOMMU_H_*/
