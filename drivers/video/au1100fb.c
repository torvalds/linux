/*
 * BRIEF MODULE DESCRIPTION
 *	Au1100 LCD Driver.
 *
 * Rewritten for 2.6 by Embedded Alley Solutions
 * 	<source@embeddedalley.com>, based on submissions by
 *  	Karl Lessard <klessard@sunrisetelecom.com>
 *  	<c.pellegrin@exadron.com>
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
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ctype.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>

#include <asm/mach-au1x00/au1000.h>

#define DEBUG 0

#include "au1100fb.h"

/*
 * Sanity check. If this is a new Au1100 based board, search for
 * the PB1100 ifdefs to make sure you modify the code accordingly.
 */
#if defined(CONFIG_MIPS_PB1100)
  #include <asm/mach-pb1x00/pb1100.h>
#elif defined(CONFIG_MIPS_DB1100)
  #include <asm/mach-db1x00/db1x00.h>
#else
  #error "Unknown Au1100 board, Au1100 FB driver not supported"
#endif

#define DRIVER_NAME "au1100fb"
#define DRIVER_DESC "LCD controller driver for AU1100 processors"

#define to_au1100fb_device(_info) \
	  (_info ? container_of(_info, struct au1100fb_device, info) : NULL);

/* Bitfields format supported by the controller. Note that the order of formats
 * SHOULD be the same as in the LCD_CONTROL_SBPPF field, so we can retrieve the
 * right pixel format by doing rgb_bitfields[LCD_CONTROL_SBPPF_XXX >> LCD_CONTROL_SBPPF]
 */
struct fb_bitfield rgb_bitfields[][4] =
{
  	/*     Red, 	   Green, 	 Blue, 	     Transp   */
	{ { 10, 6, 0 }, { 5, 5, 0 }, { 0, 5, 0 }, { 0, 0, 0 } },
	{ { 11, 5, 0 }, { 5, 6, 0 }, { 0, 5, 0 }, { 0, 0, 0 } },
	{ { 11, 5, 0 }, { 6, 5, 0 }, { 0, 6, 0 }, { 0, 0, 0 } },
	{ { 10, 5, 0 }, { 5, 5, 0 }, { 0, 5, 0 }, { 15, 1, 0 } },
	{ { 11, 5, 0 }, { 6, 5, 0 }, { 1, 5, 0 }, { 0, 1, 0 } },

	/* The last is used to describe 12bpp format */
	{ { 8, 4, 0 },  { 4, 4, 0 }, { 0, 4, 0 }, { 0, 0, 0 } },
};

static struct fb_fix_screeninfo au1100fb_fix __initdata = {
	.id		= "AU1100 FB",
	.xpanstep 	= 1,
	.ypanstep 	= 1,
	.type		= FB_TYPE_PACKED_PIXELS,
	.accel		= FB_ACCEL_NONE,
};

static struct fb_var_screeninfo au1100fb_var __initdata = {
	.activate	= FB_ACTIVATE_NOW,
	.height		= -1,
	.width		= -1,
	.vmode		= FB_VMODE_NONINTERLACED,
};

static struct au1100fb_drv_info drv_info;

/*
 * Set hardware with var settings. This will enable the controller with a specific
 * mode, normally validated with the fb_check_var method
	 */
