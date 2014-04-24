/*
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/vmalloc.h>

#include <video/adf.h>
#include <video/adf_client.h>
#include <video/adf_fbdev.h>
#include <video/adf_format.h>

#include "adf.h"

struct adf_fbdev_format {
	u32 fourcc;
	u32 bpp;
	u32 r_length;
	u32 g_length;
	u32 b_length;
	u32 a_length;
	u32 r_offset;
	u32 g_offset;
	u32 b_offset;
	u32 a_offset;
};

static const struct adf_fbdev_format format_table[] = {
	{DRM_FORMAT_RGB332, 8, 3, 3, 2, 0, 5, 2, 0, 0},
	{DRM_FORMAT_BGR233, 8, 3, 3, 2, 0, 0, 3, 5, 0},

	{DRM_FORMAT_XRGB4444, 16, 4, 4, 4, 0, 8, 4, 0, 0},
	{DRM_FORMAT_XBGR4444, 16, 4, 4, 4, 0, 0, 4, 8, 0},
	{DRM_FORMAT_RGBX4444, 16, 4, 4, 4, 0, 12, 8, 4, 0},
	{DRM_FORMAT_BGRX4444, 16, 4, 4, 4, 0, 0, 4, 8, 0},

	{DRM_FORMAT_ARGB4444, 16, 4, 4, 4, 4, 8, 4, 0, 12},
	{DRM_FORMAT_ABGR4444, 16, 4, 4, 4, 4, 0, 4, 8, 12},
	{DRM_FORMAT_RGBA4444, 16, 4, 4, 4, 4, 12, 8, 4, 0},
	{DRM_FORMAT_BGRA4444, 16, 4, 4, 4, 4, 0, 4, 8, 0},

	{DRM_FORMAT_XRGB1555, 16, 5, 5, 5, 0, 10, 5, 0, 0},
	{DRM_FORMAT_XBGR1555, 16, 5, 5, 5, 0, 0, 5, 10, 0},
	{DRM_FORMAT_RGBX5551, 16, 5, 5, 5, 0, 11, 6, 1, 0},
	{DRM_FORMAT_BGRX5551, 16, 5, 5, 5, 0, 1, 6, 11, 0},

	{DRM_FORMAT_ARGB1555, 16, 5, 5, 5, 1, 10, 5, 0, 15},
	{DRM_FORMAT_ABGR1555, 16, 5, 5, 5, 1, 0, 5, 10, 15},
	{DRM_FORMAT_RGBA5551, 16, 5, 5, 5, 1, 11, 6, 1, 0},
	{DRM_FORMAT_BGRA5551, 16, 5, 5, 5, 1, 1, 6, 11, 0},

	{DRM_FORMAT_RGB565, 16, 5, 6, 5, 0, 11, 5, 0, 0},
	{DRM_FORMAT_BGR565, 16, 5, 6, 5, 0, 0, 5, 11, 0},

	{DRM_FORMAT_RGB888, 24, 8, 8, 8, 0, 16, 8, 0, 0},
	{DRM_FORMAT_BGR888, 24, 8, 8, 8, 0, 0, 8, 16, 0},

	{DRM_FORMAT_XRGB8888, 32, 8, 8, 8, 0, 16, 8, 0, 0},
	{DRM_FORMAT_XBGR8888, 32, 8, 8, 8, 0, 0, 8, 16, 0},
	{DRM_FORMAT_RGBX8888, 32, 8, 8, 8, 0, 24, 16, 8, 0},
	{DRM_FORMAT_BGRX8888, 32, 8, 8, 8, 0, 8, 16, 24, 0},

	{DRM_FORMAT_ARGB8888, 32, 8, 8, 8, 8, 16, 8, 0, 24},
	{DRM_FORMAT_ABGR8888, 32, 8, 8, 8, 8, 0, 8, 16, 24},
	{DRM_FORMAT_RGBA8888, 32, 8, 8, 8, 8, 24, 16, 8, 0},
	{DRM_FORMAT_BGRA8888, 32, 8, 8, 8, 8, 8, 16, 24, 0},

	{DRM_FORMAT_XRGB2101010, 32, 10, 10, 10, 0, 20, 10, 0, 0},
	{DRM_FORMAT_XBGR2101010, 32, 10, 10, 10, 0, 0, 10, 20, 0},
	{DRM_FORMAT_RGBX1010102, 32, 10, 10, 10, 0, 22, 12, 2, 0},
	{DRM_FORMAT_BGRX1010102, 32, 10, 10, 10, 0, 2, 12, 22, 0},

	{DRM_FORMAT_ARGB2101010, 32, 10, 10, 10, 2, 20, 10, 0, 30},
	{DRM_FORMAT_ABGR2101010, 32, 10, 10, 10, 2, 0, 10, 20, 30},
	{DRM_FORMAT_RGBA1010102, 32, 10, 10, 10, 2, 22, 12, 2, 0},
	{DRM_FORMAT_BGRA1010102, 32, 10, 10, 10, 2, 2, 12, 22, 0},
};

static u32 drm_fourcc_from_fb_var(struct fb_var_screeninfo *var)
{
	size_t i;
	for (i = 0; i < ARRAY_SIZE(format_table); i++) {
		const struct adf_fbdev_format *f = &format_table[i];
		if (var->red.length == f->r_length &&
			var->red.offset == f->r_offset &&
			var->green.length == f->g_length &&
			var->green.offset == f->g_offset &&
			var->blue.length == f->b_length &&
			var->blue.offset == f->b_offset &&
			var->transp.length == f->a_length &&
			(var->transp.length == 0 ||
					var->transp.offset == f->a_offset))
			return f->fourcc;
	}

	return 0;
}

static const struct adf_fbdev_format *fbdev_format_info(u32 format)
{
	size_t i;
	for (i = 0; i < ARRAY_SIZE(format_table); i++) {
		const struct adf_fbdev_format *f = &format_table[i];
		if (f->fourcc == format)
			return f;
	}

	BUG();
}

void adf_modeinfo_to_fb_videomode(const struct drm_mode_modeinfo *mode,
		struct fb_videomode *vmode)
{
	memset(vmode, 0, sizeof(*vmode));

	vmode->refresh = mode->vrefresh;

	vmode->xres = mode->hdisplay;
	vmode->yres = mode->vdisplay;

	vmode->pixclock = mode->clock ? KHZ2PICOS(mode->clock) : 0;
	vmode->left_margin = mode->htotal - mode->hsync_end;
	vmode->right_margin = mode->hsync_start - mode->hdisplay;
	vmode->upper_margin = mode->vtotal - mode->vsync_end;
	vmode->lower_margin = mode->vsync_start - mode->vdisplay;
	vmode->hsync_len = mode->hsync_end - mode->hsync_start;
	vmode->vsync_len = mode->vsync_end - mode->vsync_start;

	vmode->sync = 0;
	if (mode->flags | DRM_MODE_FLAG_PHSYNC)
		vmode->sync |= FB_SYNC_HOR_HIGH_ACT;
	if (mode->flags | DRM_MODE_FLAG_PVSYNC)
		vmode->sync |= FB_SYNC_VERT_HIGH_ACT;
	if (mode->flags | DRM_MODE_FLAG_PCSYNC)
		vmode->sync |= FB_SYNC_COMP_HIGH_ACT;
	if (mode->flags | DRM_MODE_FLAG_BCAST)
		vmode->sync |= FB_SYNC_BROADCAST;

	vmode->vmode = 0;
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		vmode->vmode |= FB_VMODE_INTERLACED;
	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		vmode->vmode |= FB_VMODE_DOUBLE;
}
EXPORT_SYMBOL(adf_modeinfo_to_fb_videomode);

void adf_modeinfo_from_fb_videomode(const struct fb_videomode *vmode,
		struct drm_mode_modeinfo *mode)
{
	memset(mode, 0, sizeof(*mode));

	mode->hdisplay = vmode->xres;
	mode->hsync_start = mode->hdisplay + vmode->right_margin;
	mode->hsync_end = mode->hsync_start + vmode->hsync_len;
	mode->htotal = mode->hsync_end + vmode->left_margin;

	mode->vdisplay = vmode->yres;
	mode->vsync_start = mode->vdisplay + vmode->lower_margin;
	mode->vsync_end = mode->vsync_start + vmode->vsync_len;
	mode->vtotal = mode->vsync_end + vmode->upper_margin;

	mode->clock = vmode->pixclock ? PICOS2KHZ(vmode->pixclock) : 0;

	mode->flags = 0;
	if (vmode->sync & FB_SYNC_HOR_HIGH_ACT)
		mode->flags |= DRM_MODE_FLAG_PHSYNC;
	if (vmode->sync & FB_SYNC_VERT_HIGH_ACT)
		mode->flags |= DRM_MODE_FLAG_PVSYNC;
	if (vmode->sync & FB_SYNC_COMP_HIGH_ACT)
		mode->flags |= DRM_MODE_FLAG_PCSYNC;
	if (vmode->sync & FB_SYNC_BROADCAST)
		mode->flags |= DRM_MODE_FLAG_BCAST;
	if (vmode->vmode & FB_VMODE_INTERLACED)
		mode->flags |= DRM_MODE_FLAG_INTERLACE;
	if (vmode->vmode & FB_VMODE_DOUBLE)
		mode->flags |= DRM_MODE_FLAG_DBLSCAN;

	if (vmode->refresh)
		mode->vrefresh = vmode->refresh;
	else
		adf_modeinfo_set_vrefresh(mode);

	if (vmode->name)
		strlcpy(mode->name, vmode->name, sizeof(mode->name));
	else
		adf_modeinfo_set_name(mode);
}
EXPORT_SYMBOL(adf_modeinfo_from_fb_videomode);

static int adf_fbdev_post(struct adf_fbdev *fbdev)
{
	struct adf_buffer buf;
	struct sync_fence *complete_fence;
	int ret = 0;

	memset(&buf, 0, sizeof(buf));
	buf.overlay_engine = fbdev->eng;
	buf.w = fbdev->info->var.xres;
	buf.h = fbdev->info->var.yres;
	buf.format = fbdev->format;
	buf.dma_bufs[0] = fbdev->dma_buf;
	buf.offset[0] = fbdev->offset +
			fbdev->info->var.yoffset * fbdev->pitch +
			fbdev->info->var.xoffset *
			(fbdev->info->var.bits_per_pixel / 8);
	buf.pitch[0] = fbdev->pitch;
	buf.n_planes = 1;

	complete_fence = adf_interface_simple_post(fbdev->intf, &buf);
	if (IS_ERR(complete_fence)) {
		ret = PTR_ERR(complete_fence);
		goto done;
	}

	sync_fence_put(complete_fence);
done:
	return ret;
}

static const u16 vga_palette[][3] = {
	{0x0000, 0x0000, 0x0000},
	{0x0000, 0x0000, 0xAAAA},
	{0x0000, 0xAAAA, 0x0000},
	{0x0000, 0xAAAA, 0xAAAA},
	{0xAAAA, 0x0000, 0x0000},
	{0xAAAA, 0x0000, 0xAAAA},
	{0xAAAA, 0x5555, 0x0000},
	{0xAAAA, 0xAAAA, 0xAAAA},
	{0x5555, 0x5555, 0x5555},
	{0x5555, 0x5555, 0xFFFF},
	{0x5555, 0xFFFF, 0x5555},
	{0x5555, 0xFFFF, 0xFFFF},
	{0xFFFF, 0x5555, 0x5555},
	{0xFFFF, 0x5555, 0xFFFF},
	{0xFFFF, 0xFFFF, 0x5555},
	{0xFFFF, 0xFFFF, 0xFFFF},
};

static int adf_fb_alloc(struct adf_fbdev *fbdev)
{
	int ret;

	ret = adf_interface_simple_buffer_alloc(fbdev->intf,
			fbdev->default_xres_virtual,
			fbdev->default_yres_virtual,
			fbdev->default_format,
			&fbdev->dma_buf, &fbdev->offset, &fbdev->pitch);
	if (ret < 0) {
		dev_err(fbdev->info->dev, "allocating fb failed: %d\n", ret);
		return ret;
	}

	fbdev->vaddr = dma_buf_vmap(fbdev->dma_buf);
	if (!fbdev->vaddr) {
		ret = -ENOMEM;
		dev_err(fbdev->info->dev, "vmapping fb failed\n");
		goto err_vmap;
	}
	fbdev->info->fix.line_length = fbdev->pitch;
	fbdev->info->var.xres_virtual = fbdev->default_xres_virtual;
	fbdev->info->var.yres_virtual = fbdev->default_yres_virtual;
	fbdev->info->fix.smem_len = fbdev->dma_buf->size;
	fbdev->info->screen_base = fbdev->vaddr;

	return 0;

err_vmap:
	dma_buf_put(fbdev->dma_buf);
	return ret;
}

static void adf_fb_destroy(struct adf_fbdev *fbdev)
{
	dma_buf_vunmap(fbdev->dma_buf, fbdev->vaddr);
	dma_buf_put(fbdev->dma_buf);
}

static void adf_fbdev_set_format(struct adf_fbdev *fbdev, u32 format)
{
	size_t i;
	const struct adf_fbdev_format *info = fbdev_format_info(format);
	for (i = 0; i < ARRAY_SIZE(vga_palette); i++) {
		u16 r = vga_palette[i][0];
		u16 g = vga_palette[i][1];
		u16 b = vga_palette[i][2];

		r >>= (16 - info->r_length);
		g >>= (16 - info->g_length);
		b >>= (16 - info->b_length);

		fbdev->pseudo_palette[i] =
			(r << info->r_offset) |
			(g << info->g_offset) |
			(b << info->b_offset);

		if (info->a_length) {
			u16 a = BIT(info->a_length) - 1;
			fbdev->pseudo_palette[i] |= (a << info->a_offset);
		}
	}

	fbdev->info->var.bits_per_pixel = adf_format_bpp(format);
	fbdev->info->var.red.length = info->r_length;
	fbdev->info->var.red.offset = info->r_offset;
	fbdev->info->var.green.length = info->g_length;
	fbdev->info->var.green.offset = info->g_offset;
	fbdev->info->var.blue.length = info->b_length;
	fbdev->info->var.blue.offset = info->b_offset;
	fbdev->info->var.transp.length = info->a_length;
	fbdev->info->var.transp.offset = info->a_offset;
	fbdev->format = format;
}

static void adf_fbdev_fill_modelist(struct adf_fbdev *fbdev)
{
	struct drm_mode_modeinfo *modelist;
	struct fb_videomode fbmode;
	size_t n_modes, i;
	int ret = 0;

	n_modes = adf_interface_modelist(fbdev->intf, NULL, 0);
	modelist = kzalloc(sizeof(modelist[0]) * n_modes, GFP_KERNEL);
	if (!modelist) {
		dev_warn(fbdev->info->dev, "allocating new modelist failed; keeping old modelist\n");
		return;
	}
	adf_interface_modelist(fbdev->intf, modelist, n_modes);

	fb_destroy_modelist(&fbdev->info->modelist);

	for (i = 0; i < n_modes; i++) {
		adf_modeinfo_to_fb_videomode(&modelist[i], &fbmode);
		ret = fb_add_videomode(&fbmode, &fbdev->info->modelist);
		if (ret < 0)
			dev_warn(fbdev->info->dev, "adding mode %s to modelist failed: %d\n",
					modelist[i].name, ret);
	}

	kfree(modelist);
}

/**
 * adf_fbdev_open - default implementation of fbdev open op
 */
