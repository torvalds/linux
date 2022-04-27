/*************************************************************************/ /*!
@File           htb_debug.c
@Title          Debug Functionality
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Provides kernel side debugFS Functionality.
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
#include "rgxdevice.h"
#include "htbserver.h"
#include "htbuffer.h"
#include "htbuffer_types.h"
#include "tlstream.h"
#include "tlclient.h"
#include "pvrsrv_tlcommon.h"
#include "di_server.h"
#include "img_types.h"
#include "img_defs.h"
#include "pvrsrv_error.h"
#include "osfunc.h"
#include "allocmem.h"
#include "pvr_notifier.h"
#include "pvrsrv.h"
#include "htb_debug.h"

// Global data handles for buffer manipulation and processing

typedef struct {
	IMG_PBYTE	pBuf;		/* Raw data buffer from TL stream */
	IMG_UINT32	uiBufLen;	/* Amount of data to process from 'pBuf' */
	IMG_UINT32	uiTotal;	/* Total bytes processed */
	IMG_UINT32	uiMsgLen;	/* Length of HTB message to be processed */
	IMG_PBYTE	pCurr;		/* pointer to current message to be decoded */
	IMG_CHAR	szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN];	/* Output string */
} HTB_Sentinel_t;

typedef struct
{
	DI_ENTRY *psDumpHostDiEntry;  /* debug info entry */
	HTB_Sentinel_t sSentinel;     /* private control structure for HTB DI
	                                 operations */
	IMG_HANDLE hStream;           /* stream handle for debugFS use */
} HTB_DBG_INFO;

static HTB_DBG_INFO g_sHTBData;

// Comment out for extra debug level
// #define HTB_CHATTY_PRINT(x) PVR_DPF(x)
#define HTB_CHATTY_PRINT(x)

typedef void (DI_PRINTF)(const OSDI_IMPL_ENTRY *, const IMG_CHAR *, ...);

/******************************************************************************
 * debugFS display routines
 *****************************************************************************/
static int HTBDumpBuffer(DI_PRINTF, OSDI_IMPL_ENTRY *, void *);

static int _DebugHBTraceDIShow(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	int	retVal;

	PVR_ASSERT(psEntry != NULL);

	/* psEntry should never be NULL */
	if (psEntry == NULL)
	{
		return -1;
	}

	/* Ensure that we have a valid address to use to dump info from. If NULL we
	 * return a failure code to terminate the DI read call. pvData is either
	 * DI_START_TOKEN (for the initial call) or an HTB buffer address for
	 * subsequent calls [returned from the NEXT function]. */
	if (pvData == NULL)
	{
		return -1;
	}

	retVal = HTBDumpBuffer(DIPrintf, psEntry, pvData);

	HTB_CHATTY_PRINT((PVR_DBG_WARNING, "%s: Returning %d", __func__, retVal));

	return retVal;
}

static IMG_UINT32 idToLogIdx(IMG_UINT32);	/* Forward declaration */

/*
 * HTB_GetNextMessage
 *
 * Get next non-empty message block from the buffer held in pSentinel->pBuf
 * If we exhaust the data buffer we refill it (after releasing the previous
 * message(s) [only one non-NULL message, but PAD messages will get released
 * as we traverse them].
 *
 * Input:
 *	pSentinel		references the already acquired data buffer
 *
 * Output:
 *	pSentinel
 *		-> uiMsglen updated to the size of the non-NULL message
 *
 * Returns:
 *	Address of first non-NULL message in the buffer (if any)
 *	NULL if there is no further data available from the stream and the buffer
 *	contents have been drained.
 */
