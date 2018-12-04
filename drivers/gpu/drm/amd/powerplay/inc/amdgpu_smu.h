/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
 */
#ifndef __AMDGPU_SMU_H__
#define __AMDGPU_SMU_H__

#include "amdgpu.h"

struct smu_context
{
	struct amdgpu_device            *adev;

	const struct smu_funcs		*funcs;
	struct mutex			mutex;
};

struct smu_funcs
{
	int (*init_microcode)(struct smu_context *smu);
	int (*init_smc_tables)(struct smu_context *smu);
	int (*init_power)(struct smu_context *smu);
	int (*load_microcode)(struct smu_context *smu);
	int (*check_fw_status)(struct smu_context *smu);
	int (*read_pptable_from_vbios)(struct smu_context *smu);
	int (*get_vbios_bootup_values)(struct smu_context *smu);
	int (*check_pptable)(struct smu_context *smu);
	int (*parse_pptable)(struct smu_context *smu);
	int (*populate_smc_pptable)(struct smu_context *smu);
	int (*check_fw_version)(struct smu_context *smu);
};

#define smu_init_microcode(smu) \
	((smu)->funcs->init_microcode ? (smu)->funcs->init_microcode((smu)) : 0)
#define smu_init_smc_tables(smu) \
	((smu)->funcs->init_smc_tables ? (smu)->funcs->init_smc_tables((smu)) : 0)
#define smu_init_power(smu) \
	((smu)->funcs->init_power ? (smu)->funcs->init_power((smu)) : 0)
#define smu_load_microcode(smu) \
	((smu)->funcs->load_microcode ? (smu)->funcs->load_microcode((smu)) : 0)
#define smu_check_fw_status(smu) \
	((smu)->funcs->check_fw_status ? (smu)->funcs->check_fw_status((smu)) : 0)
#define smu_read_pptable_from_vbios(smu) \
	((smu)->funcs->read_pptable_from_vbios ? (smu)->funcs->read_pptable_from_vbios((smu)) : 0)
#define smu_get_vbios_bootup_values(smu) \
	((smu)->funcs->get_vbios_bootup_values ? (smu)->funcs->get_vbios_bootup_values((smu)) : 0)
#define smu_check_pptable(smu) \
	((smu)->funcs->check_pptable ? (smu)->funcs->check_pptable((smu)) : 0)
#define smu_parse_pptable(smu) \
	((smu)->funcs->parse_pptable ? (smu)->funcs->parse_pptable((smu)) : 0)
#define smu_populate_smc_pptable(smu) \
	((smu)->funcs->populate_smc_pptable ? (smu)->funcs->populate_smc_pptable((smu)) : 0)
#define smu_check_fw_version(smu) \
	((smu)->funcs->check_fw_version ? (smu)->funcs->check_fw_version((smu)) : 0)


extern const struct amd_ip_funcs smu_ip_funcs;

extern const struct amdgpu_ip_block_version smu_v11_0_ip_block;

#endif