int adf_fbdev_open(struct fb_info *info, int user)
{
	struct adf_fbdev *fbdev = info->par;
	int ret;

	mutex_lock(&fbdev->refcount_lock);

	if (unlikely(fbdev->refcount == UINT_MAX)) {
		ret = -EMFILE;
		goto done;
	}

	if (!fbdev->refcount) {
		struct drm_mode_modeinfo mode;
		struct fb_videomode fbmode;
		struct adf_device *dev = adf_interface_parent(fbdev->intf);

		ret = adf_device_attach(dev, fbdev->eng, fbdev->intf);
		if (ret < 0 && ret != -EALREADY)
			goto done;

		ret = adf_fb_alloc(fbdev);
		if (ret < 0)
			goto done;

		adf_interface_current_mode(fbdev->intf, &mode);
		adf_modeinfo_to_fb_videomode(&mode, &fbmode);
		fb_videomode_to_var(&fbdev->info->var, &fbmode);

		adf_fbdev_set_format(fbdev, fbdev->default_format);
		adf_fbdev_fill_modelist(fbdev);
	}

	ret = adf_fbdev_post(fbdev);
	if (ret < 0) {
		if (!fbdev->refcount)
			adf_fb_destroy(fbdev);
		goto done;
	}

	fbdev->refcount++;
done:
	mutex_unlock(&fbdev->refcount_lock);
	return ret;
}
EXPORT_SYMBOL(adf_fbdev_open);

