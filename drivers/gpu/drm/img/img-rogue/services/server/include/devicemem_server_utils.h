/**************************************************************************/ /*!
@File
@Title          Device Memory Management
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header file utilities that are specific to device memory functions
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
*/ /***************************************************************************/

#include "img_defs.h"
#include "img_types.h"
#include "device.h"
#include "pvrsrv_memallocflags.h"
#include "pvrsrv.h"

static INLINE PVRSRV_ERROR DevmemCPUCacheMode(PVRSRV_DEVICE_NODE *psDeviceNode,
											  PVRSRV_MEMALLOCFLAGS_T ulFlags,
											  IMG_UINT32 *pui32Ret)
{
	IMG_UINT32 ui32CPUCacheMode = PVRSRV_CPU_CACHE_MODE(ulFlags);
	IMG_UINT32 ui32Ret;
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_ASSERT(ui32CPUCacheMode == PVRSRV_CPU_CACHE_MODE(ulFlags));

	switch (ui32CPUCacheMode)
	{
		case PVRSRV_MEMALLOCFLAG_CPU_UNCACHED:
			ui32Ret = PVRSRV_MEMALLOCFLAG_CPU_UNCACHED;
			break;

		case PVRSRV_MEMALLOCFLAG_CPU_UNCACHED_WC:
			ui32Ret = PVRSRV_MEMALLOCFLAG_CPU_UNCACHED_WC;
			break;

		case PVRSRV_MEMALLOCFLAG_CPU_CACHE_INCOHERENT:
#if defined(SAFETY_CRITICAL_BUILD)
			ui32Ret = PVRSRV_MEMALLOCFLAG_CPU_UNCACHED_WC;
#else
			ui32Ret = PVRSRV_MEMALLOCFLAG_CPU_CACHED;
#endif
			break;

		case PVRSRV_MEMALLOCFLAG_CPU_CACHE_COHERENT:

			/*
			 * If system has no coherency but coherency has been requested for CPU
			 * and GPU we currently fall back to write-combine.
			 * This avoids errors on arm64 when uncached is turned into ordered device memory
			 * and suffers from problems with unaligned access.
			 */
			if ( (PVRSRV_GPU_CACHE_MODE(ulFlags) == PVRSRV_MEMALLOCFLAG_GPU_CACHE_COHERENT) &&
				!(PVRSRVSystemSnoopingOfCPUCache(psDeviceNode->psDevConfig) && PVRSRVSystemSnoopingOfDeviceCache(psDeviceNode->psDevConfig)) )
			{
				ui32Ret = PVRSRV_MEMALLOCFLAG_CPU_UNCACHED_WC;
			}
			else
			{
#if defined(SAFETY_CRITICAL_BUILD)
				ui32Ret = PVRSRV_MEMALLOCFLAG_CPU_UNCACHED_WC;
#else
				ui32Ret = PVRSRV_MEMALLOCFLAG_CPU_CACHED;
#endif
			}
			break;

		default:
			PVR_LOG(("DevmemCPUCacheMode: Unknown CPU cache mode 0x%08x", ui32CPUCacheMode));
			PVR_ASSERT(0);
			/*
				We should never get here, but if we do then setting the mode
				to uncached is the safest thing to do.
			*/
			ui32Ret = PVRSRV_MEMALLOCFLAG_CPU_UNCACHED;
			eError = PVRSRV_ERROR_UNSUPPORTED_CACHE_MODE;
			break;
	}

	*pui32Ret = ui32Ret;

	return eError;
}

