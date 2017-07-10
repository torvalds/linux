/**************************************************************************/ /*!
@File
@Title          OS functions header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    OS specific API definitions
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

#ifdef DEBUG_RELEASE_BUILD
#pragma optimize( "", off )
#define DEBUG		1
#endif

#ifndef __OSFUNC_H__
#define __OSFUNC_H__


#if defined(__KERNEL__) && defined(LINUX) && !defined(__GENKSYMS__)
#define __pvrsrv_defined_struct_enum__
#include <services_kernel_client.h>
#endif

#if defined(LINUX) && defined(__KERNEL__) && !defined(NO_HARDWARE)
#include <asm/io.h>
#endif

#if defined(__QNXNTO__)
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#endif

#if defined(INTEGRITY_OS)
#include <string.h>
#endif

#include "img_types.h"
#include "pvrsrv_device.h"
#include "device.h"

/******************************************************************************
 * Static defines
 *****************************************************************************/
#define KERNEL_ID			0xffffffffL
#define ISR_ID				0xfffffffdL

/*************************************************************************/ /*!
@Function       OSClockns64
@Description    This function returns the number of ticks since system boot
                expressed in nanoseconds. Unlike OSClockns, OSClockns64 has
                a near 64-bit range.
@Return         The 64-bit clock value, in nanoseconds.
*/ /**************************************************************************/
IMG_UINT64 OSClockns64(void);

/*************************************************************************/ /*!
@Function       OSClockus64
@Description    This function returns the number of ticks since system boot
                expressed in microseconds. Unlike   OSClockus, OSClockus64 has
                a near 64-bit range.
@Return         The 64-bit clock value, in microseconds.
*/ /**************************************************************************/
IMG_UINT64 OSClockus64(void);

/*************************************************************************/ /*!
@Function       OSClockus
@Description    This function returns the number of ticks since system boot
                in microseconds.
@Return         The 32-bit clock value, in microseconds.
*/ /**************************************************************************/
IMG_UINT32 OSClockus(void);

/*************************************************************************/ /*!
@Function       OSClockms
@Description    This function returns the number of ticks since system boot
                in milliseconds.
@Return         The 32-bit clock value, in milliseconds.
*/ /**************************************************************************/
IMG_UINT32 OSClockms(void);

/*************************************************************************/ /*!
@Function       OSClockMonotonicns64
@Description    This function returns a clock value based on the system
                monotonic clock.
@Output         pui64Time     The 64-bit clock value, in nanoseconds.
@Return         Error Code.
*/ /**************************************************************************/
PVRSRV_ERROR OSClockMonotonicns64(IMG_UINT64 *pui64Time);

/*************************************************************************/ /*!
@Function       OSClockMonotonicus64
@Description    This function returns a clock value based on the system
                monotonic clock.
@Output         pui64Time     The 64-bit clock value, in microseconds.
@Return         Error Code.
*/ /**************************************************************************/
PVRSRV_ERROR OSClockMonotonicus64(IMG_UINT64 *pui64Time);

/*************************************************************************/ /*!
@Function       OSClockMonotonicRawns64
@Description    This function returns a clock value based on the system
                monotonic raw clock.
@Return         64bit ns timestamp
*/ /**************************************************************************/
IMG_UINT64 OSClockMonotonicRawns64(void);

/*************************************************************************/ /*!
@Function       OSClockMonotonicRawns64
@Description    This function returns a clock value based on the system
                monotonic raw clock.
@Return         64bit us timestamp
*/ /**************************************************************************/
IMG_UINT64 OSClockMonotonicRawus64(void);

/*************************************************************************/ /*!
@Function       OSGetPageSize
@Description    This function returns the page size.
                If the OS is not using memory mappings it should return a
                default value of 4096.
@Return         The size of a page, in bytes.
*/ /**************************************************************************/
size_t OSGetPageSize(void);

/*************************************************************************/ /*!
@Function       OSGetPageShift
@Description    This function returns the page size expressed as a power
                of two. A number of pages, left-shifted by this value, gives
                the equivalent size in bytes.
                If the OS is not using memory mappings it should return a
                default value of 12.
@Return         The page size expressed as a power of two.
*/ /**************************************************************************/
size_t OSGetPageShift(void);

/*************************************************************************/ /*!
@Function       OSGetPageMask
@Description    This function returns a bitmask that may be applied to an
                address to mask off the least-significant bits so as to
                leave the start address of the page containing that address.
@Return         The page mask.
*/ /**************************************************************************/
size_t OSGetPageMask(void);

/*************************************************************************/ /*!
@Function       OSGetOrder
@Description    This function returns the order of power of two for a given
                size. Eg. for a uSize of 4096 bytes the function would
                return 12 (4096 = 2^12).
@Input          uSize     The size in bytes.
@Return         The order of power of two.
*/ /**************************************************************************/
size_t OSGetOrder(size_t uSize);

typedef void (*PFN_MISR)(void *pvData);
typedef void (*PFN_THREAD)(void *pvData);

/**************************************************************************/ /*!
@Function       OSChangeSparseMemCPUAddrMap
@Description    This function changes the CPU mapping of the underlying
                sparse allocation. It is used by a PMR 'factory'
                implementation if that factory supports sparse
                allocations.
@Input          psPageArray        array representing the pages in the
                                   sparse allocation
@Input          sCpuVAddrBase      the virtual base address of the sparse
                                   allocation ('first' page)
@Input          sCpuPAHeapBase     the physical address of the virtual
                                   base address 'sCpuVAddrBase'
@Input          ui32AllocPageCount the number of pages referenced in
                                   'pai32AllocIndices'
@Input          pai32AllocIndices  list of indices of pages within
                                   'psPageArray' that we now want to
                                   allocate and map
@Input          ui32FreePageCount  the number of pages referenced in
                                   'pai32FreeIndices'
@Input          pai32FreeIndices   list of indices of pages within
                                   'psPageArray' we now want to
                                   unmap and free
@Input          bIsLMA             flag indicating if the sparse allocation
                                   is from LMA or UMA memory
@Return         PVRSRV_OK on success, a failure code otherwise.
 */ /**************************************************************************/
PVRSRV_ERROR OSChangeSparseMemCPUAddrMap(void **psPageArray,
                                         IMG_UINT64 sCpuVAddrBase,
                                         IMG_CPU_PHYADDR sCpuPAHeapBase,
                                         IMG_UINT32 ui32AllocPageCount,
                                         IMG_UINT32 *pai32AllocIndices,
                                         IMG_UINT32 ui32FreePageCount,
                                         IMG_UINT32 *pai32FreeIndices,
                                         IMG_BOOL bIsLMA);

/*************************************************************************/ /*!
@Function       OSInstallMISR
@Description    Installs a Mid-level Interrupt Service Routine (MISR)
                which handles higher-level processing of interrupts from
                the device (GPU).
                An MISR runs outside of interrupt context, and so may be
                descheduled. This means it can contain code that would
                not be permitted in the LISR.
                An MISR is invoked when OSScheduleMISR() is called. This
                call should be made by installed LISR once it has completed
                its interrupt processing.
                Multiple MISRs may be installed by the driver to handle
                different causes of interrupt.
@Input          pfnMISR       pointer to the function to be installed
                              as the MISR
@Input          hData         private data provided to the MISR
@Output         hMISRData     handle to the installed MISR (to be used
                              for a subsequent uninstall)
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSInstallMISR(IMG_HANDLE *hMISRData,
						   PFN_MISR pfnMISR,
						   void *hData);

/*************************************************************************/ /*!
@Function       OSUninstallMISR
@Description    Uninstalls a Mid-level Interrupt Service Routine (MISR).
@Input          hMISRData     handle to the installed MISR
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSUninstallMISR(IMG_HANDLE hMISRData);

/*************************************************************************/ /*!
@Function       OSScheduleMISR
@Description    Schedules a Mid-level Interrupt Service Routine (MISR) to be
                executed. An MISR should be executed outside of interrupt
                context, for example in a work queue.
@Input          hMISRData     handle to the installed MISR
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSScheduleMISR(IMG_HANDLE hMISRData);


/*************************************************************************/ /*!
@Function       OSThreadCreate
@Description    Creates a kernel thread and starts it running. The caller
                is responsible for informing the thread that it must finish
                and return from the pfnThread function. It is not possible
                to kill or terminate it.The new thread runs with the default
                priority provided by the Operating System.
@Output         phThread       Returned handle to the thread.
@Input          pszThreadName  Name to assign to the thread.
@Input          pfnThread      Thread entry point function.
@Input          hData          Thread specific data pointer for pfnThread().
@Return         Standard PVRSRV_ERROR error code.
*/ /**************************************************************************/

