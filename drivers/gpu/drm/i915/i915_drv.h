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

#include <linux/pm_qos.h>

#include <drm/ttm/ttm_device.h>

#include "display/intel_display.h"
#include "display/intel_display_core.h"

#include "gem/i915_gem_context_types.h"
#include "gem/i915_gem_lmem.h"
#include "gem/i915_gem_shrinker.h"
#include "gem/i915_gem_stolen.h"

#include "gt/intel_engine.h"
#include "gt/intel_gt_types.h"
#include "gt/intel_region_lmem.h"
#include "gt/intel_workarounds.h"
#include "gt/uc/intel_uc.h"

#include "i915_drm_client.h"
#include "i915_gem.h"
#include "i915_gpu_error.h"
#include "i915_params.h"
#include "i915_perf_types.h"
#include "i915_scheduler.h"
#include "i915_utils.h"
#include "intel_device_info.h"
#include "intel_memory_region.h"
#include "intel_pch.h"
#include "intel_runtime_pm.h"
#include "intel_step.h"
#include "intel_uncore.h"
#include "intel_wopcm.h"

struct drm_i915_clock_gating_funcs;
struct drm_i915_gem_object;
struct drm_i915_private;
struct intel_connector;
struct intel_dp;
struct intel_encoder;
struct intel_limit;
struct intel_overlay_error_state;
struct vlv_s0ix_state;

/* Threshold == 5 for long IRQs, 50 for short */
#define HPD_STORM_DEFAULT_THRESHOLD 50

#define I915_GEM_GPU_DOMAINS \
	(I915_GEM_DOMAIN_RENDER | \
	 I915_GEM_DOMAIN_SAMPLER | \
	 I915_GEM_DOMAIN_COMMAND | \
	 I915_GEM_DOMAIN_INSTRUCTION | \
	 I915_GEM_DOMAIN_VERTEX)

#define I915_COLOR_UNEVICTABLE (-1) /* a non-vma sharing the address space */

#define GEM_QUIRK_PIN_SWIZZLED_PAGES	BIT(0)

struct i915_suspend_saved_registers {
	u32 saveDSPARB;
	u32 saveSWF0[16];
	u32 saveSWF1[16];
	u32 saveSWF3[3];
	u16 saveGCDGMBUS;
};

#define MAX_L3_SLICES 2
struct intel_l3_parity {
	u32 *remap_info[MAX_L3_SLICES];
	struct work_struct error_work;
	int which_slice;
};

struct i915_gem_mm {
	/*
	 * Shortcut for the stolen region. This points to either
	 * INTEL_REGION_STOLEN_SMEM for integrated platforms, or
	 * INTEL_REGION_STOLEN_LMEM for discrete, or NULL if the device doesn't
	 * support stolen.
	 */
	struct intel_memory_region *stolen_region;
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
	/**
	 * Count of objects pending destructions. Used to skip needlessly
	 * waiting on an RCU barrier if no objects are waiting to be freed.
	 */
	atomic_t free_count;

	/**
	 * tmpfs instance used for shmem backed objects
	 */
	struct vfsmount *gemfs;

	struct intel_memory_region *regions[INTEL_REGION_UNKNOWN];

	struct notifier_block oom_notifier;
	struct notifier_block vmap_notifier;
	struct shrinker shrinker;

#ifdef CONFIG_MMU_NOTIFIER
	/**
	 * notifier_lock for mmu notifiers, memory may not be allocated
	 * while holding this lock.
	 */
	rwlock_t notifier_lock;
#endif

	/* shrinker accounting, also useful for userland debugging */
	u64 shrink_memory;
	u32 shrink_count;
};

#define I915_IDLE_ENGINES_TIMEOUT (200) /* in ms */

unsigned long i915_fence_context_timeout(const struct drm_i915_private *i915,
					 u64 context);

static inline unsigned long
i915_fence_timeout(const struct drm_i915_private *i915)
{
	return i915_fence_context_timeout(i915, U64_MAX);
}

#define HAS_HW_SAGV_WM(i915) (DISPLAY_VER(i915) >= 13 && !IS_DGFX(i915))

struct i915_virtual_gpu {
	struct mutex lock; /* serialises sending of g2v_notify command pkts */
	bool active;
	u32 caps;
	u32 *initial_mmio;
	u8 *initial_cfg_space;
	struct list_head entry;
};

struct i915_selftest_stash {
	atomic_t counter;
	struct ida mock_region_instances;
};

struct drm_i915_private {
	struct drm_device drm;

	struct intel_display display;

	/* FIXME: Device release actions should all be moved to drmm_ */
	bool do_release;

