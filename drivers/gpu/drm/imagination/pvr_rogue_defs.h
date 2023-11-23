/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_ROGUE_DEFS_H
#define PVR_ROGUE_DEFS_H

#include "pvr_rogue_cr_defs.h"

#include <linux/bits.h>

/*
 ******************************************************************************
 * ROGUE Defines
 ******************************************************************************
 */

#define ROGUE_FW_MAX_NUM_OS (8U)
#define ROGUE_FW_HOST_OS (0U)
#define ROGUE_FW_GUEST_OSID_START (1U)

#define ROGUE_FW_THREAD_0 (0U)
#define ROGUE_FW_THREAD_1 (1U)

#define GET_ROGUE_CACHE_LINE_SIZE(x) ((((s32)(x)) > 0) ? ((x) / 8) : (0))

#define MAX_HW_GEOM_FRAG_CONTEXTS 2U

#define ROGUE_CR_CLK_CTRL_ALL_ON \
	(0x5555555555555555ull & ROGUE_CR_CLK_CTRL_MASKFULL)
#define ROGUE_CR_CLK_CTRL_ALL_AUTO \
	(0xaaaaaaaaaaaaaaaaull & ROGUE_CR_CLK_CTRL_MASKFULL)
#define ROGUE_CR_CLK_CTRL2_ALL_ON \
	(0x5555555555555555ull & ROGUE_CR_CLK_CTRL2_MASKFULL)
#define ROGUE_CR_CLK_CTRL2_ALL_AUTO \
	(0xaaaaaaaaaaaaaaaaull & ROGUE_CR_CLK_CTRL2_MASKFULL)

#define ROGUE_CR_SOFT_RESET_DUST_n_CORE_EN    \
	(ROGUE_CR_SOFT_RESET_DUST_A_CORE_EN | \
	 ROGUE_CR_SOFT_RESET_DUST_B_CORE_EN | \
	 ROGUE_CR_SOFT_RESET_DUST_C_CORE_EN | \
	 ROGUE_CR_SOFT_RESET_DUST_D_CORE_EN | \
	 ROGUE_CR_SOFT_RESET_DUST_E_CORE_EN | \
	 ROGUE_CR_SOFT_RESET_DUST_F_CORE_EN | \
	 ROGUE_CR_SOFT_RESET_DUST_G_CORE_EN | \
	 ROGUE_CR_SOFT_RESET_DUST_H_CORE_EN)

/* SOFT_RESET Rascal and DUSTs bits */
#define ROGUE_CR_SOFT_RESET_RASCALDUSTS_EN    \
	(ROGUE_CR_SOFT_RESET_RASCAL_CORE_EN | \
	 ROGUE_CR_SOFT_RESET_DUST_n_CORE_EN)

/* SOFT_RESET steps as defined in the TRM */
#define ROGUE_S7_SOFT_RESET_DUSTS (ROGUE_CR_SOFT_RESET_DUST_n_CORE_EN)

#define ROGUE_S7_SOFT_RESET_JONES                                 \
	(ROGUE_CR_SOFT_RESET_PM_EN | ROGUE_CR_SOFT_RESET_VDM_EN | \
	 ROGUE_CR_SOFT_RESET_ISP_EN)

#define ROGUE_S7_SOFT_RESET_JONES_ALL                             \
	(ROGUE_S7_SOFT_RESET_JONES | ROGUE_CR_SOFT_RESET_BIF_EN | \
	 ROGUE_CR_SOFT_RESET_SLC_EN | ROGUE_CR_SOFT_RESET_GARTEN_EN)

#define ROGUE_S7_SOFT_RESET2                                                  \
	(ROGUE_CR_SOFT_RESET2_BLACKPEARL_EN | ROGUE_CR_SOFT_RESET2_PIXEL_EN | \
	 ROGUE_CR_SOFT_RESET2_CDM_EN | ROGUE_CR_SOFT_RESET2_VERTEX_EN)

#define ROGUE_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT (12U)
#define ROGUE_BIF_PM_PHYSICAL_PAGE_SIZE \
	BIT(ROGUE_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT)

#define ROGUE_BIF_PM_VIRTUAL_PAGE_ALIGNSHIFT (14U)
#define ROGUE_BIF_PM_VIRTUAL_PAGE_SIZE BIT(ROGUE_BIF_PM_VIRTUAL_PAGE_ALIGNSHIFT)

#define ROGUE_BIF_PM_FREELIST_BASE_ADDR_ALIGNSIZE (16U)

/*
 * To get the number of required Dusts, divide the number of
 * clusters by 2 and round up
 */
#define ROGUE_REQ_NUM_DUSTS(CLUSTERS) (((CLUSTERS) + 1U) / 2U)

