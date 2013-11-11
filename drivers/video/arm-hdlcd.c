/*
 * drivers/video/arm-hdlcd.c
 *
 * Copyright (C) 2011 ARM Limited
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 *  ARM HDLCD Controller
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/fb.h>
#include <linux/clk.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/memblock.h>
#include <linux/arm-hdlcd.h>
#ifdef HDLCD_COUNT_BUFFERUNDERRUNS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#endif

#include "edid.h"

#ifdef CONFIG_SERIAL_AMBA_PCU_UART
int get_edid(u8 *msgbuf);
#else
#endif

#define to_hdlcd_device(info)	container_of(info, struct hdlcd_device, fb)

static struct of_device_id  hdlcd_of_matches[] = {
	{ .compatible	= "arm,hdlcd" },
	{},
};

/* Framebuffer size.  */
static unsigned long framebuffer_size;

#ifdef HDLCD_COUNT_BUFFERUNDERRUNS
static unsigned long buffer_underrun_events;
static DEFINE_SPINLOCK(hdlcd_underrun_lock);

static void hdlcd_underrun_set(unsigned long val)
{
	spin_lock(&hdlcd_underrun_lock);
	buffer_underrun_events = val;
	spin_unlock(&hdlcd_underrun_lock);
}

static unsigned long hdlcd_underrun_get(void)
{
	unsigned long val;
	spin_lock(&hdlcd_underrun_lock);
	val = buffer_underrun_events;
	spin_unlock(&hdlcd_underrun_lock);
	return val;
}

#ifdef CONFIG_PROC_FS
static int hdlcd_underrun_show(struct seq_file *m, void *v)
{
	unsigned char underrun_string[32];
	snprintf(underrun_string, 32, "%lu\n", hdlcd_underrun_get());
	seq_puts(m, underrun_string);
	return 0;
}

static int proc_hdlcd_underrun_open(struct inode *inode, struct file *file)
{
	return single_open(file, hdlcd_underrun_show, NULL);
}

