/*
 * @File
 * @Title       PVR DRM definitions shared between kernel and user space.
 * @Codingstyle LinuxKernel
 * @Copyright   Copyright (c) Imagination Technologies Ltd. All Rights Reserved
 * @License     Dual MIT/GPLv2
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License Version 2 ("GPL") in which case the provisions
 * of GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms of
 * GPL, and not to allow others to use your version of this file under the terms
 * of the MIT license, indicate your decision by deleting the provisions above
 * and replace them with the notice and other provisions required by GPL as set
 * out in the file called "GPL-COPYING" included in this distribution. If you do
 * not delete the provisions above, a recipient may use your version of this file
 * under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT-COPYING".
 *
 * EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#if !defined(__PVR_DRM_H__)
#define __PVR_DRM_H__

#include <linux/types.h>

#if defined(__KERNEL__)
#include <drm/drm.h>
#else
#include <drm.h>
#endif

/*
 * IMPORTANT:
 * All structures below are designed to be the same size when compiled for 32
 * and/or 64 bit architectures, i.e. there should be no compiler inserted
 * padding. This is achieved by sticking to the following rules:
 * 1) only use fixed width types
 * 2) always naturally align fields by arranging them appropriately and by using
 *    padding fields when necessary
 *
 * These rules should _always_ be followed when modifying or adding new
 * structures to this file.
 */

struct drm_pvr_srvkm_cmd {
	__u32 bridge_id;
	__u32 bridge_func_id;
	__u64 in_data_ptr;
	__u64 out_data_ptr;
	__u32 in_data_size;
	__u32 out_data_size;
};

struct pvr_sync_rename_ioctl_data {
	char szName[32];
};

struct pvr_sw_sync_create_fence_data {
	char name[32];
	__s32 fence;
	__u32 pad;
	__u64 sync_pt_idx;
};

struct pvr_sw_timeline_advance_data {
	__u64 sync_pt_idx;
};

#define PVR_SRVKM_SERVICES_INIT  1
#define PVR_SRVKM_SYNC_INIT 2
struct drm_pvr_srvkm_init_data {
	__u32 init_module;
};

/* Values used to configure the PVRSRV_DEVICE_INIT_MODE tunable (Linux-only) */
#define PVRSRV_LINUX_DEV_INIT_ON_PROBE   1
#define PVRSRV_LINUX_DEV_INIT_ON_OPEN    2
#define PVRSRV_LINUX_DEV_INIT_ON_CONNECT 3

/*
 * DRM command numbers, relative to DRM_COMMAND_BASE.
 * These defines must be prefixed with "DRM_".
 */

/* PVR Services command */
#define DRM_PVR_SRVKM_CMD			0

/* PVR Sync commands */
#define DRM_PVR_SYNC_RENAME_CMD			1
#define DRM_PVR_SYNC_FORCE_SW_ONLY_CMD		2

/* PVR Software Sync commands */
#define DRM_PVR_SW_SYNC_CREATE_FENCE_CMD	3
#define DRM_PVR_SW_SYNC_INC_CMD			4

/* PVR Services Render Device Init command */
#define DRM_PVR_SRVKM_INIT		5

/* These defines must be prefixed with "DRM_IOCTL_". */
#define	DRM_IOCTL_PVR_SRVKM_CMD	\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_PVR_SRVKM_CMD, \
		 struct drm_pvr_srvkm_cmd)

#define DRM_IOCTL_PVR_SYNC_RENAME_CMD \
	DRM_IOW(DRM_COMMAND_BASE + DRM_PVR_SYNC_RENAME_CMD, \
		struct pvr_sync_rename_ioctl_data)

#define DRM_IOCTL_PVR_SYNC_FORCE_SW_ONLY_CMD \
	DRM_IO(DRM_COMMAND_BASE + DRM_PVR_SYNC_FORCE_SW_ONLY_CMD)

#define	DRM_IOCTL_PVR_SW_SYNC_CREATE_FENCE_CMD \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_PVR_SW_SYNC_CREATE_FENCE_CMD, \
		struct pvr_sw_sync_create_fence_data)

#define DRM_IOCTL_PVR_SW_SYNC_INC_CMD \
	DRM_IOR(DRM_COMMAND_BASE + DRM_PVR_SW_SYNC_INC_CMD, \
		struct pvr_sw_timeline_advance_data)

#define DRM_IOCTL_PVR_SRVKM_INIT \
	DRM_IOW(DRM_COMMAND_BASE + DRM_PVR_SRVKM_INIT, \
		struct drm_pvr_srvkm_init_data)

#endif /* defined(__PVR_DRM_H__) */
