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
static const char name_exynos5440[] = "EXYNOS5440";

static void exynos4_map_io(void);
static void exynos5_map_io(void);
static void exynos5440_map_io(void);
static int exynos_init(void);

unsigned long xxti_f = 0, xusbxti_f = 0;

static struct cpu_table cpu_ids[] __initdata = {
	{
		.idcode		= EXYNOS4210_CPU_ID,
		.idmask		= EXYNOS4_CPU_MASK,
		.map_io		= exynos4_map_io,
		.init		= exynos_init,
		.name		= name_exynos4210,
	}, {
		.idcode		= EXYNOS4212_CPU_ID,
		.idmask		= EXYNOS4_CPU_MASK,
		.map_io		= exynos4_map_io,
		.init		= exynos_init,
		.name		= name_exynos4212,
	}, {
		.idcode		= EXYNOS4412_CPU_ID,
		.idmask		= EXYNOS4_CPU_MASK,
		.map_io		= exynos4_map_io,
		.init		= exynos_init,
		.name		= name_exynos4412,
	}, {
		.idcode		= EXYNOS5250_SOC_ID,
		.idmask		= EXYNOS5_SOC_MASK,
		.map_io		= exynos5_map_io,
		.init		= exynos_init,
		.name		= name_exynos5250,
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

/*
 * exynos_map_io
 *
 * register the standard cpu IO areas
 */

void __init exynos_init_io(struct map_desc *mach_desc, int size)
{
	debug_ll_io_init();

	if (initial_boot_params)
		of_scan_flat_dt(exynos_fdt_map_chipid, NULL);
	else
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

void __init exynos_init_time(void)
{
	of_clk_init(NULL);
	clocksource_of_init();
}

void __init exynos4_init_irq(void)
{
	irqchip_init();
}

void __init exynos5_init_irq(void)
{
	irqchip_init();
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
