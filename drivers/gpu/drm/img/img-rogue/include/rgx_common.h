/*************************************************************************/ /*!
@File
@Title          RGX Common Types and Defines Header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Common types and definitions for RGX software
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
#ifndef RGX_COMMON_H
#define RGX_COMMON_H

#if defined(__cplusplus)
extern "C" {
#endif

#include "img_defs.h"

/* Included to get the BVNC_KM_N defined and other feature defs */
#include "km/rgxdefs_km.h"

#include "rgx_common_asserts.h"


/* Virtualisation validation builds are meant to test the VZ-related hardware without a fully virtualised platform.
 * As such a driver can support either the vz-validation code or real virtualisation.
 * Note: PVRSRV_VZ_NUM_OSID is the external build option, while RGX_NUM_OS_SUPPORTED is the internal symbol used in the DDK */
#if defined(SUPPORT_GPUVIRT_VALIDATION) && (defined(RGX_NUM_OS_SUPPORTED) && (RGX_NUM_OS_SUPPORTED > 1))
#error "Invalid build configuration: Virtualisation support (PVRSRV_VZ_NUM_OSID > 1) and virtualisation validation code (SUPPORT_GPUVIRT_VALIDATION) are mutually exclusive."
#endif

/* The RGXFWIF_DM defines assume only one of RGX_FEATURE_TLA or
 * RGX_FEATURE_FASTRENDER_DM is present. Ensure this with a compile-time check.
 */
#if defined(RGX_FEATURE_TLA) && defined(RGX_FEATURE_FASTRENDER_DM)
#error "Both RGX_FEATURE_TLA and RGX_FEATURE_FASTRENDER_DM defined. Fix code to handle this!"
#endif

/*! The master definition for data masters known to the firmware of RGX.
 * When a new DM is added to this list, relevant entry should be added to
 * RGX_HWPERF_DM enum list.
 * The DM in a V1 HWPerf packet uses this definition. */

typedef IMG_UINT32 RGXFWIF_DM;

#define	RGXFWIF_DM_GP			IMG_UINT32_C(0)
/* Either TDM or 2D DM is present. The above build time error is present to verify this */
#define	RGXFWIF_DM_2D			IMG_UINT32_C(1) /* when RGX_FEATURE_TLA defined */
#define	RGXFWIF_DM_TDM			IMG_UINT32_C(1) /* when RGX_FEATURE_FASTRENDER_DM defined */

#define	RGXFWIF_DM_GEOM			IMG_UINT32_C(2)
#define	RGXFWIF_DM_3D			IMG_UINT32_C(3)
#define	RGXFWIF_DM_CDM			IMG_UINT32_C(4)
#define	RGXFWIF_DM_RAY			IMG_UINT32_C(5)
#define	RGXFWIF_DM_GEOM2		IMG_UINT32_C(6)
#define	RGXFWIF_DM_GEOM3		IMG_UINT32_C(7)
#define	RGXFWIF_DM_GEOM4		IMG_UINT32_C(8)

#define	RGXFWIF_DM_LAST			RGXFWIF_DM_GEOM4

typedef IMG_UINT32 RGX_KICK_TYPE_DM;
#define RGX_KICK_TYPE_DM_GP		IMG_UINT32_C(0x001)
#define RGX_KICK_TYPE_DM_TDM_2D	IMG_UINT32_C(0x002)
#define RGX_KICK_TYPE_DM_TA		IMG_UINT32_C(0x004)
#define RGX_KICK_TYPE_DM_3D		IMG_UINT32_C(0x008)
#define RGX_KICK_TYPE_DM_CDM	IMG_UINT32_C(0x010)
#define RGX_KICK_TYPE_DM_RTU	IMG_UINT32_C(0x020)
#define RGX_KICK_TYPE_DM_SHG	IMG_UINT32_C(0x040)
#define RGX_KICK_TYPE_DM_TQ2D	IMG_UINT32_C(0x080)
#define RGX_KICK_TYPE_DM_TQ3D	IMG_UINT32_C(0x100)
#define RGX_KICK_TYPE_DM_RAY	IMG_UINT32_C(0x200)
#define RGX_KICK_TYPE_DM_LAST	IMG_UINT32_C(0x400)

/* Maximum number of DM in use: GP, 2D/TDM, GEOM, 3D, CDM, RDM, GEOM2, GEOM3, GEOM4 */
#define RGXFWIF_DM_MAX			(RGXFWIF_DM_LAST + 1U)

/*
 * Data Master Tags to be appended to resources created on behalf of each RGX
 * Context.
 */
