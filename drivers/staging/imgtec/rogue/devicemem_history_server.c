/*************************************************************************/ /*!
@File
@Title          Devicemem history functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Devicemem history functions
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
#include "pmr.h"
#include "pvrsrv.h"
#include "pvrsrv_device.h"
#include "pvr_debug.h"
#include "devicemem_server.h"
#include "lock.h"
#include "devicemem_history_server.h"
#include "pdump_km.h"

#define ALLOCATION_LIST_NUM_ENTRIES 10000

/* data type to hold an allocation index.
 * we make it 16 bits wide if possible
 */
#if ALLOCATION_LIST_NUM_ENTRIES <= 0xFFFF
typedef uint16_t ALLOC_INDEX_T;
#else
typedef uint32_t ALLOC_INDEX_T;
#endif

/* a record describing a single allocation known to DeviceMemHistory.
 * this is an element in a doubly linked list of allocations
 */
typedef struct _RECORD_ALLOCATION_
{
	/* time when this RECORD_ALLOCATION was created/initialised */
	IMG_UINT64 ui64CreationTime;
	/* serial number of the PMR relating to this allocation */
	IMG_UINT64 ui64Serial;
	/* base DevVAddr of this allocation */
	IMG_DEV_VIRTADDR sDevVAddr;
	/* size in bytes of this allocation */
	IMG_DEVMEM_SIZE_T uiSize;
	/* Log2 page size of this allocation's GPU pages */
	IMG_UINT32 ui32Log2PageSize;
	/* Process ID (PID) this allocation belongs to */
	IMG_PID uiPID;
	/* index of previous allocation in the list */
	ALLOC_INDEX_T ui32Prev;
	/* index of next allocation in the list */
	ALLOC_INDEX_T ui32Next;
	/* annotation/name of this allocation */
	IMG_CHAR szName[DEVICEMEM_HISTORY_TEXT_BUFSZ];
} RECORD_ALLOCATION;

/* each command in the circular buffer is prefixed with an 8-bit value
 * denoting the command type
 */
typedef enum _COMMAND_TYPE_
{
	COMMAND_TYPE_NONE,
	COMMAND_TYPE_TIMESTAMP,
	COMMAND_TYPE_MAP_ALL,
	COMMAND_TYPE_UNMAP_ALL,
	COMMAND_TYPE_MAP_RANGE,
	COMMAND_TYPE_UNMAP_RANGE,
	/* sentinel value */
	COMMAND_TYPE_COUNT,
} COMMAND_TYPE;

/* Timestamp command:
 * This command is inserted into the circular buffer to provide an updated
 * timestamp.
 * The nanosecond-accuracy timestamp is packed into a 56-bit integer, in order
 * for the whole command to fit into 8 bytes.
 */
typedef struct _COMMAND_TIMESTAMP_
{
	IMG_UINT8 aui8TimeNs[7];
} COMMAND_TIMESTAMP;

/* MAP_ALL command:
 * This command denotes the allocation at the given index was wholly mapped
 * in to the GPU MMU
 */
typedef struct _COMMAND_MAP_ALL_
{
	ALLOC_INDEX_T uiAllocIndex;
} COMMAND_MAP_ALL;

/* UNMAP_ALL command:
 * This command denotes the allocation at the given index was wholly unmapped
 * from the GPU MMU
 * Note: COMMAND_MAP_ALL and COMMAND_UNMAP_ALL commands have the same layout.
 */
typedef COMMAND_MAP_ALL COMMAND_UNMAP_ALL;

/* packing attributes for the MAP_RANGE command */
#define MAP_RANGE_MAX_START ((1 << 18) - 1)
#define MAP_RANGE_MAX_RANGE ((1 << 12) - 1)

/* MAP_RANGE command:
 * Denotes a range of pages within the given allocation being mapped.
 * The range is expressed as [Page Index] + [Page Count]
 * This information is packed into a 40-bit integer, in order to make
 * the command size 8 bytes.
 */

typedef struct _COMMAND_MAP_RANGE_
{
	IMG_UINT8 aui8Data[5];
	ALLOC_INDEX_T uiAllocIndex;
} COMMAND_MAP_RANGE;

/* UNMAP_RANGE command:
 * Denotes a range of pages within the given allocation being mapped.
 * The range is expressed as [Page Index] + [Page Count]
 * This information is packed into a 40-bit integer, in order to make
 * the command size 8 bytes.
 * Note: COMMAND_MAP_RANGE and COMMAND_UNMAP_RANGE commands have the same layout.
 */
typedef COMMAND_MAP_RANGE COMMAND_UNMAP_RANGE;

/* wrapper structure for a command */
typedef struct _COMMAND_WRAPPER_
{
	IMG_UINT8 ui8Type;
	union {
		COMMAND_TIMESTAMP sTimeStamp;
		COMMAND_MAP_ALL sMapAll;
		COMMAND_UNMAP_ALL sUnmapAll;
		COMMAND_MAP_RANGE sMapRange;
		COMMAND_UNMAP_RANGE sUnmapRange;
	} u;
} COMMAND_WRAPPER;

/* target size for the circular buffer of commands */
#define CIRCULAR_BUFFER_SIZE_KB 2048
/* turn the circular buffer target size into a number of commands */
#define CIRCULAR_BUFFER_NUM_COMMANDS ((CIRCULAR_BUFFER_SIZE_KB * 1024) / sizeof(COMMAND_WRAPPER))

/* index value denoting the end of a list */
#define END_OF_LIST 0xFFFFFFFF
#define ALLOC_INDEX_TO_PTR(idx) (&(gsDevicememHistoryData.sRecords.pasAllocations[idx]))
#define CHECK_ALLOC_INDEX(idx) (idx < ALLOCATION_LIST_NUM_ENTRIES)

/* wrapper structure for the allocation records and the commands circular buffer */
typedef struct _RECORDS_
{
	RECORD_ALLOCATION *pasAllocations;
	IMG_UINT32 ui32AllocationsListHead;

	IMG_UINT32 ui32Head;
	IMG_UINT32 ui32Tail;
	COMMAND_WRAPPER *pasCircularBuffer;;
} RECORDS;

typedef struct _DEVICEMEM_HISTORY_DATA_
{
	/* debugfs entry */
	void *pvStatsEntry;

	RECORDS sRecords;
	POS_LOCK hLock;
} DEVICEMEM_HISTORY_DATA;

static DEVICEMEM_HISTORY_DATA gsDevicememHistoryData = { 0 };

static void DevicememHistoryLock(void)
{
	OSLockAcquire(gsDevicememHistoryData.hLock);
}

static void DevicememHistoryUnlock(void)
{
	OSLockRelease(gsDevicememHistoryData.hLock);
}

/* given a time stamp, calculate the age in nanoseconds */
static IMG_UINT64 _CalculateAge(IMG_UINT64 ui64Now,
						IMG_UINT64 ui64Then,
						IMG_UINT64 ui64Max)
{
	if(ui64Now >= ui64Then)
	{
		/* no clock wrap */
		return ui64Now - ui64Then;
	}
	else
	{
		/* clock has wrapped */
		return (ui64Max - ui64Then) + ui64Now + 1;
	}
}

/* AcquireCBSlot:
 * Acquire the next slot in the circular buffer and
 * move the circular buffer head along by one
 * Returns a pointer to the acquired slot.
 */
static COMMAND_WRAPPER *AcquireCBSlot(void)
{
	COMMAND_WRAPPER *psSlot;

	psSlot = &gsDevicememHistoryData.sRecords.pasCircularBuffer[gsDevicememHistoryData.sRecords.ui32Head];

	gsDevicememHistoryData.sRecords.ui32Head =
		(gsDevicememHistoryData.sRecords.ui32Head + 1)
				% CIRCULAR_BUFFER_NUM_COMMANDS;

	return psSlot;
}

/* TimeStampPack:
 * Packs the given timestamp value into the COMMAND_TIMESTAMP structure.
 * This takes a 64-bit nanosecond timestamp and packs it in to a 56-bit
 * integer in the COMMAND_TIMESTAMP command.
 */
static void TimeStampPack(COMMAND_TIMESTAMP *psTimeStamp, IMG_UINT64 ui64Now)
{
	IMG_UINT32 i;

	for(i = 0; i < IMG_ARR_NUM_ELEMS(psTimeStamp->aui8TimeNs); i++)
	{
		psTimeStamp->aui8TimeNs[i] = ui64Now & 0xFF;
		ui64Now >>= 8;
	}
}

/* packing a 64-bit nanosecond into a 7-byte integer loses the
 * top 8 bits of data. This must be taken into account when
 * comparing a full timestamp against an unpacked timestamp
 */
