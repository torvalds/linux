/*
 * linux/drivers/video/w100fb.c
 *
 * Frame Buffer Device for ATI Imageon w100 (Wallaby)
 *
 * Copyright (C) 2002, ATI Corp.
 * Copyright (C) 2004-2006 Richard Purdie
 * Copyright (c) 2005 Ian Molton
 * Copyright (c) 2006 Alberto Mardegan
 *
 * Rewritten for 2.6 by Richard Purdie <rpurdie@rpsys.net>
 *
 * Generic platform support by Ian Molton <spyro@f2s.com>
 * and Richard Purdie <rpurdie@rpsys.net>
 *
 * w32xx support by Ian Molton
 *
 * Hardware acceleration support by Alberto Mardegan
 * <mardy@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <video/w100fb.h>
#include "w100fb.h"

/*
 * Prototypes
 */
static void w100_suspend(u32 mode);
static void w100_vsync(void);
static void w100_hw_init(struct w100fb_par*);
static void w100_pwm_setup(struct w100fb_par*);
static void w100_init_clocks(struct w100fb_par*);
static void w100_setup_memory(struct w100fb_par*);
static void w100_init_lcd(struct w100fb_par*);
static void w100_set_dispregs(struct w100fb_par*);
static void w100_update_enable(void);
static void w100_update_disable(void);
static void calc_hsync(struct w100fb_par *par);
static void w100_init_graphic_engine(struct w100fb_par *par);
struct w100_pll_info *w100_get_xtal_table(unsigned int freq);

/* Pseudo palette size */
#define MAX_PALETTES      16

#define W100_SUSPEND_EXTMEM 0
#define W100_SUSPEND_ALL    1

#define BITS_PER_PIXEL    16

/* Remapped addresses for base cfg, memmapped regs and the frame buffer itself */
static void *remapped_base;
static void *remapped_regs;
static void *remapped_fbuf;

#define REMAPPED_FB_LEN   0x15ffff

/* This is the offset in the w100's address space we map the current
   framebuffer memory to. We use the position of external memory as
   we can remap internal memory to there if external isn't present. */
#define W100_FB_BASE MEM_EXT_BASE_VALUE


/*
 * Sysfs functions
 */
static ssize_t flip_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct w100fb_par *par=info->par;

	return sprintf(buf, "%d\n",par->flip);
}

static ssize_t flip_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int flip;
	struct fb_info *info = dev_get_drvdata(dev);
	struct w100fb_par *par=info->par;

	flip = simple_strtoul(buf, NULL, 10);

	if (flip > 0)
		par->flip = 1;
	else
		par->flip = 0;

	w100_update_disable();
	w100_set_dispregs(par);
	w100_update_enable();

	calc_hsync(par);

	return count;
}

static DEVICE_ATTR(flip, 0644, flip_show, flip_store);

static ssize_t w100fb_reg_read(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long regs, param;
	regs = simple_strtoul(buf, NULL, 16);
	param = readl(remapped_regs + regs);
	printk("Read Register 0x%08lX: 0x%08lX\n", regs, param);
	return count;
}

static DEVICE_ATTR(reg_read, 0200, NULL, w100fb_reg_read);

static ssize_t w100fb_reg_write(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long regs, param;
	sscanf(buf, "%lx %lx", &regs, &param);

	if (regs <= 0x2000) {
		printk("Write Register 0x%08lX: 0x%08lX\n", regs, param);
		writel(param, remapped_regs + regs);
	}

	return count;
}

static DEVICE_ATTR(reg_write, 0200, NULL, w100fb_reg_write);


static ssize_t fastpllclk_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct w100fb_par *par=info->par;

	return sprintf(buf, "%d\n",par->fastpll_mode);
}

static ssize_t fastpllclk_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct w100fb_par *par=info->par;

	if (simple_strtoul(buf, NULL, 10) > 0) {
		par->fastpll_mode=1;
		printk("w100fb: Using fast system clock (if possible)\n");
	} else {
		par->fastpll_mode=0;
		printk("w100fb: Using normal system clock\n");
	}

	w100_init_clocks(par);
	calc_hsync(par);

	return count;
}

static DEVICE_ATTR(fastpllclk, 0644, fastpllclk_show, fastpllclk_store);

/*
 * Some touchscreens need hsync information from the video driver to
 * function correctly. We export it here.
 */
unsigned long w100fb_get_hsynclen(struct device *dev)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct w100fb_par *par=info->par;

	/* If display is blanked/suspended, hsync isn't active */
	if (par->blanked)
		return 0;
	else
		return par->hsync_len;
}
EXPORT_SYMBOL(w100fb_get_hsynclen);

static void w100fb_clear_screen(struct w100fb_par *par)
{
	memset_io(remapped_fbuf + (W100_FB_BASE-MEM_WINDOW_BASE), 0, (par->xres * par->yres * BITS_PER_PIXEL/8));
}


/*
 * Set a palette value from rgb components
 */
static int w100fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			     u_int trans, struct fb_info *info)
{
	unsigned int val;
	int ret = 1;

	/*
	 * If greyscale is true, then we convert the RGB value
	 * to greyscale no matter what visual we are using.
	 */
	if (info->var.grayscale)
		red = green = blue = (19595 * red + 38470 * green + 7471 * blue) >> 16;

	/*
	 * 16-bit True Colour.  We encode the RGB value
	 * according to the RGB bitfield information.
	 */
	if (regno < MAX_PALETTES) {
		u32 *pal = info->pseudo_palette;

		val = (red & 0xf800) | ((green & 0xfc00) >> 5) | ((blue & 0xf800) >> 11);
		pal[regno] = val;
		ret = 0;
	}
	return ret;
}


/*
 * Blank the display based on value in blank_mode
 */
static int w100fb_blank(int blank_mode, struct fb_info *info)
{
	struct w100fb_par *par = info->par;
	struct w100_tg_info *tg = par->mach->tg;

	switch(blank_mode) {

 	case FB_BLANK_NORMAL:         /* Normal blanking */
	case FB_BLANK_VSYNC_SUSPEND:  /* VESA blank (vsync off) */
	case FB_BLANK_HSYNC_SUSPEND:  /* VESA blank (hsync off) */
 	case FB_BLANK_POWERDOWN:      /* Poweroff */
  		if (par->blanked == 0) {
			if(tg && tg->suspend)
				tg->suspend(par);
			par->blanked = 1;
  		}
  		break;

 	case FB_BLANK_UNBLANK: /* Unblanking */
  		if (par->blanked != 0) {
			if(tg && tg->resume)
				tg->resume(par);
			par->blanked = 0;
  		}
  		break;
 	}
	return 0;
}


static void w100_fifo_wait(int entries)
{
	union rbbm_status_u status;
	int i;

	for (i = 0; i < 2000000; i++) {
		status.val = readl(remapped_regs + mmRBBM_STATUS);
		if (status.f.cmdfifo_avail >= entries)
			return;
		udelay(1);
	}
	printk(KERN_ERR "w100fb: FIFO Timeout!\n");
}


static int w100fb_sync(struct fb_info *info)
{
	union rbbm_status_u status;
	int i;

	for (i = 0; i < 2000000; i++) {
		status.val = readl(remapped_regs + mmRBBM_STATUS);
		if (!status.f.gui_active)
			return 0;
		udelay(1);
	}
	printk(KERN_ERR "w100fb: Graphic engine timeout!\n");
	return -EBUSY;
}


