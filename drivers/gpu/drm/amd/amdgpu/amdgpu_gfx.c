/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
 */
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_gfx.h"
#include "amdgpu_rlc.h"

/* delay 0.1 second to enable gfx off feature */
#define GFX_OFF_DELAY_ENABLE         msecs_to_jiffies(100)

/*
 * GPU GFX IP block helpers function.
 */

int amdgpu_gfx_queue_to_bit(struct amdgpu_device *adev, int mec,
			    int pipe, int queue)
{
	int bit = 0;

	bit += mec * adev->gfx.mec.num_pipe_per_mec
		* adev->gfx.mec.num_queue_per_pipe;
	bit += pipe * adev->gfx.mec.num_queue_per_pipe;
	bit += queue;

	return bit;
}

void amdgpu_gfx_bit_to_queue(struct amdgpu_device *adev, int bit,
			     int *mec, int *pipe, int *queue)
{
	*queue = bit % adev->gfx.mec.num_queue_per_pipe;
	*pipe = (bit / adev->gfx.mec.num_queue_per_pipe)
		% adev->gfx.mec.num_pipe_per_mec;
	*mec = (bit / adev->gfx.mec.num_queue_per_pipe)
	       / adev->gfx.mec.num_pipe_per_mec;

}

bool amdgpu_gfx_is_mec_queue_enabled(struct amdgpu_device *adev,
				     int mec, int pipe, int queue)
{
	return test_bit(amdgpu_gfx_queue_to_bit(adev, mec, pipe, queue),
			adev->gfx.mec.queue_bitmap);
}

/**
 * amdgpu_gfx_scratch_get - Allocate a scratch register
 *
 * @adev: amdgpu_device pointer
 * @reg: scratch register mmio offset
 *
 * Allocate a CP scratch register for use by the driver (all asics).
 * Returns 0 on success or -EINVAL on failure.
 */
int amdgpu_gfx_scratch_get(struct amdgpu_device *adev, uint32_t *reg)
{
	int i;

	i = ffs(adev->gfx.scratch.free_mask);
	if (i != 0 && i <= adev->gfx.scratch.num_reg) {
		i--;
		adev->gfx.scratch.free_mask &= ~(1u << i);
		*reg = adev->gfx.scratch.reg_base + i;
		return 0;
	}
	return -EINVAL;
}

/**
 * amdgpu_gfx_scratch_free - Free a scratch register
 *
 * @adev: amdgpu_device pointer
 * @reg: scratch register mmio offset
 *
 * Free a CP scratch register allocated for use by the driver (all asics)
 */
void amdgpu_gfx_scratch_free(struct amdgpu_device *adev, uint32_t reg)
{
	adev->gfx.scratch.free_mask |= 1u << (reg - adev->gfx.scratch.reg_base);
}

/**
 * amdgpu_gfx_parse_disable_cu - Parse the disable_cu module parameter
 *
 * @mask: array in which the per-shader array disable masks will be stored
 * @max_se: number of SEs
 * @max_sh: number of SHs
 *
 * The bitmask of CUs to be disabled in the shader array determined by se and
 * sh is stored in mask[se * max_sh + sh].
 */
void amdgpu_gfx_parse_disable_cu(unsigned *mask, unsigned max_se, unsigned max_sh)
{
	unsigned se, sh, cu;
	const char *p;

	memset(mask, 0, sizeof(*mask) * max_se * max_sh);

	if (!amdgpu_disable_cu || !*amdgpu_disable_cu)
		return;

	p = amdgpu_disable_cu;
	for (;;) {
		char *next;
		int ret = sscanf(p, "%u.%u.%u", &se, &sh, &cu);
		if (ret < 3) {
			DRM_ERROR("amdgpu: could not parse disable_cu\n");
			return;
		}

		if (se < max_se && sh < max_sh && cu < 16) {
			DRM_INFO("amdgpu: disabling CU %u.%u.%u\n", se, sh, cu);
			mask[se * max_sh + sh] |= 1u << cu;
		} else {
			DRM_ERROR("amdgpu: disable_cu %u.%u.%u is out of range\n",
				  se, sh, cu);
		}

		next = strchr(p, ',');
		if (!next)
			break;
		p = next + 1;
	}
}

static bool amdgpu_gfx_is_multipipe_capable(struct amdgpu_device *adev)
{
	if (amdgpu_compute_multipipe != -1) {
		DRM_INFO("amdgpu: forcing compute pipe policy %d\n",
			 amdgpu_compute_multipipe);
		return amdgpu_compute_multipipe == 1;
	}

	/* FIXME: spreading the queues across pipes causes perf regressions
	 * on POLARIS11 compute workloads */
	if (adev->asic_type == CHIP_POLARIS11)
		return false;

	return adev->gfx.mec.num_mec > 1;
}

