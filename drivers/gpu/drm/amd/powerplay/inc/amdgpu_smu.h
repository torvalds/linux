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

enum smu_message_type
{
	SMU_MSG_TestMessage = 0,
	SMU_MSG_GetSmuVersion,
	SMU_MSG_GetDriverIfVersion,
	SMU_MSG_SetAllowedFeaturesMaskLow,
	SMU_MSG_SetAllowedFeaturesMaskHigh,
	SMU_MSG_EnableAllSmuFeatures,
	SMU_MSG_DisableAllSmuFeatures,
	SMU_MSG_EnableSmuFeaturesLow,
	SMU_MSG_EnableSmuFeaturesHigh,
	SMU_MSG_DisableSmuFeaturesLow,
	SMU_MSG_DisableSmuFeaturesHigh,
	SMU_MSG_GetEnabledSmuFeaturesLow,
	SMU_MSG_GetEnabledSmuFeaturesHigh,
	SMU_MSG_SetWorkloadMask,
	SMU_MSG_SetPptLimit,
	SMU_MSG_SetDriverDramAddrHigh,
	SMU_MSG_SetDriverDramAddrLow,
	SMU_MSG_SetToolsDramAddrHigh,
	SMU_MSG_SetToolsDramAddrLow,
	SMU_MSG_TransferTableSmu2Dram,
	SMU_MSG_TransferTableDram2Smu,
	SMU_MSG_UseDefaultPPTable,
	SMU_MSG_UseBackupPPTable,
	SMU_MSG_RunBtc,
	SMU_MSG_RequestI2CBus,
	SMU_MSG_ReleaseI2CBus,
	SMU_MSG_SetFloorSocVoltage,
	SMU_MSG_SoftReset,
	SMU_MSG_StartBacoMonitor,
	SMU_MSG_CancelBacoMonitor,
	SMU_MSG_EnterBaco,
	SMU_MSG_SetSoftMinByFreq,
	SMU_MSG_SetSoftMaxByFreq,
	SMU_MSG_SetHardMinByFreq,
	SMU_MSG_SetHardMaxByFreq,
	SMU_MSG_GetMinDpmFreq,
	SMU_MSG_GetMaxDpmFreq,
	SMU_MSG_GetDpmFreqByIndex,
	SMU_MSG_GetDpmClockFreq,
	SMU_MSG_GetSsVoltageByDpm,
	SMU_MSG_SetMemoryChannelConfig,
	SMU_MSG_SetGeminiMode,
	SMU_MSG_SetGeminiApertureHigh,
	SMU_MSG_SetGeminiApertureLow,
	SMU_MSG_SetMinLinkDpmByIndex,
	SMU_MSG_OverridePcieParameters,
	SMU_MSG_OverDriveSetPercentage,
	SMU_MSG_SetMinDeepSleepDcefclk,
	SMU_MSG_ReenableAcDcInterrupt,
	SMU_MSG_NotifyPowerSource,
	SMU_MSG_SetUclkFastSwitch,
	SMU_MSG_SetUclkDownHyst,
	SMU_MSG_GfxDeviceDriverReset,
	SMU_MSG_GetCurrentRpm,
	SMU_MSG_SetVideoFps,
	SMU_MSG_SetTjMax,
	SMU_MSG_SetFanTemperatureTarget,
	SMU_MSG_PrepareMp1ForUnload,
	SMU_MSG_DramLogSetDramAddrHigh,
	SMU_MSG_DramLogSetDramAddrLow,
	SMU_MSG_DramLogSetDramSize,
	SMU_MSG_SetFanMaxRpm,
	SMU_MSG_SetFanMinPwm,
	SMU_MSG_ConfigureGfxDidt,
	SMU_MSG_NumOfDisplays,
	SMU_MSG_RemoveMargins,
	SMU_MSG_ReadSerialNumTop32,
	SMU_MSG_ReadSerialNumBottom32,
	SMU_MSG_SetSystemVirtualDramAddrHigh,
	SMU_MSG_SetSystemVirtualDramAddrLow,
	SMU_MSG_WaflTest,
	SMU_MSG_SetFclkGfxClkRatio,
	SMU_MSG_AllowGfxOff,
	SMU_MSG_DisallowGfxOff,
	SMU_MSG_GetPptLimit,
	SMU_MSG_GetDcModeMaxDpmFreq,
	SMU_MSG_GetDebugData,
	SMU_MSG_SetXgmiMode,
	SMU_MSG_RunAfllBtc,
	SMU_MSG_ExitBaco,
	SMU_MSG_PrepareMp1ForReset,
	SMU_MSG_PrepareMp1ForShutdown,
	SMU_MSG_SetMGpuFanBoostLimitRpm,
	SMU_MSG_GetAVFSVoltageByDpm,
	SMU_MSG_MAX_COUNT,
};

