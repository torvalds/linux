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

#ifndef __KGD_PP_INTERFACE_H__
#define __KGD_PP_INTERFACE_H__

extern const struct amdgpu_ip_block_version pp_smu_ip_block;
extern const struct amdgpu_ip_block_version smu_v11_0_ip_block;
extern const struct amdgpu_ip_block_version smu_v12_0_ip_block;
extern const struct amdgpu_ip_block_version smu_v13_0_ip_block;

enum smu_event_type {
	SMU_EVENT_RESET_COMPLETE = 0,
};

struct amd_vce_state {
	/* vce clocks */
	u32 evclk;
	u32 ecclk;
	/* gpu clocks */
	u32 sclk;
	u32 mclk;
	u8 clk_idx;
	u8 pstate;
};


enum amd_dpm_forced_level {
	AMD_DPM_FORCED_LEVEL_AUTO = 0x1,
	AMD_DPM_FORCED_LEVEL_MANUAL = 0x2,
	AMD_DPM_FORCED_LEVEL_LOW = 0x4,
	AMD_DPM_FORCED_LEVEL_HIGH = 0x8,
	AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD = 0x10,
	AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK = 0x20,
	AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK = 0x40,
	AMD_DPM_FORCED_LEVEL_PROFILE_PEAK = 0x80,
	AMD_DPM_FORCED_LEVEL_PROFILE_EXIT = 0x100,
	AMD_DPM_FORCED_LEVEL_PERF_DETERMINISM = 0x200,
};

enum amd_pm_state_type {
	/* not used for dpm */
	POWER_STATE_TYPE_DEFAULT,
	POWER_STATE_TYPE_POWERSAVE,
	/* user selectable states */
	POWER_STATE_TYPE_BATTERY,
	POWER_STATE_TYPE_BALANCED,
	POWER_STATE_TYPE_PERFORMANCE,
	/* internal states */
	POWER_STATE_TYPE_INTERNAL_UVD,
	POWER_STATE_TYPE_INTERNAL_UVD_SD,
	POWER_STATE_TYPE_INTERNAL_UVD_HD,
	POWER_STATE_TYPE_INTERNAL_UVD_HD2,
	POWER_STATE_TYPE_INTERNAL_UVD_MVC,
	POWER_STATE_TYPE_INTERNAL_BOOT,
	POWER_STATE_TYPE_INTERNAL_THERMAL,
	POWER_STATE_TYPE_INTERNAL_ACPI,
	POWER_STATE_TYPE_INTERNAL_ULV,
	POWER_STATE_TYPE_INTERNAL_3DPERF,
};

#define AMD_MAX_VCE_LEVELS 6

enum amd_vce_level {
	AMD_VCE_LEVEL_AC_ALL = 0,     /* AC, All cases */
	AMD_VCE_LEVEL_DC_EE = 1,      /* DC, entropy encoding */
	AMD_VCE_LEVEL_DC_LL_LOW = 2,  /* DC, low latency queue, res <= 720 */
	AMD_VCE_LEVEL_DC_LL_HIGH = 3, /* DC, low latency queue, 1080 >= res > 720 */
	AMD_VCE_LEVEL_DC_GP_LOW = 4,  /* DC, general purpose queue, res <= 720 */
	AMD_VCE_LEVEL_DC_GP_HIGH = 5, /* DC, general purpose queue, 1080 >= res > 720 */
};

enum amd_fan_ctrl_mode {
	AMD_FAN_CTRL_NONE = 0,
	AMD_FAN_CTRL_MANUAL = 1,
	AMD_FAN_CTRL_AUTO = 2,
};

enum pp_clock_type {
	PP_SCLK,
	PP_MCLK,
	PP_PCIE,
	PP_SOCCLK,
	PP_FCLK,
	PP_DCEFCLK,
	PP_VCLK,
	PP_DCLK,
	OD_SCLK,
	OD_MCLK,
	OD_VDDC_CURVE,
	OD_RANGE,
	OD_VDDGFX_OFFSET,
	OD_CCLK,
};

