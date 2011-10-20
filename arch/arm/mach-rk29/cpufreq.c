/* arch/arm/mach-rk29/cpufreq.c
 *
 * Copyright (C) 2010, 2011 ROCKCHIP, Inc.
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

#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/reboot.h>
#include <linux/regulator/consumer.h>
#include <linux/suspend.h>
#include <linux/tick.h>
#include <linux/workqueue.h>
#include <mach/clock.h>
#include <mach/cpufreq.h>
#include <../../../drivers/video/rk29_fb.h>

#define MHZ	(1000*1000)

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
static struct clk *ddr_clk;
static unsigned long ddr_max_rate;
static DEFINE_MUTEX(mutex);

#ifdef CONFIG_REGULATOR
static struct regulator *vcore;
static int vcore_uV;
#define CONFIG_RK29_CPU_FREQ_LIMIT_BY_TEMP
#endif

static struct workqueue_struct *wq;

#ifdef CONFIG_RK29_CPU_FREQ_LIMIT_BY_TEMP
static bool limit = true;
module_param(limit, bool, 0644);

static int limit_secs = 30;
module_param(limit_secs, int, 0644);

static int limit_secs_1200 = 6;
module_param(limit_secs_1200, int, 0644);

static int limit_temp;
module_param(limit_temp, int, 0444);

#define LIMIT_AVG_VOLTAGE	1200000 /* vU */
#else /* !CONFIG_RK29_CPU_FREQ_LIMIT_BY_TEMP */
#define LIMIT_AVG_VOLTAGE	1400000 /* vU */
#endif /* CONFIG_RK29_CPU_FREQ_LIMIT_BY_TEMP */

enum {
	DEBUG_CHANGE	= 1U << 0,
	DEBUG_TEMP	= 1U << 1,
	DEBUG_DISP	= 1U << 2,
};
static uint debug_mask = DEBUG_CHANGE;
module_param(debug_mask, uint, 0644);
#define dprintk(mask, fmt, ...) do { if (mask & debug_mask) printk(KERN_DEBUG "cpufreq: " fmt, ##__VA_ARGS__); } while (0)

#define LIMIT_AVG_FREQ	(816 * 1000) /* kHz */
static unsigned int limit_avg_freq = LIMIT_AVG_FREQ;
module_param(limit_avg_freq, uint, 0444);

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

#define CONFIG_RK29_CPU_FREQ_LIMIT_BY_DISP
#ifdef CONFIG_RK29_CPU_FREQ_LIMIT_BY_DISP
static bool limit_fb1_enabled;
static bool limit_hdmi_enabled;
static inline bool aclk_limit(void) { return limit_hdmi_enabled && limit_fb1_enabled; }
module_param(limit_fb1_enabled, bool, 0644);
module_param(limit_hdmi_enabled, bool, 0644);
#else
static inline bool aclk_limit(void) { return false; }
#endif

#if defined(CONFIG_RK29_CPU_FREQ_LIMIT_BY_DISP) || defined(CONFIG_RK29_CPU_FREQ_LIMIT_BY_TEMP)
static unsigned int limit_max_freq;
static int limit_index_816 = -1;
static unsigned int limit_freq_816;
static int limit_index_1008 = -1;
static unsigned int limit_freq_1008;
#endif

static bool rk29_cpufreq_is_ondemand_policy(struct cpufreq_policy *policy)
{
	char c = 0;
	if (policy && policy->governor)
		c = policy->governor->name[0];
	return (c == 'o' || c == 'i' || c == 'c');
}

