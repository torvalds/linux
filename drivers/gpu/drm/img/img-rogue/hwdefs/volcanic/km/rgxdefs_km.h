/*************************************************************************/ /*!
@Title          Rogue hw definitions (kernel mode)
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#ifndef RGXDEFS_KM_H
#define RGXDEFS_KM_H

#if defined(RGX_BVNC_CORE_KM_HEADER) && defined(RGX_BNC_CONFIG_KM_HEADER)
#include RGX_BVNC_CORE_KM_HEADER
#include RGX_BNC_CONFIG_KM_HEADER
#endif

#define IMG_EXPLICIT_INCLUDE_HWDEFS
#if defined(__KERNEL__)
#include "rgx_cr_defs_km.h"
#endif
#undef IMG_EXPLICIT_INCLUDE_HWDEFS

#include "rgx_heap_firmware.h"

/* The following Macros are picked up through BVNC headers for no hardware
 * operations to be compatible with old build infrastructure.
 */
#if defined(NO_HARDWARE)
/******************************************************************************
 * Check for valid B.X.N.C
 *****************************************************************************/
#if !defined(RGX_BVNC_KM_B) || !defined(RGX_BVNC_KM_V) || !defined(RGX_BVNC_KM_N) || !defined(RGX_BVNC_KM_C)
#error "Need to specify BVNC (RGX_BVNC_KM_B, RGX_BVNC_KM_V, RGX_BVNC_KM_N and RGX_BVNC_C)"
#endif

/* Check core/config compatibility */
#if (RGX_BVNC_KM_B != RGX_BNC_KM_B) || (RGX_BVNC_KM_N != RGX_BNC_KM_N) || (RGX_BVNC_KM_C != RGX_BNC_KM_C)
#error "BVNC headers are mismatching (KM core/config)"
#endif
#endif

/******************************************************************************
 * RGX Version name
 *****************************************************************************/
#define RGX_BVNC_KM_ST2(S)	#S
#define RGX_BVNC_KM_ST(S)	RGX_BVNC_KM_ST2(S)
#define RGX_BVNC_KM			RGX_BVNC_KM_ST(RGX_BVNC_KM_B) "." RGX_BVNC_KM_ST(RGX_BVNC_KM_V) "." RGX_BVNC_KM_ST(RGX_BVNC_KM_N) "." RGX_BVNC_KM_ST(RGX_BVNC_KM_C)
#define RGX_BVNC_KM_V_ST	RGX_BVNC_KM_ST(RGX_BVNC_KM_V)

/* Maximum string size is [bb.vvvp.nnnn.cccc\0], includes null char */
#define RGX_BVNC_STR_SIZE_MAX (2+1+4+1+4+1+4+1)
#define RGX_BVNC_STR_FMTSPEC  "%u.%u.%u.%u"
#define RGX_BVNC_STRP_FMTSPEC "%u.%up.%u.%u"


/******************************************************************************
 * RGX Defines
 *****************************************************************************/

#define BVNC_FIELD_MASK     ((1 << BVNC_FIELD_WIDTH) - 1)
#define C_POSITION          (0)
#define N_POSITION          ((C_POSITION) + (BVNC_FIELD_WIDTH))
#define V_POSITION          ((N_POSITION) + (BVNC_FIELD_WIDTH))
#define B_POSITION          ((V_POSITION) + (BVNC_FIELD_WIDTH))

#define B_POSTION_MASK      (((IMG_UINT64)(BVNC_FIELD_MASK) << (B_POSITION)))
#define V_POSTION_MASK      (((IMG_UINT64)(BVNC_FIELD_MASK) << (V_POSITION)))
#define N_POSTION_MASK      (((IMG_UINT64)(BVNC_FIELD_MASK) << (N_POSITION)))
#define C_POSTION_MASK      (((IMG_UINT64)(BVNC_FIELD_MASK) << (C_POSITION)))

#define GET_B(x)            (((x) & (B_POSTION_MASK)) >> (B_POSITION))
#define GET_V(x)            (((x) & (V_POSTION_MASK)) >> (V_POSITION))
#define GET_N(x)            (((x) & (N_POSTION_MASK)) >> (N_POSITION))
#define GET_C(x)            (((x) & (C_POSTION_MASK)) >> (C_POSITION))

#define BVNC_PACK(B,V,N,C)  ((((IMG_UINT64)B)) << (B_POSITION) | \
                             (((IMG_UINT64)V)) << (V_POSITION) | \
                             (((IMG_UINT64)N)) << (N_POSITION) | \
                             (((IMG_UINT64)C)) << (C_POSITION) \
                            )

#define RGX_CR_CORE_ID_CONFIG_N_SHIFT    (8U)
#define RGX_CR_CORE_ID_CONFIG_C_SHIFT    (0U)

