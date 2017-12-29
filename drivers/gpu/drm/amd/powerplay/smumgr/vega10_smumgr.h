/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
#ifndef _VEGA10_SMUMANAGER_H_
#define _VEGA10_SMUMANAGER_H_

#include "vega10_hwmgr.h"

enum smu_table_id {
	PPTABLE = 0,
	WMTABLE,
	AVFSTABLE,
	TOOLSTABLE,
	AVFSFUSETABLE,
	MAX_SMU_TABLE,
};

struct smu_table_entry {
	uint32_t version;
	uint32_t size;
	uint32_t table_id;
	uint32_t table_addr_high;
	uint32_t table_addr_low;
	uint8_t *table;
	unsigned long handle;
};

struct smu_table_array {
	struct smu_table_entry entry[MAX_SMU_TABLE];
};

struct vega10_smumgr {
	struct smu_table_array            smu_tables;
};

int vega10_read_arg_from_smc(struct pp_hwmgr *hwmgr, uint32_t *arg);
int vega10_copy_table_from_smc(struct pp_hwmgr *hwmgr,
		uint8_t *table, int16_t table_id);
int vega10_copy_table_to_smc(struct pp_hwmgr *hwmgr,
		uint8_t *table, int16_t table_id);
int vega10_enable_smc_features(struct pp_hwmgr *hwmgr,
		bool enable, uint32_t feature_mask);
int vega10_get_smc_features(struct pp_hwmgr *hwmgr,
		uint32_t *features_enabled);
int vega10_save_vft_table(struct pp_hwmgr *hwmgr, uint8_t *avfs_table);
int vega10_restore_vft_table(struct pp_hwmgr *hwmgr, uint8_t *avfs_table);

int vega10_set_tools_address(struct pp_hwmgr *hwmgr);

#endif

