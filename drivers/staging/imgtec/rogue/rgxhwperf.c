/*************************************************************************/ /*!
@File
@Title          RGX HW Performance implementation
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX HW Performance implementation
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

//#define PVR_DPF_FUNCTION_TRACE_ON 1
#undef PVR_DPF_FUNCTION_TRACE_ON

#include "pvr_debug.h"
#include "pvr_hwperf.h"
#include "pvr_notifier.h"
#include "osfunc.h"
#include "allocmem.h"

#include "pvrsrv.h"
#include "pvrsrv_tlstreams.h"
#include "pvrsrv_tlcommon.h"
#include "tlclient.h"
#include "tlstream.h"

#include "rgx_hwperf_km.h"
#include "rgxhwperf.h"
#include "rgxapi_km.h"
#include "rgxfwutils.h"
#include "rgxtimecorr.h"
#include "devicemem.h"
#include "devicemem_pdump.h"
#include "pdump_km.h"
#include "pvrsrv_apphint.h"

#if defined(SUPPORT_GPUTRACE_EVENTS)
#include "pvr_gputrace.h"
#endif

/* Defined to ensure HWPerf packets are not delayed */
#define SUPPORT_TL_PROODUCER_CALLBACK 1


/******************************************************************************
 *
 *****************************************************************************/


/*
	RGXHWPerfCopyDataL1toL2
*/
static IMG_UINT32 RGXHWPerfCopyDataL1toL2(IMG_HANDLE hHWPerfStream,
										  IMG_BYTE   *pbFwBuffer, 
										  IMG_UINT32 ui32BytesExp)
{
  	IMG_BYTE 	 *pbL2Buffer;
	IMG_UINT32   ui32L2BufFree;
	IMG_UINT32   ui32BytesCopied = 0;
	IMG_UINT32   ui32BytesExpMin = RGX_HWPERF_GET_SIZE(RGX_HWPERF_GET_PACKET(pbFwBuffer));
	PVRSRV_ERROR eError;

/* HWPERF_MISR_FUNC_DEBUG enables debug code for investigating HWPerf issues */
#ifdef HWPERF_MISR_FUNC_DEBUG
	static IMG_UINT32 gui32Ordinal = IMG_UINT32_MAX;
#endif

	PVR_DPF_ENTERED;

#ifdef HWPERF_MISR_FUNC_DEBUG
	PVR_DPF((PVR_DBG_VERBOSE, "EVENTS to copy from 0x%p length:%05d",
							  pbFwBuffer, ui32BytesExp));
#endif

#ifdef HWPERF_MISR_FUNC_DEBUG
	{
		/* Check the incoming buffer of data has not lost any packets */
 	 	IMG_BYTE *pbFwBufferIter = pbFwBuffer;
 	 	IMG_BYTE *pbFwBufferEnd = pbFwBuffer+ui32BytesExp;
	 	do
		{
			RGX_HWPERF_V2_PACKET_HDR *asCurPos = RGX_HWPERF_GET_PACKET(pbFwBufferIter);
			IMG_UINT32 ui32CurOrdinal = asCurPos->ui32Ordinal;
			if (gui32Ordinal != IMG_UINT32_MAX)
			{
				if ((gui32Ordinal+1) != ui32CurOrdinal)
				{
					if (gui32Ordinal < ui32CurOrdinal)
					{
						PVR_DPF((PVR_DBG_WARNING,
								 "HWPerf [%p] packets lost (%u packets) between ordinal %u...%u",
								 pbFwBufferIter,
								 ui32CurOrdinal - gui32Ordinal - 1,
								 gui32Ordinal,
								 ui32CurOrdinal));
					}
					else
					{
						PVR_DPF((PVR_DBG_WARNING,
								 "HWPerf [%p] packet ordinal out of sequence last: %u, current: %u",
								  pbFwBufferIter,
								  gui32Ordinal,
								  ui32CurOrdinal));
					}
				}
			}
			gui32Ordinal = asCurPos->ui32Ordinal;
			pbFwBufferIter += RGX_HWPERF_GET_SIZE(asCurPos);
		} while( pbFwBufferIter < pbFwBufferEnd );
	}
#endif

	/* Try submitting all data in one TL packet. */
	eError = TLStreamReserve2( hHWPerfStream, 
							   &pbL2Buffer, 
							   (size_t)ui32BytesExp, ui32BytesExpMin,
							   &ui32L2BufFree);
	if ( eError == PVRSRV_OK )
	{
		OSDeviceMemCopy( pbL2Buffer, pbFwBuffer, (size_t)ui32BytesExp );
		eError = TLStreamCommit(hHWPerfStream, (size_t)ui32BytesExp);
		if ( eError != PVRSRV_OK )
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "TLStreamCommit() failed (%d) in %s(), unable to copy packet from L1 to L2 buffer",
					 eError, __func__));
			goto e0;
		}
		/* Data were successfully written */
		ui32BytesCopied = ui32BytesExp;
	}
	else if (eError == PVRSRV_ERROR_STREAM_RESERVE_TOO_BIG)
	{
		/* There was not enough space for all data, copy as much as possible */
		IMG_UINT32                sizeSum  = 0;
		RGX_PHWPERF_V2_PACKET_HDR psCurPkt = RGX_HWPERF_GET_PACKET(pbFwBuffer);

		PVR_DPF((PVR_DBG_MESSAGE, "Unable to reserve space (%d) in host buffer on first attempt, remaining free space: %d", ui32BytesExp, ui32L2BufFree));

		/* Traverse the array to find how many packets will fit in the available space. */
		while ( sizeSum < ui32BytesExp  &&
				sizeSum + RGX_HWPERF_GET_SIZE(psCurPkt) < ui32L2BufFree )
		{
			sizeSum += RGX_HWPERF_GET_SIZE(psCurPkt);
			psCurPkt = RGX_HWPERF_GET_NEXT_PACKET(psCurPkt);
		}

		if ( 0 != sizeSum )
		{
			eError = TLStreamReserve( hHWPerfStream, &pbL2Buffer, (size_t)sizeSum);

			if ( eError == PVRSRV_OK )
			{
				OSDeviceMemCopy( pbL2Buffer, pbFwBuffer, (size_t)sizeSum );
				eError = TLStreamCommit(hHWPerfStream, (size_t)sizeSum);
				if ( eError != PVRSRV_OK )
				{
					PVR_DPF((PVR_DBG_ERROR,
							 "TLStreamCommit() failed (%d) in %s(), unable to copy packet from L1 to L2 buffer",
							 eError, __func__));
					goto e0;
				}
				/* sizeSum bytes of hwperf packets have been successfully written */
				ui32BytesCopied = sizeSum;
			}
			else if ( PVRSRV_ERROR_STREAM_RESERVE_TOO_BIG == eError )
			{
				PVR_DPF((PVR_DBG_WARNING, "Can not write HWPerf packet into host buffer, check data in case of packet loss, remaining free space: %d", ui32L2BufFree));
			}
		}
		else
		{
			PVR_DPF((PVR_DBG_MESSAGE, "Can not find space in host buffer, check data in case of packet loss, remaining free space: %d", ui32L2BufFree));
		}
	}
	if ( PVRSRV_OK != eError && /*  Some other error occurred */
	     PVRSRV_ERROR_STREAM_RESERVE_TOO_BIG != eError ) /* Full error handled by caller, we returning the copied bytes count to caller*/
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "HWPerf enabled: Unexpected Error ( %d ) while copying FW buffer to TL buffer.",
				 eError));
	}

e0:
	/* Return the remaining packets left to be transported. */
	PVR_DPF_RETURN_VAL(ui32BytesCopied);
}


static INLINE IMG_UINT32 RGXHWPerfAdvanceRIdx(
		const IMG_UINT32 ui32BufSize,
		const IMG_UINT32 ui32Pos,
		const IMG_UINT32 ui32Size)
{
	return (  ui32Pos + ui32Size < ui32BufSize ? ui32Pos + ui32Size : 0 );
}


/*
	RGXHWPerfDataStore
*/
static IMG_UINT32 RGXHWPerfDataStore(PVRSRV_RGXDEV_INFO	*psDevInfo)
{
	RGXFWIF_TRACEBUF	*psRGXFWIfTraceBufCtl = psDevInfo->psRGXFWIfTraceBuf;
	IMG_BYTE*			psHwPerfInfo = psDevInfo->psRGXFWIfHWPerfBuf;
	IMG_UINT32			ui32SrcRIdx, ui32SrcWIdx, ui32SrcWrapCount;
	IMG_UINT32			ui32BytesExp = 0, ui32BytesCopied = 0, ui32BytesCopiedSum = 0;
#ifdef HWPERF_MISR_FUNC_DEBUG
	IMG_UINT32			ui32BytesExpSum = 0;
#endif
	
	PVR_DPF_ENTERED;

	/* Caller should check this member is valid before calling */
	PVR_ASSERT(psDevInfo->hHWPerfStream);
	
 	/* Get a copy of the current
	 *   read (first packet to read) 
	 *   write (empty location for the next write to be inserted) 
	 *   WrapCount (size in bytes of the buffer at or past end)
	 * indexes of the FW buffer */
	ui32SrcRIdx = psRGXFWIfTraceBufCtl->ui32HWPerfRIdx;
	ui32SrcWIdx = psRGXFWIfTraceBufCtl->ui32HWPerfWIdx;
	OSMemoryBarrier();
	ui32SrcWrapCount = psRGXFWIfTraceBufCtl->ui32HWPerfWrapCount;

	/* Is there any data in the buffer not yet retrieved? */
	if ( ui32SrcRIdx != ui32SrcWIdx )
	{
		PVR_DPF((PVR_DBG_MESSAGE, "RGXHWPerfDataStore EVENTS found srcRIdx:%d srcWIdx: %d ", ui32SrcRIdx, ui32SrcWIdx));

		/* Is the write position higher than the read position? */
		if ( ui32SrcWIdx > ui32SrcRIdx )
		{
			/* Yes, buffer has not wrapped */
			ui32BytesExp  = ui32SrcWIdx - ui32SrcRIdx;
#ifdef HWPERF_MISR_FUNC_DEBUG
			ui32BytesExpSum += ui32BytesExp;
#endif
			ui32BytesCopied = RGXHWPerfCopyDataL1toL2(psDevInfo->hHWPerfStream,
													  psHwPerfInfo + ui32SrcRIdx,
													  ui32BytesExp);

			/* Advance the read index and the free bytes counter by the number
			 * of bytes transported. Items will be left in buffer if not all data
			 * could be transported. Exit to allow buffer to drain. */
			psRGXFWIfTraceBufCtl->ui32HWPerfRIdx = RGXHWPerfAdvanceRIdx(
					psDevInfo->ui32RGXFWIfHWPerfBufSize, ui32SrcRIdx,
					ui32BytesCopied);

			ui32BytesCopiedSum += ui32BytesCopied;
		}
		/* No, buffer has wrapped and write position is behind read position */
		else
		{
			/* Byte count equal to 
			 *     number of bytes from read position to the end of the buffer, 
			 *   + data in the extra space in the end of the buffer. */
			ui32BytesExp = ui32SrcWrapCount - ui32SrcRIdx;

#ifdef HWPERF_MISR_FUNC_DEBUG
			ui32BytesExpSum += ui32BytesExp;
#endif
			/* Attempt to transfer the packets to the TL stream buffer */
			ui32BytesCopied = RGXHWPerfCopyDataL1toL2(psDevInfo->hHWPerfStream,
													  psHwPerfInfo + ui32SrcRIdx,
													  ui32BytesExp);

			/* Advance read index as before and Update the local copy of the
			 * read index as it might be used in the last if branch*/
			ui32SrcRIdx = RGXHWPerfAdvanceRIdx(
					psDevInfo->ui32RGXFWIfHWPerfBufSize, ui32SrcRIdx,
					ui32BytesCopied);

			/* Update Wrap Count */
			if ( ui32SrcRIdx == 0)
			{
				psRGXFWIfTraceBufCtl->ui32HWPerfWrapCount = psDevInfo->ui32RGXFWIfHWPerfBufSize;
			}
			psRGXFWIfTraceBufCtl->ui32HWPerfRIdx = ui32SrcRIdx;

			ui32BytesCopiedSum += ui32BytesCopied;

			/* If all the data in the end of the array was copied, try copying
			 * wrapped data in the beginning of the array, assuming there is
			 * any and the RIdx was wrapped. */
			if (   (ui32BytesCopied == ui32BytesExp)
			    && (ui32SrcWIdx > 0) 
				&& (ui32SrcRIdx == 0) )
			{
				ui32BytesExp = ui32SrcWIdx;
#ifdef HWPERF_MISR_FUNC_DEBUG
				ui32BytesExpSum += ui32BytesExp;
#endif
				ui32BytesCopied = RGXHWPerfCopyDataL1toL2(psDevInfo->hHWPerfStream,
														  psHwPerfInfo,
														  ui32BytesExp);
				/* Advance the FW buffer read position. */
				psRGXFWIfTraceBufCtl->ui32HWPerfRIdx = RGXHWPerfAdvanceRIdx(
						psDevInfo->ui32RGXFWIfHWPerfBufSize, ui32SrcRIdx,
						ui32BytesCopied);

				ui32BytesCopiedSum += ui32BytesCopied;
			}
		}
#ifdef HWPERF_MISR_FUNC_DEBUG
		if (ui32BytesCopiedSum != ui32BytesExpSum)
		{
			PVR_DPF((PVR_DBG_WARNING, "RGXHWPerfDataStore: FW L1 RIdx:%u. Not all bytes copied to L2: %u bytes out of %u expected", psRGXFWIfTraceBufCtl->ui32HWPerfRIdx, ui32BytesCopiedSum, ui32BytesExpSum));
		}
#endif

	}
	else
	{
		PVR_DPF((PVR_DBG_VERBOSE, "RGXHWPerfDataStore NO EVENTS to transport"));
	}

	PVR_DPF_RETURN_VAL(ui32BytesCopiedSum);
}


PVRSRV_ERROR RGXHWPerfDataStoreCB(PVRSRV_DEVICE_NODE *psDevInfo)
{
	PVRSRV_ERROR		eError = PVRSRV_OK;
	PVRSRV_RGXDEV_INFO* psRgxDevInfo;
	IMG_UINT32          ui32BytesCopied;


	PVR_DPF_ENTERED;

	PVR_ASSERT(psDevInfo);
	psRgxDevInfo = psDevInfo->pvDevice;

	/* Keep HWPerf resource init check and use of
	 * resources atomic, they may not be freed during use 
	 */
	OSLockAcquire(psRgxDevInfo->hHWPerfLock);
	
	if (psRgxDevInfo->hHWPerfStream != 0)
	{
		ui32BytesCopied = RGXHWPerfDataStore(psRgxDevInfo);
		if ( ui32BytesCopied )
		{	/* Signal consumers that packets may be available to read when
			 * running from a HW kick, not when called by client APP thread
			 * via the transport layer CB as this can lead to stream
			 * corruption.*/
			eError = TLStreamSync(psRgxDevInfo->hHWPerfStream);
			PVR_ASSERT(eError == PVRSRV_OK);
		}
		else
		{
			PVR_DPF((PVR_DBG_MESSAGE, "RGXHWPerfDataStoreCB: Zero bytes copied"));
			RGXDEBUG_PRINT_IRQ_COUNT(psRgxDevInfo);
		}
	}

	OSLockRelease(psRgxDevInfo->hHWPerfLock);

	PVR_DPF_RETURN_OK;
}


/* Not currently supported by default */
#if defined(SUPPORT_TL_PROODUCER_CALLBACK)
static PVRSRV_ERROR RGXHWPerfTLCB(IMG_HANDLE hStream,
		IMG_UINT32 ui32ReqOp, IMG_UINT32* ui32Resp, void* pvUser)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_RGXDEV_INFO* psRgxDevInfo = (PVRSRV_RGXDEV_INFO*)pvUser;

	PVR_UNREFERENCED_PARAMETER(hStream);
	PVR_UNREFERENCED_PARAMETER(ui32Resp);

	PVR_ASSERT(psRgxDevInfo);

	switch (ui32ReqOp)
	{
	case TL_SOURCECB_OP_CLIENT_EOS:
		/* Keep HWPerf resource init check and use of
		 * resources atomic, they may not be freed during use 
		 */
		OSLockAcquire(psRgxDevInfo->hHWPerfLock);
		if (psRgxDevInfo->hHWPerfStream != 0)
		{
			(void) RGXHWPerfDataStore(psRgxDevInfo);
		}
		OSLockRelease(psRgxDevInfo->hHWPerfLock);
		break;

	default:
		break;
	}

	return eError;
}
#endif


