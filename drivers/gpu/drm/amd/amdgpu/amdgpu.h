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
 */
#ifndef __AMDGPU_H__
#define __AMDGPU_H__

#include <linux/atomic.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <linux/interval_tree.h>
#include <linux/hashtable.h>
#include <linux/fence.h>

#include <ttm/ttm_bo_api.h>
#include <ttm/ttm_bo_driver.h>
#include <ttm/ttm_placement.h>
#include <ttm/ttm_module.h>
#include <ttm/ttm_execbuf_util.h>

#include <drm/drmP.h>
#include <drm/drm_gem.h>
#include <drm/amdgpu_drm.h>

#include "amd_shared.h"
#include "amdgpu_mode.h"
#include "amdgpu_ih.h"
#include "amdgpu_irq.h"
#include "amdgpu_ucode.h"
#include "amdgpu_gds.h"

#include "gpu_scheduler.h"

/*
 * Modules parameters.
 */
extern int amdgpu_modeset;
extern int amdgpu_vram_limit;
extern int amdgpu_gart_size;
extern int amdgpu_benchmarking;
extern int amdgpu_testing;
extern int amdgpu_audio;
extern int amdgpu_disp_priority;
extern int amdgpu_hw_i2c;
extern int amdgpu_pcie_gen2;
extern int amdgpu_msi;
extern int amdgpu_lockup_timeout;
extern int amdgpu_dpm;
extern int amdgpu_smc_load_fw;
extern int amdgpu_aspm;
extern int amdgpu_runtime_pm;
extern int amdgpu_hard_reset;
extern unsigned amdgpu_ip_block_mask;
extern int amdgpu_bapm;
extern int amdgpu_deep_color;
extern int amdgpu_vm_size;
extern int amdgpu_vm_block_size;
extern int amdgpu_vm_fault_stop;
extern int amdgpu_vm_debug;
extern int amdgpu_enable_scheduler;
extern int amdgpu_sched_jobs;
extern int amdgpu_sched_hw_submission;
extern int amdgpu_enable_semaphores;

#define AMDGPU_WAIT_IDLE_TIMEOUT_IN_MS	        3000
#define AMDGPU_MAX_USEC_TIMEOUT			100000	/* 100 ms */
#define AMDGPU_FENCE_JIFFIES_TIMEOUT		(HZ / 2)
/* AMDGPU_IB_POOL_SIZE must be a power of 2 */
#define AMDGPU_IB_POOL_SIZE			16
#define AMDGPU_DEBUGFS_MAX_COMPONENTS		32
#define AMDGPUFB_CONN_LIMIT			4
#define AMDGPU_BIOS_NUM_SCRATCH			8

/* max number of rings */
#define AMDGPU_MAX_RINGS			16
#define AMDGPU_MAX_GFX_RINGS			1
#define AMDGPU_MAX_COMPUTE_RINGS		8
#define AMDGPU_MAX_VCE_RINGS			2

/* max number of IP instances */
#define AMDGPU_MAX_SDMA_INSTANCES		2

/* number of hw syncs before falling back on blocking */
#define AMDGPU_NUM_SYNCS			4

/* hardcode that limit for now */
#define AMDGPU_VA_RESERVED_SIZE			(8 << 20)

/* hard reset data */
#define AMDGPU_ASIC_RESET_DATA                  0x39d5e86b

/* reset flags */
#define AMDGPU_RESET_GFX			(1 << 0)
#define AMDGPU_RESET_COMPUTE			(1 << 1)
#define AMDGPU_RESET_DMA			(1 << 2)
#define AMDGPU_RESET_CP				(1 << 3)
#define AMDGPU_RESET_GRBM			(1 << 4)
#define AMDGPU_RESET_DMA1			(1 << 5)
#define AMDGPU_RESET_RLC			(1 << 6)
#define AMDGPU_RESET_SEM			(1 << 7)
#define AMDGPU_RESET_IH				(1 << 8)
#define AMDGPU_RESET_VMC			(1 << 9)
#define AMDGPU_RESET_MC				(1 << 10)
#define AMDGPU_RESET_DISPLAY			(1 << 11)
#define AMDGPU_RESET_UVD			(1 << 12)
#define AMDGPU_RESET_VCE			(1 << 13)
#define AMDGPU_RESET_VCE1			(1 << 14)

/* CG block flags */
#define AMDGPU_CG_BLOCK_GFX			(1 << 0)
#define AMDGPU_CG_BLOCK_MC			(1 << 1)
#define AMDGPU_CG_BLOCK_SDMA			(1 << 2)
#define AMDGPU_CG_BLOCK_UVD			(1 << 3)
#define AMDGPU_CG_BLOCK_VCE			(1 << 4)
#define AMDGPU_CG_BLOCK_HDP			(1 << 5)
#define AMDGPU_CG_BLOCK_BIF			(1 << 6)

/* CG flags */
#define AMDGPU_CG_SUPPORT_GFX_MGCG		(1 << 0)
#define AMDGPU_CG_SUPPORT_GFX_MGLS		(1 << 1)
#define AMDGPU_CG_SUPPORT_GFX_CGCG		(1 << 2)
#define AMDGPU_CG_SUPPORT_GFX_CGLS		(1 << 3)
#define AMDGPU_CG_SUPPORT_GFX_CGTS		(1 << 4)
#define AMDGPU_CG_SUPPORT_GFX_CGTS_LS		(1 << 5)
#define AMDGPU_CG_SUPPORT_GFX_CP_LS		(1 << 6)
#define AMDGPU_CG_SUPPORT_GFX_RLC_LS		(1 << 7)
#define AMDGPU_CG_SUPPORT_MC_LS			(1 << 8)
#define AMDGPU_CG_SUPPORT_MC_MGCG		(1 << 9)
#define AMDGPU_CG_SUPPORT_SDMA_LS		(1 << 10)
#define AMDGPU_CG_SUPPORT_SDMA_MGCG		(1 << 11)
#define AMDGPU_CG_SUPPORT_BIF_LS		(1 << 12)
#define AMDGPU_CG_SUPPORT_UVD_MGCG		(1 << 13)
#define AMDGPU_CG_SUPPORT_VCE_MGCG		(1 << 14)
#define AMDGPU_CG_SUPPORT_HDP_LS		(1 << 15)
#define AMDGPU_CG_SUPPORT_HDP_MGCG		(1 << 16)

/* PG flags */
#define AMDGPU_PG_SUPPORT_GFX_PG		(1 << 0)
#define AMDGPU_PG_SUPPORT_GFX_SMG		(1 << 1)
#define AMDGPU_PG_SUPPORT_GFX_DMG		(1 << 2)
#define AMDGPU_PG_SUPPORT_UVD			(1 << 3)
#define AMDGPU_PG_SUPPORT_VCE			(1 << 4)
#define AMDGPU_PG_SUPPORT_CP			(1 << 5)
#define AMDGPU_PG_SUPPORT_GDS			(1 << 6)
#define AMDGPU_PG_SUPPORT_RLC_SMU_HS		(1 << 7)
#define AMDGPU_PG_SUPPORT_SDMA			(1 << 8)
#define AMDGPU_PG_SUPPORT_ACP			(1 << 9)
#define AMDGPU_PG_SUPPORT_SAMU			(1 << 10)

/* GFX current status */
#define AMDGPU_GFX_NORMAL_MODE			0x00000000L
#define AMDGPU_GFX_SAFE_MODE			0x00000001L
#define AMDGPU_GFX_PG_DISABLED_MODE		0x00000002L
#define AMDGPU_GFX_CG_DISABLED_MODE		0x00000004L
#define AMDGPU_GFX_LBPW_DISABLED_MODE		0x00000008L

/* max cursor sizes (in pixels) */
#define CIK_CURSOR_WIDTH 128
#define CIK_CURSOR_HEIGHT 128

struct amdgpu_device;
struct amdgpu_fence;
struct amdgpu_ib;
struct amdgpu_vm;
struct amdgpu_ring;
struct amdgpu_semaphore;
struct amdgpu_cs_parser;
struct amdgpu_job;
struct amdgpu_irq_src;
struct amdgpu_fpriv;

enum amdgpu_cp_irq {
	AMDGPU_CP_IRQ_GFX_EOP = 0,
	AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE0_EOP,
	AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE1_EOP,
	AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE2_EOP,
	AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE3_EOP,
	AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE0_EOP,
	AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE1_EOP,
	AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE2_EOP,
	AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE3_EOP,

	AMDGPU_CP_IRQ_LAST
};

enum amdgpu_sdma_irq {
	AMDGPU_SDMA_IRQ_TRAP0 = 0,
	AMDGPU_SDMA_IRQ_TRAP1,

	AMDGPU_SDMA_IRQ_LAST
};

enum amdgpu_thermal_irq {
	AMDGPU_THERMAL_IRQ_LOW_TO_HIGH = 0,
	AMDGPU_THERMAL_IRQ_HIGH_TO_LOW,

	AMDGPU_THERMAL_IRQ_LAST
};

int amdgpu_set_clockgating_state(struct amdgpu_device *adev,
				  enum amd_ip_block_type block_type,
				  enum amd_clockgating_state state);
int amdgpu_set_powergating_state(struct amdgpu_device *adev,
				  enum amd_ip_block_type block_type,
				  enum amd_powergating_state state);

struct amdgpu_ip_block_version {
	enum amd_ip_block_type type;
	u32 major;
	u32 minor;
	u32 rev;
	const struct amd_ip_funcs *funcs;
};

int amdgpu_ip_block_version_cmp(struct amdgpu_device *adev,
				enum amd_ip_block_type type,
				u32 major, u32 minor);

const struct amdgpu_ip_block_version * amdgpu_get_ip_block(
					struct amdgpu_device *adev,
					enum amd_ip_block_type type);

/* provided by hw blocks that can move/clear data.  e.g., gfx or sdma */
struct amdgpu_buffer_funcs {
	/* maximum bytes in a single operation */
	uint32_t	copy_max_bytes;

	/* number of dw to reserve per operation */
	unsigned	copy_num_dw;

	/* used for buffer migration */
	void (*emit_copy_buffer)(struct amdgpu_ib *ib,
				 /* src addr in bytes */
				 uint64_t src_offset,
				 /* dst addr in bytes */
				 uint64_t dst_offset,
				 /* number of byte to transfer */
				 uint32_t byte_count);

	/* maximum bytes in a single operation */
	uint32_t	fill_max_bytes;

	/* number of dw to reserve per operation */
	unsigned	fill_num_dw;

	/* used for buffer clearing */
	void (*emit_fill_buffer)(struct amdgpu_ib *ib,
				 /* value to write to memory */
				 uint32_t src_data,
				 /* dst addr in bytes */
				 uint64_t dst_offset,
				 /* number of byte to fill */
				 uint32_t byte_count);
};

/* provided by hw blocks that can write ptes, e.g., sdma */
struct amdgpu_vm_pte_funcs {
	/* copy pte entries from GART */
	void (*copy_pte)(struct amdgpu_ib *ib,
			 uint64_t pe, uint64_t src,
			 unsigned count);
	/* write pte one entry at a time with addr mapping */
	void (*write_pte)(struct amdgpu_ib *ib,
			  uint64_t pe,
			  uint64_t addr, unsigned count,
			  uint32_t incr, uint32_t flags);
	/* for linear pte/pde updates without addr mapping */
	void (*set_pte_pde)(struct amdgpu_ib *ib,
			    uint64_t pe,
			    uint64_t addr, unsigned count,
			    uint32_t incr, uint32_t flags);
	/* pad the indirect buffer to the necessary number of dw */
	void (*pad_ib)(struct amdgpu_ib *ib);
};

/* provided by the gmc block */
struct amdgpu_gart_funcs {
	/* flush the vm tlb via mmio */
	void (*flush_gpu_tlb)(struct amdgpu_device *adev,
			      uint32_t vmid);
	/* write pte/pde updates using the cpu */
	int (*set_pte_pde)(struct amdgpu_device *adev,
			   void *cpu_pt_addr, /* cpu addr of page table */
			   uint32_t gpu_page_idx, /* pte/pde to update */
			   uint64_t addr, /* addr to write into pte/pde */
			   uint32_t flags); /* access flags */
};

