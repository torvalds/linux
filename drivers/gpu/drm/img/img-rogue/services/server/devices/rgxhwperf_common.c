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

#include "img_defs.h"
#include "pvr_debug.h"
#include "rgxdevice.h"
#include "pvrsrv_error.h"
#include "pvr_notifier.h"
#include "osfunc.h"
#include "allocmem.h"

#include "pvrsrv.h"
#include "pvrsrv_tlstreams.h"
#include "pvrsrv_tlcommon.h"
#include "tlclient.h"
#include "tlstream.h"

#include "rgxhwperf.h"
#include "rgxapi_km.h"
#include "rgxfwutils.h"
#include "rgxtimecorr.h"
#include "devicemem.h"
#include "devicemem_pdump.h"
#include "pdump_km.h"
#include "pvrsrv_apphint.h"
#include "process_stats.h"
#include "rgx_hwperf_table.h"
#include "rgxinit.h"

#include "info_page_defs.h"

/* This is defined by default to enable producer callbacks.
 * Clients of the TL interface can disable the use of the callback
 * with PVRSRV_STREAM_FLAG_DISABLE_PRODUCER_CALLBACK. */
#define SUPPORT_TL_PRODUCER_CALLBACK 1

/* Maximum enum value to prevent access to RGX_HWPERF_STREAM_ID2_CLIENT stream */
#define RGX_HWPERF_MAX_STREAM_ID (RGX_HWPERF_STREAM_ID2_CLIENT)

/* Defines size of buffers returned from acquire/release calls */
#define FW_STREAM_BUFFER_SIZE (0x80000)
#define HOST_STREAM_BUFFER_SIZE (0x20000)

/* Must be at least as large as two tl packets of maximum size */
static_assert(HOST_STREAM_BUFFER_SIZE >= (PVRSRVTL_MAX_PACKET_SIZE<<1),
              "HOST_STREAM_BUFFER_SIZE is less than (PVRSRVTL_MAX_PACKET_SIZE<<1)");
static_assert(FW_STREAM_BUFFER_SIZE >= (PVRSRVTL_MAX_PACKET_SIZE<<1),
              "FW_STREAM_BUFFER_SIZE is less than (PVRSRVTL_MAX_PACKET_SIZE<<1)");

IMG_INTERNAL /*static inline*/ IMG_UINT32 RGXGetHWPerfBlockConfig(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL **);

static inline IMG_UINT32
RGXHWPerfGetPackets(IMG_UINT32  ui32BytesExp,
                    IMG_UINT32  ui32AllowedSize,
                    RGX_PHWPERF_V2_PACKET_HDR psCurPkt )
{
	IMG_UINT32 sizeSum = 0;

	/* Traverse the array to find how many packets will fit in the available space. */
	while ( sizeSum < ui32BytesExp  &&
			sizeSum + RGX_HWPERF_GET_SIZE(psCurPkt) < ui32AllowedSize )
	{
		sizeSum += RGX_HWPERF_GET_SIZE(psCurPkt);
		psCurPkt = RGX_HWPERF_GET_NEXT_PACKET(psCurPkt);
	}

	return sizeSum;
}

static inline void
RGXSuspendHWPerfL2DataCopy(PVRSRV_RGXDEV_INFO* psDeviceInfo,
			   IMG_BOOL bIsReaderConnected)
{
	if (!bIsReaderConnected)
	{
		PVR_DPF((PVR_DBG_WARNING, "%s : HWPerf FW events enabled but host buffer for FW events is full "
		        "and no reader is currently connected, suspending event collection. "
		        "Connect a reader or restart driver to avoid event loss.", __func__));
		psDeviceInfo->bSuspendHWPerfL2DataCopy = IMG_TRUE;
	}
}

/*
	RGXHWPerfCopyDataL1toL2
 */
static IMG_UINT32 RGXHWPerfCopyDataL1toL2(PVRSRV_RGXDEV_INFO* psDeviceInfo,
                                          IMG_BYTE   *pbFwBuffer,
                                          IMG_UINT32 ui32BytesExp)
{
	IMG_HANDLE   hHWPerfStream = psDeviceInfo->hHWPerfStream;
	IMG_BYTE *   pbL2Buffer;
	IMG_UINT32   ui32L2BufFree;
	IMG_UINT32   ui32BytesCopied = 0;
	IMG_UINT32   ui32BytesExpMin = RGX_HWPERF_GET_SIZE(RGX_HWPERF_GET_PACKET(pbFwBuffer));
	PVRSRV_ERROR eError;
	IMG_BOOL     bIsReaderConnected;

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
		} while (pbFwBufferIter < pbFwBufferEnd);
	}
#endif

	if (ui32BytesExp > psDeviceInfo->ui32L2BufMaxPacketSize)
	{
		IMG_UINT32 sizeSum = RGXHWPerfGetPackets(ui32BytesExp,
		                                         psDeviceInfo->ui32L2BufMaxPacketSize,
		                                         RGX_HWPERF_GET_PACKET(pbFwBuffer));

		if (0 != sizeSum)
		{
			ui32BytesExp = sizeSum;
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "Failed to write data into host buffer as "
					"packet is too big and hence it breaches TL "
					"packet size limit (TLBufferSize / 2.5)"));
			goto e0;
		}
	}

	/* Try submitting all data in one TL packet. */
	eError = TLStreamReserve2(hHWPerfStream,
				  &pbL2Buffer,
				  (size_t)ui32BytesExp, ui32BytesExpMin,
				  &ui32L2BufFree, &bIsReaderConnected);
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
	else if (eError == PVRSRV_ERROR_STREAM_FULL)
	{
		/* There was not enough space for all data, copy as much as possible */
		IMG_UINT32 sizeSum = RGXHWPerfGetPackets(ui32BytesExp, ui32L2BufFree, RGX_HWPERF_GET_PACKET(pbFwBuffer));

		PVR_DPF((PVR_DBG_MESSAGE, "Unable to reserve space (%d) in host buffer on first attempt, remaining free space: %d", ui32BytesExp, ui32L2BufFree));

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
			else if ( PVRSRV_ERROR_STREAM_FULL == eError )
			{
				PVR_DPF((PVR_DBG_WARNING, "Cannot write HWPerf packet into host buffer, check data in case of packet loss, remaining free space: %d", ui32L2BufFree));
				RGXSuspendHWPerfL2DataCopy(psDeviceInfo, bIsReaderConnected);
			}
		}
		else
		{
			PVR_DPF((PVR_DBG_MESSAGE, "Cannot find space in host buffer, check data in case of packet loss, remaining free space: %d", ui32L2BufFree));
			RGXSuspendHWPerfL2DataCopy(psDeviceInfo, bIsReaderConnected);
		}
	}
	if ( PVRSRV_OK != eError && /* Some other error occurred */
			PVRSRV_ERROR_STREAM_FULL != eError ) /* Full error handled by caller, we returning the copied bytes count to caller */
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
	return ( ui32Pos + ui32Size < ui32BufSize ? ui32Pos + ui32Size : 0 );
}


/*
	RGXHWPerfDataStore
 */
static IMG_UINT32 RGXHWPerfDataStore(PVRSRV_RGXDEV_INFO	*psDevInfo)
{
	RGXFWIF_SYSDATA	*psFwSysData = psDevInfo->psRGXFWIfFwSysData;
	IMG_BYTE*				psHwPerfInfo = psDevInfo->psRGXFWIfHWPerfBuf;
	IMG_UINT32				ui32SrcRIdx, ui32SrcWIdx, ui32SrcWrapCount;
	IMG_UINT32				ui32BytesExp = 0, ui32BytesCopied = 0, ui32BytesCopiedSum = 0;
#ifdef HWPERF_MISR_FUNC_DEBUG
	IMG_UINT32				ui32BytesExpSum = 0;
#endif

	PVR_DPF_ENTERED;

	/* Caller should check this member is valid before calling */
	PVR_ASSERT(psDevInfo->hHWPerfStream);

	if (psDevInfo->bSuspendHWPerfL2DataCopy)
	{
		PVR_DPF((PVR_DBG_MESSAGE,
		        "%s : Copying data to host buffer for FW events is "
		        "suspended. Start HWPerf consumer or restart driver if "
		        "HWPerf FW events are needed", __func__));

		PVR_DPF_RETURN_VAL(ui32BytesCopiedSum);
	}

	/* Get a copy of the current
	 *   read (first packet to read)
	 *   write (empty location for the next write to be inserted)
	 *   WrapCount (size in bytes of the buffer at or past end)
	 * indexes of the FW buffer */
	ui32SrcRIdx = psFwSysData->ui32HWPerfRIdx;
	ui32SrcWIdx = psFwSysData->ui32HWPerfWIdx;
	OSMemoryBarrier(NULL);
	ui32SrcWrapCount = psFwSysData->ui32HWPerfWrapCount;

#if defined(HWPERF_MISR_FUNC_DEBUG) || defined(EMULATOR)
	{
		IMG_UINT32  ui32SrcBufSize = psDevInfo->ui32RGXFWIfHWPerfBufSize;

		if (ui32SrcRIdx >= ui32SrcBufSize || ui32SrcWIdx >= ui32SrcBufSize)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s : Invalid read/write offsets found! srcRIdx:%u srcWIdx:%u srcBufSize:%u",
			        __func__, ui32SrcRIdx, ui32SrcWIdx, ui32SrcBufSize));

			PVR_DPF_RETURN_VAL(ui32BytesCopiedSum);
		}
	}
#endif

	/* Is there any data in the buffer not yet retrieved? */
	if ( ui32SrcRIdx != ui32SrcWIdx )
	{
		PVR_DPF((PVR_DBG_MESSAGE, "RGXHWPerfDataStore EVENTS found srcRIdx:%d srcWIdx: %d", ui32SrcRIdx, ui32SrcWIdx));

		/* Is the write position higher than the read position? */
		if ( ui32SrcWIdx > ui32SrcRIdx )
		{
			/* Yes, buffer has not wrapped */
			ui32BytesExp = ui32SrcWIdx - ui32SrcRIdx;
#ifdef HWPERF_MISR_FUNC_DEBUG
			ui32BytesExpSum += ui32BytesExp;
#endif
			ui32BytesCopied = RGXHWPerfCopyDataL1toL2(psDevInfo,
			                                          psHwPerfInfo + ui32SrcRIdx,
			                                          ui32BytesExp);

			/* Advance the read index and the free bytes counter by the number
			 * of bytes transported. Items will be left in buffer if not all data
			 * could be transported. Exit to allow buffer to drain. */
			OSWriteDeviceMem32WithWMB(&psFwSysData->ui32HWPerfRIdx,
			                          RGXHWPerfAdvanceRIdx(psDevInfo->ui32RGXFWIfHWPerfBufSize,
			                                               ui32SrcRIdx,
			                                               ui32BytesCopied));

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
			ui32BytesCopied = RGXHWPerfCopyDataL1toL2(psDevInfo,
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
				OSWriteDeviceMem32WithWMB(&psFwSysData->ui32HWPerfWrapCount,
				                          psDevInfo->ui32RGXFWIfHWPerfBufSize);
			}
			OSWriteDeviceMem32WithWMB(&psFwSysData->ui32HWPerfRIdx, ui32SrcRIdx);

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
				ui32BytesCopied = RGXHWPerfCopyDataL1toL2(psDevInfo,
				                                          psHwPerfInfo,
				                                          ui32BytesExp);
				/* Advance the FW buffer read position. */
				psFwSysData->ui32HWPerfRIdx = RGXHWPerfAdvanceRIdx(
						psDevInfo->ui32RGXFWIfHWPerfBufSize, ui32SrcRIdx,
						ui32BytesCopied);

				ui32BytesCopiedSum += ui32BytesCopied;
			}
		}
#ifdef HWPERF_MISR_FUNC_DEBUG
		if (ui32BytesCopiedSum != ui32BytesExpSum)
		{
			PVR_DPF((PVR_DBG_WARNING, "RGXHWPerfDataStore: FW L1 RIdx:%u. Not all bytes copied to L2: %u bytes out of %u expected", psFwSysData->ui32HWPerfRIdx, ui32BytesCopiedSum, ui32BytesExpSum));
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

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_OK);

	PVR_DPF_ENTERED;

	PVR_ASSERT(psDevInfo);
	psRgxDevInfo = psDevInfo->pvDevice;

	/* Store FW event data if the destination buffer exists.*/
	if (psRgxDevInfo->hHWPerfStream != (IMG_HANDLE) NULL)
	{
		OSLockAcquire(psRgxDevInfo->hHWPerfLock);
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
		OSLockRelease(psRgxDevInfo->hHWPerfLock);
	}


	PVR_DPF_RETURN_OK;
}


/* Currently supported by default */
#if defined(SUPPORT_TL_PRODUCER_CALLBACK)
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

			/* This solution is for avoiding a deadlock situation where -
			 * in DoTLStreamReserve(), writer has acquired HWPerfLock and
			 * ReadLock and is waiting on ReadPending (which will be reset
			 * by reader), And
			 * the reader after setting ReadPending in TLStreamAcquireReadPos(),
			 * is waiting for HWPerfLock in RGXHWPerfTLCB().
			 * So here in RGXHWPerfTLCB(), if HWPerfLock is already acquired we
			 * will return to the reader without waiting to acquire HWPerfLock.
			 */
			if (!OSTryLockAcquire(psRgxDevInfo->hHWPerfLock))
			{
				PVR_DPF((PVR_DBG_MESSAGE, "hHWPerfLock is already acquired, a write "
						"operation might already be in process"));
				return PVRSRV_OK;
			}

			if (psRgxDevInfo->hHWPerfStream != (IMG_HANDLE) NULL)
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


static void RGXHWPerfL1BufferDeinit(PVRSRV_RGXDEV_INFO *psRgxDevInfo)
{
	if (psRgxDevInfo->psRGXFWIfHWPerfBufMemDesc)
	{
		if (psRgxDevInfo->psRGXFWIfHWPerfBuf != NULL)
		{
			DevmemReleaseCpuVirtAddr(psRgxDevInfo->psRGXFWIfHWPerfBufMemDesc);
			psRgxDevInfo->psRGXFWIfHWPerfBuf = NULL;
		}
		DevmemFwUnmapAndFree(psRgxDevInfo, psRgxDevInfo->psRGXFWIfHWPerfBufMemDesc);
		psRgxDevInfo->psRGXFWIfHWPerfBufMemDesc = NULL;
	}
}

/*************************************************************************/ /*!
@Function       RGXHWPerfInit

@Description    Called during driver init for initialization of HWPerf module
				in the Rogue device driver. This function keeps allocated
				only the minimal necessary resources, which are required for
				functioning of HWPerf server module.

@Input          psRgxDevInfo	RGX Device Info

@Return			PVRSRV_ERROR
 */ /**************************************************************************/
PVRSRV_ERROR RGXHWPerfInit(PVRSRV_RGXDEV_INFO *psRgxDevInfo)
{
	PVRSRV_ERROR eError;

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_OK);

	PVR_DPF_ENTERED;

	/* expecting a valid device info */
	PVR_RETURN_IF_INVALID_PARAM(psRgxDevInfo != NULL);

	/* Create a lock for HWPerf server module used for serializing, L1 to L2
	 * copy calls (e.g. in case of TL producer callback) and L1, L2 resource
	 * allocation */
	eError = OSLockCreate(&psRgxDevInfo->hHWPerfLock);
	PVR_LOG_RETURN_IF_ERROR(eError, "OSLockCreate");

	/* avoid uninitialised data */
	psRgxDevInfo->hHWPerfStream = (IMG_HANDLE) NULL;
	psRgxDevInfo->psRGXFWIfHWPerfBufMemDesc = NULL;

	PVR_DPF_RETURN_OK;
}

/*************************************************************************/ /*!
@Function       RGXHWPerfIsInitRequired

@Description    Returns true if the HWperf firmware buffer (L1 buffer) and host
                driver TL buffer (L2 buffer) are not already allocated. Caller
                must possess hHWPerfLock lock before calling this
                function so the state tested is not inconsistent.

@Input          psRgxDevInfo RGX Device Info, on which init requirement is
                checked.

@Return         IMG_BOOL	Whether initialization (allocation) is required
 */ /**************************************************************************/
static INLINE IMG_BOOL RGXHWPerfIsInitRequired(PVRSRV_RGXDEV_INFO *psRgxDevInfo)
{
	PVR_ASSERT(OSLockIsLocked(psRgxDevInfo->hHWPerfLock));

#if !defined(NO_HARDWARE)
	/* Both L1 and L2 buffers are required (for HWPerf functioning) on driver
	 * built for actual hardware (TC, EMU, etc.)
	 */
	if (psRgxDevInfo->hHWPerfStream == (IMG_HANDLE) NULL)
	{
		/* The allocation API (RGXHWPerfInitOnDemandResources) allocates
		 * device memory for both L1 and L2 without any checks. Hence,
		 * either both should be allocated or both be NULL.
		 *
		 * In-case this changes in future (for e.g. a situation where one
		 * of the 2 buffers is already allocated and other is required),
		 * add required checks before allocation calls to avoid memory leaks.
		 */
		PVR_ASSERT(psRgxDevInfo->psRGXFWIfHWPerfBufMemDesc == NULL);
		return IMG_TRUE;
	}
	PVR_ASSERT(psRgxDevInfo->psRGXFWIfHWPerfBufMemDesc != NULL);
#else
	/* On a NO-HW driver L2 is not allocated. So, no point in checking its
	 * allocation */
	if (psRgxDevInfo->psRGXFWIfHWPerfBufMemDesc == NULL)
	{
		return IMG_TRUE;
	}
#endif
	return IMG_FALSE;
}
#if !defined(NO_HARDWARE)
static void _HWPerfFWOnReaderOpenCB(void *pvArg)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_RGXDEV_INFO* psRgxDevInfo = (PVRSRV_RGXDEV_INFO*) pvArg;
	PVRSRV_DEVICE_NODE* psDevNode = (PVRSRV_DEVICE_NODE*) psRgxDevInfo->psDeviceNode;
	RGXFWIF_KCCB_CMD sKccbCmd;
	IMG_UINT32 ui32kCCBCommandSlot;

	PVRSRV_VZ_RETN_IF_MODE(GUEST);

	/* Clear any previously suspended state for bSuspendHWPerfL2DataCopy as we
	 * now have a reader attached so the data will be delivered upstream. */
	if (psRgxDevInfo->bSuspendHWPerfL2DataCopy)
	{
		PVR_DPF((PVR_DBG_WARNING, "%s: Resuming HWPerf FW event collection.",
		        __func__));
		psRgxDevInfo->bSuspendHWPerfL2DataCopy = IMG_FALSE;
	}

	sKccbCmd.eCmdType = RGXFWIF_KCCB_CMD_HWPERF_UPDATE_CONFIG;
	sKccbCmd.uCmdData.sHWPerfCtrl.eOpCode = RGXFWIF_HWPERF_CTRL_EMIT_FEATURES_EV;
	sKccbCmd.uCmdData.sHWPerfCtrl.ui64Mask = 0;

	eError = RGXScheduleCommandAndGetKCCBSlot(psDevNode->pvDevice,
											  RGXFWIF_DM_GP,
											  &sKccbCmd,
											  PDUMP_FLAGS_CONTINUOUS,
											  &ui32kCCBCommandSlot);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to generate feature packet in "
				"firmware (error = %d)", __func__, eError));
		return;
	}

	eError = RGXWaitForKCCBSlotUpdate(psRgxDevInfo, ui32kCCBCommandSlot, PDUMP_FLAGS_CONTINUOUS);
	PVR_LOG_RETURN_VOID_IF_ERROR(eError, "RGXWaitForKCCBSlotUpdate");
}
#endif
/*************************************************************************/ /*!
@Function       RGXHWPerfInitOnDemandResources

@Description    This function allocates the HWperf firmware buffer (L1 buffer)
                and host driver TL buffer (L2 buffer) if HWPerf is enabled at
                driver load time. Otherwise, these buffers are allocated
                on-demand as and when required. Caller
                must possess hHWPerfLock lock before calling this
                function so the state tested is not inconsistent if called
                outside of driver initialisation.

@Input          psRgxDevInfo RGX Device Info, on which init is done

@Return         PVRSRV_ERROR
 */ /**************************************************************************/
