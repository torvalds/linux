/*
 * Xilinx TFT frame buffer driver
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * 2002-2007 (c) MontaVista Software, Inc.
 * 2007 (c) Secret Lab Technologies, Ltd.
 * 2009 (c) Xilinx Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

/*
 * This driver was based on au1100fb.c by MontaVista rewritten for 2.6
 * by Embedded Alley Solutions <source@embeddedalley.com>, which in turn
 * was based on skeletonfb.c, Skeleton for a frame buffer device by
 * Geert Uytterhoeven.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/xilinxfb.h>
#include <asm/dcr.h>

#define DRIVER_NAME		"xilinxfb"


/*
 * Xilinx calls it "PLB TFT LCD Controller" though it can also be used for
 * the VGA port on the Xilinx ML40x board. This is a hardware display
 * controller for a 640x480 resolution TFT or VGA screen.
 *
 * The interface to the framebuffer is nice and simple.  There are two
 * control registers.  The first tells the LCD interface where in memory
 * the frame buffer is (only the 11 most significant bits are used, so
 * don't start thinking about scrolling).  The second allows the LCD to
 * be turned on or off as well as rotated 180 degrees.
 *
 * In case of direct PLB access the second control register will be at
 * an offset of 4 as compared to the DCR access where the offset is 1
 * i.e. REG_CTRL. So this is taken care in the function
 * xilinx_fb_out_be32 where it left shifts the offset 2 times in case of
 * direct PLB access.
 */
#define NUM_REGS	2
#define REG_FB_ADDR	0
#define REG_CTRL	1
#define REG_CTRL_ENABLE	 0x0001
#define REG_CTRL_ROTATE	 0x0002

/*
 * The hardware only handles a single mode: 640x480 24 bit true
 * color. Each pixel gets a word (32 bits) of memory.  Within each word,
 * the 8 most significant bits are ignored, the next 8 bits are the red
 * level, the next 8 bits are the green level and the 8 least
 * significant bits are the blue level.  Each row of the LCD uses 1024
 * words, but only the first 640 pixels are displayed with the other 384
 * words being ignored.  There are 480 rows.
 */
#define BYTES_PER_PIXEL	4
#define BITS_PER_PIXEL	(BYTES_PER_PIXEL * 8)

#define RED_SHIFT	16
#define GREEN_SHIFT	8
#define BLUE_SHIFT	0

#define PALETTE_ENTRIES_NO	16	/* passed to fb_alloc_cmap() */

/*
 * Default xilinxfb configuration
 */
static struct xilinxfb_platform_data xilinx_fb_default_pdata = {
	.xres = 640,
	.yres = 480,
	.xvirt = 1024,
	.yvirt = 480,
};

/*
 * Here are the default fb_fix_screeninfo and fb_var_screeninfo structures
 */
static struct fb_fix_screeninfo xilinx_fb_fix = {
	.id =		"Xilinx",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_TRUECOLOR,
	.accel =	FB_ACCEL_NONE
};

static struct fb_var_screeninfo xilinx_fb_var = {
	.bits_per_pixel =	BITS_PER_PIXEL,

	.red =		{ RED_SHIFT, 8, 0 },
	.green =	{ GREEN_SHIFT, 8, 0 },
	.blue =		{ BLUE_SHIFT, 8, 0 },
	.transp =	{ 0, 0, 0 },

	.activate =	FB_ACTIVATE_NOW
};


#define PLB_ACCESS_FLAG	0x1		/* 1 = PLB, 0 = DCR */

struct xilinxfb_drvdata {

	struct fb_info	info;		/* FB driver info record */

	phys_addr_t	regs_phys;	/* phys. address of the control
						registers */
	void __iomem	*regs;		/* virt. address of the control
						registers */

	dcr_host_t      dcr_host;
	unsigned int    dcr_start;
	unsigned int    dcr_len;

	void		*fb_virt;	/* virt. address of the frame buffer */
	dma_addr_t	fb_phys;	/* phys. address of the frame buffer */
	int		fb_alloced;	/* Flag, was the fb memory alloced? */

	u8 		flags;		/* features of the driver */

	u32		reg_ctrl_default;

	u32		pseudo_palette[PALETTE_ENTRIES_NO];
					/* Fake palette of 16 colors */
};

#define to_xilinxfb_drvdata(_info) \
	container_of(_info, struct xilinxfb_drvdata, info)

/*
 * The XPS TFT Controller can be accessed through PLB or DCR interface.
 * To perform the read/write on the registers we need to check on
 * which bus its connected and call the appropriate write API.
 */
