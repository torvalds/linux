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
#include "kgd_pp_interface.h"
#include "dm_pp_interface.h"

struct smu_hw_power_state {
	unsigned int magic;
};

struct smu_power_state;

enum smu_state_ui_label {
	SMU_STATE_UI_LABEL_NONE,
	SMU_STATE_UI_LABEL_BATTERY,
	SMU_STATE_UI_TABEL_MIDDLE_LOW,
	SMU_STATE_UI_LABEL_BALLANCED,
	SMU_STATE_UI_LABEL_MIDDLE_HIGHT,
	SMU_STATE_UI_LABEL_PERFORMANCE,
	SMU_STATE_UI_LABEL_BACO,
};

enum smu_state_classification_flag {
	SMU_STATE_CLASSIFICATION_FLAG_BOOT                     = 0x0001,
	SMU_STATE_CLASSIFICATION_FLAG_THERMAL                  = 0x0002,
	SMU_STATE_CLASSIFICATIN_FLAG_LIMITED_POWER_SOURCE      = 0x0004,
	SMU_STATE_CLASSIFICATION_FLAG_RESET                    = 0x0008,
	SMU_STATE_CLASSIFICATION_FLAG_FORCED                   = 0x0010,
	SMU_STATE_CLASSIFICATION_FLAG_USER_3D_PERFORMANCE      = 0x0020,
	SMU_STATE_CLASSIFICATION_FLAG_USER_2D_PERFORMANCE      = 0x0040,
	SMU_STATE_CLASSIFICATION_FLAG_3D_PERFORMANCE           = 0x0080,
	SMU_STATE_CLASSIFICATION_FLAG_AC_OVERDIRVER_TEMPLATE   = 0x0100,
	SMU_STATE_CLASSIFICATION_FLAG_UVD                      = 0x0200,
	SMU_STATE_CLASSIFICATION_FLAG_3D_PERFORMANCE_LOW       = 0x0400,
	SMU_STATE_CLASSIFICATION_FLAG_ACPI                     = 0x0800,
	SMU_STATE_CLASSIFICATION_FLAG_HD2                      = 0x1000,
	SMU_STATE_CLASSIFICATION_FLAG_UVD_HD                   = 0x2000,
	SMU_STATE_CLASSIFICATION_FLAG_UVD_SD                   = 0x4000,
	SMU_STATE_CLASSIFICATION_FLAG_USER_DC_PERFORMANCE      = 0x8000,
	SMU_STATE_CLASSIFICATION_FLAG_DC_OVERDIRVER_TEMPLATE   = 0x10000,
	SMU_STATE_CLASSIFICATION_FLAG_BACO                     = 0x20000,
	SMU_STATE_CLASSIFICATIN_FLAG_LIMITED_POWER_SOURCE2      = 0x40000,
	SMU_STATE_CLASSIFICATION_FLAG_ULV                      = 0x80000,
	SMU_STATE_CLASSIFICATION_FLAG_UVD_MVC                  = 0x100000,
};

struct smu_state_classification_block {
	enum smu_state_ui_label         ui_label;
	enum smu_state_classification_flag  flags;
	int                          bios_index;
	bool                      temporary_state;
	bool                      to_be_deleted;
};

struct smu_state_pcie_block {
	unsigned int lanes;
};

enum smu_refreshrate_source {
	SMU_REFRESHRATE_SOURCE_EDID,
	SMU_REFRESHRATE_SOURCE_EXPLICIT
};

struct smu_state_display_block {
	bool              disable_frame_modulation;
	bool              limit_refreshrate;
	enum smu_refreshrate_source refreshrate_source;
	int                  explicit_refreshrate;
	int                  edid_refreshrate_index;
	bool              enable_vari_bright;
};

struct smu_state_memroy_block {
	bool              dll_off;
	uint8_t                 m3arb;
	uint8_t                 unused[3];
};

struct smu_state_software_algorithm_block {
	bool disable_load_balancing;
	bool enable_sleep_for_timestamps;
};

struct smu_temperature_range {
	int min;
	int max;
};

