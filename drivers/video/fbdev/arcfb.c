/*
 * linux/drivers/video/arcfb.c -- FB driver for Arc monochrome LCD board
 *
 * Copyright (C) 2005, Jaya Kumar <jayalk@intworks.biz>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 *
 * Layout is based on skeletonfb.c by James Simmons and Geert Uytterhoeven.
 *
 * This driver was written to be used with the Arc LCD board. Arc uses a
 * set of KS108 chips that control individual 64x64 LCD matrices. The board
 * can be paneled in a variety of setups such as 2x1=128x64, 4x4=256x256 and
 * so on. The interface between the board and the host is TTL based GPIO. The
 * GPIO requirements are 8 writable data lines and 4+n lines for control. On a
 * GPIO-less system, the board can be tested by connecting the respective sigs
 * up to a parallel port connector. The driver requires the IO addresses for
 * data and control GPIO at load time. It is unable to probe for the
 * existence of the LCD so it must be told at load time whether it should
 * be enabled or not.
 *
 * Todo:
 * - testing with 4x4
 * - testing with interrupt hw
 *
 * General notes:
 * - User must set tuhold. It's in microseconds. According to the 108 spec,
 *   the hold time is supposed to be at least 1 microsecond.
 * - User must set num_cols=x num_rows=y, eg: x=2 means 128
 * - User must set arcfb_enable=1 to enable it
 * - User must set dio_addr=0xIOADDR cio_addr=0xIOADDR
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/arcfb.h>
#include <linux/platform_device.h>

#include <linux/uaccess.h>

#define floor8(a) (a&(~0x07))
#define floorXres(a,xres) (a&(~(xres - 1)))
#define iceil8(a) (((int)((a+7)/8))*8)
#define ceil64(a) (a|0x3F)
#define ceilXres(a,xres) (a|(xres - 1))

/* ks108 chipset specific defines and code */

#define KS_SET_DPY_START_LINE 	0xC0
#define KS_SET_PAGE_NUM 	0xB8
#define KS_SET_X 		0x40
#define KS_CEHI 		0x01
#define KS_CELO 		0x00
#define KS_SEL_CMD 		0x08
#define KS_SEL_DATA 		0x00
#define KS_DPY_ON 		0x3F
#define KS_DPY_OFF 		0x3E
#define KS_INTACK 		0x40
#define KS_CLRINT		0x02

struct arcfb_par {
	unsigned long dio_addr;
	unsigned long cio_addr;
	unsigned long c2io_addr;
	atomic_t ref_count;
	unsigned char cslut[9];
	struct fb_info *info;
	unsigned int irq;
	spinlock_t lock;
};

static const struct fb_fix_screeninfo arcfb_fix = {
	.id =		"arcfb",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_MONO01,
	.xpanstep =	0,
	.ypanstep =	1,
	.ywrapstep =	0,
	.accel =	FB_ACCEL_NONE,
};

static const struct fb_var_screeninfo arcfb_var = {
	.xres		= 128,
	.yres		= 64,
	.xres_virtual	= 128,
	.yres_virtual	= 64,
	.bits_per_pixel	= 1,
	.nonstd		= 1,
};

static unsigned long num_cols;
static unsigned long num_rows;
static unsigned long dio_addr;
static unsigned long cio_addr;
static unsigned long c2io_addr;
static unsigned long splashval;
static unsigned long tuhold;
static unsigned int nosplash;
static unsigned int arcfb_enable;
static unsigned int irq;

static DECLARE_WAIT_QUEUE_HEAD(arcfb_waitq);

static void ks108_writeb_ctl(struct arcfb_par *par,
				unsigned int chipindex, unsigned char value)
{
	unsigned char chipselval = par->cslut[chipindex];

	outb(chipselval|KS_CEHI|KS_SEL_CMD, par->cio_addr);
	outb(value, par->dio_addr);
	udelay(tuhold);
	outb(chipselval|KS_CELO|KS_SEL_CMD, par->cio_addr);
}

static void ks108_writeb_mainctl(struct arcfb_par *par, unsigned char value)
{

	outb(value, par->cio_addr);
	udelay(tuhold);
}