static void board_do_update_cpufreq_table(struct cpufreq_frequency_table *table)
{
	unsigned int i;

	limit_avg_freq = 0;
	limit_avg_index = -1;
#if defined(CONFIG_RK29_CPU_FREQ_LIMIT_BY_DISP) || defined(CONFIG_RK29_CPU_FREQ_LIMIT_BY_TEMP)
	limit_max_freq = 0;
	limit_index_816 = -1;
	limit_freq_816 = 0;
	limit_index_1008 = -1;
	limit_freq_1008 = 0;
#endif

	for (i = 0; table[i].frequency != CPUFREQ_TABLE_END; i++) {
		table[i].frequency = clk_round_rate(arm_clk, table[i].frequency * 1000) / 1000;
		if (table[i].index <= limit_avg_voltage && limit_avg_freq < table[i].frequency) {
			limit_avg_freq = table[i].frequency;
			limit_avg_index = i;
		}
#if defined(CONFIG_RK29_CPU_FREQ_LIMIT_BY_DISP) || defined(CONFIG_RK29_CPU_FREQ_LIMIT_BY_TEMP)
		if (limit_max_freq < table[i].frequency)
			limit_max_freq = table[i].frequency;
		if (table[i].frequency <= 816000 &&
		    (limit_index_816 < 0 ||
		    (limit_index_816 >= 0 && table[limit_index_816].frequency < table[i].frequency)))
			limit_index_816 = i;
		if (table[i].frequency <= 1008000 &&
		    (limit_index_1008 < 0 ||
		    (limit_index_1008 >= 0 && table[limit_index_1008].frequency < table[i].frequency))) {
			limit_index_1008 = i;
			limit_freq_1008 = table[i].frequency;
		}
#endif
	}

	if (!limit_avg_freq)
		limit_avg_freq = LIMIT_AVG_FREQ;
}

int board_update_cpufreq_table(struct cpufreq_frequency_table *table)
{
	mutex_lock(&mutex);
	if (arm_clk) {
		board_do_update_cpufreq_table(table);
	}
	freq_table = table;
	mutex_unlock(&mutex);
	return 0;
}

static int rk29_cpufreq_verify(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, freq_table);
}

#ifdef CONFIG_RK29_CPU_FREQ_LIMIT_BY_TEMP
static bool limit_vpu_enabled;
static bool limit_gpu_enabled;
static bool limit_gpu_high;
module_param(limit_vpu_enabled, bool, 0644);
module_param(limit_gpu_enabled, bool, 0644);
module_param(limit_gpu_high, bool, 0644);
static struct clk* clk_vpu;
static struct clk* clk_gpu;
#define GPU_LOW_RATE	(300 * MHZ)
static unsigned long limit_gpu_low_rate = GPU_LOW_RATE;
module_param(limit_gpu_low_rate, ulong, 0644);

