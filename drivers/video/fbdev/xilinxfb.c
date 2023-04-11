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
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/slab.h>

#ifdef CONFIG_PPC_DCR
#include <asm/dcr.h>
#endif

#define DRIVER_NAME		"xilinxfb"

/*
 * Xilinx calls it "TFT LCD Controller" though it can also be used for
 * the VGA port on the Xilinx ML40x board. This is a hardware display
 * controller for a 640x480 resolution TFT or VGA screen.
 *
 * The interface to the framebuffer is nice and simple.  There are two
 * control registers.  The first tells the LCD interface where in memory
 * the frame buffer is (only the 11 most significant bits are used, so
 * don't start thinking about scrolling).  The second allows the LCD to
 * be turned on or off as well as rotated 180 degrees.
 *
 * In case of direct BUS access the second control register will be at
 * an offset of 4 as compared to the DCR access where the offset is 1
 * i.e. REG_CTRL. So this is taken care in the function
 * xilinx_fb_out32 where it left shifts the offset 2 times in case of
 * direct BUS access.
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

/* ML300/403 reference design framebuffer driver platform data struct */
struct xilinxfb_platform_data {
	u32 rotate_screen;      /* Flag to rotate display 180 degrees */
	u32 screen_height_mm;   /* Physical dimensions of screen in mm */
	u32 screen_width_mm;
	u32 xres, yres;         /* resolution of screen in pixels */
	u32 xvirt, yvirt;       /* resolution of memory buffer */

	/* Physical address of framebuffer memory; If non-zero, driver
	 * will use provided memory address instead of allocating one from
	 * the consistent pool.
	 */
	u32 fb_phys;
};

/*
 * Default xilinxfb configuration
 */
static const struct xilinxfb_platform_data xilinx_fb_default_pdata = {
	.xres = 640,
	.yres = 480,
	.xvirt = 1024,
	.yvirt = 480,
};

/*
 * Here are the default fb_fix_screeninfo and fb_var_screeninfo structures
 */
static const struct fb_fix_screeninfo xilinx_fb_fix = {
	.id =		"Xilinx",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_TRUECOLOR,
	.accel =	FB_ACCEL_NONE
};

static const struct fb_var_screeninfo xilinx_fb_var = {
	.bits_per_pixel =	BITS_PER_PIXEL,

	.red =		{ RED_SHIFT, 8, 0 },
	.green =	{ GREEN_SHIFT, 8, 0 },
	.blue =		{ BLUE_SHIFT, 8, 0 },
	.transp =	{ 0, 0, 0 },

	.activate =	FB_ACTIVATE_NOW
};

#define BUS_ACCESS_FLAG		0x1 /* 1 = BUS, 0 = DCR */
#define LITTLE_ENDIAN_ACCESS	0x2 /* LITTLE ENDIAN IO functions */

struct xilinxfb_drvdata {
	struct fb_info	info;		/* FB driver info record */

	phys_addr_t	regs_phys;	/* phys. address of the control
					 * registers
					 */
	void __iomem	*regs;		/* virt. address of the control
					 * registers
					 */
#ifdef CONFIG_PPC_DCR
	dcr_host_t      dcr_host;
	unsigned int    dcr_len;
#endif
	void		*fb_virt;	/* virt. address of the frame buffer */
	dma_addr_t	fb_phys;	/* phys. address of the frame buffer */
	int		fb_alloced;	/* Flag, was the fb memory alloced? */

	u8		flags;		/* features of the driver */

	u32		reg_ctrl_default;

	u32		pseudo_palette[PALETTE_ENTRIES_NO];
					/* Fake palette of 16 colors */
};

#define to_xilinxfb_drvdata(_info) \
	container_of(_info, struct xilinxfb_drvdata, info)

/*
 * The XPS TFT Controller can be accessed through BUS or DCR interface.
 * To perform the read/write on the registers we need to check on
 * which bus its connected and call the appropriate write API.
 */
static void xilinx_fb_out32(struct xilinxfb_drvdata *drvdata, u32 offset,
			    u32 val)
{
	if (drvdata->flags & BUS_ACCESS_FLAG) {
		if (drvdata->flags & LITTLE_ENDIAN_ACCESS)
			iowrite32(val, drvdata->regs + (offset << 2));
		else
			iowrite32be(val, drvdata->regs + (offset << 2));
	}
#ifdef CONFIG_PPC_DCR
	else
		dcr_write(drvdata->dcr_host, offset, val);
#endif
}

