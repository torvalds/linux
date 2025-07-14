/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#ifndef __MSM_DRV_H__
#define __MSM_DRV_H__

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/devfreq.h>
#include <linux/module.h>
#include <linux/component.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/iommu.h>
#include <linux/types.h>
#include <linux/of_graph.h>
#include <linux/of_device.h>
#include <linux/sizes.h>
#include <linux/kthread.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/display/drm_dsc.h>
#include <drm/msm_drm.h>
#include <drm/drm_gem.h>

extern struct fault_attr fail_gem_alloc;
extern struct fault_attr fail_gem_iova;

struct drm_fb_helper;
struct drm_fb_helper_surface_size;

struct msm_kms;
struct msm_gpu;
struct msm_mmu;
struct msm_mdss;
struct msm_rd_state;
struct msm_perf_state;
struct msm_gem_submit;
struct msm_fence_context;
struct msm_gem_address_space;
struct msm_gem_vma;
struct msm_disp_state;

#define MAX_CRTCS      8

#define FRAC_16_16(mult, div)    (((mult) << 16) / (div))

enum msm_dp_controller {
	MSM_DP_CONTROLLER_0,
	MSM_DP_CONTROLLER_1,
	MSM_DP_CONTROLLER_2,
	MSM_DP_CONTROLLER_3,
	MSM_DP_CONTROLLER_COUNT,
};

enum msm_dsi_controller {
	MSM_DSI_CONTROLLER_0,
	MSM_DSI_CONTROLLER_1,
	MSM_DSI_CONTROLLER_COUNT,
};

#define MSM_GPU_MAX_RINGS 4

/* Commit/Event thread specific structure */
struct msm_drm_thread {
	struct drm_device *dev;
	struct kthread_worker *worker;
};

struct msm_drm_private {

	struct drm_device *dev;

	struct msm_kms *kms;
	int (*kms_init)(struct drm_device *dev);

	/* subordinate devices, if present: */
	struct platform_device *gpu_pdev;

	/* possibly this should be in the kms component, but it is
	 * shared by both mdp4 and mdp5..
	 */
	struct hdmi *hdmi;

	/* DSI is shared by mdp4 and mdp5 */
	struct msm_dsi *dsi[MSM_DSI_CONTROLLER_COUNT];

	struct msm_dp *dp[MSM_DP_CONTROLLER_COUNT];

	/* when we have more than one 'msm_gpu' these need to be an array: */
	struct msm_gpu *gpu;

	/* gpu is only set on open(), but we need this info earlier */
	bool is_a2xx;
	bool has_cached_coherent;

	struct msm_rd_state *rd;       /* debugfs to dump all submits */
	struct msm_rd_state *hangrd;   /* debugfs to dump hanging submits */
	struct msm_perf_state *perf;

	/**
	 * total_mem: Total/global amount of memory backing GEM objects.
	 */
	atomic64_t total_mem;

	/**
	 * List of all GEM objects (mainly for debugfs, protected by obj_lock
	 * (acquire before per GEM object lock)
	 */
	struct list_head objects;
	struct mutex obj_lock;

	/**
	 * lru:
	 *
	 * The various LRU's that a GEM object is in at various stages of
	 * it's lifetime.  Objects start out in the unbacked LRU.  When
	 * pinned (for scannout or permanently mapped GPU buffers, like
	 * ringbuffer, memptr, fw, etc) it moves to the pinned LRU.  When
	 * unpinned, it moves into willneed or dontneed LRU depending on
	 * madvise state.  When backing pages are evicted (willneed) or
	 * purged (dontneed) it moves back into the unbacked LRU.
	 *
	 * The dontneed LRU is considered by the shrinker for objects
	 * that are candidate for purging, and the willneed LRU is
	 * considered for objects that could be evicted.
	 */
	struct {
		/**
		 * unbacked:
		 *
		 * The LRU for GEM objects without backing pages allocated.
		 * This mostly exists so that objects are always is one
		 * LRU.
		 */
		struct drm_gem_lru unbacked;

		/**
		 * pinned:
		 *
		 * The LRU for pinned GEM objects
		 */
		struct drm_gem_lru pinned;

