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

#include <drm/amdgpu_drm.h>
#include <drm/gpu_scheduler.h>
#include <drm/drm_print.h>

/* max number of rings */
#define AMDGPU_MAX_RINGS		21
#define AMDGPU_MAX_GFX_RINGS		1
#define AMDGPU_MAX_COMPUTE_RINGS	8
#define AMDGPU_MAX_VCE_RINGS		3
#define AMDGPU_MAX_UVD_ENC_RINGS	2

/* some special values for the owner field */
#define AMDGPU_FENCE_OWNER_UNDEFINED	((void *)0ul)
#define AMDGPU_FENCE_OWNER_VM		((void *)1ul)
#define AMDGPU_FENCE_OWNER_KFD		((void *)2ul)

#define AMDGPU_FENCE_FLAG_64BIT         (1 << 0)
#define AMDGPU_FENCE_FLAG_INT           (1 << 1)
#define AMDGPU_FENCE_FLAG_TC_WB_ONLY    (1 << 2)

#define to_amdgpu_ring(s) container_of((s), struct amdgpu_ring, sched)

enum amdgpu_ring_type {
	AMDGPU_RING_TYPE_GFX,
	AMDGPU_RING_TYPE_COMPUTE,
	AMDGPU_RING_TYPE_SDMA,
	AMDGPU_RING_TYPE_UVD,
	AMDGPU_RING_TYPE_VCE,
	AMDGPU_RING_TYPE_KIQ,
	AMDGPU_RING_TYPE_UVD_ENC,
	AMDGPU_RING_TYPE_VCN_DEC,
	AMDGPU_RING_TYPE_VCN_ENC,
	AMDGPU_RING_TYPE_VCN_JPEG
};

struct amdgpu_device;
struct amdgpu_ring;
struct amdgpu_ib;
struct amdgpu_cs_parser;
struct amdgpu_job;

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
	struct dma_fence		**fences;
};

int amdgpu_fence_driver_init(struct amdgpu_device *adev);
void amdgpu_fence_driver_fini(struct amdgpu_device *adev);
void amdgpu_fence_driver_force_completion(struct amdgpu_ring *ring);

int amdgpu_fence_driver_init_ring(struct amdgpu_ring *ring,
				  unsigned num_hw_submission);
int amdgpu_fence_driver_start_ring(struct amdgpu_ring *ring,
				   struct amdgpu_irq_src *irq_src,
				   unsigned irq_type);
void amdgpu_fence_driver_suspend(struct amdgpu_device *adev);
void amdgpu_fence_driver_resume(struct amdgpu_device *adev);
int amdgpu_fence_emit(struct amdgpu_ring *ring, struct dma_fence **fence,
		      unsigned flags);
int amdgpu_fence_emit_polling(struct amdgpu_ring *ring, uint32_t *s);
bool amdgpu_fence_process(struct amdgpu_ring *ring);
int amdgpu_fence_wait_empty(struct amdgpu_ring *ring);
signed long amdgpu_fence_wait_polling(struct amdgpu_ring *ring,
				      uint32_t wait_seq,
				      signed long timeout);
unsigned amdgpu_fence_count_emitted(struct amdgpu_ring *ring);

/*
 * Rings.
 */

/* provided by hw blocks that expose a ring buffer for commands */
struct amdgpu_ring_funcs {
	enum amdgpu_ring_type	type;
	uint32_t		align_mask;
	u32			nop;
	bool			support_64bit_ptrs;
	unsigned		vmhub;
	unsigned		extra_dw;

