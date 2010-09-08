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
#include <linux/nvhost.h>

#include <asm/atomic.h>

#include <mach/dc.h>
#include <mach/fb.h>

struct tegra_fb_info {
	struct tegra_dc_win	*win;
	struct nvhost_device	*ndev;
	struct fb_info		*info;
	bool			valid;

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
	if ((var->yres * var->xres * var->bits_per_pixel / 8 * 2) >
	    info->screen_size)
		return -EINVAL;

	/* double yres_virtual to allow double buffering through pan_display */
	var->yres_virtual = var->yres * 2;

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

	case 0:
		break;

	default:
		return -EINVAL;
	}
	info->fix.line_length = var->xres * var->bits_per_pixel / 8;

	if (var->pixclock) {
		struct tegra_dc_mode mode;

		info->mode = (struct fb_videomode *)
			fb_find_best_mode(var, &info->modelist);
		if (!info->mode) {
			dev_warn(&tegra_fb->ndev->dev, "can't match video mode\n");
			return -EINVAL;
		}

		mode.pclk = PICOS2KHZ(info->mode->pixclock) * 1000;
		mode.h_ref_to_sync = 1;
		mode.v_ref_to_sync = 1;
		mode.h_sync_width = info->mode->hsync_len;
		mode.v_sync_width = info->mode->vsync_len;
		mode.h_back_porch = info->mode->left_margin;
		mode.v_back_porch = info->mode->upper_margin;
		mode.h_active = info->mode->xres;
		mode.v_active = info->mode->yres;
		mode.h_front_porch = info->mode->right_margin;
		mode.v_front_porch = info->mode->lower_margin;

		tegra_dc_set_mode(tegra_fb->win->dc, &mode);

		tegra_fb->win->w = info->mode->xres;
		tegra_fb->win->h = info->mode->xres;
		tegra_fb->win->out_w = info->mode->xres;
		tegra_fb->win->out_h = info->mode->xres;
	}
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

static int tegra_fb_blank(int blank, struct fb_info *info)
{
	struct tegra_fb_info *tegra_fb = info->par;

	switch (blank) {
	case FB_BLANK_UNBLANK:
		dev_dbg(&tegra_fb->ndev->dev, "unblank\n");
		tegra_dc_enable(tegra_fb->win->dc);
		return 0;

	case FB_BLANK_POWERDOWN:
		dev_dbg(&tegra_fb->ndev->dev, "blank\n");
		tegra_dc_disable(tegra_fb->win->dc);
		return 0;

	default:
		return -ENOTTY;
	}
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
	.fb_blank = tegra_fb_blank,
	.fb_pan_display = tegra_fb_pan_display,
	.fb_fillrect = tegra_fb_fillrect,
	.fb_copyarea = tegra_fb_copyarea,
	.fb_imageblit = tegra_fb_imageblit,
};

void tegra_fb_update_monspecs(struct tegra_fb_info *fb_info,
			      struct fb_monspecs *specs,
			      bool (*mode_filter)(struct fb_videomode *mode))
{
	struct fb_event event;
	int i;

	mutex_lock(&fb_info->info->lock);
	fb_destroy_modedb(fb_info->info->monspecs.modedb);

	memcpy(&fb_info->info->monspecs, specs,
	       sizeof(fb_info->info->monspecs));

	fb_destroy_modelist(&fb_info->info->modelist);

	for (i = 0; i < specs->modedb_len; i++) {
		if (mode_filter) {
			if (mode_filter(&specs->modedb[i]))
				fb_add_videomode(&specs->modedb[i],
						 &fb_info->info->modelist);
		} else {
			fb_add_videomode(&specs->modedb[i],
					 &fb_info->info->modelist);
		}
	}

	fb_info->info->mode = (struct fb_videomode *)
		fb_find_best_display(specs, &fb_info->info->modelist);

	event.info = fb_info->info;
	fb_notifier_call_chain(FB_EVENT_NEW_MODELIST, &event);
	mutex_unlock(&fb_info->info->lock);
}

struct tegra_fb_info *tegra_fb_register(struct nvhost_device *ndev,
					struct tegra_dc *dc,
					struct tegra_fb_data *fb_data,
					struct resource *fb_mem)
{
	struct tegra_dc_win *win;
	struct fb_info *info;
	struct tegra_fb_info *tegra_fb;
	void __iomem *fb_base = NULL;
	unsigned long fb_size = 0;
	unsigned long fb_phys = 0;
	int ret = 0;

	win = tegra_dc_get_window(dc, fb_data->win);
	if (!win) {
		dev_err(&ndev->dev, "dc does not have a window at index %d\n",
			fb_data->win);
		return ERR_PTR(-ENOENT);
	}

	info = framebuffer_alloc(sizeof(struct tegra_fb_info), &ndev->dev);
	if (!info) {
		ret = -ENOMEM;
		goto err;
	}

	tegra_fb = info->par;
	tegra_fb->win = win;
	tegra_fb->ndev = ndev;
	tegra_fb->fb_mem = fb_mem;
	tegra_fb->xres = fb_data->xres;
	tegra_fb->yres = fb_data->yres;
	atomic_set(&tegra_fb->in_use, 0);

	if (fb_mem) {
		fb_size = resource_size(fb_mem);
		fb_phys = fb_mem->start;
		fb_base = ioremap_nocache(fb_phys, fb_size);
		if (!fb_base) {
			dev_err(&ndev->dev, "fb can't be mapped\n");
			ret = -EBUSY;
			goto err_free;
		}
		tegra_fb->valid = true;
	}

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
	info->var.yres_virtual		= fb_data->yres * 2;
	info->var.bits_per_pixel	= fb_data->bits_per_pixel;
	info->var.activate		= FB_ACTIVATE_VBL;
	/* TODO: fill in the following by querying the DC */
	info->var.height		= -1;
	info->var.width			= -1;
	info->var.pixclock		= 0;
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
	win->out_x = 0;
	win->out_y = 0;
	win->out_w = fb_data->xres;
	win->out_h = fb_data->yres;
	win->z = 0;
	win->phys_addr = fb_phys;
	win->virt_addr = fb_base;
	win->stride = fb_data->xres * fb_data->bits_per_pixel / 8;
	win->flags = TEGRA_WIN_FLAG_ENABLED;

	if (fb_mem)
		tegra_fb_set_par(info);

	if (register_framebuffer(info)) {
		dev_err(&ndev->dev, "failed to register framebuffer\n");
		ret = -ENODEV;
		goto err_iounmap_fb;
	}

	tegra_fb->info = info;

	dev_info(&ndev->dev, "probed\n");

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
