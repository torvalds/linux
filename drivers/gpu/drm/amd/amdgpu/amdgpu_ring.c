/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
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
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 *          Christian König
 */
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <drm/drmP.h>
#include <drm/amdgpu_drm.h>
#include "amdgpu.h"
#include "atom.h"

/*
 * Rings
 * Most engines on the GPU are fed via ring buffers.  Ring
 * buffers are areas of GPU accessible memory that the host
 * writes commands into and the GPU reads commands out of.
 * There is a rptr (read pointer) that determines where the
 * GPU is currently reading, and a wptr (write pointer)
 * which determines where the host has written.  When the
 * pointers are equal, the ring is idle.  When the host
 * writes commands to the ring buffer, it increments the
 * wptr.  The GPU then starts fetching commands and executes
 * them until the pointers are equal again.
 */
static int amdgpu_debugfs_ring_init(struct amdgpu_device *adev,
				    struct amdgpu_ring *ring);
static void amdgpu_debugfs_ring_fini(struct amdgpu_ring *ring);

/**
 * amdgpu_ring_alloc - allocate space on the ring buffer
 *
 * @adev: amdgpu_device pointer
 * @ring: amdgpu_ring structure holding ring information
 * @ndw: number of dwords to allocate in the ring buffer
 *
 * Allocate @ndw dwords in the ring buffer (all asics).
 * Returns 0 on success, error on failure.
 */
int amdgpu_ring_alloc(struct amdgpu_ring *ring, unsigned ndw)
{
	/* Align requested size with padding so unlock_commit can
	 * pad safely */
	ndw = (ndw + ring->align_mask) & ~ring->align_mask;

	/* Make sure we aren't trying to allocate more space
	 * than the maximum for one submission
	 */
	if (WARN_ON_ONCE(ndw > ring->max_dw))
		return -ENOMEM;

	ring->count_dw = ndw;
	ring->wptr_old = ring->wptr;

	if (ring->funcs->begin_use)
		ring->funcs->begin_use(ring);

	return 0;
}

/** amdgpu_ring_insert_nop - insert NOP packets
 *
 * @ring: amdgpu_ring structure holding ring information
 * @count: the number of NOP packets to insert
 *
 * This is the generic insert_nop function for rings except SDMA
 */
void amdgpu_ring_insert_nop(struct amdgpu_ring *ring, uint32_t count)
{
	int i;

	for (i = 0; i < count; i++)
		amdgpu_ring_write(ring, ring->nop);
}

/** amdgpu_ring_generic_pad_ib - pad IB with NOP packets
 *
 * @ring: amdgpu_ring structure holding ring information
 * @ib: IB to add NOP packets to
 *
 * This is the generic pad_ib function for rings except SDMA
 */
void amdgpu_ring_generic_pad_ib(struct amdgpu_ring *ring, struct amdgpu_ib *ib)
{
	while (ib->length_dw & ring->align_mask)
		ib->ptr[ib->length_dw++] = ring->nop;
}

/**
 * amdgpu_ring_commit - tell the GPU to execute the new
 * commands on the ring buffer
 *
 * @adev: amdgpu_device pointer
 * @ring: amdgpu_ring structure holding ring information
 *
 * Update the wptr (write pointer) to tell the GPU to
 * execute new commands on the ring buffer (all asics).
 */
void amdgpu_ring_commit(struct amdgpu_ring *ring)
{
	uint32_t count;

	/* We pad to match fetch size */
	count = ring->align_mask + 1 - (ring->wptr & ring->align_mask);
	count %= ring->align_mask + 1;
	ring->funcs->insert_nop(ring, count);

	mb();
	amdgpu_ring_set_wptr(ring);

	if (ring->funcs->end_use)
		ring->funcs->end_use(ring);
}

/**
 * amdgpu_ring_undo - reset the wptr
 *
 * @ring: amdgpu_ring structure holding ring information
 *
 * Reset the driver's copy of the wptr (all asics).
 */
void amdgpu_ring_undo(struct amdgpu_ring *ring)
{
	ring->wptr = ring->wptr_old;

	if (ring->funcs->end_use)
		ring->funcs->end_use(ring);
}

/**
 * amdgpu_ring_init - init driver ring struct.
 *
 * @adev: amdgpu_device pointer
 * @ring: amdgpu_ring structure holding ring information
 * @max_ndw: maximum number of dw for ring alloc
 * @nop: nop packet for this ring
 *
 * Initialize the driver information for the selected ring (all asics).
 * Returns 0 on success, error on failure.
 */
int amdgpu_ring_init(struct amdgpu_device *adev, struct amdgpu_ring *ring,
		     unsigned max_dw, u32 nop, u32 align_mask,
		     struct amdgpu_irq_src *irq_src, unsigned irq_type,
		     enum amdgpu_ring_type ring_type)
{
	int r;

	if (ring->adev == NULL) {
		if (adev->num_rings >= AMDGPU_MAX_RINGS)
			return -EINVAL;

		ring->adev = adev;
		ring->idx = adev->num_rings++;
		adev->rings[ring->idx] = ring;
		r = amdgpu_fence_driver_init_ring(ring,
			amdgpu_sched_hw_submission);
		if (r)
			return r;
	}

	r = amdgpu_wb_get(adev, &ring->rptr_offs);
	if (r) {
		dev_err(adev->dev, "(%d) ring rptr_offs wb alloc failed\n", r);
		return r;
	}

