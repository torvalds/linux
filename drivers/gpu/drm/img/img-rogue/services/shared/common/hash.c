/*************************************************************************/ /*!
@File
@Title          Self scaling hash tables.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description
   Implements simple self scaling hash tables. Hash collisions are handled by
   chaining entries together. Hash tables are increased in size when they
   become more than (50%?) full and decreased in size when less than (25%?)
   full. Hash tables are never decreased below their initial size.
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

/* include/ */
#include "img_defs.h"
#include "img_types.h"
#include "pvr_debug.h"
#include "pvrsrv_error.h"

/* services/shared/include/ */
#include "hash.h"

/* services/client/include/ or services/server/include/ */
#include "osfunc_common.h"
#include "allocmem.h"

//#define PERF_DBG_RESIZE
#if !defined(__KERNEL__) && defined(PERF_DBG_RESIZE)
#include <sys/time.h>
#endif

#if defined(__KERNEL__)
#include "pvrsrv.h"
#endif

#define	KEY_TO_INDEX(pHash, key, uSize) \
	((pHash)->pfnHashFunc((pHash)->uKeySize, (key), (uSize)) % (uSize))

#define	KEY_COMPARE(pHash, pKey1, pKey2) \
	((pHash)->pfnKeyComp((pHash)->uKeySize, (pKey1), (pKey2)))

#if defined(__linux__) && defined(__KERNEL__)
#define _AllocMem OSAllocMemNoStats
#define _AllocZMem OSAllocZMemNoStats
#define _FreeMem OSFreeMemNoStats
#else
#define _AllocMem OSAllocMem
#define _AllocZMem OSAllocZMem
#define _FreeMem OSFreeMem
#endif

#define NO_SHRINK 0

/* Each entry in a hash table is placed into a bucket */
typedef struct _BUCKET_
{
	struct _BUCKET_ *pNext; /*!< the next bucket on the same chain */
	uintptr_t v;            /*!< entry value */
	uintptr_t k[];          /* PRQA S 0642 */
	                        /* override dynamic array declaration warning */
} BUCKET;

struct _HASH_TABLE_
{
	IMG_UINT32 uSize;            /*!< current size of the hash table */
	IMG_UINT32 uCount;           /*!< number of entries currently in the hash table */
	IMG_UINT32 uMinimumSize;     /*!< the minimum size that the hash table should be re-sized to */
	IMG_UINT32 uKeySize;         /*!< size of key in bytes */
	IMG_UINT32 uShrinkThreshold; /*!< The threshold at which to trigger a shrink */
	IMG_UINT32 uGrowThreshold;   /*!< The threshold at which to trigger a grow */
	HASH_FUNC*     pfnHashFunc;  /*!< hash function */
	HASH_KEY_COMP* pfnKeyComp;   /*!< key comparison function */
	BUCKET**   ppBucketTable;    /*!< the hash table array */
#if defined(DEBUG)
	const char*      pszFile;
	unsigned int     ui32LineNum;
#endif
};

/*************************************************************************/ /*!
@Function       HASH_Func_Default
@Description    Hash function intended for hashing keys composed of uintptr_t
                arrays.
@Input          uKeySize     The size of the hash key, in bytes.
@Input          pKey         A pointer to the key to hash.
@Input          uHashTabLen  The length of the hash table.
@Return         The hash value.
*/ /**************************************************************************/
IMG_INTERNAL IMG_UINT32
HASH_Func_Default(size_t uKeySize, void *pKey, IMG_UINT32 uHashTabLen)
{
	uintptr_t *p = (uintptr_t *)pKey;
	IMG_UINT32 uKeyLen = uKeySize / sizeof(uintptr_t);
	IMG_UINT32 ui;
	IMG_UINT32 uHashKey = 0;

	PVR_UNREFERENCED_PARAMETER(uHashTabLen);

	PVR_ASSERT((uKeySize % sizeof(uintptr_t)) == 0);

	for (ui = 0; ui < uKeyLen; ui++)
	{
		IMG_UINT32 uHashPart = (IMG_UINT32)*p++;

		uHashPart += (uHashPart << 12);
		uHashPart ^= (uHashPart >> 22);
		uHashPart += (uHashPart << 4);
		uHashPart ^= (uHashPart >> 9);
		uHashPart += (uHashPart << 10);
		uHashPart ^= (uHashPart >> 2);
		uHashPart += (uHashPart << 7);
		uHashPart ^= (uHashPart >> 12);

		uHashKey += uHashPart;
	}

	return uHashKey;
}

