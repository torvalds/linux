/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2017, 2019 The Linux Foundation. All rights reserved. */

#ifndef __A6XX_GPU_H__
#define __A6XX_GPU_H__


#include "adreno_gpu.h"
#include "a6xx_enums.xml.h"
#include "a7xx_enums.xml.h"
#include "a6xx_perfcntrs.xml.h"
#include "a7xx_perfcntrs.xml.h"
#include "a6xx.xml.h"

#include "a6xx_gmu.h"

extern bool hang_debug;

struct cpu_gpu_lock {
	uint32_t gpu_req;
	uint32_t cpu_req;
	uint32_t turn;
	union {
		struct {
			uint16_t list_length;
			uint16_t list_offset;
		};
		struct {
			uint8_t ifpc_list_len;
			uint8_t preemption_list_len;
			uint16_t dynamic_list_len;
		};
	};
	uint64_t regs[62];
};

/**
 * struct a6xx_info - a6xx specific information from device table
 *
 * @hwcg: hw clock gating register sequence
 * @protect: CP_PROTECT settings
 * @pwrup_reglist pwrup reglist for preemption
 */
struct a6xx_info {
	const struct adreno_reglist *hwcg;
	const struct adreno_protect *protect;
	const struct adreno_reglist_list *pwrup_reglist;
	const struct adreno_reglist_list *ifpc_reglist;
	u32 gmu_chipid;
	u32 gmu_cgc_mode;
	u32 prim_fifo_threshold;
	const struct a6xx_bcm *bcms;
};

struct a6xx_gpu {
	struct adreno_gpu base;

	struct drm_gem_object *sqe_bo;
	uint64_t sqe_iova;

	struct msm_ringbuffer *cur_ring;
	struct msm_ringbuffer *next_ring;

	struct drm_gem_object *preempt_bo[MSM_GPU_MAX_RINGS];
	void *preempt[MSM_GPU_MAX_RINGS];
	uint64_t preempt_iova[MSM_GPU_MAX_RINGS];
	struct drm_gem_object *preempt_smmu_bo[MSM_GPU_MAX_RINGS];
	void *preempt_smmu[MSM_GPU_MAX_RINGS];
	uint64_t preempt_smmu_iova[MSM_GPU_MAX_RINGS];
	uint32_t last_seqno[MSM_GPU_MAX_RINGS];

	atomic_t preempt_state;
	spinlock_t eval_lock;
	struct timer_list preempt_timer;

	unsigned int preempt_level;
	bool uses_gmem;
	bool skip_save_restore;

	struct drm_gem_object *preempt_postamble_bo;
	void *preempt_postamble_ptr;
	uint64_t preempt_postamble_iova;
	uint64_t preempt_postamble_len;
	bool postamble_enabled;

	struct a6xx_gmu gmu;

	struct drm_gem_object *shadow_bo;
	uint64_t shadow_iova;
	uint32_t *shadow;

	struct drm_gem_object *pwrup_reglist_bo;
	void *pwrup_reglist_ptr;
	uint64_t pwrup_reglist_iova;
	bool pwrup_reglist_emitted;

	bool has_whereami;

	void __iomem *llc_mmio;
	void *llc_slice;
	void *htw_llc_slice;
	bool have_mmu500;
	bool hung;
};

#define to_a6xx_gpu(x) container_of(x, struct a6xx_gpu, base)

/*
 * In order to do lockless preemption we use a simple state machine to progress
 * through the process.
 *
 * PREEMPT_NONE - no preemption in progress.  Next state START.
 * PREEMPT_START - The trigger is evaluating if preemption is possible. Next
 * states: TRIGGERED, NONE
 * PREEMPT_FINISH - An intermediate state before moving back to NONE. Next
 * state: NONE.
 * PREEMPT_TRIGGERED: A preemption has been executed on the hardware. Next
 * states: FAULTED, PENDING
 * PREEMPT_FAULTED: A preemption timed out (never completed). This will trigger
 * recovery.  Next state: N/A
 * PREEMPT_PENDING: Preemption complete interrupt fired - the callback is
 * checking the success of the operation. Next state: FAULTED, NONE.
 */