#define RGX_CR_CORE_ID_CONFIG_N_CLRMSK   (0XFFFF00FFU)
#define RGX_CR_CORE_ID_CONFIG_C_CLRMSK   (0XFFFFFF00U)

/* The default number of OSID is 1, higher number implies VZ enabled firmware */
#if !defined(RGXFW_NATIVE) && defined(RGX_NUM_DRIVERS_SUPPORTED) && (RGX_NUM_DRIVERS_SUPPORTED + 1U > 1U)
#define RGXFW_NUM_OS RGX_NUM_DRIVERS_SUPPORTED
#else
#define RGXFW_NUM_OS 1U
#endif

#if defined(RGX_FEATURE_NUM_OSIDS)
#define RGXFW_MAX_NUM_OSIDS                               (RGX_FEATURE_NUM_OSIDS)
#else
#define RGXFW_MAX_NUM_OSIDS                               (8U)
#endif

#define RGXFW_HOST_DRIVER_ID                              (0U)
#define RGXFW_GUEST_DRIVER_ID_START                       (RGXFW_HOST_DRIVER_ID + 1U)

#define RGXFW_THREAD_0                                    (0U)
#define RGXFW_THREAD_1                                    (1U)

/* META cores (required for the RGX_FEATURE_META) */
#define MTP218   (1)
#define MTP219   (2)
#define LTP218   (3)
#define LTP217   (4)

/* META Core memory feature depending on META variants */
#define RGX_META_COREMEM_32K      (32*1024)
#define RGX_META_COREMEM_48K      (48*1024)
#define RGX_META_COREMEM_64K      (64*1024)
#define RGX_META_COREMEM_96K      (96*1024)
#define RGX_META_COREMEM_128K     (128*1024)
#define RGX_META_COREMEM_256K     (256*1024)

#if !defined(__KERNEL__)
#if defined(SUPPORT_TRUSTED_DEVICE) && !defined(RGX_FEATURE_META_DMA)
#undef SUPPORT_META_COREMEM
#undef RGX_FEATURE_META_COREMEM_SIZE
#define RGX_FEATURE_META_COREMEM_SIZE (0)
#define RGX_META_COREMEM_SIZE         (0)
#elif defined(RGX_FEATURE_META_COREMEM_SIZE)
#define RGX_META_COREMEM_SIZE         (RGX_FEATURE_META_COREMEM_SIZE*1024U)
#else
#define RGX_META_COREMEM_SIZE         (0)
#endif

#if RGX_META_COREMEM_SIZE != 0
#define RGX_META_COREMEM
#define RGX_META_COREMEM_CODE
#define RGX_META_COREMEM_DATA
#endif
#endif

#define GET_ROGUE_CACHE_LINE_SIZE(x)    ((((IMG_INT32)x) > 0) ? ((x)/8) : (0))

#if defined(RGX_FEATURE_META_DMA)
#define RGX_META_DMA_BLOCK_SIZE (32U)
#else
#define RGX_META_DMA_BLOCK_SIZE (0U)
#endif

#if defined(SUPPORT_AGP)
#if defined(SUPPORT_AGP4)
#define MAX_HW_TA3DCONTEXTS	5U
#else
#define MAX_HW_TA3DCONTEXTS	3U
#endif
#else
#define MAX_HW_TA3DCONTEXTS	2U
#endif

#define RGX_CR_CLK_CTRL0_ALL_ON   (IMG_UINT64_C(0x5555555555555555)&RGX_CR_CLK_CTRL0_MASKFULL)
#define RGX_CR_CLK_CTRL0_ALL_AUTO (IMG_UINT64_C(0xaaaaaaaaaaaaaaaa)&RGX_CR_CLK_CTRL0_MASKFULL)
#define RGX_CR_CLK_CTRL1_ALL_ON   (IMG_UINT64_C(0x5555555555555555)&RGX_CR_CLK_CTRL1_MASKFULL)
#define RGX_CR_CLK_CTRL1_ALL_AUTO (IMG_UINT64_C(0xaaaaaaaaaaaaaaaa)&RGX_CR_CLK_CTRL1_MASKFULL)
#define RGX_CR_CLK_CTRL2_ALL_ON   (IMG_UINT64_C(0x5555555555555555)&RGX_CR_CLK_CTRL2_MASKFULL)
#define RGX_CR_CLK_CTRL2_ALL_AUTO (IMG_UINT64_C(0xaaaaaaaaaaaaaaaa)&RGX_CR_CLK_CTRL2_MASKFULL)

