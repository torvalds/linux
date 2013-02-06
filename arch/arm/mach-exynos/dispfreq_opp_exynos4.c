/* linux/arch/arm/mach-exynos/dispfreq_opp_exynos4.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS4 - Display frequency scaling support with OPP
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* This feature was derived from exynos4 display of devfreq
 * which was made by Mr Myungjoo Ham.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/opp.h>
#include <linux/mutex.h>
#include <linux/suspend.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/pm_qos_params.h>
#include <linux/devfreq/exynos4_display.h>


#include <linux/list.h>
#include <linux/pm_qos_params.h>
#include <mach/cpufreq.h>
#include <mach/dev.h>
#include <linux/device.h>

#define DEVFREQ_OPTION_FREQ_LUB	0x0
#define DEVFREQ_OPTION_FREQ_GLB 0x1

#define EXYNOS4_DISPLAY_ON	1
#define EXYNOS4_DISPLAY_OFF	0

#define DEFAULT_DELAY_TIME	1000	/* us (millisecond) */

enum exynos_display_type {
	TYPE_DISPLAY_EXYNOS4210,
	TYPE_DISPLAY_EXYNOS4x12,
};

/* Define opp table which include various frequency level */
struct exynos4_dispfreq_opp_table {
	unsigned int idx;
	unsigned long clk;
	unsigned long volt;
};

struct exynos4_dispfreq_pm_qos_table {
	unsigned long freq; /* 0 if this is the last element */
	s32 qos_value;
};

struct exynos4_dispfreq_data {
	struct device *dev;
	struct opp *curr_opp;
	struct delayed_work wq_lowfreq;
	struct notifier_block nb;
	struct notifier_block nb_pm;
	struct notifier_block nb_qos;

	unsigned long initial_freq;
	int qos_type;
	struct exynos4_dispfreq_pm_qos_table *qos_list;

	unsigned long previous_freq;
	unsigned long min_freq;
	unsigned long max_freq;
	unsigned long qos_min_freq;

	enum exynos_display_type type;
	unsigned int state;
	struct mutex lock;
};

/* Define frequency level */
enum exynos4_dispfreq_clk_level_idx {
	LV_0 = 0,
	LV_1,
	_LV_END
};

static struct exynos4_dispfreq_opp_table exynos_dispfreq_clk_table[] = {
	{LV_0, 40, 0 },
	{LV_1, 60, 0 },
	{0, 0, 0 },
};

static struct pm_qos_request_list qos_wrapper[DVFS_LOCK_ID_END];

/* Wrappers for obsolete legacy kernel hack (busfreq_lock/lock_free) */
int exynos4_busfreq_lock(unsigned int nId, enum busfreq_level_request lvl)
{
	s32 qos_value;

	if (WARN(nId >= DVFS_LOCK_ID_END, "incorrect nId."))
		return -EINVAL;
	if (WARN(lvl >= BUS_LEVEL_END, "incorrect level."))
		return -EINVAL;

	switch (lvl) {
	case BUS_L0:
		qos_value = 400000;
		break;
	case BUS_L1:
		qos_value = 267000;
		break;
	case BUS_L2:
		qos_value = 133000;
		break;
	default:
		qos_value = 0;
	}

	if (qos_wrapper[nId].pm_qos_class == 0) {
		pm_qos_add_request(&qos_wrapper[nId],
				   PM_QOS_BUS_QOS, qos_value);
	} else {
		pm_qos_update_request(&qos_wrapper[nId], qos_value);
	}

	return 0;
}

void exynos4_busfreq_lock_free(unsigned int nId)
{
	if (WARN(nId >= DVFS_LOCK_ID_END, "incorrect nId."))
		return;

	if (qos_wrapper[nId].pm_qos_class)
		pm_qos_update_request(&qos_wrapper[nId],
				      PM_QOS_BUS_DMA_THROUGHPUT_DEFAULT_VALUE);
}

/*
 * The exynos-display driver send newly frequency to display client
 * if it receive event from sender device.
 *	List of display client device
 *	- FIMD and so on
 */
static BLOCKING_NOTIFIER_HEAD(exynos4_display_notifier_client_list);


int exynos4_display_register_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(
			&exynos4_display_notifier_client_list, nb);
}
EXPORT_SYMBOL(exynos4_display_register_client);

int exynos4_display_unregister_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(
			&exynos4_display_notifier_client_list, nb);
}
EXPORT_SYMBOL(exynos4_display_unregister_client);

static int devfreq_powersave_func(struct exynos4_dispfreq_data *data,
				  unsigned long *freq)
{
	*freq = data->min_freq;
	return 0;
}


static int exynos4_dispfreq_profile_target(struct device *dev,
					unsigned long *_freq, u32 options);

