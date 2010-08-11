/*
 * drivers/video/tegra/fb.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Erik Gilling <konkers@android.com>
 *         Colin Cross <ccross@android.com>
 *         Travis Geiselbrecht <travis@palm.com>
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

#include <linux/fb.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

#include <asm/atomic.h>

#include <mach/dc.h>
#include <mach/fb.h>

struct tegra_fb_info {
	struct tegra_dc_win	*win;
	struct platform_device	*pdev;
	struct fb_info		*info;

	struct resource		*fb_mem;

	int			xres;
	int			yres;

	atomic_t		in_use;
};

/* palette array used by the fbcon */
static u32 pseudo_palette[16];

static int tegra_fb_open(struct fb_info *info, int user)
{
	struct tegra_fb_info *tegra_fb = info->par;

	if (atomic_xchg(&tegra_fb->in_use, 1))
		return -EBUSY;

	return 0;
}

static int tegra_fb_release(struct fb_info *info, int user)
{
	struct tegra_fb_info *tegra_fb = info->par;

	WARN_ON(!atomic_xchg(&tegra_fb->in_use, 0));

	return 0;
}

static int tegra_fb_check_var(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	if ((var->xres != info->var.xres) ||
	    (var->yres != info->var.yres) ||
	    (var->xres_virtual != info->var.xres_virtual) ||
	    (var->yres_virtual != info->var.yres_virtual) ||
	    (var->grayscale != info->var.grayscale))
		return -EINVAL;
	return 0;
}

static int tegra_fb_set_par(struct fb_info *info)
{
	struct tegra_fb_info *tegra_fb = info->par;
	struct fb_var_screeninfo *var = &info->var;

	/* we only support RGB ordering for now */
	switch (var->bits_per_pixel) {
	case 32:
	case 24:
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 16;
		var->blue.length = 8;
		tegra_fb->win->fmt = TEGRA_WIN_FMT_R8G8B8A8;
		break;
	case 16:
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		tegra_fb->win->fmt = TEGRA_WIN_FMT_B5G6R5;
		break;
	default:
		return -EINVAL;
	}
	info->fix.line_length = var->xres * var->bits_per_pixel / 8;

	tegra_dc_update_windows(&tegra_fb->win, 1);

	return 0;
}

static int tegra_fb_setcolreg(unsigned regno, unsigned red, unsigned green,
	unsigned blue, unsigned transp, struct fb_info *info)
{
	struct fb_var_screeninfo *var = &info->var;

	if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
	    info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
		u32 v;

		if (regno >= 16)
			return -EINVAL;

		v = (red << var->red.offset) |
			(green << var->green.offset) |
			(blue << var->blue.offset);

		((u32 *)info->pseudo_palette)[regno] = v;
	}

	return 0;
}

static int tegra_fb_pan_display(struct fb_var_screeninfo *var,
				struct fb_info *info)
{
	struct tegra_fb_info *tegra_fb = info->par;
	char __iomem *flush_start;
	char __iomem *flush_end;
	u32 addr;

	flush_start = info->screen_base + (var->yoffset * info->fix.line_length);
	flush_end = flush_start + (var->yres * info->fix.line_length);

	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;

	addr = info->fix.smem_start + (var->yoffset * info->fix.line_length) +
		(var->xoffset * (var->bits_per_pixel/8));

	tegra_fb->win->phys_addr = addr;
	/* TODO: update virt_addr */

	tegra_dc_update_windows(&tegra_fb->win, 1);
	tegra_dc_sync_windows(&tegra_fb->win, 1);

	return 0;
}

static void tegra_fb_fillrect(struct fb_info *info,
			      const struct fb_fillrect *rect)
{
	cfb_fillrect(info, rect);
}

static void tegra_fb_copyarea(struct fb_info *info,
			      const struct fb_copyarea *region)
{
	cfb_copyarea(info, region);
}

static void tegra_fb_imageblit(struct fb_info *info,
			       const struct fb_image *image)
{
	cfb_imageblit(info, image);
}

static struct fb_ops tegra_fb_ops = {
	.owner = THIS_MODULE,
	.fb_open = tegra_fb_open,
	.fb_release = tegra_fb_release,
	.fb_check_var = tegra_fb_check_var,
	.fb_set_par = tegra_fb_set_par,
	.fb_setcolreg = tegra_fb_setcolreg,
	.fb_pan_display = tegra_fb_pan_display,
	.fb_fillrect = tegra_fb_fillrect,
	.fb_copyarea = tegra_fb_copyarea,
	.fb_imageblit = tegra_fb_imageblit,
};

