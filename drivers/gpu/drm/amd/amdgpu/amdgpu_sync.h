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
struct dma_resv;
struct amdgpu_device;
struct amdgpu_ring;

enum amdgpu_sync_mode {
	AMDGPU_SYNC_ALWAYS,
	AMDGPU_SYNC_NE_OWNER,
	AMDGPU_SYNC_EQ_OWNER,
	AMDGPU_SYNC_EXPLICIT
};

/*
 * Container for fences used to sync command submissions.
 */
struct amdgpu_sync {
	DECLARE_HASHTABLE(fences, 4);
};

void amdgpu_sync_create(struct amdgpu_sync *sync);
int amdgpu_sync_fence(struct amdgpu_sync *sync, struct dma_fence *f);
int amdgpu_sync_resv(struct amdgpu_device *adev, struct amdgpu_sync *sync,
		     struct dma_resv *resv, enum amdgpu_sync_mode mode,
		     void *owner);
struct dma_fence *amdgpu_sync_peek_fence(struct amdgpu_sync *sync,
				     struct amdgpu_ring *ring);
struct dma_fence *amdgpu_sync_get_fence(struct amdgpu_sync *sync);
int amdgpu_sync_clone(struct amdgpu_sync *source, struct amdgpu_sync *clone);
int amdgpu_sync_wait(struct amdgpu_sync *sync, bool intr);
void amdgpu_sync_free(struct amdgpu_sync *sync);
int amdgpu_sync_init(void);
void amdgpu_sync_fini(void);

#endif
