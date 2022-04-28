/*************************************************************************/ /*!
@Title          Linux trace event helper functions
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

#include <linux/version.h>
#include <linux/sched.h>

#if defined(CONFIG_TRACE_GPU_MEM) || defined(PVRSRV_ENABLE_GPU_MEM_TRACEPOINT)
#if !defined(CONFIG_TRACE_GPU_MEM)
#define CREATE_TRACE_POINTS
#include <trace/events/gpu_mem.h>
#undef CREATE_TRACE_POINTS
#else /* !defined(CONFIG_TRACE_GPU_MEM) */
#include <trace/events/gpu_mem.h>
#endif /* !defined(CONFIG_TRACE_GPU_MEM) */
#endif /* defined(CONFIG_TRACE_GPU_MEM) || defined(PVRSRV_ENABLE_GPU_MEM_TRACEPOINT) */

#include "img_types.h"
#include "trace_events.h"
#include "rogue_trace_events.h"
#include "sync_checkpoint_external.h"

static bool fence_update_event_enabled, fence_check_event_enabled;

bool trace_rogue_are_fence_updates_traced(void)
{
	return fence_update_event_enabled;
}

bool trace_rogue_are_fence_checks_traced(void)
{
	return fence_check_event_enabled;
}

/*
 * Call backs referenced from rogue_trace_events.h. Note that these are not
 * thread-safe, however, since running trace code when tracing is not enabled is
 * simply a no-op, there is no harm in it.
 */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0))
int trace_fence_update_enabled_callback(void)
#else
void trace_fence_update_enabled_callback(void)
#endif
{
	fence_update_event_enabled = true;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0))
	return 0;
#endif
}

void trace_fence_update_disabled_callback(void)
{
	fence_update_event_enabled = false;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0))
int trace_fence_check_enabled_callback(void)
#else
void trace_fence_check_enabled_callback(void)
#endif
{
	fence_check_event_enabled = true;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0))
	return 0;
#endif
}

void trace_fence_check_disabled_callback(void)
{
	fence_check_event_enabled = false;
}

#if defined(SUPPORT_RGX)
/* This is a helper that calls trace_rogue_fence_update for each fence in an
 * array.
 */
void trace_rogue_fence_updates(const char *cmd, const char *dm, IMG_UINT32 ui32FWContext,
							   IMG_UINT32 ui32Offset,
							   IMG_UINT uCount,
							   PRGXFWIF_UFO_ADDR *pauiAddresses,
							   IMG_UINT32 *paui32Values)
{
	IMG_UINT i;
	for (i = 0; i < uCount; i++)
	{
		trace_rogue_fence_update(current->comm, cmd, dm, ui32FWContext, ui32Offset,
								 pauiAddresses[i].ui32Addr, PVRSRV_SYNC_CHECKPOINT_SIGNALLED);
	}
}

void trace_rogue_fence_checks(const char *cmd, const char *dm, IMG_UINT32 ui32FWContext,
							  IMG_UINT32 ui32Offset,
							  IMG_UINT uCount,
							  PRGXFWIF_UFO_ADDR *pauiAddresses,
							  IMG_UINT32 *paui32Values)
{
	IMG_UINT i;
	for (i = 0; i < uCount; i++)
	{
		trace_rogue_fence_check(current->comm, cmd, dm, ui32FWContext, ui32Offset,
							  pauiAddresses[i].ui32Addr, PVRSRV_SYNC_CHECKPOINT_SIGNALLED);
	}
}

void trace_rogue_ufo_updates(IMG_UINT64 ui64OSTimestamp,
							 IMG_UINT32 ui32FWCtx,
							 IMG_UINT32 ui32ExtJobRef,
							 IMG_UINT32 ui32IntJobRef,
							 IMG_UINT32 ui32UFOCount,
							 const RGX_HWPERF_UFO_DATA_ELEMENT *puData)
{
	IMG_UINT i;
	for (i = 0; i < ui32UFOCount; i++)
	{
		trace_rogue_ufo_update(ui64OSTimestamp, ui32FWCtx,
				ui32IntJobRef,
				ui32ExtJobRef,
				puData->sUpdate.ui32FWAddr,
				puData->sUpdate.ui32OldValue,
				puData->sUpdate.ui32NewValue);
		puData = IMG_OFFSET_ADDR(puData, sizeof(puData->sUpdate));
	}
}

