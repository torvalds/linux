/*************************************************************************/ /*!
@File
@Title		Resource Handle Manager - Generic Back-end
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description	Provide generic resource handle management back-end
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
*/ /***************************************************************************/

#if defined(PVR_SECURE_HANDLES)

#include <stddef.h>

#include "handle_impl.h"
#include "allocmem.h"
#include "osfunc.h"
#include "pvr_debug.h"


/* Valid handles are never NULL. Therefore, this value should never be 0! */
#define HANDLE_OFFSET_FROM_INDEX				1

#if defined(DEBUG)
#define	HANDLE_BLOCK_SHIFT					2
#else
#define	HANDLE_BLOCK_SHIFT					8
#endif

#define	DIVIDE_BY_BLOCK_SIZE(i)					(((IMG_UINT32)(i)) >> HANDLE_BLOCK_SHIFT)
#define	MULTIPLY_BY_BLOCK_SIZE(i)				(((IMG_UINT32)(i)) << HANDLE_BLOCK_SHIFT)

#define HANDLE_BLOCK_SIZE       				MULTIPLY_BY_BLOCK_SIZE(1)
#define	HANDLE_SUB_BLOCK_MASK					(HANDLE_BLOCK_SIZE - 1)
#define	HANDLE_BLOCK_MASK					(~(HANDLE_SUB_BLOCK_MASK))

#define	INDEX_IS_VALID(psBase, i) 				((i) < (psBase)->ui32TotalHandCount)

#define	INDEX_TO_HANDLE(i) 					((IMG_HANDLE)((IMG_UINTPTR_T)(i) + HANDLE_OFFSET_FROM_INDEX))
#define	HANDLE_TO_INDEX(h) 					((IMG_UINT32)((IMG_UINTPTR_T)(h) - HANDLE_OFFSET_FROM_INDEX))

#define	INDEX_TO_BLOCK_INDEX(i)					DIVIDE_BY_BLOCK_SIZE(i)
#define BLOCK_INDEX_TO_INDEX(i)					MULTIPLY_BY_BLOCK_SIZE(i)
#define INDEX_TO_SUB_BLOCK_INDEX(i)				((i) & HANDLE_SUB_BLOCK_MASK)

#define BLOCK_ARRAY_AND_INDEX_TO_HANDLE_BLOCK(psArray, i)	(&((psArray)[INDEX_TO_BLOCK_INDEX(i)]))
#define	BASE_AND_INDEX_TO_HANDLE_BLOCK(psBase, i)		BLOCK_ARRAY_AND_INDEX_TO_HANDLE_BLOCK((psBase)->psHandleBlockArray, i)
#define BASE_TO_TOTAL_INDICES(psBase)				(HANDLE_TO_INDEX((psBase)->ui32MaxHandleValue) + 1)

#define	INDEX_TO_BLOCK_FREE_HAND_COUNT(psBase, i)		(BASE_AND_INDEX_TO_HANDLE_BLOCK(psBase, i)->ui32FreeHandCount)
#define INDEX_TO_HANDLE_DATA(psBase, i)				(BASE_AND_INDEX_TO_HANDLE_BLOCK(psBase, i)->psHandleDataArray + INDEX_TO_SUB_BLOCK_INDEX(i))

#define	ROUND_DOWN_TO_MULTIPLE_OF_BLOCK_SIZE(a)			(HANDLE_BLOCK_MASK & (a))
#define	ROUND_UP_TO_MULTIPLE_OF_BLOCK_SIZE(a)			ROUND_DOWN_TO_MULTIPLE_OF_BLOCK_SIZE((a) + HANDLE_BLOCK_SIZE - 1)

#define INDEX_MIN						0x0u
#define INDEX_MAX						(ROUND_DOWN_TO_MULTIPLE_OF_BLOCK_SIZE(0x7fffffffu) - 1)

#define HANDLE_VALUE_MIN					((IMG_UINT32)(IMG_UINTPTR_T)INDEX_TO_HANDLE(INDEX_MIN))
#define HANDLE_VALUE_MAX					((IMG_UINT32)(IMG_UINTPTR_T)INDEX_TO_HANDLE(INDEX_MAX))

#define HANDLE_BLOCK_ARRAY_SIZE(uiNumHandles)				DIVIDE_BY_BLOCK_SIZE(ROUND_UP_TO_MULTIPLE_OF_BLOCK_SIZE(uiNumHandles))

