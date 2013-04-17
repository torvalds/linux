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
#ifndef __RADEON_H__
#define __RADEON_H__

/* TODO: Here are things that needs to be done :
 *	- surface allocator & initializer : (bit like scratch reg) should
 *	  initialize HDP_ stuff on RS600, R600, R700 hw, well anythings
 *	  related to surface
 *	- WB : write back stuff (do it bit like scratch reg things)
 *	- Vblank : look at Jesse's rework and what we should do
 *	- r600/r700: gart & cp
 *	- cs : clean cs ioctl use bitmap & things like that.
 *	- power management stuff
 *	- Barrier in gart code
 *	- Unmappabled vram ?
 *	- TESTING, TESTING, TESTING
 */

/* Initialization path:
 *  We expect that acceleration initialization might fail for various
 *  reasons even thought we work hard to make it works on most
 *  configurations. In order to still have a working userspace in such
 *  situation the init path must succeed up to the memory controller
 *  initialization point. Failure before this point are considered as
 *  fatal error. Here is the init callchain :
 *      radeon_device_init  perform common structure, mutex initialization
 *      asic_init           setup the GPU memory layout and perform all
 *                          one time initialization (failure in this
 *                          function are considered fatal)
 *      asic_startup        setup the GPU acceleration, in order to
 *                          follow guideline the first thing this
 *                          function should do is setting the GPU
 *                          memory controller (only MC setup failure
 *                          are considered as fatal)
 */

#include <linux/atomic.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/kref.h>

#include <ttm/ttm_bo_api.h>
#include <ttm/ttm_bo_driver.h>
#include <ttm/ttm_placement.h>
#include <ttm/ttm_module.h>
#include <ttm/ttm_execbuf_util.h>

#include "radeon_family.h"
#include "radeon_mode.h"
#include "radeon_reg.h"

/*
 * Modules parameters.
 */
extern int radeon_no_wb;
extern int radeon_modeset;
extern int radeon_dynclks;
extern int radeon_r4xx_atom;
extern int radeon_agpmode;
extern int radeon_vram_limit;
extern int radeon_gart_size;
extern int radeon_benchmarking;
extern int radeon_testing;
extern int radeon_connector_table;
extern int radeon_tv;
extern int radeon_audio;
extern int radeon_disp_priority;
extern int radeon_hw_i2c;
extern int radeon_pcie_gen2;
extern int radeon_msi;
extern int radeon_lockup_timeout;
extern int radeon_fastfb;
extern int radeon_dpm;
extern int radeon_aspm;

/*
 * Copy from radeon_drv.h so we don't have to include both and have conflicting
 * symbol;
 */
#define RADEON_MAX_USEC_TIMEOUT			100000	/* 100 ms */
#define RADEON_FENCE_JIFFIES_TIMEOUT		(HZ / 2)
/* RADEON_IB_POOL_SIZE must be a power of 2 */
#define RADEON_IB_POOL_SIZE			16
#define RADEON_DEBUGFS_MAX_COMPONENTS		32
#define RADEONFB_CONN_LIMIT			4
#define RADEON_BIOS_NUM_SCRATCH			8

/* max number of rings */
#define RADEON_NUM_RINGS			6

/* fence seq are set to this number when signaled */
#define RADEON_FENCE_SIGNALED_SEQ		0LL

/* internal ring indices */
/* r1xx+ has gfx CP ring */
#define RADEON_RING_TYPE_GFX_INDEX	0

/* cayman has 2 compute CP rings */
#define CAYMAN_RING_TYPE_CP1_INDEX	1
#define CAYMAN_RING_TYPE_CP2_INDEX	2

/* R600+ has an async dma ring */
#define R600_RING_TYPE_DMA_INDEX		3
/* cayman add a second async dma ring */
#define CAYMAN_RING_TYPE_DMA1_INDEX		4

/* R600+ */
#define R600_RING_TYPE_UVD_INDEX	5

/* hardcode those limit for now */
#define RADEON_VA_IB_OFFSET			(1 << 20)
#define RADEON_VA_RESERVED_SIZE			(8 << 20)
#define RADEON_IB_VM_MAX_SIZE			(64 << 10)

/* reset flags */
#define RADEON_RESET_GFX			(1 << 0)
#define RADEON_RESET_COMPUTE			(1 << 1)
#define RADEON_RESET_DMA			(1 << 2)
#define RADEON_RESET_CP				(1 << 3)
#define RADEON_RESET_GRBM			(1 << 4)
#define RADEON_RESET_DMA1			(1 << 5)
#define RADEON_RESET_RLC			(1 << 6)
#define RADEON_RESET_SEM			(1 << 7)
#define RADEON_RESET_IH				(1 << 8)
#define RADEON_RESET_VMC			(1 << 9)
#define RADEON_RESET_MC				(1 << 10)
#define RADEON_RESET_DISPLAY			(1 << 11)

/* max cursor sizes (in pixels) */
#define CURSOR_WIDTH 64
#define CURSOR_HEIGHT 64

#define CIK_CURSOR_WIDTH 128
#define CIK_CURSOR_HEIGHT 128

/*
 * Errata workarounds.
 */
enum radeon_pll_errata {
	CHIP_ERRATA_R300_CG             = 0x00000001,
	CHIP_ERRATA_PLL_DUMMYREADS      = 0x00000002,
	CHIP_ERRATA_PLL_DELAY           = 0x00000004
};


struct radeon_device;


/*
 * BIOS.
 */
bool radeon_get_bios(struct radeon_device *rdev);

/*
 * Dummy page
 */
struct radeon_dummy_page {
	struct page	*page;
	dma_addr_t	addr;
};
int radeon_dummy_page_init(struct radeon_device *rdev);
void radeon_dummy_page_fini(struct radeon_device *rdev);


/*
 * Clocks
 */
struct radeon_clock {
	struct radeon_pll p1pll;
	struct radeon_pll p2pll;
	struct radeon_pll dcpll;
	struct radeon_pll spll;
	struct radeon_pll mpll;
	/* 10 Khz units */
	uint32_t default_mclk;
	uint32_t default_sclk;
	uint32_t default_dispclk;
	uint32_t current_dispclk;
	uint32_t dp_extclk;
	uint32_t max_pixel_clock;
};

/*
 * Power management
 */
int radeon_pm_init(struct radeon_device *rdev);
void radeon_pm_fini(struct radeon_device *rdev);
void radeon_pm_compute_clocks(struct radeon_device *rdev);
void radeon_pm_suspend(struct radeon_device *rdev);
void radeon_pm_resume(struct radeon_device *rdev);
void radeon_combios_get_power_modes(struct radeon_device *rdev);
void radeon_atombios_get_power_modes(struct radeon_device *rdev);
int radeon_atom_get_clock_dividers(struct radeon_device *rdev,
				   u8 clock_type,
				   u32 clock,
				   bool strobe_mode,
				   struct atom_clock_dividers *dividers);
int radeon_atom_get_memory_pll_dividers(struct radeon_device *rdev,
					u32 clock,
					bool strobe_mode,
					struct atom_mpll_param *mpll_param);
void radeon_atom_set_voltage(struct radeon_device *rdev, u16 voltage_level, u8 voltage_type);
int radeon_atom_get_voltage_gpio_settings(struct radeon_device *rdev,
					  u16 voltage_level, u8 voltage_type,
					  u32 *gpio_value, u32 *gpio_mask);
void radeon_atom_set_engine_dram_timings(struct radeon_device *rdev,
					 u32 eng_clock, u32 mem_clock);
int radeon_atom_get_voltage_step(struct radeon_device *rdev,
				 u8 voltage_type, u16 *voltage_step);
int radeon_atom_get_max_vddc(struct radeon_device *rdev, u8 voltage_type,
			     u16 voltage_id, u16 *voltage);
int radeon_atom_get_leakage_vddc_based_on_leakage_idx(struct radeon_device *rdev,
						      u16 *voltage,
						      u16 leakage_idx);
int radeon_atom_round_to_true_voltage(struct radeon_device *rdev,
				      u8 voltage_type,
				      u16 nominal_voltage,
				      u16 *true_voltage);
int radeon_atom_get_min_voltage(struct radeon_device *rdev,
				u8 voltage_type, u16 *min_voltage);
int radeon_atom_get_max_voltage(struct radeon_device *rdev,
				u8 voltage_type, u16 *max_voltage);
int radeon_atom_get_voltage_table(struct radeon_device *rdev,
				  u8 voltage_type, u8 voltage_mode,
				  struct atom_voltage_table *voltage_table);
bool radeon_atom_is_voltage_gpio(struct radeon_device *rdev,
				 u8 voltage_type, u8 voltage_mode);
void radeon_atom_update_memory_dll(struct radeon_device *rdev,
				   u32 mem_clock);
void radeon_atom_set_ac_timing(struct radeon_device *rdev,
			       u32 mem_clock);
int radeon_atom_init_mc_reg_table(struct radeon_device *rdev,
				  u8 module_index,
				  struct atom_mc_reg_table *reg_table);
int radeon_atom_get_memory_info(struct radeon_device *rdev,
				u8 module_index, struct atom_memory_info *mem_info);
int radeon_atom_get_mclk_range_table(struct radeon_device *rdev,
				     bool gddr5, u8 module_index,
				     struct atom_memory_clock_range_table *mclk_range_table);
int radeon_atom_get_max_vddc(struct radeon_device *rdev, u8 voltage_type,
			     u16 voltage_id, u16 *voltage);
void rs690_pm_info(struct radeon_device *rdev);
extern void evergreen_tiling_fields(unsigned tiling_flags, unsigned *bankw,
				    unsigned *bankh, unsigned *mtaspect,
				    unsigned *tile_split);

/*
 * Fences.
 */
struct radeon_fence_driver {
	uint32_t			scratch_reg;
	uint64_t			gpu_addr;
	volatile uint32_t		*cpu_addr;
	/* sync_seq is protected by ring emission lock */
	uint64_t			sync_seq[RADEON_NUM_RINGS];
	atomic64_t			last_seq;
	unsigned long			last_activity;
	bool				initialized;
};

struct radeon_fence {
	struct radeon_device		*rdev;
	struct kref			kref;
	/* protected by radeon_fence.lock */
	uint64_t			seq;
	/* RB, DMA, etc. */
	unsigned			ring;
};

int radeon_fence_driver_start_ring(struct radeon_device *rdev, int ring);
int radeon_fence_driver_init(struct radeon_device *rdev);
void radeon_fence_driver_fini(struct radeon_device *rdev);
void radeon_fence_driver_force_completion(struct radeon_device *rdev);
int radeon_fence_emit(struct radeon_device *rdev, struct radeon_fence **fence, int ring);
void radeon_fence_process(struct radeon_device *rdev, int ring);
bool radeon_fence_signaled(struct radeon_fence *fence);
int radeon_fence_wait(struct radeon_fence *fence, bool interruptible);
int radeon_fence_wait_next_locked(struct radeon_device *rdev, int ring);
int radeon_fence_wait_empty_locked(struct radeon_device *rdev, int ring);
int radeon_fence_wait_any(struct radeon_device *rdev,
			  struct radeon_fence **fences,
			  bool intr);
struct radeon_fence *radeon_fence_ref(struct radeon_fence *fence);
void radeon_fence_unref(struct radeon_fence **fence);
unsigned radeon_fence_count_emitted(struct radeon_device *rdev, int ring);
bool radeon_fence_need_sync(struct radeon_fence *fence, int ring);
void radeon_fence_note_sync(struct radeon_fence *fence, int ring);
static inline struct radeon_fence *radeon_fence_later(struct radeon_fence *a,
						      struct radeon_fence *b)
{
	if (!a) {
		return b;
	}

	if (!b) {
		return a;
	}

	BUG_ON(a->ring != b->ring);

	if (a->seq > b->seq) {
		return a;
	} else {
		return b;
	}
}

static inline bool radeon_fence_is_earlier(struct radeon_fence *a,
					   struct radeon_fence *b)
{
	if (!a) {
		return false;
	}

	if (!b) {
		return true;
	}

	BUG_ON(a->ring != b->ring);

	return a->seq < b->seq;
}

/*
 * Tiling registers
 */
