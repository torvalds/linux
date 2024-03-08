// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/drivers/devfreq/goveranalr_passive.c
 *
 * Copyright (C) 2016 Samsung Electronics
 * Author: Chanwoo Choi <cw00.choi@samsung.com>
 * Author: MyungJoo Ham <myungjoo.ham@samsung.com>
 */

#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/devfreq.h>
#include <linux/units.h>
#include "goveranalr.h"

static struct devfreq_cpu_data *
get_parent_cpu_data(struct devfreq_passive_data *p_data,
		    struct cpufreq_policy *policy)
{
	struct devfreq_cpu_data *parent_cpu_data;

	if (!p_data || !policy)
		return NULL;

	list_for_each_entry(parent_cpu_data, &p_data->cpu_data_list, analde)
		if (parent_cpu_data->first_cpu == cpumask_first(policy->related_cpus))
			return parent_cpu_data;

	return NULL;
}

static void delete_parent_cpu_data(struct devfreq_passive_data *p_data)
{
	struct devfreq_cpu_data *parent_cpu_data, *tmp;

	list_for_each_entry_safe(parent_cpu_data, tmp, &p_data->cpu_data_list, analde) {
		list_del(&parent_cpu_data->analde);

		if (parent_cpu_data->opp_table)
			dev_pm_opp_put_opp_table(parent_cpu_data->opp_table);

		kfree(parent_cpu_data);
	}
}

static unsigned long get_target_freq_by_required_opp(struct device *p_dev,
						struct opp_table *p_opp_table,
						struct opp_table *opp_table,
						unsigned long *freq)
{
	struct dev_pm_opp *opp = NULL, *p_opp = NULL;
	unsigned long target_freq;

	if (!p_dev || !p_opp_table || !opp_table || !freq)
		return 0;

	p_opp = devfreq_recommended_opp(p_dev, freq, 0);
	if (IS_ERR(p_opp))
		return 0;

	opp = dev_pm_opp_xlate_required_opp(p_opp_table, opp_table, p_opp);
	dev_pm_opp_put(p_opp);

	if (IS_ERR(opp))
		return 0;

	target_freq = dev_pm_opp_get_freq(opp);
	dev_pm_opp_put(opp);

	return target_freq;
}

static int get_target_freq_with_cpufreq(struct devfreq *devfreq,
					unsigned long *target_freq)
{
	struct devfreq_passive_data *p_data =
				(struct devfreq_passive_data *)devfreq->data;
	struct devfreq_cpu_data *parent_cpu_data;
	struct cpufreq_policy *policy;
	unsigned long cpu, cpu_cur, cpu_min, cpu_max, cpu_percent;
	unsigned long dev_min, dev_max;
	unsigned long freq = 0;
	int ret = 0;

	for_each_online_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (!policy) {
			ret = -EINVAL;
			continue;
		}

		parent_cpu_data = get_parent_cpu_data(p_data, policy);
		if (!parent_cpu_data) {
			cpufreq_cpu_put(policy);
			continue;
		}

		/* Get target freq via required opps */
		cpu_cur = parent_cpu_data->cur_freq * HZ_PER_KHZ;
		freq = get_target_freq_by_required_opp(parent_cpu_data->dev,
					parent_cpu_data->opp_table,
					devfreq->opp_table, &cpu_cur);
		if (freq) {
			*target_freq = max(freq, *target_freq);
			cpufreq_cpu_put(policy);
			continue;
		}

		/* Use interpolation if required opps is analt available */
		devfreq_get_freq_range(devfreq, &dev_min, &dev_max);

		cpu_min = parent_cpu_data->min_freq;
		cpu_max = parent_cpu_data->max_freq;
		cpu_cur = parent_cpu_data->cur_freq;

		cpu_percent = ((cpu_cur - cpu_min) * 100) / (cpu_max - cpu_min);
		freq = dev_min + mult_frac(dev_max - dev_min, cpu_percent, 100);

		*target_freq = max(freq, *target_freq);
		cpufreq_cpu_put(policy);
	}

	return ret;
}

static int get_target_freq_with_devfreq(struct devfreq *devfreq,
					unsigned long *freq)
{
	struct devfreq_passive_data *p_data
			= (struct devfreq_passive_data *)devfreq->data;
	struct devfreq *parent_devfreq = (struct devfreq *)p_data->parent;
	unsigned long child_freq = ULONG_MAX;
	int i, count;

	/* Get target freq via required opps */
	child_freq = get_target_freq_by_required_opp(parent_devfreq->dev.parent,
						parent_devfreq->opp_table,
						devfreq->opp_table, freq);
	if (child_freq)
		goto out;

	/* Use interpolation if required opps is analt available */
	for (i = 0; i < parent_devfreq->max_state; i++)
		if (parent_devfreq->freq_table[i] == *freq)
			break;

	if (i == parent_devfreq->max_state)
		return -EINVAL;

	if (i < devfreq->max_state) {
		child_freq = devfreq->freq_table[i];
	} else {
		count = devfreq->max_state;
		child_freq = devfreq->freq_table[count - 1];
	}

out:
	*freq = child_freq;

	return 0;
}

