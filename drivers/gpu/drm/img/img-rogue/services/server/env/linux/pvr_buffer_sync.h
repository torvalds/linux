/*
 * @File        pvr_buffer_sync.h
 * @Title       PowerVR Linux buffer sync interface
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

#ifndef PVR_BUFFER_SYNC_H
#define PVR_BUFFER_SYNC_H

#include <linux/device.h>
#include <linux/err.h>
#include <linux/types.h>

struct _PMR_;
struct pvr_buffer_sync_context;
struct pvr_buffer_sync_append_data;

/**
 * pvr_buffer_sync_context_create - creates a buffer sync context
 * @dev: Linux device
 * @name: context name (used for debugging)
 *
 * pvr_buffer_sync_context_destroy() should be used to clean up the buffer
 * sync context.
 *
 * Return: A buffer sync context or NULL if it fails for any reason.
 */
struct pvr_buffer_sync_context *
pvr_buffer_sync_context_create(struct device *dev, const char *name);

/**
 * pvr_buffer_sync_context_destroy() - frees a buffer sync context
 * @ctx: buffer sync context
 */
void
pvr_buffer_sync_context_destroy(struct pvr_buffer_sync_context *ctx);

/**
 * pvr_buffer_sync_resolve_and_create_fences() - create checkpoints from
 *                                               buffers
 * @ctx: buffer sync context
 * @sync_checkpoint_ctx: context in which to create sync checkpoints
 * @nr_pmrs: number of buffer objects (PMRs)
 * @pmrs: buffer array
 * @pmr_flags: internal flags
 * @nr_fence_checkpoints_out: returned number of fence sync checkpoints
 * @fence_checkpoints_out: returned array of fence sync checkpoints
 * @update_checkpoint_out: returned update sync checkpoint
 * @data_out: returned buffer sync data
 *
 * After this call, either pvr_buffer_sync_kick_succeeded() or
 * pvr_buffer_sync_kick_failed() must be called.
 *
 * Return: 0 on success or an error code otherwise.
 */
int
pvr_buffer_sync_resolve_and_create_fences(struct pvr_buffer_sync_context *ctx,
					  PSYNC_CHECKPOINT_CONTEXT sync_checkpoint_ctx,
					  u32 nr_pmrs,
					  struct _PMR_ **pmrs,
					  u32 *pmr_flags,
					  u32 *nr_fence_checkpoints_out,
					  PSYNC_CHECKPOINT **fence_checkpoints_out,
					  PSYNC_CHECKPOINT *update_checkpoint_out,
					  struct pvr_buffer_sync_append_data **data_out);

/**
 * pvr_buffer_sync_kick_succeeded() - cleans up after a successful kick
 *                                    operation
 * @data: buffer sync data returned by
 *        pvr_buffer_sync_resolve_and_create_fences()
 *
 * Should only be called following pvr_buffer_sync_resolve_and_create_fences().
 */
void
pvr_buffer_sync_kick_succeeded(struct pvr_buffer_sync_append_data *data);

/**
 * pvr_buffer_sync_kick_failed() - cleans up after a failed kick operation
 * @data: buffer sync data returned by
 *        pvr_buffer_sync_resolve_and_create_fences()
 *
 * Should only be called following pvr_buffer_sync_resolve_and_create_fences().
 */
void
pvr_buffer_sync_kick_failed(struct pvr_buffer_sync_append_data *data);

#endif /* PVR_BUFFER_SYNC_H */