struct radeon_surface_reg {
	struct radeon_bo *bo;
};

#define RADEON_GEM_MAX_SURFACES 8

/*
 * TTM.
 */
struct radeon_mman {
	struct ttm_bo_global_ref        bo_global_ref;
	struct drm_global_reference	mem_global_ref;
	struct ttm_bo_device		bdev;
	bool				mem_global_referenced;
	bool				initialized;
};

/* bo virtual address in a specific vm */
struct radeon_bo_va {
	/* protected by bo being reserved */
	struct list_head		bo_list;
	uint64_t			soffset;
	uint64_t			eoffset;
	uint32_t			flags;
	bool				valid;
	unsigned			ref_count;

	/* protected by vm mutex */
	struct list_head		vm_list;

	/* constant after initialization */
	struct radeon_vm		*vm;
	struct radeon_bo		*bo;
};

struct radeon_bo {
	/* Protected by gem.mutex */
	struct list_head		list;
	/* Protected by tbo.reserved */
	u32				placements[3];
	struct ttm_placement		placement;
	struct ttm_buffer_object	tbo;
	struct ttm_bo_kmap_obj		kmap;
	unsigned			pin_count;
	void				*kptr;
	u32				tiling_flags;
	u32				pitch;
	int				surface_reg;
	/* list of all virtual address to which this bo
	 * is associated to
	 */
	struct list_head		va;
	/* Constant after initialization */
	struct radeon_device		*rdev;
	struct drm_gem_object		gem_base;

	struct ttm_bo_kmap_obj		dma_buf_vmap;
	pid_t				pid;
};
#define gem_to_radeon_bo(gobj) container_of((gobj), struct radeon_bo, gem_base)

struct radeon_bo_list {
	struct ttm_validate_buffer tv;
	struct radeon_bo	*bo;
	uint64_t		gpu_offset;
	bool			written;
	unsigned		domain;
	unsigned		alt_domain;
	u32			tiling_flags;
};

int radeon_gem_debugfs_init(struct radeon_device *rdev);

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
struct radeon_sa_manager {
	wait_queue_head_t	wq;
	struct radeon_bo	*bo;
	struct list_head	*hole;
	struct list_head	flist[RADEON_NUM_RINGS];
	struct list_head	olist;
	unsigned		size;
	uint64_t		gpu_addr;
	void			*cpu_ptr;
	uint32_t		domain;
	uint32_t		align;
};

struct radeon_sa_bo;

/* sub-allocation buffer */
struct radeon_sa_bo {
	struct list_head		olist;
	struct list_head		flist;
	struct radeon_sa_manager	*manager;
	unsigned			soffset;
	unsigned			eoffset;
	struct radeon_fence		*fence;
};

/*
 * GEM objects.
 */
struct radeon_gem {
	struct mutex		mutex;
	struct list_head	objects;
};

int radeon_gem_init(struct radeon_device *rdev);
void radeon_gem_fini(struct radeon_device *rdev);
int radeon_gem_object_create(struct radeon_device *rdev, int size,
				int alignment, int initial_domain,
				bool discardable, bool kernel,
				struct drm_gem_object **obj);

int radeon_mode_dumb_create(struct drm_file *file_priv,
			    struct drm_device *dev,
			    struct drm_mode_create_dumb *args);
int radeon_mode_dumb_mmap(struct drm_file *filp,
			  struct drm_device *dev,
			  uint32_t handle, uint64_t *offset_p);
int radeon_mode_dumb_destroy(struct drm_file *file_priv,
			     struct drm_device *dev,
			     uint32_t handle);

/*
 * Semaphores.
 */
/* everything here is constant */
struct radeon_semaphore {
	struct radeon_sa_bo		*sa_bo;
	signed				waiters;
	uint64_t			gpu_addr;
};

int radeon_semaphore_create(struct radeon_device *rdev,
			    struct radeon_semaphore **semaphore);
void radeon_semaphore_emit_signal(struct radeon_device *rdev, int ring,
				  struct radeon_semaphore *semaphore);
void radeon_semaphore_emit_wait(struct radeon_device *rdev, int ring,
				struct radeon_semaphore *semaphore);
int radeon_semaphore_sync_rings(struct radeon_device *rdev,
				struct radeon_semaphore *semaphore,
				int signaler, int waiter);
void radeon_semaphore_free(struct radeon_device *rdev,
			   struct radeon_semaphore **semaphore,
			   struct radeon_fence *fence);

/*
 * GART structures, functions & helpers
 */
struct radeon_mc;

#define RADEON_GPU_PAGE_SIZE 4096
#define RADEON_GPU_PAGE_MASK (RADEON_GPU_PAGE_SIZE - 1)
#define RADEON_GPU_PAGE_SHIFT 12
#define RADEON_GPU_PAGE_ALIGN(a) (((a) + RADEON_GPU_PAGE_MASK) & ~RADEON_GPU_PAGE_MASK)

struct radeon_gart {
	dma_addr_t			table_addr;
	struct radeon_bo		*robj;
	void				*ptr;
	unsigned			num_gpu_pages;
	unsigned			num_cpu_pages;
	unsigned			table_size;
	struct page			**pages;
	dma_addr_t			*pages_addr;
	bool				ready;
};

int radeon_gart_table_ram_alloc(struct radeon_device *rdev);
void radeon_gart_table_ram_free(struct radeon_device *rdev);
int radeon_gart_table_vram_alloc(struct radeon_device *rdev);
void radeon_gart_table_vram_free(struct radeon_device *rdev);
int radeon_gart_table_vram_pin(struct radeon_device *rdev);
void radeon_gart_table_vram_unpin(struct radeon_device *rdev);
int radeon_gart_init(struct radeon_device *rdev);
void radeon_gart_fini(struct radeon_device *rdev);
void radeon_gart_unbind(struct radeon_device *rdev, unsigned offset,
			int pages);
int radeon_gart_bind(struct radeon_device *rdev, unsigned offset,
		     int pages, struct page **pagelist,
		     dma_addr_t *dma_addr);
void radeon_gart_restore(struct radeon_device *rdev);


/*
 * GPU MC structures, functions & helpers
 */
struct radeon_mc {
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
	bool			vram_is_ddr;
	bool			igp_sideport_enabled;
	u64                     gtt_base_align;
	u64                     mc_mask;
};

bool radeon_combios_sideport_present(struct radeon_device *rdev);
bool radeon_atombios_sideport_present(struct radeon_device *rdev);

/*
 * GPU scratch registers structures, functions & helpers
 */
struct radeon_scratch {
	unsigned		num_reg;
	uint32_t                reg_base;
	bool			free[32];
	uint32_t		reg[32];
};

int radeon_scratch_get(struct radeon_device *rdev, uint32_t *reg);
void radeon_scratch_free(struct radeon_device *rdev, uint32_t reg);

/*
 * GPU doorbell structures, functions & helpers
 */
struct radeon_doorbell {
	u32			num_pages;
	bool			free[1024];
	/* doorbell mmio */
	resource_size_t			base;
	resource_size_t			size;
	void __iomem			*ptr;
};

int radeon_doorbell_get(struct radeon_device *rdev, u32 *page);
void radeon_doorbell_free(struct radeon_device *rdev, u32 doorbell);

/*
 * IRQS.
 */

struct radeon_unpin_work {
	struct work_struct work;
	struct radeon_device *rdev;
	int crtc_id;
	struct radeon_fence *fence;
	struct drm_pending_vblank_event *event;
	struct radeon_bo *old_rbo;
	u64 new_crtc_base;
};

struct r500_irq_stat_regs {
	u32 disp_int;
	u32 hdmi0_status;
};

struct r600_irq_stat_regs {
	u32 disp_int;
	u32 disp_int_cont;
	u32 disp_int_cont2;
	u32 d1grph_int;
	u32 d2grph_int;
	u32 hdmi0_status;
	u32 hdmi1_status;
};

struct evergreen_irq_stat_regs {
	u32 disp_int;
	u32 disp_int_cont;
	u32 disp_int_cont2;
	u32 disp_int_cont3;
	u32 disp_int_cont4;
	u32 disp_int_cont5;
	u32 d1grph_int;
	u32 d2grph_int;
	u32 d3grph_int;
	u32 d4grph_int;
	u32 d5grph_int;
	u32 d6grph_int;
	u32 afmt_status1;
	u32 afmt_status2;
	u32 afmt_status3;
	u32 afmt_status4;
	u32 afmt_status5;
	u32 afmt_status6;
};

struct cik_irq_stat_regs {
	u32 disp_int;
	u32 disp_int_cont;
	u32 disp_int_cont2;
	u32 disp_int_cont3;
	u32 disp_int_cont4;
	u32 disp_int_cont5;
	u32 disp_int_cont6;
};

union radeon_irq_stat_regs {
	struct r500_irq_stat_regs r500;
	struct r600_irq_stat_regs r600;
	struct evergreen_irq_stat_regs evergreen;
	struct cik_irq_stat_regs cik;
};

#define RADEON_MAX_HPD_PINS 6
#define RADEON_MAX_CRTCS 6
#define RADEON_MAX_AFMT_BLOCKS 6

struct radeon_irq {
	bool				installed;
	spinlock_t			lock;
	atomic_t			ring_int[RADEON_NUM_RINGS];
	bool				crtc_vblank_int[RADEON_MAX_CRTCS];
	atomic_t			pflip[RADEON_MAX_CRTCS];
	wait_queue_head_t		vblank_queue;
	bool				hpd[RADEON_MAX_HPD_PINS];
	bool				afmt[RADEON_MAX_AFMT_BLOCKS];
	union radeon_irq_stat_regs	stat_regs;
	bool				dpm_thermal;
};

int radeon_irq_kms_init(struct radeon_device *rdev);
void radeon_irq_kms_fini(struct radeon_device *rdev);
void radeon_irq_kms_sw_irq_get(struct radeon_device *rdev, int ring);
void radeon_irq_kms_sw_irq_put(struct radeon_device *rdev, int ring);
void radeon_irq_kms_pflip_irq_get(struct radeon_device *rdev, int crtc);
void radeon_irq_kms_pflip_irq_put(struct radeon_device *rdev, int crtc);
void radeon_irq_kms_enable_afmt(struct radeon_device *rdev, int block);
void radeon_irq_kms_disable_afmt(struct radeon_device *rdev, int block);
void radeon_irq_kms_enable_hpd(struct radeon_device *rdev, unsigned hpd_mask);
void radeon_irq_kms_disable_hpd(struct radeon_device *rdev, unsigned hpd_mask);

/*
 * CP & rings.
 */

struct radeon_ib {
	struct radeon_sa_bo		*sa_bo;
	uint32_t			length_dw;
	uint64_t			gpu_addr;
	uint32_t			*ptr;
	int				ring;
	struct radeon_fence		*fence;
	struct radeon_vm		*vm;
	bool				is_const_ib;
	struct radeon_fence		*sync_to[RADEON_NUM_RINGS];
	struct radeon_semaphore		*semaphore;
};

struct radeon_ring {
	struct radeon_bo	*ring_obj;
	volatile uint32_t	*ring;
	unsigned		rptr;
	unsigned		rptr_offs;
	unsigned		rptr_reg;
	unsigned		rptr_save_reg;
	u64			next_rptr_gpu_addr;
	volatile u32		*next_rptr_cpu_addr;
	unsigned		wptr;
	unsigned		wptr_old;
	unsigned		wptr_reg;
	unsigned		ring_size;
	unsigned		ring_free_dw;
	int			count_dw;
	unsigned long		last_activity;
	unsigned		last_rptr;
	uint64_t		gpu_addr;
	uint32_t		align_mask;
	uint32_t		ptr_mask;
	bool			ready;
	u32			ptr_reg_shift;
	u32			ptr_reg_mask;
	u32			nop;
	u32			idx;
	u64			last_semaphore_signal_addr;
	u64			last_semaphore_wait_addr;
	/* for CIK queues */
	u32 me;
	u32 pipe;
	u32 queue;
	struct radeon_bo	*mqd_obj;
	u32 doorbell_page_num;
	u32 doorbell_offset;
	unsigned		wptr_offs;
};

struct radeon_mec {
	struct radeon_bo	*hpd_eop_obj;
	u64			hpd_eop_gpu_addr;
	u32 num_pipe;
	u32 num_mec;
	u32 num_queue;
};

