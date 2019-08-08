// SPDX-License-Identifier: GPL-2.0-only
/*
 * S3C2416/2450 CPUfreq Support
 *
 * Copyright 2011 Heiko Stuebner <heiko@sntech.de>
 *
 * based on s3c64xx_cpufreq.c
 *
 * Copyright 2009 Wolfson Microelectronics plc
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/reboot.h>
#include <linux/module.h>

static DEFINE_MUTEX(cpufreq_lock);

struct s3c2416_data {
	struct clk *armdiv;
	struct clk *armclk;
	struct clk *hclk;

	unsigned long regulator_latency;
#ifdef CONFIG_ARM_S3C2416_CPUFREQ_VCORESCALE
	struct regulator *vddarm;
#endif

	struct cpufreq_frequency_table *freq_table;

	bool is_dvs;
	bool disable_dvs;
};

static struct s3c2416_data s3c2416_cpufreq;

struct s3c2416_dvfs {
	unsigned int vddarm_min;
	unsigned int vddarm_max;
};

/* pseudo-frequency for dvs mode */
#define FREQ_DVS	132333

/* frequency to sleep and reboot in
 * it's essential to leave dvs, as some boards do not reconfigure the
 * regulator on reboot
 */
#define FREQ_SLEEP	133333

/* Sources for the ARMCLK */
#define SOURCE_HCLK	0
#define SOURCE_ARMDIV	1

#ifdef CONFIG_ARM_S3C2416_CPUFREQ_VCORESCALE
/* S3C2416 only supports changing the voltage in the dvs-mode.
 * Voltages down to 1.0V seem to work, so we take what the regulator
 * can get us.
 */
static struct s3c2416_dvfs s3c2416_dvfs_table[] = {
	[SOURCE_HCLK] = {  950000, 1250000 },
	[SOURCE_ARMDIV] = { 1250000, 1350000 },
};
#endif

static struct cpufreq_frequency_table s3c2416_freq_table[] = {
	{ 0, SOURCE_HCLK, FREQ_DVS },
	{ 0, SOURCE_ARMDIV, 133333 },
	{ 0, SOURCE_ARMDIV, 266666 },
	{ 0, SOURCE_ARMDIV, 400000 },
	{ 0, 0, CPUFREQ_TABLE_END },
};

static struct cpufreq_frequency_table s3c2450_freq_table[] = {
	{ 0, SOURCE_HCLK, FREQ_DVS },
	{ 0, SOURCE_ARMDIV, 133500 },
	{ 0, SOURCE_ARMDIV, 267000 },
	{ 0, SOURCE_ARMDIV, 534000 },
	{ 0, 0, CPUFREQ_TABLE_END },
};

static unsigned int s3c2416_cpufreq_get_speed(unsigned int cpu)
{
	struct s3c2416_data *s3c_freq = &s3c2416_cpufreq;

	if (cpu != 0)
		return 0;

	/* return our pseudo-frequency when in dvs mode */
	if (s3c_freq->is_dvs)
		return FREQ_DVS;

	return clk_get_rate(s3c_freq->armclk) / 1000;
}

static int s3c2416_cpufreq_set_armdiv(struct s3c2416_data *s3c_freq,
				      unsigned int freq)
{
	int ret;

	if (clk_get_rate(s3c_freq->armdiv) / 1000 != freq) {
		ret = clk_set_rate(s3c_freq->armdiv, freq * 1000);
		if (ret < 0) {
			pr_err("cpufreq: Failed to set armdiv rate %dkHz: %d\n",
			       freq, ret);
			return ret;
		}
	}

	return 0;
}

