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
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/sched.h>
#include <linux/serial_core.h>
#include <linux/of.h>
#include <linux/of_irq.h>

#include <asm/proc-fns.h>
#include <asm/exception.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/hardware/gic.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/cacheflush.h>

#include <mach/regs-irq.h>
#include <mach/regs-pmu.h>
#include <mach/regs-gpio.h>
#include <mach/pmu.h>

#include <plat/cpu.h>
#include <plat/clock.h>
#include <plat/devs.h>
#include <plat/pm.h>
#include <plat/sdhci.h>
#include <plat/gpio-cfg.h>
#include <plat/adc-core.h>
#include <plat/fb-core.h>
#include <plat/fimc-core.h>
#include <plat/iic-core.h>
#include <plat/tv-core.h>
#include <plat/regs-serial.h>

#include "common.h"
#define L2_AUX_VAL 0x7C470001
#define L2_AUX_MASK 0xC200ffff

static const char name_exynos4210[] = "EXYNOS4210";
static const char name_exynos4212[] = "EXYNOS4212";
static const char name_exynos4412[] = "EXYNOS4412";
static const char name_exynos5250[] = "EXYNOS5250";

static void exynos4_map_io(void);
static void exynos5_map_io(void);
static void exynos4_init_clocks(int xtal);
static void exynos5_init_clocks(int xtal);
static void exynos_init_uarts(struct s3c2410_uartcfg *cfg, int no);
static int exynos_init(void);

static struct cpu_table cpu_ids[] __initdata = {
	{
		.idcode		= EXYNOS4210_CPU_ID,
		.idmask		= EXYNOS4_CPU_MASK,
		.map_io		= exynos4_map_io,
		.init_clocks	= exynos4_init_clocks,
		.init_uarts	= exynos_init_uarts,
		.init		= exynos_init,
		.name		= name_exynos4210,
	}, {
		.idcode		= EXYNOS4212_CPU_ID,
		.idmask		= EXYNOS4_CPU_MASK,
		.map_io		= exynos4_map_io,
		.init_clocks	= exynos4_init_clocks,
		.init_uarts	= exynos_init_uarts,
		.init		= exynos_init,
		.name		= name_exynos4212,
	}, {
		.idcode		= EXYNOS4412_CPU_ID,
		.idmask		= EXYNOS4_CPU_MASK,
		.map_io		= exynos4_map_io,
		.init_clocks	= exynos4_init_clocks,
		.init_uarts	= exynos_init_uarts,
		.init		= exynos_init,
		.name		= name_exynos4412,
	}, {
		.idcode		= EXYNOS5250_SOC_ID,
		.idmask		= EXYNOS5_SOC_MASK,
		.map_io		= exynos5_map_io,
		.init_clocks	= exynos5_init_clocks,
		.init_uarts	= exynos_init_uarts,
		.init		= exynos_init,
		.name		= name_exynos5250,
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
		.virtual	= (unsigned long)S5P_VA_SYSTIMER,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_SYSTIMER),
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
		.virtual	= (unsigned long)S5P_VA_COMBINER_BASE,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_COMBINER),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_UART,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_UART),
		.length		= SZ_512K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_GIC_CPU,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_GIC_CPU),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_GIC_DIST,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_GIC_DIST),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	},
};

void exynos4_restart(char mode, const char *cmd)
{
	__raw_writel(0x1, S5P_SWRESET);
}

void exynos5_restart(char mode, const char *cmd)
{
	__raw_writel(0x1, EXYNOS_SWRESET);
}

/*
 * exynos_map_io
 *
 * register the standard cpu IO areas
 */

void __init exynos_init_io(struct map_desc *mach_desc, int size)
{
	/* initialize the io descriptors we need for initialization */
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

	/* The I2C bus controllers are directly compatible with s3c2440 */
	s3c_i2c0_setname("s3c2440-i2c");
	s3c_i2c1_setname("s3c2440-i2c");
	s3c_i2c2_setname("s3c2440-i2c");

	s5p_fb_setname(0, "exynos4-fb");
	s5p_hdmi_setname("exynos4-hdmi");
}

static void __init exynos5_map_io(void)
{
	iotable_init(exynos5_iodesc, ARRAY_SIZE(exynos5_iodesc));

	s3c_device_i2c0.resource[0].start = EXYNOS5_PA_IIC(0);
	s3c_device_i2c0.resource[0].end   = EXYNOS5_PA_IIC(0) + SZ_4K - 1;
	s3c_device_i2c0.resource[1].start = EXYNOS5_IRQ_IIC;
	s3c_device_i2c0.resource[1].end   = EXYNOS5_IRQ_IIC;

	/* The I2C bus controllers are directly compatible with s3c2440 */
	s3c_i2c0_setname("s3c2440-i2c");
	s3c_i2c1_setname("s3c2440-i2c");
	s3c_i2c2_setname("s3c2440-i2c");
}