/* provided by the ih block */
struct amdgpu_ih_funcs {
	/* ring read/write ptr handling, called from interrupt context */
	u32 (*get_wptr)(struct amdgpu_device *adev);
	void (*decode_iv)(struct amdgpu_device *adev,
			  struct amdgpu_iv_entry *entry);
	void (*set_rptr)(struct amdgpu_device *adev);
};

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
			struct amdgpu_ib *ib);
	void (*emit_fence)(struct amdgpu_ring *ring, uint64_t addr,
			   uint64_t seq, unsigned flags);
	bool (*emit_semaphore)(struct amdgpu_ring *ring,
			       struct amdgpu_semaphore *semaphore,
			       bool emit_wait);
	void (*emit_vm_flush)(struct amdgpu_ring *ring, unsigned vm_id,
			      uint64_t pd_addr);
	void (*emit_hdp_flush)(struct amdgpu_ring *ring);
	void (*emit_gds_switch)(struct amdgpu_ring *ring, uint32_t vmid,
				uint32_t gds_base, uint32_t gds_size,
				uint32_t gws_base, uint32_t gws_size,
				uint32_t oa_base, uint32_t oa_size);
	/* testing functions */
	int (*test_ring)(struct amdgpu_ring *ring);
	int (*test_ib)(struct amdgpu_ring *ring);
	/* insert NOP packets */
	void (*insert_nop)(struct amdgpu_ring *ring, uint32_t count);
};

/*
 * BIOS.
 */
bool amdgpu_get_bios(struct amdgpu_device *adev);
bool amdgpu_read_bios(struct amdgpu_device *adev);

/*
 * Dummy page
 */
struct amdgpu_dummy_page {
	struct page	*page;
	dma_addr_t	addr;
};
int amdgpu_dummy_page_init(struct amdgpu_device *adev);
void amdgpu_dummy_page_fini(struct amdgpu_device *adev);


/*
 * Clocks
 */

#define AMDGPU_MAX_PPLL 3

struct amdgpu_clock {
	struct amdgpu_pll ppll[AMDGPU_MAX_PPLL];
	struct amdgpu_pll spll;
	struct amdgpu_pll mpll;
	/* 10 Khz units */
	uint32_t default_mclk;
	uint32_t default_sclk;
	uint32_t default_dispclk;
	uint32_t current_dispclk;
	uint32_t dp_extclk;
	uint32_t max_pixel_clock;
};

/*
 * Fences.
 */
struct amdgpu_fence_driver {
	uint64_t			gpu_addr;
	volatile uint32_t		*cpu_addr;
	/* sync_seq is protected by ring emission lock */
	uint64_t			sync_seq[AMDGPU_MAX_RINGS];
	atomic64_t			last_seq;
	bool				initialized;
	struct amdgpu_irq_src		*irq_src;
	unsigned			irq_type;
	struct timer_list		fallback_timer;
	wait_queue_head_t		fence_queue;
};

/* some special values for the owner field */
#define AMDGPU_FENCE_OWNER_UNDEFINED	((void*)0ul)
#define AMDGPU_FENCE_OWNER_VM		((void*)1ul)

#define AMDGPU_FENCE_FLAG_64BIT         (1 << 0)
#define AMDGPU_FENCE_FLAG_INT           (1 << 1)

struct amdgpu_fence {
	struct fence base;

	/* RB, DMA, etc. */
	struct amdgpu_ring		*ring;
	uint64_t			seq;

	/* filp or special value for fence creator */
	void				*owner;

	wait_queue_t			fence_wake;
};

struct amdgpu_user_fence {
	/* write-back bo */
	struct amdgpu_bo 	*bo;
	/* write-back address offset to bo start */
	uint32_t                offset;
};

int amdgpu_fence_driver_init(struct amdgpu_device *adev);
void amdgpu_fence_driver_fini(struct amdgpu_device *adev);
void amdgpu_fence_driver_force_completion(struct amdgpu_device *adev);

int amdgpu_fence_driver_init_ring(struct amdgpu_ring *ring);
int amdgpu_fence_driver_start_ring(struct amdgpu_ring *ring,
				   struct amdgpu_irq_src *irq_src,
				   unsigned irq_type);
void amdgpu_fence_driver_suspend(struct amdgpu_device *adev);
void amdgpu_fence_driver_resume(struct amdgpu_device *adev);
int amdgpu_fence_emit(struct amdgpu_ring *ring, void *owner,
		      struct amdgpu_fence **fence);
void amdgpu_fence_process(struct amdgpu_ring *ring);
int amdgpu_fence_wait_next(struct amdgpu_ring *ring);
int amdgpu_fence_wait_empty(struct amdgpu_ring *ring);
unsigned amdgpu_fence_count_emitted(struct amdgpu_ring *ring);

bool amdgpu_fence_need_sync(struct amdgpu_fence *fence,
			    struct amdgpu_ring *ring);
void amdgpu_fence_note_sync(struct amdgpu_fence *fence,
			    struct amdgpu_ring *ring);

/*
 * TTM.
 */
struct amdgpu_mman {
	struct ttm_bo_global_ref        bo_global_ref;
	struct drm_global_reference	mem_global_ref;
	struct ttm_bo_device		bdev;
	bool				mem_global_referenced;
	bool				initialized;

#if defined(CONFIG_DEBUG_FS)
	struct dentry			*vram;
	struct dentry			*gtt;
#endif

	/* buffer handling */
	const struct amdgpu_buffer_funcs	*buffer_funcs;
	struct amdgpu_ring			*buffer_funcs_ring;
};

int amdgpu_copy_buffer(struct amdgpu_ring *ring,
		       uint64_t src_offset,
		       uint64_t dst_offset,
		       uint32_t byte_count,
		       struct reservation_object *resv,
		       struct fence **fence);
int amdgpu_mmap(struct file *filp, struct vm_area_struct *vma);

struct amdgpu_bo_list_entry {
	struct amdgpu_bo		*robj;
	struct ttm_validate_buffer	tv;
	struct amdgpu_bo_va		*bo_va;
	unsigned			prefered_domains;
	unsigned			allowed_domains;
	uint32_t			priority;
};

struct amdgpu_bo_va_mapping {
	struct list_head		list;
	struct interval_tree_node	it;
	uint64_t			offset;
	uint32_t			flags;
};

/* bo virtual addresses in a specific vm */
struct amdgpu_bo_va {
	struct mutex		        mutex;
	/* protected by bo being reserved */
	struct list_head		bo_list;
	struct fence		        *last_pt_update;
	unsigned			ref_count;

	/* protected by vm mutex and spinlock */
	struct list_head		vm_status;

	/* mappings for this bo_va */
	struct list_head		invalids;
	struct list_head		valids;

	/* constant after initialization */
	struct amdgpu_vm		*vm;
	struct amdgpu_bo		*bo;
};

#define AMDGPU_GEM_DOMAIN_MAX		0x3

struct amdgpu_bo {
	/* Protected by gem.mutex */
	struct list_head		list;
	/* Protected by tbo.reserved */
	u32				initial_domain;
	struct ttm_place		placements[AMDGPU_GEM_DOMAIN_MAX + 1];
	struct ttm_placement		placement;
	struct ttm_buffer_object	tbo;
	struct ttm_bo_kmap_obj		kmap;
	u64				flags;
	unsigned			pin_count;
	void				*kptr;
	u64				tiling_flags;
	u64				metadata_flags;
	void				*metadata;
	u32				metadata_size;
	/* list of all virtual address to which this bo
	 * is associated to
	 */
	struct list_head		va;
	/* Constant after initialization */
	struct amdgpu_device		*adev;
	struct drm_gem_object		gem_base;
	struct amdgpu_bo		*parent;

	struct ttm_bo_kmap_obj		dma_buf_vmap;
	pid_t				pid;
	struct amdgpu_mn		*mn;
	struct list_head		mn_list;
};
#define gem_to_amdgpu_bo(gobj) container_of((gobj), struct amdgpu_bo, gem_base)

void amdgpu_gem_object_free(struct drm_gem_object *obj);
int amdgpu_gem_object_open(struct drm_gem_object *obj,
				struct drm_file *file_priv);
void amdgpu_gem_object_close(struct drm_gem_object *obj,
				struct drm_file *file_priv);
unsigned long amdgpu_gem_timeout(uint64_t timeout_ns);
struct sg_table *amdgpu_gem_prime_get_sg_table(struct drm_gem_object *obj);
struct drm_gem_object *amdgpu_gem_prime_import_sg_table(struct drm_device *dev,
							struct dma_buf_attachment *attach,
							struct sg_table *sg);
struct dma_buf *amdgpu_gem_prime_export(struct drm_device *dev,
					struct drm_gem_object *gobj,
					int flags);
int amdgpu_gem_prime_pin(struct drm_gem_object *obj);
void amdgpu_gem_prime_unpin(struct drm_gem_object *obj);
struct reservation_object *amdgpu_gem_prime_res_obj(struct drm_gem_object *);
void *amdgpu_gem_prime_vmap(struct drm_gem_object *obj);
void amdgpu_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr);
int amdgpu_gem_debugfs_init(struct amdgpu_device *adev);

/* sub-allocation manager, it has to be protected by another lock.
 * By conception this is an helper for other part of the driver
 * like the indirect buffer or semaphore, which both have their
 * locking.
 *
 * Principe is simple, we keep a list of sub allocation in offset
 * order (first entry has offset == 0, last entry has the highest
 * offset).
 *
 * When allocating new object we first check if there is room at
 * the end total_size - (last_object_offset + last_object_size) >=
 * alloc_size. If so we allocate new object there.
 *
 * When there is not enough room at the end, we start waiting for
 * each sub object until we reach object_offset+object_size >=
 * alloc_size, this object then become the sub object we return.
 *
 * Alignment can't be bigger than page size.
 *
 * Hole are not considered for allocation to keep things simple.
 * Assumption is that there won't be hole (all object on same
 * alignment).
 */
struct amdgpu_sa_manager {
	wait_queue_head_t	wq;
	struct amdgpu_bo	*bo;
	struct list_head	*hole;
	struct list_head	flist[AMDGPU_MAX_RINGS];
	struct list_head	olist;
	unsigned		size;
	uint64_t		gpu_addr;
	void			*cpu_ptr;
	uint32_t		domain;
	uint32_t		align;
};

/* sub-allocation buffer */
struct amdgpu_sa_bo {
	struct list_head		olist;
	struct list_head		flist;
	struct amdgpu_sa_manager	*manager;
	unsigned			soffset;
	unsigned			eoffset;
	struct fence		        *fence;
};

/*
 * GEM objects.
 */
struct amdgpu_gem {
	struct mutex		mutex;
	struct list_head	objects;
};

int amdgpu_gem_init(struct amdgpu_device *adev);
void amdgpu_gem_fini(struct amdgpu_device *adev);
int amdgpu_gem_object_create(struct amdgpu_device *adev, unsigned long size,
				int alignment, u32 initial_domain,
				u64 flags, bool kernel,
				struct drm_gem_object **obj);

int amdgpu_mode_dumb_create(struct drm_file *file_priv,
			    struct drm_device *dev,
			    struct drm_mode_create_dumb *args);
int amdgpu_mode_dumb_mmap(struct drm_file *filp,
			  struct drm_device *dev,
			  uint32_t handle, uint64_t *offset_p);

/*
 * Semaphores.
 */
struct amdgpu_semaphore {
	struct amdgpu_sa_bo	*sa_bo;
	signed			waiters;
	uint64_t		gpu_addr;
};

int amdgpu_semaphore_create(struct amdgpu_device *adev,
			    struct amdgpu_semaphore **semaphore);
bool amdgpu_semaphore_emit_signal(struct amdgpu_ring *ring,
				  struct amdgpu_semaphore *semaphore);
bool amdgpu_semaphore_emit_wait(struct amdgpu_ring *ring,
				struct amdgpu_semaphore *semaphore);
void amdgpu_semaphore_free(struct amdgpu_device *adev,
			   struct amdgpu_semaphore **semaphore,
			   struct fence *fence);

/*
 * Synchronization
 */
struct amdgpu_sync {
	struct amdgpu_semaphore *semaphores[AMDGPU_NUM_SYNCS];
	struct fence		*sync_to[AMDGPU_MAX_RINGS];
	DECLARE_HASHTABLE(fences, 4);
	struct fence	        *last_vm_update;
};

