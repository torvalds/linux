// SPDX-License-Identifier: GPL-2.0
/*
 * Static Memory Controller
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/syscore_ops.h>

#include <mach/hardware.h>
#include <mach/smemc.h>

#ifdef CONFIG_PM
static unsigned long msc[2];
static unsigned long sxcnfg, memclkcfg;
static unsigned long csadrcfg[4];

static int pxa3xx_smemc_suspend(void)
{
	msc[0] = __raw_readl(MSC0);
	msc[1] = __raw_readl(MSC1);
	sxcnfg = __raw_readl(SXCNFG);
	memclkcfg = __raw_readl(MEMCLKCFG);
	csadrcfg[0] = __raw_readl(CSADRCFG0);
	csadrcfg[1] = __raw_readl(CSADRCFG1);
	csadrcfg[2] = __raw_readl(CSADRCFG2);
	csadrcfg[3] = __raw_readl(CSADRCFG3);

	return 0;
}

static void pxa3xx_smemc_resume(void)
{
	__raw_writel(msc[0], MSC0);
	__raw_writel(msc[1], MSC1);
	__raw_writel(sxcnfg, SXCNFG);
	__raw_writel(memclkcfg, MEMCLKCFG);
	__raw_writel(csadrcfg[0], CSADRCFG0);
	__raw_writel(csadrcfg[1], CSADRCFG1);
	__raw_writel(csadrcfg[2], CSADRCFG2);
	__raw_writel(csadrcfg[3], CSADRCFG3);
	/* CSMSADRCFG wakes up in its default state (0), so we need to set it */
	__raw_writel(0x2, CSMSADRCFG);
}

static struct syscore_ops smemc_syscore_ops = {
	.suspend	= pxa3xx_smemc_suspend,
	.resume		= pxa3xx_smemc_resume,
};

static int __init smemc_init(void)
{
	if (cpu_is_pxa3xx()) {
		/*
		 * The only documentation we have on the
		 * Chip Select Configuration Register (CSMSADRCFG) is that
		 * it must be programmed to 0x2.
		 * Moreover, in the bit definitions, the second bit
		 * (CSMSADRCFG[1]) is called "SETALWAYS".
		 * Other bits are reserved in this register.
		 */
		__raw_writel(0x2, CSMSADRCFG);

		register_syscore_ops(&smemc_syscore_ops);
	}

	return 0;
}
subsys_initcall(smemc_init);
#endif