#define TEMP_COEFF_IDLE -1000
#define TEMP_COEFF_408  -325
#define TEMP_COEFF_624  -202
#define TEMP_COEFF_816  -78
#define TEMP_COEFF_1008 325
#define TEMP_COEFF_1200 1300
#define WORK_DELAY      HZ
static void rk29_cpufreq_limit_by_temp(struct cpufreq_policy *policy, unsigned int relation, int *index)
{
	int c, ms;
	ktime_t now;
	static ktime_t last = { .tv64 = 0 };
	cputime64_t wall;
	u64 idle_time_us;
	static u64 last_idle_time_us;
	int idle;
	unsigned int cur = policy->cur;
	int overheat_temp_1200, overheat_temp;
	int temp;
	int target_index;
	unsigned int target_freq;
	bool overheat;

	if (!limit || !rk29_cpufreq_is_ondemand_policy(policy) ||
	    (limit_index_816 < 0) || (relation & MASK_FURTHER_CPUFREQ)) {
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

	temp = limit_temp;
	idle = idle_time_us - last_idle_time_us;
	if (idle) {
		temp -= idle; // -1000
		last_idle_time_us = idle_time_us;
	}

	ms = div_u64(ktime_us_delta(now, last), 1000);
	dprintk(DEBUG_TEMP, "%d kHz (%d uV) elapsed %d ms idle %d us\n", cur, vcore_uV, ms, idle);
	last = now;

	if (cur <= 408 * 1000)
		c = TEMP_COEFF_408;
	else if (cur <= 624 * 1000)
		c = TEMP_COEFF_624;
	else if (cur <= 816 * 1000)
		c = TEMP_COEFF_816;
	else if (cur <= 1008 * 1000)
		c = TEMP_COEFF_1008;
	else
		c = TEMP_COEFF_1200;
	temp += c * ms;

	if (temp < 0)
		temp = 0;

	target_index = *index;
	target_freq = freq_table[target_index].frequency;
	overheat_temp = TEMP_COEFF_1008 * limit_secs * MSEC_PER_SEC;
	overheat_temp_1200 = TEMP_COEFF_1200 * limit_secs_1200 * MSEC_PER_SEC;
	overheat = false;

	if (temp >= overheat_temp && target_freq > limit_freq_816) {
		target_index = limit_index_816;
		overheat = true;
	} else if (target_freq > limit_freq_1008 && limit_freq_1008 > limit_freq_816 &&
		 temp >= overheat_temp_1200 && temp < overheat_temp) {
		target_index = limit_index_1008;
		overheat = true;
	} else if (target_freq > 1008000 && (limit_vpu_enabled || (limit_gpu_enabled && limit_gpu_high))) {
		target_index = limit_index_1008;
	}

	dprintk(DEBUG_TEMP, "%d kHz c %d temp %d (%s) selected %d kHz\n", target_freq, c, temp, overheat ? "overheat" : "normal", freq_table[target_index].frequency);
	limit_temp = temp;
	*index = target_index;
}
#else
#define rk29_cpufreq_limit_by_temp(...) do {} while (0)
#endif

#ifdef CONFIG_RK29_CPU_FREQ_LIMIT_BY_DISP
static void rk29_cpufreq_limit_by_disp(int *index)
{
	unsigned int frequency = freq_table[*index].frequency;
	int new_index = -1;

	if (!aclk_limit())
		return;

	if (ddr_max_rate < 492 * MHZ) {
		if (limit_index_816 >= 0 && frequency > 816000)
			new_index = limit_index_816;
	} else {
		if (limit_index_1008 >= 0 && frequency > 1008000)
			new_index = limit_index_1008;
	}

	if (new_index != -1) {
		dprintk(DEBUG_DISP, "old %d new %d\n", freq_table[*index].frequency, freq_table[new_index].frequency);
		*index = new_index;
	}
}
#else
#define rk29_cpufreq_limit_by_disp(...) do {} while (0)
#endif

static int rk29_cpufreq_do_target(struct cpufreq_policy *policy, unsigned int target_freq, unsigned int relation)
{
	int index;
	int new_vcore_uV;
	struct cpufreq_freqs freqs;
	const struct cpufreq_frequency_table *freq;
	int err = 0;
	bool force = relation & CPUFREQ_FORCE_CHANGE;
	unsigned long new_arm_rate;

	relation &= ~CPUFREQ_FORCE_CHANGE;

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
	rk29_cpufreq_limit_by_disp(&index);
	rk29_cpufreq_limit_by_temp(policy, relation, &index);
	freq = &freq_table[index];

	if (policy->cur == freq->frequency && !force)
		return 0;

	freqs.old = policy->cur;
	freqs.new = freq->frequency;
	freqs.cpu = 0;
	new_vcore_uV = freq->index;
	new_arm_rate = freqs.new * 1000;
	dprintk(DEBUG_CHANGE, "%d kHz r %d(%c) selected %d kHz (%d uV)\n",
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
	clk_set_rate(arm_clk, freqs.new * 1000 + aclk_limit());
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
	dprintk(DEBUG_CHANGE, "got %d kHz\n", freqs.new);

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

#ifdef CONFIG_RK29_CPU_FREQ_LIMIT_BY_TEMP
static void rk29_cpufreq_limit_by_temp_work_func(struct work_struct *work);
static DECLARE_DELAYED_WORK(rk29_cpufreq_limit_by_temp_work, rk29_cpufreq_limit_by_temp_work_func);

static int rk29_cpufreq_notifier_policy(struct notifier_block *nb,
		unsigned long val, void *data)
{
	struct cpufreq_policy *policy = data;

	if (val != CPUFREQ_NOTIFY)
		return 0;

	if (rk29_cpufreq_is_ondemand_policy(policy)) {
		dprintk(DEBUG_TEMP, "queue work\n");
		queue_delayed_work(wq, &rk29_cpufreq_limit_by_temp_work, WORK_DELAY);
	} else {
		dprintk(DEBUG_TEMP, "cancel work\n");
		cancel_delayed_work(&rk29_cpufreq_limit_by_temp_work);
	}

	return 0;
}

static struct notifier_block notifier_policy_block = {
	.notifier_call = rk29_cpufreq_notifier_policy
};

static void rk29_cpufreq_limit_by_temp_work_func(struct work_struct *work)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(0);

	if (policy) {
		dprintk(DEBUG_TEMP, "check %d kHz\n", policy->cur);
		cpufreq_driver_target(policy, policy->cur, CPUFREQ_RELATION_L);
		cpufreq_cpu_put(policy);
	}
	queue_delayed_work(wq, &rk29_cpufreq_limit_by_temp_work, WORK_DELAY);
}

static int rk29_cpufreq_vpu_notifier_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	switch (event) {
	case CLK_PRE_ENABLE:
		limit_vpu_enabled = true;
		break;
	case CLK_ABORT_ENABLE:
	case CLK_POST_DISABLE:
		limit_vpu_enabled = false;
		break;
	default:
		return NOTIFY_OK;
	}

	if (limit_vpu_enabled) {
		struct cpufreq_policy *policy = cpufreq_cpu_get(0);

		if (policy) {
			dprintk(DEBUG_TEMP, "vpu on\n");
			cpufreq_driver_target(policy, policy->cur, CPUFREQ_RELATION_L);
			cpufreq_cpu_put(policy);
		}
	}
	return NOTIFY_OK;
}

static struct notifier_block rk29_cpufreq_vpu_notifier = {
	.notifier_call = rk29_cpufreq_vpu_notifier_event,
};

static int rk29_cpufreq_gpu_notifier_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	struct clk_notifier_data *cnd = ptr;
	bool gpu_high_old = limit_gpu_enabled && limit_gpu_high;
	bool gpu_high;

	switch (event) {
	case CLK_PRE_RATE_CHANGE:
		if (cnd->new_rate > limit_gpu_low_rate)
			limit_gpu_high = true;
		break;
	case CLK_ABORT_RATE_CHANGE:
		if (cnd->new_rate > limit_gpu_low_rate && cnd->old_rate <= limit_gpu_low_rate)
			limit_gpu_high = false;
		break;
	case CLK_POST_RATE_CHANGE:
		if (cnd->new_rate <= limit_gpu_low_rate)
			limit_gpu_high = false;
		break;
	case CLK_PRE_ENABLE:
		limit_gpu_enabled = true;
		break;
	case CLK_ABORT_ENABLE:
	case CLK_POST_DISABLE:
		limit_gpu_enabled = false;
		break;
	default:
		return NOTIFY_OK;
	}

	gpu_high = limit_gpu_enabled && limit_gpu_high;
	if (gpu_high_old != gpu_high && gpu_high) {
		struct cpufreq_policy *policy = cpufreq_cpu_get(0);

		if (policy) {
			dprintk(DEBUG_TEMP, "gpu high\n");
			cpufreq_driver_target(policy, policy->cur, CPUFREQ_RELATION_L);
			cpufreq_cpu_put(policy);
		}
	}
	return NOTIFY_OK;
}

