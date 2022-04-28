/*************************************************************************/ /*!
@File
@Title          OS agnostic implementation of Debug Info interface.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements osdi_impl.h API to provide access to driver's
                debug data via pvrdebug.
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

#include "allocmem.h"
#include "hash.h"
#include "img_defs.h"
#include "img_types.h"
#include "lock.h"
#include "osfunc_common.h"
#include "osfunc.h" /* for thread */
#include "tlstream.h"
#include "dllist.h"

#include "osdi_impl.h"
#include "di_impl_brg.h"
#include "di_impl_brg_intern.h"
#include "pvr_dicommon.h"
#if defined(PVRSRV_FORCE_UNLOAD_IF_BAD_STATE)
#include "pvrsrv.h"
#endif

#define ENTRIES_TABLE_INIT_SIZE 64
#define STREAM_BUFFER_SIZE 0x4000 /* 16KB */
#define STREAM_LINE_LENGTH 512

#if defined(PVRSRV_SERVER_THREADS_INDEFINITE_SLEEP)
#define WRITER_THREAD_SLEEP_TIMEOUT 0ull
#else
#define WRITER_THREAD_SLEEP_TIMEOUT 28800000000ull
#endif
#define WRITER_THREAD_DESTROY_TIMEOUT 100000ull
#define WRITER_THREAD_DESTROY_RETRIES 10u

#define WRITE_RETRY_COUNT 10      /* retry a write to a TL buffer 10 times */
#define WRITE_RETRY_WAIT_TIME 100 /* wait 10ms between write retries */

typedef enum THREAD_STATE
{
	THREAD_STATE_NULL,
	THREAD_STATE_ALIVE,
	THREAD_STATE_TERMINATED,
} THREAD_STATE;

static struct DIIB_IMPL
{
	HASH_TABLE *psEntriesTable;    /*!< Table of entries. */
	POS_LOCK psEntriesLock;        /*!< Protects psEntriesTable. */
	IMG_HANDLE hWriterThread;
	IMG_HANDLE hWriterEventObject;
	ATOMIC_T eThreadState;

	DLLIST_NODE sWriterQueue;
	POS_LOCK psWriterLock;         /*!< Protects sWriterQueue. */
} *_g_psImpl;

struct DIIB_GROUP
{
	const IMG_CHAR *pszName;
	struct DIIB_GROUP *psParentGroup;
};

struct DIIB_ENTRY
{
	struct DIIB_GROUP *psParentGroup;
	OSDI_IMPL_ENTRY sImplEntry;
	DI_ITERATOR_CB sIterCb;
	DI_ENTRY_TYPE eType;
	IMG_CHAR pszFullPath[DI_IMPL_BRG_PATH_LEN];
	void *pvPrivData;

	POS_LOCK hLock; /*!< Protects access to entry's iterator. */
};

struct DI_CONTEXT_TAG
{
	IMG_HANDLE hStream;
	ATOMIC_T iRefCnt;
	IMG_BOOL bClientConnected; /*!< Indicated that the client is or is not
	                                connected to the DI. */
};

struct DIIB_WORK_ITEM
{
	DI_CONTEXT *psContext;
	DIIB_ENTRY *psEntry;
	IMG_UINT64 ui64Size;
	IMG_UINT64 ui64Offset;

	DLLIST_NODE sQueueElement;
};

/* Declaring function here to avoid dependencies that are introduced by
 * including osfunc.h. */
IMG_INT32 OSStringNCompare(const IMG_CHAR *pStr1, const IMG_CHAR *pStr2,
                           size_t uiSize);

/* djb2 hash function is public domain */
static IMG_UINT32 _Hash(size_t uKeySize, void *pKey, IMG_UINT32 uHashTabLen)
{
	IMG_CHAR *pszStr = pKey;
	IMG_UINT32 ui32Hash = 5381, ui32Char;

	PVR_UNREFERENCED_PARAMETER(uKeySize);
	PVR_UNREFERENCED_PARAMETER(uHashTabLen);

	while ((ui32Char = *pszStr++) != '\0')
	{
		ui32Hash = ((ui32Hash << 5) + ui32Hash) + ui32Char; /* hash * 33 + c */
	}

	return ui32Hash;
}