static int s3c2416_cpufreq_enter_dvs(struct s3c2416_data *s3c_freq, int idx)
{
#ifdef CONFIG_ARM_S3C2416_CPUFREQ_VCORESCALE
	struct s3c2416_dvfs *dvfs;
#endif
	int ret;

	if (s3c_freq->is_dvs) {
		pr_debug("cpufreq: already in dvs mode, nothing to do\n");
		return 0;
	}

	pr_debug("cpufreq: switching armclk to hclk (%lukHz)\n",
		 clk_get_rate(s3c_freq->hclk) / 1000);
	ret = clk_set_parent(s3c_freq->armclk, s3c_freq->hclk);
	if (ret < 0) {
		pr_err("cpufreq: Failed to switch armclk to hclk: %d\n", ret);
		return ret;
	}

#ifdef CONFIG_ARM_S3C2416_CPUFREQ_VCORESCALE
	/* changing the core voltage is only allowed when in dvs mode */
	if (s3c_freq->vddarm) {
		dvfs = &s3c2416_dvfs_table[idx];

		pr_debug("cpufreq: setting regulator to %d-%d\n",
			 dvfs->vddarm_min, dvfs->vddarm_max);
		ret = regulator_set_voltage(s3c_freq->vddarm,
					    dvfs->vddarm_min,
					    dvfs->vddarm_max);

		/* when lowering the voltage failed, there is nothing to do */
		if (ret != 0)
			pr_err("cpufreq: Failed to set VDDARM: %d\n", ret);
	}
#endif

	s3c_freq->is_dvs = 1;

	return 0;
}

static int s3c2416_cpufreq_leave_dvs(struct s3c2416_data *s3c_freq, int idx)
{
#ifdef CONFIG_ARM_S3C2416_CPUFREQ_VCORESCALE
	struct s3c2416_dvfs *dvfs;
#endif
	int ret;

	if (!s3c_freq->is_dvs) {
		pr_debug("cpufreq: not in dvs mode, so can't leave\n");
		return 0;
	}

#ifdef CONFIG_ARM_S3C2416_CPUFREQ_VCORESCALE
	if (s3c_freq->vddarm) {
		dvfs = &s3c2416_dvfs_table[idx];

		pr_debug("cpufreq: setting regulator to %d-%d\n",
			 dvfs->vddarm_min, dvfs->vddarm_max);
		ret = regulator_set_voltage(s3c_freq->vddarm,
					    dvfs->vddarm_min,
					    dvfs->vddarm_max);
		if (ret != 0) {
			pr_err("cpufreq: Failed to set VDDARM: %d\n", ret);
			return ret;
		}
	}
#endif

	/* force armdiv to hclk frequency for transition from dvs*/
	if (clk_get_rate(s3c_freq->armdiv) > clk_get_rate(s3c_freq->hclk)) {
		pr_debug("cpufreq: force armdiv to hclk frequency (%lukHz)\n",
			 clk_get_rate(s3c_freq->hclk) / 1000);
		ret = s3c2416_cpufreq_set_armdiv(s3c_freq,
					clk_get_rate(s3c_freq->hclk) / 1000);
		if (ret < 0) {
			pr_err("cpufreq: Failed to set the armdiv to %lukHz: %d\n",
			       clk_get_rate(s3c_freq->hclk) / 1000, ret);
			return ret;
		}
	}

	pr_debug("cpufreq: switching armclk parent to armdiv (%lukHz)\n",
			clk_get_rate(s3c_freq->armdiv) / 1000);

	ret = clk_set_parent(s3c_freq->armclk, s3c_freq->armdiv);
	if (ret < 0) {
		pr_err("cpufreq: Failed to switch armclk clock parent to armdiv: %d\n",
		       ret);
		return ret;
	}

	s3c_freq->is_dvs = 0;

	return 0;
}

static int s3c2416_cpufreq_set_target(struct cpufreq_policy *policy,
				      unsigned int index)
{
	struct s3c2416_data *s3c_freq = &s3c2416_cpufreq;
	unsigned int new_freq;
	int idx, ret, to_dvs = 0;

	mutex_lock(&cpufreq_lock);

	idx = s3c_freq->freq_table[index].driver_data;

	if (idx == SOURCE_HCLK)
		to_dvs = 1;

	/* switching to dvs when it's not allowed */
	if (to_dvs && s3c_freq->disable_dvs) {
		pr_debug("cpufreq: entering dvs mode not allowed\n");
		ret = -EINVAL;
		goto out;
	}

	/* When leavin dvs mode, always switch the armdiv to the hclk rate
	 * The S3C2416 has stability issues when switching directly to
	 * higher frequencies.
	 */
	new_freq = (s3c_freq->is_dvs && !to_dvs)
				? clk_get_rate(s3c_freq->hclk) / 1000
				: s3c_freq->freq_table[index].frequency;

	if (to_dvs) {
		pr_debug("cpufreq: enter dvs\n");
		ret = s3c2416_cpufreq_enter_dvs(s3c_freq, idx);
	} else if (s3c_freq->is_dvs) {
		pr_debug("cpufreq: leave dvs\n");
		ret = s3c2416_cpufreq_leave_dvs(s3c_freq, idx);
	} else {
		pr_debug("cpufreq: change armdiv to %dkHz\n", new_freq);
		ret = s3c2416_cpufreq_set_armdiv(s3c_freq, new_freq);
	}

out:
	mutex_unlock(&cpufreq_lock);

	return ret;
}

