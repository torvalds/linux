/*
 * smscufx.c -- Framebuffer driver for SMSC UFX USB controller
 *
 * Copyright (C) 2011 Steve Glendinning <steve.glendinning@shawell.net>
 * Copyright (C) 2009 Roberto De Ioris <roberto@unbit.it>
 * Copyright (C) 2009 Jaya Kumar <jayakumar.lkml@gmail.com>
 * Copyright (C) 2009 Bernie Thompson <bernie@plugable.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2. See the file COPYING in the main directory of this archive for
 * more details.
 *
 * Based on udlfb, with work from Florian Echtler, Henrik Bjerregaard Pedersen,
 * and others.
 *
 * Works well with Bernie Thompson's X DAMAGE patch to xf86-video-fbdev
 * available from http://git.plugable.com
 *
 * Layout is based on skeletonfb by James Simmons and Geert Uytterhoeven,
 * usb-skeleton by GregKH.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include "edid.h"

#define check_warn(status, fmt, args...) \
	({ if (status < 0) pr_warn(fmt, ##args); })

#define check_warn_return(status, fmt, args...) \
	({ if (status < 0) { pr_warn(fmt, ##args); return status; } })

#define check_warn_goto_error(status, fmt, args...) \
	({ if (status < 0) { pr_warn(fmt, ##args); goto error; } })

#define all_bits_set(x, bits) (((x) & (bits)) == (bits))

#define USB_VENDOR_REQUEST_WRITE_REGISTER	0xA0
#define USB_VENDOR_REQUEST_READ_REGISTER	0xA1

/*
 * TODO: Propose standard fb.h ioctl for reporting damage,
 * using _IOWR() and one of the existing area structs from fb.h
 * Consider these ioctls deprecated, but they're still used by the
 * DisplayLink X server as yet - need both to be modified in tandem
 * when new ioctl(s) are ready.
 */
#define UFX_IOCTL_RETURN_EDID	(0xAD)
#define UFX_IOCTL_REPORT_DAMAGE	(0xAA)

/* -BULK_SIZE as per usb-skeleton. Can we get full page and avoid overhead? */
#define BULK_SIZE		(512)
#define MAX_TRANSFER		(PAGE_SIZE*16 - BULK_SIZE)
#define WRITES_IN_FLIGHT	(4)

#define GET_URB_TIMEOUT		(HZ)
#define FREE_URB_TIMEOUT	(HZ*2)

#define BPP			2

#define UFX_DEFIO_WRITE_DELAY	5 /* fb_deferred_io.delay in jiffies */
#define UFX_DEFIO_WRITE_DISABLE	(HZ*60) /* "disable" with long delay */

struct dloarea {
	int x, y;
	int w, h;
};

struct urb_node {
	struct list_head entry;
	struct ufx_data *dev;
	struct delayed_work release_urb_work;
	struct urb *urb;
};

struct urb_list {
	struct list_head list;
	spinlock_t lock;
	struct semaphore limit_sem;
	int available;
	int count;
	size_t size;
};

struct ufx_data {
	struct usb_device *udev;
	struct device *gdev; /* &udev->dev */
	struct fb_info *info;
	struct urb_list urbs;
	struct kref kref;
	int fb_count;
	bool virtualized; /* true when physical usb device not present */
	struct delayed_work free_framebuffer_work;
	atomic_t usb_active; /* 0 = update virtual buffer, but no usb traffic */
	atomic_t lost_pixels; /* 1 = a render op failed. Need screen refresh */
	u8 *edid; /* null until we read edid from hw or get from sysfs */
	size_t edid_size;
	u32 pseudo_palette[256];
};

static struct fb_fix_screeninfo ufx_fix = {
	.id =           "smscufx",
	.type =         FB_TYPE_PACKED_PIXELS,
	.visual =       FB_VISUAL_TRUECOLOR,
	.xpanstep =     0,
	.ypanstep =     0,
	.ywrapstep =    0,
	.accel =        FB_ACCEL_NONE,
};

static const u32 smscufx_info_flags = FBINFO_DEFAULT | FBINFO_READS_FAST |
	FBINFO_VIRTFB |	FBINFO_HWACCEL_IMAGEBLIT | FBINFO_HWACCEL_FILLRECT |
	FBINFO_HWACCEL_COPYAREA | FBINFO_MISC_ALWAYS_SETPAR;

static struct usb_device_id id_table[] = {
	{USB_DEVICE(0x0424, 0x9d00),},
	{USB_DEVICE(0x0424, 0x9d01),},
	{},
};
MODULE_DEVICE_TABLE(usb, id_table);

/* module options */
static bool console;   /* Optionally allow fbcon to consume first framebuffer */
static bool fb_defio = true;  /* Optionally enable fb_defio mmap support */

/* ufx keeps a list of urbs for efficient bulk transfers */
static void ufx_urb_completion(struct urb *urb);
static struct urb *ufx_get_urb(struct ufx_data *dev);
static int ufx_submit_urb(struct ufx_data *dev, struct urb * urb, size_t len);
static int ufx_alloc_urb_list(struct ufx_data *dev, int count, size_t size);
static void ufx_free_urb_list(struct ufx_data *dev);

/* reads a control register */
static int ufx_reg_read(struct ufx_data *dev, u32 index, u32 *data)
{
	u32 *buf = kmalloc(4, GFP_KERNEL);
	int ret;

	BUG_ON(!dev);

	if (!buf)
		return -ENOMEM;

	ret = usb_control_msg(dev->udev, usb_rcvctrlpipe(dev->udev, 0),
		USB_VENDOR_REQUEST_READ_REGISTER,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		00, index, buf, 4, USB_CTRL_GET_TIMEOUT);

	le32_to_cpus(buf);
	*data = *buf;
	kfree(buf);

	if (unlikely(ret < 0))
		pr_warn("Failed to read register index 0x%08x\n", index);

	return ret;
}

/* writes a control register */
static int ufx_reg_write(struct ufx_data *dev, u32 index, u32 data)
{
	u32 *buf = kmalloc(4, GFP_KERNEL);
	int ret;

	BUG_ON(!dev);

	if (!buf)
		return -ENOMEM;

	*buf = data;
	cpu_to_le32s(buf);

	ret = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0),
		USB_VENDOR_REQUEST_WRITE_REGISTER,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		00, index, buf, 4, USB_CTRL_SET_TIMEOUT);

	kfree(buf);

	if (unlikely(ret < 0))
		pr_warn("Failed to write register index 0x%08x with value "
			"0x%08x\n", index, data);

	return ret;
}

static int ufx_reg_clear_and_set_bits(struct ufx_data *dev, u32 index,
	u32 bits_to_clear, u32 bits_to_set)
{
	u32 data;
	int status = ufx_reg_read(dev, index, &data);
	check_warn_return(status, "ufx_reg_clear_and_set_bits error reading "
		"0x%x", index);

	data &= (~bits_to_clear);
	data |= bits_to_set;

	status = ufx_reg_write(dev, index, data);
	check_warn_return(status, "ufx_reg_clear_and_set_bits error writing "
		"0x%x", index);

	return 0;
}

static int ufx_reg_set_bits(struct ufx_data *dev, u32 index, u32 bits)
{
	return ufx_reg_clear_and_set_bits(dev, index, 0, bits);
}

static int ufx_reg_clear_bits(struct ufx_data *dev, u32 index, u32 bits)
{
	return ufx_reg_clear_and_set_bits(dev, index, bits, 0);
}

static int ufx_lite_reset(struct ufx_data *dev)
{
	int status;
	u32 value;

	status = ufx_reg_write(dev, 0x3008, 0x00000001);
	check_warn_return(status, "ufx_lite_reset error writing 0x3008");

	status = ufx_reg_read(dev, 0x3008, &value);
	check_warn_return(status, "ufx_lite_reset error reading 0x3008");

	return (value == 0) ? 0 : -EIO;
}

