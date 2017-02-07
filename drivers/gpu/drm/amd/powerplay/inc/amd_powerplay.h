/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
#ifndef _AMD_POWERPLAY_H_
#define _AMD_POWERPLAY_H_

#include <linux/seq_file.h>
#include <linux/types.h>
#include <linux/errno.h>
#include "amd_shared.h"
#include "cgs_common.h"

extern const struct amd_ip_funcs pp_ip_funcs;
extern const struct amd_powerplay_funcs pp_dpm_funcs;

#define PP_DPM_DISABLED 0xCCCC

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
};

enum amd_pp_event {
	AMD_PP_EVENT_INITIALIZE = 0,
	AMD_PP_EVENT_UNINITIALIZE,
	AMD_PP_EVENT_POWER_SOURCE_CHANGE,
	AMD_PP_EVENT_SUSPEND,
	AMD_PP_EVENT_RESUME,
	AMD_PP_EVENT_ENTER_REST_STATE,
	AMD_PP_EVENT_EXIT_REST_STATE,
	AMD_PP_EVENT_DISPLAY_CONFIG_CHANGE,
	AMD_PP_EVENT_THERMAL_NOTIFICATION,
	AMD_PP_EVENT_VBIOS_NOTIFICATION,
	AMD_PP_EVENT_ENTER_THERMAL_STATE,
	AMD_PP_EVENT_EXIT_THERMAL_STATE,
	AMD_PP_EVENT_ENTER_FORCED_STATE,
	AMD_PP_EVENT_EXIT_FORCED_STATE,
	AMD_PP_EVENT_ENTER_EXCLUSIVE_MODE,
	AMD_PP_EVENT_EXIT_EXCLUSIVE_MODE,
	AMD_PP_EVENT_ENTER_SCREEN_SAVER,
	AMD_PP_EVENT_EXIT_SCREEN_SAVER,
	AMD_PP_EVENT_VPU_RECOVERY_BEGIN,
	AMD_PP_EVENT_VPU_RECOVERY_END,
	AMD_PP_EVENT_ENABLE_POWER_PLAY,
	AMD_PP_EVENT_DISABLE_POWER_PLAY,
	AMD_PP_EVENT_CHANGE_POWER_SOURCE_UI_LABEL,
	AMD_PP_EVENT_ENABLE_USER2D_PERFORMANCE,
	AMD_PP_EVENT_DISABLE_USER2D_PERFORMANCE,
	AMD_PP_EVENT_ENABLE_USER3D_PERFORMANCE,
	AMD_PP_EVENT_DISABLE_USER3D_PERFORMANCE,
	AMD_PP_EVENT_ENABLE_OVER_DRIVE_TEST,
	AMD_PP_EVENT_DISABLE_OVER_DRIVE_TEST,
	AMD_PP_EVENT_ENABLE_REDUCED_REFRESH_RATE,
	AMD_PP_EVENT_DISABLE_REDUCED_REFRESH_RATE,
	AMD_PP_EVENT_ENABLE_GFX_CLOCK_GATING,
	AMD_PP_EVENT_DISABLE_GFX_CLOCK_GATING,
	AMD_PP_EVENT_ENABLE_CGPG,
	AMD_PP_EVENT_DISABLE_CGPG,
	AMD_PP_EVENT_ENTER_TEXT_MODE,
	AMD_PP_EVENT_EXIT_TEXT_MODE,
	AMD_PP_EVENT_VIDEO_START,
	AMD_PP_EVENT_VIDEO_STOP,
	AMD_PP_EVENT_ENABLE_USER_STATE,
	AMD_PP_EVENT_DISABLE_USER_STATE,
	AMD_PP_EVENT_READJUST_POWER_STATE,
	AMD_PP_EVENT_START_INACTIVITY,
	AMD_PP_EVENT_STOP_INACTIVITY,
	AMD_PP_EVENT_LINKED_ADAPTERS_READY,
	AMD_PP_EVENT_ADAPTER_SAFE_TO_DISABLE,
	AMD_PP_EVENT_COMPLETE_INIT,
	AMD_PP_EVENT_CRITICAL_THERMAL_FAULT,
	AMD_PP_EVENT_BACKLIGHT_CHANGED,
	AMD_PP_EVENT_ENABLE_VARI_BRIGHT,
	AMD_PP_EVENT_DISABLE_VARI_BRIGHT,
	AMD_PP_EVENT_ENABLE_VARI_BRIGHT_ON_POWER_XPRESS,
	AMD_PP_EVENT_DISABLE_VARI_BRIGHT_ON_POWER_XPRESS,
	AMD_PP_EVENT_SET_VARI_BRIGHT_LEVEL,
	AMD_PP_EVENT_VARI_BRIGHT_MONITOR_MEASUREMENT,
	AMD_PP_EVENT_SCREEN_ON,
	AMD_PP_EVENT_SCREEN_OFF,
	AMD_PP_EVENT_PRE_DISPLAY_CONFIG_CHANGE,
	AMD_PP_EVENT_ENTER_ULP_STATE,
	AMD_PP_EVENT_EXIT_ULP_STATE,
	AMD_PP_EVENT_REGISTER_IP_STATE,
	AMD_PP_EVENT_UNREGISTER_IP_STATE,
	AMD_PP_EVENT_ENTER_MGPU_MODE,
	AMD_PP_EVENT_EXIT_MGPU_MODE,
	AMD_PP_EVENT_ENTER_MULTI_GPU_MODE,
	AMD_PP_EVENT_PRE_SUSPEND,
	AMD_PP_EVENT_PRE_RESUME,
	AMD_PP_EVENT_ENTER_BACOS,
	AMD_PP_EVENT_EXIT_BACOS,
	AMD_PP_EVENT_RESUME_BACO,
	AMD_PP_EVENT_RESET_BACO,
	AMD_PP_EVENT_PRE_DISPLAY_PHY_ACCESS,
	AMD_PP_EVENT_POST_DISPLAY_PHY_CCESS,
	AMD_PP_EVENT_START_COMPUTE_APPLICATION,
	AMD_PP_EVENT_STOP_COMPUTE_APPLICATION,
	AMD_PP_EVENT_REDUCE_POWER_LIMIT,
	AMD_PP_EVENT_ENTER_FRAME_LOCK,
	AMD_PP_EVENT_EXIT_FRAME_LOOCK,
	AMD_PP_EVENT_LONG_IDLE_REQUEST_BACO,
	AMD_PP_EVENT_LONG_IDLE_ENTER_BACO,
	AMD_PP_EVENT_LONG_IDLE_EXIT_BACO,
	AMD_PP_EVENT_HIBERNATE,
	AMD_PP_EVENT_CONNECTED_STANDBY,
	AMD_PP_EVENT_ENTER_SELF_REFRESH,
	AMD_PP_EVENT_EXIT_SELF_REFRESH,
	AMD_PP_EVENT_START_AVFS_BTC,
	AMD_PP_EVENT_MAX
};

