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
static int amdgpu_debugfs_ring_init(struct amdgpu_device *adev, struct amdgpu_ring *ring);

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
}

/**
 * amdgpu_ring_backup - Back up the content of a ring
 *
 * @ring: the ring we want to back up
 *
 * Saves all unprocessed commits from a ring, returns the number of dwords saved.
 */
unsigned amdgpu_ring_backup(struct amdgpu_ring *ring,
			    uint32_t **data)
{
	unsigned size, ptr, i;

	*data = NULL;

	if (ring->ring_obj == NULL)
		return 0;

	/* it doesn't make sense to save anything if all fences are signaled */
	if (!amdgpu_fence_count_emitted(ring))
		return 0;

	ptr = le32_to_cpu(*ring->next_rptr_cpu_addr);

	size = ring->wptr + (ring->ring_size / 4);
	size -= ptr;
	size &= ring->ptr_mask;
	if (size == 0)
		return 0;

	/* and then save the content of the ring */
	*data = kmalloc_array(size, sizeof(uint32_t), GFP_KERNEL);
	if (!*data)
		return 0;
	for (i = 0; i < size; ++i) {
		(*data)[i] = ring->ring[ptr++];
		ptr &= ring->ptr_mask;
	}

	return size;
}

/**
 * amdgpu_ring_restore - append saved commands to the ring again
 *
 * @ring: ring to append commands to
 * @size: number of dwords we want to write
 * @data: saved commands
 *
 * Allocates space on the ring and restore the previously saved commands.
 */
int amdgpu_ring_restore(struct amdgpu_ring *ring,
			unsigned size, uint32_t *data)
{
	int i, r;

	if (!size || !data)
		return 0;

	/* restore the saved ring content */
	r = amdgpu_ring_alloc(ring, size);
	if (r)
		return r;

	for (i = 0; i < size; ++i) {
		amdgpu_ring_write(ring, data[i]);
	}

	amdgpu_ring_commit(ring);
	kfree(data);
	return 0;
}

/**
 * amdgpu_ring_init - init driver ring struct.
 *
 * @adev: amdgpu_device pointer
 * @ring: amdgpu_ring structure holding ring information
 * @ring_size: size of the ring
 * @nop: nop packet for this ring
 *
 * Initialize the driver information for the selected ring (all asics).
 * Returns 0 on success, error on failure.
 */
int amdgpu_ring_init(struct amdgpu_device *adev, struct amdgpu_ring *ring,
		     unsigned ring_size, u32 nop, u32 align_mask,
		     struct amdgpu_irq_src *irq_src, unsigned irq_type,
		     enum amdgpu_ring_type ring_type)
{
	u32 rb_bufsz;
	int r;

	if (ring->adev == NULL) {
		if (adev->num_rings >= AMDGPU_MAX_RINGS)
			return -EINVAL;

		ring->adev = adev;
		ring->idx = adev->num_rings++;
		adev->rings[ring->idx] = ring;
		r = amdgpu_fence_driver_init_ring(ring);
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

	r = amdgpu_wb_get(adev, &ring->next_rptr_offs);
	if (r) {
		dev_err(adev->dev, "(%d) ring next_rptr wb alloc failed\n", r);
		return r;
	}
	ring->next_rptr_gpu_addr = adev->wb.gpu_addr + (ring->next_rptr_offs * 4);
	ring->next_rptr_cpu_addr = &adev->wb.wb[ring->next_rptr_offs];
	spin_lock_init(&ring->fence_lock);
	r = amdgpu_fence_driver_start_ring(ring, irq_src, irq_type);
	if (r) {
		dev_err(adev->dev, "failed initializing fences (%d).\n", r);
		return r;
	}

	/* Align ring size */
	rb_bufsz = order_base_2(ring_size / 8);
	ring_size = (1 << (rb_bufsz + 1)) * 4;
	ring->ring_size = ring_size;
	ring->align_mask = align_mask;
	ring->nop = nop;
	ring->type = ring_type;

	/* Allocate ring buffer */
	if (ring->ring_obj == NULL) {
		r = amdgpu_bo_create(adev, ring->ring_size, PAGE_SIZE, true,
				     AMDGPU_GEM_DOMAIN_GTT, 0,
				     NULL, NULL, &ring->ring_obj);
		if (r) {
			dev_err(adev->dev, "(%d) ring create failed\n", r);
			return r;
		}
		r = amdgpu_bo_reserve(ring->ring_obj, false);
		if (unlikely(r != 0))
			return r;
		r = amdgpu_bo_pin(ring->ring_obj, AMDGPU_GEM_DOMAIN_GTT,
					&ring->gpu_addr);
		if (r) {
			amdgpu_bo_unreserve(ring->ring_obj);
			dev_err(adev->dev, "(%d) ring pin failed\n", r);
			return r;
		}
		r = amdgpu_bo_kmap(ring->ring_obj,
				       (void **)&ring->ring);
		amdgpu_bo_unreserve(ring->ring_obj);
		if (r) {
			dev_err(adev->dev, "(%d) ring map failed\n", r);
			return r;
		}
	}
	ring->ptr_mask = (ring->ring_size / 4) - 1;
	ring->max_dw = DIV_ROUND_UP(ring->ring_size / 4,
				    amdgpu_sched_hw_submission);

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
	int r;
	struct amdgpu_bo *ring_obj;

	ring_obj = ring->ring_obj;
	ring->ready = false;
	ring->ring = NULL;
	ring->ring_obj = NULL;

	amdgpu_wb_free(ring->adev, ring->fence_offs);
	amdgpu_wb_free(ring->adev, ring->rptr_offs);
	amdgpu_wb_free(ring->adev, ring->wptr_offs);
	amdgpu_wb_free(ring->adev, ring->next_rptr_offs);

	if (ring_obj) {
		r = amdgpu_bo_reserve(ring_obj, false);
		if (likely(r == 0)) {
			amdgpu_bo_kunmap(ring_obj);
			amdgpu_bo_unpin(ring_obj);
			amdgpu_bo_unreserve(ring_obj);
		}
		amdgpu_bo_unref(&ring_obj);
	}
}

/**
 * amdgpu_ring_from_fence - get ring from fence
 *
 * @f: fence structure
 *
 * Extract the ring a fence belongs to. Handles both scheduler as
 * well as hardware fences.
 */
struct amdgpu_ring *amdgpu_ring_from_fence(struct fence *f)
{
	struct amdgpu_fence *a_fence;
	struct amd_sched_fence *s_fence;

