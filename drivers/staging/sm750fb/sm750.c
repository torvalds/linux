// SPDX-License-Identifier: GPL-2.0
#include <linux/aperture.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/mm_types.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/screen_info.h>
#include <linux/console.h>

#include "sm750.h"
#include "sm750_accel.h"
#include "sm750_cursor.h"

/*
 * #ifdef __BIG_ENDIAN
 * ssize_t lynxfb_ops_write(struct fb_info *info, const char __user *buf,
 * size_t count, loff_t *ppos);
 * ssize_t lynxfb_ops_read(struct fb_info *info, char __user *buf,
 * size_t count, loff_t *ppos);
 * #endif
 */

/* common var for all device */
static int g_hwcursor = 1;
static int g_noaccel;
static int g_nomtrr;
static const char *g_fbmode[] = {NULL, NULL};
static const char *g_def_fbmode = "1024x768-32@60";
static char *g_settings;
static int g_dualview;
static char *g_option;

static const struct fb_videomode lynx750_ext[] = {
	/*	1024x600-60 VESA	[1.71:1] */
	{NULL,  60, 1024, 600, 20423, 144,  40, 18, 1, 104, 3,
	 FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	 FB_VMODE_NONINTERLACED},

	/*	1024x600-70 VESA */
	{NULL,  70, 1024, 600, 17211, 152,  48, 21, 1, 104, 3,
	 FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	 FB_VMODE_NONINTERLACED},

	/*	1024x600-75 VESA */
	{NULL,  75, 1024, 600, 15822, 160,  56, 23, 1, 104, 3,
	 FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	 FB_VMODE_NONINTERLACED},

	/*	1024x600-85 VESA */
	{NULL,  85, 1024, 600, 13730, 168,  56, 26, 1, 112, 3,
	 FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	 FB_VMODE_NONINTERLACED},

	/*	720x480	*/
	{NULL, 60,  720,  480,  37427, 88,   16, 13, 1,   72,  3,
	 FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	 FB_VMODE_NONINTERLACED},

	/*	1280x720		[1.78:1]	*/
	{NULL, 60,  1280,  720,  13426, 162, 86, 22, 1,  136, 3,
	 FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	 FB_VMODE_NONINTERLACED},

	/*	1280x768@60 */
	{NULL, 60, 1280, 768, 12579, 192, 64, 20, 3, 128, 7,
	 FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	 FB_VMODE_NONINTERLACED},

	/*	1360 x 768	[1.77083:1]	*/
	{NULL,  60, 1360, 768, 11804, 208,  64, 23, 1, 144, 3,
	 FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	 FB_VMODE_NONINTERLACED},

	/*	1368 x 768      [1.78:1]	*/
	{NULL, 60,  1368,  768,  11647, 216, 72, 23, 1,  144, 3,
	 FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	 FB_VMODE_NONINTERLACED},

	/*	1440 x 900		[16:10]	*/
	{NULL, 60, 1440, 900, 9392, 232, 80, 28, 1, 152, 3,
	 FB_SYNC_VERT_HIGH_ACT,
	 FB_VMODE_NONINTERLACED},

	/*	1440x960		[15:10]	*/
	{NULL, 60, 1440, 960, 8733, 240, 88, 30, 1, 152, 3,
	 FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	 FB_VMODE_NONINTERLACED},

	/*	1920x1080	[16:9]	*/
	{NULL, 60, 1920, 1080, 6734, 148, 88, 41, 1, 44, 3,
	 FB_SYNC_VERT_HIGH_ACT,
	 FB_VMODE_NONINTERLACED},
};

/* no hardware cursor supported under version 2.6.10, kernel bug */
static int lynxfb_ops_cursor(struct fb_info *info, struct fb_cursor *fbcursor)
{
	struct lynxfb_par *par;
	struct lynxfb_crtc *crtc;
	struct lynx_cursor *cursor;

	par = info->par;
	crtc = &par->crtc;
	cursor = &crtc->cursor;

	if (fbcursor->image.width > cursor->max_w ||
	    fbcursor->image.height > cursor->max_h ||
	    fbcursor->image.depth > 1) {
		return -ENXIO;
	}

	sm750_hw_cursor_disable(cursor);
	if (fbcursor->set & FB_CUR_SETSIZE)
		sm750_hw_cursor_setSize(cursor,
					fbcursor->image.width,
					fbcursor->image.height);

	if (fbcursor->set & FB_CUR_SETPOS)
		sm750_hw_cursor_setPos(cursor,
				       fbcursor->image.dx - info->var.xoffset,
				       fbcursor->image.dy - info->var.yoffset);

	if (fbcursor->set & FB_CUR_SETCMAP) {
		/* get the 16bit color of kernel means */
		u16 fg, bg;

		fg = ((info->cmap.red[fbcursor->image.fg_color] & 0xf800)) |
		     ((info->cmap.green[fbcursor->image.fg_color] & 0xfc00) >> 5) |
		     ((info->cmap.blue[fbcursor->image.fg_color] & 0xf800) >> 11);

		bg = ((info->cmap.red[fbcursor->image.bg_color] & 0xf800)) |
		     ((info->cmap.green[fbcursor->image.bg_color] & 0xfc00) >> 5) |
		     ((info->cmap.blue[fbcursor->image.bg_color] & 0xf800) >> 11);

		sm750_hw_cursor_setColor(cursor, fg, bg);
	}

	if (fbcursor->set & (FB_CUR_SETSHAPE | FB_CUR_SETIMAGE)) {
		sm750_hw_cursor_setData(cursor,
					fbcursor->rop,
					fbcursor->image.data,
					fbcursor->mask);
	}

	if (fbcursor->enable)
		sm750_hw_cursor_enable(cursor);

	return 0;
}