PVRSRV_ERROR OSThreadCreate(IMG_HANDLE *phThread,
							IMG_CHAR *pszThreadName,
							PFN_THREAD pfnThread,
							void *hData);

/*! Available priority levels for the creation of a new Kernel Thread. */
typedef enum priority_levels
{
	OS_THREAD_HIGHEST_PRIORITY = 0,
	OS_THREAD_HIGH_PRIORITY,
	OS_THREAD_NORMAL_PRIORITY,
	OS_THREAD_LOW_PRIORITY,
	OS_THREAD_LOWEST_PRIORITY,
	OS_THREAD_NOSET_PRIORITY,   /* With this option the priority level is is the default for the given OS */
	OS_THREAD_LAST_PRIORITY     /* This must be always the last entry */
} OS_THREAD_LEVEL;

/*************************************************************************/ /*!
@Function       OSThreadCreatePriority
@Description    As OSThreadCreate, this function creates a kernel thread and
                starts it running. The difference is that with this function
                is possible to specify the priority used to schedule the new
                thread.

@Output         phThread        Returned handle to the thread.
@Input          pszThreadName   Name to assign to the thread.
@Input          pfnThread       Thread entry point function.
@Input          hData           Thread specific data pointer for pfnThread().
@Input          eThreadPriority Priority level to assign to the new thread.
@Return         Standard PVRSRV_ERROR error code.
*/ /**************************************************************************/
PVRSRV_ERROR OSThreadCreatePriority(IMG_HANDLE *phThread,
									IMG_CHAR *pszThreadName,
									PFN_THREAD pfnThread,
									void *hData,
									OS_THREAD_LEVEL eThreadPriority);

/*************************************************************************/ /*!
@Function       OSThreadDestroy
@Description    Waits for the thread to end and then destroys the thread
                handle memory. This function will block and wait for the
                thread to finish successfully, thereby providing a sync point
                for the thread completing its work. No attempt is made to kill
                or otherwise terminate the thread.
@Input          hThread   The thread handle returned by OSThreadCreate().
@Return         Standard PVRSRV_ERROR error code.
*/ /**************************************************************************/
PVRSRV_ERROR OSThreadDestroy(IMG_HANDLE hThread);

/*************************************************************************/ /*!
@Function       OSSetThreadPriority
@Description    Set the priority and weight of a thread
@Input          hThread  			The thread handle.
@Input			nThreadPriority		The integer value of the thread priority
@Input			nThreadWeight		The integer value of the thread weight
@Return         Standard PVRSRV_ERROR error code.
*/ /**************************************************************************/
PVRSRV_ERROR OSSetThreadPriority( IMG_HANDLE hThread,
								  IMG_UINT32  nThreadPriority,
								  IMG_UINT32  nThreadWeight);

#if defined(__arm64__) || defined(__aarch64__) || defined (PVRSRV_DEVMEM_TEST_SAFE_MEMSETCPY)

/* Workarounds for assumptions made that memory will not be mapped uncached
 * in kernel or user address spaces on arm64 platforms (or other testing).
 */

/**************************************************************************/ /*!
@Function       DeviceMemSet
@Description    Set memory, whose mapping may be uncached, to a given value.
                On some architectures, additional processing may be needed
                if the mapping is uncached. In such cases, OSDeviceMemSet()
                is defined as a call to this function.
@Input          pvDest     void pointer to the memory to be set
@Input          ui8Value   byte containing the value to be set
@Input          ui32Size   the number of bytes to be set to the given value
@Return         None
 */ /**************************************************************************/
void DeviceMemSet(void *pvDest, IMG_UINT8 ui8Value, size_t ui32Size);

/**************************************************************************/ /*!
@Function       DeviceMemCopy
@Description    Copy values from one area of memory, to another, when one
                or both mappings may be uncached.
                On some architectures, additional processing may be needed
                if mappings are uncached. In such cases, OSDeviceMemCopy()
                is defined as a call to this function.
@Input          pvDst      void pointer to the destination memory
@Input          pvSrc      void pointer to the source memory
@Input          ui32Size   the number of bytes to be copied
@Return         None
 */ /**************************************************************************/
void DeviceMemCopy(void *pvDst, const void *pvSrc, size_t ui32Size);

#define OSDeviceMemSet(a,b,c)  DeviceMemSet((a), (b), (c))
#define OSDeviceMemCopy(a,b,c) DeviceMemCopy((a), (b), (c))
#define OSCachedMemSet(a,b,c)  memset((a), (b), (c))
#define OSCachedMemCopy(a,b,c) memcpy((a), (b), (c))

#else /* !(defined(__arm64__) || defined(__aarch64__) || defined(PVRSRV_DEVMEM_TEST_SAFE_MEMSETCPY)) */

/* Everything else */

/**************************************************************************/ /*!
@Function       OSDeviceMemSet
@Description    Set memory, whose mapping may be uncached, to a given value.
                On some architectures, additional processing may be needed
                if the mapping is uncached.
@Input          a     void pointer to the memory to be set
@Input          b     byte containing the value to be set
@Input          c     the number of bytes to be set to the given value
@Return         Pointer to the destination memory.
 */ /**************************************************************************/
#define OSDeviceMemSet(a,b,c) memset((a), (b), (c))

/**************************************************************************/ /*!
@Function       OSDeviceMemCopy
@Description    Copy values from one area of memory, to another, when one
                or both mappings may be uncached.
                On some architectures, additional processing may be needed
                if mappings are uncached.
@Input          a     void pointer to the destination memory
@Input          b     void pointer to the source memory
@Input          c     the number of bytes to be copied
@Return         Pointer to the destination memory.
 */ /**************************************************************************/
#define OSDeviceMemCopy(a,b,c) memcpy((a), (b), (c))

/**************************************************************************/ /*!
@Function       OSCachedMemSet
@Description    Set memory, where the mapping is known to be cached, to a
                given value. This function exists to allow an optimal memset
                to be performed when memory is known to be cached.
@Input          a     void pointer to the memory to be set
@Input          b     byte containing the value to be set
@Input          c     the number of bytes to be set to the given value
@Return         Pointer to the destination memory.
 */ /**************************************************************************/
#define OSCachedMemSet(a,b,c)  memset((a), (b), (c))

/**************************************************************************/ /*!
@Function       OSCachedMemCopy
@Description    Copy values from one area of memory, to another, when both
                mappings are known to be cached.
                This function exists to allow an optimal memcpy to be
                performed when memory is known to be cached.
@Input          a     void pointer to the destination memory
@Input          b     void pointer to the source memory
@Input          c     the number of bytes to be copied
@Return         Pointer to the destination memory.
 */ /**************************************************************************/
#define OSCachedMemCopy(a,b,c) memcpy((a), (b), (c))

#endif /* !(defined(__arm64__) || defined(__aarch64__) || defined(PVRSRV_DEVMEM_TEST_SAFE_MEMSETCPY)) */

/**************************************************************************/ /*!
@Function       OSMapPhysToLin
@Description    Maps physical memory into a linear address range.
@Input          BasePAddr    physical CPU address
@Input          ui32Bytes    number of bytes to be mapped
@Input          ui32Flags    flags denoting the caching mode to be employed
                             for the mapping (uncached/write-combined,
                             cached coherent or cached incoherent).
                             See pvrsrv_memallocflags.h for full flag bit
                             definitions.
@Return         Pointer to the new mapping if successful, NULL otherwise.
 */ /**************************************************************************/
void *OSMapPhysToLin(IMG_CPU_PHYADDR BasePAddr, size_t ui32Bytes, IMG_UINT32 ui32Flags);

/**************************************************************************/ /*!
@Function       OSUnMapPhysToLin
@Description    Unmaps physical memory previously mapped by OSMapPhysToLin().
@Input          pvLinAddr    the linear mapping to be unmapped
@Input          ui32Bytes    number of bytes to be unmapped
@Input          ui32Flags    flags denoting the caching mode that was employed
                             for the original mapping.
@Return         IMG_TRUE if unmapping was successful, IMG_FALSE otherwise.
 */ /**************************************************************************/
IMG_BOOL OSUnMapPhysToLin(void *pvLinAddr, size_t ui32Bytes, IMG_UINT32 ui32Flags);

/**************************************************************************/ /*!
@Function       OSCPUOperation
@Description    Perform the specified cache operation on the CPU.
@Input          eCacheOp      the type of cache operation to be performed
@Return         PVRSRV_OK on success, a failure code otherwise.
 */ /**************************************************************************/
