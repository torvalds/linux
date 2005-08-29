/*
 * BRIEF MODULE DESCRIPTION
 *	Au1100 LCD Driver.
 *
 * Copyright 2002 MontaVista Software
 * Author: MontaVista Software, Inc.
 *		ppopov@mvista.com or source@mvista.com
 *
 * Copyright 2002 Alchemy Semiconductor
 * Author: Alchemy Semiconductor
 *
 * Based on:
 * linux/drivers/video/skeletonfb.c -- Skeleton for a frame buffer device
 *  Created 28 Dec 1997 by Geert Uytterhoeven
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED	  ``AS	IS'' AND   ANY	EXPRESS OR IMPLIED
 *  WARRANTIES,	  INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO	EVENT  SHALL   THE AUTHOR  BE	 LIABLE FOR ANY	  DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED	  TO, PROCUREMENT OF  SUBSTITUTE GOODS	OR SERVICES; LOSS OF
 *  USE, DATA,	OR PROFITS; OR	BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN	 CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/au1000.h>
#include <asm/pb1100.h>
#include "au1100fb.h"

#include <video/fbcon.h>
#include <video/fbcon-mfb.h>
#include <video/fbcon-cfb2.h>
#include <video/fbcon-cfb4.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>

/*
 * Sanity check. If this is a new Au1100 based board, search for
 * the PB1100 ifdefs to make sure you modify the code accordingly.
 */
#if defined(CONFIG_MIPS_PB1100) || defined(CONFIG_MIPS_DB1100) || defined(CONFIG_MIPS_HYDROGEN3)
#else
error Unknown Au1100 board
#endif

#define CMAPSIZE 16

static int my_lcd_index; /* default is zero */
struct known_lcd_panels *p_lcd;
AU1100_LCD *p_lcd_reg = (AU1100_LCD *)AU1100_LCD_ADDR;

struct au1100fb_info {
	struct fb_info_gen gen;
	unsigned long fb_virt_start;
	unsigned long fb_size;
	unsigned long fb_phys;
	int mmaped;
	int nohwcursor;

	struct { unsigned red, green, blue, pad; } palette[256];

#if defined(FBCON_HAS_CFB16)
	u16 fbcon_cmap16[16];
#endif
};


struct au1100fb_par {
        struct fb_var_screeninfo var;

	int line_length;  // in bytes
	int cmap_len;     // color-map length
};


static struct au1100fb_info fb_info;
static struct au1100fb_par current_par;
static struct display disp;

int au1100fb_init(void);
void au1100fb_setup(char *options, int *ints);
static int au1100fb_mmap(struct fb_info *fb, struct file *file,
		struct vm_area_struct *vma);
static int au1100_blank(int blank_mode, struct fb_info_gen *info);
static int au1100fb_ioctl(struct inode *inode, struct file *file, u_int cmd,
			  u_long arg, int con, struct fb_info *info);

void au1100_nocursor(struct display *p, int mode, int xx, int yy){};

static struct fb_ops au1100fb_ops = {
	.owner		= THIS_MODULE,
	.fb_get_fix	= fbgen_get_fix,
	.fb_get_var	= fbgen_get_var,
	.fb_set_var	= fbgen_set_var,
	.fb_get_cmap	= fbgen_get_cmap,
	.fb_set_cmap	= fbgen_set_cmap,
	.fb_pan_display	= fbgen_pan_display,
        .fb_ioctl	= au1100fb_ioctl,
	.fb_mmap	= au1100fb_mmap,
};

static void au1100_detect(void)
{
	/*
	 *  This function should detect the current video mode settings
	 *  and store it as the default video mode
	 */

	/*
	 * Yeh, well, we're not going to change any settings so we're
	 * always stuck with the default ...
	 */

}