#define	HANDLE_DATA_TO_HANDLE(psHandleData)			((psHandleData)->hHandle)
#define	HANDLE_DATA_TO_INDEX(psHandleData)			HANDLE_TO_INDEX(HANDLE_DATA_TO_HANDLE(psHandleData))

#if defined(MIN)
#undef MIN
#endif

#define	MIN(x, y)						(((x) < (y)) ? (x) : (y))

typedef struct _HANDLE_IMPL_DATA_
{
	/* Handle which represents this handle structure */
	IMG_HANDLE hHandle;

	/* Pointer to the data that the handle represents */
	IMG_VOID *pvData;

	/*
	 * When handles are on the free list, the value of the "next index
	 * plus one field" has the following meaning:
	 * zero - next handle is the one that follows this one,
	 * nonzero - the index of the next handle is the value minus one.
	 * This scheme means handle space can be initialised to all zeros.
	 *
	 * When this field is used to link together handles on a list
	 * other than the free list, zero indicates the end of the
	 * list, with nonzero the same as above.
	 */
	IMG_UINT32 ui32NextIndexPlusOne;
} HANDLE_IMPL_DATA;

typedef struct _HANDLE_BLOCK_
{
	/* Pointer to an array of handle data structures */
	HANDLE_IMPL_DATA *psHandleDataArray;

	/* Number of free handle data structures in block */
	IMG_UINT32 ui32FreeHandCount;
} HANDLE_BLOCK;

struct _HANDLE_IMPL_BASE_
{
	/* Pointer to array of handle block structures */
	HANDLE_BLOCK *psHandleBlockArray;

	/* Maximum handle value */
	IMG_UINT32 ui32MaxHandleValue;

	/* Total number of handles (this may include allocated but unused handles) */
	IMG_UINT32 ui32TotalHandCount;

	/* Number of free handles */
	IMG_UINT32 ui32TotalFreeHandCount;

	/* Purging enabled.
	 * If purging is enabled, the size of the table can be reduced
	 * by removing free space at the end of the table.  To make
	 * purging more likely to succeed, handles are allocated as
	 * far to the front of the table as possible.  The first free
	 * handle is found by a linear search from the start of the table,
	 * and so no free handle list management is done.
	 */
	IMG_BOOL bPurgingEnabled;

	/*
	 * If purging is not enabled, this is the array index of first free
	 * handle.
	 * If purging is enabled, this is the index to start searching for
	 * a free handle from.  In this case it is usually zero, unless
	 * the handle array size has been increased due to lack of
	 * handles.
	 */
	IMG_UINT32 ui32FirstFreeIndex;

	/*
	 * Index of the last free index, plus one. Not used if purging
	 * is enabled.
	 */
	IMG_UINT32 ui32LastFreeIndexPlusOne;
};