enum a6xx_preempt_state {
	PREEMPT_NONE = 0,
	PREEMPT_START,
	PREEMPT_FINISH,
	PREEMPT_TRIGGERED,
	PREEMPT_FAULTED,
	PREEMPT_PENDING,
};

/*
 * struct a6xx_preempt_record is a shared buffer between the microcode and the
 * CPU to store the state for preemption. The record itself is much larger
 * (2112k) but most of that is used by the CP for storage.
 *
 * There is a preemption record assigned per ringbuffer. When the CPU triggers a
 * preemption, it fills out the record with the useful information (wptr, ring
 * base, etc) and the microcode uses that information to set up the CP following
 * the preemption.  When a ring is switched out, the CP will save the ringbuffer
 * state back to the record. In this way, once the records are properly set up
 * the CPU can quickly switch back and forth between ringbuffers by only
 * updating a few registers (often only the wptr).
 *
 * These are the CPU aware registers in the record:
 * @magic: Must always be 0xAE399D6EUL
 * @info: Type of the record - written 0 by the CPU, updated by the CP
 * @errno: preemption error record
 * @data: Data field in YIELD and SET_MARKER packets, Written and used by CP
 * @cntl: Value of RB_CNTL written by CPU, save/restored by CP
 * @rptr: Value of RB_RPTR written by CPU, save/restored by CP
 * @wptr: Value of RB_WPTR written by CPU, save/restored by CP
 * @_pad: Reserved/padding
 * @rptr_addr: Value of RB_RPTR_ADDR_LO|HI written by CPU, save/restored by CP
 * @rbase: Value of RB_BASE written by CPU, save/restored by CP
 * @counter: GPU address of the storage area for the preemption counters
 * @bv_rptr_addr: Value of BV_RB_RPTR_ADDR_LO|HI written by CPU, save/restored by CP
 */
struct a6xx_preempt_record {
	u32 magic;
	u32 info;
	u32 errno;
	u32 data;
	u32 cntl;
	u32 rptr;
	u32 wptr;
	u32 _pad;
	u64 rptr_addr;
	u64 rbase;
	u64 counter;
	u64 bv_rptr_addr;
};

#define A6XX_PREEMPT_RECORD_MAGIC 0xAE399D6EUL

#define PREEMPT_SMMU_INFO_SIZE 4096

#define PREEMPT_RECORD_SIZE(adreno_gpu) \
	((adreno_gpu->info->preempt_record_size) == 0 ? \
	 4192 * SZ_1K : (adreno_gpu->info->preempt_record_size))

/*
 * The preemption counter block is a storage area for the value of the
 * preemption counters that are saved immediately before context switch. We
 * append it on to the end of the allocation for the preemption record.
 */
#define A6XX_PREEMPT_COUNTER_SIZE (16 * 4)

struct a7xx_cp_smmu_info {
	u32 magic;
	u32 _pad4;
	u64 ttbr0;
	u32 asid;
	u32 context_idr;
	u32 context_bank;
};

#define GEN7_CP_SMMU_INFO_MAGIC 0x241350d5UL

/*
 * Given a register and a count, return a value to program into
 * REG_CP_PROTECT_REG(n) - this will block both reads and writes for
 * _len + 1 registers starting at _reg.
 */
#define A6XX_PROTECT_NORDWR(_reg, _len) \
	((1 << 31) | \
	(((_len) & 0x3FFF) << 18) | ((_reg) & 0x3FFFF))

/*
 * Same as above, but allow reads over the range. For areas of mixed use (such
 * as performance counters) this allows us to protect a much larger range with a
 * single register
 */
#define A6XX_PROTECT_RDONLY(_reg, _len) \
	((((_len) & 0x3FFF) << 18) | ((_reg) & 0x3FFFF))

