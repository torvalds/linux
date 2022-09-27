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

#include <linux/firmware.h>
#include "amdgpu.h"
#include "amdgpu_gfx.h"
#include "amdgpu_rlc.h"
#include "amdgpu_ras.h"

/* delay 0.1 second to enable gfx off feature */
#define GFX_OFF_DELAY_ENABLE         msecs_to_jiffies(100)

#define GFX_OFF_NO_DELAY 0

/*
 * GPU GFX IP block helpers function.
 */

int amdgpu_gfx_mec_queue_to_bit(struct amdgpu_device *adev, int mec,
				int pipe, int queue)
{
	int bit = 0;

	bit += mec * adev->gfx.mec.num_pipe_per_mec
		* adev->gfx.mec.num_queue_per_pipe;
	bit += pipe * adev->gfx.mec.num_queue_per_pipe;
	bit += queue;

	return bit;
}

void amdgpu_queue_mask_bit_to_mec_queue(struct amdgpu_device *adev, int bit,
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
	return test_bit(amdgpu_gfx_mec_queue_to_bit(adev, mec, pipe, queue),
			adev->gfx.mec.queue_bitmap);
}

int amdgpu_gfx_me_queue_to_bit(struct amdgpu_device *adev,
			       int me, int pipe, int queue)
{
	int bit = 0;

	bit += me * adev->gfx.me.num_pipe_per_me
		* adev->gfx.me.num_queue_per_pipe;
	bit += pipe * adev->gfx.me.num_queue_per_pipe;
	bit += queue;

	return bit;
}

void amdgpu_gfx_bit_to_me_queue(struct amdgpu_device *adev, int bit,
				int *me, int *pipe, int *queue)
{
	*queue = bit % adev->gfx.me.num_queue_per_pipe;
	*pipe = (bit / adev->gfx.me.num_queue_per_pipe)
		% adev->gfx.me.num_pipe_per_me;
	*me = (bit / adev->gfx.me.num_queue_per_pipe)
		/ adev->gfx.me.num_pipe_per_me;
}

