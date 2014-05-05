/*
 *  This file was based upon code in Powertweak Linux (http://powertweak.sf.net)
 *  (C) 2000-2003  Dave Jones, Arjan van de Ven, Janne Pänkälä,
 *                 Dominik Brodowski.
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
#include <linux/timex.h>
#include <linux/io.h>

#include <asm/cpu_device_id.h>
#include <asm/msr.h>

#define POWERNOW_IOPORT 0xfff0          /* it doesn't matter where, as long
					   as it is unused */

#define PFX "powernow-k6: "
static unsigned int                     busfreq;   /* FSB, in 10 kHz */
static unsigned int                     max_multiplier;

static unsigned int			param_busfreq = 0;
static unsigned int			param_max_multiplier = 0;

module_param_named(max_multiplier, param_max_multiplier, uint, S_IRUGO);
MODULE_PARM_DESC(max_multiplier, "Maximum multiplier (allowed values: 20 30 35 40 45 50 55 60)");

module_param_named(bus_frequency, param_busfreq, uint, S_IRUGO);
MODULE_PARM_DESC(bus_frequency, "Bus frequency in kHz");

/* Clock ratio multiplied by 10 - see table 27 in AMD#23446 */
static struct cpufreq_frequency_table clock_ratio[] = {
	{0, 60,  /* 110 -> 6.0x */ 0},
	{0, 55,  /* 011 -> 5.5x */ 0},
	{0, 50,  /* 001 -> 5.0x */ 0},
	{0, 45,  /* 000 -> 4.5x */ 0},
	{0, 40,  /* 010 -> 4.0x */ 0},
	{0, 35,  /* 111 -> 3.5x */ 0},
	{0, 30,  /* 101 -> 3.0x */ 0},
	{0, 20,  /* 100 -> 2.0x */ 0},
	{0, 0, CPUFREQ_TABLE_END}
};

static const u8 index_to_register[8] = { 6, 3, 1, 0, 2, 7, 5, 4 };
static const u8 register_to_index[8] = { 3, 2, 4, 1, 7, 6, 0, 5 };

static const struct {
	unsigned freq;
	unsigned mult;
} usual_frequency_table[] = {
	{ 400000, 40 },	// 100   * 4
	{ 450000, 45 }, // 100   * 4.5
	{ 475000, 50 }, //  95   * 5
	{ 500000, 50 }, // 100   * 5
	{ 506250, 45 }, // 112.5 * 4.5
	{ 533500, 55 }, //  97   * 5.5
	{ 550000, 55 }, // 100   * 5.5
	{ 562500, 50 }, // 112.5 * 5
	{ 570000, 60 }, //  95   * 6
	{ 600000, 60 }, // 100   * 6
	{ 618750, 55 }, // 112.5 * 5.5
	{ 660000, 55 }, // 120   * 5.5
	{ 675000, 60 }, // 112.5 * 6
	{ 720000, 60 }, // 120   * 6
};

#define FREQ_RANGE		3000

/**
 * powernow_k6_get_cpu_multiplier - returns the current FSB multiplier
 *
 * Returns the current setting of the frequency multiplier. Core clock
 * speed is frequency of the Front-Side Bus multiplied with this value.
 */
static int powernow_k6_get_cpu_multiplier(void)
{
	unsigned long invalue = 0;
	u32 msrval;

	local_irq_disable();

	msrval = POWERNOW_IOPORT + 0x1;
	wrmsr(MSR_K6_EPMR, msrval, 0); /* enable the PowerNow port */
	invalue = inl(POWERNOW_IOPORT + 0x8);
	msrval = POWERNOW_IOPORT + 0x0;
	wrmsr(MSR_K6_EPMR, msrval, 0); /* disable it again */

	local_irq_enable();

	return clock_ratio[register_to_index[(invalue >> 5)&7]].driver_data;
}

static void powernow_k6_set_cpu_multiplier(unsigned int best_i)
{
	unsigned long outvalue, invalue;
	unsigned long msrval;
	unsigned long cr0;

	/* we now need to transform best_i to the BVC format, see AMD#23446 */

	/*
	 * The processor doesn't respond to inquiry cycles while changing the
	 * frequency, so we must disable cache.
	 */
	local_irq_disable();
	cr0 = read_cr0();
	write_cr0(cr0 | X86_CR0_CD);
	wbinvd();

	outvalue = (1<<12) | (1<<10) | (1<<9) | (index_to_register[best_i]<<5);

	msrval = POWERNOW_IOPORT + 0x1;
	wrmsr(MSR_K6_EPMR, msrval, 0); /* enable the PowerNow port */
	invalue = inl(POWERNOW_IOPORT + 0x8);
	invalue = invalue & 0x1f;
	outvalue = outvalue | invalue;
	outl(outvalue, (POWERNOW_IOPORT + 0x8));
	msrval = POWERNOW_IOPORT + 0x0;
	wrmsr(MSR_K6_EPMR, msrval, 0); /* disable it again */

	write_cr0(cr0);
	local_irq_enable();
}

/**
 * powernow_k6_target - set the PowerNow! multiplier
 * @best_i: clock_ratio[best_i] is the target multiplier
 *
 *   Tries to change the PowerNow! multiplier
 */
static int powernow_k6_target(struct cpufreq_policy *policy,
		unsigned int best_i)
{

	if (clock_ratio[best_i].driver_data > max_multiplier) {
		printk(KERN_ERR PFX "invalid target frequency\n");
		return -EINVAL;
	}

	powernow_k6_set_cpu_multiplier(best_i);

	return 0;
}

