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
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_modeset_helper.h>

#include "udl_drv.h"

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

static long udl_log_cpp(unsigned int cpp)
{
	if (WARN_ON(!is_power_of_2(cpp)))
		return -EINVAL;
	return __ffs(cpp);
}

static int udl_aligned_damage_clip(struct drm_rect *clip, int x, int y,
				   int width, int height)
{
	int x1, x2;

	if (WARN_ON_ONCE(x < 0) ||
	    WARN_ON_ONCE(y < 0) ||
	    WARN_ON_ONCE(width < 0) ||
	    WARN_ON_ONCE(height < 0))
		return -EINVAL;

	x1 = ALIGN_DOWN(x, sizeof(unsigned long));
	x2 = ALIGN(width + (x - x1), sizeof(unsigned long)) + x1;

	clip->x1 = x1;
	clip->y1 = y;
	clip->x2 = x2;
	clip->y2 = y + height;

	return 0;
}

int udl_handle_damage(struct drm_framebuffer *fb, int x, int y,
		      int width, int height)
{
	struct drm_device *dev = fb->dev;
	struct udl_device *udl = to_udl(dev);
	struct dma_buf_attachment *import_attach = fb->obj[0]->import_attach;
	int i, ret, tmp_ret;
	char *cmd;
	struct urb *urb;
	struct drm_rect clip;
	int log_bpp;
	void *vaddr;

	spin_lock(&udl->active_fb_16_lock);
	if (udl->active_fb_16 != fb) {
		spin_unlock(&udl->active_fb_16_lock);
		return 0;
	}
	spin_unlock(&udl->active_fb_16_lock);

	ret = udl_log_cpp(fb->format->cpp[0]);
	if (ret < 0)
		return ret;
	log_bpp = ret;

	ret = udl_aligned_damage_clip(&clip, x, y, width, height);
	if (ret)
		return ret;
	else if ((clip.x2 > fb->width) || (clip.y2 > fb->height))
		return -EINVAL;

	if (import_attach) {
		ret = dma_buf_begin_cpu_access(import_attach->dmabuf,
					       DMA_FROM_DEVICE);
		if (ret)
			return ret;
	}

	vaddr = drm_gem_shmem_vmap(fb->obj[0]);
	if (IS_ERR(vaddr)) {
		DRM_ERROR("failed to vmap fb\n");
		goto out_dma_buf_end_cpu_access;
	}

	urb = udl_get_urb(dev);
	if (!urb)
		goto out_drm_gem_shmem_vunmap;
	cmd = urb->transfer_buffer;

	for (i = clip.y1; i < clip.y2; i++) {
		const int line_offset = fb->pitches[0] * i;
		const int byte_offset = line_offset + (clip.x1 << log_bpp);
		const int dev_byte_offset = (fb->width * i + clip.x1) << log_bpp;
		const int byte_width = (clip.x2 - clip.x1) << log_bpp;
		ret = udl_render_hline(dev, log_bpp, &urb, (char *)vaddr,
				       &cmd, byte_offset, dev_byte_offset,
				       byte_width);
		if (ret)
			goto out_drm_gem_shmem_vunmap;
	}

	if (cmd > (char *) urb->transfer_buffer) {
		/* Send partial buffer remaining before exiting */
		int len;
		if (cmd < (char *) urb->transfer_buffer + urb->transfer_buffer_length)
			*cmd++ = 0xAF;
		len = cmd - (char *) urb->transfer_buffer;
		ret = udl_submit_urb(dev, urb, len);
	} else
		udl_urb_completion(urb);

	ret = 0;

out_drm_gem_shmem_vunmap:
	drm_gem_shmem_vunmap(fb->obj[0], vaddr);
out_dma_buf_end_cpu_access:
	if (import_attach) {
		tmp_ret = dma_buf_end_cpu_access(import_attach->dmabuf,
						 DMA_FROM_DEVICE);
		if (tmp_ret && !ret)
			ret = tmp_ret; /* only update ret if not set yet */
	}

	return ret;
}

static int udl_user_framebuffer_dirty(struct drm_framebuffer *fb,
				      struct drm_file *file,
				      unsigned flags, unsigned color,
				      struct drm_clip_rect *clips,
				      unsigned num_clips)
{
	struct udl_device *udl = fb->dev->dev_private;
	int i;
	int ret = 0;

	drm_modeset_lock_all(fb->dev);

	spin_lock(&udl->active_fb_16_lock);
	if (udl->active_fb_16 != fb) {
		spin_unlock(&udl->active_fb_16_lock);
		goto unlock;
	}
	spin_unlock(&udl->active_fb_16_lock);

	for (i = 0; i < num_clips; i++) {
		ret = udl_handle_damage(fb, clips[i].x1, clips[i].y1,
					clips[i].x2 - clips[i].x1,
					clips[i].y2 - clips[i].y1);
		if (ret)
			break;
	}

 unlock:
	drm_modeset_unlock_all(fb->dev);

	return ret;
}

static const struct drm_framebuffer_funcs udlfb_funcs = {
	.destroy	= drm_gem_fb_destroy,
	.create_handle	= drm_gem_fb_create_handle,
	.dirty		= udl_user_framebuffer_dirty,
};

struct drm_framebuffer *
udl_fb_user_fb_create(struct drm_device *dev,
		   struct drm_file *file,
		   const struct drm_mode_fb_cmd2 *mode_cmd)
{
	return drm_gem_fb_create_with_funcs(dev, file, mode_cmd,
					    &udlfb_funcs);
}
