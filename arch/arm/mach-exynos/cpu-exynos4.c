/* linux/arch/arm/mach-exynos/cpu.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
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
#include <asm/hardware/cache-l2x0.h>
#include <asm/hardware/gic.h>
#include <asm/cacheflush.h>

#include <plat/cpu.h>
#include <plat/clock.h>
#include <plat/devs.h>
#include <plat/fb-core.h>
#include <plat/exynos4.h>
#include <plat/sdhci.h>
#include <plat/mshci.h>
#include <plat/fimc-core.h>
#include <plat/adc-core.h>
#include <plat/pm.h>
#include <plat/iic-core.h>
#include <plat/ace-core.h>
#include <plat/reset.h>
#include <plat/audio.h>
#include <plat/tv-core.h>
#include <plat/rtc-core.h>

#include <mach/regs-irq.h>
#include <mach/regs-pmu.h>
#include <mach/smc.h>

unsigned int gic_bank_offset __read_mostly;

extern int combiner_init(unsigned int combiner_nr, void __iomem *base,
			 unsigned int irq_start);
extern void combiner_cascade_irq(unsigned int combiner_nr, unsigned int irq);

/* Initial IO mappings */
static struct map_desc exynos4_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S5P_VA_SYSTIMER,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_SYSTIMER),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_CMU,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_CMU),
		.length		= SZ_128K,
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
		.virtual	= (unsigned long)S5P_VA_GPIO1,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_GPIO1),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_GPIO2,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_GPIO2),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_GPIO3,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_GPIO3),
		.length		= SZ_256,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_GPIO4,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_GPIO4),
		.length		= SZ_256,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_UART,
		.pfn		= __phys_to_pfn(S3C_PA_UART),
		.length		= SZ_512K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_SROMC,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_SROMC),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_USB_HSPHY,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_HSPHY),
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
		.virtual        = (unsigned long)S5P_VA_AUDSS,
		.pfn            = __phys_to_pfn(EXYNOS4_PA_AUDSS),
		.length         = SZ_4K,
		.type           = MT_DEVICE,
	}, {
		.virtual        = (unsigned long)S5P_VA_PPMU_CPU,
		.pfn            = __phys_to_pfn(EXYNOS4_PA_PPMU_CPU),
		.length         = SZ_8K,
		.type           = MT_DEVICE,
	}, {
		.virtual        = (unsigned long)S5P_VA_PPMU_DMC0,
		.pfn            = __phys_to_pfn(EXYNOS4_PA_PPMU_DMC0),
		.length         = SZ_8K,
		.type           = MT_DEVICE,
	}, {
		.virtual        = (unsigned long)S5P_VA_PPMU_DMC1,
		.pfn            = __phys_to_pfn(EXYNOS4_PA_PPMU_DMC1),
		.length         = SZ_8K,
		.type           = MT_DEVICE,
	}, {
		.virtual        = (unsigned long)S5P_VA_GDL,
		.pfn            = __phys_to_pfn(EXYNOS4_PA_GDL),
		.length         = SZ_4K,
		.type           = MT_DEVICE,
	}, {
		.virtual        = (unsigned long)S5P_VA_GDR,
		.pfn            = __phys_to_pfn(EXYNOS4_PA_GDR),
		.length         = SZ_4K,
		.type           = MT_DEVICE,
	},
};

static struct map_desc exynos4210_iodesc[] __initdata = {
	{
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
		.virtual	= (unsigned long)S5P_VA_SYSRAM_NS,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_SYSRAM_NS),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

static struct map_desc exynos4210_iodesc_rev_0[] __initdata = {
	{
		.virtual	= (unsigned long)S5P_VA_SYSRAM,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_SYSRAM0),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

static struct map_desc exynos4210_iodesc_rev_1[] __initdata = {
	{
		.virtual	= (unsigned long)S5P_VA_SYSRAM,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_SYSRAM1),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

static struct map_desc exynos4212_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S5P_VA_DMC0,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_DMC0_4212),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_DMC1,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_DMC1_4212),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_SYSRAM,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_SYSRAM1),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_SYSRAM_NS,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_SYSRAM_NS_4212),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

static void exynos4_idle(void)
{
	if (!need_resched())
		cpu_do_idle();

	local_irq_enable();
}

/*
 * exynos4_map_io
 *
 * register the standard cpu IO areas
 */