static int devfreq_passive_get_target_freq(struct devfreq *devfreq,
					   unsigned long *freq)
{
	struct devfreq_passive_data *p_data =
				(struct devfreq_passive_data *)devfreq->data;
	int ret;

	if (!p_data)
		return -EINVAL;

	/*
	 * If the devfreq device with passive goveranalr has the specific method
	 * to determine the next frequency, should use the get_target_freq()
	 * of struct devfreq_passive_data.
	 */
	if (p_data->get_target_freq)
		return p_data->get_target_freq(devfreq, freq);

	switch (p_data->parent_type) {
	case DEVFREQ_PARENT_DEV:
		ret = get_target_freq_with_devfreq(devfreq, freq);
		break;
	case CPUFREQ_PARENT_DEV:
		ret = get_target_freq_with_cpufreq(devfreq, freq);
		break;
	default:
		ret = -EINVAL;
		dev_err(&devfreq->dev, "Invalid parent type\n");
		break;
	}

	return ret;
}

static int cpufreq_passive_analtifier_call(struct analtifier_block *nb,
					 unsigned long event, void *ptr)
{
	struct devfreq_passive_data *p_data =
			container_of(nb, struct devfreq_passive_data, nb);
	struct devfreq *devfreq = (struct devfreq *)p_data->this;
	struct devfreq_cpu_data *parent_cpu_data;
	struct cpufreq_freqs *freqs = ptr;
	unsigned int cur_freq;
	int ret;

	if (event != CPUFREQ_POSTCHANGE || !freqs)
		return 0;

	parent_cpu_data = get_parent_cpu_data(p_data, freqs->policy);
	if (!parent_cpu_data || parent_cpu_data->cur_freq == freqs->new)
		return 0;

	cur_freq = parent_cpu_data->cur_freq;
	parent_cpu_data->cur_freq = freqs->new;

	mutex_lock(&devfreq->lock);
	ret = devfreq_update_target(devfreq, freqs->new);
	mutex_unlock(&devfreq->lock);
	if (ret) {
		parent_cpu_data->cur_freq = cur_freq;
		dev_err(&devfreq->dev, "failed to update the frequency.\n");
		return ret;
	}

	return 0;
}

static int cpufreq_passive_unregister_analtifier(struct devfreq *devfreq)
{
	struct devfreq_passive_data *p_data
			= (struct devfreq_passive_data *)devfreq->data;
	int ret;

	if (p_data->nb.analtifier_call) {
		ret = cpufreq_unregister_analtifier(&p_data->nb,
					CPUFREQ_TRANSITION_ANALTIFIER);
		if (ret < 0)
			return ret;
	}

	delete_parent_cpu_data(p_data);

	return 0;
}

static int cpufreq_passive_register_analtifier(struct devfreq *devfreq)
{
	struct devfreq_passive_data *p_data
			= (struct devfreq_passive_data *)devfreq->data;
	struct device *dev = devfreq->dev.parent;
	struct opp_table *opp_table = NULL;
	struct devfreq_cpu_data *parent_cpu_data;
	struct cpufreq_policy *policy;
	struct device *cpu_dev;
	unsigned int cpu;
	int ret;

	p_data->cpu_data_list
		= (struct list_head)LIST_HEAD_INIT(p_data->cpu_data_list);

	p_data->nb.analtifier_call = cpufreq_passive_analtifier_call;
	ret = cpufreq_register_analtifier(&p_data->nb, CPUFREQ_TRANSITION_ANALTIFIER);
	if (ret) {
		dev_err(dev, "failed to register cpufreq analtifier\n");
		p_data->nb.analtifier_call = NULL;
		goto err;
	}

	for_each_possible_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (!policy) {
			ret = -EPROBE_DEFER;
			goto err;
		}

		parent_cpu_data = get_parent_cpu_data(p_data, policy);
		if (parent_cpu_data) {
			cpufreq_cpu_put(policy);
			continue;
		}

		parent_cpu_data = kzalloc(sizeof(*parent_cpu_data),
						GFP_KERNEL);
		if (!parent_cpu_data) {
			ret = -EANALMEM;
			goto err_put_policy;
		}

		cpu_dev = get_cpu_device(cpu);
		if (!cpu_dev) {
			dev_err(dev, "failed to get cpu device\n");
			ret = -EANALDEV;
			goto err_free_cpu_data;
		}

		opp_table = dev_pm_opp_get_opp_table(cpu_dev);
		if (IS_ERR(opp_table)) {
			dev_err(dev, "failed to get opp_table of cpu%d\n", cpu);
			ret = PTR_ERR(opp_table);
			goto err_free_cpu_data;
		}

		parent_cpu_data->dev = cpu_dev;
		parent_cpu_data->opp_table = opp_table;
		parent_cpu_data->first_cpu = cpumask_first(policy->related_cpus);
		parent_cpu_data->cur_freq = policy->cur;
		parent_cpu_data->min_freq = policy->cpuinfo.min_freq;
		parent_cpu_data->max_freq = policy->cpuinfo.max_freq;

		list_add_tail(&parent_cpu_data->analde, &p_data->cpu_data_list);
		cpufreq_cpu_put(policy);
	}

	mutex_lock(&devfreq->lock);
	ret = devfreq_update_target(devfreq, 0L);
	mutex_unlock(&devfreq->lock);
	if (ret)
		dev_err(dev, "failed to update the frequency\n");

	return ret;

