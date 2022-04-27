/*************************************************************************/ /*!
@Title          Physmem_test
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Single entry point for testing of page factories
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

#include "img_defs.h"
#include "img_types.h"
#include "pvrsrv_error.h"
#include "physmem_test.h"
#include "device.h"
#include "syscommon.h"
#include "pmr.h"
#include "osfunc.h"
#include "physmem.h"
#include "physmem_osmem.h"
#include "physmem_lma.h"
#include "pvrsrv.h"

#define PHYSMEM_TEST_PAGES        2     /* Mem test pages */
#define PHYSMEM_TEST_PASSES_MAX   1000  /* Limit number of passes to some reasonable value */


/* Test patterns for mem test */

static const IMG_UINT64 gui64Patterns[] = {
	0,
	0xffffffffffffffffULL,
	0x5555555555555555ULL,
	0xaaaaaaaaaaaaaaaaULL,
	0x1111111111111111ULL,
	0x2222222222222222ULL,
	0x4444444444444444ULL,
	0x8888888888888888ULL,
	0x3333333333333333ULL,
	0x6666666666666666ULL,
	0x9999999999999999ULL,
	0xccccccccccccccccULL,
	0x7777777777777777ULL,
	0xbbbbbbbbbbbbbbbbULL,
	0xddddddddddddddddULL,
	0xeeeeeeeeeeeeeeeeULL,
	0x7a6c7258554e494cULL,
};

static const IMG_UINT32 gui32Patterns[] = {
	0,
	0xffffffffU,
	0x55555555U,
	0xaaaaaaaaU,
	0x11111111U,
	0x22222222U,
	0x44444444U,
	0x88888888U,
	0x33333333U,
	0x66666666U,
	0x99999999U,
	0xccccccccU,
	0x77777777U,
	0xbbbbbbbbU,
	0xddddddddU,
	0xeeeeeeeeU,
	0x7a6c725cU,
};

static const IMG_UINT16 gui16Patterns[] = {
	0,
	0xffffU,
	0x5555U,
	0xaaaaU,
	0x1111U,
	0x2222U,
	0x4444U,
	0x8888U,
	0x3333U,
	0x6666U,
	0x9999U,
	0xccccU,
	0x7777U,
	0xbbbbU,
	0xddddU,
	0xeeeeU,
	0x7a6cU,
};

static const IMG_UINT8 gui8Patterns[] = {
	0,
	0xffU,
	0x55U,
	0xaaU,
	0x11U,
	0x22U,
	0x44U,
	0x88U,
	0x33U,
	0x66U,
	0x99U,
	0xccU,
	0x77U,
	0xbbU,
	0xddU,
	0xeeU,
	0x6cU,
};


/* Following function does minimal required initialisation for mem test using dummy device node */
static PVRSRV_ERROR
PhysMemTestInit(PVRSRV_DEVICE_NODE **ppsDeviceNode, PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	PVRSRV_DEVICE_NODE *psDeviceNode;
	PVRSRV_ERROR eError;

	/* Dummy device node */
	psDeviceNode = OSAllocZMem(sizeof(*psDeviceNode));
	PVR_LOG_RETURN_IF_NOMEM(psDeviceNode, "OSAllocZMem");

	psDeviceNode->eDevState = PVRSRV_DEVICE_STATE_INIT;
	psDeviceNode->psDevConfig = psDevConfig;
	psDeviceNode->eCurrentSysPowerState = PVRSRV_SYS_POWER_STATE_ON;

	/* Initialise Phys mem heaps */
	eError = PVRSRVPhysMemHeapsInit(psDeviceNode, psDevConfig);
	PVR_LOG_GOTO_IF_ERROR(eError, "PVRSRVPhysMemHeapsInit", ErrorSysDevDeInit);

	psDeviceNode->sDevMMUPxSetup.uiMMUPxLog2AllocGran = OSGetPageShift();

	*ppsDeviceNode = psDeviceNode;

	return PVRSRV_OK;

ErrorSysDevDeInit:
	psDevConfig->psDevNode = NULL;
	OSFreeMem(psDeviceNode);
	return eError;
}

