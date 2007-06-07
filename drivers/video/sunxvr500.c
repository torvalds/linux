/* sunxvr500.c: Sun 3DLABS XVR-500 Expert3D driver for sparc64 systems
 *
 * Copyright (C) 2007 David S. Miller (davem@davemloft.net)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/prom.h>
#include <asm/of_device.h>

/* XXX This device has a 'dev-comm' property which aparently is
 * XXX a pointer into the openfirmware's address space which is
 * XXX a shared area the kernel driver can use to keep OBP
 * XXX informed about the current resolution setting.  The idea
 * XXX is that the kernel can change resolutions, and as long
 * XXX as the values in the 'dev-comm' area are accurate then
 * XXX OBP can still render text properly to the console.
 * XXX
 * XXX I'm still working out the layout of this and whether there
 * XXX are any signatures we need to look for etc.
 */
struct e3d_info {
	struct fb_info		*info;
	struct pci_dev		*pdev;

	spinlock_t		lock;

	char __iomem		*fb_base;
	unsigned long		fb_base_phys;

	unsigned long		fb8_buf_diff;
	unsigned long		regs_base_phys;

	void __iomem		*ramdac;

	struct device_node	*of_node;

	unsigned int		width;
	unsigned int		height;
	unsigned int		depth;
	unsigned int		fb_size;

	u32			fb_base_reg;
	u32			fb8_0_off;
	u32			fb8_1_off;

	u32			pseudo_palette[16];
};

static int __devinit e3d_get_props(struct e3d_info *ep)
{
	ep->width = of_getintprop_default(ep->of_node, "width", 0);
	ep->height = of_getintprop_default(ep->of_node, "height", 0);
	ep->depth = of_getintprop_default(ep->of_node, "depth", 8);

	if (!ep->width || !ep->height) {
		printk(KERN_ERR "e3d: Critical properties missing for %s\n",
		       pci_name(ep->pdev));
		return -EINVAL;
	}

	return 0;
}

/* My XVR-500 comes up, at 1280x768 and a FB base register value of
 * 0x04000000, the following video layout register values:
 *
 * RAMDAC_VID_WH	0x03ff04ff
 * RAMDAC_VID_CFG	0x1a0b0088
 * RAMDAC_VID_32FB_0	0x04000000
 * RAMDAC_VID_32FB_1	0x04800000
 * RAMDAC_VID_8FB_0	0x05000000
 * RAMDAC_VID_8FB_1	0x05200000
 * RAMDAC_VID_XXXFB	0x05400000
 * RAMDAC_VID_YYYFB	0x05c00000
 * RAMDAC_VID_ZZZFB	0x05e00000
 */
/* Video layout registers */
#define RAMDAC_VID_WH		0x00000070UL /* (height-1)<<16 | (width-1) */
#define RAMDAC_VID_CFG		0x00000074UL /* 0x1a000088|(linesz_log2<<16) */
#define RAMDAC_VID_32FB_0	0x00000078UL /* PCI base 32bpp FB buffer 0 */
#define RAMDAC_VID_32FB_1	0x0000007cUL /* PCI base 32bpp FB buffer 1 */
#define RAMDAC_VID_8FB_0	0x00000080UL /* PCI base 8bpp FB buffer 0 */
#define RAMDAC_VID_8FB_1	0x00000084UL /* PCI base 8bpp FB buffer 1 */
#define RAMDAC_VID_XXXFB	0x00000088UL /* PCI base of XXX FB */
#define RAMDAC_VID_YYYFB	0x0000008cUL /* PCI base of YYY FB */
#define RAMDAC_VID_ZZZFB	0x00000090UL /* PCI base of ZZZ FB */

/* CLUT registers */
#define RAMDAC_INDEX		0x000000bcUL
#define RAMDAC_DATA		0x000000c0UL

static void e3d_clut_write(struct e3d_info *ep, int index, u32 val)
{
	void __iomem *ramdac = ep->ramdac;
	unsigned long flags;

	spin_lock_irqsave(&ep->lock, flags);

	writel(index, ramdac + RAMDAC_INDEX);
	writel(val, ramdac + RAMDAC_DATA);

	spin_unlock_irqrestore(&ep->lock, flags);
}

static int e3d_setcolreg(unsigned regno,
			 unsigned red, unsigned green, unsigned blue,
			 unsigned transp, struct fb_info *info)
{
	struct e3d_info *ep = info->par;
	u32 red_8, green_8, blue_8;
	u32 red_10, green_10, blue_10;
	u32 value;

	if (regno >= 256)
		return 1;

	red_8 = red >> 8;
	green_8 = green >> 8;
	blue_8 = blue >> 8;

	value = (blue_8 << 24) | (green_8 << 16) | (red_8 << 8);

	if (info->fix.visual == FB_VISUAL_TRUECOLOR && regno < 16)
		((u32 *)info->pseudo_palette)[regno] = value;


	red_10 = red >> 6;
	green_10 = green >> 6;
	blue_10 = blue >> 6;

	value = (blue_10 << 20) | (green_10 << 10) | (red_10 << 0);
	e3d_clut_write(ep, regno, value);

	return 0;
}