struct smu_state_validation_block {
	bool single_display_only;
	bool disallow_on_dc;
	uint8_t supported_power_levels;
};

struct smu_uvd_clocks {
	uint32_t vclk;
	uint32_t dclk;
};

/**
* Structure to hold a SMU Power State.
*/
struct smu_power_state {
	uint32_t                                      id;
	struct list_head                              ordered_list;
	struct list_head                              all_states_list;

	struct smu_state_classification_block         classification;
	struct smu_state_validation_block             validation;
	struct smu_state_pcie_block                   pcie;
	struct smu_state_display_block                display;
	struct smu_state_memroy_block                 memory;
	struct smu_temperature_range                  temperatures;
	struct smu_state_software_algorithm_block     software;
	struct smu_uvd_clocks                         uvd_clocks;
	struct smu_hw_power_state                     hardware;
};

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

enum smu_perf_level_designation {
	PERF_LEVEL_ACTIVITY,
	PERF_LEVEL_POWER_CONTAINMENT,
};

struct smu_performance_level {
	uint32_t core_clock;
	uint32_t memory_clock;
	uint32_t vddc;
	uint32_t vddci;
	uint32_t non_local_mem_freq;
	uint32_t non_local_mem_width;
};

struct smu_clock_info {
	uint32_t min_mem_clk;
	uint32_t max_mem_clk;
	uint32_t min_eng_clk;
	uint32_t max_eng_clk;
	uint32_t min_bus_bandwidth;
	uint32_t max_bus_bandwidth;
};

struct smu_bios_boot_up_values
{
	uint32_t			revision;
	uint32_t			gfxclk;
	uint32_t			uclk;
	uint32_t			socclk;
	uint32_t			dcefclk;
	uint32_t			eclk;
	uint32_t			vclk;
	uint32_t			dclk;
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
	void				*hardcode_pptable;

	void				*max_sustainable_clocks;
	struct smu_bios_boot_up_values	boot_values;
	void                            *driver_pptable;
	struct smu_table		*tables;
	uint32_t			table_count;
	struct smu_table		memory_pool;
	uint16_t                        software_shutdown_temp;
	uint8_t                         thermal_controller_type;
	uint16_t			TDPODLimit;

	uint8_t				*od_feature_capabilities;
	uint32_t			*od_settings_max;
	uint32_t			*od_settings_min;
	void				*overdrive_table;
	void				*od8_settings;
	bool				od_gfxclk_update;
	bool				od_memclk_update;
};

struct smu_dpm_context {
	uint32_t dpm_context_size;
	void *dpm_context;
	void *golden_dpm_context;
	bool enable_umd_pstate;
	enum amd_dpm_forced_level dpm_level;
	enum amd_dpm_forced_level saved_dpm_level;
	enum amd_dpm_forced_level requested_dpm_level;
	struct smu_power_state *dpm_request_power_state;
	struct smu_power_state *dpm_current_power_state;
	struct mclock_latency_table *mclk_latency_table;
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
	struct mutex mutex;
};

struct smu_clocks {
	uint32_t engine_clock;
	uint32_t memory_clock;
	uint32_t bus_bandwidth;
	uint32_t engine_clock_in_sr;
	uint32_t dcef_clock;
	uint32_t dcef_clock_in_sr;
};

#define MAX_REGULAR_DPM_NUM 16
struct mclk_latency_entries {
	uint32_t  frequency;
	uint32_t  latency;
};
struct mclock_latency_table {
	uint32_t  count;
	struct mclk_latency_entries  entries[MAX_REGULAR_DPM_NUM];
};

#define WORKLOAD_POLICY_MAX 7
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
	struct amd_pp_display_configuration  *display_config;

	uint32_t pstate_sclk;
	uint32_t pstate_mclk;

	bool od_enabled;
	uint32_t power_limit;
	uint32_t default_power_limit;

	bool support_power_containment;
	bool disable_watermark;

