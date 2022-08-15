/*************************************************************************/ /*!
@File
@Title          Physmem (PMR) abstraction
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Part of the memory management.  This module is responsible for
                the "PMR" abstraction.  A PMR (Physical Memory Resource)
                represents some unit of physical memory which is
                allocated/freed/mapped/unmapped as an indivisible unit
                (higher software levels provide an abstraction above that
                to deal with dividing this down into smaller manageable units).
                Importantly, this module knows nothing of virtual memory, or
                of MMUs etc., with one excusable exception.  We have the
                concept of a "page size", which really means nothing in
                physical memory, but represents a "contiguity quantum" such
                that the higher level modules which map this memory are able
                to verify that it matches the needs of the page size for the
                virtual realm into which it is being mapped.
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

#ifndef SRVSRV_PMR_H
#define SRVSRV_PMR_H

/* include/ */
#include "img_types.h"
#include "img_defs.h"
#include "pdumpdefs.h"
#include "pvrsrv_error.h"
#include "pvrsrv_memallocflags.h"
#include "devicemem_typedefs.h"			/* Required for export DEVMEM_EXPORTCOOKIE */

/* services/include */
#include "pdump.h"
#include "physheap.h"

/* services/server/include/ */
#include "pmr_impl.h"
#include "opaque_types.h"

#define PMR_MAX_TRANSLATION_STACK_ALLOC				(32)

/* Maximum number of pages a PMR can have is 1G of memory */
#define PMR_MAX_SUPPORTED_PAGE_COUNT				(262144)

typedef IMG_UINT64 PMR_BASE_T;
typedef IMG_UINT64 PMR_SIZE_T;
#define PMR_SIZE_FMTSPEC "0x%010"IMG_UINT64_FMTSPECX
#define PMR_VALUE32_FMTSPEC "0x%08X"
#define PMR_VALUE64_FMTSPEC "0x%016"IMG_UINT64_FMTSPECX
typedef IMG_UINT32 PMR_LOG2ALIGN_T;
typedef IMG_UINT64 PMR_PASSWORD_T;

struct _PMR_MAPPING_TABLE_
{
	PMR_SIZE_T	uiChunkSize;			/*!< Size of a "chunk" */
	IMG_UINT32	ui32NumPhysChunks;		/*!< Number of physical chunks that are valid */
	IMG_UINT32	ui32NumVirtChunks;		/*!< Number of virtual chunks in the mapping */
	/* Must be last */
	IMG_UINT32	aui32Translation[1];	/*!< Translation mapping for "logical" to physical */
};

#define TRANSLATION_INVALID 0xFFFFFFFFUL

typedef struct _PMR_EXPORT_ PMR_EXPORT;

typedef struct _PMR_PAGELIST_ PMR_PAGELIST;

/*
 * PMRCreatePMR
 *
 * Not to be called directly, only via implementations of PMR
 * factories, e.g. in physmem_osmem.c, deviceclass.c, etc.
 *
 * Creates a PMR object, with callbacks and private data as per the
 * FuncTab/PrivData args.
 *
 * Note that at creation time the PMR must set in stone the "logical
 * size" and the "contiguity guarantee"
 *
 * Flags are also set at this time.  (T.B.D.  flags also immutable for
 * the life of the PMR?)
 *
 * Logical size is the amount of Virtual space this allocation would
 * take up when mapped.  Note that this does not have to be the same
 * as the actual physical size of the memory.  For example, consider
 * the sparsely allocated non-power-of-2 texture case.  In this
 * instance, the "logical size" would be the virtual size of the
 * rounded-up power-of-2 texture.  That some pages of physical memory
 * may not exist does not affect the logical size calculation.
 *
 * The PMR must also supply the "contiguity guarantee" which is the
 * finest granularity of alignment and size of physical pages that the
 * PMR will provide after LockSysPhysAddresses is called.  Note that
 * the calling code may choose to call PMRSysPhysAddr with a finer
 * granularity than this, for example if it were to map into a device
 * MMU with a smaller page size, and it's also OK for the PMR to
 * supply physical memory in larger chunks than this.  But
 * importantly, never the other way around.
 *
 * More precisely, the following inequality must be maintained
 * whenever mappings and/or physical addresses exist:
 *
 *       (device MMU page size) <= 2**(uiLog2ContiguityGuarantee) <= (actual contiguity of physical memory)
 *
 * The function table will contain the following callbacks which may
 * be overridden by the PMR implementation:
 *
 * pfnLockPhysAddresses
 *
 *      Called when someone locks requests that Physical pages are to
 *      be locked down via the PMRLockSysPhysAddresses() API.  Note
 *      that if physical pages are prefaulted at PMR creation time and
 *      therefore static, it would not be necessary to override this
 *      function, in which case NULL may be supplied.
 *
 * pfnUnlockPhysAddresses
 *
 *      The reverse of pfnLockPhysAddresses.  Note that this should be
 *      NULL if and only if pfnLockPhysAddresses is NULL
 *
 * pfnSysPhysAddr
 *
 *      This function is mandatory.  This is the one which returns the
 *      system physical address for a given offset into this PMR.  The
 *      "lock" function will have been called, if overridden, before
 *      this function, thus the implementation should not increase any
 *      refcount when answering this call.  Refcounting, if necessary,
 *      should be done in the lock/unlock calls.  Refcounting would
 *      not be necessary in the prefaulted/static scenario, as the
 *      pmr.c abstraction will handle the refcounting for the whole
 *      PMR.
 *
 * pfnFinalize
 *
 *      Called when the PMR's refcount reaches zero and it gets
 *      destroyed.  This allows the implementation to free up any
 *      resource acquired during creation time.
 *
 */