static void w100_init_graphic_engine(struct w100fb_par *par)
{
	union dp_gui_master_cntl_u gmc;
	union dp_mix_u dp_mix;
	union dp_datatype_u dp_datatype;
	union dp_cntl_u dp_cntl;

	w100_fifo_wait(4);
	writel(W100_FB_BASE, remapped_regs + mmDST_OFFSET);
	writel(par->xres, remapped_regs + mmDST_PITCH);
	writel(W100_FB_BASE, remapped_regs + mmSRC_OFFSET);
	writel(par->xres, remapped_regs + mmSRC_PITCH);

	w100_fifo_wait(3);
	writel(0, remapped_regs + mmSC_TOP_LEFT);
	writel((par->yres << 16) | par->xres, remapped_regs + mmSC_BOTTOM_RIGHT);
	writel(0x1fff1fff, remapped_regs + mmSRC_SC_BOTTOM_RIGHT);

	w100_fifo_wait(4);
	dp_cntl.val = 0;
	dp_cntl.f.dst_x_dir = 1;
	dp_cntl.f.dst_y_dir = 1;
	dp_cntl.f.src_x_dir = 1;
	dp_cntl.f.src_y_dir = 1;
	dp_cntl.f.dst_major_x = 1;
	dp_cntl.f.src_major_x = 1;
	writel(dp_cntl.val, remapped_regs + mmDP_CNTL);

	gmc.val = 0;
	gmc.f.gmc_src_pitch_offset_cntl = 1;
	gmc.f.gmc_dst_pitch_offset_cntl = 1;
	gmc.f.gmc_src_clipping = 1;
	gmc.f.gmc_dst_clipping = 1;
	gmc.f.gmc_brush_datatype = GMC_BRUSH_NONE;
	gmc.f.gmc_dst_datatype = 3; /* from DstType_16Bpp_444 */
	gmc.f.gmc_src_datatype = SRC_DATATYPE_EQU_DST;
	gmc.f.gmc_byte_pix_order = 1;
	gmc.f.gmc_default_sel = 0;
	gmc.f.gmc_rop3 = ROP3_SRCCOPY;
	gmc.f.gmc_dp_src_source = DP_SRC_MEM_RECTANGULAR;
	gmc.f.gmc_clr_cmp_fcn_dis = 1;
	gmc.f.gmc_wr_msk_dis = 1;
	gmc.f.gmc_dp_op = DP_OP_ROP;
	writel(gmc.val, remapped_regs + mmDP_GUI_MASTER_CNTL);

	dp_datatype.val = dp_mix.val = 0;
	dp_datatype.f.dp_dst_datatype = gmc.f.gmc_dst_datatype;
	dp_datatype.f.dp_brush_datatype = gmc.f.gmc_brush_datatype;
	dp_datatype.f.dp_src2_type = 0;
	dp_datatype.f.dp_src2_datatype = gmc.f.gmc_src_datatype;
	dp_datatype.f.dp_src_datatype = gmc.f.gmc_src_datatype;
	dp_datatype.f.dp_byte_pix_order = gmc.f.gmc_byte_pix_order;
	writel(dp_datatype.val, remapped_regs + mmDP_DATATYPE);

	dp_mix.f.dp_src_source = gmc.f.gmc_dp_src_source;
	dp_mix.f.dp_src2_source = 1;
	dp_mix.f.dp_rop3 = gmc.f.gmc_rop3;
	dp_mix.f.dp_op = gmc.f.gmc_dp_op;
	writel(dp_mix.val, remapped_regs + mmDP_MIX);
}


static void w100fb_fillrect(struct fb_info *info,
                            const struct fb_fillrect *rect)
{
	union dp_gui_master_cntl_u gmc;

	if (info->state != FBINFO_STATE_RUNNING)
		return;
	if (info->flags & FBINFO_HWACCEL_DISABLED) {
		cfb_fillrect(info, rect);
		return;
	}

	gmc.val = readl(remapped_regs + mmDP_GUI_MASTER_CNTL);
	gmc.f.gmc_rop3 = ROP3_PATCOPY;
	gmc.f.gmc_brush_datatype = GMC_BRUSH_SOLID_COLOR;
	w100_fifo_wait(2);
	writel(gmc.val, remapped_regs + mmDP_GUI_MASTER_CNTL);
	writel(rect->color, remapped_regs + mmDP_BRUSH_FRGD_CLR);

	w100_fifo_wait(2);
	writel((rect->dy << 16) | (rect->dx & 0xffff), remapped_regs + mmDST_Y_X);
	writel((rect->width << 16) | (rect->height & 0xffff),
	       remapped_regs + mmDST_WIDTH_HEIGHT);
}


static void w100fb_copyarea(struct fb_info *info,
                            const struct fb_copyarea *area)
{
	u32 dx = area->dx, dy = area->dy, sx = area->sx, sy = area->sy;
	u32 h = area->height, w = area->width;
	union dp_gui_master_cntl_u gmc;

	if (info->state != FBINFO_STATE_RUNNING)
		return;
	if (info->flags & FBINFO_HWACCEL_DISABLED) {
		cfb_copyarea(info, area);
		return;
	}

	gmc.val = readl(remapped_regs + mmDP_GUI_MASTER_CNTL);
	gmc.f.gmc_rop3 = ROP3_SRCCOPY;
	gmc.f.gmc_brush_datatype = GMC_BRUSH_NONE;
	w100_fifo_wait(1);
	writel(gmc.val, remapped_regs + mmDP_GUI_MASTER_CNTL);

	w100_fifo_wait(3);
	writel((sy << 16) | (sx & 0xffff), remapped_regs + mmSRC_Y_X);
	writel((dy << 16) | (dx & 0xffff), remapped_regs + mmDST_Y_X);
	writel((w << 16) | (h & 0xffff), remapped_regs + mmDST_WIDTH_HEIGHT);
}


/*
 *  Change the resolution by calling the appropriate hardware functions
 */
static void w100fb_activate_var(struct w100fb_par *par)
{
	struct w100_tg_info *tg = par->mach->tg;

	w100_pwm_setup(par);
	w100_setup_memory(par);
	w100_init_clocks(par);
	w100fb_clear_screen(par);
	w100_vsync();

	w100_update_disable();
	w100_init_lcd(par);
	w100_set_dispregs(par);
	w100_update_enable();
	w100_init_graphic_engine(par);

	calc_hsync(par);

	if (!par->blanked && tg && tg->change)
		tg->change(par);
}


/* Select the smallest mode that allows the desired resolution to be
 * displayed. If desired, the x and y parameters can be rounded up to
 * match the selected mode.
 */
static struct w100_mode *w100fb_get_mode(struct w100fb_par *par, unsigned int *x, unsigned int *y, int saveval)
{
	struct w100_mode *mode = NULL;
	struct w100_mode *modelist = par->mach->modelist;
	unsigned int best_x = 0xffffffff, best_y = 0xffffffff;
	unsigned int i;

	for (i = 0 ; i < par->mach->num_modes ; i++) {
		if (modelist[i].xres >= *x && modelist[i].yres >= *y &&
				modelist[i].xres < best_x && modelist[i].yres < best_y) {
			best_x = modelist[i].xres;
			best_y = modelist[i].yres;
			mode = &modelist[i];
		} else if(modelist[i].xres >= *y && modelist[i].yres >= *x &&
		        modelist[i].xres < best_y && modelist[i].yres < best_x) {
			best_x = modelist[i].yres;
			best_y = modelist[i].xres;
			mode = &modelist[i];
		}
	}

	if (mode && saveval) {
		*x = best_x;
		*y = best_y;
	}

	return mode;
}


/*
 *  w100fb_check_var():
 *  Get the video params out of 'var'. If a value doesn't fit, round it up,
 *  if it's too big, return -EINVAL.
 */
static int w100fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct w100fb_par *par=info->par;

	if(!w100fb_get_mode(par, &var->xres, &var->yres, 1))
		return -EINVAL;

	if (par->mach->mem && ((var->xres*var->yres*BITS_PER_PIXEL/8) > (par->mach->mem->size+1)))
		return -EINVAL;

	if (!par->mach->mem && ((var->xres*var->yres*BITS_PER_PIXEL/8) > (MEM_INT_SIZE+1)))
		return -EINVAL;

	var->xres_virtual = max(var->xres_virtual, var->xres);
	var->yres_virtual = max(var->yres_virtual, var->yres);

	if (var->bits_per_pixel > BITS_PER_PIXEL)
		return -EINVAL;
	else
		var->bits_per_pixel = BITS_PER_PIXEL;

	var->red.offset = 11;
	var->red.length = 5;
	var->green.offset = 5;
	var->green.length = 6;
	var->blue.offset = 0;
	var->blue.length = 5;
	var->transp.offset = var->transp.length = 0;

	var->nonstd = 0;
	var->height = -1;
	var->width = -1;
	var->vmode = FB_VMODE_NONINTERLACED;
	var->sync = 0;
	var->pixclock = 0x04;  /* 171521; */

	return 0;
}


/*
 * w100fb_set_par():
 *	Set the user defined part of the display for the specified console
 *  by looking at the values in info.var
 */