PVRSRV_ERROR RGXHWPerfInitOnDemandResources(PVRSRV_RGXDEV_INFO* psRgxDevInfo)
{
	IMG_HANDLE hStream = NULL; /* Init required for noHW */
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32L2BufferSize = 0;
	PVRSRV_MEMALLOCFLAGS_T uiMemAllocFlags;
	IMG_CHAR pszHWPerfStreamName[sizeof(PVRSRV_TL_HWPERF_RGX_FW_STREAM) + 5]; /* 5 seems reasonable as it can hold
																			  names up to "hwperf_9999", which is enough */

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_IMPLEMENTED);

	PVR_DPF_ENTERED;

	/* Create the L1 HWPerf buffer on demand, read-only for the CPU
	 * (except for the zero/poison operations) */
	uiMemAllocFlags = PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT)
						| PVRSRV_MEMALLOCFLAG_GPU_READABLE
						| PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE
						| PVRSRV_MEMALLOCFLAG_GPU_UNCACHED
						| PVRSRV_MEMALLOCFLAG_CPU_READABLE
						| PVRSRV_MEMALLOCFLAG_CPU_UNCACHED_WC
						| PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE
#if defined(PDUMP) /* Helps show where the packet data ends */
						| PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC
#else /* Helps show corruption issues in driver-live */
						| PVRSRV_MEMALLOCFLAG_POISON_ON_ALLOC
#endif
						| PVRSRV_MEMALLOCFLAG_PHYS_HEAP_HINT(FW_MAIN);

	/* Allocate HWPerf FW L1 buffer */
	eError = DevmemFwAllocate(psRgxDevInfo,
	                          /* Pad it enough to hold the biggest variable sized packet. */
	                          psRgxDevInfo->ui32RGXFWIfHWPerfBufSize+RGX_HWPERF_MAX_PACKET_SIZE,
	                          uiMemAllocFlags,
	                          "FwHWPerfBuffer",
	                          &psRgxDevInfo->psRGXFWIfHWPerfBufMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: Failed to allocate kernel fw hwperf buffer (%u)",
				__func__, eError));
		goto e0;
	}

	/* Expecting the RuntimeCfg structure is mapped into CPU virtual memory.
	 * Also, make sure the FW address is not already set */
	PVR_ASSERT(psRgxDevInfo->psRGXFWIfRuntimeCfg && psRgxDevInfo->psRGXFWIfRuntimeCfg->sHWPerfBuf.ui32Addr == 0x0);

	/* Meta cached flag removed from this allocation as it was found
	 * FW performance was better without it. */
	eError = RGXSetFirmwareAddress(&psRgxDevInfo->psRGXFWIfRuntimeCfg->sHWPerfBuf,
	                      psRgxDevInfo->psRGXFWIfHWPerfBufMemDesc,
	                      0, RFW_FWADDR_NOREF_FLAG);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXSetFirmwareAddress", e0);

#if defined(RGX_FEATURE_HWPERF_VOLCANIC)
	RGXSetMetaDMAAddress(&psRgxDevInfo->psRGXFWIfRuntimeCfg->sHWPerfDMABuf,
	                     psRgxDevInfo->psRGXFWIfHWPerfBufMemDesc,
	                     &psRgxDevInfo->psRGXFWIfRuntimeCfg->sHWPerfBuf,
	                     0);
#endif

	/* flush write buffers for psRgxDevInfo->psRGXFWIfRuntimeCfg */
	OSWriteMemoryBarrier(&psRgxDevInfo->psRGXFWIfRuntimeCfg->sHWPerfBuf.ui32Addr);

	eError = DevmemAcquireCpuVirtAddr(psRgxDevInfo->psRGXFWIfHWPerfBufMemDesc,
	                                  (void**)&psRgxDevInfo->psRGXFWIfHWPerfBuf);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: Failed to acquire kernel hwperf buffer (%u)",
				__func__, eError));
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
	ui32L2BufferSize = psRgxDevInfo->ui32RGXFWIfHWPerfBufSize +
			(psRgxDevInfo->ui32RGXFWIfHWPerfBufSize>>1);

	/* form the HWPerf stream name, corresponding to this DevNode; which can make sense in the UM */
	if (OSSNPrintf(pszHWPerfStreamName, sizeof(pszHWPerfStreamName), "%s%d",
	               PVRSRV_TL_HWPERF_RGX_FW_STREAM,
	               psRgxDevInfo->psDeviceNode->sDevId.i32OsDeviceID) < 0)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: Failed to form HWPerf stream name for device %d",
				__func__,
				psRgxDevInfo->psDeviceNode->sDevId.i32OsDeviceID));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = TLStreamCreate(&hStream,
	                        pszHWPerfStreamName,
	                        ui32L2BufferSize,
	                        TL_OPMODE_DROP_NEWER | TL_FLAG_NO_SIGNAL_ON_COMMIT,
	                        _HWPerfFWOnReaderOpenCB, psRgxDevInfo,
#if !defined(SUPPORT_TL_PRODUCER_CALLBACK)
	                        NULL, NULL
#else
	                        /* Not enabled by default */
	                        RGXHWPerfTLCB, psRgxDevInfo
#endif
	                        );
	PVR_LOG_GOTO_IF_ERROR(eError, "TLStreamCreate", e1);

	eError = TLStreamSetNotifStream(hStream,
	                                PVRSRVGetPVRSRVData()->hTLCtrlStream);
	/* we can still discover host stream so leave it as is and just log error */
	PVR_LOG_IF_ERROR(eError, "TLStreamSetNotifStream");

	/* send the event here because host stream is implicitly opened for write
	 * in TLStreamCreate and TLStreamOpen is never called (so the event is
	 * never emitted) */
	TLStreamMarkStreamOpen(hStream);

	{
		TL_STREAM_INFO sTLStreamInfo;

		TLStreamInfo(hStream, &sTLStreamInfo);
		psRgxDevInfo->ui32L2BufMaxPacketSize = sTLStreamInfo.maxTLpacketSize;

		psRgxDevInfo->bSuspendHWPerfL2DataCopy = IMG_FALSE;
	}

	PVR_DPF((PVR_DBG_MESSAGE, "HWPerf buffer size in bytes: L1: %d  L2: %d",
			psRgxDevInfo->ui32RGXFWIfHWPerfBufSize, ui32L2BufferSize));

#else /* defined(NO_HARDWARE) */
	PVR_UNREFERENCED_PARAMETER(ui32L2BufferSize);
	PVR_UNREFERENCED_PARAMETER(RGXHWPerfTLCB);
	PVR_UNREFERENCED_PARAMETER(pszHWPerfStreamName);
	ui32L2BufferSize = 0;
#endif

	psRgxDevInfo->hHWPerfStream = hStream;
	PVR_DPF_RETURN_OK;

#if !defined(NO_HARDWARE)
e1: /* L2 buffer initialisation failures */
	psRgxDevInfo->hHWPerfStream = NULL;
#endif
e0: /* L1 buffer initialisation failures */
	RGXHWPerfL1BufferDeinit(psRgxDevInfo);

	PVR_DPF_RETURN_RC(eError);
}


void RGXHWPerfDeinit(PVRSRV_RGXDEV_INFO *psRgxDevInfo)
{
	IMG_HANDLE hStream = psRgxDevInfo->hHWPerfStream;

	PVRSRV_VZ_RETN_IF_MODE(GUEST);

	PVR_DPF_ENTERED;

	PVR_ASSERT(psRgxDevInfo);
	psRgxDevInfo->hHWPerfStream = NULL;

	/* Clean up the L2 buffer stream object if allocated */
	if (hStream)
	{
		/* send the event here because host stream is implicitly opened for
		 * write in TLStreamCreate and TLStreamClose is never called (so the
		 * event is never emitted) */
		TLStreamMarkStreamClose(hStream);
		TLStreamClose(hStream);
	}

	/* Cleanup L1 buffer resources */
	RGXHWPerfL1BufferDeinit(psRgxDevInfo);

	/* Cleanup the HWPerf server module lock resource */
	if (psRgxDevInfo->hHWPerfLock)
	{
		OSLockDestroy(psRgxDevInfo->hHWPerfLock);
		psRgxDevInfo->hHWPerfLock = NULL;
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
	IMG_UINT32 ui32kCCBCommandSlot;

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_SUPPORTED);

	/* If this method is being used whether to enable or disable
	 * then the hwperf buffers (host and FW) are likely to be needed
	 * eventually so create them, also helps unit testing. Buffers
	 * allocated on demand to reduce RAM foot print on systems not
	 * needing HWPerf resources.
	 * Obtain lock first, test and init if required. */
	OSLockAcquire(psDevice->hHWPerfLock);

	if (!psDevice->bFirmwareInitialised)
	{
		psDevice->ui64HWPerfFilter = ui64Mask; // at least set filter
		eError = PVRSRV_ERROR_NOT_INITIALISED;

		PVR_DPF((PVR_DBG_ERROR,
				 "HWPerf has NOT been initialised yet. Mask has been SET to "
				 "(%" IMG_UINT64_FMTSPECx ")",
				 ui64Mask));

		goto unlock_and_return;
	}

	if (RGXHWPerfIsInitRequired(psDevice))
	{
		eError = RGXHWPerfInitOnDemandResources(psDevice);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Initialisation of on-demand HWPerfFW "
					"resources failed", __func__));
			goto unlock_and_return;
		}
	}

#if defined(RGX_FEATURE_HWPERF_VOLCANIC) && defined(SUPPORT_POWMON_COMPONENT) && defined(SUPPORT_POWER_VALIDATION_VIA_DEBUGFS)
	if (RGXPowmonBufferIsInitRequired(psDeviceNode->pvDevice))
	{
		/* Allocate power monitoring log buffer if enabled */
		eError = RGXPowmonBufferInitOnDemandResources(psDeviceNode->pvDevice);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Initialisation of on-demand power monitoring "
					"resources failed", __func__));
			goto unlock_and_return;
		}
	}
#endif

	/* Unlock here as no further HWPerf resources are used below that would be
	 * affected if freed by another thread */
	OSLockRelease(psDevice->hHWPerfLock);

	/* Return if the filter is the same */
	if (!bToggle && psDevice->ui64HWPerfFilter == ui64Mask)
		goto return_;

	/* Prepare command parameters ... */
	sKccbCmd.eCmdType = RGXFWIF_KCCB_CMD_HWPERF_UPDATE_CONFIG;
	sKccbCmd.uCmdData.sHWPerfCtrl.eOpCode = bToggle ? RGXFWIF_HWPERF_CTRL_TOGGLE : RGXFWIF_HWPERF_CTRL_SET;
	sKccbCmd.uCmdData.sHWPerfCtrl.ui64Mask = ui64Mask;

	/* Ask the FW to carry out the HWPerf configuration command */
	eError = RGXScheduleCommandAndGetKCCBSlot(psDevice,
	                                          RGXFWIF_DM_GP,
	                                          &sKccbCmd,
	                                          IMG_TRUE,
	                                          &ui32kCCBCommandSlot);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to set new HWPerfFW filter in "
				"firmware (error = %d)", __func__, eError));
		goto return_;
	}

	psDevice->ui64HWPerfFilter = bToggle ?
			psDevice->ui64HWPerfFilter ^ ui64Mask : ui64Mask;

	/* Wait for FW to complete */
	eError = RGXWaitForKCCBSlotUpdate(psDevice, ui32kCCBCommandSlot, PDUMP_FLAGS_CONTINUOUS);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXWaitForKCCBSlotUpdate", return_);

#if defined(DEBUG)
	if (bToggle)
	{
		PVR_DPF((PVR_DBG_WARNING, "HWPerfFW events (%" IMG_UINT64_FMTSPECx ") have been TOGGLED",
				ui64Mask));
	}
	else
	{
		PVR_DPF((PVR_DBG_WARNING, "HWPerfFW mask has been SET to (%" IMG_UINT64_FMTSPECx ")",
				ui64Mask));
	}
#endif

	return PVRSRV_OK;

unlock_and_return:
	OSLockRelease(psDevice->hHWPerfLock);

return_:
	return eError;
}

#define HWPERF_HOST_MAX_DEFERRED_PACKETS 800

static PVRSRV_ERROR RGXHWPerfCtrlHostBuffer(const PVRSRV_DEVICE_NODE *psDeviceNode,
                                            IMG_BOOL bToggle,
                                            IMG_UINT32 ui32Mask)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_RGXDEV_INFO* psDevice = psDeviceNode->pvDevice;
#if defined(PVRSRV_HWPERF_HOST_DEBUG_DEFERRED_EVENTS)
	IMG_UINT32 ui32OldFilter = psDevice->ui32HWPerfHostFilter;
#endif

	OSLockAcquire(psDevice->hLockHWPerfHostStream);
	if (psDevice->hHWPerfHostStream == NULL)
	{
		eError = RGXHWPerfHostInitOnDemandResources(psDevice);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "%s: Initialisation of on-demand HWPerfHost resources failed",
					 __func__));
			OSLockRelease(psDevice->hLockHWPerfHostStream);
			return eError;
		}
	}

	psDevice->ui32HWPerfHostFilter = bToggle ?
			psDevice->ui32HWPerfHostFilter ^ ui32Mask : ui32Mask;

	// Deferred creation of host periodic events thread
	if (psDevice->ui32HWPerfHostFilter & RGX_HWPERF_EVENT_MASK_VALUE(RGX_HWPERF_HOST_INFO))
	{
		eError = PVRSRVCreateHWPerfHostThread(PVRSRV_APPHINT_HWPERFHOSTTHREADTIMEOUTINMS);
		PVR_LOG_IF_ERROR(eError, "PVRSRVCreateHWPerfHostThread");
	}
	else if (!(psDevice->ui32HWPerfHostFilter & RGX_HWPERF_EVENT_MASK_VALUE(RGX_HWPERF_HOST_INFO)))
	{
		eError = PVRSRVDestroyHWPerfHostThread();
		PVR_LOG_IF_ERROR(eError, "PVRSRVDestroyHWPerfHostThread");
	}

#if defined(PVRSRV_HWPERF_HOST_DEBUG_DEFERRED_EVENTS)
	// Log deferred events stats if filter changed from non-zero to zero
	if ((ui32OldFilter != 0) && (psDevice->ui32HWPerfHostFilter == 0))
	{
		PVR_LOG(("HWPerfHost deferred events buffer high-watermark / size: (%u / %u)",
				psDevice->ui32DEHighWatermark, HWPERF_HOST_MAX_DEFERRED_PACKETS));

		PVR_LOG(("HWPerfHost deferred event retries: WaitForAtomicCtxPktHighWatermark(%u) "
				"WaitForRightOrdPktHighWatermark(%u)",
				psDevice->ui32WaitForAtomicCtxPktHighWatermark,
				psDevice->ui32WaitForRightOrdPktHighWatermark));
	}
#endif

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

static PVRSRV_ERROR RGXHWPerfCtrlClientBuffer(IMG_BOOL bToggle,
                                              IMG_UINT32 ui32InfoPageIdx,
                                              IMG_UINT32 ui32Mask)
{
	PVRSRV_DATA *psData = PVRSRVGetPVRSRVData();

	PVR_LOG_RETURN_IF_FALSE(ui32InfoPageIdx >= HWPERF_INFO_IDX_START &&
	                  ui32InfoPageIdx < HWPERF_INFO_IDX_END, "invalid info"
	                  " page index", PVRSRV_ERROR_INVALID_PARAMS);

	OSLockAcquire(psData->hInfoPageLock);
	psData->pui32InfoPage[ui32InfoPageIdx] = bToggle ?
			psData->pui32InfoPage[ui32InfoPageIdx] ^ ui32Mask : ui32Mask;
	OSLockRelease(psData->hInfoPageLock);

#if defined(DEBUG)
	if (bToggle)
	{
		PVR_DPF((PVR_DBG_WARNING, "HWPerfClient (%u) events (%x) have been TOGGLED",
				ui32InfoPageIdx, ui32Mask));
	}
	else
	{
		PVR_DPF((PVR_DBG_WARNING, "HWPerfClient (%u) mask has been SET to (%x)",
				ui32InfoPageIdx, ui32Mask));
	}
#endif

	return PVRSRV_OK;
}

PVRSRV_ERROR PVRSRVRGXGetHWPerfBvncFeatureFlagsKM(CONNECTION_DATA    *psConnection,
                                                  PVRSRV_DEVICE_NODE *psDeviceNode,
                                                  RGX_HWPERF_BVNC    *psBVNC)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;
	PVRSRV_ERROR        eError;

	PVR_LOG_RETURN_IF_FALSE((NULL != psDeviceNode), "psDeviceNode invalid", PVRSRV_ERROR_INVALID_PARAMS);

	psDevInfo = psDeviceNode->pvDevice;
	eError = RGXServerFeatureFlagsToHWPerfFlags(psDevInfo, psBVNC);

	return eError;
}

/*
	AppHint interfaces
 */
static
PVRSRV_ERROR RGXHWPerfSetFwFilter(const PVRSRV_DEVICE_NODE *psDeviceNode,
                                  const void *psPrivate,
                                  IMG_UINT64 ui64Value)
{
	PVRSRV_ERROR eError;
	PVRSRV_RGXDEV_INFO *psDeviceInfo;

	PVR_UNREFERENCED_PARAMETER(psPrivate);

	PVR_RETURN_IF_INVALID_PARAM(psDeviceNode != NULL);
	PVR_RETURN_IF_INVALID_PARAM(psDeviceNode->pvDevice != NULL);

	psDeviceInfo = psDeviceNode->pvDevice;

	eError = RGXHWPerfCtrlFwBuffer(psDeviceNode, IMG_FALSE, ui64Value);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "Failed to set HWPerf firmware filter for device (%u)",
		        psDeviceNode->sDevId.ui32InternalID));
		return eError;
	}

	return PVRSRV_OK;
}

static
PVRSRV_ERROR RGXHWPerfReadFwFilter(const PVRSRV_DEVICE_NODE *psDeviceNode,
                                   const void *psPrivate,
                                   IMG_UINT64 *pui64Value)
{
	PVR_RETURN_IF_INVALID_PARAM(psDeviceNode != NULL);
	PVR_RETURN_IF_INVALID_PARAM(psDeviceNode->pvDevice != NULL);

	PVR_UNREFERENCED_PARAMETER(psPrivate);

	*pui64Value =
	    ((PVRSRV_RGXDEV_INFO *) psDeviceNode->pvDevice)->ui64HWPerfFilter;

	return PVRSRV_OK;
}

