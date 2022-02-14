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

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) "amdgpu: " fmt

#ifdef dev_fmt
#undef dev_fmt
#endif

#define dev_fmt(fmt) "amdgpu: " fmt

#include "amdgpu_ctx.h"

#include <linux/atomic.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <linux/rbtree.h>
#include <linux/hashtable.h>
#include <linux/dma-fence.h>
#include <linux/pci.h>
#include <linux/aer.h>

#include <drm/ttm/ttm_bo_api.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_execbuf_util.h>

#include <drm/amdgpu_drm.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>
#include <drm/gpu_scheduler.h>

#include <kgd_kfd_interface.h>
#include "dm_pp_interface.h"
#include "kgd_pp_interface.h"

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
#include "amdgpu_dpm.h"
#include "amdgpu_acp.h"
#include "amdgpu_uvd.h"
#include "amdgpu_vce.h"
#include "amdgpu_vcn.h"
#include "amdgpu_jpeg.h"
#include "amdgpu_mn.h"
#include "amdgpu_gmc.h"
#include "amdgpu_gfx.h"
#include "amdgpu_sdma.h"
#include "amdgpu_nbio.h"
#include "amdgpu_hdp.h"
#include "amdgpu_dm.h"
#include "amdgpu_virt.h"
#include "amdgpu_csa.h"
#include "amdgpu_gart.h"
#include "amdgpu_debugfs.h"
#include "amdgpu_job.h"
#include "amdgpu_bo_list.h"
#include "amdgpu_gem.h"
#include "amdgpu_doorbell.h"
#include "amdgpu_amdkfd.h"
#include "amdgpu_smu.h"
#include "amdgpu_discovery.h"
#include "amdgpu_mes.h"
#include "amdgpu_umc.h"
#include "amdgpu_mmhub.h"
#include "amdgpu_gfxhub.h"
#include "amdgpu_df.h"
#include "amdgpu_smuio.h"
#include "amdgpu_fdinfo.h"
#include "amdgpu_mca.h"

#define MAX_GPU_INSTANCE		16

struct amdgpu_gpu_instance
{
	struct amdgpu_device		*adev;
	int				mgpu_fan_enabled;
};

struct amdgpu_mgpu_info
{
	struct amdgpu_gpu_instance	gpu_ins[MAX_GPU_INSTANCE];
	struct mutex			mutex;
	uint32_t			num_gpu;
	uint32_t			num_dgpu;
	uint32_t			num_apu;

	/* delayed reset_func for XGMI configuration if necessary */
	struct delayed_work		delayed_reset_work;
	bool				pending_reset;
};

enum amdgpu_ss {
	AMDGPU_SS_DRV_LOAD,
	AMDGPU_SS_DEV_D0,
	AMDGPU_SS_DEV_D3,
	AMDGPU_SS_DRV_UNLOAD
};

struct amdgpu_watchdog_timer
{
	bool timeout_fatal_disable;
	uint32_t period; /* maxCycles = (1 << period), the number of cycles before a timeout */
};

#define AMDGPU_MAX_TIMEOUT_PARAM_LENGTH	256

/*
 * Modules parameters.
 */
extern int amdgpu_modeset;
extern int amdgpu_vram_limit;
extern int amdgpu_vis_vram_limit;
extern int amdgpu_gart_size;
extern int amdgpu_gtt_size;
extern int amdgpu_moverate;
extern int amdgpu_benchmarking;
extern int amdgpu_testing;
extern int amdgpu_audio;
extern int amdgpu_disp_priority;
extern int amdgpu_hw_i2c;
extern int amdgpu_pcie_gen2;
extern int amdgpu_msi;
extern char amdgpu_lockup_timeout[AMDGPU_MAX_TIMEOUT_PARAM_LENGTH];
extern int amdgpu_dpm;
extern int amdgpu_fw_load_type;
extern int amdgpu_aspm;
extern int amdgpu_runtime_pm;
extern uint amdgpu_ip_block_mask;
extern int amdgpu_bapm;
extern int amdgpu_deep_color;
extern int amdgpu_vm_size;
extern int amdgpu_vm_block_size;
extern int amdgpu_vm_fragment_size;
extern int amdgpu_vm_fault_stop;
extern int amdgpu_vm_debug;
extern int amdgpu_vm_update_mode;
extern int amdgpu_exp_hw_support;
extern int amdgpu_dc;
extern int amdgpu_sched_jobs;
extern int amdgpu_sched_hw_submission;
extern uint amdgpu_pcie_gen_cap;
extern uint amdgpu_pcie_lane_cap;
extern uint amdgpu_cg_mask;
extern uint amdgpu_pg_mask;
extern uint amdgpu_sdma_phase_quantum;
extern char *amdgpu_disable_cu;
extern char *amdgpu_virtual_display;
extern uint amdgpu_pp_feature_mask;
extern uint amdgpu_force_long_training;
extern int amdgpu_job_hang_limit;
extern int amdgpu_lbpw;
extern int amdgpu_compute_multipipe;
extern int amdgpu_gpu_recovery;
extern int amdgpu_emu_mode;
extern uint amdgpu_smu_memory_pool_size;
extern int amdgpu_smu_pptable_id;
extern uint amdgpu_dc_feature_mask;
extern uint amdgpu_freesync_vid_mode;
extern uint amdgpu_dc_debug_mask;
extern uint amdgpu_dm_abm_level;
extern int amdgpu_backlight;
extern struct amdgpu_mgpu_info mgpu_info;
extern int amdgpu_ras_enable;
extern uint amdgpu_ras_mask;
extern int amdgpu_bad_page_threshold;
extern bool amdgpu_ignore_bad_page_threshold;
extern struct amdgpu_watchdog_timer amdgpu_watchdog_timer;
extern int amdgpu_async_gfx_ring;
extern int amdgpu_mcbp;
extern int amdgpu_discovery;
extern int amdgpu_mes;
extern int amdgpu_noretry;
extern int amdgpu_force_asic_type;
extern int amdgpu_smartshift_bias;
#ifdef CONFIG_HSA_AMD
extern int sched_policy;
extern bool debug_evictions;
extern bool no_system_mem_limit;
#else
static const int __maybe_unused sched_policy = KFD_SCHED_POLICY_HWS;
static const bool __maybe_unused debug_evictions; /* = false */
static const bool __maybe_unused no_system_mem_limit;
#endif

