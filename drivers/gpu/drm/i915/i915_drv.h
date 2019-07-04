/* i915_drv.h -- Private header for the I915 driver -*- linux-c -*-
 */
/*
 *
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _I915_DRV_H_
#define _I915_DRV_H_

#include <uapi/drm/i915_drm.h>
#include <uapi/drm/drm_fourcc.h>

#include <linux/io-mapping.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/backlight.h>
#include <linux/hash.h>
#include <linux/intel-iommu.h>
#include <linux/kref.h>
#include <linux/mm_types.h>
#include <linux/perf_event.h>
#include <linux/pm_qos.h>
#include <linux/reservation.h>
#include <linux/shmem_fs.h>
#include <linux/stackdepot.h>

#include <drm/intel-gtt.h>
#include <drm/drm_legacy.h> /* for struct drm_dma_handle */
#include <drm/drm_gem.h>
#include <drm/drm_auth.h>
#include <drm/drm_cache.h>
#include <drm/drm_util.h>
#include <drm/drm_dsc.h>
#include <drm/drm_atomic.h>
#include <drm/drm_connector.h>
#include <drm/i915_mei_hdcp_interface.h>

#include "i915_fixed.h"
#include "i915_params.h"
#include "i915_reg.h"
#include "i915_utils.h"

#include "display/intel_bios.h"
#include "display/intel_display.h"
#include "display/intel_display_power.h"
#include "display/intel_dpll_mgr.h"
#include "display/intel_frontbuffer.h"
#include "display/intel_opregion.h"

#include "gt/intel_lrc.h"
#include "gt/intel_engine.h"
#include "gt/intel_gt_types.h"
#include "gt/intel_workarounds.h"

#include "intel_device_info.h"
#include "intel_runtime_pm.h"
#include "intel_uc.h"
#include "intel_uncore.h"
#include "intel_wakeref.h"
#include "intel_wopcm.h"

#include "i915_gem.h"
#include "gem/i915_gem_context_types.h"
#include "i915_gem_fence_reg.h"
#include "i915_gem_gtt.h"
#include "i915_gpu_error.h"
#include "i915_request.h"
#include "i915_scheduler.h"
#include "gt/intel_timeline.h"
#include "i915_vma.h"

#include "intel_gvt.h"

/* General customization:
 */

#define DRIVER_NAME		"i915"
#define DRIVER_DESC		"Intel Graphics"
#define DRIVER_DATE		"20190619"
#define DRIVER_TIMESTAMP	1560947544

/* Use I915_STATE_WARN(x) and I915_STATE_WARN_ON() (rather than WARN() and
 * WARN_ON()) for hw state sanity checks to check for unexpected conditions
 * which may not necessarily be a user visible problem.  This will either
 * WARN() or DRM_ERROR() depending on the verbose_checks moduleparam, to
 * enable distros and users to tailor their preferred amount of i915 abrt
 * spam.
 */
#define I915_STATE_WARN(condition, format...) ({			\
	int __ret_warn_on = !!(condition);				\
	if (unlikely(__ret_warn_on))					\
		if (!WARN(i915_modparams.verbose_state_checks, format))	\
			DRM_ERROR(format);				\
	unlikely(__ret_warn_on);					\
})

#define I915_STATE_WARN_ON(x)						\
	I915_STATE_WARN((x), "%s", "WARN_ON(" __stringify(x) ")")

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG)

bool __i915_inject_load_failure(const char *func, int line);
#define i915_inject_load_failure() \
	__i915_inject_load_failure(__func__, __LINE__)

bool i915_error_injected(void);

#else

#define i915_inject_load_failure() false
#define i915_error_injected() false

#endif