/* References to key objects to allow kernel-side behaviour to function
 * e.g. FTrace and KM interface to HWPerf.
 */
static PVRSRV_DEVICE_NODE* gpsRgxDevNode = NULL;
static PVRSRV_RGXDEV_INFO* gpsRgxDevInfo = NULL;

static void RGXHWPerfL1BufferDeinit(void)
{
	if (gpsRgxDevInfo && gpsRgxDevInfo->psRGXFWIfHWPerfBufMemDesc)
	{
		if (gpsRgxDevInfo->psRGXFWIfHWPerfBuf != NULL)
		{
			DevmemReleaseCpuVirtAddr(gpsRgxDevInfo->psRGXFWIfHWPerfBufMemDesc);
			gpsRgxDevInfo->psRGXFWIfHWPerfBuf = NULL;
		}
		DevmemFwFree(gpsRgxDevInfo, gpsRgxDevInfo->psRGXFWIfHWPerfBufMemDesc);
		gpsRgxDevInfo->psRGXFWIfHWPerfBufMemDesc = NULL;
	}
}

/*************************************************************************/ /*!
@Function       RGXHWPerfInit

@Description    Called during driver init for initialization of HWPerf module
		in the Rogue device driver. This function keeps allocated
		only the minimal necessary resources, which are required for
		functioning of HWPerf server module.

@Input          psRgxDevInfo	RGX Device Node

@Return		PVRSRV_ERROR
*/ /**************************************************************************/
PVRSRV_ERROR RGXHWPerfInit(PVRSRV_DEVICE_NODE *psRgxDevNode)
{
	PVRSRV_ERROR eError;

	PVR_DPF_ENTERED;

	/* expecting a valid device node */
	PVR_ASSERT(psRgxDevNode);

	/* Keep RGX device's reference for later use as this parameter is
	 * optional on later calls to HWPerf server module */
	gpsRgxDevNode = psRgxDevNode;
	gpsRgxDevInfo = psRgxDevNode->pvDevice;

	/* Create a lock for HWPerf server module used for serializing, L1 to L2
	 * copy calls (e.g. in case of TL producer callback) and L1, L2 resource
	 * allocation */
	eError = OSLockCreate(&gpsRgxDevInfo->hHWPerfLock, LOCK_TYPE_PASSIVE);
	PVR_LOGR_IF_ERROR(eError, "OSLockCreate");

	/* avoid uninitialised data */
	gpsRgxDevInfo->hHWPerfStream = 0;
	gpsRgxDevInfo->psRGXFWIfHWPerfBufMemDesc = NULL;

	PVR_DPF_RETURN_OK;
}

/*************************************************************************/ /*!
@Function       RGXHWPerfIsInitRequired

@Description    Returns true if the HWperf firmware buffer (L1 buffer) and host
                driver TL buffer (L2 buffer) are not already allocated. Caller
                must possess hHWPerfLock lock  before calling this
                function so the state tested is not inconsistent.

@Return         IMG_BOOL	Whether initialization (allocation) is required
*/ /**************************************************************************/
static INLINE IMG_BOOL RGXHWPerfIsInitRequired(void)
{
	PVR_ASSERT(OSLockIsLocked(gpsRgxDevInfo->hHWPerfLock));

#if !defined (NO_HARDWARE)
	/* Both L1 and L2 buffers are required (for HWPerf functioning) on driver
	 * built for actual hardware (TC, EMU, etc.)
	 */
	if (gpsRgxDevInfo->hHWPerfStream == 0)
	{
		/* The allocation API (RGXHWPerfInitOnDemandResources) allocates
		 * device memory for both L1 and L2 without any checks. Hence,
		 * either both should be allocated or both be NULL.
		 *
		 * In-case this changes in future (for e.g. a situation where one
		 * of the 2 buffers is already allocated and other is required),
		 * add required checks before allocation calls to avoid memory leaks.
		 */
		PVR_ASSERT(gpsRgxDevInfo->psRGXFWIfHWPerfBufMemDesc == NULL);
		return IMG_TRUE;
	}
	PVR_ASSERT(gpsRgxDevInfo->psRGXFWIfHWPerfBufMemDesc != NULL);
#else
	/* On a NO-HW driver L2 is not allocated. So, no point in checking its
	 * allocation */
	if (gpsRgxDevInfo->psRGXFWIfHWPerfBufMemDesc == NULL)
	{
		return IMG_TRUE;
	}
#endif
	return IMG_FALSE;
}

/*************************************************************************/ /*!
@Function       RGXHWPerfInitOnDemandResources

@Description    This function allocates the HWperf firmware buffer (L1 buffer)
                and host driver TL buffer (L2 buffer) if HWPerf is enabled at
                driver load time. Otherwise, these buffers are allocated
                on-demand as and when required. Caller
                must possess hHWPerfLock lock  before calling this
                function so the state tested is not inconsistent if called
                outside of driver initialisation.

@Return         PVRSRV_ERROR
*/ /**************************************************************************/
PVRSRV_ERROR RGXHWPerfInitOnDemandResources(void)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32L2BufferSize;
	DEVMEM_FLAGS_T uiMemAllocFlags;

	PVR_DPF_ENTERED;

	/* Create the L1 HWPerf buffer on demand */
	uiMemAllocFlags = PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT)
				| PVRSRV_MEMALLOCFLAG_GPU_READABLE
				| PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE
				| PVRSRV_MEMALLOCFLAG_CPU_READABLE
				| PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE
				| PVRSRV_MEMALLOCFLAG_UNCACHED
				#if defined(PDUMP)
				| PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC
				#endif
				;

	/* Allocate HWPerf FW L1 buffer */
	eError = DevmemFwAllocate(gpsRgxDevInfo,
							  gpsRgxDevInfo->ui32RGXFWIfHWPerfBufSize+RGXFW_HWPERF_L1_PADDING_DEFAULT,
							  uiMemAllocFlags,
							  "FwHWPerfBuffer",
							  &gpsRgxDevInfo->psRGXFWIfHWPerfBufMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to allocate kernel fw hwperf buffer (%u)",
					__FUNCTION__, eError));
		goto e0;
	}

	/* Expecting the RuntimeCfg structure is mapped into CPU virtual memory.
	 * Also, make sure the FW address is not already set */
	PVR_ASSERT(gpsRgxDevInfo->psRGXFWIfRuntimeCfg && gpsRgxDevInfo->psRGXFWIfRuntimeCfg->sHWPerfBuf.ui32Addr == 0x0);

	/* Meta cached flag removed from this allocation as it was found
	 * FW performance was better without it. */
	RGXSetFirmwareAddress(&gpsRgxDevInfo->psRGXFWIfRuntimeCfg->sHWPerfBuf,
						  gpsRgxDevInfo->psRGXFWIfHWPerfBufMemDesc,
						  0, RFW_FWADDR_NOREF_FLAG);

	eError = DevmemAcquireCpuVirtAddr(gpsRgxDevInfo->psRGXFWIfHWPerfBufMemDesc,
									  (void**)&gpsRgxDevInfo->psRGXFWIfHWPerfBuf);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to acquire kernel hwperf buffer (%u)",
					 __FUNCTION__, eError));
		goto e0;
	}

	/* On NO-HW driver, there is no MISR installed to copy data from L1 to L2. Hence,
	 * L2 buffer is not allocated */
#if !defined(NO_HARDWARE)
	/* Host L2 HWPERF buffer size in bytes must be bigger than the L1 buffer
	 * accessed by the FW. The MISR may try to write one packet the size of the L1
	 * buffer in some scenarios. When logging is enabled in the MISR, it can be seen
	 * if the L2 buffer hits a full condition. The closer in size the L2 and L1 buffers
	 * are the more chance of this happening.
	 * Size chosen to allow MISR to write an L1 sized packet and for the client
	 * application/daemon to drain a L1 sized packet e.g. ~ 1.5*L1.
	 */
	ui32L2BufferSize = gpsRgxDevInfo->ui32RGXFWIfHWPerfBufSize + 
	                       (gpsRgxDevInfo->ui32RGXFWIfHWPerfBufSize>>1);
	eError = TLStreamCreate(&gpsRgxDevInfo->hHWPerfStream, PVRSRV_TL_HWPERF_RGX_FW_STREAM,
					ui32L2BufferSize, 
					TL_FLAG_RESERVE_DROP_NEWER | TL_FLAG_NO_SIGNAL_ON_COMMIT, 
					NULL, NULL,
#if !defined(SUPPORT_TL_PROODUCER_CALLBACK)
					NULL, NULL
#else
					/* Not enabled  by default */
					RGXHWPerfTLCB, gpsRgxDevInfo
#endif
					);
	PVR_LOGG_IF_ERROR(eError, "TLStreamCreate", e1);
#else /* defined (NO_HARDWARE) */
	PVR_UNREFERENCED_PARAMETER(ui32L2BufferSize);
	PVR_UNREFERENCED_PARAMETER(RGXHWPerfTLCB);
ui32L2BufferSize = 0;
#endif 

	PVR_DPF((PVR_DBG_MESSAGE, "HWPerf buffer size in bytes: L1: %d  L2: %d",
			gpsRgxDevInfo->ui32RGXFWIfHWPerfBufSize, ui32L2BufferSize));

	PVR_DPF_RETURN_OK;

#if !defined(NO_HARDWARE)
e1: /* L2 buffer initialisation failures */
	gpsRgxDevInfo->hHWPerfStream = NULL;
#endif
e0: /* L1 buffer initialisation failures */
	RGXHWPerfL1BufferDeinit();
	
	PVR_DPF_RETURN_RC(eError);
}


void RGXHWPerfDeinit(void)
{
	PVR_DPF_ENTERED;

	/* Clean up the L2 buffer stream object if allocated */
	if (gpsRgxDevInfo && gpsRgxDevInfo->hHWPerfStream)
	{
		TLStreamClose(gpsRgxDevInfo->hHWPerfStream);
		gpsRgxDevInfo->hHWPerfStream = NULL;
	}

	/* Cleanup L1 buffer resources */
	RGXHWPerfL1BufferDeinit();

	/* Cleanup the HWPerf server module lock resource */
	if (gpsRgxDevInfo && gpsRgxDevInfo->hHWPerfLock)
	{
		OSLockDestroy(gpsRgxDevInfo->hHWPerfLock);
		gpsRgxDevInfo->hHWPerfLock = NULL;
	}

	PVR_DPF_RETURN;
}


/******************************************************************************
 * RGX HW Performance Profiling Server API(s)
 *****************************************************************************/

static PVRSRV_ERROR RGXHWPerfCtrlFwBuffer(const PVRSRV_DEVICE_NODE *psDeviceNode,
                                          IMG_BOOL bToggle,
                                          IMG_UINT64 ui64Mask)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_RGXDEV_INFO* psDevice = psDeviceNode->pvDevice;
	RGXFWIF_KCCB_CMD sKccbCmd;

	/* If this method is being used whether to enable or disable
	 * then the hwperf buffers (host and FW) are likely to be needed
	 * eventually so create them, also helps unit testing. Buffers
	 * allocated on demand to reduce RAM foot print on systems not
	 * needing HWPerf resources.
	 * Obtain lock first, test and init if required. */
	OSLockAcquire(psDevice->hHWPerfLock);

	if (!psDevice->bFirmwareInitialised)
	{
		gpsRgxDevInfo->ui64HWPerfFilter = ui64Mask; // at least set filter
		eError = PVRSRV_ERROR_NOT_INITIALISED;

		PVR_DPF((PVR_DBG_ERROR, "HWPerf has NOT been initialised yet."
		        " Mask has been SET to (%llx)", (long long) ui64Mask));

		goto unlock_and_return;
	}

	if (RGXHWPerfIsInitRequired())
	{
		eError = RGXHWPerfInitOnDemandResources();
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Initialisation of on-demand HWPerfFW "
			        "resources failed", __func__));
			goto unlock_and_return;
		}
	}

	/* Unlock here as no further HWPerf resources are used below that would be
	 * affected if freed by another thread */
	OSLockRelease(psDevice->hHWPerfLock);

	/* Return if the filter is the same */
	if (!bToggle && gpsRgxDevInfo->ui64HWPerfFilter == ui64Mask)
		goto return_;

	/* Prepare command parameters ... */
	sKccbCmd.eCmdType = RGXFWIF_KCCB_CMD_HWPERF_UPDATE_CONFIG;
	sKccbCmd.uCmdData.sHWPerfCtrl.bToggle = bToggle;
	sKccbCmd.uCmdData.sHWPerfCtrl.ui64Mask = ui64Mask;

	/* Ask the FW to carry out the HWPerf configuration command */
	eError = RGXScheduleCommand(psDeviceNode->pvDevice,	RGXFWIF_DM_GP, 
								&sKccbCmd, sizeof(sKccbCmd), 0, PDUMP_FLAGS_CONTINUOUS);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to set new HWPerfFW filter in "
				"firmware (error = %d)", __func__, eError));
		goto return_;
	}

	gpsRgxDevInfo->ui64HWPerfFilter = bToggle ?
	        gpsRgxDevInfo->ui64HWPerfFilter ^ ui64Mask : ui64Mask;

	/* Wait for FW to complete */
	eError = RGXWaitForFWOp(psDeviceNode->pvDevice, RGXFWIF_DM_GP,
	                        psDeviceNode->psSyncPrim, PDUMP_FLAGS_CONTINUOUS);
	PVR_LOGG_IF_ERROR(eError, "RGXWaitForFWOp", return_);

#if defined(DEBUG)
	if (bToggle)
	{
		PVR_DPF((PVR_DBG_WARNING, "HWPerfFW events (%llx) have been TOGGLED",
		        ui64Mask));
	}
	else
	{
		PVR_DPF((PVR_DBG_WARNING, "HWPerfFW mask has been SET to (%llx)",
		        ui64Mask));
	}
#endif

	return PVRSRV_OK;

unlock_and_return:
	OSLockRelease(psDevice->hHWPerfLock);

return_:
	return eError;
}

static PVRSRV_ERROR RGXHWPerfCtrlHostBuffer(const PVRSRV_DEVICE_NODE *psDeviceNode,
                                            IMG_BOOL bToggle,
                                            IMG_UINT32 ui32Mask)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_RGXDEV_INFO* psDevice = psDeviceNode->pvDevice;

	OSLockAcquire(psDevice->hLockHWPerfHostStream);
	if (psDevice->hHWPerfHostStream == NULL)
	{
		eError = RGXHWPerfHostInitOnDemandResources();
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Initialization of on-demand HWPerfHost"
			        " resources failed", __FUNCTION__));
			OSLockRelease(psDevice->hLockHWPerfHostStream);
			return eError;
		}
	}

	psDevice->ui32HWPerfHostFilter = bToggle ?
	        psDevice->ui32HWPerfHostFilter ^ ui32Mask : ui32Mask;
	OSLockRelease(psDevice->hLockHWPerfHostStream);

#if defined(DEBUG)
	if (bToggle)
	{
		PVR_DPF((PVR_DBG_WARNING, "HWPerfHost events (%x) have been TOGGLED",
		        ui32Mask));
	}
	else
	{
		PVR_DPF((PVR_DBG_WARNING, "HWPerfHost mask has been SET to (%x)",
		        ui32Mask));
	}
#endif

	return PVRSRV_OK;
}

/*
	PVRSRVRGXCtrlHWPerfKM
*/
PVRSRV_ERROR PVRSRVRGXCtrlHWPerfKM(
	CONNECTION_DATA         *psConnection,
	PVRSRV_DEVICE_NODE      *psDeviceNode,
	RGX_HWPERF_STREAM_ID     eStreamId,
	IMG_BOOL                 bToggle,
	IMG_UINT64               ui64Mask)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);

	PVR_DPF_ENTERED;
	PVR_ASSERT(psDeviceNode);

	if (eStreamId == RGX_HWPERF_STREAM_ID0_FW)
	{
		return RGXHWPerfCtrlFwBuffer(psDeviceNode, bToggle, ui64Mask);
	}
	else if (eStreamId == RGX_HWPERF_STREAM_ID1_HOST)
	{
		return RGXHWPerfCtrlHostBuffer(psDeviceNode, bToggle, (IMG_UINT32) ui64Mask);
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVRGXCtrlHWPerfKM: Unknown stream id."));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	PVR_DPF_RETURN_OK;
}