#define TIME_STAMP_MASK ((1LLU << 56) - 1)
#define DO_TIME_STAMP_MASK(ns64) (ns64 & TIME_STAMP_MASK)

/* TimeStampUnpack:
 * Unpack the timestamp value from the given COMMAND_TIMESTAMP command
 */
static IMG_UINT64 TimeStampUnpack(COMMAND_TIMESTAMP *psTimeStamp)
{
	IMG_UINT64 ui64TimeNs = 0;
	IMG_UINT32 i;

	for(i = IMG_ARR_NUM_ELEMS(psTimeStamp->aui8TimeNs); i > 0; i--)
	{
		ui64TimeNs <<= 8;
		ui64TimeNs |= psTimeStamp->aui8TimeNs[i - 1];
	}

	return ui64TimeNs;
}

#if defined(PDUMP)

static void EmitPDumpAllocation(IMG_UINT32 ui32AllocationIndex,
					RECORD_ALLOCATION *psAlloc)
{
	PDUMPCOMMENT("[SrvPFD] Allocation: %u"
			" Addr: " IMG_DEV_VIRTADDR_FMTSPEC
			" Size: " IMG_DEVMEM_SIZE_FMTSPEC
			" Page size: %u"
			" PID: %u"
			" Process: %s"
			" Name: %s",
			ui32AllocationIndex,
			psAlloc->sDevVAddr.uiAddr,
			psAlloc->uiSize,
			1U << psAlloc->ui32Log2PageSize,
			psAlloc->uiPID,
			OSGetCurrentClientProcessNameKM(),
			psAlloc->szName);
}

static void EmitPDumpMapUnmapAll(COMMAND_TYPE eType,
					IMG_UINT32 ui32AllocationIndex)
{
	const IMG_CHAR *pszOpName;

	switch(eType)
	{
		case COMMAND_TYPE_MAP_ALL:
			pszOpName = "MAP_ALL";
			break;
		case COMMAND_TYPE_UNMAP_ALL:
			pszOpName = "UNMAP_ALL";
			break;
		default:
			PVR_DPF((PVR_DBG_ERROR, "EmitPDumpMapUnmapAll: Invalid type: %u",
										eType));
			return;

	}

	PDUMPCOMMENT("[SrvPFD] Op: %s Allocation: %u",
								pszOpName,
								ui32AllocationIndex);
}

static void EmitPDumpMapUnmapRange(COMMAND_TYPE eType,
					IMG_UINT32 ui32AllocationIndex,
					IMG_UINT32 ui32StartPage,
					IMG_UINT32 ui32Count)
{
	const IMG_CHAR *pszOpName;

	switch(eType)
	{
		case COMMAND_TYPE_MAP_RANGE:
			pszOpName = "MAP_RANGE";
			break;
		case COMMAND_TYPE_UNMAP_RANGE:
			pszOpName = "UNMAP_RANGE";
			break;
		default:
			PVR_DPF((PVR_DBG_ERROR, "EmitPDumpMapUnmapRange: Invalid type: %u",
										eType));
			return;
	}

	PDUMPCOMMENT("[SrvPFD] Op: %s Allocation: %u Start Page: %u Count: %u",
									pszOpName,
									ui32AllocationIndex,
									ui32StartPage,
									ui32Count);
}

#endif

/* InsertTimeStampCommand:
 * Insert a timestamp command into the circular buffer.
 */
static void InsertTimeStampCommand(IMG_UINT64 ui64Now)
{
	COMMAND_WRAPPER *psCommand;

	psCommand = AcquireCBSlot();

	psCommand->ui8Type = COMMAND_TYPE_TIMESTAMP;

	TimeStampPack(&psCommand->u.sTimeStamp, ui64Now);
}

/* InsertMapAllCommand:
 * Insert a "MAP_ALL" command for the given allocation into the circular buffer
 */
static void InsertMapAllCommand(IMG_UINT32 ui32AllocIndex)
{
	COMMAND_WRAPPER *psCommand;

	psCommand = AcquireCBSlot();

	psCommand->ui8Type = COMMAND_TYPE_MAP_ALL;
	psCommand->u.sMapAll.uiAllocIndex = ui32AllocIndex;

#if defined(PDUMP)
	EmitPDumpMapUnmapAll(COMMAND_TYPE_MAP_ALL, ui32AllocIndex);
#endif
}

/* InsertUnmapAllCommand:
 * Insert a "UNMAP_ALL" command for the given allocation into the circular buffer
 */
static void InsertUnmapAllCommand(IMG_UINT32 ui32AllocIndex)
{
	COMMAND_WRAPPER *psCommand;

	psCommand = AcquireCBSlot();

	psCommand->ui8Type = COMMAND_TYPE_UNMAP_ALL;
	psCommand->u.sUnmapAll.uiAllocIndex = ui32AllocIndex;

#if defined(PDUMP)
	EmitPDumpMapUnmapAll(COMMAND_TYPE_UNMAP_ALL, ui32AllocIndex);
#endif
}

/* MapRangePack:
 * Pack the given StartPage and Count values into the 40-bit representation
 * in the MAP_RANGE command.
 */
static void MapRangePack(COMMAND_MAP_RANGE *psMapRange,
						IMG_UINT32 ui32StartPage,
						IMG_UINT32 ui32Count)
{
	IMG_UINT64 ui64Data;
	IMG_UINT32 i;

	/* we must encode the data into 40 bits:
	 *   18 bits for the start page index
	 *   12 bits for the range
	*/

	PVR_ASSERT(ui32StartPage <= MAP_RANGE_MAX_START);
	PVR_ASSERT(ui32Count <= MAP_RANGE_MAX_RANGE);

	ui64Data = (((IMG_UINT64) ui32StartPage) << 12) | ui32Count;

	for(i = 0; i < IMG_ARR_NUM_ELEMS(psMapRange->aui8Data); i++)
	{
		psMapRange->aui8Data[i] = ui64Data & 0xFF;
		ui64Data >>= 8;
	}
}

/* MapRangePack:
 * Unpack the StartPage and Count values from the 40-bit representation
 * in the MAP_RANGE command.
 */
static void MapRangeUnpack(COMMAND_MAP_RANGE *psMapRange,
						IMG_UINT32 *pui32StartPage,
						IMG_UINT32 *pui32Count)
{
	IMG_UINT64 ui64Data = 0;
	IMG_UINT32 i;

	for(i = IMG_ARR_NUM_ELEMS(psMapRange->aui8Data); i > 0; i--)
	{
		ui64Data <<= 8;
		ui64Data |= psMapRange->aui8Data[i - 1];
	}

	*pui32StartPage = (ui64Data >> 12);
	*pui32Count = ui64Data & ((1 << 12) - 1);
}

/* InsertMapRangeCommand:
 * Insert a MAP_RANGE command into the circular buffer with the given
 * StartPage and Count values.
 */
static void InsertMapRangeCommand(IMG_UINT32 ui32AllocIndex,
						IMG_UINT32 ui32StartPage,
						IMG_UINT32 ui32Count)
{
	COMMAND_WRAPPER *psCommand;

	psCommand = AcquireCBSlot();

	psCommand->ui8Type = COMMAND_TYPE_MAP_RANGE;
	psCommand->u.sMapRange.uiAllocIndex = ui32AllocIndex;

	MapRangePack(&psCommand->u.sMapRange, ui32StartPage, ui32Count);

#if defined(PDUMP)
	EmitPDumpMapUnmapRange(COMMAND_TYPE_MAP_RANGE,
							ui32AllocIndex,
							ui32StartPage,
							ui32Count);
#endif
}

/* InsertUnmapRangeCommand:
 * Insert a UNMAP_RANGE command into the circular buffer with the given
 * StartPage and Count values.
 */
static void InsertUnmapRangeCommand(IMG_UINT32 ui32AllocIndex,
						IMG_UINT32 ui32StartPage,
						IMG_UINT32 ui32Count)
{
	COMMAND_WRAPPER *psCommand;

	psCommand = AcquireCBSlot();

	psCommand->ui8Type = COMMAND_TYPE_UNMAP_RANGE;
	psCommand->u.sMapRange.uiAllocIndex = ui32AllocIndex;

	MapRangePack(&psCommand->u.sMapRange, ui32StartPage, ui32Count);

#if defined(PDUMP)
	EmitPDumpMapUnmapRange(COMMAND_TYPE_UNMAP_RANGE,
							ui32AllocIndex,
							ui32StartPage,
							ui32Count);
#endif
}

/* InsertAllocationToList:
 * Helper function for the allocation list.
 * Inserts the given allocation at the head of the list, whose current head is
 * pointed to by pui32ListHead
 */
