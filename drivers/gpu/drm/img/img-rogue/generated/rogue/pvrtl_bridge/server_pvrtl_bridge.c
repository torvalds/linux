/*******************************************************************************
@File
@Title          Server bridge for pvrtl
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for pvrtl
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
*******************************************************************************/

#include <linux/uaccess.h>

#include "img_defs.h"

#include "tlserver.h"

#include "common_pvrtl_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#if defined(SUPPORT_RGX)
#include "rgx_bridge.h"
#endif
#include "srvcore.h"
#include "handle.h"

#include <linux/slab.h>

/* ***************************************************************************
 * Server-side bridge entry points
 */

static PVRSRV_ERROR _TLOpenStreampsSDIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = TLServerCloseStreamKM((TL_STREAM_DESC *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgeTLOpenStream(IMG_UINT32 ui32DispatchTableEntry,
			 IMG_UINT8 * psTLOpenStreamIN_UI8,
			 IMG_UINT8 * psTLOpenStreamOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_TLOPENSTREAM *psTLOpenStreamIN =
	    (PVRSRV_BRIDGE_IN_TLOPENSTREAM *) IMG_OFFSET_ADDR(psTLOpenStreamIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_TLOPENSTREAM *psTLOpenStreamOUT =
	    (PVRSRV_BRIDGE_OUT_TLOPENSTREAM *) IMG_OFFSET_ADDR(psTLOpenStreamOUT_UI8, 0);

	IMG_CHAR *uiNameInt = NULL;
	TL_STREAM_DESC *psSDInt = NULL;
	PMR *psTLPMRInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = (PRVSRVTL_MAX_STREAM_NAME_SIZE * sizeof(IMG_CHAR)) + 0;

	psTLOpenStreamOUT->hSD = NULL;

	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psTLOpenStreamIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psTLOpenStreamIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocZMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psTLOpenStreamOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto TLOpenStream_exit;
			}
		}
	}

	{
		uiNameInt = (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += PRVSRVTL_MAX_STREAM_NAME_SIZE * sizeof(IMG_CHAR);
	}

	/* Copy the data over */
	if (PRVSRVTL_MAX_STREAM_NAME_SIZE * sizeof(IMG_CHAR) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiNameInt, (const void __user *)psTLOpenStreamIN->puiName,
		     PRVSRVTL_MAX_STREAM_NAME_SIZE * sizeof(IMG_CHAR)) != PVRSRV_OK)
		{
			psTLOpenStreamOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto TLOpenStream_exit;
		}
		((IMG_CHAR *) uiNameInt)[(PRVSRVTL_MAX_STREAM_NAME_SIZE * sizeof(IMG_CHAR)) - 1] =
		    '\0';
	}

	psTLOpenStreamOUT->eError =
	    TLServerOpenStreamKM(uiNameInt, psTLOpenStreamIN->ui32Mode, &psSDInt, &psTLPMRInt);
	/* Exit early if bridged call fails */
	if (unlikely(psTLOpenStreamOUT->eError != PVRSRV_OK))
	{
		goto TLOpenStream_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psTLOpenStreamOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
							      &psTLOpenStreamOUT->hSD,
							      (void *)psSDInt,
							      PVRSRV_HANDLE_TYPE_PVR_TL_SD,
							      PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
							      (PFN_HANDLE_RELEASE) &
							      _TLOpenStreampsSDIntRelease);
	if (unlikely(psTLOpenStreamOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto TLOpenStream_exit;
	}

	psTLOpenStreamOUT->eError = PVRSRVAllocSubHandleUnlocked(psConnection->psHandleBase,
								 &psTLOpenStreamOUT->hTLPMR,
								 (void *)psTLPMRInt,
								 PVRSRV_HANDLE_TYPE_PMR_LOCAL_EXPORT_HANDLE,
								 PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
								 psTLOpenStreamOUT->hSD);
	if (unlikely(psTLOpenStreamOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto TLOpenStream_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

TLOpenStream_exit:

	if (psTLOpenStreamOUT->eError != PVRSRV_OK)
	{
		if (psTLOpenStreamOUT->hSD)
		{
			PVRSRV_ERROR eError;

			/* Lock over handle creation cleanup. */
			LockHandle(psConnection->psHandleBase);

			eError = PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
							     (IMG_HANDLE) psTLOpenStreamOUT->hSD,
							     PVRSRV_HANDLE_TYPE_PVR_TL_SD);
			if (unlikely((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_RETRY)))
			{
				PVR_DPF((PVR_DBG_ERROR,
					 "%s: %s", __func__, PVRSRVGetErrorString(eError)));
			}
			/* Releasing the handle should free/destroy/release the resource.
			 * This should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

			/* Avoid freeing/destroying/releasing the resource a second time below */
			psSDInt = NULL;
			/* Release now we have cleaned up creation handles. */
			UnlockHandle(psConnection->psHandleBase);

		}

		if (psSDInt)
		{
			TLServerCloseStreamKM(psSDInt);
		}
	}

	/* Allocated space should be equal to the last updated offset */
	PVR_ASSERT(ui32BufferSize == ui32NextOffset);

#if defined(INTEGRITY_OS)
	if (pArrayArgsBuffer)
#else
	if (!bHaveEnoughSpace && pArrayArgsBuffer)
#endif
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static IMG_INT
PVRSRVBridgeTLCloseStream(IMG_UINT32 ui32DispatchTableEntry,
			  IMG_UINT8 * psTLCloseStreamIN_UI8,
			  IMG_UINT8 * psTLCloseStreamOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_TLCLOSESTREAM *psTLCloseStreamIN =
	    (PVRSRV_BRIDGE_IN_TLCLOSESTREAM *) IMG_OFFSET_ADDR(psTLCloseStreamIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_TLCLOSESTREAM *psTLCloseStreamOUT =
	    (PVRSRV_BRIDGE_OUT_TLCLOSESTREAM *) IMG_OFFSET_ADDR(psTLCloseStreamOUT_UI8, 0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psTLCloseStreamOUT->eError =
	    PVRSRVReleaseHandleStagedUnlock(psConnection->psHandleBase,
					    (IMG_HANDLE) psTLCloseStreamIN->hSD,
					    PVRSRV_HANDLE_TYPE_PVR_TL_SD);
	if (unlikely((psTLCloseStreamOUT->eError != PVRSRV_OK) &&
		     (psTLCloseStreamOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s", __func__, PVRSRVGetErrorString(psTLCloseStreamOUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto TLCloseStream_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

TLCloseStream_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeTLAcquireData(IMG_UINT32 ui32DispatchTableEntry,
			  IMG_UINT8 * psTLAcquireDataIN_UI8,
			  IMG_UINT8 * psTLAcquireDataOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_TLACQUIREDATA *psTLAcquireDataIN =
	    (PVRSRV_BRIDGE_IN_TLACQUIREDATA *) IMG_OFFSET_ADDR(psTLAcquireDataIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_TLACQUIREDATA *psTLAcquireDataOUT =
	    (PVRSRV_BRIDGE_OUT_TLACQUIREDATA *) IMG_OFFSET_ADDR(psTLAcquireDataOUT_UI8, 0);

	IMG_HANDLE hSD = psTLAcquireDataIN->hSD;
	TL_STREAM_DESC *psSDInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psTLAcquireDataOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psSDInt,
				       hSD, PVRSRV_HANDLE_TYPE_PVR_TL_SD, IMG_TRUE);
	if (unlikely(psTLAcquireDataOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto TLAcquireData_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psTLAcquireDataOUT->eError =
	    TLServerAcquireDataKM(psSDInt,
				  &psTLAcquireDataOUT->ui32ReadOffset,
				  &psTLAcquireDataOUT->ui32ReadLen);

TLAcquireData_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psSDInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hSD, PVRSRV_HANDLE_TYPE_PVR_TL_SD);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static IMG_INT
PVRSRVBridgeTLReleaseData(IMG_UINT32 ui32DispatchTableEntry,
			  IMG_UINT8 * psTLReleaseDataIN_UI8,
			  IMG_UINT8 * psTLReleaseDataOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_TLRELEASEDATA *psTLReleaseDataIN =
	    (PVRSRV_BRIDGE_IN_TLRELEASEDATA *) IMG_OFFSET_ADDR(psTLReleaseDataIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_TLRELEASEDATA *psTLReleaseDataOUT =
	    (PVRSRV_BRIDGE_OUT_TLRELEASEDATA *) IMG_OFFSET_ADDR(psTLReleaseDataOUT_UI8, 0);

	IMG_HANDLE hSD = psTLReleaseDataIN->hSD;
	TL_STREAM_DESC *psSDInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psTLReleaseDataOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psSDInt,
				       hSD, PVRSRV_HANDLE_TYPE_PVR_TL_SD, IMG_TRUE);
	if (unlikely(psTLReleaseDataOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto TLReleaseData_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psTLReleaseDataOUT->eError =
	    TLServerReleaseDataKM(psSDInt,
				  psTLReleaseDataIN->ui32ReadOffset,
				  psTLReleaseDataIN->ui32ReadLen);

TLReleaseData_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psSDInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hSD, PVRSRV_HANDLE_TYPE_PVR_TL_SD);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static IMG_INT
PVRSRVBridgeTLDiscoverStreams(IMG_UINT32 ui32DispatchTableEntry,
			      IMG_UINT8 * psTLDiscoverStreamsIN_UI8,
			      IMG_UINT8 * psTLDiscoverStreamsOUT_UI8,
			      CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_TLDISCOVERSTREAMS *psTLDiscoverStreamsIN =
	    (PVRSRV_BRIDGE_IN_TLDISCOVERSTREAMS *) IMG_OFFSET_ADDR(psTLDiscoverStreamsIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_TLDISCOVERSTREAMS *psTLDiscoverStreamsOUT =
	    (PVRSRV_BRIDGE_OUT_TLDISCOVERSTREAMS *) IMG_OFFSET_ADDR(psTLDiscoverStreamsOUT_UI8, 0);

	IMG_CHAR *uiNamePatternInt = NULL;
	IMG_CHAR *puiStreamsInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize =
	    (PRVSRVTL_MAX_STREAM_NAME_SIZE * sizeof(IMG_CHAR)) +
	    (psTLDiscoverStreamsIN->ui32Size * sizeof(IMG_CHAR)) + 0;

	if (psTLDiscoverStreamsIN->ui32Size > PVRSRVTL_MAX_DISCOVERABLE_STREAMS_BUFFER)
	{
		psTLDiscoverStreamsOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto TLDiscoverStreams_exit;
	}

	PVR_UNREFERENCED_PARAMETER(psConnection);

	psTLDiscoverStreamsOUT->puiStreams = psTLDiscoverStreamsIN->puiStreams;

	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psTLDiscoverStreamsIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psTLDiscoverStreamsIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocZMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psTLDiscoverStreamsOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto TLDiscoverStreams_exit;
			}
		}
	}

	{
		uiNamePatternInt = (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += PRVSRVTL_MAX_STREAM_NAME_SIZE * sizeof(IMG_CHAR);
	}

	/* Copy the data over */
	if (PRVSRVTL_MAX_STREAM_NAME_SIZE * sizeof(IMG_CHAR) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiNamePatternInt,
		     (const void __user *)psTLDiscoverStreamsIN->puiNamePattern,
		     PRVSRVTL_MAX_STREAM_NAME_SIZE * sizeof(IMG_CHAR)) != PVRSRV_OK)
		{
			psTLDiscoverStreamsOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto TLDiscoverStreams_exit;
		}
		((IMG_CHAR *) uiNamePatternInt)[(PRVSRVTL_MAX_STREAM_NAME_SIZE * sizeof(IMG_CHAR)) -
						1] = '\0';
	}
	if (psTLDiscoverStreamsIN->ui32Size != 0)
	{
		puiStreamsInt = (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psTLDiscoverStreamsIN->ui32Size * sizeof(IMG_CHAR);
	}

	psTLDiscoverStreamsOUT->eError =
	    TLServerDiscoverStreamsKM(uiNamePatternInt,
				      psTLDiscoverStreamsIN->ui32Size,
				      puiStreamsInt, &psTLDiscoverStreamsOUT->ui32NumFound);

	/* If dest ptr is non-null and we have data to copy */
	if ((puiStreamsInt) && ((psTLDiscoverStreamsIN->ui32Size * sizeof(IMG_CHAR)) > 0))
	{
		if (unlikely
		    (OSCopyToUser
		     (NULL, (void __user *)psTLDiscoverStreamsOUT->puiStreams, puiStreamsInt,
		      (psTLDiscoverStreamsIN->ui32Size * sizeof(IMG_CHAR))) != PVRSRV_OK))
		{
			psTLDiscoverStreamsOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto TLDiscoverStreams_exit;
		}
	}

TLDiscoverStreams_exit:

	/* Allocated space should be equal to the last updated offset */
	PVR_ASSERT(ui32BufferSize == ui32NextOffset);

#if defined(INTEGRITY_OS)
	if (pArrayArgsBuffer)
#else
	if (!bHaveEnoughSpace && pArrayArgsBuffer)
#endif
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static IMG_INT
PVRSRVBridgeTLReserveStream(IMG_UINT32 ui32DispatchTableEntry,
			    IMG_UINT8 * psTLReserveStreamIN_UI8,
			    IMG_UINT8 * psTLReserveStreamOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_TLRESERVESTREAM *psTLReserveStreamIN =
	    (PVRSRV_BRIDGE_IN_TLRESERVESTREAM *) IMG_OFFSET_ADDR(psTLReserveStreamIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_TLRESERVESTREAM *psTLReserveStreamOUT =
	    (PVRSRV_BRIDGE_OUT_TLRESERVESTREAM *) IMG_OFFSET_ADDR(psTLReserveStreamOUT_UI8, 0);

	IMG_HANDLE hSD = psTLReserveStreamIN->hSD;
	TL_STREAM_DESC *psSDInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psTLReserveStreamOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psSDInt,
				       hSD, PVRSRV_HANDLE_TYPE_PVR_TL_SD, IMG_TRUE);
	if (unlikely(psTLReserveStreamOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto TLReserveStream_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psTLReserveStreamOUT->eError =
	    TLServerReserveStreamKM(psSDInt,
				    &psTLReserveStreamOUT->ui32BufferOffset,
				    psTLReserveStreamIN->ui32Size,
				    psTLReserveStreamIN->ui32SizeMin,
				    &psTLReserveStreamOUT->ui32Available);

TLReserveStream_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psSDInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hSD, PVRSRV_HANDLE_TYPE_PVR_TL_SD);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static IMG_INT
PVRSRVBridgeTLCommitStream(IMG_UINT32 ui32DispatchTableEntry,
			   IMG_UINT8 * psTLCommitStreamIN_UI8,
			   IMG_UINT8 * psTLCommitStreamOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_TLCOMMITSTREAM *psTLCommitStreamIN =
	    (PVRSRV_BRIDGE_IN_TLCOMMITSTREAM *) IMG_OFFSET_ADDR(psTLCommitStreamIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_TLCOMMITSTREAM *psTLCommitStreamOUT =
	    (PVRSRV_BRIDGE_OUT_TLCOMMITSTREAM *) IMG_OFFSET_ADDR(psTLCommitStreamOUT_UI8, 0);

	IMG_HANDLE hSD = psTLCommitStreamIN->hSD;
	TL_STREAM_DESC *psSDInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psTLCommitStreamOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psSDInt,
				       hSD, PVRSRV_HANDLE_TYPE_PVR_TL_SD, IMG_TRUE);
	if (unlikely(psTLCommitStreamOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto TLCommitStream_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psTLCommitStreamOUT->eError =
	    TLServerCommitStreamKM(psSDInt, psTLCommitStreamIN->ui32ReqSize);

TLCommitStream_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psSDInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hSD, PVRSRV_HANDLE_TYPE_PVR_TL_SD);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static IMG_INT
PVRSRVBridgeTLWriteData(IMG_UINT32 ui32DispatchTableEntry,
			IMG_UINT8 * psTLWriteDataIN_UI8,
			IMG_UINT8 * psTLWriteDataOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_TLWRITEDATA *psTLWriteDataIN =
	    (PVRSRV_BRIDGE_IN_TLWRITEDATA *) IMG_OFFSET_ADDR(psTLWriteDataIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_TLWRITEDATA *psTLWriteDataOUT =
	    (PVRSRV_BRIDGE_OUT_TLWRITEDATA *) IMG_OFFSET_ADDR(psTLWriteDataOUT_UI8, 0);

	IMG_HANDLE hSD = psTLWriteDataIN->hSD;
	TL_STREAM_DESC *psSDInt = NULL;
	IMG_BYTE *ui8DataInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = (psTLWriteDataIN->ui32Size * sizeof(IMG_BYTE)) + 0;

	if (unlikely(psTLWriteDataIN->ui32Size > PVRSRVTL_MAX_PACKET_SIZE))
	{
		psTLWriteDataOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto TLWriteData_exit;
	}

	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psTLWriteDataIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psTLWriteDataIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocZMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psTLWriteDataOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto TLWriteData_exit;
			}
		}
	}

	if (psTLWriteDataIN->ui32Size != 0)
	{
		ui8DataInt = (IMG_BYTE *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psTLWriteDataIN->ui32Size * sizeof(IMG_BYTE);
	}

	/* Copy the data over */
	if (psTLWriteDataIN->ui32Size * sizeof(IMG_BYTE) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui8DataInt, (const void __user *)psTLWriteDataIN->pui8Data,
		     psTLWriteDataIN->ui32Size * sizeof(IMG_BYTE)) != PVRSRV_OK)
		{
			psTLWriteDataOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto TLWriteData_exit;
		}
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psTLWriteDataOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psSDInt,
				       hSD, PVRSRV_HANDLE_TYPE_PVR_TL_SD, IMG_TRUE);
	if (unlikely(psTLWriteDataOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto TLWriteData_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psTLWriteDataOUT->eError =
	    TLServerWriteDataKM(psSDInt, psTLWriteDataIN->ui32Size, ui8DataInt);

TLWriteData_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psSDInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hSD, PVRSRV_HANDLE_TYPE_PVR_TL_SD);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	/* Allocated space should be equal to the last updated offset */
	PVR_ASSERT(ui32BufferSize == ui32NextOffset);

#if defined(INTEGRITY_OS)
	if (pArrayArgsBuffer)
#else
	if (!bHaveEnoughSpace && pArrayArgsBuffer)
#endif
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

PVRSRV_ERROR InitPVRTLBridge(void);
PVRSRV_ERROR DeinitPVRTLBridge(void);

/*
 * Register all PVRTL functions with services
 */
PVRSRV_ERROR InitPVRTLBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLOPENSTREAM,
			      PVRSRVBridgeTLOpenStream, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLCLOSESTREAM,
			      PVRSRVBridgeTLCloseStream, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLACQUIREDATA,
			      PVRSRVBridgeTLAcquireData, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLRELEASEDATA,
			      PVRSRVBridgeTLReleaseData, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLDISCOVERSTREAMS,
			      PVRSRVBridgeTLDiscoverStreams, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLRESERVESTREAM,
			      PVRSRVBridgeTLReserveStream, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLCOMMITSTREAM,
			      PVRSRVBridgeTLCommitStream, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLWRITEDATA,
			      PVRSRVBridgeTLWriteData, NULL);

	return PVRSRV_OK;
}

/*
 * Unregister all pvrtl functions with services
 */
PVRSRV_ERROR DeinitPVRTLBridge(void)
{

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLOPENSTREAM);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLCLOSESTREAM);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLACQUIREDATA);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLRELEASEDATA);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLDISCOVERSTREAMS);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLRESERVESTREAM);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLCOMMITSTREAM);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLWRITEDATA);

	return PVRSRV_OK;
}
