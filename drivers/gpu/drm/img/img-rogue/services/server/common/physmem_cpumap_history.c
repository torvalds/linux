/*************************************************************************/ /*!
@File
@Title          Implementation of PMR Mapping History for OS managed memory
@Codingstyle    IMG
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    This module allows for an OS independent method of capture of
                mapping history for OS memory.
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
#include "img_types.h"
#include "img_defs.h"
#include <powervr/mem_types.h>
#include "di_common.h"
#include "di_server.h"
#include "pvr_notifier.h"
#include "physmem_cpumap_history.h"
#include "osfunc.h"
#include "allocmem.h"

#define MAPPING_HISTORY_CB_NUM_ENTRIES 10000
#if defined(DEBUG)
#define MAX_MAPPING_ANNOT_STR PVR_ANNOTATION_MAX_LEN
#else
#define MAX_MAPPING_ANNOT_STR 32
#endif

typedef struct _MAPPING_RECORD_ MAPPING_RECORD; /* Forward declaration */
typedef void (*PFN_MAPPING_RECORD_STRING)(MAPPING_RECORD *psRecord,
                                          IMG_CHAR (*pszBuffer)[PVR_MAX_DEBUG_MESSAGE_LEN]);

typedef struct _MAP_DATA_
{
	IMG_CHAR aszAnnotation[MAX_MAPPING_ANNOT_STR];
	IMG_PID uiPID;
	IMG_CPU_VIRTADDR pvAddress;
	IMG_CPU_PHYADDR sCpuPhyAddr;
	IMG_UINT32 ui32CPUCacheFlags;
	size_t uiMapOffset; /* Mapping offset used when we don't map the whole PMR */
	IMG_UINT32 ui32PageCount; /* # pages mapped */
} MAP_DATA;

typedef struct _UNMAP_DATA_
{
	IMG_CPU_VIRTADDR pvAddress;
	IMG_CPU_PHYADDR sCpuPhyAddr;
	IMG_UINT32 ui32CPUCacheFlags;
	IMG_UINT32 ui32PageCount;
} UNMAP_DATA;

struct _MAPPING_RECORD_
{
	enum MAPPING_OP
	{
		UNSET = 0,
		MAP,
		UNMAP,
	} etype;
	union
	{
		MAP_DATA sMapData;
		UNMAP_DATA sUnmapData;
	} u;
	PFN_MAPPING_RECORD_STRING pfnRecordString;
};

typedef struct _RECORDS_
{
	/* CB of mapping records that will be overwritten by newer entries */
	MAPPING_RECORD *pasMapRecordsCB;
	/* Current head of CB, used to get next slot, newest record +1 */
	IMG_UINT32 ui32Head;
	/* Current tail of CB, oldest record */
	IMG_UINT32 ui32Tail;
	/* Have we overwritten any records */
	IMG_BOOL bOverwrite;
} RECORDS;

typedef struct _PHYSMEM_CPUMAP_HISTORY_DATA_
{
	DI_ENTRY *psDIEntry;
	RECORDS sRecords;
	POS_LOCK hLock;
	IMG_HANDLE hDbgNotifier;

} PHYSMEM_CPUMAP_HISTORY_DATA;

typedef struct _MAPPING_HISTORY_ITERATOR_
{
	IMG_UINT32 ui32Start;
	IMG_UINT32 ui32Finish;
	IMG_UINT32 ui32Current;
} MAPPING_HISTORY_ITERATOR;

static PHYSMEM_CPUMAP_HISTORY_DATA gsMappingHistoryData;

static PVRSRV_ERROR CreateMappingRecords(void)
{
	gsMappingHistoryData.sRecords.pasMapRecordsCB =
	    OSAllocMem(sizeof(MAPPING_RECORD) * MAPPING_HISTORY_CB_NUM_ENTRIES);
	PVR_RETURN_IF_NOMEM(gsMappingHistoryData.sRecords.pasMapRecordsCB);

	return PVRSRV_OK;
}

static void InitMappingRecordCB(void)
{
	gsMappingHistoryData.sRecords.ui32Head = 0;
	gsMappingHistoryData.sRecords.ui32Tail = 0;
	gsMappingHistoryData.sRecords.bOverwrite = IMG_FALSE;
}

