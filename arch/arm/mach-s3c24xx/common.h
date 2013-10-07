/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Common Header for S3C24XX SoCs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_S3C24XX_COMMON_H
#define __ARCH_ARM_MACH_S3C24XX_COMMON_H __FILE__

#include <linux/reboot.h>

struct s3c2410_uartcfg;

#ifdef CONFIG_CPU_S3C2410
extern  int s3c2410_init(void);
extern  int s3c2410a_init(void);
extern void s3c2410_map_io(void);
extern void s3c2410_init_uarts(struct s3c2410_uartcfg *cfg, int no);
extern void s3c2410_init_clocks(int xtal);
extern void s3c2410_restart(enum reboot_mode mode, const char *cmd);
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
extern void s3c2412_restart(enum reboot_mode mode, const char *cmd);
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
extern void s3c2416_restart(enum reboot_mode mode, const char *cmd);
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
extern void s3c244x_init_clocks(int xtal);
extern void s3c244x_restart(enum reboot_mode mode, const char *cmd);
#else
#define s3c244x_init_clocks NULL
#define s3c244x_init_uarts NULL
#endif

#ifdef CONFIG_CPU_S3C2440
extern  int s3c2440_init(void);
extern void s3c2440_map_io(void);
extern void s3c2440_init_irq(void);
#else
#define s3c2440_init NULL
#define s3c2440_map_io NULL
#endif

#ifdef CONFIG_CPU_S3C2442
extern  int s3c2442_init(void);
extern void s3c2442_map_io(void);
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
extern void s3c2443_restart(enum reboot_mode mode, const char *cmd);
extern void s3c2443_init_irq(void);
#else
#define s3c2443_init_clocks NULL
#define s3c2443_init_uarts NULL
#define s3c2443_map_io NULL
#define s3c2443_init NULL
#endif

extern struct syscore_ops s3c24xx_irq_syscore_ops;

extern struct platform_device s3c2412_device_dma;
extern struct platform_device s3c2443_device_dma;

#endif /* __ARCH_ARM_MACH_S3C24XX_COMMON_H */