/*
	AppHint interfaces
*/
static
PVRSRV_ERROR RGXHWPerfSetFwFilter(const PVRSRV_DEVICE_NODE *psDeviceNode,
                                  const void *psPrivate,
                                  IMG_UINT64 ui64Value)
{
	PVR_UNREFERENCED_PARAMETER(psPrivate);
	return RGXHWPerfCtrlFwBuffer(psDeviceNode, IMG_FALSE, ui64Value);
}

static
PVRSRV_ERROR RGXHWPerfReadFwFilter(const PVRSRV_DEVICE_NODE *psDeviceNode,
                                   const void *psPrivate,
                                   IMG_UINT64 *pui64Value)
{
	PVRSRV_RGXDEV_INFO *psDevice;

	PVR_UNREFERENCED_PARAMETER(psPrivate);

	if (!psDeviceNode || !psDeviceNode->pvDevice)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevice = psDeviceNode->pvDevice;
	*pui64Value = psDevice->ui64HWPerfFilter;
	return PVRSRV_OK;
}

static
PVRSRV_ERROR RGXHWPerfSetHostFilter(const PVRSRV_DEVICE_NODE *psDeviceNode,
                                    const void *psPrivate,
                                    IMG_UINT32 ui32Value)
{
	PVR_UNREFERENCED_PARAMETER(psPrivate);
	return RGXHWPerfCtrlHostBuffer(psDeviceNode, IMG_FALSE, ui32Value);
}

static
PVRSRV_ERROR RGXHWPerfReadHostFilter(const PVRSRV_DEVICE_NODE *psDeviceNode,
                                     const void *psPrivate,
                                     IMG_UINT32 *pui32Value)
{
	PVRSRV_RGXDEV_INFO *psDevice;

	PVR_UNREFERENCED_PARAMETER(psPrivate);

	if (!psDeviceNode || !psDeviceNode->pvDevice)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevice = psDeviceNode->pvDevice;
	*pui32Value = psDevice->ui32HWPerfHostFilter;
	return PVRSRV_OK;
}

void RGXHWPerfInitAppHintCallbacks(const PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRVAppHintRegisterHandlersUINT64(APPHINT_ID_HWPerfFWFilter,
	                                    RGXHWPerfReadFwFilter,
	                                    RGXHWPerfSetFwFilter,
	                                    psDeviceNode,
	                                    NULL);
	PVRSRVAppHintRegisterHandlersUINT32(APPHINT_ID_HWPerfHostFilter,
	                                    RGXHWPerfReadHostFilter,
	                                    RGXHWPerfSetHostFilter,
	                                    psDeviceNode,
	                                    NULL);
}

/*
	PVRSRVRGXEnableHWPerfCountersKM
*/
PVRSRV_ERROR PVRSRVRGXConfigEnableHWPerfCountersKM(
	CONNECTION_DATA          * psConnection,
	PVRSRV_DEVICE_NODE       * psDeviceNode,
	IMG_UINT32                 ui32ArrayLen,
	RGX_HWPERF_CONFIG_CNTBLK * psBlockConfigs)
{
	PVRSRV_ERROR 		eError = PVRSRV_OK;
	RGXFWIF_KCCB_CMD 	sKccbCmd;
	DEVMEM_MEMDESC*		psFwBlkConfigsMemDesc;
	RGX_HWPERF_CONFIG_CNTBLK* psFwArray;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	PVR_DPF_ENTERED;

	PVR_ASSERT(psDeviceNode);
	PVR_ASSERT(ui32ArrayLen>0);
	PVR_ASSERT(psBlockConfigs);

	/* Fill in the command structure with the parameters needed
	 */
	sKccbCmd.eCmdType = RGXFWIF_KCCB_CMD_HWPERF_CONFIG_ENABLE_BLKS;
	sKccbCmd.uCmdData.sHWPerfCfgEnableBlks.ui32NumBlocks = ui32ArrayLen;

	eError = DevmemFwAllocate(psDeviceNode->pvDevice,
			sizeof(RGX_HWPERF_CONFIG_CNTBLK)*ui32ArrayLen, 
			PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
									  PVRSRV_MEMALLOCFLAG_GPU_READABLE | 
									  PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
									  PVRSRV_MEMALLOCFLAG_CPU_READABLE |
									  PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE | 
									  PVRSRV_MEMALLOCFLAG_UNCACHED |
									  PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC,
			"FwHWPerfCountersConfigBlock",
			&psFwBlkConfigsMemDesc);
	if (eError != PVRSRV_OK)
		PVR_LOGR_IF_ERROR(eError, "DevmemFwAllocate");

	RGXSetFirmwareAddress(&sKccbCmd.uCmdData.sHWPerfCfgEnableBlks.sBlockConfigs,
			psFwBlkConfigsMemDesc, 0, 0);

	eError = DevmemAcquireCpuVirtAddr(psFwBlkConfigsMemDesc, (void **)&psFwArray);
	if (eError != PVRSRV_OK)
	{
		PVR_LOGG_IF_ERROR(eError, "DevmemAcquireCpuVirtAddr", fail1);
	}

	OSDeviceMemCopy(psFwArray, psBlockConfigs, sizeof(RGX_HWPERF_CONFIG_CNTBLK)*ui32ArrayLen);
	DevmemPDumpLoadMem(psFwBlkConfigsMemDesc,
						0,
						sizeof(RGX_HWPERF_CONFIG_CNTBLK)*ui32ArrayLen,
						0);

	/* PVR_DPF((PVR_DBG_VERBOSE, "PVRSRVRGXConfigEnableHWPerfCountersKM parameters set, calling FW")); */

	/* Ask the FW to carry out the HWPerf configuration command
	 */
	eError = RGXScheduleCommand(psDeviceNode->pvDevice,
			RGXFWIF_DM_GP, &sKccbCmd, sizeof(sKccbCmd), 0, PDUMP_FLAGS_CONTINUOUS);
	if (eError != PVRSRV_OK)
	{
		PVR_LOGG_IF_ERROR(eError, "RGXScheduleCommand", fail2);
	}

	/* PVR_DPF((PVR_DBG_VERBOSE, "PVRSRVRGXConfigEnableHWPerfCountersKM command scheduled for FW")); */

	/* Wait for FW to complete */
	eError = RGXWaitForFWOp(psDeviceNode->pvDevice, RGXFWIF_DM_GP, psDeviceNode->psSyncPrim, PDUMP_FLAGS_CONTINUOUS);
	if (eError != PVRSRV_OK)
	{
		PVR_LOGG_IF_ERROR(eError, "RGXWaitForFWOp", fail2);
	}

	/* Release temporary memory used for block configuration
	 */
	RGXUnsetFirmwareAddress(psFwBlkConfigsMemDesc);
	DevmemReleaseCpuVirtAddr(psFwBlkConfigsMemDesc);
	DevmemFwFree(psDeviceNode->pvDevice, psFwBlkConfigsMemDesc);

	/* PVR_DPF((PVR_DBG_VERBOSE, "PVRSRVRGXConfigEnableHWPerfCountersKM firmware completed")); */

	PVR_DPF((PVR_DBG_WARNING, "HWPerf %d counter blocks configured and ENABLED",  ui32ArrayLen));

	PVR_DPF_RETURN_OK;

fail2:
	DevmemReleaseCpuVirtAddr(psFwBlkConfigsMemDesc);
fail1:
	RGXUnsetFirmwareAddress(psFwBlkConfigsMemDesc);
	DevmemFwFree(psDeviceNode->pvDevice, psFwBlkConfigsMemDesc);

	PVR_DPF_RETURN_RC(eError);
}


/*
	PVRSRVRGXConfigCustomCountersReadingHWPerfKM
 */
PVRSRV_ERROR PVRSRVRGXConfigCustomCountersKM(
	CONNECTION_DATA             * psConnection,
	PVRSRV_DEVICE_NODE          * psDeviceNode,
	IMG_UINT16                    ui16CustomBlockID,
	IMG_UINT16                    ui16NumCustomCounters,
	IMG_UINT32                  * pui32CustomCounterIDs)
{
	PVRSRV_ERROR		eError = PVRSRV_OK;
	RGXFWIF_KCCB_CMD	sKccbCmd;
	DEVMEM_MEMDESC*		psFwSelectCntrsMemDesc = NULL;
	IMG_UINT32*			psFwArray;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	PVR_DPF_ENTERED;

	PVR_ASSERT(psDeviceNode);

	PVR_DPF((PVR_DBG_MESSAGE, "PVRSRVRGXSelectCustomCountersKM: configure block %u to read %u counters", ui16CustomBlockID, ui16NumCustomCounters));

	/* Fill in the command structure with the parameters needed */
	sKccbCmd.eCmdType = RGXFWIF_KCCB_CMD_HWPERF_SELECT_CUSTOM_CNTRS;
	sKccbCmd.uCmdData.sHWPerfSelectCstmCntrs.ui16NumCounters = ui16NumCustomCounters;
	sKccbCmd.uCmdData.sHWPerfSelectCstmCntrs.ui16CustomBlock = ui16CustomBlockID;

	if (ui16NumCustomCounters > 0)
	{
		PVR_ASSERT(pui32CustomCounterIDs);

		eError = DevmemFwAllocate(psDeviceNode->pvDevice,
				sizeof(IMG_UINT32) * ui16NumCustomCounters,
				PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
				PVRSRV_MEMALLOCFLAG_GPU_READABLE |
				PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
				PVRSRV_MEMALLOCFLAG_CPU_READABLE |
				PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
				PVRSRV_MEMALLOCFLAG_UNCACHED |
				PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC,
				"FwHWPerfConfigCustomCounters",
				&psFwSelectCntrsMemDesc);
		if (eError != PVRSRV_OK)
			PVR_LOGR_IF_ERROR(eError, "DevmemFwAllocate");

		RGXSetFirmwareAddress(&sKccbCmd.uCmdData.sHWPerfSelectCstmCntrs.sCustomCounterIDs,
				psFwSelectCntrsMemDesc, 0, 0);

		eError = DevmemAcquireCpuVirtAddr(psFwSelectCntrsMemDesc, (void **)&psFwArray);
		if (eError != PVRSRV_OK)
		{
			PVR_LOGG_IF_ERROR(eError, "DevmemAcquireCpuVirtAddr", fail1);
		}

		OSDeviceMemCopy(psFwArray, pui32CustomCounterIDs, sizeof(IMG_UINT32) * ui16NumCustomCounters);
		DevmemPDumpLoadMem(psFwSelectCntrsMemDesc,
				0,
				sizeof(IMG_UINT32) * ui16NumCustomCounters,
				0);
	}

	/* Push in the KCCB the command to configure the custom counters block */
	eError = RGXScheduleCommand(psDeviceNode->pvDevice,
			RGXFWIF_DM_GP, &sKccbCmd, sizeof(sKccbCmd), 0, PDUMP_FLAGS_CONTINUOUS);
	if (eError != PVRSRV_OK)
	{
		PVR_LOGG_IF_ERROR(eError, "RGXScheduleCommand", fail2);
	}
	PVR_DPF((PVR_DBG_VERBOSE, "PVRSRVRGXSelectCustomCountersKM: Command scheduled"));

	/* Wait for FW to complete */
	eError = RGXWaitForFWOp(psDeviceNode->pvDevice, RGXFWIF_DM_GP, psDeviceNode->psSyncPrim, PDUMP_FLAGS_CONTINUOUS);
	if (eError != PVRSRV_OK)
	{
		PVR_LOGG_IF_ERROR(eError, "RGXWaitForFWOp", fail2);
	}
	PVR_DPF((PVR_DBG_VERBOSE, "PVRSRVRGXSelectCustomCountersKM: FW operation completed"));

	if (ui16NumCustomCounters > 0)
	{
		/* Release temporary memory used for block configuration */
		RGXUnsetFirmwareAddress(psFwSelectCntrsMemDesc);
		DevmemReleaseCpuVirtAddr(psFwSelectCntrsMemDesc);
		DevmemFwFree(psDeviceNode->pvDevice, psFwSelectCntrsMemDesc);
	}

	PVR_DPF((PVR_DBG_MESSAGE, "HWPerf custom counters %u reading will be sent with the next HW events", ui16NumCustomCounters));

	PVR_DPF_RETURN_OK;

	fail2:
	if (psFwSelectCntrsMemDesc) DevmemReleaseCpuVirtAddr(psFwSelectCntrsMemDesc);

	fail1:
	if (psFwSelectCntrsMemDesc) 
	{
		RGXUnsetFirmwareAddress(psFwSelectCntrsMemDesc);
		DevmemFwFree(psDeviceNode->pvDevice, psFwSelectCntrsMemDesc);
	}
	
	PVR_DPF_RETURN_RC(eError);
}
/*
	PVRSRVRGXDisableHWPerfcountersKM
*/
PVRSRV_ERROR PVRSRVRGXCtrlHWPerfCountersKM(
	CONNECTION_DATA             * psConnection,
	PVRSRV_DEVICE_NODE          * psDeviceNode,
	IMG_BOOL                      bEnable,
	IMG_UINT32                    ui32ArrayLen,
	IMG_UINT16                  * psBlockIDs)
{
	PVRSRV_ERROR 		eError = PVRSRV_OK;
	RGXFWIF_KCCB_CMD 	sKccbCmd;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	PVR_DPF_ENTERED;

	PVR_ASSERT(psDeviceNode);
	PVR_ASSERT(ui32ArrayLen>0);
	PVR_ASSERT(ui32ArrayLen<=RGXFWIF_HWPERF_CTRL_BLKS_MAX);
	PVR_ASSERT(psBlockIDs);

	/* Fill in the command structure with the parameters needed
	 */
	sKccbCmd.eCmdType = RGXFWIF_KCCB_CMD_HWPERF_CTRL_BLKS;
	sKccbCmd.uCmdData.sHWPerfCtrlBlks.bEnable = bEnable;
	sKccbCmd.uCmdData.sHWPerfCtrlBlks.ui32NumBlocks = ui32ArrayLen;
	OSDeviceMemCopy(sKccbCmd.uCmdData.sHWPerfCtrlBlks.aeBlockIDs, psBlockIDs, sizeof(IMG_UINT16)*ui32ArrayLen);

	/* PVR_DPF((PVR_DBG_VERBOSE, "PVRSRVRGXCtrlHWPerfCountersKM parameters set, calling FW")); */

	/* Ask the FW to carry out the HWPerf configuration command
	 */
	eError = RGXScheduleCommand(psDeviceNode->pvDevice,
			RGXFWIF_DM_GP, &sKccbCmd, sizeof(sKccbCmd), 0, PDUMP_FLAGS_CONTINUOUS);
	if (eError != PVRSRV_OK)
		PVR_LOGR_IF_ERROR(eError, "RGXScheduleCommand");

	/* PVR_DPF((PVR_DBG_VERBOSE, "PVRSRVRGXCtrlHWPerfCountersKM command scheduled for FW")); */

	/* Wait for FW to complete */
	eError = RGXWaitForFWOp(psDeviceNode->pvDevice, RGXFWIF_DM_GP, psDeviceNode->psSyncPrim, PDUMP_FLAGS_CONTINUOUS);
	if (eError != PVRSRV_OK)
		PVR_LOGR_IF_ERROR(eError, "RGXWaitForFWOp");

	/* PVR_DPF((PVR_DBG_VERBOSE, "PVRSRVRGXCtrlHWPerfCountersKM firmware completed")); */

#if defined(DEBUG)
	if (bEnable)
		PVR_DPF((PVR_DBG_WARNING, "HWPerf %d counter blocks have been ENABLED",  ui32ArrayLen));
	else
		PVR_DPF((PVR_DBG_WARNING, "HWPerf %d counter blocks have been DISABLED",  ui32ArrayLen));
#endif

	PVR_DPF_RETURN_OK;
}