static inline bool a6xx_has_gbif(struct adreno_gpu *gpu)
{
	if(adreno_is_a630(gpu))
		return false;

	return true;
}

static inline void a6xx_llc_rmw(struct a6xx_gpu *a6xx_gpu, u32 reg, u32 mask, u32 or)
{
	return msm_rmw(a6xx_gpu->llc_mmio + (reg << 2), mask, or);
}

static inline u32 a6xx_llc_read(struct a6xx_gpu *a6xx_gpu, u32 reg)
{
	return readl(a6xx_gpu->llc_mmio + (reg << 2));
}

static inline void a6xx_llc_write(struct a6xx_gpu *a6xx_gpu, u32 reg, u32 value)
{
	writel(value, a6xx_gpu->llc_mmio + (reg << 2));
}

#define shadowptr(_a6xx_gpu, _ring) ((_a6xx_gpu)->shadow_iova + \
		((_ring)->id * sizeof(uint32_t)))

int a6xx_gmu_resume(struct a6xx_gpu *gpu);
int a6xx_gmu_stop(struct a6xx_gpu *gpu);

int a6xx_gmu_wait_for_idle(struct a6xx_gmu *gmu);

bool a6xx_gmu_isidle(struct a6xx_gmu *gmu);

int a6xx_gmu_set_oob(struct a6xx_gmu *gmu, enum a6xx_gmu_oob_state state);
void a6xx_gmu_clear_oob(struct a6xx_gmu *gmu, enum a6xx_gmu_oob_state state);

int a6xx_gmu_init(struct a6xx_gpu *a6xx_gpu, struct device_node *node);
int a6xx_gmu_wrapper_init(struct a6xx_gpu *a6xx_gpu, struct device_node *node);
void a6xx_gmu_remove(struct a6xx_gpu *a6xx_gpu);
void a6xx_gmu_sysprof_setup(struct msm_gpu *gpu);

void a6xx_preempt_init(struct msm_gpu *gpu);
void a6xx_preempt_hw_init(struct msm_gpu *gpu);
void a6xx_preempt_trigger(struct msm_gpu *gpu);
void a6xx_preempt_irq(struct msm_gpu *gpu);
void a6xx_preempt_fini(struct msm_gpu *gpu);
int a6xx_preempt_submitqueue_setup(struct msm_gpu *gpu,
		struct msm_gpu_submitqueue *queue);
void a6xx_preempt_submitqueue_close(struct msm_gpu *gpu,
		struct msm_gpu_submitqueue *queue);

/* Return true if we are in a preempt state */
static inline bool a6xx_in_preempt(struct a6xx_gpu *a6xx_gpu)
{
	/*
	 * Make sure the read to preempt_state is ordered with respect to reads
	 * of other variables before ...
	 */
	smp_rmb();

	int preempt_state = atomic_read(&a6xx_gpu->preempt_state);

	/* ... and after. */
	smp_rmb();

	return !(preempt_state == PREEMPT_NONE ||
			preempt_state == PREEMPT_FINISH);
}

void a6xx_gmu_set_freq(struct msm_gpu *gpu, struct dev_pm_opp *opp,
		       bool suspended);
unsigned long a6xx_gmu_get_freq(struct msm_gpu *gpu);

void a6xx_show(struct msm_gpu *gpu, struct msm_gpu_state *state,
		struct drm_printer *p);

struct msm_gpu_state *a6xx_gpu_state_get(struct msm_gpu *gpu);
int a6xx_gpu_state_put(struct msm_gpu_state *state);

void a6xx_bus_clear_pending_transactions(struct adreno_gpu *adreno_gpu, bool gx_off);
void a6xx_gpu_sw_reset(struct msm_gpu *gpu, bool assert);
int a6xx_fenced_write(struct a6xx_gpu *gpu, u32 offset, u64 value, u32 mask, bool is_64b);

#endif /* __A6XX_GPU_H__ */
