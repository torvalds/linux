/*************************************************************************/ /*!
@File
@Title          Device Memory Management core
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Client side part of device memory management -- This
                file defines the exposed Services API to core memory management
                functions.
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

#ifndef PVRSRV_DEVMEM_H
#define PVRSRV_DEVMEM_H

#if defined __cplusplus
extern "C" {
#endif

#include "img_types.h"
#include "devicemem_typedefs.h"
#include "pdumpdefs.h"
#include "pvrsrv_error.h"
#include "pvrsrv_memallocflags.h"
#include "services.h"	/* For PVRSRV_DEV_DATA */
#include "sync_external.h"

/*
  Device memory contexts, heaps and memory descriptors are passed
  through to underlying memory APIs directly, but are to be regarded
  as an opaque handle externally.
*/
typedef DEVMEM_CONTEXT *PVRSRV_DEVMEMCTX;       /*!< Device-Mem Client-Side Interface: Typedef for Context Ptr */
typedef DEVMEM_HEAP *PVRSRV_HEAP;               /*!< Device-Mem Client-Side Interface: Typedef for Heap Ptr */
typedef DEVMEM_MEMDESC *PVRSRV_MEMDESC;         /*!< Device-Mem Client-Side Interface: Typedef for Memory Descriptor Ptr */
typedef DEVMEM_EXPORTCOOKIE PVRSRV_DEVMEM_EXPORTCOOKIE;     /*!< Device-Mem Client-Side Interface: Typedef for Export Cookie */
typedef DEVMEM_FLAGS_T PVRSRV_MEMMAP_FLAGS_T;               /*!< Device-Mem Client-Side Interface: Typedef for Memory-Mapping Flags Enum */
typedef DEVMEM_SERVER_EXPORTCOOKIE PVRSRV_DEVMEM_SERVER_EXPORTCOOKIE;   /*!< Device-Mem Client-Side Interface: Typedef for Server Export Cookie */

/* N.B.  Flags are now defined in pvrsrv_memallocflags.h as they need
         to be omnipresent. */

/*
 *
 *  API functions
 *
 */

/**************************************************************************/ /*!
@Function       PVRSRVCreateDeviceMemContext
@Description    Creates a device memory context.  There is a one-to-one
                correspondence between this context data structure and the top
                level MMU page table (known as the Page Catalogue, in the case of a
                3-tier MMU).  It is intended that a process with its own virtual
                space on the CPU will also have its own virtual space on the GPU.
                Thus there is loosely a one-to-one correspondence between process
                and device memory context, but this is not enforced at this API.
 
                Every process must create the device memory context before any
                memory allocations are made, and is responsible for freeing all
                such allocations before destroying the context
     
                This is a wrapper function above the "bare-metal" device memory
                context creation function which would create just a context and no
                heaps.  This function will also create the heaps, according to the
                heap config that the device specific initialization code has
                nominated for use by this API.
     
                The number of heaps thus created is returned to the caller, such
                that the caller can allocate an array and the call in to fetch
                details of each heap, or look up the heap with the "Find Heap" API
                described below.
     
                In order to derive the details of the MMU configuration for the
                device, and for retrieving the "bridge handle" for communication
                internally in services, is is necessary to pass in the
                PVRSRV_DEV_DATA object as populated with a prior call to
                PVRSRVAcquireDeviceData()
@Input          psDev           dev data
@Output         phCtxOut        On success, the returned DevMem Context. The
                                caller is responsible for providing storage
                                for this.
@Return         PVRSRV_ERROR:   PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
*/ /***************************************************************************/
extern IMG_IMPORT PVRSRV_ERROR
PVRSRVCreateDeviceMemContext(const PVRSRV_DEV_DATA *psDev,
                              PVRSRV_DEVMEMCTX *phCtxOut);

/**************************************************************************/ /*!
@Function       PVRSRVDestroyDeviceMemContext
@Description    Destroy cannot fail.  Well.  It shouldn't, assuming the caller
                has obeyed the protocol, i.e. has freed all his allocations 
                beforehand.
@Input          hCtx            Handle to a DevMem Context
@Return         None
*/ /***************************************************************************/
extern IMG_IMPORT IMG_VOID
PVRSRVDestroyDeviceMemContext(PVRSRV_DEVMEMCTX hCtx);

