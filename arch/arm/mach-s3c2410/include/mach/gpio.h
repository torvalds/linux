/* arch/arm/mach-s3c2410/include/mach/gpio.h
 *
 * Copyright (c) 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 - GPIO lib support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define gpio_get_value	__gpio_get_value
#define gpio_set_value	__gpio_set_value
#define gpio_cansleep	__gpio_cansleep
#define gpio_to_irq	__gpio_to_irq

/* some boards require extra gpio capacity to support external
 * devices that need GPIO.
 */

#ifdef CONFIG_CPU_S3C244X
#define ARCH_NR_GPIOS	(32 * 9 + CONFIG_S3C24XX_GPIO_EXTRA)
#elif defined(CONFIG_CPU_S3C2443) || defined(CONFIG_CPU_S3C2416)
#define ARCH_NR_GPIOS	(32 * 12 + CONFIG_S3C24XX_GPIO_EXTRA)
#else
#define ARCH_NR_GPIOS	(256 + CONFIG_S3C24XX_GPIO_EXTRA)
#endif

#include <asm-generic/gpio.h>
#include <mach/gpio-nrs.h>
#include <mach/gpio-fns.h>

#ifdef CONFIG_CPU_S3C244X
#define S3C_GPIO_END	(S3C2410_GPJ(0) + 32)
#elif defined(CONFIG_CPU_S3C2443) || defined(CONFIG_CPU_S3C2416)
#define S3C_GPIO_END	(S3C2410_GPM(0) + 32)
#else
#define S3C_GPIO_END	(S3C2410_GPH(0) + 32)
#endif
