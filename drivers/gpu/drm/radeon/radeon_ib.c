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

#include <linux/debugfs.h>

#include <drm/drm_file.h>

#include "radeon.h"

/*
 * IB
 * IBs (Indirect Buffers) and areas of GPU accessible memory where
 * commands are stored.  You can put a pointer to the IB in the
 * command ring and the hw will fetch the commands from the IB
 * and execute them.  Generally userspace acceleration drivers
 * produce command buffers which are send to the kernel and
 * put in IBs for execution by the requested ring.
 */
static void radeon_debugfs_sa_init(struct radeon_device *rdev);

/**
 * radeon_ib_get - request an IB (Indirect Buffer)
 *
 * @rdev: radeon_device pointer
 * @ring: ring index the IB is associated with
 * @vm: requested vm
 * @ib: IB object returned
 * @size: requested IB size
 *
 * Request an IB (all asics).  IBs are allocated using the
 * suballocator.
 * Returns 0 on success, error on failure.
 */
int radeon_ib_get(struct radeon_device *rdev, int ring,
		  struct radeon_ib *ib, struct radeon_vm *vm,
		  unsigned size)
{
	int r;

	r = radeon_sa_bo_new(&rdev->ring_tmp_bo, &ib->sa_bo, size, 256);
	if (r) {
		dev_err(rdev->dev, "failed to get a new IB (%d)\n", r);
		return r;
	}

	radeon_sync_create(&ib->sync);

	ib->ring = ring;
	ib->fence = NULL;
	ib->ptr = radeon_sa_bo_cpu_addr(ib->sa_bo);
	ib->vm = vm;
	if (vm) {
		/* ib pool is bound at RADEON_VA_IB_OFFSET in virtual address
		 * space and soffset is the offset inside the pool bo
		 */
		ib->gpu_addr = drm_suballoc_soffset(ib->sa_bo) + RADEON_VA_IB_OFFSET;
	} else {
		ib->gpu_addr = radeon_sa_bo_gpu_addr(ib->sa_bo);
	}
	ib->is_const_ib = false;

	return 0;
}

/**
 * radeon_ib_free - free an IB (Indirect Buffer)
 *
 * @rdev: radeon_device pointer
 * @ib: IB object to free
 *
 * Free an IB (all asics).
 */
void radeon_ib_free(struct radeon_device *rdev, struct radeon_ib *ib)
{
	radeon_sync_free(rdev, &ib->sync, ib->fence);
	radeon_sa_bo_free(&ib->sa_bo, ib->fence);
	radeon_fence_unref(&ib->fence);
}

/**
 * radeon_ib_schedule - schedule an IB (Indirect Buffer) on the ring
 *
 * @rdev: radeon_device pointer
 * @ib: IB object to schedule
 * @const_ib: Const IB to schedule (SI only)
 * @hdp_flush: Whether or not to perform an HDP cache flush
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
int radeon_ib_schedule(struct radeon_device *rdev, struct radeon_ib *ib,
		       struct radeon_ib *const_ib, bool hdp_flush)
{
	struct radeon_ring *ring = &rdev->ring[ib->ring];
	int r = 0;

	if (!ib->length_dw || !ring->ready) {
		/* TODO: Nothings in the ib we should report. */
		dev_err(rdev->dev, "couldn't schedule ib\n");
		return -EINVAL;
	}

	/* 64 dwords should be enough for fence too */
	r = radeon_ring_lock(rdev, ring, 64 + RADEON_NUM_SYNCS * 8);
	if (r) {
		dev_err(rdev->dev, "scheduling IB failed (%d).\n", r);
		return r;
	}

	/* grab a vm id if necessary */
	if (ib->vm) {
		struct radeon_fence *vm_id_fence;
		vm_id_fence = radeon_vm_grab_id(rdev, ib->vm, ib->ring);
		radeon_sync_fence(&ib->sync, vm_id_fence);
	}

	/* sync with other rings */
	r = radeon_sync_rings(rdev, &ib->sync, ib->ring);
	if (r) {
		dev_err(rdev->dev, "failed to sync rings (%d)\n", r);
		radeon_ring_unlock_undo(rdev, ring);
		return r;
	}

	if (ib->vm)
		radeon_vm_flush(rdev, ib->vm, ib->ring,
				ib->sync.last_vm_update);

	if (const_ib) {
		radeon_ring_ib_execute(rdev, const_ib->ring, const_ib);
		radeon_sync_free(rdev, &const_ib->sync, NULL);
	}
	radeon_ring_ib_execute(rdev, ib->ring, ib);
	r = radeon_fence_emit(rdev, &ib->fence, ib->ring);
	if (r) {
		dev_err(rdev->dev, "failed to emit fence for new IB (%d)\n", r);
		radeon_ring_unlock_undo(rdev, ring);
		return r;
	}
	if (const_ib) {
		const_ib->fence = radeon_fence_ref(ib->fence);
	}

	if (ib->vm)
		radeon_vm_fence(rdev, ib->vm, ib->fence);

	radeon_ring_unlock_commit(rdev, ring, hdp_flush);
	return 0;
}

