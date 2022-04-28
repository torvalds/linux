/*************************************************************************/ /*!
@File
@Title          PVR Bridge Functionality
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the PVR Bridge code
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

#ifndef PVR_BRIDGE_H
#define PVR_BRIDGE_H

#if defined(__cplusplus)
extern "C" {
#endif

#include "pvrsrv_error.h"
#if defined(SUPPORT_DISPLAY_CLASS)
#include "common_dc_bridge.h"
#if defined(SUPPORT_DCPLAT_BRIDGE)
#include "common_dcplat_bridge.h"
#endif
#endif
#include "common_mm_bridge.h"
#if defined(SUPPORT_MMPLAT_BRIDGE)
#include "common_mmplat_bridge.h"
#endif
#if defined(SUPPORT_WRAP_EXTMEM)
#include "common_mmextmem_bridge.h"
#endif
#if !defined(EXCLUDE_CMM_BRIDGE)
#include "common_cmm_bridge.h"
#endif
#if defined(__linux__)
#include "common_dmabuf_bridge.h"
#endif
#if defined(PDUMP)
#include "common_pdump_bridge.h"
#include "common_pdumpctrl_bridge.h"
#include "common_pdumpmm_bridge.h"
#endif
#include "common_cache_bridge.h"
#if defined(SUPPORT_DMA_TRANSFER)
#include "common_dma_bridge.h"
#endif
#include "common_srvcore_bridge.h"
#include "common_sync_bridge.h"
#if defined(SUPPORT_SECURE_EXPORT)
#include "common_smm_bridge.h"
#endif
#if !defined(EXCLUDE_HTBUFFER_BRIDGE)
#include "common_htbuffer_bridge.h"
#endif
#include "common_pvrtl_bridge.h"
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
#include "common_ri_bridge.h"
#endif

#if defined(SUPPORT_VALIDATION_BRIDGE)
#include "common_validation_bridge.h"
#endif

#if defined(PVR_TESTING_UTILS)
#include "common_tutils_bridge.h"
#endif

#include "common_devicememhistory_bridge.h"
#include "common_synctracking_bridge.h"

#if defined(SUPPORT_FALLBACK_FENCE_SYNC)
#include "common_syncfallback_bridge.h"
#endif

#if defined(SUPPORT_DI_BRG_IMPL)
#include "common_di_bridge.h"
#endif

/*
 * Bridge Cmd Ids
 */


/* Note: The pattern
 *   #define PVRSRV_BRIDGE_FEATURE (PVRSRV_BRIDGE_PREVFEATURE + 1)
 *   #if defined(SUPPORT_FEATURE)
 *   #define PVRSRV_BRIDGE_FEATURE_DISPATCH_FIRST (PVRSRV_BRIDGE_PREVFEATURE_DISPATCH_LAST + 1)
 *   #define PVRSRV_BRIDGE_FEATURE_DISPATCH_LAST  (PVRSRV_BRIDGE_FEATURE_DISPATCH_FIRST + PVRSRV_BRIDGE_FEATURE_CMD_LAST)
 *   #else
 *   #define PVRSRV_BRIDGE_FEATURE_DISPATCH_FIRST 0
 *   #define PVRSRV_BRIDGE_FEATURE_DISPATCH_LAST  (PVRSRV_BRIDGE_PREVFEATURE_DISPATCH_LAST)
 *   #endif
 * is used in the macro definitions below to make PVRSRV_BRIDGE_FEATURE_*
 * take up no space in the dispatch table if SUPPORT_FEATURE is disabled.
 *
 * Note however that a bridge always defines PVRSRV_BRIDGE_FEATURE, even where
 * the feature is not enabled (each bridge group retains its own ioctl number).
 */

#define PVRSRV_BRIDGE_FIRST					0UL