static int w100fb_set_par(struct fb_info *info)
{
	struct w100fb_par *par=info->par;

	if (par->xres != info->var.xres || par->yres != info->var.yres)	{
		par->xres = info->var.xres;
		par->yres = info->var.yres;
		par->mode = w100fb_get_mode(par, &par->xres, &par->yres, 0);

		info->fix.visual = FB_VISUAL_TRUECOLOR;
		info->fix.ypanstep = 0;
		info->fix.ywrapstep = 0;
		info->fix.line_length = par->xres * BITS_PER_PIXEL / 8;

		if ((par->xres*par->yres*BITS_PER_PIXEL/8) > (MEM_INT_SIZE+1)) {
			par->extmem_active = 1;
			info->fix.smem_len = par->mach->mem->size+1;
		} else {
			par->extmem_active = 0;
			info->fix.smem_len = MEM_INT_SIZE+1;
		}

		w100fb_activate_var(par);
	}
	return 0;
}


/*
 *  Frame buffer operations
 */
static struct fb_ops w100fb_ops = {
	.owner        = THIS_MODULE,
	.fb_check_var = w100fb_check_var,
	.fb_set_par   = w100fb_set_par,
	.fb_setcolreg = w100fb_setcolreg,
	.fb_blank     = w100fb_blank,
	.fb_fillrect  = w100fb_fillrect,
	.fb_copyarea  = w100fb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_sync      = w100fb_sync,
};

#ifdef CONFIG_PM
static void w100fb_save_vidmem(struct w100fb_par *par)
{
	int memsize;

	if (par->extmem_active) {
		memsize=par->mach->mem->size;
		par->saved_extmem = vmalloc(memsize);
		if (par->saved_extmem)
			memcpy_fromio(par->saved_extmem, remapped_fbuf + (W100_FB_BASE-MEM_WINDOW_BASE), memsize);
	}
	memsize=MEM_INT_SIZE;
	par->saved_intmem = vmalloc(memsize);
	if (par->saved_intmem && par->extmem_active)
		memcpy_fromio(par->saved_intmem, remapped_fbuf + (W100_FB_BASE-MEM_INT_BASE_VALUE), memsize);
	else if (par->saved_intmem)
		memcpy_fromio(par->saved_intmem, remapped_fbuf + (W100_FB_BASE-MEM_WINDOW_BASE), memsize);
}

static void w100fb_restore_vidmem(struct w100fb_par *par)
{
	int memsize;

	if (par->extmem_active && par->saved_extmem) {
		memsize=par->mach->mem->size;
		memcpy_toio(remapped_fbuf + (W100_FB_BASE-MEM_WINDOW_BASE), par->saved_extmem, memsize);
		vfree(par->saved_extmem);
	}
	if (par->saved_intmem) {
		memsize=MEM_INT_SIZE;
		if (par->extmem_active)
			memcpy_toio(remapped_fbuf + (W100_FB_BASE-MEM_INT_BASE_VALUE), par->saved_intmem, memsize);
		else
			memcpy_toio(remapped_fbuf + (W100_FB_BASE-MEM_WINDOW_BASE), par->saved_intmem, memsize);
		vfree(par->saved_intmem);
	}
}

static int w100fb_suspend(struct platform_device *dev, pm_message_t state)
{
	struct fb_info *info = platform_get_drvdata(dev);
	struct w100fb_par *par=info->par;
	struct w100_tg_info *tg = par->mach->tg;

	w100fb_save_vidmem(par);
	if(tg && tg->suspend)
		tg->suspend(par);
	w100_suspend(W100_SUSPEND_ALL);
	par->blanked = 1;

	return 0;
}

static int w100fb_resume(struct platform_device *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);
	struct w100fb_par *par=info->par;
	struct w100_tg_info *tg = par->mach->tg;

	w100_hw_init(par);
	w100fb_activate_var(par);
	w100fb_restore_vidmem(par);
	if(tg && tg->resume)
		tg->resume(par);
	par->blanked = 0;

	return 0;
}
#else
#define w100fb_suspend  NULL
#define w100fb_resume   NULL
#endif


int __init w100fb_probe(struct platform_device *pdev)
{
	int err = -EIO;
	struct w100fb_mach_info *inf;
	struct fb_info *info = NULL;
	struct w100fb_par *par;
	struct resource *mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	unsigned int chip_id;

	if (!mem)
		return -EINVAL;

	/* Remap the chip base address */
	remapped_base = ioremap_nocache(mem->start+W100_CFG_BASE, W100_CFG_LEN);
	if (remapped_base == NULL)
		goto out;

	/* Map the register space */
	remapped_regs = ioremap_nocache(mem->start+W100_REG_BASE, W100_REG_LEN);
	if (remapped_regs == NULL)
		goto out;

	/* Identify the chip */
	printk("Found ");
	chip_id = readl(remapped_regs + mmCHIP_ID);
	switch(chip_id) {
		case CHIP_ID_W100:  printk("w100");  break;
		case CHIP_ID_W3200: printk("w3200"); break;
		case CHIP_ID_W3220: printk("w3220"); break;
		default:
			printk("Unknown imageon chip ID\n");
			err = -ENODEV;
			goto out;
	}
	printk(" at 0x%08lx.\n", (unsigned long) mem->start+W100_CFG_BASE);

	/* Remap the framebuffer */
	remapped_fbuf = ioremap_nocache(mem->start+MEM_WINDOW_BASE, MEM_WINDOW_SIZE);
	if (remapped_fbuf == NULL)
		goto out;

	info=framebuffer_alloc(sizeof(struct w100fb_par), &pdev->dev);
	if (!info) {
		err = -ENOMEM;
		goto out;
	}

	par = info->par;
	platform_set_drvdata(pdev, info);

	inf = pdev->dev.platform_data;
	par->chip_id = chip_id;
	par->mach = inf;
	par->fastpll_mode = 0;
	par->blanked = 0;

	par->pll_table=w100_get_xtal_table(inf->xtal_freq);
	if (!par->pll_table) {
		printk(KERN_ERR "No matching Xtal definition found\n");
		err = -EINVAL;
		goto out;
	}

	info->pseudo_palette = kmalloc(sizeof (u32) * MAX_PALETTES, GFP_KERNEL);
	if (!info->pseudo_palette) {
		err = -ENOMEM;
		goto out;
	}

	info->fbops = &w100fb_ops;
	info->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_COPYAREA |
		FBINFO_HWACCEL_FILLRECT;
	info->node = -1;
	info->screen_base = remapped_fbuf + (W100_FB_BASE-MEM_WINDOW_BASE);
	info->screen_size = REMAPPED_FB_LEN;

	strcpy(info->fix.id, "w100fb");
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.type_aux = 0;
	info->fix.accel = FB_ACCEL_NONE;
	info->fix.smem_start = mem->start+W100_FB_BASE;
	info->fix.mmio_start = mem->start+W100_REG_BASE;
	info->fix.mmio_len = W100_REG_LEN;

	if (fb_alloc_cmap(&info->cmap, 256, 0) < 0) {
		err = -ENOMEM;
		goto out;
	}

	par->mode = &inf->modelist[0];
	if(inf->init_mode & INIT_MODE_ROTATED) {
		info->var.xres = par->mode->yres;
		info->var.yres = par->mode->xres;
	}
	else {
		info->var.xres = par->mode->xres;
		info->var.yres = par->mode->yres;
	}

	if(inf->init_mode &= INIT_MODE_FLIPPED)
		par->flip = 1;
	else
		par->flip = 0;

	info->var.xres_virtual = info->var.xres;
	info->var.yres_virtual = info->var.yres;
	info->var.pixclock = 0x04;  /* 171521; */
	info->var.sync = 0;
	info->var.grayscale = 0;
	info->var.xoffset = info->var.yoffset = 0;
	info->var.accel_flags = 0;
	info->var.activate = FB_ACTIVATE_NOW;

	w100_hw_init(par);

	if (w100fb_check_var(&info->var, info) < 0) {
		err = -EINVAL;
		goto out;
	}

	w100fb_set_par(info);

	if (register_framebuffer(info) < 0) {
		err = -EINVAL;
		goto out;
	}

	err = device_create_file(&pdev->dev, &dev_attr_fastpllclk);
	err |= device_create_file(&pdev->dev, &dev_attr_reg_read);
	err |= device_create_file(&pdev->dev, &dev_attr_reg_write);
	err |= device_create_file(&pdev->dev, &dev_attr_flip);

	if (err != 0)
		printk(KERN_WARNING "fb%d: failed to register attributes (%d)\n",
				info->node, err);

	printk(KERN_INFO "fb%d: %s frame buffer device\n", info->node, info->fix.id);
	return 0;
out:
	fb_dealloc_cmap(&info->cmap);
	kfree(info->pseudo_palette);
	if (remapped_fbuf != NULL)
		iounmap(remapped_fbuf);
	if (remapped_regs != NULL)
		iounmap(remapped_regs);
	if (remapped_base != NULL)
		iounmap(remapped_base);
	if (info)
		framebuffer_release(info);
	return err;
}


