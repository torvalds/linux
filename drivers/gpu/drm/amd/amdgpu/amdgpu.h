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
#include <linux/rbtree.h>
#include <linux/hashtable.h>
#include <linux/dma-fence.h>

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
#include "amdgpu_ttm.h"
#include "amdgpu_psp.h"
#include "amdgpu_gds.h"
#include "amdgpu_sync.h"
#include "amdgpu_ring.h"
#include "amdgpu_vm.h"
#include "amd_powerplay.h"
#include "amdgpu_dpm.h"
#include "amdgpu_acp.h"
#include "amdgpu_uvd.h"
#include "amdgpu_vce.h"

#include "gpu_scheduler.h"
#include "amdgpu_virt.h"

/*
 * Modules parameters.
 */
extern int amdgpu_modeset;
extern int amdgpu_vram_limit;
extern int amdgpu_gart_size;
extern int amdgpu_moverate;
extern int amdgpu_benchmarking;
extern int amdgpu_testing;
extern int amdgpu_audio;
extern int amdgpu_disp_priority;
extern int amdgpu_hw_i2c;
extern int amdgpu_pcie_gen2;
extern int amdgpu_msi;
extern int amdgpu_lockup_timeout;
extern int amdgpu_dpm;
extern int amdgpu_fw_load_type;
extern int amdgpu_aspm;
extern int amdgpu_runtime_pm;
extern unsigned amdgpu_ip_block_mask;
extern int amdgpu_bapm;
extern int amdgpu_deep_color;
extern int amdgpu_vm_size;
extern int amdgpu_vm_block_size;
extern int amdgpu_vm_fault_stop;
extern int amdgpu_vm_debug;
extern int amdgpu_sched_jobs;
extern int amdgpu_sched_hw_submission;
extern int amdgpu_no_evict;
extern int amdgpu_direct_gma_size;
extern unsigned amdgpu_pcie_gen_cap;
extern unsigned amdgpu_pcie_lane_cap;
extern unsigned amdgpu_cg_mask;
extern unsigned amdgpu_pg_mask;
extern char *amdgpu_disable_cu;
extern char *amdgpu_virtual_display;
extern unsigned amdgpu_pp_feature_mask;
extern int amdgpu_vram_page_split;
extern int amdgpu_ngg;
extern int amdgpu_prim_buf_per_se;
extern int amdgpu_pos_buf_per_se;
extern int amdgpu_cntl_sb_buf_per_se;
extern int amdgpu_param_buf_per_se;

#define AMDGPU_DEFAULT_GTT_SIZE_MB		3072ULL /* 3GB by default */
#define AMDGPU_WAIT_IDLE_TIMEOUT_IN_MS	        3000
#define AMDGPU_MAX_USEC_TIMEOUT			100000	/* 100 ms */
#define AMDGPU_FENCE_JIFFIES_TIMEOUT		(HZ / 2)
/* AMDGPU_IB_POOL_SIZE must be a power of 2 */
#define AMDGPU_IB_POOL_SIZE			16
#define AMDGPU_DEBUGFS_MAX_COMPONENTS		32
#define AMDGPUFB_CONN_LIMIT			4
#define AMDGPU_BIOS_NUM_SCRATCH			16

/* max number of IP instances */
#define AMDGPU_MAX_SDMA_INSTANCES		2

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
struct amdgpu_ib;
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

enum amdgpu_kiq_irq {
	AMDGPU_CP_KIQ_IRQ_DRIVER0 = 0,
	AMDGPU_CP_KIQ_IRQ_LAST
};

int amdgpu_set_clockgating_state(struct amdgpu_device *adev,
				  enum amd_ip_block_type block_type,
				  enum amd_clockgating_state state);
int amdgpu_set_powergating_state(struct amdgpu_device *adev,
				  enum amd_ip_block_type block_type,
				  enum amd_powergating_state state);
void amdgpu_get_clockgating_state(struct amdgpu_device *adev, u32 *flags);
int amdgpu_wait_for_idle(struct amdgpu_device *adev,
			 enum amd_ip_block_type block_type);
bool amdgpu_is_idle(struct amdgpu_device *adev,
		    enum amd_ip_block_type block_type);

#define AMDGPU_MAX_IP_NUM 16

struct amdgpu_ip_block_status {
	bool valid;
	bool sw;
	bool hw;
	bool late_initialized;
	bool hang;
};

struct amdgpu_ip_block_version {
	const enum amd_ip_block_type type;
	const u32 major;
	const u32 minor;
	const u32 rev;
	const struct amd_ip_funcs *funcs;
};

struct amdgpu_ip_block {
	struct amdgpu_ip_block_status status;
	const struct amdgpu_ip_block_version *version;
};

int amdgpu_ip_block_version_cmp(struct amdgpu_device *adev,
				enum amd_ip_block_type type,
				u32 major, u32 minor);

struct amdgpu_ip_block * amdgpu_get_ip_block(struct amdgpu_device *adev,
					     enum amd_ip_block_type type);

int amdgpu_ip_block_add(struct amdgpu_device *adev,
			const struct amdgpu_ip_block_version *ip_block_version);

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
	void (*write_pte)(struct amdgpu_ib *ib, uint64_t pe,
			  uint64_t value, unsigned count,
			  uint32_t incr);
	/* for linear pte/pde updates without addr mapping */
	void (*set_pte_pde)(struct amdgpu_ib *ib,
			    uint64_t pe,
			    uint64_t addr, unsigned count,
			    uint32_t incr, uint64_t flags);
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
			   uint64_t flags); /* access flags */
	/* enable/disable PRT support */
	void (*set_prt)(struct amdgpu_device *adev, bool enable);
	/* set pte flags based per asic */
	uint64_t (*get_vm_pte_flags)(struct amdgpu_device *adev,
				     uint32_t flags);
	/* adjust mc addr in fb for APU case */
	u64 (*adjust_mc_addr)(struct amdgpu_device *adev, u64 addr);
	uint32_t (*get_invalidate_req)(unsigned int vm_id);
};