/*************************************************************************/ /*!
@Function       HASH_Key_Comp_Default
@Description    Compares keys composed of uintptr_t arrays.
@Input          uKeySize     The size of the hash key, in bytes.
@Input          pKey1        Pointer to first hash key to compare.
@Input          pKey2        Pointer to second hash key to compare.
@Return         IMG_TRUE  - The keys match.
                IMG_FALSE - The keys don't match.
*/ /**************************************************************************/
IMG_INTERNAL IMG_BOOL
HASH_Key_Comp_Default(size_t uKeySize, void *pKey1, void *pKey2)
{
	uintptr_t *p1 = (uintptr_t *)pKey1;
	uintptr_t *p2 = (uintptr_t *)pKey2;
	IMG_UINT32 uKeyLen = uKeySize / sizeof(uintptr_t);
	IMG_UINT32 ui;

	PVR_ASSERT((uKeySize % sizeof(uintptr_t)) == 0);

	for (ui = 0; ui < uKeyLen; ui++)
	{
		if (*p1++ != *p2++)
			return IMG_FALSE;
	}

	return IMG_TRUE;
}

/*************************************************************************/ /*!
@Function       _ChainInsert
@Description    Insert a bucket into the appropriate hash table chain.
@Input          pBucket       The bucket
@Input          ppBucketTable The hash table
@Input          uSize         The size of the hash table
@Return         PVRSRV_ERROR
*/ /**************************************************************************/
static void
_ChainInsert(HASH_TABLE *pHash, BUCKET *pBucket, BUCKET **ppBucketTable, IMG_UINT32 uSize)
{
	IMG_UINT32 uIndex;

	/* We assume that all parameters passed by the caller are valid. */
	PVR_ASSERT(pBucket != NULL);
	PVR_ASSERT(ppBucketTable != NULL);
	PVR_ASSERT(uSize != 0);

	uIndex = KEY_TO_INDEX(pHash, pBucket->k, uSize);	/* PRQA S 0432,0541 */ /* ignore dynamic array warning */
	pBucket->pNext = ppBucketTable[uIndex];
	ppBucketTable[uIndex] = pBucket;
}

/*************************************************************************/ /*!
@Function       _Rehash
@Description    Iterate over every entry in an old hash table and rehash into
                the new table.
@Input          ppOldTable   The old hash table
@Input          uOldSize     The size of the old hash table
@Input          ppNewTable   The new hash table
@Input          uNewSize     The size of the new hash table
@Return         None
*/ /**************************************************************************/
static void
_Rehash(HASH_TABLE *pHash,
		 BUCKET **ppOldTable, IMG_UINT32 uOldSize,
		 BUCKET **ppNewTable, IMG_UINT32 uNewSize)
{
	IMG_UINT32 uIndex;
	for (uIndex=0; uIndex< uOldSize; uIndex++)
	{
		BUCKET *pBucket;
		pBucket = ppOldTable[uIndex];
		while (pBucket != NULL)
		{
			BUCKET *pNextBucket = pBucket->pNext;
			_ChainInsert(pHash, pBucket, ppNewTable, uNewSize);
			pBucket = pNextBucket;
		}
	}
}