		/**
		 * willneed:
		 *
		 * The LRU for unpinned GEM objects which are in madvise
		 * WILLNEED state (ie. can be evicted)
		 */
		struct drm_gem_lru willneed;

		/**
		 * dontneed:
		 *
		 * The LRU for unpinned GEM objects which are in madvise
		 * DONTNEED state (ie. can be purged)
		 */
		struct drm_gem_lru dontneed;

		/**
		 * lock:
		 *
		 * Protects manipulation of all of the LRUs.
		 */
		struct mutex lock;
	} lru;

	struct workqueue_struct *wq;

	unsigned int num_crtcs;

	struct msm_drm_thread event_thread[MAX_CRTCS];

	/* VRAM carveout, used when no IOMMU: */
	struct {
		unsigned long size;
		dma_addr_t paddr;
		/* NOTE: mm managed at the page level, size is in # of pages
		 * and position mm_node->start is in # of pages:
		 */
		struct drm_mm mm;
		spinlock_t lock; /* Protects drm_mm node allocation/removal */
	} vram;

	struct notifier_block vmap_notifier;
	struct shrinker *shrinker;

	/**
	 * hangcheck_period: For hang detection, in ms
	 *
	 * Note that in practice, a submit/job will get at least two hangcheck
	 * periods, due to checking for progress being implemented as simply
	 * "have the CP position registers changed since last time?"
	 */
	unsigned int hangcheck_period;

	/** gpu_devfreq_config: Devfreq tuning config for the GPU. */
	struct devfreq_simple_ondemand_data gpu_devfreq_config;

	/**
	 * gpu_clamp_to_idle: Enable clamping to idle freq when inactive
	 */
	bool gpu_clamp_to_idle;

	/**
	 * disable_err_irq:
	 *
	 * Disable handling of GPU hw error interrupts, to force fallback to
	 * sw hangcheck timer.  Written (via debugfs) by igt tests to test
	 * the sw hangcheck mechanism.
	 */
	bool disable_err_irq;

	/**
	 * @fault_stall_lock:
	 *
	 * Serialize changes to stall-on-fault state.
	 */
	spinlock_t fault_stall_lock;

	/**
	 * @fault_stall_reenable_time:
	 *
	 * If stall_enabled is false, when to reenable stall-on-fault.
	 * Protected by @fault_stall_lock.
	 */
	ktime_t stall_reenable_time;

	/**
	 * @stall_enabled:
	 *
	 * Whether stall-on-fault is currently enabled. Protected by
	 * @fault_stall_lock.
	 */
	bool stall_enabled;
};

const struct msm_format *mdp_get_format(struct msm_kms *kms, uint32_t format, uint64_t modifier);

struct msm_pending_timer;

int msm_atomic_init_pending_timer(struct msm_pending_timer *timer,
		struct msm_kms *kms, int crtc_idx);
void msm_atomic_destroy_pending_timer(struct msm_pending_timer *timer);
void msm_atomic_commit_tail(struct drm_atomic_state *state);
int msm_atomic_check(struct drm_device *dev, struct drm_atomic_state *state);
struct drm_atomic_state *msm_atomic_state_alloc(struct drm_device *dev);

int msm_crtc_enable_vblank(struct drm_crtc *crtc);
void msm_crtc_disable_vblank(struct drm_crtc *crtc);

int msm_register_mmu(struct drm_device *dev, struct msm_mmu *mmu);
void msm_unregister_mmu(struct drm_device *dev, struct msm_mmu *mmu);

struct msm_gem_address_space *msm_kms_init_aspace(struct drm_device *dev);
bool msm_use_mmu(struct drm_device *dev);

int msm_ioctl_gem_submit(struct drm_device *dev, void *data,
		struct drm_file *file);

#ifdef CONFIG_DEBUG_FS
unsigned long msm_gem_shrinker_shrink(struct drm_device *dev, unsigned long nr_to_scan);
#endif

int msm_gem_shrinker_init(struct drm_device *dev);
void msm_gem_shrinker_cleanup(struct drm_device *dev);