static int w100fb_remove(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);
	struct w100fb_par *par=info->par;

	device_remove_file(&pdev->dev, &dev_attr_fastpllclk);
	device_remove_file(&pdev->dev, &dev_attr_reg_read);
	device_remove_file(&pdev->dev, &dev_attr_reg_write);
	device_remove_file(&pdev->dev, &dev_attr_flip);

	unregister_framebuffer(info);

	vfree(par->saved_intmem);
	vfree(par->saved_extmem);
	kfree(info->pseudo_palette);
	fb_dealloc_cmap(&info->cmap);

	iounmap(remapped_base);
	iounmap(remapped_regs);
	iounmap(remapped_fbuf);

	framebuffer_release(info);

	return 0;
}


/* ------------------- chipset specific functions -------------------------- */


static void w100_soft_reset(void)
{
	u16 val = readw((u16 *) remapped_base + cfgSTATUS);
	writew(val | 0x08, (u16 *) remapped_base + cfgSTATUS);
	udelay(100);
	writew(0x00, (u16 *) remapped_base + cfgSTATUS);
	udelay(100);
}

static void w100_update_disable(void)
{
	union disp_db_buf_cntl_wr_u disp_db_buf_wr_cntl;

	/* Prevent display updates */
	disp_db_buf_wr_cntl.f.db_buf_cntl = 0x1e;
	disp_db_buf_wr_cntl.f.update_db_buf = 0;
	disp_db_buf_wr_cntl.f.en_db_buf = 0;
	writel((u32) (disp_db_buf_wr_cntl.val), remapped_regs + mmDISP_DB_BUF_CNTL);
}

static void w100_update_enable(void)
{
	union disp_db_buf_cntl_wr_u disp_db_buf_wr_cntl;

	/* Enable display updates */
	disp_db_buf_wr_cntl.f.db_buf_cntl = 0x1e;
	disp_db_buf_wr_cntl.f.update_db_buf = 1;
	disp_db_buf_wr_cntl.f.en_db_buf = 1;
	writel((u32) (disp_db_buf_wr_cntl.val), remapped_regs + mmDISP_DB_BUF_CNTL);
}

unsigned long w100fb_gpio_read(int port)
{
	unsigned long value;

	if (port==W100_GPIO_PORT_A)
		value = readl(remapped_regs + mmGPIO_DATA);
	else
		value = readl(remapped_regs + mmGPIO_DATA2);

	return value;
}

void w100fb_gpio_write(int port, unsigned long value)
{
	if (port==W100_GPIO_PORT_A)
		value = writel(value, remapped_regs + mmGPIO_DATA);
	else
		value = writel(value, remapped_regs + mmGPIO_DATA2);
}
EXPORT_SYMBOL(w100fb_gpio_read);
EXPORT_SYMBOL(w100fb_gpio_write);

/*
 * Initialization of critical w100 hardware
 */
static void w100_hw_init(struct w100fb_par *par)
{
	u32 temp32;
	union cif_cntl_u cif_cntl;
	union intf_cntl_u intf_cntl;
	union cfgreg_base_u cfgreg_base;
	union wrap_top_dir_u wrap_top_dir;
	union cif_read_dbg_u cif_read_dbg;
	union cpu_defaults_u cpu_default;
	union cif_write_dbg_u cif_write_dbg;
	union wrap_start_dir_u wrap_start_dir;
	union cif_io_u cif_io;
	struct w100_gpio_regs *gpio = par->mach->gpio;

	w100_soft_reset();

	/* This is what the fpga_init code does on reset. May be wrong
	   but there is little info available */
	writel(0x31, remapped_regs + mmSCRATCH_UMSK);
	for (temp32 = 0; temp32 < 10000; temp32++)
		readl(remapped_regs + mmSCRATCH_UMSK);
	writel(0x30, remapped_regs + mmSCRATCH_UMSK);

	/* Set up CIF */
	cif_io.val = defCIF_IO;
	writel((u32)(cif_io.val), remapped_regs + mmCIF_IO);

	cif_write_dbg.val = readl(remapped_regs + mmCIF_WRITE_DBG);
	cif_write_dbg.f.dis_packer_ful_during_rbbm_timeout = 0;
	cif_write_dbg.f.en_dword_split_to_rbbm = 1;
	cif_write_dbg.f.dis_timeout_during_rbbm = 1;
	writel((u32) (cif_write_dbg.val), remapped_regs + mmCIF_WRITE_DBG);

	cif_read_dbg.val = readl(remapped_regs + mmCIF_READ_DBG);
	cif_read_dbg.f.dis_rd_same_byte_to_trig_fetch = 1;
	writel((u32) (cif_read_dbg.val), remapped_regs + mmCIF_READ_DBG);

	cif_cntl.val = readl(remapped_regs + mmCIF_CNTL);
	cif_cntl.f.dis_system_bits = 1;
	cif_cntl.f.dis_mr = 1;
	cif_cntl.f.en_wait_to_compensate_dq_prop_dly = 0;
	cif_cntl.f.intb_oe = 1;
	cif_cntl.f.interrupt_active_high = 1;
	writel((u32) (cif_cntl.val), remapped_regs + mmCIF_CNTL);

	/* Setup cfgINTF_CNTL and cfgCPU defaults */
	intf_cntl.val = defINTF_CNTL;
	intf_cntl.f.ad_inc_a = 1;
	intf_cntl.f.ad_inc_b = 1;
	intf_cntl.f.rd_data_rdy_a = 0;
	intf_cntl.f.rd_data_rdy_b = 0;
	writeb((u8) (intf_cntl.val), remapped_base + cfgINTF_CNTL);

	cpu_default.val = defCPU_DEFAULTS;
	cpu_default.f.access_ind_addr_a = 1;
	cpu_default.f.access_ind_addr_b = 1;
	cpu_default.f.access_scratch_reg = 1;
	cpu_default.f.transition_size = 0;
	writeb((u8) (cpu_default.val), remapped_base + cfgCPU_DEFAULTS);

	/* set up the apertures */
	writeb((u8) (W100_REG_BASE >> 16), remapped_base + cfgREG_BASE);

	cfgreg_base.val = defCFGREG_BASE;
	cfgreg_base.f.cfgreg_base = W100_CFG_BASE;
	writel((u32) (cfgreg_base.val), remapped_regs + mmCFGREG_BASE);

	wrap_start_dir.val = defWRAP_START_DIR;
	wrap_start_dir.f.start_addr = WRAP_BUF_BASE_VALUE >> 1;
	writel((u32) (wrap_start_dir.val), remapped_regs + mmWRAP_START_DIR);

	wrap_top_dir.val = defWRAP_TOP_DIR;
	wrap_top_dir.f.top_addr = WRAP_BUF_TOP_VALUE >> 1;
	writel((u32) (wrap_top_dir.val), remapped_regs + mmWRAP_TOP_DIR);

	writel((u32) 0x2440, remapped_regs + mmRBBM_CNTL);

	/* Set the hardware to 565 colour */
	temp32 = readl(remapped_regs + mmDISP_DEBUG2);
	temp32 &= 0xff7fffff;
	temp32 |= 0x00800000;
	writel(temp32, remapped_regs + mmDISP_DEBUG2);

	/* Initialise the GPIO lines */
	if (gpio) {
		writel(gpio->init_data1, remapped_regs + mmGPIO_DATA);
		writel(gpio->init_data2, remapped_regs + mmGPIO_DATA2);
		writel(gpio->gpio_dir1,  remapped_regs + mmGPIO_CNTL1);
		writel(gpio->gpio_oe1,   remapped_regs + mmGPIO_CNTL2);
		writel(gpio->gpio_dir2,  remapped_regs + mmGPIO_CNTL3);
		writel(gpio->gpio_oe2,   remapped_regs + mmGPIO_CNTL4);
	}
}


struct power_state {
	union clk_pin_cntl_u clk_pin_cntl;
	union pll_ref_fb_div_u pll_ref_fb_div;
	union pll_cntl_u pll_cntl;
	union sclk_cntl_u sclk_cntl;
	union pclk_cntl_u pclk_cntl;
	union pwrmgt_cntl_u pwrmgt_cntl;
	int auto_mode;  /* system clock auto changing? */
};


