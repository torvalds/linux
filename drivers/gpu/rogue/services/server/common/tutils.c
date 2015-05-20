/*************************************************************************/ /*!
@File
@Title          KM server Transport Layer implementation
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Main bridge APIs for Transport Layer client functions
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
#include <stddef.h>

#if defined(LINUX)
#include "linux/random.h"
#include "linux/kthread.h"
#elif defined(_WIN32)
#if defined (UNDER_WDDM)
#include <Ntifs.h>
#endif
#elif defined(__QNXNTO__)
#include <stdlib.h>
#else
#error TLTEST.C needs random number API
#endif

#if defined(DEBUG) && !defined(PVRSRV_NEED_PVR_DPF)
#define PVRSRV_NEED_PVR_DPF
#endif

#include "img_defs.h"

#include "pvrsrv_error.h"

//#define PVR_DPF_FUNCTION_TRACE_ON 1
#undef PVR_DPF_FUNCTION_TRACE_ON

#include "pvr_debug.h"
#include "srvkm.h"
#include "power.h"

#include "allocmem.h"
#include "osfunc.h"

#include "pvrsrv.h"
#include "rgxdevice.h"
#include "lists.h"
#include "rgxdefs_km.h"
#include "rgxfwutils.h"

#include "tlintern.h"
#include "tlstream.h"
#include "tltestdefs.h"

/******************************************************************************
 * Do not include tlserver.h, as this is testing the kernelAPI, some
 * duplicate definitions
 */
PVRSRV_ERROR
TLServerTestIoctlKM(IMG_UINT32  uiCmd,
					IMG_PBYTE	uiIn1,
			 		IMG_UINT32  uiIn2,
	   			  	IMG_UINT32	*puiOut1,
			   	  	IMG_UINT32	*puiOut2);

PVRSRV_ERROR
PowMonTestIoctlKM(IMG_UINT32 uiCmd,
 				  IMG_UINT32 uiIn1,
			 	  IMG_UINT32 uiIn2,
	   			  IMG_UINT32 *puiOut1,
	   	  		  IMG_UINT32 *puiOut2);

/*****************************************************************************/

#if defined(PVR_TESTING_UTILS)

/******************************************************************************
 *
 * TL KM Test helper global variables, prototypes
 */
typedef struct _TLT_SRCNODE_
{
	struct _TLT_SRCNODE_* psNext;

	IMG_HANDLE  gTLStream;
	IMG_HANDLE  gStartTimer;
	IMG_HANDLE  gSourceTimer;

	IMG_UINT32  gSourceCount;
	IMG_UINT8*  gpuiDataPacket;

	union {
		PVR_TL_TEST_CMD_SOURCE_START_IN  gsStartIn;
		PVR_TL_TEST_CMD_STREAM_CREATE_IN gsCreateIn;
	} u;

	IMG_VOID (*gSourceWriteCB)(IMG_VOID *, IMG_UINT16);
} TLT_SRCNODE;

TLT_SRCNODE* gpsSourceStack = 0;

typedef enum
{
	TLT_OP_NONE,
	TLT_OP_CLEANUP_START_TIMER,
	TLT_OP_CLEANUP_SOURCE_STOP,
	TLT_OP_END
} TLT_OP;

typedef struct _TLT_THREAD_
{
	IMG_HANDLE	hThread;
	IMG_HANDLE  hEventList;
	IMG_HANDLE  hEvent;
	TLT_OP		eOperation;
	IMG_VOID* 	pvOpParam1;
} TLT_THREAD;

TLT_THREAD* gpsCleanUp = 0;

static PVRSRV_ERROR RegisterCleanupAction(TLT_OP eOp, IMG_VOID* pvParam1);

#define RANDOM_MOD 256
IMG_BOOL	gRandomReady = IMG_FALSE;
IMG_BYTE	gRandomStore[RANDOM_MOD];

#if defined(__QNXNTO__)
#define RANDOM_STATE_SIZE 128
static char gcRandomState[RANDOM_STATE_SIZE];
#endif

static IMG_VOID StartTimerFuncCB(IMG_VOID* p);
static IMG_VOID SourceTimerFuncCB(IMG_VOID* p);
static IMG_VOID SourceWriteFunc(IMG_VOID* p, IMG_UINT16 uiPacketSizeInBytes);
static IMG_VOID SourceWriteFunc2(IMG_VOID* p, IMG_UINT16 uiPacketSizeInBytes);

/******************************************************************************
 *
 * Power Monitoring variables
 */

static IMG_HANDLE g_hPowerMonitoringThread;
static IMG_BOOL   g_bPowMonEnable;
static IMG_UINT32 g_ui32PowMonLatencyms;
static IMG_UINT32 g_PowMonEstimate;
static IMG_UINT32 g_PowMonState;

/******************************************************************************
 *
 * TL KM Test helper routines
 */

/* Since this is done in multiple locations in this file, make it a local function. */
static PVRSRV_ERROR createStream(PVR_TL_TEST_CMD_SOURCE_START_IN** ppsIn1, 
								 TLT_SRCNODE** pSrcn)
{	
	PVR_TL_TEST_CMD_SOURCE_START_IN* psIn1 = *ppsIn1;
	TLT_SRCNODE* srcn = *pSrcn;
	PVRSRV_ERROR eError = PVRSRV_OK;
	
	PVR_DPF_ENTERED;

	/* Do not ask the server to create two streams with the same name,
	 * reuse existing one to support multiple source single stream test cases.
	 */
	if ( IMG_NULL == TLFindStreamNodeByName(psIn1->pszStreamName) )
	{
		eError = TLStreamCreate (&srcn->gTLStream,
								 psIn1->pszStreamName,
								 ((OSGetPageSize()*psIn1->uiStreamSizeInPages))
								 	- PVR_TL_TEST_STREAM_BUFFER_REDUCTION, 
								 psIn1->uiStreamCreateFlags, IMG_NULL, IMG_NULL);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF_RETURN_RC(PVRSRV_ERROR_ALREADY_EXISTS);
		}
		PVR_ASSERT(srcn->gTLStream);
	}
	else
	{
		eError = TLStreamOpen (&srcn->gTLStream, psIn1->pszStreamName);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF_RETURN_RC(PVRSRV_ERROR_ALREADY_EXISTS);
		}
		PVR_ASSERT(srcn->gTLStream);
	}
	PVR_DPF_RETURN_RC(PVRSRV_OK);
}

