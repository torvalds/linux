/*
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Common Codes for EXYNOS
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <clocksource/samsung_pwm.h>
#include <linux/sched.h>
#include <linux/serial_core.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/export.h>
#include <linux/irqdomain.h>
#include <linux/of_address.h>
#include <linux/clocksource.h>
#include <linux/clk-provider.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/irqchip/chained_irq.h>

#include <asm/proc-fns.h>
#include <asm/exception.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/cacheflush.h>

#include <mach/regs-irq.h>
#include <mach/regs-pmu.h>
#include <mach/regs-gpio.h>
#include <mach/irqs.h>

#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/pm.h>
#include <plat/sdhci.h>
#include <plat/gpio-cfg.h>
#include <plat/adc-core.h>
#include <plat/fb-core.h>
#include <plat/fimc-core.h>
#include <plat/iic-core.h>
#include <plat/tv-core.h>
#include <plat/spi-core.h>
#include <plat/regs-serial.h>

#include "common.h"
#define L2_AUX_VAL 0x7C470001
#define L2_AUX_MASK 0xC200ffff

static const char name_exynos4210[] = "EXYNOS4210";
static const char name_exynos4212[] = "EXYNOS4212";
static const char name_exynos4412[] = "EXYNOS4412";
static const char name_exynos5250[] = "EXYNOS5250";
static const char name_exynos5420[] = "EXYNOS5420";
static const char name_exynos5440[] = "EXYNOS5440";

static void exynos4_map_io(void);
static void exynos5_map_io(void);
static void exynos5440_map_io(void);
static void exynos4_init_uarts(struct s3c2410_uartcfg *cfg, int no);
static int exynos_init(void);

unsigned long xxti_f = 0, xusbxti_f = 0;

static struct cpu_table cpu_ids[] __initdata = {
	{
		.idcode		= EXYNOS4210_CPU_ID,
		.idmask		= EXYNOS4_CPU_MASK,
		.map_io		= exynos4_map_io,
		.init_uarts	= exynos4_init_uarts,
		.init		= exynos_init,
		.name		= name_exynos4210,
	}, {
		.idcode		= EXYNOS4212_CPU_ID,
		.idmask		= EXYNOS4_CPU_MASK,
		.map_io		= exynos4_map_io,
		.init_uarts	= exynos4_init_uarts,
		.init		= exynos_init,
		.name		= name_exynos4212,
	}, {
		.idcode		= EXYNOS4412_CPU_ID,
		.idmask		= EXYNOS4_CPU_MASK,
		.map_io		= exynos4_map_io,
		.init_uarts	= exynos4_init_uarts,
		.init		= exynos_init,
		.name		= name_exynos4412,
	}, {
		.idcode		= EXYNOS5250_SOC_ID,
		.idmask		= EXYNOS5_SOC_MASK,
		.map_io		= exynos5_map_io,
		.init		= exynos_init,
		.name		= name_exynos5250,
	}, {
		.idcode		= EXYNOS5420_SOC_ID,
		.idmask		= EXYNOS5_SOC_MASK,
		.map_io		= exynos5_map_io,
		.init		= exynos_init,
		.name		= name_exynos5420,
	}, {
		.idcode		= EXYNOS5440_SOC_ID,
		.idmask		= EXYNOS5_SOC_MASK,
		.map_io		= exynos5440_map_io,
		.init		= exynos_init,
		.name		= name_exynos5440,
	},
};

/* Initial IO mappings */

static struct map_desc exynos_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S5P_VA_CHIPID,
		.pfn		= __phys_to_pfn(EXYNOS_PA_CHIPID),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

static struct map_desc exynos4_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S3C_VA_SYS,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_SYSCON),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_TIMER,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_TIMER),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_WATCHDOG,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_WATCHDOG),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_SROMC,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_SROMC),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_SYSTIMER,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_SYSTIMER),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_PMU,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_PMU),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_COMBINER_BASE,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_COMBINER),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_GIC_CPU,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_GIC_CPU),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_GIC_DIST,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_GIC_DIST),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_UART,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_UART),
		.length		= SZ_512K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_CMU,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_CMU),
		.length		= SZ_128K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_COREPERI_BASE,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_COREPERI),
		.length		= SZ_8K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_L2CC,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_L2CC),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_DMC0,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_DMC0),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_DMC1,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_DMC1),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_USB_HSPHY,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_HSPHY),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

