/* linux/arch/arm/mach-exynos/cpu-exynos5.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/sched.h>
#include <linux/sysdev.h>
#include <linux/delay.h>

#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/proc-fns.h>

#include <plat/cpu.h>
#include <plat/clock.h>
#include <plat/devs.h>
#include <plat/fb-core.h>
#include <plat/exynos5.h>
#include <plat/sdhci.h>
#include <plat/pm.h>
#include <plat/iic-core.h>
#include <plat/tv-core.h>
#include <plat/ace-core.h>
#include <plat/reset.h>

#include <mach/regs-irq.h>
#include <mach/regs-pmu.h>
#include <mach/regs-pmu5.h>
#include <mach/smc.h>

unsigned int gic_bank_offset __read_mostly;

extern int combiner_init(unsigned int combiner_nr, void __iomem *base,
			 unsigned int irq_start);
extern void combiner_cascade_irq(unsigned int combiner_nr, unsigned int irq);

/* Initial IO mappings */
static struct map_desc exynos5_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S5P_VA_SYSTIMER,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_SYSTIMER),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual        = (unsigned long)S5P_VA_SYSRAM,
		.pfn            = __phys_to_pfn(EXYNOS5_PA_SYSRAM),
		.length         = SZ_4K,
		.type           = MT_DEVICE,
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
		.pfn		= __phys_to_pfn(S3C_PA_UART),
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
	}, {
		.virtual	= (unsigned long)S5P_VA_GPIO1,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_GPIO1),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_GPIO2,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_GPIO2),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_GPIO3,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_GPIO3),
		.length		= SZ_256,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_GPIO4,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_GPIO4),
		.length		= SZ_256,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_AUDSS,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_AUDSS),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual        = (unsigned long)S3C_VA_USB_HSPHY,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_HSPHY),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual        = (unsigned long)S5P_VA_SS_PHY,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_SS_PHY),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
#ifdef CONFIG_ARM_TRUSTZONE
	}, {
		.virtual	= (unsigned long)S5P_VA_SYSRAM_NS,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_SYSRAM_NS),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
#endif
	}, {
		.virtual        = (unsigned long)S5P_VA_PPMU_CPU,
		.pfn            = __phys_to_pfn(EXYNOS5_PA_PPMU_CPU),
		.length         = SZ_8K,
		.type		= MT_DEVICE,
	}, {
		.virtual        = (unsigned long)S5P_VA_PPMU_DDR_C,
		.pfn            = __phys_to_pfn(EXYNOS5_PA_PPMU_DDR_C),
		.length         = SZ_8K,
		.type           = MT_DEVICE,
	}, {
		.virtual        = (unsigned long)S5P_VA_PPMU_DDR_R1,
		.pfn            = __phys_to_pfn(EXYNOS5_PA_PPMU_DDR_R1),
		.length         = SZ_8K,
		.type           = MT_DEVICE,
	}, {
		.virtual        = (unsigned long)S5P_VA_PPMU_DDR_L,
		.pfn            = __phys_to_pfn(EXYNOS5_PA_PPMU_DDR_L),
		.length         = SZ_8K,
		.type           = MT_DEVICE,
	}, {
		.virtual        = (unsigned long)S5P_VA_PPMU_RIGHT0_BUS,
		.pfn            = __phys_to_pfn(EXYNOS5_PA_PPMU_RIGHT0_BUS),
		.length         = SZ_8K,
		.type           = MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_FIMCLITE0,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_FIMC_LITE0),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_MIPICSI0,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_MIPI_CSIS0),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

static void exynos5_idle(void)
{
	if (!need_resched())
		cpu_do_idle();

	local_irq_enable();
}

/*
 * exynos5_map_io
 *
 * register the standard cpu IO areas
 */
void __init exynos5_map_io(void)
{
	iotable_init(exynos5_iodesc, ARRAY_SIZE(exynos5_iodesc));

#ifdef CONFIG_S3C_DEV_HSMMC
	exynos5_default_sdhci0();
#endif
#ifdef CONFIG_S3C_DEV_HSMMC1
	exynos5_default_sdhci1();
#endif
#ifdef CONFIG_S3C_DEV_HSMMC2
	exynos5_default_sdhci2();
#endif
#ifdef CONFIG_S3C_DEV_HSMMC3
	exynos5_default_sdhci3();
#endif

	s5p_fb_setname(1, "exynos5-fb");        /* FIMD1 */

	s5p_hdmi_setname("exynos5-hdmi");

	/* The I2C bus controllers are directly compatible with s3c2440 */
	s3c_i2c0_setname("s3c2440-i2c");
	s3c_i2c1_setname("s3c2440-i2c");
	s3c_i2c2_setname("s3c2440-i2c");

#ifdef CONFIG_S5P_DEV_ACE
	s5p_ace_setname("exynos4-ace");
#endif
}