#define i915_load_error(i915, fmt, ...)					 \
	__i915_printk(i915, i915_error_injected() ? KERN_DEBUG : KERN_ERR, \
		      fmt, ##__VA_ARGS__)

struct drm_i915_gem_object;

enum hpd_pin {
	HPD_NONE = 0,
	HPD_TV = HPD_NONE,     /* TV is known to be unreliable */
	HPD_CRT,
	HPD_SDVO_B,
	HPD_SDVO_C,
	HPD_PORT_A,
	HPD_PORT_B,
	HPD_PORT_C,
	HPD_PORT_D,
	HPD_PORT_E,
	HPD_PORT_F,
	HPD_NUM_PINS
};

#define for_each_hpd_pin(__pin) \
	for ((__pin) = (HPD_NONE + 1); (__pin) < HPD_NUM_PINS; (__pin)++)

/* Threshold == 5 for long IRQs, 50 for short */
#define HPD_STORM_DEFAULT_THRESHOLD 50

struct i915_hotplug {
	struct work_struct hotplug_work;

	struct {
		unsigned long last_jiffies;
		int count;
		enum {
			HPD_ENABLED = 0,
			HPD_DISABLED = 1,
			HPD_MARK_DISABLED = 2
		} state;
	} stats[HPD_NUM_PINS];
	u32 event_bits;
	struct delayed_work reenable_work;

	u32 long_port_mask;
	u32 short_port_mask;
	struct work_struct dig_port_work;

	struct work_struct poll_init_work;
	bool poll_enabled;

	unsigned int hpd_storm_threshold;
	/* Whether or not to count short HPD IRQs in HPD storms */
	u8 hpd_short_storm_enabled;

	/*
	 * if we get a HPD irq from DP and a HPD irq from non-DP
	 * the non-DP HPD could block the workqueue on a mode config
	 * mutex getting, that userspace may have taken. However
	 * userspace is waiting on the DP workqueue to run which is
	 * blocked behind the non-DP one.
	 */
	struct workqueue_struct *dp_wq;
};

#define I915_GEM_GPU_DOMAINS \
	(I915_GEM_DOMAIN_RENDER | \
	 I915_GEM_DOMAIN_SAMPLER | \
	 I915_GEM_DOMAIN_COMMAND | \
	 I915_GEM_DOMAIN_INSTRUCTION | \
	 I915_GEM_DOMAIN_VERTEX)

struct drm_i915_private;
struct i915_mm_struct;
struct i915_mmu_object;

struct drm_i915_file_private {
	struct drm_i915_private *dev_priv;
	struct drm_file *file;

	struct {
		spinlock_t lock;
		struct list_head request_list;
	} mm;

	struct idr context_idr;
	struct mutex context_idr_lock; /* guards context_idr */

	struct idr vm_idr;
	struct mutex vm_idr_lock; /* guards vm_idr */

	unsigned int bsd_engine;

/*
 * Every context ban increments per client ban score. Also
 * hangs in short succession increments ban score. If ban threshold
 * is reached, client is considered banned and submitting more work
 * will fail. This is a stop gap measure to limit the badly behaving
 * clients access to gpu. Note that unbannable contexts never increment
 * the client ban score.
 */
#define I915_CLIENT_SCORE_HANG_FAST	1
#define   I915_CLIENT_FAST_HANG_JIFFIES (60 * HZ)
#define I915_CLIENT_SCORE_CONTEXT_BAN   3
#define I915_CLIENT_SCORE_BANNED	9
	/** ban_score: Accumulated score of all ctx bans and fast hangs. */
	atomic_t ban_score;
	unsigned long hang_timestamp;
};

/* Interface history:
 *
 * 1.1: Original.
 * 1.2: Add Power Management
 * 1.3: Add vblank support
 * 1.4: Fix cmdbuffer path, add heap destroy
 * 1.5: Add vblank pipe configuration
 * 1.6: - New ioctl for scheduling buffer swaps on vertical blank
 *      - Support vertical blank on secondary display pipe
 */
#define DRIVER_MAJOR		1
#define DRIVER_MINOR		6
#define DRIVER_PATCHLEVEL	0

struct intel_overlay;
struct intel_overlay_error_state;

struct sdvo_device_mapping {
	u8 initialized;
	u8 dvo_port;
	u8 slave_addr;
	u8 dvo_wiring;
	u8 i2c_pin;
	u8 ddc_pin;
};

struct intel_connector;
struct intel_encoder;
struct intel_atomic_state;
struct intel_crtc_state;
struct intel_initial_plane_config;
struct intel_crtc;
struct intel_limit;
struct dpll;
struct intel_cdclk_state;

struct drm_i915_display_funcs {
	void (*get_cdclk)(struct drm_i915_private *dev_priv,
			  struct intel_cdclk_state *cdclk_state);
	void (*set_cdclk)(struct drm_i915_private *dev_priv,
			  const struct intel_cdclk_state *cdclk_state,
			  enum pipe pipe);
	int (*get_fifo_size)(struct drm_i915_private *dev_priv,
			     enum i9xx_plane_id i9xx_plane);
	int (*compute_pipe_wm)(struct intel_crtc_state *crtc_state);
	int (*compute_intermediate_wm)(struct intel_crtc_state *crtc_state);
	void (*initial_watermarks)(struct intel_atomic_state *state,
				   struct intel_crtc_state *crtc_state);
	void (*atomic_update_watermarks)(struct intel_atomic_state *state,
					 struct intel_crtc_state *crtc_state);
	void (*optimize_watermarks)(struct intel_atomic_state *state,
				    struct intel_crtc_state *crtc_state);
	int (*compute_global_watermarks)(struct intel_atomic_state *state);
	void (*update_wm)(struct intel_crtc *crtc);
	int (*modeset_calc_cdclk)(struct intel_atomic_state *state);
	/* Returns the active state of the crtc, and if the crtc is active,
	 * fills out the pipe-config with the hw state. */
	bool (*get_pipe_config)(struct intel_crtc *,
				struct intel_crtc_state *);
	void (*get_initial_plane_config)(struct intel_crtc *,
					 struct intel_initial_plane_config *);
	int (*crtc_compute_clock)(struct intel_crtc *crtc,
				  struct intel_crtc_state *crtc_state);
	void (*crtc_enable)(struct intel_crtc_state *pipe_config,
			    struct intel_atomic_state *old_state);
	void (*crtc_disable)(struct intel_crtc_state *old_crtc_state,
			     struct intel_atomic_state *old_state);
	void (*update_crtcs)(struct intel_atomic_state *state);
	void (*audio_codec_enable)(struct intel_encoder *encoder,
				   const struct intel_crtc_state *crtc_state,
				   const struct drm_connector_state *conn_state);
	void (*audio_codec_disable)(struct intel_encoder *encoder,
				    const struct intel_crtc_state *old_crtc_state,
				    const struct drm_connector_state *old_conn_state);
	void (*fdi_link_train)(struct intel_crtc *crtc,
			       const struct intel_crtc_state *crtc_state);
	void (*init_clock_gating)(struct drm_i915_private *dev_priv);
	void (*hpd_irq_setup)(struct drm_i915_private *dev_priv);
	/* clock updates for mode set */
	/* cursor updates */
	/* render clock increase/decrease */
	/* display clock increase/decrease */
	/* pll clock increase/decrease */

	int (*color_check)(struct intel_crtc_state *crtc_state);
	/*
	 * Program double buffered color management registers during
	 * vblank evasion. The registers should then latch during the
	 * next vblank start, alongside any other double buffered registers
	 * involved with the same commit.
	 */
	void (*color_commit)(const struct intel_crtc_state *crtc_state);
	/*
	 * Load LUTs (and other single buffered color management
	 * registers). Will (hopefully) be called during the vblank
	 * following the latching of any double buffered registers
	 * involved with the same commit.
	 */
	void (*load_luts)(const struct intel_crtc_state *crtc_state);
	void (*read_luts)(struct intel_crtc_state *crtc_state);
};

struct intel_csr {
	struct work_struct work;
	const char *fw_path;
	u32 required_version;
	u32 max_fw_size; /* bytes */
	u32 *dmc_payload;
	u32 dmc_fw_size; /* dwords */
	u32 version;
	u32 mmio_count;
	i915_reg_t mmioaddr[20];
	u32 mmiodata[20];
	u32 dc_state;
	u32 allowed_dc_mask;
	intel_wakeref_t wakeref;
};

enum i915_cache_level {
	I915_CACHE_NONE = 0,
	I915_CACHE_LLC, /* also used for snoopable memory on non-LLC */
	I915_CACHE_L3_LLC, /* gen7+, L3 sits between the domain specifc
			      caches, eg sampler/render caches, and the
			      large Last-Level-Cache. LLC is coherent with
			      the CPU, but L3 is only visible to the GPU. */
	I915_CACHE_WT, /* hsw:gt3e WriteThrough for scanouts */
};

#define I915_COLOR_UNEVICTABLE (-1) /* a non-vma sharing the address space */

struct intel_fbc {
	/* This is always the inner lock when overlapping with struct_mutex and
	 * it's the outer lock when overlapping with stolen_lock. */
	struct mutex lock;
	unsigned threshold;
	unsigned int possible_framebuffer_bits;
	unsigned int busy_bits;
	unsigned int visible_pipes_mask;
	struct intel_crtc *crtc;

	struct drm_mm_node compressed_fb;
	struct drm_mm_node *compressed_llb;

	bool false_color;

	bool enabled;
	bool active;
	bool flip_pending;

	bool underrun_detected;
	struct work_struct underrun_work;

	/*
	 * Due to the atomic rules we can't access some structures without the
	 * appropriate locking, so we cache information here in order to avoid
	 * these problems.
	 */
	struct intel_fbc_state_cache {
		struct i915_vma *vma;
		unsigned long flags;

		struct {
			unsigned int mode_flags;
			u32 hsw_bdw_pixel_rate;
		} crtc;

		struct {
			unsigned int rotation;
			int src_w;
			int src_h;
			bool visible;
			/*
			 * Display surface base address adjustement for
			 * pageflips. Note that on gen4+ this only adjusts up
			 * to a tile, offsets within a tile are handled in
			 * the hw itself (with the TILEOFF register).
			 */
			int adjusted_x;
			int adjusted_y;

			int y;

			u16 pixel_blend_mode;
		} plane;

		struct {
			const struct drm_format_info *format;
			unsigned int stride;
		} fb;
	} state_cache;

	/*
	 * This structure contains everything that's relevant to program the
	 * hardware registers. When we want to figure out if we need to disable
	 * and re-enable FBC for a new configuration we just check if there's
	 * something different in the struct. The genx_fbc_activate functions
	 * are supposed to read from it in order to program the registers.
	 */
	struct intel_fbc_reg_params {
		struct i915_vma *vma;
		unsigned long flags;

		struct {
			enum pipe pipe;
			enum i9xx_plane_id i9xx_plane;
			unsigned int fence_y_offset;
		} crtc;

		struct {
			const struct drm_format_info *format;
			unsigned int stride;
		} fb;

		int cfb_size;
		unsigned int gen9_wa_cfb_stride;
	} params;

	const char *no_fbc_reason;
};

/*
 * HIGH_RR is the highest eDP panel refresh rate read from EDID
 * LOW_RR is the lowest eDP panel refresh rate found from EDID
 * parsing for same resolution.
 */
enum drrs_refresh_rate_type {
	DRRS_HIGH_RR,
	DRRS_LOW_RR,
	DRRS_MAX_RR, /* RR count */
};

enum drrs_support_type {
	DRRS_NOT_SUPPORTED = 0,
	STATIC_DRRS_SUPPORT = 1,
	SEAMLESS_DRRS_SUPPORT = 2
};

struct intel_dp;
struct i915_drrs {
	struct mutex mutex;
	struct delayed_work work;
	struct intel_dp *dp;
	unsigned busy_frontbuffer_bits;
	enum drrs_refresh_rate_type refresh_rate_type;
	enum drrs_support_type type;
};

struct i915_psr {
	struct mutex lock;

#define I915_PSR_DEBUG_MODE_MASK	0x0f
#define I915_PSR_DEBUG_DEFAULT		0x00
#define I915_PSR_DEBUG_DISABLE		0x01
#define I915_PSR_DEBUG_ENABLE		0x02
#define I915_PSR_DEBUG_FORCE_PSR1	0x03
#define I915_PSR_DEBUG_IRQ		0x10

	u32 debug;
	bool sink_support;
	bool enabled;
	struct intel_dp *dp;
	enum pipe pipe;
	bool active;
	struct work_struct work;
	unsigned busy_frontbuffer_bits;
	bool sink_psr2_support;
	bool link_standby;
	bool colorimetry_support;
	bool psr2_enabled;
	u8 sink_sync_latency;
	ktime_t last_entry_attempt;
	ktime_t last_exit;
	bool sink_not_reliable;
	bool irq_aux_error;
	u16 su_x_granularity;
};

/*
 * Sorted by south display engine compatibility.
 * If the new PCH comes with a south display engine that is not
 * inherited from the latest item, please do not add it to the
 * end. Instead, add it right after its "parent" PCH.
 */
enum intel_pch {
	PCH_NOP = -1,	/* PCH without south display */
	PCH_NONE = 0,	/* No PCH present */
	PCH_IBX,	/* Ibexpeak PCH */
	PCH_CPT,	/* Cougarpoint/Pantherpoint PCH */
	PCH_LPT,	/* Lynxpoint/Wildcatpoint PCH */
	PCH_SPT,        /* Sunrisepoint/Kaby Lake PCH */
	PCH_CNP,        /* Cannon/Comet Lake PCH */
	PCH_ICP,	/* Ice Lake PCH */
	PCH_MCC,        /* Mule Creek Canyon PCH */
};

#define QUIRK_LVDS_SSC_DISABLE (1<<1)
#define QUIRK_INVERT_BRIGHTNESS (1<<2)
#define QUIRK_BACKLIGHT_PRESENT (1<<3)
#define QUIRK_PIN_SWIZZLED_PAGES (1<<5)
#define QUIRK_INCREASE_T12_DELAY (1<<6)
#define QUIRK_INCREASE_DDI_DISABLED_TIME (1<<7)

struct intel_fbdev;
struct intel_fbc_work;

struct intel_gmbus {
	struct i2c_adapter adapter;
#define GMBUS_FORCE_BIT_RETRY (1U << 31)
	u32 force_bit;
	u32 reg0;
	i915_reg_t gpio_reg;
	struct i2c_algo_bit_data bit_algo;
	struct drm_i915_private *dev_priv;
};

struct i915_suspend_saved_registers {
	u32 saveDSPARB;
	u32 saveFBC_CONTROL;
	u32 saveCACHE_MODE_0;
	u32 saveMI_ARB_STATE;
	u32 saveSWF0[16];
	u32 saveSWF1[16];
	u32 saveSWF3[3];
	u64 saveFENCE[I915_MAX_NUM_FENCES];
	u32 savePCH_PORT_HOTPLUG;
	u16 saveGCDGMBUS;
};

struct vlv_s0ix_state {
	/* GAM */
	u32 wr_watermark;
	u32 gfx_prio_ctrl;
	u32 arb_mode;
	u32 gfx_pend_tlb0;
	u32 gfx_pend_tlb1;
	u32 lra_limits[GEN7_LRA_LIMITS_REG_NUM];
	u32 media_max_req_count;
	u32 gfx_max_req_count;
	u32 render_hwsp;
	u32 ecochk;
	u32 bsd_hwsp;
	u32 blt_hwsp;
	u32 tlb_rd_addr;

	/* MBC */
	u32 g3dctl;
	u32 gsckgctl;
	u32 mbctl;

	/* GCP */
	u32 ucgctl1;
	u32 ucgctl3;
	u32 rcgctl1;
	u32 rcgctl2;
	u32 rstctl;
	u32 misccpctl;

	/* GPM */
	u32 gfxpause;
	u32 rpdeuhwtc;
	u32 rpdeuc;
	u32 ecobus;
	u32 pwrdwnupctl;
	u32 rp_down_timeout;
	u32 rp_deucsw;
	u32 rcubmabdtmr;
	u32 rcedata;
	u32 spare2gh;

	/* Display 1 CZ domain */
	u32 gt_imr;
	u32 gt_ier;
	u32 pm_imr;
	u32 pm_ier;
	u32 gt_scratch[GEN7_GT_SCRATCH_REG_NUM];

	/* GT SA CZ domain */
	u32 tilectl;
	u32 gt_fifoctl;
	u32 gtlc_wake_ctrl;
	u32 gtlc_survive;
	u32 pmwgicz;

	/* Display 2 CZ domain */
	u32 gu_ctl0;
	u32 gu_ctl1;
	u32 pcbr;
	u32 clock_gate_dis2;
};

struct intel_rps_ei {
	ktime_t ktime;
	u32 render_c0;
	u32 media_c0;
};

struct intel_rps {
	struct mutex lock; /* protects enabling and the worker */

	/*
	 * work, interrupts_enabled and pm_iir are protected by
	 * dev_priv->irq_lock
	 */
	struct work_struct work;
	bool interrupts_enabled;
	u32 pm_iir;

	/* PM interrupt bits that should never be masked */
	u32 pm_intrmsk_mbz;

	/* Frequencies are stored in potentially platform dependent multiples.
	 * In other words, *_freq needs to be multiplied by X to be interesting.
	 * Soft limits are those which are used for the dynamic reclocking done
	 * by the driver (raise frequencies under heavy loads, and lower for
	 * lighter loads). Hard limits are those imposed by the hardware.
	 *
	 * A distinction is made for overclocking, which is never enabled by
	 * default, and is considered to be above the hard limit if it's
	 * possible at all.
	 */
	u8 cur_freq;		/* Current frequency (cached, may not == HW) */
	u8 min_freq_softlimit;	/* Minimum frequency permitted by the driver */
	u8 max_freq_softlimit;	/* Max frequency permitted by the driver */
	u8 max_freq;		/* Maximum frequency, RP0 if not overclocking */
	u8 min_freq;		/* AKA RPn. Minimum frequency */
	u8 boost_freq;		/* Frequency to request when wait boosting */
	u8 idle_freq;		/* Frequency to request when we are idle */
	u8 efficient_freq;	/* AKA RPe. Pre-determined balanced frequency */
	u8 rp1_freq;		/* "less than" RP0 power/freqency */
	u8 rp0_freq;		/* Non-overclocked max frequency. */
	u16 gpll_ref_freq;	/* vlv/chv GPLL reference frequency */

	int last_adj;

	struct {
		struct mutex mutex;

		enum { LOW_POWER, BETWEEN, HIGH_POWER } mode;
		unsigned int interactive;

		u8 up_threshold; /* Current %busy required to uplock */
		u8 down_threshold; /* Current %busy required to downclock */
	} power;

	bool enabled;
	atomic_t num_waiters;
	atomic_t boosts;

	/* manual wa residency calculations */
	struct intel_rps_ei ei;
};

struct intel_rc6 {
	bool enabled;
	u64 prev_hw_residency[4];
	u64 cur_residency[4];
};

struct intel_llc_pstate {
	bool enabled;
};

struct intel_gen6_power_mgmt {
	struct intel_rps rps;
	struct intel_rc6 rc6;
	struct intel_llc_pstate llc_pstate;
};

/* defined intel_pm.c */
extern spinlock_t mchdev_lock;

struct intel_ilk_power_mgmt {
	u8 cur_delay;
	u8 min_delay;
	u8 max_delay;
	u8 fmax;
	u8 fstart;

	u64 last_count1;
	unsigned long last_time1;
	unsigned long chipset_power;
	u64 last_count2;
	u64 last_time2;
	unsigned long gfx_power;
	u8 corr;

	int c_m;
	int r_t;
};

#define MAX_L3_SLICES 2
struct intel_l3_parity {
	u32 *remap_info[MAX_L3_SLICES];
	struct work_struct error_work;
	int which_slice;
};

struct i915_gem_mm {
	/** Memory allocator for GTT stolen memory */
	struct drm_mm stolen;
	/** Protects the usage of the GTT stolen memory allocator. This is
	 * always the inner lock when overlapping with struct_mutex. */
	struct mutex stolen_lock;

	/* Protects bound_list/unbound_list and #drm_i915_gem_object.mm.link */
	spinlock_t obj_lock;

	/**
	 * List of objects which are purgeable.
	 */
	struct list_head purge_list;

	/**
	 * List of objects which have allocated pages and are shrinkable.
	 */
	struct list_head shrink_list;

	/**
	 * List of objects which are pending destruction.
	 */
	struct llist_head free_list;
	struct work_struct free_work;
	spinlock_t free_lock;
	/**
	 * Count of objects pending destructions. Used to skip needlessly
	 * waiting on an RCU barrier if no objects are waiting to be freed.
	 */
	atomic_t free_count;

	/**
	 * Small stash of WC pages
	 */
	struct pagestash wc_stash;

	/**
	 * tmpfs instance used for shmem backed objects
	 */
	struct vfsmount *gemfs;

	/** PPGTT used for aliasing the PPGTT with the GTT */
	struct i915_ppgtt *aliasing_ppgtt;

	struct notifier_block oom_notifier;
	struct notifier_block vmap_notifier;
	struct shrinker shrinker;

	/**
	 * Workqueue to fault in userptr pages, flushed by the execbuf
	 * when required but otherwise left to userspace to try again
	 * on EAGAIN.
	 */
	struct workqueue_struct *userptr_wq;

	u64 unordered_timeline;

	/* the indicator for dispatch video commands on two BSD rings */
	atomic_t bsd_engine_dispatch_index;

	/** Bit 6 swizzling required for X tiling */
	u32 bit_6_swizzle_x;
	/** Bit 6 swizzling required for Y tiling */
	u32 bit_6_swizzle_y;

	/* shrinker accounting, also useful for userland debugging */
	u64 shrink_memory;
	u32 shrink_count;
};

#define I915_IDLE_ENGINES_TIMEOUT (200) /* in ms */

#define I915_RESET_TIMEOUT (10 * HZ) /* 10s */
#define I915_FENCE_TIMEOUT (10 * HZ) /* 10s */

#define I915_ENGINE_DEAD_TIMEOUT  (4 * HZ)  /* Seqno, head and subunits dead */
#define I915_SEQNO_DEAD_TIMEOUT   (12 * HZ) /* Seqno dead with active head */

#define I915_ENGINE_WEDGED_TIMEOUT  (60 * HZ)  /* Reset but no recovery? */

struct ddi_vbt_port_info {
	/* Non-NULL if port present. */
	const struct child_device_config *child;

	int max_tmds_clock;

	/*
	 * This is an index in the HDMI/DVI DDI buffer translation table.
	 * The special value HDMI_LEVEL_SHIFT_UNKNOWN means the VBT didn't
	 * populate this field.
	 */
#define HDMI_LEVEL_SHIFT_UNKNOWN	0xff
	u8 hdmi_level_shift;

	u8 supports_dvi:1;
	u8 supports_hdmi:1;
	u8 supports_dp:1;
	u8 supports_edp:1;
	u8 supports_typec_usb:1;
	u8 supports_tbt:1;

	u8 alternate_aux_channel;
	u8 alternate_ddc_pin;

	u8 dp_boost_level;
	u8 hdmi_boost_level;
	int dp_max_link_rate;		/* 0 for not limited by VBT */
};

enum psr_lines_to_wait {
	PSR_0_LINES_TO_WAIT = 0,
	PSR_1_LINE_TO_WAIT,
	PSR_4_LINES_TO_WAIT,
	PSR_8_LINES_TO_WAIT
};

struct intel_vbt_data {
	struct drm_display_mode *lfp_lvds_vbt_mode; /* if any */
	struct drm_display_mode *sdvo_lvds_vbt_mode; /* if any */

	/* Feature bits */
	unsigned int int_tv_support:1;
	unsigned int lvds_dither:1;
	unsigned int int_crt_support:1;
	unsigned int lvds_use_ssc:1;
	unsigned int int_lvds_support:1;
	unsigned int display_clock_mode:1;
	unsigned int fdi_rx_polarity_inverted:1;
	unsigned int panel_type:4;
	int lvds_ssc_freq;
	unsigned int bios_lvds_val; /* initial [PCH_]LVDS reg val in VBIOS */
	enum drm_panel_orientation orientation;

	enum drrs_support_type drrs_type;

	struct {
		int rate;
		int lanes;
		int preemphasis;
		int vswing;
		bool low_vswing;
		bool initialized;
		int bpp;
		struct edp_power_seq pps;
	} edp;

	struct {
		bool enable;
		bool full_link;
		bool require_aux_wakeup;
		int idle_frames;
		enum psr_lines_to_wait lines_to_wait;
		int tp1_wakeup_time_us;
		int tp2_tp3_wakeup_time_us;
		int psr2_tp2_tp3_wakeup_time_us;
	} psr;

	struct {
		u16 pwm_freq_hz;
		bool present;
		bool active_low_pwm;
		u8 min_brightness;	/* min_brightness/255 of max */
		u8 controller;		/* brightness controller number */
		enum intel_backlight_type type;
	} backlight;

	/* MIPI DSI */
	struct {
		u16 panel_id;
		struct mipi_config *config;
		struct mipi_pps_data *pps;
		u16 bl_ports;
		u16 cabc_ports;
		u8 seq_version;
		u32 size;
		u8 *data;
		const u8 *sequence[MIPI_SEQ_MAX];
		u8 *deassert_seq; /* Used by fixup_mipi_sequences() */
		enum drm_panel_orientation orientation;
	} dsi;

	int crt_ddc_pin;

	int child_dev_num;
	struct child_device_config *child_dev;

	struct ddi_vbt_port_info ddi_port_info[I915_MAX_PORTS];
	struct sdvo_device_mapping sdvo_mappings[2];
};

enum intel_ddb_partitioning {
	INTEL_DDB_PART_1_2,
	INTEL_DDB_PART_5_6, /* IVB+ */
};

struct intel_wm_level {
	bool enable;
	u32 pri_val;
	u32 spr_val;
	u32 cur_val;
	u32 fbc_val;
};

struct ilk_wm_values {
	u32 wm_pipe[3];
	u32 wm_lp[3];
	u32 wm_lp_spr[3];
	u32 wm_linetime[3];
	bool enable_fbc_wm;
	enum intel_ddb_partitioning partitioning;
};

struct g4x_pipe_wm {
	u16 plane[I915_MAX_PLANES];
	u16 fbc;
};

struct g4x_sr_wm {
	u16 plane;
	u16 cursor;
	u16 fbc;
};

struct vlv_wm_ddl_values {
	u8 plane[I915_MAX_PLANES];
};

struct vlv_wm_values {
	struct g4x_pipe_wm pipe[3];
	struct g4x_sr_wm sr;
	struct vlv_wm_ddl_values ddl[3];
	u8 level;
	bool cxsr;
};

struct g4x_wm_values {
	struct g4x_pipe_wm pipe[2];
	struct g4x_sr_wm sr;
	struct g4x_sr_wm hpll;
	bool cxsr;
	bool hpll_en;
	bool fbc_en;
};

struct skl_ddb_entry {
	u16 start, end;	/* in number of blocks, 'end' is exclusive */
};

static inline u16 skl_ddb_entry_size(const struct skl_ddb_entry *entry)
{
	return entry->end - entry->start;
}

static inline bool skl_ddb_entry_equal(const struct skl_ddb_entry *e1,
				       const struct skl_ddb_entry *e2)
{
	if (e1->start == e2->start && e1->end == e2->end)
		return true;

	return false;
}

struct skl_ddb_allocation {
	u8 enabled_slices; /* GEN11 has configurable 2 slices */
};

struct skl_ddb_values {
	unsigned dirty_pipes;
	struct skl_ddb_allocation ddb;
};

struct skl_wm_level {
	u16 min_ddb_alloc;
	u16 plane_res_b;
	u8 plane_res_l;
	bool plane_en;
	bool ignore_lines;
};

/* Stores plane specific WM parameters */
struct skl_wm_params {
	bool x_tiled, y_tiled;
	bool rc_surface;
	bool is_planar;
	u32 width;
	u8 cpp;
	u32 plane_pixel_rate;
	u32 y_min_scanlines;
	u32 plane_bytes_per_line;
	uint_fixed_16_16_t plane_blocks_per_line;
	uint_fixed_16_16_t y_tile_minimum;
	u32 linetime_us;
	u32 dbuf_block_size;
};

enum intel_pipe_crc_source {
	INTEL_PIPE_CRC_SOURCE_NONE,
	INTEL_PIPE_CRC_SOURCE_PLANE1,
	INTEL_PIPE_CRC_SOURCE_PLANE2,
	INTEL_PIPE_CRC_SOURCE_PLANE3,
	INTEL_PIPE_CRC_SOURCE_PLANE4,
	INTEL_PIPE_CRC_SOURCE_PLANE5,
	INTEL_PIPE_CRC_SOURCE_PLANE6,
	INTEL_PIPE_CRC_SOURCE_PLANE7,
	INTEL_PIPE_CRC_SOURCE_PIPE,
	/* TV/DP on pre-gen5/vlv can't use the pipe source. */
	INTEL_PIPE_CRC_SOURCE_TV,
	INTEL_PIPE_CRC_SOURCE_DP_B,
	INTEL_PIPE_CRC_SOURCE_DP_C,
	INTEL_PIPE_CRC_SOURCE_DP_D,
	INTEL_PIPE_CRC_SOURCE_AUTO,
	INTEL_PIPE_CRC_SOURCE_MAX,
};

#define INTEL_PIPE_CRC_ENTRIES_NR	128
struct intel_pipe_crc {
	spinlock_t lock;
	int skipped;
	enum intel_pipe_crc_source source;
};

struct i915_frontbuffer_tracking {
	spinlock_t lock;

	/*
	 * Tracking bits for delayed frontbuffer flushing du to gpu activity or
	 * scheduled flips.
	 */
	unsigned busy_bits;
	unsigned flip_bits;
};

struct i915_virtual_gpu {
	bool active;
	u32 caps;
};

/* used in computing the new watermarks state */
struct intel_wm_config {
	unsigned int num_pipes_active;
	bool sprites_enabled;
	bool sprites_scaled;
};

struct i915_oa_format {
	u32 format;
	int size;
};

struct i915_oa_reg {
	i915_reg_t addr;
	u32 value;
};

struct i915_oa_config {
	char uuid[UUID_STRING_LEN + 1];
	int id;

	const struct i915_oa_reg *mux_regs;
	u32 mux_regs_len;
	const struct i915_oa_reg *b_counter_regs;
	u32 b_counter_regs_len;
	const struct i915_oa_reg *flex_regs;
	u32 flex_regs_len;

	struct attribute_group sysfs_metric;
	struct attribute *attrs[2];
	struct device_attribute sysfs_metric_id;

	atomic_t ref_count;
};

struct i915_perf_stream;

/**
 * struct i915_perf_stream_ops - the OPs to support a specific stream type
 */
struct i915_perf_stream_ops {
	/**
	 * @enable: Enables the collection of HW samples, either in response to
	 * `I915_PERF_IOCTL_ENABLE` or implicitly called when stream is opened
	 * without `I915_PERF_FLAG_DISABLED`.
	 */
	void (*enable)(struct i915_perf_stream *stream);

	/**
	 * @disable: Disables the collection of HW samples, either in response
	 * to `I915_PERF_IOCTL_DISABLE` or implicitly called before destroying
	 * the stream.
	 */
	void (*disable)(struct i915_perf_stream *stream);

	/**
	 * @poll_wait: Call poll_wait, passing a wait queue that will be woken
	 * once there is something ready to read() for the stream
	 */
	void (*poll_wait)(struct i915_perf_stream *stream,
			  struct file *file,
			  poll_table *wait);

	/**
	 * @wait_unlocked: For handling a blocking read, wait until there is
	 * something to ready to read() for the stream. E.g. wait on the same
	 * wait queue that would be passed to poll_wait().
	 */
	int (*wait_unlocked)(struct i915_perf_stream *stream);

	/**
	 * @read: Copy buffered metrics as records to userspace
	 * **buf**: the userspace, destination buffer
	 * **count**: the number of bytes to copy, requested by userspace
	 * **offset**: zero at the start of the read, updated as the read
	 * proceeds, it represents how many bytes have been copied so far and
	 * the buffer offset for copying the next record.
	 *
	 * Copy as many buffered i915 perf samples and records for this stream
	 * to userspace as will fit in the given buffer.
	 *
	 * Only write complete records; returning -%ENOSPC if there isn't room
	 * for a complete record.
	 *
	 * Return any error condition that results in a short read such as
	 * -%ENOSPC or -%EFAULT, even though these may be squashed before
	 * returning to userspace.
	 */
	int (*read)(struct i915_perf_stream *stream,
		    char __user *buf,
		    size_t count,
		    size_t *offset);

	/**
	 * @destroy: Cleanup any stream specific resources.
	 *
	 * The stream will always be disabled before this is called.
	 */
	void (*destroy)(struct i915_perf_stream *stream);
};

/**
 * struct i915_perf_stream - state for a single open stream FD
 */
struct i915_perf_stream {
	/**
	 * @dev_priv: i915 drm device
	 */
	struct drm_i915_private *dev_priv;

	/**
	 * @link: Links the stream into ``&drm_i915_private->streams``
	 */
	struct list_head link;

	/**
	 * @wakeref: As we keep the device awake while the perf stream is
	 * active, we track our runtime pm reference for later release.
	 */
	intel_wakeref_t wakeref;

	/**
	 * @sample_flags: Flags representing the `DRM_I915_PERF_PROP_SAMPLE_*`
	 * properties given when opening a stream, representing the contents
	 * of a single sample as read() by userspace.
	 */
	u32 sample_flags;

	/**
	 * @sample_size: Considering the configured contents of a sample
	 * combined with the required header size, this is the total size
	 * of a single sample record.
	 */
	int sample_size;

	/**
	 * @ctx: %NULL if measuring system-wide across all contexts or a
	 * specific context that is being monitored.
	 */
	struct i915_gem_context *ctx;

	/**
	 * @enabled: Whether the stream is currently enabled, considering
	 * whether the stream was opened in a disabled state and based
	 * on `I915_PERF_IOCTL_ENABLE` and `I915_PERF_IOCTL_DISABLE` calls.
	 */
	bool enabled;

	/**
	 * @ops: The callbacks providing the implementation of this specific
	 * type of configured stream.
	 */
	const struct i915_perf_stream_ops *ops;

	/**
	 * @oa_config: The OA configuration used by the stream.
	 */
	struct i915_oa_config *oa_config;
};

/**
 * struct i915_oa_ops - Gen specific implementation of an OA unit stream
 */
struct i915_oa_ops {
	/**
	 * @is_valid_b_counter_reg: Validates register's address for
	 * programming boolean counters for a particular platform.
	 */
	bool (*is_valid_b_counter_reg)(struct drm_i915_private *dev_priv,
				       u32 addr);

	/**
	 * @is_valid_mux_reg: Validates register's address for programming mux
	 * for a particular platform.
	 */
	bool (*is_valid_mux_reg)(struct drm_i915_private *dev_priv, u32 addr);

	/**
	 * @is_valid_flex_reg: Validates register's address for programming
	 * flex EU filtering for a particular platform.
	 */
	bool (*is_valid_flex_reg)(struct drm_i915_private *dev_priv, u32 addr);

	/**
	 * @enable_metric_set: Selects and applies any MUX configuration to set
	 * up the Boolean and Custom (B/C) counters that are part of the
	 * counter reports being sampled. May apply system constraints such as
	 * disabling EU clock gating as required.
	 */
	int (*enable_metric_set)(struct i915_perf_stream *stream);

	/**
	 * @disable_metric_set: Remove system constraints associated with using
	 * the OA unit.
	 */
	void (*disable_metric_set)(struct drm_i915_private *dev_priv);

	/**
	 * @oa_enable: Enable periodic sampling
	 */
	void (*oa_enable)(struct i915_perf_stream *stream);

	/**
	 * @oa_disable: Disable periodic sampling
	 */
	void (*oa_disable)(struct i915_perf_stream *stream);

	/**
	 * @read: Copy data from the circular OA buffer into a given userspace
	 * buffer.
	 */
	int (*read)(struct i915_perf_stream *stream,
		    char __user *buf,
		    size_t count,
		    size_t *offset);

	/**
	 * @oa_hw_tail_read: read the OA tail pointer register
	 *
	 * In particular this enables us to share all the fiddly code for
	 * handling the OA unit tail pointer race that affects multiple
	 * generations.
	 */
	u32 (*oa_hw_tail_read)(struct drm_i915_private *dev_priv);
};

struct intel_cdclk_state {
	unsigned int cdclk, vco, ref, bypass;
	u8 voltage_level;
};

struct drm_i915_private {
	struct drm_device drm;

	const struct intel_device_info __info; /* Use INTEL_INFO() to access. */
	struct intel_runtime_info __runtime; /* Use RUNTIME_INFO() to access. */
	struct intel_driver_caps caps;

	/**
	 * Data Stolen Memory - aka "i915 stolen memory" gives us the start and
	 * end of stolen which we can optionally use to create GEM objects
	 * backed by stolen memory. Note that stolen_usable_size tells us
	 * exactly how much of this we are actually allowed to use, given that
	 * some portion of it is in fact reserved for use by hardware functions.
	 */
	struct resource dsm;
	/**
	 * Reseved portion of Data Stolen Memory
	 */
	struct resource dsm_reserved;

	/*
	 * Stolen memory is segmented in hardware with different portions
	 * offlimits to certain functions.
	 *
	 * The drm_mm is initialised to the total accessible range, as found
	 * from the PCI config. On Broadwell+, this is further restricted to
	 * avoid the first page! The upper end of stolen memory is reserved for
	 * hardware functions and similarly removed from the accessible range.
	 */
	resource_size_t stolen_usable_size;	/* Total size minus reserved ranges */

	struct intel_uncore uncore;

	struct i915_virtual_gpu vgpu;

	struct intel_gvt *gvt;

	struct intel_wopcm wopcm;

	struct intel_huc huc;
	struct intel_guc guc;

	struct intel_csr csr;

	struct intel_gmbus gmbus[GMBUS_NUM_PINS];

	/** gmbus_mutex protects against concurrent usage of the single hw gmbus
	 * controller on different i2c buses. */
	struct mutex gmbus_mutex;

	/**
	 * Base address of where the gmbus and gpio blocks are located (either
	 * on PCH or on SoC for platforms without PCH).
	 */
	u32 gpio_mmio_base;

	/* MMIO base address for MIPI regs */
	u32 mipi_mmio_base;

	u32 psr_mmio_base;

	u32 pps_mmio_base;

	wait_queue_head_t gmbus_wait_queue;

	struct pci_dev *bridge_dev;
	struct intel_engine_cs *engine[I915_NUM_ENGINES];
	/* Context used internally to idle the GPU and setup initial state */
	struct i915_gem_context *kernel_context;
	/* Context only to be used for injecting preemption commands */
	struct i915_gem_context *preempt_context;
	struct intel_engine_cs *engine_class[MAX_ENGINE_CLASS + 1]
					    [MAX_ENGINE_INSTANCE + 1];

	struct resource mch_res;

	/* protects the irq masks */
	spinlock_t irq_lock;

	bool display_irqs_enabled;

	/* To control wakeup latency, e.g. for irq-driven dp aux transfers. */
	struct pm_qos_request pm_qos;

	/* Sideband mailbox protection */
	struct mutex sb_lock;
	struct pm_qos_request sb_qos;

	/** Cached value of IMR to avoid reads in updating the bitfield */
	union {
		u32 irq_mask;
		u32 de_irq_mask[I915_MAX_PIPES];
	};
	u32 gt_irq_mask;
	u32 pm_rps_events;
	u32 pm_guc_events;
	u32 pipestat_irq_mask[I915_MAX_PIPES];

	struct i915_hotplug hotplug;
	struct intel_fbc fbc;
	struct i915_drrs drrs;
	struct intel_opregion opregion;
	struct intel_vbt_data vbt;

	bool preserve_bios_swizzle;

	/* overlay */
	struct intel_overlay *overlay;

	/* backlight registers and fields in struct intel_panel */
	struct mutex backlight_lock;

	/* LVDS info */
	bool no_aux_handshake;

	/* protects panel power sequencer state */
	struct mutex pps_mutex;

	unsigned int fsb_freq, mem_freq, is_ddr3;
	unsigned int skl_preferred_vco_freq;
	unsigned int max_cdclk_freq;

	unsigned int max_dotclk_freq;
	unsigned int rawclk_freq;
	unsigned int hpll_freq;
	unsigned int fdi_pll_freq;
	unsigned int czclk_freq;

	struct {
		/*
		 * The current logical cdclk state.
		 * See intel_atomic_state.cdclk.logical
		 *
		 * For reading holding any crtc lock is sufficient,
		 * for writing must hold all of them.
		 */
		struct intel_cdclk_state logical;
		/*
		 * The current actual cdclk state.
		 * See intel_atomic_state.cdclk.actual
		 */
		struct intel_cdclk_state actual;
		/* The current hardware cdclk state */
		struct intel_cdclk_state hw;

		int force_min_cdclk;
	} cdclk;

	/**
	 * wq - Driver workqueue for GEM.
	 *
	 * NOTE: Work items scheduled here are not allowed to grab any modeset
	 * locks, for otherwise the flushing done in the pageflip code will
	 * result in deadlocks.
	 */
	struct workqueue_struct *wq;

	/* ordered wq for modesets */
	struct workqueue_struct *modeset_wq;

	/* Display functions */
	struct drm_i915_display_funcs display;

	/* PCH chipset type */
	enum intel_pch pch_type;
	unsigned short pch_id;

	unsigned long quirks;

	struct drm_atomic_state *modeset_restore_state;
	struct drm_modeset_acquire_ctx reset_ctx;

	struct i915_ggtt ggtt; /* VM representing the global address space */

	struct i915_gem_mm mm;
	DECLARE_HASHTABLE(mm_structs, 7);
	struct mutex mm_lock;

	/* Kernel Modesetting */

	struct intel_crtc *plane_to_crtc_mapping[I915_MAX_PIPES];
	struct intel_crtc *pipe_to_crtc_mapping[I915_MAX_PIPES];

#ifdef CONFIG_DEBUG_FS
	struct intel_pipe_crc pipe_crc[I915_MAX_PIPES];
#endif

	/* dpll and cdclk state is protected by connection_mutex */
	int num_shared_dpll;
	struct intel_shared_dpll shared_dplls[I915_NUM_PLLS];
	const struct intel_dpll_mgr *dpll_mgr;

	/*
	 * dpll_lock serializes intel_{prepare,enable,disable}_shared_dpll.
	 * Must be global rather than per dpll, because on some platforms
	 * plls share registers.
	 */
	struct mutex dpll_lock;

	unsigned int active_crtcs;
	/* minimum acceptable cdclk for each pipe */
	int min_cdclk[I915_MAX_PIPES];
	/* minimum acceptable voltage level for each pipe */
	u8 min_voltage_level[I915_MAX_PIPES];

	int dpio_phy_iosf_port[I915_NUM_PHYS_VLV];

	struct i915_wa_list gt_wa_list;

	struct i915_frontbuffer_tracking fb_tracking;

	struct intel_atomic_helper {
		struct llist_head free_list;
		struct work_struct free_work;
	} atomic_helper;

	u16 orig_clock;

	bool mchbar_need_disable;

	struct intel_l3_parity l3_parity;

	/*
	 * edram size in MB.
	 * Cannot be determined by PCIID. You must always read a register.
	 */
	u32 edram_size_mb;

	/* gen6+ GT PM state */
	struct intel_gen6_power_mgmt gt_pm;

	/* ilk-only ips/rps state. Everything in here is protected by the global
	 * mchdev_lock in intel_pm.c */
	struct intel_ilk_power_mgmt ips;

	struct i915_power_domains power_domains;

	struct i915_psr psr;

	struct i915_gpu_error gpu_error;

	struct drm_i915_gem_object *vlv_pctx;

	/* list of fbdev register on this device */
	struct intel_fbdev *fbdev;
	struct work_struct fbdev_suspend_work;

	struct drm_property *broadcast_rgb_property;
	struct drm_property *force_audio_property;

	/* hda/i915 audio component */
	struct i915_audio_component *audio_component;
	bool audio_component_registered;
	/**
	 * av_mutex - mutex for audio/video sync
	 *
	 */
	struct mutex av_mutex;
	int audio_power_refcount;

	struct {
		struct mutex mutex;
		struct list_head list;
		struct llist_head free_list;
		struct work_struct free_work;

		/* The hw wants to have a stable context identifier for the
		 * lifetime of the context (for OA, PASID, faults, etc).
		 * This is limited in execlists to 21 bits.
		 */
		struct ida hw_ida;
#define MAX_CONTEXT_HW_ID (1<<21) /* exclusive */
#define MAX_GUC_CONTEXT_HW_ID (1 << 20) /* exclusive */
#define GEN11_MAX_CONTEXT_HW_ID (1<<11) /* exclusive */
		struct list_head hw_id_list;
	} contexts;

	u32 fdi_rx_config;

	/* Shadow for DISPLAY_PHY_CONTROL which can't be safely read */
	u32 chv_phy_control;
	/*
	 * Shadows for CHV DPLL_MD regs to keep the state
	 * checker somewhat working in the presence hardware
	 * crappiness (can't read out DPLL_MD for pipes B & C).
	 */
	u32 chv_dpll_md[I915_MAX_PIPES];
	u32 bxt_phy_grc;

	u32 suspend_count;
	bool power_domains_suspended;
	struct i915_suspend_saved_registers regfile;
	struct vlv_s0ix_state vlv_s0ix_state;

	enum {
		I915_SAGV_UNKNOWN = 0,
		I915_SAGV_DISABLED,
		I915_SAGV_ENABLED,
		I915_SAGV_NOT_CONTROLLED
	} sagv_status;

	struct {
		/*
		 * Raw watermark latency values:
		 * in 0.1us units for WM0,
		 * in 0.5us units for WM1+.
		 */
		/* primary */
		u16 pri_latency[5];
		/* sprite */
		u16 spr_latency[5];
		/* cursor */
		u16 cur_latency[5];
		/*
		 * Raw watermark memory latency values
		 * for SKL for all 8 levels
		 * in 1us units.
		 */
		u16 skl_latency[8];

		/* current hardware state */
		union {
			struct ilk_wm_values hw;
			struct skl_ddb_values skl_hw;
			struct vlv_wm_values vlv;
			struct g4x_wm_values g4x;
		};

		u8 max_level;

		/*
		 * Should be held around atomic WM register writing; also
		 * protects * intel_crtc->wm.active and
		 * crtc_state->wm.need_postvbl_update.
		 */
		struct mutex wm_mutex;

		/*
		 * Set during HW readout of watermarks/DDB.  Some platforms
		 * need to know when we're still using BIOS-provided values
		 * (which we don't fully trust).
		 */
		bool distrust_bios_wm;
	} wm;

	struct dram_info {
		bool valid;
		bool is_16gb_dimm;
		u8 num_channels;
		u8 ranks;
		u32 bandwidth_kbps;
		bool symmetric_memory;
		enum intel_dram_type {
			INTEL_DRAM_UNKNOWN,
			INTEL_DRAM_DDR3,
			INTEL_DRAM_DDR4,
			INTEL_DRAM_LPDDR3,
			INTEL_DRAM_LPDDR4
		} type;
	} dram_info;

	struct intel_bw_info {
		unsigned int deratedbw[3]; /* for each QGV point */
		u8 num_qgv_points;
		u8 num_planes;
	} max_bw[6];

	struct drm_private_obj bw_obj;

	struct intel_runtime_pm runtime_pm;

	struct {
		bool initialized;

		struct kobject *metrics_kobj;
		struct ctl_table_header *sysctl_header;

		/*
		 * Lock associated with adding/modifying/removing OA configs
		 * in dev_priv->perf.metrics_idr.
		 */
		struct mutex metrics_lock;

		/*
		 * List of dynamic configurations, you need to hold
		 * dev_priv->perf.metrics_lock to access it.
		 */
		struct idr metrics_idr;

		/*
		 * Lock associated with anything below within this structure
		 * except exclusive_stream.
		 */
		struct mutex lock;
		struct list_head streams;

		struct {
			/*
			 * The stream currently using the OA unit. If accessed
			 * outside a syscall associated to its file
			 * descriptor, you need to hold
			 * dev_priv->drm.struct_mutex.
			 */
			struct i915_perf_stream *exclusive_stream;

			struct intel_context *pinned_ctx;
			u32 specific_ctx_id;
			u32 specific_ctx_id_mask;

			struct hrtimer poll_check_timer;
			wait_queue_head_t poll_wq;
			bool pollin;

			/**
			 * For rate limiting any notifications of spurious
			 * invalid OA reports
			 */
			struct ratelimit_state spurious_report_rs;

			bool periodic;
			int period_exponent;

			struct i915_oa_config test_config;

			struct {
				struct i915_vma *vma;
				u8 *vaddr;
				u32 last_ctx_id;
				int format;
				int format_size;

				/**
				 * Locks reads and writes to all head/tail state
				 *
				 * Consider: the head and tail pointer state
				 * needs to be read consistently from a hrtimer
				 * callback (atomic context) and read() fop
				 * (user context) with tail pointer updates
				 * happening in atomic context and head updates
				 * in user context and the (unlikely)
				 * possibility of read() errors needing to
				 * reset all head/tail state.
				 *
				 * Note: Contention or performance aren't
				 * currently a significant concern here
				 * considering the relatively low frequency of
				 * hrtimer callbacks (5ms period) and that
				 * reads typically only happen in response to a
				 * hrtimer event and likely complete before the
				 * next callback.
				 *
				 * Note: This lock is not held *while* reading
				 * and copying data to userspace so the value
				 * of head observed in htrimer callbacks won't
				 * represent any partial consumption of data.
				 */
				spinlock_t ptr_lock;

				/**
				 * One 'aging' tail pointer and one 'aged'
				 * tail pointer ready to used for reading.
				 *
				 * Initial values of 0xffffffff are invalid
				 * and imply that an update is required
				 * (and should be ignored by an attempted
				 * read)
				 */
				struct {
					u32 offset;
				} tails[2];

				/**
				 * Index for the aged tail ready to read()
				 * data up to.
				 */
				unsigned int aged_tail_idx;

				/**
				 * A monotonic timestamp for when the current
				 * aging tail pointer was read; used to
				 * determine when it is old enough to trust.
				 */
				u64 aging_timestamp;

				/**
				 * Although we can always read back the head
				 * pointer register, we prefer to avoid
				 * trusting the HW state, just to avoid any
				 * risk that some hardware condition could
				 * somehow bump the head pointer unpredictably
				 * and cause us to forward the wrong OA buffer
				 * data to userspace.
				 */
				u32 head;
			} oa_buffer;

			u32 gen7_latched_oastatus1;
			u32 ctx_oactxctrl_offset;
			u32 ctx_flexeu0_offset;

			/**
			 * The RPT_ID/reason field for Gen8+ includes a bit
			 * to determine if the CTX ID in the report is valid
			 * but the specific bit differs between Gen 8 and 9
			 */
			u32 gen8_valid_ctx_bit;

			struct i915_oa_ops ops;
			const struct i915_oa_format *oa_formats;
		} oa;
	} perf;

	/* Abstract the submission mechanism (legacy ringbuffer or execlists) away */
	struct intel_gt gt;

	struct {
		struct notifier_block pm_notifier;

		/**
		 * We leave the user IRQ off as much as possible,
		 * but this means that requests will finish and never
		 * be retired once the system goes idle. Set a timer to
		 * fire periodically while the ring is running. When it
		 * fires, go retire requests.
		 */
		struct delayed_work retire_work;

		/**
		 * When we detect an idle GPU, we want to turn on
		 * powersaving features. So once we see that there
		 * are no more requests outstanding and no more
		 * arrive within a small period of time, we fire
		 * off the idle_work.
		 */
		struct work_struct idle_work;
	} gem;

	/* For i945gm vblank irq vs. C3 workaround */
	struct {
		struct work_struct work;
		struct pm_qos_request pm_qos;
		u8 c3_disable_latency;
		u8 enabled;
	} i945gm_vblank;

	/* perform PHY state sanity checks? */
	bool chv_phy_assert[2];

	bool ipc_enabled;

	/* Used to save the pipe-to-encoder mapping for audio */
	struct intel_encoder *av_enc_map[I915_MAX_PIPES];

	/* necessary resource sharing with HDMI LPE audio driver. */
	struct {
		struct platform_device *platdev;
		int	irq;
	} lpe_audio;

	struct i915_pmu pmu;

	struct i915_hdcp_comp_master *hdcp_master;
	bool hdcp_comp_added;

	/* Mutex to protect the above hdcp component related values. */
	struct mutex hdcp_comp_mutex;

	/*
	 * NOTE: This is the dri1/ums dungeon, don't add stuff here. Your patch
	 * will be rejected. Instead look for a better place.
	 */
};

struct dram_dimm_info {
	u8 size, width, ranks;
};

struct dram_channel_info {
	struct dram_dimm_info dimm_l, dimm_s;
	u8 ranks;
	bool is_16gb_dimm;
};

static inline struct drm_i915_private *to_i915(const struct drm_device *dev)
{
	return container_of(dev, struct drm_i915_private, drm);
}

static inline struct drm_i915_private *kdev_to_i915(struct device *kdev)
{
	return to_i915(dev_get_drvdata(kdev));
}

static inline struct drm_i915_private *wopcm_to_i915(struct intel_wopcm *wopcm)
{
	return container_of(wopcm, struct drm_i915_private, wopcm);
}

static inline struct drm_i915_private *guc_to_i915(struct intel_guc *guc)
{
	return container_of(guc, struct drm_i915_private, guc);
}

static inline struct drm_i915_private *huc_to_i915(struct intel_huc *huc)
{
	return container_of(huc, struct drm_i915_private, huc);
}

/* Simple iterator over all initialised engines */
#define for_each_engine(engine__, dev_priv__, id__) \
	for ((id__) = 0; \
	     (id__) < I915_NUM_ENGINES; \
	     (id__)++) \
		for_each_if ((engine__) = (dev_priv__)->engine[(id__)])

/* Iterator over subset of engines selected by mask */
#define for_each_engine_masked(engine__, dev_priv__, mask__, tmp__) \
	for ((tmp__) = (mask__) & INTEL_INFO(dev_priv__)->engine_mask; \
	     (tmp__) ? \
	     ((engine__) = (dev_priv__)->engine[__mask_next_bit(tmp__)]), 1 : \
	     0;)

enum hdmi_force_audio {
	HDMI_AUDIO_OFF_DVI = -2,	/* no aux data for HDMI-DVI converter */
	HDMI_AUDIO_OFF,			/* force turn off HDMI audio */
	HDMI_AUDIO_AUTO,		/* trust EDID */
	HDMI_AUDIO_ON,			/* force turn on HDMI audio */
};

#define I915_GTT_OFFSET_NONE ((u32)-1)

/*
 * Frontbuffer tracking bits. Set in obj->frontbuffer_bits while a gem bo is
 * considered to be the frontbuffer for the given plane interface-wise. This
 * doesn't mean that the hw necessarily already scans it out, but that any
 * rendering (by the cpu or gpu) will land in the frontbuffer eventually.
 *
 * We have one bit per pipe and per scanout plane type.
 */
#define INTEL_FRONTBUFFER_BITS_PER_PIPE 8
#define INTEL_FRONTBUFFER(pipe, plane_id) ({ \
	BUILD_BUG_ON(INTEL_FRONTBUFFER_BITS_PER_PIPE * I915_MAX_PIPES > 32); \
	BUILD_BUG_ON(I915_MAX_PLANES > INTEL_FRONTBUFFER_BITS_PER_PIPE); \
	BIT((plane_id) + INTEL_FRONTBUFFER_BITS_PER_PIPE * (pipe)); \
})
#define INTEL_FRONTBUFFER_OVERLAY(pipe) \
	BIT(INTEL_FRONTBUFFER_BITS_PER_PIPE - 1 + INTEL_FRONTBUFFER_BITS_PER_PIPE * (pipe))