static const struct file_operations proc_hdlcd_underrun_operations = {
	.open		= proc_hdlcd_underrun_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int hdlcd_underrun_init(void)
{
	hdlcd_underrun_set(0);
	proc_create("hdlcd_underrun", 0, NULL, &proc_hdlcd_underrun_operations);
	return 0;
}
static void hdlcd_underrun_close(void)
{
	remove_proc_entry("hdlcd_underrun", NULL);
}
#else
static int hdlcd_underrun_init(void) { return 0; }
static void hdlcd_underrun_close(void) { }
#endif
#endif

static char *fb_mode = "1680x1050-32@60\0\0\0\0\0";

static struct fb_var_screeninfo cached_var_screeninfo;

static struct fb_videomode hdlcd_default_mode = {
	.refresh	= 60,
	.xres		= 1680,
	.yres		= 1050,
	.pixclock	= 8403,
	.left_margin	= 80,
	.right_margin	= 48,
	.upper_margin	= 21,
	.lower_margin	= 3,
	.hsync_len	= 32,
	.vsync_len	= 6,
	.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.vmode		= FB_VMODE_NONINTERLACED
};

static inline void hdlcd_enable(struct hdlcd_device *hdlcd)
{
	dev_dbg(hdlcd->dev, "HDLCD: output enabled\n");
	writel(1, hdlcd->base + HDLCD_REG_COMMAND);
}

static inline void hdlcd_disable(struct hdlcd_device *hdlcd)
{
	dev_dbg(hdlcd->dev, "HDLCD: output disabled\n");
	writel(0, hdlcd->base + HDLCD_REG_COMMAND);
}

static int hdlcd_set_bitfields(struct hdlcd_device *hdlcd,
				struct fb_var_screeninfo *var)
{
	int ret = 0;

	memset(&var->transp, 0, sizeof(var->transp));
	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->blue.offset = 0;

	switch (var->bits_per_pixel) {
	case 8:
		/* pseudocolor */
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		break;
	case 16:
		/* 565 format */
		var->red.length = 5;
		var->green.length = 6;
		var->blue.length = 5;
		break;
	case 32:
		var->transp.length = 8;
	case 24:
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (!ret) {
		if(var->bits_per_pixel != 32)
		{
			var->green.offset = var->blue.length;
			var->red.offset = var->green.offset + var->green.length;
		}
		else
		{
			/* Previously, the byte ordering for 32-bit color was
			 * (msb)<alpha><red><green><blue>(lsb)
			 * but this does not match what android expects and
			 * the colors are odd. Instead, use
			 * <alpha><blue><green><red>
			 * Since we tell fb what we are doing, console
			 * , X and directfb access should work fine.
			 */
			var->green.offset = var->red.length;
			var->blue.offset = var->green.offset + var->green.length;
			var->transp.offset = var->blue.offset + var->blue.length;
		}
	}

	return ret;
}

static int hdlcd_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct hdlcd_device *hdlcd = to_hdlcd_device(info);
	int bytes_per_pixel = var->bits_per_pixel / 8;

#ifdef HDLCD_NO_VIRTUAL_SCREEN
	var->yres_virtual = var->yres;
#else
	var->yres_virtual = 2 * var->yres;
#endif

	if ((var->xres_virtual * bytes_per_pixel * var->yres_virtual) > hdlcd->fb.fix.smem_len)
		return -ENOMEM;

	if (var->xres > HDLCD_MAX_XRES || var->yres > HDLCD_MAX_YRES)
		return -EINVAL;

	/* make sure the bitfields are set appropriately */
	return hdlcd_set_bitfields(hdlcd, var);
}

/* prototype */
static int hdlcd_pan_display(struct fb_var_screeninfo *var,
	struct fb_info *info);

#define WRITE_HDLCD_REG(reg, value)	writel((value), hdlcd->base + (reg))
#define READ_HDLCD_REG(reg)		readl(hdlcd->base + (reg))

static int hdlcd_set_par(struct fb_info *info)
{
	struct hdlcd_device *hdlcd = to_hdlcd_device(info);
	int bytes_per_pixel = hdlcd->fb.var.bits_per_pixel / 8;
	int polarities;
	int old_yoffset;

	/* check for shortcuts */
	old_yoffset = cached_var_screeninfo.yoffset;
	cached_var_screeninfo.yoffset = info->var.yoffset;
	if (!memcmp(&info->var, &cached_var_screeninfo,
				sizeof(struct fb_var_screeninfo))) {
		if(old_yoffset != info->var.yoffset) {
			/* we only changed yoffset, and we already
			 * already recorded it a couple lines up
			 */
			hdlcd_pan_display(&info->var, info);
		}
		/* or no change */
		return 0;
	}

	hdlcd->fb.fix.line_length = hdlcd->fb.var.xres * bytes_per_pixel;

	if (hdlcd->fb.var.bits_per_pixel >= 16)
		hdlcd->fb.fix.visual = FB_VISUAL_TRUECOLOR;
	else
		hdlcd->fb.fix.visual = FB_VISUAL_PSEUDOCOLOR;

	memcpy(&cached_var_screeninfo, &info->var, sizeof(struct fb_var_screeninfo));

	polarities = HDLCD_POLARITY_DATAEN |
#ifndef CONFIG_ARCH_TUSCAN
		HDLCD_POLARITY_PIXELCLK |
#endif
		HDLCD_POLARITY_DATA;
	polarities |= (hdlcd->fb.var.sync & FB_SYNC_HOR_HIGH_ACT) ? HDLCD_POLARITY_HSYNC : 0;
	polarities |= (hdlcd->fb.var.sync & FB_SYNC_VERT_HIGH_ACT) ? HDLCD_POLARITY_VSYNC : 0;

	hdlcd_disable(hdlcd);

	WRITE_HDLCD_REG(HDLCD_REG_FB_LINE_LENGTH, hdlcd->fb.var.xres * bytes_per_pixel);
	WRITE_HDLCD_REG(HDLCD_REG_FB_LINE_PITCH, hdlcd->fb.var.xres * bytes_per_pixel);
	WRITE_HDLCD_REG(HDLCD_REG_FB_LINE_COUNT, hdlcd->fb.var.yres - 1);
	WRITE_HDLCD_REG(HDLCD_REG_V_SYNC, hdlcd->fb.var.vsync_len - 1);
	WRITE_HDLCD_REG(HDLCD_REG_V_BACK_PORCH, hdlcd->fb.var.upper_margin - 1);
	WRITE_HDLCD_REG(HDLCD_REG_V_DATA, hdlcd->fb.var.yres - 1);
	WRITE_HDLCD_REG(HDLCD_REG_V_FRONT_PORCH, hdlcd->fb.var.lower_margin - 1);
	WRITE_HDLCD_REG(HDLCD_REG_H_SYNC, hdlcd->fb.var.hsync_len - 1);
	WRITE_HDLCD_REG(HDLCD_REG_H_BACK_PORCH, hdlcd->fb.var.left_margin - 1);
	WRITE_HDLCD_REG(HDLCD_REG_H_DATA, hdlcd->fb.var.xres - 1);
	WRITE_HDLCD_REG(HDLCD_REG_H_FRONT_PORCH, hdlcd->fb.var.right_margin - 1);
	WRITE_HDLCD_REG(HDLCD_REG_POLARITIES, polarities);
	WRITE_HDLCD_REG(HDLCD_REG_PIXEL_FORMAT, (bytes_per_pixel - 1) << 3);
#ifdef HDLCD_RED_DEFAULT_COLOUR
	WRITE_HDLCD_REG(HDLCD_REG_RED_SELECT, (0x00ff0000 | (hdlcd->fb.var.red.length & 0xf) << 8) \
													  | hdlcd->fb.var.red.offset);
#else
	WRITE_HDLCD_REG(HDLCD_REG_RED_SELECT, ((hdlcd->fb.var.red.length & 0xf) << 8) | hdlcd->fb.var.red.offset);
#endif
	WRITE_HDLCD_REG(HDLCD_REG_GREEN_SELECT, ((hdlcd->fb.var.green.length & 0xf) << 8) | hdlcd->fb.var.green.offset);
	WRITE_HDLCD_REG(HDLCD_REG_BLUE_SELECT, ((hdlcd->fb.var.blue.length & 0xf) << 8) | hdlcd->fb.var.blue.offset);

	clk_set_rate(hdlcd->clk, (1000000000 / hdlcd->fb.var.pixclock) * 1000);
	clk_enable(hdlcd->clk);

	hdlcd_enable(hdlcd);

	return 0;
}

static int hdlcd_setcolreg(unsigned int regno, unsigned int red, unsigned int green,
		unsigned int blue, unsigned int transp, struct fb_info *info)
{
	if (regno < 16) {
		u32 *pal = info->pseudo_palette;

		pal[regno] = ((red >> 8) << info->var.red.offset) |
			((green >> 8) << info->var.green.offset) |
			((blue >> 8) << info->var.blue.offset);
	}

	return 0;
}

static irqreturn_t hdlcd_irq(int irq, void *data)
{
	struct hdlcd_device *hdlcd = data;
	unsigned long irq_mask, irq_status;

	irq_mask = READ_HDLCD_REG(HDLCD_REG_INT_MASK);
	irq_status = READ_HDLCD_REG(HDLCD_REG_INT_STATUS);

	/* acknowledge interrupt(s) */
	WRITE_HDLCD_REG(HDLCD_REG_INT_CLEAR, irq_status);
#ifdef HDLCD_COUNT_BUFFERUNDERRUNS
	if (irq_status & HDLCD_INTERRUPT_UNDERRUN) {
		/* increment the count */
		hdlcd_underrun_set(hdlcd_underrun_get() + 1);
	}
#endif
	if (irq_status & HDLCD_INTERRUPT_VSYNC) {
		/* disable future VSYNC interrupts */
		WRITE_HDLCD_REG(HDLCD_REG_INT_MASK, irq_mask & ~HDLCD_INTERRUPT_VSYNC);

		complete(&hdlcd->vsync_completion);
	}

	return IRQ_HANDLED;
}

static int hdlcd_wait_for_vsync(struct fb_info *info)
{
	struct hdlcd_device *hdlcd = to_hdlcd_device(info);
	unsigned long irq_mask;
	int err;

	/* enable VSYNC interrupt */
	irq_mask = READ_HDLCD_REG(HDLCD_REG_INT_MASK);
	WRITE_HDLCD_REG(HDLCD_REG_INT_MASK, irq_mask | HDLCD_INTERRUPT_VSYNC);

	err = wait_for_completion_interruptible_timeout(&hdlcd->vsync_completion,
							msecs_to_jiffies(100));

	if (!err)
		return -ETIMEDOUT;

	return 0;
}

static int hdlcd_blank(int blank_mode, struct fb_info *info)
{
	struct hdlcd_device *hdlcd = to_hdlcd_device(info);

	switch (blank_mode) {
	case FB_BLANK_POWERDOWN:
		clk_disable(hdlcd->clk);
	case FB_BLANK_NORMAL:
		hdlcd_disable(hdlcd);
		break;
	case FB_BLANK_UNBLANK:
		clk_enable(hdlcd->clk);
		hdlcd_enable(hdlcd);
		break;
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	default:
		return 1;
	}

	return 0;
}

static void hdlcd_mmap_open(struct vm_area_struct *vma)
{
}

static void hdlcd_mmap_close(struct vm_area_struct *vma)
{
}

static struct vm_operations_struct hdlcd_mmap_ops = {
	.open	= hdlcd_mmap_open,
	.close	= hdlcd_mmap_close,
};

static int hdlcd_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct hdlcd_device *hdlcd = to_hdlcd_device(info);
	unsigned long off;
	unsigned long start;
	unsigned long len = hdlcd->fb.fix.smem_len;

	if (vma->vm_end - vma->vm_start == 0)
		return 0;
	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT))
		return -EINVAL;

	off = vma->vm_pgoff << PAGE_SHIFT;
	if ((off >= len) || (vma->vm_end - vma->vm_start + off) > len)
		return -EINVAL;

	start = hdlcd->fb.fix.smem_start;
	off += start;

	vma->vm_pgoff = off >> PAGE_SHIFT;
	vma->vm_flags |= VM_IO;
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	vma->vm_ops = &hdlcd_mmap_ops;
	if (io_remap_pfn_range(vma, vma->vm_start, off >> PAGE_SHIFT,
				vma->vm_end - vma->vm_start,
				vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

static int hdlcd_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct hdlcd_device *hdlcd = to_hdlcd_device(info);

	hdlcd->fb.var.yoffset = var->yoffset;
	WRITE_HDLCD_REG(HDLCD_REG_FB_BASE, hdlcd->fb.fix.smem_start +
			(var->yoffset * hdlcd->fb.fix.line_length));

	hdlcd_wait_for_vsync(info);

	return 0;
}

static int hdlcd_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	int err;

	switch (cmd) {
	case FBIO_WAITFORVSYNC:
		err = hdlcd_wait_for_vsync(info);
		break;
	default:
		err = -ENOIOCTLCMD;
		break;
	}

	return err;
}