static struct map_desc exynos4_iodesc0[] __initdata = {
	{
		.virtual	= (unsigned long)S5P_VA_SYSRAM,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_SYSRAM0),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

static struct map_desc exynos4_iodesc1[] __initdata = {
	{
		.virtual	= (unsigned long)S5P_VA_SYSRAM,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_SYSRAM1),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

static struct map_desc exynos4210_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S5P_VA_SYSRAM_NS,
		.pfn		= __phys_to_pfn(EXYNOS4210_PA_SYSRAM_NS),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

static struct map_desc exynos4x12_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S5P_VA_SYSRAM_NS,
		.pfn		= __phys_to_pfn(EXYNOS4x12_PA_SYSRAM_NS),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

static struct map_desc exynos5250_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S5P_VA_SYSRAM_NS,
		.pfn		= __phys_to_pfn(EXYNOS5250_PA_SYSRAM_NS),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

static struct map_desc exynos5_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S3C_VA_SYS,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_SYSCON),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_TIMER,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_TIMER),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_WATCHDOG,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_WATCHDOG),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_SROMC,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_SROMC),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_SYSRAM,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_SYSRAM),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_CMU,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_CMU),
		.length		= 144 * SZ_1K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_PMU,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_PMU),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_UART,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_UART),
		.length		= SZ_512K,
		.type		= MT_DEVICE,
	},
};

static struct map_desc exynos5440_iodesc0[] __initdata = {
	{
		.virtual	= (unsigned long)S3C_VA_UART,
		.pfn		= __phys_to_pfn(EXYNOS5440_PA_UART0),
		.length		= SZ_512K,
		.type		= MT_DEVICE,
	},
};

static struct samsung_pwm_variant exynos4_pwm_variant = {
	.bits		= 32,
	.div_base	= 0,
	.has_tint_cstat	= true,
	.tclk_mask	= 0,
};

void exynos4_restart(char mode, const char *cmd)
{
	__raw_writel(0x1, S5P_SWRESET);
}

void exynos5_restart(char mode, const char *cmd)
{
	struct device_node *np;
	u32 val;
	void __iomem *addr;

	if (of_machine_is_compatible("samsung,exynos5250")) {
		val = 0x1;
		addr = EXYNOS_SWRESET;
	} else if (of_machine_is_compatible("samsung,exynos5440")) {
		u32 status;
		np = of_find_compatible_node(NULL, NULL, "samsung,exynos5440-clock");

		addr = of_iomap(np, 0) + 0xbc;
		status = __raw_readl(addr);

		addr = of_iomap(np, 0) + 0xcc;
		val = __raw_readl(addr);

		val = (val & 0xffff0000) | (status & 0xffff);
	} else {
		pr_err("%s: cannot support non-DT\n", __func__);
		return;
	}

	__raw_writel(val, addr);
}

void __init exynos_init_late(void)
{
	if (of_machine_is_compatible("samsung,exynos5440"))
		/* to be supported later */
		return;

	exynos_pm_late_initcall();
}

#ifdef CONFIG_OF
int __init exynos_fdt_map_chipid(unsigned long node, const char *uname,
					int depth, void *data)
{
	struct map_desc iodesc;
	__be32 *reg;
	unsigned long len;

	if (!of_flat_dt_is_compatible(node, "samsung,exynos4210-chipid") &&
		!of_flat_dt_is_compatible(node, "samsung,exynos5440-clock"))
		return 0;

	reg = of_get_flat_dt_prop(node, "reg", &len);
	if (reg == NULL || len != (sizeof(unsigned long) * 2))
		return 0;

	iodesc.pfn = __phys_to_pfn(be32_to_cpu(reg[0]));
	iodesc.length = be32_to_cpu(reg[1]) - 1;
	iodesc.virtual = (unsigned long)S5P_VA_CHIPID;
	iodesc.type = MT_DEVICE;
	iotable_init(&iodesc, 1);
	return 1;
}
#endif

/*
 * exynos_map_io
 *
 * register the standard cpu IO areas
 */

