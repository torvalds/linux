/*
 * Framebuffer driver for TI OMAP boards
 *
 * Copyright (C) 2004 Nokia Corporation
 * Author: Imre Deak <imre.deak@nokia.com>
 *
 * Acknowledgements:
 *   Alex McMains <aam@ridgerun.com>       - Original driver
 *   Juha Yrjola <juha.yrjola@nokia.com>   - Original driver and improvements
 *   Dirk Behme <dirk.behme@de.bosch.com>  - changes for 2.6 kernel API
 *   Texas Instruments                     - H3 support
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/uaccess.h>

#include <mach/dma.h>
#include <mach/omapfb.h>

#include "lcdc.h"
#include "dispc.h"

#define MODULE_NAME	"omapfb"

static unsigned int	def_accel;
static unsigned long	def_vram[OMAPFB_PLANE_NUM];
static unsigned int	def_vram_cnt;
static unsigned long	def_vxres;
static unsigned long	def_vyres;
static unsigned int	def_rotate;
static unsigned int	def_mirror;

#ifdef CONFIG_FB_OMAP_MANUAL_UPDATE
static int		manual_update = 1;
#else
static int		manual_update;
#endif

static struct platform_device	*fbdev_pdev;
static struct lcd_panel		*fbdev_panel;
static struct omapfb_device	*omapfb_dev;

struct caps_table_struct {
	unsigned long flag;
	const char *name;
};

static struct caps_table_struct ctrl_caps[] = {
	{ OMAPFB_CAPS_MANUAL_UPDATE,  "manual update" },
	{ OMAPFB_CAPS_TEARSYNC,       "tearing synchronization" },
	{ OMAPFB_CAPS_PLANE_RELOCATE_MEM, "relocate plane memory" },
	{ OMAPFB_CAPS_PLANE_SCALE,    "scale plane" },
	{ OMAPFB_CAPS_WINDOW_PIXEL_DOUBLE, "pixel double window" },
	{ OMAPFB_CAPS_WINDOW_SCALE,   "scale window" },
	{ OMAPFB_CAPS_WINDOW_OVERLAY, "overlay window" },
	{ OMAPFB_CAPS_SET_BACKLIGHT,  "backlight setting" },
};

static struct caps_table_struct color_caps[] = {
	{ 1 << OMAPFB_COLOR_RGB565,	"RGB565", },
	{ 1 << OMAPFB_COLOR_YUV422,	"YUV422", },
	{ 1 << OMAPFB_COLOR_YUV420,	"YUV420", },
	{ 1 << OMAPFB_COLOR_CLUT_8BPP,	"CLUT8", },
	{ 1 << OMAPFB_COLOR_CLUT_4BPP,	"CLUT4", },
	{ 1 << OMAPFB_COLOR_CLUT_2BPP,	"CLUT2", },
	{ 1 << OMAPFB_COLOR_CLUT_1BPP,	"CLUT1", },
	{ 1 << OMAPFB_COLOR_RGB444,	"RGB444", },
	{ 1 << OMAPFB_COLOR_YUY422,	"YUY422", },
};

/*
 * ---------------------------------------------------------------------------
 * LCD panel
 * ---------------------------------------------------------------------------
 */
extern struct lcd_ctrl hwa742_ctrl;
extern struct lcd_ctrl blizzard_ctrl;

static const struct lcd_ctrl *ctrls[] = {
#ifdef CONFIG_ARCH_OMAP1
	&omap1_int_ctrl,
#else
	&omap2_int_ctrl,
#endif

#ifdef CONFIG_FB_OMAP_LCDC_HWA742
	&hwa742_ctrl,
#endif
#ifdef CONFIG_FB_OMAP_LCDC_BLIZZARD
	&blizzard_ctrl,
#endif
};

#ifdef CONFIG_FB_OMAP_LCDC_EXTERNAL
#ifdef CONFIG_ARCH_OMAP1
extern struct lcd_ctrl_extif omap1_ext_if;
#else
extern struct lcd_ctrl_extif omap2_ext_if;
#endif
#endif

static void omapfb_rqueue_lock(struct omapfb_device *fbdev)
{
	mutex_lock(&fbdev->rqueue_mutex);
}

static void omapfb_rqueue_unlock(struct omapfb_device *fbdev)
{
	mutex_unlock(&fbdev->rqueue_mutex);
}

/*
 * ---------------------------------------------------------------------------
 * LCD controller and LCD DMA
 * ---------------------------------------------------------------------------
 */
/* Lookup table to map elem size to elem type. */
static const int dma_elem_type[] = {
	0,
	OMAP_DMA_DATA_TYPE_S8,
	OMAP_DMA_DATA_TYPE_S16,
	0,
	OMAP_DMA_DATA_TYPE_S32,
};

/*
 * Allocate resources needed for LCD controller and LCD DMA operations. Video
 * memory is allocated from system memory according to the virtual display
 * size, except if a bigger memory size is specified explicitly as a kernel
 * parameter.
 */
static int ctrl_init(struct omapfb_device *fbdev)
{
	int r;
	int i;

	/* kernel/module vram parameters override boot tags/board config */
	if (def_vram_cnt) {
		for (i = 0; i < def_vram_cnt; i++)
			fbdev->mem_desc.region[i].size =
				PAGE_ALIGN(def_vram[i]);
		fbdev->mem_desc.region_cnt = i;
	} else {
		struct omapfb_platform_data *conf;

		conf = fbdev->dev->platform_data;
		fbdev->mem_desc = conf->mem_desc;
	}

	if (!fbdev->mem_desc.region_cnt) {
		struct lcd_panel *panel = fbdev->panel;
		int def_size;
		int bpp = panel->bpp;

		/* 12 bpp is packed in 16 bits */
		if (bpp == 12)
			bpp = 16;
		def_size = def_vxres * def_vyres * bpp / 8;
		fbdev->mem_desc.region_cnt = 1;
		fbdev->mem_desc.region[0].size = PAGE_ALIGN(def_size);
	}
	r = fbdev->ctrl->init(fbdev, 0, &fbdev->mem_desc);
	if (r < 0) {
		dev_err(fbdev->dev, "controller initialization failed (%d)\n",
			r);
		return r;
	}

#ifdef DEBUG
	for (i = 0; i < fbdev->mem_desc.region_cnt; i++) {
		dev_dbg(fbdev->dev, "region%d phys %08x virt %p size=%lu\n",
			 i,
			 fbdev->mem_desc.region[i].paddr,
			 fbdev->mem_desc.region[i].vaddr,
			 fbdev->mem_desc.region[i].size);
	}
#endif
	return 0;
}

static void ctrl_cleanup(struct omapfb_device *fbdev)
{
	fbdev->ctrl->cleanup();
}

/* Must be called with fbdev->rqueue_mutex held. */
static int ctrl_change_mode(struct fb_info *fbi)
{
	int r;
	unsigned long offset;
	struct omapfb_plane_struct *plane = fbi->par;
	struct omapfb_device *fbdev = plane->fbdev;
	struct fb_var_screeninfo *var = &fbi->var;

	offset = var->yoffset * fbi->fix.line_length +
		 var->xoffset * var->bits_per_pixel / 8;

	if (fbdev->ctrl->sync)
		fbdev->ctrl->sync();
	r = fbdev->ctrl->setup_plane(plane->idx, plane->info.channel_out,
				 offset, var->xres_virtual,
				 plane->info.pos_x, plane->info.pos_y,
				 var->xres, var->yres, plane->color_mode);
	if (fbdev->ctrl->set_scale != NULL)
		r = fbdev->ctrl->set_scale(plane->idx,
				   var->xres, var->yres,
				   plane->info.out_width,
				   plane->info.out_height);

	return r;
}

/*
 * ---------------------------------------------------------------------------
 * fbdev framework callbacks and the ioctl interface
 * ---------------------------------------------------------------------------
 */
