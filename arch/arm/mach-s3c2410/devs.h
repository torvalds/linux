/* arch/arm/mach-s3c2410/devs.h
 *
 * Copyright (c) 2004 Simtec Electronics
 * Ben Dooks <ben@simtec.co.uk>
 *
 * Header file for s3c2410 standard platform devices
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *      18-Aug-2004 BJD  Created initial version
 *	27-Aug-2004 BJD  Added timers 0 through 3
 *	10-Feb-2005 BJD	 Added camera from guillaume.gourat@nexvision.tv
*/
#include <linux/config.h>
#include <linux/platform_device.h>

extern struct platform_device *s3c24xx_uart_devs[];

extern struct platform_device s3c_device_usb;
extern struct platform_device s3c_device_lcd;
extern struct platform_device s3c_device_wdt;
extern struct platform_device s3c_device_i2c;
extern struct platform_device s3c_device_iis;
extern struct platform_device s3c_device_rtc;
extern struct platform_device s3c_device_adc;
extern struct platform_device s3c_device_sdi;

extern struct platform_device s3c_device_spi0;
extern struct platform_device s3c_device_spi1;

extern struct platform_device s3c_device_nand;

extern struct platform_device s3c_device_timer0;
extern struct platform_device s3c_device_timer1;
extern struct platform_device s3c_device_timer2;
extern struct platform_device s3c_device_timer3;

extern struct platform_device s3c_device_usbgadget;

/* s3c2440 specific devices */

#ifdef CONFIG_CPU_S3C2440

extern struct platform_device s3c_device_camif;

#endif
