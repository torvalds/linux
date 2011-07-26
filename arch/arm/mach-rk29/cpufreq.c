/* arch/arm/mach-rk2818/cpufreq.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifdef CONFIG_CPU_FREQ_DEBUG
#define DEBUG
#endif
#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/regulator/consumer.h>
#include <linux/suspend.h>
#include <linux/tick.h>
#include <linux/workqueue.h>
#include <mach/cpufreq.h>

static int no_cpufreq_access;

static struct cpufreq_frequency_table default_freq_table[] = {
//	{ .index = 1100000, .frequency =   24000 },
//	{ .index = 1200000, .frequency =  204000 },
//	{ .index = 1200000, .frequency =  300000 },
	{ .index = 1200000, .frequency =  408000 },
//	{ .index = 1200000, .frequency =  600000 },
	{ .index = 1200000, .frequency =  816000 }, /* must enable, see SLEEP_FREQ above */
//	{ .index = 1250000, .frequency = 1008000 },
//	{ .index = 1300000, .frequency = 1104000 },
//	{ .index = 1400000, .frequency = 1176000 },
//	{ .index = 1400000, .frequency = 1200000 },
	{ .frequency = CPUFREQ_TABLE_END },
};
static struct cpufreq_frequency_table *freq_table = default_freq_table;
static struct clk *arm_clk;
static DEFINE_MUTEX(mutex);

#ifdef CONFIG_REGULATOR
static struct regulator *vcore;
static int vcore_uV;
#define CONFIG_RK29_CPU_FREQ_LIMIT
#endif

#ifdef CONFIG_RK29_CPU_FREQ_LIMIT
static struct workqueue_struct *wq;
static void rk29_cpufreq_work_func(struct work_struct *work);
static DECLARE_DELAYED_WORK(rk29_cpufreq_work, rk29_cpufreq_work_func);
#define WORK_DELAY HZ

static int limit = 1;
module_param(limit, int, 0644);

#define LIMIT_SECS	30
static int limit_secs = LIMIT_SECS;
module_param(limit_secs, int, 0644);

static int limit_temp;
module_param(limit_temp, int, 0444);

#define LIMIT_AVG_VOLTAGE	1225000 /* vU */
#else /* !CONFIG_RK29_CPU_FREQ_LIMIT */
#define LIMIT_AVG_VOLTAGE	1400000 /* vU */
#endif /* CONFIG_RK29_CPU_FREQ_LIMIT */

enum {
	DEBUG_CHANGE	= 1U << 0,
	DEBUG_LIMIT	= 1U << 1,
};
static uint debug_mask = DEBUG_CHANGE;
module_param(debug_mask, uint, 0644);
#define dprintk(mask, fmt, ...) do { if (mask & debug_mask) printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__); } while (0)

#define LIMIT_AVG_FREQ	(816 * 1000) /* kHz */
static unsigned int limit_avg_freq = LIMIT_AVG_FREQ;
static int rk29_cpufreq_set_limit_avg_freq(const char *val, struct kernel_param *kp)
{
	int err = param_set_uint(val, kp);
	if (!err) {
		board_update_cpufreq_table(freq_table);
	}
	return err;
}
module_param_call(limit_avg_freq, rk29_cpufreq_set_limit_avg_freq, param_get_uint, &limit_avg_freq, 0644);

static int limit_avg_index = -1;

static unsigned int limit_avg_voltage = LIMIT_AVG_VOLTAGE;
static int rk29_cpufreq_set_limit_avg_voltage(const char *val, struct kernel_param *kp)
{
	int err = param_set_uint(val, kp);
	if (!err) {
		board_update_cpufreq_table(freq_table);
	}
	return err;
}
module_param_call(limit_avg_voltage, rk29_cpufreq_set_limit_avg_voltage, param_get_uint, &limit_avg_voltage, 0644);

static bool rk29_cpufreq_is_ondemand_policy(struct cpufreq_policy *policy)
{
	return (policy && policy->governor && (policy->governor->name[0] == 'o'));
}