int au1100fb_setmode(struct au1100fb_device *fbdev)
{
	struct fb_info *info = &fbdev->info;
	u32 words;
	int index;

	if (!fbdev)
		return -EINVAL;

	/* Update var-dependent FB info */
	if (panel_is_active(fbdev->panel) || panel_is_color(fbdev->panel)) {
		if (info->var.bits_per_pixel <= 8) {
			/* palettized */
			info->var.red.offset    = 0;
			info->var.red.length    = info->var.bits_per_pixel;
			info->var.red.msb_right = 0;

			info->var.green.offset  = 0;
			info->var.green.length  = info->var.bits_per_pixel;
			info->var.green.msb_right = 0;

			info->var.blue.offset   = 0;
			info->var.blue.length   = info->var.bits_per_pixel;
			info->var.blue.msb_right = 0;

			info->var.transp.offset = 0;
			info->var.transp.length = 0;
			info->var.transp.msb_right = 0;

			info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
			info->fix.line_length = info->var.xres_virtual /
							(8/info->var.bits_per_pixel);
		} else {
			/* non-palettized */
			index = (fbdev->panel->control_base & LCD_CONTROL_SBPPF_MASK) >> LCD_CONTROL_SBPPF_BIT;
			info->var.red = rgb_bitfields[index][0];
			info->var.green = rgb_bitfields[index][1];
			info->var.blue = rgb_bitfields[index][2];
			info->var.transp = rgb_bitfields[index][3];

			info->fix.visual = FB_VISUAL_TRUECOLOR;
			info->fix.line_length = info->var.xres_virtual << 1; /* depth=16 */
	}
	} else {
		/* mono */
		info->fix.visual = FB_VISUAL_MONO10;
		info->fix.line_length = info->var.xres_virtual / 8;
	}

	info->screen_size = info->fix.line_length * info->var.yres_virtual;

	/* Determine BPP mode and format */
	fbdev->regs->lcd_control = fbdev->panel->control_base |
			    ((info->var.rotate/90) << LCD_CONTROL_SM_BIT);

	fbdev->regs->lcd_intenable = 0;
	fbdev->regs->lcd_intstatus = 0;

	fbdev->regs->lcd_horztiming = fbdev->panel->horztiming;

	fbdev->regs->lcd_verttiming = fbdev->panel->verttiming;

	fbdev->regs->lcd_clkcontrol = fbdev->panel->clkcontrol_base;

	fbdev->regs->lcd_dmaaddr0 = LCD_DMA_SA_N(fbdev->fb_phys);

	if (panel_is_dual(fbdev->panel)) {
		/* Second panel display seconf half of screen if possible,
		 * otherwise display the same as the first panel */
		if (info->var.yres_virtual >= (info->var.yres << 1)) {
			fbdev->regs->lcd_dmaaddr1 = LCD_DMA_SA_N(fbdev->fb_phys +
							  (info->fix.line_length *
						          (info->var.yres_virtual >> 1)));
		} else {
			fbdev->regs->lcd_dmaaddr1 = LCD_DMA_SA_N(fbdev->fb_phys);
		}
	}

	words = info->fix.line_length / sizeof(u32);
	if (!info->var.rotate || (info->var.rotate == 180)) {
		words *= info->var.yres_virtual;
		if (info->var.rotate /* 180 */) {
			words -= (words % 8); /* should be divisable by 8 */
		}
	}
	fbdev->regs->lcd_words = LCD_WRD_WRDS_N(words);

	fbdev->regs->lcd_pwmdiv = 0;
	fbdev->regs->lcd_pwmhi = 0;

	/* Resume controller */
	fbdev->regs->lcd_control |= LCD_CONTROL_GO;

	return 0;
}

/* fb_setcolreg
 * Set color in LCD palette.
 */
