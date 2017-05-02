/*
 *	linux/drivers/video/pmagb-b-fb.c
 *
 *	PMAGB-B TURBOchannel Smart Frame Buffer (SFB) card support,
 *	derived from:
 *	"HP300 Topcat framebuffer support (derived from macfb of all things)
 *	Phil Blundell <philb@gnu.org> 1998", the original code can be
 *	found in the file hpfb.c in the same directory.
 *
 *	DECstation related code Copyright (C) 1999, 2000, 2001 by
 *	Michael Engel <engel@unix-ag.org>,
 *	Karsten Merker <merker@linuxtag.org> and
 *	Harald Koerfgen.
 *	Copyright (c) 2005, 2006  Maciej W. Rozycki
 *
 *	This file is subject to the terms and conditions of the GNU General
 *	Public License.  See the file COPYING in the main directory of this
 *	archive for more details.
 */

#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/tc.h>
#include <linux/types.h>

#include <asm/io.h>

#include <video/pmagb-b-fb.h>


struct pmagbbfb_par {
	volatile void __iomem *mmio;
	volatile void __iomem *smem;
	volatile u32 __iomem *sfb;
	volatile u32 __iomem *dac;
	unsigned int osc0;
	unsigned int osc1;
	int slot;
};


static struct fb_var_screeninfo pmagbbfb_defined = {
	.bits_per_pixel	= 8,
	.red.length	= 8,
	.green.length	= 8,
	.blue.length	= 8,
	.activate	= FB_ACTIVATE_NOW,
	.height		= -1,
	.width		= -1,
	.accel_flags	= FB_ACCEL_NONE,
	.sync		= FB_SYNC_ON_GREEN,
	.vmode		= FB_VMODE_NONINTERLACED,
};

static struct fb_fix_screeninfo pmagbbfb_fix = {
	.id		= "PMAGB-BA",
	.smem_len	= (2048 * 1024),
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_PSEUDOCOLOR,
	.mmio_len	= PMAGB_B_FBMEM,
};


static inline void sfb_write(struct pmagbbfb_par *par, unsigned int reg, u32 v)
{
	writel(v, par->sfb + reg / 4);
}

static inline u32 sfb_read(struct pmagbbfb_par *par, unsigned int reg)
{
	return readl(par->sfb + reg / 4);
}

static inline void dac_write(struct pmagbbfb_par *par, unsigned int reg, u8 v)
{
	writeb(v, par->dac + reg / 4);
}

static inline u8 dac_read(struct pmagbbfb_par *par, unsigned int reg)
{
	return readb(par->dac + reg / 4);
}

static inline void gp0_write(struct pmagbbfb_par *par, u32 v)
{
	writel(v, par->mmio + PMAGB_B_GP0);
}


/*
 * Set the palette.
 */