/* Called each time the omapfb device is opened */
static int omapfb_open(struct fb_info *info, int user)
{
	return 0;
}

static void omapfb_sync(struct fb_info *info);

/* Called when the omapfb device is closed. We make sure that any pending
 * gfx DMA operations are ended, before we return. */
static int omapfb_release(struct fb_info *info, int user)
{
	omapfb_sync(info);
	return 0;
}

/* Store a single color palette entry into a pseudo palette or the hardware
 * palette if one is available. For now we support only 16bpp and thus store
 * the entry only to the pseudo palette.
 */
static int _setcolreg(struct fb_info *info, u_int regno, u_int red, u_int green,
			u_int blue, u_int transp, int update_hw_pal)
{
	struct omapfb_plane_struct *plane = info->par;
	struct omapfb_device *fbdev = plane->fbdev;
	struct fb_var_screeninfo *var = &info->var;
	int r = 0;

	switch (plane->color_mode) {
	case OMAPFB_COLOR_YUV422:
	case OMAPFB_COLOR_YUV420:
	case OMAPFB_COLOR_YUY422:
		r = -EINVAL;
		break;
	case OMAPFB_COLOR_CLUT_8BPP:
	case OMAPFB_COLOR_CLUT_4BPP:
	case OMAPFB_COLOR_CLUT_2BPP:
	case OMAPFB_COLOR_CLUT_1BPP:
		if (fbdev->ctrl->setcolreg)
			r = fbdev->ctrl->setcolreg(regno, red, green, blue,
							transp, update_hw_pal);
		/* Fallthrough */
	case OMAPFB_COLOR_RGB565:
	case OMAPFB_COLOR_RGB444:
		if (r != 0)
			break;

		if (regno < 0) {
			r = -EINVAL;
			break;
		}

		if (regno < 16) {
			u16 pal;
			pal = ((red >> (16 - var->red.length)) <<
					var->red.offset) |
			      ((green >> (16 - var->green.length)) <<
					var->green.offset) |
			      (blue >> (16 - var->blue.length));
			((u32 *)(info->pseudo_palette))[regno] = pal;
		}
		break;
	default:
		BUG();
	}
	return r;
}

static int omapfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			    u_int transp, struct fb_info *info)
{
	return _setcolreg(info, regno, red, green, blue, transp, 1);
}

static int omapfb_setcmap(struct fb_cmap *cmap, struct fb_info *info)
{
	int count, index, r;
	u16 *red, *green, *blue, *transp;
	u16 trans = 0xffff;

	red     = cmap->red;
	green   = cmap->green;
	blue    = cmap->blue;
	transp  = cmap->transp;
	index   = cmap->start;

	for (count = 0; count < cmap->len; count++) {
		if (transp)
			trans = *transp++;
		r = _setcolreg(info, index++, *red++, *green++, *blue++, trans,
				count == cmap->len - 1);
		if (r != 0)
			return r;
	}

	return 0;
}

static int omapfb_update_full_screen(struct fb_info *fbi);

static int omapfb_blank(int blank, struct fb_info *fbi)
{
	struct omapfb_plane_struct *plane = fbi->par;
	struct omapfb_device *fbdev = plane->fbdev;
	int do_update = 0;
	int r = 0;

	omapfb_rqueue_lock(fbdev);
	switch (blank) {
	case VESA_NO_BLANKING:
		if (fbdev->state == OMAPFB_SUSPENDED) {
			if (fbdev->ctrl->resume)
				fbdev->ctrl->resume();
			fbdev->panel->enable(fbdev->panel);
			fbdev->state = OMAPFB_ACTIVE;
			if (fbdev->ctrl->get_update_mode() ==
					OMAPFB_MANUAL_UPDATE)
				do_update = 1;
		}
		break;
	case VESA_POWERDOWN:
		if (fbdev->state == OMAPFB_ACTIVE) {
			fbdev->panel->disable(fbdev->panel);
			if (fbdev->ctrl->suspend)
				fbdev->ctrl->suspend();
			fbdev->state = OMAPFB_SUSPENDED;
		}
		break;
	default:
		r = -EINVAL;
	}
	omapfb_rqueue_unlock(fbdev);

	if (r == 0 && do_update)
		r = omapfb_update_full_screen(fbi);

	return r;
}

static void omapfb_sync(struct fb_info *fbi)
{
	struct omapfb_plane_struct *plane = fbi->par;
	struct omapfb_device *fbdev = plane->fbdev;

	omapfb_rqueue_lock(fbdev);
	if (fbdev->ctrl->sync)
		fbdev->ctrl->sync();
	omapfb_rqueue_unlock(fbdev);
}

/*
 * Set fb_info.fix fields and also updates fbdev.
 * When calling this fb_info.var must be set up already.
 */
static void set_fb_fix(struct fb_info *fbi)
{
	struct fb_fix_screeninfo *fix = &fbi->fix;
	struct fb_var_screeninfo *var = &fbi->var;
	struct omapfb_plane_struct *plane = fbi->par;
	struct omapfb_mem_region *rg;
	int bpp;

	rg = &plane->fbdev->mem_desc.region[plane->idx];
	fbi->screen_base	= rg->vaddr;
	fix->smem_start		= rg->paddr;
	fix->smem_len		= rg->size;

	fix->type = FB_TYPE_PACKED_PIXELS;
	bpp = var->bits_per_pixel;
	if (var->nonstd)
		fix->visual = FB_VISUAL_PSEUDOCOLOR;
	else switch (var->bits_per_pixel) {
	case 16:
	case 12:
		fix->visual = FB_VISUAL_TRUECOLOR;
		/* 12bpp is stored in 16 bits */
		bpp = 16;
		break;
	case 1:
	case 2:
	case 4:
	case 8:
		fix->visual = FB_VISUAL_PSEUDOCOLOR;
		break;
	}
	fix->accel		= FB_ACCEL_OMAP1610;
	fix->line_length	= var->xres_virtual * bpp / 8;
}

static int set_color_mode(struct omapfb_plane_struct *plane,
			  struct fb_var_screeninfo *var)
{
	switch (var->nonstd) {
	case 0:
		break;
	case OMAPFB_COLOR_YUV422:
		var->bits_per_pixel = 16;
		plane->color_mode = var->nonstd;
		return 0;
	case OMAPFB_COLOR_YUV420:
		var->bits_per_pixel = 12;
		plane->color_mode = var->nonstd;
		return 0;
	case OMAPFB_COLOR_YUY422:
		var->bits_per_pixel = 16;
		plane->color_mode = var->nonstd;
		return 0;
	default:
		return -EINVAL;
	}

	switch (var->bits_per_pixel) {
	case 1:
		plane->color_mode = OMAPFB_COLOR_CLUT_1BPP;
		return 0;
	case 2:
		plane->color_mode = OMAPFB_COLOR_CLUT_2BPP;
		return 0;
	case 4:
		plane->color_mode = OMAPFB_COLOR_CLUT_4BPP;
		return 0;
	case 8:
		plane->color_mode = OMAPFB_COLOR_CLUT_8BPP;
		return 0;
	case 12:
		var->bits_per_pixel = 16;
		plane->color_mode = OMAPFB_COLOR_RGB444;
		return 0;
	case 16:
		plane->color_mode = OMAPFB_COLOR_RGB565;
		return 0;
	default:
		return -EINVAL;
	}
}

/*
 * Check the values in var against our capabilities and in case of out of
 * bound values try to adjust them.
 */
static int set_fb_var(struct fb_info *fbi,
		      struct fb_var_screeninfo *var)
{
	int		bpp;
	unsigned long	max_frame_size;
	unsigned long	line_size;
	int		xres_min, xres_max;
	int		yres_min, yres_max;
	struct omapfb_plane_struct *plane = fbi->par;
	struct omapfb_device *fbdev = plane->fbdev;
	struct lcd_panel *panel = fbdev->panel;

