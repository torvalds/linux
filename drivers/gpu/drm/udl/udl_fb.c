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
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/dma-buf.h>

#include "drmP.h"
#include "drm.h"
#include "drm_crtc.h"
#include "drm_crtc_helper.h"
#include "udl_drv.h"

#include "drm_fb_helper.h"

#define DL_DEFIO_WRITE_DELAY    5 /* fb_deferred_io.delay in jiffies */

static int fb_defio = 1;  /* Optionally enable experimental fb_defio mmap support */
static int fb_bpp = 16;

module_param(fb_bpp, int, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
module_param(fb_defio, int, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);

struct udl_fbdev {
	struct drm_fb_helper helper;
	struct udl_framebuffer ufb;
	struct list_head fbdev_list;
	int fb_count;
};

#define DL_ALIGN_UP(x, a) ALIGN(x, a)
#define DL_ALIGN_DOWN(x, a) ALIGN(x-(a-1), a)

/** Read the red component (0..255) of a 32 bpp colour. */
#define DLO_RGB_GETRED(col) (uint8_t)((col) & 0xFF)

/** Read the green component (0..255) of a 32 bpp colour. */
#define DLO_RGB_GETGRN(col) (uint8_t)(((col) >> 8) & 0xFF)

/** Read the blue component (0..255) of a 32 bpp colour. */
#define DLO_RGB_GETBLU(col) (uint8_t)(((col) >> 16) & 0xFF)

/** Return red/green component of a 16 bpp colour number. */
#define DLO_RG16(red, grn) (uint8_t)((((red) & 0xF8) | ((grn) >> 5)) & 0xFF)

/** Return green/blue component of a 16 bpp colour number. */
#define DLO_GB16(grn, blu) (uint8_t)(((((grn) & 0x1C) << 3) | ((blu) >> 3)) & 0xFF)

/** Return 8 bpp colour number from red, green and blue components. */
#define DLO_RGB8(red, grn, blu) ((((red) << 5) | (((grn) & 3) << 3) | ((blu) & 7)) & 0xFF)

#if 0
static uint8_t rgb8(uint32_t col)
{
	uint8_t red = DLO_RGB_GETRED(col);
	uint8_t grn = DLO_RGB_GETGRN(col);
	uint8_t blu = DLO_RGB_GETBLU(col);

	return DLO_RGB8(red, grn, blu);
}

static uint16_t rgb16(uint32_t col)
{
	uint8_t red = DLO_RGB_GETRED(col);
	uint8_t grn = DLO_RGB_GETGRN(col);
	uint8_t blu = DLO_RGB_GETBLU(col);

	return (DLO_RG16(red, grn) << 8) + DLO_GB16(grn, blu);
}
#endif

/*
 * NOTE: fb_defio.c is holding info->fbdefio.mutex
 *   Touching ANY framebuffer memory that triggers a page fault
 *   in fb_defio will cause a deadlock, when it also tries to
 *   grab the same mutex.
 */
static void udlfb_dpy_deferred_io(struct fb_info *info,
				  struct list_head *pagelist)
{
	struct page *cur;
	struct fb_deferred_io *fbdefio = info->fbdefio;
	struct udl_fbdev *ufbdev = info->par;
	struct drm_device *dev = ufbdev->ufb.base.dev;
	struct udl_device *udl = dev->dev_private;
	struct urb *urb;
	char *cmd;
	cycles_t start_cycles, end_cycles;
	int bytes_sent = 0;
	int bytes_identical = 0;
	int bytes_rendered = 0;

	if (!fb_defio)
		return;

	start_cycles = get_cycles();

	urb = udl_get_urb(dev);
	if (!urb)
		return;

	cmd = urb->transfer_buffer;

	/* walk the written page list and render each to device */
	list_for_each_entry(cur, &fbdefio->pagelist, lru) {

		if (udl_render_hline(dev, (ufbdev->ufb.base.bits_per_pixel / 8),
				  &urb, (char *) info->fix.smem_start,
				  &cmd, cur->index << PAGE_SHIFT,
				  PAGE_SIZE, &bytes_identical, &bytes_sent))
			goto error;
		bytes_rendered += PAGE_SIZE;
	}

	if (cmd > (char *) urb->transfer_buffer) {
		/* Send partial buffer remaining before exiting */
		int len = cmd - (char *) urb->transfer_buffer;
		udl_submit_urb(dev, urb, len);
		bytes_sent += len;
	} else
		udl_urb_completion(urb);

error:
	atomic_add(bytes_sent, &udl->bytes_sent);
	atomic_add(bytes_identical, &udl->bytes_identical);
	atomic_add(bytes_rendered, &udl->bytes_rendered);
	end_cycles = get_cycles();
	atomic_add(((unsigned int) ((end_cycles - start_cycles)
		    >> 10)), /* Kcycles */
		   &udl->cpu_kcycles_used);
}

int udl_handle_damage(struct udl_framebuffer *fb, int x, int y,
		      int width, int height)
{
	struct drm_device *dev = fb->base.dev;
	struct udl_device *udl = dev->dev_private;
	int i, ret;
	char *cmd;
	cycles_t start_cycles, end_cycles;
	int bytes_sent = 0;
	int bytes_identical = 0;
	struct urb *urb;
	int aligned_x;
	int bpp = (fb->base.bits_per_pixel / 8);

	if (!fb->active_16)
		return 0;

	if (!fb->obj->vmapping) {
		ret = udl_gem_vmap(fb->obj);
		if (ret == -ENOMEM) {
			DRM_ERROR("failed to vmap fb\n");
			return 0;
		}
		if (!fb->obj->vmapping) {
			DRM_ERROR("failed to vmapping\n");
			return 0;
		}
	}

	start_cycles = get_cycles();

	aligned_x = DL_ALIGN_DOWN(x, sizeof(unsigned long));
	width = DL_ALIGN_UP(width + (x-aligned_x), sizeof(unsigned long));
	x = aligned_x;

	if ((width <= 0) ||
	    (x + width > fb->base.width) ||
	    (y + height > fb->base.height))
		return -EINVAL;

	urb = udl_get_urb(dev);
	if (!urb)
		return 0;
	cmd = urb->transfer_buffer;

	for (i = y; i < y + height ; i++) {
		const int line_offset = fb->base.pitches[0] * i;
		const int byte_offset = line_offset + (x * bpp);

		if (udl_render_hline(dev, bpp, &urb,
				     (char *) fb->obj->vmapping,
				     &cmd, byte_offset, width * bpp,
				     &bytes_identical, &bytes_sent))
			goto error;
	}

	if (cmd > (char *) urb->transfer_buffer) {
		/* Send partial buffer remaining before exiting */
		int len = cmd - (char *) urb->transfer_buffer;
		ret = udl_submit_urb(dev, urb, len);
		bytes_sent += len;
	} else
		udl_urb_completion(urb);

error:
	atomic_add(bytes_sent, &udl->bytes_sent);
	atomic_add(bytes_identical, &udl->bytes_identical);
	atomic_add(width*height*bpp, &udl->bytes_rendered);
	end_cycles = get_cycles();
	atomic_add(((unsigned int) ((end_cycles - start_cycles)
		    >> 10)), /* Kcycles */
		   &udl->cpu_kcycles_used);

	return 0;
}

static int udl_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	unsigned long start = vma->vm_start;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long page, pos;

	if (offset + size > info->fix.smem_len)
		return -EINVAL;

	pos = (unsigned long)info->fix.smem_start + offset;

	pr_notice("mmap() framebuffer addr:%lu size:%lu\n",
		  pos, size);

	while (size > 0) {
		page = vmalloc_to_pfn((void *)pos);
		if (remap_pfn_range(vma, start, page, PAGE_SIZE, PAGE_SHARED))
			return -EAGAIN;

		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}

	vma->vm_flags |= VM_RESERVED;	/* avoid to swap out this VMA */
	return 0;
}