int au1100fb_fb_setcolreg(unsigned regno, unsigned red, unsigned green, unsigned blue, unsigned transp, struct fb_info *fbi)
{
	struct au1100fb_device *fbdev = to_au1100fb_device(fbi);
	u32 *palette = fbdev->regs->lcd_pallettebase;
	u32 value;

	if (regno > (AU1100_LCD_NBR_PALETTE_ENTRIES - 1))
		return -EINVAL;

	if (fbi->var.grayscale) {
		/* Convert color to grayscale */
		red = green = blue =
			(19595 * red + 38470 * green + 7471 * blue) >> 16;
	}

	if (fbi->fix.visual == FB_VISUAL_TRUECOLOR) {
		/* Place color in the pseudopalette */
		if (regno > 16)
			return -EINVAL;

		palette = (u32*)fbi->pseudo_palette;

		red   >>= (16 - fbi->var.red.length);
		green >>= (16 - fbi->var.green.length);
		blue  >>= (16 - fbi->var.blue.length);

		value = (red   << fbi->var.red.offset) 	|
			(green << fbi->var.green.offset)|
			(blue  << fbi->var.blue.offset);
		value &= 0xFFFF;

	} else if (panel_is_active(fbdev->panel)) {
		/* COLOR TFT PALLETTIZED (use RGB 565) */
		value = (red & 0xF800)|((green >> 5) & 0x07E0)|((blue >> 11) & 0x001F);
		value &= 0xFFFF;

	} else if (panel_is_color(fbdev->panel)) {
		/* COLOR STN MODE */
		value = (((panel_swap_rgb(fbdev->panel) ? blue : red) >> 12) & 0x000F) |
			((green >> 8) & 0x00F0) |
			(((panel_swap_rgb(fbdev->panel) ? red : blue) >> 4) & 0x0F00);
		value &= 0xFFF;
	} else {
		/* MONOCHROME MODE */
		value = (green >> 12) & 0x000F;
		value &= 0xF;
	}

	palette[regno] = value;

	return 0;
}

/* fb_blank
 * Blank the screen. Depending on the mode, the screen will be
 * activated with the backlight color, or desactivated
 */
int au1100fb_fb_blank(int blank_mode, struct fb_info *fbi)
{
	struct au1100fb_device *fbdev = to_au1100fb_device(fbi);

	print_dbg("fb_blank %d %p", blank_mode, fbi);

	switch (blank_mode) {

	case VESA_NO_BLANKING:
			/* Turn on panel */
			fbdev->regs->lcd_control |= LCD_CONTROL_GO;
#ifdef CONFIG_MIPS_PB1100
			if (drv_info.panel_idx == 1) {
				au_writew(au_readw(PB1100_G_CONTROL)
					  | (PB1100_G_CONTROL_BL | PB1100_G_CONTROL_VDD),
			PB1100_G_CONTROL);
			}
#endif
		au_sync();
		break;

	case VESA_VSYNC_SUSPEND:
	case VESA_HSYNC_SUSPEND:
	case VESA_POWERDOWN:
			/* Turn off panel */
			fbdev->regs->lcd_control &= ~LCD_CONTROL_GO;
#ifdef CONFIG_MIPS_PB1100
			if (drv_info.panel_idx == 1) {
				au_writew(au_readw(PB1100_G_CONTROL)
				  	  & ~(PB1100_G_CONTROL_BL | PB1100_G_CONTROL_VDD),
			PB1100_G_CONTROL);
			}
#endif
		au_sync();
		break;
	default:
		break;

	}
	return 0;
}

/* fb_pan_display
 * Pan display in x and/or y as specified
 */
int au1100fb_fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *fbi)
{
	struct au1100fb_device *fbdev = to_au1100fb_device(fbi);
	int dy;

	print_dbg("fb_pan_display %p %p", var, fbi);

	if (!var || !fbdev) {
		return -EINVAL;
	}

	if (var->xoffset - fbi->var.xoffset) {
		/* No support for X panning for now! */
		return -EINVAL;
	}

	print_dbg("fb_pan_display 2 %p %p", var, fbi);
	dy = var->yoffset - fbi->var.yoffset;
	if (dy) {

		u32 dmaaddr;

		print_dbg("Panning screen of %d lines", dy);

		dmaaddr = fbdev->regs->lcd_dmaaddr0;
		dmaaddr += (fbi->fix.line_length * dy);

		/* TODO: Wait for current frame to finished */
		fbdev->regs->lcd_dmaaddr0 = LCD_DMA_SA_N(dmaaddr);

		if (panel_is_dual(fbdev->panel)) {
			dmaaddr = fbdev->regs->lcd_dmaaddr1;
			dmaaddr += (fbi->fix.line_length * dy);
			fbdev->regs->lcd_dmaaddr0 = LCD_DMA_SA_N(dmaaddr);
	}
	}
	print_dbg("fb_pan_display 3 %p %p", var, fbi);

	return 0;
}