static void InsertAllocationToList(IMG_UINT32 *pui32ListHead, IMG_UINT32 ui32Alloc)
{
	RECORD_ALLOCATION *psAlloc;

	psAlloc = ALLOC_INDEX_TO_PTR(ui32Alloc);

	if(*pui32ListHead == END_OF_LIST)
	{
		/* list is currently empty, so just replace it */
		*pui32ListHead = ui32Alloc;
		psAlloc->ui32Next = psAlloc->ui32Prev = *pui32ListHead;
	}
	else
	{
		RECORD_ALLOCATION *psHeadAlloc;
		RECORD_ALLOCATION *psTailAlloc;

		psHeadAlloc = ALLOC_INDEX_TO_PTR(*pui32ListHead);
		psTailAlloc = ALLOC_INDEX_TO_PTR(psHeadAlloc->ui32Prev);

		/* make the new alloc point forwards to the previous head */
		psAlloc->ui32Next = *pui32ListHead;
		/* make the new alloc point backwards to the previous tail */
		psAlloc->ui32Prev = psHeadAlloc->ui32Prev;

		/* the head is now our new alloc */
		*pui32ListHead = ui32Alloc;

		/* the old head now points back to the new head */
		psHeadAlloc->ui32Prev = *pui32ListHead;

		/* the tail now points forward to the new head */
		psTailAlloc->ui32Next = ui32Alloc;
	}
}

static void InsertAllocationToBusyList(IMG_UINT32 ui32Alloc)
{
	InsertAllocationToList(&gsDevicememHistoryData.sRecords.ui32AllocationsListHead, ui32Alloc);
}

/* RemoveAllocationFromList:
 * Helper function for the allocation list.
 * Removes the given allocation from the list, whose head is
 * pointed to by pui32ListHead
 */
static void RemoveAllocationFromList(IMG_UINT32 *pui32ListHead, IMG_UINT32 ui32Alloc)
{
	RECORD_ALLOCATION *psAlloc;

	psAlloc = ALLOC_INDEX_TO_PTR(ui32Alloc);

	/* if this is the only element in the list then just make the list empty */
	if((*pui32ListHead == ui32Alloc) && (psAlloc->ui32Next == ui32Alloc))
	{
		*pui32ListHead = END_OF_LIST;
	}
	else
	{
		RECORD_ALLOCATION *psPrev, *psNext;

		psPrev = ALLOC_INDEX_TO_PTR(psAlloc->ui32Prev);
		psNext = ALLOC_INDEX_TO_PTR(psAlloc->ui32Next);

		/* remove the allocation from the list */
		psPrev->ui32Next = psAlloc->ui32Next;
		psNext->ui32Prev = psAlloc->ui32Prev;

		/* if this allocation is the head then update the head */
		if(*pui32ListHead == ui32Alloc)
		{
			*pui32ListHead = psAlloc->ui32Prev;
		}
	}
}

static void RemoveAllocationFromBusyList(IMG_UINT32 ui32Alloc)
{
	RemoveAllocationFromList(&gsDevicememHistoryData.sRecords.ui32AllocationsListHead, ui32Alloc);
}

/* TouchBusyAllocation:
 * Move the given allocation to the head of the list
 */
static void TouchBusyAllocation(IMG_UINT32 ui32Alloc)
{
	RemoveAllocationFromBusyList(ui32Alloc);
	InsertAllocationToBusyList(ui32Alloc);
}

static INLINE IMG_BOOL IsAllocationListEmpty(IMG_UINT32 ui32ListHead)
{
	return ui32ListHead == END_OF_LIST;
}

/* GetOldestBusyAllocation:
 * Returns the index of the oldest allocation in the MRU list
 */
static IMG_UINT32 GetOldestBusyAllocation(void)
{
	IMG_UINT32 ui32Alloc;
	RECORD_ALLOCATION *psAlloc;

	ui32Alloc = gsDevicememHistoryData.sRecords.ui32AllocationsListHead;

	if(ui32Alloc == END_OF_LIST)
	{
		return END_OF_LIST;
	}

	psAlloc = ALLOC_INDEX_TO_PTR(ui32Alloc);

	return psAlloc->ui32Prev;
}

static IMG_UINT32 GetFreeAllocation(void)
{
	IMG_UINT32 ui32Alloc;

	ui32Alloc = GetOldestBusyAllocation();

	return ui32Alloc;
}

/* FindAllocation:
 * Searches the list of allocations and returns the index if an allocation
 * is found which matches the given properties
 */
static IMG_UINT32 FindAllocation(const IMG_CHAR *pszName,
							IMG_UINT64 ui64Serial,
							IMG_PID uiPID,
							IMG_DEV_VIRTADDR sDevVAddr,
							IMG_DEVMEM_SIZE_T uiSize)
{
	IMG_UINT32 ui32Head, ui32Index;
	RECORD_ALLOCATION *psAlloc;

	ui32Head = ui32Index = gsDevicememHistoryData.sRecords.ui32AllocationsListHead;

	if(IsAllocationListEmpty(ui32Index))
	{
		goto not_found;
	}

	do
	{
		psAlloc = &gsDevicememHistoryData.sRecords.pasAllocations[ui32Index];

		if(	(psAlloc->ui64Serial == ui64Serial) &&
			(psAlloc->sDevVAddr.uiAddr == sDevVAddr.uiAddr) &&
			(psAlloc->uiSize == uiSize) &&
			(strcmp(psAlloc->szName, pszName) == 0))
		{
			goto found;
		}

		ui32Index = psAlloc->ui32Next;
	} while(ui32Index != ui32Head);

not_found:
	/* not found */
	ui32Index = END_OF_LIST;

found:
	/* if the allocation was not found then we return END_OF_LIST.
	 * otherwise, we return the index of the allocation
	 */

	return ui32Index;
}

/* InitialiseAllocation:
 * Initialise the given allocation structure with the given properties
 */
static void InitialiseAllocation(RECORD_ALLOCATION *psAlloc,
							const IMG_CHAR *pszName,
							IMG_UINT64 ui64Serial,
							IMG_PID uiPID,
							IMG_DEV_VIRTADDR sDevVAddr,
							IMG_DEVMEM_SIZE_T uiSize,
							IMG_UINT32 ui32Log2PageSize)
{
	OSStringNCopy(psAlloc->szName, pszName, sizeof(psAlloc->szName));
	psAlloc->szName[sizeof(psAlloc->szName) - 1] = '\0';
	psAlloc->ui64Serial = ui64Serial;
	psAlloc->uiPID = uiPID;
	psAlloc->sDevVAddr = sDevVAddr;
	psAlloc->uiSize = uiSize;
	psAlloc->ui32Log2PageSize = ui32Log2PageSize;
	psAlloc->ui64CreationTime = OSClockns64();
}

/* CreateAllocation:
 * Creates a new allocation with the given properties then outputs the
 * index of the allocation
 */
static PVRSRV_ERROR CreateAllocation(const IMG_CHAR *pszName,
							IMG_UINT64 ui64Serial,
							IMG_PID uiPID,
							IMG_DEV_VIRTADDR sDevVAddr,
							IMG_DEVMEM_SIZE_T uiSize,
							IMG_UINT32 ui32Log2PageSize,
							IMG_BOOL bAutoPurge,
							IMG_UINT32 *puiAllocationIndex)
{
	IMG_UINT32 ui32Alloc;
	RECORD_ALLOCATION *psAlloc;

	ui32Alloc = GetFreeAllocation();

	psAlloc = ALLOC_INDEX_TO_PTR(ui32Alloc);

	InitialiseAllocation(ALLOC_INDEX_TO_PTR(ui32Alloc),
						pszName,
						ui64Serial,
						uiPID,
						sDevVAddr,
						uiSize,
						ui32Log2PageSize);

	/* put the newly initialised allocation at the front of the MRU list */
	TouchBusyAllocation(ui32Alloc);

	*puiAllocationIndex = ui32Alloc;

#if defined(PDUMP)
	EmitPDumpAllocation(ui32Alloc, psAlloc);
#endif

	return PVRSRV_OK;
}

/* MatchAllocation:
 * Tests if the allocation at the given index matches the supplied properties.
 * Returns IMG_TRUE if it is a match, otherwise IMG_FALSE.
 */
static IMG_BOOL MatchAllocation(IMG_UINT32 ui32AllocationIndex,
						IMG_UINT64 ui64Serial,
						IMG_DEV_VIRTADDR sDevVAddr,
						IMG_DEVMEM_SIZE_T uiSize,
						const IMG_CHAR *pszName,
						IMG_UINT32 ui32Log2PageSize,
						IMG_PID uiPID)
{
	RECORD_ALLOCATION *psAlloc;

	psAlloc = ALLOC_INDEX_TO_PTR(ui32AllocationIndex);

	return 	(psAlloc->ui64Serial == ui64Serial) &&
			(psAlloc->sDevVAddr.uiAddr == sDevVAddr.uiAddr) &&
			(psAlloc->uiSize == uiSize) &&
			(psAlloc->ui32Log2PageSize == ui32Log2PageSize) &&
			(strcmp(psAlloc->szName, pszName) == 0);
}