/* provided by the ih block */
struct amdgpu_ih_funcs {
	/* ring read/write ptr handling, called from interrupt context */
	u32 (*get_wptr)(struct amdgpu_device *adev);
	void (*decode_iv)(struct amdgpu_device *adev,
			  struct amdgpu_iv_entry *entry);
	void (*set_rptr)(struct amdgpu_device *adev);
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
 * BO.
 */
struct amdgpu_bo_list_entry {
	struct amdgpu_bo		*robj;
	struct ttm_validate_buffer	tv;
	struct amdgpu_bo_va		*bo_va;
	uint32_t			priority;
	struct page			**user_pages;
	int				user_invalidated;
};

struct amdgpu_bo_va_mapping {
	struct list_head		list;
	struct rb_node			rb;
	uint64_t			start;
	uint64_t			last;
	uint64_t			__subtree_last;
	uint64_t			offset;
	uint64_t			flags;
};

/* bo virtual addresses in a specific vm */
struct amdgpu_bo_va {
	/* protected by bo being reserved */
	struct list_head		bo_list;
	struct dma_fence	        *last_pt_update;
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
	/* Protected by tbo.reserved */
	u32				prefered_domains;
	u32				allowed_domains;
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
	unsigned			prime_shared_count;
	/* list of all virtual address to which this bo
	 * is associated to
	 */
	struct list_head		va;
	/* Constant after initialization */
	struct drm_gem_object		gem_base;
	struct amdgpu_bo		*parent;
	struct amdgpu_bo		*shadow;

	struct ttm_bo_kmap_obj		dma_buf_vmap;
	struct amdgpu_mn		*mn;
	struct list_head		mn_list;
	struct list_head		shadow_list;
};
#define gem_to_amdgpu_bo(gobj) container_of((gobj), struct amdgpu_bo, gem_base)

void amdgpu_gem_object_free(struct drm_gem_object *obj);
int amdgpu_gem_object_open(struct drm_gem_object *obj,
				struct drm_file *file_priv);
void amdgpu_gem_object_close(struct drm_gem_object *obj,
				struct drm_file *file_priv);
unsigned long amdgpu_gem_timeout(uint64_t timeout_ns);
struct sg_table *amdgpu_gem_prime_get_sg_table(struct drm_gem_object *obj);
struct drm_gem_object *
amdgpu_gem_prime_import_sg_table(struct drm_device *dev,
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

#define AMDGPU_SA_NUM_FENCE_LISTS	32

struct amdgpu_sa_manager {
	wait_queue_head_t	wq;
	struct amdgpu_bo	*bo;
	struct list_head	*hole;
	struct list_head	flist[AMDGPU_SA_NUM_FENCE_LISTS];
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
	struct dma_fence	        *fence;
};

/*
 * GEM objects.
 */
void amdgpu_gem_force_release(struct amdgpu_device *adev);
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
int amdgpu_fence_slab_init(void);
void amdgpu_fence_slab_fini(void);

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
#ifdef CONFIG_DRM_AMDGPU_GART_DEBUGFS
	struct page			**pages;
#endif
	bool				ready;

	/* Asic default pte flags */
	uint64_t			gart_pte_flags;

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
		     dma_addr_t *dma_addr, uint64_t flags);
int amdgpu_ttm_recover_gart(struct amdgpu_device *adev);

/*
 * VMHUB structures, functions & helpers
 */
struct amdgpu_vmhub {
	uint32_t	ctx0_ptb_addr_lo32;
	uint32_t	ctx0_ptb_addr_hi32;
	uint32_t	vm_inv_eng0_req;
	uint32_t	vm_inv_eng0_ack;
	uint32_t	vm_context0_cntl;
	uint32_t	vm_l2_pro_fault_status;
	uint32_t	vm_l2_pro_fault_cntl;
};

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
	uint32_t                srbm_soft_reset;
	struct amdgpu_mode_mc_save save;
	bool			prt_warning;
	/* apertures */
	u64					shared_aperture_start;
	u64					shared_aperture_end;
	u64					private_aperture_start;
	u64					private_aperture_end;
	/* protects concurrent invalidation */
	spinlock_t		invalidate_lock;
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

/*
 * 64bit doorbell, offset are in QWORD, occupy 2KB doorbell space
 */
typedef enum _AMDGPU_DOORBELL64_ASSIGNMENT
{
	/*
	 * All compute related doorbells: kiq, hiq, diq, traditional compute queue, user queue, should locate in
	 * a continues range so that programming CP_MEC_DOORBELL_RANGE_LOWER/UPPER can cover this range.
	 *  Compute related doorbells are allocated from 0x00 to 0x8a
	 */


	/* kernel scheduling */
	AMDGPU_DOORBELL64_KIQ                     = 0x00,

	/* HSA interface queue and debug queue */
	AMDGPU_DOORBELL64_HIQ                     = 0x01,
	AMDGPU_DOORBELL64_DIQ                     = 0x02,

	/* Compute engines */
	AMDGPU_DOORBELL64_MEC_RING0               = 0x03,
	AMDGPU_DOORBELL64_MEC_RING1               = 0x04,
	AMDGPU_DOORBELL64_MEC_RING2               = 0x05,
	AMDGPU_DOORBELL64_MEC_RING3               = 0x06,
	AMDGPU_DOORBELL64_MEC_RING4               = 0x07,
	AMDGPU_DOORBELL64_MEC_RING5               = 0x08,
	AMDGPU_DOORBELL64_MEC_RING6               = 0x09,
	AMDGPU_DOORBELL64_MEC_RING7               = 0x0a,

	/* User queue doorbell range (128 doorbells) */
	AMDGPU_DOORBELL64_USERQUEUE_START         = 0x0b,
	AMDGPU_DOORBELL64_USERQUEUE_END           = 0x8a,

	/* Graphics engine */
	AMDGPU_DOORBELL64_GFX_RING0               = 0x8b,

	/*
	 * Other graphics doorbells can be allocated here: from 0x8c to 0xef
	 * Graphics voltage island aperture 1
	 * default non-graphics QWORD index is 0xF0 - 0xFF inclusive
	 */

	/* sDMA engines */
	AMDGPU_DOORBELL64_sDMA_ENGINE0            = 0xF0,
	AMDGPU_DOORBELL64_sDMA_HI_PRI_ENGINE0     = 0xF1,
	AMDGPU_DOORBELL64_sDMA_ENGINE1            = 0xF2,
	AMDGPU_DOORBELL64_sDMA_HI_PRI_ENGINE1     = 0xF3,

	/* Interrupt handler */
	AMDGPU_DOORBELL64_IH                      = 0xF4,  /* For legacy interrupt ring buffer */
	AMDGPU_DOORBELL64_IH_RING1                = 0xF5,  /* For page migration request log */
	AMDGPU_DOORBELL64_IH_RING2                = 0xF6,  /* For page migration translation/invalidation log */

	/* VCN engine use 32 bits doorbell  */
	AMDGPU_DOORBELL64_VCN0_1                  = 0xF8, /* lower 32 bits for VNC0 and upper 32 bits for VNC1 */
	AMDGPU_DOORBELL64_VCN2_3                  = 0xF9,
	AMDGPU_DOORBELL64_VCN4_5                  = 0xFA,
	AMDGPU_DOORBELL64_VCN6_7                  = 0xFB,

	/* overlap the doorbell assignment with VCN as they are  mutually exclusive
	 * VCE engine's doorbell is 32 bit and two VCE ring share one QWORD
	 */
	AMDGPU_DOORBELL64_RING0_1                 = 0xF8,
	AMDGPU_DOORBELL64_RING2_3                 = 0xF9,
	AMDGPU_DOORBELL64_RING4_5                 = 0xFA,
	AMDGPU_DOORBELL64_RING6_7                 = 0xFB,

	AMDGPU_DOORBELL64_UVD_RING0_1             = 0xFC,
	AMDGPU_DOORBELL64_UVD_RING2_3             = 0xFD,
	AMDGPU_DOORBELL64_UVD_RING4_5             = 0xFE,
	AMDGPU_DOORBELL64_UVD_RING6_7             = 0xFF,

	AMDGPU_DOORBELL64_MAX_ASSIGNMENT          = 0xFF,
	AMDGPU_DOORBELL64_INVALID                 = 0xFFFF
} AMDGPU_DOORBELL64_ASSIGNMENT;


void amdgpu_doorbell_get_kfd_info(struct amdgpu_device *adev,
				phys_addr_t *aperture_base,
				size_t *aperture_size,
				size_t *start_offset);

/*
 * IRQS.
 */

struct amdgpu_flip_work {
	struct delayed_work		flip_work;
	struct work_struct		unpin_work;
	struct amdgpu_device		*adev;
	int				crtc_id;
	u32				target_vblank;
	uint64_t			base;
	struct drm_pending_vblank_event *event;
	struct amdgpu_bo		*old_abo;
	struct dma_fence		*excl;
	unsigned			shared_count;
	struct dma_fence		**shared;
	struct dma_fence_cb		cb;
	bool				async;
};


/*
 * CP & rings.
 */

struct amdgpu_ib {
	struct amdgpu_sa_bo		*sa_bo;
	uint32_t			length_dw;
	uint64_t			gpu_addr;
	uint32_t			*ptr;
	uint32_t			flags;
};

extern const struct amd_sched_backend_ops amdgpu_sched_ops;

int amdgpu_job_alloc(struct amdgpu_device *adev, unsigned num_ibs,
		     struct amdgpu_job **job, struct amdgpu_vm *vm);
int amdgpu_job_alloc_with_ib(struct amdgpu_device *adev, unsigned size,
			     struct amdgpu_job **job);

void amdgpu_job_free_resources(struct amdgpu_job *job);
void amdgpu_job_free(struct amdgpu_job *job);
int amdgpu_job_submit(struct amdgpu_job *job, struct amdgpu_ring *ring,
		      struct amd_sched_entity *entity, void *owner,
		      struct dma_fence **f);

/*
 * context related structures
 */

struct amdgpu_ctx_ring {
	uint64_t		sequence;
	struct dma_fence	**fences;
	struct amd_sched_entity	entity;
};

struct amdgpu_ctx {
	struct kref		refcount;
	struct amdgpu_device    *adev;
	unsigned		reset_counter;
	spinlock_t		ring_lock;
	struct dma_fence	**fences;
	struct amdgpu_ctx_ring	rings[AMDGPU_MAX_RINGS];
	bool preamble_presented;
};

struct amdgpu_ctx_mgr {
	struct amdgpu_device	*adev;
	struct mutex		lock;
	/* protected by lock */
	struct idr		ctx_handles;
};

struct amdgpu_ctx *amdgpu_ctx_get(struct amdgpu_fpriv *fpriv, uint32_t id);
int amdgpu_ctx_put(struct amdgpu_ctx *ctx);

uint64_t amdgpu_ctx_add_fence(struct amdgpu_ctx *ctx, struct amdgpu_ring *ring,
			      struct dma_fence *fence);
struct dma_fence *amdgpu_ctx_get_fence(struct amdgpu_ctx *ctx,
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
	struct amdgpu_bo_va	*prt_va;
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
	unsigned first_userptr;
	unsigned num_entries;
	struct amdgpu_bo_list_entry *array;
};

struct amdgpu_bo_list *
amdgpu_bo_list_get(struct amdgpu_fpriv *fpriv, int id);
void amdgpu_bo_list_get_list(struct amdgpu_bo_list *list,
			     struct list_head *validated);
void amdgpu_bo_list_put(struct amdgpu_bo_list *list);
void amdgpu_bo_list_free(struct amdgpu_bo_list *list);

/*
 * GFX stuff
 */
#include "clearstate_defs.h"

struct amdgpu_rlc_funcs {
	void (*enter_safe_mode)(struct amdgpu_device *adev);
	void (*exit_safe_mode)(struct amdgpu_device *adev);
};

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