static IMG_PBYTE HTB_GetNextMessage(HTB_Sentinel_t *pSentinel)
{
	void	*pNext, *pLast, *pStart, *pData = NULL;
	void	*pCurrent;		/* Current processing point within buffer */
	PVRSRVTL_PPACKETHDR	ppHdr;	/* Current packet header */
	IMG_UINT32	uiHdrType;		/* Packet header type */
	IMG_UINT32	uiMsgSize;		/* Message size of current packet (bytes) */
	IMG_BOOL	bUnrecognizedErrorPrinted = IMG_FALSE;
	IMG_UINT32	ui32Data;
	IMG_UINT32	ui32LogIdx;
	PVRSRV_ERROR eError;

	PVR_ASSERT(pSentinel != NULL);

	pLast = pSentinel->pBuf + pSentinel->uiBufLen;

	pStart = pSentinel->pBuf;

	pNext = pStart;
	pSentinel->uiMsgLen = 0;	// Reset count for this message
	uiMsgSize = 0;				// nothing processed so far
	ui32LogIdx = HTB_SF_LAST;	// Loop terminator condition

	do
	{
		/*
		 * If we've drained the buffer we must RELEASE and ACQUIRE some more.
		 */
		if (pNext >= pLast)
		{
			eError = TLClientReleaseData(DIRECT_BRIDGE_HANDLE, g_sHTBData.hStream);
			PVR_ASSERT(eError == PVRSRV_OK);

			eError = TLClientAcquireData(DIRECT_BRIDGE_HANDLE,
				g_sHTBData.hStream, &pSentinel->pBuf, &pSentinel->uiBufLen);

			if (PVRSRV_OK != eError)
			{
				PVR_DPF((PVR_DBG_WARNING, "%s: %s FAILED '%s'", __func__,
					"TLClientAcquireData", PVRSRVGETERRORSTRING(eError)));
				return NULL;
			}

			// Reset our limits - if we've returned an empty buffer we're done.
			pLast = pSentinel->pBuf + pSentinel->uiBufLen;
			pStart = pSentinel->pBuf;
			pNext = pStart;

			if (pStart == NULL || pLast == NULL)
			{
				return NULL;
			}
		}

		/*
		 * We should have a header followed by data block(s) in the stream.
		 */

		pCurrent = pNext;
		ppHdr = GET_PACKET_HDR(pCurrent);

		if (ppHdr == NULL)
		{
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: Unexpected NULL packet in Host Trace buffer",
			         __func__));
			pSentinel->uiMsgLen += uiMsgSize;
			return NULL;		// This should never happen
		}

		/*
		 * This should *NEVER* fire. If it does it means we have got some
		 * dubious packet header back from the HTB stream. In this case
		 * the sensible thing is to abort processing and return to
		 * the caller
		 */
		uiHdrType = GET_PACKET_TYPE(ppHdr);

		PVR_ASSERT(uiHdrType < PVRSRVTL_PACKETTYPE_LAST &&
			uiHdrType > PVRSRVTL_PACKETTYPE_UNDEF);

		if (uiHdrType < PVRSRVTL_PACKETTYPE_LAST &&
			uiHdrType > PVRSRVTL_PACKETTYPE_UNDEF)
		{
			/*
			 * We have a (potentially) valid data header. We should see if
			 * the associated packet header matches one of our expected
			 * types.
			 */
			pNext = GET_NEXT_PACKET_ADDR(ppHdr);

			PVR_ASSERT(pNext != NULL);

			uiMsgSize = (IMG_UINT32)((size_t)pNext - (size_t)ppHdr);

			pSentinel->uiMsgLen += uiMsgSize;

			pData = GET_PACKET_DATA_PTR(ppHdr);

			/*
			 * Handle non-DATA packet types. These include PAD fields which
			 * may have data associated and other types. We simply discard
			 * these as they have no decodable information within them.
			 */
			if (uiHdrType != PVRSRVTL_PACKETTYPE_DATA)
			{
				/*
				 * Now release the current non-data packet and proceed to the
				 * next entry (if any).
				 */
				eError = TLClientReleaseDataLess(DIRECT_BRIDGE_HANDLE,
				    g_sHTBData.hStream, uiMsgSize);

				HTB_CHATTY_PRINT((PVR_DBG_WARNING, "%s: Packet Type %x "
				                 "Length %u", __func__, uiHdrType, uiMsgSize));

				if (eError != PVRSRV_OK)
				{
					PVR_DPF((PVR_DBG_WARNING, "%s: %s FAILED - '%s' message"
						" size %u", __func__, "TLClientReleaseDataLess",
						PVRSRVGETERRORSTRING(eError), uiMsgSize));
				}

				eError = TLClientAcquireData(DIRECT_BRIDGE_HANDLE,
					g_sHTBData.hStream, &pSentinel->pBuf, &pSentinel->uiBufLen);

				if (PVRSRV_OK != eError)
				{
					PVR_DPF((PVR_DBG_WARNING, "%s: %s FAILED - %s Giving up",
						__func__, "TLClientAcquireData",
						PVRSRVGETERRORSTRING(eError)));

					return NULL;
				}
				pSentinel->uiMsgLen = 0;
				// Reset our limits - if we've returned an empty buffer we're done.
				pLast = pSentinel->pBuf + pSentinel->uiBufLen;
				pStart = pSentinel->pBuf;
				pNext = pStart;

				if (pStart == NULL || pLast == NULL)
				{
					return NULL;
				}
				continue;
			}
			if (pData == NULL || pData >= pLast)
			{
				continue;
			}
			ui32Data = *(IMG_UINT32 *)pData;
			ui32LogIdx = idToLogIdx(ui32Data);
		}
		else
		{
			PVR_DPF((PVR_DBG_WARNING, "Unexpected Header @%p value %x",
				ppHdr, uiHdrType));

			return NULL;
		}

		/*
		 * Check if the unrecognized ID is valid and therefore, tracebuf
		 * needs updating.
		 */
		if (HTB_SF_LAST == ui32LogIdx && HTB_LOG_VALIDID(ui32Data)
			&& IMG_FALSE == bUnrecognizedErrorPrinted)
		{
			PVR_DPF((PVR_DBG_WARNING,
			    "%s: Unrecognised LOG value '%x' GID %x Params %d ID %x @ '%p'",
			    __func__, ui32Data, HTB_SF_GID(ui32Data),
			    HTB_SF_PARAMNUM(ui32Data), ui32Data & 0xfff, pData));
			bUnrecognizedErrorPrinted = IMG_FALSE;
		}

	} while (HTB_SF_LAST == ui32LogIdx);

	HTB_CHATTY_PRINT((PVR_DBG_WARNING, "%s: Returning data @ %p Log value '%x'",
	                 __func__, pCurrent, ui32Data));

	return pCurrent;
}

/*
 * HTB_GetFirstMessage
 *
 * Called from START to obtain the buffer address of the first message within
 * pSentinel->pBuf. Will ACQUIRE data if the buffer is empty.
 *
 * Input:
 *	pSentinel
 *	pui64Pos			Offset within the debugFS file
 *
 * Output:
 *	pSentinel->pCurr	Set to reference the first valid non-NULL message within
 *						the buffer. If no valid message is found set to NULL.
 *	pSentinel
 *		->pBuf		if unset on entry
 *		->uiBufLen	if pBuf unset on entry
 *
 * Side-effects:
 *	HTB TL stream will be updated to bypass any zero-length PAD messages before
 *	the first non-NULL message (if any).
 */
