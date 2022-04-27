/*************************************************************************/ /*!
@File           tlclient.c
@Title          Services Transport Layer shared API
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Transport layer common API used in both clients and server
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

/* DESIGN NOTE
 * This transport layer consumer-role API was created as a shared API when a
 * client wanted to read the data of a TL stream from within the KM server
 * driver. This was in addition to the existing clients supported externally
 * by the UM client library component via PVR API layer.
 * This shared API is thus used by the PVR TL API in the client library and
 * by clients internal to the server driver module. It depends on
 * client entry points of the TL and DEVMEM bridge modules. These entry points
 * encapsulate from the TL shared API whether a direct bridge or an indirect
 * (ioctl) bridge is used.
 * One reason for needing this layer centres around the fact that some of the
 * API functions make multiple bridge calls and the logic that glues these
 * together is common regardless of client location. Further this layer has
 * allowed the defensive coding that checks parameters to move into the PVR
 * API layer where untrusted clients enter giving a more efficient KM code path.
 */

#include "img_defs.h"
#include "pvrsrv_error.h"
#include "pvr_debug.h"
#include "osfunc.h"

#include "allocmem.h"
#include "devicemem.h"

#include "tlclient.h"
#include "pvrsrv_tlcommon.h"
#include "client_pvrtl_bridge.h"

/* Defines/Constants
 */

#define NO_ACQUIRE             0xffffffffU

/* User-side stream descriptor structure.
 */
typedef struct _TL_STREAM_DESC_
{
	/* Handle on kernel-side stream descriptor*/
	IMG_HANDLE		hServerSD;

	/* Stream data buffer variables */
	DEVMEM_MEMDESC*			psUMmemDesc;
	IMG_PBYTE				pBaseAddr;

	/* Offset in bytes into the circular buffer and valid only after
	 * an Acquire call and undefined after a release. */
	IMG_UINT32	uiReadOffset;

	/* Always a positive integer when the Acquire call returns and a release
	 * is outstanding. Undefined at all other times. */
	IMG_UINT32	uiReadLen;

	/* Flag indicating if the RESERVE_TOO_BIG error was already printed.
	 * It's used to reduce number of errors in kernel log. */
	IMG_BOOL bPrinted;
} TL_STREAM_DESC, *PTL_STREAM_DESC;


IMG_INTERNAL
PVRSRV_ERROR TLClientOpenStream(SHARED_DEV_CONNECTION hDevConnection,
		const IMG_CHAR* pszName,
		IMG_UINT32   ui32Mode,
		IMG_HANDLE*  phSD)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	TL_STREAM_DESC *psSD = NULL;
	IMG_HANDLE hTLPMR;
	IMG_HANDLE hTLImportHandle;
	IMG_DEVMEM_SIZE_T uiImportSize;
	PVRSRV_MEMALLOCFLAGS_T uiMemFlags = PVRSRV_MEMALLOCFLAG_CPU_READABLE;

	PVR_ASSERT(hDevConnection);
	PVR_ASSERT(pszName);
	PVR_ASSERT(phSD);
	*phSD = NULL;

	/* Allocate memory for the stream descriptor object, initialise with
	 * "no data read" yet. */
	psSD = OSAllocZMem(sizeof(TL_STREAM_DESC));
	PVR_LOG_GOTO_IF_NOMEM(psSD, eError, e0);
	psSD->uiReadLen = psSD->uiReadOffset = NO_ACQUIRE;

	/* Send open stream request to kernel server to get stream handle and
	 * buffer cookie so we can get access to the buffer in this process. */
	eError = BridgeTLOpenStream(GetBridgeHandle(hDevConnection), pszName,
			ui32Mode, &psSD->hServerSD, &hTLPMR);
	if (eError != PVRSRV_OK)
	{
		if ((ui32Mode & PVRSRV_STREAM_FLAG_OPEN_WAIT) &&
			(eError == PVRSRV_ERROR_TIMEOUT))
		{
			goto e1;
		}
		PVR_LOG_GOTO_IF_ERROR(eError, "BridgeTLOpenStream", e1);
	}

	/* Convert server export cookie into a cookie for use by this client */
	eError = DevmemMakeLocalImportHandle(hDevConnection,
			hTLPMR, &hTLImportHandle);
	PVR_LOG_GOTO_IF_ERROR(eError, "DevmemMakeLocalImportHandle", e2);

	uiMemFlags |= ui32Mode & PVRSRV_STREAM_FLAG_OPEN_WO ?
	        PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE : 0ULL;
	/* Now convert client cookie into a client handle on the buffer's
	 * physical memory region */
	eError = DevmemLocalImport(hDevConnection,
	                           hTLImportHandle,
	                           uiMemFlags,
	                           &psSD->psUMmemDesc,
	                           &uiImportSize,
	                           "TLBuffer");
	PVR_LOG_GOTO_IF_ERROR(eError, "DevmemImport", e3);

	/* Now map the memory into the virtual address space of this process. */
	eError = DevmemAcquireCpuVirtAddr(psSD->psUMmemDesc, (void **)
															&psSD->pBaseAddr);
	PVR_LOG_GOTO_IF_ERROR(eError, "DevmemAcquireCpuVirtAddr", e4);

	/* Ignore error, not much that can be done */
	(void) DevmemUnmakeLocalImportHandle(hDevConnection,
			hTLImportHandle);

	/* Return client descriptor handle to caller */
	*phSD = psSD;
	return PVRSRV_OK;