/*!
******************************************************************************

 @Function	ReallocHandleBlockArray

 @Description	Reallocate the handle block array

 @Input		psBase - Pointer to handle base structure
		ui32NewCount - The new total number of handles

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR ReallocHandleBlockArray(HANDLE_IMPL_BASE *psBase, 
					    IMG_UINT32 ui32NewCount)
{
	HANDLE_BLOCK *psOldArray = psBase->psHandleBlockArray;
	IMG_UINT32 ui32OldCount = psBase->ui32TotalHandCount;
	HANDLE_BLOCK *psNewArray = IMG_NULL;
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 ui32Index;

	if (ui32NewCount == ui32OldCount)
	{
		return PVRSRV_OK;
	}

	if (ui32NewCount != 0 && 
	    !psBase->bPurgingEnabled &&
	    ui32NewCount < ui32OldCount)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (((ui32OldCount % HANDLE_BLOCK_SIZE) != 0) ||
	    ((ui32NewCount % HANDLE_BLOCK_SIZE) != 0))
	{
		PVR_ASSERT((ui32OldCount % HANDLE_BLOCK_SIZE) == 0);
		PVR_ASSERT((ui32NewCount % HANDLE_BLOCK_SIZE) == 0);

		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (ui32NewCount != 0)
	{
		/* Allocate new handle array */
		psNewArray = OSAllocMem(HANDLE_BLOCK_ARRAY_SIZE(ui32NewCount) * sizeof(HANDLE_BLOCK));
		if (psNewArray == IMG_NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Couldn't allocate new handle array", __FUNCTION__));
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto error;
		}

		if (ui32OldCount != 0)
		{
			OSMemCopy(psNewArray, psOldArray, HANDLE_BLOCK_ARRAY_SIZE(MIN(ui32NewCount, ui32OldCount)) * sizeof(HANDLE_BLOCK));
		}
	}

	/*
	 * If the new handle array is smaller than the old one, free
	 * unused handle data structure arrays
	 */
	for (ui32Index = ui32NewCount; ui32Index < ui32OldCount; ui32Index += HANDLE_BLOCK_SIZE)
	{
		HANDLE_BLOCK *psHandleBlock = BLOCK_ARRAY_AND_INDEX_TO_HANDLE_BLOCK(psOldArray, ui32Index);

		OSFreeMem(psHandleBlock->psHandleDataArray);
	}

	/*
	 * If the new handle array is bigger than the old one, allocate
	 * new handle data structure arrays
	 */
	for (ui32Index = ui32OldCount; ui32Index < ui32NewCount; ui32Index += HANDLE_BLOCK_SIZE)
	{
		/* PRQA S 0505 1 */ /* psNewArray is never NULL, see assert earlier */
		HANDLE_BLOCK *psHandleBlock = BLOCK_ARRAY_AND_INDEX_TO_HANDLE_BLOCK(psNewArray, ui32Index);

		psHandleBlock->psHandleDataArray = OSAllocMem(sizeof(HANDLE_IMPL_DATA) * HANDLE_BLOCK_SIZE);
		if (psHandleBlock->psHandleDataArray != IMG_NULL)
		{
			IMG_UINT32 ui32SubIndex;

			psHandleBlock->ui32FreeHandCount = HANDLE_BLOCK_SIZE;

			for (ui32SubIndex = 0; ui32SubIndex < HANDLE_BLOCK_SIZE; ui32SubIndex++)
			{
				HANDLE_IMPL_DATA *psHandleData = psHandleBlock->psHandleDataArray + ui32SubIndex;

				psHandleData->hHandle = INDEX_TO_HANDLE(ui32SubIndex + ui32Index);
				psHandleData->pvData = IMG_NULL;
				psHandleData->ui32NextIndexPlusOne = 0;
			}
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Couldn't allocate handle structures", __FUNCTION__));
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		}
	}

	if (eError != PVRSRV_OK)
	{
		goto error;
	}

#if defined(DEBUG_MAX_HANDLE_COUNT)
	/* Force handle failure to test error exit code */
	if (ui32NewCount > DEBUG_MAX_HANDLE_COUNT)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Max handle count (%u) reached", __FUNCTION__, DEBUG_MAX_HANDLE_COUNT));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto error;
	}
#endif /* defined(DEBUG_MAX_HANDLE_COUNT) */

	if (psOldArray != IMG_NULL)
	{
		/* Free old handle array */
		OSFreeMem(psOldArray);
	}

	psBase->psHandleBlockArray = psNewArray;
	psBase->ui32TotalHandCount = ui32NewCount;

	if (ui32NewCount > ui32OldCount)
	{
		/* Check for wraparound */
		PVR_ASSERT(psBase->ui32TotalFreeHandCount + (ui32NewCount - ui32OldCount) > psBase->ui32TotalFreeHandCount);

		/* PRQA S 3382 1 */ /* ui32NewCount always > ui32OldCount */
		psBase->ui32TotalFreeHandCount += (ui32NewCount - ui32OldCount);

		/*
		 * If purging is enabled, there is no free handle list
		 * management, but as an optimization, when allocating
		 * new handles, we use ui32FirstFreeIndex to point to
		 * the first handle in a newly allocated block.
		 */
		if (psBase->ui32FirstFreeIndex == 0)
		{
			PVR_ASSERT(psBase->ui32LastFreeIndexPlusOne == 0);

			psBase->ui32FirstFreeIndex = ui32OldCount;
		}
		else
		{
			if (!psBase->bPurgingEnabled)
			{
				PVR_ASSERT(psBase->ui32LastFreeIndexPlusOne != 0);
				PVR_ASSERT(INDEX_TO_HANDLE_DATA(psBase, psBase->ui32LastFreeIndexPlusOne - 1)->ui32NextIndexPlusOne == 0);

				INDEX_TO_HANDLE_DATA(psBase, psBase->ui32LastFreeIndexPlusOne - 1)->ui32NextIndexPlusOne = ui32OldCount + 1;
			}
		}

		if (!psBase->bPurgingEnabled)
		{
			psBase->ui32LastFreeIndexPlusOne = ui32NewCount;
		}
	}
	else
	{
		if (ui32NewCount == 0)
		{
			psBase->ui32TotalFreeHandCount = 0;
			psBase->ui32FirstFreeIndex = 0;
			psBase->ui32LastFreeIndexPlusOne = 0;
		}
		else
		{
			PVR_ASSERT(psBase->bPurgingEnabled);
			PVR_ASSERT(psBase->ui32FirstFreeIndex <= ui32NewCount);
			PVR_ASSERT(psBase->ui32TotalFreeHandCount - (ui32OldCount - ui32NewCount) < psBase->ui32TotalFreeHandCount);

			/* PRQA S 3382 1 */ /* ui32OldCount always >= ui32NewCount */
			psBase->ui32TotalFreeHandCount -= (ui32OldCount - ui32NewCount);
		}
	}

	PVR_ASSERT(psBase->ui32FirstFreeIndex <= psBase->ui32TotalHandCount);

	return PVRSRV_OK;