static struct fb_ops hdlcd_ops = {
	.owner			= THIS_MODULE,
	.fb_check_var		= hdlcd_check_var,
	.fb_set_par		= hdlcd_set_par,
	.fb_setcolreg		= hdlcd_setcolreg,
	.fb_blank		= hdlcd_blank,
	.fb_fillrect		= cfb_fillrect,
	.fb_copyarea		= cfb_copyarea,
	.fb_imageblit		= cfb_imageblit,
	.fb_mmap		= hdlcd_mmap,
	.fb_pan_display		= hdlcd_pan_display,
	.fb_ioctl		= hdlcd_ioctl,
	.fb_compat_ioctl	= hdlcd_ioctl
};

static int hdlcd_setup(struct hdlcd_device *hdlcd)
{
	u32 version;
	int err = -EFAULT;

	hdlcd->fb.device = hdlcd->dev;

	hdlcd->clk = clk_get(hdlcd->dev, NULL);
	if (IS_ERR(hdlcd->clk)) {
		dev_err(hdlcd->dev, "HDLCD: unable to find clock data\n");
		return PTR_ERR(hdlcd->clk);
	}

	err = clk_prepare(hdlcd->clk);
	if (err)
		goto clk_prepare_err;

	hdlcd->base = ioremap_nocache(hdlcd->fb.fix.mmio_start, hdlcd->fb.fix.mmio_len);
	if (!hdlcd->base) {
		dev_err(hdlcd->dev, "HDLCD: unable to map registers\n");
		goto remap_err;
	}

	hdlcd->fb.pseudo_palette = kmalloc(sizeof(u32) * 16, GFP_KERNEL);
	if (!hdlcd->fb.pseudo_palette) {
		dev_err(hdlcd->dev, "HDLCD: unable to allocate pseudo_palette memory\n");
		err = -ENOMEM;
		goto kmalloc_err;
	}

	version = readl(hdlcd->base + HDLCD_REG_VERSION);
	if ((version & HDLCD_PRODUCT_MASK) != HDLCD_PRODUCT_ID) {
		dev_err(hdlcd->dev, "HDLCD: unknown product id: 0x%x\n", version);
		err = -EINVAL;
		goto kmalloc_err;
	}
	dev_info(hdlcd->dev, "HDLCD: found ARM HDLCD version r%dp%d\n",
		(version & HDLCD_VERSION_MAJOR_MASK) >> 8,
		version & HDLCD_VERSION_MINOR_MASK);

	strcpy(hdlcd->fb.fix.id, "hdlcd");
	hdlcd->fb.fbops			= &hdlcd_ops;
	hdlcd->fb.flags			= FBINFO_FLAG_DEFAULT/* | FBINFO_VIRTFB*/;

	hdlcd->fb.fix.type		= FB_TYPE_PACKED_PIXELS;
	hdlcd->fb.fix.type_aux		= 0;
	hdlcd->fb.fix.xpanstep		= 0;
	hdlcd->fb.fix.ypanstep		= 1;
	hdlcd->fb.fix.ywrapstep		= 0;
	hdlcd->fb.fix.accel		= FB_ACCEL_NONE;

	hdlcd->fb.var.nonstd		= 0;
	hdlcd->fb.var.activate		= FB_ACTIVATE_NOW;
	hdlcd->fb.var.height		= -1;
	hdlcd->fb.var.width		= -1;
	hdlcd->fb.var.accel_flags	= 0;

	init_completion(&hdlcd->vsync_completion);

	if (hdlcd->edid) {
		/* build modedb from EDID */
		fb_edid_to_monspecs(hdlcd->edid, &hdlcd->fb.monspecs);
		fb_videomode_to_modelist(hdlcd->fb.monspecs.modedb,
					hdlcd->fb.monspecs.modedb_len,
					&hdlcd->fb.modelist);
		fb_find_mode(&hdlcd->fb.var, &hdlcd->fb, fb_mode,
			hdlcd->fb.monspecs.modedb,
			hdlcd->fb.monspecs.modedb_len,
			&hdlcd_default_mode, 32);
	} else {
		hdlcd->fb.monspecs.hfmin	= 0;
		hdlcd->fb.monspecs.hfmax	= 100000;
		hdlcd->fb.monspecs.vfmin	= 0;
		hdlcd->fb.monspecs.vfmax	= 400;
		hdlcd->fb.monspecs.dclkmin	= 1000000;
		hdlcd->fb.monspecs.dclkmax	= 100000000;
		fb_find_mode(&hdlcd->fb.var, &hdlcd->fb, fb_mode, NULL, 0, &hdlcd_default_mode, 32);
	}

	dev_info(hdlcd->dev, "using %dx%d-%d@%d mode\n", hdlcd->fb.var.xres,
		hdlcd->fb.var.yres, hdlcd->fb.var.bits_per_pixel,
		hdlcd->fb.mode ? hdlcd->fb.mode->refresh : 60);
	hdlcd->fb.var.xres_virtual	= hdlcd->fb.var.xres;
#ifdef HDLCD_NO_VIRTUAL_SCREEN
	hdlcd->fb.var.yres_virtual	= hdlcd->fb.var.yres;
#else
	hdlcd->fb.var.yres_virtual	= hdlcd->fb.var.yres * 2;
#endif

	/* initialise and set the palette */
	if (fb_alloc_cmap(&hdlcd->fb.cmap, NR_PALETTE, 0)) {
		dev_err(hdlcd->dev, "failed to allocate cmap memory\n");
		err = -ENOMEM;
		goto setup_err;
	}
	fb_set_cmap(&hdlcd->fb.cmap, &hdlcd->fb);

	/* Allow max number of outstanding requests with the largest beat burst */
	WRITE_HDLCD_REG(HDLCD_REG_BUS_OPTIONS, HDLCD_BUS_MAX_OUTSTAND | HDLCD_BUS_BURST_16);
	/* Set the framebuffer base to start of allocated memory */
	WRITE_HDLCD_REG(HDLCD_REG_FB_BASE, hdlcd->fb.fix.smem_start);
#ifdef HDLCD_COUNT_BUFFERUNDERRUNS
	/* turn on underrun interrupt for counting */
	WRITE_HDLCD_REG(HDLCD_REG_INT_MASK, HDLCD_INTERRUPT_UNDERRUN);
#else
	/* Ensure interrupts are disabled */
	WRITE_HDLCD_REG(HDLCD_REG_INT_MASK, 0);
#endif	
	fb_set_var(&hdlcd->fb, &hdlcd->fb.var);

	if (!register_framebuffer(&hdlcd->fb)) {
		return 0;
	}

	dev_err(hdlcd->dev, "HDLCD: cannot register framebuffer\n");

	fb_dealloc_cmap(&hdlcd->fb.cmap);
setup_err:
	iounmap(hdlcd->base);
kmalloc_err:
	kfree(hdlcd->fb.pseudo_palette);
remap_err:
	clk_unprepare(hdlcd->clk);
clk_prepare_err:
	clk_put(hdlcd->clk);
	return err;
}