static void HTB_GetFirstMessage(HTB_Sentinel_t *pSentinel, IMG_UINT64 *pui64Pos)
{
	PVRSRV_ERROR	eError;

	PVR_UNREFERENCED_PARAMETER(pui64Pos);

	if (pSentinel == NULL)
		return;

	if (pSentinel->pBuf == NULL)
	{
		/* Acquire data */
		pSentinel->uiMsgLen = 0;

		eError = TLClientAcquireData(DIRECT_BRIDGE_HANDLE,
		    g_sHTBData.hStream, &pSentinel->pBuf, &pSentinel->uiBufLen);

		if (PVRSRV_OK != eError)
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: %s FAILED '%s'",
			    __func__, "TLClientAcquireData", PVRSRVGETERRORSTRING(eError)));

			pSentinel->pBuf = NULL;
			pSentinel->pCurr = NULL;
		}
		else
		{
			/*
			 * If there is no data available we set pSentinel->pCurr to NULL
			 * and return. This is expected behaviour if we've drained the
			 * data and nothing else has yet been produced.
			 */
			if (pSentinel->uiBufLen == 0 || pSentinel->pBuf == NULL)
			{
				HTB_CHATTY_PRINT((PVR_DBG_WARNING, "%s: Empty Buffer @ %p",
				                 __func__, pSentinel->pBuf));

				pSentinel->pCurr = NULL;
				return;
			}
		}
	}

	/* Locate next message within buffer. NULL => no more data to process */
	pSentinel->pCurr = HTB_GetNextMessage(pSentinel);
}

/*
 * _DebugHBTraceDIStart:
 *
 * Returns the address to use for subsequent 'Show', 'Next', 'Stop' file ops.
 * Return DI_START_TOKEN for the very first call and allocate a sentinel for
 * use by the 'Show' routine and its helpers.
 * This is stored in the psEntry's private hook field.
 *
 * We obtain access to the TLstream associated with the HTB. If this doesn't
 * exist (because no pvrdebug capture trace has been set) we simply return with
 * a NULL value which will stop the DI traversal.
 */
static void *_DebugHBTraceDIStart(OSDI_IMPL_ENTRY *psEntry,
                                  IMG_UINT64 *pui64Pos)
{
	HTB_Sentinel_t	*pSentinel = DIGetPrivData(psEntry);
	PVRSRV_ERROR	eError;
	IMG_UINT32		uiTLMode;
	void			*retVal;
	IMG_HANDLE		hStream;

	/* The sentinel object should have been allocated during the creation
	 * of the DI entry. If it's not there it means that something went
	 * wrong. Return NULL in such case. */
	if (pSentinel == NULL)
	{
		return NULL;
	}

	/* Check to see if the HTB stream has been configured yet. If not, there is
	 * nothing to display so we just return NULL to stop the stream access.
	 */
	if (!HTBIsConfigured())
	{
		return NULL;
	}

	/* Open the stream in non-blocking mode so that we can determine if there
	 * is no data to consume. Also disable the producer callback (if any) and
	 * the open callback so that we do not generate spurious trace data when
	 * accessing the stream.
	 */
	uiTLMode = PVRSRV_STREAM_FLAG_ACQUIRE_NONBLOCKING|
			   PVRSRV_STREAM_FLAG_DISABLE_PRODUCER_CALLBACK|
			   PVRSRV_STREAM_FLAG_IGNORE_OPEN_CALLBACK;

	/* If two or more processes try to read from this file at the same time
	 * the TLClientOpenStream() function will handle this by allowing only
	 * one of them to actually open the stream. The other process will get
	 * an error stating that the stream is already open. The open function
	 * is threads safe. */
	eError = TLClientOpenStream(DIRECT_BRIDGE_HANDLE, HTB_STREAM_NAME, uiTLMode,
	                            &hStream);

	if (eError == PVRSRV_ERROR_ALREADY_OPEN)
	{
		/* Stream allows only one reader so return error if it's already
		 * opened. */
		HTB_CHATTY_PRINT((PVR_DBG_WARNING, "%s: Stream handle %p already "
		                 "exists for %s", __func__, g_sHTBData.hStream,
		                 HTB_STREAM_NAME));
		return NULL;
	}
	else if (eError != PVRSRV_OK)
	{
		/*
		 * No stream available so nothing to report
		 */
		return NULL;
	}

	/* There is a window where hStream can be NULL but the stream is already
	 * opened. This shouldn't matter since the TLClientOpenStream() will make
	 * sure that only one stream can be opened and only one process can reach
	 * this place at a time. Also the .stop function will be always called
	 * after this function returns so there should be no risk of stream
	 * not being closed. */
	PVR_ASSERT(g_sHTBData.hStream == NULL);
	g_sHTBData.hStream = hStream;

	/* We're starting the read operation so ensure we properly zero the
	 * sentinel object. */
	memset(pSentinel, 0, sizeof(*pSentinel));

	/*
	 * Find the first message location within pSentinel->pBuf
	 * => for DI_START_TOKEN we must issue our first ACQUIRE, also for the
	 * subsequent re-START calls (if any).
	 */

	HTB_GetFirstMessage(pSentinel, pui64Pos);

	retVal = *pui64Pos == 0 ? DI_START_TOKEN : pSentinel->pCurr;

	HTB_CHATTY_PRINT((PVR_DBG_WARNING, "%s: Returning %p, Stream %s @ %p",
	                 __func__, retVal, HTB_STREAM_NAME, g_sHTBData.hStream));

	return retVal;
}