#define INTEL_FRONTBUFFER_ALL_MASK(pipe) \
	GENMASK(INTEL_FRONTBUFFER_BITS_PER_PIPE * ((pipe) + 1) - 1, \
		INTEL_FRONTBUFFER_BITS_PER_PIPE * (pipe))

#define INTEL_INFO(dev_priv)	(&(dev_priv)->__info)
#define RUNTIME_INFO(dev_priv)	(&(dev_priv)->__runtime)
#define DRIVER_CAPS(dev_priv)	(&(dev_priv)->caps)

#define INTEL_GEN(dev_priv)	(INTEL_INFO(dev_priv)->gen)
#define INTEL_DEVID(dev_priv)	(RUNTIME_INFO(dev_priv)->device_id)

#define REVID_FOREVER		0xff
#define INTEL_REVID(dev_priv)	((dev_priv)->drm.pdev->revision)

#define INTEL_GEN_MASK(s, e) ( \
	BUILD_BUG_ON_ZERO(!__builtin_constant_p(s)) + \
	BUILD_BUG_ON_ZERO(!__builtin_constant_p(e)) + \
	GENMASK((e) - 1, (s) - 1))

/* Returns true if Gen is in inclusive range [Start, End] */
#define IS_GEN_RANGE(dev_priv, s, e) \
	(!!(INTEL_INFO(dev_priv)->gen_mask & INTEL_GEN_MASK((s), (e))))

