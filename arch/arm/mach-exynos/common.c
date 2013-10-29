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
#include <linux/dma-mapping.h>

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
#include <mach/smc.h>

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
#include <plat/ace-core.h>
#include <plat/regs-serial.h>

#include "common.h"
#define L2_AUX_VAL 0x7C470001
#define L2_AUX_MASK 0xC200ffff

static const char name_exynos4210[] = "EXYNOS4210";
static const char name_exynos4212[] = "EXYNOS4212";
static const char name_exynos4412[] = "EXYNOS4412";
static const char name_exynos5250[] = "EXYNOS5250";
static const char name_exynos5410[] = "EXYNOS5410";

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
	}, {
		.idcode		= EXYNOS5410_SOC_ID,
		.idmask		= EXYNOS5_SOC_MASK,
		.map_io		= exynos5_map_io,
		.init_clocks	= exynos5_init_clocks,
		.init_uarts	= exynos_init_uarts,
		.init		= exynos_init,
		.name		= name_exynos5410,
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
		.virtual	= (unsigned long)S3C_VA_USB_HSPHY,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_HSPHY),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_AUDSS,
		.pfn		= __phys_to_pfn(EXYNOS_PA_AUDSS),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_PPMU_CPU,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_PPMU_CPU),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_PPMU_DMC0,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_PPMU_DMC0),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_PPMU_DMC1,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_PPMU_DMC1),
		.length		= SZ_4K,
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

static struct map_desc exynos4x12_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S5P_VA_DMC0,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_DMC0_4X12),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_DMC1,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_DMC1_4X12),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_SYSRAM,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_SYSRAM1),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_SYSRAM_NS,
		.pfn		= __phys_to_pfn(EXYNOS4_PA_SYSRAM_NS_4X12),
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
		.length		= 192 * SZ_1K,
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
		.length		= SZ_8K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_GIC_DIST,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_GIC_DIST),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_USB_HSPHY,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_HSPHY),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_AUDSS,
		.pfn		= __phys_to_pfn(EXYNOS_PA_AUDSS),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
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
		.virtual        = (unsigned long)S5P_VA_PPMU_RIGHT,
		.pfn            = __phys_to_pfn(EXYNOS5_PA_PPMU_RIGHT),
		.length         = SZ_8K,
		.type           = MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_FIMCLITE0,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_FIMC_LITE0),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_FIMCLITE1,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_FIMC_LITE1),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_FIMCLITE2,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_FIMC_LITE2),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_MIPICSI0,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_MIPI_CSIS0),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_MIPICSI1,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_MIPI_CSIS1),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_MIPICSI2,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_MIPI_CSIS2),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_DREXII,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_DREXII),
		.length 	= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_USB3_DRD0_PHY,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_USB3_DRD0_PHY),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_USB3_DRD1_PHY,
		.pfn		= __phys_to_pfn(EXYNOS5_PA_USB3_DRD1_PHY),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

static struct map_desc exynos5250_iodesc[] __initdata = {
	{
#ifdef CONFIG_ARM_TRUSTZONE
		.virtual	= (unsigned long)S5P_VA_SYSRAM_NS,
		.pfn		= __phys_to_pfn(EXYNOS5250_PA_SYSRAM_NS),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
#endif
	},
};

