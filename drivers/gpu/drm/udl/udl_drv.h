/*
 * Copyright (C) 2012 Red Hat
 *
 * based in parts on udlfb.c:
 * Copyright (C) 2009 Roberto De Ioris <roberto@unbit.it>
 * Copyright (C) 2009 Jaya Kumar <jayakumar.lkml@gmail.com>
 * Copyright (C) 2009 Bernie Thompson <bernie@plugable.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2. See the file COPYING in the main directory of this archive for
 * more details.
 */

#ifndef UDL_DRV_H
#define UDL_DRV_H

#include <linux/usb.h>
#include <drm/drm_gem.h>
#include <linux/mm_types.h>

#define DRIVER_NAME		"udl"
#define DRIVER_DESC		"DisplayLink"
#define DRIVER_DATE		"20120220"

#define DRIVER_MAJOR		0
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	1

#define UDL_BO_CACHEABLE		(1 << 0)
#define UDL_BO_WC		(1 << 1)

struct udl_device;

struct urb_node {
	struct list_head entry;
	struct udl_device *dev;
	struct delayed_work release_urb_work;
	struct urb *urb;
};

struct urb_list {
	struct list_head list;
	spinlock_t lock;
	struct semaphore limit_sem;
	int available;
	int count;
	size_t size;
};

struct udl_fbdev;

struct udl_device {
	struct device *dev;
	struct drm_device *ddev;
	struct usb_device *udev;
	struct drm_crtc *crtc;

	struct mutex gem_lock;

	int sku_pixel_limit;

	struct urb_list urbs;
	atomic_t lost_pixels; /* 1 = a render op failed. Need screen refresh */

	struct udl_fbdev *fbdev;
	char mode_buf[1024];
	uint32_t mode_buf_len;
	atomic_t bytes_rendered; /* raw pixel-bytes driver asked to render */
	atomic_t bytes_identical; /* saved effort with backbuffer comparison */
	atomic_t bytes_sent; /* to usb, after compression including overhead */
	atomic_t cpu_kcycles_used; /* transpired during pixel processing */
};

struct udl_gem_object {
	struct drm_gem_object base;
	struct page **pages;
	void *vmapping;
	struct sg_table *sg;
	unsigned int flags;
};

#define to_udl_bo(x) container_of(x, struct udl_gem_object, base)

struct udl_framebuffer {
	struct drm_framebuffer base;
	struct udl_gem_object *obj;
	bool active_16; /* active on the 16-bit channel */
};

#define to_udl_fb(x) container_of(x, struct udl_framebuffer, base)

/* modeset */
int udl_modeset_init(struct drm_device *dev);
void udl_modeset_restore(struct drm_device *dev);
void udl_modeset_cleanup(struct drm_device *dev);
int udl_connector_init(struct drm_device *dev, struct drm_encoder *encoder);

struct drm_encoder *udl_encoder_init(struct drm_device *dev);

struct urb *udl_get_urb(struct drm_device *dev);

int udl_submit_urb(struct drm_device *dev, struct urb *urb, size_t len);
void udl_urb_completion(struct urb *urb);

int udl_driver_load(struct drm_device *dev, unsigned long flags);
void udl_driver_unload(struct drm_device *dev);

int udl_fbdev_init(struct drm_device *dev);
void udl_fbdev_cleanup(struct drm_device *dev);
void udl_fbdev_unplug(struct drm_device *dev);
struct drm_framebuffer *
udl_fb_user_fb_create(struct drm_device *dev,
		      struct drm_file *file,
		      const struct drm_mode_fb_cmd2 *mode_cmd);

int udl_render_hline(struct drm_device *dev, int bpp, struct urb **urb_ptr,
		     const char *front, char **urb_buf_ptr,
		     u32 byte_offset, u32 device_byte_offset, u32 byte_width,
		     int *ident_ptr, int *sent_ptr);

int udl_dumb_create(struct drm_file *file_priv,
		    struct drm_device *dev,
		    struct drm_mode_create_dumb *args);
int udl_gem_mmap(struct drm_file *file_priv, struct drm_device *dev,
		 uint32_t handle, uint64_t *offset);

void udl_gem_free_object(struct drm_gem_object *gem_obj);
struct udl_gem_object *udl_gem_alloc_object(struct drm_device *dev,
					    size_t size);
struct dma_buf *udl_gem_prime_export(struct drm_device *dev,
				     struct drm_gem_object *obj, int flags);
struct drm_gem_object *udl_gem_prime_import(struct drm_device *dev,
				struct dma_buf *dma_buf);

int udl_gem_get_pages(struct udl_gem_object *obj);
void udl_gem_put_pages(struct udl_gem_object *obj);
int udl_gem_vmap(struct udl_gem_object *obj);
void udl_gem_vunmap(struct udl_gem_object *obj);
int udl_drm_gem_mmap(struct file *filp, struct vm_area_struct *vma);
vm_fault_t udl_gem_fault(struct vm_fault *vmf);

int udl_handle_damage(struct udl_framebuffer *fb, int x, int y,
		      int width, int height);

int udl_drop_usb(struct drm_device *dev);

#define CMD_WRITE_RAW8   "\xAF\x60" /**< 8 bit raw write command. */
#define CMD_WRITE_RL8    "\xAF\x61" /**< 8 bit run length command. */
#define CMD_WRITE_COPY8  "\xAF\x62" /**< 8 bit copy command. */
#define CMD_WRITE_RLX8   "\xAF\x63" /**< 8 bit extended run length command. */

#define CMD_WRITE_RAW16  "\xAF\x68" /**< 16 bit raw write command. */
#define CMD_WRITE_RL16   "\xAF\x69" /**< 16 bit run length command. */
#define CMD_WRITE_COPY16 "\xAF\x6A" /**< 16 bit copy command. */
#define CMD_WRITE_RLX16  "\xAF\x6B" /**< 16 bit extended run length command. */

#endif
