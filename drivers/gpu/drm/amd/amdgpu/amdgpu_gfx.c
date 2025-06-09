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
#include <linux/pm_runtime.h>

#include "amdgpu.h"
#include "amdgpu_gfx.h"
#include "amdgpu_rlc.h"
#include "amdgpu_ras.h"
#include "amdgpu_reset.h"
#include "amdgpu_xcp.h"
#include "amdgpu_xgmi.h"

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
				     int xcc_id, int mec, int pipe, int queue)
{
	return test_bit(amdgpu_gfx_mec_queue_to_bit(adev, mec, pipe, queue),
			adev->gfx.mec_bitmap[xcc_id].queue_bitmap);
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
void amdgpu_gfx_parse_disable_cu(unsigned int *mask, unsigned int max_se, unsigned int max_sh)
{
	unsigned int se, sh, cu;
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

	if (amdgpu_ip_version(adev, GC_HWIP, 0) > IP_VERSION(9, 0, 0))
		return true;

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
	int i, j, queue, pipe;
	bool multipipe_policy = amdgpu_gfx_is_compute_multipipe_capable(adev);
	int max_queues_per_mec = min(adev->gfx.mec.num_pipe_per_mec *
				     adev->gfx.mec.num_queue_per_pipe,
				     adev->gfx.num_compute_rings);
	int num_xcc = adev->gfx.xcc_mask ? NUM_XCC(adev->gfx.xcc_mask) : 1;

	if (multipipe_policy) {
		/* policy: make queues evenly cross all pipes on MEC1 only
		 * for multiple xcc, just use the original policy for simplicity */
		for (j = 0; j < num_xcc; j++) {
			for (i = 0; i < max_queues_per_mec; i++) {
				pipe = i % adev->gfx.mec.num_pipe_per_mec;
				queue = (i / adev->gfx.mec.num_pipe_per_mec) %
					 adev->gfx.mec.num_queue_per_pipe;

				set_bit(pipe * adev->gfx.mec.num_queue_per_pipe + queue,
					adev->gfx.mec_bitmap[j].queue_bitmap);
			}
		}
	} else {
		/* policy: amdgpu owns all queues in the given pipe */
		for (j = 0; j < num_xcc; j++) {
			for (i = 0; i < max_queues_per_mec; ++i)
				set_bit(i, adev->gfx.mec_bitmap[j].queue_bitmap);
		}
	}

	for (j = 0; j < num_xcc; j++) {
		dev_dbg(adev->dev, "mec queue bitmap weight=%d\n",
			bitmap_weight(adev->gfx.mec_bitmap[j].queue_bitmap, AMDGPU_MAX_COMPUTE_QUEUES));
	}
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
				  struct amdgpu_ring *ring, int xcc_id)
{
	int queue_bit;
	int mec, pipe, queue;

	queue_bit = adev->gfx.mec.num_mec
		    * adev->gfx.mec.num_pipe_per_mec
		    * adev->gfx.mec.num_queue_per_pipe;

	while (--queue_bit >= 0) {
		if (test_bit(queue_bit, adev->gfx.mec_bitmap[xcc_id].queue_bitmap))
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

int amdgpu_gfx_kiq_init_ring(struct amdgpu_device *adev, int xcc_id)
{
	struct amdgpu_kiq *kiq = &adev->gfx.kiq[xcc_id];
	struct amdgpu_irq_src *irq = &kiq->irq;
	struct amdgpu_ring *ring = &kiq->ring;
	int r = 0;

	spin_lock_init(&kiq->ring_lock);

	ring->adev = NULL;
	ring->ring_obj = NULL;
	ring->use_doorbell = true;
	ring->xcc_id = xcc_id;
	ring->vm_hub = AMDGPU_GFXHUB(xcc_id);
	ring->doorbell_index =
		(adev->doorbell_index.kiq +
		 xcc_id * adev->doorbell_index.xcc_doorbell_range)
		<< 1;

	r = amdgpu_gfx_kiq_acquire(adev, ring, xcc_id);
	if (r)
		return r;

	ring->eop_gpu_addr = kiq->eop_gpu_addr;
	ring->no_scheduler = true;
	snprintf(ring->name, sizeof(ring->name), "kiq_%hhu.%hhu.%hhu.%hhu",
		 (unsigned char)xcc_id, (unsigned char)ring->me,
		 (unsigned char)ring->pipe, (unsigned char)ring->queue);
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

void amdgpu_gfx_kiq_fini(struct amdgpu_device *adev, int xcc_id)
{
	struct amdgpu_kiq *kiq = &adev->gfx.kiq[xcc_id];

	amdgpu_bo_free_kernel(&kiq->eop_obj, &kiq->eop_gpu_addr, NULL);
}

int amdgpu_gfx_kiq_init(struct amdgpu_device *adev,
			unsigned int hpd_size, int xcc_id)
{
	int r;
	u32 *hpd;
	struct amdgpu_kiq *kiq = &adev->gfx.kiq[xcc_id];

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
			   unsigned int mqd_size, int xcc_id)
{
	int r, i, j;
	struct amdgpu_kiq *kiq = &adev->gfx.kiq[xcc_id];
	struct amdgpu_ring *ring = &kiq->ring;
	u32 domain = AMDGPU_GEM_DOMAIN_GTT;

#if !defined(CONFIG_ARM) && !defined(CONFIG_ARM64)
	/* Only enable on gfx10 and 11 for now to avoid changing behavior on older chips */
	if (amdgpu_ip_version(adev, GC_HWIP, 0) >= IP_VERSION(10, 0, 0))
		domain |= AMDGPU_GEM_DOMAIN_VRAM;
#endif

	/* create MQD for KIQ */
	if (!adev->enable_mes_kiq && !ring->mqd_obj) {
		/* originaly the KIQ MQD is put in GTT domain, but for SRIOV VRAM domain is a must
		 * otherwise hypervisor trigger SAVE_VF fail after driver unloaded which mean MQD
		 * deallocated and gart_unbind, to strict diverage we decide to use VRAM domain for
		 * KIQ MQD no matter SRIOV or Bare-metal
		 */
		r = amdgpu_bo_create_kernel(adev, mqd_size, PAGE_SIZE,
					    AMDGPU_GEM_DOMAIN_VRAM |
					    AMDGPU_GEM_DOMAIN_GTT,
					    &ring->mqd_obj,
					    &ring->mqd_gpu_addr,
					    &ring->mqd_ptr);
		if (r) {
			dev_warn(adev->dev, "failed to create ring mqd ob (%d)", r);
			return r;
		}

		/* prepare MQD backup */
		kiq->mqd_backup = kzalloc(mqd_size, GFP_KERNEL);
		if (!kiq->mqd_backup) {
			dev_warn(adev->dev,
				 "no memory to create MQD backup for ring %s\n", ring->name);
			return -ENOMEM;
		}
	}

	if (adev->asic_type >= CHIP_NAVI10 && amdgpu_async_gfx_ring) {
		/* create MQD for each KGQ */
		for (i = 0; i < adev->gfx.num_gfx_rings; i++) {
			ring = &adev->gfx.gfx_ring[i];
			if (!ring->mqd_obj) {
				r = amdgpu_bo_create_kernel(adev, mqd_size, PAGE_SIZE,
							    domain, &ring->mqd_obj,
							    &ring->mqd_gpu_addr, &ring->mqd_ptr);
				if (r) {
					dev_warn(adev->dev, "failed to create ring mqd bo (%d)", r);
					return r;
				}

				ring->mqd_size = mqd_size;
				/* prepare MQD backup */
				adev->gfx.me.mqd_backup[i] = kzalloc(mqd_size, GFP_KERNEL);
				if (!adev->gfx.me.mqd_backup[i]) {
					dev_warn(adev->dev, "no memory to create MQD backup for ring %s\n", ring->name);
					return -ENOMEM;
				}
			}
		}
	}

	/* create MQD for each KCQ */
	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		j = i + xcc_id * adev->gfx.num_compute_rings;
		ring = &adev->gfx.compute_ring[j];
		if (!ring->mqd_obj) {
			r = amdgpu_bo_create_kernel(adev, mqd_size, PAGE_SIZE,
						    domain, &ring->mqd_obj,
						    &ring->mqd_gpu_addr, &ring->mqd_ptr);
			if (r) {
				dev_warn(adev->dev, "failed to create ring mqd bo (%d)", r);
				return r;
			}

			ring->mqd_size = mqd_size;
			/* prepare MQD backup */
			adev->gfx.mec.mqd_backup[j] = kzalloc(mqd_size, GFP_KERNEL);
			if (!adev->gfx.mec.mqd_backup[j]) {
				dev_warn(adev->dev, "no memory to create MQD backup for ring %s\n", ring->name);
				return -ENOMEM;
			}
		}
	}

	return 0;
}

void amdgpu_gfx_mqd_sw_fini(struct amdgpu_device *adev, int xcc_id)
{
	struct amdgpu_ring *ring = NULL;
	int i, j;
	struct amdgpu_kiq *kiq = &adev->gfx.kiq[xcc_id];

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
		j = i + xcc_id * adev->gfx.num_compute_rings;
		ring = &adev->gfx.compute_ring[j];
		kfree(adev->gfx.mec.mqd_backup[j]);
		amdgpu_bo_free_kernel(&ring->mqd_obj,
				      &ring->mqd_gpu_addr,
				      &ring->mqd_ptr);
	}

	ring = &kiq->ring;
	kfree(kiq->mqd_backup);
	amdgpu_bo_free_kernel(&ring->mqd_obj,
			      &ring->mqd_gpu_addr,
			      &ring->mqd_ptr);
}

int amdgpu_gfx_disable_kcq(struct amdgpu_device *adev, int xcc_id)
{
	struct amdgpu_kiq *kiq = &adev->gfx.kiq[xcc_id];
	struct amdgpu_ring *kiq_ring = &kiq->ring;
	int i, r = 0;
	int j;

	if (adev->enable_mes) {
		for (i = 0; i < adev->gfx.num_compute_rings; i++) {
			j = i + xcc_id * adev->gfx.num_compute_rings;
			amdgpu_mes_unmap_legacy_queue(adev,
						   &adev->gfx.compute_ring[j],
						   RESET_QUEUES, 0, 0);
		}
		return 0;
	}

	if (!kiq->pmf || !kiq->pmf->kiq_unmap_queues)
		return -EINVAL;

	if (!kiq_ring->sched.ready || amdgpu_in_reset(adev))
		return 0;

	spin_lock(&kiq->ring_lock);
	if (amdgpu_ring_alloc(kiq_ring, kiq->pmf->unmap_queues_size *
					adev->gfx.num_compute_rings)) {
		spin_unlock(&kiq->ring_lock);
		return -ENOMEM;
	}

	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		j = i + xcc_id * adev->gfx.num_compute_rings;
		kiq->pmf->kiq_unmap_queues(kiq_ring,
					   &adev->gfx.compute_ring[j],
					   RESET_QUEUES, 0, 0);
	}
	/* Submit unmap queue packet */
	amdgpu_ring_commit(kiq_ring);
	/*
	 * Ring test will do a basic scratch register change check. Just run
	 * this to ensure that unmap queues that is submitted before got
	 * processed successfully before returning.
	 */
	r = amdgpu_ring_test_helper(kiq_ring);

	spin_unlock(&kiq->ring_lock);

	return r;
}

int amdgpu_gfx_disable_kgq(struct amdgpu_device *adev, int xcc_id)
{
	struct amdgpu_kiq *kiq = &adev->gfx.kiq[xcc_id];
	struct amdgpu_ring *kiq_ring = &kiq->ring;
	int i, r = 0;
	int j;

	if (adev->enable_mes) {
		if (amdgpu_gfx_is_master_xcc(adev, xcc_id)) {
			for (i = 0; i < adev->gfx.num_gfx_rings; i++) {
				j = i + xcc_id * adev->gfx.num_gfx_rings;
				amdgpu_mes_unmap_legacy_queue(adev,
						      &adev->gfx.gfx_ring[j],
						      PREEMPT_QUEUES, 0, 0);
			}
		}
		return 0;
	}

	if (!kiq->pmf || !kiq->pmf->kiq_unmap_queues)
		return -EINVAL;

	if (!adev->gfx.kiq[0].ring.sched.ready || amdgpu_in_reset(adev))
		return 0;

	if (amdgpu_gfx_is_master_xcc(adev, xcc_id)) {
		spin_lock(&kiq->ring_lock);
		if (amdgpu_ring_alloc(kiq_ring, kiq->pmf->unmap_queues_size *
						adev->gfx.num_gfx_rings)) {
			spin_unlock(&kiq->ring_lock);
			return -ENOMEM;
		}

		for (i = 0; i < adev->gfx.num_gfx_rings; i++) {
			j = i + xcc_id * adev->gfx.num_gfx_rings;
			kiq->pmf->kiq_unmap_queues(kiq_ring,
						   &adev->gfx.gfx_ring[j],
						   PREEMPT_QUEUES, 0, 0);
		}
		/* Submit unmap queue packet */
		amdgpu_ring_commit(kiq_ring);

		/*
		 * Ring test will do a basic scratch register change check.
		 * Just run this to ensure that unmap queues that is submitted
		 * before got processed successfully before returning.
		 */
		r = amdgpu_ring_test_helper(kiq_ring);
		spin_unlock(&kiq->ring_lock);
	}

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

static int amdgpu_gfx_mes_enable_kcq(struct amdgpu_device *adev, int xcc_id)
{
	struct amdgpu_kiq *kiq = &adev->gfx.kiq[xcc_id];
	struct amdgpu_ring *kiq_ring = &kiq->ring;
	uint64_t queue_mask = ~0ULL;
	int r, i, j;

	amdgpu_device_flush_hdp(adev, NULL);

	if (!adev->enable_uni_mes) {
		spin_lock(&kiq->ring_lock);
		r = amdgpu_ring_alloc(kiq_ring, kiq->pmf->set_resources_size);
		if (r) {
			dev_err(adev->dev, "Failed to lock KIQ (%d).\n", r);
			spin_unlock(&kiq->ring_lock);
			return r;
		}

		kiq->pmf->kiq_set_resources(kiq_ring, queue_mask);
		r = amdgpu_ring_test_helper(kiq_ring);
		spin_unlock(&kiq->ring_lock);
		if (r)
			dev_err(adev->dev, "KIQ failed to set resources\n");
	}

	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		j = i + xcc_id * adev->gfx.num_compute_rings;
		r = amdgpu_mes_map_legacy_queue(adev,
						&adev->gfx.compute_ring[j]);
		if (r) {
			dev_err(adev->dev, "failed to map compute queue\n");
			return r;
		}
	}

	return 0;
}

int amdgpu_gfx_enable_kcq(struct amdgpu_device *adev, int xcc_id)
{
	struct amdgpu_kiq *kiq = &adev->gfx.kiq[xcc_id];
	struct amdgpu_ring *kiq_ring = &kiq->ring;
	uint64_t queue_mask = 0;
	int r, i, j;

	if (adev->mes.enable_legacy_queue_map)
		return amdgpu_gfx_mes_enable_kcq(adev, xcc_id);

	if (!kiq->pmf || !kiq->pmf->kiq_map_queues || !kiq->pmf->kiq_set_resources)
		return -EINVAL;

	for (i = 0; i < AMDGPU_MAX_COMPUTE_QUEUES; ++i) {
		if (!test_bit(i, adev->gfx.mec_bitmap[xcc_id].queue_bitmap))
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

	amdgpu_device_flush_hdp(adev, NULL);

	DRM_INFO("kiq ring mec %d pipe %d q %d\n", kiq_ring->me, kiq_ring->pipe,
		 kiq_ring->queue);

	spin_lock(&kiq->ring_lock);
	r = amdgpu_ring_alloc(kiq_ring, kiq->pmf->map_queues_size *
					adev->gfx.num_compute_rings +
					kiq->pmf->set_resources_size);
	if (r) {
		DRM_ERROR("Failed to lock KIQ (%d).\n", r);
		spin_unlock(&kiq->ring_lock);
		return r;
	}

	kiq->pmf->kiq_set_resources(kiq_ring, queue_mask);
	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		j = i + xcc_id * adev->gfx.num_compute_rings;
		kiq->pmf->kiq_map_queues(kiq_ring,
					 &adev->gfx.compute_ring[j]);
	}
	/* Submit map queue packet */
	amdgpu_ring_commit(kiq_ring);
	/*
	 * Ring test will do a basic scratch register change check. Just run
	 * this to ensure that map queues that is submitted before got
	 * processed successfully before returning.
	 */
	r = amdgpu_ring_test_helper(kiq_ring);
	spin_unlock(&kiq->ring_lock);
	if (r)
		DRM_ERROR("KCQ enable failed\n");

	return r;
}

int amdgpu_gfx_enable_kgq(struct amdgpu_device *adev, int xcc_id)
{
	struct amdgpu_kiq *kiq = &adev->gfx.kiq[xcc_id];
	struct amdgpu_ring *kiq_ring = &kiq->ring;
	int r, i, j;

	if (!kiq->pmf || !kiq->pmf->kiq_map_queues)
		return -EINVAL;

	amdgpu_device_flush_hdp(adev, NULL);

	if (adev->mes.enable_legacy_queue_map) {
		for (i = 0; i < adev->gfx.num_gfx_rings; i++) {
			j = i + xcc_id * adev->gfx.num_gfx_rings;
			r = amdgpu_mes_map_legacy_queue(adev,
							&adev->gfx.gfx_ring[j]);
			if (r) {
				DRM_ERROR("failed to map gfx queue\n");
				return r;
			}
		}

		return 0;
	}

	spin_lock(&kiq->ring_lock);
	/* No need to map kcq on the slave */
	if (amdgpu_gfx_is_master_xcc(adev, xcc_id)) {
		r = amdgpu_ring_alloc(kiq_ring, kiq->pmf->map_queues_size *
						adev->gfx.num_gfx_rings);
		if (r) {
			DRM_ERROR("Failed to lock KIQ (%d).\n", r);
			spin_unlock(&kiq->ring_lock);
			return r;
		}

		for (i = 0; i < adev->gfx.num_gfx_rings; i++) {
			j = i + xcc_id * adev->gfx.num_gfx_rings;
			kiq->pmf->kiq_map_queues(kiq_ring,
						 &adev->gfx.gfx_ring[j]);
		}
	}
	/* Submit map queue packet */
	amdgpu_ring_commit(kiq_ring);
	/*
	 * Ring test will do a basic scratch register change check. Just run
	 * this to ensure that map queues that is submitted before got
	 * processed successfully before returning.
	 */
	r = amdgpu_ring_test_helper(kiq_ring);
	spin_unlock(&kiq->ring_lock);
	if (r)
		DRM_ERROR("KGQ enable failed\n");

	return r;
}

static void amdgpu_gfx_do_off_ctrl(struct amdgpu_device *adev, bool enable,
				   bool no_delay)
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
			if (no_delay) {
				if (!amdgpu_dpm_set_powergating_by_smu(adev,
						AMD_IP_BLOCK_TYPE_GFX, true, 0))
					adev->gfx.gfx_off_state = true;
			} else {
				schedule_delayed_work(&adev->gfx.gfx_off_delay_work,
					      delay);
			}
		}
	} else {
		if (adev->gfx.gfx_off_req_count == 0) {
			cancel_delayed_work_sync(&adev->gfx.gfx_off_delay_work);

			if (adev->gfx.gfx_off_state &&
			    !amdgpu_dpm_set_powergating_by_smu(adev, AMD_IP_BLOCK_TYPE_GFX, false, 0)) {
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

/* amdgpu_gfx_off_ctrl - Handle gfx off feature enable/disable
 *
 * @adev: amdgpu_device pointer
 * @bool enable true: enable gfx off feature, false: disable gfx off feature
 *
 * 1. gfx off feature will be enabled by gfx ip after gfx cg pg enabled.
 * 2. other client can send request to disable gfx off feature, the request should be honored.
 * 3. other client can cancel their request of disable gfx off feature
 * 4. other client should not send request to enable gfx off feature before disable gfx off feature.
 *
 * gfx off allow will be delayed by GFX_OFF_DELAY_ENABLE ms.
 */
void amdgpu_gfx_off_ctrl(struct amdgpu_device *adev, bool enable)
{
	/* If going to s2idle, no need to wait */
	bool no_delay = adev->in_s0ix ? true : false;

	amdgpu_gfx_do_off_ctrl(adev, enable, no_delay);
}

/* amdgpu_gfx_off_ctrl_immediate - Handle gfx off feature enable/disable
 *
 * @adev: amdgpu_device pointer
 * @bool enable true: enable gfx off feature, false: disable gfx off feature
 *
 * 1. gfx off feature will be enabled by gfx ip after gfx cg pg enabled.
 * 2. other client can send request to disable gfx off feature, the request should be honored.
 * 3. other client can cancel their request of disable gfx off feature
 * 4. other client should not send request to enable gfx off feature before disable gfx off feature.
 *
 * gfx off allow will be issued immediately.
 */
void amdgpu_gfx_off_ctrl_immediate(struct amdgpu_device *adev, bool enable)
{
	amdgpu_gfx_do_off_ctrl(adev, enable, true);
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
		if (!amdgpu_persistent_edc_harvesting_supported(adev)) {
			r = amdgpu_ras_reset_error_status(adev, AMDGPU_RAS_BLOCK__GFX);
			if (r)
				return r;
		}

		r = amdgpu_ras_block_late_init(adev, ras_block);
		if (r)
			return r;

		if (amdgpu_sriov_vf(adev))
			return r;

		if (adev->gfx.cp_ecc_error_irq.funcs) {
			r = amdgpu_irq_get(adev, &adev->gfx.cp_ecc_error_irq, 0);
			if (r)
				goto late_fini;
		}
	} else {
		amdgpu_ras_feature_enable_on_boot(adev, ras_block, 0);
	}

	return 0;
late_fini:
	amdgpu_ras_block_late_fini(adev, ras_block);
	return r;
}

int amdgpu_gfx_ras_sw_init(struct amdgpu_device *adev)
{
	int err = 0;
	struct amdgpu_gfx_ras *ras = NULL;

	/* adev->gfx.ras is NULL, which means gfx does not
	 * support ras function, then do nothing here.
	 */
	if (!adev->gfx.ras)
		return 0;

	ras = adev->gfx.ras;

	err = amdgpu_ras_register_ras_block(adev, &ras->ras_block);
	if (err) {
		dev_err(adev->dev, "Failed to register gfx ras block!\n");
		return err;
	}

	strcpy(ras->ras_block.ras_comm.name, "gfx");
	ras->ras_block.ras_comm.block = AMDGPU_RAS_BLOCK__GFX;
	ras->ras_block.ras_comm.type = AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE;
	adev->gfx.ras_if = &ras->ras_block.ras_comm;

	/* If not define special ras_late_init function, use gfx default ras_late_init */
	if (!ras->ras_block.ras_late_init)
		ras->ras_block.ras_late_init = amdgpu_gfx_ras_late_init;

	/* If not defined special ras_cb function, use default ras_cb */
	if (!ras->ras_block.ras_cb)
		ras->ras_block.ras_cb = amdgpu_gfx_process_ras_data_cb;

	return 0;
}

int amdgpu_gfx_poison_consumption_handler(struct amdgpu_device *adev,
						struct amdgpu_iv_entry *entry)
{
	if (adev->gfx.ras && adev->gfx.ras->poison_consumption_handler)
		return adev->gfx.ras->poison_consumption_handler(adev, entry);

	return 0;
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

void amdgpu_gfx_ras_error_func(struct amdgpu_device *adev,
		void *ras_error_status,
		void (*func)(struct amdgpu_device *adev, void *ras_error_status,
				int xcc_id))
{
	int i;
	int num_xcc = adev->gfx.xcc_mask ? NUM_XCC(adev->gfx.xcc_mask) : 1;
	uint32_t xcc_mask = GENMASK(num_xcc - 1, 0);
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;

	if (err_data) {
		err_data->ue_count = 0;
		err_data->ce_count = 0;
	}

	for_each_inst(i, xcc_mask)
		func(adev, ras_error_status, i);
}

uint32_t amdgpu_kiq_rreg(struct amdgpu_device *adev, uint32_t reg, uint32_t xcc_id)
{
	signed long r, cnt = 0;
	unsigned long flags;
	uint32_t seq, reg_val_offs = 0, value = 0;
	struct amdgpu_kiq *kiq = &adev->gfx.kiq[xcc_id];
	struct amdgpu_ring *ring = &kiq->ring;

	if (amdgpu_device_skip_hw_access(adev))
		return 0;

	if (adev->mes.ring[0].sched.ready)
		return amdgpu_mes_rreg(adev, reg);

	BUG_ON(!ring->funcs->emit_rreg);

	spin_lock_irqsave(&kiq->ring_lock, flags);
	if (amdgpu_device_wb_get(adev, &reg_val_offs)) {
		pr_err("critical bug! too many kiq readers\n");
		goto failed_unlock;
	}
	r = amdgpu_ring_alloc(ring, 32);
	if (r)
		goto failed_unlock;

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

void amdgpu_kiq_wreg(struct amdgpu_device *adev, uint32_t reg, uint32_t v, uint32_t xcc_id)
{
	signed long r, cnt = 0;
	unsigned long flags;
	uint32_t seq;
	struct amdgpu_kiq *kiq = &adev->gfx.kiq[xcc_id];
	struct amdgpu_ring *ring = &kiq->ring;

	BUG_ON(!ring->funcs->emit_wreg);

	if (amdgpu_device_skip_hw_access(adev))
		return;

	if (adev->mes.ring[0].sched.ready) {
		amdgpu_mes_wreg(adev, reg, v);
		return;
	}

	spin_lock_irqsave(&kiq->ring_lock, flags);
	r = amdgpu_ring_alloc(ring, 32);
	if (r)
		goto failed_unlock;

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
failed_unlock:
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
		dev_err(adev->dev, "Invalid ucode id %u\n", ucode_id);
		return;
	}

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		info = &adev->firmware.ucode[ucode_id];
		info->ucode_id = ucode_id;
		info->fw = ucode_fw;
		adev->firmware.fw_size += ALIGN(fw_size, PAGE_SIZE);
	}
}

bool amdgpu_gfx_is_master_xcc(struct amdgpu_device *adev, int xcc_id)
{
	return !(xcc_id % (adev->gfx.num_xcc_per_xcp ?
			adev->gfx.num_xcc_per_xcp : 1));
}

static ssize_t amdgpu_gfx_get_current_compute_partition(struct device *dev,
						struct device_attribute *addr,
						char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	int mode;

	mode = amdgpu_xcp_query_partition_mode(adev->xcp_mgr,
					       AMDGPU_XCP_FL_NONE);

	return sysfs_emit(buf, "%s\n", amdgpu_gfx_compute_mode_desc(mode));
}

static ssize_t amdgpu_gfx_set_compute_partition(struct device *dev,
						struct device_attribute *addr,
						const char *buf, size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	enum amdgpu_gfx_partition mode;
	int ret = 0, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	if (num_xcc % 2 != 0)
		return -EINVAL;

	if (!strncasecmp("SPX", buf, strlen("SPX"))) {
		mode = AMDGPU_SPX_PARTITION_MODE;
	} else if (!strncasecmp("DPX", buf, strlen("DPX"))) {
		/*
		 * DPX mode needs AIDs to be in multiple of 2.
		 * Each AID connects 2 XCCs.
		 */
		if (num_xcc%4)
			return -EINVAL;
		mode = AMDGPU_DPX_PARTITION_MODE;
	} else if (!strncasecmp("TPX", buf, strlen("TPX"))) {
		if (num_xcc != 6)
			return -EINVAL;
		mode = AMDGPU_TPX_PARTITION_MODE;
	} else if (!strncasecmp("QPX", buf, strlen("QPX"))) {
		if (num_xcc != 8)
			return -EINVAL;
		mode = AMDGPU_QPX_PARTITION_MODE;
	} else if (!strncasecmp("CPX", buf, strlen("CPX"))) {
		mode = AMDGPU_CPX_PARTITION_MODE;
	} else {
		return -EINVAL;
	}

	ret = amdgpu_xcp_switch_partition_mode(adev->xcp_mgr, mode);

	if (ret)
		return ret;

	return count;
}

static const char *xcp_desc[] = {
	[AMDGPU_SPX_PARTITION_MODE] = "SPX",
	[AMDGPU_DPX_PARTITION_MODE] = "DPX",
	[AMDGPU_TPX_PARTITION_MODE] = "TPX",
	[AMDGPU_QPX_PARTITION_MODE] = "QPX",
	[AMDGPU_CPX_PARTITION_MODE] = "CPX",
};

static ssize_t amdgpu_gfx_get_available_compute_partition(struct device *dev,
						struct device_attribute *addr,
						char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	struct amdgpu_xcp_mgr *xcp_mgr = adev->xcp_mgr;
	int size = 0, mode;
	char *sep = "";

	if (!xcp_mgr || !xcp_mgr->avail_xcp_modes)
		return sysfs_emit(buf, "Not supported\n");

	for_each_inst(mode, xcp_mgr->avail_xcp_modes) {
		size += sysfs_emit_at(buf, size, "%s%s", sep, xcp_desc[mode]);
		sep = ", ";
	}

	size += sysfs_emit_at(buf, size, "\n");

	return size;
}

static int amdgpu_gfx_run_cleaner_shader_job(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct drm_gpu_scheduler *sched = &ring->sched;
	struct drm_sched_entity entity;
	struct dma_fence *f;
	struct amdgpu_job *job;
	struct amdgpu_ib *ib;
	int i, r;

	/* Initialize the scheduler entity */
	r = drm_sched_entity_init(&entity, DRM_SCHED_PRIORITY_NORMAL,
				  &sched, 1, NULL);
	if (r) {
		dev_err(adev->dev, "Failed setting up GFX kernel entity.\n");
		goto err;
	}

	r = amdgpu_job_alloc_with_ib(ring->adev, &entity, NULL,
				     64, 0,
				     &job);
	if (r)
		goto err;

	job->enforce_isolation = true;

	ib = &job->ibs[0];
	for (i = 0; i <= ring->funcs->align_mask; ++i)
		ib->ptr[i] = ring->funcs->nop;
	ib->length_dw = ring->funcs->align_mask + 1;

	f = amdgpu_job_submit(job);

	r = dma_fence_wait(f, false);
	if (r)
		goto err;

	dma_fence_put(f);

	/* Clean up the scheduler entity */
	drm_sched_entity_destroy(&entity);
	return 0;

err:
	return r;
}

static int amdgpu_gfx_run_cleaner_shader(struct amdgpu_device *adev, int xcp_id)
{
	int num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	struct amdgpu_ring *ring;
	int num_xcc_to_clear;
	int i, r, xcc_id;

	if (adev->gfx.num_xcc_per_xcp)
		num_xcc_to_clear = adev->gfx.num_xcc_per_xcp;
	else
		num_xcc_to_clear = 1;

	for (xcc_id = 0; xcc_id < num_xcc; xcc_id++) {
		for (i = 0; i < adev->gfx.num_compute_rings; i++) {
			ring = &adev->gfx.compute_ring[i + xcc_id * adev->gfx.num_compute_rings];
			if ((ring->xcp_id == xcp_id) && ring->sched.ready) {
				r = amdgpu_gfx_run_cleaner_shader_job(ring);
				if (r)
					return r;
				num_xcc_to_clear--;
				break;
			}
		}
	}

	if (num_xcc_to_clear)
		return -ENOENT;

	return 0;
}

/**
 * amdgpu_gfx_set_run_cleaner_shader - Execute the AMDGPU GFX Cleaner Shader
 * @dev: The device structure
 * @attr: The device attribute structure
 * @buf: The buffer containing the input data
 * @count: The size of the input data
 *
 * Provides the sysfs interface to manually run a cleaner shader, which is
 * used to clear the GPU state between different tasks. Writing a value to the
 * 'run_cleaner_shader' sysfs file triggers the cleaner shader execution.
 * The value written corresponds to the partition index on multi-partition
 * devices. On single-partition devices, the value should be '0'.
 *
 * The cleaner shader clears the Local Data Store (LDS) and General Purpose
 * Registers (GPRs) to ensure data isolation between GPU workloads.
 *
 * Return: The number of bytes written to the sysfs file.
 */
static ssize_t amdgpu_gfx_set_run_cleaner_shader(struct device *dev,
						 struct device_attribute *attr,
						 const char *buf,
						 size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	int ret;
	long value;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = kstrtol(buf, 0, &value);

	if (ret)
		return -EINVAL;

	if (value < 0)
		return -EINVAL;

	if (adev->xcp_mgr) {
		if (value >= adev->xcp_mgr->num_xcps)
			return -EINVAL;
	} else {
		if (value > 1)
			return -EINVAL;
	}

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	ret = amdgpu_gfx_run_cleaner_shader(adev, value);

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	if (ret)
		return ret;

	return count;
}

/**
 * amdgpu_gfx_get_enforce_isolation - Query AMDGPU GFX Enforce Isolation Settings
 * @dev: The device structure
 * @attr: The device attribute structure
 * @buf: The buffer to store the output data
 *
 * Provides the sysfs read interface to get the current settings of the 'enforce_isolation'
 * feature for each GPU partition. Reading from the 'enforce_isolation'
 * sysfs file returns the isolation settings for all partitions, where '0'
 * indicates disabled and '1' indicates enabled.
 *
 * Return: The number of bytes read from the sysfs file.
 */
static ssize_t amdgpu_gfx_get_enforce_isolation(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	int i;
	ssize_t size = 0;

	if (adev->xcp_mgr) {
		for (i = 0; i < adev->xcp_mgr->num_xcps; i++) {
			size += sysfs_emit_at(buf, size, "%u", adev->enforce_isolation[i]);
			if (i < (adev->xcp_mgr->num_xcps - 1))
				size += sysfs_emit_at(buf, size, " ");
		}
		buf[size++] = '\n';
	} else {
		size = sysfs_emit_at(buf, 0, "%u\n", adev->enforce_isolation[0]);
	}

	return size;
}

/**
 * amdgpu_gfx_set_enforce_isolation - Control AMDGPU GFX Enforce Isolation
 * @dev: The device structure
 * @attr: The device attribute structure
 * @buf: The buffer containing the input data
 * @count: The size of the input data
 *
 * This function allows control over the 'enforce_isolation' feature, which
 * serializes access to the graphics engine. Writing '1' or '0' to the
 * 'enforce_isolation' sysfs file enables or disables process isolation for
 * each partition. The input should specify the setting for all partitions.
 *
 * Return: The number of bytes written to the sysfs file.
 */
static ssize_t amdgpu_gfx_set_enforce_isolation(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	long partition_values[MAX_XCP] = {0};
	int ret, i, num_partitions;
	const char *input_buf = buf;

	for (i = 0; i < (adev->xcp_mgr ? adev->xcp_mgr->num_xcps : 1); i++) {
		ret = sscanf(input_buf, "%ld", &partition_values[i]);
		if (ret <= 0)
			break;

		/* Move the pointer to the next value in the string */
		input_buf = strchr(input_buf, ' ');
		if (input_buf) {
			input_buf++;
		} else {
			i++;
			break;
		}
	}
	num_partitions = i;

	if (adev->xcp_mgr && num_partitions != adev->xcp_mgr->num_xcps)
		return -EINVAL;

	if (!adev->xcp_mgr && num_partitions != 1)
		return -EINVAL;

	for (i = 0; i < num_partitions; i++) {
		if (partition_values[i] != 0 && partition_values[i] != 1)
			return -EINVAL;
	}

	mutex_lock(&adev->enforce_isolation_mutex);
	for (i = 0; i < num_partitions; i++)
		adev->enforce_isolation[i] = partition_values[i];
	mutex_unlock(&adev->enforce_isolation_mutex);

	amdgpu_mes_update_enforce_isolation(adev);

	return count;
}

static ssize_t amdgpu_gfx_get_gfx_reset_mask(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);

	if (!adev)
		return -ENODEV;

	return amdgpu_show_reset_mask(buf, adev->gfx.gfx_supported_reset);
}

static ssize_t amdgpu_gfx_get_compute_reset_mask(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);

	if (!adev)
		return -ENODEV;

	return amdgpu_show_reset_mask(buf, adev->gfx.compute_supported_reset);
}

