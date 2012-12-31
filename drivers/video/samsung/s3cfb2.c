/* linux/drivers/video/samsung/s3cfb2.c
 *
 * Core file for Samsung Display Controller (FIMD) driver
 *
 * Jinsung Yang, Copyright (c) 2009 Samsung Electronics
 *	http://www.samsungsemi.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/irq.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/ctype.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <linux/io.h>
#include <linux/memory.h>
#include <plat/clock.h>

#include "logo_rgb24.h"
#include "s3cfb2.h"

static struct s3cfb_global *ctrl;

struct s3c_platform_fb *to_fb_plat(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	return (struct s3c_platform_fb *) pdev->dev.platform_data;
}

#ifndef CONFIG_FRAMEBUFFER_CONSOLE
static int s3cfb_draw_logo(struct fb_info *fb)
{
	struct s3c_platform_fb *pdata = to_fb_plat(ctrl->dev);
	struct fb_fix_screeninfo *fix = &fb->fix;
	struct fb_var_screeninfo *var = &fb->var;

	memcpy(ctrl->fb[pdata->default_win]->screen_base,
		LOGO_RGB24, fix->line_length * var->yres);

	return 0;
}
#else
int fb_is_primary_device(struct fb_info *fb)
{
	struct s3c_platform_fb *pdata = to_fb_plat(ctrl->dev);
	struct s3cfb_window *win = fb->par;

	dev_dbg(ctrl->dev, "[fb%d] checking for primary device\n", win->id);

	if (win->id == pdata->default_win)
		return 1;
	else
		return 0;
}
#endif

static irqreturn_t s3cfb_irq_frame(int irq, void *dev_id)
{
	s3cfb_clear_interrupt(ctrl);

	ctrl->wq_count++;
	wake_up_interruptible(&ctrl->wq);

	return IRQ_HANDLED;
}

#ifdef CONFIG_FB_S3C_V2_TRACE_UNDERRUN
static irqreturn_t s3cfb_irq_fifo(int irq, void *dev_id)
{
	s3cfb_clear_interrupt(ctrl);

	return IRQ_HANDLED;
}
#endif

static int s3cfb_enable_window(int id)
{
	struct s3cfb_window *win = ctrl->fb[id]->par;

	if (s3cfb_window_on(ctrl, id)) {
		win->enabled = 0;
		return -EFAULT;
	} else {
		win->enabled = 1;
		return 0;
	}
}

static int s3cfb_disable_window(int id)
{
	struct s3cfb_window *win = ctrl->fb[id]->par;

	if (s3cfb_window_off(ctrl, id)) {
		win->enabled = 1;
		return -EFAULT;
	} else {
		win->enabled = 0;
		return 0;
	}
}

static int s3cfb_init_global(void)
{
	ctrl->output = OUTPUT_RGB;
	ctrl->rgb_mode = MODE_RGB_P;

	ctrl->wq_count = 0;
	init_waitqueue_head(&ctrl->wq);
	mutex_init(&ctrl->lock);

	s3cfb_set_output(ctrl);
	s3cfb_set_display_mode(ctrl);
	s3cfb_set_polarity(ctrl);
	s3cfb_set_timing(ctrl);
	s3cfb_set_lcd_size(ctrl);

	return 0;
}

static int s3cfb_map_video_memory(struct fb_info *fb)
{
	struct fb_fix_screeninfo *fix = &fb->fix;
	struct s3cfb_window *win = fb->par;

	if (win->path == DATA_PATH_FIFO)
		return 0;

	fb->screen_base = dma_alloc_writecombine(ctrl->dev,
				PAGE_ALIGN(fix->smem_len),
				(unsigned int *) &fix->smem_start, GFP_KERNEL);
	if (!fb->screen_base)
		return -ENOMEM;
	else
		dev_info(ctrl->dev, "[fb%d] dma: 0x%08x, cpu: 0x%08x, size: 0x%08x\n",
			win->id, (unsigned int) fix->smem_start,
			(unsigned int) fb->screen_base, fix->smem_len);

	memset(fb->screen_base, 0, fix->smem_len);

	return 0;
}

static int s3cfb_unmap_video_memory(struct fb_info *fb)
{
	struct fb_fix_screeninfo *fix = &fb->fix;
	struct s3cfb_window *win = fb->par;

	if (fix->smem_start) {
		dma_free_writecombine(ctrl->dev, fix->smem_len,
			fb->screen_base, fix->smem_start);
		fix->smem_start = 0;
		fix->smem_len = 0;
		dev_info(ctrl->dev, "[fb%d] video memory released\n", win->id);
	}

	return 0;
}

static int s3cfb_set_bitfield(struct fb_var_screeninfo *var)
{
	switch (var->bits_per_pixel) {
	case 16:
		if (var->transp.length == 1) {
			var->red.offset = 10;
			var->red.length = 5;
			var->green.offset = 5;
			var->green.length = 5;
			var->blue.offset = 0;
			var->blue.length = 5;
			var->transp.offset = 15;
		} else if (var->transp.length == 4) {
			var->red.offset = 8;
			var->red.length = 4;
			var->green.offset = 4;
			var->green.length = 4;
			var->blue.offset = 0;
			var->blue.length = 4;
			var->transp.offset = 12;
		} else {
			var->red.offset = 11;
			var->red.length = 5;
			var->green.offset = 5;
			var->green.length = 6;
			var->blue.offset = 0;
			var->blue.length = 5;
			var->transp.offset = 0;
		}
		break;

	case 24:
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;

	case 32:
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 24;
		break;
	}

	return 0;
}

static int s3cfb_set_alpha_info(struct fb_var_screeninfo *var,
				struct s3cfb_window *win)
{
	if (var->transp.length > 0)
		win->alpha.mode = PIXEL_BLENDING;
	else {
		win->alpha.mode = PLANE_BLENDING;
		win->alpha.channel = 0;
		win->alpha.value = S3CFB_AVALUE(0xf, 0xf, 0xf);
	}

	return 0;
}

static int s3cfb_check_var(struct fb_var_screeninfo *var,
				struct fb_info *fb)
{
	struct s3c_platform_fb *pdata = to_fb_plat(ctrl->dev);
	struct fb_fix_screeninfo *fix = &fb->fix;
	struct s3cfb_window *win = fb->par;
	struct s3cfb_lcd *lcd = ctrl->lcd;

	dev_dbg(ctrl->dev, "[fb%d] check_var\n", win->id);

	if (var->bits_per_pixel != 16 && var->bits_per_pixel != 24 &&
		var->bits_per_pixel != 32) {
		dev_err(ctrl->dev, "invalid bits per pixel\n");
		return -EINVAL;
	}

	if (var->xres > lcd->width)
		var->xres = lcd->width;

	if (var->yres > lcd->height)
		var->yres = lcd->height;

	if (var->xres_virtual != var->xres)
		var->xres_virtual = var->xres;

	if (var->yres_virtual > var->yres * (fb->fix.ypanstep + 1))
		var->yres_virtual = var->yres * (fb->fix.ypanstep + 1);

	if (var->xoffset != 0)
		var->xoffset = 0;

	if (var->yoffset + var->yres > var->yres_virtual)
		var->yoffset = var->yres_virtual - var->yres;

	if (win->x + var->xres > lcd->width)
		win->x = lcd->width - var->xres;

	if (win->y + var->yres > lcd->height)
		win->y = lcd->height - var->yres;

	/* modify the fix info */
	if (win->id != pdata->default_win) {
		fix->line_length = var->xres_virtual * var->bits_per_pixel / 8;
		fix->smem_len = fix->line_length * var->yres_virtual;
	}

	s3cfb_set_bitfield(var);
	s3cfb_set_alpha_info(var, win);

	return 0;
}