/* Undo initialisation done for mem test */
static void
PhysMemTestDeInit(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	/* Deinitialise Phys mem heaps */
	PVRSRVPhysMemHeapsDeinit(psDeviceNode);

	OSFreeMem(psDeviceNode);
}

/* Test for PMR factory validation */
static PVRSRV_ERROR
PMRValidationTest(PVRSRV_DEVICE_NODE *psDeviceNode, PVRSRV_MEMALLOCFLAGS_T uiFlags)
{
	PVRSRV_ERROR eError, eError1;
	IMG_UINT32 i = 0, j = 0, ui32Index = 0;
	IMG_UINT32 *pui32MappingTable = NULL;
	PMR *psPMR = NULL;
	IMG_BOOL *pbValid;
	IMG_DEV_PHYADDR *apsDevPAddr;
	IMG_UINT32 ui32NumOfPages = 10, ui32NumOfPhysPages = 5;
	size_t uiMappedSize, uiPageSize;
	IMG_UINT8 *pcWriteBuffer, *pcReadBuffer;
	IMG_HANDLE hPrivData = NULL;
	void *pvKernAddr = NULL;

	uiPageSize = OSGetPageSize();

	/* Allocate OS memory for PMR page list */
	apsDevPAddr = OSAllocMem(ui32NumOfPages * sizeof(IMG_DEV_PHYADDR));
	PVR_LOG_RETURN_IF_NOMEM(apsDevPAddr, "OSAllocMem");

	/* Allocate OS memory for PMR page state */
	pbValid = OSAllocMem(ui32NumOfPages * sizeof(IMG_BOOL));
	PVR_LOG_GOTO_IF_NOMEM(pbValid, eError, ErrorFreePMRPageListMem);
	OSCachedMemSet(pbValid, 0, ui32NumOfPages * sizeof(IMG_BOOL));

	/* Allocate OS memory for write buffer */
	pcWriteBuffer = OSAllocMem(uiPageSize);
	PVR_LOG_GOTO_IF_NOMEM(pcWriteBuffer, eError, ErrorFreePMRPageStateMem);
	OSCachedMemSet(pcWriteBuffer, 0xF, uiPageSize);

	/* Allocate OS memory for read buffer */
	pcReadBuffer = OSAllocMem(uiPageSize);
	PVR_LOG_GOTO_IF_NOMEM(pcReadBuffer, eError, ErrorFreeWriteBuffer);

	/* Allocate OS memory for mapping table */
	pui32MappingTable = (IMG_UINT32 *)OSAllocMem(ui32NumOfPhysPages * sizeof(*pui32MappingTable));
	PVR_LOG_GOTO_IF_NOMEM(pui32MappingTable, eError, ErrorFreeReadBuffer);

	/* Pages having even index will have physical backing in PMR */
	for (ui32Index=0; ui32Index < ui32NumOfPages; ui32Index+=2)
	{
		pui32MappingTable[i++] = ui32Index;
	}

	/* Allocate Sparse PMR with SPARSE | READ | WRITE | UNCACHED attributes */
	uiFlags |= PVRSRV_MEMALLOCFLAG_SPARSE_NO_DUMMY_BACKING | \
				PVRSRV_MEMALLOCFLAG_CPU_READABLE | \
				PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE | \
				PVRSRV_MEMALLOCFLAG_CPU_UNCACHED;

	/* Allocate a sparse PMR from given physical heap - CPU/GPU/FW */
	eError = PhysmemNewRamBackedPMR(NULL,
									psDeviceNode,
									ui32NumOfPages * uiPageSize,
									uiPageSize,
									ui32NumOfPhysPages,
									ui32NumOfPages,
									pui32MappingTable,
									OSGetPageShift(),
									uiFlags,
									sizeof("PMR ValidationTest"),
									"PMR ValidationTest",
									OSGetCurrentClientProcessIDKM(),
									&psPMR,
									PDUMP_NONE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to allocate a PMR"));
		goto ErrorFreeMappingTable;
	}

	/* Check whether allocated PMR can be locked and obtain physical addresses
	 * of underlying memory pages.
	 */
	eError = PMRLockSysPhysAddresses(psPMR);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to lock PMR"));
		goto ErrorUnrefPMR;
	}

	/* Get the Device physical addresses of the pages */
	eError = PMR_DevPhysAddr(psPMR, OSGetPageShift(), ui32NumOfPages, 0, apsDevPAddr, pbValid);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to map PMR pages into device physical addresses"));
		goto ErrorUnlockPhysAddresses;
	}

	/* Check whether device address of each physical page is OS PAGE_SIZE aligned */
	for (i = 0; i < ui32NumOfPages; i++)
	{
		if (pbValid[i])
		{
			if ((apsDevPAddr[i].uiAddr & OSGetPageMask()) != 0)
			{
				PVR_DPF((PVR_DBG_ERROR, "Physical memory of PMR is not page aligned"));
				eError = PVRSRV_ERROR_MEMORY_TEST_FAILED;
				goto ErrorUnlockPhysAddresses;
			}
		}
	}

	/* Acquire kernel virtual address of each physical page and write to it
	 * and then release it.
	 */
	for (i = 0; i < ui32NumOfPages; i++)
	{
		if (pbValid[i])
		{
			eError = PMRAcquireSparseKernelMappingData(psPMR, (i * uiPageSize), uiPageSize, &pvKernAddr, &uiMappedSize, &hPrivData);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "Failed to Acquire Kernel Mapping of PMR"));
				goto ErrorUnlockPhysAddresses;
			}
			OSDeviceMemCopy(pvKernAddr, pcWriteBuffer, OSGetPageSize());

			eError = PMRReleaseKernelMappingData(psPMR, hPrivData);
			PVR_LOG_IF_ERROR(eError, "PMRReleaseKernelMappingData");
		}
	}

	/* Acquire kernel virtual address of each physical page and read
	 * from it and check where contents are intact.
	 */
	for (i = 0; i < ui32NumOfPages; i++)
	{
		if (pbValid[i])
		{
			eError = PMRAcquireSparseKernelMappingData(psPMR, (i * uiPageSize), uiPageSize, &pvKernAddr, &uiMappedSize, &hPrivData);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "Failed to Acquire Kernel Mapping of PMR"));
				goto ErrorUnlockPhysAddresses;
			}
			OSCachedMemSet(pcReadBuffer, 0x0, uiPageSize);
			OSDeviceMemCopy(pcReadBuffer, pvKernAddr, uiMappedSize);

			eError = PMRReleaseKernelMappingData(psPMR, hPrivData);
			PVR_LOG_IF_ERROR(eError, "PMRReleaseKernelMappingData");

			for (j = 0; j < uiPageSize; j++)
			{
				if (pcReadBuffer[j] != pcWriteBuffer[j])
				{
					PVR_DPF((PVR_DBG_ERROR,
					         "%s: Test failed. Got (0x%hhx), expected (0x%hhx)!",
					         __func__, pcReadBuffer[j], pcWriteBuffer[j]));
					eError = PVRSRV_ERROR_MEMORY_TEST_FAILED;
					goto ErrorUnlockPhysAddresses;
				}
			}
		}
	}

