/*
 * cpufreq driver for the cell processor
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005
 *
 * Author: Christian Krafft <krafft@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/cpufreq.h>
#include <linux/timer.h>

#include <asm/hw_irq.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/processor.h>
#include <asm/prom.h>
#include <asm/time.h>
#include <asm/pmi.h>
#include <asm/of_platform.h>

#include "cbe_regs.h"

static DEFINE_MUTEX(cbe_switch_mutex);


/* the CBE supports an 8 step frequency scaling */
static struct cpufreq_frequency_table cbe_freqs[] = {
	{1,	0},
	{2,	0},
	{3,	0},
	{4,	0},
	{5,	0},
	{6,	0},
	{8,	0},
	{10,	0},
	{0,	CPUFREQ_TABLE_END},
};

/* to write to MIC register */
static u64 MIC_Slow_Fast_Timer_table[] = {
	[0 ... 7] = 0x007fc00000000000ull,
};

/* more values for the MIC */
static u64 MIC_Slow_Next_Timer_table[] = {
	0x0000240000000000ull,
	0x0000268000000000ull,
	0x000029C000000000ull,
	0x00002D0000000000ull,
	0x0000300000000000ull,
	0x0000334000000000ull,
	0x000039C000000000ull,
	0x00003FC000000000ull,
};

static unsigned int pmi_frequency_limit = 0;
/*
 * hardware specific functions
 */

static struct of_device *pmi_dev;

static int set_pmode_pmi(int cpu, unsigned int pmode)
{
	int ret;
	pmi_message_t pmi_msg;
#ifdef DEBUG
	u64 time;
#endif

	pmi_msg.type = PMI_TYPE_FREQ_CHANGE;
	pmi_msg.data1 =	cbe_cpu_to_node(cpu);
	pmi_msg.data2 = pmode;

#ifdef DEBUG
	time = (u64) get_cycles();
#endif

	pmi_send_message(pmi_dev, pmi_msg);
	ret = pmi_msg.data2;

	pr_debug("PMI returned slow mode %d\n", ret);

#ifdef DEBUG
	time = (u64) get_cycles() - time; /* actual cycles (not cpu cycles!) */
	time = 1000000000 * time / CLOCK_TICK_RATE; /* time in ns (10^-9) */
	pr_debug("had to wait %lu ns for a transition\n", time);
#endif
	return ret;
}


static int get_pmode(int cpu)
{
	int ret;
	struct cbe_pmd_regs __iomem *pmd_regs;

	pmd_regs = cbe_get_cpu_pmd_regs(cpu);
	ret = in_be64(&pmd_regs->pmsr) & 0x07;

	return ret;
}

static int set_pmode_reg(int cpu, unsigned int pmode)
{
	struct cbe_pmd_regs __iomem *pmd_regs;
	struct cbe_mic_tm_regs __iomem *mic_tm_regs;
	u64 flags;
	u64 value;

	local_irq_save(flags);

	mic_tm_regs = cbe_get_cpu_mic_tm_regs(cpu);
	pmd_regs = cbe_get_cpu_pmd_regs(cpu);

	pr_debug("pm register is mapped at %p\n", &pmd_regs->pmcr);
	pr_debug("mic register is mapped at %p\n", &mic_tm_regs->slow_fast_timer_0);

	out_be64(&mic_tm_regs->slow_fast_timer_0, MIC_Slow_Fast_Timer_table[pmode]);
	out_be64(&mic_tm_regs->slow_fast_timer_1, MIC_Slow_Fast_Timer_table[pmode]);

	out_be64(&mic_tm_regs->slow_next_timer_0, MIC_Slow_Next_Timer_table[pmode]);
	out_be64(&mic_tm_regs->slow_next_timer_1, MIC_Slow_Next_Timer_table[pmode]);

	value = in_be64(&pmd_regs->pmcr);
	/* set bits to zero */
	value &= 0xFFFFFFFFFFFFFFF8ull;
	/* set bits to next pmode */
	value |= pmode;

	out_be64(&pmd_regs->pmcr, value);

	/* wait until new pmode appears in status register */
	value = in_be64(&pmd_regs->pmsr) & 0x07;
	while(value != pmode) {
		cpu_relax();
		value = in_be64(&pmd_regs->pmsr) & 0x07;
	}

	local_irq_restore(flags);

	return 0;
}

static int set_pmode(int cpu, unsigned int slow_mode) {
	if (pmi_dev)
		return set_pmode_pmi(cpu, slow_mode);
	else
		return set_pmode_reg(cpu, slow_mode);
}

static void cbe_cpufreq_handle_pmi(struct of_device *dev, pmi_message_t pmi_msg)
{
	u8 cpu;
	u8 cbe_pmode_new;

	BUG_ON(pmi_msg.type != PMI_TYPE_FREQ_CHANGE);

	cpu = cbe_node_to_cpu(pmi_msg.data1);
	cbe_pmode_new = pmi_msg.data2;

	pmi_frequency_limit = cbe_freqs[cbe_pmode_new].frequency;

	pr_debug("cbe_handle_pmi: max freq=%d\n", pmi_frequency_limit);
}

