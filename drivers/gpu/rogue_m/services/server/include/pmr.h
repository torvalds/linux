/**************************************************************************/ /*!
@File
@Title		Physmem (PMR) abstraction
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description	Part of the memory management.  This module is responsible for
                the "PMR" abstraction.  A PMR (Physical Memory Resource)
                represents some unit of physical memory which is
                allocated/freed/mapped/unmapped as an indivisible unit
                (higher software levels provide an abstraction above that
                to deal with dividing this down into smaller manageable units).
                Importantly, this module knows nothing of virtual memory, or
                of MMUs etc., with one excuseable exception.  We have the
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
*/ /***************************************************************************/

#ifndef _SRVSRV_PMR_H_
#define _SRVSRV_PMR_H_

/* include/ */
#include "img_types.h"
#include "pdumpdefs.h"
#include "pvrsrv_error.h"
#include "pvrsrv_memallocflags.h"
#include "devicemem_typedefs.h"			/* Required for export DEVMEM_EXPORTCOOKIE */

/* services/include */
#include "pdump.h"

/* services/server/include/ */
#include "pmr_impl.h"
#include "physheap.h"
/* A typical symbolic address for physical memory may look like:
   :MEMORYSPACE:SUBSYS_NNNNNN_0X1234567890_XYZ.  That example is quite
   extreme, they are likely shorter than that.  We'll make the define
   here plentiful, however, note that this is _advisory_ not
   _mandatory_ - in other words, it's the allocator's responsibility
   to choose the amount of memory to set aside, and it's up to us to
   honour the size passed in by the caller.  i.e. this define is for
   GUIDANCE ONLY.
*/
#define PMR_MAX_SYMBOLIC_ADDRESS_LENGTH_DEFAULT		(60)
#define PMR_MAX_MEMSPACE_NAME_LENGTH_DEFAULT		(20)
#define PMR_MAX_MEMSPNAME_SYMB_ADDR_LENGTH_DEFAULT	(PMR_MAX_SYMBOLIC_ADDRESS_LENGTH_DEFAULT + PMR_MAX_MEMSPACE_NAME_LENGTH_DEFAULT)
#define PMR_MAX_PARAMSTREAM_FILENAME_LENGTH_DEFAULT (100)
#define PMR_MAX_TRANSLATION_STACK_ALLOC				(32)

typedef IMG_UINT64 PMR_BASE_T;
typedef IMG_UINT64 PMR_SIZE_T;
#define PMR_SIZE_FMTSPEC "0x%010llX"
#define PMR_VALUE32_FMTSPEC "0x%08X"
#define PMR_VALUE64_FMTSPEC "0x%016llX"
typedef IMG_UINT32 PMR_LOG2ALIGN_T;
typedef IMG_UINT64 PMR_PASSWORD_T;

typedef struct _PMR_ PMR;
typedef struct _PMR_EXPORT_ PMR_EXPORT;

typedef struct _PMR_PAGELIST_ PMR_PAGELIST;

struct _PVRSRV_DEVICE_NODE_;

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
 *
 * Note also that the implementation may supply pszPDumpFlavour and
 * pszPDumpDefaultMemspaceName, which are irrelevant where the PMR
 * implementation overrides the default symbolic name construction
 * routine.  Where the function pointer for PDump symbolic name
 * derivation is not overridden (i.e. IMG_NULL appears in the relevant
 * entry of the functab) and default implementation shall be used
 * which will copy the PDumpDefaultMemspaceName into the namespace
 * argument, and create the symbolic name by concatenating the
 * "PDumpFlavour" and a numeric representation of the PMR's serial
 * number.
 *
 * The implementation must guarantee that the storage for these two
 * strings, and the function table, are maintained, as copies are not
 * made, the pointer is simply stored.
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
 *      function, in which case IMG_NULL may be supplied.
 *
 * pfnUnlockPhysAddresses
 *
 *      The reverse of pfnLockPhysAddresses.  Note that this should be
 *      IMG_NULL if and only if pfnLockPhysAddresses is IMG_NULL
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
 * pfnPDumpSymbolicAddr
 *
 *      Derives the PDump symbolic address for the given offset.  The
 *      default implementation will copy the PDumpDefaultMemspaceName
 *      into the namespace argument (or use SYSMEM if none was
 *      supplied), and create the symbolic name by concatenating the
 *      "PDumpFlavour" and a numeric representation of the PMR's
 *      serial number.
 *
 * pfnFinalize
 *
 *      Called when the PMR's refcount reaches zero and it gets
 *      destroyed.  This allows the implementation to free up any
 *      resource acquired during creation time.
 *
 */