void __init exynos_init_io(struct map_desc *mach_desc, int size)
{
	debug_ll_io_init();

#ifdef CONFIG_OF
	if (initial_boot_params)
		of_scan_flat_dt(exynos_fdt_map_chipid, NULL);
	else
#endif
		iotable_init(exynos_iodesc, ARRAY_SIZE(exynos_iodesc));

	if (mach_desc)
		iotable_init(mach_desc, size);

	/* detect cpu id and rev. */
	s5p_init_cpu(S5P_VA_CHIPID);

	s3c_init_cpu(samsung_cpu_id, cpu_ids, ARRAY_SIZE(cpu_ids));
}

static void __init exynos4_map_io(void)
{
	iotable_init(exynos4_iodesc, ARRAY_SIZE(exynos4_iodesc));

	if (soc_is_exynos4210() && samsung_rev() == EXYNOS4210_REV_0)
		iotable_init(exynos4_iodesc0, ARRAY_SIZE(exynos4_iodesc0));
	else
		iotable_init(exynos4_iodesc1, ARRAY_SIZE(exynos4_iodesc1));

	if (soc_is_exynos4210())
		iotable_init(exynos4210_iodesc, ARRAY_SIZE(exynos4210_iodesc));
	if (soc_is_exynos4212() || soc_is_exynos4412())
		iotable_init(exynos4x12_iodesc, ARRAY_SIZE(exynos4x12_iodesc));

	/* initialize device information early */
	exynos4_default_sdhci0();
	exynos4_default_sdhci1();
	exynos4_default_sdhci2();
	exynos4_default_sdhci3();

	s3c_adc_setname("samsung-adc-v3");

	s3c_fimc_setname(0, "exynos4-fimc");
	s3c_fimc_setname(1, "exynos4-fimc");
	s3c_fimc_setname(2, "exynos4-fimc");
	s3c_fimc_setname(3, "exynos4-fimc");

	s3c_sdhci_setname(0, "exynos4-sdhci");
	s3c_sdhci_setname(1, "exynos4-sdhci");
	s3c_sdhci_setname(2, "exynos4-sdhci");
	s3c_sdhci_setname(3, "exynos4-sdhci");

	/* The I2C bus controllers are directly compatible with s3c2440 */
	s3c_i2c0_setname("s3c2440-i2c");
	s3c_i2c1_setname("s3c2440-i2c");
	s3c_i2c2_setname("s3c2440-i2c");

	s5p_fb_setname(0, "exynos4-fb");
	s5p_hdmi_setname("exynos4-hdmi");

	s3c64xx_spi_setname("exynos4210-spi");
}

static void __init exynos5_map_io(void)
{
	iotable_init(exynos5_iodesc, ARRAY_SIZE(exynos5_iodesc));

	if (soc_is_exynos5250())
		iotable_init(exynos5250_iodesc, ARRAY_SIZE(exynos5250_iodesc));
}

static void __init exynos5440_map_io(void)
{
	iotable_init(exynos5440_iodesc0, ARRAY_SIZE(exynos5440_iodesc0));
}

void __init exynos_set_timer_source(u8 channels)
{
	exynos4_pwm_variant.output_mask = BIT(SAMSUNG_PWM_NUM) - 1;
	exynos4_pwm_variant.output_mask &= ~channels;
}

void __init exynos_init_time(void)
{
	unsigned int timer_irqs[SAMSUNG_PWM_NUM] = {
		EXYNOS4_IRQ_TIMER0_VIC, EXYNOS4_IRQ_TIMER1_VIC,
		EXYNOS4_IRQ_TIMER2_VIC, EXYNOS4_IRQ_TIMER3_VIC,
		EXYNOS4_IRQ_TIMER4_VIC,
	};

	if (of_have_populated_dt()) {
#ifdef CONFIG_OF
		of_clk_init(NULL);
		clocksource_of_init();
#endif
	} else {
		/* todo: remove after migrating legacy E4 platforms to dt */
#ifdef CONFIG_ARCH_EXYNOS4
		exynos4_clk_init(NULL, !soc_is_exynos4210(), S5P_VA_CMU, readl(S5P_VA_CHIPID + 8) & 1);
		exynos4_clk_register_fixed_ext(xxti_f, xusbxti_f);
#endif
#ifdef CONFIG_CLKSRC_SAMSUNG_PWM
		if (soc_is_exynos4210() && samsung_rev() == EXYNOS4210_REV_0)
			samsung_pwm_clocksource_init(S3C_VA_TIMER,
					timer_irqs, &exynos4_pwm_variant);
		else
#endif
			mct_init(S5P_VA_SYSTIMER, EXYNOS4_IRQ_MCT_G0,
					EXYNOS4_IRQ_MCT_L0, EXYNOS4_IRQ_MCT_L1);
	}
}