/*
 * VM
 */

/* maximum number of VMIDs */
#define RADEON_NUM_VM	16

/* defines number of bits in page table versus page directory,
 * a page is 4KB so we have 12 bits offset, 9 bits in the page
 * table and the remaining 19 bits are in the page directory */
#define RADEON_VM_BLOCK_SIZE   9

/* number of entries in page table */
#define RADEON_VM_PTE_COUNT (1 << RADEON_VM_BLOCK_SIZE)

/* PTBs (Page Table Blocks) need to be aligned to 32K */
#define RADEON_VM_PTB_ALIGN_SIZE   32768
#define RADEON_VM_PTB_ALIGN_MASK (RADEON_VM_PTB_ALIGN_SIZE - 1)
#define RADEON_VM_PTB_ALIGN(a) (((a) + RADEON_VM_PTB_ALIGN_MASK) & ~RADEON_VM_PTB_ALIGN_MASK)

struct radeon_vm {
	struct list_head		list;
	struct list_head		va;
	unsigned			id;

	/* contains the page directory */
	struct radeon_sa_bo		*page_directory;
	uint64_t			pd_gpu_addr;

	/* array of page tables, one for each page directory entry */
	struct radeon_sa_bo		**page_tables;

	struct mutex			mutex;
	/* last fence for cs using this vm */
	struct radeon_fence		*fence;
	/* last flush or NULL if we still need to flush */
	struct radeon_fence		*last_flush;
};

struct radeon_vm_manager {
	struct mutex			lock;
	struct list_head		lru_vm;
	struct radeon_fence		*active[RADEON_NUM_VM];
	struct radeon_sa_manager	sa_manager;
	uint32_t			max_pfn;
	/* number of VMIDs */
	unsigned			nvm;
	/* vram base address for page table entry  */
	u64				vram_base_offset;
	/* is vm enabled? */
	bool				enabled;
};

/*
 * file private structure
 */
struct radeon_fpriv {
	struct radeon_vm		vm;
};

/*
 * R6xx+ IH ring
 */
struct r600_ih {
	struct radeon_bo	*ring_obj;
	volatile uint32_t	*ring;
	unsigned		rptr;
	unsigned		ring_size;
	uint64_t		gpu_addr;
	uint32_t		ptr_mask;
	atomic_t		lock;
	bool                    enabled;
};

/*
 * RLC stuff
 */
#include "clearstate_defs.h"

struct radeon_rlc {
	/* for power gating */
	struct radeon_bo	*save_restore_obj;
	uint64_t		save_restore_gpu_addr;
	volatile uint32_t	*sr_ptr;
	const u32               *reg_list;
	u32                     reg_list_size;
	/* for clear state */
	struct radeon_bo	*clear_state_obj;
	uint64_t		clear_state_gpu_addr;
	volatile uint32_t	*cs_ptr;
	const struct cs_section_def   *cs_data;
};

int radeon_ib_get(struct radeon_device *rdev, int ring,
		  struct radeon_ib *ib, struct radeon_vm *vm,
		  unsigned size);
void radeon_ib_free(struct radeon_device *rdev, struct radeon_ib *ib);
void radeon_ib_sync_to(struct radeon_ib *ib, struct radeon_fence *fence);
int radeon_ib_schedule(struct radeon_device *rdev, struct radeon_ib *ib,
		       struct radeon_ib *const_ib);
int radeon_ib_pool_init(struct radeon_device *rdev);
void radeon_ib_pool_fini(struct radeon_device *rdev);
int radeon_ib_ring_tests(struct radeon_device *rdev);
/* Ring access between begin & end cannot sleep */
bool radeon_ring_supports_scratch_reg(struct radeon_device *rdev,
				      struct radeon_ring *ring);
void radeon_ring_free_size(struct radeon_device *rdev, struct radeon_ring *cp);
int radeon_ring_alloc(struct radeon_device *rdev, struct radeon_ring *cp, unsigned ndw);
int radeon_ring_lock(struct radeon_device *rdev, struct radeon_ring *cp, unsigned ndw);
void radeon_ring_commit(struct radeon_device *rdev, struct radeon_ring *cp);
void radeon_ring_unlock_commit(struct radeon_device *rdev, struct radeon_ring *cp);
void radeon_ring_undo(struct radeon_ring *ring);
void radeon_ring_unlock_undo(struct radeon_device *rdev, struct radeon_ring *cp);
int radeon_ring_test(struct radeon_device *rdev, struct radeon_ring *cp);
void radeon_ring_force_activity(struct radeon_device *rdev, struct radeon_ring *ring);
void radeon_ring_lockup_update(struct radeon_ring *ring);
bool radeon_ring_test_lockup(struct radeon_device *rdev, struct radeon_ring *ring);
unsigned radeon_ring_backup(struct radeon_device *rdev, struct radeon_ring *ring,
			    uint32_t **data);
int radeon_ring_restore(struct radeon_device *rdev, struct radeon_ring *ring,
			unsigned size, uint32_t *data);
int radeon_ring_init(struct radeon_device *rdev, struct radeon_ring *cp, unsigned ring_size,
		     unsigned rptr_offs, unsigned rptr_reg, unsigned wptr_reg,
		     u32 ptr_reg_shift, u32 ptr_reg_mask, u32 nop);
void radeon_ring_fini(struct radeon_device *rdev, struct radeon_ring *cp);


/* r600 async dma */
void r600_dma_stop(struct radeon_device *rdev);
int r600_dma_resume(struct radeon_device *rdev);
void r600_dma_fini(struct radeon_device *rdev);

void cayman_dma_stop(struct radeon_device *rdev);
int cayman_dma_resume(struct radeon_device *rdev);
void cayman_dma_fini(struct radeon_device *rdev);

/*
 * CS.
 */
struct radeon_cs_reloc {
	struct drm_gem_object		*gobj;
	struct radeon_bo		*robj;
	struct radeon_bo_list		lobj;
	uint32_t			handle;
	uint32_t			flags;
};

struct radeon_cs_chunk {
	uint32_t		chunk_id;
	uint32_t		length_dw;
	int			kpage_idx[2];
	uint32_t		*kpage[2];
	uint32_t		*kdata;
	void __user		*user_ptr;
	int			last_copied_page;
	int			last_page_index;
};

struct radeon_cs_parser {
	struct device		*dev;
	struct radeon_device	*rdev;
	struct drm_file		*filp;
	/* chunks */
	unsigned		nchunks;
	struct radeon_cs_chunk	*chunks;
	uint64_t		*chunks_array;
	/* IB */
	unsigned		idx;
	/* relocations */
	unsigned		nrelocs;
	struct radeon_cs_reloc	*relocs;
	struct radeon_cs_reloc	**relocs_ptr;
	struct list_head	validated;
	unsigned		dma_reloc_idx;
	/* indices of various chunks */
	int			chunk_ib_idx;
	int			chunk_relocs_idx;
	int			chunk_flags_idx;
	int			chunk_const_ib_idx;
	struct radeon_ib	ib;
	struct radeon_ib	const_ib;
	void			*track;
	unsigned		family;
	int			parser_error;
	u32			cs_flags;
	u32			ring;
	s32			priority;
	struct ww_acquire_ctx	ticket;
};

extern int radeon_cs_finish_pages(struct radeon_cs_parser *p);
extern u32 radeon_get_ib_value(struct radeon_cs_parser *p, int idx);

struct radeon_cs_packet {
	unsigned	idx;
	unsigned	type;
	unsigned	reg;
	unsigned	opcode;
	int		count;
	unsigned	one_reg_wr;
};

typedef int (*radeon_packet0_check_t)(struct radeon_cs_parser *p,
				      struct radeon_cs_packet *pkt,
				      unsigned idx, unsigned reg);
typedef int (*radeon_packet3_check_t)(struct radeon_cs_parser *p,
				      struct radeon_cs_packet *pkt);


/*
 * AGP
 */
int radeon_agp_init(struct radeon_device *rdev);
void radeon_agp_resume(struct radeon_device *rdev);
void radeon_agp_suspend(struct radeon_device *rdev);
void radeon_agp_fini(struct radeon_device *rdev);


/*
 * Writeback
 */
struct radeon_wb {
	struct radeon_bo	*wb_obj;
	volatile uint32_t	*wb;
	uint64_t		gpu_addr;
	bool                    enabled;
	bool                    use_event;
};

#define RADEON_WB_SCRATCH_OFFSET 0
#define RADEON_WB_RING0_NEXT_RPTR 256
#define RADEON_WB_CP_RPTR_OFFSET 1024
#define RADEON_WB_CP1_RPTR_OFFSET 1280
#define RADEON_WB_CP2_RPTR_OFFSET 1536
#define R600_WB_DMA_RPTR_OFFSET   1792
#define R600_WB_IH_WPTR_OFFSET   2048
#define CAYMAN_WB_DMA1_RPTR_OFFSET   2304
#define R600_WB_UVD_RPTR_OFFSET  2560
#define R600_WB_EVENT_OFFSET     3072
#define CIK_WB_CP1_WPTR_OFFSET     3328
#define CIK_WB_CP2_WPTR_OFFSET     3584

/**
 * struct radeon_pm - power management datas
 * @max_bandwidth:      maximum bandwidth the gpu has (MByte/s)
 * @igp_sideport_mclk:  sideport memory clock Mhz (rs690,rs740,rs780,rs880)
 * @igp_system_mclk:    system clock Mhz (rs690,rs740,rs780,rs880)
 * @igp_ht_link_clk:    ht link clock Mhz (rs690,rs740,rs780,rs880)
 * @igp_ht_link_width:  ht link width in bits (rs690,rs740,rs780,rs880)
 * @k8_bandwidth:       k8 bandwidth the gpu has (MByte/s) (IGP)
 * @sideport_bandwidth: sideport bandwidth the gpu has (MByte/s) (IGP)
 * @ht_bandwidth:       ht bandwidth the gpu has (MByte/s) (IGP)
 * @core_bandwidth:     core GPU bandwidth the gpu has (MByte/s) (IGP)
 * @sclk:          	GPU clock Mhz (core bandwidth depends of this clock)
 * @needed_bandwidth:   current bandwidth needs
 *
 * It keeps track of various data needed to take powermanagement decision.
 * Bandwidth need is used to determine minimun clock of the GPU and memory.
 * Equation between gpu/memory clock and available bandwidth is hw dependent
 * (type of memory, bus size, efficiency, ...)
 */

enum radeon_pm_method {
	PM_METHOD_PROFILE,
	PM_METHOD_DYNPM,
	PM_METHOD_DPM,
};

enum radeon_dynpm_state {
	DYNPM_STATE_DISABLED,
	DYNPM_STATE_MINIMUM,
	DYNPM_STATE_PAUSED,
	DYNPM_STATE_ACTIVE,
	DYNPM_STATE_SUSPENDED,
};
enum radeon_dynpm_action {
	DYNPM_ACTION_NONE,
	DYNPM_ACTION_MINIMUM,
	DYNPM_ACTION_DOWNCLOCK,
	DYNPM_ACTION_UPCLOCK,
	DYNPM_ACTION_DEFAULT
};

enum radeon_voltage_type {
	VOLTAGE_NONE = 0,
	VOLTAGE_GPIO,
	VOLTAGE_VDDC,
	VOLTAGE_SW
};

