/*
 *  Based on documentation provided by Dave Jones. Thanks!
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *
 *  BIG FAT DISCLAIMER: Work in progress code. Possibly *dangerous*
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/ioport.h>
#include <linux/slab.h>

#include <asm/msr.h>
#include <asm/tsc.h>
#include <asm/timex.h>
#include <asm/io.h>
#include <asm/delay.h>

#define EPS_BRAND_C7M	0
#define EPS_BRAND_C7	1
#define EPS_BRAND_EDEN	2
#define EPS_BRAND_C3	3

struct eps_cpu_data {
	u32 fsb;
	struct cpufreq_frequency_table freq_table[];
};

static struct eps_cpu_data *eps_cpu[NR_CPUS];


static unsigned int eps_get(unsigned int cpu)
{
	struct eps_cpu_data *centaur;
	u32 lo, hi;

	if (cpu)
		return 0;
	centaur = eps_cpu[cpu];
	if (centaur == NULL)
		return 0;

	/* Return current frequency */
	rdmsr(MSR_IA32_PERF_STATUS, lo, hi);
	return centaur->fsb * ((lo >> 8) & 0xff);
}

static int eps_set_state(struct eps_cpu_data *centaur,
			 unsigned int cpu,
			 u32 dest_state)
{
	struct cpufreq_freqs freqs;
	u32 lo, hi;
	int err = 0;
	int i;

	freqs.old = eps_get(cpu);
	freqs.new = centaur->fsb * ((dest_state >> 8) & 0xff);
	freqs.cpu = cpu;
	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	/* Wait while CPU is busy */
	rdmsr(MSR_IA32_PERF_STATUS, lo, hi);
	i = 0;
	while (lo & ((1 << 16) | (1 << 17))) {
		udelay(16);
		rdmsr(MSR_IA32_PERF_STATUS, lo, hi);
		i++;
		if (unlikely(i > 64)) {
			err = -ENODEV;
			goto postchange;
		}
	}
	/* Set new multiplier and voltage */
	wrmsr(MSR_IA32_PERF_CTL, dest_state & 0xffff, 0);
	/* Wait until transition end */
	i = 0;
	do {
		udelay(16);
		rdmsr(MSR_IA32_PERF_STATUS, lo, hi);
		i++;
		if (unlikely(i > 64)) {
			err = -ENODEV;
			goto postchange;
		}
	} while (lo & ((1 << 16) | (1 << 17)));

	/* Return current frequency */
postchange:
	rdmsr(MSR_IA32_PERF_STATUS, lo, hi);
	freqs.new = centaur->fsb * ((lo >> 8) & 0xff);

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
	return err;
}

static int eps_target(struct cpufreq_policy *policy,
			       unsigned int target_freq,
			       unsigned int relation)
{
	struct eps_cpu_data *centaur;
	unsigned int newstate = 0;
	unsigned int cpu = policy->cpu;
	unsigned int dest_state;
	int ret;

	if (unlikely(eps_cpu[cpu] == NULL))
		return -ENODEV;
	centaur = eps_cpu[cpu];

	if (unlikely(cpufreq_frequency_table_target(policy,
			&eps_cpu[cpu]->freq_table[0],
			target_freq,
			relation,
			&newstate))) {
		return -EINVAL;
	}

	/* Make frequency transition */
	dest_state = centaur->freq_table[newstate].index & 0xffff;
	ret = eps_set_state(centaur, cpu, dest_state);
	if (ret)
		printk(KERN_ERR "eps: Timeout!\n");
	return ret;
}

static int eps_verify(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy,
			&eps_cpu[policy->cpu]->freq_table[0]);
}

