/* linux/include/asm/arch-nuc900/fb.h
 *
 * Copyright (c) 2008 Nuvoton technology corporation
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Changelog:
 *
 *   2008/08/26     vincen.zswan modify this file for LCD.
 */

#ifndef __ASM_ARM_FB_H
#define __ASM_ARM_FB_H



/* LCD Controller Hardware Desc */
struct nuc900fb_hw {
	unsigned int lcd_dccs;
	unsigned int lcd_device_ctrl;
	unsigned int lcd_mpulcd_cmd;
	unsigned int lcd_int_cs;
	unsigned int lcd_crtc_size;
	unsigned int lcd_crtc_dend;
	unsigned int lcd_crtc_hr;
	unsigned int lcd_crtc_hsync;
	unsigned int lcd_crtc_vr;
	unsigned int lcd_va_baddr0;
	unsigned int lcd_va_baddr1;
	unsigned int lcd_va_fbctrl;
	unsigned int lcd_va_scale;
	unsigned int lcd_va_test;
	unsigned int lcd_va_win;
	unsigned int lcd_va_stuff;
};

/* LCD Display Description */
struct nuc900fb_display {
	/* LCD Image type */
	unsigned type;

	/* LCD Screen Size */
	unsigned short width;
	unsigned short height;

	/* LCD Screen Info */
	unsigned short xres;
	unsigned short yres;
	unsigned short bpp;

	unsigned long pixclock;
	unsigned short left_margin;
	unsigned short right_margin;
	unsigned short hsync_len;
	unsigned short upper_margin;
	unsigned short lower_margin;
	unsigned short vsync_len;

	/* hardware special register value */
	unsigned int dccs;
	unsigned int devctl;
	unsigned int fbctrl;
	unsigned int scale;
};

struct nuc900fb_mach_info {
	struct nuc900fb_display *displays;
	unsigned num_displays;
	unsigned default_display;
	/* GPIO Setting  Info */
	unsigned gpio_dir;
	unsigned gpio_dir_mask;
	unsigned gpio_data;
	unsigned gpio_data_mask;
};

extern void __init nuc900_fb_set_platdata(struct nuc900fb_mach_info *);

#endif /* __ASM_ARM_FB_H */