ErrorUnlockPhysAddresses:
	/* Unlock and Unref the PMR to destroy it */
	eError1 = PMRUnlockSysPhysAddresses(psPMR);
	if (eError1 != PVRSRV_OK)
	{
		eError = (eError == PVRSRV_OK)? eError1 : eError;
		PVR_DPF((PVR_DBG_ERROR, "Failed to unlock PMR"));
	}

ErrorUnrefPMR:
	eError1 = PMRUnrefPMR(psPMR);
	if (eError1 != PVRSRV_OK)
	{
		eError = (eError == PVRSRV_OK)? eError1 : eError;
		PVR_DPF((PVR_DBG_ERROR, "Failed to free PMR"));
	}
ErrorFreeMappingTable:
	OSFreeMem(pui32MappingTable);
ErrorFreeReadBuffer:
	OSFreeMem(pcReadBuffer);
ErrorFreeWriteBuffer:
	OSFreeMem(pcWriteBuffer);
ErrorFreePMRPageStateMem:
	OSFreeMem(pbValid);
ErrorFreePMRPageListMem:
	OSFreeMem(apsDevPAddr);

	return eError;
}

#define DO_MEMTEST_FOR_PATTERNS(StartAddr, EndAddr, Patterns, NumOfPatterns, Error, ptr, i) \
	for (i = 0; i < NumOfPatterns; i++) \
	{ \
		/* Write pattern */ \
		for (ptr = StartAddr; ptr < EndAddr; ptr++) \
		{ \
			*ptr = Patterns[i]; \
		} \
		\
		/* Read back and validate pattern */ \
		for (ptr = StartAddr; ptr < EndAddr ; ptr++) \
		{ \
			if (*ptr != Patterns[i]) \
			{ \
				Error = PVRSRV_ERROR_MEMORY_TEST_FAILED; \
				break; \
			} \
		} \
		\
		if (Error != PVRSRV_OK) \
		{ \
			break; \
		} \
	}

