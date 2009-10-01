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

#include "radeon_object.h"

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

#include <asm/atomic.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/kref.h>

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

/*
 * Copy from radeon_drv.h so we don't have to include both and have conflicting
 * symbol;
 */
#define RADEON_MAX_USEC_TIMEOUT		100000	/* 100 ms */
#define RADEON_IB_POOL_SIZE		16
#define RADEON_DEBUGFS_MAX_NUM_FILES	32
#define RADEONFB_CONN_LIMIT		4
#define RADEON_BIOS_NUM_SCRATCH		8

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
	struct radeon_pll spll;
	struct radeon_pll mpll;
	/* 10 Khz units */
	uint32_t default_mclk;
	uint32_t default_sclk;
};


/*
 * Fences.
 */
struct radeon_fence_driver {
	uint32_t			scratch_reg;
	atomic_t			seq;
	uint32_t			last_seq;
	unsigned long			count_timeout;
	wait_queue_head_t		queue;
	rwlock_t			lock;
	struct list_head		created;
	struct list_head		emited;
	struct list_head		signaled;
};

struct radeon_fence {
	struct radeon_device		*rdev;
	struct kref			kref;
	struct list_head		list;
	/* protected by radeon_fence.lock */
	uint32_t			seq;
	unsigned long			timeout;
	bool				emited;
	bool				signaled;
};

int radeon_fence_driver_init(struct radeon_device *rdev);
void radeon_fence_driver_fini(struct radeon_device *rdev);
int radeon_fence_create(struct radeon_device *rdev, struct radeon_fence **fence);
int radeon_fence_emit(struct radeon_device *rdev, struct radeon_fence *fence);
void radeon_fence_process(struct radeon_device *rdev);
bool radeon_fence_signaled(struct radeon_fence *fence);
int radeon_fence_wait(struct radeon_fence *fence, bool interruptible);
int radeon_fence_wait_next(struct radeon_device *rdev);
int radeon_fence_wait_last(struct radeon_device *rdev);
struct radeon_fence *radeon_fence_ref(struct radeon_fence *fence);
void radeon_fence_unref(struct radeon_fence **fence);

/*
 * Tiling registers
 */
struct radeon_surface_reg {
	struct radeon_object *robj;
};

#define RADEON_GEM_MAX_SURFACES 8

/*
 * Radeon buffer.
 */
struct radeon_object;

struct radeon_object_list {
	struct list_head	list;
	struct radeon_object	*robj;
	uint64_t		gpu_offset;
	unsigned		rdomain;
	unsigned		wdomain;
	uint32_t                tiling_flags;
};

int radeon_object_init(struct radeon_device *rdev);
void radeon_object_fini(struct radeon_device *rdev);
int radeon_object_create(struct radeon_device *rdev,
			 struct drm_gem_object *gobj,
			 unsigned long size,
			 bool kernel,
			 uint32_t domain,
			 bool interruptible,
			 struct radeon_object **robj_ptr);
int radeon_object_kmap(struct radeon_object *robj, void **ptr);
void radeon_object_kunmap(struct radeon_object *robj);
void radeon_object_unref(struct radeon_object **robj);
int radeon_object_pin(struct radeon_object *robj, uint32_t domain,
		      uint64_t *gpu_addr);
void radeon_object_unpin(struct radeon_object *robj);
int radeon_object_wait(struct radeon_object *robj);
int radeon_object_busy_domain(struct radeon_object *robj, uint32_t *cur_placement);
int radeon_object_evict_vram(struct radeon_device *rdev);
int radeon_object_mmap(struct radeon_object *robj, uint64_t *offset);
void radeon_object_force_delete(struct radeon_device *rdev);
void radeon_object_list_add_object(struct radeon_object_list *lobj,
				   struct list_head *head);
int radeon_object_list_validate(struct list_head *head, void *fence);
void radeon_object_list_unvalidate(struct list_head *head);
void radeon_object_list_clean(struct list_head *head);
int radeon_object_fbdev_mmap(struct radeon_object *robj,
			     struct vm_area_struct *vma);
unsigned long radeon_object_size(struct radeon_object *robj);
void radeon_object_clear_surface_reg(struct radeon_object *robj);
int radeon_object_check_tiling(struct radeon_object *robj, bool has_moved,
			       bool force_drop);