static
PVRSRV_ERROR RGXHWPerfSetHostFilter(const PVRSRV_DEVICE_NODE *psDeviceNode,
                                    const void *psPrivate,
                                    IMG_UINT32 ui32Value)
{
	PVRSRV_ERROR eError;

	PVR_RETURN_IF_INVALID_PARAM(psDeviceNode != NULL);
	PVR_RETURN_IF_INVALID_PARAM(psDeviceNode->pvDevice != NULL);

	PVR_UNREFERENCED_PARAMETER(psPrivate);

	eError = RGXHWPerfCtrlHostBuffer(psDeviceNode, IMG_FALSE, ui32Value);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "Failed to set HWPerf firmware filter for device (%u)",
		        psDeviceNode->sDevId.ui32InternalID));
		return eError;
	}

	return PVRSRV_OK;
}

static
PVRSRV_ERROR RGXHWPerfReadHostFilter(const PVRSRV_DEVICE_NODE *psDeviceNode,
                                     const void *psPrivate,
                                     IMG_UINT32 *pui32Value)
{
	PVR_RETURN_IF_INVALID_PARAM(psDeviceNode != NULL);
	PVR_RETURN_IF_INVALID_PARAM(psDeviceNode->pvDevice != NULL);

	PVR_UNREFERENCED_PARAMETER(psPrivate);

	*pui32Value =
	    ((PVRSRV_RGXDEV_INFO *) psDeviceNode->pvDevice)->ui32HWPerfHostFilter;

	return PVRSRV_OK;
}

static PVRSRV_ERROR _ReadClientFilter(const PVRSRV_DEVICE_NODE *psDevice,
                                      const void *psPrivData,
                                      IMG_UINT32 *pui32Value)
{
	PVRSRV_DATA *psData = PVRSRVGetPVRSRVData();
	IMG_UINT32 ui32Idx = (IMG_UINT32) (uintptr_t) psPrivData;
	PVR_UNREFERENCED_PARAMETER(psDevice);

	OSLockAcquire(psData->hInfoPageLock);
	*pui32Value = psData->pui32InfoPage[ui32Idx];
	OSLockRelease(psData->hInfoPageLock);

	return PVRSRV_OK;
}

static PVRSRV_ERROR _WriteClientFilter(const PVRSRV_DEVICE_NODE *psDevice,
                                       const void *psPrivData,
                                       IMG_UINT32 ui32Value)
{
	IMG_UINT32 ui32Idx = (IMG_UINT32) (uintptr_t) psPrivData;
	PVR_UNREFERENCED_PARAMETER(psDevice);

	return RGXHWPerfCtrlClientBuffer(IMG_FALSE, ui32Idx, ui32Value);
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

void RGXHWPerfClientInitAppHintCallbacks(void)
{
	PVRSRVAppHintRegisterHandlersUINT32(APPHINT_ID_HWPerfClientFilter_Services,
	                                    _ReadClientFilter,
	                                    _WriteClientFilter,
	                                    APPHINT_OF_DRIVER_NO_DEVICE,
	                                    (void *) HWPERF_FILTER_SERVICES_IDX);
	PVRSRVAppHintRegisterHandlersUINT32(APPHINT_ID_HWPerfClientFilter_EGL,
	                                    _ReadClientFilter,
	                                    _WriteClientFilter,
	                                    APPHINT_OF_DRIVER_NO_DEVICE,
	                                    (void *) HWPERF_FILTER_EGL_IDX);
	PVRSRVAppHintRegisterHandlersUINT32(APPHINT_ID_HWPerfClientFilter_OpenGLES,
	                                    _ReadClientFilter,
	                                    _WriteClientFilter,
	                                    APPHINT_OF_DRIVER_NO_DEVICE,
	                                    (void *) HWPERF_FILTER_OPENGLES_IDX);
	PVRSRVAppHintRegisterHandlersUINT32(APPHINT_ID_HWPerfClientFilter_OpenCL,
	                                    _ReadClientFilter,
	                                    _WriteClientFilter,
	                                    APPHINT_OF_DRIVER_NO_DEVICE,
	                                    (void *) HWPERF_FILTER_OPENCL_IDX);
	PVRSRVAppHintRegisterHandlersUINT32(APPHINT_ID_HWPerfClientFilter_Vulkan,
	                                    _ReadClientFilter,
	                                    _WriteClientFilter,
	                                    APPHINT_OF_DRIVER_NO_DEVICE,
	                                    (void *) HWPERF_FILTER_VULKAN_IDX);
	PVRSRVAppHintRegisterHandlersUINT32(APPHINT_ID_HWPerfClientFilter_OpenGL,
		                                _ReadClientFilter,
		                                _WriteClientFilter,
		                                APPHINT_OF_DRIVER_NO_DEVICE,
		                                (void *) HWPERF_FILTER_OPENGL_IDX);
}

static INLINE IMG_UINT32 _RGXHWPerfFixBufferSize(IMG_UINT32 ui32BufSizeKB)
{
	if (ui32BufSizeKB > HWPERF_HOST_TL_STREAM_SIZE_MAX)
	{
		/* Size specified as a AppHint but it is too big */
		PVR_DPF((PVR_DBG_WARNING,
		         "RGXHWPerfHostInit: HWPerf Host buffer size "
		         "value (%u) too big, using maximum (%u)",
		         ui32BufSizeKB, HWPERF_HOST_TL_STREAM_SIZE_MAX));
		return HWPERF_HOST_TL_STREAM_SIZE_MAX<<10;
	}
	else if (ui32BufSizeKB >= HWPERF_HOST_TL_STREAM_SIZE_MIN)
	{
		return ui32BufSizeKB<<10;
	}
	else if (ui32BufSizeKB > 0)
	{
		/* Size specified as a AppHint but it is too small */
		PVR_DPF((PVR_DBG_WARNING,
		         "RGXHWPerfHostInit: HWPerf Host buffer size "
		         "value (%u) too small, using minimum (%u)",
		         ui32BufSizeKB, HWPERF_HOST_TL_STREAM_SIZE_MIN));
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
PVRSRV_ERROR RGXHWPerfHostInit(PVRSRV_RGXDEV_INFO *psRgxDevInfo, IMG_UINT32 ui32BufSizeKB)
{
	PVRSRV_ERROR eError;

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_OK);

	PVR_RETURN_IF_INVALID_PARAM(psRgxDevInfo != NULL);

	eError = OSLockCreate(&psRgxDevInfo->hLockHWPerfHostStream);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSLockCreate", error);

	psRgxDevInfo->hHWPerfHostStream = NULL;
	psRgxDevInfo->ui32HWPerfHostFilter = 0; /* disable all events */
	psRgxDevInfo->ui32HWPerfHostNextOrdinal = 1;
	psRgxDevInfo->ui32HWPerfHostBufSize = _RGXHWPerfFixBufferSize(ui32BufSizeKB);
	psRgxDevInfo->pvHostHWPerfMISR = NULL;
	psRgxDevInfo->pui8DeferredEvents = NULL;
	/* First packet has ordinal=1, so LastOrdinal=0 will ensure ordering logic
	 * is maintained */
	psRgxDevInfo->ui32HWPerfHostLastOrdinal = 0;
	psRgxDevInfo->hHWPerfHostSpinLock = NULL;

error:
	return eError;
}

#define RGX_HWPERF_HOST_CLIENT_INFO_PROC_NAME_BASE_SIZE \
	((IMG_UINT32)(offsetof(RGX_HWPERF_HOST_CLIENT_INFO_DATA, uDetail) + \
		sizeof(((RGX_HWPERF_HOST_CLIENT_INFO_DETAIL*)0)->sProcName.ui32Count)))

static void _HWPerfHostOnConnectCB(void *pvArg)
{
	PVRSRV_RGXDEV_INFO* psDevice;
	PVRSRV_ERROR eError;

	RGXSRV_HWPERF_CLK_SYNC(pvArg);

	psDevice = (PVRSRV_RGXDEV_INFO*) pvArg;

	/* Handle the case where the RGX_HWPERF_HOST_INFO bit is set in the event filter
	 * before the host stream is opened for reading by a HWPerf client.
	 * Which can result in the host periodic thread sleeping for a long duration as TLStreamIsOpenForReading may return false. */
	if (psDevice->ui32HWPerfHostFilter & RGX_HWPERF_EVENT_MASK_VALUE(RGX_HWPERF_HOST_INFO))
	{
		eError = PVRSRVCreateHWPerfHostThread(PVRSRV_APPHINT_HWPERFHOSTTHREADTIMEOUTINMS);
		PVR_LOG_IF_ERROR(eError, "PVRSRVCreateHWPerfHostThread");
	}

	if (RGXHWPerfHostIsEventEnabled(psDevice, RGX_HWPERF_HOST_CLIENT_INFO))
	{
		// GCC throws -Werror=frame-larger-than error if the frame size is > 1024 bytes,
		// so use a heap allocation - is there an alternate solution?
		IMG_BYTE *pbPktPayload = (IMG_BYTE*)OSAllocMem(RGX_HWPERF_MAX_PAYLOAD_SIZE);

		if (pbPktPayload)
		{
			RGX_HWPERF_HOST_CLIENT_INFO_DATA *psHostClientInfo;
			RGX_HWPERF_HOST_CLIENT_PROC_NAME *psProcName;
			IMG_UINT32 ui32TotalPayloadSize, ui32NameLen, ui32ProcNamePktSize;
			DLLIST_NODE *pNode, *pNext;

			psHostClientInfo = IMG_OFFSET_ADDR(pbPktPayload,0);
			psHostClientInfo->eType = RGX_HWPERF_HOST_CLIENT_INFO_TYPE_PROCESS_NAME;
			psHostClientInfo->uDetail.sProcName.ui32Count = 0U;
			psProcName = psHostClientInfo->uDetail.sProcName.asProcNames;
			ui32TotalPayloadSize = RGX_HWPERF_HOST_CLIENT_INFO_PROC_NAME_BASE_SIZE;

			OSLockAcquire(psDevice->psDeviceNode->hConnectionsLock);

			// Announce current client connections to the reader
			dllist_foreach_node(&psDevice->psDeviceNode->sConnections, pNode, pNext)
			{
				CONNECTION_DATA *psData = IMG_CONTAINER_OF(pNode, CONNECTION_DATA, sConnectionListNode);

				ui32NameLen = OSStringLength(psData->pszProcName) + 1U;
				ui32ProcNamePktSize = RGX_HWPERF_HOST_CLIENT_PROC_NAME_SIZE(ui32NameLen);

				// Unlikely case where we have too much data to fit into a single hwperf packet
				if (ui32ProcNamePktSize + ui32TotalPayloadSize > RGX_HWPERF_MAX_PAYLOAD_SIZE)
				{
					RGXHWPerfHostPostRaw(psDevice, RGX_HWPERF_HOST_CLIENT_INFO, pbPktPayload, ui32TotalPayloadSize);

					psHostClientInfo->uDetail.sProcName.ui32Count = 0U;
					psProcName = psHostClientInfo->uDetail.sProcName.asProcNames;
					ui32TotalPayloadSize = RGX_HWPERF_HOST_CLIENT_INFO_PROC_NAME_BASE_SIZE;
				}

				// Setup packet data
				psHostClientInfo->uDetail.sProcName.ui32Count++;
				psProcName->uiClientPID = psData->pid;
				psProcName->ui32Length = ui32NameLen;
				(void)OSStringLCopy(psProcName->acName, psData->pszProcName, ui32NameLen);

				psProcName = (RGX_HWPERF_HOST_CLIENT_PROC_NAME*)IMG_OFFSET_ADDR(psProcName, ui32ProcNamePktSize);
				ui32TotalPayloadSize += ui32ProcNamePktSize;
			}

			OSLockRelease(psDevice->psDeviceNode->hConnectionsLock);
			RGXHWPerfHostPostRaw(psDevice, RGX_HWPERF_HOST_CLIENT_INFO, pbPktPayload, ui32TotalPayloadSize);
			OSFreeMem(pbPktPayload);
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: OUT OF MEMORY. Could not allocate memory for RGX_HWPERF_HOST_CLIENT_INFO_DATA packet.", __func__));
		}
	}
}

/* Avoiding a holder struct using fields below, as a struct gets along padding,
 * packing, and other compiler dependencies, and we want a continuous stream of
 * bytes for (header+data) for use in TLStreamWrite. See
 * _HWPerfHostDeferredEventsEmitter().
 *
 * A deferred (UFO) packet is represented in memory as:
 *     - IMG_BOOL                 --> Indicates whether a packet write is
 *                                    "complete" by atomic context or not.
 *     - RGX_HWPERF_V2_PACKET_HDR --.
 *                                  |--> Fed together to TLStreamWrite for
 *                                  |    deferred packet to be written to
 *                                  |    HWPerfHost buffer
 *     - RGX_HWPERF_HOST_UFO_DATA---`
 *
 * PS: Currently only UFO events are supported in deferred list */
#define HWPERF_HOST_DEFERRED_UFO_PACKET_SIZE (sizeof(IMG_BOOL) +\
		sizeof(RGX_HWPERF_V2_PACKET_HDR) +\
		sizeof(RGX_HWPERF_HOST_UFO_DATA))

static void RGX_MISRHandler_HWPerfPostDeferredHostEvents(void *pvData);
static void _HWPerfHostDeferredEventsEmitter(PVRSRV_RGXDEV_INFO *psRgxDevInfo,
                                             IMG_UINT32 ui32MaxOrdinal);

/*************************************************************************/ /*!
@Function       RGXHWPerfHostInitOnDemandResources

@Description    This function allocates the HWPerfHost buffer if HWPerf is
                enabled at driver load time. Otherwise, these buffers are
                allocated on-demand as and when required.

@Return         PVRSRV_ERROR
 */ /**************************************************************************/
PVRSRV_ERROR RGXHWPerfHostInitOnDemandResources(PVRSRV_RGXDEV_INFO *psRgxDevInfo)
{
	PVRSRV_ERROR eError;
	/* 5 makes space up to "hwperf_host_9999" streams */
	IMG_CHAR pszHWPerfHostStreamName[sizeof(PVRSRV_TL_HWPERF_HOST_SERVER_STREAM) + 5];

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_IMPLEMENTED);

	if (psRgxDevInfo->hHWPerfHostStream != NULL)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "HWPerf host stream already initialised"));
		return PVRSRV_OK;
	}

	/* form the HWPerf host stream name, corresponding to this DevNode; which can make sense in the UM */
	if (OSSNPrintf(pszHWPerfHostStreamName, sizeof(pszHWPerfHostStreamName), "%s%d",
	               PVRSRV_TL_HWPERF_HOST_SERVER_STREAM,
	               psRgxDevInfo->psDeviceNode->sDevId.i32OsDeviceID) < 0)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: Failed to form HWPerf host stream name for device %d",
				__func__,
				psRgxDevInfo->psDeviceNode->sDevId.i32OsDeviceID));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = TLStreamCreate(&psRgxDevInfo->hHWPerfHostStream,
	                        pszHWPerfHostStreamName, psRgxDevInfo->ui32HWPerfHostBufSize,
	                        TL_OPMODE_DROP_NEWER,
	                        _HWPerfHostOnConnectCB, psRgxDevInfo,
	                        NULL, NULL);
	PVR_LOG_RETURN_IF_ERROR(eError, "TLStreamCreate");

	eError = TLStreamSetNotifStream(psRgxDevInfo->hHWPerfHostStream,
	                                PVRSRVGetPVRSRVData()->hTLCtrlStream);
	/* we can still discover host stream so leave it as is and just log error */
	PVR_LOG_IF_ERROR(eError, "TLStreamSetNotifStream");

	/* send the event here because host stream is implicitly opened for write
	 * in TLStreamCreate and TLStreamOpen is never called (so the event is
	 * never emitted) */
	eError = TLStreamMarkStreamOpen(psRgxDevInfo->hHWPerfHostStream);
	PVR_LOG_IF_ERROR(eError, "TLStreamMarkStreamOpen");

	/* HWPerfHost deferred events specific initialization */
	eError = OSInstallMISR(&psRgxDevInfo->pvHostHWPerfMISR,
	                       RGX_MISRHandler_HWPerfPostDeferredHostEvents,
	                       psRgxDevInfo,
	                       "RGX_HWPerfDeferredEventPoster");
	PVR_LOG_GOTO_IF_ERROR(eError, "OSInstallMISR", err_install_misr);

	eError = OSSpinLockCreate(&psRgxDevInfo->hHWPerfHostSpinLock);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSSpinLockCreate", err_spinlock_create);

	psRgxDevInfo->pui8DeferredEvents = OSAllocMem(HWPERF_HOST_MAX_DEFERRED_PACKETS
	                                              * HWPERF_HOST_DEFERRED_UFO_PACKET_SIZE);
	if (NULL == psRgxDevInfo->pui8DeferredEvents)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: OUT OF MEMORY. Could not allocate memory for "
				"HWPerfHost deferred events array", __func__));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto err_alloc_deferred_events;
	}
	psRgxDevInfo->ui16DEReadIdx = 0;
	psRgxDevInfo->ui16DEWriteIdx = 0;
#if defined(PVRSRV_HWPERF_HOST_DEBUG_DEFERRED_EVENTS)
	psRgxDevInfo->ui32DEHighWatermark = 0;
	psRgxDevInfo->ui32WaitForRightOrdPktHighWatermark = 0;
	psRgxDevInfo->ui32WaitForAtomicCtxPktHighWatermark = 0;
#endif

	PVR_DPF((DBGPRIV_MESSAGE, "HWPerf Host buffer size is %uKB",
			psRgxDevInfo->ui32HWPerfHostBufSize));

	return PVRSRV_OK;

err_alloc_deferred_events:
	OSSpinLockDestroy(psRgxDevInfo->hHWPerfHostSpinLock);
	psRgxDevInfo->hHWPerfHostSpinLock = NULL;

err_spinlock_create:
	(void) OSUninstallMISR(psRgxDevInfo->pvHostHWPerfMISR);
	psRgxDevInfo->pvHostHWPerfMISR = NULL;

err_install_misr:
	TLStreamMarkStreamClose(psRgxDevInfo->hHWPerfHostStream);
	TLStreamClose(psRgxDevInfo->hHWPerfHostStream);
	psRgxDevInfo->hHWPerfHostStream = NULL;

	return eError;
}

void RGXHWPerfHostDeInit(PVRSRV_RGXDEV_INFO *psRgxDevInfo)
{
	PVRSRV_VZ_RETN_IF_MODE(GUEST);

	PVR_ASSERT (psRgxDevInfo);

	if (psRgxDevInfo->pui8DeferredEvents)
	{
		OSFreeMem(psRgxDevInfo->pui8DeferredEvents);
		psRgxDevInfo->pui8DeferredEvents = NULL;
	}

	if (psRgxDevInfo->hHWPerfHostSpinLock)
	{
		OSSpinLockDestroy(psRgxDevInfo->hHWPerfHostSpinLock);
		psRgxDevInfo->hHWPerfHostSpinLock = NULL;
	}

	if (psRgxDevInfo->pvHostHWPerfMISR)
	{
		(void) OSUninstallMISR(psRgxDevInfo->pvHostHWPerfMISR);
		psRgxDevInfo->pvHostHWPerfMISR = NULL;
	}

	if (psRgxDevInfo->hHWPerfHostStream)
	{
		/* send the event here because host stream is implicitly opened for
		 * write in TLStreamCreate and TLStreamClose is never called (so the
		 * event is never emitted) */
		TLStreamMarkStreamClose(psRgxDevInfo->hHWPerfHostStream);
		TLStreamClose(psRgxDevInfo->hHWPerfHostStream);
		psRgxDevInfo->hHWPerfHostStream = NULL;
	}

	if (psRgxDevInfo->hLockHWPerfHostStream)
	{
		OSLockDestroy(psRgxDevInfo->hLockHWPerfHostStream);
		psRgxDevInfo->hLockHWPerfHostStream = NULL;
	}
}