/**************************************************************************/ /*!
@Function       PVRSRVFindHeapByName
@Description    Returns the heap handle for the named heap which is assumed to
                exist in this context. PVRSRV_HEAP *phHeapOut,  

                N.B.  No need for acquire/release semantics here, as when using
                this wrapper layer, the heaps are automatically instantiated at
                context creation time and destroyed when the context is 
                destroyed.

                The caller is required to know the heap names already as these 
                will vary from device to device and from purpose to purpose.
@Input          hCtx            Handle to a DevMem Context
@Input          pszHeapName     Name of the heap to look for
@Output         phHeapOut       a handle to the heap, for use in future calls 
                                to OpenAllocation / AllocDeviceMemory / Map 
                                DeviceClassMemory, etc. (The PVRSRV_HEAP type
                                to be regarded by caller as an opaque, but 
                                strongly typed, handle)
@Return         PVRSRV_ERROR:   PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
*/ /***************************************************************************/
extern PVRSRV_ERROR
PVRSRVFindHeapByName(PVRSRV_DEVMEMCTX hCtx,
                     const IMG_CHAR *pszHeapName,
                     PVRSRV_HEAP *phHeapOut);

/**************************************************************************/ /*!
@Function       PVRSRVDevmemGetHeapBaseDevVAddr
@Description    returns the device virtual address of the base of the heap.
@Input          hHeap           Handle to a Heap
@Output         pDevVAddr       On success, the device virtual address of the
                                base of the heap.
@Return         PVRSRV_ERROR:   PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
*/ /***************************************************************************/
PVRSRV_ERROR
PVRSRVDevmemGetHeapBaseDevVAddr(PVRSRV_HEAP hHeap,
			        IMG_DEV_VIRTADDR *pDevVAddr);

/**************************************************************************/ /*!
@Function       PVRSRVAllocDeviceMem
@Description    Allocate memory from the specified heap, acquiring physical
                memory from OS as we go and mapping this into
                the GPU (mandatorily) and CPU (optionally)

                Size must be a positive integer multiple of alignment, or, to
                put it another way, the uiLog2Align LSBs should all be zero, but
                at least one other bit should not be.

                Caller to take charge of the PVRSRV_MEMDESC (the memory
                descriptor) which is to be regarded as an opaque handle.
@Input          hHeap               Handle to the heap from which memory will be
                                    allocated
@Input          uiSize              Amount of memory to be allocated.
@Input          uiLog2Align         LOG2 of the required alignment
@Input          uiMemAllocFlags     Allocation Flags
@Input          pszText     		Text to describe the allocation
@Output         phMemDescOut        On success, the resulting memory descriptor
@Return         PVRSRV_OK on success. Otherwise, a PVRSRV_ error code
*/ /***************************************************************************/
extern PVRSRV_ERROR
PVRSRVAllocDeviceMem(PVRSRV_HEAP hHeap,
                     IMG_DEVMEM_SIZE_T uiSize,
                     IMG_DEVMEM_LOG2ALIGN_T uiLog2Align,
                     PVRSRV_MEMALLOCFLAGS_T uiMemAllocFlags,
                     IMG_PCHAR pszText,
                     PVRSRV_MEMDESC *phMemDescOut);

/**************************************************************************/ /*!
@Function       PVRSRVFreeDeviceMem
@Description    Free that allocated by PVRSRVAllocDeviceMem (Memory descriptor 
                will be destroyed)
@Input          hMemDesc            Handle to the descriptor of the memory to be
                                    freed
@Return         None
*/ /***************************************************************************/
extern IMG_VOID
PVRSRVFreeDeviceMem(PVRSRV_MEMDESC hMemDesc);

/**************************************************************************/ /*!
@Function       PVRSRVAcquireCPUMapping
@Description    Causes the allocation referenced by this memory descriptor to be
                mapped into cpu virtual memory, if it wasn't already, and the
                CPU virtual address returned in the caller-provided location.

                The caller must call PVRSRVReleaseCPUMapping to advise when he
                has finished with the mapping.
@Input          hMemDesc            Handle to the memory descriptor for which a
                                    CPU mapping is required
@Output         ppvCpuVirtAddrOut   On success, the caller's ptr is set to the
                                    new CPU mapping
@Return         PVRSRV_ERROR:       PVRSRV_OK on success. Otherwise, a PVRSRV_
                                    error code
*/ /***************************************************************************/
extern PVRSRV_ERROR
PVRSRVAcquireCPUMapping(PVRSRV_MEMDESC hMemDesc,
                        IMG_VOID **ppvCpuVirtAddrOut);

