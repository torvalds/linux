/*
 *	sc520_freq.c: cpufreq driver for the AMD Elan sc520
 *
 *	Copyright (C) 2005 Sean Young <sean@mess.org>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Based on elanfreq.c
 *
 *	2005-03-30: - initial revision
 */

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

#define PFX "sc520_freq: "

static struct cpufreq_frequency_table sc520_freq_table[] = {
	{0x01,	100000},
	{0x02,	133000},
	{0,	CPUFREQ_TABLE_END},
};

static unsigned int sc520_freq_get_cpu_frequency(unsigned int cpu)
{
	u8 clockspeed_reg = *cpuctl;

	switch (clockspeed_reg & 0x03) {
	default:
		printk(KERN_ERR PFX "error: cpuctl register has unexpected "
				"value %02x\n", clockspeed_reg);
	case 0x01:
		return 100000;
	case 0x02:
		return 133000;
	}
}

static void sc520_freq_set_cpu_state(unsigned int state)
{

	struct cpufreq_freqs	freqs;
	u8 clockspeed_reg;

	freqs.old = sc520_freq_get_cpu_frequency(0);
	freqs.new = sc520_freq_table[state].frequency;
	freqs.cpu = 0; /* AMD Elan is UP */

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	pr_debug("attempting to set frequency to %i kHz\n",
			sc520_freq_table[state].frequency);

	local_irq_disable();

	clockspeed_reg = *cpuctl & ~0x03;
	*cpuctl = clockspeed_reg | sc520_freq_table[state].index;

	local_irq_enable();

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
};

static int sc520_freq_verify(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, &sc520_freq_table[0]);
}

static int sc520_freq_target(struct cpufreq_policy *policy,
			    unsigned int target_freq,
			    unsigned int relation)
{
	unsigned int newstate = 0;

	if (cpufreq_frequency_table_target(policy, sc520_freq_table,
				target_freq, relation, &newstate))
		return -EINVAL;

	sc520_freq_set_cpu_state(newstate);

	return 0;
}


/*
 *	Module init and exit code
 */

static int sc520_freq_cpu_init(struct cpufreq_policy *policy)
{
	struct cpuinfo_x86 *c = &cpu_data(0);
	int result;

	/* capability check */
	if (c->x86_vendor != X86_VENDOR_AMD ||
	    c->x86 != 4 || c->x86_model != 9)
		return -ENODEV;

	/* cpuinfo and default policy values */
	policy->cpuinfo.transition_latency = 1000000; /* 1ms */
	policy->cur = sc520_freq_get_cpu_frequency(0);

	result = cpufreq_frequency_table_cpuinfo(policy, sc520_freq_table);
	if (result)
		return result;

	cpufreq_frequency_table_get_attr(sc520_freq_table, policy->cpu);

	return 0;
}


static int sc520_freq_cpu_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_put_attr(policy->cpu);
	return 0;
}


static struct freq_attr *sc520_freq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};


static struct cpufreq_driver sc520_freq_driver = {
	.get	= sc520_freq_get_cpu_frequency,
	.verify	= sc520_freq_verify,
	.target	= sc520_freq_target,
	.init	= sc520_freq_cpu_init,
	.exit	= sc520_freq_cpu_exit,
	.name	= "sc520_freq",
	.owner	= THIS_MODULE,
	.attr	= sc520_freq_attr,
};

static const struct x86_cpu_id sc520_ids[] = {
	{ X86_VENDOR_AMD, 4, 9 },
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
		printk(KERN_ERR "sc520_freq: error: failed to remap memory\n");
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