static int au1100_encode_fix(struct fb_fix_screeninfo *fix,
		const void *_par, struct fb_info_gen *_info)
{
        struct au1100fb_info *info = (struct au1100fb_info *) _info;
        struct au1100fb_par *par = (struct au1100fb_par *) _par;
	struct fb_var_screeninfo *var = &par->var;

	memset(fix, 0, sizeof(struct fb_fix_screeninfo));

	fix->smem_start = info->fb_phys;
	fix->smem_len = info->fb_size;
	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->type_aux = 0;
        fix->visual = (var->bits_per_pixel == 8) ?
	       	FB_VISUAL_PSEUDOCOLOR	: FB_VISUAL_TRUECOLOR;
	fix->ywrapstep = 0;
	fix->xpanstep = 1;
	fix->ypanstep = 1;
	fix->line_length = current_par.line_length;
	return 0;
}

static void set_color_bitfields(struct fb_var_screeninfo *var)
{
	switch (var->bits_per_pixel) {
	case 8:
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 16:	/* RGB 565 */
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	}

	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;
}

static int au1100_decode_var(const struct fb_var_screeninfo *var,
		void *_par, struct fb_info_gen *_info)
{

	struct au1100fb_par *par = (struct au1100fb_par *)_par;

	/*
	 * Don't allow setting any of these yet: xres and yres don't
	 * make sense for LCD panels.
	 */
	if (var->xres != p_lcd->xres ||
	    var->yres != p_lcd->yres ||
	    var->xres != p_lcd->xres ||
	    var->yres != p_lcd->yres) {
		return -EINVAL;
	}
	if(var->bits_per_pixel != p_lcd->bpp) {
		return -EINVAL;
	}

	memset(par, 0, sizeof(struct au1100fb_par));
	par->var = *var;

	/* FIXME */
	switch (var->bits_per_pixel) {
		case 8:
			par->var.bits_per_pixel = 8;
			break;
		case 16:
			par->var.bits_per_pixel = 16;
			break;
		default:
			printk("color depth %d bpp not supported\n",
					var->bits_per_pixel);
			return -EINVAL;

	}
	set_color_bitfields(&par->var);
	par->cmap_len = (par->var.bits_per_pixel == 8) ? 256 : 16;
	return 0;
}

static int au1100_encode_var(struct fb_var_screeninfo *var,
		const void *par, struct fb_info_gen *_info)
{

	*var = ((struct au1100fb_par *)par)->var;
	return 0;
}

static void
au1100_get_par(void *_par, struct fb_info_gen *_info)
{
	*(struct au1100fb_par *)_par = current_par;
}

static void au1100_set_par(const void *par, struct fb_info_gen *info)
{
	/* nothing to do: we don't change any settings */
}

static int au1100_getcolreg(unsigned regno, unsigned *red, unsigned *green,
			 unsigned *blue, unsigned *transp,
			 struct fb_info *info)
{

	struct au1100fb_info* i = (struct au1100fb_info*)info;

	if (regno > 255)
		return 1;

	*red    = i->palette[regno].red;
	*green  = i->palette[regno].green;
	*blue   = i->palette[regno].blue;
	*transp = 0;

	return 0;
}

static int au1100_setcolreg(unsigned regno, unsigned red, unsigned green,
			 unsigned blue, unsigned transp,
			 struct fb_info *info)
{
	struct au1100fb_info* i = (struct au1100fb_info *)info;
	u32 rgbcol;

	if (regno > 255)
		return 1;

	i->palette[regno].red    = red;
	i->palette[regno].green  = green;
	i->palette[regno].blue   = blue;

	switch(p_lcd->bpp) {
#ifdef FBCON_HAS_CFB8
	case 8:
		red >>= 10;
		green >>= 10;
		blue >>= 10;
		p_lcd_reg->lcd_pallettebase[regno] = (blue&0x1f) |
			((green&0x3f)<<5) | ((red&0x1f)<<11);
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 16:
		i->fbcon_cmap16[regno] =
			((red & 0xf800) >> 0) |
			((green & 0xfc00) >> 5) |
			((blue & 0xf800) >> 11);
		break;
#endif
	default:
		break;
	}

	return 0;
}


