// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	sc520_freq.c: cpufreq driver for the AMD Elan sc520
 *
 *	Copyright (C) 2005 Sean Young <sean@mess.org>
 *
 *	Based on elanfreq.c
 *
 *	2005-03-30: - initial revision
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/delay.h>
#include <linux/cpufreq.h>
#include <linux/timex.h>
#include <linux/io.h>

#include <asm/cpu_device_id.h>
#include <asm/msr.h>

#define MMCR_BASE	0xfffef000	/* The default base address */
#define OFFS_CPUCTL	0x2   /* CPU Control Register */

static __u8 __iomem *cpuctl;

static struct cpufreq_frequency_table sc520_freq_table[] = {
	{0, 0x01,	100000},
	{0, 0x02,	133000},
	{0, 0,	CPUFREQ_TABLE_END},
};

static unsigned int sc520_freq_get_cpu_frequency(unsigned int cpu)
{
	u8 clockspeed_reg = *cpuctl;

	switch (clockspeed_reg & 0x03) {
	default:
		pr_err("error: cpuctl register has unexpected value %02x\n",
		       clockspeed_reg);
	case 0x01:
		return 100000;
	case 0x02:
		return 133000;
	}
}

static int sc520_freq_target(struct cpufreq_policy *policy, unsigned int state)
{

	u8 clockspeed_reg;

	local_irq_disable();

	clockspeed_reg = *cpuctl & ~0x03;
	*cpuctl = clockspeed_reg | sc520_freq_table[state].driver_data;

	local_irq_enable();

	return 0;
}

/*
 *	Module init and exit code
 */

static int sc520_freq_cpu_init(struct cpufreq_policy *policy)
{
	struct cpuinfo_x86 *c = &cpu_data(0);

	/* capability check */
	if (c->x86_vendor != X86_VENDOR_AMD ||
	    c->x86 != 4 || c->x86_model != 9)
		return -ENODEV;

	/* cpuinfo and default policy values */
	policy->cpuinfo.transition_latency = 1000000; /* 1ms */
	policy->freq_table = sc520_freq_table;

	return 0;
}


static struct cpufreq_driver sc520_freq_driver = {
	.get	= sc520_freq_get_cpu_frequency,
	.verify	= cpufreq_generic_frequency_table_verify,
	.target_index = sc520_freq_target,
	.init	= sc520_freq_cpu_init,
	.name	= "sc520_freq",
	.attr	= cpufreq_generic_attr,
};

static const struct x86_cpu_id sc520_ids[] = {
	X86_MATCH_VENDOR_FAM_MODEL(AMD, 4, 9, NULL),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, sc520_ids);

static int __init sc520_freq_init(void)
{
	int err;

	if (!x86_match_cpu(sc520_ids))
		return -ENODEV;

	cpuctl = ioremap((unsigned long)(MMCR_BASE + OFFS_CPUCTL), 1);
	if (!cpuctl) {
		pr_err("sc520_freq: error: failed to remap memory\n");
		return -ENOMEM;
	}

	err = cpufreq_register_driver(&sc520_freq_driver);
	if (err)
		iounmap(cpuctl);

	return err;
}


static void __exit sc520_freq_exit(void)
{
	cpufreq_unregister_driver(&sc520_freq_driver);
	iounmap(cpuctl);
}


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sean Young <sean@mess.org>");
MODULE_DESCRIPTION("cpufreq driver for AMD's Elan sc520 CPU");

module_init(sc520_freq_init);
module_exit(sc520_freq_exit);

