/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/* Copyright 2017-2019 Qiang Yu <yuq825@gmail.com> */

#ifndef __LIMA_SCHED_H__
#define __LIMA_SCHED_H__

#include <drm/gpu_scheduler.h>
#include <linux/list.h>
#include <linux/xarray.h>

struct lima_device;
struct lima_vm;

struct lima_sched_error_task {
	struct list_head list;
	void *data;
	u32 size;
};

struct lima_sched_task {
	struct drm_sched_job base;

	struct lima_vm *vm;
	void *frame;

	struct lima_bo **bos;
	int num_bos;

	bool recoverable;
	struct lima_bo *heap;

	/* pipe fence */
	struct dma_fence *fence;
};

struct lima_sched_context {
	struct drm_sched_entity base;
};

#define LIMA_SCHED_PIPE_MAX_MMU       8
#define LIMA_SCHED_PIPE_MAX_L2_CACHE  2
#define LIMA_SCHED_PIPE_MAX_PROCESSOR 8

struct lima_ip;

struct lima_sched_pipe {
	struct drm_gpu_scheduler base;

	u64 fence_context;
	u32 fence_seqno;
	spinlock_t fence_lock;

	struct lima_device *ldev;

	struct lima_sched_task *current_task;
	struct lima_vm *current_vm;

	struct lima_ip *mmu[LIMA_SCHED_PIPE_MAX_MMU];
	int num_mmu;

	struct lima_ip *l2_cache[LIMA_SCHED_PIPE_MAX_L2_CACHE];
	int num_l2_cache;

	struct lima_ip *processor[LIMA_SCHED_PIPE_MAX_PROCESSOR];
	int num_processor;

	struct lima_ip *bcast_processor;
	struct lima_ip *bcast_mmu;

	u32 done;
	bool error;
	atomic_t task;

	int frame_size;
	struct kmem_cache *task_slab;

	int (*task_validate)(struct lima_sched_pipe *pipe, struct lima_sched_task *task);
	void (*task_run)(struct lima_sched_pipe *pipe, struct lima_sched_task *task);
	void (*task_fini)(struct lima_sched_pipe *pipe);
	void (*task_error)(struct lima_sched_pipe *pipe);
	void (*task_mmu_error)(struct lima_sched_pipe *pipe);
	int (*task_recover)(struct lima_sched_pipe *pipe);

	struct work_struct recover_work;
};

int lima_sched_task_init(struct lima_sched_task *task,
			 struct lima_sched_context *context,
			 struct lima_bo **bos, int num_bos,
			 struct lima_vm *vm);
void lima_sched_task_fini(struct lima_sched_task *task);

int lima_sched_context_init(struct lima_sched_pipe *pipe,
			    struct lima_sched_context *context,
			    atomic_t *guilty);
void lima_sched_context_fini(struct lima_sched_pipe *pipe,
			     struct lima_sched_context *context);
struct dma_fence *lima_sched_context_queue_task(struct lima_sched_task *task);

int lima_sched_pipe_init(struct lima_sched_pipe *pipe, const char *name);
void lima_sched_pipe_fini(struct lima_sched_pipe *pipe);
void lima_sched_pipe_task_done(struct lima_sched_pipe *pipe);

static inline void lima_sched_pipe_mmu_error(struct lima_sched_pipe *pipe)
{
	pipe->error = true;
	pipe->task_mmu_error(pipe);
}

int lima_sched_slab_init(void);
void lima_sched_slab_fini(void);

#endif