/**
 * update_devfreq() - Reevaluate the device and configure frequency.
 * @devfreq:	the devfreq instance.
 *
 * Note: Lock devfreq->lock before calling update_devfreq
 *	 This function is exported for governors.
 */
static int exynos4_dispfreq_update(struct exynos4_dispfreq_data *data)
{
	unsigned long freq;
	int err = 0;
	u32 options = 0;

	if (!mutex_is_locked(&data->lock)) {
		WARN(true, "devfreq->lock must be locked by the caller.\n");
		return -EINVAL;
	}

	/* Reevaluate the proper frequency */
	err = devfreq_powersave_func(data, &freq);
	if (err)
		return err;

	/*
	 * Adjust the freuqency with user freq and QoS.
	 *
	 * List from the highest proiority
	 * min_freq
	 * max_freq
	 * qos_min_freq
	 */

	if (data->qos_min_freq && freq < data->qos_min_freq) {
		freq = data->qos_min_freq;
		options &= ~(1 << 0);
		options |= DEVFREQ_OPTION_FREQ_LUB;
	}
	if (data->max_freq && freq > data->max_freq) {
		freq = data->max_freq;
		options &= ~(1 << 0);
		options |= DEVFREQ_OPTION_FREQ_GLB;
	}
	if (data->min_freq && freq < data->min_freq) {
		freq = data->min_freq;
		options &= ~(1 << 0);
		options |= DEVFREQ_OPTION_FREQ_LUB;
	}

	err = exynos4_dispfreq_profile_target(data->dev, &freq, options);
	if (err)
		return err;

	data->previous_freq = freq;
	return err;
}

static int exynos4_dispfreq_send_event_to_display(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(
			&exynos4_display_notifier_client_list, val, v);
}

static int exynos4_dispfreq_opp_notifier_call(struct notifier_block *nb,
	unsigned long val, void *devp)
{
	struct exynos4_dispfreq_data *data = container_of(nb,
			struct exynos4_dispfreq_data, nb);
	int ret;

	mutex_lock(&data->lock);
	ret = exynos4_dispfreq_update(data);
	mutex_unlock(&data->lock);

	return ret;
}

struct opp *devfreq_recommended_opp(struct device *dev, unsigned long *freq,
				    bool floor)
{
	struct opp *opp;

	if (floor) {
		opp = opp_find_freq_floor(dev, freq);

		if (opp == ERR_PTR(-ENODEV))
			opp = opp_find_freq_ceil(dev, freq);
	} else {
		opp = opp_find_freq_ceil(dev, freq);

		if (opp == ERR_PTR(-ENODEV))
			opp = opp_find_freq_floor(dev, freq);
	}

	return opp;
}


static int exynos4_dispfreq_profile_target(struct device *dev,
					unsigned long *_freq, u32 options)
{
	/* Inform display client of new frequency */
	struct exynos4_dispfreq_data *data = dev_get_drvdata(dev);
	struct opp *opp = devfreq_recommended_opp(dev, _freq, options &
						  DEVFREQ_OPTION_FREQ_GLB);
	unsigned long old_freq = opp_get_freq(data->curr_opp);
	unsigned long new_freq = opp_get_freq(opp);

	/* TODO: No longer use fb notifier to identify LCD on/off state and
	   have yet alternative feature of it. So, exynos4-display change
	   refresh rate of display clinet irrespective of LCD state until
	   proper feature will be implemented. */
	if (old_freq == new_freq)
		return 0;

	opp = opp_find_freq_floor(dev, &new_freq);
	data->curr_opp = opp;

	switch (new_freq) {
	case EXYNOS4_DISPLAY_LV_HF:
		if (delayed_work_pending(&data->wq_lowfreq))
			cancel_delayed_work(&data->wq_lowfreq);

		exynos4_dispfreq_send_event_to_display(
				EXYNOS4_DISPLAY_LV_HF, NULL);
		break;
	case EXYNOS4_DISPLAY_LV_LF:
		schedule_delayed_work(&data->wq_lowfreq,
				msecs_to_jiffies(DEFAULT_DELAY_TIME));
		break;
	}

	return 0;
}