/* If display is unblanked, then blank it */
static int ufx_blank(struct ufx_data *dev, bool wait)
{
	u32 dc_ctrl, dc_sts;
	int i;

	int status = ufx_reg_read(dev, 0x2004, &dc_sts);
	check_warn_return(status, "ufx_blank error reading 0x2004");

	status = ufx_reg_read(dev, 0x2000, &dc_ctrl);
	check_warn_return(status, "ufx_blank error reading 0x2000");

	/* return success if display is already blanked */
	if ((dc_sts & 0x00000100) || (dc_ctrl & 0x00000100))
		return 0;

	/* request the DC to blank the display */
	dc_ctrl |= 0x00000100;
	status = ufx_reg_write(dev, 0x2000, dc_ctrl);
	check_warn_return(status, "ufx_blank error writing 0x2000");

	/* return success immediately if we don't have to wait */
	if (!wait)
		return 0;

	for (i = 0; i < 250; i++) {
		status = ufx_reg_read(dev, 0x2004, &dc_sts);
		check_warn_return(status, "ufx_blank error reading 0x2004");

		if (dc_sts & 0x00000100)
			return 0;
	}

	/* timed out waiting for display to blank */
	return -EIO;
}

/* If display is blanked, then unblank it */
static int ufx_unblank(struct ufx_data *dev, bool wait)
{
	u32 dc_ctrl, dc_sts;
	int i;

	int status = ufx_reg_read(dev, 0x2004, &dc_sts);
	check_warn_return(status, "ufx_unblank error reading 0x2004");

	status = ufx_reg_read(dev, 0x2000, &dc_ctrl);
	check_warn_return(status, "ufx_unblank error reading 0x2000");

	/* return success if display is already unblanked */
	if (((dc_sts & 0x00000100) == 0) || ((dc_ctrl & 0x00000100) == 0))
		return 0;

	/* request the DC to unblank the display */
	dc_ctrl &= ~0x00000100;
	status = ufx_reg_write(dev, 0x2000, dc_ctrl);
	check_warn_return(status, "ufx_unblank error writing 0x2000");

	/* return success immediately if we don't have to wait */
	if (!wait)
		return 0;

	for (i = 0; i < 250; i++) {
		status = ufx_reg_read(dev, 0x2004, &dc_sts);
		check_warn_return(status, "ufx_unblank error reading 0x2004");

		if ((dc_sts & 0x00000100) == 0)
			return 0;
	}

	/* timed out waiting for display to unblank */
	return -EIO;
}

/* If display is enabled, then disable it */
static int ufx_disable(struct ufx_data *dev, bool wait)
{
	u32 dc_ctrl, dc_sts;
	int i;

	int status = ufx_reg_read(dev, 0x2004, &dc_sts);
	check_warn_return(status, "ufx_disable error reading 0x2004");

	status = ufx_reg_read(dev, 0x2000, &dc_ctrl);
	check_warn_return(status, "ufx_disable error reading 0x2000");

	/* return success if display is already disabled */
	if (((dc_sts & 0x00000001) == 0) || ((dc_ctrl & 0x00000001) == 0))
		return 0;

	/* request the DC to disable the display */
	dc_ctrl &= ~(0x00000001);
	status = ufx_reg_write(dev, 0x2000, dc_ctrl);
	check_warn_return(status, "ufx_disable error writing 0x2000");

	/* return success immediately if we don't have to wait */
	if (!wait)
		return 0;

	for (i = 0; i < 250; i++) {
		status = ufx_reg_read(dev, 0x2004, &dc_sts);
		check_warn_return(status, "ufx_disable error reading 0x2004");

		if ((dc_sts & 0x00000001) == 0)
			return 0;
	}

	/* timed out waiting for display to disable */
	return -EIO;
}

/* If display is disabled, then enable it */
static int ufx_enable(struct ufx_data *dev, bool wait)
{
	u32 dc_ctrl, dc_sts;
	int i;

	int status = ufx_reg_read(dev, 0x2004, &dc_sts);
	check_warn_return(status, "ufx_enable error reading 0x2004");

	status = ufx_reg_read(dev, 0x2000, &dc_ctrl);
	check_warn_return(status, "ufx_enable error reading 0x2000");

	/* return success if display is already enabled */
	if ((dc_sts & 0x00000001) || (dc_ctrl & 0x00000001))
		return 0;

	/* request the DC to enable the display */
	dc_ctrl |= 0x00000001;
	status = ufx_reg_write(dev, 0x2000, dc_ctrl);
	check_warn_return(status, "ufx_enable error writing 0x2000");

	/* return success immediately if we don't have to wait */
	if (!wait)
		return 0;

	for (i = 0; i < 250; i++) {
		status = ufx_reg_read(dev, 0x2004, &dc_sts);
		check_warn_return(status, "ufx_enable error reading 0x2004");

		if (dc_sts & 0x00000001)
			return 0;
	}

	/* timed out waiting for display to enable */
	return -EIO;
}

static int ufx_config_sys_clk(struct ufx_data *dev)
{
	int status = ufx_reg_write(dev, 0x700C, 0x8000000F);
	check_warn_return(status, "error writing 0x700C");

	status = ufx_reg_write(dev, 0x7014, 0x0010024F);
	check_warn_return(status, "error writing 0x7014");

	status = ufx_reg_write(dev, 0x7010, 0x00000000);
	check_warn_return(status, "error writing 0x7010");

	status = ufx_reg_clear_bits(dev, 0x700C, 0x0000000A);
	check_warn_return(status, "error clearing PLL1 bypass in 0x700C");
	msleep(1);

	status = ufx_reg_clear_bits(dev, 0x700C, 0x80000000);
	check_warn_return(status, "error clearing output gate in 0x700C");

	return 0;
}

static int ufx_config_ddr2(struct ufx_data *dev)
{
	int status, i = 0;
	u32 tmp;

	status = ufx_reg_write(dev, 0x0004, 0x001F0F77);
	check_warn_return(status, "error writing 0x0004");

	status = ufx_reg_write(dev, 0x0008, 0xFFF00000);
	check_warn_return(status, "error writing 0x0008");

	status = ufx_reg_write(dev, 0x000C, 0x0FFF2222);
	check_warn_return(status, "error writing 0x000C");

	status = ufx_reg_write(dev, 0x0010, 0x00030814);
	check_warn_return(status, "error writing 0x0010");

	status = ufx_reg_write(dev, 0x0014, 0x00500019);
	check_warn_return(status, "error writing 0x0014");

	status = ufx_reg_write(dev, 0x0018, 0x020D0F15);
	check_warn_return(status, "error writing 0x0018");

	status = ufx_reg_write(dev, 0x001C, 0x02532305);
	check_warn_return(status, "error writing 0x001C");

	status = ufx_reg_write(dev, 0x0020, 0x0B030905);
	check_warn_return(status, "error writing 0x0020");

	status = ufx_reg_write(dev, 0x0024, 0x00000827);
	check_warn_return(status, "error writing 0x0024");

	status = ufx_reg_write(dev, 0x0028, 0x00000000);
	check_warn_return(status, "error writing 0x0028");

	status = ufx_reg_write(dev, 0x002C, 0x00000042);
	check_warn_return(status, "error writing 0x002C");

	status = ufx_reg_write(dev, 0x0030, 0x09520000);
	check_warn_return(status, "error writing 0x0030");

	status = ufx_reg_write(dev, 0x0034, 0x02223314);
	check_warn_return(status, "error writing 0x0034");

	status = ufx_reg_write(dev, 0x0038, 0x00430043);
	check_warn_return(status, "error writing 0x0038");

	status = ufx_reg_write(dev, 0x003C, 0xF00F000F);
	check_warn_return(status, "error writing 0x003C");

	status = ufx_reg_write(dev, 0x0040, 0xF380F00F);
	check_warn_return(status, "error writing 0x0040");

	status = ufx_reg_write(dev, 0x0044, 0xF00F0496);
	check_warn_return(status, "error writing 0x0044");

	status = ufx_reg_write(dev, 0x0048, 0x03080406);
	check_warn_return(status, "error writing 0x0048");

	status = ufx_reg_write(dev, 0x004C, 0x00001000);
	check_warn_return(status, "error writing 0x004C");

	status = ufx_reg_write(dev, 0x005C, 0x00000007);
	check_warn_return(status, "error writing 0x005C");

	status = ufx_reg_write(dev, 0x0100, 0x54F00012);
	check_warn_return(status, "error writing 0x0100");

	status = ufx_reg_write(dev, 0x0104, 0x00004012);
	check_warn_return(status, "error writing 0x0104");

	status = ufx_reg_write(dev, 0x0118, 0x40404040);
	check_warn_return(status, "error writing 0x0118");

	status = ufx_reg_write(dev, 0x0000, 0x00000001);
	check_warn_return(status, "error writing 0x0000");

	while (i++ < 500) {
		status = ufx_reg_read(dev, 0x0000, &tmp);
		check_warn_return(status, "error reading 0x0000");

		if (all_bits_set(tmp, 0xC0000000))
			return 0;
	}

	pr_err("DDR2 initialisation timed out, reg 0x0000=0x%08x", tmp);
	return -ETIMEDOUT;
}