PVRSRV_ERROR
PMRCreatePMR(PHYS_HEAP *psPhysHeap,
             PMR_SIZE_T uiLogicalSize,
             PMR_SIZE_T uiChunkSize,
             IMG_UINT32 ui32NumPhysChunks,
             IMG_UINT32 ui32NumVirtChunks,
             IMG_UINT32 *pui32MappingTable,
             PMR_LOG2ALIGN_T uiLog2ContiguityGuarantee,
             PMR_FLAGS_T uiFlags,
             const IMG_CHAR *pszAnnotation,
             const PMR_IMPL_FUNCTAB *psFuncTab,
             PMR_IMPL_PRIVDATA pvPrivData,
             PMR_IMPL_TYPE eType,
             PMR **ppsPMRPtr,
             IMG_UINT32 ui32PDumpFlags);

/*
 * PMRLockSysPhysAddresses()
 *
 * Calls the relevant callback to lock down the system physical addresses of
 * the memory that makes up the whole PMR.
 *
 * Before this call, it is not valid to use any of the information
 * getting APIs: PMR_Flags(), PMR_SysPhysAddr(),
 * [ see note below about lock/unlock semantics ]
 *
 * The caller of this function does not have to care about how the PMR
 * is implemented.  He only has to know that he is allowed access to
 * the physical addresses _after_ calling this function and _until_
 * calling PMRUnlockSysPhysAddresses().
 *
 *
 * Notes to callback implementers (authors of PMR Factories):
 *
 * Some PMR implementations will be such that the physical memory exists for
 * the lifetime of the PMR, with a static address, (and normally flags and
 * symbolic address are static too) and so it is legal for a PMR
 * implementation to not provide an implementation for the lock callback.
 *
 * Some PMR implementation may wish to page memory in from secondary storage
 * on demand. The lock/unlock callbacks _may_ be the place to do this.
 * (More likely, there would be a separate API for doing this, but this API
 * provides a useful place to assert that it has been done)
 */

PVRSRV_ERROR
PMRLockSysPhysAddresses(PMR *psPMR);

PVRSRV_ERROR
PMRLockSysPhysAddressesNested(PMR *psPMR,
                        IMG_UINT32 ui32NestingLevel);

/*
 * PMRUnlockSysPhysAddresses()
 *
 * the reverse of PMRLockSysPhysAddresses()
 */
PVRSRV_ERROR
PMRUnlockSysPhysAddresses(PMR *psPMR);

PVRSRV_ERROR
PMRUnlockSysPhysAddressesNested(PMR *psPMR, IMG_UINT32 ui32NestingLevel);