static void xilinx_fb_out_be32(struct xilinxfb_drvdata *drvdata, u32 offset,
				u32 val)
{
	if (drvdata->flags & PLB_ACCESS_FLAG)
		out_be32(drvdata->regs + (offset << 2), val);
	else
		dcr_write(drvdata->dcr_host, offset, val);

}

static int
xilinx_fb_setcolreg(unsigned regno, unsigned red, unsigned green, unsigned blue,
	unsigned transp, struct fb_info *fbi)
{
	u32 *palette = fbi->pseudo_palette;

	if (regno >= PALETTE_ENTRIES_NO)
		return -EINVAL;

	if (fbi->var.grayscale) {
		/* Convert color to grayscale.
		 * grayscale = 0.30*R + 0.59*G + 0.11*B */
		red = green = blue =
			(red * 77 + green * 151 + blue * 28 + 127) >> 8;
	}

	/* fbi->fix.visual is always FB_VISUAL_TRUECOLOR */

	/* We only handle 8 bits of each color. */
	red >>= 8;
	green >>= 8;
	blue >>= 8;
	palette[regno] = (red << RED_SHIFT) | (green << GREEN_SHIFT) |
			 (blue << BLUE_SHIFT);

	return 0;
}

static int
xilinx_fb_blank(int blank_mode, struct fb_info *fbi)
{
	struct xilinxfb_drvdata *drvdata = to_xilinxfb_drvdata(fbi);

	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		/* turn on panel */
		xilinx_fb_out_be32(drvdata, REG_CTRL, drvdata->reg_ctrl_default);
		break;

	case FB_BLANK_NORMAL:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_POWERDOWN:
		/* turn off panel */
		xilinx_fb_out_be32(drvdata, REG_CTRL, 0);
	default:
		break;

	}
	return 0; /* success */
}

static struct fb_ops xilinxfb_ops =
{
	.owner			= THIS_MODULE,
	.fb_setcolreg		= xilinx_fb_setcolreg,
	.fb_blank		= xilinx_fb_blank,
	.fb_fillrect		= cfb_fillrect,
	.fb_copyarea		= cfb_copyarea,
	.fb_imageblit		= cfb_imageblit,
};

/* ---------------------------------------------------------------------
 * Bus independent setup/teardown
 */

static int xilinxfb_assign(struct device *dev,
			   struct xilinxfb_drvdata *drvdata,
			   unsigned long physaddr,
			   struct xilinxfb_platform_data *pdata)
{
	int rc;
	int fbsize = pdata->xvirt * pdata->yvirt * BYTES_PER_PIXEL;

	if (drvdata->flags & PLB_ACCESS_FLAG) {
		/*
		 * Map the control registers in if the controller
		 * is on direct PLB interface.
		 */
		if (!request_mem_region(physaddr, 8, DRIVER_NAME)) {
			dev_err(dev, "Couldn't lock memory region at 0x%08lX\n",
				physaddr);
			rc = -ENODEV;
			goto err_region;
		}

		drvdata->regs_phys = physaddr;
		drvdata->regs = ioremap(physaddr, 8);
		if (!drvdata->regs) {
			dev_err(dev, "Couldn't lock memory region at 0x%08lX\n",
				physaddr);
			rc = -ENODEV;
			goto err_map;
		}
	}

	/* Allocate the framebuffer memory */
	if (pdata->fb_phys) {
		drvdata->fb_phys = pdata->fb_phys;
		drvdata->fb_virt = ioremap(pdata->fb_phys, fbsize);
	} else {
		drvdata->fb_alloced = 1;
		drvdata->fb_virt = dma_alloc_coherent(dev, PAGE_ALIGN(fbsize),
					&drvdata->fb_phys, GFP_KERNEL);
	}

	if (!drvdata->fb_virt) {
		dev_err(dev, "Could not allocate frame buffer memory\n");
		rc = -ENOMEM;
		if (drvdata->flags & PLB_ACCESS_FLAG)
			goto err_fbmem;
		else
			goto err_region;
	}

	/* Clear (turn to black) the framebuffer */
	memset_io((void __iomem *)drvdata->fb_virt, 0, fbsize);

	/* Tell the hardware where the frame buffer is */
	xilinx_fb_out_be32(drvdata, REG_FB_ADDR, drvdata->fb_phys);

