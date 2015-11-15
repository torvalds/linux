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
#include <linux/wait.h>
#include <video/omapdss.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem.h>
#include <drm/omap_drm.h>

#define DBG(fmt, ...) DRM_DEBUG(fmt"\n", ##__VA_ARGS__)
#define VERB(fmt, ...) if (0) DRM_DEBUG(fmt, ##__VA_ARGS__) /* verbose debug */

#define MODULE_NAME     "omapdrm"

/* max # of mapper-id's that can be assigned.. todo, come up with a better
 * (but still inexpensive) way to store/access per-buffer mapper private
 * data..
 */
#define MAX_MAPPERS 2

/* parameters which describe (unrotated) coordinates of scanout within a fb: */
struct omap_drm_window {
	uint32_t rotation;
	int32_t  crtc_x, crtc_y;		/* signed because can be offscreen */
	uint32_t crtc_w, crtc_h;
	uint32_t src_x, src_y;
	uint32_t src_w, src_h;
};

/* For transiently registering for different DSS irqs that various parts
 * of the KMS code need during setup/configuration.  We these are not
 * necessarily the same as what drm_vblank_get/put() are requesting, and
 * the hysteresis in drm_vblank_put() is not necessarily desirable for
 * internal housekeeping related irq usage.
 */
struct omap_drm_irq {
	struct list_head node;
	uint32_t irqmask;
	bool registered;
	void (*irq)(struct omap_drm_irq *irq, uint32_t irqstatus);
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

	bool has_dmm;

	/* properties: */
	struct drm_property *zorder_prop;

	/* irq handling: */
	struct list_head irq_list;    /* list of omap_drm_irq */
	uint32_t vblank_mask;         /* irq bits set for userspace vblank */
	struct omap_drm_irq error_handler;

	/* atomic commit */
	struct {
		struct list_head events;
		wait_queue_head_t wait;
		u32 pending;
		spinlock_t lock;	/* Protects commit.pending */
	} commit;
};


#ifdef CONFIG_DEBUG_FS
int omap_debugfs_init(struct drm_minor *minor);
void omap_debugfs_cleanup(struct drm_minor *minor);
void omap_framebuffer_describe(struct drm_framebuffer *fb, struct seq_file *m);
void omap_gem_describe(struct drm_gem_object *obj, struct seq_file *m);
void omap_gem_describe_objects(struct list_head *list, struct seq_file *m);
#endif

#ifdef CONFIG_PM
int omap_gem_resume(struct device *dev);
#endif

int omap_irq_enable_vblank(struct drm_device *dev, unsigned int pipe);
void omap_irq_disable_vblank(struct drm_device *dev, unsigned int pipe);
void __omap_irq_register(struct drm_device *dev, struct omap_drm_irq *irq);
void __omap_irq_unregister(struct drm_device *dev, struct omap_drm_irq *irq);
void omap_irq_register(struct drm_device *dev, struct omap_drm_irq *irq);
void omap_irq_unregister(struct drm_device *dev, struct omap_drm_irq *irq);
void omap_drm_irq_uninstall(struct drm_device *dev);
int omap_drm_irq_install(struct drm_device *dev);

struct drm_fb_helper *omap_fbdev_init(struct drm_device *dev);
void omap_fbdev_free(struct drm_device *dev);

struct omap_video_timings *omap_crtc_timings(struct drm_crtc *crtc);
enum omap_channel omap_crtc_channel(struct drm_crtc *crtc);
void omap_crtc_pre_init(void);
void omap_crtc_pre_uninit(void);
struct drm_crtc *omap_crtc_init(struct drm_device *dev,
		struct drm_plane *plane, enum omap_channel channel, int id);
int omap_crtc_wait_pending(struct drm_crtc *crtc);

struct drm_plane *omap_plane_init(struct drm_device *dev,
		int id, enum drm_plane_type type);
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

void copy_timings_omap_to_drm(struct drm_display_mode *mode,
		struct omap_video_timings *timings);
void copy_timings_drm_to_omap(struct omap_video_timings *timings,
		struct drm_display_mode *mode);

uint32_t omap_framebuffer_get_formats(uint32_t *pixel_formats,
		uint32_t max_formats, enum omap_color_mode supported_modes);