/*************************************************************************/ /*!
@Function       _Resize
@Description    Attempt to resize a hash table, failure to allocate a new
                larger hash table is not considered a hard failure. We simply
                continue and allow the table to fill up, the effect is to
                allow hash chains to become longer.
@Input          pHash        Hash table to resize.
@Input          uNewSize     Required table size.
@Return         IMG_TRUE Success
                IMG_FALSE Failed
*/ /**************************************************************************/
static IMG_BOOL
_Resize(HASH_TABLE *pHash, IMG_UINT32 uNewSize)
{
	BUCKET **ppNewTable;
	IMG_UINT32 uiThreshold = uNewSize >> 2;
#if !defined(__KERNEL__) && defined(PERF_DBG_RESIZE)
	struct timeval start, end;
#endif

	if (uNewSize == pHash->uSize)
	{
		return IMG_TRUE;
	}

#if !defined(__KERNEL__) && defined(PERF_DBG_RESIZE)
	gettimeofday(&start, NULL);
#endif

	ppNewTable = _AllocZMem(sizeof(BUCKET *) * uNewSize);
	if (ppNewTable == NULL)
	{
		return IMG_FALSE;
	}

	_Rehash(pHash, pHash->ppBucketTable, pHash->uSize, ppNewTable, uNewSize);

	_FreeMem(pHash->ppBucketTable);

#if !defined(__KERNEL__) && defined(PERF_DBG_RESIZE)
	gettimeofday(&end, NULL);
	if (start.tv_usec > end.tv_usec)
	{
		end.tv_usec = 1000000 - start.tv_usec + end.tv_usec;
	}
	else
	{
		end.tv_usec -= start.tv_usec;
	}

	PVR_DPF((PVR_DBG_ERROR, "%s: H:%p O:%d N:%d C:%d G:%d S:%d T:%06luus", __func__, pHash, pHash->uSize, uNewSize, pHash->uCount, pHash->uGrowThreshold, pHash->uShrinkThreshold, end.tv_usec));
#endif

	/*not nulling pointer, being reassigned just below*/
	pHash->ppBucketTable = ppNewTable;
	pHash->uSize = uNewSize;

	pHash->uGrowThreshold = uiThreshold * 3;
	pHash->uShrinkThreshold = (uNewSize <= pHash->uMinimumSize) ? NO_SHRINK : uiThreshold;

	return IMG_TRUE;
}


/*************************************************************************/ /*!
@Function       HASH_Create_Extended
@Description    Create a self scaling hash table, using the supplied key size,
                and the supplied hash and key comparison functions.
@Input          uInitialLen  Initial and minimum length of the hash table,
                             where the length refers to the number of entries
                             in the hash table, not its size in bytes.
@Input          uKeySize     The size of the key, in bytes.
@Input          pfnHashFunc  Pointer to hash function.
@Input          pfnKeyComp   Pointer to key comparison function.
@Return         NULL or hash table handle.
*/ /**************************************************************************/
IMG_INTERNAL
HASH_TABLE * HASH_Create_Extended_Int (IMG_UINT32 uInitialLen, size_t uKeySize, HASH_FUNC *pfnHashFunc, HASH_KEY_COMP *pfnKeyComp)
{
	HASH_TABLE *pHash;

	if (uInitialLen == 0 || uKeySize == 0 || pfnHashFunc == NULL || pfnKeyComp == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: invalid input parameters", __func__));
		return NULL;
	}

	PVR_DPF((PVR_DBG_MESSAGE, "%s: InitialSize=0x%x", __func__, uInitialLen));

	pHash = _AllocMem(sizeof(HASH_TABLE));
	if (pHash == NULL)
	{
		return NULL;
	}

	pHash->uCount = 0;
	pHash->uSize = uInitialLen;
	pHash->uMinimumSize = uInitialLen;
	pHash->uKeySize = uKeySize;
	pHash->uGrowThreshold = (uInitialLen >> 2) * 3;
	pHash->uShrinkThreshold = NO_SHRINK;
	pHash->pfnHashFunc = pfnHashFunc;
	pHash->pfnKeyComp = pfnKeyComp;

	pHash->ppBucketTable = _AllocZMem(sizeof(BUCKET *) * pHash->uSize);
	if (pHash->ppBucketTable == NULL)
	{
		_FreeMem(pHash);
		/*not nulling pointer, out of scope*/
		return NULL;
	}

	return pHash;
}

#if defined(DEBUG)
IMG_INTERNAL
HASH_TABLE * HASH_Create_Extended_Debug (IMG_UINT32 uInitialLen, size_t uKeySize, HASH_FUNC *pfnHashFunc, HASH_KEY_COMP *pfnKeyComp,
										 const char *file, const unsigned int line)
{
	HASH_TABLE *hash;
	hash = HASH_Create_Extended_Int(uInitialLen, uKeySize,
									pfnHashFunc, pfnKeyComp);
	if (hash)
	{
		hash->pszFile = file;
		hash->ui32LineNum = line;
	}
	return hash;
}
#endif

