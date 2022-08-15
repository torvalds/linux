/*************************************************************************/ /*!
@File           ri_server.c
@Title          Resource Information (RI) server implementation
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Resource Information (RI) server functions
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

#if defined(__linux__)
 #include <linux/version.h>
 #if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
  #include <linux/stdarg.h>
 #else
  #include <stdarg.h>
 #endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0) */
#else
 #include <stdarg.h>
#endif /* __linux__ */
#include "img_defs.h"
#include "allocmem.h"
#include "pvr_debug.h"
#include "pvrsrv_error.h"
#include "osfunc.h"

#include "srvkm.h"
#include "lock.h"

/* services/include */
#include "pvr_ricommon.h"

/* services/server/include/ */
#include "ri_server.h"

/* services/include/shared/ */
#include "hash.h"
/* services/shared/include/ */
#include "dllist.h"

#include "pmr.h"

/* include/device.h */
#include "device.h"

#if !defined(RI_UNIT_TEST)
#include "pvrsrv.h"
#endif


#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)

#define USE_RI_LOCK		1

/*
 * Initial size use for Hash table. (Used to index the RI list entries).
 */
#define _RI_INITIAL_HASH_TABLE_SIZE	64

/*
 * Values written to the 'valid' field of RI structures when created and
 * cleared prior to being destroyed. The code can then check this value
 * before accessing the provided pointer contents as a valid RI structure.
 */
#define _VALID_RI_LIST_ENTRY	0x66bccb66
#define _VALID_RI_SUBLIST_ENTRY	0x77cddc77
#define _INVALID				0x00000000

/*
 * If this define is set to 1, details of the linked lists (addresses,
 * prev/next ptrs, etc) are also output when function RIDumpList() is called.
 */
#define _DUMP_LINKEDLIST_INFO		0


typedef IMG_UINT64 _RI_BASE_T;


/* No +1 in SIZE macros since sizeof includes \0 byte in size */

#define RI_PROC_BUF_SIZE    16

#define RI_MEMDESC_SUM_FRMT     "PID %d %s MEMDESCs Alloc'd:0x%010" IMG_UINT64_FMTSPECx " (%" IMG_UINT64_FMTSPEC "K) + "\
                                                  "Imported:0x%010" IMG_UINT64_FMTSPECx " (%" IMG_UINT64_FMTSPEC "K) = "\
                                                     "Total:0x%010" IMG_UINT64_FMTSPECx " (%" IMG_UINT64_FMTSPEC "K)\n"
#define RI_MEMDESC_SUM_BUF_SIZE (sizeof(RI_MEMDESC_SUM_FRMT)+5+RI_PROC_BUF_SIZE+30+60)


#define RI_PMR_SUM_FRMT     "PID %d %s PMRs Alloc'd:0x%010" IMG_UINT64_FMTSPECx ", %" IMG_UINT64_FMTSPEC "K  "\
                                        "[Physical: 0x%010" IMG_UINT64_FMTSPECx ", %" IMG_UINT64_FMTSPEC "K]\n"
#define RI_PMR_SUM_BUF_SIZE (sizeof(RI_PMR_SUM_FRMT)+(20+40))

#define RI_PMR_ENTRY_FRMT      "%%sPID:%%-5d <%%p>\t%%-%ds\t0x%%010" IMG_UINT64_FMTSPECx "\t[0x%%010" IMG_UINT64_FMTSPECx "]\t%%c"
#define RI_PMR_ENTRY_BUF_SIZE  (sizeof(RI_PMR_ENTRY_FRMT)+(3+5+16+PVR_ANNOTATION_MAX_LEN+10+10))
#define RI_PMR_ENTRY_FRMT_SIZE (sizeof(RI_PMR_ENTRY_FRMT))

/* Use %5d rather than %d so the output aligns in server/kernel.log, debugFS sees extra spaces */
#define RI_MEMDESC_ENTRY_PROC_FRMT        "[%5d:%s]"
#define RI_MEMDESC_ENTRY_PROC_BUF_SIZE    (sizeof(RI_MEMDESC_ENTRY_PROC_FRMT)+5+16)

#define RI_SYS_ALLOC_IMPORT_FRMT      "{Import from PID %d}"
#define RI_SYS_ALLOC_IMPORT_FRMT_SIZE (sizeof(RI_SYS_ALLOC_IMPORT_FRMT)+5)
static IMG_CHAR g_szSysAllocImport[RI_SYS_ALLOC_IMPORT_FRMT_SIZE];

#define RI_MEMDESC_ENTRY_IMPORT_FRMT     "{Import from PID %d}"
#define RI_MEMDESC_ENTRY_IMPORT_BUF_SIZE (sizeof(RI_MEMDESC_ENTRY_IMPORT_FRMT)+5)

#define RI_MEMDESC_ENTRY_UNPINNED_FRMT     "{Unpinned}"
#define RI_MEMDESC_ENTRY_UNPINNED_BUF_SIZE (sizeof(RI_MEMDESC_ENTRY_UNPINNED_FRMT))

#define RI_MEMDESC_ENTRY_FRMT      "%%sPID:%%-5d 0x%%010" IMG_UINT64_FMTSPECx "\t%%-%ds %%s\t0x%%010" IMG_UINT64_FMTSPECx "\t<%%p> %%s%%s%%s%%c"
#define RI_MEMDESC_ENTRY_BUF_SIZE  (sizeof(RI_MEMDESC_ENTRY_FRMT)+(3+5+10+PVR_ANNOTATION_MAX_LEN+RI_MEMDESC_ENTRY_PROC_BUF_SIZE+16+\
                                               RI_MEMDESC_ENTRY_IMPORT_BUF_SIZE+RI_SYS_ALLOC_IMPORT_FRMT_SIZE+RI_MEMDESC_ENTRY_UNPINNED_BUF_SIZE))
#define RI_MEMDESC_ENTRY_FRMT_SIZE (sizeof(RI_MEMDESC_ENTRY_FRMT))


#define RI_FRMT_SIZE_MAX (MAX(RI_MEMDESC_ENTRY_BUF_SIZE,\
                              MAX(RI_PMR_ENTRY_BUF_SIZE,\
                                  MAX(RI_MEMDESC_SUM_BUF_SIZE,\
                                      RI_PMR_SUM_BUF_SIZE))))




/* Structure used to make linked sublist of memory allocations (MEMDESC) */
struct _RI_SUBLIST_ENTRY_
{
	DLLIST_NODE				sListNode;
	struct _RI_LIST_ENTRY_	*psRI;
	IMG_UINT32				valid;
	IMG_BOOL				bIsImport;
	IMG_BOOL				bIsSuballoc;
	IMG_PID					pid;
	IMG_CHAR				ai8ProcName[RI_PROC_BUF_SIZE];
	IMG_DEV_VIRTADDR		sVAddr;
	IMG_UINT64				ui64Offset;
	IMG_UINT64				ui64Size;
	IMG_CHAR				ai8TextB[DEVMEM_ANNOTATION_MAX_LEN+1];
	DLLIST_NODE				sProcListNode;
};

/*
 * Structure used to make linked list of PMRs. Sublists of allocations
 * (MEMDESCs) made from these PMRs are chained off these entries.
 */