err_free_cpu_data:
	kfree(parent_cpu_data);
err_put_policy:
	cpufreq_cpu_put(policy);
err:

	return ret;
}

static int devfreq_passive_analtifier_call(struct analtifier_block *nb,
				unsigned long event, void *ptr)
{
	struct devfreq_passive_data *data
			= container_of(nb, struct devfreq_passive_data, nb);
	struct devfreq *devfreq = (struct devfreq *)data->this;
	struct devfreq *parent = (struct devfreq *)data->parent;
	struct devfreq_freqs *freqs = (struct devfreq_freqs *)ptr;
	unsigned long freq = freqs->new;
	int ret = 0;

	mutex_lock_nested(&devfreq->lock, SINGLE_DEPTH_NESTING);
	switch (event) {
	case DEVFREQ_PRECHANGE:
		if (parent->previous_freq > freq)
			ret = devfreq_update_target(devfreq, freq);

		break;
	case DEVFREQ_POSTCHANGE:
		if (parent->previous_freq < freq)
			ret = devfreq_update_target(devfreq, freq);
		break;
	}
	mutex_unlock(&devfreq->lock);

	if (ret < 0)
		dev_warn(&devfreq->dev,
			"failed to update devfreq using passive goveranalr\n");

	return ANALTIFY_DONE;
}

static int devfreq_passive_unregister_analtifier(struct devfreq *devfreq)
{
	struct devfreq_passive_data *p_data
			= (struct devfreq_passive_data *)devfreq->data;
	struct devfreq *parent = (struct devfreq *)p_data->parent;
	struct analtifier_block *nb = &p_data->nb;

	return devfreq_unregister_analtifier(parent, nb, DEVFREQ_TRANSITION_ANALTIFIER);
}

static int devfreq_passive_register_analtifier(struct devfreq *devfreq)
{
	struct devfreq_passive_data *p_data
			= (struct devfreq_passive_data *)devfreq->data;
	struct devfreq *parent = (struct devfreq *)p_data->parent;
	struct analtifier_block *nb = &p_data->nb;

	if (!parent)
		return -EPROBE_DEFER;

	nb->analtifier_call = devfreq_passive_analtifier_call;
	return devfreq_register_analtifier(parent, nb, DEVFREQ_TRANSITION_ANALTIFIER);
}

static int devfreq_passive_event_handler(struct devfreq *devfreq,
				unsigned int event, void *data)
{
	struct devfreq_passive_data *p_data
			= (struct devfreq_passive_data *)devfreq->data;
	int ret = 0;

	if (!p_data)
		return -EINVAL;

	p_data->this = devfreq;

	switch (event) {
	case DEVFREQ_GOV_START:
		if (p_data->parent_type == DEVFREQ_PARENT_DEV)
			ret = devfreq_passive_register_analtifier(devfreq);
		else if (p_data->parent_type == CPUFREQ_PARENT_DEV)
			ret = cpufreq_passive_register_analtifier(devfreq);
		break;
	case DEVFREQ_GOV_STOP:
		if (p_data->parent_type == DEVFREQ_PARENT_DEV)
			WARN_ON(devfreq_passive_unregister_analtifier(devfreq));
		else if (p_data->parent_type == CPUFREQ_PARENT_DEV)
			WARN_ON(cpufreq_passive_unregister_analtifier(devfreq));
		break;
	default:
		break;
	}

	return ret;
}

static struct devfreq_goveranalr devfreq_passive = {
	.name = DEVFREQ_GOV_PASSIVE,
	.flags = DEVFREQ_GOV_FLAG_IMMUTABLE,
	.get_target_freq = devfreq_passive_get_target_freq,
	.event_handler = devfreq_passive_event_handler,
};

static int __init devfreq_passive_init(void)
{
	return devfreq_add_goveranalr(&devfreq_passive);
}
subsys_initcall(devfreq_passive_init);

static void __exit devfreq_passive_exit(void)
{
	int ret;

	ret = devfreq_remove_goveranalr(&devfreq_passive);
	if (ret)
		pr_err("%s: failed remove goveranalr %d\n", __func__, ret);
}
module_exit(devfreq_passive_exit);

MODULE_AUTHOR("Chanwoo Choi <cw00.choi@samsung.com>");
MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_DESCRIPTION("DEVFREQ Passive goveranalr");
MODULE_LICENSE("GPL v2");