extern PVRSRV_ERROR
PMRCreatePMR(PHYS_HEAP *psPhysHeap,
             PMR_SIZE_T uiLogicalSize,
             PMR_SIZE_T uiChunkSize,
             IMG_UINT32 ui32NumPhysChunks,
             IMG_UINT32 ui32NumVirtChunks,
             IMG_BOOL *pabMappingTable,
             PMR_LOG2ALIGN_T uiLog2ContiguityGuarantee,
             PMR_FLAGS_T uiFlags,
             const IMG_CHAR *pszPDumpFlavour,
             const PMR_IMPL_FUNCTAB *psFuncTab,
             PMR_IMPL_PRIVDATA pvPrivData,
             PMR **ppsPMRPtr,
             IMG_HANDLE *phPDumpAllocInfo,
             IMG_BOOL bForcePersistent);

/*
 * PMRLockSysPhysAddresses()
 *
 * Calls the relevant callback to lock down the system physical addresses of the memory that makes up the whole PMR.
 *
 * Before this call, it is not valid to use any of the information
 * getting APIs: PMR_Flags(), PMR_SysPhysAddr(),
 * PMR_PDumpSymbolicAddr() [ see note below about lock/unlock
 * semantics ]
 *
 * The caller of this function does not have to care about how the PMR
 * is implemented.  He only has to know that he is allowed access to
 * the physical addresses _after_ calling this function and _until_
 * calling PMRUnlockSysPhysAddresses().
 *
 *
 * Notes to callback implementers (authors of PMR Factories):
 *
 * Some PMR implementations will be such that the physical memory
 * exists for the lifetime of the PMR, with a static address, (and
 * normally flags and symbolic address are static too) and so it is
 * legal for a PMR implementation to not provide an implementation for
 * the lock callback.
 *
 * Some PMR implementation may wish to page memory in from secondary
 * storage on demand.  The lock/unlock callbacks _may_ be the place to
 * do this.  (more likely, there would be a separate API for doing
 * this, but this API provides a useful place to assert that it has
 * been done)
 */

extern PVRSRV_ERROR
PMRLockSysPhysAddresses(PMR *psPMR,
                        IMG_UINT32 uiLog2DevPageSize);

extern PVRSRV_ERROR
PMRLockSysPhysAddressesNested(PMR *psPMR,
                        IMG_UINT32 uiLog2DevPageSize,
                        IMG_UINT32 ui32NestingLevel);

/*
 * PMRUnlockSysPhysAddresses()
 *
 * the reverse of PMRLockSysPhysAddresses()
 */
extern PVRSRV_ERROR
PMRUnlockSysPhysAddresses(PMR *psPMR);

IMG_VOID PMRLock(void);
IMG_VOID PMRUnlock(void);
IMG_BOOL PMRIsLocked(void);
IMG_BOOL PMRIsLockedByMe(void);

/*
 * PhysmemPMRExport()
 *
 * Given a PMR, creates a PMR "Export", which is a handle that
 * provides sufficient data to be able to "import" this PMR elsewhere.
 * The PMR Export is an object in its own right, whose existance
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
extern PVRSRV_ERROR
PMRExportPMR(PMR *psPMR,
             PMR_EXPORT **ppsPMRExport,
             PMR_SIZE_T *puiSize,
             PMR_LOG2ALIGN_T *puiLog2Contig,
             PMR_PASSWORD_T *puiPassword);

/*
 * PMRMakeServerExportClientExport()
 * 
 * This is a "special case" function for making a server export cookie
 * which went through the direct bridge into an export cookie that can
 * be passed through the client bridge.
 */
PVRSRV_ERROR
PMRMakeServerExportClientExport(DEVMEM_EXPORTCOOKIE *psPMRExportIn,
								PMR_EXPORT **ppsPMRExportPtr,
								PMR_SIZE_T *puiSize,
								PMR_LOG2ALIGN_T *puiLog2Contig,
								PMR_PASSWORD_T *puiPassword);

PVRSRV_ERROR
PMRUnmakeServerExportClientExport(PMR_EXPORT *psPMRExport);

/*
 * PMRUnexporPMRt()
 *
 * The reverse of PMRExportPMR().  This causes the PMR to no
 * longer be exported.  If the PMR has already been imported, the
 * imported PMR reference will still be valid, but no further imports
 * will be possible.
 */
extern PVRSRV_ERROR
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
extern PVRSRV_ERROR
PMRImportPMR(PMR_EXPORT *psPMRExport,
             PMR_PASSWORD_T uiPassword,
             PMR_SIZE_T uiSize,
             PMR_LOG2ALIGN_T uiLog2Contig,
             PMR **ppsPMR);

