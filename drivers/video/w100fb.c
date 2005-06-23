/*
 * linux/drivers/video/w100fb.c
 *
 * Frame Buffer Device for ATI Imageon w100 (Wallaby)
 *
 * Copyright (C) 2002, ATI Corp.
 * Copyright (C) 2004-2005 Richard Purdie
 *
 * Rewritten for 2.6 by Richard Purdie <rpurdie@rpsys.net>
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
#include <linux/device.h>
#include <linux/string.h>
#include <linux/proc_fs.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <video/w100fb.h>
#include "w100fb.h"

/*
 * Prototypes
 */
static void w100fb_save_buffer(void);
static void w100fb_clear_buffer(void);
static void w100fb_restore_buffer(void);
static void w100fb_clear_screen(u32 mode, long int offset);
static void w100_resume(void);
static void w100_suspend(u32 mode);
static void w100_init_qvga_rotation(u16 deg);
static void w100_init_vga_rotation(u16 deg);
static void w100_vsync(void);
static void w100_init_sharp_lcd(u32 mode);
static void w100_pwm_setup(void);
static void w100_InitExtMem(u32 mode);
static void w100_hw_init(void);
static u16 w100_set_fastsysclk(u16 Freq);

static void lcdtg_hw_init(u32 mode);
static void lcdtg_lcd_change(u32 mode);
static void lcdtg_resume(void);
static void lcdtg_suspend(void);


/* Register offsets & lengths */
#define REMAPPED_FB_LEN   0x15ffff

#define BITS_PER_PIXEL    16

/* Pseudo palette size */
#define MAX_PALETTES      16

/* for resolution change */
#define LCD_MODE_INIT (-1)
#define LCD_MODE_480    0
#define LCD_MODE_320    1
#define LCD_MODE_240    2
#define LCD_MODE_640    3

#define LCD_SHARP_QVGA 0
#define LCD_SHARP_VGA  1

#define LCD_MODE_PORTRAIT	0
#define LCD_MODE_LANDSCAPE	1

#define W100_SUSPEND_EXTMEM 0
#define W100_SUSPEND_ALL    1

/* General frame buffer data structures */
struct w100fb_par {
	u32 xres;
	u32 yres;
	int fastsysclk_mode;
	int lcdMode;
	int rotation_flag;
	int blanking_flag;
	int comadj;
	int phadadj;
};

static struct w100fb_par *current_par;

/* Remapped addresses for base cfg, memmapped regs and the frame buffer itself */
static void *remapped_base;
static void *remapped_regs;
static void *remapped_fbuf;

/* External Function */
static void(*w100fb_ssp_send)(u8 adrs, u8 data);

/*
 * Sysfs functions
 */

static ssize_t rotation_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct w100fb_par *par=info->par;

	return sprintf(buf, "%d\n",par->rotation_flag);
}

static ssize_t rotation_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int rotate;
	struct fb_info *info = dev_get_drvdata(dev);
	struct w100fb_par *par=info->par;

	rotate = simple_strtoul(buf, NULL, 10);

	if (rotate > 0) par->rotation_flag = 1;
	else par->rotation_flag = 0;

	if (par->lcdMode == LCD_MODE_320)
		w100_init_qvga_rotation(par->rotation_flag ? 270 : 90);
	else if (par->lcdMode == LCD_MODE_240)
		w100_init_qvga_rotation(par->rotation_flag ? 180 : 0);
	else if (par->lcdMode == LCD_MODE_640)
		w100_init_vga_rotation(par->rotation_flag ? 270 : 90);
	else if (par->lcdMode == LCD_MODE_480)
		w100_init_vga_rotation(par->rotation_flag ? 180 : 0);

	return count;
}

static DEVICE_ATTR(rotation, 0644, rotation_show, rotation_store);

static ssize_t w100fb_reg_read(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long param;
	unsigned long regs;
	regs = simple_strtoul(buf, NULL, 16);
	param = readl(remapped_regs + regs);
	printk("Read Register 0x%08lX: 0x%08lX\n", regs, param);
	return count;
}

static DEVICE_ATTR(reg_read, 0200, NULL, w100fb_reg_read);

static ssize_t w100fb_reg_write(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long regs;
	unsigned long param;
	sscanf(buf, "%lx %lx", &regs, &param);

	if (regs <= 0x2000) {
		printk("Write Register 0x%08lX: 0x%08lX\n", regs, param);
		writel(param, remapped_regs + regs);
	}

	return count;
}

static DEVICE_ATTR(reg_write, 0200, NULL, w100fb_reg_write);


static ssize_t fastsysclk_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct w100fb_par *par=info->par;

	return sprintf(buf, "%d\n",par->fastsysclk_mode);
}

static ssize_t fastsysclk_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int param;
	struct fb_info *info = dev_get_drvdata(dev);
	struct w100fb_par *par=info->par;

	param = simple_strtoul(buf, NULL, 10);

	if (param == 75) {
		printk("Set fastsysclk %d\n", param);
		par->fastsysclk_mode = param;
		w100_set_fastsysclk(par->fastsysclk_mode);
	} else if (param == 100) {
		printk("Set fastsysclk %d\n", param);
		par->fastsysclk_mode = param;
		w100_set_fastsysclk(par->fastsysclk_mode);
	}
	return count;
}

static DEVICE_ATTR(fastsysclk, 0644, fastsysclk_show, fastsysclk_store);

/*
 * The touchscreen on this device needs certain information
 * from the video driver to function correctly. We export it here.
 */
int w100fb_get_xres(void) {
	return current_par->xres;
}

int w100fb_get_blanking(void) {
	return current_par->blanking_flag;
}

int w100fb_get_fastsysclk(void) {
	return current_par->fastsysclk_mode;
}
EXPORT_SYMBOL(w100fb_get_xres);
EXPORT_SYMBOL(w100fb_get_blanking);
EXPORT_SYMBOL(w100fb_get_fastsysclk);


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
	struct w100fb_par *par;
	par=info->par;

	switch(blank_mode) {

 	case FB_BLANK_NORMAL: /* Normal blanking */
	case FB_BLANK_VSYNC_SUSPEND: /* VESA blank (vsync off) */
	case FB_BLANK_HSYNC_SUSPEND: /* VESA blank (hsync off) */
 	case FB_BLANK_POWERDOWN: /* Poweroff */
  		if (par->blanking_flag == 0) {
			w100fb_save_buffer();
			lcdtg_suspend();
			par->blanking_flag = 1;
  		}
  		break;

 	case FB_BLANK_UNBLANK: /* Unblanking */
  		if (par->blanking_flag != 0) {
			w100fb_restore_buffer();
			lcdtg_resume();
			par->blanking_flag = 0;
  		}
  		break;
 	}
	return 0;
}

/*
 *  Change the resolution by calling the appropriate hardware functions
 */
static void w100fb_changeres(int rotate_mode, u32 mode)
{
	u16 rotation=0;

	switch(rotate_mode) {
	case LCD_MODE_LANDSCAPE:
		rotation=(current_par->rotation_flag ? 270 : 90);
		break;
	case LCD_MODE_PORTRAIT:
		rotation=(current_par->rotation_flag ? 180 : 0);
		break;
	}

	w100_pwm_setup();
	switch(mode) {
	case LCD_SHARP_QVGA:
		w100_vsync();
		w100_suspend(W100_SUSPEND_EXTMEM);
		w100_init_sharp_lcd(LCD_SHARP_QVGA);
		w100_init_qvga_rotation(rotation);
		w100_InitExtMem(LCD_SHARP_QVGA);
		w100fb_clear_screen(LCD_SHARP_QVGA, 0);
		lcdtg_lcd_change(LCD_SHARP_QVGA);
		break;
	case LCD_SHARP_VGA:
		w100fb_clear_screen(LCD_SHARP_QVGA, 0);
		writel(0xBFFFA000, remapped_regs + mmMC_EXT_MEM_LOCATION);
		w100_InitExtMem(LCD_SHARP_VGA);
		w100fb_clear_screen(LCD_SHARP_VGA, 0x200000);
		w100_vsync();
		w100_init_sharp_lcd(LCD_SHARP_VGA);
		if (rotation != 0)
			w100_init_vga_rotation(rotation);
		lcdtg_lcd_change(LCD_SHARP_VGA);
		break;
	}
}

/*
 * Set up the display for the fb subsystem
 */