struct pll_values {
	u32 div_r0;
	u32 div_f0;
	u32 div_q0;
	u32 range0;
	u32 div_r1;
	u32 div_f1;
	u32 div_q1;
	u32 range1;
};

static u32 ufx_calc_range(u32 ref_freq)
{
	if (ref_freq >= 88000000)
		return 7;

	if (ref_freq >= 54000000)
		return 6;

	if (ref_freq >= 34000000)
		return 5;

	if (ref_freq >= 21000000)
		return 4;

	if (ref_freq >= 13000000)
		return 3;

	if (ref_freq >= 8000000)
		return 2;

	return 1;
}

/* calculates PLL divider settings for a desired target frequency */
static void ufx_calc_pll_values(const u32 clk_pixel_pll, struct pll_values *asic_pll)
{
	const u32 ref_clk = 25000000;
	u32 div_r0, div_f0, div_q0, div_r1, div_f1, div_q1;
	u32 min_error = clk_pixel_pll;

	for (div_r0 = 1; div_r0 <= 32; div_r0++) {
		u32 ref_freq0 = ref_clk / div_r0;
		if (ref_freq0 < 5000000)
			break;

		if (ref_freq0 > 200000000)
			continue;

		for (div_f0 = 1; div_f0 <= 256; div_f0++) {
			u32 vco_freq0 = ref_freq0 * div_f0;

			if (vco_freq0 < 350000000)
				continue;

			if (vco_freq0 > 700000000)
				break;

			for (div_q0 = 0; div_q0 < 7; div_q0++) {
				u32 pllout_freq0 = vco_freq0 / (1 << div_q0);

				if (pllout_freq0 < 5000000)
					break;

				if (pllout_freq0 > 200000000)
					continue;

				for (div_r1 = 1; div_r1 <= 32; div_r1++) {
					u32 ref_freq1 = pllout_freq0 / div_r1;

					if (ref_freq1 < 5000000)
						break;

					for (div_f1 = 1; div_f1 <= 256; div_f1++) {
						u32 vco_freq1 = ref_freq1 * div_f1;

						if (vco_freq1 < 350000000)
							continue;

						if (vco_freq1 > 700000000)
							break;

						for (div_q1 = 0; div_q1 < 7; div_q1++) {
							u32 pllout_freq1 = vco_freq1 / (1 << div_q1);
							int error = abs(pllout_freq1 - clk_pixel_pll);

							if (pllout_freq1 < 5000000)
								break;

							if (pllout_freq1 > 700000000)
								continue;

							if (error < min_error) {
								min_error = error;

								/* final returned value is equal to calculated value - 1
								 * because a value of 0 = divide by 1 */
								asic_pll->div_r0 = div_r0 - 1;
								asic_pll->div_f0 = div_f0 - 1;
								asic_pll->div_q0 = div_q0;
								asic_pll->div_r1 = div_r1 - 1;
								asic_pll->div_f1 = div_f1 - 1;
								asic_pll->div_q1 = div_q1;

								asic_pll->range0 = ufx_calc_range(ref_freq0);
								asic_pll->range1 = ufx_calc_range(ref_freq1);

								if (min_error == 0)
									return;
							}
						}
					}
				}
			}
		}
	}
}

/* sets analog bit PLL configuration values */
static int ufx_config_pix_clk(struct ufx_data *dev, u32 pixclock)
{
	struct pll_values asic_pll = {0};
	u32 value, clk_pixel, clk_pixel_pll;
	int status;

	/* convert pixclock (in ps) to frequency (in Hz) */
	clk_pixel = PICOS2KHZ(pixclock) * 1000;
	pr_debug("pixclock %d ps = clk_pixel %d Hz", pixclock, clk_pixel);

	/* clk_pixel = 1/2 clk_pixel_pll */
	clk_pixel_pll = clk_pixel * 2;

	ufx_calc_pll_values(clk_pixel_pll, &asic_pll);

	/* Keep BYPASS and RESET signals asserted until configured */
	status = ufx_reg_write(dev, 0x7000, 0x8000000F);
	check_warn_return(status, "error writing 0x7000");

	value = (asic_pll.div_f1 | (asic_pll.div_r1 << 8) |
		(asic_pll.div_q1 << 16) | (asic_pll.range1 << 20));
	status = ufx_reg_write(dev, 0x7008, value);
	check_warn_return(status, "error writing 0x7008");

	value = (asic_pll.div_f0 | (asic_pll.div_r0 << 8) |
		(asic_pll.div_q0 << 16) | (asic_pll.range0 << 20));
	status = ufx_reg_write(dev, 0x7004, value);
	check_warn_return(status, "error writing 0x7004");

	status = ufx_reg_clear_bits(dev, 0x7000, 0x00000005);
	check_warn_return(status,
		"error clearing PLL0 bypass bits in 0x7000");
	msleep(1);

	status = ufx_reg_clear_bits(dev, 0x7000, 0x0000000A);
	check_warn_return(status,
		"error clearing PLL1 bypass bits in 0x7000");
	msleep(1);

	status = ufx_reg_clear_bits(dev, 0x7000, 0x80000000);
	check_warn_return(status, "error clearing gate bits in 0x7000");

	return 0;
}