/*   0: Default handler */
#define PVRSRV_BRIDGE_DEFAULT				0UL
#define PVRSRV_BRIDGE_DEFAULT_DISPATCH_FIRST 0UL
#define PVRSRV_BRIDGE_DEFAULT_DISPATCH_LAST  (PVRSRV_BRIDGE_DEFAULT_DISPATCH_FIRST)
/*   1: CORE functions */
#define PVRSRV_BRIDGE_SRVCORE				1UL
#define PVRSRV_BRIDGE_SRVCORE_DISPATCH_FIRST (PVRSRV_BRIDGE_DEFAULT_DISPATCH_LAST+1)
#define PVRSRV_BRIDGE_SRVCORE_DISPATCH_LAST  (PVRSRV_BRIDGE_SRVCORE_DISPATCH_FIRST + PVRSRV_BRIDGE_SRVCORE_CMD_LAST)

/*   2: SYNC functions */
#define PVRSRV_BRIDGE_SYNC					2UL
#define PVRSRV_BRIDGE_SYNC_DISPATCH_FIRST (PVRSRV_BRIDGE_SRVCORE_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_SYNC_DISPATCH_LAST  (PVRSRV_BRIDGE_SYNC_DISPATCH_FIRST + PVRSRV_BRIDGE_SYNC_CMD_LAST)

/* 3,4: Reserved */
#define PVRSRV_BRIDGE_RESERVED1				3UL
#define PVRSRV_BRIDGE_RESERVED1_DISPATCH_FIRST 0
#define PVRSRV_BRIDGE_RESERVED1_DISPATCH_LAST (PVRSRV_BRIDGE_SYNC_DISPATCH_LAST)

#define PVRSRV_BRIDGE_RESERVED2				4UL
#define PVRSRV_BRIDGE_RESERVED2_DISPATCH_FIRST 0
#define PVRSRV_BRIDGE_RESERVED2_DISPATCH_LAST  (PVRSRV_BRIDGE_RESERVED1_DISPATCH_LAST)

/*   5: PDUMP CTRL layer functions */
#define PVRSRV_BRIDGE_PDUMPCTRL				5UL
#if defined(PDUMP)
#define PVRSRV_BRIDGE_PDUMPCTRL_DISPATCH_FIRST (PVRSRV_BRIDGE_SYNC_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_PDUMPCTRL_DISPATCH_LAST  (PVRSRV_BRIDGE_PDUMPCTRL_DISPATCH_FIRST + PVRSRV_BRIDGE_PDUMPCTRL_CMD_LAST)
#else
#define PVRSRV_BRIDGE_PDUMPCTRL_DISPATCH_FIRST 0
#define PVRSRV_BRIDGE_PDUMPCTRL_DISPATCH_LAST  (PVRSRV_BRIDGE_SYNC_DISPATCH_LAST)
#endif

/*   6: Memory Management functions */
#define PVRSRV_BRIDGE_MM					6UL
#define PVRSRV_BRIDGE_MM_DISPATCH_FIRST (PVRSRV_BRIDGE_PDUMPCTRL_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_MM_DISPATCH_LAST  (PVRSRV_BRIDGE_MM_DISPATCH_FIRST + PVRSRV_BRIDGE_MM_CMD_LAST)

/*   7: Non-Linux Memory Management functions */
#define PVRSRV_BRIDGE_MMPLAT				7UL
#if defined(SUPPORT_MMPLAT_BRIDGE)
#define PVRSRV_BRIDGE_MMPLAT_DISPATCH_FIRST (PVRSRV_BRIDGE_MM_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_MMPLAT_DISPATCH_LAST  (PVRSRV_BRIDGE_MMPLAT_DISPATCH_FIRST + PVRSRV_BRIDGE_MMPLAT_CMD_LAST)
#else
#define PVRSRV_BRIDGE_MMPLAT_DISPATCH_FIRST 0
#define PVRSRV_BRIDGE_MMPLAT_DISPATCH_LAST  (PVRSRV_BRIDGE_MM_DISPATCH_LAST)
#endif