static void w100fb_activate_var(struct fb_info *info)
{
	u32 temp32;
	struct w100fb_par *par=info->par;
	struct fb_var_screeninfo *var = &info->var;

	/* Set the hardware to 565 */
	temp32 = readl(remapped_regs + mmDISP_DEBUG2);
	temp32 &= 0xff7fffff;
	temp32 |= 0x00800000;
	writel(temp32, remapped_regs + mmDISP_DEBUG2);

	if (par->lcdMode == LCD_MODE_INIT) {
		w100_init_sharp_lcd(LCD_SHARP_VGA);
		w100_init_vga_rotation(par->rotation_flag ? 270 : 90);
		par->lcdMode = LCD_MODE_640;
		lcdtg_hw_init(LCD_SHARP_VGA);
	} else if (var->xres == 320 && var->yres == 240) {
		if (par->lcdMode != LCD_MODE_320) {
			w100fb_changeres(LCD_MODE_LANDSCAPE, LCD_SHARP_QVGA);
			par->lcdMode = LCD_MODE_320;
		}
	} else if (var->xres == 240 && var->yres == 320) {
		if (par->lcdMode != LCD_MODE_240) {
			w100fb_changeres(LCD_MODE_PORTRAIT, LCD_SHARP_QVGA);
			par->lcdMode = LCD_MODE_240;
		}
	} else if (var->xres == 640 && var->yres == 480) {
		if (par->lcdMode != LCD_MODE_640) {
			w100fb_changeres(LCD_MODE_LANDSCAPE, LCD_SHARP_VGA);
			par->lcdMode = LCD_MODE_640;
		}
	} else if (var->xres == 480 && var->yres == 640) {
		if (par->lcdMode != LCD_MODE_480) {
			w100fb_changeres(LCD_MODE_PORTRAIT, LCD_SHARP_VGA);
			par->lcdMode = LCD_MODE_480;
		}
	} else printk(KERN_ERR "W100FB: Resolution error!\n");
}


/*
 *  w100fb_check_var():
 *  Get the video params out of 'var'. If a value doesn't fit, round it up,
 *  if it's too big, return -EINVAL.
 *
 */
static int w100fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	if (var->xres < var->yres) { /* Portrait mode */
		if ((var->xres > 480) || (var->yres > 640)) {
			return -EINVAL;
		} else if ((var->xres > 240) || (var->yres > 320)) {
			var->xres = 480;
			var->yres = 640;
		} else {
			var->xres = 240;
			var->yres = 320;
		}
	} else { /* Landscape mode */
		if ((var->xres > 640) || (var->yres > 480)) {
			return -EINVAL;
		} else if ((var->xres > 320) || (var->yres > 240)) {
			var->xres = 640;
			var->yres = 480;
		} else {
			var->xres = 320;
			var->yres = 240;
		}
	}

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
	var->pixclock = 0x04;	/* 171521; */

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

	par->xres = info->var.xres;
	par->yres = info->var.yres;

	info->fix.visual = FB_VISUAL_TRUECOLOR;

	info->fix.ypanstep = 0;
	info->fix.ywrapstep = 0;

	if (par->blanking_flag)
		w100fb_clear_buffer();

	w100fb_activate_var(info);

	if (par->lcdMode == LCD_MODE_480) {
		info->fix.line_length = (480 * BITS_PER_PIXEL) / 8;
		info->fix.smem_len = 0x200000;
	} else if (par->lcdMode == LCD_MODE_320) {
		info->fix.line_length = (320 * BITS_PER_PIXEL) / 8;
		info->fix.smem_len = 0x60000;
	} else if (par->lcdMode == LCD_MODE_240) {
		info->fix.line_length = (240 * BITS_PER_PIXEL) / 8;
		info->fix.smem_len = 0x60000;
	} else if (par->lcdMode == LCD_MODE_INIT || par->lcdMode == LCD_MODE_640) {
		info->fix.line_length = (640 * BITS_PER_PIXEL) / 8;
		info->fix.smem_len = 0x200000;
	}

	return 0;
}


/*
 *      Frame buffer operations
 */
static struct fb_ops w100fb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = w100fb_check_var,
	.fb_set_par = w100fb_set_par,
	.fb_setcolreg = w100fb_setcolreg,
	.fb_blank = w100fb_blank,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_cursor = soft_cursor,
};


static void w100fb_clear_screen(u32 mode, long int offset)
{
	int i, numPix = 0;

	if (mode == LCD_SHARP_VGA)
		numPix = 640 * 480;
	else if (mode == LCD_SHARP_QVGA)
		numPix = 320 * 240;

	for (i = 0; i < numPix; i++)
		writew(0xffff, remapped_fbuf + offset + (2*i));
}


/* Need to split up the buffers to stay within the limits of kmalloc */
#define W100_BUF_NUM	6
static uint32_t *gSaveImagePtr[W100_BUF_NUM] = { NULL };

static void w100fb_save_buffer(void)
{
	int i, j, bufsize;

	bufsize=(current_par->xres * current_par->yres * BITS_PER_PIXEL / 8) / W100_BUF_NUM;
	for (i = 0; i < W100_BUF_NUM; i++) {
		if (gSaveImagePtr[i] == NULL)
			gSaveImagePtr[i] = kmalloc(bufsize, GFP_KERNEL);
		if (gSaveImagePtr[i] == NULL) {
			w100fb_clear_buffer();
			printk(KERN_WARNING "can't alloc pre-off image buffer %d\n", i);
			break;
		}
		for (j = 0; j < bufsize/4; j++)
			*(gSaveImagePtr[i] + j) = readl(remapped_fbuf + (bufsize*i) + j*4);
	}
}


static void w100fb_restore_buffer(void)
{
	int i, j, bufsize;

	bufsize=(current_par->xres * current_par->yres * BITS_PER_PIXEL / 8) / W100_BUF_NUM;
	for (i = 0; i < W100_BUF_NUM; i++) {
		if (gSaveImagePtr[i] == NULL) {
			printk(KERN_WARNING "can't find pre-off image buffer %d\n", i);
			w100fb_clear_buffer();
			break;
		}
		for (j = 0; j < (bufsize/4); j++)
			writel(*(gSaveImagePtr[i] + j),remapped_fbuf + (bufsize*i) + (j*4));
		kfree(gSaveImagePtr[i]);
		gSaveImagePtr[i] = NULL;
	}
}


static void w100fb_clear_buffer(void)
{
	int i;
	for (i = 0; i < W100_BUF_NUM; i++) {
		kfree(gSaveImagePtr[i]);
		gSaveImagePtr[i] = NULL;
	}
}


#ifdef CONFIG_PM
static int w100fb_suspend(struct device *dev, pm_message_t state, u32 level)
{
	if (level == SUSPEND_POWER_DOWN) {
		struct fb_info *info = dev_get_drvdata(dev);
		struct w100fb_par *par=info->par;

		w100fb_save_buffer();
		lcdtg_suspend();
		w100_suspend(W100_SUSPEND_ALL);
		par->blanking_flag = 1;
	}
	return 0;
}

static int w100fb_resume(struct device *dev, u32 level)
{
	if (level == RESUME_POWER_ON) {
		struct fb_info *info = dev_get_drvdata(dev);
		struct w100fb_par *par=info->par;

		w100_resume();
		w100fb_restore_buffer();
		lcdtg_resume();
		par->blanking_flag = 0;
	}
	return 0;
}
#else
#define w100fb_suspend	NULL
#define w100fb_resume	NULL
#endif


int __init w100fb_probe(struct device *dev)
{
	struct w100fb_mach_info *inf;
	struct fb_info *info;
	struct w100fb_par *par;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!mem)
		return -EINVAL;

	/* remap the areas we're going to use */
	remapped_base = ioremap_nocache(mem->start+W100_CFG_BASE, W100_CFG_LEN);
	if (remapped_base == NULL)
		return -EIO;

	remapped_regs = ioremap_nocache(mem->start+W100_REG_BASE, W100_REG_LEN);
	if (remapped_regs == NULL) {
		iounmap(remapped_base);
		return -EIO;
	}

	remapped_fbuf = ioremap_nocache(mem->start+MEM_EXT_BASE_VALUE, REMAPPED_FB_LEN);
	if (remapped_fbuf == NULL) {
		iounmap(remapped_base);
		iounmap(remapped_regs);
		return -EIO;
	}

	info=framebuffer_alloc(sizeof(struct w100fb_par), dev);
	if (!info) {
		iounmap(remapped_base);
		iounmap(remapped_regs);
		iounmap(remapped_fbuf);
		return -ENOMEM;
	}

	info->device=dev;
	par = info->par;
	current_par=info->par;
	dev_set_drvdata(dev, info);

	inf = dev->platform_data;
	par->phadadj = inf->phadadj;
	par->comadj = inf->comadj;
	par->fastsysclk_mode = 75;
	par->lcdMode = LCD_MODE_INIT;
	par->rotation_flag=0;
	par->blanking_flag=0;
	w100fb_ssp_send = inf->w100fb_ssp_send;

	w100_hw_init();
	w100_pwm_setup();

	info->pseudo_palette = kmalloc(sizeof (u32) * MAX_PALETTES, GFP_KERNEL);
	if (!info->pseudo_palette) {
		iounmap(remapped_base);
		iounmap(remapped_regs);
		iounmap(remapped_fbuf);
		return -ENOMEM;
	}

	info->fbops = &w100fb_ops;
	info->flags = FBINFO_DEFAULT;
	info->node = -1;
	info->screen_base = remapped_fbuf;
	info->screen_size = REMAPPED_FB_LEN;

	info->var.xres = 640;
	info->var.xres_virtual = info->var.xres;
	info->var.yres = 480;
	info->var.yres_virtual = info->var.yres;
	info->var.pixclock = 0x04;	/* 171521; */
	info->var.sync = 0;
	info->var.grayscale = 0;
	info->var.xoffset = info->var.yoffset = 0;
	info->var.accel_flags = 0;
	info->var.activate = FB_ACTIVATE_NOW;

	strcpy(info->fix.id, "w100fb");
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.type_aux = 0;
	info->fix.accel = FB_ACCEL_NONE;
	info->fix.smem_start = mem->start+MEM_EXT_BASE_VALUE;
	info->fix.mmio_start = mem->start+W100_REG_BASE;
	info->fix.mmio_len = W100_REG_LEN;

	w100fb_check_var(&info->var, info);
	w100fb_set_par(info);

	if (register_framebuffer(info) < 0) {
		kfree(info->pseudo_palette);
		iounmap(remapped_base);
		iounmap(remapped_regs);
		iounmap(remapped_fbuf);
		return -EINVAL;
	}

	device_create_file(dev, &dev_attr_fastsysclk);
	device_create_file(dev, &dev_attr_reg_read);
	device_create_file(dev, &dev_attr_reg_write);
	device_create_file(dev, &dev_attr_rotation);

	printk(KERN_INFO "fb%d: %s frame buffer device\n", info->node, info->fix.id);
	return 0;
}