static int ufx_set_vid_mode(struct ufx_data *dev, struct fb_var_screeninfo *var)
{
	u32 temp;
	u16 h_total, h_active, h_blank_start, h_blank_end, h_sync_start, h_sync_end;
	u16 v_total, v_active, v_blank_start, v_blank_end, v_sync_start, v_sync_end;

	int status = ufx_reg_write(dev, 0x8028, 0);
	check_warn_return(status, "ufx_set_vid_mode error disabling RGB pad");

	status = ufx_reg_write(dev, 0x8024, 0);
	check_warn_return(status, "ufx_set_vid_mode error disabling VDAC");

	/* shut everything down before changing timing */
	status = ufx_blank(dev, true);
	check_warn_return(status, "ufx_set_vid_mode error blanking display");

	status = ufx_disable(dev, true);
	check_warn_return(status, "ufx_set_vid_mode error disabling display");

	status = ufx_config_pix_clk(dev, var->pixclock);
	check_warn_return(status, "ufx_set_vid_mode error configuring pixclock");

	status = ufx_reg_write(dev, 0x2000, 0x00000104);
	check_warn_return(status, "ufx_set_vid_mode error writing 0x2000");

	/* set horizontal timings */
	h_total = var->xres + var->right_margin + var->hsync_len + var->left_margin;
	h_active = var->xres;
	h_blank_start = var->xres + var->right_margin;
	h_blank_end = var->xres + var->right_margin + var->hsync_len;
	h_sync_start = var->xres + var->right_margin;
	h_sync_end = var->xres + var->right_margin + var->hsync_len;

	temp = ((h_total - 1) << 16) | (h_active - 1);
	status = ufx_reg_write(dev, 0x2008, temp);
	check_warn_return(status, "ufx_set_vid_mode error writing 0x2008");

	temp = ((h_blank_start - 1) << 16) | (h_blank_end - 1);
	status = ufx_reg_write(dev, 0x200C, temp);
	check_warn_return(status, "ufx_set_vid_mode error writing 0x200C");

	temp = ((h_sync_start - 1) << 16) | (h_sync_end - 1);
	status = ufx_reg_write(dev, 0x2010, temp);
	check_warn_return(status, "ufx_set_vid_mode error writing 0x2010");

	/* set vertical timings */
	v_total = var->upper_margin + var->yres + var->lower_margin + var->vsync_len;
	v_active = var->yres;
	v_blank_start = var->yres + var->lower_margin;
	v_blank_end = var->yres + var->lower_margin + var->vsync_len;
	v_sync_start = var->yres + var->lower_margin;
	v_sync_end = var->yres + var->lower_margin + var->vsync_len;

	temp = ((v_total - 1) << 16) | (v_active - 1);
	status = ufx_reg_write(dev, 0x2014, temp);
	check_warn_return(status, "ufx_set_vid_mode error writing 0x2014");

	temp = ((v_blank_start - 1) << 16) | (v_blank_end - 1);
	status = ufx_reg_write(dev, 0x2018, temp);
	check_warn_return(status, "ufx_set_vid_mode error writing 0x2018");

	temp = ((v_sync_start - 1) << 16) | (v_sync_end - 1);
	status = ufx_reg_write(dev, 0x201C, temp);
	check_warn_return(status, "ufx_set_vid_mode error writing 0x201C");

	status = ufx_reg_write(dev, 0x2020, 0x00000000);
	check_warn_return(status, "ufx_set_vid_mode error writing 0x2020");

	status = ufx_reg_write(dev, 0x2024, 0x00000000);
	check_warn_return(status, "ufx_set_vid_mode error writing 0x2024");

	/* Set the frame length register (#pix * 2 bytes/pixel) */
	temp = var->xres * var->yres * 2;
	temp = (temp + 7) & (~0x7);
	status = ufx_reg_write(dev, 0x2028, temp);
	check_warn_return(status, "ufx_set_vid_mode error writing 0x2028");

	/* enable desired output interface & disable others */
	status = ufx_reg_write(dev, 0x2040, 0);
	check_warn_return(status, "ufx_set_vid_mode error writing 0x2040");

	status = ufx_reg_write(dev, 0x2044, 0);
	check_warn_return(status, "ufx_set_vid_mode error writing 0x2044");

	status = ufx_reg_write(dev, 0x2048, 0);
	check_warn_return(status, "ufx_set_vid_mode error writing 0x2048");

	/* set the sync polarities & enable bit */
	temp = 0x00000001;
	if (var->sync & FB_SYNC_HOR_HIGH_ACT)
		temp |= 0x00000010;

	if (var->sync & FB_SYNC_VERT_HIGH_ACT)
		temp |= 0x00000008;

	status = ufx_reg_write(dev, 0x2040, temp);
	check_warn_return(status, "ufx_set_vid_mode error writing 0x2040");

	/* start everything back up */
	status = ufx_enable(dev, true);
	check_warn_return(status, "ufx_set_vid_mode error enabling display");

	/* Unblank the display */
	status = ufx_unblank(dev, true);
	check_warn_return(status, "ufx_set_vid_mode error unblanking display");

	/* enable RGB pad */
	status = ufx_reg_write(dev, 0x8028, 0x00000003);
	check_warn_return(status, "ufx_set_vid_mode error enabling RGB pad");

	/* enable VDAC */
	status = ufx_reg_write(dev, 0x8024, 0x00000007);
	check_warn_return(status, "ufx_set_vid_mode error enabling VDAC");

	return 0;
}

static int ufx_ops_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	unsigned long start = vma->vm_start;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long page, pos;

	if (offset + size > info->fix.smem_len)
		return -EINVAL;

	pos = (unsigned long)info->fix.smem_start + offset;

	pr_debug("mmap() framebuffer addr:%lu size:%lu\n",
		  pos, size);

	while (size > 0) {
		page = vmalloc_to_pfn((void *)pos);
		if (remap_pfn_range(vma, start, page, PAGE_SIZE, PAGE_SHARED))
			return -EAGAIN;

		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}

	vma->vm_flags |= VM_RESERVED;	/* avoid to swap out this VMA */
	return 0;
}

static void ufx_raw_rect(struct ufx_data *dev, u16 *cmd, int x, int y,
	int width, int height)
{
	size_t packed_line_len = ALIGN((width * 2), 4);
	size_t packed_rect_len = packed_line_len * height;
	int line;

	BUG_ON(!dev);
	BUG_ON(!dev->info);

	/* command word */
	*((u32 *)&cmd[0]) = cpu_to_le32(0x01);

	/* length word */
	*((u32 *)&cmd[2]) = cpu_to_le32(packed_rect_len + 16);

	cmd[4] = cpu_to_le16(x);
	cmd[5] = cpu_to_le16(y);
	cmd[6] = cpu_to_le16(width);
	cmd[7] = cpu_to_le16(height);

	/* frame base address */
	*((u32 *)&cmd[8]) = cpu_to_le32(0);

	/* color mode and horizontal resolution */
	cmd[10] = cpu_to_le16(0x4000 | dev->info->var.xres);

	/* vertical resolution */
	cmd[11] = cpu_to_le16(dev->info->var.yres);

	/* packed data */
	for (line = 0; line < height; line++) {
		const int line_offset = dev->info->fix.line_length * (y + line);
		const int byte_offset = line_offset + (x * BPP);
		memcpy(&cmd[(24 + (packed_line_len * line)) / 2],
			(char *)dev->info->fix.smem_start + byte_offset, width * BPP);
	}
}

static int ufx_handle_damage(struct ufx_data *dev, int x, int y,
	int width, int height)
{
	size_t packed_line_len = ALIGN((width * 2), 4);
	int len, status, urb_lines, start_line = 0;

	if ((width <= 0) || (height <= 0) ||
	    (x + width > dev->info->var.xres) ||
	    (y + height > dev->info->var.yres))
		return -EINVAL;

	if (!atomic_read(&dev->usb_active))
		return 0;

	while (start_line < height) {
		struct urb *urb = ufx_get_urb(dev);
		if (!urb) {
			pr_warn("ufx_handle_damage unable to get urb");
			return 0;
		}

		/* assume we have enough space to transfer at least one line */
		BUG_ON(urb->transfer_buffer_length < (24 + (width * 2)));

		/* calculate the maximum number of lines we could fit in */
		urb_lines = (urb->transfer_buffer_length - 24) / packed_line_len;

		/* but we might not need this many */
		urb_lines = min(urb_lines, (height - start_line));

		memset(urb->transfer_buffer, 0, urb->transfer_buffer_length);

		ufx_raw_rect(dev, urb->transfer_buffer, x, (y + start_line), width, urb_lines);
		len = 24 + (packed_line_len * urb_lines);

		status = ufx_submit_urb(dev, urb, len);
		check_warn_return(status, "Error submitting URB");

		start_line += urb_lines;
	}

	return 0;
}

/* Path triggered by usermode clients who write to filesystem
 * e.g. cat filename > /dev/fb1
 * Not used by X Windows or text-mode console. But useful for testing.
 * Slow because of extra copy and we must assume all pixels dirty. */
static ssize_t ufx_ops_write(struct fb_info *info, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	ssize_t result;
	struct ufx_data *dev = info->par;
	u32 offset = (u32) *ppos;

	result = fb_sys_write(info, buf, count, ppos);

	if (result > 0) {
		int start = max((int)(offset / info->fix.line_length) - 1, 0);
		int lines = min((u32)((result / info->fix.line_length) + 1),
				(u32)info->var.yres);

		ufx_handle_damage(dev, 0, start, info->var.xres, lines);
	}

	return result;
}

static void ufx_ops_copyarea(struct fb_info *info,
				const struct fb_copyarea *area)
{

	struct ufx_data *dev = info->par;

	sys_copyarea(info, area);

	ufx_handle_damage(dev, area->dx, area->dy,
			area->width, area->height);
}

static void ufx_ops_imageblit(struct fb_info *info,
				const struct fb_image *image)
{
	struct ufx_data *dev = info->par;

	sys_imageblit(info, image);

	ufx_handle_damage(dev, image->dx, image->dy,
			image->width, image->height);
}

static void ufx_ops_fillrect(struct fb_info *info,
			  const struct fb_fillrect *rect)
{
	struct ufx_data *dev = info->par;

	sys_fillrect(info, rect);

	ufx_handle_damage(dev, rect->dx, rect->dy, rect->width,
			      rect->height);
}

/* NOTE: fb_defio.c is holding info->fbdefio.mutex
 *   Touching ANY framebuffer memory that triggers a page fault
 *   in fb_defio will cause a deadlock, when it also tries to
 *   grab the same mutex. */
static void ufx_dpy_deferred_io(struct fb_info *info,
				struct list_head *pagelist)
{
	struct page *cur;
	struct fb_deferred_io *fbdefio = info->fbdefio;
	struct ufx_data *dev = info->par;

	if (!fb_defio)
		return;