#define IS_GEN(dev_priv, n) \
	(BUILD_BUG_ON_ZERO(!__builtin_constant_p(n)) + \
	 INTEL_INFO(dev_priv)->gen == (n))

/*
 * Return true if revision is in range [since,until] inclusive.
 *
 * Use 0 for open-ended since, and REVID_FOREVER for open-ended until.
 */
#define IS_REVID(p, since, until) \
	(INTEL_REVID(p) >= (since) && INTEL_REVID(p) <= (until))

static __always_inline unsigned int
__platform_mask_index(const struct intel_runtime_info *info,
		      enum intel_platform p)
{
	const unsigned int pbits =
		BITS_PER_TYPE(info->platform_mask[0]) - INTEL_SUBPLATFORM_BITS;

	/* Expand the platform_mask array if this fails. */
	BUILD_BUG_ON(INTEL_MAX_PLATFORMS >
		     pbits * ARRAY_SIZE(info->platform_mask));

	return p / pbits;
}

static __always_inline unsigned int
__platform_mask_bit(const struct intel_runtime_info *info,
		    enum intel_platform p)
{
	const unsigned int pbits =
		BITS_PER_TYPE(info->platform_mask[0]) - INTEL_SUBPLATFORM_BITS;

	return p % pbits + INTEL_SUBPLATFORM_BITS;
}

