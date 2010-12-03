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

#include <asm/atomic.h>
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

/*
 * Copy from radeon_drv.h so we don't have to include both and have conflicting
 * symbol;
 */
#define RADEON_MAX_USEC_TIMEOUT		100000	/* 100 ms */
#define RADEON_FENCE_JIFFIES_TIMEOUT	(HZ / 2)
/* RADEON_IB_POOL_SIZE must be a power of 2 */
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
#define ATRM_BIOS_PAGE 4096

#if defined(CONFIG_VGA_SWITCHEROO)
bool radeon_atrm_supported(struct pci_dev *pdev);
int radeon_atrm_get_bios_chunk(uint8_t *bios, int offset, int len);
#else
static inline bool radeon_atrm_supported(struct pci_dev *pdev)
{
	return false;
}

static inline int radeon_atrm_get_bios_chunk(uint8_t *bios, int offset, int len){
	return -EINVAL;
}
#endif
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
	uint32_t dp_extclk;
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
void radeon_atom_set_voltage(struct radeon_device *rdev, u16 level);
void rs690_pm_info(struct radeon_device *rdev);
extern u32 rv6xx_get_temp(struct radeon_device *rdev);
extern u32 rv770_get_temp(struct radeon_device *rdev);
extern u32 evergreen_get_temp(struct radeon_device *rdev);

/*
 * Fences.
 */
struct radeon_fence_driver {
	uint32_t			scratch_reg;
	atomic_t			seq;
	uint32_t			last_seq;
	unsigned long			last_jiffies;
	unsigned long			last_timeout;
	wait_queue_head_t		queue;
	rwlock_t			lock;
	struct list_head		created;
	struct list_head		emited;
	struct list_head		signaled;
	bool				initialized;
};

struct radeon_fence {
	struct radeon_device		*rdev;
	struct kref			kref;
	struct list_head		list;
	/* protected by radeon_fence.lock */
	uint32_t			seq;
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
	/* Constant after initialization */
	struct radeon_device		*rdev;
	struct drm_gem_object		*gobj;
};

struct radeon_bo_list {
	struct ttm_validate_buffer tv;
	struct radeon_bo	*bo;
	uint64_t		gpu_offset;
	unsigned		rdomain;
	unsigned		wdomain;
	u32			tiling_flags;
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
	struct radeon_bo		*robj;
	volatile uint32_t		*ptr;
};

union radeon_gart_table {
	struct radeon_gart_table_ram	ram;
	struct radeon_gart_table_vram	vram;
};

#define RADEON_GPU_PAGE_SIZE 4096
#define RADEON_GPU_PAGE_MASK (RADEON_GPU_PAGE_SIZE - 1)

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
	u64			visible_vram_size;
	u64			active_vram_size;
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
 * IRQS.
 */
struct radeon_irq {
	bool		installed;
	bool		sw_int;
	/* FIXME: use a define max crtc rather than hardcode it */
	bool		crtc_vblank_int[6];
	wait_queue_head_t	vblank_queue;
	/* FIXME: use defines for max hpd/dacs */
	bool            hpd[6];
	bool            gui_idle;
	bool            gui_idle_acked;
	wait_queue_head_t	idle_queue;
	/* FIXME: use defines for max HDMI blocks */
	bool		hdmi[2];
	spinlock_t sw_lock;
	int sw_refcount;
};

int radeon_irq_kms_init(struct radeon_device *rdev);
void radeon_irq_kms_fini(struct radeon_device *rdev);
void radeon_irq_kms_sw_irq_get(struct radeon_device *rdev);
void radeon_irq_kms_sw_irq_put(struct radeon_device *rdev);

/*
 * CP & ring.
 */
struct radeon_ib {
	struct list_head	list;
	unsigned		idx;
	uint64_t		gpu_addr;
	struct radeon_fence	*fence;
	uint32_t		*ptr;
	uint32_t		length_dw;
	bool			free;
};

/*
 * locking -
 * mutex protects scheduled_ibs, ready, alloc_bm
 */
struct radeon_ib_pool {
	struct mutex		mutex;
	struct radeon_bo	*robj;
	struct list_head	bogus_ib;
	struct radeon_ib	ibs[RADEON_IB_POOL_SIZE];
	bool			ready;
	unsigned		head_id;
};