static IMG_BOOL _Compare(size_t uKeySize, void *pKey1, void *pKey2)
{
	IMG_CHAR *pszKey1 = pKey1, *pszKey2 = pKey2;

	return OSStringNCompare(pszKey1, pszKey2, uKeySize) == 0;
}

/* ----- native callbacks interface ----------------------------------------- */

static void _WriteWithRetires(void *pvNativeHandle, const IMG_CHAR *pszStr,
                              IMG_UINT uiLen)
{
	PVRSRV_ERROR eError;
	IMG_INT iRetry = 0;
	IMG_UINT32 ui32Flags = TL_FLAG_NO_WRITE_FAILED;

	do
	{
		/* Try to write to the buffer but don't inject MOST_RECENT_WRITE_FAILED
		 * packet in case of failure because we're going to retry. */
		eError = TLStreamWriteRetFlags(pvNativeHandle, (IMG_UINT8 *) pszStr,
		                               uiLen, &ui32Flags);
		if (eError == PVRSRV_ERROR_STREAM_FULL)
		{
			// wait to give the client a change to read
			OSSleepms(WRITE_RETRY_WAIT_TIME);
		}
	}
	while (eError == PVRSRV_ERROR_STREAM_FULL && iRetry++ < WRITE_RETRY_COUNT);

	/* One last try to write to the buffer. In this case upon failure
	 * a MOST_RECENT_WRITE_FAILED packet will be inject to the buffer to
	 * indicate data loss. */
	if (eError == PVRSRV_ERROR_STREAM_FULL)
	{
		eError = TLStreamWrite(pvNativeHandle, (IMG_UINT8 *) pszStr, uiLen);
	}

	PVR_LOG_IF_ERROR(eError, "TLStreamWrite");
}

__printf(2, 0)
static void _VPrintf(void *pvNativeHandle, const IMG_CHAR *pszFmt,
                     va_list pArgs)
{
	IMG_CHAR pcBuffer[STREAM_LINE_LENGTH];
	IMG_UINT uiLen = OSVSNPrintf(pcBuffer, sizeof(pcBuffer) - 1, pszFmt, pArgs);
	pcBuffer[uiLen] = '\0';

	_WriteWithRetires(pvNativeHandle, pcBuffer, uiLen + 1);
}

static void _Puts(void *pvNativeHandle, const IMG_CHAR *pszStr)
{
	_WriteWithRetires(pvNativeHandle, pszStr, OSStringLength(pszStr) + 1);
}

static IMG_BOOL _HasOverflowed(void *pvNativeHandle)
{
	PVR_UNREFERENCED_PARAMETER(pvNativeHandle);
	return IMG_FALSE;
}

static OSDI_IMPL_ENTRY_CB _g_sEntryCallbacks = {
	.pfnVPrintf = _VPrintf,
	.pfnPuts = _Puts,
	.pfnHasOverflowed = _HasOverflowed,
};

/* ----- entry operations --------------------------------------------------- */

static PVRSRV_ERROR _ContextUnrefAndMaybeDestroy(DI_CONTEXT *psContext)
{
	if (OSAtomicDecrement(&psContext->iRefCnt) == 0)
	{
		TLStreamClose(psContext->hStream);
		OSFreeMem(psContext);
	}

	return PVRSRV_OK;
}

static IMG_INT64 _ReadGeneric(const DI_CONTEXT *psContext, DIIB_ENTRY *psEntry)
{
	IMG_INT64 iRet = 0;
	IMG_UINT64 ui64Pos = 0;
	DI_ITERATOR_CB *psIter = &psEntry->sIterCb;
	OSDI_IMPL_ENTRY *psImplEntry = &psEntry->sImplEntry;
	PVRSRV_ERROR eError;

	if (psIter->pfnStart != NULL)
	{
		/* this is a full sequence of the operation */
		void *pvData = psIter->pfnStart(psImplEntry, &ui64Pos);

		while (pvData != NULL && psContext->bClientConnected)
		{
			iRet = psIter->pfnShow(psImplEntry, pvData);
			if (iRet < 0)
			{
				break;
			}

			pvData = psIter->pfnNext(psImplEntry, pvData, &ui64Pos);
		}

		psIter->pfnStop(psImplEntry, pvData);
	}
	else if (psIter->pfnShow != NULL)
	{
		/* this is a simplified sequence of the operation */
		iRet = psIter->pfnShow(psImplEntry, NULL);
	}

	eError = TLStreamMarkEOS(psImplEntry->pvNative, IMG_FALSE);
	PVR_LOG_GOTO_IF_ERROR(eError, "TLStreamMarkEOS", return_error_);

	return iRet;

return_error_:
	return -1;
}