enum amd_pp_sensors {
	AMDGPU_PP_SENSOR_GFX_SCLK = 0,
	AMDGPU_PP_SENSOR_CPU_CLK,
	AMDGPU_PP_SENSOR_VDDNB,
	AMDGPU_PP_SENSOR_VDDGFX,
	AMDGPU_PP_SENSOR_UVD_VCLK,
	AMDGPU_PP_SENSOR_UVD_DCLK,
	AMDGPU_PP_SENSOR_VCE_ECCLK,
	AMDGPU_PP_SENSOR_GPU_LOAD,
	AMDGPU_PP_SENSOR_MEM_LOAD,
	AMDGPU_PP_SENSOR_GFX_MCLK,
	AMDGPU_PP_SENSOR_GPU_TEMP,
	AMDGPU_PP_SENSOR_EDGE_TEMP = AMDGPU_PP_SENSOR_GPU_TEMP,
	AMDGPU_PP_SENSOR_HOTSPOT_TEMP,
	AMDGPU_PP_SENSOR_MEM_TEMP,
	AMDGPU_PP_SENSOR_VCE_POWER,
	AMDGPU_PP_SENSOR_UVD_POWER,
	AMDGPU_PP_SENSOR_GPU_POWER,
	AMDGPU_PP_SENSOR_SS_APU_SHARE,
	AMDGPU_PP_SENSOR_SS_DGPU_SHARE,
	AMDGPU_PP_SENSOR_STABLE_PSTATE_SCLK,
	AMDGPU_PP_SENSOR_STABLE_PSTATE_MCLK,
	AMDGPU_PP_SENSOR_ENABLED_SMC_FEATURES_MASK,
	AMDGPU_PP_SENSOR_MIN_FAN_RPM,
	AMDGPU_PP_SENSOR_MAX_FAN_RPM,
	AMDGPU_PP_SENSOR_VCN_POWER_STATE,
};

enum amd_pp_task {
	AMD_PP_TASK_DISPLAY_CONFIG_CHANGE,
	AMD_PP_TASK_ENABLE_USER_STATE,
	AMD_PP_TASK_READJUST_POWER_STATE,
	AMD_PP_TASK_COMPLETE_INIT,
	AMD_PP_TASK_MAX
};

enum PP_SMC_POWER_PROFILE {
	PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT = 0x0,
	PP_SMC_POWER_PROFILE_FULLSCREEN3D = 0x1,
	PP_SMC_POWER_PROFILE_POWERSAVING  = 0x2,
	PP_SMC_POWER_PROFILE_VIDEO        = 0x3,
	PP_SMC_POWER_PROFILE_VR           = 0x4,
	PP_SMC_POWER_PROFILE_COMPUTE      = 0x5,
	PP_SMC_POWER_PROFILE_CUSTOM       = 0x6,
	PP_SMC_POWER_PROFILE_COUNT,
};

extern const char * const amdgpu_pp_profile_name[PP_SMC_POWER_PROFILE_COUNT];



enum {
	PP_GROUP_UNKNOWN = 0,
	PP_GROUP_GFX = 1,
	PP_GROUP_SYS,
	PP_GROUP_MAX
};

enum PP_OD_DPM_TABLE_COMMAND {
	PP_OD_EDIT_SCLK_VDDC_TABLE,
	PP_OD_EDIT_MCLK_VDDC_TABLE,
	PP_OD_EDIT_CCLK_VDDC_TABLE,
	PP_OD_EDIT_VDDC_CURVE,
	PP_OD_RESTORE_DEFAULT_TABLE,
	PP_OD_COMMIT_DPM_TABLE,
	PP_OD_EDIT_VDDGFX_OFFSET
};

struct pp_states_info {
	uint32_t nums;
	uint32_t states[16];
};

enum PP_HWMON_TEMP {
	PP_TEMP_EDGE = 0,
	PP_TEMP_JUNCTION,
	PP_TEMP_MEM,
	PP_TEMP_MAX
};