struct _RI_LIST_ENTRY_
{
	DLLIST_NODE				sListNode;
	DLLIST_NODE				sSysAllocListNode;
	DLLIST_NODE				sSubListFirst;
	IMG_UINT32				valid;
	PMR						*psPMR;
	IMG_PID					pid;
	IMG_CHAR				ai8ProcName[RI_PROC_BUF_SIZE];
	IMG_UINT16				ui16SubListCount;
	IMG_UINT16				ui16MaxSubListCount;
	IMG_UINT32				ui32RIPMRFlags; /* Flags used to indicate the type of allocation */
	IMG_UINT32				ui32Flags; /* Flags used to indicate if PMR appears in ri debugfs output */
};

typedef struct _RI_LIST_ENTRY_ RI_LIST_ENTRY;
typedef struct _RI_SUBLIST_ENTRY_ RI_SUBLIST_ENTRY;

static IMG_UINT16	g_ui16RICount;
static HASH_TABLE	*g_pRIHashTable;
static IMG_UINT16	g_ui16ProcCount;
static HASH_TABLE	*g_pProcHashTable;

static POS_LOCK		g_hRILock;

/* Linked list of PMR allocations made against the PVR_SYS_ALLOC_PID and lock
 * to prevent concurrent access to it.
 */
static POS_LOCK		g_hSysAllocPidListLock;
static DLLIST_NODE	g_sSysAllocPidListHead;

/*
 * Flag used to indicate if RILock should be destroyed when final PMR entry is
 * deleted, i.e. if RIDeInitKM() has already been called before that point but
 * the handle manager has deferred deletion of RI entries.
 */
static IMG_BOOL		bRIDeInitDeferred = IMG_FALSE;

/*
 * Used as head of linked-list of PMR RI entries - this is useful when we wish
 * to iterate all PMR list entries (when we don't have a PMR ref)
 */
static DLLIST_NODE	sListFirst;

/* Function used to produce string containing info for MEMDESC RI entries (used for both debugfs and kernel log output) */
static void _GenerateMEMDESCEntryString(RI_SUBLIST_ENTRY *psRISubEntry, IMG_BOOL bDebugFs, IMG_UINT16 ui16MaxStrLen, IMG_CHAR *pszEntryString);
/* Function used to produce string containing info for PMR RI entries (used for both debugfs and kernel log output) */
static void _GeneratePMREntryString(RI_LIST_ENTRY *psRIEntry, IMG_BOOL bDebugFs, IMG_UINT16 ui16MaxStrLen, IMG_CHAR *pszEntryString);

static PVRSRV_ERROR _DumpAllEntries (uintptr_t k, uintptr_t v, void* pvPriv);
static PVRSRV_ERROR _DeleteAllEntries (uintptr_t k, uintptr_t v, void* pvPriv);
static PVRSRV_ERROR _DeleteAllProcEntries (uintptr_t k, uintptr_t v, void* pvPriv);
static PVRSRV_ERROR _DumpList(PMR *psPMR, IMG_PID pid);
#define _RIOutput(x) PVR_LOG(x)

#define RI_FLAG_PMR_PHYS_COUNTED_BY_DEBUGFS	0x1
#define RI_FLAG_SYSALLOC_PMR				0x2

static IMG_UINT32
_ProcHashFunc(size_t uKeySize, void *pKey, IMG_UINT32 uHashTabLen);

static IMG_UINT32
_ProcHashFunc(size_t uKeySize, void *pKey, IMG_UINT32 uHashTabLen)
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

static IMG_BOOL
_ProcHashComp(size_t uKeySize, void *pKey1, void *pKey2);

static IMG_BOOL
_ProcHashComp(size_t uKeySize, void *pKey1, void *pKey2)
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

/* This value maintains a count of the number of PMRs attributed to the
 * PVR_SYS_ALLOC_PID. Access to this value is protected by g_hRILock, so it
 * does not need to be an ATOMIC_T.
 */
static IMG_UINT32 g_ui32SysAllocPMRCount;


PVRSRV_ERROR RIInitKM(void)
{
	IMG_INT iCharsWritten;
	PVRSRV_ERROR eError;

	bRIDeInitDeferred = IMG_FALSE;

	iCharsWritten = OSSNPrintf(g_szSysAllocImport,
	            RI_SYS_ALLOC_IMPORT_FRMT_SIZE,
	            RI_SYS_ALLOC_IMPORT_FRMT,
	            PVR_SYS_ALLOC_PID);
	PVR_LOG_IF_FALSE((iCharsWritten>0 && iCharsWritten<(IMG_INT32)RI_SYS_ALLOC_IMPORT_FRMT_SIZE), \
			"OSSNPrintf failed to initialise g_szSysAllocImport");

	eError = OSLockCreate(&g_hSysAllocPidListLock);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: OSLockCreate (g_hSysAllocPidListLock) failed (returned %d)",
		         __func__,
		         eError));
	}
	dllist_init(&(g_sSysAllocPidListHead));
#if (USE_RI_LOCK == 1)
	eError = OSLockCreate(&g_hRILock);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: OSLockCreate (g_hRILock) failed (returned %d)",
		         __func__,
		         eError));
	}
#endif
	return eError;
}
void RIDeInitKM(void)
{
#if (USE_RI_LOCK == 1)
	if (g_ui16RICount > 0)
	{
		PVR_DPF((PVR_DBG_WARNING,
		         "%s: called with %d entries remaining - deferring OSLockDestroy()",
		         __func__,
		         g_ui16RICount));
		bRIDeInitDeferred = IMG_TRUE;
	}
	else
	{
		OSLockDestroy(g_hRILock);
		OSLockDestroy(g_hSysAllocPidListLock);
	}
#endif
}

/*!
*******************************************************************************

 @Function	RILockAcquireKM

 @Description
            Acquires the RI Lock (which protects the integrity of the RI
            linked lists). Caller will be suspended until lock is acquired.

 @Return	None

******************************************************************************/
void RILockAcquireKM(void)
{
	_RILock();
}

/*!
*******************************************************************************

 @Function	RILockReleaseKM

 @Description
            Releases the RI Lock (which protects the integrity of the RI
            linked lists).

 @Return	None

******************************************************************************/
void RILockReleaseKM(void)
{
	_RIUnlock();
}