/*
 * _DebugTBTraceDIStop:
 *
 * Stop processing data collection and release any previously allocated private
 * data structure if we have exhausted the previously filled data buffers.
 */
static void _DebugHBTraceDIStop(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	HTB_Sentinel_t *pSentinel = DIGetPrivData(psEntry);
	IMG_UINT32 uiMsgLen;
	PVRSRV_ERROR eError;

	if (pSentinel == NULL)
	{
		return;
	}

	uiMsgLen = pSentinel->uiMsgLen;

	HTB_CHATTY_PRINT((PVR_DBG_WARNING, "%s: MsgLen = %d", __func__, uiMsgLen));

	/* If we get here the handle should never be NULL because
	 * _DebugHBTraceDIStart() shouldn't allow that. */
	if (g_sHTBData.hStream == NULL)
	{
		return;
	}

	if (uiMsgLen != 0)
	{
		eError = TLClientReleaseDataLess(DIRECT_BRIDGE_HANDLE,
		                                 g_sHTBData.hStream, uiMsgLen);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: %s FAILED - %s, nBytes %u",
			        __func__, "TLClientReleaseDataLess",
			        PVRSRVGETERRORSTRING(eError), uiMsgLen));
		}
	}

	eError = TLClientCloseStream(DIRECT_BRIDGE_HANDLE, g_sHTBData.hStream);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_WARNING, "%s() failed (%s) in %s()",
		        "TLClientCloseStream", PVRSRVGETERRORSTRING(eError),
		        __func__));
	}

	g_sHTBData.hStream = NULL;
}


/*
 * _DebugHBTraceDINext:
 *
 * This is where we release any acquired data which has been processed by the
 * DIShow routine. If we have encountered a DI entry overflow we stop
 * processing and return NULL. Otherwise we release the message that we
 * previously processed and simply update our position pointer to the next
 * valid HTB message (if any)
 */
static void *_DebugHBTraceDINext(OSDI_IMPL_ENTRY *psEntry, void *pvPriv,
                                 IMG_UINT64 *pui64Pos)
{
	HTB_Sentinel_t *pSentinel = DIGetPrivData(psEntry);
	IMG_UINT64 ui64CurPos;
	PVRSRV_ERROR eError;

	PVR_UNREFERENCED_PARAMETER(pvPriv);

	if (pui64Pos)
	{
		ui64CurPos = *pui64Pos;
		*pui64Pos = ui64CurPos + 1;
	}

	/* Determine if we've had an overflow on the previous 'Show' call. If so
	 * we leave the previously acquired data in the queue (by releasing 0 bytes)
	 * and return NULL to end this DI entry iteration.
	 * If we have not overflowed we simply get the next HTB message and use that
	 * for our display purposes. */

	if (DIHasOverflowed(psEntry))
	{
		(void) TLClientReleaseDataLess(DIRECT_BRIDGE_HANDLE, g_sHTBData.hStream,
		                               0);

		HTB_CHATTY_PRINT((PVR_DBG_WARNING, "%s: OVERFLOW - returning NULL",
		                 __func__));

		return NULL;
	}
	else
	{
		eError = TLClientReleaseDataLess(DIRECT_BRIDGE_HANDLE,
		                                 g_sHTBData.hStream,
		                                 pSentinel->uiMsgLen);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: %s FAILED '%s' @ %p Length %d",
			        __func__, "TLClientReleaseDataLess",
			        PVRSRVGETERRORSTRING(eError), pSentinel->pCurr,
			        pSentinel->uiMsgLen));
			PVR_DPF((PVR_DBG_WARNING, "%s: Buffer @ %p..%p", __func__,
			        pSentinel->pBuf,
			        (IMG_PBYTE) (pSentinel->pBuf + pSentinel->uiBufLen)));

		}

		eError = TLClientAcquireData(DIRECT_BRIDGE_HANDLE,
		                             g_sHTBData.hStream, &pSentinel->pBuf,
		                             &pSentinel->uiBufLen);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: %s FAILED '%s'\nPrev message len %d",
			        __func__, "TLClientAcquireData",
			        PVRSRVGETERRORSTRING(eError), pSentinel->uiMsgLen));
			pSentinel->pBuf = NULL;
		}

		pSentinel->uiMsgLen = 0; /* We don't (yet) know the message size */
	}

	HTB_CHATTY_PRINT((PVR_DBG_WARNING, "%s: Returning %p Msglen %d", __func__,
	                 pSentinel->pBuf, pSentinel->uiMsgLen));

	if (pSentinel->pBuf == NULL || pSentinel->uiBufLen == 0)
	{
		return NULL;
	}

	pSentinel->pCurr = HTB_GetNextMessage(pSentinel);

	return pSentinel->pCurr;
}

/******************************************************************************
 * HTB Dumping routines and definitions
 *****************************************************************************/
#define IS_VALID_FMT_STRING(FMT) (strchr(FMT, '%') != NULL)
#define MAX_STRING_SIZE (128)

typedef enum
{
	TRACEBUF_ARG_TYPE_INT,
	TRACEBUF_ARG_TYPE_ERR,
	TRACEBUF_ARG_TYPE_NONE
} TRACEBUF_ARG_TYPE;

/*
 * Array of all Host Trace log IDs used to convert the tracebuf data
 */