	if (set_color_mode(plane, var) < 0)
		return -EINVAL;

	bpp = var->bits_per_pixel;
	if (plane->color_mode == OMAPFB_COLOR_RGB444)
		bpp = 16;

	switch (var->rotate) {
	case 0:
	case 180:
		xres_min = OMAPFB_PLANE_XRES_MIN;
		xres_max = panel->x_res;
		yres_min = OMAPFB_PLANE_YRES_MIN;
		yres_max = panel->y_res;
		if (cpu_is_omap15xx()) {
			var->xres = panel->x_res;
			var->yres = panel->y_res;
		}
		break;
	case 90:
	case 270:
		xres_min = OMAPFB_PLANE_YRES_MIN;
		xres_max = panel->y_res;
		yres_min = OMAPFB_PLANE_XRES_MIN;
		yres_max = panel->x_res;
		if (cpu_is_omap15xx()) {
			var->xres = panel->y_res;
			var->yres = panel->x_res;
		}
		break;
	default:
		return -EINVAL;
	}

	if (var->xres < xres_min)
		var->xres = xres_min;
	if (var->yres < yres_min)
		var->yres = yres_min;
	if (var->xres > xres_max)
		var->xres = xres_max;
	if (var->yres > yres_max)
		var->yres = yres_max;

	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;
	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;
	max_frame_size = fbdev->mem_desc.region[plane->idx].size;
	line_size = var->xres_virtual * bpp / 8;
	if (line_size * var->yres_virtual > max_frame_size) {
		/* Try to keep yres_virtual first */
		line_size = max_frame_size / var->yres_virtual;
		var->xres_virtual = line_size * 8 / bpp;
		if (var->xres_virtual < var->xres) {
			/* Still doesn't fit. Shrink yres_virtual too */
			var->xres_virtual = var->xres;
			line_size = var->xres * bpp / 8;
			var->yres_virtual = max_frame_size / line_size;
		}
		/* Recheck this, as the virtual size changed. */
		if (var->xres_virtual < var->xres)
			var->xres = var->xres_virtual;
		if (var->yres_virtual < var->yres)
			var->yres = var->yres_virtual;
		if (var->xres < xres_min || var->yres < yres_min)
			return -EINVAL;
	}
	if (var->xres + var->xoffset > var->xres_virtual)
		var->xoffset = var->xres_virtual - var->xres;
	if (var->yres + var->yoffset > var->yres_virtual)
		var->yoffset = var->yres_virtual - var->yres;
	line_size = var->xres * bpp / 8;

	if (plane->color_mode == OMAPFB_COLOR_RGB444) {
		var->red.offset	  = 8; var->red.length	 = 4;
						var->red.msb_right   = 0;
		var->green.offset = 4; var->green.length = 4;
						var->green.msb_right = 0;
		var->blue.offset  = 0; var->blue.length  = 4;
						var->blue.msb_right  = 0;
	} else {
		var->red.offset	 = 11; var->red.length	 = 5;
						var->red.msb_right   = 0;
		var->green.offset = 5;  var->green.length = 6;
						var->green.msb_right = 0;
		var->blue.offset = 0;  var->blue.length  = 5;
						var->blue.msb_right  = 0;
	}

	var->height		= -1;
	var->width		= -1;
	var->grayscale		= 0;

	/* pixclock in ps, the rest in pixclock */
	var->pixclock		= 10000000 / (panel->pixel_clock / 100);
	var->left_margin	= panel->hfp;
	var->right_margin	= panel->hbp;
	var->upper_margin	= panel->vfp;
	var->lower_margin	= panel->vbp;
	var->hsync_len		= panel->hsw;
	var->vsync_len		= panel->vsw;

	/* TODO: get these from panel->config */
	var->vmode		= FB_VMODE_NONINTERLACED;
	var->sync		= 0;

	return 0;
}


/* Set rotation (0, 90, 180, 270 degree), and switch to the new mode. */
static void omapfb_rotate(struct fb_info *fbi, int rotate)
{
	struct omapfb_plane_struct *plane = fbi->par;
	struct omapfb_device *fbdev = plane->fbdev;

	omapfb_rqueue_lock(fbdev);
	if (cpu_is_omap15xx() && rotate != fbi->var.rotate) {
		struct fb_var_screeninfo *new_var = &fbdev->new_var;

		memcpy(new_var, &fbi->var, sizeof(*new_var));
		new_var->rotate = rotate;
		if (set_fb_var(fbi, new_var) == 0 &&
		    memcmp(new_var, &fbi->var, sizeof(*new_var))) {
			memcpy(&fbi->var, new_var, sizeof(*new_var));
			ctrl_change_mode(fbi);
		}
	}
	omapfb_rqueue_unlock(fbdev);
}

/*
 * Set new x,y offsets in the virtual display for the visible area and switch
 * to the new mode.
 */
static int omapfb_pan_display(struct fb_var_screeninfo *var,
			       struct fb_info *fbi)
{
	struct omapfb_plane_struct *plane = fbi->par;
	struct omapfb_device *fbdev = plane->fbdev;
	int r = 0;

	omapfb_rqueue_lock(fbdev);
	if (var->xoffset != fbi->var.xoffset ||
	    var->yoffset != fbi->var.yoffset) {
		struct fb_var_screeninfo *new_var = &fbdev->new_var;

		memcpy(new_var, &fbi->var, sizeof(*new_var));
		new_var->xoffset = var->xoffset;
		new_var->yoffset = var->yoffset;
		if (set_fb_var(fbi, new_var))
			r = -EINVAL;
		else {
			memcpy(&fbi->var, new_var, sizeof(*new_var));
			ctrl_change_mode(fbi);
		}
	}
	omapfb_rqueue_unlock(fbdev);

	return r;
}

/* Set mirror to vertical axis and switch to the new mode. */
static int omapfb_mirror(struct fb_info *fbi, int mirror)
{
	struct omapfb_plane_struct *plane = fbi->par;
	struct omapfb_device *fbdev = plane->fbdev;
	int r = 0;

	omapfb_rqueue_lock(fbdev);
	mirror = mirror ? 1 : 0;
	if (cpu_is_omap15xx())
		r = -EINVAL;
	else if (mirror != plane->info.mirror) {
		plane->info.mirror = mirror;
		r = ctrl_change_mode(fbi);
	}
	omapfb_rqueue_unlock(fbdev);

	return r;
}

/*
 * Check values in var, try to adjust them in case of out of bound values if
 * possible, or return error.
 */
static int omapfb_check_var(struct fb_var_screeninfo *var, struct fb_info *fbi)
{
	struct omapfb_plane_struct *plane = fbi->par;
	struct omapfb_device *fbdev = plane->fbdev;
	int r;

	omapfb_rqueue_lock(fbdev);
	if (fbdev->ctrl->sync != NULL)
		fbdev->ctrl->sync();
	r = set_fb_var(fbi, var);
	omapfb_rqueue_unlock(fbdev);

	return r;
}

/*
 * Switch to a new mode. The parameters for it has been check already by
 * omapfb_check_var.
 */
static int omapfb_set_par(struct fb_info *fbi)
{
	struct omapfb_plane_struct *plane = fbi->par;
	struct omapfb_device *fbdev = plane->fbdev;
	int r = 0;

	omapfb_rqueue_lock(fbdev);
	set_fb_fix(fbi);
	r = ctrl_change_mode(fbi);
	omapfb_rqueue_unlock(fbdev);

	return r;
}