static inline u32
intel_subplatform(const struct intel_runtime_info *info, enum intel_platform p)
{
	const unsigned int pi = __platform_mask_index(info, p);

	return info->platform_mask[pi] & INTEL_SUBPLATFORM_BITS;
}

static __always_inline bool
IS_PLATFORM(const struct drm_i915_private *i915, enum intel_platform p)
{
	const struct intel_runtime_info *info = RUNTIME_INFO(i915);
	const unsigned int pi = __platform_mask_index(info, p);
	const unsigned int pb = __platform_mask_bit(info, p);

	BUILD_BUG_ON(!__builtin_constant_p(p));

	return info->platform_mask[pi] & BIT(pb);
}

static __always_inline bool
IS_SUBPLATFORM(const struct drm_i915_private *i915,
	       enum intel_platform p, unsigned int s)
{
	const struct intel_runtime_info *info = RUNTIME_INFO(i915);
	const unsigned int pi = __platform_mask_index(info, p);
	const unsigned int pb = __platform_mask_bit(info, p);
	const unsigned int msb = BITS_PER_TYPE(info->platform_mask[0]) - 1;
	const u32 mask = info->platform_mask[pi];

	BUILD_BUG_ON(!__builtin_constant_p(p));
	BUILD_BUG_ON(!__builtin_constant_p(s));
	BUILD_BUG_ON((s) >= INTEL_SUBPLATFORM_BITS);

	/* Shift and test on the MSB position so sign flag can be used. */
	return ((mask << (msb - pb)) & (mask << (msb - s))) & BIT(msb);
}