static int w100fb_remove(struct device *dev)
{
	struct fb_info *info = dev_get_drvdata(dev);

	device_remove_file(dev, &dev_attr_fastsysclk);
	device_remove_file(dev, &dev_attr_reg_read);
	device_remove_file(dev, &dev_attr_reg_write);
	device_remove_file(dev, &dev_attr_rotation);

	unregister_framebuffer(info);

	w100fb_clear_buffer();
	kfree(info->pseudo_palette);

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

/*
 * Initialization of critical w100 hardware
 */
static void w100_hw_init(void)
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
	union mc_ext_mem_location_u mc_ext_mem_loc;
	union cif_io_u cif_io;

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

	/* This location is relative to internal w100 addresses */
	writel(0x15FF1000, remapped_regs + mmMC_FB_LOCATION);

	mc_ext_mem_loc.val = defMC_EXT_MEM_LOCATION;
	mc_ext_mem_loc.f.mc_ext_mem_start = MEM_EXT_BASE_VALUE >> 8;
	mc_ext_mem_loc.f.mc_ext_mem_top = MEM_EXT_TOP_VALUE >> 8;
	writel((u32) (mc_ext_mem_loc.val), remapped_regs + mmMC_EXT_MEM_LOCATION);

	if ((current_par->lcdMode == LCD_MODE_240) || (current_par->lcdMode == LCD_MODE_320))
		w100_InitExtMem(LCD_SHARP_QVGA);
	else
		w100_InitExtMem(LCD_SHARP_VGA);

	wrap_start_dir.val = defWRAP_START_DIR;
	wrap_start_dir.f.start_addr = WRAP_BUF_BASE_VALUE >> 1;
	writel((u32) (wrap_start_dir.val), remapped_regs + mmWRAP_START_DIR);

	wrap_top_dir.val = defWRAP_TOP_DIR;
	wrap_top_dir.f.top_addr = WRAP_BUF_TOP_VALUE >> 1;
	writel((u32) (wrap_top_dir.val), remapped_regs + mmWRAP_TOP_DIR);

	writel((u32) 0x2440, remapped_regs + mmRBBM_CNTL);
}


/*
 * Types
 */

struct pll_parm {
	u16 freq;		/* desired Fout for PLL */
	u8 M;
	u8 N_int;
	u8 N_fac;
	u8 tfgoal;
	u8 lock_time;
};

struct power_state {
	union clk_pin_cntl_u clk_pin_cntl;
	union pll_ref_fb_div_u pll_ref_fb_div;
	union pll_cntl_u pll_cntl;
	union sclk_cntl_u sclk_cntl;
	union pclk_cntl_u pclk_cntl;
	union clk_test_cntl_u clk_test_cntl;
	union pwrmgt_cntl_u pwrmgt_cntl;
	u32 freq;		/* Fout for PLL calibration */
	u8 tf100;		/* for pll calibration */
	u8 tf80;		/* for pll calibration */
	u8 tf20;		/* for pll calibration */
	u8 M;			/* for pll calibration */
	u8 N_int;		/* for pll calibration */
	u8 N_fac;		/* for pll calibration */
	u8 lock_time;	/* for pll calibration */
	u8 tfgoal;		/* for pll calibration */
	u8 auto_mode;	/* hardware auto switch? */
	u8 pwm_mode;		/* 0 fast, 1 normal/slow */
	u16 fast_sclk;	/* fast clk freq */
	u16 norm_sclk;	/* slow clk freq */
};


/*
 * Global state variables
 */

static struct power_state w100_pwr_state;

/* This table is specific for 12.5MHz ref crystal.  */
static struct pll_parm gPLLTable[] = {
    /*freq     M   N_int    N_fac  tfgoal  lock_time */
    { 50,      0,   1,       0,     0xE0,        56}, /*  50.00 MHz */
    { 75,      0,   5,       0,     0xDE,	     37}, /*  75.00 MHz */
    {100,      0,   7,       0,     0xE0,        28}, /* 100.00 MHz */
    {125,      0,   9,       0,     0xE0,        22}, /* 125.00 MHz */
    {150,      0,   11,      0,     0xE0,        17}, /* 150.00 MHz */
    {  0,      0,   0,       0,        0,         0}  /* Terminator */
};


static u8 w100_pll_get_testcount(u8 testclk_sel)
{
	udelay(5);

	w100_pwr_state.clk_test_cntl.f.start_check_freq = 0x0;
	w100_pwr_state.clk_test_cntl.f.testclk_sel = testclk_sel;
	w100_pwr_state.clk_test_cntl.f.tstcount_rst = 0x1;	/*reset test count */
	writel((u32) (w100_pwr_state.clk_test_cntl.val), remapped_regs + mmCLK_TEST_CNTL);
	w100_pwr_state.clk_test_cntl.f.tstcount_rst = 0x0;
	writel((u32) (w100_pwr_state.clk_test_cntl.val), remapped_regs + mmCLK_TEST_CNTL);

	w100_pwr_state.clk_test_cntl.f.start_check_freq = 0x1;
	writel((u32) (w100_pwr_state.clk_test_cntl.val), remapped_regs + mmCLK_TEST_CNTL);

	udelay(20);

	w100_pwr_state.clk_test_cntl.val = readl(remapped_regs + mmCLK_TEST_CNTL);
	w100_pwr_state.clk_test_cntl.f.start_check_freq = 0x0;
	writel((u32) (w100_pwr_state.clk_test_cntl.val), remapped_regs + mmCLK_TEST_CNTL);

	return w100_pwr_state.clk_test_cntl.f.test_count;
}


static u8 w100_pll_adjust(void)
{
	do {
		/* Wai Ming 80 percent of VDD 1.3V gives 1.04V, minimum operating voltage is 1.08V
		 * therefore, commented out the following lines
		 * tf80 meant tf100
		 * set VCO input = 0.8 * VDD
		 */
		w100_pwr_state.pll_cntl.f.pll_dactal = 0xd;
		writel((u32) (w100_pwr_state.pll_cntl.val), remapped_regs + mmPLL_CNTL);

		w100_pwr_state.tf80 = w100_pll_get_testcount(0x1);	/* PLLCLK */
		if (w100_pwr_state.tf80 >= (w100_pwr_state.tfgoal)) {
			/* set VCO input = 0.2 * VDD */
			w100_pwr_state.pll_cntl.f.pll_dactal = 0x7;
			writel((u32) (w100_pwr_state.pll_cntl.val), remapped_regs + mmPLL_CNTL);

			w100_pwr_state.tf20 = w100_pll_get_testcount(0x1);	/* PLLCLK */
			if (w100_pwr_state.tf20 <= (w100_pwr_state.tfgoal))
				return 1; // Success

			if ((w100_pwr_state.pll_cntl.f.pll_vcofr == 0x0) &&
			    ((w100_pwr_state.pll_cntl.f.pll_pvg == 0x7) ||
			     (w100_pwr_state.pll_cntl.f.pll_ioffset == 0x0))) {
				/* slow VCO config */
				w100_pwr_state.pll_cntl.f.pll_vcofr = 0x1;
				w100_pwr_state.pll_cntl.f.pll_pvg = 0x0;
				w100_pwr_state.pll_cntl.f.pll_ioffset = 0x0;
				writel((u32) (w100_pwr_state.pll_cntl.val),
					remapped_regs + mmPLL_CNTL);
				continue;
			}
		}
		if ((w100_pwr_state.pll_cntl.f.pll_ioffset) < 0x3) {
			w100_pwr_state.pll_cntl.f.pll_ioffset += 0x1;
			writel((u32) (w100_pwr_state.pll_cntl.val), remapped_regs + mmPLL_CNTL);
			continue;
		}
		if ((w100_pwr_state.pll_cntl.f.pll_pvg) < 0x7) {
			w100_pwr_state.pll_cntl.f.pll_ioffset = 0x0;
			w100_pwr_state.pll_cntl.f.pll_pvg += 0x1;
			writel((u32) (w100_pwr_state.pll_cntl.val), remapped_regs + mmPLL_CNTL);
			continue;
		}
		return 0; // error
	} while(1);
}