void radeon_object_set_tiling_flags(struct radeon_object *robj,
				    uint32_t tiling_flags, uint32_t pitch);
void radeon_object_get_tiling_flags(struct radeon_object *robj, uint32_t *tiling_flags, uint32_t *pitch);
void radeon_bo_move_notify(struct ttm_buffer_object *bo,
			   struct ttm_mem_reg *mem);
void radeon_bo_fault_reserve_notify(struct ttm_buffer_object *bo);
/*
 * GEM objects.
 */
struct radeon_gem {
	struct list_head	objects;
};

int radeon_gem_init(struct radeon_device *rdev);
void radeon_gem_fini(struct radeon_device *rdev);
int radeon_gem_object_create(struct radeon_device *rdev, int size,
			     int alignment, int initial_domain,
			     bool discardable, bool kernel,
			     bool interruptible,
			     struct drm_gem_object **obj);
int radeon_gem_object_pin(struct drm_gem_object *obj, uint32_t pin_domain,
			  uint64_t *gpu_addr);
void radeon_gem_object_unpin(struct drm_gem_object *obj);


/*
 * GART structures, functions & helpers
 */
struct radeon_mc;

struct radeon_gart_table_ram {
	volatile uint32_t		*ptr;
};

struct radeon_gart_table_vram {
	struct radeon_object		*robj;
	volatile uint32_t		*ptr;
};

union radeon_gart_table {
	struct radeon_gart_table_ram	ram;
	struct radeon_gart_table_vram	vram;
};

struct radeon_gart {
	dma_addr_t			table_addr;
	unsigned			num_gpu_pages;
	unsigned			num_cpu_pages;
	unsigned			table_size;
	union radeon_gart_table		table;
	struct page			**pages;
	dma_addr_t			*pages_addr;
	bool				ready;
};

int radeon_gart_table_ram_alloc(struct radeon_device *rdev);
void radeon_gart_table_ram_free(struct radeon_device *rdev);
int radeon_gart_table_vram_alloc(struct radeon_device *rdev);
void radeon_gart_table_vram_free(struct radeon_device *rdev);
int radeon_gart_init(struct radeon_device *rdev);
void radeon_gart_fini(struct radeon_device *rdev);
void radeon_gart_unbind(struct radeon_device *rdev, unsigned offset,
			int pages);
int radeon_gart_bind(struct radeon_device *rdev, unsigned offset,
		     int pages, struct page **pagelist);


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
	u64			gtt_location;
	u64			gtt_size;
	u64			gtt_start;
	u64			gtt_end;
	u64			vram_location;
	u64			vram_start;
	u64			vram_end;
	unsigned		vram_width;
	u64			real_vram_size;
	int			vram_mtrr;
	bool			vram_is_ddr;
};

int radeon_mc_setup(struct radeon_device *rdev);


/*
 * GPU scratch registers structures, functions & helpers
 */
struct radeon_scratch {
	unsigned		num_reg;
	bool			free[32];
	uint32_t		reg[32];
};

int radeon_scratch_get(struct radeon_device *rdev, uint32_t *reg);
void radeon_scratch_free(struct radeon_device *rdev, uint32_t reg);


/*
 * IRQS.
 */
struct radeon_irq {
	bool		installed;
	bool		sw_int;
	/* FIXME: use a define max crtc rather than hardcode it */
	bool		crtc_vblank_int[2];
};

int radeon_irq_kms_init(struct radeon_device *rdev);
void radeon_irq_kms_fini(struct radeon_device *rdev);


/*
 * CP & ring.
 */
struct radeon_ib {
	struct list_head	list;
	unsigned long		idx;
	uint64_t		gpu_addr;
	struct radeon_fence	*fence;
	uint32_t	*ptr;
	uint32_t		length_dw;
};

/*
 * locking -
 * mutex protects scheduled_ibs, ready, alloc_bm
 */
struct radeon_ib_pool {
	struct mutex		mutex;
	struct radeon_object	*robj;
	struct list_head	scheduled_ibs;
	struct radeon_ib	ibs[RADEON_IB_POOL_SIZE];
	bool			ready;
	DECLARE_BITMAP(alloc_bm, RADEON_IB_POOL_SIZE);
};