	/* safe mode for updating CG/PG state */
	bool in_safe_mode;
	const struct amdgpu_rlc_funcs *funcs;

	/* for firmware data */
	u32 save_and_restore_offset;
	u32 clear_state_descriptor_offset;
	u32 avail_scratch_ram_locations;
	u32 reg_restore_list_size;
	u32 reg_list_format_start;
	u32 reg_list_format_separate_start;
	u32 starting_offsets_start;
	u32 reg_list_format_size_bytes;
	u32 reg_list_size_bytes;

	u32 *register_list_format;
	u32 *register_restore;
};

struct amdgpu_mec {
	struct amdgpu_bo	*hpd_eop_obj;
	u64			hpd_eop_gpu_addr;
	struct amdgpu_bo	*mec_fw_obj;
	u64			mec_fw_gpu_addr;
	u32 num_pipe;
	u32 num_mec;
	u32 num_queue;
	void			*mqd_backup[AMDGPU_MAX_COMPUTE_RINGS + 1];
};

struct amdgpu_kiq {
	u64			eop_gpu_addr;
	struct amdgpu_bo	*eop_obj;
	struct amdgpu_ring	ring;
	struct amdgpu_irq_src	irq;
};

/*
 * GPU scratch registers structures, functions & helpers
 */
struct amdgpu_scratch {
	unsigned		num_reg;
	uint32_t                reg_base;
	uint32_t		free_mask;
};

/*
 * GFX configurations
 */
#define AMDGPU_GFX_MAX_SE 4
#define AMDGPU_GFX_MAX_SH_PER_SE 2

struct amdgpu_rb_config {
	uint32_t rb_backend_disable;
	uint32_t user_rb_backend_disable;
	uint32_t raster_config;
	uint32_t raster_config_1;
};

struct gb_addr_config {
	uint16_t pipe_interleave_size;
	uint8_t num_pipes;
	uint8_t max_compress_frags;
	uint8_t num_banks;
	uint8_t num_se;
	uint8_t num_rb_per_se;
};

struct amdgpu_gfx_config {
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
	unsigned num_rbs;
	unsigned gs_vgt_table_depth;
	unsigned gs_prim_buffer_depth;

	uint32_t tile_mode_array[32];
	uint32_t macrotile_mode_array[16];