enum radeon_pm_state_type {
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

enum radeon_pm_profile_type {
	PM_PROFILE_DEFAULT,
	PM_PROFILE_AUTO,
	PM_PROFILE_LOW,
	PM_PROFILE_MID,
	PM_PROFILE_HIGH,
};

#define PM_PROFILE_DEFAULT_IDX 0
#define PM_PROFILE_LOW_SH_IDX  1
#define PM_PROFILE_MID_SH_IDX  2
#define PM_PROFILE_HIGH_SH_IDX 3
#define PM_PROFILE_LOW_MH_IDX  4
#define PM_PROFILE_MID_MH_IDX  5
#define PM_PROFILE_HIGH_MH_IDX 6
#define PM_PROFILE_MAX         7

struct radeon_pm_profile {
	int dpms_off_ps_idx;
	int dpms_on_ps_idx;
	int dpms_off_cm_idx;
	int dpms_on_cm_idx;
};

enum radeon_int_thermal_type {
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
};

struct radeon_voltage {
	enum radeon_voltage_type type;
	/* gpio voltage */
	struct radeon_gpio_rec gpio;
	u32 delay; /* delay in usec from voltage drop to sclk change */
	bool active_high; /* voltage drop is active when bit is high */
	/* VDDC voltage */
	u8 vddc_id; /* index into vddc voltage table */
	u8 vddci_id; /* index into vddci voltage table */
	bool vddci_enabled;
	/* r6xx+ sw */
	u16 voltage;
	/* evergreen+ vddci */
	u16 vddci;
};

/* clock mode flags */
#define RADEON_PM_MODE_NO_DISPLAY          (1 << 0)

struct radeon_pm_clock_info {
	/* memory clock */
	u32 mclk;
	/* engine clock */
	u32 sclk;
	/* voltage info */
	struct radeon_voltage voltage;
	/* standardized clock flags */
	u32 flags;
};

/* state flags */
#define RADEON_PM_STATE_SINGLE_DISPLAY_ONLY (1 << 0)

struct radeon_power_state {
	enum radeon_pm_state_type type;
	struct radeon_pm_clock_info *clock_info;
	/* number of valid clock modes in this power state */
	int num_clock_modes;
	struct radeon_pm_clock_info *default_clock_mode;
	/* standardized state flags */
	u32 flags;
	u32 misc; /* vbios specific flags */
	u32 misc2; /* vbios specific flags */
	int pcie_lanes; /* pcie lanes */
};

/*
 * Some modes are overclocked by very low value, accept them
 */
#define RADEON_MODE_OVERCLOCK_MARGIN 500 /* 5 MHz */

enum radeon_dpm_auto_throttle_src {
	RADEON_DPM_AUTO_THROTTLE_SRC_THERMAL,
	RADEON_DPM_AUTO_THROTTLE_SRC_EXTERNAL
};

enum radeon_dpm_event_src {
	RADEON_DPM_EVENT_SRC_ANALOG = 0,
	RADEON_DPM_EVENT_SRC_EXTERNAL = 1,
	RADEON_DPM_EVENT_SRC_DIGITAL = 2,
	RADEON_DPM_EVENT_SRC_ANALOG_OR_EXTERNAL = 3,
	RADEON_DPM_EVENT_SRC_DIGIAL_OR_EXTERNAL = 4
};

struct radeon_ps {
	u32 caps; /* vbios flags */
	u32 class; /* vbios flags */
	u32 class2; /* vbios flags */
	/* UVD clocks */
	u32 vclk;
	u32 dclk;
	/* asic priv */
	void *ps_priv;
};

struct radeon_dpm_thermal {
	/* thermal interrupt work */
	struct work_struct work;
	/* low temperature threshold */
	int                min_temp;
	/* high temperature threshold */
	int                max_temp;
	/* was interrupt low to high or high to low */
	bool               high_to_low;
};

enum radeon_clk_action
{
	RADEON_SCLK_UP = 1,
	RADEON_SCLK_DOWN
};

struct radeon_blacklist_clocks
{
	u32 sclk;
	u32 mclk;
	enum radeon_clk_action action;
};

struct radeon_clock_and_voltage_limits {
	u32 sclk;
	u32 mclk;
	u32 vddc;
	u32 vddci;
};

struct radeon_clock_array {
	u32 count;
	u32 *values;
};

struct radeon_clock_voltage_dependency_entry {
	u32 clk;
	u16 v;
};

struct radeon_clock_voltage_dependency_table {
	u32 count;
	struct radeon_clock_voltage_dependency_entry *entries;
};

struct radeon_cac_leakage_entry {
	u16 vddc;
	u32 leakage;
};

struct radeon_cac_leakage_table {
	u32 count;
	struct radeon_cac_leakage_entry *entries;
};

struct radeon_phase_shedding_limits_entry {
	u16 voltage;
	u32 sclk;
	u32 mclk;
};

struct radeon_phase_shedding_limits_table {
	u32 count;
	struct radeon_phase_shedding_limits_entry *entries;
};

struct radeon_ppm_table {
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

struct radeon_dpm_dynamic_state {
	struct radeon_clock_voltage_dependency_table vddc_dependency_on_sclk;
	struct radeon_clock_voltage_dependency_table vddci_dependency_on_mclk;
	struct radeon_clock_voltage_dependency_table vddc_dependency_on_mclk;
	struct radeon_clock_voltage_dependency_table vddc_dependency_on_dispclk;
	struct radeon_clock_array valid_sclk_values;
	struct radeon_clock_array valid_mclk_values;
	struct radeon_clock_and_voltage_limits max_clock_voltage_on_dc;
	struct radeon_clock_and_voltage_limits max_clock_voltage_on_ac;
	u32 mclk_sclk_ratio;
	u32 sclk_mclk_delta;
	u16 vddc_vddci_delta;
	u16 min_vddc_for_pcie_gen2;
	struct radeon_cac_leakage_table cac_leakage_table;
	struct radeon_phase_shedding_limits_table phase_shedding_limits_table;
	struct radeon_ppm_table *ppm_table;
};

struct radeon_dpm_fan {
	u16 t_min;
	u16 t_med;
	u16 t_high;
	u16 pwm_min;
	u16 pwm_med;
	u16 pwm_high;
	u8 t_hyst;
	u32 cycle_delay;
	u16 t_max;
	bool ucode_fan_control;
};

enum radeon_pcie_gen {
	RADEON_PCIE_GEN1 = 0,
	RADEON_PCIE_GEN2 = 1,
	RADEON_PCIE_GEN3 = 2,
	RADEON_PCIE_GEN_INVALID = 0xffff
};

enum radeon_dpm_forced_level {
	RADEON_DPM_FORCED_LEVEL_AUTO = 0,
	RADEON_DPM_FORCED_LEVEL_LOW = 1,
	RADEON_DPM_FORCED_LEVEL_HIGH = 2,
};

struct radeon_dpm {
	struct radeon_ps        *ps;
	/* number of valid power states */
	int                     num_ps;
	/* current power state that is active */
	struct radeon_ps        *current_ps;
	/* requested power state */
	struct radeon_ps        *requested_ps;
	/* boot up power state */
	struct radeon_ps        *boot_ps;
	/* default uvd power state */
	struct radeon_ps        *uvd_ps;
	enum radeon_pm_state_type state;
	enum radeon_pm_state_type user_state;
	u32                     platform_caps;
	u32                     voltage_response_time;
	u32                     backbias_response_time;
	void                    *priv;
	u32			new_active_crtcs;
	int			new_active_crtc_count;
	u32			current_active_crtcs;
	int			current_active_crtc_count;
	struct radeon_dpm_dynamic_state dyn_state;
	struct radeon_dpm_fan fan;
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
	/* thermal handling */
	struct radeon_dpm_thermal thermal;
	/* forced levels */
	enum radeon_dpm_forced_level forced_level;
	/* track UVD streams */
	unsigned sd;
	unsigned hd;
};

void radeon_dpm_enable_uvd(struct radeon_device *rdev, bool enable);

struct radeon_pm {
	struct mutex		mutex;
	/* write locked while reprogramming mclk */
	struct rw_semaphore	mclk_lock;
	u32			active_crtcs;
	int			active_crtc_count;
	int			req_vblank;
	bool			vblank_sync;
	fixed20_12		max_bandwidth;
	fixed20_12		igp_sideport_mclk;
	fixed20_12		igp_system_mclk;
	fixed20_12		igp_ht_link_clk;
	fixed20_12		igp_ht_link_width;
	fixed20_12		k8_bandwidth;
	fixed20_12		sideport_bandwidth;
	fixed20_12		ht_bandwidth;
	fixed20_12		core_bandwidth;
	fixed20_12		sclk;
	fixed20_12		mclk;
	fixed20_12		needed_bandwidth;
	struct radeon_power_state *power_state;
	/* number of valid power states */
	int                     num_power_states;
	int                     current_power_state_index;
	int                     current_clock_mode_index;
	int                     requested_power_state_index;
	int                     requested_clock_mode_index;
	int                     default_power_state_index;
	u32                     current_sclk;
	u32                     current_mclk;
	u16                     current_vddc;
	u16                     current_vddci;
	u32                     default_sclk;
	u32                     default_mclk;
	u16                     default_vddc;
	u16                     default_vddci;
	struct radeon_i2c_chan *i2c_bus;
	/* selected pm method */
	enum radeon_pm_method     pm_method;
	/* dynpm power management */
	struct delayed_work	dynpm_idle_work;
	enum radeon_dynpm_state	dynpm_state;
	enum radeon_dynpm_action	dynpm_planned_action;
	unsigned long		dynpm_action_timeout;
	bool                    dynpm_can_upclock;
	bool                    dynpm_can_downclock;
	/* profile-based power management */
	enum radeon_pm_profile_type profile;
	int                     profile_index;
	struct radeon_pm_profile profiles[PM_PROFILE_MAX];
	/* internal thermal controller on rv6xx+ */
	enum radeon_int_thermal_type int_thermal_type;
	struct device	        *int_hwmon_dev;
	/* dpm */
	bool                    dpm_enabled;
	struct radeon_dpm       dpm;
};

int radeon_pm_get_type_index(struct radeon_device *rdev,
			     enum radeon_pm_state_type ps_type,
			     int instance);
/*
 * UVD
 */
#define RADEON_MAX_UVD_HANDLES	10
#define RADEON_UVD_STACK_SIZE	(1024*1024)
#define RADEON_UVD_HEAP_SIZE	(1024*1024)

struct radeon_uvd {
	struct radeon_bo	*vcpu_bo;
	void			*cpu_addr;
	uint64_t		gpu_addr;
	void			*saved_bo;
	atomic_t		handles[RADEON_MAX_UVD_HANDLES];
	struct drm_file		*filp[RADEON_MAX_UVD_HANDLES];
	unsigned		img_size[RADEON_MAX_UVD_HANDLES];
	struct delayed_work	idle_work;
};

int radeon_uvd_init(struct radeon_device *rdev);
void radeon_uvd_fini(struct radeon_device *rdev);
int radeon_uvd_suspend(struct radeon_device *rdev);
int radeon_uvd_resume(struct radeon_device *rdev);
int radeon_uvd_get_create_msg(struct radeon_device *rdev, int ring,
			      uint32_t handle, struct radeon_fence **fence);
int radeon_uvd_get_destroy_msg(struct radeon_device *rdev, int ring,
			       uint32_t handle, struct radeon_fence **fence);
void radeon_uvd_force_into_uvd_segment(struct radeon_bo *rbo);
void radeon_uvd_free_handles(struct radeon_device *rdev,
			     struct drm_file *filp);
int radeon_uvd_cs_parse(struct radeon_cs_parser *parser);
void radeon_uvd_note_usage(struct radeon_device *rdev);
int radeon_uvd_calc_upll_dividers(struct radeon_device *rdev,
				  unsigned vclk, unsigned dclk,
				  unsigned vco_min, unsigned vco_max,
				  unsigned fb_factor, unsigned fb_mask,
				  unsigned pd_min, unsigned pd_max,
				  unsigned pd_even,
				  unsigned *optimal_fb_div,
				  unsigned *optimal_vclk_div,
				  unsigned *optimal_dclk_div);
int radeon_uvd_send_upll_ctlreq(struct radeon_device *rdev,
                                unsigned cg_upll_func_cntl);

struct r600_audio {
	int			channels;
	int			rate;
	int			bits_per_sample;
	u8			status_bits;
	u8			category_code;
};

/*
 * Benchmarking
 */
void radeon_benchmark(struct radeon_device *rdev, int test_number);


/*
 * Testing
 */
void radeon_test_moves(struct radeon_device *rdev);
void radeon_test_ring_sync(struct radeon_device *rdev,
			   struct radeon_ring *cpA,
			   struct radeon_ring *cpB);
void radeon_test_syncing(struct radeon_device *rdev);


/*
 * Debugfs
 */
struct radeon_debugfs {
	struct drm_info_list	*files;
	unsigned		num_files;
};

int radeon_debugfs_add_files(struct radeon_device *rdev,
			     struct drm_info_list *files,
			     unsigned nfiles);
int radeon_debugfs_fence_init(struct radeon_device *rdev);


/*
 * ASIC specific functions.
 */
struct radeon_asic {
	int (*init)(struct radeon_device *rdev);
	void (*fini)(struct radeon_device *rdev);
	int (*resume)(struct radeon_device *rdev);
	int (*suspend)(struct radeon_device *rdev);
	void (*vga_set_state)(struct radeon_device *rdev, bool state);
	int (*asic_reset)(struct radeon_device *rdev);
	/* ioctl hw specific callback. Some hw might want to perform special
	 * operation on specific ioctl. For instance on wait idle some hw
	 * might want to perform and HDP flush through MMIO as it seems that
	 * some R6XX/R7XX hw doesn't take HDP flush into account if programmed
	 * through ring.
	 */
	void (*ioctl_wait_idle)(struct radeon_device *rdev, struct radeon_bo *bo);
	/* check if 3D engine is idle */
	bool (*gui_idle)(struct radeon_device *rdev);
	/* wait for mc_idle */
	int (*mc_wait_for_idle)(struct radeon_device *rdev);
	/* get the reference clock */
	u32 (*get_xclk)(struct radeon_device *rdev);
	/* get the gpu clock counter */
	uint64_t (*get_gpu_clock_counter)(struct radeon_device *rdev);
	/* gart */
	struct {
		void (*tlb_flush)(struct radeon_device *rdev);
		int (*set_page)(struct radeon_device *rdev, int i, uint64_t addr);
	} gart;
	struct {
		int (*init)(struct radeon_device *rdev);
		void (*fini)(struct radeon_device *rdev);