void amdgpu_gfx_compute_queue_acquire(struct amdgpu_device *adev)
{
	int i, queue, pipe, mec;
	bool multipipe_policy = amdgpu_gfx_is_multipipe_capable(adev);

	/* policy for amdgpu compute queue ownership */
	for (i = 0; i < AMDGPU_MAX_COMPUTE_QUEUES; ++i) {
		queue = i % adev->gfx.mec.num_queue_per_pipe;
		pipe = (i / adev->gfx.mec.num_queue_per_pipe)
			% adev->gfx.mec.num_pipe_per_mec;
		mec = (i / adev->gfx.mec.num_queue_per_pipe)
			/ adev->gfx.mec.num_pipe_per_mec;

		/* we've run out of HW */
		if (mec >= adev->gfx.mec.num_mec)
			break;

		if (multipipe_policy) {
			/* policy: amdgpu owns the first two queues of the first MEC */
			if (mec == 0 && queue < 2)
				set_bit(i, adev->gfx.mec.queue_bitmap);
		} else {
			/* policy: amdgpu owns all queues in the first pipe */
			if (mec == 0 && pipe == 0)
				set_bit(i, adev->gfx.mec.queue_bitmap);
		}
	}

	/* update the number of active compute rings */
	adev->gfx.num_compute_rings =
		bitmap_weight(adev->gfx.mec.queue_bitmap, AMDGPU_MAX_COMPUTE_QUEUES);

	/* If you hit this case and edited the policy, you probably just
	 * need to increase AMDGPU_MAX_COMPUTE_RINGS */
	if (WARN_ON(adev->gfx.num_compute_rings > AMDGPU_MAX_COMPUTE_RINGS))
		adev->gfx.num_compute_rings = AMDGPU_MAX_COMPUTE_RINGS;
}

static int amdgpu_gfx_kiq_acquire(struct amdgpu_device *adev,
				  struct amdgpu_ring *ring)
{
	int queue_bit;
	int mec, pipe, queue;

	queue_bit = adev->gfx.mec.num_mec
		    * adev->gfx.mec.num_pipe_per_mec
		    * adev->gfx.mec.num_queue_per_pipe;

	while (queue_bit-- >= 0) {
		if (test_bit(queue_bit, adev->gfx.mec.queue_bitmap))
			continue;

		amdgpu_gfx_bit_to_queue(adev, queue_bit, &mec, &pipe, &queue);

		/*
		 * 1. Using pipes 2/3 from MEC 2 seems cause problems.
		 * 2. It must use queue id 0, because CGPG_IDLE/SAVE/LOAD/RUN
		 * only can be issued on queue 0.
		 */
		if ((mec == 1 && pipe > 1) || queue != 0)
			continue;

		ring->me = mec + 1;
		ring->pipe = pipe;
		ring->queue = queue;

		return 0;
	}

	dev_err(adev->dev, "Failed to find a queue for KIQ\n");
	return -EINVAL;
}

int amdgpu_gfx_kiq_init_ring(struct amdgpu_device *adev,
			     struct amdgpu_ring *ring,
			     struct amdgpu_irq_src *irq)
{
	struct amdgpu_kiq *kiq = &adev->gfx.kiq;
	int r = 0;

	spin_lock_init(&kiq->ring_lock);

	r = amdgpu_device_wb_get(adev, &adev->virt.reg_val_offs);
	if (r)
		return r;

	ring->adev = NULL;
	ring->ring_obj = NULL;
	ring->use_doorbell = true;
	ring->doorbell_index = adev->doorbell_index.kiq;

	r = amdgpu_gfx_kiq_acquire(adev, ring);
	if (r)
		return r;

	ring->eop_gpu_addr = kiq->eop_gpu_addr;
	sprintf(ring->name, "kiq_%d.%d.%d", ring->me, ring->pipe, ring->queue);
	r = amdgpu_ring_init(adev, ring, 1024,
			     irq, AMDGPU_CP_KIQ_IRQ_DRIVER0);
	if (r)
		dev_warn(adev->dev, "(%d) failed to init kiq ring\n", r);

	return r;
}

void amdgpu_gfx_kiq_free_ring(struct amdgpu_ring *ring,
			      struct amdgpu_irq_src *irq)
{
	amdgpu_device_wb_free(ring->adev, ring->adev->virt.reg_val_offs);
	amdgpu_ring_fini(ring);
}

void amdgpu_gfx_kiq_fini(struct amdgpu_device *adev)
{
	struct amdgpu_kiq *kiq = &adev->gfx.kiq;

	amdgpu_bo_free_kernel(&kiq->eop_obj, &kiq->eop_gpu_addr, NULL);
}

int amdgpu_gfx_kiq_init(struct amdgpu_device *adev,
			unsigned hpd_size)
{
	int r;
	u32 *hpd;
	struct amdgpu_kiq *kiq = &adev->gfx.kiq;

	r = amdgpu_bo_create_kernel(adev, hpd_size, PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_GTT, &kiq->eop_obj,
				    &kiq->eop_gpu_addr, (void **)&hpd);
	if (r) {
		dev_warn(adev->dev, "failed to create KIQ bo (%d).\n", r);
		return r;
	}

	memset(hpd, 0, hpd_size);

	r = amdgpu_bo_reserve(kiq->eop_obj, true);
	if (unlikely(r != 0))
		dev_warn(adev->dev, "(%d) reserve kiq eop bo failed\n", r);
	amdgpu_bo_kunmap(kiq->eop_obj);
	amdgpu_bo_unreserve(kiq->eop_obj);

	return 0;
}