static u32 xilinx_fb_in32(struct xilinxfb_drvdata *drvdata, u32 offset)
{
	if (drvdata->flags & BUS_ACCESS_FLAG) {
		if (drvdata->flags & LITTLE_ENDIAN_ACCESS)
			return ioread32(drvdata->regs + (offset << 2));
		else
			return ioread32be(drvdata->regs + (offset << 2));
	}
#ifdef CONFIG_PPC_DCR
	else
		return dcr_read(drvdata->dcr_host, offset);
#endif
	return 0;
}

static int
xilinx_fb_setcolreg(unsigned int regno, unsigned int red, unsigned int green,
		    unsigned int blue, unsigned int transp, struct fb_info *fbi)
{
	u32 *palette = fbi->pseudo_palette;

	if (regno >= PALETTE_ENTRIES_NO)
		return -EINVAL;

	if (fbi->var.grayscale) {
		/* Convert color to grayscale.
		 * grayscale = 0.30*R + 0.59*G + 0.11*B
		 */
		blue = (red * 77 + green * 151 + blue * 28 + 127) >> 8;
		green = blue;
		red = green;
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
		xilinx_fb_out32(drvdata, REG_CTRL, drvdata->reg_ctrl_default);
		break;

	case FB_BLANK_NORMAL:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_POWERDOWN:
		/* turn off panel */
		xilinx_fb_out32(drvdata, REG_CTRL, 0);
		break;

	default:
		break;
	}
	return 0; /* success */
}

static const struct fb_ops xilinxfb_ops = {
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

static int xilinxfb_assign(struct platform_device *pdev,
			   struct xilinxfb_drvdata *drvdata,
			   struct xilinxfb_platform_data *pdata)
{
	int rc;
	struct device *dev = &pdev->dev;
	int fbsize = pdata->xvirt * pdata->yvirt * BYTES_PER_PIXEL;

	if (drvdata->flags & BUS_ACCESS_FLAG) {
		struct resource *res;

		drvdata->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
		if (IS_ERR(drvdata->regs))
			return PTR_ERR(drvdata->regs);

		drvdata->regs_phys = res->start;
	}

	/* Allocate the framebuffer memory */
	if (pdata->fb_phys) {
		drvdata->fb_phys = pdata->fb_phys;
		drvdata->fb_virt = ioremap(pdata->fb_phys, fbsize);
	} else {
		drvdata->fb_alloced = 1;
		drvdata->fb_virt = dma_alloc_coherent(dev, PAGE_ALIGN(fbsize),
						      &drvdata->fb_phys,
						      GFP_KERNEL);
	}

	if (!drvdata->fb_virt) {
		dev_err(dev, "Could not allocate frame buffer memory\n");
		return -ENOMEM;
	}

	/* Clear (turn to black) the framebuffer */
	memset_io((void __iomem *)drvdata->fb_virt, 0, fbsize);

	/* Tell the hardware where the frame buffer is */
	xilinx_fb_out32(drvdata, REG_FB_ADDR, drvdata->fb_phys);
	rc = xilinx_fb_in32(drvdata, REG_FB_ADDR);
	/* Endianness detection */
	if (rc != drvdata->fb_phys) {
		drvdata->flags |= LITTLE_ENDIAN_ACCESS;
		xilinx_fb_out32(drvdata, REG_FB_ADDR, drvdata->fb_phys);
	}

	/* Turn on the display */
	drvdata->reg_ctrl_default = REG_CTRL_ENABLE;
	if (pdata->rotate_screen)
		drvdata->reg_ctrl_default |= REG_CTRL_ROTATE;
	xilinx_fb_out32(drvdata, REG_CTRL, drvdata->reg_ctrl_default);

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

	if (drvdata->flags & BUS_ACCESS_FLAG) {
		/* Put a banner in the log (for DEBUG) */
		dev_dbg(dev, "regs: phys=%pa, virt=%p\n",
			&drvdata->regs_phys, drvdata->regs);
	}
	/* Put a banner in the log (for DEBUG) */
	dev_dbg(dev, "fb: phys=%llx, virt=%p, size=%x\n",
		(unsigned long long)drvdata->fb_phys, drvdata->fb_virt, fbsize);

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
	xilinx_fb_out32(drvdata, REG_CTRL, 0);

	return rc;
}

static void xilinxfb_release(struct device *dev)
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
	xilinx_fb_out32(drvdata, REG_CTRL, 0);

#ifdef CONFIG_PPC_DCR
	/* Release the resources, as allocated based on interface */
	if (!(drvdata->flags & BUS_ACCESS_FLAG))
		dcr_unmap(drvdata->dcr_host, drvdata->dcr_len);
#endif
}

