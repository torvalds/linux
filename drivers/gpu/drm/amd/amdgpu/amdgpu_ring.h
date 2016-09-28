/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
 * Authors: Christian König
 */
#ifndef __AMDGPU_RING_H__
#define __AMDGPU_RING_H__

#include "gpu_scheduler.h"

/* max number of rings */
#define AMDGPU_MAX_RINGS		16
#define AMDGPU_MAX_GFX_RINGS		1
#define AMDGPU_MAX_COMPUTE_RINGS	8
#define AMDGPU_MAX_VCE_RINGS		3

/* some special values for the owner field */
#define AMDGPU_FENCE_OWNER_UNDEFINED	((void*)0ul)
#define AMDGPU_FENCE_OWNER_VM		((void*)1ul)

#define AMDGPU_FENCE_FLAG_64BIT         (1 << 0)
#define AMDGPU_FENCE_FLAG_INT           (1 << 1)

enum amdgpu_ring_type {
	AMDGPU_RING_TYPE_GFX,
	AMDGPU_RING_TYPE_COMPUTE,
	AMDGPU_RING_TYPE_SDMA,
	AMDGPU_RING_TYPE_UVD,
	AMDGPU_RING_TYPE_VCE
};

struct amdgpu_device;
struct amdgpu_ring;
struct amdgpu_ib;
struct amdgpu_cs_parser;

/*
 * Fences.
 */
struct amdgpu_fence_driver {
	uint64_t			gpu_addr;
	volatile uint32_t		*cpu_addr;
	/* sync_seq is protected by ring emission lock */
	uint32_t			sync_seq;
	atomic_t			last_seq;
	bool				initialized;
	struct amdgpu_irq_src		*irq_src;
	unsigned			irq_type;
	struct timer_list		fallback_timer;
	unsigned			num_fences_mask;
	spinlock_t			lock;
	struct fence			**fences;
};

int amdgpu_fence_driver_init(struct amdgpu_device *adev);
void amdgpu_fence_driver_fini(struct amdgpu_device *adev);
void amdgpu_fence_driver_force_completion(struct amdgpu_device *adev);

int amdgpu_fence_driver_init_ring(struct amdgpu_ring *ring,
				  unsigned num_hw_submission);
int amdgpu_fence_driver_start_ring(struct amdgpu_ring *ring,
				   struct amdgpu_irq_src *irq_src,
				   unsigned irq_type);
void amdgpu_fence_driver_suspend(struct amdgpu_device *adev);
void amdgpu_fence_driver_resume(struct amdgpu_device *adev);
int amdgpu_fence_emit(struct amdgpu_ring *ring, struct fence **fence);
void amdgpu_fence_process(struct amdgpu_ring *ring);
int amdgpu_fence_wait_empty(struct amdgpu_ring *ring);
unsigned amdgpu_fence_count_emitted(struct amdgpu_ring *ring);

/*
 * Rings.
 */

/* provided by hw blocks that expose a ring buffer for commands */
struct amdgpu_ring_funcs {
	/* ring read/write ptr handling */
	u32 (*get_rptr)(struct amdgpu_ring *ring);
	u32 (*get_wptr)(struct amdgpu_ring *ring);
	void (*set_wptr)(struct amdgpu_ring *ring);
	/* validating and patching of IBs */
	int (*parse_cs)(struct amdgpu_cs_parser *p, uint32_t ib_idx);
	/* command emit functions */
	void (*emit_ib)(struct amdgpu_ring *ring,
			struct amdgpu_ib *ib,
			unsigned vm_id, bool ctx_switch);
	void (*emit_fence)(struct amdgpu_ring *ring, uint64_t addr,
			   uint64_t seq, unsigned flags);
	void (*emit_pipeline_sync)(struct amdgpu_ring *ring);
	void (*emit_vm_flush)(struct amdgpu_ring *ring, unsigned vm_id,
			      uint64_t pd_addr);
	void (*emit_hdp_flush)(struct amdgpu_ring *ring);
	void (*emit_hdp_invalidate)(struct amdgpu_ring *ring);
	void (*emit_gds_switch)(struct amdgpu_ring *ring, uint32_t vmid,
				uint32_t gds_base, uint32_t gds_size,
				uint32_t gws_base, uint32_t gws_size,
				uint32_t oa_base, uint32_t oa_size);
	/* testing functions */
	int (*test_ring)(struct amdgpu_ring *ring);
	int (*test_ib)(struct amdgpu_ring *ring, long timeout);
	/* insert NOP packets */
	void (*insert_nop)(struct amdgpu_ring *ring, uint32_t count);
	/* pad the indirect buffer to the necessary number of dw */
	void (*pad_ib)(struct amdgpu_ring *ring, struct amdgpu_ib *ib);
	unsigned (*init_cond_exec)(struct amdgpu_ring *ring);
	void (*patch_cond_exec)(struct amdgpu_ring *ring, unsigned offset);
	/* note usage for clock and power gating */
	void (*begin_use)(struct amdgpu_ring *ring);
	void (*end_use)(struct amdgpu_ring *ring);
	void (*emit_switch_buffer) (struct amdgpu_ring *ring);
	void (*emit_cntxcntl) (struct amdgpu_ring *ring, uint32_t flags);
	unsigned (*get_emit_ib_size) (struct amdgpu_ring *ring);
	unsigned (*get_dma_frame_size) (struct amdgpu_ring *ring);
};

struct amdgpu_ring {
	struct amdgpu_device		*adev;
	const struct amdgpu_ring_funcs	*funcs;
	struct amdgpu_fence_driver	fence_drv;
	struct amd_gpu_scheduler	sched;

	struct amdgpu_bo	*ring_obj;
	volatile uint32_t	*ring;
	unsigned		rptr_offs;
	unsigned		wptr;
	unsigned		wptr_old;
	unsigned		ring_size;
	unsigned		max_dw;
	int			count_dw;
	uint64_t		gpu_addr;
	uint32_t		align_mask;
	uint32_t		ptr_mask;
	bool			ready;
	u32			nop;
	u32			idx;
	u32			me;
	u32			pipe;
	u32			queue;
	struct amdgpu_bo	*mqd_obj;
	u32			doorbell_index;
	bool			use_doorbell;
	unsigned		wptr_offs;
	unsigned		fence_offs;
	uint64_t		current_ctx;
	enum amdgpu_ring_type	type;
	char			name[16];
	unsigned		cond_exe_offs;
	u64			cond_exe_gpu_addr;
	volatile u32		*cond_exe_cpu_addr;
#if defined(CONFIG_DEBUG_FS)
	struct dentry *ent;
#endif
};

int amdgpu_ring_alloc(struct amdgpu_ring *ring, unsigned ndw);
void amdgpu_ring_insert_nop(struct amdgpu_ring *ring, uint32_t count);
void amdgpu_ring_generic_pad_ib(struct amdgpu_ring *ring, struct amdgpu_ib *ib);
void amdgpu_ring_commit(struct amdgpu_ring *ring);
void amdgpu_ring_undo(struct amdgpu_ring *ring);
int amdgpu_ring_init(struct amdgpu_device *adev, struct amdgpu_ring *ring,
		     unsigned ring_size, u32 nop, u32 align_mask,
		     struct amdgpu_irq_src *irq_src, unsigned irq_type,
		     enum amdgpu_ring_type ring_type);
void amdgpu_ring_fini(struct amdgpu_ring *ring);

#endif