static INLINE IMG_UINT32 _RGXHWPerfFixBufferSize(IMG_UINT32 ui32BufSizeKB)
{
	if (ui32BufSizeKB > HWPERF_HOST_TL_STREAM_SIZE_MAX)
	{
		/* Size specified as a AppHint but it is too big */
		PVR_DPF((PVR_DBG_WARNING,"RGXHWPerfHostInit: HWPerf Host buffer size "
				"value (%u) too big, using maximum (%u)", ui32BufSizeKB,
		        HWPERF_HOST_TL_STREAM_SIZE_MAX));
		return HWPERF_HOST_TL_STREAM_SIZE_MAX<<10;
	}
	else if (ui32BufSizeKB >= HWPERF_HOST_TL_STREAM_SIZE_MIN)
	{
		return ui32BufSizeKB<<10;
	}
	else if (ui32BufSizeKB > 0)
	{
		/* Size specified as a AppHint but it is too small */
		PVR_DPF((PVR_DBG_WARNING,"RGXHWPerfHostInit: HWPerf Host buffer size "
		        "value (%u) too small, using minimum (%u)", ui32BufSizeKB,
		        HWPERF_HOST_TL_STREAM_SIZE_MIN));
		return HWPERF_HOST_TL_STREAM_SIZE_MIN<<10;
	}
	else
	{
		/* 0 size implies AppHint not set or is set to zero,
		 * use default size from driver constant. */
		return HWPERF_HOST_TL_STREAM_SIZE_DEFAULT<<10;
	}
}

/******************************************************************************
 * RGX HW Performance Host Stream API
 *****************************************************************************/

/*************************************************************************/ /*!
@Function       RGXHWPerfHostInit

@Description    Called during driver init for initialisation of HWPerfHost
                stream in the Rogue device driver. This function keeps allocated
                only the minimal necessary resources, which are required for
                functioning of HWPerf server module.

@Return         PVRSRV_ERROR
*/ /**************************************************************************/
PVRSRV_ERROR RGXHWPerfHostInit(IMG_UINT32 ui32BufSizeKB)
{
	PVRSRV_ERROR eError;
	PVR_ASSERT(gpsRgxDevInfo != NULL);

	eError = OSLockCreate(&gpsRgxDevInfo->hLockHWPerfHostStream, LOCK_TYPE_PASSIVE);
	PVR_LOGG_IF_ERROR(eError, "OSLockCreate", error);

	gpsRgxDevInfo->hHWPerfHostStream = NULL;
	gpsRgxDevInfo->ui32HWPerfHostFilter = 0; /* disable all events */
	gpsRgxDevInfo->ui32HWPerfHostNextOrdinal = 0;
	gpsRgxDevInfo->ui32HWPerfHostBufSize = _RGXHWPerfFixBufferSize(ui32BufSizeKB);

error:
	return eError;
}

static void _HWPerfHostOnConnectCB(void *pvArg)
{
	(void) pvArg;

	RGX_HWPERF_HOST_CLK_SYNC();
}

/*************************************************************************/ /*!
@Function       RGXHWPerfHostInitOnDemandResources

@Description    This function allocates the HWPerfHost buffer if HWPerf is
                enabled at driver load time. Otherwise, these buffers are
                allocated on-demand as and when required.

@Return         PVRSRV_ERROR
*/ /**************************************************************************/
PVRSRV_ERROR RGXHWPerfHostInitOnDemandResources(void)
{
	PVRSRV_ERROR eError;

	eError = TLStreamCreate(&gpsRgxDevInfo->hHWPerfHostStream,
	        PVRSRV_TL_HWPERF_HOST_SERVER_STREAM, gpsRgxDevInfo->ui32HWPerfHostBufSize,
	        TL_FLAG_RESERVE_DROP_NEWER, _HWPerfHostOnConnectCB, NULL, NULL,
	        NULL);
	PVR_LOGG_IF_ERROR(eError, "TLStreamCreate", error_stream_create);
	
	PVR_DPF((DBGPRIV_MESSAGE, "HWPerf Host buffer size is %uKB",
	        gpsRgxDevInfo->ui32HWPerfHostBufSize));

	return PVRSRV_OK;

error_stream_create:
	OSLockDestroy(gpsRgxDevInfo->hLockHWPerfHostStream);
	gpsRgxDevInfo->hLockHWPerfHostStream = NULL;

	return eError;
}

void RGXHWPerfHostDeInit(void)
{
	if (gpsRgxDevInfo && gpsRgxDevInfo->hHWPerfHostStream)
	{
		TLStreamClose(gpsRgxDevInfo->hHWPerfHostStream);
		gpsRgxDevInfo->hHWPerfHostStream = NULL;
	}

	if (gpsRgxDevInfo && gpsRgxDevInfo->hLockHWPerfHostStream)
	{
		OSLockDestroy(gpsRgxDevInfo->hLockHWPerfHostStream);
		gpsRgxDevInfo->hLockHWPerfHostStream = NULL;
	}

	/* Clear global RGX device reference */
	gpsRgxDevInfo = NULL;
	gpsRgxDevNode = NULL;
}

void RGXHWPerfHostSetEventFilter(IMG_UINT32 ui32Filter)
{
	gpsRgxDevInfo->ui32HWPerfHostFilter = ui32Filter;
}

IMG_BOOL RGXHWPerfHostIsEventEnabled(RGX_HWPERF_HOST_EVENT_TYPE eEvent)
{
	return gpsRgxDevInfo != NULL &&
	    (gpsRgxDevInfo->ui32HWPerfHostFilter & RGX_HWPERF_EVENT_MASK_VALUE(eEvent));
}

static inline void _PostFunctionPrologue(void)
{
	PVR_ASSERT(gpsRgxDevInfo->hLockHWPerfHostStream != NULL);
	PVR_ASSERT(gpsRgxDevInfo->hHWPerfHostStream != NULL);

	OSLockAcquire(gpsRgxDevInfo->hLockHWPerfHostStream);

	/* In case we drop packet we increment ordinal beforehand. */
	gpsRgxDevInfo->ui32HWPerfHostNextOrdinal++;
}

static inline void _PostFunctionEpilogue(void)
{
	OSLockRelease(gpsRgxDevInfo->hLockHWPerfHostStream);
}

static inline IMG_UINT8 *_ReserveHWPerfStream(IMG_UINT32 ui32Size)
{
	IMG_UINT8 *pui8Dest;

	PVRSRV_ERROR eError = TLStreamReserve(gpsRgxDevInfo->hHWPerfHostStream,
	                         &pui8Dest, ui32Size);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: Could not reserve space in %s buffer"
		        " (%d). Dropping packet.",
		        __func__, PVRSRV_TL_HWPERF_HOST_SERVER_STREAM, eError));
		return NULL;
	}
	PVR_ASSERT(pui8Dest != NULL);

	return pui8Dest;
}

static inline void _CommitHWPerfStream(IMG_UINT32 ui32Size)
{
	PVRSRV_ERROR eError = TLStreamCommit(gpsRgxDevInfo->hHWPerfHostStream,
	                                     ui32Size);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: Could not commit data to %s"
	            " (%d)", __func__, PVRSRV_TL_HWPERF_HOST_SERVER_STREAM, eError));
	}
}

static inline void _SetupHostPacketHeader(IMG_UINT8 *pui8Dest,
                                          RGX_HWPERF_HOST_EVENT_TYPE eEvType,
                                          IMG_UINT32 ui32Size)
{
	RGX_HWPERF_V2_PACKET_HDR *psHeader = (RGX_HWPERF_V2_PACKET_HDR *) pui8Dest;

	PVR_ASSERT(ui32Size<=RGX_HWPERF_MAX_PACKET_SIZE);

	psHeader->ui32Ordinal = gpsRgxDevInfo->ui32HWPerfHostNextOrdinal;
	psHeader->ui64Timestamp = RGXGPUFreqCalibrateClockus64();
	psHeader->ui32Sig = HWPERF_PACKET_V2B_SIG;
	psHeader->eTypeId = RGX_HWPERF_MAKE_TYPEID(RGX_HWPERF_STREAM_ID1_HOST,
	        eEvType, 0, 0);
	psHeader->ui32Size = ui32Size;
}

static inline IMG_UINT32 _CalculateHostCtrlPacketSize(
                                            RGX_HWPERF_HOST_CTRL_TYPE eCtrlType)
{
	RGX_HWPERF_HOST_CTRL_DATA *psData;
	IMG_UINT32 ui32Size = sizeof(psData->eEvType);

	switch (eCtrlType)
	{
		case RGX_HWPERF_CTRL_TYPE_CLIENT_STREAM_OPEN:
		case RGX_HWPERF_CTRL_TYPE_CLIENT_STREAM_CLOSE:
			ui32Size += sizeof(psData->uData.ui32Pid);
			break;
		default:
			// unknown type - this should never happen
			PVR_DPF((PVR_DBG_ERROR, "RGXHWPerfHostPostCtrlEvent: Invalid alloc"
			        " event type"));
			PVR_ASSERT(IMG_FALSE);
			break;
	}

	return RGX_HWPERF_MAKE_SIZE_VARIABLE(ui32Size);
}

static inline void _SetupHostCtrlPacketData(IMG_UINT8 *pui8Dest,
                                            RGX_HWPERF_HOST_CTRL_TYPE eEvType,
											IMG_UINT32 ui32Pid)
{
	RGX_HWPERF_HOST_CTRL_DATA *psData = (RGX_HWPERF_HOST_CTRL_DATA *)
	        (pui8Dest + sizeof(RGX_HWPERF_V2_PACKET_HDR));
	psData->eEvType = eEvType;
	psData->uData.ui32Pid = ui32Pid;
}

void RGXHWPerfHostPostCtrlEvent(RGX_HWPERF_HOST_CTRL_TYPE eEvType,
                                IMG_UINT32 ui32Pid)
{
	IMG_UINT8 *pui8Dest;
	IMG_UINT32 ui32Size = _CalculateHostCtrlPacketSize(eEvType);

	_PostFunctionPrologue();

	if ((pui8Dest = _ReserveHWPerfStream(ui32Size)) == NULL)
	{
		goto cleanup;
	}

	_SetupHostPacketHeader(pui8Dest, RGX_HWPERF_HOST_CTRL, ui32Size);
	_SetupHostCtrlPacketData(pui8Dest, eEvType, ui32Pid);

	_CommitHWPerfStream(ui32Size);

cleanup:
	_PostFunctionEpilogue();
}

static inline void _SetupHostEnqPacketData(IMG_UINT8 *pui8Dest,
                                           RGX_HWPERF_KICK_TYPE eEnqType,
                                           IMG_UINT32 ui32Pid,
                                           IMG_UINT32 ui32FWDMContext,
                                           IMG_UINT32 ui32ExtJobRef,
                                           IMG_UINT32 ui32IntJobRef)
{
	RGX_HWPERF_HOST_ENQ_DATA *psData = (RGX_HWPERF_HOST_ENQ_DATA *)
	        (pui8Dest + sizeof(RGX_HWPERF_V2_PACKET_HDR));
	psData->ui32EnqType = eEnqType;
	psData->ui32PID = ui32Pid;
	psData->ui32ExtJobRef = ui32ExtJobRef;
	psData->ui32IntJobRef = ui32IntJobRef;
	psData->ui32DMContext = ui32FWDMContext;
	psData->ui32Padding = 0;       /* Set to zero for future compatibility */
}

void RGXHWPerfHostPostEnqEvent(RGX_HWPERF_KICK_TYPE eEnqType,
                               IMG_UINT32 ui32Pid,
                               IMG_UINT32 ui32FWDMContext,
                               IMG_UINT32 ui32ExtJobRef,
                               IMG_UINT32 ui32IntJobRef)
{
	IMG_UINT8 *pui8Dest;
	IMG_UINT32 ui32Size = RGX_HWPERF_MAKE_SIZE_FIXED(RGX_HWPERF_HOST_ENQ_DATA);

	_PostFunctionPrologue();

	if ((pui8Dest = _ReserveHWPerfStream(ui32Size)) == NULL)
	{
		goto cleanup;
	}

	_SetupHostPacketHeader(pui8Dest, RGX_HWPERF_HOST_ENQ, ui32Size);
	_SetupHostEnqPacketData(pui8Dest, eEnqType, ui32Pid, ui32FWDMContext,
	        ui32ExtJobRef, ui32IntJobRef);

	_CommitHWPerfStream(ui32Size);

cleanup:
	_PostFunctionEpilogue();
}

static inline IMG_UINT32 _CalculateHostUfoPacketSize(RGX_HWPERF_UFO_EV eUfoType,
                                                     IMG_UINT uiNoOfUFOs)
{
	IMG_UINT32 ui32Size =
		(IMG_UINT32) offsetof(RGX_HWPERF_UFO_DATA, aui32StreamData);
	RGX_HWPERF_UFO_DATA_ELEMENT *puData;

	switch (eUfoType)
	{
		case RGX_HWPERF_UFO_EV_CHECK_SUCCESS:
		case RGX_HWPERF_UFO_EV_PRCHECK_SUCCESS:
			ui32Size += uiNoOfUFOs * sizeof(puData->sCheckSuccess);
			break;
		case RGX_HWPERF_UFO_EV_CHECK_FAIL:
		case RGX_HWPERF_UFO_EV_PRCHECK_FAIL:
			ui32Size += uiNoOfUFOs * sizeof(puData->sCheckFail);
			break;
		case RGX_HWPERF_UFO_EV_UPDATE:
			ui32Size += uiNoOfUFOs * sizeof(puData->sUpdate);
			break;
		default:
			// unknown type - this should never happen
			PVR_DPF((PVR_DBG_ERROR, "RGXHWPerfHostPostUfoEvent: Invalid UFO"
			        " event type"));
			PVR_ASSERT(IMG_FALSE);
			break;
	}

	return RGX_HWPERF_MAKE_SIZE_VARIABLE(ui32Size);
}

static inline void _SetupHostUfoPacketData(IMG_UINT8 *pui8Dest,
                                        RGX_HWPERF_UFO_EV eUfoType,
                                        RGX_HWPERF_UFO_DATA_ELEMENT psUFOData[],
                                        IMG_UINT uiNoOfUFOs)
{
	IMG_UINT uiUFOIdx;
	RGX_HWPERF_HOST_UFO_DATA *psData = (RGX_HWPERF_HOST_UFO_DATA *)
	        (pui8Dest + sizeof(RGX_HWPERF_V2_PACKET_HDR));
	RGX_HWPERF_UFO_DATA_ELEMENT *puData = (RGX_HWPERF_UFO_DATA_ELEMENT *)
	         psData->aui32StreamData;

	psData->eEvType = eUfoType;
	psData->ui32StreamInfo = RGX_HWPERF_MAKE_UFOPKTINFO(uiNoOfUFOs,
	        offsetof(RGX_HWPERF_HOST_UFO_DATA, aui32StreamData));

	switch (eUfoType)
	{
		case RGX_HWPERF_UFO_EV_CHECK_SUCCESS:
		case RGX_HWPERF_UFO_EV_PRCHECK_SUCCESS:
			for (uiUFOIdx = 0; uiUFOIdx < uiNoOfUFOs; uiUFOIdx++)
			{
				puData->sCheckSuccess.ui32FWAddr =
				        psUFOData[uiUFOIdx].sCheckSuccess.ui32FWAddr;
				puData->sCheckSuccess.ui32Value =
				        psUFOData[uiUFOIdx].sCheckSuccess.ui32Value;

				puData = (RGX_HWPERF_UFO_DATA_ELEMENT *)
				        (((IMG_BYTE *) puData) + sizeof(puData->sCheckSuccess));
			}
			break;
		case RGX_HWPERF_UFO_EV_CHECK_FAIL:
		case RGX_HWPERF_UFO_EV_PRCHECK_FAIL:
			for (uiUFOIdx = 0; uiUFOIdx < uiNoOfUFOs; uiUFOIdx++)
			{
				puData->sCheckFail.ui32FWAddr =
				        psUFOData[uiUFOIdx].sCheckFail.ui32FWAddr;
				puData->sCheckFail.ui32Value =
				        psUFOData[uiUFOIdx].sCheckFail.ui32Value;
				puData->sCheckFail.ui32Required =
				        psUFOData[uiUFOIdx].sCheckFail.ui32Required;

				puData = (RGX_HWPERF_UFO_DATA_ELEMENT *)
				        (((IMG_BYTE *) puData) + sizeof(puData->sCheckFail));
			}
			break;
		case RGX_HWPERF_UFO_EV_UPDATE:
			for (uiUFOIdx = 0; uiUFOIdx < uiNoOfUFOs; uiUFOIdx++)
			{
				puData->sUpdate.ui32FWAddr =
				        psUFOData[uiUFOIdx].sUpdate.ui32FWAddr;
				puData->sUpdate.ui32OldValue =
				        psUFOData[uiUFOIdx].sUpdate.ui32OldValue;
				puData->sUpdate.ui32NewValue =
				        psUFOData[uiUFOIdx].sUpdate.ui32NewValue;

				puData = (RGX_HWPERF_UFO_DATA_ELEMENT *)
				        (((IMG_BYTE *) puData) + sizeof(puData->sUpdate));
			}
			break;
		default:
			// unknown type - this should never happen
			PVR_DPF((PVR_DBG_ERROR, "RGXHWPerfHostPostUfoEvent: Invalid UFO"
			         " event type"));
			PVR_ASSERT(IMG_FALSE);
			break;
	}
}

