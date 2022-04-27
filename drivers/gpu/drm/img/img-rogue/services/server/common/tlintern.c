/*************************************************************************/ /*!
@File
@Title          Transport Layer kernel side API implementation.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Transport Layer functions available to driver components in
                the driver.
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

#include "allocmem.h"
#include "pvrsrv_error.h"
#include "osfunc.h"
#include "devicemem.h"

#include "pvrsrv_tlcommon.h"
#include "tlintern.h"

/*
 * Make functions
 */
PTL_STREAM_DESC
TLMakeStreamDesc(PTL_SNODE f1, IMG_UINT32 f2, IMG_HANDLE f3)
{
	PTL_STREAM_DESC ps = OSAllocZMem(sizeof(TL_STREAM_DESC));
	if (ps == NULL)
	{
		return NULL;
	}
	ps->psNode = f1;
	ps->ui32Flags = f2;
	ps->hReadEvent = f3;
	ps->uiRefCount = 1;

	if (f2 & PVRSRV_STREAM_FLAG_READ_LIMIT)
	{
		ps->ui32ReadLimit = f1->psStream->ui32Write;
	}
	return ps;
}

PTL_SNODE
TLMakeSNode(IMG_HANDLE f2, TL_STREAM *f3, TL_STREAM_DESC *f4)
{
	PTL_SNODE ps = OSAllocZMem(sizeof(TL_SNODE));
	if (ps == NULL)
	{
		return NULL;
	}
	ps->hReadEventObj = f2;
	ps->psStream = f3;
	ps->psRDesc = f4;
	f3->psNode = ps;
	return ps;
}

/*
 * Transport Layer Global top variables and functions
 */
static TL_GLOBAL_DATA sTLGlobalData;

TL_GLOBAL_DATA *TLGGD(void) /* TLGetGlobalData() */
{
	return &sTLGlobalData;
}

/* TLInit must only be called once at driver initialisation.
 * An assert is provided to check this condition on debug builds.
 */
PVRSRV_ERROR
TLInit(void)
{
	PVRSRV_ERROR eError;

	PVR_DPF_ENTERED;

	PVR_ASSERT(sTLGlobalData.hTLGDLock == NULL && sTLGlobalData.hTLEventObj == NULL);

	/* Allocate a lock for TL global data, to be used while updating the TL data.
	 * This is for making TL global data multi-thread safe */
	eError = OSLockCreate(&sTLGlobalData.hTLGDLock);
	PVR_GOTO_IF_ERROR(eError, e0);

	/* Allocate the event object used to signal global TL events such as
	 * a new stream created */
	eError = OSEventObjectCreate("TLGlobalEventObj", &sTLGlobalData.hTLEventObj);
	PVR_GOTO_IF_ERROR(eError, e1);

	PVR_DPF_RETURN_OK;

/* Don't allow the driver to start up on error */
e1:
	OSLockDestroy (sTLGlobalData.hTLGDLock);
	sTLGlobalData.hTLGDLock = NULL;
e0:
	PVR_DPF_RETURN_RC (eError);
}

static void RemoveAndFreeStreamNode(PTL_SNODE psRemove)
{
	TL_GLOBAL_DATA*  psGD = TLGGD();
	PTL_SNODE*       last;
	PTL_SNODE        psn;
	PVRSRV_ERROR     eError;

	PVR_DPF_ENTERED;

	/* Unlink the stream node from the master list */
	PVR_ASSERT(psGD->psHead);
	last = &psGD->psHead;
	for (psn = psGD->psHead; psn; psn=psn->psNext)
	{
		if (psn == psRemove)
		{
			/* Other calling code may have freed and zeroed the pointers */
			if (psn->psRDesc)
			{
				OSFreeMem(psn->psRDesc);
				psn->psRDesc = NULL;
			}
			if (psn->psStream)
			{
				OSFreeMem(psn->psStream);
				psn->psStream = NULL;
			}
			*last = psn->psNext;
			break;
		}
		last = &psn->psNext;
	}

	/* Release the event list object owned by the stream node */
	if (psRemove->hReadEventObj)
	{
		eError = OSEventObjectDestroy(psRemove->hReadEventObj);
		PVR_LOG_IF_ERROR(eError, "OSEventObjectDestroy");

		psRemove->hReadEventObj = NULL;
	}

	/* Release the memory of the stream node */
	OSFreeMem(psRemove);

	PVR_DPF_RETURN;
}