typedef struct _HTB_TRACEBUF_LOG_ {
	HTB_LOG_SFids eSFId;
	IMG_CHAR      *pszName;
	IMG_CHAR      *pszFmt;
	IMG_UINT32    ui32ArgNum;
} HTB_TRACEBUF_LOG;

static const HTB_TRACEBUF_LOG aLogs[] = {
#define X(a, b, c, d, e) {HTB_LOG_CREATESFID(a,b,e), #c, d, e},
	HTB_LOG_SFIDLIST
#undef X
};

static const IMG_CHAR *aGroups[] = {
#define X(A,B) #B,
	HTB_LOG_SFGROUPLIST
#undef X
};
static const IMG_UINT32 uiMax_aGroups = ARRAY_SIZE(aGroups) - 1;

static TRACEBUF_ARG_TYPE ExtractOneArgFmt(IMG_CHAR **, IMG_CHAR *);
/*
 * ExtractOneArgFmt
 *
 * Scan the input 'printf-like' string *ppszFmt and return the next
 * value string to be displayed. If there is no '%' format field in the
 * string we return 'TRACEBUF_ARG_TYPE_NONE' and leave the input string
 * untouched.
 *
 * Input
 *	ppszFmt          reference to format string to be decoded
 *	pszOneArgFmt     single field format from *ppszFmt
 *
 * Returns
 *	TRACEBUF_ARG_TYPE_ERR       unrecognised argument
 *	TRACEBUF_ARG_TYPE_INT       variable is of numeric type
 *	TRACEBUF_ARG_TYPE_NONE      no variable reference in *ppszFmt
 *
 * Side-effect
 *	*ppszFmt is updated to reference the next part of the format string
 *	to be scanned
 */
static TRACEBUF_ARG_TYPE ExtractOneArgFmt(
	IMG_CHAR **ppszFmt,
	IMG_CHAR *pszOneArgFmt)
{
	IMG_CHAR          *pszFmt;
	IMG_CHAR          *psT;
	IMG_UINT32        ui32Count = MAX_STRING_SIZE;
	IMG_UINT32        ui32OneArgSize;
	TRACEBUF_ARG_TYPE eRet = TRACEBUF_ARG_TYPE_ERR;

	if (NULL == ppszFmt)
		return TRACEBUF_ARG_TYPE_ERR;

	pszFmt = *ppszFmt;
	if (NULL == pszFmt)
		return TRACEBUF_ARG_TYPE_ERR;

	/*
	 * Find the first '%'
	 * NOTE: we can be passed a simple string to display which will have no
	 * parameters embedded within it. In this case we simply return
	 * TRACEBUF_ARG_TYPE_NONE and the string contents will be the full pszFmt
	 */
	psT = strchr(pszFmt, '%');
	if (psT == NULL)
	{
		return TRACEBUF_ARG_TYPE_NONE;
	}

	/* Find next conversion identifier after the initial '%' */
	while ((*psT++) && (ui32Count-- > 0))
	{
		switch (*psT)
		{
			case 'd':
			case 'i':
			case 'o':
			case 'u':
			case 'x':
			case 'X':
			{
				eRet = TRACEBUF_ARG_TYPE_INT;
				goto _found_arg;
			}
			case 's':
			{
				eRet = TRACEBUF_ARG_TYPE_ERR;
				goto _found_arg;
			}
		}
	}

	if ((psT == NULL) || (ui32Count == 0)) return TRACEBUF_ARG_TYPE_ERR;

_found_arg:
	ui32OneArgSize = psT - pszFmt + 1;
	OSCachedMemCopy(pszOneArgFmt, pszFmt, ui32OneArgSize);
	pszOneArgFmt[ui32OneArgSize] = '\0';

	*ppszFmt = psT + 1;

	return eRet;
}

static IMG_UINT32 idToLogIdx(IMG_UINT32 ui32CheckData)
{
	IMG_UINT32	i = 0;
	for (i = 0; aLogs[i].eSFId != HTB_SF_LAST; i++)
	{
		if ( ui32CheckData == aLogs[i].eSFId )
			return i;
	}
	/* Nothing found, return max value */
	return HTB_SF_LAST;
}

/*
 * DecodeHTB
 *
 * Decode the data buffer message located at pBuf. This should be a valid
 * HTB message as we are provided with the start of the buffer. If empty there
 * is no message to process. We update the uiMsgLen field with the size of the
 * HTB message that we have processed so that it can be returned to the system
 * on successful logging of the message to the output file.
 *
 *	Input
 *		pSentinel reference to newly read data and pending completion data
 *		          from a previous invocation [handle DI entry buffer overflow]
 *		 -> pBuf         reference to raw data that we are to parse
 *		 -> uiBufLen     total number of bytes of data available
 *		 -> pCurr        start of message to decode
 *
 *		pvDumpDebugFile     output file
 *		pfnDumpDebugPrintf  output generating routine
 *
 * Output
 *		pSentinel
 *		 -> uiMsgLen	length of the decoded message which will be freed to
 *						the system on successful completion of the DI entry
 *						update via _DebugHBTraceDINext(),
 * Return Value
 *		0				successful decode
 *		-1				unsuccessful decode
 */