/*
 * PMRUnimportPMR()
 *
 * releases the reference on the PMR as obtained by PMRImportPMR()
 */
extern PVRSRV_ERROR
PMRUnimportPMR(PMR *psPMR);

PVRSRV_ERROR
PMRLocalImportPMR(PMR *psPMR,
				  PMR **ppsPMR,
				  IMG_DEVMEM_SIZE_T *puiSize,
				  IMG_DEVMEM_ALIGN_T *puiAlign);

/*
 * Equivalent mapping functions when in kernel mode - TOOD: should
 * unify this and the PMRAcquireMMapArgs API with a suitable
 * abstraction
 */
extern PVRSRV_ERROR
PMRAcquireKernelMappingData(PMR *psPMR,
                            IMG_SIZE_T uiLogicalOffset,
                            IMG_SIZE_T uiSize,
                            IMG_VOID **ppvKernelAddressOut,
                            IMG_SIZE_T *puiLengthOut,
                            IMG_HANDLE *phPrivOut);

extern PVRSRV_ERROR
PMRAcquireSparseKernelMappingData(PMR *psPMR,
                                  IMG_SIZE_T uiLogicalOffset,
                                  IMG_SIZE_T uiSize,
                                  IMG_VOID **ppvKernelAddressOut,
                                  IMG_SIZE_T *puiLengthOut,
                                  IMG_HANDLE *phPrivOut);

extern PVRSRV_ERROR
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
extern PVRSRV_ERROR
PMR_ReadBytes(PMR *psPMR,
              IMG_DEVMEM_OFFSET_T uiLogicalOffset,
              IMG_UINT8 *pcBuffer,
              IMG_SIZE_T uiBufSz,
              IMG_SIZE_T *puiNumBytes);

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
extern PVRSRV_ERROR
PMR_WriteBytes(PMR *psPMR,
			   IMG_DEVMEM_OFFSET_T uiLogicalOffset,
               IMG_UINT8 *pcBuffer,
               IMG_SIZE_T uiBufSz,
               IMG_SIZE_T *puiNumBytes);

/*
 * PMRRefPMR()
 *
 * Take a reference on the passed in PMR
 */
extern IMG_VOID
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
extern PVRSRV_ERROR
PMRUnrefPMR(PMR *psPMR);


/*
 * PMR_Flags()
 *
 * Flags are static and guaranteed for the life of the PMR.  Thus this
 * function is idempotent and acquire/release semantics is not
 * required.
 *
 * Returns the flags as specified on the PMR.  The flags are to be
 * interpreted as mapping permissions
 */
extern PVRSRV_ERROR
PMR_Flags(const PMR *psPMR,
          PMR_FLAGS_T *puiMappingFlags);


extern PVRSRV_ERROR
PMR_LogicalSize(const PMR *psPMR,
				IMG_DEVMEM_SIZE_T *puiLogicalSize);

/*
 * PMR_SysPhysAddr()
 *
 * A note regarding Lock/Unlock semantics
 * ======================================
 *
 * PMR_SysPhysAddr may only be called after PMRLockSysPhysAddresses()
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
extern PVRSRV_ERROR
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
extern PVRSRV_ERROR
PMR_CpuPhysAddr(const PMR *psPMR,
                IMG_UINT32 ui32Log2PageSize,
                IMG_UINT32 ui32NumOfPages,
                IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                IMG_CPU_PHYADDR *psCpuAddrPtr,
                IMG_BOOL *pbValid);

PVRSRV_ERROR
PMRGetUID(PMR *psPMR,
		  IMG_UINT64 *pui64UID);

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
extern PVRSRV_ERROR
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
extern PVRSRV_ERROR
PMRPDumpLoadMemValue32(PMR *psPMR,
			         IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                     IMG_UINT32 ui32Value,
                     PDUMP_FLAGS_T uiPDumpFlags);

/*
 * PMRPDumpLoadMemValue64()
 *
 * writes the current contents of a dword in PMR memory to the pdump
 * script stream. Useful for patching a buffer by simply editing the
 * script output file in ASCII plain text.
 *
 */
extern PVRSRV_ERROR
PMRPDumpLoadMemValue64(PMR *psPMR,
			         IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                     IMG_UINT64 ui64Value,
                     PDUMP_FLAGS_T uiPDumpFlags);

/*
 * PMRPDumpLoadMem()
 *
 * writes the current contents of the PMR memory to the pdump PRM
 * stream, and emits some PDump code to the script stream to LDB said
 * bytes from said file. If bZero is IMG_TRUE then the PDump zero page
 * is used as the source for the LDB.
 *
 */