static IMG_INT64 _ReadRndAccess(DIIB_ENTRY *psEntry, IMG_UINT64 ui64Count,
                                IMG_UINT64 *pui64Pos, void *pvData)
{
	PVRSRV_ERROR eError;
	IMG_UINT8 *pui8Buffer;
	IMG_HANDLE hStream = psEntry->sImplEntry.pvNative;

	if (psEntry->sIterCb.pfnRead == NULL)
	{
		return -1;
	}

	eError = TLStreamReserve(hStream, &pui8Buffer, ui64Count);
	PVR_LOG_GOTO_IF_ERROR(eError, "TLStreamReserve", return_error_);

	psEntry->sIterCb.pfnRead((IMG_CHAR *) pui8Buffer, ui64Count, pui64Pos,
	                         pvData);

	eError = TLStreamCommit(hStream, ui64Count);
	PVR_LOG_GOTO_IF_ERROR(eError, "TLStreamCommit", return_error_);

	eError = TLStreamMarkEOS(psEntry->sImplEntry.pvNative, IMG_FALSE);
	PVR_LOG_GOTO_IF_ERROR(eError, "TLStreamMarkEOS", return_error_);

	return 0;

return_error_:
	return -1;
}

static void _WriterThread(void *pvArg)
{
	PVRSRV_ERROR eError;
	IMG_HANDLE hEvent;
	DLLIST_NODE *psNode;

	eError = OSEventObjectOpen(_g_psImpl->hWriterEventObject, &hEvent);
	PVR_LOG_RETURN_VOID_IF_ERROR(eError, "OSEventObjectOpen");

#ifdef PVRSRV_FORCE_UNLOAD_IF_BAD_STATE
	while (PVRSRVGetPVRSRVData()->eServicesState == PVRSRV_SERVICES_STATE_OK &&
	       OSAtomicRead(&_g_psImpl->eThreadState) == THREAD_STATE_ALIVE)
#else
	while (OSAtomicRead(&_g_psImpl->eThreadState) == THREAD_STATE_ALIVE)
#endif
	{
		struct DIIB_WORK_ITEM *psItem = NULL;

		OSLockAcquire(_g_psImpl->psWriterLock);
		/* Get element from list tail so that we always get the oldest element
		 * (elements are added to head). */
		while ((psNode = dllist_get_prev_node(&_g_psImpl->sWriterQueue)) != NULL)
		{
			IMG_INT64 i64Ret;
			DIIB_ENTRY *psEntry;
			OSDI_IMPL_ENTRY *psImplEntry;

			dllist_remove_node(psNode);
			OSLockRelease(_g_psImpl->psWriterLock);

			psItem = IMG_CONTAINER_OF(psNode, struct DIIB_WORK_ITEM,
			                          sQueueElement);

			psEntry = psItem->psEntry;
			psImplEntry = &psItem->psEntry->sImplEntry;

			/* if client has already disconnected we can just drop this item */
			if (psItem->psContext->bClientConnected)
			{

				PVR_ASSERT(psItem->psContext->hStream != NULL);

				psImplEntry->pvNative = psItem->psContext->hStream;

				if (psEntry->eType == DI_ENTRY_TYPE_GENERIC)
				{
					i64Ret = _ReadGeneric(psItem->psContext, psEntry);
					PVR_LOG_IF_FALSE(i64Ret >= 0, "generic access read operation "
					                 "failed");
				}
				else if (psEntry->eType == DI_ENTRY_TYPE_RANDOM_ACCESS)
				{
					IMG_UINT64 ui64Pos = psItem->ui64Offset;

					i64Ret = _ReadRndAccess(psEntry, psItem->ui64Size, &ui64Pos,
					                        psEntry->pvPrivData);
					PVR_LOG_IF_FALSE(i64Ret >= 0, "random access read operation "
					                 "failed");
				}
				else
				{
					PVR_ASSERT(psEntry->eType == DI_ENTRY_TYPE_GENERIC ||
					           psEntry->eType == DI_ENTRY_TYPE_RANDOM_ACCESS);
				}

				psImplEntry->pvNative = NULL;
			}
			else
			{
				PVR_DPF((PVR_DBG_MESSAGE, "client reading entry \"%s\" has "
				        "disconnected", psEntry->pszFullPath));
			}

			_ContextUnrefAndMaybeDestroy(psItem->psContext);
			OSFreeMemNoStats(psItem);

			OSLockAcquire(_g_psImpl->psWriterLock);
		}
		OSLockRelease(_g_psImpl->psWriterLock);

		eError = OSEventObjectWaitKernel(hEvent, WRITER_THREAD_SLEEP_TIMEOUT);
		if (eError != PVRSRV_OK && eError != PVRSRV_ERROR_TIMEOUT)
		{
			PVR_LOG_ERROR(eError, "OSEventObjectWaitKernel");
		}
	}

	OSLockAcquire(_g_psImpl->psWriterLock);
	/* clear the queue if there are any items pending */
	while ((psNode = dllist_get_prev_node(&_g_psImpl->sWriterQueue)) != NULL)
	{
		struct DIIB_WORK_ITEM *psItem = IMG_CONTAINER_OF(psNode,
		                                                 struct DIIB_WORK_ITEM,
		                                                 sQueueElement);

		dllist_remove_node(psNode);
		_ContextUnrefAndMaybeDestroy(psItem->psContext);
		OSFreeMem(psItem);
	}
	OSLockRelease(_g_psImpl->psWriterLock);

	eError = OSEventObjectClose(hEvent);
	PVR_LOG_IF_ERROR(eError, "OSEventObjectClose");

	OSAtomicWrite(&_g_psImpl->eThreadState, THREAD_STATE_TERMINATED);
}