void amdgpu_sync_create(struct amdgpu_sync *sync);
int amdgpu_sync_fence(struct amdgpu_device *adev, struct amdgpu_sync *sync,
		      struct fence *f);
int amdgpu_sync_resv(struct amdgpu_device *adev,
		     struct amdgpu_sync *sync,
		     struct reservation_object *resv,
		     void *owner);
int amdgpu_sync_rings(struct amdgpu_sync *sync,
		      struct amdgpu_ring *ring);
struct fence *amdgpu_sync_get_fence(struct amdgpu_sync *sync);
int amdgpu_sync_wait(struct amdgpu_sync *sync);
void amdgpu_sync_free(struct amdgpu_device *adev, struct amdgpu_sync *sync,
		      struct fence *fence);

/*
 * GART structures, functions & helpers
 */
struct amdgpu_mc;

#define AMDGPU_GPU_PAGE_SIZE 4096
#define AMDGPU_GPU_PAGE_MASK (AMDGPU_GPU_PAGE_SIZE - 1)
#define AMDGPU_GPU_PAGE_SHIFT 12
#define AMDGPU_GPU_PAGE_ALIGN(a) (((a) + AMDGPU_GPU_PAGE_MASK) & ~AMDGPU_GPU_PAGE_MASK)

struct amdgpu_gart {
	dma_addr_t			table_addr;
	struct amdgpu_bo		*robj;
	void				*ptr;
	unsigned			num_gpu_pages;
	unsigned			num_cpu_pages;
	unsigned			table_size;
	struct page			**pages;
	dma_addr_t			*pages_addr;
	bool				ready;
	const struct amdgpu_gart_funcs *gart_funcs;
};

int amdgpu_gart_table_ram_alloc(struct amdgpu_device *adev);
void amdgpu_gart_table_ram_free(struct amdgpu_device *adev);
int amdgpu_gart_table_vram_alloc(struct amdgpu_device *adev);
void amdgpu_gart_table_vram_free(struct amdgpu_device *adev);
int amdgpu_gart_table_vram_pin(struct amdgpu_device *adev);
void amdgpu_gart_table_vram_unpin(struct amdgpu_device *adev);
int amdgpu_gart_init(struct amdgpu_device *adev);
void amdgpu_gart_fini(struct amdgpu_device *adev);
void amdgpu_gart_unbind(struct amdgpu_device *adev, uint64_t offset,
			int pages);
int amdgpu_gart_bind(struct amdgpu_device *adev, uint64_t offset,
		     int pages, struct page **pagelist,
		     dma_addr_t *dma_addr, uint32_t flags);

/*
 * GPU MC structures, functions & helpers
 */
struct amdgpu_mc {
	resource_size_t		aper_size;
	resource_size_t		aper_base;
	resource_size_t		agp_base;
	/* for some chips with <= 32MB we need to lie
	 * about vram size near mc fb location */
	u64			mc_vram_size;
	u64			visible_vram_size;
	u64			gtt_size;
	u64			gtt_start;
	u64			gtt_end;
	u64			vram_start;
	u64			vram_end;
	unsigned		vram_width;
	u64			real_vram_size;
	int			vram_mtrr;
	u64                     gtt_base_align;
	u64                     mc_mask;
	const struct firmware   *fw;	/* MC firmware */
	uint32_t                fw_version;
	struct amdgpu_irq_src	vm_fault;
	uint32_t		vram_type;
};

/*
 * GPU doorbell structures, functions & helpers
 */
typedef enum _AMDGPU_DOORBELL_ASSIGNMENT
{
	AMDGPU_DOORBELL_KIQ                     = 0x000,
	AMDGPU_DOORBELL_HIQ                     = 0x001,
	AMDGPU_DOORBELL_DIQ                     = 0x002,
	AMDGPU_DOORBELL_MEC_RING0               = 0x010,
	AMDGPU_DOORBELL_MEC_RING1               = 0x011,
	AMDGPU_DOORBELL_MEC_RING2               = 0x012,
	AMDGPU_DOORBELL_MEC_RING3               = 0x013,
	AMDGPU_DOORBELL_MEC_RING4               = 0x014,
	AMDGPU_DOORBELL_MEC_RING5               = 0x015,
	AMDGPU_DOORBELL_MEC_RING6               = 0x016,
	AMDGPU_DOORBELL_MEC_RING7               = 0x017,
	AMDGPU_DOORBELL_GFX_RING0               = 0x020,
	AMDGPU_DOORBELL_sDMA_ENGINE0            = 0x1E0,
	AMDGPU_DOORBELL_sDMA_ENGINE1            = 0x1E1,
	AMDGPU_DOORBELL_IH                      = 0x1E8,
	AMDGPU_DOORBELL_MAX_ASSIGNMENT          = 0x3FF,
	AMDGPU_DOORBELL_INVALID                 = 0xFFFF
} AMDGPU_DOORBELL_ASSIGNMENT;

struct amdgpu_doorbell {
	/* doorbell mmio */
	resource_size_t		base;
	resource_size_t		size;
	u32 __iomem		*ptr;
	u32			num_doorbells;	/* Number of doorbells actually reserved for amdgpu. */
};

void amdgpu_doorbell_get_kfd_info(struct amdgpu_device *adev,
				phys_addr_t *aperture_base,
				size_t *aperture_size,
				size_t *start_offset);

/*
 * IRQS.
 */

struct amdgpu_flip_work {
	struct work_struct		flip_work;
	struct work_struct		unpin_work;
	struct amdgpu_device		*adev;
	int				crtc_id;
	uint64_t			base;
	struct drm_pending_vblank_event *event;
	struct amdgpu_bo		*old_rbo;
	struct fence			*excl;
	unsigned			shared_count;
	struct fence			**shared;
};


/*
 * CP & rings.
 */

struct amdgpu_ib {
	struct amdgpu_sa_bo		*sa_bo;
	uint32_t			length_dw;
	uint64_t			gpu_addr;
	uint32_t			*ptr;
	struct amdgpu_ring		*ring;
	struct amdgpu_fence		*fence;
	struct amdgpu_user_fence        *user;
	struct amdgpu_vm		*vm;
	struct amdgpu_ctx		*ctx;
	struct amdgpu_sync		sync;
	uint32_t			gds_base, gds_size;
	uint32_t			gws_base, gws_size;
	uint32_t			oa_base, oa_size;
	uint32_t			flags;
	/* resulting sequence number */
	uint64_t			sequence;
};

enum amdgpu_ring_type {
	AMDGPU_RING_TYPE_GFX,
	AMDGPU_RING_TYPE_COMPUTE,
	AMDGPU_RING_TYPE_SDMA,
	AMDGPU_RING_TYPE_UVD,
	AMDGPU_RING_TYPE_VCE
};

extern struct amd_sched_backend_ops amdgpu_sched_ops;

int amdgpu_sched_ib_submit_kernel_helper(struct amdgpu_device *adev,
					 struct amdgpu_ring *ring,
					 struct amdgpu_ib *ibs,
					 unsigned num_ibs,
					 int (*free_job)(struct amdgpu_job *),
					 void *owner,
					 struct fence **fence);

struct amdgpu_ring {
	struct amdgpu_device		*adev;
	const struct amdgpu_ring_funcs	*funcs;
	struct amdgpu_fence_driver	fence_drv;
	struct amd_gpu_scheduler 	sched;

	spinlock_t              fence_lock;
	struct mutex		*ring_lock;
	struct amdgpu_bo	*ring_obj;
	volatile uint32_t	*ring;
	unsigned		rptr_offs;
	u64			next_rptr_gpu_addr;
	volatile u32		*next_rptr_cpu_addr;
	unsigned		wptr;
	unsigned		wptr_old;
	unsigned		ring_size;
	unsigned		ring_free_dw;
	int			count_dw;
	uint64_t		gpu_addr;
	uint32_t		align_mask;
	uint32_t		ptr_mask;
	bool			ready;
	u32			nop;
	u32			idx;
	u64			last_semaphore_signal_addr;
	u64			last_semaphore_wait_addr;
	u32			me;
	u32			pipe;
	u32			queue;
	struct amdgpu_bo	*mqd_obj;
	u32			doorbell_index;
	bool			use_doorbell;
	unsigned		wptr_offs;
	unsigned		next_rptr_offs;
	unsigned		fence_offs;
	struct amdgpu_ctx	*current_ctx;
	enum amdgpu_ring_type	type;
	char			name[16];
	bool                    is_pte_ring;
};

/*
 * VM
 */

/* maximum number of VMIDs */
#define AMDGPU_NUM_VM	16

/* number of entries in page table */
#define AMDGPU_VM_PTE_COUNT (1 << amdgpu_vm_block_size)

/* PTBs (Page Table Blocks) need to be aligned to 32K */
#define AMDGPU_VM_PTB_ALIGN_SIZE   32768
#define AMDGPU_VM_PTB_ALIGN_MASK (AMDGPU_VM_PTB_ALIGN_SIZE - 1)
#define AMDGPU_VM_PTB_ALIGN(a) (((a) + AMDGPU_VM_PTB_ALIGN_MASK) & ~AMDGPU_VM_PTB_ALIGN_MASK)

#define AMDGPU_PTE_VALID	(1 << 0)
#define AMDGPU_PTE_SYSTEM	(1 << 1)
#define AMDGPU_PTE_SNOOPED	(1 << 2)

/* VI only */
#define AMDGPU_PTE_EXECUTABLE	(1 << 4)

#define AMDGPU_PTE_READABLE	(1 << 5)
#define AMDGPU_PTE_WRITEABLE	(1 << 6)

/* PTE (Page Table Entry) fragment field for different page sizes */
#define AMDGPU_PTE_FRAG_4KB	(0 << 7)
#define AMDGPU_PTE_FRAG_64KB	(4 << 7)
#define AMDGPU_LOG2_PAGES_PER_FRAG 4

/* How to programm VM fault handling */
#define AMDGPU_VM_FAULT_STOP_NEVER	0
#define AMDGPU_VM_FAULT_STOP_FIRST	1
#define AMDGPU_VM_FAULT_STOP_ALWAYS	2

struct amdgpu_vm_pt {
	struct amdgpu_bo	*bo;
	uint64_t		addr;
};

struct amdgpu_vm_id {
	unsigned		id;
	uint64_t		pd_gpu_addr;
	/* last flushed PD/PT update */
	struct fence	        *flushed_updates;
};

struct amdgpu_vm {
	struct rb_root		va;

	/* protecting invalidated */
	spinlock_t		status_lock;

	/* BOs moved, but not yet updated in the PT */
	struct list_head	invalidated;

	/* BOs cleared in the PT because of a move */
	struct list_head	cleared;

	/* BO mappings freed, but not yet updated in the PT */
	struct list_head	freed;

	/* contains the page directory */
	struct amdgpu_bo	*page_directory;
	unsigned		max_pde_used;
	struct fence		*page_directory_fence;

	/* array of page tables, one for each page directory entry */
	struct amdgpu_vm_pt	*page_tables;

	/* for id and flush management per ring */
	struct amdgpu_vm_id	ids[AMDGPU_MAX_RINGS];
	/* for interval tree */
	spinlock_t		it_lock;
	/* protecting freed */
	spinlock_t		freed_lock;
};

struct amdgpu_vm_manager {
	struct {
		struct fence	*active;
		atomic_long_t	owner;
	} ids[AMDGPU_NUM_VM];

	uint32_t				max_pfn;
	/* number of VMIDs */
	unsigned				nvm;
	/* vram base address for page table entry  */
	u64					vram_base_offset;
	/* is vm enabled? */
	bool					enabled;
	/* vm pte handling */
	const struct amdgpu_vm_pte_funcs        *vm_pte_funcs;
	struct amdgpu_ring                      *vm_pte_funcs_ring;
};

void amdgpu_vm_manager_fini(struct amdgpu_device *adev);
int amdgpu_vm_init(struct amdgpu_device *adev, struct amdgpu_vm *vm);
void amdgpu_vm_fini(struct amdgpu_device *adev, struct amdgpu_vm *vm);
struct amdgpu_bo_list_entry *amdgpu_vm_get_bos(struct amdgpu_device *adev,
					       struct amdgpu_vm *vm,
					       struct list_head *head);