static int pmi_notifier(struct notifier_block *nb,
				       unsigned long event, void *data)
{
	struct cpufreq_policy *policy = data;

	if (event != CPUFREQ_INCOMPATIBLE)
		return 0;

	cpufreq_verify_within_limits(policy, 0, pmi_frequency_limit);
	return 0;
}

static struct notifier_block pmi_notifier_block = {
	.notifier_call = pmi_notifier,
};

static struct pmi_handler cbe_pmi_handler = {
	.type			= PMI_TYPE_FREQ_CHANGE,
	.handle_pmi_message	= cbe_cpufreq_handle_pmi,
};


/*
 * cpufreq functions
 */

static int cbe_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	const u32 *max_freqp;
	u32 max_freq;
	int i, cur_pmode;
	struct device_node *cpu;

	cpu = of_get_cpu_node(policy->cpu, NULL);

	if (!cpu)
		return -ENODEV;

	pr_debug("init cpufreq on CPU %d\n", policy->cpu);

	max_freqp = of_get_property(cpu, "clock-frequency", NULL);

	if (!max_freqp)
		return -EINVAL;

	/* we need the freq in kHz */
	max_freq = *max_freqp / 1000;

	pr_debug("max clock-frequency is at %u kHz\n", max_freq);
	pr_debug("initializing frequency table\n");

	/* initialize frequency table */
	for (i=0; cbe_freqs[i].frequency!=CPUFREQ_TABLE_END; i++) {
		cbe_freqs[i].frequency = max_freq / cbe_freqs[i].index;
		pr_debug("%d: %d\n", i, cbe_freqs[i].frequency);
	}

	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;
	/* if DEBUG is enabled set_pmode() measures the correct latency of a transition */
	policy->cpuinfo.transition_latency = 25000;

	cur_pmode = get_pmode(policy->cpu);
	pr_debug("current pmode is at %d\n",cur_pmode);

	policy->cur = cbe_freqs[cur_pmode].frequency;

#ifdef CONFIG_SMP
	policy->cpus = cpu_sibling_map[policy->cpu];
#endif

	cpufreq_frequency_table_get_attr(cbe_freqs, policy->cpu);

	if (pmi_dev) {
		/* frequency might get limited later, initialize limit with max_freq */
		pmi_frequency_limit = max_freq;
		cpufreq_register_notifier(&pmi_notifier_block, CPUFREQ_POLICY_NOTIFIER);
	}

	/* this ensures that policy->cpuinfo_min and policy->cpuinfo_max are set correctly */
	return cpufreq_frequency_table_cpuinfo(policy, cbe_freqs);
}

static int cbe_cpufreq_cpu_exit(struct cpufreq_policy *policy)
{
	if (pmi_dev)
		cpufreq_unregister_notifier(&pmi_notifier_block, CPUFREQ_POLICY_NOTIFIER);

	cpufreq_frequency_table_put_attr(policy->cpu);
	return 0;
}

static int cbe_cpufreq_verify(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, cbe_freqs);
}


static int cbe_cpufreq_target(struct cpufreq_policy *policy, unsigned int target_freq,
			    unsigned int relation)
{
	int rc;
	struct cpufreq_freqs freqs;
	int cbe_pmode_new;

	cpufreq_frequency_table_target(policy,
				       cbe_freqs,
				       target_freq,
				       relation,
				       &cbe_pmode_new);

	freqs.old = policy->cur;
	freqs.new = cbe_freqs[cbe_pmode_new].frequency;
	freqs.cpu = policy->cpu;

	mutex_lock(&cbe_switch_mutex);
	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	pr_debug("setting frequency for cpu %d to %d kHz, 1/%d of max frequency\n",
		 policy->cpu,
		 cbe_freqs[cbe_pmode_new].frequency,
		 cbe_freqs[cbe_pmode_new].index);

	rc = set_pmode(policy->cpu, cbe_pmode_new);
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
	mutex_unlock(&cbe_switch_mutex);

	return rc;
}

static struct cpufreq_driver cbe_cpufreq_driver = {
	.verify		= cbe_cpufreq_verify,
	.target		= cbe_cpufreq_target,
	.init		= cbe_cpufreq_cpu_init,
	.exit		= cbe_cpufreq_cpu_exit,
	.name		= "cbe-cpufreq",
	.owner		= THIS_MODULE,
	.flags		= CPUFREQ_CONST_LOOPS,
};

/*
 * module init and destoy
 */

static int __init cbe_cpufreq_init(void)
{
	struct device_node *np;

	if (!machine_is(cell))
		return -ENODEV;

	np = of_find_node_by_type(NULL, "ibm,pmi");

	pmi_dev = of_find_device_by_node(np);

	if (pmi_dev)
		pmi_register_handler(pmi_dev, &cbe_pmi_handler);

	return cpufreq_register_driver(&cbe_cpufreq_driver);
}

static void __exit cbe_cpufreq_exit(void)
{
	if (pmi_dev)
		pmi_unregister_handler(pmi_dev, &cbe_pmi_handler);

	cpufreq_unregister_driver(&cbe_cpufreq_driver);
}

module_init(cbe_cpufreq_init);
module_exit(cbe_cpufreq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Krafft <krafft@de.ibm.com>");
