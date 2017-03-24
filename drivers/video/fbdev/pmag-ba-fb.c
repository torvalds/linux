/*
 *	linux/drivers/video/pmag-ba-fb.c
 *
 *	PMAG-BA TURBOchannel Color Frame Buffer (CFB) card support,
 *	derived from:
 *	"HP300 Topcat framebuffer support (derived from macfb of all things)
 *	Phil Blundell <philb@gnu.org> 1998", the original code can be
 *	found in the file hpfb.c in the same directory.
 *
 *	Based on digital document:
 * 	"PMAG-BA TURBOchannel Color Frame Buffer
 *	 Functional Specification", Revision 1.2, August 27, 1990
 *
 *	DECstation related code Copyright (C) 1999, 2000, 2001 by
 *	Michael Engel <engel@unix-ag.org>,
 *	Karsten Merker <merker@linuxtag.org> and
 *	Harald Koerfgen.
 *	Copyright (c) 2005, 2006  Maciej W. Rozycki
 *	Copyright (c) 2005  James Simmons
 *
 *	This file is subject to the terms and conditions of the GNU General
 *	Public License.  See the file COPYING in the main directory of this
 *	archive for more details.
 */

#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/tc.h>
#include <linux/types.h>

#include <asm/io.h>

#include <video/pmag-ba-fb.h>


struct pmagbafb_par {
	volatile void __iomem *mmio;
	volatile u32 __iomem *dac;
};


static struct fb_var_screeninfo pmagbafb_defined = {
	.xres		= 1024,
	.yres		= 864,
	.xres_virtual	= 1024,
	.yres_virtual	= 864,
	.bits_per_pixel	= 8,
	.red.length	= 8,
	.green.length	= 8,
	.blue.length	= 8,
	.activate	= FB_ACTIVATE_NOW,
	.height		= -1,
	.width		= -1,
	.accel_flags	= FB_ACCEL_NONE,
	.pixclock	= 14452,
	.left_margin	= 116,
	.right_margin	= 12,
	.upper_margin	= 34,
	.lower_margin	= 0,
	.hsync_len	= 128,
	.vsync_len	= 3,
	.sync		= FB_SYNC_ON_GREEN,
	.vmode		= FB_VMODE_NONINTERLACED,
};

static struct fb_fix_screeninfo pmagbafb_fix = {
	.id		= "PMAG-BA",
	.smem_len	= (1024 * 1024),
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_PSEUDOCOLOR,
	.line_length	= 1024,
	.mmio_len	= PMAG_BA_SIZE - PMAG_BA_BT459,
};


static inline void dac_write(struct pmagbafb_par *par, unsigned int reg, u8 v)
{
	writeb(v, par->dac + reg / 4);
}

static inline u8 dac_read(struct pmagbafb_par *par, unsigned int reg)
{
	return readb(par->dac + reg / 4);
}


/*
 * Set the palette.
 */
static int pmagbafb_setcolreg(unsigned int regno, unsigned int red,
			      unsigned int green, unsigned int blue,
			      unsigned int transp, struct fb_info *info)
{
	struct pmagbafb_par *par = info->par;

	if (regno >= info->cmap.len)
		return 1;

	red   >>= 8;	/* The cmap fields are 16 bits    */
	green >>= 8;	/* wide, but the hardware colormap */
	blue  >>= 8;	/* registers are only 8 bits wide */

	mb();
	dac_write(par, BT459_ADDR_LO, regno);
	dac_write(par, BT459_ADDR_HI, 0x00);
	wmb();
	dac_write(par, BT459_CMAP, red);
	wmb();
	dac_write(par, BT459_CMAP, green);
	wmb();
	dac_write(par, BT459_CMAP, blue);

	return 0;
}

static struct fb_ops pmagbafb_ops = {
	.owner		= THIS_MODULE,
	.fb_setcolreg	= pmagbafb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};


/*
 * Turn the hardware cursor off.
 */
static void pmagbafb_erase_cursor(struct fb_info *info)
{
	struct pmagbafb_par *par = info->par;

	mb();
	dac_write(par, BT459_ADDR_LO, 0x00);
	dac_write(par, BT459_ADDR_HI, 0x03);
	wmb();
	dac_write(par, BT459_DATA, 0x00);
}