static DEVICE_ATTR(run_cleaner_shader, 0200,
		   NULL, amdgpu_gfx_set_run_cleaner_shader);

static DEVICE_ATTR(enforce_isolation, 0644,
		   amdgpu_gfx_get_enforce_isolation,
		   amdgpu_gfx_set_enforce_isolation);

static DEVICE_ATTR(current_compute_partition, 0644,
		   amdgpu_gfx_get_current_compute_partition,
		   amdgpu_gfx_set_compute_partition);

static DEVICE_ATTR(available_compute_partition, 0444,
		   amdgpu_gfx_get_available_compute_partition, NULL);
static DEVICE_ATTR(gfx_reset_mask, 0444,
		   amdgpu_gfx_get_gfx_reset_mask, NULL);

static DEVICE_ATTR(compute_reset_mask, 0444,
		   amdgpu_gfx_get_compute_reset_mask, NULL);

static int amdgpu_gfx_sysfs_xcp_init(struct amdgpu_device *adev)
{
	struct amdgpu_xcp_mgr *xcp_mgr = adev->xcp_mgr;
	bool xcp_switch_supported;
	int r;

	if (!xcp_mgr)
		return 0;

	xcp_switch_supported =
		(xcp_mgr->funcs && xcp_mgr->funcs->switch_partition_mode);

	if (!xcp_switch_supported)
		dev_attr_current_compute_partition.attr.mode &=
			~(S_IWUSR | S_IWGRP | S_IWOTH);

	r = device_create_file(adev->dev, &dev_attr_current_compute_partition);
	if (r)
		return r;

	if (xcp_switch_supported)
		r = device_create_file(adev->dev,
				       &dev_attr_available_compute_partition);

	return r;
}