static void FreeGlobalData(void)
{
	PTL_SNODE psCurrent = sTLGlobalData.psHead;
	PTL_SNODE psNext;
	PVRSRV_ERROR eError;

	PVR_DPF_ENTERED;

	/* Clean up the SNODE list */
	if (psCurrent)
	{
		while (psCurrent)
		{
			psNext = psCurrent->psNext;

			/* Other calling code may have freed and zeroed the pointers */
			if (psCurrent->psRDesc)
			{
				OSFreeMem(psCurrent->psRDesc);
				psCurrent->psRDesc = NULL;
			}
			if (psCurrent->psStream)
			{
				OSFreeMem(psCurrent->psStream);
				psCurrent->psStream = NULL;
			}

			/* Release the event list object owned by the stream node */
			if (psCurrent->hReadEventObj)
			{
				eError = OSEventObjectDestroy(psCurrent->hReadEventObj);
				PVR_LOG_IF_ERROR(eError, "OSEventObjectDestroy");

				psCurrent->hReadEventObj = NULL;
			}

			OSFreeMem(psCurrent);
			psCurrent = psNext;
		}

		sTLGlobalData.psHead = NULL;
	}

	PVR_DPF_RETURN;
}

void
TLDeInit(void)
{
	PVR_DPF_ENTERED;

	if (sTLGlobalData.uiClientCnt)
	{
		PVR_DPF((PVR_DBG_ERROR, "TLDeInit transport layer but %d client streams are still connected", sTLGlobalData.uiClientCnt));
		sTLGlobalData.uiClientCnt = 0;
	}

	FreeGlobalData();

	/* Clean up the TL global event object */
	if (sTLGlobalData.hTLEventObj)
	{
		OSEventObjectDestroy(sTLGlobalData.hTLEventObj);
		sTLGlobalData.hTLEventObj = NULL;
	}

	/* Destroy the TL global data lock */
	if (sTLGlobalData.hTLGDLock)
	{
		OSLockDestroy (sTLGlobalData.hTLGDLock);
		sTLGlobalData.hTLGDLock = NULL;
	}

	PVR_DPF_RETURN;
}

void TLAddStreamNode(PTL_SNODE psAdd)
{
	PVR_DPF_ENTERED;

	PVR_ASSERT(psAdd);
	psAdd->psNext = TLGGD()->psHead;
	TLGGD()->psHead = psAdd;

	PVR_DPF_RETURN;
}

PTL_SNODE TLFindStreamNodeByName(const IMG_CHAR *pszName)
{
	TL_GLOBAL_DATA* psGD = TLGGD();
	PTL_SNODE psn;

	PVR_DPF_ENTERED;

	PVR_ASSERT(pszName);

	for (psn = psGD->psHead; psn; psn=psn->psNext)
	{
		if (psn->psStream && OSStringNCompare(psn->psStream->szName, pszName, PRVSRVTL_MAX_STREAM_NAME_SIZE)==0)
		{
			PVR_DPF_RETURN_VAL(psn);
		}
	}

	PVR_DPF_RETURN_VAL(NULL);
}

PTL_SNODE TLFindStreamNodeByDesc(PTL_STREAM_DESC psDesc)
{
	TL_GLOBAL_DATA* psGD = TLGGD();
	PTL_SNODE psn;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psDesc);

	for (psn = psGD->psHead; psn; psn=psn->psNext)
	{
		if (psn->psRDesc == psDesc || psn->psWDesc == psDesc)
		{
			PVR_DPF_RETURN_VAL(psn);
		}
	}
	PVR_DPF_RETURN_VAL(NULL);
}

static inline IMG_BOOL IsDigit(IMG_CHAR c)
{
	return c >= '0' && c <= '9';
}

static inline IMG_BOOL ReadNumber(const IMG_CHAR *pszBuffer,
                                  IMG_UINT32 *pui32Number)
{
	IMG_CHAR acTmp[11] = {0}; /* max 10 digits */
	IMG_UINT32 ui32Result;
	IMG_UINT i;

	for (i = 0; i < sizeof(acTmp) - 1; i++)
	{
		if (!IsDigit(*pszBuffer))
			break;
		acTmp[i] = *pszBuffer++;
	}

	/* if there are no digits or there is something after the number */
	if (i == 0 || *pszBuffer != '\0')
		return IMG_FALSE;

	if (OSStringToUINT32(acTmp, 10, &ui32Result) != PVRSRV_OK)
		return IMG_FALSE;

	*pui32Number = ui32Result;

	return IMG_TRUE;
}