struct sg_table *msm_gem_prime_get_sg_table(struct drm_gem_object *obj);
int msm_gem_prime_vmap(struct drm_gem_object *obj, struct iosys_map *map);
void msm_gem_prime_vunmap(struct drm_gem_object *obj, struct iosys_map *map);
struct drm_gem_object *msm_gem_prime_import_sg_table(struct drm_device *dev,
		struct dma_buf_attachment *attach, struct sg_table *sg);
int msm_gem_prime_pin(struct drm_gem_object *obj);
void msm_gem_prime_unpin(struct drm_gem_object *obj);

int msm_framebuffer_prepare(struct drm_framebuffer *fb,
		struct msm_gem_address_space *aspace, bool needs_dirtyfb);
void msm_framebuffer_cleanup(struct drm_framebuffer *fb,
		struct msm_gem_address_space *aspace, bool needed_dirtyfb);
uint32_t msm_framebuffer_iova(struct drm_framebuffer *fb,
		struct msm_gem_address_space *aspace, int plane);
struct drm_gem_object *msm_framebuffer_bo(struct drm_framebuffer *fb, int plane);
const struct msm_format *msm_framebuffer_format(struct drm_framebuffer *fb);
struct drm_framebuffer *msm_framebuffer_create(struct drm_device *dev,
		struct drm_file *file, const struct drm_mode_fb_cmd2 *mode_cmd);
struct drm_framebuffer * msm_alloc_stolen_fb(struct drm_device *dev,
		int w, int h, int p, uint32_t format);

#ifdef CONFIG_DRM_FBDEV_EMULATION
int msm_fbdev_driver_fbdev_probe(struct drm_fb_helper *helper,
				 struct drm_fb_helper_surface_size *sizes);
#define MSM_FBDEV_DRIVER_OPS \
	.fbdev_probe = msm_fbdev_driver_fbdev_probe
#else
#define MSM_FBDEV_DRIVER_OPS \
	.fbdev_probe = NULL
#endif

struct hdmi;
#ifdef CONFIG_DRM_MSM_HDMI
int msm_hdmi_modeset_init(struct hdmi *hdmi, struct drm_device *dev,
		struct drm_encoder *encoder);
void __init msm_hdmi_register(void);
void __exit msm_hdmi_unregister(void);
#else
static inline int msm_hdmi_modeset_init(struct hdmi *hdmi, struct drm_device *dev,
		struct drm_encoder *encoder)
{
	return -EINVAL;
}
static inline void __init msm_hdmi_register(void) {}
static inline void __exit msm_hdmi_unregister(void) {}
#endif

struct msm_dsi;
#ifdef CONFIG_DRM_MSM_DSI
int dsi_dev_attach(struct platform_device *pdev);
void dsi_dev_detach(struct platform_device *pdev);
void __init msm_dsi_register(void);
void __exit msm_dsi_unregister(void);
int msm_dsi_modeset_init(struct msm_dsi *msm_dsi, struct drm_device *dev,
			 struct drm_encoder *encoder);
void msm_dsi_snapshot(struct msm_disp_state *disp_state, struct msm_dsi *msm_dsi);
bool msm_dsi_is_cmd_mode(struct msm_dsi *msm_dsi);
bool msm_dsi_is_bonded_dsi(struct msm_dsi *msm_dsi);
bool msm_dsi_is_master_dsi(struct msm_dsi *msm_dsi);
bool msm_dsi_wide_bus_enabled(struct msm_dsi *msm_dsi);
struct drm_dsc_config *msm_dsi_get_dsc_config(struct msm_dsi *msm_dsi);
const char *msm_dsi_get_te_source(struct msm_dsi *msm_dsi);
#else
static inline void __init msm_dsi_register(void)
{
}
static inline void __exit msm_dsi_unregister(void)
{
}
static inline int msm_dsi_modeset_init(struct msm_dsi *msm_dsi,
				       struct drm_device *dev,
				       struct drm_encoder *encoder)
{
	return -EINVAL;
}
static inline void msm_dsi_snapshot(struct msm_disp_state *disp_state, struct msm_dsi *msm_dsi)
{
}
static inline bool msm_dsi_is_cmd_mode(struct msm_dsi *msm_dsi)
{
	return false;
}
static inline bool msm_dsi_is_bonded_dsi(struct msm_dsi *msm_dsi)
{
	return false;
}
static inline bool msm_dsi_is_master_dsi(struct msm_dsi *msm_dsi)
{
	return false;
}
static inline bool msm_dsi_wide_bus_enabled(struct msm_dsi *msm_dsi)
{
	return false;
}