static PVRSRV_ERROR  TLTestCMD_SourceStart (PVR_TL_TEST_CMD_SOURCE_START_IN *psIn1,
											IMG_VOID (*pfCB)(IMG_VOID* , IMG_UINT16))
{
	PVRSRV_ERROR	eError = PVRSRV_OK;
	IMG_UINT32 		i;
	IMG_UINT16		uiPacketBufferSize;
	TLT_SRCNODE 	*srcn =0;

	PVR_DPF_ENTERED;

	srcn = OSAllocZMem(sizeof(TLT_SRCNODE));
	if (!srcn)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_OUT_OF_MEMORY);
	}

	// Remember the start parameters, initialise buffers
	srcn->u.gsStartIn = *psIn1;
	PVR_DPF((PVR_DBG_MESSAGE, "--- SourceParameters (n:%s, p:%d, i:%d, k:%d, s:%d, f:%x sd:%d ds:%d cs:%d)",
			psIn1->pszStreamName, psIn1->uiStreamSizeInPages,
			psIn1->uiInterval, psIn1->uiCallbackKicks, psIn1->uiPacketSizeInBytes,
			psIn1->uiStreamCreateFlags, psIn1->uiStartDelay,
			psIn1->bDoNotDeleteStream, psIn1->bDelayStreamCreate));

	uiPacketBufferSize = (psIn1->uiPacketSizeInBytes == 0) ? 256 : psIn1->uiPacketSizeInBytes;
	srcn->gpuiDataPacket = OSAllocMem(uiPacketBufferSize);
	if (!srcn->gpuiDataPacket)
	{
		OSFREEMEM(srcn);
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_OUT_OF_MEMORY);
	}
	for (i=0; i<uiPacketBufferSize; i++) 
	{	
	    /* Only use LSB of i to populate the data array */
		srcn->gpuiDataPacket[i] = (IMG_UINT8)i;
	}

	if (!gRandomReady)
	{
#if defined(LINUX)
		get_random_bytes(gRandomStore, sizeof(gRandomStore));
#elif defined(_WIN32)
		// IMG_UINT32 seed = ( KeQueryInterruptTime() / 10 ) & 0xFFFFFFFF;
		LARGE_INTEGER seed = KeQueryPerformanceCounter(0);

		for (i=0; i< RANDOM_MOD/sizeof(ULONG); i+=sizeof(ULONG))
		{
			/* Use just the lower byte of the random 8-bit number */
			gRandomStore[i] =  (IMG_BYTE) RtlRandomEx(&seed.u.LowPart);
		}
#elif defined(__QNXNTO__)
		initstate(time(NULL), gcRandomState, sizeof(gcRandomState));
		setstate(gcRandomState);
		for (i=0; i<RANDOM_MOD; ++i)
		{
			gRandomStore[i] = (IMG_BYTE) (random() & 0xFF);
		}
#endif
		gRandomReady = IMG_TRUE;
	}

	// Create the stream to use with the source now if deferral not enabled.
	if (!psIn1->bDelayStreamCreate)
	{
		eError = createStream(&psIn1, &srcn);
		if (eError != PVRSRV_OK)
		{
			OSFREEMEM(srcn->gpuiDataPacket);
			OSFREEMEM(srcn);
			PVR_DPF_RETURN_RC(eError);
		}
	}

	// Setup timer and start it
	i = (psIn1->uiStartDelay==0) ? psIn1->uiInterval : psIn1->uiStartDelay;
	srcn->gStartTimer = OSAddTimer(StartTimerFuncCB, (IMG_VOID *)srcn, i);
	if (!srcn->gStartTimer)
	{
		OSFREEMEM(srcn->gpuiDataPacket);
		OSFREEMEM(srcn);
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_UNABLE_TO_ADD_TIMER);
	}

	// Reset global count and enable timer
	srcn->gSourceTimer = 0;
	srcn->gSourceCount = 0;
	srcn->gSourceWriteCB = pfCB;
	eError = OSEnableTimer(srcn->gStartTimer);

	srcn->psNext = gpsSourceStack;
	gpsSourceStack = srcn;

	PVR_DPF_RETURN_RC(eError);
}

static PVRSRV_ERROR  TLTestCMD_SourceStop (PVR_TL_TEST_CMD_SOURCE_STOP_IN *psIn1)
{
	PVRSRV_ERROR 	eError = PVRSRV_OK;
	TLT_SRCNODE		*srcn = gpsSourceStack;
	TLT_SRCNODE		**srcn_prev = &gpsSourceStack;

	PVR_DPF_ENTERED;

	if (psIn1->pszStreamName[0] != '\0')
	{
		/* Find the data source in the stack of sources */
		for (; srcn != 0; srcn = srcn->psNext)
		{
			if (OSStringCompare(srcn->u.gsStartIn.pszStreamName, psIn1->pszStreamName) == 0)
			{
				break;
			}
			srcn_prev = &srcn->psNext;
		}
	}
 	/* else
	 *   Select the data source at the top of the stack.*/

	if (!srcn)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_NOT_FOUND);
	}

	PVR_DPF((PVR_DBG_MESSAGE, "--- Stopping Data Source: %s", srcn->u.gsStartIn.pszStreamName));

	/* See if we have been asked to stop before we have started */
	if (srcn->gStartTimer)
	{
		/* Stop and clean up timer */
		eError = OSDisableTimer(srcn->gStartTimer);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF_RETURN_RC(PVRSRV_ERROR_UNABLE_TO_DISABLE_TIMER);
		}

		eError = OSRemoveTimer(srcn->gStartTimer);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF_RETURN_RC(PVRSRV_ERROR_UNABLE_TO_REMOVE_TIMER);
		}
		srcn->gStartTimer = 0;
	}

	if (srcn->gSourceTimer)
	{
		/* Stop and clean up source timer */
		eError = OSDisableTimer(srcn->gSourceTimer);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF_RETURN_RC(PVRSRV_ERROR_UNABLE_TO_DISABLE_TIMER);
		}

		eError = OSRemoveTimer(srcn->gSourceTimer);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF_RETURN_RC(PVRSRV_ERROR_UNABLE_TO_REMOVE_TIMER);
		}
		srcn->gSourceTimer = 0;
	}

	if (srcn->gpuiDataPacket)
	{
		OSFREEMEM(srcn->gpuiDataPacket);
	}

	/* Cleanup transport stream AND source node if we are not to keep the
	 * stream for later destruction */
	if (psIn1->bDoNotDeleteStream)
	{
		srcn->gSourceWriteCB = 0;
	}
	else
	{
		TLStreamClose(srcn->gTLStream);
		srcn->gTLStream = 0;

		*srcn_prev = srcn->psNext;
		OSFREEMEM(srcn);
	}

	PVR_DPF_RETURN_OK;
}


// Return TRUE if the source should generate a packet, FALSE if not.
static IMG_BOOL SourceCBCommon(TLT_SRCNODE *srcn, IMG_UINT16 *puiPacketSizeInBytes)
{
	PVRSRV_ERROR	eError = PVRSRV_OK;

	srcn->gSourceCount++;
	PVR_DPF((PVR_DBG_MESSAGE, "--- Data Source CB: %s #%d", srcn->u.gsStartIn.pszStreamName, srcn->gSourceCount));

	// Work out packet size to use...
	if (srcn->u.gsStartIn.uiPacketSizeInBytes == 0)
	{
		// Random packet size
		*puiPacketSizeInBytes = (IMG_UINT16)gRandomStore[srcn->gSourceCount%RANDOM_MOD];
	}
	else
	{
		// Fixed packet size
		*puiPacketSizeInBytes = srcn->u.gsStartIn.uiPacketSizeInBytes;
	}
	if (*puiPacketSizeInBytes==0)
	{
		*puiPacketSizeInBytes=4;
	}
	PVR_ASSERT(srcn->gTLStream);

	if (srcn->u.gsStartIn.uiCallbackKicks && (srcn->gSourceCount >= srcn->u.gsStartIn.uiCallbackKicks+2U))
	{ // If there is a kick limit and we have surpassed it, clean up

		// Kick sequence should mean that this clean is only triggered
		// on a periodic (not start) kick.
		PVR_ASSERT((srcn->gStartTimer == 0) && (srcn->gSourceTimer != 0));

		eError = RegisterCleanupAction(TLT_OP_CLEANUP_SOURCE_STOP, srcn);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "RegisterCleanupAction() error %d", eError));
			return IMG_TRUE;
		}

		return IMG_FALSE;
	}
	else if (srcn->u.gsStartIn.uiCallbackKicks &&
			(srcn->gSourceCount > srcn->u.gsStartIn.uiCallbackKicks) &&
			(srcn->gSourceCount <= srcn->u.gsStartIn.uiCallbackKicks+2U))
	{ // We have reached the callback limit, do not add data
      // Delay the stream destructions for two kicks
		return IMG_FALSE;
	}
	else
	{
		return IMG_TRUE;
	}
}


