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
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/msm_drm.h>
#include <drm/drm_gem.h>

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
#define MAX_PLANES     20
#define MAX_ENCODERS   8
#define MAX_BRIDGES    8
#define MAX_CONNECTORS 8

#define FRAC_16_16(mult, div)    (((mult) << 16) / (div))

enum msm_mdp_plane_property {
	PLANE_PROP_ZPOS,
	PLANE_PROP_ALPHA,
	PLANE_PROP_PREMULTIPLIED,
	PLANE_PROP_MAX_NUM
};

enum msm_dp_controller {
	MSM_DP_CONTROLLER_0,
	MSM_DP_CONTROLLER_1,
	MSM_DP_CONTROLLER_2,
	MSM_DP_CONTROLLER_COUNT,
};

#define MSM_GPU_MAX_RINGS 4
#define MAX_H_TILES_PER_DISPLAY 2

/**
 * enum msm_display_caps - features/capabilities supported by displays
 * @MSM_DISPLAY_CAP_VID_MODE:           Video or "active" mode supported
 * @MSM_DISPLAY_CAP_CMD_MODE:           Command mode supported
 * @MSM_DISPLAY_CAP_HOT_PLUG:           Hot plug detection supported
 * @MSM_DISPLAY_CAP_EDID:               EDID supported
 */
enum msm_display_caps {
	MSM_DISPLAY_CAP_VID_MODE	= BIT(0),
	MSM_DISPLAY_CAP_CMD_MODE	= BIT(1),
	MSM_DISPLAY_CAP_HOT_PLUG	= BIT(2),
	MSM_DISPLAY_CAP_EDID		= BIT(3),
};

/**
 * enum msm_event_wait - type of HW events to wait for
 * @MSM_ENC_COMMIT_DONE - wait for the driver to flush the registers to HW
 * @MSM_ENC_TX_COMPLETE - wait for the HW to transfer the frame to panel
 * @MSM_ENC_VBLANK - wait for the HW VBLANK event (for driver-internal waiters)
 */
enum msm_event_wait {
	MSM_ENC_COMMIT_DONE = 0,
	MSM_ENC_TX_COMPLETE,
	MSM_ENC_VBLANK,
};

/**
 * struct msm_display_topology - defines a display topology pipeline
 * @num_lm:       number of layer mixers used
 * @num_enc:      number of compression encoder blocks used
 * @num_intf:     number of interfaces the panel is mounted on
 */
struct msm_display_topology {
	u32 num_lm;
	u32 num_enc;
	u32 num_intf;
	u32 num_dspp;
};

/* Commit/Event thread specific structure */
struct msm_drm_thread {
	struct drm_device *dev;
	unsigned int crtc_id;
	struct kthread_worker *worker;
};

struct msm_drm_private {

	struct drm_device *dev;

	struct msm_kms *kms;

	/* subordinate devices, if present: */
	struct platform_device *gpu_pdev;

	/* top level MDSS wrapper device (for MDP5/DPU only) */
	struct msm_mdss *mdss;

	/* possibly this should be in the kms component, but it is
	 * shared by both mdp4 and mdp5..
	 */
	struct hdmi *hdmi;

	/* DSI is shared by mdp4 and mdp5 */
	struct msm_dsi *dsi[2];

	struct msm_dp *dp[MSM_DP_CONTROLLER_COUNT];

	/* when we have more than one 'msm_gpu' these need to be an array: */
	struct msm_gpu *gpu;

	/* gpu is only set on open(), but we need this info earlier */
	bool is_a2xx;
	bool has_cached_coherent;

	struct drm_fb_helper *fbdev;

	struct msm_rd_state *rd;       /* debugfs to dump all submits */
	struct msm_rd_state *hangrd;   /* debugfs to dump hanging submits */
	struct msm_perf_state *perf;

	/**
	 * List of all GEM objects (mainly for debugfs, protected by obj_lock
	 * (acquire before per GEM object lock)
	 */
	struct list_head objects;
	struct mutex obj_lock;

	/**
	 * LRUs of inactive GEM objects.  Every bo is either in one of the
	 * inactive lists (depending on whether or not it is shrinkable) or
	 * gpu->active_list (for the gpu it is active on[1]), or transiently
	 * on a temporary list as the shrinker is running.
	 *
	 * Note that inactive_willneed also contains pinned and vmap'd bos,
	 * but the number of pinned-but-not-active objects is small (scanout
	 * buffers, ringbuffer, etc).
	 *
	 * These lists are protected by mm_lock (which should be acquired
	 * before per GEM object lock).  One should *not* hold mm_lock in
	 * get_pages()/vmap()/etc paths, as they can trigger the shrinker.
	 *
	 * [1] if someone ever added support for the old 2d cores, there could be
	 *     more than one gpu object
	 */
	struct list_head inactive_willneed;  /* inactive + potentially unpin/evictable */
	struct list_head inactive_dontneed;  /* inactive + shrinkable */
	struct list_head inactive_unpinned;  /* inactive + purged or unpinned */
	long shrinkable_count;               /* write access under mm_lock */
	long evictable_count;                /* write access under mm_lock */
	struct mutex mm_lock;

