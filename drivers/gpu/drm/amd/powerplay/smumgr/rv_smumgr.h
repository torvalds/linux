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

#ifndef PP_RAVEN_SMUMANAGER_H
#define PP_RAVEN_SMUMANAGER_H

#include "rv_ppsmc.h"
#include "smu10_driver_if.h"

enum SMU_TABLE_ID {
	WMTABLE = 0,
	CLOCKTABLE,
	MAX_SMU_TABLE,
};

struct smu_table_entry {
	uint32_t version;
	uint32_t size;
	uint32_t table_id;
	uint32_t table_addr_high;
	uint32_t table_addr_low;
	uint8_t *table;
	uint32_t handle;
};

struct smu_table_array {
	struct smu_table_entry entry[MAX_SMU_TABLE];
};

struct rv_smumgr {
	struct smu_table_array            smu_tables;
};

int rv_read_arg_from_smc(struct pp_hwmgr *hwmgr, uint32_t *arg);
bool rv_is_smc_ram_running(struct pp_hwmgr *hwmgr);
int rv_copy_table_from_smc(struct pp_hwmgr *hwmgr,
		uint8_t *table, int16_t table_id);
int rv_copy_table_to_smc(struct pp_hwmgr *hwmgr,
		uint8_t *table, int16_t table_id);


#endif
