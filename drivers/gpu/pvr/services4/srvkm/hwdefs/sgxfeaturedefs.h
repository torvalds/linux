/*************************************************************************/ /*!
@Title          SGX fexture definitions
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
#if defined(SGX520)
	#define SGX_CORE_FRIENDLY_NAME							"SGX520"
	#define SGX_CORE_ID										SGX_CORE_ID_520
	#define SGX_FEATURE_ADDRESS_SPACE_SIZE					(28)
	#define SGX_FEATURE_NUM_USE_PIPES						(1)
	#define SGX_FEATURE_AUTOCLOCKGATING
#else
#if defined(SGX530)
	#define SGX_CORE_FRIENDLY_NAME							"SGX530"
	#define SGX_CORE_ID										SGX_CORE_ID_530
	#define SGX_FEATURE_ADDRESS_SPACE_SIZE					(28)
	#define SGX_FEATURE_NUM_USE_PIPES						(2)
	#define SGX_FEATURE_AUTOCLOCKGATING
#else
#if defined(SGX531)
	#define SGX_CORE_FRIENDLY_NAME							"SGX531"
	#define SGX_CORE_ID										SGX_CORE_ID_531
	#define SGX_FEATURE_ADDRESS_SPACE_SIZE					(28)
	#define SGX_FEATURE_NUM_USE_PIPES						(2)
	#define SGX_FEATURE_AUTOCLOCKGATING
	#define SGX_FEATURE_MULTI_EVENT_KICK
#else
#if defined(SGX535)
	#define SGX_CORE_FRIENDLY_NAME							"SGX535"
	#define SGX_CORE_ID										SGX_CORE_ID_535
	#define SGX_FEATURE_ADDRESS_SPACE_SIZE					(32)
	#define SGX_FEATURE_MULTIPLE_MEM_CONTEXTS
	#define SGX_FEATURE_BIF_NUM_DIRLISTS					(16)
	#define SGX_FEATURE_2D_HARDWARE
	#define SGX_FEATURE_NUM_USE_PIPES						(2)
	#define SGX_FEATURE_AUTOCLOCKGATING
	#define SUPPORT_SGX_GENERAL_MAPPING_HEAP
	#define SGX_FEATURE_EDM_VERTEX_PDSADDR_FULL_RANGE
#else
#if defined(SGX540)
	#define SGX_CORE_FRIENDLY_NAME							"SGX540"
	#define SGX_CORE_ID										SGX_CORE_ID_540
	#define SGX_FEATURE_ADDRESS_SPACE_SIZE					(28)
	#define SGX_FEATURE_NUM_USE_PIPES						(4)
	#define SGX_FEATURE_AUTOCLOCKGATING
	#define SGX_FEATURE_MULTI_EVENT_KICK
#else
#if defined(SGX543)
	#define SGX_CORE_FRIENDLY_NAME							"SGX543"
	#define SGX_CORE_ID										SGX_CORE_ID_543
	#define SGX_FEATURE_USE_NO_INSTRUCTION_PAIRING
	#define SGX_FEATURE_USE_UNLIMITED_PHASES
	#define SGX_FEATURE_ADDRESS_SPACE_SIZE					(32)
	#define SGX_FEATURE_MULTIPLE_MEM_CONTEXTS
	#define SGX_FEATURE_BIF_NUM_DIRLISTS					(8)
	#define SGX_FEATURE_NUM_USE_PIPES						(4)
	#define SGX_FEATURE_AUTOCLOCKGATING
	#define SGX_FEATURE_MONOLITHIC_UKERNEL
	#define SGX_FEATURE_MULTI_EVENT_KICK
	#define SGX_FEATURE_DATA_BREAKPOINTS
    #define SGX_FEATURE_PERPIPE_BKPT_REGS
    #define SGX_FEATURE_PERPIPE_BKPT_REGS_NUMPIPES          (2)
	#define SGX_FEATURE_2D_HARDWARE
	#define SGX_FEATURE_PTLA
	#define SGX_FEATURE_EXTENDED_PERF_COUNTERS
	#define SGX_FEATURE_EDM_VERTEX_PDSADDR_FULL_RANGE
	#if defined(SUPPORT_SGX_LOW_LATENCY_SCHEDULING)
		#if defined(SGX_FEATURE_MP)
		#define SGX_FEATURE_MASTER_VDM_CONTEXT_SWITCH
		#endif
		#define SGX_FEATURE_SLAVE_VDM_CONTEXT_SWITCH
	#endif
#else
#if defined(SGX544)
	#define SGX_CORE_FRIENDLY_NAME							"SGX544"
	#define SGX_CORE_ID										SGX_CORE_ID_544
	#define SGX_FEATURE_USE_NO_INSTRUCTION_PAIRING
	#define SGX_FEATURE_USE_UNLIMITED_PHASES
	#define SGX_FEATURE_ADDRESS_SPACE_SIZE					(32)
	#define SGX_FEATURE_MULTIPLE_MEM_CONTEXTS
	#define SGX_FEATURE_BIF_NUM_DIRLISTS					(8)
	#define SGX_FEATURE_NUM_USE_PIPES						(4)
	#define SGX_FEATURE_AUTOCLOCKGATING
	#define SGX_FEATURE_MONOLITHIC_UKERNEL
	#define SGX_FEATURE_MULTI_EVENT_KICK
//	#define SGX_FEATURE_DATA_BREAKPOINTS
//    #define SGX_FEATURE_PERPIPE_BKPT_REGS
//    #define SGX_FEATURE_PERPIPE_BKPT_REGS_NUMPIPES          (2)
	#if defined(SGX_FEATURE_MP)
		#define SGX_FEATURE_2D_HARDWARE
		#define SGX_FEATURE_PTLA
	#endif
	#define SGX_FEATURE_EXTENDED_PERF_COUNTERS
	#define SGX_FEATURE_EDM_VERTEX_PDSADDR_FULL_RANGE
	#if defined(SUPPORT_SGX_LOW_LATENCY_SCHEDULING)
		#if defined(SGX_FEATURE_MP)
		#define SGX_FEATURE_MASTER_VDM_CONTEXT_SWITCH
		#define SGX_FEATURE_SLAVE_VDM_CONTEXT_SWITCH
		#endif
	#endif
#else
#if defined(SGX545)
	#define SGX_CORE_FRIENDLY_NAME							"SGX545"
	#define SGX_CORE_ID										SGX_CORE_ID_545
	#define SGX_FEATURE_ADDRESS_SPACE_SIZE					(32)
	#define SGX_FEATURE_AUTOCLOCKGATING
	#define SGX_FEATURE_USE_NO_INSTRUCTION_PAIRING
	#define SGX_FEATURE_USE_UNLIMITED_PHASES
	#define SGX_FEATURE_VOLUME_TEXTURES
	#define SGX_FEATURE_HOST_ALLOC_FROM_DPM
	#define SGX_FEATURE_MULTIPLE_MEM_CONTEXTS
	#define SGX_FEATURE_BIF_NUM_DIRLISTS				(16)
	#define SGX_FEATURE_NUM_USE_PIPES					(4)
	#define	SGX_FEATURE_TEXTURESTRIDE_EXTENSION
	#define SGX_FEATURE_PDS_DATA_INTERLEAVE_2DWORDS
	#define SGX_FEATURE_MONOLITHIC_UKERNEL
	#define SGX_FEATURE_ZLS_EXTERNALZ
	#define SGX_FEATURE_NUM_PDS_PIPES					(2)
	#define SGX_FEATURE_NATIVE_BACKWARD_BLIT
	#define SGX_FEATURE_MAX_TA_RENDER_TARGETS				(512)
	#define SGX_FEATURE_SECONDARY_REQUIRES_USE_KICK
	#define SGX_FEATURE_WRITEBACK_DCU
	//FIXME: this is defined in the build config for now
	//#define SGX_FEATURE_36BIT_MMU
	#define SGX_FEATURE_BIF_WIDE_TILING_AND_4K_ADDRESS
	#define SGX_FEATURE_MULTI_EVENT_KICK
	#define SGX_FEATURE_EDM_VERTEX_PDSADDR_FULL_RANGE
#else
#if defined(SGX554)
	#define SGX_CORE_FRIENDLY_NAME							"SGX554"
	#define SGX_CORE_ID										SGX_CORE_ID_554
	#define SGX_FEATURE_USE_NO_INSTRUCTION_PAIRING
	#define SGX_FEATURE_USE_UNLIMITED_PHASES
	#define SGX_FEATURE_ADDRESS_SPACE_SIZE					(32)
	#define SGX_FEATURE_MULTIPLE_MEM_CONTEXTS
	#define SGX_FEATURE_BIF_NUM_DIRLISTS					(8)
	#define SGX_FEATURE_NUM_USE_PIPES						(8)
	#define SGX_FEATURE_AUTOCLOCKGATING
	#define SGX_FEATURE_MONOLITHIC_UKERNEL
	#define SGX_FEATURE_MULTI_EVENT_KICK
//	#define SGX_FEATURE_DATA_BREAKPOINTS
//    #define SGX_FEATURE_PERPIPE_BKPT_REGS
//    #define SGX_FEATURE_PERPIPE_BKPT_REGS_NUMPIPES          (2)
	#define SGX_FEATURE_2D_HARDWARE
	#define SGX_FEATURE_PTLA
	#define SGX_FEATURE_EXTENDED_PERF_COUNTERS
	#define SGX_FEATURE_EDM_VERTEX_PDSADDR_FULL_RANGE
	#if defined(SUPPORT_SGX_LOW_LATENCY_SCHEDULING)
		#if defined(SGX_FEATURE_MP)
		#define SGX_FEATURE_MASTER_VDM_CONTEXT_SWITCH
		#endif
		#define SGX_FEATURE_SLAVE_VDM_CONTEXT_SWITCH
	#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif

#if defined(SGX_FEATURE_SLAVE_VDM_CONTEXT_SWITCH) \
	|| defined(SGX_FEATURE_MASTER_VDM_CONTEXT_SWITCH)
/* Enable the define so common code for HW VDMCS code is compiled */
#define SGX_FEATURE_VDM_CONTEXT_SWITCH
#endif

