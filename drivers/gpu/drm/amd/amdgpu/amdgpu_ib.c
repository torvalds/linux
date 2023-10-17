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

#include <drm/amdgpu_drm.h>

#include "amdgpu.h"
#include "atom.h"
#include "amdgpu_trace.h"

#define AMDGPU_IB_TEST_TIMEOUT	msecs_to_jiffies(1000)
#define AMDGPU_IB_TEST_GFX_XGMI_TIMEOUT	msecs_to_jiffies(2000)

/*
 * IB
 * IBs (Indirect Buffers) and areas of GPU accessible memory where
 * commands are stored.  You can put a pointer to the IB in the
 * command ring and the hw will fetch the commands from the IB
 * and execute them.  Generally userspace acceleration drivers
 * produce command buffers which are send to the kernel and
 * put in IBs for execution by the requested ring.
 */

/**
 * amdgpu_ib_get - request an IB (Indirect Buffer)
 *
 * @adev: amdgpu_device pointer
 * @vm: amdgpu_vm pointer
 * @size: requested IB size
 * @pool_type: IB pool type (delayed, immediate, direct)
 * @ib: IB object returned
 *
 * Request an IB (all asics).  IBs are allocated using the
 * suballocator.
 * Returns 0 on success, error on failure.
 */
int amdgpu_ib_get(struct amdgpu_device *adev, struct amdgpu_vm *vm,
		  unsigned int size, enum amdgpu_ib_pool_type pool_type,
		  struct amdgpu_ib *ib)
{
	int r;

