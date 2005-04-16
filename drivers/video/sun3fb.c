/*
 *  linux/drivers/video/sun3fb.c -- Frame buffer driver for Sun3
 *
 * (C) 1998 Thomas Bogendoerfer
 *
 * This driver is bases on sbusfb.c, which is
 *
 *	Copyright (C) 1998 Jakub Jelinek
 *
 *  This driver is partly based on the Open Firmware console driver
 *
 *	Copyright (C) 1997 Geert Uytterhoeven
 *
 *  and SPARC console subsystem
 *
 *      Copyright (C) 1995 Peter Zaitcev (zaitcev@yahoo.com)
 *      Copyright (C) 1995-1997 David S. Miller (davem@caip.rutgers.edu)
 *      Copyright (C) 1995-1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *      Copyright (C) 1996 Dave Redman (djhr@tadpole.co.uk)
 *      Copyright (C) 1996-1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *      Copyright (C) 1996 Eddie C. Dost (ecd@skynet.be)
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/selection.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/kd.h>
#include <linux/vt_kern.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>	/* io_remap_page_range() */

#ifdef CONFIG_SUN3
#include <asm/oplib.h>
#include <asm/machines.h>
#include <asm/idprom.h>

#define CGFOUR_OBMEM_ADDR 0x1f300000
#define BWTWO_OBMEM_ADDR 0x1f000000
#define BWTWO_OBMEM50_ADDR 0x00100000

#endif
#ifdef CONFIG_SUN3X
#include <asm/sun3x.h>
#endif
#include <video/sbusfb.h>

#define DEFAULT_CURSOR_BLINK_RATE       (2*HZ/5)

#define CURSOR_SHAPE			1
#define CURSOR_BLINK			2

#define mymemset(x,y) memset(x,0,y)

    /*
     *  Interface used by the world
     */

int sun3fb_init(void);
void sun3fb_setup(char *options);

static char fontname[40] __initdata = { 0 };
static int curblink __initdata = 1;

static int sun3fb_get_fix(struct fb_fix_screeninfo *fix, int con,
			struct fb_info *info);
static int sun3fb_get_var(struct fb_var_screeninfo *var, int con,
			struct fb_info *info);
static int sun3fb_set_var(struct fb_var_screeninfo *var, int con,
			struct fb_info *info);
static int sun3fb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			struct fb_info *info);
static int sun3fb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			struct fb_info *info);
static int sun3fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			    u_int transp, struct fb_info *info);
static int sun3fb_blank(int blank, struct fb_info *info);
static void sun3fb_cursor(struct display *p, int mode, int x, int y);
static void sun3fb_clear_margin(struct display *p, int s);

    /*
     *  Interface to the low level console driver
     */

static int sun3fbcon_switch(int con, struct fb_info *info);
static int sun3fbcon_updatevar(int con, struct fb_info *info);

    /*
     *  Internal routines
     */

static int sun3fb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
			    u_int *transp, struct fb_info *info);

static struct fb_ops sun3fb_ops = {
	.owner =	THIS_MODULE,
	.fb_get_fix =	sun3fb_get_fix,
	.fb_get_var =	sun3fb_get_var,
	.fb_set_var =	sun3fb_set_var,
	.fb_get_cmap =	sun3fb_get_cmap,
	.fb_set_cmap =	sun3fb_set_cmap,
	.fb_setcolreg =	sun3fb_setcolreg,
	.fb_blank =	sun3fb_blank,
};

