/*************************************************************************/ /*!
@File           allocmem.h
@Title          memory allocation header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Memory-Allocation API definitions
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

#ifndef ALLOCMEM_H
#define ALLOCMEM_H

#include "img_types.h"
#include "pvr_debug.h"

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * PVRSRV_ENABLE_PROCESS_STATS enables process statistics regarding events,
 *     resources and memory across all processes
 * PVRSRV_ENABLE_MEMORY_STATS enables recording of Linux kernel memory
 *     allocations, provided that PVRSRV_ENABLE_PROCESS_STATS is enabled
 *   - Output can be found in:
 *     /(sys/kernel/debug|proc)/pvr/proc_stats/[live|retired]_pids_stats/mem_area
 * PVRSRV_DEBUG_LINUX_MEMORY_STATS provides more details about memory
 *     statistics in conjunction with PVRSRV_ENABLE_MEMORY_STATS
 * PVRSRV_DEBUG_LINUX_MEMORY_STATS_ON is defined to encompass both memory
 *     allocation statistics functionalities described above in a single macro
 */
#if defined(PVRSRV_ENABLE_PROCESS_STATS) && defined(PVRSRV_ENABLE_MEMORY_STATS) && defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS) && defined(DEBUG)
#define PVRSRV_DEBUG_LINUX_MEMORY_STATS_ON
#endif

/*
 * When using detailed memory allocation statistics, the line number and
 * file name where the allocation happened are also provided.
 * When this feature is not used, these parameters are not needed.
 */
#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS_ON)
#define DEBUG_MEMSTATS_PARAMS ,void *pvAllocFromFile, IMG_UINT32 ui32AllocFromLine
#define DEBUG_MEMSTATS_ARGS   ,pvAllocFromFile, ui32AllocFromLine
#define DEBUG_MEMSTATS_UNREF  (void)pvAllocFromFile; (void)ui32AllocFromLine;
#define DEBUG_MEMSTATS_VALUES ,__FILE__, __LINE__
#else
#define DEBUG_MEMSTATS_PARAMS /*!<
                                 * Used for PVRSRV_DEBUG_LINUX_MEMORY_STATS_ON
                                 * build option. */
#define DEBUG_MEMSTATS_ARGS   /*!<
                                 * Used for PVRSRV_DEBUG_LINUX_MEMORY_STATS_ON
                                 * build option. */
#define DEBUG_MEMSTATS_UNREF  /*!<
                                 * Used for PVRSRV_DEBUG_LINUX_MEMORY_STATS_ON
                                 * build option. */
#define DEBUG_MEMSTATS_VALUES  /*!<
                                 * Used for PVRSRV_DEBUG_LINUX_MEMORY_STATS_ON
                                 * build option. */
#endif


/**************************************************************************/ /*!
@Function       OSAllocMem
@Description    Allocates CPU memory. Contents are uninitialized.
                If passed a size of zero, function should not assert,
                but just return a NULL pointer.
@Input          ui32Size        Size of required allocation (in bytes)
@Return         Pointer to allocated memory on success.
                Otherwise NULL.
 */ /**************************************************************************/
#if defined(DOXYGEN)
void *OSAllocMem(IMG_UINT32 ui32Size);
#else
void *OSAllocMem(IMG_UINT32 ui32Size DEBUG_MEMSTATS_PARAMS);
#define OSAllocMem(_size)	(OSAllocMem)((_size) DEBUG_MEMSTATS_VALUES)
#endif

/**************************************************************************/ /*!
@Function       OSAllocZMem
@Description    Allocates CPU memory and initializes the contents to zero.
                If passed a size of zero, function should not assert,
                but just return a NULL pointer.
@Input          ui32Size        Size of required allocation (in bytes)
@Return         Pointer to allocated memory on success.
                Otherwise NULL.
 */ /**************************************************************************/
#if defined(DOXYGEN)
void *OSAllocZMem(IMG_UINT32 ui32Size);
#else
void *OSAllocZMem(IMG_UINT32 ui32Size DEBUG_MEMSTATS_PARAMS);
#define OSAllocZMem(_size)	(OSAllocZMem)((_size) DEBUG_MEMSTATS_VALUES)
#endif