/*
 * w100_pll_calibration
 *                freq = target frequency of the PLL
 *                (note: crystal = 14.3MHz)
 */
static u8 w100_pll_calibration(u32 freq)
{
	u8 status;

	/* initial setting */
	w100_pwr_state.pll_cntl.f.pll_pwdn = 0x0;		/* power down */
	w100_pwr_state.pll_cntl.f.pll_reset = 0x0;		/* not reset */
	w100_pwr_state.pll_cntl.f.pll_tcpoff = 0x1;	/* Hi-Z */
	w100_pwr_state.pll_cntl.f.pll_pvg = 0x0;		/* VCO gain = 0 */
	w100_pwr_state.pll_cntl.f.pll_vcofr = 0x0;		/* VCO frequency range control = off */
	w100_pwr_state.pll_cntl.f.pll_ioffset = 0x0;	/* current offset inside VCO = 0 */
	w100_pwr_state.pll_cntl.f.pll_ring_off = 0x0;
	writel((u32) (w100_pwr_state.pll_cntl.val), remapped_regs + mmPLL_CNTL);

	/* check for (tf80 >= tfgoal) && (tf20 =< tfgoal) */
	if ((w100_pwr_state.tf80 < w100_pwr_state.tfgoal) || (w100_pwr_state.tf20 > w100_pwr_state.tfgoal)) {
		status=w100_pll_adjust();
	}
	/* PLL Reset And Lock */

	/* set VCO input = 0.5 * VDD */
	w100_pwr_state.pll_cntl.f.pll_dactal = 0xa;
	writel((u32) (w100_pwr_state.pll_cntl.val), remapped_regs + mmPLL_CNTL);

	/* reset time */
	udelay(1);

	/* enable charge pump */
	w100_pwr_state.pll_cntl.f.pll_tcpoff = 0x0;	/* normal */
	writel((u32) (w100_pwr_state.pll_cntl.val), remapped_regs + mmPLL_CNTL);

	/* set VCO input = Hi-Z */
	/* disable DAC */
	w100_pwr_state.pll_cntl.f.pll_dactal = 0x0;
	writel((u32) (w100_pwr_state.pll_cntl.val), remapped_regs + mmPLL_CNTL);

	/* lock time */
	udelay(400);	/* delay 400 us */

	/* PLL locked */

	w100_pwr_state.sclk_cntl.f.sclk_src_sel = 0x1;	/* PLL clock */
	writel((u32) (w100_pwr_state.sclk_cntl.val), remapped_regs + mmSCLK_CNTL);

	w100_pwr_state.tf100 = w100_pll_get_testcount(0x1);	/* PLLCLK */

	return status;
}


static u8 w100_pll_set_clk(void)
{
	u8 status;

	if (w100_pwr_state.auto_mode == 1)	/* auto mode */
	{
		w100_pwr_state.pwrmgt_cntl.f.pwm_fast_noml_hw_en = 0x0;	/* disable fast to normal */
		w100_pwr_state.pwrmgt_cntl.f.pwm_noml_fast_hw_en = 0x0;	/* disable normal to fast */
		writel((u32) (w100_pwr_state.pwrmgt_cntl.val), remapped_regs + mmPWRMGT_CNTL);
	}

	w100_pwr_state.sclk_cntl.f.sclk_src_sel = 0x0;	/* crystal clock */
	writel((u32) (w100_pwr_state.sclk_cntl.val), remapped_regs + mmSCLK_CNTL);

	w100_pwr_state.pll_ref_fb_div.f.pll_ref_div = w100_pwr_state.M;
	w100_pwr_state.pll_ref_fb_div.f.pll_fb_div_int = w100_pwr_state.N_int;
	w100_pwr_state.pll_ref_fb_div.f.pll_fb_div_frac = w100_pwr_state.N_fac;
	w100_pwr_state.pll_ref_fb_div.f.pll_lock_time = w100_pwr_state.lock_time;
	writel((u32) (w100_pwr_state.pll_ref_fb_div.val), remapped_regs + mmPLL_REF_FB_DIV);

	w100_pwr_state.pwrmgt_cntl.f.pwm_mode_req = 0;
	writel((u32) (w100_pwr_state.pwrmgt_cntl.val), remapped_regs + mmPWRMGT_CNTL);

	status = w100_pll_calibration (w100_pwr_state.freq);

	if (w100_pwr_state.auto_mode == 1)	/* auto mode */
	{
		w100_pwr_state.pwrmgt_cntl.f.pwm_fast_noml_hw_en = 0x1;	/* reenable fast to normal */
		w100_pwr_state.pwrmgt_cntl.f.pwm_noml_fast_hw_en = 0x1;	/* reenable normal to fast  */
		writel((u32) (w100_pwr_state.pwrmgt_cntl.val), remapped_regs + mmPWRMGT_CNTL);
	}
	return status;
}


/* assume reference crystal clk is 12.5MHz,
 * and that doubling is not enabled.
 *
 * Freq = 12 == 12.5MHz.
 */
static u16 w100_set_slowsysclk(u16 freq)
{
	if (w100_pwr_state.norm_sclk == freq)
		return freq;

	if (w100_pwr_state.auto_mode == 1)	/* auto mode */
		return 0;

	if (freq == 12) {
		w100_pwr_state.norm_sclk = freq;
		w100_pwr_state.sclk_cntl.f.sclk_post_div_slow = 0x0;	/* Pslow = 1 */
		w100_pwr_state.sclk_cntl.f.sclk_src_sel = 0x0;	/* crystal src */

		writel((u32) (w100_pwr_state.sclk_cntl.val), remapped_regs + mmSCLK_CNTL);

		w100_pwr_state.clk_pin_cntl.f.xtalin_pm_en = 0x1;
		writel((u32) (w100_pwr_state.clk_pin_cntl.val), remapped_regs + mmCLK_PIN_CNTL);

		w100_pwr_state.pwrmgt_cntl.f.pwm_enable = 0x1;
		w100_pwr_state.pwrmgt_cntl.f.pwm_mode_req = 0x1;
		writel((u32) (w100_pwr_state.pwrmgt_cntl.val), remapped_regs + mmPWRMGT_CNTL);
		w100_pwr_state.pwm_mode = 1;	/* normal mode */
		return freq;
	} else
		return 0;
}


static u16 w100_set_fastsysclk(u16 freq)
{
	u16 pll_freq;
	int i;

	while(1) {
		pll_freq = (u16) (freq * (w100_pwr_state.sclk_cntl.f.sclk_post_div_fast + 1));
		i = 0;
		do {
			if (pll_freq == gPLLTable[i].freq) {
				w100_pwr_state.freq = gPLLTable[i].freq * 1000000;
				w100_pwr_state.M = gPLLTable[i].M;
				w100_pwr_state.N_int = gPLLTable[i].N_int;
				w100_pwr_state.N_fac = gPLLTable[i].N_fac;
				w100_pwr_state.tfgoal = gPLLTable[i].tfgoal;
				w100_pwr_state.lock_time = gPLLTable[i].lock_time;
				w100_pwr_state.tf20 = 0xff;	/* set highest */
				w100_pwr_state.tf80 = 0x00;	/* set lowest */

				w100_pll_set_clk();
				w100_pwr_state.pwm_mode = 0;	/* fast mode */
				w100_pwr_state.fast_sclk = freq;
				return freq;
			}
			i++;
		} while(gPLLTable[i].freq);

		if (w100_pwr_state.auto_mode == 1)
			break;

		if (w100_pwr_state.sclk_cntl.f.sclk_post_div_fast == 0)
			break;

		w100_pwr_state.sclk_cntl.f.sclk_post_div_fast -= 1;
		writel((u32) (w100_pwr_state.sclk_cntl.val), remapped_regs + mmSCLK_CNTL);
	}
	return 0;
}


/* Set up an initial state.  Some values/fields set
   here will be overwritten. */
