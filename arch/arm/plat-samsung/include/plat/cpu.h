/* linux/arch/arm/plat-samsung/include/plat/cpu.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Copyright (c) 2004-2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Header file for Samsung CPU support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* todo - fix when rmk changes iodescs to use `void __iomem *` */

#ifndef __SAMSUNG_PLAT_CPU_H
#define __SAMSUNG_PLAT_CPU_H

extern unsigned long samsung_cpu_id;

#define S3C24XX_CPU_ID		0x32400000
#define S3C24XX_CPU_MASK	0xFFF00000

#define S3C2412_CPU_ID		0x32412000
#define S3C2412_CPU_MASK	0xFFFFF000

#define S3C6400_CPU_ID		0x36400000
#define S3C6410_CPU_ID		0x36410000
#define S3C64XX_CPU_MASK	0xFFFFF000

#define S5P6440_CPU_ID		0x56440000
#define S5P6450_CPU_ID		0x36450000
#define S5P64XX_CPU_MASK	0xFFFFF000

#define S5PC100_CPU_ID		0x43100000
#define S5PC100_CPU_MASK	0xFFFFF000

#define S5PV210_CPU_ID		0x43110000
#define S5PV210_CPU_MASK	0xFFFFF000

#define EXYNOS4210_CPU_ID	0x43210000
#define EXYNOS4212_CPU_ID	0x43220000
#define EXYNOS4412_CPU_ID	0xE4412200
#define EXYNOS4_CPU_MASK	0xFFFE0000

#define EXYNOS5250_SOC_ID	0x43520000
#define EXYNOS5420_SOC_ID	0xE5420000
#define EXYNOS5440_SOC_ID	0xE5440000
#define EXYNOS5_SOC_MASK	0xFFFFF000

#define IS_SAMSUNG_CPU(name, id, mask)		\
static inline int is_samsung_##name(void)	\
{						\
	return ((samsung_cpu_id & mask) == (id & mask));	\
}

IS_SAMSUNG_CPU(s3c24xx, S3C24XX_CPU_ID, S3C24XX_CPU_MASK)
IS_SAMSUNG_CPU(s3c2412, S3C2412_CPU_ID, S3C2412_CPU_MASK)
IS_SAMSUNG_CPU(s3c6400, S3C6400_CPU_ID, S3C64XX_CPU_MASK)
IS_SAMSUNG_CPU(s3c6410, S3C6410_CPU_ID, S3C64XX_CPU_MASK)
IS_SAMSUNG_CPU(s5p6440, S5P6440_CPU_ID, S5P64XX_CPU_MASK)
IS_SAMSUNG_CPU(s5p6450, S5P6450_CPU_ID, S5P64XX_CPU_MASK)
IS_SAMSUNG_CPU(s5pc100, S5PC100_CPU_ID, S5PC100_CPU_MASK)
IS_SAMSUNG_CPU(s5pv210, S5PV210_CPU_ID, S5PV210_CPU_MASK)
IS_SAMSUNG_CPU(exynos4210, EXYNOS4210_CPU_ID, EXYNOS4_CPU_MASK)
IS_SAMSUNG_CPU(exynos4212, EXYNOS4212_CPU_ID, EXYNOS4_CPU_MASK)
IS_SAMSUNG_CPU(exynos4412, EXYNOS4412_CPU_ID, EXYNOS4_CPU_MASK)
IS_SAMSUNG_CPU(exynos5250, EXYNOS5250_SOC_ID, EXYNOS5_SOC_MASK)
IS_SAMSUNG_CPU(exynos5420, EXYNOS5420_SOC_ID, EXYNOS5_SOC_MASK)
IS_SAMSUNG_CPU(exynos5440, EXYNOS5440_SOC_ID, EXYNOS5_SOC_MASK)

#if defined(CONFIG_CPU_S3C2410) || defined(CONFIG_CPU_S3C2412) || \
    defined(CONFIG_CPU_S3C2416) || defined(CONFIG_CPU_S3C2440) || \
    defined(CONFIG_CPU_S3C2442) || defined(CONFIG_CPU_S3C244X) || \
    defined(CONFIG_CPU_S3C2443)
# define soc_is_s3c24xx()	is_samsung_s3c24xx()
#else
# define soc_is_s3c24xx()	0
#endif

#if defined(CONFIG_CPU_S3C2412)
# define soc_is_s3c2412()	is_samsung_s3c2412()
#else
# define soc_is_s3c2412()	0
#endif

#if defined(CONFIG_CPU_S3C6400) || defined(CONFIG_CPU_S3C6410)
# define soc_is_s3c64xx()	(is_samsung_s3c6400() || is_samsung_s3c6410())
#else
# define soc_is_s3c64xx()	0
#endif