/*************************************************************************/ /*!
@Function       PMRUnpinPMR
@Description    This is the counterpart to PMRPinPMR(). It is meant to be
                called before repinning an allocation.

                For a detailed description see client API documentation.

@Input          psPMR           The physical memory to unpin.

@Input          bDevMapped      A flag that indicates if this PMR has been
                                mapped to device virtual space.
                                Needed to check if this PMR is allowed to be
                                unpinned or not.

@Return         PVRSRV_ERROR:   PVRSRV_OK on success and the memory is
                                registered to be reclaimed. Error otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR PMRUnpinPMR(PMR *psPMR, IMG_BOOL bDevMapped);

/*************************************************************************/ /*!
@Function       PMRPinPMR
@Description    This is the counterpart to PMRUnpinPMR(). It is meant to be
                called after unpinning an allocation.

                For a detailed description see client API documentation.

@Input          psPMR           The physical memory to pin.

@Return         PVRSRV_ERROR:   PVRSRV_OK on success and the allocation content
                                was successfully restored.

                                PVRSRV_ERROR_PMR_NEW_MEMORY when the content
                                could not be restored and new physical memory
                                was allocated.

                                A different error otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR PMRPinPMR(PMR *psPMR);

/*
 * PhysmemPMRExport()
 *
 * Given a PMR, creates a PMR "Export", which is a handle that
 * provides sufficient data to be able to "import" this PMR elsewhere.
 * The PMR Export is an object in its own right, whose existence
 * implies a reference on the PMR, thus the PMR cannot be destroyed
 * while the PMR Export exists.  The intention is that the PMR Export
 * will be wrapped in the devicemem layer by a cross process handle,
 * and some IPC by which to communicate the handle value and password
 * to other processes.  The receiving process is able to unwrap this
 * to gain access to the same PMR Export in this layer, and, via
 * PhysmemPMRImport(), obtain a reference to the original PMR.
 *
 * The caller receives, along with the PMR Export object, information
 * about the size and contiguity guarantee for the PMR, and also the
 * PMRs secret password, in order to authenticate the subsequent
 * import.
 *
 * N.B.  If you call PMRExportPMR() (and it succeeds), you are
 * promising to later call PMRUnexportPMR()
 */
PVRSRV_ERROR
PMRExportPMR(PMR *psPMR,
             PMR_EXPORT **ppsPMRExport,
             PMR_SIZE_T *puiSize,
             PMR_LOG2ALIGN_T *puiLog2Contig,
             PMR_PASSWORD_T *puiPassword);

/*!
*******************************************************************************

 @Function      PMRMakeLocalImportHandle

 @Description

 Transform a general handle type into one that we are able to import.
 Takes a PMR reference.

 @Input   psPMR     The input PMR.
 @Output  ppsPMR    The output PMR that is going to be transformed to the
                    correct handle type.

 @Return   PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR
PMRMakeLocalImportHandle(PMR *psPMR,
                         PMR **ppsPMR);

/*!
*******************************************************************************

 @Function      PMRUnmakeLocalImportHandle

 @Description

 Take a PMR, destroy the handle and release a reference.
 Counterpart to PMRMakeServerExportClientExport().

 @Input   psPMR       PMR to destroy.
                      Created by PMRMakeLocalImportHandle().

 @Return   PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR
PMRUnmakeLocalImportHandle(PMR *psPMR);

/*
 * PMRUnexporPMRt()
 *
 * The reverse of PMRExportPMR().  This causes the PMR to no longer be
 * exported.  If the PMR has already been imported, the imported PMR
 * reference will still be valid, but no further imports will be possible.
 */
PVRSRV_ERROR
PMRUnexportPMR(PMR_EXPORT *psPMRExport);

/*
 * PMRImportPMR()
 *
 * Takes a PMR Export object, as obtained by PMRExportPMR(), and
 * obtains a reference to the original PMR.
 *
 * The password must match, and is assumed to have been (by whatever
 * means, IPC etc.) preserved intact from the former call to
 * PMRExportPMR()
 *
 * The size and contiguity arguments are entirely irrelevant for the
 * import, however they are verified in order to trap bugs.
 *
 * N.B.  If you call PhysmemPMRImport() (and it succeeds), you are
 * promising to later call PhysmemPMRUnimport()
 */
PVRSRV_ERROR
PMRImportPMR(PMR_EXPORT *psPMRExport,
             PMR_PASSWORD_T uiPassword,
             PMR_SIZE_T uiSize,
             PMR_LOG2ALIGN_T uiLog2Contig,
             PMR **ppsPMR);

/* Function that alters the mutability property
 * of the PMR
 * Setting it to TRUE makes sure the PMR memory layout
 * can't be changed through future calls */
void
PMR_SetLayoutFixed(PMR *psPMR, IMG_BOOL bFlag);

IMG_BOOL PMR_IsMemLayoutFixed(PMR *psPMR);

/*
 * PMRUnimportPMR()
 *
 * releases the reference on the PMR as obtained by PMRImportPMR()
 */
PVRSRV_ERROR
PMRUnimportPMR(PMR *psPMR);

PVRSRV_ERROR
PMRLocalImportPMR(PMR *psPMR,
                  PMR **ppsPMR,
                  IMG_DEVMEM_SIZE_T *puiSize,
                  IMG_DEVMEM_ALIGN_T *puiAlign);

/*
 * Equivalent mapping functions when in kernel mode.
 */
PVRSRV_ERROR
PMRAcquireKernelMappingData(PMR *psPMR,
                            size_t uiLogicalOffset,
                            size_t uiSize,
                            void **ppvKernelAddressOut,
                            size_t *puiLengthOut,
                            IMG_HANDLE *phPrivOut);