extern int amdgpu_tmz;
extern int amdgpu_reset_method;

#ifdef CONFIG_DRM_AMDGPU_SI
extern int amdgpu_si_support;
#endif
#ifdef CONFIG_DRM_AMDGPU_CIK
extern int amdgpu_cik_support;
#endif
extern int amdgpu_num_kcq;

#define AMDGPU_VM_MAX_NUM_CTX			4096
#define AMDGPU_SG_THRESHOLD			(256*1024*1024)
#define AMDGPU_DEFAULT_GTT_SIZE_MB		3072ULL /* 3GB by default */
#define AMDGPU_WAIT_IDLE_TIMEOUT_IN_MS	        3000
#define AMDGPU_MAX_USEC_TIMEOUT			100000	/* 100 ms */
#define AMDGPU_FENCE_JIFFIES_TIMEOUT		(HZ / 2)
#define AMDGPU_DEBUGFS_MAX_COMPONENTS		32
#define AMDGPUFB_CONN_LIMIT			4
#define AMDGPU_BIOS_NUM_SCRATCH			16

#define AMDGPU_VBIOS_VGA_ALLOCATION		(9 * 1024 * 1024) /* reserve 8MB for vga emulator and 1 MB for FB */

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

/* max cursor sizes (in pixels) */
#define CIK_CURSOR_WIDTH 128
#define CIK_CURSOR_HEIGHT 128

/* smasrt shift bias level limits */
#define AMDGPU_SMARTSHIFT_MAX_BIAS (100)
#define AMDGPU_SMARTSHIFT_MIN_BIAS (-100)

struct amdgpu_device;
struct amdgpu_ib;
struct amdgpu_cs_parser;
struct amdgpu_job;
struct amdgpu_irq_src;
struct amdgpu_fpriv;
struct amdgpu_bo_va_mapping;
struct kfd_vm_fault_info;
struct amdgpu_hive_info;
struct amdgpu_reset_context;
struct amdgpu_reset_control;

enum amdgpu_cp_irq {
	AMDGPU_CP_IRQ_GFX_ME0_PIPE0_EOP = 0,
	AMDGPU_CP_IRQ_GFX_ME0_PIPE1_EOP,
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

enum amdgpu_thermal_irq {
	AMDGPU_THERMAL_IRQ_LOW_TO_HIGH = 0,
	AMDGPU_THERMAL_IRQ_HIGH_TO_LOW,