static struct notifier_block rk29_cpufreq_gpu_notifier = {
	.notifier_call = rk29_cpufreq_gpu_notifier_event,
};
#endif

#ifdef CONFIG_RK29_CPU_FREQ_LIMIT_BY_DISP
static void rk29_cpufreq_limit_by_disp_work_func(struct work_struct *work)
{
	struct cpufreq_policy *policy;

	policy = cpufreq_cpu_get(0);
	if (policy) {
		cpufreq_driver_target(policy, policy->cur, CPUFREQ_RELATION_L | CPUFREQ_FORCE_CHANGE);
		cpufreq_cpu_put(policy);
	}
}

static DECLARE_WORK(rk29_cpufreq_limit_by_disp_work, rk29_cpufreq_limit_by_disp_work_func);

static int rk29_cpufreq_fb_notifier_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	switch (event) {
	case RK29FB_EVENT_HDMI_ON:
		limit_hdmi_enabled = true;
		break;
	case RK29FB_EVENT_HDMI_OFF:
		limit_hdmi_enabled = false;
		break;
	case RK29FB_EVENT_FB1_ON:
		limit_fb1_enabled = true;
		break;
	case RK29FB_EVENT_FB1_OFF:
		limit_fb1_enabled = false;
		break;
	}

	dprintk(DEBUG_DISP, "event: %lu aclk_limit: %d\n", event, aclk_limit());
	flush_work(&rk29_cpufreq_limit_by_disp_work);
	queue_work(wq, &rk29_cpufreq_limit_by_disp_work);

	return NOTIFY_OK;
}

static struct notifier_block rk29_cpufreq_fb_notifier = {
	.notifier_call = rk29_cpufreq_fb_notifier_event,
};
#endif