struct radeon_cp {
	struct radeon_object	*ring_obj;
	volatile uint32_t	*ring;
	unsigned		rptr;
	unsigned		wptr;
	unsigned		wptr_old;
	unsigned		ring_size;
	unsigned		ring_free_dw;
	int			count_dw;
	uint64_t		gpu_addr;
	uint32_t		align_mask;
	uint32_t		ptr_mask;
	struct mutex		mutex;
	bool			ready;
};

struct r600_blit {
	struct radeon_object	*shader_obj;
	u64 shader_gpu_addr;
	u32 vs_offset, ps_offset;
	u32 state_offset;
	u32 state_len;
	u32 vb_used, vb_total;
	struct radeon_ib *vb_ib;
};

int radeon_ib_get(struct radeon_device *rdev, struct radeon_ib **ib);
void radeon_ib_free(struct radeon_device *rdev, struct radeon_ib **ib);
int radeon_ib_schedule(struct radeon_device *rdev, struct radeon_ib *ib);
int radeon_ib_pool_init(struct radeon_device *rdev);
void radeon_ib_pool_fini(struct radeon_device *rdev);
int radeon_ib_test(struct radeon_device *rdev);
/* Ring access between begin & end cannot sleep */
void radeon_ring_free_size(struct radeon_device *rdev);
int radeon_ring_lock(struct radeon_device *rdev, unsigned ndw);
void radeon_ring_unlock_commit(struct radeon_device *rdev);
void radeon_ring_unlock_undo(struct radeon_device *rdev);
int radeon_ring_test(struct radeon_device *rdev);
int radeon_ring_init(struct radeon_device *rdev, unsigned ring_size);
void radeon_ring_fini(struct radeon_device *rdev);


/*
 * CS.
 */
struct radeon_cs_reloc {
	struct drm_gem_object		*gobj;
	struct radeon_object		*robj;
	struct radeon_object_list	lobj;
	uint32_t			handle;
	uint32_t			flags;
};

struct radeon_cs_chunk {
	uint32_t		chunk_id;
	uint32_t		length_dw;
	int kpage_idx[2];
	uint32_t                *kpage[2];
	uint32_t		*kdata;
	void __user *user_ptr;
	int last_copied_page;
	int last_page_index;
};

struct radeon_cs_parser {
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
	/* indices of various chunks */
	int			chunk_ib_idx;
	int			chunk_relocs_idx;
	struct radeon_ib	*ib;
	void			*track;
	unsigned		family;
	int parser_error;
};

extern int radeon_cs_update_pages(struct radeon_cs_parser *p, int pg_idx);
extern int radeon_cs_finish_pages(struct radeon_cs_parser *p);


static inline u32 radeon_get_ib_value(struct radeon_cs_parser *p, int idx)
{
	struct radeon_cs_chunk *ibc = &p->chunks[p->chunk_ib_idx];
	u32 pg_idx, pg_offset;
	u32 idx_value = 0;
	int new_page;

	pg_idx = (idx * 4) / PAGE_SIZE;
	pg_offset = (idx * 4) % PAGE_SIZE;

	if (ibc->kpage_idx[0] == pg_idx)
		return ibc->kpage[0][pg_offset/4];
	if (ibc->kpage_idx[1] == pg_idx)
		return ibc->kpage[1][pg_offset/4];

	new_page = radeon_cs_update_pages(p, pg_idx);
	if (new_page < 0) {
		p->parser_error = new_page;
		return 0;
	}

	idx_value = ibc->kpage[new_page][pg_offset/4];
	return idx_value;
}

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
void radeon_agp_fini(struct radeon_device *rdev);


/*
 * Writeback
 */
struct radeon_wb {
	struct radeon_object	*wb_obj;
	volatile uint32_t	*wb;
	uint64_t		gpu_addr;
};

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
 * @sclk:          	GPU clock Mhz (core bandwith depends of this clock)
 * @needed_bandwidth:   current bandwidth needs
 *
 * It keeps track of various data needed to take powermanagement decision.
 * Bandwith need is used to determine minimun clock of the GPU and memory.
 * Equation between gpu/memory clock and available bandwidth is hw dependent
 * (type of memory, bus size, efficiency, ...)
 */
struct radeon_pm {
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
	fixed20_12		needed_bandwidth;
};


/*
 * Benchmarking
 */
void radeon_benchmark(struct radeon_device *rdev);