void RGXHWPerfHostPostUfoEvent(RGX_HWPERF_UFO_EV eUfoType,
                               RGX_HWPERF_UFO_DATA_ELEMENT psUFOData[],
                               IMG_UINT uiNoOfUFOs)
{
	IMG_UINT8 *pui8Dest;
	IMG_UINT32 ui32Size = _CalculateHostUfoPacketSize(eUfoType, uiNoOfUFOs);

	_PostFunctionPrologue();

	if ((pui8Dest = _ReserveHWPerfStream(ui32Size)) == NULL)
	{
		goto cleanup;
	}

	_SetupHostPacketHeader(pui8Dest, RGX_HWPERF_HOST_UFO, ui32Size);
	_SetupHostUfoPacketData(pui8Dest, eUfoType, psUFOData, uiNoOfUFOs);

	_CommitHWPerfStream(ui32Size);

cleanup:
	_PostFunctionEpilogue();
}

static inline IMG_UINT32 _FixNameSizeAndCalculateHostAllocPacketSize(
                                       RGX_HWPERF_HOST_RESOURCE_TYPE eAllocType,
                                       const IMG_CHAR *psName,
                                       IMG_UINT32 *ui32NameSize)
{
	RGX_HWPERF_HOST_ALLOC_DATA *psData;
	RGX_HWPERF_HOST_ALLOC_DETAIL *puData;
	IMG_UINT32 ui32Size = sizeof(psData->ui32AllocType);

	/* first strip the terminator */
	if (psName[*ui32NameSize - 1] == '\0')
		*ui32NameSize -= 1;
	/* if string longer than maximum cut it (leave space for '\0') */
	if (*ui32NameSize >= SYNC_MAX_CLASS_NAME_LEN)
		*ui32NameSize = SYNC_MAX_CLASS_NAME_LEN - 1;

	switch (eAllocType)
	{
		case RGX_HWPERF_HOST_RESOURCE_TYPE_SYNC:
			ui32Size += sizeof(puData->sSyncAlloc) - SYNC_MAX_CLASS_NAME_LEN +
			        *ui32NameSize + 1; /* +1 for '\0' */
			break;
		default:
			// unknown type - this should never happen
			PVR_DPF((PVR_DBG_ERROR, "RGXHWPerfHostPostAllocEvent: Invalid alloc"
			        " event type"));
			PVR_ASSERT(IMG_FALSE);
			break;
	}

	return RGX_HWPERF_MAKE_SIZE_VARIABLE(ui32Size);
}

static inline void _SetupHostAllocPacketData(IMG_UINT8 *pui8Dest,
                                       RGX_HWPERF_HOST_RESOURCE_TYPE eAllocType,
                                       IMG_UINT32 ui32FWAddr,
                                       const IMG_CHAR *psName,
                                       IMG_UINT32 ui32NameSize)
{
	RGX_HWPERF_HOST_ALLOC_DATA *psData = (RGX_HWPERF_HOST_ALLOC_DATA *)
	        (pui8Dest + sizeof(RGX_HWPERF_V2_PACKET_HDR));
	psData->ui32AllocType = eAllocType;
	psData->uAllocDetail.sSyncAlloc.ui32FWAddr = ui32FWAddr;
	OSStringNCopy(psData->uAllocDetail.sSyncAlloc.acName, psName,
	              ui32NameSize);
	/* we know here that string is not null terminated and that we have enough
	 * space for the terminator */
	psData->uAllocDetail.sSyncAlloc.acName[ui32NameSize] = '\0';
}

void RGXHWPerfHostPostAllocEvent(RGX_HWPERF_HOST_RESOURCE_TYPE eAllocType,
                                 IMG_UINT32 ui32FWAddr,
                                 const IMG_CHAR *psName,
                                 IMG_UINT32 ui32NameSize)
{
	IMG_UINT8 *pui8Dest;
	IMG_UINT32 ui32Size =
	        _FixNameSizeAndCalculateHostAllocPacketSize(eAllocType, psName,
	                                                    &ui32NameSize);

	_PostFunctionPrologue();

	if ((pui8Dest = _ReserveHWPerfStream(ui32Size)) == NULL)
	{
		goto cleanup;
	}

	_SetupHostPacketHeader(pui8Dest, RGX_HWPERF_HOST_ALLOC, ui32Size);
	_SetupHostAllocPacketData(pui8Dest, eAllocType, ui32FWAddr, psName,
	                          ui32NameSize);

	_CommitHWPerfStream(ui32Size);

cleanup:
	_PostFunctionEpilogue();
}

static inline void _SetupHostFreePacketData(IMG_UINT8 *pui8Dest,
                                          RGX_HWPERF_HOST_RESOURCE_TYPE eFreeType,
                                          IMG_UINT32 ui32FWAddr)
{
	RGX_HWPERF_HOST_FREE_DATA *psData = (RGX_HWPERF_HOST_FREE_DATA *)
	        (pui8Dest + sizeof(RGX_HWPERF_V2_PACKET_HDR));
	psData->ui32FreeType = eFreeType;
	psData->uFreeDetail.sSyncFree.ui32FWAddr = ui32FWAddr;
}

void RGXHWPerfHostPostFreeEvent(RGX_HWPERF_HOST_RESOURCE_TYPE eFreeType,
                                IMG_UINT32 ui32FWAddr)
{
	IMG_UINT8 *pui8Dest;
	IMG_UINT32 ui32Size = RGX_HWPERF_MAKE_SIZE_FIXED(RGX_HWPERF_HOST_FREE_DATA);

	_PostFunctionPrologue();

	if ((pui8Dest = _ReserveHWPerfStream(ui32Size)) == NULL)
	{
		goto cleanup;
	}

	_SetupHostPacketHeader(pui8Dest, RGX_HWPERF_HOST_FREE, ui32Size);
	_SetupHostFreePacketData(pui8Dest, eFreeType, ui32FWAddr);

	_CommitHWPerfStream(ui32Size);

cleanup:
	_PostFunctionEpilogue();
}

static inline void _SetupHostClkSyncPacketData(IMG_UINT8 *pui8Dest)
{
	RGX_HWPERF_HOST_CLK_SYNC_DATA *psData = (RGX_HWPERF_HOST_CLK_SYNC_DATA *)
	        (pui8Dest + sizeof(RGX_HWPERF_V2_PACKET_HDR));
	RGXFWIF_GPU_UTIL_FWCB *psGpuUtilFWCB = gpsRgxDevInfo->psRGXFWIfGpuUtilFWCb;
	IMG_UINT32 ui32CurrIdx =
	        RGXFWIF_TIME_CORR_CURR_INDEX(psGpuUtilFWCB->ui32TimeCorrSeqCount);
	RGXFWIF_TIME_CORR *psTimeCorr = &psGpuUtilFWCB->sTimeCorr[ui32CurrIdx];

	psData->ui64CRTimestamp = psTimeCorr->ui64CRTimeStamp;
	psData->ui64OSTimestamp = psTimeCorr->ui64OSTimeStamp;
	psData->ui32ClockSpeed = psTimeCorr->ui32CoreClockSpeed;
}

void RGXHWPerfHostPostClkSyncEvent(void)
{
	IMG_UINT8 *pui8Dest;
	IMG_UINT32 ui32Size =
	        RGX_HWPERF_MAKE_SIZE_FIXED(RGX_HWPERF_HOST_CLK_SYNC_DATA);

	_PostFunctionPrologue();

	if ((pui8Dest = _ReserveHWPerfStream(ui32Size)) == NULL)
	{
		goto cleanup;
	}

	_SetupHostPacketHeader(pui8Dest, RGX_HWPERF_HOST_CLK_SYNC, ui32Size);
	_SetupHostClkSyncPacketData(pui8Dest);

	_CommitHWPerfStream(ui32Size);

cleanup:
	_PostFunctionEpilogue();
}

/******************************************************************************
 * SUPPORT_GPUTRACE_EVENTS
 *
 * Currently only implemented on Linux and Android. Feature can be enabled on
 * Android builds but can also be enabled on Linux builds for testing
 * but requires the gpu.h FTrace event header file to be present.
 *****************************************************************************/
#if defined(SUPPORT_GPUTRACE_EVENTS)

static void RGXHWPerfFTraceCmdCompleteNotify(PVRSRV_CMDCOMP_HANDLE);

typedef struct RGX_HWPERF_FTRACE_DATA {
	/* This lock ensures the HWPerf TL stream reading resources are not destroyed
	 * by one thread disabling it while another is reading from it in. Keeps the
	 * state and resource create/destroy atomic and consistent. */
	POS_LOCK    hFTraceLock;
	IMG_HANDLE  hGPUTraceCmdCompleteHandle;
	IMG_HANDLE  hGPUTraceTLStream;
	IMG_UINT64  ui64LastSampledTimeCorrOSTimeStamp;
	IMG_UINT32  ui32FTraceLastOrdinal;
	/* This lock ensures that the reference counting operation on the FTrace UFO
	 * events and enable/disable operation on firmware event are performed as
	 * one atomic operation. This should ensure that there are no race conditions
	 * between reference counting and firmware event state change.
	 * See below comment for g_uiUfoEventRef.
	 */
	POS_LOCK    hLockFTraceEventLock;
	/* Multiple FTrace UFO events are reflected in the firmware as only one event. When
	 * we enable FTrace UFO event we want to also at the same time enable it in
	 * the firmware. Since there is a multiple-to-one relation between those events
	 * we count how many FTrace UFO events is enabled. If at least one event is
	 * enabled we enabled the firmware event. When all FTrace UFO events are disabled
	 * we disable firmware event. */
	IMG_UINT    uiUfoEventRef;
	/* Saved value of the clock source before the trace was enabled. We're keeping
	 * it here so that know we which clock should be selected after we disable the
	 * gpu ftrace. */
	IMG_UINT64  ui64LastTimeCorrClock;
} RGX_HWPERF_FTRACE_DATA;

/* Caller must now hold hFTraceLock before calling this method.
 */
static PVRSRV_ERROR RGXHWPerfFTraceGPUEnable(void)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	RGX_HWPERF_FTRACE_DATA *psFtraceData;

	PVR_DPF_ENTERED;

	PVR_ASSERT(gpsRgxDevNode && gpsRgxDevInfo);

	psFtraceData = gpsRgxDevInfo->pvGpuFtraceData;

	PVR_ASSERT(OSLockIsLocked(psFtraceData->hFTraceLock));

	/* In the case where the AppHint has not been set we need to
	 * initialise the host driver HWPerf resources here. Allocated on
	 * demand to reduce RAM foot print on systems not needing HWPerf.
	 * Signal FW to enable event generation.
	 */
	if (gpsRgxDevInfo->bFirmwareInitialised)
	{
		IMG_UINT64 ui64UFOFilter = RGX_HWPERF_EVENT_MASK_VALUE(RGX_HWPERF_UFO) &
								   gpsRgxDevInfo->ui64HWPerfFilter;

		eError = PVRSRVRGXCtrlHWPerfKM(NULL, gpsRgxDevNode, IMG_FALSE,
		                               RGX_HWPERF_STREAM_ID0_FW,
		                               RGX_HWPERF_EVENT_MASK_HW_KICKFINISH |
		                               ui64UFOFilter);
		PVR_LOGG_IF_ERROR(eError, "PVRSRVRGXCtrlHWPerfKM", err_out);
	}
	else
	{
		/* only set filter and exit */
		gpsRgxDevInfo->ui64HWPerfFilter = RGX_HWPERF_EVENT_MASK_HW_KICKFINISH |
		        (RGX_HWPERF_EVENT_MASK_VALUE(RGX_HWPERF_UFO) &
		        gpsRgxDevInfo->ui64HWPerfFilter);
		PVRGpuTraceSetPreEnabled(IMG_TRUE);

		PVR_DPF((PVR_DBG_WARNING, "HWPerfFW mask has been SET to (%llx)",
		        (long long) gpsRgxDevInfo->ui64HWPerfFilter));

		return PVRSRV_OK;
	}

	/* Open the TL Stream for HWPerf data consumption */
	eError = TLClientOpenStream(DIRECT_BRIDGE_HANDLE,
								PVRSRV_TL_HWPERF_RGX_FW_STREAM,
								PVRSRV_STREAM_FLAG_ACQUIRE_NONBLOCKING,
								&psFtraceData->hGPUTraceTLStream);
	PVR_LOGG_IF_ERROR(eError, "TLClientOpenStream", err_out);

	/* Set clock source for timer correlation data to sched_clock */
	psFtraceData->ui64LastTimeCorrClock = RGXGPUFreqCalibrateGetClockSource();
	RGXGPUFreqCalibrateSetClockSource(gpsRgxDevNode, RGXTIMECORR_CLOCK_SCHED);

	/* Reset the OS timestamp coming from the timer correlation data
	 * associated with the latest HWPerf event we processed.
	 */
	psFtraceData->ui64LastSampledTimeCorrOSTimeStamp = 0;

	PVRGpuTraceSetEnabled(IMG_TRUE);

	/* Register a notifier to collect HWPerf data whenever the HW completes
	 * an operation.
	 */
	eError = PVRSRVRegisterCmdCompleteNotify(
		&psFtraceData->hGPUTraceCmdCompleteHandle,
		&RGXHWPerfFTraceCmdCompleteNotify,
		gpsRgxDevInfo);
	PVR_LOGG_IF_ERROR(eError, "PVRSRVRegisterCmdCompleteNotify", err_close_stream);

err_out:
	PVR_DPF_RETURN_RC(eError);

err_close_stream:
	PVRGpuTraceSetEnabled(IMG_FALSE);

	TLClientCloseStream(DIRECT_BRIDGE_HANDLE,
						psFtraceData->hGPUTraceTLStream);
	psFtraceData->hGPUTraceTLStream = 0;
	goto err_out;
}

/* Caller must now hold hFTraceLock before calling this method.
 */
static PVRSRV_ERROR RGXHWPerfFTraceGPUDisable(IMG_BOOL bDeInit)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	RGX_HWPERF_FTRACE_DATA *psFtraceData;

	PVR_DPF_ENTERED;

	PVR_ASSERT(gpsRgxDevNode && gpsRgxDevInfo);

	psFtraceData = gpsRgxDevInfo->pvGpuFtraceData;

	PVRGpuTraceSetEnabled(IMG_FALSE);
	PVRGpuTraceSetPreEnabled(IMG_FALSE);

	if (!bDeInit)
	{
		PVR_ASSERT(OSLockIsLocked(psFtraceData->hFTraceLock));

		eError = PVRSRVRGXCtrlHWPerfKM(NULL, gpsRgxDevNode, RGX_HWPERF_STREAM_ID0_FW, IMG_FALSE, (RGX_HWPERF_EVENT_MASK_NONE));
		PVR_LOG_IF_ERROR(eError, "PVRSRVRGXCtrlHWPerfKM");
	}

	if (psFtraceData->hGPUTraceCmdCompleteHandle)
	{
		/* Tracing is being turned off. Unregister the notifier. */
		eError = PVRSRVUnregisterCmdCompleteNotify(
				psFtraceData->hGPUTraceCmdCompleteHandle);
		PVR_LOG_IF_ERROR(eError, "PVRSRVUnregisterCmdCompleteNotify");
		psFtraceData->hGPUTraceCmdCompleteHandle = NULL;
	}

	if (psFtraceData->hGPUTraceTLStream)
	{
		IMG_PBYTE pbTmp = NULL;
		IMG_UINT32 ui32Tmp = 0;

		/* We have to flush both the L1 (FW) and L2 (Host) buffers in case there
		 * are some events left unprocessed in this FTrace/systrace "session"
		 * (note that even if we have just disabled HWPerf on the FW some packets
		 * could have been generated and already copied to L2 by the MISR handler).
		 *
		 * With the following calls we will both copy new data to the Host buffer
		 * (done by the producer callback in TLClientAcquireData) and advance
		 * the read offset in the buffer to catch up with the latest events.
		 */
		eError = TLClientAcquireData(DIRECT_BRIDGE_HANDLE,
		                             psFtraceData->hGPUTraceTLStream,
		                             &pbTmp, &ui32Tmp);
		PVR_LOG_IF_ERROR(eError, "TLClientCloseStream");

		/* Let close stream perform the release data on the outstanding acquired data */
		eError = TLClientCloseStream(DIRECT_BRIDGE_HANDLE,
		                             psFtraceData->hGPUTraceTLStream);
		PVR_LOG_IF_ERROR(eError, "TLClientCloseStream");

		psFtraceData->hGPUTraceTLStream = NULL;
	}

	if (psFtraceData->ui64LastTimeCorrClock != RGXTIMECORR_CLOCK_SCHED)
	{
		RGXGPUFreqCalibrateSetClockSource(gpsRgxDevNode,
		                                  psFtraceData->ui64LastTimeCorrClock);
	}

	PVR_DPF_RETURN_RC(eError);
}