struct amd_pp_init {
	struct cgs_device *device;
	uint32_t chip_family;
	uint32_t chip_id;
	bool pm_en;
	uint32_t feature_mask;
};

enum amd_pp_display_config_type{
	AMD_PP_DisplayConfigType_None = 0,
	AMD_PP_DisplayConfigType_DP54 ,
	AMD_PP_DisplayConfigType_DP432 ,
	AMD_PP_DisplayConfigType_DP324 ,
	AMD_PP_DisplayConfigType_DP27,
	AMD_PP_DisplayConfigType_DP243,
	AMD_PP_DisplayConfigType_DP216,
	AMD_PP_DisplayConfigType_DP162,
	AMD_PP_DisplayConfigType_HDMI6G ,
	AMD_PP_DisplayConfigType_HDMI297 ,
	AMD_PP_DisplayConfigType_HDMI162,
	AMD_PP_DisplayConfigType_LVDS,
	AMD_PP_DisplayConfigType_DVI,
	AMD_PP_DisplayConfigType_WIRELESS,
	AMD_PP_DisplayConfigType_VGA
};

struct single_display_configuration
{
	uint32_t controller_index;
	uint32_t controller_id;
	uint32_t signal_type;
	uint32_t display_state;
	/* phy id for the primary internal transmitter */
	uint8_t primary_transmitter_phyi_d;
	/* bitmap with the active lanes */
	uint8_t primary_transmitter_active_lanemap;
	/* phy id for the secondary internal transmitter (for dual-link dvi) */
	uint8_t secondary_transmitter_phy_id;
	/* bitmap with the active lanes */
	uint8_t secondary_transmitter_active_lanemap;
	/* misc phy settings for SMU. */
	uint32_t config_flags;
	uint32_t display_type;
	uint32_t view_resolution_cx;
	uint32_t view_resolution_cy;
	enum amd_pp_display_config_type displayconfigtype;
	uint32_t vertical_refresh; /* for active display */
};

