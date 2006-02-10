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
 *	Copyright (c) 2005  Maciej W. Rozycki
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
#include <linux/types.h>

#include <asm/io.h>
#include <asm/system.h>

#include <asm/dec/tc.h>

#include <video/pmag-ba-fb.h>


struct pmagbafb_par {
	struct fb_info *next;
	volatile void __iomem *mmio;
	volatile u32 __iomem *dac;
	int slot;
};


static struct fb_info *root_pmagbafb_dev;

static struct fb_var_screeninfo pmagbafb_defined __initdata = {
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
	.lower_margin	= 12,
	.hsync_len	= 128,
	.vsync_len	= 3,
	.sync		= FB_SYNC_ON_GREEN,
	.vmode		= FB_VMODE_NONINTERLACED,
};

static struct fb_fix_screeninfo pmagbafb_fix __initdata = {
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

	BUG_ON(regno >= info->cmap.len);

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
static void __init pmagbafb_erase_cursor(struct fb_info *info)
{
	struct pmagbafb_par *par = info->par;

	mb();
	dac_write(par, BT459_ADDR_LO, 0x00);
	dac_write(par, BT459_ADDR_HI, 0x03);
	wmb();
	dac_write(par, BT459_DATA, 0x00);
}


static int __init pmagbafb_init_one(int slot)
{
	struct fb_info *info;
	struct pmagbafb_par *par;
	unsigned long base_addr;

	info = framebuffer_alloc(sizeof(struct pmagbafb_par), NULL);
	if (!info)
		return -ENOMEM;

	par = info->par;
	par->slot = slot;
	claim_tc_card(par->slot);

	base_addr = get_tc_base_addr(par->slot);

	par->next = root_pmagbafb_dev;
	root_pmagbafb_dev = info;

	if (fb_alloc_cmap(&info->cmap, 256, 0) < 0)
		goto err_alloc;

	info->fbops = &pmagbafb_ops;
	info->fix = pmagbafb_fix;
	info->var = pmagbafb_defined;
	info->flags = FBINFO_DEFAULT;

	/* MMIO mapping setup.  */
	info->fix.mmio_start = base_addr;
	par->mmio = ioremap_nocache(info->fix.mmio_start, info->fix.mmio_len);
	if (!par->mmio)
		goto err_cmap;
	par->dac = par->mmio + PMAG_BA_BT459;

	/* Frame buffer mapping setup.  */
	info->fix.smem_start = base_addr + PMAG_BA_FBMEM;
	info->screen_base = ioremap_nocache(info->fix.smem_start,
					    info->fix.smem_len);
	if (!info->screen_base)
		goto err_mmio_map;
	info->screen_size = info->fix.smem_len;

	pmagbafb_erase_cursor(info);

	if (register_framebuffer(info) < 0)
		goto err_smem_map;

	pr_info("fb%d: %s frame buffer device in slot %d\n",
		info->node, info->fix.id, par->slot);

	return 0;


err_smem_map:
	iounmap(info->screen_base);

err_mmio_map:
	iounmap(par->mmio);

err_cmap:
	fb_dealloc_cmap(&info->cmap);

err_alloc:
	root_pmagbafb_dev = par->next;
	release_tc_card(par->slot);
	framebuffer_release(info);
	return -ENXIO;
}

static void __exit pmagbafb_exit_one(void)
{
	struct fb_info *info = root_pmagbafb_dev;
	struct pmagbafb_par *par = info->par;

	unregister_framebuffer(info);
	iounmap(info->screen_base);
	iounmap(par->mmio);
	fb_dealloc_cmap(&info->cmap);
	root_pmagbafb_dev = par->next;
	release_tc_card(par->slot);
	framebuffer_release(info);
}


/*
 * Initialise the framebuffer.
 */
static int __init pmagbafb_init(void)
{
	int count = 0;
	int slot;

	if (fb_get_options("pmagbafb", NULL))
		return -ENXIO;

	while ((slot = search_tc_card("PMAG-BA")) >= 0) {
		if (pmagbafb_init_one(slot) < 0)
			break;
		count++;
	}
	return (count > 0) ? 0 : -ENXIO;
}

static void __exit pmagbafb_exit(void)
{
	while (root_pmagbafb_dev)
		pmagbafb_exit_one();
}


module_init(pmagbafb_init);
module_exit(pmagbafb_exit);

MODULE_LICENSE("GPL");