int amdgpu_vm_grab_id(struct amdgpu_vm *vm, struct amdgpu_ring *ring,
		      struct amdgpu_sync *sync);
void amdgpu_vm_flush(struct amdgpu_ring *ring,
		     struct amdgpu_vm *vm,
		     struct fence *updates);
void amdgpu_vm_fence(struct amdgpu_device *adev,
		     struct amdgpu_vm *vm,
		     struct fence *fence);
uint64_t amdgpu_vm_map_gart(struct amdgpu_device *adev, uint64_t addr);
int amdgpu_vm_update_page_directory(struct amdgpu_device *adev,
				    struct amdgpu_vm *vm);
int amdgpu_vm_clear_freed(struct amdgpu_device *adev,
			  struct amdgpu_vm *vm);
int amdgpu_vm_clear_invalids(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			     struct amdgpu_sync *sync);
int amdgpu_vm_bo_update(struct amdgpu_device *adev,
			struct amdgpu_bo_va *bo_va,
			struct ttm_mem_reg *mem);
void amdgpu_vm_bo_invalidate(struct amdgpu_device *adev,
			     struct amdgpu_bo *bo);
struct amdgpu_bo_va *amdgpu_vm_bo_find(struct amdgpu_vm *vm,
				       struct amdgpu_bo *bo);
struct amdgpu_bo_va *amdgpu_vm_bo_add(struct amdgpu_device *adev,
				      struct amdgpu_vm *vm,
				      struct amdgpu_bo *bo);
int amdgpu_vm_bo_map(struct amdgpu_device *adev,
		     struct amdgpu_bo_va *bo_va,
		     uint64_t addr, uint64_t offset,
		     uint64_t size, uint32_t flags);
int amdgpu_vm_bo_unmap(struct amdgpu_device *adev,
		       struct amdgpu_bo_va *bo_va,
		       uint64_t addr);
void amdgpu_vm_bo_rmv(struct amdgpu_device *adev,
		      struct amdgpu_bo_va *bo_va);
int amdgpu_vm_free_job(struct amdgpu_job *job);

/*
 * context related structures
 */

#define AMDGPU_CTX_MAX_CS_PENDING	16

struct amdgpu_ctx_ring {
	uint64_t		sequence;
	struct fence		*fences[AMDGPU_CTX_MAX_CS_PENDING];
	struct amd_sched_entity	entity;
};

struct amdgpu_ctx {
	struct kref		refcount;
	struct amdgpu_device    *adev;
	unsigned		reset_counter;
	spinlock_t		ring_lock;
	struct amdgpu_ctx_ring	rings[AMDGPU_MAX_RINGS];
};

struct amdgpu_ctx_mgr {
	struct amdgpu_device	*adev;
	struct mutex		lock;
	/* protected by lock */
	struct idr		ctx_handles;
};

int amdgpu_ctx_init(struct amdgpu_device *adev, bool kernel,
		    struct amdgpu_ctx *ctx);
void amdgpu_ctx_fini(struct amdgpu_ctx *ctx);

struct amdgpu_ctx *amdgpu_ctx_get(struct amdgpu_fpriv *fpriv, uint32_t id);
int amdgpu_ctx_put(struct amdgpu_ctx *ctx);

uint64_t amdgpu_ctx_add_fence(struct amdgpu_ctx *ctx, struct amdgpu_ring *ring,
			      struct fence *fence);
struct fence *amdgpu_ctx_get_fence(struct amdgpu_ctx *ctx,
				   struct amdgpu_ring *ring, uint64_t seq);

int amdgpu_ctx_ioctl(struct drm_device *dev, void *data,
		     struct drm_file *filp);

void amdgpu_ctx_mgr_init(struct amdgpu_ctx_mgr *mgr);
void amdgpu_ctx_mgr_fini(struct amdgpu_ctx_mgr *mgr);

/*
 * file private structure
 */

struct amdgpu_fpriv {
	struct amdgpu_vm	vm;
	struct mutex		bo_list_lock;
	struct idr		bo_list_handles;
	struct amdgpu_ctx_mgr	ctx_mgr;
};

/*
 * residency list
 */

struct amdgpu_bo_list {
	struct mutex lock;
	struct amdgpu_bo *gds_obj;
	struct amdgpu_bo *gws_obj;
	struct amdgpu_bo *oa_obj;
	bool has_userptr;
	unsigned num_entries;
	struct amdgpu_bo_list_entry *array;
};

struct amdgpu_bo_list *
amdgpu_bo_list_get(struct amdgpu_fpriv *fpriv, int id);
void amdgpu_bo_list_put(struct amdgpu_bo_list *list);
void amdgpu_bo_list_free(struct amdgpu_bo_list *list);

/*
 * GFX stuff
 */
#include "clearstate_defs.h"

struct amdgpu_rlc {
	/* for power gating */
	struct amdgpu_bo	*save_restore_obj;
	uint64_t		save_restore_gpu_addr;
	volatile uint32_t	*sr_ptr;
	const u32               *reg_list;
	u32                     reg_list_size;
	/* for clear state */
	struct amdgpu_bo	*clear_state_obj;
	uint64_t		clear_state_gpu_addr;
	volatile uint32_t	*cs_ptr;
	const struct cs_section_def   *cs_data;
	u32                     clear_state_size;
	/* for cp tables */
	struct amdgpu_bo	*cp_table_obj;
	uint64_t		cp_table_gpu_addr;
	volatile uint32_t	*cp_table_ptr;
	u32                     cp_table_size;
};

struct amdgpu_mec {
	struct amdgpu_bo	*hpd_eop_obj;
	u64			hpd_eop_gpu_addr;
	u32 num_pipe;
	u32 num_mec;
	u32 num_queue;
};

/*
 * GPU scratch registers structures, functions & helpers
 */
struct amdgpu_scratch {
	unsigned		num_reg;
	uint32_t                reg_base;
	bool			free[32];
	uint32_t		reg[32];
};

/*
 * GFX configurations
 */
struct amdgpu_gca_config {
	unsigned max_shader_engines;
	unsigned max_tile_pipes;
	unsigned max_cu_per_sh;
	unsigned max_sh_per_se;
	unsigned max_backends_per_se;
	unsigned max_texture_channel_caches;
	unsigned max_gprs;
	unsigned max_gs_threads;
	unsigned max_hw_contexts;
	unsigned sc_prim_fifo_size_frontend;
	unsigned sc_prim_fifo_size_backend;
	unsigned sc_hiz_tile_fifo_size;
	unsigned sc_earlyz_tile_fifo_size;

	unsigned num_tile_pipes;
	unsigned backend_enable_mask;
	unsigned mem_max_burst_length_bytes;
	unsigned mem_row_size_in_kb;
	unsigned shader_engine_tile_size;
	unsigned num_gpus;
	unsigned multi_gpu_tile_size;
	unsigned mc_arb_ramcfg;
	unsigned gb_addr_config;

	uint32_t tile_mode_array[32];
	uint32_t macrotile_mode_array[16];
};

struct amdgpu_gfx {
	struct mutex			gpu_clock_mutex;
	struct amdgpu_gca_config	config;
	struct amdgpu_rlc		rlc;
	struct amdgpu_mec		mec;
	struct amdgpu_scratch		scratch;
	const struct firmware		*me_fw;	/* ME firmware */
	uint32_t			me_fw_version;
	const struct firmware		*pfp_fw; /* PFP firmware */
	uint32_t			pfp_fw_version;
	const struct firmware		*ce_fw;	/* CE firmware */
	uint32_t			ce_fw_version;
	const struct firmware		*rlc_fw; /* RLC firmware */
	uint32_t			rlc_fw_version;
	const struct firmware		*mec_fw; /* MEC firmware */
	uint32_t			mec_fw_version;
	const struct firmware		*mec2_fw; /* MEC2 firmware */
	uint32_t			mec2_fw_version;
	uint32_t			me_feature_version;
	uint32_t			ce_feature_version;
	uint32_t			pfp_feature_version;
	uint32_t			rlc_feature_version;
	uint32_t			mec_feature_version;
	uint32_t			mec2_feature_version;
	struct amdgpu_ring		gfx_ring[AMDGPU_MAX_GFX_RINGS];
	unsigned			num_gfx_rings;
	struct amdgpu_ring		compute_ring[AMDGPU_MAX_COMPUTE_RINGS];
	unsigned			num_compute_rings;
	struct amdgpu_irq_src		eop_irq;
	struct amdgpu_irq_src		priv_reg_irq;
	struct amdgpu_irq_src		priv_inst_irq;
	/* gfx status */
	uint32_t gfx_current_status;
	/* ce ram size*/
	unsigned ce_ram_size;
};

int amdgpu_ib_get(struct amdgpu_ring *ring, struct amdgpu_vm *vm,
		  unsigned size, struct amdgpu_ib *ib);
void amdgpu_ib_free(struct amdgpu_device *adev, struct amdgpu_ib *ib);
int amdgpu_ib_schedule(struct amdgpu_device *adev, unsigned num_ibs,
		       struct amdgpu_ib *ib, void *owner);
int amdgpu_ib_pool_init(struct amdgpu_device *adev);
void amdgpu_ib_pool_fini(struct amdgpu_device *adev);
int amdgpu_ib_ring_tests(struct amdgpu_device *adev);
/* Ring access between begin & end cannot sleep */
void amdgpu_ring_free_size(struct amdgpu_ring *ring);
int amdgpu_ring_alloc(struct amdgpu_ring *ring, unsigned ndw);
int amdgpu_ring_lock(struct amdgpu_ring *ring, unsigned ndw);
void amdgpu_ring_insert_nop(struct amdgpu_ring *ring, uint32_t count);
void amdgpu_ring_commit(struct amdgpu_ring *ring);
void amdgpu_ring_unlock_commit(struct amdgpu_ring *ring);
void amdgpu_ring_undo(struct amdgpu_ring *ring);
void amdgpu_ring_unlock_undo(struct amdgpu_ring *ring);
unsigned amdgpu_ring_backup(struct amdgpu_ring *ring,
			    uint32_t **data);
int amdgpu_ring_restore(struct amdgpu_ring *ring,
			unsigned size, uint32_t *data);
int amdgpu_ring_init(struct amdgpu_device *adev, struct amdgpu_ring *ring,
		     unsigned ring_size, u32 nop, u32 align_mask,
		     struct amdgpu_irq_src *irq_src, unsigned irq_type,
		     enum amdgpu_ring_type ring_type);
void amdgpu_ring_fini(struct amdgpu_ring *ring);
struct amdgpu_ring *amdgpu_ring_from_fence(struct fence *f);

/*
 * CS.
 */
struct amdgpu_cs_chunk {
	uint32_t		chunk_id;
	uint32_t		length_dw;
	uint32_t		*kdata;
	void __user		*user_ptr;
};

struct amdgpu_cs_parser {
	struct amdgpu_device	*adev;
	struct drm_file		*filp;
	struct amdgpu_ctx	*ctx;
	struct amdgpu_bo_list *bo_list;
	/* chunks */
	unsigned		nchunks;
	struct amdgpu_cs_chunk	*chunks;
	/* relocations */
	struct amdgpu_bo_list_entry	*vm_bos;
	struct list_head	validated;
	struct fence		*fence;

	struct amdgpu_ib	*ibs;
	uint32_t		num_ibs;

	struct ww_acquire_ctx	ticket;

	/* user fence */
	struct amdgpu_user_fence	uf;
	struct amdgpu_bo_list_entry	uf_entry;
};

struct amdgpu_job {
	struct amd_sched_job    base;
	struct amdgpu_device	*adev;
	struct amdgpu_ib	*ibs;
	uint32_t		num_ibs;
	void			*owner;
	struct amdgpu_user_fence uf;
	int (*free_job)(struct amdgpu_job *job);
};
#define to_amdgpu_job(sched_job)		\
		container_of((sched_job), struct amdgpu_job, base)

static inline u32 amdgpu_get_ib_value(struct amdgpu_cs_parser *p, uint32_t ib_idx, int idx)
{
	return p->ibs[ib_idx].ptr[idx];
}

/*
 * Writeback
 */
#define AMDGPU_MAX_WB 1024	/* Reserve at most 1024 WB slots for amdgpu-owned rings. */

