/*************************************************************************/ /*!
@File           pvr_fd_sync_kernel.h
@Title          Kernel/userspace interface definitions to use the kernel sync
                driver
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
/* vi: set ts=8: */


#ifndef _PVR_FD_SYNC_KERNEL_H_
#define _PVR_FD_SYNC_KERNEL_H_

#include <linux/types.h>
#include <linux/ioctl.h>

#define PVR_SYNC_MAX_QUERY_FENCE_POINTS 14

#define PVR_SYNC_IOC_MAGIC 'W'

#define PVR_SYNC_IOC_CREATE_FENCE \
 _IOWR(PVR_SYNC_IOC_MAGIC, 0, struct pvr_sync_create_fence_ioctl_data)

#define PVR_SYNC_IOC_ENABLE_FENCING \
 _IOW(PVR_SYNC_IOC_MAGIC,  1, struct pvr_sync_enable_fencing_ioctl_data)

#define PVR_SYNC_IOC_ALLOC_FENCE \
 _IOWR(PVR_SYNC_IOC_MAGIC, 3, struct pvr_sync_alloc_fence_ioctl_data)

#define PVR_SYNC_IOC_RENAME \
 _IOWR(PVR_SYNC_IOC_MAGIC, 4, struct pvr_sync_rename_ioctl_data)

#define PVR_SYNC_IOC_FORCE_SW_ONLY \
 _IO(PVR_SYNC_IOC_MAGIC,   5)

#define PVRSYNC_MODNAME "pvr_sync"

struct pvr_sync_alloc_fence_ioctl_data
{
	/* Output */
	int				iFenceFd;
	int				bTimelineIdle;
}
__attribute__((packed, aligned(8)));

struct pvr_sync_create_fence_ioctl_data
{
	/* Input */
	int				iAllocFenceFd;
	char				szName[32];

	/* Output */
	int				iFenceFd;
}
__attribute__((packed, aligned(8)));

struct pvr_sync_enable_fencing_ioctl_data
{
	/* Input */
	int				bFencingEnabled;
}
__attribute__((packed, aligned(8)));

struct pvr_sync_pt_info {
	/* Output */
	__u32 id;
	__u32 ui32FWAddr;
	__u32 ui32CurrOp;
	__u32 ui32NextOp;
	__u32 ui32TlTaken;
} __attribute__((packed, aligned(8)));

struct pvr_sync_rename_ioctl_data
{
	/* Input */
	char szName[32];
} __attribute__((packed, aligned(8)));

#endif /* _PVR_FD_SYNC_KERNEL_H_ */