int omapfb_update_window_async(struct fb_info *fbi,
				struct omapfb_update_window *win,
				void (*callback)(void *),
				void *callback_data)
{
	struct omapfb_plane_struct *plane = fbi->par;
	struct omapfb_device *fbdev = plane->fbdev;
	struct fb_var_screeninfo *var;

	var = &fbi->var;
	if (win->x >= var->xres || win->y >= var->yres ||
	    win->out_x > var->xres || win->out_y >= var->yres)
		return -EINVAL;

	if (!fbdev->ctrl->update_window ||
	    fbdev->ctrl->get_update_mode() != OMAPFB_MANUAL_UPDATE)
		return -ENODEV;

	if (win->x + win->width >= var->xres)
		win->width = var->xres - win->x;
	if (win->y + win->height >= var->yres)
		win->height = var->yres - win->y;
	/* The out sizes should be cropped to the LCD size */
	if (win->out_x + win->out_width > fbdev->panel->x_res)
		win->out_width = fbdev->panel->x_res - win->out_x;
	if (win->out_y + win->out_height > fbdev->panel->y_res)
		win->out_height = fbdev->panel->y_res - win->out_y;
	if (!win->width || !win->height || !win->out_width || !win->out_height)
		return 0;

	return fbdev->ctrl->update_window(fbi, win, callback, callback_data);
}
EXPORT_SYMBOL(omapfb_update_window_async);

static int omapfb_update_win(struct fb_info *fbi,
				struct omapfb_update_window *win)
{
	struct omapfb_plane_struct *plane = fbi->par;
	int ret;

	omapfb_rqueue_lock(plane->fbdev);
	ret = omapfb_update_window_async(fbi, win, NULL, NULL);
	omapfb_rqueue_unlock(plane->fbdev);

	return ret;
}

static int omapfb_update_full_screen(struct fb_info *fbi)
{
	struct omapfb_plane_struct *plane = fbi->par;
	struct omapfb_device *fbdev = plane->fbdev;
	struct omapfb_update_window win;
	int r;

	if (!fbdev->ctrl->update_window ||
	    fbdev->ctrl->get_update_mode() != OMAPFB_MANUAL_UPDATE)
		return -ENODEV;

	win.x = 0;
	win.y = 0;
	win.width = fbi->var.xres;
	win.height = fbi->var.yres;
	win.out_x = 0;
	win.out_y = 0;
	win.out_width = fbi->var.xres;
	win.out_height = fbi->var.yres;
	win.format = 0;

	omapfb_rqueue_lock(fbdev);
	r = fbdev->ctrl->update_window(fbi, &win, NULL, NULL);
	omapfb_rqueue_unlock(fbdev);

	return r;
}

static int omapfb_setup_plane(struct fb_info *fbi, struct omapfb_plane_info *pi)
{
	struct omapfb_plane_struct *plane = fbi->par;
	struct omapfb_device *fbdev = plane->fbdev;
	struct lcd_panel *panel = fbdev->panel;
	struct omapfb_plane_info old_info;
	int r = 0;

	if (pi->pos_x + pi->out_width > panel->x_res ||
	    pi->pos_y + pi->out_height > panel->y_res)
		return -EINVAL;

	omapfb_rqueue_lock(fbdev);
	if (pi->enabled && !fbdev->mem_desc.region[plane->idx].size) {
		/*
		 * This plane's memory was freed, can't enable it
		 * until it's reallocated.
		 */
		r = -EINVAL;
		goto out;
	}
	old_info = plane->info;
	plane->info = *pi;
	if (pi->enabled) {
		r = ctrl_change_mode(fbi);
		if (r < 0) {
			plane->info = old_info;
			goto out;
		}
	}
	r = fbdev->ctrl->enable_plane(plane->idx, pi->enabled);
	if (r < 0) {
		plane->info = old_info;
		goto out;
	}
out:
	omapfb_rqueue_unlock(fbdev);
	return r;
}

static int omapfb_query_plane(struct fb_info *fbi, struct omapfb_plane_info *pi)
{
	struct omapfb_plane_struct *plane = fbi->par;

	*pi = plane->info;
	return 0;
}

static int omapfb_setup_mem(struct fb_info *fbi, struct omapfb_mem_info *mi)
{
	struct omapfb_plane_struct *plane = fbi->par;
	struct omapfb_device *fbdev = plane->fbdev;
	struct omapfb_mem_region *rg = &fbdev->mem_desc.region[plane->idx];
	size_t size;
	int r = 0;

	if (fbdev->ctrl->setup_mem == NULL)
		return -ENODEV;
	if (mi->type > OMAPFB_MEMTYPE_MAX)
		return -EINVAL;

	size = PAGE_ALIGN(mi->size);
	omapfb_rqueue_lock(fbdev);
	if (plane->info.enabled) {
		r = -EBUSY;
		goto out;
	}
	if (rg->size != size || rg->type != mi->type) {
		struct fb_var_screeninfo *new_var = &fbdev->new_var;
		unsigned long old_size = rg->size;
		u8	      old_type = rg->type;
		unsigned long paddr;

		rg->size = size;
		rg->type = mi->type;
		/*
		 * size == 0 is a special case, for which we
		 * don't check / adjust the screen parameters.
		 * This isn't a problem since the plane can't
		 * be reenabled unless its size is > 0.
		 */
		if (old_size != size && size) {
			if (size) {
				memcpy(new_var, &fbi->var, sizeof(*new_var));
				r = set_fb_var(fbi, new_var);
				if (r < 0)
					goto out;
			}
		}

		if (fbdev->ctrl->sync)
			fbdev->ctrl->sync();
		r = fbdev->ctrl->setup_mem(plane->idx, size, mi->type, &paddr);
		if (r < 0) {
			/* Revert changes. */
			rg->size = old_size;
			rg->type = old_type;
			goto out;
		}
		rg->paddr = paddr;

		if (old_size != size) {
			if (size) {
				memcpy(&fbi->var, new_var, sizeof(fbi->var));
				set_fb_fix(fbi);
			} else {
				/*
				 * Set these explicitly to indicate that the
				 * plane memory is dealloce'd, the other
				 * screen parameters in var / fix are invalid.
				 */
				fbi->fix.smem_start = 0;
				fbi->fix.smem_len = 0;
			}
		}
	}
out:
	omapfb_rqueue_unlock(fbdev);

	return r;
}

static int omapfb_query_mem(struct fb_info *fbi, struct omapfb_mem_info *mi)
{
	struct omapfb_plane_struct *plane = fbi->par;
	struct omapfb_device *fbdev = plane->fbdev;
	struct omapfb_mem_region *rg;

	rg = &fbdev->mem_desc.region[plane->idx];
	memset(mi, 0, sizeof(*mi));
	mi->size = rg->size;
	mi->type = rg->type;

	return 0;
}

static int omapfb_set_color_key(struct omapfb_device *fbdev,
				struct omapfb_color_key *ck)
{
	int r;

	if (!fbdev->ctrl->set_color_key)
		return -ENODEV;

	omapfb_rqueue_lock(fbdev);
	r = fbdev->ctrl->set_color_key(ck);
	omapfb_rqueue_unlock(fbdev);

	return r;
}

static int omapfb_get_color_key(struct omapfb_device *fbdev,
				struct omapfb_color_key *ck)
{
	int r;

	if (!fbdev->ctrl->get_color_key)
		return -ENODEV;

	omapfb_rqueue_lock(fbdev);
	r = fbdev->ctrl->get_color_key(ck);
	omapfb_rqueue_unlock(fbdev);

	return r;
}

static struct blocking_notifier_head omapfb_client_list[OMAPFB_PLANE_NUM];
static int notifier_inited;

static void omapfb_init_notifier(void)
{
	int i;

	for (i = 0; i < OMAPFB_PLANE_NUM; i++)
		BLOCKING_INIT_NOTIFIER_HEAD(&omapfb_client_list[i]);
}

