/* arch/arm/mach-s3c24xx/include/mach/s3cfb.h
 *
 * Copyright (c) 2015 FriendlyARM (www.arm9.net)
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com/
 *
 * Header file for Samsung Display Driver (FIMD) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MACH_S3CFB_H__
#define __MACH_S3CFB_H__

/*
 * struct s3cfb_lcd_polarity
 * @rise_vclk:	if 1, video data is fetched at rising edge
 * @inv_hsync:	if HSYNC polarity is inversed
 * @inv_vsync:	if VSYNC polarity is inversed
 * @inv_vden:	if VDEN polarity is inversed
 */
struct s3cfb_lcd_polarity {
	int	rise_vclk;
	int	inv_hsync;
	int	inv_vsync;
	int	inv_vden;
};

/*
 * struct s3cfb_lcd_timing
 * @h_fp:	horizontal front porch
 * @h_bp:	horizontal back porch
 * @h_sw:	horizontal sync width
 * @v_fp:	vertical front porch
 * @v_fpe:	vertical front porch for even field
 * @v_bp:	vertical back porch
 * @v_bpe:	vertical back porch for even field
 */
struct s3cfb_lcd_timing {
	int	h_fp;
	int	h_bp;
	int	h_sw;
	int	v_fp;
	int	v_fpe;
	int	v_bp;
	int	v_bpe;
	int	v_sw;
};

/*
 * struct s3cfb_lcd
 * @width:		horizontal resolution
 * @height:		vertical resolution
 * @p_width:	width of lcd in mm
 * @p_height:	height of lcd in mm
 * @bpp:		bits per pixel
 * @freq:		vframe frequency
 * @timing:		timing values
 * @polarity:	polarity settings
 * @init_ldi:	pointer to LDI init function
 *
 */
struct s3cfb_lcd {
	int	width;
	int	height;
	int	p_width;
	int	p_height;
	int	bpp;
	int	freq;
	struct	s3cfb_lcd_timing timing;
	struct	s3cfb_lcd_polarity polarity;
};

/**
 * mini2451_get_lcd()
 *
 * Get s3cfb_lcd which selected by kernel command line.
 */
extern struct s3cfb_lcd *mini2451_get_lcd(void);

#endif /* __MACH_S3CFB_H__ */

