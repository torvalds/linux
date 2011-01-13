/* linux/arch/arm/mach-s5pv310/cpu.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/sched.h>
#include <linux/sysdev.h>

#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/proc-fns.h>
#include <asm/hardware/cache-l2x0.h>

#include <plat/cpu.h>
#include <plat/clock.h>
#include <plat/s5pv310.h>
#include <plat/sdhci.h>

#include <mach/regs-irq.h>

extern int combiner_init(unsigned int combiner_nr, void __iomem *base,
			 unsigned int irq_start);
extern void combiner_cascade_irq(unsigned int combiner_nr, unsigned int irq);

/* Initial IO mappings */
static struct map_desc s5pv310_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S5P_VA_SYSRAM,
		.pfn		= __phys_to_pfn(S5PV310_PA_SYSRAM),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_CMU,
		.pfn		= __phys_to_pfn(S5PV310_PA_CMU),
		.length		= SZ_128K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_COMBINER_BASE,
		.pfn		= __phys_to_pfn(S5PV310_PA_COMBINER),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_COREPERI_BASE,
		.pfn		= __phys_to_pfn(S5PV310_PA_COREPERI),
		.length		= SZ_8K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_L2CC,
		.pfn		= __phys_to_pfn(S5PV310_PA_L2CC),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_GPIO1,
		.pfn		= __phys_to_pfn(S5PV310_PA_GPIO1),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_GPIO2,
		.pfn		= __phys_to_pfn(S5PV310_PA_GPIO2),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_GPIO3,
		.pfn		= __phys_to_pfn(S5PV310_PA_GPIO3),
		.length		= SZ_256,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_UART,
		.pfn		= __phys_to_pfn(S3C_PA_UART),
		.length		= SZ_512K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_SROMC,
		.pfn		= __phys_to_pfn(S5PV310_PA_SROMC),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

static void s5pv310_idle(void)
{
	if (!need_resched())
		cpu_do_idle();

	local_irq_enable();
}

/* s5pv310_map_io
 *
 * register the standard cpu IO areas
*/
void __init s5pv310_map_io(void)
{
	iotable_init(s5pv310_iodesc, ARRAY_SIZE(s5pv310_iodesc));

	/* initialize device information early */
	s5pv310_default_sdhci0();
	s5pv310_default_sdhci1();
	s5pv310_default_sdhci2();
	s5pv310_default_sdhci3();
}

void __init s5pv310_init_clocks(int xtal)
{
	printk(KERN_DEBUG "%s: initializing clocks\n", __func__);

	s3c24xx_register_baseclocks(xtal);
	s5p_register_clocks(xtal);
	s5pv310_register_clocks();
	s5pv310_setup_clocks();
}

void __init s5pv310_init_irq(void)
{
	int irq;

	gic_init(0, IRQ_LOCALTIMER, S5P_VA_GIC_DIST, S5P_VA_GIC_CPU);

	for (irq = 0; irq < MAX_COMBINER_NR; irq++) {
		combiner_init(irq, (void __iomem *)S5P_VA_COMBINER(irq),
				COMBINER_IRQ(irq, 0));
		combiner_cascade_irq(irq, IRQ_SPI(irq));
	}

	/* The parameters of s5p_init_irq() are for VIC init.
	 * Theses parameters should be NULL and 0 because S5PV310
	 * uses GIC instead of VIC.
	 */
	s5p_init_irq(NULL, 0);
}

struct sysdev_class s5pv310_sysclass = {
	.name	= "s5pv310-core",
};

static struct sys_device s5pv310_sysdev = {
	.cls	= &s5pv310_sysclass,
};

static int __init s5pv310_core_init(void)
{
	return sysdev_class_register(&s5pv310_sysclass);
}

core_initcall(s5pv310_core_init);

#ifdef CONFIG_CACHE_L2X0
static int __init s5pv310_l2x0_cache_init(void)
{
	/* TAG, Data Latency Control: 2cycle */
	__raw_writel(0x110, S5P_VA_L2CC + L2X0_TAG_LATENCY_CTRL);
	__raw_writel(0x110, S5P_VA_L2CC + L2X0_DATA_LATENCY_CTRL);

	/* L2X0 Prefetch Control */
	__raw_writel(0x30000007, S5P_VA_L2CC + L2X0_PREFETCH_CTRL);

	/* L2X0 Power Control */
	__raw_writel(L2X0_DYNAMIC_CLK_GATING_EN | L2X0_STNDBY_MODE_EN,
		     S5P_VA_L2CC + L2X0_POWER_CTRL);

	l2x0_init(S5P_VA_L2CC, 0x7C070001, 0xC200ffff);

	return 0;
}

early_initcall(s5pv310_l2x0_cache_init);
#endif

int __init s5pv310_init(void)
{
	printk(KERN_INFO "S5PV310: Initializing architecture\n");

	/* set idle function */
	pm_idle = s5pv310_idle;

	return sysdev_register(&s5pv310_sysdev);
}