/**
 * radeon_ib_pool_init - Init the IB (Indirect Buffer) pool
 *
 * @rdev: radeon_device pointer
 *
 * Initialize the suballocator to manage a pool of memory
 * for use as IBs (all asics).
 * Returns 0 on success, error on failure.
 */
int radeon_ib_pool_init(struct radeon_device *rdev)
{
	int r;

	if (rdev->ib_pool_ready) {
		return 0;
	}

	if (rdev->family >= CHIP_BONAIRE) {
		r = radeon_sa_bo_manager_init(rdev, &rdev->ring_tmp_bo,
					      RADEON_IB_POOL_SIZE*64*1024, 256,
					      RADEON_GEM_DOMAIN_GTT,
					      RADEON_GEM_GTT_WC);
	} else {
		/* Before CIK, it's better to stick to cacheable GTT due
		 * to the command stream checking
		 */
		r = radeon_sa_bo_manager_init(rdev, &rdev->ring_tmp_bo,
					      RADEON_IB_POOL_SIZE*64*1024, 256,
					      RADEON_GEM_DOMAIN_GTT, 0);
	}
	if (r) {
		return r;
	}

	r = radeon_sa_bo_manager_start(rdev, &rdev->ring_tmp_bo);
	if (r) {
		return r;
	}

	rdev->ib_pool_ready = true;
	radeon_debugfs_sa_init(rdev);
	return 0;
}

/**
 * radeon_ib_pool_fini - Free the IB (Indirect Buffer) pool
 *
 * @rdev: radeon_device pointer
 *
 * Tear down the suballocator managing the pool of memory
 * for use as IBs (all asics).
 */
void radeon_ib_pool_fini(struct radeon_device *rdev)
{
	if (rdev->ib_pool_ready) {
		radeon_sa_bo_manager_suspend(rdev, &rdev->ring_tmp_bo);
		radeon_sa_bo_manager_fini(rdev, &rdev->ring_tmp_bo);
		rdev->ib_pool_ready = false;
	}
}

/**
 * radeon_ib_ring_tests - test IBs on the rings
 *
 * @rdev: radeon_device pointer
 *
 * Test an IB (Indirect Buffer) on each ring.
 * If the test fails, disable the ring.
 * Returns 0 on success, error if the primary GFX ring
 * IB test fails.
 */
int radeon_ib_ring_tests(struct radeon_device *rdev)
{
	unsigned i;
	int r;

	for (i = 0; i < RADEON_NUM_RINGS; ++i) {
		struct radeon_ring *ring = &rdev->ring[i];

		if (!ring->ready)
			continue;

		r = radeon_ib_test(rdev, i, ring);
		if (r) {
			radeon_fence_driver_force_completion(rdev, i);
			ring->ready = false;
			rdev->needs_reset = false;

			if (i == RADEON_RING_TYPE_GFX_INDEX) {
				/* oh, oh, that's really bad */
				DRM_ERROR("radeon: failed testing IB on GFX ring (%d).\n", r);
				rdev->accel_working = false;
				return r;

			} else {
				/* still not good, but we can live with it */
				DRM_ERROR("radeon: failed testing IB on ring %d (%d).\n", i, r);
			}
		}
	}
	return 0;
}

/*
 * Debugfs info
 */
#if defined(CONFIG_DEBUG_FS)

static int radeon_debugfs_sa_info_show(struct seq_file *m, void *unused)
{
	struct radeon_device *rdev = m->private;

	radeon_sa_bo_dump_debug_info(&rdev->ring_tmp_bo, m);

	return 0;

}

DEFINE_SHOW_ATTRIBUTE(radeon_debugfs_sa_info);

#endif

static void radeon_debugfs_sa_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	struct dentry *root = rdev->ddev->primary->debugfs_root;

	debugfs_create_file("radeon_sa_info", 0444, root, rdev,
			    &radeon_debugfs_sa_info_fops);
#endif
}