	/* ring read/write ptr handling */
	u64 (*get_rptr)(struct amdgpu_ring *ring);
	u64 (*get_wptr)(struct amdgpu_ring *ring);
	void (*set_wptr)(struct amdgpu_ring *ring);
	/* validating and patching of IBs */
	int (*parse_cs)(struct amdgpu_cs_parser *p, uint32_t ib_idx);
	int (*patch_cs_in_place)(struct amdgpu_cs_parser *p, uint32_t ib_idx);
	/* constants to calculate how many DW are needed for an emit */
	unsigned emit_frame_size;
	unsigned emit_ib_size;
	/* command emit functions */
	void (*emit_ib)(struct amdgpu_ring *ring,
			struct amdgpu_job *job,
			struct amdgpu_ib *ib,
			bool ctx_switch);
	void (*emit_fence)(struct amdgpu_ring *ring, uint64_t addr,
			   uint64_t seq, unsigned flags);
	void (*emit_pipeline_sync)(struct amdgpu_ring *ring);
	void (*emit_vm_flush)(struct amdgpu_ring *ring, unsigned vmid,
			      uint64_t pd_addr);
	void (*emit_hdp_flush)(struct amdgpu_ring *ring);
	void (*emit_gds_switch)(struct amdgpu_ring *ring, uint32_t vmid,
				uint32_t gds_base, uint32_t gds_size,
				uint32_t gws_base, uint32_t gws_size,
				uint32_t oa_base, uint32_t oa_size);
	/* testing functions */
	int (*test_ring)(struct amdgpu_ring *ring);
	int (*test_ib)(struct amdgpu_ring *ring, long timeout);
	/* insert NOP packets */
	void (*insert_nop)(struct amdgpu_ring *ring, uint32_t count);
	void (*insert_start)(struct amdgpu_ring *ring);
	void (*insert_end)(struct amdgpu_ring *ring);
	/* pad the indirect buffer to the necessary number of dw */
	void (*pad_ib)(struct amdgpu_ring *ring, struct amdgpu_ib *ib);
	unsigned (*init_cond_exec)(struct amdgpu_ring *ring);
	void (*patch_cond_exec)(struct amdgpu_ring *ring, unsigned offset);
	/* note usage for clock and power gating */
	void (*begin_use)(struct amdgpu_ring *ring);
	void (*end_use)(struct amdgpu_ring *ring);
	void (*emit_switch_buffer) (struct amdgpu_ring *ring);
	void (*emit_cntxcntl) (struct amdgpu_ring *ring, uint32_t flags);
	void (*emit_rreg)(struct amdgpu_ring *ring, uint32_t reg);
	void (*emit_wreg)(struct amdgpu_ring *ring, uint32_t reg, uint32_t val);
	void (*emit_reg_wait)(struct amdgpu_ring *ring, uint32_t reg,
			      uint32_t val, uint32_t mask);
	void (*emit_reg_write_reg_wait)(struct amdgpu_ring *ring,
					uint32_t reg0, uint32_t reg1,
					uint32_t ref, uint32_t mask);
	void (*emit_tmz)(struct amdgpu_ring *ring, bool start);
	/* priority functions */
	void (*set_priority) (struct amdgpu_ring *ring,
			      enum drm_sched_priority priority);
	/* Try to soft recover the ring to make the fence signal */
	void (*soft_recovery)(struct amdgpu_ring *ring, unsigned vmid);
};

struct amdgpu_ring {
	struct amdgpu_device		*adev;
	const struct amdgpu_ring_funcs	*funcs;
	struct amdgpu_fence_driver	fence_drv;
	struct drm_gpu_scheduler	sched;

	struct amdgpu_bo	*ring_obj;
	volatile uint32_t	*ring;
	unsigned		rptr_offs;
	u64			wptr;
	u64			wptr_old;
	unsigned		ring_size;
	unsigned		max_dw;
	int			count_dw;
	uint64_t		gpu_addr;
	uint64_t		ptr_mask;
	uint32_t		buf_mask;
	u32			idx;
	u32			me;
	u32			pipe;
	u32			queue;
	struct amdgpu_bo	*mqd_obj;
	uint64_t                mqd_gpu_addr;
	void                    *mqd_ptr;
	uint64_t                eop_gpu_addr;
	u32			doorbell_index;
	bool			use_doorbell;
	bool			use_pollmem;
	unsigned		wptr_offs;
	unsigned		fence_offs;
	uint64_t		current_ctx;
	char			name[16];
	unsigned		cond_exe_offs;
	u64			cond_exe_gpu_addr;
	volatile u32		*cond_exe_cpu_addr;
	unsigned		vm_inv_eng;
	struct dma_fence	*vmid_wait;
	bool			has_compute_vm_bug;

	atomic_t		num_jobs[DRM_SCHED_PRIORITY_MAX];
	struct mutex		priority_mutex;
	/* protected by priority_mutex */
	int			priority;

#if defined(CONFIG_DEBUG_FS)
	struct dentry *ent;
#endif
};

