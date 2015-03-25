/*************************************************************************/ /*!
@File           pvr_gputrace.c
@Title          PVR GPU Trace module Linux implementation
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

#include "pvrsrv_error.h"
#include "srvkm.h"
#include "pvr_debug.h"
#include "pvr_debugfs.h"
#include "pvr_uaccess.h"

#include "pvr_gputrace.h"


#define CREATE_TRACE_POINTS
#include <trace/events/gpu.h>

#define KM_FTRACE_NO_PRIORITY (0)


/******************************************************************************
 Module internal implementation
******************************************************************************/

/* Circular buffer sizes, must be a power of two */
#define PVRSRV_KM_FTRACE_JOB_MAX       (512)
#define PVRSRV_KM_FTRACE_CTX_MAX        (16)

#define PVRSRV_FTRACE_JOB_FLAG_MASK     (0xFF000000)
#define PVRSRV_FTRACE_JOB_ID_MASK       (0x00FFFFFF)
#define PVRSRV_FTRACE_JOB_FLAG_ENQUEUED (0x80000000)

#define PVRSRV_FTRACE_JOB_GET_ID(pa)               ((pa)->ui32FlagsAndID & PVRSRV_FTRACE_JOB_ID_MASK)
#define PVRSRV_FTRACE_JOB_SET_ID_CLR_FLAGS(pa, id) ((pa)->ui32FlagsAndID = PVRSRV_FTRACE_JOB_ID_MASK & (id))

#define PVRSRV_FTRACE_JOB_GET_FLAGS(pa)     ((pa)->ui32FlagsAndID & PVRSRV_FTRACE_JOB_FLAG_MASK)
#define PVRSRV_FTRACE_JOB_SET_FLAGS(pa, fl) ((pa)->ui32FlagsAndID |= PVRSRV_FTRACE_JOB_FLAG_MASK & (fl))

typedef struct _PVRSRV_FTRACE_JOB_
{
	/* Job ID calculated, no need to store it. */
	IMG_UINT32 ui32FlagsAndID;
	IMG_UINT32 ui32ExtJobRef;
	IMG_UINT32 ui32IntJobRef;
} PVRSRV_FTRACE_GPU_JOB;


typedef struct _PVRSRV_FTRACE_GPU_CTX_
{
	/* Context ID is calculated, no need to store it IMG_UINT32 ui32CtxID; */
	IMG_UINT32            ui32PID;

	/* Every context has a circular buffer of jobs */
	IMG_UINT16            ui16JobWrite;		/*!< Next position to write to */
	PVRSRV_FTRACE_GPU_JOB asJobs[PVRSRV_KM_FTRACE_JOB_MAX];
} PVRSRV_FTRACE_GPU_CTX;


typedef struct _PVRSRV_FTRACE_GPU_DATA_
{
	IMG_UINT16 ui16CtxWrite;				/*!< Next position to write to */
	PVRSRV_FTRACE_GPU_CTX asFTraceContext[PVRSRV_KM_FTRACE_CTX_MAX];
} PVRSRV_FTRACE_GPU_DATA;

PVRSRV_FTRACE_GPU_DATA gsFTraceGPUData;

static void CreateJob(IMG_UINT32 ui32PID, IMG_UINT32 ui32ExtJobRef,
		IMG_UINT32 ui32IntJobRef)
{
	PVRSRV_FTRACE_GPU_CTX* psContext = IMG_NULL;
	PVRSRV_FTRACE_GPU_JOB* psJob = IMG_NULL;
	IMG_UINT32 i;

	/* Search for a previously created CTX object */
	for (i = 0; i < PVRSRV_KM_FTRACE_CTX_MAX; ++i)
	{
		if(gsFTraceGPUData.asFTraceContext[i].ui32PID == ui32PID)
		{
			psContext = &(gsFTraceGPUData.asFTraceContext[i]);
			break;
		} 
	}

	/* If not present in the CB history, create it */
	if (psContext == NULL)
	{
		/*
		  We overwrite old contexts as we don't get a "finished" indication
		  so we assume PVRSRV_KM_FTRACE_CTX_MAX is a sufficient number of
		  process contexts in use at any one time.
		*/
		i = gsFTraceGPUData.ui16CtxWrite;

		gsFTraceGPUData.asFTraceContext[i].ui32PID = ui32PID;
		gsFTraceGPUData.asFTraceContext[i].ui16JobWrite = 0;
		psContext = &(gsFTraceGPUData.asFTraceContext[i]);

		/* Advance the write position of the context CB. */
		gsFTraceGPUData.ui16CtxWrite = (i+1) & (PVRSRV_KM_FTRACE_CTX_MAX-1);
	}

	/*
	  This is just done during the first kick so it is assumed the job is not
	  in the CB of jobs yet so we create it. Clear flags.
	*/
	psJob = &(psContext->asJobs[psContext->ui16JobWrite]);
	PVRSRV_FTRACE_JOB_SET_ID_CLR_FLAGS(psJob, 1001+psContext->ui16JobWrite);
	psJob->ui32ExtJobRef = ui32ExtJobRef;
	psJob->ui32IntJobRef = ui32IntJobRef;

	/*
	  Advance the write position of the job CB. Overwrite oldest job
	  when buffer overflows
	*/
	psContext->ui16JobWrite = (psContext->ui16JobWrite + 1) & (PVRSRV_KM_FTRACE_JOB_MAX-1);
}


