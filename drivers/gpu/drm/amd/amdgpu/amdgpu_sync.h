/*
 * Copyright 2016 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Christian König
 */
#ifndef __AMDGPU_SYNC_H__
#define __AMDGPU_SYNC_H__

#include <linux/hashtable.h>

struct dma_fence;
struct reservation_object;
struct amdgpu_device;
struct amdgpu_ring;

/*
 * Container for fences used to sync command submissions.
 */
struct amdgpu_sync {
	DECLARE_HASHTABLE(fences, 4);
	struct dma_fence	*last_vm_update;
};

void amdgpu_sync_create(struct amdgpu_sync *sync);
int amdgpu_sync_fence(struct amdgpu_device *adev, struct amdgpu_sync *sync,
		      struct dma_fence *f, bool explicit);
int amdgpu_sync_resv(struct amdgpu_device *adev,
		     struct amdgpu_sync *sync,
		     struct reservation_object *resv,
		     void *owner,
		     bool explicit_sync);
struct dma_fence *amdgpu_sync_peek_fence(struct amdgpu_sync *sync,
				     struct amdgpu_ring *ring);
struct dma_fence *amdgpu_sync_get_fence(struct amdgpu_sync *sync, bool *explicit);
int amdgpu_sync_clone(struct amdgpu_sync *source, struct amdgpu_sync *clone);
int amdgpu_sync_wait(struct amdgpu_sync *sync, bool intr);
void amdgpu_sync_free(struct amdgpu_sync *sync);
int amdgpu_sync_init(void);
void amdgpu_sync_fini(void);

#endif