#ifdef CONFIG_ARM_S3C2416_CPUFREQ_VCORESCALE
static void s3c2416_cpufreq_cfg_regulator(struct s3c2416_data *s3c_freq)
{
	int count, v, i, found;
	struct cpufreq_frequency_table *pos;
	struct s3c2416_dvfs *dvfs;

	count = regulator_count_voltages(s3c_freq->vddarm);
	if (count < 0) {
		pr_err("cpufreq: Unable to check supported voltages\n");
		return;
	}

	if (!count)
		goto out;

	cpufreq_for_each_valid_entry(pos, s3c_freq->freq_table) {
		dvfs = &s3c2416_dvfs_table[pos->driver_data];
		found = 0;

		/* Check only the min-voltage, more is always ok on S3C2416 */
		for (i = 0; i < count; i++) {
			v = regulator_list_voltage(s3c_freq->vddarm, i);
			if (v >= dvfs->vddarm_min)
				found = 1;
		}

		if (!found) {
			pr_debug("cpufreq: %dkHz unsupported by regulator\n",
				 pos->frequency);
			pos->frequency = CPUFREQ_ENTRY_INVALID;
		}
	}

out:
	/* Guessed */
	s3c_freq->regulator_latency = 1 * 1000 * 1000;
}
#endif

static int s3c2416_cpufreq_reboot_notifier_evt(struct notifier_block *this,
					       unsigned long event, void *ptr)
{
	struct s3c2416_data *s3c_freq = &s3c2416_cpufreq;
	int ret;

	mutex_lock(&cpufreq_lock);

	/* disable further changes */
	s3c_freq->disable_dvs = 1;

	mutex_unlock(&cpufreq_lock);

	/* some boards don't reconfigure the regulator on reboot, which
	 * could lead to undervolting the cpu when the clock is reset.
	 * Therefore we always leave the DVS mode on reboot.
	 */
	if (s3c_freq->is_dvs) {
		pr_debug("cpufreq: leave dvs on reboot\n");
		ret = cpufreq_driver_target(cpufreq_cpu_get(0), FREQ_SLEEP, 0);
		if (ret < 0)
			return NOTIFY_BAD;
	}

	return NOTIFY_DONE;
}

static struct notifier_block s3c2416_cpufreq_reboot_notifier = {
	.notifier_call = s3c2416_cpufreq_reboot_notifier_evt,
};