/*
 * To get the number of required Bernado/Phantom(s), divide
 * the number of clusters by 4 and round up
 */
#define ROGUE_REQ_NUM_PHANTOMS(CLUSTERS) (((CLUSTERS) + 3U) / 4U)
#define ROGUE_REQ_NUM_BERNADOS(CLUSTERS) (((CLUSTERS) + 3U) / 4U)
#define ROGUE_REQ_NUM_BLACKPEARLS(CLUSTERS) (((CLUSTERS) + 3U) / 4U)

/*
 * FW MMU contexts
 */
#define MMU_CONTEXT_MAPPING_FWPRIV (0x0) /* FW code/private data */
#define MMU_CONTEXT_MAPPING_FWIF (0x0) /* Host/FW data */

/*
 * Utility macros to calculate CAT_BASE register addresses
 */
#define BIF_CAT_BASEX(n)          \
	(ROGUE_CR_BIF_CAT_BASE0 + \
	 (n) * (ROGUE_CR_BIF_CAT_BASE1 - ROGUE_CR_BIF_CAT_BASE0))

#define FWCORE_MEM_CAT_BASEX(n)                 \
	(ROGUE_CR_FWCORE_MEM_CAT_BASE0 +        \
	 (n) * (ROGUE_CR_FWCORE_MEM_CAT_BASE1 - \
		ROGUE_CR_FWCORE_MEM_CAT_BASE0))

/*
 * FWCORE wrapper register defines
 */
#define FWCORE_ADDR_REMAP_CONFIG0_MMU_CONTEXT_SHIFT \
	ROGUE_CR_FWCORE_ADDR_REMAP_CONFIG0_CBASE_SHIFT
#define FWCORE_ADDR_REMAP_CONFIG0_MMU_CONTEXT_CLRMSK \
	ROGUE_CR_FWCORE_ADDR_REMAP_CONFIG0_CBASE_CLRMSK
#define FWCORE_ADDR_REMAP_CONFIG0_SIZE_ALIGNSHIFT (12U)

#define ROGUE_MAX_COMPUTE_SHARED_REGISTERS (2 * 1024)
#define ROGUE_MAX_VERTEX_SHARED_REGISTERS 1024
#define ROGUE_MAX_PIXEL_SHARED_REGISTERS 1024
#define ROGUE_CSRM_LINE_SIZE_IN_DWORDS (64 * 4 * 4)

#define ROGUE_CDMCTRL_USC_COMMON_SIZE_ALIGNSIZE 64
#define ROGUE_CDMCTRL_USC_COMMON_SIZE_UPPER 256

/*
 * The maximum amount of local memory which can be allocated by a single kernel
 * (in dwords/32-bit registers).
 *
 * ROGUE_CDMCTRL_USC_COMMON_SIZE_ALIGNSIZE is in bytes so we divide by four.
 */
#define ROGUE_MAX_PER_KERNEL_LOCAL_MEM_SIZE_REGS ((ROGUE_CDMCTRL_USC_COMMON_SIZE_ALIGNSIZE * \
						   ROGUE_CDMCTRL_USC_COMMON_SIZE_UPPER) >> 2)

/*
 ******************************************************************************
 * WA HWBRNs
 ******************************************************************************
 */

/* GPU CR timer tick in GPU cycles */
#define ROGUE_CRTIME_TICK_IN_CYCLES (256U)

/* for nohw multicore return max cores possible to client */
#define ROGUE_MULTICORE_MAX_NOHW_CORES (4U)

/*
 * If the size of the SLC is less than this value then the TPU bypasses the SLC.
 */
#define ROGUE_TPU_CACHED_SLC_SIZE_THRESHOLD (128U * 1024U)

/*
 * If the size of the SLC is bigger than this value then the TCU must not be
 * bypassed in the SLC.
 * In XE_MEMORY_HIERARCHY cores, the TCU is bypassed by default.
 */
#define ROGUE_TCU_CACHED_SLC_SIZE_THRESHOLD (32U * 1024U)

/*
 * Register used by the FW to track the current boot stage (not used in MIPS)
 */
#define ROGUE_FW_BOOT_STAGE_REGISTER (ROGUE_CR_POWER_ESTIMATE_RESULT)

/*
 * Virtualisation definitions
 */
#define ROGUE_VIRTUALISATION_REG_SIZE_PER_OS \
	(ROGUE_CR_MTS_SCHEDULE1 - ROGUE_CR_MTS_SCHEDULE)

/*
 * Macro used to indicate which version of HWPerf is active
 */
#define ROGUE_FEATURE_HWPERF_ROGUE

/*
 * Maximum number of cores supported by TRP
 */
#define ROGUE_TRP_MAX_NUM_CORES (4U)

#endif /* PVR_ROGUE_DEFS_H */