static int s3cfb_set_par(struct fb_info *fb)
{
	struct s3c_platform_fb *pdata = to_fb_plat(ctrl->dev);
	struct s3cfb_window *win = fb->par;

	dev_dbg(ctrl->dev, "[fb%d] set_par\n", win->id);

	if ((win->id != pdata->default_win) && !fb->fix.smem_start)
		s3cfb_map_video_memory(fb);

	s3cfb_set_window_control(ctrl, win->id);
	s3cfb_set_window_position(ctrl, win->id);
	s3cfb_set_window_size(ctrl, win->id);
	s3cfb_set_buffer_address(ctrl, win->id);
	s3cfb_set_buffer_size(ctrl, win->id);

	if (win->id > 0)
		s3cfb_set_alpha_blending(ctrl, win->id);

	return 0;
}

static int s3cfb_blank(int blank_mode, struct fb_info *fb)
{
	struct s3cfb_window *win = fb->par;

	dev_dbg(ctrl->dev, "change blank mode\n");

	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		if (fb->fix.smem_start)
			s3cfb_enable_window(win->id);
		else
			dev_info(ctrl->dev, "[fb%d] no allocated memory for unblank\n",
				win->id);
		break;

	case FB_BLANK_POWERDOWN:
		s3cfb_disable_window(win->id);
		break;

	default:
		dev_dbg(ctrl->dev, "unsupported blank mode\n");
		return -EINVAL;
	}

	return 0;
}