#define MAX_NUM_DISPLAY 32

struct amd_pp_display_configuration {
	bool nb_pstate_switch_disable;/* controls NB PState switch */
	bool cpu_cc6_disable; /* controls CPU CState switch ( on or off) */
	bool cpu_pstate_disable;
	uint32_t cpu_pstate_separation_time;

	uint32_t num_display;  /* total number of display*/
	uint32_t num_path_including_non_display;
	uint32_t crossfire_display_index;
	uint32_t min_mem_set_clock;
	uint32_t min_core_set_clock;
	/* unit 10KHz x bit*/
	uint32_t min_bus_bandwidth;
	/* minimum required stutter sclk, in 10khz uint32_t ulMinCoreSetClk;*/
	uint32_t min_core_set_clock_in_sr;

	struct single_display_configuration displays[MAX_NUM_DISPLAY];

	uint32_t vrefresh; /* for active display*/

	uint32_t min_vblank_time; /* for active display*/
	bool multi_monitor_in_sync;
	/* Controller Index of primary display - used in MCLK SMC switching hang
	 * SW Workaround*/
	uint32_t crtc_index;
	/* htotal*1000/pixelclk - used in MCLK SMC switching hang SW Workaround*/
	uint32_t line_time_in_us;
	bool invalid_vblank_time;

	uint32_t display_clk;
	/*
	 * for given display configuration if multimonitormnsync == false then
	 * Memory clock DPMS with this latency or below is allowed, DPMS with
	 * higher latency not allowed.
	 */
	uint32_t dce_tolerable_mclk_in_active_latency;
};

struct amd_pp_simple_clock_info {
	uint32_t	engine_max_clock;
	uint32_t	memory_max_clock;
	uint32_t	level;
};

enum PP_DAL_POWERLEVEL {
	PP_DAL_POWERLEVEL_INVALID = 0,
	PP_DAL_POWERLEVEL_ULTRALOW,
	PP_DAL_POWERLEVEL_LOW,
	PP_DAL_POWERLEVEL_NOMINAL,
	PP_DAL_POWERLEVEL_PERFORMANCE,

	PP_DAL_POWERLEVEL_0 = PP_DAL_POWERLEVEL_ULTRALOW,
	PP_DAL_POWERLEVEL_1 = PP_DAL_POWERLEVEL_LOW,
	PP_DAL_POWERLEVEL_2 = PP_DAL_POWERLEVEL_NOMINAL,
	PP_DAL_POWERLEVEL_3 = PP_DAL_POWERLEVEL_PERFORMANCE,
	PP_DAL_POWERLEVEL_4 = PP_DAL_POWERLEVEL_3+1,
	PP_DAL_POWERLEVEL_5 = PP_DAL_POWERLEVEL_4+1,
	PP_DAL_POWERLEVEL_6 = PP_DAL_POWERLEVEL_5+1,
	PP_DAL_POWERLEVEL_7 = PP_DAL_POWERLEVEL_6+1,
};

struct amd_pp_clock_info {
	uint32_t min_engine_clock;
	uint32_t max_engine_clock;
	uint32_t min_memory_clock;
	uint32_t max_memory_clock;
	uint32_t min_bus_bandwidth;
	uint32_t max_bus_bandwidth;
	uint32_t max_engine_clock_in_sr;
	uint32_t min_engine_clock_in_sr;
	enum PP_DAL_POWERLEVEL max_clocks_state;
};

enum amd_pp_clock_type {
	amd_pp_disp_clock = 1,
	amd_pp_sys_clock,
	amd_pp_mem_clock
};