static unsigned char ks108_readb_ctl2(struct arcfb_par *par)
{
	return inb(par->c2io_addr);
}

static void ks108_writeb_data(struct arcfb_par *par,
				unsigned int chipindex, unsigned char value)
{
	unsigned char chipselval = par->cslut[chipindex];

	outb(chipselval|KS_CEHI|KS_SEL_DATA, par->cio_addr);
	outb(value, par->dio_addr);
	udelay(tuhold);
	outb(chipselval|KS_CELO|KS_SEL_DATA, par->cio_addr);
}

static void ks108_set_start_line(struct arcfb_par *par,
				unsigned int chipindex, unsigned char y)
{
	ks108_writeb_ctl(par, chipindex, KS_SET_DPY_START_LINE|y);
}

static void ks108_set_yaddr(struct arcfb_par *par,
				unsigned int chipindex, unsigned char y)
{
	ks108_writeb_ctl(par, chipindex, KS_SET_PAGE_NUM|y);
}

static void ks108_set_xaddr(struct arcfb_par *par,
				unsigned int chipindex, unsigned char x)
{
	ks108_writeb_ctl(par, chipindex, KS_SET_X|x);
}

static void ks108_clear_lcd(struct arcfb_par *par, unsigned int chipindex)
{
	int i,j;

	for (i = 0; i <= 8; i++) {
		ks108_set_yaddr(par, chipindex, i);
		ks108_set_xaddr(par, chipindex, 0);
		for (j = 0; j < 64; j++) {
			ks108_writeb_data(par, chipindex,
				(unsigned char) splashval);
		}
	}
}

/* main arcfb functions */

static int arcfb_open(struct fb_info *info, int user)
{
	struct arcfb_par *par = info->par;

	atomic_inc(&par->ref_count);
	return 0;
}

static int arcfb_release(struct fb_info *info, int user)
{
	struct arcfb_par *par = info->par;
	int count = atomic_read(&par->ref_count);

	if (!count)
		return -EINVAL;
	atomic_dec(&par->ref_count);
	return 0;
}

static int arcfb_pan_display(struct fb_var_screeninfo *var,
				struct fb_info *info)
{
	int i;
	struct arcfb_par *par = info->par;

	if ((var->vmode & FB_VMODE_YWRAP) && (var->yoffset < 64)
		&& (info->var.yres <= 64)) {
		for (i = 0; i < num_cols; i++) {
			ks108_set_start_line(par, i, var->yoffset);
		}
		info->var.yoffset = var->yoffset;
		return 0;
	}

	return -EINVAL;
}

static irqreturn_t arcfb_interrupt(int vec, void *dev_instance)
{
	struct fb_info *info = dev_instance;
	unsigned char ctl2status;
	struct arcfb_par *par = info->par;

	ctl2status = ks108_readb_ctl2(par);

	if (!(ctl2status & KS_INTACK)) /* not arc generated interrupt */
		return IRQ_NONE;

	ks108_writeb_mainctl(par, KS_CLRINT);

	spin_lock(&par->lock);
        if (waitqueue_active(&arcfb_waitq)) {
                wake_up(&arcfb_waitq);
        }
	spin_unlock(&par->lock);

	return IRQ_HANDLED;
}

/*
 * here we handle a specific page on the lcd. the complexity comes from
 * the fact that the fb is laidout in 8xX vertical columns. we extract
 * each write of 8 vertical pixels. then we shift out as we move along
 * X. That's what rightshift does. bitmask selects the desired input bit.
 */