	struct gb_addr_config gb_addr_config_fields;
	struct amdgpu_rb_config rb_config[AMDGPU_GFX_MAX_SE][AMDGPU_GFX_MAX_SH_PER_SE];

	/* gfx configure feature */
	uint32_t double_offchip_lds_buf;
};

struct amdgpu_cu_info {
	uint32_t number; /* total active CU number */
	uint32_t ao_cu_mask;
	uint32_t wave_front_size;
	uint32_t bitmap[4][4];
};

struct amdgpu_gfx_funcs {
	/* get the gpu clock counter */
	uint64_t (*get_gpu_clock_counter)(struct amdgpu_device *adev);
	void (*select_se_sh)(struct amdgpu_device *adev, u32 se_num, u32 sh_num, u32 instance);
	void (*read_wave_data)(struct amdgpu_device *adev, uint32_t simd, uint32_t wave, uint32_t *dst, int *no_fields);
	void (*read_wave_vgprs)(struct amdgpu_device *adev, uint32_t simd, uint32_t wave, uint32_t thread, uint32_t start, uint32_t size, uint32_t *dst);
	void (*read_wave_sgprs)(struct amdgpu_device *adev, uint32_t simd, uint32_t wave, uint32_t start, uint32_t size, uint32_t *dst);
};

struct amdgpu_ngg_buf {
	struct amdgpu_bo	*bo;
	uint64_t		gpu_addr;
	uint32_t		size;
	uint32_t		bo_size;
};

enum {
	NGG_PRIM = 0,
	NGG_POS,
	NGG_CNTL,
	NGG_PARAM,
	NGG_BUF_MAX
};

struct amdgpu_ngg {
	struct amdgpu_ngg_buf	buf[NGG_BUF_MAX];
	uint32_t		gds_reserve_addr;
	uint32_t		gds_reserve_size;
	bool			init;
};

struct amdgpu_gfx {
	struct mutex			gpu_clock_mutex;
	struct amdgpu_gfx_config	config;
	struct amdgpu_rlc		rlc;
	struct amdgpu_mec		mec;
	struct amdgpu_kiq		kiq;
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
	uint32_t			gfx_current_status;
	/* ce ram size*/
	unsigned			ce_ram_size;
	struct amdgpu_cu_info		cu_info;
	const struct amdgpu_gfx_funcs	*funcs;

	/* reset mask */
	uint32_t                        grbm_soft_reset;
	uint32_t                        srbm_soft_reset;
	bool                            in_reset;
	/* NGG */
	struct amdgpu_ngg		ngg;
};

int amdgpu_ib_get(struct amdgpu_device *adev, struct amdgpu_vm *vm,
		  unsigned size, struct amdgpu_ib *ib);
void amdgpu_ib_free(struct amdgpu_device *adev, struct amdgpu_ib *ib,
		    struct dma_fence *f);
int amdgpu_ib_schedule(struct amdgpu_ring *ring, unsigned num_ibs,
		       struct amdgpu_ib *ibs, struct amdgpu_job *job,
		       struct dma_fence **f);
int amdgpu_ib_pool_init(struct amdgpu_device *adev);
void amdgpu_ib_pool_fini(struct amdgpu_device *adev);
int amdgpu_ib_ring_tests(struct amdgpu_device *adev);

/*
 * CS.
 */
struct amdgpu_cs_chunk {
	uint32_t		chunk_id;
	uint32_t		length_dw;
	void			*kdata;
};

struct amdgpu_cs_parser {
	struct amdgpu_device	*adev;
	struct drm_file		*filp;
	struct amdgpu_ctx	*ctx;

	/* chunks */
	unsigned		nchunks;
	struct amdgpu_cs_chunk	*chunks;

	/* scheduler job object */
	struct amdgpu_job	*job;

	/* buffer objects */
	struct ww_acquire_ctx		ticket;
	struct amdgpu_bo_list		*bo_list;
	struct amdgpu_bo_list_entry	vm_pd;
	struct list_head		validated;
	struct dma_fence		*fence;
	uint64_t			bytes_moved_threshold;
	uint64_t			bytes_moved;
	struct amdgpu_bo_list_entry	*evictable;

	/* user fence */
	struct amdgpu_bo_list_entry	uf_entry;
};

#define AMDGPU_PREAMBLE_IB_PRESENT          (1 << 0) /* bit set means command submit involves a preamble IB */
#define AMDGPU_PREAMBLE_IB_PRESENT_FIRST    (1 << 1) /* bit set means preamble IB is first presented in belonging context */
#define AMDGPU_HAVE_CTX_SWITCH              (1 << 2) /* bit set means context switch occured */
#define AMDGPU_VM_DOMAIN                    (1 << 3) /* bit set means in virtual memory context */

struct amdgpu_job {
	struct amd_sched_job    base;
	struct amdgpu_device	*adev;
	struct amdgpu_vm	*vm;
	struct amdgpu_ring	*ring;
	struct amdgpu_sync	sync;
	struct amdgpu_ib	*ibs;
	struct dma_fence	*fence; /* the hw fence */
	uint32_t		preamble_status;
	uint32_t		num_ibs;
	void			*owner;
	uint64_t		fence_ctx; /* the fence_context this job uses */
	bool                    vm_needs_flush;
	bool			need_pipeline_sync;
	unsigned		vm_id;
	uint64_t		vm_pd_addr;
	uint32_t		gds_base, gds_size;
	uint32_t		gws_base, gws_size;
	uint32_t		oa_base, oa_size;

	/* user fence handling */
	uint64_t		uf_addr;
	uint64_t		uf_sequence;

};
#define to_amdgpu_job(sched_job)		\
		container_of((sched_job), struct amdgpu_job, base)

static inline u32 amdgpu_get_ib_value(struct amdgpu_cs_parser *p,
				      uint32_t ib_idx, int idx)
{
	return p->job->ibs[ib_idx].ptr[idx];
}

static inline void amdgpu_set_ib_value(struct amdgpu_cs_parser *p,
				       uint32_t ib_idx, int idx,
				       uint32_t value)
{
	p->job->ibs[ib_idx].ptr[idx] = value;
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
int amdgpu_wb_get_64bit(struct amdgpu_device *adev, u32 *wb);
void amdgpu_wb_free_64bit(struct amdgpu_device *adev, u32 wb);

void amdgpu_get_pcie_info(struct amdgpu_device *adev);

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
#ifdef CONFIG_DRM_AMDGPU_SI
	//SI DMA has a difference trap irq number for the second engine
	struct amdgpu_irq_src	trap_irq_1;
#endif
	struct amdgpu_irq_src	trap_irq;
	struct amdgpu_irq_src	illegal_inst_irq;
	int			num_instances;
	uint32_t                    srbm_soft_reset;
};