/*!
*******************************************************************************

 @Function	RIWritePMREntryWithOwnerKM

 @Description
            Writes a new Resource Information list entry.
            The new entry will be inserted at the head of the list of
            PMR RI entries and assigned the values provided.

 @input     psPMR - Reference (handle) to the PMR to which this reference relates

 @input     ui32Owner - PID of the process which owns the allocation. This
                        may not be the current process (e.g. a request to
                        grow a buffer may happen in the context of a kernel
                        thread, or we may import further resource for a
                        suballocation made from the FW heap which can then
                        also be utilized by other processes)

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIWritePMREntryWithOwnerKM(PMR *psPMR,
                                        IMG_PID ui32Owner)
{
	PMR *pPMRHashKey = psPMR;
	RI_LIST_ENTRY *psRIEntry;
	uintptr_t hashData;

	/* if Hash table has not been created, create it now */
	if (!g_pRIHashTable)
	{
		g_pRIHashTable = HASH_Create_Extended(_RI_INITIAL_HASH_TABLE_SIZE, sizeof(PMR*), HASH_Func_Default, HASH_Key_Comp_Default);
		g_pProcHashTable = HASH_Create_Extended(_RI_INITIAL_HASH_TABLE_SIZE, sizeof(IMG_PID), _ProcHashFunc, _ProcHashComp);
	}
	PVR_RETURN_IF_NOMEM(g_pRIHashTable);
	PVR_RETURN_IF_NOMEM(g_pProcHashTable);

	PVR_RETURN_IF_INVALID_PARAM(psPMR);

	/* Acquire RI Lock */
	_RILock();

	/* Look-up psPMR in Hash Table */
	hashData = HASH_Retrieve_Extended (g_pRIHashTable, (void *)&pPMRHashKey);
	psRIEntry = (RI_LIST_ENTRY *)hashData;
	if (!psRIEntry)
	{
		/*
		 * If failed to find a matching existing entry, create a new one
		 */
		psRIEntry = (RI_LIST_ENTRY *)OSAllocZMemNoStats(sizeof(RI_LIST_ENTRY));
		if (!psRIEntry)
		{
			/* Release RI Lock */
			_RIUnlock();
			/* Error - no memory to allocate for new RI entry */
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}
		else
		{
			PMR_FLAGS_T uiPMRFlags = PMR_Flags(psPMR);
			PVRSRV_DEVICE_NODE *psDeviceNode = (PVRSRV_DEVICE_NODE *)PMR_DeviceNode(psPMR);

			/*
			 * Add new RI Entry
			 */
			if (g_ui16RICount == 0)
			{
				/* Initialise PMR entry linked-list head */
				dllist_init(&sListFirst);
			}
			g_ui16RICount++;

			dllist_init (&(psRIEntry->sSysAllocListNode));
			dllist_init (&(psRIEntry->sSubListFirst));
			psRIEntry->ui16SubListCount = 0;
			psRIEntry->ui16MaxSubListCount = 0;
			psRIEntry->valid = _VALID_RI_LIST_ENTRY;

			/* Check if this PMR should be accounted for under the
			 * PVR_SYS_ALLOC_PID debugFS entry. This should happen if
			 * we are in the driver init phase, the flags indicate
			 * this is a FW Main allocation (made from FW heap)
			 * or the owner PID is PVR_SYS_ALLOC_PID.
			 * Also record host dev node allocs on the system PID.
			 */
			if (psDeviceNode->eDevState == PVRSRV_DEVICE_STATE_INIT ||
				PVRSRV_CHECK_FW_MAIN(uiPMRFlags) ||
				ui32Owner == PVR_SYS_ALLOC_PID ||
				psDeviceNode == PVRSRVGetPVRSRVData()->psHostMemDeviceNode)
			{
				psRIEntry->ui32RIPMRFlags = RI_FLAG_SYSALLOC_PMR;
				OSSNPrintf(psRIEntry->ai8ProcName,
						RI_PROC_BUF_SIZE,
						"SysProc");
				psRIEntry->pid = PVR_SYS_ALLOC_PID;
				OSLockAcquire(g_hSysAllocPidListLock);
				/* Add this psRIEntry to the list of entries for PVR_SYS_ALLOC_PID */
				dllist_add_to_tail(&g_sSysAllocPidListHead, (PDLLIST_NODE)&(psRIEntry->sSysAllocListNode));
				OSLockRelease(g_hSysAllocPidListLock);
				g_ui32SysAllocPMRCount++;
			}
			else
			{
				psRIEntry->ui32RIPMRFlags = 0;
				psRIEntry->pid = ui32Owner;
			}

			OSSNPrintf(psRIEntry->ai8ProcName,
					RI_PROC_BUF_SIZE,
					"%s",
					OSGetCurrentClientProcessNameKM());
			/* Add PMR entry to linked-list of all PMR entries */
			dllist_init (&(psRIEntry->sListNode));
			dllist_add_to_tail(&sListFirst, (PDLLIST_NODE)&(psRIEntry->sListNode));
		}

		psRIEntry->psPMR = psPMR;
		psRIEntry->ui32Flags = 0;

		/* Create index entry in Hash Table */
		HASH_Insert_Extended (g_pRIHashTable, (void *)&pPMRHashKey, (uintptr_t)psRIEntry);

		/* Store phRIHandle in PMR structure, so it can delete the associated RI entry when it destroys the PMR */
		PMRStoreRIHandle(psPMR, psRIEntry);
	}
	/* Release RI Lock */
	_RIUnlock();

	return PVRSRV_OK;
}