static PVRSRV_ERROR
TestPatternU8(void *pvKernAddr, size_t uiMappedSize)
{
	IMG_UINT8 *StartAddr = (IMG_UINT8 *) pvKernAddr;
	IMG_UINT8 *EndAddr = ((IMG_UINT8 *) pvKernAddr) + (uiMappedSize / sizeof(IMG_UINT8));
	IMG_UINT8 *p;
	IMG_UINT32 i;
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_ASSERT((uiMappedSize % sizeof(IMG_UINT8)) == 0);

	DO_MEMTEST_FOR_PATTERNS(StartAddr, EndAddr, gui8Patterns, sizeof(gui8Patterns)/sizeof(IMG_UINT8), eError, p, i);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Test failed. Got (0x%hhx), expected (0x%hhx)!",
		         __func__, *p, gui8Patterns[i]));
	}

	return eError;
}


static PVRSRV_ERROR
TestPatternU16(void *pvKernAddr, size_t uiMappedSize)
{
	IMG_UINT16 *StartAddr = (IMG_UINT16 *) pvKernAddr;
	IMG_UINT16 *EndAddr = ((IMG_UINT16 *) pvKernAddr) + (uiMappedSize / sizeof(IMG_UINT16));
	IMG_UINT16 *p;
	IMG_UINT32 i;
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_ASSERT((uiMappedSize % sizeof(IMG_UINT16)) == 0);

	DO_MEMTEST_FOR_PATTERNS(StartAddr, EndAddr, gui16Patterns, sizeof(gui16Patterns)/sizeof(IMG_UINT16), eError, p, i);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Test failed. Got (0x%hx), expected (0x%hx)!",
		         __func__, *p, gui16Patterns[i]));
	}

	return eError;
}

static PVRSRV_ERROR
TestPatternU32(void *pvKernAddr, size_t uiMappedSize)
{
	IMG_UINT32 *StartAddr = (IMG_UINT32 *) pvKernAddr;
	IMG_UINT32 *EndAddr = ((IMG_UINT32 *) pvKernAddr) + (uiMappedSize / sizeof(IMG_UINT32));
	IMG_UINT32 *p;
	IMG_UINT32 i;
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_ASSERT((uiMappedSize % sizeof(IMG_UINT32)) == 0);

	DO_MEMTEST_FOR_PATTERNS(StartAddr, EndAddr, gui32Patterns, sizeof(gui32Patterns)/sizeof(IMG_UINT32), eError, p, i);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Test failed. Got (0x%x), expected (0x%x)!",
		         __func__, *p, gui32Patterns[i]));
	}

	return eError;
}

static PVRSRV_ERROR
TestPatternU64(void *pvKernAddr, size_t uiMappedSize)
{
	IMG_UINT64 *StartAddr = (IMG_UINT64 *) pvKernAddr;
	IMG_UINT64 *EndAddr = ((IMG_UINT64 *) pvKernAddr) + (uiMappedSize / sizeof(IMG_UINT64));
	IMG_UINT64 *p;
	IMG_UINT32 i;
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_ASSERT((uiMappedSize % sizeof(IMG_UINT64)) == 0);

	DO_MEMTEST_FOR_PATTERNS(StartAddr, EndAddr, gui64Patterns, sizeof(gui64Patterns)/sizeof(IMG_UINT64), eError, p, i);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Test failed. Got (0x%llx), expected (0x%llx)!",
		         __func__, *p, gui64Patterns[i]));
	}

	return eError;
}