inline void RGXHWPerfHostSetEventFilter(PVRSRV_RGXDEV_INFO *psRgxDevInfo, IMG_UINT32 ui32Filter)
{
	PVRSRV_VZ_RETN_IF_MODE(GUEST);
	psRgxDevInfo->ui32HWPerfHostFilter = ui32Filter;
}

inline IMG_BOOL RGXHWPerfHostIsEventEnabled(PVRSRV_RGXDEV_INFO *psRgxDevInfo, RGX_HWPERF_HOST_EVENT_TYPE eEvent)
{
	PVR_ASSERT(psRgxDevInfo);
	return (psRgxDevInfo->ui32HWPerfHostFilter & RGX_HWPERF_EVENT_MASK_VALUE(eEvent)) ? IMG_TRUE : IMG_FALSE;
}

#define MAX_RETRY_COUNT 80
static inline void _PostFunctionPrologue(PVRSRV_RGXDEV_INFO *psRgxDevInfo,
                                         IMG_UINT32 ui32CurrentOrdinal)
{
	IMG_UINT32 ui32Retry = MAX_RETRY_COUNT;

	PVR_ASSERT(psRgxDevInfo->hLockHWPerfHostStream != NULL);
	PVR_ASSERT(psRgxDevInfo->hHWPerfHostStream != NULL);

	OSLockAcquire(psRgxDevInfo->hLockHWPerfHostStream);

	/* First, flush pending events (if any) */
	_HWPerfHostDeferredEventsEmitter(psRgxDevInfo, ui32CurrentOrdinal);

	while ((ui32CurrentOrdinal != psRgxDevInfo->ui32HWPerfHostLastOrdinal + 1)
		   && (--ui32Retry != 0))
	{
		/* Release lock and give a chance to a waiting context to emit the
		 * expected packet */
		OSLockRelease (psRgxDevInfo->hLockHWPerfHostStream);
		OSSleepms(100);
		OSLockAcquire(psRgxDevInfo->hLockHWPerfHostStream);
	}

#if defined(PVRSRV_HWPERF_HOST_DEBUG_DEFERRED_EVENTS)
	if ((ui32Retry == 0) && !(psRgxDevInfo->bWarnedPktOrdinalBroke))
	{
		PVR_DPF((PVR_DBG_WARNING,
				 "%s: Will warn only once! Potential packet(s) lost after ordinal"
				 " %u (Current ordinal = %u)",
				 __func__,
				 psRgxDevInfo->ui32HWPerfHostLastOrdinal, ui32CurrentOrdinal));
		psRgxDevInfo->bWarnedPktOrdinalBroke = IMG_TRUE;
	}

	if (psRgxDevInfo->ui32WaitForRightOrdPktHighWatermark < (MAX_RETRY_COUNT - ui32Retry))
	{
		psRgxDevInfo->ui32WaitForRightOrdPktHighWatermark = MAX_RETRY_COUNT - ui32Retry;
	}
#endif
}

static inline void _PostFunctionEpilogue(PVRSRV_RGXDEV_INFO *psRgxDevInfo,
                                         IMG_UINT32 ui32CurrentOrdinal)
{
	/* update last ordinal emitted */
	psRgxDevInfo->ui32HWPerfHostLastOrdinal = ui32CurrentOrdinal;

	PVR_ASSERT(OSLockIsLocked(psRgxDevInfo->hLockHWPerfHostStream));
	OSLockRelease(psRgxDevInfo->hLockHWPerfHostStream);
}

static inline IMG_UINT8 *_ReserveHWPerfStream(PVRSRV_RGXDEV_INFO *psRgxDevInfo, IMG_UINT32 ui32Size)
{
	IMG_UINT8 *pui8Dest;

	PVRSRV_ERROR eError = TLStreamReserve(psRgxDevInfo->hHWPerfHostStream,
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

static inline void _CommitHWPerfStream(PVRSRV_RGXDEV_INFO *psRgxDevInfo, IMG_UINT32 ui32Size)
{
	PVRSRV_ERROR eError = TLStreamCommit(psRgxDevInfo->hHWPerfHostStream,
	                                     ui32Size);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: Could not commit data to %s"
				" (%d)", __func__, PVRSRV_TL_HWPERF_HOST_SERVER_STREAM, eError));
	}
}

/* Returns IMG_TRUE if packet write passes, IMG_FALSE otherwise */
static inline IMG_BOOL _WriteHWPerfStream(PVRSRV_RGXDEV_INFO *psRgxDevInfo,
                                          RGX_HWPERF_V2_PACKET_HDR *psHeader)
{
	PVRSRV_ERROR eError = TLStreamWrite(psRgxDevInfo->hHWPerfHostStream,
	                                    IMG_OFFSET_ADDR(psHeader, 0), psHeader->ui32Size);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: Could not write packet in %s buffer"
				" (%d). Dropping packet.",
				__func__, PVRSRV_TL_HWPERF_HOST_SERVER_STREAM, eError));
	}

	/* Regardless of whether write passed/failed, we consider it "written" */
	psRgxDevInfo->ui32HWPerfHostLastOrdinal = psHeader->ui32Ordinal;

	return (eError == PVRSRV_OK);
}

/* Helper macros for deferred events operations */
#define GET_DE_NEXT_IDX(_curridx) ((_curridx + 1) % HWPERF_HOST_MAX_DEFERRED_PACKETS)
#define GET_DE_EVENT_BASE(_idx)   (IMG_OFFSET_ADDR(psRgxDevInfo->pui8DeferredEvents, \
		(_idx) * HWPERF_HOST_DEFERRED_UFO_PACKET_SIZE))

#define GET_DE_EVENT_WRITE_STATUS(_base) ((IMG_BOOL*)((void *)(_base)))
#define GET_DE_EVENT_DATA(_base)         (IMG_OFFSET_ADDR((_base), sizeof(IMG_BOOL)))

/* Emits HWPerfHost event packets present in the deferred list stopping when one
 * of the following cases is hit:
 * case 1: Packet ordering breaks i.e. a packet found doesn't meet ordering
 *         criteria (ordinal == last_ordinal + 1)
 *
 * case 2: A packet with ordinal > ui32MaxOrdinal is found
 *
 * case 3: Deferred list's (read == write) i.e. no more deferred packets.
 *
 * NOTE: Caller must possess the hLockHWPerfHostStream lock before calling
 *       this function.*/
static void _HWPerfHostDeferredEventsEmitter(PVRSRV_RGXDEV_INFO *psRgxDevInfo,
                                             IMG_UINT32 ui32MaxOrdinal)
{
	RGX_HWPERF_V2_PACKET_HDR *psHeader;
	IMG_UINT32 ui32Retry;
	IMG_UINT8  *pui8DeferredEvent;
	IMG_BOOL   *pbPacketWritten;
	IMG_BOOL   bWritePassed;

	PVR_ASSERT(OSLockIsLocked(psRgxDevInfo->hLockHWPerfHostStream));

	while (psRgxDevInfo->ui16DEReadIdx != psRgxDevInfo->ui16DEWriteIdx)
	{
		pui8DeferredEvent = GET_DE_EVENT_BASE(psRgxDevInfo->ui16DEReadIdx);
		pbPacketWritten   = GET_DE_EVENT_WRITE_STATUS(pui8DeferredEvent);
		psHeader          = (RGX_HWPERF_V2_PACKET_HDR*) GET_DE_EVENT_DATA(pui8DeferredEvent);

		for (ui32Retry = MAX_RETRY_COUNT; !(*pbPacketWritten) && (ui32Retry != 0); ui32Retry--)
		{
			/* Packet not yet written, re-check after a while. Wait for a short period as
			 * atomic contexts are generally expected to finish fast */
			OSWaitus(10);
		}

#if defined(PVRSRV_HWPERF_HOST_DEBUG_DEFERRED_EVENTS)
		if ((ui32Retry == 0) && !(psRgxDevInfo->bWarnedAtomicCtxPktLost))
		{
			PVR_DPF((PVR_DBG_WARNING,
					 "%s: Will warn only once. Dropping a deferred packet as atomic context"
					 " took too long to write it",
					 __func__));
			psRgxDevInfo->bWarnedAtomicCtxPktLost = IMG_TRUE;
		}

		if (psRgxDevInfo->ui32WaitForAtomicCtxPktHighWatermark < (MAX_RETRY_COUNT - ui32Retry))
		{
			psRgxDevInfo->ui32WaitForAtomicCtxPktHighWatermark = MAX_RETRY_COUNT - ui32Retry;
		}
#endif

		if (*pbPacketWritten)
		{
			if ((psHeader->ui32Ordinal > ui32MaxOrdinal) ||
					(psHeader->ui32Ordinal != (psRgxDevInfo->ui32HWPerfHostLastOrdinal + 1)))
			{
				/* Leave remaining events to be emitted by next call to this function */
				break;
			}
			bWritePassed = _WriteHWPerfStream(psRgxDevInfo, psHeader);
		}
		else
		{
			PVR_DPF((PVR_DBG_MESSAGE, "%s: Atomic context packet lost!", __func__));
			bWritePassed = IMG_FALSE;
		}

		/* Move on to next packet */
		psRgxDevInfo->ui16DEReadIdx = GET_DE_NEXT_IDX(psRgxDevInfo->ui16DEReadIdx);

		if (!bWritePassed // if write failed
				&& ui32MaxOrdinal == IMG_UINT32_MAX // and we are from MISR
				&& psRgxDevInfo->ui16DEReadIdx != psRgxDevInfo->ui16DEWriteIdx) // and there are more events
		{
			/* Stop emitting here and re-schedule MISR */
			OSScheduleMISR(psRgxDevInfo->pvHostHWPerfMISR);
			break;
		}
	}
}

static void RGX_MISRHandler_HWPerfPostDeferredHostEvents(void *pvData)
{
	PVRSRV_RGXDEV_INFO *psRgxDevInfo = pvData;

	OSLockAcquire(psRgxDevInfo->hLockHWPerfHostStream);

	/* Since we're called from MISR, there is no upper cap of ordinal to be emitted.
	 * Send IMG_UINT32_MAX to signify all possible packets. */
	_HWPerfHostDeferredEventsEmitter(psRgxDevInfo, IMG_UINT32_MAX);

	OSLockRelease(psRgxDevInfo->hLockHWPerfHostStream);
}

#if defined(PVRSRV_HWPERF_HOST_DEBUG_DEFERRED_EVENTS)
static inline void _UpdateDEBufferHighWatermark(PVRSRV_RGXDEV_INFO *psRgxDevInfo)
{
	IMG_UINT32 ui32DEWatermark;
	IMG_UINT16 ui16LRead = psRgxDevInfo->ui16DEReadIdx;
	IMG_UINT16 ui16LWrite = psRgxDevInfo->ui16DEWriteIdx;

	if (ui16LWrite >= ui16LRead)
	{
		ui32DEWatermark = ui16LWrite - ui16LRead;
	}
	else
	{
		ui32DEWatermark = (HWPERF_HOST_MAX_DEFERRED_PACKETS - ui16LRead) + (ui16LWrite);
	}

	if (ui32DEWatermark > psRgxDevInfo->ui32DEHighWatermark)
	{
		psRgxDevInfo->ui32DEHighWatermark = ui32DEWatermark;
	}
}
#endif

/* @Description Gets the data/members that concerns the accuracy of a packet in HWPerfHost
                buffer. Since the data returned by this function is required in both, an
                atomic as well as a process/sleepable context, it is protected under spinlock

   @Output      pui32Ordinal Pointer to ordinal number assigned to this packet
   @Output      pui64Timestamp Timestamp value for this packet
   @Output      ppui8Dest If the current context cannot sleep, pointer to a place in
                          deferred events buffer where the packet data should be written.
                          Don't care, otherwise.
 */
static void _GetHWPerfHostPacketSpecifics(PVRSRV_RGXDEV_INFO *psRgxDevInfo,
                                          IMG_UINT32 *pui32Ordinal,
                                          IMG_UINT64 *pui64Timestamp,
                                          IMG_UINT8 **ppui8Dest,
                                          IMG_BOOL    bSleepAllowed)
{
	OS_SPINLOCK_FLAGS uiFlags;

	/* Spin lock is required to avoid getting scheduled out by a higher priority
	 * context while we're getting header specific details and packet place in
	 * HWPerf buffer (when in atomic context) for ourselves */
	OSSpinLockAcquire(psRgxDevInfo->hHWPerfHostSpinLock, uiFlags);

	*pui32Ordinal = psRgxDevInfo->ui32HWPerfHostNextOrdinal++;
	*pui64Timestamp = RGXTimeCorrGetClockus64(psRgxDevInfo->psDeviceNode);

	if (!bSleepAllowed)
	{
		/* We're in an atomic context. So return the next position available in
		 * deferred events buffer */
		IMG_UINT16 ui16NewWriteIdx;
		IMG_BOOL *pbPacketWritten;

		PVR_ASSERT(ppui8Dest != NULL);

		ui16NewWriteIdx = GET_DE_NEXT_IDX(psRgxDevInfo->ui16DEWriteIdx);
		if (ui16NewWriteIdx == psRgxDevInfo->ui16DEReadIdx)
		{
			/* This shouldn't happen. HWPERF_HOST_MAX_DEFERRED_PACKETS should be
			 * big enough to avoid any such scenario */
#if defined(PVRSRV_HWPERF_HOST_DEBUG_DEFERRED_EVENTS)
			/* PVR_LOG/printk isn't recommended in atomic context. Perhaps we'll do
			 * this debug output here when trace_printk support is added to DDK */
//			PVR_LOG(("%s: No more space in deferred events buffer (%u/%u) W=%u,R=%u",
//                     __func__, psRgxDevInfo->ui32DEHighWatermark,
//					 HWPERF_HOST_MAX_DEFERRED_PACKETS, psRgxDevInfo->ui16DEWriteIdx,
//					 psRgxDevInfo->ui16DEReadIdx));
#endif
			*ppui8Dest = NULL;
		}
		else
		{
			/* Return the position where deferred event would be written */
			*ppui8Dest = GET_DE_EVENT_BASE(psRgxDevInfo->ui16DEWriteIdx);

			/* Make sure packet write "state" is "write-pending" _before_ moving write
			 * pointer forward */
			pbPacketWritten = GET_DE_EVENT_WRITE_STATUS(*ppui8Dest);
			*pbPacketWritten = IMG_FALSE;

			psRgxDevInfo->ui16DEWriteIdx = ui16NewWriteIdx;

#if defined(PVRSRV_HWPERF_HOST_DEBUG_DEFERRED_EVENTS)
			_UpdateDEBufferHighWatermark(psRgxDevInfo);
#endif
		}
	}

	OSSpinLockRelease(psRgxDevInfo->hHWPerfHostSpinLock, uiFlags);
}

static inline void _SetupHostPacketHeader(PVRSRV_RGXDEV_INFO *psRgxDevInfo,
                                          IMG_UINT8 *pui8Dest,
                                          RGX_HWPERF_HOST_EVENT_TYPE eEvType,
                                          IMG_UINT32 ui32Size,
                                          IMG_UINT32 ui32Ordinal,
                                          IMG_UINT64 ui64Timestamp)
{
	RGX_HWPERF_V2_PACKET_HDR *psHeader = (RGX_HWPERF_V2_PACKET_HDR *) ((void *)pui8Dest);

	PVR_ASSERT(ui32Size<=RGX_HWPERF_MAX_PACKET_SIZE);

	psHeader->ui32Ordinal = ui32Ordinal;
	psHeader->ui64Timestamp = ui64Timestamp;
	psHeader->ui32Sig = HWPERF_PACKET_V2B_SIG;
	psHeader->eTypeId = RGX_HWPERF_MAKE_TYPEID(RGX_HWPERF_STREAM_ID1_HOST,
	                                           eEvType, 0, 0, 0);
	psHeader->ui32Size = ui32Size;
}

static inline void _SetupHostEnqPacketData(IMG_UINT8 *pui8Dest,
                                           RGX_HWPERF_KICK_TYPE eEnqType,
                                           IMG_UINT32 ui32Pid,
                                           IMG_UINT32 ui32FWDMContext,
                                           IMG_UINT32 ui32ExtJobRef,
                                           IMG_UINT32 ui32IntJobRef,
                                           PVRSRV_FENCE hCheckFence,
                                           PVRSRV_FENCE hUpdateFence,
                                           PVRSRV_TIMELINE hUpdateTimeline,
                                           IMG_UINT64 ui64CheckFenceUID,
                                           IMG_UINT64 ui64UpdateFenceUID,
                                           IMG_UINT64 ui64DeadlineInus,
                                           IMG_UINT32 ui32CycleEstimate)
{
	RGX_HWPERF_HOST_ENQ_DATA *psData = (RGX_HWPERF_HOST_ENQ_DATA *)
					IMG_OFFSET_ADDR(pui8Dest, sizeof(RGX_HWPERF_V2_PACKET_HDR));
	psData->ui32EnqType = eEnqType;
	psData->ui32PID = ui32Pid;
	psData->ui32ExtJobRef = ui32ExtJobRef;
	psData->ui32IntJobRef = ui32IntJobRef;
	psData->ui32DMContext = ui32FWDMContext;
	psData->hCheckFence = hCheckFence;
	psData->hUpdateFence = hUpdateFence;
	psData->hUpdateTimeline = hUpdateTimeline;
	psData->ui64CheckFence_UID = ui64CheckFenceUID;
	psData->ui64UpdateFence_UID = ui64UpdateFenceUID;
	psData->ui64DeadlineInus = ui64DeadlineInus;
	psData->ui32CycleEstimate = ui32CycleEstimate;
}

void RGXHWPerfHostPostRaw(PVRSRV_RGXDEV_INFO *psRgxDevInfo,
						  RGX_HWPERF_HOST_EVENT_TYPE eEvType,
						  IMG_BYTE *pbPayload,
						  IMG_UINT32 ui32PayloadSize)
{
	IMG_UINT8 *pui8Dest;
	IMG_UINT32 ui32PktSize;
	IMG_UINT32 ui32Ordinal;
	IMG_UINT64 ui64Timestamp;

	PVR_ASSERT(ui32PayloadSize <= RGX_HWPERF_MAX_PAYLOAD_SIZE);

	_GetHWPerfHostPacketSpecifics(psRgxDevInfo, &ui32Ordinal, &ui64Timestamp, NULL, IMG_TRUE);
	_PostFunctionPrologue(psRgxDevInfo, ui32Ordinal);

	ui32PktSize = RGX_HWPERF_MAKE_SIZE_VARIABLE(ui32PayloadSize);
	pui8Dest = _ReserveHWPerfStream(psRgxDevInfo, ui32PktSize);

	if (pui8Dest == NULL)
	{
		goto cleanup;
	}

	_SetupHostPacketHeader(psRgxDevInfo, pui8Dest, eEvType, ui32PktSize, ui32Ordinal, ui64Timestamp);
	OSDeviceMemCopy((IMG_UINT8*)IMG_OFFSET_ADDR(pui8Dest, sizeof(RGX_HWPERF_V2_PACKET_HDR)), pbPayload, ui32PayloadSize);
	_CommitHWPerfStream(psRgxDevInfo, ui32PktSize);

cleanup:
	_PostFunctionEpilogue(psRgxDevInfo, ui32Ordinal);
}