/*!
*******************************************************************************

 @Function	RIWritePMREntryKM

 @Description
            Writes a new Resource Information list entry.
            The new entry will be inserted at the head of the list of
            PMR RI entries and assigned the values provided.

 @input     psPMR - Reference (handle) to the PMR to which this reference relates

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIWritePMREntryKM(PMR *psPMR)
{
	return RIWritePMREntryWithOwnerKM(psPMR,
	                                  OSGetCurrentClientProcessIDKM());
}

/*!
*******************************************************************************

 @Function	RIWriteMEMDESCEntryKM

 @Description
            Writes a new Resource Information sublist entry.
            The new entry will be inserted at the head of the sublist of
            the indicated PMR list entry, and assigned the values provided.

 @input     psPMR - Reference (handle) to the PMR to which this MEMDESC RI entry relates
 @input     ui32TextBSize - Length of string provided in psz8TextB parameter
 @input     psz8TextB - String describing this secondary reference (may be null)
 @input     ui64Offset - Offset from the start of the PMR at which this allocation begins
 @input     ui64Size - Size of this allocation
 @input     bIsImport - Flag indicating if this is an allocation or an import
 @input     bIsSuballoc - Flag indicating if this is a sub-allocation
 @output    phRIHandle - Handle to the created RI entry

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIWriteMEMDESCEntryKM(PMR *psPMR,
                                   IMG_UINT32 ui32TextBSize,
                                   const IMG_CHAR *psz8TextB,
                                   IMG_UINT64 ui64Offset,
                                   IMG_UINT64 ui64Size,
                                   IMG_BOOL bIsImport,
                                   IMG_BOOL bIsSuballoc,
                                   RI_HANDLE *phRIHandle)
{
	RI_SUBLIST_ENTRY *psRISubEntry;
	RI_LIST_ENTRY *psRIEntry;
	PMR *pPMRHashKey = psPMR;
	uintptr_t hashData;
	IMG_PID	pid;

	/* Check Hash tables have been created (meaning at least one PMR has been defined) */
	PVR_RETURN_IF_INVALID_PARAM(g_pRIHashTable);
	PVR_RETURN_IF_INVALID_PARAM(g_pProcHashTable);

	PVR_RETURN_IF_INVALID_PARAM(psPMR);
	PVR_RETURN_IF_INVALID_PARAM(phRIHandle);

	/* Acquire RI Lock */
	_RILock();

	*phRIHandle = NULL;

	/* Look-up psPMR in Hash Table */
	hashData = HASH_Retrieve_Extended (g_pRIHashTable, (void *)&pPMRHashKey);
	psRIEntry = (RI_LIST_ENTRY *)hashData;
	if (!psRIEntry)
	{
		/* Release RI Lock */
		_RIUnlock();
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psRISubEntry = (RI_SUBLIST_ENTRY *)OSAllocZMemNoStats(sizeof(RI_SUBLIST_ENTRY));
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

	/* If allocation is made during device or driver initialisation,
	 * track the MEMDESC entry under PVR_SYS_ALLOC_PID, otherwise use
	 * the current PID.
	 * Record host dev node allocations on the system PID.
	 */
	{
		PVRSRV_DEVICE_NODE *psDeviceNode = (PVRSRV_DEVICE_NODE *)PMR_DeviceNode(psRISubEntry->psRI->psPMR);

		if (psDeviceNode->eDevState == PVRSRV_DEVICE_STATE_INIT ||
			psDeviceNode == PVRSRVGetPVRSRVData()->psHostMemDeviceNode)
		{
			psRISubEntry->pid = psRISubEntry->psRI->pid;
		}
		else
		{
			psRISubEntry->pid = OSGetCurrentClientProcessIDKM();
		}
	}

	if (ui32TextBSize > sizeof(psRISubEntry->ai8TextB)-1)
	{
		PVR_DPF((PVR_DBG_WARNING,
				 "%s: TextBSize too long (%u). Text will be truncated "
				 "to %zu characters", __func__,
				 ui32TextBSize, sizeof(psRISubEntry->ai8TextB)-1));
	}

	/* copy ai8TextB field data */
	OSSNPrintf((IMG_CHAR *)psRISubEntry->ai8TextB, sizeof(psRISubEntry->ai8TextB), "%s", psz8TextB);

	psRISubEntry->ui64Offset = ui64Offset;
	psRISubEntry->ui64Size = ui64Size;
	psRISubEntry->bIsImport = bIsImport;
	psRISubEntry->bIsSuballoc = bIsSuballoc;
	OSSNPrintf((IMG_CHAR *)psRISubEntry->ai8ProcName, RI_PROC_BUF_SIZE, "%s", OSGetCurrentClientProcessNameKM());
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
*******************************************************************************

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

 @input     psz8TextB - String describing this secondary reference (may be null)
 @input     ui64Size - Size of this allocation
 @input     ui64DevVAddr - Virtual address of this entry
 @output    phRIHandle - Handle to the created RI entry

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIWriteProcListEntryKM(IMG_UINT32 ui32TextBSize,
                                    const IMG_CHAR *psz8TextB,
                                    IMG_UINT64 ui64Size,
                                    IMG_UINT64 ui64DevVAddr,
                                    RI_HANDLE *phRIHandle)
{
	uintptr_t hashData = 0;
	IMG_PID		pid;
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

	psRISubEntry = (RI_SUBLIST_ENTRY *)OSAllocZMemNoStats(sizeof(RI_SUBLIST_ENTRY));
	if (!psRISubEntry)
	{
		/* Release RI Lock */
		_RIUnlock();
		/* Error - no memory to allocate for new RI sublist entry */
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psRISubEntry->valid = _VALID_RI_SUBLIST_ENTRY;

	psRISubEntry->pid = OSGetCurrentClientProcessIDKM();

	if (ui32TextBSize > sizeof(psRISubEntry->ai8TextB)-1)
	{
		PVR_DPF((PVR_DBG_WARNING,
		         "%s: TextBSize too long (%u). Text will be truncated "
		         "to %zu characters", __func__,
		         ui32TextBSize, sizeof(psRISubEntry->ai8TextB)-1));
	}

	/* copy ai8TextB field data */
	OSSNPrintf((IMG_CHAR *)psRISubEntry->ai8TextB, sizeof(psRISubEntry->ai8TextB), "%s", psz8TextB);

	psRISubEntry->ui64Offset = 0;
	psRISubEntry->ui64Size = ui64Size;
	psRISubEntry->sVAddr.uiAddr = ui64DevVAddr;
	psRISubEntry->bIsImport = IMG_FALSE;
	psRISubEntry->bIsSuballoc = IMG_FALSE;
	OSSNPrintf((IMG_CHAR *)psRISubEntry->ai8ProcName, RI_PROC_BUF_SIZE, "%s", OSGetCurrentClientProcessNameKM());
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
*******************************************************************************

 @Function	RIUpdateMEMDESCAddrKM

 @Description
            Update a Resource Information entry.

 @input     hRIHandle - Handle of object whose reference info is to be updated
 @input     sVAddr - New address for the RI entry

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIUpdateMEMDESCAddrKM(RI_HANDLE hRIHandle,
								   IMG_DEV_VIRTADDR sVAddr)
{
	RI_SUBLIST_ENTRY *psRISubEntry;

	PVR_RETURN_IF_INVALID_PARAM(hRIHandle);

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
*******************************************************************************

 @Function	RIDeletePMREntryKM

 @Description
            Delete a Resource Information entry.

 @input     hRIHandle - Handle of object whose reference info is to be deleted

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIDeletePMREntryKM(RI_HANDLE hRIHandle)
{
	RI_LIST_ENTRY *psRIEntry;
	PMR			*pPMRHashKey;
	PVRSRV_ERROR eResult = PVRSRV_OK;

	PVR_RETURN_IF_INVALID_PARAM(hRIHandle);

	psRIEntry = (RI_LIST_ENTRY *)hRIHandle;

	if (psRIEntry->valid != _VALID_RI_LIST_ENTRY)
	{
		/* Pointer does not point to valid structure */
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (psRIEntry->ui16SubListCount == 0)
	{
		/* Acquire RI lock*/
		_RILock();

		/* Remove the HASH table index entry */
		pPMRHashKey = psRIEntry->psPMR;
		HASH_Remove_Extended(g_pRIHashTable, (void *)&pPMRHashKey);

		psRIEntry->valid = _INVALID;

		/* Remove PMR entry from linked-list of PMR entries */
		dllist_remove_node((PDLLIST_NODE)&(psRIEntry->sListNode));

		if (psRIEntry->ui32RIPMRFlags & RI_FLAG_SYSALLOC_PMR)
		{
			dllist_remove_node((PDLLIST_NODE)&(psRIEntry->sSysAllocListNode));
			g_ui32SysAllocPMRCount--;
		}

		/* Now, free the memory used to store the RI entry */
		OSFreeMemNoStats(psRIEntry);
		psRIEntry = NULL;

		/*
		 * Decrement number of RI entries - if this is now zero,
		 * we can delete the RI hash table
		 */
		if (--g_ui16RICount == 0)
		{
			HASH_Delete(g_pRIHashTable);
			g_pRIHashTable = NULL;

			_RIUnlock();

			/* If deInit has been deferred, we can now destroy the RI Lock */
			if (bRIDeInitDeferred)
			{
				OSLockDestroy(g_hRILock);
			}
		}
		else
		{
			/* Release RI lock*/
			_RIUnlock();
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

	return eResult;
}

/*!
*******************************************************************************

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
	RI_SUBLIST_ENTRY *psRISubEntry;
	uintptr_t hashData;
	IMG_PID pid;

	PVR_RETURN_IF_INVALID_PARAM(hRIHandle);

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
		if (--g_ui16ProcCount == 0)
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
	OSFreeMemNoStats(psRISubEntry);
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

	return PVRSRV_OK;
}

/*!
*******************************************************************************

 @Function	RIDeleteListKM

 @Description
            Delete all Resource Information entries and free associated
            memory.

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIDeleteListKM(void)
{
	PVRSRV_ERROR eResult = PVRSRV_OK;

	_RILock();

	if (g_pRIHashTable)
	{
		eResult = HASH_Iterate(g_pRIHashTable, (HASH_pfnCallback)_DeleteAllEntries, NULL);
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
		eResult = HASH_Iterate(g_pProcHashTable, (HASH_pfnCallback) _DeleteAllProcEntries, NULL);
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

	_RIUnlock();

	return eResult;
}

/*!
*******************************************************************************

 @Function	RIDumpListKM

 @Description
            Dumps out the contents of the RI List entry for the
            specified PMR, and all MEMDESC allocation entries
            in the associated sub linked list.
            At present, output is directed to Kernel log
            via PVR_DPF.

 @input     psPMR - PMR for which RI entry details are to be output

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIDumpListKM(PMR *psPMR)
{
	PVRSRV_ERROR eError;

	/* Acquire RI lock*/
	_RILock();

	eError = _DumpList(psPMR, 0);

	/* Release RI lock*/
	_RIUnlock();

	return eError;
}

/*!
*******************************************************************************

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
	RI_LIST_ENTRY  *psRIEntry = NULL;
	uintptr_t     hashData = 0;
	IMG_PID       hashKey  = pid;

	static IMG_CHAR acStringBuffer[RI_FRMT_SIZE_MAX];

	static IMG_UINT64 ui64TotalMemdescAlloc;
	static IMG_UINT64 ui64TotalImport;
	static IMG_UINT64 ui64TotalPMRAlloc;
	static IMG_UINT64 ui64TotalPMRBacked;
	static enum {
		RI_GET_STATE_MEMDESCS_LIST_START,
		RI_GET_STATE_MEMDESCS_SUMMARY,
		RI_GET_STATE_PMR_LIST,
		RI_GET_STATE_PMR_SUMMARY,
		RI_GET_STATE_END,
		RI_GET_STATE_LAST
	} g_bNextGetState = RI_GET_STATE_MEMDESCS_LIST_START;

	static DLLIST_NODE *psNode;
	static DLLIST_NODE *psSysAllocNode;
	static IMG_CHAR szProcName[RI_PROC_BUF_SIZE];
	static IMG_UINT32 ui32ProcessedSysAllocPMRCount;

	acStringBuffer[0] = '\0';

	switch (g_bNextGetState)
	{
	case RI_GET_STATE_MEMDESCS_LIST_START:
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
			PDLLIST_NODE psNextProcListNode = dllist_get_next_node(&psRISubEntry->sProcListNode);

			if (psRISubEntry->bIsImport)
			{
				ui64TotalImport += psRISubEntry->ui64Size;
			}
			else
			{
				ui64TotalMemdescAlloc += psRISubEntry->ui64Size;
			}

			_GenerateMEMDESCEntryString(psRISubEntry,
										IMG_TRUE,
										RI_MEMDESC_ENTRY_BUF_SIZE,
										acStringBuffer);

			if (szProcName[0] == '\0')
			{
				OSStringLCopy(szProcName, (pid == PVR_SYS_ALLOC_PID) ?
						PVRSRV_MODNAME : psRISubEntry->ai8ProcName, RI_PROC_BUF_SIZE);
			}


			*ppszEntryString = acStringBuffer;
			*ppHandle        = (IMG_HANDLE)IMG_CONTAINER_OF(psNextProcListNode, RI_SUBLIST_ENTRY, sProcListNode);

			if (psNextProcListNode == NULL ||
				psNextProcListNode == (PDLLIST_NODE)hashData)
			{
				g_bNextGetState = RI_GET_STATE_MEMDESCS_SUMMARY;
			}
			/* else continue to list MEMDESCs */
		}
		else
		{
			if (ui64TotalMemdescAlloc == 0)
			{
				acStringBuffer[0] = '\0';
				*ppszEntryString = acStringBuffer;
				g_bNextGetState = RI_GET_STATE_MEMDESCS_SUMMARY;
			}
			/* else continue to list MEMDESCs */
		}
		break;

	case RI_GET_STATE_MEMDESCS_SUMMARY:
		OSSNPrintf(acStringBuffer,
		           RI_MEMDESC_SUM_BUF_SIZE,
		           RI_MEMDESC_SUM_FRMT,
		           pid,
		           szProcName,
		           ui64TotalMemdescAlloc,
		           ui64TotalMemdescAlloc >> 10,
		           ui64TotalImport,
		           ui64TotalImport >> 10,
		           (ui64TotalMemdescAlloc + ui64TotalImport),
		           (ui64TotalMemdescAlloc + ui64TotalImport) >> 10);

		*ppszEntryString = acStringBuffer;
		ui64TotalMemdescAlloc = 0;
		ui64TotalImport = 0;
		szProcName[0] = '\0';

		g_bNextGetState = RI_GET_STATE_PMR_LIST;
		break;

	case RI_GET_STATE_PMR_LIST:
		if (pid == PVR_SYS_ALLOC_PID)
		{
			OSLockAcquire(g_hSysAllocPidListLock);
			acStringBuffer[0] = '\0';
			if (!psSysAllocNode)
			{
				psSysAllocNode = &g_sSysAllocPidListHead;
				ui32ProcessedSysAllocPMRCount = 0;
			}
			psSysAllocNode = dllist_get_next_node(psSysAllocNode);

			if (szProcName[0] == '\0')
			{
				OSStringLCopy(szProcName, PVRSRV_MODNAME, RI_PROC_BUF_SIZE);
			}
			if (psSysAllocNode != NULL && psSysAllocNode != &g_sSysAllocPidListHead)
			{
				IMG_DEVMEM_SIZE_T uiPMRPhysicalBacking, uiPMRLogicalSize = 0;

				psRIEntry = IMG_CONTAINER_OF((PDLLIST_NODE)psSysAllocNode, RI_LIST_ENTRY, sSysAllocListNode);
				_GeneratePMREntryString(psRIEntry,
										IMG_TRUE,
										RI_PMR_ENTRY_BUF_SIZE,
										acStringBuffer);
				PMR_LogicalSize(psRIEntry->psPMR,
								&uiPMRLogicalSize);
				ui64TotalPMRAlloc += uiPMRLogicalSize;
				PMR_PhysicalSize(psRIEntry->psPMR, &uiPMRPhysicalBacking);
				ui64TotalPMRBacked += uiPMRPhysicalBacking;

				ui32ProcessedSysAllocPMRCount++;
				if (ui32ProcessedSysAllocPMRCount > g_ui32SysAllocPMRCount+1)
				{
					g_bNextGetState = RI_GET_STATE_PMR_SUMMARY;
				}
				/* else continue to list PMRs */
			}
			else
			{
				g_bNextGetState = RI_GET_STATE_PMR_SUMMARY;
			}
			*ppszEntryString = (IMG_CHAR *)acStringBuffer;
			OSLockRelease(g_hSysAllocPidListLock);
		}
		else
		{
			IMG_BOOL bPMRToDisplay = IMG_FALSE;

			/* Iterate through the 'touched' PMRs and display details */
			if (!psNode)
			{
				psNode = dllist_get_next_node(&sListFirst);
			}
			else
			{
				psNode = dllist_get_next_node(psNode);
			}

			while ((psNode != NULL && psNode != &sListFirst) &&
					!bPMRToDisplay)
			{
				psRIEntry =	IMG_CONTAINER_OF(psNode, RI_LIST_ENTRY, sListNode);
				if (psRIEntry->pid == pid)
				{
					IMG_DEVMEM_SIZE_T uiPMRPhysicalBacking, uiPMRLogicalSize = 0;

					/* This PMR was 'touched', so display details and unflag it*/
					_GeneratePMREntryString(psRIEntry,
											IMG_TRUE,
											RI_PMR_ENTRY_BUF_SIZE,
											acStringBuffer);
					PMR_LogicalSize(psRIEntry->psPMR, &uiPMRLogicalSize);
					ui64TotalPMRAlloc += uiPMRLogicalSize;
					PMR_PhysicalSize(psRIEntry->psPMR, &uiPMRPhysicalBacking);
					ui64TotalPMRBacked += uiPMRPhysicalBacking;

					/* Remember the name of the process for 1 PMR for the summary */
					if (szProcName[0] == '\0')
					{
						OSStringLCopy(szProcName, psRIEntry->ai8ProcName, RI_PROC_BUF_SIZE);
					}
					bPMRToDisplay = IMG_TRUE;
				}
				else
				{
					psNode = dllist_get_next_node(psNode);
				}
			}

			if (psNode == NULL || (psNode == &sListFirst))
			{
				g_bNextGetState = RI_GET_STATE_PMR_SUMMARY;
			}
			/* else continue listing PMRs */
		}
		break;

	case RI_GET_STATE_PMR_SUMMARY:
		OSSNPrintf(acStringBuffer,
		           RI_PMR_SUM_BUF_SIZE,
		           RI_PMR_SUM_FRMT,
		           pid,
		           szProcName,
		           ui64TotalPMRAlloc,
		           ui64TotalPMRAlloc >> 10,
		           ui64TotalPMRBacked,
		           ui64TotalPMRBacked >> 10);

		*ppszEntryString = acStringBuffer;
		ui64TotalPMRAlloc = 0;
		ui64TotalPMRBacked = 0;
		szProcName[0] = '\0';
		psSysAllocNode = NULL;

		g_bNextGetState = RI_GET_STATE_END;
		break;

	default:
		PVR_DPF((PVR_DBG_ERROR, "%s: Bad %d)",__func__, g_bNextGetState));

		__fallthrough;
	case RI_GET_STATE_END:
		/* Reset state ready for the next gpu_mem_area file to display */
		*ppszEntryString = NULL;
		*ppHandle        = NULL;
		psNode = NULL;
		szProcName[0] = '\0';

		g_bNextGetState = RI_GET_STATE_MEMDESCS_LIST_START;
		return IMG_FALSE;
		break;
	}

	return IMG_TRUE;
}

/* Function used to produce string containing info for MEMDESC RI entries (used for both debugfs and kernel log output) */
static void _GenerateMEMDESCEntryString(RI_SUBLIST_ENTRY *psRISubEntry,
                                        IMG_BOOL bDebugFs,
                                        IMG_UINT16 ui16MaxStrLen,
                                        IMG_CHAR *pszEntryString)
{
	IMG_CHAR szProc[RI_MEMDESC_ENTRY_PROC_BUF_SIZE];
	IMG_CHAR szImport[RI_MEMDESC_ENTRY_IMPORT_BUF_SIZE];
	IMG_CHAR szEntryFormat[RI_MEMDESC_ENTRY_FRMT_SIZE];
	const IMG_CHAR *pszAnnotationText;
	IMG_PID uiRIPid = 0;
	PMR* psRIPMR = NULL;
	IMG_UINT32 ui32RIPMRFlags = 0;

	if (psRISubEntry->psRI != NULL)
	{
		uiRIPid = psRISubEntry->psRI->pid;
		psRIPMR = psRISubEntry->psRI->psPMR;
		ui32RIPMRFlags = psRISubEntry->psRI->ui32RIPMRFlags;
	}

	OSSNPrintf(szEntryFormat,
			RI_MEMDESC_ENTRY_FRMT_SIZE,
			RI_MEMDESC_ENTRY_FRMT,
			DEVMEM_ANNOTATION_MAX_LEN);

	if (!bDebugFs)
	{
		/* we don't include process ID info for debugfs output */
		OSSNPrintf(szProc,
				RI_MEMDESC_ENTRY_PROC_BUF_SIZE,
				RI_MEMDESC_ENTRY_PROC_FRMT,
				psRISubEntry->pid,
				psRISubEntry->ai8ProcName);
	}

	if (psRISubEntry->bIsImport && psRIPMR)
	{
		OSSNPrintf((IMG_CHAR *)&szImport,
		           RI_MEMDESC_ENTRY_IMPORT_BUF_SIZE,
		           RI_MEMDESC_ENTRY_IMPORT_FRMT,
		           uiRIPid);
		/* Set pszAnnotationText to that of the 'parent' PMR RI entry */
		pszAnnotationText = PMR_GetAnnotation(psRIPMR);
	}
	else if (!psRISubEntry->bIsSuballoc && psRIPMR)
	{
		/* Set pszAnnotationText to that of the 'parent' PMR RI entry */
		pszAnnotationText = PMR_GetAnnotation(psRIPMR);
	}
	else
	{
		/* Set pszAnnotationText to that of the MEMDESC RI entry */
		pszAnnotationText = psRISubEntry->ai8TextB;
	}

	/* Don't print memdescs if they are local imports
	 * (i.e. imported PMRs allocated by this process)
	 */
	if (bDebugFs &&
		((psRISubEntry->sVAddr.uiAddr + psRISubEntry->ui64Offset) == 0) &&
		(psRISubEntry->bIsImport && ((psRISubEntry->pid == uiRIPid)
									 || (psRISubEntry->pid == PVR_SYS_ALLOC_PID))))
	{
		/* Don't print this entry */
		pszEntryString[0] = '\0';
	}
	else
	{
		OSSNPrintf(pszEntryString,
				   ui16MaxStrLen,
				   szEntryFormat,
				   (bDebugFs ? "" : "   "),
				   psRISubEntry->pid,
				   (psRISubEntry->sVAddr.uiAddr + psRISubEntry->ui64Offset),
				   pszAnnotationText,
				   (bDebugFs ? "" : (char *)szProc),
				   psRISubEntry->ui64Size,
				   psRIPMR,
				   (psRISubEntry->bIsImport ? (char *)&szImport : ""),
				   (!psRISubEntry->bIsImport && (ui32RIPMRFlags & RI_FLAG_SYSALLOC_PMR) && (psRISubEntry->pid != PVR_SYS_ALLOC_PID)) ? g_szSysAllocImport : "",
				   (psRIPMR && PMR_IsUnpinned(psRIPMR)) ? RI_MEMDESC_ENTRY_UNPINNED_FRMT : "",
				   (bDebugFs ? '\n' : ' '));
	}
}

/* Function used to produce string containing info for PMR RI entries (used for debugfs and kernel log output) */
static void _GeneratePMREntryString(RI_LIST_ENTRY *psRIEntry,
                                    IMG_BOOL bDebugFs,
                                    IMG_UINT16 ui16MaxStrLen,
                                    IMG_CHAR *pszEntryString)
{
	const IMG_CHAR*   pszAnnotationText;
	IMG_DEVMEM_SIZE_T uiLogicalSize = 0;
	IMG_DEVMEM_SIZE_T uiPhysicalSize = 0;
	IMG_CHAR          szEntryFormat[RI_PMR_ENTRY_FRMT_SIZE];

	PMR_LogicalSize(psRIEntry->psPMR, &uiLogicalSize);

	PMR_PhysicalSize(psRIEntry->psPMR, &uiPhysicalSize);

	OSSNPrintf(szEntryFormat,
			RI_PMR_ENTRY_FRMT_SIZE,
			RI_PMR_ENTRY_FRMT,
			DEVMEM_ANNOTATION_MAX_LEN);

	/* Set pszAnnotationText to that PMR RI entry */
	pszAnnotationText = (IMG_PCHAR) PMR_GetAnnotation(psRIEntry->psPMR);

	OSSNPrintf(pszEntryString,
	           ui16MaxStrLen,
	           szEntryFormat,
	           (bDebugFs ? "" : "   "),
	           psRIEntry->pid,
	           (void*)psRIEntry->psPMR,
	           pszAnnotationText,
	           uiLogicalSize,
	           uiPhysicalSize,
	           (bDebugFs ? '\n' : ' '));
}

/*!
*******************************************************************************

 @Function	_DumpList

 @Description
            Dumps out RI List entries according to parameters passed.

 @input     psPMR - If not NULL, function will output the RI entries for
                   the specified PMR only
 @input     pid - If non-zero, the function will only output MEMDESC RI
                  entries made by the process with ID pid.
                  If zero, all MEMDESC RI entries will be output.

 @Return	PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR _DumpList(PMR *psPMR, IMG_PID pid)
{
	RI_LIST_ENTRY *psRIEntry = NULL;
	RI_SUBLIST_ENTRY *psRISubEntry = NULL;
	IMG_UINT16 ui16SubEntriesParsed = 0;
	uintptr_t hashData = 0;
	IMG_PID hashKey;
	PMR *pPMRHashKey = psPMR;
	IMG_BOOL bDisplayedThisPMR = IMG_FALSE;
	IMG_UINT64 ui64LogicalSize = 0;

	PVR_RETURN_IF_INVALID_PARAM(psPMR);

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
			/* Look-up psPMR in Hash Table */
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
				PMR_LogicalSize(psPMR, (IMG_DEVMEM_SIZE_T*)&ui64LogicalSize);

				_RIOutput (("%s <%p> suballocs:%d size:0x%010" IMG_UINT64_FMTSPECx,
				            PMR_GetAnnotation(psRIEntry->psPMR),
				            psRIEntry->psPMR,
				            (IMG_UINT)psRIEntry->ui16SubListCount,
				            ui64LogicalSize));
				bDisplayedThisPMR = IMG_TRUE;
			}
			ui16SubEntriesParsed = 0;
			if (psRIEntry->ui16SubListCount)
			{
#if _DUMP_LINKEDLIST_INFO
				_RIOutput (("RI LIST: {sSubListFirst.psNextNode:0x%p}\n",
				            psRIEntry->sSubListFirst.psNextNode));
#endif /* _DUMP_LINKEDLIST_INFO */
				if (!pid)
				{
					psRISubEntry = IMG_CONTAINER_OF(dllist_get_next_node(&(psRIEntry->sSubListFirst)),
					                                RI_SUBLIST_ENTRY, sListNode);
				}
				/* Traverse RI sublist and output details for each entry */
				while (psRISubEntry)
				{
					if (psRIEntry)
					{
						if ((ui16SubEntriesParsed >= psRIEntry->ui16SubListCount))
						{
							break;
						}
						if (!bDisplayedThisPMR)
						{
							PMR_LogicalSize(psPMR, (IMG_DEVMEM_SIZE_T*)&ui64LogicalSize);

							_RIOutput (("%s <%p> suballocs:%d size:0x%010" IMG_UINT64_FMTSPECx,
								    PMR_GetAnnotation(psRIEntry->psPMR),
								    psRIEntry->psPMR,
								    (IMG_UINT)psRIEntry->ui16SubListCount,
								    ui64LogicalSize));
							bDisplayedThisPMR = IMG_TRUE;
						}
					}
#if _DUMP_LINKEDLIST_INFO
					_RIOutput (("RI LIST:    [this subentry:0x%p]\n",psRISubEntry));
					_RIOutput (("RI LIST:     psRI:0x%p\n",psRISubEntry->psRI));
#endif /* _DUMP_LINKEDLIST_INFO */

					{
						IMG_CHAR szEntryString[RI_MEMDESC_ENTRY_BUF_SIZE];

						_GenerateMEMDESCEntryString(psRISubEntry,
						                            IMG_FALSE,
						                            RI_MEMDESC_ENTRY_BUF_SIZE,
						                            szEntryString);
						_RIOutput (("%s",szEntryString));
					}

					if (pid)
					{
						if ((dllist_get_next_node(&(psRISubEntry->sProcListNode)) == NULL) ||
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
			if (!pid && psRIEntry)
			{
				if (ui16SubEntriesParsed != psRIEntry->ui16SubListCount)
				{
					/*
					 * Output error message as sublist does not contain the
					 * number of entries indicated by sublist count
					 */
					_RIOutput (("RI ERROR: RI sublist contains %d entries, not %d entries\n",
					            ui16SubEntriesParsed, psRIEntry->ui16SubListCount));
				}
				else if (psRIEntry->ui16SubListCount && !dllist_get_next_node(&(psRIEntry->sSubListFirst)))
				{
					/*
					 * Output error message as sublist is empty but sublist count
					 * is not zero
					 */
					_RIOutput (("RI ERROR: ui16SubListCount=%d for empty RI sublist\n",
					            psRIEntry->ui16SubListCount));
				}
			}
			psRIEntry = NULL;
		}
	}
	return PVRSRV_OK;
}

/*!
*******************************************************************************

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
		return HASH_Iterate(g_pRIHashTable, (HASH_pfnCallback)_DumpAllEntries, NULL);
	}
	return PVRSRV_OK;
}

/*!
*******************************************************************************

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
	PVRSRV_ERROR eError;
	IMG_UINT32 dummyPMR;

	if (!g_pProcHashTable)
	{
		return PVRSRV_OK;
	}

	/* Acquire RI lock*/
	_RILock();

	eError = _DumpList((PMR *)&dummyPMR, pid);

	/* Release RI lock*/
	_RIUnlock();

	return eError;
}

/*!
*******************************************************************************

 @Function	_TotalAllocsForProcess

 @Description
            Totals all PMR physical backing for given process.

 @input     pid - ID of process.

 @input     ePhysHeapType - type of Physical Heap for which to total allocs

 @Return	Size of all physical backing for PID's PMRs allocated from the
            specified heap type (in bytes).

******************************************************************************/
static IMG_INT32 _TotalAllocsForProcess(IMG_PID pid, PHYS_HEAP_TYPE ePhysHeapType)
{
	RI_LIST_ENTRY *psRIEntry = NULL;
	RI_SUBLIST_ENTRY *psInitialRISubEntry = NULL;
	RI_SUBLIST_ENTRY *psRISubEntry = NULL;
	uintptr_t hashData = 0;
	IMG_PID hashKey;
	IMG_INT32 i32TotalPhysical = 0;

	if (g_pRIHashTable && g_pProcHashTable)
	{
		if (pid == PVR_SYS_ALLOC_PID)
		{
			IMG_UINT32 ui32ProcessedSysAllocPMRCount = 0;
			DLLIST_NODE *psSysAllocNode = NULL;

			OSLockAcquire(g_hSysAllocPidListLock);
			psSysAllocNode = dllist_get_next_node(&g_sSysAllocPidListHead);
			while (psSysAllocNode && psSysAllocNode != &g_sSysAllocPidListHead)
			{
				psRIEntry = IMG_CONTAINER_OF((PDLLIST_NODE)psSysAllocNode, RI_LIST_ENTRY, sSysAllocListNode);
				ui32ProcessedSysAllocPMRCount++;
				if (PhysHeapGetType(PMR_PhysHeap(psRIEntry->psPMR)) == ePhysHeapType)
				{
					IMG_UINT64 ui64PhysicalSize;

					PMR_PhysicalSize(psRIEntry->psPMR, (IMG_DEVMEM_SIZE_T*)&ui64PhysicalSize);
					if (((IMG_UINT64)i32TotalPhysical + ui64PhysicalSize > 0x7fffffff))
					{
						PVR_DPF((PVR_DBG_WARNING, "%s: i32TotalPhysical exceeding size for i32",__func__));
					}
					i32TotalPhysical += (IMG_INT32)(ui64PhysicalSize & 0x00000000ffffffff);
				}
				psSysAllocNode = dllist_get_next_node(psSysAllocNode);
			}
			OSLockRelease(g_hSysAllocPidListLock);
		}
		else
		{
			if (pid != 0)
			{
				/* look-up pid in Hash Table */
				hashKey = pid;
				hashData = HASH_Retrieve_Extended (g_pProcHashTable, (void *)&hashKey);
				if (hashData)
				{
					psInitialRISubEntry = IMG_CONTAINER_OF((PDLLIST_NODE)hashData, RI_SUBLIST_ENTRY, sProcListNode);
					psRISubEntry = psInitialRISubEntry;
					if (psRISubEntry)
					{
						psRIEntry = psRISubEntry->psRI;
					}
				}
			}

			while (psRISubEntry && psRIEntry)
			{
				if (!psRISubEntry->bIsImport && !(psRIEntry->ui32RIPMRFlags & RI_FLAG_PMR_PHYS_COUNTED_BY_DEBUGFS) &&
					(pid == PVR_SYS_ALLOC_PID || !(psRIEntry->ui32RIPMRFlags & RI_FLAG_SYSALLOC_PMR)) &&
					(PhysHeapGetType(PMR_PhysHeap(psRIEntry->psPMR)) == ePhysHeapType))
				{
					IMG_UINT64 ui64PhysicalSize;


					PMR_PhysicalSize(psRIEntry->psPMR, (IMG_DEVMEM_SIZE_T*)&ui64PhysicalSize);
					if (((IMG_UINT64)i32TotalPhysical + ui64PhysicalSize > 0x7fffffff))
					{
						PVR_DPF((PVR_DBG_WARNING, "%s: i32TotalPhysical exceeding size for i32",__func__));
					}
					i32TotalPhysical += (IMG_INT32)(ui64PhysicalSize & 0x00000000ffffffff);
					psRIEntry->ui32RIPMRFlags |= RI_FLAG_PMR_PHYS_COUNTED_BY_DEBUGFS;
				}
				if ((dllist_get_next_node(&(psRISubEntry->sProcListNode)) == NULL) ||
					(dllist_get_next_node(&(psRISubEntry->sProcListNode)) == (PDLLIST_NODE)hashData))
				{
					psRISubEntry = NULL;
					psRIEntry = NULL;
				}
				else
				{
					psRISubEntry = IMG_CONTAINER_OF(dllist_get_next_node(&(psRISubEntry->sProcListNode)),
					                                RI_SUBLIST_ENTRY, sProcListNode);
					if (psRISubEntry)
					{
						psRIEntry = psRISubEntry->psRI;
					}
				}
			}
			psRISubEntry = psInitialRISubEntry;
			if (psRISubEntry)
			{
				psRIEntry = psRISubEntry->psRI;
			}
			while (psRISubEntry && psRIEntry)
			{
				psRIEntry->ui32RIPMRFlags &= ~RI_FLAG_PMR_PHYS_COUNTED_BY_DEBUGFS;
				if ((dllist_get_next_node(&(psRISubEntry->sProcListNode)) == NULL) ||
					(dllist_get_next_node(&(psRISubEntry->sProcListNode)) == (PDLLIST_NODE)hashData))
				{
					psRISubEntry = NULL;
					psRIEntry = NULL;
				}
				else
				{
					psRISubEntry = IMG_CONTAINER_OF(dllist_get_next_node(&(psRISubEntry->sProcListNode)),
					                                RI_SUBLIST_ENTRY, sProcListNode);
					if (psRISubEntry)
					{
						psRIEntry = psRISubEntry->psRI;
					}
				}
			}
		}
	}
	return i32TotalPhysical;
}

/*!
*******************************************************************************

 @Function	RITotalAllocProcessKM

 @Description
            Returns the total of allocated GPU memory (backing for PMRs)
            which has been allocated from the specific heap by the specified
            process only.

 @Return	Amount of physical backing allocated (in bytes)

******************************************************************************/
IMG_INT32 RITotalAllocProcessKM(IMG_PID pid, PHYS_HEAP_TYPE ePhysHeapType)
{
	IMG_INT32 i32BackingTotal = 0;

	if (g_pProcHashTable)
	{
		/* Acquire RI lock*/
		_RILock();

		i32BackingTotal = _TotalAllocsForProcess(pid, ePhysHeapType);

		/* Release RI lock*/
		_RIUnlock();
	}
	return i32BackingTotal;
}

#if defined(DEBUG)
/*!
*******************************************************************************

 @Function	_DumpProcessList

 @Description
            Dumps out RI List entries according to parameters passed.

 @input     psPMR - If not NULL, function will output the RI entries for
                   the specified PMR only
 @input     pid - If non-zero, the function will only output MEMDESC RI
                  entries made by the process with ID pid.
                  If zero, all MEMDESC RI entries will be output.

 @Return	PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR _DumpProcessList(PMR *psPMR,
									 IMG_PID pid,
									 IMG_UINT64 ui64Offset,
									 IMG_DEV_VIRTADDR *psDevVAddr)
{
	RI_LIST_ENTRY *psRIEntry = NULL;
	RI_SUBLIST_ENTRY *psRISubEntry = NULL;
	IMG_UINT16 ui16SubEntriesParsed = 0;
	uintptr_t hashData = 0;
	PMR *pPMRHashKey = psPMR;

	psDevVAddr->uiAddr = 0;

	PVR_RETURN_IF_INVALID_PARAM(psPMR);

	if (g_pRIHashTable && g_pProcHashTable)
	{
		PVR_ASSERT(psPMR && pid);

		/* Look-up psPMR in Hash Table */
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

	return PVRSRV_ERROR_INVALID_PARAMS;
}

/*!
*******************************************************************************

 @Function	RIDumpProcessListKM

 @Description
            Dumps out selected contents of all MEMDESC RI List entries (for a
            PMR) which have been allocate by the specified process only.

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RIDumpProcessListKM(PMR *psPMR,
								 IMG_PID pid,
								 IMG_UINT64 ui64Offset,
								 IMG_DEV_VIRTADDR *psDevVAddr)
{
	PVRSRV_ERROR eError;

	if (!g_pProcHashTable)
	{
		return PVRSRV_OK;
	}

	/* Acquire RI lock*/
	_RILock();

	eError = _DumpProcessList(psPMR,
							  pid,
							  ui64Offset,
							  psDevVAddr);

	/* Release RI lock*/
	_RIUnlock();

	return eError;
}
#endif