static unsigned int max_combiner_nr(void)
{
	if (soc_is_exynos5250())
		return EXYNOS5_MAX_COMBINER_NR;
	else if (soc_is_exynos4412())
		return EXYNOS4412_MAX_COMBINER_NR;
	else if (soc_is_exynos4212())
		return EXYNOS4212_MAX_COMBINER_NR;
	else
		return EXYNOS4210_MAX_COMBINER_NR;
}


void __init exynos4_init_irq(void)
{
	unsigned int gic_bank_offset;

	gic_bank_offset = soc_is_exynos4412() ? 0x4000 : 0x8000;

	if (!of_have_populated_dt())
		gic_init_bases(0, IRQ_PPI(0), S5P_VA_GIC_DIST, S5P_VA_GIC_CPU, gic_bank_offset, NULL);
#ifdef CONFIG_OF
	else
		irqchip_init();
#endif

	if (!of_have_populated_dt())
		combiner_init(S5P_VA_COMBINER_BASE, NULL,
			      max_combiner_nr(), COMBINER_IRQ(0, 0));

	gic_arch_extn.irq_set_wake = s3c_irq_wake;
}

void __init exynos5_init_irq(void)
{
#ifdef CONFIG_OF
	irqchip_init();
#endif
	gic_arch_extn.irq_set_wake = s3c_irq_wake;
}

struct bus_type exynos_subsys = {
	.name		= "exynos-core",
	.dev_name	= "exynos-core",
};

static struct device exynos4_dev = {
	.bus	= &exynos_subsys,
};

static int __init exynos_core_init(void)
{
	return subsys_system_register(&exynos_subsys, NULL);
}
core_initcall(exynos_core_init);

#ifdef CONFIG_CACHE_L2X0
static int __init exynos4_l2x0_cache_init(void)
{
	int ret;

	if (soc_is_exynos5250() || soc_is_exynos5440())
		return 0;

	ret = l2x0_of_init(L2_AUX_VAL, L2_AUX_MASK);
	if (!ret) {
		l2x0_regs_phys = virt_to_phys(&l2x0_saved_regs);
		clean_dcache_area(&l2x0_regs_phys, sizeof(unsigned long));
		return 0;
	}

	if (!(__raw_readl(S5P_VA_L2CC + L2X0_CTRL) & 0x1)) {
		l2x0_saved_regs.phy_base = EXYNOS4_PA_L2CC;
		/* TAG, Data Latency Control: 2 cycles */
		l2x0_saved_regs.tag_latency = 0x110;

		if (soc_is_exynos4212() || soc_is_exynos4412())
			l2x0_saved_regs.data_latency = 0x120;
		else
			l2x0_saved_regs.data_latency = 0x110;

		l2x0_saved_regs.prefetch_ctrl = 0x30000007;
		l2x0_saved_regs.pwr_ctrl =
			(L2X0_DYNAMIC_CLK_GATING_EN | L2X0_STNDBY_MODE_EN);

		l2x0_regs_phys = virt_to_phys(&l2x0_saved_regs);

		__raw_writel(l2x0_saved_regs.tag_latency,
				S5P_VA_L2CC + L2X0_TAG_LATENCY_CTRL);
		__raw_writel(l2x0_saved_regs.data_latency,
				S5P_VA_L2CC + L2X0_DATA_LATENCY_CTRL);

		/* L2X0 Prefetch Control */
		__raw_writel(l2x0_saved_regs.prefetch_ctrl,
				S5P_VA_L2CC + L2X0_PREFETCH_CTRL);

		/* L2X0 Power Control */
		__raw_writel(l2x0_saved_regs.pwr_ctrl,
				S5P_VA_L2CC + L2X0_POWER_CTRL);

		clean_dcache_area(&l2x0_regs_phys, sizeof(unsigned long));
		clean_dcache_area(&l2x0_saved_regs, sizeof(struct l2x0_regs));
	}

	l2x0_init(S5P_VA_L2CC, L2_AUX_VAL, L2_AUX_MASK);
	return 0;
}
early_initcall(exynos4_l2x0_cache_init);
#endif