static void amdgpu_gfx_sysfs_xcp_fini(struct amdgpu_device *adev)
{
	struct amdgpu_xcp_mgr *xcp_mgr = adev->xcp_mgr;
	bool xcp_switch_supported;

	if (!xcp_mgr)
		return;

	xcp_switch_supported =
		(xcp_mgr->funcs && xcp_mgr->funcs->switch_partition_mode);
	device_remove_file(adev->dev, &dev_attr_current_compute_partition);

	if (xcp_switch_supported)
		device_remove_file(adev->dev,
				   &dev_attr_available_compute_partition);
}

static int amdgpu_gfx_sysfs_isolation_shader_init(struct amdgpu_device *adev)
{
	int r;

	r = device_create_file(adev->dev, &dev_attr_enforce_isolation);
	if (r)
		return r;
	if (adev->gfx.enable_cleaner_shader)
		r = device_create_file(adev->dev, &dev_attr_run_cleaner_shader);

	return r;
}

static void amdgpu_gfx_sysfs_isolation_shader_fini(struct amdgpu_device *adev)
{
	device_remove_file(adev->dev, &dev_attr_enforce_isolation);
	if (adev->gfx.enable_cleaner_shader)
		device_remove_file(adev->dev, &dev_attr_run_cleaner_shader);
}