static struct power_state w100_pwr_state;

/* The PLL Fout is determined by (XtalFreq/(M+1)) * ((N_int+1) + (N_fac/8)) */

/* 12.5MHz Crystal PLL Table */
static struct w100_pll_info xtal_12500000[] = {
	/*freq     M   N_int    N_fac  tfgoal  lock_time */
	{ 50,      0,   1,       0,     0xe0,        56},  /*  50.00 MHz */
	{ 75,      0,   5,       0,     0xde,        37},  /*  75.00 MHz */
	{100,      0,   7,       0,     0xe0,        28},  /* 100.00 MHz */
	{125,      0,   9,       0,     0xe0,        22},  /* 125.00 MHz */
	{150,      0,   11,      0,     0xe0,        17},  /* 150.00 MHz */
	{  0,      0,   0,       0,        0,         0},  /* Terminator */
};

/* 14.318MHz Crystal PLL Table */
static struct w100_pll_info xtal_14318000[] = {
	/*freq     M   N_int    N_fac  tfgoal  lock_time */
	{ 40,      4,   13,      0,     0xe0,        80}, /* tfgoal guessed */
	{ 50,      1,   6,       0,     0xe0,	     64}, /*  50.05 MHz */
	{ 57,      2,   11,      0,     0xe0,        53}, /* tfgoal guessed */
	{ 75,      0,   4,       3,     0xe0,	     43}, /*  75.08 MHz */
	{100,      0,   6,       0,     0xe0,        32}, /* 100.10 MHz */
	{  0,      0,   0,       0,        0,         0},
};

/* 16MHz Crystal PLL Table */
static struct w100_pll_info xtal_16000000[] = {
	/*freq     M   N_int    N_fac  tfgoal  lock_time */
	{ 72,      1,   8,       0,     0xe0,        48}, /* tfgoal guessed */
	{ 95,      1,   10,      7,     0xe0,        38}, /* tfgoal guessed */
	{ 96,      1,   11,      0,     0xe0,        36}, /* tfgoal guessed */
	{  0,      0,   0,       0,        0,         0},
};

static struct pll_entries {
	int xtal_freq;
	struct w100_pll_info *pll_table;
} w100_pll_tables[] = {
	{ 12500000, &xtal_12500000[0] },
	{ 14318000, &xtal_14318000[0] },
	{ 16000000, &xtal_16000000[0] },
	{ 0 },
};

struct w100_pll_info *w100_get_xtal_table(unsigned int freq)
{
	struct pll_entries *pll_entry = w100_pll_tables;

	do {
		if (freq == pll_entry->xtal_freq)
			return pll_entry->pll_table;
		pll_entry++;
	} while (pll_entry->xtal_freq);
	return 0;
}


static unsigned int w100_get_testcount(unsigned int testclk_sel)
{
	union clk_test_cntl_u clk_test_cntl;

	udelay(5);

	/* Select the test clock source and reset */
	clk_test_cntl.f.start_check_freq = 0x0;
	clk_test_cntl.f.testclk_sel = testclk_sel;
	clk_test_cntl.f.tstcount_rst = 0x1; /* set reset */
	writel((u32) (clk_test_cntl.val), remapped_regs + mmCLK_TEST_CNTL);

	clk_test_cntl.f.tstcount_rst = 0x0; /* clear reset */
	writel((u32) (clk_test_cntl.val), remapped_regs + mmCLK_TEST_CNTL);

	/* Run clock test */
	clk_test_cntl.f.start_check_freq = 0x1;
	writel((u32) (clk_test_cntl.val), remapped_regs + mmCLK_TEST_CNTL);

	/* Give the test time to complete */
	udelay(20);

	/* Return the result */
	clk_test_cntl.val = readl(remapped_regs + mmCLK_TEST_CNTL);
	clk_test_cntl.f.start_check_freq = 0x0;
	writel((u32) (clk_test_cntl.val), remapped_regs + mmCLK_TEST_CNTL);

	return clk_test_cntl.f.test_count;
}


static int w100_pll_adjust(struct w100_pll_info *pll)
{
	unsigned int tf80;
	unsigned int tf20;

	/* Initial Settings */
	w100_pwr_state.pll_cntl.f.pll_pwdn = 0x0;     /* power down */
	w100_pwr_state.pll_cntl.f.pll_reset = 0x0;    /* not reset */
	w100_pwr_state.pll_cntl.f.pll_tcpoff = 0x1;   /* Hi-Z */
	w100_pwr_state.pll_cntl.f.pll_pvg = 0x0;      /* VCO gain = 0 */
	w100_pwr_state.pll_cntl.f.pll_vcofr = 0x0;    /* VCO frequency range control = off */
	w100_pwr_state.pll_cntl.f.pll_ioffset = 0x0;  /* current offset inside VCO = 0 */
	w100_pwr_state.pll_cntl.f.pll_ring_off = 0x0;

	/* Wai Ming 80 percent of VDD 1.3V gives 1.04V, minimum operating voltage is 1.08V
	 * therefore, commented out the following lines
	 * tf80 meant tf100
	 */
	do {
		/* set VCO input = 0.8 * VDD */
		w100_pwr_state.pll_cntl.f.pll_dactal = 0xd;
		writel((u32) (w100_pwr_state.pll_cntl.val), remapped_regs + mmPLL_CNTL);

		tf80 = w100_get_testcount(TESTCLK_SRC_PLL);
		if (tf80 >= (pll->tfgoal)) {
			/* set VCO input = 0.2 * VDD */
			w100_pwr_state.pll_cntl.f.pll_dactal = 0x7;
			writel((u32) (w100_pwr_state.pll_cntl.val), remapped_regs + mmPLL_CNTL);

			tf20 = w100_get_testcount(TESTCLK_SRC_PLL);
			if (tf20 <= (pll->tfgoal))
				return 1;  /* Success */

			if ((w100_pwr_state.pll_cntl.f.pll_vcofr == 0x0) &&
				((w100_pwr_state.pll_cntl.f.pll_pvg == 0x7) ||
				(w100_pwr_state.pll_cntl.f.pll_ioffset == 0x0))) {
				/* slow VCO config */
				w100_pwr_state.pll_cntl.f.pll_vcofr = 0x1;
				w100_pwr_state.pll_cntl.f.pll_pvg = 0x0;
				w100_pwr_state.pll_cntl.f.pll_ioffset = 0x0;
				continue;
			}
		}
		if ((w100_pwr_state.pll_cntl.f.pll_ioffset) < 0x3) {
			w100_pwr_state.pll_cntl.f.pll_ioffset += 0x1;
		} else if ((w100_pwr_state.pll_cntl.f.pll_pvg) < 0x7) {
			w100_pwr_state.pll_cntl.f.pll_ioffset = 0x0;
			w100_pwr_state.pll_cntl.f.pll_pvg += 0x1;
		} else {
			return 0;  /* Error */
		}
	} while(1);
}


/*
 * w100_pll_calibration
 */
static int w100_pll_calibration(struct w100_pll_info *pll)
{
	int status;

	status = w100_pll_adjust(pll);

	/* PLL Reset And Lock */
	/* set VCO input = 0.5 * VDD */
	w100_pwr_state.pll_cntl.f.pll_dactal = 0xa;
	writel((u32) (w100_pwr_state.pll_cntl.val), remapped_regs + mmPLL_CNTL);

	udelay(1);  /* reset time */

	/* enable charge pump */
	w100_pwr_state.pll_cntl.f.pll_tcpoff = 0x0;  /* normal */
	writel((u32) (w100_pwr_state.pll_cntl.val), remapped_regs + mmPLL_CNTL);

	/* set VCO input = Hi-Z, disable DAC */
	w100_pwr_state.pll_cntl.f.pll_dactal = 0x0;
	writel((u32) (w100_pwr_state.pll_cntl.val), remapped_regs + mmPLL_CNTL);

	udelay(400);  /* lock time */

	/* PLL locked */

	return status;
}


