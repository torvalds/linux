/*
 *	HP300 Topcat framebuffer support (derived from macfb of all things)
 *	Phil Blundell <philb@gnu.org> 1998
 *	DIO-II, colour map and Catseye support by
 *	Kars de Jong <jongk@linux-m68k.org>, May 2004.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <linux/dio.h>

#include <asm/io.h>
#include <asm/uaccess.h>

static struct fb_info fb_info = {
	.fix = {
		.id		= "HP300 ",
		.type		= FB_TYPE_PACKED_PIXELS,
		.visual		= FB_VISUAL_PSEUDOCOLOR,
		.accel		= FB_ACCEL_NONE,
	}
};

static unsigned long fb_regs;
static unsigned char fb_bitmask;

#define TC_NBLANK	0x4080
#define TC_WEN		0x4088
#define TC_REN		0x408c
#define TC_FBEN		0x4090
#define TC_PRR		0x40ea

/* These defines match the X window system */
#define RR_CLEAR	0x0
#define RR_COPY		0x3
#define RR_NOOP		0x5
#define RR_XOR		0x6
#define RR_INVERT	0xa
#define RR_COPYINVERTED 0xc
#define RR_SET		0xf

/* blitter regs */
#define BUSY		0x4044
#define WMRR		0x40ef
#define SOURCE_X	0x40f2
#define SOURCE_Y	0x40f6
#define DEST_X		0x40fa
#define DEST_Y		0x40fe
#define WHEIGHT		0x4106
#define WWIDTH		0x4102
#define WMOVE		0x409c

static struct fb_var_screeninfo hpfb_defined = {
	.red		= {
		.length = 8,
	},
	.green		= {
		.length = 8,
	},
	.blue		= {
		.length = 8,
	},
	.activate	= FB_ACTIVATE_NOW,
	.height		= -1,
	.width		= -1,
	.vmode		= FB_VMODE_NONINTERLACED,
};

static int hpfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			  unsigned blue, unsigned transp,
			  struct fb_info *info)
{
	/* use MSBs */
	unsigned char _red  =red>>8;
	unsigned char _green=green>>8;
	unsigned char _blue =blue>>8;
	unsigned char _regno=regno;

	/*
	 *  Set a single color register. The values supplied are
	 *  already rounded down to the hardware's capabilities
	 *  (according to the entries in the `var' structure). Return
	 *  != 0 for invalid regno.
	 */

	if (regno >= info->cmap.len)
		return 1;
	
	while (in_be16(fb_regs + 0x6002) & 0x4) udelay(1);

	out_be16(fb_regs + 0x60ba, 0xff);

	out_be16(fb_regs + 0x60b2, _red);
	out_be16(fb_regs + 0x60b4, _green);
	out_be16(fb_regs + 0x60b6, _blue);
	out_be16(fb_regs + 0x60b8, ~_regno);
	out_be16(fb_regs + 0x60f0, 0xff);

	udelay(100);

	while (in_be16(fb_regs + 0x6002) & 0x4) udelay(1);
	out_be16(fb_regs + 0x60b2, 0);
	out_be16(fb_regs + 0x60b4, 0);
	out_be16(fb_regs + 0x60b6, 0);
	out_be16(fb_regs + 0x60b8, 0);

	return 0;
}

/* 0 unblank, 1 blank, 2 no vsync, 3 no hsync, 4 off */

static int hpfb_blank(int blank, struct fb_info *info)
{
	out_8(fb_regs + TC_NBLANK, (blank ? 0x00 : fb_bitmask));

	return 0;
}

static void topcat_blit(int x0, int y0, int x1, int y1, int w, int h, int rr)
{
	if (rr >= 0) {
		while (in_8(fb_regs + BUSY) & fb_bitmask)
			;
	}
	out_8(fb_regs + TC_FBEN, fb_bitmask);
	if (rr >= 0) {
		out_8(fb_regs + TC_WEN, fb_bitmask);
		out_8(fb_regs + WMRR, rr);
	}
	out_be16(fb_regs + SOURCE_X, x0);
	out_be16(fb_regs + SOURCE_Y, y0);
	out_be16(fb_regs + DEST_X, x1);
	out_be16(fb_regs + DEST_Y, y1);
	out_be16(fb_regs + WWIDTH, w);
	out_be16(fb_regs + WHEIGHT, h);
	out_8(fb_regs + WMOVE, fb_bitmask);
}