#define IS_MOBILE(dev_priv)	(INTEL_INFO(dev_priv)->is_mobile)

#define IS_I830(dev_priv)	IS_PLATFORM(dev_priv, INTEL_I830)
#define IS_I845G(dev_priv)	IS_PLATFORM(dev_priv, INTEL_I845G)
#define IS_I85X(dev_priv)	IS_PLATFORM(dev_priv, INTEL_I85X)
#define IS_I865G(dev_priv)	IS_PLATFORM(dev_priv, INTEL_I865G)
#define IS_I915G(dev_priv)	IS_PLATFORM(dev_priv, INTEL_I915G)
#define IS_I915GM(dev_priv)	IS_PLATFORM(dev_priv, INTEL_I915GM)
#define IS_I945G(dev_priv)	IS_PLATFORM(dev_priv, INTEL_I945G)
#define IS_I945GM(dev_priv)	IS_PLATFORM(dev_priv, INTEL_I945GM)
#define IS_I965G(dev_priv)	IS_PLATFORM(dev_priv, INTEL_I965G)
#define IS_I965GM(dev_priv)	IS_PLATFORM(dev_priv, INTEL_I965GM)
#define IS_G45(dev_priv)	IS_PLATFORM(dev_priv, INTEL_G45)
#define IS_GM45(dev_priv)	IS_PLATFORM(dev_priv, INTEL_GM45)
#define IS_G4X(dev_priv)	(IS_G45(dev_priv) || IS_GM45(dev_priv))
#define IS_PINEVIEW(dev_priv)	IS_PLATFORM(dev_priv, INTEL_PINEVIEW)
#define IS_G33(dev_priv)	IS_PLATFORM(dev_priv, INTEL_G33)
#define IS_IRONLAKE(dev_priv)	IS_PLATFORM(dev_priv, INTEL_IRONLAKE)
#define IS_IRONLAKE_M(dev_priv) \
	(IS_PLATFORM(dev_priv, INTEL_IRONLAKE) && IS_MOBILE(dev_priv))
#define IS_IVYBRIDGE(dev_priv)	IS_PLATFORM(dev_priv, INTEL_IVYBRIDGE)
#define IS_IVB_GT1(dev_priv)	(IS_IVYBRIDGE(dev_priv) && \
				 INTEL_INFO(dev_priv)->gt == 1)
#define IS_VALLEYVIEW(dev_priv)	IS_PLATFORM(dev_priv, INTEL_VALLEYVIEW)
#define IS_CHERRYVIEW(dev_priv)	IS_PLATFORM(dev_priv, INTEL_CHERRYVIEW)
#define IS_HASWELL(dev_priv)	IS_PLATFORM(dev_priv, INTEL_HASWELL)
#define IS_BROADWELL(dev_priv)	IS_PLATFORM(dev_priv, INTEL_BROADWELL)
#define IS_SKYLAKE(dev_priv)	IS_PLATFORM(dev_priv, INTEL_SKYLAKE)
#define IS_BROXTON(dev_priv)	IS_PLATFORM(dev_priv, INTEL_BROXTON)
#define IS_KABYLAKE(dev_priv)	IS_PLATFORM(dev_priv, INTEL_KABYLAKE)
#define IS_GEMINILAKE(dev_priv)	IS_PLATFORM(dev_priv, INTEL_GEMINILAKE)
#define IS_COFFEELAKE(dev_priv)	IS_PLATFORM(dev_priv, INTEL_COFFEELAKE)
#define IS_CANNONLAKE(dev_priv)	IS_PLATFORM(dev_priv, INTEL_CANNONLAKE)
#define IS_ICELAKE(dev_priv)	IS_PLATFORM(dev_priv, INTEL_ICELAKE)
#define IS_ELKHARTLAKE(dev_priv)	IS_PLATFORM(dev_priv, INTEL_ELKHARTLAKE)
#define IS_HSW_EARLY_SDV(dev_priv) (IS_HASWELL(dev_priv) && \
				    (INTEL_DEVID(dev_priv) & 0xFF00) == 0x0C00)
#define IS_BDW_ULT(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_BROADWELL, INTEL_SUBPLATFORM_ULT)
#define IS_BDW_ULX(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_BROADWELL, INTEL_SUBPLATFORM_ULX)
#define IS_BDW_GT3(dev_priv)	(IS_BROADWELL(dev_priv) && \
				 INTEL_INFO(dev_priv)->gt == 3)
#define IS_HSW_ULT(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_HASWELL, INTEL_SUBPLATFORM_ULT)
#define IS_HSW_GT3(dev_priv)	(IS_HASWELL(dev_priv) && \
				 INTEL_INFO(dev_priv)->gt == 3)
#define IS_HSW_GT1(dev_priv)	(IS_HASWELL(dev_priv) && \
				 INTEL_INFO(dev_priv)->gt == 1)
/* ULX machines are also considered ULT. */
#define IS_HSW_ULX(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_HASWELL, INTEL_SUBPLATFORM_ULX)
#define IS_SKL_ULT(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_SKYLAKE, INTEL_SUBPLATFORM_ULT)
#define IS_SKL_ULX(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_SKYLAKE, INTEL_SUBPLATFORM_ULX)
#define IS_KBL_ULT(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_KABYLAKE, INTEL_SUBPLATFORM_ULT)
#define IS_KBL_ULX(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_KABYLAKE, INTEL_SUBPLATFORM_ULX)
#define IS_SKL_GT2(dev_priv)	(IS_SKYLAKE(dev_priv) && \
				 INTEL_INFO(dev_priv)->gt == 2)
#define IS_SKL_GT3(dev_priv)	(IS_SKYLAKE(dev_priv) && \
				 INTEL_INFO(dev_priv)->gt == 3)
#define IS_SKL_GT4(dev_priv)	(IS_SKYLAKE(dev_priv) && \
				 INTEL_INFO(dev_priv)->gt == 4)
#define IS_KBL_GT2(dev_priv)	(IS_KABYLAKE(dev_priv) && \
				 INTEL_INFO(dev_priv)->gt == 2)
#define IS_KBL_GT3(dev_priv)	(IS_KABYLAKE(dev_priv) && \
				 INTEL_INFO(dev_priv)->gt == 3)
#define IS_CFL_ULT(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_COFFEELAKE, INTEL_SUBPLATFORM_ULT)
#define IS_CFL_ULX(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_COFFEELAKE, INTEL_SUBPLATFORM_ULX)
#define IS_CFL_GT2(dev_priv)	(IS_COFFEELAKE(dev_priv) && \
				 INTEL_INFO(dev_priv)->gt == 2)
#define IS_CFL_GT3(dev_priv)	(IS_COFFEELAKE(dev_priv) && \
				 INTEL_INFO(dev_priv)->gt == 3)
#define IS_CNL_WITH_PORT_F(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_CANNONLAKE, INTEL_SUBPLATFORM_PORTF)
#define IS_ICL_WITH_PORT_F(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_ICELAKE, INTEL_SUBPLATFORM_PORTF)

#define SKL_REVID_A0		0x0
#define SKL_REVID_B0		0x1
#define SKL_REVID_C0		0x2
#define SKL_REVID_D0		0x3
#define SKL_REVID_E0		0x4
#define SKL_REVID_F0		0x5
#define SKL_REVID_G0		0x6
#define SKL_REVID_H0		0x7

#define IS_SKL_REVID(p, since, until) (IS_SKYLAKE(p) && IS_REVID(p, since, until))

#define BXT_REVID_A0		0x0
#define BXT_REVID_A1		0x1
#define BXT_REVID_B0		0x3
#define BXT_REVID_B_LAST	0x8
#define BXT_REVID_C0		0x9

#define IS_BXT_REVID(dev_priv, since, until) \
	(IS_BROXTON(dev_priv) && IS_REVID(dev_priv, since, until))

#define KBL_REVID_A0		0x0
#define KBL_REVID_B0		0x1
#define KBL_REVID_C0		0x2
#define KBL_REVID_D0		0x3
#define KBL_REVID_E0		0x4

#define IS_KBL_REVID(dev_priv, since, until) \
	(IS_KABYLAKE(dev_priv) && IS_REVID(dev_priv, since, until))

#define GLK_REVID_A0		0x0
#define GLK_REVID_A1		0x1

#define IS_GLK_REVID(dev_priv, since, until) \
	(IS_GEMINILAKE(dev_priv) && IS_REVID(dev_priv, since, until))

#define CNL_REVID_A0		0x0
#define CNL_REVID_B0		0x1
#define CNL_REVID_C0		0x2

#define IS_CNL_REVID(p, since, until) \
	(IS_CANNONLAKE(p) && IS_REVID(p, since, until))

#define ICL_REVID_A0		0x0
#define ICL_REVID_A2		0x1
#define ICL_REVID_B0		0x3
#define ICL_REVID_B2		0x4
#define ICL_REVID_C0		0x5

#define IS_ICL_REVID(p, since, until) \
	(IS_ICELAKE(p) && IS_REVID(p, since, until))

#define IS_LP(dev_priv)	(INTEL_INFO(dev_priv)->is_lp)
#define IS_GEN9_LP(dev_priv)	(IS_GEN(dev_priv, 9) && IS_LP(dev_priv))
#define IS_GEN9_BC(dev_priv)	(IS_GEN(dev_priv, 9) && !IS_LP(dev_priv))

#define HAS_ENGINE(dev_priv, id) (INTEL_INFO(dev_priv)->engine_mask & BIT(id))

#define ENGINE_INSTANCES_MASK(dev_priv, first, count) ({		\
	unsigned int first__ = (first);					\
	unsigned int count__ = (count);					\
	(INTEL_INFO(dev_priv)->engine_mask &				\
	 GENMASK(first__ + count__ - 1, first__)) >> first__;		\
})
#define VDBOX_MASK(dev_priv) \
	ENGINE_INSTANCES_MASK(dev_priv, VCS0, I915_MAX_VCS)
#define VEBOX_MASK(dev_priv) \
	ENGINE_INSTANCES_MASK(dev_priv, VECS0, I915_MAX_VECS)

#define HAS_LLC(dev_priv)	(INTEL_INFO(dev_priv)->has_llc)
#define HAS_SNOOP(dev_priv)	(INTEL_INFO(dev_priv)->has_snoop)
#define HAS_EDRAM(dev_priv)	((dev_priv)->edram_size_mb)
#define HAS_WT(dev_priv)	((IS_HASWELL(dev_priv) || \
				 IS_BROADWELL(dev_priv)) && HAS_EDRAM(dev_priv))

#define HWS_NEEDS_PHYSICAL(dev_priv)	(INTEL_INFO(dev_priv)->hws_needs_physical)

#define HAS_LOGICAL_RING_CONTEXTS(dev_priv) \
		(INTEL_INFO(dev_priv)->has_logical_ring_contexts)
#define HAS_LOGICAL_RING_ELSQ(dev_priv) \
		(INTEL_INFO(dev_priv)->has_logical_ring_elsq)
#define HAS_LOGICAL_RING_PREEMPTION(dev_priv) \
		(INTEL_INFO(dev_priv)->has_logical_ring_preemption)

#define HAS_EXECLISTS(dev_priv) HAS_LOGICAL_RING_CONTEXTS(dev_priv)

#define INTEL_PPGTT(dev_priv) (INTEL_INFO(dev_priv)->ppgtt_type)
#define HAS_PPGTT(dev_priv) \
	(INTEL_PPGTT(dev_priv) != INTEL_PPGTT_NONE)
#define HAS_FULL_PPGTT(dev_priv) \
	(INTEL_PPGTT(dev_priv) >= INTEL_PPGTT_FULL)

#define HAS_PAGE_SIZES(dev_priv, sizes) ({ \
	GEM_BUG_ON((sizes) == 0); \
	((sizes) & ~INTEL_INFO(dev_priv)->page_sizes) == 0; \
})

#define HAS_OVERLAY(dev_priv)		 (INTEL_INFO(dev_priv)->display.has_overlay)
#define OVERLAY_NEEDS_PHYSICAL(dev_priv) \
		(INTEL_INFO(dev_priv)->display.overlay_needs_physical)

/* Early gen2 have a totally busted CS tlb and require pinned batches. */
#define HAS_BROKEN_CS_TLB(dev_priv)	(IS_I830(dev_priv) || IS_I845G(dev_priv))

/* WaRsDisableCoarsePowerGating:skl,cnl */
#define NEEDS_WaRsDisableCoarsePowerGating(dev_priv) \
	(IS_CANNONLAKE(dev_priv) || \
	 IS_SKL_GT3(dev_priv) || IS_SKL_GT4(dev_priv))

#define HAS_GMBUS_IRQ(dev_priv) (INTEL_GEN(dev_priv) >= 4)
#define HAS_GMBUS_BURST_READ(dev_priv) (INTEL_GEN(dev_priv) >= 10 || \
					IS_GEMINILAKE(dev_priv) || \
					IS_KABYLAKE(dev_priv))