enum pp_mp1_state {
	PP_MP1_STATE_NONE,
	PP_MP1_STATE_SHUTDOWN,
	PP_MP1_STATE_UNLOAD,
	PP_MP1_STATE_RESET,
};

enum pp_df_cstate {
	DF_CSTATE_DISALLOW = 0,
	DF_CSTATE_ALLOW,
};

/**
 * DOC: amdgpu_pp_power
 *
 * APU power is managed to system-level requirements through the PPT
 * (package power tracking) feature. PPT is intended to limit power to the
 * requirements of the power source and could be dynamically updated to
 * maximize APU performance within the system power budget.
 *
 * Two types of power measurement can be requested, where supported, with
 * :c:type:`enum pp_power_type <pp_power_type>`.
 */

/**
 * enum pp_power_limit_level - Used to query the power limits
 *
 * @PP_PWR_LIMIT_MIN: Minimum Power Limit
 * @PP_PWR_LIMIT_CURRENT: Current Power Limit
 * @PP_PWR_LIMIT_DEFAULT: Default Power Limit
 * @PP_PWR_LIMIT_MAX: Maximum Power Limit
 */
enum pp_power_limit_level
{
	PP_PWR_LIMIT_MIN = -1,
	PP_PWR_LIMIT_CURRENT,
	PP_PWR_LIMIT_DEFAULT,
	PP_PWR_LIMIT_MAX,
};

/**
 * enum pp_power_type - Used to specify the type of the requested power
 *
 * @PP_PWR_TYPE_SUSTAINED: manages the configurable, thermally significant
 * moving average of APU power (default ~5000 ms).
 * @PP_PWR_TYPE_FAST: manages the ~10 ms moving average of APU power,
 * where supported.
 */
enum pp_power_type
{
	PP_PWR_TYPE_SUSTAINED,
	PP_PWR_TYPE_FAST,
};

#define PP_GROUP_MASK        0xF0000000
#define PP_GROUP_SHIFT       28

#define PP_BLOCK_MASK        0x0FFFFF00
#define PP_BLOCK_SHIFT       8

#define PP_BLOCK_GFX_CG         0x01
#define PP_BLOCK_GFX_MG         0x02
#define PP_BLOCK_GFX_3D         0x04
#define PP_BLOCK_GFX_RLC        0x08
#define PP_BLOCK_GFX_CP         0x10
#define PP_BLOCK_SYS_BIF        0x01
#define PP_BLOCK_SYS_MC         0x02
#define PP_BLOCK_SYS_ROM        0x04
#define PP_BLOCK_SYS_DRM        0x08
#define PP_BLOCK_SYS_HDP        0x10
#define PP_BLOCK_SYS_SDMA       0x20

#define PP_STATE_MASK           0x0000000F
#define PP_STATE_SHIFT          0
#define PP_STATE_SUPPORT_MASK   0x000000F0
#define PP_STATE_SUPPORT_SHIFT  0

#define PP_STATE_CG             0x01
#define PP_STATE_LS             0x02
#define PP_STATE_DS             0x04
#define PP_STATE_SD             0x08
#define PP_STATE_SUPPORT_CG     0x10
#define PP_STATE_SUPPORT_LS     0x20
#define PP_STATE_SUPPORT_DS     0x40
#define PP_STATE_SUPPORT_SD     0x80

#define PP_CG_MSG_ID(group, block, support, state) \
		((group) << PP_GROUP_SHIFT | (block) << PP_BLOCK_SHIFT | \
		(support) << PP_STATE_SUPPORT_SHIFT | (state) << PP_STATE_SHIFT)

#define XGMI_MODE_PSTATE_D3 0
#define XGMI_MODE_PSTATE_D0 1

#define NUM_HBM_INSTANCES 4

struct seq_file;
enum amd_pp_clock_type;
struct amd_pp_simple_clock_info;
struct amd_pp_display_configuration;
struct amd_pp_clock_info;
struct pp_display_clock_request;
struct pp_clock_levels_with_voltage;
struct pp_clock_levels_with_latency;
struct amd_pp_clocks;
struct pp_smu_wm_range_sets;
struct pp_smu_nv_clock_table;
struct dpm_clocks;