static int s3cfb_pan_display(struct fb_var_screeninfo *var,
				struct fb_info *fb)
{
	struct s3cfb_window *win = fb->par;

	if (var->yoffset + var->yres > var->yres_virtual) {
		dev_err(ctrl->dev, "invalid yoffset value\n");
		return -EINVAL;
	}

	fb->var.yoffset = var->yoffset;

	dev_dbg(ctrl->dev, "[fb%d] yoffset for pan display: %d\n", win->id,
		var->yoffset);

	s3cfb_set_buffer_address(ctrl, win->id);

	return 0;
}

static inline unsigned int __chan_to_field(unsigned int chan,
						struct fb_bitfield bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf.length;

	return chan << bf.offset;
}

static int s3cfb_setcolreg(unsigned int regno, unsigned int red,
				unsigned int green, unsigned int blue,
				unsigned int transp, struct fb_info *fb)
{
	unsigned int *pal = (unsigned int *) fb->pseudo_palette;
	unsigned int val = 0;

	if (regno < 16) {
		/* fake palette of 16 colors */
		val |= __chan_to_field(red, fb->var.red);
		val |= __chan_to_field(green, fb->var.green);
		val |= __chan_to_field(blue, fb->var.blue);
		val |= __chan_to_field(transp, fb->var.transp);

		pal[regno] = val;
	}

	return 0;
}

static int s3cfb_open(struct fb_info *fb, int user)
{
	struct s3c_platform_fb *pdata = to_fb_plat(ctrl->dev);
	struct s3cfb_window *win = fb->par;
	int ret = 0;

	mutex_lock(&ctrl->lock);

	if (atomic_read(&win->in_use)) {
		if (win->id == pdata->default_win) {
			dev_dbg(ctrl->dev,
				"multiple open for default window\n");
			ret = 0;
		} else {
			dev_dbg(ctrl->dev,
				"do not allow multiple open "
				"for non-default window\n");
			ret = -EBUSY;
		}
	} else
		atomic_inc(&win->in_use);

	mutex_unlock(&ctrl->lock);

	return ret;
}

static int s3cfb_release_window(struct fb_info *fb)
{
	struct s3c_platform_fb *pdata = to_fb_plat(ctrl->dev);
	struct s3cfb_window *win = fb->par;

	if (win->id != pdata->default_win) {
		s3cfb_disable_window(win->id);
		s3cfb_unmap_video_memory(fb);
		s3cfb_set_buffer_address(ctrl, win->id);
	}

	win->x = 0;
	win->y = 0;

	return 0;
}

static int s3cfb_release(struct fb_info *fb, int user)
{
	struct s3cfb_window *win = fb->par;

	s3cfb_release_window(fb);

	mutex_lock(&ctrl->lock);
	atomic_dec(&win->in_use);
	mutex_unlock(&ctrl->lock);

	return 0;
}

static int s3cfb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	/* nothing to do for removing cursor */
	return 0;
}

static int s3cfb_wait_for_vsync(void)
{
	int count = ctrl->wq_count;

	dev_dbg(ctrl->dev, "waiting for VSYNC interrupt\n");

	wait_event_interruptible_timeout(ctrl->wq,
			count != ctrl->wq_count, HZ / 10);

	dev_dbg(ctrl->dev, "got a VSYNC interrupt\n");

	return 0;
}