static void udl_fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct udl_fbdev *ufbdev = info->par;

	sys_fillrect(info, rect);

	udl_handle_damage(&ufbdev->ufb, rect->dx, rect->dy, rect->width,
			  rect->height);
}

static void udl_fb_copyarea(struct fb_info *info, const struct fb_copyarea *region)
{
	struct udl_fbdev *ufbdev = info->par;

	sys_copyarea(info, region);

	udl_handle_damage(&ufbdev->ufb, region->dx, region->dy, region->width,
			  region->height);
}

static void udl_fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct udl_fbdev *ufbdev = info->par;

	sys_imageblit(info, image);

	udl_handle_damage(&ufbdev->ufb, image->dx, image->dy, image->width,
			  image->height);
}

/*
 * It's common for several clients to have framebuffer open simultaneously.
 * e.g. both fbcon and X. Makes things interesting.
 * Assumes caller is holding info->lock (for open and release at least)
 */
static int udl_fb_open(struct fb_info *info, int user)
{
	struct udl_fbdev *ufbdev = info->par;
	struct drm_device *dev = ufbdev->ufb.base.dev;
	struct udl_device *udl = dev->dev_private;

	/* If the USB device is gone, we don't accept new opens */
	if (drm_device_is_unplugged(udl->ddev))
		return -ENODEV;

	ufbdev->fb_count++;

	if (fb_defio && (info->fbdefio == NULL)) {
		/* enable defio at last moment if not disabled by client */

		struct fb_deferred_io *fbdefio;

		fbdefio = kmalloc(sizeof(struct fb_deferred_io), GFP_KERNEL);

		if (fbdefio) {
			fbdefio->delay = DL_DEFIO_WRITE_DELAY;
			fbdefio->deferred_io = udlfb_dpy_deferred_io;
		}

		info->fbdefio = fbdefio;
		fb_deferred_io_init(info);
	}

	pr_notice("open /dev/fb%d user=%d fb_info=%p count=%d\n",
		  info->node, user, info, ufbdev->fb_count);

	return 0;
}