	struct workqueue_struct *wq;

	unsigned int num_planes;
	struct drm_plane *planes[MAX_PLANES];

	unsigned int num_crtcs;
	struct drm_crtc *crtcs[MAX_CRTCS];

	struct msm_drm_thread event_thread[MAX_CRTCS];

	unsigned int num_encoders;
	struct drm_encoder *encoders[MAX_ENCODERS];

	unsigned int num_bridges;
	struct drm_bridge *bridges[MAX_BRIDGES];

	unsigned int num_connectors;
	struct drm_connector *connectors[MAX_CONNECTORS];

	/* Properties */
	struct drm_property *plane_property[PLANE_PROP_MAX_NUM];

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
	struct shrinker shrinker;

	struct drm_atomic_state *pm_state;

	/* For hang detection, in ms */
	unsigned int hangcheck_period;

	/**
	 * disable_err_irq:
	 *
	 * Disable handling of GPU hw error interrupts, to force fallback to
	 * sw hangcheck timer.  Written (via debugfs) by igt tests to test
	 * the sw hangcheck mechanism.
	 */
	bool disable_err_irq;
};

struct msm_format {
	uint32_t pixel_format;
};

struct msm_pending_timer;

int msm_atomic_prepare_fb(struct drm_plane *plane,
			  struct drm_plane_state *new_state);
int msm_atomic_init_pending_timer(struct msm_pending_timer *timer,
		struct msm_kms *kms, int crtc_idx);
void msm_atomic_destroy_pending_timer(struct msm_pending_timer *timer);
void msm_atomic_commit_tail(struct drm_atomic_state *state);
struct drm_atomic_state *msm_atomic_state_alloc(struct drm_device *dev);
void msm_atomic_state_clear(struct drm_atomic_state *state);
void msm_atomic_state_free(struct drm_atomic_state *state);

int msm_crtc_enable_vblank(struct drm_crtc *crtc);
void msm_crtc_disable_vblank(struct drm_crtc *crtc);

int msm_gem_init_vma(struct msm_gem_address_space *aspace,
		struct msm_gem_vma *vma, int npages,
		u64 range_start, u64 range_end);
void msm_gem_purge_vma(struct msm_gem_address_space *aspace,
		struct msm_gem_vma *vma);
void msm_gem_unmap_vma(struct msm_gem_address_space *aspace,
		struct msm_gem_vma *vma);
int msm_gem_map_vma(struct msm_gem_address_space *aspace,
		struct msm_gem_vma *vma, int prot,
		struct sg_table *sgt, int npages);
void msm_gem_close_vma(struct msm_gem_address_space *aspace,
		struct msm_gem_vma *vma);


struct msm_gem_address_space *
msm_gem_address_space_get(struct msm_gem_address_space *aspace);

void msm_gem_address_space_put(struct msm_gem_address_space *aspace);

struct msm_gem_address_space *
msm_gem_address_space_create(struct msm_mmu *mmu, const char *name,
		u64 va_start, u64 size);

int msm_register_mmu(struct drm_device *dev, struct msm_mmu *mmu);
void msm_unregister_mmu(struct drm_device *dev, struct msm_mmu *mmu);

bool msm_use_mmu(struct drm_device *dev);

int msm_ioctl_gem_submit(struct drm_device *dev, void *data,
		struct drm_file *file);

#ifdef CONFIG_DEBUG_FS
unsigned long msm_gem_shrinker_shrink(struct drm_device *dev, unsigned long nr_to_scan);
#endif

void msm_gem_shrinker_init(struct drm_device *dev);
void msm_gem_shrinker_cleanup(struct drm_device *dev);

struct sg_table *msm_gem_prime_get_sg_table(struct drm_gem_object *obj);
int msm_gem_prime_vmap(struct drm_gem_object *obj, struct dma_buf_map *map);
void msm_gem_prime_vunmap(struct drm_gem_object *obj, struct dma_buf_map *map);
struct drm_gem_object *msm_gem_prime_import_sg_table(struct drm_device *dev,
		struct dma_buf_attachment *attach, struct sg_table *sg);