int omapfb_register_client(struct omapfb_notifier_block *omapfb_nb,
				omapfb_notifier_callback_t callback,
				void *callback_data)
{
	int r;

	if ((unsigned)omapfb_nb->plane_idx > OMAPFB_PLANE_NUM)
		return -EINVAL;

	if (!notifier_inited) {
		omapfb_init_notifier();
		notifier_inited = 1;
	}

	omapfb_nb->nb.notifier_call = (int (*)(struct notifier_block *,
					unsigned long, void *))callback;
	omapfb_nb->data = callback_data;
	r = blocking_notifier_chain_register(
				&omapfb_client_list[omapfb_nb->plane_idx],
				&omapfb_nb->nb);
	if (r)
		return r;
	if (omapfb_dev != NULL &&
	    omapfb_dev->ctrl && omapfb_dev->ctrl->bind_client) {
		omapfb_dev->ctrl->bind_client(omapfb_nb);
	}

	return 0;
}
EXPORT_SYMBOL(omapfb_register_client);

int omapfb_unregister_client(struct omapfb_notifier_block *omapfb_nb)
{
	return blocking_notifier_chain_unregister(
		&omapfb_client_list[omapfb_nb->plane_idx], &omapfb_nb->nb);
}
EXPORT_SYMBOL(omapfb_unregister_client);

void omapfb_notify_clients(struct omapfb_device *fbdev, unsigned long event)
{
	int i;

	if (!notifier_inited)
		/* no client registered yet */
		return;

	for (i = 0; i < OMAPFB_PLANE_NUM; i++)
		blocking_notifier_call_chain(&omapfb_client_list[i], event,
				    fbdev->fb_info[i]);
}
EXPORT_SYMBOL(omapfb_notify_clients);

static int omapfb_set_update_mode(struct omapfb_device *fbdev,
				   enum omapfb_update_mode mode)
{
	int r;

	omapfb_rqueue_lock(fbdev);
	r = fbdev->ctrl->set_update_mode(mode);
	omapfb_rqueue_unlock(fbdev);

	return r;
}

static enum omapfb_update_mode omapfb_get_update_mode(struct omapfb_device *fbdev)
{
	int r;

	omapfb_rqueue_lock(fbdev);
	r = fbdev->ctrl->get_update_mode();
	omapfb_rqueue_unlock(fbdev);

	return r;
}

static void omapfb_get_caps(struct omapfb_device *fbdev, int plane,
				     struct omapfb_caps *caps)
{
	memset(caps, 0, sizeof(*caps));
	fbdev->ctrl->get_caps(plane, caps);
	caps->ctrl |= fbdev->panel->get_caps(fbdev->panel);
}

/* For lcd testing */
void omapfb_write_first_pixel(struct omapfb_device *fbdev, u16 pixval)
{
	omapfb_rqueue_lock(fbdev);
	*(u16 *)fbdev->mem_desc.region[0].vaddr = pixval;
	if (fbdev->ctrl->get_update_mode() == OMAPFB_MANUAL_UPDATE) {
		struct omapfb_update_window win;

		memset(&win, 0, sizeof(win));
		win.width = 2;
		win.height = 2;
		win.out_width = 2;
		win.out_height = 2;
		fbdev->ctrl->update_window(fbdev->fb_info[0], &win, NULL, NULL);
	}
	omapfb_rqueue_unlock(fbdev);
}
EXPORT_SYMBOL(omapfb_write_first_pixel);

/*
 * Ioctl interface. Part of the kernel mode frame buffer API is duplicated
 * here to be accessible by user mode code.
 */
static int omapfb_ioctl(struct fb_info *fbi, unsigned int cmd,
			unsigned long arg)
{
	struct omapfb_plane_struct *plane = fbi->par;
	struct omapfb_device	*fbdev = plane->fbdev;
	struct fb_ops		*ops = fbi->fbops;
	union {
		struct omapfb_update_window	update_window;
		struct omapfb_plane_info	plane_info;
		struct omapfb_mem_info		mem_info;
		struct omapfb_color_key		color_key;
		enum omapfb_update_mode		update_mode;
		struct omapfb_caps		caps;
		unsigned int		mirror;
		int			plane_out;
		int			enable_plane;
	} p;
	int r = 0;

	BUG_ON(!ops);
	switch (cmd) {
	case OMAPFB_MIRROR:
		if (get_user(p.mirror, (int __user *)arg))
			r = -EFAULT;
		else
			omapfb_mirror(fbi, p.mirror);
		break;
	case OMAPFB_SYNC_GFX:
		omapfb_sync(fbi);
		break;
	case OMAPFB_VSYNC:
		break;
	case OMAPFB_SET_UPDATE_MODE:
		if (get_user(p.update_mode, (int __user *)arg))
			r = -EFAULT;
		else
			r = omapfb_set_update_mode(fbdev, p.update_mode);
		break;
	case OMAPFB_GET_UPDATE_MODE:
		p.update_mode = omapfb_get_update_mode(fbdev);
		if (put_user(p.update_mode,
					(enum omapfb_update_mode __user *)arg))
			r = -EFAULT;
		break;
	case OMAPFB_UPDATE_WINDOW_OLD:
		if (copy_from_user(&p.update_window, (void __user *)arg,
				   sizeof(struct omapfb_update_window_old)))
			r = -EFAULT;
		else {
			struct omapfb_update_window *u = &p.update_window;
			u->out_x = u->x;
			u->out_y = u->y;
			u->out_width = u->width;
			u->out_height = u->height;
			memset(u->reserved, 0, sizeof(u->reserved));
			r = omapfb_update_win(fbi, u);
		}
		break;
	case OMAPFB_UPDATE_WINDOW:
		if (copy_from_user(&p.update_window, (void __user *)arg,
				   sizeof(p.update_window)))
			r = -EFAULT;
		else
			r = omapfb_update_win(fbi, &p.update_window);
		break;
	case OMAPFB_SETUP_PLANE:
		if (copy_from_user(&p.plane_info, (void __user *)arg,
				   sizeof(p.plane_info)))
			r = -EFAULT;
		else
			r = omapfb_setup_plane(fbi, &p.plane_info);
		break;
	case OMAPFB_QUERY_PLANE:
		if ((r = omapfb_query_plane(fbi, &p.plane_info)) < 0)
			break;
		if (copy_to_user((void __user *)arg, &p.plane_info,
				   sizeof(p.plane_info)))
			r = -EFAULT;
		break;
	case OMAPFB_SETUP_MEM:
		if (copy_from_user(&p.mem_info, (void __user *)arg,
				   sizeof(p.mem_info)))
			r = -EFAULT;
		else
			r = omapfb_setup_mem(fbi, &p.mem_info);
		break;
	case OMAPFB_QUERY_MEM:
		if ((r = omapfb_query_mem(fbi, &p.mem_info)) < 0)
			break;
		if (copy_to_user((void __user *)arg, &p.mem_info,
				   sizeof(p.mem_info)))
			r = -EFAULT;
		break;
	case OMAPFB_SET_COLOR_KEY:
		if (copy_from_user(&p.color_key, (void __user *)arg,
				   sizeof(p.color_key)))
			r = -EFAULT;
		else
			r = omapfb_set_color_key(fbdev, &p.color_key);
		break;
	case OMAPFB_GET_COLOR_KEY:
		if ((r = omapfb_get_color_key(fbdev, &p.color_key)) < 0)
			break;
		if (copy_to_user((void __user *)arg, &p.color_key,
				 sizeof(p.color_key)))
			r = -EFAULT;
		break;
	case OMAPFB_GET_CAPS:
		omapfb_get_caps(fbdev, plane->idx, &p.caps);
		if (copy_to_user((void __user *)arg, &p.caps, sizeof(p.caps)))
			r = -EFAULT;
		break;
	case OMAPFB_LCD_TEST:
		{
			int test_num;

			if (get_user(test_num, (int __user *)arg)) {
				r = -EFAULT;
				break;
			}
			if (!fbdev->panel->run_test) {
				r = -EINVAL;
				break;
			}
			r = fbdev->panel->run_test(fbdev->panel, test_num);
			break;
		}
	case OMAPFB_CTRL_TEST:
		{
			int test_num;

			if (get_user(test_num, (int __user *)arg)) {
				r = -EFAULT;
				break;
			}
			if (!fbdev->ctrl->run_test) {
				r = -EINVAL;
				break;
			}
			r = fbdev->ctrl->run_test(test_num);
			break;
		}
	default:
		r = -EINVAL;
	}

	return r;
}