static IMG_VOID StartTimerFuncCB(IMG_VOID *p)
{
	PVRSRV_ERROR	eError = PVRSRV_OK;
	IMG_UINT16		uiPacketSizeInBytes;
	TLT_SRCNODE		*srcn = (TLT_SRCNODE*)p;
	PVR_TL_TEST_CMD_SOURCE_START_IN* psIn1;

	PVR_ASSERT(srcn);

	psIn1 = &(srcn->u.gsStartIn);

	PVR_DPF((PVR_DBG_MESSAGE, "%s() srcn->gSourceCount = '%d'", __func__, srcn->gSourceCount));

	if (psIn1->bDelayStreamCreate)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s() Going to create stream '%s'", __func__, psIn1->pszStreamName));
		eError = createStream(&psIn1, &srcn);
		PVR_LOG_IF_ERROR(eError, "createStream");

		PVR_ASSERT(srcn->gTLStream);
	}
	else
	{
		// We should only get CB once but if the period is short we might get
		// invoked multiple times so silently return
		if (srcn->gTLStream == 0)
		{
			return;
		}
	}
	// Clean up start timer, we will use the source timer for our next invoke
	eError = RegisterCleanupAction(TLT_OP_CLEANUP_START_TIMER, srcn);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RegisterCleanupAction() error %d", eError));
		// No clean up not fatal so cont...
		// return;
	}

	// Attempt to perform our first kick, submit 1st packet
	// Has the kicks limit not been exceeded?
	if (SourceCBCommon(srcn, &uiPacketSizeInBytes) == IMG_TRUE)
	{
		srcn->gSourceWriteCB(srcn, uiPacketSizeInBytes);
	}

	// Always start the periodic timer as clean up happens two kicks after
	// the last data packet. This allows clients to drain the stream buffer.

	// Setup timer and start it
	srcn->gSourceTimer = OSAddTimer(SourceTimerFuncCB, (IMG_VOID *)srcn,  srcn->u.gsStartIn.uiInterval);
	if (!srcn->gSourceTimer)
	{
		PVR_DPF((PVR_DBG_ERROR, "OSAddTimer() unable to add timer"));
		return;
	}

	eError = OSEnableTimer(srcn->gSourceTimer);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "OSEnableTimer() error %d", eError));
	}

	return;
}


static IMG_VOID SourceTimerFuncCB(IMG_VOID *p)
{
	IMG_UINT16		uiPacketSizeInBytes;
	TLT_SRCNODE		*srcn = (TLT_SRCNODE*)p;

	PVR_ASSERT(srcn);

	// Has the kicks limit not been exceeded?
	if (SourceCBCommon(srcn, &uiPacketSizeInBytes) == IMG_TRUE)
	{
		srcn->gSourceWriteCB(srcn, uiPacketSizeInBytes);
	}
}

static IMG_VOID InjectEOSPacket(TLT_SRCNODE *srcn)
{
	PVRSRV_ERROR eError;

	if ( 0 != srcn->u.gsStartIn.uiEOSMarkerKicks )
	{
		if ( 0 == ( srcn->gSourceCount % srcn->u.gsStartIn.uiEOSMarkerKicks) )
		{
			eError = TLStreamMarkEOS(srcn->gTLStream);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "TLStreamMarkEOS() error %d", eError));
			}
		}
	}
}

static IMG_VOID SourceWriteFunc(IMG_VOID *p, IMG_UINT16 uiPacketSizeInBytes)
{
	PVRSRV_ERROR	eError = PVRSRV_OK;
	TLT_SRCNODE		*srcn = (TLT_SRCNODE*)p;

	PVR_ASSERT(srcn);

	// Inject EOS Markers every uiEOSMarkerKicks number of kicks.
	 InjectEOSPacket(srcn);

	// Commit packet into transport layer
	// Special case if it is one word in a packet, use global counter as data
	if (uiPacketSizeInBytes  <= 4)
	{
		eError = TLStreamWrite(srcn->gTLStream, (IMG_UINT8*)&srcn->gSourceCount, uiPacketSizeInBytes);
	}
	else
	{
		eError = TLStreamWrite(srcn->gTLStream, (IMG_UINT8*)srcn->gpuiDataPacket, uiPacketSizeInBytes);
	}

	if (eError == PVRSRV_ERROR_STREAM_FULL)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "TLStreamWrite() returned 'data dropped' error code"));
	}
	else if (eError != PVRSRV_OK)
	{
		/* Consume errors as they may occur normally in test scenarios */
		PVR_DPF((PVR_DBG_MESSAGE, "TLStreamWrite() error %d", eError));
	}
}

static IMG_VOID SourceWriteFunc2(IMG_VOID *p, IMG_UINT16 uiPacketSizeInBytes)
{
	PVRSRV_ERROR	eError = PVRSRV_OK;
	TLT_SRCNODE		*srcn = (TLT_SRCNODE*)p;
	IMG_UINT32		*pBuffer;

	PVR_ASSERT(srcn);

	// Inject EOS Markers every uiEOSMarkerKicks number of kicks.
	InjectEOSPacket(srcn);

	// Commit packet into transport layer via 2-stage API...
	eError = TLStreamReserve(srcn->gTLStream, (IMG_UINT8**) &pBuffer, uiPacketSizeInBytes);
	if (eError == PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "TLStreamReserve() pBuffer %p, uiPacketSizeInBytes %d", pBuffer, uiPacketSizeInBytes));

		OSMemCopy(pBuffer, srcn->gpuiDataPacket, uiPacketSizeInBytes);

		TLStreamCommit(srcn->gTLStream, uiPacketSizeInBytes);
	}
	else if (eError == PVRSRV_ERROR_STREAM_FULL)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "TLStreamReserve() returned 'data dropped' error code"));
	}
	else
	{
		/* Consume errors as they may occur normally in test scenarios */
		PVR_DPF((PVR_DBG_MESSAGE, "TLStreamReserve() error %d", eError));
	}
}

#if defined(PVRSRV_NEED_PVR_DPF)
// From services/server/env/linux/pvr_debug.c
// Externed here to allow us to update it to log "message" class trace
extern IMG_UINT32 gPVRDebugLevel;
#endif

static PVRSRV_ERROR  TLTestCMD_DebugLevel (IMG_UINT32 uiIn1, IMG_UINT32 *puiOut1)
{
	PVR_DPF_ENTERED;

#if defined(PVRSRV_NEED_PVR_DPF)
//	gPVRDebugLevel |= DBGPRIV_MESSAGE;

	*puiOut1 = gPVRDebugLevel;

	gPVRDebugLevel = uiIn1;

#endif

	PVR_DPF((PVR_DBG_WARNING, "TLTestCMD_DebugLevel: gPVRDebugLevel set to 0x%x", gPVRDebugLevel));

	PVR_DPF_RETURN_OK;
}


