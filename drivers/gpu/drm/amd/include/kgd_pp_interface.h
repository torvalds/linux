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
	OD_SCLK,
	OD_MCLK,
	OD_RANGE,
};

enum amd_pp_sensors {
	AMDGPU_PP_SENSOR_GFX_SCLK = 0,
	AMDGPU_PP_SENSOR_VDDNB,
	AMDGPU_PP_SENSOR_VDDGFX,
	AMDGPU_PP_SENSOR_UVD_VCLK,
	AMDGPU_PP_SENSOR_UVD_DCLK,
	AMDGPU_PP_SENSOR_VCE_ECCLK,
	AMDGPU_PP_SENSOR_GPU_LOAD,
	AMDGPU_PP_SENSOR_GFX_MCLK,
	AMDGPU_PP_SENSOR_GPU_TEMP,
	AMDGPU_PP_SENSOR_VCE_POWER,
	AMDGPU_PP_SENSOR_UVD_POWER,
	AMDGPU_PP_SENSOR_GPU_POWER,
	AMDGPU_PP_SENSOR_STABLE_PSTATE_SCLK,
	AMDGPU_PP_SENSOR_STABLE_PSTATE_MCLK,
};

enum amd_pp_task {
	AMD_PP_TASK_DISPLAY_CONFIG_CHANGE,
	AMD_PP_TASK_ENABLE_USER_STATE,
	AMD_PP_TASK_READJUST_POWER_STATE,
	AMD_PP_TASK_COMPLETE_INIT,
	AMD_PP_TASK_MAX
};

enum PP_SMC_POWER_PROFILE {
	PP_SMC_POWER_PROFILE_FULLSCREEN3D = 0x0,
	PP_SMC_POWER_PROFILE_POWERSAVING  = 0x1,
	PP_SMC_POWER_PROFILE_VIDEO        = 0x2,
	PP_SMC_POWER_PROFILE_VR           = 0x3,
	PP_SMC_POWER_PROFILE_COMPUTE      = 0x4,
	PP_SMC_POWER_PROFILE_CUSTOM       = 0x5,
};

enum {
	PP_GROUP_UNKNOWN = 0,
	PP_GROUP_GFX = 1,
	PP_GROUP_SYS,
	PP_GROUP_MAX
};

enum PP_OD_DPM_TABLE_COMMAND {
	PP_OD_EDIT_SCLK_VDDC_TABLE,
	PP_OD_EDIT_MCLK_VDDC_TABLE,
	PP_OD_RESTORE_DEFAULT_TABLE,
	PP_OD_COMMIT_DPM_TABLE
};

struct pp_states_info {
	uint32_t nums;
	uint32_t states[16];
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

struct seq_file;
enum amd_pp_clock_type;
struct amd_pp_simple_clock_info;
struct amd_pp_display_configuration;
struct amd_pp_clock_info;
struct pp_display_clock_request;
struct pp_wm_sets_with_clock_ranges_soc15;
struct pp_clock_levels_with_voltage;
struct pp_clock_levels_with_latency;
struct amd_pp_clocks;

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
	void (*set_fan_control_mode)(void *handle, u32 mode);
	u32 (*get_fan_control_mode)(void *handle);
	int (*set_fan_speed_percent)(void *handle, u32 speed);
	int (*get_fan_speed_percent)(void *handle, u32 *speed);
	int (*force_clock_level)(void *handle, enum pp_clock_type type, uint32_t mask);
	int (*print_clock_levels)(void *handle, enum pp_clock_type type, char *buf);
	int (*force_performance_level)(void *handle, enum amd_dpm_forced_level level);
	int (*get_sclk_od)(void *handle);
	int (*set_sclk_od)(void *handle, uint32_t value);
	int (*get_mclk_od)(void *handle);
	int (*set_mclk_od)(void *handle, uint32_t value);
	int (*read_sensor)(void *handle, int idx, void *value, int *size);
	enum amd_dpm_forced_level (*get_performance_level)(void *handle);
	enum amd_pm_state_type (*get_current_power_state)(void *handle);
	int (*get_fan_speed_rpm)(void *handle, uint32_t *rpm);
	int (*get_pp_num_states)(void *handle, struct pp_states_info *data);
	int (*get_pp_table)(void *handle, char **table);
	int (*set_pp_table)(void *handle, const char *buf, size_t size);
	void (*debugfs_print_current_performance_level)(void *handle, struct seq_file *m);
	int (*switch_power_profile)(void *handle, enum PP_SMC_POWER_PROFILE type, bool en);
/* export to amdgpu */
	void (*powergate_uvd)(void *handle, bool gate);
	void (*powergate_vce)(void *handle, bool gate);
	struct amd_vce_state *(*get_vce_clock_state)(void *handle, u32 idx);
	int (*dispatch_tasks)(void *handle, enum amd_pp_task task_id,
			enum amd_pm_state_type *user_state);
	int (*load_firmware)(void *handle);
	int (*wait_for_fw_loading_complete)(void *handle);
	int (*set_clockgating_by_smu)(void *handle, uint32_t msg_id);
	int (*set_power_limit)(void *handle, uint32_t n);
	int (*get_power_limit)(void *handle, uint32_t *limit, bool default_limit);
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
		struct pp_wm_sets_with_clock_ranges_soc15 *wm_with_clock_ranges);
	int (*display_clock_voltage_request)(void *handle,
				struct pp_display_clock_request *clock);
	int (*get_display_mode_validation_clocks)(void *handle,
		struct amd_pp_simple_clock_info *clocks);
	int (*get_power_profile_mode)(void *handle, char *buf);
	int (*set_power_profile_mode)(void *handle, long *input, uint32_t size);
	int (*odn_edit_dpm_table)(void *handle, uint32_t type, long *input, uint32_t size);
	int (*set_mmhub_powergating_by_smu)(void *handle);
};

#endif