IMG_UINT32 TLDiscoverStreamNodes(const IMG_CHAR *pszNamePattern,
                          IMG_CHAR aaszStreams[][PRVSRVTL_MAX_STREAM_NAME_SIZE],
                          IMG_UINT32 ui32Max)
{
	TL_GLOBAL_DATA *psGD = TLGGD();
	PTL_SNODE psn;
	IMG_UINT32 ui32Count = 0;
	size_t uiLen;

	PVR_ASSERT(pszNamePattern);

	if ((uiLen = OSStringLength(pszNamePattern)) == 0)
		return 0;

	for (psn = psGD->psHead; psn; psn = psn->psNext)
	{
		if (OSStringNCompare(pszNamePattern, psn->psStream->szName, uiLen) != 0)
			continue;

		/* If aaszStreams is NULL we only count how many string match
		 * the given pattern. If it's a valid pointer we also return
		 * the names. */
		if (aaszStreams != NULL)
		{
			if (ui32Count >= ui32Max)
				break;

			/* all of names are shorter than MAX and null terminated */
			OSStringLCopy(aaszStreams[ui32Count], psn->psStream->szName,
			              PRVSRVTL_MAX_STREAM_NAME_SIZE);
		}

		ui32Count++;
	}

	return ui32Count;
}

PTL_SNODE TLFindAndGetStreamNodeByDesc(PTL_STREAM_DESC psDesc)
{
	PTL_SNODE psn;

	PVR_DPF_ENTERED;

	psn = TLFindStreamNodeByDesc(psDesc);
	if (psn == NULL)
		PVR_DPF_RETURN_VAL(NULL);

	PVR_ASSERT(psDesc == psn->psWDesc);

	psn->uiWRefCount++;
	psDesc->uiRefCount++;

	PVR_DPF_RETURN_VAL(psn);
}

void TLReturnStreamNode(PTL_SNODE psNode)
{
	psNode->uiWRefCount--;
	psNode->psWDesc->uiRefCount--;

	PVR_ASSERT(psNode->uiWRefCount > 0);
	PVR_ASSERT(psNode->psWDesc->uiRefCount > 0);
}

IMG_BOOL TLTryRemoveStreamAndFreeStreamNode(PTL_SNODE psRemove)
{
	PVR_DPF_ENTERED;

	PVR_ASSERT(psRemove);

	/* If there is a client connected to this stream, defer stream's deletion */
	if (psRemove->psRDesc != NULL || psRemove->psWDesc != NULL)
	{
		PVR_DPF_RETURN_VAL(IMG_FALSE);
	}

	/* Remove stream from TL_GLOBAL_DATA's list and free stream node */
	psRemove->psStream = NULL;
	RemoveAndFreeStreamNode(psRemove);

	PVR_DPF_RETURN_VAL(IMG_TRUE);
}

IMG_BOOL TLUnrefDescAndTryFreeStreamNode(PTL_SNODE psNodeToRemove,
                                          PTL_STREAM_DESC psSD)
{
	PVR_DPF_ENTERED;

	PVR_ASSERT(psNodeToRemove);
	PVR_ASSERT(psSD);

	/* Decrement reference count. For descriptor obtained by reader it must
	 * reach 0 (only single reader allowed) and for descriptors obtained by
	 * writers it must reach value greater or equal to 0 (multiple writers
	 * model). */
	psSD->uiRefCount--;

	if (psSD == psNodeToRemove->psRDesc)
	{
		PVR_ASSERT(0 == psSD->uiRefCount);
		/* Remove stream descriptor (i.e. stream reader context) */
		psNodeToRemove->psRDesc = NULL;
	}
	else if (psSD == psNodeToRemove->psWDesc)
	{
		PVR_ASSERT(0 <= psSD->uiRefCount);

		psNodeToRemove->uiWRefCount--;

		/* Remove stream descriptor if reference == 0 */
		if (0 == psSD->uiRefCount)
		{
			psNodeToRemove->psWDesc = NULL;
		}
	}

	/* Do not Free Stream Node if there is a write reference (a producer
	 * context) to the stream */
	if (NULL != psNodeToRemove->psRDesc || NULL != psNodeToRemove->psWDesc ||
	    0 != psNodeToRemove->uiWRefCount)
	{
		PVR_DPF_RETURN_VAL(IMG_FALSE);
	}

	/* Make stream pointer NULL to prevent it from being destroyed in
	 * RemoveAndFreeStreamNode. Cleanup of stream should be done by the
	 * calling context */
	psNodeToRemove->psStream = NULL;
	RemoveAndFreeStreamNode(psNodeToRemove);

	PVR_DPF_RETURN_VAL(IMG_TRUE);
}