/* FindOrCreateAllocation:
 * Convenience function.
 * Given a set of allocation properties (serial, DevVAddr, size, name, etc),
 * this function will look for an existing record of this allocation and
 * create the allocation if there is no existing record
 */
static PVRSRV_ERROR FindOrCreateAllocation(IMG_UINT32 ui32AllocationIndexHint,
							IMG_UINT64 ui64Serial,
							IMG_DEV_VIRTADDR sDevVAddr,
							IMG_DEVMEM_SIZE_T uiSize,
							const char *pszName,
							IMG_UINT32 ui32Log2PageSize,
							IMG_PID uiPID,
							IMG_BOOL bSparse,
							IMG_UINT32 *pui32AllocationIndexOut,
							IMG_BOOL *pbCreated)
{
	IMG_UINT32 ui32AllocationIndex;

	if(ui32AllocationIndexHint != DEVICEMEM_HISTORY_ALLOC_INDEX_NONE)
	{
		IMG_BOOL bHaveAllocation;

		/* first, try to match against the index given by the client */
		bHaveAllocation = MatchAllocation(ui32AllocationIndexHint,
								ui64Serial,
								sDevVAddr,
								uiSize,
								pszName,
								ui32Log2PageSize,
								uiPID);
		if(bHaveAllocation)
		{
			*pbCreated = IMG_FALSE;
			*pui32AllocationIndexOut = ui32AllocationIndexHint;
			return PVRSRV_OK;
		}
	}

	/* if matching against the client-supplied index fails then check
	 * if the allocation exists in the list
	 */
	ui32AllocationIndex = FindAllocation(pszName,
						ui64Serial,
						uiPID,
						sDevVAddr,
						uiSize);

	/* if there is no record of the allocation then we
	 * create it now
	 */
	if(ui32AllocationIndex == END_OF_LIST)
	{
		PVRSRV_ERROR eError;
		eError = CreateAllocation(pszName,
						ui64Serial,
						uiPID,
						sDevVAddr,
						uiSize,
						ui32Log2PageSize,
						IMG_TRUE,
						&ui32AllocationIndex);

		if(eError == PVRSRV_OK)
		{
			*pui32AllocationIndexOut = ui32AllocationIndex;
			*pbCreated = IMG_TRUE;
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR,
				"%s: Failed to create record for allocation %s",
									__func__,
									pszName));
		}

		return eError;
	}
	else
	{
		/* found existing record */
		*pui32AllocationIndexOut = ui32AllocationIndex;
		*pbCreated = IMG_FALSE;
		return PVRSRV_OK;
	}

}

/* GenerateMapUnmapCommandsForSparsePMR:
 * Generate the MAP_RANGE or UNMAP_RANGE commands for the sparse PMR, using the PMR's
 * current mapping table
 *
 * PMR: The PMR whose mapping table to read.
 * ui32AllocIndex: The allocation to attribute the MAP_RANGE/UNMAP range commands to.
 * bMap: Set to TRUE for mapping or IMG_FALSE for unmapping
 *
 * This function goes through every page in the PMR's mapping table and looks for
 * virtually contiguous ranges to record as being mapped or unmapped.
 */
static void GenerateMapUnmapCommandsForSparsePMR(PMR *psPMR,
							IMG_UINT32 ui32AllocIndex,
							IMG_BOOL bMap)
{
	PMR_MAPPING_TABLE *psMappingTable;
	IMG_UINT32 ui32DonePages = 0;
	IMG_UINT32 ui32NumPages;
	IMG_UINT32 i;
	IMG_BOOL bInARun = IMG_FALSE;
	IMG_UINT32 ui32CurrentStart = 0;
	IMG_UINT32 ui32RunCount = 0;

	psMappingTable = PMR_GetMappigTable(psPMR);
	ui32NumPages = psMappingTable->ui32NumPhysChunks;

	if(ui32NumPages == 0)
	{
		/* nothing to do */
		return;
	}

	for(i = 0; i < psMappingTable->ui32NumVirtChunks; i++)
	{
		if(psMappingTable->aui32Translation[i] != TRANSLATION_INVALID)
		{
			if(!bInARun)
			{
				bInARun = IMG_TRUE;
				ui32CurrentStart = i;
				ui32RunCount = 1;
			}
			else
			{
				ui32RunCount++;
			}
		}

		if(bInARun)
		{
			/* test if we need to end this current run and generate the command,
			 * either because the next page is not virtually contiguous
			 * to the current page, we have reached the maximum range,
			 * or this is the last page in the mapping table
			 */
			if((psMappingTable->aui32Translation[i] == TRANSLATION_INVALID) ||
						(ui32RunCount == MAP_RANGE_MAX_RANGE) ||
						(i == (psMappingTable->ui32NumVirtChunks - 1)))
			{
				if(bMap)
				{
					InsertMapRangeCommand(ui32AllocIndex,
										ui32CurrentStart,
										ui32RunCount);
				}
				else
				{
					InsertUnmapRangeCommand(ui32AllocIndex,
										ui32CurrentStart,
										ui32RunCount);
				}

				ui32DonePages += ui32RunCount;

				if(ui32DonePages == ui32NumPages)
				{
					 break;
				}

				bInARun = IMG_FALSE;
			}
		}
	}

}

/* GenerateMapUnmapCommandsForChangeList:
 * Generate the MAP_RANGE or UNMAP_RANGE commands for the sparse PMR, using the
 * list of page change (page map or page unmap) indices given.
 *
 * ui32NumPages: Number of pages which have changed.
 * pui32PageList: List of indices of the pages which have changed.
 * ui32AllocIndex: The allocation to attribute the MAP_RANGE/UNMAP range commands to.
 * bMap: Set to TRUE for mapping or IMG_FALSE for unmapping
 *
 * This function goes through every page in the list and looks for
 * virtually contiguous ranges to record as being mapped or unmapped.
 */
static void GenerateMapUnmapCommandsForChangeList(IMG_UINT32 ui32NumPages,
							IMG_UINT32 *pui32PageList,
							IMG_UINT32 ui32AllocIndex,
							IMG_BOOL bMap)
{
	IMG_UINT32 i;
	IMG_BOOL bInARun = IMG_FALSE;
	IMG_UINT32 ui32CurrentStart = 0;
	IMG_UINT32 ui32RunCount = 0;

	for(i = 0; i < ui32NumPages; i++)
	{
		if(!bInARun)
		{
			bInARun = IMG_TRUE;
			ui32CurrentStart = pui32PageList[i];
		}

		ui32RunCount++;

		 /* we flush if:
		 * - the next page in the list is not one greater than the current page
		 * - this is the last page in the list
		 * - we have reached the maximum range size
		 */
		if((i == (ui32NumPages - 1)) ||
			((pui32PageList[i] + 1) != pui32PageList[i + 1]) ||
			(ui32RunCount == MAP_RANGE_MAX_RANGE))
		{
			if(bMap)
			{
				InsertMapRangeCommand(ui32AllocIndex,
									ui32CurrentStart,
									ui32RunCount);
			}
			else
			{
				InsertUnmapRangeCommand(ui32AllocIndex,
									ui32CurrentStart,
									ui32RunCount);
			}

			bInARun = IMG_FALSE;
			ui32RunCount = 0;
		}
	}
}

/* DevicememHistoryMapKM:
 * Entry point for when an allocation is mapped into the MMU GPU
 *
 * psPMR: The PMR to which the allocation belongs.
 * ui32Offset: The offset within the PMR at which the allocation begins.
 * sDevVAddr: The DevVAddr at which the allocation begins.
 * szName: Annotation/name for the allocation.
 * ui32Log2PageSize: Page size of the allocation, expressed in log2 form.
 * ui32AllocationIndex: Allocation index as provided by the client.
 *                      We will use this as a short-cut to find the allocation
 *                      in our records.
 * pui32AllocationIndexOut: An updated allocation index for the client.
 *                          This may be a new value if we just created the
 *                          allocation record.
 */