/*
 * Firmware
 */
enum amdgpu_firmware_load_type {
	AMDGPU_FW_LOAD_DIRECT = 0,
	AMDGPU_FW_LOAD_SMU,
	AMDGPU_FW_LOAD_PSP,
};

struct amdgpu_firmware {
	struct amdgpu_firmware_info ucode[AMDGPU_UCODE_ID_MAXIMUM];
	enum amdgpu_firmware_load_type load_type;
	struct amdgpu_bo *fw_buf;
	unsigned int fw_size;
	unsigned int max_ucodes;
	/* firmwares are loaded by psp instead of smu from vega10 */
	const struct amdgpu_psp_funcs *funcs;
	struct amdgpu_bo *rbuf;
	struct mutex mutex;
};

/*
 * Benchmarking
 */
void amdgpu_benchmark(struct amdgpu_device *adev, int test_number);


/*
 * Testing
 */
void amdgpu_test_moves(struct amdgpu_device *adev);

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
	const struct drm_info_list	*files;
	unsigned		num_files;
};

int amdgpu_debugfs_add_files(struct amdgpu_device *adev,
			     const struct drm_info_list *files,
			     unsigned nfiles);
int amdgpu_debugfs_fence_init(struct amdgpu_device *adev);

#if defined(CONFIG_DEBUG_FS)
int amdgpu_debugfs_init(struct drm_minor *minor);
#endif

int amdgpu_debugfs_firmware_init(struct amdgpu_device *adev);

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

/*
 * ASIC specific functions.
 */
struct amdgpu_asic_funcs {
	bool (*read_disabled_bios)(struct amdgpu_device *adev);
	bool (*read_bios_from_rom)(struct amdgpu_device *adev,
				   u8 *bios, u32 length_bytes);
	int (*read_register)(struct amdgpu_device *adev, u32 se_num,
			     u32 sh_num, u32 reg_offset, u32 *value);
	void (*set_vga_state)(struct amdgpu_device *adev, bool state);
	int (*reset)(struct amdgpu_device *adev);
	/* get the reference clock */
	u32 (*get_xclk)(struct amdgpu_device *adev);
	/* MM block clocks */
	int (*set_uvd_clocks)(struct amdgpu_device *adev, u32 vclk, u32 dclk);
	int (*set_vce_clocks)(struct amdgpu_device *adev, u32 evclk, u32 ecclk);
	/* static power management */
	int (*get_pcie_lanes)(struct amdgpu_device *adev);
	void (*set_pcie_lanes)(struct amdgpu_device *adev, int lanes);
	/* get config memsize register */
	u32 (*get_config_memsize)(struct amdgpu_device *adev);
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
int amdgpu_cs_wait_fences_ioctl(struct drm_device *dev, void *data,
				struct drm_file *filp);

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
struct cgs_device *amdgpu_cgs_create_device(struct amdgpu_device *adev);
void amdgpu_cgs_destroy_device(struct cgs_device *cgs_device);

/*
 * Core structure, functions and helpers.
 */
typedef uint32_t (*amdgpu_rreg_t)(struct amdgpu_device*, uint32_t);
typedef void (*amdgpu_wreg_t)(struct amdgpu_device*, uint32_t, uint32_t);

typedef uint32_t (*amdgpu_block_rreg_t)(struct amdgpu_device*, uint32_t, uint32_t);
typedef void (*amdgpu_block_wreg_t)(struct amdgpu_device*, uint32_t, uint32_t, uint32_t);

struct amdgpu_device {
	struct device			*dev;
	struct drm_device		*ddev;
	struct pci_dev			*pdev;

#ifdef CONFIG_DRM_AMD_ACP
	struct amdgpu_acp		acp;
#endif

	/* ASIC */
	enum amd_asic_type		asic_type;
	uint32_t			family;
	uint32_t			rev_id;
	uint32_t			external_rev_id;
	unsigned long			flags;
	int				usec_timeout;
	const struct amdgpu_asic_funcs	*asic_funcs;
	bool				shutdown;
	bool				need_dma32;
	bool				accel_working;
	struct work_struct		reset_work;
	struct notifier_block		acpi_nb;
	struct amdgpu_i2c_chan		*i2c_bus[AMDGPU_MAX_I2C_BUS];
	struct amdgpu_debugfs		debugfs[AMDGPU_DEBUGFS_MAX_COMPONENTS];
	unsigned			debugfs_count;
#if defined(CONFIG_DEBUG_FS)
	struct dentry			*debugfs_regs[AMDGPU_DEBUGFS_MAX_COMPONENTS];
#endif
	struct amdgpu_atif		atif;
	struct amdgpu_atcs		atcs;
	struct mutex			srbm_mutex;
	/* GRBM index mutex. Protects concurrent access to GRBM index */
	struct mutex                    grbm_idx_mutex;
	struct dev_pm_domain		vga_pm_domain;
	bool				have_disp_power_ref;

	/* BIOS */
	bool				is_atom_fw;
	uint8_t				*bios;
	uint32_t			bios_size;
	struct amdgpu_bo		*stollen_vga_memory;
	uint32_t			bios_scratch_reg_offset;
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
	amdgpu_rreg_t			pciep_rreg;
	amdgpu_wreg_t			pciep_wreg;
	/* protects concurrent UVD register access */
	spinlock_t uvd_ctx_idx_lock;
	amdgpu_rreg_t			uvd_ctx_rreg;
	amdgpu_wreg_t			uvd_ctx_wreg;
	/* protects concurrent DIDT register access */
	spinlock_t didt_idx_lock;
	amdgpu_rreg_t			didt_rreg;
	amdgpu_wreg_t			didt_wreg;
	/* protects concurrent gc_cac register access */
	spinlock_t gc_cac_idx_lock;
	amdgpu_rreg_t			gc_cac_rreg;
	amdgpu_wreg_t			gc_cac_wreg;
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
	struct amdgpu_vmhub             vmhub[AMDGPU_MAX_VMHUBS];

	/* memory management */
	struct amdgpu_mman		mman;
	struct amdgpu_vram_scratch	vram_scratch;
	struct amdgpu_wb		wb;
	atomic64_t			vram_usage;
	atomic64_t			vram_vis_usage;
	atomic64_t			gtt_usage;
	atomic64_t			num_bytes_moved;
	atomic64_t			num_evictions;
	atomic_t			gpu_reset_counter;

	/* data for buffer migration throttling */
	struct {
		spinlock_t		lock;
		s64			last_update_us;
		s64			accum_us; /* accumulated microseconds */
		u32			log2_max_MBps;
	} mm_stats;

	/* display */
	bool				enable_virtual_display;
	struct amdgpu_mode_info		mode_info;
	struct work_struct		hotplug_work;
	struct amdgpu_irq_src		crtc_irq;
	struct amdgpu_irq_src		pageflip_irq;
	struct amdgpu_irq_src		hpd_irq;