/**************************************************************************/ /*!
@Function       OSAllocMemNoStats
@Description    Allocates CPU memory. Contents are uninitialized.
                 If passed a size of zero, function should not assert,
                 but just return a NULL pointer.
                 The allocated memory is not accounted for by process stats.
                 Process stats are an optional feature (enabled only when
                 PVRSRV_ENABLE_PROCESS_STATS is defined) which track the amount
                 of memory allocated to help in debugging. Where this is not
                 required, OSAllocMem() and OSAllocMemNoStats() equate to
                 the same operation.
@Input          ui32Size        Size of required allocation (in bytes)
@Return         Pointer to allocated memory on success.
                Otherwise NULL.
 */ /**************************************************************************/
void *OSAllocMemNoStats(IMG_UINT32 ui32Size);

/**************************************************************************/ /*!
@Function       OSAllocZMemNoStats
@Description    Allocates CPU memory and initializes the contents to zero.
                 If passed a size of zero, function should not assert,
                 but just return a NULL pointer.
                 The allocated memory is not accounted for by process stats.
                 Process stats are an optional feature (enabled only when
                 PVRSRV_ENABLE_PROCESS_STATS is defined) which track the amount
                 of memory allocated to help in debugging. Where this is not
                 required, OSAllocZMem() and OSAllocZMemNoStats() equate to
                 the same operation.
@Input          ui32Size        Size of required allocation (in bytes)
@Return         Pointer to allocated memory on success.
                Otherwise NULL.
 */ /**************************************************************************/
void *OSAllocZMemNoStats(IMG_UINT32 ui32Size);

/**************************************************************************/ /*!
@Function       OSFreeMem
@Description    Frees previously allocated CPU memory.
@Input          pvCpuVAddr       Pointer to the memory to be freed.
@Return         None.
 */ /**************************************************************************/
void OSFreeMem(void *pvCpuVAddr);

/**************************************************************************/ /*!
@Function       OSFreeMemNoStats
@Description    Frees previously allocated CPU memory.
                 The freed memory does not update the figures in process stats.
                 Process stats are an optional feature (enabled only when
                 PVRSRV_ENABLE_PROCESS_STATS is defined) which track the amount
                 of memory allocated to help in debugging. Where this is not
                 required, OSFreeMem() and OSFreeMemNoStats() equate to the
                 same operation.
@Input          pvCpuVAddr       Pointer to the memory to be freed.
@Return         None.
 */ /**************************************************************************/
void OSFreeMemNoStats(void *pvCpuVAddr);

/*
 * These macros allow us to catch double-free bugs on DEBUG builds and
 * prevent crashes on RELEASE builds.
 */

/*! @cond Doxygen_Suppress */
#if defined(DEBUG)
#define double_free_sentinel ((void *)&OSFreeMem)
#define ALLOCMEM_ASSERT(exp) PVR_ASSERT(exp)
#else
#define double_free_sentinel NULL
#define ALLOCMEM_ASSERT(exp) do {} while (0)
#endif
/*! @endcond */

/*! Frees memory allocated by OSAllocMem(). */
#define OSFreeMem(_ptr) do { \
		ALLOCMEM_ASSERT((_ptr) != double_free_sentinel); \
		(OSFreeMem)(_ptr); \
		(_ptr) = double_free_sentinel; \
		MSC_SUPPRESS_4127 \
	} while (0)

/*! Frees memory allocated by OSAllocMemNoStats(). */
#define OSFreeMemNoStats(_ptr) do { \
		ALLOCMEM_ASSERT((_ptr) != double_free_sentinel); \
		(OSFreeMemNoStats)(_ptr); \
		(_ptr) = double_free_sentinel; \
		MSC_SUPPRESS_4127 \
	} while (0)

#if defined(__cplusplus)
}
#endif

#endif /* ALLOCMEM_H */

/******************************************************************************
 End of file (allocmem.h)
******************************************************************************/