PVRSRV_ERROR OSCPUOperation(PVRSRV_CACHE_OP eCacheOp);

/**************************************************************************/ /*!
@Function       OSFlushCPUCacheRangeKM
@Description    Clean and invalidate the CPU cache for the specified
                address range.
@Input          psDevNode     device on which the allocation was made
@Input          pvVirtStart   virtual start address of the range to be
                              flushed
@Input          pvVirtEnd     virtual end address of the range to be
                              flushed
@Input          sCPUPhysStart physical start address of the range to be
                              flushed
@Input          sCPUPhysEnd   physical end address of the range to be
                              flushed
@Return         None
 */ /**************************************************************************/
void OSFlushCPUCacheRangeKM(PVRSRV_DEVICE_NODE *psDevNode,
                            void *pvVirtStart,
                            void *pvVirtEnd,
                            IMG_CPU_PHYADDR sCPUPhysStart,
                            IMG_CPU_PHYADDR sCPUPhysEnd);


/**************************************************************************/ /*!
@Function       OSCleanCPUCacheRangeKM
@Description    Clean the CPU cache for the specified address range.
                This writes out the contents of the cache and unsets the
                'dirty' bit (which indicates the physical memory is
                consistent with the cache contents).
@Input          psDevNode     device on which the allocation was made
@Input          pvVirtStart   virtual start address of the range to be
                              cleaned
@Input          pvVirtEnd     virtual end address of the range to be
                              cleaned
@Input          sCPUPhysStart physical start address of the range to be
                              cleaned
@Input          sCPUPhysEnd   physical end address of the range to be
                              cleaned
@Return         None
 */ /**************************************************************************/
void OSCleanCPUCacheRangeKM(PVRSRV_DEVICE_NODE *psDevNode,
                            void *pvVirtStart,
                            void *pvVirtEnd,
                            IMG_CPU_PHYADDR sCPUPhysStart,
                            IMG_CPU_PHYADDR sCPUPhysEnd);

/**************************************************************************/ /*!
@Function       OSInvalidateCPUCacheRangeKM
@Description    Invalidate the CPU cache for the specified address range.
                The cache must reload data from those addresses if they
                are accessed.
@Input          psDevNode     device on which the allocation was made
@Input          pvVirtStart   virtual start address of the range to be
                              invalidated
@Input          pvVirtEnd     virtual end address of the range to be
                              invalidated
@Input          sCPUPhysStart physical start address of the range to be
                              invalidated
@Input          sCPUPhysEnd   physical end address of the range to be
                              invalidated
@Return         None
 */ /**************************************************************************/
void OSInvalidateCPUCacheRangeKM(PVRSRV_DEVICE_NODE *psDevNode,
                                 void *pvVirtStart,
                                 void *pvVirtEnd,
                                 IMG_CPU_PHYADDR sCPUPhysStart,
                                 IMG_CPU_PHYADDR sCPUPhysEnd);

/**************************************************************************/ /*!
@Function       OSCPUCacheOpAddressType
@Description    Returns the address type (i.e. virtual/physical/both) that is 
                used to perform cache maintenance on the CPU. This is used
				to infer whether the virtual or physical address supplied to
				the OSxxxCPUCacheRangeKM functions can be omitted when called.
@Input          uiCacheOp       the type of cache operation to be performed
@Return         PVRSRV_CACHE_OP_ADDR_TYPE
 */ /**************************************************************************/
PVRSRV_CACHE_OP_ADDR_TYPE OSCPUCacheOpAddressType(PVRSRV_CACHE_OP uiCacheOp);

/*!
 ******************************************************************************
 * Cache attribute size type
 *****************************************************************************/
typedef enum _IMG_DCACHE_ATTRIBUTE_
{
	PVR_DCACHE_LINE_SIZE = 0,    /*!< The cache line size */
	PVR_DCACHE_ATTRIBUTE_COUNT   /*!< The number of attributes (must be last) */
} IMG_DCACHE_ATTRIBUTE;

/**************************************************************************/ /*!
@Function       OSCPUCacheAttributeSize
@Description    Returns the size of a given cache attribute.
                Typically this function is used to return the cache line
                size, but may be extended to return the size of other
                cache attributes.
@Input          eCacheAttribute   the cache attribute whose size should
                                  be returned.
@Return         The size of the specified cache attribute, in bytes.
 */ /**************************************************************************/
IMG_UINT32 OSCPUCacheAttributeSize(IMG_DCACHE_ATTRIBUTE eCacheAttribute);

/*************************************************************************/ /*!
@Function       OSGetCurrentProcessID
@Description    Returns ID of current process (thread group)
@Return         ID of current process
*****************************************************************************/
IMG_PID OSGetCurrentProcessID(void);

/*************************************************************************/ /*!
@Function       OSGetCurrentProcessName
@Description    Gets the name of current process
@Return         Process name
*****************************************************************************/
IMG_CHAR *OSGetCurrentProcessName(void);

/*************************************************************************/ /*!
@Function		OSGetCurrentProcessVASpaceSize
@Description	Returns the CPU virtual address space size of current process
@Return			Process VA space size
*/ /**************************************************************************/
IMG_UINT64 OSGetCurrentProcessVASpaceSize(void);

/*************************************************************************/ /*!
@Function       OSGetCurrentThreadID
@Description    Returns ID for current thread
@Return         ID of current thread
*****************************************************************************/
uintptr_t OSGetCurrentThreadID(void);

/*************************************************************************/ /*!
@Function       OSGetCurrentClientProcessIDKM
@Description    Returns ID of current client process (thread group) which
                has made a bridge call into the server.
                For some operating systems, this may simply be the current
                process id. For others, it may be that a dedicated thread
                is used to handle the processing of bridge calls and that
                some additional processing is required to obtain the ID of
                the client process making the bridge call.
@Return         ID of current client process
*****************************************************************************/
IMG_PID OSGetCurrentClientProcessIDKM(void);

/*************************************************************************/ /*!
@Function       OSGetCurrentClientProcessNameKM
@Description    Gets the name of current client process
@Return         Client process name
*****************************************************************************/
IMG_CHAR *OSGetCurrentClientProcessNameKM(void);

/*************************************************************************/ /*!
@Function       OSGetCurrentClientThreadIDKM
@Description    Returns ID for current client thread
                For some operating systems, this may simply be the current
                thread id. For others, it may be that a dedicated thread
                is used to handle the processing of bridge calls and that
                some additional processing is require to obtain the ID of
                the client thread making the bridge call.
@Return         ID of current client thread
*****************************************************************************/
uintptr_t OSGetCurrentClientThreadIDKM(void);

/**************************************************************************/ /*!
@Function       OSMemCmp
@Description    Compares two blocks of memory for equality.
@Input          pvBufA      Pointer to the first block of memory
@Input          pvBufB      Pointer to the second block of memory
@Input          uiLen       The number of bytes to be compared
@Return         Value < 0 if pvBufA is less than pvBufB.
                Value > 0 if pvBufB is less than pvBufA.
                Value = 0 if pvBufA is equal to pvBufB.
*****************************************************************************/
IMG_INT OSMemCmp(void *pvBufA, void *pvBufB, size_t uiLen);

/*************************************************************************/ /*!
@Function       OSPhyContigPagesAlloc
@Description    Allocates a number of contiguous physical pages.
                If allocations made by this function are CPU cached then
                OSPhyContigPagesClean has to be implemented to write the
                cached data to memory.
@Input          psDevNode     the device for which the allocation is
                              required
@Input          uiSize        the size of the required allocation (in bytes)
@Output         psMemHandle   a returned handle to be used to refer to this
                              allocation
@Output         psDevPAddr    the physical address of the allocation
@Return         PVRSRV_OK on success, a failure code otherwise.
*****************************************************************************/
PVRSRV_ERROR OSPhyContigPagesAlloc(PVRSRV_DEVICE_NODE *psDevNode, size_t uiSize,
							PG_HANDLE *psMemHandle, IMG_DEV_PHYADDR *psDevPAddr);

/*************************************************************************/ /*!
@Function       OSPhyContigPagesFree
@Description    Frees a previous allocation of contiguous physical pages
@Input          psDevNode     the device on which the allocation was made
@Input          psMemHandle   the handle of the allocation to be freed
@Return         None.
*****************************************************************************/
void OSPhyContigPagesFree(PVRSRV_DEVICE_NODE *psDevNode, PG_HANDLE *psMemHandle);

