/*************************************************************************/ /*!
@File
@Title          RISC-V specific OS functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Processor specific OS functions
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

#include "img_defs.h"
#include "osfunc.h"
#include "pvr_debug.h"
#include "cache_ops.h"

extern void SysDevHost_Cache_Maintenance(IMG_HANDLE hSysData,
                                                                        PVRSRV_CACHE_OP eRequestType,
                                                                        void *pvVirtStart,
                                                                        void *pvVirtEnd,
                                                                        IMG_CPU_PHYADDR sCPUPhysStar,
                                                                        IMG_CPU_PHYADDR sCPUPhysEnd);


void OSCPUCacheFlushRangeKM(PVRSRV_DEVICE_NODE *psDevNode,
							void *pvVirtStart,
							void *pvVirtEnd,
							IMG_CPU_PHYADDR sCPUPhysStart,
							IMG_CPU_PHYADDR sCPUPhysEnd)
{
	/*
	 * RISC-V cache maintenance mechanism is not part of the core spec.
	 * This leaves the actual mechanism of action to an implementer.
	 * Here we let the system layer decide how maintenance is done.
	 */
	if (psDevNode->psDevConfig->pfnHostCacheMaintenance)
	{
		psDevNode->psDevConfig->pfnHostCacheMaintenance(
				psDevNode->psDevConfig->hSysData,
				PVRSRV_CACHE_OP_FLUSH,
				pvVirtStart,
				pvVirtEnd,
				sCPUPhysStart,
				sCPUPhysEnd);

	}
#if !defined(NO_HARDWARE)
	else
	{
		//PVR_DPF((PVR_DBG_WARNING,
		//        "%s: System doesn't implement cache maintenance. Skipping!",
		//         __func__));
                SysDevHost_Cache_Maintenance(
                                psDevNode->psDevConfig->hSysData,
                                PVRSRV_CACHE_OP_FLUSH,
                                pvVirtStart,
                                pvVirtEnd,
                                sCPUPhysStart,
                                sCPUPhysEnd);

	}
#endif
}

void OSCPUCacheCleanRangeKM(PVRSRV_DEVICE_NODE *psDevNode,
							void *pvVirtStart,
							void *pvVirtEnd,
							IMG_CPU_PHYADDR sCPUPhysStart,
							IMG_CPU_PHYADDR sCPUPhysEnd)
{
	/*
	 * RISC-V cache maintenance mechanism is not part of the core spec.
	 * This leaves the actual mechanism of action to an implementer.
	 * Here we let the system layer decide how maintenance is done.
	 */
	if (psDevNode->psDevConfig->pfnHostCacheMaintenance)
	{
		psDevNode->psDevConfig->pfnHostCacheMaintenance(
				psDevNode->psDevConfig->hSysData,
				PVRSRV_CACHE_OP_CLEAN,
				pvVirtStart,
				pvVirtEnd,
				sCPUPhysStart,
				sCPUPhysEnd);

	}
#if !defined(NO_HARDWARE)
	else
	{
		//PVR_DPF((PVR_DBG_WARNING,
		//         "%s: System doesn't implement cache maintenance. Skipping!",
		//         __func__));
                SysDevHost_Cache_Maintenance(
                                psDevNode->psDevConfig->hSysData,
                                PVRSRV_CACHE_OP_CLEAN,
                                pvVirtStart,
                                pvVirtEnd,
                                sCPUPhysStart,
                                sCPUPhysEnd);


	}
#endif
}

void OSCPUCacheInvalidateRangeKM(PVRSRV_DEVICE_NODE *psDevNode,
								 void *pvVirtStart,
								 void *pvVirtEnd,
								 IMG_CPU_PHYADDR sCPUPhysStart,
								 IMG_CPU_PHYADDR sCPUPhysEnd)
{
	/*
	 * RISC-V cache maintenance mechanism is not part of the core spec.
	 * This leaves the actual mechanism of action to an implementer.
	 * Here we let the system layer decide how maintenance is done.
	 */
	if (psDevNode->psDevConfig->pfnHostCacheMaintenance)
	{
		psDevNode->psDevConfig->pfnHostCacheMaintenance(
				psDevNode->psDevConfig->hSysData,
				PVRSRV_CACHE_OP_INVALIDATE,
				pvVirtStart,
				pvVirtEnd,
				sCPUPhysStart,
				sCPUPhysEnd);

	}
#if !defined(NO_HARDWARE)
	else
	{
		//PVR_DPF((PVR_DBG_WARNING,
		//         "%s: System doesn't implement cache maintenance. Skipping!",
		//         __func__));
                SysDevHost_Cache_Maintenance(
                                psDevNode->psDevConfig->hSysData,
                                PVRSRV_CACHE_OP_INVALIDATE,
                                pvVirtStart,
                                pvVirtEnd,
                                sCPUPhysStart,
                                sCPUPhysEnd);

	}
#endif
}

OS_CACHE_OP_ADDR_TYPE OSCPUCacheOpAddressType(void)
{
	/*
	 * Need to obtain psDevNode here and do the following:
	 *
	 * OS_CACHE_OP_ADDR_TYPE eOpAddrType =
	 *	psDevNode->psDevConfig->bHasPhysicalCacheMaintenance ?
	 *		OS_CACHE_OP_ADDR_TYPE_PHYSICAL : OS_CACHE_OP_ADDR_TYPE_VIRTUAL;
	 *
	 * Return BOTH for now on.
	 *
	 */
	return OS_CACHE_OP_ADDR_TYPE_PHYSICAL;//OS_CACHE_OP_ADDR_TYPE_BOTH;
}

void OSUserModeAccessToPerfCountersEn(void)
{
#if 0//!defined(NO_HARDWARE)
	PVR_DPF((PVR_DBG_WARNING, "%s: Not implemented!", __func__));
	PVR_ASSERT(0);
#endif
}

IMG_BOOL OSIsWriteCombineUnalignedSafe(void)
{
#if 0//!defined(NO_HARDWARE)
	PVR_DPF((PVR_DBG_WARNING,
	         "%s: Not implemented (assuming false)!",
	         __func__));
	PVR_ASSERT(0);
	return IMG_FALSE;
#else
	return IMG_TRUE;
#endif
}