static void w100_pwm_setup(void)
{
	w100_pwr_state.clk_pin_cntl.f.osc_en = 0x1;
	w100_pwr_state.clk_pin_cntl.f.osc_gain = 0x1f;
	w100_pwr_state.clk_pin_cntl.f.dont_use_xtalin = 0x0;
	w100_pwr_state.clk_pin_cntl.f.xtalin_pm_en = 0x0;
	w100_pwr_state.clk_pin_cntl.f.xtalin_dbl_en = 0x0;	/* no freq doubling */
	w100_pwr_state.clk_pin_cntl.f.cg_debug = 0x0;
	writel((u32) (w100_pwr_state.clk_pin_cntl.val), remapped_regs + mmCLK_PIN_CNTL);

	w100_pwr_state.sclk_cntl.f.sclk_src_sel = 0x0;	/* Crystal Clk */
	w100_pwr_state.sclk_cntl.f.sclk_post_div_fast = 0x0;	/* Pfast = 1 */
	w100_pwr_state.sclk_cntl.f.sclk_clkon_hys = 0x3;
	w100_pwr_state.sclk_cntl.f.sclk_post_div_slow = 0x0;	/* Pslow = 1 */
	w100_pwr_state.sclk_cntl.f.disp_cg_ok2switch_en = 0x0;
	w100_pwr_state.sclk_cntl.f.sclk_force_reg = 0x0;	/* Dynamic */
	w100_pwr_state.sclk_cntl.f.sclk_force_disp = 0x0;	/* Dynamic */
	w100_pwr_state.sclk_cntl.f.sclk_force_mc = 0x0;	/* Dynamic */
	w100_pwr_state.sclk_cntl.f.sclk_force_extmc = 0x0;	/* Dynamic */
	w100_pwr_state.sclk_cntl.f.sclk_force_cp = 0x0;	/* Dynamic */
	w100_pwr_state.sclk_cntl.f.sclk_force_e2 = 0x0;	/* Dynamic */
	w100_pwr_state.sclk_cntl.f.sclk_force_e3 = 0x0;	/* Dynamic */
	w100_pwr_state.sclk_cntl.f.sclk_force_idct = 0x0;	/* Dynamic */
	w100_pwr_state.sclk_cntl.f.sclk_force_bist = 0x0;	/* Dynamic */
	w100_pwr_state.sclk_cntl.f.busy_extend_cp = 0x0;
	w100_pwr_state.sclk_cntl.f.busy_extend_e2 = 0x0;
	w100_pwr_state.sclk_cntl.f.busy_extend_e3 = 0x0;
	w100_pwr_state.sclk_cntl.f.busy_extend_idct = 0x0;
	writel((u32) (w100_pwr_state.sclk_cntl.val), remapped_regs + mmSCLK_CNTL);

	w100_pwr_state.pclk_cntl.f.pclk_src_sel = 0x0;	/* Crystal Clk */
	w100_pwr_state.pclk_cntl.f.pclk_post_div = 0x1;	/* P = 2 */
	w100_pwr_state.pclk_cntl.f.pclk_force_disp = 0x0;	/* Dynamic */
	writel((u32) (w100_pwr_state.pclk_cntl.val), remapped_regs + mmPCLK_CNTL);

	w100_pwr_state.pll_ref_fb_div.f.pll_ref_div = 0x0;	/* M = 1 */
	w100_pwr_state.pll_ref_fb_div.f.pll_fb_div_int = 0x0;	/* N = 1.0 */
	w100_pwr_state.pll_ref_fb_div.f.pll_fb_div_frac = 0x0;
	w100_pwr_state.pll_ref_fb_div.f.pll_reset_time = 0x5;
	w100_pwr_state.pll_ref_fb_div.f.pll_lock_time = 0xff;
	writel((u32) (w100_pwr_state.pll_ref_fb_div.val), remapped_regs + mmPLL_REF_FB_DIV);

	w100_pwr_state.pll_cntl.f.pll_pwdn = 0x1;
	w100_pwr_state.pll_cntl.f.pll_reset = 0x1;
	w100_pwr_state.pll_cntl.f.pll_pm_en = 0x0;
	w100_pwr_state.pll_cntl.f.pll_mode = 0x0;	/* uses VCO clock */
	w100_pwr_state.pll_cntl.f.pll_refclk_sel = 0x0;
	w100_pwr_state.pll_cntl.f.pll_fbclk_sel = 0x0;
	w100_pwr_state.pll_cntl.f.pll_tcpoff = 0x0;
	w100_pwr_state.pll_cntl.f.pll_pcp = 0x4;
	w100_pwr_state.pll_cntl.f.pll_pvg = 0x0;
	w100_pwr_state.pll_cntl.f.pll_vcofr = 0x0;
	w100_pwr_state.pll_cntl.f.pll_ioffset = 0x0;
	w100_pwr_state.pll_cntl.f.pll_pecc_mode = 0x0;
	w100_pwr_state.pll_cntl.f.pll_pecc_scon = 0x0;
	w100_pwr_state.pll_cntl.f.pll_dactal = 0x0;	/* Hi-Z */
	w100_pwr_state.pll_cntl.f.pll_cp_clip = 0x3;
	w100_pwr_state.pll_cntl.f.pll_conf = 0x2;
	w100_pwr_state.pll_cntl.f.pll_mbctrl = 0x2;
	w100_pwr_state.pll_cntl.f.pll_ring_off = 0x0;
	writel((u32) (w100_pwr_state.pll_cntl.val), remapped_regs + mmPLL_CNTL);

	w100_pwr_state.clk_test_cntl.f.testclk_sel = 0x1;	/* PLLCLK (for testing) */
	w100_pwr_state.clk_test_cntl.f.start_check_freq = 0x0;
	w100_pwr_state.clk_test_cntl.f.tstcount_rst = 0x0;
	writel((u32) (w100_pwr_state.clk_test_cntl.val), remapped_regs + mmCLK_TEST_CNTL);

	w100_pwr_state.pwrmgt_cntl.f.pwm_enable = 0x0;
	w100_pwr_state.pwrmgt_cntl.f.pwm_mode_req = 0x1;	/* normal mode (0, 1, 3) */
	w100_pwr_state.pwrmgt_cntl.f.pwm_wakeup_cond = 0x0;
	w100_pwr_state.pwrmgt_cntl.f.pwm_fast_noml_hw_en = 0x0;
	w100_pwr_state.pwrmgt_cntl.f.pwm_noml_fast_hw_en = 0x0;
	w100_pwr_state.pwrmgt_cntl.f.pwm_fast_noml_cond = 0x1;	/* PM4,ENG */
	w100_pwr_state.pwrmgt_cntl.f.pwm_noml_fast_cond = 0x1;	/* PM4,ENG */
	w100_pwr_state.pwrmgt_cntl.f.pwm_idle_timer = 0xFF;
	w100_pwr_state.pwrmgt_cntl.f.pwm_busy_timer = 0xFF;
	writel((u32) (w100_pwr_state.pwrmgt_cntl.val), remapped_regs + mmPWRMGT_CNTL);

	w100_pwr_state.auto_mode = 0;	/* manual mode */
	w100_pwr_state.pwm_mode = 1;	/* normal mode (0, 1, 2) */
	w100_pwr_state.freq = 50000000;	/* 50 MHz */
	w100_pwr_state.M = 3;	/* M = 4 */
	w100_pwr_state.N_int = 6;	/* N = 7.0 */
	w100_pwr_state.N_fac = 0;
	w100_pwr_state.tfgoal = 0xE0;
	w100_pwr_state.lock_time = 56;
	w100_pwr_state.tf20 = 0xff;	/* set highest */
	w100_pwr_state.tf80 = 0x00;	/* set lowest */
	w100_pwr_state.tf100 = 0x00;	/* set lowest */
	w100_pwr_state.fast_sclk = 50;	/* 50.0 MHz */
	w100_pwr_state.norm_sclk = 12;	/* 12.5 MHz */
}