static void sun3fb_clear_margin(struct display *p, int s)
{
	struct fb_info_sbusfb *fb = sbusfbinfod(p);
	
	return;

	if (fb->switch_from_graph)
		(*fb->switch_from_graph)(fb);
	if (fb->fill) {
		unsigned short rects [16];

		rects [0] = 0;
		rects [1] = 0;
		rects [2] = fb->var.xres_virtual;
		rects [3] = fb->y_margin;
		rects [4] = 0;
		rects [5] = fb->y_margin;
		rects [6] = fb->x_margin;
		rects [7] = fb->var.yres_virtual;
		rects [8] = fb->var.xres_virtual - fb->x_margin;
		rects [9] = fb->y_margin;
		rects [10] = fb->var.xres_virtual;
		rects [11] = fb->var.yres_virtual;
		rects [12] = fb->x_margin;
		rects [13] = fb->var.yres_virtual - fb->y_margin;
		rects [14] = fb->var.xres_virtual - fb->x_margin;
		rects [15] = fb->var.yres_virtual;
		(*fb->fill)(fb, p, s, 4, rects);
	} else {
		unsigned char *fb_base = fb->info.screen_base, *q;
		int skip_bytes = fb->y_margin * fb->var.xres_virtual;
		int scr_size = fb->var.xres_virtual * fb->var.yres_virtual;
		int h, he, incr, size;

		he = fb->var.yres;
		if (fb->var.bits_per_pixel == 1) {
			fb_base -= (skip_bytes + fb->x_margin) / 8;
			skip_bytes /= 8;
			scr_size /= 8;
			mymemset (fb_base, skip_bytes - fb->x_margin / 8);
			mymemset (fb_base + scr_size - skip_bytes + fb->x_margin / 8, skip_bytes - fb->x_margin / 8);
			incr = fb->var.xres_virtual / 8;
			size = fb->x_margin / 8 * 2;
			for (q = fb_base + skip_bytes - fb->x_margin / 8, h = 0;
			     h <= he; q += incr, h++)
				mymemset (q, size);
		} else {
			fb_base -= (skip_bytes + fb->x_margin);
			memset (fb_base, attr_bgcol(p,s), skip_bytes - fb->x_margin);
			memset (fb_base + scr_size - skip_bytes + fb->x_margin, attr_bgcol(p,s), skip_bytes - fb->x_margin);
			incr = fb->var.xres_virtual;
			size = fb->x_margin * 2;
			for (q = fb_base + skip_bytes - fb->x_margin, h = 0;
			     h <= he; q += incr, h++)
				memset (q, attr_bgcol(p,s), size);
		}
	}
}

static void sun3fb_disp_setup(struct display *p)
{
	struct fb_info_sbusfb *fb = sbusfbinfod(p);

	if (fb->setup)
		fb->setup(p);	
	sun3fb_clear_margin(p, 0);
}

    /*
     *  Get the Fixed Part of the Display
     */

static int sun3fb_get_fix(struct fb_fix_screeninfo *fix, int con,
			  struct fb_info *info)
{
	struct fb_info_sbusfb *fb = sbusfbinfo(info);

	memcpy(fix, &fb->fix, sizeof(struct fb_fix_screeninfo));
	return 0;
}

    /*
     *  Get the User Defined Part of the Display
     */

static int sun3fb_get_var(struct fb_var_screeninfo *var, int con,
			  struct fb_info *info)
{
	struct fb_info_sbusfb *fb = sbusfbinfo(info);

	memcpy(var, &fb->var, sizeof(struct fb_var_screeninfo));
	return 0;
}

    /*
     *  Set the User Defined Part of the Display
     */

static int sun3fb_set_var(struct fb_var_screeninfo *var, int con,
			struct fb_info *info)
{
	struct fb_info_sbusfb *fb = sbusfbinfo(info);

	if (var->xres > fb->var.xres || var->yres > fb->var.yres ||
	    var->xres_virtual > fb->var.xres_virtual ||
	    var->yres_virtual > fb->var.yres_virtual ||
	    var->bits_per_pixel != fb->var.bits_per_pixel ||
	    var->nonstd ||
	    (var->vmode & FB_VMODE_MASK) != FB_VMODE_NONINTERLACED)
		return -EINVAL;
	memcpy(var, &fb->var, sizeof(struct fb_var_screeninfo));
	return 0;
}

    /*
     *  Hardware cursor
     */
     
static unsigned char hw_cursor_cmap[2] = { 0, 0xff };