	AMDGPU_THERMAL_IRQ_LAST
};

enum amdgpu_kiq_irq {
	AMDGPU_CP_KIQ_IRQ_DRIVER0 = 0,
	AMDGPU_CP_KIQ_IRQ_LAST
};

#define MAX_KIQ_REG_WAIT       5000 /* in usecs, 5ms */
#define MAX_KIQ_REG_BAILOUT_INTERVAL   5 /* in msecs, 5ms */
#define MAX_KIQ_REG_TRY 1000

int amdgpu_device_ip_set_clockgating_state(void *dev,
					   enum amd_ip_block_type block_type,
					   enum amd_clockgating_state state);
int amdgpu_device_ip_set_powergating_state(void *dev,
					   enum amd_ip_block_type block_type,
					   enum amd_powergating_state state);
void amdgpu_device_ip_get_clockgating_state(struct amdgpu_device *adev,
					    u32 *flags);
int amdgpu_device_ip_wait_for_idle(struct amdgpu_device *adev,
				   enum amd_ip_block_type block_type);
bool amdgpu_device_ip_is_idle(struct amdgpu_device *adev,
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

#define HW_REV(_Major, _Minor, _Rev) \
	((((uint32_t) (_Major)) << 16) | ((uint32_t) (_Minor) << 8) | ((uint32_t) (_Rev)))

struct amdgpu_ip_block {
	struct amdgpu_ip_block_status status;
	const struct amdgpu_ip_block_version *version;
};

int amdgpu_device_ip_block_version_cmp(struct amdgpu_device *adev,
				       enum amd_ip_block_type type,
				       u32 major, u32 minor);

struct amdgpu_ip_block *
amdgpu_device_ip_get_ip_block(struct amdgpu_device *adev,
			      enum amd_ip_block_type type);

int amdgpu_device_ip_block_add(struct amdgpu_device *adev,
			       const struct amdgpu_ip_block_version *ip_block_version);

/*
 * BIOS.
 */
bool amdgpu_get_bios(struct amdgpu_device *adev);
bool amdgpu_read_bios(struct amdgpu_device *adev);

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

int amdgpu_fence_slab_init(void);
void amdgpu_fence_slab_fini(void);

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

extern const struct drm_sched_backend_ops amdgpu_sched_ops;

/*
 * file private structure
 */

struct amdgpu_fpriv {
	struct amdgpu_vm	vm;
	struct amdgpu_bo_va	*prt_va;
	struct amdgpu_bo_va	*csa_va;
	struct mutex		bo_list_lock;
	struct idr		bo_list_handles;
	struct amdgpu_ctx_mgr	ctx_mgr;
};

int amdgpu_file_to_fpriv(struct file *filp, struct amdgpu_fpriv **fpriv);

int amdgpu_ib_get(struct amdgpu_device *adev, struct amdgpu_vm *vm,
		  unsigned size,
		  enum amdgpu_ib_pool_type pool,
		  struct amdgpu_ib *ib);
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

struct amdgpu_cs_post_dep {
	struct drm_syncobj *syncobj;
	struct dma_fence_chain *chain;
	u64 point;
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
	struct drm_sched_entity	*entity;

	/* buffer objects */
	struct ww_acquire_ctx		ticket;
	struct amdgpu_bo_list		*bo_list;
	struct amdgpu_mn		*mn;
	struct amdgpu_bo_list_entry	vm_pd;
	struct list_head		validated;
	struct dma_fence		*fence;
	uint64_t			bytes_moved_threshold;
	uint64_t			bytes_moved_vis_threshold;
	uint64_t			bytes_moved;
	uint64_t			bytes_moved_vis;

	/* user fence */
	struct amdgpu_bo_list_entry	uf_entry;

	unsigned			num_post_deps;
	struct amdgpu_cs_post_dep	*post_deps;
};

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
#define AMDGPU_MAX_WB 256	/* Reserve at most 256 WB slots for amdgpu-owned rings. */

struct amdgpu_wb {
	struct amdgpu_bo	*wb_obj;
	volatile uint32_t	*wb;
	uint64_t		gpu_addr;
	u32			num_wb;	/* Number of wb slots actually reserved for amdgpu. */
	unsigned long		used[DIV_ROUND_UP(AMDGPU_MAX_WB, BITS_PER_LONG)];
};

int amdgpu_device_wb_get(struct amdgpu_device *adev, u32 *wb);
void amdgpu_device_wb_free(struct amdgpu_device *adev, u32 wb);

/*
 * Benchmarking
 */
void amdgpu_benchmark(struct amdgpu_device *adev, int test_number);


/*
 * Testing
 */
void amdgpu_test_moves(struct amdgpu_device *adev);

/*
 * ASIC specific register table accessible by UMD
 */
struct amdgpu_allowed_register_entry {
	uint32_t reg_offset;
	bool grbm_indexed;
};

enum amd_reset_method {
	AMD_RESET_METHOD_NONE = -1,
	AMD_RESET_METHOD_LEGACY = 0,
	AMD_RESET_METHOD_MODE0,
	AMD_RESET_METHOD_MODE1,
	AMD_RESET_METHOD_MODE2,
	AMD_RESET_METHOD_BACO,
	AMD_RESET_METHOD_PCI,
};

struct amdgpu_video_codec_info {
	u32 codec_type;
	u32 max_width;
	u32 max_height;
	u32 max_pixels_per_frame;
	u32 max_level;
};

#define codec_info_build(type, width, height, level) \
			 .codec_type = type,\
			 .max_width = width,\
			 .max_height = height,\
			 .max_pixels_per_frame = height * width,\
			 .max_level = level,

struct amdgpu_video_codecs {
	const u32 codec_count;
	const struct amdgpu_video_codec_info *codec_array;
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
	enum amd_reset_method (*reset_method)(struct amdgpu_device *adev);
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
	/* flush hdp write queue */
	void (*flush_hdp)(struct amdgpu_device *adev, struct amdgpu_ring *ring);
	/* invalidate hdp read cache */
	void (*invalidate_hdp)(struct amdgpu_device *adev,
			       struct amdgpu_ring *ring);
	/* check if the asic needs a full reset of if soft reset will work */
	bool (*need_full_reset)(struct amdgpu_device *adev);
	/* initialize doorbell layout for specific asic*/
	void (*init_doorbell_index)(struct amdgpu_device *adev);
	/* PCIe bandwidth usage */
	void (*get_pcie_usage)(struct amdgpu_device *adev, uint64_t *count0,
			       uint64_t *count1);
	/* do we need to reset the asic at init time (e.g., kexec) */
	bool (*need_reset_on_init)(struct amdgpu_device *adev);
	/* PCIe replay counter */
	uint64_t (*get_pcie_replay_count)(struct amdgpu_device *adev);
	/* device supports BACO */
	bool (*supports_baco)(struct amdgpu_device *adev);
	/* pre asic_init quirks */
	void (*pre_asic_init)(struct amdgpu_device *adev);
	/* enter/exit umd stable pstate */
	int (*update_umd_stable_pstate)(struct amdgpu_device *adev, bool enter);
	/* query video codecs */
	int (*query_video_codecs)(struct amdgpu_device *adev, bool encode,
				  const struct amdgpu_video_codecs **codecs);
};

/*
 * IOCTL.
 */
int amdgpu_bo_list_ioctl(struct drm_device *dev, void *data,
				struct drm_file *filp);

int amdgpu_cs_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
int amdgpu_cs_fence_to_handle_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *filp);
int amdgpu_cs_wait_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
int amdgpu_cs_wait_fences_ioctl(struct drm_device *dev, void *data,
				struct drm_file *filp);

/* VRAM scratch page for HDP bug, default vram page */
struct amdgpu_vram_scratch {
	struct amdgpu_bo		*robj;
	volatile uint32_t		*ptr;
	u64				gpu_addr;
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

typedef uint64_t (*amdgpu_rreg64_t)(struct amdgpu_device*, uint32_t);
typedef void (*amdgpu_wreg64_t)(struct amdgpu_device*, uint32_t, uint64_t);

typedef uint32_t (*amdgpu_block_rreg_t)(struct amdgpu_device*, uint32_t, uint32_t);
typedef void (*amdgpu_block_wreg_t)(struct amdgpu_device*, uint32_t, uint32_t, uint32_t);

struct amdgpu_mmio_remap {
	u32 reg_offset;
	resource_size_t bus_addr;
};

/* Define the HW IP blocks will be used in driver , add more if necessary */
enum amd_hw_ip_block_type {
	GC_HWIP = 1,
	HDP_HWIP,
	SDMA0_HWIP,
	SDMA1_HWIP,
	SDMA2_HWIP,
	SDMA3_HWIP,
	SDMA4_HWIP,
	SDMA5_HWIP,
	SDMA6_HWIP,
	SDMA7_HWIP,
	MMHUB_HWIP,
	ATHUB_HWIP,
	NBIO_HWIP,
	MP0_HWIP,
	MP1_HWIP,
	UVD_HWIP,
	VCN_HWIP = UVD_HWIP,
	JPEG_HWIP = VCN_HWIP,
	VCN1_HWIP,
	VCE_HWIP,
	DF_HWIP,
	DCE_HWIP,
	OSSSYS_HWIP,
	SMUIO_HWIP,
	PWR_HWIP,
	NBIF_HWIP,
	THM_HWIP,
	CLK_HWIP,
	UMC_HWIP,
	RSMU_HWIP,
	XGMI_HWIP,
	DCI_HWIP,
	MAX_HWIP
};

#define HWIP_MAX_INSTANCE	10

#define HW_ID_MAX		300
#define IP_VERSION(mj, mn, rv) (((mj) << 16) | ((mn) << 8) | (rv))

struct amd_powerplay {
	void *pp_handle;
	const struct amd_pm_funcs *pp_funcs;
};

/* polaris10 kickers */
#define ASICID_IS_P20(did, rid)		(((did == 0x67DF) && \
					 ((rid == 0xE3) || \
					  (rid == 0xE4) || \
					  (rid == 0xE5) || \
					  (rid == 0xE7) || \
					  (rid == 0xEF))) || \
					 ((did == 0x6FDF) && \
					 ((rid == 0xE7) || \
					  (rid == 0xEF) || \
					  (rid == 0xFF))))

