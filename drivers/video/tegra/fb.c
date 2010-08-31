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
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/workqueue.h>

#include <asm/atomic.h>

#include <video/tegrafb.h>

#include <mach/dc.h>
#include <mach/fb.h>
#include <mach/nvhost.h>
#include <mach/nvmap.h>

#include "host/dev.h"
#include "nvmap/nvmap.h"

struct tegra_fb_info {
	struct tegra_dc_win	*win;
	struct nvhost_device	*ndev;
	struct fb_info		*info;
	bool			valid;

	struct resource		*fb_mem;

	int			xres;
	int			yres;

	atomic_t		in_use;
	struct nvmap_client	*user_nvmap;
	struct nvmap_client	*fb_nvmap;

	struct workqueue_struct	*flip_wq;
};

struct tegra_fb_flip_win {
	struct tegra_dc_win	win_data;
	struct tegra_dc_win	*dc_win;
	s32			pre_syncpt_id;
	u32			pre_syncpt_val;
};

struct tegra_fb_flip_data {
	struct work_struct	work;
	struct tegra_fb_info	*fb;
	struct tegra_fb_flip_win windows[TEGRA_FB_FLIP_N_WINDOWS];
	u32			syncpt_max;
};

/* palette array used by the fbcon */
static u32 pseudo_palette[16];

static int tegra_fb_open(struct fb_info *info, int user)
{
	struct tegra_fb_info *tegra_fb = info->par;

	if (atomic_xchg(&tegra_fb->in_use, 1))
		return -EBUSY;

	tegra_fb->user_nvmap = NULL;

	return 0;
}