error:
	PVR_ASSERT(eError != PVRSRV_OK);

	if (psNewArray != IMG_NULL)
	{
		/* Free any new handle structures that were allocated */
		for (ui32Index = ui32OldCount; ui32Index < ui32NewCount; ui32Index += HANDLE_BLOCK_SIZE)
		{
			HANDLE_BLOCK *psHandleBlock = BLOCK_ARRAY_AND_INDEX_TO_HANDLE_BLOCK(psNewArray, ui32Index);
			if (psHandleBlock->psHandleDataArray != IMG_NULL)
			{
				OSFreeMem(psHandleBlock->psHandleDataArray);
			}
		}

		/* Free new handle array */
		OSFreeMem(psNewArray);
	}

	return eError;
}

/*!
******************************************************************************

 @Function	IncreaseHandleArraySize

 @Description	Allocate some more free handles

 @Input		psBase - pointer to handle base structure
		ui32Delta - number of new handles required

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR IncreaseHandleArraySize(HANDLE_IMPL_BASE *psBase, 
					    IMG_UINT32 ui32Delta)
{
	IMG_UINT32 ui32DeltaAdjusted = ROUND_UP_TO_MULTIPLE_OF_BLOCK_SIZE(ui32Delta);
	IMG_UINT32 ui32NewTotalHandCount = psBase->ui32TotalHandCount + ui32DeltaAdjusted;
	IMG_UINT32 ui32TotalIndices = BASE_TO_TOTAL_INDICES(psBase);

	PVR_ASSERT(ui32Delta != 0);

	/* Check new count against max handle array size and check for wrap around */
	if (ui32NewTotalHandCount > ui32TotalIndices || ui32NewTotalHandCount <= psBase->ui32TotalHandCount)
	{
		ui32NewTotalHandCount = ui32TotalIndices;

		ui32DeltaAdjusted = ui32NewTotalHandCount - psBase->ui32TotalHandCount;

		if (ui32DeltaAdjusted < ui32Delta)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Maximum handle limit reached (%u)", 
				 __FUNCTION__, psBase->ui32MaxHandleValue));
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}
	}

	PVR_ASSERT(ui32DeltaAdjusted >= ui32Delta);

	/* Realloc handle pointer array */
	return ReallocHandleBlockArray(psBase, ui32NewTotalHandCount);
}