static void lynxfb_ops_fillrect(struct fb_info *info,
				const struct fb_fillrect *region)
{
	struct lynxfb_par *par;
	struct sm750_dev *sm750_dev;
	unsigned int base, pitch, Bpp, rop;
	u32 color;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	par = info->par;
	sm750_dev = par->dev;

	/*
	 * each time 2d function begin to work,below three variable always need
	 * be set, seems we can put them together in some place
	 */
	base = par->crtc.o_screen;
	pitch = info->fix.line_length;
	Bpp = info->var.bits_per_pixel >> 3;

	color = (Bpp == 1) ? region->color :
		((u32 *)info->pseudo_palette)[region->color];
	rop = (region->rop != ROP_COPY) ? HW_ROP2_XOR : HW_ROP2_COPY;

	/*
	 * If not use spin_lock, system will die if user load driver
	 * and immediately unload driver frequently (dual)
	 * since they fb_count could change during the lifetime of
	 * this lock, we are holding it for all cases.
	 */
	spin_lock(&sm750_dev->slock);

	sm750_dev->accel.de_fillrect(&sm750_dev->accel,
				     base, pitch, Bpp,
				     region->dx, region->dy,
				     region->width, region->height,
				     color, rop);
	spin_unlock(&sm750_dev->slock);
}

static void lynxfb_ops_copyarea(struct fb_info *info,
				const struct fb_copyarea *region)
{
	struct lynxfb_par *par;
	struct sm750_dev *sm750_dev;
	unsigned int base, pitch, Bpp;

	par = info->par;
	sm750_dev = par->dev;

	/*
	 * each time 2d function begin to work,below three variable always need
	 * be set, seems we can put them together in some place
	 */
	base = par->crtc.o_screen;
	pitch = info->fix.line_length;
	Bpp = info->var.bits_per_pixel >> 3;

	/*
	 * If not use spin_lock, system will die if user load driver
	 * and immediately unload driver frequently (dual)
	 * since they fb_count could change during the lifetime of
	 * this lock, we are holding it for all cases.
	 */
	spin_lock(&sm750_dev->slock);

	sm750_dev->accel.de_copyarea(&sm750_dev->accel,
				     base, pitch, region->sx, region->sy,
				     base, pitch, Bpp, region->dx, region->dy,
				     region->width, region->height,
				     HW_ROP2_COPY);
	spin_unlock(&sm750_dev->slock);
}

static void lynxfb_ops_imageblit(struct fb_info *info,
				 const struct fb_image *image)
{
	unsigned int base, pitch, Bpp;
	unsigned int fgcol, bgcol;
	struct lynxfb_par *par;
	struct sm750_dev *sm750_dev;

	par = info->par;
	sm750_dev = par->dev;
	/*
	 * each time 2d function begin to work,below three variable always need
	 * be set, seems we can put them together in some place
	 */
	base = par->crtc.o_screen;
	pitch = info->fix.line_length;
	Bpp = info->var.bits_per_pixel >> 3;

	/* TODO: Implement hardware acceleration for image->depth > 1 */
	if (image->depth != 1) {
		cfb_imageblit(info, image);
		return;
	}

	if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
	    info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
		fgcol = ((u32 *)info->pseudo_palette)[image->fg_color];
		bgcol = ((u32 *)info->pseudo_palette)[image->bg_color];
	} else {
		fgcol = image->fg_color;
		bgcol = image->bg_color;
	}

	/*
	 * If not use spin_lock, system will die if user load driver
	 * and immediately unload driver frequently (dual)
	 * since they fb_count could change during the lifetime of
	 * this lock, we are holding it for all cases.
	 */
	spin_lock(&sm750_dev->slock);

	sm750_dev->accel.de_imageblit(&sm750_dev->accel,
				      image->data, image->width >> 3, 0,
				      base, pitch, Bpp,
				      image->dx, image->dy,
				      image->width, image->height,
				      fgcol, bgcol, HW_ROP2_COPY);
	spin_unlock(&sm750_dev->slock);
}