static int powernow_k6_cpu_init(struct cpufreq_policy *policy)
{
	unsigned int i, f;
	unsigned khz;

	if (policy->cpu != 0)
		return -ENODEV;

	max_multiplier = 0;
	khz = cpu_khz;
	for (i = 0; i < ARRAY_SIZE(usual_frequency_table); i++) {
		if (khz >= usual_frequency_table[i].freq - FREQ_RANGE &&
		    khz <= usual_frequency_table[i].freq + FREQ_RANGE) {
			khz = usual_frequency_table[i].freq;
			max_multiplier = usual_frequency_table[i].mult;
			break;
		}
	}
	if (param_max_multiplier) {
		for (i = 0; (clock_ratio[i].frequency != CPUFREQ_TABLE_END); i++) {
			if (clock_ratio[i].driver_data == param_max_multiplier) {
				max_multiplier = param_max_multiplier;
				goto have_max_multiplier;
			}
		}
		printk(KERN_ERR "powernow-k6: invalid max_multiplier parameter, valid parameters 20, 30, 35, 40, 45, 50, 55, 60\n");
		return -EINVAL;
	}

	if (!max_multiplier) {
		printk(KERN_WARNING "powernow-k6: unknown frequency %u, cannot determine current multiplier\n", khz);
		printk(KERN_WARNING "powernow-k6: use module parameters max_multiplier and bus_frequency\n");
		return -EOPNOTSUPP;
	}

have_max_multiplier:
	param_max_multiplier = max_multiplier;

	if (param_busfreq) {
		if (param_busfreq >= 50000 && param_busfreq <= 150000) {
			busfreq = param_busfreq / 10;
			goto have_busfreq;
		}
		printk(KERN_ERR "powernow-k6: invalid bus_frequency parameter, allowed range 50000 - 150000 kHz\n");
		return -EINVAL;
	}

	busfreq = khz / max_multiplier;
have_busfreq:
	param_busfreq = busfreq * 10;

	/* table init */
	for (i = 0; (clock_ratio[i].frequency != CPUFREQ_TABLE_END); i++) {
		f = clock_ratio[i].driver_data;
		if (f > max_multiplier)
			clock_ratio[i].frequency = CPUFREQ_ENTRY_INVALID;
		else
			clock_ratio[i].frequency = busfreq * f;
	}

	/* cpuinfo and default policy values */
	policy->cpuinfo.transition_latency = 500000;

	return cpufreq_table_validate_and_show(policy, clock_ratio);
}


static int powernow_k6_cpu_exit(struct cpufreq_policy *policy)
{
	unsigned int i;

	for (i = 0; (clock_ratio[i].frequency != CPUFREQ_TABLE_END); i++) {
		if (clock_ratio[i].driver_data == max_multiplier) {
			struct cpufreq_freqs freqs;

			freqs.old = policy->cur;
			freqs.new = clock_ratio[i].frequency;
			freqs.flags = 0;

			cpufreq_freq_transition_begin(policy, &freqs);
			powernow_k6_target(policy, i);
			cpufreq_freq_transition_end(policy, &freqs, 0);
			break;
		}
	}
	return 0;
}

static unsigned int powernow_k6_get(unsigned int cpu)
{
	unsigned int ret;
	ret = (busfreq * powernow_k6_get_cpu_multiplier());
	return ret;
}

static struct cpufreq_driver powernow_k6_driver = {
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= powernow_k6_target,
	.init		= powernow_k6_cpu_init,
	.exit		= powernow_k6_cpu_exit,
	.get		= powernow_k6_get,
	.name		= "powernow-k6",
	.attr		= cpufreq_generic_attr,
};

static const struct x86_cpu_id powernow_k6_ids[] = {
	{ X86_VENDOR_AMD, 5, 12 },
	{ X86_VENDOR_AMD, 5, 13 },
	{}
};
MODULE_DEVICE_TABLE(x86cpu, powernow_k6_ids);

/**
 * powernow_k6_init - initializes the k6 PowerNow! CPUFreq driver
 *
 *   Initializes the K6 PowerNow! support. Returns -ENODEV on unsupported
 * devices, -EINVAL or -ENOMEM on problems during initiatization, and zero
 * on success.
 */
static int __init powernow_k6_init(void)
{
	if (!x86_match_cpu(powernow_k6_ids))
		return -ENODEV;

	if (!request_region(POWERNOW_IOPORT, 16, "PowerNow!")) {
		printk(KERN_INFO PFX "PowerNow IOPORT region already used.\n");
		return -EIO;
	}

	if (cpufreq_register_driver(&powernow_k6_driver)) {
		release_region(POWERNOW_IOPORT, 16);
		return -EINVAL;
	}

	return 0;
}


/**
 * powernow_k6_exit - unregisters AMD K6-2+/3+ PowerNow! support
 *
 *   Unregisters AMD K6-2+ / K6-3+ PowerNow! support.
 */
static void __exit powernow_k6_exit(void)
{
	cpufreq_unregister_driver(&powernow_k6_driver);
	release_region(POWERNOW_IOPORT, 16);
}


MODULE_AUTHOR("Arjan van de Ven, Dave Jones <davej@redhat.com>, "
		"Dominik Brodowski <linux@brodo.de>");
MODULE_DESCRIPTION("PowerNow! driver for AMD K6-2+ / K6-3+ processors.");
MODULE_LICENSE("GPL");

module_init(powernow_k6_init);
module_exit(powernow_k6_exit);
