/*
 * Copyright (c) 2009, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "psb_pvr_glue.h"

/**
 * FIXME: should NOT use these file under env/linux directly
 */

int psb_get_meminfo_by_handle(void *hKernelMemInfo,
				void **ppsKernelMemInfo)
{
	return -EINVAL;
#if 0
	void *psKernelMemInfo = IMG_NULL;
	PVRSRV_PER_PROCESS_DATA *psPerProc = IMG_NULL;
	PVRSRV_ERROR eError;

	psPerProc = PVRSRVPerProcessData(task_tgid_nr(current));
	eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
				    (IMG_VOID *)&psKernelMemInfo,
				    hKernelMemInfo,
				    PVRSRV_HANDLE_TYPE_MEM_INFO);
	if (eError != PVRSRV_OK) {
		DRM_ERROR("Cannot find kernel meminfo for handle 0x%x\n",
			  (u32)hKernelMemInfo);
		return -EINVAL;
	}

	*ppsKernelMemInfo = psKernelMemInfo;

	DRM_DEBUG("Got Kernel MemInfo for handle %lx\n",
		  (u32)hKernelMemInfo);
	return 0;
#endif
}

int psb_get_pages_by_mem_handle(void *hOSMemHandle, struct page ***pages)
{
	return -EINVAL;
#if 0
	LinuxMemArea *psLinuxMemArea = (LinuxMemArea *)hOSMemHandle;
	struct page **page_list;
	if (psLinuxMemArea->eAreaType != LINUX_MEM_AREA_ALLOC_PAGES) {
		DRM_ERROR("MemArea type is not LINUX_MEM_AREA_ALLOC_PAGES\n");
		return -EINVAL;
	}

	page_list = psLinuxMemArea->uData.sPageList.pvPageList;
	if (!page_list) {
		DRM_DEBUG("Page List is NULL\n");
		return -ENOMEM;
	}

	*pages = page_list;
	return 0;
#endif
}