static void w100_init_sharp_lcd(u32 mode)
{
	u32 temp32;
	union disp_db_buf_cntl_wr_u disp_db_buf_wr_cntl;

	/* Prevent display updates */
	disp_db_buf_wr_cntl.f.db_buf_cntl = 0x1e;
	disp_db_buf_wr_cntl.f.update_db_buf = 0;
	disp_db_buf_wr_cntl.f.en_db_buf = 0;
	writel((u32) (disp_db_buf_wr_cntl.val), remapped_regs + mmDISP_DB_BUF_CNTL);

	switch(mode) {
	case LCD_SHARP_QVGA:
		w100_set_slowsysclk(12);	/* use crystal -- 12.5MHz */
		/* not use PLL */

		writel(0x7FFF8000, remapped_regs + mmMC_EXT_MEM_LOCATION);
		writel(0x85FF8000, remapped_regs + mmMC_FB_LOCATION);
		writel(0x00000003, remapped_regs + mmLCD_FORMAT);
		writel(0x00CF1C06, remapped_regs + mmGRAPHIC_CTRL);
		writel(0x01410145, remapped_regs + mmCRTC_TOTAL);
		writel(0x01170027, remapped_regs + mmACTIVE_H_DISP);
		writel(0x01410001, remapped_regs + mmACTIVE_V_DISP);
		writel(0x01170027, remapped_regs + mmGRAPHIC_H_DISP);
		writel(0x01410001, remapped_regs + mmGRAPHIC_V_DISP);
		writel(0x81170027, remapped_regs + mmCRTC_SS);
		writel(0xA0140000, remapped_regs + mmCRTC_LS);
		writel(0x00400008, remapped_regs + mmCRTC_REV);
		writel(0xA0000000, remapped_regs + mmCRTC_DCLK);
		writel(0xC0140014, remapped_regs + mmCRTC_GS);
		writel(0x00010141, remapped_regs + mmCRTC_VPOS_GS);
		writel(0x8015010F, remapped_regs + mmCRTC_GCLK);
		writel(0x80100110, remapped_regs + mmCRTC_GOE);
		writel(0x00000000, remapped_regs + mmCRTC_FRAME);
		writel(0x00000000, remapped_regs + mmCRTC_FRAME_VPOS);
		writel(0x01CC0000, remapped_regs + mmLCDD_CNTL1);
		writel(0x0003FFFF, remapped_regs + mmLCDD_CNTL2);
		writel(0x00FFFF0D, remapped_regs + mmGENLCD_CNTL1);
		writel(0x003F3003, remapped_regs + mmGENLCD_CNTL2);
		writel(0x00000000, remapped_regs + mmCRTC_DEFAULT_COUNT);
		writel(0x0000FF00, remapped_regs + mmLCD_BACKGROUND_COLOR);
		writel(0x000102aa, remapped_regs + mmGENLCD_CNTL3);
		writel(0x00800000, remapped_regs + mmGRAPHIC_OFFSET);
		writel(0x000001e0, remapped_regs + mmGRAPHIC_PITCH);
		writel(0x000000bf, remapped_regs + mmGPIO_DATA);
		writel(0x03c0feff, remapped_regs + mmGPIO_CNTL2);
		writel(0x00000000, remapped_regs + mmGPIO_CNTL1);
		writel(0x41060010, remapped_regs + mmCRTC_PS1_ACTIVE);
		break;
	case LCD_SHARP_VGA:
		w100_set_slowsysclk(12);	/* use crystal -- 12.5MHz */
		w100_set_fastsysclk(current_par->fastsysclk_mode);	/* use PLL -- 75.0MHz */
		w100_pwr_state.pclk_cntl.f.pclk_src_sel = 0x1;
		w100_pwr_state.pclk_cntl.f.pclk_post_div = 0x2;
		writel((u32) (w100_pwr_state.pclk_cntl.val), remapped_regs + mmPCLK_CNTL);
		writel(0x15FF1000, remapped_regs + mmMC_FB_LOCATION);
		writel(0x9FFF8000, remapped_regs + mmMC_EXT_MEM_LOCATION);
		writel(0x00000003, remapped_regs + mmLCD_FORMAT);
		writel(0x00DE1D66, remapped_regs + mmGRAPHIC_CTRL);

		writel(0x0283028B, remapped_regs + mmCRTC_TOTAL);
		writel(0x02360056, remapped_regs + mmACTIVE_H_DISP);
		writel(0x02830003, remapped_regs + mmACTIVE_V_DISP);
		writel(0x02360056, remapped_regs + mmGRAPHIC_H_DISP);
		writel(0x02830003, remapped_regs + mmGRAPHIC_V_DISP);
		writel(0x82360056, remapped_regs + mmCRTC_SS);
		writel(0xA0280000, remapped_regs + mmCRTC_LS);
		writel(0x00400008, remapped_regs + mmCRTC_REV);
		writel(0xA0000000, remapped_regs + mmCRTC_DCLK);
		writel(0x80280028, remapped_regs + mmCRTC_GS);
		writel(0x02830002, remapped_regs + mmCRTC_VPOS_GS);
		writel(0x8015010F, remapped_regs + mmCRTC_GCLK);
		writel(0x80100110, remapped_regs + mmCRTC_GOE);
		writel(0x00000000, remapped_regs + mmCRTC_FRAME);
		writel(0x00000000, remapped_regs + mmCRTC_FRAME_VPOS);
		writel(0x01CC0000, remapped_regs + mmLCDD_CNTL1);
		writel(0x0003FFFF, remapped_regs + mmLCDD_CNTL2);
		writel(0x00FFFF0D, remapped_regs + mmGENLCD_CNTL1);
		writel(0x003F3003, remapped_regs + mmGENLCD_CNTL2);
		writel(0x00000000, remapped_regs + mmCRTC_DEFAULT_COUNT);
		writel(0x0000FF00, remapped_regs + mmLCD_BACKGROUND_COLOR);
		writel(0x000102aa, remapped_regs + mmGENLCD_CNTL3);
		writel(0x00800000, remapped_regs + mmGRAPHIC_OFFSET);
		writel(0x000003C0, remapped_regs + mmGRAPHIC_PITCH);
		writel(0x000000bf, remapped_regs + mmGPIO_DATA);
		writel(0x03c0feff, remapped_regs + mmGPIO_CNTL2);
		writel(0x00000000, remapped_regs + mmGPIO_CNTL1);
		writel(0x41060010, remapped_regs + mmCRTC_PS1_ACTIVE);
		break;
	default:
		break;
	}

	/* Hack for overlay in ext memory */
	temp32 = readl(remapped_regs + mmDISP_DEBUG2);
	temp32 |= 0xc0000000;
	writel(temp32, remapped_regs + mmDISP_DEBUG2);

	/* Re-enable display updates */
	disp_db_buf_wr_cntl.f.db_buf_cntl = 0x1e;
	disp_db_buf_wr_cntl.f.update_db_buf = 1;
	disp_db_buf_wr_cntl.f.en_db_buf = 1;
	writel((u32) (disp_db_buf_wr_cntl.val), remapped_regs + mmDISP_DB_BUF_CNTL);
}


static void w100_set_vga_rotation_regs(u16 divider, unsigned long ctrl, unsigned long offset, unsigned long pitch)
{
	w100_pwr_state.pclk_cntl.f.pclk_src_sel = 0x1;
	w100_pwr_state.pclk_cntl.f.pclk_post_div = divider;
	writel((u32) (w100_pwr_state.pclk_cntl.val), remapped_regs + mmPCLK_CNTL);

	writel(ctrl, remapped_regs + mmGRAPHIC_CTRL);
	writel(offset, remapped_regs + mmGRAPHIC_OFFSET);
	writel(pitch, remapped_regs + mmGRAPHIC_PITCH);

	/* Re-enable display updates */
	writel(0x0000007b, remapped_regs + mmDISP_DB_BUF_CNTL);
}


static void w100_init_vga_rotation(u16 deg)
{
	switch(deg) {
	case 0:
		w100_set_vga_rotation_regs(0x02, 0x00DE1D66, 0x00800000, 0x000003c0);
		break;
	case 90:
		w100_set_vga_rotation_regs(0x06, 0x00DE1D0e, 0x00895b00, 0x00000500);
		break;
	case 180:
		w100_set_vga_rotation_regs(0x02, 0x00DE1D7e, 0x00895ffc, 0x000003c0);
		break;
	case 270:
		w100_set_vga_rotation_regs(0x06, 0x00DE1D16, 0x008004fc, 0x00000500);
		break;
	default:
		/* not-support */
		break;
	}
}


static void w100_set_qvga_rotation_regs(unsigned long ctrl, unsigned long offset, unsigned long pitch)
{
	writel(ctrl, remapped_regs + mmGRAPHIC_CTRL);
	writel(offset, remapped_regs + mmGRAPHIC_OFFSET);
	writel(pitch, remapped_regs + mmGRAPHIC_PITCH);

	/* Re-enable display updates */
	writel(0x0000007b, remapped_regs + mmDISP_DB_BUF_CNTL);
}


static void w100_init_qvga_rotation(u16 deg)
{
	switch(deg) {
	case 0:
		w100_set_qvga_rotation_regs(0x00d41c06, 0x00800000, 0x000001e0);
		break;
	case 90:
		w100_set_qvga_rotation_regs(0x00d41c0E, 0x00825580, 0x00000280);
		break;
	case 180:
		w100_set_qvga_rotation_regs(0x00d41c1e, 0x008257fc, 0x000001e0);
		break;
	case 270:
		w100_set_qvga_rotation_regs(0x00d41c16, 0x0080027c, 0x00000280);
		break;
	default:
		/* not-support */
		break;
	}
}