void RGXHWPerfHostPostEnqEvent(PVRSRV_RGXDEV_INFO *psRgxDevInfo,
                               RGX_HWPERF_KICK_TYPE eEnqType,
                               IMG_UINT32 ui32Pid,
                               IMG_UINT32 ui32FWDMContext,
                               IMG_UINT32 ui32ExtJobRef,
                               IMG_UINT32 ui32IntJobRef,
                               PVRSRV_FENCE hCheckFence,
                               PVRSRV_FENCE hUpdateFence,
                               PVRSRV_TIMELINE hUpdateTimeline,
                               IMG_UINT64 ui64CheckFenceUID,
                               IMG_UINT64 ui64UpdateFenceUID,
                               IMG_UINT64 ui64DeadlineInus,
                               IMG_UINT32 ui32CycleEstimate )
{
	IMG_UINT8 *pui8Dest;
	IMG_UINT32 ui32Size = RGX_HWPERF_MAKE_SIZE_FIXED(RGX_HWPERF_HOST_ENQ_DATA);
	IMG_UINT32 ui32Ordinal;
	IMG_UINT64 ui64Timestamp;

	_GetHWPerfHostPacketSpecifics(psRgxDevInfo, &ui32Ordinal, &ui64Timestamp,
	                              NULL, IMG_TRUE);

	_PostFunctionPrologue(psRgxDevInfo, ui32Ordinal);

	if ((pui8Dest = _ReserveHWPerfStream(psRgxDevInfo, ui32Size)) == NULL)
	{
		goto cleanup;
	}

	_SetupHostPacketHeader(psRgxDevInfo, pui8Dest, RGX_HWPERF_HOST_ENQ, ui32Size,
	                       ui32Ordinal, ui64Timestamp);
	_SetupHostEnqPacketData(pui8Dest,
	                        eEnqType,
	                        ui32Pid,
	                        ui32FWDMContext,
	                        ui32ExtJobRef,
	                        ui32IntJobRef,
	                        hCheckFence,
	                        hUpdateFence,
	                        hUpdateTimeline,
	                        ui64CheckFenceUID,
	                        ui64UpdateFenceUID,
	                        ui64DeadlineInus,
	                        ui32CycleEstimate);

	_CommitHWPerfStream(psRgxDevInfo, ui32Size);

cleanup:
	_PostFunctionEpilogue(psRgxDevInfo, ui32Ordinal);
}

