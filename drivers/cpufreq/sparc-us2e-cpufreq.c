/* us2e_cpufreq.c: UltraSPARC-IIe cpu frequency support
 *
 * Copyright (C) 2003 David S. Miller (davem@redhat.com)
 *
 * Many thanks to Dominik Brodowski for fixing up the cpufreq
 * infrastructure in order to make this driver easier to implement.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/cpufreq.h>
#include <linux/threads.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <asm/asi.h>
#include <asm/timer.h>

static struct cpufreq_driver *cpufreq_us2e_driver;

struct us2e_freq_percpu_info {
	struct cpufreq_frequency_table table[6];
};

/* Indexed by cpu number. */
static struct us2e_freq_percpu_info *us2e_freq_table;

#define HBIRD_MEM_CNTL0_ADDR	0x1fe0000f010UL
#define HBIRD_ESTAR_MODE_ADDR	0x1fe0000f080UL

/* UltraSPARC-IIe has five dividers: 1, 2, 4, 6, and 8.  These are controlled
 * in the ESTAR mode control register.
 */
#define ESTAR_MODE_DIV_1	0x0000000000000000UL
#define ESTAR_MODE_DIV_2	0x0000000000000001UL
#define ESTAR_MODE_DIV_4	0x0000000000000003UL
#define ESTAR_MODE_DIV_6	0x0000000000000002UL
#define ESTAR_MODE_DIV_8	0x0000000000000004UL
#define ESTAR_MODE_DIV_MASK	0x0000000000000007UL

#define MCTRL0_SREFRESH_ENAB	0x0000000000010000UL
#define MCTRL0_REFR_COUNT_MASK	0x0000000000007f00UL
#define MCTRL0_REFR_COUNT_SHIFT	8
#define MCTRL0_REFR_INTERVAL	7800
#define MCTRL0_REFR_CLKS_P_CNT	64

static unsigned long read_hbreg(unsigned long addr)
{
	unsigned long ret;

	__asm__ __volatile__("ldxa	[%1] %2, %0"
			     : "=&r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E));
	return ret;
}

static void write_hbreg(unsigned long addr, unsigned long val)
{
	__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
			     "membar	#Sync"
			     : /* no outputs */
			     : "r" (val), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E)
			     : "memory");
	if (addr == HBIRD_ESTAR_MODE_ADDR) {
		/* Need to wait 16 clock cycles for the PLL to lock.  */
		udelay(1);
	}
}

static void self_refresh_ctl(int enable)
{
	unsigned long mctrl = read_hbreg(HBIRD_MEM_CNTL0_ADDR);

	if (enable)
		mctrl |= MCTRL0_SREFRESH_ENAB;
	else
		mctrl &= ~MCTRL0_SREFRESH_ENAB;
	write_hbreg(HBIRD_MEM_CNTL0_ADDR, mctrl);
	(void) read_hbreg(HBIRD_MEM_CNTL0_ADDR);
}

static void frob_mem_refresh(int cpu_slowing_down,
			     unsigned long clock_tick,
			     unsigned long old_divisor, unsigned long divisor)
{
	unsigned long old_refr_count, refr_count, mctrl;

	refr_count  = (clock_tick * MCTRL0_REFR_INTERVAL);
	refr_count /= (MCTRL0_REFR_CLKS_P_CNT * divisor * 1000000000UL);

	mctrl = read_hbreg(HBIRD_MEM_CNTL0_ADDR);
	old_refr_count = (mctrl & MCTRL0_REFR_COUNT_MASK)
		>> MCTRL0_REFR_COUNT_SHIFT;

	mctrl &= ~MCTRL0_REFR_COUNT_MASK;
	mctrl |= refr_count << MCTRL0_REFR_COUNT_SHIFT;
	write_hbreg(HBIRD_MEM_CNTL0_ADDR, mctrl);
	mctrl = read_hbreg(HBIRD_MEM_CNTL0_ADDR);

	if (cpu_slowing_down && !(mctrl & MCTRL0_SREFRESH_ENAB)) {
		unsigned long usecs;

		/* We have to wait for both refresh counts (old
		 * and new) to go to zero.
		 */
		usecs = (MCTRL0_REFR_CLKS_P_CNT *
			 (refr_count + old_refr_count) *
			 1000000UL *
			 old_divisor) / clock_tick;
		udelay(usecs + 1UL);
	}
}