static int tegra_fb_release(struct fb_info *info, int user)
{
	struct tegra_fb_info *tegra_fb = info->par;

	flush_workqueue(tegra_fb->flip_wq);

	if (tegra_fb->user_nvmap) {
		nvmap_client_put(tegra_fb->user_nvmap);
		tegra_fb->user_nvmap = NULL;
	}

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

	info->fix.line_length = tegra_dc_compute_stride(var->xres,
			var->bits_per_pixel, TEGRA_WIN_LAYOUT_PITCH);
	tegra_fb->win->stride = info->fix.line_length;

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

	if (WARN_ON(tegra_fb->win->surface)) {
		nvmap_unpin(tegra_fb->fb_nvmap, tegra_fb->win->surface);
		nvmap_free(tegra_fb->fb_nvmap, tegra_fb->win->surface);
		tegra_fb->win->surface = NULL;
	}

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

/* TODO: implement ALLOC, FREE, BLANK ioctls */

static int tegra_fb_set_nvmap_fd(struct tegra_fb_info *tegra_fb, int fd)
{
	struct nvmap_client *nvmap = NULL;

	if (fd < 0)
		return -EINVAL;

	nvmap = nvmap_client_get_file(fd);
	if (IS_ERR(nvmap))
		return PTR_ERR(nvmap);

	if (tegra_fb->user_nvmap)
		nvmap_client_put(tegra_fb->user_nvmap);

	tegra_fb->user_nvmap = nvmap;

	return 0;
}

static int tegra_fb_set_windowattr(struct tegra_fb_info *tegra_fb,
				   struct tegra_dc_win *win,
				   const struct tegra_fb_windowattr *attr)
{
	struct nvmap_handle_ref *r_dupe;
	struct nvmap_handle *h_win;

	if (!attr->buff_id) {
		win->flags = 0;
		win->surface = NULL;
		return 0;
	}

	h_win = nvmap_get_handle_id(tegra_fb->user_nvmap, attr->buff_id);
	if (h_win == NULL) {
		dev_err(&tegra_fb->ndev->dev, "%s: flip invalid "
			"handle %08x\n", current->comm, attr->buff_id);
		return -EPERM;
	}

	/* duplicate the new framebuffer's handle into the fb driver's
	 * nvmap context, to ensure that the handle won't be freed as
	 * long as it is in-use by the fb driver */
	r_dupe = nvmap_duplicate_handle_id(tegra_fb->fb_nvmap, attr->buff_id);
	nvmap_handle_put(h_win);

	if (IS_ERR(r_dupe)) {
		dev_err(&tegra_fb->ndev->dev, "couldn't duplicate handle\n");
		return PTR_ERR(r_dupe);
	}

	win->surface = r_dupe;

	win->flags = TEGRA_WIN_FLAG_ENABLED;
	if (attr->blend == TEGRA_FB_WIN_BLEND_PREMULT)
		win->flags |= TEGRA_WIN_FLAG_BLEND_PREMULT;
	else if (attr->blend == TEGRA_FB_WIN_BLEND_COVERAGE)
		win->flags |= TEGRA_WIN_FLAG_BLEND_COVERAGE;
	win->fmt = attr->pixformat;
	win->x = attr->x;
	win->y = attr->y;
	win->w = attr->w;
	win->h = attr->h;
	win->out_x = attr->out_x;
	win->out_y = attr->out_y;
	win->out_w = attr->out_w;
	win->out_h = attr->out_h;
	win->z = attr->z;

	win->phys_addr = nvmap_pin(tegra_fb->fb_nvmap, r_dupe);
	if (IS_ERR((void *)win->phys_addr)) {
		dev_err(&tegra_fb->ndev->dev, "couldn't pin handle\n");
		nvmap_free(tegra_fb->fb_nvmap, r_dupe);
		return (int)win->phys_addr;
	}
	/* STOPSHIP verify that this won't read outside of the surface */
	win->phys_addr += attr->offset;
	win->stride = attr->stride;

	return 0;
}

static void tegra_fb_flip_work(struct work_struct *work)
{
	struct tegra_fb_flip_data *data;
	struct tegra_dc_win *wins[TEGRA_FB_FLIP_N_WINDOWS];
	struct nvmap_handle_ref *surfs[TEGRA_FB_FLIP_N_WINDOWS];
	int i, nr_win = 0, nr_unpin = 0;

	data = container_of(work, struct tegra_fb_flip_data, work);

	for (i = 0; i < TEGRA_FB_FLIP_N_WINDOWS; i++) {
		struct tegra_fb_flip_win *flip_win = &data->windows[i];

		if (!flip_win->dc_win)
			continue;

		if (flip_win->dc_win->flags && flip_win->dc_win->surface)
			surfs[nr_unpin++] = flip_win->dc_win->surface;

		wins[nr_win++] = flip_win->dc_win;

		flip_win->dc_win->flags = flip_win->win_data.flags;
		if (!flip_win->dc_win->flags)
			continue;

		flip_win->dc_win->surface = flip_win->win_data.surface;
		flip_win->dc_win->fmt = flip_win->win_data.fmt;
		flip_win->dc_win->x = flip_win->win_data.x;
		flip_win->dc_win->y = flip_win->win_data.y;
		flip_win->dc_win->w = flip_win->win_data.w;
		flip_win->dc_win->h = flip_win->win_data.h;
		flip_win->dc_win->out_x = flip_win->win_data.out_x;
		flip_win->dc_win->out_y = flip_win->win_data.out_y;
		flip_win->dc_win->out_w = flip_win->win_data.out_w;
		flip_win->dc_win->out_h = flip_win->win_data.out_h;
		flip_win->dc_win->z = flip_win->win_data.z;
		flip_win->dc_win->phys_addr = flip_win->win_data.phys_addr;
		flip_win->dc_win->stride = flip_win->win_data.stride;

		if (flip_win->pre_syncpt_id < 0)
			continue;

		nvhost_syncpt_wait_timeout(&data->fb->ndev->host->syncpt,
					   flip_win->pre_syncpt_id,
					   flip_win->pre_syncpt_val,
					   msecs_to_jiffies(500));
	}

	if (!nr_win)
		goto free_data;

	tegra_dc_update_windows(wins, nr_win);
	/* TODO: implement swapinterval here */
	tegra_dc_sync_windows(wins, nr_win);

	tegra_dc_incr_syncpt_min(data->fb->win->dc, data->syncpt_max);

	/* unpin and deref previous front buffers */
	for (i = 0; i < nr_unpin; i++) {
		nvmap_unpin(data->fb->fb_nvmap, surfs[i]);
		nvmap_free(data->fb->fb_nvmap, surfs[i]);
	}

free_data:
	kfree(data);
}

static int tegra_fb_flip(struct tegra_fb_info *tegra_fb,
			 struct tegra_fb_flip_args *args)
{
	struct tegra_fb_flip_data *data;
	struct tegra_fb_flip_win *flip_win;
	struct tegra_dc *dc = tegra_fb->win->dc;
	u32 syncpt_max;
	int i, err;

	if (WARN_ON(!tegra_fb->user_nvmap))
		return -EFAULT;

	if (WARN_ON(!tegra_fb->ndev))
		return -EFAULT;

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(&tegra_fb->ndev->dev, "no memory for flip\n");
		return -ENOMEM;
	}

	INIT_WORK(&data->work, tegra_fb_flip_work);
	data->fb = tegra_fb;

	for (i = 0; i < TEGRA_FB_FLIP_N_WINDOWS; i++) {

		flip_win = &data->windows[i];
		flip_win->dc_win = tegra_dc_get_window(dc, args->win[i].index);
		flip_win->pre_syncpt_id = args->win[i].pre_syncpt_id;
		flip_win->pre_syncpt_val = args->win[i].pre_syncpt_val;

		if (!flip_win->dc_win)
			continue;

		err = tegra_fb_set_windowattr(tegra_fb, &flip_win->win_data,
					      &args->win[i]);
		if (err) {
			dev_err(&tegra_fb->ndev->dev, "error setting window "
				"attributes\n");
			goto surf_err;
		}
	}

	syncpt_max = tegra_dc_incr_syncpt_max(dc);
	data->syncpt_max = syncpt_max;

	queue_work(tegra_fb->flip_wq, &data->work);

	args->post_syncpt_val = syncpt_max;
	args->post_syncpt_id = tegra_dc_get_syncpt_id(dc);

	return 0;

surf_err:
	while (i--) {
		if (data->windows[i].win_data.surface) {
			nvmap_unpin(tegra_fb->fb_nvmap,
				    data->windows[i].win_data.surface);
			nvmap_free(tegra_fb->fb_nvmap,
				   data->windows[i].win_data.surface);
		}
	}
	kfree(data);
	return err;
}

