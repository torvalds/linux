/*
 * drivers/staging/omapdrm/omap_drv.h
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

#include <video/omapdss.h>
#include <linux/module.h>
#include <linux/types.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include "omap_drm.h"
#include "omap_priv.h"

#define DBG(fmt, ...) DRM_DEBUG(fmt"\n", ##__VA_ARGS__)
#define VERB(fmt, ...) if (0) DRM_DEBUG(fmt, ##__VA_ARGS__) /* verbose debug */

#define MODULE_NAME     "omapdrm"

/* max # of mapper-id's that can be assigned.. todo, come up with a better
 * (but still inexpensive) way to store/access per-buffer mapper private
 * data..
 */
#define MAX_MAPPERS 2

struct omap_drm_private {
	unsigned int num_crtcs;
	struct drm_crtc *crtcs[8];
	unsigned int num_planes;
	struct drm_plane *planes[8];
	unsigned int num_encoders;
	struct drm_encoder *encoders[8];
	unsigned int num_connectors;
	struct drm_connector *connectors[8];

	struct drm_fb_helper *fbdev;

	bool has_dmm;
};

#ifdef CONFIG_DEBUG_FS
int omap_debugfs_init(struct drm_minor *minor);
void omap_debugfs_cleanup(struct drm_minor *minor);
#endif

struct drm_fb_helper *omap_fbdev_init(struct drm_device *dev);
void omap_fbdev_free(struct drm_device *dev);

struct drm_crtc *omap_crtc_init(struct drm_device *dev,
		struct omap_overlay *ovl, int id);

struct drm_plane *omap_plane_init(struct drm_device *dev,
		struct omap_overlay *ovl, unsigned int possible_crtcs,
		bool priv);
int omap_plane_dpms(struct drm_plane *plane, int mode);
int omap_plane_mode_set(struct drm_plane *plane,
		struct drm_crtc *crtc, struct drm_framebuffer *fb,
		int crtc_x, int crtc_y,
		unsigned int crtc_w, unsigned int crtc_h,
		uint32_t src_x, uint32_t src_y,
		uint32_t src_w, uint32_t src_h);

struct drm_encoder *omap_encoder_init(struct drm_device *dev,
		struct omap_overlay_manager *mgr);
struct omap_overlay_manager *omap_encoder_get_manager(
		struct drm_encoder *encoder);
struct drm_encoder *omap_connector_attached_encoder(
		struct drm_connector *connector);
enum drm_connector_status omap_connector_detect(
		struct drm_connector *connector, bool force);

struct drm_connector *omap_connector_init(struct drm_device *dev,
		int connector_type, struct omap_dss_device *dssdev);
void omap_connector_mode_set(struct drm_connector *connector,
		struct drm_display_mode *mode);
void omap_connector_flush(struct drm_connector *connector,
		int x, int y, int w, int h);

struct drm_framebuffer *omap_framebuffer_create(struct drm_device *dev,
		struct drm_file *file, struct drm_mode_fb_cmd2 *mode_cmd);
struct drm_framebuffer *omap_framebuffer_init(struct drm_device *dev,
		struct drm_mode_fb_cmd2 *mode_cmd, struct drm_gem_object **bos);
struct drm_gem_object *omap_framebuffer_bo(struct drm_framebuffer *fb, int p);
int omap_framebuffer_pin(struct drm_framebuffer *fb);
void omap_framebuffer_unpin(struct drm_framebuffer *fb);
void omap_framebuffer_update_scanout(struct drm_framebuffer *fb, int x, int y,
		struct omap_overlay_info *info);
struct drm_connector *omap_framebuffer_get_next_connector(
		struct drm_framebuffer *fb, struct drm_connector *from);
void omap_framebuffer_flush(struct drm_framebuffer *fb,
		int x, int y, int w, int h);

void omap_gem_init(struct drm_device *dev);
void omap_gem_deinit(struct drm_device *dev);

struct drm_gem_object *omap_gem_new(struct drm_device *dev,
		union omap_gem_size gsize, uint32_t flags);
int omap_gem_new_handle(struct drm_device *dev, struct drm_file *file,
		union omap_gem_size gsize, uint32_t flags, uint32_t *handle);
void omap_gem_free_object(struct drm_gem_object *obj);
int omap_gem_init_object(struct drm_gem_object *obj);
void *omap_gem_vaddr(struct drm_gem_object *obj);
int omap_gem_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
		uint32_t handle, uint64_t *offset);
int omap_gem_dumb_destroy(struct drm_file *file, struct drm_device *dev,
		uint32_t handle);
int omap_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
		struct drm_mode_create_dumb *args);
int omap_gem_mmap(struct file *filp, struct vm_area_struct *vma);
int omap_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf);
int omap_gem_op_start(struct drm_gem_object *obj, enum omap_gem_op op);
int omap_gem_op_finish(struct drm_gem_object *obj, enum omap_gem_op op);
int omap_gem_op_sync(struct drm_gem_object *obj, enum omap_gem_op op);
int omap_gem_op_async(struct drm_gem_object *obj, enum omap_gem_op op,
		void (*fxn)(void *arg), void *arg);
int omap_gem_roll(struct drm_gem_object *obj, uint32_t roll);
int omap_gem_get_paddr(struct drm_gem_object *obj,
		dma_addr_t *paddr, bool remap);
int omap_gem_put_paddr(struct drm_gem_object *obj);
uint64_t omap_gem_mmap_offset(struct drm_gem_object *obj);
size_t omap_gem_mmap_size(struct drm_gem_object *obj);

static inline int align_pitch(int pitch, int width, int bpp)
{
	int bytespp = (bpp + 7) / 8;
	/* in case someone tries to feed us a completely bogus stride: */
	pitch = max(pitch, width * bytespp);
	/* PVR needs alignment to 8 pixels.. right now that is the most
	 * restrictive stride requirement..
	 */
	return ALIGN(pitch, 8 * bytespp);
}

/* should these be made into common util helpers?
 */

static inline int objects_lookup(struct drm_device *dev,
		struct drm_file *filp, uint32_t pixel_format,
		struct drm_gem_object **bos, uint32_t *handles)
{
	int i, n = drm_format_num_planes(pixel_format);

	for (i = 0; i < n; i++) {
		bos[i] = drm_gem_object_lookup(dev, filp, handles[i]);
		if (!bos[i]) {
			goto fail;
		}
	}

	return 0;

fail:
	while (--i > 0) {
		drm_gem_object_unreference_unlocked(bos[i]);
	}
	return -ENOENT;
}

#endif /* __OMAP_DRV_H__ */
