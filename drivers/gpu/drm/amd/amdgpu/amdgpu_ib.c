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

#define AMDGPU_IB_TEST_TIMEOUT	msecs_to_jiffies(1000)

/*
 * IB
 * IBs (Indirect Buffers) and areas of GPU accessible memory where
 * commands are stored.  You can put a pointer to the IB in the
 * command ring and the hw will fetch the commands from the IB
 * and execute them.  Generally userspace acceleration drivers
 * produce command buffers which are send to the kernel and
 * put in IBs for execution by the requested ring.
 */
static int amdgpu_debugfs_sa_init(struct amdgpu_device *adev);

/**
 * amdgpu_ib_get - request an IB (Indirect Buffer)
 *
 * @ring: ring index the IB is associated with
 * @size: requested IB size
 * @ib: IB object returned
 *
 * Request an IB (all asics).  IBs are allocated using the
 * suballocator.
 * Returns 0 on success, error on failure.
 */
int amdgpu_ib_get(struct amdgpu_device *adev, struct amdgpu_vm *vm,
		  unsigned size, struct amdgpu_ib *ib)
{
	int r;

	if (size) {
		r = amdgpu_sa_bo_new(&adev->ring_tmp_bo,
				      &ib->sa_bo, size, 256);
		if (r) {
			dev_err(adev->dev, "failed to get a new IB (%d)\n", r);
			return r;
		}

		ib->ptr = amdgpu_sa_bo_cpu_addr(ib->sa_bo);

		if (!vm)
			ib->gpu_addr = amdgpu_sa_bo_gpu_addr(ib->sa_bo);
	}

	return 0;
}

/**
 * amdgpu_ib_free - free an IB (Indirect Buffer)
 *
 * @adev: amdgpu_device pointer
 * @ib: IB object to free
 * @f: the fence SA bo need wait on for the ib alloation
 *
 * Free an IB (all asics).
 */
void amdgpu_ib_free(struct amdgpu_device *adev, struct amdgpu_ib *ib,
		    struct dma_fence *f)
{
	amdgpu_sa_bo_free(adev, &ib->sa_bo, f);
}

/**
 * amdgpu_ib_schedule - schedule an IB (Indirect Buffer) on the ring
 *
 * @adev: amdgpu_device pointer
 * @num_ibs: number of IBs to schedule
 * @ibs: IB objects to schedule
 * @f: fence created during this submission
 *
 * Schedule an IB on the associated ring (all asics).
 * Returns 0 on success, error on failure.
 *
 * On SI, there are two parallel engines fed from the primary ring,
 * the CE (Constant Engine) and the DE (Drawing Engine).  Since
 * resource descriptors have moved to memory, the CE allows you to
 * prime the caches while the DE is updating register state so that
 * the resource descriptors will be already in cache when the draw is
 * processed.  To accomplish this, the userspace driver submits two
 * IBs, one for the CE and one for the DE.  If there is a CE IB (called
 * a CONST_IB), it will be put on the ring prior to the DE IB.  Prior
 * to SI there was just a DE IB.
 */
int amdgpu_ib_schedule(struct amdgpu_ring *ring, unsigned num_ibs,
		       struct amdgpu_ib *ibs, struct amdgpu_job *job,
		       struct dma_fence **f)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ib *ib = &ibs[0];
	bool skip_preamble, need_ctx_switch;
	unsigned patch_offset = ~0;
	struct amdgpu_vm *vm;
	uint64_t fence_ctx;
	uint32_t status = 0, alloc_size;

	unsigned i;
	int r = 0;

	if (num_ibs == 0)
		return -EINVAL;

	/* ring tests don't use a job */
	if (job) {
		vm = job->vm;
		fence_ctx = job->fence_ctx;
	} else {
		vm = NULL;
		fence_ctx = 0;
	}

	if (!ring->ready) {
		dev_err(adev->dev, "couldn't schedule ib on ring <%s>\n", ring->name);
		return -EINVAL;
	}

	if (vm && !job->vm_id) {
		dev_err(adev->dev, "VM IB without ID\n");
		return -EINVAL;
	}

	alloc_size = ring->funcs->emit_frame_size + num_ibs *
		ring->funcs->emit_ib_size;

	r = amdgpu_ring_alloc(ring, alloc_size);
	if (r) {
		dev_err(adev->dev, "scheduling IB failed (%d).\n", r);
		return r;
	}

	if (vm) {
		r = amdgpu_vm_flush(ring, job);
		if (r) {
			amdgpu_ring_undo(ring);
			return r;
		}
	}

	if (ring->funcs->init_cond_exec)
		patch_offset = amdgpu_ring_init_cond_exec(ring);

	if (ring->funcs->emit_hdp_flush
#ifdef CONFIG_X86_64
	    && !(adev->flags & AMD_IS_APU)
#endif
	   )
		amdgpu_ring_emit_hdp_flush(ring);

	skip_preamble = ring->current_ctx == fence_ctx;
	need_ctx_switch = ring->current_ctx != fence_ctx;
	if (job && ring->funcs->emit_cntxcntl) {
		if (need_ctx_switch)
			status |= AMDGPU_HAVE_CTX_SWITCH;
		status |= job->preamble_status;

		if (vm)
			status |= AMDGPU_VM_DOMAIN;
		amdgpu_ring_emit_cntxcntl(ring, status);
	}

	for (i = 0; i < num_ibs; ++i) {
		ib = &ibs[i];

		/* drop preamble IBs if we don't have a context switch */
		if ((ib->flags & AMDGPU_IB_FLAG_PREAMBLE) &&
			skip_preamble &&
			!(status & AMDGPU_PREAMBLE_IB_PRESENT_FIRST) &&
			!amdgpu_sriov_vf(adev)) /* for SRIOV preemption, Preamble CE ib must be inserted anyway */
			continue;

		amdgpu_ring_emit_ib(ring, ib, job ? job->vm_id : 0,
				    need_ctx_switch);
		need_ctx_switch = false;
	}

	if (ring->funcs->emit_hdp_invalidate
#ifdef CONFIG_X86_64
	    && !(adev->flags & AMD_IS_APU)
#endif
	   )
		amdgpu_ring_emit_hdp_invalidate(ring);

	r = amdgpu_fence_emit(ring, f);
	if (r) {
		dev_err(adev->dev, "failed to emit fence (%d)\n", r);
		if (job && job->vm_id)
			amdgpu_vm_reset_id(adev, job->vm_id);
		amdgpu_ring_undo(ring);
		return r;
	}

	if (ring->funcs->insert_end)
		ring->funcs->insert_end(ring);

	/* wrap the last IB with fence */
	if (job && job->uf_addr) {
		amdgpu_ring_emit_fence(ring, job->uf_addr, job->uf_sequence,
				       AMDGPU_FENCE_FLAG_64BIT);
	}

	if (patch_offset != ~0 && ring->funcs->patch_cond_exec)
		amdgpu_ring_patch_cond_exec(ring, patch_offset);

	ring->current_ctx = fence_ctx;
	if (vm && ring->funcs->emit_switch_buffer)
		amdgpu_ring_emit_switch_buffer(ring);
	amdgpu_ring_commit(ring);
	return 0;
}