static int s3cfb_ioctl(struct fb_info *fb, unsigned int cmd,
			unsigned long arg)
{
	struct s3c_platform_fb *pdata = to_fb_plat(ctrl->dev);
	struct fb_var_screeninfo *var = &fb->var;
	struct s3cfb_window *win = fb->par, *win_temp;
	struct s3cfb_lcd *lcd = ctrl->lcd;
	int ret = 0, i;

	union {
		struct s3cfb_user_window user_window;
		struct s3cfb_user_plane_alpha user_alpha;
		struct s3cfb_user_chroma user_chroma;
		int vsync;
	} p;

	switch (cmd) {
	case FBIO_WAITFORVSYNC:
		s3cfb_wait_for_vsync();
		break;

	case S3CFB_WIN_ON:
		s3cfb_enable_window(win->id);
		break;

	case S3CFB_WIN_OFF:
		s3cfb_disable_window(win->id);
		break;

	case S3CFB_WIN_OFF_ALL:
		for (i = 0; i < pdata->nr_wins; i++) {
			win_temp = ctrl->fb[i]->par;
			s3cfb_disable_window(win_temp->id);
		}
		break;

	case S3CFB_WIN_POSITION:
		if (copy_from_user(&p.user_window,
			(struct s3cfb_user_window __user *) arg,
			sizeof(p.user_window)))
			ret = -EFAULT;
		else {
			if (p.user_window.x < 0)
				p.user_window.x = 0;

			if (p.user_window.y < 0)
				p.user_window.y = 0;

			if (p.user_window.x + var->xres > lcd->width)
				win->x = lcd->width - var->xres;
			else
				win->x = p.user_window.x;

			if (p.user_window.y + var->yres > lcd->height)
				win->y = lcd->height - var->yres;
			else
				win->y = p.user_window.y;

			s3cfb_set_window_position(ctrl, win->id);
		}
		break;

	case S3CFB_WIN_SET_PLANE_ALPHA:
		if (copy_from_user(&p.user_alpha,
			(struct s3cfb_user_plane_alpha __user *) arg,
			sizeof(p.user_alpha)))
			ret = -EFAULT;
		else {
			win->alpha.mode = PLANE_BLENDING;
			win->alpha.channel = p.user_alpha.channel;
			win->alpha.value =
				S3CFB_AVALUE(p.user_alpha.red,
					p.user_alpha.green,
					p.user_alpha.blue);

			s3cfb_set_alpha_blending(ctrl, win->id);
		}
		break;

	case S3CFB_WIN_SET_CHROMA:
		if (copy_from_user(&p.user_chroma,
			(struct s3cfb_user_chroma __user *) arg,
			sizeof(p.user_chroma)))
			ret = -EFAULT;
		else {
			win->chroma.enabled = p.user_chroma.enabled;
			win->chroma.key = S3CFB_CHROMA(p.user_chroma.red,
						p.user_chroma.green,
						p.user_chroma.blue);

			s3cfb_set_chroma_key(ctrl, win->id);
		}
		break;

	case S3CFB_SET_VSYNC_INT:
		if (get_user(p.vsync, (int __user *) arg))
			ret = -EFAULT;
		else {
			if (p.vsync)
				s3cfb_set_global_interrupt(ctrl, 1);

			s3cfb_set_vsync_interrupt(ctrl, p.vsync);
		}
		break;
	}

	return ret;
}

struct fb_ops s3cfb_ops = {
	.owner		= THIS_MODULE,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_check_var	= s3cfb_check_var,
	.fb_set_par	= s3cfb_set_par,
	.fb_blank	= s3cfb_blank,
	.fb_pan_display	= s3cfb_pan_display,
	.fb_setcolreg	= s3cfb_setcolreg,
	.fb_cursor	= s3cfb_cursor,
	.fb_ioctl	= s3cfb_ioctl,
	.fb_open	= s3cfb_open,
	.fb_release	= s3cfb_release,
};

/* new function to open fifo */
int s3cfb_open_fifo(int id, int ch, int (*do_priv)(void *), void *param)
{
	struct s3cfb_window *win = ctrl->fb[id]->par;

	dev_dbg(ctrl->dev, "[fb%d] open fifo\n", win->id);

	win->path = DATA_PATH_FIFO;
	win->local_channel = ch;

	s3cfb_set_vsync_interrupt(ctrl, 1);
	s3cfb_wait_for_vsync();
	s3cfb_set_vsync_interrupt(ctrl, 0);

	if (do_priv) {
		if (do_priv(param)) {
			dev_err(ctrl->dev, "failed to run for private fifo open\n");
			s3cfb_enable_window(id);
			return -EFAULT;
		}
	}

	s3cfb_set_window_control(ctrl, id);
	s3cfb_enable_window(id);

	return 0;
}

/* new function to close fifo */
int s3cfb_close_fifo(int id, int (*do_priv)(void *), void *param, int sleep)
{
	struct s3cfb_window *win = ctrl->fb[id]->par;

	dev_dbg(ctrl->dev, "[fb%d] close fifo\n", win->id);

	if (sleep)
		win->path = DATA_PATH_FIFO;
	else
		win->path = DATA_PATH_DMA;

	s3cfb_set_vsync_interrupt(ctrl, 1);
	s3cfb_wait_for_vsync();
	s3cfb_set_vsync_interrupt(ctrl, 0);

	s3cfb_display_off(ctrl);
	s3cfb_check_line_count(ctrl);
	s3cfb_disable_window(id);

	if (do_priv) {
		if (do_priv(param)) {
			dev_err(ctrl->dev, "failed to run for private fifo close\n");
			s3cfb_enable_window(id);
			s3cfb_display_on(ctrl);
			return -EFAULT;
		}
	}

	s3cfb_display_on(ctrl);

	return 0;
}