	/* Turn on the display */
	drvdata->reg_ctrl_default = REG_CTRL_ENABLE;
	if (pdata->rotate_screen)
		drvdata->reg_ctrl_default |= REG_CTRL_ROTATE;
	xilinx_fb_out_be32(drvdata, REG_CTRL,
					drvdata->reg_ctrl_default);

	/* Fill struct fb_info */
	drvdata->info.device = dev;
	drvdata->info.screen_base = (void __iomem *)drvdata->fb_virt;
	drvdata->info.fbops = &xilinxfb_ops;
	drvdata->info.fix = xilinx_fb_fix;
	drvdata->info.fix.smem_start = drvdata->fb_phys;
	drvdata->info.fix.smem_len = fbsize;
	drvdata->info.fix.line_length = pdata->xvirt * BYTES_PER_PIXEL;

	drvdata->info.pseudo_palette = drvdata->pseudo_palette;
	drvdata->info.flags = FBINFO_DEFAULT;
	drvdata->info.var = xilinx_fb_var;
	drvdata->info.var.height = pdata->screen_height_mm;
	drvdata->info.var.width = pdata->screen_width_mm;
	drvdata->info.var.xres = pdata->xres;
	drvdata->info.var.yres = pdata->yres;
	drvdata->info.var.xres_virtual = pdata->xvirt;
	drvdata->info.var.yres_virtual = pdata->yvirt;

	/* Allocate a colour map */
	rc = fb_alloc_cmap(&drvdata->info.cmap, PALETTE_ENTRIES_NO, 0);
	if (rc) {
		dev_err(dev, "Fail to allocate colormap (%d entries)\n",
			PALETTE_ENTRIES_NO);
		goto err_cmap;
	}

	/* Register new frame buffer */
	rc = register_framebuffer(&drvdata->info);
	if (rc) {
		dev_err(dev, "Could not register frame buffer\n");
		goto err_regfb;
	}

	if (drvdata->flags & PLB_ACCESS_FLAG) {
		/* Put a banner in the log (for DEBUG) */
		dev_dbg(dev, "regs: phys=%lx, virt=%p\n", physaddr,
					drvdata->regs);
	}
	/* Put a banner in the log (for DEBUG) */
	dev_dbg(dev, "fb: phys=%p, virt=%p, size=%x\n",
		(void *)drvdata->fb_phys, drvdata->fb_virt, fbsize);

	return 0;	/* success */

err_regfb:
	fb_dealloc_cmap(&drvdata->info.cmap);

err_cmap:
	if (drvdata->fb_alloced)
		dma_free_coherent(dev, PAGE_ALIGN(fbsize), drvdata->fb_virt,
			drvdata->fb_phys);
	else
		iounmap(drvdata->fb_virt);

	/* Turn off the display */
	xilinx_fb_out_be32(drvdata, REG_CTRL, 0);

err_fbmem:
	if (drvdata->flags & PLB_ACCESS_FLAG)
		iounmap(drvdata->regs);

err_map:
	if (drvdata->flags & PLB_ACCESS_FLAG)
		release_mem_region(physaddr, 8);

err_region:
	kfree(drvdata);
	dev_set_drvdata(dev, NULL);

	return rc;
}

static int xilinxfb_release(struct device *dev)
{
	struct xilinxfb_drvdata *drvdata = dev_get_drvdata(dev);

#if !defined(CONFIG_FRAMEBUFFER_CONSOLE) && defined(CONFIG_LOGO)
	xilinx_fb_blank(VESA_POWERDOWN, &drvdata->info);
#endif

	unregister_framebuffer(&drvdata->info);

	fb_dealloc_cmap(&drvdata->info.cmap);

	if (drvdata->fb_alloced)
		dma_free_coherent(dev, PAGE_ALIGN(drvdata->info.fix.smem_len),
				  drvdata->fb_virt, drvdata->fb_phys);
	else
		iounmap(drvdata->fb_virt);

	/* Turn off the display */
	xilinx_fb_out_be32(drvdata, REG_CTRL, 0);

	/* Release the resources, as allocated based on interface */
	if (drvdata->flags & PLB_ACCESS_FLAG) {
		iounmap(drvdata->regs);
		release_mem_region(drvdata->regs_phys, 8);
	} else
		dcr_unmap(drvdata->dcr_host, drvdata->dcr_len);

	kfree(drvdata);
	dev_set_drvdata(dev, NULL);

	return 0;
}

/* ---------------------------------------------------------------------
 * OF bus binding
 */