#define RGX_RI_DM_TAG_KS   'K'
#define RGX_RI_DM_TAG_CDM  'C'
#define RGX_RI_DM_TAG_RC   'R' /* To be removed once TA/3D Timelines are split */
#define RGX_RI_DM_TAG_TA   'V'
#define RGX_RI_DM_TAG_GEOM 'V'
#define RGX_RI_DM_TAG_3D   'P'
#define RGX_RI_DM_TAG_TDM  'T'
#define RGX_RI_DM_TAG_TQ2D '2'
#define RGX_RI_DM_TAG_TQ3D 'Q'
#define RGX_RI_DM_TAG_RAY  'r'

/*
 * Client API Tags to be appended to resources created on behalf of each
 * Client API.
 */
#define RGX_RI_CLIENT_API_GLES1    '1'
#define RGX_RI_CLIENT_API_GLES3    '3'
#define RGX_RI_CLIENT_API_VULKAN   'V'
#define RGX_RI_CLIENT_API_EGL      'E'
#define RGX_RI_CLIENT_API_OPENCL   'C'
#define RGX_RI_CLIENT_API_OPENGL   'G'
#define RGX_RI_CLIENT_API_SERVICES 'S'
#define RGX_RI_CLIENT_API_WSEGL    'W'
#define RGX_RI_CLIENT_API_ANDROID  'A'
#define RGX_RI_CLIENT_API_LWS      'L'

/*
 * Format a RI annotation for a given RGX Data Master context
 */
#define RGX_RI_FORMAT_DM_ANNOTATION(annotation, dmTag, clientAPI) do         \
	{                                                                        \
		(annotation)[0] = (dmTag);                                           \
		(annotation)[1] = (clientAPI);                                       \
		(annotation)[2] = '\0';                                              \
	} while (false)

/*!
 ******************************************************************************
 * RGXFW Compiler alignment definitions
 *****************************************************************************/
#if defined(__GNUC__) || defined(HAS_GNUC_ATTRIBUTES) || defined(INTEGRITY_OS)
#define RGXFW_ALIGN			__attribute__ ((aligned (8)))
#define	RGXFW_ALIGN_DCACHEL		__attribute__((aligned (64)))
#elif defined(_MSC_VER)
#define RGXFW_ALIGN			__declspec(align(8))
#define	RGXFW_ALIGN_DCACHEL		__declspec(align(64))
#pragma warning (disable : 4324)
#else
#error "Align MACROS need to be defined for this compiler"
#endif

/*!
 ******************************************************************************
 * Force 8-byte alignment for structures allocated uncached.
 *****************************************************************************/
#define UNCACHED_ALIGN      RGXFW_ALIGN


/*!
 ******************************************************************************
 * GPU Utilisation states
 *****************************************************************************/
#define RGXFWIF_GPU_UTIL_STATE_IDLE      (0U)
#define RGXFWIF_GPU_UTIL_STATE_ACTIVE    (1U)
#define RGXFWIF_GPU_UTIL_STATE_BLOCKED   (2U)
#define RGXFWIF_GPU_UTIL_STATE_NUM       (3U)
#define RGXFWIF_GPU_UTIL_STATE_MASK      IMG_UINT64_C(0x0000000000000003)


/*
 * Maximum amount of register writes that can be done by the register
 * programmer (FW or META DMA). This is not a HW limitation, it is only
 * a protection against malformed inputs to the register programmer.
 */
#define RGX_MAX_NUM_REGISTER_PROGRAMMER_WRITES  (128U)

/* FW common context priority. */
/*!
 * @AddToGroup WorkloadContexts
 * @{
 */
#define RGX_CTX_PRIORITY_REALTIME  (INT32_MAX)
#define RGX_CTX_PRIORITY_HIGH      (2U) /*!< HIGH priority */
#define RGX_CTX_PRIORITY_MEDIUM    (1U) /*!< MEDIUM priority */
#define RGX_CTX_PRIORITY_LOW       (0) /*!< LOW priority */
/*!
 * @} End of AddToGroup WorkloadContexts
 */


/*
 *   Use of the 32-bit context property flags mask
 *   ( X = taken/in use, - = available/unused )
 *
 *                                   0
 *                                   |
 *    -------------------------------x
 */
/*
 * Context creation flags
 * (specify a context's properties at creation time)
 */
#define RGX_CONTEXT_FLAG_DISABLESLR					(1UL << 0) /*!< Disable SLR */

/* Bitmask of context flags allowed to be modified after context create. */
#define RGX_CONTEXT_FLAGS_WRITEABLE_MASK            (RGX_CONTEXT_FLAG_DISABLESLR)

/* List of attributes that may be set for a context */
typedef enum _RGX_CONTEXT_PROPERTY_
{
	RGX_CONTEXT_PROPERTY_FLAGS  = 0, /*!< Context flags */
} RGX_CONTEXT_PROPERTY;

#if defined(__cplusplus)
}
#endif

#endif /* RGX_COMMON_H */

/******************************************************************************
 End of file
******************************************************************************/