static int eps_cpu_init(struct cpufreq_policy *policy)
{
	unsigned int i;
	u32 lo, hi;
	u64 val;
	u8 current_multiplier, current_voltage;
	u8 max_multiplier, max_voltage;
	u8 min_multiplier, min_voltage;
	u8 brand;
	u32 fsb;
	struct eps_cpu_data *centaur;
	struct cpufreq_frequency_table *f_table;
	int k, step, voltage;
	int ret;
	int states;

	if (policy->cpu != 0)
		return -ENODEV;

	/* Check brand */
	printk("eps: Detected VIA ");
	rdmsr(0x1153, lo, hi);
	brand = (((lo >> 2) ^ lo) >> 18) & 3;
	switch(brand) {
	case EPS_BRAND_C7M:
		printk("C7-M\n");
		break;
	case EPS_BRAND_C7:
		printk("C7\n");
		break;
	case EPS_BRAND_EDEN:
		printk("Eden\n");
		break;
	case EPS_BRAND_C3:
		printk("C3\n");
		return -ENODEV;
		break;
	}
	/* Enable Enhanced PowerSaver */
	rdmsrl(MSR_IA32_MISC_ENABLE, val);
	if (!(val & 1 << 16)) {
		val |= 1 << 16;
		wrmsrl(MSR_IA32_MISC_ENABLE, val);
		/* Can be locked at 0 */
		rdmsrl(MSR_IA32_MISC_ENABLE, val);
		if (!(val & 1 << 16)) {
			printk("eps: Can't enable Enhanced PowerSaver\n");
			return -ENODEV;
		}
	}

	/* Print voltage and multiplier */
	rdmsr(MSR_IA32_PERF_STATUS, lo, hi);
	current_voltage = lo & 0xff;
	printk("eps: Current voltage = %dmV\n", current_voltage * 16 + 700);
	current_multiplier = (lo >> 8) & 0xff;
	printk("eps: Current multiplier = %d\n", current_multiplier);

	/* Print limits */
	max_voltage = hi & 0xff;
	printk("eps: Highest voltage = %dmV\n", max_voltage * 16 + 700);
	max_multiplier = (hi >> 8) & 0xff;
	printk("eps: Highest multiplier = %d\n", max_multiplier);
	min_voltage = (hi >> 16) & 0xff;
	printk("eps: Lowest voltage = %dmV\n", min_voltage * 16 + 700);
	min_multiplier = (hi >> 24) & 0xff;
	printk("eps: Lowest multiplier = %d\n", min_multiplier);

	/* Sanity checks */
	if (current_multiplier == 0 || max_multiplier == 0
	    || min_multiplier == 0)
		return -EINVAL;
	if (current_multiplier > max_multiplier
	    || max_multiplier <= min_multiplier)
		return -EINVAL;
	if (current_voltage > 0x1c || max_voltage > 0x1c)
		return -EINVAL;
	if (max_voltage < min_voltage)
		return -EINVAL;

	/* Calc FSB speed */
	fsb = cpu_khz / current_multiplier;
	/* Calc number of p-states supported */
	if (brand == EPS_BRAND_C7M)
		states = max_multiplier - min_multiplier + 1;
	else
		states = 2;

	/* Allocate private data and frequency table for current cpu */
	centaur = kzalloc(sizeof(struct eps_cpu_data)
		    + (states + 1) * sizeof(struct cpufreq_frequency_table),
		    GFP_KERNEL);
	if (!centaur)
		return -ENOMEM;
	eps_cpu[0] = centaur;

	/* Copy basic values */
	centaur->fsb = fsb;

	/* Fill frequency and MSR value table */
	f_table = &centaur->freq_table[0];
	if (brand != EPS_BRAND_C7M) {
		f_table[0].frequency = fsb * min_multiplier;
		f_table[0].index = (min_multiplier << 8) | min_voltage;
		f_table[1].frequency = fsb * max_multiplier;
		f_table[1].index = (max_multiplier << 8) | max_voltage;
		f_table[2].frequency = CPUFREQ_TABLE_END;
	} else {
		k = 0;
		step = ((max_voltage - min_voltage) * 256)
			/ (max_multiplier - min_multiplier);
		for (i = min_multiplier; i <= max_multiplier; i++) {
			voltage = (k * step) / 256 + min_voltage;
			f_table[k].frequency = fsb * i;
			f_table[k].index = (i << 8) | voltage;
			k++;
		}
		f_table[k].frequency = CPUFREQ_TABLE_END;
	}

	policy->cpuinfo.transition_latency = 140000; /* 844mV -> 700mV in ns */
	policy->cur = fsb * current_multiplier;

	ret = cpufreq_frequency_table_cpuinfo(policy, &centaur->freq_table[0]);
	if (ret) {
		kfree(centaur);
		return ret;
	}

	cpufreq_frequency_table_get_attr(&centaur->freq_table[0], policy->cpu);
	return 0;
}

static int eps_cpu_exit(struct cpufreq_policy *policy)
{
	unsigned int cpu = policy->cpu;
	struct eps_cpu_data *centaur;
	u32 lo, hi;

	if (eps_cpu[cpu] == NULL)
		return -ENODEV;
	centaur = eps_cpu[cpu];

	/* Get max frequency */
	rdmsr(MSR_IA32_PERF_STATUS, lo, hi);
	/* Set max frequency */
	eps_set_state(centaur, cpu, hi & 0xffff);
	/* Bye */
	cpufreq_frequency_table_put_attr(policy->cpu);
	kfree(eps_cpu[cpu]);
	eps_cpu[cpu] = NULL;
	return 0;
}

static struct freq_attr* eps_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver eps_driver = {
	.verify		= eps_verify,
	.target		= eps_target,
	.init		= eps_cpu_init,
	.exit		= eps_cpu_exit,
	.get		= eps_get,
	.name		= "e_powersaver",
	.owner		= THIS_MODULE,
	.attr		= eps_attr,
};

static int __init eps_init(void)
{
	struct cpuinfo_x86 *c = cpu_data;

	/* This driver will work only on Centaur C7 processors with
	 * Enhanced SpeedStep/PowerSaver registers */
	if (c->x86_vendor != X86_VENDOR_CENTAUR
	    || c->x86 != 6 || c->x86_model != 10)
		return -ENODEV;
	if (!cpu_has(c, X86_FEATURE_EST))
		return -ENODEV;

	if (cpufreq_register_driver(&eps_driver))
		return -EINVAL;
	return 0;
}

static void __exit eps_exit(void)
{
	cpufreq_unregister_driver(&eps_driver);
}

MODULE_AUTHOR("Rafa³ Bilski <rafalbilski@interia.pl>");
MODULE_DESCRIPTION("Enhanced PowerSaver driver for VIA C7 CPU's.");
MODULE_LICENSE("GPL");

module_init(eps_init);
module_exit(eps_exit);
