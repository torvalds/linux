/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Common Header for S3C24XX SoCs
 */

#ifndef __ARCH_ARM_MACH_S3C24XX_COMMON_H
#define __ARCH_ARM_MACH_S3C24XX_COMMON_H __FILE__

#include <linux/reboot.h>
#include "irqs.h"

struct s3c2410_uartcfg;

#ifdef CONFIG_CPU_S3C2410
extern  int s3c2410_init(void);
extern  int s3c2410a_init(void);
extern void s3c2410_map_io(void);
extern void s3c2410_init_uarts(struct s3c2410_uartcfg *cfg, int no);
extern void s3c2410_init_clocks(int xtal);
extern void s3c2410_init_irq(void);
#else
#define s3c2410_init_clocks NULL
#define s3c2410_init_uarts NULL
#define s3c2410_map_io NULL
#define s3c2410_init NULL
#define s3c2410a_init NULL
#endif

#ifdef CONFIG_CPU_S3C2412
extern  int s3c2412_init(void);
extern void s3c2412_map_io(void);
extern void s3c2412_init_uarts(struct s3c2410_uartcfg *cfg, int no);
extern void s3c2412_init_clocks(int xtal);
extern  int s3c2412_baseclk_add(void);
extern void s3c2412_init_irq(void);
#else
#define s3c2412_init_clocks NULL
#define s3c2412_init_uarts NULL
#define s3c2412_map_io NULL
#define s3c2412_init NULL
#endif

#ifdef CONFIG_CPU_S3C2416
extern  int s3c2416_init(void);
extern void s3c2416_map_io(void);
extern void s3c2416_init_uarts(struct s3c2410_uartcfg *cfg, int no);
extern void s3c2416_init_clocks(int xtal);
extern  int s3c2416_baseclk_add(void);
extern void s3c2416_init_irq(void);

extern struct syscore_ops s3c2416_irq_syscore_ops;
#else
#define s3c2416_init_clocks NULL
#define s3c2416_init_uarts NULL
#define s3c2416_map_io NULL
#define s3c2416_init NULL
#endif

#if defined(CONFIG_CPU_S3C2440) || defined(CONFIG_CPU_S3C2442)
extern void s3c244x_map_io(void);
extern void s3c244x_init_uarts(struct s3c2410_uartcfg *cfg, int no);
#else
#define s3c244x_init_uarts NULL
#endif

#ifdef CONFIG_CPU_S3C2440
extern  int s3c2440_init(void);
extern void s3c2440_map_io(void);
extern void s3c2440_init_clocks(int xtal);
extern void s3c2440_init_irq(void);
#else
#define s3c2440_init NULL
#define s3c2440_map_io NULL
#endif

#ifdef CONFIG_CPU_S3C2442
extern  int s3c2442_init(void);
extern void s3c2442_map_io(void);
extern void s3c2442_init_clocks(int xtal);
extern void s3c2442_init_irq(void);
#else
#define s3c2442_init NULL
#define s3c2442_map_io NULL
#endif

#ifdef CONFIG_CPU_S3C2443
extern  int s3c2443_init(void);
extern void s3c2443_map_io(void);
extern void s3c2443_init_uarts(struct s3c2410_uartcfg *cfg, int no);
extern void s3c2443_init_clocks(int xtal);
extern  int s3c2443_baseclk_add(void);
extern void s3c2443_init_irq(void);
#else
#define s3c2443_init_clocks NULL
#define s3c2443_init_uarts NULL
#define s3c2443_map_io NULL
#define s3c2443_init NULL
#endif

extern struct syscore_ops s3c24xx_irq_syscore_ops;

extern struct platform_device s3c2410_device_dma;
extern struct platform_device s3c2412_device_dma;
extern struct platform_device s3c2440_device_dma;
extern struct platform_device s3c2443_device_dma;

extern struct platform_device s3c2410_device_dclk;

enum s3c24xx_timer_mode {
	S3C24XX_PWM0,
	S3C24XX_PWM1,
	S3C24XX_PWM2,
	S3C24XX_PWM3,
	S3C24XX_PWM4,
};

extern void __init s3c24xx_set_timer_source(enum s3c24xx_timer_mode event,
					    enum s3c24xx_timer_mode source);
extern void __init s3c24xx_timer_init(void);

#endif /* __ARCH_ARM_MACH_S3C24XX_COMMON_H */