/* fb_rotate
 * Rotate the display of this angle. This doesn't seems to be used by the core,
 * but as our hardware supports it, so why not implementing it...
 */
void au1100fb_fb_rotate(struct fb_info *fbi, int angle)
{
	struct au1100fb_device *fbdev = to_au1100fb_device(fbi);

	print_dbg("fb_rotate %p %d", fbi, angle);

	if (fbdev && (angle > 0) && !(angle % 90)) {

		fbdev->regs->lcd_control &= ~LCD_CONTROL_GO;

		fbdev->regs->lcd_control &= ~(LCD_CONTROL_SM_MASK);
		fbdev->regs->lcd_control |= ((angle/90) << LCD_CONTROL_SM_BIT);

		fbdev->regs->lcd_control |= LCD_CONTROL_GO;
	}
}

/* fb_mmap
 * Map video memory in user space. We don't use the generic fb_mmap method mainly
 * to allow the use of the TLB streaming flag (CCA=6)
 */
int au1100fb_fb_mmap(struct fb_info *fbi, struct vm_area_struct *vma)
{
	struct au1100fb_device *fbdev = to_au1100fb_device(fbi);
	unsigned int len;
	unsigned long start=0, off;

	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT)) {
		return -EINVAL;
	}

	start = fbdev->fb_phys & PAGE_MASK;
	len = PAGE_ALIGN((start & ~PAGE_MASK) + fbdev->fb_len);

	off = vma->vm_pgoff << PAGE_SHIFT;

	if ((vma->vm_end - vma->vm_start + off) > len) {
		return -EINVAL;
	}

	off += start;
	vma->vm_pgoff = off >> PAGE_SHIFT;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	pgprot_val(vma->vm_page_prot) |= (6 << 9); //CCA=6

	vma->vm_flags |= VM_IO;

	if (io_remap_pfn_range(vma, vma->vm_start, off >> PAGE_SHIFT,
				vma->vm_end - vma->vm_start,
				vma->vm_page_prot)) {
		return -EAGAIN;
	}

	return 0;
}

static struct fb_ops au1100fb_ops =
{
	.owner			= THIS_MODULE,
	.fb_setcolreg		= au1100fb_fb_setcolreg,
	.fb_blank		= au1100fb_fb_blank,
	.fb_pan_display		= au1100fb_fb_pan_display,
	.fb_fillrect		= cfb_fillrect,
	.fb_copyarea		= cfb_copyarea,
	.fb_imageblit		= cfb_imageblit,
	.fb_rotate		= au1100fb_fb_rotate,
	.fb_mmap		= au1100fb_fb_mmap,
};


/*-------------------------------------------------------------------------*/

/* AU1100 LCD controller device driver */

