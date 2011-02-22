/*
 * Static Memory Controller
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/sysdev.h>

#include <mach/hardware.h>
#include <mach/smemc.h>

#ifdef CONFIG_PM
static unsigned long msc[2];
static unsigned long sxcnfg, memclkcfg;
static unsigned long csadrcfg[4];

static int pxa3xx_smemc_suspend(struct sys_device *dev, pm_message_t state)
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

static int pxa3xx_smemc_resume(struct sys_device *dev)
{
	__raw_writel(msc[0], MSC0);
	__raw_writel(msc[1], MSC1);
	__raw_writel(sxcnfg, SXCNFG);
	__raw_writel(memclkcfg, MEMCLKCFG);
	__raw_writel(csadrcfg[0], CSADRCFG0);
	__raw_writel(csadrcfg[1], CSADRCFG1);
	__raw_writel(csadrcfg[2], CSADRCFG2);
	__raw_writel(csadrcfg[3], CSADRCFG3);

	return 0;
}

static struct sysdev_class smemc_sysclass = {
	.name		= "smemc",
	.suspend	= pxa3xx_smemc_suspend,
	.resume		= pxa3xx_smemc_resume,
};

static struct sys_device smemc_sysdev = {
	.id		= 0,
	.cls		= &smemc_sysclass,
};

static int __init smemc_init(void)
{
	int ret = 0;

	if (cpu_is_pxa3xx()) {
		ret = sysdev_class_register(&smemc_sysclass);
		if (ret)
			return ret;

		ret = sysdev_register(&smemc_sysdev);
	}

	return ret;
}
subsys_initcall(smemc_init);
#endif