static int amdgpu_gfx_sysfs_reset_mask_init(struct amdgpu_device *adev)
{
	int r = 0;

	if (!amdgpu_gpu_recovery)
		return r;

	if (adev->gfx.num_gfx_rings) {
		r = device_create_file(adev->dev, &dev_attr_gfx_reset_mask);
		if (r)
			return r;
	}

	if (adev->gfx.num_compute_rings) {
		r = device_create_file(adev->dev, &dev_attr_compute_reset_mask);
		if (r)
			return r;
	}

	return r;
}

static void amdgpu_gfx_sysfs_reset_mask_fini(struct amdgpu_device *adev)
{
	if (!amdgpu_gpu_recovery)
		return;

	if (adev->gfx.num_gfx_rings)
		device_remove_file(adev->dev, &dev_attr_gfx_reset_mask);

	if (adev->gfx.num_compute_rings)
		device_remove_file(adev->dev, &dev_attr_compute_reset_mask);
}

int amdgpu_gfx_sysfs_init(struct amdgpu_device *adev)
{
	int r;

	r = amdgpu_gfx_sysfs_xcp_init(adev);
	if (r) {
		dev_err(adev->dev, "failed to create xcp sysfs files");
		return r;
	}

	r = amdgpu_gfx_sysfs_isolation_shader_init(adev);
	if (r)
		dev_err(adev->dev, "failed to create isolation sysfs files");

	r = amdgpu_gfx_sysfs_reset_mask_init(adev);
	if (r)
		dev_err(adev->dev, "failed to create reset mask sysfs files");

	return r;
}