static int w100_pll_set_clk(struct w100_pll_info *pll)
{
	int status;

	if (w100_pwr_state.auto_mode == 1)  /* auto mode */
	{
		w100_pwr_state.pwrmgt_cntl.f.pwm_fast_noml_hw_en = 0x0;  /* disable fast to normal */
		w100_pwr_state.pwrmgt_cntl.f.pwm_noml_fast_hw_en = 0x0;  /* disable normal to fast */
		writel((u32) (w100_pwr_state.pwrmgt_cntl.val), remapped_regs + mmPWRMGT_CNTL);
	}

	/* Set system clock source to XTAL whilst adjusting the PLL! */
	w100_pwr_state.sclk_cntl.f.sclk_src_sel = CLK_SRC_XTAL;
	writel((u32) (w100_pwr_state.sclk_cntl.val), remapped_regs + mmSCLK_CNTL);

	w100_pwr_state.pll_ref_fb_div.f.pll_ref_div = pll->M;
	w100_pwr_state.pll_ref_fb_div.f.pll_fb_div_int = pll->N_int;
	w100_pwr_state.pll_ref_fb_div.f.pll_fb_div_frac = pll->N_fac;
	w100_pwr_state.pll_ref_fb_div.f.pll_lock_time = pll->lock_time;
	writel((u32) (w100_pwr_state.pll_ref_fb_div.val), remapped_regs + mmPLL_REF_FB_DIV);

	w100_pwr_state.pwrmgt_cntl.f.pwm_mode_req = 0;
	writel((u32) (w100_pwr_state.pwrmgt_cntl.val), remapped_regs + mmPWRMGT_CNTL);

	status = w100_pll_calibration(pll);

	if (w100_pwr_state.auto_mode == 1)  /* auto mode */
	{
		w100_pwr_state.pwrmgt_cntl.f.pwm_fast_noml_hw_en = 0x1;  /* reenable fast to normal */
		w100_pwr_state.pwrmgt_cntl.f.pwm_noml_fast_hw_en = 0x1;  /* reenable normal to fast  */
		writel((u32) (w100_pwr_state.pwrmgt_cntl.val), remapped_regs + mmPWRMGT_CNTL);
	}
	return status;
}

/* freq = target frequency of the PLL */
static int w100_set_pll_freq(struct w100fb_par *par, unsigned int freq)
{
	struct w100_pll_info *pll = par->pll_table;

	do {
		if (freq == pll->freq) {
			return w100_pll_set_clk(pll);
		}
		pll++;
	} while(pll->freq);
	return 0;
}

/* Set up an initial state.  Some values/fields set
   here will be overwritten. */
static void w100_pwm_setup(struct w100fb_par *par)
{
	w100_pwr_state.clk_pin_cntl.f.osc_en = 0x1;
	w100_pwr_state.clk_pin_cntl.f.osc_gain = 0x1f;
	w100_pwr_state.clk_pin_cntl.f.dont_use_xtalin = 0x0;
	w100_pwr_state.clk_pin_cntl.f.xtalin_pm_en = 0x0;
	w100_pwr_state.clk_pin_cntl.f.xtalin_dbl_en = par->mach->xtal_dbl ? 1 : 0;
	w100_pwr_state.clk_pin_cntl.f.cg_debug = 0x0;
	writel((u32) (w100_pwr_state.clk_pin_cntl.val), remapped_regs + mmCLK_PIN_CNTL);

	w100_pwr_state.sclk_cntl.f.sclk_src_sel = CLK_SRC_XTAL;
	w100_pwr_state.sclk_cntl.f.sclk_post_div_fast = 0x0;  /* Pfast = 1 */
	w100_pwr_state.sclk_cntl.f.sclk_clkon_hys = 0x3;
	w100_pwr_state.sclk_cntl.f.sclk_post_div_slow = 0x0;  /* Pslow = 1 */
	w100_pwr_state.sclk_cntl.f.disp_cg_ok2switch_en = 0x0;
	w100_pwr_state.sclk_cntl.f.sclk_force_reg = 0x0;    /* Dynamic */
	w100_pwr_state.sclk_cntl.f.sclk_force_disp = 0x0;   /* Dynamic */
	w100_pwr_state.sclk_cntl.f.sclk_force_mc = 0x0;     /* Dynamic */
	w100_pwr_state.sclk_cntl.f.sclk_force_extmc = 0x0;  /* Dynamic */
	w100_pwr_state.sclk_cntl.f.sclk_force_cp = 0x0;     /* Dynamic */
	w100_pwr_state.sclk_cntl.f.sclk_force_e2 = 0x0;     /* Dynamic */
	w100_pwr_state.sclk_cntl.f.sclk_force_e3 = 0x0;     /* Dynamic */
	w100_pwr_state.sclk_cntl.f.sclk_force_idct = 0x0;   /* Dynamic */
	w100_pwr_state.sclk_cntl.f.sclk_force_bist = 0x0;   /* Dynamic */
	w100_pwr_state.sclk_cntl.f.busy_extend_cp = 0x0;
	w100_pwr_state.sclk_cntl.f.busy_extend_e2 = 0x0;
	w100_pwr_state.sclk_cntl.f.busy_extend_e3 = 0x0;
	w100_pwr_state.sclk_cntl.f.busy_extend_idct = 0x0;
	writel((u32) (w100_pwr_state.sclk_cntl.val), remapped_regs + mmSCLK_CNTL);

	w100_pwr_state.pclk_cntl.f.pclk_src_sel = CLK_SRC_XTAL;
	w100_pwr_state.pclk_cntl.f.pclk_post_div = 0x1;    /* P = 2 */
	w100_pwr_state.pclk_cntl.f.pclk_force_disp = 0x0;  /* Dynamic */
	writel((u32) (w100_pwr_state.pclk_cntl.val), remapped_regs + mmPCLK_CNTL);

	w100_pwr_state.pll_ref_fb_div.f.pll_ref_div = 0x0;     /* M = 1 */
	w100_pwr_state.pll_ref_fb_div.f.pll_fb_div_int = 0x0;  /* N = 1.0 */
	w100_pwr_state.pll_ref_fb_div.f.pll_fb_div_frac = 0x0;
	w100_pwr_state.pll_ref_fb_div.f.pll_reset_time = 0x5;
	w100_pwr_state.pll_ref_fb_div.f.pll_lock_time = 0xff;
	writel((u32) (w100_pwr_state.pll_ref_fb_div.val), remapped_regs + mmPLL_REF_FB_DIV);

	w100_pwr_state.pll_cntl.f.pll_pwdn = 0x1;
	w100_pwr_state.pll_cntl.f.pll_reset = 0x1;
	w100_pwr_state.pll_cntl.f.pll_pm_en = 0x0;
	w100_pwr_state.pll_cntl.f.pll_mode = 0x0;  /* uses VCO clock */
	w100_pwr_state.pll_cntl.f.pll_refclk_sel = 0x0;
	w100_pwr_state.pll_cntl.f.pll_fbclk_sel = 0x0;
	w100_pwr_state.pll_cntl.f.pll_tcpoff = 0x0;
	w100_pwr_state.pll_cntl.f.pll_pcp = 0x4;
	w100_pwr_state.pll_cntl.f.pll_pvg = 0x0;
	w100_pwr_state.pll_cntl.f.pll_vcofr = 0x0;
	w100_pwr_state.pll_cntl.f.pll_ioffset = 0x0;
	w100_pwr_state.pll_cntl.f.pll_pecc_mode = 0x0;
	w100_pwr_state.pll_cntl.f.pll_pecc_scon = 0x0;
	w100_pwr_state.pll_cntl.f.pll_dactal = 0x0;  /* Hi-Z */
	w100_pwr_state.pll_cntl.f.pll_cp_clip = 0x3;
	w100_pwr_state.pll_cntl.f.pll_conf = 0x2;
	w100_pwr_state.pll_cntl.f.pll_mbctrl = 0x2;
	w100_pwr_state.pll_cntl.f.pll_ring_off = 0x0;
	writel((u32) (w100_pwr_state.pll_cntl.val), remapped_regs + mmPLL_CNTL);

	w100_pwr_state.pwrmgt_cntl.f.pwm_enable = 0x0;
	w100_pwr_state.pwrmgt_cntl.f.pwm_mode_req = 0x1;  /* normal mode (0, 1, 3) */
	w100_pwr_state.pwrmgt_cntl.f.pwm_wakeup_cond = 0x0;
	w100_pwr_state.pwrmgt_cntl.f.pwm_fast_noml_hw_en = 0x0;
	w100_pwr_state.pwrmgt_cntl.f.pwm_noml_fast_hw_en = 0x0;
	w100_pwr_state.pwrmgt_cntl.f.pwm_fast_noml_cond = 0x1;  /* PM4,ENG */
	w100_pwr_state.pwrmgt_cntl.f.pwm_noml_fast_cond = 0x1;  /* PM4,ENG */
	w100_pwr_state.pwrmgt_cntl.f.pwm_idle_timer = 0xFF;
	w100_pwr_state.pwrmgt_cntl.f.pwm_busy_timer = 0xFF;
	writel((u32) (w100_pwr_state.pwrmgt_cntl.val), remapped_regs + mmPWRMGT_CNTL);

	w100_pwr_state.auto_mode = 0;  /* manual mode */
}