static inline unsigned char atohex(u8 data)
{
	if (!isxdigit(data))
		return 0;
	/* truncate the upper nibble and add 9 to non-digit values */
	return (data > 0x39) ? ((data & 0xf) + 9) : (data & 0xf);
}

/* EDID data is passed from devicetree in a literal string that can contain spaces and
   the hexadecimal dump of the data */
static int parse_edid_data(struct hdlcd_device *hdlcd, const u8 *edid_data, int data_len)
{
	int i, j;

	if (!edid_data)
		return -EINVAL;

	hdlcd->edid = kzalloc(EDID_LENGTH, GFP_KERNEL);
	if (!hdlcd->edid)
		return -ENOMEM;

	for (i = 0, j = 0; i < data_len; i++) {
		if (isspace(edid_data[i]))
			continue;
		hdlcd->edid[j++] = atohex(edid_data[i]);
		if (j >= EDID_LENGTH)
			break;
	}

	if (j < EDID_LENGTH) {
		kfree(hdlcd->edid);
		hdlcd->edid = NULL;
		return -EINVAL;
	}

	return 0;
}

static int hdlcd_probe(struct platform_device *pdev)
{
	int err = 0, i;
	struct hdlcd_device *hdlcd;
	struct resource *mem;
#ifdef CONFIG_OF
	struct device_node *of_node;
#endif

	memset(&cached_var_screeninfo, 0, sizeof(struct fb_var_screeninfo));

	dev_dbg(&pdev->dev, "HDLCD: probing\n");

	hdlcd = kzalloc(sizeof(*hdlcd), GFP_KERNEL);
	if (!hdlcd)
		return -ENOMEM;

#ifdef CONFIG_OF
	of_node = pdev->dev.of_node;
	if (of_node) {
		int len;
		const u8 *edid;
		const __be32 *prop = of_get_property(of_node, "mode", &len);
		if (prop)
			strncpy(fb_mode, (char *)prop, len);
		prop = of_get_property(of_node, "framebuffer", &len);
		if (prop) {
			hdlcd->fb.fix.smem_start = of_read_ulong(prop,
					of_n_addr_cells(of_node));
			prop += of_n_addr_cells(of_node);
			framebuffer_size = of_read_ulong(prop,
					of_n_size_cells(of_node));
			if (framebuffer_size > HDLCD_MAX_FRAMEBUFFER_SIZE)
				framebuffer_size = HDLCD_MAX_FRAMEBUFFER_SIZE;
			dev_dbg(&pdev->dev, "HDLCD: phys_addr = 0x%lx, size = 0x%lx\n",
				hdlcd->fb.fix.smem_start, framebuffer_size);
		}
		edid = of_get_property(of_node, "edid", &len);
		if (edid) {
			err = parse_edid_data(hdlcd, edid, len);
#ifdef CONFIG_SERIAL_AMBA_PCU_UART
		} else {
			/* ask the firmware to fetch the EDID */
			dev_dbg(&pdev->dev, "HDLCD: Requesting EDID data\n");
			hdlcd->edid = kzalloc(EDID_LENGTH, GFP_KERNEL);
			if (!hdlcd->edid)
				return -ENOMEM;
			err = get_edid(hdlcd->edid);
#endif /* CONFIG_SERIAL_AMBA_PCU_UART */
		}
		if (err)
			dev_info(&pdev->dev, "HDLCD: Failed to parse EDID data\n");
	}
#endif /* CONFIG_OF */

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "HDLCD: cannot get platform resources\n");
		err = -EINVAL;
		goto resource_err;
	}

	i = platform_get_irq(pdev, 0);
	if (i < 0) {
		dev_err(&pdev->dev, "HDLCD: no irq defined for vsync\n");
		err = -ENOENT;
		goto resource_err;
	} else {
		err = request_irq(i, hdlcd_irq, 0, dev_name(&pdev->dev), hdlcd);
		if (err) {
			dev_err(&pdev->dev, "HDLCD: unable to request irq\n");
			goto resource_err;
		}
		hdlcd->irq = i;
	}

	if (!request_mem_region(mem->start, resource_size(mem),	dev_name(&pdev->dev))) {
		err = -ENXIO;
		goto request_err;
	}

	if (!hdlcd->fb.fix.smem_start) {
		dev_err(&pdev->dev, "platform did not allocate frame buffer memory\n");
		err = -ENOMEM;
		goto memalloc_err;
	}
	hdlcd->fb.screen_base = ioremap_wc(hdlcd->fb.fix.smem_start, framebuffer_size);
	if (!hdlcd->fb.screen_base) {
		dev_err(&pdev->dev, "unable to ioremap framebuffer\n");
		err = -ENOMEM;
		goto probe_err;
	}

	hdlcd->fb.screen_size = framebuffer_size;
	hdlcd->fb.fix.smem_len = framebuffer_size;
	hdlcd->fb.fix.mmio_start = mem->start;
	hdlcd->fb.fix.mmio_len = resource_size(mem);

	/* Clear the framebuffer */
	memset(hdlcd->fb.screen_base, 0, framebuffer_size);

	hdlcd->dev = &pdev->dev;

	dev_dbg(&pdev->dev, "HDLCD: framebuffer virt base %p, phys base 0x%lX\n",
		hdlcd->fb.screen_base, (unsigned long)hdlcd->fb.fix.smem_start);

	err = hdlcd_setup(hdlcd);

	if (err)
		goto probe_err;

	platform_set_drvdata(pdev, hdlcd);
	return 0;