/*************************************************************************/ /*!
@Function       HASH_Create
@Description    Create a self scaling hash table with a key consisting of a
                single uintptr_t, and using the default hash and key
                comparison functions.
@Input          uInitialLen  Initial and minimum length of the hash table,
                             where the length refers to the number of entries
                             in the hash table, not its size in bytes.
@Return         NULL or hash table handle.
*/ /**************************************************************************/
IMG_INTERNAL
HASH_TABLE * HASH_Create_Int (IMG_UINT32 uInitialLen)
{
	return HASH_Create_Extended_Int(uInitialLen, sizeof(uintptr_t),
									&HASH_Func_Default, &HASH_Key_Comp_Default);
}

#if defined(DEBUG)
IMG_INTERNAL
HASH_TABLE * HASH_Create_Debug(IMG_UINT32 uInitialLen, const char *file, const unsigned int line)
{
	HASH_TABLE *hash;
	hash = HASH_Create_Extended_Int(uInitialLen, sizeof(uintptr_t),
									&HASH_Func_Default, &HASH_Key_Comp_Default);
	if (hash)
	{
		hash->pszFile = file;
		hash->ui32LineNum = line;
	}
	return hash;
}
#endif

/*************************************************************************/ /*!
@Function       HASH_Delete_Extended
@Description    Delete a hash table created by HASH_Create_Extended or
                HASH_Create. All entries in the table should have been removed
                before calling this function.
@Input          pHash        Hash table
@Input          bWarn        Set false to suppress warnings in the case of
                             deletion with active entries.
*/ /**************************************************************************/
IMG_INTERNAL void
HASH_Delete_Extended(HASH_TABLE *pHash, IMG_BOOL bWarn)
{
	IMG_BOOL bDoCheck = IMG_TRUE;
#if defined(__KERNEL__) && !defined(__QNXNTO__)
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	if (psPVRSRVData != NULL)
	{
		if (psPVRSRVData->eServicesState != PVRSRV_SERVICES_STATE_OK)
		{
			bDoCheck = IMG_FALSE;
		}
	}
#if defined(PVRSRV_FORCE_UNLOAD_IF_BAD_STATE)
	else
	{
		bDoCheck = IMG_FALSE;
	}
#endif
#endif
	if (pHash != NULL)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "HASH_Delete"));

		if (bDoCheck)
		{
			PVR_ASSERT(pHash->uCount==0);
		}
		if (pHash->uCount != 0)
		{
			IMG_UINT32 i;
			if (bWarn)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Leak detected in hash table!", __func__));
				PVR_DPF((PVR_DBG_ERROR, "%s: Likely Cause: client drivers not freeing allocations before destroying devmem context", __func__));
				PVR_DPF((PVR_DBG_ERROR, "%s: Removing remaining %u hash entries.", __func__, pHash->uCount));
#if defined(DEBUG)
				PVR_DPF ((PVR_DBG_ERROR, "%s: Hash %p created at %s:%u.", __func__, (uintptr_t*)pHash, pHash->pszFile, pHash->ui32LineNum));
#endif
			}

			for (i = 0; i < pHash->uSize; i++)
			{
				BUCKET *pBucket = pHash->ppBucketTable[i];
				while (pBucket != NULL)
				{
					BUCKET *pNextBucket = pBucket->pNext;
					_FreeMem(pBucket);
					pBucket = pNextBucket;
				}
			}

		}
		_FreeMem(pHash->ppBucketTable);
		pHash->ppBucketTable = NULL;
		_FreeMem(pHash);
		/*not nulling pointer, copy on stack*/
	}
}

/*************************************************************************/ /*!
@Function       HASH_Delete
@Description    Delete a hash table created by HASH_Create_Extended or
                HASH_Create. All entries in the table must have been removed
                before calling this function.
@Input          pHash        Hash table
*/ /**************************************************************************/
IMG_INTERNAL void
HASH_Delete(HASH_TABLE *pHash)
{
	HASH_Delete_Extended(pHash, IMG_TRUE);
}