		u32 pt_ring_index;
		void (*set_page)(struct radeon_device *rdev,
				 struct radeon_ib *ib,
				 uint64_t pe,
				 uint64_t addr, unsigned count,
				 uint32_t incr, uint32_t flags);
	} vm;
	/* ring specific callbacks */
	struct {
		void (*ib_execute)(struct radeon_device *rdev, struct radeon_ib *ib);
		int (*ib_parse)(struct radeon_device *rdev, struct radeon_ib *ib);
		void (*emit_fence)(struct radeon_device *rdev, struct radeon_fence *fence);
		void (*emit_semaphore)(struct radeon_device *rdev, struct radeon_ring *cp,
				       struct radeon_semaphore *semaphore, bool emit_wait);
		int (*cs_parse)(struct radeon_cs_parser *p);
		void (*ring_start)(struct radeon_device *rdev, struct radeon_ring *cp);
		int (*ring_test)(struct radeon_device *rdev, struct radeon_ring *cp);
		int (*ib_test)(struct radeon_device *rdev, struct radeon_ring *cp);
		bool (*is_lockup)(struct radeon_device *rdev, struct radeon_ring *cp);
		void (*vm_flush)(struct radeon_device *rdev, int ridx, struct radeon_vm *vm);

		u32 (*get_rptr)(struct radeon_device *rdev, struct radeon_ring *ring);
		u32 (*get_wptr)(struct radeon_device *rdev, struct radeon_ring *ring);
		void (*set_wptr)(struct radeon_device *rdev, struct radeon_ring *ring);
	} ring[RADEON_NUM_RINGS];
	/* irqs */
	struct {
		int (*set)(struct radeon_device *rdev);
		int (*process)(struct radeon_device *rdev);
	} irq;
	/* displays */
	struct {
		/* display watermarks */
		void (*bandwidth_update)(struct radeon_device *rdev);
		/* get frame count */
		u32 (*get_vblank_counter)(struct radeon_device *rdev, int crtc);
		/* wait for vblank */
		void (*wait_for_vblank)(struct radeon_device *rdev, int crtc);
		/* set backlight level */
		void (*set_backlight_level)(struct radeon_encoder *radeon_encoder, u8 level);
		/* get backlight level */
		u8 (*get_backlight_level)(struct radeon_encoder *radeon_encoder);
		/* audio callbacks */
		void (*hdmi_enable)(struct drm_encoder *encoder, bool enable);
		void (*hdmi_setmode)(struct drm_encoder *encoder, struct drm_display_mode *mode);
	} display;
	/* copy functions for bo handling */
	struct {
		int (*blit)(struct radeon_device *rdev,
			    uint64_t src_offset,
			    uint64_t dst_offset,
			    unsigned num_gpu_pages,
			    struct radeon_fence **fence);
		u32 blit_ring_index;
		int (*dma)(struct radeon_device *rdev,
			   uint64_t src_offset,
			   uint64_t dst_offset,
			   unsigned num_gpu_pages,
			   struct radeon_fence **fence);
		u32 dma_ring_index;
		/* method used for bo copy */
		int (*copy)(struct radeon_device *rdev,
			    uint64_t src_offset,
			    uint64_t dst_offset,
			    unsigned num_gpu_pages,
			    struct radeon_fence **fence);
		/* ring used for bo copies */
		u32 copy_ring_index;
	} copy;
	/* surfaces */
	struct {
		int (*set_reg)(struct radeon_device *rdev, int reg,
				       uint32_t tiling_flags, uint32_t pitch,
				       uint32_t offset, uint32_t obj_size);
		void (*clear_reg)(struct radeon_device *rdev, int reg);
	} surface;
	/* hotplug detect */
	struct {
		void (*init)(struct radeon_device *rdev);
		void (*fini)(struct radeon_device *rdev);
		bool (*sense)(struct radeon_device *rdev, enum radeon_hpd_id hpd);
		void (*set_polarity)(struct radeon_device *rdev, enum radeon_hpd_id hpd);
	} hpd;
	/* static power management */
	struct {
		void (*misc)(struct radeon_device *rdev);
		void (*prepare)(struct radeon_device *rdev);
		void (*finish)(struct radeon_device *rdev);
		void (*init_profile)(struct radeon_device *rdev);
		void (*get_dynpm_state)(struct radeon_device *rdev);
		uint32_t (*get_engine_clock)(struct radeon_device *rdev);
		void (*set_engine_clock)(struct radeon_device *rdev, uint32_t eng_clock);
		uint32_t (*get_memory_clock)(struct radeon_device *rdev);
		void (*set_memory_clock)(struct radeon_device *rdev, uint32_t mem_clock);
		int (*get_pcie_lanes)(struct radeon_device *rdev);
		void (*set_pcie_lanes)(struct radeon_device *rdev, int lanes);
		void (*set_clock_gating)(struct radeon_device *rdev, int enable);
		int (*set_uvd_clocks)(struct radeon_device *rdev, u32 vclk, u32 dclk);
		int (*get_temperature)(struct radeon_device *rdev);
	} pm;
	/* dynamic power management */
	struct {
		int (*init)(struct radeon_device *rdev);
		void (*setup_asic)(struct radeon_device *rdev);
		int (*enable)(struct radeon_device *rdev);
		void (*disable)(struct radeon_device *rdev);
		int (*pre_set_power_state)(struct radeon_device *rdev);
		int (*set_power_state)(struct radeon_device *rdev);
		void (*post_set_power_state)(struct radeon_device *rdev);
		void (*display_configuration_changed)(struct radeon_device *rdev);
		void (*fini)(struct radeon_device *rdev);
		u32 (*get_sclk)(struct radeon_device *rdev, bool low);
		u32 (*get_mclk)(struct radeon_device *rdev, bool low);
		void (*print_power_state)(struct radeon_device *rdev, struct radeon_ps *ps);
		void (*debugfs_print_current_performance_level)(struct radeon_device *rdev, struct seq_file *m);
		int (*force_performance_level)(struct radeon_device *rdev, enum radeon_dpm_forced_level level);
		bool (*vblank_too_short)(struct radeon_device *rdev);
	} dpm;
	/* pageflipping */
	struct {
		void (*pre_page_flip)(struct radeon_device *rdev, int crtc);
		u32 (*page_flip)(struct radeon_device *rdev, int crtc, u64 crtc_base);
		void (*post_page_flip)(struct radeon_device *rdev, int crtc);
	} pflip;
};

/*
 * Asic structures
 */
struct r100_asic {
	const unsigned		*reg_safe_bm;
	unsigned		reg_safe_bm_size;
	u32			hdp_cntl;
};

struct r300_asic {
	const unsigned		*reg_safe_bm;
	unsigned		reg_safe_bm_size;
	u32			resync_scratch;
	u32			hdp_cntl;
};

struct r600_asic {
	unsigned		max_pipes;
	unsigned		max_tile_pipes;
	unsigned		max_simds;
	unsigned		max_backends;
	unsigned		max_gprs;
	unsigned		max_threads;
	unsigned		max_stack_entries;
	unsigned		max_hw_contexts;
	unsigned		max_gs_threads;
	unsigned		sx_max_export_size;
	unsigned		sx_max_export_pos_size;
	unsigned		sx_max_export_smx_size;
	unsigned		sq_num_cf_insts;
	unsigned		tiling_nbanks;
	unsigned		tiling_npipes;
	unsigned		tiling_group_size;
	unsigned		tile_config;
	unsigned		backend_map;
};

struct rv770_asic {
	unsigned		max_pipes;
	unsigned		max_tile_pipes;
	unsigned		max_simds;
	unsigned		max_backends;
	unsigned		max_gprs;
	unsigned		max_threads;
	unsigned		max_stack_entries;
	unsigned		max_hw_contexts;
	unsigned		max_gs_threads;
	unsigned		sx_max_export_size;
	unsigned		sx_max_export_pos_size;
	unsigned		sx_max_export_smx_size;
	unsigned		sq_num_cf_insts;
	unsigned		sx_num_of_sets;
	unsigned		sc_prim_fifo_size;
	unsigned		sc_hiz_tile_fifo_size;
	unsigned		sc_earlyz_tile_fifo_fize;
	unsigned		tiling_nbanks;
	unsigned		tiling_npipes;
	unsigned		tiling_group_size;
	unsigned		tile_config;
	unsigned		backend_map;
};

struct evergreen_asic {
	unsigned num_ses;
	unsigned max_pipes;
	unsigned max_tile_pipes;
	unsigned max_simds;
	unsigned max_backends;
	unsigned max_gprs;
	unsigned max_threads;
	unsigned max_stack_entries;
	unsigned max_hw_contexts;
	unsigned max_gs_threads;
	unsigned sx_max_export_size;
	unsigned sx_max_export_pos_size;
	unsigned sx_max_export_smx_size;
	unsigned sq_num_cf_insts;
	unsigned sx_num_of_sets;
	unsigned sc_prim_fifo_size;
	unsigned sc_hiz_tile_fifo_size;
	unsigned sc_earlyz_tile_fifo_size;
	unsigned tiling_nbanks;
	unsigned tiling_npipes;
	unsigned tiling_group_size;
	unsigned tile_config;
	unsigned backend_map;
};

struct cayman_asic {
	unsigned max_shader_engines;
	unsigned max_pipes_per_simd;
	unsigned max_tile_pipes;
	unsigned max_simds_per_se;
	unsigned max_backends_per_se;
	unsigned max_texture_channel_caches;
	unsigned max_gprs;
	unsigned max_threads;
	unsigned max_gs_threads;
	unsigned max_stack_entries;
	unsigned sx_num_of_sets;
	unsigned sx_max_export_size;
	unsigned sx_max_export_pos_size;
	unsigned sx_max_export_smx_size;
	unsigned max_hw_contexts;
	unsigned sq_num_cf_insts;
	unsigned sc_prim_fifo_size;
	unsigned sc_hiz_tile_fifo_size;
	unsigned sc_earlyz_tile_fifo_size;

	unsigned num_shader_engines;
	unsigned num_shader_pipes_per_simd;
	unsigned num_tile_pipes;
	unsigned num_simds_per_se;
	unsigned num_backends_per_se;
	unsigned backend_disable_mask_per_asic;
	unsigned backend_map;
	unsigned num_texture_channel_caches;
	unsigned mem_max_burst_length_bytes;
	unsigned mem_row_size_in_kb;
	unsigned shader_engine_tile_size;
	unsigned num_gpus;
	unsigned multi_gpu_tile_size;