#define ASICID_IS_P30(did, rid)		((did == 0x67DF) && \
					((rid == 0xE1) || \
					 (rid == 0xF7)))

/* polaris11 kickers */
#define ASICID_IS_P21(did, rid)		(((did == 0x67EF) && \
					 ((rid == 0xE0) || \
					  (rid == 0xE5))) || \
					 ((did == 0x67FF) && \
					 ((rid == 0xCF) || \
					  (rid == 0xEF) || \
					  (rid == 0xFF))))

#define ASICID_IS_P31(did, rid)		((did == 0x67EF) && \
					((rid == 0xE2)))

/* polaris12 kickers */
#define ASICID_IS_P23(did, rid)		(((did == 0x6987) && \
					 ((rid == 0xC0) || \
					  (rid == 0xC1) || \
					  (rid == 0xC3) || \
					  (rid == 0xC7))) || \
					 ((did == 0x6981) && \
					 ((rid == 0x00) || \
					  (rid == 0x01) || \
					  (rid == 0x10))))

#define AMDGPU_RESET_MAGIC_NUM 64
#define AMDGPU_MAX_DF_PERFMONS 4
#define AMDGPU_PRODUCT_NAME_LEN 64
struct amdgpu_device {
	struct device			*dev;
	struct pci_dev			*pdev;
	struct drm_device		ddev;

#ifdef CONFIG_DRM_AMD_ACP
	struct amdgpu_acp		acp;
#endif
	struct amdgpu_hive_info *hive;
	/* ASIC */
	enum amd_asic_type		asic_type;
	uint32_t			family;
	uint32_t			rev_id;
	uint32_t			external_rev_id;
	unsigned long			flags;
	unsigned long			apu_flags;
	int				usec_timeout;
	const struct amdgpu_asic_funcs	*asic_funcs;
	bool				shutdown;
	bool				need_swiotlb;
	bool				accel_working;
	struct notifier_block		acpi_nb;
	struct amdgpu_i2c_chan		*i2c_bus[AMDGPU_MAX_I2C_BUS];
	struct debugfs_blob_wrapper     debugfs_vbios_blob;
	struct debugfs_blob_wrapper     debugfs_discovery_blob;
	struct mutex			srbm_mutex;
	/* GRBM index mutex. Protects concurrent access to GRBM index */
	struct mutex                    grbm_idx_mutex;
	struct dev_pm_domain		vga_pm_domain;
	bool				have_disp_power_ref;
	bool                            have_atomics_support;

	/* BIOS */
	bool				is_atom_fw;
	uint8_t				*bios;
	uint32_t			bios_size;
	uint32_t			bios_scratch_reg_offset;
	uint32_t			bios_scratch[AMDGPU_BIOS_NUM_SCRATCH];

	/* Register/doorbell mmio */
	resource_size_t			rmmio_base;
	resource_size_t			rmmio_size;
	void __iomem			*rmmio;
	/* protects concurrent MM_INDEX/DATA based register access */
	spinlock_t mmio_idx_lock;
	struct amdgpu_mmio_remap        rmmio_remap;
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
	amdgpu_rreg64_t			pcie_rreg64;
	amdgpu_wreg64_t			pcie_wreg64;
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
	/* protects concurrent se_cac register access */
	spinlock_t se_cac_idx_lock;
	amdgpu_rreg_t			se_cac_rreg;
	amdgpu_wreg_t			se_cac_wreg;
	/* protects concurrent ENDPOINT (audio) register access */
	spinlock_t audio_endpt_idx_lock;
	amdgpu_block_rreg_t		audio_endpt_rreg;
	amdgpu_block_wreg_t		audio_endpt_wreg;
	struct amdgpu_doorbell		doorbell;