/*************************************************************************/ /*!
@Function       OSPhyContigPagesMap
@Description    Maps the specified allocation of contiguous physical pages
                to a kernel virtual address
@Input          psDevNode     the device on which the allocation was made
@Input          psMemHandle   the handle of the allocation to be mapped
@Input          uiSize        the size of the allocation (in bytes)
@Input          psDevPAddr    the physical address of the allocation
@Output         pvPtr         the virtual kernel address to which the
                              allocation is now mapped
@Return         PVRSRV_OK on success, a failure code otherwise.
*****************************************************************************/
PVRSRV_ERROR OSPhyContigPagesMap(PVRSRV_DEVICE_NODE *psDevNode, PG_HANDLE *psMemHandle,
						size_t uiSize, IMG_DEV_PHYADDR *psDevPAddr,
						void **pvPtr);

/*************************************************************************/ /*!
@Function       OSPhyContigPagesUnmap
@Description    Unmaps the kernel mapping for the specified allocation of
                contiguous physical pages
@Input          psDevNode     the device on which the allocation was made
@Input          psMemHandle   the handle of the allocation to be unmapped
@Input          pvPtr         the virtual kernel address to which the
                              allocation is currently mapped
@Return         None.
*****************************************************************************/
void OSPhyContigPagesUnmap(PVRSRV_DEVICE_NODE *psDevNode, PG_HANDLE *psMemHandle, void *pvPtr);

/*************************************************************************/ /*!
@Function       OSPhyContigPagesClean
@Description    Write the content of the specified allocation from CPU cache to
                memory from (start + uiOffset) to (start + uiOffset + uiLength)
                It is expected to be implemented as a cache clean operation but
                it is allowed to fall back to a cache clean + invalidate
                (i.e. flush).
                If allocations returned by OSPhyContigPagesAlloc are always
                uncached this can be implemented as nop.
@Input          psDevNode     device on which the allocation was made
@Input          psMemHandle   the handle of the allocation to be flushed
@Input          uiOffset      the offset in bytes from the start of the 
                              allocation from where to start flushing
@Input          uiLength      the amount to flush from the offset in bytes
@Return         PVRSRV_OK on success, a failure code otherwise.
*****************************************************************************/
PVRSRV_ERROR OSPhyContigPagesClean(PVRSRV_DEVICE_NODE *psDevNode,
                                   PG_HANDLE *psMemHandle,
                                   IMG_UINT32 uiOffset,
                                   IMG_UINT32 uiLength);


/**************************************************************************/ /*!
@Function       OSInitEnvData
@Description    Called to initialise any environment-specific data. This
                could include initialising the bridge calling infrastructure
                or device memory management infrastructure.
@Return         PVRSRV_OK on success, a failure code otherwise.
 */ /**************************************************************************/
PVRSRV_ERROR OSInitEnvData(void);

/**************************************************************************/ /*!
@Function       OSDeInitEnvData
@Description    The counterpart to OSInitEnvData(). Called to free any
                resources which may have been allocated by OSInitEnvData().
@Return         None.
 */ /**************************************************************************/
void OSDeInitEnvData(void);

/**************************************************************************/ /*!
@Function       OSSScanf
@Description    OS function to support the standard C sscanf() function.
 */ /**************************************************************************/
IMG_UINT32 OSVSScanf(IMG_CHAR *pStr, const IMG_CHAR *pszFormat, ...);

/**************************************************************************/ /*!
@Function       OSStringNCopy
@Description    OS function to support the standard C strncpy() function.
 */ /**************************************************************************/
IMG_CHAR* OSStringNCopy(IMG_CHAR *pszDest, const IMG_CHAR *pszSrc, size_t uSize);

/**************************************************************************/ /*!
@Function       OSSNPrintf
@Description    OS function to support the standard C snprintf() function.
 */ /**************************************************************************/
IMG_INT32 OSSNPrintf(IMG_CHAR *pStr, size_t ui32Size, const IMG_CHAR *pszFormat, ...) __printf(3, 4);

/**************************************************************************/ /*!
@Function       OSStringLength
@Description    OS function to support the standard C strlen() function.
 */ /**************************************************************************/
size_t OSStringLength(const IMG_CHAR *pStr);

/**************************************************************************/ /*!
@Function       OSStringNLength
@Description    Return the length of a string, excluding the terminating null
                byte ('\0'), but return at most 'uiCount' bytes. Only the first
                'uiCount' bytes of 'pStr' are interrogated.
@Input          pStr     pointer to the string
@Input          uiCount  the maximum length to return
@Return         Length of the string if less than 'uiCount' bytes, otherwise
                'uiCount'.
 */ /**************************************************************************/
size_t OSStringNLength(const IMG_CHAR *pStr, size_t uiCount);

/**************************************************************************/ /*!
@Function       OSStringCompare
@Description    OS function to support the standard C strcmp() function.
 */ /**************************************************************************/
IMG_INT32 OSStringCompare(const IMG_CHAR *pStr1, const IMG_CHAR *pStr2);

/**************************************************************************/ /*!
@Function       OSStringNCompare
@Description    OS function to support the standard C strncmp() function.
 */ /**************************************************************************/
IMG_INT32 OSStringNCompare(const IMG_CHAR *pStr1, const IMG_CHAR *pStr2,
                           size_t uiSize);

/**************************************************************************/ /*!
@Function       OSStringToUINT32
@Description    Changes string to IMG_UINT32.
 */ /**************************************************************************/
PVRSRV_ERROR OSStringToUINT32(const IMG_CHAR *pStr, IMG_UINT32 ui32Base,
                              IMG_UINT32 *ui32Result);

/*************************************************************************/ /*!
@Function       OSEventObjectCreate
@Description    Create an event object.
@Input          pszName         name to assign to the new event object.
@Output         EventObject     the created event object.
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSEventObjectCreate(const IMG_CHAR *pszName,
								 IMG_HANDLE *EventObject);

/*************************************************************************/ /*!
@Function       OSEventObjectDestroy
@Description    Destroy an event object.
@Input          hEventObject    the event object to destroy.
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSEventObjectDestroy(IMG_HANDLE hEventObject);

/*************************************************************************/ /*!
@Function       OSEventObjectSignal
@Description    Signal an event object. Any thread waiting on that event
                object will be woken.
@Input          hEventObject    the event object to signal.
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSEventObjectSignal(IMG_HANDLE hEventObject);

/*************************************************************************/ /*!
@Function       OSEventObjectWait
@Description    Wait for an event object to signal. The function is passed
                an OS event object handle (which allows the OS to have the
                calling thread wait on the associated event object).
                The calling thread will be rescheduled when the associated
                event object signals.
                If the event object has not signalled after a default timeout
                period (defined in EVENT_OBJECT_TIMEOUT_MS), the function
                will return with the result code PVRSRV_ERROR_TIMEOUT.

                Note: The global bridge lock should be released while waiting
                for the event object to signal (if held by the current thread).
                The following logic should be implemented in the OS
                implementation:
                ...
                bReleasePVRLock = (!bHoldBridgeLock &&
                                   BridgeLockIsLocked() &&
                                   current == BridgeLockGetOwner());
                if (bReleasePVRLock == IMG_TRUE) OSReleaseBridgeLock();
                ...
                / * sleep & reschedule - wait for signal * /
                ...
                if (bReleasePVRLock == IMG_TRUE) OSReleaseBridgeLock();
                ...

@Input          hOSEventKM    the OS event object handle associated with
                              the event object.
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSEventObjectWait(IMG_HANDLE hOSEventKM);

/*************************************************************************/ /*!
@Function       OSEventObjectWaitTimeout
@Description    Wait for an event object to signal or timeout. The function
                is passed an OS event object handle (which allows the OS to
                have the calling thread wait on the associated event object).
                The calling thread will be rescheduled when the associated
                event object signals.
                If the event object has not signalled after the specified
                timeout period (passed in 'uiTimeoutus'), the function
                will return with the result code PVRSRV_ERROR_TIMEOUT.
                NB. The global bridge lock should be released while waiting
                for the event object to signal (if held by the current thread)
                See OSEventObjectWait() for details.
@Input          hOSEventKM    the OS event object handle associated with
                              the event object.
@Input          uiTimeoutus   the timeout period (in usecs)
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSEventObjectWaitTimeout(IMG_HANDLE hOSEventKM, IMG_UINT64 uiTimeoutus);

/*************************************************************************/ /*!
@Function       OSEventObjectWaitAndHoldBridgeLock
@Description    Wait for an event object to signal. The function is passed
                an OS event object handle (which allows the OS to have the
                calling thread wait on the associated event object).
                The calling thread will be rescheduled when the associated
                event object signals.
                If the event object has not signalled after a default timeout
                period (defined in EVENT_OBJECT_TIMEOUT_MS), the function
                will return with the result code PVRSRV_ERROR_TIMEOUT.
                The global bridge lock is held while waiting for the event
                object to signal (this will prevent other bridge calls from
                being serviced during this time).
                See OSEventObjectWait() for details.
@Input          hOSEventKM    the OS event object handle associated with
                              the event object.
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSEventObjectWaitAndHoldBridgeLock(IMG_HANDLE hOSEventKM);

/*************************************************************************/ /*!
@Function       OSEventObjectWaitTimeoutAndHoldBridgeLock
@Description    Wait for an event object to signal or timeout. The function
                is passed an OS event object handle (which allows the OS to
                have the calling thread wait on the associated event object).
                The calling thread will be rescheduled when the associated
                event object signals.
                If the event object has not signalled after the specified
                timeout period (passed in 'uiTimeoutus'), the function
                will return with the result code PVRSRV_ERROR_TIMEOUT.
                The global bridge lock is held while waiting for the event
                object to signal (this will prevent other bridge calls from
                being serviced during this time).
                See OSEventObjectWait() for details.
@Input          hOSEventKM    the OS event object handle associated with
                              the event object.
@Input          uiTimeoutus   the timeout period (in usecs)
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSEventObjectWaitTimeoutAndHoldBridgeLock(IMG_HANDLE hOSEventKM, IMG_UINT64 uiTimeoutus);

/*************************************************************************/ /*!
@Function       OSEventObjectOpen
@Description    Open an OS handle on the specified event object.
                This OS handle may then be used to make a thread wait for
                that event object to signal.
@Input          hEventObject    Event object handle.
@Output         phOSEvent       OS handle to the returned event object.
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSEventObjectOpen(IMG_HANDLE hEventObject,
											IMG_HANDLE *phOSEvent);

/*************************************************************************/ /*!
@Function       OSEventObjectClose
@Description    Close an OS handle previously opened for an event object.
@Input          hOSEventKM      OS event object handle to close.
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSEventObjectClose(IMG_HANDLE hOSEventKM);

/**************************************************************************/ /*!
@Function       OSStringCopy
@Description    OS function to support the standard C strcpy() function.
 */ /**************************************************************************/