static int
DecodeHTB(HTB_Sentinel_t *pSentinel, OSDI_IMPL_ENTRY *pvDumpDebugFile,
          DI_PRINTF pfnDumpDebugPrintf)
{
	IMG_UINT32	ui32Data, ui32LogIdx, ui32ArgsCur;
	IMG_CHAR	*pszFmt = NULL;
	IMG_CHAR	aszOneArgFmt[MAX_STRING_SIZE];
	IMG_BOOL	bUnrecognizedErrorPrinted = IMG_FALSE;

	size_t	nPrinted;

	void	*pNext, *pLast, *pStart, *pData = NULL;
	PVRSRVTL_PPACKETHDR	ppHdr;	/* Current packet header */
	IMG_UINT32	uiHdrType;		/* Packet header type */
	IMG_UINT32	uiMsgSize;		/* Message size of current packet (bytes) */
	IMG_BOOL	bPacketsDropped;

	pLast = pSentinel->pBuf + pSentinel->uiBufLen;
	pStart = pSentinel->pCurr;

	pSentinel->uiMsgLen = 0;	// Reset count for this message

	HTB_CHATTY_PRINT((PVR_DBG_WARNING, "%s: Buf @ %p..%p, Length = %d",
	                 __func__, pStart, pLast, pSentinel->uiBufLen));

	/*
	 * We should have a DATA header with the necessary information following
	 */
	ppHdr = GET_PACKET_HDR(pStart);

	if (ppHdr == NULL)
	{
			PVR_DPF((PVR_DBG_ERROR,
			    "%s: Unexpected NULL packet in Host Trace buffer", __func__));
			return -1;
	}

	uiHdrType = GET_PACKET_TYPE(ppHdr);
	PVR_ASSERT(uiHdrType == PVRSRVTL_PACKETTYPE_DATA);

	pNext = GET_NEXT_PACKET_ADDR(ppHdr);

	PVR_ASSERT(pNext != NULL);

	uiMsgSize = (IMG_UINT32)((size_t)pNext - (size_t)ppHdr);

	pSentinel->uiMsgLen += uiMsgSize;

	pData = GET_PACKET_DATA_PTR(ppHdr);

	if (pData == NULL || pData >= pLast)
	{
		HTB_CHATTY_PRINT((PVR_DBG_WARNING, "%s: pData = %p, pLast = %p "
		                 "Returning 0", __func__, pData, pLast));
		return 0;
	}

	ui32Data = *(IMG_UINT32 *)pData;
	ui32LogIdx = idToLogIdx(ui32Data);

	/*
	 * Check if the unrecognised ID is valid and therefore, tracebuf
	 * needs updating.
	 */
	if (ui32LogIdx == HTB_SF_LAST)
	{
		if (HTB_LOG_VALIDID(ui32Data))
		{
			if (!bUnrecognizedErrorPrinted)
			{
				PVR_DPF((PVR_DBG_WARNING,
				    "%s: Unrecognised LOG value '%x' GID %x Params %d ID %x @ '%p'",
				    __func__, ui32Data, HTB_SF_GID(ui32Data),
				    HTB_SF_PARAMNUM(ui32Data), ui32Data & 0xfff, pData));
				bUnrecognizedErrorPrinted = IMG_TRUE;
			}

			return 0;
		}

		PVR_DPF((PVR_DBG_ERROR,
		    "%s: Unrecognised and invalid LOG value detected '%x'",
		    __func__, ui32Data));

		return -1;
	}

	/* The string format we are going to display */
	/*
	 * The display will show the header (log-ID, group-ID, number of params)
	 * The maximum parameter list length = 15 (only 4bits used to encode)
	 * so we need HEADER + 15 * sizeof(UINT32) and the displayed string
	 * describing the event. We use a buffer in the per-process pSentinel
	 * structure to hold the data.
	 */
	pszFmt = aLogs[ui32LogIdx].pszFmt;

	/* add the message payload size to the running count */
	ui32ArgsCur = HTB_SF_PARAMNUM(ui32Data);

	/* Determine if we've over-filled the buffer and had to drop packets */
	bPacketsDropped = CHECK_PACKETS_DROPPED(ppHdr);
	if (bPacketsDropped ||
		(uiHdrType == PVRSRVTL_PACKETTYPE_MOST_RECENT_WRITE_FAILED))
	{
		/* Flag this as it is useful to know ... */

		PVR_DUMPDEBUG_LOG("\n<========================== *** PACKETS DROPPED *** ======================>\n");
	}

	{
		IMG_UINT32 ui32Timestampns, ui32PID, ui32TID;
		IMG_UINT64 ui64Timestamp, ui64TimestampSec;
		IMG_CHAR	*szBuffer = pSentinel->szBuffer;	// Buffer start
		IMG_CHAR	*pszBuffer = pSentinel->szBuffer;	// Current place in buf
		size_t		uBufBytesAvailable = sizeof(pSentinel->szBuffer);
		IMG_UINT32	*pui32Data = (IMG_UINT32 *)pData;
		IMG_UINT32	ui_aGroupIdx;

		// Get PID field from data stream
		pui32Data++;
		ui32PID = *pui32Data;
		// Get TID field from data stream
		pui32Data++;
		ui32TID = *pui32Data;
		// Get Timestamp part 1 from data stream
		pui32Data++;
		ui64Timestamp = (IMG_UINT64) *pui32Data << 32;
		// Get Timestamp part 2 from data stream
		pui32Data++;
		ui64Timestamp |= (IMG_UINT64) *pui32Data;
		// Move to start of message contents data
		pui32Data++;

		/*
		 * We need to snprintf the data to a local in-kernel buffer
		 * and then PVR_DUMPDEBUG_LOG() that in one shot
		 */
		ui_aGroupIdx = MIN(HTB_SF_GID(ui32Data), uiMax_aGroups);

		/* Divide by 1B to get seconds & mod using output var (nanosecond resolution)*/
		ui64TimestampSec = OSDivide64r64(ui64Timestamp, 1000000000, &ui32Timestampns);

		nPrinted = OSSNPrintf(szBuffer, uBufBytesAvailable, "%010"IMG_UINT64_FMTSPEC".%09u:%-5u-%-5u-%s> ",
			ui64TimestampSec, ui32Timestampns, ui32PID, ui32TID, aGroups[ui_aGroupIdx]);
		if (nPrinted >= uBufBytesAvailable)
		{
			PVR_DUMPDEBUG_LOG("Buffer overrun - "IMG_SIZE_FMTSPEC" printed,"
				" max space "IMG_SIZE_FMTSPEC"\n", nPrinted,
				uBufBytesAvailable);

			nPrinted = uBufBytesAvailable;	/* Ensure we don't overflow buffer */
		}

		PVR_DUMPDEBUG_LOG("%s", pszBuffer);
		/* Update where our next 'output' point in the buffer is */
		pszBuffer += nPrinted;
		uBufBytesAvailable -= nPrinted;

		/*
		 * Print one argument at a time as this simplifies handling variable
		 * number of arguments. Special case handling for no arguments.
		 * This is the case for simple format strings such as
		 * HTB_SF_MAIN_KICK_UNCOUNTED.
		 */
		if (ui32ArgsCur == 0)
		{
			if (pszFmt)
			{
				nPrinted = OSStringLCopy(pszBuffer, pszFmt, uBufBytesAvailable);
				if (nPrinted >= uBufBytesAvailable)
				{
					PVR_DUMPDEBUG_LOG("Buffer overrun - "IMG_SIZE_FMTSPEC" printed,"
						" max space "IMG_SIZE_FMTSPEC"\n", nPrinted,
						uBufBytesAvailable);
					nPrinted = uBufBytesAvailable;	/* Ensure we don't overflow buffer */
				}
				PVR_DUMPDEBUG_LOG("%s", pszBuffer);
				pszBuffer += nPrinted;
				/* Don't update the uBufBytesAvailable as we have finished this
				 * message decode. pszBuffer - szBuffer is the total amount of
				 * data we have decoded.
				 */
			}
		}
		else
		{
			if (HTB_SF_GID(ui32Data) == HTB_GID_CTRL && HTB_SF_ID(ui32Data) == HTB_ID_MARK_SCALE)
			{
				IMG_UINT32 i;
				IMG_UINT32 ui32ArgArray[HTB_MARK_SCALE_ARG_ARRAY_SIZE];
				IMG_UINT64 ui64OSTS = 0;
				IMG_UINT32 ui32OSTSRem = 0;
				IMG_UINT64 ui64CRTS = 0;

				/* Retrieve 6 args to an array */
				for (i = 0; i < ARRAY_SIZE(ui32ArgArray); i++)
				{
					ui32ArgArray[i] = *pui32Data;
					pui32Data++;
					--ui32ArgsCur;
				}

				ui64OSTS = (IMG_UINT64) ui32ArgArray[HTB_ARG_OSTS_PT1] << 32 | ui32ArgArray[HTB_ARG_OSTS_PT2];
				ui64CRTS = (IMG_UINT64) ui32ArgArray[HTB_ARG_CRTS_PT1] << 32 | ui32ArgArray[HTB_ARG_CRTS_PT2];

				/* Divide by 1B to get seconds, remainder in nano seconds*/
				ui64OSTS = OSDivide64r64(ui64OSTS, 1000000000, &ui32OSTSRem);

				nPrinted = OSSNPrintf(pszBuffer,
						              uBufBytesAvailable,
						              "HTBFWMkSync Mark=%u OSTS=%010" IMG_UINT64_FMTSPEC ".%09u CRTS=%" IMG_UINT64_FMTSPEC " CalcClkSpd=%u\n",
						              ui32ArgArray[HTB_ARG_SYNCMARK],
						              ui64OSTS,
						              ui32OSTSRem,
						              ui64CRTS,
						              ui32ArgArray[HTB_ARG_CLKSPD]);

				if (nPrinted >= uBufBytesAvailable)
				{
					PVR_DUMPDEBUG_LOG("Buffer overrun - "IMG_SIZE_FMTSPEC" printed,"
						" max space "IMG_SIZE_FMTSPEC"\n", nPrinted,
						uBufBytesAvailable);
					nPrinted = uBufBytesAvailable;	/* Ensure we don't overflow buffer */
				}

				PVR_DUMPDEBUG_LOG("%s", pszBuffer);
				pszBuffer += nPrinted;
				uBufBytesAvailable -= nPrinted;
			}
			else
			{
				while (IS_VALID_FMT_STRING(pszFmt) && (uBufBytesAvailable > 0))
				{
					IMG_UINT32 ui32TmpArg = *pui32Data;
					TRACEBUF_ARG_TYPE eArgType;

					eArgType = ExtractOneArgFmt(&pszFmt, aszOneArgFmt);

					pui32Data++;
					ui32ArgsCur--;

					switch (eArgType)
					{
						case TRACEBUF_ARG_TYPE_INT:
							nPrinted = OSSNPrintf(pszBuffer, uBufBytesAvailable,
								aszOneArgFmt, ui32TmpArg);
							break;

						case TRACEBUF_ARG_TYPE_NONE:
							nPrinted = OSStringLCopy(pszBuffer, pszFmt,
								uBufBytesAvailable);
							break;

						default:
							nPrinted = OSSNPrintf(pszBuffer, uBufBytesAvailable,
								"Error processing arguments, type not "
								"recognized (fmt: %s)", aszOneArgFmt);
							break;
					}
					if (nPrinted >= uBufBytesAvailable)
					{
						PVR_DUMPDEBUG_LOG("Buffer overrun - "IMG_SIZE_FMTSPEC" printed,"
							" max space "IMG_SIZE_FMTSPEC"\n", nPrinted,
							uBufBytesAvailable);
						nPrinted = uBufBytesAvailable;	/* Ensure we don't overflow buffer */
					}
					PVR_DUMPDEBUG_LOG("%s", pszBuffer);
					pszBuffer += nPrinted;
					uBufBytesAvailable -= nPrinted;
				}
				/* Display any remaining text in pszFmt string */
				if (pszFmt)
				{
					nPrinted = OSStringLCopy(pszBuffer, pszFmt, uBufBytesAvailable);
					if (nPrinted >= uBufBytesAvailable)
					{
						PVR_DUMPDEBUG_LOG("Buffer overrun - "IMG_SIZE_FMTSPEC" printed,"
							" max space "IMG_SIZE_FMTSPEC"\n", nPrinted,
							uBufBytesAvailable);
						nPrinted = uBufBytesAvailable;	/* Ensure we don't overflow buffer */
					}
					PVR_DUMPDEBUG_LOG("%s", pszBuffer);
					pszBuffer += nPrinted;
					/* Don't update the uBufBytesAvailable as we have finished this
					 * message decode. pszBuffer - szBuffer is the total amount of
					 * data we have decoded.
					 */
				}
			}
		}

		/* Update total bytes processed */
		pSentinel->uiTotal += (pszBuffer - szBuffer);
	}
	return 0;
}