/*
 * Testing
 */
void radeon_test_moves(struct radeon_device *rdev);


/*
 * Debugfs
 */
int radeon_debugfs_add_files(struct radeon_device *rdev,
			     struct drm_info_list *files,
			     unsigned nfiles);
int radeon_debugfs_fence_init(struct radeon_device *rdev);
int r100_debugfs_rbbm_init(struct radeon_device *rdev);
int r100_debugfs_cp_init(struct radeon_device *rdev);


/*
 * ASIC specific functions.
 */
struct radeon_asic {
	int (*init)(struct radeon_device *rdev);
	void (*fini)(struct radeon_device *rdev);
	int (*resume)(struct radeon_device *rdev);
	int (*suspend)(struct radeon_device *rdev);
	void (*errata)(struct radeon_device *rdev);
	void (*vram_info)(struct radeon_device *rdev);
	void (*vga_set_state)(struct radeon_device *rdev, bool state);
	int (*gpu_reset)(struct radeon_device *rdev);
	int (*mc_init)(struct radeon_device *rdev);
	void (*mc_fini)(struct radeon_device *rdev);
	int (*wb_init)(struct radeon_device *rdev);
	void (*wb_fini)(struct radeon_device *rdev);
	int (*gart_init)(struct radeon_device *rdev);
	void (*gart_fini)(struct radeon_device *rdev);
	int (*gart_enable)(struct radeon_device *rdev);
	void (*gart_disable)(struct radeon_device *rdev);
	void (*gart_tlb_flush)(struct radeon_device *rdev);
	int (*gart_set_page)(struct radeon_device *rdev, int i, uint64_t addr);
	int (*cp_init)(struct radeon_device *rdev, unsigned ring_size);
	void (*cp_fini)(struct radeon_device *rdev);
	void (*cp_disable)(struct radeon_device *rdev);
	void (*cp_commit)(struct radeon_device *rdev);
	void (*ring_start)(struct radeon_device *rdev);
	int (*ring_test)(struct radeon_device *rdev);
	void (*ring_ib_execute)(struct radeon_device *rdev, struct radeon_ib *ib);
	int (*ib_test)(struct radeon_device *rdev);
	int (*irq_set)(struct radeon_device *rdev);
	int (*irq_process)(struct radeon_device *rdev);
	u32 (*get_vblank_counter)(struct radeon_device *rdev, int crtc);
	void (*fence_ring_emit)(struct radeon_device *rdev, struct radeon_fence *fence);
	int (*cs_parse)(struct radeon_cs_parser *p);
	int (*copy_blit)(struct radeon_device *rdev,
			 uint64_t src_offset,
			 uint64_t dst_offset,
			 unsigned num_pages,
			 struct radeon_fence *fence);
	int (*copy_dma)(struct radeon_device *rdev,
			uint64_t src_offset,
			uint64_t dst_offset,
			unsigned num_pages,
			struct radeon_fence *fence);
	int (*copy)(struct radeon_device *rdev,
		    uint64_t src_offset,
		    uint64_t dst_offset,
		    unsigned num_pages,
		    struct radeon_fence *fence);
	void (*set_engine_clock)(struct radeon_device *rdev, uint32_t eng_clock);
	void (*set_memory_clock)(struct radeon_device *rdev, uint32_t mem_clock);
	void (*set_pcie_lanes)(struct radeon_device *rdev, int lanes);
	void (*set_clock_gating)(struct radeon_device *rdev, int enable);
	int (*set_surface_reg)(struct radeon_device *rdev, int reg,
			       uint32_t tiling_flags, uint32_t pitch,
			       uint32_t offset, uint32_t obj_size);
	int (*clear_surface_reg)(struct radeon_device *rdev, int reg);
	void (*bandwidth_update)(struct radeon_device *rdev);
};

/*
 * Asic structures
 */
struct r100_asic {
	const unsigned	*reg_safe_bm;
	unsigned	reg_safe_bm_size;
};

struct r300_asic {
	const unsigned	*reg_safe_bm;
	unsigned	reg_safe_bm_size;
};

struct r600_asic {
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
};

struct rv770_asic {
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
	unsigned sc_earlyz_tile_fifo_fize;
};