/*   8: Context Memory Management functions */
#define PVRSRV_BRIDGE_CMM					8UL
#if !defined(EXCLUDE_CMM_BRIDGE)
#define PVRSRV_BRIDGE_CMM_DISPATCH_FIRST (PVRSRV_BRIDGE_MMPLAT_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_CMM_DISPATCH_LAST  (PVRSRV_BRIDGE_CMM_DISPATCH_FIRST + PVRSRV_BRIDGE_CMM_CMD_LAST)
#else
#define PVRSRV_BRIDGE_CMM_DISPATCH_FIRST 0
#define PVRSRV_BRIDGE_CMM_DISPATCH_LAST  (PVRSRV_BRIDGE_MMPLAT_DISPATCH_LAST)
#endif

/*   9: PDUMP Memory Management functions */
#define PVRSRV_BRIDGE_PDUMPMM				9UL
#if defined(PDUMP)
#define PVRSRV_BRIDGE_PDUMPMM_DISPATCH_FIRST (PVRSRV_BRIDGE_CMM_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_PDUMPMM_DISPATCH_LAST  (PVRSRV_BRIDGE_PDUMPMM_DISPATCH_FIRST + PVRSRV_BRIDGE_PDUMPMM_CMD_LAST)
#else
#define PVRSRV_BRIDGE_PDUMPMM_DISPATCH_FIRST 0
#define PVRSRV_BRIDGE_PDUMPMM_DISPATCH_LAST  (PVRSRV_BRIDGE_CMM_DISPATCH_LAST)
#endif

/*   10: PDUMP functions */
#define PVRSRV_BRIDGE_PDUMP					10UL
#if defined(PDUMP)
#define PVRSRV_BRIDGE_PDUMP_DISPATCH_FIRST (PVRSRV_BRIDGE_PDUMPMM_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_PDUMP_DISPATCH_LAST  (PVRSRV_BRIDGE_PDUMP_DISPATCH_FIRST + PVRSRV_BRIDGE_PDUMP_CMD_LAST)
#else
#define PVRSRV_BRIDGE_PDUMP_DISPATCH_FIRST 0
#define PVRSRV_BRIDGE_PDUMP_DISPATCH_LAST  (PVRSRV_BRIDGE_PDUMPMM_DISPATCH_LAST)
#endif

/*  11: DMABUF functions */
#define PVRSRV_BRIDGE_DMABUF				11UL
#if defined(__linux__)
#define PVRSRV_BRIDGE_DMABUF_DISPATCH_FIRST (PVRSRV_BRIDGE_PDUMP_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_DMABUF_DISPATCH_LAST  (PVRSRV_BRIDGE_DMABUF_DISPATCH_FIRST + PVRSRV_BRIDGE_DMABUF_CMD_LAST)
#else
#define PVRSRV_BRIDGE_DMABUF_DISPATCH_FIRST 0
#define PVRSRV_BRIDGE_DMABUF_DISPATCH_LAST  (PVRSRV_BRIDGE_PDUMP_DISPATCH_LAST)
#endif

/*  12: Display Class functions */
#define PVRSRV_BRIDGE_DC					12UL
#if defined(SUPPORT_DISPLAY_CLASS)
#define PVRSRV_BRIDGE_DC_DISPATCH_FIRST (PVRSRV_BRIDGE_DMABUF_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_DC_DISPATCH_LAST  (PVRSRV_BRIDGE_DC_DISPATCH_FIRST + PVRSRV_BRIDGE_DC_CMD_LAST)
#else
#define PVRSRV_BRIDGE_DC_DISPATCH_FIRST 0
#define PVRSRV_BRIDGE_DC_DISPATCH_LAST  (PVRSRV_BRIDGE_DMABUF_DISPATCH_LAST)
#endif