/* Clean up post buffer setup */
e4:
	DevmemFree(psSD->psUMmemDesc);
e3:
	(void) DevmemUnmakeLocalImportHandle(hDevConnection,
				&hTLImportHandle);
/* Clean up post stream open */
e2:
	BridgeTLCloseStream(GetBridgeHandle(hDevConnection), psSD->hServerSD);

/* Clean up post allocation of the descriptor object */
e1:
	OSFreeMem(psSD);

e0:
	return eError;
}

IMG_INTERNAL
PVRSRV_ERROR TLClientCloseStream(SHARED_DEV_CONNECTION hDevConnection,
		IMG_HANDLE hSD)
{
	PVRSRV_ERROR eError;
	TL_STREAM_DESC* psSD = (TL_STREAM_DESC*) hSD;

	PVR_ASSERT(hDevConnection);
	PVR_ASSERT(hSD);

	/* Check the caller provided connection is valid */
	if (!psSD->hServerSD)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: descriptor already "
				"closed/not open", __func__));
		return PVRSRV_ERROR_HANDLE_NOT_FOUND;
	}

	/* Check if acquire is outstanding, perform release if it is, ignore result
	 * as there is not much we can do if it is an error other than close */
	if (psSD->uiReadLen != NO_ACQUIRE)
	{
		(void) BridgeTLReleaseData(GetBridgeHandle(hDevConnection),
				psSD->hServerSD, psSD->uiReadOffset, psSD->uiReadLen);
		psSD->uiReadLen = psSD->uiReadOffset = NO_ACQUIRE;
	}

	/* Clean up DevMem resources used for this stream in this client */
	DevmemReleaseCpuVirtAddr(psSD->psUMmemDesc);

	DevmemFree(psSD->psUMmemDesc);

	/* Send close to server to clean up kernel mode resources for this
	 * handle and release the memory. */
	eError = BridgeTLCloseStream(GetBridgeHandle(hDevConnection),
			psSD->hServerSD);
	PVR_LOG_IF_ERROR(eError, "BridgeTLCloseStream");

	OSCachedMemSet(psSD, 0x00, sizeof(TL_STREAM_DESC));
	OSFreeMem(psSD);

	return eError;
}

IMG_INTERNAL
PVRSRV_ERROR TLClientDiscoverStreams(SHARED_DEV_CONNECTION hDevConnection,
		const IMG_CHAR *pszNamePattern,
		IMG_CHAR aszStreams[][PRVSRVTL_MAX_STREAM_NAME_SIZE],
		IMG_UINT32 *pui32NumFound)
{
	PVR_ASSERT(hDevConnection);
	PVR_ASSERT(pszNamePattern);
	PVR_ASSERT(pui32NumFound);

	return BridgeTLDiscoverStreams(GetBridgeHandle(hDevConnection),
			pszNamePattern,
			/* we need to treat this as one dimensional array */
			*pui32NumFound * PRVSRVTL_MAX_STREAM_NAME_SIZE,
			(IMG_CHAR *) aszStreams,
			pui32NumFound);
}