struct amd_pm_funcs {
/* export for dpm on ci and si */
	int (*pre_set_power_state)(void *handle);
	int (*set_power_state)(void *handle);
	void (*post_set_power_state)(void *handle);
	void (*display_configuration_changed)(void *handle);
	void (*print_power_state)(void *handle, void *ps);
	bool (*vblank_too_short)(void *handle);
	void (*enable_bapm)(void *handle, bool enable);
	int (*check_state_equal)(void *handle,
				void  *cps,
				void  *rps,
				bool  *equal);
/* export for sysfs */
	int (*set_fan_control_mode)(void *handle, u32 mode);
	int (*get_fan_control_mode)(void *handle, u32 *fan_mode);
	int (*set_fan_speed_pwm)(void *handle, u32 speed);
	int (*get_fan_speed_pwm)(void *handle, u32 *speed);
	int (*force_clock_level)(void *handle, enum pp_clock_type type, uint32_t mask);
	int (*print_clock_levels)(void *handle, enum pp_clock_type type, char *buf);
	int (*emit_clock_levels)(void *handle, enum pp_clock_type type, char *buf, int *offset);
	int (*force_performance_level)(void *handle, enum amd_dpm_forced_level level);
	int (*get_sclk_od)(void *handle);
	int (*set_sclk_od)(void *handle, uint32_t value);
	int (*get_mclk_od)(void *handle);
	int (*set_mclk_od)(void *handle, uint32_t value);
	int (*read_sensor)(void *handle, int idx, void *value, int *size);
	enum amd_dpm_forced_level (*get_performance_level)(void *handle);
	enum amd_pm_state_type (*get_current_power_state)(void *handle);
	int (*get_fan_speed_rpm)(void *handle, uint32_t *rpm);
	int (*set_fan_speed_rpm)(void *handle, uint32_t rpm);
	int (*get_pp_num_states)(void *handle, struct pp_states_info *data);
	int (*get_pp_table)(void *handle, char **table);
	int (*set_pp_table)(void *handle, const char *buf, size_t size);
	void (*debugfs_print_current_performance_level)(void *handle, struct seq_file *m);
	int (*switch_power_profile)(void *handle, enum PP_SMC_POWER_PROFILE type, bool en);
/* export to amdgpu */
	struct amd_vce_state *(*get_vce_clock_state)(void *handle, u32 idx);
	int (*dispatch_tasks)(void *handle, enum amd_pp_task task_id,
			enum amd_pm_state_type *user_state);
	int (*load_firmware)(void *handle);
	int (*wait_for_fw_loading_complete)(void *handle);
	int (*set_powergating_by_smu)(void *handle,
				uint32_t block_type, bool gate);
	int (*set_clockgating_by_smu)(void *handle, uint32_t msg_id);
	int (*set_power_limit)(void *handle, uint32_t n);
	int (*get_power_limit)(void *handle, uint32_t *limit,
			enum pp_power_limit_level pp_limit_level,
			enum pp_power_type power_type);
	int (*get_power_profile_mode)(void *handle, char *buf);
	int (*set_power_profile_mode)(void *handle, long *input, uint32_t size);
	int (*set_fine_grain_clk_vol)(void *handle, uint32_t type, long *input, uint32_t size);
	int (*odn_edit_dpm_table)(void *handle, uint32_t type, long *input, uint32_t size);
	int (*set_mp1_state)(void *handle, enum pp_mp1_state mp1_state);
	int (*smu_i2c_bus_access)(void *handle, bool acquire);
	int (*gfx_state_change_set)(void *handle, uint32_t state);
/* export to DC */
	u32 (*get_sclk)(void *handle, bool low);
	u32 (*get_mclk)(void *handle, bool low);
	int (*display_configuration_change)(void *handle,
		const struct amd_pp_display_configuration *input);
	int (*get_display_power_level)(void *handle,
		struct amd_pp_simple_clock_info *output);
	int (*get_current_clocks)(void *handle,
		struct amd_pp_clock_info *clocks);
	int (*get_clock_by_type)(void *handle,
		enum amd_pp_clock_type type,
		struct amd_pp_clocks *clocks);
	int (*get_clock_by_type_with_latency)(void *handle,
		enum amd_pp_clock_type type,
		struct pp_clock_levels_with_latency *clocks);
	int (*get_clock_by_type_with_voltage)(void *handle,
		enum amd_pp_clock_type type,
		struct pp_clock_levels_with_voltage *clocks);
	int (*set_watermarks_for_clocks_ranges)(void *handle,
						void *clock_ranges);
	int (*display_clock_voltage_request)(void *handle,
				struct pp_display_clock_request *clock);
	int (*get_display_mode_validation_clocks)(void *handle,
		struct amd_pp_simple_clock_info *clocks);
	int (*notify_smu_enable_pwe)(void *handle);
	int (*enable_mgpu_fan_boost)(void *handle);
	int (*set_active_display_count)(void *handle, uint32_t count);
	int (*set_hard_min_dcefclk_by_freq)(void *handle, uint32_t clock);
	int (*set_hard_min_fclk_by_freq)(void *handle, uint32_t clock);
	int (*set_min_deep_sleep_dcefclk)(void *handle, uint32_t clock);
	int (*get_asic_baco_capability)(void *handle, bool *cap);
	int (*get_asic_baco_state)(void *handle, int *state);
	int (*set_asic_baco_state)(void *handle, int state);
	int (*get_ppfeature_status)(void *handle, char *buf);
	int (*set_ppfeature_status)(void *handle, uint64_t ppfeature_masks);
	int (*asic_reset_mode_2)(void *handle);
	int (*set_df_cstate)(void *handle, enum pp_df_cstate state);
	int (*set_xgmi_pstate)(void *handle, uint32_t pstate);
	ssize_t (*get_gpu_metrics)(void *handle, void **table);
	int (*set_watermarks_for_clock_ranges)(void *handle,
					       struct pp_smu_wm_range_sets *ranges);
	int (*display_disable_memory_clock_switch)(void *handle,
						   bool disable_memory_clock_switch);
	int (*get_max_sustainable_clocks_by_dc)(void *handle,
						struct pp_smu_nv_clock_table *max_clocks);
	int (*get_uclk_dpm_states)(void *handle,
				   unsigned int *clock_values_in_khz,
				   unsigned int *num_states);
	int (*get_dpm_clock_table)(void *handle,
				   struct dpm_clocks *clock_table);
	int (*get_smu_prv_buf_details)(void *handle, void **addr, size_t *size);
	void (*pm_compute_clocks)(void *handle);
};