void amdgpu_gfx_sysfs_fini(struct amdgpu_device *adev)
{
	if (adev->dev->kobj.sd) {
		amdgpu_gfx_sysfs_xcp_fini(adev);
		amdgpu_gfx_sysfs_isolation_shader_fini(adev);
		amdgpu_gfx_sysfs_reset_mask_fini(adev);
	}
}

int amdgpu_gfx_cleaner_shader_sw_init(struct amdgpu_device *adev,
				      unsigned int cleaner_shader_size)
{
	if (!adev->gfx.enable_cleaner_shader)
		return -EOPNOTSUPP;

	return amdgpu_bo_create_kernel(adev, cleaner_shader_size, PAGE_SIZE,
				       AMDGPU_GEM_DOMAIN_VRAM | AMDGPU_GEM_DOMAIN_GTT,
				       &adev->gfx.cleaner_shader_obj,
				       &adev->gfx.cleaner_shader_gpu_addr,
				       (void **)&adev->gfx.cleaner_shader_cpu_ptr);
}

void amdgpu_gfx_cleaner_shader_sw_fini(struct amdgpu_device *adev)
{
	if (!adev->gfx.enable_cleaner_shader)
		return;

	amdgpu_bo_free_kernel(&adev->gfx.cleaner_shader_obj,
			      &adev->gfx.cleaner_shader_gpu_addr,
			      (void **)&adev->gfx.cleaner_shader_cpu_ptr);
}