/*************************************************************************/ /*!
@Function       HASH_Insert_Extended
@Description    Insert a key value pair into a hash table created with
                HASH_Create_Extended.
@Input          pHash        The hash table.
@Input          pKey         Pointer to the key.
@Input          v            The value associated with the key.
@Return         IMG_TRUE - success.
                IMG_FALSE - failure.
*/ /**************************************************************************/
IMG_INTERNAL IMG_BOOL
HASH_Insert_Extended(HASH_TABLE *pHash, void *pKey, uintptr_t v)
{
	BUCKET *pBucket;

	PVR_ASSERT(pHash != NULL);

	if (pHash == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: invalid parameter", __func__));
		return IMG_FALSE;
	}

	pBucket = _AllocMem(sizeof(BUCKET) + pHash->uKeySize);
	if (pBucket == NULL)
	{
		return IMG_FALSE;
	}

	pBucket->v = v;
	/* PRQA S 0432,0541 1 */ /* ignore warning about dynamic array k (linux)*/
	OSCachedMemCopy(pBucket->k, pKey, pHash->uKeySize);

	_ChainInsert(pHash, pBucket, pHash->ppBucketTable, pHash->uSize);

	pHash->uCount++;

	/* check if we need to think about re-balancing */
	if (pHash->uCount > pHash->uGrowThreshold)
	{
		/* Ignore the return code from _Resize because the hash table is
		   still in a valid state and although not ideally sized, it is still
		   functional */
		_Resize(pHash, pHash->uSize << 1);
	}

	return IMG_TRUE;
}

/*************************************************************************/ /*!
@Function       HASH_Insert
@Description    Insert a key value pair into a hash table created with
                HASH_Create.
@Input          pHash        The hash table.
@Input          k            The key value.
@Input          v            The value associated with the key.
@Return         IMG_TRUE - success.
                IMG_FALSE - failure.
*/ /**************************************************************************/
IMG_INTERNAL IMG_BOOL
HASH_Insert(HASH_TABLE *pHash, uintptr_t k, uintptr_t v)
{
	return HASH_Insert_Extended(pHash, &k, v);
}

/*************************************************************************/ /*!
@Function       HASH_Remove_Extended
@Description    Remove a key from a hash table created with
                HASH_Create_Extended.
@Input          pHash        The hash table.
@Input          pKey         Pointer to key.
@Return         0 if the key is missing, or the value associated with the key.
*/ /**************************************************************************/
IMG_INTERNAL uintptr_t
HASH_Remove_Extended(HASH_TABLE *pHash, void *pKey)
{
	BUCKET **ppBucket;
	IMG_UINT32 uIndex;

	PVR_ASSERT(pHash != NULL);

	if (pHash == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Null hash table", __func__));
		return 0;
	}

	uIndex = KEY_TO_INDEX(pHash, pKey, pHash->uSize);

	for (ppBucket = &(pHash->ppBucketTable[uIndex]); *ppBucket != NULL; ppBucket = &((*ppBucket)->pNext))
	{
		/* PRQA S 0432,0541 1 */ /* ignore warning about dynamic array k */
		if (KEY_COMPARE(pHash, (*ppBucket)->k, pKey))
		{
			BUCKET *pBucket = *ppBucket;
			uintptr_t v = pBucket->v;
			(*ppBucket) = pBucket->pNext;

			_FreeMem(pBucket);
			/*not nulling original pointer, already overwritten*/

			pHash->uCount--;

			/* check if we need to think about re-balancing, when the shrink
			 * threshold is 0 we are at the minimum size, no further shrink */
			if (pHash->uCount < pHash->uShrinkThreshold)
			{
				/* Ignore the return code from _Resize because the
				   hash table is still in a valid state and although
				   not ideally sized, it is still functional */
				_Resize(pHash, MAX(pHash->uSize >> 1, pHash->uMinimumSize));
			}

			return v;
		}
	}
	return 0;
}

/*************************************************************************/ /*!
@Function       HASH_Remove
@Description    Remove a key value pair from a hash table created with
                HASH_Create.
@Input          pHash        The hash table.
@Input          pKey         Pointer to key.
@Return         0 if the key is missing, or the value associated with the key.
*/ /**************************************************************************/
IMG_INTERNAL uintptr_t
HASH_Remove(HASH_TABLE *pHash, uintptr_t k)
{
	return HASH_Remove_Extended(pHash, &k);
}