static inline IMG_UINT32 _CalculateHostUfoPacketSize(RGX_HWPERF_UFO_EV eUfoType)
{
	IMG_UINT32 ui32Size =
			(IMG_UINT32) offsetof(RGX_HWPERF_HOST_UFO_DATA, aui32StreamData);
	RGX_HWPERF_UFO_DATA_ELEMENT *puData;

	switch (eUfoType)
	{
		case RGX_HWPERF_UFO_EV_CHECK_SUCCESS:
		case RGX_HWPERF_UFO_EV_PRCHECK_SUCCESS:
			ui32Size += sizeof(puData->sCheckSuccess);
			break;
		case RGX_HWPERF_UFO_EV_CHECK_FAIL:
		case RGX_HWPERF_UFO_EV_PRCHECK_FAIL:
			ui32Size += sizeof(puData->sCheckFail);
			break;
		case RGX_HWPERF_UFO_EV_UPDATE:
			ui32Size += sizeof(puData->sUpdate);
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
                                           RGX_HWPERF_UFO_DATA_ELEMENT *psUFOData)
{
	RGX_HWPERF_HOST_UFO_DATA *psData = (RGX_HWPERF_HOST_UFO_DATA *)
					IMG_OFFSET_ADDR(pui8Dest, sizeof(RGX_HWPERF_V2_PACKET_HDR));
	RGX_HWPERF_UFO_DATA_ELEMENT *puData = (RGX_HWPERF_UFO_DATA_ELEMENT *)
					 psData->aui32StreamData;

	psData->eEvType = eUfoType;
	/* HWPerfHost always emits 1 UFO at a time, since each UFO has 1-to-1 mapping
	 * with an underlying DevNode, and each DevNode has a dedicated HWPerf buffer */
	psData->ui32StreamInfo = RGX_HWPERF_MAKE_UFOPKTINFO(1,
	                                                    offsetof(RGX_HWPERF_HOST_UFO_DATA, aui32StreamData));

	switch (eUfoType)
	{
		case RGX_HWPERF_UFO_EV_CHECK_SUCCESS:
		case RGX_HWPERF_UFO_EV_PRCHECK_SUCCESS:
			puData->sCheckSuccess.ui32FWAddr =
					psUFOData->sCheckSuccess.ui32FWAddr;
			puData->sCheckSuccess.ui32Value =
					psUFOData->sCheckSuccess.ui32Value;
			break;
		case RGX_HWPERF_UFO_EV_CHECK_FAIL:
		case RGX_HWPERF_UFO_EV_PRCHECK_FAIL:
			puData->sCheckFail.ui32FWAddr =
					psUFOData->sCheckFail.ui32FWAddr;
			puData->sCheckFail.ui32Value =
					psUFOData->sCheckFail.ui32Value;
			puData->sCheckFail.ui32Required =
					psUFOData->sCheckFail.ui32Required;
			break;
		case RGX_HWPERF_UFO_EV_UPDATE:
			puData->sUpdate.ui32FWAddr =
					psUFOData->sUpdate.ui32FWAddr;
			puData->sUpdate.ui32OldValue =
					psUFOData->sUpdate.ui32OldValue;
			puData->sUpdate.ui32NewValue =
					psUFOData->sUpdate.ui32NewValue;
			break;
		default:
			// unknown type - this should never happen
			PVR_DPF((PVR_DBG_ERROR, "RGXHWPerfHostPostUfoEvent: Invalid UFO"
					" event type"));
			PVR_ASSERT(IMG_FALSE);
			break;
	}
}

void RGXHWPerfHostPostUfoEvent(PVRSRV_RGXDEV_INFO *psRgxDevInfo,
                               RGX_HWPERF_UFO_EV eUfoType,
                               RGX_HWPERF_UFO_DATA_ELEMENT *psUFOData,
                               const IMG_BOOL bSleepAllowed)
{
	IMG_UINT8 *pui8Dest;
	IMG_UINT32 ui32Size = _CalculateHostUfoPacketSize(eUfoType);
	IMG_UINT32 ui32Ordinal;
	IMG_UINT64 ui64Timestamp;
	IMG_BOOL   *pbPacketWritten = NULL;

	_GetHWPerfHostPacketSpecifics(psRgxDevInfo, &ui32Ordinal, &ui64Timestamp,
	                              &pui8Dest, bSleepAllowed);

	if (bSleepAllowed)
	{
		_PostFunctionPrologue(psRgxDevInfo, ui32Ordinal);

		if ((pui8Dest = _ReserveHWPerfStream(psRgxDevInfo, ui32Size)) == NULL)
		{
			goto cleanup;
		}
	}
	else
	{
		if (pui8Dest == NULL)
		{
			// Give-up if we couldn't get a place in deferred events buffer
			goto cleanup;
		}
		pbPacketWritten = GET_DE_EVENT_WRITE_STATUS(pui8Dest);
		pui8Dest = GET_DE_EVENT_DATA(pui8Dest);
	}

	_SetupHostPacketHeader(psRgxDevInfo, pui8Dest, RGX_HWPERF_HOST_UFO, ui32Size,
	                       ui32Ordinal, ui64Timestamp);
	_SetupHostUfoPacketData(pui8Dest, eUfoType, psUFOData);

	if (bSleepAllowed)
	{
		_CommitHWPerfStream(psRgxDevInfo, ui32Size);
	}
	else
	{
		*pbPacketWritten = IMG_TRUE;
		OSScheduleMISR(psRgxDevInfo->pvHostHWPerfMISR);
	}

cleanup:
	if (bSleepAllowed)
	{
		_PostFunctionEpilogue(psRgxDevInfo, ui32Ordinal);
	}
}

#define UNKNOWN_SYNC_NAME "UnknownSync"

static_assert(PVRSRV_SYNC_NAME_LENGTH==PVRSRV_SYNC_NAME_LENGTH, "Sync class name max does not match Fence Sync name max");

static inline IMG_UINT32 _FixNameAndCalculateHostAllocPacketSize(
		RGX_HWPERF_HOST_RESOURCE_TYPE eAllocType,
		const IMG_CHAR **ppsName,
		IMG_UINT32 *ui32NameSize)
{
	RGX_HWPERF_HOST_ALLOC_DATA *psData;
	IMG_UINT32 ui32Size = offsetof(RGX_HWPERF_HOST_ALLOC_DATA, uAllocDetail);

	if (*ppsName != NULL && *ui32NameSize > 0)
	{
		/* if string longer than maximum cut it (leave space for '\0') */
		if (*ui32NameSize >= PVRSRV_SYNC_NAME_LENGTH)
			*ui32NameSize = PVRSRV_SYNC_NAME_LENGTH;
	}
	else
	{
		PVR_DPF((PVR_DBG_WARNING, "RGXHWPerfHostPostAllocEvent: Invalid"
				" resource name given."));
		*ppsName = UNKNOWN_SYNC_NAME;
		*ui32NameSize = sizeof(UNKNOWN_SYNC_NAME);
	}

	switch (eAllocType)
	{
		case RGX_HWPERF_HOST_RESOURCE_TYPE_SYNC:
			ui32Size += sizeof(psData->uAllocDetail.sSyncAlloc) - PVRSRV_SYNC_NAME_LENGTH +
			*ui32NameSize;
			break;
		case RGX_HWPERF_HOST_RESOURCE_TYPE_FENCE_PVR:
			ui32Size += sizeof(psData->uAllocDetail.sFenceAlloc) - PVRSRV_SYNC_NAME_LENGTH +
			*ui32NameSize;
			break;
		case RGX_HWPERF_HOST_RESOURCE_TYPE_FENCE_SW:
			ui32Size += sizeof(psData->uAllocDetail.sSWFenceAlloc) - PVRSRV_SYNC_NAME_LENGTH +
			*ui32NameSize;
			break;
		case RGX_HWPERF_HOST_RESOURCE_TYPE_SYNC_CP:
			ui32Size += sizeof(psData->uAllocDetail.sSyncCheckPointAlloc) - PVRSRV_SYNC_NAME_LENGTH +
			*ui32NameSize;
			break;
		default:
			// unknown type - this should never happen
			PVR_DPF((PVR_DBG_ERROR,
					"RGXHWPerfHostPostAllocEvent: Invalid alloc event type"));
			PVR_ASSERT(IMG_FALSE);
			break;
	}

	return RGX_HWPERF_MAKE_SIZE_VARIABLE(ui32Size);
}

static inline void _SetupHostAllocPacketData(IMG_UINT8 *pui8Dest,
                                             RGX_HWPERF_HOST_RESOURCE_TYPE eAllocType,
                                             RGX_HWPERF_HOST_ALLOC_DETAIL *puAllocDetail,
                                             const IMG_CHAR *psName,
                                             IMG_UINT32 ui32NameSize)
{
	RGX_HWPERF_HOST_ALLOC_DATA *psData = (RGX_HWPERF_HOST_ALLOC_DATA *)
					IMG_OFFSET_ADDR(pui8Dest, sizeof(RGX_HWPERF_V2_PACKET_HDR));

	IMG_CHAR *acName = NULL;

	psData->ui32AllocType = eAllocType;

	switch (eAllocType)
	{
		case RGX_HWPERF_HOST_RESOURCE_TYPE_SYNC:
			psData->uAllocDetail.sSyncAlloc = puAllocDetail->sSyncAlloc;
			acName = psData->uAllocDetail.sSyncAlloc.acName;
			break;
		case RGX_HWPERF_HOST_RESOURCE_TYPE_FENCE_PVR:
			psData->uAllocDetail.sFenceAlloc = puAllocDetail->sFenceAlloc;
			acName = psData->uAllocDetail.sFenceAlloc.acName;
			break;
		case RGX_HWPERF_HOST_RESOURCE_TYPE_FENCE_SW:
			psData->uAllocDetail.sSWFenceAlloc = puAllocDetail->sSWFenceAlloc;
			acName = psData->uAllocDetail.sSWFenceAlloc.acName;
			break;
		case RGX_HWPERF_HOST_RESOURCE_TYPE_SYNC_CP:
			psData->uAllocDetail.sSyncCheckPointAlloc = puAllocDetail->sSyncCheckPointAlloc;
			acName = psData->uAllocDetail.sSyncCheckPointAlloc.acName;
			break;
		default:
			// unknown type - this should never happen
			PVR_DPF((PVR_DBG_ERROR,
					"RGXHWPerfHostPostAllocEvent: Invalid alloc event type"));
			PVR_ASSERT(IMG_FALSE);
	}


	if (acName != NULL)
	{
		if (ui32NameSize)
		{
			OSStringLCopy(acName, psName, ui32NameSize);
		}
		else
		{
			/* In case no name was given make sure we don't access random
			 * memory */
			acName[0] = '\0';
		}
	}
}

void RGXHWPerfHostPostAllocEvent(PVRSRV_RGXDEV_INFO* psRgxDevInfo,
                                 RGX_HWPERF_HOST_RESOURCE_TYPE eAllocType,
                                 const IMG_CHAR *psName,
                                 IMG_UINT32 ui32NameSize,
                                 RGX_HWPERF_HOST_ALLOC_DETAIL *puAllocDetail)
{
	IMG_UINT8 *pui8Dest;
	IMG_UINT64 ui64Timestamp;
	IMG_UINT32 ui32Ordinal;
	IMG_UINT32 ui32Size = _FixNameAndCalculateHostAllocPacketSize(eAllocType,
	                                                              &psName,
	                                                              &ui32NameSize);

	_GetHWPerfHostPacketSpecifics(psRgxDevInfo, &ui32Ordinal, &ui64Timestamp,
	                              NULL, IMG_TRUE);

	_PostFunctionPrologue(psRgxDevInfo, ui32Ordinal);

	if ((pui8Dest = _ReserveHWPerfStream(psRgxDevInfo, ui32Size)) == NULL)
	{
		goto cleanup;
	}

	_SetupHostPacketHeader(psRgxDevInfo, pui8Dest, RGX_HWPERF_HOST_ALLOC, ui32Size,
	                       ui32Ordinal, ui64Timestamp);

	_SetupHostAllocPacketData(pui8Dest,
	                          eAllocType,
	                          puAllocDetail,
	                          psName,
	                          ui32NameSize);

	_CommitHWPerfStream(psRgxDevInfo, ui32Size);

cleanup:
	_PostFunctionEpilogue(psRgxDevInfo, ui32Ordinal);
}

static inline void _SetupHostFreePacketData(IMG_UINT8 *pui8Dest,
                                            RGX_HWPERF_HOST_RESOURCE_TYPE eFreeType,
                                            IMG_UINT64 ui64UID,
                                            IMG_UINT32 ui32PID,
                                            IMG_UINT32 ui32FWAddr)
{
	RGX_HWPERF_HOST_FREE_DATA *psData = (RGX_HWPERF_HOST_FREE_DATA *)
					IMG_OFFSET_ADDR(pui8Dest, sizeof(RGX_HWPERF_V2_PACKET_HDR));

	psData->ui32FreeType = eFreeType;

	switch (eFreeType)
	{
		case RGX_HWPERF_HOST_RESOURCE_TYPE_SYNC:
			psData->uFreeDetail.sSyncFree.ui32FWAddr = ui32FWAddr;
			break;
		case RGX_HWPERF_HOST_RESOURCE_TYPE_FENCE_PVR:
			psData->uFreeDetail.sFenceDestroy.ui64Fence_UID = ui64UID;
			break;
		case RGX_HWPERF_HOST_RESOURCE_TYPE_SYNC_CP:
			psData->uFreeDetail.sSyncCheckPointFree.ui32CheckPt_FWAddr = ui32FWAddr;
			break;
		default:
			// unknown type - this should never happen
			PVR_DPF((PVR_DBG_ERROR,
					"RGXHWPerfHostPostFreeEvent: Invalid free event type"));
			PVR_ASSERT(IMG_FALSE);
	}
}

void RGXHWPerfHostPostFreeEvent(PVRSRV_RGXDEV_INFO *psRgxDevInfo,
                                RGX_HWPERF_HOST_RESOURCE_TYPE eFreeType,
                                IMG_UINT64 ui64UID,
                                IMG_UINT32 ui32PID,
                                IMG_UINT32 ui32FWAddr)
{
	IMG_UINT8 *pui8Dest;
	IMG_UINT32 ui32Size = RGX_HWPERF_MAKE_SIZE_FIXED(RGX_HWPERF_HOST_FREE_DATA);
	IMG_UINT32 ui32Ordinal;
	IMG_UINT64 ui64Timestamp;

	_GetHWPerfHostPacketSpecifics(psRgxDevInfo, &ui32Ordinal, &ui64Timestamp,
	                              NULL, IMG_TRUE);
	_PostFunctionPrologue(psRgxDevInfo, ui32Ordinal);

	if ((pui8Dest = _ReserveHWPerfStream(psRgxDevInfo, ui32Size)) == NULL)
	{
		goto cleanup;
	}

	_SetupHostPacketHeader(psRgxDevInfo, pui8Dest, RGX_HWPERF_HOST_FREE, ui32Size,
	                       ui32Ordinal, ui64Timestamp);
	_SetupHostFreePacketData(pui8Dest,
	                         eFreeType,
	                         ui64UID,
	                         ui32PID,
	                         ui32FWAddr);

	_CommitHWPerfStream(psRgxDevInfo, ui32Size);

cleanup:
	_PostFunctionEpilogue(psRgxDevInfo, ui32Ordinal);
}

static inline IMG_UINT32 _FixNameAndCalculateHostModifyPacketSize(
		RGX_HWPERF_HOST_RESOURCE_TYPE eModifyType,
		const IMG_CHAR **ppsName,
		IMG_UINT32 *ui32NameSize)
{
	RGX_HWPERF_HOST_MODIFY_DATA *psData;
	RGX_HWPERF_HOST_MODIFY_DETAIL *puData;
	IMG_UINT32 ui32Size = sizeof(psData->ui32ModifyType);

	if (*ppsName != NULL && *ui32NameSize > 0)
	{
		/* first strip the terminator */
		if ((*ppsName)[*ui32NameSize - 1] == '\0')
			*ui32NameSize -= 1;
		/* if string longer than maximum cut it (leave space for '\0') */
		if (*ui32NameSize >= PVRSRV_SYNC_NAME_LENGTH)
			*ui32NameSize = PVRSRV_SYNC_NAME_LENGTH - 1;
	}
	else
	{
		PVR_DPF((PVR_DBG_WARNING, "RGXHWPerfHostPostModifyEvent: Invalid"
				" resource name given."));
		*ppsName = UNKNOWN_SYNC_NAME;
		*ui32NameSize = sizeof(UNKNOWN_SYNC_NAME) - 1;
	}

	switch (eModifyType)
	{
		case RGX_HWPERF_HOST_RESOURCE_TYPE_FENCE_PVR:
			ui32Size += sizeof(puData->sFenceMerge) - PVRSRV_SYNC_NAME_LENGTH +
			*ui32NameSize + 1; /* +1 for '\0' */
			break;
		default:
			// unknown type - this should never happen
			PVR_DPF((PVR_DBG_ERROR,
					"RGXHWPerfHostPostModifyEvent: Invalid modify event type"));
			PVR_ASSERT(IMG_FALSE);
			break;
	}

	return RGX_HWPERF_MAKE_SIZE_VARIABLE(ui32Size);
}

static inline void _SetupHostModifyPacketData(IMG_UINT8 *pui8Dest,
                                              RGX_HWPERF_HOST_RESOURCE_TYPE eModifyType,
                                              IMG_UINT64 ui64NewUID,
                                              IMG_UINT64 ui64UID1,
                                              IMG_UINT64 ui64UID2,
                                              const IMG_CHAR *psName,
                                              IMG_UINT32 ui32NameSize)
{
	RGX_HWPERF_HOST_MODIFY_DATA *psData = (RGX_HWPERF_HOST_MODIFY_DATA *)IMG_OFFSET_ADDR(pui8Dest, sizeof(RGX_HWPERF_V2_PACKET_HDR));

	IMG_CHAR *acName = NULL;

	psData->ui32ModifyType = eModifyType;

	switch (eModifyType)
	{
		case RGX_HWPERF_HOST_RESOURCE_TYPE_FENCE_PVR:
			psData->uModifyDetail.sFenceMerge.ui64NewFence_UID = ui64NewUID;
			psData->uModifyDetail.sFenceMerge.ui64InFence1_UID = ui64UID1;
			psData->uModifyDetail.sFenceMerge.ui64InFence2_UID = ui64UID2;
			acName = psData->uModifyDetail.sFenceMerge.acName;
			break;
		default:
			// unknown type - this should never happen
			PVR_DPF((PVR_DBG_ERROR,
					"RGXHWPerfHostPostModifyEvent: Invalid modify event type"));
			PVR_ASSERT(IMG_FALSE);
	}

	if (acName != NULL)
	{
		if (ui32NameSize)
		{
			OSStringLCopy(acName, psName, ui32NameSize);
		}
		else
		{
			/* In case no name was given make sure we don't access random
			 * memory */
			acName[0] = '\0';
		}
	}
}

void RGXHWPerfHostPostModifyEvent(PVRSRV_RGXDEV_INFO *psRgxDevInfo,
                                  RGX_HWPERF_HOST_RESOURCE_TYPE eModifyType,
                                  IMG_UINT64 ui64NewUID,
                                  IMG_UINT64 ui64UID1,
                                  IMG_UINT64 ui64UID2,
                                  const IMG_CHAR *psName,
                                  IMG_UINT32 ui32NameSize)
{
	IMG_UINT8 *pui8Dest;
	IMG_UINT64 ui64Timestamp;
	IMG_UINT32 ui32Ordinal;
	IMG_UINT32 ui32Size = _FixNameAndCalculateHostModifyPacketSize(eModifyType,
	                                                               &psName,
	                                                               &ui32NameSize);

	_GetHWPerfHostPacketSpecifics(psRgxDevInfo, &ui32Ordinal, &ui64Timestamp,
	                              NULL, IMG_TRUE);
	_PostFunctionPrologue(psRgxDevInfo, ui32Ordinal);

	if ((pui8Dest = _ReserveHWPerfStream(psRgxDevInfo, ui32Size)) == NULL)
	{
		goto cleanup;
	}

	_SetupHostPacketHeader(psRgxDevInfo, pui8Dest, RGX_HWPERF_HOST_MODIFY, ui32Size,
	                       ui32Ordinal, ui64Timestamp);
	_SetupHostModifyPacketData(pui8Dest,
	                           eModifyType,
	                           ui64NewUID,
	                           ui64UID1,
	                           ui64UID2,
	                           psName,
	                           ui32NameSize);

	_CommitHWPerfStream(psRgxDevInfo, ui32Size);

cleanup:
	_PostFunctionEpilogue(psRgxDevInfo, ui32Ordinal);
}

static inline void _SetupHostClkSyncPacketData(PVRSRV_RGXDEV_INFO *psRgxDevInfo, IMG_UINT8 *pui8Dest)
{
	RGX_HWPERF_HOST_CLK_SYNC_DATA *psData = (RGX_HWPERF_HOST_CLK_SYNC_DATA *)
					IMG_OFFSET_ADDR(pui8Dest, sizeof(RGX_HWPERF_V2_PACKET_HDR));
	RGXFWIF_GPU_UTIL_FWCB *psGpuUtilFWCB = psRgxDevInfo->psRGXFWIfGpuUtilFWCb;
	IMG_UINT32 ui32CurrIdx =
			RGXFWIF_TIME_CORR_CURR_INDEX(psGpuUtilFWCB->ui32TimeCorrSeqCount);
	RGXFWIF_TIME_CORR *psTimeCorr = &psGpuUtilFWCB->sTimeCorr[ui32CurrIdx];

	psData->ui64CRTimestamp = psTimeCorr->ui64CRTimeStamp;
	psData->ui64OSTimestamp = psTimeCorr->ui64OSTimeStamp;
	psData->ui32ClockSpeed = psTimeCorr->ui32CoreClockSpeed;
}

void RGXHWPerfHostPostClkSyncEvent(PVRSRV_RGXDEV_INFO *psRgxDevInfo)
{
	IMG_UINT8 *pui8Dest;
	IMG_UINT32 ui32Size =
			RGX_HWPERF_MAKE_SIZE_FIXED(RGX_HWPERF_HOST_CLK_SYNC_DATA);
	IMG_UINT32 ui32Ordinal;
	IMG_UINT64 ui64Timestamp;

	/* if the buffer for time correlation data is not yet available (possibly
	 * device not initialised yet) skip this event */
	if (psRgxDevInfo->psRGXFWIfGpuUtilFWCb == NULL)
	{
		return;
	}

	_GetHWPerfHostPacketSpecifics(psRgxDevInfo, &ui32Ordinal, &ui64Timestamp,
	                              NULL, IMG_TRUE);
	_PostFunctionPrologue(psRgxDevInfo, ui32Ordinal);

	if ((pui8Dest = _ReserveHWPerfStream(psRgxDevInfo, ui32Size)) == NULL)
	{
		goto cleanup;
	}

	_SetupHostPacketHeader(psRgxDevInfo, pui8Dest, RGX_HWPERF_HOST_CLK_SYNC, ui32Size,
	                       ui32Ordinal, ui64Timestamp);
	_SetupHostClkSyncPacketData(psRgxDevInfo, pui8Dest);

	_CommitHWPerfStream(psRgxDevInfo, ui32Size);

cleanup:
	_PostFunctionEpilogue(psRgxDevInfo, ui32Ordinal);
}

static inline RGX_HWPERF_HOST_DEVICE_HEALTH_STATUS _ConvDeviceHealthStatus(PVRSRV_DEVICE_HEALTH_STATUS eDeviceHealthStatus)
{
	switch (eDeviceHealthStatus)
	{
		case PVRSRV_DEVICE_HEALTH_STATUS_UNDEFINED:			return RGX_HWPERF_HOST_DEVICE_HEALTH_STATUS_UNDEFINED;
		case PVRSRV_DEVICE_HEALTH_STATUS_OK:				return RGX_HWPERF_HOST_DEVICE_HEALTH_STATUS_OK;
		case PVRSRV_DEVICE_HEALTH_STATUS_NOT_RESPONDING:	return RGX_HWPERF_HOST_DEVICE_HEALTH_STATUS_RESPONDING;
		case PVRSRV_DEVICE_HEALTH_STATUS_DEAD:				return RGX_HWPERF_HOST_DEVICE_HEALTH_STATUS_DEAD;
		case PVRSRV_DEVICE_HEALTH_STATUS_FAULT:				return RGX_HWPERF_HOST_DEVICE_HEALTH_STATUS_FAULT;
		default:											return RGX_HWPERF_HOST_DEVICE_HEALTH_STATUS_UNDEFINED;
	}
}

static inline RGX_HWPERF_HOST_DEVICE_HEALTH_REASON _ConvDeviceHealthReason(PVRSRV_DEVICE_HEALTH_REASON eDeviceHealthReason)
{
	switch (eDeviceHealthReason)
	{
		case PVRSRV_DEVICE_HEALTH_REASON_NONE:				return RGX_HWPERF_HOST_DEVICE_HEALTH_REASON_NONE;
		case PVRSRV_DEVICE_HEALTH_REASON_ASSERTED:			return RGX_HWPERF_HOST_DEVICE_HEALTH_REASON_ASSERTED;
		case PVRSRV_DEVICE_HEALTH_REASON_POLL_FAILING:		return RGX_HWPERF_HOST_DEVICE_HEALTH_REASON_POLL_FAILING;
		case PVRSRV_DEVICE_HEALTH_REASON_TIMEOUTS:			return RGX_HWPERF_HOST_DEVICE_HEALTH_REASON_TIMEOUTS;
		case PVRSRV_DEVICE_HEALTH_REASON_QUEUE_CORRUPT:		return RGX_HWPERF_HOST_DEVICE_HEALTH_REASON_QUEUE_CORRUPT;
		case PVRSRV_DEVICE_HEALTH_REASON_QUEUE_STALLED:		return RGX_HWPERF_HOST_DEVICE_HEALTH_REASON_QUEUE_STALLED;
		case PVRSRV_DEVICE_HEALTH_REASON_IDLING:			return RGX_HWPERF_HOST_DEVICE_HEALTH_REASON_IDLING;
		case PVRSRV_DEVICE_HEALTH_REASON_RESTARTING:		return RGX_HWPERF_HOST_DEVICE_HEALTH_REASON_RESTARTING;
		case PVRSRV_DEVICE_HEALTH_REASON_MISSING_INTERRUPTS:return RGX_HWPERF_HOST_DEVICE_HEALTH_REASON_MISSING_INTERRUPTS;
		default:											return RGX_HWPERF_HOST_DEVICE_HEALTH_REASON_UNDEFINED;
	}
}

static inline void _SetupHostDeviceInfoPacketData(RGX_HWPERF_DEV_INFO_EV eEvType,
												  PVRSRV_DEVICE_HEALTH_STATUS eDeviceHealthStatus,
												  PVRSRV_DEVICE_HEALTH_REASON eDeviceHealthReason,
												  IMG_UINT8 *pui8Dest)
{
	RGX_HWPERF_HOST_DEV_INFO_DATA *psData = (RGX_HWPERF_HOST_DEV_INFO_DATA *)IMG_OFFSET_ADDR(pui8Dest, sizeof(RGX_HWPERF_V2_PACKET_HDR));
	psData->eEvType = eEvType;

	switch (eEvType)
	{
		case RGX_HWPERF_DEV_INFO_EV_HEALTH:
			psData->uDevInfoDetail.sDeviceStatus.eDeviceHealthStatus = _ConvDeviceHealthStatus(eDeviceHealthStatus);
			psData->uDevInfoDetail.sDeviceStatus.eDeviceHealthReason = _ConvDeviceHealthReason(eDeviceHealthReason);
			break;
		default:
			// unknown type - this should never happen
			PVR_DPF((PVR_DBG_ERROR, "RGXHWPerfHostPostDeviceInfo: Invalid event type"));
			PVR_ASSERT(IMG_FALSE);
			break;
	}
}

static inline IMG_UINT32 _CalculateHostDeviceInfoPacketSize(RGX_HWPERF_DEV_INFO_EV eEvType)
{
	IMG_UINT32 ui32Size = offsetof(RGX_HWPERF_HOST_DEV_INFO_DATA, uDevInfoDetail);

	switch (eEvType)
	{
		case RGX_HWPERF_DEV_INFO_EV_HEALTH:
			ui32Size += sizeof(((RGX_HWPERF_HOST_DEV_INFO_DATA*)0)->uDevInfoDetail.sDeviceStatus);
			break;
		default:
			// unknown type - this should never happen
			PVR_DPF((PVR_DBG_ERROR, "RGXHWPerfHostPostDeviceInfo: Invalid event type"));
			PVR_ASSERT(IMG_FALSE);
			break;
	}
	return RGX_HWPERF_MAKE_SIZE_VARIABLE(ui32Size);
}

void RGXHWPerfHostPostDeviceInfo(PVRSRV_RGXDEV_INFO *psRgxDevInfo,
								 RGX_HWPERF_DEV_INFO_EV eEvType,
								 PVRSRV_DEVICE_HEALTH_STATUS eDeviceHealthStatus,
								 PVRSRV_DEVICE_HEALTH_REASON eDeviceHealthReason)
{
	IMG_UINT8 *pui8Dest;
	IMG_UINT32 ui32Ordinal;
	IMG_UINT64 ui64Timestamp;
	IMG_UINT32 ui32Size;

	OSLockAcquire(psRgxDevInfo->hHWPerfLock);

	if (psRgxDevInfo->hHWPerfHostStream != (IMG_HANDLE) NULL)
	{
		_GetHWPerfHostPacketSpecifics(psRgxDevInfo, &ui32Ordinal, &ui64Timestamp, NULL, IMG_TRUE);
		_PostFunctionPrologue(psRgxDevInfo, ui32Ordinal);
		ui32Size = _CalculateHostDeviceInfoPacketSize(eEvType);

		if ((pui8Dest = _ReserveHWPerfStream(psRgxDevInfo, ui32Size)) != NULL)
		{
			_SetupHostPacketHeader(psRgxDevInfo, pui8Dest, RGX_HWPERF_HOST_DEV_INFO, ui32Size, ui32Ordinal, ui64Timestamp);
			_SetupHostDeviceInfoPacketData(eEvType, eDeviceHealthStatus, eDeviceHealthReason, pui8Dest);
			_CommitHWPerfStream(psRgxDevInfo, ui32Size);
		}

		_PostFunctionEpilogue(psRgxDevInfo, ui32Ordinal);
	}

	OSLockRelease(psRgxDevInfo->hHWPerfLock);
}

static inline void _SetupHostInfoPacketData(RGX_HWPERF_INFO_EV eEvType,
												  IMG_UINT32 ui32TotalMemoryUsage,
												  IMG_UINT32 ui32LivePids,
												  PVRSRV_PER_PROCESS_MEM_USAGE *psPerProcessMemUsage,
												  IMG_UINT8 *pui8Dest)
{
	IMG_INT i;
	RGX_HWPERF_HOST_INFO_DATA *psData = (RGX_HWPERF_HOST_INFO_DATA *)IMG_OFFSET_ADDR(pui8Dest, sizeof(RGX_HWPERF_V2_PACKET_HDR));
	psData->eEvType = eEvType;

	switch (eEvType)
	{
		case RGX_HWPERF_INFO_EV_MEM_USAGE:
			psData->uInfoDetail.sMemUsageStats.ui32TotalMemoryUsage = ui32TotalMemoryUsage;

			if (psPerProcessMemUsage)
			{
				for (i = 0; i < ui32LivePids; ++i)
				{
					psData->uInfoDetail.sMemUsageStats.sPerProcessUsage[i].ui32Pid = psPerProcessMemUsage[i].ui32Pid;
					psData->uInfoDetail.sMemUsageStats.sPerProcessUsage[i].ui32KernelMemUsage = psPerProcessMemUsage[i].ui32KernelMemUsage;
					psData->uInfoDetail.sMemUsageStats.sPerProcessUsage[i].ui32GraphicsMemUsage = psPerProcessMemUsage[i].ui32GraphicsMemUsage;
				}
			}
			break;
		default:
			// unknown type - this should never happen
			PVR_DPF((PVR_DBG_ERROR, "RGXHWPerfHostPostInfo: Invalid event type"));
			PVR_ASSERT(IMG_FALSE);
			break;
	}
}

static inline IMG_UINT32 _CalculateHostInfoPacketSize(RGX_HWPERF_INFO_EV eEvType,
															IMG_UINT32 *pui32TotalMemoryUsage,
															IMG_UINT32 *pui32LivePids,
															PVRSRV_PER_PROCESS_MEM_USAGE **ppsPerProcessMemUsage)
{
	IMG_UINT32 ui32Size = offsetof(RGX_HWPERF_HOST_INFO_DATA, uInfoDetail);

	switch (eEvType)
	{
		case RGX_HWPERF_INFO_EV_MEM_USAGE:
#if !defined(__QNXNTO__)
			if (PVRSRVGetProcessMemUsage(pui32TotalMemoryUsage, pui32LivePids, ppsPerProcessMemUsage) == PVRSRV_OK)
			{
				ui32Size += ((offsetof(RGX_HWPERF_HOST_INFO_DATA, uInfoDetail.sMemUsageStats.ui32TotalMemoryUsage) - ui32Size)
					+ ((*pui32LivePids) * sizeof(((RGX_HWPERF_HOST_INFO_DATA*)0)->uInfoDetail.sMemUsageStats.sPerProcessUsage)));
			}
#else
			PVR_DPF((PVR_DBG_ERROR, "This functionality is not yet implemented for this platform"));
#endif
			break;
		default:
			// unknown type - this should never happen
			PVR_DPF((PVR_DBG_ERROR, "RGXHWPerfHostPostInfo: Invalid event type"));
			PVR_ASSERT(IMG_FALSE);
			break;
	}
	return RGX_HWPERF_MAKE_SIZE_VARIABLE(ui32Size);
}

void RGXHWPerfHostPostInfo(PVRSRV_RGXDEV_INFO *psRgxDevInfo,
								 RGX_HWPERF_INFO_EV eEvType)
{
	IMG_UINT8 *pui8Dest;
	IMG_UINT32 ui32Size;
	IMG_UINT32 ui32Ordinal;
	IMG_UINT64 ui64Timestamp;
	IMG_UINT32 ui32TotalMemoryUsage = 0;
	PVRSRV_PER_PROCESS_MEM_USAGE *psPerProcessMemUsage = NULL;
	IMG_UINT32 ui32LivePids = 0;

	OSLockAcquire(psRgxDevInfo->hHWPerfLock);

	if (psRgxDevInfo->hHWPerfHostStream != (IMG_HANDLE) NULL)
	{
		_GetHWPerfHostPacketSpecifics(psRgxDevInfo, &ui32Ordinal, &ui64Timestamp, NULL, IMG_TRUE);
		_PostFunctionPrologue(psRgxDevInfo, ui32Ordinal);

		ui32Size = _CalculateHostInfoPacketSize(eEvType, &ui32TotalMemoryUsage, &ui32LivePids, &psPerProcessMemUsage);

		if ((pui8Dest = _ReserveHWPerfStream(psRgxDevInfo, ui32Size)) != NULL)
		{
			_SetupHostPacketHeader(psRgxDevInfo, pui8Dest, RGX_HWPERF_HOST_INFO, ui32Size, ui32Ordinal, ui64Timestamp);
			_SetupHostInfoPacketData(eEvType, ui32TotalMemoryUsage, ui32LivePids, psPerProcessMemUsage, pui8Dest);
			_CommitHWPerfStream(psRgxDevInfo, ui32Size);
		}

		_PostFunctionEpilogue(psRgxDevInfo, ui32Ordinal);

		if (psPerProcessMemUsage)
			OSFreeMemNoStats(psPerProcessMemUsage); // psPerProcessMemUsage was allocated with OSAllocZMemNoStats
	}

	OSLockRelease(psRgxDevInfo->hHWPerfLock);
}

static inline IMG_UINT32
_CalculateHostFenceWaitPacketSize(RGX_HWPERF_HOST_SYNC_FENCE_WAIT_TYPE eWaitType)
{
	RGX_HWPERF_HOST_SYNC_FENCE_WAIT_DATA *psSizeCalculator;
	IMG_UINT32 ui32Size = offsetof(RGX_HWPERF_HOST_SYNC_FENCE_WAIT_DATA, uDetail);

	switch (eWaitType)
	{
		case RGX_HWPERF_HOST_SYNC_FENCE_WAIT_TYPE_BEGIN:
			ui32Size += sizeof(psSizeCalculator->uDetail.sBegin);
			break;
		case RGX_HWPERF_HOST_SYNC_FENCE_WAIT_TYPE_END:
			ui32Size += sizeof(psSizeCalculator->uDetail.sEnd);
			break;
		default:
			// unknown type - this should never happen
			PVR_DPF((PVR_DBG_ERROR, "%s: Invalid wait event type (%u)", __func__,
			         eWaitType));
			PVR_ASSERT(IMG_FALSE);
			break;
	}
	return RGX_HWPERF_MAKE_SIZE_VARIABLE(ui32Size);
}

static inline void
_SetupHostFenceWaitPacketData(IMG_UINT8 *pui8Dest,
                              RGX_HWPERF_HOST_SYNC_FENCE_WAIT_TYPE eWaitType,
                              IMG_PID uiPID,
                              PVRSRV_FENCE hFence,
                              IMG_UINT32 ui32Data)
{
	RGX_HWPERF_HOST_SYNC_FENCE_WAIT_DATA *psData = (RGX_HWPERF_HOST_SYNC_FENCE_WAIT_DATA *)
	                IMG_OFFSET_ADDR(pui8Dest, sizeof(RGX_HWPERF_V2_PACKET_HDR));

	psData->eType = eWaitType;
	psData->uiPID = uiPID;
	psData->hFence = hFence;

	switch (eWaitType)
	{
		case RGX_HWPERF_HOST_SYNC_FENCE_WAIT_TYPE_BEGIN:
			psData->uDetail.sBegin.ui32TimeoutInMs = ui32Data;
			break;
		case RGX_HWPERF_HOST_SYNC_FENCE_WAIT_TYPE_END:
			psData->uDetail.sEnd.eResult =
			    (RGX_HWPERF_HOST_SYNC_FENCE_WAIT_RESULT) ui32Data;
			break;
		default:
			// unknown type - this should never happen
			PVR_DPF((PVR_DBG_ERROR,
					"%s: Invalid fence-wait event type", __func__));
			PVR_ASSERT(IMG_FALSE);
	}
}

void RGXHWPerfHostPostFenceWait(PVRSRV_RGXDEV_INFO *psRgxDevInfo,
								RGX_HWPERF_HOST_SYNC_FENCE_WAIT_TYPE eType,
								IMG_PID uiPID,
								PVRSRV_FENCE hFence,
								IMG_UINT32 ui32Data)
{
	IMG_UINT8 *pui8Dest;
	IMG_UINT32 ui32Size;
	IMG_UINT32 ui32Ordinal;
	IMG_UINT64 ui64Timestamp;

	_GetHWPerfHostPacketSpecifics(psRgxDevInfo, &ui32Ordinal, &ui64Timestamp,
	                              NULL, IMG_TRUE);

	_PostFunctionPrologue(psRgxDevInfo, ui32Ordinal);

	ui32Size = _CalculateHostFenceWaitPacketSize(eType);
	if ((pui8Dest = _ReserveHWPerfStream(psRgxDevInfo, ui32Size)) == NULL)
	{
		goto cleanup;
	}

	_SetupHostPacketHeader(psRgxDevInfo, pui8Dest, RGX_HWPERF_HOST_SYNC_FENCE_WAIT,
	                       ui32Size, ui32Ordinal, ui64Timestamp);
	_SetupHostFenceWaitPacketData(pui8Dest, eType, uiPID, hFence, ui32Data);

	_CommitHWPerfStream(psRgxDevInfo, ui32Size);

cleanup:
	_PostFunctionEpilogue(psRgxDevInfo, ui32Ordinal);
}

static inline IMG_UINT32 _CalculateHostSWTimelineAdvPacketSize(void)
{
	IMG_UINT32 ui32Size = sizeof(RGX_HWPERF_HOST_SYNC_SW_TL_ADV_DATA);
	return RGX_HWPERF_MAKE_SIZE_VARIABLE(ui32Size);
}

static inline void
_SetupHostSWTimelineAdvPacketData(IMG_UINT8 *pui8Dest,
                                  IMG_PID uiPID,
								  PVRSRV_TIMELINE hSWTimeline,
								  IMG_UINT64 ui64SyncPtIndex)

{
	RGX_HWPERF_HOST_SYNC_SW_TL_ADV_DATA *psData = (RGX_HWPERF_HOST_SYNC_SW_TL_ADV_DATA *)
	                IMG_OFFSET_ADDR(pui8Dest, sizeof(RGX_HWPERF_V2_PACKET_HDR));

	psData->uiPID = uiPID;
	psData->hTimeline = hSWTimeline;
	psData->ui64SyncPtIndex = ui64SyncPtIndex;
}

void RGXHWPerfHostPostSWTimelineAdv(PVRSRV_RGXDEV_INFO *psRgxDevInfo,
                                    IMG_PID uiPID,
                                    PVRSRV_TIMELINE hSWTimeline,
                                    IMG_UINT64 ui64SyncPtIndex)
{
	IMG_UINT8 *pui8Dest;
	IMG_UINT32 ui32Size;
	IMG_UINT32 ui32Ordinal;
	IMG_UINT64 ui64Timestamp;

	_GetHWPerfHostPacketSpecifics(psRgxDevInfo, &ui32Ordinal, &ui64Timestamp,
	                              NULL, IMG_TRUE);

	_PostFunctionPrologue(psRgxDevInfo, ui32Ordinal);

	ui32Size = _CalculateHostSWTimelineAdvPacketSize();
	if ((pui8Dest = _ReserveHWPerfStream(psRgxDevInfo, ui32Size)) == NULL)
	{
		goto cleanup;
	}

	_SetupHostPacketHeader(psRgxDevInfo, pui8Dest, RGX_HWPERF_HOST_SYNC_SW_TL_ADVANCE,
	                       ui32Size, ui32Ordinal, ui64Timestamp);
	_SetupHostSWTimelineAdvPacketData(pui8Dest, uiPID, hSWTimeline, ui64SyncPtIndex);

	_CommitHWPerfStream(psRgxDevInfo, ui32Size);

cleanup:
	_PostFunctionEpilogue(psRgxDevInfo, ui32Ordinal);

}

void RGXHWPerfHostPostClientInfoProcName(PVRSRV_RGXDEV_INFO *psRgxDevInfo,
                                         IMG_PID uiPID,
									     const IMG_CHAR *psName)
{
	RGX_HWPERF_HOST_CLIENT_INFO_DATA* psPkt;
	IMG_UINT8 *pui8Dest;
	IMG_UINT32 ui32Size;
	IMG_UINT32 ui32NameLen;
	IMG_UINT32 ui32Ordinal;
	IMG_UINT64 ui64Timestamp;

	_GetHWPerfHostPacketSpecifics(psRgxDevInfo, &ui32Ordinal, &ui64Timestamp, NULL, IMG_TRUE);
	_PostFunctionPrologue(psRgxDevInfo, ui32Ordinal);

	ui32NameLen = OSStringLength(psName) + 1U;
	ui32Size = RGX_HWPERF_MAKE_SIZE_VARIABLE(RGX_HWPERF_HOST_CLIENT_INFO_PROC_NAME_BASE_SIZE
		+ RGX_HWPERF_HOST_CLIENT_PROC_NAME_SIZE(ui32NameLen));

	if ((pui8Dest = _ReserveHWPerfStream(psRgxDevInfo, ui32Size)) == NULL)
	{
		goto cleanup;
	}

	_SetupHostPacketHeader(psRgxDevInfo, pui8Dest, RGX_HWPERF_HOST_CLIENT_INFO,
	                       ui32Size, ui32Ordinal, ui64Timestamp);

	psPkt = (RGX_HWPERF_HOST_CLIENT_INFO_DATA*)IMG_OFFSET_ADDR(pui8Dest, sizeof(RGX_HWPERF_V2_PACKET_HDR));
	psPkt->eType = RGX_HWPERF_HOST_CLIENT_INFO_TYPE_PROCESS_NAME;
	psPkt->uDetail.sProcName.ui32Count = 1U;
	psPkt->uDetail.sProcName.asProcNames[0].uiClientPID = uiPID;
	psPkt->uDetail.sProcName.asProcNames[0].ui32Length = ui32NameLen;
	(void)OSStringLCopy(psPkt->uDetail.sProcName.asProcNames[0].acName, psName, ui32NameLen);

	_CommitHWPerfStream(psRgxDevInfo, ui32Size);

cleanup:
	_PostFunctionEpilogue(psRgxDevInfo, ui32Ordinal);
}

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
	IMG_HANDLE          hSD[RGX_HWPERF_MAX_STREAM_ID];

	/* TL Acquire/release state */
	IMG_PBYTE			pHwpBuf[RGX_HWPERF_MAX_STREAM_ID];			/*!< buffer returned to user in acquire call */
	IMG_PBYTE			pHwpBufEnd[RGX_HWPERF_MAX_STREAM_ID];		/*!< pointer to end of HwpBuf */
	IMG_PBYTE			pTlBuf[RGX_HWPERF_MAX_STREAM_ID];			/*!< buffer obtained via TlAcquireData */
	IMG_PBYTE			pTlBufPos[RGX_HWPERF_MAX_STREAM_ID];		/*!< initial position in TlBuf to acquire packets */
	IMG_PBYTE			pTlBufRead[RGX_HWPERF_MAX_STREAM_ID];		/*!< pointer to the last packet read */
	IMG_UINT32			ui32AcqDataLen[RGX_HWPERF_MAX_STREAM_ID];	/*!< length of acquired TlBuf */
	IMG_BOOL			bRelease[RGX_HWPERF_MAX_STREAM_ID];		/*!< used to determine whether or not to release currently held TlBuf */


} RGX_KM_HWPERF_DEVDATA;