/* ----- DI internal API ---------------------------------------------------- */

DIIB_ENTRY *DIImplBrgFind(const IMG_CHAR *pszPath)
{
	DIIB_ENTRY *psEntry;

	OSLockAcquire(_g_psImpl->psEntriesLock);
	psEntry = (void *) HASH_Retrieve_Extended(_g_psImpl->psEntriesTable,
	                                          (IMG_CHAR *) pszPath);
	OSLockRelease(_g_psImpl->psEntriesLock);

	return psEntry;
}

/* ----- DI bridge interface ------------------------------------------------ */

static PVRSRV_ERROR _CreateStream(IMG_CHAR *pszStreamName, IMG_HANDLE *phStream)
{
	IMG_UINT32 iRet;
	IMG_HANDLE hStream;
	PVRSRV_ERROR eError;

	/* for now only one stream can be created. Should we be able to create
	 * per context stream? */
	iRet = OSSNPrintf(pszStreamName, PRVSRVTL_MAX_STREAM_NAME_SIZE,
	                  "di_stream_%x", OSGetCurrentClientProcessIDKM());
	if (iRet >= PRVSRVTL_MAX_STREAM_NAME_SIZE)
	{
		/* this check is superfluous because it can never happen but in case
		 * someone changes the definition of PRVSRVTL_MAX_STREAM_NAME_SIZE
		 * handle this case */
		pszStreamName[0] = '\0';
		return PVRSRV_ERROR_INTERNAL_ERROR;
	}

	eError = TLStreamCreate(&hStream, pszStreamName, STREAM_BUFFER_SIZE,
	                        TL_OPMODE_DROP_NEWER, NULL, NULL, NULL, NULL);
	PVR_RETURN_IF_ERROR(eError);

	*phStream = hStream;

	return PVRSRV_OK;
}