	/* i915 device parameters */
	struct i915_params params;

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
	struct intel_uncore_mmio_debug mmio_debug;

	struct i915_virtual_gpu vgpu;

	struct intel_gvt *gvt;

	struct intel_wopcm wopcm;

	struct pci_dev *bridge_dev;

	struct rb_root uabi_engines;
	unsigned int engine_uabi_class_count[I915_LAST_UABI_ENGINE_CLASS + 1];

	struct resource mch_res;

	/* protects the irq masks */
	spinlock_t irq_lock;

	bool display_irqs_enabled;

	/* Sideband mailbox protection */
	struct mutex sb_lock;
	struct pm_qos_request sb_qos;

	/** Cached value of IMR to avoid reads in updating the bitfield */
	union {
		u32 irq_mask;
		u32 de_irq_mask[I915_MAX_PIPES];
	};
	u32 pipestat_irq_mask[I915_MAX_PIPES];

	bool preserve_bios_swizzle;

	unsigned int fsb_freq, mem_freq, is_ddr3;
	unsigned int skl_preferred_vco_freq;

	unsigned int max_dotclk_freq;
	unsigned int hpll_freq;
	unsigned int czclk_freq;

	/**
	 * wq - Driver workqueue for GEM.
	 *
	 * NOTE: Work items scheduled here are not allowed to grab any modeset
	 * locks, for otherwise the flushing done in the pageflip code will
	 * result in deadlocks.
	 */
	struct workqueue_struct *wq;

	/* pm private clock gating functions */
	const struct drm_i915_clock_gating_funcs *clock_gating_funcs;

	/* PCH chipset type */
	enum intel_pch pch_type;
	unsigned short pch_id;

	unsigned long gem_quirks;

	struct drm_atomic_state *modeset_restore_state;
	struct drm_modeset_acquire_ctx reset_ctx;

	struct i915_gem_mm mm;

	/* Kernel Modesetting */

	struct list_head global_obj_list;

	bool mchbar_need_disable;

	struct intel_l3_parity l3_parity;

	/*
	 * HTI (aka HDPORT) state read during initial hw readout.  Most
	 * platforms don't have HTI, so this will just stay 0.  Those that do
	 * will use this later to figure out which PLLs and PHYs are unavailable
	 * for driver usage.
	 */
	u32 hti_state;

	/*
	 * edram size in MB.
	 * Cannot be determined by PCIID. You must always read a register.
	 */
	u32 edram_size_mb;

	struct i915_gpu_error gpu_error;

	/*
	 * Shadows for CHV DPLL_MD regs to keep the state
	 * checker somewhat working in the presence hardware
	 * crappiness (can't read out DPLL_MD for pipes B & C).
	 */
	u32 chv_dpll_md[I915_MAX_PIPES];
	u32 bxt_phy_grc;

	u32 suspend_count;
	struct i915_suspend_saved_registers regfile;
	struct vlv_s0ix_state *vlv_s0ix_state;

	struct dram_info {
		bool wm_lv_0_adjust_needed;
		u8 num_channels;
		bool symmetric_memory;
		enum intel_dram_type {
			INTEL_DRAM_UNKNOWN,
			INTEL_DRAM_DDR3,
			INTEL_DRAM_DDR4,
			INTEL_DRAM_LPDDR3,
			INTEL_DRAM_LPDDR4,
			INTEL_DRAM_DDR5,
			INTEL_DRAM_LPDDR5,
		} type;
		u8 num_qgv_points;
		u8 num_psf_gv_points;
	} dram_info;

	struct intel_runtime_pm runtime_pm;

	struct i915_perf perf;

	/* Abstract the submission mechanism (legacy ringbuffer or execlists) away */
	struct intel_gt gt0;

	/*
	 * i915->gt[0] == &i915->gt0
	 */
#define I915_MAX_GT 4
	struct intel_gt *gt[I915_MAX_GT];

	struct kobject *sysfs_gt;

	/* Quick lookup of media GT (current platforms only have one) */
	struct intel_gt *media_gt;

	struct {
		struct i915_gem_contexts {
			spinlock_t lock; /* locks list */
			struct list_head list;
		} contexts;

		/*
		 * We replace the local file with a global mappings as the
		 * backing storage for the mmap is on the device and not
		 * on the struct file, and we do not want to prolong the
		 * lifetime of the local fd. To minimise the number of
		 * anonymous inodes we create, we use a global singleton to
		 * share the global mapping.
		 */
		struct file *mmap_singleton;
	} gem;

	u8 pch_ssc_use;

	/* For i915gm/i945gm vblank irq workaround */
	u8 vblank_enabled;

	bool irq_enabled;