PVRSRV_ERROR DevicememHistoryMapNewKM(PMR *psPMR,
							IMG_UINT32 ui32Offset,
							IMG_DEV_VIRTADDR sDevVAddr,
							IMG_DEVMEM_SIZE_T uiSize,
							const char szName[DEVICEMEM_HISTORY_TEXT_BUFSZ],
							IMG_UINT32 ui32Log2PageSize,
							IMG_UINT32 ui32AllocationIndex,
							IMG_UINT32 *pui32AllocationIndexOut)
{
	IMG_BOOL bSparse = PMR_IsSparse(psPMR);
	IMG_UINT64 ui64Serial;
	IMG_PID uiPID = OSGetCurrentProcessID();
	PVRSRV_ERROR eError;
	IMG_BOOL bCreated;

	if((ui32AllocationIndex != DEVICEMEM_HISTORY_ALLOC_INDEX_NONE) &&
				!CHECK_ALLOC_INDEX(ui32AllocationIndex))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Invalid allocation index: %u",
								__func__,
								ui32AllocationIndex));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	PMRGetUID(psPMR, &ui64Serial);

	DevicememHistoryLock();

	eError = FindOrCreateAllocation(ui32AllocationIndex,
						ui64Serial,
						sDevVAddr,
						uiSize,
						szName,
						ui32Log2PageSize,
						uiPID,
						bSparse,
						&ui32AllocationIndex,
						&bCreated);

	if((eError == PVRSRV_OK) && !bCreated)
	{
		/* touch the allocation so it goes to the head of our MRU list */
		TouchBusyAllocation(ui32AllocationIndex);
	}
	else if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to Find or Create allocation %s (%s)",
									__func__,
									szName,
									PVRSRVGETERRORSTRING(eError)));
		goto out_unlock;
	}

	if(!bSparse)
	{
		InsertMapAllCommand(ui32AllocationIndex);
	}
	else
	{
		GenerateMapUnmapCommandsForSparsePMR(psPMR,
								ui32AllocationIndex,
								IMG_TRUE);
	}

	InsertTimeStampCommand(OSClockns64());

	*pui32AllocationIndexOut = ui32AllocationIndex;

out_unlock:
	DevicememHistoryUnlock();

	return eError;
}

static void VRangeInsertMapUnmapCommands(IMG_BOOL bMap,
							IMG_UINT32 ui32AllocationIndex,
							IMG_DEV_VIRTADDR sBaseDevVAddr,
							IMG_UINT32 ui32StartPage,
							IMG_UINT32 ui32NumPages,
							const IMG_CHAR *pszName)
{
	while(ui32NumPages > 0)
	{
		IMG_UINT32 ui32PagesToAdd;

		ui32PagesToAdd = MIN(ui32NumPages, MAP_RANGE_MAX_RANGE);

		if(ui32StartPage > MAP_RANGE_MAX_START)
		{
			PVR_DPF((PVR_DBG_WARNING, "Cannot record %s range beginning at page "
									"%u on allocation %s",
									bMap ? "map" : "unmap",
									ui32StartPage,
									pszName));
			return;
		}

		if(bMap)
		{
			InsertMapRangeCommand(ui32AllocationIndex,
								ui32StartPage,
								ui32PagesToAdd);
		}
		else
		{
			InsertUnmapRangeCommand(ui32AllocationIndex,
								ui32StartPage,
								ui32PagesToAdd);
		}

		ui32StartPage += ui32PagesToAdd;
		ui32NumPages -= ui32PagesToAdd;
	}
}

PVRSRV_ERROR DevicememHistoryMapVRangeKM(IMG_DEV_VIRTADDR sBaseDevVAddr,
						IMG_UINT32 ui32StartPage,
						IMG_UINT32 ui32NumPages,
						IMG_DEVMEM_SIZE_T uiAllocSize,
						const IMG_CHAR szName[DEVICEMEM_HISTORY_TEXT_BUFSZ],
						IMG_UINT32 ui32Log2PageSize,
						IMG_UINT32 ui32AllocationIndex,
						IMG_UINT32 *pui32AllocationIndexOut)
{
	IMG_PID uiPID = OSGetCurrentProcessID();
	PVRSRV_ERROR eError;
	IMG_BOOL bCreated;

	if((ui32AllocationIndex != DEVICEMEM_HISTORY_ALLOC_INDEX_NONE) &&
				!CHECK_ALLOC_INDEX(ui32AllocationIndex))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Invalid allocation index: %u",
								__func__,
							ui32AllocationIndex));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	DevicememHistoryLock();

	eError = FindOrCreateAllocation(ui32AllocationIndex,
						0,
						sBaseDevVAddr,
						uiAllocSize,
						szName,
						ui32Log2PageSize,
						uiPID,
						IMG_FALSE,
						&ui32AllocationIndex,
						&bCreated);

	if((eError == PVRSRV_OK) && !bCreated)
	{
		/* touch the allocation so it goes to the head of our MRU list */
		TouchBusyAllocation(ui32AllocationIndex);
	}
	else if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to Find or Create allocation %s (%s)",
									__func__,
									szName,
									PVRSRVGETERRORSTRING(eError)));
		goto out_unlock;
	}

	VRangeInsertMapUnmapCommands(IMG_TRUE,
						ui32AllocationIndex,
						sBaseDevVAddr,
						ui32StartPage,
						ui32NumPages,
						szName);

	*pui32AllocationIndexOut = ui32AllocationIndex;

out_unlock:
	DevicememHistoryUnlock();

	return eError;

}

PVRSRV_ERROR DevicememHistoryUnmapVRangeKM(IMG_DEV_VIRTADDR sBaseDevVAddr,
						IMG_UINT32 ui32StartPage,
						IMG_UINT32 ui32NumPages,
						IMG_DEVMEM_SIZE_T uiAllocSize,
						const IMG_CHAR szName[DEVICEMEM_HISTORY_TEXT_BUFSZ],
						IMG_UINT32 ui32Log2PageSize,
						IMG_UINT32 ui32AllocationIndex,
						IMG_UINT32 *pui32AllocationIndexOut)
{
	IMG_PID uiPID = OSGetCurrentProcessID();
	PVRSRV_ERROR eError;
	IMG_BOOL bCreated;

	if((ui32AllocationIndex != DEVICEMEM_HISTORY_ALLOC_INDEX_NONE) &&
				!CHECK_ALLOC_INDEX(ui32AllocationIndex))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Invalid allocation index: %u",
								__func__,
							ui32AllocationIndex));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	DevicememHistoryLock();

	eError = FindOrCreateAllocation(ui32AllocationIndex,
						0,
						sBaseDevVAddr,
						uiAllocSize,
						szName,
						ui32Log2PageSize,
						uiPID,
						IMG_FALSE,
						&ui32AllocationIndex,
						&bCreated);

	if((eError == PVRSRV_OK) && !bCreated)
	{
		/* touch the allocation so it goes to the head of our MRU list */
		TouchBusyAllocation(ui32AllocationIndex);
	}
	else if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to Find or Create allocation %s (%s)",
									__func__,
									szName,
									PVRSRVGETERRORSTRING(eError)));
		goto out_unlock;
	}

	VRangeInsertMapUnmapCommands(IMG_FALSE,
						ui32AllocationIndex,
						sBaseDevVAddr,
						ui32StartPage,
						ui32NumPages,
						szName);

	*pui32AllocationIndexOut = ui32AllocationIndex;

out_unlock:
	DevicememHistoryUnlock();

	return eError;
}



/* DevicememHistoryUnmapKM:
 * Entry point for when an allocation is unmapped from the MMU GPU
 *
 * psPMR: The PMR to which the allocation belongs.
 * ui32Offset: The offset within the PMR at which the allocation begins.
 * sDevVAddr: The DevVAddr at which the allocation begins.
 * szName: Annotation/name for the allocation.
 * ui32Log2PageSize: Page size of the allocation, expressed in log2 form.
 * ui32AllocationIndex: Allocation index as provided by the client.
 *                      We will use this as a short-cut to find the allocation
 *                      in our records.
 * pui32AllocationIndexOut: An updated allocation index for the client.
 *                          This may be a new value if we just created the
 *                          allocation record.
 */
PVRSRV_ERROR DevicememHistoryUnmapNewKM(PMR *psPMR,
							IMG_UINT32 ui32Offset,
							IMG_DEV_VIRTADDR sDevVAddr,
							IMG_DEVMEM_SIZE_T uiSize,
							const char szName[DEVICEMEM_HISTORY_TEXT_BUFSZ],
							IMG_UINT32 ui32Log2PageSize,
							IMG_UINT32 ui32AllocationIndex,
							IMG_UINT32 *pui32AllocationIndexOut)
{
	IMG_BOOL bSparse = PMR_IsSparse(psPMR);
	IMG_UINT64 ui64Serial;
	IMG_PID uiPID = OSGetCurrentProcessID();
	PVRSRV_ERROR eError;
	IMG_BOOL bCreated;

	if((ui32AllocationIndex != DEVICEMEM_HISTORY_ALLOC_INDEX_NONE) &&
				!CHECK_ALLOC_INDEX(ui32AllocationIndex))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Invalid allocation index: %u",
								__func__,
								ui32AllocationIndex));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	PMRGetUID(psPMR, &ui64Serial);

	DevicememHistoryLock();

	eError = FindOrCreateAllocation(ui32AllocationIndex,
						ui64Serial,
						sDevVAddr,
						uiSize,
						szName,
						ui32Log2PageSize,
						uiPID,
						bSparse,
						&ui32AllocationIndex,
						&bCreated);

	if((eError == PVRSRV_OK) && !bCreated)
	{
		/* touch the allocation so it goes to the head of our MRU list */
		TouchBusyAllocation(ui32AllocationIndex);
	}
	else if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to Find or Create allocation %s (%s)",
									__func__,
									szName,
									PVRSRVGETERRORSTRING(eError)));
		goto out_unlock;
	}

	if(!bSparse)
	{
		InsertUnmapAllCommand(ui32AllocationIndex);
	}
	else
	{
		GenerateMapUnmapCommandsForSparsePMR(psPMR,
								ui32AllocationIndex,
								IMG_FALSE);
	}

	InsertTimeStampCommand(OSClockns64());

	*pui32AllocationIndexOut = ui32AllocationIndex;