/* Avoid macros so we don't evaluate pszSrc twice */
static INLINE IMG_CHAR *OSStringCopy(IMG_CHAR *pszDest, const IMG_CHAR *pszSrc)
{
	return OSStringNCopy(pszDest, pszSrc, OSStringLength(pszSrc) + 1);
}

/*************************************************************************/ /*!
@Function      OSWaitus
@Description   Implements a busy wait of the specified number of microseconds.
               This function does NOT release thread quanta.
@Input         ui32Timeus     The duration of the wait period (in us)
@Return        None.
*/ /**************************************************************************/
void OSWaitus(IMG_UINT32 ui32Timeus);

/*************************************************************************/ /*!
@Function       OSSleepms
@Description    Implements a sleep of the specified number of milliseconds.
                This function may allow pre-emption, meaning the thread
                may potentially not be rescheduled for a longer period.
@Input          ui32Timems    The duration of the sleep (in ms)
@Return         None.
*/ /**************************************************************************/
void OSSleepms(IMG_UINT32 ui32Timems);

/*************************************************************************/ /*!
@Function       OSReleaseThreadQuanta
@Description    Relinquishes the current thread's execution time-slice,
                permitting the OS scheduler to schedule another thread.
@Return         None.
*/ /**************************************************************************/
void OSReleaseThreadQuanta(void);

#if defined(LINUX) && defined(__KERNEL__) && !defined(NO_HARDWARE)
	#define OSReadHWReg8(addr, off)  (IMG_UINT8)readb((IMG_PBYTE)(addr) + (off))
	#define OSReadHWReg16(addr, off) (IMG_UINT16)readw((IMG_PBYTE)(addr) + (off))
	#define OSReadHWReg32(addr, off) (IMG_UINT32)readl((IMG_PBYTE)(addr) + (off))
	/* Little endian support only */
	#define OSReadHWReg64(addr, off) \
			({ \
				__typeof__(addr) _addr = addr; \
				__typeof__(off) _off = off; \
				(IMG_UINT64) \
				( \
					( (IMG_UINT64)(readl((IMG_PBYTE)(_addr) + (_off) + 4)) << 32) \
					| readl((IMG_PBYTE)(_addr) + (_off)) \
				); \
			})

	#define OSWriteHWReg8(addr, off, val)  writeb((IMG_UINT8)(val), (IMG_PBYTE)(addr) + (off))
	#define OSWriteHWReg16(addr, off, val) writew((IMG_UINT16)(val), (IMG_PBYTE)(addr) + (off))
	#define OSWriteHWReg32(addr, off, val) writel((IMG_UINT32)(val), (IMG_PBYTE)(addr) + (off))
	/* Little endian support only */
	#define OSWriteHWReg64(addr, off, val) do \
			{ \
				__typeof__(addr) _addr = addr; \
				__typeof__(off) _off = off; \
				__typeof__(val) _val = val; \
				writel((IMG_UINT32)((_val) & 0xffffffff), (_addr) + (_off));	\
				writel((IMG_UINT32)(((IMG_UINT64)(_val) >> 32) & 0xffffffff), (_addr) + (_off) + 4); \
			} while (0)

#elif defined(NO_HARDWARE)
	/* FIXME: OSReadHWReg should not exist in no hardware builds */
	#define OSReadHWReg8(addr, off)  (0x4eU)
	#define OSReadHWReg16(addr, off) (0x3a4eU)
	#define OSReadHWReg32(addr, off) (0x30f73a4eU)
	#define OSReadHWReg64(addr, off) (0x5b376c9d30f73a4eU)

	#define OSWriteHWReg8(addr, off, val)
	#define OSWriteHWReg16(addr, off, val)
	#define OSWriteHWReg32(addr, off, val)
	#define OSWriteHWReg64(addr, off, val)
#else
/*************************************************************************/ /*!
@Function       OSReadHWReg8
@Description    Read from an 8-bit memory-mapped device register.
                The implementation should not permit the compiler to
                reorder the I/O sequence.
                The implementation should ensure that for a NO_HARDWARE
                build the code does not attempt to read from a location
                but instead returns a constant value.
@Input          pvLinRegBaseAddr   The virtual base address of the register
                                   block.
@Input          ui32Offset         The byte offset from the base address of
                                   the register to be read.
@Return         The byte read.
*/ /**************************************************************************/
	IMG_UINT8 OSReadHWReg8(void *pvLinRegBaseAddr, IMG_UINT32 ui32Offset);

/*************************************************************************/ /*!
@Function       OSReadHWReg16
@Description    Read from a 16-bit memory-mapped device register.
                The implementation should not permit the compiler to
                reorder the I/O sequence.
                The implementation should ensure that for a NO_HARDWARE
                build the code does not attempt to read from a location
                but instead returns a constant value.
@Input          pvLinRegBaseAddr   The virtual base address of the register
                                   block.
@Input          ui32Offset         The byte offset from the base address of
                                   the register to be read.
@Return         The word read.
*/ /**************************************************************************/
	IMG_UINT16 OSReadHWReg16(void *pvLinRegBaseAddr, IMG_UINT32 ui32Offset);

/*************************************************************************/ /*!
@Function       OSReadHWReg32
@Description    Read from a 32-bit memory-mapped device register.
                The implementation should not permit the compiler to
                reorder the I/O sequence.
                The implementation should ensure that for a NO_HARDWARE
                build the code does not attempt to read from a location
                but instead returns a constant value.
@Input          pvLinRegBaseAddr   The virtual base address of the register
                                   block.
@Input          ui32Offset         The byte offset from the base address of
                                   the register to be read.
@Return         The long word read.
*/ /**************************************************************************/
	IMG_UINT32 OSReadHWReg32(void *pvLinRegBaseAddr, IMG_UINT32 ui32Offset);

/*************************************************************************/ /*!
@Function       OSReadHWReg64
@Description    Read from a 64-bit memory-mapped device register.
                The implementation should not permit the compiler to
                reorder the I/O sequence.
                The implementation should ensure that for a NO_HARDWARE
                build the code does not attempt to read from a location
                but instead returns a constant value.
@Input          pvLinRegBaseAddr   The virtual base address of the register
                                   block.
@Input          ui32Offset         The byte offset from the base address of
                                   the register to be read.
@Return         The long long word read.
*/ /**************************************************************************/
	IMG_UINT64 OSReadHWReg64(void *pvLinRegBaseAddr, IMG_UINT32 ui32Offset);

