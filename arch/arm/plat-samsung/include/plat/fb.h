/* arch/arm/plat-samsung/include/plat/fb.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C - FB platform data definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __PLAT_S3C_FB_H
#define __PLAT_S3C_FB_H __FILE__

#include <linux/platform_data/video_s3c.h>

/**
 * s3c_fb_set_platdata() - Setup the FB device with platform data.
 * @pd: The platform data to set. The data is copied from the passed structure
 *      so the machine data can mark the data __initdata so that any unused
 *      machines will end up dumping their data at runtime.
 */
extern void s3c_fb_set_platdata(struct s3c_fb_platdata *pd);

/**
 * s3c64xx_fb_gpio_setup_24bpp() - S3C64XX setup function for 24bpp LCD
 *
 * Initialise the GPIO for an 24bpp LCD display on the RGB interface.
 */
extern void s3c64xx_fb_gpio_setup_24bpp(void);

#endif /* __PLAT_S3C_FB_H */