static int __devinit
xilinxfb_of_probe(struct of_device *op, const struct of_device_id *match)
{
	const u32 *prop;
	u32 *p;
	u32 tft_access;
	struct xilinxfb_platform_data pdata;
	struct resource res;
	int size, rc;
	int start = 0, len = 0;
	dcr_host_t dcr_host;
	struct xilinxfb_drvdata *drvdata;

	/* Copy with the default pdata (not a ptr reference!) */
	pdata = xilinx_fb_default_pdata;

	dev_dbg(&op->dev, "xilinxfb_of_probe(%p, %p)\n", op, match);

	/*
	 * To check whether the core is connected directly to DCR or PLB
	 * interface and initialize the tft_access accordingly.
	 */
	p = (u32 *)of_get_property(op->node, "xlnx,dcr-splb-slave-if", NULL);

	if (p)
		tft_access = *p;
	else
		tft_access = 0;		/* For backward compatibility */

	/*
	 * Fill the resource structure if its direct PLB interface
	 * otherwise fill the dcr_host structure.
	 */
	if (tft_access) {
		rc = of_address_to_resource(op->node, 0, &res);
		if (rc) {
			dev_err(&op->dev, "invalid address\n");
			return -ENODEV;
		}

	} else {
		start = dcr_resource_start(op->node, 0);
		len = dcr_resource_len(op->node, 0);
		dcr_host = dcr_map(op->node, start, len);
		if (!DCR_MAP_OK(dcr_host)) {
			dev_err(&op->dev, "invalid address\n");
			return -ENODEV;
		}
	}

	prop = of_get_property(op->node, "phys-size", &size);
	if ((prop) && (size >= sizeof(u32)*2)) {
		pdata.screen_width_mm = prop[0];
		pdata.screen_height_mm = prop[1];
	}

	prop = of_get_property(op->node, "resolution", &size);
	if ((prop) && (size >= sizeof(u32)*2)) {
		pdata.xres = prop[0];
		pdata.yres = prop[1];
	}

	prop = of_get_property(op->node, "virtual-resolution", &size);
	if ((prop) && (size >= sizeof(u32)*2)) {
		pdata.xvirt = prop[0];
		pdata.yvirt = prop[1];
	}

	if (of_find_property(op->node, "rotate-display", NULL))
		pdata.rotate_screen = 1;

	/* Allocate the driver data region */
	drvdata = kzalloc(sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata) {
		dev_err(&op->dev, "Couldn't allocate device private record\n");
		return -ENOMEM;
	}
	dev_set_drvdata(&op->dev, drvdata);

	if (tft_access)
		drvdata->flags |= PLB_ACCESS_FLAG;

	/* Arguments are passed based on the interface */
	if (drvdata->flags & PLB_ACCESS_FLAG) {
		return xilinxfb_assign(&op->dev, drvdata, res.start, &pdata);
	} else {
		drvdata->dcr_start = start;
		drvdata->dcr_len = len;
		drvdata->dcr_host = dcr_host;
		return xilinxfb_assign(&op->dev, drvdata, 0, &pdata);
	}
}

static int __devexit xilinxfb_of_remove(struct of_device *op)
{
	return xilinxfb_release(&op->dev);
}

/* Match table for of_platform binding */
static struct of_device_id xilinxfb_of_match[] __devinitdata = {
	{ .compatible = "xlnx,xps-tft-1.00.a", },
	{ .compatible = "xlnx,plb-tft-cntlr-ref-1.00.a", },
	{ .compatible = "xlnx,plb-dvi-cntlr-ref-1.00.c", },
	{},
};
MODULE_DEVICE_TABLE(of, xilinxfb_of_match);

static struct of_platform_driver xilinxfb_of_driver = {
	.owner = THIS_MODULE,
	.name = DRIVER_NAME,
	.match_table = xilinxfb_of_match,
	.probe = xilinxfb_of_probe,
	.remove = __devexit_p(xilinxfb_of_remove),
	.driver = {
		.name = DRIVER_NAME,
	},
};


/* ---------------------------------------------------------------------
 * Module setup and teardown
 */

static int __init
xilinxfb_init(void)
{
	return of_register_platform_driver(&xilinxfb_of_driver);
}

static void __exit
xilinxfb_cleanup(void)
{
	of_unregister_platform_driver(&xilinxfb_of_driver);
}

module_init(xilinxfb_init);
module_exit(xilinxfb_cleanup);

MODULE_AUTHOR("MontaVista Software, Inc. <source@mvista.com>");
MODULE_DESCRIPTION("Xilinx TFT frame buffer driver");
MODULE_LICENSE("GPL");