	unsigned tile_config;
};

struct si_asic {
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
	unsigned num_backends_per_se;
	unsigned backend_disable_mask_per_asic;
	unsigned backend_map;
	unsigned num_texture_channel_caches;
	unsigned mem_max_burst_length_bytes;
	unsigned mem_row_size_in_kb;
	unsigned shader_engine_tile_size;
	unsigned num_gpus;
	unsigned multi_gpu_tile_size;

	unsigned tile_config;
	uint32_t tile_mode_array[32];
};

struct cik_asic {
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
	unsigned num_backends_per_se;
	unsigned backend_disable_mask_per_asic;
	unsigned backend_map;
	unsigned num_texture_channel_caches;
	unsigned mem_max_burst_length_bytes;
	unsigned mem_row_size_in_kb;
	unsigned shader_engine_tile_size;
	unsigned num_gpus;
	unsigned multi_gpu_tile_size;

	unsigned tile_config;
	uint32_t tile_mode_array[32];
};

union radeon_asic_config {
	struct r300_asic	r300;
	struct r100_asic	r100;
	struct r600_asic	r600;
	struct rv770_asic	rv770;
	struct evergreen_asic	evergreen;
	struct cayman_asic	cayman;
	struct si_asic		si;
	struct cik_asic		cik;
};

/*
 * asic initizalization from radeon_asic.c
 */
void radeon_agp_disable(struct radeon_device *rdev);
int radeon_asic_init(struct radeon_device *rdev);


/*
 * IOCTL.
 */
int radeon_gem_info_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *filp);
int radeon_gem_create_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *filp);
int radeon_gem_pin_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
int radeon_gem_unpin_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);
int radeon_gem_pwrite_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv);
int radeon_gem_pread_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);
int radeon_gem_set_domain_ioctl(struct drm_device *dev, void *data,
				struct drm_file *filp);
int radeon_gem_mmap_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *filp);
int radeon_gem_busy_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *filp);
int radeon_gem_wait_idle_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *filp);
int radeon_gem_va_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *filp);
int radeon_cs_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
int radeon_gem_set_tiling_ioctl(struct drm_device *dev, void *data,
				struct drm_file *filp);
int radeon_gem_get_tiling_ioctl(struct drm_device *dev, void *data,
				struct drm_file *filp);

/* VRAM scratch page for HDP bug, default vram page */
struct r600_vram_scratch {
	struct radeon_bo		*robj;
	volatile uint32_t		*ptr;
	u64				gpu_addr;
};

/*
 * ACPI
 */
struct radeon_atif_notification_cfg {
	bool enabled;
	int command_code;
};

struct radeon_atif_notifications {
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

struct radeon_atif_functions {
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

struct radeon_atif {
	struct radeon_atif_notifications notifications;
	struct radeon_atif_functions functions;
	struct radeon_atif_notification_cfg notification_cfg;
	struct radeon_encoder *encoder_for_bl;
};

struct radeon_atcs_functions {
	bool get_ext_state;
	bool pcie_perf_req;
	bool pcie_dev_rdy;
	bool pcie_bus_width;
};

struct radeon_atcs {
	struct radeon_atcs_functions functions;
};

/*
 * Core structure, functions and helpers.
 */
typedef uint32_t (*radeon_rreg_t)(struct radeon_device*, uint32_t);
typedef void (*radeon_wreg_t)(struct radeon_device*, uint32_t, uint32_t);

struct radeon_device {
	struct device			*dev;
	struct drm_device		*ddev;
	struct pci_dev			*pdev;
	struct rw_semaphore		exclusive_lock;
	/* ASIC */
	union radeon_asic_config	config;
	enum radeon_family		family;
	unsigned long			flags;
	int				usec_timeout;
	enum radeon_pll_errata		pll_errata;
	int				num_gb_pipes;
	int				num_z_pipes;
	int				disp_priority;
	/* BIOS */
	uint8_t				*bios;
	bool				is_atom_bios;
	uint16_t			bios_header_start;
	struct radeon_bo		*stollen_vga_memory;
	/* Register mmio */
	resource_size_t			rmmio_base;
	resource_size_t			rmmio_size;
	/* protects concurrent MM_INDEX/DATA based register access */
	spinlock_t mmio_idx_lock;
	void __iomem			*rmmio;
	radeon_rreg_t			mc_rreg;
	radeon_wreg_t			mc_wreg;
	radeon_rreg_t			pll_rreg;
	radeon_wreg_t			pll_wreg;
	uint32_t                        pcie_reg_mask;
	radeon_rreg_t			pciep_rreg;
	radeon_wreg_t			pciep_wreg;
	/* io port */
	void __iomem                    *rio_mem;
	resource_size_t			rio_mem_size;
	struct radeon_clock             clock;
	struct radeon_mc		mc;
	struct radeon_gart		gart;
	struct radeon_mode_info		mode_info;
	struct radeon_scratch		scratch;
	struct radeon_doorbell		doorbell;
	struct radeon_mman		mman;
	struct radeon_fence_driver	fence_drv[RADEON_NUM_RINGS];
	wait_queue_head_t		fence_queue;
	struct mutex			ring_lock;
	struct radeon_ring		ring[RADEON_NUM_RINGS];
	bool				ib_pool_ready;
	struct radeon_sa_manager	ring_tmp_bo;
	struct radeon_irq		irq;
	struct radeon_asic		*asic;
	struct radeon_gem		gem;
	struct radeon_pm		pm;
	struct radeon_uvd		uvd;
	uint32_t			bios_scratch[RADEON_BIOS_NUM_SCRATCH];
	struct radeon_wb		wb;
	struct radeon_dummy_page	dummy_page;
	bool				shutdown;
	bool				suspend;
	bool				need_dma32;
	bool				accel_working;
	bool				fastfb_working; /* IGP feature*/
	struct radeon_surface_reg surface_regs[RADEON_GEM_MAX_SURFACES];
	const struct firmware *me_fw;	/* all family ME firmware */
	const struct firmware *pfp_fw;	/* r6/700 PFP firmware */
	const struct firmware *rlc_fw;	/* r6/700 RLC firmware */
	const struct firmware *mc_fw;	/* NI MC firmware */
	const struct firmware *ce_fw;	/* SI CE firmware */
	const struct firmware *mec_fw;	/* CIK MEC firmware */
	const struct firmware *sdma_fw;	/* CIK SDMA firmware */
	const struct firmware *smc_fw;	/* SMC firmware */
	const struct firmware *uvd_fw;	/* UVD firmware */
	struct r600_vram_scratch vram_scratch;
	int msi_enabled; /* msi enabled */
	struct r600_ih ih; /* r6/700 interrupt ring */
	struct radeon_rlc rlc;
	struct radeon_mec mec;
	struct work_struct hotplug_work;
	struct work_struct audio_work;
	struct work_struct reset_work;
	int num_crtc; /* number of crtcs */
	struct mutex dc_hw_i2c_mutex; /* display controller hw i2c mutex */
	bool audio_enabled;
	bool has_uvd;
	struct r600_audio audio_status; /* audio stuff */
	struct notifier_block acpi_nb;
	/* only one userspace can use Hyperz features or CMASK at a time */
	struct drm_file *hyperz_filp;
	struct drm_file *cmask_filp;
	/* i2c buses */
	struct radeon_i2c_chan *i2c_bus[RADEON_MAX_I2C_BUS];
	/* debugfs */
	struct radeon_debugfs	debugfs[RADEON_DEBUGFS_MAX_COMPONENTS];
	unsigned 		debugfs_count;
	/* virtual memory */
	struct radeon_vm_manager	vm_manager;
	struct mutex			gpu_clock_mutex;
	/* ACPI interface */
	struct radeon_atif		atif;
	struct radeon_atcs		atcs;
	/* srbm instance registers */
	struct mutex			srbm_mutex;
};

int radeon_device_init(struct radeon_device *rdev,
		       struct drm_device *ddev,
		       struct pci_dev *pdev,
		       uint32_t flags);
void radeon_device_fini(struct radeon_device *rdev);
int radeon_gpu_wait_for_idle(struct radeon_device *rdev);

uint32_t r100_mm_rreg(struct radeon_device *rdev, uint32_t reg,
		      bool always_indirect);
void r100_mm_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v,
		  bool always_indirect);
u32 r100_io_rreg(struct radeon_device *rdev, u32 reg);
void r100_io_wreg(struct radeon_device *rdev, u32 reg, u32 v);

u32 cik_mm_rdoorbell(struct radeon_device *rdev, u32 offset);
void cik_mm_wdoorbell(struct radeon_device *rdev, u32 offset, u32 v);

/*
 * Cast helper
 */
#define to_radeon_fence(p) ((struct radeon_fence *)(p))

/*
 * Registers read & write functions.
 */
#define RREG8(reg) readb((rdev->rmmio) + (reg))
#define WREG8(reg, v) writeb(v, (rdev->rmmio) + (reg))
#define RREG16(reg) readw((rdev->rmmio) + (reg))
#define WREG16(reg, v) writew(v, (rdev->rmmio) + (reg))
#define RREG32(reg) r100_mm_rreg(rdev, (reg), false)
#define RREG32_IDX(reg) r100_mm_rreg(rdev, (reg), true)
#define DREG32(reg) printk(KERN_INFO "REGISTER: " #reg " : 0x%08X\n", r100_mm_rreg(rdev, (reg), false))
#define WREG32(reg, v) r100_mm_wreg(rdev, (reg), (v), false)
#define WREG32_IDX(reg, v) r100_mm_wreg(rdev, (reg), (v), true)
#define REG_SET(FIELD, v) (((v) << FIELD##_SHIFT) & FIELD##_MASK)
#define REG_GET(FIELD, v) (((v) << FIELD##_SHIFT) & FIELD##_MASK)
#define RREG32_PLL(reg) rdev->pll_rreg(rdev, (reg))
#define WREG32_PLL(reg, v) rdev->pll_wreg(rdev, (reg), (v))
#define RREG32_MC(reg) rdev->mc_rreg(rdev, (reg))
#define WREG32_MC(reg, v) rdev->mc_wreg(rdev, (reg), (v))
#define RREG32_PCIE(reg) rv370_pcie_rreg(rdev, (reg))
#define WREG32_PCIE(reg, v) rv370_pcie_wreg(rdev, (reg), (v))
#define RREG32_PCIE_PORT(reg) rdev->pciep_rreg(rdev, (reg))
#define WREG32_PCIE_PORT(reg, v) rdev->pciep_wreg(rdev, (reg), (v))
#define RREG32_SMC(reg) tn_smc_rreg(rdev, (reg))
#define WREG32_SMC(reg, v) tn_smc_wreg(rdev, (reg), (v))
#define RREG32_RCU(reg) r600_rcu_rreg(rdev, (reg))
#define WREG32_RCU(reg, v) r600_rcu_wreg(rdev, (reg), (v))
#define RREG32_CG(reg) eg_cg_rreg(rdev, (reg))
#define WREG32_CG(reg, v) eg_cg_wreg(rdev, (reg), (v))
#define RREG32_PIF_PHY0(reg) eg_pif_phy0_rreg(rdev, (reg))
#define WREG32_PIF_PHY0(reg, v) eg_pif_phy0_wreg(rdev, (reg), (v))
#define RREG32_PIF_PHY1(reg) eg_pif_phy1_rreg(rdev, (reg))
#define WREG32_PIF_PHY1(reg, v) eg_pif_phy1_wreg(rdev, (reg), (v))
#define RREG32_UVD_CTX(reg) r600_uvd_ctx_rreg(rdev, (reg))
#define WREG32_UVD_CTX(reg, v) r600_uvd_ctx_wreg(rdev, (reg), (v))
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
#define DREG32_SYS(sqf, rdev, reg) seq_printf((sqf), #reg " : 0x%08X\n", r100_mm_rreg((rdev), (reg), false))
#define RREG32_IO(reg) r100_io_rreg(rdev, (reg))
#define WREG32_IO(reg, v) r100_io_wreg(rdev, (reg), (v))

#define RDOORBELL32(offset) cik_mm_rdoorbell(rdev, (offset))
#define WDOORBELL32(offset, v) cik_mm_wdoorbell(rdev, (offset), (v))

/*
 * Indirect registers accessor
 */
static inline uint32_t rv370_pcie_rreg(struct radeon_device *rdev, uint32_t reg)
{
	uint32_t r;

	WREG32(RADEON_PCIE_INDEX, ((reg) & rdev->pcie_reg_mask));
	r = RREG32(RADEON_PCIE_DATA);
	return r;
}

static inline void rv370_pcie_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v)
{
	WREG32(RADEON_PCIE_INDEX, ((reg) & rdev->pcie_reg_mask));
	WREG32(RADEON_PCIE_DATA, (v));
}