static void arcfb_lcd_update_page(struct arcfb_par *par, unsigned int upper,
		unsigned int left, unsigned int right, unsigned int distance)
{
	unsigned char *src;
	unsigned int xindex, yindex, chipindex, linesize;
	int i;
	unsigned char val;
	unsigned char bitmask, rightshift;

	xindex = left >> 6;
	yindex = upper >> 6;
	chipindex = (xindex + (yindex*num_cols));

	ks108_set_yaddr(par, chipindex, upper/8);

	linesize = par->info->var.xres/8;
	src = (unsigned char __force *) par->info->screen_base + (left/8) +
		(upper * linesize);
	ks108_set_xaddr(par, chipindex, left);

	bitmask=1;
	rightshift=0;
	while (left <= right) {
		val = 0;
		for (i = 0; i < 8; i++) {
			if ( i > rightshift) {
				val |= (*(src + (i*linesize)) & bitmask)
						<< (i - rightshift);
			} else {
				val |= (*(src + (i*linesize)) & bitmask)
						 >> (rightshift - i);
			}
		}
		ks108_writeb_data(par, chipindex, val);
		left++;
		if (bitmask == 0x80) {
			bitmask = 1;
			src++;
			rightshift=0;
		} else {
			bitmask <<= 1;
			rightshift++;
		}
	}
}

/*
 * here we handle the entire vertical page of the update. we write across
 * lcd chips. update_page uses the upper/left values to decide which
 * chip to select for the right. upper is needed for setting the page
 * desired for the write.
 */
static void arcfb_lcd_update_vert(struct arcfb_par *par, unsigned int top,
		unsigned int bottom, unsigned int left, unsigned int right)
{
	unsigned int distance, upper, lower;

	distance = (bottom - top) + 1;
	upper = top;
	lower = top + 7;

	while (distance > 0) {
		distance -= 8;
		arcfb_lcd_update_page(par, upper, left, right, 8);
		upper = lower + 1;
		lower = upper + 7;
	}
}

/*
 * here we handle horizontal blocks for the update. update_vert will
 * handle spaning multiple pages. we break out each horizontal
 * block in to individual blocks no taller than 64 pixels.
 */
static void arcfb_lcd_update_horiz(struct arcfb_par *par, unsigned int left,
			unsigned int right, unsigned int top, unsigned int h)
{
	unsigned int distance, upper, lower;

	distance = h;
	upper = floor8(top);
	lower = min(upper + distance - 1, ceil64(upper));

	while (distance > 0) {
		distance -= ((lower - upper) + 1 );
		arcfb_lcd_update_vert(par, upper, lower, left, right);
		upper = lower + 1;
		lower = min(upper + distance - 1, ceil64(upper));
	}
}

/*
 * here we start the process of splitting out the fb update into
 * individual blocks of pixels. we end up splitting into 64x64 blocks
 * and finally down to 64x8 pages.
 */
static void arcfb_lcd_update(struct arcfb_par *par, unsigned int dx,
			unsigned int dy, unsigned int w, unsigned int h)
{
	unsigned int left, right, distance, y;

	/* align the request first */
	y = floor8(dy);
	h += dy - y;
	h = iceil8(h);

	distance = w;
	left = dx;
	right = min(left + w - 1, ceil64(left));

	while (distance > 0) {
		arcfb_lcd_update_horiz(par, left, right, y, h);
		distance -= ((right - left) + 1);
		left = right + 1;
		right = min(left + distance - 1, ceil64(left));
	}
}

static void arcfb_fillrect(struct fb_info *info,
			   const struct fb_fillrect *rect)
{
	struct arcfb_par *par = info->par;

	sys_fillrect(info, rect);

	/* update the physical lcd */
	arcfb_lcd_update(par, rect->dx, rect->dy, rect->width, rect->height);
}

static void arcfb_copyarea(struct fb_info *info,
			   const struct fb_copyarea *area)
{
	struct arcfb_par *par = info->par;

	sys_copyarea(info, area);

	/* update the physical lcd */
	arcfb_lcd_update(par, area->dx, area->dy, area->width, area->height);
}

static void arcfb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct arcfb_par *par = info->par;

	sys_imageblit(info, image);

	/* update the physical lcd */
	arcfb_lcd_update(par, image->dx, image->dy, image->width,
				image->height);
}