static struct map_desc exynos5410_iodesc[] __initdata = {
	{
#ifdef CONFIG_ARM_TRUSTZONE
		.virtual	= (unsigned long)S5P_VA_SYSRAM_NS,
		.pfn		= __phys_to_pfn(EXYNOS5410_PA_SYSRAM_NS),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
#endif
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

#define REG_CPU_STATE_ADDR	(S5P_VA_SYSRAM_NS + 0x28)

void set_boot_flag(unsigned int cpu, unsigned int mode)
{
	unsigned int tmp;

	tmp = __raw_readl(REG_CPU_STATE_ADDR + (cpu * 4));

	if (mode & BOOT_MODE_MASK)
		tmp &= ~BOOT_MODE_MASK;

	tmp |= mode;
	__raw_writel(tmp, REG_CPU_STATE_ADDR + (cpu * 4));
}

void clear_boot_flag(unsigned int cpu, unsigned int mode)
{
	unsigned int tmp;

	tmp = __raw_readl(REG_CPU_STATE_ADDR + (cpu * 4));
	tmp &= ~mode;
	__raw_writel(tmp, REG_CPU_STATE_ADDR + (cpu * 4));
}

#define REG_GIC_STATE_ADDR	(S5P_VA_SYSRAM_NS + 0x38)

unsigned int read_gic_flag(unsigned int cpu)
{
	return __raw_readl(REG_GIC_STATE_ADDR + (cpu << 2));
}

void clear_gic_flag(unsigned int cpu)
{
	unsigned int clear = 0x0;

	__raw_writel(clear, REG_GIC_STATE_ADDR + (cpu << 2));
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

	if (soc_is_exynos4210()) {
		iotable_init(exynos4210_iodesc, ARRAY_SIZE(exynos4210_iodesc));
		if (samsung_rev() == EXYNOS4210_REV_0)
			iotable_init(exynos4210_iodesc_rev_0,
				ARRAY_SIZE(exynos4210_iodesc_rev_0));
		else
			iotable_init(exynos4210_iodesc_rev_1,
				ARRAY_SIZE(exynos4210_iodesc_rev_1));
	} else {
		iotable_init(exynos4x12_iodesc, ARRAY_SIZE(exynos4x12_iodesc));
	}

	/* initialize device information early */
	exynos4_default_sdhci0();
	exynos4_default_sdhci1();
	exynos4_default_sdhci2();
	exynos4_default_sdhci3();

	if (soc_is_exynos4210())
		s3c_adc_setname("samsung-adc-v3");
	else
		s3c_adc_setname("samsung-adc-v4");

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
}

static void __init exynos5_map_io(void)
{
	iotable_init(exynos5_iodesc, ARRAY_SIZE(exynos5_iodesc));

	if (soc_is_exynos5250())
		iotable_init(exynos5250_iodesc, ARRAY_SIZE(exynos5250_iodesc));
	else
		iotable_init(exynos5410_iodesc, ARRAY_SIZE(exynos5410_iodesc));

	init_consistent_dma_size(14 << 20);

	s3c_device_i2c0.resource[0].start = EXYNOS5_PA_IIC(0);
	s3c_device_i2c0.resource[0].end   = EXYNOS5_PA_IIC(0) + SZ_4K - 1;
	s3c_device_i2c0.resource[1].start = EXYNOS5_IRQ_IIC;
	s3c_device_i2c0.resource[1].end   = EXYNOS5_IRQ_IIC;

	if (soc_is_exynos5250())
		s3c_adc_setname("samsung-adc-v4");
	else if (soc_is_exynos5410())
		s3c_adc_setname("samsung-adc-v5");

	s3c_sdhci_setname(0, "exynos4-sdhci");
	s3c_sdhci_setname(1, "exynos4-sdhci");
	s3c_sdhci_setname(2, "exynos4-sdhci");
	s3c_sdhci_setname(3, "exynos4-sdhci");

	/* The I2C bus controllers are directly compatible with s3c2440 */
	s3c_i2c0_setname("s3c2440-i2c");
	s3c_i2c1_setname("s3c2440-i2c");
	s3c_i2c2_setname("s3c2440-i2c");

	s5p_fb_setname(1, "exynos5-fb");
	s5p_hdmi_setname("exynos5-hdmi");

#ifdef CONFIG_S5P_DEV_ACE
	s5p_ace_setname("exynos-ace");
#endif
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
	exynos_register_audss_clocks();
}

static void __init exynos5_init_clocks(int xtal)
{
	printk(KERN_DEBUG "%s: initializing clocks\n", __func__);

	s3c24xx_register_baseclocks(xtal);
	s5p_register_clocks(xtal);

	if (soc_is_exynos5250()) {
		exynos5250_register_clocks();
		exynos5250_setup_clocks();
	} else if (soc_is_exynos5410()) {
		exynos5410_register_clocks();
		exynos5410_setup_clocks();
	}
	exynos_register_audss_clocks();
}

#define COMBINER_ENABLE_SET	0x0
#define COMBINER_ENABLE_CLEAR	0x4
#define COMBINER_INT_STATUS	0xC
#define COMBINER_IRQS		8

static DEFINE_SPINLOCK(irq_controller_lock);

struct combiner_chip_data {
	unsigned int irq_offset;
	unsigned int irq_mask;
	void __iomem *base;
	unsigned int parent_irq;
	struct cpumask affinity[COMBINER_IRQS];
	spinlock_t lock;
};

static struct combiner_chip_data combiner_data[MAX_COMBINER_NR];

static inline bool is_max_combiner_nr_bad(unsigned int combiner_nr)
{
	unsigned int max_nr;

	if (soc_is_exynos5250() || soc_is_exynos5410())
		max_nr = EXYNOS5_MAX_COMBINER_NR;
	else
		max_nr = EXYNOS4_MAX_COMBINER_NR;

	return (combiner_nr >= max_nr);
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

static void __init combiner_cascade_irq(unsigned int combiner_nr, unsigned int irq)
{
	if (is_max_combiner_nr_bad(combiner_nr))
		BUG();
	if (irq_set_handler_data(irq, &combiner_data[combiner_nr]) != 0)
		BUG();
	irq_set_chained_handler(irq, combiner_handle_cascade_irq);
}

static void combiner_resume(struct irq_data *data)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);