/**
 * adf_fbdev_release - default implementation of fbdev release op
 */
int adf_fbdev_release(struct fb_info *info, int user)
{
	struct adf_fbdev *fbdev = info->par;
	mutex_lock(&fbdev->refcount_lock);
	BUG_ON(!fbdev->refcount);
	fbdev->refcount--;
	if (!fbdev->refcount)
		adf_fb_destroy(fbdev);
	mutex_unlock(&fbdev->refcount_lock);
	return 0;
}
EXPORT_SYMBOL(adf_fbdev_release);

/**
 * adf_fbdev_check_var - default implementation of fbdev check_var op
 */
int adf_fbdev_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct adf_fbdev *fbdev = info->par;
	bool valid_format = true;
	u32 format = drm_fourcc_from_fb_var(var);
	u32 pitch = var->xres_virtual * var->bits_per_pixel / 8;

	if (!format) {
		dev_dbg(info->dev, "%s: unrecognized format\n", __func__);
		valid_format = false;
	}

	if (valid_format && var->grayscale) {
		dev_dbg(info->dev, "%s: grayscale modes not supported\n",
				__func__);
		valid_format = false;
	}

	if (valid_format && var->nonstd) {
		dev_dbg(info->dev, "%s: nonstandard formats not supported\n",
				__func__);
		valid_format = false;
	}

	if (valid_format && !adf_overlay_engine_supports_format(fbdev->eng,
			format)) {
		char format_str[ADF_FORMAT_STR_SIZE];
		adf_format_str(format, format_str);
		dev_dbg(info->dev, "%s: format %s not supported by overlay engine %s\n",
				__func__, format_str, fbdev->eng->base.name);
		valid_format = false;
	}

	if (valid_format && pitch > fbdev->pitch) {
		dev_dbg(info->dev, "%s: fb pitch too small for var (pitch = %u, xres_virtual = %u, bits_per_pixel = %u)\n",
				__func__, fbdev->pitch, var->xres_virtual,
				var->bits_per_pixel);
		valid_format = false;
	}

	if (valid_format && var->yres_virtual > fbdev->default_yres_virtual) {
		dev_dbg(info->dev, "%s: fb height too small for var (h = %u, yres_virtual = %u)\n",
				__func__, fbdev->default_yres_virtual,
				var->yres_virtual);
		valid_format = false;
	}

	if (valid_format) {
		var->activate = info->var.activate;
		var->height = info->var.height;
		var->width = info->var.width;
		var->accel_flags = info->var.accel_flags;
		var->rotate = info->var.rotate;
		var->colorspace = info->var.colorspace;
		/* userspace can't change these */
	} else {
		/* if any part of the format is invalid then fixing it up is
		   impractical, so save just the modesetting bits and
		   overwrite everything else */
		struct fb_videomode mode;
		fb_var_to_videomode(&mode, var);
		memcpy(var, &info->var, sizeof(*var));
		fb_videomode_to_var(var, &mode);
	}

	return 0;
}
EXPORT_SYMBOL(adf_fbdev_check_var);