PVRSRV_ERROR
PMRAcquireSparseKernelMappingData(PMR *psPMR,
                                  size_t uiLogicalOffset,
                                  size_t uiSize,
                                  void **ppvKernelAddressOut,
                                  size_t *puiLengthOut,
                                  IMG_HANDLE *phPrivOut);

PVRSRV_ERROR
PMRReleaseKernelMappingData(PMR *psPMR,
                            IMG_HANDLE hPriv);

/*
 * PMR_ReadBytes()
 *
 * calls into the PMR implementation to read up to uiBufSz bytes,
 * returning the actual number read in *puiNumBytes
 *
 * this will read up to the end of the PMR, or the next symbolic name
 * boundary, or until the requested number of bytes is read, whichever
 * comes first
 *
 * In the case of sparse PMR's the caller doesn't know what offsets are
 * valid and which ones aren't so we will just write 0 to invalid offsets
 */
PVRSRV_ERROR
PMR_ReadBytes(PMR *psPMR,
              IMG_DEVMEM_OFFSET_T uiLogicalOffset,
              IMG_UINT8 *pcBuffer,
              size_t uiBufSz,
              size_t *puiNumBytes);

/*
 * PMR_WriteBytes()
 *
 * calls into the PMR implementation to write up to uiBufSz bytes,
 * returning the actual number read in *puiNumBytes
 *
 * this will write up to the end of the PMR, or the next symbolic name
 * boundary, or until the requested number of bytes is written, whichever
 * comes first
 *
 * In the case of sparse PMR's the caller doesn't know what offsets are
 * valid and which ones aren't so we will just ignore data at invalid offsets
 */
PVRSRV_ERROR
PMR_WriteBytes(PMR *psPMR,
               IMG_DEVMEM_OFFSET_T uiLogicalOffset,
               IMG_UINT8 *pcBuffer,
               size_t uiBufSz,
               size_t *puiNumBytes);

/*************************************************************************/ /*!
@Function       PMRMMapPMR
@Description    Performs the necessary steps to map the PMR into a user process
                address space. The caller does not need to call
                PMRLockSysPhysAddresses before calling this function.

@Input          psPMR           PMR to map.

@Input          pOSMMapData     OS specific data needed to create a mapping.

@Return         PVRSRV_ERROR:   PVRSRV_OK on success or an error otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR
PMRMMapPMR(PMR *psPMR, PMR_MMAP_DATA pOSMMapData);

/*
 * PMRRefPMR()
 *
 * Take a reference on the passed in PMR
 */
void
PMRRefPMR(PMR *psPMR);

/*
 * PMRUnrefPMR()
 *
 * This undoes a call to any of the PhysmemNew* family of APIs
 * (i.e. any PMR factory "constructor")
 *
 * This relinquishes a reference to the PMR, and, where the refcount
 * reaches 0, causes the PMR to be destroyed (calling the finalizer
 * callback on the PMR, if there is one)
 */
PVRSRV_ERROR
PMRUnrefPMR(PMR *psPMR);

/*
 * PMRUnrefUnlockPMR()
 *
 * Same as above but also unlocks the PMR.
 */
PVRSRV_ERROR
PMRUnrefUnlockPMR(PMR *psPMR);

PPVRSRV_DEVICE_NODE
PMR_DeviceNode(const PMR *psPMR);

/*
 * PMRIsPMRLive()
 *
 * This function returns true if the PMR is in use and false otherwise.
 * This function is not thread safe and hence the caller needs to ensure the
 * thread safety by explicitly taking PMR or through other means.
 */
IMG_BOOL PMRIsPMRLive(PMR *psPMR);

/*
 * PMR_Flags()
 *
 * Flags are static and guaranteed for the life of the PMR.  Thus this
 * function is idempotent and acquire/release semantics is not required.
 *
 * Returns the flags as specified on the PMR.  The flags are to be
 * interpreted as mapping permissions
 */
PMR_FLAGS_T
PMR_Flags(const PMR *psPMR);

IMG_BOOL
PMR_IsSparse(const PMR *psPMR);

IMG_BOOL
PMR_IsUnpinned(const PMR *psPMR);

void
PMR_LogicalSize(const PMR *psPMR,
				IMG_DEVMEM_SIZE_T *puiLogicalSize);

PVRSRV_ERROR
PMR_PhysicalSize(const PMR *psPMR,
				 IMG_DEVMEM_SIZE_T *puiPhysicalSize);

PHYS_HEAP *
PMR_PhysHeap(const PMR *psPMR);