/*!
******************************************************************************

 @Function	EnsureFreeHandles

 @Description	Ensure there are enough free handles

 @Input		psBase - Pointer to handle base structure
		ui32Free - Number of free handles required

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR EnsureFreeHandles(HANDLE_IMPL_BASE *psBase, 
				      IMG_UINT32 ui32Free)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (ui32Free > psBase->ui32TotalFreeHandCount)
	{
		IMG_UINT32 ui32FreeHandDelta = ui32Free - psBase->ui32TotalFreeHandCount;

		eError = IncreaseHandleArraySize(psBase, ui32FreeHandDelta);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Couldn't allocate %u handles to ensure %u free "
				 "handles (IncreaseHandleArraySize failed with error %s)",
				 __FUNCTION__, ui32FreeHandDelta, ui32Free, PVRSRVGetErrorStringKM(eError)));
		}
	}

	return eError;
}

/*!
******************************************************************************

 @Function	AcquireHandle

 @Description	Acquire a new handle

 @Input		psBase - Pointer to handle base structure
		phHandle - Points to a handle pointer
		pvData - Pointer to resource to be associated with the handle

 @Output	phHandle - Points to a handle pointer

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR AcquireHandle(HANDLE_IMPL_BASE *psBase, 
				  IMG_HANDLE *phHandle, 
				  IMG_VOID *pvData)
{
	IMG_UINT32 ui32NewIndex = BASE_TO_TOTAL_INDICES(psBase);
	HANDLE_IMPL_DATA *psNewHandleData = IMG_NULL;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psBase != IMG_NULL);
	PVR_ASSERT(phHandle != IMG_NULL);
	PVR_ASSERT(pvData != IMG_NULL);

	/* Ensure there is a free handle */
	eError = EnsureFreeHandles(psBase, 1);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: EnsureFreeHandles failed (%s)",
			 __FUNCTION__, PVRSRVGetErrorStringKM(eError)));
		return eError;
	}
	PVR_ASSERT(psBase->ui32TotalFreeHandCount != 0);

	if (!psBase->bPurgingEnabled)
	{
		/* Array index of first free handle */
		ui32NewIndex = psBase->ui32FirstFreeIndex;

		/* Get handle array entry */
		psNewHandleData = INDEX_TO_HANDLE_DATA(psBase, ui32NewIndex);
	}
	else
	{
		IMG_UINT32 ui32BlockedIndex;

		/*
		 * If purging is enabled, we always try to allocate handles
		 * at the front of the array, to increase the chances that
		 * the size of the handle array can be reduced by a purge.
		 * No linked list of free handles is kept; we search for
		 * free handles as required.
		 */

		/*
		 * ui32FirstFreeIndex should only be set when a new batch of
		 * handle structures is allocated, and should always be a
		 * multiple of the block size.
		 */
		PVR_ASSERT((psBase->ui32FirstFreeIndex % HANDLE_BLOCK_SIZE) == 0);

		for (ui32BlockedIndex = ROUND_DOWN_TO_MULTIPLE_OF_BLOCK_SIZE(psBase->ui32FirstFreeIndex); ui32BlockedIndex < psBase->ui32TotalHandCount; ui32BlockedIndex += HANDLE_BLOCK_SIZE)
		{
			HANDLE_BLOCK *psHandleBlock = BASE_AND_INDEX_TO_HANDLE_BLOCK(psBase, ui32BlockedIndex);

			if (psHandleBlock->ui32FreeHandCount == 0)
			{
				continue;
			}

			for (ui32NewIndex = ui32BlockedIndex; ui32NewIndex < ui32BlockedIndex + HANDLE_BLOCK_SIZE; ui32NewIndex++)
			{
				psNewHandleData = INDEX_TO_HANDLE_DATA(psBase, ui32NewIndex);
				if (psNewHandleData->pvData == IMG_NULL)
				{
					break;
				}
			}
		}
		psBase->ui32FirstFreeIndex = 0;
		PVR_ASSERT(INDEX_IS_VALID(psBase, ui32NewIndex));
	}
	PVR_ASSERT(psNewHandleData != IMG_NULL);

	psBase->ui32TotalFreeHandCount--;

	PVR_ASSERT(INDEX_TO_BLOCK_FREE_HAND_COUNT(psBase, ui32NewIndex) <= HANDLE_BLOCK_SIZE);
	PVR_ASSERT(INDEX_TO_BLOCK_FREE_HAND_COUNT(psBase, ui32NewIndex) > 0);

	INDEX_TO_BLOCK_FREE_HAND_COUNT(psBase, ui32NewIndex)--;

	/* No free list management if purging is enabled */
	if (!psBase->bPurgingEnabled)
	{
		/* Check whether the last free handle has been allocated */
		if (psBase->ui32TotalFreeHandCount == 0)
		{
			PVR_ASSERT(psBase->ui32FirstFreeIndex == ui32NewIndex);
			PVR_ASSERT(psBase->ui32LastFreeIndexPlusOne == (ui32NewIndex + 1));

			psBase->ui32LastFreeIndexPlusOne = 0;
			psBase->ui32FirstFreeIndex = 0;
		}
		else
		{
			/*
			 * Update the first free handle index.
			 * If the "next free index plus one" field in the new
			 * handle structure is zero, the next free index is
			 * the index of the new handle plus one.  This
			 * convention has been adopted to simplify the
			 * initialisation of freshly allocated handle
			 * space.
			 */
			if (psNewHandleData->ui32NextIndexPlusOne == 0)
			{
				psBase->ui32FirstFreeIndex = ui32NewIndex + 1;
			}
			else
			{
				psBase->ui32FirstFreeIndex = psNewHandleData->ui32NextIndexPlusOne - 1;
			}
		}
	}

	PVR_ASSERT(HANDLE_DATA_TO_HANDLE(psNewHandleData) == INDEX_TO_HANDLE(ui32NewIndex));

	psNewHandleData->pvData = pvData;
	psNewHandleData->ui32NextIndexPlusOne = 0;

	/* Return the new handle to the client */
	*phHandle = INDEX_TO_HANDLE(ui32NewIndex);