/*
 * Setup the w100 clocks for the specified mode
 */
static void w100_init_clocks(struct w100fb_par *par)
{
	struct w100_mode *mode = par->mode;

	if (mode->pixclk_src == CLK_SRC_PLL || mode->sysclk_src == CLK_SRC_PLL)
		w100_set_pll_freq(par, (par->fastpll_mode && mode->fast_pll_freq) ? mode->fast_pll_freq : mode->pll_freq);

	w100_pwr_state.sclk_cntl.f.sclk_src_sel = mode->sysclk_src;
	w100_pwr_state.sclk_cntl.f.sclk_post_div_fast = mode->sysclk_divider;
	w100_pwr_state.sclk_cntl.f.sclk_post_div_slow = mode->sysclk_divider;
	writel((u32) (w100_pwr_state.sclk_cntl.val), remapped_regs + mmSCLK_CNTL);
}

static void w100_init_lcd(struct w100fb_par *par)
{
	u32 temp32;
	struct w100_mode *mode = par->mode;
	struct w100_gen_regs *regs = par->mach->regs;
	union active_h_disp_u active_h_disp;
	union active_v_disp_u active_v_disp;
	union graphic_h_disp_u graphic_h_disp;
	union graphic_v_disp_u graphic_v_disp;
	union crtc_total_u crtc_total;

	/* w3200 doesnt like undefined bits being set so zero register values first */

	active_h_disp.val = 0;
	active_h_disp.f.active_h_start=mode->left_margin;
	active_h_disp.f.active_h_end=mode->left_margin + mode->xres;
	writel(active_h_disp.val, remapped_regs + mmACTIVE_H_DISP);

	active_v_disp.val = 0;
	active_v_disp.f.active_v_start=mode->upper_margin;
	active_v_disp.f.active_v_end=mode->upper_margin + mode->yres;
	writel(active_v_disp.val, remapped_regs + mmACTIVE_V_DISP);

	graphic_h_disp.val = 0;
	graphic_h_disp.f.graphic_h_start=mode->left_margin;
	graphic_h_disp.f.graphic_h_end=mode->left_margin + mode->xres;
	writel(graphic_h_disp.val, remapped_regs + mmGRAPHIC_H_DISP);

	graphic_v_disp.val = 0;
	graphic_v_disp.f.graphic_v_start=mode->upper_margin;
	graphic_v_disp.f.graphic_v_end=mode->upper_margin + mode->yres;
	writel(graphic_v_disp.val, remapped_regs + mmGRAPHIC_V_DISP);

	crtc_total.val = 0;
	crtc_total.f.crtc_h_total=mode->left_margin  + mode->xres + mode->right_margin;
	crtc_total.f.crtc_v_total=mode->upper_margin + mode->yres + mode->lower_margin;
	writel(crtc_total.val, remapped_regs + mmCRTC_TOTAL);

	writel(mode->crtc_ss, remapped_regs + mmCRTC_SS);
	writel(mode->crtc_ls, remapped_regs + mmCRTC_LS);
	writel(mode->crtc_gs, remapped_regs + mmCRTC_GS);
	writel(mode->crtc_vpos_gs, remapped_regs + mmCRTC_VPOS_GS);
	writel(mode->crtc_rev, remapped_regs + mmCRTC_REV);
	writel(mode->crtc_dclk, remapped_regs + mmCRTC_DCLK);
	writel(mode->crtc_gclk, remapped_regs + mmCRTC_GCLK);
	writel(mode->crtc_goe, remapped_regs + mmCRTC_GOE);
	writel(mode->crtc_ps1_active, remapped_regs + mmCRTC_PS1_ACTIVE);

	writel(regs->lcd_format, remapped_regs + mmLCD_FORMAT);
	writel(regs->lcdd_cntl1, remapped_regs + mmLCDD_CNTL1);
	writel(regs->lcdd_cntl2, remapped_regs + mmLCDD_CNTL2);
	writel(regs->genlcd_cntl1, remapped_regs + mmGENLCD_CNTL1);
	writel(regs->genlcd_cntl2, remapped_regs + mmGENLCD_CNTL2);
	writel(regs->genlcd_cntl3, remapped_regs + mmGENLCD_CNTL3);

	writel(0x00000000, remapped_regs + mmCRTC_FRAME);
	writel(0x00000000, remapped_regs + mmCRTC_FRAME_VPOS);
	writel(0x00000000, remapped_regs + mmCRTC_DEFAULT_COUNT);
	writel(0x0000FF00, remapped_regs + mmLCD_BACKGROUND_COLOR);

	/* Hack for overlay in ext memory */
	temp32 = readl(remapped_regs + mmDISP_DEBUG2);
	temp32 |= 0xc0000000;
	writel(temp32, remapped_regs + mmDISP_DEBUG2);
}


static void w100_setup_memory(struct w100fb_par *par)
{
	union mc_ext_mem_location_u extmem_location;
	union mc_fb_location_u intmem_location;
	struct w100_mem_info *mem = par->mach->mem;
	struct w100_bm_mem_info *bm_mem = par->mach->bm_mem;

	if (!par->extmem_active) {
		w100_suspend(W100_SUSPEND_EXTMEM);

		/* Map Internal Memory at FB Base */
		intmem_location.f.mc_fb_start = W100_FB_BASE >> 8;
		intmem_location.f.mc_fb_top = (W100_FB_BASE+MEM_INT_SIZE) >> 8;
		writel((u32) (intmem_location.val), remapped_regs + mmMC_FB_LOCATION);

		/* Unmap External Memory - value is *probably* irrelevant but may have meaning
		   to acceleration libraries */
		extmem_location.f.mc_ext_mem_start = MEM_EXT_BASE_VALUE >> 8;
		extmem_location.f.mc_ext_mem_top = (MEM_EXT_BASE_VALUE-1) >> 8;
		writel((u32) (extmem_location.val), remapped_regs + mmMC_EXT_MEM_LOCATION);
	} else {
		/* Map Internal Memory to its default location */
		intmem_location.f.mc_fb_start = MEM_INT_BASE_VALUE >> 8;
		intmem_location.f.mc_fb_top = (MEM_INT_BASE_VALUE+MEM_INT_SIZE) >> 8;
		writel((u32) (intmem_location.val), remapped_regs + mmMC_FB_LOCATION);

		/* Map External Memory at FB Base */
		extmem_location.f.mc_ext_mem_start = W100_FB_BASE >> 8;
		extmem_location.f.mc_ext_mem_top = (W100_FB_BASE+par->mach->mem->size) >> 8;
		writel((u32) (extmem_location.val), remapped_regs + mmMC_EXT_MEM_LOCATION);

		writel(0x00007800, remapped_regs + mmMC_BIST_CTRL);
		writel(mem->ext_cntl, remapped_regs + mmMEM_EXT_CNTL);
		writel(0x00200021, remapped_regs + mmMEM_SDRAM_MODE_REG);
		udelay(100);
		writel(0x80200021, remapped_regs + mmMEM_SDRAM_MODE_REG);
		udelay(100);
		writel(mem->sdram_mode_reg, remapped_regs + mmMEM_SDRAM_MODE_REG);
		udelay(100);
		writel(mem->ext_timing_cntl, remapped_regs + mmMEM_EXT_TIMING_CNTL);
		writel(mem->io_cntl, remapped_regs + mmMEM_IO_CNTL);
		if (bm_mem) {
			writel(bm_mem->ext_mem_bw, remapped_regs + mmBM_EXT_MEM_BANDWIDTH);
			writel(bm_mem->offset, remapped_regs + mmBM_OFFSET);
			writel(bm_mem->ext_timing_ctl, remapped_regs + mmBM_MEM_EXT_TIMING_CNTL);
			writel(bm_mem->ext_cntl, remapped_regs + mmBM_MEM_EXT_CNTL);
			writel(bm_mem->mode_reg, remapped_regs + mmBM_MEM_MODE_REG);
			writel(bm_mem->io_cntl, remapped_regs + mmBM_MEM_IO_CNTL);
			writel(bm_mem->config, remapped_regs + mmBM_CONFIG);
		}
	}
}