/*************************************************************************/ /*!
@Function       OSWriteHWReg8
@Description    Write to an 8-bit memory-mapped device register.
                The implementation should not permit the compiler to
                reorder the I/O sequence.
                The implementation should ensure that for a NO_HARDWARE
                build the code does not attempt to write to a location.
@Input          pvLinRegBaseAddr   The virtual base address of the register
                                   block.
@Input          ui32Offset         The byte offset from the base address of
                                   the register to be written to.
@Input          ui8Value           The byte to be written to the register.
@Return         None.
*/ /**************************************************************************/
	void OSWriteHWReg8(void *pvLinRegBaseAddr, IMG_UINT32 ui32Offset, IMG_UINT8 ui8Value);

/*************************************************************************/ /*!
@Function       OSWriteHWReg16
@Description    Write to a 16-bit memory-mapped device register.
                The implementation should not permit the compiler to
                reorder the I/O sequence.
                The implementation should ensure that for a NO_HARDWARE
                build the code does not attempt to write to a location.
@Input          pvLinRegBaseAddr   The virtual base address of the register
                                   block.
@Input          ui32Offset         The byte offset from the base address of
                                   the register to be written to.
@Input          ui16Value          The word to be written to the register.
@Return         None.
*/ /**************************************************************************/
	void OSWriteHWReg16(void *pvLinRegBaseAddr, IMG_UINT32 ui32Offset, IMG_UINT16 ui16Value);

/*************************************************************************/ /*!
@Function       OSWriteHWReg32
@Description    Write to a 32-bit memory-mapped device register.
                The implementation should not permit the compiler to
                reorder the I/O sequence.
                The implementation should ensure that for a NO_HARDWARE
                build the code does not attempt to write to a location.
@Input          pvLinRegBaseAddr   The virtual base address of the register
                                   block.
@Input          ui32Offset         The byte offset from the base address of
                                   the register to be written to.
@Input          ui32Value          The long word to be written to the register.
@Return         None.
*/ /**************************************************************************/
	void OSWriteHWReg32(void *pvLinRegBaseAddr, IMG_UINT32 ui32Offset, IMG_UINT32 ui32Value);

/*************************************************************************/ /*!
@Function       OSWriteHWReg64
@Description    Write to a 64-bit memory-mapped device register.
                The implementation should not permit the compiler to
                reorder the I/O sequence.
                The implementation should ensure that for a NO_HARDWARE
                build the code does not attempt to write to a location.
@Input          pvLinRegBaseAddr   The virtual base address of the register
                                   block.
@Input          ui32Offset         The byte offset from the base address of
                                   the register to be written to.
@Input          ui64Value          The long long word to be written to the
                                   register.
@Return         None.
*/ /**************************************************************************/
	void OSWriteHWReg64(void *pvLinRegBaseAddr, IMG_UINT32 ui32Offset, IMG_UINT64 ui64Value);
#endif

typedef void (*PFN_TIMER_FUNC)(void*);
/*************************************************************************/ /*!
@Function       OSAddTimer
@Description    OS specific function to install a timer callback. The
                timer will then need to be enabled, as it is disabled by
                default.
                When enabled, the callback will be invoked once the specified
                timeout has elapsed.
@Input          pfnTimerFunc    Timer callback
@Input          *pvData         Callback data
@Input          ui32MsTimeout   Callback period
@Return         Valid handle on success, NULL if a failure
*/ /**************************************************************************/
IMG_HANDLE OSAddTimer(PFN_TIMER_FUNC pfnTimerFunc, void *pvData, IMG_UINT32 ui32MsTimeout);

/*************************************************************************/ /*!
@Function       OSRemoveTimer
@Description    Removes the specified timer. The handle becomes invalid and
                should no longer be used.
@Input          hTimer          handle of the timer to be removed
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSRemoveTimer(IMG_HANDLE hTimer);

/*************************************************************************/ /*!
@Function       OSEnableTimer
@Description    Enable the specified timer. after enabling, the timer will
                invoke the associated callback at an interval determined by
                the configured timeout period until disabled.
@Input          hTimer          handle of the timer to be enabled
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSEnableTimer(IMG_HANDLE hTimer);

/*************************************************************************/ /*!
@Function       OSDisableTimer
@Description    Disable the specified timer
@Input          hTimer          handle of the timer to be disabled
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSDisableTimer(IMG_HANDLE hTimer);


/*************************************************************************/ /*!
 @Function      OSPanic
 @Description   Take action in response to an unrecoverable driver error
 @Return        None
*/ /**************************************************************************/
void OSPanic(void);

/*************************************************************************/ /*!
@Function       OSProcHasPrivSrvInit
@Description    Checks whether the current process has sufficient privileges
                to initialise services
@Return         IMG_TRUE if it does, IMG_FALSE if it does not.
*/ /**************************************************************************/
IMG_BOOL OSProcHasPrivSrvInit(void);

/*!
 ******************************************************************************
 * Access operation verification type
 *****************************************************************************/
typedef enum _img_verify_test
{
	PVR_VERIFY_WRITE = 0,  /*!< Used with OSAccessOK() to check writing is possible */
	PVR_VERIFY_READ        /*!< Used with OSAccessOK() to check reading is possible */
} IMG_VERIFY_TEST;

/*************************************************************************/ /*!
@Function       OSAccessOK
@Description    Checks that a user space pointer is valid
@Input          eVerification    the test to be verified. This can be either
                                 PVRSRV_VERIFY_WRITE or PVRSRV_VERIFY_READ.
@Input          pvUserPtr        pointer to the memory to be checked
@Input          ui32Bytes        size of the memory to be checked
@Return         IMG_TRUE if the specified access is valid, IMG_FALSE if not.
*/ /**************************************************************************/
IMG_BOOL OSAccessOK(IMG_VERIFY_TEST eVerification, void *pvUserPtr, size_t ui32Bytes);

/*************************************************************************/ /*!
@Function       OSCopyFromUser
@Description    Copy data from user-addressable memory to kernel-addressable
                memory.
                For operating systems that do not have a user/kernel space
                distinction, this function should be implemented as a stub
                which simply returns PVRSRV_ERROR_NOT_SUPPORTED.
@Input          pvProcess        handle of the connection
@Input          pvDest           pointer to the destination Kernel memory
@Input          pvSrc            pointer to the source User memory
@Input          ui32Bytes        size of the data to be copied
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSCopyToUser(void *pvProcess, void *pvDest, const void *pvSrc, size_t ui32Bytes);

/*************************************************************************/ /*!
@Function       OSCopyToUser
@Description    Copy data to user-addressable memory from kernel-addressable
                memory.
                For operating systems that do not have a user/kernel space
                distinction, this function should be implemented as a stub
                which simply returns PVRSRV_ERROR_NOT_SUPPORTED.
@Input          pvProcess        handle of the connection
@Input          pvDest           pointer to the destination User memory
@Input          pvSrc            pointer to the source Kernel memory
@Input          ui32Bytes        size of the data to be copied
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSCopyFromUser(void *pvProcess, void *pvDest, const void *pvSrc, size_t ui32Bytes);

#if defined (__linux__) || defined (WINDOWS_WDF) || defined(INTEGRITY_OS)
#define OSBridgeCopyFromUser OSCopyFromUser
#define OSBridgeCopyToUser OSCopyToUser
#else
/*************************************************************************/ /*!
@Function       OSBridgeCopyFromUser
@Description    Copy data from user-addressable memory into kernel-addressable
                memory as part of a bridge call operation.
                For operating systems that do not have a user/kernel space
                distinction, this function will require whatever implementation
                is needed to pass data for making the bridge function call.
                For operating systems which do have a user/kernel space
                distinction (such as Linux) this function may be defined so
                as to equate to a call to OSCopyFromUser().
@Input          pvProcess        handle of the connection
@Input          pvDest           pointer to the destination Kernel memory
@Input          pvSrc            pointer to the source User memory
@Input          ui32Bytes        size of the data to be copied
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSBridgeCopyFromUser (void *pvProcess,
						void *pvDest,
						const void *pvSrc,
						size_t ui32Bytes);

/*************************************************************************/ /*!
@Function       OSBridgeCopyToUser
@Description    Copy data to user-addressable memory from kernel-addressable
                memory as part of a bridge call operation.
                For operating systems that do not have a user/kernel space
                distinction, this function will require whatever implementation
                is needed to pass data for making the bridge function call.
                For operating systems which do have a user/kernel space
                distinction (such as Linux) this function may be defined so
                as to equate to a call to OSCopyToUser().
@Input          pvProcess        handle of the connection
@Input          pvDest           pointer to the destination User memory
@Input          pvSrc            pointer to the source Kernel memory
@Input          ui32Bytes        size of the data to be copied
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSBridgeCopyToUser (void *pvProcess,
						void *pvDest,
						const void *pvSrc,
						size_t ui32Bytes);
#endif

/* To be increased if required in future */
#define PVRSRV_MAX_BRIDGE_IN_SIZE      0x2000    /*!< Size of the memory block used to hold data passed in to a bridge call */
#define PVRSRV_MAX_BRIDGE_OUT_SIZE     0x1000    /*!< Size of the memory block used to hold data returned from a bridge call */