static INLINE PVRSRV_ERROR DevmemDeviceCacheMode(PVRSRV_DEVICE_NODE *psDeviceNode,
												 PVRSRV_MEMALLOCFLAGS_T ulFlags,
												 IMG_UINT32 *pui32Ret)
{
	IMG_UINT32 ui32DeviceCacheMode = PVRSRV_GPU_CACHE_MODE(ulFlags);
	IMG_UINT32 ui32Ret;
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_ASSERT(ui32DeviceCacheMode == PVRSRV_GPU_CACHE_MODE(ulFlags));

	switch (ui32DeviceCacheMode)
	{
		case PVRSRV_MEMALLOCFLAG_GPU_UNCACHED:
			ui32Ret = PVRSRV_MEMALLOCFLAG_GPU_UNCACHED;
			break;

		case PVRSRV_MEMALLOCFLAG_GPU_UNCACHED_WC:
			ui32Ret = PVRSRV_MEMALLOCFLAG_GPU_UNCACHED_WC;
			break;

		case PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT:
#if defined(SAFETY_CRITICAL_BUILD)
			ui32Ret = PVRSRV_MEMALLOCFLAG_GPU_UNCACHED_WC;
#else
			ui32Ret = PVRSRV_MEMALLOCFLAG_GPU_CACHED;
#endif
			break;

		case PVRSRV_MEMALLOCFLAG_GPU_CACHE_COHERENT:

			/*
			 * If system has no coherency but coherency has been requested for CPU
			 * and GPU we currently fall back to write-combine.
			 * This avoids errors on arm64 when uncached is turned into ordered device memory
			 * and suffers from problems with unaligned access.
			 */
			if ( (PVRSRV_CPU_CACHE_MODE(ulFlags) == PVRSRV_MEMALLOCFLAG_CPU_CACHE_COHERENT) &&
				!(PVRSRVSystemSnoopingOfCPUCache(psDeviceNode->psDevConfig) && PVRSRVSystemSnoopingOfDeviceCache(psDeviceNode->psDevConfig)) )
			{
				ui32Ret = PVRSRV_MEMALLOCFLAG_GPU_UNCACHED_WC;
			}
			else
			{
#if defined(SAFETY_CRITICAL_BUILD)
				ui32Ret = PVRSRV_MEMALLOCFLAG_GPU_UNCACHED_WC;
#else
				ui32Ret = PVRSRV_MEMALLOCFLAG_GPU_CACHED;
#endif
			}
			break;

		default:
			PVR_LOG(("DevmemDeviceCacheMode: Unknown device cache mode 0x%08x", ui32DeviceCacheMode));
			PVR_ASSERT(0);
			/*
				We should never get here, but if we do then setting the mode
				to uncached is the safest thing to do.
			*/
			ui32Ret = PVRSRV_MEMALLOCFLAG_GPU_UNCACHED;
			eError = PVRSRV_ERROR_UNSUPPORTED_CACHE_MODE;
			break;
	}

	*pui32Ret = ui32Ret;

	return eError;
}

static INLINE IMG_BOOL DevmemCPUCacheCoherency(PVRSRV_DEVICE_NODE *psDeviceNode,
											   PVRSRV_MEMALLOCFLAGS_T ulFlags)
{
	IMG_UINT32 ui32CPUCacheMode = PVRSRV_CPU_CACHE_MODE(ulFlags);
	IMG_BOOL bRet = IMG_FALSE;

	PVR_ASSERT(ui32CPUCacheMode == PVRSRV_CPU_CACHE_MODE(ulFlags));

	if (ui32CPUCacheMode == PVRSRV_MEMALLOCFLAG_CPU_CACHE_COHERENT)
	{
		bRet = PVRSRVSystemSnoopingOfDeviceCache(psDeviceNode->psDevConfig);
	}
	return bRet;
}

static INLINE IMG_BOOL DevmemDeviceCacheCoherency(PVRSRV_DEVICE_NODE *psDeviceNode,
												  PVRSRV_MEMALLOCFLAGS_T ulFlags)
{
	IMG_UINT32 ui32DeviceCacheMode = PVRSRV_GPU_CACHE_MODE(ulFlags);
	IMG_BOOL bRet = IMG_FALSE;

	PVR_ASSERT(ui32DeviceCacheMode == PVRSRV_GPU_CACHE_MODE(ulFlags));

	if (ui32DeviceCacheMode == PVRSRV_MEMALLOCFLAG_GPU_CACHE_COHERENT)
	{
		bRet = PVRSRVSystemSnoopingOfCPUCache(psDeviceNode->psDevConfig);
	}
	return bRet;
}