PMR_MAPPING_TABLE *
PMR_GetMappingTable(const PMR *psPMR);

IMG_UINT32
PMR_GetLog2Contiguity(const PMR *psPMR);

const IMG_CHAR *
PMR_GetAnnotation(const PMR *psPMR);

/*
 * PMR_IsOffsetValid()
 *
 * Returns if an address offset inside a PMR has a valid
 * physical backing.
 */
PVRSRV_ERROR
PMR_IsOffsetValid(const PMR *psPMR,
				IMG_UINT32 ui32Log2PageSize,
				IMG_UINT32 ui32NumOfPages,
				IMG_DEVMEM_OFFSET_T uiLogicalOffset,
				IMG_BOOL *pbValid);

PMR_IMPL_TYPE
PMR_GetType(const PMR *psPMR);

IMG_INT32
PMR_GetRefCount(const PMR *psPMR);

/*
 * PMR_DevPhysAddr()
 *
 * A note regarding Lock/Unlock semantics
 * ======================================
 *
 * PMR_DevPhysAddr may only be called after PMRLockSysPhysAddresses()
 * has been called.  The data returned may be used only until
 * PMRUnlockSysPhysAddresses() is called after which time the licence
 * to use the data is revoked and the information may be invalid.
 *
 * Given an offset, this function returns the device physical address of the
 * corresponding page in the PMR.  It may be called multiple times
 * until the address of all relevant pages has been determined.
 *
 * If caller only wants one physical address it is sufficient to pass in:
 * ui32Log2PageSize==0 and ui32NumOfPages==1
 */
PVRSRV_ERROR
PMR_DevPhysAddr(const PMR *psPMR,
                IMG_UINT32 ui32Log2PageSize,
                IMG_UINT32 ui32NumOfPages,
                IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                IMG_DEV_PHYADDR *psDevAddr,
                IMG_BOOL *pbValid);

/*
 * PMR_CpuPhysAddr()
 *
 * See note above about Lock/Unlock semantics.
 *
 * Given an offset, this function returns the CPU physical address of the
 * corresponding page in the PMR.  It may be called multiple times
 * until the address of all relevant pages has been determined.
 *
 */
PVRSRV_ERROR
PMR_CpuPhysAddr(const PMR *psPMR,
                IMG_UINT32 ui32Log2PageSize,
                IMG_UINT32 ui32NumOfPages,
                IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                IMG_CPU_PHYADDR *psCpuAddrPtr,
                IMG_BOOL *pbValid);

PVRSRV_ERROR
PMRGetUID(PMR *psPMR,
          IMG_UINT64 *pui64UID);
/*
 * PMR_ChangeSparseMem()
 *
 * See note above about Lock/Unlock semantics.
 *
 * This function alters the memory map of the given PMR in device space by
 *  adding/deleting the pages as requested.
 *
 */
PVRSRV_ERROR PMR_ChangeSparseMem(PMR *psPMR,
                                 IMG_UINT32 ui32AllocPageCount,
                                 IMG_UINT32 *pai32AllocIndices,
                                 IMG_UINT32 ui32FreePageCount,
                                 IMG_UINT32 *pai32FreeIndices,
                                 IMG_UINT32	uiSparseFlags);

/*
 * PMR_ChangeSparseMemCPUMap()
 *
 * See note above about Lock/Unlock semantics.
 *
 * This function alters the memory map of the given PMR in CPU space by
 * adding/deleting the pages as requested.
 */
PVRSRV_ERROR PMR_ChangeSparseMemCPUMap(PMR *psPMR,
                                       IMG_UINT64 sCpuVAddrBase,
                                       IMG_UINT32 ui32AllocPageCount,
                                       IMG_UINT32 *pai32AllocIndices,
                                       IMG_UINT32 ui32FreePageCount,
                                       IMG_UINT32 *pai32FreeIndices);

#if defined(PDUMP)

/*
 * PMR_PDumpSymbolicAddr()
 *
 * Given an offset, returns the pdump memspace name and symbolic
 * address of the corresponding page in the PMR.
 *
 * Note that PDump memspace names and symbolic addresses are static
 * and valid for the lifetime of the PMR, therefore we don't require
 * acquire/release semantics here.
 *
 * Note that it is expected that the pdump "mapping" code will call
 * this function multiple times as each page is mapped in turn
 *
 * Note that NextSymName is the offset from the base of the PMR to the
 * next pdump symbolic address (or the end of the PMR if the PMR only
 * had one PDUMPMALLOC
 */