static int lynxfb_ops_pan_display(struct fb_var_screeninfo *var,
				  struct fb_info *info)
{
	struct lynxfb_par *par;
	struct lynxfb_crtc *crtc;

	if (!info)
		return -EINVAL;

	par = info->par;
	crtc = &par->crtc;
	return hw_sm750_pan_display(crtc, var, info);
}

static inline void lynxfb_set_visual_mode(struct fb_info *info)
{
	switch (info->var.bits_per_pixel) {
	case 8:
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		break;
	case 16:
	case 24:
	case 32:
		info->fix.visual = FB_VISUAL_TRUECOLOR;
		break;
	default:
		break;
	}
}

static inline int lynxfb_set_color_offsets(struct fb_info *info)
{
	lynxfb_set_visual_mode(info);

	switch (info->var.bits_per_pixel) {
	case 8:
		info->var.red.offset = 0;
		info->var.red.length = 8;
		info->var.green.offset = 0;
		info->var.green.length = 8;
		info->var.blue.offset = 0;
		info->var.blue.length = 8;
		info->var.transp.length = 0;
		info->var.transp.offset = 0;
		break;
	case 16:
		info->var.red.offset = 11;
		info->var.red.length = 5;
		info->var.green.offset = 5;
		info->var.green.length = 6;
		info->var.blue.offset = 0;
		info->var.blue.length = 5;
		info->var.transp.length = 0;
		info->var.transp.offset = 0;
		break;
	case 24:
	case 32:
		info->var.red.offset = 16;
		info->var.red.length = 8;
		info->var.green.offset = 8;
		info->var.green.length = 8;
		info->var.blue.offset = 0;
		info->var.blue.length = 8;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int lynxfb_ops_set_par(struct fb_info *info)
{
	struct lynxfb_par *par;
	struct lynxfb_crtc *crtc;
	struct lynxfb_output *output;
	struct fb_var_screeninfo *var;
	struct fb_fix_screeninfo *fix;
	int ret;
	unsigned int line_length;

	if (!info)
		return -EINVAL;

	ret = 0;
	par = info->par;
	crtc = &par->crtc;
	output = &par->output;
	var = &info->var;
	fix = &info->fix;

	/* fix structure is not so FIX ... */
	line_length = var->xres_virtual * var->bits_per_pixel / 8;
	line_length = ALIGN(line_length, crtc->line_pad);
	fix->line_length = line_length;
	pr_info("fix->line_length = %d\n", fix->line_length);

	/*
	 * var->red,green,blue,transp are need to be set by driver
	 * and these data should be set before setcolreg routine
	 */

	ret = lynxfb_set_color_offsets(info);

	var->height = -1;
	var->width = -1;
	var->accel_flags = 0;/*FB_ACCELF_TEXT;*/

	if (ret) {
		pr_err("bpp %d not supported\n", var->bits_per_pixel);
		return ret;
	}
	ret = hw_sm750_crtc_setMode(crtc, var, fix);
	if (!ret)
		ret = hw_sm750_output_setMode(output, var, fix);
	return ret;
}

static inline unsigned int chan_to_field(unsigned int chan,
					 struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int __maybe_unused lynxfb_suspend(struct device *dev)
{
	struct fb_info *info;
	struct sm750_dev *sm750_dev;

	sm750_dev = dev_get_drvdata(dev);

	console_lock();
	info = sm750_dev->fbinfo[0];
	if (info)
		/* 1 means do suspend */
		fb_set_suspend(info, 1);
	info = sm750_dev->fbinfo[1];
	if (info)
		/* 1 means do suspend */
		fb_set_suspend(info, 1);

	console_unlock();
	return 0;
}

static int __maybe_unused lynxfb_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct fb_info *info;
	struct sm750_dev *sm750_dev;

	struct lynxfb_par *par;
	struct lynxfb_crtc *crtc;
	struct lynx_cursor *cursor;

	sm750_dev = pci_get_drvdata(pdev);

	console_lock();

	hw_sm750_inithw(sm750_dev, pdev);

	info = sm750_dev->fbinfo[0];

	if (info) {
		par = info->par;
		crtc = &par->crtc;
		cursor = &crtc->cursor;
		memset_io(cursor->vstart, 0x0, cursor->size);
		memset_io(crtc->v_screen, 0x0, crtc->vidmem_size);
		lynxfb_ops_set_par(info);
		fb_set_suspend(info, 0);
	}

	info = sm750_dev->fbinfo[1];

	if (info) {
		par = info->par;
		crtc = &par->crtc;
		cursor = &crtc->cursor;
		memset_io(cursor->vstart, 0x0, cursor->size);
		memset_io(crtc->v_screen, 0x0, crtc->vidmem_size);
		lynxfb_ops_set_par(info);
		fb_set_suspend(info, 0);
	}

	pdev->dev.power.power_state.event = PM_EVENT_RESUME;

	console_unlock();
	return 0;
}

static int lynxfb_ops_check_var(struct fb_var_screeninfo *var,
				struct fb_info *info)
{
	int ret;
	struct lynxfb_par *par;
	struct lynxfb_crtc *crtc;
	resource_size_t request;

	ret = 0;
	par = info->par;
	crtc = &par->crtc;

	pr_debug("check var:%dx%d-%d\n",
		 var->xres,
		 var->yres,
		 var->bits_per_pixel);

	ret = lynxfb_set_color_offsets(info);

	if (ret) {
		pr_err("bpp %d not supported\n", var->bits_per_pixel);
		return ret;
	}

	var->height = -1;
	var->width = -1;
	var->accel_flags = 0;/* FB_ACCELF_TEXT; */

	/* check if current fb's video memory big enough to hold the onscreen*/
	request = var->xres_virtual * (var->bits_per_pixel >> 3);
	/* defaulty crtc->channel go with par->index */

	request = ALIGN(request, crtc->line_pad);
	request = request * var->yres_virtual;
	if (crtc->vidmem_size < request) {
		pr_err("not enough video memory for mode\n");
		return -ENOMEM;
	}

	return hw_sm750_crtc_checkMode(crtc, var);
}

static int lynxfb_ops_setcolreg(unsigned int regno,
				unsigned int red,
				unsigned int green,
				unsigned int blue,
				unsigned int transp,
				struct fb_info *info)
{
	struct lynxfb_par *par;
	struct lynxfb_crtc *crtc;
	struct fb_var_screeninfo *var;
	int ret;

	par = info->par;
	crtc = &par->crtc;
	var = &info->var;
	ret = 0;

	if (regno > 256) {
		pr_err("regno = %d\n", regno);
		return -EINVAL;
	}

	if (info->var.grayscale)
		red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;

	if (var->bits_per_pixel == 8 &&
	    info->fix.visual == FB_VISUAL_PSEUDOCOLOR) {
		red >>= 8;
		green >>= 8;
		blue >>= 8;
		ret = hw_sm750_setColReg(crtc, regno, red, green, blue);
		goto exit;
	}

	if (info->fix.visual == FB_VISUAL_TRUECOLOR && regno < 256) {
		u32 val;

		if (var->bits_per_pixel == 16 ||
		    var->bits_per_pixel == 32 ||
		    var->bits_per_pixel == 24) {
			val = chan_to_field(red, &var->red);
			val |= chan_to_field(green, &var->green);
			val |= chan_to_field(blue, &var->blue);
			par->pseudo_palette[regno] = val;
			goto exit;
		}
	}

	ret = -EINVAL;

exit:
	return ret;
}

static int lynxfb_ops_blank(int blank, struct fb_info *info)
{
	struct lynxfb_par *par;
	struct lynxfb_output *output;

	pr_debug("blank = %d.\n", blank);
	par = info->par;
	output = &par->output;
	return output->proc_setBLANK(output, blank);
}

static int sm750fb_set_drv(struct lynxfb_par *par)
{
	int ret;
	struct sm750_dev *sm750_dev;
	struct lynxfb_output *output;
	struct lynxfb_crtc *crtc;

	ret = 0;

	sm750_dev = par->dev;
	output = &par->output;
	crtc = &par->crtc;

	crtc->vidmem_size = sm750_dev->vidmem_size;
	if (sm750_dev->fb_count > 1)
		crtc->vidmem_size >>= 1;

	/* setup crtc and output member */
	sm750_dev->hwCursor = g_hwcursor;

	crtc->line_pad = 16;
	crtc->xpanstep = 8;
	crtc->ypanstep = 1;
	crtc->ywrapstep = 0;

	output->proc_setBLANK = (sm750_dev->revid == SM750LE_REVISION_ID) ?
				 hw_sm750le_setBLANK : hw_sm750_setBLANK;
	/* chip specific phase */
	sm750_dev->accel.de_wait = (sm750_dev->revid == SM750LE_REVISION_ID) ?
				    hw_sm750le_deWait : hw_sm750_deWait;
	switch (sm750_dev->dataflow) {
	case sm750_simul_pri:
		output->paths = sm750_pnc;
		crtc->channel = sm750_primary;
		crtc->o_screen = 0;
		crtc->v_screen = sm750_dev->pvMem;
		pr_info("use simul primary mode\n");
		break;
	case sm750_simul_sec:
		output->paths = sm750_pnc;
		crtc->channel = sm750_secondary;
		crtc->o_screen = 0;
		crtc->v_screen = sm750_dev->pvMem;
		break;
	case sm750_dual_normal:
		if (par->index == 0) {
			output->paths = sm750_panel;
			crtc->channel = sm750_primary;
			crtc->o_screen = 0;
			crtc->v_screen = sm750_dev->pvMem;
		} else {
			output->paths = sm750_crt;
			crtc->channel = sm750_secondary;
			/* not consider of padding stuffs for o_screen,need fix */
			crtc->o_screen = sm750_dev->vidmem_size >> 1;
			crtc->v_screen = sm750_dev->pvMem + crtc->o_screen;
		}
		break;
	case sm750_dual_swap:
		if (par->index == 0) {
			output->paths = sm750_panel;
			crtc->channel = sm750_secondary;
			crtc->o_screen = 0;
			crtc->v_screen = sm750_dev->pvMem;
		} else {
			output->paths = sm750_crt;
			crtc->channel = sm750_primary;
			/* not consider of padding stuffs for o_screen,
			 * need fix
			 */
			crtc->o_screen = sm750_dev->vidmem_size >> 1;
			crtc->v_screen = sm750_dev->pvMem + crtc->o_screen;
		}
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static struct fb_ops lynxfb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var =  lynxfb_ops_check_var,
	.fb_set_par = lynxfb_ops_set_par,
	.fb_setcolreg = lynxfb_ops_setcolreg,
	.fb_blank = lynxfb_ops_blank,
	.fb_fillrect = cfb_fillrect,
	.fb_imageblit = cfb_imageblit,
	.fb_copyarea = cfb_copyarea,
	/* cursor */
	.fb_cursor = lynxfb_ops_cursor,
};

static int lynxfb_set_fbinfo(struct fb_info *info, int index)
{
	int i;
	struct lynxfb_par *par;
	struct sm750_dev *sm750_dev;
	struct lynxfb_crtc *crtc;
	struct lynxfb_output *output;
	struct fb_var_screeninfo *var;
	struct fb_fix_screeninfo *fix;

	const struct fb_videomode *pdb[] = {
		lynx750_ext, NULL, vesa_modes,
	};
	int cdb[] = {ARRAY_SIZE(lynx750_ext), 0, VESA_MODEDB_SIZE};
	static const char * const mdb_desc[] = {
		"driver prepared modes",
		"kernel prepared default modedb",
		"kernel HELPERS prepared vesa_modes",
	};

	static const char *fixId[2] = {
		"sm750_fb1", "sm750_fb2",
	};

	int ret, line_length;

	ret = 0;
	par = (struct lynxfb_par *)info->par;
	sm750_dev = par->dev;
	crtc = &par->crtc;
	output = &par->output;
	var = &info->var;
	fix = &info->fix;

	/* set index */
	par->index = index;
	output->channel = &crtc->channel;
	sm750fb_set_drv(par);
	lynxfb_ops.fb_pan_display = lynxfb_ops_pan_display;

	/*
	 * set current cursor variable and proc pointer,
	 * must be set after crtc member initialized
	 */
	crtc->cursor.offset = crtc->o_screen + crtc->vidmem_size - 1024;
	crtc->cursor.mmio = sm750_dev->pvReg +
		0x800f0 + (int)crtc->channel * 0x140;

	pr_info("crtc->cursor.mmio = %p\n", crtc->cursor.mmio);
	crtc->cursor.max_h = 64;
	crtc->cursor.max_w = 64;
	crtc->cursor.size = crtc->cursor.max_h * crtc->cursor.max_w * 2 / 8;
	crtc->cursor.vstart = sm750_dev->pvMem + crtc->cursor.offset;

	memset_io(crtc->cursor.vstart, 0, crtc->cursor.size);
	if (!g_hwcursor) {
		lynxfb_ops.fb_cursor = NULL;
		sm750_hw_cursor_disable(&crtc->cursor);
	}

	/* set info->fbops, must be set before fb_find_mode */
	if (!sm750_dev->accel_off) {
		/* use 2d acceleration */
		lynxfb_ops.fb_fillrect = lynxfb_ops_fillrect;
		lynxfb_ops.fb_copyarea = lynxfb_ops_copyarea;
		lynxfb_ops.fb_imageblit = lynxfb_ops_imageblit;
	}
	info->fbops = &lynxfb_ops;

	if (!g_fbmode[index]) {
		g_fbmode[index] = g_def_fbmode;
		if (index)
			g_fbmode[index] = g_fbmode[0];
	}

	for (i = 0; i < 3; i++) {
		ret = fb_find_mode(var, info, g_fbmode[index],
				   pdb[i], cdb[i], NULL, 8);

		if (ret == 1) {
			pr_info("success! use specified mode:%s in %s\n",
				g_fbmode[index],
				mdb_desc[i]);
			break;
		} else if (ret == 2) {
			pr_warn("use specified mode:%s in %s,with an ignored refresh rate\n",
				g_fbmode[index],
				mdb_desc[i]);
			break;
		} else if (ret == 3) {
			pr_warn("wanna use default mode\n");
			/*break;*/
		} else if (ret == 4) {
			pr_warn("fall back to any valid mode\n");
		} else {
			pr_warn("ret = %d,fb_find_mode failed,with %s\n",
				ret,
				mdb_desc[i]);
		}
	}

	/* some member of info->var had been set by fb_find_mode */

	pr_info("Member of info->var is :\n"
		"xres=%d\n"
		"yres=%d\n"
		"xres_virtual=%d\n"
		"yres_virtual=%d\n"
		"xoffset=%d\n"
		"yoffset=%d\n"
		"bits_per_pixel=%d\n"
		" ...\n",
		var->xres,
		var->yres,
		var->xres_virtual,
		var->yres_virtual,
		var->xoffset,
		var->yoffset,
		var->bits_per_pixel);

	/* set par */
	par->info = info;

	/* set info */
	line_length = ALIGN((var->xres_virtual * var->bits_per_pixel / 8),
			    crtc->line_pad);

	info->pseudo_palette = &par->pseudo_palette[0];
	info->screen_base = crtc->v_screen;
	pr_debug("screen_base vaddr = %p\n", info->screen_base);
	info->screen_size = line_length * var->yres_virtual;
	info->flags = FBINFO_FLAG_DEFAULT | 0;

	/* set info->fix */
	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->type_aux = 0;
	fix->xpanstep = crtc->xpanstep;
	fix->ypanstep = crtc->ypanstep;
	fix->ywrapstep = crtc->ywrapstep;
	fix->accel = FB_ACCEL_SMI;

	strscpy(fix->id, fixId[index], sizeof(fix->id));

	fix->smem_start = crtc->o_screen + sm750_dev->vidmem_start;
	pr_info("fix->smem_start = %lx\n", fix->smem_start);
	/*
	 * according to mmap experiment from user space application,
	 * fix->mmio_len should not larger than virtual size
	 * (xres_virtual x yres_virtual x ByPP)
	 * Below line maybe buggy when user mmap fb dev node and write
	 * data into the bound over virtual size
	 */
	fix->smem_len = crtc->vidmem_size;
	pr_info("fix->smem_len = %x\n", fix->smem_len);
	info->screen_size = fix->smem_len;
	fix->line_length = line_length;
	fix->mmio_start = sm750_dev->vidreg_start;
	pr_info("fix->mmio_start = %lx\n", fix->mmio_start);
	fix->mmio_len = sm750_dev->vidreg_size;
	pr_info("fix->mmio_len = %x\n", fix->mmio_len);

	lynxfb_set_visual_mode(info);

	/* set var */
	var->activate = FB_ACTIVATE_NOW;
	var->accel_flags = 0;
	var->vmode = FB_VMODE_NONINTERLACED;

	pr_debug("#1 show info->cmap :\nstart=%d,len=%d,red=%p,green=%p,blue=%p,transp=%p\n",
		 info->cmap.start, info->cmap.len,
		 info->cmap.red, info->cmap.green, info->cmap.blue,
		 info->cmap.transp);

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret < 0) {
		pr_err("Could not allocate memory for cmap.\n");
		goto exit;
	}

	pr_debug("#2 show info->cmap :\nstart=%d,len=%d,red=%p,green=%p,blue=%p,transp=%p\n",
		 info->cmap.start, info->cmap.len,
		 info->cmap.red, info->cmap.green, info->cmap.blue,
		 info->cmap.transp);

exit:
	lynxfb_ops_check_var(var, info);
	return ret;
}

/*	chip specific g_option configuration routine */
static void sm750fb_setup(struct sm750_dev *sm750_dev, char *src)
{
	char *opt;
	int swap;

	swap = 0;

	sm750_dev->initParm.chip_clk = 0;
	sm750_dev->initParm.mem_clk = 0;
	sm750_dev->initParm.master_clk = 0;
	sm750_dev->initParm.powerMode = 0;
	sm750_dev->initParm.setAllEngOff = 0;
	sm750_dev->initParm.resetMemory = 1;

	/* defaultly turn g_hwcursor on for both view */
	g_hwcursor = 3;

	if (!src || !*src) {
		dev_warn(&sm750_dev->pdev->dev, "no specific g_option.\n");
		goto NO_PARAM;
	}

	while ((opt = strsep(&src, ":")) != NULL && *opt != 0) {
		dev_info(&sm750_dev->pdev->dev, "opt=%s\n", opt);
		dev_info(&sm750_dev->pdev->dev, "src=%s\n", src);

		if (!strncmp(opt, "swap", strlen("swap"))) {
			swap = 1;
		} else if (!strncmp(opt, "nocrt", strlen("nocrt"))) {
			sm750_dev->nocrt = 1;
		} else if (!strncmp(opt, "36bit", strlen("36bit"))) {
			sm750_dev->pnltype = sm750_doubleTFT;
		} else if (!strncmp(opt, "18bit", strlen("18bit"))) {
			sm750_dev->pnltype = sm750_dualTFT;
		} else if (!strncmp(opt, "24bit", strlen("24bit"))) {
			sm750_dev->pnltype = sm750_24TFT;
		} else if (!strncmp(opt, "nohwc0", strlen("nohwc0"))) {
			g_hwcursor &= ~0x1;
		} else if (!strncmp(opt, "nohwc1", strlen("nohwc1"))) {
			g_hwcursor &= ~0x2;
		} else if (!strncmp(opt, "nohwc", strlen("nohwc"))) {
			g_hwcursor = 0;
		} else {
			if (!g_fbmode[0]) {
				g_fbmode[0] = opt;
				dev_info(&sm750_dev->pdev->dev,
					 "find fbmode0 : %s\n", g_fbmode[0]);
			} else if (!g_fbmode[1]) {
				g_fbmode[1] = opt;
				dev_info(&sm750_dev->pdev->dev,
					 "find fbmode1 : %s\n", g_fbmode[1]);
			} else {
				dev_warn(&sm750_dev->pdev->dev, "How many view you wann set?\n");
			}
		}
	}

NO_PARAM:
	if (sm750_dev->revid != SM750LE_REVISION_ID) {
		if (sm750_dev->fb_count > 1) {
			if (swap)
				sm750_dev->dataflow = sm750_dual_swap;
			else
				sm750_dev->dataflow = sm750_dual_normal;
		} else {
			if (swap)
				sm750_dev->dataflow = sm750_simul_sec;
			else
				sm750_dev->dataflow = sm750_simul_pri;
		}
	} else {
		/* SM750LE only have one crt channel */
		sm750_dev->dataflow = sm750_simul_sec;
		/* sm750le do not have complex attributes */
		sm750_dev->nocrt = 0;
	}
}

static void sm750fb_framebuffer_release(struct sm750_dev *sm750_dev)
{
	struct fb_info *fb_info;

	while (sm750_dev->fb_count) {
		fb_info = sm750_dev->fbinfo[sm750_dev->fb_count - 1];
		unregister_framebuffer(fb_info);
		framebuffer_release(fb_info);
		sm750_dev->fb_count--;
	}
}

static int sm750fb_framebuffer_alloc(struct sm750_dev *sm750_dev, int fbidx)
{
	struct fb_info *fb_info;
	struct lynxfb_par *par;
	int err;

	fb_info = framebuffer_alloc(sizeof(struct lynxfb_par),
				    &sm750_dev->pdev->dev);
	if (!fb_info)
		return -ENOMEM;

	sm750_dev->fbinfo[fbidx] = fb_info;
	par = fb_info->par;
	par->dev = sm750_dev;

	err = lynxfb_set_fbinfo(fb_info, fbidx);
	if (err)
		goto release_fb;

	err = register_framebuffer(fb_info);
	if (err < 0)
		goto release_fb;

	sm750_dev->fb_count++;

	return 0;

release_fb:
	framebuffer_release(fb_info);
	return err;
}

static int lynxfb_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	struct sm750_dev *sm750_dev = NULL;
	int max_fb;
	int fbidx;
	int err;

	err = aperture_remove_conflicting_pci_devices(pdev, "sm750_fb1");
	if (err)
		return err;

	/* enable device */
	err = pcim_enable_device(pdev);
	if (err)
		return err;

	err = -ENOMEM;
	sm750_dev = devm_kzalloc(&pdev->dev, sizeof(*sm750_dev), GFP_KERNEL);
	if (!sm750_dev)
		return err;

	sm750_dev->fbinfo[0] = NULL;
	sm750_dev->fbinfo[1] = NULL;
	sm750_dev->devid = pdev->device;
	sm750_dev->revid = pdev->revision;
	sm750_dev->pdev = pdev;
	sm750_dev->mtrr_off = g_nomtrr;
	sm750_dev->mtrr.vram = 0;
	sm750_dev->accel_off = g_noaccel;
	spin_lock_init(&sm750_dev->slock);

	if (!sm750_dev->accel_off) {
		/*
		 * hook deInit and 2d routines, notes that below hw_xxx
		 * routine can work on most of lynx chips
		 * if some chip need specific function,
		 * please hook it in smXXX_set_drv routine
		 */
		sm750_dev->accel.de_init = sm750_hw_de_init;
		sm750_dev->accel.de_fillrect = sm750_hw_fillrect;
		sm750_dev->accel.de_copyarea = sm750_hw_copyarea;
		sm750_dev->accel.de_imageblit = sm750_hw_imageblit;
	}

	/* call chip specific setup routine  */
	sm750fb_setup(sm750_dev, g_settings);

	/* call chip specific mmap routine */
	err = hw_sm750_map(sm750_dev, pdev);
	if (err)
		return err;

	if (!sm750_dev->mtrr_off)
		sm750_dev->mtrr.vram = arch_phys_wc_add(sm750_dev->vidmem_start,
							sm750_dev->vidmem_size);

	memset_io(sm750_dev->pvMem, 0, sm750_dev->vidmem_size);

	pci_set_drvdata(pdev, sm750_dev);

	/* call chipInit routine */
	hw_sm750_inithw(sm750_dev, pdev);

	/* allocate frame buffer info structures according to g_dualview */
	max_fb = g_dualview ? 2 : 1;
	for (fbidx = 0; fbidx < max_fb; fbidx++) {
		err = sm750fb_framebuffer_alloc(sm750_dev, fbidx);
		if (err)
			goto release_fb;
	}

	return 0;

release_fb:
	sm750fb_framebuffer_release(sm750_dev);
	return err;
}

static void lynxfb_pci_remove(struct pci_dev *pdev)
{
	struct sm750_dev *sm750_dev;

	sm750_dev = pci_get_drvdata(pdev);

	sm750fb_framebuffer_release(sm750_dev);
	arch_phys_wc_del(sm750_dev->mtrr.vram);

	iounmap(sm750_dev->pvReg);
	iounmap(sm750_dev->pvMem);
	kfree(g_settings);
}

static int __init lynxfb_setup(char *options)
{
	int len;
	char *opt, *tmp;

	if (!options || !*options) {
		pr_warn("no options.\n");
		return 0;
	}

	pr_info("options:%s\n", options);

	len = strlen(options) + 1;
	g_settings = kzalloc(len, GFP_KERNEL);
	if (!g_settings)
		return -ENOMEM;

	tmp = g_settings;

	/*
	 * Notes:
	 * char * strsep(char **s,const char * ct);
	 * @s: the string to be searched
	 * @ct :the characters to search for
	 *
	 * strsep() updates @options to pointer after the first found token
	 * it also returns the pointer ahead the token.
	 */
	while ((opt = strsep(&options, ":")) != NULL) {
		/* options that mean for any lynx chips are configured here */
		if (!strncmp(opt, "noaccel", strlen("noaccel"))) {
			g_noaccel = 1;
		} else if (!strncmp(opt, "nomtrr", strlen("nomtrr"))) {
			g_nomtrr = 1;
		} else if (!strncmp(opt, "dual", strlen("dual"))) {
			g_dualview = 1;
		} else {
			strcat(tmp, opt);
			tmp += strlen(opt);
			if (options)
				*tmp++ = ':';
			else
				*tmp++ = 0;
		}
	}

	/* misc g_settings are transport to chip specific routines */
	pr_info("parameter left for chip specific analysis:%s\n", g_settings);
	return 0;
}

static const struct pci_device_id smi_pci_table[] = {
	{ PCI_DEVICE(0x126f, 0x0750), },
	{0,}
};

MODULE_DEVICE_TABLE(pci, smi_pci_table);

static SIMPLE_DEV_PM_OPS(lynxfb_pm_ops, lynxfb_suspend, lynxfb_resume);

static struct pci_driver lynxfb_driver = {
	.name =		"sm750fb",
	.id_table =	smi_pci_table,
	.probe =	lynxfb_pci_probe,
	.remove =	lynxfb_pci_remove,
	.driver.pm =	&lynxfb_pm_ops,
};

static int __init lynxfb_init(void)
{
	char *option;

	if (fb_modesetting_disabled("sm750fb"))
		return -ENODEV;

#ifdef MODULE
	option = g_option;
#else
	if (fb_get_options("sm750fb", &option))
		return -ENODEV;
#endif

	lynxfb_setup(option);
	return pci_register_driver(&lynxfb_driver);
}
module_init(lynxfb_init);

static void __exit lynxfb_exit(void)
{
	pci_unregister_driver(&lynxfb_driver);
}
module_exit(lynxfb_exit);

module_param(g_option, charp, 0444);

MODULE_PARM_DESC(g_option,
		 "\n\t\tCommon options:\n"
		 "\t\tnoaccel:disable 2d capabilities\n"
		 "\t\tnomtrr:disable MTRR attribute for video memory\n"
		 "\t\tdualview:dual frame buffer feature enabled\n"
		 "\t\tnohwc:disable hardware cursor\n"
		 "\t\tUsual example:\n"
		 "\t\tinsmod ./sm750fb.ko g_option=\"noaccel,nohwc,1280x1024-8@60\"\n"
		 );

MODULE_AUTHOR("monk liu <monk.liu@siliconmotion.com>");
MODULE_AUTHOR("Sudip Mukherjee <sudip@vectorindia.org>");
MODULE_DESCRIPTION("Frame buffer driver for SM750 chipset");
MODULE_LICENSE("Dual BSD/GPL");