struct metrics_table_header {
	uint16_t			structure_size;
	uint8_t				format_revision;
	uint8_t				content_revision;
};

/*
 * gpu_metrics_v1_0 is not recommended as it's not naturally aligned.
 * Use gpu_metrics_v1_1 or later instead.
 */
struct gpu_metrics_v1_0 {
	struct metrics_table_header	common_header;

	/* Driver attached timestamp (in ns) */
	uint64_t			system_clock_counter;

	/* Temperature */
	uint16_t			temperature_edge;
	uint16_t			temperature_hotspot;
	uint16_t			temperature_mem;
	uint16_t			temperature_vrgfx;
	uint16_t			temperature_vrsoc;
	uint16_t			temperature_vrmem;

	/* Utilization */
	uint16_t			average_gfx_activity;
	uint16_t			average_umc_activity; // memory controller
	uint16_t			average_mm_activity; // UVD or VCN

	/* Power/Energy */
	uint16_t			average_socket_power;
	uint32_t			energy_accumulator;

	/* Average clocks */
	uint16_t			average_gfxclk_frequency;
	uint16_t			average_socclk_frequency;
	uint16_t			average_uclk_frequency;
	uint16_t			average_vclk0_frequency;
	uint16_t			average_dclk0_frequency;
	uint16_t			average_vclk1_frequency;
	uint16_t			average_dclk1_frequency;