IMG_INTERNAL
PVRSRV_ERROR TLClientReserveStream(SHARED_DEV_CONNECTION hDevConnection,
		IMG_HANDLE hSD,
		IMG_UINT8 **ppui8Data,
		IMG_UINT32 ui32Size)
{
	PVRSRV_ERROR eError;
	TL_STREAM_DESC* psSD = (TL_STREAM_DESC*) hSD;
	IMG_UINT32 ui32BufferOffset, ui32Unused;

	PVR_ASSERT(hDevConnection);
	PVR_ASSERT(hSD);
	PVR_ASSERT(ppui8Data);
	PVR_ASSERT(ui32Size);

	eError = BridgeTLReserveStream(GetBridgeHandle(hDevConnection),
			psSD->hServerSD, &ui32BufferOffset, ui32Size, ui32Size, &ui32Unused);
	PVR_RETURN_IF_ERROR(eError);

	*ppui8Data = psSD->pBaseAddr + ui32BufferOffset;

	return PVRSRV_OK;
}

IMG_INTERNAL
PVRSRV_ERROR TLClientReserveStream2(SHARED_DEV_CONNECTION hDevConnection,
		IMG_HANDLE hSD,
		IMG_UINT8 **ppui8Data,
		IMG_UINT32 ui32Size,
		IMG_UINT32 ui32SizeMin,
		IMG_UINT32 *pui32Available)
{
	PVRSRV_ERROR eError;
	TL_STREAM_DESC* psSD = (TL_STREAM_DESC*) hSD;
	IMG_UINT32 ui32BufferOffset;

	PVR_ASSERT(hDevConnection);
	PVR_ASSERT(hSD);
	PVR_ASSERT(ppui8Data);
	PVR_ASSERT(ui32Size);

	eError = BridgeTLReserveStream(GetBridgeHandle(hDevConnection),
			psSD->hServerSD, &ui32BufferOffset, ui32Size, ui32SizeMin,
			pui32Available);
	PVR_RETURN_IF_ERROR(eError);

	*ppui8Data = psSD->pBaseAddr + ui32BufferOffset;

	return PVRSRV_OK;
}

IMG_INTERNAL
PVRSRV_ERROR TLClientCommitStream(SHARED_DEV_CONNECTION hDevConnection,
		IMG_HANDLE hSD,
		IMG_UINT32 ui32Size)
{
	PVRSRV_ERROR eError;
	TL_STREAM_DESC* psSD = (TL_STREAM_DESC*) hSD;

	PVR_ASSERT(hDevConnection);
	PVR_ASSERT(hSD);
	PVR_ASSERT(ui32Size);

	eError = BridgeTLCommitStream(GetBridgeHandle(hDevConnection),
			psSD->hServerSD, ui32Size);
	PVR_RETURN_IF_ERROR(eError);

	return PVRSRV_OK;
}