struct amdgpu_wb {
	struct amdgpu_bo	*wb_obj;
	volatile uint32_t	*wb;
	uint64_t		gpu_addr;
	u32			num_wb;	/* Number of wb slots actually reserved for amdgpu. */
	unsigned long		used[DIV_ROUND_UP(AMDGPU_MAX_WB, BITS_PER_LONG)];
};

int amdgpu_wb_get(struct amdgpu_device *adev, u32 *wb);
void amdgpu_wb_free(struct amdgpu_device *adev, u32 wb);

/**
 * struct amdgpu_pm - power management datas
 * It keeps track of various data needed to take powermanagement decision.
 */

enum amdgpu_pm_state_type {
	/* not used for dpm */
	POWER_STATE_TYPE_DEFAULT,
	POWER_STATE_TYPE_POWERSAVE,
	/* user selectable states */
	POWER_STATE_TYPE_BATTERY,
	POWER_STATE_TYPE_BALANCED,
	POWER_STATE_TYPE_PERFORMANCE,
	/* internal states */
	POWER_STATE_TYPE_INTERNAL_UVD,
	POWER_STATE_TYPE_INTERNAL_UVD_SD,
	POWER_STATE_TYPE_INTERNAL_UVD_HD,
	POWER_STATE_TYPE_INTERNAL_UVD_HD2,
	POWER_STATE_TYPE_INTERNAL_UVD_MVC,
	POWER_STATE_TYPE_INTERNAL_BOOT,
	POWER_STATE_TYPE_INTERNAL_THERMAL,
	POWER_STATE_TYPE_INTERNAL_ACPI,
	POWER_STATE_TYPE_INTERNAL_ULV,
	POWER_STATE_TYPE_INTERNAL_3DPERF,
};

enum amdgpu_int_thermal_type {
	THERMAL_TYPE_NONE,
	THERMAL_TYPE_EXTERNAL,
	THERMAL_TYPE_EXTERNAL_GPIO,
	THERMAL_TYPE_RV6XX,
	THERMAL_TYPE_RV770,
	THERMAL_TYPE_ADT7473_WITH_INTERNAL,
	THERMAL_TYPE_EVERGREEN,
	THERMAL_TYPE_SUMO,
	THERMAL_TYPE_NI,
	THERMAL_TYPE_SI,
	THERMAL_TYPE_EMC2103_WITH_INTERNAL,
	THERMAL_TYPE_CI,
	THERMAL_TYPE_KV,
};

enum amdgpu_dpm_auto_throttle_src {
	AMDGPU_DPM_AUTO_THROTTLE_SRC_THERMAL,
	AMDGPU_DPM_AUTO_THROTTLE_SRC_EXTERNAL
};

enum amdgpu_dpm_event_src {
	AMDGPU_DPM_EVENT_SRC_ANALOG = 0,
	AMDGPU_DPM_EVENT_SRC_EXTERNAL = 1,
	AMDGPU_DPM_EVENT_SRC_DIGITAL = 2,
	AMDGPU_DPM_EVENT_SRC_ANALOG_OR_EXTERNAL = 3,
	AMDGPU_DPM_EVENT_SRC_DIGIAL_OR_EXTERNAL = 4
};

#define AMDGPU_MAX_VCE_LEVELS 6

enum amdgpu_vce_level {
	AMDGPU_VCE_LEVEL_AC_ALL = 0,     /* AC, All cases */
	AMDGPU_VCE_LEVEL_DC_EE = 1,      /* DC, entropy encoding */
	AMDGPU_VCE_LEVEL_DC_LL_LOW = 2,  /* DC, low latency queue, res <= 720 */
	AMDGPU_VCE_LEVEL_DC_LL_HIGH = 3, /* DC, low latency queue, 1080 >= res > 720 */
	AMDGPU_VCE_LEVEL_DC_GP_LOW = 4,  /* DC, general purpose queue, res <= 720 */
	AMDGPU_VCE_LEVEL_DC_GP_HIGH = 5, /* DC, general purpose queue, 1080 >= res > 720 */
};

struct amdgpu_ps {
	u32 caps; /* vbios flags */
	u32 class; /* vbios flags */
	u32 class2; /* vbios flags */
	/* UVD clocks */
	u32 vclk;
	u32 dclk;
	/* VCE clocks */
	u32 evclk;
	u32 ecclk;
	bool vce_active;
	enum amdgpu_vce_level vce_level;
	/* asic priv */
	void *ps_priv;
};

struct amdgpu_dpm_thermal {
	/* thermal interrupt work */
	struct work_struct work;
	/* low temperature threshold */
	int                min_temp;
	/* high temperature threshold */
	int                max_temp;
	/* was last interrupt low to high or high to low */
	bool               high_to_low;
	/* interrupt source */
	struct amdgpu_irq_src	irq;
};

enum amdgpu_clk_action
{
	AMDGPU_SCLK_UP = 1,
	AMDGPU_SCLK_DOWN
};

struct amdgpu_blacklist_clocks
{
	u32 sclk;
	u32 mclk;
	enum amdgpu_clk_action action;
};

struct amdgpu_clock_and_voltage_limits {
	u32 sclk;
	u32 mclk;
	u16 vddc;
	u16 vddci;
};

struct amdgpu_clock_array {
	u32 count;
	u32 *values;
};

struct amdgpu_clock_voltage_dependency_entry {
	u32 clk;
	u16 v;
};

struct amdgpu_clock_voltage_dependency_table {
	u32 count;
	struct amdgpu_clock_voltage_dependency_entry *entries;
};

union amdgpu_cac_leakage_entry {
	struct {
		u16 vddc;
		u32 leakage;
	};
	struct {
		u16 vddc1;
		u16 vddc2;
		u16 vddc3;
	};
};

struct amdgpu_cac_leakage_table {
	u32 count;
	union amdgpu_cac_leakage_entry *entries;
};

struct amdgpu_phase_shedding_limits_entry {
	u16 voltage;
	u32 sclk;
	u32 mclk;
};

struct amdgpu_phase_shedding_limits_table {
	u32 count;
	struct amdgpu_phase_shedding_limits_entry *entries;
};

struct amdgpu_uvd_clock_voltage_dependency_entry {
	u32 vclk;
	u32 dclk;
	u16 v;
};

struct amdgpu_uvd_clock_voltage_dependency_table {
	u8 count;
	struct amdgpu_uvd_clock_voltage_dependency_entry *entries;
};

struct amdgpu_vce_clock_voltage_dependency_entry {
	u32 ecclk;
	u32 evclk;
	u16 v;
};

struct amdgpu_vce_clock_voltage_dependency_table {
	u8 count;
	struct amdgpu_vce_clock_voltage_dependency_entry *entries;
};

struct amdgpu_ppm_table {
	u8 ppm_design;
	u16 cpu_core_number;
	u32 platform_tdp;
	u32 small_ac_platform_tdp;
	u32 platform_tdc;
	u32 small_ac_platform_tdc;
	u32 apu_tdp;
	u32 dgpu_tdp;
	u32 dgpu_ulv_power;
	u32 tj_max;
};

struct amdgpu_cac_tdp_table {
	u16 tdp;
	u16 configurable_tdp;
	u16 tdc;
	u16 battery_power_limit;
	u16 small_power_limit;
	u16 low_cac_leakage;
	u16 high_cac_leakage;
	u16 maximum_power_delivery_limit;
};

struct amdgpu_dpm_dynamic_state {
	struct amdgpu_clock_voltage_dependency_table vddc_dependency_on_sclk;
	struct amdgpu_clock_voltage_dependency_table vddci_dependency_on_mclk;
	struct amdgpu_clock_voltage_dependency_table vddc_dependency_on_mclk;
	struct amdgpu_clock_voltage_dependency_table mvdd_dependency_on_mclk;
	struct amdgpu_clock_voltage_dependency_table vddc_dependency_on_dispclk;
	struct amdgpu_uvd_clock_voltage_dependency_table uvd_clock_voltage_dependency_table;
	struct amdgpu_vce_clock_voltage_dependency_table vce_clock_voltage_dependency_table;
	struct amdgpu_clock_voltage_dependency_table samu_clock_voltage_dependency_table;
	struct amdgpu_clock_voltage_dependency_table acp_clock_voltage_dependency_table;
	struct amdgpu_clock_voltage_dependency_table vddgfx_dependency_on_sclk;
	struct amdgpu_clock_array valid_sclk_values;
	struct amdgpu_clock_array valid_mclk_values;
	struct amdgpu_clock_and_voltage_limits max_clock_voltage_on_dc;
	struct amdgpu_clock_and_voltage_limits max_clock_voltage_on_ac;
	u32 mclk_sclk_ratio;
	u32 sclk_mclk_delta;
	u16 vddc_vddci_delta;
	u16 min_vddc_for_pcie_gen2;
	struct amdgpu_cac_leakage_table cac_leakage_table;
	struct amdgpu_phase_shedding_limits_table phase_shedding_limits_table;
	struct amdgpu_ppm_table *ppm_table;
	struct amdgpu_cac_tdp_table *cac_tdp_table;
};

struct amdgpu_dpm_fan {
	u16 t_min;
	u16 t_med;
	u16 t_high;
	u16 pwm_min;
	u16 pwm_med;
	u16 pwm_high;
	u8 t_hyst;
	u32 cycle_delay;
	u16 t_max;
	u8 control_mode;
	u16 default_max_fan_pwm;
	u16 default_fan_output_sensitivity;
	u16 fan_output_sensitivity;
	bool ucode_fan_control;
};

enum amdgpu_pcie_gen {
	AMDGPU_PCIE_GEN1 = 0,
	AMDGPU_PCIE_GEN2 = 1,
	AMDGPU_PCIE_GEN3 = 2,
	AMDGPU_PCIE_GEN_INVALID = 0xffff
};

enum amdgpu_dpm_forced_level {
	AMDGPU_DPM_FORCED_LEVEL_AUTO = 0,
	AMDGPU_DPM_FORCED_LEVEL_LOW = 1,
	AMDGPU_DPM_FORCED_LEVEL_HIGH = 2,
};

struct amdgpu_vce_state {
	/* vce clocks */
	u32 evclk;
	u32 ecclk;
	/* gpu clocks */
	u32 sclk;
	u32 mclk;
	u8 clk_idx;
	u8 pstate;
};

struct amdgpu_dpm_funcs {
	int (*get_temperature)(struct amdgpu_device *adev);
	int (*pre_set_power_state)(struct amdgpu_device *adev);
	int (*set_power_state)(struct amdgpu_device *adev);
	void (*post_set_power_state)(struct amdgpu_device *adev);
	void (*display_configuration_changed)(struct amdgpu_device *adev);
	u32 (*get_sclk)(struct amdgpu_device *adev, bool low);
	u32 (*get_mclk)(struct amdgpu_device *adev, bool low);
	void (*print_power_state)(struct amdgpu_device *adev, struct amdgpu_ps *ps);
	void (*debugfs_print_current_performance_level)(struct amdgpu_device *adev, struct seq_file *m);
	int (*force_performance_level)(struct amdgpu_device *adev, enum amdgpu_dpm_forced_level level);
	bool (*vblank_too_short)(struct amdgpu_device *adev);
	void (*powergate_uvd)(struct amdgpu_device *adev, bool gate);
	void (*powergate_vce)(struct amdgpu_device *adev, bool gate);
	void (*enable_bapm)(struct amdgpu_device *adev, bool enable);
	void (*set_fan_control_mode)(struct amdgpu_device *adev, u32 mode);
	u32 (*get_fan_control_mode)(struct amdgpu_device *adev);
	int (*set_fan_speed_percent)(struct amdgpu_device *adev, u32 speed);
	int (*get_fan_speed_percent)(struct amdgpu_device *adev, u32 *speed);
};

