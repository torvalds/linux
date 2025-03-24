/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
#ifndef __AMDGPU_JOB_H__
#define __AMDGPU_JOB_H__

#include <drm/gpu_scheduler.h>
#include "amdgpu_sync.h"
#include "amdgpu_ring.h"

/* bit set means command submit involves a preamble IB */
#define AMDGPU_PREAMBLE_IB_PRESENT          (1 << 0)
/* bit set means preamble IB is first presented in belonging context */
#define AMDGPU_PREAMBLE_IB_PRESENT_FIRST    (1 << 1)
/* bit set means context switch occured */
#define AMDGPU_HAVE_CTX_SWITCH              (1 << 2)
/* bit set means IB is preempted */
#define AMDGPU_IB_PREEMPTED                 (1 << 3)

#define to_amdgpu_job(sched_job)		\
		container_of((sched_job), struct amdgpu_job, base)

#define AMDGPU_JOB_GET_VMID(job) ((job) ? (job)->vmid : 0)

struct amdgpu_fence;
enum amdgpu_ib_pool_type;

struct amdgpu_job {
	struct drm_sched_job    base;
	struct amdgpu_vm	*vm;
	struct amdgpu_sync	explicit_sync;
	struct dma_fence	hw_fence;
	struct dma_fence	*gang_submit;
	uint32_t		preamble_status;
	uint32_t                preemption_status;
	bool                    vm_needs_flush;
	bool			gds_switch_needed;
	bool			spm_update_needed;
	uint64_t		vm_pd_addr;
	unsigned		vmid;
	unsigned		pasid;
	uint32_t		gds_base, gds_size;
	uint32_t		gws_base, gws_size;
	uint32_t		oa_base, oa_size;
	uint64_t		generation;

	/* user fence handling */
	uint64_t		uf_addr;
	uint64_t		uf_sequence;

	/* virtual addresses for shadow/GDS/CSA */
	uint64_t		shadow_va;
	uint64_t		csa_va;
	uint64_t		gds_va;
	bool			init_shadow;

	/* job_run_counter >= 1 means a resubmit job */
	uint32_t		job_run_counter;

	/* enforce isolation */
	bool			enforce_isolation;

	uint32_t		num_ibs;
	struct amdgpu_ib	ibs[];
};

static inline struct amdgpu_ring *amdgpu_job_ring(struct amdgpu_job *job)
{
	return to_amdgpu_ring(job->base.entity->rq->sched);
}

int amdgpu_job_alloc(struct amdgpu_device *adev, struct amdgpu_vm *vm,
		     struct drm_sched_entity *entity, void *owner,
		     unsigned int num_ibs, struct amdgpu_job **job);
int amdgpu_job_alloc_with_ib(struct amdgpu_device *adev,
			     struct drm_sched_entity *entity, void *owner,
			     size_t size, enum amdgpu_ib_pool_type pool_type,
			     struct amdgpu_job **job);
void amdgpu_job_set_resources(struct amdgpu_job *job, struct amdgpu_bo *gds,
			      struct amdgpu_bo *gws, struct amdgpu_bo *oa);
void amdgpu_job_free_resources(struct amdgpu_job *job);
void amdgpu_job_set_gang_leader(struct amdgpu_job *job,
				struct amdgpu_job *leader);
void amdgpu_job_free(struct amdgpu_job *job);
struct dma_fence *amdgpu_job_submit(struct amdgpu_job *job);
int amdgpu_job_submit_direct(struct amdgpu_job *job, struct amdgpu_ring *ring,
			     struct dma_fence **fence);

void amdgpu_job_stop_all_jobs_on_sched(struct drm_gpu_scheduler *sched);

#endif
