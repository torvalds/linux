/*
 * xilinxfb.c
 *
 * Xilinx TFT LCD frame buffer driver
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * 2002-2007 (c) MontaVista Software, Inc.  This file is licensed under the
 * terms of the GNU General Public License version 2.  This program is licensed
 * "as is" without any warranty of any kind, whether express or implied.
 */

/*
 * This driver was based on au1100fb.c by MontaVista rewritten for 2.6
 * by Embedded Alley Solutions <source@embeddedalley.com>, which in turn
 * was based on skeletonfb.c, Skeleton for a frame buffer device by
 * Geert Uytterhoeven.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include <syslib/virtex_devices.h>

#define DRIVER_NAME		"xilinxfb"
#define DRIVER_DESCRIPTION	"Xilinx TFT LCD frame buffer driver"

/*
 * Xilinx calls it "PLB TFT LCD Controller" though it can also be used for
 * the VGA port on the Xilinx ML40x board. This is a hardware display controller
 * for a 640x480 resolution TFT or VGA screen.
 *
 * The interface to the framebuffer is nice and simple.  There are two
 * control registers.  The first tells the LCD interface where in memory
 * the frame buffer is (only the 11 most significant bits are used, so
 * don't start thinking about scrolling).  The second allows the LCD to
 * be turned on or off as well as rotated 180 degrees.
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
#define XRES		640
#define YRES		480
#define XRES_VIRTUAL	1024
#define YRES_VIRTUAL	YRES
#define LINE_LENGTH	(XRES_VIRTUAL * BYTES_PER_PIXEL)
#define FB_SIZE		(YRES_VIRTUAL * LINE_LENGTH)

#define RED_SHIFT	16
#define GREEN_SHIFT	8
#define BLUE_SHIFT	0

#define PALETTE_ENTRIES_NO	16	/* passed to fb_alloc_cmap() */

/*
 * Here are the default fb_fix_screeninfo and fb_var_screeninfo structures
 */
static struct fb_fix_screeninfo xilinx_fb_fix = {
	.id =		"Xilinx",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_TRUECOLOR,
	.smem_len =	FB_SIZE,
	.line_length =	LINE_LENGTH,
	.accel =	FB_ACCEL_NONE
};

static struct fb_var_screeninfo xilinx_fb_var = {
	.xres =			XRES,
	.yres =			YRES,
	.xres_virtual =		XRES_VIRTUAL,
	.yres_virtual =		YRES_VIRTUAL,

	.bits_per_pixel =	BITS_PER_PIXEL,

	.red =		{ RED_SHIFT, 8, 0 },
	.green =	{ GREEN_SHIFT, 8, 0 },
	.blue =		{ BLUE_SHIFT, 8, 0 },
	.transp =	{ 0, 0, 0 },

	.activate =	FB_ACTIVATE_NOW
};

struct xilinxfb_drvdata {

	struct fb_info	info;		/* FB driver info record */

	u32		regs_phys;	/* phys. address of the control registers */
	u32 __iomem	*regs;		/* virt. address of the control registers */

	unsigned char __iomem	*fb_virt;	/* virt. address of the frame buffer */
	dma_addr_t	fb_phys;	/* phys. address of the frame buffer */

	u32		reg_ctrl_default;

	u32		pseudo_palette[PALETTE_ENTRIES_NO];
					/* Fake palette of 16 colors */
};

#define to_xilinxfb_drvdata(_info) \
	container_of(_info, struct xilinxfb_drvdata, info)

/*
 * The LCD controller has DCR interface to its registers, but all
 * the boards and configurations the driver has been tested with
 * use opb2dcr bridge. So the registers are seen as memory mapped.
 * This macro is to make it simple to add the direct DCR access
 * when it's needed.
 */
#define xilinx_fb_out_be32(driverdata, offset, val) \
	out_be32(driverdata->regs + offset, val)

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

/* === The device driver === */