static void DestroyMappingRecords(void)
{
	OSFreeMem(gsMappingHistoryData.sRecords.pasMapRecordsCB);
}

static void MappingHistoryLock(void)
{
	if (gsMappingHistoryData.hLock)
	{
		OSLockAcquire(gsMappingHistoryData.hLock);
	}
}

static void MappingHistoryUnlock(void)
{
	if (gsMappingHistoryData.hLock)
	{
		OSLockRelease(gsMappingHistoryData.hLock);
	}
}

static MAPPING_HISTORY_ITERATOR CreateMappingHistoryIterator(void)
{
	MAPPING_HISTORY_ITERATOR sIter;

	sIter.ui32Start = gsMappingHistoryData.sRecords.ui32Tail;
	sIter.ui32Current = gsMappingHistoryData.sRecords.ui32Tail;
	sIter.ui32Finish = gsMappingHistoryData.sRecords.ui32Head;

	return sIter;
}

static IMG_BOOL MappingHistoryHasRecords(void)
{
	if ((gsMappingHistoryData.sRecords.ui32Head ==
	     gsMappingHistoryData.sRecords.ui32Tail) &&
	    !gsMappingHistoryData.sRecords.bOverwrite)
	{
		return IMG_FALSE;
	}

	return IMG_TRUE;
}

static IMG_BOOL MappingHistoryIteratorNext(MAPPING_HISTORY_ITERATOR *psIter)
{
	psIter->ui32Current = (psIter->ui32Current + 1) % MAPPING_HISTORY_CB_NUM_ENTRIES;

	if (psIter->ui32Current == psIter->ui32Finish)
	{
		return IMG_FALSE;
	}

	return IMG_TRUE;
}

#define MAPPING_ITER_TO_PTR(psIter) (&(gsMappingHistoryData.sRecords.pasMapRecordsCB[psIter.ui32Current]))

static MAPPING_RECORD* AcquireMappingHistoryCBSlot(void)
{
	MAPPING_RECORD *psSlot;

	psSlot = &gsMappingHistoryData.sRecords.pasMapRecordsCB[gsMappingHistoryData.sRecords.ui32Head];

	gsMappingHistoryData.sRecords.ui32Head =
	    (gsMappingHistoryData.sRecords.ui32Head + 1)
	    % MAPPING_HISTORY_CB_NUM_ENTRIES;

	if (!gsMappingHistoryData.sRecords.bOverwrite)
	{
		if (gsMappingHistoryData.sRecords.ui32Head == gsMappingHistoryData.sRecords.ui32Tail)
		{
			gsMappingHistoryData.sRecords.bOverwrite = IMG_TRUE;
		}
	}
	else
	{
		gsMappingHistoryData.sRecords.ui32Tail = gsMappingHistoryData.sRecords.ui32Head;
	}

	return psSlot;
}

static IMG_CHAR* CacheFlagsToStr(IMG_UINT32 ui32CPUCacheFlags)
{
	switch (ui32CPUCacheFlags)
	{
		case PVRSRV_MEMALLOCFLAG_CPU_UNCACHED:
			return "UC";

		case PVRSRV_MEMALLOCFLAG_CPU_UNCACHED_WC:
			return "WC";

		case PVRSRV_MEMALLOCFLAG_CPU_CACHED:
			return "CA";

		default:
			return "UN";
	}
}

static void MapRecordString(MAPPING_RECORD *psRecord,
                            IMG_CHAR (*pszBuffer)[PVR_MAX_DEBUG_MESSAGE_LEN])
{
	OSSNPrintf(*pszBuffer, PVR_MAX_DEBUG_MESSAGE_LEN,
	           "%-32s %-8u 0x%p "CPUPHYADDR_UINT_FMTSPEC" %-5u (%-2s) %-12lu %-8u",
	           psRecord->u.sMapData.aszAnnotation,
	           psRecord->u.sMapData.uiPID,
	           psRecord->u.sMapData.pvAddress,
	           CPUPHYADDR_FMTARG(psRecord->u.sMapData.sCpuPhyAddr.uiAddr),
	           psRecord->u.sMapData.ui32CPUCacheFlags,
	           CacheFlagsToStr(psRecord->u.sMapData.ui32CPUCacheFlags),
	           (unsigned long) psRecord->u.sMapData.uiMapOffset,
	           psRecord->u.sMapData.ui32PageCount);
}