#define WATERMARKS_EXIST	(1 << 0)
#define WATERMARKS_LOADED	(1 << 1)
	uint32_t watermarks_bitmap;

	uint32_t workload_mask;
	uint32_t workload_prority[WORKLOAD_POLICY_MAX];
	uint32_t workload_setting[WORKLOAD_POLICY_MAX];
	uint32_t power_profile_mode;
	uint32_t default_power_profile_mode;

	uint32_t smc_if_version;
};

struct pptable_funcs {
	int (*alloc_dpm_context)(struct smu_context *smu);
	int (*store_powerplay_table)(struct smu_context *smu);
	int (*check_powerplay_table)(struct smu_context *smu);
	int (*append_powerplay_table)(struct smu_context *smu);
	int (*get_smu_msg_index)(struct smu_context *smu, uint32_t index);
	int (*run_afll_btc)(struct smu_context *smu);
	int (*get_unallowed_feature_mask)(struct smu_context *smu, uint32_t *feature_mask, uint32_t num);
	enum amd_pm_state_type (*get_current_power_state)(struct smu_context *smu);
	int (*set_default_dpm_table)(struct smu_context *smu);
	int (*set_power_state)(struct smu_context *smu);
	int (*populate_umd_state_clk)(struct smu_context *smu);
	int (*print_clk_levels)(struct smu_context *smu, enum pp_clock_type type, char *buf);
	int (*force_clk_levels)(struct smu_context *smu, enum pp_clock_type type, uint32_t mask);
	int (*set_default_od8_settings)(struct smu_context *smu);
	int (*update_specified_od8_value)(struct smu_context *smu,
					  uint32_t index,
					  uint32_t value);
	int (*get_od_percentage)(struct smu_context *smu, enum pp_clock_type type);
	int (*set_od_percentage)(struct smu_context *smu,
				 enum pp_clock_type type,
				 uint32_t value);
	int (*od_edit_dpm_table)(struct smu_context *smu,
				 enum PP_OD_DPM_TABLE_COMMAND type,
				 long *input, uint32_t size);
	int (*get_clock_by_type_with_latency)(struct smu_context *smu,
					      enum amd_pp_clock_type type,
					      struct
					      pp_clock_levels_with_latency
					      *clocks);
	int (*get_clock_by_type_with_voltage)(struct smu_context *smu,
					      enum amd_pp_clock_type type,
					      struct
					      pp_clock_levels_with_voltage
					      *clocks);
	int (*get_power_profile_mode)(struct smu_context *smu, char *buf);
	int (*set_power_profile_mode)(struct smu_context *smu, long *input, uint32_t size);
	enum amd_dpm_forced_level (*get_performance_level)(struct smu_context *smu);
	int (*force_performance_level)(struct smu_context *smu, enum amd_dpm_forced_level level);
	int (*pre_display_config_changed)(struct smu_context *smu);
	int (*display_config_changed)(struct smu_context *smu);
	int (*apply_clocks_adjust_rules)(struct smu_context *smu);
	int (*notify_smc_dispaly_config)(struct smu_context *smu);
	int (*force_dpm_limit_value)(struct smu_context *smu, bool highest);
	int (*unforce_dpm_levels)(struct smu_context *smu);
	int (*upload_dpm_level)(struct smu_context *smu, bool max,
				uint32_t feature_mask);
	int (*get_profiling_clk_mask)(struct smu_context *smu,
				      enum amd_dpm_forced_level level,
				      uint32_t *sclk_mask,
				      uint32_t *mclk_mask,
				      uint32_t *soc_mask);
	int (*set_cpu_power_state)(struct smu_context *smu);
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
	bool (*is_dpm_running)(struct smu_context *smu);
	int (*update_feature_enable_state)(struct smu_context *smu, uint32_t feature_id, bool enabled);
	int (*notify_display_change)(struct smu_context *smu);
	int (*get_power_limit)(struct smu_context *smu, uint32_t *limit, bool def);
	int (*set_power_limit)(struct smu_context *smu, uint32_t n);
	int (*get_current_clk_freq)(struct smu_context *smu, uint32_t clk_id, uint32_t *value);
	int (*init_max_sustainable_clocks)(struct smu_context *smu);
	int (*start_thermal_control)(struct smu_context *smu);
	int (*read_sensor)(struct smu_context *smu, enum amd_pp_sensors sensor,
			   void *data, uint32_t *size);
	int (*set_deep_sleep_dcefclk)(struct smu_context *smu, uint32_t clk);
	int (*set_active_display_count)(struct smu_context *smu, uint32_t count);
	int (*store_cc6_data)(struct smu_context *smu, uint32_t separation_time,
			      bool cc6_disable, bool pstate_disable,
			      bool pstate_switch_disable);
	int (*get_clock_by_type)(struct smu_context *smu,
				 enum amd_pp_clock_type type,
				 struct amd_pp_clocks *clocks);
	int (*get_max_high_clocks)(struct smu_context *smu,
				   struct amd_pp_simple_clock_info *clocks);
	int (*display_clock_voltage_request)(struct smu_context *smu, struct
					     pp_display_clock_request
					     *clock_req);
	int (*get_dal_power_level)(struct smu_context *smu,
				   struct amd_pp_simple_clock_info *clocks);
	int (*get_perf_level)(struct smu_context *smu,
			      enum smu_perf_level_designation designation,
			      struct smu_performance_level *level);
	int (*get_current_shallow_sleep_clocks)(struct smu_context *smu,
						struct smu_clock_info *clocks);
	int (*notify_smu_enable_pwe)(struct smu_context *smu);
	int (*set_watermarks_for_clock_ranges)(struct smu_context *smu,
					       struct dm_pp_wm_sets_with_clock_ranges_soc15 *clock_ranges);
	int (*set_od8_default_settings)(struct smu_context *smu,
					bool initialize);
	int (*get_activity_monitor_coeff)(struct smu_context *smu,
				      uint8_t *table,
				      uint16_t workload_type);
	int (*set_activity_monitor_coeff)(struct smu_context *smu,
				      uint8_t *table,
				      uint16_t workload_type);
	int (*conv_power_profile_to_pplib_workload)(int power_profile);
	int (*get_power_profile_mode)(struct smu_context *smu, char *buf);
	int (*set_power_profile_mode)(struct smu_context *smu, long *input, uint32_t size);
	int (*update_od8_settings)(struct smu_context *smu,
				   uint32_t index,
				   uint32_t value);
	int (*dpm_set_uvd_enable)(struct smu_context *smu, bool enable);
	int (*dpm_set_vce_enable)(struct smu_context *smu, bool enable);
	uint32_t (*get_sclk)(struct smu_context *smu, bool low);
	uint32_t (*get_mclk)(struct smu_context *smu, bool low);
	int (*get_current_rpm)(struct smu_context *smu, uint32_t *speed);
	uint32_t (*get_fan_control_mode)(struct smu_context *smu);
	int (*set_fan_control_mode)(struct smu_context *smu, uint32_t mode);
	int (*get_fan_speed_percent)(struct smu_context *smu, uint32_t *speed);
	int (*set_fan_speed_percent)(struct smu_context *smu, uint32_t speed);
	int (*set_fan_speed_rpm)(struct smu_context *smu, uint32_t speed);
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
#define smu_init_max_sustainable_clocks(smu) \
	((smu)->funcs->init_max_sustainable_clocks ? (smu)->funcs->init_max_sustainable_clocks((smu)) : 0)
#define smu_set_od8_default_settings(smu, initialize) \
	((smu)->funcs->set_od8_default_settings ? (smu)->funcs->set_od8_default_settings((smu), (initialize)) : 0)
#define smu_update_od8_settings(smu, index, value) \
	((smu)->funcs->update_od8_settings ? (smu)->funcs->update_od8_settings((smu), (index), (value)) : 0)
#define smu_get_current_rpm(smu, speed) \
	((smu)->funcs->get_current_rpm ? (smu)->funcs->get_current_rpm((smu), (speed)) : 0)
#define smu_set_fan_speed_rpm(smu, speed) \
	((smu)->funcs->set_fan_speed_rpm ? (smu)->funcs->set_fan_speed_rpm((smu), (speed)) : 0)
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
#define smu_is_dpm_running(smu) \
	((smu)->funcs->is_dpm_running ? (smu)->funcs->is_dpm_running((smu)) : 0)
#define smu_feature_update_enable_state(smu, feature_id, enabled) \
	((smu)->funcs->update_feature_enable_state? (smu)->funcs->update_feature_enable_state((smu), (feature_id), (enabled)) : 0)
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
#define smu_set_default_od8_settings(smu) \
	((smu)->ppt_funcs->set_default_od8_settings ? (smu)->ppt_funcs->set_default_od8_settings((smu)) : 0)
#define smu_update_specified_od8_value(smu, index, value) \
	((smu)->ppt_funcs->update_specified_od8_value ? (smu)->ppt_funcs->update_specified_od8_value((smu), (index), (value)) : 0)
#define smu_get_power_limit(smu, limit, def) \
	((smu)->funcs->get_power_limit ? (smu)->funcs->get_power_limit((smu), (limit), (def)) : 0)
#define smu_set_power_limit(smu, limit) \
	((smu)->funcs->set_power_limit ? (smu)->funcs->set_power_limit((smu), (limit)) : 0)
#define smu_get_current_clk_freq(smu, clk_id, value) \
	((smu)->funcs->get_current_clk_freq? (smu)->funcs->get_current_clk_freq((smu), (clk_id), (value)) : 0)
#define smu_print_clk_levels(smu, type, buf) \
	((smu)->ppt_funcs->print_clk_levels ? (smu)->ppt_funcs->print_clk_levels((smu), (type), (buf)) : 0)
#define smu_force_clk_levels(smu, type, level) \
	((smu)->ppt_funcs->force_clk_levels ? (smu)->ppt_funcs->force_clk_levels((smu), (type), (level)) : 0)
#define smu_get_od_percentage(smu, type) \
	((smu)->ppt_funcs->get_od_percentage ? (smu)->ppt_funcs->get_od_percentage((smu), (type)) : 0)
#define smu_set_od_percentage(smu, type, value) \
	((smu)->ppt_funcs->set_od_percentage ? (smu)->ppt_funcs->set_od_percentage((smu), (type), (value)) : 0)
#define smu_od_edit_dpm_table(smu, type, input, size) \
	((smu)->ppt_funcs->od_edit_dpm_table ? (smu)->ppt_funcs->od_edit_dpm_table((smu), (type), (input), (size)) : 0)
#define smu_start_thermal_control(smu) \
	((smu)->funcs->start_thermal_control? (smu)->funcs->start_thermal_control((smu)) : 0)
#define smu_read_sensor(smu, sensor, data, size) \
	((smu)->funcs->read_sensor? (smu)->funcs->read_sensor((smu), (sensor), (data), (size)) : 0)
#define smu_get_power_profile_mode(smu, buf) \
	((smu)->funcs->get_power_profile_mode ? (smu)->funcs->get_power_profile_mode((smu), buf) : 0)
#define smu_set_power_profile_mode(smu, param, param_size) \
	((smu)->funcs->set_power_profile_mode ? (smu)->funcs->set_power_profile_mode((smu), (param), (param_size)) : 0)
#define smu_get_performance_level(smu) \
	((smu)->ppt_funcs->get_performance_level ? (smu)->ppt_funcs->get_performance_level((smu)) : 0)
#define smu_force_performance_level(smu, level) \
	((smu)->ppt_funcs->force_performance_level ? (smu)->ppt_funcs->force_performance_level((smu), (level)) : 0)
#define smu_pre_display_config_changed(smu) \
	((smu)->ppt_funcs->pre_display_config_changed ? (smu)->ppt_funcs->pre_display_config_changed((smu)) : 0)
#define smu_display_config_changed(smu) \
	((smu)->ppt_funcs->display_config_changed ? (smu)->ppt_funcs->display_config_changed((smu)) : 0)
#define smu_apply_clocks_adjust_rules(smu) \
	((smu)->ppt_funcs->apply_clocks_adjust_rules ? (smu)->ppt_funcs->apply_clocks_adjust_rules((smu)) : 0)
#define smu_notify_smc_dispaly_config(smu) \
	((smu)->ppt_funcs->notify_smc_dispaly_config ? (smu)->ppt_funcs->notify_smc_dispaly_config((smu)) : 0)
#define smu_force_dpm_limit_value(smu, highest) \
	((smu)->ppt_funcs->force_dpm_limit_value ? (smu)->ppt_funcs->force_dpm_limit_value((smu), (highest)) : 0)
#define smu_unforce_dpm_levels(smu) \
	((smu)->ppt_funcs->unforce_dpm_levels ? (smu)->ppt_funcs->unforce_dpm_levels((smu)) : 0)
#define smu_upload_dpm_level(smu, max, feature_mask) \
	((smu)->ppt_funcs->upload_dpm_level ? (smu)->ppt_funcs->upload_dpm_level((smu), (max), (feature_mask)) : 0)
#define smu_get_profiling_clk_mask(smu, level, sclk_mask, mclk_mask, soc_mask) \
	((smu)->ppt_funcs->get_profiling_clk_mask ? (smu)->ppt_funcs->get_profiling_clk_mask((smu), (level), (sclk_mask), (mclk_mask), (soc_mask)) : 0)
#define smu_set_cpu_power_state(smu) \
	((smu)->ppt_funcs->set_cpu_power_state ? (smu)->ppt_funcs->set_cpu_power_state((smu)) : 0)
#define smu_get_fan_control_mode(smu) \
	((smu)->funcs->get_fan_control_mode ? (smu)->funcs->get_fan_control_mode((smu)) : 0)
#define smu_set_fan_control_mode(smu, value) \
	((smu)->funcs->set_fan_control_mode ? (smu)->funcs->set_fan_control_mode((smu), (value)) : 0)
#define smu_get_fan_speed_percent(smu, speed) \
	((smu)->funcs->get_fan_speed_percent ? (smu)->funcs->get_fan_speed_percent((smu), (speed)) : 0)
#define smu_set_fan_speed_percent(smu, speed) \
	((smu)->funcs->set_fan_speed_percent ? (smu)->funcs->set_fan_speed_percent((smu), (speed)) : 0)

#define smu_msg_get_index(smu, msg) \
	((smu)->ppt_funcs? ((smu)->ppt_funcs->get_smu_msg_index? (smu)->ppt_funcs->get_smu_msg_index((smu), (msg)) : -EINVAL) : -EINVAL)
#define smu_run_afll_btc(smu) \
	((smu)->ppt_funcs? ((smu)->ppt_funcs->run_afll_btc? (smu)->ppt_funcs->run_afll_btc((smu)) : 0) : 0)
#define smu_get_unallowed_feature_mask(smu, feature_mask, num) \
	((smu)->ppt_funcs? ((smu)->ppt_funcs->get_unallowed_feature_mask? (smu)->ppt_funcs->get_unallowed_feature_mask((smu), (feature_mask), (num)) : 0) : 0)
#define smu_set_deep_sleep_dcefclk(smu, clk) \
	((smu)->funcs->set_deep_sleep_dcefclk ? (smu)->funcs->set_deep_sleep_dcefclk((smu), (clk)) : 0)
#define smu_set_active_display_count(smu, count) \
	((smu)->funcs->set_active_display_count ? (smu)->funcs->set_active_display_count((smu), (count)) : 0)
#define smu_store_cc6_data(smu, st, cc6_dis, pst_dis, pst_sw_dis) \
	((smu)->funcs->store_cc6_data ? (smu)->funcs->store_cc6_data((smu), (st), (cc6_dis), (pst_dis), (pst_sw_dis)) : 0)
#define smu_get_clock_by_type(smu, type, clocks) \
	((smu)->funcs->get_clock_by_type ? (smu)->funcs->get_clock_by_type((smu), (type), (clocks)) : 0)
#define smu_get_max_high_clocks(smu, clocks) \
	((smu)->funcs->get_max_high_clocks ? (smu)->funcs->get_max_high_clocks((smu), (clocks)) : 0)
#define smu_get_clock_by_type_with_latency(smu, type, clocks) \
	((smu)->ppt_funcs->get_clock_by_type_with_latency ? (smu)->ppt_funcs->get_clock_by_type_with_latency((smu), (type), (clocks)) : 0)
#define smu_get_clock_by_type_with_voltage(smu, type, clocks) \
	((smu)->ppt_funcs->get_clock_by_type_with_voltage ? (smu)->ppt_funcs->get_clock_by_type_with_voltage((smu), (type), (clocks)) : 0)
#define smu_display_clock_voltage_request(smu, clock_req) \
	((smu)->funcs->display_clock_voltage_request ? (smu)->funcs->display_clock_voltage_request((smu), (clock_req)) : 0)
#define smu_get_dal_power_level(smu, clocks) \
	((smu)->funcs->get_dal_power_level ? (smu)->funcs->get_dal_power_level((smu), (clocks)) : 0)
#define smu_get_perf_level(smu, designation, level) \
	((smu)->funcs->get_perf_level ? (smu)->funcs->get_perf_level((smu), (designation), (level)) : 0)
#define smu_get_current_shallow_sleep_clocks(smu, clocks) \
	((smu)->funcs->get_current_shallow_sleep_clocks ? (smu)->funcs->get_current_shallow_sleep_clocks((smu), (clocks)) : 0)
#define smu_notify_smu_enable_pwe(smu) \
	((smu)->funcs->notify_smu_enable_pwe ? (smu)->funcs->notify_smu_enable_pwe((smu)) : 0)
#define smu_set_watermarks_for_clock_ranges(smu, clock_ranges) \
	((smu)->funcs->set_watermarks_for_clock_ranges ? (smu)->funcs->set_watermarks_for_clock_ranges((smu), (clock_ranges)) : 0)
#define smu_dpm_set_uvd_enable(smu, enable) \
	((smu)->funcs->dpm_set_uvd_enable ? (smu)->funcs->dpm_set_uvd_enable((smu), (enable)) : 0)
#define smu_dpm_set_vce_enable(smu, enable) \
	((smu)->funcs->dpm_set_vce_enable ? (smu)->funcs->dpm_set_vce_enable((smu), (enable)) : 0)
#define smu_get_sclk(smu, low) \
	((smu)->funcs->get_sclk ? (smu)->funcs->get_sclk((smu), (low)) : 0)
#define smu_get_mclk(smu, low) \
	((smu)->funcs->get_mclk ? (smu)->funcs->get_mclk((smu), (low)) : 0)


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

int smu_update_table(struct smu_context *smu, uint32_t table_id,
		     void *table_data, bool drv2smu);
bool is_support_sw_smu(struct amdgpu_device *adev);
int smu_reset(struct smu_context *smu);
int smu_common_read_sensor(struct smu_context *smu, enum amd_pp_sensors sensor,
			   void *data, uint32_t *size);
int smu_sys_get_pp_table(struct smu_context *smu, void **table);
int smu_sys_set_pp_table(struct smu_context *smu,  void *buf, size_t size);
int smu_get_power_num_states(struct smu_context *smu, struct pp_states_info *state_info);
enum amd_pm_state_type smu_get_current_power_state(struct smu_context *smu);

/* smu to display interface */
extern int smu_display_configuration_change(struct smu_context *smu, const
					    struct amd_pp_display_configuration
					    *display_config);
extern int smu_get_current_clocks(struct smu_context *smu,
				  struct amd_pp_clock_info *clocks);
extern int smu_dpm_set_power_gate(struct smu_context *smu,uint32_t block_type, bool gate);
extern int smu_handle_task(struct smu_context *smu,
			   enum amd_dpm_forced_level level,
			   enum amd_pp_task task_id);
#endif