void __init exynos5_init_clocks(int xtal)
{
	printk(KERN_DEBUG "%s: initializing clocks\n", __func__);

	s3c24xx_register_baseclocks(xtal);

	s5p_register_clocks(xtal);
	exynos5_register_clocks();
	exynos5_setup_clocks();
}

void __init exynos5_init_irq(void)
{
	int irq;

	gic_init(0, IRQ_PPI(0), S5P_VA_GIC_DIST, S5P_VA_GIC_CPU);
	gic_arch_extn.irq_set_wake = s3c_irq_wake;

	for (irq = 0; irq < MAX_COMBINER_NR; irq++) {
		combiner_init(irq, (void __iomem *)S5P_VA_COMBINER(irq),
				COMBINER_IRQ(irq, 0));
		combiner_cascade_irq(irq, IRQ_SPI(irq));
	}

	/* The parameters of s5p_init_irq() are for VIC init.
	 * Theses parameters should be NULL and 0 because EXYNOS5
	 * uses GIC instead of VIC.
	 */
	s5p_init_irq(NULL, 0);
}

struct sysdev_class exynos5_sysclass = {
	.name	= "exynos5-core",
};

static struct sys_device exynos5_sysdev = {
	.cls	= &exynos5_sysclass,
};

static int __init exynos5_core_init(void)
{
	return sysdev_class_register(&exynos5_sysclass);
}

core_initcall(exynos5_core_init);

#define TAG_RAM_SETUP_SHIFT		(9)
#define DATA_RAM_SETUP_SHIFT		(5)
#define TAG_RAM_LATENCY_SHIFT		(6)
#define DATA_RAM_LATENCY_SHIFT		(0)

static int __init exynos5_l2_cache_init(void)
{
	unsigned int val;

	if (soc_is_exynos5250()) {
		asm volatile(
			"mrc p15, 0, %0, c1, c0, 0\n"
			"bic %0, %0, #(1 << 2)\n"	/* cache disable */
			"mcr p15, 0, %0, c1, c0, 0\n"
			"mrc p15, 1, %0, c9, c0, 2\n"
			: "=r"(val));

		val |= (1 << TAG_RAM_SETUP_SHIFT) |
			(1 << DATA_RAM_SETUP_SHIFT) |
			(2 << TAG_RAM_LATENCY_SHIFT) |
			(2 << DATA_RAM_LATENCY_SHIFT);

#ifdef CONFIG_ARM_TRUSTZONE
		exynos_smc(SMC_CMD_REG, SMC_REG_ID_CP15(9, 1, 0, 2), val, 0);
#else
		asm volatile("mcr p15, 1, %0, c9, c0, 2\n": : "r"(val));
#endif
		asm volatile(
			"mrc p15, 0, %0, c1, c0, 0\n"
			"orr %0, %0, #(1 << 2)\n"	/* cache enable */
			"mcr p15, 0, %0, c1, c0, 0\n"
			: : "r"(val));
	}

	return 0;
}

early_initcall(exynos5_l2_cache_init);

static void exynos5_sw_reset(void)
{
	int count = 3;

	while (count--) {
		__raw_writel(0x1, S5P_SWRESET);
		mdelay(500);
	}
}

int __init exynos5_init(void)
{
	unsigned int value;
	printk(KERN_INFO "EXYNOS5: Initializing architecture\n");

	/* set idle function */
	pm_idle = exynos5_idle;

	/* set sw_reset function */
	s5p_reset_hook = exynos5_sw_reset;

	value = __raw_readl(EXYNOS5_AUTOMATIC_WDT_RESET_DISABLE);
	value &= ~EXYNOS5_SYS_WDTRESET;
	__raw_writel(value, EXYNOS5_AUTOMATIC_WDT_RESET_DISABLE);
	value = __raw_readl(EXYNOS5_MASK_WDT_RESET_REQUEST);
	value &= ~EXYNOS5_SYS_WDTRESET;
	__raw_writel(value, EXYNOS5_MASK_WDT_RESET_REQUEST);

	return sysdev_register(&exynos5_sysdev);
}