static int  au1100_blank(int blank_mode, struct fb_info_gen *_info)
{

	switch (blank_mode) {
	case VESA_NO_BLANKING:
		/* turn on panel */
		//printk("turn on panel\n");
#ifdef CONFIG_MIPS_PB1100
		p_lcd_reg->lcd_control |= LCD_CONTROL_GO;
		au_writew(au_readw(PB1100_G_CONTROL) | p_lcd->mode_backlight,
			PB1100_G_CONTROL);
#endif
#ifdef CONFIG_MIPS_HYDROGEN3
		/*  Turn controller & power supply on,  GPIO213 */
		au_writel(0x20002000, 0xB1700008);
		au_writel(0x00040000, 0xB1900108);
		au_writel(0x01000100, 0xB1700008);
#endif
		au_sync();
		break;

	case VESA_VSYNC_SUSPEND:
	case VESA_HSYNC_SUSPEND:
	case VESA_POWERDOWN:
		/* turn off panel */
		//printk("turn off panel\n");
#ifdef CONFIG_MIPS_PB1100
		au_writew(au_readw(PB1100_G_CONTROL) & ~p_lcd->mode_backlight,
			PB1100_G_CONTROL);
		p_lcd_reg->lcd_control &= ~LCD_CONTROL_GO;
#endif
		au_sync();
		break;
	default:
		break;

	}
	return 0;
}

static void au1100_set_disp(const void *unused, struct display *disp,
			 struct fb_info_gen *info)
{
	disp->screen_base = (char *)fb_info.fb_virt_start;

	switch (disp->var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
	case 8:
		disp->dispsw = &fbcon_cfb8;
		if (fb_info.nohwcursor)
			fbcon_cfb8.cursor = au1100_nocursor;
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 16:
		disp->dispsw = &fbcon_cfb16;
		disp->dispsw_data = fb_info.fbcon_cmap16;
		if (fb_info.nohwcursor)
			fbcon_cfb16.cursor = au1100_nocursor;
		break;
#endif
	default:
		disp->dispsw = &fbcon_dummy;
		disp->dispsw_data = NULL;
		break;
	}
}

static int
au1100fb_mmap(struct fb_info *_fb,
	     struct file *file,
	     struct vm_area_struct *vma)
{
	unsigned int len;
	unsigned long start=0, off;
	struct au1100fb_info *fb = (struct au1100fb_info *)_fb;

	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT)) {
		return -EINVAL;
	}

	start = fb_info.fb_phys & PAGE_MASK;
	len = PAGE_ALIGN((start & ~PAGE_MASK) + fb_info.fb_size);

	off = vma->vm_pgoff << PAGE_SHIFT;

	if ((vma->vm_end - vma->vm_start + off) > len) {
		return -EINVAL;
	}

	off += start;
	vma->vm_pgoff = off >> PAGE_SHIFT;

	pgprot_val(vma->vm_page_prot) &= ~_CACHE_MASK;
	//pgprot_val(vma->vm_page_prot) |= _CACHE_CACHABLE_NONCOHERENT;
	pgprot_val(vma->vm_page_prot) |= (6 << 9); //CCA=6

	/* This is an IO map - tell maydump to skip this VMA */
	vma->vm_flags |= VM_IO;

	if (io_remap_pfn_range(vma, vma->vm_start, off >> PAGE_SHIFT,
				vma->vm_end - vma->vm_start,
				vma->vm_page_prot)) {
		return -EAGAIN;
	}

	fb->mmaped = 1;
	return 0;
}

int au1100_pan_display(const struct fb_var_screeninfo *var,
		       struct fb_info_gen *info)
{
	return 0;
}

static int au1100fb_ioctl(struct inode *inode, struct file *file, u_int cmd,
			  u_long arg, int con, struct fb_info *info)
{
	/* nothing to do yet */
	return -EINVAL;
}

static struct fbgen_hwswitch au1100_switch = {
	au1100_detect,
	au1100_encode_fix,
	au1100_decode_var,
	au1100_encode_var,
	au1100_get_par,
	au1100_set_par,
	au1100_getcolreg,
	au1100_setcolreg,
	au1100_pan_display,
	au1100_blank,
	au1100_set_disp
};