/**
 * amdgpu_ib_pool_init - Init the IB (Indirect Buffer) pool
 *
 * @adev: amdgpu_device pointer
 *
 * Initialize the suballocator to manage a pool of memory
 * for use as IBs (all asics).
 * Returns 0 on success, error on failure.
 */
int amdgpu_ib_pool_init(struct amdgpu_device *adev)
{
	int r;

	if (adev->ib_pool_ready) {
		return 0;
	}
	r = amdgpu_sa_bo_manager_init(adev, &adev->ring_tmp_bo,
				      AMDGPU_IB_POOL_SIZE*64*1024,
				      AMDGPU_GPU_PAGE_SIZE,
				      AMDGPU_GEM_DOMAIN_GTT);
	if (r) {
		return r;
	}

	r = amdgpu_sa_bo_manager_start(adev, &adev->ring_tmp_bo);
	if (r) {
		return r;
	}

	adev->ib_pool_ready = true;
	if (amdgpu_debugfs_sa_init(adev)) {
		dev_err(adev->dev, "failed to register debugfs file for SA\n");
	}
	return 0;
}

/**
 * amdgpu_ib_pool_fini - Free the IB (Indirect Buffer) pool
 *
 * @adev: amdgpu_device pointer
 *
 * Tear down the suballocator managing the pool of memory
 * for use as IBs (all asics).
 */
void amdgpu_ib_pool_fini(struct amdgpu_device *adev)
{
	if (adev->ib_pool_ready) {
		amdgpu_sa_bo_manager_suspend(adev, &adev->ring_tmp_bo);
		amdgpu_sa_bo_manager_fini(adev, &adev->ring_tmp_bo);
		adev->ib_pool_ready = false;
	}
}

/**
 * amdgpu_ib_ring_tests - test IBs on the rings
 *
 * @adev: amdgpu_device pointer
 *
 * Test an IB (Indirect Buffer) on each ring.
 * If the test fails, disable the ring.
 * Returns 0 on success, error if the primary GFX ring
 * IB test fails.
 */
int amdgpu_ib_ring_tests(struct amdgpu_device *adev)
{
	unsigned i;
	int r, ret = 0;

	for (i = 0; i < AMDGPU_MAX_RINGS; ++i) {
		struct amdgpu_ring *ring = adev->rings[i];

		if (!ring || !ring->ready)
			continue;

		r = amdgpu_ring_test_ib(ring, AMDGPU_IB_TEST_TIMEOUT);
		if (r) {
			ring->ready = false;

			if (ring == &adev->gfx.gfx_ring[0]) {
				/* oh, oh, that's really bad */
				DRM_ERROR("amdgpu: failed testing IB on GFX ring (%d).\n", r);
				adev->accel_working = false;
				return r;

			} else {
				/* still not good, but we can live with it */
				DRM_ERROR("amdgpu: failed testing IB on ring %d (%d).\n", i, r);
				ret = r;
			}
		}
	}
	return ret;
}

/*
 * Debugfs info
 */
#if defined(CONFIG_DEBUG_FS)

static int amdgpu_debugfs_sa_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct amdgpu_device *adev = dev->dev_private;

	amdgpu_sa_bo_dump_debug_info(&adev->ring_tmp_bo, m);

	return 0;

}

static const struct drm_info_list amdgpu_debugfs_sa_list[] = {
	{"amdgpu_sa_info", &amdgpu_debugfs_sa_info, 0, NULL},
};

#endif

static int amdgpu_debugfs_sa_init(struct amdgpu_device *adev)
{
#if defined(CONFIG_DEBUG_FS)
	return amdgpu_debugfs_add_files(adev, amdgpu_debugfs_sa_list, 1);
#else
	return 0;
#endif
}