IMG_INTERNAL
PVRSRV_ERROR TLClientAcquireData(SHARED_DEV_CONNECTION hDevConnection,
		IMG_HANDLE  hSD,
		IMG_PBYTE*  ppPacketBuf,
		IMG_UINT32* pui32BufLen)
{
	PVRSRV_ERROR eError;
	TL_STREAM_DESC* psSD = (TL_STREAM_DESC*) hSD;

	PVR_ASSERT(hDevConnection);
	PVR_ASSERT(hSD);
	PVR_ASSERT(ppPacketBuf);
	PVR_ASSERT(pui32BufLen);

	/* In case of non-blocking acquires, which can return no data, and
	 * error paths ensure we clear the output parameters first. */
	*ppPacketBuf = NULL;
	*pui32BufLen = 0;

	/* Check Acquire has not been called twice in a row without a release */
	if (psSD->uiReadOffset != NO_ACQUIRE)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: acquire already "
				"outstanding, ReadOffset(%d), ReadLength(%d)",
				__func__, psSD->uiReadOffset, psSD->uiReadLen));
		return PVRSRV_ERROR_RETRY;
	}

	/* Ask the kernel server for the next chunk of data to read */
	eError = BridgeTLAcquireData(GetBridgeHandle(hDevConnection),
			psSD->hServerSD, &psSD->uiReadOffset, &psSD->uiReadLen);
	if (eError != PVRSRV_OK)
	{
		/* Mask reporting of the errors seen under normal operation */
		if ((eError != PVRSRV_ERROR_RESOURCE_UNAVAILABLE) &&
			(eError != PVRSRV_ERROR_TIMEOUT) &&
			(eError != PVRSRV_ERROR_STREAM_READLIMIT_REACHED))
		{
			PVR_LOG_ERROR(eError, "BridgeTLAcquireData");
		}
		psSD->uiReadOffset = psSD->uiReadLen = NO_ACQUIRE;
		return eError;
	}
	/* else PVRSRV_OK */

	/* Return the data offset and length to the caller if bytes are available
	 * to be read. Could be zero for non-blocking mode so pass back cleared
	 * values above */
	if (psSD->uiReadLen)
	{
		*ppPacketBuf = psSD->pBaseAddr + psSD->uiReadOffset;
		*pui32BufLen = psSD->uiReadLen;
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR _TLClientReleaseDataLen(
		SHARED_DEV_CONNECTION hDevConnection,
		TL_STREAM_DESC* psSD,
		IMG_UINT32 uiReadLen)
{
	PVRSRV_ERROR eError;

	/* the previous acquire did not return any data, this is a no-operation */
	if (psSD->uiReadLen == 0)
	{
		return PVRSRV_OK;
	}

	/* Check release has not been called twice in a row without an acquire */
	if (psSD->uiReadOffset == NO_ACQUIRE)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: no acquire to release", __func__));
		return PVRSRV_ERROR_RETRY;
	}

	/* Inform the kernel to release the data from the buffer */
	eError = BridgeTLReleaseData(GetBridgeHandle(hDevConnection),
			psSD->hServerSD,
			psSD->uiReadOffset, uiReadLen);
	PVR_LOG_IF_ERROR(eError, "BridgeTLReleaseData");

	/* Reset state to indicate no outstanding acquire */
	psSD->uiReadLen = psSD->uiReadOffset = NO_ACQUIRE;

	return eError;
}

IMG_INTERNAL
PVRSRV_ERROR TLClientReleaseData(SHARED_DEV_CONNECTION hDevConnection,
		IMG_HANDLE hSD)
{
	TL_STREAM_DESC* psSD = (TL_STREAM_DESC*) hSD;

	PVR_ASSERT(hDevConnection);
	PVR_ASSERT(hSD);

	return _TLClientReleaseDataLen(hDevConnection, psSD, psSD->uiReadLen);
}

IMG_INTERNAL
PVRSRV_ERROR TLClientReleaseDataLess(SHARED_DEV_CONNECTION hDevConnection,
		IMG_HANDLE hSD, IMG_UINT32 uiActualReadLen)
{
	TL_STREAM_DESC* psSD = (TL_STREAM_DESC*) hSD;

	PVR_ASSERT(hDevConnection);
	PVR_ASSERT(hSD);

	/* Check the specified size is within the size returned by Acquire */
	if (uiActualReadLen > psSD->uiReadLen)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: no acquire to release", __func__));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	return _TLClientReleaseDataLen(hDevConnection, psSD, uiActualReadLen);
}

IMG_INTERNAL
PVRSRV_ERROR TLClientWriteData(SHARED_DEV_CONNECTION hDevConnection,
		IMG_HANDLE hSD,
		IMG_UINT32 ui32Size,
		IMG_BYTE *pui8Data)
{
	PVRSRV_ERROR eError;
	TL_STREAM_DESC* psSD = (TL_STREAM_DESC*) hSD;

	PVR_ASSERT(hDevConnection);
	PVR_ASSERT(hSD);
	PVR_ASSERT(ui32Size);
	PVR_ASSERT(pui8Data);

	eError = BridgeTLWriteData(GetBridgeHandle(hDevConnection),
			psSD->hServerSD, ui32Size, pui8Data);
	PVR_LOG_IF_ERROR(eError, "BridgeTLWriteData");

	if (eError == PVRSRV_ERROR_STREAM_FULL && !psSD->bPrinted)
	{
		psSD->bPrinted = IMG_TRUE;
	}

	return eError;
}

/******************************************************************************
 End of file (tlclient.c)
******************************************************************************/