int au1100fb_drv_probe(struct device *dev)
{
	struct au1100fb_device *fbdev = NULL;
	struct resource *regs_res;
	unsigned long page;
	u32 sys_clksrc;

	if (!dev)
			return -EINVAL;

	/* Allocate new device private */
	if (!(fbdev = kmalloc(sizeof(struct au1100fb_device), GFP_KERNEL))) {
		print_err("fail to allocate device private record");
		return -ENOMEM;
	}
	memset((void*)fbdev, 0, sizeof(struct au1100fb_device));

	fbdev->panel = &known_lcd_panels[drv_info.panel_idx];

	dev_set_drvdata(dev, (void*)fbdev);

	/* Allocate region for our registers and map them */
	if (!(regs_res = platform_get_resource(to_platform_device(dev),
					IORESOURCE_MEM, 0))) {
		print_err("fail to retrieve registers resource");
		return -EFAULT;
	}

	au1100fb_fix.mmio_start = regs_res->start;
	au1100fb_fix.mmio_len = regs_res->end - regs_res->start + 1;

	if (!request_mem_region(au1100fb_fix.mmio_start, au1100fb_fix.mmio_len,
				DRIVER_NAME)) {
		print_err("fail to lock memory region at 0x%08x",
				au1100fb_fix.mmio_start);
		return -EBUSY;
	}

	fbdev->regs = (struct au1100fb_regs*)KSEG1ADDR(au1100fb_fix.mmio_start);

	print_dbg("Register memory map at %p", fbdev->regs);
	print_dbg("phys=0x%08x, size=%d", fbdev->regs_phys, fbdev->regs_len);



	/* Allocate the framebuffer to the maximum screen size * nbr of video buffers */
	fbdev->fb_len = fbdev->panel->xres * fbdev->panel->yres *
		  	(fbdev->panel->bpp >> 3) * AU1100FB_NBR_VIDEO_BUFFERS;

	fbdev->fb_mem = dma_alloc_coherent(dev, PAGE_ALIGN(fbdev->fb_len),
					&fbdev->fb_phys, GFP_KERNEL);
	if (!fbdev->fb_mem) {
		print_err("fail to allocate frambuffer (size: %dK))",
			  fbdev->fb_len / 1024);
		return -ENOMEM;
	}

	au1100fb_fix.smem_start = fbdev->fb_phys;
	au1100fb_fix.smem_len = fbdev->fb_len;

	/*
	 * Set page reserved so that mmap will work. This is necessary
	 * since we'll be remapping normal memory.
	 */
	for (page = (unsigned long)fbdev->fb_mem;
	     page < PAGE_ALIGN((unsigned long)fbdev->fb_mem + fbdev->fb_len);
	     page += PAGE_SIZE) {
#if CONFIG_DMA_NONCOHERENT
		SetPageReserved(virt_to_page(CAC_ADDR(page)));
#else
		SetPageReserved(virt_to_page(page));
#endif
	}

	print_dbg("Framebuffer memory map at %p", fbdev->fb_mem);
	print_dbg("phys=0x%08x, size=%dK", fbdev->fb_phys, fbdev->fb_len / 1024);

	/* Setup LCD clock to AUX (48 MHz) */
	sys_clksrc = au_readl(SYS_CLKSRC) & ~(SYS_CS_ML_MASK | SYS_CS_DL | SYS_CS_CL);
	au_writel((sys_clksrc | (1 << SYS_CS_ML_BIT)), SYS_CLKSRC);

	/* load the panel info into the var struct */
	au1100fb_var.bits_per_pixel = fbdev->panel->bpp;
	au1100fb_var.xres = fbdev->panel->xres;
	au1100fb_var.xres_virtual = au1100fb_var.xres;
	au1100fb_var.yres = fbdev->panel->yres;
	au1100fb_var.yres_virtual = au1100fb_var.yres;

	fbdev->info.screen_base = fbdev->fb_mem;
	fbdev->info.fbops = &au1100fb_ops;
	fbdev->info.fix = au1100fb_fix;

	if (!(fbdev->info.pseudo_palette = kmalloc(sizeof(u32) * 16, GFP_KERNEL))) {
		return -ENOMEM;
	}
	memset(fbdev->info.pseudo_palette, 0, sizeof(u32) * 16);

	if (fb_alloc_cmap(&fbdev->info.cmap, AU1100_LCD_NBR_PALETTE_ENTRIES, 0) < 0) {
		print_err("Fail to allocate colormap (%d entries)",
			   AU1100_LCD_NBR_PALETTE_ENTRIES);
		kfree(fbdev->info.pseudo_palette);
		return -EFAULT;
	}

	fbdev->info.var = au1100fb_var;

	/* Set h/w registers */
	au1100fb_setmode(fbdev);

	/* Register new framebuffer */
	if (register_framebuffer(&fbdev->info) < 0) {
		print_err("cannot register new framebuffer");
		goto failed;
	}

	return 0;

failed:
	if (fbdev->regs) {
		release_mem_region(fbdev->regs_phys, fbdev->regs_len);
	}
	if (fbdev->fb_mem) {
		dma_free_noncoherent(dev, fbdev->fb_len, fbdev->fb_mem, fbdev->fb_phys);
	}
	if (fbdev->info.cmap.len != 0) {
		fb_dealloc_cmap(&fbdev->info.cmap);
	}
	kfree(fbdev);
	dev_set_drvdata(dev, NULL);

	return 0;
}