struct radeon_cp {
	struct radeon_bo	*ring_obj;
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

/*
 * R6xx+ IH ring
 */
struct r600_ih {
	struct radeon_bo	*ring_obj;
	volatile uint32_t	*ring;
	unsigned		rptr;
	unsigned		wptr;
	unsigned		wptr_old;
	unsigned		ring_size;
	uint64_t		gpu_addr;
	uint32_t		ptr_mask;
	spinlock_t              lock;
	bool                    enabled;
};

struct r600_blit {
	struct mutex		mutex;
	struct radeon_bo	*shader_obj;
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
extern void radeon_ib_bogus_add(struct radeon_device *rdev, struct radeon_ib *ib);
/* Ring access between begin & end cannot sleep */
void radeon_ring_free_size(struct radeon_device *rdev);
int radeon_ring_alloc(struct radeon_device *rdev, unsigned ndw);
int radeon_ring_lock(struct radeon_device *rdev, unsigned ndw);
void radeon_ring_commit(struct radeon_device *rdev);
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
	struct radeon_bo		*robj;
	struct radeon_bo_list		lobj;
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
#define RADEON_WB_CP_RPTR_OFFSET 1024
#define R600_WB_IH_WPTR_OFFSET   2048
#define R600_WB_EVENT_OFFSET     3072

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

enum radeon_pm_method {
	PM_METHOD_PROFILE,
	PM_METHOD_DYNPM,
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
	POWER_STATE_TYPE_DEFAULT,
	POWER_STATE_TYPE_POWERSAVE,
	POWER_STATE_TYPE_BATTERY,
	POWER_STATE_TYPE_BALANCED,
	POWER_STATE_TYPE_PERFORMANCE,
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
	THERMAL_TYPE_RV6XX,
	THERMAL_TYPE_RV770,
	THERMAL_TYPE_EVERGREEN,
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
	u32 voltage;
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
	/* XXX: use a define for num clock modes */
	struct radeon_pm_clock_info clock_info[8];
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

struct radeon_pm {
	struct mutex		mutex;
	u32			active_crtcs;
	int			active_crtc_count;
	int			req_vblank;
	bool			vblank_sync;
	bool			gui_idle;
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
	/* XXX: use a define for num power modes */
	struct radeon_power_state power_state[8];
	/* number of valid power states */
	int                     num_power_states;
	int                     current_power_state_index;
	int                     current_clock_mode_index;
	int                     requested_power_state_index;
	int                     requested_clock_mode_index;
	int                     default_power_state_index;
	u32                     current_sclk;
	u32                     current_mclk;
	u32                     current_vddc;
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


/*
 * ASIC specific functions.
 */
struct radeon_asic {
	int (*init)(struct radeon_device *rdev);
	void (*fini)(struct radeon_device *rdev);
	int (*resume)(struct radeon_device *rdev);
	int (*suspend)(struct radeon_device *rdev);
	void (*vga_set_state)(struct radeon_device *rdev, bool state);
	bool (*gpu_is_lockup)(struct radeon_device *rdev);
	int (*asic_reset)(struct radeon_device *rdev);
	void (*gart_tlb_flush)(struct radeon_device *rdev);
	int (*gart_set_page)(struct radeon_device *rdev, int i, uint64_t addr);
	int (*cp_init)(struct radeon_device *rdev, unsigned ring_size);
	void (*cp_fini)(struct radeon_device *rdev);
	void (*cp_disable)(struct radeon_device *rdev);
	void (*cp_commit)(struct radeon_device *rdev);
	void (*ring_start)(struct radeon_device *rdev);
	int (*ring_test)(struct radeon_device *rdev);
	void (*ring_ib_execute)(struct radeon_device *rdev, struct radeon_ib *ib);
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
	uint32_t (*get_engine_clock)(struct radeon_device *rdev);
	void (*set_engine_clock)(struct radeon_device *rdev, uint32_t eng_clock);
	uint32_t (*get_memory_clock)(struct radeon_device *rdev);
	void (*set_memory_clock)(struct radeon_device *rdev, uint32_t mem_clock);
	int (*get_pcie_lanes)(struct radeon_device *rdev);
	void (*set_pcie_lanes)(struct radeon_device *rdev, int lanes);
	void (*set_clock_gating)(struct radeon_device *rdev, int enable);
	int (*set_surface_reg)(struct radeon_device *rdev, int reg,
			       uint32_t tiling_flags, uint32_t pitch,
			       uint32_t offset, uint32_t obj_size);
	void (*clear_surface_reg)(struct radeon_device *rdev, int reg);
	void (*bandwidth_update)(struct radeon_device *rdev);
	void (*hpd_init)(struct radeon_device *rdev);
	void (*hpd_fini)(struct radeon_device *rdev);
	bool (*hpd_sense)(struct radeon_device *rdev, enum radeon_hpd_id hpd);
	void (*hpd_set_polarity)(struct radeon_device *rdev, enum radeon_hpd_id hpd);
	/* ioctl hw specific callback. Some hw might want to perform special
	 * operation on specific ioctl. For instance on wait idle some hw
	 * might want to perform and HDP flush through MMIO as it seems that
	 * some R6XX/R7XX hw doesn't take HDP flush into account if programmed
	 * through ring.
	 */
	void (*ioctl_wait_idle)(struct radeon_device *rdev, struct radeon_bo *bo);
	bool (*gui_idle)(struct radeon_device *rdev);
	/* power management */
	void (*pm_misc)(struct radeon_device *rdev);
	void (*pm_prepare)(struct radeon_device *rdev);
	void (*pm_finish)(struct radeon_device *rdev);
	void (*pm_init_profile)(struct radeon_device *rdev);
	void (*pm_get_dynpm_state)(struct radeon_device *rdev);
};

/*
 * Asic structures
 */
struct r100_gpu_lockup {
	unsigned long	last_jiffies;
	u32		last_cp_rptr;
};

struct r100_asic {
	const unsigned		*reg_safe_bm;
	unsigned		reg_safe_bm_size;
	u32			hdp_cntl;
	struct r100_gpu_lockup	lockup;
};

struct r300_asic {
	const unsigned		*reg_safe_bm;
	unsigned		reg_safe_bm_size;
	u32			resync_scratch;
	u32			hdp_cntl;
	struct r100_gpu_lockup	lockup;
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
	struct r100_gpu_lockup	lockup;
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
	struct r100_gpu_lockup	lockup;
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
};

union radeon_asic_config {
	struct r300_asic	r300;
	struct r100_asic	r100;
	struct r600_asic	r600;
	struct rv770_asic	rv770;
	struct evergreen_asic	evergreen;
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
int radeon_cs_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
int radeon_gem_set_tiling_ioctl(struct drm_device *dev, void *data,
				struct drm_file *filp);
int radeon_gem_get_tiling_ioctl(struct drm_device *dev, void *data,
				struct drm_file *filp);

/* VRAM scratch page for HDP bug */
struct r700_vram_scratch {
	struct radeon_bo		*robj;
	volatile uint32_t		*ptr;
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
	void				*rmmio;
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
	bool				accel_working;
	struct radeon_surface_reg surface_regs[RADEON_GEM_MAX_SURFACES];
	const struct firmware *me_fw;	/* all family ME firmware */
	const struct firmware *pfp_fw;	/* r6/700 PFP firmware */
	const struct firmware *rlc_fw;	/* r6/700 RLC firmware */
	struct r600_blit r600_blit;
	struct r700_vram_scratch vram_scratch;
	int msi_enabled; /* msi enabled */
	struct r600_ih ih; /* r6/700 interrupt ring */
	struct workqueue_struct *wq;
	struct work_struct hotplug_work;
	int num_crtc; /* number of crtcs */
	struct mutex dc_hw_i2c_mutex; /* display controller hw i2c mutex */
	struct mutex vram_mutex;