#if defined(DEBUG_HANDLEALLOC_KM)
	PVR_DPF((PVR_DBG_MESSAGE, "Handle acquire base %p hdl %p", psBase, *phHandle));
#endif

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	ReleaseHandle

 @Description	Release a handle that is no longer needed.

 @Input		psBase - Pointer to handle base structure
		hHandle - Handle to release
		ppvData - Points to a void data pointer

 @Output	ppvData - Points to a void data pointer

 @Return	PVRSRV_OK or PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR ReleaseHandle(HANDLE_IMPL_BASE *psBase, 
				  IMG_HANDLE hHandle, 
				  IMG_VOID **ppvData)
{
	IMG_UINT32 ui32Index = HANDLE_TO_INDEX(hHandle);
	HANDLE_IMPL_DATA *psHandleData;
	IMG_VOID *pvData;

	PVR_ASSERT(psBase);

	/* Check handle index is in range */
	if (!INDEX_IS_VALID(psBase, ui32Index))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Handle index out of range (%u >= %u)", 
			 __FUNCTION__, ui32Index, psBase->ui32TotalHandCount));
		return PVRSRV_ERROR_HANDLE_INDEX_OUT_OF_RANGE;
	}

	psHandleData = INDEX_TO_HANDLE_DATA(psBase, ui32Index);

	pvData = psHandleData->pvData;
	psHandleData->pvData = IMG_NULL;

	/* No free list management if purging is enabled */
	if (!psBase->bPurgingEnabled)
	{
		if (psBase->ui32TotalFreeHandCount == 0)
		{
			PVR_ASSERT(psBase->ui32FirstFreeIndex == 0);
			PVR_ASSERT(psBase->ui32LastFreeIndexPlusOne == 0);

			psBase->ui32FirstFreeIndex =  ui32Index;
		}
		else
		{
			/*
			 * Put the handle pointer on the end of the the free
			 * handle pointer linked list.
			 */
			PVR_ASSERT(psBase->ui32LastFreeIndexPlusOne != 0);
			PVR_ASSERT(INDEX_TO_HANDLE_DATA(psBase, psBase->ui32LastFreeIndexPlusOne - 1)->ui32NextIndexPlusOne == 0);
			INDEX_TO_HANDLE_DATA(psBase, psBase->ui32LastFreeIndexPlusOne - 1)->ui32NextIndexPlusOne =  ui32Index + 1;
		}

		PVR_ASSERT(psHandleData->ui32NextIndexPlusOne == 0);

		/* Update the end of the free handle linked list */
		psBase->ui32LastFreeIndexPlusOne = ui32Index + 1;
	}

	psBase->ui32TotalFreeHandCount++;
	INDEX_TO_BLOCK_FREE_HAND_COUNT(psBase, ui32Index)++;

	PVR_ASSERT(INDEX_TO_BLOCK_FREE_HAND_COUNT(psBase, ui32Index)<= HANDLE_BLOCK_SIZE);

#if defined(DEBUG)
	{
		IMG_UINT32 ui32BlockedIndex;
		IMG_UINT32 ui32TotalFreeHandCount = 0;

		for (ui32BlockedIndex = 0; ui32BlockedIndex < psBase->ui32TotalHandCount; ui32BlockedIndex += HANDLE_BLOCK_SIZE)
		{
			ui32TotalFreeHandCount += INDEX_TO_BLOCK_FREE_HAND_COUNT(psBase, ui32BlockedIndex);
		}

		PVR_ASSERT(ui32TotalFreeHandCount == psBase->ui32TotalFreeHandCount);
	}