static PVRSRV_ERROR
TestSplitCacheline(void *pvKernAddr, size_t uiMappedSize)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	size_t uiCacheLineSize;
	size_t uiBlockSize;
	size_t j;
	IMG_UINT8 *pcWriteBuffer, *pcReadBuffer;
	IMG_UINT8 *StartAddr = (IMG_UINT8 *) pvKernAddr;
	IMG_UINT8 *EndAddr, *p;

	uiCacheLineSize = OSCPUCacheAttributeSize(OS_CPU_CACHE_ATTRIBUTE_LINE_SIZE);

	if (uiCacheLineSize > 0)
	{
		uiBlockSize = (uiCacheLineSize * 2)/3; /* split cacheline */

		pcWriteBuffer = OSAllocMem(uiBlockSize);
		PVR_LOG_RETURN_IF_NOMEM(pcWriteBuffer, "OSAllocMem");

		/* Fill the write buffer with test data, 0xAB*/
		OSCachedMemSet(pcWriteBuffer, 0xAB, uiBlockSize);

		pcReadBuffer = OSAllocMem(uiBlockSize);
		PVR_LOG_GOTO_IF_NOMEM(pcReadBuffer, eError, ErrorFreeWriteBuffer);

		/* Fit only complete blocks in uiMappedSize, ignore leftover bytes */
		EndAddr = StartAddr + (uiBlockSize * (uiMappedSize / uiBlockSize));

		/* Write blocks into the memory */
		for (p = StartAddr; p < EndAddr; p += uiBlockSize)
		{
			OSCachedMemCopy(p, pcWriteBuffer, uiBlockSize);
		}

		/* Read back blocks and check */
		for (p = StartAddr; p < EndAddr; p += uiBlockSize)
		{
			OSCachedMemCopy(pcReadBuffer, p, uiBlockSize);

			for (j = 0; j < uiBlockSize; j++)
			{
				if (pcReadBuffer[j] != pcWriteBuffer[j])
				{
					PVR_DPF((PVR_DBG_ERROR, "%s: Test failed. Got (0x%hhx), expected (0x%hhx)!", __func__, pcReadBuffer[j], pcWriteBuffer[j]));
					eError = PVRSRV_ERROR_MEMORY_TEST_FAILED;
					goto ErrorMemTestFailed;
				}
			}
		}

ErrorMemTestFailed:
		OSFreeMem(pcReadBuffer);
ErrorFreeWriteBuffer:
		OSFreeMem(pcWriteBuffer);
	}

	return eError;
}

/* Memory test - writes and reads back different patterns to memory and validate the same */
static PVRSRV_ERROR
MemTestPatterns(PVRSRV_DEVICE_NODE *psDeviceNode, PVRSRV_MEMALLOCFLAGS_T uiFlags)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32MappingTable = 0;
	PMR *psPMR = NULL;
	size_t uiMappedSize, uiPageSize;
	IMG_HANDLE hPrivData = NULL;
	void *pvKernAddr = NULL;

	uiPageSize = OSGetPageSize();

	/* Allocate PMR with READ | WRITE | WRITE_COMBINE attributes */
	uiFlags |= PVRSRV_MEMALLOCFLAG_CPU_READABLE | \
			   PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE | \
			   PVRSRV_MEMALLOCFLAG_CPU_UNCACHED_WC;

	/*Allocate a PMR from given physical heap */
	eError = PhysmemNewRamBackedPMR(NULL,
									psDeviceNode,
									uiPageSize * PHYSMEM_TEST_PAGES,
									uiPageSize * PHYSMEM_TEST_PAGES,
									1,
									1,
									&ui32MappingTable,
									OSGetPageShift(),
									uiFlags,
									sizeof("PMR PhysMemTest"),
									"PMR PhysMemTest",
									OSGetCurrentClientProcessIDKM(),
									&psPMR,
									PDUMP_NONE);
	PVR_LOG_RETURN_IF_ERROR(eError, "PhysmemNewRamBackedPMR");

	/* Check whether allocated PMR can be locked and obtain physical
	 * addresses of underlying memory pages.
	 */
	eError = PMRLockSysPhysAddresses(psPMR);
	PVR_LOG_GOTO_IF_ERROR(eError, "PMRLockSysPhysAddresses", ErrorUnrefPMR);

	/* Map the physical page(s) into kernel space, acquire kernel mapping
	 * for PMR.
	 */
	eError = PMRAcquireKernelMappingData(psPMR, 0, uiPageSize * PHYSMEM_TEST_PAGES, &pvKernAddr, &uiMappedSize, &hPrivData);
	PVR_LOG_GOTO_IF_ERROR(eError, "PMRAcquireKernelMappingData", ErrorUnlockPhysAddresses);

	PVR_ASSERT((uiPageSize * PHYSMEM_TEST_PAGES) == uiMappedSize);

	/* Test various patterns */
	eError = TestPatternU64(pvKernAddr, uiMappedSize);
	if (eError != PVRSRV_OK)
	{
		goto ErrorReleaseKernelMappingData;
	}

	eError = TestPatternU32(pvKernAddr, uiMappedSize);
	if (eError != PVRSRV_OK)
	{
		goto ErrorReleaseKernelMappingData;
	}

	eError = TestPatternU16(pvKernAddr, uiMappedSize);
	if (eError != PVRSRV_OK)
	{
		goto ErrorReleaseKernelMappingData;
	}

	eError = TestPatternU8(pvKernAddr, uiMappedSize);
	if (eError != PVRSRV_OK)
	{
		goto ErrorReleaseKernelMappingData;
	}

	/* Test split cachelines */
	eError = TestSplitCacheline(pvKernAddr, uiMappedSize);