PVRSRV_ERROR DICreateContextKM(IMG_CHAR *pszStreamName, DI_CONTEXT **ppsContext)
{
	PVRSRV_ERROR eError;
	DI_CONTEXT *psContext;
	IMG_HANDLE hStream = NULL;
	THREAD_STATE eTState;

	PVR_LOG_RETURN_IF_INVALID_PARAM(ppsContext != NULL, "ppsContext");
	PVR_LOG_RETURN_IF_INVALID_PARAM(pszStreamName != NULL, "pszStreamName");

	psContext = OSAllocMem(sizeof(*psContext));
	PVR_LOG_GOTO_IF_NOMEM(psContext, eError, return_);

	eError = _CreateStream(pszStreamName, &hStream);
	PVR_LOG_GOTO_IF_ERROR(eError, "_CreateStream", free_desc_);

	psContext->hStream = hStream;
	/* indicated to the write thread if the client is still connected and
	 * waiting for the data */
	psContext->bClientConnected = IMG_TRUE;
	OSAtomicWrite(&psContext->iRefCnt, 1);

	eTState = OSAtomicCompareExchange(&_g_psImpl->eThreadState,
	                                  THREAD_STATE_NULL,
	                                  THREAD_STATE_ALIVE);

	/* if the thread has not been started yet do it */
	if (eTState == THREAD_STATE_NULL)
	{
		PVR_ASSERT(_g_psImpl->hWriterThread == NULL);

		eError = OSThreadCreate(&_g_psImpl->hWriterThread, "di_writer",
		                        _WriterThread, NULL, IMG_FALSE, NULL);
		PVR_LOG_GOTO_IF_ERROR(eError, "OSThreadCreate", free_close_stream_);
	}

	*ppsContext = psContext;

	return PVRSRV_OK;

free_close_stream_:
	TLStreamClose(psContext->hStream);
	OSAtomicWrite(&_g_psImpl->eThreadState, THREAD_STATE_TERMINATED);
free_desc_:
	OSFreeMem(psContext);
return_:
	return eError;
}

PVRSRV_ERROR DIDestroyContextKM(DI_CONTEXT *psContext)
{
	PVR_LOG_RETURN_IF_INVALID_PARAM(psContext != NULL, "psContext");

	/* pass the information to the write thread that the client has
	 * disconnected */
	psContext->bClientConnected = IMG_FALSE;

	return _ContextUnrefAndMaybeDestroy(psContext);
}

PVRSRV_ERROR DIReadEntryKM(DI_CONTEXT *psContext, const IMG_CHAR *pszEntryPath,
                           IMG_UINT64 ui64Offset, IMG_UINT64 ui64Size)
{
	PVRSRV_ERROR eError;
	struct DIIB_WORK_ITEM *psItem;

	PVR_LOG_RETURN_IF_INVALID_PARAM(psContext != NULL, "psContext");
	PVR_LOG_RETURN_IF_INVALID_PARAM(pszEntryPath != NULL, "pszEntryPath");

	/* 'no stats' to avoid acquiring the process stats locks */
	psItem = OSAllocMemNoStats(sizeof(*psItem));
	PVR_LOG_GOTO_IF_NOMEM(psItem, eError, return_);

	psItem->psContext = psContext;
	psItem->psEntry = DIImplBrgFind(pszEntryPath);
	PVR_LOG_GOTO_IF_FALSE_VA(psItem->psEntry != NULL, free_item_,
	                         "entry %s does not exist", pszEntryPath);
	psItem->ui64Size = ui64Size;
	psItem->ui64Offset = ui64Offset;

	/* increment ref count on the context so that it doesn't get freed
	 * before it gets processed by the writer thread. */
	OSAtomicIncrement(&psContext->iRefCnt);

	OSLockAcquire(_g_psImpl->psWriterLock);
	dllist_add_to_head(&_g_psImpl->sWriterQueue, &psItem->sQueueElement);
	OSLockRelease(_g_psImpl->psWriterLock);

	eError = OSEventObjectSignal(_g_psImpl->hWriterEventObject);
	PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");

	return PVRSRV_OK;

free_item_:
	eError = PVRSRV_ERROR_NOT_FOUND;
	OSFreeMemNoStats(psItem);
return_:
	return eError;
}