/*  13: Cache interface functions */
#define PVRSRV_BRIDGE_CACHE					13UL
#define PVRSRV_BRIDGE_CACHE_DISPATCH_FIRST (PVRSRV_BRIDGE_DC_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_CACHE_DISPATCH_LAST  (PVRSRV_BRIDGE_CACHE_DISPATCH_FIRST + PVRSRV_BRIDGE_CACHE_CMD_LAST)

/*  14: Secure Memory Management functions */
#define PVRSRV_BRIDGE_SMM					14UL
#if defined(SUPPORT_SECURE_EXPORT)
#define PVRSRV_BRIDGE_SMM_DISPATCH_FIRST (PVRSRV_BRIDGE_CACHE_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_SMM_DISPATCH_LAST  (PVRSRV_BRIDGE_SMM_DISPATCH_FIRST + PVRSRV_BRIDGE_SMM_CMD_LAST)
#else
#define PVRSRV_BRIDGE_SMM_DISPATCH_FIRST 0
#define PVRSRV_BRIDGE_SMM_DISPATCH_LAST  (PVRSRV_BRIDGE_CACHE_DISPATCH_LAST)
#endif

/*  15: Transport Layer interface functions */
#define PVRSRV_BRIDGE_PVRTL					15UL
#define PVRSRV_BRIDGE_PVRTL_DISPATCH_FIRST (PVRSRV_BRIDGE_SMM_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_PVRTL_DISPATCH_LAST  (PVRSRV_BRIDGE_PVRTL_DISPATCH_FIRST + PVRSRV_BRIDGE_PVRTL_CMD_LAST)

/*  16: Resource Information (RI) interface functions */
#define PVRSRV_BRIDGE_RI					16UL
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
#define PVRSRV_BRIDGE_RI_DISPATCH_FIRST (PVRSRV_BRIDGE_PVRTL_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_RI_DISPATCH_LAST  (PVRSRV_BRIDGE_RI_DISPATCH_FIRST + PVRSRV_BRIDGE_RI_CMD_LAST)
#else
#define PVRSRV_BRIDGE_RI_DISPATCH_FIRST 0
#define PVRSRV_BRIDGE_RI_DISPATCH_LAST  (PVRSRV_BRIDGE_PVRTL_DISPATCH_LAST)
#endif

/*  17: Validation interface functions */
#define PVRSRV_BRIDGE_VALIDATION			17UL
#if defined(SUPPORT_VALIDATION_BRIDGE)
#define PVRSRV_BRIDGE_VALIDATION_DISPATCH_FIRST (PVRSRV_BRIDGE_RI_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_VALIDATION_DISPATCH_LAST  (PVRSRV_BRIDGE_VALIDATION_DISPATCH_FIRST + PVRSRV_BRIDGE_VALIDATION_CMD_LAST)
#else
#define PVRSRV_BRIDGE_VALIDATION_DISPATCH_FIRST 0
#define PVRSRV_BRIDGE_VALIDATION_DISPATCH_LAST  (PVRSRV_BRIDGE_RI_DISPATCH_LAST)
#endif

/*  18: TUTILS interface functions */
#define PVRSRV_BRIDGE_TUTILS				18UL
#if defined(PVR_TESTING_UTILS)
#define PVRSRV_BRIDGE_TUTILS_DISPATCH_FIRST (PVRSRV_BRIDGE_VALIDATION_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_TUTILS_DISPATCH_LAST  (PVRSRV_BRIDGE_TUTILS_DISPATCH_FIRST + PVRSRV_BRIDGE_TUTILS_CMD_LAST)
#else
#define PVRSRV_BRIDGE_TUTILS_DISPATCH_FIRST 0
#define PVRSRV_BRIDGE_TUTILS_DISPATCH_LAST  (PVRSRV_BRIDGE_VALIDATION_DISPATCH_LAST)
#endif