void amdgpu_gfx_cleaner_shader_init(struct amdgpu_device *adev,
				    unsigned int cleaner_shader_size,
				    const void *cleaner_shader_ptr)
{
	if (!adev->gfx.enable_cleaner_shader)
		return;

	if (adev->gfx.cleaner_shader_cpu_ptr && cleaner_shader_ptr)
		memcpy_toio(adev->gfx.cleaner_shader_cpu_ptr, cleaner_shader_ptr,
			    cleaner_shader_size);
}

/**
 * amdgpu_gfx_kfd_sch_ctrl - Control the KFD scheduler from the KGD (Graphics Driver)
 * @adev: amdgpu_device pointer
 * @idx: Index of the scheduler to control
 * @enable: Whether to enable or disable the KFD scheduler
 *
 * This function is used to control the KFD (Kernel Fusion Driver) scheduler
 * from the KGD. It is part of the cleaner shader feature. This function plays
 * a key role in enforcing process isolation on the GPU.
 *
 * The function uses a reference count mechanism (kfd_sch_req_count) to keep
 * track of the number of requests to enable the KFD scheduler. When a request
 * to enable the KFD scheduler is made, the reference count is decremented.
 * When the reference count reaches zero, a delayed work is scheduled to
 * enforce isolation after a delay of GFX_SLICE_PERIOD.
 *
 * When a request to disable the KFD scheduler is made, the function first
 * checks if the reference count is zero. If it is, it cancels the delayed work
 * for enforcing isolation and checks if the KFD scheduler is active. If the
 * KFD scheduler is active, it sends a request to stop the KFD scheduler and
 * sets the KFD scheduler state to inactive. Then, it increments the reference
 * count.
 *
 * The function is synchronized using the kfd_sch_mutex to ensure that the KFD
 * scheduler state and reference count are updated atomically.
 *
 * Note: If the reference count is already zero when a request to enable the
 * KFD scheduler is made, it means there's an imbalance bug somewhere. The
 * function triggers a warning in this case.
 */
static void amdgpu_gfx_kfd_sch_ctrl(struct amdgpu_device *adev, u32 idx,
				    bool enable)
{
	mutex_lock(&adev->gfx.kfd_sch_mutex);

	if (enable) {
		/* If the count is already 0, it means there's an imbalance bug somewhere.
		 * Note that the bug may be in a different caller than the one which triggers the
		 * WARN_ON_ONCE.
		 */
		if (WARN_ON_ONCE(adev->gfx.kfd_sch_req_count[idx] == 0)) {
			dev_err(adev->dev, "Attempted to enable KFD scheduler when reference count is already zero\n");
			goto unlock;
		}

		adev->gfx.kfd_sch_req_count[idx]--;

		if (adev->gfx.kfd_sch_req_count[idx] == 0 &&
		    adev->gfx.kfd_sch_inactive[idx]) {
			schedule_delayed_work(&adev->gfx.enforce_isolation[idx].work,
					      msecs_to_jiffies(adev->gfx.enforce_isolation_time[idx]));
		}
	} else {
		if (adev->gfx.kfd_sch_req_count[idx] == 0) {
			cancel_delayed_work_sync(&adev->gfx.enforce_isolation[idx].work);
			if (!adev->gfx.kfd_sch_inactive[idx]) {
				amdgpu_amdkfd_stop_sched(adev, idx);
				adev->gfx.kfd_sch_inactive[idx] = true;
			}
		}

		adev->gfx.kfd_sch_req_count[idx]++;
	}

unlock:
	mutex_unlock(&adev->gfx.kfd_sch_mutex);
}