enum smu_memory_pool_size
{
    SMU_MEMORY_POOL_SIZE_ZERO   = 0,
    SMU_MEMORY_POOL_SIZE_256_MB = 0x10000000,
    SMU_MEMORY_POOL_SIZE_512_MB = 0x20000000,
    SMU_MEMORY_POOL_SIZE_1_GB   = 0x40000000,
    SMU_MEMORY_POOL_SIZE_2_GB   = 0x80000000,
};

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
	void                            *driver_pptable;
	struct smu_table		*tables;
	uint32_t			table_count;
	struct smu_table		memory_pool;
};

struct smu_dpm_context {
	void *dpm_context;
	uint32_t dpm_context_size;
};

struct smu_power_context {
	void *power_context;
	uint32_t power_context_size;
};


#define SMU_FEATURE_MAX	(64)
struct smu_feature
{
	uint32_t feature_num;
	DECLARE_BITMAP(supported, SMU_FEATURE_MAX);
	DECLARE_BITMAP(allowed, SMU_FEATURE_MAX);
	DECLARE_BITMAP(enabled, SMU_FEATURE_MAX);
};

struct smu_context
{
	struct amdgpu_device            *adev;

	const struct smu_funcs		*funcs;
	const struct pptable_funcs	*ppt_funcs;
	struct mutex			mutex;
	uint64_t pool_size;

	struct smu_table_context	smu_table;
	struct smu_dpm_context		smu_dpm;
	struct smu_power_context	smu_power;
	struct smu_feature		smu_feature;

	uint32_t pstate_sclk;
	uint32_t pstate_mclk;

	uint32_t power_limit;
	uint32_t default_power_limit;
};

