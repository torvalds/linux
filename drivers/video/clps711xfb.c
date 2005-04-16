/*
 *  linux/drivers/video/clps711xfb.c
 *
 *  Copyright (C) 2000-2001 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Framebuffer driver for the CLPS7111 and EP7212 processors.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/uaccess.h>

#include <asm/hardware/clps7111.h>
#include <asm/arch/syspld.h>

struct fb_info	*cfb;

#define CMAP_MAX_SIZE	16

/* The /proc entry for the backlight. */
static struct proc_dir_entry *clps7111fb_backlight_proc_entry = NULL;

static int clps7111fb_proc_backlight_read(char *page, char **start, off_t off,
		int count, int *eof, void *data);
static int clps7111fb_proc_backlight_write(struct file *file, 
		const char *buffer, unsigned long count, void *data);

/*
 * LCD AC Prescale.  This comes from the LCD panel manufacturers specifications.
 * This determines how many clocks + 1 of CL1 before the M signal toggles.
 * The number of lines on the display must not be divisible by this number.
 */
static unsigned int lcd_ac_prescale = 13;

/*
 *    Set a single color register. Return != 0 for invalid regno.
 */
static int
clps7111fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
		     u_int transp, struct fb_info *info)
{
	unsigned int level, mask, shift, pal;

	if (regno >= (1 << info->var.bits_per_pixel))
		return 1;

	/* gray = 0.30*R + 0.58*G + 0.11*B */
	level = (red * 77 + green * 151 + blue * 28) >> 20;

	/*
	 * On an LCD, a high value is dark, while a low value is light. 
	 * So we invert the level.
	 *
	 * This isn't true on all machines, so we only do it on EDB7211.
	 *  --rmk
	 */
	if (machine_is_edb7211()) {
		level = 15 - level;
	}

	shift = 4 * (regno & 7);
	level <<= shift;
	mask  = 15 << shift;
	level &= mask;

	regno = regno < 8 ? PALLSW : PALMSW;

	pal = clps_readl(regno);
	pal = (pal & ~mask) | level;
	clps_writel(pal, regno);

	return 0;
}

/*
 * Validate the purposed mode.
 */	
static int
clps7111fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	var->transp.msb_right	= 0;
	var->transp.offset	= 0;
	var->transp.length	= 0;
	var->red.msb_right	= 0;
	var->red.offset		= 0;
	var->red.length		= var->bits_per_pixel;
	var->green		= var->red;
	var->blue		= var->red;

	if (var->bits_per_pixel > 4) 
		return -EINVAL;

	return 0;
}

/*
 * Set the hardware state.
 */ 
static int 
clps7111fb_set_par(struct fb_info *info)
{
	unsigned int lcdcon, syscon, pixclock;

	switch (info->var.bits_per_pixel) {
	case 1:
		info->fix.visual = FB_VISUAL_MONO01;
		break;
	case 2:
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		break;
	case 4:
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		break;
	}

	info->fix.line_length = info->var.xres_virtual * info->var.bits_per_pixel / 8;

	lcdcon = (info->var.xres_virtual * info->var.yres_virtual * info->var.bits_per_pixel) / 128 - 1;
	lcdcon |= ((info->var.xres_virtual / 16) - 1) << 13;
	lcdcon |= lcd_ac_prescale << 25;

	/*
	 * Calculate pixel prescale value from the pixclock.  This is:
	 *  36.864MHz / pixclock_mhz - 1.
	 * However, pixclock is in picoseconds, so this ends up being:
	 *  36864000 * pixclock_ps / 10^12 - 1
	 * and this will overflow the 32-bit math.  We perform this as
	 * (9 * 4096000 == 36864000):
	 *  pixclock_ps * 9 * (4096000 / 10^12) - 1
	 */
	pixclock = 9 * info->var.pixclock / 244140 - 1;
	lcdcon |= pixclock << 19;

	if (info->var.bits_per_pixel == 4)
		lcdcon |= LCDCON_GSMD;
	if (info->var.bits_per_pixel >= 2)
		lcdcon |= LCDCON_GSEN;

	/*
	 * LCDCON must only be changed while the LCD is disabled
	 */
	syscon = clps_readl(SYSCON1);
	clps_writel(syscon & ~SYSCON1_LCDEN, SYSCON1);
	clps_writel(lcdcon, LCDCON);
	clps_writel(syscon | SYSCON1_LCDEN, SYSCON1);
	return 0;
}