static int pmagbafb_probe(struct device *dev)
{
	struct tc_dev *tdev = to_tc_dev(dev);
	resource_size_t start, len;
	struct fb_info *info;
	struct pmagbafb_par *par;
	int err;

	info = framebuffer_alloc(sizeof(struct pmagbafb_par), dev);
	if (!info) {
		printk(KERN_ERR "%s: Cannot allocate memory\n", dev_name(dev));
		return -ENOMEM;
	}

	par = info->par;
	dev_set_drvdata(dev, info);

	if (fb_alloc_cmap(&info->cmap, 256, 0) < 0) {
		printk(KERN_ERR "%s: Cannot allocate color map\n",
		       dev_name(dev));
		err = -ENOMEM;
		goto err_alloc;
	}

	info->fbops = &pmagbafb_ops;
	info->fix = pmagbafb_fix;
	info->var = pmagbafb_defined;
	info->flags = FBINFO_DEFAULT;

	/* Request the I/O MEM resource.  */
	start = tdev->resource.start;
	len = tdev->resource.end - start + 1;
	if (!request_mem_region(start, len, dev_name(dev))) {
		printk(KERN_ERR "%s: Cannot reserve FB region\n",
		       dev_name(dev));
		err = -EBUSY;
		goto err_cmap;
	}

	/* MMIO mapping setup.  */
	info->fix.mmio_start = start;
	par->mmio = ioremap_nocache(info->fix.mmio_start, info->fix.mmio_len);
	if (!par->mmio) {
		printk(KERN_ERR "%s: Cannot map MMIO\n", dev_name(dev));
		err = -ENOMEM;
		goto err_resource;
	}
	par->dac = par->mmio + PMAG_BA_BT459;

	/* Frame buffer mapping setup.  */
	info->fix.smem_start = start + PMAG_BA_FBMEM;
	info->screen_base = ioremap_nocache(info->fix.smem_start,
					    info->fix.smem_len);
	if (!info->screen_base) {
		printk(KERN_ERR "%s: Cannot map FB\n", dev_name(dev));
		err = -ENOMEM;
		goto err_mmio_map;
	}
	info->screen_size = info->fix.smem_len;

	pmagbafb_erase_cursor(info);

	err = register_framebuffer(info);
	if (err < 0) {
		printk(KERN_ERR "%s: Cannot register framebuffer\n",
		       dev_name(dev));
		goto err_smem_map;
	}

	get_device(dev);

	fb_info(info, "%s frame buffer device at %s\n",
		info->fix.id, dev_name(dev));

	return 0;


err_smem_map:
	iounmap(info->screen_base);

err_mmio_map:
	iounmap(par->mmio);

err_resource:
	release_mem_region(start, len);

err_cmap:
	fb_dealloc_cmap(&info->cmap);

err_alloc:
	framebuffer_release(info);
	return err;
}

static int __exit pmagbafb_remove(struct device *dev)
{
	struct tc_dev *tdev = to_tc_dev(dev);
	struct fb_info *info = dev_get_drvdata(dev);
	struct pmagbafb_par *par = info->par;
	resource_size_t start, len;

	put_device(dev);
	unregister_framebuffer(info);
	iounmap(info->screen_base);
	iounmap(par->mmio);
	start = tdev->resource.start;
	len = tdev->resource.end - start + 1;
	release_mem_region(start, len);
	fb_dealloc_cmap(&info->cmap);
	framebuffer_release(info);
	return 0;
}


/*
 * Initialize the framebuffer.
 */
static const struct tc_device_id pmagbafb_tc_table[] = {
	{ "DEC     ", "PMAG-BA " },
	{ }
};
MODULE_DEVICE_TABLE(tc, pmagbafb_tc_table);

static struct tc_driver pmagbafb_driver = {
	.id_table	= pmagbafb_tc_table,
	.driver		= {
		.name	= "pmagbafb",
		.bus	= &tc_bus_type,
		.probe	= pmagbafb_probe,
		.remove	= __exit_p(pmagbafb_remove),
	},
};

static int __init pmagbafb_init(void)
{
#ifndef MODULE
	if (fb_get_options("pmagbafb", NULL))
		return -ENXIO;
#endif
	return tc_register_driver(&pmagbafb_driver);
}

static void __exit pmagbafb_exit(void)
{
	tc_unregister_driver(&pmagbafb_driver);
}


module_init(pmagbafb_init);
module_exit(pmagbafb_exit);

MODULE_LICENSE("GPL");
