/*
 * GTA02 header
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __MACH_S3C24XX_GTA02_H
#define __MACH_S3C24XX_GTA02_H __FILE__

#include <mach/regs-gpio.h>

#define GTA02_GPIO_AUX_LED	S3C2410_GPB(2)
#define GTA02_GPIO_USB_PULLUP	S3C2410_GPB(9)
#define GTA02_GPIO_AUX_KEY	S3C2410_GPF(6)
#define GTA02_GPIO_HOLD_KEY	S3C2410_GPF(7)
#define GTA02_GPIO_AMP_SHUT	S3C2410_GPJ(1)	/* v2 + v3 + v4 only */
#define GTA02_GPIO_HP_IN	S3C2410_GPJ(2)	/* v2 + v3 + v4 only */

#define GTA02_IRQ_PCF50633	IRQ_EINT9

#endif /* __MACH_S3C24XX_GTA02_H */