int au1100_setmode(void)
{
	int words;

	/* FIXME Need to accomodate for swivel mode and 12bpp, <8bpp*/
	switch (p_lcd->mode_control & LCD_CONTROL_SM)
	{
		case LCD_CONTROL_SM_0:
		case LCD_CONTROL_SM_180:
		words = (p_lcd->xres * p_lcd->yres * p_lcd->bpp) / 32;
			break;
		case LCD_CONTROL_SM_90:
		case LCD_CONTROL_SM_270:
			/* is this correct? */
		words = (p_lcd->xres * p_lcd->bpp) / 8;
			break;
		default:
			printk("mode_control reg not initialized\n");
			return -EINVAL;
	}

	/*
	 * Setup LCD controller
	 */

	p_lcd_reg->lcd_control = p_lcd->mode_control;
	p_lcd_reg->lcd_intstatus = 0;
	p_lcd_reg->lcd_intenable = 0;
	p_lcd_reg->lcd_horztiming = p_lcd->mode_horztiming;
	p_lcd_reg->lcd_verttiming = p_lcd->mode_verttiming;
	p_lcd_reg->lcd_clkcontrol = p_lcd->mode_clkcontrol;
	p_lcd_reg->lcd_words = words - 1;
	p_lcd_reg->lcd_dmaaddr0 = fb_info.fb_phys;

	/* turn on panel */
#ifdef CONFIG_MIPS_PB1100
	au_writew(au_readw(PB1100_G_CONTROL) | p_lcd->mode_backlight,
			PB1100_G_CONTROL);
#endif
#ifdef CONFIG_MIPS_HYDROGEN3
	/*  Turn controller & power supply on,  GPIO213 */
	au_writel(0x20002000, 0xB1700008);
	au_writel(0x00040000, 0xB1900108);
	au_writel(0x01000100, 0xB1700008);
#endif

	p_lcd_reg->lcd_control |= LCD_CONTROL_GO;

	return 0;
}


int __init au1100fb_init(void)
{
	uint32 sys_clksrc;
	unsigned long page;

	/*
	* Get the panel information/display mode and update the registry
	*/
	p_lcd = &panels[my_lcd_index];

	switch (p_lcd->mode_control & LCD_CONTROL_SM)
	{
		case LCD_CONTROL_SM_0:
		case LCD_CONTROL_SM_180:
		p_lcd->xres =
			(p_lcd->mode_horztiming & LCD_HORZTIMING_PPL) + 1;
		p_lcd->yres =
			(p_lcd->mode_verttiming & LCD_VERTTIMING_LPP) + 1;
			break;
		case LCD_CONTROL_SM_90:
		case LCD_CONTROL_SM_270:
		p_lcd->yres =
			(p_lcd->mode_horztiming & LCD_HORZTIMING_PPL) + 1;
		p_lcd->xres =
			(p_lcd->mode_verttiming & LCD_VERTTIMING_LPP) + 1;
			break;
	}

	/*
	 * Panel dimensions x bpp must be divisible by 32
	 */
	if (((p_lcd->yres * p_lcd->bpp) % 32) != 0)
		printk("VERT %% 32\n");
	if (((p_lcd->xres * p_lcd->bpp) % 32) != 0)
		printk("HORZ %% 32\n");

	/*
	 * Allocate LCD framebuffer from system memory
	 */
	fb_info.fb_size = (p_lcd->xres * p_lcd->yres * p_lcd->bpp) / 8;

	current_par.var.xres = p_lcd->xres;
	current_par.var.xres_virtual = p_lcd->xres;
	current_par.var.yres = p_lcd->yres;
	current_par.var.yres_virtual = p_lcd->yres;
	current_par.var.bits_per_pixel = p_lcd->bpp;

	/* FIX!!! only works for 8/16 bpp */
	current_par.line_length = p_lcd->xres * p_lcd->bpp / 8; /* in bytes */
	fb_info.fb_virt_start = (unsigned long )
		__get_free_pages(GFP_ATOMIC | GFP_DMA,
				get_order(fb_info.fb_size + 0x1000));
	if (!fb_info.fb_virt_start) {
		printk("Unable to allocate fb memory\n");
		return -ENOMEM;
	}
	fb_info.fb_phys = virt_to_bus((void *)fb_info.fb_virt_start);

	/*
	 * Set page reserved so that mmap will work. This is necessary
	 * since we'll be remapping normal memory.
	 */
	for (page = fb_info.fb_virt_start;
	     page < PAGE_ALIGN(fb_info.fb_virt_start + fb_info.fb_size);
	     page += PAGE_SIZE) {
		SetPageReserved(virt_to_page(page));
	}

	memset((void *)fb_info.fb_virt_start, 0, fb_info.fb_size);

	/* set freqctrl now to allow more time to stabilize */
	/* zero-out out LCD bits */
	sys_clksrc = au_readl(SYS_CLKSRC) & ~0x000003e0;
	sys_clksrc |= p_lcd->mode_toyclksrc;
	au_writel(sys_clksrc, SYS_CLKSRC);

	/* FIXME add check to make sure auxpll is what is expected! */
	au1100_setmode();

	fb_info.gen.parsize = sizeof(struct au1100fb_par);
	fb_info.gen.fbhw = &au1100_switch;

	strcpy(fb_info.gen.info.modename, "Au1100 LCD");
	fb_info.gen.info.changevar = NULL;
	fb_info.gen.info.node = -1;

	fb_info.gen.info.fbops = &au1100fb_ops;
	fb_info.gen.info.disp = &disp;
	fb_info.gen.info.switch_con = &fbgen_switch;
	fb_info.gen.info.updatevar = &fbgen_update_var;
	fb_info.gen.info.blank = &fbgen_blank;
	fb_info.gen.info.flags = FBINFO_FLAG_DEFAULT;

	/* This should give a reasonable default video mode */
	fbgen_get_var(&disp.var, -1, &fb_info.gen.info);
	fbgen_do_set_var(&disp.var, 1, &fb_info.gen);
	fbgen_set_disp(-1, &fb_info.gen);
	fbgen_install_cmap(0, &fb_info.gen);
	if (register_framebuffer(&fb_info.gen.info) < 0)
		return -EINVAL;
	printk(KERN_INFO "fb%d: %s frame buffer device\n",
			GET_FB_IDX(fb_info.gen.info.node),
			fb_info.gen.info.modename);

	return 0;
}


