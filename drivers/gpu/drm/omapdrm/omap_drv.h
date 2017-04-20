/*
 * drivers/gpu/drm/omapdrm/omap_drv.h
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
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

#ifndef __OMAP_DRV_H__
#define __OMAP_DRV_H__

#include <linux/module.h>
#include <linux/platform_data/omap_drm.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem.h>
#include <drm/omap_drm.h>

#include "dss/omapdss.h"

#define DBG(fmt, ...) DRM_DEBUG(fmt"\n", ##__VA_ARGS__)
#define VERB(fmt, ...) if (0) DRM_DEBUG(fmt, ##__VA_ARGS__) /* verbose debug */

#define MODULE_NAME     "omapdrm"

struct omap_drm_usergart;

/* parameters which describe (unrotated) coordinates of scanout within a fb: */
struct omap_drm_window {
	uint32_t rotation;
	int32_t  crtc_x, crtc_y;		/* signed because can be offscreen */
	uint32_t crtc_w, crtc_h;
	uint32_t src_x, src_y;
	uint32_t src_w, src_h;
};

/* For KMS code that needs to wait for a certain # of IRQs:
 */
struct omap_irq_wait;
struct omap_irq_wait * omap_irq_wait_init(struct drm_device *dev,
		uint32_t irqmask, int count);
int omap_irq_wait(struct drm_device *dev, struct omap_irq_wait *wait,
		unsigned long timeout);

struct omap_drm_private {
	uint32_t omaprev;

	const struct dispc_ops *dispc_ops;

	unsigned int num_crtcs;
	struct drm_crtc *crtcs[8];

	unsigned int num_planes;
	struct drm_plane *planes[8];

	unsigned int num_encoders;
	struct drm_encoder *encoders[8];

	unsigned int num_connectors;
	struct drm_connector *connectors[8];

	struct drm_fb_helper *fbdev;

	struct workqueue_struct *wq;

	/* lock for obj_list below */
	spinlock_t list_lock;

	/* list of GEM objects: */
	struct list_head obj_list;

	struct omap_drm_usergart *usergart;
	bool has_dmm;

	/* properties: */
	struct drm_property *zorder_prop;

	/* irq handling: */
	spinlock_t wait_lock;		/* protects the wait_list */
	struct list_head wait_list;	/* list of omap_irq_wait */
	uint32_t irq_mask;		/* enabled irqs in addition to wait_list */
};


#ifdef CONFIG_DEBUG_FS
int omap_debugfs_init(struct drm_minor *minor);
void omap_framebuffer_describe(struct drm_framebuffer *fb, struct seq_file *m);
void omap_gem_describe(struct drm_gem_object *obj, struct seq_file *m);
void omap_gem_describe_objects(struct list_head *list, struct seq_file *m);
#endif

#ifdef CONFIG_PM
int omap_gem_resume(struct device *dev);
#endif

int omap_irq_enable_vblank(struct drm_crtc *crtc);
void omap_irq_disable_vblank(struct drm_crtc *crtc);
void omap_drm_irq_uninstall(struct drm_device *dev);
int omap_drm_irq_install(struct drm_device *dev);

#ifdef CONFIG_DRM_FBDEV_EMULATION
struct drm_fb_helper *omap_fbdev_init(struct drm_device *dev);
void omap_fbdev_free(struct drm_device *dev);
#else
static inline struct drm_fb_helper *omap_fbdev_init(struct drm_device *dev)
{
	return NULL;
}
static inline void omap_fbdev_free(struct drm_device *dev)
{
}
#endif

struct videomode *omap_crtc_timings(struct drm_crtc *crtc);
enum omap_channel omap_crtc_channel(struct drm_crtc *crtc);
void omap_crtc_pre_init(void);
void omap_crtc_pre_uninit(void);
struct drm_crtc *omap_crtc_init(struct drm_device *dev,
		struct drm_plane *plane, struct omap_dss_device *dssdev);
int omap_crtc_wait_pending(struct drm_crtc *crtc);
void omap_crtc_error_irq(struct drm_crtc *crtc, uint32_t irqstatus);
void omap_crtc_vblank_irq(struct drm_crtc *crtc);