struct amdgpu_dpm {
	struct amdgpu_ps        *ps;
	/* number of valid power states */
	int                     num_ps;
	/* current power state that is active */
	struct amdgpu_ps        *current_ps;
	/* requested power state */
	struct amdgpu_ps        *requested_ps;
	/* boot up power state */
	struct amdgpu_ps        *boot_ps;
	/* default uvd power state */
	struct amdgpu_ps        *uvd_ps;
	/* vce requirements */
	struct amdgpu_vce_state vce_states[AMDGPU_MAX_VCE_LEVELS];
	enum amdgpu_vce_level vce_level;
	enum amdgpu_pm_state_type state;
	enum amdgpu_pm_state_type user_state;
	u32                     platform_caps;
	u32                     voltage_response_time;
	u32                     backbias_response_time;
	void                    *priv;
	u32			new_active_crtcs;
	int			new_active_crtc_count;
	u32			current_active_crtcs;
	int			current_active_crtc_count;
	struct amdgpu_dpm_dynamic_state dyn_state;
	struct amdgpu_dpm_fan fan;
	u32 tdp_limit;
	u32 near_tdp_limit;
	u32 near_tdp_limit_adjusted;
	u32 sq_ramping_threshold;
	u32 cac_leakage;
	u16 tdp_od_limit;
	u32 tdp_adjustment;
	u16 load_line_slope;
	bool power_control;
	bool ac_power;
	/* special states active */
	bool                    thermal_active;
	bool                    uvd_active;
	bool                    vce_active;
	/* thermal handling */
	struct amdgpu_dpm_thermal thermal;
	/* forced levels */
	enum amdgpu_dpm_forced_level forced_level;
};

struct amdgpu_pm {
	struct mutex		mutex;
	u32                     current_sclk;
	u32                     current_mclk;
	u32                     default_sclk;
	u32                     default_mclk;
	struct amdgpu_i2c_chan *i2c_bus;
	/* internal thermal controller on rv6xx+ */
	enum amdgpu_int_thermal_type int_thermal_type;
	struct device	        *int_hwmon_dev;
	/* fan control parameters */
	bool                    no_fan;
	u8                      fan_pulses_per_revolution;
	u8                      fan_min_rpm;
	u8                      fan_max_rpm;
	/* dpm */
	bool                    dpm_enabled;
	bool                    sysfs_initialized;
	struct amdgpu_dpm       dpm;
	const struct firmware	*fw;	/* SMC firmware */
	uint32_t                fw_version;
	const struct amdgpu_dpm_funcs *funcs;
};

/*
 * UVD
 */
#define AMDGPU_MAX_UVD_HANDLES	10
#define AMDGPU_UVD_STACK_SIZE	(1024*1024)
#define AMDGPU_UVD_HEAP_SIZE	(1024*1024)
#define AMDGPU_UVD_FIRMWARE_OFFSET 256

struct amdgpu_uvd {
	struct amdgpu_bo	*vcpu_bo;
	void			*cpu_addr;
	uint64_t		gpu_addr;
	unsigned		fw_version;
	atomic_t		handles[AMDGPU_MAX_UVD_HANDLES];
	struct drm_file		*filp[AMDGPU_MAX_UVD_HANDLES];
	struct delayed_work	idle_work;
	const struct firmware	*fw;	/* UVD firmware */
	struct amdgpu_ring	ring;
	struct amdgpu_irq_src	irq;
	bool			address_64_bit;
};

/*
 * VCE
 */
#define AMDGPU_MAX_VCE_HANDLES	16
#define AMDGPU_VCE_FIRMWARE_OFFSET 256

#define AMDGPU_VCE_HARVEST_VCE0 (1 << 0)
#define AMDGPU_VCE_HARVEST_VCE1 (1 << 1)

struct amdgpu_vce {
	struct amdgpu_bo	*vcpu_bo;
	uint64_t		gpu_addr;
	unsigned		fw_version;
	unsigned		fb_version;
	atomic_t		handles[AMDGPU_MAX_VCE_HANDLES];
	struct drm_file		*filp[AMDGPU_MAX_VCE_HANDLES];
	uint32_t		img_size[AMDGPU_MAX_VCE_HANDLES];
	struct delayed_work	idle_work;
	const struct firmware	*fw;	/* VCE firmware */
	struct amdgpu_ring	ring[AMDGPU_MAX_VCE_RINGS];
	struct amdgpu_irq_src	irq;
	unsigned		harvest_config;
};

/*
 * SDMA
 */
struct amdgpu_sdma_instance {
	/* SDMA firmware */
	const struct firmware	*fw;
	uint32_t		fw_version;
	uint32_t		feature_version;

	struct amdgpu_ring	ring;
	bool			burst_nop;
};

struct amdgpu_sdma {
	struct amdgpu_sdma_instance instance[AMDGPU_MAX_SDMA_INSTANCES];
	struct amdgpu_irq_src	trap_irq;
	struct amdgpu_irq_src	illegal_inst_irq;
	int 			num_instances;
};

/*
 * Firmware
 */
struct amdgpu_firmware {
	struct amdgpu_firmware_info ucode[AMDGPU_UCODE_ID_MAXIMUM];
	bool smu_load;
	struct amdgpu_bo *fw_buf;
	unsigned int fw_size;
};

/*
 * Benchmarking
 */
void amdgpu_benchmark(struct amdgpu_device *adev, int test_number);


/*
 * Testing
 */
void amdgpu_test_moves(struct amdgpu_device *adev);
void amdgpu_test_ring_sync(struct amdgpu_device *adev,
			   struct amdgpu_ring *cpA,
			   struct amdgpu_ring *cpB);
void amdgpu_test_syncing(struct amdgpu_device *adev);

/*
 * MMU Notifier
 */
#if defined(CONFIG_MMU_NOTIFIER)
int amdgpu_mn_register(struct amdgpu_bo *bo, unsigned long addr);
void amdgpu_mn_unregister(struct amdgpu_bo *bo);
#else
static inline int amdgpu_mn_register(struct amdgpu_bo *bo, unsigned long addr)
{
	return -ENODEV;
}
static inline void amdgpu_mn_unregister(struct amdgpu_bo *bo) {}
#endif

/*
 * Debugfs
 */
struct amdgpu_debugfs {
	struct drm_info_list	*files;
	unsigned		num_files;
};

int amdgpu_debugfs_add_files(struct amdgpu_device *adev,
			     struct drm_info_list *files,
			     unsigned nfiles);
int amdgpu_debugfs_fence_init(struct amdgpu_device *adev);

#if defined(CONFIG_DEBUG_FS)
int amdgpu_debugfs_init(struct drm_minor *minor);
void amdgpu_debugfs_cleanup(struct drm_minor *minor);
#endif

/*
 * amdgpu smumgr functions
 */
struct amdgpu_smumgr_funcs {
	int (*check_fw_load_finish)(struct amdgpu_device *adev, uint32_t fwtype);
	int (*request_smu_load_fw)(struct amdgpu_device *adev);
	int (*request_smu_specific_fw)(struct amdgpu_device *adev, uint32_t fwtype);
};

/*
 * amdgpu smumgr
 */
struct amdgpu_smumgr {
	struct amdgpu_bo *toc_buf;
	struct amdgpu_bo *smu_buf;
	/* asic priv smu data */
	void *priv;
	spinlock_t smu_lock;
	/* smumgr functions */
	const struct amdgpu_smumgr_funcs *smumgr_funcs;
	/* ucode loading complete flag */
	uint32_t fw_flags;
};

/*
 * ASIC specific register table accessible by UMD
 */
struct amdgpu_allowed_register_entry {
	uint32_t reg_offset;
	bool untouched;
	bool grbm_indexed;
};

struct amdgpu_cu_info {
	uint32_t number; /* total active CU number */
	uint32_t ao_cu_mask;
	uint32_t bitmap[4][4];
};


/*
 * ASIC specific functions.
 */
struct amdgpu_asic_funcs {
	bool (*read_disabled_bios)(struct amdgpu_device *adev);
	int (*read_register)(struct amdgpu_device *adev, u32 se_num,
			     u32 sh_num, u32 reg_offset, u32 *value);
	void (*set_vga_state)(struct amdgpu_device *adev, bool state);
	int (*reset)(struct amdgpu_device *adev);
	/* wait for mc_idle */
	int (*wait_for_mc_idle)(struct amdgpu_device *adev);
	/* get the reference clock */
	u32 (*get_xclk)(struct amdgpu_device *adev);
	/* get the gpu clock counter */
	uint64_t (*get_gpu_clock_counter)(struct amdgpu_device *adev);
	int (*get_cu_info)(struct amdgpu_device *adev, struct amdgpu_cu_info *info);
	/* MM block clocks */
	int (*set_uvd_clocks)(struct amdgpu_device *adev, u32 vclk, u32 dclk);
	int (*set_vce_clocks)(struct amdgpu_device *adev, u32 evclk, u32 ecclk);
};

/*
 * IOCTL.
 */
int amdgpu_gem_create_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *filp);
int amdgpu_bo_list_ioctl(struct drm_device *dev, void *data,
				struct drm_file *filp);

int amdgpu_gem_info_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *filp);
int amdgpu_gem_userptr_ioctl(struct drm_device *dev, void *data,
			struct drm_file *filp);
int amdgpu_gem_mmap_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *filp);
int amdgpu_gem_wait_idle_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *filp);
int amdgpu_gem_va_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *filp);
int amdgpu_gem_op_ioctl(struct drm_device *dev, void *data,
			struct drm_file *filp);
int amdgpu_cs_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
int amdgpu_cs_wait_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);

int amdgpu_gem_metadata_ioctl(struct drm_device *dev, void *data,
				struct drm_file *filp);

/* VRAM scratch page for HDP bug, default vram page */
struct amdgpu_vram_scratch {
	struct amdgpu_bo		*robj;
	volatile uint32_t		*ptr;
	u64				gpu_addr;
};

/*
 * ACPI
 */
struct amdgpu_atif_notification_cfg {
	bool enabled;
	int command_code;
};

struct amdgpu_atif_notifications {
	bool display_switch;
	bool expansion_mode_change;
	bool thermal_state;
	bool forced_power_state;
	bool system_power_state;
	bool display_conf_change;
	bool px_gfx_switch;
	bool brightness_change;
	bool dgpu_display_event;
};

struct amdgpu_atif_functions {
	bool system_params;
	bool sbios_requests;
	bool select_active_disp;
	bool lid_state;
	bool get_tv_standard;
	bool set_tv_standard;
	bool get_panel_expansion_mode;
	bool set_panel_expansion_mode;
	bool temperature_change;
	bool graphics_device_types;
};

struct amdgpu_atif {
	struct amdgpu_atif_notifications notifications;
	struct amdgpu_atif_functions functions;
	struct amdgpu_atif_notification_cfg notification_cfg;
	struct amdgpu_encoder *encoder_for_bl;
};

struct amdgpu_atcs_functions {
	bool get_ext_state;
	bool pcie_perf_req;
	bool pcie_dev_rdy;
	bool pcie_bus_width;
};

struct amdgpu_atcs {
	struct amdgpu_atcs_functions functions;
};

/*
 * CGS
 */
void *amdgpu_cgs_create_device(struct amdgpu_device *adev);
void amdgpu_cgs_destroy_device(void *cgs_device);


/*
 * Core structure, functions and helpers.
 */
typedef uint32_t (*amdgpu_rreg_t)(struct amdgpu_device*, uint32_t);
typedef void (*amdgpu_wreg_t)(struct amdgpu_device*, uint32_t, uint32_t);

typedef uint32_t (*amdgpu_block_rreg_t)(struct amdgpu_device*, uint32_t, uint32_t);
typedef void (*amdgpu_block_wreg_t)(struct amdgpu_device*, uint32_t, uint32_t, uint32_t);

struct amdgpu_ip_block_status {
	bool valid;
	bool sw;
	bool hw;
};

struct amdgpu_device {
	struct device			*dev;
	struct drm_device		*ddev;
	struct pci_dev			*pdev;

	/* ASIC */
	enum amd_asic_type		asic_type;
	uint32_t			family;
	uint32_t			rev_id;
	uint32_t			external_rev_id;
	unsigned long			flags;
	int				usec_timeout;
	const struct amdgpu_asic_funcs	*asic_funcs;
	bool				shutdown;
	bool				suspend;
	bool				need_dma32;
	bool				accel_working;
	struct work_struct 		reset_work;
	struct notifier_block		acpi_nb;
	struct amdgpu_i2c_chan		*i2c_bus[AMDGPU_MAX_I2C_BUS];
	struct amdgpu_debugfs		debugfs[AMDGPU_DEBUGFS_MAX_COMPONENTS];
	unsigned 			debugfs_count;
#if defined(CONFIG_DEBUG_FS)
	struct dentry			*debugfs_regs;
#endif
	struct amdgpu_atif		atif;
	struct amdgpu_atcs		atcs;
	struct mutex			srbm_mutex;
	/* GRBM index mutex. Protects concurrent access to GRBM index */
	struct mutex                    grbm_idx_mutex;
	struct dev_pm_domain		vga_pm_domain;
	bool				have_disp_power_ref;