union radeon_asic_config {
	struct r300_asic	r300;
	struct r100_asic	r100;
	struct r600_asic	r600;
	struct rv770_asic	rv770;
};


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
int radeon_cs_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
int radeon_gem_set_tiling_ioctl(struct drm_device *dev, void *data,
				struct drm_file *filp);
int radeon_gem_get_tiling_ioctl(struct drm_device *dev, void *data,
				struct drm_file *filp);


/*
 * Core structure, functions and helpers.
 */
typedef uint32_t (*radeon_rreg_t)(struct radeon_device*, uint32_t);
typedef void (*radeon_wreg_t)(struct radeon_device*, uint32_t, uint32_t);

struct radeon_device {
	struct device			*dev;
	struct drm_device		*ddev;
	struct pci_dev			*pdev;
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
	struct radeon_object		*stollen_vga_memory;
	struct fb_info			*fbdev_info;
	struct radeon_object		*fbdev_robj;
	struct radeon_framebuffer	*fbdev_rfb;
	/* Register mmio */
	resource_size_t			rmmio_base;
	resource_size_t			rmmio_size;
	void				*rmmio;
	radeon_rreg_t			mc_rreg;
	radeon_wreg_t			mc_wreg;
	radeon_rreg_t			pll_rreg;
	radeon_wreg_t			pll_wreg;
	uint32_t                        pcie_reg_mask;
	radeon_rreg_t			pciep_rreg;
	radeon_wreg_t			pciep_wreg;
	struct radeon_clock             clock;
	struct radeon_mc		mc;
	struct radeon_gart		gart;
	struct radeon_mode_info		mode_info;
	struct radeon_scratch		scratch;
	struct radeon_mman		mman;
	struct radeon_fence_driver	fence_drv;
	struct radeon_cp		cp;
	struct radeon_ib_pool		ib_pool;
	struct radeon_irq		irq;
	struct radeon_asic		*asic;
	struct radeon_gem		gem;
	struct radeon_pm		pm;
	uint32_t			bios_scratch[RADEON_BIOS_NUM_SCRATCH];
	struct mutex			cs_mutex;
	struct radeon_wb		wb;
	struct radeon_dummy_page	dummy_page;
	bool				gpu_lockup;
	bool				shutdown;
	bool				suspend;
	bool				need_dma32;
	bool				new_init_path;
	bool				accel_working;
	struct radeon_surface_reg surface_regs[RADEON_GEM_MAX_SURFACES];
	const struct firmware *me_fw;	/* all family ME firmware */
	const struct firmware *pfp_fw;	/* r6/700 PFP firmware */
	struct r600_blit r600_blit;
};

int radeon_device_init(struct radeon_device *rdev,
		       struct drm_device *ddev,
		       struct pci_dev *pdev,
		       uint32_t flags);
void radeon_device_fini(struct radeon_device *rdev);
int radeon_gpu_wait_for_idle(struct radeon_device *rdev);

/* r600 blit */
int r600_blit_prepare_copy(struct radeon_device *rdev, int size_bytes);
void r600_blit_done_copy(struct radeon_device *rdev, struct radeon_fence *fence);
void r600_kms_blit_copy(struct radeon_device *rdev,
			u64 src_gpu_addr, u64 dst_gpu_addr,
			int size_bytes);

static inline uint32_t r100_mm_rreg(struct radeon_device *rdev, uint32_t reg)
{
	if (reg < 0x10000)
		return readl(((void __iomem *)rdev->rmmio) + reg);
	else {
		writel(reg, ((void __iomem *)rdev->rmmio) + RADEON_MM_INDEX);
		return readl(((void __iomem *)rdev->rmmio) + RADEON_MM_DATA);
	}
}

static inline void r100_mm_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v)
{
	if (reg < 0x10000)
		writel(v, ((void __iomem *)rdev->rmmio) + reg);
	else {
		writel(reg, ((void __iomem *)rdev->rmmio) + RADEON_MM_INDEX);
		writel(v, ((void __iomem *)rdev->rmmio) + RADEON_MM_DATA);
	}
}


/*
 * Registers read & write functions.
 */