/* XXX This is a bit of a hack.  I can't figure out exactly how the
 * XXX two 8bpp areas of the framebuffer work.  I imagine there is
 * XXX a WID attribute somewhere else in the framebuffer which tells
 * XXX the ramdac which of the two 8bpp framebuffer regions to take
 * XXX the pixel from.  So, for now, render into both regions to make
 * XXX sure the pixel shows up.
 */
static void e3d_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct e3d_info *ep = info->par;
	unsigned long flags;

	spin_lock_irqsave(&ep->lock, flags);
	cfb_imageblit(info, image);
	info->screen_base += ep->fb8_buf_diff;
	cfb_imageblit(info, image);
	info->screen_base -= ep->fb8_buf_diff;
	spin_unlock_irqrestore(&ep->lock, flags);
}

static void e3d_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct e3d_info *ep = info->par;
	unsigned long flags;

	spin_lock_irqsave(&ep->lock, flags);
	cfb_fillrect(info, rect);
	info->screen_base += ep->fb8_buf_diff;
	cfb_fillrect(info, rect);
	info->screen_base -= ep->fb8_buf_diff;
	spin_unlock_irqrestore(&ep->lock, flags);
}

static void e3d_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	struct e3d_info *ep = info->par;
	unsigned long flags;

	spin_lock_irqsave(&ep->lock, flags);
	cfb_copyarea(info, area);
	info->screen_base += ep->fb8_buf_diff;
	cfb_copyarea(info, area);
	info->screen_base -= ep->fb8_buf_diff;
	spin_unlock_irqrestore(&ep->lock, flags);
}

static struct fb_ops e3d_ops = {
	.owner			= THIS_MODULE,
	.fb_setcolreg		= e3d_setcolreg,
	.fb_fillrect		= e3d_fillrect,
	.fb_copyarea		= e3d_copyarea,
	.fb_imageblit		= e3d_imageblit,
};

static int __devinit e3d_set_fbinfo(struct e3d_info *ep)
{
	struct fb_info *info = ep->info;
	struct fb_var_screeninfo *var = &info->var;

	info->flags = FBINFO_DEFAULT;
	info->fbops = &e3d_ops;
	info->screen_base = ep->fb_base;
	info->screen_size = ep->fb_size;

	info->pseudo_palette = ep->pseudo_palette;

	/* Fill fix common fields */
	strlcpy(info->fix.id, "e3d", sizeof(info->fix.id));
        info->fix.smem_start = ep->fb_base_phys;
        info->fix.smem_len = ep->fb_size;
        info->fix.type = FB_TYPE_PACKED_PIXELS;
	if (ep->depth == 32 || ep->depth == 24)
		info->fix.visual = FB_VISUAL_TRUECOLOR;
	else
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;

	var->xres = ep->width;
	var->yres = ep->height;
	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;
	var->bits_per_pixel = ep->depth;

	var->red.offset = 8;
	var->red.length = 8;
	var->green.offset = 16;
	var->green.length = 8;
	var->blue.offset = 24;
	var->blue.length = 8;
	var->transp.offset = 0;
	var->transp.length = 0;

	if (fb_alloc_cmap(&info->cmap, 256, 0)) {
		printk(KERN_ERR "e3d: Cannot allocate color map.\n");
		return -ENOMEM;
	}

        return 0;
}