#if defined(CONFIG_CPU_S5P6440)
# define soc_is_s5p6440()	is_samsung_s5p6440()
#else
# define soc_is_s5p6440()	0
#endif

#if defined(CONFIG_CPU_S5P6450)
# define soc_is_s5p6450()	is_samsung_s5p6450()
#else
# define soc_is_s5p6450()	0
#endif

#if defined(CONFIG_CPU_S5PC100)
# define soc_is_s5pc100()	is_samsung_s5pc100()
#else
# define soc_is_s5pc100()	0
#endif

#if defined(CONFIG_CPU_S5PV210)
# define soc_is_s5pv210()	is_samsung_s5pv210()
#else
# define soc_is_s5pv210()	0
#endif

#if defined(CONFIG_CPU_EXYNOS4210)
# define soc_is_exynos4210()	is_samsung_exynos4210()
#else
# define soc_is_exynos4210()	0
#endif

#if defined(CONFIG_SOC_EXYNOS4212)
# define soc_is_exynos4212()	is_samsung_exynos4212()
#else
# define soc_is_exynos4212()	0
#endif

#if defined(CONFIG_SOC_EXYNOS4412)
# define soc_is_exynos4412()	is_samsung_exynos4412()
#else
# define soc_is_exynos4412()	0
#endif

#define EXYNOS4210_REV_0	(0x0)
#define EXYNOS4210_REV_1_0	(0x10)
#define EXYNOS4210_REV_1_1	(0x11)

#if defined(CONFIG_SOC_EXYNOS5250)
# define soc_is_exynos5250()	is_samsung_exynos5250()
#else
# define soc_is_exynos5250()	0
#endif

#if defined(CONFIG_SOC_EXYNOS5420)
# define soc_is_exynos5420()	is_samsung_exynos5420()
#else
# define soc_is_exynos5420()	0
#endif

#if defined(CONFIG_SOC_EXYNOS5440)
# define soc_is_exynos5440()	is_samsung_exynos5440()
#else
# define soc_is_exynos5440()	0
#endif

#define IODESC_ENT(x) { (unsigned long)S3C24XX_VA_##x, __phys_to_pfn(S3C24XX_PA_##x), S3C24XX_SZ_##x, MT_DEVICE }

#ifndef KHZ
#define KHZ (1000)
#endif

#ifndef MHZ
#define MHZ (1000*1000)
#endif

#define print_mhz(m) ((m) / MHZ), (((m) / 1000) % 1000)

/* forward declaration */
struct s3c24xx_uart_resources;
struct platform_device;
struct s3c2410_uartcfg;
struct map_desc;

/* per-cpu initialisation function table. */

struct cpu_table {
	unsigned long	idcode;
	unsigned long	idmask;
	void		(*map_io)(void);
	void		(*init_uarts)(struct s3c2410_uartcfg *cfg, int no);
	void		(*init_clocks)(int xtal);
	int		(*init)(void);
	const char	*name;
};

extern void s3c_init_cpu(unsigned long idcode,
			 struct cpu_table *cpus, unsigned int cputab_size);

/* core initialisation functions */

extern void s5p_init_irq(u32 *vic, u32 num_vic);

extern void s3c24xx_init_io(struct map_desc *mach_desc, int size);

extern void s3c24xx_init_cpu(void);
extern void s3c64xx_init_cpu(void);
extern void s5p_init_cpu(void __iomem *cpuid_addr);

extern unsigned int samsung_rev(void);

extern void s3c24xx_init_uarts(struct s3c2410_uartcfg *cfg, int no);

extern void s3c24xx_init_clocks(int xtal);

extern void s3c24xx_init_uartdevs(char *name,
				  struct s3c24xx_uart_resources *res,
				  struct s3c2410_uartcfg *cfg, int no);

extern struct syscore_ops s3c2410_pm_syscore_ops;
extern struct syscore_ops s3c2412_pm_syscore_ops;
extern struct syscore_ops s3c2416_pm_syscore_ops;
extern struct syscore_ops s3c244x_pm_syscore_ops;

/* system device subsystems */

extern struct bus_type s3c2410_subsys;
extern struct bus_type s3c2410a_subsys;
extern struct bus_type s3c2412_subsys;
extern struct bus_type s3c2416_subsys;
extern struct bus_type s3c2440_subsys;
extern struct bus_type s3c2442_subsys;
extern struct bus_type s3c2443_subsys;
extern struct bus_type s3c6410_subsys;
extern struct bus_type s5p64x0_subsys;
extern struct bus_type s5pv210_subsys;
extern struct bus_type exynos_subsys;

extern void (*s5pc1xx_idle)(void);

#endif