PVRSRV_ERROR RGXHWPerfLazyConnect(RGX_HWPERF_CONNECTION** ppsHWPerfConnection)
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_DEVICE_NODE *psDeviceNode;
	RGX_KM_HWPERF_DEVDATA *psDevData;
	RGX_HWPERF_DEVICE *psNewHWPerfDevice;
	RGX_HWPERF_CONNECTION* psHWPerfConnection;
	IMG_BOOL bFWActive = IMG_FALSE;

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_IMPLEMENTED);

	/* avoid uninitialised data */
	PVR_ASSERT(*ppsHWPerfConnection == NULL);
	PVR_ASSERT(psPVRSRVData);

	/* Allocate connection object */
	psHWPerfConnection = OSAllocZMem(sizeof(*psHWPerfConnection));
	if (!psHWPerfConnection)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	/* early save the return pointer to aid clean-up if failure occurs */
	*ppsHWPerfConnection = psHWPerfConnection;

	OSWRLockAcquireRead(psPVRSRVData->hDeviceNodeListLock);
	psDeviceNode = psPVRSRVData->psDeviceNodeList;

	while (psDeviceNode)
	{
		if (psDeviceNode->eDevState != PVRSRV_DEVICE_STATE_ACTIVE)
		{
			PVR_DPF((PVR_DBG_WARNING,
					 "%s: HWPerf: Device not currently active. ID:%u",
					 __func__,
					 psDeviceNode->sDevId.i32OsDeviceID));
			psDeviceNode = psDeviceNode->psNext;
			continue;
		}
		/* Create a list node to be attached to connection object's list */
		psNewHWPerfDevice = OSAllocMem(sizeof(*psNewHWPerfDevice));
		if (!psNewHWPerfDevice)
		{
			OSWRLockReleaseRead(psPVRSRVData->hDeviceNodeListLock);
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}
		/* Insert node at head of the list */
		psNewHWPerfDevice->psNext = psHWPerfConnection->psHWPerfDevList;
		psHWPerfConnection->psHWPerfDevList = psNewHWPerfDevice;

		/* create a device data object for kernel server */
		psDevData = OSAllocZMem(sizeof(*psDevData));
		psNewHWPerfDevice->hDevData = (IMG_HANDLE)psDevData;
		if (!psDevData)
		{
			OSWRLockReleaseRead(psPVRSRVData->hDeviceNodeListLock);
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}
		if (OSSNPrintf(psNewHWPerfDevice->pszName, sizeof(psNewHWPerfDevice->pszName),
			           "hwperf_device_%d", psDeviceNode->sDevId.i32OsDeviceID) < 0)
		{
			OSWRLockReleaseRead(psPVRSRVData->hDeviceNodeListLock);
			PVR_DPF((PVR_DBG_ERROR,
					 "%s: Failed to form HWPerf device name for device %d",
					__func__,
					psDeviceNode->sDevId.i32OsDeviceID));
			return PVRSRV_ERROR_INVALID_PARAMS;
		}

		psDevData->psRgxDevNode = psDeviceNode;
		psDevData->psRgxDevInfo = psDeviceNode->pvDevice;

		psDeviceNode = psDeviceNode->psNext;

		/* At least one device is active */
		bFWActive = IMG_TRUE;
	}

	OSWRLockReleaseRead(psPVRSRVData->hDeviceNodeListLock);

	if (!bFWActive)
	{
		return PVRSRV_ERROR_NOT_READY;
	}

	return PVRSRV_OK;
}

PVRSRV_ERROR RGXHWPerfOpen(RGX_HWPERF_CONNECTION *psHWPerfConnection)
{
	RGX_KM_HWPERF_DEVDATA *psDevData;
	RGX_HWPERF_DEVICE *psHWPerfDev;
	PVRSRV_RGXDEV_INFO *psRgxDevInfo;
	PVRSRV_ERROR eError;
	IMG_CHAR pszHWPerfFwStreamName[sizeof(PVRSRV_TL_HWPERF_RGX_FW_STREAM) + 5];
	IMG_CHAR pszHWPerfHostStreamName[sizeof(PVRSRV_TL_HWPERF_HOST_SERVER_STREAM) + 5];
	IMG_UINT32 ui32BufSize;

	/* Disable producer callback by default for the Kernel API. */
	IMG_UINT32 ui32StreamFlags = PVRSRV_STREAM_FLAG_ACQUIRE_NONBLOCKING |
			PVRSRV_STREAM_FLAG_DISABLE_PRODUCER_CALLBACK;

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_IMPLEMENTED);

	/* Validate input argument values supplied by the caller */
	if (!psHWPerfConnection)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psHWPerfDev = psHWPerfConnection->psHWPerfDevList;
	while (psHWPerfDev)
	{
		psDevData = (RGX_KM_HWPERF_DEVDATA *) psHWPerfDev->hDevData;
		psRgxDevInfo = psDevData->psRgxDevInfo;

		/* In the case where the AppHint has not been set we need to
		 * initialise the HWPerf resources here. Allocated on-demand
		 * to reduce RAM foot print on systems not needing HWPerf.
		 */
		OSLockAcquire(psRgxDevInfo->hHWPerfLock);
		if (RGXHWPerfIsInitRequired(psRgxDevInfo))
		{
			eError = RGXHWPerfInitOnDemandResources(psRgxDevInfo);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,
						 "%s: Initialisation of on-demand HWPerfFW resources failed",
						 __func__));
				OSLockRelease(psRgxDevInfo->hHWPerfLock);
				return eError;
			}
		}
		OSLockRelease(psRgxDevInfo->hHWPerfLock);

		OSLockAcquire(psRgxDevInfo->hLockHWPerfHostStream);
		if (psRgxDevInfo->hHWPerfHostStream == NULL)
		{
			eError = RGXHWPerfHostInitOnDemandResources(psRgxDevInfo);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,
						 "%s: Initialisation of on-demand HWPerfHost resources failed",
						 __func__));
				OSLockRelease(psRgxDevInfo->hLockHWPerfHostStream);
				return eError;
			}
		}
		OSLockRelease(psRgxDevInfo->hLockHWPerfHostStream);

		/* form the HWPerf stream name, corresponding to this DevNode; which can make sense in the UM */
		if (OSSNPrintf(pszHWPerfFwStreamName, sizeof(pszHWPerfFwStreamName), "%s%d",
		               PVRSRV_TL_HWPERF_RGX_FW_STREAM,
		               psRgxDevInfo->psDeviceNode->sDevId.i32OsDeviceID) < 0)
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "%s: Failed to form HWPerf stream name for device %d",
					__func__,
					psRgxDevInfo->psDeviceNode->sDevId.i32OsDeviceID));
			return PVRSRV_ERROR_INVALID_PARAMS;
		}
		/* Open the RGX TL stream for reading in this session */
		eError = TLClientOpenStream(DIRECT_BRIDGE_HANDLE,
		                            pszHWPerfFwStreamName,
		                            ui32StreamFlags,
		                            &psDevData->hSD[RGX_HWPERF_STREAM_ID0_FW]);
		PVR_LOG_RETURN_IF_ERROR(eError, "TLClientOpenStream(RGX_HWPerf)");

		/* form the HWPerf host stream name, corresponding to this DevNode; which can make sense in the UM */
		if (OSSNPrintf(pszHWPerfHostStreamName, sizeof(pszHWPerfHostStreamName), "%s%d",
		               PVRSRV_TL_HWPERF_HOST_SERVER_STREAM,
		               psRgxDevInfo->psDeviceNode->sDevId.i32OsDeviceID) < 0)
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "%s: Failed to form HWPerf host stream name for device %d",
					__func__,
					psRgxDevInfo->psDeviceNode->sDevId.i32OsDeviceID));
			return PVRSRV_ERROR_INVALID_PARAMS;
		}

		/* Open the host TL stream for reading in this session */
		eError = TLClientOpenStream(DIRECT_BRIDGE_HANDLE,
		                            pszHWPerfHostStreamName,
		                            PVRSRV_STREAM_FLAG_ACQUIRE_NONBLOCKING,
		                            &psDevData->hSD[RGX_HWPERF_STREAM_ID1_HOST]);
		PVR_LOG_RETURN_IF_ERROR(eError, "TLClientOpenStream(Host_HWPerf)");

		/* Allocate a large enough buffer for use during the entire session to
		 * avoid the need to resize in the Acquire call as this might be in an ISR
		 * Choose size that can contain at least one packet.
		 */
		/* Allocate buffer for FW Stream */
		ui32BufSize = FW_STREAM_BUFFER_SIZE;
		psDevData->pHwpBuf[RGX_HWPERF_STREAM_ID0_FW] = OSAllocMem(ui32BufSize);
		if (psDevData->pHwpBuf[RGX_HWPERF_STREAM_ID0_FW] == NULL)
		{
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}
		psDevData->pHwpBufEnd[RGX_HWPERF_STREAM_ID0_FW] = psDevData->pHwpBuf[RGX_HWPERF_STREAM_ID0_FW]+ui32BufSize;

		/* Allocate buffer for Host Stream */
		ui32BufSize = HOST_STREAM_BUFFER_SIZE;
		psDevData->pHwpBuf[RGX_HWPERF_STREAM_ID1_HOST] = OSAllocMem(ui32BufSize);
		if (psDevData->pHwpBuf[RGX_HWPERF_STREAM_ID1_HOST] == NULL)
		{
			OSFreeMem(psDevData->pHwpBuf[RGX_HWPERF_STREAM_ID0_FW]);
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}
		psDevData->pHwpBufEnd[RGX_HWPERF_STREAM_ID1_HOST] = psDevData->pHwpBuf[RGX_HWPERF_STREAM_ID1_HOST]+ui32BufSize;

		psHWPerfDev = psHWPerfDev->psNext;
	}

	return PVRSRV_OK;
}