static int s3c2416_cpufreq_driver_init(struct cpufreq_policy *policy)
{
	struct s3c2416_data *s3c_freq = &s3c2416_cpufreq;
	struct cpufreq_frequency_table *pos;
	struct clk *msysclk;
	unsigned long rate;
	int ret;

	if (policy->cpu != 0)
		return -EINVAL;

	msysclk = clk_get(NULL, "msysclk");
	if (IS_ERR(msysclk)) {
		ret = PTR_ERR(msysclk);
		pr_err("cpufreq: Unable to obtain msysclk: %d\n", ret);
		return ret;
	}

	/*
	 * S3C2416 and S3C2450 share the same processor-ID and also provide no
	 * other means to distinguish them other than through the rate of
	 * msysclk. On S3C2416 msysclk runs at 800MHz and on S3C2450 at 533MHz.
	 */
	rate = clk_get_rate(msysclk);
	if (rate == 800 * 1000 * 1000) {
		pr_info("cpufreq: msysclk running at %lukHz, using S3C2416 frequency table\n",
			rate / 1000);
		s3c_freq->freq_table = s3c2416_freq_table;
		policy->cpuinfo.max_freq = 400000;
	} else if (rate / 1000 == 534000) {
		pr_info("cpufreq: msysclk running at %lukHz, using S3C2450 frequency table\n",
			rate / 1000);
		s3c_freq->freq_table = s3c2450_freq_table;
		policy->cpuinfo.max_freq = 534000;
	}

	/* not needed anymore */
	clk_put(msysclk);

	if (s3c_freq->freq_table == NULL) {
		pr_err("cpufreq: No frequency information for this CPU, msysclk at %lukHz\n",
		       rate / 1000);
		return -ENODEV;
	}

	s3c_freq->is_dvs = 0;

	s3c_freq->armdiv = clk_get(NULL, "armdiv");
	if (IS_ERR(s3c_freq->armdiv)) {
		ret = PTR_ERR(s3c_freq->armdiv);
		pr_err("cpufreq: Unable to obtain ARMDIV: %d\n", ret);
		return ret;
	}

	s3c_freq->hclk = clk_get(NULL, "hclk");
	if (IS_ERR(s3c_freq->hclk)) {
		ret = PTR_ERR(s3c_freq->hclk);
		pr_err("cpufreq: Unable to obtain HCLK: %d\n", ret);
		goto err_hclk;
	}

	/* chech hclk rate, we only support the common 133MHz for now
	 * hclk could also run at 66MHz, but this not often used
	 */
	rate = clk_get_rate(s3c_freq->hclk);
	if (rate < 133 * 1000 * 1000) {
		pr_err("cpufreq: HCLK not at 133MHz\n");
		ret = -EINVAL;
		goto err_armclk;
	}

	s3c_freq->armclk = clk_get(NULL, "armclk");
	if (IS_ERR(s3c_freq->armclk)) {
		ret = PTR_ERR(s3c_freq->armclk);
		pr_err("cpufreq: Unable to obtain ARMCLK: %d\n", ret);
		goto err_armclk;
	}

#ifdef CONFIG_ARM_S3C2416_CPUFREQ_VCORESCALE
	s3c_freq->vddarm = regulator_get(NULL, "vddarm");
	if (IS_ERR(s3c_freq->vddarm)) {
		ret = PTR_ERR(s3c_freq->vddarm);
		pr_err("cpufreq: Failed to obtain VDDARM: %d\n", ret);
		goto err_vddarm;
	}

	s3c2416_cpufreq_cfg_regulator(s3c_freq);
#else
	s3c_freq->regulator_latency = 0;
#endif

	cpufreq_for_each_entry(pos, s3c_freq->freq_table) {
		/* special handling for dvs mode */
		if (pos->driver_data == 0) {
			if (!s3c_freq->hclk) {
				pr_debug("cpufreq: %dkHz unsupported as it would need unavailable dvs mode\n",
					 pos->frequency);
				pos->frequency = CPUFREQ_ENTRY_INVALID;
			} else {
				continue;
			}
		}

		/* Check for frequencies we can generate */
		rate = clk_round_rate(s3c_freq->armdiv,
				      pos->frequency * 1000);
		rate /= 1000;
		if (rate != pos->frequency) {
			pr_debug("cpufreq: %dkHz unsupported by clock (clk_round_rate return %lu)\n",
				pos->frequency, rate);
			pos->frequency = CPUFREQ_ENTRY_INVALID;
		}
	}

	/* Datasheet says PLL stabalisation time must be at least 300us,
	 * so but add some fudge. (reference in LOCKCON0 register description)
	 */
	ret = cpufreq_generic_init(policy, s3c_freq->freq_table,
			(500 * 1000) + s3c_freq->regulator_latency);
	if (ret)
		goto err_freq_table;

	register_reboot_notifier(&s3c2416_cpufreq_reboot_notifier);

	return 0;

err_freq_table:
#ifdef CONFIG_ARM_S3C2416_CPUFREQ_VCORESCALE
	regulator_put(s3c_freq->vddarm);
err_vddarm:
#endif
	clk_put(s3c_freq->armclk);
err_armclk:
	clk_put(s3c_freq->hclk);
err_hclk:
	clk_put(s3c_freq->armdiv);

	return ret;
}

static struct cpufreq_driver s3c2416_cpufreq_driver = {
	.flags		= CPUFREQ_NEED_INITIAL_FREQ_CHECK,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= s3c2416_cpufreq_set_target,
	.get		= s3c2416_cpufreq_get_speed,
	.init		= s3c2416_cpufreq_driver_init,
	.name		= "s3c2416",
	.attr		= cpufreq_generic_attr,
};

static int __init s3c2416_cpufreq_init(void)
{
	return cpufreq_register_driver(&s3c2416_cpufreq_driver);
}
module_init(s3c2416_cpufreq_init);