static int omapfb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct omapfb_plane_struct *plane = info->par;
	struct omapfb_device *fbdev = plane->fbdev;
	int r;

	omapfb_rqueue_lock(fbdev);
	r = fbdev->ctrl->mmap(info, vma);
	omapfb_rqueue_unlock(fbdev);

	return r;
}

/*
 * Callback table for the frame buffer framework. Some of these pointers
 * will be changed according to the current setting of fb_info->accel_flags.
 */
static struct fb_ops omapfb_ops = {
	.owner		= THIS_MODULE,
	.fb_open        = omapfb_open,
	.fb_release     = omapfb_release,
	.fb_setcolreg	= omapfb_setcolreg,
	.fb_setcmap	= omapfb_setcmap,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_blank       = omapfb_blank,
	.fb_ioctl	= omapfb_ioctl,
	.fb_check_var	= omapfb_check_var,
	.fb_set_par	= omapfb_set_par,
	.fb_rotate	= omapfb_rotate,
	.fb_pan_display = omapfb_pan_display,
};

/*
 * ---------------------------------------------------------------------------
 * Sysfs interface
 * ---------------------------------------------------------------------------
 */
/* omapfbX sysfs entries */
static ssize_t omapfb_show_caps_num(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct omapfb_device *fbdev = (struct omapfb_device *)dev->driver_data;
	int plane;
	size_t size;
	struct omapfb_caps caps;

	plane = 0;
	size = 0;
	while (size < PAGE_SIZE && plane < OMAPFB_PLANE_NUM) {
		omapfb_get_caps(fbdev, plane, &caps);
		size += snprintf(&buf[size], PAGE_SIZE - size,
			"plane#%d %#010x %#010x %#010x\n",
			plane, caps.ctrl, caps.plane_color, caps.wnd_color);
		plane++;
	}
	return size;
}

static ssize_t omapfb_show_caps_text(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct omapfb_device *fbdev = (struct omapfb_device *)dev->driver_data;
	int i;
	struct omapfb_caps caps;
	int plane;
	size_t size;

	plane = 0;
	size = 0;
	while (size < PAGE_SIZE && plane < OMAPFB_PLANE_NUM) {
		omapfb_get_caps(fbdev, plane, &caps);
		size += snprintf(&buf[size], PAGE_SIZE - size,
				 "plane#%d:\n", plane);
		for (i = 0; i < ARRAY_SIZE(ctrl_caps) &&
		     size < PAGE_SIZE; i++) {
			if (ctrl_caps[i].flag & caps.ctrl)
				size += snprintf(&buf[size], PAGE_SIZE - size,
					" %s\n", ctrl_caps[i].name);
		}
		size += snprintf(&buf[size], PAGE_SIZE - size,
				 " plane colors:\n");
		for (i = 0; i < ARRAY_SIZE(color_caps) &&
		     size < PAGE_SIZE; i++) {
			if (color_caps[i].flag & caps.plane_color)
				size += snprintf(&buf[size], PAGE_SIZE - size,
					"  %s\n", color_caps[i].name);
		}
		size += snprintf(&buf[size], PAGE_SIZE - size,
				 " window colors:\n");
		for (i = 0; i < ARRAY_SIZE(color_caps) &&
		     size < PAGE_SIZE; i++) {
			if (color_caps[i].flag & caps.wnd_color)
				size += snprintf(&buf[size], PAGE_SIZE - size,
					"  %s\n", color_caps[i].name);
		}

		plane++;
	}
	return size;
}

static DEVICE_ATTR(caps_num, 0444, omapfb_show_caps_num, NULL);
static DEVICE_ATTR(caps_text, 0444, omapfb_show_caps_text, NULL);

/* panel sysfs entries */
static ssize_t omapfb_show_panel_name(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct omapfb_device *fbdev = (struct omapfb_device *)dev->driver_data;

	return snprintf(buf, PAGE_SIZE, "%s\n", fbdev->panel->name);
}

static ssize_t omapfb_show_bklight_level(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct omapfb_device *fbdev = (struct omapfb_device *)dev->driver_data;
	int r;

	if (fbdev->panel->get_bklight_level) {
		r = snprintf(buf, PAGE_SIZE, "%d\n",
			     fbdev->panel->get_bklight_level(fbdev->panel));
	} else
		r = -ENODEV;
	return r;
}

static ssize_t omapfb_store_bklight_level(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t size)
{
	struct omapfb_device *fbdev = (struct omapfb_device *)dev->driver_data;
	int r;

	if (fbdev->panel->set_bklight_level) {
		unsigned int level;

		if (sscanf(buf, "%10d", &level) == 1) {
			r = fbdev->panel->set_bklight_level(fbdev->panel,
							    level);
		} else
			r = -EINVAL;
	} else
		r = -ENODEV;
	return r ? r : size;
}

static ssize_t omapfb_show_bklight_max(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct omapfb_device *fbdev = (struct omapfb_device *)dev->driver_data;
	int r;

	if (fbdev->panel->get_bklight_level) {
		r = snprintf(buf, PAGE_SIZE, "%d\n",
			     fbdev->panel->get_bklight_max(fbdev->panel));
	} else
		r = -ENODEV;
	return r;
}

static struct device_attribute dev_attr_panel_name =
	__ATTR(name, 0444, omapfb_show_panel_name, NULL);
static DEVICE_ATTR(backlight_level, 0664,
		   omapfb_show_bklight_level, omapfb_store_bklight_level);
static DEVICE_ATTR(backlight_max, 0444, omapfb_show_bklight_max, NULL);

static struct attribute *panel_attrs[] = {
	&dev_attr_panel_name.attr,
	&dev_attr_backlight_level.attr,
	&dev_attr_backlight_max.attr,
	NULL,
};

static struct attribute_group panel_attr_grp = {
	.name  = "panel",
	.attrs = panel_attrs,
};

/* ctrl sysfs entries */
static ssize_t omapfb_show_ctrl_name(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct omapfb_device *fbdev = (struct omapfb_device *)dev->driver_data;

	return snprintf(buf, PAGE_SIZE, "%s\n", fbdev->ctrl->name);
}

static struct device_attribute dev_attr_ctrl_name =
	__ATTR(name, 0444, omapfb_show_ctrl_name, NULL);

static struct attribute *ctrl_attrs[] = {
	&dev_attr_ctrl_name.attr,
	NULL,
};

static struct attribute_group ctrl_attr_grp = {
	.name  = "ctrl",
	.attrs = ctrl_attrs,
};

static int omapfb_register_sysfs(struct omapfb_device *fbdev)
{
	int r;

	if ((r = device_create_file(fbdev->dev, &dev_attr_caps_num)))
		goto fail0;

	if ((r = device_create_file(fbdev->dev, &dev_attr_caps_text)))
		goto fail1;

	if ((r = sysfs_create_group(&fbdev->dev->kobj, &panel_attr_grp)))
		goto fail2;

	if ((r = sysfs_create_group(&fbdev->dev->kobj, &ctrl_attr_grp)))
		goto fail3;

	return 0;
fail3:
	sysfs_remove_group(&fbdev->dev->kobj, &panel_attr_grp);
fail2:
	device_remove_file(fbdev->dev, &dev_attr_caps_text);
fail1:
	device_remove_file(fbdev->dev, &dev_attr_caps_num);
fail0:
	dev_err(fbdev->dev, "unable to register sysfs interface\n");
	return r;
}