int au1100fb_drv_remove(struct device *dev)
{
	struct au1100fb_device *fbdev = NULL;

	if (!dev)
		return -ENODEV;

	fbdev = (struct au1100fb_device*) dev_get_drvdata(dev);

#if !defined(CONFIG_FRAMEBUFFER_CONSOLE) && defined(CONFIG_LOGO)
	au1100fb_fb_blank(VESA_POWERDOWN, &fbdev->info);
#endif
	fbdev->regs->lcd_control &= ~LCD_CONTROL_GO;

	/* Clean up all probe data */
	unregister_framebuffer(&fbdev->info);

	release_mem_region(fbdev->regs_phys, fbdev->regs_len);

	dma_free_coherent(dev, PAGE_ALIGN(fbdev->fb_len), fbdev->fb_mem, fbdev->fb_phys);

	fb_dealloc_cmap(&fbdev->info.cmap);
	kfree(fbdev->info.pseudo_palette);
	kfree((void*)fbdev);

	return 0;
}

int au1100fb_drv_suspend(struct device *dev, u32 state, u32 level)
{
	/* TODO */
	return 0;
}

int au1100fb_drv_resume(struct device *dev, u32 level)
{
	/* TODO */
	return 0;
}

static struct device_driver au1100fb_driver = {
	.name		= "au1100-lcd",
	.bus		= &platform_bus_type,

	.probe		= au1100fb_drv_probe,
        .remove		= au1100fb_drv_remove,
	.suspend	= au1100fb_drv_suspend,
        .resume		= au1100fb_drv_resume,
};

/*-------------------------------------------------------------------------*/

/* Kernel driver */

int au1100fb_setup(char *options)
{
	char* this_opt;
	int num_panels = ARRAY_SIZE(known_lcd_panels);
	char* mode = NULL;
	int panel_idx = 0;

	if (num_panels <= 0) {
		print_err("No LCD panels supported by driver!");
		return -EFAULT;
			}

	if (options) {
		while ((this_opt = strsep(&options,",")) != NULL) {
			/* Panel option */
		if (!strncmp(this_opt, "panel:", 6)) {
				int i;
				this_opt += 6;
				for (i = 0; i < num_panels; i++) {
					if (!strncmp(this_opt,
					      	     known_lcd_panels[i].name,
							strlen(this_opt))) {
						panel_idx = i;
					break;
				}
			}
				if (i >= num_panels) {
 					print_warn("Panel %s not supported!", this_opt);
				}
			}
			/* Mode option (only option that start with digit) */
			else if (isdigit(this_opt[0])) {
				mode = kmalloc(strlen(this_opt) + 1, GFP_KERNEL);
				strncpy(mode, this_opt, strlen(this_opt) + 1);
			}
			/* Unsupported option */
			else {
				print_warn("Unsupported option \"%s\"", this_opt);
		}
		}
	}

	drv_info.panel_idx = panel_idx;
	drv_info.opt_mode = mode;

	print_info("Panel=%s Mode=%s",
			known_lcd_panels[drv_info.panel_idx].name,
		      	drv_info.opt_mode ? drv_info.opt_mode : "default");

	return 0;
}

int __init au1100fb_init(void)
{
	char* options;
	int ret;

	print_info("" DRIVER_DESC "");

	memset(&drv_info, 0, sizeof(drv_info));

	if (fb_get_options(DRIVER_NAME, &options))
		return -ENODEV;

	/* Setup driver with options */
	ret = au1100fb_setup(options);
	if (ret < 0) {
		print_err("Fail to setup driver");
		return ret;
	}

	return driver_register(&au1100fb_driver);
}

void __exit au1100fb_cleanup(void)
{
	driver_unregister(&au1100fb_driver);

	if (drv_info.opt_mode)
		kfree(drv_info.opt_mode);
}

module_init(au1100fb_init);
module_exit(au1100fb_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
