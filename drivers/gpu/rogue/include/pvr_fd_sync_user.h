/*************************************************************************/ /*!
@File           pvr_fd_sync_user.h
@Title          Userspace definitions to use the kernel sync driver
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

#ifndef _PVR_FD_SYNC_USER_H_
#define _PVR_FD_SYNC_USER_H_

#include <linux/types.h>
#include <linux/ioctl.h>

#include "pvrsrv_error.h"

#define PVR_SYNC_MAX_QUERY_FENCE_POINTS 14

#define PVR_SYNC_IOC_MAGIC 'W'

#define PVR_SYNC_IOC_CREATE_FENCE \
 _IOWR(PVR_SYNC_IOC_MAGIC, 0, struct pvr_sync_create_fence_ioctl_data)

#define PVR_SYNC_IOC_ENABLE_FENCING \
 _IOWR(PVR_SYNC_IOC_MAGIC, 1, struct pvr_sync_enable_fencing_ioctl_data)

#define PVR_SYNC_IOC_DEBUG_FENCE \
 _IOWR(PVR_SYNC_IOC_MAGIC, 2, struct pvr_sync_debug_fence_ioctl_data)

#define PVR_SYNC_IOC_ALLOC_FENCE \
 _IOWR(PVR_SYNC_IOC_MAGIC, 3, struct pvr_sync_alloc_fence_ioctl_data)

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

struct pvr_sync_debug_sync_data {
	/* Output */
	char				szParentName[32];
	__s32				i32Status;
	__u8				ui8Foreign;
	union
	{
		struct {
			__u32		id;
			__u32		ui32FWAddr;
			__u32		ui32CurrOp;
			__u32		ui32NextOp;
			__u32		ui32TlTaken;
		} s;
		char			szForeignVal[16];
	};
} __attribute__((packed, aligned(8)));

struct pvr_sync_debug_fence_ioctl_data
{
	/* Input */
	int				iFenceFd;

	/* Output */
	char				szName[32];
	__s32				i32Status;
	__u32				ui32NumSyncs;
	struct pvr_sync_debug_sync_data	aPts[PVR_SYNC_MAX_QUERY_FENCE_POINTS];
}
__attribute__((packed, aligned(8)));

PVRSRV_ERROR PVRFDSyncOpen(int *piSyncFd);
PVRSRV_ERROR PVRFDSyncClose(int iSyncFd);

PVRSRV_ERROR PVRFDSyncWaitFence(int iFenceFd);
PVRSRV_ERROR PVRFDSyncCheckFence(int iFenceFd);

PVRSRV_ERROR PVRFDSyncMergeFences(const char *pcszName,
				  int iFenceFd1,
				  int iFenceFd2,
				  int *piNewFenceFd);

PVRSRV_ERROR PVRFDSyncAllocFence(int iSyncFd,
                                 int *piFenceFd,
                                 int *pbTimelineIdle);

PVRSRV_ERROR PVRFDSyncCreateFence(int iSyncFd,
                                  const char *pcszName,
				  int iAllocFenceFd,
				  int *piFenceFd);

PVRSRV_ERROR PVRFDSyncEnableFencing(int iSyncFd,
				    int bFencingEnabled);

PVRSRV_ERROR PVRFDSyncQueryFence(int iSyncFd,
				 int iFenceFd,
				 struct pvr_sync_debug_fence_ioctl_data *psData);

PVRSRV_ERROR PVRFDSyncDumpFence(int iSyncFd,
				int iFenceFd,
				const char *pcszModule,
				const char *pcszFmt, ...)
	__attribute__((format(printf,4,5)));

#endif /* _PVR_FD_SYNC_USER_H_ */