/**
 * amdgpu_gfx_enforce_isolation_handler - work handler for enforcing shader isolation
 *
 * @work: work_struct.
 *
 * This function is the work handler for enforcing shader isolation on AMD GPUs.
 * It counts the number of emitted fences for each GFX and compute ring. If there
 * are any fences, it schedules the `enforce_isolation_work` to be run after a
 * delay of `GFX_SLICE_PERIOD`. If there are no fences, it signals the Kernel Fusion
 * Driver (KFD) to resume the runqueue. The function is synchronized using the
 * `enforce_isolation_mutex`.
 */
void amdgpu_gfx_enforce_isolation_handler(struct work_struct *work)
{
	struct amdgpu_isolation_work *isolation_work =
		container_of(work, struct amdgpu_isolation_work, work.work);
	struct amdgpu_device *adev = isolation_work->adev;
	u32 i, idx, fences = 0;

	if (isolation_work->xcp_id == AMDGPU_XCP_NO_PARTITION)
		idx = 0;
	else
		idx = isolation_work->xcp_id;

	if (idx >= MAX_XCP)
		return;

	mutex_lock(&adev->enforce_isolation_mutex);
	for (i = 0; i < AMDGPU_MAX_GFX_RINGS; ++i) {
		if (isolation_work->xcp_id == adev->gfx.gfx_ring[i].xcp_id)
			fences += amdgpu_fence_count_emitted(&adev->gfx.gfx_ring[i]);
	}
	for (i = 0; i < (AMDGPU_MAX_COMPUTE_RINGS * AMDGPU_MAX_GC_INSTANCES); ++i) {
		if (isolation_work->xcp_id == adev->gfx.compute_ring[i].xcp_id)
			fences += amdgpu_fence_count_emitted(&adev->gfx.compute_ring[i]);
	}
	if (fences) {
		/* we've already had our timeslice, so let's wrap this up */
		schedule_delayed_work(&adev->gfx.enforce_isolation[idx].work,
				      msecs_to_jiffies(1));
	} else {
		/* Tell KFD to resume the runqueue */
		if (adev->kfd.init_complete) {
			WARN_ON_ONCE(!adev->gfx.kfd_sch_inactive[idx]);
			WARN_ON_ONCE(adev->gfx.kfd_sch_req_count[idx]);
			amdgpu_amdkfd_start_sched(adev, idx);
			adev->gfx.kfd_sch_inactive[idx] = false;
		}
	}
	mutex_unlock(&adev->enforce_isolation_mutex);
}

/**
 * amdgpu_gfx_enforce_isolation_wait_for_kfd - Manage KFD wait period for process isolation
 * @adev: amdgpu_device pointer
 * @idx: Index of the GPU partition
 *
 * When kernel submissions come in, the jobs are given a time slice and once
 * that time slice is up, if there are KFD user queues active, kernel
 * submissions are blocked until KFD has had its time slice. Once the KFD time
 * slice is up, KFD user queues are preempted and kernel submissions are
 * unblocked and allowed to run again.
 */
static void
amdgpu_gfx_enforce_isolation_wait_for_kfd(struct amdgpu_device *adev,
					  u32 idx)
{
	unsigned long cjiffies;
	bool wait = false;

	mutex_lock(&adev->enforce_isolation_mutex);
	if (adev->enforce_isolation[idx]) {
		/* set the initial values if nothing is set */
		if (!adev->gfx.enforce_isolation_jiffies[idx]) {
			adev->gfx.enforce_isolation_jiffies[idx] = jiffies;
			adev->gfx.enforce_isolation_time[idx] =	GFX_SLICE_PERIOD_MS;
		}
		/* Make sure KFD gets a chance to run */
		if (amdgpu_amdkfd_compute_active(adev, idx)) {
			cjiffies = jiffies;
			if (time_after(cjiffies, adev->gfx.enforce_isolation_jiffies[idx])) {
				cjiffies -= adev->gfx.enforce_isolation_jiffies[idx];
				if ((jiffies_to_msecs(cjiffies) >= GFX_SLICE_PERIOD_MS)) {
					/* if our time is up, let KGD work drain before scheduling more */
					wait = true;
					/* reset the timer period */
					adev->gfx.enforce_isolation_time[idx] =	GFX_SLICE_PERIOD_MS;
				} else {
					/* set the timer period to what's left in our time slice */
					adev->gfx.enforce_isolation_time[idx] =
						GFX_SLICE_PERIOD_MS - jiffies_to_msecs(cjiffies);
				}
			} else {
				/* if jiffies wrap around we will just wait a little longer */
				adev->gfx.enforce_isolation_jiffies[idx] = jiffies;
			}
		} else {
			/* if there is no KFD work, then set the full slice period */
			adev->gfx.enforce_isolation_jiffies[idx] = jiffies;
			adev->gfx.enforce_isolation_time[idx] = GFX_SLICE_PERIOD_MS;
		}
	}
	mutex_unlock(&adev->enforce_isolation_mutex);

	if (wait)
		msleep(GFX_SLICE_PERIOD_MS);
}

/**
 * amdgpu_gfx_enforce_isolation_ring_begin_use - Begin use of a ring with enforced isolation
 * @ring: Pointer to the amdgpu_ring structure
 *
 * Ring begin_use helper implementation for gfx which serializes access to the
 * gfx IP between kernel submission IOCTLs and KFD user queues when isolation
 * enforcement is enabled. The kernel submission IOCTLs and KFD user queues
 * each get a time slice when both are active.
 */
void amdgpu_gfx_enforce_isolation_ring_begin_use(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u32 idx;
	bool sched_work = false;

	if (!adev->gfx.enable_cleaner_shader)
		return;

	if (ring->xcp_id == AMDGPU_XCP_NO_PARTITION)
		idx = 0;
	else
		idx = ring->xcp_id;

	if (idx >= MAX_XCP)
		return;

	/* Don't submit more work until KFD has had some time */
	amdgpu_gfx_enforce_isolation_wait_for_kfd(adev, idx);

	mutex_lock(&adev->enforce_isolation_mutex);
	if (adev->enforce_isolation[idx]) {
		if (adev->kfd.init_complete)
			sched_work = true;
	}
	mutex_unlock(&adev->enforce_isolation_mutex);

	if (sched_work)
		amdgpu_gfx_kfd_sch_ctrl(adev, idx, false);
}

/**
 * amdgpu_gfx_enforce_isolation_ring_end_use - End use of a ring with enforced isolation
 * @ring: Pointer to the amdgpu_ring structure
 *
 * Ring end_use helper implementation for gfx which serializes access to the
 * gfx IP between kernel submission IOCTLs and KFD user queues when isolation
 * enforcement is enabled. The kernel submission IOCTLs and KFD user queues
 * each get a time slice when both are active.
 */
void amdgpu_gfx_enforce_isolation_ring_end_use(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u32 idx;
	bool sched_work = false;

	if (!adev->gfx.enable_cleaner_shader)
		return;

	if (ring->xcp_id == AMDGPU_XCP_NO_PARTITION)
		idx = 0;
	else
		idx = ring->xcp_id;

	if (idx >= MAX_XCP)
		return;

	mutex_lock(&adev->enforce_isolation_mutex);
	if (adev->enforce_isolation[idx]) {
		if (adev->kfd.init_complete)
			sched_work = true;
	}
	mutex_unlock(&adev->enforce_isolation_mutex);

	if (sched_work)
		amdgpu_gfx_kfd_sch_ctrl(adev, idx, true);
}