	if (!atomic_read(&dev->usb_active))
		return;

	/* walk the written page list and render each to device */
	list_for_each_entry(cur, &fbdefio->pagelist, lru) {
		/* create a rectangle of full screen width that encloses the
		 * entire dirty framebuffer page */
		const int x = 0;
		const int width = dev->info->var.xres;
		const int y = (cur->index << PAGE_SHIFT) / (width * 2);
		int height = (PAGE_SIZE / (width * 2)) + 1;
		height = min(height, (int)(dev->info->var.yres - y));

		BUG_ON(y >= dev->info->var.yres);
		BUG_ON((y + height) > dev->info->var.yres);

		ufx_handle_damage(dev, x, y, width, height);
	}
}

static int ufx_ops_ioctl(struct fb_info *info, unsigned int cmd,
			 unsigned long arg)
{
	struct ufx_data *dev = info->par;
	struct dloarea *area = NULL;

	if (!atomic_read(&dev->usb_active))
		return 0;

	/* TODO: Update X server to get this from sysfs instead */
	if (cmd == UFX_IOCTL_RETURN_EDID) {
		u8 __user *edid = (u8 __user *)arg;
		if (copy_to_user(edid, dev->edid, dev->edid_size))
			return -EFAULT;
		return 0;
	}

	/* TODO: Help propose a standard fb.h ioctl to report mmap damage */
	if (cmd == UFX_IOCTL_REPORT_DAMAGE) {
		/* If we have a damage-aware client, turn fb_defio "off"
		 * To avoid perf imact of unnecessary page fault handling.
		 * Done by resetting the delay for this fb_info to a very
		 * long period. Pages will become writable and stay that way.
		 * Reset to normal value when all clients have closed this fb.
		 */
		if (info->fbdefio)
			info->fbdefio->delay = UFX_DEFIO_WRITE_DISABLE;

		area = (struct dloarea *)arg;

		if (area->x < 0)
			area->x = 0;

		if (area->x > info->var.xres)
			area->x = info->var.xres;

		if (area->y < 0)
			area->y = 0;

		if (area->y > info->var.yres)
			area->y = info->var.yres;

		ufx_handle_damage(dev, area->x, area->y, area->w, area->h);
	}

	return 0;
}

/* taken from vesafb */
static int
ufx_ops_setcolreg(unsigned regno, unsigned red, unsigned green,
	       unsigned blue, unsigned transp, struct fb_info *info)
{
	int err = 0;

	if (regno >= info->cmap.len)
		return 1;

	if (regno < 16) {
		if (info->var.red.offset == 10) {
			/* 1:5:5:5 */
			((u32 *) (info->pseudo_palette))[regno] =
			    ((red & 0xf800) >> 1) |
			    ((green & 0xf800) >> 6) | ((blue & 0xf800) >> 11);
		} else {
			/* 0:5:6:5 */
			((u32 *) (info->pseudo_palette))[regno] =
			    ((red & 0xf800)) |
			    ((green & 0xfc00) >> 5) | ((blue & 0xf800) >> 11);
		}
	}

	return err;
}

/* It's common for several clients to have framebuffer open simultaneously.
 * e.g. both fbcon and X. Makes things interesting.
 * Assumes caller is holding info->lock (for open and release at least) */
static int ufx_ops_open(struct fb_info *info, int user)
{
	struct ufx_data *dev = info->par;

	/* fbcon aggressively connects to first framebuffer it finds,
	 * preventing other clients (X) from working properly. Usually
	 * not what the user wants. Fail by default with option to enable. */
	if (user == 0 && !console)
		return -EBUSY;

	/* If the USB device is gone, we don't accept new opens */
	if (dev->virtualized)
		return -ENODEV;

	dev->fb_count++;

	kref_get(&dev->kref);

	if (fb_defio && (info->fbdefio == NULL)) {
		/* enable defio at last moment if not disabled by client */

		struct fb_deferred_io *fbdefio;

		fbdefio = kzalloc(sizeof(struct fb_deferred_io), GFP_KERNEL);

		if (fbdefio) {
			fbdefio->delay = UFX_DEFIO_WRITE_DELAY;
			fbdefio->deferred_io = ufx_dpy_deferred_io;
		}

		info->fbdefio = fbdefio;
		fb_deferred_io_init(info);
	}

	pr_debug("open /dev/fb%d user=%d fb_info=%p count=%d",
		info->node, user, info, dev->fb_count);

	return 0;
}

/*
 * Called when all client interfaces to start transactions have been disabled,
 * and all references to our device instance (ufx_data) are released.
 * Every transaction must have a reference, so we know are fully spun down
 */
static void ufx_free(struct kref *kref)
{
	struct ufx_data *dev = container_of(kref, struct ufx_data, kref);

	/* this function will wait for all in-flight urbs to complete */
	if (dev->urbs.count > 0)
		ufx_free_urb_list(dev);

	pr_debug("freeing ufx_data %p", dev);

	kfree(dev);
}

static void ufx_release_urb_work(struct work_struct *work)
{
	struct urb_node *unode = container_of(work, struct urb_node,
					      release_urb_work.work);

	up(&unode->dev->urbs.limit_sem);
}

static void ufx_free_framebuffer_work(struct work_struct *work)
{
	struct ufx_data *dev = container_of(work, struct ufx_data,
					    free_framebuffer_work.work);
	struct fb_info *info = dev->info;
	int node = info->node;

	unregister_framebuffer(info);

	if (info->cmap.len != 0)
		fb_dealloc_cmap(&info->cmap);
	if (info->monspecs.modedb)
		fb_destroy_modedb(info->monspecs.modedb);
	if (info->screen_base)
		vfree(info->screen_base);

	fb_destroy_modelist(&info->modelist);

	dev->info = 0;

	/* Assume info structure is freed after this point */
	framebuffer_release(info);

	pr_debug("fb_info for /dev/fb%d has been freed", node);

	/* ref taken in probe() as part of registering framebfufer */
	kref_put(&dev->kref, ufx_free);
}

/*
 * Assumes caller is holding info->lock mutex (for open and release at least)
 */
static int ufx_ops_release(struct fb_info *info, int user)
{
	struct ufx_data *dev = info->par;

	dev->fb_count--;

	/* We can't free fb_info here - fbmem will touch it when we return */
	if (dev->virtualized && (dev->fb_count == 0))
		schedule_delayed_work(&dev->free_framebuffer_work, HZ);

	if ((dev->fb_count == 0) && (info->fbdefio)) {
		fb_deferred_io_cleanup(info);
		kfree(info->fbdefio);
		info->fbdefio = NULL;
		info->fbops->fb_mmap = ufx_ops_mmap;
	}

	pr_debug("released /dev/fb%d user=%d count=%d",
		  info->node, user, dev->fb_count);

	kref_put(&dev->kref, ufx_free);

	return 0;
}

/* Check whether a video mode is supported by the chip
 * We start from monitor's modes, so don't need to filter that here */
static int ufx_is_valid_mode(struct fb_videomode *mode,
		struct fb_info *info)
{
	if ((mode->xres * mode->yres) > (2048 * 1152)) {
		pr_debug("%dx%d too many pixels",
		       mode->xres, mode->yres);
		return 0;
	}

	if (mode->pixclock < 5000) {
		pr_debug("%dx%d %dps pixel clock too fast",
		       mode->xres, mode->yres, mode->pixclock);
		return 0;
	}

	pr_debug("%dx%d (pixclk %dps %dMHz) valid mode", mode->xres, mode->yres,
		mode->pixclock, (1000000 / mode->pixclock));
	return 1;
}

static void ufx_var_color_format(struct fb_var_screeninfo *var)
{
	const struct fb_bitfield red = { 11, 5, 0 };
	const struct fb_bitfield green = { 5, 6, 0 };
	const struct fb_bitfield blue = { 0, 5, 0 };

	var->bits_per_pixel = 16;
	var->red = red;
	var->green = green;
	var->blue = blue;
}

static int ufx_ops_check_var(struct fb_var_screeninfo *var,
				struct fb_info *info)
{
	struct fb_videomode mode;

	/* TODO: support dynamically changing framebuffer size */
	if ((var->xres * var->yres * 2) > info->fix.smem_len)
		return -EINVAL;

	/* set device-specific elements of var unrelated to mode */
	ufx_var_color_format(var);

