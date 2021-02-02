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
#include <linux/uaccess.h>
#include <linux/debugfs.h>

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

/**
 * amdgpu_ring_alloc - allocate space on the ring buffer
 *
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
	ndw = (ndw + ring->funcs->align_mask) & ~ring->funcs->align_mask;

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
		amdgpu_ring_write(ring, ring->funcs->nop);
}

/**
 * amdgpu_ring_generic_pad_ib - pad IB with NOP packets
 *
 * @ring: amdgpu_ring structure holding ring information
 * @ib: IB to add NOP packets to
 *
 * This is the generic pad_ib function for rings except SDMA
 */
void amdgpu_ring_generic_pad_ib(struct amdgpu_ring *ring, struct amdgpu_ib *ib)
{
	while (ib->length_dw & ring->funcs->align_mask)
		ib->ptr[ib->length_dw++] = ring->funcs->nop;
}

/**
 * amdgpu_ring_commit - tell the GPU to execute the new
 * commands on the ring buffer
 *
 * @ring: amdgpu_ring structure holding ring information
 *
 * Update the wptr (write pointer) to tell the GPU to
 * execute new commands on the ring buffer (all asics).
 */
void amdgpu_ring_commit(struct amdgpu_ring *ring)
{
	uint32_t count;

	/* We pad to match fetch size */
	count = ring->funcs->align_mask + 1 -
		(ring->wptr & ring->funcs->align_mask);
	count %= ring->funcs->align_mask + 1;
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
 * @max_dw: maximum number of dw for ring alloc
 * @irq_src: interrupt source to use for this ring
 * @irq_type: interrupt type to use for this ring
 * @hw_prio: ring priority (NORMAL/HIGH)
 *
 * Initialize the driver information for the selected ring (all asics).
 * Returns 0 on success, error on failure.
 */
int amdgpu_ring_init(struct amdgpu_device *adev, struct amdgpu_ring *ring,
		     unsigned int max_dw, struct amdgpu_irq_src *irq_src,
		     unsigned int irq_type, unsigned int hw_prio,
		     atomic_t *sched_score)
{
	int r;
	int sched_hw_submission = amdgpu_sched_hw_submission;
	u32 *num_sched;
	u32 hw_ip;

	/* Set the hw submission limit higher for KIQ because
	 * it's used for a number of gfx/compute tasks by both
	 * KFD and KGD which may have outstanding fences and
	 * it doesn't really use the gpu scheduler anyway;
	 * KIQ tasks get submitted directly to the ring.
	 */
	if (ring->funcs->type == AMDGPU_RING_TYPE_KIQ)
		sched_hw_submission = max(sched_hw_submission, 256);
	else if (ring == &adev->sdma.instance[0].page)
		sched_hw_submission = 256;

	if (ring->adev == NULL) {
		if (adev->num_rings >= AMDGPU_MAX_RINGS)
			return -EINVAL;

		ring->adev = adev;
		ring->idx = adev->num_rings++;
		adev->rings[ring->idx] = ring;
		r = amdgpu_fence_driver_init_ring(ring, sched_hw_submission,
						  sched_score);
		if (r)
			return r;
	}

	r = amdgpu_device_wb_get(adev, &ring->rptr_offs);
	if (r) {
		dev_err(adev->dev, "(%d) ring rptr_offs wb alloc failed\n", r);
		return r;
	}

	r = amdgpu_device_wb_get(adev, &ring->wptr_offs);
	if (r) {
		dev_err(adev->dev, "(%d) ring wptr_offs wb alloc failed\n", r);
		return r;
	}

	r = amdgpu_device_wb_get(adev, &ring->fence_offs);
	if (r) {
		dev_err(adev->dev, "(%d) ring fence_offs wb alloc failed\n", r);
		return r;
	}

	r = amdgpu_device_wb_get(adev, &ring->trail_fence_offs);
	if (r) {
		dev_err(adev->dev,
			"(%d) ring trail_fence_offs wb alloc failed\n", r);
		return r;
	}
	ring->trail_fence_gpu_addr =
		adev->wb.gpu_addr + (ring->trail_fence_offs * 4);
	ring->trail_fence_cpu_addr = &adev->wb.wb[ring->trail_fence_offs];

	r = amdgpu_device_wb_get(adev, &ring->cond_exe_offs);
	if (r) {
		dev_err(adev->dev, "(%d) ring cond_exec_polling wb alloc failed\n", r);
		return r;
	}
	ring->cond_exe_gpu_addr = adev->wb.gpu_addr + (ring->cond_exe_offs * 4);
	ring->cond_exe_cpu_addr = &adev->wb.wb[ring->cond_exe_offs];
	/* always set cond_exec_polling to CONTINUE */
	*ring->cond_exe_cpu_addr = 1;

	r = amdgpu_fence_driver_start_ring(ring, irq_src, irq_type);
	if (r) {
		dev_err(adev->dev, "failed initializing fences (%d).\n", r);
		return r;
	}

	ring->ring_size = roundup_pow_of_two(max_dw * 4 * sched_hw_submission);

	ring->buf_mask = (ring->ring_size / 4) - 1;
	ring->ptr_mask = ring->funcs->support_64bit_ptrs ?
		0xffffffffffffffff : ring->buf_mask;
	/* Allocate ring buffer */
	if (ring->ring_obj == NULL) {
		r = amdgpu_bo_create_kernel(adev, ring->ring_size + ring->funcs->extra_dw, PAGE_SIZE,
					    AMDGPU_GEM_DOMAIN_GTT,
					    &ring->ring_obj,
					    &ring->gpu_addr,
					    (void **)&ring->ring);
		if (r) {
			dev_err(adev->dev, "(%d) ring create failed\n", r);
			return r;
		}
		amdgpu_ring_clear_ring(ring);
	}

	ring->max_dw = max_dw;
	ring->hw_prio = hw_prio;

	if (!ring->no_scheduler) {
		hw_ip = ring->funcs->type;
		num_sched = &adev->gpu_sched[hw_ip][hw_prio].num_scheds;
		adev->gpu_sched[hw_ip][hw_prio].sched[(*num_sched)++] =
			&ring->sched;
	}

	return 0;
}

/**
 * amdgpu_ring_fini - tear down the driver ring struct.
 *
 * @ring: amdgpu_ring structure holding ring information
 *
 * Tear down the driver information for the selected ring (all asics).
 */
void amdgpu_ring_fini(struct amdgpu_ring *ring)
{

	/* Not to finish a ring which is not initialized */
	if (!(ring->adev) || !(ring->adev->rings[ring->idx]))
		return;

	ring->sched.ready = false;

	amdgpu_device_wb_free(ring->adev, ring->rptr_offs);
	amdgpu_device_wb_free(ring->adev, ring->wptr_offs);

	amdgpu_device_wb_free(ring->adev, ring->cond_exe_offs);
	amdgpu_device_wb_free(ring->adev, ring->fence_offs);

	amdgpu_bo_free_kernel(&ring->ring_obj,
			      &ring->gpu_addr,
			      (void **)&ring->ring);

	dma_fence_put(ring->vmid_wait);
	ring->vmid_wait = NULL;
	ring->me = 0;

	ring->adev->rings[ring->idx] = NULL;
}

/**
 * amdgpu_ring_emit_reg_write_reg_wait_helper - ring helper
 *
 * @ring: ring to write to
 * @reg0: register to write
 * @reg1: register to wait on
 * @ref: reference value to write/wait on
 * @mask: mask to wait on
 *
 * Helper for rings that don't support write and wait in a
 * single oneshot packet.
 */
void amdgpu_ring_emit_reg_write_reg_wait_helper(struct amdgpu_ring *ring,
						uint32_t reg0, uint32_t reg1,
						uint32_t ref, uint32_t mask)
{
	amdgpu_ring_emit_wreg(ring, reg0, ref);
	amdgpu_ring_emit_reg_wait(ring, reg1, mask, mask);
}

/**
 * amdgpu_ring_soft_recovery - try to soft recover a ring lockup
 *
 * @ring: ring to try the recovery on
 * @vmid: VMID we try to get going again
 * @fence: timedout fence
 *
 * Tries to get a ring proceeding again when it is stuck.
 */
bool amdgpu_ring_soft_recovery(struct amdgpu_ring *ring, unsigned int vmid,
			       struct dma_fence *fence)
{
	ktime_t deadline = ktime_add_us(ktime_get(), 10000);

	if (amdgpu_sriov_vf(ring->adev) || !ring->funcs->soft_recovery || !fence)
		return false;

	atomic_inc(&ring->adev->gpu_reset_counter);
	while (!dma_fence_is_signaled(fence) &&
	       ktime_to_ns(ktime_sub(deadline, ktime_get())) > 0)
		ring->funcs->soft_recovery(ring, vmid);

	return dma_fence_is_signaled(fence);
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
	struct amdgpu_ring *ring = file_inode(f)->i_private;
	int r, i;
	uint32_t value, result, early[3];

	if (*pos & 3 || size & 3)
		return -EINVAL;

	result = 0;

	if (*pos < 12) {
		early[0] = amdgpu_ring_get_rptr(ring) & ring->buf_mask;
		early[1] = amdgpu_ring_get_wptr(ring) & ring->buf_mask;
		early[2] = ring->wptr & ring->buf_mask;
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
		r = put_user(value, (uint32_t *)buf);
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

int amdgpu_debugfs_ring_init(struct amdgpu_device *adev,
			     struct amdgpu_ring *ring)
{
#if defined(CONFIG_DEBUG_FS)
	struct drm_minor *minor = adev_to_drm(adev)->primary;
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

/**
 * amdgpu_ring_test_helper - tests ring and set sched readiness status
 *
 * @ring: ring to try the recovery on
 *
 * Tests ring and set sched readiness status
 *
 * Returns 0 on success, error on failure.
 */
int amdgpu_ring_test_helper(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	int r;

	r = amdgpu_ring_test_ring(ring);
	if (r)
		DRM_DEV_ERROR(adev->dev, "ring %s test failed (%d)\n",
			      ring->name, r);
	else
		DRM_DEV_DEBUG(adev->dev, "ring test on %s succeeded\n",
			      ring->name);

	ring->sched.ready = !r;
	return r;
}