#define MAX_NUM_CLOCKS 16

struct amd_pp_clocks {
	uint32_t count;
	uint32_t clock[MAX_NUM_CLOCKS];
	uint32_t latency[MAX_NUM_CLOCKS];
};


enum {
	PP_GROUP_UNKNOWN = 0,
	PP_GROUP_GFX = 1,
	PP_GROUP_SYS,
	PP_GROUP_MAX
};

enum pp_clock_type {
	PP_SCLK,
	PP_MCLK,
	PP_PCIE,
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

#define PP_CG_MSG_ID(group, block, support, state) (group << PP_GROUP_SHIFT |\
								block << PP_BLOCK_SHIFT |\
								support << PP_STATE_SUPPORT_SHIFT |\
								state << PP_STATE_SHIFT)

struct amd_powerplay_funcs {
	int (*get_temperature)(void *handle);
	int (*load_firmware)(void *handle);
	int (*wait_for_fw_loading_complete)(void *handle);
	int (*force_performance_level)(void *handle, enum amd_dpm_forced_level level);
	enum amd_dpm_forced_level (*get_performance_level)(void *handle);
	enum amd_pm_state_type (*get_current_power_state)(void *handle);
	int (*get_sclk)(void *handle, bool low);
	int (*get_mclk)(void *handle, bool low);
	int (*powergate_vce)(void *handle, bool gate);
	int (*powergate_uvd)(void *handle, bool gate);
	int (*dispatch_tasks)(void *handle, enum amd_pp_event event_id,
				   void *input, void *output);
	int (*set_fan_control_mode)(void *handle, uint32_t mode);
	int (*get_fan_control_mode)(void *handle);
	int (*set_fan_speed_percent)(void *handle, uint32_t percent);
	int (*get_fan_speed_percent)(void *handle, uint32_t *speed);
	int (*get_fan_speed_rpm)(void *handle, uint32_t *rpm);
	int (*get_pp_num_states)(void *handle, struct pp_states_info *data);
	int (*get_pp_table)(void *handle, char **table);
	int (*set_pp_table)(void *handle, const char *buf, size_t size);
	int (*force_clock_level)(void *handle, enum pp_clock_type type, uint32_t mask);
	int (*print_clock_levels)(void *handle, enum pp_clock_type type, char *buf);
	int (*get_sclk_od)(void *handle);
	int (*set_sclk_od)(void *handle, uint32_t value);
	int (*get_mclk_od)(void *handle);
	int (*set_mclk_od)(void *handle, uint32_t value);
	int (*read_sensor)(void *handle, int idx, void *value);
	struct amd_vce_state* (*get_vce_clock_state)(void *handle, unsigned idx);
	int (*reset_power_profile_state)(void *handle,
			struct amd_pp_profile *request);
	int (*get_power_profile_state)(void *handle,
			struct amd_pp_profile *query);
	int (*set_power_profile_state)(void *handle,
			struct amd_pp_profile *request);
	int (*switch_power_profile)(void *handle,
			enum amd_pp_profile_type type);
};

struct amd_powerplay {
	void *pp_handle;
	const struct amd_ip_funcs *ip_funcs;
	const struct amd_powerplay_funcs *pp_funcs;
};

int amd_powerplay_create(struct amd_pp_init *pp_init,
				void **handle);

int amd_powerplay_destroy(void *handle);

int amd_powerplay_reset(void *handle);

int amd_powerplay_display_configuration_change(void *handle,
		const struct amd_pp_display_configuration *input);

int amd_powerplay_get_display_power_level(void *handle,
		struct amd_pp_simple_clock_info *output);

int amd_powerplay_get_current_clocks(void *handle,
		struct amd_pp_clock_info *output);

int amd_powerplay_get_clock_by_type(void *handle,
		enum amd_pp_clock_type type,
		struct amd_pp_clocks *clocks);

int amd_powerplay_get_display_mode_validation_clocks(void *handle,
		struct amd_pp_simple_clock_info *output);

int amd_set_clockgating_by_smu(void *handle, uint32_t msg_id);

#endif /* _AMD_POWERPLAY_H_ */