/*
 * Assumes caller is holding info->lock mutex (for open and release at least)
 */
static int udl_fb_release(struct fb_info *info, int user)
{
	struct udl_fbdev *ufbdev = info->par;

	ufbdev->fb_count--;

	if ((ufbdev->fb_count == 0) && (info->fbdefio)) {
		fb_deferred_io_cleanup(info);
		kfree(info->fbdefio);
		info->fbdefio = NULL;
		info->fbops->fb_mmap = udl_fb_mmap;
	}

	pr_warn("released /dev/fb%d user=%d count=%d\n",
		info->node, user, ufbdev->fb_count);

	return 0;
}

static struct fb_ops udlfb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_fillrect = udl_fb_fillrect,
	.fb_copyarea = udl_fb_copyarea,
	.fb_imageblit = udl_fb_imageblit,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
	.fb_debug_enter = drm_fb_helper_debug_enter,
	.fb_debug_leave = drm_fb_helper_debug_leave,
	.fb_mmap = udl_fb_mmap,
	.fb_open = udl_fb_open,
	.fb_release = udl_fb_release,
};

void udl_crtc_fb_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
			   u16 blue, int regno)
{
}

void udl_crtc_fb_gamma_get(struct drm_crtc *crtc, u16 *red, u16 *green,
			     u16 *blue, int regno)
{
	*red = 0;
	*green = 0;
	*blue = 0;
}