static void omapfb_unregister_sysfs(struct omapfb_device *fbdev)
{
	sysfs_remove_group(&fbdev->dev->kobj, &ctrl_attr_grp);
	sysfs_remove_group(&fbdev->dev->kobj, &panel_attr_grp);
	device_remove_file(fbdev->dev, &dev_attr_caps_num);
	device_remove_file(fbdev->dev, &dev_attr_caps_text);
}

/*
 * ---------------------------------------------------------------------------
 * LDM callbacks
 * ---------------------------------------------------------------------------
 */
/* Initialize system fb_info object and set the default video mode.
 * The frame buffer memory already allocated by lcddma_init
 */
static int fbinfo_init(struct omapfb_device *fbdev, struct fb_info *info)
{
	struct fb_var_screeninfo	*var = &info->var;
	struct fb_fix_screeninfo	*fix = &info->fix;
	int				r = 0;

	info->fbops = &omapfb_ops;
	info->flags = FBINFO_FLAG_DEFAULT;

	strncpy(fix->id, MODULE_NAME, sizeof(fix->id));

	info->pseudo_palette = fbdev->pseudo_palette;

	var->accel_flags  = def_accel ? FB_ACCELF_TEXT : 0;
	var->xres = def_vxres;
	var->yres = def_vyres;
	var->xres_virtual = def_vxres;
	var->yres_virtual = def_vyres;
	var->rotate	  = def_rotate;
	var->bits_per_pixel = fbdev->panel->bpp;

	set_fb_var(info, var);
	set_fb_fix(info);

	r = fb_alloc_cmap(&info->cmap, 16, 0);
	if (r != 0)
		dev_err(fbdev->dev, "unable to allocate color map memory\n");

	return r;
}

/* Release the fb_info object */
static void fbinfo_cleanup(struct omapfb_device *fbdev, struct fb_info *fbi)
{
	fb_dealloc_cmap(&fbi->cmap);
}

static void planes_cleanup(struct omapfb_device *fbdev)
{
	int i;

	for (i = 0; i < fbdev->mem_desc.region_cnt; i++) {
		if (fbdev->fb_info[i] == NULL)
			break;
		fbinfo_cleanup(fbdev, fbdev->fb_info[i]);
		framebuffer_release(fbdev->fb_info[i]);
	}
}

static int planes_init(struct omapfb_device *fbdev)
{
	struct fb_info *fbi;
	int i;
	int r;

	for (i = 0; i < fbdev->mem_desc.region_cnt; i++) {
		struct omapfb_plane_struct *plane;
		fbi = framebuffer_alloc(sizeof(struct omapfb_plane_struct),
					fbdev->dev);
		if (fbi == NULL) {
			dev_err(fbdev->dev,
				"unable to allocate memory for plane info\n");
			planes_cleanup(fbdev);
			return -ENOMEM;
		}
		plane = fbi->par;
		plane->idx = i;
		plane->fbdev = fbdev;
		plane->info.mirror = def_mirror;
		fbdev->fb_info[i] = fbi;

		if ((r = fbinfo_init(fbdev, fbi)) < 0) {
			framebuffer_release(fbi);
			planes_cleanup(fbdev);
			return r;
		}
		plane->info.out_width = fbi->var.xres;
		plane->info.out_height = fbi->var.yres;
	}
	return 0;
}

/*
 * Free driver resources. Can be called to rollback an aborted initialization
 * sequence.
 */
static void omapfb_free_resources(struct omapfb_device *fbdev, int state)
{
	int i;

	switch (state) {
	case OMAPFB_ACTIVE:
		for (i = 0; i < fbdev->mem_desc.region_cnt; i++)
			unregister_framebuffer(fbdev->fb_info[i]);
	case 7:
		omapfb_unregister_sysfs(fbdev);
	case 6:
		fbdev->panel->disable(fbdev->panel);
	case 5:
		omapfb_set_update_mode(fbdev, OMAPFB_UPDATE_DISABLED);
	case 4:
		planes_cleanup(fbdev);
	case 3:
		ctrl_cleanup(fbdev);
	case 2:
		fbdev->panel->cleanup(fbdev->panel);
	case 1:
		dev_set_drvdata(fbdev->dev, NULL);
		kfree(fbdev);
	case 0:
		/* nothing to free */
		break;
	default:
		BUG();
	}
}

