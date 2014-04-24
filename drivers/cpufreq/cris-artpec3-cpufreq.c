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
	{0, 0x01, 6000},
	{0, 0x02, 200000},
	{0, 0, CPUFREQ_TABLE_END},
};

static unsigned int cris_freq_get_cpu_frequency(unsigned int cpu)
{
	reg_clkgen_rw_clk_ctrl clk_ctrl;
	clk_ctrl = REG_RD(clkgen, regi_clkgen, rw_clk_ctrl);
	return clk_ctrl.pll ? 200000 : 6000;
}

static int cris_freq_target(struct cpufreq_policy *policy, unsigned int state)
{
	reg_clkgen_rw_clk_ctrl clk_ctrl;
	clk_ctrl = REG_RD(clkgen, regi_clkgen, rw_clk_ctrl);

	local_irq_disable();

	/* Even though we may be SMP they will share the same clock
	 * so all settings are made on CPU0. */
	if (cris_freq_table[state].frequency == 200000)
		clk_ctrl.pll = 1;
	else
		clk_ctrl.pll = 0;
	REG_WR(clkgen, regi_clkgen, rw_clk_ctrl, clk_ctrl);

	local_irq_enable();

	return 0;
}

static int cris_freq_cpu_init(struct cpufreq_policy *policy)
{
	return cpufreq_generic_init(policy, cris_freq_table, 1000000);
}

static struct cpufreq_driver cris_freq_driver = {
	.get	= cris_freq_get_cpu_frequency,
	.verify	= cpufreq_generic_frequency_table_verify,
	.target_index = cris_freq_target,
	.init	= cris_freq_cpu_init,
	.name	= "cris_freq",
	.attr	= cpufreq_generic_attr,
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