static void UnMapRecordString(MAPPING_RECORD *psRecord,
                              IMG_CHAR (*pszBuffer)[PVR_MAX_DEBUG_MESSAGE_LEN])
{
	OSSNPrintf(*pszBuffer, PVR_MAX_DEBUG_MESSAGE_LEN,
	           "%-41s 0x%p "CPUPHYADDR_UINT_FMTSPEC" %-5u (%-2s) %-12s %-8u",
	           "UnMap", /* PADDING */
	           psRecord->u.sUnmapData.pvAddress,
	           CPUPHYADDR_FMTARG(psRecord->u.sUnmapData.sCpuPhyAddr.uiAddr),
	           psRecord->u.sUnmapData.ui32CPUCacheFlags,
	           CacheFlagsToStr(psRecord->u.sUnmapData.ui32CPUCacheFlags),
	           "", /* PADDING */
	           psRecord->u.sUnmapData.ui32PageCount);
}

static void MappingHistoryGetHeaderString(IMG_CHAR (*pszBuffer)[PVR_MAX_DEBUG_MESSAGE_LEN])
{
	OSSNPrintf(*pszBuffer, PVR_MAX_DEBUG_MESSAGE_LEN,
	           "%-32s %-8s %-18s %-18s %-9s %-8s %-8s",
	           "PMRAnnotation",
	           "PID",
	           "CpuVirtAddr",
	           "CpuPhyAddr",
	           "CacheFlags",
	           "PMRMapOffset",
	           "ui32PageCount");
}

static void MappingHistoryPrintAll(OSDI_IMPL_ENTRY *psEntry)
{
	IMG_CHAR szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN];
	MAPPING_HISTORY_ITERATOR sIter = CreateMappingHistoryIterator();

	if (!MappingHistoryHasRecords())
	{
		DIPrintf(psEntry, "No Records...\n");
		return;
	}

	MappingHistoryGetHeaderString(&szBuffer);
	DIPrintf(psEntry, "%s\n", szBuffer);

	do
	{
		MAPPING_RECORD *psRecord = MAPPING_ITER_TO_PTR(sIter);
		psRecord->pfnRecordString(psRecord, &szBuffer);
		DIPrintf(psEntry, "%s\n", szBuffer);

	}
	while (MappingHistoryIteratorNext(&sIter));
}

static int MappingHistoryPrintAllWrapper(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(pvData);

	MappingHistoryLock();
	MappingHistoryPrintAll(psEntry);
	MappingHistoryUnlock();

	return 0;
}

static void MappingHistoryDebugDump(PVRSRV_DBGREQ_HANDLE hDebugRequestHandle,
                                    IMG_UINT32 ui32VerbLevel,
                                    DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                    void *pvDumpDebugFile)
{
	if (DD_VERB_LVL_ENABLED(ui32VerbLevel, DEBUG_REQUEST_VERBOSITY_MAX))
	{
		MAPPING_HISTORY_ITERATOR sIter = CreateMappingHistoryIterator();
		IMG_CHAR szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN];

		PVR_UNREFERENCED_PARAMETER(hDebugRequestHandle);

		PVR_DUMPDEBUG_LOG("------[ Physmem CPU Map History ]------");

		if (!MappingHistoryHasRecords())
		{
			PVR_DUMPDEBUG_LOG("No Records...");
			return;
		}

		MappingHistoryGetHeaderString(&szBuffer);
		PVR_DUMPDEBUG_LOG("%s", szBuffer);

		do
		{
			MAPPING_RECORD *psRecord = MAPPING_ITER_TO_PTR(sIter);
			psRecord->pfnRecordString(psRecord, &szBuffer);
			PVR_DUMPDEBUG_LOG("%s", szBuffer);
		}
		while (MappingHistoryIteratorNext(&sIter));
	}
}