#endif /* defined(DEBUG) */

	if (ppvData)
	{
		*ppvData = pvData;
	}

#if defined(DEBUG_HANDLEALLOC_KM)
	PVR_DPF((PVR_DBG_MESSAGE, "Handle release base %p hdl %p", psBase, hHandle));
#endif

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	GetHandleData

 @Description	Get the data associated with the given handle

 @Input		psBase - Pointer to handle base structure
		hHandle - Handle from which data should be retrieved
                ppvData - Points to a void data pointer

 @Output	ppvData - Points to a void data pointer

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR GetHandleData(HANDLE_IMPL_BASE *psBase, 
				  IMG_HANDLE hHandle, 
				  IMG_VOID **ppvData)
{
	IMG_UINT32 ui32Index = HANDLE_TO_INDEX(hHandle);
	HANDLE_IMPL_DATA *psHandleData;

	PVR_ASSERT(psBase);
	PVR_ASSERT(ppvData);

	/* Check handle index is in range */
	if (!INDEX_IS_VALID(psBase, ui32Index))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Handle index out of range (%u >= %u)", 
			 __FUNCTION__, ui32Index, psBase->ui32TotalHandCount));
		OSDumpStack();
		return PVRSRV_ERROR_HANDLE_INDEX_OUT_OF_RANGE;
	}

	psHandleData = INDEX_TO_HANDLE_DATA(psBase, ui32Index);
	if (psHandleData == IMG_NULL  ||  psHandleData->pvData == IMG_NULL)
	{
		return PVRSRV_ERROR_HANDLE_NOT_ALLOCATED;
	}

	*ppvData = psHandleData->pvData;

	return PVRSRV_OK;
}

static PVRSRV_ERROR IterateOverHandles(HANDLE_IMPL_BASE *psBase, PFN_HANDLE_ITER pfnHandleIter, IMG_VOID *pvHandleIterData)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 i;

	PVR_ASSERT(psBase);
	PVR_ASSERT(pfnHandleIter);

	if (psBase->ui32TotalFreeHandCount == psBase->ui32TotalHandCount)
	{
		return PVRSRV_OK;
	}

	for (i = 0; i < psBase->ui32TotalHandCount; i++)
	{
		HANDLE_IMPL_DATA *psHandleData = INDEX_TO_HANDLE_DATA(psBase, i);

		if (psHandleData->pvData != IMG_NULL)
		{
			eError = pfnHandleIter(HANDLE_DATA_TO_HANDLE(psHandleData), pvHandleIterData);
			if (eError != PVRSRV_OK)
			{
				break;
			}

			if (psBase->ui32TotalFreeHandCount == psBase->ui32TotalHandCount)
			{
				break;
			}
		}
	}

	return eError;
}