out_unlock:
	DevicememHistoryUnlock();

	return eError;
}

/* DevicememHistorySparseChangeKM:
 * Entry point for when a sparse allocation is changed, such that some of the
 * pages within the sparse allocation are mapped or unmapped.
 *
 * psPMR: The PMR to which the allocation belongs.
 * ui32Offset: The offset within the PMR at which the allocation begins.
 * sDevVAddr: The DevVAddr at which the allocation begins.
 * szName: Annotation/name for the allocation.
 * ui32Log2PageSize: Page size of the allocation, expressed in log2 form.
 * ui32AllocPageCount: Number of pages which have been mapped.
 * paui32AllocPageIndices: Indices of pages which have been mapped.
 * ui32FreePageCount: Number of pages which have been unmapped.
 * paui32FreePageIndices: Indices of pages which have been unmapped.
 * ui32AllocationIndex: Allocation index as provided by the client.
 *                      We will use this as a short-cut to find the allocation
 *                      in our records.
 * pui32AllocationIndexOut: An updated allocation index for the client.
 *                          This may be a new value if we just created the
 *                          allocation record.
 */
PVRSRV_ERROR DevicememHistorySparseChangeKM(PMR *psPMR,
							IMG_UINT32 ui32Offset,
							IMG_DEV_VIRTADDR sDevVAddr,
							IMG_DEVMEM_SIZE_T uiSize,
							const char szName[DEVICEMEM_HISTORY_TEXT_BUFSZ],
							IMG_UINT32 ui32Log2PageSize,
							IMG_UINT32 ui32AllocPageCount,
							IMG_UINT32 *paui32AllocPageIndices,
							IMG_UINT32 ui32FreePageCount,
							IMG_UINT32 *paui32FreePageIndices,
							IMG_UINT32 ui32AllocationIndex,
							IMG_UINT32 *pui32AllocationIndexOut)
{
	IMG_UINT64 ui64Serial;
	IMG_PID uiPID = OSGetCurrentProcessID();
	PVRSRV_ERROR eError;
	IMG_BOOL bCreated;

	if((ui32AllocationIndex != DEVICEMEM_HISTORY_ALLOC_INDEX_NONE) &&
				!CHECK_ALLOC_INDEX(ui32AllocationIndex))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Invalid allocation index: %u",
								__func__,
								ui32AllocationIndex));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	PMRGetUID(psPMR, &ui64Serial);

	DevicememHistoryLock();

	eError = FindOrCreateAllocation(ui32AllocationIndex,
						ui64Serial,
						sDevVAddr,
						uiSize,
						szName,
						ui32Log2PageSize,
						uiPID,
						IMG_TRUE /* bSparse */,
						&ui32AllocationIndex,
						&bCreated);

	if((eError == PVRSRV_OK) && !bCreated)
	{
		/* touch the allocation so it goes to the head of our MRU list */
		TouchBusyAllocation(ui32AllocationIndex);
	}
	else if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to Find or Create allocation %s (%s)",
									__func__,
									szName,
									PVRSRVGETERRORSTRING(eError)));
		goto out_unlock;
	}

	GenerateMapUnmapCommandsForChangeList(ui32AllocPageCount,
							paui32AllocPageIndices,
							ui32AllocationIndex,
							IMG_TRUE);

	GenerateMapUnmapCommandsForChangeList(ui32FreePageCount,
							paui32FreePageIndices,
							ui32AllocationIndex,
							IMG_FALSE);

	InsertTimeStampCommand(OSClockns64());

	*pui32AllocationIndexOut = ui32AllocationIndex;

out_unlock:
	DevicememHistoryUnlock();

	return eError;

}

/* CircularBufferIterateStart:
 * Initialise local state for iterating over the circular buffer
 */
static void CircularBufferIterateStart(IMG_UINT32 *pui32Head, IMG_UINT32 *pui32Iter)
{
	*pui32Head = gsDevicememHistoryData.sRecords.ui32Head;

	if(*pui32Head != 0)
	{
		*pui32Iter = *pui32Head - 1;
	}
	else
	{
		*pui32Iter = CIRCULAR_BUFFER_NUM_COMMANDS - 1;
	}
}

/* CircularBufferIteratePrevious:
 * Iterate to the previous item in the circular buffer.
 * This is called repeatedly to iterate over the whole circular buffer.
 */
static COMMAND_WRAPPER *CircularBufferIteratePrevious(IMG_UINT32 ui32Head,
							IMG_UINT32 *pui32Iter,
							COMMAND_TYPE *peType,
							IMG_BOOL *pbLast)
{
	IMG_UINT8 *pui8Header;
	COMMAND_WRAPPER *psOut = NULL;

	psOut = gsDevicememHistoryData.sRecords.pasCircularBuffer + *pui32Iter;

	pui8Header = (IMG_UINT8 *) psOut;

	/* sanity check the command looks valid.
	 * this condition should never happen, but check for it anyway
	 * and try to handle it
	 */
	if(*pui8Header >= COMMAND_TYPE_COUNT)
	{
		/* invalid header detected. Circular buffer corrupted? */
		PVR_DPF((PVR_DBG_ERROR, "CircularBufferIteratePrevious: "
							"Invalid header: %u",
							*pui8Header));
		*pbLast = IMG_TRUE;
		return NULL;
	}

	*peType = *pui8Header;

	if(*pui32Iter != 0)
	{
		(*pui32Iter)--;
	}
	else
	{
		*pui32Iter = CIRCULAR_BUFFER_NUM_COMMANDS - 1;
	}


	/* inform the caller this is the last command if either we have reached
	 * the head (where we started) or if we have reached an empty command,
	 * which means we have covered all populated entries
	 */
	if((*pui32Iter == ui32Head) || (*peType == COMMAND_TYPE_NONE))
	{
		/* this is the final iteration */
		*pbLast = IMG_TRUE;
	}

	return psOut;
}

/* MapUnmapCommandGetInfo:
 * Helper function to get the address and mapping information from a MAP_ALL, UNMAP_ALL,
 * MAP_RANGE or UNMAP_RANGE command
 */
static void MapUnmapCommandGetInfo(COMMAND_WRAPPER *psCommand,
					COMMAND_TYPE eType,
					IMG_DEV_VIRTADDR *psDevVAddrStart,
					IMG_DEV_VIRTADDR *psDevVAddrEnd,
					IMG_BOOL *pbMap,
					IMG_UINT32 *pui32AllocIndex)
{
	if((eType == COMMAND_TYPE_MAP_ALL) || ((eType == COMMAND_TYPE_UNMAP_ALL)))
	{
		COMMAND_MAP_ALL *psMapAll = &psCommand->u.sMapAll;
		RECORD_ALLOCATION *psAlloc;

		*pbMap = (eType == COMMAND_TYPE_MAP_ALL);
		*pui32AllocIndex = psMapAll->uiAllocIndex;

		psAlloc = ALLOC_INDEX_TO_PTR(psMapAll->uiAllocIndex);

		*psDevVAddrStart = psAlloc->sDevVAddr;
		psDevVAddrEnd->uiAddr = psDevVAddrStart->uiAddr + psAlloc->uiSize - 1;
	}
	else if((eType == COMMAND_TYPE_MAP_RANGE) || ((eType == COMMAND_TYPE_UNMAP_RANGE)))
	{
		COMMAND_MAP_RANGE *psMapRange = &psCommand->u.sMapRange;
		RECORD_ALLOCATION *psAlloc;
		IMG_UINT32 ui32StartPage, ui32Count;

		*pbMap = (eType == COMMAND_TYPE_MAP_RANGE);
		*pui32AllocIndex = psMapRange->uiAllocIndex;

		psAlloc = ALLOC_INDEX_TO_PTR(psMapRange->uiAllocIndex);

		MapRangeUnpack(psMapRange, &ui32StartPage, &ui32Count);

		psDevVAddrStart->uiAddr = psAlloc->sDevVAddr.uiAddr +
				((1U << psAlloc->ui32Log2PageSize) * ui32StartPage);

		psDevVAddrEnd->uiAddr = psDevVAddrStart->uiAddr +
				((1U << psAlloc->ui32Log2PageSize) * ui32Count) - 1;
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Invalid command type: %u",
								__func__,
								eType));
	}
}