static inline struct drm_dsc_config *msm_dsi_get_dsc_config(struct msm_dsi *msm_dsi)
{
	return NULL;
}

static inline const char *msm_dsi_get_te_source(struct msm_dsi *msm_dsi)
{
	return NULL;
}
#endif

#ifdef CONFIG_DRM_MSM_DP
int __init msm_dp_register(void);
void __exit msm_dp_unregister(void);
int msm_dp_modeset_init(struct msm_dp *dp_display, struct drm_device *dev,
			 struct drm_encoder *encoder, bool yuv_supported);
void msm_dp_snapshot(struct msm_disp_state *disp_state, struct msm_dp *dp_display);
bool msm_dp_is_yuv_420_enabled(const struct msm_dp *dp_display,
			       const struct drm_display_mode *mode);
bool msm_dp_needs_periph_flush(const struct msm_dp *dp_display,
			       const struct drm_display_mode *mode);
bool msm_dp_wide_bus_available(const struct msm_dp *dp_display);

#else
static inline int __init msm_dp_register(void)
{
	return -EINVAL;
}
static inline void __exit msm_dp_unregister(void)
{
}
static inline int msm_dp_modeset_init(struct msm_dp *dp_display,
				       struct drm_device *dev,
				       struct drm_encoder *encoder,
				       bool yuv_supported)
{
	return -EINVAL;
}

static inline void msm_dp_snapshot(struct msm_disp_state *disp_state, struct msm_dp *dp_display)
{
}

static inline bool msm_dp_is_yuv_420_enabled(const struct msm_dp *dp_display,
					     const struct drm_display_mode *mode)
{
	return false;
}

static inline bool msm_dp_needs_periph_flush(const struct msm_dp *dp_display,
					     const struct drm_display_mode *mode)
{
	return false;
}

static inline bool msm_dp_wide_bus_available(const struct msm_dp *dp_display)
{
	return false;
}

#endif

#ifdef CONFIG_DRM_MSM_MDP4
void msm_mdp4_register(void);
void msm_mdp4_unregister(void);
#else
static inline void msm_mdp4_register(void) {}
static inline void msm_mdp4_unregister(void) {}
#endif

#ifdef CONFIG_DRM_MSM_MDP5
void msm_mdp_register(void);
void msm_mdp_unregister(void);
#else
static inline void msm_mdp_register(void) {}
static inline void msm_mdp_unregister(void) {}
#endif

#ifdef CONFIG_DRM_MSM_DPU
void msm_dpu_register(void);
void msm_dpu_unregister(void);
#else
static inline void msm_dpu_register(void) {}
static inline void msm_dpu_unregister(void) {}
#endif

#ifdef CONFIG_DRM_MSM_MDSS
void msm_mdss_register(void);
void msm_mdss_unregister(void);
#else
static inline void msm_mdss_register(void) {}
static inline void msm_mdss_unregister(void) {}
#endif

#ifdef CONFIG_DEBUG_FS
void msm_framebuffer_describe(struct drm_framebuffer *fb, struct seq_file *m);
int msm_debugfs_late_init(struct drm_device *dev);
int msm_rd_debugfs_init(struct drm_minor *minor);
void msm_rd_debugfs_cleanup(struct msm_drm_private *priv);
__printf(3, 4)
void msm_rd_dump_submit(struct msm_rd_state *rd, struct msm_gem_submit *submit,
		const char *fmt, ...);
int msm_perf_debugfs_init(struct drm_minor *minor);
void msm_perf_debugfs_cleanup(struct msm_drm_private *priv);
#else
static inline int msm_debugfs_late_init(struct drm_device *dev) { return 0; }
__printf(3, 4)
static inline void msm_rd_dump_submit(struct msm_rd_state *rd,
			struct msm_gem_submit *submit,
			const char *fmt, ...) {}
