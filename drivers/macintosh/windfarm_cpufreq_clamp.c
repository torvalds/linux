// SPDX-License-Identifier: GPL-2.0-only
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>

#include <asm/prom.h>

#include "windfarm.h"

#define VERSION "0.3"

static int clamped;
static struct wf_control *clamp_control;
static struct freq_qos_request qos_req;
static unsigned int min_freq, max_freq;

static int clamp_set(struct wf_control *ct, s32 value)
{
	unsigned int freq;

	if (value) {
		freq = min_freq;
		printk(KERN_INFO "windfarm: Clamping CPU frequency to "
		       "minimum !\n");
	} else {
		freq = max_freq;
		printk(KERN_INFO "windfarm: CPU frequency unclamped !\n");
	}
	clamped = value;

	return freq_qos_update_request(&qos_req, freq);
}

static int clamp_get(struct wf_control *ct, s32 *value)
{
	*value = clamped;
	return 0;
}

static s32 clamp_min(struct wf_control *ct)
{
	return 0;
}

static s32 clamp_max(struct wf_control *ct)
{
	return 1;
}

static const struct wf_control_ops clamp_ops = {
	.set_value	= clamp_set,
	.get_value	= clamp_get,
	.get_min	= clamp_min,
	.get_max	= clamp_max,
	.owner		= THIS_MODULE,
};

static int __init wf_cpufreq_clamp_init(void)
{
	struct cpufreq_policy *policy;
	struct wf_control *clamp;
	struct device *dev;
	int ret;

	policy = cpufreq_cpu_get(0);
	if (!policy) {
		pr_warn("%s: cpufreq policy not found cpu0\n", __func__);
		return -EPROBE_DEFER;
	}

	min_freq = policy->cpuinfo.min_freq;
	max_freq = policy->cpuinfo.max_freq;

	ret = freq_qos_add_request(&policy->constraints, &qos_req, FREQ_QOS_MAX,
				   max_freq);

	cpufreq_cpu_put(policy);

	if (ret < 0) {
		pr_err("%s: Failed to add freq constraint (%d)\n", __func__,
		       ret);
		return ret;
	}

	dev = get_cpu_device(0);
	if (unlikely(!dev)) {
		pr_warn("%s: No cpu device for cpu0\n", __func__);
		ret = -ENODEV;
		goto fail;
	}

	clamp = kmalloc(sizeof(struct wf_control), GFP_KERNEL);
	if (clamp == NULL) {
		ret = -ENOMEM;
		goto fail;
	}

	clamp->ops = &clamp_ops;
	clamp->name = "cpufreq-clamp";
	ret = wf_register_control(clamp);
	if (ret)
		goto free;

	clamp_control = clamp;
	return 0;

 free:
	kfree(clamp);
 fail:
	freq_qos_remove_request(&qos_req);
	return ret;
}

static void __exit wf_cpufreq_clamp_exit(void)
{
	if (clamp_control) {
		wf_unregister_control(clamp_control);
		freq_qos_remove_request(&qos_req);
	}
}


module_init(wf_cpufreq_clamp_init);
module_exit(wf_cpufreq_clamp_exit);

MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("CPU frequency clamp for PowerMacs thermal control");
MODULE_LICENSE("GPL");