PVRSRV_ERROR RGXHWPerfFTraceGPUEventsEnabledSet(IMG_BOOL bNewValue)
{
	IMG_BOOL bOldValue;
	PVRSRV_ERROR eError = PVRSRV_OK;
	RGX_HWPERF_FTRACE_DATA *psFtraceData;

	PVR_DPF_ENTERED;

	PVR_ASSERT(gpsRgxDevNode && gpsRgxDevInfo);

	psFtraceData = gpsRgxDevInfo->pvGpuFtraceData;

	/* About to create/destroy FTrace resources, lock critical section
	 * to avoid HWPerf MISR thread contention.
	 */
	OSLockAcquire(psFtraceData->hFTraceLock);

	bOldValue = PVRGpuTraceEnabled();

	if (bOldValue != bNewValue)
	{
		if (bNewValue)
		{
			eError = RGXHWPerfFTraceGPUEnable();
		}
		else
		{
			eError = RGXHWPerfFTraceGPUDisable(IMG_FALSE);
		}
	}

	OSLockRelease(psFtraceData->hFTraceLock);

	PVR_DPF_RETURN_RC(eError);
}

PVRSRV_ERROR PVRGpuTraceEnabledSet(IMG_BOOL bNewValue)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	/* This entry point from DebugFS must take the global 
	 * bridge lock at this outer level of the stack before calling
	 * into the RGX part of the driver which can lead to RGX
	 * device data changes and communication with the FW which
	 * all requires the bridge lock.
	 */
	OSAcquireBridgeLock();
	eError = RGXHWPerfFTraceGPUEventsEnabledSet(bNewValue);
	OSReleaseBridgeLock();

	PVR_DPF_RETURN_RC(eError);
}

/* Calculate the OS timestamp given an RGX timestamp in the HWPerf event. */
static uint64_t
CalculateEventTimestamp(PVRSRV_RGXDEV_INFO *psDevInfo,
						uint32_t ui32TimeCorrIndex,
						uint64_t ui64EventTimestamp)
{
	RGXFWIF_GPU_UTIL_FWCB *psGpuUtilFWCB = psDevInfo->psRGXFWIfGpuUtilFWCb;
	RGX_HWPERF_FTRACE_DATA *psFtraceData = psDevInfo->pvGpuFtraceData;
	RGXFWIF_TIME_CORR *psTimeCorr = &psGpuUtilFWCB->sTimeCorr[ui32TimeCorrIndex];
	uint64_t ui64CRTimeStamp = psTimeCorr->ui64CRTimeStamp;
	uint64_t ui64OSTimeStamp = psTimeCorr->ui64OSTimeStamp;
	uint32_t ui32CRDeltaToOSDeltaKNs = psTimeCorr->ui32CRDeltaToOSDeltaKNs;
	uint64_t ui64EventOSTimestamp, deltaRgxTimer, delta_ns;

	if (psFtraceData->ui64LastSampledTimeCorrOSTimeStamp > ui64OSTimeStamp)
	{
		/* The previous packet had a time reference (time correlation data) more
		 * recent than the one in the current packet, it means the timer
		 * correlation array wrapped too quickly (buffer too small) and in the
		 * previous call to RGXHWPerfFTraceGPUUfoEvent we read one of the
		 * newest timer correlations rather than one of the oldest ones.
		 */
		PVR_DPF((PVR_DBG_ERROR, "%s: The timestamps computed so far could be "
				 "wrong! The time correlation array size should be increased "
				 "to avoid this.", __func__));
	}

	psFtraceData->ui64LastSampledTimeCorrOSTimeStamp = ui64OSTimeStamp;

	/* RGX CR timer ticks delta */
	deltaRgxTimer = ui64EventTimestamp - ui64CRTimeStamp;
	/* RGX time delta in nanoseconds */
	delta_ns = RGXFWIF_GET_DELTA_OSTIME_NS(deltaRgxTimer, ui32CRDeltaToOSDeltaKNs);
	/* Calculate OS time of HWPerf event */
	ui64EventOSTimestamp = ui64OSTimeStamp + delta_ns;

	PVR_DPF((PVR_DBG_VERBOSE, "%s: psCurrentDvfs RGX %llu, OS %llu, DVFSCLK %u",
			 __func__, ui64CRTimeStamp, ui64OSTimeStamp,
			 psTimeCorr->ui32CoreClockSpeed));

	return ui64EventOSTimestamp;
}

void RGXHWPerfFTraceGPUEnqueueEvent(PVRSRV_RGXDEV_INFO *psDevInfo,
		IMG_UINT32 ui32CtxId, IMG_UINT32 ui32JobId,
		RGX_HWPERF_KICK_TYPE eKickType)
{
	PVR_DPF_ENTERED;

	PVR_DPF((PVR_DBG_VERBOSE, "RGXHWPerfFTraceGPUEnqueueEvent: ui32CtxId %u, "
	        "ui32JobId %u", ui32CtxId, ui32JobId));

	PVRGpuTraceClientWork(ui32CtxId, ui32JobId,
	    RGXHWPerfKickTypeToStr(eKickType));

	PVR_DPF_RETURN;
}


static void RGXHWPerfFTraceGPUSwitchEvent(PVRSRV_RGXDEV_INFO *psDevInfo,
		RGX_HWPERF_V2_PACKET_HDR* psHWPerfPkt, const IMG_CHAR* pszWorkName,
		PVR_GPUTRACE_SWITCH_TYPE eSwType)
{
	IMG_UINT64 ui64Timestamp;
	RGX_HWPERF_HW_DATA_FIELDS* psHWPerfPktData;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psHWPerfPkt);
	PVR_ASSERT(pszWorkName);

	psHWPerfPktData = (RGX_HWPERF_HW_DATA_FIELDS*) RGX_HWPERF_GET_PACKET_DATA_BYTES(psHWPerfPkt);

	ui64Timestamp = CalculateEventTimestamp(psDevInfo, psHWPerfPktData->ui32TimeCorrIndex,
											psHWPerfPkt->ui64Timestamp);

	PVR_DPF((PVR_DBG_VERBOSE, "RGXHWPerfFTraceGPUSwitchEvent: %s ui32ExtJobRef=%d, ui32IntJobRef=%d, eSwType=%d",
			pszWorkName, psHWPerfPktData->ui32DMContext, psHWPerfPktData->ui32IntJobRef, eSwType));

	PVRGpuTraceWorkSwitch(ui64Timestamp, psHWPerfPktData->ui32DMContext, psHWPerfPktData->ui32CtxPriority,
	                      psHWPerfPktData->ui32IntJobRef, pszWorkName, eSwType);

	PVR_DPF_RETURN;
}

static void RGXHWPerfFTraceGPUUfoEvent(PVRSRV_RGXDEV_INFO *psDevInfo,
                                       RGX_HWPERF_V2_PACKET_HDR* psHWPerfPkt)
{
	IMG_UINT64 ui64Timestamp;
	RGX_HWPERF_UFO_DATA *psHWPerfPktData;
	IMG_UINT32 ui32UFOCount;
	RGX_HWPERF_UFO_DATA_ELEMENT *puData;

	psHWPerfPktData = (RGX_HWPERF_UFO_DATA *)
	        RGX_HWPERF_GET_PACKET_DATA_BYTES(psHWPerfPkt);

	ui32UFOCount = RGX_HWPERF_GET_UFO_STREAMSIZE(psHWPerfPktData->ui32StreamInfo);
	puData = (RGX_HWPERF_UFO_DATA_ELEMENT *) (((IMG_BYTE *) psHWPerfPktData)
	        + RGX_HWPERF_GET_UFO_STREAMOFFSET(psHWPerfPktData->ui32StreamInfo));

	ui64Timestamp = CalculateEventTimestamp(psDevInfo, psHWPerfPktData->ui32TimeCorrIndex,
											psHWPerfPkt->ui64Timestamp);

	PVR_DPF((PVR_DBG_VERBOSE, "RGXHWPerfFTraceGPUUfoEvent: ui32ExtJobRef=%d, "
	        "ui32IntJobRef=%d", psHWPerfPktData->ui32ExtJobRef,
	        psHWPerfPktData->ui32IntJobRef));

	PVRGpuTraceUfo(ui64Timestamp, psHWPerfPktData->eEvType,
	        psHWPerfPktData->ui32ExtJobRef, psHWPerfPktData->ui32DMContext,
	        psHWPerfPktData->ui32IntJobRef, ui32UFOCount, puData);
}

static void RGXHWPerfFTraceGPUFirmwareEvent(PVRSRV_RGXDEV_INFO *psDevInfo,
		RGX_HWPERF_V2_PACKET_HDR* psHWPerfPkt, const IMG_CHAR* pszWorkName,
		PVR_GPUTRACE_SWITCH_TYPE eSwType)

{
	uint64_t ui64Timestamp;
	RGX_HWPERF_FW_DATA *psHWPerfPktData = (RGX_HWPERF_FW_DATA *)
		RGX_HWPERF_GET_PACKET_DATA_BYTES(psHWPerfPkt);

	ui64Timestamp = CalculateEventTimestamp(psDevInfo, psHWPerfPktData->ui32TimeCorrIndex,
											psHWPerfPkt->ui64Timestamp);

	PVRGpuTraceFirmware(ui64Timestamp, pszWorkName, eSwType);
}

static IMG_BOOL ValidAndEmitFTraceEvent(PVRSRV_RGXDEV_INFO *psDevInfo,
		RGX_HWPERF_V2_PACKET_HDR* psHWPerfPkt)
{
	RGX_HWPERF_EVENT_TYPE eType;
	RGX_HWPERF_FTRACE_DATA *psFtraceData = psDevInfo->pvGpuFtraceData;
	IMG_UINT32 ui32HwEventTypeIndex;
	static const struct {
		IMG_CHAR* pszName;
		PVR_GPUTRACE_SWITCH_TYPE eSwType;
	} aszHwEventTypeMap[] = {
			{ /* RGX_HWPERF_FW_BGSTART */      "BG",     PVR_GPUTRACE_SWITCH_TYPE_BEGIN },
			{ /* RGX_HWPERF_FW_BGEND */        "BG",     PVR_GPUTRACE_SWITCH_TYPE_END },
			{ /* RGX_HWPERF_FW_IRQSTART */     "IRQ",     PVR_GPUTRACE_SWITCH_TYPE_BEGIN },
			{ /* RGX_HWPERF_FW_IRQEND */       "IRQ",     PVR_GPUTRACE_SWITCH_TYPE_END },
			{ /* RGX_HWPERF_FW_DBGSTART */     "DBG",     PVR_GPUTRACE_SWITCH_TYPE_BEGIN },
			{ /* RGX_HWPERF_FW_DBGEND */       "DBG",     PVR_GPUTRACE_SWITCH_TYPE_END },
			{ /* RGX_HWPERF_HW_PMOOM_TAPAUSE */	"PMOOM_TAPAUSE",  PVR_GPUTRACE_SWITCH_TYPE_END },
			{ /* RGX_HWPERF_HW_TAKICK */       "TA",     PVR_GPUTRACE_SWITCH_TYPE_BEGIN },
			{ /* RGX_HWPERF_HW_TAFINISHED */   "TA",     PVR_GPUTRACE_SWITCH_TYPE_END },
			{ /* RGX_HWPERF_HW_3DTQKICK */     "TQ3D",   PVR_GPUTRACE_SWITCH_TYPE_BEGIN },
			{ /* RGX_HWPERF_HW_3DKICK */       "3D",     PVR_GPUTRACE_SWITCH_TYPE_BEGIN },
			{ /* RGX_HWPERF_HW_3DFINISHED */   "3D",     PVR_GPUTRACE_SWITCH_TYPE_END },
			{ /* RGX_HWPERF_HW_CDMKICK */      "CDM",    PVR_GPUTRACE_SWITCH_TYPE_BEGIN },
			{ /* RGX_HWPERF_HW_CDMFINISHED */  "CDM",    PVR_GPUTRACE_SWITCH_TYPE_END },
			{ /* RGX_HWPERF_HW_TLAKICK */      "TQ2D",   PVR_GPUTRACE_SWITCH_TYPE_BEGIN },
			{ /* RGX_HWPERF_HW_TLAFINISHED */  "TQ2D",   PVR_GPUTRACE_SWITCH_TYPE_END },
			{ /* RGX_HWPERF_HW_3DSPMKICK */    "3DSPM",  PVR_GPUTRACE_SWITCH_TYPE_BEGIN },
			{ /* RGX_HWPERF_HW_PERIODIC */     NULL, 0 }, /* PERIODIC not supported */
			{ /* RGX_HWPERF_HW_RTUKICK */      "RTU",    PVR_GPUTRACE_SWITCH_TYPE_BEGIN },
			{ /* RGX_HWPERF_HW_RTUFINISHED */  "RTU",    PVR_GPUTRACE_SWITCH_TYPE_END },
			{ /* RGX_HWPERF_HW_SHGKICK */      "SHG",    PVR_GPUTRACE_SWITCH_TYPE_BEGIN },
			{ /* RGX_HWPERF_HW_SHGFINISHED */  "SHG",    PVR_GPUTRACE_SWITCH_TYPE_END },
			{ /* RGX_HWPERF_HW_3DTQFINISHED */ "TQ3D",   PVR_GPUTRACE_SWITCH_TYPE_END },
			{ /* RGX_HWPERF_HW_3DSPMFINISHED */ "3DSPM", PVR_GPUTRACE_SWITCH_TYPE_END },
			{ /* RGX_HWPERF_HW_PMOOM_TARESUME */	"PMOOM_TARESUME",  PVR_GPUTRACE_SWITCH_TYPE_BEGIN },
			{ /* RGX_HWPERF_HW_TDMKICK */      "TDM",   PVR_GPUTRACE_SWITCH_TYPE_BEGIN },
			{ /* RGX_HWPERF_HW_TDMFINISHED */  "TDM",   PVR_GPUTRACE_SWITCH_TYPE_END },
	};
	static_assert(RGX_HWPERF_HW_EVENT_RANGE0_FIRST_TYPE == RGX_HWPERF_FW_EVENT_RANGE_LAST_TYPE + 1,
				  "FW and HW events are not contiguous in RGX_HWPERF_EVENT_TYPE");

	PVR_ASSERT(psHWPerfPkt);
	eType = RGX_HWPERF_GET_TYPE(psHWPerfPkt);

	if (psFtraceData->ui32FTraceLastOrdinal != psHWPerfPkt->ui32Ordinal - 1)
	{
		RGX_HWPERF_STREAM_ID eStreamId = RGX_HWPERF_GET_STREAM_ID(psHWPerfPkt);
		PVRGpuTraceEventsLost(eStreamId,
		                      psFtraceData->ui32FTraceLastOrdinal,
		                      psHWPerfPkt->ui32Ordinal);
		PVR_DPF((PVR_DBG_ERROR, "FTrace events lost (stream_id = %u, ordinal: last = %u, current = %u)",
		         eStreamId, psFtraceData->ui32FTraceLastOrdinal, psHWPerfPkt->ui32Ordinal));
	}

	psFtraceData->ui32FTraceLastOrdinal = psHWPerfPkt->ui32Ordinal;

	/* Process UFO packets */
	if (eType == RGX_HWPERF_UFO)
	{
		RGXHWPerfFTraceGPUUfoEvent(psDevInfo, psHWPerfPkt);
		return IMG_TRUE;
	}

	if (eType <= RGX_HWPERF_HW_EVENT_RANGE0_LAST_TYPE)
	{
		/* this ID belongs to range 0, so index directly in range 0 */
		ui32HwEventTypeIndex = eType - RGX_HWPERF_FW_EVENT_RANGE_FIRST_TYPE;
	}
	else
	{
		/* this ID belongs to range 1, so first index in range 1 and skip number of slots used up for range 0 */
		ui32HwEventTypeIndex = (eType - RGX_HWPERF_HW_EVENT_RANGE1_FIRST_TYPE) + 
		                       (RGX_HWPERF_HW_EVENT_RANGE0_LAST_TYPE - RGX_HWPERF_FW_EVENT_RANGE_FIRST_TYPE + 1);
	}

	if (ui32HwEventTypeIndex >= IMG_ARR_NUM_ELEMS(aszHwEventTypeMap))
		goto err_unsupported;

	if (aszHwEventTypeMap[ui32HwEventTypeIndex].pszName == NULL)
	{
		/* Not supported map entry, ignore event */
		goto err_unsupported;
	}

	if (HWPERF_PACKET_IS_HW_TYPE(eType))
	{
		RGXHWPerfFTraceGPUSwitchEvent(psDevInfo, psHWPerfPkt,
									  aszHwEventTypeMap[ui32HwEventTypeIndex].pszName,
									  aszHwEventTypeMap[ui32HwEventTypeIndex].eSwType);
	}
	else if (HWPERF_PACKET_IS_FW_TYPE(eType))
	{
		RGXHWPerfFTraceGPUFirmwareEvent(psDevInfo, psHWPerfPkt,
										aszHwEventTypeMap[ui32HwEventTypeIndex].pszName,
										aszHwEventTypeMap[ui32HwEventTypeIndex].eSwType);
	}
	else
	{
		goto err_unsupported;
	}

	return IMG_TRUE;

err_unsupported:
	PVR_DPF((PVR_DBG_VERBOSE, "%s: Unsupported event type %d", __func__, eType));
	return IMG_FALSE;
}