/* for backward compatibilities */
void s3cfb_enable_local(int id, int in_yuv, int ch)
{
	struct s3cfb_window *win = ctrl->fb[id]->par;

	win->path = DATA_PATH_FIFO;
	win->local_channel = ch;

	s3cfb_set_vsync_interrupt(ctrl, 1);
	s3cfb_wait_for_vsync();
	s3cfb_set_vsync_interrupt(ctrl, 0);

	s3cfb_set_window_control(ctrl, id);
	s3cfb_enable_window(id);
}

/* for backward compatibilities */
void s3cfb_enable_dma(int id)
{
	struct s3cfb_window *win = ctrl->fb[id]->par;

	win->path = DATA_PATH_DMA;

	s3cfb_set_vsync_interrupt(ctrl, 1);
	s3cfb_wait_for_vsync();
	s3cfb_set_vsync_interrupt(ctrl, 0);

	s3cfb_disable_window(id);
	s3cfb_display_off(ctrl);
	s3cfb_set_window_control(ctrl, id);
	s3cfb_display_on(ctrl);
}

int s3cfb_direct_ioctl(int id, unsigned int cmd, unsigned long arg)
{
	struct fb_info *fb = ctrl->fb[id];
	struct fb_var_screeninfo *var = &fb->var;
	struct s3cfb_window *win = fb->par;
	struct s3cfb_lcd *lcd = ctrl->lcd;
	struct s3cfb_user_window user_win;
	void *argp = (void *) arg;
	int ret = 0;

	switch (cmd) {
	case FBIO_ALLOC:
		win->path = (enum s3cfb_data_path_t) argp;
		break;

	case FBIOGET_FSCREENINFO:
		ret = memcpy(argp, &fb->fix, sizeof(fb->fix)) ? 0 : -EFAULT;
		break;

	case FBIOGET_VSCREENINFO:
		ret = memcpy(argp, &fb->var, sizeof(fb->var)) ? 0 : -EFAULT;
		break;

	case FBIOPUT_VSCREENINFO:
		ret = s3cfb_check_var((struct fb_var_screeninfo *) argp, fb);
		if (ret) {
			dev_err(ctrl->dev, "invalid vscreeninfo\n");
			break;
		}

		ret = memcpy(&fb->var, (struct fb_var_screeninfo *) argp,
				sizeof(fb->var)) ? 0 : -EFAULT;
		if (ret) {
			dev_err(ctrl->dev, "failed to put new vscreeninfo\n");
			break;
		}

		ret = s3cfb_set_par(fb);
		break;

	case S3CFB_WIN_POSITION:
		ret = memcpy(&user_win, (struct s3cfb_user_window __user *) arg,
				sizeof(user_win)) ? 0 : -EFAULT;
		if (ret) {
			dev_err(ctrl->dev, "failed to S3CFB_WIN_POSITION.\n");
			break;
		}

		if (user_win.x < 0)
			user_win.x = 0;

		if (user_win.y < 0)
			user_win.y = 0;

		if (user_win.x + var->xres > lcd->width)
			win->x = lcd->width - var->xres;
		else
			win->x = user_win.x;

		if (user_win.y + var->yres > lcd->height)
			win->y = lcd->height - var->yres;
		else
			win->y = user_win.y;

		s3cfb_set_window_position(ctrl, win->id);
		break;

	case S3CFB_SET_SUSPEND_FIFO:
		win->suspend_fifo = argp;
		break;

	case S3CFB_SET_RESUME_FIFO:
		win->resume_fifo = argp;
		break;

	/*
	 * for FBIO_WAITFORVSYNC
	*/
	default:
		ret = s3cfb_ioctl(fb, cmd, arg);
		break;
	}

	return ret;
}

EXPORT_SYMBOL(s3cfb_direct_ioctl);