PVRSRV_ERROR
PMR_PDumpSymbolicAddr(const PMR *psPMR,
                      IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                      IMG_UINT32 ui32NamespaceNameLen,
                      IMG_CHAR *pszNamespaceName,
                      IMG_UINT32 ui32SymbolicAddrLen,
                      IMG_CHAR *pszSymbolicAddr,
                      IMG_DEVMEM_OFFSET_T *puiNewOffset,
		      IMG_DEVMEM_OFFSET_T *puiNextSymName
                      );

/*
 * PMRPDumpLoadMemValue32()
 *
 * writes the current contents of a dword in PMR memory to the pdump
 * script stream. Useful for patching a buffer by simply editing the
 * script output file in ASCII plain text.
 *
 */
PVRSRV_ERROR
PMRPDumpLoadMemValue32(PMR *psPMR,
			         IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                     IMG_UINT32 ui32Value,
                     PDUMP_FLAGS_T uiPDumpFlags);

/*
 * PMRPDumpCopyMem32
 *
 * Adds in the pdump script stream a copy of a dword in one PMR memory
 * location to another PMR memory location.
 *
 */
PVRSRV_ERROR
PMRPDumpCopyMem32(PMR *psDstPMR,
                  IMG_DEVMEM_OFFSET_T uiDstLogicalOffset,
                  PMR *psSrcPMR,
                  IMG_DEVMEM_OFFSET_T uiSrcLogicalOffset,
                  const IMG_CHAR *pszTmpVar,
                  PDUMP_FLAGS_T uiPDumpFlags);

/*
 * PMRPDumpLoadMemValue64()
 *
 * writes the current contents of a dword in PMR memory to the pdump
 * script stream. Useful for patching a buffer by simply editing the
 * script output file in ASCII plain text.
 *
 */
PVRSRV_ERROR
PMRPDumpLoadMemValue64(PMR *psPMR,
			         IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                     IMG_UINT64 ui64Value,
                     PDUMP_FLAGS_T uiPDumpFlags);

/*
 * PMRPDumpCopyMem64
 *
 * Adds in the pdump script stream a copy of a quadword in one PMR memory
 * location to another PMR memory location.
 */
PVRSRV_ERROR
PMRPDumpCopyMem64(PMR *psDstPMR,
                  IMG_DEVMEM_OFFSET_T uiDstLogicalOffset,
                  PMR *psSrcPMR,
                  IMG_DEVMEM_OFFSET_T uiSrcLogicalOffset,
                  const IMG_CHAR *pszTmpVar,
                  PDUMP_FLAGS_T uiPDumpFlags);

/*
 * PMRPDumpLoadMem()
 *
 * Writes the current contents of the PMR memory to the pdump PRM stream,
 * and emits some PDump code to the script stream to LDB said bytes from
 * said file. If bZero is IMG_TRUE then the PDump zero page is used as the
 * source for the LDB.
 */
PVRSRV_ERROR
PMRPDumpLoadMem(PMR *psPMR,
                IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                IMG_DEVMEM_SIZE_T uiSize,
                PDUMP_FLAGS_T uiPDumpFlags,
                IMG_BOOL bZero);

/*
 * PMRPDumpSaveToFile()
 *
 * Emits some PDump that does an SAB (save bytes) using the PDump symbolic
 * address of the PMR. Note that this is generally not the preferred way to
 * dump the buffer contents. There is an equivalent function in
 * devicemem_server.h which also emits SAB but using the virtual address,
 * which is the "right" way to dump the buffer contents to a file.
 * This function exists just to aid testing by providing a means to dump
 * the PMR directly by symbolic address also.
 */
PVRSRV_ERROR
PMRPDumpSaveToFile(const PMR *psPMR,
                   IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                   IMG_DEVMEM_SIZE_T uiSize,
                   IMG_UINT32 uiArraySize,
                   const IMG_CHAR *pszFilename,
                   IMG_UINT32 uiFileOffset);
#else /* PDUMP */

#ifdef INLINE_IS_PRAGMA
#pragma inline(PMR_PDumpSymbolicAddr)
#endif
static INLINE PVRSRV_ERROR
PMR_PDumpSymbolicAddr(const PMR *psPMR,
                      IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                      IMG_UINT32 ui32NamespaceNameLen,
                      IMG_CHAR *pszNamespaceName,
                      IMG_UINT32 ui32SymbolicAddrLen,
                      IMG_CHAR *pszSymbolicAddr,
                      IMG_DEVMEM_OFFSET_T *puiNewOffset,
                      IMG_DEVMEM_OFFSET_T *puiNextSymName)
{
	PVR_UNREFERENCED_PARAMETER(psPMR);
	PVR_UNREFERENCED_PARAMETER(uiLogicalOffset);
	PVR_UNREFERENCED_PARAMETER(ui32NamespaceNameLen);
	PVR_UNREFERENCED_PARAMETER(pszNamespaceName);
	PVR_UNREFERENCED_PARAMETER(ui32SymbolicAddrLen);
	PVR_UNREFERENCED_PARAMETER(pszSymbolicAddr);
	PVR_UNREFERENCED_PARAMETER(puiNewOffset);
	PVR_UNREFERENCED_PARAMETER(puiNextSymName);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PMRPDumpLoadMemValue32)