static int udl_user_framebuffer_dirty(struct drm_framebuffer *fb,
				      struct drm_file *file,
				      unsigned flags, unsigned color,
				      struct drm_clip_rect *clips,
				      unsigned num_clips)
{
	struct udl_framebuffer *ufb = to_udl_fb(fb);
	int i;
	int ret = 0;

	if (!ufb->active_16)
		return 0;

	if (ufb->obj->base.import_attach) {
		ret = dma_buf_begin_cpu_access(ufb->obj->base.import_attach->dmabuf,
					       0, ufb->obj->base.size,
					       DMA_FROM_DEVICE);
		if (ret)
			return ret;
	}

	for (i = 0; i < num_clips; i++) {
		ret = udl_handle_damage(ufb, clips[i].x1, clips[i].y1,
				  clips[i].x2 - clips[i].x1,
				  clips[i].y2 - clips[i].y1);
		if (ret)
			break;
	}

	if (ufb->obj->base.import_attach) {
		dma_buf_end_cpu_access(ufb->obj->base.import_attach->dmabuf,
				       0, ufb->obj->base.size,
				       DMA_FROM_DEVICE);
	}
	return ret;
}

static void udl_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct udl_framebuffer *ufb = to_udl_fb(fb);

	if (ufb->obj)
		drm_gem_object_unreference_unlocked(&ufb->obj->base);

	drm_framebuffer_cleanup(fb);
	kfree(ufb);
}

static const struct drm_framebuffer_funcs udlfb_funcs = {
	.destroy = udl_user_framebuffer_destroy,
	.dirty = udl_user_framebuffer_dirty,
	.create_handle = NULL,
};


static int
udl_framebuffer_init(struct drm_device *dev,
		     struct udl_framebuffer *ufb,
		     struct drm_mode_fb_cmd2 *mode_cmd,
		     struct udl_gem_object *obj)
{
	int ret;

	ufb->obj = obj;
	ret = drm_framebuffer_init(dev, &ufb->base, &udlfb_funcs);
	drm_helper_mode_fill_fb_struct(&ufb->base, mode_cmd);
	return ret;
}


static int udlfb_create(struct udl_fbdev *ufbdev,
			struct drm_fb_helper_surface_size *sizes)
{
	struct drm_device *dev = ufbdev->helper.dev;
	struct fb_info *info;
	struct device *device = &dev->usbdev->dev;
	struct drm_framebuffer *fb;
	struct drm_mode_fb_cmd2 mode_cmd;
	struct udl_gem_object *obj;
	uint32_t size;
	int ret = 0;

	if (sizes->surface_bpp == 24)
		sizes->surface_bpp = 32;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	mode_cmd.pitches[0] = mode_cmd.width * ((sizes->surface_bpp + 7) / 8);

	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);

	size = mode_cmd.pitches[0] * mode_cmd.height;
	size = ALIGN(size, PAGE_SIZE);

	obj = udl_gem_alloc_object(dev, size);
	if (!obj)
		goto out;

	ret = udl_gem_vmap(obj);
	if (ret) {
		DRM_ERROR("failed to vmap fb\n");
		goto out_gfree;
	}

	info = framebuffer_alloc(0, device);
	if (!info) {
		ret = -ENOMEM;
		goto out_gfree;
	}
	info->par = ufbdev;

	ret = udl_framebuffer_init(dev, &ufbdev->ufb, &mode_cmd, obj);
	if (ret)
		goto out_gfree;

	fb = &ufbdev->ufb.base;

	ufbdev->helper.fb = fb;
	ufbdev->helper.fbdev = info;

	strcpy(info->fix.id, "udldrmfb");

	info->screen_base = ufbdev->ufb.obj->vmapping;
	info->fix.smem_len = size;
	info->fix.smem_start = (unsigned long)ufbdev->ufb.obj->vmapping;

	info->flags = FBINFO_DEFAULT | FBINFO_CAN_FORCE_OUTPUT;
	info->fbops = &udlfb_ops;
	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->depth);
	drm_fb_helper_fill_var(info, &ufbdev->helper, sizes->fb_width, sizes->fb_height);

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret) {
		ret = -ENOMEM;
		goto out_gfree;
	}


	DRM_DEBUG_KMS("allocated %dx%d vmal %p\n",
		      fb->width, fb->height,
		      ufbdev->ufb.obj->vmapping);

	return ret;
out_gfree:
	drm_gem_object_unreference(&ufbdev->ufb.obj->base);
