/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * Common Header for S3C64XX machines
 */

#ifndef __ARCH_ARM_MACH_S3C64XX_COMMON_H
#define __ARCH_ARM_MACH_S3C64XX_COMMON_H

#include <linux/reboot.h>

void s3c64xx_init_irq(u32 vic0, u32 vic1);
void s3c64xx_init_io(struct map_desc *mach_desc, int size);

struct device_node;
void s3c64xx_set_xtal_freq(unsigned long freq);
void s3c64xx_set_xusbxti_freq(unsigned long freq);

#ifdef CONFIG_CPU_S3C6400

extern  int s3c6400_init(void);
extern void s3c6400_init_irq(void);
extern void s3c6400_map_io(void);

#else
#define s3c6400_map_io NULL
#define s3c6400_init NULL
#endif

#ifdef CONFIG_CPU_S3C6410

extern  int s3c6410_init(void);
extern void s3c6410_init_irq(void);
extern void s3c6410_map_io(void);

#else
#define s3c6410_map_io NULL
#define s3c6410_init NULL
#endif

#ifdef CONFIG_S3C64XX_PL080
extern struct pl08x_platform_data s3c64xx_dma0_plat_data;
extern struct pl08x_platform_data s3c64xx_dma1_plat_data;
#endif

/* Samsung HR-Timer Clock mode */
enum samsung_timer_mode {
	SAMSUNG_PWM0,
	SAMSUNG_PWM1,
	SAMSUNG_PWM2,
	SAMSUNG_PWM3,
	SAMSUNG_PWM4,
};

extern void __init samsung_set_timer_source(enum samsung_timer_mode event,
					    enum samsung_timer_mode source);
extern void __init samsung_timer_init(void);

#endif /* __ARCH_ARM_MACH_S3C64XX_COMMON_H */