void __init exynos4_map_io(void)
{
	iotable_init(exynos4_iodesc, ARRAY_SIZE(exynos4_iodesc));

	if (soc_is_exynos4210()) {
		iotable_init(exynos4210_iodesc, ARRAY_SIZE(exynos4210_iodesc));
		if (samsung_rev() == EXYNOS4210_REV_0)
			iotable_init(exynos4210_iodesc_rev_0,
				ARRAY_SIZE(exynos4210_iodesc_rev_0));
		else
			iotable_init(exynos4210_iodesc_rev_1,
				ARRAY_SIZE(exynos4210_iodesc_rev_1));
	} else {
		iotable_init(exynos4212_iodesc, ARRAY_SIZE(exynos4212_iodesc));
	}

#ifdef CONFIG_S3C_DEV_HSMMC
	exynos4_default_sdhci0();
#endif
#ifdef CONFIG_S3C_DEV_HSMMC1
	exynos4_default_sdhci1();
#endif
#ifdef CONFIG_S3C_DEV_HSMMC2
	exynos4_default_sdhci2();
#endif
#ifdef CONFIG_S3C_DEV_HSMMC3
	exynos4_default_sdhci3();
#endif
#ifdef CONFIG_EXYNOS4_DEV_MSHC
	exynos4_default_mshci();
#endif
	exynos4_i2sv3_setup_resource();

	s3c_fimc_setname(0, "exynos4-fimc");
	s3c_fimc_setname(1, "exynos4-fimc");
	s3c_fimc_setname(2, "exynos4-fimc");
	s3c_fimc_setname(3, "exynos4-fimc");
#ifdef CONFIG_S3C_DEV_RTC
	s3c_rtc_setname("exynos-rtc");
#endif
#ifdef CONFIG_FB_S3C
	s5p_fb_setname(0, "exynos4-fb");	/* FIMD0 */
#endif
	if (soc_is_exynos4210())
		s3c_adc_setname("samsung-adc-v3");
	else
		s3c_adc_setname("samsung-adc-v4");

	s5p_hdmi_setname("exynos4-hdmi");

	/* The I2C bus controllers are directly compatible with s3c2440 */
	s3c_i2c0_setname("s3c2440-i2c");
	s3c_i2c1_setname("s3c2440-i2c");
	s3c_i2c2_setname("s3c2440-i2c");

#ifdef CONFIG_S5P_DEV_ACE
	s5p_ace_setname("exynos-ace");
#endif
}

void __init exynos4_init_clocks(int xtal)
{
	printk(KERN_DEBUG "%s: initializing clocks\n", __func__);

	s3c24xx_register_baseclocks(xtal);

	if (soc_is_exynos4210())
		exynos4210_register_clocks();
	else
		exynos4212_register_clocks();

	s5p_register_clocks(xtal);
	exynos4_register_clocks();
	exynos4_setup_clocks();
}

#define COMBINER_MAP(x)	((x < 16) ? IRQ_SPI(x) :	\
			(x == 16) ? IRQ_SPI(107) :	\
			(x == 17) ? IRQ_SPI(108) :	\
			(x == 18) ? IRQ_SPI(48) :	\
			(x == 19) ? IRQ_SPI(49) : 0)

void __init exynos4_init_irq(void)
{
	int irq;

	gic_bank_offset = soc_is_exynos4412() ? 0x4000 : 0x8000;

	gic_init(0, IRQ_PPI_MCT_L, S5P_VA_GIC_DIST, S5P_VA_GIC_CPU);
	gic_arch_extn.irq_set_wake = s3c_irq_wake;

	for (irq = 0; irq < COMMON_COMBINER_NR; irq++) {
		combiner_init(irq, (void __iomem *)S5P_VA_COMBINER(irq),
				COMBINER_IRQ(irq, 0));
		combiner_cascade_irq(irq, COMBINER_MAP(irq));
	}

	if (soc_is_exynos4412() && (samsung_rev() >= EXYNOS4412_REV_1_0)) {
		for (irq = COMMON_COMBINER_NR; irq < MAX_COMBINER_NR; irq++) {
			combiner_init(irq, (void __iomem *)S5P_VA_COMBINER(irq),
					COMBINER_IRQ(irq, 0));
			combiner_cascade_irq(irq, COMBINER_MAP(irq));
		}
	}

	/* The parameters of s5p_init_irq() are for VIC init.
	 * Theses parameters should be NULL and 0 because EXYNOS4
	 * uses GIC instead of VIC.
	 */
	s5p_init_irq(NULL, 0);
}

struct sysdev_class exynos4_sysclass = {
	.name	= "exynos4-core",
};

static struct sys_device exynos4_sysdev = {
	.cls	= &exynos4_sysclass,
};

static int __init exynos4_core_init(void)
{
	return sysdev_class_register(&exynos4_sysclass);
}