static void
sun3fb_cursor_timer_handler(unsigned long dev_addr)
{
	struct fb_info_sbusfb *fb = (struct fb_info_sbusfb *)dev_addr;
        
	if (!fb->setcursor) return;
                                
	if (fb->cursor.mode & CURSOR_BLINK) {
		fb->cursor.enable ^= 1;
		fb->setcursor(fb);
	}
	
	fb->cursor.timer.expires = jiffies + fb->cursor.blink_rate;
	add_timer(&fb->cursor.timer);
}

static void sun3fb_cursor(struct display *p, int mode, int x, int y)
{
	struct fb_info_sbusfb *fb = sbusfbinfod(p);
	
	switch (mode) {
	case CM_ERASE:
		fb->cursor.mode &= ~CURSOR_BLINK;
		fb->cursor.enable = 0;
		(*fb->setcursor)(fb);
		break;
				  
	case CM_MOVE:
	case CM_DRAW:
		if (fb->cursor.mode & CURSOR_SHAPE) {
			fb->cursor.size.fbx = fontwidth(p);
			fb->cursor.size.fby = fontheight(p);
			fb->cursor.chot.fbx = 0;
			fb->cursor.chot.fby = 0;
			fb->cursor.enable = 1;
			memset (fb->cursor.bits, 0, sizeof (fb->cursor.bits));
			fb->cursor.bits[0][fontheight(p) - 2] = (0xffffffff << (32 - fontwidth(p)));
			fb->cursor.bits[1][fontheight(p) - 2] = (0xffffffff << (32 - fontwidth(p)));
			fb->cursor.bits[0][fontheight(p) - 1] = (0xffffffff << (32 - fontwidth(p)));
			fb->cursor.bits[1][fontheight(p) - 1] = (0xffffffff << (32 - fontwidth(p)));
			(*fb->setcursormap) (fb, hw_cursor_cmap, hw_cursor_cmap, hw_cursor_cmap);
			(*fb->setcurshape) (fb);
		}
		fb->cursor.mode = CURSOR_BLINK;
		if (fontwidthlog(p))
			fb->cursor.cpos.fbx = (x << fontwidthlog(p)) + fb->x_margin;
		else
			fb->cursor.cpos.fbx = (x * fontwidth(p)) + fb->x_margin;
		if (fontheightlog(p))
			fb->cursor.cpos.fby = (y << fontheightlog(p)) + fb->y_margin;
		else
			fb->cursor.cpos.fby = (y * fontheight(p)) + fb->y_margin;
		(*fb->setcursor)(fb);
		break;
	}
}

    /*
     *  Get the Colormap
     */

static int sun3fb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			 struct fb_info *info)
{
	if (con == info->currcon) /* current console? */
		return fb_get_cmap(cmap, kspc, sun3fb_getcolreg, info);
	else if (fb_display[con].cmap.len) /* non default colormap? */
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
	else
		fb_copy_cmap(fb_default_cmap(1<<fb_display[con].var.bits_per_pixel), cmap, kspc ? 0 : 2);
	return 0;
}

    /*
     *  Set the Colormap
     */

static int sun3fb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			 struct fb_info *info)
{
	int err;

	if (!fb_display[con].cmap.len) {	/* no colormap allocated? */
		if ((err = fb_alloc_cmap(&fb_display[con].cmap, 1<<fb_display[con].var.bits_per_pixel, 0)))
			return err;
	}
	if (con == info->currcon) {			/* current console? */
		err = fb_set_cmap(cmap, kspc, info);
		if (!err) {
			struct fb_info_sbusfb *fb = sbusfbinfo(info);
			
			if (fb->loadcmap)
				(*fb->loadcmap)(fb, &fb_display[con], cmap->start, cmap->len);
		}
		return err;
	} else
		fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
	return 0;
}

    /*
     *  Setup: parse used options
     */