static PVRSRV_ERROR GetCtxAndJobID(IMG_UINT32 ui32PID,
	IMG_UINT32 ui32ExtJobRef, IMG_UINT32 ui32IntJobRef,
	IMG_UINT32 *pui32CtxID, PVRSRV_FTRACE_GPU_JOB** ppsJob)
{
	PVRSRV_FTRACE_GPU_CTX* psContext = IMG_NULL;
	IMG_UINT32 i;

	/* Search for the process context object in the CB */
	for (i = 0; i < PVRSRV_KM_FTRACE_CTX_MAX; ++i)
	{
		if(gsFTraceGPUData.asFTraceContext[i].ui32PID == ui32PID)
		{
			psContext = &(gsFTraceGPUData.asFTraceContext[i]);
			/* Derive context ID from CB index: 101..101+PVRSRV_KM_FTRACE_CTX_MAX */
			*pui32CtxID = 101+i;
			break;
		}
	}

	/* If not found, return an error, let caller trace the error */
	if (psContext == NULL)
	{
		PVR_DPF((PVR_DBG_MESSAGE,"GetCtxAndJobID: Failed to find context ID for PID %d", ui32PID));
		return PVRSRV_ERROR_PROCESS_NOT_FOUND;
	}

	/* Look for the JobID in the jobs CB */
	for(i = 0; i < PVRSRV_KM_FTRACE_JOB_MAX; ++i)
	{
		if((psContext->asJobs[i].ui32ExtJobRef == ui32ExtJobRef) &&
			(psContext->asJobs[i].ui32IntJobRef == ui32IntJobRef))
		{
			/* Derive job ID from CB index: 1001..1001+PVRSRV_KM_FTRACE_JOB_MAX */
			*ppsJob = &psContext->asJobs[i];
			return PVRSRV_OK;
		}
	}

	PVR_DPF((PVR_DBG_MESSAGE,"GetCtxAndJobID: Failed to find job ID for extJobRef %d, intJobRef %x", ui32ExtJobRef, ui32IntJobRef));
	return PVRSRV_ERROR_NOT_FOUND;
}


/* DebugFS entry for the feature's on/off file */
static struct dentry* gpsPVRDebugFSGpuTracingOnEntry = NULL;


/*
  If SUPPORT_GPUTRACE_EVENTS is defined the drive is built with support
  to route RGX HWPerf packets to the Linux FTrace mechanism. To allow
  this routing feature to be switched on and off at run-time the following
  debugfs entry is created:
  	/sys/kernel/debug/pvr/gpu_tracing_on
  To enable GPU events in the FTrace log type the following on the target:
 	echo Y > /sys/kernel/debug/pvr/gpu_tracing_on
  To disable, type:
  	echo N > /sys/kernel/debug/pvr/gpu_tracing_on

  It is also possible to enable this feature at driver load by setting the
  default application hint "EnableFTraceGPU=1" in /etc/powervr.ini.
*/

static void *GpuTracingSeqStart(struct seq_file *psSeqFile, loff_t *puiPosition)
{
	if (*puiPosition == 0)
	{
		/* We want only one entry in the sequence, one call to show() */
		return (void*)1;
	}

	return NULL;
}


static void GpuTracingSeqStop(struct seq_file *psSeqFile, void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(psSeqFile);
}


static void *GpuTracingSeqNext(struct seq_file *psSeqFile, void *pvData, loff_t *puiPosition)
{
	PVR_UNREFERENCED_PARAMETER(psSeqFile);
	return NULL;
}


static int GpuTracingSeqShow(struct seq_file *psSeqFile, void *pvData)
{
	IMG_BOOL bValue = PVRGpuTraceEnabled();

	PVR_UNREFERENCED_PARAMETER(pvData);

	seq_puts(psSeqFile, (bValue ? "Y\n" : "N\n"));
	return 0;
}


static struct seq_operations gsGpuTracingReadOps =
{
	.start = GpuTracingSeqStart,
	.stop  = GpuTracingSeqStop,
	.next  = GpuTracingSeqNext,
	.show  = GpuTracingSeqShow,
};