/* create MQD for each compute queue */
int amdgpu_gfx_compute_mqd_sw_init(struct amdgpu_device *adev,
				   unsigned mqd_size)
{
	struct amdgpu_ring *ring = NULL;
	int r, i;

	/* create MQD for KIQ */
	ring = &adev->gfx.kiq.ring;
	if (!ring->mqd_obj) {
		/* originaly the KIQ MQD is put in GTT domain, but for SRIOV VRAM domain is a must
		 * otherwise hypervisor trigger SAVE_VF fail after driver unloaded which mean MQD
		 * deallocated and gart_unbind, to strict diverage we decide to use VRAM domain for
		 * KIQ MQD no matter SRIOV or Bare-metal
		 */
		r = amdgpu_bo_create_kernel(adev, mqd_size, PAGE_SIZE,
					    AMDGPU_GEM_DOMAIN_VRAM, &ring->mqd_obj,
					    &ring->mqd_gpu_addr, &ring->mqd_ptr);
		if (r) {
			dev_warn(adev->dev, "failed to create ring mqd ob (%d)", r);
			return r;
		}

		/* prepare MQD backup */
		adev->gfx.mec.mqd_backup[AMDGPU_MAX_COMPUTE_RINGS] = kmalloc(mqd_size, GFP_KERNEL);
		if (!adev->gfx.mec.mqd_backup[AMDGPU_MAX_COMPUTE_RINGS])
				dev_warn(adev->dev, "no memory to create MQD backup for ring %s\n", ring->name);
	}

	/* create MQD for each KCQ */
	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		ring = &adev->gfx.compute_ring[i];
		if (!ring->mqd_obj) {
			r = amdgpu_bo_create_kernel(adev, mqd_size, PAGE_SIZE,
						    AMDGPU_GEM_DOMAIN_GTT, &ring->mqd_obj,
						    &ring->mqd_gpu_addr, &ring->mqd_ptr);
			if (r) {
				dev_warn(adev->dev, "failed to create ring mqd ob (%d)", r);
				return r;
			}

			/* prepare MQD backup */
			adev->gfx.mec.mqd_backup[i] = kmalloc(mqd_size, GFP_KERNEL);
			if (!adev->gfx.mec.mqd_backup[i])
				dev_warn(adev->dev, "no memory to create MQD backup for ring %s\n", ring->name);
		}
	}

	return 0;
}

void amdgpu_gfx_compute_mqd_sw_fini(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = NULL;
	int i;

	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		ring = &adev->gfx.compute_ring[i];
		kfree(adev->gfx.mec.mqd_backup[i]);
		amdgpu_bo_free_kernel(&ring->mqd_obj,
				      &ring->mqd_gpu_addr,
				      &ring->mqd_ptr);
	}

	ring = &adev->gfx.kiq.ring;
	kfree(adev->gfx.mec.mqd_backup[AMDGPU_MAX_COMPUTE_RINGS]);
	amdgpu_bo_free_kernel(&ring->mqd_obj,
			      &ring->mqd_gpu_addr,
			      &ring->mqd_ptr);
}

/* amdgpu_gfx_off_ctrl - Handle gfx off feature enable/disable
 *
 * @adev: amdgpu_device pointer
 * @bool enable true: enable gfx off feature, false: disable gfx off feature
 *
 * 1. gfx off feature will be enabled by gfx ip after gfx cg gp enabled.
 * 2. other client can send request to disable gfx off feature, the request should be honored.
 * 3. other client can cancel their request of disable gfx off feature
 * 4. other client should not send request to enable gfx off feature before disable gfx off feature.
 */

void amdgpu_gfx_off_ctrl(struct amdgpu_device *adev, bool enable)
{
	if (!(adev->powerplay.pp_feature & PP_GFXOFF_MASK))
		return;

	if (!adev->powerplay.pp_funcs || !adev->powerplay.pp_funcs->set_powergating_by_smu)
		return;


	mutex_lock(&adev->gfx.gfx_off_mutex);

	if (!enable)
		adev->gfx.gfx_off_req_count++;
	else if (adev->gfx.gfx_off_req_count > 0)
		adev->gfx.gfx_off_req_count--;

	if (enable && !adev->gfx.gfx_off_state && !adev->gfx.gfx_off_req_count) {
		schedule_delayed_work(&adev->gfx.gfx_off_delay_work, GFX_OFF_DELAY_ENABLE);
	} else if (!enable && adev->gfx.gfx_off_state) {
		if (!amdgpu_dpm_set_powergating_by_smu(adev, AMD_IP_BLOCK_TYPE_GFX, false))
			adev->gfx.gfx_off_state = false;
	}

	mutex_unlock(&adev->gfx.gfx_off_mutex);
}