#define amdgpu_ring_parse_cs(r, p, ib) ((r)->funcs->parse_cs((p), (ib)))
#define amdgpu_ring_patch_cs_in_place(r, p, ib) ((r)->funcs->patch_cs_in_place((p), (ib)))
#define amdgpu_ring_test_ring(r) (r)->funcs->test_ring((r))
#define amdgpu_ring_test_ib(r, t) (r)->funcs->test_ib((r), (t))
#define amdgpu_ring_get_rptr(r) (r)->funcs->get_rptr((r))
#define amdgpu_ring_get_wptr(r) (r)->funcs->get_wptr((r))
#define amdgpu_ring_set_wptr(r) (r)->funcs->set_wptr((r))
#define amdgpu_ring_emit_ib(r, job, ib, c) ((r)->funcs->emit_ib((r), (job), (ib), (c)))
#define amdgpu_ring_emit_pipeline_sync(r) (r)->funcs->emit_pipeline_sync((r))
#define amdgpu_ring_emit_vm_flush(r, vmid, addr) (r)->funcs->emit_vm_flush((r), (vmid), (addr))
#define amdgpu_ring_emit_fence(r, addr, seq, flags) (r)->funcs->emit_fence((r), (addr), (seq), (flags))
#define amdgpu_ring_emit_gds_switch(r, v, db, ds, wb, ws, ab, as) (r)->funcs->emit_gds_switch((r), (v), (db), (ds), (wb), (ws), (ab), (as))
#define amdgpu_ring_emit_hdp_flush(r) (r)->funcs->emit_hdp_flush((r))
#define amdgpu_ring_emit_switch_buffer(r) (r)->funcs->emit_switch_buffer((r))
#define amdgpu_ring_emit_cntxcntl(r, d) (r)->funcs->emit_cntxcntl((r), (d))
#define amdgpu_ring_emit_rreg(r, d) (r)->funcs->emit_rreg((r), (d))
#define amdgpu_ring_emit_wreg(r, d, v) (r)->funcs->emit_wreg((r), (d), (v))
#define amdgpu_ring_emit_reg_wait(r, d, v, m) (r)->funcs->emit_reg_wait((r), (d), (v), (m))
#define amdgpu_ring_emit_reg_write_reg_wait(r, d0, d1, v, m) (r)->funcs->emit_reg_write_reg_wait((r), (d0), (d1), (v), (m))
#define amdgpu_ring_emit_tmz(r, b) (r)->funcs->emit_tmz((r), (b))
#define amdgpu_ring_pad_ib(r, ib) ((r)->funcs->pad_ib((r), (ib)))
#define amdgpu_ring_init_cond_exec(r) (r)->funcs->init_cond_exec((r))
#define amdgpu_ring_patch_cond_exec(r,o) (r)->funcs->patch_cond_exec((r),(o))

int amdgpu_ring_alloc(struct amdgpu_ring *ring, unsigned ndw);
void amdgpu_ring_insert_nop(struct amdgpu_ring *ring, uint32_t count);
void amdgpu_ring_generic_pad_ib(struct amdgpu_ring *ring, struct amdgpu_ib *ib);
void amdgpu_ring_commit(struct amdgpu_ring *ring);
void amdgpu_ring_undo(struct amdgpu_ring *ring);
void amdgpu_ring_priority_get(struct amdgpu_ring *ring,
			      enum drm_sched_priority priority);
void amdgpu_ring_priority_put(struct amdgpu_ring *ring,
			      enum drm_sched_priority priority);
int amdgpu_ring_init(struct amdgpu_device *adev, struct amdgpu_ring *ring,
		     unsigned ring_size, struct amdgpu_irq_src *irq_src,
		     unsigned irq_type);
void amdgpu_ring_fini(struct amdgpu_ring *ring);
void amdgpu_ring_emit_reg_write_reg_wait_helper(struct amdgpu_ring *ring,
						uint32_t reg0, uint32_t val0,
						uint32_t reg1, uint32_t val1);
bool amdgpu_ring_soft_recovery(struct amdgpu_ring *ring, unsigned int vmid,
			       struct dma_fence *fence);

static inline void amdgpu_ring_clear_ring(struct amdgpu_ring *ring)
{
	int i = 0;
	while (i <= ring->buf_mask)
		ring->ring[i++] = ring->funcs->nop;

}

static inline void amdgpu_ring_write(struct amdgpu_ring *ring, uint32_t v)
{
	if (ring->count_dw <= 0)
		DRM_ERROR("amdgpu: writing more dwords to the ring than expected!\n");
	ring->ring[ring->wptr++ & ring->buf_mask] = v;
	ring->wptr &= ring->ptr_mask;
	ring->count_dw--;
}

static inline void amdgpu_ring_write_multiple(struct amdgpu_ring *ring,
					      void *src, int count_dw)
{
	unsigned occupied, chunk1, chunk2;
	void *dst;

	if (unlikely(ring->count_dw < count_dw))
		DRM_ERROR("amdgpu: writing more dwords to the ring than expected!\n");

	occupied = ring->wptr & ring->buf_mask;
	dst = (void *)&ring->ring[occupied];
	chunk1 = ring->buf_mask + 1 - occupied;
	chunk1 = (chunk1 >= count_dw) ? count_dw: chunk1;
	chunk2 = count_dw - chunk1;
	chunk1 <<= 2;
	chunk2 <<= 2;

	if (chunk1)
		memcpy(dst, src, chunk1);

	if (chunk2) {
		src += chunk1;
		dst = (void *)ring->ring;
		memcpy(dst, src, chunk2);
	}

	ring->wptr += count_dw;
	ring->wptr &= ring->ptr_mask;
	ring->count_dw -= count_dw;
}

int amdgpu_ring_test_helper(struct amdgpu_ring *ring);

#endif