	r = amdgpu_wb_get(adev, &ring->wptr_offs);
	if (r) {
		dev_err(adev->dev, "(%d) ring wptr_offs wb alloc failed\n", r);
		return r;
	}

	r = amdgpu_wb_get(adev, &ring->fence_offs);
	if (r) {
		dev_err(adev->dev, "(%d) ring fence_offs wb alloc failed\n", r);
		return r;
	}

	r = amdgpu_wb_get(adev, &ring->cond_exe_offs);
	if (r) {
		dev_err(adev->dev, "(%d) ring cond_exec_polling wb alloc failed\n", r);
		return r;
	}
	ring->cond_exe_gpu_addr = adev->wb.gpu_addr + (ring->cond_exe_offs * 4);
	ring->cond_exe_cpu_addr = &adev->wb.wb[ring->cond_exe_offs];

	r = amdgpu_fence_driver_start_ring(ring, irq_src, irq_type);
	if (r) {
		dev_err(adev->dev, "failed initializing fences (%d).\n", r);
		return r;
	}

	ring->ring_size = roundup_pow_of_two(max_dw * 4 *
					     amdgpu_sched_hw_submission);
	ring->align_mask = align_mask;
	ring->nop = nop;
	ring->type = ring_type;

	/* Allocate ring buffer */
	if (ring->ring_obj == NULL) {
		r = amdgpu_bo_create_kernel(adev, ring->ring_size, PAGE_SIZE,
					    AMDGPU_GEM_DOMAIN_GTT,
					    &ring->ring_obj,
					    &ring->gpu_addr,
					    (void **)&ring->ring);
		if (r) {
			dev_err(adev->dev, "(%d) ring create failed\n", r);
			return r;
		}
		memset((void *)ring->ring, 0, ring->ring_size);
	}
	ring->ptr_mask = (ring->ring_size / 4) - 1;
	ring->max_dw = max_dw;

	if (amdgpu_debugfs_ring_init(adev, ring)) {
		DRM_ERROR("Failed to register debugfs file for rings !\n");
	}
	return 0;
}

/**
 * amdgpu_ring_fini - tear down the driver ring struct.
 *
 * @adev: amdgpu_device pointer
 * @ring: amdgpu_ring structure holding ring information
 *
 * Tear down the driver information for the selected ring (all asics).
 */
void amdgpu_ring_fini(struct amdgpu_ring *ring)
{
	ring->ready = false;

	amdgpu_wb_free(ring->adev, ring->cond_exe_offs);
	amdgpu_wb_free(ring->adev, ring->fence_offs);
	amdgpu_wb_free(ring->adev, ring->rptr_offs);
	amdgpu_wb_free(ring->adev, ring->wptr_offs);

	amdgpu_bo_free_kernel(&ring->ring_obj,
			      &ring->gpu_addr,
			      (void **)&ring->ring);

	amdgpu_debugfs_ring_fini(ring);

	ring->adev->rings[ring->idx] = NULL;
}

/*
 * Debugfs info
 */
#if defined(CONFIG_DEBUG_FS)

/* Layout of file is 12 bytes consisting of
 * - rptr
 * - wptr
 * - driver's copy of wptr
 *
 * followed by n-words of ring data
 */
static ssize_t amdgpu_debugfs_ring_read(struct file *f, char __user *buf,
					size_t size, loff_t *pos)
{
	struct amdgpu_ring *ring = (struct amdgpu_ring*)f->f_inode->i_private;
	int r, i;
	uint32_t value, result, early[3];

	if (*pos & 3 || size & 3)
		return -EINVAL;

	result = 0;

	if (*pos < 12) {
		early[0] = amdgpu_ring_get_rptr(ring);
		early[1] = amdgpu_ring_get_wptr(ring);
		early[2] = ring->wptr;
		for (i = *pos / 4; i < 3 && size; i++) {
			r = put_user(early[i], (uint32_t *)buf);
			if (r)
				return r;
			buf += 4;
			result += 4;
			size -= 4;
			*pos += 4;
		}
	}

	while (size) {
		if (*pos >= (ring->ring_size + 12))
			return result;
			
		value = ring->ring[(*pos - 12)/4];
		r = put_user(value, (uint32_t*)buf);
		if (r)
			return r;
		buf += 4;
		result += 4;
		size -= 4;
		*pos += 4;
	}

	return result;
}

static const struct file_operations amdgpu_debugfs_ring_fops = {
	.owner = THIS_MODULE,
	.read = amdgpu_debugfs_ring_read,
	.llseek = default_llseek
};

#endif

static int amdgpu_debugfs_ring_init(struct amdgpu_device *adev,
				    struct amdgpu_ring *ring)
{
#if defined(CONFIG_DEBUG_FS)
	struct drm_minor *minor = adev->ddev->primary;
	struct dentry *ent, *root = minor->debugfs_root;
	char name[32];

	sprintf(name, "amdgpu_ring_%s", ring->name);

	ent = debugfs_create_file(name,
				  S_IFREG | S_IRUGO, root,
				  ring, &amdgpu_debugfs_ring_fops);
	if (!ent)
		return -ENOMEM;

	i_size_write(ent->d_inode, ring->ring_size + 12);
	ring->ent = ent;
#endif
	return 0;
}

static void amdgpu_debugfs_ring_fini(struct amdgpu_ring *ring)
{
#if defined(CONFIG_DEBUG_FS)
	debugfs_remove(ring->ent);
#endif
}