bool amdgpu_gfx_is_me_queue_enabled(struct amdgpu_device *adev,
				    int me, int pipe, int queue)
{
	return test_bit(amdgpu_gfx_me_queue_to_bit(adev, me, pipe, queue),
			adev->gfx.me.queue_bitmap);
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

static bool amdgpu_gfx_is_graphics_multipipe_capable(struct amdgpu_device *adev)
{
	return amdgpu_async_gfx_ring && adev->gfx.me.num_pipe_per_me > 1;
}

static bool amdgpu_gfx_is_compute_multipipe_capable(struct amdgpu_device *adev)
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

bool amdgpu_gfx_is_high_priority_graphics_queue(struct amdgpu_device *adev,
						struct amdgpu_ring *ring)
{
	int queue = ring->queue;
	int pipe = ring->pipe;

	/* Policy: use pipe1 queue0 as high priority graphics queue if we
	 * have more than one gfx pipe.
	 */
	if (amdgpu_gfx_is_graphics_multipipe_capable(adev) &&
	    adev->gfx.num_gfx_rings > 1 && pipe == 1 && queue == 0) {
		int me = ring->me;
		int bit;

		bit = amdgpu_gfx_me_queue_to_bit(adev, me, pipe, queue);
		if (ring == &adev->gfx.gfx_ring[bit])
			return true;
	}

	return false;
}

bool amdgpu_gfx_is_high_priority_compute_queue(struct amdgpu_device *adev,
					       struct amdgpu_ring *ring)
{
	/* Policy: use 1st queue as high priority compute queue if we
	 * have more than one compute queue.
	 */
	if (adev->gfx.num_compute_rings > 1 &&
	    ring == &adev->gfx.compute_ring[0])
		return true;

	return false;
}

void amdgpu_gfx_compute_queue_acquire(struct amdgpu_device *adev)
{
	int i, queue, pipe;
	bool multipipe_policy = amdgpu_gfx_is_compute_multipipe_capable(adev);
	int max_queues_per_mec = min(adev->gfx.mec.num_pipe_per_mec *
				     adev->gfx.mec.num_queue_per_pipe,
				     adev->gfx.num_compute_rings);

	if (multipipe_policy) {
		/* policy: make queues evenly cross all pipes on MEC1 only */
		for (i = 0; i < max_queues_per_mec; i++) {
			pipe = i % adev->gfx.mec.num_pipe_per_mec;
			queue = (i / adev->gfx.mec.num_pipe_per_mec) %
				adev->gfx.mec.num_queue_per_pipe;

			set_bit(pipe * adev->gfx.mec.num_queue_per_pipe + queue,
					adev->gfx.mec.queue_bitmap);
		}
	} else {
		/* policy: amdgpu owns all queues in the given pipe */
		for (i = 0; i < max_queues_per_mec; ++i)
			set_bit(i, adev->gfx.mec.queue_bitmap);
	}

	dev_dbg(adev->dev, "mec queue bitmap weight=%d\n", bitmap_weight(adev->gfx.mec.queue_bitmap, AMDGPU_MAX_COMPUTE_QUEUES));
}

void amdgpu_gfx_graphics_queue_acquire(struct amdgpu_device *adev)
{
	int i, queue, pipe;
	bool multipipe_policy = amdgpu_gfx_is_graphics_multipipe_capable(adev);
	int max_queues_per_me = adev->gfx.me.num_pipe_per_me *
					adev->gfx.me.num_queue_per_pipe;

	if (multipipe_policy) {
		/* policy: amdgpu owns the first queue per pipe at this stage
		 * will extend to mulitple queues per pipe later */
		for (i = 0; i < max_queues_per_me; i++) {
			pipe = i % adev->gfx.me.num_pipe_per_me;
			queue = (i / adev->gfx.me.num_pipe_per_me) %
				adev->gfx.me.num_queue_per_pipe;

			set_bit(pipe * adev->gfx.me.num_queue_per_pipe + queue,
				adev->gfx.me.queue_bitmap);
		}
	} else {
		for (i = 0; i < max_queues_per_me; ++i)
			set_bit(i, adev->gfx.me.queue_bitmap);
	}

	/* update the number of active graphics rings */
	adev->gfx.num_gfx_rings =
		bitmap_weight(adev->gfx.me.queue_bitmap, AMDGPU_MAX_GFX_QUEUES);
}

static int amdgpu_gfx_kiq_acquire(struct amdgpu_device *adev,
				  struct amdgpu_ring *ring)
{
	int queue_bit;
	int mec, pipe, queue;

	queue_bit = adev->gfx.mec.num_mec
		    * adev->gfx.mec.num_pipe_per_mec
		    * adev->gfx.mec.num_queue_per_pipe;

	while (--queue_bit >= 0) {
		if (test_bit(queue_bit, adev->gfx.mec.queue_bitmap))
			continue;

		amdgpu_queue_mask_bit_to_mec_queue(adev, queue_bit, &mec, &pipe, &queue);

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

	ring->adev = NULL;
	ring->ring_obj = NULL;
	ring->use_doorbell = true;
	ring->doorbell_index = adev->doorbell_index.kiq;

	r = amdgpu_gfx_kiq_acquire(adev, ring);
	if (r)
		return r;

	ring->eop_gpu_addr = kiq->eop_gpu_addr;
	ring->no_scheduler = true;
	sprintf(ring->name, "kiq_%d.%d.%d", ring->me, ring->pipe, ring->queue);
	r = amdgpu_ring_init(adev, ring, 1024, irq, AMDGPU_CP_KIQ_IRQ_DRIVER0,
			     AMDGPU_RING_PRIO_DEFAULT, NULL);
	if (r)
		dev_warn(adev->dev, "(%d) failed to init kiq ring\n", r);

	return r;
}

void amdgpu_gfx_kiq_free_ring(struct amdgpu_ring *ring)
{
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

/* create MQD for each compute/gfx queue */
int amdgpu_gfx_mqd_sw_init(struct amdgpu_device *adev,
			   unsigned mqd_size)
{
	struct amdgpu_ring *ring = NULL;
	int r, i;

	/* create MQD for KIQ */
	ring = &adev->gfx.kiq.ring;
	if (!adev->enable_mes_kiq && !ring->mqd_obj) {
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

	if (adev->asic_type >= CHIP_NAVI10 && amdgpu_async_gfx_ring) {
		/* create MQD for each KGQ */
		for (i = 0; i < adev->gfx.num_gfx_rings; i++) {
			ring = &adev->gfx.gfx_ring[i];
			if (!ring->mqd_obj) {
				r = amdgpu_bo_create_kernel(adev, mqd_size, PAGE_SIZE,
							    AMDGPU_GEM_DOMAIN_GTT, &ring->mqd_obj,
							    &ring->mqd_gpu_addr, &ring->mqd_ptr);
				if (r) {
					dev_warn(adev->dev, "failed to create ring mqd bo (%d)", r);
					return r;
				}

				/* prepare MQD backup */
				adev->gfx.me.mqd_backup[i] = kmalloc(mqd_size, GFP_KERNEL);
				if (!adev->gfx.me.mqd_backup[i])
					dev_warn(adev->dev, "no memory to create MQD backup for ring %s\n", ring->name);
			}
		}
	}

	/* create MQD for each KCQ */
	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		ring = &adev->gfx.compute_ring[i];
		if (!ring->mqd_obj) {
			r = amdgpu_bo_create_kernel(adev, mqd_size, PAGE_SIZE,
						    AMDGPU_GEM_DOMAIN_GTT, &ring->mqd_obj,
						    &ring->mqd_gpu_addr, &ring->mqd_ptr);
			if (r) {
				dev_warn(adev->dev, "failed to create ring mqd bo (%d)", r);
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

void amdgpu_gfx_mqd_sw_fini(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = NULL;
	int i;

	if (adev->asic_type >= CHIP_NAVI10 && amdgpu_async_gfx_ring) {
		for (i = 0; i < adev->gfx.num_gfx_rings; i++) {
			ring = &adev->gfx.gfx_ring[i];
			kfree(adev->gfx.me.mqd_backup[i]);
			amdgpu_bo_free_kernel(&ring->mqd_obj,
					      &ring->mqd_gpu_addr,
					      &ring->mqd_ptr);
		}
	}

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

int amdgpu_gfx_disable_kcq(struct amdgpu_device *adev)
{
	struct amdgpu_kiq *kiq = &adev->gfx.kiq;
	struct amdgpu_ring *kiq_ring = &kiq->ring;
	int i, r = 0;

	if (!kiq->pmf || !kiq->pmf->kiq_unmap_queues)
		return -EINVAL;

	spin_lock(&adev->gfx.kiq.ring_lock);
	if (amdgpu_ring_alloc(kiq_ring, kiq->pmf->unmap_queues_size *
					adev->gfx.num_compute_rings)) {
		spin_unlock(&adev->gfx.kiq.ring_lock);
		return -ENOMEM;
	}

	for (i = 0; i < adev->gfx.num_compute_rings; i++)
		kiq->pmf->kiq_unmap_queues(kiq_ring, &adev->gfx.compute_ring[i],
					   RESET_QUEUES, 0, 0);

	if (adev->gfx.kiq.ring.sched.ready && !adev->job_hang)
		r = amdgpu_ring_test_helper(kiq_ring);
	spin_unlock(&adev->gfx.kiq.ring_lock);

	return r;
}

int amdgpu_queue_mask_bit_to_set_resource_bit(struct amdgpu_device *adev,
					int queue_bit)
{
	int mec, pipe, queue;
	int set_resource_bit = 0;

	amdgpu_queue_mask_bit_to_mec_queue(adev, queue_bit, &mec, &pipe, &queue);

	set_resource_bit = mec * 4 * 8 + pipe * 8 + queue;

	return set_resource_bit;
}

int amdgpu_gfx_enable_kcq(struct amdgpu_device *adev)
{
	struct amdgpu_kiq *kiq = &adev->gfx.kiq;
	struct amdgpu_ring *kiq_ring = &adev->gfx.kiq.ring;
	uint64_t queue_mask = 0;
	int r, i;

	if (!kiq->pmf || !kiq->pmf->kiq_map_queues || !kiq->pmf->kiq_set_resources)
		return -EINVAL;

	for (i = 0; i < AMDGPU_MAX_COMPUTE_QUEUES; ++i) {
		if (!test_bit(i, adev->gfx.mec.queue_bitmap))
			continue;

		/* This situation may be hit in the future if a new HW
		 * generation exposes more than 64 queues. If so, the
		 * definition of queue_mask needs updating */
		if (WARN_ON(i > (sizeof(queue_mask)*8))) {
			DRM_ERROR("Invalid KCQ enabled: %d\n", i);
			break;
		}

		queue_mask |= (1ull << amdgpu_queue_mask_bit_to_set_resource_bit(adev, i));
	}

	DRM_INFO("kiq ring mec %d pipe %d q %d\n", kiq_ring->me, kiq_ring->pipe,
							kiq_ring->queue);
	spin_lock(&adev->gfx.kiq.ring_lock);
	r = amdgpu_ring_alloc(kiq_ring, kiq->pmf->map_queues_size *
					adev->gfx.num_compute_rings +
					kiq->pmf->set_resources_size);
	if (r) {
		DRM_ERROR("Failed to lock KIQ (%d).\n", r);
		spin_unlock(&adev->gfx.kiq.ring_lock);
		return r;
	}

	if (adev->enable_mes)
		queue_mask = ~0ULL;

	kiq->pmf->kiq_set_resources(kiq_ring, queue_mask);
	for (i = 0; i < adev->gfx.num_compute_rings; i++)
		kiq->pmf->kiq_map_queues(kiq_ring, &adev->gfx.compute_ring[i]);

	r = amdgpu_ring_test_helper(kiq_ring);
	spin_unlock(&adev->gfx.kiq.ring_lock);
	if (r)
		DRM_ERROR("KCQ enable failed\n");

	return r;
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
	unsigned long delay = GFX_OFF_DELAY_ENABLE;

	if (!(adev->pm.pp_feature & PP_GFXOFF_MASK))
		return;

	mutex_lock(&adev->gfx.gfx_off_mutex);

	if (enable) {
		/* If the count is already 0, it means there's an imbalance bug somewhere.
		 * Note that the bug may be in a different caller than the one which triggers the
		 * WARN_ON_ONCE.
		 */
		if (WARN_ON_ONCE(adev->gfx.gfx_off_req_count == 0))
			goto unlock;

		adev->gfx.gfx_off_req_count--;

		if (adev->gfx.gfx_off_req_count == 0 &&
		    !adev->gfx.gfx_off_state) {
			/* If going to s2idle, no need to wait */
			if (adev->in_s0ix)
				delay = GFX_OFF_NO_DELAY;
			schedule_delayed_work(&adev->gfx.gfx_off_delay_work,
					      delay);
		}
	} else {
		if (adev->gfx.gfx_off_req_count == 0) {
			cancel_delayed_work_sync(&adev->gfx.gfx_off_delay_work);

			if (adev->gfx.gfx_off_state &&
			    !amdgpu_dpm_set_powergating_by_smu(adev, AMD_IP_BLOCK_TYPE_GFX, false)) {
				adev->gfx.gfx_off_state = false;

				if (adev->gfx.funcs->init_spm_golden) {
					dev_dbg(adev->dev,
						"GFXOFF is disabled, re-init SPM golden settings\n");
					amdgpu_gfx_init_spm_golden(adev);
				}
			}
		}

		adev->gfx.gfx_off_req_count++;
	}

unlock:
	mutex_unlock(&adev->gfx.gfx_off_mutex);
}

int amdgpu_set_gfx_off_residency(struct amdgpu_device *adev, bool value)
{
	int r = 0;

	mutex_lock(&adev->gfx.gfx_off_mutex);

	r = amdgpu_dpm_set_residency_gfxoff(adev, value);

	mutex_unlock(&adev->gfx.gfx_off_mutex);

	return r;
}

int amdgpu_get_gfx_off_residency(struct amdgpu_device *adev, u32 *value)
{
	int r = 0;

	mutex_lock(&adev->gfx.gfx_off_mutex);

	r = amdgpu_dpm_get_residency_gfxoff(adev, value);

	mutex_unlock(&adev->gfx.gfx_off_mutex);

	return r;
}

int amdgpu_get_gfx_off_entrycount(struct amdgpu_device *adev, u64 *value)
{
	int r = 0;

	mutex_lock(&adev->gfx.gfx_off_mutex);

	r = amdgpu_dpm_get_entrycount_gfxoff(adev, value);

	mutex_unlock(&adev->gfx.gfx_off_mutex);

	return r;
}

int amdgpu_get_gfx_off_status(struct amdgpu_device *adev, uint32_t *value)
{

	int r = 0;

	mutex_lock(&adev->gfx.gfx_off_mutex);

	r = amdgpu_dpm_get_status_gfxoff(adev, value);

	mutex_unlock(&adev->gfx.gfx_off_mutex);

	return r;
}

int amdgpu_gfx_ras_late_init(struct amdgpu_device *adev, struct ras_common_if *ras_block)
{
	int r;

	if (amdgpu_ras_is_supported(adev, ras_block->block)) {
		if (!amdgpu_persistent_edc_harvesting_supported(adev))
			amdgpu_ras_reset_error_status(adev, AMDGPU_RAS_BLOCK__GFX);

		r = amdgpu_ras_block_late_init(adev, ras_block);
		if (r)
			return r;

		r = amdgpu_irq_get(adev, &adev->gfx.cp_ecc_error_irq, 0);
		if (r)
			goto late_fini;
	} else {
		amdgpu_ras_feature_enable_on_boot(adev, ras_block, 0);
	}

	return 0;
late_fini:
	amdgpu_ras_block_late_fini(adev, ras_block);
	return r;
}

int amdgpu_gfx_process_ras_data_cb(struct amdgpu_device *adev,
		void *err_data,
		struct amdgpu_iv_entry *entry)
{
	/* TODO ue will trigger an interrupt.
	 *
	 * When “Full RAS” is enabled, the per-IP interrupt sources should
	 * be disabled and the driver should only look for the aggregated
	 * interrupt via sync flood
	 */
	if (!amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__GFX)) {
		kgd2kfd_set_sram_ecc_flag(adev->kfd.dev);
		if (adev->gfx.ras && adev->gfx.ras->ras_block.hw_ops &&
		    adev->gfx.ras->ras_block.hw_ops->query_ras_error_count)
			adev->gfx.ras->ras_block.hw_ops->query_ras_error_count(adev, err_data);
		amdgpu_ras_reset_gpu(adev);
	}
	return AMDGPU_RAS_SUCCESS;
}

int amdgpu_gfx_cp_ecc_error_irq(struct amdgpu_device *adev,
				  struct amdgpu_irq_src *source,
				  struct amdgpu_iv_entry *entry)
{
	struct ras_common_if *ras_if = adev->gfx.ras_if;
	struct ras_dispatch_if ih_data = {
		.entry = entry,
	};

	if (!ras_if)
		return 0;

	ih_data.head = *ras_if;

	DRM_ERROR("CP ECC ERROR IRQ\n");
	amdgpu_ras_interrupt_dispatch(adev, &ih_data);
	return 0;
}

uint32_t amdgpu_kiq_rreg(struct amdgpu_device *adev, uint32_t reg)
{
	signed long r, cnt = 0;
	unsigned long flags;
	uint32_t seq, reg_val_offs = 0, value = 0;
	struct amdgpu_kiq *kiq = &adev->gfx.kiq;
	struct amdgpu_ring *ring = &kiq->ring;

	if (amdgpu_device_skip_hw_access(adev))
		return 0;

	if (adev->mes.ring.sched.ready)
		return amdgpu_mes_rreg(adev, reg);

	BUG_ON(!ring->funcs->emit_rreg);

	spin_lock_irqsave(&kiq->ring_lock, flags);
	if (amdgpu_device_wb_get(adev, &reg_val_offs)) {
		pr_err("critical bug! too many kiq readers\n");
		goto failed_unlock;
	}
	amdgpu_ring_alloc(ring, 32);
	amdgpu_ring_emit_rreg(ring, reg, reg_val_offs);
	r = amdgpu_fence_emit_polling(ring, &seq, MAX_KIQ_REG_WAIT);
	if (r)
		goto failed_undo;

	amdgpu_ring_commit(ring);
	spin_unlock_irqrestore(&kiq->ring_lock, flags);

	r = amdgpu_fence_wait_polling(ring, seq, MAX_KIQ_REG_WAIT);

	/* don't wait anymore for gpu reset case because this way may
	 * block gpu_recover() routine forever, e.g. this virt_kiq_rreg
	 * is triggered in TTM and ttm_bo_lock_delayed_workqueue() will
	 * never return if we keep waiting in virt_kiq_rreg, which cause
	 * gpu_recover() hang there.
	 *
	 * also don't wait anymore for IRQ context
	 * */
	if (r < 1 && (amdgpu_in_reset(adev) || in_interrupt()))
		goto failed_kiq_read;

	might_sleep();
	while (r < 1 && cnt++ < MAX_KIQ_REG_TRY) {
		msleep(MAX_KIQ_REG_BAILOUT_INTERVAL);
		r = amdgpu_fence_wait_polling(ring, seq, MAX_KIQ_REG_WAIT);
	}

	if (cnt > MAX_KIQ_REG_TRY)
		goto failed_kiq_read;

	mb();
	value = adev->wb.wb[reg_val_offs];
	amdgpu_device_wb_free(adev, reg_val_offs);
	return value;

failed_undo:
	amdgpu_ring_undo(ring);
failed_unlock:
	spin_unlock_irqrestore(&kiq->ring_lock, flags);
failed_kiq_read:
	if (reg_val_offs)
		amdgpu_device_wb_free(adev, reg_val_offs);
	dev_err(adev->dev, "failed to read reg:%x\n", reg);
	return ~0;
}

void amdgpu_kiq_wreg(struct amdgpu_device *adev, uint32_t reg, uint32_t v)
{
	signed long r, cnt = 0;
	unsigned long flags;
	uint32_t seq;
	struct amdgpu_kiq *kiq = &adev->gfx.kiq;
	struct amdgpu_ring *ring = &kiq->ring;

	BUG_ON(!ring->funcs->emit_wreg);

	if (amdgpu_device_skip_hw_access(adev))
		return;

	if (adev->mes.ring.sched.ready) {
		amdgpu_mes_wreg(adev, reg, v);
		return;
	}

	spin_lock_irqsave(&kiq->ring_lock, flags);
	amdgpu_ring_alloc(ring, 32);
	amdgpu_ring_emit_wreg(ring, reg, v);
	r = amdgpu_fence_emit_polling(ring, &seq, MAX_KIQ_REG_WAIT);
	if (r)
		goto failed_undo;

	amdgpu_ring_commit(ring);
	spin_unlock_irqrestore(&kiq->ring_lock, flags);

	r = amdgpu_fence_wait_polling(ring, seq, MAX_KIQ_REG_WAIT);

	/* don't wait anymore for gpu reset case because this way may
	 * block gpu_recover() routine forever, e.g. this virt_kiq_rreg
	 * is triggered in TTM and ttm_bo_lock_delayed_workqueue() will
	 * never return if we keep waiting in virt_kiq_rreg, which cause
	 * gpu_recover() hang there.
	 *
	 * also don't wait anymore for IRQ context
	 * */
	if (r < 1 && (amdgpu_in_reset(adev) || in_interrupt()))
		goto failed_kiq_write;

	might_sleep();
	while (r < 1 && cnt++ < MAX_KIQ_REG_TRY) {

		msleep(MAX_KIQ_REG_BAILOUT_INTERVAL);
		r = amdgpu_fence_wait_polling(ring, seq, MAX_KIQ_REG_WAIT);
	}

	if (cnt > MAX_KIQ_REG_TRY)
		goto failed_kiq_write;

	return;

failed_undo:
	amdgpu_ring_undo(ring);
	spin_unlock_irqrestore(&kiq->ring_lock, flags);
failed_kiq_write:
	dev_err(adev->dev, "failed to write reg:%x\n", reg);
}

int amdgpu_gfx_get_num_kcq(struct amdgpu_device *adev)
{
	if (amdgpu_num_kcq == -1) {
		return 8;
	} else if (amdgpu_num_kcq > 8 || amdgpu_num_kcq < 0) {
		dev_warn(adev->dev, "set kernel compute queue number to 8 due to invalid parameter provided by user\n");
		return 8;
	}
	return amdgpu_num_kcq;
}

void amdgpu_gfx_cp_init_microcode(struct amdgpu_device *adev,
				  uint32_t ucode_id)
{
	const struct gfx_firmware_header_v1_0 *cp_hdr;
	const struct gfx_firmware_header_v2_0 *cp_hdr_v2_0;
	struct amdgpu_firmware_info *info = NULL;
	const struct firmware *ucode_fw;
	unsigned int fw_size;

	switch (ucode_id) {
	case AMDGPU_UCODE_ID_CP_PFP:
		cp_hdr = (const struct gfx_firmware_header_v1_0 *)
			adev->gfx.pfp_fw->data;
		adev->gfx.pfp_fw_version =
			le32_to_cpu(cp_hdr->header.ucode_version);
		adev->gfx.pfp_feature_version =
			le32_to_cpu(cp_hdr->ucode_feature_version);
		ucode_fw = adev->gfx.pfp_fw;
		fw_size = le32_to_cpu(cp_hdr->header.ucode_size_bytes);
		break;
	case AMDGPU_UCODE_ID_CP_RS64_PFP:
		cp_hdr_v2_0 = (const struct gfx_firmware_header_v2_0 *)
			adev->gfx.pfp_fw->data;
		adev->gfx.pfp_fw_version =
			le32_to_cpu(cp_hdr_v2_0->header.ucode_version);
		adev->gfx.pfp_feature_version =
			le32_to_cpu(cp_hdr_v2_0->ucode_feature_version);
		ucode_fw = adev->gfx.pfp_fw;
		fw_size = le32_to_cpu(cp_hdr_v2_0->ucode_size_bytes);
		break;
	case AMDGPU_UCODE_ID_CP_RS64_PFP_P0_STACK:
	case AMDGPU_UCODE_ID_CP_RS64_PFP_P1_STACK:
		cp_hdr_v2_0 = (const struct gfx_firmware_header_v2_0 *)
			adev->gfx.pfp_fw->data;
		ucode_fw = adev->gfx.pfp_fw;
		fw_size = le32_to_cpu(cp_hdr_v2_0->data_size_bytes);
		break;
	case AMDGPU_UCODE_ID_CP_ME:
		cp_hdr = (const struct gfx_firmware_header_v1_0 *)
			adev->gfx.me_fw->data;
		adev->gfx.me_fw_version =
			le32_to_cpu(cp_hdr->header.ucode_version);
		adev->gfx.me_feature_version =
			le32_to_cpu(cp_hdr->ucode_feature_version);
		ucode_fw = adev->gfx.me_fw;
		fw_size = le32_to_cpu(cp_hdr->header.ucode_size_bytes);
		break;
	case AMDGPU_UCODE_ID_CP_RS64_ME:
		cp_hdr_v2_0 = (const struct gfx_firmware_header_v2_0 *)
			adev->gfx.me_fw->data;
		adev->gfx.me_fw_version =
			le32_to_cpu(cp_hdr_v2_0->header.ucode_version);
		adev->gfx.me_feature_version =
			le32_to_cpu(cp_hdr_v2_0->ucode_feature_version);
		ucode_fw = adev->gfx.me_fw;
		fw_size = le32_to_cpu(cp_hdr_v2_0->ucode_size_bytes);
		break;
	case AMDGPU_UCODE_ID_CP_RS64_ME_P0_STACK:
	case AMDGPU_UCODE_ID_CP_RS64_ME_P1_STACK:
		cp_hdr_v2_0 = (const struct gfx_firmware_header_v2_0 *)
			adev->gfx.me_fw->data;
		ucode_fw = adev->gfx.me_fw;
		fw_size = le32_to_cpu(cp_hdr_v2_0->data_size_bytes);
		break;
	case AMDGPU_UCODE_ID_CP_CE:
		cp_hdr = (const struct gfx_firmware_header_v1_0 *)
			adev->gfx.ce_fw->data;
		adev->gfx.ce_fw_version =
			le32_to_cpu(cp_hdr->header.ucode_version);
		adev->gfx.ce_feature_version =
			le32_to_cpu(cp_hdr->ucode_feature_version);
		ucode_fw = adev->gfx.ce_fw;
		fw_size = le32_to_cpu(cp_hdr->header.ucode_size_bytes);
		break;
	case AMDGPU_UCODE_ID_CP_MEC1:
		cp_hdr = (const struct gfx_firmware_header_v1_0 *)
			adev->gfx.mec_fw->data;
		adev->gfx.mec_fw_version =
			le32_to_cpu(cp_hdr->header.ucode_version);
		adev->gfx.mec_feature_version =
			le32_to_cpu(cp_hdr->ucode_feature_version);
		ucode_fw = adev->gfx.mec_fw;
		fw_size = le32_to_cpu(cp_hdr->header.ucode_size_bytes) -
			  le32_to_cpu(cp_hdr->jt_size) * 4;
		break;
	case AMDGPU_UCODE_ID_CP_MEC1_JT:
		cp_hdr = (const struct gfx_firmware_header_v1_0 *)
			adev->gfx.mec_fw->data;
		ucode_fw = adev->gfx.mec_fw;
		fw_size = le32_to_cpu(cp_hdr->jt_size) * 4;
		break;
	case AMDGPU_UCODE_ID_CP_MEC2:
		cp_hdr = (const struct gfx_firmware_header_v1_0 *)
			adev->gfx.mec2_fw->data;
		adev->gfx.mec2_fw_version =
			le32_to_cpu(cp_hdr->header.ucode_version);
		adev->gfx.mec2_feature_version =
			le32_to_cpu(cp_hdr->ucode_feature_version);
		ucode_fw = adev->gfx.mec2_fw;
		fw_size = le32_to_cpu(cp_hdr->header.ucode_size_bytes) -
			  le32_to_cpu(cp_hdr->jt_size) * 4;
		break;
	case AMDGPU_UCODE_ID_CP_MEC2_JT:
		cp_hdr = (const struct gfx_firmware_header_v1_0 *)
			adev->gfx.mec2_fw->data;
		ucode_fw = adev->gfx.mec2_fw;
		fw_size = le32_to_cpu(cp_hdr->jt_size) * 4;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_MEC:
		cp_hdr_v2_0 = (const struct gfx_firmware_header_v2_0 *)
			adev->gfx.mec_fw->data;
		adev->gfx.mec_fw_version =
			le32_to_cpu(cp_hdr_v2_0->header.ucode_version);
		adev->gfx.mec_feature_version =
			le32_to_cpu(cp_hdr_v2_0->ucode_feature_version);
		ucode_fw = adev->gfx.mec_fw;
		fw_size = le32_to_cpu(cp_hdr_v2_0->ucode_size_bytes);
		break;
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P0_STACK:
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P1_STACK:
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P2_STACK:
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P3_STACK:
		cp_hdr_v2_0 = (const struct gfx_firmware_header_v2_0 *)
			adev->gfx.mec_fw->data;
		ucode_fw = adev->gfx.mec_fw;
		fw_size = le32_to_cpu(cp_hdr_v2_0->data_size_bytes);
		break;
	default:
		break;
	}

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		info = &adev->firmware.ucode[ucode_id];
		info->ucode_id = ucode_id;
		info->fw = ucode_fw;
		adev->firmware.fw_size += ALIGN(fw_size, PAGE_SIZE);
	}
}