#define RREG8(reg) readb(((void __iomem *)rdev->rmmio) + (reg))
#define WREG8(reg, v) writeb(v, ((void __iomem *)rdev->rmmio) + (reg))
#define RREG32(reg) r100_mm_rreg(rdev, (reg))
#define DREG32(reg) printk(KERN_INFO "REGISTER: " #reg " : 0x%08X\n", r100_mm_rreg(rdev, (reg)))
#define WREG32(reg, v) r100_mm_wreg(rdev, (reg), (v))
#define REG_SET(FIELD, v) (((v) << FIELD##_SHIFT) & FIELD##_MASK)
#define REG_GET(FIELD, v) (((v) << FIELD##_SHIFT) & FIELD##_MASK)
#define RREG32_PLL(reg) rdev->pll_rreg(rdev, (reg))
#define WREG32_PLL(reg, v) rdev->pll_wreg(rdev, (reg), (v))
#define RREG32_MC(reg) rdev->mc_rreg(rdev, (reg))
#define WREG32_MC(reg, v) rdev->mc_wreg(rdev, (reg), (v))
#define RREG32_PCIE(reg) rv370_pcie_rreg(rdev, (reg))
#define WREG32_PCIE(reg, v) rv370_pcie_wreg(rdev, (reg), (v))
#define WREG32_P(reg, val, mask)				\
	do {							\
		uint32_t tmp_ = RREG32(reg);			\
		tmp_ &= (mask);					\
		tmp_ |= ((val) & ~(mask));			\
		WREG32(reg, tmp_);				\
	} while (0)
#define WREG32_PLL_P(reg, val, mask)				\
	do {							\
		uint32_t tmp_ = RREG32_PLL(reg);		\
		tmp_ &= (mask);					\
		tmp_ |= ((val) & ~(mask));			\
		WREG32_PLL(reg, tmp_);				\
	} while (0)
#define DREG32_SYS(sqf, rdev, reg) seq_printf((sqf), #reg " : 0x%08X\n", r100_mm_rreg((rdev), (reg)))

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
#define ASIC_IS_AVIVO(rdev) ((rdev->family >= CHIP_RS600))
#define ASIC_IS_DCE3(rdev) ((rdev->family >= CHIP_RV620))
#define ASIC_IS_DCE32(rdev) ((rdev->family >= CHIP_RV730))


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
static inline void radeon_ring_write(struct radeon_device *rdev, uint32_t v)
{
#if DRM_DEBUG_CODE
	if (rdev->cp.count_dw <= 0) {
		DRM_ERROR("radeon: writting more dword to ring than expected !\n");
	}
#endif
	rdev->cp.ring[rdev->cp.wptr++] = v;
	rdev->cp.wptr &= rdev->cp.ptr_mask;
	rdev->cp.count_dw--;
	rdev->cp.ring_free_dw--;
}


/*
 * ASICs macro.
 */