#endif
static INLINE PVRSRV_ERROR
PMRPDumpLoadMemValue32(PMR *psPMR,
			         IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                     IMG_UINT32 ui32Value,
                     PDUMP_FLAGS_T uiPDumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psPMR);
	PVR_UNREFERENCED_PARAMETER(uiLogicalOffset);
	PVR_UNREFERENCED_PARAMETER(ui32Value);
	PVR_UNREFERENCED_PARAMETER(uiPDumpFlags);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PMRPDumpLoadMemValue64)
#endif
static INLINE PVRSRV_ERROR
PMRPDumpLoadMemValue64(PMR *psPMR,
			         IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                     IMG_UINT64 ui64Value,
                     PDUMP_FLAGS_T uiPDumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psPMR);
	PVR_UNREFERENCED_PARAMETER(uiLogicalOffset);
	PVR_UNREFERENCED_PARAMETER(ui64Value);
	PVR_UNREFERENCED_PARAMETER(uiPDumpFlags);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PMRPDumpLoadMem)
#endif
static INLINE PVRSRV_ERROR
PMRPDumpLoadMem(PMR *psPMR,
                IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                IMG_DEVMEM_SIZE_T uiSize,
                PDUMP_FLAGS_T uiPDumpFlags,
                IMG_BOOL bZero)
{
	PVR_UNREFERENCED_PARAMETER(psPMR);
	PVR_UNREFERENCED_PARAMETER(uiLogicalOffset);
	PVR_UNREFERENCED_PARAMETER(uiSize);
	PVR_UNREFERENCED_PARAMETER(uiPDumpFlags);
	PVR_UNREFERENCED_PARAMETER(bZero);
	return PVRSRV_OK;
}


#ifdef INLINE_IS_PRAGMA
#pragma inline(PMRPDumpSaveToFile)
#endif
static INLINE PVRSRV_ERROR
PMRPDumpSaveToFile(const PMR *psPMR,
                   IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                   IMG_DEVMEM_SIZE_T uiSize,
                   IMG_UINT32 uiArraySize,
                   const IMG_CHAR *pszFilename,
                   IMG_UINT32 uiFileOffset)
{
	PVR_UNREFERENCED_PARAMETER(psPMR);
	PVR_UNREFERENCED_PARAMETER(uiLogicalOffset);
	PVR_UNREFERENCED_PARAMETER(uiSize);
	PVR_UNREFERENCED_PARAMETER(uiArraySize);
	PVR_UNREFERENCED_PARAMETER(pszFilename);
	PVR_UNREFERENCED_PARAMETER(uiFileOffset);
	return PVRSRV_OK;
}

#endif	/* PDUMP */

/* This function returns the private data that a pmr subtype embedded in
 * here. We use the function table pointer as "authorisation" that this
 * function is being called by the pmr subtype implementation. We can
 * assume (assert) that. It would be a bug in the implementation of the
 * pmr subtype if this assertion ever fails.
 */
void *
PMRGetPrivateData(const PMR *psPMR,
                  const PMR_IMPL_FUNCTAB *psFuncTab);

PVRSRV_ERROR
PMRZeroingPMR(PMR *psPMR,
				IMG_DEVMEM_LOG2ALIGN_T uiLog2PageSize);

PVRSRV_ERROR
PMRDumpPageList(PMR *psReferencePMR,
					IMG_DEVMEM_LOG2ALIGN_T uiLog2PageSize);

PVRSRV_ERROR
PMRWritePMPageList(/* Target PMR, offset, and length */
                   PMR *psPageListPMR,
                   IMG_DEVMEM_OFFSET_T uiTableOffset,
                   IMG_DEVMEM_SIZE_T  uiTableLength,
                   /* Referenced PMR, and "page" granularity */
                   PMR *psReferencePMR,
                   IMG_DEVMEM_LOG2ALIGN_T uiLog2PageSize,
                   PMR_PAGELIST **ppsPageList);

/* Doesn't actually erase the page list - just releases
 * the appropriate refcounts
 */
PVRSRV_ERROR // should be void, surely
PMRUnwritePMPageList(PMR_PAGELIST *psPageList);