/**
 * adf_fbdev_set_par - default implementation of fbdev set_par op
 */
int adf_fbdev_set_par(struct fb_info *info)
{
	struct adf_fbdev *fbdev = info->par;
	struct adf_interface *intf = fbdev->intf;
	struct fb_videomode vmode;
	struct drm_mode_modeinfo mode;
	int ret;
	u32 format = drm_fourcc_from_fb_var(&info->var);

	fb_var_to_videomode(&vmode, &info->var);
	adf_modeinfo_from_fb_videomode(&vmode, &mode);
	ret = adf_interface_set_mode(intf, &mode);
	if (ret < 0)
		return ret;

	ret = adf_fbdev_post(fbdev);
	if (ret < 0)
		return ret;

	if (format != fbdev->format)
		adf_fbdev_set_format(fbdev, format);

	return 0;
}
EXPORT_SYMBOL(adf_fbdev_set_par);

/**
 * adf_fbdev_blank - default implementation of fbdev blank op
 */
int adf_fbdev_blank(int blank, struct fb_info *info)
{
	struct adf_fbdev *fbdev = info->par;
	struct adf_interface *intf = fbdev->intf;
	u8 dpms_state;

	switch (blank) {
	case FB_BLANK_UNBLANK:
		dpms_state = DRM_MODE_DPMS_ON;
		break;
	case FB_BLANK_NORMAL:
		dpms_state = DRM_MODE_DPMS_STANDBY;
		break;
	case FB_BLANK_VSYNC_SUSPEND:
		dpms_state = DRM_MODE_DPMS_SUSPEND;
		break;
	case FB_BLANK_HSYNC_SUSPEND:
		dpms_state = DRM_MODE_DPMS_STANDBY;
		break;
	case FB_BLANK_POWERDOWN:
		dpms_state = DRM_MODE_DPMS_OFF;
		break;
	default:
		return -EINVAL;
	}

	return adf_interface_blank(intf, dpms_state);
}
EXPORT_SYMBOL(adf_fbdev_blank);