static int pmagbbfb_setcolreg(unsigned int regno, unsigned int red,
			      unsigned int green, unsigned int blue,
			      unsigned int transp, struct fb_info *info)
{
	struct pmagbbfb_par *par = info->par;

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

static struct fb_ops pmagbbfb_ops = {
	.owner		= THIS_MODULE,
	.fb_setcolreg	= pmagbbfb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};


/*
 * Turn the hardware cursor off.
 */
static void pmagbbfb_erase_cursor(struct fb_info *info)
{
	struct pmagbbfb_par *par = info->par;

	mb();
	dac_write(par, BT459_ADDR_LO, 0x00);
	dac_write(par, BT459_ADDR_HI, 0x03);
	wmb();
	dac_write(par, BT459_DATA, 0x00);
}

/*
 * Set up screen parameters.
 */
static void pmagbbfb_screen_setup(struct fb_info *info)
{
	struct pmagbbfb_par *par = info->par;

	info->var.xres = ((sfb_read(par, SFB_REG_VID_HOR) >>
			   SFB_VID_HOR_PIX_SHIFT) & SFB_VID_HOR_PIX_MASK) * 4;
	info->var.xres_virtual = info->var.xres;
	info->var.yres = (sfb_read(par, SFB_REG_VID_VER) >>
			  SFB_VID_VER_SL_SHIFT) & SFB_VID_VER_SL_MASK;
	info->var.yres_virtual = info->var.yres;
	info->var.left_margin = ((sfb_read(par, SFB_REG_VID_HOR) >>
				  SFB_VID_HOR_BP_SHIFT) &
				 SFB_VID_HOR_BP_MASK) * 4;
	info->var.right_margin = ((sfb_read(par, SFB_REG_VID_HOR) >>
				   SFB_VID_HOR_FP_SHIFT) &
				  SFB_VID_HOR_FP_MASK) * 4;
	info->var.upper_margin = (sfb_read(par, SFB_REG_VID_VER) >>
				  SFB_VID_VER_BP_SHIFT) & SFB_VID_VER_BP_MASK;
	info->var.lower_margin = (sfb_read(par, SFB_REG_VID_VER) >>
				  SFB_VID_VER_FP_SHIFT) & SFB_VID_VER_FP_MASK;
	info->var.hsync_len = ((sfb_read(par, SFB_REG_VID_HOR) >>
				SFB_VID_HOR_SYN_SHIFT) &
			       SFB_VID_HOR_SYN_MASK) * 4;
	info->var.vsync_len = (sfb_read(par, SFB_REG_VID_VER) >>
			       SFB_VID_VER_SYN_SHIFT) & SFB_VID_VER_SYN_MASK;

	info->fix.line_length = info->var.xres;
};

/*
 * Determine oscillator configuration.
 */
static void pmagbbfb_osc_setup(struct fb_info *info)
{
	static unsigned int pmagbbfb_freqs[] = {
		130808, 119843, 104000, 92980, 74370, 72800,
		69197, 66000, 65000, 50350, 36000, 32000, 25175
	};
	struct pmagbbfb_par *par = info->par;
	struct tc_bus *tbus = to_tc_dev(info->device)->bus;
	u32 count0 = 8, count1 = 8, counttc = 16 * 256 + 8;
	u32 freq0, freq1, freqtc = tc_get_speed(tbus) / 250;
	int i, j;

	gp0_write(par, 0);				/* select Osc0 */
	for (j = 0; j < 16; j++) {
		mb();
		sfb_write(par, SFB_REG_TCCLK_COUNT, 0);
		mb();
		for (i = 0; i < 100; i++) {	/* nominally max. 20.5us */
			if (sfb_read(par, SFB_REG_TCCLK_COUNT) == 0)
				break;
			udelay(1);
		}
		count0 += sfb_read(par, SFB_REG_VIDCLK_COUNT);
	}

	gp0_write(par, 1);				/* select Osc1 */
	for (j = 0; j < 16; j++) {
		mb();
		sfb_write(par, SFB_REG_TCCLK_COUNT, 0);

		for (i = 0; i < 100; i++) {	/* nominally max. 20.5us */
			if (sfb_read(par, SFB_REG_TCCLK_COUNT) == 0)
				break;
			udelay(1);
		}
		count1 += sfb_read(par, SFB_REG_VIDCLK_COUNT);
	}

	freq0 = (freqtc * count0 + counttc / 2) / counttc;
	par->osc0 = freq0;
	if (freq0 >= pmagbbfb_freqs[0] - (pmagbbfb_freqs[0] + 32) / 64 &&
	    freq0 <= pmagbbfb_freqs[0] + (pmagbbfb_freqs[0] + 32) / 64)
		par->osc0 = pmagbbfb_freqs[0];

	freq1 = (par->osc0 * count1 + count0 / 2) / count0;
	par->osc1 = freq1;
	for (i = 0; i < ARRAY_SIZE(pmagbbfb_freqs); i++)
		if (freq1 >= pmagbbfb_freqs[i] -
			     (pmagbbfb_freqs[i] + 128) / 256 &&
		    freq1 <= pmagbbfb_freqs[i] +
			     (pmagbbfb_freqs[i] + 128) / 256) {
			par->osc1 = pmagbbfb_freqs[i];
			break;
		}

	if (par->osc0 - par->osc1 <= (par->osc0 + par->osc1 + 256) / 512 ||
	    par->osc1 - par->osc0 <= (par->osc0 + par->osc1 + 256) / 512)
		par->osc1 = 0;

	gp0_write(par, par->osc1 != 0);			/* reselect OscX */

	info->var.pixclock = par->osc1 ?
			     (1000000000 + par->osc1 / 2) / par->osc1 :
			     (1000000000 + par->osc0 / 2) / par->osc0;
};


static int pmagbbfb_probe(struct device *dev)
{
	struct tc_dev *tdev = to_tc_dev(dev);
	resource_size_t start, len;
	struct fb_info *info;
	struct pmagbbfb_par *par;
	char freq0[12], freq1[12];
	u32 vid_base;
	int err;

	info = framebuffer_alloc(sizeof(struct pmagbbfb_par), dev);
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

	info->fbops = &pmagbbfb_ops;
	info->fix = pmagbbfb_fix;
	info->var = pmagbbfb_defined;
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
	par->sfb = par->mmio + PMAGB_B_SFB;
	par->dac = par->mmio + PMAGB_B_BT459;

	/* Frame buffer mapping setup.  */
	info->fix.smem_start = start + PMAGB_B_FBMEM;
	par->smem = ioremap_nocache(info->fix.smem_start, info->fix.smem_len);
	if (!par->smem) {
		printk(KERN_ERR "%s: Cannot map FB\n", dev_name(dev));
		err = -ENOMEM;
		goto err_mmio_map;
	}
	vid_base = sfb_read(par, SFB_REG_VID_BASE);
	info->screen_base = (void __iomem *)par->smem + vid_base * 0x1000;
	info->screen_size = info->fix.smem_len - 2 * vid_base * 0x1000;

	pmagbbfb_erase_cursor(info);
	pmagbbfb_screen_setup(info);
	pmagbbfb_osc_setup(info);

	err = register_framebuffer(info);
	if (err < 0) {
		printk(KERN_ERR "%s: Cannot register framebuffer\n",
		       dev_name(dev));
		goto err_smem_map;
	}

	get_device(dev);

	snprintf(freq0, sizeof(freq0), "%u.%03uMHz",
		 par->osc0 / 1000, par->osc0 % 1000);
	snprintf(freq1, sizeof(freq1), "%u.%03uMHz",
		 par->osc1 / 1000, par->osc1 % 1000);

	fb_info(info, "%s frame buffer device at %s\n",
		info->fix.id, dev_name(dev));
	fb_info(info, "Osc0: %s, Osc1: %s, Osc%u selected\n",
		freq0, par->osc1 ? freq1 : "disabled", par->osc1 != 0);

	return 0;


err_smem_map:
	iounmap(par->smem);

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

static int __exit pmagbbfb_remove(struct device *dev)
{
	struct tc_dev *tdev = to_tc_dev(dev);
	struct fb_info *info = dev_get_drvdata(dev);
	struct pmagbbfb_par *par = info->par;
	resource_size_t start, len;

	put_device(dev);
	unregister_framebuffer(info);
	iounmap(par->smem);
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
static const struct tc_device_id pmagbbfb_tc_table[] = {
	{ "DEC     ", "PMAGB-BA" },
	{ }
};
MODULE_DEVICE_TABLE(tc, pmagbbfb_tc_table);

static struct tc_driver pmagbbfb_driver = {
	.id_table	= pmagbbfb_tc_table,
	.driver		= {
		.name	= "pmagbbfb",
		.bus	= &tc_bus_type,
		.probe	= pmagbbfb_probe,
		.remove	= __exit_p(pmagbbfb_remove),
	},
};

static int __init pmagbbfb_init(void)
{
#ifndef MODULE
	if (fb_get_options("pmagbbfb", NULL))
		return -ENXIO;
#endif
	return tc_register_driver(&pmagbbfb_driver);
}

static void __exit pmagbbfb_exit(void)
{
	tc_unregister_driver(&pmagbbfb_driver);
}


module_init(pmagbbfb_init);
module_exit(pmagbbfb_exit);

MODULE_LICENSE("GPL");