/**************************************************************************/ /*!
@Function       PVRSRVReleaseCPUMapping
@Description    Relinquishes the cpu mapping acquired with 
                PVRSRVAcquireCPUMapping()
@Input          hMemDesc            Handle of the memory descriptor
@Return         None
*/ /***************************************************************************/
extern IMG_VOID
PVRSRVReleaseCPUMapping(PVRSRV_MEMDESC hMemDesc);


/**************************************************************************/ /*!
@Function       PVRSRVMapToDevice
@Description    Map allocation into the device MMU. This function must only be
                called once, any further calls will return
                PVRSRV_ERROR_DEVICEMEM_ALREADY_MAPPED

                The caller must call PVRSRVReleaseDeviceMapping when they
                are finished with the mapping.

@Input          hMemDesc            Handle of the memory descriptor
@Input          hHeap               Device heap to map the allocation into
@Output         psDevVirtAddrOut    Device virtual address
@Return         PVRSRV_ERROR:       PVRSRV_OK on success. Otherwise, a PVRSRV_
                                    error code
*/ /***************************************************************************/
extern PVRSRV_ERROR
PVRSRVMapToDevice(PVRSRV_MEMDESC hMemDesc,
				  PVRSRV_HEAP hHeap,
				  IMG_DEV_VIRTADDR *psDevVirtAddrOut);

/**************************************************************************/ /*!
@Function       PVRSRVAcquireDeviceMapping
@Description    Acquire a reference on the device mapping the allocation.
                If the allocation wasn't mapped into the device then 
                and the device virtual address returned in the
                PVRSRV_ERROR_DEVICEMEM_NO_MAPPING will be returned as
                PVRSRVMapToDevice must be called first.

                The caller must call PVRSRVReleaseDeviceMapping when they
                are finished with the mapping.
@Input          hMemDesc            Handle to the memory descriptor for which a
                                    device mapping is required
@Output         psDevVirtAddrOut    On success, the caller's ptr is set to the
                                    new device mapping
@Return         PVRSRV_ERROR:       PVRSRV_OK on success. Otherwise, a PVRSRV_
                                    error code
*/ /***************************************************************************/
extern PVRSRV_ERROR
PVRSRVAcquireDeviceMapping(PVRSRV_MEMDESC hMemDesc,
						   IMG_DEV_VIRTADDR *psDevVirtAddrOut);

/**************************************************************************/ /*!
@Function       PVRSRVReleaseDeviceMapping
@Description    Relinquishes the device mapping acquired with
                PVRSRVAcquireDeviceMapping or PVRSRVMapToDevice
@Input          hMemDesc            Handle of the memory descriptor
@Return         None
*/ /***************************************************************************/
extern IMG_VOID
PVRSRVReleaseDeviceMapping(PVRSRV_MEMDESC hMemDesc);

/*************************************************************************/ /*!
@Function       PVRSRVDevmemLocalImport

@Description    Import a PMR that was created with this connection to services.

@Input          hExtHandle              External memory handle

@Input          uiFlags                 Import flags

@Output         phMemDescPtr            Created MemDesc

@Output         puiSizePtr              Size of the created MemDesc

@Return         PVRSRV_OK is succesful
*/
/*****************************************************************************/
PVRSRV_ERROR PVRSRVDevmemLocalImport(const PVRSRV_CONNECTION *psConnection,
									 IMG_HANDLE hExtHandle,
									 PVRSRV_MEMMAP_FLAGS_T uiFlags,
									 PVRSRV_MEMDESC *phMemDescPtr,
									 IMG_DEVMEM_SIZE_T *puiSizePtr);

/*************************************************************************/ /*!
@Function       PVRSRVDevmemGetImportUID

@Description    Get the UID of the import that backs this MemDesc

@Input          hMemDesc                MemDesc

@Return         UID of import
*/
/*****************************************************************************/
PVRSRV_ERROR PVRSRVDevmemGetImportUID(PVRSRV_MEMDESC hMemDesc,
									  IMG_UINT64 *pui64UID);