#define radeon_init(rdev) (rdev)->asic->init((rdev))
#define radeon_fini(rdev) (rdev)->asic->fini((rdev))
#define radeon_resume(rdev) (rdev)->asic->resume((rdev))
#define radeon_suspend(rdev) (rdev)->asic->suspend((rdev))
#define radeon_cs_parse(p) rdev->asic->cs_parse((p))
#define radeon_errata(rdev) (rdev)->asic->errata((rdev))
#define radeon_vram_info(rdev) (rdev)->asic->vram_info((rdev))
#define radeon_vga_set_state(rdev, state) (rdev)->asic->vga_set_state((rdev), (state))
#define radeon_gpu_reset(rdev) (rdev)->asic->gpu_reset((rdev))
#define radeon_mc_init(rdev) (rdev)->asic->mc_init((rdev))
#define radeon_mc_fini(rdev) (rdev)->asic->mc_fini((rdev))
#define radeon_wb_init(rdev) (rdev)->asic->wb_init((rdev))
#define radeon_wb_fini(rdev) (rdev)->asic->wb_fini((rdev))
#define radeon_gpu_gart_init(rdev) (rdev)->asic->gart_init((rdev))
#define radeon_gpu_gart_fini(rdev) (rdev)->asic->gart_fini((rdev))
#define radeon_gart_enable(rdev) (rdev)->asic->gart_enable((rdev))
#define radeon_gart_disable(rdev) (rdev)->asic->gart_disable((rdev))
#define radeon_gart_tlb_flush(rdev) (rdev)->asic->gart_tlb_flush((rdev))
#define radeon_gart_set_page(rdev, i, p) (rdev)->asic->gart_set_page((rdev), (i), (p))
#define radeon_cp_init(rdev,rsize) (rdev)->asic->cp_init((rdev), (rsize))
#define radeon_cp_fini(rdev) (rdev)->asic->cp_fini((rdev))
#define radeon_cp_disable(rdev) (rdev)->asic->cp_disable((rdev))
#define radeon_cp_commit(rdev) (rdev)->asic->cp_commit((rdev))
#define radeon_ring_start(rdev) (rdev)->asic->ring_start((rdev))
#define radeon_ring_test(rdev) (rdev)->asic->ring_test((rdev))
#define radeon_ring_ib_execute(rdev, ib) (rdev)->asic->ring_ib_execute((rdev), (ib))
#define radeon_ib_test(rdev) (rdev)->asic->ib_test((rdev))
#define radeon_irq_set(rdev) (rdev)->asic->irq_set((rdev))
#define radeon_irq_process(rdev) (rdev)->asic->irq_process((rdev))
#define radeon_get_vblank_counter(rdev, crtc) (rdev)->asic->get_vblank_counter((rdev), (crtc))
#define radeon_fence_ring_emit(rdev, fence) (rdev)->asic->fence_ring_emit((rdev), (fence))
#define radeon_copy_blit(rdev, s, d, np, f) (rdev)->asic->copy_blit((rdev), (s), (d), (np), (f))
#define radeon_copy_dma(rdev, s, d, np, f) (rdev)->asic->copy_dma((rdev), (s), (d), (np), (f))
#define radeon_copy(rdev, s, d, np, f) (rdev)->asic->copy((rdev), (s), (d), (np), (f))
#define radeon_set_engine_clock(rdev, e) (rdev)->asic->set_engine_clock((rdev), (e))
#define radeon_set_memory_clock(rdev, e) (rdev)->asic->set_engine_clock((rdev), (e))
#define radeon_set_pcie_lanes(rdev, l) (rdev)->asic->set_pcie_lanes((rdev), (l))
#define radeon_set_clock_gating(rdev, e) (rdev)->asic->set_clock_gating((rdev), (e))
#define radeon_set_surface_reg(rdev, r, f, p, o, s) ((rdev)->asic->set_surface_reg((rdev), (r), (f), (p), (o), (s)))
#define radeon_clear_surface_reg(rdev, r) ((rdev)->asic->clear_surface_reg((rdev), (r)))
#define radeon_bandwidth_update(rdev) (rdev)->asic->bandwidth_update((rdev))

/* Common functions */
extern int radeon_gart_table_vram_pin(struct radeon_device *rdev);
extern int radeon_modeset_init(struct radeon_device *rdev);
extern void radeon_modeset_fini(struct radeon_device *rdev);
extern bool radeon_card_posted(struct radeon_device *rdev);
extern int radeon_clocks_init(struct radeon_device *rdev);
extern void radeon_clocks_fini(struct radeon_device *rdev);
extern void radeon_scratch_init(struct radeon_device *rdev);
extern void radeon_surface_init(struct radeon_device *rdev);
extern int radeon_cs_parser_init(struct radeon_cs_parser *p, void *data);
extern void radeon_atom_set_clock_gating(struct radeon_device *rdev, int enable);

/* r100,rv100,rs100,rv200,rs200,r200,rv250,rs300,rv280 */
struct r100_mc_save {
	u32	GENMO_WT;
	u32	CRTC_EXT_CNTL;
	u32	CRTC_GEN_CNTL;
	u32	CRTC2_GEN_CNTL;
	u32	CUR_OFFSET;
	u32	CUR2_OFFSET;
};
extern void r100_cp_disable(struct radeon_device *rdev);
extern int r100_cp_init(struct radeon_device *rdev, unsigned ring_size);
extern void r100_cp_fini(struct radeon_device *rdev);
extern void r100_pci_gart_tlb_flush(struct radeon_device *rdev);
extern int r100_pci_gart_init(struct radeon_device *rdev);
extern void r100_pci_gart_fini(struct radeon_device *rdev);
extern int r100_pci_gart_enable(struct radeon_device *rdev);
extern void r100_pci_gart_disable(struct radeon_device *rdev);
extern int r100_pci_gart_set_page(struct radeon_device *rdev, int i, uint64_t addr);
extern int r100_debugfs_mc_info_init(struct radeon_device *rdev);
extern int r100_gui_wait_for_idle(struct radeon_device *rdev);
extern void r100_ib_fini(struct radeon_device *rdev);
extern int r100_ib_init(struct radeon_device *rdev);
extern void r100_irq_disable(struct radeon_device *rdev);
extern int r100_irq_set(struct radeon_device *rdev);
extern void r100_mc_stop(struct radeon_device *rdev, struct r100_mc_save *save);
extern void r100_mc_resume(struct radeon_device *rdev, struct r100_mc_save *save);
extern void r100_vram_init_sizes(struct radeon_device *rdev);
extern void r100_wb_disable(struct radeon_device *rdev);
extern void r100_wb_fini(struct radeon_device *rdev);
extern int r100_wb_init(struct radeon_device *rdev);
extern void r100_hdp_reset(struct radeon_device *rdev);
extern int r100_rb2d_reset(struct radeon_device *rdev);
extern int r100_cp_reset(struct radeon_device *rdev);