static int rk29_cpufreq_init(struct cpufreq_policy *policy)
{
	if (policy->cpu != 0)
		return -EINVAL;

	arm_clk = clk_get(NULL, "arm_pll");
	if (IS_ERR(arm_clk)) {
		int err = PTR_ERR(arm_clk);
		pr_err("fail to get arm_pll clk: %d\n", err);
		arm_clk = NULL;
		return err;
	}

	ddr_clk = clk_get(NULL, "ddr");
	if (IS_ERR(ddr_clk)) {
		int err = PTR_ERR(ddr_clk);
		pr_err("fail to get ddr clk: %d\n", err);
		ddr_clk = NULL;
		return err;
	}
	ddr_max_rate = clk_get_rate(ddr_clk);

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

	wq = create_singlethread_workqueue("rk29_cpufreqd");
	if (!wq) {
		pr_err("fail to create workqueue\n");
		return -ENOMEM;
	}

#ifdef CONFIG_RK29_CPU_FREQ_LIMIT_BY_TEMP
	if (rk29_cpufreq_is_ondemand_policy(policy)) {
		dprintk(DEBUG_TEMP, "queue work\n");
		queue_delayed_work(wq, &rk29_cpufreq_limit_by_temp_work, WORK_DELAY);
	}
	cpufreq_register_notifier(&notifier_policy_block, CPUFREQ_POLICY_NOTIFIER);
	if (limit_max_freq > 1008000) {
		clk_gpu = clk_get(NULL, "gpu");
		clk_vpu = clk_get(NULL, "vpu");
		clk_notifier_register(clk_gpu, &rk29_cpufreq_gpu_notifier);
		clk_notifier_register(clk_vpu, &rk29_cpufreq_vpu_notifier);
	}
#endif
#ifdef CONFIG_RK29_CPU_FREQ_LIMIT_BY_DISP
	rk29fb_register_notifier(&rk29_cpufreq_fb_notifier);
#endif
	return 0;
}

static int rk29_cpufreq_exit(struct cpufreq_policy *policy)
{
#ifdef CONFIG_RK29_CPU_FREQ_LIMIT_BY_DISP
	rk29fb_unregister_notifier(&rk29_cpufreq_fb_notifier);
#endif
#ifdef CONFIG_RK29_CPU_FREQ_LIMIT_BY_TEMP
	if (limit_max_freq > 1008000) {
		clk_notifier_unregister(clk_gpu, &rk29_cpufreq_gpu_notifier);
		clk_notifier_unregister(clk_vpu, &rk29_cpufreq_vpu_notifier);
		clk_put(clk_gpu);
		clk_put(clk_vpu);
	}
	cpufreq_unregister_notifier(&notifier_policy_block, CPUFREQ_POLICY_NOTIFIER);
	if (wq)
		cancel_delayed_work(&rk29_cpufreq_limit_by_temp_work);
#endif
	if (wq) {
		flush_workqueue(wq);
		destroy_workqueue(wq);
		wq = NULL;
	}
#ifdef CONFIG_REGULATOR
	if (vcore)
		regulator_put(vcore);
#endif
	clk_put(ddr_clk);
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
		ret = cpufreq_driver_target(policy, limit_avg_freq, DISABLE_FURTHER_CPUFREQ | CPUFREQ_RELATION_H);
		if (ret < 0) {
			ret = NOTIFY_BAD;
			goto out;
		}
		ret = NOTIFY_OK;
		break;
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		cpufreq_driver_target(policy, limit_avg_freq, ENABLE_FURTHER_CPUFREQ | CPUFREQ_RELATION_H);
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

static int rk29_cpufreq_reboot_notifier_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(0);

	if (policy) {
		cpufreq_driver_target(policy, limit_avg_freq, DISABLE_FURTHER_CPUFREQ | CPUFREQ_RELATION_H);
		cpufreq_cpu_put(policy);
	}

	return NOTIFY_OK;
}

static struct notifier_block rk29_cpufreq_reboot_notifier = {
	.notifier_call = rk29_cpufreq_reboot_notifier_event,
};

static int __init rk29_cpufreq_register(void)
{
	register_pm_notifier(&rk29_cpufreq_pm_notifier);
	register_reboot_notifier(&rk29_cpufreq_reboot_notifier);

	return cpufreq_register_driver(&rk29_cpufreq_driver);
}

device_initcall(rk29_cpufreq_register);