/**************************************************************************/ /*!
@Function       PVRSRVAllocExportableDevMem
@Description    Allocate memory without mapping into device memory context.  This
                memory is exported and ready to be mapped into the device memory
                context of other processes, or to CPU only with 
                PVRSRVMapMemoryToCPUOnly(). The caller agrees to later call 
                PVRSRVFreeUnmappedExportedMemory(). The caller must give the page
                size of the heap into which this memory may be subsequently 
                mapped, or the largest of such page sizes if it may be mapped 
                into multiple places.  This information is to be communicated in
                the Log2Align field.

                Size must be a positive integer multiple of the page size
@Input          uiLog2Align         Log2 of the alignment required
@Input          uiSize              the amount of memory to be allocated
@Input          uiFlags             Allocation flags
@Input          pszText     		Text to describe the allocation
@Output         hMemDesc
@Return         PVRSRV_OK on success. Otherwise, a PVRSRV_ error code
*/ /***************************************************************************/
PVRSRV_ERROR
PVRSRVAllocExportableDevMem(const PVRSRV_DEV_DATA *psDevData,
							IMG_DEVMEM_SIZE_T uiSize,
							IMG_DEVMEM_LOG2ALIGN_T uiLog2Align,
							PVRSRV_MEMALLOCFLAGS_T uiFlags,
							IMG_PCHAR pszText,
							PVRSRV_MEMDESC *hMemDesc);

/**************************************************************************/ /*!
@Function       PVRSRVAllocSparseDevMem
@Description    Allocate sparse memory without mapping into device memory context.
				Sparse memory is used where you have an allocation that has a
				logical size (i.e. the amount of VM space it will need when
				mapping it into a device) that is larger then the amount of
				physical memory that allocation will use. An example of this
				is a NPOT texture where the twiddling algorithm requires you
				to round the width and height to next POT and so you know there
				will be pages that are never accessed.

				This memory is can to be exported and mapped into the device
				memory context of other processes, or to CPU.

                Size must be a positive integer multiple of the page size
@Input          psDevData           Device to allocation the memory for
@Input          uiSize              The logical size of allocation
@Input          uiChunkSize         The size of the chunk
@Input          ui32NumPhysChunks   The number of physical chunks required
@Input          ui32NumVirtChunks   The number of virtual chunks required
@Input			pabMappingTable		Mapping table
@Input          uiLog2Align         Log2 of the required alignment
@Input          uiFlags             Allocation flags
@Input          pszText     		Text to describe the allocation
@Output         hMemDesc
@Return         PVRSRV_OK on success. Otherwise, a PVRSRV_ error code
*/ /***************************************************************************/
PVRSRV_ERROR
PVRSRVAllocSparseDevMem(const PVRSRV_DEV_DATA *psDevData,
						IMG_DEVMEM_SIZE_T uiSize,
						IMG_DEVMEM_SIZE_T uiChunkSize,
						IMG_UINT32 ui32NumPhysChunks,
						IMG_UINT32 ui32NumVirtChunks,
						IMG_BOOL *pabMappingTable,
						IMG_DEVMEM_LOG2ALIGN_T uiLog2Align,
						DEVMEM_FLAGS_T uiFlags,
						IMG_PCHAR pszText,
						PVRSRV_MEMDESC *hMemDesc);

/**************************************************************************/ /*!
@Function       PVRSRVGetLog2PageSize
@Description    Just call AFTER setting up the connection to the kernel module
                otherwise it will run into an assert.
                Gives the log2 of the page size that is currently utilised by
                devmem.

@Return         The page size
*/ /***************************************************************************/

IMG_UINT32 PVRSRVGetLog2PageSize(void);

/**************************************************************************/ /*!
@Function       PVRSRVExport
@Description    Given a memory allocation allocated with Devmem_Allocate(),
                create a "cookie" that can be passed intact by the caller's own
                choice of secure IPC to another process and used as the argument
                to "map" to map this memory into a heap in the target processes.
                N.B.  This can also be used to map into multiple heaps in one 
                process, though that's not the intention.

                Note, the caller must later call Unexport before freeing the
                memory.
@Input          hMemDesc        handle to the descriptor of the memory to be
                                exported
@Output         phExportCookie  On success, a handle to the exported cookie
@Return         PVRSRV_ERROR:   PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
*/ /***************************************************************************/
PVRSRV_ERROR PVRSRVExportDevMem(PVRSRV_MEMDESC hMemDesc,
						  		PVRSRV_DEVMEM_EXPORTCOOKIE *phExportCookie);

/**************************************************************************/ /*!
@Function DevmemMakeServerExportClientExport
@Description    This is a "special case" function for making a server export 
                cookie which went through the direct bridge into an export 
                cookie that can be passed through the client bridge.
@Input          psConnection        Services connection
@Input          hServerExportCookie server export cookie
@Output         psExportCookie      ptr to export cookie
@Return         PVRSRV_ERROR:       PVRSRV_OK on success. Otherwise, a PVRSRV_
                                    error code
*/ /***************************************************************************/
PVRSRV_ERROR
PVRSRVMakeServerExportClientExport(const PVRSRV_CONNECTION *psConnection,
                                   PVRSRV_DEVMEM_SERVER_EXPORTCOOKIE hServerExportCookie,
                                   PVRSRV_DEVMEM_EXPORTCOOKIE *psExportCookie);