	/* clock/pll info */
	struct amdgpu_clock            clock;

	/* MC */
	struct amdgpu_gmc		gmc;
	struct amdgpu_gart		gart;
	dma_addr_t			dummy_page_addr;
	struct amdgpu_vm_manager	vm_manager;
	struct amdgpu_vmhub             vmhub[AMDGPU_MAX_VMHUBS];
	unsigned			num_vmhubs;

	/* memory management */
	struct amdgpu_mman		mman;
	struct amdgpu_vram_scratch	vram_scratch;
	struct amdgpu_wb		wb;
	atomic64_t			num_bytes_moved;
	atomic64_t			num_evictions;
	atomic64_t			num_vram_cpu_page_faults;
	atomic_t			gpu_reset_counter;
	atomic_t			vram_lost_counter;

	/* data for buffer migration throttling */
	struct {
		spinlock_t		lock;
		s64			last_update_us;
		s64			accum_us; /* accumulated microseconds */
		s64			accum_us_vis; /* for visible VRAM */
		u32			log2_max_MBps;
	} mm_stats;

	/* display */
	bool				enable_virtual_display;
	struct amdgpu_vkms_output       *amdgpu_vkms_output;
	struct amdgpu_mode_info		mode_info;
	/* For pre-DCE11. DCE11 and later are in "struct amdgpu_device->dm" */
	struct work_struct		hotplug_work;
	struct amdgpu_irq_src		crtc_irq;
	struct amdgpu_irq_src		vline0_irq;
	struct amdgpu_irq_src		vupdate_irq;
	struct amdgpu_irq_src		pageflip_irq;
	struct amdgpu_irq_src		hpd_irq;
	struct amdgpu_irq_src		dmub_trace_irq;
	struct amdgpu_irq_src		dmub_outbox_irq;

	/* rings */
	u64				fence_context;
	unsigned			num_rings;
	struct amdgpu_ring		*rings[AMDGPU_MAX_RINGS];
	bool				ib_pool_ready;
	struct amdgpu_sa_manager	ib_pools[AMDGPU_IB_POOL_MAX];
	struct amdgpu_sched		gpu_sched[AMDGPU_HW_IP_NUM][AMDGPU_RING_PRIO_MAX];

	/* interrupts */
	struct amdgpu_irq		irq;

	/* powerplay */
	struct amd_powerplay		powerplay;
	bool				pp_force_state_enabled;

	/* smu */
	struct smu_context		smu;

	/* dpm */
	struct amdgpu_pm		pm;
	u32				cg_flags;
	u32				pg_flags;

	/* nbio */
	struct amdgpu_nbio		nbio;

	/* hdp */
	struct amdgpu_hdp		hdp;

	/* smuio */
	struct amdgpu_smuio		smuio;

	/* mmhub */
	struct amdgpu_mmhub		mmhub;

	/* gfxhub */
	struct amdgpu_gfxhub		gfxhub;

	/* gfx */
	struct amdgpu_gfx		gfx;

	/* sdma */
	struct amdgpu_sdma		sdma;

	/* uvd */
	struct amdgpu_uvd		uvd;

	/* vce */
	struct amdgpu_vce		vce;

	/* vcn */
	struct amdgpu_vcn		vcn;

	/* jpeg */
	struct amdgpu_jpeg		jpeg;

	/* firmwares */
	struct amdgpu_firmware		firmware;

	/* PSP */
	struct psp_context		psp;

	/* GDS */
	struct amdgpu_gds		gds;

	/* KFD */
	struct amdgpu_kfd_dev		kfd;

	/* UMC */
	struct amdgpu_umc		umc;

	/* display related functionality */
	struct amdgpu_display_manager dm;

	/* mes */
	bool                            enable_mes;
	struct amdgpu_mes               mes;

	/* df */
	struct amdgpu_df                df;

	/* MCA */
	struct amdgpu_mca               mca;

	struct amdgpu_ip_block          ip_blocks[AMDGPU_MAX_IP_NUM];
	uint32_t		        harvest_ip_mask;
	int				num_ip_blocks;
	struct mutex	mn_lock;
	DECLARE_HASHTABLE(mn_hash, 7);

	/* tracking pinned memory */
	atomic64_t vram_pin_size;
	atomic64_t visible_pin_size;
	atomic64_t gart_pin_size;

	/* soc15 register offset based on ip, instance and  segment */
	uint32_t		*reg_offset[MAX_HWIP][HWIP_MAX_INSTANCE];

	/* delayed work_func for deferring clockgating during resume */
	struct delayed_work     delayed_init_work;

	struct amdgpu_virt	virt;

	/* link all shadow bo */
	struct list_head                shadow_list;
	struct mutex                    shadow_list_lock;

	/* record hw reset is performed */
	bool has_hw_reset;
	u8				reset_magic[AMDGPU_RESET_MAGIC_NUM];

	/* s3/s4 mask */
	bool                            in_suspend;
	bool				in_s3;
	bool				in_s4;
	bool				in_s0ix;

	atomic_t 			in_gpu_reset;
	enum pp_mp1_state               mp1_state;
	struct rw_semaphore reset_sem;
	struct amdgpu_doorbell_index doorbell_index;

	struct mutex			notifier_lock;

	int asic_reset_res;
	struct work_struct		xgmi_reset_work;
	struct list_head		reset_list;

	long				gfx_timeout;
	long				sdma_timeout;
	long				video_timeout;
	long				compute_timeout;

	uint64_t			unique_id;
	uint64_t	df_perfmon_config_assign_mask[AMDGPU_MAX_DF_PERFMONS];