static void us2e_transition(unsigned long estar, unsigned long new_bits,
			    unsigned long clock_tick,
			    unsigned long old_divisor, unsigned long divisor)
{
	unsigned long flags;

	local_irq_save(flags);

	estar &= ~ESTAR_MODE_DIV_MASK;

	/* This is based upon the state transition diagram in the IIe manual.  */
	if (old_divisor == 2 && divisor == 1) {
		self_refresh_ctl(0);
		write_hbreg(HBIRD_ESTAR_MODE_ADDR, estar | new_bits);
		frob_mem_refresh(0, clock_tick, old_divisor, divisor);
	} else if (old_divisor == 1 && divisor == 2) {
		frob_mem_refresh(1, clock_tick, old_divisor, divisor);
		write_hbreg(HBIRD_ESTAR_MODE_ADDR, estar | new_bits);
		self_refresh_ctl(1);
	} else if (old_divisor == 1 && divisor > 2) {
		us2e_transition(estar, ESTAR_MODE_DIV_2, clock_tick,
				1, 2);
		us2e_transition(estar, new_bits, clock_tick,
				2, divisor);
	} else if (old_divisor > 2 && divisor == 1) {
		us2e_transition(estar, ESTAR_MODE_DIV_2, clock_tick,
				old_divisor, 2);
		us2e_transition(estar, new_bits, clock_tick,
				2, divisor);
	} else if (old_divisor < divisor) {
		frob_mem_refresh(0, clock_tick, old_divisor, divisor);
		write_hbreg(HBIRD_ESTAR_MODE_ADDR, estar | new_bits);
	} else if (old_divisor > divisor) {
		write_hbreg(HBIRD_ESTAR_MODE_ADDR, estar | new_bits);
		frob_mem_refresh(1, clock_tick, old_divisor, divisor);
	} else {
		BUG();
	}

	local_irq_restore(flags);
}

static unsigned long index_to_estar_mode(unsigned int index)
{
	switch (index) {
	case 0:
		return ESTAR_MODE_DIV_1;

	case 1:
		return ESTAR_MODE_DIV_2;

	case 2:
		return ESTAR_MODE_DIV_4;

	case 3:
		return ESTAR_MODE_DIV_6;

	case 4:
		return ESTAR_MODE_DIV_8;

	default:
		BUG();
	}
}

static unsigned long index_to_divisor(unsigned int index)
{
	switch (index) {
	case 0:
		return 1;

	case 1:
		return 2;

	case 2:
		return 4;

	case 3:
		return 6;

	case 4:
		return 8;

	default:
		BUG();
	}
}

static unsigned long estar_to_divisor(unsigned long estar)
{
	unsigned long ret;

	switch (estar & ESTAR_MODE_DIV_MASK) {
	case ESTAR_MODE_DIV_1:
		ret = 1;
		break;
	case ESTAR_MODE_DIV_2:
		ret = 2;
		break;
	case ESTAR_MODE_DIV_4:
		ret = 4;
		break;
	case ESTAR_MODE_DIV_6:
		ret = 6;
		break;
	case ESTAR_MODE_DIV_8:
		ret = 8;
		break;
	default:
		BUG();
	}

	return ret;
}

static unsigned int us2e_freq_get(unsigned int cpu)
{
	cpumask_t cpus_allowed;
	unsigned long clock_tick, estar;

	cpumask_copy(&cpus_allowed, tsk_cpus_allowed(current));
	set_cpus_allowed_ptr(current, cpumask_of(cpu));

	clock_tick = sparc64_get_clock_tick(cpu) / 1000;
	estar = read_hbreg(HBIRD_ESTAR_MODE_ADDR);

	set_cpus_allowed_ptr(current, &cpus_allowed);

	return clock_tick / estar_to_divisor(estar);
}

static void us2e_set_cpu_divider_index(struct cpufreq_policy *policy,
		unsigned int index)
{
	unsigned int cpu = policy->cpu;
	unsigned long new_bits, new_freq;
	unsigned long clock_tick, divisor, old_divisor, estar;
	cpumask_t cpus_allowed;
	struct cpufreq_freqs freqs;

	cpumask_copy(&cpus_allowed, tsk_cpus_allowed(current));
	set_cpus_allowed_ptr(current, cpumask_of(cpu));

	new_freq = clock_tick = sparc64_get_clock_tick(cpu) / 1000;
	new_bits = index_to_estar_mode(index);
	divisor = index_to_divisor(index);
	new_freq /= divisor;

	estar = read_hbreg(HBIRD_ESTAR_MODE_ADDR);

	old_divisor = estar_to_divisor(estar);

	freqs.old = clock_tick / old_divisor;
	freqs.new = new_freq;
	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);

	if (old_divisor != divisor)
		us2e_transition(estar, new_bits, clock_tick * 1000,
				old_divisor, divisor);

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);

	set_cpus_allowed_ptr(current, &cpus_allowed);
}