	fb_var_to_videomode(&mode, var);

	if (!ufx_is_valid_mode(&mode, info))
		return -EINVAL;

	return 0;
}

static int ufx_ops_set_par(struct fb_info *info)
{
	struct ufx_data *dev = info->par;
	int result;
	u16 *pix_framebuffer;
	int i;

	pr_debug("set_par mode %dx%d", info->var.xres, info->var.yres);
	result = ufx_set_vid_mode(dev, &info->var);

	if ((result == 0) && (dev->fb_count == 0)) {
		/* paint greenscreen */
		pix_framebuffer = (u16 *) info->screen_base;
		for (i = 0; i < info->fix.smem_len / 2; i++)
			pix_framebuffer[i] = 0x37e6;

		ufx_handle_damage(dev, 0, 0, info->var.xres, info->var.yres);
	}

	/* re-enable defio if previously disabled by damage tracking */
	if (info->fbdefio)
		info->fbdefio->delay = UFX_DEFIO_WRITE_DELAY;

	return result;
}

/* In order to come back from full DPMS off, we need to set the mode again */
static int ufx_ops_blank(int blank_mode, struct fb_info *info)
{
	struct ufx_data *dev = info->par;
	ufx_set_vid_mode(dev, &info->var);
	return 0;
}

static struct fb_ops ufx_ops = {
	.owner = THIS_MODULE,
	.fb_read = fb_sys_read,
	.fb_write = ufx_ops_write,
	.fb_setcolreg = ufx_ops_setcolreg,
	.fb_fillrect = ufx_ops_fillrect,
	.fb_copyarea = ufx_ops_copyarea,
	.fb_imageblit = ufx_ops_imageblit,
	.fb_mmap = ufx_ops_mmap,
	.fb_ioctl = ufx_ops_ioctl,
	.fb_open = ufx_ops_open,
	.fb_release = ufx_ops_release,
	.fb_blank = ufx_ops_blank,
	.fb_check_var = ufx_ops_check_var,
	.fb_set_par = ufx_ops_set_par,
};

/* Assumes &info->lock held by caller
 * Assumes no active clients have framebuffer open */
static int ufx_realloc_framebuffer(struct ufx_data *dev, struct fb_info *info)
{
	int retval = -ENOMEM;
	int old_len = info->fix.smem_len;
	int new_len;
	unsigned char *old_fb = info->screen_base;
	unsigned char *new_fb;

	pr_debug("Reallocating framebuffer. Addresses will change!");

	new_len = info->fix.line_length * info->var.yres;

	if (PAGE_ALIGN(new_len) > old_len) {
		/*
		 * Alloc system memory for virtual framebuffer
		 */
		new_fb = vmalloc(new_len);
		if (!new_fb) {
			pr_err("Virtual framebuffer alloc failed");
			goto error;
		}

		if (info->screen_base) {
			memcpy(new_fb, old_fb, old_len);
			vfree(info->screen_base);
		}

		info->screen_base = new_fb;
		info->fix.smem_len = PAGE_ALIGN(new_len);
		info->fix.smem_start = (unsigned long) new_fb;
		info->flags = smscufx_info_flags;
	}

	retval = 0;

error:
	return retval;
}

/* sets up I2C Controller for 100 Kbps, std. speed, 7-bit addr, master,
 * restart enabled, but no start byte, enable controller */
static int ufx_i2c_init(struct ufx_data *dev)
{
	u32 tmp;

	/* disable the controller before it can be reprogrammed */
	int status = ufx_reg_write(dev, 0x106C, 0x00);
	check_warn_return(status, "failed to disable I2C");

	/* Setup the clock count registers
	 * (12+1) = 13 clks @ 2.5 MHz = 5.2 uS */
	status = ufx_reg_write(dev, 0x1018, 12);
	check_warn_return(status, "error writing 0x1018");

	/* (6+8) = 14 clks @ 2.5 MHz = 5.6 uS */
	status = ufx_reg_write(dev, 0x1014, 6);
	check_warn_return(status, "error writing 0x1014");

	status = ufx_reg_read(dev, 0x1000, &tmp);
	check_warn_return(status, "error reading 0x1000");

	/* set speed to std mode */
	tmp &= ~(0x06);
	tmp |= 0x02;

	/* 7-bit (not 10-bit) addressing */
	tmp &= ~(0x10);

	/* enable restart conditions and master mode */
	tmp |= 0x21;

	status = ufx_reg_write(dev, 0x1000, tmp);
	check_warn_return(status, "error writing 0x1000");

	/* Set normal tx using target address 0 */
	status = ufx_reg_clear_and_set_bits(dev, 0x1004, 0xC00, 0x000);
	check_warn_return(status, "error setting TX mode bits in 0x1004");

	/* Enable the controller */
	status = ufx_reg_write(dev, 0x106C, 0x01);
	check_warn_return(status, "failed to enable I2C");

	return 0;
}

/* sets the I2C port mux and target address */
static int ufx_i2c_configure(struct ufx_data *dev)
{
	int status = ufx_reg_write(dev, 0x106C, 0x00);
	check_warn_return(status, "failed to disable I2C");

	status = ufx_reg_write(dev, 0x3010, 0x00000000);
	check_warn_return(status, "failed to write 0x3010");

	/* A0h is std for any EDID, right shifted by one */
	status = ufx_reg_clear_and_set_bits(dev, 0x1004, 0x3FF,	(0xA0 >> 1));
	check_warn_return(status, "failed to set TAR bits in 0x1004");

	status = ufx_reg_write(dev, 0x106C, 0x01);
	check_warn_return(status, "failed to enable I2C");

	return 0;
}

/* wait for BUSY to clear, with a timeout of 50ms with 10ms sleeps. if no
 * monitor is connected, there is no error except for timeout */
static int ufx_i2c_wait_busy(struct ufx_data *dev)
{
	u32 tmp;
	int i, status;

	for (i = 0; i < 15; i++) {
		status = ufx_reg_read(dev, 0x1100, &tmp);
		check_warn_return(status, "0x1100 read failed");

		/* if BUSY is clear, check for error */
		if ((tmp & 0x80000000) == 0) {
			if (tmp & 0x20000000) {
				pr_warn("I2C read failed, 0x1100=0x%08x", tmp);
				return -EIO;
			}

			return 0;
		}

		/* perform the first 10 retries without delay */
		if (i >= 10)
			msleep(10);
	}

	pr_warn("I2C access timed out, resetting I2C hardware");
	status =  ufx_reg_write(dev, 0x1100, 0x40000000);
	check_warn_return(status, "0x1100 write failed");

	return -ETIMEDOUT;
}

/* reads a 128-byte EDID block from the currently selected port and TAR */
static int ufx_read_edid(struct ufx_data *dev, u8 *edid, int edid_len)
{
	int i, j, status;
	u32 *edid_u32 = (u32 *)edid;

	BUG_ON(edid_len != EDID_LENGTH);

	status = ufx_i2c_configure(dev);
	if (status < 0) {
		pr_err("ufx_i2c_configure failed");
		return status;
	}

	memset(edid, 0xff, EDID_LENGTH);

	/* Read the 128-byte EDID as 2 bursts of 64 bytes */
	for (i = 0; i < 2; i++) {
		u32 temp = 0x28070000 | (63 << 20) | (((u32)(i * 64)) << 8);
		status = ufx_reg_write(dev, 0x1100, temp);
		check_warn_return(status, "Failed to write 0x1100");

		temp |= 0x80000000;
		status = ufx_reg_write(dev, 0x1100, temp);
		check_warn_return(status, "Failed to write 0x1100");

		status = ufx_i2c_wait_busy(dev);
		check_warn_return(status, "Timeout waiting for I2C BUSY to clear");

		for (j = 0; j < 16; j++) {
			u32 data_reg_addr = 0x1110 + (j * 4);
			status = ufx_reg_read(dev, data_reg_addr, edid_u32++);
			check_warn_return(status, "Error reading i2c data");
		}
	}

	/* all FF's in the first 16 bytes indicates nothing is connected */
	for (i = 0; i < 16; i++) {
		if (edid[i] != 0xFF) {
			pr_debug("edid data read successfully");
			return EDID_LENGTH;
		}
	}

	pr_warn("edid data contains all 0xff");
	return -ETIMEDOUT;
}

