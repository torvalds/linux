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
 */

#ifndef __AMD_SHARED_H__
#define __AMD_SHARED_H__

#include <drm/amd_asic_type.h>


#define AMD_MAX_USEC_TIMEOUT		200000  /* 200 ms */

/*
 * Chip flags
 */
enum amd_chip_flags {
	AMD_ASIC_MASK = 0x0000ffffUL,
	AMD_FLAGS_MASK  = 0xffff0000UL,
	AMD_IS_MOBILITY = 0x00010000UL,
	AMD_IS_APU      = 0x00020000UL,
	AMD_IS_PX       = 0x00040000UL,
	AMD_EXP_HW_SUPPORT = 0x00080000UL,
};

enum amd_ip_block_type {
	AMD_IP_BLOCK_TYPE_COMMON,
	AMD_IP_BLOCK_TYPE_GMC,
	AMD_IP_BLOCK_TYPE_IH,
	AMD_IP_BLOCK_TYPE_SMC,
	AMD_IP_BLOCK_TYPE_PSP,
	AMD_IP_BLOCK_TYPE_DCE,
	AMD_IP_BLOCK_TYPE_GFX,
	AMD_IP_BLOCK_TYPE_SDMA,
	AMD_IP_BLOCK_TYPE_UVD,
	AMD_IP_BLOCK_TYPE_VCE,
	AMD_IP_BLOCK_TYPE_ACP,
	AMD_IP_BLOCK_TYPE_VCN
};

enum amd_clockgating_state {
	AMD_CG_STATE_GATE = 0,
	AMD_CG_STATE_UNGATE,
};


enum amd_powergating_state {
	AMD_PG_STATE_GATE = 0,
	AMD_PG_STATE_UNGATE,
};


/* CG flags */
#define AMD_CG_SUPPORT_GFX_MGCG			(1 << 0)
#define AMD_CG_SUPPORT_GFX_MGLS			(1 << 1)
#define AMD_CG_SUPPORT_GFX_CGCG			(1 << 2)
#define AMD_CG_SUPPORT_GFX_CGLS			(1 << 3)
#define AMD_CG_SUPPORT_GFX_CGTS			(1 << 4)
#define AMD_CG_SUPPORT_GFX_CGTS_LS		(1 << 5)
#define AMD_CG_SUPPORT_GFX_CP_LS		(1 << 6)
#define AMD_CG_SUPPORT_GFX_RLC_LS		(1 << 7)
#define AMD_CG_SUPPORT_MC_LS			(1 << 8)
#define AMD_CG_SUPPORT_MC_MGCG			(1 << 9)
#define AMD_CG_SUPPORT_SDMA_LS			(1 << 10)
#define AMD_CG_SUPPORT_SDMA_MGCG		(1 << 11)
#define AMD_CG_SUPPORT_BIF_LS			(1 << 12)
#define AMD_CG_SUPPORT_UVD_MGCG			(1 << 13)
#define AMD_CG_SUPPORT_VCE_MGCG			(1 << 14)
#define AMD_CG_SUPPORT_HDP_LS			(1 << 15)
#define AMD_CG_SUPPORT_HDP_MGCG			(1 << 16)
#define AMD_CG_SUPPORT_ROM_MGCG			(1 << 17)
#define AMD_CG_SUPPORT_DRM_LS			(1 << 18)
#define AMD_CG_SUPPORT_BIF_MGCG			(1 << 19)
#define AMD_CG_SUPPORT_GFX_3D_CGCG		(1 << 20)
#define AMD_CG_SUPPORT_GFX_3D_CGLS		(1 << 21)
#define AMD_CG_SUPPORT_DRM_MGCG			(1 << 22)
#define AMD_CG_SUPPORT_DF_MGCG			(1 << 23)
#define AMD_CG_SUPPORT_VCN_MGCG			(1 << 24)
/* PG flags */
#define AMD_PG_SUPPORT_GFX_PG			(1 << 0)
#define AMD_PG_SUPPORT_GFX_SMG			(1 << 1)
#define AMD_PG_SUPPORT_GFX_DMG			(1 << 2)
#define AMD_PG_SUPPORT_UVD			(1 << 3)
#define AMD_PG_SUPPORT_VCE			(1 << 4)
#define AMD_PG_SUPPORT_CP			(1 << 5)
#define AMD_PG_SUPPORT_GDS			(1 << 6)
#define AMD_PG_SUPPORT_RLC_SMU_HS		(1 << 7)
#define AMD_PG_SUPPORT_SDMA			(1 << 8)
#define AMD_PG_SUPPORT_ACP			(1 << 9)
#define AMD_PG_SUPPORT_SAMU			(1 << 10)
#define AMD_PG_SUPPORT_GFX_QUICK_MG		(1 << 11)
#define AMD_PG_SUPPORT_GFX_PIPELINE		(1 << 12)
#define AMD_PG_SUPPORT_MMHUB			(1 << 13)
#define AMD_PG_SUPPORT_VCN			(1 << 14)
#define AMD_PG_SUPPORT_VCN_DPG	(1 << 15)