/*
 * HTBDumpBuffer: Dump the Host Trace Buffer using the TLClient API
 *
 * This routine just parses *one* message from the buffer.
 * The stream will be opened by the Start() routine, closed by the Stop() and
 * updated for data consumed by this routine once we have DebugPrintf'd it.
 * We use the new TLReleaseDataLess() routine which enables us to update the
 * HTB contents with just the amount of data we have successfully processed.
 * If we need to leave the data available we can call this with a 0 count.
 * This will happen in the case of a buffer overflow so that we can reprocess
 * any data which wasn't handled before.
 *
 * In case of overflow or an error we return -1 otherwise 0
 *
 * Input:
 *  pfnPrintf           output routine to display data
 *  psEntry             handle to debug frontend
 *  pvData              data address to start dumping from
 *                      (set by Start() / Next())
 */
static int HTBDumpBuffer(DI_PRINTF pfnPrintf, OSDI_IMPL_ENTRY *psEntry,
                         void *pvData)
{
	HTB_Sentinel_t *pSentinel = DIGetPrivData(psEntry);

	PVR_ASSERT(pvData != NULL);

	if (pvData == DI_START_TOKEN)
	{
		if (pSentinel->pCurr == NULL)
		{
			HTB_CHATTY_PRINT((PVR_DBG_WARNING, "%s: DI_START_TOKEN, "
			                 "Empty buffer", __func__));
			return 0;
		}
		PVR_ASSERT(pSentinel->pCurr != NULL);

		/* Display a Header as we have data to process */
		pfnPrintf(psEntry, "%-20s:%-5s-%-5s-%s  %s\n", "Timestamp", "PID", "TID", "Group>",
		         "Log Entry");
	}
	else
	{
		if (pvData != NULL)
		{
			PVR_ASSERT(pSentinel->pCurr == pvData);
		}
	}

	return DecodeHTB(pSentinel, psEntry, pfnPrintf);
}