static int __init exynos_init(void)
{
	printk(KERN_INFO "EXYNOS: Initializing architecture\n");

	return device_register(&exynos4_dev);
}

/* uart registration process */

static void __init exynos4_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	struct s3c2410_uartcfg *tcfg = cfg;
	u32 ucnt;

	for (ucnt = 0; ucnt < no; ucnt++, tcfg++)
		tcfg->has_fracval = 1;

	s3c24xx_init_uartdevs("exynos4210-uart", exynos4_uart_resources, cfg, no);
}

static void __iomem *exynos_eint_base;

static DEFINE_SPINLOCK(eint_lock);

static unsigned int eint0_15_data[16];

static inline int exynos4_irq_to_gpio(unsigned int irq)
{
	if (irq < IRQ_EINT(0))
		return -EINVAL;

	irq -= IRQ_EINT(0);
	if (irq < 8)
		return EXYNOS4_GPX0(irq);

	irq -= 8;
	if (irq < 8)
		return EXYNOS4_GPX1(irq);

	irq -= 8;
	if (irq < 8)
		return EXYNOS4_GPX2(irq);

	irq -= 8;
	if (irq < 8)
		return EXYNOS4_GPX3(irq);

	return -EINVAL;
}

static inline int exynos5_irq_to_gpio(unsigned int irq)
{
	if (irq < IRQ_EINT(0))
		return -EINVAL;

	irq -= IRQ_EINT(0);
	if (irq < 8)
		return EXYNOS5_GPX0(irq);

	irq -= 8;
	if (irq < 8)
		return EXYNOS5_GPX1(irq);

	irq -= 8;
	if (irq < 8)
		return EXYNOS5_GPX2(irq);

	irq -= 8;
	if (irq < 8)
		return EXYNOS5_GPX3(irq);

	return -EINVAL;
}

static unsigned int exynos4_eint0_15_src_int[16] = {
	EXYNOS4_IRQ_EINT0,
	EXYNOS4_IRQ_EINT1,
	EXYNOS4_IRQ_EINT2,
	EXYNOS4_IRQ_EINT3,
	EXYNOS4_IRQ_EINT4,
	EXYNOS4_IRQ_EINT5,
	EXYNOS4_IRQ_EINT6,
	EXYNOS4_IRQ_EINT7,
	EXYNOS4_IRQ_EINT8,
	EXYNOS4_IRQ_EINT9,
	EXYNOS4_IRQ_EINT10,
	EXYNOS4_IRQ_EINT11,
	EXYNOS4_IRQ_EINT12,
	EXYNOS4_IRQ_EINT13,
	EXYNOS4_IRQ_EINT14,
	EXYNOS4_IRQ_EINT15,
};

static unsigned int exynos5_eint0_15_src_int[16] = {
	EXYNOS5_IRQ_EINT0,
	EXYNOS5_IRQ_EINT1,
	EXYNOS5_IRQ_EINT2,
	EXYNOS5_IRQ_EINT3,
	EXYNOS5_IRQ_EINT4,
	EXYNOS5_IRQ_EINT5,
	EXYNOS5_IRQ_EINT6,
	EXYNOS5_IRQ_EINT7,
	EXYNOS5_IRQ_EINT8,
	EXYNOS5_IRQ_EINT9,
	EXYNOS5_IRQ_EINT10,
	EXYNOS5_IRQ_EINT11,
	EXYNOS5_IRQ_EINT12,
	EXYNOS5_IRQ_EINT13,
	EXYNOS5_IRQ_EINT14,
	EXYNOS5_IRQ_EINT15,
};
static inline void exynos_irq_eint_mask(struct irq_data *data)
{
	u32 mask;

	spin_lock(&eint_lock);
	mask = __raw_readl(EINT_MASK(exynos_eint_base, data->irq));
	mask |= EINT_OFFSET_BIT(data->irq);
	__raw_writel(mask, EINT_MASK(exynos_eint_base, data->irq));
	spin_unlock(&eint_lock);
}