static int s3cfb_init_fbinfo(int id)
{
	struct s3c_platform_fb *pdata = to_fb_plat(ctrl->dev);
	struct fb_info *fb = ctrl->fb[id];
	struct fb_fix_screeninfo *fix = &fb->fix;
	struct fb_var_screeninfo *var = &fb->var;
	struct s3cfb_window *win = fb->par;
	struct s3cfb_alpha *alpha = &win->alpha;
	struct s3cfb_lcd *lcd = ctrl->lcd;
	struct s3cfb_lcd_timing *timing = &lcd->timing;

	memset(win, 0, sizeof(struct s3cfb_window));
	platform_set_drvdata(to_platform_device(ctrl->dev), fb);
	strcpy(fix->id, S3CFB_NAME);

	/* fimd specific */
	win->id = id;
	win->path = DATA_PATH_DMA;
	win->dma_burst = 16;
	alpha->mode = PLANE_BLENDING;

	/* fbinfo */
	fb->fbops = &s3cfb_ops;
	fb->flags = FBINFO_FLAG_DEFAULT;
	fb->pseudo_palette = &win->pseudo_pal;
	fix->xpanstep = 0;
	fix->ypanstep = pdata->nr_buffers[id] - 1;
	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->accel = FB_ACCEL_NONE;
	fix->visual = FB_VISUAL_TRUECOLOR;
	var->xres = lcd->width;
	var->yres = lcd->height;
	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres + (var->yres * fix->ypanstep);
	var->bits_per_pixel = 32;
	var->xoffset = 0;
	var->yoffset = 0;
	var->width = 0;
	var->height = 0;
	var->transp.length = 0;

	fix->line_length = var->xres_virtual * var->bits_per_pixel / 8;
	fix->smem_len = fix->line_length * var->yres_virtual;

	var->nonstd = 0;
	var->activate = FB_ACTIVATE_NOW;
	var->vmode = FB_VMODE_NONINTERLACED;
	var->hsync_len = timing->h_sw;
	var->vsync_len = timing->v_sw;
	var->left_margin = timing->h_bp;
	var->right_margin = timing->h_fp;
	var->upper_margin = timing->v_bp;
	var->lower_margin = timing->v_fp;

	var->pixclock = lcd->freq * (var->left_margin + var->right_margin +
			var->hsync_len + var->xres) *
			(var->upper_margin + var->lower_margin +
			var->vsync_len + var->yres);

	dev_dbg(ctrl->dev, "pixclock: %d\n", var->pixclock);

	s3cfb_set_bitfield(var);
	s3cfb_set_alpha_info(var, win);

	return 0;
}

static int s3cfb_alloc_framebuffer(void)
{
	struct s3c_platform_fb *pdata = to_fb_plat(ctrl->dev);
	int ret, i;

	/* alloc for framebuffers */
	ctrl->fb = (struct fb_info **) kmalloc(pdata->nr_wins *
			sizeof(struct fb_info *), GFP_KERNEL);
	if (!ctrl->fb) {
		dev_err(ctrl->dev, "not enough memory\n");
		ret = -ENOMEM;
		goto err_alloc;
	}

	/* alloc for each framebuffer */
	for (i = 0; i < pdata->nr_wins; i++) {
		ctrl->fb[i] = framebuffer_alloc(sizeof(struct s3cfb_window),
				ctrl->dev);
		if (!ctrl->fb[i]) {
			dev_err(ctrl->dev, "not enough memory\n");
			ret = -ENOMEM;
			goto err_alloc_fb;
		}

		ret = s3cfb_init_fbinfo(i);
		if (ret) {
			dev_err(ctrl->dev, "failed to allocate memory for fb%d\n", i);
			ret = -ENOMEM;
			goto err_alloc_fb;
		}

		if (i == pdata->default_win) {
			if (s3cfb_map_video_memory(ctrl->fb[i])) {
				dev_err(ctrl->dev, "failed to map video memory "
					"for default window (%d)\n", i);
				ret = -ENOMEM;
				goto err_alloc_fb;
			}
		}
	}

	return 0;

err_alloc_fb:
	for (i = 0; i < pdata->nr_wins; i++) {
		if (ctrl->fb[i])
			framebuffer_release(ctrl->fb[i]);
	}

	kfree(ctrl->fb);

err_alloc:
	return ret;
}

int s3cfb_register_framebuffer(void)
{
	struct s3c_platform_fb *pdata = to_fb_plat(ctrl->dev);
	int ret, i;

	for (i = 0; i < pdata->nr_wins; i++) {
		ret = register_framebuffer(ctrl->fb[i]);
		if (ret) {
			dev_err(ctrl->dev, "failed to register framebuffer device\n");
			return -EINVAL;
		}

#ifndef CONFIG_FRAMEBUFFER_CONSOLE
		if (i == pdata->default_win) {
			s3cfb_check_var(&ctrl->fb[i]->var, ctrl->fb[i]);
			s3cfb_set_par(ctrl->fb[i]);
			s3cfb_draw_logo(ctrl->fb[i]);
		}
#endif
	}

	return 0;
}