ErrorReleaseKernelMappingData:
	(void) PMRReleaseKernelMappingData(psPMR, hPrivData);

ErrorUnlockPhysAddresses:
	/* Unlock and Unref the PMR to destroy it, ignore returned value */
	(void) PMRUnlockSysPhysAddresses(psPMR);
ErrorUnrefPMR:
	(void) PMRUnrefPMR(psPMR);

	return eError;
}

static PVRSRV_ERROR
PhysMemTestRun(PVRSRV_DEVICE_NODE *psDeviceNode, PVRSRV_MEMALLOCFLAGS_T uiFlags, IMG_UINT32 ui32Passes)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 i;

	/* PMR validation test */
	eError = PMRValidationTest(psDeviceNode, uiFlags);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: PMR validation test failed!",
		         __func__));
		return eError;
	}

	for (i = 0; i < ui32Passes; i++)
	{
		/* Mem test */
		eError = MemTestPatterns(psDeviceNode, uiFlags);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: [Pass#%u] MemTestPatterns failed!",
			         __func__, i));
			break;
		}
	}

	return eError;
}

PVRSRV_ERROR
PhysMemTest(void *pvDevConfig, IMG_UINT32 ui32MemTestPasses)
{
	PVRSRV_DEVICE_NODE *psDeviceNode;
	PVRSRV_DEVICE_CONFIG *psDevConfig = pvDevConfig;
	PVRSRV_ERROR eError;

	/* validate memtest passes requested */
	ui32MemTestPasses = (ui32MemTestPasses > PHYSMEM_TEST_PASSES_MAX)? PHYSMEM_TEST_PASSES_MAX : ui32MemTestPasses;

	/* Do minimal initialisation before test */
	eError = PhysMemTestInit(&psDeviceNode, psDevConfig);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Test failed to initialize", __func__));
		return eError;
	}

	/* GPU local mem */
	eError = PhysMemTestRun(psDeviceNode, 0, ui32MemTestPasses);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "GPU local memory test failed!"));
		goto ErrorPhysMemTestDeinit;
	}

	/* CPU local mem */
	eError = PhysMemTestRun(psDeviceNode, PVRSRV_MEMALLOCFLAG_PHYS_HEAP_HINT(CPU_LOCAL), ui32MemTestPasses);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "CPU local memory test failed!"));
		goto ErrorPhysMemTestDeinit;
	}

	PVR_LOG(("PhysMemTest: Passed."));
	goto PhysMemTestPassed;

ErrorPhysMemTestDeinit:
	PVR_DPF((PVR_DBG_ERROR, "PhysMemTest: Failed."));
PhysMemTestPassed:
	PhysMemTestDeInit(psDeviceNode);

	return eError;
}
