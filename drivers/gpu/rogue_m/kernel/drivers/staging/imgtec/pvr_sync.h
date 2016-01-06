/*************************************************************************/ /*!
@File           pvr_sync.h
@Title          Kernel driver for Android's sync mechanism
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

#ifndef _PVR_SYNC_H
#define _PVR_SYNC_H

#include "pvr_fd_sync_kernel.h"
#include "rgx_fwif_shared.h"

/* Services internal interface */
enum PVRSRV_ERROR pvr_sync_init(void);
void pvr_sync_deinit(void);

struct pvr_sync_append_data;

enum PVRSRV_ERROR
pvr_sync_append_fences(
	const char			*name,
	const u32			nr_check_fences,
	const s32			*check_fence_fds,
	const s32                       update_fence_fd,
	const u32			nr_updates,
	const PRGXFWIF_UFO_ADDR		*update_ufo_addresses,
	const u32			*update_values,
	const u32			nr_checks,
	const PRGXFWIF_UFO_ADDR		*check_ufo_addresses,
	const u32			*check_values,
	struct pvr_sync_append_data	**append_sync_data);

void pvr_sync_get_updates(const struct pvr_sync_append_data *sync_data,
	u32 *nr_fences,
	PRGXFWIF_UFO_ADDR **ufo_addrs,
	u32 **values);
void pvr_sync_get_checks(const struct pvr_sync_append_data *sync_data,
	u32 *nr_fences,
	PRGXFWIF_UFO_ADDR **ufo_addrs,
	u32 **values);

void pvr_sync_rollback_append_fences(
	struct pvr_sync_append_data	*sync_check_data);
void pvr_sync_nohw_complete_fences(
	struct pvr_sync_append_data	*sync_check_data);
void pvr_sync_free_append_fences_data(
	struct pvr_sync_append_data	*sync_check_data);

#endif /* _PVR_SYNC_H */