int msm_gem_prime_pin(struct drm_gem_object *obj);
void msm_gem_prime_unpin(struct drm_gem_object *obj);

int msm_framebuffer_prepare(struct drm_framebuffer *fb,
		struct msm_gem_address_space *aspace);
void msm_framebuffer_cleanup(struct drm_framebuffer *fb,
		struct msm_gem_address_space *aspace);
uint32_t msm_framebuffer_iova(struct drm_framebuffer *fb,
		struct msm_gem_address_space *aspace, int plane);
struct drm_gem_object *msm_framebuffer_bo(struct drm_framebuffer *fb, int plane);
const struct msm_format *msm_framebuffer_format(struct drm_framebuffer *fb);
struct drm_framebuffer *msm_framebuffer_create(struct drm_device *dev,
		struct drm_file *file, const struct drm_mode_fb_cmd2 *mode_cmd);
struct drm_framebuffer * msm_alloc_stolen_fb(struct drm_device *dev,
		int w, int h, int p, uint32_t format);

struct drm_fb_helper *msm_fbdev_init(struct drm_device *dev);
void msm_fbdev_free(struct drm_device *dev);

struct hdmi;
int msm_hdmi_modeset_init(struct hdmi *hdmi, struct drm_device *dev,
		struct drm_encoder *encoder);
void __init msm_hdmi_register(void);
void __exit msm_hdmi_unregister(void);

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
#endif

#ifdef CONFIG_DRM_MSM_DP
int __init msm_dp_register(void);
void __exit msm_dp_unregister(void);
int msm_dp_modeset_init(struct msm_dp *dp_display, struct drm_device *dev,
			 struct drm_encoder *encoder);
int msm_dp_display_enable(struct msm_dp *dp, struct drm_encoder *encoder);
int msm_dp_display_disable(struct msm_dp *dp, struct drm_encoder *encoder);
int msm_dp_display_pre_disable(struct msm_dp *dp, struct drm_encoder *encoder);
void msm_dp_display_mode_set(struct msm_dp *dp, struct drm_encoder *encoder,
				const struct drm_display_mode *mode,
				const struct drm_display_mode *adjusted_mode);

struct drm_bridge *msm_dp_bridge_init(struct msm_dp *dp_display,
					struct drm_device *dev,
					struct drm_encoder *encoder);
void msm_dp_irq_postinstall(struct msm_dp *dp_display);
void msm_dp_snapshot(struct msm_disp_state *disp_state, struct msm_dp *dp_display);

void msm_dp_debugfs_init(struct msm_dp *dp_display, struct drm_minor *minor);

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
				       struct drm_encoder *encoder)
{
	return -EINVAL;
}
static inline int msm_dp_display_enable(struct msm_dp *dp,
					struct drm_encoder *encoder)
{
	return -EINVAL;
}
static inline int msm_dp_display_disable(struct msm_dp *dp,
					struct drm_encoder *encoder)
{
	return -EINVAL;
}
static inline int msm_dp_display_pre_disable(struct msm_dp *dp,
					struct drm_encoder *encoder)
{
	return -EINVAL;
}
static inline void msm_dp_display_mode_set(struct msm_dp *dp,
				struct drm_encoder *encoder,
				const struct drm_display_mode *mode,
				const struct drm_display_mode *adjusted_mode)
{
}

static inline void msm_dp_irq_postinstall(struct msm_dp *dp_display)
{
}

static inline void msm_dp_snapshot(struct msm_disp_state *disp_state, struct msm_dp *dp_display)
{
}

static inline void msm_dp_debugfs_init(struct msm_dp *dp_display,
		struct drm_minor *minor)
{
}

#endif

void __init msm_mdp_register(void);
void __exit msm_mdp_unregister(void);
void __init msm_dpu_register(void);
void __exit msm_dpu_unregister(void);

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

#define msm_writel(data, addr) writel((data), (addr))
#define msm_readl(addr) readl((addr))

static inline void msm_rmw(void __iomem *addr, u32 mask, u32 or)
{
	u32 val = msm_readl(addr);

	val &= ~mask;
	msm_writel(val | or, addr);
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
	s64 remaining_jiffies;

	if (ktime_compare(*timeout, now) < 0) {
		remaining_jiffies = 0;
	} else {
		ktime_t rem = ktime_sub(*timeout, now);
		remaining_jiffies = ktime_divns(rem, NSEC_PER_SEC / HZ);
	}

	return clamp(remaining_jiffies, 0LL, (s64)INT_MAX);
}

#endif /* __MSM_DRV_H__ */
