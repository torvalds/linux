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
#include "dm_pp_interface.h"

extern const struct amd_ip_funcs pp_ip_funcs;
extern const struct amd_pm_funcs pp_dpm_funcs;

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
};

enum amd_pp_task {
	AMD_PP_TASK_DISPLAY_CONFIG_CHANGE,
	AMD_PP_TASK_ENABLE_USER_STATE,
	AMD_PP_TASK_READJUST_POWER_STATE,
	AMD_PP_TASK_COMPLETE_INIT,
	AMD_PP_TASK_MAX
};

struct amd_pp_init {
	struct cgs_device *device;
	uint32_t chip_family;
	uint32_t chip_id;
	bool pm_en;
	uint32_t feature_mask;
};



enum {
	PP_GROUP_UNKNOWN = 0,
	PP_GROUP_GFX = 1,
	PP_GROUP_SYS,
	PP_GROUP_MAX
};

struct pp_states_info {
	uint32_t nums;
	uint32_t states[16];
};

struct pp_gpu_power {
	uint32_t vddc_power;
	uint32_t vddci_power;
	uint32_t max_gpu_power;
	uint32_t average_gpu_power;
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

struct amd_powerplay {
	struct cgs_device *cgs_device;
	void *pp_handle;
	const struct amd_ip_funcs *ip_funcs;
	const struct amd_pm_funcs *pp_funcs;
};


#endif /* _AMD_POWERPLAY_H_ */