	/*
	 * DG2: Mask of PHYs that were not calibrated by the firmware
	 * and should not be used.
	 */
	u8 snps_phy_failed_calibration;

	struct i915_pmu pmu;

	struct i915_drm_clients clients;

	/* The TTM device structure. */
	struct ttm_device bdev;

	I915_SELFTEST_DECLARE(struct i915_selftest_stash selftest;)

	/*
	 * NOTE: This is the dri1/ums dungeon, don't add stuff here. Your patch
	 * will be rejected. Instead look for a better place.
	 */
};

static inline struct drm_i915_private *to_i915(const struct drm_device *dev)
{
	return container_of(dev, struct drm_i915_private, drm);
}

static inline struct drm_i915_private *kdev_to_i915(struct device *kdev)
{
	return dev_get_drvdata(kdev);
}

static inline struct drm_i915_private *pdev_to_i915(struct pci_dev *pdev)
{
	return pci_get_drvdata(pdev);
}

static inline struct intel_gt *to_gt(struct drm_i915_private *i915)
{
	return &i915->gt0;
}

/* Simple iterator over all initialised engines */
#define for_each_engine(engine__, dev_priv__, id__) \
	for ((id__) = 0; \
	     (id__) < I915_NUM_ENGINES; \
	     (id__)++) \
		for_each_if ((engine__) = (dev_priv__)->engine[(id__)])

/* Iterator over subset of engines selected by mask */
#define for_each_engine_masked(engine__, gt__, mask__, tmp__) \
	for ((tmp__) = (mask__) & (gt__)->info.engine_mask; \
	     (tmp__) ? \
	     ((engine__) = (gt__)->engine[__mask_next_bit(tmp__)]), 1 : \
	     0;)

#define rb_to_uabi_engine(rb) \
	rb_entry_safe(rb, struct intel_engine_cs, uabi_node)

#define for_each_uabi_engine(engine__, i915__) \
	for ((engine__) = rb_to_uabi_engine(rb_first(&(i915__)->uabi_engines));\
	     (engine__); \
	     (engine__) = rb_to_uabi_engine(rb_next(&(engine__)->uabi_node)))

#define for_each_uabi_class_engine(engine__, class__, i915__) \
	for ((engine__) = intel_engine_lookup_user((i915__), (class__), 0); \
	     (engine__) && (engine__)->uabi_class == (class__); \
	     (engine__) = rb_to_uabi_engine(rb_next(&(engine__)->uabi_node)))

#define INTEL_INFO(dev_priv)	(&(dev_priv)->__info)
#define RUNTIME_INFO(dev_priv)	(&(dev_priv)->__runtime)
#define DRIVER_CAPS(dev_priv)	(&(dev_priv)->caps)

#define INTEL_DEVID(dev_priv)	(RUNTIME_INFO(dev_priv)->device_id)

#define IP_VER(ver, rel)		((ver) << 8 | (rel))

#define GRAPHICS_VER(i915)		(RUNTIME_INFO(i915)->graphics.ip.ver)
#define GRAPHICS_VER_FULL(i915)		IP_VER(RUNTIME_INFO(i915)->graphics.ip.ver, \
					       RUNTIME_INFO(i915)->graphics.ip.rel)
#define IS_GRAPHICS_VER(i915, from, until) \
	(GRAPHICS_VER(i915) >= (from) && GRAPHICS_VER(i915) <= (until))

#define MEDIA_VER(i915)			(RUNTIME_INFO(i915)->media.ip.ver)
#define MEDIA_VER_FULL(i915)		IP_VER(RUNTIME_INFO(i915)->media.ip.ver, \
					       RUNTIME_INFO(i915)->media.ip.rel)
#define IS_MEDIA_VER(i915, from, until) \
	(MEDIA_VER(i915) >= (from) && MEDIA_VER(i915) <= (until))

#define DISPLAY_VER(i915)	(RUNTIME_INFO(i915)->display.ip.ver)
#define IS_DISPLAY_VER(i915, from, until) \
	(DISPLAY_VER(i915) >= (from) && DISPLAY_VER(i915) <= (until))

#define INTEL_REVID(dev_priv)	(to_pci_dev((dev_priv)->drm.dev)->revision)

#define HAS_DSB(dev_priv)	(INTEL_INFO(dev_priv)->display.has_dsb)

#define INTEL_DISPLAY_STEP(__i915) (RUNTIME_INFO(__i915)->step.display_step)
#define INTEL_GRAPHICS_STEP(__i915) (RUNTIME_INFO(__i915)->step.graphics_step)
#define INTEL_MEDIA_STEP(__i915) (RUNTIME_INFO(__i915)->step.media_step)
#define INTEL_BASEDIE_STEP(__i915) (RUNTIME_INFO(__i915)->step.basedie_step)

