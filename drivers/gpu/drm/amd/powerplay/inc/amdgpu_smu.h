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

#define SMU_TABLE_INIT(tables, table_id, s, a, d)	\
	do {						\
		tables[table_id].size = s;		\
		tables[table_id].align = a;		\
		tables[table_id].domain = d;		\
	} while (0)

struct smu_table {
	uint64_t size;
	uint32_t align;
	uint8_t domain;
	uint64_t mc_address;
	void *cpu_addr;
	struct amdgpu_bo *bo;
};

struct smu_bios_boot_up_values
{
	uint32_t			revision;
	uint32_t			gfxclk;
	uint32_t			uclk;
	uint32_t			socclk;
	uint32_t			dcefclk;
	uint16_t			vddc;
	uint16_t			vddci;
	uint16_t			mvddc;
	uint16_t			vdd_gfx;
	uint8_t				cooling_id;
	uint32_t			pp_table_id;
};

struct smu_table_context
{
	void				*power_play_table;
	uint32_t			power_play_table_size;

	struct smu_bios_boot_up_values	boot_values;
	struct smu_table		*tables;
	uint32_t			table_count;
};

struct smu_dpm_context {
	void *dpm_context;
	uint32_t dpm_context_size;
};

struct smu_power_context {
	void *power_context;
	uint32_t power_context_size;
};

struct smu_context
{
	struct amdgpu_device            *adev;

	const struct smu_funcs		*funcs;
	struct mutex			mutex;

	struct smu_table_context	smu_table;
	struct smu_dpm_context		smu_dpm;
	struct smu_power_context	smu_power;
};

struct smu_funcs
{
	int (*init_microcode)(struct smu_context *smu);
	int (*init_smc_tables)(struct smu_context *smu);
	int (*fini_smc_tables)(struct smu_context *smu);
	int (*init_power)(struct smu_context *smu);
	int (*fini_power)(struct smu_context *smu);
	int (*load_microcode)(struct smu_context *smu);
	int (*check_fw_status)(struct smu_context *smu);
	int (*read_pptable_from_vbios)(struct smu_context *smu);
	int (*get_vbios_bootup_values)(struct smu_context *smu);
	int (*check_pptable)(struct smu_context *smu);
	int (*parse_pptable)(struct smu_context *smu);
	int (*populate_smc_pptable)(struct smu_context *smu);
	int (*check_fw_version)(struct smu_context *smu);
	int (*write_pptable)(struct smu_context *smu);
	int (*set_min_dcef_deep_sleep)(struct smu_context *smu);
	int (*set_tool_table_location)(struct smu_context *smu);
	int (*notify_memory_pool_location)(struct smu_context *smu);
	int (*write_watermarks_table)(struct smu_context *smu);
	int (*set_last_dcef_min_deep_sleep_clk)(struct smu_context *smu);
	int (*system_features_control)(struct smu_context *smu, bool en);
	int (*send_smc_msg)(struct smu_context *smu, uint16_t msg);
	int (*send_smc_msg_with_param)(struct smu_context *smu, uint16_t msg, uint32_t param);

};

#define smu_init_microcode(smu) \
	((smu)->funcs->init_microcode ? (smu)->funcs->init_microcode((smu)) : 0)
#define smu_init_smc_tables(smu) \
	((smu)->funcs->init_smc_tables ? (smu)->funcs->init_smc_tables((smu)) : 0)
#define smu_fini_smc_tables(smu) \
	((smu)->funcs->fini_smc_tables ? (smu)->funcs->fini_smc_tables((smu)) : 0)
#define smu_init_power(smu) \
	((smu)->funcs->init_power ? (smu)->funcs->init_power((smu)) : 0)
#define smu_fini_power(smu) \
	((smu)->funcs->fini_power ? (smu)->funcs->fini_power((smu)) : 0)
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
#define smu_write_pptable(smu) \
	((smu)->funcs->write_pptable ? (smu)->funcs->write_pptable((smu)) : 0)
#define smu_set_min_dcef_deep_sleep(smu) \
	((smu)->funcs->set_min_dcef_deep_sleep ? (smu)->funcs->set_min_dcef_deep_sleep((smu)) : 0)
#define smu_set_tool_table_location(smu) \
	((smu)->funcs->set_tool_table_location ? (smu)->funcs->set_tool_table_location((smu)) : 0)
#define smu_notify_memory_pool_location(smu) \
	((smu)->funcs->notify_memory_pool_location ? (smu)->funcs->notify_memory_pool_location((smu)) : 0)
#define smu_write_watermarks_table(smu) \
	((smu)->funcs->write_watermarks_table ? (smu)->funcs->write_watermarks_table((smu)) : 0)
#define smu_set_last_dcef_min_deep_sleep_clk(smu) \
	((smu)->funcs->set_last_dcef_min_deep_sleep_clk ? (smu)->funcs->set_last_dcef_min_deep_sleep_clk((smu)) : 0)
#define smu_system_features_control(smu, en) \
	((smu)->funcs->system_features_control ? (smu)->funcs->system_features_control((smu), (en)) : 0)
#define smu_send_smc_msg(smu, msg) \
	((smu)->funcs->send_smc_msg? (smu)->funcs->send_smc_msg((smu), (msg)) : 0)
#define smu_send_smc_msg_with_param(smu, msg, param) \
	((smu)->funcs->send_smc_msg_with_param? (smu)->funcs->send_smc_msg_with_param((smu), (msg), (param)) : 0)

extern int smu_get_atom_data_table(struct smu_context *smu, uint32_t table,
				   uint16_t *size, uint8_t *frev, uint8_t *crev,
				   uint8_t **addr);

extern const struct amd_ip_funcs smu_ip_funcs;

extern const struct amdgpu_ip_block_version smu_v11_0_ip_block;

#endif