enum PP_FEATURE_MASK {
	PP_SCLK_DPM_MASK = 0x1,
	PP_MCLK_DPM_MASK = 0x2,
	PP_PCIE_DPM_MASK = 0x4,
	PP_SCLK_DEEP_SLEEP_MASK = 0x8,
	PP_POWER_CONTAINMENT_MASK = 0x10,
	PP_UVD_HANDSHAKE_MASK = 0x20,
	PP_SMC_VOLTAGE_CONTROL_MASK = 0x40,
	PP_VBI_TIME_SUPPORT_MASK = 0x80,
	PP_ULV_MASK = 0x100,
	PP_ENABLE_GFX_CG_THRU_SMU = 0x200,
	PP_CLOCK_STRETCH_MASK = 0x400,
	PP_OD_FUZZY_FAN_CONTROL_MASK = 0x800,
	PP_SOCCLK_DPM_MASK = 0x1000,
	PP_DCEFCLK_DPM_MASK = 0x2000,
	PP_OVERDRIVE_MASK = 0x4000,
	PP_GFXOFF_MASK = 0x8000,
	PP_ACG_MASK = 0x10000,
	PP_STUTTER_MODE = 0x20000,
	PP_AVFS_MASK = 0x40000,
};

enum DC_FEATURE_MASK {
	DC_FBC_MASK = 0x1,
};

/**
 * struct amd_ip_funcs - general hooks for managing amdgpu IP Blocks
 */
struct amd_ip_funcs {
	/** @name: Name of IP block */
	char *name;
	/**
	 * @early_init:
	 *
	 * sets up early driver state (pre sw_init),
	 * does not configure hw - Optional
	 */
	int (*early_init)(void *handle);
	/** @late_init: sets up late driver/hw state (post hw_init) - Optional */
	int (*late_init)(void *handle);
	/** @sw_init: sets up driver state, does not configure hw */
	int (*sw_init)(void *handle);
	/** @sw_fini: tears down driver state, does not configure hw */
	int (*sw_fini)(void *handle);
	/** @hw_init: sets up the hw state */
	int (*hw_init)(void *handle);
	/** @hw_fini: tears down the hw state */
	int (*hw_fini)(void *handle);
	/** @late_fini: final cleanup */
	void (*late_fini)(void *handle);
	/** @suspend: handles IP specific hw/sw changes for suspend */
	int (*suspend)(void *handle);
	/** @resume: handles IP specific hw/sw changes for resume */
	int (*resume)(void *handle);
	/** @is_idle: returns current IP block idle status */
	bool (*is_idle)(void *handle);
	/** @wait_for_idle: poll for idle */
	int (*wait_for_idle)(void *handle);
	/** @check_soft_reset: check soft reset the IP block */
	bool (*check_soft_reset)(void *handle);
	/** @pre_soft_reset: pre soft reset the IP block */
	int (*pre_soft_reset)(void *handle);
	/** @soft_reset: soft reset the IP block */
	int (*soft_reset)(void *handle);
	/** @post_soft_reset: post soft reset the IP block */
	int (*post_soft_reset)(void *handle);
	/** @set_clockgating_state: enable/disable cg for the IP block */
	int (*set_clockgating_state)(void *handle,
				     enum amd_clockgating_state state);
	/** @set_powergating_state: enable/disable pg for the IP block */
	int (*set_powergating_state)(void *handle,
				     enum amd_powergating_state state);
	/** @get_clockgating_state: get current clockgating status */
	void (*get_clockgating_state)(void *handle, u32 *flags);
};


#endif /* __AMD_SHARED_H__ */