/* DevicememHistoryQuery:
 * Entry point for rgxdebug to look up addresses relating to a page fault
 */
IMG_BOOL DevicememHistoryQuery(DEVICEMEM_HISTORY_QUERY_IN *psQueryIn,
                               DEVICEMEM_HISTORY_QUERY_OUT *psQueryOut,
                               IMG_UINT32 ui32PageSizeBytes,
                               IMG_BOOL bMatchAnyAllocInPage)
{
	IMG_UINT32 ui32Head, ui32Iter;
	COMMAND_TYPE eType = COMMAND_TYPE_NONE;
	COMMAND_WRAPPER *psCommand = NULL;
	IMG_BOOL bLast = IMG_FALSE;
	IMG_UINT64 ui64StartTime = OSClockns64();
	IMG_UINT64 ui64TimeNs = 0;

	/* initialise the results count for the caller */
	psQueryOut->ui32NumResults = 0;

	DevicememHistoryLock();

	/* if the search is constrained to a particular PID then we
	 * first search the list of allocations to see if this
	 * PID is known to us
	 */
	if(psQueryIn->uiPID != DEVICEMEM_HISTORY_PID_ANY)
	{
		IMG_UINT32 ui32Alloc;
		ui32Alloc = gsDevicememHistoryData.sRecords.ui32AllocationsListHead;

		while(ui32Alloc != END_OF_LIST)
		{
			RECORD_ALLOCATION *psAlloc;

			psAlloc = ALLOC_INDEX_TO_PTR(ui32Alloc);

			if(psAlloc->uiPID == psQueryIn->uiPID)
			{
				goto found_pid;
			}

			if(ui32Alloc == gsDevicememHistoryData.sRecords.ui32AllocationsListHead)
			{
				/* gone through whole list */
				break;
			}
		}

		/* PID not found, so we do not have any suitable data for this
		 * page fault
		 */
		 goto out_unlock;
	}

found_pid:

	CircularBufferIterateStart(&ui32Head, &ui32Iter);

	while(!bLast)
	{
		psCommand = CircularBufferIteratePrevious(ui32Head, &ui32Iter, &eType, &bLast);

		if(eType == COMMAND_TYPE_TIMESTAMP)
		{
			ui64TimeNs = TimeStampUnpack(&psCommand->u.sTimeStamp);
			continue;
		}

		if((eType == COMMAND_TYPE_MAP_ALL) ||
			(eType == COMMAND_TYPE_UNMAP_ALL) ||
			(eType == COMMAND_TYPE_MAP_RANGE) ||
			(eType == COMMAND_TYPE_UNMAP_RANGE))
		{
			RECORD_ALLOCATION *psAlloc;
			IMG_DEV_VIRTADDR sAllocStartAddrOrig, sAllocEndAddrOrig;
			IMG_DEV_VIRTADDR sAllocStartAddr, sAllocEndAddr;
			IMG_BOOL bMap;
			IMG_UINT32 ui32AllocIndex;

			MapUnmapCommandGetInfo(psCommand,
							eType,
							&sAllocStartAddrOrig,
							&sAllocEndAddrOrig,
							&bMap,
							&ui32AllocIndex);

			sAllocStartAddr = sAllocStartAddrOrig;
			sAllocEndAddr = sAllocEndAddrOrig;

			psAlloc = ALLOC_INDEX_TO_PTR(ui32AllocIndex);

			/* skip this command if we need to search within
			 * a particular PID, and this allocation is not from
			 * that PID
			 */
			if((psQueryIn->uiPID != DEVICEMEM_HISTORY_PID_ANY) &&
				(psAlloc->uiPID != psQueryIn->uiPID))
			{
				continue;
			}

			/* if the allocation was created after this event, then this
			 * event must be for an old/removed allocation, so skip it
			 */
			if(DO_TIME_STAMP_MASK(psAlloc->ui64CreationTime) > ui64TimeNs)
			{
				continue;
			}

			/* if the caller wants us to match any allocation in the
			 * same page as the allocation then tweak the real start/end
			 * addresses of the allocation here
			 */
			if(bMatchAnyAllocInPage)
			{
				sAllocStartAddr.uiAddr = sAllocStartAddr.uiAddr & ~(IMG_UINT64) (ui32PageSizeBytes - 1);
				sAllocEndAddr.uiAddr = (sAllocEndAddr.uiAddr + ui32PageSizeBytes - 1) & ~(IMG_UINT64) (ui32PageSizeBytes - 1);
			}

			if((psQueryIn->sDevVAddr.uiAddr >= sAllocStartAddr.uiAddr) &&
				(psQueryIn->sDevVAddr.uiAddr <  sAllocEndAddr.uiAddr))
			{
				DEVICEMEM_HISTORY_QUERY_OUT_RESULT *psResult = &psQueryOut->sResults[psQueryOut->ui32NumResults];

				OSStringNCopy(psResult->szString, psAlloc->szName, sizeof(psResult->szString));
				psResult->szString[DEVICEMEM_HISTORY_TEXT_BUFSZ - 1] = '\0';
				psResult->sBaseDevVAddr = psAlloc->sDevVAddr;
				psResult->uiSize = psAlloc->uiSize;
				psResult->bMap = bMap;
				psResult->ui64Age = _CalculateAge(ui64StartTime, ui64TimeNs, TIME_STAMP_MASK);
				psResult->ui64When = ui64TimeNs;
				/* write the responsible PID in the placeholder */
				psResult->sProcessInfo.uiPID = psAlloc->uiPID;

				if((eType == COMMAND_TYPE_MAP_ALL) || (eType == COMMAND_TYPE_UNMAP_ALL))
				{
					psResult->bRange = IMG_FALSE;
					psResult->bAll = IMG_TRUE;
				}
				else
				{
					psResult->bRange = IMG_TRUE;
					MapRangeUnpack(&psCommand->u.sMapRange,
										&psResult->ui32StartPage,
										&psResult->ui32PageCount);
					psResult->bAll = (psResult->ui32PageCount * (1U << psAlloc->ui32Log2PageSize))
											== psAlloc->uiSize;
					psResult->sMapStartAddr = sAllocStartAddrOrig;
					psResult->sMapEndAddr = sAllocEndAddrOrig;
				}

				psQueryOut->ui32NumResults++;

				if(psQueryOut->ui32NumResults == DEVICEMEM_HISTORY_QUERY_OUT_MAX_RESULTS)
				{
					break;
				}
			}
		}
	}

out_unlock:
	DevicememHistoryUnlock();

	return psQueryOut->ui32NumResults > 0;
}

static void DeviceMemHistoryFmt(IMG_CHAR szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN],
							IMG_PID uiPID,
							const IMG_CHAR *pszName,
							const IMG_CHAR *pszAction,
							IMG_DEV_VIRTADDR sDevVAddrStart,
							IMG_DEV_VIRTADDR sDevVAddrEnd,
							IMG_UINT64 ui64TimeNs)
{

	szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN - 1] = '\0';
	OSSNPrintf(szBuffer, PVR_MAX_DEBUG_MESSAGE_LEN,
				/* PID NAME MAP/UNMAP MIN-MAX SIZE AbsUS AgeUS*/
				"%04u %-40s %-10s "
				IMG_DEV_VIRTADDR_FMTSPEC "-" IMG_DEV_VIRTADDR_FMTSPEC " "
				"0x%08llX "
				"%013llu", /* 13 digits is over 2 hours of ns */
				uiPID,
				pszName,
				pszAction,
				sDevVAddrStart.uiAddr,
				sDevVAddrEnd.uiAddr,
				sDevVAddrEnd.uiAddr - sDevVAddrStart.uiAddr,
				ui64TimeNs);
}

static void DeviceMemHistoryFmtHeader(IMG_CHAR szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN])
{
	OSSNPrintf(szBuffer, PVR_MAX_DEBUG_MESSAGE_LEN,
				"%-4s %-40s %-6s   %10s   %10s   %8s %13s",
				"PID",
				"NAME",
				"ACTION",
				"ADDR MIN",
				"ADDR MAX",
				"SIZE",
				"ABS NS");
}