	/* rings */
	u64				fence_context;
	unsigned			num_rings;
	struct amdgpu_ring		*rings[AMDGPU_MAX_RINGS];
	bool				ib_pool_ready;
	struct amdgpu_sa_manager	ring_tmp_bo;

	/* interrupts */
	struct amdgpu_irq		irq;

	/* powerplay */
	struct amd_powerplay		powerplay;
	bool				pp_enabled;
	bool				pp_force_state_enabled;

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
	struct amdgpu_uvd		uvd;

	/* vce */
	struct amdgpu_vce		vce;

	/* firmwares */
	struct amdgpu_firmware		firmware;

	/* PSP */
	struct psp_context		psp;

	/* GDS */
	struct amdgpu_gds		gds;

	struct amdgpu_ip_block          ip_blocks[AMDGPU_MAX_IP_NUM];
	int				num_ip_blocks;
	struct mutex	mn_lock;
	DECLARE_HASHTABLE(mn_hash, 7);

	/* tracking pinned memory */
	u64 vram_pin_size;
	u64 invisible_pin_size;
	u64 gart_pin_size;

	/* amdkfd interface */
	struct kfd_dev          *kfd;

	struct amdgpu_virt	virt;

	/* link all shadow bo */
	struct list_head                shadow_list;
	struct mutex                    shadow_list_lock;
	/* link all gtt */
	spinlock_t			gtt_list_lock;
	struct list_head                gtt_list;

