// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/apollohw.h>
#include <linux/fb.h>
#include <linux/module.h>

/* apollo video HW definitions */

/*
 * Control Registers.   IOBASE + $x
 *
 * Note: these are the Memory/IO BASE definitions for a mono card set to the
 * alternate address
 *
 * Control 3A and 3B serve identical functions except that 3A
 * deals with control 1 and 3b deals with Color LUT reg.
 */

#define AP_IOBASE       0x3b0	/* Base address of 1 plane board. */
#define AP_STATUS       isaIO2mem(AP_IOBASE+0)	/* Status register.  Read */
#define AP_WRITE_ENABLE isaIO2mem(AP_IOBASE+0)	/* Write Enable Register Write */
#define AP_DEVICE_ID    isaIO2mem(AP_IOBASE+1)	/* Device ID Register. Read */
#define AP_ROP_1        isaIO2mem(AP_IOBASE+2)	/* Raster Operation reg. Write Word */
#define AP_DIAG_MEM_REQ isaIO2mem(AP_IOBASE+4)	/* Diagnostic Memory Request. Write Word */
#define AP_CONTROL_0    isaIO2mem(AP_IOBASE+8)	/* Control Register 0.  Read/Write */
#define AP_CONTROL_1    isaIO2mem(AP_IOBASE+0xa)	/* Control Register 1.  Read/Write */
#define AP_CONTROL_3A   isaIO2mem(AP_IOBASE+0xe)	/* Control Register 3a. Read/Write */
#define AP_CONTROL_2    isaIO2mem(AP_IOBASE+0xc)	/* Control Register 2. Read/Write */


#define FRAME_BUFFER_START 0x0FA0000
#define FRAME_BUFFER_LEN 0x40000

/* CREG 0 */
#define VECTOR_MODE 0x40	/* 010x.xxxx */
#define DBLT_MODE   0x80	/* 100x.xxxx */
#define NORMAL_MODE 0xE0	/* 111x.xxxx */
#define SHIFT_BITS  0x1F	/* xxx1.1111 */
	/* other bits are Shift value */

/* CREG 1 */
#define AD_BLT      0x80	/* 1xxx.xxxx */
#define NORMAL      0x80 /* 1xxx.xxxx */	/* What is happening here ?? */
#define INVERSE     0x00 /* 0xxx.xxxx */	/* Clearing this reverses the screen */
#define PIX_BLT     0x00	/* 0xxx.xxxx */

#define AD_HIBIT        0x40	/* xIxx.xxxx */

#define ROP_EN          0x10	/* xxx1.xxxx */
#define DST_EQ_SRC      0x00	/* xxx0.xxxx */
#define nRESET_SYNC     0x08	/* xxxx.1xxx */
#define SYNC_ENAB       0x02	/* xxxx.xx1x */

#define BLANK_DISP      0x00	/* xxxx.xxx0 */
#define ENAB_DISP       0x01	/* xxxx.xxx1 */

#define NORM_CREG1      (nRESET_SYNC | SYNC_ENAB | ENAB_DISP)	/* no reset sync */

/* CREG 2 */

/*
 * Following 3 defines are common to 1, 4 and 8 plane.
 */

#define S_DATA_1s   0x00 /* 00xx.xxxx */	/* set source to all 1's -- vector drawing */
#define S_DATA_PIX  0x40 /* 01xx.xxxx */	/* takes source from ls-bits and replicates over 16 bits */
#define S_DATA_PLN  0xC0 /* 11xx.xxxx */	/* normal, each data access =16-bits in
						   one plane of image mem */

/* CREG 3A/CREG 3B */
#       define RESET_CREG 0x80	/* 1000.0000 */

/* ROP REG  -  all one nibble */
/*      ********* NOTE : this is used r0,r1,r2,r3 *********** */
#define ROP(r2,r3,r0,r1) ( (U_SHORT)((r0)|((r1)<<4)|((r2)<<8)|((r3)<<12)) )
#define DEST_ZERO               0x0
#define SRC_AND_DEST    0x1
#define SRC_AND_nDEST   0x2
#define SRC                             0x3
#define nSRC_AND_DEST   0x4
#define DEST                    0x5
#define SRC_XOR_DEST    0x6
#define SRC_OR_DEST             0x7
#define SRC_NOR_DEST    0x8
#define SRC_XNOR_DEST   0x9
#define nDEST                   0xA
#define SRC_OR_nDEST    0xB
#define nSRC                    0xC
#define nSRC_OR_DEST    0xD
#define SRC_NAND_DEST   0xE
#define DEST_ONE                0xF

#define SWAP(A) ((A>>8) | ((A&0xff) <<8))

/* frame buffer operations */

static int dnfb_blank(int blank, struct fb_info *info);
static void dnfb_copyarea(struct fb_info *info, const struct fb_copyarea *area);