static PVRSRV_ERROR  TLTestCMD_DumpState (IMG_VOID)
{
	TL_GLOBAL_DATA	*psd = TLGGD();
	PTL_SNODE		 psn = 0;
	IMG_UINT		 count = 0;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psd);

	PVR_LOG(("--- TL_GLOBAL_DATA: %p - uiClientCnt(%d) psHead(%p) psRgxDevNode(%p)",
			psd, psd->uiClientCnt, psd->psHead, psd->psRgxDevNode));

	for (psn = psd->psHead; psn; psn = psn->psNext)
	{
		count++;
		PVR_LOG(("----- TL_SNODE[%d]: %p - psNext(%p) hDataEventObj(%p) psStream(%p) psDesc(%p)",
				count, psn, psn->psNext, psn->hDataEventObj, psn->psStream, psn->psDesc));

		if (psn->psStream)
		{
			PVR_LOG(("------- TL_STREAM[%d]: %p - psNode(%p) szName(%s) bDrop(%d) ",
				count, psn->psStream, psn->psStream->psNode, psn->psStream->szName, psn->psStream->bDrop));
			PVR_LOG(("------- TL_STREAM[%d]: %p - ui32Read(%d) ui32Write(%d) ui32Pending(%d) ui32Size(%d) ui32BufferUt(%d.%d%%)",
				count, psn->psStream, psn->psStream->ui32Read, psn->psStream->ui32Write, psn->psStream->ui32Pending, psn->psStream->ui32Size,
				((psn->psStream->ui32BufferUt*10000)/psn->psStream->ui32Size)/100,
				((psn->psStream->ui32BufferUt*10000)/psn->psStream->ui32Size)%100));
			PVR_LOG(("------- TL_STREAM[%d]: %p - ui32Buffer(%p) psStreamMemDesc(%p) sExportCookie.hPMRExportHandle(%p)",
				count, psn->psStream, psn->psStream->pbyBuffer, psn->psStream->psStreamMemDesc, psn->psStream->sExportCookie.hPMRExportHandle));
		}

		if (psn->psDesc)
		{
			PVR_LOG(("------ TL_STREAM_DESC[%d]: %p - psNode(%p) ui32Flags(%x) hDataEvent(%p)",
				count, psn->psDesc, psn->psDesc->psNode, psn->psDesc->ui32Flags, psn->psDesc->hDataEvent));
		}

	}
	PVR_DPF_RETURN_OK;
}

static PVRSRV_ERROR TLTestCMD_DumpHWPerfState (IMG_VOID)
{
	PVRSRV_DATA* psGD = PVRSRVGetPVRSRVData();
	PVRSRV_RGXDEV_INFO* psRD;
	RGXFWIF_TRACEBUF* psTB;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psGD);
	if (psGD->ui32NumDevices != 1)
	{
		PVR_LOG(("--- PVRSRV_DATA: Unable to dump, 0 or multiple devices exist!"));
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_NOT_READY);
	}

	PVR_LOG(("--- PVRSRV_DATA: %p - ui32NumDevices( %d ) apsRegisteredDevNodes[0]( %p ) apsRegisteredDevNodes[0]->pvDevice( %p )",
			psGD, psGD->ui32NumDevices,
			psGD->apsRegisteredDevNodes[0],
			psGD->apsRegisteredDevNodes[0]->pvDevice));

	psRD = psGD->apsRegisteredDevNodes[0]->pvDevice;
	PVR_LOG(("----- PVRSRV_RGXDEV_INFO: %p - psRGXFWIfTraceBuf( %p ) psRGXFWIfHWPerfBuf( %p )",
			psRD, psRD->psRGXFWIfTraceBuf,
			psRD->psRGXFWIfHWPerfBuf));

	psTB = psRD->psRGXFWIfTraceBuf;
	PVR_LOG(("-------- RGXFWIF_TRACEBUF: %p",	psTB));

	PVR_LOG(("-------- RGXFWIF_TRACEBUF: HWPerSize( %d )  HWPerfFullWatermark%%( %d.%d %% )",
			psRD->ui32RGXFWIfHWPerfBufSize,
			((psTB->ui32HWPerfUt*10000)/psRD->ui32RGXFWIfHWPerfBufSize)/100,
			((psTB->ui32HWPerfUt*10000)/psRD->ui32RGXFWIfHWPerfBufSize)%100));
	PVR_LOG(("-------- RGXFWIF_TRACEBUF: HWPerfDropCount( %d )  FirstDropOrdinal( %d ) LastDropOrdinal( %d )",
			psTB->ui32HWPerfDropCount, psTB->ui32FirstDropOrdinal, psTB->ui32LastDropOrdinal));
	PVR_LOG(("-------- RGXFWIF_TRACEBUF: HWPerfRIdx( %d )  HWPerfWIdx( %d )  HWPerfWrapCount( %d )",
			psTB->ui32HWPerfRIdx, psTB->ui32HWPerfWIdx,
			psTB->ui32HWPerfWrapCount));

	PVR_DPF_RETURN_OK;
}

static IMG_PVOID  TLTestCMD_FindRGXDevNode(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	if (psDeviceNode->sDevId.eDeviceType == PVRSRV_DEVICE_TYPE_RGX)
	{
		return psDeviceNode;
	}
	return NULL;
}

static PVRSRV_ERROR TLTestCMD_FlushHWPerfFWBuf (IMG_VOID)
{
	PVRSRV_ERROR        rc = PVRSRV_OK;
	PVRSRV_DEVICE_NODE* psDevNode = 0;
	PVRSRV_DATA*        psGlobalSrvData = PVRSRVGetPVRSRVData();

	PVR_DPF_ENTERED;

	PVR_DPF((PVR_DBG_WARNING, "TLTestCMD_FlushHWPerfFWBuf: ..."));

	/* Search for the RGX device node */
	psDevNode = List_PVRSRV_DEVICE_NODE_Any(psGlobalSrvData->psDeviceNodeList, TLTestCMD_FindRGXDevNode);
	if (psDevNode == 0)
	{
		/* Device node was not found */
		PVR_DPF((PVR_DBG_ERROR, "Failed to fin the RGX device node"));
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_NOT_FOUND);
	}

	rc = psDevNode->pfnServiceHWPerf(psDevNode);

#if defined(NO_HARDWARE)
	PVR_DPF((PVR_DBG_WARNING, "TLTestCMD_FlushHWPerfFWBuf is a no-op on NO_HARDWARE!"));
#endif

	PVR_DPF_RETURN_RC(rc);
}

static PVRSRV_ERROR TLTestCMD_SignalPE (IMG_UINT32* psIn1)
{
	PVRSRV_DEVICE_NODE* psDevNode = 0;
	PVRSRV_RGXDEV_INFO* psDevInfo = 0;
	PVRSRV_DATA*        psGlobalSrvData = PVRSRVGetPVRSRVData();
	IMG_UINT32          ui32Tmp;

	PVR_DPF_ENTERED;

	PVR_DPF((PVR_DBG_WARNING, "TLTestCMD_SignalPE: ( %d ) ( %d )",
			psIn1[1], psIn1[3]));

	/* Search for the RGX device node */
	psDevNode = List_PVRSRV_DEVICE_NODE_Any(psGlobalSrvData->psDeviceNodeList, TLTestCMD_FindRGXDevNode);
	if (psDevNode == 0)
	{
		/* Device node was not found */
		PVR_DPF((PVR_DBG_ERROR, "Failed to fin the RGX device node"));
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_NOT_FOUND);
	}
	psDevInfo = psDevNode->pvDevice;

	OSWriteHWReg32(psDevInfo->pvRegsBaseKM, psIn1[0], psIn1[1]);
	OSWriteHWReg32(psDevInfo->pvRegsBaseKM, psIn1[2], psIn1[3]);

	/* kick GPIO */
	ui32Tmp = OSReadHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_EVENT_STATUS);
	OSWriteHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_EVENT_STATUS, ui32Tmp|RGX_CR_EVENT_STATUS_GPIO_REQ_EN);

	/* ensure the registers goes through before continuing */
	OSMemoryBarrier();

	/* ack the output from the FW (using the event_status this way could lead to corrupted event_status, 
	   as there is a race condition, but there is no other way to check the behaviour, so use this) */
	ui32Tmp = OSReadHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_EVENT_STATUS);
	OSWriteHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_EVENT_STATUS, ui32Tmp|RGX_CR_EVENT_STATUS_GPIO_ACK_EN);
	OSMemoryBarrier();