static int clps7111fb_blank(int blank, struct fb_info *info)
{
    	if (blank) {
		if (machine_is_edb7211()) {
			/* Turn off the LCD backlight. */
			clps_writeb(clps_readb(PDDR) & ~EDB_PD3_LCDBL, PDDR);

			/* Power off the LCD DC-DC converter. */
			clps_writeb(clps_readb(PDDR) & ~EDB_PD1_LCD_DC_DC_EN, PDDR);

			/* Delay for a little while (half a second). */
			udelay(100);

			/* Power off the LCD panel. */
			clps_writeb(clps_readb(PDDR) & ~EDB_PD2_LCDEN, PDDR);

			/* Power off the LCD controller. */
			clps_writel(clps_readl(SYSCON1) & ~SYSCON1_LCDEN, 
					SYSCON1);
		}
	} else {
		if (machine_is_edb7211()) {
			/* Power up the LCD controller. */
			clps_writel(clps_readl(SYSCON1) | SYSCON1_LCDEN,
					SYSCON1);

			/* Power up the LCD panel. */
			clps_writeb(clps_readb(PDDR) | EDB_PD2_LCDEN, PDDR);

			/* Delay for a little while. */
			udelay(100);

			/* Power up the LCD DC-DC converter. */
			clps_writeb(clps_readb(PDDR) | EDB_PD1_LCD_DC_DC_EN,
					PDDR);

			/* Turn on the LCD backlight. */
			clps_writeb(clps_readb(PDDR) | EDB_PD3_LCDBL, PDDR);
		}
	}
	return 0;
}

static struct fb_ops clps7111fb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= clps7111fb_check_var,
	.fb_set_par	= clps7111fb_set_par,
	.fb_setcolreg	= clps7111fb_setcolreg,
	.fb_blank	= clps7111fb_blank,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_cursor	= soft_cursor,
};

static int 
clps7111fb_proc_backlight_read(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	/* We need at least two characters, one for the digit, and one for
	 * the terminating NULL. */
	if (count < 2) 
		return -EINVAL;

	if (machine_is_edb7211()) {
		return sprintf(page, "%d\n", 
				(clps_readb(PDDR) & EDB_PD3_LCDBL) ? 1 : 0);
	}

	return 0;
}

static int 
clps7111fb_proc_backlight_write(struct file *file, const char *buffer, 
		unsigned long count, void *data)
{
	unsigned char char_value;
	int value;

	if (count < 1) {
		return -EINVAL;
	}

	if (copy_from_user(&char_value, buffer, 1)) 
		return -EFAULT;

	value = char_value - '0';

	if (machine_is_edb7211()) {
		unsigned char port_d;

		port_d = clps_readb(PDDR);

		if (value) {
			port_d |= EDB_PD3_LCDBL;
		} else {
			port_d &= ~EDB_PD3_LCDBL;
		}

		clps_writeb(port_d, PDDR);
	}

	return count;
}

