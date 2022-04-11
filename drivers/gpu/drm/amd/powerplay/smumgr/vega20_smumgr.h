/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#ifndef _VEGA20_SMUMANAGER_H_
#define _VEGA20_SMUMANAGER_H_

#include "hwmgr.h"
#include "smu11_driver_if.h"

struct smu_table_entry {
	uint32_t version;
	uint32_t size;
	uint64_t mc_addr;
	void *table;
	struct amdgpu_bo *handle;
};

struct smu_table_array {
	struct smu_table_entry entry[TABLE_COUNT];
};

struct vega20_smumgr {
	struct smu_table_array            smu_tables;
};

#define SMU_FEATURES_LOW_MASK        0x00000000FFFFFFFF
#define SMU_FEATURES_LOW_SHIFT       0
#define SMU_FEATURES_HIGH_MASK       0xFFFFFFFF00000000
#define SMU_FEATURES_HIGH_SHIFT      32

int vega20_enable_smc_features(struct pp_hwmgr *hwmgr,
		bool enable, uint64_t feature_mask);
int vega20_get_enabled_smc_features(struct pp_hwmgr *hwmgr,
		uint64_t *features_enabled);
int vega20_set_activity_monitor_coeff(struct pp_hwmgr *hwmgr,
		uint8_t *table, uint16_t workload_type);
int vega20_get_activity_monitor_coeff(struct pp_hwmgr *hwmgr,
		uint8_t *table, uint16_t workload_type);
int vega20_set_pptable_driver_address(struct pp_hwmgr *hwmgr);

bool vega20_is_smc_ram_running(struct pp_hwmgr *hwmgr);

#endif