void au1100fb_cleanup(struct fb_info *info)
{
	unregister_framebuffer(info);
}


void au1100fb_setup(char *options, int *ints)
{
	char* this_opt;
	int i;
	int num_panels = sizeof(panels)/sizeof(struct known_lcd_panels);


	if (!options || !*options)
		return;

	for(this_opt=strtok(options, ","); this_opt;
	    this_opt=strtok(NULL, ",")) {
		if (!strncmp(this_opt, "panel:", 6)) {
#if defined(CONFIG_MIPS_PB1100) || defined(CONFIG_MIPS_DB1100)
			/* Read Pb1100 Switch S10 ? */
			if (!strncmp(this_opt+6, "s10", 3))
			{
				int panel;
				panel = *(volatile int *)0xAE000008; /* BCSR SWITCHES */
				panel >>= 8;
				panel &= 0x0F;
				if (panel >= num_panels) panel = 0;
				my_lcd_index = panel;
			}
			else
#endif
			/* Get the panel name, everything else if fixed */
			for (i=0; i<num_panels; i++) {
				if (!strncmp(this_opt+6, panels[i].panel_name,
							strlen(this_opt))) {
					my_lcd_index = i;
					break;
				}
			}
		}
		else if (!strncmp(this_opt, "nohwcursor", 10)) {
			printk("nohwcursor\n");
			fb_info.nohwcursor = 1;
		}
	}

	printk("au1100fb: Panel %d %s\n", my_lcd_index,
		panels[my_lcd_index].panel_name);
}



#ifdef MODULE
MODULE_LICENSE("GPL");
int init_module(void)
{
	return au1100fb_init();
}

void cleanup_module(void)
{
	au1100fb_cleanup(void);
}

MODULE_AUTHOR("Pete Popov <ppopov@mvista.com>");
MODULE_DESCRIPTION("Au1100 LCD framebuffer device driver");
#endif /* MODULE */