	s_fence = to_amd_sched_fence(f);
	if (s_fence)
		return container_of(s_fence->sched, struct amdgpu_ring, sched);

	a_fence = to_amdgpu_fence(f);
	if (a_fence)
		return a_fence->ring;

	return NULL;
}

/*
 * Debugfs info
 */
#if defined(CONFIG_DEBUG_FS)

static int amdgpu_debugfs_ring_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct amdgpu_device *adev = dev->dev_private;
	int roffset = *(int*)node->info_ent->data;
	struct amdgpu_ring *ring = (void *)(((uint8_t*)adev) + roffset);

	uint32_t rptr, wptr, rptr_next;
	unsigned i;

	wptr = amdgpu_ring_get_wptr(ring);
	seq_printf(m, "wptr: 0x%08x [%5d]\n", wptr, wptr);

	rptr = amdgpu_ring_get_rptr(ring);
	rptr_next = le32_to_cpu(*ring->next_rptr_cpu_addr);

	seq_printf(m, "rptr: 0x%08x [%5d]\n", rptr, rptr);

	seq_printf(m, "driver's copy of the wptr: 0x%08x [%5d]\n",
		   ring->wptr, ring->wptr);

	if (!ring->ready)
		return 0;

	/* print 8 dw before current rptr as often it's the last executed
	 * packet that is the root issue
	 */
	i = (rptr + ring->ptr_mask + 1 - 32) & ring->ptr_mask;
	while (i != rptr) {
		seq_printf(m, "r[%5d]=0x%08x", i, ring->ring[i]);
		if (i == rptr)
			seq_puts(m, " *");
		if (i == rptr_next)
			seq_puts(m, " #");
		seq_puts(m, "\n");
		i = (i + 1) & ring->ptr_mask;
	}
	while (i != wptr) {
		seq_printf(m, "r[%5d]=0x%08x", i, ring->ring[i]);
		if (i == rptr)
			seq_puts(m, " *");
		if (i == rptr_next)
			seq_puts(m, " #");
		seq_puts(m, "\n");
		i = (i + 1) & ring->ptr_mask;
	}
	return 0;
}

/* TODO: clean this up !*/
static int amdgpu_gfx_index = offsetof(struct amdgpu_device, gfx.gfx_ring[0]);
static int cayman_cp1_index = offsetof(struct amdgpu_device, gfx.compute_ring[0]);
static int cayman_cp2_index = offsetof(struct amdgpu_device, gfx.compute_ring[1]);
static int amdgpu_dma1_index = offsetof(struct amdgpu_device, sdma.instance[0].ring);
static int amdgpu_dma2_index = offsetof(struct amdgpu_device, sdma.instance[1].ring);
static int r600_uvd_index = offsetof(struct amdgpu_device, uvd.ring);
static int si_vce1_index = offsetof(struct amdgpu_device, vce.ring[0]);
static int si_vce2_index = offsetof(struct amdgpu_device, vce.ring[1]);

static struct drm_info_list amdgpu_debugfs_ring_info_list[] = {
	{"amdgpu_ring_gfx", amdgpu_debugfs_ring_info, 0, &amdgpu_gfx_index},
	{"amdgpu_ring_cp1", amdgpu_debugfs_ring_info, 0, &cayman_cp1_index},
	{"amdgpu_ring_cp2", amdgpu_debugfs_ring_info, 0, &cayman_cp2_index},
	{"amdgpu_ring_dma1", amdgpu_debugfs_ring_info, 0, &amdgpu_dma1_index},
	{"amdgpu_ring_dma2", amdgpu_debugfs_ring_info, 0, &amdgpu_dma2_index},
	{"amdgpu_ring_uvd", amdgpu_debugfs_ring_info, 0, &r600_uvd_index},
	{"amdgpu_ring_vce1", amdgpu_debugfs_ring_info, 0, &si_vce1_index},
	{"amdgpu_ring_vce2", amdgpu_debugfs_ring_info, 0, &si_vce2_index},
};

#endif

static int amdgpu_debugfs_ring_init(struct amdgpu_device *adev, struct amdgpu_ring *ring)
{
#if defined(CONFIG_DEBUG_FS)
	unsigned i;
	for (i = 0; i < ARRAY_SIZE(amdgpu_debugfs_ring_info_list); ++i) {
		struct drm_info_list *info = &amdgpu_debugfs_ring_info_list[i];
		int roffset = *(int*)amdgpu_debugfs_ring_info_list[i].data;
		struct amdgpu_ring *other = (void *)(((uint8_t*)adev) + roffset);
		unsigned r;

		if (other != ring)
			continue;

		r = amdgpu_debugfs_add_files(adev, info, 1);
		if (r)
			return r;
	}
#endif
	return 0;
}