static void w100_set_dispregs(struct w100fb_par *par)
{
	unsigned long rot=0, divider, offset=0;
	union graphic_ctrl_u graphic_ctrl;

	/* See if the mode has been rotated */
	if (par->xres == par->mode->xres) {
		if (par->flip) {
			rot=3; /* 180 degree */
			offset=(par->xres * par->yres) - 1;
		} /* else 0 degree */
		divider = par->mode->pixclk_divider;
	} else {
		if (par->flip) {
			rot=2; /* 270 degree */
			offset=par->xres - 1;
		} else {
			rot=1; /* 90 degree */
			offset=par->xres * (par->yres - 1);
		}
		divider = par->mode->pixclk_divider_rotated;
	}

	graphic_ctrl.val = 0; /* w32xx doesn't like undefined bits */
	switch (par->chip_id) {
		case CHIP_ID_W100:
			graphic_ctrl.f_w100.color_depth=6;
			graphic_ctrl.f_w100.en_crtc=1;
			graphic_ctrl.f_w100.en_graphic_req=1;
			graphic_ctrl.f_w100.en_graphic_crtc=1;
			graphic_ctrl.f_w100.lcd_pclk_on=1;
			graphic_ctrl.f_w100.lcd_sclk_on=1;
			graphic_ctrl.f_w100.low_power_on=0;
			graphic_ctrl.f_w100.req_freq=0;
			graphic_ctrl.f_w100.portrait_mode=rot;

			/* Zaurus needs this */
			switch(par->xres) {
				case 240:
				case 320:
				default:
					graphic_ctrl.f_w100.total_req_graphic=0xa0;
					break;
				case 480:
				case 640:
					switch(rot) {
						case 0:  /* 0 */
						case 3:  /* 180 */
							graphic_ctrl.f_w100.low_power_on=1;
							graphic_ctrl.f_w100.req_freq=5;
						break;
						case 1:  /* 90 */
						case 2:  /* 270 */
							graphic_ctrl.f_w100.req_freq=4;
							break;
						default:
							break;
					}
					graphic_ctrl.f_w100.total_req_graphic=0xf0;
					break;
			}
			break;
		case CHIP_ID_W3200:
		case CHIP_ID_W3220:
			graphic_ctrl.f_w32xx.color_depth=6;
			graphic_ctrl.f_w32xx.en_crtc=1;
			graphic_ctrl.f_w32xx.en_graphic_req=1;
			graphic_ctrl.f_w32xx.en_graphic_crtc=1;
			graphic_ctrl.f_w32xx.lcd_pclk_on=1;
			graphic_ctrl.f_w32xx.lcd_sclk_on=1;
			graphic_ctrl.f_w32xx.low_power_on=0;
			graphic_ctrl.f_w32xx.req_freq=0;
			graphic_ctrl.f_w32xx.total_req_graphic=par->mode->xres >> 1; /* panel xres, not mode */
			graphic_ctrl.f_w32xx.portrait_mode=rot;
			break;
	}

	/* Set the pixel clock source and divider */
	w100_pwr_state.pclk_cntl.f.pclk_src_sel = par->mode->pixclk_src;
	w100_pwr_state.pclk_cntl.f.pclk_post_div = divider;
	writel((u32) (w100_pwr_state.pclk_cntl.val), remapped_regs + mmPCLK_CNTL);

	writel(graphic_ctrl.val, remapped_regs + mmGRAPHIC_CTRL);
	writel(W100_FB_BASE + ((offset * BITS_PER_PIXEL/8)&~0x03UL), remapped_regs + mmGRAPHIC_OFFSET);
	writel((par->xres*BITS_PER_PIXEL/8), remapped_regs + mmGRAPHIC_PITCH);
}


/*
 * Work out how long the sync pulse lasts
 * Value is 1/(time in seconds)
 */
static void calc_hsync(struct w100fb_par *par)
{
	unsigned long hsync;
	struct w100_mode *mode = par->mode;
	union crtc_ss_u crtc_ss;

	if (mode->pixclk_src == CLK_SRC_XTAL)
		hsync=par->mach->xtal_freq;
	else
		hsync=((par->fastpll_mode && mode->fast_pll_freq) ? mode->fast_pll_freq : mode->pll_freq)*100000;

	hsync /= (w100_pwr_state.pclk_cntl.f.pclk_post_div + 1);

	crtc_ss.val = readl(remapped_regs + mmCRTC_SS);
	if (crtc_ss.val)
		par->hsync_len = hsync / (crtc_ss.f.ss_end-crtc_ss.f.ss_start);
	else
		par->hsync_len = 0;
}

static void w100_suspend(u32 mode)
{
	u32 val;

	writel(0x7FFF8000, remapped_regs + mmMC_EXT_MEM_LOCATION);
	writel(0x00FF0000, remapped_regs + mmMC_PERF_MON_CNTL);

	val = readl(remapped_regs + mmMEM_EXT_TIMING_CNTL);
	val &= ~(0x00100000);  /* bit20=0 */
	val |= 0xFF000000;     /* bit31:24=0xff */
	writel(val, remapped_regs + mmMEM_EXT_TIMING_CNTL);

	val = readl(remapped_regs + mmMEM_EXT_CNTL);
	val &= ~(0x00040000);  /* bit18=0 */
	val |= 0x00080000;     /* bit19=1 */
	writel(val, remapped_regs + mmMEM_EXT_CNTL);

	udelay(1);  /* wait 1us */

	if (mode == W100_SUSPEND_EXTMEM) {
		/* CKE: Tri-State */
		val = readl(remapped_regs + mmMEM_EXT_CNTL);
		val |= 0x40000000;  /* bit30=1 */
		writel(val, remapped_regs + mmMEM_EXT_CNTL);

		/* CLK: Stop */
		val = readl(remapped_regs + mmMEM_EXT_CNTL);
		val &= ~(0x00000001);  /* bit0=0 */
		writel(val, remapped_regs + mmMEM_EXT_CNTL);
	} else {
		writel(0x00000000, remapped_regs + mmSCLK_CNTL);
		writel(0x000000BF, remapped_regs + mmCLK_PIN_CNTL);
		writel(0x00000015, remapped_regs + mmPWRMGT_CNTL);

		udelay(5);

		val = readl(remapped_regs + mmPLL_CNTL);
		val |= 0x00000004;  /* bit2=1 */
		writel(val, remapped_regs + mmPLL_CNTL);
		writel(0x0000001d, remapped_regs + mmPWRMGT_CNTL);
	}
}

static void w100_vsync(void)
{
	u32 tmp;
	int timeout = 30000;  /* VSync timeout = 30[ms] > 16.8[ms] */

	tmp = readl(remapped_regs + mmACTIVE_V_DISP);

	/* set vline pos  */
	writel((tmp >> 16) & 0x3ff, remapped_regs + mmDISP_INT_CNTL);

	/* disable vline irq */
	tmp = readl(remapped_regs + mmGEN_INT_CNTL);

	tmp &= ~0x00000002;
	writel(tmp, remapped_regs + mmGEN_INT_CNTL);

	/* clear vline irq status */
	writel(0x00000002, remapped_regs + mmGEN_INT_STATUS);

	/* enable vline irq */
	writel((tmp | 0x00000002), remapped_regs + mmGEN_INT_CNTL);

	/* clear vline irq status */
	writel(0x00000002, remapped_regs + mmGEN_INT_STATUS);

	while(timeout > 0) {
		if (readl(remapped_regs + mmGEN_INT_STATUS) & 0x00000002)
			break;
		udelay(1);
		timeout--;
	}

	/* disable vline irq */
	writel(tmp, remapped_regs + mmGEN_INT_CNTL);

	/* clear vline irq status */
	writel(0x00000002, remapped_regs + mmGEN_INT_STATUS);
}

static struct platform_driver w100fb_driver = {
	.probe		= w100fb_probe,
	.remove		= w100fb_remove,
	.suspend	= w100fb_suspend,
	.resume		= w100fb_resume,
	.driver		= {
		.name	= "w100fb",
	},
};

int __devinit w100fb_init(void)
{
	return platform_driver_register(&w100fb_driver);
}

void __exit w100fb_cleanup(void)
{
	platform_driver_unregister(&w100fb_driver);
}

module_init(w100fb_init);
module_exit(w100fb_cleanup);

MODULE_DESCRIPTION("ATI Imageon w100 framebuffer driver");
MODULE_LICENSE("GPL");