static IMG_INT GpuTracingSet(const IMG_CHAR *buffer, size_t count, loff_t uiPosition, void *data)
{
	IMG_CHAR cFirstChar;

	PVR_UNREFERENCED_PARAMETER(uiPosition);
	PVR_UNREFERENCED_PARAMETER(data);

	if (!count)
	{
		return -EINVAL;
	}

	if (pvr_copy_from_user(&cFirstChar, buffer, 1))
	{
		return -EFAULT;
	}

	switch (cFirstChar)
	{
		case '0':
		case 'n':
		case 'N':
		{
			PVRGpuTraceEnabledSet(IMG_FALSE);
			PVR_TRACE(("DISABLED GPU FTrace"));
			break;
		}
		case '1':
		case 'y':
		case 'Y':
		{
			PVRGpuTraceEnabledSet(IMG_TRUE);
			PVR_TRACE(("ENABLED GPU FTrace"));
			break;
		}
	}

	return count;
}


/******************************************************************************
 Module In-bound API
******************************************************************************/


void PVRGpuTraceClientWork(
		const IMG_UINT32 ui32Pid,
		const IMG_UINT32 ui32ExtJobRef,
		const IMG_UINT32 ui32IntJobRef,
		const IMG_CHAR* pszKickType)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_FTRACE_GPU_JOB* psJob;
	IMG_UINT32   ui32CtxId = 0;

	PVR_ASSERT(pszKickType);

	PVR_DPF((PVR_DBG_VERBOSE, "PVRGpuTraceClientKick(%s): PID %u, extJobRef %u, intJobRef %u", pszKickType, ui32Pid, ui32ExtJobRef, ui32IntJobRef));

	CreateJob(ui32Pid, ui32ExtJobRef, ui32IntJobRef);

	/*
	  Always create jobs for client work above but only emit the enqueue
	  trace if the feature is enabled.
	  This keeps the lookup tables up to date when the gpu_tracing_on is
	  disabled so that when it is re-enabled the packets that might be in
	  the HWPerf buffer can be decoded in the switch event processing below.
	*/
	if (PVRGpuTraceEnabled())
	{
		eError = GetCtxAndJobID(ui32Pid, ui32ExtJobRef, ui32IntJobRef, &ui32CtxId,  &psJob);
		PVR_LOGRN_IF_ERROR(eError, "GetCtxAndJobID");

		trace_gpu_job_enqueue(ui32CtxId, PVRSRV_FTRACE_JOB_GET_ID(psJob), pszKickType);

		PVRSRV_FTRACE_JOB_SET_FLAGS(psJob, PVRSRV_FTRACE_JOB_FLAG_ENQUEUED);
	}
}


void PVRGpuTraceWorkSwitch(
		IMG_UINT64 ui64HWTimestampInOSTime,
		const IMG_UINT32 ui32Pid,
		const IMG_UINT32 ui32ExtJobRef,
		const IMG_UINT32 ui32IntJobRef,
		const IMG_CHAR* pszWorkType,
		PVR_GPUTRACE_SWITCH_TYPE eSwType)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
    PVRSRV_FTRACE_GPU_JOB* psJob = IMG_NULL;
	IMG_UINT32 ui32CtxId;

	PVR_ASSERT(pszWorkType);

	eError = GetCtxAndJobID(ui32Pid, ui32ExtJobRef, ui32IntJobRef,	&ui32CtxId,  &psJob);
	PVR_LOGRN_IF_ERROR(eError, "GetCtxAndJobID");

	PVR_ASSERT(psJob);

    /*
      Only trace switch event if the job's enqueue event was traced. Necessary
	  for when the GPU tracing is disabled, apps run and re-enabled to avoid
	  orphan switch events from appearing in the trace file.
	*/
	if (PVRSRV_FTRACE_JOB_GET_FLAGS(psJob) & PVRSRV_FTRACE_JOB_FLAG_ENQUEUED)
	{
		if (eSwType == PVR_GPUTRACE_SWITCH_TYPE_END)
		{
			/* When the GPU goes idle, we need to trace a switch with a context
			 * ID of 0.
			 */
			ui32CtxId = 0;
		}

		trace_gpu_sched_switch(pszWorkType, ui64HWTimestampInOSTime,
				ui32CtxId, KM_FTRACE_NO_PRIORITY, PVRSRV_FTRACE_JOB_GET_ID(psJob));
	}
}


PVRSRV_ERROR PVRGpuTraceInit(void)
{
	return PVRDebugFSCreateEntry("gpu_tracing_on",
				      NULL,
				      &gsGpuTracingReadOps,
				      (PVRSRV_ENTRY_WRITE_FUNC *)GpuTracingSet,
				      NULL,
				      &gpsPVRDebugFSGpuTracingOnEntry);
}


void PVRGpuTraceDeInit(void)
{
	/* Can be NULL if driver startup failed */
	if (gpsPVRDebugFSGpuTracingOnEntry)
	{
		PVRDebugFSRemoveEntry(gpsPVRDebugFSGpuTracingOnEntry);
		gpsPVRDebugFSGpuTracingOnEntry = NULL;
	}
}


/******************************************************************************
 End of file (pvr_gputrace.c)
******************************************************************************/