/*  19: DevMem history interface functions */
#define PVRSRV_BRIDGE_DEVICEMEMHISTORY		19UL
#define PVRSRV_BRIDGE_DEVICEMEMHISTORY_DISPATCH_FIRST (PVRSRV_BRIDGE_TUTILS_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_DEVICEMEMHISTORY_DISPATCH_LAST  (PVRSRV_BRIDGE_DEVICEMEMHISTORY_DISPATCH_FIRST + PVRSRV_BRIDGE_DEVICEMEMHISTORY_CMD_LAST)

/*  20: Host Trace Buffer interface functions */
#define PVRSRV_BRIDGE_HTBUFFER				20UL
#if !defined(EXCLUDE_HTBUFFER_BRIDGE)
#define PVRSRV_BRIDGE_HTBUFFER_DISPATCH_FIRST (PVRSRV_BRIDGE_DEVICEMEMHISTORY_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_HTBUFFER_DISPATCH_LAST  (PVRSRV_BRIDGE_HTBUFFER_DISPATCH_FIRST + PVRSRV_BRIDGE_HTBUFFER_CMD_LAST)
#else
#define PVRSRV_BRIDGE_HTBUFFER_DISPATCH_FIRST 0
#define PVRSRV_BRIDGE_HTBUFFER_DISPATCH_LAST  (PVRSRV_BRIDGE_DEVICEMEMHISTORY_DISPATCH_LAST)
#endif

/*  21: Non-Linux Display functions */
#define PVRSRV_BRIDGE_DCPLAT				21UL
#if defined(SUPPORT_DISPLAY_CLASS) && defined(SUPPORT_DCPLAT_BRIDGE)
#define PVRSRV_BRIDGE_DCPLAT_DISPATCH_FIRST (PVRSRV_BRIDGE_HTBUFFER_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_DCPLAT_DISPATCH_LAST  (PVRSRV_BRIDGE_DCPLAT_DISPATCH_FIRST + PVRSRV_BRIDGE_DCPLAT_CMD_LAST)
#else
#define PVRSRV_BRIDGE_DCPLAT_DISPATCH_FIRST 0
#define PVRSRV_BRIDGE_DCPLAT_DISPATCH_LAST  (PVRSRV_BRIDGE_HTBUFFER_DISPATCH_LAST)
#endif

/*  22: Extmem functions */
#define PVRSRV_BRIDGE_MMEXTMEM				22UL
#if defined(SUPPORT_WRAP_EXTMEM)
#define PVRSRV_BRIDGE_MMEXTMEM_DISPATCH_FIRST (PVRSRV_BRIDGE_DCPLAT_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_MMEXTMEM_DISPATCH_LAST  (PVRSRV_BRIDGE_MMEXTMEM_DISPATCH_FIRST + PVRSRV_BRIDGE_MMEXTMEM_CMD_LAST)
#else
#define PVRSRV_BRIDGE_MMEXTMEM_DISPATCH_FIRST 0
#define PVRSRV_BRIDGE_MMEXTMEM_DISPATCH_LAST (PVRSRV_BRIDGE_DCPLAT_DISPATCH_LAST)
#endif

/*  23: Sync tracking functions */
#define PVRSRV_BRIDGE_SYNCTRACKING			23UL
#define PVRSRV_BRIDGE_SYNCTRACKING_DISPATCH_FIRST (PVRSRV_BRIDGE_MMEXTMEM_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_SYNCTRACKING_DISPATCH_LAST  (PVRSRV_BRIDGE_SYNCTRACKING_DISPATCH_FIRST + PVRSRV_BRIDGE_SYNCTRACKING_CMD_LAST)

/*  24: Sync fallback functions */
#define PVRSRV_BRIDGE_SYNCFALLBACK			24UL
#if defined(SUPPORT_FALLBACK_FENCE_SYNC)
#define PVRSRV_BRIDGE_SYNCFALLBACK_DISPATCH_FIRST (PVRSRV_BRIDGE_SYNCTRACKING_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_SYNCFALLBACK_DISPATCH_LAST  (PVRSRV_BRIDGE_SYNCFALLBACK_DISPATCH_FIRST + PVRSRV_BRIDGE_SYNCFALLBACK_CMD_LAST)
#else
#define PVRSRV_BRIDGE_SYNCFALLBACK_DISPATCH_FIRST 0
#define PVRSRV_BRIDGE_SYNCFALLBACK_DISPATCH_LAST  (PVRSRV_BRIDGE_SYNCTRACKING_DISPATCH_LAST)
#endif