int board_update_cpufreq_table(struct cpufreq_frequency_table *table)
{
	mutex_lock(&mutex);
	if (arm_clk) {
		unsigned int i;

		limit_avg_freq = 0;
		limit_avg_index = -1;
		for (i = 0; table[i].frequency != CPUFREQ_TABLE_END; i++) {
			table[i].frequency = clk_round_rate(arm_clk, table[i].frequency * 1000) / 1000;
			if (table[i].index <= limit_avg_voltage && limit_avg_freq < table[i].frequency) {
				limit_avg_freq = table[i].frequency;
				limit_avg_index = i;
			}
		}
		if (!limit_avg_freq)
			limit_avg_freq = LIMIT_AVG_FREQ;
	}
	freq_table = table;
	mutex_unlock(&mutex);
	return 0;
}

static int rk29_cpufreq_verify(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, freq_table);
}

#ifdef CONFIG_RK29_CPU_FREQ_LIMIT
static void rk29_cpufreq_limit(struct cpufreq_policy *policy, unsigned int relation, int *index)
{
	int c, ms;
	ktime_t now;
	static ktime_t last = { .tv64 = 0 };
	cputime64_t wall;
	u64 idle_time_us;
	static u64 last_idle_time_us;
	unsigned int cur = policy->cur;

	if (!limit || !wq || !rk29_cpufreq_is_ondemand_policy(policy) ||
	    (limit_avg_index < 0) || (relation & MASK_FURTHER_CPUFREQ)) {
		limit_temp = 0;
		last.tv64 = 0;
		return;
	}

	idle_time_us = get_cpu_idle_time_us(0, &wall);
	now = ktime_get();
	if (!last.tv64) {
		last = now;
		last_idle_time_us = idle_time_us;
		return;
	}

	limit_temp -= idle_time_us - last_idle_time_us; // -1000
	dprintk(DEBUG_LIMIT, "idle %lld us (%lld - %lld)\n", idle_time_us - last_idle_time_us, idle_time_us, last_idle_time_us);
	last_idle_time_us = idle_time_us;

	ms = div_u64(ktime_us_delta(now, last), 1000);
	dprintk(DEBUG_LIMIT, "%d kHz (%d uV) elapsed %d ms (%lld - %lld)\n", cur, vcore_uV, ms, now.tv64, last.tv64);
	last = now;

	if (cur <= 408 * 1000)
		c = -325;
	else if (cur <= 624 * 1000)
		c = -202;
	else if (cur <= limit_avg_freq)
		c = -78;
	else
		c = 325;
	limit_temp += c * ms;

	if (limit_temp < 0)
		limit_temp = 0;
	if (limit_temp > 325 * limit_secs * 1000)
		*index = limit_avg_index;
	dprintk(DEBUG_LIMIT, "c %d temp %d (%s) index %d", c, limit_temp, limit_temp > 325 * limit_secs * 1000 ? "overheat" : "normal", *index);
}
#else
#define rk29_cpufreq_limit(...) do {} while (0)
#endif