struct drm_plane *omap_plane_init(struct drm_device *dev,
		int idx, enum drm_plane_type type,
		u32 possible_crtcs);
void omap_plane_install_properties(struct drm_plane *plane,
		struct drm_mode_object *obj);

struct drm_encoder *omap_encoder_init(struct drm_device *dev,
		struct omap_dss_device *dssdev);

struct drm_connector *omap_connector_init(struct drm_device *dev,
		int connector_type, struct omap_dss_device *dssdev,
		struct drm_encoder *encoder);
struct drm_encoder *omap_connector_attached_encoder(
		struct drm_connector *connector);
bool omap_connector_get_hdmi_mode(struct drm_connector *connector);

uint32_t omap_framebuffer_get_formats(uint32_t *pixel_formats,
		uint32_t max_formats, enum omap_color_mode supported_modes);
struct drm_framebuffer *omap_framebuffer_create(struct drm_device *dev,
		struct drm_file *file, const struct drm_mode_fb_cmd2 *mode_cmd);
struct drm_framebuffer *omap_framebuffer_init(struct drm_device *dev,
		const struct drm_mode_fb_cmd2 *mode_cmd, struct drm_gem_object **bos);
int omap_framebuffer_pin(struct drm_framebuffer *fb);
void omap_framebuffer_unpin(struct drm_framebuffer *fb);
void omap_framebuffer_update_scanout(struct drm_framebuffer *fb,
		struct omap_drm_window *win, struct omap_overlay_info *info);
struct drm_connector *omap_framebuffer_get_next_connector(
		struct drm_framebuffer *fb, struct drm_connector *from);
bool omap_framebuffer_supports_rotation(struct drm_framebuffer *fb);

void omap_gem_init(struct drm_device *dev);
void omap_gem_deinit(struct drm_device *dev);

struct drm_gem_object *omap_gem_new(struct drm_device *dev,
		union omap_gem_size gsize, uint32_t flags);
struct drm_gem_object *omap_gem_new_dmabuf(struct drm_device *dev, size_t size,
		struct sg_table *sgt);
int omap_gem_new_handle(struct drm_device *dev, struct drm_file *file,
		union omap_gem_size gsize, uint32_t flags, uint32_t *handle);
void omap_gem_free_object(struct drm_gem_object *obj);
void *omap_gem_vaddr(struct drm_gem_object *obj);
int omap_gem_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
		uint32_t handle, uint64_t *offset);
int omap_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
		struct drm_mode_create_dumb *args);
int omap_gem_mmap(struct file *filp, struct vm_area_struct *vma);
int omap_gem_mmap_obj(struct drm_gem_object *obj,
		struct vm_area_struct *vma);
int omap_gem_fault(struct vm_fault *vmf);
int omap_gem_roll(struct drm_gem_object *obj, uint32_t roll);
void omap_gem_cpu_sync(struct drm_gem_object *obj, int pgoff);
void omap_gem_dma_sync(struct drm_gem_object *obj,
		enum dma_data_direction dir);
int omap_gem_pin(struct drm_gem_object *obj, dma_addr_t *dma_addr);
void omap_gem_unpin(struct drm_gem_object *obj);
int omap_gem_get_pages(struct drm_gem_object *obj, struct page ***pages,
		bool remap);
int omap_gem_put_pages(struct drm_gem_object *obj);
uint32_t omap_gem_flags(struct drm_gem_object *obj);
int omap_gem_rotated_dma_addr(struct drm_gem_object *obj, uint32_t orient,
		int x, int y, dma_addr_t *dma_addr);
uint64_t omap_gem_mmap_offset(struct drm_gem_object *obj);
size_t omap_gem_mmap_size(struct drm_gem_object *obj);
int omap_gem_tiled_stride(struct drm_gem_object *obj, uint32_t orient);

struct dma_buf *omap_gem_prime_export(struct drm_device *dev,
		struct drm_gem_object *obj, int flags);
struct drm_gem_object *omap_gem_prime_import(struct drm_device *dev,
		struct dma_buf *buffer);

/* map crtc to vblank mask */
struct omap_dss_device *omap_encoder_get_dssdev(struct drm_encoder *encoder);

#endif /* __OMAP_DRV_H__ */