static void __init clps711x_guess_lcd_params(struct fb_info *info)
{
	unsigned int lcdcon, syscon, size;
	unsigned long phys_base = PAGE_OFFSET;
	void *virt_base = (void *)PAGE_OFFSET;

	info->var.xres_virtual	 = 640;
	info->var.yres_virtual	 = 240;
	info->var.bits_per_pixel = 4;
	info->var.activate	 = FB_ACTIVATE_NOW;
	info->var.height	 = -1;
	info->var.width		 = -1;
	info->var.pixclock	 = 93006; /* 10.752MHz pixel clock */

	/*
	 * If the LCD controller is already running, decode the values
	 * in LCDCON to xres/yres/bpp/pixclock/acprescale
	 */
	syscon = clps_readl(SYSCON1);
	if (syscon & SYSCON1_LCDEN) {
		lcdcon = clps_readl(LCDCON);

		/*
		 * Decode GSMD and GSEN bits to bits per pixel
		 */
		switch (lcdcon & (LCDCON_GSMD | LCDCON_GSEN)) {
		case LCDCON_GSMD | LCDCON_GSEN:
			info->var.bits_per_pixel = 4;
			break;

		case LCDCON_GSEN:
			info->var.bits_per_pixel = 2;
			break;

		default:
			info->var.bits_per_pixel = 1;
			break;
		}

		/*
		 * Decode xres/yres
		 */
		info->var.xres_virtual = (((lcdcon >> 13) & 0x3f) + 1) * 16;
		info->var.yres_virtual = (((lcdcon & 0x1fff) + 1) * 128) /
					  (info->var.xres_virtual *
					   info->var.bits_per_pixel);

		/*
		 * Calculate pixclock
		 */
		info->var.pixclock = (((lcdcon >> 19) & 0x3f) + 1) * 244140 / 9;

		/*
		 * Grab AC prescale
		 */
		lcd_ac_prescale = (lcdcon >> 25) & 0x1f;
	}

	info->var.xres = info->var.xres_virtual;
	info->var.yres = info->var.yres_virtual;
	info->var.grayscale = info->var.bits_per_pixel > 1;

	size = info->var.xres * info->var.yres * info->var.bits_per_pixel / 8;

	/*
	 * Might be worth checking to see if we can use the on-board
	 * RAM if size here...
	 * CLPS7110 - no on-board SRAM
	 * EP7212   - 38400 bytes
	 */
	if (size <= 38400) {
		printk(KERN_INFO "CLPS711xFB: could use on-board SRAM?\n");
	}

	if ((syscon & SYSCON1_LCDEN) == 0) {
		/*
		 * The display isn't running.  Ensure that
		 * the display memory is empty.
		 */
		memset(virt_base, 0, size);
	}

	info->screen_base    = virt_base;
	info->fix.smem_start = phys_base;
	info->fix.smem_len   = PAGE_ALIGN(size);
	info->fix.type       = FB_TYPE_PACKED_PIXELS;
}

int __init clps711xfb_init(void)
{
	int err = -ENOMEM;

	if (fb_get_options("clps711xfb", NULL))
		return -ENODEV;

	cfb = kmalloc(sizeof(*cfb), GFP_KERNEL);
	if (!cfb)
		goto out;

	memset(cfb, 0, sizeof(*cfb));
	strcpy(cfb->fix.id, "clps711x");

	cfb->fbops		= &clps7111fb_ops;
	cfb->flags		= FBINFO_DEFAULT;

	clps711x_guess_lcd_params(cfb);

	fb_alloc_cmap(&cfb->cmap, CMAP_MAX_SIZE, 0);

	/* Register the /proc entries. */
	clps7111fb_backlight_proc_entry = create_proc_entry("backlight", 0444,
		&proc_root);
	if (clps7111fb_backlight_proc_entry == NULL) {
		printk("Couldn't create the /proc entry for the backlight.\n");
		return -EINVAL;
	}

	clps7111fb_backlight_proc_entry->read_proc = 
		&clps7111fb_proc_backlight_read;
	clps7111fb_backlight_proc_entry->write_proc = 
		&clps7111fb_proc_backlight_write;

	/*
	 * Power up the LCD
	 */
	if (machine_is_p720t()) {
		PLD_LCDEN = PLD_LCDEN_EN;
		PLD_PWR |= (PLD_S4_ON|PLD_S3_ON|PLD_S2_ON|PLD_S1_ON);
	}

	if (machine_is_edb7211()) {
		/* Power up the LCD panel. */
		clps_writeb(clps_readb(PDDR) | EDB_PD2_LCDEN, PDDR);

		/* Delay for a little while. */
		udelay(100);

		/* Power up the LCD DC-DC converter. */
		clps_writeb(clps_readb(PDDR) | EDB_PD1_LCD_DC_DC_EN, PDDR);

		/* Turn on the LCD backlight. */
		clps_writeb(clps_readb(PDDR) | EDB_PD3_LCDBL, PDDR);
	}

	err = register_framebuffer(cfb);

out:	return err;
}

static void __exit clps711xfb_exit(void)
{
	unregister_framebuffer(cfb);
	kfree(cfb);

	/*
	 * Power down the LCD
	 */
	if (machine_is_p720t()) {
		PLD_LCDEN = 0;
		PLD_PWR &= ~(PLD_S4_ON|PLD_S3_ON|PLD_S2_ON|PLD_S1_ON);
	}
}

module_init(clps711xfb_init);
module_exit(clps711xfb_exit);

MODULE_AUTHOR("Russell King <rmk@arm.linux.org.uk>");
MODULE_DESCRIPTION("CLPS711x framebuffer driver");
MODULE_LICENSE("GPL");
