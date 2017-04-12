/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
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
#include <asm/sizes.h>

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>
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
struct msm_fence_cb;

#define NUM_DOMAINS 2    /* one for KMS, then one per gpu core (?) */

struct msm_file_private {
	/* currently we don't do anything useful with this.. but when
	 * per-context address spaces are supported we'd keep track of
	 * the context's page-tables here.
	 */
	int dummy;
};

enum msm_mdp_plane_property {
	PLANE_PROP_ZPOS,
	PLANE_PROP_ALPHA,
	PLANE_PROP_PREMULTIPLIED,
	PLANE_PROP_MAX_NUM
};

struct msm_vblank_ctrl {
	struct work_struct work;
	struct list_head event_list;
	spinlock_t lock;
};

struct msm_drm_private {

	struct drm_device *dev;

	struct msm_kms *kms;

	/* subordinate devices, if present: */
	struct platform_device *gpu_pdev;

	/* top level MDSS wrapper device (for MDP5 only) */
	struct msm_mdss *mdss;

	/* possibly this should be in the kms component, but it is
	 * shared by both mdp4 and mdp5..
	 */
	struct hdmi *hdmi;

	/* eDP is for mdp5 only, but kms has not been created
	 * when edp_bind() and edp_init() are called. Here is the only
	 * place to keep the edp instance.
	 */
	struct msm_edp *edp;

	/* DSI is shared by mdp4 and mdp5 */
	struct msm_dsi *dsi[2];

	/* when we have more than one 'msm_gpu' these need to be an array: */
	struct msm_gpu *gpu;
	struct msm_file_private *lastctx;

	struct drm_fb_helper *fbdev;

	struct msm_rd_state *rd;
	struct msm_perf_state *perf;

	/* list of GEM objects: */
	struct list_head inactive_list;

	struct workqueue_struct *wq;
	struct workqueue_struct *atomic_wq;

	/* crtcs pending async atomic updates: */
	uint32_t pending_crtcs;
	wait_queue_head_t pending_crtcs_event;

	/* registered MMUs: */
	unsigned int num_mmus;
	struct msm_mmu *mmus[NUM_DOMAINS];

	unsigned int num_planes;
	struct drm_plane *planes[8];

	unsigned int num_crtcs;
	struct drm_crtc *crtcs[8];

	unsigned int num_encoders;
	struct drm_encoder *encoders[8];

	unsigned int num_bridges;
	struct drm_bridge *bridges[8];

	unsigned int num_connectors;
	struct drm_connector *connectors[8];

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
	} vram;

	struct notifier_block vmap_notifier;
	struct shrinker shrinker;

	struct msm_vblank_ctrl vblank_ctrl;

	/* task holding struct_mutex.. currently only used in submit path
	 * to detect and reject faults from copy_from_user() for submit
	 * ioctl.
	 */
	struct task_struct *struct_mutex_task;
};

struct msm_format {
	uint32_t pixel_format;
};

int msm_atomic_check(struct drm_device *dev,
		     struct drm_atomic_state *state);
int msm_atomic_commit(struct drm_device *dev,
		struct drm_atomic_state *state, bool nonblock);

int msm_register_mmu(struct drm_device *dev, struct msm_mmu *mmu);

void msm_gem_submit_free(struct msm_gem_submit *submit);
int msm_ioctl_gem_submit(struct drm_device *dev, void *data,
		struct drm_file *file);

void msm_gem_shrinker_init(struct drm_device *dev);
void msm_gem_shrinker_cleanup(struct drm_device *dev);

int msm_gem_mmap_obj(struct drm_gem_object *obj,
			struct vm_area_struct *vma);
int msm_gem_mmap(struct file *filp, struct vm_area_struct *vma);
int msm_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf);
uint64_t msm_gem_mmap_offset(struct drm_gem_object *obj);
int msm_gem_get_iova_locked(struct drm_gem_object *obj, int id,
		uint32_t *iova);
int msm_gem_get_iova(struct drm_gem_object *obj, int id, uint32_t *iova);
uint32_t msm_gem_iova(struct drm_gem_object *obj, int id);
struct page **msm_gem_get_pages(struct drm_gem_object *obj);
void msm_gem_put_pages(struct drm_gem_object *obj);
void msm_gem_put_iova(struct drm_gem_object *obj, int id);
int msm_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
		struct drm_mode_create_dumb *args);
int msm_gem_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
		uint32_t handle, uint64_t *offset);
struct sg_table *msm_gem_prime_get_sg_table(struct drm_gem_object *obj);
void *msm_gem_prime_vmap(struct drm_gem_object *obj);
void msm_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr);
int msm_gem_prime_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma);
struct reservation_object *msm_gem_prime_res_obj(struct drm_gem_object *obj);
struct drm_gem_object *msm_gem_prime_import_sg_table(struct drm_device *dev,
		struct dma_buf_attachment *attach, struct sg_table *sg);
int msm_gem_prime_pin(struct drm_gem_object *obj);
void msm_gem_prime_unpin(struct drm_gem_object *obj);
void *msm_gem_get_vaddr_locked(struct drm_gem_object *obj);
void *msm_gem_get_vaddr(struct drm_gem_object *obj);
void msm_gem_put_vaddr_locked(struct drm_gem_object *obj);
void msm_gem_put_vaddr(struct drm_gem_object *obj);
int msm_gem_madvise(struct drm_gem_object *obj, unsigned madv);
void msm_gem_purge(struct drm_gem_object *obj);
void msm_gem_vunmap(struct drm_gem_object *obj);
int msm_gem_sync_object(struct drm_gem_object *obj,
		struct msm_fence_context *fctx, bool exclusive);