void __init sun3fb_setup(char *options)
{
	char *p;
	
	for (p = options;;) {
		if (!strncmp(p, "font=", 5)) {
			int i;
			
			for (i = 0; i < sizeof(fontname) - 1; i++)
				if (p[i+5] == ' ' || !p[i+5])
					break;
			memcpy(fontname, p+5, i);
			fontname[i] = 0;
		} else if (!strncmp(p, "noblink", 7))
			curblink = 0;
		while (*p && *p != ' ' && *p != ',') p++;
		if (*p != ',') break;
		p++;
	}

	return;
}

static int sun3fbcon_switch(int con, struct fb_info *info)
{
	int x_margin, y_margin;
	struct fb_info_sbusfb *fb = sbusfbinfo(info);
	int lastconsole;
    
	/* Do we have to save the colormap? */
	if (fb_display[info->currcon].cmap.len)
		fb_get_cmap(&fb_display[info->currcon].cmap, 1, sun3fb_getcolreg, info);

	if (info->display_fg) {
		lastconsole = info->display_fg->vc_num;
		if (lastconsole != con && 
		    (fontwidth(&fb_display[lastconsole]) != fontwidth(&fb_display[con]) ||
		     fontheight(&fb_display[lastconsole]) != fontheight(&fb_display[con])))
			fb->cursor.mode |= CURSOR_SHAPE;
	}
	x_margin = (fb_display[con].var.xres_virtual - fb_display[con].var.xres) / 2;
	y_margin = (fb_display[con].var.yres_virtual - fb_display[con].var.yres) / 2;
	if (fb->margins)
		fb->margins(fb, &fb_display[con], x_margin, y_margin);
	if (fb->graphmode || fb->x_margin != x_margin || fb->y_margin != y_margin) {
		fb->x_margin = x_margin; fb->y_margin = y_margin;
		sun3fb_clear_margin(&fb_display[con], 0);
	}
	info->currcon = con;
	/* Install new colormap */
	do_install_cmap(con, info);
	return 0;
}

    /*
     *  Update the `var' structure (called by fbcon.c)
     */

static int sun3fbcon_updatevar(int con, struct fb_info *info)
{
	/* Nothing */
	return 0;
}

    /*
     *  Blank the display.
     */

static int sun3fb_blank(int blank, struct fb_info *info)
{
    struct fb_info_sbusfb *fb = sbusfbinfo(info);
    
    if (blank && fb->blank)
    	return fb->blank(fb);
    else if (!blank && fb->unblank)
    	return fb->unblank(fb);
    return 0;
}

    /*
     *  Read a single color register and split it into
     *  colors/transparent. Return != 0 for invalid regno.
     */

static int sun3fb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
			  u_int *transp, struct fb_info *info)
{
	struct fb_info_sbusfb *fb = sbusfbinfo(info);

	if (!fb->color_map || regno > 255)
		return 1;
	*red = (fb->color_map CM(regno, 0)<<8) | fb->color_map CM(regno, 0);
	*green = (fb->color_map CM(regno, 1)<<8) | fb->color_map CM(regno, 1);
	*blue = (fb->color_map CM(regno, 2)<<8) | fb->color_map CM(regno, 2);
	*transp = 0;
	return 0;
}


    /*
     *  Set a single color register. The values supplied are already
     *  rounded down to the hardware's capabilities (according to the
     *  entries in the var structure). Return != 0 for invalid regno.
     */

static int sun3fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			    u_int transp, struct fb_info *info)
{
	struct fb_info_sbusfb *fb = sbusfbinfo(info);

	if (!fb->color_map || regno > 255)
		return 1;
	red >>= 8;
	green >>= 8;
	blue >>= 8;
	fb->color_map CM(regno, 0) = red;
	fb->color_map CM(regno, 1) = green;
	fb->color_map CM(regno, 2) = blue;
	return 0;
}

