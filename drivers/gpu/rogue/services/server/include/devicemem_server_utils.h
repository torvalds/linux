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
#include "pvrsrv_memallocflags.h"
#include "pvrsrv.h"

static INLINE IMG_UINT32 DevmemCPUCacheMode(PVRSRV_MEMALLOCFLAGS_T ulFlags)
{
	IMG_UINT32 ui32CPUCacheMode = ulFlags & PVRSRV_MEMALLOCFLAG_CPU_CACHE_MODE_MASK;
	IMG_UINT32 ui32Ret;

	PVR_ASSERT(ui32CPUCacheMode == (ulFlags & PVRSRV_MEMALLOCFLAG_CPU_CACHE_MODE_MASK));

	switch (ui32CPUCacheMode)
	{
		case PVRSRV_MEMALLOCFLAG_CPU_UNCACHED:
			ui32Ret = PVRSRV_MEMALLOCFLAG_CPU_UNCACHED;
			break;

		case PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE:
			ui32Ret = PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE;
			break;

		case PVRSRV_MEMALLOCFLAG_CPU_CACHE_INCOHERENT:
			ui32Ret = PVRSRV_MEMALLOCFLAG_CPU_CACHED;
			break;

		case PVRSRV_MEMALLOCFLAG_CPU_CACHE_COHERENT:
			/* Fall through */
		case PVRSRV_MEMALLOCFLAG_CPU_CACHED_CACHE_COHERENT:
			/*
				If the allocation needs to be coherent what we end up doing
				depends on the snooping features of the system
			*/
			if (PVRSRVSystemSnoopingOfCPUCache())
			{
				/*
					If the system has CPU cache snooping (tested above)
					then the allocation should be cached ...
				*/
				ui32Ret = PVRSRV_MEMALLOCFLAG_CPU_CACHED;
			}
			else
			{
				/* ... otherwise it should be uncached */
				ui32Ret = PVRSRV_MEMALLOCFLAG_CPU_UNCACHED;
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
			break;
	}

	return ui32Ret;
}

static INLINE IMG_UINT32 DevmemDeviceCacheMode(PVRSRV_MEMALLOCFLAGS_T ulFlags)
{
	IMG_UINT32 ui32DeviceCacheMode = ulFlags & PVRSRV_MEMALLOCFLAG_GPU_CACHE_MODE_MASK;
	IMG_UINT32 ui32Ret;

	PVR_ASSERT(ui32DeviceCacheMode == (ulFlags & PVRSRV_MEMALLOCFLAG_GPU_CACHE_MODE_MASK));

	switch (ui32DeviceCacheMode)
	{
		case PVRSRV_MEMALLOCFLAG_GPU_UNCACHED:
			ui32Ret = PVRSRV_MEMALLOCFLAG_GPU_UNCACHED;
			break;

		case PVRSRV_MEMALLOCFLAG_GPU_WRITE_COMBINE:
			ui32Ret = PVRSRV_MEMALLOCFLAG_GPU_WRITE_COMBINE;
			break;

		case PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT:
			ui32Ret = PVRSRV_MEMALLOCFLAG_GPU_CACHED;
			break;

		case PVRSRV_MEMALLOCFLAG_GPU_CACHE_COHERENT:
			/* Fall through */
		case PVRSRV_MEMALLOCFLAG_GPU_CACHED_CACHE_COHERENT:
			/*
				If the allocation needs to be coherent what we end up doing
				depends on the snooping features of the system
			*/
			if (PVRSRVSystemSnoopingOfDeviceCache())
			{
				/*
					If the system has GPU cache snooping (tested above)
					then the allocation should be cached ...
				*/
				ui32Ret = PVRSRV_MEMALLOCFLAG_GPU_CACHED;
			}
			else
			{
				/* ... otherwise it should be uncached */
				ui32Ret = PVRSRV_MEMALLOCFLAG_GPU_UNCACHED;
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
			break;
	}

	return ui32Ret;
}

static INLINE IMG_BOOL DevmemCPUCacheCoherency(PVRSRV_MEMALLOCFLAGS_T ulFlags)
{
	IMG_UINT32 ui32CPUCacheMode = ulFlags & PVRSRV_MEMALLOCFLAG_CPU_CACHE_MODE_MASK;
	IMG_BOOL bRet = IMG_FALSE;

	PVR_ASSERT(ui32CPUCacheMode == (ulFlags & PVRSRV_MEMALLOCFLAG_CPU_CACHE_MODE_MASK));

	if ((ui32CPUCacheMode == PVRSRV_MEMALLOCFLAG_CPU_CACHE_COHERENT) ||
		(ui32CPUCacheMode == PVRSRV_MEMALLOCFLAG_CPU_CACHED_CACHE_COHERENT))
	{
		bRet = PVRSRVSystemSnoopingOfDeviceCache();
	}
	return bRet;
}

static INLINE IMG_BOOL DevmemDeviceCacheCoherency(PVRSRV_MEMALLOCFLAGS_T ulFlags)
{
	IMG_UINT32 ui32DeviceCacheMode = ulFlags & PVRSRV_MEMALLOCFLAG_GPU_CACHE_MODE_MASK;
	IMG_BOOL bRet = IMG_FALSE;

	PVR_ASSERT(ui32DeviceCacheMode == (ulFlags & PVRSRV_MEMALLOCFLAG_GPU_CACHE_MODE_MASK));

	if ((ui32DeviceCacheMode == PVRSRV_MEMALLOCFLAG_GPU_CACHE_COHERENT) ||
		(ui32DeviceCacheMode == PVRSRV_MEMALLOCFLAG_GPU_CACHED_CACHE_COHERENT))
	{
		bRet = PVRSRVSystemSnoopingOfCPUCache();
	}
	return bRet;
}