	__raw_writel(~gc->mask_cache, gc->reg_base + COMBINER_ENABLE_CLEAR);
	__raw_writel(gc->mask_cache, gc->reg_base + COMBINER_ENABLE_SET);
}

static int combiner_set_affinity(struct irq_data *d,
					const struct cpumask *dest, bool force)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct cpumask target_affinity;
	unsigned int i;
	unsigned int ret;
	unsigned long flags;
	unsigned int sub_irq = (d->irq - gc->irq_base) % COMBINER_IRQS;
	unsigned int combiner_nr = (unsigned int)gc->private +
				   (d->irq - gc->irq_base) / COMBINER_IRQS;

	if (is_max_combiner_nr_bad(combiner_nr)) {
		pr_err("unable to set irq affinity: bad combiner number\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&combiner_data[combiner_nr].lock, flags);

	cpumask_setall(&target_affinity);
	for (i = 0; i < COMBINER_IRQS; ++i) {
		if (unlikely(i == sub_irq))
			cpumask_and(&target_affinity,
				    &target_affinity,
				    dest);
		else
			cpumask_and(&target_affinity,
				    &target_affinity,
				    &combiner_data[combiner_nr].affinity[i]);
	}
	if (cpumask_empty(&target_affinity)) {
		pr_err("warning: denying unsafe change of irq cpu affinity\n");
		ret = -EINVAL;
		goto out;
	}

	ret = irq_set_affinity(combiner_data[combiner_nr].parent_irq,
			       &target_affinity);
	if (ret >= 0)
		cpumask_copy(&combiner_data[combiner_nr].affinity[sub_irq],
			     &target_affinity);
out:
	spin_unlock_irqrestore(&combiner_data[combiner_nr].lock, flags);
	return ret;
}

static void __init combiner_init(unsigned int combiner_nr, void __iomem *base,
				 unsigned int irq_start)
{
	unsigned int i;
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;

	if (is_max_combiner_nr_bad(combiner_nr))
		BUG();
	combiner_data[combiner_nr].base = base;
	combiner_data[combiner_nr].irq_offset = irq_start;
	combiner_data[combiner_nr].irq_mask = 0xff << ((combiner_nr % 4) << 3);
	combiner_data[combiner_nr].parent_irq = IRQ_SPI(combiner_nr);
	spin_lock_init(&combiner_data[combiner_nr].lock);
	for (i = 0; i < COMBINER_IRQS; ++i)
		cpumask_setall(&combiner_data[combiner_nr].affinity[i]);

	if ((combiner_nr % 4) == 0) {
		/* Disable all interrupts */

		__raw_writel(IRQ_MSK(32), base + COMBINER_ENABLE_CLEAR);

		gc = irq_alloc_generic_chip("combiner", 1, irq_start,
					    base, handle_level_irq);
		if (!gc) {
			pr_err("Failed to allocate combiner irq chip\n");
			return;
		}

		ct = gc->chip_types;
		ct->chip.irq_mask = irq_gc_mask_disable_reg;
		ct->chip.irq_unmask = irq_gc_unmask_enable_reg;
		ct->chip.irq_set_affinity = combiner_set_affinity;
		ct->chip.irq_resume = combiner_resume;
		ct->regs.enable = COMBINER_ENABLE_SET;
		ct->regs.disable = COMBINER_ENABLE_CLEAR;
		gc->private = (void *)combiner_nr;
		irq_setup_generic_chip(gc, IRQ_MSK(32), 0,
			IRQ_NOREQUEST | IRQ_NOPROBE, 0);
	}
}

#ifdef CONFIG_OF
static const struct of_device_id exynos4_dt_irq_match[] = {
	{ .compatible = "arm,cortex-a9-gic", .data = gic_of_init, },
	{},
};
#endif

static inline void __combiner_init(int combiner_gr, int irq)
{
	combiner_init(combiner_gr, (void __iomem *)S5P_VA_COMBINER(combiner_gr),
			COMBINER_IRQ(combiner_gr, 0));
	combiner_cascade_irq(combiner_gr, irq);
}

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
	gic_arch_extn.irq_set_wake = s3c_irq_wake;

	for (irq = 0; irq < EXYNOS4_MAX_COMBINER_COMMON_NR; irq++)
		__combiner_init(irq, IRQ_SPI(irq));

	if (soc_is_exynos4212()) {
		__combiner_init(16, EXYNOS4_IRQ_COMBINER_G16);
		__combiner_init(17, EXYNOS4_IRQ_COMBINER_G17);
	} else if (soc_is_exynos4412()) {
		__combiner_init(16, EXYNOS4_IRQ_COMBINER_G16);
		__combiner_init(17, EXYNOS4_IRQ_COMBINER_G17);
		__combiner_init(18, EXYNOS4_IRQ_COMBINER_G18);
		__combiner_init(19, EXYNOS4_IRQ_COMBINER_G19);
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

#ifdef CONFIG_OF
	of_irq_init(exynos4_dt_irq_match);
#else
	gic_init(0, IRQ_PPI(0), S5P_VA_GIC_DIST, S5P_VA_GIC_CPU);
#endif
	gic_arch_extn.irq_set_wake = s3c_irq_wake;

	for (irq = 0; irq < EXYNOS5_MAX_COMBINER_NR; irq++)
		__combiner_init(irq, IRQ_SPI(irq));

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
	if (soc_is_exynos5250() || soc_is_exynos5410())
		return subsys_system_register(&exynos5_subsys, NULL);
	else
		return subsys_system_register(&exynos4_subsys, NULL);
}
core_initcall(exynos_core_init);

#ifdef CONFIG_ARM_TRUSTZONE
#if defined(CONFIG_PL310_ERRATA_588369) || defined(CONFIG_PL310_ERRATA_727915)
static void exynos4_l2x0_set_debug(unsigned long val)
{
	exynos_smc(SMC_CMD_L2X0DEBUG, val, 0, 0);
}
#endif
#endif

#ifdef CONFIG_CACHE_L2X0
static int __init exynos4_l2x0_cache_init(void)
{
	int ret;

	if (soc_is_exynos5250() || soc_is_exynos5410())
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

		if (soc_is_exynos4212() || soc_is_exynos4412()) {
			l2x0_saved_regs.data_latency = 0x120;
			if (samsung_rev() >= EXYNOS4412_REV_1_0)
				l2x0_saved_regs.prefetch_ctrl = 0x71000007;
			else
				l2x0_saved_regs.prefetch_ctrl = 0x30000007;
		} else {
			l2x0_saved_regs.data_latency = 0x110;
			l2x0_saved_regs.prefetch_ctrl = 0x30000007;
		}

		l2x0_saved_regs.pwr_ctrl =
			(L2X0_DYNAMIC_CLK_GATING_EN | L2X0_STNDBY_MODE_EN);

		l2x0_regs_phys = virt_to_phys(&l2x0_saved_regs);

#ifdef CONFIG_ARM_TRUSTZONE
		exynos_smc(SMC_CMD_L2X0SETUP1, l2x0_saved_regs.tag_latency,
					       l2x0_saved_regs.data_latency,
					       l2x0_saved_regs.prefetch_ctrl);
		exynos_smc(SMC_CMD_L2X0SETUP2, l2x0_saved_regs.pwr_ctrl,
					       L2_AUX_VAL,
					       L2_AUX_MASK);
		exynos_smc(SMC_CMD_L2X0INVALL, 0, 0, 0);
		exynos_smc(SMC_CMD_L2X0CTRL, 1, 0, 0);
#else
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
#endif
		clean_dcache_area(&l2x0_regs_phys, sizeof(unsigned long));
		clean_dcache_area(&l2x0_saved_regs, sizeof(struct l2x0_regs));
	}

	l2x0_init(S5P_VA_L2CC, L2_AUX_VAL, L2_AUX_MASK);

#ifdef CONFIG_ARM_TRUSTZONE
#if defined(CONFIG_PL310_ERRATA_588369) || defined(CONFIG_PL310_ERRATA_727915)
	outer_cache.set_debug = exynos4_l2x0_set_debug;
#endif
#endif
	return 0;
}
early_initcall(exynos4_l2x0_cache_init);
#endif

static int __init exynos_init(void)
{
	printk(KERN_INFO "EXYNOS: Initializing architecture\n");

	if (soc_is_exynos5250() || soc_is_exynos5410())
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

	if (soc_is_exynos5250() || soc_is_exynos5410())
		s3c24xx_init_uartdevs("exynos4210-uart", exynos5_uart_resources, cfg, no);
	else
		s3c24xx_init_uartdevs("exynos4210-uart", exynos4_uart_resources, cfg, no);
}

void __iomem *exynos_eint_base;

static DEFINE_SPINLOCK(eint_lock);

static unsigned int eint0_15_data[16];

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

	if (type & IRQ_TYPE_EDGE_BOTH)
		__irq_set_handler_locked(data->irq, handle_edge_irq);
	else
		__irq_set_handler_locked(data->irq, handle_level_irq);

	s3c_gpio_cfgpin(irq_to_gpio(data->irq), S3C_GPIO_SFN(0xf));

	return 0;
}

static struct irq_chip exynos_irq_eint = {
	.name		= "exynos-eint",
	.irq_disable	= exynos_irq_eint_maskack,
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
	else if (soc_is_exynos5410())
		exynos_eint_base = ioremap(EXYNOS5410_PA_GPIO_RT, SZ_4K);
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

		if (soc_is_exynos5250() || soc_is_exynos5410()) {
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