	/* record hw reset is performed */
	bool has_hw_reset;

};

static inline struct amdgpu_device *amdgpu_ttm_adev(struct ttm_bo_device *bdev)
{
	return container_of(bdev, struct amdgpu_device, mman.bdev);
}

bool amdgpu_device_is_px(struct drm_device *dev);
int amdgpu_device_init(struct amdgpu_device *adev,
		       struct drm_device *ddev,
		       struct pci_dev *pdev,
		       uint32_t flags);
void amdgpu_device_fini(struct amdgpu_device *adev);
int amdgpu_gpu_wait_for_idle(struct amdgpu_device *adev);

uint32_t amdgpu_mm_rreg(struct amdgpu_device *adev, uint32_t reg,
			uint32_t acc_flags);
void amdgpu_mm_wreg(struct amdgpu_device *adev, uint32_t reg, uint32_t v,
		    uint32_t acc_flags);
u32 amdgpu_io_rreg(struct amdgpu_device *adev, u32 reg);
void amdgpu_io_wreg(struct amdgpu_device *adev, u32 reg, u32 v);

u32 amdgpu_mm_rdoorbell(struct amdgpu_device *adev, u32 index);
void amdgpu_mm_wdoorbell(struct amdgpu_device *adev, u32 index, u32 v);
u64 amdgpu_mm_rdoorbell64(struct amdgpu_device *adev, u32 index);
void amdgpu_mm_wdoorbell64(struct amdgpu_device *adev, u32 index, u64 v);

/*
 * Registers read & write functions.
 */

#define AMDGPU_REGS_IDX       (1<<0)
#define AMDGPU_REGS_NO_KIQ    (1<<1)

#define RREG32_NO_KIQ(reg) amdgpu_mm_rreg(adev, (reg), AMDGPU_REGS_NO_KIQ)
#define WREG32_NO_KIQ(reg, v) amdgpu_mm_wreg(adev, (reg), (v), AMDGPU_REGS_NO_KIQ)

#define RREG32(reg) amdgpu_mm_rreg(adev, (reg), 0)
#define RREG32_IDX(reg) amdgpu_mm_rreg(adev, (reg), AMDGPU_REGS_IDX)
#define DREG32(reg) printk(KERN_INFO "REGISTER: " #reg " : 0x%08X\n", amdgpu_mm_rreg(adev, (reg), 0))
#define WREG32(reg, v) amdgpu_mm_wreg(adev, (reg), (v), 0)
#define WREG32_IDX(reg, v) amdgpu_mm_wreg(adev, (reg), (v), AMDGPU_REGS_IDX)
#define REG_SET(FIELD, v) (((v) << FIELD##_SHIFT) & FIELD##_MASK)
#define REG_GET(FIELD, v) (((v) << FIELD##_SHIFT) & FIELD##_MASK)
#define RREG32_PCIE(reg) adev->pcie_rreg(adev, (reg))
#define WREG32_PCIE(reg, v) adev->pcie_wreg(adev, (reg), (v))
#define RREG32_PCIE_PORT(reg) adev->pciep_rreg(adev, (reg))
#define WREG32_PCIE_PORT(reg, v) adev->pciep_wreg(adev, (reg), (v))
#define RREG32_SMC(reg) adev->smc_rreg(adev, (reg))
#define WREG32_SMC(reg, v) adev->smc_wreg(adev, (reg), (v))
#define RREG32_UVD_CTX(reg) adev->uvd_ctx_rreg(adev, (reg))
#define WREG32_UVD_CTX(reg, v) adev->uvd_ctx_wreg(adev, (reg), (v))
#define RREG32_DIDT(reg) adev->didt_rreg(adev, (reg))
#define WREG32_DIDT(reg, v) adev->didt_wreg(adev, (reg), (v))
#define RREG32_GC_CAC(reg) adev->gc_cac_rreg(adev, (reg))
#define WREG32_GC_CAC(reg, v) adev->gc_cac_wreg(adev, (reg), (v))
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
#define RDOORBELL64(index) amdgpu_mm_rdoorbell64(adev, (index))
#define WDOORBELL64(index, v) amdgpu_mm_wdoorbell64(adev, (index), (v))

#define REG_FIELD_SHIFT(reg, field) reg##__##field##__SHIFT
#define REG_FIELD_MASK(reg, field) reg##__##field##_MASK

#define REG_SET_FIELD(orig_val, reg, field, field_val)			\
	(((orig_val) & ~REG_FIELD_MASK(reg, field)) |			\
	 (REG_FIELD_MASK(reg, field) & ((field_val) << REG_FIELD_SHIFT(reg, field))))

#define REG_GET_FIELD(value, reg, field)				\
	(((value) & REG_FIELD_MASK(reg, field)) >> REG_FIELD_SHIFT(reg, field))

#define WREG32_FIELD(reg, field, val)	\
	WREG32(mm##reg, (RREG32(mm##reg) & ~REG_FIELD_MASK(reg, field)) | (val) << REG_FIELD_SHIFT(reg, field))

#define WREG32_FIELD_OFFSET(reg, offset, field, val)	\
	WREG32(mm##reg + offset, (RREG32(mm##reg + offset) & ~REG_FIELD_MASK(reg, field)) | (val) << REG_FIELD_SHIFT(reg, field))

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
	ring->ring[ring->wptr++ & ring->buf_mask] = v;
	ring->wptr &= ring->ptr_mask;
	ring->count_dw--;
}

static inline void amdgpu_ring_write_multiple(struct amdgpu_ring *ring, void *src, int count_dw)
{
	unsigned occupied, chunk1, chunk2;
	void *dst;

	if (ring->count_dw < count_dw) {
		DRM_ERROR("amdgpu: writing more dwords to the ring than expected!\n");
	} else {
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
#define amdgpu_asic_get_xclk(adev) (adev)->asic_funcs->get_xclk((adev))
#define amdgpu_asic_set_uvd_clocks(adev, v, d) (adev)->asic_funcs->set_uvd_clocks((adev), (v), (d))
#define amdgpu_asic_set_vce_clocks(adev, ev, ec) (adev)->asic_funcs->set_vce_clocks((adev), (ev), (ec))
#define amdgpu_get_pcie_lanes(adev) (adev)->asic_funcs->get_pcie_lanes((adev))
#define amdgpu_set_pcie_lanes(adev, l) (adev)->asic_funcs->set_pcie_lanes((adev), (l))
#define amdgpu_asic_get_gpu_clock_counter(adev) (adev)->asic_funcs->get_gpu_clock_counter((adev))
#define amdgpu_asic_read_disabled_bios(adev) (adev)->asic_funcs->read_disabled_bios((adev))
#define amdgpu_asic_read_bios_from_rom(adev, b, l) (adev)->asic_funcs->read_bios_from_rom((adev), (b), (l))
#define amdgpu_asic_read_register(adev, se, sh, offset, v)((adev)->asic_funcs->read_register((adev), (se), (sh), (offset), (v)))
#define amdgpu_asic_get_config_memsize(adev) (adev)->asic_funcs->get_config_memsize((adev))
#define amdgpu_gart_flush_gpu_tlb(adev, vmid) (adev)->gart.gart_funcs->flush_gpu_tlb((adev), (vmid))
#define amdgpu_gart_set_pte_pde(adev, pt, idx, addr, flags) (adev)->gart.gart_funcs->set_pte_pde((adev), (pt), (idx), (addr), (flags))
#define amdgpu_vm_copy_pte(adev, ib, pe, src, count) ((adev)->vm_manager.vm_pte_funcs->copy_pte((ib), (pe), (src), (count)))
#define amdgpu_vm_write_pte(adev, ib, pe, value, count, incr) ((adev)->vm_manager.vm_pte_funcs->write_pte((ib), (pe), (value), (count), (incr)))
#define amdgpu_vm_set_pte_pde(adev, ib, pe, addr, count, incr, flags) ((adev)->vm_manager.vm_pte_funcs->set_pte_pde((ib), (pe), (addr), (count), (incr), (flags)))
#define amdgpu_vm_get_pte_flags(adev, flags) (adev)->gart.gart_funcs->get_vm_pte_flags((adev),(flags))
#define amdgpu_ring_parse_cs(r, p, ib) ((r)->funcs->parse_cs((p), (ib)))
#define amdgpu_ring_test_ring(r) (r)->funcs->test_ring((r))
#define amdgpu_ring_test_ib(r, t) (r)->funcs->test_ib((r), (t))
#define amdgpu_ring_get_rptr(r) (r)->funcs->get_rptr((r))
#define amdgpu_ring_get_wptr(r) (r)->funcs->get_wptr((r))
#define amdgpu_ring_set_wptr(r) (r)->funcs->set_wptr((r))
#define amdgpu_ring_emit_ib(r, ib, vm_id, c) (r)->funcs->emit_ib((r), (ib), (vm_id), (c))
#define amdgpu_ring_emit_pipeline_sync(r) (r)->funcs->emit_pipeline_sync((r))
#define amdgpu_ring_emit_vm_flush(r, vmid, addr) (r)->funcs->emit_vm_flush((r), (vmid), (addr))
#define amdgpu_ring_emit_fence(r, addr, seq, flags) (r)->funcs->emit_fence((r), (addr), (seq), (flags))
#define amdgpu_ring_emit_gds_switch(r, v, db, ds, wb, ws, ab, as) (r)->funcs->emit_gds_switch((r), (v), (db), (ds), (wb), (ws), (ab), (as))
#define amdgpu_ring_emit_hdp_flush(r) (r)->funcs->emit_hdp_flush((r))
#define amdgpu_ring_emit_hdp_invalidate(r) (r)->funcs->emit_hdp_invalidate((r))
#define amdgpu_ring_emit_switch_buffer(r) (r)->funcs->emit_switch_buffer((r))
#define amdgpu_ring_emit_cntxcntl(r, d) (r)->funcs->emit_cntxcntl((r), (d))
#define amdgpu_ring_emit_rreg(r, d) (r)->funcs->emit_rreg((r), (d))
#define amdgpu_ring_emit_wreg(r, d, v) (r)->funcs->emit_wreg((r), (d), (v))
#define amdgpu_ring_pad_ib(r, ib) ((r)->funcs->pad_ib((r), (ib)))
#define amdgpu_ring_init_cond_exec(r) (r)->funcs->init_cond_exec((r))
#define amdgpu_ring_patch_cond_exec(r,o) (r)->funcs->patch_cond_exec((r),(o))
#define amdgpu_ih_get_wptr(adev) (adev)->irq.ih_funcs->get_wptr((adev))
#define amdgpu_ih_decode_iv(adev, iv) (adev)->irq.ih_funcs->decode_iv((adev), (iv))
#define amdgpu_ih_set_rptr(adev) (adev)->irq.ih_funcs->set_rptr((adev))
#define amdgpu_display_set_vga_render_state(adev, r) (adev)->mode_info.funcs->set_vga_render_state((adev), (r))
#define amdgpu_display_vblank_get_counter(adev, crtc) (adev)->mode_info.funcs->vblank_get_counter((adev), (crtc))
#define amdgpu_display_vblank_wait(adev, crtc) (adev)->mode_info.funcs->vblank_wait((adev), (crtc))
#define amdgpu_display_backlight_set_level(adev, e, l) (adev)->mode_info.funcs->backlight_set_level((e), (l))
#define amdgpu_display_backlight_get_level(adev, e) (adev)->mode_info.funcs->backlight_get_level((e))
#define amdgpu_display_hpd_sense(adev, h) (adev)->mode_info.funcs->hpd_sense((adev), (h))
#define amdgpu_display_hpd_set_polarity(adev, h) (adev)->mode_info.funcs->hpd_set_polarity((adev), (h))
#define amdgpu_display_hpd_get_gpio_reg(adev) (adev)->mode_info.funcs->hpd_get_gpio_reg((adev))
#define amdgpu_display_bandwidth_update(adev) (adev)->mode_info.funcs->bandwidth_update((adev))
#define amdgpu_display_page_flip(adev, crtc, base, async) (adev)->mode_info.funcs->page_flip((adev), (crtc), (base), (async))
#define amdgpu_display_page_flip_get_scanoutpos(adev, crtc, vbl, pos) (adev)->mode_info.funcs->page_flip_get_scanoutpos((adev), (crtc), (vbl), (pos))
#define amdgpu_display_add_encoder(adev, e, s, c) (adev)->mode_info.funcs->add_encoder((adev), (e), (s), (c))
#define amdgpu_display_add_connector(adev, ci, sd, ct, ib, coi, h, r) (adev)->mode_info.funcs->add_connector((adev), (ci), (sd), (ct), (ib), (coi), (h), (r))
#define amdgpu_display_stop_mc_access(adev, s) (adev)->mode_info.funcs->stop_mc_access((adev), (s))
#define amdgpu_display_resume_mc_access(adev, s) (adev)->mode_info.funcs->resume_mc_access((adev), (s))
#define amdgpu_emit_copy_buffer(adev, ib, s, d, b) (adev)->mman.buffer_funcs->emit_copy_buffer((ib),  (s), (d), (b))
#define amdgpu_emit_fill_buffer(adev, ib, s, d, b) (adev)->mman.buffer_funcs->emit_fill_buffer((ib), (s), (d), (b))
#define amdgpu_gfx_get_gpu_clock_counter(adev) (adev)->gfx.funcs->get_gpu_clock_counter((adev))
#define amdgpu_gfx_select_se_sh(adev, se, sh, instance) (adev)->gfx.funcs->select_se_sh((adev), (se), (sh), (instance))
#define amdgpu_gds_switch(adev, r, v, d, w, a) (adev)->gds.funcs->patch_gds_switch((r), (v), (d), (w), (a))
#define amdgpu_psp_check_fw_loading_status(adev, i) (adev)->firmware.funcs->check_fw_loading_status((adev), (i))

/* Common functions */
int amdgpu_gpu_reset(struct amdgpu_device *adev);
bool amdgpu_need_backup(struct amdgpu_device *adev);
void amdgpu_pci_config_reset(struct amdgpu_device *adev);
bool amdgpu_need_post(struct amdgpu_device *adev);
void amdgpu_update_display_priority(struct amdgpu_device *adev);

int amdgpu_cs_parser_init(struct amdgpu_cs_parser *p, void *data);
int amdgpu_cs_get_ring(struct amdgpu_device *adev, u32 ip_type,
		       u32 ip_instance, u32 ring,
		       struct amdgpu_ring **out_ring);
void amdgpu_cs_report_moved_bytes(struct amdgpu_device *adev, u64 num_bytes);
void amdgpu_ttm_placement_from_domain(struct amdgpu_bo *abo, u32 domain);
bool amdgpu_ttm_bo_is_amdgpu_bo(struct ttm_buffer_object *bo);
int amdgpu_ttm_tt_get_user_pages(struct ttm_tt *ttm, struct page **pages);
int amdgpu_ttm_tt_set_userptr(struct ttm_tt *ttm, uint64_t addr,
				     uint32_t flags);
bool amdgpu_ttm_tt_has_userptr(struct ttm_tt *ttm);
struct mm_struct *amdgpu_ttm_tt_get_usermm(struct ttm_tt *ttm);
bool amdgpu_ttm_tt_affect_userptr(struct ttm_tt *ttm, unsigned long start,
				  unsigned long end);
bool amdgpu_ttm_tt_userptr_invalidated(struct ttm_tt *ttm,
				       int *last_invalidated);
bool amdgpu_ttm_tt_is_readonly(struct ttm_tt *ttm);
uint64_t amdgpu_ttm_tt_pte_flags(struct amdgpu_device *adev, struct ttm_tt *ttm,
				 struct ttm_mem_reg *mem);
void amdgpu_vram_location(struct amdgpu_device *adev, struct amdgpu_mc *mc, u64 base);
void amdgpu_gtt_location(struct amdgpu_device *adev, struct amdgpu_mc *mc);
void amdgpu_ttm_set_active_vram_size(struct amdgpu_device *adev, u64 size);
int amdgpu_ttm_init(struct amdgpu_device *adev);
void amdgpu_ttm_fini(struct amdgpu_device *adev);
void amdgpu_program_register_sequence(struct amdgpu_device *adev,
					     const u32 *registers,
					     const u32 array_size);

bool amdgpu_device_is_px(struct drm_device *dev);
/* atpx handler */
#if defined(CONFIG_VGA_SWITCHEROO)
void amdgpu_register_atpx_handler(void);
void amdgpu_unregister_atpx_handler(void);
bool amdgpu_has_atpx_dgpu_power_cntl(void);
bool amdgpu_is_atpx_hybrid(void);
bool amdgpu_atpx_dgpu_req_power_for_displays(void);
bool amdgpu_has_atpx(void);
#else
static inline void amdgpu_register_atpx_handler(void) {}
static inline void amdgpu_unregister_atpx_handler(void) {}
static inline bool amdgpu_has_atpx_dgpu_power_cntl(void) { return false; }
static inline bool amdgpu_is_atpx_hybrid(void) { return false; }
static inline bool amdgpu_atpx_dgpu_req_power_for_displays(void) { return false; }
static inline bool amdgpu_has_atpx(void) { return false; }
#endif

/*
 * KMS
 */
extern const struct drm_ioctl_desc amdgpu_ioctls_kms[];
extern const int amdgpu_max_kms_ioctl;

int amdgpu_driver_load_kms(struct drm_device *dev, unsigned long flags);
void amdgpu_driver_unload_kms(struct drm_device *dev);
void amdgpu_driver_lastclose_kms(struct drm_device *dev);
int amdgpu_driver_open_kms(struct drm_device *dev, struct drm_file *file_priv);
void amdgpu_driver_postclose_kms(struct drm_device *dev,
				 struct drm_file *file_priv);
int amdgpu_suspend(struct amdgpu_device *adev);
int amdgpu_device_suspend(struct drm_device *dev, bool suspend, bool fbcon);
int amdgpu_device_resume(struct drm_device *dev, bool resume, bool fbcon);
u32 amdgpu_get_vblank_counter_kms(struct drm_device *dev, unsigned int pipe);
int amdgpu_enable_vblank_kms(struct drm_device *dev, unsigned int pipe);
void amdgpu_disable_vblank_kms(struct drm_device *dev, unsigned int pipe);
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
int amdgpu_cs_sysvm_access_required(struct amdgpu_cs_parser *parser);

#include "amdgpu_object.h"
#endif