PVRSRV_ERROR CPUMappingHistoryInit(void)
{
	PVRSRV_ERROR eError;
	DI_ITERATOR_CB sIterator =
	{ .pfnShow = MappingHistoryPrintAllWrapper };

	eError = OSLockCreate(&gsMappingHistoryData.hLock);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSLockCreate", error);

	eError = CreateMappingRecords();
	PVR_LOG_GOTO_IF_ERROR(eError, "CreateMappingRecords", error);

	InitMappingRecordCB();

	eError = DICreateEntry("cpumap_history", NULL, &sIterator, NULL,
	                       DI_ENTRY_TYPE_GENERIC,
	                       &gsMappingHistoryData.psDIEntry);
	PVR_LOG_GOTO_IF_ERROR(eError, "DICreateEntry", error);

	eError = PVRSRVRegisterDriverDbgRequestNotify(&gsMappingHistoryData.hDbgNotifier,
	                                              MappingHistoryDebugDump,
	                                              DEBUG_REQUEST_SRV,
	                                              NULL);
	PVR_LOG_GOTO_IF_ERROR(eError, "PVRSRVRegisterDriverDbgRequestNotify", error);

	return PVRSRV_OK;

error:
	CPUMappingHistoryDeInit();
	return eError;
}

void CPUMappingHistoryDeInit(void)
{
	if (gsMappingHistoryData.hDbgNotifier != NULL)
	{
		PVRSRVUnregisterDriverDbgRequestNotify(gsMappingHistoryData.hDbgNotifier);
		gsMappingHistoryData.hDbgNotifier = NULL;
	}

	if (gsMappingHistoryData.psDIEntry != NULL)
	{
		DIDestroyEntry(gsMappingHistoryData.psDIEntry);
		gsMappingHistoryData.psDIEntry = NULL;
	}

	if (gsMappingHistoryData.sRecords.pasMapRecordsCB)
	{
		DestroyMappingRecords();
		gsMappingHistoryData.sRecords.pasMapRecordsCB = NULL;
	}

	if (gsMappingHistoryData.hLock != NULL)
	{
		OSLockDestroy(gsMappingHistoryData.hLock);
		gsMappingHistoryData.hLock = NULL;
	}
}

void InsertMappingRecord(const IMG_CHAR *pszAnnotation,
                         IMG_PID uiPID,
                         IMG_CPU_VIRTADDR pvAddress,
                         IMG_CPU_PHYADDR sCpuPhyAddr,
                         IMG_UINT32 ui32CPUCacheFlags,
                         size_t uiMapOffset,
                         IMG_UINT32 ui32PageCount)
{
	MAPPING_RECORD *psRecord;

	psRecord = AcquireMappingHistoryCBSlot();
	psRecord->etype = MAP;
	psRecord->pfnRecordString = MapRecordString;

	OSStringLCopy(psRecord->u.sMapData.aszAnnotation, pszAnnotation, MAX_MAPPING_ANNOT_STR);
	psRecord->u.sMapData.uiPID = uiPID;
	psRecord->u.sMapData.pvAddress = pvAddress;
	psRecord->u.sMapData.ui32CPUCacheFlags = ui32CPUCacheFlags;
	psRecord->u.sMapData.sCpuPhyAddr = sCpuPhyAddr;
	psRecord->u.sMapData.uiMapOffset = uiMapOffset;
	psRecord->u.sMapData.ui32PageCount = ui32PageCount;
}

void InsertUnMappingRecord(IMG_CPU_VIRTADDR pvAddress,
                           IMG_CPU_PHYADDR sCpuPhyAddr,
                           IMG_UINT32 ui32CPUCacheFlags,
                           IMG_UINT32 ui32PageCount)
{
	MAPPING_RECORD *psRecord;

	psRecord = AcquireMappingHistoryCBSlot();
	psRecord->etype = UNMAP;
	psRecord->pfnRecordString = UnMapRecordString;

	psRecord->u.sUnmapData.pvAddress = pvAddress;
	psRecord->u.sUnmapData.ui32CPUCacheFlags = ui32CPUCacheFlags;
	psRecord->u.sUnmapData.sCpuPhyAddr = sCpuPhyAddr;
	psRecord->u.sUnmapData.ui32PageCount = ui32PageCount;
}