#define IS_DISPLAY_STEP(__i915, since, until) \
	(drm_WARN_ON(&(__i915)->drm, INTEL_DISPLAY_STEP(__i915) == STEP_NONE), \
	 INTEL_DISPLAY_STEP(__i915) >= (since) && INTEL_DISPLAY_STEP(__i915) < (until))

#define IS_GRAPHICS_STEP(__i915, since, until) \
	(drm_WARN_ON(&(__i915)->drm, INTEL_GRAPHICS_STEP(__i915) == STEP_NONE), \
	 INTEL_GRAPHICS_STEP(__i915) >= (since) && INTEL_GRAPHICS_STEP(__i915) < (until))

#define IS_MEDIA_STEP(__i915, since, until) \
	(drm_WARN_ON(&(__i915)->drm, INTEL_MEDIA_STEP(__i915) == STEP_NONE), \
	 INTEL_MEDIA_STEP(__i915) >= (since) && INTEL_MEDIA_STEP(__i915) < (until))

#define IS_BASEDIE_STEP(__i915, since, until) \
	(drm_WARN_ON(&(__i915)->drm, INTEL_BASEDIE_STEP(__i915) == STEP_NONE), \
	 INTEL_BASEDIE_STEP(__i915) >= (since) && INTEL_BASEDIE_STEP(__i915) < (until))

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

	return info->platform_mask[pi] & INTEL_SUBPLATFORM_MASK;
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
#define IS_DGFX(dev_priv)   (INTEL_INFO(dev_priv)->is_dgfx)

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
#define IS_SANDYBRIDGE(dev_priv) IS_PLATFORM(dev_priv, INTEL_SANDYBRIDGE)
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
#define IS_COMETLAKE(dev_priv)	IS_PLATFORM(dev_priv, INTEL_COMETLAKE)
#define IS_ICELAKE(dev_priv)	IS_PLATFORM(dev_priv, INTEL_ICELAKE)
#define IS_JSL_EHL(dev_priv)	(IS_PLATFORM(dev_priv, INTEL_JASPERLAKE) || \
				IS_PLATFORM(dev_priv, INTEL_ELKHARTLAKE))
#define IS_TIGERLAKE(dev_priv)	IS_PLATFORM(dev_priv, INTEL_TIGERLAKE)
#define IS_ROCKETLAKE(dev_priv)	IS_PLATFORM(dev_priv, INTEL_ROCKETLAKE)
#define IS_DG1(dev_priv)        IS_PLATFORM(dev_priv, INTEL_DG1)
#define IS_ALDERLAKE_S(dev_priv) IS_PLATFORM(dev_priv, INTEL_ALDERLAKE_S)
#define IS_ALDERLAKE_P(dev_priv) IS_PLATFORM(dev_priv, INTEL_ALDERLAKE_P)
#define IS_XEHPSDV(dev_priv) IS_PLATFORM(dev_priv, INTEL_XEHPSDV)
#define IS_DG2(dev_priv)	IS_PLATFORM(dev_priv, INTEL_DG2)
#define IS_PONTEVECCHIO(dev_priv) IS_PLATFORM(dev_priv, INTEL_PONTEVECCHIO)
#define IS_METEORLAKE(dev_priv) IS_PLATFORM(dev_priv, INTEL_METEORLAKE)

#define IS_METEORLAKE_M(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_METEORLAKE, INTEL_SUBPLATFORM_M)
#define IS_METEORLAKE_P(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_METEORLAKE, INTEL_SUBPLATFORM_P)
#define IS_DG2_G10(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_DG2, INTEL_SUBPLATFORM_G10)
#define IS_DG2_G11(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_DG2, INTEL_SUBPLATFORM_G11)
#define IS_DG2_G12(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_DG2, INTEL_SUBPLATFORM_G12)
#define IS_ADLS_RPLS(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_ALDERLAKE_S, INTEL_SUBPLATFORM_RPL)
#define IS_ADLP_N(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_ALDERLAKE_P, INTEL_SUBPLATFORM_N)
#define IS_ADLP_RPLP(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_ALDERLAKE_P, INTEL_SUBPLATFORM_RPL)
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

#define IS_CML_ULT(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_COMETLAKE, INTEL_SUBPLATFORM_ULT)
#define IS_CML_ULX(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_COMETLAKE, INTEL_SUBPLATFORM_ULX)
#define IS_CML_GT2(dev_priv)	(IS_COMETLAKE(dev_priv) && \
				 INTEL_INFO(dev_priv)->gt == 2)