out:
	return ret;
}

static int udl_fb_find_or_create_single(struct drm_fb_helper *helper,
					struct drm_fb_helper_surface_size *sizes)
{
	struct udl_fbdev *ufbdev = (struct udl_fbdev *)helper;
	int new_fb = 0;
	int ret;

	if (!helper->fb) {
		ret = udlfb_create(ufbdev, sizes);
		if (ret)
			return ret;

		new_fb = 1;
	}
	return new_fb;
}

static struct drm_fb_helper_funcs udl_fb_helper_funcs = {
	.gamma_set = udl_crtc_fb_gamma_set,
	.gamma_get = udl_crtc_fb_gamma_get,
	.fb_probe = udl_fb_find_or_create_single,
};

static void udl_fbdev_destroy(struct drm_device *dev,
			      struct udl_fbdev *ufbdev)
{
	struct fb_info *info;
	if (ufbdev->helper.fbdev) {
		info = ufbdev->helper.fbdev;
		unregister_framebuffer(info);
		if (info->cmap.len)
			fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
	}
	drm_fb_helper_fini(&ufbdev->helper);
	drm_framebuffer_cleanup(&ufbdev->ufb.base);
	drm_gem_object_unreference_unlocked(&ufbdev->ufb.obj->base);
}

int udl_fbdev_init(struct drm_device *dev)
{
	struct udl_device *udl = dev->dev_private;
	int bpp_sel = fb_bpp;
	struct udl_fbdev *ufbdev;
	int ret;

	ufbdev = kzalloc(sizeof(struct udl_fbdev), GFP_KERNEL);
	if (!ufbdev)
		return -ENOMEM;

	udl->fbdev = ufbdev;
	ufbdev->helper.funcs = &udl_fb_helper_funcs;

	ret = drm_fb_helper_init(dev, &ufbdev->helper,
				 1, 1);
	if (ret) {
		kfree(ufbdev);
		return ret;

	}

	drm_fb_helper_single_add_all_connectors(&ufbdev->helper);
	drm_fb_helper_initial_config(&ufbdev->helper, bpp_sel);
	return 0;
}

void udl_fbdev_cleanup(struct drm_device *dev)
{
	struct udl_device *udl = dev->dev_private;
	if (!udl->fbdev)
		return;

	udl_fbdev_destroy(dev, udl->fbdev);
	kfree(udl->fbdev);
	udl->fbdev = NULL;
}

void udl_fbdev_unplug(struct drm_device *dev)
{
	struct udl_device *udl = dev->dev_private;
	struct udl_fbdev *ufbdev;
	if (!udl->fbdev)
		return;

	ufbdev = udl->fbdev;
	if (ufbdev->helper.fbdev) {
		struct fb_info *info;
		info = ufbdev->helper.fbdev;
		unlink_framebuffer(info);
	}
}

struct drm_framebuffer *
udl_fb_user_fb_create(struct drm_device *dev,
		   struct drm_file *file,
		   struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_gem_object *obj;
	struct udl_framebuffer *ufb;
	int ret;
	uint32_t size;

	obj = drm_gem_object_lookup(dev, file, mode_cmd->handles[0]);
	if (obj == NULL)
		return ERR_PTR(-ENOENT);

	size = mode_cmd->pitches[0] * mode_cmd->height;
	size = ALIGN(size, PAGE_SIZE);

	if (size > obj->size) {
		DRM_ERROR("object size not sufficient for fb %d %zu %d %d\n", size, obj->size, mode_cmd->pitches[0], mode_cmd->height);
		return ERR_PTR(-ENOMEM);
	}

	ufb = kzalloc(sizeof(*ufb), GFP_KERNEL);
	if (ufb == NULL)
		return ERR_PTR(-ENOMEM);

	ret = udl_framebuffer_init(dev, ufb, mode_cmd, to_udl_bo(obj));
	if (ret) {
		kfree(ufb);
		return ERR_PTR(-EINVAL);
	}
	return &ufb->base;
}
