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
#include "amd_shared.h"
#include "cgs_common.h"


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

enum amd_dpm_forced_level {
	AMD_DPM_FORCED_LEVEL_AUTO = 0,
	AMD_DPM_FORCED_LEVEL_LOW = 1,
	AMD_DPM_FORCED_LEVEL_HIGH = 2,
};

struct amd_pp_init {
	struct cgs_device *device;
	uint32_t chip_family;
	uint32_t chip_id;
	uint32_t rev_id;
};

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
	void (*print_current_performance_level)(void *handle,
						      struct seq_file *m);
};

struct amd_powerplay {
	void *pp_handle;
	const struct amd_ip_funcs *ip_funcs;
	const struct amd_powerplay_funcs *pp_funcs;
};

int amd_powerplay_init(struct amd_pp_init *pp_init,
		       struct amd_powerplay *amd_pp);
int amd_powerplay_fini(void *handle);

#endif /* _AMD_POWERPLAY_H_ */