static inline u32 tn_smc_rreg(struct radeon_device *rdev, u32 reg)
{
	u32 r;

	WREG32(TN_SMC_IND_INDEX_0, (reg));
	r = RREG32(TN_SMC_IND_DATA_0);
	return r;
}

static inline void tn_smc_wreg(struct radeon_device *rdev, u32 reg, u32 v)
{
	WREG32(TN_SMC_IND_INDEX_0, (reg));
	WREG32(TN_SMC_IND_DATA_0, (v));
}

static inline u32 r600_rcu_rreg(struct radeon_device *rdev, u32 reg)
{
	u32 r;

	WREG32(R600_RCU_INDEX, ((reg) & 0x1fff));
	r = RREG32(R600_RCU_DATA);
	return r;
}

static inline void r600_rcu_wreg(struct radeon_device *rdev, u32 reg, u32 v)
{
	WREG32(R600_RCU_INDEX, ((reg) & 0x1fff));
	WREG32(R600_RCU_DATA, (v));
}

static inline u32 eg_cg_rreg(struct radeon_device *rdev, u32 reg)
{
	u32 r;

	WREG32(EVERGREEN_CG_IND_ADDR, ((reg) & 0xffff));
	r = RREG32(EVERGREEN_CG_IND_DATA);
	return r;
}

static inline void eg_cg_wreg(struct radeon_device *rdev, u32 reg, u32 v)
{
	WREG32(EVERGREEN_CG_IND_ADDR, ((reg) & 0xffff));
	WREG32(EVERGREEN_CG_IND_DATA, (v));
}

static inline u32 eg_pif_phy0_rreg(struct radeon_device *rdev, u32 reg)
{
	u32 r;

	WREG32(EVERGREEN_PIF_PHY0_INDEX, ((reg) & 0xffff));
	r = RREG32(EVERGREEN_PIF_PHY0_DATA);
	return r;
}

static inline void eg_pif_phy0_wreg(struct radeon_device *rdev, u32 reg, u32 v)
{
	WREG32(EVERGREEN_PIF_PHY0_INDEX, ((reg) & 0xffff));
	WREG32(EVERGREEN_PIF_PHY0_DATA, (v));
}

static inline u32 eg_pif_phy1_rreg(struct radeon_device *rdev, u32 reg)
{
	u32 r;

	WREG32(EVERGREEN_PIF_PHY1_INDEX, ((reg) & 0xffff));
	r = RREG32(EVERGREEN_PIF_PHY1_DATA);
	return r;
}

static inline void eg_pif_phy1_wreg(struct radeon_device *rdev, u32 reg, u32 v)
{
	WREG32(EVERGREEN_PIF_PHY1_INDEX, ((reg) & 0xffff));
	WREG32(EVERGREEN_PIF_PHY1_DATA, (v));
}

static inline u32 r600_uvd_ctx_rreg(struct radeon_device *rdev, u32 reg)
{
	u32 r;

	WREG32(R600_UVD_CTX_INDEX, ((reg) & 0x1ff));
	r = RREG32(R600_UVD_CTX_DATA);
	return r;
}

static inline void r600_uvd_ctx_wreg(struct radeon_device *rdev, u32 reg, u32 v)
{
	WREG32(R600_UVD_CTX_INDEX, ((reg) & 0x1ff));
	WREG32(R600_UVD_CTX_DATA, (v));
}

void r100_pll_errata_after_index(struct radeon_device *rdev);


/*
 * ASICs helpers.
 */
#define ASIC_IS_RN50(rdev) ((rdev->pdev->device == 0x515e) || \
			    (rdev->pdev->device == 0x5969))
#define ASIC_IS_RV100(rdev) ((rdev->family == CHIP_RV100) || \
		(rdev->family == CHIP_RV200) || \
		(rdev->family == CHIP_RS100) || \
		(rdev->family == CHIP_RS200) || \
		(rdev->family == CHIP_RV250) || \
		(rdev->family == CHIP_RV280) || \
		(rdev->family == CHIP_RS300))
#define ASIC_IS_R300(rdev) ((rdev->family == CHIP_R300)  ||	\
		(rdev->family == CHIP_RV350) ||			\
		(rdev->family == CHIP_R350)  ||			\
		(rdev->family == CHIP_RV380) ||			\
		(rdev->family == CHIP_R420)  ||			\
		(rdev->family == CHIP_R423)  ||			\
		(rdev->family == CHIP_RV410) ||			\
		(rdev->family == CHIP_RS400) ||			\
		(rdev->family == CHIP_RS480))
#define ASIC_IS_X2(rdev) ((rdev->ddev->pdev->device == 0x9441) || \
		(rdev->ddev->pdev->device == 0x9443) || \
		(rdev->ddev->pdev->device == 0x944B) || \
		(rdev->ddev->pdev->device == 0x9506) || \
		(rdev->ddev->pdev->device == 0x9509) || \
		(rdev->ddev->pdev->device == 0x950F) || \
		(rdev->ddev->pdev->device == 0x689C) || \
		(rdev->ddev->pdev->device == 0x689D))
#define ASIC_IS_AVIVO(rdev) ((rdev->family >= CHIP_RS600))
#define ASIC_IS_DCE2(rdev) ((rdev->family == CHIP_RS600)  ||	\
			    (rdev->family == CHIP_RS690)  ||	\
			    (rdev->family == CHIP_RS740)  ||	\
			    (rdev->family >= CHIP_R600))
#define ASIC_IS_DCE3(rdev) ((rdev->family >= CHIP_RV620))
#define ASIC_IS_DCE32(rdev) ((rdev->family >= CHIP_RV730))
#define ASIC_IS_DCE4(rdev) ((rdev->family >= CHIP_CEDAR))
#define ASIC_IS_DCE41(rdev) ((rdev->family >= CHIP_PALM) && \
			     (rdev->flags & RADEON_IS_IGP))
#define ASIC_IS_DCE5(rdev) ((rdev->family >= CHIP_BARTS))
#define ASIC_IS_DCE6(rdev) ((rdev->family >= CHIP_ARUBA))
#define ASIC_IS_DCE61(rdev) ((rdev->family >= CHIP_ARUBA) && \
			     (rdev->flags & RADEON_IS_IGP))
#define ASIC_IS_DCE64(rdev) ((rdev->family == CHIP_OLAND))
#define ASIC_IS_NODCE(rdev) ((rdev->family == CHIP_HAINAN))
#define ASIC_IS_DCE8(rdev) ((rdev->family >= CHIP_BONAIRE))

#define ASIC_IS_LOMBOK(rdev) ((rdev->ddev->pdev->device == 0x6849) || \
			      (rdev->ddev->pdev->device == 0x6850) || \
			      (rdev->ddev->pdev->device == 0x6858) || \
			      (rdev->ddev->pdev->device == 0x6859) || \
			      (rdev->ddev->pdev->device == 0x6840) || \
			      (rdev->ddev->pdev->device == 0x6841) || \
			      (rdev->ddev->pdev->device == 0x6842) || \
			      (rdev->ddev->pdev->device == 0x6843))

/*
 * BIOS helpers.
 */
#define RBIOS8(i) (rdev->bios[i])
#define RBIOS16(i) (RBIOS8(i) | (RBIOS8((i)+1) << 8))
#define RBIOS32(i) ((RBIOS16(i)) | (RBIOS16((i)+2) << 16))

int radeon_combios_init(struct radeon_device *rdev);
void radeon_combios_fini(struct radeon_device *rdev);
int radeon_atombios_init(struct radeon_device *rdev);
void radeon_atombios_fini(struct radeon_device *rdev);


/*
 * RING helpers.
 */
#if DRM_DEBUG_CODE == 0
static inline void radeon_ring_write(struct radeon_ring *ring, uint32_t v)
{
	ring->ring[ring->wptr++] = v;
	ring->wptr &= ring->ptr_mask;
	ring->count_dw--;
	ring->ring_free_dw--;
}
#else
/* With debugging this is just too big to inline */
void radeon_ring_write(struct radeon_ring *ring, uint32_t v);
#endif

/*
 * ASICs macro.
 */
