// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/workqueue.h>

#define CPUFREQ_CDEV_NAME "cpu%d"
#define CPUFREQ_CDEV "qcom-cpufreq-cdev"

struct cpufreq_cdev_device {
	struct list_head node;
	struct thermal_cooling_device *cdev;
	int cpu;
	unsigned long cur_state;
	unsigned long max_state;
	unsigned int *freq_table;
	struct freq_qos_request qos_max_freq_req;
	char cdev_name[THERMAL_NAME_LENGTH];
	struct cpufreq_policy *policy;
	struct work_struct reg_work;
};

static DEFINE_MUTEX(qti_cpufreq_cdev_lock);
static LIST_HEAD(qti_cpufreq_cdev_list);
static enum cpuhp_state cpu_hp_online;

static unsigned int state_to_cpufreq(struct cpufreq_cdev_device *cdev_data,
					   unsigned long state)
{
	return cdev_data->freq_table ?
			cdev_data->freq_table[state] : UINT_MAX;
}

static int cpufreq_cdev_set_state(struct thermal_cooling_device *cdev,
					unsigned long state)
{
	struct cpufreq_cdev_device *cdev_data = cdev->devdata;
	int ret = 0;
	unsigned int freq;

	if (state > cdev_data->max_state)
		return -EINVAL;
	if (state == cdev_data->cur_state)
		return 0;

	if (freq_qos_request_active(&cdev_data->qos_max_freq_req)) {
		freq = state_to_cpufreq(cdev_data, state);
		pr_debug("cdev:%s Limit:%u\n", cdev->type, freq);
		ret = freq_qos_update_request(&cdev_data->qos_max_freq_req,
						freq);
		if (ret < 0) {
			pr_err("Error placing qos request:%u. cdev:%s err:%d\n",
				freq, cdev->type, ret);
			return ret;
		}
	}
	cdev_data->cur_state = state;

	return 0;
}

static int cpufreq_cdev_get_state(struct thermal_cooling_device *cdev,
					unsigned long *state)
{
	struct cpufreq_cdev_device *cdev_data = cdev->devdata;

	*state = cdev_data->cur_state;
	return 0;
}

static int cpufreq_cdev_get_max_state(struct thermal_cooling_device *cdev,
					unsigned long *state)
{
	struct cpufreq_cdev_device *cdev_data = cdev->devdata;

	*state = cdev_data->max_state;
	return 0;
}

static struct thermal_cooling_device_ops cpufreq_cdev_ops = {
	.set_cur_state = cpufreq_cdev_set_state,
	.get_cur_state = cpufreq_cdev_get_state,
	.get_max_state = cpufreq_cdev_get_max_state,
};

static void cpufreq_cdev_register(struct work_struct *work)
{
	struct cpufreq_cdev_device *cdev_data = container_of(work,
			struct cpufreq_cdev_device, reg_work);
	struct cpufreq_policy *policy = NULL;
	int freq_count = 0, i;

	policy = cpufreq_cpu_get(cdev_data->cpu);
	if (!policy) {
		pr_err("No policy for CPU:%d\n", cdev_data->cpu);
		return;
	}
	freq_count = cpufreq_table_count_valid_entries(policy);
	if (!freq_count) {
		pr_debug("CPU%d freq policy table count error%d\n",
			cdev_data->cpu, freq_count);
		goto error_exit;
	}

	cdev_data->freq_table = kmalloc_array(freq_count,
					sizeof(*cdev_data->freq_table),
					GFP_KERNEL);
	if (!cdev_data->freq_table)
		goto error_exit;

	for (i = 0; i < freq_count; i++) {
		if (policy->freq_table_sorted ==
				CPUFREQ_TABLE_SORTED_ASCENDING)
			cdev_data->freq_table[i] =
			policy->freq_table[freq_count - i - 1].frequency;
		else
			cdev_data->freq_table[i] =
				policy->freq_table[i].frequency;
	}

	freq_count--;
	cdev_data->policy = policy;
	cdev_data->max_state = freq_count;
	cdev_data->cur_state = 0;
	freq_qos_add_request(&policy->constraints,
			   &cdev_data->qos_max_freq_req, FREQ_QOS_MAX,
			   state_to_cpufreq(cdev_data, 0));
	cdev_data->cdev = thermal_cooling_device_register(cdev_data->cdev_name,
						cdev_data, &cpufreq_cdev_ops);
	if (IS_ERR(cdev_data->cdev)) {
		pr_err("Cdev register failed for %s, ret:%d\n",
			cdev_data->cdev_name, PTR_ERR(cdev_data->cdev));
		freq_qos_remove_request(&cdev_data->qos_max_freq_req);
		goto error_exit;
	}

	pr_debug("Cdev %s registered\n", cdev_data->cdev_name);
	return;
error_exit:
	if (policy)
		cpufreq_cpu_put(policy);
	if (cdev_data->cdev)
		cdev_data->cdev = NULL;
	kfree(cdev_data->freq_table);
}