static int omapfb_find_ctrl(struct omapfb_device *fbdev)
{
	struct omapfb_platform_data *conf;
	char name[17];
	int i;

	conf = fbdev->dev->platform_data;

	fbdev->ctrl = NULL;

	strncpy(name, conf->lcd.ctrl_name, sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';

	if (strcmp(name, "internal") == 0) {
		fbdev->ctrl = fbdev->int_ctrl;
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(ctrls); i++) {
		dev_dbg(fbdev->dev, "ctrl %s\n", ctrls[i]->name);
		if (strcmp(ctrls[i]->name, name) == 0) {
			fbdev->ctrl = ctrls[i];
			break;
		}
	}

	if (fbdev->ctrl == NULL) {
		dev_dbg(fbdev->dev, "ctrl %s not supported\n", name);
		return -1;
	}

	return 0;
}

static void check_required_callbacks(struct omapfb_device *fbdev)
{
#define _C(x) (fbdev->ctrl->x != NULL)
#define _P(x) (fbdev->panel->x != NULL)
	BUG_ON(fbdev->ctrl == NULL || fbdev->panel == NULL);
	BUG_ON(!(_C(init) && _C(cleanup) && _C(get_caps) &&
		 _C(set_update_mode) && _C(setup_plane) && _C(enable_plane) &&
		 _P(init) && _P(cleanup) && _P(enable) && _P(disable) &&
		 _P(get_caps)));
#undef _P
#undef _C
}

/*
 * Called by LDM binding to probe and attach a new device.
 * Initialization sequence:
 *   1. allocate system omapfb_device structure
 *   2. select controller type according to platform configuration
 *      init LCD panel
 *   3. init LCD controller and LCD DMA
 *   4. init system fb_info structure for all planes
 *   5. setup video mode for first plane and enable it
 *   6. enable LCD panel
 *   7. register sysfs attributes
 *   OMAPFB_ACTIVE: register system fb_info structure for all planes
 */
static int omapfb_do_probe(struct platform_device *pdev,
				struct lcd_panel *panel)
{
	struct omapfb_device	*fbdev = NULL;
	int			init_state;
	unsigned long		phz, hhz, vhz;
	unsigned long		vram;
	int			i;
	int			r = 0;

	init_state = 0;

	if (pdev->num_resources != 0) {
		dev_err(&pdev->dev, "probed for an unknown device\n");
		r = -ENODEV;
		goto cleanup;
	}

	if (pdev->dev.platform_data == NULL) {
		dev_err(&pdev->dev, "missing platform data\n");
		r = -ENOENT;
		goto cleanup;
	}

	fbdev = kzalloc(sizeof(struct omapfb_device), GFP_KERNEL);
	if (fbdev == NULL) {
		dev_err(&pdev->dev,
			"unable to allocate memory for device info\n");
		r = -ENOMEM;
		goto cleanup;
	}
	init_state++;

	fbdev->dev = &pdev->dev;
	fbdev->panel = panel;
	platform_set_drvdata(pdev, fbdev);

	mutex_init(&fbdev->rqueue_mutex);

#ifdef CONFIG_ARCH_OMAP1
	fbdev->int_ctrl = &omap1_int_ctrl;
#ifdef CONFIG_FB_OMAP_LCDC_EXTERNAL
	fbdev->ext_if = &omap1_ext_if;
#endif
#else	/* OMAP2 */
	fbdev->int_ctrl = &omap2_int_ctrl;
#ifdef CONFIG_FB_OMAP_LCDC_EXTERNAL
	fbdev->ext_if = &omap2_ext_if;
#endif
#endif
	if (omapfb_find_ctrl(fbdev) < 0) {
		dev_err(fbdev->dev,
			"LCD controller not found, board not supported\n");
		r = -ENODEV;
		goto cleanup;
	}

	r = fbdev->panel->init(fbdev->panel, fbdev);
	if (r)
		goto cleanup;

	pr_info("omapfb: configured for panel %s\n", fbdev->panel->name);

	def_vxres = def_vxres ? : fbdev->panel->x_res;
	def_vyres = def_vyres ? : fbdev->panel->y_res;

	init_state++;

	r = ctrl_init(fbdev);
	if (r)
		goto cleanup;
	if (fbdev->ctrl->mmap != NULL)
		omapfb_ops.fb_mmap = omapfb_mmap;
	init_state++;

	check_required_callbacks(fbdev);

	r = planes_init(fbdev);
	if (r)
		goto cleanup;
	init_state++;

#ifdef CONFIG_FB_OMAP_DMA_TUNE
	/* Set DMA priority for EMIFF access to highest */
	if (cpu_class_is_omap1())
		omap_set_dma_priority(0, OMAP_DMA_PORT_EMIFF, 15);
#endif

	r = ctrl_change_mode(fbdev->fb_info[0]);
	if (r) {
		dev_err(fbdev->dev, "mode setting failed\n");
		goto cleanup;
	}

	/* GFX plane is enabled by default */
	r = fbdev->ctrl->enable_plane(OMAPFB_PLANE_GFX, 1);
	if (r)
		goto cleanup;

	omapfb_set_update_mode(fbdev, manual_update ?
				   OMAPFB_MANUAL_UPDATE : OMAPFB_AUTO_UPDATE);
	init_state++;

	r = fbdev->panel->enable(fbdev->panel);
	if (r)
		goto cleanup;
	init_state++;

	r = omapfb_register_sysfs(fbdev);
	if (r)
		goto cleanup;
	init_state++;

	vram = 0;
	for (i = 0; i < fbdev->mem_desc.region_cnt; i++) {
		r = register_framebuffer(fbdev->fb_info[i]);
		if (r != 0) {
			dev_err(fbdev->dev,
				"registering framebuffer %d failed\n", i);
			goto cleanup;
		}
		vram += fbdev->mem_desc.region[i].size;
	}

	fbdev->state = OMAPFB_ACTIVE;

	panel = fbdev->panel;
	phz = panel->pixel_clock * 1000;
	hhz = phz * 10 / (panel->hfp + panel->x_res + panel->hbp + panel->hsw);
	vhz = hhz / (panel->vfp + panel->y_res + panel->vbp + panel->vsw);

	omapfb_dev = fbdev;

	pr_info("omapfb: Framebuffer initialized. Total vram %lu planes %d\n",
			vram, fbdev->mem_desc.region_cnt);
	pr_info("omapfb: Pixclock %lu kHz hfreq %lu.%lu kHz "
			"vfreq %lu.%lu Hz\n",
			phz / 1000, hhz / 10000, hhz % 10, vhz / 10, vhz % 10);

	return 0;

cleanup:
	omapfb_free_resources(fbdev, init_state);

	return r;
}

static int omapfb_probe(struct platform_device *pdev)
{
	BUG_ON(fbdev_pdev != NULL);

	/* Delay actual initialization until the LCD is registered */
	fbdev_pdev = pdev;
	if (fbdev_panel != NULL)
		omapfb_do_probe(fbdev_pdev, fbdev_panel);
	return 0;
}

void omapfb_register_panel(struct lcd_panel *panel)
{
	BUG_ON(fbdev_panel != NULL);

	fbdev_panel = panel;
	if (fbdev_pdev != NULL)
		omapfb_do_probe(fbdev_pdev, fbdev_panel);
}

/* Called when the device is being detached from the driver */
static int omapfb_remove(struct platform_device *pdev)
{
	struct omapfb_device *fbdev = platform_get_drvdata(pdev);
	enum omapfb_state saved_state = fbdev->state;

	/* FIXME: wait till completion of pending events */

	fbdev->state = OMAPFB_DISABLED;
	omapfb_free_resources(fbdev, saved_state);

	return 0;
}

/* PM suspend */
static int omapfb_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	struct omapfb_device *fbdev = platform_get_drvdata(pdev);

	omapfb_blank(VESA_POWERDOWN, fbdev->fb_info[0]);

	return 0;
}

/* PM resume */
static int omapfb_resume(struct platform_device *pdev)
{
	struct omapfb_device *fbdev = platform_get_drvdata(pdev);

	omapfb_blank(VESA_NO_BLANKING, fbdev->fb_info[0]);
	return 0;
}

static struct platform_driver omapfb_driver = {
	.probe		= omapfb_probe,
	.remove		= omapfb_remove,
	.suspend	= omapfb_suspend,
	.resume		= omapfb_resume,
	.driver		= {
		.name	= MODULE_NAME,
		.owner	= THIS_MODULE,
	},
};

#ifndef MODULE

/* Process kernel command line parameters */
static int __init omapfb_setup(char *options)
{
	char *this_opt = NULL;
	int r = 0;

	pr_debug("omapfb: options %s\n", options);

	if (!options || !*options)
		return 0;

	while (!r && (this_opt = strsep(&options, ",")) != NULL) {
		if (!strncmp(this_opt, "accel", 5))
			def_accel = 1;
		else if (!strncmp(this_opt, "vram:", 5)) {
			char *suffix;
			unsigned long vram;
			vram = (simple_strtoul(this_opt + 5, &suffix, 0));
			switch (suffix[0]) {
			case '\0':
				break;
			case 'm':
			case 'M':
				vram *= 1024;
				/* Fall through */
			case 'k':
			case 'K':
				vram *= 1024;
				break;
			default:
				pr_debug("omapfb: invalid vram suffix %c\n",
					 suffix[0]);
				r = -1;
			}
			def_vram[def_vram_cnt++] = vram;
		}
		else if (!strncmp(this_opt, "vxres:", 6))
			def_vxres = simple_strtoul(this_opt + 6, NULL, 0);
		else if (!strncmp(this_opt, "vyres:", 6))
			def_vyres = simple_strtoul(this_opt + 6, NULL, 0);
		else if (!strncmp(this_opt, "rotate:", 7))
			def_rotate = (simple_strtoul(this_opt + 7, NULL, 0));
		else if (!strncmp(this_opt, "mirror:", 7))
			def_mirror = (simple_strtoul(this_opt + 7, NULL, 0));
		else if (!strncmp(this_opt, "manual_update", 13))
			manual_update = 1;
		else {
			pr_debug("omapfb: invalid option\n");
			r = -1;
		}
	}

	return r;
}

#endif

/* Register both the driver and the device */
static int __init omapfb_init(void)
{
#ifndef MODULE
	char *option;

	if (fb_get_options("omapfb", &option))
		return -ENODEV;
	omapfb_setup(option);
#endif
	/* Register the driver with LDM */
	if (platform_driver_register(&omapfb_driver)) {
		pr_debug("failed to register omapfb driver\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit omapfb_cleanup(void)
{
	platform_driver_unregister(&omapfb_driver);
}

module_param_named(accel, def_accel, uint, 0664);
module_param_array_named(vram, def_vram, ulong, &def_vram_cnt, 0664);
module_param_named(vxres, def_vxres, long, 0664);
module_param_named(vyres, def_vyres, long, 0664);
module_param_named(rotate, def_rotate, uint, 0664);
module_param_named(mirror, def_mirror, uint, 0664);
module_param_named(manual_update, manual_update, bool, 0664);

module_init(omapfb_init);
module_exit(omapfb_cleanup);

MODULE_DESCRIPTION("TI OMAP framebuffer driver");
MODULE_AUTHOR("Imre Deak <imre.deak@nokia.com>");
MODULE_LICENSE("GPL");