static int rk29_cpufreq_do_target(struct cpufreq_policy *policy, unsigned int target_freq, unsigned int relation)
{
	int index;
	int new_vcore_uV;
	struct cpufreq_freqs freqs;
	const struct cpufreq_frequency_table *freq;
	int err = 0;

	if ((relation & ENABLE_FURTHER_CPUFREQ) &&
	    (relation & DISABLE_FURTHER_CPUFREQ)) {
		/* Invalidate both if both marked */
		relation &= ~ENABLE_FURTHER_CPUFREQ;
		relation &= ~DISABLE_FURTHER_CPUFREQ;
		pr_err("denied marking FURTHER_CPUFREQ as both marked.\n");
	}
	if (relation & ENABLE_FURTHER_CPUFREQ)
		no_cpufreq_access--;
	if (no_cpufreq_access) {
#ifdef CONFIG_PM_VERBOSE
		pr_err("denied access to %s as it is disabled temporarily\n", __func__);
#endif
		return -EINVAL;
	}
	if (relation & DISABLE_FURTHER_CPUFREQ)
		no_cpufreq_access++;

	if (cpufreq_frequency_table_target(policy, freq_table, target_freq, relation & ~MASK_FURTHER_CPUFREQ, &index)) {
		pr_err("invalid target_freq: %d\n", target_freq);
		return -EINVAL;
	}
	rk29_cpufreq_limit(policy, relation, &index);
	freq = &freq_table[index];

	if (policy->cur == freq->frequency)
		return 0;

	freqs.old = policy->cur;
	freqs.new = freq->frequency;
	freqs.cpu = 0;
	new_vcore_uV = freq->index;
	dprintk(DEBUG_CHANGE, "%d Hz r %d(%c) selected %d Hz (%d uV)\n",
		target_freq, relation, relation & CPUFREQ_RELATION_H ? 'H' : 'L',
		freq->frequency, new_vcore_uV);

#ifdef CONFIG_REGULATOR
	if (vcore && freqs.new > freqs.old && vcore_uV != new_vcore_uV) {
		int err = regulator_set_voltage(vcore, new_vcore_uV, new_vcore_uV);
		if (err) {
			pr_err("fail to set vcore (%d uV) for %d kHz: %d\n",
				new_vcore_uV, freqs.new, err);
			return err;
		} else {
			vcore_uV = new_vcore_uV;
		}
	}
#endif

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	dprintk(DEBUG_CHANGE, "pre change\n");
	clk_set_rate(arm_clk, freqs.new * 1000);
	dprintk(DEBUG_CHANGE, "post change\n");
	freqs.new = clk_get_rate(arm_clk) / 1000;
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

#ifdef CONFIG_REGULATOR
	if (vcore && freqs.new < freqs.old && vcore_uV != new_vcore_uV) {
		int err = regulator_set_voltage(vcore, new_vcore_uV, new_vcore_uV);
		if (err) {
			pr_err("fail to set vcore (%d uV) for %d kHz: %d\n",
				new_vcore_uV, freqs.new, err);
		} else {
			vcore_uV = new_vcore_uV;
		}
	}
#endif
	dprintk(DEBUG_CHANGE, "ok, got %d kHz\n", freqs.new);

	return err;
}

static int rk29_cpufreq_target(struct cpufreq_policy *policy, unsigned int target_freq, unsigned int relation)
{
	int err;

	if (!policy || policy->cpu != 0)
		return -EINVAL;

	mutex_lock(&mutex);
	err = rk29_cpufreq_do_target(policy, target_freq, relation);
	mutex_unlock(&mutex);

	return err;
}

#ifdef CONFIG_RK29_CPU_FREQ_LIMIT
static int rk29_cpufreq_notifier_policy(struct notifier_block *nb,
		unsigned long val, void *data)
{
	struct cpufreq_policy *policy = data;
	bool is_ondemand;

	if (val != CPUFREQ_NOTIFY)
		return 0;

	is_ondemand = rk29_cpufreq_is_ondemand_policy(policy);
	if (!wq && is_ondemand) {
		dprintk(DEBUG_LIMIT, "start wq\n");
		wq = create_singlethread_workqueue("rk29_cpufreqd");
		if (wq)
			queue_delayed_work(wq, &rk29_cpufreq_work, WORK_DELAY);
	} else if (wq && !is_ondemand) {
		dprintk(DEBUG_LIMIT, "stop wq\n");
		cancel_delayed_work(&rk29_cpufreq_work);
		destroy_workqueue(wq);
		wq = NULL;
	}

	return 0;
}

static struct notifier_block notifier_policy_block = {
	.notifier_call = rk29_cpufreq_notifier_policy
};

static void rk29_cpufreq_work_func(struct work_struct *work)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(0);

	if (policy) {
		cpufreq_driver_target(policy, policy->cur, CPUFREQ_RELATION_L);
		cpufreq_cpu_put(policy);
	}
	queue_delayed_work(wq, &rk29_cpufreq_work, WORK_DELAY);
}
#endif