/* r300,r350,rv350,rv370,rv380 */
extern void r300_set_reg_safe(struct radeon_device *rdev);
extern void r300_mc_program(struct radeon_device *rdev);
extern void r300_vram_info(struct radeon_device *rdev);
extern int rv370_pcie_gart_init(struct radeon_device *rdev);
extern void rv370_pcie_gart_fini(struct radeon_device *rdev);
extern int rv370_pcie_gart_enable(struct radeon_device *rdev);
extern void rv370_pcie_gart_disable(struct radeon_device *rdev);

/* r420,r423,rv410 */
extern int r420_mc_init(struct radeon_device *rdev);
extern u32 r420_mc_rreg(struct radeon_device *rdev, u32 reg);
extern void r420_mc_wreg(struct radeon_device *rdev, u32 reg, u32 v);
extern int r420_debugfs_pipes_info_init(struct radeon_device *rdev);
extern void r420_pipes_init(struct radeon_device *rdev);

/* rv515 */
struct rv515_mc_save {
	u32 d1vga_control;
	u32 d2vga_control;
	u32 vga_render_control;
	u32 vga_hdp_control;
	u32 d1crtc_control;
	u32 d2crtc_control;
};
extern void rv515_bandwidth_avivo_update(struct radeon_device *rdev);
extern void rv515_vga_render_disable(struct radeon_device *rdev);
extern void rv515_set_safe_registers(struct radeon_device *rdev);
extern void rv515_mc_stop(struct radeon_device *rdev, struct rv515_mc_save *save);
extern void rv515_mc_resume(struct radeon_device *rdev, struct rv515_mc_save *save);
extern void rv515_clock_startup(struct radeon_device *rdev);
extern void rv515_debugfs(struct radeon_device *rdev);
extern int rv515_suspend(struct radeon_device *rdev);

/* rs690, rs740 */
extern void rs690_line_buffer_adjust(struct radeon_device *rdev,
					struct drm_display_mode *mode1,
					struct drm_display_mode *mode2);

/* r600, rv610, rv630, rv620, rv635, rv670, rs780, rs880 */
extern bool r600_card_posted(struct radeon_device *rdev);
extern void r600_cp_stop(struct radeon_device *rdev);
extern void r600_ring_init(struct radeon_device *rdev, unsigned ring_size);
extern int r600_cp_resume(struct radeon_device *rdev);
extern int r600_count_pipe_bits(uint32_t val);
extern int r600_gart_clear_page(struct radeon_device *rdev, int i);
extern int r600_mc_wait_for_idle(struct radeon_device *rdev);
extern int r600_pcie_gart_init(struct radeon_device *rdev);
extern void r600_pcie_gart_tlb_flush(struct radeon_device *rdev);
extern int r600_ib_test(struct radeon_device *rdev);
extern int r600_ring_test(struct radeon_device *rdev);
extern int r600_wb_init(struct radeon_device *rdev);
extern void r600_wb_fini(struct radeon_device *rdev);
extern void r600_scratch_init(struct radeon_device *rdev);
extern int r600_blit_init(struct radeon_device *rdev);
extern void r600_blit_fini(struct radeon_device *rdev);
extern int r600_cp_init_microcode(struct radeon_device *rdev);
extern int r600_gpu_reset(struct radeon_device *rdev);

#endif