#define IS_ICL_WITH_PORT_F(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_ICELAKE, INTEL_SUBPLATFORM_PORTF)

#define IS_TGL_UY(dev_priv) \
	IS_SUBPLATFORM(dev_priv, INTEL_TIGERLAKE, INTEL_SUBPLATFORM_UY)

#define IS_SKL_GRAPHICS_STEP(p, since, until) (IS_SKYLAKE(p) && IS_GRAPHICS_STEP(p, since, until))

#define IS_KBL_GRAPHICS_STEP(dev_priv, since, until) \
	(IS_KABYLAKE(dev_priv) && IS_GRAPHICS_STEP(dev_priv, since, until))
#define IS_KBL_DISPLAY_STEP(dev_priv, since, until) \
	(IS_KABYLAKE(dev_priv) && IS_DISPLAY_STEP(dev_priv, since, until))

#define IS_JSL_EHL_GRAPHICS_STEP(p, since, until) \
	(IS_JSL_EHL(p) && IS_GRAPHICS_STEP(p, since, until))
#define IS_JSL_EHL_DISPLAY_STEP(p, since, until) \
	(IS_JSL_EHL(p) && IS_DISPLAY_STEP(p, since, until))

#define IS_TGL_DISPLAY_STEP(__i915, since, until) \
	(IS_TIGERLAKE(__i915) && \
	 IS_DISPLAY_STEP(__i915, since, until))

#define IS_TGL_UY_GRAPHICS_STEP(__i915, since, until) \
	(IS_TGL_UY(__i915) && \
	 IS_GRAPHICS_STEP(__i915, since, until))

#define IS_TGL_GRAPHICS_STEP(__i915, since, until) \
	(IS_TIGERLAKE(__i915) && !IS_TGL_UY(__i915)) && \
	 IS_GRAPHICS_STEP(__i915, since, until))

#define IS_RKL_DISPLAY_STEP(p, since, until) \
	(IS_ROCKETLAKE(p) && IS_DISPLAY_STEP(p, since, until))

#define IS_DG1_GRAPHICS_STEP(p, since, until) \
	(IS_DG1(p) && IS_GRAPHICS_STEP(p, since, until))
#define IS_DG1_DISPLAY_STEP(p, since, until) \
	(IS_DG1(p) && IS_DISPLAY_STEP(p, since, until))

#define IS_ADLS_DISPLAY_STEP(__i915, since, until) \
	(IS_ALDERLAKE_S(__i915) && \
	 IS_DISPLAY_STEP(__i915, since, until))

#define IS_ADLS_GRAPHICS_STEP(__i915, since, until) \
	(IS_ALDERLAKE_S(__i915) && \
	 IS_GRAPHICS_STEP(__i915, since, until))

#define IS_ADLP_DISPLAY_STEP(__i915, since, until) \
	(IS_ALDERLAKE_P(__i915) && \
	 IS_DISPLAY_STEP(__i915, since, until))

#define IS_ADLP_GRAPHICS_STEP(__i915, since, until) \
	(IS_ALDERLAKE_P(__i915) && \
	 IS_GRAPHICS_STEP(__i915, since, until))

#define IS_XEHPSDV_GRAPHICS_STEP(__i915, since, until) \
	(IS_XEHPSDV(__i915) && IS_GRAPHICS_STEP(__i915, since, until))

/*
 * DG2 hardware steppings are a bit unusual.  The hardware design was forked to
 * create three variants (G10, G11, and G12) which each have distinct
 * workaround sets.  The G11 and G12 forks of the DG2 design reset the GT
 * stepping back to "A0" for their first iterations, even though they're more
 * similar to a G10 B0 stepping and G10 C0 stepping respectively in terms of
 * functionality and workarounds.  However the display stepping does not reset
 * in the same manner --- a specific stepping like "B0" has a consistent
 * meaning regardless of whether it belongs to a G10, G11, or G12 DG2.
 *
 * TLDR:  All GT workarounds and stepping-specific logic must be applied in
 * relation to a specific subplatform (G10/G11/G12), whereas display workarounds
 * and stepping-specific logic will be applied with a general DG2-wide stepping
 * number.
 */