/**
 * adf_fbdev_pan_display - default implementation of fbdev pan_display op
 */
int adf_fbdev_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct adf_fbdev *fbdev = info->par;
	return adf_fbdev_post(fbdev);
}
EXPORT_SYMBOL(adf_fbdev_pan_display);

/**
 * adf_fbdev_mmap - default implementation of fbdev mmap op
 */
int adf_fbdev_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct adf_fbdev *fbdev = info->par;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	return dma_buf_mmap(fbdev->dma_buf, vma, 0);
}
EXPORT_SYMBOL(adf_fbdev_mmap);

/**
 * adf_fbdev_init - initialize helper to wrap ADF device in fbdev API
 *
 * @fbdev: the fbdev helper
 * @interface: the ADF interface that will display the framebuffer
 * @eng: the ADF overlay engine that will scan out the framebuffer
 * @xres_virtual: the virtual width of the framebuffer
 * @yres_virtual: the virtual height of the framebuffer
 * @format: the format of the framebuffer
 * @fbops: the device's fbdev ops
 * @fmt: formatting for the framebuffer identification string
 * @...: variable arguments
 *
 * @format must be a standard, non-indexed RGB format, i.e.,
 * adf_format_is_rgb(@format) && @format != @DRM_FORMAT_C8.
 *
 * Returns 0 on success or -errno on failure.
 */
