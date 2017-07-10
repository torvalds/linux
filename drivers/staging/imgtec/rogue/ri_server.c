/*************************************************************************/ /*!
@File			ri_server.c
@Title          Resource Information (RI) server implementation
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description	Resource Information (RI) server functions
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


#include <stdarg.h>
#include "allocmem.h"
#include "pvr_debug.h"
#include "pvrsrv_error.h"
#include "osfunc.h"

#include "srvkm.h"
#include "lock.h"
/* services/server/include/ */
#include "ri_server.h"

/* services/include/shared/ */
#include "hash.h"
/* services/shared/include/ */
#include "dllist.h"

#include "pmr.h"

#if defined(PVR_RI_DEBUG)

#define USE_RI_LOCK 	1

/*
 * Initial size use for Hash table.
 * (Used to index the RI list entries).
 */
#define _RI_INITIAL_HASH_TABLE_SIZE	64

/*
 * Values written to the 'valid' field of
 * RI structures when created and cleared
 * prior to being destroyed.
 * The code can then check this value
 * before accessing the provided pointer
 * contents as a valid RI structure.
 */
#define _VALID_RI_LIST_ENTRY 	0x66bccb66
#define _VALID_RI_SUBLIST_ENTRY	0x77cddc77
#define _INVALID				0x00000000

/*
 * If this define is set to 1, details of
 * the linked lists (addresses, prev/next
 * ptrs, etc) are also output when function
 * RIDumpList() is called
 */
#define _DUMP_LINKEDLIST_INFO		0


typedef IMG_UINT64 _RI_BASE_T;

/*
 *  Length of string used for process name
 */
#define TASK_COMM_LEN 				16
/*
 *  Length of string used for process ID
 */
#define TASK_PID_LEN 				11
/*
 *  Length of string used for "[{PID}:_{process_name}]"
 */
#define RI_PROC_TAG_CHAR_LEN 		(1+TASK_PID_LEN+2+TASK_COMM_LEN+1)

/*
 *  Length of string used for address
 */
#define RI_ADDR_CHAR_LEN			12
/*
 *  Length of string used for size
 */
#define RI_SIZE_CHAR_LEN			12
/*
 *  Length of string used for "{Imported from PID nnnnnnnnnn}"
 */
#define RI_IMPORT_TAG_CHAR_LEN 		32
/*
 *  Total length of string returned to debugfs
 *  {0xaddr}_{annotation_text}_{0xsize}_{import_tag}
 */
#define RI_MAX_DEBUGFS_ENTRY_LEN	(RI_ADDR_CHAR_LEN+1+RI_MAX_TEXT_LEN+1+RI_SIZE_CHAR_LEN+1+RI_IMPORT_TAG_CHAR_LEN+1)
/*
 *  Total length of string output to _RIOutput()
 *  for MEMDESC RI sub-list entries
 *  {0xaddr}_{annotation_text}_[{PID}:_{process_name}]_{0xsize}_bytes_{import_tag}
 */
#define RI_MAX_MEMDESC_RI_ENTRY_LEN	(RI_ADDR_CHAR_LEN+1+RI_MAX_TEXT_LEN+1+RI_PROC_TAG_CHAR_LEN+1+RI_SIZE_CHAR_LEN+7+RI_IMPORT_TAG_CHAR_LEN+1)
/*
 *  Total length of string output to _RIOutput()
 *  for PMR RI list entries
 *  {annotation_text}_{pmr_handle}_suballocs:{num_suballocs}_{0xsize}
 */
#define RI_MAX_PMR_RI_ENTRY_LEN		(RI_MAX_TEXT_LEN+1+RI_ADDR_CHAR_LEN+11+10+1+RI_SIZE_CHAR_LEN)


/*
 * Structure used to make linked sublist of
 * memory allocations (MEMDESC)
 */
struct _RI_SUBLIST_ENTRY_
{
	DLLIST_NODE				sListNode;
	struct _RI_LIST_ENTRY_	*psRI;
	IMG_UINT32 				valid;
	IMG_BOOL				bIsImport;
	IMG_BOOL				bIsExportable;
	IMG_BOOL				bIsPinned;
	IMG_PID					pid;
	IMG_CHAR				ai8ProcName[TASK_COMM_LEN];
	IMG_DEV_VIRTADDR 		sVAddr;
	IMG_UINT64				ui64Offset;
	IMG_UINT64				ui64Size;
	IMG_UINT64				ui64BackedSize;
	IMG_CHAR				ai8TextB[RI_MAX_TEXT_LEN+1];
	DLLIST_NODE				sProcListNode;
};

/*
 * Structure used to make linked list of
 * PMRs. Sublists of allocations (MEMDESCs) made
 * from these PMRs are chained off these entries.
 */
struct _RI_LIST_ENTRY_
{
	DLLIST_NODE				sListNode;
	DLLIST_NODE				sSubListFirst;
	IMG_UINT32 				valid;
	PMR						*hPMR;
	IMG_UINT64 				ui64LogicalSize;
	IMG_PID					pid;
	IMG_CHAR				ai8ProcName[TASK_COMM_LEN];
	IMG_CHAR				ai8TextA[RI_MAX_TEXT_LEN+1];
	IMG_UINT16 				ui16SubListCount;
	IMG_UINT16 				ui16MaxSubListCount;
};

typedef struct _RI_LIST_ENTRY_ RI_LIST_ENTRY;
typedef struct _RI_SUBLIST_ENTRY_ RI_SUBLIST_ENTRY;

static IMG_UINT16 	g_ui16RICount = 0;
static HASH_TABLE 	*g_pRIHashTable = NULL;
static IMG_UINT16 	g_ui16ProcCount = 0;
static HASH_TABLE 	*g_pProcHashTable = NULL;

static POS_LOCK		g_hRILock;
/*
 * Flag used to indicate if RILock should be destroyed when final PMR entry
 * is deleted, i.e. if RIDeInitKM() has already been called before that point
 * but the handle manager has deferred deletion of RI entries.
 */
static IMG_BOOL 	bRIDeInitDeferred = IMG_FALSE;

/*
 *  Used as head of linked-list of PMR RI entries -
 *  this is useful when we wish to iterate all PMR
 *  list entries (when we don't have a PMR ref)
 */
static DLLIST_NODE	sListFirst;

/* Function used to produce string containing info for MEMDESC RI entries (used for both debugfs and kernel log output) */
static void _GenerateMEMDESCEntryString(RI_SUBLIST_ENTRY *psRISubEntry, IMG_BOOL bDebugFs, IMG_UINT16 ui16MaxStrLen, IMG_CHAR *pszEntryString);

static PVRSRV_ERROR _DumpAllEntries (uintptr_t k, uintptr_t v);
static PVRSRV_ERROR _DeleteAllEntries (uintptr_t k, uintptr_t v);
static PVRSRV_ERROR _DeleteAllProcEntries (uintptr_t k, uintptr_t v);
static PVRSRV_ERROR _DumpList(PMR *hPMR, IMG_PID pid);
#define _RIOutput(x) PVR_LOG(x)