#define IS_DG2_GRAPHICS_STEP(__i915, variant, since, until) \
	(IS_SUBPLATFORM(__i915, INTEL_DG2, INTEL_SUBPLATFORM_##variant) && \
	 IS_GRAPHICS_STEP(__i915, since, until))

#define IS_DG2_DISPLAY_STEP(__i915, since, until) \
	(IS_DG2(__i915) && \
	 IS_DISPLAY_STEP(__i915, since, until))

#define IS_PVC_BD_STEP(__i915, since, until) \
	(IS_PONTEVECCHIO(__i915) && \
	 IS_BASEDIE_STEP(__i915, since, until))

#define IS_PVC_CT_STEP(__i915, since, until) \
	(IS_PONTEVECCHIO(__i915) && \
	 IS_GRAPHICS_STEP(__i915, since, until))

#define IS_LP(dev_priv)		(INTEL_INFO(dev_priv)->is_lp)
#define IS_GEN9_LP(dev_priv)	(GRAPHICS_VER(dev_priv) == 9 && IS_LP(dev_priv))
#define IS_GEN9_BC(dev_priv)	(GRAPHICS_VER(dev_priv) == 9 && !IS_LP(dev_priv))

#define __HAS_ENGINE(engine_mask, id) ((engine_mask) & BIT(id))
#define HAS_ENGINE(gt, id) __HAS_ENGINE((gt)->info.engine_mask, id)

#define ENGINE_INSTANCES_MASK(gt, first, count) ({		\
	unsigned int first__ = (first);					\
	unsigned int count__ = (count);					\
	((gt)->info.engine_mask &						\
	 GENMASK(first__ + count__ - 1, first__)) >> first__;		\
})
#define RCS_MASK(gt) \
	ENGINE_INSTANCES_MASK(gt, RCS0, I915_MAX_RCS)
#define BCS_MASK(gt) \
	ENGINE_INSTANCES_MASK(gt, BCS0, I915_MAX_BCS)
#define VDBOX_MASK(gt) \
	ENGINE_INSTANCES_MASK(gt, VCS0, I915_MAX_VCS)
#define VEBOX_MASK(gt) \
	ENGINE_INSTANCES_MASK(gt, VECS0, I915_MAX_VECS)
#define CCS_MASK(gt) \
	ENGINE_INSTANCES_MASK(gt, CCS0, I915_MAX_CCS)

#define HAS_MEDIA_RATIO_MODE(dev_priv) (INTEL_INFO(dev_priv)->has_media_ratio_mode)

/*
 * The Gen7 cmdparser copies the scanned buffer to the ggtt for execution
 * All later gens can run the final buffer from the ppgtt
 */
#define CMDPARSER_USES_GGTT(dev_priv) (GRAPHICS_VER(dev_priv) == 7)

#define HAS_LLC(dev_priv)	(INTEL_INFO(dev_priv)->has_llc)
#define HAS_4TILE(dev_priv)	(INTEL_INFO(dev_priv)->has_4tile)
#define HAS_SNOOP(dev_priv)	(INTEL_INFO(dev_priv)->has_snoop)
#define HAS_EDRAM(dev_priv)	((dev_priv)->edram_size_mb)
#define HAS_SECURE_BATCHES(dev_priv) (GRAPHICS_VER(dev_priv) < 6)
#define HAS_WT(dev_priv)	HAS_EDRAM(dev_priv)

#define HWS_NEEDS_PHYSICAL(dev_priv)	(INTEL_INFO(dev_priv)->hws_needs_physical)

#define HAS_LOGICAL_RING_CONTEXTS(dev_priv) \
		(INTEL_INFO(dev_priv)->has_logical_ring_contexts)
#define HAS_LOGICAL_RING_ELSQ(dev_priv) \
		(INTEL_INFO(dev_priv)->has_logical_ring_elsq)

#define HAS_EXECLISTS(dev_priv) HAS_LOGICAL_RING_CONTEXTS(dev_priv)

#define INTEL_PPGTT(dev_priv) (RUNTIME_INFO(dev_priv)->ppgtt_type)
#define HAS_PPGTT(dev_priv) \
	(INTEL_PPGTT(dev_priv) != INTEL_PPGTT_NONE)
#define HAS_FULL_PPGTT(dev_priv) \
	(INTEL_PPGTT(dev_priv) >= INTEL_PPGTT_FULL)

#define HAS_PAGE_SIZES(dev_priv, sizes) ({ \
	GEM_BUG_ON((sizes) == 0); \
	((sizes) & ~RUNTIME_INFO(dev_priv)->page_sizes) == 0; \
})

#define HAS_OVERLAY(dev_priv)		 (INTEL_INFO(dev_priv)->display.has_overlay)
#define OVERLAY_NEEDS_PHYSICAL(dev_priv) \
		(INTEL_INFO(dev_priv)->display.overlay_needs_physical)

/* Early gen2 have a totally busted CS tlb and require pinned batches. */
#define HAS_BROKEN_CS_TLB(dev_priv)	(IS_I830(dev_priv) || IS_I845G(dev_priv))