#if defined(NO_HARDWARE)
	PVR_DPF((PVR_DBG_WARNING, "TLTestCMD_SignalPwrEst is a no-op on NO_HARDWARE!"));
#endif

	PVR_DPF_RETURN_OK;
}

extern IMG_VOID PDumpCommonDumpState(IMG_VOID);

static PVRSRV_ERROR TLTestCMD_DumpPDumpState (IMG_VOID)
{
	PVR_DPF_ENTERED;

#if defined(PDUMP)
	PDumpCommonDumpState();
#endif

	PVR_DPF_RETURN_OK;
}



static PVRSRV_ERROR RegisterCleanupAction(TLT_OP eOp, IMG_VOID *pvParam1)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_DPF_ENTERED;

	if (gpsCleanUp == 0)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INIT_FAILURE);
	}

	if (gpsCleanUp->eOperation != TLT_OP_NONE)
	{
		PVR_DPF((PVR_DBG_ERROR, "RegisterCleanupAction not ready, pending cleanup %d, requested cleanup %d", gpsCleanUp->eOperation, eOp));
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_NOT_READY);
	}

	gpsCleanUp->eOperation = eOp;
	gpsCleanUp->pvOpParam1 = pvParam1;

	eError = OSEventObjectSignal(gpsCleanUp->hEventList);

	PVR_DPF_RETURN_RC(eError);
}

/* Called at driver unloaded/shutdown.
 */
static PVRSRV_ERROR TLDeInitialiseCleanupTestThread (IMG_VOID)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	TLT_THREAD*	 psThreadData;

	PVR_DPF_ENTERED;

	/* Has the thread already died and cleaned up? */
	if (gpsCleanUp == 0)
	{
		PVR_DPF_RETURN_OK;
	}
	
	psThreadData = gpsCleanUp;

	PVR_DPF((PVR_DBG_MESSAGE, "------- DeInit CleanUp: %p, %p, %p, %d, %p", psThreadData->hThread,
			psThreadData->hEventList, psThreadData->hEvent, psThreadData->eOperation, psThreadData->pvOpParam1));

	eError = RegisterCleanupAction(TLT_OP_END, 0);
	PVR_LOG_IF_ERROR(eError, "RegisterCleanupAction");

	/* Prevent any further registrations between now an when the thread exits */
	gpsCleanUp = NULL;

	eError = OSThreadDestroy(psThreadData->hThread);
	PVR_LOG_IF_ERROR(eError, "OSThreadDestroy");

	/* At this point we expect the clean up thread's event loop to break
	 * so that it can perform the clean up as needed and free the thread data.
	 */

	PVR_DPF_RETURN_RC(eError);
}

#define TEN_SECOND_TIMEOUT 10000

static IMG_VOID CleanupThread(IMG_PVOID pvData)
{
	PVRSRV_ERROR 	eError = PVRSRV_OK;
	TLT_THREAD		*psThreadData = pvData;
	TLT_SRCNODE		*psSrcNode;
	PVR_TL_TEST_CMD_SOURCE_STOP_IN  sStopParams;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psThreadData);
 	memset(&sStopParams, 0x00, sizeof(sStopParams));

	while (psThreadData->eOperation != TLT_OP_END)
	{
		OSEventObjectWaitTimeout(psThreadData->hEvent, TEN_SECOND_TIMEOUT);

#if defined(LINUX) && defined(__KERNEL__)
		if (kthread_should_stop()) 
		{
			break;
		}
#endif

		// On detecting bad state, exit thread as a safety measure
		if (gpsCleanUp == 0) 
		{
			break;
		}

		switch (psThreadData->eOperation)
		{
		case TLT_OP_CLEANUP_START_TIMER:
			// Stop and clean up start timer
			PVR_DPF((PVR_DBG_MESSAGE, "TLT CleanupThread: source start cleanup"));
			psSrcNode = psThreadData->pvOpParam1;
			eError = OSDisableTimer(psSrcNode->gStartTimer);
			PVR_LOG_IF_ERROR(eError, "OSDisableTimer");
			eError = OSRemoveTimer(psSrcNode->gStartTimer);
			PVR_LOG_IF_ERROR(eError, "OSRemoveTimer");
			psSrcNode->gStartTimer = 0;

			/* Loop and wait again */
			psThreadData->eOperation = TLT_OP_NONE;
			psThreadData->pvOpParam1 = 0;
			break;

		case TLT_OP_CLEANUP_SOURCE_STOP:
			// Stop and clean up data source
			PVR_DPF((PVR_DBG_MESSAGE, "TLT CleanupThread: source stop cleanup"));
			psSrcNode = psThreadData->pvOpParam1;
			OSStringCopy(sStopParams.pszStreamName, psSrcNode->u.gsStartIn.pszStreamName);
			sStopParams.bDoNotDeleteStream = psSrcNode->u.gsStartIn.bDoNotDeleteStream;
			eError = TLTestCMD_SourceStop (&sStopParams);
			PVR_LOG_IF_ERROR(eError, "TLTestCMD_SourceStop");
			psSrcNode = 0;

			/* Loop and wait again */
			psThreadData->eOperation = TLT_OP_NONE;
			psThreadData->pvOpParam1 = 0;
			break;

		case TLT_OP_END: /* Used on WDDM platform */
			PVR_DPF((PVR_DBG_MESSAGE, "TLT CleanupThread: ending..."));
			break;

		case TLT_OP_NONE:
		default: /* Do nothing... but wait again */
			PVR_DPF((PVR_DBG_MESSAGE, "TLT CleanupThread: waiting..."));
			break;
		}

	}

	/* We exit event loop when the thread must end signalled either via our
	 * OP_END (WDDM) or a kill signal (Linux).
	 * We are responsible here for cleaning up of thread resources.
	 * gpsCleanUp==0 already from the TLDeInitialiseCleanupTestThread() call.
	 */
	PVR_ASSERT(gpsCleanUp==0);

	eError = OSEventObjectClose(psThreadData->hEvent);
	psThreadData->hEvent = 0;
	PVR_LOG_IF_ERROR(eError, "OSEventObjectClose");

	eError = OSEventObjectDestroy(psThreadData->hEventList);
	psThreadData->hEventList = 0;
	PVR_LOG_IF_ERROR(eError, "OSEventObjectDestroy");

	OSFREEMEM(psThreadData);

	PVR_DPF_RETURN;
}