/**************************************************************************/ /*!
@Function DevmemUnmakeServerExportClientExport
@Description    Remove any associated resource from the Make operation
@Input          psConnection        Services connection
@Output         psExportCookie      ptr to export cookie
@Return         PVRSRV_ERROR:       PVRSRV_OK on success. Otherwise, a PVRSRV_
                                    error code
*/ /***************************************************************************/
PVRSRV_ERROR
PVRSRVUnmakeServerExportClientExport(const PVRSRV_CONNECTION *psConnection,
                                   PVRSRV_DEVMEM_EXPORTCOOKIE *psExportCookie);

/**************************************************************************/ /*!
@Function       PVRSRVUnexport
@Description    Undo the export caused by "PVRSRVExport" - note - it doesn't
                actually tear down any mapping made by processes that received
                the export cookie.  It will simply make the cookie null and void
                and prevent further mappings.
@Input          hMemDesc        handle to the descriptor of the memory which
                                will no longer be exported
@Output         phExportCookie  On success, the export cookie provided will be
                                set to null
@Return         PVRSRV_ERROR:   PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
*/ /***************************************************************************/
PVRSRV_ERROR PVRSRVUnexportDevMem(PVRSRV_MEMDESC hMemDesc,
								  PVRSRV_DEVMEM_EXPORTCOOKIE *phExportCookie);

/**************************************************************************/ /*!
@Function       PVRSRVImportDevMem
@Description    Import memory that was previously exported with PVRSRVExport()
                into the current process.

                Note: This call only makes the memory accessible to this
                process, it doesn't map it into the device or CPU.

@Input          psConnection    Connection to services
@Input          phExportCookie  Ptr to the handle of the export-cookie 
                                identifying                          
@Output         phMemDescOut    On Success, a handle to a new memory descriptor
                                representing the memory as mapped into the
                                local process address space.
@Input          uiFlags         Device memory mapping flags                                
@Input          pszText     	Text to describe the import
@Return         PVRSRV_ERROR:   PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
*/ /***************************************************************************/
PVRSRV_ERROR PVRSRVImportDevMem(const PVRSRV_CONNECTION *psConnection,
								PVRSRV_DEVMEM_EXPORTCOOKIE *phExportCookie,
								PVRSRV_MEMMAP_FLAGS_T uiFlags,
								PVRSRV_MEMDESC *phMemDescOut);

#if defined (SUPPORT_EXPORTING_MEMORY_CONTEXT)
/**************************************************************************/ /*!
@Function       PVRSRVExportDevmemContext
@Description    Export a device memory context to another process

@Input          hCtx            Memory context to export                        
@Output         phExport        On Success, a export handle that can be passed
                                to another process and used with 
                                PVRSRVImportDeviceMemContext to import the
                                memory context                            
@Return         PVRSRV_ERROR:   PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
*/ /***************************************************************************/
PVRSRV_ERROR
PVRSRVExportDevmemContext(PVRSRV_DEVMEMCTX hCtx,
						  IMG_HANDLE *phExport);

/**************************************************************************/ /*!
@Function       PVRSRVUnexportDevmemContext
@Description    Unexport an exported device memory context

@Input          psConnection    Services connection
@Input          hExport         Export handle created to be unexported

@Return         None
*/ /***************************************************************************/
IMG_VOID
PVRSRVUnexportDevmemContext(PVRSRV_CONNECTION *psConnection,
							IMG_HANDLE hExport);

/**************************************************************************/ /*!
@Function       PVRSRVImportDeviceMemContext
@Description    Import an exported device memory context

                Note: The memory context created with this function is not
                complete and can only be used with debugger related functions

@Input          psConnection    Services connection
@Input          hExport         Export handle to import
@Output         phCtxOut        Device memory context

@Return         None
*/ /***************************************************************************/
PVRSRV_ERROR
PVRSRVImportDeviceMemContext(PVRSRV_CONNECTION *psConnection,
							 IMG_HANDLE hExport,
							 PVRSRV_DEVMEMCTX *phCtxOut);

#endif /* SUPPORT_EXPORTING_MEMORY_CONTEXT */
#if defined __cplusplus
};
#endif
#endif /* PVRSRV_DEVMEM_H */