	/* audio stuff */
	bool			audio_enabled;
	struct timer_list	audio_timer;
	int			audio_channels;
	int			audio_rate;
	int			audio_bits_per_sample;
	uint8_t			audio_status_bits;
	uint8_t			audio_category_code;

	bool powered_down;
	struct notifier_block acpi_nb;
	/* only one userspace can use Hyperz features at a time */
	struct drm_file *hyperz_filp;
	/* i2c buses */
	struct radeon_i2c_chan *i2c_bus[RADEON_MAX_I2C_BUS];
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
/* evergreen blit */
int evergreen_blit_prepare_copy(struct radeon_device *rdev, int size_bytes);
void evergreen_blit_done_copy(struct radeon_device *rdev, struct radeon_fence *fence);
void evergreen_kms_blit_copy(struct radeon_device *rdev,
			     u64 src_gpu_addr, u64 dst_gpu_addr,
			     int size_bytes);

static inline uint32_t r100_mm_rreg(struct radeon_device *rdev, uint32_t reg)
{
	if (reg < rdev->rmmio_size)
		return readl(((void __iomem *)rdev->rmmio) + reg);
	else {
		writel(reg, ((void __iomem *)rdev->rmmio) + RADEON_MM_INDEX);
		return readl(((void __iomem *)rdev->rmmio) + RADEON_MM_DATA);
	}
}

static inline void r100_mm_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v)
{
	if (reg < rdev->rmmio_size)
		writel(v, ((void __iomem *)rdev->rmmio) + reg);
	else {
		writel(reg, ((void __iomem *)rdev->rmmio) + RADEON_MM_INDEX);
		writel(v, ((void __iomem *)rdev->rmmio) + RADEON_MM_DATA);
	}
}

static inline u32 r100_io_rreg(struct radeon_device *rdev, u32 reg)
{
	if (reg < rdev->rio_mem_size)
		return ioread32(rdev->rio_mem + reg);
	else {
		iowrite32(reg, rdev->rio_mem + RADEON_MM_INDEX);
		return ioread32(rdev->rio_mem + RADEON_MM_DATA);
	}
}

static inline void r100_io_wreg(struct radeon_device *rdev, u32 reg, u32 v)
{
	if (reg < rdev->rio_mem_size)
		iowrite32(v, rdev->rio_mem + reg);
	else {
		iowrite32(reg, rdev->rio_mem + RADEON_MM_INDEX);
		iowrite32(v, rdev->rio_mem + RADEON_MM_DATA);
	}
}

/*
 * Cast helper
 */
#define to_radeon_fence(p) ((struct radeon_fence *)(p))

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
#define RREG32_PCIE_P(reg) rdev->pciep_rreg(rdev, (reg))
#define WREG32_PCIE_P(reg, v) rdev->pciep_wreg(rdev, (reg), (v))
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
#define RREG32_IO(reg) r100_io_rreg(rdev, (reg))
#define WREG32_IO(reg, v) r100_io_wreg(rdev, (reg), (v))

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
#define ASIC_IS_DCE2(rdev) ((rdev->family == CHIP_RS600)  ||	\
			    (rdev->family == CHIP_RS690)  ||	\
			    (rdev->family == CHIP_RS740)  ||	\
			    (rdev->family >= CHIP_R600))
#define ASIC_IS_DCE3(rdev) ((rdev->family >= CHIP_RV620))
#define ASIC_IS_DCE32(rdev) ((rdev->family >= CHIP_RV730))
#define ASIC_IS_DCE4(rdev) ((rdev->family >= CHIP_CEDAR))

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
#define radeon_vga_set_state(rdev, state) (rdev)->asic->vga_set_state((rdev), (state))
#define radeon_gpu_is_lockup(rdev) (rdev)->asic->gpu_is_lockup((rdev))
#define radeon_asic_reset(rdev) (rdev)->asic->asic_reset((rdev))
#define radeon_gart_tlb_flush(rdev) (rdev)->asic->gart_tlb_flush((rdev))
#define radeon_gart_set_page(rdev, i, p) (rdev)->asic->gart_set_page((rdev), (i), (p))
#define radeon_cp_commit(rdev) (rdev)->asic->cp_commit((rdev))
#define radeon_ring_start(rdev) (rdev)->asic->ring_start((rdev))
#define radeon_ring_test(rdev) (rdev)->asic->ring_test((rdev))
#define radeon_ring_ib_execute(rdev, ib) (rdev)->asic->ring_ib_execute((rdev), (ib))
#define radeon_irq_set(rdev) (rdev)->asic->irq_set((rdev))
#define radeon_irq_process(rdev) (rdev)->asic->irq_process((rdev))
#define radeon_get_vblank_counter(rdev, crtc) (rdev)->asic->get_vblank_counter((rdev), (crtc))
#define radeon_fence_ring_emit(rdev, fence) (rdev)->asic->fence_ring_emit((rdev), (fence))
#define radeon_copy_blit(rdev, s, d, np, f) (rdev)->asic->copy_blit((rdev), (s), (d), (np), (f))
#define radeon_copy_dma(rdev, s, d, np, f) (rdev)->asic->copy_dma((rdev), (s), (d), (np), (f))
#define radeon_copy(rdev, s, d, np, f) (rdev)->asic->copy((rdev), (s), (d), (np), (f))
#define radeon_get_engine_clock(rdev) (rdev)->asic->get_engine_clock((rdev))
#define radeon_set_engine_clock(rdev, e) (rdev)->asic->set_engine_clock((rdev), (e))
#define radeon_get_memory_clock(rdev) (rdev)->asic->get_memory_clock((rdev))
#define radeon_set_memory_clock(rdev, e) (rdev)->asic->set_memory_clock((rdev), (e))
#define radeon_get_pcie_lanes(rdev) (rdev)->asic->get_pcie_lanes((rdev))
#define radeon_set_pcie_lanes(rdev, l) (rdev)->asic->set_pcie_lanes((rdev), (l))
#define radeon_set_clock_gating(rdev, e) (rdev)->asic->set_clock_gating((rdev), (e))
#define radeon_set_surface_reg(rdev, r, f, p, o, s) ((rdev)->asic->set_surface_reg((rdev), (r), (f), (p), (o), (s)))
#define radeon_clear_surface_reg(rdev, r) ((rdev)->asic->clear_surface_reg((rdev), (r)))
#define radeon_bandwidth_update(rdev) (rdev)->asic->bandwidth_update((rdev))
#define radeon_hpd_init(rdev) (rdev)->asic->hpd_init((rdev))
#define radeon_hpd_fini(rdev) (rdev)->asic->hpd_fini((rdev))
#define radeon_hpd_sense(rdev, hpd) (rdev)->asic->hpd_sense((rdev), (hpd))
#define radeon_hpd_set_polarity(rdev, hpd) (rdev)->asic->hpd_set_polarity((rdev), (hpd))
#define radeon_gui_idle(rdev) (rdev)->asic->gui_idle((rdev))
#define radeon_pm_misc(rdev) (rdev)->asic->pm_misc((rdev))
#define radeon_pm_prepare(rdev) (rdev)->asic->pm_prepare((rdev))
#define radeon_pm_finish(rdev) (rdev)->asic->pm_finish((rdev))
#define radeon_pm_init_profile(rdev) (rdev)->asic->pm_init_profile((rdev))
#define radeon_pm_get_dynpm_state(rdev) (rdev)->asic->pm_get_dynpm_state((rdev))

/* Common functions */
/* AGP */
extern int radeon_gpu_reset(struct radeon_device *rdev);
extern void radeon_agp_disable(struct radeon_device *rdev);
extern int radeon_gart_table_vram_pin(struct radeon_device *rdev);
extern void radeon_gart_restore(struct radeon_device *rdev);
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

/* r100,rv100,rs100,rv200,rs200,r200,rv250,rs300,rv280 */
extern void r100_gpu_lockup_update(struct r100_gpu_lockup *lockup, struct radeon_cp *cp);
extern bool r100_gpu_cp_is_lockup(struct radeon_device *rdev, struct r100_gpu_lockup *lockup, struct radeon_cp *cp);

/* rv200,rv250,rv280 */
extern void r200_set_safe_registers(struct radeon_device *rdev);

/* r300,r350,rv350,rv370,rv380 */
extern void r300_set_reg_safe(struct radeon_device *rdev);
extern void r300_mc_program(struct radeon_device *rdev);
extern void r300_mc_init(struct radeon_device *rdev);
extern void r300_clock_startup(struct radeon_device *rdev);
extern int r300_mc_wait_for_idle(struct radeon_device *rdev);
extern int rv370_pcie_gart_init(struct radeon_device *rdev);
extern void rv370_pcie_gart_fini(struct radeon_device *rdev);
extern int rv370_pcie_gart_enable(struct radeon_device *rdev);
extern void rv370_pcie_gart_disable(struct radeon_device *rdev);

/* r420,r423,rv410 */
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

/* rs400 */
extern int rs400_gart_init(struct radeon_device *rdev);
extern int rs400_gart_enable(struct radeon_device *rdev);
extern void rs400_gart_adjust_size(struct radeon_device *rdev);
extern void rs400_gart_disable(struct radeon_device *rdev);
extern void rs400_gart_fini(struct radeon_device *rdev);

/* rs600 */
extern void rs600_set_safe_registers(struct radeon_device *rdev);
extern int rs600_irq_set(struct radeon_device *rdev);
extern void rs600_irq_disable(struct radeon_device *rdev);

/* rs690, rs740 */
extern void rs690_line_buffer_adjust(struct radeon_device *rdev,
					struct drm_display_mode *mode1,
					struct drm_display_mode *mode2);

/* r600, rv610, rv630, rv620, rv635, rv670, rs780, rs880 */
extern void r600_vram_gtt_location(struct radeon_device *rdev, struct radeon_mc *mc);
extern bool r600_card_posted(struct radeon_device *rdev);
extern void r600_cp_stop(struct radeon_device *rdev);
extern int r600_cp_start(struct radeon_device *rdev);
extern void r600_ring_init(struct radeon_device *rdev, unsigned ring_size);
extern int r600_cp_resume(struct radeon_device *rdev);
extern void r600_cp_fini(struct radeon_device *rdev);
extern int r600_count_pipe_bits(uint32_t val);
extern int r600_mc_wait_for_idle(struct radeon_device *rdev);
extern int r600_pcie_gart_init(struct radeon_device *rdev);
extern void r600_pcie_gart_tlb_flush(struct radeon_device *rdev);
extern int r600_ib_test(struct radeon_device *rdev);
extern int r600_ring_test(struct radeon_device *rdev);
extern void r600_scratch_init(struct radeon_device *rdev);
extern int r600_blit_init(struct radeon_device *rdev);
extern void r600_blit_fini(struct radeon_device *rdev);
extern int r600_init_microcode(struct radeon_device *rdev);
extern int r600_asic_reset(struct radeon_device *rdev);
/* r600 irq */
extern int r600_irq_init(struct radeon_device *rdev);
extern void r600_irq_fini(struct radeon_device *rdev);
extern void r600_ih_ring_init(struct radeon_device *rdev, unsigned ring_size);
extern int r600_irq_set(struct radeon_device *rdev);
extern void r600_irq_suspend(struct radeon_device *rdev);
extern void r600_disable_interrupts(struct radeon_device *rdev);
extern void r600_rlc_stop(struct radeon_device *rdev);
/* r600 audio */
extern int r600_audio_init(struct radeon_device *rdev);
extern int r600_audio_tmds_index(struct drm_encoder *encoder);
extern void r600_audio_set_clock(struct drm_encoder *encoder, int clock);
extern int r600_audio_channels(struct radeon_device *rdev);
extern int r600_audio_bits_per_sample(struct radeon_device *rdev);
extern int r600_audio_rate(struct radeon_device *rdev);
extern uint8_t r600_audio_status_bits(struct radeon_device *rdev);
extern uint8_t r600_audio_category_code(struct radeon_device *rdev);
extern void r600_audio_schedule_polling(struct radeon_device *rdev);
extern void r600_audio_enable_polling(struct drm_encoder *encoder);
extern void r600_audio_disable_polling(struct drm_encoder *encoder);
extern void r600_audio_fini(struct radeon_device *rdev);
extern void r600_hdmi_init(struct drm_encoder *encoder);
extern void r600_hdmi_enable(struct drm_encoder *encoder);
extern void r600_hdmi_disable(struct drm_encoder *encoder);
extern void r600_hdmi_setmode(struct drm_encoder *encoder, struct drm_display_mode *mode);
extern int r600_hdmi_buffer_status_changed(struct drm_encoder *encoder);
extern void r600_hdmi_update_audio_settings(struct drm_encoder *encoder);

extern void r700_cp_stop(struct radeon_device *rdev);
extern void r700_cp_fini(struct radeon_device *rdev);
extern void evergreen_disable_interrupt_state(struct radeon_device *rdev);
extern int evergreen_irq_set(struct radeon_device *rdev);
extern int evergreen_blit_init(struct radeon_device *rdev);
extern void evergreen_blit_fini(struct radeon_device *rdev);

/* radeon_acpi.c */ 
#if defined(CONFIG_ACPI) 
extern int radeon_acpi_init(struct radeon_device *rdev); 
#else 
static inline int radeon_acpi_init(struct radeon_device *rdev) { return 0; } 
#endif 

/* evergreen */
struct evergreen_mc_save {
	u32 vga_control[6];
	u32 vga_render_control;
	u32 vga_hdp_control;
	u32 crtc_control[6];
};

#include "radeon_object.h"

#endif