/*
	'switch-off' features if defined BRNs affect the feature
*/

#if defined(FIX_HW_BRN_27266)
#undef SGX_FEATURE_36BIT_MMU
#endif

#if defined(FIX_HW_BRN_22934)	\
	|| defined(FIX_HW_BRN_25499)
#undef SGX_FEATURE_MULTI_EVENT_KICK
#endif

#if defined(SGX_FEATURE_SYSTEM_CACHE)
	#if defined(SGX_FEATURE_36BIT_MMU)
		#error SGX_FEATURE_SYSTEM_CACHE is incompatible with SGX_FEATURE_36BIT_MMU
	#endif
	#if defined(FIX_HW_BRN_26620) && !defined(SGX_FEATURE_MULTI_EVENT_KICK)
		#define SGX_BYPASS_SYSTEM_CACHE
	#endif
#endif

#if defined(FIX_HW_BRN_29954)
#undef SGX_FEATURE_PERPIPE_BKPT_REGS
#endif

#if defined(FIX_HW_BRN_31620)
#undef SGX_FEATURE_MULTIPLE_MEM_CONTEXTS
#undef SGX_FEATURE_BIF_NUM_DIRLISTS
#endif

/*
	Derive other definitions:
*/

/* define default MP core count */
#if defined(SGX_FEATURE_MP)
#if defined(SGX_FEATURE_MP_CORE_COUNT_TA) && defined(SGX_FEATURE_MP_CORE_COUNT_3D)
#if (SGX_FEATURE_MP_CORE_COUNT_TA > SGX_FEATURE_MP_CORE_COUNT_3D)
#error Number of TA cores larger than number of 3D cores not supported in current driver
#endif /* (SGX_FEATURE_MP_CORE_COUNT_TA > SGX_FEATURE_MP_CORE_COUNT_3D) */
#else
#if defined(SGX_FEATURE_MP_CORE_COUNT)
#define SGX_FEATURE_MP_CORE_COUNT_TA		(SGX_FEATURE_MP_CORE_COUNT)
#define SGX_FEATURE_MP_CORE_COUNT_3D		(SGX_FEATURE_MP_CORE_COUNT)
#else
#error Either SGX_FEATURE_MP_CORE_COUNT or \
both SGX_FEATURE_MP_CORE_COUNT_TA and SGX_FEATURE_MP_CORE_COUNT_3D \
must be defined when SGX_FEATURE_MP is defined
#endif /* SGX_FEATURE_MP_CORE_COUNT */
#endif /* defined(SGX_FEATURE_MP_CORE_COUNT_TA) && defined(SGX_FEATURE_MP_CORE_COUNT_3D) */
#else
#define SGX_FEATURE_MP_CORE_COUNT		(1)
#define SGX_FEATURE_MP_CORE_COUNT_TA	(1)
#define SGX_FEATURE_MP_CORE_COUNT_3D	(1)
#endif /* SGX_FEATURE_MP */

#if defined(SUPPORT_SGX_LOW_LATENCY_SCHEDULING) && !defined(SUPPORT_SGX_PRIORITY_SCHEDULING)
#define SUPPORT_SGX_PRIORITY_SCHEDULING
#endif

#include "img_types.h"

/******************************************************************************
 End of file (sgxfeaturedefs.h)
******************************************************************************/