/*************************************************************************/ /*!
@Function       OSGetGlobalBridgeBuffers
@Description    Returns the addresses and sizes of the buffers used to pass
                data into and out of bridge function calls.
@Output         ppvBridgeInBuffer         pointer to the input bridge data buffer
                                          of size PVRSRV_MAX_BRIDGE_IN_SIZE.
@Output         ppvBridgeOutBuffer        pointer to the output bridge data buffer
                                          of size PVRSRV_MAX_BRIDGE_OUT_SIZE.
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSGetGlobalBridgeBuffers (void **ppvBridgeInBuffer,
									   void **ppvBridgeOutBuffer);

/*************************************************************************/ /*!
@Function       OSSetDriverSuspended
@Description    Prevent processes from using the driver while it is
                suspended. This function is not required for most operating
                systems.
@Return         IMG_TRUE on success, IMG_FALSE otherwise.
*/ /**************************************************************************/
IMG_BOOL OSSetDriverSuspended(void);

/*************************************************************************/ /*!
@Function       OSClearDriverSuspended
@Description    Re-allows processes to use the driver when it is no longer
                suspended. This function is not required for most operating
                systems.
@Return         IMG_TRUE on success, IMG_FALSE otherwise.
*/ /**************************************************************************/
IMG_BOOL OSClearDriverSuspended(void);

/*************************************************************************/ /*!
@Function       OSGetDriverSuspended
@Description    Returns whether or not processes are unable to use the driver
                (due to  it being suspended). This function is not required
                for most operating systems.
@Return         IMG_TRUE if the driver is suspended (use is not possible),
                IMG_FALSE if the driver is not suspended (use is possible).
*/ /**************************************************************************/
IMG_BOOL OSGetDriverSuspended(void);

#if defined(LINUX) && defined(__KERNEL__)
#define OSWriteMemoryBarrier() wmb()
#define OSReadMemoryBarrier() rmb()
#define OSMemoryBarrier() mb()
#else
/*************************************************************************/ /*!
@Function       OSWriteMemoryBarrier
@Description    Insert a write memory barrier.
                The write memory barrier guarantees that all store operations
                (writes) specified before the barrier will appear to happen
                before all of the store operations specified after the barrier.
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
void OSWriteMemoryBarrier(void);
#define OSReadMemoryBarrier() OSMemoryBarrier()
/*************************************************************************/ /*!
@Function       OSMemoryBarrier
@Description    Insert a read/write memory barrier.
                The read and write memory barrier guarantees that all load
                (read) and all store (write) operations specified before the
                barrier will appear to happen before all of the load/store
                operations specified after the barrier.
@Return         None.
*/ /**************************************************************************/
void OSMemoryBarrier(void);
#endif

/*************************************************************************/ /*!
@Function       PVRSRVToNativeError
@Description    Returns the OS-specific equivalent error number/code for
                the specified PVRSRV_ERROR value.
                If there is no equivalent, or the PVRSRV_ERROR value is
                PVRSRV_OK (no error), 0 is returned.
@Return         The OS equivalent error code.
*/ /**************************************************************************/
int PVRSRVToNativeError(PVRSRV_ERROR e);
#define OSPVRSRVToNativeError(e) ( (PVRSRV_OK == e)? 0: PVRSRVToNativeError(e) )


#if defined(LINUX) && defined(__KERNEL__)

/* Provide LockDep friendly definitions for Services RW locks */
#include <linux/mutex.h>
#include <linux/slab.h>
#include "allocmem.h"

typedef struct rw_semaphore *POSWR_LOCK;

#define OSWRLockCreate(ppsLock) ({ \
	PVRSRV_ERROR e = PVRSRV_ERROR_OUT_OF_MEMORY; \
	*(ppsLock) = OSAllocMem(sizeof(struct rw_semaphore)); \
	if (*(ppsLock)) { init_rwsem(*(ppsLock)); e = PVRSRV_OK; }; \
	e;})
#define OSWRLockDestroy(psLock) ({OSFreeMem(psLock); PVRSRV_OK;})

#define OSWRLockAcquireRead(psLock) ({down_read(psLock); PVRSRV_OK;})
#define OSWRLockReleaseRead(psLock) ({up_read(psLock); PVRSRV_OK;})
#define OSWRLockAcquireWrite(psLock) ({down_write(psLock); PVRSRV_OK;})
#define OSWRLockReleaseWrite(psLock) ({up_write(psLock); PVRSRV_OK;})

#elif defined(LINUX) || defined(__QNXNTO__) || defined (INTEGRITY_OS)
/* User-mode unit tests use these definitions on Linux */

typedef struct _OSWR_LOCK_ *POSWR_LOCK;

PVRSRV_ERROR OSWRLockCreate(POSWR_LOCK *ppsLock);
void OSWRLockDestroy(POSWR_LOCK psLock);
void OSWRLockAcquireRead(POSWR_LOCK psLock);
void OSWRLockReleaseRead(POSWR_LOCK psLock);
void OSWRLockAcquireWrite(POSWR_LOCK psLock);
void OSWRLockReleaseWrite(POSWR_LOCK psLock);

#else
struct _OSWR_LOCK_ {
	IMG_UINT32 ui32Dummy;
};
#if defined(WINDOWS_WDF)
	typedef struct _OSWR_LOCK_ *POSWR_LOCK;
#endif

/*************************************************************************/ /*!
@Function       OSWRLockCreate
@Description    Create a writer/reader lock.
                This type of lock allows multiple concurrent readers but
                only a single writer, allowing for optimized performance.
@Output         ppsLock     A handle to the created WR lock.
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
static INLINE PVRSRV_ERROR OSWRLockCreate(POSWR_LOCK *ppsLock)
{
	PVR_UNREFERENCED_PARAMETER(ppsLock);
	return PVRSRV_OK;
}

/*************************************************************************/ /*!
@Function       OSWRLockDestroy
@Description    Destroys a writer/reader lock.
@Input          psLock     The handle of the WR lock to be destroyed.
@Return         None.
*/ /**************************************************************************/
static INLINE void OSWRLockDestroy(POSWR_LOCK psLock)
{
	PVR_UNREFERENCED_PARAMETER(psLock);
}

/*************************************************************************/ /*!
@Function       OSWRLockAcquireRead
@Description    Acquire a writer/reader read lock.
                If the write lock is already acquired, the caller will
                block until it is released.
@Input          psLock     The handle of the WR lock to be acquired for
                           reading.
@Return         None.
*/ /**************************************************************************/
static INLINE void OSWRLockAcquireRead(POSWR_LOCK psLock)
{
	PVR_UNREFERENCED_PARAMETER(psLock);
}

/*************************************************************************/ /*!
@Function       OSWRLockReleaseRead
@Description    Release a writer/reader read lock.
@Input          psLock     The handle of the WR lock whose read lock is to
                           be released.
@Return         None.
*/ /**************************************************************************/
static INLINE void OSWRLockReleaseRead(POSWR_LOCK psLock)
{
	PVR_UNREFERENCED_PARAMETER(psLock);
}

/*************************************************************************/ /*!
@Function       OSWRLockAcquireWrite
@Description    Acquire a writer/reader write lock.
                If the write lock or any read lock are already acquired,
                the caller will block until all are released.
@Input          psLock     The handle of the WR lock to be acquired for
                           writing.
@Return         None.
*/ /**************************************************************************/
static INLINE void OSWRLockAcquireWrite(POSWR_LOCK psLock)
{
	PVR_UNREFERENCED_PARAMETER(psLock);
}

/*************************************************************************/ /*!
@Function       OSWRLockReleaseWrite
@Description    Release a writer/reader write lock.
@Input          psLock     The handle of the WR lock whose write lock is to
                           be released.
@Return         None
*/ /**************************************************************************/
static INLINE void OSWRLockReleaseWrite(POSWR_LOCK psLock)
{
	PVR_UNREFERENCED_PARAMETER(psLock);
}
#endif