#define NEEDS_RC6_CTX_CORRUPTION_WA(dev_priv)	\
	(IS_BROADWELL(dev_priv) || GRAPHICS_VER(dev_priv) == 9)

/* WaRsDisableCoarsePowerGating:skl,cnl */
#define NEEDS_WaRsDisableCoarsePowerGating(dev_priv)			\
	(IS_SKL_GT3(dev_priv) || IS_SKL_GT4(dev_priv))

#define HAS_GMBUS_IRQ(dev_priv) (DISPLAY_VER(dev_priv) >= 4)
#define HAS_GMBUS_BURST_READ(dev_priv) (DISPLAY_VER(dev_priv) >= 11 || \
					IS_GEMINILAKE(dev_priv) || \
					IS_KABYLAKE(dev_priv))

/* With the 945 and later, Y tiling got adjusted so that it was 32 128-byte
 * rows, which changed the alignment requirements and fence programming.
 */
#define HAS_128_BYTE_Y_TILING(dev_priv) (GRAPHICS_VER(dev_priv) != 2 && \
					 !(IS_I915G(dev_priv) || IS_I915GM(dev_priv)))
#define SUPPORTS_TV(dev_priv)		(INTEL_INFO(dev_priv)->display.supports_tv)
#define I915_HAS_HOTPLUG(dev_priv)	(INTEL_INFO(dev_priv)->display.has_hotplug)

#define HAS_FW_BLC(dev_priv)	(DISPLAY_VER(dev_priv) > 2)
#define HAS_FBC(dev_priv)	(RUNTIME_INFO(dev_priv)->fbc_mask != 0)
#define HAS_CUR_FBC(dev_priv)	(!HAS_GMCH(dev_priv) && DISPLAY_VER(dev_priv) >= 7)

#define HAS_IPS(dev_priv)	(IS_HSW_ULT(dev_priv) || IS_BROADWELL(dev_priv))

#define HAS_DP_MST(dev_priv)	(INTEL_INFO(dev_priv)->display.has_dp_mst)
#define HAS_DP20(dev_priv)	(IS_DG2(dev_priv) || DISPLAY_VER(dev_priv) >= 14)

#define HAS_DOUBLE_BUFFERED_M_N(dev_priv)	(DISPLAY_VER(dev_priv) >= 9 || IS_BROADWELL(dev_priv))

#define HAS_CDCLK_CRAWL(dev_priv)	 (INTEL_INFO(dev_priv)->display.has_cdclk_crawl)
#define HAS_DDI(dev_priv)		 (INTEL_INFO(dev_priv)->display.has_ddi)
#define HAS_FPGA_DBG_UNCLAIMED(dev_priv) (INTEL_INFO(dev_priv)->display.has_fpga_dbg)
#define HAS_PSR(dev_priv)		 (INTEL_INFO(dev_priv)->display.has_psr)
#define HAS_PSR_HW_TRACKING(dev_priv) \
	(INTEL_INFO(dev_priv)->display.has_psr_hw_tracking)
#define HAS_PSR2_SEL_FETCH(dev_priv)	 (DISPLAY_VER(dev_priv) >= 12)
#define HAS_TRANSCODER(dev_priv, trans)	 ((RUNTIME_INFO(dev_priv)->cpu_transcoder_mask & BIT(trans)) != 0)

#define HAS_RC6(dev_priv)		 (INTEL_INFO(dev_priv)->has_rc6)
#define HAS_RC6p(dev_priv)		 (INTEL_INFO(dev_priv)->has_rc6p)
#define HAS_RC6pp(dev_priv)		 (false) /* HW was never validated */

#define HAS_RPS(dev_priv)	(INTEL_INFO(dev_priv)->has_rps)

#define HAS_DMC(dev_priv)	(RUNTIME_INFO(dev_priv)->has_dmc)

#define HAS_HECI_PXP(dev_priv) \
	(INTEL_INFO(dev_priv)->has_heci_pxp)

#define HAS_HECI_GSCFI(dev_priv) \
	(INTEL_INFO(dev_priv)->has_heci_gscfi)

#define HAS_HECI_GSC(dev_priv) (HAS_HECI_PXP(dev_priv) || HAS_HECI_GSCFI(dev_priv))

#define HAS_MSO(i915)		(DISPLAY_VER(i915) >= 12)

#define HAS_RUNTIME_PM(dev_priv) (INTEL_INFO(dev_priv)->has_runtime_pm)
#define HAS_64BIT_RELOC(dev_priv) (INTEL_INFO(dev_priv)->has_64bit_reloc)

/*
 * Set this flag, when platform requires 64K GTT page sizes or larger for
 * device local memory access.
 */
