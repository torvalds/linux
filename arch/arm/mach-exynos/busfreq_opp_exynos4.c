/* linux/arch/arm/mach-exynos/busfreq_opp_exynos4.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS4 - BUS clock frequency scaling support with OPP
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/ktime.h>
#include <linux/tick.h>
#include <linux/kernel_stat.h>
#include <linux/suspend.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/opp.h>
#include <linux/clk.h>
#include <linux/workqueue.h>
#include <linux/pm_qos.h>

#include <asm/mach-types.h>

#include <mach/ppmu.h>
#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/gpio.h>
#include <mach/regs-mem.h>
#include <mach/cpufreq.h>
#include <mach/busfreq_exynos4.h>
#include <mach/smc.h>

#include <plat/map-s5p.h>
#include <plat/cpu.h>
#include <plat/clock.h>

static DEFINE_MUTEX(busfreq_lock);

struct busfreq_control {
	struct opp *opp_lock;
	struct device *dev;
	struct busfreq_data *data;
	bool init_done;
};

static struct busfreq_control bus_ctrl;

static struct opp __maybe_unused *step_up(struct busfreq_data *data, int step)
{
	int i;
	struct opp *opp = data->curr_opp;
	unsigned long newfreq;

	if (data->max_opp == data->curr_opp)
		return data->curr_opp;

	for (i = 0; i < step; i++) {
		newfreq = opp_get_freq(opp) + 1;
		opp = opp_find_freq_ceil(data->dev, &newfreq);

		if (opp == data->max_opp)
			break;
	}

	return opp;
}

struct opp *step_down(struct busfreq_data *data, int step)
{
	int i;
	struct opp *opp = data->curr_opp;
	unsigned long newfreq;

	if (data->min_opp == data->curr_opp)
		return data->curr_opp;

	for (i = 0; i < step; i++) {
		newfreq = opp_get_freq(opp) - 1;
		opp = opp_find_freq_floor(data->dev, &newfreq);

		if (opp == data->min_opp)
			break;
	}

	return opp;
}

static unsigned int _target(struct busfreq_data *data, struct opp *new)
{
	unsigned int index;
	unsigned int voltage;
	unsigned long newfreq;
	unsigned long currfreq;

	newfreq = opp_get_freq(new);
	currfreq = opp_get_freq(data->curr_opp);

	index = data->get_table_index(new);

	if (newfreq == 0 || newfreq == currfreq || data->use == false)
		return data->get_table_index(data->curr_opp);

	voltage = opp_get_voltage(new);
	if (newfreq > currfreq) {
		regulator_set_voltage(data->vdd_mif, voltage,
				voltage + 25000);
		voltage = data->get_int_volt(index);
		regulator_set_voltage(data->vdd_int, voltage,
				voltage + 25000);
		if (data->busfreq_prepare)
			data->busfreq_prepare(index);
	}
	data->target(index);

	if (newfreq < currfreq) {
		if (data->busfreq_post)
			data->busfreq_post(index);
		regulator_set_voltage(data->vdd_mif, voltage,
				voltage + 25000);
		voltage = data->get_int_volt(index);
		regulator_set_voltage(data->vdd_int, voltage,
				voltage + 25000);
	}
	data->curr_opp = new;

	return index;
}

static void exynos_busfreq_timer(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct busfreq_data *data = container_of(delayed_work, struct busfreq_data,
			worker);
	struct opp *opp;
	unsigned int index;

	opp = data->monitor(data);

	ppmu_start(data->dev);

	mutex_lock(&busfreq_lock);

	if (bus_ctrl.opp_lock)
		opp = bus_ctrl.opp_lock;

	index = _target(data, opp);

	mutex_unlock(&busfreq_lock);
	queue_delayed_work(system_freezable_wq, &data->worker, data->sampling_rate);
}

static int exynos_buspm_notifier_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	struct busfreq_data *data = container_of(this, struct busfreq_data,
			exynos_buspm_notifier);

	switch (event) {
	case PM_SUSPEND_PREPARE:
		mutex_lock(&busfreq_lock);
		_target(data, data->max_opp);
		mutex_unlock(&busfreq_lock);
		data->use = false;
		return NOTIFY_OK;
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		data->use = true;
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

static int exynos_busfreq_reboot_event(struct notifier_block *this,
		unsigned long code, void *unused)
{
	struct busfreq_data *data = container_of(this, struct busfreq_data,
			exynos_reboot_notifier);

	unsigned long voltage = opp_get_voltage(data->max_opp);
	unsigned int index = data->get_table_index(data->max_opp);

	regulator_set_voltage(data->vdd_mif, voltage, voltage + 25000);
	voltage = data->get_int_volt(index);
	regulator_set_voltage(data->vdd_int, voltage, voltage + 25000);
	data->use = false;

	printk(KERN_INFO "REBOOT Notifier for BUSFREQ\n");
	return NOTIFY_DONE;
}

void exynos_request_apply(unsigned long freq)
{
	struct opp *opp;
	unsigned int index;

	mutex_lock(&busfreq_lock);

	if (!bus_ctrl.init_done)
		goto out;

	opp = bus_ctrl.data->curr_opp;

	opp = opp_find_freq_ceil(bus_ctrl.data->dev, &freq);

	if (bus_ctrl.opp_lock)
		opp = bus_ctrl.opp_lock;

	if (opp_get_freq(bus_ctrl.data->curr_opp) >= opp_get_freq(opp))
		goto out;

	index = _target(bus_ctrl.data, opp);
out:
	mutex_unlock(&busfreq_lock);
}

static int exynos_qos_handler(struct notifier_block *b, unsigned long val, void *v)
{
	exynos_request_apply(val);

	return NOTIFY_OK;
}

static struct notifier_block exynos_qos_notifier = {
	.notifier_call = exynos_qos_handler,
};

static __devinit int exynos_busfreq_probe(struct platform_device *pdev)
{
	struct busfreq_data *data;
	unsigned int val = 0;
	bool pop = true;

#ifdef CONFIG_ARM_TRUSTZONE
	exynos_smc_readsfr(EXYNOS4_PA_DMC0_4X12 + 0x4, &val);
#else
	val = __raw_readl(S5P_VA_DMC0 + 0x4);
#endif
	val = (val >> 8) & 0xf;

	/* Check Memory Type Only support -> 0x5: 0xLPDDR2 */
	if (val != 0x05) {
		pr_err("[ %x ] Memory Type Undertermined.\n", val);
		pop = false;
	}

	data = kzalloc(sizeof(struct busfreq_data), GFP_KERNEL);
	if (!data) {
		pr_err("Unable to create busfreq_data struct.\n");
		return -ENOMEM;
	}

	data->exynos_buspm_notifier.notifier_call =
		exynos_buspm_notifier_event;
	data->exynos_reboot_notifier.notifier_call =
		exynos_busfreq_reboot_event;

	if (soc_is_exynos4212() || soc_is_exynos4412()) {
		data->init = exynos4x12_init;
		data->target = exynos4x12_target;
		data->get_int_volt = exynos4x12_get_int_volt;
		data->get_table_index = exynos4x12_get_table_index;
		data->monitor = exynos4x12_monitor;
		data->busfreq_prepare = exynos4x12_prepare;
		data->busfreq_post = exynos4x12_post;
		data->busfreq_suspend = exynos4x12_suspend;
		data->busfreq_resume = exynos4x12_resume;
	} else {
		pr_err("Unsupport device type.\n");
		goto err_busfreq;
	}

	data->dev = &pdev->dev;
	data->sampling_rate = usecs_to_jiffies(100000);
	bus_ctrl.opp_lock =  NULL;
	bus_ctrl.dev =  data->dev;
	bus_ctrl.data =  data;

	INIT_DELAYED_WORK(&data->worker, exynos_busfreq_timer);

	if (data->init(&pdev->dev, data, pop)) {
		pr_err("Failed to init busfreq.\n");
		goto err_busfreq;
	}

	data->time_in_state = kzalloc(sizeof(cputime64_t) * data->table_size, GFP_KERNEL);
	if (!data->time_in_state) {
		pr_err("Unable to create time_in_state.\n");
		goto err_busfreq;
	}

	data->last_time = get_jiffies_64();

	if (register_pm_notifier(&data->exynos_buspm_notifier)) {
		pr_err("Failed to setup buspm notifier\n");
		goto err_pm_notifier;
	}

	data->use = true;
	bus_ctrl.init_done = true;

	if (register_reboot_notifier(&data->exynos_reboot_notifier))
		pr_err("Failed to setup reboot notifier\n");

	platform_set_drvdata(pdev, data);
	pm_qos_add_notifier(PM_QOS_BUS_THROUGHPUT, &exynos_qos_notifier);

	queue_delayed_work(system_freezable_wq, &data->worker, 10 * data->sampling_rate);
	return 0;