/*!
******************************************************************************

 @Function	EnableHandlePurging

 @Description	Enable purging for a given handle base

 @Input 	psBase - pointer to handle base structure

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR EnableHandlePurging(HANDLE_IMPL_BASE *psBase)
{
	PVR_ASSERT(psBase);

	if (psBase->bPurgingEnabled)
	{
		PVR_DPF((PVR_DBG_WARNING, "%s: Purging already enabled", __FUNCTION__));
		return PVRSRV_OK;
	}

	/* Purging can only be enabled if no handles have been allocated */
	if (psBase->ui32TotalHandCount != 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Handles have already been allocated", __FUNCTION__));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psBase->bPurgingEnabled = IMG_TRUE;

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PurgeHandles

 @Description	Purge handles for a given handle base

 @Input 	psBase - Pointer to handle base structure

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR PurgeHandles(HANDLE_IMPL_BASE *psBase)
{
	IMG_UINT32 ui32BlockIndex;
	IMG_UINT32 ui32NewHandCount;

	PVR_ASSERT(psBase);

	if (!psBase->bPurgingEnabled)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Purging not enabled for this handle base", __FUNCTION__));
		return PVRSRV_ERROR_NOT_SUPPORTED;
	}

	PVR_ASSERT((psBase->ui32TotalHandCount % HANDLE_BLOCK_SIZE) == 0);

	for (ui32BlockIndex = INDEX_TO_BLOCK_INDEX(psBase->ui32TotalHandCount); ui32BlockIndex != 0; ui32BlockIndex--)
	{
		if (psBase->psHandleBlockArray[ui32BlockIndex - 1].ui32FreeHandCount != HANDLE_BLOCK_SIZE)
		{
			break;
		}
	}
	ui32NewHandCount = BLOCK_INDEX_TO_INDEX(ui32BlockIndex);

	/* Check for a suitable decrease in the handle count */
	if (ui32NewHandCount <= (psBase->ui32TotalHandCount / 2))
	{
		return ReallocHandleBlockArray(psBase, ui32NewHandCount);
	}

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	CreateHandleBase

 @Description	Create a handle base structure

 @Input 	ppsBase - pointer to handle base structure pointer

 @Output	ppsBase - points to handle base structure pointer

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR CreateHandleBase(HANDLE_IMPL_BASE **ppsBase)
{
	HANDLE_IMPL_BASE *psBase;

	PVR_ASSERT(ppsBase);

	psBase = OSAllocZMem(sizeof(*psBase));
	if (psBase == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Couldn't allocate generic handle base", __FUNCTION__));

		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psBase->psHandleBlockArray	= IMG_NULL;
	psBase->ui32MaxHandleValue	= HANDLE_VALUE_MAX;
	psBase->bPurgingEnabled		= IMG_FALSE;

	*ppsBase = psBase;

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	DestroyHandleBase

 @Description	Destroy a handle base structure

 @Input 	psBase - pointer to handle base structure

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR DestroyHandleBase(HANDLE_IMPL_BASE *psBase)
{
	PVRSRV_ERROR eError;

	PVR_ASSERT(psBase);

	if (psBase->ui32TotalHandCount != psBase->ui32TotalFreeHandCount)
	{
		PVR_DPF((PVR_DBG_WARNING, "%s: Handles still exist (%u found)", 
			 __FUNCTION__, 
			 psBase->ui32TotalHandCount - psBase->ui32TotalFreeHandCount));

#if defined(DEBUG_HANDLEALLOC_INFO_KM)
		{
			IMG_UINT32 i;

			for (i = 0; i < psBase->ui32TotalHandCount; i++)
			{
				HANDLE_IMPL_DATA *psHandleData = INDEX_TO_HANDLE_DATA(psBase, i);

				if (psHandleData->pvData != IMG_NULL)
				{
					PVR_DPF((PVR_DBG_WARNING, "%d: handle[%p] data[%p] still allocated",
							i, psHandleData->hHandle, psHandleData->pvData));

				}
			}
		}
#endif /* DEBUG_HANDLEALLOC_INFO_KM */

	}

	eError = ReallocHandleBlockArray(psBase, 0);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Couldn't free handle array (%s)", 
			 __FUNCTION__, 
			 PVRSRVGetErrorStringKM(eError)));

		return eError;
	}

	OSFreeMem(psBase);

	return PVRSRV_OK;
}


static const HANDLE_IMPL_FUNCTAB g_sHandleFuncTab = 
{
	/* pfnAcquireHandle */
	&AcquireHandle,

	/* pfnReleaseHandle */
	&ReleaseHandle,

	/* pfnGetHandleData */
	&GetHandleData,

	/* pfnIterateOverHandles */
	&IterateOverHandles,

	/* pfnEnableHandlePurging */
	&EnableHandlePurging,

	/* pfnPurgeHandles */
	&PurgeHandles,

	/* pfnCreateHandleBase */
	&CreateHandleBase,

	/* pfnDestroyHandleBase */
	&DestroyHandleBase
};

PVRSRV_ERROR PVRSRVHandleGetFuncTable(HANDLE_IMPL_FUNCTAB const **ppsFuncs)
{
	static IMG_BOOL bAcquired = IMG_FALSE;

	if (bAcquired)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Function table already acquired", 
			 __FUNCTION__));
		return PVRSRV_ERROR_RESOURCE_UNAVAILABLE;
	}

	if (ppsFuncs == IMG_NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	*ppsFuncs = &g_sHandleFuncTab;

	bAcquired = IMG_TRUE;

	return PVRSRV_OK;
}

#else
/* Disable warning about an empty file */
#if defined(_WIN32)
#pragma warning (disable:4206)
#endif
#endif /* defined(PVR_SECURE_HANDLES) */

