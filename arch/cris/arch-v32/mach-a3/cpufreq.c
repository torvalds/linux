#include <linux/init.h>
#include <linux/module.h>
#include <linux/cpufreq.h>
#include <hwregs/reg_map.h>
#include <hwregs/reg_rdwr.h>
#include <hwregs/clkgen_defs.h>
#include <hwregs/ddr2_defs.h>

static int
cris_sdram_freq_notifier(struct notifier_block *nb, unsigned long val,
	void *data);

static struct notifier_block cris_sdram_freq_notifier_block = {
	.notifier_call = cris_sdram_freq_notifier
};

static struct cpufreq_frequency_table cris_freq_table[] = {
	{0x01,	6000},
	{0x02,	200000},
	{0,	CPUFREQ_TABLE_END},
};

static unsigned int cris_freq_get_cpu_frequency(unsigned int cpu)
{
	reg_clkgen_rw_clk_ctrl clk_ctrl;
	clk_ctrl = REG_RD(clkgen, regi_clkgen, rw_clk_ctrl);
	return clk_ctrl.pll ? 200000 : 6000;
}

static void cris_freq_set_cpu_state(unsigned int state)
{
	int i = 0;
	struct cpufreq_freqs freqs;
	reg_clkgen_rw_clk_ctrl clk_ctrl;
	clk_ctrl = REG_RD(clkgen, regi_clkgen, rw_clk_ctrl);

#ifdef CONFIG_SMP
	for_each_present_cpu(i)
#endif
	{
		freqs.old = cris_freq_get_cpu_frequency(i);
		freqs.new = cris_freq_table[state].frequency;
		freqs.cpu = i;
	}

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	local_irq_disable();

	/* Even though we may be SMP they will share the same clock
	 * so all settings are made on CPU0. */
	if (cris_freq_table[state].frequency == 200000)
		clk_ctrl.pll = 1;
	else
		clk_ctrl.pll = 0;
	REG_WR(clkgen, regi_clkgen, rw_clk_ctrl, clk_ctrl);

	local_irq_enable();

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
};

static int cris_freq_verify(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, &cris_freq_table[0]);
}

static int cris_freq_target(struct cpufreq_policy *policy,
			    unsigned int target_freq,
			    unsigned int relation)
{
	unsigned int newstate = 0;

	if (cpufreq_frequency_table_target(policy, cris_freq_table,
			target_freq, relation, &newstate))
		return -EINVAL;

	cris_freq_set_cpu_state(newstate);

	return 0;
}

static int cris_freq_cpu_init(struct cpufreq_policy *policy)
{
	int result;

	/* cpuinfo and default policy values */
	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;
	policy->cpuinfo.transition_latency = 1000000; /* 1ms */
	policy->cur = cris_freq_get_cpu_frequency(0);

	result = cpufreq_frequency_table_cpuinfo(policy, cris_freq_table);
	if (result)
		return (result);

	cpufreq_frequency_table_get_attr(cris_freq_table, policy->cpu);

	return 0;
}


static int cris_freq_cpu_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_put_attr(policy->cpu);
	return 0;
}


static struct freq_attr *cris_freq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver cris_freq_driver = {
	.get	= cris_freq_get_cpu_frequency,
	.verify	= cris_freq_verify,
	.target	= cris_freq_target,
	.init	= cris_freq_cpu_init,
	.exit	= cris_freq_cpu_exit,
	.name	= "cris_freq",
	.owner	= THIS_MODULE,
	.attr	= cris_freq_attr,
};

static int __init cris_freq_init(void)
{
	int ret;
	ret = cpufreq_register_driver(&cris_freq_driver);
	cpufreq_register_notifier(&cris_sdram_freq_notifier_block,
		CPUFREQ_TRANSITION_NOTIFIER);
	return ret;
}

static int
cris_sdram_freq_notifier(struct notifier_block *nb, unsigned long val,
	void *data)
{
	int i;
	struct cpufreq_freqs *freqs = data;
	if (val == CPUFREQ_PRECHANGE) {
		reg_ddr2_rw_cfg cfg =
		  REG_RD(ddr2, regi_ddr2_ctrl, rw_cfg);
		cfg.ref_interval = (freqs->new == 200000 ? 1560 : 46);

		if (freqs->new == 200000)
			for (i = 0; i < 50000; i++);
		REG_WR(bif_core, regi_bif_core, rw_sdram_timing, timing);
	}
	return 0;
}


module_init(cris_freq_init);