void msm_gem_move_to_active(struct drm_gem_object *obj,
		struct msm_gpu *gpu, bool exclusive, struct fence *fence);
void msm_gem_move_to_inactive(struct drm_gem_object *obj);
int msm_gem_cpu_prep(struct drm_gem_object *obj, uint32_t op, ktime_t *timeout);
int msm_gem_cpu_fini(struct drm_gem_object *obj);
void msm_gem_free_object(struct drm_gem_object *obj);
int msm_gem_new_handle(struct drm_device *dev, struct drm_file *file,
		uint32_t size, uint32_t flags, uint32_t *handle);
struct drm_gem_object *msm_gem_new(struct drm_device *dev,
		uint32_t size, uint32_t flags);
struct drm_gem_object *msm_gem_import(struct drm_device *dev,
		struct dma_buf *dmabuf, struct sg_table *sgt);

int msm_framebuffer_prepare(struct drm_framebuffer *fb, int id);
void msm_framebuffer_cleanup(struct drm_framebuffer *fb, int id);
uint32_t msm_framebuffer_iova(struct drm_framebuffer *fb, int id, int plane);
struct drm_gem_object *msm_framebuffer_bo(struct drm_framebuffer *fb, int plane);
const struct msm_format *msm_framebuffer_format(struct drm_framebuffer *fb);
struct drm_framebuffer *msm_framebuffer_init(struct drm_device *dev,
		const struct drm_mode_fb_cmd2 *mode_cmd, struct drm_gem_object **bos);
struct drm_framebuffer *msm_framebuffer_create(struct drm_device *dev,
		struct drm_file *file, const struct drm_mode_fb_cmd2 *mode_cmd);

struct drm_fb_helper *msm_fbdev_init(struct drm_device *dev);
void msm_fbdev_free(struct drm_device *dev);

struct hdmi;
int msm_hdmi_modeset_init(struct hdmi *hdmi, struct drm_device *dev,
		struct drm_encoder *encoder);
void __init msm_hdmi_register(void);
void __exit msm_hdmi_unregister(void);

struct msm_edp;
void __init msm_edp_register(void);
void __exit msm_edp_unregister(void);
int msm_edp_modeset_init(struct msm_edp *edp, struct drm_device *dev,
		struct drm_encoder *encoder);

struct msm_dsi;
enum msm_dsi_encoder_id {
	MSM_DSI_VIDEO_ENCODER_ID = 0,
	MSM_DSI_CMD_ENCODER_ID = 1,
	MSM_DSI_ENCODER_NUM = 2
};
#ifdef CONFIG_DRM_MSM_DSI
void __init msm_dsi_register(void);
void __exit msm_dsi_unregister(void);
int msm_dsi_modeset_init(struct msm_dsi *msm_dsi, struct drm_device *dev,
		struct drm_encoder *encoders[MSM_DSI_ENCODER_NUM]);
#else
static inline void __init msm_dsi_register(void)
{
}
static inline void __exit msm_dsi_unregister(void)
{
}
static inline int msm_dsi_modeset_init(struct msm_dsi *msm_dsi,
		struct drm_device *dev,
		struct drm_encoder *encoders[MSM_DSI_ENCODER_NUM])
{
	return -EINVAL;
}
#endif

void __init msm_mdp_register(void);
void __exit msm_mdp_unregister(void);

#ifdef CONFIG_DEBUG_FS
void msm_gem_describe(struct drm_gem_object *obj, struct seq_file *m);
void msm_gem_describe_objects(struct list_head *list, struct seq_file *m);
void msm_framebuffer_describe(struct drm_framebuffer *fb, struct seq_file *m);
int msm_debugfs_late_init(struct drm_device *dev);
int msm_rd_debugfs_init(struct drm_minor *minor);
void msm_rd_debugfs_cleanup(struct drm_minor *minor);
void msm_rd_dump_submit(struct msm_gem_submit *submit);
int msm_perf_debugfs_init(struct drm_minor *minor);
void msm_perf_debugfs_cleanup(struct drm_minor *minor);
#else
static inline int msm_debugfs_late_init(struct drm_device *dev) { return 0; }
static inline void msm_rd_dump_submit(struct msm_gem_submit *submit) {}
#endif

void __iomem *msm_ioremap(struct platform_device *pdev, const char *name,
		const char *dbgname);
void msm_writel(u32 data, void __iomem *addr);
u32 msm_readl(const void __iomem *addr);

#define DBG(fmt, ...) DRM_DEBUG(fmt"\n", ##__VA_ARGS__)
#define VERB(fmt, ...) if (0) DRM_DEBUG(fmt"\n", ##__VA_ARGS__)

static inline int align_pitch(int width, int bpp)
{
	int bytespp = (bpp + 7) / 8;
	/* adreno needs pitch aligned to 32 pixels: */
	return bytespp * ALIGN(width, 32);
}

/* for the generated headers: */
#define INVALID_IDX(idx) ({BUG(); 0;})
#define fui(x)                ({BUG(); 0;})
#define util_float_to_half(x) ({BUG(); 0;})


#define FIELD(val, name) (((val) & name ## __MASK) >> name ## __SHIFT)

/* for conditionally setting boolean flag(s): */
#define COND(bool, val) ((bool) ? (val) : 0)

static inline unsigned long timeout_to_jiffies(const ktime_t *timeout)
{
	ktime_t now = ktime_get();
	unsigned long remaining_jiffies;

	if (ktime_compare(*timeout, now) < 0) {
		remaining_jiffies = 0;
	} else {
		ktime_t rem = ktime_sub(*timeout, now);
		struct timespec ts = ktime_to_timespec(rem);
		remaining_jiffies = timespec_to_jiffies(&ts);
	}

	return remaining_jiffies;
}

#endif /* __MSM_DRV_H__ */