	/* enable runtime pm on the device */
	bool                            runpm;
	bool                            in_runpm;
	bool                            has_pr3;
	bool                            is_fw_fb;

	bool                            pm_sysfs_en;
	bool                            ucode_sysfs_en;

	/* Chip product information */
	char				product_number[16];
	char				product_name[AMDGPU_PRODUCT_NAME_LEN];
	char				serial[20];

	atomic_t			throttling_logging_enabled;
	struct ratelimit_state		throttling_logging_rs;
	uint32_t                        ras_hw_enabled;
	uint32_t                        ras_enabled;

	bool                            no_hw_access;
	struct pci_saved_state          *pci_state;
	pci_channel_state_t		pci_channel_state;

	struct amdgpu_reset_control     *reset_cntl;
	uint32_t                        ip_versions[MAX_HWIP][HWIP_MAX_INSTANCE];

	bool				ram_is_direct_mapped;
};

static inline struct amdgpu_device *drm_to_adev(struct drm_device *ddev)
{
	return container_of(ddev, struct amdgpu_device, ddev);
}

static inline struct drm_device *adev_to_drm(struct amdgpu_device *adev)
{
	return &adev->ddev;
}

static inline struct amdgpu_device *amdgpu_ttm_adev(struct ttm_device *bdev)
{
	return container_of(bdev, struct amdgpu_device, mman.bdev);
}

int amdgpu_device_init(struct amdgpu_device *adev,
		       uint32_t flags);
void amdgpu_device_fini_hw(struct amdgpu_device *adev);
void amdgpu_device_fini_sw(struct amdgpu_device *adev);

int amdgpu_gpu_wait_for_idle(struct amdgpu_device *adev);

void amdgpu_device_mm_access(struct amdgpu_device *adev, loff_t pos,
			     void *buf, size_t size, bool write);
size_t amdgpu_device_aper_access(struct amdgpu_device *adev, loff_t pos,
				 void *buf, size_t size, bool write);

void amdgpu_device_vram_access(struct amdgpu_device *adev, loff_t pos,
			       void *buf, size_t size, bool write);
uint32_t amdgpu_device_rreg(struct amdgpu_device *adev,
			    uint32_t reg, uint32_t acc_flags);
void amdgpu_device_wreg(struct amdgpu_device *adev,
			uint32_t reg, uint32_t v,
			uint32_t acc_flags);
void amdgpu_mm_wreg_mmio_rlc(struct amdgpu_device *adev,
			     uint32_t reg, uint32_t v);
void amdgpu_mm_wreg8(struct amdgpu_device *adev, uint32_t offset, uint8_t value);
uint8_t amdgpu_mm_rreg8(struct amdgpu_device *adev, uint32_t offset);

u32 amdgpu_device_indirect_rreg(struct amdgpu_device *adev,
				u32 pcie_index, u32 pcie_data,
				u32 reg_addr);
u64 amdgpu_device_indirect_rreg64(struct amdgpu_device *adev,
				  u32 pcie_index, u32 pcie_data,
				  u32 reg_addr);
void amdgpu_device_indirect_wreg(struct amdgpu_device *adev,
				 u32 pcie_index, u32 pcie_data,
				 u32 reg_addr, u32 reg_data);
void amdgpu_device_indirect_wreg64(struct amdgpu_device *adev,
				   u32 pcie_index, u32 pcie_data,
				   u32 reg_addr, u64 reg_data);

bool amdgpu_device_asic_has_dc_support(enum amd_asic_type asic_type);
bool amdgpu_device_has_dc_support(struct amdgpu_device *adev);

int amdgpu_device_pre_asic_reset(struct amdgpu_device *adev,
				 struct amdgpu_reset_context *reset_context);

int amdgpu_do_asic_reset(struct list_head *device_list_handle,
			 struct amdgpu_reset_context *reset_context);

int emu_soc_asic_init(struct amdgpu_device *adev);

/*
 * Registers read & write functions.
 */
#define AMDGPU_REGS_NO_KIQ    (1<<1)
#define AMDGPU_REGS_RLC	(1<<2)

#define RREG32_NO_KIQ(reg) amdgpu_device_rreg(adev, (reg), AMDGPU_REGS_NO_KIQ)
#define WREG32_NO_KIQ(reg, v) amdgpu_device_wreg(adev, (reg), (v), AMDGPU_REGS_NO_KIQ)

#define RREG32_KIQ(reg) amdgpu_kiq_rreg(adev, (reg))
#define WREG32_KIQ(reg, v) amdgpu_kiq_wreg(adev, (reg), (v))

#define RREG8(reg) amdgpu_mm_rreg8(adev, (reg))
#define WREG8(reg, v) amdgpu_mm_wreg8(adev, (reg), (v))