static void __init exynos4_init_clocks(int xtal)
{
	printk(KERN_DEBUG "%s: initializing clocks\n", __func__);

	s3c24xx_register_baseclocks(xtal);
	s5p_register_clocks(xtal);

	if (soc_is_exynos4210())
		exynos4210_register_clocks();
	else if (soc_is_exynos4212() || soc_is_exynos4412())
		exynos4212_register_clocks();

	exynos4_register_clocks();
	exynos4_setup_clocks();
}

static void __init exynos5_init_clocks(int xtal)
{
	printk(KERN_DEBUG "%s: initializing clocks\n", __func__);

	s3c24xx_register_baseclocks(xtal);
	s5p_register_clocks(xtal);

	exynos5_register_clocks();
	exynos5_setup_clocks();
}

#define COMBINER_ENABLE_SET	0x0
#define COMBINER_ENABLE_CLEAR	0x4
#define COMBINER_INT_STATUS	0xC

static DEFINE_SPINLOCK(irq_controller_lock);

struct combiner_chip_data {
	unsigned int irq_offset;
	unsigned int irq_mask;
	void __iomem *base;
};

static struct combiner_chip_data combiner_data[MAX_COMBINER_NR];

static inline void __iomem *combiner_base(struct irq_data *data)
{
	struct combiner_chip_data *combiner_data =
		irq_data_get_irq_chip_data(data);

	return combiner_data->base;
}

static void combiner_mask_irq(struct irq_data *data)
{
	u32 mask = 1 << (data->irq % 32);

	__raw_writel(mask, combiner_base(data) + COMBINER_ENABLE_CLEAR);
}

static void combiner_unmask_irq(struct irq_data *data)
{
	u32 mask = 1 << (data->irq % 32);

	__raw_writel(mask, combiner_base(data) + COMBINER_ENABLE_SET);
}

static void combiner_handle_cascade_irq(unsigned int irq, struct irq_desc *desc)
{
	struct combiner_chip_data *chip_data = irq_get_handler_data(irq);
	struct irq_chip *chip = irq_get_chip(irq);
	unsigned int cascade_irq, combiner_irq;
	unsigned long status;

	chained_irq_enter(chip, desc);

	spin_lock(&irq_controller_lock);
	status = __raw_readl(chip_data->base + COMBINER_INT_STATUS);
	spin_unlock(&irq_controller_lock);
	status &= chip_data->irq_mask;

	if (status == 0)
		goto out;

	combiner_irq = __ffs(status);

	cascade_irq = combiner_irq + (chip_data->irq_offset & ~31);
	if (unlikely(cascade_irq >= NR_IRQS))
		do_bad_IRQ(cascade_irq, desc);
	else
		generic_handle_irq(cascade_irq);

 out:
	chained_irq_exit(chip, desc);
}

static struct irq_chip combiner_chip = {
	.name		= "COMBINER",
	.irq_mask	= combiner_mask_irq,
	.irq_unmask	= combiner_unmask_irq,
};

static void __init combiner_cascade_irq(unsigned int combiner_nr, unsigned int irq)
{
	unsigned int max_nr;

	if (soc_is_exynos5250())
		max_nr = EXYNOS5_MAX_COMBINER_NR;
	else
		max_nr = EXYNOS4_MAX_COMBINER_NR;

	if (combiner_nr >= max_nr)
		BUG();
	if (irq_set_handler_data(irq, &combiner_data[combiner_nr]) != 0)
		BUG();
	irq_set_chained_handler(irq, combiner_handle_cascade_irq);
}

static void __init combiner_init(unsigned int combiner_nr, void __iomem *base,
			  unsigned int irq_start)
{
	unsigned int i;
	unsigned int max_nr;

	if (soc_is_exynos5250())
		max_nr = EXYNOS5_MAX_COMBINER_NR;
	else
		max_nr = EXYNOS4_MAX_COMBINER_NR;

	if (combiner_nr >= max_nr)
		BUG();

	combiner_data[combiner_nr].base = base;
	combiner_data[combiner_nr].irq_offset = irq_start;
	combiner_data[combiner_nr].irq_mask = 0xff << ((combiner_nr % 4) << 3);

	/* Disable all interrupts */

	__raw_writel(combiner_data[combiner_nr].irq_mask,
		     base + COMBINER_ENABLE_CLEAR);

	/* Setup the Linux IRQ subsystem */

	for (i = irq_start; i < combiner_data[combiner_nr].irq_offset
				+ MAX_IRQ_IN_COMBINER; i++) {
		irq_set_chip_and_handler(i, &combiner_chip, handle_level_irq);
		irq_set_chip_data(i, &combiner_data[combiner_nr]);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
	}
}