static int s3cfb_sysfs_show_win_power(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct s3c_platform_fb *pdata = to_fb_plat(dev);
	struct s3cfb_window *win;
	char temp[16];
	int i;

	for (i = 0; i < pdata->nr_wins; i++) {
		win = ctrl->fb[i]->par;
		sprintf(temp, "[fb%d] %s\n", i, win->enabled ? "on" : "off");
		strcat(buf, temp);
	}

	return strlen(buf);
}

static int s3cfb_sysfs_store_win_power(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct s3c_platform_fb *pdata = to_fb_plat(dev);
	char temp[4] = {0, };
	const char *p = buf;
	int id, to;

	while (*p != '\0') {
		if (!isspace(*p))
			strncat(temp, p, 1);
		p++;
	}

	if (strlen(temp) != 2)
		return -EINVAL;

	id = simple_strtoul(temp, NULL, 10) / 10;
	to = simple_strtoul(temp, NULL, 10) % 10;

	if (id < 0 || id > pdata->nr_wins)
		return -EINVAL;

	if (to != 0 && to != 1)
		return -EINVAL;

	if (to == 0)
		s3cfb_disable_window(id);
	else
		s3cfb_enable_window(id);

	return len;
}

static DEVICE_ATTR(win_power, 0644,
			s3cfb_sysfs_show_win_power,
			s3cfb_sysfs_store_win_power);

static int s3cfb_probe(struct platform_device *pdev)
{
	struct s3c_platform_fb *pdata;
	struct resource *res;
	int ret = 0;

	/* initialzing global structure */
	ctrl = kzalloc(sizeof(struct s3cfb_global), GFP_KERNEL);
	if (!ctrl) {
		dev_err(ctrl->dev, "failed to allocate for global fb structure\n");
		goto err_global;
	}

	ctrl->dev = &pdev->dev;
	s3cfb_set_lcd_info(ctrl);

	/* gpio */
	pdata = to_fb_plat(&pdev->dev);
	if (pdata->cfg_gpio)
		pdata->cfg_gpio(pdev);

	/* clock */
	ctrl->clock = clk_get(&pdev->dev, pdata->clk_name);
	if (IS_ERR(ctrl->clock)) {
		dev_err(ctrl->dev, "failed to get fimd clock source\n");
		ret = -EINVAL;
		goto err_clk;
	}

	clk_enable(ctrl->clock);

	/* io memory */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(ctrl->dev, "failed to get io memory region\n");
		ret = -EINVAL;
		goto err_io;
	}

	/* request mem region */
	res = request_mem_region(res->start,
		res->end - res->start + 1, pdev->name);
	if (!res) {
		dev_err(ctrl->dev, "failed to request io memory region\n");
		ret = -EINVAL;
		goto err_io;
	}

	/* ioremap for register block */
	ctrl->regs = ioremap(res->start, res->end - res->start + 1);
	if (!ctrl->regs) {
		dev_err(ctrl->dev, "failed to remap io region\n");
		ret = -EINVAL;
		goto err_io;
	}

	/* irq */
	ctrl->irq = platform_get_irq(pdev, 0);
	if (request_irq(ctrl->irq, s3cfb_irq_frame, IRQF_DISABLED,
				pdev->name, ctrl)) {
		dev_err(ctrl->dev, "request_irq failed\n");
		ret = -EINVAL;
		goto err_irq;
	}

#ifdef CONFIG_FB_S3C_V2_TRACE_UNDERRUN
	if (request_irq(platform_get_irq(pdev, 1), s3cfb_irq_fifo,
			IRQF_DISABLED, pdev->name, ctrl)) {
		dev_err(ctrl->dev, "request_irq failed\n");
		ret = -EINVAL;
		goto err_irq;
	}

	s3cfb_set_fifo_interrupt(ctrl, 1);
	dev_info(ctrl->dev, "fifo underrun trace\n");