#define radeon_init(rdev) (rdev)->asic->init((rdev))
#define radeon_fini(rdev) (rdev)->asic->fini((rdev))
#define radeon_resume(rdev) (rdev)->asic->resume((rdev))
#define radeon_suspend(rdev) (rdev)->asic->suspend((rdev))
#define radeon_cs_parse(rdev, r, p) (rdev)->asic->ring[(r)].cs_parse((p))
#define radeon_vga_set_state(rdev, state) (rdev)->asic->vga_set_state((rdev), (state))
#define radeon_asic_reset(rdev) (rdev)->asic->asic_reset((rdev))
#define radeon_gart_tlb_flush(rdev) (rdev)->asic->gart.tlb_flush((rdev))
#define radeon_gart_set_page(rdev, i, p) (rdev)->asic->gart.set_page((rdev), (i), (p))
#define radeon_asic_vm_init(rdev) (rdev)->asic->vm.init((rdev))
#define radeon_asic_vm_fini(rdev) (rdev)->asic->vm.fini((rdev))
#define radeon_asic_vm_set_page(rdev, ib, pe, addr, count, incr, flags) ((rdev)->asic->vm.set_page((rdev), (ib), (pe), (addr), (count), (incr), (flags)))
#define radeon_ring_start(rdev, r, cp) (rdev)->asic->ring[(r)].ring_start((rdev), (cp))
#define radeon_ring_test(rdev, r, cp) (rdev)->asic->ring[(r)].ring_test((rdev), (cp))
#define radeon_ib_test(rdev, r, cp) (rdev)->asic->ring[(r)].ib_test((rdev), (cp))
#define radeon_ring_ib_execute(rdev, r, ib) (rdev)->asic->ring[(r)].ib_execute((rdev), (ib))
#define radeon_ring_ib_parse(rdev, r, ib) (rdev)->asic->ring[(r)].ib_parse((rdev), (ib))
#define radeon_ring_is_lockup(rdev, r, cp) (rdev)->asic->ring[(r)].is_lockup((rdev), (cp))
#define radeon_ring_vm_flush(rdev, r, vm) (rdev)->asic->ring[(r)].vm_flush((rdev), (r), (vm))
#define radeon_ring_get_rptr(rdev, r) (rdev)->asic->ring[(r)->idx].get_rptr((rdev), (r))
#define radeon_ring_get_wptr(rdev, r) (rdev)->asic->ring[(r)->idx].get_wptr((rdev), (r))
#define radeon_ring_set_wptr(rdev, r) (rdev)->asic->ring[(r)->idx].set_wptr((rdev), (r))
#define radeon_irq_set(rdev) (rdev)->asic->irq.set((rdev))
#define radeon_irq_process(rdev) (rdev)->asic->irq.process((rdev))
#define radeon_get_vblank_counter(rdev, crtc) (rdev)->asic->display.get_vblank_counter((rdev), (crtc))
#define radeon_set_backlight_level(rdev, e, l) (rdev)->asic->display.set_backlight_level((e), (l))
#define radeon_get_backlight_level(rdev, e) (rdev)->asic->display.get_backlight_level((e))
#define radeon_hdmi_enable(rdev, e, b) (rdev)->asic->display.hdmi_enable((e), (b))
#define radeon_hdmi_setmode(rdev, e, m) (rdev)->asic->display.hdmi_setmode((e), (m))
#define radeon_fence_ring_emit(rdev, r, fence) (rdev)->asic->ring[(r)].emit_fence((rdev), (fence))
#define radeon_semaphore_ring_emit(rdev, r, cp, semaphore, emit_wait) (rdev)->asic->ring[(r)].emit_semaphore((rdev), (cp), (semaphore), (emit_wait))
#define radeon_copy_blit(rdev, s, d, np, f) (rdev)->asic->copy.blit((rdev), (s), (d), (np), (f))
#define radeon_copy_dma(rdev, s, d, np, f) (rdev)->asic->copy.dma((rdev), (s), (d), (np), (f))
#define radeon_copy(rdev, s, d, np, f) (rdev)->asic->copy.copy((rdev), (s), (d), (np), (f))
#define radeon_copy_blit_ring_index(rdev) (rdev)->asic->copy.blit_ring_index
#define radeon_copy_dma_ring_index(rdev) (rdev)->asic->copy.dma_ring_index
#define radeon_copy_ring_index(rdev) (rdev)->asic->copy.copy_ring_index
#define radeon_get_engine_clock(rdev) (rdev)->asic->pm.get_engine_clock((rdev))
#define radeon_set_engine_clock(rdev, e) (rdev)->asic->pm.set_engine_clock((rdev), (e))
#define radeon_get_memory_clock(rdev) (rdev)->asic->pm.get_memory_clock((rdev))
#define radeon_set_memory_clock(rdev, e) (rdev)->asic->pm.set_memory_clock((rdev), (e))
#define radeon_get_pcie_lanes(rdev) (rdev)->asic->pm.get_pcie_lanes((rdev))
#define radeon_set_pcie_lanes(rdev, l) (rdev)->asic->pm.set_pcie_lanes((rdev), (l))
#define radeon_set_clock_gating(rdev, e) (rdev)->asic->pm.set_clock_gating((rdev), (e))
#define radeon_set_uvd_clocks(rdev, v, d) (rdev)->asic->pm.set_uvd_clocks((rdev), (v), (d))
#define radeon_get_temperature(rdev) (rdev)->asic->pm.get_temperature((rdev))
#define radeon_set_surface_reg(rdev, r, f, p, o, s) ((rdev)->asic->surface.set_reg((rdev), (r), (f), (p), (o), (s)))
#define radeon_clear_surface_reg(rdev, r) ((rdev)->asic->surface.clear_reg((rdev), (r)))
#define radeon_bandwidth_update(rdev) (rdev)->asic->display.bandwidth_update((rdev))
#define radeon_hpd_init(rdev) (rdev)->asic->hpd.init((rdev))
#define radeon_hpd_fini(rdev) (rdev)->asic->hpd.fini((rdev))
#define radeon_hpd_sense(rdev, h) (rdev)->asic->hpd.sense((rdev), (h))
#define radeon_hpd_set_polarity(rdev, h) (rdev)->asic->hpd.set_polarity((rdev), (h))
#define radeon_gui_idle(rdev) (rdev)->asic->gui_idle((rdev))
#define radeon_pm_misc(rdev) (rdev)->asic->pm.misc((rdev))
#define radeon_pm_prepare(rdev) (rdev)->asic->pm.prepare((rdev))
#define radeon_pm_finish(rdev) (rdev)->asic->pm.finish((rdev))
#define radeon_pm_init_profile(rdev) (rdev)->asic->pm.init_profile((rdev))
#define radeon_pm_get_dynpm_state(rdev) (rdev)->asic->pm.get_dynpm_state((rdev))
#define radeon_pre_page_flip(rdev, crtc) (rdev)->asic->pflip.pre_page_flip((rdev), (crtc))
#define radeon_page_flip(rdev, crtc, base) (rdev)->asic->pflip.page_flip((rdev), (crtc), (base))
#define radeon_post_page_flip(rdev, crtc) (rdev)->asic->pflip.post_page_flip((rdev), (crtc))
#define radeon_wait_for_vblank(rdev, crtc) (rdev)->asic->display.wait_for_vblank((rdev), (crtc))
#define radeon_mc_wait_for_idle(rdev) (rdev)->asic->mc_wait_for_idle((rdev))
#define radeon_get_xclk(rdev) (rdev)->asic->get_xclk((rdev))
#define radeon_get_gpu_clock_counter(rdev) (rdev)->asic->get_gpu_clock_counter((rdev))
#define radeon_dpm_init(rdev) rdev->asic->dpm.init((rdev))
#define radeon_dpm_setup_asic(rdev) rdev->asic->dpm.setup_asic((rdev))
#define radeon_dpm_enable(rdev) rdev->asic->dpm.enable((rdev))
#define radeon_dpm_disable(rdev) rdev->asic->dpm.disable((rdev))
#define radeon_dpm_pre_set_power_state(rdev) rdev->asic->dpm.pre_set_power_state((rdev))
#define radeon_dpm_set_power_state(rdev) rdev->asic->dpm.set_power_state((rdev))
#define radeon_dpm_post_set_power_state(rdev) rdev->asic->dpm.post_set_power_state((rdev))
#define radeon_dpm_display_configuration_changed(rdev) rdev->asic->dpm.display_configuration_changed((rdev))
#define radeon_dpm_fini(rdev) rdev->asic->dpm.fini((rdev))
#define radeon_dpm_get_sclk(rdev, l) rdev->asic->dpm.get_sclk((rdev), (l))
#define radeon_dpm_get_mclk(rdev, l) rdev->asic->dpm.get_mclk((rdev), (l))
#define radeon_dpm_print_power_state(rdev, ps) rdev->asic->dpm.print_power_state((rdev), (ps))
#define radeon_dpm_debugfs_print_current_performance_level(rdev, m) rdev->asic->dpm.debugfs_print_current_performance_level((rdev), (m))
#define radeon_dpm_force_performance_level(rdev, l) rdev->asic->dpm.force_performance_level((rdev), (l))
#define radeon_dpm_vblank_too_short(rdev) rdev->asic->dpm.vblank_too_short((rdev))

/* Common functions */
/* AGP */
extern int radeon_gpu_reset(struct radeon_device *rdev);
extern void r600_set_bios_scratch_engine_hung(struct radeon_device *rdev, bool hung);
extern void radeon_agp_disable(struct radeon_device *rdev);
extern int radeon_modeset_init(struct radeon_device *rdev);
extern void radeon_modeset_fini(struct radeon_device *rdev);
extern bool radeon_card_posted(struct radeon_device *rdev);
extern void radeon_update_bandwidth_info(struct radeon_device *rdev);
extern void radeon_update_display_priority(struct radeon_device *rdev);
extern bool radeon_boot_test_post_card(struct radeon_device *rdev);
extern void radeon_scratch_init(struct radeon_device *rdev);
extern void radeon_wb_fini(struct radeon_device *rdev);
extern int radeon_wb_init(struct radeon_device *rdev);
extern void radeon_wb_disable(struct radeon_device *rdev);
extern void radeon_surface_init(struct radeon_device *rdev);
extern int radeon_cs_parser_init(struct radeon_cs_parser *p, void *data);
extern void radeon_legacy_set_clock_gating(struct radeon_device *rdev, int enable);
extern void radeon_atom_set_clock_gating(struct radeon_device *rdev, int enable);
extern void radeon_ttm_placement_from_domain(struct radeon_bo *rbo, u32 domain);
extern bool radeon_ttm_bo_is_radeon_bo(struct ttm_buffer_object *bo);
extern void radeon_vram_location(struct radeon_device *rdev, struct radeon_mc *mc, u64 base);
extern void radeon_gtt_location(struct radeon_device *rdev, struct radeon_mc *mc);
extern int radeon_resume_kms(struct drm_device *dev);
extern int radeon_suspend_kms(struct drm_device *dev, pm_message_t state);
extern void radeon_ttm_set_active_vram_size(struct radeon_device *rdev, u64 size);
extern void radeon_program_register_sequence(struct radeon_device *rdev,
					     const u32 *registers,
					     const u32 array_size);

/*
 * vm
 */
int radeon_vm_manager_init(struct radeon_device *rdev);
void radeon_vm_manager_fini(struct radeon_device *rdev);
void radeon_vm_init(struct radeon_device *rdev, struct radeon_vm *vm);
void radeon_vm_fini(struct radeon_device *rdev, struct radeon_vm *vm);
int radeon_vm_alloc_pt(struct radeon_device *rdev, struct radeon_vm *vm);
void radeon_vm_add_to_lru(struct radeon_device *rdev, struct radeon_vm *vm);
struct radeon_fence *radeon_vm_grab_id(struct radeon_device *rdev,
				       struct radeon_vm *vm, int ring);
void radeon_vm_fence(struct radeon_device *rdev,
		     struct radeon_vm *vm,
		     struct radeon_fence *fence);
uint64_t radeon_vm_map_gart(struct radeon_device *rdev, uint64_t addr);
int radeon_vm_bo_update_pte(struct radeon_device *rdev,
			    struct radeon_vm *vm,
			    struct radeon_bo *bo,
			    struct ttm_mem_reg *mem);
void radeon_vm_bo_invalidate(struct radeon_device *rdev,
			     struct radeon_bo *bo);
struct radeon_bo_va *radeon_vm_bo_find(struct radeon_vm *vm,
				       struct radeon_bo *bo);
struct radeon_bo_va *radeon_vm_bo_add(struct radeon_device *rdev,
				      struct radeon_vm *vm,
				      struct radeon_bo *bo);
int radeon_vm_bo_set_addr(struct radeon_device *rdev,
			  struct radeon_bo_va *bo_va,
			  uint64_t offset,
			  uint32_t flags);
int radeon_vm_bo_rmv(struct radeon_device *rdev,
		     struct radeon_bo_va *bo_va);

/* audio */
void r600_audio_update_hdmi(struct work_struct *work);

/*
 * R600 vram scratch functions
 */
int r600_vram_scratch_init(struct radeon_device *rdev);
void r600_vram_scratch_fini(struct radeon_device *rdev);

/*
 * r600 cs checking helper
 */
unsigned r600_mip_minify(unsigned size, unsigned level);
bool r600_fmt_is_valid_color(u32 format);
bool r600_fmt_is_valid_texture(u32 format, enum radeon_family family);
int r600_fmt_get_blocksize(u32 format);
int r600_fmt_get_nblocksx(u32 format, u32 w);
int r600_fmt_get_nblocksy(u32 format, u32 h);

/*
 * r600 functions used by radeon_encoder.c
 */
struct radeon_hdmi_acr {
	u32 clock;

	int n_32khz;
	int cts_32khz;

	int n_44_1khz;
	int cts_44_1khz;

	int n_48khz;
	int cts_48khz;

};

extern struct radeon_hdmi_acr r600_hdmi_acr(uint32_t clock);

extern u32 r6xx_remap_render_backend(struct radeon_device *rdev,
				     u32 tiling_pipe_num,
				     u32 max_rb_num,
				     u32 total_max_rb_num,
				     u32 enabled_rb_mask);

/*
 * evergreen functions used by radeon_encoder.c
 */

extern int ni_init_microcode(struct radeon_device *rdev);
extern int ni_mc_load_microcode(struct radeon_device *rdev);

/* radeon_acpi.c */
#if defined(CONFIG_ACPI)
extern int radeon_acpi_init(struct radeon_device *rdev);
extern void radeon_acpi_fini(struct radeon_device *rdev);
extern bool radeon_acpi_is_pcie_performance_request_supported(struct radeon_device *rdev);
extern int radeon_acpi_pcie_performance_request(struct radeon_device *rdev,
						u8 perf_req, bool advertise);
extern int radeon_acpi_pcie_notify_device_ready(struct radeon_device *rdev);
#else
static inline int radeon_acpi_init(struct radeon_device *rdev) { return 0; }
static inline void radeon_acpi_fini(struct radeon_device *rdev) { }
#endif

int radeon_cs_packet_parse(struct radeon_cs_parser *p,
			   struct radeon_cs_packet *pkt,
			   unsigned idx);
bool radeon_cs_packet_next_is_pkt3_nop(struct radeon_cs_parser *p);
void radeon_cs_dump_packet(struct radeon_cs_parser *p,
			   struct radeon_cs_packet *pkt);
int radeon_cs_packet_next_reloc(struct radeon_cs_parser *p,
				struct radeon_cs_reloc **cs_reloc,
				int nomm);
int r600_cs_common_vline_parse(struct radeon_cs_parser *p,
			       uint32_t *vline_start_end,
			       uint32_t *vline_status);

#include "radeon_object.h"

#endif