#ifdef CONFIG_OF
static const struct of_device_id exynos4_dt_irq_match[] = {
	{ .compatible = "arm,cortex-a9-gic", .data = gic_of_init, },
	{},
};
#endif

void __init exynos4_init_irq(void)
{
	int irq;
	unsigned int gic_bank_offset;

	gic_bank_offset = soc_is_exynos4412() ? 0x4000 : 0x8000;

	if (!of_have_populated_dt())
		gic_init_bases(0, IRQ_PPI(0), S5P_VA_GIC_DIST, S5P_VA_GIC_CPU, gic_bank_offset, NULL);
#ifdef CONFIG_OF
	else
		of_irq_init(exynos4_dt_irq_match);
#endif

	for (irq = 0; irq < EXYNOS4_MAX_COMBINER_NR; irq++) {

		combiner_init(irq, (void __iomem *)S5P_VA_COMBINER(irq),
				COMBINER_IRQ(irq, 0));
		combiner_cascade_irq(irq, IRQ_SPI(irq));
	}

	/*
	 * The parameters of s5p_init_irq() are for VIC init.
	 * Theses parameters should be NULL and 0 because EXYNOS4
	 * uses GIC instead of VIC.
	 */
	s5p_init_irq(NULL, 0);
}

void __init exynos5_init_irq(void)
{
	int irq;

	gic_init(0, IRQ_PPI(0), S5P_VA_GIC_DIST, S5P_VA_GIC_CPU);

	for (irq = 0; irq < EXYNOS5_MAX_COMBINER_NR; irq++) {
		combiner_init(irq, (void __iomem *)S5P_VA_COMBINER(irq),
				COMBINER_IRQ(irq, 0));
		combiner_cascade_irq(irq, IRQ_SPI(irq));
	}

	/*
	 * The parameters of s5p_init_irq() are for VIC init.
	 * Theses parameters should be NULL and 0 because EXYNOS4
	 * uses GIC instead of VIC.
	 */
	s5p_init_irq(NULL, 0);
}

struct bus_type exynos4_subsys = {
	.name		= "exynos4-core",
	.dev_name	= "exynos4-core",
};

struct bus_type exynos5_subsys = {
	.name		= "exynos5-core",
	.dev_name	= "exynos5-core",
};

static struct device exynos4_dev = {
	.bus	= &exynos4_subsys,
};

static struct device exynos5_dev = {
	.bus	= &exynos5_subsys,
};

static int __init exynos_core_init(void)
{
	if (soc_is_exynos5250())
		return subsys_system_register(&exynos5_subsys, NULL);
	else
		return subsys_system_register(&exynos4_subsys, NULL);
}
core_initcall(exynos_core_init);

#ifdef CONFIG_CACHE_L2X0
static int __init exynos4_l2x0_cache_init(void)
{
	int ret;

	if (soc_is_exynos5250())
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

static int __init exynos5_l2_cache_init(void)
{
	unsigned int val;

	if (!soc_is_exynos5250())
		return 0;

	asm volatile("mrc p15, 0, %0, c1, c0, 0\n"
		     "bic %0, %0, #(1 << 2)\n"	/* cache disable */
		     "mcr p15, 0, %0, c1, c0, 0\n"
		     "mrc p15, 1, %0, c9, c0, 2\n"
		     : "=r"(val));

	val |= (1 << 9) | (1 << 5) | (2 << 6) | (2 << 0);

	asm volatile("mcr p15, 1, %0, c9, c0, 2\n" : : "r"(val));
	asm volatile("mrc p15, 0, %0, c1, c0, 0\n"
		     "orr %0, %0, #(1 << 2)\n"	/* cache enable */
		     "mcr p15, 0, %0, c1, c0, 0\n"
		     : : "r"(val));

	return 0;
}
early_initcall(exynos5_l2_cache_init);

static int __init exynos_init(void)
{
	printk(KERN_INFO "EXYNOS: Initializing architecture\n");

	if (soc_is_exynos5250())
		return device_register(&exynos5_dev);
	else
		return device_register(&exynos4_dev);
}

/* uart registration process */

static void __init exynos_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	struct s3c2410_uartcfg *tcfg = cfg;
	u32 ucnt;

	for (ucnt = 0; ucnt < no; ucnt++, tcfg++)
		tcfg->has_fracval = 1;

	if (soc_is_exynos5250())
		s3c24xx_init_uartdevs("exynos4210-uart", exynos5_uart_resources, cfg, no);
	else
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
	chip->irq_mask(&desc->irq_data);

	if (chip->irq_ack)
		chip->irq_ack(&desc->irq_data);

	generic_handle_irq(*irq_data);

	chip->irq_unmask(&desc->irq_data);
	chained_irq_exit(chip, desc);
}

static int __init exynos_init_irq_eint(void)
{
	int irq;

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
