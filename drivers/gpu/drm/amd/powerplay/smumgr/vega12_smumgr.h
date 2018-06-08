/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
#ifndef _VEGA12_SMUMANAGER_H_
#define _VEGA12_SMUMANAGER_H_

#include "hwmgr.h"
#include "vega12/smu9_driver_if.h"
#include "vega12_hwmgr.h"

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

struct vega12_smumgr {
	struct smu_table_array            smu_tables;
};

#define SMU_FEATURES_LOW_MASK        0x00000000FFFFFFFF
#define SMU_FEATURES_LOW_SHIFT       0
#define SMU_FEATURES_HIGH_MASK       0xFFFFFFFF00000000
#define SMU_FEATURES_HIGH_SHIFT      32

int vega12_read_arg_from_smc(struct pp_hwmgr *hwmgr, uint32_t *arg);
int vega12_copy_table_from_smc(struct pp_hwmgr *hwmgr,
		uint8_t *table, int16_t table_id);
int vega12_copy_table_to_smc(struct pp_hwmgr *hwmgr,
		uint8_t *table, int16_t table_id);
int vega12_enable_smc_features(struct pp_hwmgr *hwmgr,
		bool enable, uint64_t feature_mask);
int vega12_get_enabled_smc_features(struct pp_hwmgr *hwmgr,
		uint64_t *features_enabled);

#endif