/*  25: Debug Information (DI) interface functions */
#define PVRSRV_BRIDGE_DI					25UL
#if defined(SUPPORT_DI_BRG_IMPL)
#define PVRSRV_BRIDGE_DI_DISPATCH_FIRST (PVRSRV_BRIDGE_SYNCFALLBACK_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_DI_DISPATCH_LAST  (PVRSRV_BRIDGE_DI_DISPATCH_FIRST + PVRSRV_BRIDGE_DI_CMD_LAST)
#else
#define PVRSRV_BRIDGE_DI_DISPATCH_FIRST 0
#define PVRSRV_BRIDGE_DI_DISPATCH_LAST  (PVRSRV_BRIDGE_SYNCFALLBACK_DISPATCH_LAST)
#endif

/*  26: DMA transfer functions */

#define PVRSRV_BRIDGE_DMA			26UL
#if defined(SUPPORT_DMA_TRANSFER)
#define PVRSRV_BRIDGE_DMA_DISPATCH_FIRST (PVRSRV_BRIDGE_DI_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_DMA_DISPATCH_LAST  (PVRSRV_BRIDGE_DMA_DISPATCH_FIRST + PVRSRV_BRIDGE_DMA_CMD_LAST)
#else
#define PVRSRV_BRIDGE_DMA_DISPATCH_FIRST 0
#define PVRSRV_BRIDGE_DMA_DISPATCH_LAST  (PVRSRV_BRIDGE_DI_DISPATCH_LAST)
#endif

/* NB PVRSRV_BRIDGE_LAST below must be the last bridge group defined above (PVRSRV_BRIDGE_FEATURE) */
#define PVRSRV_BRIDGE_LAST					(PVRSRV_BRIDGE_DMA)
/* NB PVRSRV_BRIDGE_DISPATCH LAST below must be the last dispatch entry defined above (PVRSRV_BRIDGE_FEATURE_DISPATCH_LAST) */
#define PVRSRV_BRIDGE_DISPATCH_LAST			(PVRSRV_BRIDGE_DMA_DISPATCH_LAST)

/* bit mask representing the enabled PVR bridges */

static const IMG_UINT32 gui32PVRBridges =
	  (1U << (PVRSRV_BRIDGE_DEFAULT - PVRSRV_BRIDGE_FIRST))
	| (1U << (PVRSRV_BRIDGE_SRVCORE - PVRSRV_BRIDGE_FIRST))
	| (1U << (PVRSRV_BRIDGE_SYNC - PVRSRV_BRIDGE_FIRST))

#if defined(PDUMP)
	| (1U << (PVRSRV_BRIDGE_PDUMPCTRL - PVRSRV_BRIDGE_FIRST))
#endif
	| (1U << (PVRSRV_BRIDGE_MM - PVRSRV_BRIDGE_FIRST))
#if defined(SUPPORT_MMPLAT_BRIDGE)
	| (1U << (PVRSRV_BRIDGE_MMPLAT - PVRSRV_BRIDGE_FIRST))
#endif
#if defined(SUPPORT_CMM)
	| (1U << (PVRSRV_BRIDGE_CMM - PVRSRV_BRIDGE_FIRST))
#endif
#if defined(PDUMP)
	| (1U << (PVRSRV_BRIDGE_PDUMPMM - PVRSRV_BRIDGE_FIRST))
	| (1U << (PVRSRV_BRIDGE_PDUMP - PVRSRV_BRIDGE_FIRST))