void amdgpu_gfx_profile_idle_work_handler(struct work_struct *work)
{
	struct amdgpu_device *adev =
		container_of(work, struct amdgpu_device, gfx.idle_work.work);
	enum PP_SMC_POWER_PROFILE profile;
	u32 i, fences = 0;
	int r;

	if (adev->gfx.num_gfx_rings)
		profile = PP_SMC_POWER_PROFILE_FULLSCREEN3D;
	else
		profile = PP_SMC_POWER_PROFILE_COMPUTE;

	for (i = 0; i < AMDGPU_MAX_GFX_RINGS; ++i)
		fences += amdgpu_fence_count_emitted(&adev->gfx.gfx_ring[i]);
	for (i = 0; i < (AMDGPU_MAX_COMPUTE_RINGS * AMDGPU_MAX_GC_INSTANCES); ++i)
		fences += amdgpu_fence_count_emitted(&adev->gfx.compute_ring[i]);
	if (!fences && !atomic_read(&adev->gfx.total_submission_cnt)) {
		mutex_lock(&adev->gfx.workload_profile_mutex);
		if (adev->gfx.workload_profile_active) {
			r = amdgpu_dpm_switch_power_profile(adev, profile, false);
			if (r)
				dev_warn(adev->dev, "(%d) failed to disable %s power profile mode\n", r,
					 profile == PP_SMC_POWER_PROFILE_FULLSCREEN3D ?
					 "fullscreen 3D" : "compute");
			adev->gfx.workload_profile_active = false;
		}
		mutex_unlock(&adev->gfx.workload_profile_mutex);
	} else {
		schedule_delayed_work(&adev->gfx.idle_work, GFX_PROFILE_IDLE_TIMEOUT);
	}
}

void amdgpu_gfx_profile_ring_begin_use(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	enum PP_SMC_POWER_PROFILE profile;
	int r;

	if (adev->gfx.num_gfx_rings)
		profile = PP_SMC_POWER_PROFILE_FULLSCREEN3D;
	else
		profile = PP_SMC_POWER_PROFILE_COMPUTE;

	atomic_inc(&adev->gfx.total_submission_cnt);

	cancel_delayed_work_sync(&adev->gfx.idle_work);

	/* We can safely return early here because we've cancelled the
	 * the delayed work so there is no one else to set it to false
	 * and we don't care if someone else sets it to true.
	 */
	if (adev->gfx.workload_profile_active)
		return;

	mutex_lock(&adev->gfx.workload_profile_mutex);
	if (!adev->gfx.workload_profile_active) {
		r = amdgpu_dpm_switch_power_profile(adev, profile, true);
		if (r)
			dev_warn(adev->dev, "(%d) failed to disable %s power profile mode\n", r,
				 profile == PP_SMC_POWER_PROFILE_FULLSCREEN3D ?
				 "fullscreen 3D" : "compute");
		adev->gfx.workload_profile_active = true;
	}
	mutex_unlock(&adev->gfx.workload_profile_mutex);
}

void amdgpu_gfx_profile_ring_end_use(struct amdgpu_ring *ring)
{
	atomic_dec(&ring->adev->gfx.total_submission_cnt);

	schedule_delayed_work(&ring->adev->gfx.idle_work, GFX_PROFILE_IDLE_TIMEOUT);
}

/*
 * debugfs for to enable/disable gfx job submission to specific core.
 */
#if defined(CONFIG_DEBUG_FS)
static int amdgpu_debugfs_gfx_sched_mask_set(void *data, u64 val)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)data;
	u32 i;
	u64 mask = 0;
	struct amdgpu_ring *ring;

	if (!adev)
		return -ENODEV;

	mask = (1ULL << adev->gfx.num_gfx_rings) - 1;
	if ((val & mask) == 0)
		return -EINVAL;

	for (i = 0; i < adev->gfx.num_gfx_rings; ++i) {
		ring = &adev->gfx.gfx_ring[i];
		if (val & (1 << i))
			ring->sched.ready = true;
		else
			ring->sched.ready = false;
	}
	/* publish sched.ready flag update effective immediately across smp */
	smp_rmb();
	return 0;
}

static int amdgpu_debugfs_gfx_sched_mask_get(void *data, u64 *val)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)data;
	u32 i;
	u64 mask = 0;
	struct amdgpu_ring *ring;

	if (!adev)
		return -ENODEV;
	for (i = 0; i < adev->gfx.num_gfx_rings; ++i) {
		ring = &adev->gfx.gfx_ring[i];
		if (ring->sched.ready)
			mask |= 1ULL << i;
	}

	*val = mask;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(amdgpu_debugfs_gfx_sched_mask_fops,
			 amdgpu_debugfs_gfx_sched_mask_get,
			 amdgpu_debugfs_gfx_sched_mask_set, "%llx\n");

#endif

void amdgpu_debugfs_gfx_sched_mask_init(struct amdgpu_device *adev)
{
#if defined(CONFIG_DEBUG_FS)
	struct drm_minor *minor = adev_to_drm(adev)->primary;
	struct dentry *root = minor->debugfs_root;
	char name[32];

	if (!(adev->gfx.num_gfx_rings > 1))
		return;
	sprintf(name, "amdgpu_gfx_sched_mask");
	debugfs_create_file(name, 0600, root, adev,
			    &amdgpu_debugfs_gfx_sched_mask_fops);
#endif
}

/*
 * debugfs for to enable/disable compute job submission to specific core.
 */
#if defined(CONFIG_DEBUG_FS)
static int amdgpu_debugfs_compute_sched_mask_set(void *data, u64 val)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)data;
	u32 i;
	u64 mask = 0;
	struct amdgpu_ring *ring;

	if (!adev)
		return -ENODEV;

	mask = (1ULL << adev->gfx.num_compute_rings) - 1;
	if ((val & mask) == 0)
		return -EINVAL;

	for (i = 0; i < adev->gfx.num_compute_rings; ++i) {
		ring = &adev->gfx.compute_ring[i];
		if (val & (1 << i))
			ring->sched.ready = true;
		else
			ring->sched.ready = false;
	}

	/* publish sched.ready flag update effective immediately across smp */
	smp_rmb();
	return 0;
}

static int amdgpu_debugfs_compute_sched_mask_get(void *data, u64 *val)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)data;
	u32 i;
	u64 mask = 0;
	struct amdgpu_ring *ring;

	if (!adev)
		return -ENODEV;
	for (i = 0; i < adev->gfx.num_compute_rings; ++i) {
		ring = &adev->gfx.compute_ring[i];
		if (ring->sched.ready)
			mask |= 1ULL << i;
	}

	*val = mask;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(amdgpu_debugfs_compute_sched_mask_fops,
			 amdgpu_debugfs_compute_sched_mask_get,
			 amdgpu_debugfs_compute_sched_mask_set, "%llx\n");

#endif

void amdgpu_debugfs_compute_sched_mask_init(struct amdgpu_device *adev)
{
#if defined(CONFIG_DEBUG_FS)
	struct drm_minor *minor = adev_to_drm(adev)->primary;
	struct dentry *root = minor->debugfs_root;
	char name[32];

	if (!(adev->gfx.num_compute_rings > 1))
		return;
	sprintf(name, "amdgpu_compute_sched_mask");
	debugfs_create_file(name, 0600, root, adev,
			    &amdgpu_debugfs_compute_sched_mask_fops);
#endif
}