probe_err:
	iounmap(hdlcd->fb.screen_base);
	memblock_free(hdlcd->fb.fix.smem_start, hdlcd->fb.fix.smem_start);

memalloc_err:
	release_mem_region(mem->start, resource_size(mem));

request_err:
	free_irq(hdlcd->irq, hdlcd);

resource_err:
	kfree(hdlcd);

	return err;
}

static int hdlcd_remove(struct platform_device *pdev)
{
	struct hdlcd_device *hdlcd = platform_get_drvdata(pdev);

	clk_disable(hdlcd->clk);
	clk_unprepare(hdlcd->clk);
	clk_put(hdlcd->clk);

	/* unmap memory */
	iounmap(hdlcd->fb.screen_base);
	iounmap(hdlcd->base);

	/* deallocate fb memory */
	fb_dealloc_cmap(&hdlcd->fb.cmap);
	kfree(hdlcd->fb.pseudo_palette);
	memblock_free(hdlcd->fb.fix.smem_start, hdlcd->fb.fix.smem_start);
	release_mem_region(hdlcd->fb.fix.mmio_start, hdlcd->fb.fix.mmio_len);

	free_irq(hdlcd->irq, NULL);
	kfree(hdlcd);

	return 0;
}

#ifdef CONFIG_PM
static int hdlcd_suspend(struct platform_device *pdev, pm_message_t state)
{
	/* not implemented yet */
	return 0;
}