/* With the 945 and later, Y tiling got adjusted so that it was 32 128-byte
 * rows, which changed the alignment requirements and fence programming.
 */
#define HAS_128_BYTE_Y_TILING(dev_priv) (!IS_GEN(dev_priv, 2) && \
					 !(IS_I915G(dev_priv) || \
					 IS_I915GM(dev_priv)))
#define SUPPORTS_TV(dev_priv)		(INTEL_INFO(dev_priv)->display.supports_tv)
#define I915_HAS_HOTPLUG(dev_priv)	(INTEL_INFO(dev_priv)->display.has_hotplug)

#define HAS_FW_BLC(dev_priv) 	(INTEL_GEN(dev_priv) > 2)
#define HAS_FBC(dev_priv)	(INTEL_INFO(dev_priv)->display.has_fbc)
#define HAS_CUR_FBC(dev_priv)	(!HAS_GMCH(dev_priv) && INTEL_GEN(dev_priv) >= 7)

#define HAS_IPS(dev_priv)	(IS_HSW_ULT(dev_priv) || IS_BROADWELL(dev_priv))

#define HAS_DP_MST(dev_priv)	(INTEL_INFO(dev_priv)->display.has_dp_mst)

#define HAS_DDI(dev_priv)		 (INTEL_INFO(dev_priv)->display.has_ddi)
#define HAS_FPGA_DBG_UNCLAIMED(dev_priv) (INTEL_INFO(dev_priv)->has_fpga_dbg)
#define HAS_PSR(dev_priv)		 (INTEL_INFO(dev_priv)->display.has_psr)
#define HAS_TRANSCODER_EDP(dev_priv)	 (INTEL_INFO(dev_priv)->trans_offsets[TRANSCODER_EDP] != 0)

#define HAS_RC6(dev_priv)		 (INTEL_INFO(dev_priv)->has_rc6)
#define HAS_RC6p(dev_priv)		 (INTEL_INFO(dev_priv)->has_rc6p)
#define HAS_RC6pp(dev_priv)		 (false) /* HW was never validated */

#define HAS_RPS(dev_priv)	(INTEL_INFO(dev_priv)->has_rps)

#define HAS_CSR(dev_priv)	(INTEL_INFO(dev_priv)->display.has_csr)

#define HAS_RUNTIME_PM(dev_priv) (INTEL_INFO(dev_priv)->has_runtime_pm)
#define HAS_64BIT_RELOC(dev_priv) (INTEL_INFO(dev_priv)->has_64bit_reloc)

#define HAS_IPC(dev_priv)		 (INTEL_INFO(dev_priv)->display.has_ipc)

/*
 * For now, anything with a GuC requires uCode loading, and then supports
 * command submission once loaded. But these are logically independent
 * properties, so we have separate macros to test them.
 */
#define HAS_GUC(dev_priv)	(INTEL_INFO(dev_priv)->has_guc)
#define HAS_GUC_UCODE(dev_priv)	(HAS_GUC(dev_priv))
#define HAS_GUC_SCHED(dev_priv)	(HAS_GUC(dev_priv))

/* For now, anything with a GuC has also HuC */
#define HAS_HUC(dev_priv)	(HAS_GUC(dev_priv))
#define HAS_HUC_UCODE(dev_priv)	(HAS_GUC(dev_priv))

/* Having a GuC is not the same as using a GuC */
#define USES_GUC(dev_priv)		intel_uc_is_using_guc(dev_priv)
#define USES_GUC_SUBMISSION(dev_priv)	intel_uc_is_using_guc_submission(dev_priv)
#define USES_HUC(dev_priv)		intel_uc_is_using_huc(dev_priv)

#define HAS_POOLED_EU(dev_priv)	(INTEL_INFO(dev_priv)->has_pooled_eu)

#define INTEL_PCH_DEVICE_ID_MASK		0xff80
#define INTEL_PCH_IBX_DEVICE_ID_TYPE		0x3b00
#define INTEL_PCH_CPT_DEVICE_ID_TYPE		0x1c00
#define INTEL_PCH_PPT_DEVICE_ID_TYPE		0x1e00
#define INTEL_PCH_LPT_DEVICE_ID_TYPE		0x8c00
#define INTEL_PCH_LPT_LP_DEVICE_ID_TYPE		0x9c00
#define INTEL_PCH_WPT_DEVICE_ID_TYPE		0x8c80
#define INTEL_PCH_WPT_LP_DEVICE_ID_TYPE		0x9c80
#define INTEL_PCH_SPT_DEVICE_ID_TYPE		0xA100
#define INTEL_PCH_SPT_LP_DEVICE_ID_TYPE		0x9D00
#define INTEL_PCH_KBP_DEVICE_ID_TYPE		0xA280
#define INTEL_PCH_CNP_DEVICE_ID_TYPE		0xA300
#define INTEL_PCH_CNP_LP_DEVICE_ID_TYPE		0x9D80
#define INTEL_PCH_CMP_DEVICE_ID_TYPE		0x0280
#define INTEL_PCH_ICP_DEVICE_ID_TYPE		0x3480
#define INTEL_PCH_MCC_DEVICE_ID_TYPE		0x4B00
#define INTEL_PCH_MCC2_DEVICE_ID_TYPE		0x3880
#define INTEL_PCH_P2X_DEVICE_ID_TYPE		0x7100
#define INTEL_PCH_P3X_DEVICE_ID_TYPE		0x7000
#define INTEL_PCH_QEMU_DEVICE_ID_TYPE		0x2900 /* qemu q35 has 2918 */

#define INTEL_PCH_TYPE(dev_priv) ((dev_priv)->pch_type)
#define INTEL_PCH_ID(dev_priv) ((dev_priv)->pch_id)
#define HAS_PCH_MCC(dev_priv) (INTEL_PCH_TYPE(dev_priv) == PCH_MCC)
#define HAS_PCH_ICP(dev_priv) (INTEL_PCH_TYPE(dev_priv) == PCH_ICP)
#define HAS_PCH_CNP(dev_priv) (INTEL_PCH_TYPE(dev_priv) == PCH_CNP)
#define HAS_PCH_SPT(dev_priv) (INTEL_PCH_TYPE(dev_priv) == PCH_SPT)
#define HAS_PCH_LPT(dev_priv) (INTEL_PCH_TYPE(dev_priv) == PCH_LPT)
#define HAS_PCH_LPT_LP(dev_priv) \
	(INTEL_PCH_ID(dev_priv) == INTEL_PCH_LPT_LP_DEVICE_ID_TYPE || \
	 INTEL_PCH_ID(dev_priv) == INTEL_PCH_WPT_LP_DEVICE_ID_TYPE)
#define HAS_PCH_LPT_H(dev_priv) \
	(INTEL_PCH_ID(dev_priv) == INTEL_PCH_LPT_DEVICE_ID_TYPE || \
	 INTEL_PCH_ID(dev_priv) == INTEL_PCH_WPT_DEVICE_ID_TYPE)
#define HAS_PCH_CPT(dev_priv) (INTEL_PCH_TYPE(dev_priv) == PCH_CPT)
#define HAS_PCH_IBX(dev_priv) (INTEL_PCH_TYPE(dev_priv) == PCH_IBX)
#define HAS_PCH_NOP(dev_priv) (INTEL_PCH_TYPE(dev_priv) == PCH_NOP)
#define HAS_PCH_SPLIT(dev_priv) (INTEL_PCH_TYPE(dev_priv) != PCH_NONE)

#define HAS_GMCH(dev_priv) (INTEL_INFO(dev_priv)->display.has_gmch)

#define HAS_LSPCON(dev_priv) (INTEL_GEN(dev_priv) >= 9)

/* DPF == dynamic parity feature */
#define HAS_L3_DPF(dev_priv) (INTEL_INFO(dev_priv)->has_l3_dpf)
#define NUM_L3_SLICES(dev_priv) (IS_HSW_GT3(dev_priv) ? \
				 2 : HAS_L3_DPF(dev_priv))

#define GT_FREQUENCY_MULTIPLIER 50
#define GEN9_FREQ_SCALER 3

#define HAS_DISPLAY(dev_priv) (INTEL_INFO(dev_priv)->num_pipes > 0)

#include "i915_trace.h"

static inline bool intel_vtd_active(void)
{
#ifdef CONFIG_INTEL_IOMMU
	if (intel_iommu_gfx_mapped)
		return true;
#endif
	return false;
}

static inline bool intel_scanout_needs_vtd_wa(struct drm_i915_private *dev_priv)
{
	return INTEL_GEN(dev_priv) >= 6 && intel_vtd_active();
}

static inline bool
intel_ggtt_update_needs_vtd_wa(struct drm_i915_private *dev_priv)
{
	return IS_BROXTON(dev_priv) && intel_vtd_active();
}

/* i915_drv.c */
void __printf(3, 4)
__i915_printk(struct drm_i915_private *dev_priv, const char *level,
	      const char *fmt, ...);

#define i915_report_error(dev_priv, fmt, ...)				   \
	__i915_printk(dev_priv, KERN_ERR, fmt, ##__VA_ARGS__)

#ifdef CONFIG_COMPAT
extern long i915_compat_ioctl(struct file *filp, unsigned int cmd,
			      unsigned long arg);
#else
#define i915_compat_ioctl NULL
#endif
extern const struct dev_pm_ops i915_pm_ops;

extern int i915_driver_load(struct pci_dev *pdev,
			    const struct pci_device_id *ent);
extern void i915_driver_unload(struct drm_device *dev);

extern void intel_engine_init_hangcheck(struct intel_engine_cs *engine);
extern void intel_hangcheck_init(struct drm_i915_private *dev_priv);
int vlv_force_gfx_clock(struct drm_i915_private *dev_priv, bool on);

u32 intel_calculate_mcr_s_ss_select(struct drm_i915_private *dev_priv);

static inline void i915_queue_hangcheck(struct drm_i915_private *dev_priv)
{
	unsigned long delay;

	if (unlikely(!i915_modparams.enable_hangcheck))
		return;

	/* Don't continually defer the hangcheck so that it is always run at
	 * least once after work has been scheduled on any ring. Otherwise,
	 * we will ignore a hung ring if a second ring is kept busy.
	 */

	delay = round_jiffies_up_relative(DRM_I915_HANGCHECK_JIFFIES);
	queue_delayed_work(system_long_wq,
			   &dev_priv->gpu_error.hangcheck_work, delay);
}

static inline bool intel_gvt_active(struct drm_i915_private *dev_priv)
{
	return dev_priv->gvt;
}

static inline bool intel_vgpu_active(struct drm_i915_private *dev_priv)
{
	return dev_priv->vgpu.active;
}

/* i915_gem.c */
int i915_gem_init_userptr(struct drm_i915_private *dev_priv);
void i915_gem_cleanup_userptr(struct drm_i915_private *dev_priv);
void i915_gem_sanitize(struct drm_i915_private *i915);
int i915_gem_init_early(struct drm_i915_private *dev_priv);
void i915_gem_cleanup_early(struct drm_i915_private *dev_priv);
int i915_gem_freeze(struct drm_i915_private *dev_priv);
int i915_gem_freeze_late(struct drm_i915_private *dev_priv);

static inline void i915_gem_drain_freed_objects(struct drm_i915_private *i915)
{
	/*
	 * A single pass should suffice to release all the freed objects (along
	 * most call paths) , but be a little more paranoid in that freeing
	 * the objects does take a little amount of time, during which the rcu
	 * callbacks could have added new objects into the freed list, and
	 * armed the work again.
	 */
	while (atomic_read(&i915->mm.free_count)) {
		flush_work(&i915->mm.free_work);
		rcu_barrier();
	}
}

static inline void i915_gem_drain_workqueue(struct drm_i915_private *i915)
{
	/*
	 * Similar to objects above (see i915_gem_drain_freed-objects), in
	 * general we have workers that are armed by RCU and then rearm
	 * themselves in their callbacks. To be paranoid, we need to
	 * drain the workqueue a second time after waiting for the RCU
	 * grace period so that we catch work queued via RCU from the first
	 * pass. As neither drain_workqueue() nor flush_workqueue() report
	 * a result, we make an assumption that we only don't require more
	 * than 3 passes to catch all _recursive_ RCU delayed work.
	 *
	 */
	int pass = 3;
	do {
		flush_workqueue(i915->wq);
		rcu_barrier();
		i915_gem_drain_freed_objects(i915);
	} while (--pass);
	drain_workqueue(i915->wq);
}

struct i915_vma * __must_check
i915_gem_object_ggtt_pin(struct drm_i915_gem_object *obj,
			 const struct i915_ggtt_view *view,
			 u64 size,
			 u64 alignment,
			 u64 flags);

int i915_gem_object_unbind(struct drm_i915_gem_object *obj,
			   unsigned long flags);
#define I915_GEM_OBJECT_UNBIND_ACTIVE BIT(0)

void i915_gem_runtime_suspend(struct drm_i915_private *dev_priv);

static inline int __must_check
i915_mutex_lock_interruptible(struct drm_device *dev)
{
	return mutex_lock_interruptible(&dev->struct_mutex);
}

int i915_gem_dumb_create(struct drm_file *file_priv,
			 struct drm_device *dev,
			 struct drm_mode_create_dumb *args);
int i915_gem_mmap_gtt(struct drm_file *file_priv, struct drm_device *dev,
		      u32 handle, u64 *offset);
int i915_gem_mmap_gtt_version(void);

void i915_gem_track_fb(struct drm_i915_gem_object *old,
		       struct drm_i915_gem_object *new,
		       unsigned frontbuffer_bits);

int __must_check i915_gem_set_global_seqno(struct drm_device *dev, u32 seqno);

static inline bool __i915_wedged(struct i915_gpu_error *error)
{
	return unlikely(test_bit(I915_WEDGED, &error->flags));
}

static inline bool i915_reset_failed(struct drm_i915_private *i915)
{
	return __i915_wedged(&i915->gpu_error);
}

static inline u32 i915_reset_count(struct i915_gpu_error *error)
{
	return READ_ONCE(error->reset_count);
}

static inline u32 i915_reset_engine_count(struct i915_gpu_error *error,
					  struct intel_engine_cs *engine)
{
	return READ_ONCE(error->reset_engine_count[engine->id]);
}

void i915_gem_set_wedged(struct drm_i915_private *dev_priv);
bool i915_gem_unset_wedged(struct drm_i915_private *dev_priv);

void i915_gem_init_mmio(struct drm_i915_private *i915);
int __must_check i915_gem_init(struct drm_i915_private *dev_priv);
int __must_check i915_gem_init_hw(struct drm_i915_private *dev_priv);
void i915_gem_fini_hw(struct drm_i915_private *dev_priv);
void i915_gem_fini(struct drm_i915_private *dev_priv);
int i915_gem_wait_for_idle(struct drm_i915_private *dev_priv,
			   unsigned int flags, long timeout);
void i915_gem_suspend(struct drm_i915_private *dev_priv);
void i915_gem_suspend_late(struct drm_i915_private *dev_priv);
void i915_gem_resume(struct drm_i915_private *dev_priv);
vm_fault_t i915_gem_fault(struct vm_fault *vmf);

int i915_gem_open(struct drm_i915_private *i915, struct drm_file *file);
void i915_gem_release(struct drm_device *dev, struct drm_file *file);

int i915_gem_object_set_cache_level(struct drm_i915_gem_object *obj,
				    enum i915_cache_level cache_level);

struct drm_gem_object *i915_gem_prime_import(struct drm_device *dev,
				struct dma_buf *dma_buf);

struct dma_buf *i915_gem_prime_export(struct drm_device *dev,
				struct drm_gem_object *gem_obj, int flags);

static inline struct i915_gem_context *
__i915_gem_context_lookup_rcu(struct drm_i915_file_private *file_priv, u32 id)
{
	return idr_find(&file_priv->context_idr, id);
}

static inline struct i915_gem_context *
i915_gem_context_lookup(struct drm_i915_file_private *file_priv, u32 id)
{
	struct i915_gem_context *ctx;

	rcu_read_lock();
	ctx = __i915_gem_context_lookup_rcu(file_priv, id);
	if (ctx && !kref_get_unless_zero(&ctx->ref))
		ctx = NULL;
	rcu_read_unlock();

	return ctx;
}

int i915_perf_open_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file);
int i915_perf_add_config_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file);
int i915_perf_remove_config_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file);
void i915_oa_init_reg_state(struct intel_engine_cs *engine,
			    struct intel_context *ce,
			    u32 *reg_state);