PVRSRV_ERROR DIWriteEntryKM(DI_CONTEXT *psContext, const IMG_CHAR *pszEntryPath,
                           IMG_UINT64 ui64ValueSize, const IMG_CHAR *pszValue)
{
	DIIB_ENTRY *psEntry;
	DI_PFN_WRITE pfnEntryPuts;
	IMG_INT64 i64Length = 0;

	PVR_LOG_RETURN_IF_INVALID_PARAM(psContext != NULL, "psContext");
	PVR_LOG_RETURN_IF_INVALID_PARAM(pszEntryPath != NULL, "pszEntryPath");
	PVR_LOG_RETURN_IF_INVALID_PARAM(pszValue != NULL, "pszValue");

	psEntry = DIImplBrgFind(pszEntryPath);
	PVR_LOG_RETURN_IF_FALSE_VA(psEntry != NULL, PVRSRV_ERROR_NOT_FOUND,
	                         "entry %s does not exist", pszEntryPath);

	pfnEntryPuts = psEntry->sIterCb.pfnWrite;
	if (pfnEntryPuts != NULL)
	{
		i64Length = pfnEntryPuts(pszValue, ui64ValueSize, (IMG_UINT64*)&i64Length, psEntry->pvPrivData);

		/* To deal with -EINVAL being returned */
		PVR_LOG_RETURN_IF_INVALID_PARAM(i64Length >= 0, pszValue);
	}
	else
	{
		PVR_LOG_MSG(PVR_DBG_WARNING, "Unable to write to Entry. Write callback not enabled");
		return PVRSRV_ERROR_INVALID_REQUEST;
	}
	return PVRSRV_OK;
}

static PVRSRV_ERROR _listName(uintptr_t k,
                               uintptr_t v,
                               void* hStream)
{
	PVRSRV_ERROR eError;
	DIIB_ENTRY *psEntry;
	IMG_UINT32 ui32Size;
	IMG_CHAR aszName[DI_IMPL_BRG_PATH_LEN];

	psEntry = (DIIB_ENTRY*) v;
	PVR_ASSERT(psEntry != NULL);
	PVR_UNREFERENCED_PARAMETER(k);

	ui32Size = OSSNPrintf(aszName, DI_IMPL_BRG_PATH_LEN, "%s\n", psEntry->pszFullPath);
	PVR_LOG_IF_FALSE(ui32Size > 5, "ui32Size too small, Error suspected!");
	eError = TLStreamWrite(hStream, (IMG_UINT8 *)aszName, ui32Size+1);

	return eError;
}


PVRSRV_ERROR DIListAllEntriesKM(DI_CONTEXT *psContext)
{
	PVRSRV_ERROR eError;

	PVR_LOG_RETURN_IF_INVALID_PARAM(psContext != NULL, "psContext");

	eError = HASH_Iterate(_g_psImpl->psEntriesTable, _listName, psContext->hStream);
	PVR_LOG_IF_ERROR(eError, "HASH_Iterate_Extended");

	eError = TLStreamMarkEOS(psContext->hStream, IMG_FALSE);
	return eError;
}

/* ----- DI implementation interface ---------------------------------------- */

static PVRSRV_ERROR _Init(void)
{
	PVRSRV_ERROR eError;

	_g_psImpl = OSAllocMem(sizeof(*_g_psImpl));
	PVR_LOG_GOTO_IF_NOMEM(_g_psImpl, eError, return_);

	_g_psImpl->psEntriesTable = HASH_Create_Extended(ENTRIES_TABLE_INIT_SIZE,
	                                                 DI_IMPL_BRG_PATH_LEN,
	                                                 _Hash, _Compare);
	PVR_LOG_GOTO_IF_NOMEM(_g_psImpl->psEntriesTable, eError, free_impl_);

	eError = OSLockCreate(&_g_psImpl->psEntriesLock);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSCreateLock", free_table_);

	eError = OSLockCreate(&_g_psImpl->psWriterLock);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSCreateLock", free_entries_lock_);

	eError = OSEventObjectCreate("DI_WRITER_EO",
	                             &_g_psImpl->hWriterEventObject);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSEventObjectCreate", free_writer_lock_);

	_g_psImpl->hWriterThread = NULL;
	OSAtomicWrite(&_g_psImpl->eThreadState, THREAD_STATE_NULL);

	dllist_init(&_g_psImpl->sWriterQueue);

	return PVRSRV_OK;

free_writer_lock_:
	OSLockDestroy(_g_psImpl->psWriterLock);
free_entries_lock_:
	OSLockDestroy(_g_psImpl->psEntriesLock);
free_table_:
	HASH_Delete_Extended(_g_psImpl->psEntriesTable, IMG_FALSE);
free_impl_:
	OSFreeMem(_g_psImpl);
	_g_psImpl = NULL;
return_:
	return eError;
}