PVRSRV_ERROR RGXHWPerfConnect(RGX_HWPERF_CONNECTION** ppsHWPerfConnection)
{
	PVRSRV_ERROR eError;

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_IMPLEMENTED);

	eError = RGXHWPerfLazyConnect(ppsHWPerfConnection);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXHWPerfLazyConnect", e0);

	eError = RGXHWPerfOpen(*ppsHWPerfConnection);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXHWPerfOpen", e1);

	return PVRSRV_OK;

e1: /* HWPerfOpen might have opened some, and then failed */
	RGXHWPerfClose(*ppsHWPerfConnection);
e0: /* LazyConnect might have allocated some resources and then failed,
	 * make sure they are cleaned up */
	RGXHWPerfFreeConnection(ppsHWPerfConnection);
	return eError;
}

/*
    PVRSRVRGXControlHWPerfBlocksKM
 */
PVRSRV_ERROR PVRSRVRGXControlHWPerfBlocksKM(
		CONNECTION_DATA             * psConnection,
		PVRSRV_DEVICE_NODE          * psDeviceNode,
		IMG_BOOL                      bEnable,
		IMG_UINT32                    ui32ArrayLen,
		IMG_UINT16                  * psBlockIDs)
{
	PVRSRV_ERROR		eError = PVRSRV_OK;
	RGXFWIF_KCCB_CMD	sKccbCmd;
	IMG_UINT32			ui32kCCBCommandSlot;
	PVRSRV_RGXDEV_INFO	*psDevice;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_SUPPORTED);

	PVR_DPF_ENTERED;

	PVR_LOG_RETURN_IF_INVALID_PARAM(psBlockIDs != NULL, "psBlockIDs");
	PVR_LOG_RETURN_IF_INVALID_PARAM((ui32ArrayLen>0) && (ui32ArrayLen <= RGXFWIF_HWPERF_CTRL_BLKS_MAX), "ui32ArrayLen");

	PVR_ASSERT(psDeviceNode);
	psDevice = psDeviceNode->pvDevice;

	/* Fill in the command structure with the parameters needed
	 */
	sKccbCmd.eCmdType = RGXFWIF_KCCB_CMD_HWPERF_CTRL_BLKS;
	sKccbCmd.uCmdData.sHWPerfCtrlBlks.bEnable = bEnable;
	sKccbCmd.uCmdData.sHWPerfCtrlBlks.ui32NumBlocks = ui32ArrayLen;

	OSDeviceMemCopy(sKccbCmd.uCmdData.sHWPerfCtrlBlks.aeBlockIDs, psBlockIDs, sizeof(IMG_UINT16) * ui32ArrayLen);


	/* Ask the FW to carry out the HWPerf configuration command
	 */
	eError = RGXScheduleCommandAndGetKCCBSlot(psDevice,
	                                          RGXFWIF_DM_GP,
	                                          &sKccbCmd,
	                                          PDUMP_FLAGS_CONTINUOUS,
	                                          &ui32kCCBCommandSlot);
	PVR_LOG_RETURN_IF_ERROR(eError, "RGXScheduleCommandAndGetKCCBSlot");

	/* Wait for FW to complete */
	eError = RGXWaitForKCCBSlotUpdate(psDevice, ui32kCCBCommandSlot, PDUMP_FLAGS_CONTINUOUS);
	PVR_LOG_RETURN_IF_ERROR(eError, "RGXWaitForKCCBSlotUpdate");


#if defined(DEBUG)
	if (bEnable)
		PVR_DPF((PVR_DBG_WARNING, "HWPerf %d counter blocks have been ENABLED", ui32ArrayLen));
	else
		PVR_DPF((PVR_DBG_WARNING, "HWPerf %d counter blocks have been DISABLED", ui32ArrayLen));
#endif

	PVR_DPF_RETURN_OK;
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

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_IMPLEMENTED);

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
	else if (eStreamId == RGX_HWPERF_STREAM_ID2_CLIENT)
	{
		IMG_UINT32 ui32Index = (IMG_UINT32) (ui64Mask >> 32);
		IMG_UINT32 ui32Mask = (IMG_UINT32) ui64Mask;

		return RGXHWPerfCtrlClientBuffer(bToggle, ui32Index, ui32Mask);
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVRGXCtrlHWPerfKM: Unknown stream id."));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	PVR_DPF_RETURN_OK;
}

PVRSRV_ERROR RGXHWPerfControl(
		RGX_HWPERF_CONNECTION *psHWPerfConnection,
		RGX_HWPERF_STREAM_ID eStreamId,
		IMG_BOOL             bToggle,
		IMG_UINT64           ui64Mask)
{
	PVRSRV_ERROR           eError;
	RGX_KM_HWPERF_DEVDATA* psDevData;
	RGX_HWPERF_DEVICE* psHWPerfDev;

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_IMPLEMENTED);

	/* Validate input argument values supplied by the caller */
	if (!psHWPerfConnection)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psHWPerfDev = psHWPerfConnection->psHWPerfDevList;

	while (psHWPerfDev)
	{
		psDevData = (RGX_KM_HWPERF_DEVDATA *) psHWPerfDev->hDevData;

		/* Call the internal server API */
		eError = PVRSRVRGXCtrlHWPerfKM(NULL, psDevData->psRgxDevNode, eStreamId, bToggle, ui64Mask);
		PVR_LOG_RETURN_IF_ERROR(eError, "PVRSRVRGXCtrlHWPerfKM");

		psHWPerfDev = psHWPerfDev->psNext;
	}

	return PVRSRV_OK;
}


IMG_INTERNAL PVRSRV_ERROR RGXHWPerfToggleCounters(
		RGX_HWPERF_CONNECTION *psHWPerfConnection,
		IMG_UINT32   ui32NumBlocks,
		IMG_UINT16*  aeBlockIDs,
		IMG_BOOL     bToggle,
		const char* szFunctionString);

IMG_INTERNAL PVRSRV_ERROR RGXHWPerfToggleCounters(
		RGX_HWPERF_CONNECTION *psHWPerfConnection,
		IMG_UINT32   ui32NumBlocks,
		IMG_UINT16*  aeBlockIDs,
		IMG_BOOL     bToggle,
		const char* szFunctionString)
{
	PVRSRV_ERROR           eError;
	RGX_KM_HWPERF_DEVDATA* psDevData;
	RGX_HWPERF_DEVICE*     psHWPerfDev;

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_IMPLEMENTED);

	if (!psHWPerfConnection || ui32NumBlocks==0 || !aeBlockIDs)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (ui32NumBlocks > RGXFWIF_HWPERF_CTRL_BLKS_MAX)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psHWPerfDev = psHWPerfConnection->psHWPerfDevList;

	while (psHWPerfDev)
	{
		psDevData = (RGX_KM_HWPERF_DEVDATA *) psHWPerfDev->hDevData;

		/* Call the internal server API */
		eError = PVRSRVRGXControlHWPerfBlocksKM(NULL,
		                                        psDevData->psRgxDevNode,
		                                        bToggle,
		                                        ui32NumBlocks,
		                                        aeBlockIDs);

		PVR_LOG_RETURN_IF_ERROR(eError, szFunctionString);

		psHWPerfDev = psHWPerfDev->psNext;
	}
	return PVRSRV_OK;
}

PVRSRV_ERROR RGXHWPerfDisableCounters(
		RGX_HWPERF_CONNECTION *psHWPerfConnection,
		IMG_UINT32            ui32NumBlocks,
		IMG_UINT16*           aeBlockIDs)
{
	return RGXHWPerfToggleCounters(psHWPerfConnection,
			                        ui32NumBlocks,
			                        aeBlockIDs,
			                        IMG_FALSE,
			                        __func__);
}


PVRSRV_ERROR RGXHWPerfEnableCounters(
		RGX_HWPERF_CONNECTION *psHWPerfConnection,
		IMG_UINT32            ui32NumBlocks,
		IMG_UINT16*           aeBlockIDs)
{
	return RGXHWPerfToggleCounters(psHWPerfConnection,
			                        ui32NumBlocks,
			                        aeBlockIDs,
			                        IMG_TRUE,
			                        __func__);
}


PVRSRV_ERROR RGXHWPerfAcquireEvents(
		IMG_HANDLE  hDevData,
		RGX_HWPERF_STREAM_ID eStreamId,
		IMG_PBYTE*  ppBuf,
		IMG_UINT32* pui32BufLen)
{
	PVRSRV_ERROR			eError;
	RGX_KM_HWPERF_DEVDATA*	psDevData = (RGX_KM_HWPERF_DEVDATA*)hDevData;
	IMG_PBYTE				pDataDest;
	IMG_UINT32			ui32TlPackets = 0;
	IMG_PBYTE			pBufferEnd;
	PVRSRVTL_PPACKETHDR psHDRptr;
	PVRSRVTL_PACKETTYPE ui16TlType;

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_IMPLEMENTED);

	/* Reset the output arguments in case we discover an error */
	*ppBuf = NULL;
	*pui32BufLen = 0;

	/* Valid input argument values supplied by the caller */
	if (!psDevData || eStreamId >= RGX_HWPERF_MAX_STREAM_ID)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (psDevData->pTlBuf[eStreamId] == NULL)
	{
		/* Acquire some data to read from the HWPerf TL stream */
		eError = TLClientAcquireData(DIRECT_BRIDGE_HANDLE,
		                             psDevData->hSD[eStreamId],
		                             &psDevData->pTlBuf[eStreamId],
		                             &psDevData->ui32AcqDataLen[eStreamId]);
		PVR_LOG_RETURN_IF_ERROR(eError, "TLClientAcquireData");

		psDevData->pTlBufPos[eStreamId] = psDevData->pTlBuf[eStreamId];
	}

	/* TL indicates no data exists so return OK and zero. */
	if ((psDevData->pTlBufPos[eStreamId] == NULL) || (psDevData->ui32AcqDataLen[eStreamId] == 0))
	{
		return PVRSRV_OK;
	}

	/* Process each TL packet in the data buffer we have acquired */
	pBufferEnd = psDevData->pTlBuf[eStreamId]+psDevData->ui32AcqDataLen[eStreamId];
	pDataDest = psDevData->pHwpBuf[eStreamId];
	psHDRptr = GET_PACKET_HDR(psDevData->pTlBufPos[eStreamId]);
	psDevData->pTlBufRead[eStreamId] = psDevData->pTlBufPos[eStreamId];
	while (psHDRptr < (PVRSRVTL_PPACKETHDR)((void *)pBufferEnd))
	{
		ui16TlType = GET_PACKET_TYPE(psHDRptr);
		if (ui16TlType == PVRSRVTL_PACKETTYPE_DATA)
		{
			IMG_UINT16 ui16DataLen = GET_PACKET_DATA_LEN(psHDRptr);
			if (0 == ui16DataLen)
			{
				PVR_DPF((PVR_DBG_ERROR, "RGXHWPerfAcquireEvents: ZERO Data in TL data packet: %p", psHDRptr));
			}
			else
			{
				/* Check next packet does not fill buffer */
				if (pDataDest + ui16DataLen > psDevData->pHwpBufEnd[eStreamId])
				{
					break;
				}

				/* For valid data copy it into the client buffer and move
				 * the write position on */
				OSDeviceMemCopy(pDataDest, GET_PACKET_DATA_PTR(psHDRptr), ui16DataLen);
				pDataDest += ui16DataLen;
			}
		}
		else if (ui16TlType == PVRSRVTL_PACKETTYPE_MOST_RECENT_WRITE_FAILED)
		{
			PVR_DPF((PVR_DBG_MESSAGE, "RGXHWPerfAcquireEvents: Indication that the transport buffer was full"));
		}
		else
		{
			/* else Ignore padding packet type and others */
			PVR_DPF((PVR_DBG_MESSAGE, "RGXHWPerfAcquireEvents: Ignoring TL packet, type %d", ui16TlType ));
		}

		/* Update loop variable to the next packet and increment counts */
		psHDRptr = GET_NEXT_PACKET_ADDR(psHDRptr);
		/* Updated to keep track of the next packet to be read. */
		psDevData->pTlBufRead[eStreamId] = (IMG_PBYTE) ((void *)psHDRptr);
		ui32TlPackets++;
	}

	PVR_DPF((PVR_DBG_VERBOSE, "RGXHWPerfAcquireEvents: TL Packets processed %03d", ui32TlPackets));

	psDevData->bRelease[eStreamId] = IMG_FALSE;
	if (psHDRptr >= (PVRSRVTL_PPACKETHDR)((void *)pBufferEnd))
	{
		psDevData->bRelease[eStreamId] = IMG_TRUE;
	}

	/* Update output arguments with client buffer details and true length */
	*ppBuf = psDevData->pHwpBuf[eStreamId];
	*pui32BufLen = pDataDest - psDevData->pHwpBuf[eStreamId];

	return PVRSRV_OK;
}


PVRSRV_ERROR RGXHWPerfReleaseEvents(
		IMG_HANDLE hDevData,
		RGX_HWPERF_STREAM_ID eStreamId)
{
	PVRSRV_ERROR			eError = PVRSRV_OK;
	RGX_KM_HWPERF_DEVDATA*	psDevData = (RGX_KM_HWPERF_DEVDATA*)hDevData;

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_IMPLEMENTED);

	/* Valid input argument values supplied by the caller */
	if (!psDevData || eStreamId >= RGX_HWPERF_MAX_STREAM_ID)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (psDevData->bRelease[eStreamId])
	{
		/* Inform the TL that we are done with reading the data. */
		eError = TLClientReleaseData(DIRECT_BRIDGE_HANDLE, psDevData->hSD[eStreamId]);
		psDevData->ui32AcqDataLen[eStreamId] = 0;
		psDevData->pTlBuf[eStreamId] = NULL;
	}
	else
	{
		psDevData->pTlBufPos[eStreamId] = psDevData->pTlBufRead[eStreamId];
	}
	return eError;
}


PVRSRV_ERROR RGXHWPerfGetFilter(
		IMG_HANDLE  hDevData,
		RGX_HWPERF_STREAM_ID eStreamId,
		IMG_UINT64 *ui64Filter)
{
	PVRSRV_RGXDEV_INFO* psRgxDevInfo =
			hDevData ? ((RGX_KM_HWPERF_DEVDATA*) hDevData)->psRgxDevInfo : NULL;

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_IMPLEMENTED);

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


PVRSRV_ERROR RGXHWPerfFreeConnection(RGX_HWPERF_CONNECTION** ppsHWPerfConnection)
{
	RGX_HWPERF_DEVICE *psHWPerfDev, *psHWPerfNextDev;
	RGX_HWPERF_CONNECTION *psHWPerfConnection = *ppsHWPerfConnection;

	/* if connection object itself is NULL, nothing to free */
	if (psHWPerfConnection == NULL)
	{
		return PVRSRV_OK;
	}

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_IMPLEMENTED);

	psHWPerfNextDev = psHWPerfConnection->psHWPerfDevList;
	while (psHWPerfNextDev)
	{
		psHWPerfDev = psHWPerfNextDev;
		psHWPerfNextDev = psHWPerfNextDev->psNext;

		/* Free the session memory */
		if (psHWPerfDev->hDevData)
			OSFreeMem(psHWPerfDev->hDevData);
		OSFreeMem(psHWPerfDev);
	}
	OSFreeMem(psHWPerfConnection);
	*ppsHWPerfConnection = NULL;

	return PVRSRV_OK;
}


PVRSRV_ERROR RGXHWPerfClose(RGX_HWPERF_CONNECTION *psHWPerfConnection)
{
	RGX_HWPERF_DEVICE *psHWPerfDev;
	RGX_KM_HWPERF_DEVDATA* psDevData;
	IMG_UINT uiStreamId;
	PVRSRV_ERROR eError;

	/* Check session connection is not zero */
	if (!psHWPerfConnection)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_IMPLEMENTED);

	psHWPerfDev = psHWPerfConnection->psHWPerfDevList;
	while (psHWPerfDev)
	{
		psDevData = (RGX_KM_HWPERF_DEVDATA *) psHWPerfDev->hDevData;
		for (uiStreamId = 0; uiStreamId < RGX_HWPERF_MAX_STREAM_ID; uiStreamId++)
		{
			/* If the TL buffer exists they have not called ReleaseData
			 * before disconnecting so clean it up */
			if (psDevData->pTlBuf[uiStreamId])
			{
				/* TLClientReleaseData call and null out the buffer fields
				 * and length */
				eError = TLClientReleaseData(DIRECT_BRIDGE_HANDLE, psDevData->hSD[uiStreamId]);
				psDevData->ui32AcqDataLen[uiStreamId] = 0;
				psDevData->pTlBuf[uiStreamId] = NULL;
				PVR_LOG_IF_ERROR(eError, "TLClientReleaseData");
				/* Packets may be lost if release was not required */
				if (!psDevData->bRelease[uiStreamId])
				{
					PVR_DPF((PVR_DBG_WARNING, "RGXHWPerfClose: Events in buffer waiting to be read, remaining events may be lost."));
				}
			}

			/* Close the TL stream, ignore the error if it occurs as we
			 * are disconnecting */
			if (psDevData->hSD[uiStreamId])
			{
				eError = TLClientCloseStream(DIRECT_BRIDGE_HANDLE,
				                             psDevData->hSD[uiStreamId]);
				PVR_LOG_IF_ERROR(eError, "TLClientCloseStream");
				psDevData->hSD[uiStreamId] = NULL;
			}

			/* Free the client buffer used in session */
			if (psDevData->pHwpBuf[uiStreamId])
			{
				OSFreeMem(psDevData->pHwpBuf[uiStreamId]);
				psDevData->pHwpBuf[uiStreamId] = NULL;
			}
		}
		psHWPerfDev = psHWPerfDev->psNext;
	}

	return PVRSRV_OK;
}


PVRSRV_ERROR RGXHWPerfDisconnect(RGX_HWPERF_CONNECTION** ppsHWPerfConnection)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_IMPLEMENTED);

	eError = RGXHWPerfClose(*ppsHWPerfConnection);
	PVR_LOG_IF_ERROR(eError, "RGXHWPerfClose");

	eError = RGXHWPerfFreeConnection(ppsHWPerfConnection);
	PVR_LOG_IF_ERROR(eError, "RGXHWPerfFreeConnection");

	return eError;
}

IMG_UINT64 RGXHWPerfConvertCRTimeStamp(
		IMG_UINT32 ui32ClkSpeed,
		IMG_UINT64 ui64CorrCRTimeStamp,
		IMG_UINT64 ui64CorrOSTimeStamp,
		IMG_UINT64 ui64CRTimeStamp)
{
	IMG_UINT64 ui64CRDeltaToOSDeltaKNs;
	IMG_UINT64 ui64EventOSTimestamp, deltaRgxTimer, delta_ns;

	if (!(ui64CRTimeStamp) || !(ui32ClkSpeed) || !(ui64CorrCRTimeStamp) || !(ui64CorrOSTimeStamp))
	{
		return 0;
	}

	ui64CRDeltaToOSDeltaKNs = RGXTimeCorrGetConversionFactor(ui32ClkSpeed);

	/* RGX CR timer ticks delta */
	deltaRgxTimer = ui64CRTimeStamp - ui64CorrCRTimeStamp;
	/* RGX time delta in nanoseconds */
	delta_ns = RGXFWIF_GET_DELTA_OSTIME_NS(deltaRgxTimer, ui64CRDeltaToOSDeltaKNs);
	/* Calculate OS time of HWPerf event */
	ui64EventOSTimestamp = ui64CorrOSTimeStamp + delta_ns;

	return ui64EventOSTimestamp;
}

/******************************************************************************
 End of file (rgxhwperf_common.c)
 ******************************************************************************/
