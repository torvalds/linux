/*************************************************************************/ /*!
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
*/ /**************************************************************************/
#ifdef DEBUG_RELEASE_BUILD
#pragma optimize( "", off )
#define DEBUG		1
#endif

#ifndef __OSFUNC_H__
#define __OSFUNC_H__

#if defined (__cplusplus)
extern "C" {
#endif

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/hardirq.h>
#include <linux/string.h>
#if defined(__arm__)
#include <asm/memory.h>
#endif
#endif


/* setup conditional pageable / non-pageable select */
	/* Other OSs only need pageable */
	#define PVRSRV_PAGEABLE_SELECT		PVRSRV_OS_PAGEABLE_HEAP

/******************************************************************************
 * Static defines
 *****************************************************************************/
#define KERNEL_ID			0xffffffffL
#define POWER_MANAGER_ID	0xfffffffeL
#define ISR_ID				0xfffffffdL
#define TIMER_ID			0xfffffffcL


#define HOST_PAGESIZE			OSGetPageSize
#define HOST_PAGEMASK			(HOST_PAGESIZE()-1)
#define HOST_PAGEALIGN(addr)	(((addr) + HOST_PAGEMASK) & ~HOST_PAGEMASK)

/******************************************************************************
 *	Host memory heaps
 *****************************************************************************/
#define PVRSRV_OS_HEAP_MASK			0xf /* host heap flags mask */
#define PVRSRV_OS_PAGEABLE_HEAP		0x1 /* allocation pageable */
#define PVRSRV_OS_NON_PAGEABLE_HEAP	0x2 /* allocation non pageable */


IMG_UINT32 OSClockus(IMG_VOID);
IMG_UINT32 OSGetPageSize(IMG_VOID);
PVRSRV_ERROR OSInstallDeviceLISR(IMG_VOID *pvSysData,
								 IMG_UINT32 ui32Irq,
								 IMG_CHAR *pszISRName,
								 IMG_VOID *pvDeviceNode);
PVRSRV_ERROR OSUninstallDeviceLISR(IMG_VOID *pvSysData);
PVRSRV_ERROR OSInstallSystemLISR(IMG_VOID *pvSysData, IMG_UINT32 ui32Irq);
PVRSRV_ERROR OSUninstallSystemLISR(IMG_VOID *pvSysData);
PVRSRV_ERROR OSInstallMISR(IMG_VOID *pvSysData);
PVRSRV_ERROR OSUninstallMISR(IMG_VOID *pvSysData);
IMG_CPU_PHYADDR OSMapLinToCPUPhys(IMG_HANDLE, IMG_VOID* pvLinAddr);
IMG_VOID OSMemCopy(IMG_VOID *pvDst, IMG_VOID *pvSrc, IMG_SIZE_T uiSize);
IMG_VOID *OSMapPhysToLin(IMG_CPU_PHYADDR BasePAddr, IMG_SIZE_T uBytes, IMG_UINT32 ui32Flags, IMG_HANDLE *phOSMemHandle);
IMG_BOOL OSUnMapPhysToLin(IMG_VOID *pvLinAddr, IMG_SIZE_T uBytes, IMG_UINT32 ui32Flags, IMG_HANDLE hOSMemHandle);

PVRSRV_ERROR OSReservePhys(IMG_CPU_PHYADDR BasePAddr, IMG_SIZE_T uBytes, IMG_UINT32 ui32Flags, IMG_HANDLE hBMHandle, IMG_VOID **ppvCpuVAddr, IMG_HANDLE *phOSMemHandle);
PVRSRV_ERROR OSUnReservePhys(IMG_VOID *pvCpuVAddr, IMG_SIZE_T uBytes, IMG_UINT32 ui32Flags, IMG_HANDLE hOSMemHandle);

/* Some terminology:
 *
 *  FLUSH		Flush w/ invalidate
 *  CLEAN		Flush w/o invalidate
 *  INVALIDATE	Invalidate w/o flush
 */

#if defined(__linux__) && defined(__KERNEL__)

IMG_VOID OSFlushCPUCacheKM(IMG_VOID);

IMG_VOID OSCleanCPUCacheKM(IMG_VOID);

IMG_BOOL OSFlushCPUCacheRangeKM(IMG_HANDLE hOSMemHandle,
								IMG_UINT32 ui32ByteOffset,
								IMG_VOID *pvRangeAddrStart,
								IMG_UINT32 ui32Length);
IMG_BOOL OSCleanCPUCacheRangeKM(IMG_HANDLE hOSMemHandle,
								IMG_UINT32 ui32ByteOffset,
								IMG_VOID *pvRangeAddrStart,
								IMG_UINT32 ui32Length);
IMG_BOOL OSInvalidateCPUCacheRangeKM(IMG_HANDLE hOSMemHandle,
									 IMG_UINT32 ui32ByteOffset,
									 IMG_VOID *pvRangeAddrStart,
									 IMG_UINT32 ui32Length);

#else /* defined(__linux__) && defined(__KERNEL__) */

#ifdef INLINE_IS_PRAGMA
#pragma inline(OSFlushCPUCacheKM)
#endif
static INLINE IMG_VOID OSFlushCPUCacheKM(IMG_VOID) {}

#ifdef INLINE_IS_PRAGMA
#pragma inline(OSCleanCPUCacheKM)
#endif
static INLINE IMG_VOID OSCleanCPUCacheKM(IMG_VOID) {}

#ifdef INLINE_IS_PRAGMA
#pragma inline(OSFlushCPUCacheRangeKM)
#endif
static INLINE IMG_BOOL OSFlushCPUCacheRangeKM(IMG_HANDLE hOSMemHandle,
											  IMG_UINT32 ui32ByteOffset,
											  IMG_VOID *pvRangeAddrStart,
											  IMG_UINT32 ui32Length)
{
	PVR_UNREFERENCED_PARAMETER(hOSMemHandle);
	PVR_UNREFERENCED_PARAMETER(ui32ByteOffset);
	PVR_UNREFERENCED_PARAMETER(pvRangeAddrStart);
	PVR_UNREFERENCED_PARAMETER(ui32Length);
	return IMG_FALSE;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(OSCleanCPUCacheRangeKM)
#endif
static INLINE IMG_BOOL OSCleanCPUCacheRangeKM(IMG_HANDLE hOSMemHandle,
											  IMG_UINT32 ui32ByteOffset,
											  IMG_VOID *pvRangeAddrStart,
											  IMG_UINT32 ui32Length)
{
	PVR_UNREFERENCED_PARAMETER(hOSMemHandle);
	PVR_UNREFERENCED_PARAMETER(ui32ByteOffset);
	PVR_UNREFERENCED_PARAMETER(pvRangeAddrStart);
	PVR_UNREFERENCED_PARAMETER(ui32Length);
	return IMG_FALSE;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(OSInvalidateCPUCacheRangeKM)
#endif
static INLINE IMG_BOOL OSInvalidateCPUCacheRangeKM(IMG_HANDLE hOSMemHandle,
												   IMG_UINT32 ui32ByteOffset,
												   IMG_VOID *pvRangeAddrStart,
												   IMG_UINT32 ui32Length)
{
	PVR_UNREFERENCED_PARAMETER(hOSMemHandle);
	PVR_UNREFERENCED_PARAMETER(ui32ByteOffset);
	PVR_UNREFERENCED_PARAMETER(pvRangeAddrStart);
	PVR_UNREFERENCED_PARAMETER(ui32Length);
	return IMG_FALSE;
}

#endif /* defined(__linux__) && defined(__KERNEL__) */

#if defined(__linux__) || defined(__QNXNTO__)
PVRSRV_ERROR OSRegisterDiscontigMem(IMG_SYS_PHYADDR *pBasePAddr,
									IMG_VOID *pvCpuVAddr, 
									IMG_SIZE_T uBytes,
									IMG_UINT32 ui32Flags, 
									IMG_HANDLE *phOSMemHandle);
PVRSRV_ERROR OSUnRegisterDiscontigMem(IMG_VOID *pvCpuVAddr,
									IMG_SIZE_T uBytes,
									IMG_UINT32 ui32Flags,
									IMG_HANDLE hOSMemHandle);
#else	/* defined(__linux__) */
#ifdef INLINE_IS_PRAGMA
#pragma inline(OSRegisterDiscontigMem)
#endif
static INLINE PVRSRV_ERROR OSRegisterDiscontigMem(IMG_SYS_PHYADDR *pBasePAddr,
													IMG_VOID *pvCpuVAddr,
													IMG_SIZE_T uBytes,
													IMG_UINT32 ui32Flags,
													IMG_HANDLE *phOSMemHandle)
{
	PVR_UNREFERENCED_PARAMETER(pBasePAddr);
	PVR_UNREFERENCED_PARAMETER(pvCpuVAddr);
	PVR_UNREFERENCED_PARAMETER(ui32Bytes);
	PVR_UNREFERENCED_PARAMETER(ui32Flags);
	PVR_UNREFERENCED_PARAMETER(phOSMemHandle);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(OSUnRegisterDiscontigMem)
#endif
static INLINE PVRSRV_ERROR OSUnRegisterDiscontigMem(IMG_VOID *pvCpuVAddr,
													IMG_SIZE_T uBytes,
													IMG_UINT32 ui32Flags,
													IMG_HANDLE hOSMemHandle)
{
	PVR_UNREFERENCED_PARAMETER(pvCpuVAddr);
	PVR_UNREFERENCED_PARAMETER(ui32Bytes);
	PVR_UNREFERENCED_PARAMETER(ui32Flags);
	PVR_UNREFERENCED_PARAMETER(hOSMemHandle);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}
#endif	/* defined(__linux__) */


#if  defined(__linux__) || defined(__QNXNTO__)
#ifdef INLINE_IS_PRAGMA
#pragma inline(OSReserveDiscontigPhys)
#endif
static INLINE PVRSRV_ERROR OSReserveDiscontigPhys(IMG_SYS_PHYADDR *pBasePAddr, IMG_SIZE_T uBytes, IMG_UINT32 ui32Flags, IMG_VOID **ppvCpuVAddr, IMG_HANDLE *phOSMemHandle)
{
#if defined(__linux__) || defined(__QNXNTO__) 
	*ppvCpuVAddr = IMG_NULL;
	return OSRegisterDiscontigMem(pBasePAddr, *ppvCpuVAddr, uBytes, ui32Flags, phOSMemHandle);	
#else
	extern IMG_CPU_PHYADDR SysSysPAddrToCpuPAddr(IMG_SYS_PHYADDR SysPAddr);

	/*
	 * On uITRON we know:
	 * 1. We will only be called with a non-contig physical if we
	 *    already have a contiguous CPU linear
	 * 2. There is a one->one mapping of CpuPAddr -> CpuVAddr
	 * 3. Looking up the first CpuPAddr will find the first CpuVAddr
	 * 4. We don't need to unmap
	 */

	return OSReservePhys(SysSysPAddrToCpuPAddr(pBasePAddr[0]), uBytes, ui32Flags, IMG_NULL, ppvCpuVAddr, phOSMemHandle);
#endif	
}

static INLINE PVRSRV_ERROR OSUnReserveDiscontigPhys(IMG_VOID *pvCpuVAddr, IMG_SIZE_T uBytes, IMG_UINT32 ui32Flags, IMG_HANDLE hOSMemHandle)
{
#if defined(__linux__) || defined(__QNXNTO__) 
	OSUnRegisterDiscontigMem(pvCpuVAddr, uBytes, ui32Flags, hOSMemHandle);
#endif
	/* We don't need to unmap */
	return PVRSRV_OK;
}
#else	/* defined(__linux__) */


#ifdef INLINE_IS_PRAGMA
#pragma inline(OSReserveDiscontigPhys)
#endif
static INLINE PVRSRV_ERROR OSReserveDiscontigPhys(IMG_SYS_PHYADDR *pBasePAddr, IMG_SIZE_T uBytes, IMG_UINT32 ui32Flags, IMG_VOID **ppvCpuVAddr, IMG_HANDLE *phOSMemHandle)
{
	PVR_UNREFERENCED_PARAMETER(pBasePAddr);
	PVR_UNREFERENCED_PARAMETER(uBytes);
	PVR_UNREFERENCED_PARAMETER(ui32Flags);
	PVR_UNREFERENCED_PARAMETER(ppvCpuVAddr);
	PVR_UNREFERENCED_PARAMETER(phOSMemHandle);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(OSUnReserveDiscontigPhys)
#endif
static INLINE PVRSRV_ERROR OSUnReserveDiscontigPhys(IMG_VOID *pvCpuVAddr, IMG_SIZE_T uBytes, IMG_UINT32 ui32Flags, IMG_HANDLE hOSMemHandle)
{
	PVR_UNREFERENCED_PARAMETER(pvCpuVAddr);
	PVR_UNREFERENCED_PARAMETER(uBytes);
	PVR_UNREFERENCED_PARAMETER(ui32Flags);
	PVR_UNREFERENCED_PARAMETER(hOSMemHandle);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}
#endif	/* defined(__linux__) */

PVRSRV_ERROR OSRegisterMem(IMG_CPU_PHYADDR BasePAddr,
							IMG_VOID *pvCpuVAddr,
							IMG_SIZE_T uBytes,
							IMG_UINT32 ui32Flags,
							IMG_HANDLE *phOSMemHandle);
PVRSRV_ERROR OSUnRegisterMem(IMG_VOID *pvCpuVAddr,
							IMG_SIZE_T uBytes,
							IMG_UINT32 ui32Flags,
							IMG_HANDLE hOSMemHandle);



#if defined(__linux__) || defined(__QNXNTO__)
PVRSRV_ERROR OSGetSubMemHandle(IMG_HANDLE hOSMemHandle,
							   IMG_UINTPTR_T uByteOffset,
							   IMG_SIZE_T uBytes,
							   IMG_UINT32 ui32Flags,
							   IMG_HANDLE *phOSMemHandleRet);
PVRSRV_ERROR OSReleaseSubMemHandle(IMG_HANDLE hOSMemHandle, IMG_UINT32 ui32Flags);
#else
#ifdef INLINE_IS_PRAGMA
#pragma inline(OSGetSubMemHandle)
#endif
static INLINE PVRSRV_ERROR OSGetSubMemHandle(IMG_HANDLE hOSMemHandle,
											 IMG_UINTPTR_T uByteOffset,
											 IMG_SIZE_T uBytes,
											 IMG_UINT32 ui32Flags,
											 IMG_HANDLE *phOSMemHandleRet)
{
	PVR_UNREFERENCED_PARAMETER(uByteOffset);
	PVR_UNREFERENCED_PARAMETER(uBytes);
	PVR_UNREFERENCED_PARAMETER(ui32Flags);

	*phOSMemHandleRet = hOSMemHandle;
	return PVRSRV_OK;
}

static INLINE PVRSRV_ERROR OSReleaseSubMemHandle(IMG_HANDLE hOSMemHandle, IMG_UINT32 ui32Flags)
{
	PVR_UNREFERENCED_PARAMETER(hOSMemHandle);
	PVR_UNREFERENCED_PARAMETER(ui32Flags);
	return PVRSRV_OK;
}
#endif

IMG_UINT32 OSGetCurrentProcessIDKM(IMG_VOID);
IMG_UINTPTR_T OSGetCurrentThreadID( IMG_VOID );
IMG_VOID OSMemSet(IMG_VOID *pvDest, IMG_UINT8 ui8Value, IMG_SIZE_T uSize);

PVRSRV_ERROR OSAllocPages_Impl(IMG_UINT32 ui32Flags, IMG_SIZE_T uSize, IMG_UINT32 ui32PageSize,
							   IMG_PVOID pvPrivData, IMG_UINT32 ui32PrivDataLength, IMG_HANDLE hBMHandle, IMG_PVOID *ppvLinAddr, IMG_HANDLE *phPageAlloc);
PVRSRV_ERROR OSFreePages(IMG_UINT32 ui32Flags, IMG_SIZE_T uSize, IMG_PVOID pvLinAddr, IMG_HANDLE hPageAlloc);


/*---------------------
The set of macros below follows this pattern:

f(x) = if F -> f2(g(x))
       else -> g(x)

g(x) = if G -> g2(h(x))
       else -> h(x)

h(x) = ...

-----------------------*/

/*If level 3 wrapper is enabled, we add a PVR_TRACE and call the next level, else just call the next level*/
#ifdef PVRSRV_LOG_MEMORY_ALLOCS
	#define OSAllocMem(flags, size, linAddr, blockAlloc, logStr) \
		(PVR_TRACE(("OSAllocMem(" #flags ", " #size ", " #linAddr ", " #blockAlloc "): " logStr " (size = 0x%lx)", size)), \
			OSAllocMem_Debug_Wrapper(flags, size, linAddr, blockAlloc, __FILE__, __LINE__))

	#define OSAllocPages(flags, size, pageSize, privdata, privdatalength, bmhandle, linAddr, pageAlloc) \
		(PVR_TRACE(("OSAllocPages(" #flags ", " #size ", " #pageSize ", " #linAddr ", " #pageAlloc "): (size = 0x%lx)", size)), \
			OSAllocPages_Impl(flags, size, pageSize, linAddr, privdata, privdatalength, bmhandle, pageAlloc))
		
	#define OSFreeMem(flags, size, linAddr, blockAlloc) \
		(PVR_TRACE(("OSFreeMem(" #flags ", " #size ", " #linAddr ", " #blockAlloc "): (pointer = 0x%X)", linAddr)), \
			OSFreeMem_Debug_Wrapper(flags, size, linAddr, blockAlloc, __FILE__, __LINE__))
#else
	#define OSAllocMem(flags, size, linAddr, blockAlloc, logString) \
		OSAllocMem_Debug_Wrapper(flags, size, linAddr, blockAlloc, __FILE__, __LINE__)
	
	#define OSAllocPages OSAllocPages_Impl
	
	#define OSFreeMem(flags, size, linAddr, blockAlloc) \
			OSFreeMem_Debug_Wrapper(flags, size, linAddr, blockAlloc, __FILE__, __LINE__)
#endif
 
/*If level 2 wrapper is enabled declare the function,
else alias to level 1 wrapper, else the wrapper function will be used*/
#ifdef PVRSRV_DEBUG_OS_MEMORY

	PVRSRV_ERROR OSAllocMem_Debug_Wrapper(IMG_UINT32 ui32Flags,
										IMG_UINT32 ui32Size,
										IMG_PVOID *ppvCpuVAddr,
										IMG_HANDLE *phBlockAlloc,
										IMG_CHAR *pszFilename,
										IMG_UINT32 ui32Line);
	
	PVRSRV_ERROR OSFreeMem_Debug_Wrapper(IMG_UINT32 ui32Flags,
									 IMG_UINT32 ui32Size,
									 IMG_PVOID pvCpuVAddr,
									 IMG_HANDLE hBlockAlloc,
									 IMG_CHAR *pszFilename,
									 IMG_UINT32 ui32Line);


	typedef struct
	{	
		IMG_UINT8 sGuardRegionBefore[8];
		IMG_CHAR sFileName[128];
		IMG_UINT32 uLineNo;
		IMG_SIZE_T uSize;
		IMG_SIZE_T uSizeParityCheck;
		enum valid_tag
		{	isFree = 0x277260FF,
			isAllocated = 0x260511AA
		} eValid;
	} OSMEM_DEBUG_INFO;
	
	#define TEST_BUFFER_PADDING_STATUS (sizeof(OSMEM_DEBUG_INFO))
	#define TEST_BUFFER_PADDING_AFTER  (8)
	#define TEST_BUFFER_PADDING (TEST_BUFFER_PADDING_STATUS + TEST_BUFFER_PADDING_AFTER)
#else
	#define OSAllocMem_Debug_Wrapper OSAllocMem_Debug_Linux_Memory_Allocations
	#define OSFreeMem_Debug_Wrapper OSFreeMem_Debug_Linux_Memory_Allocations
#endif
 
/*If level 1 wrapper is enabled declare the functions with extra parameters
else alias to level 0 and declare the functions without the extra debugging parameters*/
#if (defined(__linux__) || defined(__QNXNTO__)) && defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	PVRSRV_ERROR OSAllocMem_Impl(IMG_UINT32 ui32Flags, IMG_SIZE_T uSize, IMG_PVOID *ppvLinAddr, IMG_HANDLE *phBlockAlloc, IMG_CHAR *pszFilename, IMG_UINT32 ui32Line);
	PVRSRV_ERROR OSFreeMem_Impl(IMG_UINT32 ui32Flags, IMG_SIZE_T uSize, IMG_PVOID pvLinAddr, IMG_HANDLE hBlockAlloc, IMG_CHAR *pszFilename, IMG_UINT32 ui32Line);
	
	#define OSAllocMem_Debug_Linux_Memory_Allocations OSAllocMem_Impl
	#define OSFreeMem_Debug_Linux_Memory_Allocations OSFreeMem_Impl
#else
	PVRSRV_ERROR OSAllocMem_Impl(IMG_UINT32 ui32Flags, IMG_SIZE_T uSize, IMG_PVOID *ppvLinAddr, IMG_HANDLE *phBlockAlloc);
	PVRSRV_ERROR OSFreeMem_Impl(IMG_UINT32 ui32Flags, IMG_SIZE_T uSize, IMG_PVOID pvLinAddr, IMG_HANDLE hBlockAlloc);
	
	#define OSAllocMem_Debug_Linux_Memory_Allocations(flags, size, addr, blockAlloc, file, line) \
		OSAllocMem_Impl(flags, size, addr, blockAlloc)
	#define OSFreeMem_Debug_Linux_Memory_Allocations(flags, size, addr, blockAlloc, file, line) \
		OSFreeMem_Impl(flags, size, addr, blockAlloc)
#endif


#if defined(__linux__) || defined(__QNXNTO__)
IMG_CPU_PHYADDR OSMemHandleToCpuPAddr(IMG_VOID *hOSMemHandle, IMG_UINTPTR_T uiByteOffset);
#else
#ifdef INLINE_IS_PRAGMA
#pragma inline(OSMemHandleToCpuPAddr)
#endif
static INLINE IMG_CPU_PHYADDR OSMemHandleToCpuPAddr(IMG_HANDLE hOSMemHandle, IMG_UINTPTR_T uiByteOffset)
{
	IMG_CPU_PHYADDR sCpuPAddr;
	PVR_UNREFERENCED_PARAMETER(hOSMemHandle);
	PVR_UNREFERENCED_PARAMETER(uiByteOffset);
	sCpuPAddr.uiAddr = 0;
	return sCpuPAddr;
}
#endif

#if defined(__linux__)
IMG_BOOL OSMemHandleIsPhysContig(IMG_VOID *hOSMemHandle);
#else
#ifdef INLINE_IS_PRAGMA
#pragma inline(OSMemHandleIsPhysContig)
#endif
static INLINE IMG_BOOL OSMemHandleIsPhysContig(IMG_HANDLE hOSMemHandle)
{
	PVR_UNREFERENCED_PARAMETER(hOSMemHandle);
	return IMG_FALSE;
}
#endif

PVRSRV_ERROR OSInitEnvData(IMG_PVOID *ppvEnvSpecificData);
PVRSRV_ERROR OSDeInitEnvData(IMG_PVOID pvEnvSpecificData);
IMG_CHAR* OSStringCopy(IMG_CHAR *pszDest, const IMG_CHAR *pszSrc);
IMG_INT32 OSSNPrintf(IMG_CHAR *pStr, IMG_SIZE_T uSize, const IMG_CHAR *pszFormat, ...) IMG_FORMAT_PRINTF(3, 4);
#define OSStringLength(pszString) strlen(pszString)

PVRSRV_ERROR OSEventObjectCreateKM(const IMG_CHAR *pszName,
								 PVRSRV_EVENTOBJECT *psEventObject);
PVRSRV_ERROR OSEventObjectDestroyKM(PVRSRV_EVENTOBJECT *psEventObject);
PVRSRV_ERROR OSEventObjectSignalKM(IMG_HANDLE hOSEventKM);
PVRSRV_ERROR OSEventObjectWaitKM(IMG_HANDLE hOSEventKM);
PVRSRV_ERROR OSEventObjectOpenKM(PVRSRV_EVENTOBJECT *psEventObject,
											IMG_HANDLE *phOSEvent);
PVRSRV_ERROR OSEventObjectCloseKM(PVRSRV_EVENTOBJECT *psEventObject,
											IMG_HANDLE hOSEventKM);


PVRSRV_ERROR OSBaseAllocContigMemory(IMG_SIZE_T uSize, IMG_CPU_VIRTADDR *pLinAddr, IMG_CPU_PHYADDR *pPhysAddr);
PVRSRV_ERROR OSBaseFreeContigMemory(IMG_SIZE_T uSize, IMG_CPU_VIRTADDR LinAddr, IMG_CPU_PHYADDR PhysAddr);

IMG_PVOID MapUserFromKernel(IMG_PVOID pvLinAddrKM,IMG_SIZE_T uSize,IMG_HANDLE *phMemBlock);
IMG_PVOID OSMapHWRegsIntoUserSpace(IMG_HANDLE hDevCookie, IMG_SYS_PHYADDR sRegAddr, IMG_UINT32 ulSize, IMG_PVOID *ppvProcess);
IMG_VOID  OSUnmapHWRegsFromUserSpace(IMG_HANDLE hDevCookie, IMG_PVOID pvUserAddr, IMG_PVOID pvProcess);

IMG_VOID  UnmapUserFromKernel(IMG_PVOID pvLinAddrUM, IMG_SIZE_T uSize, IMG_HANDLE hMemBlock);

PVRSRV_ERROR OSMapPhysToUserSpace(IMG_HANDLE hDevCookie,
								  IMG_SYS_PHYADDR sCPUPhysAddr,
								  IMG_SIZE_T uiSizeInBytes,
								  IMG_UINT32 ui32CacheFlags,
								  IMG_PVOID *ppvUserAddr,
								  IMG_SIZE_T *puiActualSize,
								  IMG_HANDLE hMappingHandle);

PVRSRV_ERROR OSUnmapPhysToUserSpace(IMG_HANDLE hDevCookie,
									IMG_PVOID pvUserAddr,
									IMG_PVOID pvProcess);

PVRSRV_ERROR OSLockResource(PVRSRV_RESOURCE *psResource, IMG_UINT32 ui32ID);
PVRSRV_ERROR OSUnlockResource(PVRSRV_RESOURCE *psResource, IMG_UINT32 ui32ID);
IMG_BOOL OSIsResourceLocked(PVRSRV_RESOURCE *psResource, IMG_UINT32 ui32ID);
PVRSRV_ERROR OSCreateResource(PVRSRV_RESOURCE *psResource);
PVRSRV_ERROR OSDestroyResource(PVRSRV_RESOURCE *psResource);
IMG_VOID OSBreakResourceLock(PVRSRV_RESOURCE *psResource, IMG_UINT32 ui32ID);

#if defined(SYS_CUSTOM_POWERLOCK_WRAP)
#define OSPowerLockWrap SysPowerLockWrap
#define OSPowerLockUnwrap SysPowerLockUnwrap
#else
/******************************************************************************
 @Function	OSPowerLockWrap

 @Description	OS-specific wrapper around the power lock

 @Input bTryLock - don't block on lock contention

 @Return	PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR OSPowerLockWrap(IMG_BOOL bTryLock);

/******************************************************************************
 @Function	OSPowerLockUnwrap

 @Description	OS-specific wrapper around the power unlock

 @Return	IMG_VOID
******************************************************************************/
IMG_VOID OSPowerLockUnwrap(IMG_VOID);
#endif /* SYS_CUSTOM_POWERLOCK_WRAP */

/*!
******************************************************************************

 @Function OSWaitus
 
 @Description 
    This function implements a busy wait of the specified microseconds
    This function does NOT release thread quanta
 
 @Input ui32Timeus - (us)

 @Return IMG_VOID

******************************************************************************/ 
IMG_VOID OSWaitus(IMG_UINT32 ui32Timeus);

/*!
******************************************************************************

 @Function OSSleepms
 
 @Description 
    This function implements a sleep of the specified milliseconds
    This function may allow pre-emption if implemented
 
 @Input ui32Timems - (ms)

 @Return IMG_VOID

******************************************************************************/ 
IMG_VOID OSSleepms(IMG_UINT32 ui32Timems);

IMG_HANDLE OSFuncHighResTimerCreate(IMG_VOID);
IMG_UINT32 OSFuncHighResTimerGetus(IMG_HANDLE hTimer);
IMG_VOID OSFuncHighResTimerDestroy(IMG_HANDLE hTimer);
IMG_VOID OSReleaseThreadQuanta(IMG_VOID);
IMG_UINT32 OSPCIReadDword(IMG_UINT32 ui32Bus, IMG_UINT32 ui32Dev, IMG_UINT32 ui32Func, IMG_UINT32 ui32Reg);
IMG_VOID OSPCIWriteDword(IMG_UINT32 ui32Bus, IMG_UINT32 ui32Dev, IMG_UINT32 ui32Func, IMG_UINT32 ui32Reg, IMG_UINT32 ui32Value);

IMG_IMPORT
IMG_UINT32 ReadHWReg(IMG_PVOID pvLinRegBaseAddr, IMG_UINT32 ui32Offset);

IMG_IMPORT
IMG_VOID WriteHWReg(IMG_PVOID pvLinRegBaseAddr, IMG_UINT32 ui32Offset, IMG_UINT32 ui32Value);

IMG_IMPORT IMG_VOID WriteHWRegs(IMG_PVOID pvLinRegBaseAddr, IMG_UINT32 ui32Count, PVRSRV_HWREG *psHWRegs);

#ifndef OSReadHWReg
IMG_UINT32 OSReadHWReg(IMG_PVOID pvLinRegBaseAddr, IMG_UINT32 ui32Offset);
#endif
#ifndef OSWriteHWReg
IMG_VOID OSWriteHWReg(IMG_PVOID pvLinRegBaseAddr, IMG_UINT32 ui32Offset, IMG_UINT32 ui32Value);
#endif

typedef IMG_VOID (*PFN_TIMER_FUNC)(IMG_VOID*);
IMG_HANDLE OSAddTimer(PFN_TIMER_FUNC pfnTimerFunc, IMG_VOID *pvData, IMG_UINT32 ui32MsTimeout);
PVRSRV_ERROR OSRemoveTimer (IMG_HANDLE hTimer);
PVRSRV_ERROR OSEnableTimer (IMG_HANDLE hTimer);
PVRSRV_ERROR OSDisableTimer (IMG_HANDLE hTimer);

PVRSRV_ERROR OSGetSysMemSize(IMG_SIZE_T *puBytes);

typedef enum _HOST_PCI_INIT_FLAGS_
{
	HOST_PCI_INIT_FLAG_BUS_MASTER	= 0x00000001,
	HOST_PCI_INIT_FLAG_MSI		= 0x00000002,
	HOST_PCI_INIT_FLAG_FORCE_I32 	= 0x7fffffff
} HOST_PCI_INIT_FLAGS;

struct _PVRSRV_PCI_DEV_OPAQUE_STRUCT_;
typedef struct _PVRSRV_PCI_DEV_OPAQUE_STRUCT_ *PVRSRV_PCI_DEV_HANDLE;

PVRSRV_PCI_DEV_HANDLE OSPCIAcquireDev(IMG_UINT16 ui16VendorID, IMG_UINT16 ui16DeviceID, HOST_PCI_INIT_FLAGS eFlags);
PVRSRV_PCI_DEV_HANDLE OSPCISetDev(IMG_VOID *pvPCICookie, HOST_PCI_INIT_FLAGS eFlags);
PVRSRV_ERROR OSPCIReleaseDev(PVRSRV_PCI_DEV_HANDLE hPVRPCI);
PVRSRV_ERROR OSPCIIRQ(PVRSRV_PCI_DEV_HANDLE hPVRPCI, IMG_UINT32 *pui32IRQ);
IMG_UINT32 OSPCIAddrRangeLen(PVRSRV_PCI_DEV_HANDLE hPVRPCI, IMG_UINT32 ui32Index);
IMG_UINT32 OSPCIAddrRangeStart(PVRSRV_PCI_DEV_HANDLE hPVRPCI, IMG_UINT32 ui32Index);
IMG_UINT32 OSPCIAddrRangeEnd(PVRSRV_PCI_DEV_HANDLE hPVRPCI, IMG_UINT32 ui32Index);
PVRSRV_ERROR OSPCIRequestAddrRange(PVRSRV_PCI_DEV_HANDLE hPVRPCI, IMG_UINT32 ui32Index);
PVRSRV_ERROR OSPCIReleaseAddrRange(PVRSRV_PCI_DEV_HANDLE hPVRPCI, IMG_UINT32 ui32Index);
PVRSRV_ERROR OSPCISuspendDev(PVRSRV_PCI_DEV_HANDLE hPVRPCI);
PVRSRV_ERROR OSPCIResumeDev(PVRSRV_PCI_DEV_HANDLE hPVRPCI);

PVRSRV_ERROR OSScheduleMISR(IMG_VOID *pvSysData);

/******************************************************************************

 @Function		OSPanic

 @Description	Take action in response to an unrecoverable driver error

 @Input    IMG_VOID

 @Return   IMG_VOID

******************************************************************************/
IMG_VOID OSPanic(IMG_VOID);

IMG_BOOL OSProcHasPrivSrvInit(IMG_VOID);

typedef enum _img_verify_test
{
	PVR_VERIFY_WRITE = 0,
	PVR_VERIFY_READ
} IMG_VERIFY_TEST;

IMG_BOOL OSAccessOK(IMG_VERIFY_TEST eVerification, IMG_VOID *pvUserPtr, IMG_SIZE_T uBytes);

PVRSRV_ERROR OSCopyToUser(IMG_PVOID pvProcess, IMG_VOID *pvDest, IMG_VOID *pvSrc, IMG_SIZE_T uBytes);
PVRSRV_ERROR OSCopyFromUser(IMG_PVOID pvProcess, IMG_VOID *pvDest, IMG_VOID *pvSrc, IMG_SIZE_T uBytes);

#if defined(__linux__) || defined(__QNXNTO__)
PVRSRV_ERROR OSAcquirePhysPageAddr(IMG_VOID* pvCPUVAddr, 
									IMG_SIZE_T uBytes, 
									IMG_SYS_PHYADDR *psSysPAddr,
									IMG_HANDLE *phOSWrapMem);
PVRSRV_ERROR OSReleasePhysPageAddr(IMG_HANDLE hOSWrapMem);
#else
#ifdef INLINE_IS_PRAGMA
#pragma inline(OSAcquirePhysPageAddr)
#endif
static INLINE PVRSRV_ERROR OSAcquirePhysPageAddr(IMG_VOID* pvCPUVAddr, 
												IMG_SIZE_T uBytes, 
												IMG_SYS_PHYADDR *psSysPAddr,
												IMG_HANDLE *phOSWrapMem)
{
	PVR_UNREFERENCED_PARAMETER(pvCPUVAddr);
	PVR_UNREFERENCED_PARAMETER(uBytes);
	PVR_UNREFERENCED_PARAMETER(psSysPAddr);
	PVR_UNREFERENCED_PARAMETER(phOSWrapMem);
	return PVRSRV_OK;	
}
#ifdef INLINE_IS_PRAGMA
#pragma inline(OSReleasePhysPageAddr)
#endif
static INLINE PVRSRV_ERROR OSReleasePhysPageAddr(IMG_HANDLE hOSWrapMem)
{
	PVR_UNREFERENCED_PARAMETER(hOSWrapMem);
	return PVRSRV_OK;	
}
#endif
									
#if defined(__linux__) && defined(__KERNEL__)

#define	OS_SUPPORTS_IN_LISR

static inline IMG_BOOL OSInLISR(IMG_VOID unref__ *pvSysData)
{
	PVR_UNREFERENCED_PARAMETER(pvSysData);
	return (in_irq()) ? IMG_TRUE : IMG_FALSE;
}

static inline IMG_VOID OSWriteMemoryBarrier(IMG_VOID)
{
	wmb();
}

static inline IMG_VOID OSMemoryBarrier(IMG_VOID)
{
	mb();
}

#else /* defined(__linux__) && defined(__KERNEL__) */

#ifdef INLINE_IS_PRAGMA
#pragma inline(OSWriteMemoryBarrier)
#endif
static INLINE IMG_VOID OSWriteMemoryBarrier(IMG_VOID) { }

#ifdef INLINE_IS_PRAGMA
#pragma inline(OSMemoryBarrier)
#endif
static INLINE IMG_VOID OSMemoryBarrier(IMG_VOID) { }

#endif /* defined(__linux__) && defined(__KERNEL__) */

/* Atomic functions */
PVRSRV_ERROR OSAtomicAlloc(IMG_PVOID *ppvRefCount);
IMG_VOID OSAtomicFree(IMG_PVOID pvRefCount);
IMG_VOID OSAtomicInc(IMG_PVOID pvRefCount);
IMG_BOOL OSAtomicDecAndTest(IMG_PVOID pvRefCount);
IMG_UINT32 OSAtomicRead(IMG_PVOID pvRefCount);

PVRSRV_ERROR OSTimeCreateWithUSOffset(IMG_PVOID *pvRet, IMG_UINT32 ui32MSOffset);
IMG_BOOL OSTimeHasTimePassed(IMG_PVOID pvData);
IMG_VOID OSTimeDestroy(IMG_PVOID pvData);

#if defined(__linux__)
IMG_VOID OSReleaseBridgeLock(IMG_VOID);
IMG_VOID OSReacquireBridgeLock(IMG_VOID);
#else

#ifdef INLINE_IS_PRAGMA
#pragma inline(OSReleaseBridgeLock)
#endif
static INLINE IMG_VOID OSReleaseBridgeLock(IMG_VOID) { }

#ifdef INLINE_IS_PRAGMA
#pragma inline(OSReacquireBridgeLock)
#endif
static INLINE IMG_VOID OSReacquireBridgeLock(IMG_VOID) { }

#endif

#if defined(__linux__)
IMG_VOID OSGetCurrentProcessNameKM(IMG_CHAR *pszName, IMG_UINT32 ui32Size);
#else

#ifdef INLINE_IS_PRAGMA
#pragma inline(OSGetCurrentProcessNameKM)
#endif
static INLINE IMG_VOID OSGetCurrentProcessNameKM(IMG_CHAR *pszName, IMG_UINT32 ui32Size)
{
	PVR_UNREFERENCED_PARAMETER(pszName);
	PVR_UNREFERENCED_PARAMETER(ui32Size);
}

#endif

#if defined (__cplusplus)
}
#endif

#endif /* __OSFUNC_H__ */

/******************************************************************************
 End of file (osfunc.h)
******************************************************************************/

