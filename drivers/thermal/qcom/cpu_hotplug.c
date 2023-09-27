// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/device.h>

#define CPU_HOTPLUG_LEVEL 1

struct cpu_hot_cdev {
	struct list_head node;
	int cpu_id;
	bool cpu_hot_state;
	bool cpu_cur_state;
	struct thermal_cooling_device *cdev;
	struct device_node *np;
	char cdev_name[THERMAL_NAME_LENGTH];
	struct work_struct reg_work;
	struct work_struct exec_work;
};
static enum cpuhp_state cpu_hp_online;
static DEFINE_MUTEX(cpu_hot_lock);
static LIST_HEAD(cpu_hot_cdev_list);

static int cpu_hot_hp_online(unsigned int online_cpu)
{
	struct cpu_hot_cdev *cpu_hot_cdev;
	int ret = 0;

	mutex_lock(&cpu_hot_lock);
	list_for_each_entry(cpu_hot_cdev, &cpu_hot_cdev_list, node) {
		if (online_cpu != cpu_hot_cdev->cpu_id)
			continue;

		if (cpu_hot_cdev->cdev) {
			if (cpu_hot_cdev->cpu_hot_state) {
				pr_debug("Offline CPU:%d\n",
						online_cpu);
				ret = NOTIFY_BAD;
			}
		} else
			queue_work(system_highpri_wq, &cpu_hot_cdev->reg_work);

		break;
	}
	mutex_unlock(&cpu_hot_lock);

	return ret;
}

/**
 * cpu_hot_set_cur_state - callback function to set the current cooling
 *				state.
 * @cdev: thermal cooling device pointer.
 * @state: set this variable to the current cooling state.
 *
 * Callback for the thermal cooling device to change the cpu hotplug
 * current cooling state.
 *
 * Return: 0 on success, an error code otherwise.
 */
static int cpu_hot_set_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long state)
{
	struct cpu_hot_cdev *cpu_hot_cdev = cdev->devdata;

	if (cpu_hot_cdev->cpu_id == -1)
		return -ENODEV;

	/* Request state should be less than max_level */
	if (state > CPU_HOTPLUG_LEVEL)
		return -EINVAL;

	state = !!state;
	/* Check if the old cooling action is same as new cooling action */
	if (cpu_hot_cdev->cpu_hot_state == state)
		return 0;

	mutex_lock(&cpu_hot_lock);
	cpu_hot_cdev->cpu_hot_state = state;
	mutex_unlock(&cpu_hot_lock);
	queue_work(system_highpri_wq, &cpu_hot_cdev->exec_work);

	return 0;
}

/**
 * cpu_hot_get_cur_state - callback function to get the current cooling
 *				state.
 * @cdev: thermal cooling device pointer.
 * @state: fill this variable with the current cooling state.
 *
 * Callback for the thermal cooling device to return the cpu hotplug
 * current cooling state.
 *
 * Return: 0 on success, an error code otherwise.
 */
static int cpu_hot_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct cpu_hot_cdev *cpu_hot_cdev = cdev->devdata;

	mutex_lock(&cpu_hot_lock);
	*state = (cpu_hot_cdev->cpu_hot_state) ?
			CPU_HOTPLUG_LEVEL : 0;
	mutex_unlock(&cpu_hot_lock);

	return 0;
}

/**
 * cpu_hot_get_max_state - callback function to get the max cooling state.
 * @cdev: thermal cooling device pointer.
 * @state: fill this variable with the max cooling state.
 *
 * Callback for the thermal cooling device to return the cpu
 * hotplug max cooling state.
 *
 * Return: 0 on success, an error code otherwise.
 */
static int cpu_hot_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	*state = CPU_HOTPLUG_LEVEL;
	return 0;
}

static struct thermal_cooling_device_ops cpu_hot_cooling_ops = {
	.get_max_state = cpu_hot_get_max_state,
	.get_cur_state = cpu_hot_get_cur_state,
	.set_cur_state = cpu_hot_set_cur_state,
};

static void cpu_hot_execute_cdev(struct work_struct *work)
{
	struct cpu_hot_cdev *cpu_hot_cdev =
			container_of(work, struct cpu_hot_cdev, exec_work);
	int ret = 0, cpu = 0;

	mutex_lock(&cpu_hot_lock);
	cpu = cpu_hot_cdev->cpu_id;
	if (cpu_hot_cdev->cpu_hot_state == CPU_HOTPLUG_LEVEL) {
		if (!cpu_hot_cdev->cpu_cur_state)
			goto unlock_exit;
		mutex_unlock(&cpu_hot_lock);
		ret = remove_cpu(cpu);
		mutex_lock(&cpu_hot_lock);
		if (ret < 0)
			pr_err("CPU:%d offline error:%d\n", cpu, ret);
		else
			cpu_hot_cdev->cpu_cur_state = false;
	} else {
		if (cpu_hot_cdev->cpu_cur_state)
			goto unlock_exit;
		mutex_unlock(&cpu_hot_lock);
		ret = add_cpu(cpu);
		mutex_lock(&cpu_hot_lock);
		if (ret)
			pr_err("CPU:%d online error:%d\n", cpu, ret);
		else
			cpu_hot_cdev->cpu_cur_state = true;
	}
unlock_exit:
	mutex_unlock(&cpu_hot_lock);
}