int adf_fbdev_init(struct adf_fbdev *fbdev, struct adf_interface *interface,
		struct adf_overlay_engine *eng,
		u16 xres_virtual, u16 yres_virtual, u32 format,
		struct fb_ops *fbops, const char *fmt, ...)
{
	struct adf_device *parent = adf_interface_parent(interface);
	struct device *dev = &parent->base.dev;
	u16 width_mm, height_mm;
	va_list args;
	int ret;

	if (!adf_format_is_rgb(format) ||
			format == DRM_FORMAT_C8) {
		dev_err(dev, "fbdev helper does not support format %u\n",
				format);
		return -EINVAL;
	}

	memset(fbdev, 0, sizeof(*fbdev));
	fbdev->intf = interface;
	fbdev->eng = eng;
	fbdev->info = framebuffer_alloc(0, dev);
	if (!fbdev->info) {
		dev_err(dev, "allocating framebuffer device failed\n");
		return -ENOMEM;
	}
	mutex_init(&fbdev->refcount_lock);
	fbdev->default_xres_virtual = xres_virtual;
	fbdev->default_yres_virtual = yres_virtual;
	fbdev->default_format = format;

	fbdev->info->flags = FBINFO_FLAG_DEFAULT;
	ret = adf_interface_get_screen_size(interface, &width_mm, &height_mm);
	if (ret < 0) {
		width_mm = 0;
		height_mm = 0;
	}
	fbdev->info->var.width = width_mm;
	fbdev->info->var.height = height_mm;
	fbdev->info->var.activate = FB_ACTIVATE_VBL;
	va_start(args, fmt);
	vsnprintf(fbdev->info->fix.id, sizeof(fbdev->info->fix.id), fmt, args);
	va_end(args);
	fbdev->info->fix.type = FB_TYPE_PACKED_PIXELS;
	fbdev->info->fix.visual = FB_VISUAL_TRUECOLOR;
	fbdev->info->fix.xpanstep = 1;
	fbdev->info->fix.ypanstep = 1;
	INIT_LIST_HEAD(&fbdev->info->modelist);
	fbdev->info->fbops = fbops;
	fbdev->info->pseudo_palette = fbdev->pseudo_palette;
	fbdev->info->par = fbdev;

	ret = register_framebuffer(fbdev->info);
	if (ret < 0) {
		dev_err(dev, "registering framebuffer failed: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(adf_fbdev_init);

/**
 * adf_fbdev_destroy - destroy helper to wrap ADF device in fbdev API
 *
 * @fbdev: the fbdev helper
 */
void adf_fbdev_destroy(struct adf_fbdev *fbdev)
{
	unregister_framebuffer(fbdev->info);
	BUG_ON(fbdev->refcount);
	mutex_destroy(&fbdev->refcount_lock);
	framebuffer_release(fbdev->info);
}
EXPORT_SYMBOL(adf_fbdev_destroy);