static void w100_suspend(u32 mode)
{
	u32 val;

	writel(0x7FFF8000, remapped_regs + mmMC_EXT_MEM_LOCATION);
	writel(0x00FF0000, remapped_regs + mmMC_PERF_MON_CNTL);

	val = readl(remapped_regs + mmMEM_EXT_TIMING_CNTL);
	val &= ~(0x00100000);	/* bit20=0 */
	val |= 0xFF000000;	/* bit31:24=0xff */
	writel(val, remapped_regs + mmMEM_EXT_TIMING_CNTL);

	val = readl(remapped_regs + mmMEM_EXT_CNTL);
	val &= ~(0x00040000);	/* bit18=0 */
	val |= 0x00080000;	/* bit19=1 */
	writel(val, remapped_regs + mmMEM_EXT_CNTL);

	udelay(1);		/* wait 1us */

	if (mode == W100_SUSPEND_EXTMEM) {

		/* CKE: Tri-State */
		val = readl(remapped_regs + mmMEM_EXT_CNTL);
		val |= 0x40000000;	/* bit30=1 */
		writel(val, remapped_regs + mmMEM_EXT_CNTL);

		/* CLK: Stop */
		val = readl(remapped_regs + mmMEM_EXT_CNTL);
		val &= ~(0x00000001);	/* bit0=0 */
		writel(val, remapped_regs + mmMEM_EXT_CNTL);
	} else {

		writel(0x00000000, remapped_regs + mmSCLK_CNTL);
		writel(0x000000BF, remapped_regs + mmCLK_PIN_CNTL);
		writel(0x00000015, remapped_regs + mmPWRMGT_CNTL);

		udelay(5);

		val = readl(remapped_regs + mmPLL_CNTL);
		val |= 0x00000004;	/* bit2=1 */
		writel(val, remapped_regs + mmPLL_CNTL);
		writel(0x0000001d, remapped_regs + mmPWRMGT_CNTL);
	}
}


static void w100_resume(void)
{
	u32 temp32;

	w100_hw_init();
	w100_pwm_setup();

	temp32 = readl(remapped_regs + mmDISP_DEBUG2);
	temp32 &= 0xff7fffff;
	temp32 |= 0x00800000;
	writel(temp32, remapped_regs + mmDISP_DEBUG2);

	if (current_par->lcdMode == LCD_MODE_480 || current_par->lcdMode == LCD_MODE_640) {
		w100_init_sharp_lcd(LCD_SHARP_VGA);
		if (current_par->lcdMode == LCD_MODE_640) {
			w100_init_vga_rotation(current_par->rotation_flag ? 270 : 90);
		}
	} else {
		w100_init_sharp_lcd(LCD_SHARP_QVGA);
		if (current_par->lcdMode == LCD_MODE_320) {
			w100_init_qvga_rotation(current_par->rotation_flag ? 270 : 90);
		}
	}
}


