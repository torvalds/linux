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

#define AMD_MAX_USEC_TIMEOUT		100000  /* 100 ms */

/*
* Supported GPU families (aligned with amdgpu_drm.h)
*/
#define AMD_FAMILY_UNKNOWN              0
#define AMD_FAMILY_CI                   120 /* Bonaire, Hawaii */
#define AMD_FAMILY_KV                   125 /* Kaveri, Kabini, Mullins */
#define AMD_FAMILY_VI                   130 /* Iceland, Tonga */
#define AMD_FAMILY_CZ                   135 /* Carrizo */

/*
 * Supported ASIC types
 */
enum amd_asic_type {
	CHIP_BONAIRE = 0,
	CHIP_KAVERI,
	CHIP_KABINI,
	CHIP_HAWAII,
	CHIP_MULLINS,
	CHIP_TOPAZ,
	CHIP_TONGA,
	CHIP_FIJI,
	CHIP_CARRIZO,
	CHIP_STONEY,
	CHIP_LAST,
};

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
	AMD_IP_BLOCK_TYPE_DCE,
	AMD_IP_BLOCK_TYPE_GFX,
	AMD_IP_BLOCK_TYPE_SDMA,
	AMD_IP_BLOCK_TYPE_UVD,
	AMD_IP_BLOCK_TYPE_VCE,
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

struct amd_ip_funcs {
	/* sets up early driver state (pre sw_init), does not configure hw - Optional */
	int (*early_init)(void *handle);
	/* sets up late driver/hw state (post hw_init) - Optional */
	int (*late_init)(void *handle);
	/* sets up driver state, does not configure hw */
	int (*sw_init)(void *handle);
	/* tears down driver state, does not configure hw */
	int (*sw_fini)(void *handle);
	/* sets up the hw state */
	int (*hw_init)(void *handle);
	/* tears down the hw state */
	int (*hw_fini)(void *handle);
	/* handles IP specific hw/sw changes for suspend */
	int (*suspend)(void *handle);
	/* handles IP specific hw/sw changes for resume */
	int (*resume)(void *handle);
	/* returns current IP block idle status */
	bool (*is_idle)(void *handle);
	/* poll for idle */
	int (*wait_for_idle)(void *handle);
	/* soft reset the IP block */
	int (*soft_reset)(void *handle);
	/* dump the IP block status registers */
	void (*print_status)(void *handle);
	/* enable/disable cg for the IP block */
	int (*set_clockgating_state)(void *handle,
				     enum amd_clockgating_state state);
	/* enable/disable pg for the IP block */
	int (*set_powergating_state)(void *handle,
				     enum amd_powergating_state state);
};

#endif /* __AMD_SHARED_H__ */