static int us2e_freq_target(struct cpufreq_policy *policy,
			  unsigned int target_freq,
			  unsigned int relation)
{
	unsigned int new_index = 0;

	if (cpufreq_frequency_table_target(policy,
					   &us2e_freq_table[policy->cpu].table[0],
					   target_freq, relation, &new_index))
		return -EINVAL;

	us2e_set_cpu_divider_index(policy, new_index);

	return 0;
}

static int us2e_freq_verify(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy,
					      &us2e_freq_table[policy->cpu].table[0]);
}

static int __init us2e_freq_cpu_init(struct cpufreq_policy *policy)
{
	unsigned int cpu = policy->cpu;
	unsigned long clock_tick = sparc64_get_clock_tick(cpu) / 1000;
	struct cpufreq_frequency_table *table =
		&us2e_freq_table[cpu].table[0];

	table[0].index = 0;
	table[0].frequency = clock_tick / 1;
	table[1].index = 1;
	table[1].frequency = clock_tick / 2;
	table[2].index = 2;
	table[2].frequency = clock_tick / 4;
	table[2].index = 3;
	table[2].frequency = clock_tick / 6;
	table[2].index = 4;
	table[2].frequency = clock_tick / 8;
	table[2].index = 5;
	table[3].frequency = CPUFREQ_TABLE_END;

	policy->cpuinfo.transition_latency = 0;
	policy->cur = clock_tick;

	return cpufreq_frequency_table_cpuinfo(policy, table);
}

static int us2e_freq_cpu_exit(struct cpufreq_policy *policy)
{
	if (cpufreq_us2e_driver)
		us2e_set_cpu_divider_index(policy, 0);

	return 0;
}

static int __init us2e_freq_init(void)
{
	unsigned long manuf, impl, ver;
	int ret;

	if (tlb_type != spitfire)
		return -ENODEV;

	__asm__("rdpr %%ver, %0" : "=r" (ver));
	manuf = ((ver >> 48) & 0xffff);
	impl  = ((ver >> 32) & 0xffff);

	if (manuf == 0x17 && impl == 0x13) {
		struct cpufreq_driver *driver;

		ret = -ENOMEM;
		driver = kzalloc(sizeof(struct cpufreq_driver), GFP_KERNEL);
		if (!driver)
			goto err_out;

		us2e_freq_table = kzalloc(
			(NR_CPUS * sizeof(struct us2e_freq_percpu_info)),
			GFP_KERNEL);
		if (!us2e_freq_table)
			goto err_out;

		driver->init = us2e_freq_cpu_init;
		driver->verify = us2e_freq_verify;
		driver->target = us2e_freq_target;
		driver->get = us2e_freq_get;
		driver->exit = us2e_freq_cpu_exit;
		driver->owner = THIS_MODULE,
		strcpy(driver->name, "UltraSPARC-IIe");

		cpufreq_us2e_driver = driver;
		ret = cpufreq_register_driver(driver);
		if (ret)
			goto err_out;

		return 0;

err_out:
		if (driver) {
			kfree(driver);
			cpufreq_us2e_driver = NULL;
		}
		kfree(us2e_freq_table);
		us2e_freq_table = NULL;
		return ret;
	}

	return -ENODEV;
}

static void __exit us2e_freq_exit(void)
{
	if (cpufreq_us2e_driver) {
		cpufreq_unregister_driver(cpufreq_us2e_driver);
		kfree(cpufreq_us2e_driver);
		cpufreq_us2e_driver = NULL;
		kfree(us2e_freq_table);
		us2e_freq_table = NULL;
	}
}

MODULE_AUTHOR("David S. Miller <davem@redhat.com>");
MODULE_DESCRIPTION("cpufreq driver for UltraSPARC-IIe");
MODULE_LICENSE("GPL");

module_init(us2e_freq_init);
module_exit(us2e_freq_exit);