static void exynos_irq_eint_unmask(struct irq_data *data)
{
	u32 mask;

	spin_lock(&eint_lock);
	mask = __raw_readl(EINT_MASK(exynos_eint_base, data->irq));
	mask &= ~(EINT_OFFSET_BIT(data->irq));
	__raw_writel(mask, EINT_MASK(exynos_eint_base, data->irq));
	spin_unlock(&eint_lock);
}

static inline void exynos_irq_eint_ack(struct irq_data *data)
{
	__raw_writel(EINT_OFFSET_BIT(data->irq),
		     EINT_PEND(exynos_eint_base, data->irq));
}

static void exynos_irq_eint_maskack(struct irq_data *data)
{
	exynos_irq_eint_mask(data);
	exynos_irq_eint_ack(data);
}

static int exynos_irq_eint_set_type(struct irq_data *data, unsigned int type)
{
	int offs = EINT_OFFSET(data->irq);
	int shift;
	u32 ctrl, mask;
	u32 newvalue = 0;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		newvalue = S5P_IRQ_TYPE_EDGE_RISING;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		newvalue = S5P_IRQ_TYPE_EDGE_FALLING;
		break;

	case IRQ_TYPE_EDGE_BOTH:
		newvalue = S5P_IRQ_TYPE_EDGE_BOTH;
		break;

	case IRQ_TYPE_LEVEL_LOW:
		newvalue = S5P_IRQ_TYPE_LEVEL_LOW;
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		newvalue = S5P_IRQ_TYPE_LEVEL_HIGH;
		break;

	default:
		printk(KERN_ERR "No such irq type %d", type);
		return -EINVAL;
	}

	shift = (offs & 0x7) * 4;
	mask = 0x7 << shift;

	spin_lock(&eint_lock);
	ctrl = __raw_readl(EINT_CON(exynos_eint_base, data->irq));
	ctrl &= ~mask;
	ctrl |= newvalue << shift;
	__raw_writel(ctrl, EINT_CON(exynos_eint_base, data->irq));
	spin_unlock(&eint_lock);

	if (soc_is_exynos5250())
		s3c_gpio_cfgpin(exynos5_irq_to_gpio(data->irq), S3C_GPIO_SFN(0xf));
	else
		s3c_gpio_cfgpin(exynos4_irq_to_gpio(data->irq), S3C_GPIO_SFN(0xf));

	return 0;
}

static struct irq_chip exynos_irq_eint = {
	.name		= "exynos-eint",
	.irq_mask	= exynos_irq_eint_mask,
	.irq_unmask	= exynos_irq_eint_unmask,
	.irq_mask_ack	= exynos_irq_eint_maskack,
	.irq_ack	= exynos_irq_eint_ack,
	.irq_set_type	= exynos_irq_eint_set_type,
#ifdef CONFIG_PM
	.irq_set_wake	= s3c_irqext_wake,
#endif
};

/*
 * exynos4_irq_demux_eint
 *
 * This function demuxes the IRQ from from EINTs 16 to 31.
 * It is designed to be inlined into the specific handler
 * s5p_irq_demux_eintX_Y.
 *
 * Each EINT pend/mask registers handle eight of them.
 */
static inline void exynos_irq_demux_eint(unsigned int start)
{
	unsigned int irq;

	u32 status = __raw_readl(EINT_PEND(exynos_eint_base, start));
	u32 mask = __raw_readl(EINT_MASK(exynos_eint_base, start));

	status &= ~mask;
	status &= 0xff;

	while (status) {
		irq = fls(status) - 1;
		generic_handle_irq(irq + start);
		status &= ~(1 << irq);
	}
}

static void exynos_irq_demux_eint16_31(unsigned int irq, struct irq_desc *desc)
{
	struct irq_chip *chip = irq_get_chip(irq);
	chained_irq_enter(chip, desc);
	exynos_irq_demux_eint(IRQ_EINT(16));
	exynos_irq_demux_eint(IRQ_EINT(24));
	chained_irq_exit(chip, desc);
}

static void exynos_irq_eint0_15(unsigned int irq, struct irq_desc *desc)
{
	u32 *irq_data = irq_get_handler_data(irq);
	struct irq_chip *chip = irq_get_chip(irq);

	chained_irq_enter(chip, desc);
	generic_handle_irq(*irq_data);
	chained_irq_exit(chip, desc);
}