/* 1) use sw default
 * 2) Parse into various fb_info structs
 * 3) Allocate virtual framebuffer memory to back highest res mode
 *
 * Parses EDID into three places used by various parts of fbdev:
 * fb_var_screeninfo contains the timing of the monitor's preferred mode
 * fb_info.monspecs is full parsed EDID info, including monspecs.modedb
 * fb_info.modelist is a linked list of all monitor & VESA modes which work
 *
 * If EDID is not readable/valid, then modelist is all VESA modes,
 * monspecs is NULL, and fb_var_screeninfo is set to safe VESA mode
 * Returns 0 if successful */
static int ufx_setup_modes(struct ufx_data *dev, struct fb_info *info,
	char *default_edid, size_t default_edid_size)
{
	const struct fb_videomode *default_vmode = NULL;
	u8 *edid;
	int i, result = 0, tries = 3;

	if (info->dev) /* only use mutex if info has been registered */
		mutex_lock(&info->lock);

	edid = kmalloc(EDID_LENGTH, GFP_KERNEL);
	if (!edid) {
		result = -ENOMEM;
		goto error;
	}

	fb_destroy_modelist(&info->modelist);
	memset(&info->monspecs, 0, sizeof(info->monspecs));

	/* Try to (re)read EDID from hardware first
	 * EDID data may return, but not parse as valid
	 * Try again a few times, in case of e.g. analog cable noise */
	while (tries--) {
		i = ufx_read_edid(dev, edid, EDID_LENGTH);

		if (i >= EDID_LENGTH)
			fb_edid_to_monspecs(edid, &info->monspecs);

		if (info->monspecs.modedb_len > 0) {
			dev->edid = edid;
			dev->edid_size = i;
			break;
		}
	}

	/* If that fails, use a previously returned EDID if available */
	if (info->monspecs.modedb_len == 0) {
		pr_err("Unable to get valid EDID from device/display\n");

		if (dev->edid) {
			fb_edid_to_monspecs(dev->edid, &info->monspecs);
			if (info->monspecs.modedb_len > 0)
				pr_err("Using previously queried EDID\n");
		}
	}

	/* If that fails, use the default EDID we were handed */
	if (info->monspecs.modedb_len == 0) {
		if (default_edid_size >= EDID_LENGTH) {
			fb_edid_to_monspecs(default_edid, &info->monspecs);
			if (info->monspecs.modedb_len > 0) {
				memcpy(edid, default_edid, default_edid_size);
				dev->edid = edid;
				dev->edid_size = default_edid_size;
				pr_err("Using default/backup EDID\n");
			}
		}
	}

	/* If we've got modes, let's pick a best default mode */
	if (info->monspecs.modedb_len > 0) {

		for (i = 0; i < info->monspecs.modedb_len; i++) {
			if (ufx_is_valid_mode(&info->monspecs.modedb[i], info))
				fb_add_videomode(&info->monspecs.modedb[i],
					&info->modelist);
			else /* if we've removed top/best mode */
				info->monspecs.misc &= ~FB_MISC_1ST_DETAIL;
		}

		default_vmode = fb_find_best_display(&info->monspecs,
						     &info->modelist);
	}

	/* If everything else has failed, fall back to safe default mode */
	if (default_vmode == NULL) {

		struct fb_videomode fb_vmode = {0};

		/* Add the standard VESA modes to our modelist
		 * Since we don't have EDID, there may be modes that
		 * overspec monitor and/or are incorrect aspect ratio, etc.
		 * But at least the user has a chance to choose
		 */
		for (i = 0; i < VESA_MODEDB_SIZE; i++) {
			if (ufx_is_valid_mode((struct fb_videomode *)
						&vesa_modes[i], info))
				fb_add_videomode(&vesa_modes[i],
						 &info->modelist);
		}

		/* default to resolution safe for projectors
		 * (since they are most common case without EDID)
		 */
		fb_vmode.xres = 800;
		fb_vmode.yres = 600;
		fb_vmode.refresh = 60;
		default_vmode = fb_find_nearest_mode(&fb_vmode,
						     &info->modelist);
	}

	/* If we have good mode and no active clients */
	if ((default_vmode != NULL) && (dev->fb_count == 0)) {

		fb_videomode_to_var(&info->var, default_vmode);
		ufx_var_color_format(&info->var);

		/* with mode size info, we can now alloc our framebuffer */
		memcpy(&info->fix, &ufx_fix, sizeof(ufx_fix));
		info->fix.line_length = info->var.xres *
			(info->var.bits_per_pixel / 8);

		result = ufx_realloc_framebuffer(dev, info);

	} else
		result = -EINVAL;

error:
	if (edid && (dev->edid != edid))
		kfree(edid);

	if (info->dev)
		mutex_unlock(&info->lock);

	return result;
}

static int ufx_usb_probe(struct usb_interface *interface,
			const struct usb_device_id *id)
{
	struct usb_device *usbdev;
	struct ufx_data *dev;
	struct fb_info *info = 0;
	int retval = -ENOMEM;
	u32 id_rev, fpga_rev;

	/* usb initialization */
	usbdev = interface_to_usbdev(interface);
	BUG_ON(!usbdev);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		dev_err(&usbdev->dev, "ufx_usb_probe: failed alloc of dev struct\n");
		goto error;
	}

	/* we need to wait for both usb and fbdev to spin down on disconnect */
	kref_init(&dev->kref); /* matching kref_put in usb .disconnect fn */
	kref_get(&dev->kref); /* matching kref_put in free_framebuffer_work */

	dev->udev = usbdev;
	dev->gdev = &usbdev->dev; /* our generic struct device * */
	usb_set_intfdata(interface, dev);

	dev_dbg(dev->gdev, "%s %s - serial #%s\n",
		usbdev->manufacturer, usbdev->product, usbdev->serial);
	dev_dbg(dev->gdev, "vid_%04x&pid_%04x&rev_%04x driver's ufx_data struct at %p\n",
		usbdev->descriptor.idVendor, usbdev->descriptor.idProduct,
		usbdev->descriptor.bcdDevice, dev);
	dev_dbg(dev->gdev, "console enable=%d\n", console);
	dev_dbg(dev->gdev, "fb_defio enable=%d\n", fb_defio);

	if (!ufx_alloc_urb_list(dev, WRITES_IN_FLIGHT, MAX_TRANSFER)) {
		retval = -ENOMEM;
		dev_err(dev->gdev, "ufx_alloc_urb_list failed\n");
		goto error;
	}

	/* We don't register a new USB class. Our client interface is fbdev */

	/* allocates framebuffer driver structure, not framebuffer memory */
	info = framebuffer_alloc(0, &usbdev->dev);
	if (!info) {
		retval = -ENOMEM;
		dev_err(dev->gdev, "framebuffer_alloc failed\n");
		goto error;
	}

	dev->info = info;
	info->par = dev;
	info->pseudo_palette = dev->pseudo_palette;
	info->fbops = &ufx_ops;

	retval = fb_alloc_cmap(&info->cmap, 256, 0);
	if (retval < 0) {
		dev_err(dev->gdev, "fb_alloc_cmap failed %x\n", retval);
		goto error;
	}

	INIT_DELAYED_WORK(&dev->free_framebuffer_work,
			  ufx_free_framebuffer_work);

	INIT_LIST_HEAD(&info->modelist);

	retval = ufx_reg_read(dev, 0x3000, &id_rev);
	check_warn_goto_error(retval, "error %d reading 0x3000 register from device", retval);
	dev_dbg(dev->gdev, "ID_REV register value 0x%08x", id_rev);

	retval = ufx_reg_read(dev, 0x3004, &fpga_rev);
	check_warn_goto_error(retval, "error %d reading 0x3004 register from device", retval);
	dev_dbg(dev->gdev, "FPGA_REV register value 0x%08x", fpga_rev);

	dev_dbg(dev->gdev, "resetting device");
	retval = ufx_lite_reset(dev);
	check_warn_goto_error(retval, "error %d resetting device", retval);

	dev_dbg(dev->gdev, "configuring system clock");
	retval = ufx_config_sys_clk(dev);
	check_warn_goto_error(retval, "error %d configuring system clock", retval);

	dev_dbg(dev->gdev, "configuring DDR2 controller");
	retval = ufx_config_ddr2(dev);
	check_warn_goto_error(retval, "error %d initialising DDR2 controller", retval);

	dev_dbg(dev->gdev, "configuring I2C controller");
	retval = ufx_i2c_init(dev);
	check_warn_goto_error(retval, "error %d initialising I2C controller", retval);

	dev_dbg(dev->gdev, "selecting display mode");
	retval = ufx_setup_modes(dev, info, NULL, 0);
	check_warn_goto_error(retval, "unable to find common mode for display and adapter");

	retval = ufx_reg_set_bits(dev, 0x4000, 0x00000001);
	check_warn_goto_error(retval, "error %d enabling graphics engine", retval);

	/* ready to begin using device */
	atomic_set(&dev->usb_active, 1);

	dev_dbg(dev->gdev, "checking var");
	retval = ufx_ops_check_var(&info->var, info);
	check_warn_goto_error(retval, "error %d ufx_ops_check_var", retval);

	dev_dbg(dev->gdev, "setting par");
	retval = ufx_ops_set_par(info);
	check_warn_goto_error(retval, "error %d ufx_ops_set_par", retval);

	dev_dbg(dev->gdev, "registering framebuffer");
	retval = register_framebuffer(info);
	check_warn_goto_error(retval, "error %d register_framebuffer", retval);

	dev_info(dev->gdev, "SMSC UDX USB device /dev/fb%d attached. %dx%d resolution."
		" Using %dK framebuffer memory\n", info->node,
		info->var.xres, info->var.yres, info->fix.smem_len >> 10);

	return 0;