IMG_INTERNAL IMG_UINT32
_ProcHashFunc (size_t uKeySize, void *pKey, IMG_UINT32 uHashTabLen);
IMG_INTERNAL IMG_UINT32
_ProcHashFunc (size_t uKeySize, void *pKey, IMG_UINT32 uHashTabLen)
{
	IMG_UINT32 *p = (IMG_UINT32 *)pKey;
	IMG_UINT32 uKeyLen = uKeySize / sizeof(IMG_UINT32);
	IMG_UINT32 ui;
	IMG_UINT32 uHashKey = 0;

	PVR_UNREFERENCED_PARAMETER(uHashTabLen);

	for (ui = 0; ui < uKeyLen; ui++)
	{
		IMG_UINT32 uHashPart = *p++;

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
IMG_INTERNAL IMG_BOOL
_ProcHashComp (size_t uKeySize, void *pKey1, void *pKey2);
IMG_INTERNAL IMG_BOOL
_ProcHashComp (size_t uKeySize, void *pKey1, void *pKey2)
{
	IMG_UINT32 *p1 = (IMG_UINT32 *)pKey1;
	IMG_UINT32 *p2 = (IMG_UINT32 *)pKey2;
	IMG_UINT32 uKeyLen = uKeySize / sizeof(IMG_UINT32);
	IMG_UINT32 ui;

	for (ui = 0; ui < uKeyLen; ui++)
	{
		if (*p1++ != *p2++)
			return IMG_FALSE;
	}

	return IMG_TRUE;
}

static void _RILock(void)
{
#if (USE_RI_LOCK == 1)
	OSLockAcquire(g_hRILock);
#endif
}

static void _RIUnlock(void)
{
#if (USE_RI_LOCK == 1)
	OSLockRelease(g_hRILock);
#endif
}

PVRSRV_ERROR RIInitKM(void)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	bRIDeInitDeferred = IMG_FALSE;
#if (USE_RI_LOCK == 1)
	eError = OSLockCreate(&g_hRILock, LOCK_TYPE_PASSIVE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: OSLockCreate failed (returned %d)",__func__,eError));
	}
#endif
	return eError;
}
void RIDeInitKM(void)
{
#if (USE_RI_LOCK == 1)
	if (g_ui16RICount > 0)
	{
		PVR_DPF((PVR_DBG_WARNING, "%s: called with %d entries remaining - deferring OSLockDestroy()",__func__,g_ui16RICount));
		bRIDeInitDeferred = IMG_TRUE;
	}
	else
	{
		OSLockDestroy(g_hRILock);
	}
#endif
}

/*!
******************************************************************************

 @Function	RIWritePMREntryKM

 @Description
            Writes a new Resource Information list entry.
            The new entry will be inserted at the head of the list of
            PMR RI entries and assigned the values provided.

 @input     hPMR - Reference (handle) to the PMR to which this reference relates
 @input     ai8TextA - String describing this PMR (may be null)
 @input     uiLogicalSize - Size of PMR

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIWritePMREntryKM(PMR *hPMR,
					   	       IMG_UINT32 ui32TextASize,
					   	       const IMG_CHAR *psz8TextA,
					   	       IMG_UINT64 ui64LogicalSize)
{
	uintptr_t hashData = 0;
	PMR			*pPMRHashKey = hPMR;
	IMG_PCHAR pszText = (IMG_PCHAR)psz8TextA;
	RI_LIST_ENTRY *psRIEntry = NULL;


	/* if Hash table has not been created, create it now */
	if (!g_pRIHashTable)
	{
		g_pRIHashTable = HASH_Create_Extended(_RI_INITIAL_HASH_TABLE_SIZE, sizeof(PMR*), HASH_Func_Default, HASH_Key_Comp_Default);
		g_pProcHashTable = HASH_Create_Extended(_RI_INITIAL_HASH_TABLE_SIZE, sizeof(IMG_PID), _ProcHashFunc, _ProcHashComp);
	}
	if (!g_pRIHashTable || !g_pProcHashTable)
	{
		/* Error - no memory to allocate for Hash table(s) */
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	if (!hPMR)
	{
		/* NULL handle provided */
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	else
	{
		/* Acquire RI Lock */
		_RILock();

		/* look-up hPMR in Hash Table */
		hashData = HASH_Retrieve_Extended (g_pRIHashTable, (void *)&pPMRHashKey);
		psRIEntry = (RI_LIST_ENTRY *)hashData;
		if (!psRIEntry)
		{
			/*
			 * If failed to find a matching existing entry, create a new one
			 */
			psRIEntry = (RI_LIST_ENTRY *)OSAllocZMem(sizeof(RI_LIST_ENTRY));
			if (!psRIEntry)
			{
				/* Release RI Lock */
				_RIUnlock();
				/* Error - no memory to allocate for new RI entry */
				return PVRSRV_ERROR_OUT_OF_MEMORY;
			}
			else
			{
				/*
				 * Add new RI Entry
				 */
				if (g_ui16RICount == 0)
				{
					/* Initialise PMR entry linked-list head */
					dllist_init(&sListFirst);
				}
				g_ui16RICount++;

				dllist_init (&(psRIEntry->sSubListFirst));
				psRIEntry->ui16SubListCount = 0;
				psRIEntry->ui16MaxSubListCount = 0;
				psRIEntry->valid = _VALID_RI_LIST_ENTRY;
				psRIEntry->pid = OSGetCurrentClientProcessIDKM();
				OSSNPrintf((IMG_CHAR *)psRIEntry->ai8ProcName, TASK_COMM_LEN, "%s", OSGetCurrentClientProcessNameKM());
				/* Add PMR entry to linked-list of PMR entries */
				dllist_init (&(psRIEntry->sListNode));
				dllist_add_to_tail(&sListFirst,(PDLLIST_NODE)&(psRIEntry->sListNode));
			}

			if (pszText)
			{
				if (ui32TextASize > RI_MAX_TEXT_LEN)
					ui32TextASize = RI_MAX_TEXT_LEN;

				/* copy ai8TextA field data */
				OSSNPrintf((IMG_CHAR *)psRIEntry->ai8TextA, ui32TextASize+1, "%s", pszText);

				/* ensure string is NUL-terminated */
				psRIEntry->ai8TextA[ui32TextASize] = '\0';
			}
			else
			{
				/* ensure string is NUL-terminated */
				psRIEntry->ai8TextA[0] = '\0';
			}
			psRIEntry->hPMR = hPMR;
			psRIEntry->ui64LogicalSize = ui64LogicalSize;

			/* Create index entry in Hash Table */
			HASH_Insert_Extended (g_pRIHashTable, (void *)&pPMRHashKey, (uintptr_t)psRIEntry);

			/* Store phRIHandle in PMR structure, so it can delete the associated RI entry when it destroys the PMR */
			PMRStoreRIHandle(hPMR, psRIEntry);
		}
		/* Release RI Lock */
		_RIUnlock();
	}
	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	RIWriteMEMDESCEntryKM

 @Description
            Writes a new Resource Information sublist entry.
            The new entry will be inserted at the head of the sublist of
            the indicated PMR list entry, and assigned the values provided.

 @input     hPMR - Reference (handle) to the PMR to which this MEMDESC RI entry relates
 @input     ai8TextB - String describing this secondary reference (may be null)
 @input     uiOffset - Offset from the start of the PMR at which this allocation begins
 @input     uiSize - Size of this allocation
 @input     ui64BackedSize - How much of uiSize is actually physically backed?
 @input     bIsImport - Flag indicating if this is an allocation or an import
 @input     bIsExportable - Flag indicating if this allocation is exportable
 @output    phRIHandle - Handle to the created RI entry

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIWriteMEMDESCEntryKM(PMR *hPMR,
					   	   	   	   IMG_UINT32 ui32TextBSize,
					   	   	   	   const IMG_CHAR *psz8TextB,
					   	   	   	   IMG_UINT64 ui64Offset,
					   	   	   	   IMG_UINT64 ui64Size,
					   	   	   	   IMG_UINT64 ui64BackedSize,
					   	   	   	   IMG_BOOL bIsImport,
					   	   	   	   IMG_BOOL bIsExportable,
					   	   	   	   RI_HANDLE *phRIHandle)
{
	uintptr_t hashData = 0;
	PMR 		*pPMRHashKey = hPMR;
	IMG_PID		pid;
	IMG_PCHAR pszText = (IMG_PCHAR)psz8TextB;
	RI_LIST_ENTRY *psRIEntry = NULL;
	RI_SUBLIST_ENTRY *psRISubEntry = NULL;


	/* check Hash tables have been created (meaning at least one PMR has been defined) */
	if (!g_pRIHashTable || !g_pProcHashTable)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	if (!hPMR || !phRIHandle)
	{
		/* NULL handle provided */
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	else
	{
		/* Acquire RI Lock */
		_RILock();

		*phRIHandle = NULL;

		/* look-up hPMR in Hash Table */
		hashData = HASH_Retrieve_Extended (g_pRIHashTable, (void *)&pPMRHashKey);
		psRIEntry = (RI_LIST_ENTRY *)hashData;
		if (!psRIEntry)
		{
			/* Release RI Lock */
			_RIUnlock();
			return PVRSRV_ERROR_INVALID_PARAMS;
		}

		psRISubEntry = (RI_SUBLIST_ENTRY *)OSAllocZMem(sizeof(RI_SUBLIST_ENTRY));
		if (!psRISubEntry)
		{
			/* Release RI Lock */
			_RIUnlock();
			/* Error - no memory to allocate for new RI sublist entry */
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}
		else
		{
			/*
			 * Insert new entry in sublist
			 */
			PDLLIST_NODE currentNode = dllist_get_next_node(&(psRIEntry->sSubListFirst));

			/*
			 * Insert new entry before currentNode
			 */
			if (!currentNode)
			{
				currentNode = &(psRIEntry->sSubListFirst);
			}
			dllist_add_to_tail(currentNode, (PDLLIST_NODE)&(psRISubEntry->sListNode));

			psRISubEntry->psRI = psRIEntry;

			/* Increment number of entries in sublist */
			psRIEntry->ui16SubListCount++;
			if (psRIEntry->ui16SubListCount > psRIEntry->ui16MaxSubListCount)
			{
				psRIEntry->ui16MaxSubListCount = psRIEntry->ui16SubListCount;
			}
			psRISubEntry->valid = _VALID_RI_SUBLIST_ENTRY;
		}

		psRISubEntry->pid = OSGetCurrentClientProcessIDKM();

		if (ui32TextBSize > RI_MAX_TEXT_LEN)
			ui32TextBSize = RI_MAX_TEXT_LEN;
		/* copy ai8TextB field data */
		OSSNPrintf((IMG_CHAR *)psRISubEntry->ai8TextB, ui32TextBSize+1, "%s", pszText);
		/* ensure string is NUL-terminated */
		psRISubEntry->ai8TextB[ui32TextBSize] = '\0';

		psRISubEntry->ui64Offset = ui64Offset;
		psRISubEntry->ui64Size = ui64Size;
		psRISubEntry->ui64BackedSize = ui64BackedSize;
		psRISubEntry->bIsImport = bIsImport;
		psRISubEntry->bIsExportable = bIsExportable;
		psRISubEntry->bIsPinned = IMG_TRUE;
		OSSNPrintf((IMG_CHAR *)psRISubEntry->ai8ProcName, TASK_COMM_LEN, "%s", OSGetCurrentClientProcessNameKM());
		dllist_init (&(psRISubEntry->sProcListNode));

		/*
		 *	Now insert this MEMDESC into the proc list
		 */
		/* look-up pid in Hash Table */
		pid = psRISubEntry->pid;
		hashData = HASH_Retrieve_Extended (g_pProcHashTable, (void *)&pid);
		if (!hashData)
		{
			/*
			 * No allocations for this pid yet
			 */
			HASH_Insert_Extended (g_pProcHashTable, (void *)&pid, (uintptr_t)&(psRISubEntry->sProcListNode));
			/* Increment number of entries in proc hash table */
			g_ui16ProcCount++;
		}
		else
		{
			/*
			 * Insert allocation into pid allocations linked list
			 */
			PDLLIST_NODE currentNode = (PDLLIST_NODE)hashData;

			/*
			 * Insert new entry
			 */
			dllist_add_to_tail(currentNode, (PDLLIST_NODE)&(psRISubEntry->sProcListNode));
		}
		*phRIHandle = (RI_HANDLE)psRISubEntry;
		/* Release RI Lock */
		_RIUnlock();
	}
	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	RIWriteProcListEntryKM

 @Description
            Write a new entry in the process list directly. We have to do this
            because there might be no, multiple or changing PMR handles.

            In the common case we have a PMR that will be added to the PMR list
            and one or several MemDescs that are associated to it in a sub-list.
            Additionally these MemDescs will be inserted in the per-process list.

            There might be special descriptors from e.g. new user APIs that
            are associated with no or multiple PMRs and not just one.
            These can be now added to the per-process list (as RI_SUBLIST_ENTRY)
            directly with this function and won't be listed in the PMR list (RIEntry)
            because there might be no PMR.

            To remove entries from the per-process list, just use
            RIDeleteMEMDESCEntryKM().

 @input     ai8TextB - String describing this secondary reference (may be null)
 @input     uiSize - Size of this allocation
 @input     ui64BackedSize - How much of uiSize is actually physically backed?
 @input     ui64DevVAddr - Virtual address of this entry
 @output    phRIHandle - Handle to the created RI entry

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIWriteProcListEntryKM(IMG_UINT32 ui32TextBSize,
                                    const IMG_CHAR *psz8TextB,
                                    IMG_UINT64 ui64Size,
                                    IMG_UINT64 ui64BackedSize,
                                    IMG_UINT64 ui64DevVAddr,
                                    RI_HANDLE *phRIHandle)
{
	uintptr_t hashData = 0;
	IMG_PID		pid;
	IMG_PCHAR pszText = (IMG_PCHAR)psz8TextB;
	RI_SUBLIST_ENTRY *psRISubEntry = NULL;

	if (!g_pRIHashTable)
	{
		g_pRIHashTable = HASH_Create_Extended(_RI_INITIAL_HASH_TABLE_SIZE, sizeof(PMR*), HASH_Func_Default, HASH_Key_Comp_Default);
		g_pProcHashTable = HASH_Create_Extended(_RI_INITIAL_HASH_TABLE_SIZE, sizeof(IMG_PID), _ProcHashFunc, _ProcHashComp);

		if (!g_pRIHashTable || !g_pProcHashTable)
		{
			/* Error - no memory to allocate for Hash table(s) */
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}
	}

	/* Acquire RI Lock */
	_RILock();

	*phRIHandle = NULL;

	psRISubEntry = (RI_SUBLIST_ENTRY *)OSAllocZMem(sizeof(RI_SUBLIST_ENTRY));
	if (!psRISubEntry)
	{
		/* Release RI Lock */
		_RIUnlock();
		/* Error - no memory to allocate for new RI sublist entry */
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psRISubEntry->valid = _VALID_RI_SUBLIST_ENTRY;


	psRISubEntry->pid = OSGetCurrentClientProcessIDKM();

	if (ui32TextBSize > RI_MAX_TEXT_LEN)
		ui32TextBSize = RI_MAX_TEXT_LEN;
	/* copy ai8TextB field data */
	OSSNPrintf((IMG_CHAR *)psRISubEntry->ai8TextB, ui32TextBSize+1, "%s", pszText);
	/* ensure string is NUL-terminated */
	psRISubEntry->ai8TextB[ui32TextBSize] = '\0';

	psRISubEntry->ui64Offset = 0;
	psRISubEntry->ui64Size = ui64Size;
	psRISubEntry->ui64BackedSize = ui64BackedSize;
	psRISubEntry->sVAddr.uiAddr = ui64DevVAddr;
	psRISubEntry->bIsImport = IMG_FALSE;
	psRISubEntry->bIsExportable = IMG_FALSE;
	psRISubEntry->bIsPinned = IMG_TRUE;
	OSSNPrintf((IMG_CHAR *)psRISubEntry->ai8ProcName, TASK_COMM_LEN, "%s", OSGetCurrentClientProcessNameKM());
	dllist_init (&(psRISubEntry->sProcListNode));

	/*
	 *	Now insert this MEMDESC into the proc list
	 */
	/* look-up pid in Hash Table */
	pid = psRISubEntry->pid;
	hashData = HASH_Retrieve_Extended (g_pProcHashTable, (void *)&pid);
	if (!hashData)
	{
		/*
		 * No allocations for this pid yet
		 */
		HASH_Insert_Extended (g_pProcHashTable, (void *)&pid, (uintptr_t)&(psRISubEntry->sProcListNode));
		/* Increment number of entries in proc hash table */
		g_ui16ProcCount++;
	}
	else
	{
		/*
		 * Insert allocation into pid allocations linked list
		 */
		PDLLIST_NODE currentNode = (PDLLIST_NODE)hashData;

		/*
		 * Insert new entry
		 */
		dllist_add_to_tail(currentNode, (PDLLIST_NODE)&(psRISubEntry->sProcListNode));
	}
	*phRIHandle = (RI_HANDLE)psRISubEntry;
	/* Release RI Lock */
	_RIUnlock();

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	RIUpdateMEMDESCAddrKM

 @Description
            Update a Resource Information entry.

 @input     hRIHandle - Handle of object whose reference info is to be updated
 @input     uiAddr - New address for the RI entry

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIUpdateMEMDESCAddrKM(RI_HANDLE hRIHandle,
								   IMG_DEV_VIRTADDR sVAddr)
{
	RI_SUBLIST_ENTRY *psRISubEntry = NULL;

	if (!hRIHandle)
	{
		/* NULL handle provided */
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	psRISubEntry = (RI_SUBLIST_ENTRY *)hRIHandle;
	if (psRISubEntry->valid != _VALID_RI_SUBLIST_ENTRY)
	{
		/* Pointer does not point to valid structure */
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

    /* Acquire RI lock*/
	_RILock();

	psRISubEntry->sVAddr.uiAddr = sVAddr.uiAddr;

	/* Release RI lock */
	_RIUnlock();

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	RIUpdateMEMDESCPinningKM

 @Description
            Update a Resource Information entry.

 @input     hRIHandle - Handle of object whose reference info is to be updated
 @input     bIsPinned - The new pinning state

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIUpdateMEMDESCPinningKM(RI_HANDLE hRIHandle,
								   IMG_BOOL bIsPinned)
{
	RI_SUBLIST_ENTRY *psRISubEntry = NULL;

	if (!hRIHandle)
	{
		/* NULL handle provided */
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	psRISubEntry = (RI_SUBLIST_ENTRY *)hRIHandle;
	if (psRISubEntry->valid != _VALID_RI_SUBLIST_ENTRY)
	{
		/* Pointer does not point to valid structure */
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

    /* Acquire RI lock*/
	_RILock();

	psRISubEntry->bIsPinned = bIsPinned;

	/* Release RI lock */
	_RIUnlock();

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	RIUpdateMEMDESCBackingKM

 @Description
            Update a Resource Information entry.

 @input     hRIHandle       Handle of object whose reference info is to be updated
 @input     iSizeAdjustment The change of backed physical memory for this
                            allocation in bytes.

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIUpdateMEMDESCBackingKM(RI_HANDLE hRIHandle,
                                      IMG_INT32 iSizeAdjustment)
{
	RI_SUBLIST_ENTRY *psRISubEntry = NULL;

	if (!hRIHandle)
	{
		/* NULL handle provided */
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	psRISubEntry = (RI_SUBLIST_ENTRY *)hRIHandle;
	if (psRISubEntry->valid != _VALID_RI_SUBLIST_ENTRY)
	{
		/* Pointer does not point to valid structure */
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Acquire RI lock*/
	_RILock();

	psRISubEntry->ui64BackedSize += iSizeAdjustment;

	/* Release RI lock */
	_RIUnlock();

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	RIDeletePMREntryKM

 @Description
            Delete a Resource Information entry.

 @input     hRIHandle - Handle of object whose reference info is to be deleted

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIDeletePMREntryKM(RI_HANDLE hRIHandle)
{
	RI_LIST_ENTRY *psRIEntry = NULL;
	PMR			*pPMRHashKey;
	PVRSRV_ERROR eResult = PVRSRV_OK;


	if (!hRIHandle)
	{
		/* NULL handle provided */
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	else
	{
		psRIEntry = (RI_LIST_ENTRY *)hRIHandle;

		if (psRIEntry->valid != _VALID_RI_LIST_ENTRY)
		{
			/* Pointer does not point to valid structure */
			return PVRSRV_ERROR_INVALID_PARAMS;
		}

		if(psRIEntry->ui16SubListCount == 0)
		{
		    /* Acquire RI lock*/
			_RILock();

			/* Remove the HASH table index entry */
			pPMRHashKey = psRIEntry->hPMR;
			HASH_Remove_Extended(g_pRIHashTable, (void *)&pPMRHashKey);

			psRIEntry->valid = _INVALID;

			/* Remove PMR entry from linked-list of PMR entries */
			dllist_remove_node((PDLLIST_NODE)&(psRIEntry->sListNode));

			/* Now, free the memory used to store the RI entry */
			OSFreeMem(psRIEntry);
			psRIEntry = NULL;

		    /* Release RI lock*/
			_RIUnlock();

			/*
			 * Decrement number of RI entries - if this is now zero,
			 * we can delete the RI hash table
			 */
			if(--g_ui16RICount == 0)
			{
				HASH_Delete(g_pRIHashTable);
				g_pRIHashTable = NULL;
				/* If deInit has been deferred, we can now destroy the RI Lock */
				if (bRIDeInitDeferred)
				{
					OSLockDestroy(g_hRILock);
				}
			}
			/*
			 * Make the handle NULL once PMR RI entry is deleted
			 */
			hRIHandle = NULL;
		}
		else
		{
			eResult = PVRSRV_ERROR_DEVICEMEM_ALLOCATIONS_REMAIN_IN_HEAP;
		}
	}

	return eResult;
}

/*!
******************************************************************************

 @Function	RIDeleteMEMDESCEntryKM

 @Description
            Delete a Resource Information entry.
            Entry can be from RIEntry list or ProcList.

 @input     hRIHandle - Handle of object whose reference info is to be deleted

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIDeleteMEMDESCEntryKM(RI_HANDLE hRIHandle)
{
	RI_LIST_ENTRY *psRIEntry = NULL;
	RI_SUBLIST_ENTRY *psRISubEntry = NULL;
	uintptr_t hashData = 0;
	IMG_PID     pid;
	PVRSRV_ERROR eResult = PVRSRV_OK;


	if (!hRIHandle)
	{
		/* NULL handle provided */
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psRISubEntry = (RI_SUBLIST_ENTRY *)hRIHandle;
	if (psRISubEntry->valid != _VALID_RI_SUBLIST_ENTRY)
	{
		/* Pointer does not point to valid structure */
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

    /* Acquire RI lock*/
	_RILock();

	/* For entries which do have a parent PMR remove the node from the sublist */
	if (psRISubEntry->psRI)
	{
		psRIEntry = (RI_LIST_ENTRY *)psRISubEntry->psRI;

		/* Now, remove entry from the sublist */
		dllist_remove_node(&(psRISubEntry->sListNode));
	}

	psRISubEntry->valid = _INVALID;

	/* Remove the entry from the proc allocations linked list */
	pid = psRISubEntry->pid;
	/* If this is the only allocation for this pid, just remove it from the hash table */
	if (dllist_get_next_node(&(psRISubEntry->sProcListNode)) == NULL)
	{
		HASH_Remove_Extended(g_pProcHashTable, (void *)&pid);
		/* Decrement number of entries in proc hash table, and delete the hash table if there are now none */
		if(--g_ui16ProcCount == 0)
		{
			HASH_Delete(g_pProcHashTable);
			g_pProcHashTable = NULL;
		}
	}
	else
	{
		hashData = HASH_Retrieve_Extended (g_pProcHashTable, (void *)&pid);
		if ((PDLLIST_NODE)hashData == &(psRISubEntry->sProcListNode))
		{
			HASH_Remove_Extended(g_pProcHashTable, (void *)&pid);
			HASH_Insert_Extended (g_pProcHashTable, (void *)&pid, (uintptr_t)dllist_get_next_node(&(psRISubEntry->sProcListNode)));
		}
	}
	dllist_remove_node(&(psRISubEntry->sProcListNode));

	/* Now, free the memory used to store the sublist entry */
	OSFreeMem(psRISubEntry);
	psRISubEntry = NULL;

	/*
	 * Decrement number of entries in sublist if this MemDesc had a parent entry.
	 */
	if (psRIEntry)
	{
		psRIEntry->ui16SubListCount--;
	}

    /* Release RI lock*/
	_RIUnlock();

	/*
	 * Make the handle NULL once MEMDESC RI entry is deleted
	 */
	hRIHandle = NULL;

	return eResult;
}

/*!
******************************************************************************

 @Function	RIDeleteListKM

 @Description
            Delete all Resource Information entries and free associated
            memory.

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIDeleteListKM(void)
{
	PVRSRV_ERROR eResult = PVRSRV_OK;


	if (g_pRIHashTable)
	{
		eResult = HASH_Iterate(g_pRIHashTable, (HASH_pfnCallback)_DeleteAllEntries);
		if (eResult == PVRSRV_ERROR_RESOURCE_UNAVAILABLE)
		{
			/*
			 * PVRSRV_ERROR_RESOURCE_UNAVAILABLE is used to stop the Hash iterator when
			 * the hash table gets deleted as a result of deleting the final PMR entry,
			 * so this is not a real error condition...
			 */
			eResult = PVRSRV_OK;
		}
	}

	/* After the run through the RIHashTable that holds the PMR entries there might be
	 * still entries left in the per-process hash table because they were added with
	 * RIWriteProcListEntryKM() and have no PMR parent associated.
	 */
	if (g_pProcHashTable)
	{
		eResult = HASH_Iterate(g_pProcHashTable, (HASH_pfnCallback) _DeleteAllProcEntries);
		if (eResult == PVRSRV_ERROR_RESOURCE_UNAVAILABLE)
		{
			/*
			 * PVRSRV_ERROR_RESOURCE_UNAVAILABLE is used to stop the Hash iterator when
			 * the hash table gets deleted as a result of deleting the final PMR entry,
			 * so this is not a real error condition...
			 */
			eResult = PVRSRV_OK;
		}
	}

	return eResult;
}

/*!
******************************************************************************

 @Function	RIDumpListKM

 @Description
            Dumps out the contents of the RI List entry for the
            specified PMR, and all MEMDESC allocation entries
            in the associated sub linked list.
            At present, output is directed to Kernel log
            via PVR_DPF.

 @input     hPMR - PMR for which RI entry details are to be output

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIDumpListKM(PMR *hPMR)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	/* Acquire RI lock*/
	_RILock();

	eError = _DumpList(hPMR,0);

    /* Release RI lock*/
	_RIUnlock();

	return eError;
}

/*!
******************************************************************************

 @Function	RIGetListEntryKM

 @Description
            Returns pointer to a formatted string with details of the specified
            list entry. If no entry exists (e.g. it may have been deleted
            since the previous call), NULL is returned.

 @input     pid - pid for which RI entry details are to be output
 @input     ppHandle - handle to the entry, if NULL, the first entry will be
                     returned.
 @output    pszEntryString - string to be output for the entry
 @output    hEntry - hEntry will be returned pointing to the next entry
                     (or NULL if there is no next entry)

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_BOOL RIGetListEntryKM(IMG_PID pid,
						  IMG_HANDLE **ppHandle,
						  IMG_CHAR **ppszEntryString)
{
	RI_SUBLIST_ENTRY  *psRISubEntry = NULL;
	uintptr_t     hashData      = 0;
	IMG_PID      	  hashKey  = pid;

	static IMG_CHAR	  ai8DebugfsSummaryString[RI_MAX_DEBUGFS_ENTRY_LEN+1];
	static IMG_UINT64 ui64TotalAlloc = 0;
	static IMG_UINT64 ui64TotalBacked = 0;
	static IMG_UINT64 ui64TotalImport = 0;
	static IMG_UINT64 ui64TotalUnpinned = 0;
	static IMG_BOOL bDisplaySummary = IMG_FALSE;
	static IMG_BOOL bTerminateNextCall = IMG_FALSE;

	if (bDisplaySummary)
	{
		OSSNPrintf((IMG_CHAR *)&ai8DebugfsSummaryString[0],
		            RI_MAX_TEXT_LEN,
		            "Alloc:0x%llx + Imports:0x%llx = Total:0x%llx [Physical: 0x%llx] {Unpinned:0x%llx}\n",
		            (unsigned long long) ui64TotalAlloc,
		            (unsigned long long) ui64TotalImport,
		            (unsigned long long) (ui64TotalAlloc + ui64TotalImport),
		            (unsigned long long) ui64TotalBacked,
		            (unsigned long long) ui64TotalUnpinned);

		*ppszEntryString = &ai8DebugfsSummaryString[0];
		ui64TotalAlloc = 0;
		ui64TotalImport = 0;
		ui64TotalUnpinned = 0;
		ui64TotalBacked = 0;
		bTerminateNextCall = IMG_TRUE;
		bDisplaySummary = IMG_FALSE;
		return IMG_TRUE;
	}

	if (bTerminateNextCall)
	{
		*ppszEntryString = NULL;
		*ppHandle        = NULL;
		bTerminateNextCall = IMG_FALSE;
		return IMG_FALSE;
	}

    /* Acquire RI lock*/
	_RILock();

	/* look-up pid in Hash Table, to obtain first entry for pid */
	hashData = HASH_Retrieve_Extended(g_pProcHashTable, (void *)&hashKey);
	if (hashData)
	{
		if (*ppHandle)
		{
			psRISubEntry = (RI_SUBLIST_ENTRY *)*ppHandle;
			if (psRISubEntry->valid != _VALID_RI_SUBLIST_ENTRY)
			{
				psRISubEntry = NULL;
			}
		}
		else
		{
			psRISubEntry = IMG_CONTAINER_OF((PDLLIST_NODE)hashData, RI_SUBLIST_ENTRY, sProcListNode);
			if (psRISubEntry->valid != _VALID_RI_SUBLIST_ENTRY)
			{
				psRISubEntry = NULL;
			}
		}
	}

	if (psRISubEntry)
	{
		PDLLIST_NODE  psNextProcListNode = dllist_get_next_node(&psRISubEntry->sProcListNode);

		if (psNextProcListNode == NULL  ||
		    psNextProcListNode == (PDLLIST_NODE)hashData)
		{
			bDisplaySummary = IMG_TRUE;
		}


		ui64TotalBacked += psRISubEntry->ui64BackedSize;

		if (psRISubEntry->bIsImport)
		{
			/* If it is a local import we set backed size to 0
			 * so we don't account twice for the same allocation */
			ui64TotalImport += psRISubEntry->ui64BackedSize;
		}
		else
		{
			ui64TotalAlloc += psRISubEntry->ui64Size;
		}


		if (!psRISubEntry->bIsPinned)
		{
			ui64TotalUnpinned += psRISubEntry->ui64Size;
		}

		_GenerateMEMDESCEntryString(psRISubEntry,
		                            IMG_TRUE,
		                            RI_MAX_DEBUGFS_ENTRY_LEN,
		                            (IMG_CHAR *)&ai8DebugfsSummaryString);
		ai8DebugfsSummaryString[RI_MAX_DEBUGFS_ENTRY_LEN] = '\0';

		*ppszEntryString = (IMG_CHAR *)&ai8DebugfsSummaryString;
		*ppHandle        = (IMG_HANDLE)IMG_CONTAINER_OF(psNextProcListNode, RI_SUBLIST_ENTRY, sProcListNode);

	}
	else
	{
		bDisplaySummary = IMG_TRUE;
		if (ui64TotalAlloc == 0)
		{
			ai8DebugfsSummaryString[0] = '\0';
			*ppszEntryString = (IMG_CHAR *)&ai8DebugfsSummaryString;
		}
	}

    /* Release RI lock*/
	_RIUnlock();

	return IMG_TRUE;
}

/* Function used to produce string containing info for MEMDESC RI entries (used for both debugfs and kernel log output) */
static void _GenerateMEMDESCEntryString(RI_SUBLIST_ENTRY *psRISubEntry,
                                            IMG_BOOL bDebugFs,
                                            IMG_UINT16 ui16MaxStrLen,
                                            IMG_CHAR *pszEntryString)
{
	IMG_CHAR 	szProc[RI_PROC_TAG_CHAR_LEN];
	IMG_CHAR 	szImport[RI_IMPORT_TAG_CHAR_LEN];
	IMG_PCHAR   pszAnnotationText = NULL;

	if (!bDebugFs)
	{
		/* we don't include process ID info for debugfs output */
		OSSNPrintf( (IMG_CHAR *)&szProc,
		            RI_PROC_TAG_CHAR_LEN,
		            "[%d: %s]",
		            psRISubEntry->pid,
		            (IMG_CHAR *)psRISubEntry->ai8ProcName);
	}
	if (psRISubEntry->bIsImport)
	{
		OSSNPrintf( (IMG_CHAR *)&szImport,
		            RI_IMPORT_TAG_CHAR_LEN,
		            "{Import from PID %d}",
		            psRISubEntry->psRI->pid);
		/* Set pszAnnotationText to that of the 'parent' PMR RI entry */
		pszAnnotationText = (IMG_PCHAR)psRISubEntry->psRI->ai8TextA;
	}
	else
	{
		if (psRISubEntry->bIsExportable)
		{
			/* Set pszAnnotationText to that of the 'parent' PMR RI entry */
			pszAnnotationText = (IMG_PCHAR)psRISubEntry->psRI->ai8TextA;
		}
		else
		{
			/* Set pszAnnotationText to that of the MEMDESC RI entry */
			pszAnnotationText = (IMG_PCHAR)psRISubEntry->ai8TextB;
		}
	}


	OSSNPrintf(pszEntryString,
	           ui16MaxStrLen,
	           "%s 0x%010llx\t%-80s %s\t0x%010llx [0x%010llx] %s%s%c",
	           (bDebugFs ? "" : "  "),
	           (unsigned long long) (psRISubEntry->sVAddr.uiAddr + psRISubEntry->ui64Offset),
	           pszAnnotationText,
	           (bDebugFs ? "" : (char *)szProc),
	           (unsigned long long) psRISubEntry->ui64Size,
	           (unsigned long long) psRISubEntry->ui64BackedSize,
	           (psRISubEntry->bIsImport ? (char *)&szImport : ""),
	           (psRISubEntry->bIsPinned ? "" : "{Unpinned}"),
	           (bDebugFs ? '\n' : ' '));
}

/*!
******************************************************************************

 @Function	_DumpList
 @Description
            Dumps out RI List entries according to parameters passed.

 @input     hPMR - If not NULL, function will output the RI entries for
                   the specified PMR only
 @input     pid - If non-zero, the function will only output MEMDESC RI
  	  	  	  	  entries made by the process with ID pid.
                  If zero, all MEMDESC RI entries will be output.

 @Return	PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR _DumpList(PMR *hPMR, IMG_PID pid)
{
	RI_LIST_ENTRY *psRIEntry = NULL;
	RI_SUBLIST_ENTRY *psRISubEntry = NULL;
	IMG_UINT16 ui16SubEntriesParsed = 0;
	uintptr_t hashData = 0;
	IMG_PID		  hashKey;
	PMR			*pPMRHashKey = hPMR;
	IMG_BOOL 	bDisplayedThisPMR = IMG_FALSE;


	if (!hPMR)
	{
		/* NULL handle provided */
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	if (g_pRIHashTable && g_pProcHashTable)
	{
		if (pid != 0)
		{
			/* look-up pid in Hash Table */
			hashKey = pid;
			hashData = HASH_Retrieve_Extended (g_pProcHashTable, (void *)&hashKey);
			if (hashData)
			{
				psRISubEntry = IMG_CONTAINER_OF((PDLLIST_NODE)hashData, RI_SUBLIST_ENTRY, sProcListNode);
				if (psRISubEntry)
				{
					psRIEntry = psRISubEntry->psRI;
				}
			}
		}
		else
		{
			/* look-up hPMR in Hash Table */
			hashData = HASH_Retrieve_Extended (g_pRIHashTable, (void *)&pPMRHashKey);
			psRIEntry = (RI_LIST_ENTRY *)hashData;
		}
		if (!psRIEntry)
		{
			/* No entry found in hash table */
			return PVRSRV_ERROR_NOT_FOUND;
		}
		while (psRIEntry)
		{
			bDisplayedThisPMR = IMG_FALSE;
			/* Output details for RI entry */
			if (!pid)
			{
				_RIOutput (("%s (0x%p) suballocs:%d size:0x%llx",
				            psRIEntry->ai8TextA,
				            psRIEntry->hPMR,
				            (IMG_UINT)psRIEntry->ui16SubListCount,
				            (unsigned long long)psRIEntry->ui64LogicalSize));
				bDisplayedThisPMR = IMG_TRUE;
			}
			ui16SubEntriesParsed = 0;
			if(psRIEntry->ui16SubListCount)
			{
#if _DUMP_LINKEDLIST_INFO
				_RIOutput (("RI LIST: {sSubListFirst.psNextNode:0x%x}",
				            (IMG_UINT)psRIEntry->sSubListFirst.psNextNode));
#endif /* _DUMP_LINKEDLIST_INFO */
				if (!pid)
				{
					psRISubEntry = IMG_CONTAINER_OF(dllist_get_next_node(&(psRIEntry->sSubListFirst)),
					                                RI_SUBLIST_ENTRY, sListNode);
				}
				/* Traverse RI sublist and output details for each entry */
				while (psRISubEntry && (ui16SubEntriesParsed < psRIEntry->ui16SubListCount))
				{
					if (!bDisplayedThisPMR)
					{
						_RIOutput (("%s (0x%p) suballocs:%d size:0x%llx",
						            psRIEntry->ai8TextA,
						            psRIEntry->hPMR,
						            (IMG_UINT)psRIEntry->ui16SubListCount,
						            (unsigned long long)psRIEntry->ui64LogicalSize));
						bDisplayedThisPMR = IMG_TRUE;
					}
#if _DUMP_LINKEDLIST_INFO
					_RIOutput (("RI LIST:    [this subentry:0x%x]",(IMG_UINT)psRISubEntry));
					_RIOutput (("RI LIST:     psRI:0x%x",(IMG_UINT32)psRISubEntry->psRI));
#endif /* _DUMP_LINKEDLIST_INFO */

					{
						IMG_CHAR szEntryString[RI_MAX_MEMDESC_RI_ENTRY_LEN];

						_GenerateMEMDESCEntryString(psRISubEntry,
						                            IMG_FALSE,
						                            RI_MAX_MEMDESC_RI_ENTRY_LEN,
						                            (IMG_CHAR *)&szEntryString);
						szEntryString[RI_MAX_MEMDESC_RI_ENTRY_LEN-1] = '\0';
						_RIOutput (("%s",(IMG_CHAR *)&szEntryString));
					}

					if (pid)
					{
						if((dllist_get_next_node(&(psRISubEntry->sProcListNode)) == 0) ||
						   (dllist_get_next_node(&(psRISubEntry->sProcListNode)) == (PDLLIST_NODE)hashData))
						{
							psRISubEntry = NULL;
						}
						else
						{
							psRISubEntry = IMG_CONTAINER_OF(dllist_get_next_node(&(psRISubEntry->sProcListNode)),
							                                RI_SUBLIST_ENTRY, sProcListNode);
							if (psRISubEntry)
							{
								if (psRIEntry != psRISubEntry->psRI)
								{
									/*
									 * The next MEMDESC in the process linked list is in a different PMR
									 */
									psRIEntry = psRISubEntry->psRI;
									bDisplayedThisPMR = IMG_FALSE;
								}
							}
						}
					}
					else
					{
						ui16SubEntriesParsed++;
						psRISubEntry = IMG_CONTAINER_OF(dllist_get_next_node(&(psRISubEntry->sListNode)),
						                                RI_SUBLIST_ENTRY, sListNode);
					}
				}
			}
			if (!pid)
			{
				if (ui16SubEntriesParsed != psRIEntry->ui16SubListCount)
				{
					/*
					 * Output error message as sublist does not contain the
					 * number of entries indicated by sublist count
					 */
					_RIOutput (("RI ERROR: RI sublist contains %d entries, not %d entries",
					            ui16SubEntriesParsed,psRIEntry->ui16SubListCount));
				}
				else if (psRIEntry->ui16SubListCount && !dllist_get_next_node(&(psRIEntry->sSubListFirst)))
				{
					/*
					 * Output error message as sublist is empty but sublist count
					 * is not zero
					 */
					_RIOutput (("RI ERROR: ui16SubListCount=%d for empty RI sublist",
					            psRIEntry->ui16SubListCount));
				}
			}
			psRIEntry = NULL;
		}
	}
	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	RIDumpAllKM

 @Description
            Dumps out the contents of all RI List entries (i.e. for all
            MEMDESC allocations for each PMR).
            At present, output is directed to Kernel log
            via PVR_DPF.

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIDumpAllKM(void)
{
	if (g_pRIHashTable)
	{
		return HASH_Iterate(g_pRIHashTable, (HASH_pfnCallback)_DumpAllEntries);
	}
	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	RIDumpProcessKM

 @Description
            Dumps out the contents of all MEMDESC RI List entries (for every
            PMR) which have been allocate by the specified process only.
            At present, output is directed to Kernel log
            via PVR_DPF.

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIDumpProcessKM(IMG_PID pid)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32  dummyPMR;

	if (g_pProcHashTable)
	{
		/* Acquire RI lock*/
		_RILock();

		eError = _DumpList((PMR *)&dummyPMR,pid);

	    /* Release RI lock*/
		_RIUnlock();
	}
	return eError;
}

#if defined(DEBUG)
/*!
******************************************************************************

 @Function	_DumpList
 @Description
            Dumps out RI List entries according to parameters passed.

 @input     hPMR - If not NULL, function will output the RI entries for
                   the specified PMR only
 @input     pid - If non-zero, the function will only output MEMDESC RI
  	  	  	  	  entries made by the process with ID pid.
                  If zero, all MEMDESC RI entries will be output.

 @Return	PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR _DumpProcessList(PMR *hPMR,
									 IMG_PID pid,
									 IMG_UINT64 ui64Offset,
									 IMG_DEV_VIRTADDR *psDevVAddr)
{
	RI_LIST_ENTRY *psRIEntry = NULL;
	RI_SUBLIST_ENTRY *psRISubEntry = NULL;
	IMG_UINT16 ui16SubEntriesParsed = 0;
	uintptr_t hashData = 0;
	PMR *pPMRHashKey = hPMR;
	PVRSRV_ERROR eError = PVRSRV_ERROR_INVALID_PARAMS;

	psDevVAddr->uiAddr = 0;

	if (!hPMR)
	{
		/* NULL handle provided */
		return eError;
	}

	if (g_pRIHashTable && g_pProcHashTable)
	{
		PVR_ASSERT(hPMR && pid);

		/* look-up hPMR in Hash Table */
		hashData = HASH_Retrieve_Extended (g_pRIHashTable, (void *)&pPMRHashKey);
		psRIEntry = (RI_LIST_ENTRY *)hashData;

		if (!psRIEntry)
		{
			/* No entry found in hash table */
			return PVRSRV_ERROR_NOT_FOUND;
		}

		if (psRIEntry->ui16SubListCount)
		{
			psRISubEntry = IMG_CONTAINER_OF(dllist_get_next_node(&(psRIEntry->sSubListFirst)),
											RI_SUBLIST_ENTRY, sListNode);

			/* Traverse RI sublist and output details for each entry */
			while (psRISubEntry && (ui16SubEntriesParsed < psRIEntry->ui16SubListCount))
			{
				if (pid == psRISubEntry->pid)
				{
					IMG_UINT64 ui64StartOffset = psRISubEntry->ui64Offset;
					IMG_UINT64 ui64EndOffset = psRISubEntry->ui64Offset + psRISubEntry->ui64Size;

					if (ui64Offset >= ui64StartOffset && ui64Offset < ui64EndOffset)
					{
						psDevVAddr->uiAddr = psRISubEntry->sVAddr.uiAddr;
						return PVRSRV_OK;
					}
				}

				ui16SubEntriesParsed++;
				psRISubEntry = IMG_CONTAINER_OF(dllist_get_next_node(&(psRISubEntry->sListNode)),
												RI_SUBLIST_ENTRY, sListNode);
			}
		}
	}

	return eError;
}

/*!
******************************************************************************

 @Function	RIDumpProcessListKM

 @Description
            Dumps out selected contents of all MEMDESC RI List entries (for a
            PMR) which have been allocate by the specified process only.

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIDumpProcessListKM(PMR *hPMR,
								 IMG_PID pid,
								 IMG_UINT64 ui64Offset,
								 IMG_DEV_VIRTADDR *psDevVAddr)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (g_pProcHashTable)
	{
		/* Acquire RI lock*/
		_RILock();

		eError = _DumpProcessList(hPMR,
								  pid,
								  ui64Offset,
								  psDevVAddr);

		/* Release RI lock*/
		_RIUnlock();
	}

	return eError;
}
#endif

static PVRSRV_ERROR _DumpAllEntries (uintptr_t k, uintptr_t v)
{
	RI_LIST_ENTRY *psRIEntry = (RI_LIST_ENTRY *)v;

	PVR_UNREFERENCED_PARAMETER (k);

	return RIDumpListKM(psRIEntry->hPMR);
}

static PVRSRV_ERROR _DeleteAllEntries (uintptr_t k, uintptr_t v)
{
	RI_LIST_ENTRY *psRIEntry = (RI_LIST_ENTRY *)v;
	RI_SUBLIST_ENTRY *psRISubEntry;
	PVRSRV_ERROR eResult = PVRSRV_OK;

	PVR_UNREFERENCED_PARAMETER (k);

	while ((eResult == PVRSRV_OK) && (psRIEntry->ui16SubListCount > 0))
	{
		psRISubEntry = IMG_CONTAINER_OF(dllist_get_next_node(&(psRIEntry->sSubListFirst)), RI_SUBLIST_ENTRY, sListNode);
		eResult = RIDeleteMEMDESCEntryKM((RI_HANDLE)psRISubEntry);
	}
	if (eResult == PVRSRV_OK)
	{
		eResult = RIDeletePMREntryKM((RI_HANDLE)psRIEntry);
		/*
		 * If we've deleted the Hash table, return
		 * an error to stop the iterator...
		 */
		if (!g_pRIHashTable)
		{
			eResult = PVRSRV_ERROR_RESOURCE_UNAVAILABLE;
		}
	}
	return eResult;
}

static PVRSRV_ERROR _DeleteAllProcEntries (uintptr_t k, uintptr_t v)
{
	RI_SUBLIST_ENTRY *psRISubEntry = (RI_SUBLIST_ENTRY *)v;
	PVRSRV_ERROR eResult = PVRSRV_OK;

	PVR_UNREFERENCED_PARAMETER (k);

	eResult = RIDeleteMEMDESCEntryKM((RI_HANDLE) psRISubEntry);
	if (eResult == PVRSRV_OK && !g_pProcHashTable)
	{
		/*
		 * If we've deleted the Hash table, return
		 * an error to stop the iterator...
		 */
		eResult = PVRSRV_ERROR_RESOURCE_UNAVAILABLE;
	}

	return eResult;
}

#endif /* if defined(PVR_RI_DEBUG) */