static int __init exynos_init_irq_eint(void)
{
	int irq;

#ifdef CONFIG_PINCTRL_SAMSUNG
	/*
	 * The Samsung pinctrl driver provides an integrated gpio/pinmux/pinconf
	 * functionality along with support for external gpio and wakeup
	 * interrupts. If the samsung pinctrl driver is enabled and includes
	 * the wakeup interrupt support, then the setting up external wakeup
	 * interrupts here can be skipped. This check here is temporary to
	 * allow exynos4 platforms that do not use Samsung pinctrl driver to
	 * co-exist with platforms that do. When all of the Samsung Exynos4
	 * platforms switch over to using the pinctrl driver, the wakeup
	 * interrupt support code here can be completely removed.
	 */
	static const struct of_device_id exynos_pinctrl_ids[] = {
		{ .compatible = "samsung,exynos4210-pinctrl", },
		{ .compatible = "samsung,exynos4x12-pinctrl", },
		{ .compatible = "samsung,exynos5250-pinctrl", },
	};
	struct device_node *pctrl_np, *wkup_np;
	const char *wkup_compat = "samsung,exynos4210-wakeup-eint";

	for_each_matching_node(pctrl_np, exynos_pinctrl_ids) {
		if (of_device_is_available(pctrl_np)) {
			wkup_np = of_find_compatible_node(pctrl_np, NULL,
							wkup_compat);
			if (wkup_np)
				return -ENODEV;
		}
	}
#endif
	if (soc_is_exynos5440())
		return 0;

	if (soc_is_exynos5250())
		exynos_eint_base = ioremap(EXYNOS5_PA_GPIO1, SZ_4K);
	else
		exynos_eint_base = ioremap(EXYNOS4_PA_GPIO2, SZ_4K);

	if (exynos_eint_base == NULL) {
		pr_err("unable to ioremap for EINT base address\n");
		return -ENOMEM;
	}

	for (irq = 0 ; irq <= 31 ; irq++) {
		irq_set_chip_and_handler(IRQ_EINT(irq), &exynos_irq_eint,
					 handle_level_irq);
		set_irq_flags(IRQ_EINT(irq), IRQF_VALID);
	}

	irq_set_chained_handler(EXYNOS_IRQ_EINT16_31, exynos_irq_demux_eint16_31);

	for (irq = 0 ; irq <= 15 ; irq++) {
		eint0_15_data[irq] = IRQ_EINT(irq);

		if (soc_is_exynos5250()) {
			irq_set_handler_data(exynos5_eint0_15_src_int[irq],
					     &eint0_15_data[irq]);
			irq_set_chained_handler(exynos5_eint0_15_src_int[irq],
						exynos_irq_eint0_15);
		} else {
			irq_set_handler_data(exynos4_eint0_15_src_int[irq],
					     &eint0_15_data[irq]);
			irq_set_chained_handler(exynos4_eint0_15_src_int[irq],
						exynos_irq_eint0_15);
		}
	}

	return 0;
}
arch_initcall(exynos_init_irq_eint);

static struct resource exynos4_pmu_resource[] = {
	DEFINE_RES_IRQ(EXYNOS4_IRQ_PMU),
	DEFINE_RES_IRQ(EXYNOS4_IRQ_PMU_CPU1),
#if defined(CONFIG_SOC_EXYNOS4412)
	DEFINE_RES_IRQ(EXYNOS4_IRQ_PMU_CPU2),
	DEFINE_RES_IRQ(EXYNOS4_IRQ_PMU_CPU3),
#endif
};

static struct platform_device exynos4_device_pmu = {
	.name		= "arm-pmu",
	.num_resources	= ARRAY_SIZE(exynos4_pmu_resource),
	.resource	= exynos4_pmu_resource,
};

static int __init exynos_armpmu_init(void)
{
	if (!of_have_populated_dt()) {
		if (soc_is_exynos4210() || soc_is_exynos4212())
			exynos4_device_pmu.num_resources = 2;
		platform_device_register(&exynos4_device_pmu);
	}

	return 0;
}
arch_initcall(exynos_armpmu_init);