static int exynos4_dispfreq_qos_notifier_call(struct notifier_block *nb,
				     unsigned long value, void *devp)
{
	struct exynos4_dispfreq_data *data = container_of(nb,
			struct exynos4_dispfreq_data, nb_qos);
	int ret;
	int i;
	unsigned long default_value = 0;
	struct exynos4_dispfreq_pm_qos_table *qos_list = data->qos_list;
	bool qos_use_max = true;

	if (!qos_list)
		return NOTIFY_DONE;

	mutex_lock(&data->lock);

	switch (data->qos_type) {
	case PM_QOS_CPU_DMA_LATENCY:
		default_value = PM_QOS_CPU_DMA_LAT_DEFAULT_VALUE;
		break;
	case PM_QOS_NETWORK_LATENCY:
		default_value = PM_QOS_NETWORK_LAT_DEFAULT_VALUE;
		break;
	case PM_QOS_NETWORK_THROUGHPUT:
		default_value = PM_QOS_NETWORK_THROUGHPUT_DEFAULT_VALUE;
		break;
	case PM_QOS_BUS_DMA_THROUGHPUT:
		default_value = PM_QOS_BUS_DMA_THROUGHPUT_DEFAULT_VALUE;
		break;
	case PM_QOS_DISPLAY_FREQUENCY:
		default_value = PM_QOS_DISPLAY_FREQUENCY_DEFAULT_VALUE;
		break;
	default:
		/* Won't do any check to detect "default" state */
		break;
	}

	if (value == default_value) {
		data->qos_min_freq = 0;
		goto update;
	}

	for (i = 0; qos_list[i].freq; i++) {
		/* QoS Met */
		if ((qos_use_max && qos_list[i].qos_value >= value) ||
		    (!qos_use_max && qos_list[i].qos_value <= value)) {
			data->qos_min_freq = qos_list[i].freq;
			goto update;
		}
	}

	/* Use the highest QoS freq */
	if (i > 0)
		data->qos_min_freq = qos_list[i - 1].freq;

update:
	ret = exynos4_dispfreq_update(data);
	mutex_unlock(&data->lock);
	return ret;
}

/*
 * Register exynos-display as client to pm notifer
 * - This callback gets called when something important happens in pm state.
 */