static const char *CommandTypeToString(COMMAND_TYPE eType)
{
	switch(eType)
	{
		case COMMAND_TYPE_MAP_ALL:
			return "MapAll";
		case COMMAND_TYPE_UNMAP_ALL:
			return "UnmapAll";
		case COMMAND_TYPE_MAP_RANGE:
			return "MapRange";
		case COMMAND_TYPE_UNMAP_RANGE:
			return "UnmapRange";
		case COMMAND_TYPE_TIMESTAMP:
			return "TimeStamp";
		default:
			return "???";
	}
}

static void DevicememHistoryPrintAll(void *pvFilePtr, OS_STATS_PRINTF_FUNC* pfnOSStatsPrintf)
{
	IMG_CHAR szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN];
	IMG_UINT32 ui32Iter;
	IMG_UINT32 ui32Head;
	IMG_BOOL bLast = IMG_FALSE;
	IMG_UINT64 ui64TimeNs = 0;
	IMG_UINT64 ui64StartTime = OSClockns64();

	DeviceMemHistoryFmtHeader(szBuffer);
	pfnOSStatsPrintf(pvFilePtr, "%s\n", szBuffer);

	CircularBufferIterateStart(&ui32Head, &ui32Iter);

	while(!bLast)
	{
		COMMAND_WRAPPER *psCommand;
		COMMAND_TYPE eType = COMMAND_TYPE_NONE;

		psCommand = CircularBufferIteratePrevious(ui32Head, &ui32Iter, &eType, &bLast);

		if(eType == COMMAND_TYPE_TIMESTAMP)
		{
			ui64TimeNs = TimeStampUnpack(&psCommand->u.sTimeStamp);
			continue;
		}


		if((eType == COMMAND_TYPE_MAP_ALL) ||
			(eType == COMMAND_TYPE_UNMAP_ALL) ||
			(eType == COMMAND_TYPE_MAP_RANGE) ||
			(eType == COMMAND_TYPE_UNMAP_RANGE))
		{
			RECORD_ALLOCATION *psAlloc;
			IMG_DEV_VIRTADDR sDevVAddrStart, sDevVAddrEnd;
			IMG_BOOL bMap;
			IMG_UINT32 ui32AllocIndex;

			MapUnmapCommandGetInfo(psCommand,
								eType,
								&sDevVAddrStart,
								&sDevVAddrEnd,
								&bMap,
								&ui32AllocIndex);

			psAlloc = ALLOC_INDEX_TO_PTR(ui32AllocIndex);

			if(DO_TIME_STAMP_MASK(psAlloc->ui64CreationTime) > ui64TimeNs)
			{
				/* if this event relates to an allocation we
				 * are no longer tracking then do not print it
				 */
				continue;
			}

			DeviceMemHistoryFmt(szBuffer,
								psAlloc->uiPID,
								psAlloc->szName,
								CommandTypeToString(eType),
								sDevVAddrStart,
								sDevVAddrEnd,
								ui64TimeNs);

			pfnOSStatsPrintf(pvFilePtr, "%s\n", szBuffer);
		}
	}

	pfnOSStatsPrintf(pvFilePtr, "\nTimestamp reference: %013llu\n", ui64StartTime);
}

static void DevicememHistoryPrintAllWrapper(void *pvFilePtr, void *pvData, OS_STATS_PRINTF_FUNC* pfnOSStatsPrintf)
{
	PVR_UNREFERENCED_PARAMETER(pvData);
	DevicememHistoryLock();
	DevicememHistoryPrintAll(pvFilePtr, pfnOSStatsPrintf);
	DevicememHistoryUnlock();
}

static PVRSRV_ERROR CreateRecords(void)
{
	gsDevicememHistoryData.sRecords.pasAllocations =
			OSAllocMem(sizeof(RECORD_ALLOCATION) * ALLOCATION_LIST_NUM_ENTRIES);

	if(gsDevicememHistoryData.sRecords.pasAllocations == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	gsDevicememHistoryData.sRecords.pasCircularBuffer =
			OSAllocMem(sizeof(COMMAND_WRAPPER) * CIRCULAR_BUFFER_NUM_COMMANDS);

	if(gsDevicememHistoryData.sRecords.pasCircularBuffer == NULL)
	{
		OSFreeMem(gsDevicememHistoryData.sRecords.pasAllocations);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	return PVRSRV_OK;
}

static void DestroyRecords(void)
{
	OSFreeMem(gsDevicememHistoryData.sRecords.pasCircularBuffer);
	OSFreeMem(gsDevicememHistoryData.sRecords.pasAllocations);
}

static void InitialiseRecords(void)
{
	IMG_UINT32 i;

	/* initialise the allocations list */

	gsDevicememHistoryData.sRecords.pasAllocations[0].ui32Prev = ALLOCATION_LIST_NUM_ENTRIES - 1;
	gsDevicememHistoryData.sRecords.pasAllocations[0].ui32Next = 1;

	for(i = 1; i < ALLOCATION_LIST_NUM_ENTRIES; i++)
	{
		gsDevicememHistoryData.sRecords.pasAllocations[i].ui32Prev = i - 1;
		gsDevicememHistoryData.sRecords.pasAllocations[i].ui32Next = i + 1;
	}

	gsDevicememHistoryData.sRecords.pasAllocations[ALLOCATION_LIST_NUM_ENTRIES - 1].ui32Next = 0;

	gsDevicememHistoryData.sRecords.ui32AllocationsListHead = 0;

	/* initialise the circular buffer with zeros so every command
	 * is initialised as a command of type COMMAND_TYPE_NONE
	 */
	OSCachedMemSet(gsDevicememHistoryData.sRecords.pasCircularBuffer,
								COMMAND_TYPE_NONE,
			sizeof(gsDevicememHistoryData.sRecords.pasCircularBuffer[0]) * CIRCULAR_BUFFER_NUM_COMMANDS);
}

PVRSRV_ERROR DevicememHistoryInitKM(void)
{
	PVRSRV_ERROR eError;

	eError = OSLockCreate(&gsDevicememHistoryData.hLock, LOCK_TYPE_PASSIVE);

	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "DevicememHistoryInitKM: Failed to create lock"));
		goto err_lock;
	}

	eError = CreateRecords();

	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "DevicememHistoryInitKM: Failed to create records"));
		goto err_allocations;
	}

	InitialiseRecords();

	gsDevicememHistoryData.pvStatsEntry = OSCreateStatisticEntry("devicemem_history",
						NULL,
						DevicememHistoryPrintAllWrapper,
						NULL,
						NULL,
						NULL);

	return PVRSRV_OK;

err_allocations:
	OSLockDestroy(gsDevicememHistoryData.hLock);
err_lock:
	return eError;
}

void DevicememHistoryDeInitKM(void)
{
	if(gsDevicememHistoryData.pvStatsEntry != NULL)
	{
		OSRemoveStatisticEntry(gsDevicememHistoryData.pvStatsEntry);
	}

	DestroyRecords();

	OSLockDestroy(gsDevicememHistoryData.hLock);
}

PVRSRV_ERROR DevicememHistoryMapKM(IMG_DEV_VIRTADDR sDevVAddr, size_t uiSize, const char szString[DEVICEMEM_HISTORY_TEXT_BUFSZ])
{
	IMG_UINT32 ui32AllocationIndex = DEVICEMEM_HISTORY_ALLOC_INDEX_NONE;
	IMG_UINT32 ui32Log2PageSize;
	IMG_UINT32 ui32StartPage;
	IMG_UINT32 ui32NumPages;

	/* assume 4K page size */
	ui32Log2PageSize = 12;

	ui32StartPage = 0;
	ui32NumPages = (uiSize + 4095) / 4096;

	return DevicememHistoryMapVRangeKM(sDevVAddr,
								ui32StartPage,
								ui32NumPages,
								uiSize,
								szString,
								ui32Log2PageSize,
								ui32AllocationIndex,
								&ui32AllocationIndex);
}

PVRSRV_ERROR DevicememHistoryUnmapKM(IMG_DEV_VIRTADDR sDevVAddr, size_t uiSize, const char szString[DEVICEMEM_HISTORY_TEXT_BUFSZ])
{
	IMG_UINT32 ui32AllocationIndex = DEVICEMEM_HISTORY_ALLOC_INDEX_NONE;
	IMG_UINT32 ui32Log2PageSize;
	IMG_UINT32 ui32StartPage;
	IMG_UINT32 ui32NumPages;

	/* assume 4K page size */
	ui32Log2PageSize = 12;

	ui32StartPage = 0;
	ui32NumPages = (uiSize + 4095) / 4096;

	return DevicememHistoryUnmapVRangeKM(sDevVAddr,
								ui32StartPage,
								ui32NumPages,
								uiSize,
								szString,
								ui32Log2PageSize,
								ui32AllocationIndex,
								&ui32AllocationIndex);
}
