// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Red Hat
 *
 * based in parts on udlfb.c:
 * Copyright (C) 2009 Roberto De Ioris <roberto@unbit.it>
 * Copyright (C) 2009 Jaya Kumar <jayakumar.lkml@gmail.com>
 * Copyright (C) 2009 Bernie Thompson <bernie@plugable.com>
 */

#include <linux/moduleparam.h>
#include <linux/dma-buf.h>

#include <drm/drm_crtc_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_modeset_helper.h>

#include "udl_drv.h"

#define DL_ALIGN_UP(x, a) ALIGN(x, a)
#define DL_ALIGN_DOWN(x, a) ALIGN_DOWN(x, a)

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

int udl_handle_damage(struct udl_framebuffer *fb, int x, int y,
		      int width, int height)
{
	struct drm_device *dev = fb->base.dev;
	struct udl_device *udl = to_udl(dev);
	int i, ret;
	char *cmd;
	cycles_t start_cycles, end_cycles;
	int bytes_sent = 0;
	int bytes_identical = 0;
	struct urb *urb;
	int aligned_x;
	int log_bpp;
	void *vaddr;

	BUG_ON(!is_power_of_2(fb->base.format->cpp[0]));
	log_bpp = __ffs(fb->base.format->cpp[0]);

	if (!fb->active_16)
		return 0;

	vaddr = drm_gem_shmem_vmap(&fb->shmem->base);
	if (IS_ERR(vaddr)) {
		DRM_ERROR("failed to vmap fb\n");
		return 0;
	}

	aligned_x = DL_ALIGN_DOWN(x, sizeof(unsigned long));
	width = DL_ALIGN_UP(width + (x-aligned_x), sizeof(unsigned long));
	x = aligned_x;

	if ((width <= 0) ||
	    (x + width > fb->base.width) ||
	    (y + height > fb->base.height)) {
		ret = -EINVAL;
		goto err_drm_gem_shmem_vunmap;
	}

	start_cycles = get_cycles();

	urb = udl_get_urb(dev);
	if (!urb)
		goto out;
	cmd = urb->transfer_buffer;

	for (i = y; i < y + height ; i++) {
		const int line_offset = fb->base.pitches[0] * i;
		const int byte_offset = line_offset + (x << log_bpp);
		const int dev_byte_offset = (fb->base.width * i + x) << log_bpp;
		if (udl_render_hline(dev, log_bpp, &urb, (char *)vaddr,
				     &cmd, byte_offset, dev_byte_offset,
				     width << log_bpp,
				     &bytes_identical, &bytes_sent))
			goto error;
	}

	if (cmd > (char *) urb->transfer_buffer) {
		/* Send partial buffer remaining before exiting */
		int len;
		if (cmd < (char *) urb->transfer_buffer + urb->transfer_buffer_length)
			*cmd++ = 0xAF;
		len = cmd - (char *) urb->transfer_buffer;
		ret = udl_submit_urb(dev, urb, len);
		bytes_sent += len;
	} else
		udl_urb_completion(urb);

error:
	atomic_add(bytes_sent, &udl->bytes_sent);
	atomic_add(bytes_identical, &udl->bytes_identical);
	atomic_add((width * height) << log_bpp, &udl->bytes_rendered);
	end_cycles = get_cycles();
	atomic_add(((unsigned int) ((end_cycles - start_cycles)
		    >> 10)), /* Kcycles */
		   &udl->cpu_kcycles_used);

out:
	drm_gem_shmem_vunmap(&fb->shmem->base, vaddr);

	return 0;

err_drm_gem_shmem_vunmap:
	drm_gem_shmem_vunmap(&fb->shmem->base, vaddr);
	return ret;
}

static int udl_user_framebuffer_dirty(struct drm_framebuffer *fb,
				      struct drm_file *file,
				      unsigned flags, unsigned color,
				      struct drm_clip_rect *clips,
				      unsigned num_clips)
{
	struct udl_framebuffer *ufb = to_udl_fb(fb);
	struct dma_buf_attachment *import_attach;
	int i;
	int ret = 0;

	drm_modeset_lock_all(fb->dev);

	if (!ufb->active_16)
		goto unlock;

	import_attach = ufb->shmem->base.import_attach;

	if (import_attach) {
		ret = dma_buf_begin_cpu_access(import_attach->dmabuf,
					       DMA_FROM_DEVICE);
		if (ret)
			goto unlock;
	}

	for (i = 0; i < num_clips; i++) {
		ret = udl_handle_damage(ufb, clips[i].x1, clips[i].y1,
				  clips[i].x2 - clips[i].x1,
				  clips[i].y2 - clips[i].y1);
		if (ret)
			break;
	}

	if (import_attach)
		ret = dma_buf_end_cpu_access(import_attach->dmabuf,
					     DMA_FROM_DEVICE);

 unlock:
	drm_modeset_unlock_all(fb->dev);

	return ret;
}

static void udl_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct udl_framebuffer *ufb = to_udl_fb(fb);

	if (ufb->shmem)
		drm_gem_object_put_unlocked(&ufb->shmem->base);

	drm_framebuffer_cleanup(fb);
	kfree(ufb);
}

static const struct drm_framebuffer_funcs udlfb_funcs = {
	.destroy = udl_user_framebuffer_destroy,
	.dirty = udl_user_framebuffer_dirty,
};


static int
udl_framebuffer_init(struct drm_device *dev,
		     struct udl_framebuffer *ufb,
		     const struct drm_mode_fb_cmd2 *mode_cmd,
		     struct drm_gem_shmem_object *shmem)
{
	int ret;

	ufb->shmem = shmem;
	drm_helper_mode_fill_fb_struct(dev, &ufb->base, mode_cmd);
	ret = drm_framebuffer_init(dev, &ufb->base, &udlfb_funcs);
	return ret;
}

struct drm_framebuffer *
udl_fb_user_fb_create(struct drm_device *dev,
		   struct drm_file *file,
		   const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_gem_object *obj;
	struct udl_framebuffer *ufb;
	int ret;
	uint32_t size;

	obj = drm_gem_object_lookup(file, mode_cmd->handles[0]);
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

	ret = udl_framebuffer_init(dev, ufb, mode_cmd,
				   to_drm_gem_shmem_obj(obj));
	if (ret) {
		kfree(ufb);
		return ERR_PTR(-EINVAL);
	}
	return &ufb->base;
}