static struct fb_ops dn_fb_ops = {
	.owner		= THIS_MODULE,
	.fb_blank	= dnfb_blank,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= dnfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static const struct fb_var_screeninfo dnfb_var = {
	.xres		= 1280,
	.yres		= 1024,
	.xres_virtual	= 2048,
	.yres_virtual	= 1024,
	.bits_per_pixel	= 1,
	.height		= -1,
	.width		= -1,
	.vmode		= FB_VMODE_NONINTERLACED,
};

static const struct fb_fix_screeninfo dnfb_fix = {
	.id		= "Apollo Mono",
	.smem_start	= (FRAME_BUFFER_START + IO_BASE),
	.smem_len	= FRAME_BUFFER_LEN,
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_MONO10,
	.line_length	= 256,
};

static int dnfb_blank(int blank, struct fb_info *info)
{
	if (blank)
		out_8(AP_CONTROL_3A, 0x0);
	else
		out_8(AP_CONTROL_3A, 0x1);
	return 0;
}

static
void dnfb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{

	int incr, y_delta, pre_read = 0, x_end, x_word_count;
	uint start_mask, end_mask, dest;
	ushort *src, dummy;
	short i, j;

	incr = (area->dy <= area->sy) ? 1 : -1;

	src = (ushort *)(info->screen_base + area->sy * info->fix.line_length +
			(area->sx >> 4));
	dest = area->dy * (info->fix.line_length >> 1) + (area->dx >> 4);

	if (incr > 0) {
		y_delta = (info->fix.line_length * 8) - area->sx - area->width;
		x_end = area->dx + area->width - 1;
		x_word_count = (x_end >> 4) - (area->dx >> 4) + 1;
		start_mask = 0xffff0000 >> (area->dx & 0xf);
		end_mask = 0x7ffff >> (x_end & 0xf);
		out_8(AP_CONTROL_0,
		     (((area->dx & 0xf) - (area->sx & 0xf)) % 16) | (0x4 << 5));
		if ((area->dx & 0xf) < (area->sx & 0xf))
			pre_read = 1;
	} else {
		y_delta = -((info->fix.line_length * 8) - area->sx - area->width);
		x_end = area->dx - area->width + 1;
		x_word_count = (area->dx >> 4) - (x_end >> 4) + 1;
		start_mask = 0x7ffff >> (area->dx & 0xf);
		end_mask = 0xffff0000 >> (x_end & 0xf);
		out_8(AP_CONTROL_0,
		     ((-((area->sx & 0xf) - (area->dx & 0xf))) % 16) |
		     (0x4 << 5));
		if ((area->dx & 0xf) > (area->sx & 0xf))
			pre_read = 1;
	}

	for (i = 0; i < area->height; i++) {

		out_8(AP_CONTROL_3A, 0xc | (dest >> 16));

		if (pre_read) {
			dummy = *src;
			src += incr;
		}

		if (x_word_count) {
			out_8(AP_WRITE_ENABLE, start_mask);
			*src = dest;
			src += incr;
			dest += incr;
			out_8(AP_WRITE_ENABLE, 0);

			for (j = 1; j < (x_word_count - 1); j++) {
				*src = dest;
				src += incr;
				dest += incr;
			}

			out_8(AP_WRITE_ENABLE, start_mask);
			*src = dest;
			dest += incr;
			src += incr;
		} else {
			out_8(AP_WRITE_ENABLE, start_mask | end_mask);
			*src = dest;
			dest += incr;
			src += incr;
		}
		src += (y_delta / 16);
		dest += (y_delta / 16);
	}
	out_8(AP_CONTROL_0, NORMAL_MODE);
}

/*
 * Initialization
 */

static int dnfb_probe(struct platform_device *dev)
{
	struct fb_info *info;
	int err = 0;

	info = framebuffer_alloc(0, &dev->dev);
	if (!info)
		return -ENOMEM;

	info->fbops = &dn_fb_ops;
	info->fix = dnfb_fix;
	info->var = dnfb_var;
	info->var.red.length = 1;
	info->var.red.offset = 0;
	info->var.green = info->var.blue = info->var.red;
	info->screen_base = (u_char *) info->fix.smem_start;

	err = fb_alloc_cmap(&info->cmap, 2, 0);
	if (err < 0)
		goto release_framebuffer;

	err = register_framebuffer(info);
	if (err < 0) {
		fb_dealloc_cmap(&info->cmap);
		goto release_framebuffer;
	}
	platform_set_drvdata(dev, info);

	/* now we have registered we can safely setup the hardware */
	out_8(AP_CONTROL_3A, RESET_CREG);
	out_be16(AP_WRITE_ENABLE, 0x0);
	out_8(AP_CONTROL_0, NORMAL_MODE);
	out_8(AP_CONTROL_1, (AD_BLT | DST_EQ_SRC | NORM_CREG1));
	out_8(AP_CONTROL_2, S_DATA_PLN);
	out_be16(AP_ROP_1, SWAP(0x3));

	printk("apollo frame buffer alive and kicking !\n");
	return err;

release_framebuffer:
	framebuffer_release(info);
	return err;
}

static struct platform_driver dnfb_driver = {
	.probe	= dnfb_probe,
	.driver	= {
		.name	= "dnfb",
	},
};

static struct platform_device dnfb_device = {
	.name	= "dnfb",
};

int __init dnfb_init(void)
{
	int ret;

	if (!MACH_IS_APOLLO)
		return -ENODEV;

	if (fb_get_options("dnfb", NULL))
		return -ENODEV;

	ret = platform_driver_register(&dnfb_driver);

	if (!ret) {
		ret = platform_device_register(&dnfb_device);
		if (ret)
			platform_driver_unregister(&dnfb_driver);
	}
	return ret;
}

module_init(dnfb_init);

MODULE_LICENSE("GPL");