static PVRSRV_ERROR _DumpAllEntries (uintptr_t k, uintptr_t v, void* pvPriv)
{
	RI_LIST_ENTRY *psRIEntry = (RI_LIST_ENTRY *)v;

	PVR_UNREFERENCED_PARAMETER (k);
	PVR_UNREFERENCED_PARAMETER (pvPriv);

	return RIDumpListKM(psRIEntry->psPMR);
}

static PVRSRV_ERROR _DeleteAllEntries (uintptr_t k, uintptr_t v, void* pvPriv)
{
	RI_LIST_ENTRY *psRIEntry = (RI_LIST_ENTRY *)v;
	RI_SUBLIST_ENTRY *psRISubEntry;
	PVRSRV_ERROR eResult = PVRSRV_OK;

	PVR_UNREFERENCED_PARAMETER (k);
	PVR_UNREFERENCED_PARAMETER (pvPriv);

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

static PVRSRV_ERROR _DeleteAllProcEntries (uintptr_t k, uintptr_t v, void* pvPriv)
{
	RI_SUBLIST_ENTRY *psRISubEntry = (RI_SUBLIST_ENTRY *)v;
	PVRSRV_ERROR eResult;

	PVR_UNREFERENCED_PARAMETER (k);
	PVR_UNREFERENCED_PARAMETER (pvPriv);

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

#endif /* if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) */