static PVRSRV_ERROR InitialiseCleanupThread (IMG_VOID)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_DPF_ENTERED;

	/* If the TLTEST clean up thread is still running exit early
	 * as we don't want to reinitialise it.
	 */
	if (gpsCleanUp)
	{
		return eError;
	}

	gpsCleanUp = OSAllocZMem(sizeof(TLT_THREAD));
	if (!gpsCleanUp)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_OUT_OF_MEMORY);
	}

	eError = OSEventObjectCreate("TLT_CleanUpEL", &gpsCleanUp->hEventList);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF_RETURN_RC(eError);
	}

	eError = OSEventObjectOpen(gpsCleanUp->hEventList, &gpsCleanUp->hEvent);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF_RETURN_RC(eError);
	}

	eError = OSThreadCreate(&gpsCleanUp->hThread, "TLT_CleanUpT" , CleanupThread, gpsCleanUp);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF_RETURN_RC(eError);
	}

	PVR_DPF_RETURN_OK;
}


static PVRSRV_ERROR  TLTestCMD_StreamCreate(PVR_TL_TEST_CMD_STREAM_CREATE_IN *psIn1)
{
	PVRSRV_ERROR	eError = PVRSRV_OK;
	TLT_SRCNODE		*srcn = 0;


	PVR_DPF_ENTERED;

	srcn = OSAllocZMem(sizeof(TLT_SRCNODE));
	if (!srcn)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_OUT_OF_MEMORY);
	}
	srcn->u.gsCreateIn = *psIn1;

	eError = TLStreamCreate (&srcn->gTLStream, psIn1->pszStreamName,
			                 ((OSGetPageSize()*psIn1->uiStreamSizeInPages)) - PVR_TL_TEST_STREAM_BUFFER_REDUCTION,
							 psIn1->uiStreamCreateFlags, IMG_NULL, IMG_NULL);
	if (eError != PVRSRV_OK)
	{
		OSFREEMEM(srcn);
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_ALREADY_EXISTS);
	}
	PVR_ASSERT(srcn->gTLStream);

	srcn->psNext = gpsSourceStack;
	gpsSourceStack = srcn;

	PVR_DPF_RETURN_RC(eError);
}


static PVRSRV_ERROR  TLTestCMD_StreamClose(PVR_TL_TEST_CMD_STREAM_NAME_IN* psIn1)
{
	TLT_SRCNODE *srcn = gpsSourceStack;
	TLT_SRCNODE **srcn_prev = &gpsSourceStack;


	PVR_DPF_ENTERED;

	if (psIn1->pszStreamName[0] != '\0')
	{
		/* Find the data source in the stack of sources */
		for (; srcn != 0; srcn = srcn->psNext)
		{
			if (OSStringCompare(srcn->u.gsCreateIn.pszStreamName, psIn1->pszStreamName) == 0)
			{
				break;
			}
			srcn_prev = &srcn->psNext;
		}
	}
 	/* else
	 *   Select the data source at the top of the stack.*/

	if (!srcn)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_NOT_FOUND);
	}

	{// If the stream is still referenced, we do not want to close it yet.
		PTL_STREAM psTmp = (PTL_STREAM) srcn->gTLStream;
		if ( psTmp->uiRefCount > 1 )
		{
			TLStreamClose(srcn->gTLStream);
			PVR_DPF_RETURN_RC(PVRSRV_OK);
		}
	}
	if (srcn->gStartTimer || srcn->gSourceTimer || srcn->gpuiDataPacket || srcn->gSourceWriteCB)
	{
		PVR_DPF((PVR_DBG_WARNING, "TLUtils Warning Unable to close stream %p, %p, %p, %p", srcn->gStartTimer, srcn->gSourceTimer, srcn->gpuiDataPacket, srcn->gSourceWriteCB));

		PVR_DPF_RETURN_RC(PVRSRV_ERROR_NOT_READY);
	}

	// Cleanup transport stream
	TLStreamClose(srcn->gTLStream);
	srcn->gTLStream = 0;

	*srcn_prev = srcn->psNext;
	OSFREEMEM(srcn);

	PVR_DPF_RETURN_OK;
}

static PVRSRV_ERROR  TLTestCMD_StreamOpen(PVR_TL_TEST_CMD_STREAM_NAME_IN *psIn1)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_HANDLE hSrcn;

	PVR_DPF_ENTERED;

	PVR_ASSERT( psIn1 != IMG_NULL );

	eError = TLStreamOpen (&hSrcn, psIn1->pszStreamName);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_HANDLE_NOT_FOUND);
	}

	PVR_DPF_RETURN_RC(eError);
}

static PVRSRV_ERROR TLTestCMD_SetPwrState(IMG_UINT32 *uiIn1)
{
	PVRSRV_ERROR eError;

	eError = PVRSRVPowerLock();
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"TLTestCMD_SetPwrState: Failed to acquire power lock"));
		return eError;
	}

	if (*uiIn1 == PVR_TL_TEST_PWR_STATE_ON)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "TLTestCMD_SetPwrState: Turning GPU Power ON."));
		eError = PVRSRVSetDevicePowerStateKM(0,
											 PVRSRV_DEV_POWER_STATE_ON,
											 IMG_TRUE);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"TLTestCMD_SetPwrState: Failed to set power state."));
			return eError;
		}
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "TLTestCMD_SetPwrState: Turning GPU Power OFF."));
		eError = PVRSRVSetDevicePowerStateKM(0,
											 PVRSRV_DEV_POWER_STATE_OFF,
											 IMG_TRUE);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"TLTestCMD_SetPwrState: Failed to set power state."));
			return eError;
		}
	}

	PVRSRVPowerUnlock();

	return PVRSRV_OK;
}

static PVRSRV_ERROR TLTestCMD_GetPwrState(IMG_UINT32 *puiOut1)
{
	PVRSRV_DEV_POWER_STATE ePwrState;
	PVRSRV_ERROR eError;

	eError = PVRSRVPowerLock();
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"TLTestCMD_GetPwrState: Failed to acquire power lock"));
		return eError;
	}

	PVRSRVGetDevicePowerState(0, &ePwrState);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"TLTestCMD_GetPwrState: Could not obtain power state."));
		return eError;
	}

	PVRSRVPowerUnlock();

	*puiOut1 = ePwrState;

	return PVRSRV_OK;
}

static PVRSRV_ERROR TLTestCMD_SetDwtWakeupCounter(IMG_UINT32 *uiIn1)
{
	PVRSRVGetPVRSRVData()->ui32DevicesWdWakeupCounter = *uiIn1;

	return PVRSRV_OK;
}

static PVRSRV_ERROR TLTestCMD_GetDwtWakeupCounter(IMG_UINT32 *puiOut1)
{
	*puiOut1 = PVRSRVGetPVRSRVData()->ui32DevicesWdWakeupCounter;

	return PVRSRV_OK;
}