struct pptable_funcs {
	int (*alloc_dpm_context)(struct smu_context *smu);
	int (*store_powerplay_table)(struct smu_context *smu);
	int (*check_powerplay_table)(struct smu_context *smu);
	int (*append_powerplay_table)(struct smu_context *smu);
	int (*get_smu_msg_index)(struct smu_context *smu, uint32_t index);
	int (*run_afll_btc)(struct smu_context *smu);
	int (*get_unallowed_feature_mask)(struct smu_context *smu, uint32_t *feature_mask, uint32_t num);
	int (*set_default_dpm_table)(struct smu_context *smu);
	int (*populate_umd_state_clk)(struct smu_context *smu);
	int (*print_clk_levels)(struct smu_context *smu, enum pp_clock_type type, char *buf);
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
	int (*get_clk_info_from_vbios)(struct smu_context *smu);
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
	int (*read_smc_arg)(struct smu_context *smu, uint32_t *arg);
	int (*init_display)(struct smu_context *smu);
	int (*set_allowed_mask)(struct smu_context *smu);
	int (*get_enabled_mask)(struct smu_context *smu, uint32_t *feature_mask, uint32_t num);
	int (*enable_all_mask)(struct smu_context *smu);
	int (*disable_all_mask)(struct smu_context *smu);
	int (*notify_display_change)(struct smu_context *smu);
	int (*get_power_limit)(struct smu_context *smu);
	int (*get_current_clk_freq)(struct smu_context *smu, uint32_t clk_id, uint32_t *value);
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
#define smu_get_clk_info_from_vbios(smu) \
	((smu)->funcs->get_clk_info_from_vbios ? (smu)->funcs->get_clk_info_from_vbios((smu)) : 0)
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
#define smu_read_smc_arg(smu, arg) \
	((smu)->funcs->read_smc_arg? (smu)->funcs->read_smc_arg((smu), (arg)) : 0)
#define smu_alloc_dpm_context(smu) \
	((smu)->ppt_funcs->alloc_dpm_context ? (smu)->ppt_funcs->alloc_dpm_context((smu)) : 0)
#define smu_init_display(smu) \
	((smu)->funcs->init_display ? (smu)->funcs->init_display((smu)) : 0)
#define smu_feature_set_allowed_mask(smu) \
	((smu)->funcs->set_allowed_mask? (smu)->funcs->set_allowed_mask((smu)) : 0)
#define smu_feature_get_enabled_mask(smu, mask, num) \
	((smu)->funcs->get_enabled_mask? (smu)->funcs->get_enabled_mask((smu), (mask), (num)) : 0)
#define smu_feature_enable_all(smu) \
	((smu)->funcs->enable_all_mask? (smu)->funcs->enable_all_mask((smu)) : 0)
#define smu_feature_disable_all(smu) \
	((smu)->funcs->disable_all_mask? (smu)->funcs->disable_all_mask((smu)) : 0)
#define smu_notify_display_change(smu) \
	((smu)->funcs->notify_display_change? (smu)->funcs->notify_display_change((smu)) : 0)
#define smu_store_powerplay_table(smu) \
	((smu)->ppt_funcs->store_powerplay_table ? (smu)->ppt_funcs->store_powerplay_table((smu)) : 0)
#define smu_check_powerplay_table(smu) \
	((smu)->ppt_funcs->check_powerplay_table ? (smu)->ppt_funcs->check_powerplay_table((smu)) : 0)
#define smu_append_powerplay_table(smu) \
	((smu)->ppt_funcs->append_powerplay_table ? (smu)->ppt_funcs->append_powerplay_table((smu)) : 0)
#define smu_set_default_dpm_table(smu) \
	((smu)->ppt_funcs->set_default_dpm_table ? (smu)->ppt_funcs->set_default_dpm_table((smu)) : 0)
#define smu_populate_umd_state_clk(smu) \
	((smu)->ppt_funcs->populate_umd_state_clk ? (smu)->ppt_funcs->populate_umd_state_clk((smu)) : 0)
#define smu_get_power_limit(smu) \
	((smu)->funcs->get_power_limit? (smu)->funcs->get_power_limit((smu)) : 0)
#define smu_get_current_clk_freq(smu, clk_id, value) \
	((smu)->funcs->get_current_clk_freq? (smu)->funcs->get_current_clk_freq((smu), (clk_id), (value)) : 0)
#define smu_print_clk_levels(smu, type, buf) \
	((smu)->ppt_funcs->print_clk_levels ? (smu)->ppt_funcs->print_clk_levels((smu), (type), (buf)) : 0)

#define smu_msg_get_index(smu, msg) \
	((smu)->ppt_funcs? ((smu)->ppt_funcs->get_smu_msg_index? (smu)->ppt_funcs->get_smu_msg_index((smu), (msg)) : -EINVAL) : -EINVAL)
#define smu_run_afll_btc(smu) \
	((smu)->ppt_funcs? ((smu)->ppt_funcs->run_afll_btc? (smu)->ppt_funcs->run_afll_btc((smu)) : 0) : 0)
#define smu_get_unallowed_feature_mask(smu, feature_mask, num) \
	((smu)->ppt_funcs? ((smu)->ppt_funcs->get_unallowed_feature_mask? (smu)->ppt_funcs->get_unallowed_feature_mask((smu), (feature_mask), (num)) : 0) : 0)

extern int smu_get_atom_data_table(struct smu_context *smu, uint32_t table,
				   uint16_t *size, uint8_t *frev, uint8_t *crev,
				   uint8_t **addr);

extern const struct amd_ip_funcs smu_ip_funcs;

extern const struct amdgpu_ip_block_version smu_v11_0_ip_block;
extern int smu_feature_init_dpm(struct smu_context *smu);

extern int smu_feature_is_enabled(struct smu_context *smu, int feature_id);
extern int smu_feature_set_enabled(struct smu_context *smu, int feature_id, bool enable);
extern int smu_feature_is_supported(struct smu_context *smu, int feature_id);
extern int smu_feature_set_supported(struct smu_context *smu, int feature_id, bool enable);

#endif