	/* BIOS */
	uint8_t				*bios;
	bool				is_atom_bios;
	uint16_t			bios_header_start;
	struct amdgpu_bo		*stollen_vga_memory;
	uint32_t			bios_scratch[AMDGPU_BIOS_NUM_SCRATCH];

	/* Register/doorbell mmio */
	resource_size_t			rmmio_base;
	resource_size_t			rmmio_size;
	void __iomem			*rmmio;
	/* protects concurrent MM_INDEX/DATA based register access */
	spinlock_t mmio_idx_lock;
	/* protects concurrent SMC based register access */
	spinlock_t smc_idx_lock;
	amdgpu_rreg_t			smc_rreg;
	amdgpu_wreg_t			smc_wreg;
	/* protects concurrent PCIE register access */
	spinlock_t pcie_idx_lock;
	amdgpu_rreg_t			pcie_rreg;
	amdgpu_wreg_t			pcie_wreg;
	/* protects concurrent UVD register access */
	spinlock_t uvd_ctx_idx_lock;
	amdgpu_rreg_t			uvd_ctx_rreg;
	amdgpu_wreg_t			uvd_ctx_wreg;
	/* protects concurrent DIDT register access */
	spinlock_t didt_idx_lock;
	amdgpu_rreg_t			didt_rreg;
	amdgpu_wreg_t			didt_wreg;
	/* protects concurrent ENDPOINT (audio) register access */
	spinlock_t audio_endpt_idx_lock;
	amdgpu_block_rreg_t		audio_endpt_rreg;
	amdgpu_block_wreg_t		audio_endpt_wreg;
	void __iomem                    *rio_mem;
	resource_size_t			rio_mem_size;
	struct amdgpu_doorbell		doorbell;

	/* clock/pll info */
	struct amdgpu_clock            clock;

	/* MC */
	struct amdgpu_mc		mc;
	struct amdgpu_gart		gart;
	struct amdgpu_dummy_page	dummy_page;
	struct amdgpu_vm_manager	vm_manager;

	/* memory management */
	struct amdgpu_mman		mman;
	struct amdgpu_gem		gem;
	struct amdgpu_vram_scratch	vram_scratch;
	struct amdgpu_wb		wb;
	atomic64_t			vram_usage;
	atomic64_t			vram_vis_usage;
	atomic64_t			gtt_usage;
	atomic64_t			num_bytes_moved;
	atomic_t			gpu_reset_counter;

	/* display */
	struct amdgpu_mode_info		mode_info;
	struct work_struct		hotplug_work;
	struct amdgpu_irq_src		crtc_irq;
	struct amdgpu_irq_src		pageflip_irq;
	struct amdgpu_irq_src		hpd_irq;

	/* rings */
	unsigned			fence_context;
	struct mutex			ring_lock;
	unsigned			num_rings;
	struct amdgpu_ring		*rings[AMDGPU_MAX_RINGS];
	bool				ib_pool_ready;
	struct amdgpu_sa_manager	ring_tmp_bo;

	/* interrupts */
	struct amdgpu_irq		irq;

	/* dpm */
	struct amdgpu_pm		pm;
	u32				cg_flags;
	u32				pg_flags;

	/* amdgpu smumgr */
	struct amdgpu_smumgr smu;

	/* gfx */
	struct amdgpu_gfx		gfx;

	/* sdma */
	struct amdgpu_sdma		sdma;

	/* uvd */
	bool				has_uvd;
	struct amdgpu_uvd		uvd;

	/* vce */
	struct amdgpu_vce		vce;

	/* firmwares */
	struct amdgpu_firmware		firmware;

	/* GDS */
	struct amdgpu_gds		gds;

	const struct amdgpu_ip_block_version *ip_blocks;
	int				num_ip_blocks;
	struct amdgpu_ip_block_status	*ip_block_status;
	struct mutex	mn_lock;
	DECLARE_HASHTABLE(mn_hash, 7);

	/* tracking pinned memory */
	u64 vram_pin_size;
	u64 gart_pin_size;

	/* amdkfd interface */
	struct kfd_dev          *kfd;

	/* kernel conext for IB submission */
	struct amdgpu_ctx	kernel_ctx;
};

bool amdgpu_device_is_px(struct drm_device *dev);
int amdgpu_device_init(struct amdgpu_device *adev,
		       struct drm_device *ddev,
		       struct pci_dev *pdev,
		       uint32_t flags);
void amdgpu_device_fini(struct amdgpu_device *adev);
int amdgpu_gpu_wait_for_idle(struct amdgpu_device *adev);

uint32_t amdgpu_mm_rreg(struct amdgpu_device *adev, uint32_t reg,
			bool always_indirect);
void amdgpu_mm_wreg(struct amdgpu_device *adev, uint32_t reg, uint32_t v,
		    bool always_indirect);
u32 amdgpu_io_rreg(struct amdgpu_device *adev, u32 reg);
void amdgpu_io_wreg(struct amdgpu_device *adev, u32 reg, u32 v);

u32 amdgpu_mm_rdoorbell(struct amdgpu_device *adev, u32 index);
void amdgpu_mm_wdoorbell(struct amdgpu_device *adev, u32 index, u32 v);

/*
 * Cast helper
 */
extern const struct fence_ops amdgpu_fence_ops;
static inline struct amdgpu_fence *to_amdgpu_fence(struct fence *f)
{
	struct amdgpu_fence *__f = container_of(f, struct amdgpu_fence, base);

	if (__f->base.ops == &amdgpu_fence_ops)
		return __f;

	return NULL;
}

/*
 * Registers read & write functions.
 */
#define RREG32(reg) amdgpu_mm_rreg(adev, (reg), false)
#define RREG32_IDX(reg) amdgpu_mm_rreg(adev, (reg), true)
#define DREG32(reg) printk(KERN_INFO "REGISTER: " #reg " : 0x%08X\n", amdgpu_mm_rreg(adev, (reg), false))
#define WREG32(reg, v) amdgpu_mm_wreg(adev, (reg), (v), false)
#define WREG32_IDX(reg, v) amdgpu_mm_wreg(adev, (reg), (v), true)
#define REG_SET(FIELD, v) (((v) << FIELD##_SHIFT) & FIELD##_MASK)
#define REG_GET(FIELD, v) (((v) << FIELD##_SHIFT) & FIELD##_MASK)
#define RREG32_PCIE(reg) adev->pcie_rreg(adev, (reg))
#define WREG32_PCIE(reg, v) adev->pcie_wreg(adev, (reg), (v))
#define RREG32_SMC(reg) adev->smc_rreg(adev, (reg))
#define WREG32_SMC(reg, v) adev->smc_wreg(adev, (reg), (v))
#define RREG32_UVD_CTX(reg) adev->uvd_ctx_rreg(adev, (reg))
#define WREG32_UVD_CTX(reg, v) adev->uvd_ctx_wreg(adev, (reg), (v))
#define RREG32_DIDT(reg) adev->didt_rreg(adev, (reg))
#define WREG32_DIDT(reg, v) adev->didt_wreg(adev, (reg), (v))
#define RREG32_AUDIO_ENDPT(block, reg) adev->audio_endpt_rreg(adev, (block), (reg))
#define WREG32_AUDIO_ENDPT(block, reg, v) adev->audio_endpt_wreg(adev, (block), (reg), (v))
#define WREG32_P(reg, val, mask)				\
	do {							\
		uint32_t tmp_ = RREG32(reg);			\
		tmp_ &= (mask);					\
		tmp_ |= ((val) & ~(mask));			\
		WREG32(reg, tmp_);				\
	} while (0)
#define WREG32_AND(reg, and) WREG32_P(reg, 0, and)
#define WREG32_OR(reg, or) WREG32_P(reg, or, ~(or))
#define WREG32_PLL_P(reg, val, mask)				\
	do {							\
		uint32_t tmp_ = RREG32_PLL(reg);		\
		tmp_ &= (mask);					\
		tmp_ |= ((val) & ~(mask));			\
		WREG32_PLL(reg, tmp_);				\
	} while (0)
#define DREG32_SYS(sqf, adev, reg) seq_printf((sqf), #reg " : 0x%08X\n", amdgpu_mm_rreg((adev), (reg), false))
#define RREG32_IO(reg) amdgpu_io_rreg(adev, (reg))
#define WREG32_IO(reg, v) amdgpu_io_wreg(adev, (reg), (v))

#define RDOORBELL32(index) amdgpu_mm_rdoorbell(adev, (index))
#define WDOORBELL32(index, v) amdgpu_mm_wdoorbell(adev, (index), (v))

#define REG_FIELD_SHIFT(reg, field) reg##__##field##__SHIFT
#define REG_FIELD_MASK(reg, field) reg##__##field##_MASK

#define REG_SET_FIELD(orig_val, reg, field, field_val)			\
	(((orig_val) & ~REG_FIELD_MASK(reg, field)) |			\
	 (REG_FIELD_MASK(reg, field) & ((field_val) << REG_FIELD_SHIFT(reg, field))))

#define REG_GET_FIELD(value, reg, field)				\
	(((value) & REG_FIELD_MASK(reg, field)) >> REG_FIELD_SHIFT(reg, field))

/*
 * BIOS helpers.
 */
#define RBIOS8(i) (adev->bios[i])
#define RBIOS16(i) (RBIOS8(i) | (RBIOS8((i)+1) << 8))
#define RBIOS32(i) ((RBIOS16(i)) | (RBIOS16((i)+2) << 16))

/*
 * RING helpers.
 */
static inline void amdgpu_ring_write(struct amdgpu_ring *ring, uint32_t v)
{
	if (ring->count_dw <= 0)
		DRM_ERROR("amdgpu: writing more dwords to the ring than expected!\n");
	ring->ring[ring->wptr++] = v;
	ring->wptr &= ring->ptr_mask;
	ring->count_dw--;
	ring->ring_free_dw--;
}

static inline struct amdgpu_sdma_instance *
amdgpu_get_sdma_instance(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++)
		if (&adev->sdma.instance[i].ring == ring)
			break;

	if (i < AMDGPU_MAX_SDMA_INSTANCES)
		return &adev->sdma.instance[i];
	else
		return NULL;
}

/*
 * ASICs macro.
 */