/******************************************************************************
 * External Entry Point routines ...
 *****************************************************************************/
/*************************************************************************/ /*!
 @Function     HTB_CreateDIEntry

 @Description  Create the debugFS entry-point for the host-trace-buffer

 @Returns      eError          internal error code, PVRSRV_OK on success

 */ /*************************************************************************/
PVRSRV_ERROR HTB_CreateDIEntry(void)
{
	PVRSRV_ERROR eError;

	DI_ITERATOR_CB sIterator = {
		.pfnStart = _DebugHBTraceDIStart,
		.pfnStop  = _DebugHBTraceDIStop,
		.pfnNext  = _DebugHBTraceDINext,
		.pfnShow  = _DebugHBTraceDIShow,
	};

	eError = DICreateEntry("host_trace", NULL, &sIterator,
	                       &g_sHTBData.sSentinel,
	                       DI_ENTRY_TYPE_GENERIC,
	                       &g_sHTBData.psDumpHostDiEntry);
	PVR_LOG_RETURN_IF_ERROR(eError, "DICreateEntry");

	return PVRSRV_OK;
}


/*************************************************************************/ /*!
 @Function     HTB_DestroyDIEntry

 @Description  Destroy the debugFS entry-point created by earlier
               HTB_CreateDIEntry() call.
*/ /**************************************************************************/
void HTB_DestroyDIEntry(void)
{
	if (g_sHTBData.psDumpHostDiEntry != NULL)
	{
		DIDestroyEntry(g_sHTBData.psDumpHostDiEntry);
		g_sHTBData.psDumpHostDiEntry = NULL;
	}
}

/* EOF */