static int sun3fb_set_font(struct display *p, int width, int height)
{
	int w = p->var.xres_virtual, h = p->var.yres_virtual;
	int depth = p->var.bits_per_pixel;
	struct fb_info_sbusfb *fb = sbusfbinfod(p);
	int x_margin, y_margin;
	
	if (depth > 8) depth = 8;
	x_margin = (w % width) / 2;
	y_margin = (h % height) / 2;

	p->var.xres = w - 2*x_margin;
	p->var.yres = h - 2*y_margin;
	
	fb->cursor.mode |= CURSOR_SHAPE;
	
	if (fb->margins)
		fb->margins(fb, p, x_margin, y_margin);
	if (fb->x_margin != x_margin || fb->y_margin != y_margin) {
		fb->x_margin = x_margin; fb->y_margin = y_margin;
		sun3fb_clear_margin(p, 0);
	}

	return 1;
}

void sun3fb_palette(int enter)
{
	int i;
	struct display *p;
	
	for (i = 0; i < MAX_NR_CONSOLES; i++) {
		p = &fb_display[i];
		if (p->dispsw && p->dispsw->setup == sun3fb_disp_setup &&
		    p->fb_info->display_fg &&
		    p->fb_info->display_fg->vc_num == i) {
			struct fb_info_sbusfb *fb = sbusfbinfod(p);

			if (fb->restore_palette) {
				if (enter)
					fb->restore_palette(fb);
				else if (vc_cons[i].d->vc_mode != KD_GRAPHICS)
				         vc_cons[i].d->vc_sw->con_set_palette(vc_cons[i].d, color_table);
			}
		}
	}
}

    /*
     *  Initialisation
     */
static int __init sun3fb_init_fb(int fbtype, unsigned long addr)
{
	static struct sbus_dev sdb;
	struct fb_fix_screeninfo *fix;
	struct fb_var_screeninfo *var;
	struct display *disp;
	struct fb_info_sbusfb *fb;
	struct fbtype *type;
	int linebytes, w, h, depth;
	char *p = NULL;
	
	fb = kmalloc(sizeof(struct fb_info_sbusfb), GFP_ATOMIC);
	if (!fb)
		return -ENOMEM;
	
	memset(fb, 0, sizeof(struct fb_info_sbusfb));
	fix = &fb->fix;
	var = &fb->var;
	disp = &fb->disp;
	type = &fb->type;
	
	sdb.reg_addrs[0].phys_addr = addr;
	fb->sbdp = &sdb;

	type->fb_type = fbtype;
	
	type->fb_height = h = 900;
	type->fb_width  = w = 1152;
sizechange:
	type->fb_depth  = depth = (fbtype == FBTYPE_SUN2BW) ? 1 : 8;
	linebytes = w * depth / 8;
	type->fb_size   = PAGE_ALIGN((linebytes) * h);
/*	
	fb->x_margin = (w & 7) / 2;
	fb->y_margin = (h & 15) / 2;
*/
	fb->x_margin = fb->y_margin = 0;

	var->xres_virtual = w;
	var->yres_virtual = h;
	var->xres = w - 2*fb->x_margin;
	var->yres = h - 2*fb->y_margin;
	
	var->bits_per_pixel = depth;
	var->height = var->width = -1;
	var->pixclock = 10000;
	var->vmode = FB_VMODE_NONINTERLACED;
	var->red.length = var->green.length = var->blue.length = 8;

	fix->line_length = linebytes;
	fix->smem_len = type->fb_size;
	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->visual = FB_VISUAL_PSEUDOCOLOR;
	
	fb->info.fbops = &sun3fb_ops;
	fb->info.disp = disp;
	fb->info.currcon = -1;
	strcpy(fb->info.fontname, fontname);
	fb->info.changevar = NULL;
	fb->info.switch_con = &sun3fbcon_switch;
	fb->info.updatevar = &sun3fbcon_updatevar;
	fb->info.flags = FBINFO_FLAG_DEFAULT;
	
	fb->cursor.hwsize.fbx = 32;
	fb->cursor.hwsize.fby = 32;
	
	if (depth > 1 && !fb->color_map) {
		if((fb->color_map = kmalloc(256 * 3, GFP_ATOMIC))==NULL)
			return -ENOMEM;
	}
			
	switch(fbtype) {
#ifdef CONFIG_FB_CGSIX
	case FBTYPE_SUNFAST_COLOR:
		p = cgsixfb_init(fb); break;
#endif
#ifdef CONFIG_FB_BWTWO
	case FBTYPE_SUN2BW:
		p = bwtwofb_init(fb); break;
#endif
#ifdef CONFIG_FB_CGTHREE
	case FBTYPE_SUN4COLOR:
	case FBTYPE_SUN3COLOR:
		type->fb_size = 0x100000;
		p = cgthreefb_init(fb); break;
#endif
	}
	fix->smem_start = (unsigned long)fb->info.screen_base;	// FIXME
	
	if (!p) {
		kfree(fb);
		return -ENODEV;
	}
	
	if (p == SBUSFBINIT_SIZECHANGE)
		goto sizechange;

	disp->dispsw = &fb->dispsw;
	if (fb->setcursor) {
		fb->dispsw.cursor = sun3fb_cursor;
		if (curblink) {
			fb->cursor.blink_rate = DEFAULT_CURSOR_BLINK_RATE;
			init_timer(&fb->cursor.timer);
			fb->cursor.timer.expires = jiffies + fb->cursor.blink_rate;
			fb->cursor.timer.data = (unsigned long)fb;
			fb->cursor.timer.function = sun3fb_cursor_timer_handler;
			add_timer(&fb->cursor.timer);
		}
	}
	fb->cursor.mode = CURSOR_SHAPE;
	fb->dispsw.set_font = sun3fb_set_font;
	fb->setup = fb->dispsw.setup;
	fb->dispsw.setup = sun3fb_disp_setup;
	fb->dispsw.clear_margins = NULL;

	disp->var = *var;
	disp->visual = fix->visual;
	disp->type = fix->type;
	disp->type_aux = fix->type_aux;
	disp->line_length = fix->line_length;
	
	if (fb->blank)
		disp->can_soft_blank = 1;

	sun3fb_set_var(var, -1, &fb->info);

	if (register_framebuffer(&fb->info) < 0) {
		kfree(fb);
		return -EINVAL;
	}
	printk("fb%d: %s\n", fb->info.node, p);

	return 0;
}