	/* Current clocks */
	uint16_t			current_gfxclk;
	uint16_t			current_socclk;
	uint16_t			current_uclk;
	uint16_t			current_vclk0;
	uint16_t			current_dclk0;
	uint16_t			current_vclk1;
	uint16_t			current_dclk1;

	/* Throttle status */
	uint32_t			throttle_status;

	/* Fans */
	uint16_t			current_fan_speed;

	/* Link width/speed */
	uint8_t				pcie_link_width;
	uint8_t				pcie_link_speed; // in 0.1 GT/s
};

struct gpu_metrics_v1_1 {
	struct metrics_table_header	common_header;

	/* Temperature */
	uint16_t			temperature_edge;
	uint16_t			temperature_hotspot;
	uint16_t			temperature_mem;
	uint16_t			temperature_vrgfx;
	uint16_t			temperature_vrsoc;
	uint16_t			temperature_vrmem;

	/* Utilization */
	uint16_t			average_gfx_activity;
	uint16_t			average_umc_activity; // memory controller
	uint16_t			average_mm_activity; // UVD or VCN

	/* Power/Energy */
	uint16_t			average_socket_power;
	uint64_t			energy_accumulator;

	/* Driver attached timestamp (in ns) */
	uint64_t			system_clock_counter;

	/* Average clocks */
	uint16_t			average_gfxclk_frequency;
	uint16_t			average_socclk_frequency;
	uint16_t			average_uclk_frequency;
	uint16_t			average_vclk0_frequency;
	uint16_t			average_dclk0_frequency;
	uint16_t			average_vclk1_frequency;
	uint16_t			average_dclk1_frequency;

	/* Current clocks */
	uint16_t			current_gfxclk;
	uint16_t			current_socclk;
	uint16_t			current_uclk;
	uint16_t			current_vclk0;
	uint16_t			current_dclk0;
	uint16_t			current_vclk1;
	uint16_t			current_dclk1;

	/* Throttle status */
	uint32_t			throttle_status;

	/* Fans */
	uint16_t			current_fan_speed;

	/* Link width/speed */
	uint16_t			pcie_link_width;
	uint16_t			pcie_link_speed; // in 0.1 GT/s

	uint16_t			padding;

	uint32_t			gfx_activity_acc;
	uint32_t			mem_activity_acc;

	uint16_t			temperature_hbm[NUM_HBM_INSTANCES];
};

struct gpu_metrics_v1_2 {
	struct metrics_table_header	common_header;

	/* Temperature */
	uint16_t			temperature_edge;
	uint16_t			temperature_hotspot;
	uint16_t			temperature_mem;
	uint16_t			temperature_vrgfx;
	uint16_t			temperature_vrsoc;
	uint16_t			temperature_vrmem;

	/* Utilization */
	uint16_t			average_gfx_activity;
	uint16_t			average_umc_activity; // memory controller
	uint16_t			average_mm_activity; // UVD or VCN

	/* Power/Energy */
	uint16_t			average_socket_power;
	uint64_t			energy_accumulator;

	/* Driver attached timestamp (in ns) */
	uint64_t			system_clock_counter;

	/* Average clocks */
	uint16_t			average_gfxclk_frequency;
	uint16_t			average_socclk_frequency;
	uint16_t			average_uclk_frequency;
	uint16_t			average_vclk0_frequency;
	uint16_t			average_dclk0_frequency;
	uint16_t			average_vclk1_frequency;
	uint16_t			average_dclk1_frequency;

	/* Current clocks */
	uint16_t			current_gfxclk;
	uint16_t			current_socclk;
	uint16_t			current_uclk;
	uint16_t			current_vclk0;
	uint16_t			current_dclk0;
	uint16_t			current_vclk1;
	uint16_t			current_dclk1;

	/* Throttle status (ASIC dependent) */
	uint32_t			throttle_status;