#define RGX_CR_MERCER0_SOFT_RESET_SPU_EN (RGX_CR_MERCER_SOFT_RESET_SPU0_MERCER0_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU1_MERCER0_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU2_MERCER0_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU3_MERCER0_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU4_MERCER0_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU5_MERCER0_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU6_MERCER0_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU7_MERCER0_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU8_MERCER0_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU9_MERCER0_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU10_MERCER0_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU11_MERCER0_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU12_MERCER0_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU13_MERCER0_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU14_MERCER0_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU15_MERCER0_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU16_MERCER0_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU17_MERCER0_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU18_MERCER0_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU19_MERCER0_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU20_MERCER0_EN)

#define RGX_CR_MERCER1_SOFT_RESET_SPU_EN (RGX_CR_MERCER_SOFT_RESET_SPU0_MERCER1_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU1_MERCER1_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU2_MERCER1_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU3_MERCER1_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU4_MERCER1_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU5_MERCER1_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU6_MERCER1_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU7_MERCER1_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU8_MERCER1_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU9_MERCER1_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU10_MERCER1_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU11_MERCER1_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU12_MERCER1_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU13_MERCER1_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU14_MERCER1_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU15_MERCER1_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU16_MERCER1_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU17_MERCER1_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU18_MERCER1_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU19_MERCER1_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU20_MERCER1_EN)

#define RGX_CR_MERCER2_SOFT_RESET_SPU_EN (RGX_CR_MERCER_SOFT_RESET_SPU0_MERCER2_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU1_MERCER2_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU2_MERCER2_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU3_MERCER2_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU4_MERCER2_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU5_MERCER2_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU6_MERCER2_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU7_MERCER2_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU8_MERCER2_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU9_MERCER2_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU10_MERCER2_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU11_MERCER2_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU12_MERCER2_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU13_MERCER2_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU14_MERCER2_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU15_MERCER2_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU16_MERCER2_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU17_MERCER2_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU18_MERCER2_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU19_MERCER2_EN | \
										  RGX_CR_MERCER_SOFT_RESET_SPU20_MERCER2_EN)


/* SOFT_RESET steps as defined in the TRM */
#define RGX_SOFT_RESET_JONES             (RGX_CR_SOFT_RESET_PM_EN | \
                                          RGX_CR_SOFT_RESET_ISP_EN)
#define RGX_SOFT_RESET_JONES_ALL         (RGX_SOFT_RESET_JONES | \
                                          RGX_CR_SOFT_RESET_BIF_TEXAS_EN | \
                                          RGX_CR_SOFT_RESET_BIF_JONES_EN | \
										  RGX_CR_SOFT_RESET_SLC_EN | \
                                          RGX_CR_SOFT_RESET_GARTEN_EN)
#define RGX_SOFT_RESET_EXTRA             (RGX_CR_SOFT_RESET_PIXEL_EN | \
                                          RGX_CR_SOFT_RESET_VERTEX_EN |  \
                                          RGX_CR_SOFT_RESET_GEO_VERTEX_EN | \
                                          RGX_CR_SOFT_RESET_GEO_SHARED_EN | \
                                          RGX_CR_SOFT_RESET_COMPUTE_EN | \
                                          RGX_CR_SOFT_RESET_TDM_EN)
#define RGX_SOFT_RESET_FROM_WITHIN_CORE  (RGX_CR_SOFT_RESET_MASKFULL ^ \
                                          (RGX_CR_SOFT_RESET_GARTEN_EN | \
                                           RGX_CR_SOFT_RESET_BIF_JONES_EN | \
                                           RGX_CR_SOFT_RESET_SLC_EN))


#define RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT		(12U)
#define RGX_BIF_PM_PHYSICAL_PAGE_SIZE			(1U << RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT)

#define RGX_BIF_PM_VIRTUAL_PAGE_ALIGNSHIFT		(14U)
#define RGX_BIF_PM_VIRTUAL_PAGE_SIZE			(1U << RGX_BIF_PM_VIRTUAL_PAGE_ALIGNSHIFT)

#define RGX_BIF_PM_FREELIST_BASE_ADDR_ALIGNSIZE	(32U)

/* To get the number of required Bernado/Phantom(s), divide
 * the number of clusters by 4 and round up
 */
#define RGX_REQ_NUM_PHANTOMS(CLUSTERS) ((CLUSTERS + 3U) / 4U)
#define RGX_REQ_NUM_BERNADOS(CLUSTERS) ((CLUSTERS + 3U) / 4U)
#define RGX_REQ_NUM_BLACKPEARLS(CLUSTERS) ((CLUSTERS + 3U) / 4U)

#if !defined(__KERNEL__)
# define RGX_NUM_PHANTOMS (RGX_REQ_NUM_PHANTOMS(RGX_FEATURE_NUM_CLUSTERS))
#endif

/* for nohw multicore, true max number of cores returned to client */
#define RGX_MULTICORE_MAX_NOHW_CORES                (8U)