/* ---------------------------------------------------------------------
 * OF bus binding
 */

static int xilinxfb_of_probe(struct platform_device *pdev)
{
	const u32 *prop;
	u32 tft_access = 0;
	struct xilinxfb_platform_data pdata;
	int size;
	struct xilinxfb_drvdata *drvdata;

	/* Copy with the default pdata (not a ptr reference!) */
	pdata = xilinx_fb_default_pdata;

	/* Allocate the driver data region */
	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	/*
	 * To check whether the core is connected directly to DCR or BUS
	 * interface and initialize the tft_access accordingly.
	 */
	of_property_read_u32(pdev->dev.of_node, "xlnx,dcr-splb-slave-if",
			     &tft_access);

	/*
	 * Fill the resource structure if its direct BUS interface
	 * otherwise fill the dcr_host structure.
	 */
	if (tft_access)
		drvdata->flags |= BUS_ACCESS_FLAG;
#ifdef CONFIG_PPC_DCR
	else {
		int start;

		start = dcr_resource_start(pdev->dev.of_node, 0);
		drvdata->dcr_len = dcr_resource_len(pdev->dev.of_node, 0);
		drvdata->dcr_host = dcr_map(pdev->dev.of_node, start, drvdata->dcr_len);
		if (!DCR_MAP_OK(drvdata->dcr_host)) {
			dev_err(&pdev->dev, "invalid DCR address\n");
			return -ENODEV;
		}
	}
#endif

	prop = of_get_property(pdev->dev.of_node, "phys-size", &size);
	if ((prop) && (size >= sizeof(u32) * 2)) {
		pdata.screen_width_mm = prop[0];
		pdata.screen_height_mm = prop[1];
	}

	prop = of_get_property(pdev->dev.of_node, "resolution", &size);
	if ((prop) && (size >= sizeof(u32) * 2)) {
		pdata.xres = prop[0];
		pdata.yres = prop[1];
	}

	prop = of_get_property(pdev->dev.of_node, "virtual-resolution", &size);
	if ((prop) && (size >= sizeof(u32) * 2)) {
		pdata.xvirt = prop[0];
		pdata.yvirt = prop[1];
	}

	pdata.rotate_screen = of_property_read_bool(pdev->dev.of_node, "rotate-display");

	platform_set_drvdata(pdev, drvdata);
	return xilinxfb_assign(pdev, drvdata, &pdata);
}

static int xilinxfb_of_remove(struct platform_device *op)
{
	xilinxfb_release(&op->dev);

	return 0;
}

/* Match table for of_platform binding */
static const struct of_device_id xilinxfb_of_match[] = {
	{ .compatible = "xlnx,xps-tft-1.00.a", },
	{ .compatible = "xlnx,xps-tft-2.00.a", },
	{ .compatible = "xlnx,xps-tft-2.01.a", },
	{ .compatible = "xlnx,plb-tft-cntlr-ref-1.00.a", },
	{ .compatible = "xlnx,plb-dvi-cntlr-ref-1.00.c", },
	{},
};
MODULE_DEVICE_TABLE(of, xilinxfb_of_match);

static struct platform_driver xilinxfb_of_driver = {
	.probe = xilinxfb_of_probe,
	.remove = xilinxfb_of_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = xilinxfb_of_match,
	},
};

module_platform_driver(xilinxfb_of_driver);

MODULE_AUTHOR("MontaVista Software, Inc. <source@mvista.com>");
MODULE_DESCRIPTION("Xilinx TFT frame buffer driver");
MODULE_LICENSE("GPL");
