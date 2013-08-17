/*************************************************************************/ /*!
@Title          Linux mmap interface declaration
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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

#if !defined(__MMAP_H__)
#define __MMAP_H__

#include <linux/mm.h>
#include <linux/list.h>

#if defined(VM_MIXEDMAP)
/*
 * Mixed maps allow us to avoid using raw PFN mappings (VM_PFNMAP) for
 * pages without pages structures ("struct page"), giving us more
 * freedom in choosing the mmap offset for mappings.  Mixed maps also
 * allow both the mmap and the wrap code to be simplified somewhat.
 */
#define	PVR_MAKE_ALL_PFNS_SPECIAL
#endif

#include "perproc.h"
#include "mm.h"

/*
 * This structure represents the relationship between an mmap2 file
 * offset and a LinuxMemArea for a given process.
 */
typedef struct KV_OFFSET_STRUCT_TAG
{
    /*
     * Mapping count.  Incremented when the mapping is created, and
     * if the mapping is inherited across a process fork.
     */
    IMG_UINT32			ui32Mapped;

    /*
     * Offset to be passed to mmap2 to map the associated memory area
     * into user space.  The offset may represent the page frame number
     * of the first page in the area (if the area is physically
     * contiguous), or it may represent the secure handle associated
     * with the area.
     */
    IMG_UINTPTR_T       uiMMapOffset;

    IMG_SIZE_T			uiRealByteSize;

    /* Memory area associated with this offset structure */
    LinuxMemArea                *psLinuxMemArea;
    
#if !defined(PVR_MAKE_ALL_PFNS_SPECIAL)
    /* ID of the thread that owns this structure */
    IMG_UINT32			ui32TID;
#endif

    /* ID of the process that owns this structure */
    IMG_UINT32			ui32PID;

    /*
     * For offsets that represent actual page frame numbers, this structure
     * is temporarily put on a list so that it can be found from the
     * driver mmap entry point.  This flag indicates the structure is
     * on the list.
     */
    IMG_BOOL			bOnMMapList;

    /* Reference count for this structure */
    IMG_UINT32			ui32RefCount;

    /*
     * User mode address of start of mapping.  This is not necessarily the
     * first user mode address of the memory area.
     */
    IMG_UINTPTR_T		uiUserVAddr;

    /* Extra entries to support proc filesystem debug info */
#if defined(DEBUG_LINUX_MMAP_AREAS)
    const IMG_CHAR		*pszName;
#endif
    
   /* List entry field for MMap list */
   struct list_head		sMMapItem;

   /* List entry field for per-memory area list */
   struct list_head		sAreaItem;
}KV_OFFSET_STRUCT, *PKV_OFFSET_STRUCT;



/*!
 *******************************************************************************
 * @Function Mmap initialisation code
 ******************************************************************************/
IMG_VOID PVRMMapInit(IMG_VOID);


/*!
 *******************************************************************************
 * @Function Mmap de-initialisation code
 ******************************************************************************/
IMG_VOID PVRMMapCleanup(IMG_VOID);


/*!
 *******************************************************************************
 * @Function Registers a memory area with the mmap code
 *          
 * @Input psLinuxMemArea
 *
 * @Return PVRSRV_ERROR status
 ******************************************************************************/
PVRSRV_ERROR PVRMMapRegisterArea(LinuxMemArea *psLinuxMemArea);


/*!
 *******************************************************************************
 * @Function Unregisters a memory area from the mmap code
 *
 * @Input psLinuxMemArea
 *
 * @Return PVRSRV_ERROR status
 ******************************************************************************/
PVRSRV_ERROR PVRMMapRemoveRegisteredArea(LinuxMemArea *psLinuxMemArea);


/*!
 ******************************************************************************
 * @Function When a userspace services client, requests to map a memory
 *           area to userspace, this function validates the request and
 *           returns the details that the client must use when calling mmap(2).
 *
 * @Input psPerProc		Per process data.
 * @Input hMHandle              Handle associated with the memory to map.
 *				This is a (secure) handle to the OS specific
 *				memory handle structure (hOSMemHandle), or
 *				a handle to a structure that contains the 
 *				memory handle.
 * @Output pui32MMapOffset      The page aligned offset that the client must
 *				pass to the mmap2 system call.
 * @Output pui32ByteOffset       The real mapping that will be created for the
 *				services client may have a different
 *				size/alignment from it request. This offset
 *				is returned to the client and should be added
 *				to virtual address returned from mmap2 to get
 *				the first address corresponding to its request.
 * @Output pui32RealByteOffset   The size that the mapping will really be,
 *				that the client must also pass to mmap/munmap.
 *
 * @Output pui32UserVAddr	Pointer to returned user mode address of 
 * 				mapping.
 * @Return PVRSRV_ERROR
 ******************************************************************************/
PVRSRV_ERROR PVRMMapOSMemHandleToMMapData(PVRSRV_PER_PROCESS_DATA *psPerProc,
                                          IMG_HANDLE hMHandle,
                                          IMG_UINTPTR_T *puiMMapOffset,
                                          IMG_UINTPTR_T *puiByteOffset,
                                          IMG_SIZE_T *puiRealByteSize,
                                          IMG_UINTPTR_T *puiUserVAddr);

/*!
 *******************************************************************************

 @Function Release mmap data.

 @Input psPerProc            Per-process data.
 @Input hMHandle             Memory handle.

 @Output pbMUnmap            Flag that indicates whether an munmap is
		             required.
 @Output pui32RealByteSize   Location for size of mapping.
 @Output pui32UserVAddr      User mode address to munmap.

 @Return PVRSRV_ERROR
 ******************************************************************************/
PVRSRV_ERROR
PVRMMapReleaseMMapData(PVRSRV_PER_PROCESS_DATA *psPerProc,
				IMG_HANDLE hMHandle,
				IMG_BOOL *pbMUnmap,
				IMG_SIZE_T *puiRealByteSize,
                IMG_UINTPTR_T *puiUserVAddr);

/*!
 *******************************************************************************
 * @Function driver mmap entry point
 * 
 * @Input pFile : user file structure
 *
 * @Input ps_vma : vm area structure
 * 
 * @Return 0 for success, -errno for failure.
 ******************************************************************************/
int PVRMMap(struct file* pFile, struct vm_area_struct* ps_vma);


#endif	/* __MMAP_H__ */