static int
xilinxfb_drv_probe(struct device *dev)
{
	struct platform_device *pdev;
	struct xilinxfb_platform_data *pdata;
	struct xilinxfb_drvdata *drvdata;
	struct resource *regs_res;
	int retval;

	if (!dev)
		return -EINVAL;

	pdev = to_platform_device(dev);
	pdata = pdev->dev.platform_data;

	if (pdata == NULL) {
		printk(KERN_ERR "Couldn't find platform data.\n");
		return -EFAULT;
	}

	drvdata = kzalloc(sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata) {
		printk(KERN_ERR "Couldn't allocate device private record\n");
		return -ENOMEM;
	}
	dev_set_drvdata(dev, drvdata);

	/* Map the control registers in */
	regs_res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!regs_res || (regs_res->end - regs_res->start + 1 < 8)) {
		printk(KERN_ERR "Couldn't get registers resource\n");
		retval = -EFAULT;
		goto failed1;
	}

	if (!request_mem_region(regs_res->start, 8, DRIVER_NAME)) {
		printk(KERN_ERR
		       "Couldn't lock memory region at 0x%08X\n",
		       regs_res->start);
		retval = -EBUSY;
		goto failed1;
	}
	drvdata->regs = (u32 __iomem*) ioremap(regs_res->start, 8);
	drvdata->regs_phys = regs_res->start;

	/* Allocate the framebuffer memory */
	drvdata->fb_virt = dma_alloc_coherent(dev, PAGE_ALIGN(FB_SIZE),
				&drvdata->fb_phys, GFP_KERNEL);
	if (!drvdata->fb_virt) {
		printk(KERN_ERR "Could not allocate frame buffer memory\n");
		retval = -ENOMEM;
		goto failed2;
	}

	/* Clear (turn to black) the framebuffer */
	memset_io((void *) drvdata->fb_virt, 0, FB_SIZE);

	/* Tell the hardware where the frame buffer is */
	xilinx_fb_out_be32(drvdata, REG_FB_ADDR, drvdata->fb_phys);

	/* Turn on the display */
	if (pdata->rotate_screen) {
		drvdata->reg_ctrl_default = REG_CTRL_ENABLE | REG_CTRL_ROTATE;
	} else {
		drvdata->reg_ctrl_default = REG_CTRL_ENABLE;
	}
	xilinx_fb_out_be32(drvdata, REG_CTRL, drvdata->reg_ctrl_default);

	/* Fill struct fb_info */
	drvdata->info.device = dev;
	drvdata->info.screen_base = drvdata->fb_virt;
	drvdata->info.fbops = &xilinxfb_ops;
	drvdata->info.fix = xilinx_fb_fix;
	drvdata->info.fix.smem_start = drvdata->fb_phys;
	drvdata->info.pseudo_palette = drvdata->pseudo_palette;

	if (fb_alloc_cmap(&drvdata->info.cmap, PALETTE_ENTRIES_NO, 0) < 0) {
		printk(KERN_ERR "Fail to allocate colormap (%d entries)\n",
			PALETTE_ENTRIES_NO);
		retval = -EFAULT;
		goto failed3;
	}

	drvdata->info.flags = FBINFO_DEFAULT;
	xilinx_fb_var.height = pdata->screen_height_mm;
	xilinx_fb_var.width = pdata->screen_width_mm;
	drvdata->info.var = xilinx_fb_var;

	/* Register new frame buffer */
	if (register_framebuffer(&drvdata->info) < 0) {
		printk(KERN_ERR "Could not register frame buffer\n");
		retval = -EINVAL;
		goto failed4;
	}

	return 0;	/* success */

failed4:
	fb_dealloc_cmap(&drvdata->info.cmap);

failed3:
	dma_free_coherent(dev, PAGE_ALIGN(FB_SIZE), drvdata->fb_virt,
		drvdata->fb_phys);

	/* Turn off the display */
	xilinx_fb_out_be32(drvdata, REG_CTRL, 0);
	iounmap(drvdata->regs);

failed2:
	release_mem_region(regs_res->start, 8);

failed1:
	kfree(drvdata);
	dev_set_drvdata(dev, NULL);

	return retval;
}

static int
xilinxfb_drv_remove(struct device *dev)
{
	struct xilinxfb_drvdata *drvdata;

	if (!dev)
		return -ENODEV;

	drvdata = (struct xilinxfb_drvdata *) dev_get_drvdata(dev);

#if !defined(CONFIG_FRAMEBUFFER_CONSOLE) && defined(CONFIG_LOGO)
	xilinx_fb_blank(VESA_POWERDOWN, &drvdata->info);
#endif

	unregister_framebuffer(&drvdata->info);

	fb_dealloc_cmap(&drvdata->info.cmap);

	dma_free_coherent(dev, PAGE_ALIGN(FB_SIZE), drvdata->fb_virt,
		drvdata->fb_phys);

	/* Turn off the display */
	xilinx_fb_out_be32(drvdata, REG_CTRL, 0);
	iounmap(drvdata->regs);

	release_mem_region(drvdata->regs_phys, 8);

	kfree(drvdata);
	dev_set_drvdata(dev, NULL);

	return 0;
}


static struct device_driver xilinxfb_driver = {
	.name		= DRIVER_NAME,
	.bus		= &platform_bus_type,

	.probe		= xilinxfb_drv_probe,
	.remove		= xilinxfb_drv_remove
};

static int __init
xilinxfb_init(void)
{
	/*
	 * No kernel boot options used,
	 * so we just need to register the driver
	 */
	return driver_register(&xilinxfb_driver);
}

static void __exit
xilinxfb_cleanup(void)
{
	driver_unregister(&xilinxfb_driver);
}

module_init(xilinxfb_init);
module_exit(xilinxfb_cleanup);

MODULE_AUTHOR("MontaVista Software, Inc. <source@mvista.com>");
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_LICENSE("GPL");