static void RGXHWPerfFTraceGPUProcessPackets(PVRSRV_RGXDEV_INFO *psDevInfo,
		IMG_PBYTE pBuffer, IMG_UINT32 ui32ReadLen)
{
	IMG_UINT32			ui32TlPackets = 0;
	IMG_UINT32			ui32HWPerfPackets = 0;
	IMG_UINT32			ui32HWPerfPacketsSent = 0;
	IMG_PBYTE			pBufferEnd;
	PVRSRVTL_PPACKETHDR psHDRptr;
	PVRSRVTL_PACKETTYPE ui16TlType;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psDevInfo);
	PVR_ASSERT(pBuffer);
	PVR_ASSERT(ui32ReadLen);

	/* Process the TL Packets
	 */
	pBufferEnd = pBuffer+ui32ReadLen;
	psHDRptr = GET_PACKET_HDR(pBuffer);
	while ( psHDRptr < (PVRSRVTL_PPACKETHDR)pBufferEnd )
	{
		ui16TlType = GET_PACKET_TYPE(psHDRptr);
		if (ui16TlType == PVRSRVTL_PACKETTYPE_DATA)
		{
			IMG_UINT16 ui16DataLen = GET_PACKET_DATA_LEN(psHDRptr);
			if (0 == ui16DataLen)
			{
				PVR_DPF((PVR_DBG_ERROR, "RGXHWPerfFTraceGPUProcessPackets: ZERO Data in TL data packet: %p", psHDRptr));
			}
			else
			{
				RGX_HWPERF_V2_PACKET_HDR* psHWPerfPkt;
				RGX_HWPERF_V2_PACKET_HDR* psHWPerfEnd;

				/* Check for lost hwperf data packets */
				psHWPerfEnd = RGX_HWPERF_GET_PACKET(GET_PACKET_DATA_PTR(psHDRptr)+ui16DataLen);
				psHWPerfPkt = RGX_HWPERF_GET_PACKET(GET_PACKET_DATA_PTR(psHDRptr));
				do
				{
					if (ValidAndEmitFTraceEvent(psDevInfo, psHWPerfPkt))
					{
						ui32HWPerfPacketsSent++;
					}
					ui32HWPerfPackets++;
					psHWPerfPkt = RGX_HWPERF_GET_NEXT_PACKET(psHWPerfPkt);
				}
				while (psHWPerfPkt < psHWPerfEnd);
			}
		}
		else if (ui16TlType == PVRSRVTL_PACKETTYPE_MOST_RECENT_WRITE_FAILED)
		{
			PVR_DPF((PVR_DBG_MESSAGE, "RGXHWPerfFTraceGPUProcessPackets: Indication that the transport buffer was full"));
		}
		else
		{
			/* else Ignore padding packet type and others */
			PVR_DPF((PVR_DBG_MESSAGE, "RGXHWPerfFTraceGPUProcessPackets: Ignoring TL packet, type %d", ui16TlType ));
		}

		psHDRptr = GET_NEXT_PACKET_ADDR(psHDRptr);
		ui32TlPackets++;
	}

	PVR_DPF((PVR_DBG_VERBOSE, "RGXHWPerfFTraceGPUProcessPackets: TL "
	 		"Packets processed %03d, HWPerf packets %03d, sent %03d",
	 		ui32TlPackets, ui32HWPerfPackets, ui32HWPerfPacketsSent));

	PVR_DPF_RETURN;
}


static
void RGXHWPerfFTraceCmdCompleteNotify(PVRSRV_CMDCOMP_HANDLE hCmdCompHandle)
{
	PVRSRV_DATA*		psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_RGXDEV_INFO* psDeviceInfo = hCmdCompHandle;
	RGX_HWPERF_FTRACE_DATA* psFtraceData;
	PVRSRV_ERROR		eError;
	IMG_PBYTE			pBuffer;
	IMG_UINT32			ui32ReadLen;

	PVR_DPF_ENTERED;

	/* Exit if no HWPerf enabled device exits */
	PVR_ASSERT(psDeviceInfo != NULL &&
			   psPVRSRVData != NULL &&
			   gpsRgxDevInfo != NULL);

	psFtraceData = gpsRgxDevInfo->pvGpuFtraceData;

	/* Command-complete notifiers can run concurrently. If this is
	 * happening, just bail out and let the previous call finish.
	 * This is ok because we can process the queued packets on the next call.
	 */
	if (!PVRGpuTraceEnabled() || !(OSTryLockAcquire(psFtraceData->hFTraceLock)))
	{
		PVR_DPF_RETURN;
	}

	/* PVRGpuTraceSetEnabled() and hGPUTraceTLStream are now
	 * called / atomically set inside the hFTraceLock so just assert here.
	 */
	PVR_ASSERT(psFtraceData->hGPUTraceTLStream);

	/* If we have a valid stream attempt to acquire some data */
	eError = TLClientAcquireData(DIRECT_BRIDGE_HANDLE, psFtraceData->hGPUTraceTLStream, &pBuffer, &ui32ReadLen);
	if (eError == PVRSRV_OK)
	{
		/* Process the HWPerf packets and release the data */
		if (ui32ReadLen > 0)
		{
			PVR_DPF((PVR_DBG_VERBOSE, "RGXHWPerfFTraceGPUThread: DATA AVAILABLE offset=%p, length=%d", pBuffer, ui32ReadLen));

			/* Process the transport layer data for HWPerf packets... */
			RGXHWPerfFTraceGPUProcessPackets(psDeviceInfo, pBuffer, ui32ReadLen);

			eError = TLClientReleaseData(DIRECT_BRIDGE_HANDLE, psFtraceData->hGPUTraceTLStream);
			if (eError != PVRSRV_OK)
			{
				PVR_LOG_ERROR(eError, "TLClientReleaseData");

				/* Serious error, disable FTrace GPU events */

				/* Release TraceLock so we always have the locking
				 * order BridgeLock->TraceLock to prevent AB-BA deadlocks*/
				OSLockRelease(psFtraceData->hFTraceLock);
				OSAcquireBridgeLock();
				OSLockAcquire(psFtraceData->hFTraceLock);
				RGXHWPerfFTraceGPUDisable(IMG_FALSE);
				OSLockRelease(psFtraceData->hFTraceLock);
				OSReleaseBridgeLock();
				goto out;

			}
		} /* else no data, ignore */
	}
	else if (eError != PVRSRV_ERROR_TIMEOUT)
	{
		PVR_LOG_ERROR(eError, "TLClientAcquireData");
	}

	OSLockRelease(psFtraceData->hFTraceLock);
out:
	PVR_DPF_RETURN;
}

PVRSRV_ERROR RGXHWPerfFTraceGPUInit(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR eError;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	RGX_HWPERF_FTRACE_DATA *psData = OSAllocMem(sizeof(RGX_HWPERF_FTRACE_DATA));
	if (psData == NULL)
		return PVRSRV_ERROR_OUT_OF_MEMORY;

	/* We initialise it only once because we want to track if any
	 * packets were dropped. */
	psData->ui32FTraceLastOrdinal = IMG_UINT32_MAX - 1;

	eError = OSLockCreate(&psData->hFTraceLock, LOCK_TYPE_DISPATCH);
	PVR_LOGG_IF_ERROR(eError, "OSLockCreate", e0);

	eError = OSLockCreate(&psData->hLockFTraceEventLock, LOCK_TYPE_PASSIVE);
	PVR_LOGG_IF_ERROR(eError, "OSLockCreate", e1);

	psData->uiUfoEventRef = 0;

	psDevInfo->pvGpuFtraceData = psData;

	return PVRSRV_OK;

e1:
	OSLockDestroy(psData->hFTraceLock);
e0:
	OSFreeMem(psData);

	return eError;
}

void RGXHWPerfFTraceGPUDeInit(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	RGX_HWPERF_FTRACE_DATA *psData = psDevInfo->pvGpuFtraceData;

	OSLockDestroy(psData->hFTraceLock);
	OSLockDestroy(psData->hLockFTraceEventLock);
	OSFreeMem(psDevInfo->pvGpuFtraceData);
}

void PVRGpuTraceEnableUfoCallback(void)
{
	PVRSRV_ERROR eError;
	RGX_HWPERF_FTRACE_DATA *psFtraceData = gpsRgxDevInfo->pvGpuFtraceData;

	OSLockAcquire(psFtraceData->hLockFTraceEventLock);

	if (psFtraceData->uiUfoEventRef++ == 0)
	{
		IMG_UINT64 ui64Filter = RGX_HWPERF_EVENT_MASK_VALUE(RGX_HWPERF_UFO) |
		        gpsRgxDevInfo->ui64HWPerfFilter;
		/* Small chance exists that ui64HWPerfFilter can be changed here and
		 * the newest filter value will be changed to the old one + UFO event.
		 * This is not a critical problem. */
		eError = PVRSRVRGXCtrlHWPerfKM(NULL, gpsRgxDevNode,
		                               RGX_HWPERF_STREAM_ID0_FW,
		                               IMG_FALSE, ui64Filter);
		if (eError == PVRSRV_ERROR_NOT_INITIALISED)
		{
			/* If we land here that means that the FW is not initialised yet.
			 * We stored the filter and it will be passed to the firmware
			 * during it's initialisation phase. So ignore. */
		}
		else if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "Could not enable UFO HWPerf event."));
		}
	}

	OSLockRelease(psFtraceData->hLockFTraceEventLock);
}

void PVRGpuTraceDisableUfoCallback(void)
{
	PVRSRV_ERROR eError;
	RGX_HWPERF_FTRACE_DATA *psFtraceData;

	/* We have to check if lock is valid because on driver unload
	 * RGXHWPerfFTraceGPUDeInit is called before kernel disables the ftrace
	 * events. This means that the lock will be destroyed before this callback
	 * is called.
	 * We can safely return if that situation happens because driver will be
	 * unloaded so we don't care about HWPerf state anymore. */
	if (gpsRgxDevInfo == NULL || gpsRgxDevInfo->pvGpuFtraceData == NULL)
		return;

	psFtraceData = gpsRgxDevInfo->pvGpuFtraceData;

	OSLockAcquire(psFtraceData->hLockFTraceEventLock);

	if (--psFtraceData->uiUfoEventRef == 0)
	{
		IMG_UINT64 ui64Filter = ~(RGX_HWPERF_EVENT_MASK_VALUE(RGX_HWPERF_UFO)) &
		        gpsRgxDevInfo->ui64HWPerfFilter;
		/* Small chance exists that ui64HWPerfFilter can be changed here and
		 * the newest filter value will be changed to the old one + UFO event.
		 * This is not a critical problem. */
		eError = PVRSRVRGXCtrlHWPerfKM(NULL, gpsRgxDevNode,
		                               RGX_HWPERF_STREAM_ID0_FW,
		                               IMG_FALSE, ui64Filter);
		if (eError == PVRSRV_ERROR_NOT_INITIALISED)
		{
			/* If we land here that means that the FW is not initialised yet.
			 * We stored the filter and it will be passed to the firmware
			 * during it's initialisation phase. So ignore. */
		}
		else if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "Could not disable UFO HWPerf event."));
		}
	}

	OSLockRelease(psFtraceData->hLockFTraceEventLock);
}

void PVRGpuTraceEnableFirmwareActivityCallback(void)
{
	RGX_HWPERF_FTRACE_DATA *psFtraceData = gpsRgxDevInfo->pvGpuFtraceData;
	uint64_t ui64Filter;
	int i;

	OSLockAcquire(psFtraceData->hLockFTraceEventLock);

	ui64Filter = gpsRgxDevInfo->ui64HWPerfFilter;

	/* Enable all FW events. */
	for (i = RGX_HWPERF_FW_EVENT_RANGE_FIRST_TYPE;
		 i <= RGX_HWPERF_FW_EVENT_RANGE_LAST_TYPE;
		 i++)
	{
		ui64Filter |= RGX_HWPERF_EVENT_MASK_VALUE(i);
	}

	if (PVRSRVRGXCtrlHWPerfKM(NULL, gpsRgxDevNode, RGX_HWPERF_STREAM_ID0_FW,
							  IMG_FALSE, ui64Filter) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Could not enable HWPerf event for firmware task timings."));
	}

	OSLockRelease(psFtraceData->hLockFTraceEventLock);
}

void PVRGpuTraceDisableFirmwareActivityCallback(void)
{
	RGX_HWPERF_FTRACE_DATA *psFtraceData = gpsRgxDevInfo->pvGpuFtraceData;
	uint64_t ui64Filter;
	int i;

	if (!psFtraceData)
		return;

	OSLockAcquire(psFtraceData->hLockFTraceEventLock);

	ui64Filter = gpsRgxDevInfo->ui64HWPerfFilter;

	/* Disable all FW events. */
	for (i = RGX_HWPERF_FW_EVENT_RANGE_FIRST_TYPE;
		 i <= RGX_HWPERF_FW_EVENT_RANGE_LAST_TYPE;
		 i++)
	{
		ui64Filter &= ~RGX_HWPERF_EVENT_MASK_VALUE(i);
	}

	if (PVRSRVRGXCtrlHWPerfKM(NULL, gpsRgxDevNode, RGX_HWPERF_STREAM_ID0_FW,
							  IMG_FALSE, ui64Filter) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Could not disable HWPerf event for firmware task timings."));
	}

	OSLockRelease(psFtraceData->hLockFTraceEventLock);
}

#endif /* SUPPORT_GPUTRACE_EVENTS */

/******************************************************************************
 * Currently only implemented on Linux. Feature can be enabled to provide
 * an interface to 3rd-party kernel modules that wish to access the
 * HWPerf data. The API is documented in the rgxapi_km.h header and
 * the rgx_hwperf* headers.
 *****************************************************************************/

/* Internal HWPerf kernel connection/device data object to track the state
 * of a client session.
 */
typedef struct
{
	PVRSRV_DEVICE_NODE* psRgxDevNode;
	PVRSRV_RGXDEV_INFO* psRgxDevInfo;

	/* TL Open/close state */
	IMG_HANDLE          hSD[RGX_HWPERF_STREAM_ID_LAST];

	/* TL Acquire/release state */
	IMG_PBYTE			pHwpBuf[RGX_HWPERF_STREAM_ID_LAST];
	IMG_UINT32			ui32HwpBufLen[RGX_HWPERF_STREAM_ID_LAST];

} RGX_KM_HWPERF_DEVDATA;


