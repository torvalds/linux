/**************************************************************************/ /*!
@File
@Title          Implementation Callbacks for Physmem (PMR) abstraction
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Part of the memory management.  This file is for definitions
                that are private to the world of PMRs, but that need to be
                shared between pmr.c itself and the modules that implement the
                callbacks for the PMR.
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

#ifndef _SRVSRV_PMR_IMPL_H_
#define _SRVSRV_PMR_IMPL_H_

/* include/ */
#include "powervr/mem_types.h"
#include "pvrsrv_memallocflags.h"
#include "img_types.h"
#include "pvrsrv_error.h"

typedef struct _PMR_ PMR;
/* stuff that per-flavour callbacks need to share with pmr.c */
typedef void *PMR_IMPL_PRIVDATA;

typedef PVRSRV_MEMALLOCFLAGS_T PMR_FLAGS_T;
typedef struct _PMR_MAPPING_TABLE_ PMR_MAPPING_TABLE;
typedef void *PMR_MMAP_DATA;

/**
 *  Which PMR factory has created this PMR?
 */
typedef enum _PMR_IMPL_TYPE_
{
	PMR_TYPE_NONE = 0,
	PMR_TYPE_OSMEM,
	PMR_TYPE_LMA,
	PMR_TYPE_DMABUF,
	PMR_TYPE_EXTMEM,
	PMR_TYPE_DC,
	PMR_TYPE_TDFWCODE,
	PMR_TYPE_TDSECBUF
} PMR_IMPL_TYPE;

/*************************************************************************/ /*!
@Brief          Callback function type PFN_LOCK_PHYS_ADDRESSES_FN

@Description    Called to lock down the physical addresses for all pages
                allocated for a PMR.
                The default implementation is to simply increment a
                lock-count for debugging purposes.
                If overridden, the PFN_LOCK_PHYS_ADDRESSES_FN function will
                be called when someone first requires a physical address,
                and the PFN_UNLOCK_PHYS_ADDRESSES_FN counterpart will be
                called when the last such reference is released.
                The PMR implementation may assume that physical addresses
                will have been "locked" in this manner before any call is
                made to the pfnDevPhysAddr() callback

@Input          pvPriv                    Private data (which was generated
                                          by the PMR factory when PMR was
                                          created)

@Return         PVRSRV_OK if the operation was successful, an error code
                otherwise.
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*PFN_LOCK_PHYS_ADDRESSES_FN)(PMR_IMPL_PRIVDATA pvPriv);

/*************************************************************************/ /*!
@Brief          Callback function type PFN_UNLOCK_PHYS_ADDRESSES_FN

@Description    Called to release the lock taken on the physical addresses
                for all pages allocated for a PMR.
                The default implementation is to simply decrement a
                lock-count for debugging purposes.
                If overridden, the PFN_UNLOCK_PHYS_ADDRESSES_FN will be
                called when the last reference taken on the PMR is
                released.

@Input          pvPriv                    Private data (which was generated
                                          by the PMR factory when PMR was
                                          created)

@Return         PVRSRV_OK if the operation was successful, an error code
                otherwise.
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*PFN_UNLOCK_PHYS_ADDRESSES_FN)(PMR_IMPL_PRIVDATA pvPriv);

/*************************************************************************/ /*!
@Brief          Callback function type PFN_DEV_PHYS_ADDR_FN

@Description    Called to obtain one or more physical addresses for given
                offsets within a PMR.

                The PFN_LOCK_PHYS_ADDRESSES_FN callback (if overridden) is
                guaranteed to have been called prior to calling the
                PFN_DEV_PHYS_ADDR_FN callback and the caller promises not to
                rely on the physical address thus obtained after the
                PFN_UNLOCK_PHYS_ADDRESSES_FN callback is called.

   Implementation of this callback is mandatory.

@Input          pvPriv                    Private data (which was generated
                                          by the PMR factory when PMR was
                                          created)
@Input          ui32Log2PageSize          The log2 page size.
@Input          ui32NumOfAddr             The number of addresses to be
                                          returned
@Input          puiOffset                 The offset from the start of the
                                          PMR (in bytes) for which the
                                          physical address is required.
                                          Where multiple addresses are
                                          requested, this will contain a
                                          list of offsets.
@Output         pbValid                   List of boolean flags indicating
                                          which addresses in the returned
                                          list (psDevAddrPtr) are valid
                                          (for sparse allocations, not all
                                          pages may have a physical backing)
@Output         psDevAddrPtr              Returned list of physical addresses

@Return         PVRSRV_OK if the operation was successful, an error code
                otherwise.
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*PFN_DEV_PHYS_ADDR_FN)(PMR_IMPL_PRIVDATA pvPriv,
                      IMG_UINT32 ui32Log2PageSize,
                      IMG_UINT32 ui32NumOfAddr,
                      IMG_DEVMEM_OFFSET_T *puiOffset,
                      IMG_BOOL *pbValid,
                      IMG_DEV_PHYADDR *psDevAddrPtr);

/*************************************************************************/ /*!
@Brief          Callback function type PFN_ACQUIRE_KERNEL_MAPPING_DATA_FN

@Description    Called to obtain a kernel-accessible address (mapped to a
                virtual address if required) for the PMR for use internally
                in Services.

    Implementation of this function for the (default) PMR factory providing
    OS-allocations is mandatory (the driver will expect to be able to call
    this function for OS-provided allocations).
    For other PMR factories, implementation of this function is only necessary
    where an MMU mapping is required for the Kernel to be able to access the
    allocated memory.
    If no mapping is needed, this function can remain unimplemented and the
    pfn may be set to NULL.
@Input          pvPriv                    Private data (which was generated
                                          by the PMR factory when PMR was
                                          created)
@Input          uiOffset                  Offset from the beginning of
                                          the PMR at which mapping is to
                                          start
@Input          uiSize                    Size of mapping (in bytes)
@Output         ppvKernelAddressOut       Mapped kernel address
@Output         phHandleOut	              Returned handle of the new mapping
@Input          ulFlags                   Mapping flags

@Return         PVRSRV_OK if the mapping was successful, an error code
                otherwise.
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*PFN_ACQUIRE_KERNEL_MAPPING_DATA_FN)(PMR_IMPL_PRIVDATA pvPriv,
                      size_t uiOffset,
                      size_t uiSize,
                      void **ppvKernelAddressOut,
                      IMG_HANDLE *phHandleOut,
                      PMR_FLAGS_T ulFlags);

/*************************************************************************/ /*!
@Brief          Callback function type PFN_RELEASE_KERNEL_MAPPING_DATA_FN

@Description    Called to release a mapped kernel virtual address

   Implementation of this callback is mandatory if PFN_ACQUIRE_KERNEL_MAPPING_DATA_FN
   is provided for the PMR factory, otherwise this function can remain unimplemented
   and the pfn may be set to NULL.

@Input          pvPriv                    Private data (which was generated
                                          by the PMR factory when PMR was
                                          created)
@Input          hHandle                   Handle of the mapping to be
                                          released

@Return         None
*/
/*****************************************************************************/
typedef void (*PFN_RELEASE_KERNEL_MAPPING_DATA_FN)(PMR_IMPL_PRIVDATA pvPriv,
              IMG_HANDLE hHandle);