/*
 * META second thread feature depending on META variants and
 * available CoreMem
 */
#if defined(RGX_FEATURE_META) && (RGX_FEATURE_META == MTP218 || RGX_FEATURE_META == MTP219) && (RGX_FEATURE_META_COREMEM_SIZE >= 96)
#define RGXFW_META_SUPPORT_2ND_THREAD
#endif

/*
 * FWCORE wrapper register defines
 */
#define FWCORE_ADDR_REMAP_CONFIG0_MMU_CONTEXT_SHIFT   RGX_CR_FWCORE_ADDR_REMAP_CONFIG0_MMU_CONTEXT_SHIFT
#define FWCORE_ADDR_REMAP_CONFIG0_MMU_CONTEXT_CLRMSK  RGX_CR_FWCORE_ADDR_REMAP_CONFIG0_MMU_CONTEXT_CLRMSK
#define FWCORE_ADDR_REMAP_CONFIG0_SIZE_ALIGNSHIFT     (12U)


/*
 * FBC clear color register defaults based on HW defaults
 * non-YUV clear colour 0: 0x00000000 (encoded as ch3,2,1,0)
 * non-YUV clear colour 1: 0x01000000 (encoded as ch3,2,1,0)
 * YUV clear colour 0: 0x000 000 (encoded as UV Y)
 * YUV clear colour 1: 0x000 3FF (encoded as UV Y)
 */
#define RGX_FBC_CC_DEFAULT (0x0100000000000000)
#define RGX_FBC_CC_YUV_DEFAULT (0x000003FF00000000)

/*
 * Virtualisation definitions
 */

#define RGX_VIRTUALISATION_REG_SIZE_PER_OS (RGX_CR_MTS_SCHEDULE1 - RGX_CR_MTS_SCHEDULE)

/*
 * Renaming MTS sideband bitfields to emphasize that the Register Bank number
 * of the MTS register used identifies a specific Driver/VM rather than the OSID tag
 * emitted on bus memory transactions.
 */
#define RGX_MTS_SBDATA_DRIVERID_CLRMSK RGX_CR_MTS_BGCTX_SBDATA0_OS_ID_CLRMSK
#define RGX_MTS_SBDATA_DRIVERID_SHIFT RGX_CR_MTS_BGCTX_SBDATA0_OS_ID_SHIFT

/* Register Bank containing registers secured against host access */
#define RGX_HOST_SECURE_REGBANK_OFFSET				(0xF0000U)
#define RGX_HOST_SECURE_REGBANK_SIZE				(0x10000U)

/* GPU CR timer tick in GPU cycles */
#define RGX_CRTIME_TICK_IN_CYCLES (256U)

#if defined(FIX_HW_BRN_71840)
#define ROGUE_RENDERSIZE_MAXX						(16384U)
#define ROGUE_RENDERSIZE_MAXY						(16384U)
#else
#define ROGUE_RENDERSIZE_MAXX						(RGX_FEATURE_RENDER_TARGET_XY_MAX)
#define ROGUE_RENDERSIZE_MAXY						(RGX_FEATURE_RENDER_TARGET_XY_MAX)
#endif

/*
 * Register used by the FW to track the current boot stage (not used in MIPS)
 */
#define RGX_FW_BOOT_STAGE_REGISTER     (RGX_CR_SCRATCH14)

/*
 * Define used to determine whether or not SLC range-based flush/invalidate
 * interface is supported.
 */
#define RGX_SRV_SLC_RANGEBASED_CFI_SUPPORTED 1

/*
 * Macro used to indicate which version of HWPerf is active
 */
#define RGX_FEATURE_HWPERF_VOLCANIC

/*
 * Maximum number of cores supported by TRP
 */
#define RGX_TRP_MAX_NUM_CORES                           (8U)

/*
 * Maximum number of cores supported by WGP
 */
#define RGX_WGP_MAX_NUM_CORES                           (8U)

/*
 * Supports command to invalidate FBCDC descriptor state cache
 */
#define RGX_FBSC_INVALIDATE_COMMAND_SUPPORTED 1

#if defined(FIX_HW_BRN_71422)
/*
 * The BRN71422 software workaround requires a target physical address on
 * the hardware platform with a low latency response time and which will
 * not suffer from delays of DRAM hardware operations such as refresh and
 * recalibration. Only with that address defined will the workaround be used.
 */
#if !defined(PDUMP)
//#define RGX_BRN71422_TARGET_HARDWARE_PHYSICAL_ADDR  (IMG_UINT64_C(0x0000000000))
#endif
#define RGX_BRN71422_WORKAROUND_READ_SIZE           (32U)
#endif

#endif /* RGXDEFS_KM_H */