#endif

	/* init global */
	s3cfb_init_global();
	s3cfb_display_on(ctrl);

	/* panel control */
	if (pdata->backlight_on)
		pdata->backlight_on(pdev);

	if (pdata->lcd_on)
		pdata->lcd_on(pdev);

	if (ctrl->lcd->init_ldi)
		ctrl->lcd->init_ldi();

	/* prepare memory */
	if (s3cfb_alloc_framebuffer())
		goto err_alloc;

	if (s3cfb_register_framebuffer())
		goto err_alloc;

	s3cfb_set_clock(ctrl);
	s3cfb_enable_window(pdata->default_win);

	ret = device_create_file(&(pdev->dev), &dev_attr_win_power);
	if (ret < 0)
		dev_err(ctrl->dev, "failed to add sysfs entries\n");

	dev_info(ctrl->dev, "registered successfully\n");

	return 0;

err_alloc:
	free_irq(ctrl->irq, ctrl);

err_irq:
	iounmap(ctrl->regs);

err_io:
	clk_disable(ctrl->clock);

err_clk:
	clk_put(ctrl->clock);

err_global:
	return ret;
}

static int s3cfb_remove(struct platform_device *pdev)
{
	struct s3c_platform_fb *pdata = to_fb_plat(&pdev->dev);
	struct fb_info *fb;
	int i;

	free_irq(ctrl->irq, ctrl);
	iounmap(ctrl->regs);
	clk_disable(ctrl->clock);
	clk_put(ctrl->clock);

	for (i = 0; i < pdata->nr_wins; i++) {
		fb = ctrl->fb[i];

		/* free if exists */
		if (fb) {
			s3cfb_unmap_video_memory(fb);
			s3cfb_set_buffer_address(ctrl, i);
			framebuffer_release(fb);
		}
	}

	kfree(ctrl->fb);
	kfree(ctrl);

	return 0;
}

#ifdef CONFIG_PM
int s3cfb_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct s3c_platform_fb *pdata = to_fb_plat(&pdev->dev);
	struct s3cfb_window *win;
	int i;

	for (i = 0; i < pdata->nr_wins; i++) {
		win = ctrl->fb[i]->par;
		if (win->path == DATA_PATH_FIFO && win->suspend_fifo) {
			if (win->suspend_fifo())
				dev_info(ctrl->dev, "failed to run the suspend for fifo\n");
		}
	}

	s3cfb_display_off(ctrl);
	clk_disable(ctrl->clock);

	return 0;
}

int s3cfb_resume(struct platform_device *pdev)
{
	struct s3c_platform_fb *pdata = to_fb_plat(&pdev->dev);
	struct fb_info *fb;
	struct s3cfb_window *win;
	int i;

	dev_dbg(ctrl->dev, "wake up from suspend\n");

	if (pdata->cfg_gpio)
		pdata->cfg_gpio(pdev);

	if (pdata->backlight_on)
		pdata->backlight_on(pdev);

	if (pdata->lcd_on)
		pdata->lcd_on(pdev);

	if (ctrl->lcd->init_ldi)
		ctrl->lcd->init_ldi();

	clk_enable(ctrl->clock);
	s3cfb_init_global();
	s3cfb_set_clock(ctrl);
	s3cfb_display_on(ctrl);

	for (i = 0; i < pdata->nr_wins; i++) {
		fb = ctrl->fb[i];
		win = fb->par;

		if (win->path == DATA_PATH_FIFO && win->resume_fifo) {
			if (win->resume_fifo())
				dev_info(ctrl->dev, "failed to run the resume for fifo\n");
		} else {
			if (win->enabled) {
				s3cfb_check_var(&fb->var, fb);
				s3cfb_set_par(fb);
				s3cfb_enable_window(win->id);
			}
		}
	}

	return 0;
}
#else
#define s3cfb_suspend NULL
#define s3cfb_resume NULL
#endif

static struct platform_driver s3cfb_driver = {
	.probe		= s3cfb_probe,
	.remove		= s3cfb_remove,
	.suspend	= s3cfb_suspend,
	.resume		= s3cfb_resume,
	.driver		= {
		.name	= S3CFB_NAME,
		.owner	= THIS_MODULE,
	},
};

static int s3cfb_register(void)
{
	platform_driver_register(&s3cfb_driver);

	return 0;
}

static void s3cfb_unregister(void)
{
	platform_driver_unregister(&s3cfb_driver);
}

module_init(s3cfb_register);
module_exit(s3cfb_unregister);

MODULE_AUTHOR("Jinsung, Yang <jsgood.yang@samsung.com>");
MODULE_DESCRIPTION("Samsung Display Controller (FIMD) driver");
MODULE_LICENSE("GPL");