static inline void msm_rd_debugfs_cleanup(struct msm_drm_private *priv) {}
static inline void msm_perf_debugfs_cleanup(struct msm_drm_private *priv) {}
#endif

struct clk *msm_clk_get(struct platform_device *pdev, const char *name);

struct clk *msm_clk_bulk_get_clock(struct clk_bulk_data *bulk, int count,
	const char *name);
void __iomem *msm_ioremap(struct platform_device *pdev, const char *name);
void __iomem *msm_ioremap_size(struct platform_device *pdev, const char *name,
		phys_addr_t *size);
void __iomem *msm_ioremap_quiet(struct platform_device *pdev, const char *name);
void __iomem *msm_ioremap_mdss(struct platform_device *mdss_pdev,
			       struct platform_device *dev,
			       const char *name);

struct icc_path *msm_icc_get(struct device *dev, const char *name);

static inline void msm_rmw(void __iomem *addr, u32 mask, u32 or)
{
	u32 val = readl(addr);

	val &= ~mask;
	writel(val | or, addr);
}

/**
 * struct msm_hrtimer_work - a helper to combine an hrtimer with kthread_work
 *
 * @timer: hrtimer to control when the kthread work is triggered
 * @work:  the kthread work
 * @worker: the kthread worker the work will be scheduled on
 */
struct msm_hrtimer_work {
	struct hrtimer timer;
	struct kthread_work work;
	struct kthread_worker *worker;
};

void msm_hrtimer_queue_work(struct msm_hrtimer_work *work,
			    ktime_t wakeup_time,
			    enum hrtimer_mode mode);
void msm_hrtimer_work_init(struct msm_hrtimer_work *work,
			   struct kthread_worker *worker,
			   kthread_work_func_t fn,
			   clockid_t clock_id,
			   enum hrtimer_mode mode);

/* Helper for returning a UABI error with optional logging which can make
 * it easier for userspace to understand what it is doing wrong.
 */
#define UERR(err, drm, fmt, ...) \
	({ DRM_DEV_DEBUG_DRIVER((drm)->dev, fmt, ##__VA_ARGS__); -(err); })

#define DBG(fmt, ...) DRM_DEBUG_DRIVER(fmt"\n", ##__VA_ARGS__)
#define VERB(fmt, ...) if (0) DRM_DEBUG_DRIVER(fmt"\n", ##__VA_ARGS__)

static inline int align_pitch(int width, int bpp)
{
	int bytespp = (bpp + 7) / 8;
	/* adreno needs pitch aligned to 32 pixels: */
	return bytespp * ALIGN(width, 32);
}

/* for the generated headers: */
#define INVALID_IDX(idx) ({BUG(); 0;})
#define fui(x)                ({BUG(); 0;})
#define _mesa_float_to_half(x) ({BUG(); 0;})


#define FIELD(val, name) (((val) & name ## __MASK) >> name ## __SHIFT)

/* for conditionally setting boolean flag(s): */
#define COND(bool, val) ((bool) ? (val) : 0)

static inline unsigned long timeout_to_jiffies(const ktime_t *timeout)
{
	ktime_t now = ktime_get();

	if (ktime_compare(*timeout, now) <= 0)
		return 0;

	ktime_t rem = ktime_sub(*timeout, now);
	s64 remaining_jiffies = ktime_divns(rem, NSEC_PER_SEC / HZ);
	return clamp(remaining_jiffies, 1LL, (s64)INT_MAX);
}

/* Driver helpers */

extern const struct component_master_ops msm_drm_ops;

int msm_kms_pm_prepare(struct device *dev);
void msm_kms_pm_complete(struct device *dev);

int msm_drv_probe(struct device *dev,
	int (*kms_init)(struct drm_device *dev),
	struct msm_kms *kms);
void msm_kms_shutdown(struct platform_device *pdev);

bool msm_disp_drv_should_bind(struct device *dev, bool dpu_driver);

#endif /* __MSM_DRV_H__ */