static int cpufreq_cdev_hp_online(unsigned int online_cpu)
{

	struct cpufreq_cdev_device *cdev_data;

	mutex_lock(&qti_cpufreq_cdev_lock);
	list_for_each_entry(cdev_data, &qti_cpufreq_cdev_list, node) {
		if (cdev_data->cpu != online_cpu || cdev_data->cdev)
			continue;
		queue_work(system_highpri_wq, &cdev_data->reg_work);
	}
	mutex_unlock(&qti_cpufreq_cdev_lock);
	return 0;
}

static int cpufreq_cdev_probe(struct platform_device *pdev)
{
	struct cpufreq_cdev_device *cdev_data;
	struct device_node *np = pdev->dev.of_node, *cpu_phandle = NULL;
	struct device_node *subsys_np = NULL;
	struct device *dev = &pdev->dev;
	struct device *cpu_dev;
	int cpu = 0, ret = 0;
	int cpu_count;
	struct of_phandle_iterator it;

	mutex_lock(&qti_cpufreq_cdev_lock);
	for_each_available_child_of_node(np, subsys_np) {
		of_phandle_iterator_init(&it, subsys_np, "qcom,cpus", NULL, 0);
		cpu_count = 0;
		while (of_phandle_iterator_next(&it) == 0) {
			cpu_phandle = it.node;
			for_each_possible_cpu(cpu) {
				cpu_dev = get_cpu_device(cpu);
				if (cpu_dev && (cpu_dev->of_node == cpu_phandle)) {
					cpu_count++;
					break;
				}
			}
			if (cpu_count)
				break;
		}
		if (!cpu_count)
			continue;
		cdev_data = devm_kzalloc(dev, sizeof(*cdev_data), GFP_KERNEL);
		if (!cdev_data) {
			mutex_unlock(&qti_cpufreq_cdev_lock);
			return -ENOMEM;
		}
		cdev_data->cpu = cpu;
		snprintf(cdev_data->cdev_name, THERMAL_NAME_LENGTH,
				subsys_np->name);
		INIT_WORK(&cdev_data->reg_work,
				cpufreq_cdev_register);
		list_add(&cdev_data->node, &qti_cpufreq_cdev_list);
	}
	mutex_unlock(&qti_cpufreq_cdev_lock);

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "thermal-cpu/cdev:online",
				cpufreq_cdev_hp_online, NULL);
	if (ret < 0)
		return ret;
	cpu_hp_online = ret;

	return 0;
}

static int cpufreq_cdev_remove(struct platform_device *pdev)
{
	struct cpufreq_cdev_device *cdev_data;

	mutex_lock(&qti_cpufreq_cdev_lock);
	if (cpu_hp_online) {
		cpuhp_remove_state_nocalls(cpu_hp_online);
		cpu_hp_online = 0;
	}
	list_for_each_entry(cdev_data, &qti_cpufreq_cdev_list, node) {
		if (!cdev_data->cdev)
			continue;
		thermal_cooling_device_unregister(cdev_data->cdev);
		if (freq_qos_request_active(&cdev_data->qos_max_freq_req))
			freq_qos_remove_request(&cdev_data->qos_max_freq_req);
		cdev_data->cdev = NULL;
		cpufreq_cpu_put(cdev_data->policy);
		kfree(cdev_data->freq_table);
	}
	mutex_unlock(&qti_cpufreq_cdev_lock);
	return 0;
}

static const struct of_device_id cpufreq_cdev_match[] = {
	{.compatible = "qcom,cpufreq-cdev"},
	{}
};

static struct platform_driver cpufreq_cdev_driver = {
	.probe          = cpufreq_cdev_probe,
	.remove         = cpufreq_cdev_remove,
	.driver         = {
		.name   = CPUFREQ_CDEV,
		.of_match_table = cpufreq_cdev_match,
	},
};

static int __init cpufreq_cdev_init(void)
{
	return platform_driver_register(&cpufreq_cdev_driver);
}
module_init(cpufreq_cdev_init);

static void __exit cpufreq_cdev_exit(void)
{
	platform_driver_unregister(&cpufreq_cdev_driver);
}
module_exit(cpufreq_cdev_exit);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. cpufreq cooling driver");
MODULE_LICENSE("GPL");