#define RREG32(reg) amdgpu_device_rreg(adev, (reg), 0)
#define DREG32(reg) printk(KERN_INFO "REGISTER: " #reg " : 0x%08X\n", amdgpu_device_rreg(adev, (reg), 0))
#define WREG32(reg, v) amdgpu_device_wreg(adev, (reg), (v), 0)
#define REG_SET(FIELD, v) (((v) << FIELD##_SHIFT) & FIELD##_MASK)
#define REG_GET(FIELD, v) (((v) << FIELD##_SHIFT) & FIELD##_MASK)
#define RREG32_PCIE(reg) adev->pcie_rreg(adev, (reg))
#define WREG32_PCIE(reg, v) adev->pcie_wreg(adev, (reg), (v))
#define RREG32_PCIE_PORT(reg) adev->pciep_rreg(adev, (reg))
#define WREG32_PCIE_PORT(reg, v) adev->pciep_wreg(adev, (reg), (v))
#define RREG64_PCIE(reg) adev->pcie_rreg64(adev, (reg))
#define WREG64_PCIE(reg, v) adev->pcie_wreg64(adev, (reg), (v))
#define RREG32_SMC(reg) adev->smc_rreg(adev, (reg))
#define WREG32_SMC(reg, v) adev->smc_wreg(adev, (reg), (v))
#define RREG32_UVD_CTX(reg) adev->uvd_ctx_rreg(adev, (reg))
#define WREG32_UVD_CTX(reg, v) adev->uvd_ctx_wreg(adev, (reg), (v))
#define RREG32_DIDT(reg) adev->didt_rreg(adev, (reg))
#define WREG32_DIDT(reg, v) adev->didt_wreg(adev, (reg), (v))
#define RREG32_GC_CAC(reg) adev->gc_cac_rreg(adev, (reg))
#define WREG32_GC_CAC(reg, v) adev->gc_cac_wreg(adev, (reg), (v))
#define RREG32_SE_CAC(reg) adev->se_cac_rreg(adev, (reg))
#define WREG32_SE_CAC(reg, v) adev->se_cac_wreg(adev, (reg), (v))
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

#define WREG32_SMC_P(_Reg, _Val, _Mask)                         \
	do {                                                    \
		u32 tmp = RREG32_SMC(_Reg);                     \
		tmp &= (_Mask);                                 \
		tmp |= ((_Val) & ~(_Mask));                     \
		WREG32_SMC(_Reg, tmp);                          \
	} while (0)

#define DREG32_SYS(sqf, adev, reg) seq_printf((sqf), #reg " : 0x%08X\n", amdgpu_device_rreg((adev), (reg), false))

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
 * ASICs macro.
 */
#define amdgpu_asic_set_vga_state(adev, state) (adev)->asic_funcs->set_vga_state((adev), (state))
#define amdgpu_asic_reset(adev) (adev)->asic_funcs->reset((adev))
#define amdgpu_asic_reset_method(adev) (adev)->asic_funcs->reset_method((adev))
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
#define amdgpu_asic_flush_hdp(adev, r) \
	((adev)->asic_funcs->flush_hdp ? (adev)->asic_funcs->flush_hdp((adev), (r)) : (adev)->hdp.funcs->flush_hdp((adev), (r)))
#define amdgpu_asic_invalidate_hdp(adev, r) \
	((adev)->asic_funcs->invalidate_hdp ? (adev)->asic_funcs->invalidate_hdp((adev), (r)) : (adev)->hdp.funcs->invalidate_hdp((adev), (r)))
#define amdgpu_asic_need_full_reset(adev) (adev)->asic_funcs->need_full_reset((adev))
#define amdgpu_asic_init_doorbell_index(adev) (adev)->asic_funcs->init_doorbell_index((adev))
#define amdgpu_asic_get_pcie_usage(adev, cnt0, cnt1) ((adev)->asic_funcs->get_pcie_usage((adev), (cnt0), (cnt1)))
#define amdgpu_asic_need_reset_on_init(adev) (adev)->asic_funcs->need_reset_on_init((adev))
#define amdgpu_asic_get_pcie_replay_count(adev) ((adev)->asic_funcs->get_pcie_replay_count((adev)))
#define amdgpu_asic_supports_baco(adev) (adev)->asic_funcs->supports_baco((adev))
#define amdgpu_asic_pre_asic_init(adev) (adev)->asic_funcs->pre_asic_init((adev))
#define amdgpu_asic_update_umd_stable_pstate(adev, enter) \
	((adev)->asic_funcs->update_umd_stable_pstate ? (adev)->asic_funcs->update_umd_stable_pstate((adev), (enter)) : 0)
#define amdgpu_asic_query_video_codecs(adev, e, c) (adev)->asic_funcs->query_video_codecs((adev), (e), (c))

#define amdgpu_inc_vram_lost(adev) atomic_inc(&((adev)->vram_lost_counter));

#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))

/* Common functions */
bool amdgpu_device_has_job_running(struct amdgpu_device *adev);
bool amdgpu_device_should_recover_gpu(struct amdgpu_device *adev);
int amdgpu_device_gpu_recover(struct amdgpu_device *adev,
			      struct amdgpu_job* job);
void amdgpu_device_pci_config_reset(struct amdgpu_device *adev);
int amdgpu_device_pci_reset(struct amdgpu_device *adev);
bool amdgpu_device_need_post(struct amdgpu_device *adev);

void amdgpu_cs_report_moved_bytes(struct amdgpu_device *adev, u64 num_bytes,
				  u64 num_vis_bytes);
int amdgpu_device_resize_fb_bar(struct amdgpu_device *adev);
void amdgpu_device_program_register_sequence(struct amdgpu_device *adev,
					     const u32 *registers,
					     const u32 array_size);

int amdgpu_device_mode1_reset(struct amdgpu_device *adev);
bool amdgpu_device_supports_atpx(struct drm_device *dev);
bool amdgpu_device_supports_px(struct drm_device *dev);
bool amdgpu_device_supports_boco(struct drm_device *dev);
bool amdgpu_device_supports_smart_shift(struct drm_device *dev);
bool amdgpu_device_supports_baco(struct drm_device *dev);
bool amdgpu_device_is_peer_accessible(struct amdgpu_device *adev,
				      struct amdgpu_device *peer_adev);
int amdgpu_device_baco_enter(struct drm_device *dev);
int amdgpu_device_baco_exit(struct drm_device *dev);

void amdgpu_device_flush_hdp(struct amdgpu_device *adev,
		struct amdgpu_ring *ring);
void amdgpu_device_invalidate_hdp(struct amdgpu_device *adev,
		struct amdgpu_ring *ring);