/*************************************************************************/ /*!
@Function       OSDivide64r64
@Description    Divide a 64-bit value by a 32-bit value. Return the 64-bit
                quotient.
                The remainder is also returned in 'pui32Remainder'.
@Input          ui64Divident        The number to be divided.
@Input          ui32Divisor         The 32-bit value 'ui64Divident' is to
                                    be divided by.
@Output         pui32Remainder      The remainder of the division.
@Return         The 64-bit quotient (result of the division).
*/ /**************************************************************************/
IMG_UINT64 OSDivide64r64(IMG_UINT64 ui64Divident, IMG_UINT32 ui32Divisor, IMG_UINT32 *pui32Remainder);

/*************************************************************************/ /*!
@Function       OSDivide64
@Description    Divide a 64-bit value by a 32-bit value. Return a 32-bit
                quotient.
                The remainder is also returned in 'pui32Remainder'.
                This function allows for a more optional implementation
                of a 64-bit division when the result is known to be
                representable in 32-bits.
@Input          ui64Divident        The number to be divided.
@Input          ui32Divisor         The 32-bit value 'ui64Divident' is to
                                    be divided by.
@Output         pui32Remainder      The remainder of the division.
@Return         The 32-bit quotient (result of the division).
*/ /**************************************************************************/
IMG_UINT32 OSDivide64(IMG_UINT64 ui64Divident, IMG_UINT32 ui32Divisor, IMG_UINT32 *pui32Remainder);

/*************************************************************************/ /*!
@Function       OSDumpStack
@Description    Dump the current task information and its stack trace.
@Return         None
*/ /**************************************************************************/
void OSDumpStack(void);

/*************************************************************************/ /*!
@Function       OSAcquireBridgeLock
@Description    Acquire the global bridge lock.
                This prevents another bridge call from being actioned while
                we are still servicing the current bridge call.
                NB. This function must not return until the lock is acquired
                (meaning the implementation should not timeout or return with
                an error, as the caller will assume they have the lock).
                This function has an OS-specific implementation rather than
                an abstracted implementation for efficiency reasons, as it
                is called frequently.
@Return         None
*/ /**************************************************************************/
void OSAcquireBridgeLock(void);
/*************************************************************************/ /*!
@Function       OSReleaseBridgeLock
@Description    Release the global bridge lock.
                This function has an OS-specific implementation rather than
                an abstracted implementation for efficiency reasons, as it
                is called frequently.
@Return         None
*/ /**************************************************************************/
void OSReleaseBridgeLock(void);

/*
 *  Functions for providing support for PID statistics.
 */
typedef void (OS_STATS_PRINTF_FUNC)(void *pvFilePtr, const IMG_CHAR *pszFormat, ...);
 
typedef void (OS_STATS_PRINT_FUNC)(void *pvFilePtr,
								   void *pvStatPtr,
								   OS_STATS_PRINTF_FUNC* pfnOSGetStatsPrintf);

typedef IMG_UINT32 (OS_INC_STATS_MEM_REFCOUNT_FUNC)(void *pvStatPtr);
typedef IMG_UINT32 (OS_DEC_STATS_MEM_REFCOUNT_FUNC)(void *pvStatPtr);

/*************************************************************************/ /*!
@Function       OSCreateStatisticEntry
@Description    Create a statistic entry in the specified folder.
                Where operating systems do not support a debugfs,
                file system this function may be implemented as a stub.
@Input          pszName        String containing the name for the entry.
@Input          pvFolder       Reference from OSCreateStatisticFolder() of the
                               folder to create the entry in, or NULL for the
                               root.
@Input          pfnStatsPrint  Pointer to function that can be used to print the
                               values of all the statistics.
@Input          pfnIncMemRefCt Pointer to function that can be used to take a
                               reference on the memory backing the statistic
                               entry.
@Input          pfnDecMemRefCt Pointer to function that can be used to drop a
                               reference on the memory backing the statistic
                               entry.
@Input          pvData         OS specific reference that can be used by
                               pfnGetElement.
@Return	        Pointer void reference to the entry created, which can be
                passed to OSRemoveStatisticEntry() to remove the entry.
*/ /**************************************************************************/
void *OSCreateStatisticEntry(IMG_CHAR* pszName, void *pvFolder,
							 OS_STATS_PRINT_FUNC* pfnStatsPrint,
							 OS_INC_STATS_MEM_REFCOUNT_FUNC* pfnIncMemRefCt,
							 OS_DEC_STATS_MEM_REFCOUNT_FUNC* pfnDecMemRefCt,
							 void *pvData);

/*************************************************************************/ /*!
@Function       OSRemoveStatisticEntry
@Description    Removes a statistic entry.
                Where operating systems do not support a debugfs,
                file system this function may be implemented as a stub.
@Input          pvEntry  Pointer void reference to the entry created by
                         OSCreateStatisticEntry().
*/ /**************************************************************************/
void OSRemoveStatisticEntry(void *pvEntry);

#if defined(PVRSRV_ENABLE_MEMTRACK_STATS_FILE)
/*************************************************************************/ /*!
@Function       OSCreateRawStatisticEntry
@Description    Create a raw statistic entry in the specified folder.
                Where operating systems do not support a debugfs
                file system this function may be implemented as a stub.
@Input          pszFileName    String containing the name for the entry.
@Input          pvParentDir    Reference from OSCreateStatisticFolder() of the
                               folder to create the entry in, or NULL for the
                               root.
@Input          pfnStatsPrint  Pointer to function that can be used to print the
                               values of all the statistics.
@Return	        Pointer void reference to the entry created, which can be
                passed to OSRemoveRawStatisticEntry() to remove the entry.
*/ /**************************************************************************/
void *OSCreateRawStatisticEntry(const IMG_CHAR *pszFileName, void *pvParentDir,
                                OS_STATS_PRINT_FUNC *pfStatsPrint);

/*************************************************************************/ /*!
@Function       OSRemoveRawStatisticEntry
@Description    Removes a raw statistic entry.
                Where operating systems do not support a debugfs
                file system this function may be implemented as a stub.
@Input          pvEntry  Pointer void reference to the entry created by
                         OSCreateRawStatisticEntry().
*/ /**************************************************************************/
void OSRemoveRawStatisticEntry(void *pvEntry);
#endif

/*************************************************************************/ /*!
@Function       OSCreateStatisticFolder
@Description    Create a statistic folder to hold statistic entries.
                Where operating systems do not support a debugfs,
                file system this function may be implemented as a stub.
@Input          pszName   String containing the name for the folder.
@Input          pvFolder  Reference from OSCreateStatisticFolder() of the folder
                          to create the folder in, or NULL for the root.
@Return         Pointer void reference to the folder created, which can be
                passed to OSRemoveStatisticFolder() to remove the folder.
*/ /**************************************************************************/
void *OSCreateStatisticFolder(IMG_CHAR *pszName, void *pvFolder);

/*************************************************************************/ /*!
@Function       OSRemoveStatisticFolder
@Description    Removes a statistic folder.
                Where operating systems do not support a debugfs,
                file system this function may be implemented as a stub.
@Input          ppvFolder  Reference from OSCreateStatisticFolder() of the
                           folder that should be removed.
                           This needs to be double pointer because it has to
                           be NULLed right after memory is freed to avoid
                           possible races and use-after-free situations.
*/ /**************************************************************************/
void OSRemoveStatisticFolder(void **ppvFolder);

/*************************************************************************/ /*!
@Function       OSUserModeAccessToPerfCountersEn
@Description    Permit User-mode access to CPU performance counter
                registers.
                This function is called during device initialisation.
                Certain CPU architectures may need to explicitly permit
                User mode access to performance counters - if this is
                required, the necessary code should be implemented inside
                this function.
@Return         None.
*/ /**************************************************************************/
void OSUserModeAccessToPerfCountersEn(void);

/*************************************************************************/ /*!
@Function       OSDebugSignalPID
@Description    Sends a SIGTRAP signal to a specific PID in user mode for
                debugging purposes. The user mode process can register a handler
                against this signal.
                This is necessary to support the Rogue debugger. If the Rogue
                debugger is not used then this function may be implemented as
                a stub.
@Input          ui32PID    The PID for the signal.
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSDebugSignalPID(IMG_UINT32 ui32PID);

#if defined(CONFIG_L4)
#include <asm/api-l4env/api.h>
#include <asm/io.h>

#if defined(page_to_phys)
#undef page_to_phys
#define page_to_phys(x) l4x_virt_to_phys(x)
#else
#error "Unable to override page_to_phys() implementation"
#endif
#endif

#endif /* __OSFUNC_H__ */

/******************************************************************************
 End of file (osfunc.h)
******************************************************************************/