void trace_rogue_ufo_checks_success(IMG_UINT64 ui64OSTimestamp,
									IMG_UINT32 ui32FWCtx,
									IMG_UINT32 ui32ExtJobRef,
									IMG_UINT32 ui32IntJobRef,
									IMG_BOOL bPrEvent,
									IMG_UINT32 ui32UFOCount,
									const RGX_HWPERF_UFO_DATA_ELEMENT *puData)
{
	IMG_UINT i;
	for (i = 0; i < ui32UFOCount; i++)
	{
		if (bPrEvent)
		{
			trace_rogue_ufo_pr_check_success(ui64OSTimestamp, ui32FWCtx,
					ui32IntJobRef, ui32ExtJobRef,
					puData->sCheckSuccess.ui32FWAddr,
					puData->sCheckSuccess.ui32Value);
		}
		else
		{
			trace_rogue_ufo_check_success(ui64OSTimestamp, ui32FWCtx,
					ui32IntJobRef, ui32ExtJobRef,
					puData->sCheckSuccess.ui32FWAddr,
					puData->sCheckSuccess.ui32Value);
		}
		puData = IMG_OFFSET_ADDR(puData, sizeof(puData->sCheckSuccess));
	}
}

void trace_rogue_ufo_checks_fail(IMG_UINT64 ui64OSTimestamp,
								 IMG_UINT32 ui32FWCtx,
								 IMG_UINT32 ui32ExtJobRef,
								 IMG_UINT32 ui32IntJobRef,
								 IMG_BOOL bPrEvent,
								 IMG_UINT32 ui32UFOCount,
								 const RGX_HWPERF_UFO_DATA_ELEMENT *puData)
{
	IMG_UINT i;
	for (i = 0; i < ui32UFOCount; i++)
	{
		if (bPrEvent)
		{
			trace_rogue_ufo_pr_check_fail(ui64OSTimestamp, ui32FWCtx,
					ui32IntJobRef, ui32ExtJobRef,
					puData->sCheckFail.ui32FWAddr,
					puData->sCheckFail.ui32Value,
					puData->sCheckFail.ui32Required);
		}
		else
		{
			trace_rogue_ufo_check_fail(ui64OSTimestamp, ui32FWCtx,
					ui32IntJobRef, ui32ExtJobRef,
					puData->sCheckFail.ui32FWAddr,
					puData->sCheckFail.ui32Value,
					puData->sCheckFail.ui32Required);
		}
		puData = IMG_OFFSET_ADDR(puData, sizeof(puData->sCheckFail));
	}
}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0))

int PVRGpuTraceEnableUfoCallbackWrapper(void)
{

#if defined(SUPPORT_RGX)
	PVRGpuTraceEnableUfoCallback();
#endif

	return 0;
}

int PVRGpuTraceEnableFirmwareActivityCallbackWrapper(void)
{

#if defined(SUPPORT_RGX)
	PVRGpuTraceEnableFirmwareActivityCallback();
#endif

	return 0;
}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)) */

void TracepointUpdateGPUMemGlobal(IMG_UINT8 ui8GPUId,
								  IMG_UINT64 ui64Size)
{
#if defined(CONFIG_TRACE_GPU_MEM) || defined(PVRSRV_ENABLE_GPU_MEM_TRACEPOINT)
	trace_gpu_mem_total(ui8GPUId, 0, ui64Size);
#endif /* defined(CONFIG_TRACE_GPU_MEM) || defined(PVRSRV_ENABLE_GPU_MEM_TRACEPOINT) */
}

void TracepointUpdateGPUMemPerProcess(IMG_UINT8 ui8GPUId,
									  IMG_UINT32 ui32Pid,
									  IMG_UINT64 ui64Size)
{
#if defined(CONFIG_TRACE_GPU_MEM) || defined(PVRSRV_ENABLE_GPU_MEM_TRACEPOINT)
	trace_gpu_mem_total(ui8GPUId, ui32Pid, ui64Size);
#endif /* defined(CONFIG_TRACE_GPU_MEM) || defined(PVRSRV_ENABLE_GPU_MEM_TRACEPOINT) */
}