static void _DeInit(void)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	THREAD_STATE eTState;

	eTState = OSAtomicCompareExchange(&_g_psImpl->eThreadState,
	                                  THREAD_STATE_ALIVE,
	                                  THREAD_STATE_TERMINATED);

	if (eTState == THREAD_STATE_ALIVE)
	{
		if (_g_psImpl->hWriterEventObject != NULL)
		{
			eError = OSEventObjectSignal(_g_psImpl->hWriterEventObject);
			PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");
		}

		LOOP_UNTIL_TIMEOUT(WRITER_THREAD_DESTROY_TIMEOUT)
		{
			eError = OSThreadDestroy(_g_psImpl->hWriterThread);
			if (eError == PVRSRV_OK)
			{
				break;
			}
			OSWaitus(WRITER_THREAD_DESTROY_TIMEOUT/WRITER_THREAD_DESTROY_RETRIES);
		} END_LOOP_UNTIL_TIMEOUT();

		PVR_LOG_IF_ERROR(eError, "OSThreadDestroy");
	}

	if (_g_psImpl->hWriterEventObject != NULL)
	{
		eError = OSEventObjectDestroy(_g_psImpl->hWriterEventObject);
		PVR_LOG_IF_ERROR(eError, "OSEventObjectDestroy");
	}

	HASH_Delete_Extended(_g_psImpl->psEntriesTable, IMG_FALSE);
	OSLockDestroy(_g_psImpl->psWriterLock);
	OSLockDestroy(_g_psImpl->psEntriesLock);
	OSFreeMem(_g_psImpl);
	_g_psImpl = NULL;
}

/* Recursively traverses the ancestors list up to the root group and
 * appends their names preceded by "/" to the path in reverse order
 * (root group's name first and psGroup group's name last).
 * Returns current offset in the path (the current path length without the
 * NUL character). If there is no more space in the path returns -1
 * to indicate an error (the path is too long to fit into the buffer). */
static IMG_INT _BuildGroupPath(IMG_CHAR *pszPath, const DIIB_GROUP *psGroup)
{
	IMG_INT iOff;

	if (psGroup == NULL)
	{
		return 0;
	}

	PVR_ASSERT(pszPath != NULL);

	iOff = _BuildGroupPath(pszPath, psGroup->psParentGroup);
	PVR_RETURN_IF_FALSE(iOff != -1, -1);

	iOff += OSStringLCopy(pszPath + iOff, "/",
	                      DI_IMPL_BRG_PATH_LEN - iOff);
	PVR_RETURN_IF_FALSE(iOff < DI_IMPL_BRG_PATH_LEN, -1);

	iOff += OSStringLCopy(pszPath + iOff, psGroup->pszName,
	                      DI_IMPL_BRG_PATH_LEN - iOff);
	PVR_RETURN_IF_FALSE(iOff < DI_IMPL_BRG_PATH_LEN, -1);

	return iOff;
}

static PVRSRV_ERROR _BuildEntryPath(IMG_CHAR *pszPath, const IMG_CHAR *pszName,
                                    const DIIB_GROUP *psGroup)
{
	IMG_INT iOff = _BuildGroupPath(pszPath, psGroup);
	PVR_RETURN_IF_FALSE(iOff != -1, PVRSRV_ERROR_INVALID_OFFSET);

	iOff += OSStringLCopy(pszPath + iOff, "/", DI_IMPL_BRG_PATH_LEN - iOff);
	PVR_RETURN_IF_FALSE(iOff < DI_IMPL_BRG_PATH_LEN,
	                    PVRSRV_ERROR_INVALID_OFFSET);

	iOff += OSStringLCopy(pszPath + iOff, pszName, DI_IMPL_BRG_PATH_LEN - iOff);
	PVR_RETURN_IF_FALSE(iOff < DI_IMPL_BRG_PATH_LEN,
	                    PVRSRV_ERROR_INVALID_OFFSET);

	return PVRSRV_OK;
}