extern PVRSRV_ERROR
PMRPDumpLoadMem(PMR *psPMR,
                IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                IMG_DEVMEM_SIZE_T uiSize,
                PDUMP_FLAGS_T uiPDumpFlags,
                IMG_BOOL bZero);

/*
 * PMRPDumpSaveToFile()
 *
 * emits some PDump that does an SAB (save bytes) using the PDump
 * symbolic address of the PMR.  Note that this is generally not the
 * preferred way to dump the buffer contents.  There is an equivalent
 * function in devicemem_server.h which also emits SAB but using the
 * virtual address, which is the "right" way to dump the buffer
 * contents to a file.  This function exists just to aid testing by
 * providing a means to dump the PMR directly by symbolic address
 * also.
 */
extern PVRSRV_ERROR
PMRPDumpSaveToFile(const PMR *psPMR,
                   IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                   IMG_DEVMEM_SIZE_T uiSize,
                   IMG_UINT32 uiArraySize,
                   const IMG_CHAR *pszFilename);
#else	/* PDUMP */


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
#pragma inline(PMRPDumpLoadMemValue)
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
#pragma inline(PMRPDumpLoadMemValue)
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
                   const IMG_CHAR *pszFilename)
{
	PVR_UNREFERENCED_PARAMETER(psPMR);
	PVR_UNREFERENCED_PARAMETER(uiLogicalOffset);
	PVR_UNREFERENCED_PARAMETER(uiSize);
	PVR_UNREFERENCED_PARAMETER(uiArraySize);
	PVR_UNREFERENCED_PARAMETER(pszFilename);
	return PVRSRV_OK;
}

#endif	/* PDUMP */

/* This function returns the private data that a pmr subtype
   squirrelled in here. We use the function table pointer as
   "authorization" that this function is being called by the pmr
   subtype implementation.  We can assume (assert) that.  It would be
   a bug in the implementation of the pmr subtype if this assertion
   ever fails. */
extern IMG_VOID *
PMRGetPrivateDataHack(const PMR *psPMR,
                      const PMR_IMPL_FUNCTAB *psFuncTab);

extern PVRSRV_ERROR
PMRZeroingPMR(PMR *psPMR,
				IMG_DEVMEM_LOG2ALIGN_T uiLog2PageSize);

PVRSRV_ERROR
PMRDumpPageList(PMR *psReferencePMR,
					IMG_DEVMEM_LOG2ALIGN_T uiLog2PageSize);

extern PVRSRV_ERROR
PMRWritePMPageList(/* Target PMR, offset, and length */
                   PMR *psPageListPMR,
                   IMG_DEVMEM_OFFSET_T uiTableOffset,
                   IMG_DEVMEM_SIZE_T  uiTableLength,
                   /* Referenced PMR, and "page" granularity */
                   PMR *psReferencePMR,
                   IMG_DEVMEM_LOG2ALIGN_T uiLog2PageSize,
                   PMR_PAGELIST **ppsPageList,
                   IMG_UINT64 *pui64CheckSum);

/* Doesn't actually erase the page list - just releases the appropriate refcounts */
extern PVRSRV_ERROR // should be IMG_VOID, surely
PMRUnwritePMPageList(PMR_PAGELIST *psPageList);

#if defined(PDUMP)
extern PVRSRV_ERROR
PMRPDumpPol32(const PMR *psPMR,
              IMG_DEVMEM_OFFSET_T uiLogicalOffset,
              IMG_UINT32 ui32Value,
              IMG_UINT32 ui32Mask,
              PDUMP_POLL_OPERATOR eOperator,
              PDUMP_FLAGS_T uiFlags);

extern PVRSRV_ERROR
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
/*
 * PMRInit()
 *
 * To be called once and only once to initialise the internal data in
 * the PMR module (mutexes and such)
 *
 * Not for general use.  Only PVRSRVInit(); should be calling this.
 */
extern PVRSRV_ERROR
PMRInit(IMG_VOID);

/*
 * PMRDeInit()
 *
 * To be called once and only once to deinitialise the internal data in
 * the PMR module (mutexes and such) and for debug checks
 *
 * Not for general use.  Only PVRSRVDeInit(); should be calling this.
 */
extern PVRSRV_ERROR
PMRDeInit(IMG_VOID);

#if defined(PVR_RI_DEBUG)
extern PVRSRV_ERROR
PMRStoreRIHandle(PMR *psPMR,
				 IMG_PVOID hRIHandle);
#endif

#endif /* #ifdef _SRVSRV_PMR_H_ */