error:
	if (dev) {
		if (info) {
			if (info->cmap.len != 0)
				fb_dealloc_cmap(&info->cmap);
			if (info->monspecs.modedb)
				fb_destroy_modedb(info->monspecs.modedb);
			if (info->screen_base)
				vfree(info->screen_base);

			fb_destroy_modelist(&info->modelist);

			framebuffer_release(info);
		}

		kref_put(&dev->kref, ufx_free); /* ref for framebuffer */
		kref_put(&dev->kref, ufx_free); /* last ref from kref_init */

		/* dev has been deallocated. Do not dereference */
	}

	return retval;
}

static void ufx_usb_disconnect(struct usb_interface *interface)
{
	struct ufx_data *dev;
	struct fb_info *info;

	dev = usb_get_intfdata(interface);
	info = dev->info;

	pr_debug("USB disconnect starting\n");

	/* we virtualize until all fb clients release. Then we free */
	dev->virtualized = true;

	/* When non-active we'll update virtual framebuffer, but no new urbs */
	atomic_set(&dev->usb_active, 0);

	usb_set_intfdata(interface, NULL);

	/* if clients still have us open, will be freed on last close */
	if (dev->fb_count == 0)
		schedule_delayed_work(&dev->free_framebuffer_work, 0);

	/* release reference taken by kref_init in probe() */
	kref_put(&dev->kref, ufx_free);

	/* consider ufx_data freed */
}

static struct usb_driver ufx_driver = {
	.name = "smscufx",
	.probe = ufx_usb_probe,
	.disconnect = ufx_usb_disconnect,
	.id_table = id_table,
};

module_usb_driver(ufx_driver);

static void ufx_urb_completion(struct urb *urb)
{
	struct urb_node *unode = urb->context;
	struct ufx_data *dev = unode->dev;
	unsigned long flags;

	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN)) {
			pr_err("%s - nonzero write bulk status received: %d\n",
				__func__, urb->status);
			atomic_set(&dev->lost_pixels, 1);
		}
	}

	urb->transfer_buffer_length = dev->urbs.size; /* reset to actual */

	spin_lock_irqsave(&dev->urbs.lock, flags);
	list_add_tail(&unode->entry, &dev->urbs.list);
	dev->urbs.available++;
	spin_unlock_irqrestore(&dev->urbs.lock, flags);

	/* When using fb_defio, we deadlock if up() is called
	 * while another is waiting. So queue to another process */
	if (fb_defio)
		schedule_delayed_work(&unode->release_urb_work, 0);
	else
		up(&dev->urbs.limit_sem);
}

static void ufx_free_urb_list(struct ufx_data *dev)
{
	int count = dev->urbs.count;
	struct list_head *node;
	struct urb_node *unode;
	struct urb *urb;
	int ret;
	unsigned long flags;

	pr_debug("Waiting for completes and freeing all render urbs\n");

	/* keep waiting and freeing, until we've got 'em all */
	while (count--) {
		/* Getting interrupted means a leak, but ok at shutdown*/
		ret = down_interruptible(&dev->urbs.limit_sem);
		if (ret)
			break;

		spin_lock_irqsave(&dev->urbs.lock, flags);

		node = dev->urbs.list.next; /* have reserved one with sem */
		list_del_init(node);

		spin_unlock_irqrestore(&dev->urbs.lock, flags);

		unode = list_entry(node, struct urb_node, entry);
		urb = unode->urb;

		/* Free each separately allocated piece */
		usb_free_coherent(urb->dev, dev->urbs.size,
				  urb->transfer_buffer, urb->transfer_dma);
		usb_free_urb(urb);
		kfree(node);
	}
}

static int ufx_alloc_urb_list(struct ufx_data *dev, int count, size_t size)
{
	int i = 0;
	struct urb *urb;
	struct urb_node *unode;
	char *buf;

	spin_lock_init(&dev->urbs.lock);

	dev->urbs.size = size;
	INIT_LIST_HEAD(&dev->urbs.list);

	while (i < count) {
		unode = kzalloc(sizeof(struct urb_node), GFP_KERNEL);
		if (!unode)
			break;
		unode->dev = dev;

		INIT_DELAYED_WORK(&unode->release_urb_work,
			  ufx_release_urb_work);

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			kfree(unode);
			break;
		}
		unode->urb = urb;

		buf = usb_alloc_coherent(dev->udev, size, GFP_KERNEL,
					 &urb->transfer_dma);
		if (!buf) {
			kfree(unode);
			usb_free_urb(urb);
			break;
		}

		/* urb->transfer_buffer_length set to actual before submit */
		usb_fill_bulk_urb(urb, dev->udev, usb_sndbulkpipe(dev->udev, 1),
			buf, size, ufx_urb_completion, unode);
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

		list_add_tail(&unode->entry, &dev->urbs.list);

		i++;
	}

	sema_init(&dev->urbs.limit_sem, i);
	dev->urbs.count = i;
	dev->urbs.available = i;

	pr_debug("allocated %d %d byte urbs\n", i, (int) size);

	return i;
}

static struct urb *ufx_get_urb(struct ufx_data *dev)
{
	int ret = 0;
	struct list_head *entry;
	struct urb_node *unode;
	struct urb *urb = NULL;
	unsigned long flags;

	/* Wait for an in-flight buffer to complete and get re-queued */
	ret = down_timeout(&dev->urbs.limit_sem, GET_URB_TIMEOUT);
	if (ret) {
		atomic_set(&dev->lost_pixels, 1);
		pr_warn("wait for urb interrupted: %x available: %d\n",
		       ret, dev->urbs.available);
		goto error;
	}

	spin_lock_irqsave(&dev->urbs.lock, flags);

	BUG_ON(list_empty(&dev->urbs.list)); /* reserved one with limit_sem */
	entry = dev->urbs.list.next;
	list_del_init(entry);
	dev->urbs.available--;

	spin_unlock_irqrestore(&dev->urbs.lock, flags);

	unode = list_entry(entry, struct urb_node, entry);
	urb = unode->urb;

error:
	return urb;
}

static int ufx_submit_urb(struct ufx_data *dev, struct urb *urb, size_t len)
{
	int ret;

	BUG_ON(len > dev->urbs.size);

	urb->transfer_buffer_length = len; /* set to actual payload len */
	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret) {
		ufx_urb_completion(urb); /* because no one else will */
		atomic_set(&dev->lost_pixels, 1);
		pr_err("usb_submit_urb error %x\n", ret);
	}
	return ret;
}

module_param(console, bool, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(console, "Allow fbcon to be used on this display");

module_param(fb_defio, bool, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(fb_defio, "Enable fb_defio mmap support");

MODULE_AUTHOR("Steve Glendinning <steve.glendinning@shawell.net>");
MODULE_DESCRIPTION("SMSC UFX kernel framebuffer driver");
MODULE_LICENSE("GPL");