int __init sun3fb_init(void)
{
	extern int con_is_present(void);
	unsigned long addr;
	char p4id;
	
	if (!con_is_present()) return -ENODEV;
#ifdef CONFIG_SUN3
        switch(*(romvec->pv_fbtype))
        {
	case FBTYPE_SUN2BW:
		addr = 0xfe20000;
		return sun3fb_init_fb(FBTYPE_SUN2BW, addr);
	case FBTYPE_SUN3COLOR:
	case FBTYPE_SUN4COLOR:
		if(idprom->id_machtype != (SM_SUN3|SM_3_60)) {
			printk("sun3fb: cgthree/four only supported on 3/60\n");
			return -ENODEV;
		}
		
		addr = CGFOUR_OBMEM_ADDR;
		return sun3fb_init_fb(*(romvec->pv_fbtype), addr);
	default:
		printk("sun3fb: unsupported framebuffer\n");
		return -ENODEV;
	}
#else
	addr = SUN3X_VIDEO_BASE;
	p4id = *(char *)SUN3X_VIDEO_P4ID;

	p4id = (p4id == 0x45) ? p4id : (p4id & 0xf0);
	switch (p4id) {
		case 0x00:
			return sun3fb_init_fb(FBTYPE_SUN2BW, addr);
#if 0 /* not yet */
		case 0x40:
			return sun3fb_init_fb(FBTYPE_SUN4COLOR, addr);
			break;
		case 0x45:
			return sun3fb_init_fb(FBTYPE_SUN8COLOR, addr);
			break;
#endif
		case 0x60:
			return sun3fb_init_fb(FBTYPE_SUNFAST_COLOR, addr);
	}
#endif			
	
	return -ENODEV;
}

MODULE_LICENSE("GPL");