static void cpu_hot_register_cdev(struct work_struct *work)
{
	struct cpu_hot_cdev *cpu_hot_cdev =
			container_of(work, struct cpu_hot_cdev, reg_work);
	int ret = 0;

	cpu_hot_cdev->cdev = thermal_of_cooling_device_register(
					cpu_hot_cdev->np,
					cpu_hot_cdev->cdev_name,
					cpu_hot_cdev,
					&cpu_hot_cooling_ops);
	if (IS_ERR(cpu_hot_cdev->cdev)) {
		ret = PTR_ERR(cpu_hot_cdev->cdev);
		pr_err("Cooling register failed for %s, ret:%d\n",
			cpu_hot_cdev->cdev_name, ret);
		cpu_hot_cdev->cdev = NULL;
		return;
	}
	pr_debug("Cooling device [%s] registered.\n", cpu_hot_cdev->cdev_name);
}

static int cpu_hot_probe(struct platform_device *pdev)
{
	int ret = 0, cpu = 0;
	struct device_node *dev_phandle, *subsys_np = NULL;
	struct device *cpu_dev;
	struct cpu_hot_cdev *cpu_hot_cdev = NULL;
	struct device_node *np = pdev->dev.of_node;
	const char *alias;

	INIT_LIST_HEAD(&cpu_hot_cdev_list);
	for_each_available_child_of_node(np, subsys_np) {
		cpu_hot_cdev = devm_kzalloc(&pdev->dev,
				sizeof(*cpu_hot_cdev), GFP_KERNEL);
		if (!cpu_hot_cdev) {
			of_node_put(subsys_np);
			return -ENOMEM;
		}
		cpu_hot_cdev->cpu_id = -1;
		cpu_hot_cdev->cpu_hot_state = false;
		cpu_hot_cdev->cpu_cur_state = true;
		cpu_hot_cdev->cdev = NULL;
		cpu_hot_cdev->np = subsys_np;

		dev_phandle = of_parse_phandle(subsys_np, "qcom,cpu", 0);
		for_each_possible_cpu(cpu) {
			cpu_dev = get_cpu_device(cpu);
			if (cpu_dev && cpu_dev->of_node == dev_phandle) {
				cpu_hot_cdev->cpu_id = cpu;
				break;
			}
		}
		if (cpu_hot_cdev->cpu_id == -1) {
			dev_err(&pdev->dev, "Invalid CPU phandle\n");
			continue;
		}
		ret = of_property_read_string(subsys_np,
				"qcom,cdev-alias", &alias);
		if (ret)
			snprintf(cpu_hot_cdev->cdev_name, THERMAL_NAME_LENGTH, "cpu-hotplug%d",
					cpu_hot_cdev->cpu_id);
		else
			strscpy(cpu_hot_cdev->cdev_name, alias,
					THERMAL_NAME_LENGTH);
		INIT_WORK(&cpu_hot_cdev->reg_work,
				cpu_hot_register_cdev);
		INIT_WORK(&cpu_hot_cdev->exec_work,
				cpu_hot_execute_cdev);
		list_add(&cpu_hot_cdev->node, &cpu_hot_cdev_list);
	}

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "cpu-hotplug/cdev:online",
				cpu_hot_hp_online, NULL);
	if (ret < 0)
		return ret;
	cpu_hp_online = ret;

	return 0;
}

static int cpu_hot_remove(struct platform_device *pdev)
{
	struct cpu_hot_cdev *cpu_hot_cdev = NULL, *next = NULL;
	int ret = 0;

	if (cpu_hp_online) {
		cpuhp_remove_state_nocalls(cpu_hp_online);
		cpu_hp_online = 0;
	}

	mutex_lock(&cpu_hot_lock);
	list_for_each_entry_safe(cpu_hot_cdev, next, &cpu_hot_cdev_list, node) {
		if (!cpu_hot_cdev->cdev)
			goto loop_skip;
		thermal_cooling_device_unregister(cpu_hot_cdev->cdev);
		if (cpu_hot_cdev->cpu_hot_state) {
			cpu_hot_cdev->cpu_hot_state = false;
			ret = add_cpu(cpu_hot_cdev->cpu_id);
			if (ret)
				pr_err("CPU:%d online error:%d\n",
						cpu_hot_cdev->cpu_id, ret);
		}
loop_skip:
		list_del(&cpu_hot_cdev->node);
	}
	mutex_unlock(&cpu_hot_lock);

	return 0;
}

static const struct of_device_id cpu_hot_match[] = {
	{ .compatible = "qcom,cpu-hotplug", },
	{},
};

static struct platform_driver cpu_hot_driver = {
	.probe		= cpu_hot_probe,
	.remove         = cpu_hot_remove,
	.driver		= {
		.name = KBUILD_MODNAME,
		.of_match_table = cpu_hot_match,
	},
};
module_platform_driver(cpu_hot_driver);
MODULE_DESCRIPTION("CPU Hotplug cooling device driver");
MODULE_LICENSE("GPL");