static void w100_vsync(void)
{
	u32 tmp;
	int timeout = 30000; /* VSync timeout = 30[ms] > 16.8[ms] */

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


static void w100_InitExtMem(u32 mode)
{
	switch(mode) {
	case LCD_SHARP_QVGA:
		/* QVGA doesn't use external memory
		   nothing to do, really. */
		break;
	case LCD_SHARP_VGA:
		writel(0x00007800, remapped_regs + mmMC_BIST_CTRL);
		writel(0x00040003, remapped_regs + mmMEM_EXT_CNTL);
		writel(0x00200021, remapped_regs + mmMEM_SDRAM_MODE_REG);
		udelay(100);
		writel(0x80200021, remapped_regs + mmMEM_SDRAM_MODE_REG);
		udelay(100);
		writel(0x00650021, remapped_regs + mmMEM_SDRAM_MODE_REG);
		udelay(100);
		writel(0x10002a4a, remapped_regs + mmMEM_EXT_TIMING_CNTL);
		writel(0x7ff87012, remapped_regs + mmMEM_IO_CNTL);
		break;
	default:
		break;
	}
}


#define RESCTL_ADRS     0x00
#define PHACTRL_ADRS	0x01
#define DUTYCTRL_ADRS	0x02
#define POWERREG0_ADRS	0x03
#define POWERREG1_ADRS	0x04
#define GPOR3_ADRS		0x05
#define PICTRL_ADRS     0x06
#define POLCTRL_ADRS    0x07

#define RESCTL_QVGA     0x01
#define RESCTL_VGA      0x00

#define POWER1_VW_ON	0x01	/* VW Supply FET ON */
#define POWER1_GVSS_ON	0x02	/* GVSS(-8V) Power Supply ON */
#define POWER1_VDD_ON	0x04	/* VDD(8V),SVSS(-4V) Power Supply ON */

#define POWER1_VW_OFF	0x00	/* VW Supply FET OFF */
#define POWER1_GVSS_OFF	0x00	/* GVSS(-8V) Power Supply OFF */
#define POWER1_VDD_OFF	0x00	/* VDD(8V),SVSS(-4V) Power Supply OFF */

#define POWER0_COM_DCLK	0x01	/* COM Voltage DC Bias DAC Serial Data Clock */
#define POWER0_COM_DOUT	0x02	/* COM Voltage DC Bias DAC Serial Data Out */
#define POWER0_DAC_ON	0x04	/* DAC Power Supply ON */
#define POWER0_COM_ON	0x08	/* COM Powewr Supply ON */
#define POWER0_VCC5_ON	0x10	/* VCC5 Power Supply ON */

#define POWER0_DAC_OFF	0x00	/* DAC Power Supply OFF */
#define POWER0_COM_OFF	0x00	/* COM Powewr Supply OFF */
#define POWER0_VCC5_OFF	0x00	/* VCC5 Power Supply OFF */

#define PICTRL_INIT_STATE	0x01
#define PICTRL_INIOFF		0x02
#define PICTRL_POWER_DOWN	0x04
#define PICTRL_COM_SIGNAL_OFF	0x08
#define PICTRL_DAC_SIGNAL_OFF	0x10

#define PICTRL_POWER_ACTIVE	(0)

#define POLCTRL_SYNC_POL_FALL	0x01
#define POLCTRL_EN_POL_FALL	0x02
#define POLCTRL_DATA_POL_FALL	0x04
#define POLCTRL_SYNC_ACT_H	0x08
#define POLCTRL_EN_ACT_L	0x10

#define POLCTRL_SYNC_POL_RISE	0x00
#define POLCTRL_EN_POL_RISE	0x00
#define POLCTRL_DATA_POL_RISE	0x00
#define POLCTRL_SYNC_ACT_L	0x00
#define POLCTRL_EN_ACT_H	0x00

#define PHACTRL_PHASE_MANUAL	0x01

#define PHAD_QVGA_DEFAULT_VAL (9)
#define COMADJ_DEFAULT        (125)

static void lcdtg_ssp_send(u8 adrs, u8 data)
{
	w100fb_ssp_send(adrs,data);
}

/*
 * This is only a psuedo I2C interface. We can't use the standard kernel
 * routines as the interface is write only. We just assume the data is acked...
 */
static void lcdtg_ssp_i2c_send(u8 data)
{
	lcdtg_ssp_send(POWERREG0_ADRS, data);
	udelay(10);
}

static void lcdtg_i2c_send_bit(u8 data)
{
	lcdtg_ssp_i2c_send(data);
	lcdtg_ssp_i2c_send(data | POWER0_COM_DCLK);
	lcdtg_ssp_i2c_send(data);
}

static void lcdtg_i2c_send_start(u8 base)
{
	lcdtg_ssp_i2c_send(base | POWER0_COM_DCLK | POWER0_COM_DOUT);
	lcdtg_ssp_i2c_send(base | POWER0_COM_DCLK);
	lcdtg_ssp_i2c_send(base);
}

static void lcdtg_i2c_send_stop(u8 base)
{
	lcdtg_ssp_i2c_send(base);
	lcdtg_ssp_i2c_send(base | POWER0_COM_DCLK);
	lcdtg_ssp_i2c_send(base | POWER0_COM_DCLK | POWER0_COM_DOUT);
}

static void lcdtg_i2c_send_byte(u8 base, u8 data)
{
	int i;
	for (i = 0; i < 8; i++) {
		if (data & 0x80)
			lcdtg_i2c_send_bit(base | POWER0_COM_DOUT);
		else
			lcdtg_i2c_send_bit(base);
		data <<= 1;
	}
}

static void lcdtg_i2c_wait_ack(u8 base)
{
	lcdtg_i2c_send_bit(base);
}

static void lcdtg_set_common_voltage(u8 base_data, u8 data)
{
	/* Set Common Voltage to M62332FP via I2C */
	lcdtg_i2c_send_start(base_data);
	lcdtg_i2c_send_byte(base_data, 0x9c);
	lcdtg_i2c_wait_ack(base_data);
	lcdtg_i2c_send_byte(base_data, 0x00);
	lcdtg_i2c_wait_ack(base_data);
	lcdtg_i2c_send_byte(base_data, data);
	lcdtg_i2c_wait_ack(base_data);
	lcdtg_i2c_send_stop(base_data);
}

static struct lcdtg_register_setting {
	u8 adrs;
	u8 data;
	u32 wait;
} lcdtg_power_on_table[] = {

    /* Initialize Internal Logic & Port */
    { PICTRL_ADRS,
      PICTRL_POWER_DOWN | PICTRL_INIOFF | PICTRL_INIT_STATE |
      PICTRL_COM_SIGNAL_OFF | PICTRL_DAC_SIGNAL_OFF,
      0 },

    { POWERREG0_ADRS,
      POWER0_COM_DCLK | POWER0_COM_DOUT | POWER0_DAC_OFF | POWER0_COM_OFF |
      POWER0_VCC5_OFF,
      0 },

    { POWERREG1_ADRS,
      POWER1_VW_OFF | POWER1_GVSS_OFF | POWER1_VDD_OFF,
      0 },

    /* VDD(+8V),SVSS(-4V) ON */
    { POWERREG1_ADRS,
      POWER1_VW_OFF | POWER1_GVSS_OFF | POWER1_VDD_ON /* VDD ON */,
      3000 },

    /* DAC ON */
    { POWERREG0_ADRS,
      POWER0_COM_DCLK | POWER0_COM_DOUT | POWER0_DAC_ON /* DAC ON */ |
      POWER0_COM_OFF | POWER0_VCC5_OFF,
      0 },

    /* INIB = H, INI = L  */
    { PICTRL_ADRS,
      /* PICTL[0] = H , PICTL[1] = PICTL[2] = PICTL[4] = L */
      PICTRL_INIT_STATE | PICTRL_COM_SIGNAL_OFF,
      0 },

    /* Set Common Voltage */
    { 0xfe, 0, 0 },

    /* VCC5 ON */
    { POWERREG0_ADRS,
      POWER0_COM_DCLK | POWER0_COM_DOUT | POWER0_DAC_ON /* DAC ON */ |
      POWER0_COM_OFF | POWER0_VCC5_ON /* VCC5 ON */,
      0 },

    /* GVSS(-8V) ON */
    { POWERREG1_ADRS,
      POWER1_VW_OFF | POWER1_GVSS_ON /* GVSS ON */ |
      POWER1_VDD_ON /* VDD ON */,
      2000 },

    /* COM SIGNAL ON (PICTL[3] = L) */
    { PICTRL_ADRS,
      PICTRL_INIT_STATE,
      0 },

    /* COM ON */
    { POWERREG0_ADRS,
      POWER0_COM_DCLK | POWER0_COM_DOUT | POWER0_DAC_ON /* DAC ON */ |
      POWER0_COM_ON /* COM ON */ | POWER0_VCC5_ON /* VCC5_ON */,
      0 },

    /* VW ON */
    { POWERREG1_ADRS,
      POWER1_VW_ON /* VW ON */ | POWER1_GVSS_ON /* GVSS ON */ |
      POWER1_VDD_ON /* VDD ON */,
      0 /* Wait 100ms */ },

    /* Signals output enable */
    { PICTRL_ADRS,
      0 /* Signals output enable */,
      0 },

    { PHACTRL_ADRS,
      PHACTRL_PHASE_MANUAL,
      0 },

    /* Initialize for Input Signals from ATI */
    { POLCTRL_ADRS,
      POLCTRL_SYNC_POL_RISE | POLCTRL_EN_POL_RISE | POLCTRL_DATA_POL_RISE |
      POLCTRL_SYNC_ACT_L | POLCTRL_EN_ACT_H,
      1000 /*100000*/ /* Wait 100ms */ },

    /* end mark */
    { 0xff, 0, 0 }
};

static void lcdtg_resume(void)
{
	if (current_par->lcdMode == LCD_MODE_480 || current_par->lcdMode == LCD_MODE_640) {
		lcdtg_hw_init(LCD_SHARP_VGA);
	} else {
		lcdtg_hw_init(LCD_SHARP_QVGA);
	}
}

static void lcdtg_suspend(void)
{
	int i;

	for (i = 0; i < (current_par->xres * current_par->yres); i++) {
		writew(0xffff, remapped_fbuf + (2*i));
	}

	/* 60Hz x 2 frame = 16.7msec x 2 = 33.4 msec */
	mdelay(34);

	/* (1)VW OFF */
	lcdtg_ssp_send(POWERREG1_ADRS, POWER1_VW_OFF | POWER1_GVSS_ON | POWER1_VDD_ON);

	/* (2)COM OFF */
	lcdtg_ssp_send(PICTRL_ADRS, PICTRL_COM_SIGNAL_OFF);
	lcdtg_ssp_send(POWERREG0_ADRS, POWER0_DAC_ON | POWER0_COM_OFF | POWER0_VCC5_ON);

	/* (3)Set Common Voltage Bias 0V */
	lcdtg_set_common_voltage(POWER0_DAC_ON | POWER0_COM_OFF | POWER0_VCC5_ON, 0);

	/* (4)GVSS OFF */
	lcdtg_ssp_send(POWERREG1_ADRS, POWER1_VW_OFF | POWER1_GVSS_OFF | POWER1_VDD_ON);

	/* (5)VCC5 OFF */
	lcdtg_ssp_send(POWERREG0_ADRS, POWER0_DAC_ON | POWER0_COM_OFF | POWER0_VCC5_OFF);

	/* (6)Set PDWN, INIOFF, DACOFF */
	lcdtg_ssp_send(PICTRL_ADRS, PICTRL_INIOFF | PICTRL_DAC_SIGNAL_OFF |
			PICTRL_POWER_DOWN | PICTRL_COM_SIGNAL_OFF);

	/* (7)DAC OFF */
	lcdtg_ssp_send(POWERREG0_ADRS, POWER0_DAC_OFF | POWER0_COM_OFF | POWER0_VCC5_OFF);

	/* (8)VDD OFF */
	lcdtg_ssp_send(POWERREG1_ADRS, POWER1_VW_OFF | POWER1_GVSS_OFF | POWER1_VDD_OFF);

}

static void lcdtg_set_phadadj(u32 mode)
{
	int adj;

	if (mode == LCD_SHARP_VGA) {
		/* Setting for VGA */
		adj = current_par->phadadj;
		if (adj < 0) {
			adj = PHACTRL_PHASE_MANUAL;
		} else {
			adj = ((adj & 0x0f) << 1) | PHACTRL_PHASE_MANUAL;
		}
	} else {
		/* Setting for QVGA */
		adj = (PHAD_QVGA_DEFAULT_VAL << 1) | PHACTRL_PHASE_MANUAL;
	}
	lcdtg_ssp_send(PHACTRL_ADRS, adj);
}

static void lcdtg_hw_init(u32 mode)
{
	int i;
	int comadj;

	i = 0;
	while(lcdtg_power_on_table[i].adrs != 0xff) {
		if (lcdtg_power_on_table[i].adrs == 0xfe) {
			/* Set Common Voltage */
			comadj = current_par->comadj;
			if (comadj < 0) {
				comadj = COMADJ_DEFAULT;
			}
			lcdtg_set_common_voltage((POWER0_DAC_ON | POWER0_COM_OFF | POWER0_VCC5_OFF), comadj);
		} else if (lcdtg_power_on_table[i].adrs == PHACTRL_ADRS) {
			/* Set Phase Adjuct */
			lcdtg_set_phadadj(mode);
		} else {
			/* Other */
			lcdtg_ssp_send(lcdtg_power_on_table[i].adrs, lcdtg_power_on_table[i].data);
		}
		if (lcdtg_power_on_table[i].wait != 0)
			udelay(lcdtg_power_on_table[i].wait);
		i++;
	}

	switch(mode) {
	case LCD_SHARP_QVGA:
		/* Set Lcd Resolution (QVGA) */
		lcdtg_ssp_send(RESCTL_ADRS, RESCTL_QVGA);
		break;
	case LCD_SHARP_VGA:
		/* Set Lcd Resolution (VGA) */
		lcdtg_ssp_send(RESCTL_ADRS, RESCTL_VGA);
		break;
	default:
		break;
	}
}

static void lcdtg_lcd_change(u32 mode)
{
	/* Set Phase Adjuct */
	lcdtg_set_phadadj(mode);

	if (mode == LCD_SHARP_VGA)
		/* Set Lcd Resolution (VGA) */
		lcdtg_ssp_send(RESCTL_ADRS, RESCTL_VGA);
	else if (mode == LCD_SHARP_QVGA)
		/* Set Lcd Resolution (QVGA) */
		lcdtg_ssp_send(RESCTL_ADRS, RESCTL_QVGA);
}


static struct device_driver w100fb_driver = {
	.name		= "w100fb",
	.bus		= &platform_bus_type,
	.probe		= w100fb_probe,
	.remove		= w100fb_remove,
	.suspend	= w100fb_suspend,
	.resume		= w100fb_resume,
};

int __devinit w100fb_init(void)
{
	return driver_register(&w100fb_driver);
}

void __exit w100fb_cleanup(void)
{
 	driver_unregister(&w100fb_driver);
}

module_init(w100fb_init);
module_exit(w100fb_cleanup);

MODULE_DESCRIPTION("ATI Imageon w100 framebuffer driver");
MODULE_LICENSE("GPLv2");