/*************************************************************************/ /*!
@Function       HASH_Retrieve_Extended
@Description    Retrieve a value from a hash table created with
                HASH_Create_Extended.
@Input          pHash        The hash table.
@Input          pKey         Pointer to key.
@Return         0 if the key is missing, or the value associated with the key.
*/ /**************************************************************************/
IMG_INTERNAL uintptr_t
HASH_Retrieve_Extended(HASH_TABLE *pHash, void *pKey)
{
	BUCKET **ppBucket;
	IMG_UINT32 uIndex;

	PVR_ASSERT(pHash != NULL);

	if (pHash == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Null hash table", __func__));
		return 0;
	}

	uIndex = KEY_TO_INDEX(pHash, pKey, pHash->uSize);

	for (ppBucket = &(pHash->ppBucketTable[uIndex]); *ppBucket != NULL; ppBucket = &((*ppBucket)->pNext))
	{
		/* PRQA S 0432,0541 1 */ /* ignore warning about dynamic array k */
		if (KEY_COMPARE(pHash, (*ppBucket)->k, pKey))
		{
			BUCKET *pBucket = *ppBucket;
			uintptr_t v = pBucket->v;

			return v;
		}
	}
	return 0;
}

/*************************************************************************/ /*!
@Function       HASH_Retrieve
@Description    Retrieve a value from a hash table created with HASH_Create.
@Input          pHash        The hash table.
@Input          pKey         Pointer to key.
@Return         0 if the key is missing, or the value associated with the key.
*/ /**************************************************************************/
IMG_INTERNAL uintptr_t
HASH_Retrieve(HASH_TABLE *pHash, uintptr_t k)
{
	return HASH_Retrieve_Extended(pHash, &k);
}

/*************************************************************************/ /*!
@Function       HASH_Iterate
@Description    Iterate over every entry in the hash table.
@Input          pHash        Hash table to iterate.
@Input          pfnCallback  Callback to call with the key and data for each
.                            entry in the hash table
@Return         Callback error if any, otherwise PVRSRV_OK
*/ /**************************************************************************/
IMG_INTERNAL PVRSRV_ERROR
HASH_Iterate(HASH_TABLE *pHash, HASH_pfnCallback pfnCallback, void* args)
{
	IMG_UINT32 uIndex;
	for (uIndex=0; uIndex < pHash->uSize; uIndex++)
	{
		BUCKET *pBucket;
		pBucket = pHash->ppBucketTable[uIndex];
		while (pBucket != NULL)
		{
			PVRSRV_ERROR eError;
			BUCKET *pNextBucket = pBucket->pNext;

			eError = pfnCallback((uintptr_t) ((void *) *(pBucket->k)), pBucket->v, args);

			/* The callback might want us to break out early */
			if (eError != PVRSRV_OK)
				return eError;

			pBucket = pNextBucket;
		}
	}
	return PVRSRV_OK;
}

#ifdef HASH_TRACE
/*************************************************************************/ /*!
@Function       HASH_Dump
@Description    Dump out some information about a hash table.
@Input          pHash         The hash table.
*/ /**************************************************************************/
void
HASH_Dump(HASH_TABLE *pHash)
{
	IMG_UINT32 uIndex;
	IMG_UINT32 uMaxLength=0;
	IMG_UINT32 uEmptyCount=0;

	PVR_ASSERT(pHash != NULL);
	for (uIndex=0; uIndex<pHash->uSize; uIndex++)
	{
		BUCKET *pBucket;
		IMG_UINT32 uLength = 0;
		if (pHash->ppBucketTable[uIndex] == NULL)
		{
			uEmptyCount++;
		}
		for (pBucket=pHash->ppBucketTable[uIndex];
				pBucket != NULL;
				pBucket = pBucket->pNext)
		{
			uLength++;
		}
		uMaxLength = MAX(uMaxLength, uLength);
	}

	PVR_TRACE(("hash table: uMinimumSize=%d  size=%d  count=%d",
			   pHash->uMinimumSize, pHash->uSize, pHash->uCount));
	PVR_TRACE(("  empty=%d  max=%d", uEmptyCount, uMaxLength));
}
#endif
