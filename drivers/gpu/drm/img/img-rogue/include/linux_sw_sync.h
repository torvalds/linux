/*************************************************************************/ /*!
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

#ifndef _UAPI_LINUX_PVR_SW_SYNC_H
#define _UAPI_LINUX_PVR_SW_SYNC_H

#if defined(SUPPORT_NATIVE_FENCE_SYNC)

#include <linux/types.h>

#include "pvrsrv_sync_km.h"

struct pvr_sw_sync_create_fence_data {
  char name[PVRSRV_SYNC_NAME_LENGTH];
  __s32 fence;
  __u32 pad;
  __u64 sync_pt_idx;
};

struct pvr_sw_timeline_advance_data {
  __u64 sync_pt_idx;
};

#define PVR_SW_SYNC_IOC_MAGIC 'W'
#define PVR_SW_SYNC_IOC_CREATE_FENCE _IOWR(PVR_SW_SYNC_IOC_MAGIC, 0, struct pvr_sw_sync_create_fence_data)
#define PVR_SW_SYNC_IOC_INC _IOR(PVR_SW_SYNC_IOC_MAGIC, 1, struct pvr_sw_timeline_advance_data)

#endif /* defined(SUPPORT_NATIVE_FENCE_SYNC) */
#endif