	/* Fans */
	uint16_t			current_fan_speed;

	/* Link width/speed */
	uint16_t			pcie_link_width;
	uint16_t			pcie_link_speed; // in 0.1 GT/s

	uint16_t			padding;

	uint32_t			gfx_activity_acc;
	uint32_t			mem_activity_acc;

	uint16_t			temperature_hbm[NUM_HBM_INSTANCES];

	/* PMFW attached timestamp (10ns resolution) */
	uint64_t			firmware_timestamp;
};

struct gpu_metrics_v1_3 {
	struct metrics_table_header	common_header;

	/* Temperature */
	uint16_t			temperature_edge;
	uint16_t			temperature_hotspot;
	uint16_t			temperature_mem;
	uint16_t			temperature_vrgfx;
	uint16_t			temperature_vrsoc;
	uint16_t			temperature_vrmem;

	/* Utilization */
	uint16_t			average_gfx_activity;
	uint16_t			average_umc_activity; // memory controller
	uint16_t			average_mm_activity; // UVD or VCN

	/* Power/Energy */
	uint16_t			average_socket_power;
	uint64_t			energy_accumulator;

	/* Driver attached timestamp (in ns) */
	uint64_t			system_clock_counter;

	/* Average clocks */
	uint16_t			average_gfxclk_frequency;
	uint16_t			average_socclk_frequency;
	uint16_t			average_uclk_frequency;
	uint16_t			average_vclk0_frequency;
	uint16_t			average_dclk0_frequency;
	uint16_t			average_vclk1_frequency;
	uint16_t			average_dclk1_frequency;

	/* Current clocks */
	uint16_t			current_gfxclk;
	uint16_t			current_socclk;
	uint16_t			current_uclk;
	uint16_t			current_vclk0;
	uint16_t			current_dclk0;
	uint16_t			current_vclk1;
	uint16_t			current_dclk1;

	/* Throttle status */
	uint32_t			throttle_status;

	/* Fans */
	uint16_t			current_fan_speed;

	/* Link width/speed */
	uint16_t			pcie_link_width;
	uint16_t			pcie_link_speed; // in 0.1 GT/s

	uint16_t			padding;

	uint32_t			gfx_activity_acc;
	uint32_t			mem_activity_acc;

	uint16_t			temperature_hbm[NUM_HBM_INSTANCES];

	/* PMFW attached timestamp (10ns resolution) */
	uint64_t			firmware_timestamp;

	/* Voltage (mV) */
	uint16_t			voltage_soc;
	uint16_t			voltage_gfx;
	uint16_t			voltage_mem;

	uint16_t			padding1;

	/* Throttle status (ASIC independent) */
	uint64_t			indep_throttle_status;
};

/*
 * gpu_metrics_v2_0 is not recommended as it's not naturally aligned.
 * Use gpu_metrics_v2_1 or later instead.
 */
struct gpu_metrics_v2_0 {
	struct metrics_table_header	common_header;

	/* Driver attached timestamp (in ns) */
	uint64_t			system_clock_counter;

	/* Temperature */
	uint16_t			temperature_gfx; // gfx temperature on APUs
	uint16_t			temperature_soc; // soc temperature on APUs
	uint16_t			temperature_core[8]; // CPU core temperature on APUs
	uint16_t			temperature_l3[2];

	/* Utilization */
	uint16_t			average_gfx_activity;
	uint16_t			average_mm_activity; // UVD or VCN

	/* Power/Energy */
	uint16_t			average_socket_power; // dGPU + APU power on A + A platform
	uint16_t			average_cpu_power;
	uint16_t			average_soc_power;
	uint16_t			average_gfx_power;
	uint16_t			average_core_power[8]; // CPU core power on APUs

	/* Average clocks */
	uint16_t			average_gfxclk_frequency;
	uint16_t			average_socclk_frequency;
	uint16_t			average_uclk_frequency;
	uint16_t			average_fclk_frequency;
	uint16_t			average_vclk_frequency;
	uint16_t			average_dclk_frequency;