static void hpfb_copyarea(struct fb_info *info, const struct fb_copyarea *area) 
{
	topcat_blit(area->sx, area->sy, area->dx, area->dy, area->width, area->height, RR_COPY);
}

static void hpfb_fillrect(struct fb_info *p, const struct fb_fillrect *region)
{
	u8 clr;

	clr = region->color & 0xff;

	while (in_8(fb_regs + BUSY) & fb_bitmask)
		;

	/* Foreground */
	out_8(fb_regs + TC_WEN, fb_bitmask & clr);
	out_8(fb_regs + WMRR, (region->rop == ROP_COPY ? RR_SET : RR_INVERT));

	/* Background */
	out_8(fb_regs + TC_WEN, fb_bitmask & ~clr);
	out_8(fb_regs + WMRR, (region->rop == ROP_COPY ? RR_CLEAR : RR_NOOP));

	topcat_blit(region->dx, region->dy, region->dx, region->dy, region->width, region->height, -1);
}

static int hpfb_sync(struct fb_info *info)
{
	/*
	 * Since we also access the framebuffer directly, we have to wait
	 * until the block mover is finished
	 */
	while (in_8(fb_regs + BUSY) & fb_bitmask)
		;

	out_8(fb_regs + TC_WEN, fb_bitmask);
	out_8(fb_regs + TC_PRR, RR_COPY);
	out_8(fb_regs + TC_FBEN, fb_bitmask);

	return 0;
}

static struct fb_ops hpfb_ops = {
	.owner		= THIS_MODULE,
	.fb_setcolreg	= hpfb_setcolreg,
	.fb_blank	= hpfb_blank,
	.fb_fillrect	= hpfb_fillrect,
	.fb_copyarea	= hpfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_sync	= hpfb_sync,
};

/* Common to all HP framebuffers */
#define HPFB_FBWMSB	0x05	/* Frame buffer width 		*/
#define HPFB_FBWLSB	0x07
#define HPFB_FBHMSB	0x09	/* Frame buffer height		*/
#define HPFB_FBHLSB	0x0b
#define HPFB_DWMSB	0x0d	/* Display width		*/
#define HPFB_DWLSB	0x0f
#define HPFB_DHMSB	0x11	/* Display height		*/
#define HPFB_DHLSB	0x13
#define HPFB_NUMPLANES	0x5b	/* Number of colour planes	*/
#define HPFB_FBOMSB	0x5d	/* Frame buffer offset		*/
#define HPFB_FBOLSB	0x5f