#define HAS_64K_PAGES(dev_priv) (INTEL_INFO(dev_priv)->has_64k_pages)

/*
 * Set this flag when platform doesn't allow both 64k pages and 4k pages in
 * the same PT. this flag means we need to support compact PT layout for the
 * ppGTT when using the 64K GTT pages.
 */
#define NEEDS_COMPACT_PT(dev_priv) (INTEL_INFO(dev_priv)->needs_compact_pt)

#define HAS_IPC(dev_priv)		 (INTEL_INFO(dev_priv)->display.has_ipc)

#define HAS_REGION(i915, i) (RUNTIME_INFO(i915)->memory_regions & (i))
#define HAS_LMEM(i915) HAS_REGION(i915, REGION_LMEM)

#define HAS_EXTRA_GT_LIST(dev_priv)   (INTEL_INFO(dev_priv)->extra_gt_list)

/*
 * Platform has the dedicated compression control state for each lmem surfaces
 * stored in lmem to support the 3D and media compression formats.
 */
#define HAS_FLAT_CCS(dev_priv)   (INTEL_INFO(dev_priv)->has_flat_ccs)

#define HAS_GT_UC(dev_priv)	(INTEL_INFO(dev_priv)->has_gt_uc)

#define HAS_POOLED_EU(dev_priv)	(RUNTIME_INFO(dev_priv)->has_pooled_eu)

#define HAS_GLOBAL_MOCS_REGISTERS(dev_priv)	(INTEL_INFO(dev_priv)->has_global_mocs)

#define HAS_PXP(dev_priv)  ((IS_ENABLED(CONFIG_DRM_I915_PXP) && \
			    INTEL_INFO(dev_priv)->has_pxp) && \
			    VDBOX_MASK(to_gt(dev_priv)))

#define HAS_GMCH(dev_priv) (INTEL_INFO(dev_priv)->display.has_gmch)

#define HAS_LSPCON(dev_priv) (IS_DISPLAY_VER(dev_priv, 9, 10))

#define HAS_L3_CCS_READ(i915) (INTEL_INFO(i915)->has_l3_ccs_read)

/* DPF == dynamic parity feature */
#define HAS_L3_DPF(dev_priv) (INTEL_INFO(dev_priv)->has_l3_dpf)
#define NUM_L3_SLICES(dev_priv) (IS_HSW_GT3(dev_priv) ? \
				 2 : HAS_L3_DPF(dev_priv))

#define GT_FREQUENCY_MULTIPLIER 50
#define GEN9_FREQ_SCALER 3

#define INTEL_NUM_PIPES(dev_priv) (hweight8(RUNTIME_INFO(dev_priv)->pipe_mask))

#define HAS_DISPLAY(dev_priv) (RUNTIME_INFO(dev_priv)->pipe_mask != 0)

#define HAS_VRR(i915)	(DISPLAY_VER(i915) >= 11)

#define HAS_ASYNC_FLIPS(i915)		(DISPLAY_VER(i915) >= 5)

/* Only valid when HAS_DISPLAY() is true */
#define INTEL_DISPLAY_ENABLED(dev_priv) \
	(drm_WARN_ON(&(dev_priv)->drm, !HAS_DISPLAY(dev_priv)),		\
	 !(dev_priv)->params.disable_display &&				\
	 !intel_opregion_headless_sku(dev_priv))

#define HAS_GUC_DEPRIVILEGE(dev_priv) \
	(INTEL_INFO(dev_priv)->has_guc_deprivilege)

#define HAS_D12_PLANE_MINIMIZATION(dev_priv) (IS_ROCKETLAKE(dev_priv) || \
					      IS_ALDERLAKE_S(dev_priv))

#define HAS_MBUS_JOINING(i915) (IS_ALDERLAKE_P(i915) || DISPLAY_VER(i915) >= 14)

#define HAS_3D_PIPELINE(i915)	(INTEL_INFO(i915)->has_3d_pipeline)

#define HAS_ONE_EU_PER_FUSE_BIT(i915)	(INTEL_INFO(i915)->has_one_eu_per_fuse_bit)

/* intel_device_info.c */
static inline struct intel_device_info *
mkwrite_device_info(struct drm_i915_private *dev_priv)
{
	return (struct intel_device_info *)INTEL_INFO(dev_priv);
}

static inline enum i915_map_type
i915_coherent_map_type(struct drm_i915_private *i915,
		       struct drm_i915_gem_object *obj, bool always_coherent)
{
	if (i915_gem_object_is_lmem(obj))
		return I915_MAP_WC;
	if (HAS_LLC(i915) || always_coherent)
		return I915_MAP_WB;
	else
		return I915_MAP_WC;
}

#endif
