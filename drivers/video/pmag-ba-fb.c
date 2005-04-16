/*
 *      linux/drivers/video/pmag-ba-fb.c
 *
 *	PMAG-BA TurboChannel framebuffer card support ... derived from:
 *	"HP300 Topcat framebuffer support (derived from macfb of all things)
 *	Phil Blundell <philb@gnu.org> 1998", the original code can be
 *      found in the file hpfb.c in the same directory.
 *
 *	Based on digital document:
 * 	"PMAG-BA TURBOchannel Color Frame Buffer
 *	 Functional Specification", Revision 1.2, August 27, 1990
 *
 *      DECstation related code Copyright (C) 1999, 2000, 2001 by
 *      Michael Engel <engel@unix-ag.org>, 
 *      Karsten Merker <merker@linuxtag.org> and
 *	Harald Koerfgen.
 *      This file is subject to the terms and conditions of the GNU General
 *      Public License.  See the file COPYING in the main directory of this
 *      archive for more details.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <asm/bootinfo.h>
#include <asm/dec/machtype.h>
#include <asm/dec/tc.h>
#include <video/pmag-ba-fb.h>

struct pmag_ba_ramdac_regs {
	unsigned char addr_low;
	unsigned char pad0[3];
	unsigned char addr_hi;
	unsigned char pad1[3];
	unsigned char data;
	unsigned char pad2[3];
	unsigned char cmap;
};

/*
 * Max 3 TURBOchannel slots -> max 3 PMAG-BA :)
 */
static struct fb_info pmagba_fb_info[3];

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
	.height		= 274,
	.width		= 195,
	.accel		= FB_ACCEL_NONE,
	.vmode		= FB_VMODE_NONINTERLACED,
};

static struct fb_fix_screeninfo pmagbafb_fix = {
	.id		= "PMAG-BA",
	.smem_len	= (1024 * 864),
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_PSEUDOCOLOR,
	.line_length	= 1024,
};

/*
 * Turn hardware cursor off
 */
void pmagbafb_erase_cursor(struct pmag_ba_ramdac_regs *bt459_regs)
{
	bt459_regs->addr_low = 0;
	bt459_regs->addr_hi = 3;
	bt459_regs->data = 0;
}

/*
 * Set the palette. 
 */
static int pmagbafb_setcolreg(unsigned regno, unsigned red, unsigned green,
			      unsigned blue, unsigned transp,
			      struct fb_info *info)
{
	struct pmag_ba_ramdac_regs *bt459_regs = (struct pmag_ba_ramdac_regs *) info->par;

	if (regno >= info->cmap.len)
		return 1;

	red   >>= 8;	/* The cmap fields are 16 bits    */
	green >>= 8;	/* wide, but the harware colormap */
	blue  >>= 8;	/* registers are only 8 bits wide */

	bt459_regs->addr_low = (__u8) regno;
	bt459_regs->addr_hi = 0;
	bt459_regs->cmap = red;
	bt459_regs->cmap = green;
	bt459_regs->cmap = blue;
	return 0;
}

static struct fb_ops pmagbafb_ops = {
	.owner		= THIS_MODULE,
	.fb_get_fix	= gen_get_fix,
	.fb_get_var	= gen_get_var,
	.fb_setcolreg	= pmagbafb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_cursor	= soft_cursor,
};

int __init pmagbafb_init_one(int slot)
{
	unsigned long base_addr = get_tc_base_addr(slot);
	struct fb_info *info = &pmagba_fb_info[slot]; 
	struct display *disp = &pmagba_disp[slot];

	printk("PMAG-BA framebuffer in slot %d\n", slot);
	/*
	 * Framebuffer display memory base address and friends
	 */
	pmagbafb_fix.smem_start = base_addr + PMAG_BA_ONBOARD_FBMEM_OFFSET;
	info->par = (base_addr + PMAG_BA_BT459_OFFSET);

	/*
	 * Configure the Bt459 RAM DAC
	 */
	pmagbafb_erase_cursor((struct pmag_ba_ramdac_regs *) info->par);

	/*
	 *      Let there be consoles..
	 */
	info->fbops = &pmagbafb_ops;
	info->var = pmagbafb_defined;
	info->fix = pmagbafb_fix; 
	info->screen_base = pmagbafb_fix.smem_start;
	info->flags = FBINFO_DEFAULT;

	fb_alloc_cmap(&fb_info.cmap, 256, 0);

	if (register_framebuffer(info) < 0)
		return 1;
	return 0;
}

/* 
 * Initialise the framebuffer
 */

int __init pmagbafb_init(void)
{
	int sid;
	int found = 0;

	if (fb_get_options("pmagbafb", NULL))
		return -ENODEV;

	if (TURBOCHANNEL) {
		while ((sid = search_tc_card("PMAG-BA")) >= 0) {
			found = 1;
			claim_tc_card(sid);
			pmagbafb_init_one(sid);
		}
		return found ? 0 : -ENODEV;
	} else {
		return -ENODEV;
	}
}

module_init(pmagbafb_init);
MODULE_LICENSE("GPL");