core_initcall(exynos4_core_init);

#ifdef CONFIG_CACHE_L2X0
#ifdef CONFIG_ARM_TRUSTZONE
#if defined(CONFIG_PL310_ERRATA_588369) || defined(CONFIG_PL310_ERRATA_727915)
static void exynos4_l2x0_set_debug(unsigned long val)
{
	exynos_smc(SMC_CMD_L2X0DEBUG, val, 0, 0);
}
#endif
#endif

static int __init exynos4_l2x0_cache_init(void)
{
	u32 tag_latency = 0x110;
	u32 data_latency = soc_is_exynos4210() ? 0x110 : 0x120;
	u32 prefetch = (soc_is_exynos4412() &&
			samsung_rev() >= EXYNOS4412_REV_1_0) ?
			0x71000007 : 0x30000007;
	u32 aux_val = 0x7C470001;
	u32 aux_mask = 0xC200FFFF;

#ifdef CONFIG_ARM_TRUSTZONE
	exynos_smc(SMC_CMD_L2X0SETUP1, tag_latency, data_latency, prefetch);
	exynos_smc(SMC_CMD_L2X0SETUP2,
		   L2X0_DYNAMIC_CLK_GATING_EN | L2X0_STNDBY_MODE_EN,
		   aux_val, aux_mask);
	exynos_smc(SMC_CMD_L2X0INVALL, 0, 0, 0);
	exynos_smc(SMC_CMD_L2X0CTRL, 1, 0, 0);
#else
	__raw_writel(tag_latency, S5P_VA_L2CC + L2X0_TAG_LATENCY_CTRL);
	__raw_writel(data_latency, S5P_VA_L2CC + L2X0_DATA_LATENCY_CTRL);
	__raw_writel(prefetch, S5P_VA_L2CC + L2X0_PREFETCH_CTRL);
	__raw_writel(L2X0_DYNAMIC_CLK_GATING_EN | L2X0_STNDBY_MODE_EN,
		     S5P_VA_L2CC + L2X0_POWER_CTRL);
#endif

	l2x0_init(S5P_VA_L2CC, aux_val, aux_mask);

#ifdef CONFIG_ARM_TRUSTZONE
#if defined(CONFIG_PL310_ERRATA_588369) || defined(CONFIG_PL310_ERRATA_727915)
	outer_cache.set_debug = exynos4_l2x0_set_debug;
#endif
#endif
	/* Enable the full line of zero */
	enable_cache_foz();
	return 0;
}

//early_initcall(exynos4_l2x0_cache_init);
early_initcall(exynos4_l2x0_cache_init);
#endif

static void exynos4_sw_reset(void)
{
	int count = 3;

	while (count--) {
		__raw_writel(0x1, S5P_SWRESET);
		mdelay(500);
	}
}

static void __iomem *exynos4_pmu_init_zero[] = {
	S5P_CMU_RESET_ISP_SYS,
	S5P_CMU_SYSCLK_ISP_SYS,
};

int __init exynos4_init(void)
{
	unsigned int value;
	unsigned int tmp;
	unsigned int i;

	printk(KERN_INFO "EXYNOS4: Initializing architecture\n");

	/* set idle function */
	pm_idle = exynos4_idle;

	/*
	 * on exynos4x12, CMU reset system power register should to be set 0x0
	 */
	if (!soc_is_exynos4210()) {
		for (i = 0; i < ARRAY_SIZE(exynos4_pmu_init_zero); i++)
			__raw_writel(0x0, exynos4_pmu_init_zero[i]);
	}

	/* set sw_reset function */
	s5p_reset_hook = exynos4_sw_reset;

	/* Disable auto wakeup from power off mode */
	for (i = 0; i < num_possible_cpus(); i++) {
		tmp = __raw_readl(S5P_ARM_CORE_OPTION(i));
		tmp &= ~S5P_CORE_OPTION_DIS;
		__raw_writel(tmp, S5P_ARM_CORE_OPTION(i));
	}

	if (soc_is_exynos4212() || soc_is_exynos4412()) {
		value = __raw_readl(S5P_AUTOMATIC_WDT_RESET_DISABLE);
		value &= ~S5P_SYS_WDTRESET;
		__raw_writel(value, S5P_AUTOMATIC_WDT_RESET_DISABLE);
		value = __raw_readl(S5P_MASK_WDT_RESET_REQUEST);
		value &= ~S5P_SYS_WDTRESET;
		__raw_writel(value, S5P_MASK_WDT_RESET_REQUEST);
	}

	return sysdev_register(&exynos4_sysdev);
}