PVRSRV_ERROR
TLServerTestIoctlKM(IMG_UINT32 	uiCmd,
					IMG_PBYTE 	uiIn1,
					IMG_UINT32  uiIn2,
					IMG_UINT32	*puiOut1,
					IMG_UINT32	*puiOut2)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVR_DPF_ENTERED;

	PVR_UNREFERENCED_PARAMETER(puiOut2);

	PVR_DPF((PVR_DBG_MESSAGE, "--- Processing Test IOCTL command %d", uiCmd));
	PVR_DPF((PVR_DBG_MESSAGE, "--- In Arguments: %p, %d", uiIn1, uiIn2));

	switch (uiCmd)
	{
	case PVR_TL_TEST_CMD_SOURCE_START:
		eError = TLTestCMD_SourceStart((PVR_TL_TEST_CMD_SOURCE_START_IN*)uiIn1, SourceWriteFunc);
		PVR_LOG_IF_ERROR(eError, "TLTestCMD_SourceStart");
		/* Initialise the test clean up thread */
		if (eError == PVRSRV_OK) //&& (gpsCleanUp == 0))
		{
			eError = InitialiseCleanupThread();
			PVR_LOGR_IF_ERROR(eError, "InitialiseCleanupThread");
		}
		break;

	case PVR_TL_TEST_CMD_SOURCE_STOP:
		eError = TLTestCMD_SourceStop((PVR_TL_TEST_CMD_SOURCE_STOP_IN*)uiIn1);
		PVR_LOG_IF_ERROR(eError, "TLTestCMD_SourceStop");
		break;

	case PVR_TL_TEST_CMD_SOURCE_START2:
		eError = TLTestCMD_SourceStart((PVR_TL_TEST_CMD_SOURCE_START_IN*)uiIn1, SourceWriteFunc2);
		PVR_LOG_IF_ERROR(eError, "TLTestCMD_SourceStart");
		/* Initialise the test clean up thread */
		if (eError == PVRSRV_OK)//&& (gpsCleanUp == 0))
		{
			eError = InitialiseCleanupThread();
			PVR_LOGR_IF_ERROR(eError, "InitialiseCleanupThread");
		}
		break;

	case PVR_TL_TEST_CMD_DEBUG_LEVEL:
		eError = TLTestCMD_DebugLevel(*(IMG_UINT32*)uiIn1, puiOut1);
		PVR_LOG_IF_ERROR(eError, "TLTestCMD_DebugLevel");
		break;

	case PVR_TL_TEST_CMD_DUMP_TL_STATE:
		eError = TLTestCMD_DumpState();
		PVR_LOG_IF_ERROR(eError, "TLTestCMD_DumpState");
		break;

	case PVR_TL_TEST_CMD_STREAM_CREATE:
		eError = TLTestCMD_StreamCreate((PVR_TL_TEST_CMD_STREAM_CREATE_IN*)uiIn1);
		break;

	case PVR_TL_TEST_CMD_STREAM_CLOSE:
		eError = TLTestCMD_StreamClose((PVR_TL_TEST_CMD_STREAM_NAME_IN*)uiIn1);
		break;

	case PVR_TL_TEST_CMD_STREAM_OPEN:
		eError = TLTestCMD_StreamOpen((PVR_TL_TEST_CMD_STREAM_NAME_IN*)uiIn1);
		break;

	case PVR_TL_TEST_CMD_DUMP_HWPERF_STATE:
		eError = TLTestCMD_DumpHWPerfState();
		PVR_LOG_IF_ERROR(eError, "TLTestCMD_DumpHWPerfState");
		break;

	case PVR_TL_TEST_CMD_FLUSH_HWPERF_FWBUF:
		eError = TLTestCMD_FlushHWPerfFWBuf();
		PVR_LOG_IF_ERROR(eError, "TLTestCMD_FlushHWPerfFWBuf");
		break;

	case PVR_TL_TEST_CMD_SIGNAL_PE:
		eError = TLTestCMD_SignalPE((IMG_UINT32*)uiIn1);
		PVR_LOG_IF_ERROR(eError, "TLTestCMD_SignalPE");
		break;

	case PVR_TL_TEST_CMD_DUMP_PDUMP_STATE:
		eError = TLTestCMD_DumpPDumpState();
		PVR_LOG_IF_ERROR(eError, "TLTestCMD_DumpPDumpState");
		break;

	case PVR_TL_TEST_CMD_SET_PWR_STATE:
		eError = TLTestCMD_SetPwrState((IMG_UINT32*)uiIn1);
		break;

	case PVR_TL_TEST_CMD_GET_PWR_STATE:
		eError = TLTestCMD_GetPwrState(puiOut1);
		break;

	case PVR_TL_TEST_CMD_SET_DWT_PWR_CHANGE_COUNTER:
		eError = TLTestCMD_SetDwtWakeupCounter((IMG_UINT32*)uiIn1);
		break;

	case PVR_TL_TEST_CMD_GET_DWT_PWR_CHANGE_COUNTER:
		eError = TLTestCMD_GetDwtWakeupCounter(puiOut1);
		break;

	default:
		// Do nothing...
		break;
	}

	PVR_DPF_RETURN_RC(eError);
}

PVRSRV_ERROR
PowMonTestIoctlKM(IMG_UINT32  uiCmd,
				  IMG_UINT32  uiIn1,
				  IMG_UINT32  uiIn2,
				  IMG_UINT32  *puiOut1,
				  IMG_UINT32  *puiOut2)
{
	PVR_DPF_ENTERED;
	PVR_UNREFERENCED_PARAMETER(uiIn2);

	switch (uiCmd)
	{
		case 1:
		{
			if ((puiOut1 == IMG_NULL) || (puiOut2 == IMG_NULL))
			{
				PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
			}

			/* retrieve last measurement */
			*puiOut1 = g_PowMonEstimate;
			*puiOut2 = g_PowMonState;
			break;
		}
		case 2:
		{
			if (uiIn1 == 0x0)
			{
				PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
			}

			/* set new delay between measurement requests */
			g_ui32PowMonLatencyms = uiIn1;
			break;
		}
		default:
		{
			PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
		}
	}

	PVR_DPF_RETURN_RC(PVRSRV_OK);
}

static PVRSRV_ERROR PowMonEstimateRequest(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 *pui32PowEstValue, IMG_UINT32 *pui32PowEstState)
{
	IMG_UINT64 ui64Timer;
	PVRSRV_ERROR eError;

	/* read timer */
	ui64Timer = RGXReadHWTimerReg(psDevInfo);

	OSWriteHWReg32(psDevInfo->pvRegsBaseKM, 0x6320U, 0x1);

	/* send gpio_input req */
#if !defined (SUPPORT_POWMON_WO_GPIO_PIN)
	/* potentially dangerous to write directly to this reg */
	OSWriteHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_EVENT_STATUS, 0x1000); 
#else
	OSWriteHWReg32(psDevInfo->pvRegsBaseKM, 0x140U, 0x00000080);
	OSWriteHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_MTS_SCHEDULE, 0x20);
#endif

	/* poll on input req to be cleared */

	/* Poll on RGX_CR_GPIO_OUTPUT_REQ[0] = 1 */
	eError = 
	   PVRSRVPollForValueKM((IMG_UINT32*) (((IMG_UINT8*) psDevInfo->pvRegsBaseKM) + 0x148U), 0x1, 0x1);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, 
		         "PowMonEstimateRequest: Poll gpio output req failed (rc_timer 0x%llx) e:%d", 
				 ui64Timer, 
				 g_bPowMonEnable));
		return eError;
	}

	/* read gpio_output_data */
	*pui32PowEstState = OSReadHWReg32(psDevInfo->pvRegsBaseKM, 0x140U);

	/* read power estimate result */
	*pui32PowEstValue = OSReadHWReg32(psDevInfo->pvRegsBaseKM, 0x6328U);

#if !defined (SUPPORT_POWMON_WO_GPIO_PIN)
	/* Set RGX_CR_EVENT_STATUS[13] at MMADR offset 0x100130 to acknowledge the power estimation status and result have been absorbed. */
	OSWriteHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_EVENT_STATUS, 0x2000);
#else
	OSWriteHWReg32(psDevInfo->pvRegsBaseKM, 0x140U, 0x0);