/*************************************************************************/ /*!
@Brief          Callback function type PFN_READ_BYTES_FN

@Description    Called to read bytes from an unmapped allocation

   Implementation of this callback is optional -
   where it is not provided, the driver will use PFN_ACQUIRE_KERNEL_MAPPING_DATA_FN
   to map the entire PMR (if an MMU mapping is required for the Kernel to be
   able to access the allocated memory).

@Input          pvPriv                    Private data (which was generated
                                          by the PMR factory when PMR was
                                          created)
@Input          uiOffset                  Offset from the beginning of
                                          the PMR at which to begin
                                          reading
@Output         pcBuffer                  Buffer in which to return the
                                          read data
@Input          uiBufSz                   Number of bytes to be read
@Output         puiNumBytes               Number of bytes actually read
                                          (may be less than uiBufSz)

@Return         PVRSRV_OK if the read was successful, an error code
                otherwise.
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*PFN_READ_BYTES_FN)(PMR_IMPL_PRIVDATA pvPriv,
                      IMG_DEVMEM_OFFSET_T uiOffset,
                      IMG_UINT8 *pcBuffer,
                      size_t uiBufSz,
                      size_t *puiNumBytes);

/*************************************************************************/ /*!
@Brief          Callback function type PFN_WRITE_BYTES_FN

@Description    Called to write bytes into an unmapped allocation

   Implementation of this callback is optional -
   where it is not provided, the driver will use PFN_ACQUIRE_KERNEL_MAPPING_DATA_FN
   to map the entire PMR (if an MMU mapping is required for the Kernel to be
   able to access the allocated memory).

@Input          pvPriv                    Private data (which was generated
                                          by the PMR factory when PMR was
                                          created)
@Input          uiOffset                  Offset from the beginning of
                                          the PMR at which to begin
                                          writing
@Input          pcBuffer                  Buffer containing the data to be
                                          written
@Input          uiBufSz                   Number of bytes to be written
@Output         puiNumBytes               Number of bytes actually written
                                          (may be less than uiBufSz)

@Return         PVRSRV_OK if the write was successful, an error code
                otherwise.
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*PFN_WRITE_BYTES_FN)(PMR_IMPL_PRIVDATA pvPriv,
                      IMG_DEVMEM_OFFSET_T uiOffset,
                      IMG_UINT8 *pcBuffer,
                      size_t uiBufSz,
                      size_t *puiNumBytes);

/*************************************************************************/ /*!
@Brief          Callback function type PFN_UNPIN_MEM_FN

@Description    Called to unpin an allocation.
                Once unpinned, the pages backing the allocation may be
                re-used by the Operating System for another purpose.
                When the pages are required again, they may be re-pinned
                (by calling PFN_PIN_MEM_FN). The driver will try to return
                same pages as before. The caller will be told if the
                content of these returned pages has been modified or if
                the pages returned are not the original pages.

   Implementation of this callback is optional.

@Input          pvPriv                    Private data (which was generated
                                          by the PMR factory when PMR was
                                          created)

@Return         PVRSRV_OK if the unpin was successful, an error code
                otherwise.
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*PFN_UNPIN_MEM_FN)(PMR_IMPL_PRIVDATA pPriv);

/*************************************************************************/ /*!
@Brief          Callback function type PFN_PIN_MEM_FN

@Description    Called to pin a previously unpinned allocation.
                The driver will try to return same pages as were previously
                assigned to the allocation. The caller will be told if the
                content of these returned pages has been modified or if
                the pages returned are not the original pages.

   Implementation of this callback is optional.

@Input          pvPriv                    Private data (which was generated
                                          by the PMR factory when PMR was
                                          created)

@Input          psMappingTable            Mapping table, which describes how
                                          virtual 'chunks' are to be mapped to
                                          physical 'chunks' for the allocation.

@Return         PVRSRV_OK if the original pages were returned unmodified.
                PVRSRV_ERROR_PMR_NEW_MEMORY if the memory returned was modified
                or different pages were returned.
                Another PVRSRV_ERROR code on failure.
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*PFN_PIN_MEM_FN)(PMR_IMPL_PRIVDATA pPriv,
                      PMR_MAPPING_TABLE *psMappingTable);

/*************************************************************************/ /*!
@Brief          Callback function type PFN_CHANGE_SPARSE_MEM_FN

@Description    Called to modify the physical backing for a given sparse
                allocation.
                The caller provides a list of the pages within the sparse
                allocation which should be backed with a physical allocation
                and a list of the pages which do not require backing.

                Implementation of this callback is mandatory.

@Input          pvPriv                    Private data (which was generated
                                          by the PMR factory when PMR was
                                          created)
@Input          psPMR                     The PMR of the sparse allocation
                                          to be modified
@Input          ui32AllocPageCount        The number of pages specified in
                                          pai32AllocIndices
@Input          pai32AllocIndices         The list of pages in the sparse
                                          allocation that should be backed
                                          with a physical allocation. Pages
                                          are referenced by their index
                                          within the sparse allocation
                                          (e.g. in a 10 page allocation, pages
                                          are denoted by indices 0 to 9)
@Input          ui32FreePageCount         The number of pages specified in
                                          pai32FreeIndices
@Input          pai32FreeIndices          The list of pages in the sparse
                                          allocation that do not require
                                          a physical allocation.
@Input          ui32Flags                 Allocation flags

@Return         PVRSRV_OK if the sparse allocation physical backing was updated
                successfully, an error code otherwise.
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*PFN_CHANGE_SPARSE_MEM_FN)(PMR_IMPL_PRIVDATA pPriv,
                      const PMR *psPMR,
                      IMG_UINT32 ui32AllocPageCount,
                      IMG_UINT32 *pai32AllocIndices,
                      IMG_UINT32 ui32FreePageCount,
                      IMG_UINT32 *pai32FreeIndices,
                      IMG_UINT32 uiFlags);

/*************************************************************************/ /*!
@Brief          Callback function type PFN_CHANGE_SPARSE_MEM_CPU_MAP_FN

@Description    Called to modify which pages are mapped for a given sparse
                allocation.
                The caller provides a list of the pages within the sparse
                allocation which should be given a CPU mapping and a list
                of the pages which do not require a CPU mapping.

   Implementation of this callback is mandatory.

@Input          pvPriv                    Private data (which was generated
                                          by the PMR factory when PMR was
                                          created)
@Input          psPMR                     The PMR of the sparse allocation
                                          to be modified
@Input          sCpuVAddrBase             The virtual base address of the
                                          sparse allocation
@Input          ui32AllocPageCount        The number of pages specified in
                                          pai32AllocIndices
@Input          pai32AllocIndices         The list of pages in the sparse
                                          allocation that should be given
                                          a CPU mapping. Pages are referenced
                                          by their index within the sparse
                                          allocation (e.g. in a 10 page
                                          allocation, pages are denoted by
                                          indices 0 to 9)
@Input          ui32FreePageCount         The number of pages specified in
                                          pai32FreeIndices
@Input          pai32FreeIndices          The list of pages in the sparse
                                          allocation that do not require a CPU
                                          mapping.

@Return         PVRSRV_OK if the page mappings were updated successfully, an
                error code otherwise.
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*PFN_CHANGE_SPARSE_MEM_CPU_MAP_FN)(PMR_IMPL_PRIVDATA pPriv,
                      const PMR *psPMR,
                      IMG_UINT64 sCpuVAddrBase,
                      IMG_UINT32 ui32AllocPageCount,
                      IMG_UINT32 *pai32AllocIndices,
                      IMG_UINT32 ui32FreePageCount,
                      IMG_UINT32 *pai32FreeIndices);

/*************************************************************************/ /*!
@Brief          Callback function type PFN_MMAP_FN

@Description    Called to map pages in the specified PMR.

   Implementation of this callback is optional.
   Where it is provided, it will be used in place of OSMMapPMRGeneric().

@Input          pvPriv                    Private data (which was generated
                                          by the PMR factory when PMR was
                                          created)
@Input          psPMR                     The PMR of the allocation to be
                                          mapped
@Input          pMMapData                 OS-specific data to describe how
                                          mapping should be performed

@Return         PVRSRV_OK if the mapping was successful, an error code
                otherwise.
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*PFN_MMAP_FN)(PMR_IMPL_PRIVDATA pPriv,
                                    PMR *psPMR,
                                    PMR_MMAP_DATA pMMapData);

/*************************************************************************/ /*!
@Brief          Callback function type PFN_FINALIZE_FN

@Description    Called to destroy the PMR.
                This callback will be called only when all references to
                the PMR have been dropped.
                The PMR was created via a call to PhysmemNewRamBackedPMR()
                and is destroyed via this callback.

   Implementation of this callback is mandatory.

@Input          pvPriv                    Private data (which was generated
                                          by the PMR factory when PMR was
                                          created)

@Return         PVRSRV_OK if the PMR destruction was successful, an error
                code otherwise.
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*PFN_FINALIZE_FN)(PMR_IMPL_PRIVDATA pvPriv);

#if 1
struct _PMR_IMPL_FUNCTAB_ {
#else
typedef struct _PMR_IMPL_FUNCTAB_ {
#endif
    PFN_LOCK_PHYS_ADDRESSES_FN pfnLockPhysAddresses;
    PFN_UNLOCK_PHYS_ADDRESSES_FN pfnUnlockPhysAddresses;

    PFN_DEV_PHYS_ADDR_FN pfnDevPhysAddr;

    PFN_ACQUIRE_KERNEL_MAPPING_DATA_FN pfnAcquireKernelMappingData;
    PFN_RELEASE_KERNEL_MAPPING_DATA_FN pfnReleaseKernelMappingData;

#if defined (INTEGRITY_OS)
    /*
     * MapMemoryObject()/UnmapMemoryObject()
     *
     * called to map/unmap memory objects in Integrity OS
     */

    PVRSRV_ERROR (*pfnMapMemoryObject)(PMR_IMPL_PRIVDATA pvPriv,
    											IMG_HANDLE *phMemObj);
    PVRSRV_ERROR (*pfnUnmapMemoryObject)(PMR_IMPL_PRIVDATA pvPriv);
	
#if defined(USING_HYPERVISOR)
    IMG_HANDLE (*pfnGetPmr)(PMR_IMPL_PRIVDATA pvPriv, size_t ulOffset);
#endif
#endif
    
    PFN_READ_BYTES_FN pfnReadBytes;
    PFN_WRITE_BYTES_FN pfnWriteBytes;

    PFN_UNPIN_MEM_FN pfnUnpinMem;
    PFN_PIN_MEM_FN pfnPinMem;

    PFN_CHANGE_SPARSE_MEM_FN pfnChangeSparseMem;
    PFN_CHANGE_SPARSE_MEM_CPU_MAP_FN pfnChangeSparseMemCPUMap;

    PFN_MMAP_FN pfnMMap;

    PFN_FINALIZE_FN pfnFinalize;
} ;
typedef struct _PMR_IMPL_FUNCTAB_ PMR_IMPL_FUNCTAB;


#endif /* of #ifndef _SRVSRV_PMR_IMPL_H_ */
