/*
 * Copyright 2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/cpufreq.h>

#include <asm/delay.h>
#include <asm/dpmc.h>

#define DRIVER_NAME "bfin dpmc"

struct bfin_dpmc_platform_data *pdata;

/**
 *	bfin_set_vlev - Update VLEV field in VR_CTL Reg.
 *			Avoid BYPASS sequence
 */
static void bfin_set_vlev(unsigned int vlev)
{
	unsigned pll_lcnt;

	pll_lcnt = bfin_read_PLL_LOCKCNT();

	bfin_write_PLL_LOCKCNT(1);
	bfin_write_VR_CTL((bfin_read_VR_CTL() & ~VLEV) | vlev);
	bfin_write_PLL_LOCKCNT(pll_lcnt);
}

/**
 *	bfin_get_vlev - Get CPU specific VLEV from platform device data
 */
static unsigned int bfin_get_vlev(unsigned int freq)
{
	int i;

	if (!pdata)
		goto err_out;

	freq >>= 16;

	for (i = 0; i < pdata->tabsize; i++)
		if (freq <= (pdata->tuple_tab[i] & 0xFFFF))
			return pdata->tuple_tab[i] >> 16;

err_out:
	printk(KERN_WARNING "DPMC: No suitable CCLK VDDINT voltage pair found\n");
	return VLEV_120;
}

#ifdef CONFIG_CPU_FREQ
# ifdef CONFIG_SMP
static void bfin_idle_this_cpu(void *info)
{
	unsigned long flags = 0;
	unsigned long iwr0, iwr1, iwr2;
	unsigned int cpu = smp_processor_id();

	local_irq_save_hw(flags);
	bfin_iwr_set_sup0(&iwr0, &iwr1, &iwr2);

	platform_clear_ipi(cpu, IRQ_SUPPLE_0);
	SSYNC();
	asm("IDLE;");
	bfin_iwr_restore(iwr0, iwr1, iwr2);

	local_irq_restore_hw(flags);
}

static void bfin_idle_cpu(void)
{
	smp_call_function(bfin_idle_this_cpu, NULL, 0);
}

static void bfin_wakeup_cpu(void)
{
	unsigned int cpu;
	unsigned int this_cpu = smp_processor_id();
	cpumask_t mask;

	cpumask_copy(&mask, cpu_online_mask);
	cpumask_clear_cpu(this_cpu, &mask);
	for_each_cpu(cpu, &mask)
		platform_send_ipi_cpu(cpu, IRQ_SUPPLE_0);
}

# else
static void bfin_idle_cpu(void) {}
static void bfin_wakeup_cpu(void) {}
# endif

static int
vreg_cpufreq_notifier(struct notifier_block *nb, unsigned long val, void *data)
{
	struct cpufreq_freqs *freq = data;

	if (freq->cpu != CPUFREQ_CPU)
		return 0;

	if (val == CPUFREQ_PRECHANGE && freq->old < freq->new) {
		bfin_idle_cpu();
		bfin_set_vlev(bfin_get_vlev(freq->new));
		udelay(pdata->vr_settling_time); /* Wait until Volatge settled */
		bfin_wakeup_cpu();
	} else if (val == CPUFREQ_POSTCHANGE && freq->old > freq->new) {
		bfin_idle_cpu();
		bfin_set_vlev(bfin_get_vlev(freq->new));
		bfin_wakeup_cpu();
	}

	return 0;
}

static struct notifier_block vreg_cpufreq_notifier_block = {
	.notifier_call	= vreg_cpufreq_notifier
};
#endif /* CONFIG_CPU_FREQ */

/**
 *	bfin_dpmc_probe -
 *
 */
static int __devinit bfin_dpmc_probe(struct platform_device *pdev)
{
	if (pdev->dev.platform_data)
		pdata = pdev->dev.platform_data;
	else
		return -EINVAL;

	return cpufreq_register_notifier(&vreg_cpufreq_notifier_block,
					 CPUFREQ_TRANSITION_NOTIFIER);
}

/**
 *	bfin_dpmc_remove -
 */
static int __devexit bfin_dpmc_remove(struct platform_device *pdev)
{
	pdata = NULL;
	return cpufreq_unregister_notifier(&vreg_cpufreq_notifier_block,
					 CPUFREQ_TRANSITION_NOTIFIER);
}

struct platform_driver bfin_dpmc_device_driver = {
	.probe   = bfin_dpmc_probe,
	.remove  = __devexit_p(bfin_dpmc_remove),
	.driver  = {
		.name = DRIVER_NAME,
	}
};
module_platform_driver(bfin_dpmc_device_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("cpu power management driver for Blackfin");
MODULE_LICENSE("GPL");