static int exynos4_dispfreq_pm_notifier_callback(struct notifier_block *this,
				 unsigned long event, void *_data)
{
	struct exynos4_dispfreq_data *data = container_of(this,
			struct exynos4_dispfreq_data, nb_pm);

	if (data->state == EXYNOS4_DISPLAY_OFF)
		return NOTIFY_OK;

	switch (event) {
	case PM_SUSPEND_PREPARE:
		mutex_lock(&data->lock);
		data->state = EXYNOS4_DISPLAY_OFF;
		mutex_unlock(&data->lock);

		if (delayed_work_pending(&data->wq_lowfreq))
			cancel_delayed_work(&data->wq_lowfreq);

		return NOTIFY_OK;

	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		mutex_lock(&data->lock);
		data->state = EXYNOS4_DISPLAY_ON;
		mutex_unlock(&data->lock);

		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

/*
 * Enable/disable exynos-display operation
 */
static void exynos4_dispfreq_disable(struct exynos4_dispfreq_data *data)
{
	struct opp *opp;
	unsigned long freq = EXYNOS4_DISPLAY_LV_DEFAULT;

	/* Cancel workqueue which set low frequency of display client
	 * if it is pending state before executing workqueue. */
	if (delayed_work_pending(&data->wq_lowfreq))
		cancel_delayed_work(&data->wq_lowfreq);

	/* Set high frequency(default) of display client */
	exynos4_dispfreq_send_event_to_display(freq, NULL);

	mutex_lock(&data->lock);
	data->state = EXYNOS4_DISPLAY_OFF;
	mutex_unlock(&data->lock);

	/* Find opp object with high frequency */
	opp = opp_find_freq_floor(data->dev, &freq);
	if (IS_ERR(opp)) {
		dev_err(data->dev,
			"invalid initial frequency %lu kHz.\n", freq);
	} else
		data->curr_opp = opp;
}

static void exynos4_display_enable(struct exynos4_dispfreq_data *data)
{
	data->state = EXYNOS4_DISPLAY_ON;
}

/*
 * Timer to set display with low frequency state after 1 second
 */
static void exynos4_dispfreq_set_lowfreq(struct work_struct *work)
{
	exynos4_dispfreq_send_event_to_display(EXYNOS4_DISPLAY_LV_LF, NULL);
}

static __devinit int exynos4_dispfreq_probe(struct platform_device *pdev)
{
	struct exynos4_dispfreq_data *data;
	struct device *dev = &pdev->dev;
	struct opp *opp;
	int ret = 0;
	int i;
	struct exynos4_dispfreq_pm_qos_table *qos_list;
	struct srcu_notifier_head *nh;

	data = kzalloc(sizeof(struct exynos4_dispfreq_data), GFP_KERNEL);
	if (!data) {
		dev_err(dev, "cannot allocate memory.\n");
		return -ENOMEM;
	}
	data->dev = dev;
	data->state = EXYNOS4_DISPLAY_ON;
	data->initial_freq = EXYNOS4_DISPLAY_LV_DEFAULT;
	mutex_init(&data->lock);

	/* Register OPP entries */
	for (i = 0 ; i < _LV_END ; i++) {
		ret = opp_add(dev, exynos_dispfreq_clk_table[i].clk,
			      exynos_dispfreq_clk_table[i].volt);
		if (ret) {
			dev_err(dev, "cannot add opp entries.\n");
			goto err_alloc_mem;
		}
	}


	/* Find opp object with init frequency */
	opp = opp_find_freq_floor(dev, &data->initial_freq);
	if (IS_ERR(opp)) {
		dev_err(dev, "invalid initial frequency %lu kHz.\n",
		       data->initial_freq);
		ret = PTR_ERR(opp);
		goto err_alloc_mem;
	}
	data->curr_opp = opp;

	/* Initialize QoS */
	qos_list = kzalloc(
		(sizeof(struct exynos4_dispfreq_pm_qos_table) * _LV_END),
			GFP_KERNEL);
	if (!data) {
		dev_err(dev, "cannot allocate memory.\n");
		goto err_alloc_mem;
	}
	for (i = 0 ; i < _LV_END ; i++) {
		qos_list[i].freq = exynos_dispfreq_clk_table[i].clk;
		qos_list[i].qos_value = exynos_dispfreq_clk_table[i].clk;
	}

	/* Register exynos4_display as client to opp notifier */
	memset(&data->nb, 0, sizeof(data->nb));
	data->nb.notifier_call = exynos4_dispfreq_opp_notifier_call;
	nh = opp_get_notifier(dev);
	ret = srcu_notifier_chain_register(nh, &data->nb);
	if (ret < 0) {
		dev_err(dev, "failed to get pm notifier: %d\n", ret);
		goto err_reg_pm_opp;
	}

	data->qos_type = PM_QOS_DISPLAY_FREQUENCY;
	data->qos_list = qos_list;

	/* Register exynos4_display as client to pm qos notifier */
	memset(&data->nb_qos, 0, sizeof(data->nb_qos));
	data->nb_qos.notifier_call = exynos4_dispfreq_qos_notifier_call;
	ret = pm_qos_add_notifier(data->qos_type, &data->nb_qos);
	if (ret < 0) {
		dev_err(dev, "failed to get pm notifier: %d\n", ret);
		goto err_reg_pm_qos;
	}

	/* Register exynos4_display as client to pm notifier */
	memset(&data->nb_pm, 0, sizeof(data->nb_pm));
	data->nb_pm.notifier_call = exynos4_dispfreq_pm_notifier_callback;
	ret = register_pm_notifier(&data->nb_pm);
	if (ret < 0) {
		dev_err(dev, "failed to get pm notifier: %d\n", ret);
		goto err_reg_pm;
	}

	INIT_DELAYED_WORK(&data->wq_lowfreq, exynos4_dispfreq_set_lowfreq);

	platform_set_drvdata(pdev, data);

	return 0;

err_reg_pm:
	pm_qos_remove_notifier(data->qos_type, &data->nb_qos);

err_reg_pm_qos:
	srcu_notifier_chain_unregister(nh, &data->nb);

err_reg_pm_opp:
	kfree(data->qos_list);

err_alloc_mem:
	kfree(data);

	return ret;
}

static __devexit int exynos4_dispfreq_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exynos4_dispfreq_data *data = pdev->dev.platform_data;
	struct srcu_notifier_head *nh = opp_get_notifier(dev);

	unregister_pm_notifier(&data->nb_pm);
	exynos4_dispfreq_disable(data);

	pm_qos_remove_notifier(data->qos_type, &data->nb_qos);
	srcu_notifier_chain_unregister(nh, &data->nb);

	kfree(data->qos_list);
	kfree(data);

	return 0;
}

static int exynos4_dispfreq_suspend(struct device *dev)
{
	/* TODO */
	return 0;
}

static int exynos4_dispfreq_resume(struct device *dev)
{
	/* TODO */
	return 0;
}

static const struct dev_pm_ops exynos4_dispfreq_pm = {
	.suspend = exynos4_dispfreq_suspend,
	.resume = exynos4_dispfreq_resume,
};

static struct platform_driver exynos4_dispfreq_driver = {
	.probe  = exynos4_dispfreq_probe,
	.remove = __devexit_p(exynos4_dispfreq_remove),
	.driver = {
		.name   = "exynos4-dispfreq",
		.owner  = THIS_MODULE,
		.pm     = &exynos4_dispfreq_pm,
	},
};

static int __init exynos4_dispfreq_init(void)
{
	return platform_driver_register(&exynos4_dispfreq_driver);
}
late_initcall(exynos4_dispfreq_init);

static void __exit exynos4_dispfreq_exit(void)
{
	platform_driver_unregister(&exynos4_dispfreq_driver);
}
module_exit(exynos4_dispfreq_exit);