struct tegra_fb_info *tegra_fb_register(struct platform_device *pdev,
					struct tegra_dc *dc,
					struct tegra_fb_data *fb_data,
					struct resource *fb_mem)
{
	struct tegra_dc_win *win;
	struct fb_info *info;
	struct tegra_fb_info *tegra_fb;
	void __iomem *fb_base;
	unsigned long fb_size;
	unsigned long fb_phys;
	int ret = 0;

	win = tegra_dc_get_window(dc, fb_data->win);
	if (!win) {
		dev_err(&pdev->dev, "dc does not have a window at index %d\n",
			fb_data->win);
		return ERR_PTR(-ENOENT);
	}

	info = framebuffer_alloc(sizeof(struct tegra_fb_info), &pdev->dev);
	if (!info) {
		ret = -ENOMEM;
		goto err;
	}

	fb_size = resource_size(fb_mem);
	fb_phys = fb_mem->start;
	fb_base = ioremap_nocache(fb_phys, fb_size);
	if (!fb_base) {
		dev_err(&pdev->dev, "fb can't be mapped\n");
		ret = -EBUSY;
		goto err_free;
	}

	tegra_fb = info->par;
	tegra_fb->win = win;
	tegra_fb->pdev = pdev;
	tegra_fb->fb_mem = fb_mem;
	tegra_fb->xres = fb_data->xres;
	tegra_fb->yres = fb_data->yres;
	atomic_set(&tegra_fb->in_use, 0);

	info->fbops = &tegra_fb_ops;
	info->pseudo_palette = pseudo_palette;
	info->screen_base = fb_base;
	info->screen_size = fb_size;

	strlcpy(info->fix.id, "tegra_fb", sizeof(info->fix.id));
	info->fix.type		= FB_TYPE_PACKED_PIXELS;
	info->fix.visual	= FB_VISUAL_TRUECOLOR;
	info->fix.xpanstep	= 1;
	info->fix.ypanstep	= 1;
	info->fix.accel		= FB_ACCEL_NONE;
	info->fix.smem_start	= fb_phys;
	info->fix.smem_len	= fb_size;

	info->var.xres			= fb_data->xres;
	info->var.yres			= fb_data->yres;
	info->var.xres_virtual		= fb_data->xres;
	info->var.yres_virtual		= fb_data->yres*2;
	info->var.bits_per_pixel	= fb_data->bits_per_pixel;
	info->var.activate		= FB_ACTIVATE_VBL;
	/* TODO: fill in the following by querying the DC */
	info->var.height		= -1;
	info->var.width			= -1;
	info->var.pixclock		= 24500;
	info->var.left_margin		= 0;
	info->var.right_margin		= 0;
	info->var.upper_margin		= 0;
	info->var.lower_margin		= 0;
	info->var.hsync_len		= 0;
	info->var.vsync_len		= 0;
	info->var.vmode			= FB_VMODE_NONINTERLACED;

	win->x = 0;
	win->y = 0;
	win->w = fb_data->xres;
	win->h = fb_data->yres;
	/* TODO: set to output res dc */
	win->out_w = fb_data->xres;
	win->out_h = fb_data->yres;
	win->phys_addr = fb_phys;
	win->virt_addr = fb_base;
	win->flags = TEGRA_WIN_FLAG_ENABLED | TEGRA_WIN_FLAG_COLOR_EXPAND;

	tegra_fb_set_par(info);

	if (register_framebuffer(info)) {
		dev_err(&pdev->dev, "failed to register framebuffer\n");
		ret = -ENODEV;
		goto err_iounmap_fb;
	}

	tegra_fb->info = info;

	dev_info(&pdev->dev, "probed\n");

	return tegra_fb;

err_iounmap_fb:
	iounmap(fb_base);
err_free:
	framebuffer_release(info);
err:
	return ERR_PTR(ret);
}

void tegra_fb_unregister(struct tegra_fb_info *fb_info)
{
	struct fb_info *info = fb_info->info;

	unregister_framebuffer(info);
	iounmap(info->screen_base);
	framebuffer_release(info);
}