#endif

	/* POLL on output ack  to be cleared */
	eError = 
#if !defined (SUPPORT_POWMON_WO_GPIO_PIN)
	  PVRSRVPollForValueKM((IMG_UINT32*)(((IMG_UINT8*)psDevInfo->pvRegsBaseKM) + RGX_CR_EVENT_STATUS), 0x0, 0x2000);
#else		
	  PVRSRVPollForValueKM((IMG_UINT32*)(((IMG_UINT8*)psDevInfo->pvRegsBaseKM) + 0x148U), 0x0, 0x1);
#endif			
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, 
					"PowMonEstimateRequest: Poll on output ack failed (rgx_timer 0x%llx, value %x, state %d). e:%d", 
					ui64Timer,
					*pui32PowEstValue,
					*pui32PowEstState,
					g_bPowMonEnable));
		return eError;
	}

	return PVRSRV_OK;
}

static IMG_VOID PowMonTestThread(IMG_PVOID pvData)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = pvData;
	PVRSRV_ERROR       eError;
	IMG_UINT32         ui32DeviceIndex = psDevInfo->psDeviceNode->sDevId.ui32DeviceIndex;

#if defined(NO_HARDWARE)
	PVR_LOG(("No Hardware driver has no Power Monitoring testing functionality"));
	return;
#endif

#if defined(SUPPORT_POWMON_WO_GPIO_PIN)
	PVR_LOG(("PowMon with SUPPORT_POWMON_WO_GPIO_PIN"));
#endif

	PVR_LOG(("PowMonTestThread Wait to start"));

	/* Wait for FW to start */
	for (;;)
	{
		if (psDevInfo->bFirmwareInitialised)
		{
			break;
		}

		if (!g_bPowMonEnable)
		{
			PVR_LOG(("PowMonTestThread exit"));
			return;
		}

		OSSleepms(500);
	}

	PVR_LOG(("PowMonTestThread Running"));
	while (g_bPowMonEnable)
	{
		PVRSRV_DEV_POWER_STATE ePowerState;

		OSSleepms(g_ui32PowMonLatencyms);

		eError = PVRSRVPowerLock();
		if (eError != PVRSRV_OK)
		{
			PVR_LOG_ERROR(eError, "PVRSRVPowerLock");
			continue;
		}

		PVRSRVGetDevicePowerState(ui32DeviceIndex, &ePowerState);

		if (ePowerState == PVRSRV_DEV_POWER_STATE_ON)
		{
			/* Request a power monitoring estimate */
			eError = PowMonEstimateRequest(psDevInfo, &g_PowMonEstimate, &g_PowMonState);

			/* reset on invalid */
			if (g_PowMonState == 0x3)
			{
				g_PowMonEstimate = 0x0;
			}
			else if ((g_PowMonState != 0x1) && (g_PowMonState != 0x2))
			{
				PVR_DPF((PVR_DBG_ERROR, "PowMonEstimateRequest returned an invalid PowMonEstState: %d", g_PowMonState));
				g_bPowMonEnable = IMG_FALSE;
			}
		}
		else
		{
			g_PowMonEstimate = 0x0;
			g_PowMonState = 0x0;

		}

		PVRSRVPowerUnlock();
	}

	PVR_LOG(("PowMonTestThread Stopped"));
}

static PVRSRV_ERROR
PowMonInit(IMG_VOID)
{
	PVRSRV_ERROR eError;
	PVRSRV_DEVICE_NODE *psDeviceNode;

	/* find RGX specific device data */
	eError = PVRSRVAcquireDeviceDataKM(0, PVRSRV_DEVICE_TYPE_RGX, (IMG_HANDLE*) &psDeviceNode);
	PVR_LOGR_IF_ERROR(eError, "PVRSRVAcquireDeviceDataKM");
	
	/* initial state */
	g_bPowMonEnable = IMG_TRUE;
	g_ui32PowMonLatencyms = 1;
	g_PowMonEstimate = 0;
	g_PowMonState = 0x0;

	/* Create a thread which is used to test power monitoring */
	eError = OSThreadCreate(&g_hPowerMonitoringThread,
							"pvr_powmon_test",
							PowMonTestThread,
							psDeviceNode->pvDevice);
	PVR_LOGR_IF_ERROR(eError, "OSThreadCreate");

	return PVRSRV_OK;
}

static PVRSRV_ERROR
PowMonDeinit(IMG_VOID)
{
	if (g_hPowerMonitoringThread)
	{
		g_bPowMonEnable = IMG_FALSE;
		OSThreadDestroy(g_hPowerMonitoringThread);
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR
TLSourceDeInit (IMG_VOID)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVR_TL_TEST_CMD_SOURCE_STOP_IN sStopParams;
	
	/*
	 * Stop and close all the streams that may have been started, but stopped
	 */
	
	/* To call TLTestCMD_SourceStop with no stream-name so as to stop the streams in stackwise order of creation */
	memset (&sStopParams, 0x00, sizeof (sStopParams));	

	do
	{
		PVR_LOG_IF_ERROR (eError, "TLTestCMD_SourceStop");
		eError = TLTestCMD_SourceStop (&sStopParams);
	} while (eError != PVRSRV_ERROR_NOT_FOUND);	/* PVRSRV_ERROR_NOT_FOUND implies there are no more streams in sourceStack */

	return PVRSRV_OK;
}

PVRSRV_ERROR
TUtilsInit(IMG_VOID)
{

	PowMonInit();

	PVR_DPF_RETURN_OK;
}

PVRSRV_ERROR
TUtilsDeinit(IMG_VOID)
{
	PowMonDeinit();
	
	TLDeInitialiseCleanupTestThread();
	
	TLSourceDeInit ();
	
	PVR_DPF_RETURN_OK;
}
	

#else /* PVR_TESTING_UTILS */

PVRSRV_ERROR
TLServerTestIoctlKM(IMG_UINT32  uiCmd,
 					IMG_PBYTE	uiIn1,
			 		IMG_UINT32  uiIn2,
	   			  	IMG_UINT32	*puiOut1,
	   	  			IMG_UINT32	*puiOut2)
{
	PVR_DPF_ENTERED;

	PVR_UNREFERENCED_PARAMETER(uiCmd);
	PVR_UNREFERENCED_PARAMETER(uiIn1);
	PVR_UNREFERENCED_PARAMETER(uiIn2);
	PVR_UNREFERENCED_PARAMETER(puiOut1);
	PVR_UNREFERENCED_PARAMETER(puiOut2);

	PVR_DPF_RETURN_RC(PVRSRV_ERROR_NOT_SUPPORTED);
}

PVRSRV_ERROR
PowMonTestIoctlKM(IMG_UINT32 uiCmd,
 				  IMG_UINT32 uiIn1,
			 	  IMG_UINT32 uiIn2,
	   			  IMG_UINT32 *puiOut1,
	   	  		  IMG_UINT32 *puiOut2)
{
	PVR_DPF_ENTERED;

	PVR_UNREFERENCED_PARAMETER(uiCmd);
	PVR_UNREFERENCED_PARAMETER(uiIn1);
	PVR_UNREFERENCED_PARAMETER(uiIn2);
	PVR_UNREFERENCED_PARAMETER(puiOut1);
	PVR_UNREFERENCED_PARAMETER(puiOut2);

	PVR_DPF_RETURN_RC(PVRSRV_ERROR_NOT_SUPPORTED);
}
#endif