static int __devinit hpfb_init_one(unsigned long phys_base,
				   unsigned long virt_base)
{
	unsigned long fboff, fb_width, fb_height, fb_start;

	fb_regs = virt_base;
	fboff = (in_8(fb_regs + HPFB_FBOMSB) << 8) | in_8(fb_regs + HPFB_FBOLSB);

	fb_info.fix.smem_start = (in_8(fb_regs + fboff) << 16);

	if (phys_base >= DIOII_BASE) {
		fb_info.fix.smem_start += phys_base;
	}

	if (DIO_SECID(fb_regs) != DIO_ID2_TOPCAT) {
		/* This is the magic incantation the HP X server uses to make Catseye boards work. */
		while (in_be16(fb_regs+0x4800) & 1)
			;
		out_be16(fb_regs+0x4800, 0);	/* Catseye status */
		out_be16(fb_regs+0x4510, 0);	/* VB */
		out_be16(fb_regs+0x4512, 0);	/* TCNTRL */
		out_be16(fb_regs+0x4514, 0);	/* ACNTRL */
		out_be16(fb_regs+0x4516, 0);	/* PNCNTRL */
		out_be16(fb_regs+0x4206, 0x90);	/* RUG Command/Status */
		out_be16(fb_regs+0x60a2, 0);	/* Overlay Mask */
		out_be16(fb_regs+0x60bc, 0);	/* Ram Select */
	}

	/*
	 *	Fill in the available video resolution
	 */
	fb_width = (in_8(fb_regs + HPFB_FBWMSB) << 8) | in_8(fb_regs + HPFB_FBWLSB);
	fb_info.fix.line_length = fb_width;
	fb_height = (in_8(fb_regs + HPFB_FBHMSB) << 8) | in_8(fb_regs + HPFB_FBHLSB);
	fb_info.fix.smem_len = fb_width * fb_height;
	fb_start = (unsigned long)ioremap_writethrough(fb_info.fix.smem_start,
						       fb_info.fix.smem_len);
	hpfb_defined.xres = (in_8(fb_regs + HPFB_DWMSB) << 8) | in_8(fb_regs + HPFB_DWLSB);
	hpfb_defined.yres = (in_8(fb_regs + HPFB_DHMSB) << 8) | in_8(fb_regs + HPFB_DHLSB);
	hpfb_defined.xres_virtual = hpfb_defined.xres;
	hpfb_defined.yres_virtual = hpfb_defined.yres;
	hpfb_defined.bits_per_pixel = in_8(fb_regs + HPFB_NUMPLANES);

	printk(KERN_INFO "hpfb: framebuffer at 0x%lx, mapped to 0x%lx, size %dk\n",
	       fb_info.fix.smem_start, fb_start, fb_info.fix.smem_len/1024);
	printk(KERN_INFO "hpfb: mode is %dx%dx%d, linelength=%d\n",
	       hpfb_defined.xres, hpfb_defined.yres, hpfb_defined.bits_per_pixel, fb_info.fix.line_length);

	/*
	 *	Give the hardware a bit of a prod and work out how many bits per
	 *	pixel are supported.
	 */
	out_8(fb_regs + TC_WEN, 0xff);
	out_8(fb_regs + TC_PRR, RR_COPY);
	out_8(fb_regs + TC_FBEN, 0xff);
	out_8(fb_start, 0xff);
	fb_bitmask = in_8(fb_start);
	out_8(fb_start, 0);

	/*
	 *	Enable reading/writing of all the planes.
	 */
	out_8(fb_regs + TC_WEN, fb_bitmask);
	out_8(fb_regs + TC_PRR, RR_COPY);
	out_8(fb_regs + TC_REN, fb_bitmask);
	out_8(fb_regs + TC_FBEN, fb_bitmask);

	/*
	 *	Clear the screen.
	 */
	topcat_blit(0, 0, 0, 0, fb_width, fb_height, RR_CLEAR);

	/*
	 *	Let there be consoles..
	 */
	if (DIO_SECID(fb_regs) == DIO_ID2_TOPCAT)
		strcat(fb_info.fix.id, "Topcat");
	else
		strcat(fb_info.fix.id, "Catseye");
	fb_info.fbops = &hpfb_ops;
	fb_info.flags = FBINFO_DEFAULT;
	fb_info.var   = hpfb_defined;
	fb_info.screen_base = (char *)fb_start;

	fb_alloc_cmap(&fb_info.cmap, 1 << hpfb_defined.bits_per_pixel, 0);

	if (register_framebuffer(&fb_info) < 0) {
		fb_dealloc_cmap(&fb_info.cmap);
		iounmap(fb_info.screen_base);
		fb_info.screen_base = NULL;
		return 1;
	}

	printk(KERN_INFO "fb%d: %s frame buffer device\n",
	       fb_info.node, fb_info.fix.id);

	return 0;
}

/* 
 * Check that the secondary ID indicates that we have some hope of working with this
 * framebuffer.  The catseye boards are pretty much like topcats and we can muddle through.
 */