	/* Current clocks */
	uint16_t			current_gfxclk;
	uint16_t			current_socclk;
	uint16_t			current_uclk;
	uint16_t			current_fclk;
	uint16_t			current_vclk;
	uint16_t			current_dclk;
	uint16_t			current_coreclk[8]; // CPU core clocks
	uint16_t			current_l3clk[2];

	/* Throttle status */
	uint32_t			throttle_status;

	/* Fans */
	uint16_t			fan_pwm;

	uint16_t			padding;
};

struct gpu_metrics_v2_1 {
	struct metrics_table_header	common_header;

	/* Temperature */
	uint16_t			temperature_gfx; // gfx temperature on APUs
	uint16_t			temperature_soc; // soc temperature on APUs
	uint16_t			temperature_core[8]; // CPU core temperature on APUs
	uint16_t			temperature_l3[2];

	/* Utilization */
	uint16_t			average_gfx_activity;
	uint16_t			average_mm_activity; // UVD or VCN

	/* Driver attached timestamp (in ns) */
	uint64_t			system_clock_counter;

	/* Power/Energy */
	uint16_t			average_socket_power; // dGPU + APU power on A + A platform
	uint16_t			average_cpu_power;
	uint16_t			average_soc_power;
	uint16_t			average_gfx_power;
	uint16_t			average_core_power[8]; // CPU core power on APUs

	/* Average clocks */
	uint16_t			average_gfxclk_frequency;
	uint16_t			average_socclk_frequency;
	uint16_t			average_uclk_frequency;
	uint16_t			average_fclk_frequency;
	uint16_t			average_vclk_frequency;
	uint16_t			average_dclk_frequency;

	/* Current clocks */
	uint16_t			current_gfxclk;
	uint16_t			current_socclk;
	uint16_t			current_uclk;
	uint16_t			current_fclk;
	uint16_t			current_vclk;
	uint16_t			current_dclk;
	uint16_t			current_coreclk[8]; // CPU core clocks
	uint16_t			current_l3clk[2];

	/* Throttle status */
	uint32_t			throttle_status;

	/* Fans */
	uint16_t			fan_pwm;

	uint16_t			padding[3];
};

struct gpu_metrics_v2_2 {
	struct metrics_table_header	common_header;

	/* Temperature */
	uint16_t			temperature_gfx; // gfx temperature on APUs
	uint16_t			temperature_soc; // soc temperature on APUs
	uint16_t			temperature_core[8]; // CPU core temperature on APUs
	uint16_t			temperature_l3[2];

	/* Utilization */
	uint16_t			average_gfx_activity;
	uint16_t			average_mm_activity; // UVD or VCN

	/* Driver attached timestamp (in ns) */
	uint64_t			system_clock_counter;

	/* Power/Energy */
	uint16_t			average_socket_power; // dGPU + APU power on A + A platform
	uint16_t			average_cpu_power;
	uint16_t			average_soc_power;
	uint16_t			average_gfx_power;
	uint16_t			average_core_power[8]; // CPU core power on APUs

	/* Average clocks */
	uint16_t			average_gfxclk_frequency;
	uint16_t			average_socclk_frequency;
	uint16_t			average_uclk_frequency;
	uint16_t			average_fclk_frequency;
	uint16_t			average_vclk_frequency;
	uint16_t			average_dclk_frequency;

	/* Current clocks */
	uint16_t			current_gfxclk;
	uint16_t			current_socclk;
	uint16_t			current_uclk;
	uint16_t			current_fclk;
	uint16_t			current_vclk;
	uint16_t			current_dclk;
	uint16_t			current_coreclk[8]; // CPU core clocks
	uint16_t			current_l3clk[2];

	/* Throttle status (ASIC dependent) */
	uint32_t			throttle_status;

	/* Fans */
	uint16_t			fan_pwm;

	uint16_t			padding[3];

	/* Throttle status (ASIC independent) */
	uint64_t			indep_throttle_status;
};

#endif