static int arcfb_ioctl(struct fb_info *info,
			  unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct arcfb_par *par = info->par;
	unsigned long flags;

	switch (cmd) {
		case FBIO_WAITEVENT:
		{
			DEFINE_WAIT(wait);
			/* illegal to wait on arc if no irq will occur */
			if (!par->irq)
				return -EINVAL;

			/* wait until the Arc has generated an interrupt
			 * which will wake us up */
			spin_lock_irqsave(&par->lock, flags);
			prepare_to_wait(&arcfb_waitq, &wait,
					TASK_INTERRUPTIBLE);
			spin_unlock_irqrestore(&par->lock, flags);
			schedule();
			finish_wait(&arcfb_waitq, &wait);
		}
		fallthrough;

		case FBIO_GETCONTROL2:
		{
			unsigned char ctl2;

			ctl2 = ks108_readb_ctl2(info->par);
			if (copy_to_user(argp, &ctl2, sizeof(ctl2)))
				return -EFAULT;
			return 0;
		}
		default:
			return -EINVAL;
	}
}

/*
 * this is the access path from userspace. they can seek and write to
 * the fb. it's inefficient for them to do anything less than 64*8
 * writes since we update the lcd in each write() anyway.
 */
static ssize_t arcfb_write(struct fb_info *info, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	/* modded from epson 1355 */

	unsigned long p;
	int err;
	unsigned int fbmemlength,x,y,w,h, bitppos, startpos, endpos, bitcount;
	struct arcfb_par *par;
	unsigned int xres;

	p = *ppos;
	par = info->par;
	xres = info->var.xres;
	fbmemlength = (xres * info->var.yres)/8;

	if (p > fbmemlength)
		return -ENOSPC;

	err = 0;
	if ((count + p) > fbmemlength) {
		count = fbmemlength - p;
		err = -ENOSPC;
	}

	if (count) {
		char *base_addr;

		base_addr = (char __force *)info->screen_base;
		count -= copy_from_user(base_addr + p, buf, count);
		*ppos += count;
		err = -EFAULT;
	}


	bitppos = p*8;
	startpos = floorXres(bitppos, xres);
	endpos = ceilXres((bitppos + (count*8)), xres);
	bitcount = endpos - startpos;

	x = startpos % xres;
	y = startpos / xres;
	w = xres;
	h = bitcount / xres;
	arcfb_lcd_update(par, x, y, w, h);

	if (count)
		return count;
	return err;
}

static const struct fb_ops arcfb_ops = {
	.owner		= THIS_MODULE,
	.fb_open	= arcfb_open,
	.fb_read        = fb_sys_read,
	.fb_write	= arcfb_write,
	.fb_release	= arcfb_release,
	.fb_pan_display	= arcfb_pan_display,
	.fb_fillrect	= arcfb_fillrect,
	.fb_copyarea	= arcfb_copyarea,
	.fb_imageblit	= arcfb_imageblit,
	.fb_ioctl 	= arcfb_ioctl,
};

static int arcfb_probe(struct platform_device *dev)
{
	struct fb_info *info;
	int retval = -ENOMEM;
	int videomemorysize;
	unsigned char *videomemory;
	struct arcfb_par *par;
	int i;

	videomemorysize = (((64*64)*num_cols)*num_rows)/8;

	/* We need a flat backing store for the Arc's
	   less-flat actual paged framebuffer */
	videomemory = vzalloc(videomemorysize);
	if (!videomemory)
		return retval;

	info = framebuffer_alloc(sizeof(struct arcfb_par), &dev->dev);
	if (!info)
		goto err;

	info->screen_base = (char __iomem *)videomemory;
	info->fbops = &arcfb_ops;

	info->var = arcfb_var;
	info->fix = arcfb_fix;
	par = info->par;
	par->info = info;

	if (!dio_addr || !cio_addr || !c2io_addr) {
		printk(KERN_WARNING "no IO addresses supplied\n");
		goto err1;
	}
	par->dio_addr = dio_addr;
	par->cio_addr = cio_addr;
	par->c2io_addr = c2io_addr;
	par->cslut[0] = 0x00;
	par->cslut[1] = 0x06;
	info->flags = FBINFO_FLAG_DEFAULT;
	spin_lock_init(&par->lock);
	if (irq) {
		par->irq = irq;
		if (request_irq(par->irq, &arcfb_interrupt, IRQF_SHARED,
				"arcfb", info)) {
			printk(KERN_INFO
				"arcfb: Failed req IRQ %d\n", par->irq);
			retval = -EBUSY;
			goto err1;
		}
	}
	retval = register_framebuffer(info);
	if (retval < 0)
		goto err1;
	platform_set_drvdata(dev, info);
	fb_info(info, "Arc frame buffer device, using %dK of video memory\n",
		videomemorysize >> 10);

	/* this inits the lcd but doesn't clear dirty pixels */
	for (i = 0; i < num_cols * num_rows; i++) {
		ks108_writeb_ctl(par, i, KS_DPY_OFF);
		ks108_set_start_line(par, i, 0);
		ks108_set_yaddr(par, i, 0);
		ks108_set_xaddr(par, i, 0);
		ks108_writeb_ctl(par, i, KS_DPY_ON);
	}

	/* if we were told to splash the screen, we just clear it */
	if (!nosplash) {
		for (i = 0; i < num_cols * num_rows; i++) {
			fb_info(info, "splashing lcd %d\n", i);
			ks108_set_start_line(par, i, 0);
			ks108_clear_lcd(par, i);
		}
	}

	return 0;
err1:
	framebuffer_release(info);
err:
	vfree(videomemory);
	return retval;
}