static int __init rk29_cpufreq_init(struct cpufreq_policy *policy)
{
	arm_clk = clk_get(NULL, "arm_pll");
	if (IS_ERR(arm_clk))
		return PTR_ERR(arm_clk);

	if (policy->cpu != 0)
		return -EINVAL;

#ifdef CONFIG_REGULATOR
	vcore = regulator_get(NULL, "vcore");
	if (IS_ERR(vcore)) {
		pr_err("fail to get regulator vcore: %ld\n", PTR_ERR(vcore));
		vcore = NULL;
	}
#endif

	board_update_cpufreq_table(freq_table);	/* force update frequency */
	BUG_ON(cpufreq_frequency_table_cpuinfo(policy, freq_table));
	cpufreq_frequency_table_get_attr(freq_table, policy->cpu);
	policy->cur = clk_get_rate(arm_clk) / 1000;

	policy->cpuinfo.transition_latency = 40 * NSEC_PER_USEC; // make default sampling_rate to 40000

#ifdef CONFIG_RK29_CPU_FREQ_LIMIT
	if (rk29_cpufreq_is_ondemand_policy(policy)) {
		dprintk(DEBUG_LIMIT, "start wq\n");
		wq = create_singlethread_workqueue("rk29_cpufreqd");
		if (wq)
			queue_delayed_work(wq, &rk29_cpufreq_work, WORK_DELAY);
	}
	cpufreq_register_notifier(&notifier_policy_block, CPUFREQ_POLICY_NOTIFIER);
#endif
	return 0;
}

static int rk29_cpufreq_exit(struct cpufreq_policy *policy)
{
#ifdef CONFIG_RK29_CPU_FREQ_LIMIT
	cpufreq_unregister_notifier(&notifier_policy_block, CPUFREQ_POLICY_NOTIFIER);
	if (wq) {
		dprintk(DEBUG_LIMIT, "stop wq\n");
		cancel_delayed_work(&rk29_cpufreq_work);
		destroy_workqueue(wq);
		wq = NULL;
	}
#endif
#ifdef CONFIG_REGULATOR
	if (vcore)
		regulator_put(vcore);
#endif
	clk_put(arm_clk);
	return 0;
}

static struct freq_attr *rk29_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver rk29_cpufreq_driver = {
	.flags		= CPUFREQ_STICKY | CPUFREQ_CONST_LOOPS,
	.init		= rk29_cpufreq_init,
	.exit		= rk29_cpufreq_exit,
	.verify		= rk29_cpufreq_verify,
	.target		= rk29_cpufreq_target,
	.name		= "rk29",
	.attr		= rk29_cpufreq_attr,
};

static int rk29_cpufreq_pm_notifier_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	int ret = NOTIFY_DONE;
	struct cpufreq_policy *policy = cpufreq_cpu_get(0);

	if (!policy)
		return ret;

	if (!rk29_cpufreq_is_ondemand_policy(policy))
		goto out;

	switch (event) {
	case PM_SUSPEND_PREPARE:
		ret = cpufreq_driver_target(policy, limit_avg_freq, DISABLE_FURTHER_CPUFREQ | CPUFREQ_RELATION_L);
		if (ret < 0) {
			ret = NOTIFY_BAD;
			goto out;
		}
		ret = NOTIFY_OK;
		break;
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		cpufreq_driver_target(policy, limit_avg_freq, ENABLE_FURTHER_CPUFREQ | CPUFREQ_RELATION_L);
		ret = NOTIFY_OK;
		break;
	}
out:
	cpufreq_cpu_put(policy);
	return ret;
}

static struct notifier_block rk29_cpufreq_pm_notifier = {
	.notifier_call = rk29_cpufreq_pm_notifier_event,
};

static int __init rk29_cpufreq_register(void)
{
	register_pm_notifier(&rk29_cpufreq_pm_notifier);

	return cpufreq_register_driver(&rk29_cpufreq_driver);
}

device_initcall(rk29_cpufreq_register);