/* i915_gem_evict.c */
int __must_check i915_gem_evict_something(struct i915_address_space *vm,
					  u64 min_size, u64 alignment,
					  unsigned cache_level,
					  u64 start, u64 end,
					  unsigned flags);
int __must_check i915_gem_evict_for_node(struct i915_address_space *vm,
					 struct drm_mm_node *node,
					 unsigned int flags);
int i915_gem_evict_vm(struct i915_address_space *vm);

/* i915_gem_stolen.c */
int i915_gem_stolen_insert_node(struct drm_i915_private *dev_priv,
				struct drm_mm_node *node, u64 size,
				unsigned alignment);
int i915_gem_stolen_insert_node_in_range(struct drm_i915_private *dev_priv,
					 struct drm_mm_node *node, u64 size,
					 unsigned alignment, u64 start,
					 u64 end);
void i915_gem_stolen_remove_node(struct drm_i915_private *dev_priv,
				 struct drm_mm_node *node);
int i915_gem_init_stolen(struct drm_i915_private *dev_priv);
void i915_gem_cleanup_stolen(struct drm_i915_private *dev_priv);
struct drm_i915_gem_object *
i915_gem_object_create_stolen(struct drm_i915_private *dev_priv,
			      resource_size_t size);
struct drm_i915_gem_object *
i915_gem_object_create_stolen_for_preallocated(struct drm_i915_private *dev_priv,
					       resource_size_t stolen_offset,
					       resource_size_t gtt_offset,
					       resource_size_t size);

/* i915_gem_internal.c */
struct drm_i915_gem_object *
i915_gem_object_create_internal(struct drm_i915_private *dev_priv,
				phys_addr_t size);

/* i915_gem_shrinker.c */
unsigned long i915_gem_shrink(struct drm_i915_private *i915,
			      unsigned long target,
			      unsigned long *nr_scanned,
			      unsigned flags);
#define I915_SHRINK_UNBOUND	BIT(0)
#define I915_SHRINK_BOUND	BIT(1)
#define I915_SHRINK_ACTIVE	BIT(2)
#define I915_SHRINK_VMAPS	BIT(3)
#define I915_SHRINK_WRITEBACK	BIT(4)

unsigned long i915_gem_shrink_all(struct drm_i915_private *i915);
void i915_gem_shrinker_register(struct drm_i915_private *i915);
void i915_gem_shrinker_unregister(struct drm_i915_private *i915);
void i915_gem_shrinker_taints_mutex(struct drm_i915_private *i915,
				    struct mutex *mutex);

/* i915_gem_tiling.c */
static inline bool i915_gem_object_needs_bit17_swizzle(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);

	return dev_priv->mm.bit_6_swizzle_x == I915_BIT_6_SWIZZLE_9_10_17 &&
		i915_gem_object_is_tiled(obj);
}

u32 i915_gem_fence_size(struct drm_i915_private *dev_priv, u32 size,
			unsigned int tiling, unsigned int stride);
u32 i915_gem_fence_alignment(struct drm_i915_private *dev_priv, u32 size,
			     unsigned int tiling, unsigned int stride);

const char *i915_cache_level_str(struct drm_i915_private *i915, int type);

/* i915_cmd_parser.c */
int i915_cmd_parser_get_version(struct drm_i915_private *dev_priv);
void intel_engine_init_cmd_parser(struct intel_engine_cs *engine);
void intel_engine_cleanup_cmd_parser(struct intel_engine_cs *engine);
int intel_engine_cmd_parser(struct intel_engine_cs *engine,
			    struct drm_i915_gem_object *batch_obj,
			    struct drm_i915_gem_object *shadow_batch_obj,
			    u32 batch_start_offset,
			    u32 batch_len,
			    bool is_master);

/* i915_perf.c */
extern void i915_perf_init(struct drm_i915_private *dev_priv);
extern void i915_perf_fini(struct drm_i915_private *dev_priv);
extern void i915_perf_register(struct drm_i915_private *dev_priv);
extern void i915_perf_unregister(struct drm_i915_private *dev_priv);

/* i915_suspend.c */
extern int i915_save_state(struct drm_i915_private *dev_priv);
extern int i915_restore_state(struct drm_i915_private *dev_priv);

/* i915_sysfs.c */
void i915_setup_sysfs(struct drm_i915_private *dev_priv);
void i915_teardown_sysfs(struct drm_i915_private *dev_priv);

/* intel_device_info.c */
static inline struct intel_device_info *
mkwrite_device_info(struct drm_i915_private *dev_priv)
{
	return (struct intel_device_info *)INTEL_INFO(dev_priv);
}

/* modesetting */
extern void intel_modeset_init_hw(struct drm_device *dev);
extern int intel_modeset_init(struct drm_device *dev);
extern void intel_modeset_cleanup(struct drm_device *dev);
extern int intel_modeset_vga_set_state(struct drm_i915_private *dev_priv,
				       bool state);
extern void intel_display_resume(struct drm_device *dev);
extern void i915_redisable_vga(struct drm_i915_private *dev_priv);
extern void i915_redisable_vga_power_on(struct drm_i915_private *dev_priv);
extern void intel_init_pch_refclk(struct drm_i915_private *dev_priv);

int i915_reg_read_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file);

extern struct intel_display_error_state *
intel_display_capture_error_state(struct drm_i915_private *dev_priv);
extern void intel_display_print_error_state(struct drm_i915_error_state_buf *e,
					    struct intel_display_error_state *error);

#define __I915_REG_OP(op__, dev_priv__, ...) \
	intel_uncore_##op__(&(dev_priv__)->uncore, __VA_ARGS__)

#define I915_READ(reg__)	 __I915_REG_OP(read, dev_priv, (reg__))
#define I915_WRITE(reg__, val__) __I915_REG_OP(write, dev_priv, (reg__), (val__))

#define POSTING_READ(reg__)	__I915_REG_OP(posting_read, dev_priv, (reg__))

/* These are untraced mmio-accessors that are only valid to be used inside
 * critical sections, such as inside IRQ handlers, where forcewake is explicitly
 * controlled.
 *
 * Think twice, and think again, before using these.
 *
 * As an example, these accessors can possibly be used between:
 *
 * spin_lock_irq(&dev_priv->uncore.lock);
 * intel_uncore_forcewake_get__locked();
 *
 * and
 *
 * intel_uncore_forcewake_put__locked();
 * spin_unlock_irq(&dev_priv->uncore.lock);
 *
 *
 * Note: some registers may not need forcewake held, so
 * intel_uncore_forcewake_{get,put} can be omitted, see
 * intel_uncore_forcewake_for_reg().
 *
 * Certain architectures will die if the same cacheline is concurrently accessed
 * by different clients (e.g. on Ivybridge). Access to registers should
 * therefore generally be serialised, by either the dev_priv->uncore.lock or
 * a more localised lock guarding all access to that bank of registers.
 */
#define I915_READ_FW(reg__) __I915_REG_OP(read_fw, dev_priv, (reg__))
#define I915_WRITE_FW(reg__, val__) __I915_REG_OP(write_fw, dev_priv, (reg__), (val__))

/* "Broadcast RGB" property */
#define INTEL_BROADCAST_RGB_AUTO 0
#define INTEL_BROADCAST_RGB_FULL 1
#define INTEL_BROADCAST_RGB_LIMITED 2

void i915_memcpy_init_early(struct drm_i915_private *dev_priv);
bool i915_memcpy_from_wc(void *dst, const void *src, unsigned long len);

/* The movntdqa instructions used for memcpy-from-wc require 16-byte alignment,
 * as well as SSE4.1 support. i915_memcpy_from_wc() will report if it cannot
 * perform the operation. To check beforehand, pass in the parameters to
 * to i915_can_memcpy_from_wc() - since we only care about the low 4 bits,
 * you only need to pass in the minor offsets, page-aligned pointers are
 * always valid.
 *
 * For just checking for SSE4.1, in the foreknowledge that the future use
 * will be correctly aligned, just use i915_has_memcpy_from_wc().
 */
#define i915_can_memcpy_from_wc(dst, src, len) \
	i915_memcpy_from_wc((void *)((unsigned long)(dst) | (unsigned long)(src) | (len)), NULL, 0)

#define i915_has_memcpy_from_wc() \
	i915_memcpy_from_wc(NULL, NULL, 0)

/* i915_mm.c */
int remap_io_mapping(struct vm_area_struct *vma,
		     unsigned long addr, unsigned long pfn, unsigned long size,
		     struct io_mapping *iomap);

static inline int intel_hws_csb_write_index(struct drm_i915_private *i915)
{
	if (INTEL_GEN(i915) >= 10)
		return CNL_HWS_CSB_WRITE_INDEX;
	else
		return I915_HWS_CSB_WRITE_INDEX;
}

static inline enum i915_map_type
i915_coherent_map_type(struct drm_i915_private *i915)
{
	return HAS_LLC(i915) ? I915_MAP_WB : I915_MAP_WC;
}

static inline void add_taint_for_CI(unsigned int taint)
{
	/*
	 * The system is "ok", just about surviving for the user, but
	 * CI results are now unreliable as the HW is very suspect.
	 * CI checks the taint state after every test and will reboot
	 * the machine if the kernel is tainted.
	 */
	add_taint(taint, LOCKDEP_STILL_OK);
}

#endif