static int arcfb_remove(struct platform_device *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);

	if (info) {
		unregister_framebuffer(info);
		if (irq)
			free_irq(((struct arcfb_par *)(info->par))->irq, info);
		vfree((void __force *)info->screen_base);
		framebuffer_release(info);
	}
	return 0;
}

static struct platform_driver arcfb_driver = {
	.probe	= arcfb_probe,
	.remove = arcfb_remove,
	.driver	= {
		.name	= "arcfb",
	},
};

static struct platform_device *arcfb_device;

static int __init arcfb_init(void)
{
	int ret;

	if (!arcfb_enable)
		return -ENXIO;

	ret = platform_driver_register(&arcfb_driver);
	if (!ret) {
		arcfb_device = platform_device_alloc("arcfb", 0);
		if (arcfb_device) {
			ret = platform_device_add(arcfb_device);
		} else {
			ret = -ENOMEM;
		}
		if (ret) {
			platform_device_put(arcfb_device);
			platform_driver_unregister(&arcfb_driver);
		}
	}
	return ret;

}

static void __exit arcfb_exit(void)
{
	platform_device_unregister(arcfb_device);
	platform_driver_unregister(&arcfb_driver);
}

module_param(num_cols, ulong, 0);
MODULE_PARM_DESC(num_cols, "Num horiz panels, eg: 2 = 128 bit wide");
module_param(num_rows, ulong, 0);
MODULE_PARM_DESC(num_rows, "Num vert panels, eg: 1 = 64 bit high");
module_param(nosplash, uint, 0);
MODULE_PARM_DESC(nosplash, "Disable doing the splash screen");
module_param(arcfb_enable, uint, 0);
MODULE_PARM_DESC(arcfb_enable, "Enable communication with Arc board");
module_param_hw(dio_addr, ulong, ioport, 0);
MODULE_PARM_DESC(dio_addr, "IO address for data, eg: 0x480");
module_param_hw(cio_addr, ulong, ioport, 0);
MODULE_PARM_DESC(cio_addr, "IO address for control, eg: 0x400");
module_param_hw(c2io_addr, ulong, ioport, 0);
MODULE_PARM_DESC(c2io_addr, "IO address for secondary control, eg: 0x408");
module_param(splashval, ulong, 0);
MODULE_PARM_DESC(splashval, "Splash pattern: 0xFF is black, 0x00 is green");
module_param(tuhold, ulong, 0);
MODULE_PARM_DESC(tuhold, "Time to hold between strobing data to Arc board");
module_param_hw(irq, uint, irq, 0);
MODULE_PARM_DESC(irq, "IRQ for the Arc board");

module_init(arcfb_init);
module_exit(arcfb_exit);

MODULE_DESCRIPTION("fbdev driver for Arc monochrome LCD board");
MODULE_AUTHOR("Jaya Kumar");
MODULE_LICENSE("GPL");

