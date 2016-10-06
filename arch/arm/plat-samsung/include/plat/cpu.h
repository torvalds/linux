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

#define S3C2410_CPU_ID		0x32410000
#define S3C2410_CPU_MASK	0xFFFFFFFF

#define S3C24XX_CPU_ID		0x32400000
#define S3C24XX_CPU_MASK	0xFFF00000

#define S3C2412_CPU_ID		0x32412000
#define S3C2412_CPU_MASK	0xFFFFF000

#define S3C6400_CPU_ID		0x36400000
#define S3C6410_CPU_ID		0x36410000
#define S3C64XX_CPU_MASK	0xFFFFF000

#define S5PV210_CPU_ID		0x43110000
#define S5PV210_CPU_MASK	0xFFFFF000

#define IS_SAMSUNG_CPU(name, id, mask)		\
static inline int is_samsung_##name(void)	\
{						\
	return ((samsung_cpu_id & mask) == (id & mask));	\
}

IS_SAMSUNG_CPU(s3c2410, S3C2410_CPU_ID, S3C2410_CPU_MASK)
IS_SAMSUNG_CPU(s3c24xx, S3C24XX_CPU_ID, S3C24XX_CPU_MASK)
IS_SAMSUNG_CPU(s3c2412, S3C2412_CPU_ID, S3C2412_CPU_MASK)
IS_SAMSUNG_CPU(s3c6400, S3C6400_CPU_ID, S3C64XX_CPU_MASK)
IS_SAMSUNG_CPU(s3c6410, S3C6410_CPU_ID, S3C64XX_CPU_MASK)

#if defined(CONFIG_CPU_S3C2410) || defined(CONFIG_CPU_S3C2412) || \
    defined(CONFIG_CPU_S3C2416) || defined(CONFIG_CPU_S3C2440) || \
    defined(CONFIG_CPU_S3C2442) || defined(CONFIG_CPU_S3C244X) || \
    defined(CONFIG_CPU_S3C2443)
# define soc_is_s3c24xx()	is_samsung_s3c24xx()
# define soc_is_s3c2410()	is_samsung_s3c2410()
#else
# define soc_is_s3c24xx()	0
# define soc_is_s3c2410()	0
#endif

#if defined(CONFIG_CPU_S3C2412)
# define soc_is_s3c2412()	is_samsung_s3c2412()
#else
# define soc_is_s3c2412()	0
#endif

#if defined(CONFIG_CPU_S3C6400) || defined(CONFIG_CPU_S3C6410)
# define soc_is_s3c6400()	is_samsung_s3c6400()
# define soc_is_s3c6410()	is_samsung_s3c6410()
# define soc_is_s3c64xx()	(is_samsung_s3c6400() || is_samsung_s3c6410())
#else
# define soc_is_s3c6400()	0
# define soc_is_s3c6410()	0
# define soc_is_s3c64xx()	0
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

extern void s3c24xx_init_io(struct map_desc *mach_desc, int size);

extern void s3c64xx_init_cpu(void);
extern void s5p_init_cpu(const void __iomem *cpuid_addr);

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

#endif