#define topcat_sid_ok(x)  (((x) == DIO_ID2_LRCATSEYE) || ((x) == DIO_ID2_HRCCATSEYE)    \
			   || ((x) == DIO_ID2_HRMCATSEYE) || ((x) == DIO_ID2_TOPCAT))

/* 
 * Initialise the framebuffer
 */
static int __devinit hpfb_dio_probe(struct dio_dev * d, const struct dio_device_id * ent)
{
	unsigned long paddr, vaddr;

	paddr = d->resource.start;
	if (!request_mem_region(d->resource.start, resource_size(&d->resource), d->name))
                return -EBUSY;

	if (d->scode >= DIOII_SCBASE) {
		vaddr = (unsigned long)ioremap(paddr, resource_size(&d->resource));
	} else {
		vaddr = paddr + DIO_VIRADDRBASE;
	}
	printk(KERN_INFO "Topcat found at DIO select code %d "
	       "(secondary id %02x)\n", d->scode, (d->id >> 8) & 0xff);
	if (hpfb_init_one(paddr, vaddr)) {
		if (d->scode >= DIOII_SCBASE)
			iounmap((void *)vaddr);
		return -ENOMEM;
	}
	return 0;
}

static void __devexit hpfb_remove_one(struct dio_dev *d)
{
	unregister_framebuffer(&fb_info);
	if (d->scode >= DIOII_SCBASE)
		iounmap((void *)fb_regs);
	release_mem_region(d->resource.start, resource_size(&d->resource));
}

static struct dio_device_id hpfb_dio_tbl[] = {
    { DIO_ENCODE_ID(DIO_ID_FBUFFER, DIO_ID2_LRCATSEYE) },
    { DIO_ENCODE_ID(DIO_ID_FBUFFER, DIO_ID2_HRCCATSEYE) },
    { DIO_ENCODE_ID(DIO_ID_FBUFFER, DIO_ID2_HRMCATSEYE) },
    { DIO_ENCODE_ID(DIO_ID_FBUFFER, DIO_ID2_TOPCAT) },
    { 0 }
};

static struct dio_driver hpfb_driver = {
    .name      = "hpfb",
    .id_table  = hpfb_dio_tbl,
    .probe     = hpfb_dio_probe,
    .remove    = __devexit_p(hpfb_remove_one),
};

int __init hpfb_init(void)
{
	unsigned int sid;
	mm_segment_t fs;
	unsigned char i;
	int err;

	/* Topcats can be on the internal IO bus or real DIO devices.
	 * The internal variant sits at 0x560000; it has primary
	 * and secondary ID registers just like the DIO version.
	 * So we merge the two detection routines.
	 *
	 * Perhaps this #define should be in a global header file:
	 * I believe it's common to all internal fbs, not just topcat.
	 */
#define INTFBVADDR 0xf0560000
#define INTFBPADDR 0x560000

	if (!MACH_IS_HP300)
		return -ENODEV;

	if (fb_get_options("hpfb", NULL))
		return -ENODEV;

	err = dio_register_driver(&hpfb_driver);
	if (err)
		return err;

	fs = get_fs();
	set_fs(KERNEL_DS);
	err = get_user(i, (unsigned char *)INTFBVADDR + DIO_IDOFF);
	set_fs(fs);

	if (!err && (i == DIO_ID_FBUFFER) && topcat_sid_ok(sid = DIO_SECID(INTFBVADDR))) {
		if (!request_mem_region(INTFBPADDR, DIO_DEVSIZE, "Internal Topcat"))
			return -EBUSY;
		printk(KERN_INFO "Internal Topcat found (secondary id %02x)\n", sid);
		if (hpfb_init_one(INTFBPADDR, INTFBVADDR)) {
			return -ENOMEM;
		}
	}
	return 0;
}

void __exit hpfb_cleanup_module(void)
{
	dio_unregister_driver(&hpfb_driver);
}

module_init(hpfb_init);
module_exit(hpfb_cleanup_module);

MODULE_LICENSE("GPL");