	if (size) {
		r = amdgpu_sa_bo_new(&adev->ib_pools[pool_type],
				     &ib->sa_bo, size);
		if (r) {
			dev_err(adev->dev, "failed to get a new IB (%d)\n", r);
			return r;
		}

		ib->ptr = amdgpu_sa_bo_cpu_addr(ib->sa_bo);
		/* flush the cache before commit the IB */
		ib->flags = AMDGPU_IB_FLAG_EMIT_MEM_SYNC;

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
 * @ring: ring index the IB is associated with
 * @num_ibs: number of IBs to schedule
 * @ibs: IB objects to schedule
 * @job: job to schedule
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
int amdgpu_ib_schedule(struct amdgpu_ring *ring, unsigned int num_ibs,
		       struct amdgpu_ib *ibs, struct amdgpu_job *job,
		       struct dma_fence **f)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ib *ib = &ibs[0];
	struct dma_fence *tmp = NULL;
	bool need_ctx_switch;
	unsigned int patch_offset = ~0;
	struct amdgpu_vm *vm;
	uint64_t fence_ctx;
	uint32_t status = 0, alloc_size;
	unsigned int fence_flags = 0;
	bool secure, init_shadow;
	u64 shadow_va, csa_va, gds_va;
	int vmid = AMDGPU_JOB_GET_VMID(job);

	unsigned int i;
	int r = 0;
	bool need_pipe_sync = false;

	if (num_ibs == 0)
		return -EINVAL;

	/* ring tests don't use a job */
	if (job) {
		vm = job->vm;
		fence_ctx = job->base.s_fence ?
			job->base.s_fence->scheduled.context : 0;
		shadow_va = job->shadow_va;
		csa_va = job->csa_va;
		gds_va = job->gds_va;
		init_shadow = job->init_shadow;
	} else {
		vm = NULL;
		fence_ctx = 0;
		shadow_va = 0;
		csa_va = 0;
		gds_va = 0;
		init_shadow = false;
	}

	if (!ring->sched.ready && !ring->is_mes_queue) {
		dev_err(adev->dev, "couldn't schedule ib on ring <%s>\n", ring->name);
		return -EINVAL;
	}

	if (vm && !job->vmid && !ring->is_mes_queue) {
		dev_err(adev->dev, "VM IB without ID\n");
		return -EINVAL;
	}

	if ((ib->flags & AMDGPU_IB_FLAGS_SECURE) &&
	    (!ring->funcs->secure_submission_supported)) {
		dev_err(adev->dev, "secure submissions not supported on ring <%s>\n", ring->name);
		return -EINVAL;
	}

	alloc_size = ring->funcs->emit_frame_size + num_ibs *
		ring->funcs->emit_ib_size;

	r = amdgpu_ring_alloc(ring, alloc_size);
	if (r) {
		dev_err(adev->dev, "scheduling IB failed (%d).\n", r);
		return r;
	}

	need_ctx_switch = ring->current_ctx != fence_ctx;
	if (ring->funcs->emit_pipeline_sync && job &&
	    ((tmp = amdgpu_sync_get_fence(&job->explicit_sync)) ||
	     (amdgpu_sriov_vf(adev) && need_ctx_switch) ||
	     amdgpu_vm_need_pipeline_sync(ring, job))) {
		need_pipe_sync = true;

		if (tmp)
			trace_amdgpu_ib_pipe_sync(job, tmp);

		dma_fence_put(tmp);
	}

	if ((ib->flags & AMDGPU_IB_FLAG_EMIT_MEM_SYNC) && ring->funcs->emit_mem_sync)
		ring->funcs->emit_mem_sync(ring);

	if (ring->funcs->emit_wave_limit &&
	    ring->hw_prio == AMDGPU_GFX_PIPE_PRIO_HIGH)
		ring->funcs->emit_wave_limit(ring, true);

	if (ring->funcs->insert_start)
		ring->funcs->insert_start(ring);

	if (job) {
		r = amdgpu_vm_flush(ring, job, need_pipe_sync);
		if (r) {
			amdgpu_ring_undo(ring);
			return r;
		}
	}

	amdgpu_ring_ib_begin(ring);

	if (ring->funcs->emit_gfx_shadow)
		amdgpu_ring_emit_gfx_shadow(ring, shadow_va, csa_va, gds_va,
					    init_shadow, vmid);

	if (ring->funcs->init_cond_exec)
		patch_offset = amdgpu_ring_init_cond_exec(ring);

	amdgpu_device_flush_hdp(adev, ring);

	if (need_ctx_switch)
		status |= AMDGPU_HAVE_CTX_SWITCH;

	if (job && ring->funcs->emit_cntxcntl) {
		status |= job->preamble_status;
		status |= job->preemption_status;
		amdgpu_ring_emit_cntxcntl(ring, status);
	}

	/* Setup initial TMZiness and send it off.
	 */
	secure = false;
	if (job && ring->funcs->emit_frame_cntl) {
		secure = ib->flags & AMDGPU_IB_FLAGS_SECURE;
		amdgpu_ring_emit_frame_cntl(ring, true, secure);
	}

	for (i = 0; i < num_ibs; ++i) {
		ib = &ibs[i];

		if (job && ring->funcs->emit_frame_cntl) {
			if (secure != !!(ib->flags & AMDGPU_IB_FLAGS_SECURE)) {
				amdgpu_ring_emit_frame_cntl(ring, false, secure);
				secure = !secure;
				amdgpu_ring_emit_frame_cntl(ring, true, secure);
			}
		}

		amdgpu_ring_emit_ib(ring, job, ib, status);
		status &= ~AMDGPU_HAVE_CTX_SWITCH;
	}

	if (job && ring->funcs->emit_frame_cntl)
		amdgpu_ring_emit_frame_cntl(ring, false, secure);

	amdgpu_device_invalidate_hdp(adev, ring);

	if (ib->flags & AMDGPU_IB_FLAG_TC_WB_NOT_INVALIDATE)
		fence_flags |= AMDGPU_FENCE_FLAG_TC_WB_ONLY;

	/* wrap the last IB with fence */
	if (job && job->uf_addr) {
		amdgpu_ring_emit_fence(ring, job->uf_addr, job->uf_sequence,
				       fence_flags | AMDGPU_FENCE_FLAG_64BIT);
	}

	if (ring->funcs->emit_gfx_shadow) {
		amdgpu_ring_emit_gfx_shadow(ring, 0, 0, 0, false, 0);

		if (ring->funcs->init_cond_exec) {
			unsigned int ce_offset = ~0;

			ce_offset = amdgpu_ring_init_cond_exec(ring);
			if (ce_offset != ~0 && ring->funcs->patch_cond_exec)
				amdgpu_ring_patch_cond_exec(ring, ce_offset);
		}
	}

	r = amdgpu_fence_emit(ring, f, job, fence_flags);
	if (r) {
		dev_err(adev->dev, "failed to emit fence (%d)\n", r);
		if (job && job->vmid)
			amdgpu_vmid_reset(adev, ring->vm_hub, job->vmid);
		amdgpu_ring_undo(ring);
		return r;
	}

	if (ring->funcs->insert_end)
		ring->funcs->insert_end(ring);

	if (patch_offset != ~0 && ring->funcs->patch_cond_exec)
		amdgpu_ring_patch_cond_exec(ring, patch_offset);

	ring->current_ctx = fence_ctx;
	if (vm && ring->funcs->emit_switch_buffer)
		amdgpu_ring_emit_switch_buffer(ring);

	if (ring->funcs->emit_wave_limit &&
	    ring->hw_prio == AMDGPU_GFX_PIPE_PRIO_HIGH)
		ring->funcs->emit_wave_limit(ring, false);

	amdgpu_ring_ib_end(ring);
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
	int r, i;

	if (adev->ib_pool_ready)
		return 0;

	for (i = 0; i < AMDGPU_IB_POOL_MAX; i++) {
		r = amdgpu_sa_bo_manager_init(adev, &adev->ib_pools[i],
					      AMDGPU_IB_POOL_SIZE, 256,
					      AMDGPU_GEM_DOMAIN_GTT);
		if (r)
			goto error;
	}
	adev->ib_pool_ready = true;

	return 0;

error:
	while (i--)
		amdgpu_sa_bo_manager_fini(adev, &adev->ib_pools[i]);
	return r;
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
	int i;

	if (!adev->ib_pool_ready)
		return;

	for (i = 0; i < AMDGPU_IB_POOL_MAX; i++)
		amdgpu_sa_bo_manager_fini(adev, &adev->ib_pools[i]);
	adev->ib_pool_ready = false;
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
	long tmo_gfx, tmo_mm;
	int r, ret = 0;
	unsigned int i;

	tmo_mm = tmo_gfx = AMDGPU_IB_TEST_TIMEOUT;
	if (amdgpu_sriov_vf(adev)) {
		/* for MM engines in hypervisor side they are not scheduled together
		 * with CP and SDMA engines, so even in exclusive mode MM engine could
		 * still running on other VF thus the IB TEST TIMEOUT for MM engines
		 * under SR-IOV should be set to a long time. 8 sec should be enough
		 * for the MM comes back to this VF.
		 */
		tmo_mm = 8 * AMDGPU_IB_TEST_TIMEOUT;
	}

	if (amdgpu_sriov_runtime(adev)) {
		/* for CP & SDMA engines since they are scheduled together so
		 * need to make the timeout width enough to cover the time
		 * cost waiting for it coming back under RUNTIME only
		 */
		tmo_gfx = 8 * AMDGPU_IB_TEST_TIMEOUT;
	} else if (adev->gmc.xgmi.hive_id) {
		tmo_gfx = AMDGPU_IB_TEST_GFX_XGMI_TIMEOUT;
	}

	for (i = 0; i < adev->num_rings; ++i) {
		struct amdgpu_ring *ring = adev->rings[i];
		long tmo;

		/* KIQ rings don't have an IB test because we never submit IBs
		 * to them and they have no interrupt support.
		 */
		if (!ring->sched.ready || !ring->funcs->test_ib)
			continue;

		if (adev->enable_mes &&
		    ring->funcs->type == AMDGPU_RING_TYPE_KIQ)
			continue;

		/* MM engine need more time */
		if (ring->funcs->type == AMDGPU_RING_TYPE_UVD ||
			ring->funcs->type == AMDGPU_RING_TYPE_VCE ||
			ring->funcs->type == AMDGPU_RING_TYPE_UVD_ENC ||
			ring->funcs->type == AMDGPU_RING_TYPE_VCN_DEC ||
			ring->funcs->type == AMDGPU_RING_TYPE_VCN_ENC ||
			ring->funcs->type == AMDGPU_RING_TYPE_VCN_JPEG)
			tmo = tmo_mm;
		else
			tmo = tmo_gfx;

		r = amdgpu_ring_test_ib(ring, tmo);
		if (!r) {
			DRM_DEV_DEBUG(adev->dev, "ib test on %s succeeded\n",
				      ring->name);
			continue;
		}

		ring->sched.ready = false;
		DRM_DEV_ERROR(adev->dev, "IB test failed on %s (%d).\n",
			  ring->name, r);

		if (ring == &adev->gfx.gfx_ring[0]) {
			/* oh, oh, that's really bad */
			adev->accel_working = false;
			return r;

		} else {
			ret = r;
		}
	}
	return ret;
}

/*
 * Debugfs info
 */
#if defined(CONFIG_DEBUG_FS)

static int amdgpu_debugfs_sa_info_show(struct seq_file *m, void *unused)
{
	struct amdgpu_device *adev = m->private;

	seq_puts(m, "--------------------- DELAYED ---------------------\n");
	amdgpu_sa_bo_dump_debug_info(&adev->ib_pools[AMDGPU_IB_POOL_DELAYED],
				     m);
	seq_puts(m, "-------------------- IMMEDIATE --------------------\n");
	amdgpu_sa_bo_dump_debug_info(&adev->ib_pools[AMDGPU_IB_POOL_IMMEDIATE],
				     m);
	seq_puts(m, "--------------------- DIRECT ----------------------\n");
	amdgpu_sa_bo_dump_debug_info(&adev->ib_pools[AMDGPU_IB_POOL_DIRECT], m);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(amdgpu_debugfs_sa_info);

#endif

void amdgpu_debugfs_sa_init(struct amdgpu_device *adev)
{
#if defined(CONFIG_DEBUG_FS)
	struct drm_minor *minor = adev_to_drm(adev)->primary;
	struct dentry *root = minor->debugfs_root;

	debugfs_create_file("amdgpu_sa_info", 0444, root, adev,
			    &amdgpu_debugfs_sa_info_fops);

#endif
}