PVRSRV_ERROR RGXHWPerfLazyConnect(
		IMG_HANDLE* phDevData)
{
	RGX_KM_HWPERF_DEVDATA* psDevData;

	/* Valid input argument values supplied by the caller */
	if (!phDevData)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Clear the handle to aid error checking by caller */
	*phDevData = NULL;

	/* Check the HWPerf module is initialised before we allow a connection */
	if (!gpsRgxDevNode || !gpsRgxDevInfo)
	{
		return PVRSRV_ERROR_INVALID_DEVICE;
	}

	/* Allocation the session object for this connection */
	psDevData = OSAllocZMem(sizeof(*psDevData));
	if (psDevData == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	psDevData->psRgxDevNode = gpsRgxDevNode;
	psDevData->psRgxDevInfo = gpsRgxDevInfo;

	*phDevData = psDevData;

	return PVRSRV_OK;
}

PVRSRV_ERROR RGXHWPerfOpen(
		IMG_HANDLE hDevData)
{
	PVRSRV_ERROR eError;
	RGX_KM_HWPERF_DEVDATA* psDevData = (RGX_KM_HWPERF_DEVDATA*) hDevData;

	/* Valid input argument values supplied by the caller */
	if (!psDevData)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Check the HWPerf module is initialised before we allow a connection */
	if (!psDevData->psRgxDevNode || !psDevData->psRgxDevInfo)
	{
		return PVRSRV_ERROR_INVALID_DEVICE;
	}

	/* In the case where the AppHint has not been set we need to
	 * initialise the HWPerf resources here. Allocated on-demand
	 * to reduce RAM foot print on systems not needing HWPerf.
	 */
	OSLockAcquire(gpsRgxDevInfo->hHWPerfLock);
	if (RGXHWPerfIsInitRequired())
	{
		eError = RGXHWPerfInitOnDemandResources();
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Initialization of on-demand HWPerfFW"
			        " resources failed", __FUNCTION__));
			OSLockRelease(gpsRgxDevInfo->hHWPerfLock);
			goto e0;
		}
	}
	OSLockRelease(gpsRgxDevInfo->hHWPerfLock);

	OSLockAcquire(gpsRgxDevInfo->hLockHWPerfHostStream);
	if (gpsRgxDevInfo->hHWPerfHostStream == NULL)
	{
		eError = RGXHWPerfHostInitOnDemandResources();
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Initialization of on-demand HWPerfHost"
			        " resources failed", __FUNCTION__));
			OSLockRelease(gpsRgxDevInfo->hLockHWPerfHostStream);
			goto e0;
		}
	}
	OSLockRelease(gpsRgxDevInfo->hLockHWPerfHostStream);

	/* Open the RGX TL stream for reading in this session */
	eError = TLClientOpenStream(DIRECT_BRIDGE_HANDLE,
								PVRSRV_TL_HWPERF_RGX_FW_STREAM,
								PVRSRV_STREAM_FLAG_ACQUIRE_NONBLOCKING,
								&psDevData->hSD[RGX_HWPERF_STREAM_ID0_FW]);
	if (eError != PVRSRV_OK)
	{
		goto e1;
	}

	/* Open the host TL stream for reading in this session */
	eError = TLClientOpenStream(DIRECT_BRIDGE_HANDLE,
								PVRSRV_TL_HWPERF_HOST_SERVER_STREAM,
								PVRSRV_STREAM_FLAG_ACQUIRE_NONBLOCKING,
								&psDevData->hSD[RGX_HWPERF_STREAM_ID1_HOST]);
	if (eError != PVRSRV_OK)
	{
		goto e1;
	}

	return PVRSRV_OK;

e1:
	RGXHWPerfHostDeInit();
e0:
	RGXHWPerfDeinit();

	return eError;
}


PVRSRV_ERROR RGXHWPerfConnect(
		IMG_HANDLE* phDevData)
{
	PVRSRV_ERROR eError;

	eError = RGXHWPerfLazyConnect(phDevData);
	PVR_LOGG_IF_ERROR(eError, "RGXHWPerfLazyConnect", e0);

	eError = RGXHWPerfOpen(*phDevData);
	PVR_LOGG_IF_ERROR(eError, "RGXHWPerfOpen", e1);

	return PVRSRV_OK;

e1:
	RGXHWPerfFreeConnection(*phDevData);
e0:
	*phDevData = NULL;
	return eError;
}


PVRSRV_ERROR RGXHWPerfControl(
		IMG_HANDLE           hDevData,
		RGX_HWPERF_STREAM_ID eStreamId,
		IMG_BOOL             bToggle,
		IMG_UINT64           ui64Mask)
{
	PVRSRV_ERROR           eError;
	RGX_KM_HWPERF_DEVDATA* psDevData = (RGX_KM_HWPERF_DEVDATA*)hDevData;

	/* Valid input argument values supplied by the caller */
	if (!psDevData)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Ensure we are initialised and have a valid device node */
	if (!psDevData->psRgxDevNode)
	{
		return PVRSRV_ERROR_INVALID_DEVICE;
	}

	/* Call the internal server API */
	eError = PVRSRVRGXCtrlHWPerfKM(NULL, psDevData->psRgxDevNode, eStreamId, bToggle, ui64Mask);
	return eError;
}


PVRSRV_ERROR RGXHWPerfConfigureAndEnableCounters(
		IMG_HANDLE					hDevData,
		IMG_UINT32					ui32NumBlocks,
		RGX_HWPERF_CONFIG_CNTBLK*	asBlockConfigs)
{
	PVRSRV_ERROR           eError;
	RGX_KM_HWPERF_DEVDATA* psDevData = (RGX_KM_HWPERF_DEVDATA*)hDevData;

	/* Valid input argument values supplied by the caller */
	if (!psDevData || ui32NumBlocks==0 || !asBlockConfigs)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (ui32NumBlocks > RGXFWIF_HWPERF_CTRL_BLKS_MAX)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Ensure we are initialised and have a valid device node */
	if (!psDevData->psRgxDevNode)
	{
		return PVRSRV_ERROR_INVALID_DEVICE;
	}

	/* Call the internal server API */
	eError = PVRSRVRGXConfigEnableHWPerfCountersKM(NULL,
			psDevData->psRgxDevNode, ui32NumBlocks, asBlockConfigs);
	return eError;
}


PVRSRV_ERROR RGXHWPerfDisableCounters(
		IMG_HANDLE   hDevData,
		IMG_UINT32   ui32NumBlocks,
		IMG_UINT16*   aeBlockIDs)
{
	PVRSRV_ERROR           eError;
	RGX_KM_HWPERF_DEVDATA* psDevData = (RGX_KM_HWPERF_DEVDATA*)hDevData;

	/* Valid input argument values supplied by the caller */
	if (!psDevData || ui32NumBlocks==0 || !aeBlockIDs)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (ui32NumBlocks > RGXFWIF_HWPERF_CTRL_BLKS_MAX)
    {
        return PVRSRV_ERROR_INVALID_PARAMS;
    }

	/* Ensure we are initialised and have a valid device node */
	if (!psDevData->psRgxDevNode)
	{
		return PVRSRV_ERROR_INVALID_DEVICE;
	}

	/* Call the internal server API */
	eError = PVRSRVRGXCtrlHWPerfCountersKM(NULL,
			psDevData->psRgxDevNode, IMG_FALSE, ui32NumBlocks, aeBlockIDs);
	return eError;
}


PVRSRV_ERROR RGXHWPerfAcquireData(
		IMG_HANDLE  hDevData,
		RGX_HWPERF_STREAM_ID eStreamId,
		IMG_PBYTE*  ppBuf,
		IMG_UINT32* pui32BufLen)
{
	PVRSRV_ERROR			eError;
	RGX_KM_HWPERF_DEVDATA*	psDevData = (RGX_KM_HWPERF_DEVDATA*)hDevData;
	IMG_PBYTE				pTlBuf = NULL;
	IMG_UINT32				ui32TlBufLen = 0;
	IMG_PBYTE				pDataDest;
	IMG_UINT32			ui32TlPackets = 0;
	IMG_PBYTE			pBufferEnd;
	PVRSRVTL_PPACKETHDR psHDRptr;
	PVRSRVTL_PACKETTYPE ui16TlType;

	/* Reset the output arguments in case we discover an error */
	*ppBuf = NULL;
	*pui32BufLen = 0;

	/* Valid input argument values supplied by the caller */
	if (!psDevData || eStreamId >= RGX_HWPERF_STREAM_ID_LAST)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Acquire some data to read from the HWPerf TL stream */
	eError = TLClientAcquireData(DIRECT_BRIDGE_HANDLE,
								 psDevData->hSD[eStreamId],
								 &pTlBuf,
								 &ui32TlBufLen);
	PVR_LOGR_IF_ERROR(eError, "TLClientAcquireData");

	/* TL indicates no data exists so return OK and zero. */
	if ((pTlBuf == NULL) || (ui32TlBufLen == 0))
	{
		return PVRSRV_OK;
	}

	/* Is the client buffer allocated and too small? */
	if (psDevData->pHwpBuf[eStreamId] && (psDevData->ui32HwpBufLen[eStreamId] < ui32TlBufLen))
	{
		OSFreeMem(psDevData->pHwpBuf[eStreamId]);
	}

	/* Do we need to allocate a new client buffer? */
	if (!psDevData->pHwpBuf[eStreamId])
	{
		psDevData->pHwpBuf[eStreamId] = OSAllocMem(ui32TlBufLen);
		if (psDevData->pHwpBuf[eStreamId]  == NULL)
		{
			(void) TLClientReleaseData(DIRECT_BRIDGE_HANDLE, psDevData->hSD[eStreamId]);
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}
		psDevData->ui32HwpBufLen[eStreamId] = ui32TlBufLen;
	}

	/* Process each TL packet in the data buffer we have acquired */
	pBufferEnd = pTlBuf+ui32TlBufLen;
	pDataDest = psDevData->pHwpBuf[eStreamId];
	psHDRptr = GET_PACKET_HDR(pTlBuf);
	while ( psHDRptr < (PVRSRVTL_PPACKETHDR)pBufferEnd )
	{
		ui16TlType = GET_PACKET_TYPE(psHDRptr);
		if (ui16TlType == PVRSRVTL_PACKETTYPE_DATA)
		{
			IMG_UINT16 ui16DataLen = GET_PACKET_DATA_LEN(psHDRptr);
			if (0 == ui16DataLen)
			{
				PVR_DPF((PVR_DBG_ERROR, "RGXHWPerfAcquireData: ZERO Data in TL data packet: %p", psHDRptr));
			}
			else
			{
				/* For valid data copy it into the client buffer and move
				 * the write position on */
				OSDeviceMemCopy(pDataDest, GET_PACKET_DATA_PTR(psHDRptr), ui16DataLen);
				pDataDest += ui16DataLen;
			}
		}
		else if (ui16TlType == PVRSRVTL_PACKETTYPE_MOST_RECENT_WRITE_FAILED)
		{
			PVR_DPF((PVR_DBG_MESSAGE, "RGXHWPerfAcquireData: Indication that the transport buffer was full"));
		}
		else
		{
			/* else Ignore padding packet type and others */
			PVR_DPF((PVR_DBG_MESSAGE, "RGXHWPerfAcquireData: Ignoring TL packet, type %d", ui16TlType ));
		}

		/* Update loop variable to the next packet and increment counts */
		psHDRptr = GET_NEXT_PACKET_ADDR(psHDRptr);
		ui32TlPackets++;
	}

	PVR_DPF((PVR_DBG_VERBOSE, "RGXHWPerfAcquireData: TL Packets processed %03d", ui32TlPackets));

	/* Update output arguments with client buffer details and true length */
	*ppBuf = psDevData->pHwpBuf[eStreamId];
	*pui32BufLen = pDataDest - psDevData->pHwpBuf[eStreamId];

	return PVRSRV_OK;
}


PVRSRV_ERROR RGXHWPerfReleaseData(
		IMG_HANDLE hDevData,
		RGX_HWPERF_STREAM_ID eStreamId)
{
	PVRSRV_ERROR			eError;
	RGX_KM_HWPERF_DEVDATA*	psDevData = (RGX_KM_HWPERF_DEVDATA*)hDevData;

	/* Valid input argument values supplied by the caller */
	if (!psDevData || eStreamId >= RGX_HWPERF_STREAM_ID_LAST)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Free the client buffer if allocated and reset length */
	if (psDevData->pHwpBuf[eStreamId])
	{
		OSFreeMem(psDevData->pHwpBuf[eStreamId]);
	}
	psDevData->ui32HwpBufLen[eStreamId] = 0;

	/* Inform the TL that we are done with reading the data. Could perform this
	 * in the acquire call but felt it worth keeping it symmetrical */
	eError = TLClientReleaseData(DIRECT_BRIDGE_HANDLE, psDevData->hSD[eStreamId]);
	return eError;
}


PVRSRV_ERROR RGXHWPerfGetFilter(
		IMG_HANDLE  hDevData,
		RGX_HWPERF_STREAM_ID eStreamId,
		IMG_UINT64 *ui64Filter)
{
	PVRSRV_RGXDEV_INFO* psRgxDevInfo =
			hDevData ? ((RGX_KM_HWPERF_DEVDATA*) hDevData)->psRgxDevInfo : NULL;

	/* Valid input argument values supplied by the caller */
	if (!psRgxDevInfo)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Invalid pointer to the RGX device",
		        __func__));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* No need to take hHWPerfLock here since we are only reading data
	 * from always existing integers to return to debugfs which is an
	 * atomic operation. 
	 */
	switch (eStreamId) {
		case RGX_HWPERF_STREAM_ID0_FW:
			*ui64Filter = psRgxDevInfo->ui64HWPerfFilter;
			break;
		case RGX_HWPERF_STREAM_ID1_HOST:
			*ui64Filter = psRgxDevInfo->ui32HWPerfHostFilter;
			break;
		default:
			PVR_DPF((PVR_DBG_ERROR, "%s: Invalid stream ID",
			        __func__));
			return PVRSRV_ERROR_INVALID_PARAMS;
	}

	return PVRSRV_OK;
}


PVRSRV_ERROR RGXHWPerfFreeConnection(
		IMG_HANDLE hDevData)
{
	RGX_KM_HWPERF_DEVDATA* psDevData = (RGX_KM_HWPERF_DEVDATA*) hDevData;

	/* Check session handle is not zero */
	if (!psDevData)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Free the session memory */
	psDevData->psRgxDevNode = NULL;
	psDevData->psRgxDevInfo = NULL;
	OSFreeMem(psDevData);

	return PVRSRV_OK;
}


PVRSRV_ERROR RGXHWPerfClose(
		IMG_HANDLE hDevData)
{
	RGX_KM_HWPERF_DEVDATA* psDevData = (RGX_KM_HWPERF_DEVDATA*) hDevData;
	IMG_UINT uiStreamId;
	PVRSRV_ERROR eError;

	/* Check session handle is not zero */
	if (!psDevData)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	for (uiStreamId = 0; uiStreamId < RGX_HWPERF_STREAM_ID_LAST; uiStreamId++)
	{
		/* If the client buffer exists they have not called ReleaseData
		 * before disconnecting so clean it up */
		if (psDevData->pHwpBuf[uiStreamId])
		{
			/* RGXHWPerfReleaseData call will null out the buffer fields
			 * and length */
			eError = RGXHWPerfReleaseData(hDevData, uiStreamId);
			PVR_LOG_ERROR(eError, "RGXHWPerfReleaseData");
		}

		/* Close the TL stream, ignore the error if it occurs as we
		 * are disconnecting */
		if (psDevData->hSD[uiStreamId])
		{
			eError = TLClientCloseStream(DIRECT_BRIDGE_HANDLE,
										 psDevData->hSD[uiStreamId]);
			PVR_LOG_ERROR(eError, "TLClientCloseStream");
			psDevData->hSD[uiStreamId] = NULL;
		}
	}

	return PVRSRV_OK;
}


PVRSRV_ERROR RGXHWPerfDisconnect(
		IMG_HANDLE hDevData)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	eError = RGXHWPerfClose(hDevData);
	PVR_LOG_ERROR(eError, "RGXHWPerfClose");

	eError = RGXHWPerfFreeConnection(hDevData);
	PVR_LOG_ERROR(eError, "RGXHWPerfFreeConnection");

	return eError;
}


const IMG_CHAR *RGXHWPerfKickTypeToStr(RGX_HWPERF_KICK_TYPE eKickType)
{
	static const IMG_CHAR *aszKickType[RGX_HWPERF_KICK_TYPE_LAST+1] = {
		"TA3D", "TQ2D", "TQ3D", "CDM", "RS", "VRDM", "TQTDM", "SYNC", "LAST"
	};

	/* cast in case of negative value */
	if (((IMG_UINT32) eKickType) >= RGX_HWPERF_KICK_TYPE_LAST)
	{
		return "<UNKNOWN>";
	}

	return aszKickType[eKickType];
}

/******************************************************************************
 End of file (rgxhwperf.c)
******************************************************************************/