static int __devinit e3d_pci_register(struct pci_dev *pdev,
				      const struct pci_device_id *ent)
{
	struct fb_info *info;
	struct e3d_info *ep;
	unsigned int line_length;
	int err;

	err = pci_enable_device(pdev);
	if (err < 0) {
		printk(KERN_ERR "e3d: Cannot enable PCI device %s\n",
		       pci_name(pdev));
		goto err_out;
	}

	info = framebuffer_alloc(sizeof(struct e3d_info), &pdev->dev);
	if (!info) {
		printk(KERN_ERR "e3d: Cannot allocate fb_info\n");
		err = -ENOMEM;
		goto err_disable;
	}

	ep = info->par;
	ep->info = info;
	ep->pdev = pdev;
	spin_lock_init(&ep->lock);
	ep->of_node = pci_device_to_OF_node(pdev);
	if (!ep->of_node) {
		printk(KERN_ERR "e3d: Cannot find OF node of %s\n",
		       pci_name(pdev));
		err = -ENODEV;
		goto err_release_fb;
	}

	/* Read the PCI base register of the frame buffer, which we
	 * need in order to interpret the RAMDAC_VID_*FB* values in
	 * the ramdac correctly.
	 */
	pci_read_config_dword(pdev, PCI_BASE_ADDRESS_0,
			      &ep->fb_base_reg);
	ep->fb_base_reg &= PCI_BASE_ADDRESS_MEM_MASK;

	ep->regs_base_phys = pci_resource_start (pdev, 1);
	err = pci_request_region(pdev, 1, "e3d regs");
	if (err < 0) {
		printk("e3d: Cannot request region 1 for %s\n",
		       pci_name(pdev));
		goto err_release_fb;
	}
	ep->ramdac = ioremap(ep->regs_base_phys + 0x8000, 0x1000);
	if (!ep->ramdac)
		goto err_release_pci1;

	ep->fb8_0_off = readl(ep->ramdac + RAMDAC_VID_8FB_0);
	ep->fb8_0_off -= ep->fb_base_reg;

	ep->fb8_1_off = readl(ep->ramdac + RAMDAC_VID_8FB_1);
	ep->fb8_1_off -= ep->fb_base_reg;

	ep->fb8_buf_diff = ep->fb8_1_off - ep->fb8_0_off;

	ep->fb_base_phys = pci_resource_start (pdev, 0);
	ep->fb_base_phys += ep->fb8_0_off;

	err = pci_request_region(pdev, 0, "e3d framebuffer");
	if (err < 0) {
		printk("e3d: Cannot request region 0 for %s\n",
		       pci_name(pdev));
		goto err_unmap_ramdac;
	}

	err = e3d_get_props(ep);
	if (err)
		goto err_release_pci0;

	line_length = (readl(ep->ramdac + RAMDAC_VID_CFG) >> 16) & 0xff;
	line_length = 1 << line_length;

	switch (ep->depth) {
	case 8:
		info->fix.line_length = line_length;
		break;
	case 16:
		info->fix.line_length = line_length * 2;
		break;
	case 24:
		info->fix.line_length = line_length * 3;
		break;
	case 32:
		info->fix.line_length = line_length * 4;
		break;
	}
	ep->fb_size = info->fix.line_length * ep->height;

	ep->fb_base = ioremap(ep->fb_base_phys, ep->fb_size);
	if (!ep->fb_base)
		goto err_release_pci0;

	err = e3d_set_fbinfo(ep);
	if (err)
		goto err_unmap_fb;

	pci_set_drvdata(pdev, info);

	printk("e3d: Found device at %s\n", pci_name(pdev));

	err = register_framebuffer(info);
	if (err < 0) {
		printk(KERN_ERR "e3d: Could not register framebuffer %s\n",
		       pci_name(pdev));
		goto err_unmap_fb;
	}

	return 0;

err_unmap_fb:
	iounmap(ep->fb_base);

err_release_pci0:
	pci_release_region(pdev, 0);

err_unmap_ramdac:
	iounmap(ep->ramdac);

err_release_pci1:
	pci_release_region(pdev, 1);

err_release_fb:
        framebuffer_release(info);

err_disable:
	pci_disable_device(pdev);

err_out:
	return err;
}

static void __devexit e3d_pci_unregister(struct pci_dev *pdev)
{
	struct fb_info *info = pci_get_drvdata(pdev);
	struct e3d_info *ep = info->par;

	unregister_framebuffer(info);

	iounmap(ep->ramdac);
	iounmap(ep->fb_base);

	pci_release_region(pdev, 0);
	pci_release_region(pdev, 1);

        framebuffer_release(info);

	pci_disable_device(pdev);
}

static struct pci_device_id e3d_pci_table[] = {
	{	PCI_DEVICE(PCI_VENDOR_ID_3DLABS, 0x7a0),	},
	{	PCI_DEVICE(PCI_VENDOR_ID_3DLABS, 0x7a2),	},
	{	.vendor = PCI_VENDOR_ID_3DLABS,
		.device = PCI_ANY_ID,
		.subvendor = PCI_VENDOR_ID_3DLABS,
		.subdevice = 0x0108,
	},
	{	.vendor = PCI_VENDOR_ID_3DLABS,
		.device = PCI_ANY_ID,
		.subvendor = PCI_VENDOR_ID_3DLABS,
		.subdevice = 0x0140,
	},
	{	.vendor = PCI_VENDOR_ID_3DLABS,
		.device = PCI_ANY_ID,
		.subvendor = PCI_VENDOR_ID_3DLABS,
		.subdevice = 0x1024,
	},
	{ 0, }
};

static struct pci_driver e3d_driver = {
	.name		= "e3d",
	.id_table	= e3d_pci_table,
	.probe		= e3d_pci_register,
	.remove		= __devexit_p(e3d_pci_unregister),
};

static int __init e3d_init(void)
{
	if (fb_get_options("e3d", NULL))
		return -ENODEV;

	return pci_register_driver(&e3d_driver);
}

static void __exit e3d_exit(void)
{
	pci_unregister_driver(&e3d_driver);
}

module_init(e3d_init);
module_exit(e3d_exit);

MODULE_DESCRIPTION("framebuffer driver for Sun XVR-500 graphics");
MODULE_AUTHOR("David S. Miller <davem@davemloft.net>");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