void amdgpu_device_halt(struct amdgpu_device *adev);

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

#if defined(CONFIG_VGA_SWITCHEROO) && defined(CONFIG_ACPI)
void *amdgpu_atpx_get_dhandle(void);
#else
static inline void *amdgpu_atpx_get_dhandle(void) { return NULL; }
#endif

/*
 * KMS
 */
extern const struct drm_ioctl_desc amdgpu_ioctls_kms[];
extern const int amdgpu_max_kms_ioctl;

int amdgpu_driver_load_kms(struct amdgpu_device *adev, unsigned long flags);
void amdgpu_driver_unload_kms(struct drm_device *dev);
void amdgpu_driver_lastclose_kms(struct drm_device *dev);
int amdgpu_driver_open_kms(struct drm_device *dev, struct drm_file *file_priv);
void amdgpu_driver_postclose_kms(struct drm_device *dev,
				 struct drm_file *file_priv);
void amdgpu_driver_release_kms(struct drm_device *dev);

int amdgpu_device_ip_suspend(struct amdgpu_device *adev);
int amdgpu_device_suspend(struct drm_device *dev, bool fbcon);
int amdgpu_device_resume(struct drm_device *dev, bool fbcon);
u32 amdgpu_get_vblank_counter_kms(struct drm_crtc *crtc);
int amdgpu_enable_vblank_kms(struct drm_crtc *crtc);
void amdgpu_disable_vblank_kms(struct drm_crtc *crtc);
int amdgpu_info_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *filp);

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

/* ATCS Device/Driver State */
#define AMDGPU_ATCS_PSC_DEV_STATE_D0		0
#define AMDGPU_ATCS_PSC_DEV_STATE_D3_HOT	3
#define AMDGPU_ATCS_PSC_DRV_STATE_OPR		0
#define AMDGPU_ATCS_PSC_DRV_STATE_NOT_OPR	1

#if defined(CONFIG_ACPI)
int amdgpu_acpi_init(struct amdgpu_device *adev);
void amdgpu_acpi_fini(struct amdgpu_device *adev);
bool amdgpu_acpi_is_pcie_performance_request_supported(struct amdgpu_device *adev);
bool amdgpu_acpi_is_power_shift_control_supported(void);
int amdgpu_acpi_pcie_performance_request(struct amdgpu_device *adev,
						u8 perf_req, bool advertise);
int amdgpu_acpi_power_shift_control(struct amdgpu_device *adev,
				    u8 dev_state, bool drv_state);
int amdgpu_acpi_smart_shift_update(struct drm_device *dev, enum amdgpu_ss ss_state);
int amdgpu_acpi_pcie_notify_device_ready(struct amdgpu_device *adev);

void amdgpu_acpi_get_backlight_caps(struct amdgpu_dm_backlight_caps *caps);
void amdgpu_acpi_detect(void);
#else
static inline int amdgpu_acpi_init(struct amdgpu_device *adev) { return 0; }
static inline void amdgpu_acpi_fini(struct amdgpu_device *adev) { }
static inline void amdgpu_acpi_detect(void) { }
static inline bool amdgpu_acpi_is_power_shift_control_supported(void) { return false; }
static inline int amdgpu_acpi_power_shift_control(struct amdgpu_device *adev,
						  u8 dev_state, bool drv_state) { return 0; }
static inline int amdgpu_acpi_smart_shift_update(struct drm_device *dev,
						 enum amdgpu_ss ss_state) { return 0; }
#endif

#if defined(CONFIG_ACPI) && defined(CONFIG_SUSPEND)
bool amdgpu_acpi_is_s3_active(struct amdgpu_device *adev);
bool amdgpu_acpi_is_s0ix_active(struct amdgpu_device *adev);
#else
static inline bool amdgpu_acpi_is_s0ix_active(struct amdgpu_device *adev) { return false; }
static inline bool amdgpu_acpi_is_s3_active(struct amdgpu_device *adev) { return false; }
#endif

int amdgpu_cs_find_mapping(struct amdgpu_cs_parser *parser,
			   uint64_t addr, struct amdgpu_bo **bo,
			   struct amdgpu_bo_va_mapping **mapping);

#if defined(CONFIG_DRM_AMD_DC)
int amdgpu_dm_display_resume(struct amdgpu_device *adev );
#else
static inline int amdgpu_dm_display_resume(struct amdgpu_device *adev) { return 0; }
#endif


void amdgpu_register_gpu_instance(struct amdgpu_device *adev);
void amdgpu_unregister_gpu_instance(struct amdgpu_device *adev);

pci_ers_result_t amdgpu_pci_error_detected(struct pci_dev *pdev,
					   pci_channel_state_t state);
pci_ers_result_t amdgpu_pci_mmio_enabled(struct pci_dev *pdev);
pci_ers_result_t amdgpu_pci_slot_reset(struct pci_dev *pdev);
void amdgpu_pci_resume(struct pci_dev *pdev);

bool amdgpu_device_cache_pci_state(struct pci_dev *pdev);
bool amdgpu_device_load_pci_state(struct pci_dev *pdev);

bool amdgpu_device_skip_hw_access(struct amdgpu_device *adev);

int amdgpu_device_set_cg_state(struct amdgpu_device *adev,
			       enum amd_clockgating_state state);
int amdgpu_device_set_pg_state(struct amdgpu_device *adev,
			       enum amd_powergating_state state);

#include "amdgpu_object.h"

static inline bool amdgpu_is_tmz(struct amdgpu_device *adev)
{
       return adev->gmc.tmz_enabled;
}

static inline int amdgpu_in_reset(struct amdgpu_device *adev)
{
	return atomic_read(&adev->in_gpu_reset);
}
#endif