#if defined(PDUMP)
PVRSRV_ERROR
PMRPDumpPol32(const PMR *psPMR,
              IMG_DEVMEM_OFFSET_T uiLogicalOffset,
              IMG_UINT32 ui32Value,
              IMG_UINT32 ui32Mask,
              PDUMP_POLL_OPERATOR eOperator,
              PDUMP_FLAGS_T uiFlags);

PVRSRV_ERROR
PMRPDumpCheck32(const PMR *psPMR,
				IMG_DEVMEM_OFFSET_T uiLogicalOffset,
				IMG_UINT32 ui32Value,
				IMG_UINT32 ui32Mask,
				PDUMP_POLL_OPERATOR eOperator,
				PDUMP_FLAGS_T uiPDumpFlags);

PVRSRV_ERROR
PMRPDumpCBP(const PMR *psPMR,
            IMG_DEVMEM_OFFSET_T uiReadOffset,
            IMG_DEVMEM_OFFSET_T uiWriteOffset,
            IMG_DEVMEM_SIZE_T uiPacketSize,
            IMG_DEVMEM_SIZE_T uiBufferSize);
#else

#ifdef INLINE_IS_PRAGMA
#pragma inline(PMRPDumpPol32)
#endif
static INLINE PVRSRV_ERROR
PMRPDumpPol32(const PMR *psPMR,
              IMG_DEVMEM_OFFSET_T uiLogicalOffset,
              IMG_UINT32 ui32Value,
              IMG_UINT32 ui32Mask,
              PDUMP_POLL_OPERATOR eOperator,
              PDUMP_FLAGS_T uiFlags)
{
	PVR_UNREFERENCED_PARAMETER(psPMR);
	PVR_UNREFERENCED_PARAMETER(uiLogicalOffset);
	PVR_UNREFERENCED_PARAMETER(ui32Value);
	PVR_UNREFERENCED_PARAMETER(ui32Mask);
	PVR_UNREFERENCED_PARAMETER(eOperator);
	PVR_UNREFERENCED_PARAMETER(uiFlags);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PMRPDumpCheck32)
#endif
static INLINE PVRSRV_ERROR
PMRPDumpCheck32(const PMR *psPMR,
				IMG_DEVMEM_OFFSET_T uiLogicalOffset,
				IMG_UINT32 ui32Value,
				IMG_UINT32 ui32Mask,
				PDUMP_POLL_OPERATOR eOperator,
				PDUMP_FLAGS_T uiFlags)
{
	PVR_UNREFERENCED_PARAMETER(psPMR);
	PVR_UNREFERENCED_PARAMETER(uiLogicalOffset);
	PVR_UNREFERENCED_PARAMETER(ui32Value);
	PVR_UNREFERENCED_PARAMETER(ui32Mask);
	PVR_UNREFERENCED_PARAMETER(eOperator);
	PVR_UNREFERENCED_PARAMETER(uiFlags);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PMRPDumpCBP)
#endif
static INLINE PVRSRV_ERROR
PMRPDumpCBP(const PMR *psPMR,
            IMG_DEVMEM_OFFSET_T uiReadOffset,
            IMG_DEVMEM_OFFSET_T uiWriteOffset,
            IMG_DEVMEM_SIZE_T uiPacketSize,
            IMG_DEVMEM_SIZE_T uiBufferSize)
{
	PVR_UNREFERENCED_PARAMETER(psPMR);
	PVR_UNREFERENCED_PARAMETER(uiReadOffset);
	PVR_UNREFERENCED_PARAMETER(uiWriteOffset);
	PVR_UNREFERENCED_PARAMETER(uiPacketSize);
	PVR_UNREFERENCED_PARAMETER(uiBufferSize);
	return PVRSRV_OK;
}
#endif

PPVRSRV_DEVICE_NODE PMRGetExportDeviceNode(PMR_EXPORT *psExportPMR);

/*
 * PMRInit()
 *
 * To be called once and only once to initialise the internal data in
 * the PMR module (mutexes and such)
 *
 * Not for general use.  Only PVRSRVInit(); should be calling this.
 */
PVRSRV_ERROR
PMRInit(void);

/*
 * PMRDeInit()
 *
 * To be called once and only once to deinitialise the internal data in
 * the PMR module (mutexes and such) and for debug checks
 *
 * Not for general use.  Only PVRSRVDeInit(); should be calling this.
 */
PVRSRV_ERROR
PMRDeInit(void);

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
PVRSRV_ERROR
PMRStoreRIHandle(PMR *psPMR, void *hRIHandle);
#endif

#endif /* #ifdef SRVSRV_PMR_H */