static PVRSRV_ERROR _CreateEntry(const IMG_CHAR *pszName,
                                 DI_ENTRY_TYPE eType,
                                 const DI_ITERATOR_CB *psIterCb,
                                 void *pvPrivData,
                                 void *pvParentGroup,
                                 void **pvEntry)
{
	DIIB_GROUP *psParentGroup = pvParentGroup;
	DIIB_ENTRY *psEntry;
	PVRSRV_ERROR eError;

	PVR_LOG_RETURN_IF_INVALID_PARAM(pvEntry != NULL, "pvEntry");
	PVR_LOG_RETURN_IF_INVALID_PARAM(pvParentGroup != NULL, "pvParentGroup");

	switch (eType)
	{
		case DI_ENTRY_TYPE_GENERIC:
			break;
		case DI_ENTRY_TYPE_RANDOM_ACCESS:
			break;
		default:
			PVR_DPF((PVR_DBG_ERROR, "eType invalid in %s()", __func__));
			PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_INVALID_PARAMS, return_);
	}

	psEntry = OSAllocMem(sizeof(*psEntry));
	PVR_LOG_GOTO_IF_NOMEM(psEntry, eError, return_);

	eError = OSLockCreate(&psEntry->hLock);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSLockCreate", free_entry_);

	psEntry->eType = eType;
	psEntry->sIterCb = *psIterCb;
	psEntry->pvPrivData = pvPrivData;
	psEntry->psParentGroup = psParentGroup;
	psEntry->pszFullPath[0] = '\0';

	psEntry->sImplEntry.pvPrivData = pvPrivData;
	psEntry->sImplEntry.pvNative = NULL;
	psEntry->sImplEntry.psCb = &_g_sEntryCallbacks;

	eError = _BuildEntryPath(psEntry->pszFullPath, pszName,
	                         psEntry->psParentGroup);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s() failed in _BuildEntryPath() for \"%s\" "
		        "entry", __func__, pszName));
		goto destroy_lock_;
	}

	OSLockAcquire(_g_psImpl->psEntriesLock);
	eError = HASH_Insert_Extended(_g_psImpl->psEntriesTable,
	                              psEntry->pszFullPath,
	                              (uintptr_t) psEntry) ?
	         PVRSRV_OK : PVRSRV_ERROR_UNABLE_TO_ADD_HANDLE;
	OSLockRelease(_g_psImpl->psEntriesLock);
	PVR_LOG_GOTO_IF_ERROR(eError, "HASH_Insert_Extended failed", destroy_lock_);

	*pvEntry = psEntry;

	return PVRSRV_OK;

destroy_lock_:
	OSLockDestroy(psEntry->hLock);
free_entry_:
	OSFreeMem(psEntry);
return_:
	return eError;
}

static void _DestroyEntry(void *pvEntry)
{
	DIIB_ENTRY *psEntry = pvEntry;
	PVR_ASSERT(psEntry != NULL);

	OSLockAcquire(_g_psImpl->psEntriesLock);
	HASH_Remove_Extended(_g_psImpl->psEntriesTable, psEntry->pszFullPath);
	OSLockRelease(_g_psImpl->psEntriesLock);

	OSLockDestroy(psEntry->hLock);
	OSFreeMem(psEntry);
}

static PVRSRV_ERROR _CreateGroup(const IMG_CHAR *pszName,
                                 void *pvParentGroup,
                                 void **ppvGroup)
{
	DIIB_GROUP *psNewGroup;

	PVR_LOG_RETURN_IF_INVALID_PARAM(pszName != NULL, "pszName");
	PVR_LOG_RETURN_IF_INVALID_PARAM(ppvGroup != NULL, "ppvGroup");

	psNewGroup = OSAllocMem(sizeof(*psNewGroup));
	PVR_LOG_RETURN_IF_NOMEM(psNewGroup, "OSAllocMem");

	psNewGroup->pszName = pszName;
	psNewGroup->psParentGroup = pvParentGroup;

	*ppvGroup = psNewGroup;

	return PVRSRV_OK;
}

static void _DestroyGroup(void *pvGroup)
{
	DIIB_GROUP *psGroup = pvGroup;
	PVR_ASSERT(psGroup != NULL);

	OSFreeMem(psGroup);
}

PVRSRV_ERROR PVRDIImplBrgRegister(void)
{
	OSDI_IMPL_CB sImplCb = {
		.pfnInit = _Init,
		.pfnDeInit = _DeInit,
		.pfnCreateEntry = _CreateEntry,
		.pfnDestroyEntry = _DestroyEntry,
		.pfnCreateGroup = _CreateGroup,
		.pfnDestroyGroup = _DestroyGroup
	};

	return DIRegisterImplementation("impl_brg", &sImplCb);
}