#define amdgpu_asic_set_vga_state(adev, state) (adev)->asic_funcs->set_vga_state((adev), (state))
#define amdgpu_asic_reset(adev) (adev)->asic_funcs->reset((adev))
#define amdgpu_asic_wait_for_mc_idle(adev) (adev)->asic_funcs->wait_for_mc_idle((adev))
#define amdgpu_asic_get_xclk(adev) (adev)->asic_funcs->get_xclk((adev))
#define amdgpu_asic_set_uvd_clocks(adev, v, d) (adev)->asic_funcs->set_uvd_clocks((adev), (v), (d))
#define amdgpu_asic_set_vce_clocks(adev, ev, ec) (adev)->asic_funcs->set_vce_clocks((adev), (ev), (ec))
#define amdgpu_asic_get_gpu_clock_counter(adev) (adev)->asic_funcs->get_gpu_clock_counter((adev))
#define amdgpu_asic_read_disabled_bios(adev) (adev)->asic_funcs->read_disabled_bios((adev))
#define amdgpu_asic_read_register(adev, se, sh, offset, v)((adev)->asic_funcs->read_register((adev), (se), (sh), (offset), (v)))
#define amdgpu_asic_get_cu_info(adev, info) (adev)->asic_funcs->get_cu_info((adev), (info))
#define amdgpu_gart_flush_gpu_tlb(adev, vmid) (adev)->gart.gart_funcs->flush_gpu_tlb((adev), (vmid))
#define amdgpu_gart_set_pte_pde(adev, pt, idx, addr, flags) (adev)->gart.gart_funcs->set_pte_pde((adev), (pt), (idx), (addr), (flags))
#define amdgpu_vm_copy_pte(adev, ib, pe, src, count) ((adev)->vm_manager.vm_pte_funcs->copy_pte((ib), (pe), (src), (count)))
#define amdgpu_vm_write_pte(adev, ib, pe, addr, count, incr, flags) ((adev)->vm_manager.vm_pte_funcs->write_pte((ib), (pe), (addr), (count), (incr), (flags)))
#define amdgpu_vm_set_pte_pde(adev, ib, pe, addr, count, incr, flags) ((adev)->vm_manager.vm_pte_funcs->set_pte_pde((ib), (pe), (addr), (count), (incr), (flags)))
#define amdgpu_vm_pad_ib(adev, ib) ((adev)->vm_manager.vm_pte_funcs->pad_ib((ib)))
#define amdgpu_ring_parse_cs(r, p, ib) ((r)->funcs->parse_cs((p), (ib)))
#define amdgpu_ring_test_ring(r) (r)->funcs->test_ring((r))
#define amdgpu_ring_test_ib(r) (r)->funcs->test_ib((r))
#define amdgpu_ring_get_rptr(r) (r)->funcs->get_rptr((r))
#define amdgpu_ring_get_wptr(r) (r)->funcs->get_wptr((r))
#define amdgpu_ring_set_wptr(r) (r)->funcs->set_wptr((r))
#define amdgpu_ring_emit_ib(r, ib) (r)->funcs->emit_ib((r), (ib))
#define amdgpu_ring_emit_vm_flush(r, vmid, addr) (r)->funcs->emit_vm_flush((r), (vmid), (addr))
#define amdgpu_ring_emit_fence(r, addr, seq, flags) (r)->funcs->emit_fence((r), (addr), (seq), (flags))
#define amdgpu_ring_emit_semaphore(r, semaphore, emit_wait) (r)->funcs->emit_semaphore((r), (semaphore), (emit_wait))
#define amdgpu_ring_emit_gds_switch(r, v, db, ds, wb, ws, ab, as) (r)->funcs->emit_gds_switch((r), (v), (db), (ds), (wb), (ws), (ab), (as))
#define amdgpu_ring_emit_hdp_flush(r) (r)->funcs->emit_hdp_flush((r))
#define amdgpu_ih_get_wptr(adev) (adev)->irq.ih_funcs->get_wptr((adev))
#define amdgpu_ih_decode_iv(adev, iv) (adev)->irq.ih_funcs->decode_iv((adev), (iv))
#define amdgpu_ih_set_rptr(adev) (adev)->irq.ih_funcs->set_rptr((adev))
#define amdgpu_display_set_vga_render_state(adev, r) (adev)->mode_info.funcs->set_vga_render_state((adev), (r))
#define amdgpu_display_vblank_get_counter(adev, crtc) (adev)->mode_info.funcs->vblank_get_counter((adev), (crtc))
#define amdgpu_display_vblank_wait(adev, crtc) (adev)->mode_info.funcs->vblank_wait((adev), (crtc))
#define amdgpu_display_is_display_hung(adev) (adev)->mode_info.funcs->is_display_hung((adev))
#define amdgpu_display_backlight_set_level(adev, e, l) (adev)->mode_info.funcs->backlight_set_level((e), (l))
#define amdgpu_display_backlight_get_level(adev, e) (adev)->mode_info.funcs->backlight_get_level((e))
#define amdgpu_display_hpd_sense(adev, h) (adev)->mode_info.funcs->hpd_sense((adev), (h))
#define amdgpu_display_hpd_set_polarity(adev, h) (adev)->mode_info.funcs->hpd_set_polarity((adev), (h))
#define amdgpu_display_hpd_get_gpio_reg(adev) (adev)->mode_info.funcs->hpd_get_gpio_reg((adev))
#define amdgpu_display_bandwidth_update(adev) (adev)->mode_info.funcs->bandwidth_update((adev))
#define amdgpu_display_page_flip(adev, crtc, base) (adev)->mode_info.funcs->page_flip((adev), (crtc), (base))
#define amdgpu_display_page_flip_get_scanoutpos(adev, crtc, vbl, pos) (adev)->mode_info.funcs->page_flip_get_scanoutpos((adev), (crtc), (vbl), (pos))
#define amdgpu_display_add_encoder(adev, e, s, c) (adev)->mode_info.funcs->add_encoder((adev), (e), (s), (c))
#define amdgpu_display_add_connector(adev, ci, sd, ct, ib, coi, h, r) (adev)->mode_info.funcs->add_connector((adev), (ci), (sd), (ct), (ib), (coi), (h), (r))
#define amdgpu_display_stop_mc_access(adev, s) (adev)->mode_info.funcs->stop_mc_access((adev), (s))
#define amdgpu_display_resume_mc_access(adev, s) (adev)->mode_info.funcs->resume_mc_access((adev), (s))
#define amdgpu_emit_copy_buffer(adev, ib, s, d, b) (adev)->mman.buffer_funcs->emit_copy_buffer((ib),  (s), (d), (b))
#define amdgpu_emit_fill_buffer(adev, ib, s, d, b) (adev)->mman.buffer_funcs->emit_fill_buffer((ib), (s), (d), (b))
#define amdgpu_dpm_get_temperature(adev) (adev)->pm.funcs->get_temperature((adev))
#define amdgpu_dpm_pre_set_power_state(adev) (adev)->pm.funcs->pre_set_power_state((adev))
#define amdgpu_dpm_set_power_state(adev) (adev)->pm.funcs->set_power_state((adev))
#define amdgpu_dpm_post_set_power_state(adev) (adev)->pm.funcs->post_set_power_state((adev))
#define amdgpu_dpm_display_configuration_changed(adev) (adev)->pm.funcs->display_configuration_changed((adev))
#define amdgpu_dpm_get_sclk(adev, l) (adev)->pm.funcs->get_sclk((adev), (l))
#define amdgpu_dpm_get_mclk(adev, l) (adev)->pm.funcs->get_mclk((adev), (l))
#define amdgpu_dpm_print_power_state(adev, ps) (adev)->pm.funcs->print_power_state((adev), (ps))
#define amdgpu_dpm_debugfs_print_current_performance_level(adev, m) (adev)->pm.funcs->debugfs_print_current_performance_level((adev), (m))
#define amdgpu_dpm_force_performance_level(adev, l) (adev)->pm.funcs->force_performance_level((adev), (l))
#define amdgpu_dpm_vblank_too_short(adev) (adev)->pm.funcs->vblank_too_short((adev))
#define amdgpu_dpm_powergate_uvd(adev, g) (adev)->pm.funcs->powergate_uvd((adev), (g))
#define amdgpu_dpm_powergate_vce(adev, g) (adev)->pm.funcs->powergate_vce((adev), (g))
#define amdgpu_dpm_enable_bapm(adev, e) (adev)->pm.funcs->enable_bapm((adev), (e))
#define amdgpu_dpm_set_fan_control_mode(adev, m) (adev)->pm.funcs->set_fan_control_mode((adev), (m))
#define amdgpu_dpm_get_fan_control_mode(adev) (adev)->pm.funcs->get_fan_control_mode((adev))
#define amdgpu_dpm_set_fan_speed_percent(adev, s) (adev)->pm.funcs->set_fan_speed_percent((adev), (s))
#define amdgpu_dpm_get_fan_speed_percent(adev, s) (adev)->pm.funcs->get_fan_speed_percent((adev), (s))

#define amdgpu_gds_switch(adev, r, v, d, w, a) (adev)->gds.funcs->patch_gds_switch((r), (v), (d), (w), (a))

/* Common functions */
int amdgpu_gpu_reset(struct amdgpu_device *adev);
void amdgpu_pci_config_reset(struct amdgpu_device *adev);
bool amdgpu_card_posted(struct amdgpu_device *adev);
void amdgpu_update_display_priority(struct amdgpu_device *adev);
bool amdgpu_boot_test_post_card(struct amdgpu_device *adev);

int amdgpu_cs_parser_init(struct amdgpu_cs_parser *p, void *data);
int amdgpu_cs_get_ring(struct amdgpu_device *adev, u32 ip_type,
		       u32 ip_instance, u32 ring,
		       struct amdgpu_ring **out_ring);
void amdgpu_ttm_placement_from_domain(struct amdgpu_bo *rbo, u32 domain);
bool amdgpu_ttm_bo_is_amdgpu_bo(struct ttm_buffer_object *bo);
int amdgpu_ttm_tt_set_userptr(struct ttm_tt *ttm, uint64_t addr,
				     uint32_t flags);
bool amdgpu_ttm_tt_has_userptr(struct ttm_tt *ttm);
bool amdgpu_ttm_tt_affect_userptr(struct ttm_tt *ttm, unsigned long start,
				  unsigned long end);
bool amdgpu_ttm_tt_is_readonly(struct ttm_tt *ttm);
uint32_t amdgpu_ttm_tt_pte_flags(struct amdgpu_device *adev, struct ttm_tt *ttm,
				 struct ttm_mem_reg *mem);
void amdgpu_vram_location(struct amdgpu_device *adev, struct amdgpu_mc *mc, u64 base);
void amdgpu_gtt_location(struct amdgpu_device *adev, struct amdgpu_mc *mc);
void amdgpu_ttm_set_active_vram_size(struct amdgpu_device *adev, u64 size);
void amdgpu_program_register_sequence(struct amdgpu_device *adev,
					     const u32 *registers,
					     const u32 array_size);

bool amdgpu_device_is_px(struct drm_device *dev);
/* atpx handler */
#if defined(CONFIG_VGA_SWITCHEROO)
void amdgpu_register_atpx_handler(void);
void amdgpu_unregister_atpx_handler(void);
#else
static inline void amdgpu_register_atpx_handler(void) {}
static inline void amdgpu_unregister_atpx_handler(void) {}
#endif

/*
 * KMS
 */
extern const struct drm_ioctl_desc amdgpu_ioctls_kms[];
extern int amdgpu_max_kms_ioctl;

int amdgpu_driver_load_kms(struct drm_device *dev, unsigned long flags);
int amdgpu_driver_unload_kms(struct drm_device *dev);
void amdgpu_driver_lastclose_kms(struct drm_device *dev);
int amdgpu_driver_open_kms(struct drm_device *dev, struct drm_file *file_priv);
void amdgpu_driver_postclose_kms(struct drm_device *dev,
				 struct drm_file *file_priv);
void amdgpu_driver_preclose_kms(struct drm_device *dev,
				struct drm_file *file_priv);
int amdgpu_suspend_kms(struct drm_device *dev, bool suspend, bool fbcon);
int amdgpu_resume_kms(struct drm_device *dev, bool resume, bool fbcon);
u32 amdgpu_get_vblank_counter_kms(struct drm_device *dev, unsigned int pipe);
int amdgpu_enable_vblank_kms(struct drm_device *dev, unsigned int pipe);
void amdgpu_disable_vblank_kms(struct drm_device *dev, unsigned int pipe);
int amdgpu_get_vblank_timestamp_kms(struct drm_device *dev, unsigned int pipe,
				    int *max_error,
				    struct timeval *vblank_time,
				    unsigned flags);
long amdgpu_kms_compat_ioctl(struct file *filp, unsigned int cmd,
			     unsigned long arg);

/*
 * functions used by amdgpu_encoder.c
 */
struct amdgpu_afmt_acr {
	u32 clock;

	int n_32khz;
	int cts_32khz;

	int n_44_1khz;
	int cts_44_1khz;

	int n_48khz;
	int cts_48khz;

};

struct amdgpu_afmt_acr amdgpu_afmt_acr(uint32_t clock);

/* amdgpu_acpi.c */
#if defined(CONFIG_ACPI)
int amdgpu_acpi_init(struct amdgpu_device *adev);
void amdgpu_acpi_fini(struct amdgpu_device *adev);
bool amdgpu_acpi_is_pcie_performance_request_supported(struct amdgpu_device *adev);
int amdgpu_acpi_pcie_performance_request(struct amdgpu_device *adev,
						u8 perf_req, bool advertise);
int amdgpu_acpi_pcie_notify_device_ready(struct amdgpu_device *adev);
#else
static inline int amdgpu_acpi_init(struct amdgpu_device *adev) { return 0; }
static inline void amdgpu_acpi_fini(struct amdgpu_device *adev) { }
#endif

struct amdgpu_bo_va_mapping *
amdgpu_cs_find_mapping(struct amdgpu_cs_parser *parser,
		       uint64_t addr, struct amdgpu_bo **bo);

#include "amdgpu_object.h"

#endif