static int hdlcd_resume(struct platform_device *pdev)
{
	/* not implemented yet */
	return 0;
}
#else
#define hdlcd_suspend	NULL
#define hdlcd_resume	NULL
#endif

static struct platform_driver hdlcd_driver = {
	.probe		= hdlcd_probe,
	.remove		= hdlcd_remove,
	.suspend	= hdlcd_suspend,
	.resume		= hdlcd_resume,
	.driver	= {
		.name		= "hdlcd",
		.owner		= THIS_MODULE,
		.of_match_table	= hdlcd_of_matches,
	},
};

static int __init hdlcd_init(void)
{
#ifdef HDLCD_COUNT_BUFFERUNDERRUNS
	int err = platform_driver_register(&hdlcd_driver);
	if (!err)
		hdlcd_underrun_init();
	return err;
#else
	return platform_driver_register(&hdlcd_driver);
#endif
}

void __exit hdlcd_exit(void)
{
#ifdef HDLCD_COUNT_BUFFERUNDERRUNS
	hdlcd_underrun_close();
#endif
	platform_driver_unregister(&hdlcd_driver);
}

module_init(hdlcd_init);
module_exit(hdlcd_exit);

MODULE_AUTHOR("Liviu Dudau");
MODULE_DESCRIPTION("ARM HDLCD core driver");
MODULE_LICENSE("GPL v2");