#endif
#if defined(__linux__)
	| (1U << (PVRSRV_BRIDGE_DMABUF - PVRSRV_BRIDGE_FIRST))
#endif
#if defined(SUPPORT_DISPLAY_CLASS)
	| (1U << (PVRSRV_BRIDGE_DC - PVRSRV_BRIDGE_FIRST))
#endif
	| (1U << (PVRSRV_BRIDGE_CACHE - PVRSRV_BRIDGE_FIRST))
#if defined(SUPPORT_SECURE_EXPORT)
	| (1U << (PVRSRV_BRIDGE_SMM - PVRSRV_BRIDGE_FIRST))
#endif
	| (1U << (PVRSRV_BRIDGE_PVRTL - PVRSRV_BRIDGE_FIRST))
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
	| (1U << (PVRSRV_BRIDGE_RI - PVRSRV_BRIDGE_FIRST))
#endif
#if defined(SUPPORT_VALIDATION)
	| (1U << (PVRSRV_BRIDGE_VALIDATION - PVRSRV_BRIDGE_FIRST))
#endif
#if defined(PVR_TESTING_UTILS)
	| (1U << (PVRSRV_BRIDGE_TUTILS - PVRSRV_BRIDGE_FIRST))
#endif
	| (1U << (PVRSRV_BRIDGE_DEVICEMEMHISTORY - PVRSRV_BRIDGE_FIRST))
#if defined(SUPPORT_HTBUFFER)
	| (1U << (PVRSRV_BRIDGE_HTBUFFER - PVRSRV_BRIDGE_FIRST))
#endif
#if defined(SUPPORT_DISPLAY_CLASS) && defined(SUPPORT_DCPLAT_BRIDGE)
	| (1U << (PVRSRV_BRIDGE_DCPLAT - PVRSRV_BRIDGE_FIRST))
#endif
#if defined(SUPPORT_WRAP_EXTMEM)
	| (1U << (PVRSRV_BRIDGE_MMEXTMEM - PVRSRV_BRIDGE_FIRST))
#endif
	| (1U << (PVRSRV_BRIDGE_SYNCTRACKING - PVRSRV_BRIDGE_FIRST))
#if defined(SUPPORT_FALLBACK_FENCE_SYNC)
	| (1U << (PVRSRV_BRIDGE_SYNCFALLBACK - PVRSRV_BRIDGE_FIRST))
#endif
#if defined(SUPPORT_DI_BRG_IMPL)
	| (1U << (PVRSRV_BRIDGE_DI - PVRSRV_BRIDGE_FIRST))
#endif
#if defined(SUPPORT_DMA_TRANSFER)
	| (1U << (PVRSRV_BRIDGE_DMA - PVRSRV_BRIDGE_FIRST))
#endif
	;

/* bit field representing which PVR bridge groups may optionally not
 * be present in the server
 */
#define PVR_BRIDGES_OPTIONAL \
	( \
		(1U << (PVRSRV_BRIDGE_RI - PVRSRV_BRIDGE_FIRST)) \
	)

/******************************************************************************
 * Generic bridge structures
 *****************************************************************************/


/******************************************************************************
 * bridge packaging structure
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_PACKAGE_TAG
{
	IMG_UINT32		ui32BridgeID;			/*!< ioctl bridge group */
	IMG_UINT32		ui32FunctionID;			/*!< ioctl function index */
	IMG_UINT32		ui32Size;				/*!< size of structure */
	void __user		*pvParamIn;				/*!< input data buffer */
	IMG_UINT32		ui32InBufferSize;		/*!< size of input data buffer */
	void __user		*pvParamOut;			/*!< output data buffer */
	IMG_UINT32		ui32OutBufferSize;		/*!< size of output data buffer */
}PVRSRV_BRIDGE_PACKAGE;

#if defined(__cplusplus)
}
#endif

#endif /* PVR_BRIDGE_H */

/******************************************************************************
 End of file (pvr_bridge.h)
******************************************************************************/