struct drm_framebuffer *omap_framebuffer_create(struct drm_device *dev,
		struct drm_file *file, struct drm_mode_fb_cmd2 *mode_cmd);
struct drm_framebuffer *omap_framebuffer_init(struct drm_device *dev,
		struct drm_mode_fb_cmd2 *mode_cmd, struct drm_gem_object **bos);
struct drm_gem_object *omap_framebuffer_bo(struct drm_framebuffer *fb, int p);
int omap_framebuffer_pin(struct drm_framebuffer *fb);
void omap_framebuffer_unpin(struct drm_framebuffer *fb);
void omap_framebuffer_update_scanout(struct drm_framebuffer *fb,
		struct omap_drm_window *win, struct omap_overlay_info *info);
struct drm_connector *omap_framebuffer_get_next_connector(
		struct drm_framebuffer *fb, struct drm_connector *from);

void omap_gem_init(struct drm_device *dev);
void omap_gem_deinit(struct drm_device *dev);

struct drm_gem_object *omap_gem_new(struct drm_device *dev,
		union omap_gem_size gsize, uint32_t flags);
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
int omap_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf);
int omap_gem_op_start(struct drm_gem_object *obj, enum omap_gem_op op);
int omap_gem_op_finish(struct drm_gem_object *obj, enum omap_gem_op op);
int omap_gem_op_sync(struct drm_gem_object *obj, enum omap_gem_op op);
int omap_gem_op_async(struct drm_gem_object *obj, enum omap_gem_op op,
		void (*fxn)(void *arg), void *arg);
int omap_gem_roll(struct drm_gem_object *obj, uint32_t roll);
void omap_gem_cpu_sync(struct drm_gem_object *obj, int pgoff);
void omap_gem_dma_sync(struct drm_gem_object *obj,
		enum dma_data_direction dir);
int omap_gem_get_paddr(struct drm_gem_object *obj,
		dma_addr_t *paddr, bool remap);
void omap_gem_put_paddr(struct drm_gem_object *obj);
int omap_gem_get_pages(struct drm_gem_object *obj, struct page ***pages,
		bool remap);
int omap_gem_put_pages(struct drm_gem_object *obj);
uint32_t omap_gem_flags(struct drm_gem_object *obj);
int omap_gem_rotated_paddr(struct drm_gem_object *obj, uint32_t orient,
		int x, int y, dma_addr_t *paddr);
uint64_t omap_gem_mmap_offset(struct drm_gem_object *obj);
size_t omap_gem_mmap_size(struct drm_gem_object *obj);
int omap_gem_tiled_size(struct drm_gem_object *obj, uint16_t *w, uint16_t *h);
int omap_gem_tiled_stride(struct drm_gem_object *obj, uint32_t orient);

struct dma_buf *omap_gem_prime_export(struct drm_device *dev,
		struct drm_gem_object *obj, int flags);
struct drm_gem_object *omap_gem_prime_import(struct drm_device *dev,
		struct dma_buf *buffer);

static inline int align_pitch(int pitch, int width, int bpp)
{
	int bytespp = (bpp + 7) / 8;
	/* in case someone tries to feed us a completely bogus stride: */
	pitch = max(pitch, width * bytespp);
	/* PVR needs alignment to 8 pixels.. right now that is the most
	 * restrictive stride requirement..
	 */
	return roundup(pitch, 8 * bytespp);
}

/* map crtc to vblank mask */
uint32_t pipe2vbl(struct drm_crtc *crtc);
struct omap_dss_device *omap_encoder_get_dssdev(struct drm_encoder *encoder);

/* should these be made into common util helpers?
 */

static inline int objects_lookup(struct drm_device *dev,
		struct drm_file *filp, uint32_t pixel_format,
		struct drm_gem_object **bos, uint32_t *handles)
{
	int i, n = drm_format_num_planes(pixel_format);

	for (i = 0; i < n; i++) {
		bos[i] = drm_gem_object_lookup(dev, filp, handles[i]);
		if (!bos[i])
			goto fail;

	}

	return 0;

fail:
	while (--i > 0)
		drm_gem_object_unreference_unlocked(bos[i]);

	return -ENOENT;
}

#endif /* __OMAP_DRV_H__ */