/* TODO: implement private window ioctls to set overlay x,y */

static int tegra_fb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	struct tegra_fb_info *tegra_fb = info->par;
	struct tegra_fb_flip_args flip_args;
	int fd;
	int ret;

	switch (cmd) {
	case FBIO_TEGRA_SET_NVMAP_FD:
		if (copy_from_user(&fd, (void __user *)arg, sizeof(fd)))
			return -EFAULT;

		return tegra_fb_set_nvmap_fd(tegra_fb, fd);

	case FBIO_TEGRA_FLIP:
		if (copy_from_user(&flip_args, (void __user *)arg, sizeof(flip_args)))
			return -EFAULT;

		ret = tegra_fb_flip(tegra_fb, &flip_args);

		if (copy_to_user((void __user *)arg, &flip_args, sizeof(flip_args)))
			return -EFAULT;

		return ret;

	default:
		return -ENOTTY;
	}

	return 0;
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
	.fb_ioctl = tegra_fb_ioctl,
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
	tegra_fb->fb_nvmap = nvmap_create_client(nvmap_dev);
	if (!tegra_fb->fb_nvmap) {
		dev_err(&ndev->dev, "couldn't create nvmap client\n");
		ret = -ENOMEM;
		goto err_free;
	}
	atomic_set(&tegra_fb->in_use, 0);

	tegra_fb->flip_wq = create_singlethread_workqueue(dev_name(&ndev->dev));
	if (!tegra_fb->flip_wq) {
		dev_err(&ndev->dev, "couldn't create flip work-queue\n");
		ret = -ENOMEM;
		goto err_delete_wq;
	}

	if (fb_mem) {
		fb_size = resource_size(fb_mem);
		fb_phys = fb_mem->start;
		fb_base = ioremap_nocache(fb_phys, fb_size);
		if (!fb_base) {
			dev_err(&ndev->dev, "fb can't be mapped\n");
			ret = -EBUSY;
			goto err_put_client;
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
	win->layout = TEGRA_WIN_LAYOUT_PITCH;
	win->stride = tegra_dc_compute_stride(fb_data->xres,
				      fb_data->bits_per_pixel, win->layout);
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
err_put_client:
	nvmap_client_put(tegra_fb->fb_nvmap);
err_delete_wq:
	destroy_workqueue(tegra_fb->flip_wq);
err_free:
	framebuffer_release(info);
err:
	return ERR_PTR(ret);
}

void tegra_fb_unregister(struct tegra_fb_info *fb_info)
{
	struct fb_info *info = fb_info->info;

	if (fb_info->win->surface) {
		nvmap_unpin(fb_info->fb_nvmap, fb_info->win->surface);
		nvmap_free(fb_info->fb_nvmap, fb_info->win->surface);
	}

	if (fb_info->fb_nvmap)
		nvmap_client_put(fb_info->fb_nvmap);

	unregister_framebuffer(info);

	flush_workqueue(fb_info->flip_wq);
	destroy_workqueue(fb_info->flip_wq);

	iounmap(info->screen_base);
	framebuffer_release(info);
}