err_pm_notifier:
	kfree(data->time_in_state);

err_busfreq:
	if (!IS_ERR(data->vdd_int))
		regulator_put(data->vdd_int);

	if (!IS_ERR(data->vdd_mif))
		regulator_put(data->vdd_mif);

	kfree(data);
	return -ENODEV;
}

static __devexit int exynos_busfreq_remove(struct platform_device *pdev)
{
	struct busfreq_data *data = platform_get_drvdata(pdev);

	unregister_pm_notifier(&data->exynos_buspm_notifier);
	unregister_reboot_notifier(&data->exynos_reboot_notifier);
	regulator_put(data->vdd_int);
	regulator_put(data->vdd_mif);
	sysfs_remove_group(data->busfreq_kobject, &data->busfreq_attr_group);
	kfree(data->time_in_state);
	kfree(data);

	return 0;
}

static int exynos_busfreq_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct busfreq_data *data = (struct busfreq_data *)platform_get_drvdata(pdev);

	if (data->busfreq_suspend)
		data->busfreq_suspend();
	return 0;
}

static int exynos_busfreq_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct busfreq_data *data = (struct busfreq_data *)platform_get_drvdata(pdev);
	ppmu_reset(dev);

	if (data->busfreq_resume)
		data->busfreq_resume();
	return 0;
}

static const struct dev_pm_ops exynos_busfreq_pm = {
	.suspend = exynos_busfreq_suspend,
	.resume = exynos_busfreq_resume,
};

static struct platform_driver exynos_busfreq_driver = {
	.probe  = exynos_busfreq_probe,
	.remove = __devexit_p(exynos_busfreq_remove),
	.driver = {
		.name   = "exynos-busfreq",
		.owner  = THIS_MODULE,
		.pm     = &exynos_busfreq_pm,
	},
};

static int __init exynos_busfreq_init(void)
{
	return platform_driver_register(&exynos_busfreq_driver);
}
late_initcall(exynos_busfreq_init);

static void __exit exynos_busfreq_exit(void)
{
	platform_driver_unregister(&exynos_busfreq_driver);
}
module_exit(exynos_busfreq_exit);
