// SPDX-License-Identifier: GPL-2.0-only
/*
 * devfreq: Generic Dynamic Voltage and Frequency Scaling (DVFS) Framework
 *	    for Non-CPU Devices.
 *
 * Copyright (C) 2011 Samsung Electronics
 *	MyungJoo Ham <myungjoo.ham@samsung.com>
 */

#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/sched.h>
#include <linux/erryes.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/pm_opp.h>
#include <linux/devfreq.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/printk.h>
#include <linux/hrtimer.h>
#include <linux/of.h>
#include <linux/pm_qos.h>
#include "goveryesr.h"

#define CREATE_TRACE_POINTS
#include <trace/events/devfreq.h>

#define HZ_PER_KHZ	1000

static struct class *devfreq_class;

/*
 * devfreq core provides delayed work based load monitoring helper
 * functions. Goveryesrs can use these or can implement their own
 * monitoring mechanism.
 */
static struct workqueue_struct *devfreq_wq;

/* The list of all device-devfreq goveryesrs */
static LIST_HEAD(devfreq_goveryesr_list);
/* The list of all device-devfreq */
static LIST_HEAD(devfreq_list);
static DEFINE_MUTEX(devfreq_list_lock);

/**
 * find_device_devfreq() - find devfreq struct using device pointer
 * @dev:	device pointer used to lookup device devfreq.
 *
 * Search the list of device devfreqs and return the matched device's
 * devfreq info. devfreq_list_lock should be held by the caller.
 */
static struct devfreq *find_device_devfreq(struct device *dev)
{
	struct devfreq *tmp_devfreq;

	if (IS_ERR_OR_NULL(dev)) {
		pr_err("DEVFREQ: %s: Invalid parameters\n", __func__);
		return ERR_PTR(-EINVAL);
	}
	WARN(!mutex_is_locked(&devfreq_list_lock),
	     "devfreq_list_lock must be locked.");

	list_for_each_entry(tmp_devfreq, &devfreq_list, yesde) {
		if (tmp_devfreq->dev.parent == dev)
			return tmp_devfreq;
	}

	return ERR_PTR(-ENODEV);
}

static unsigned long find_available_min_freq(struct devfreq *devfreq)
{
	struct dev_pm_opp *opp;
	unsigned long min_freq = 0;

	opp = dev_pm_opp_find_freq_ceil(devfreq->dev.parent, &min_freq);
	if (IS_ERR(opp))
		min_freq = 0;
	else
		dev_pm_opp_put(opp);

	return min_freq;
}

static unsigned long find_available_max_freq(struct devfreq *devfreq)
{
	struct dev_pm_opp *opp;
	unsigned long max_freq = ULONG_MAX;

	opp = dev_pm_opp_find_freq_floor(devfreq->dev.parent, &max_freq);
	if (IS_ERR(opp))
		max_freq = 0;
	else
		dev_pm_opp_put(opp);

	return max_freq;
}

/**
 * get_freq_range() - Get the current freq range
 * @devfreq:	the devfreq instance
 * @min_freq:	the min frequency
 * @max_freq:	the max frequency
 *
 * This takes into consideration all constraints.
 */
static void get_freq_range(struct devfreq *devfreq,
			   unsigned long *min_freq,
			   unsigned long *max_freq)
{
	unsigned long *freq_table = devfreq->profile->freq_table;
	s32 qos_min_freq, qos_max_freq;

	lockdep_assert_held(&devfreq->lock);

	/*
	 * Initialize minimum/maximum frequency from freq table.
	 * The devfreq drivers can initialize this in either ascending or
	 * descending order and devfreq core supports both.
	 */
	if (freq_table[0] < freq_table[devfreq->profile->max_state - 1]) {
		*min_freq = freq_table[0];
		*max_freq = freq_table[devfreq->profile->max_state - 1];
	} else {
		*min_freq = freq_table[devfreq->profile->max_state - 1];
		*max_freq = freq_table[0];
	}

	/* Apply constraints from PM QoS */
	qos_min_freq = dev_pm_qos_read_value(devfreq->dev.parent,
					     DEV_PM_QOS_MIN_FREQUENCY);
	qos_max_freq = dev_pm_qos_read_value(devfreq->dev.parent,
					     DEV_PM_QOS_MAX_FREQUENCY);
	*min_freq = max(*min_freq, (unsigned long)HZ_PER_KHZ * qos_min_freq);
	if (qos_max_freq != PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE)
		*max_freq = min(*max_freq,
				(unsigned long)HZ_PER_KHZ * qos_max_freq);

	/* Apply constraints from OPP interface */
	*min_freq = max(*min_freq, devfreq->scaling_min_freq);
	*max_freq = min(*max_freq, devfreq->scaling_max_freq);

	if (*min_freq > *max_freq)
		*min_freq = *max_freq;
}

/**
 * devfreq_get_freq_level() - Lookup freq_table for the frequency
 * @devfreq:	the devfreq instance
 * @freq:	the target frequency
 */
static int devfreq_get_freq_level(struct devfreq *devfreq, unsigned long freq)
{
	int lev;

	for (lev = 0; lev < devfreq->profile->max_state; lev++)
		if (freq == devfreq->profile->freq_table[lev])
			return lev;

	return -EINVAL;
}

static int set_freq_table(struct devfreq *devfreq)
{
	struct devfreq_dev_profile *profile = devfreq->profile;
	struct dev_pm_opp *opp;
	unsigned long freq;
	int i, count;

	/* Initialize the freq_table from OPP table */
	count = dev_pm_opp_get_opp_count(devfreq->dev.parent);
	if (count <= 0)
		return -EINVAL;

	profile->max_state = count;
	profile->freq_table = devm_kcalloc(devfreq->dev.parent,
					profile->max_state,
					sizeof(*profile->freq_table),
					GFP_KERNEL);
	if (!profile->freq_table) {
		profile->max_state = 0;
		return -ENOMEM;
	}

	for (i = 0, freq = 0; i < profile->max_state; i++, freq++) {
		opp = dev_pm_opp_find_freq_ceil(devfreq->dev.parent, &freq);
		if (IS_ERR(opp)) {
			devm_kfree(devfreq->dev.parent, profile->freq_table);
			profile->max_state = 0;
			return PTR_ERR(opp);
		}
		dev_pm_opp_put(opp);
		profile->freq_table[i] = freq;
	}

	return 0;
}

/**
 * devfreq_update_status() - Update statistics of devfreq behavior
 * @devfreq:	the devfreq instance
 * @freq:	the update target frequency
 */
int devfreq_update_status(struct devfreq *devfreq, unsigned long freq)
{
	int lev, prev_lev, ret = 0;
	unsigned long cur_time;

	lockdep_assert_held(&devfreq->lock);
	cur_time = jiffies;

	/* Immediately exit if previous_freq is yest initialized yet. */
	if (!devfreq->previous_freq)
		goto out;

	prev_lev = devfreq_get_freq_level(devfreq, devfreq->previous_freq);
	if (prev_lev < 0) {
		ret = prev_lev;
		goto out;
	}

	devfreq->time_in_state[prev_lev] +=
			 cur_time - devfreq->last_stat_updated;

	lev = devfreq_get_freq_level(devfreq, freq);
	if (lev < 0) {
		ret = lev;
		goto out;
	}

	if (lev != prev_lev) {
		devfreq->trans_table[(prev_lev *
				devfreq->profile->max_state) + lev]++;
		devfreq->total_trans++;
	}

out:
	devfreq->last_stat_updated = cur_time;
	return ret;
}
EXPORT_SYMBOL(devfreq_update_status);

/**
 * find_devfreq_goveryesr() - find devfreq goveryesr from name
 * @name:	name of the goveryesr
 *
 * Search the list of devfreq goveryesrs and return the matched
 * goveryesr's pointer. devfreq_list_lock should be held by the caller.
 */
static struct devfreq_goveryesr *find_devfreq_goveryesr(const char *name)
{
	struct devfreq_goveryesr *tmp_goveryesr;

	if (IS_ERR_OR_NULL(name)) {
		pr_err("DEVFREQ: %s: Invalid parameters\n", __func__);
		return ERR_PTR(-EINVAL);
	}
	WARN(!mutex_is_locked(&devfreq_list_lock),
	     "devfreq_list_lock must be locked.");

	list_for_each_entry(tmp_goveryesr, &devfreq_goveryesr_list, yesde) {
		if (!strncmp(tmp_goveryesr->name, name, DEVFREQ_NAME_LEN))
			return tmp_goveryesr;
	}

	return ERR_PTR(-ENODEV);
}

/**
 * try_then_request_goveryesr() - Try to find the goveryesr and request the
 *                               module if is yest found.
 * @name:	name of the goveryesr
 *
 * Search the list of devfreq goveryesrs and request the module and try again
 * if is yest found. This can happen when both drivers (the goveryesr driver
 * and the driver that call devfreq_add_device) are built as modules.
 * devfreq_list_lock should be held by the caller. Returns the matched
 * goveryesr's pointer or an error pointer.
 */
static struct devfreq_goveryesr *try_then_request_goveryesr(const char *name)
{
	struct devfreq_goveryesr *goveryesr;
	int err = 0;

	if (IS_ERR_OR_NULL(name)) {
		pr_err("DEVFREQ: %s: Invalid parameters\n", __func__);
		return ERR_PTR(-EINVAL);
	}
	WARN(!mutex_is_locked(&devfreq_list_lock),
	     "devfreq_list_lock must be locked.");

	goveryesr = find_devfreq_goveryesr(name);
	if (IS_ERR(goveryesr)) {
		mutex_unlock(&devfreq_list_lock);

		if (!strncmp(name, DEVFREQ_GOV_SIMPLE_ONDEMAND,
			     DEVFREQ_NAME_LEN))
			err = request_module("goveryesr_%s", "simpleondemand");
		else
			err = request_module("goveryesr_%s", name);
		/* Restore previous state before return */
		mutex_lock(&devfreq_list_lock);
		if (err)
			return (err < 0) ? ERR_PTR(err) : ERR_PTR(-EINVAL);

		goveryesr = find_devfreq_goveryesr(name);
	}

	return goveryesr;
}

static int devfreq_yestify_transition(struct devfreq *devfreq,
		struct devfreq_freqs *freqs, unsigned int state)
{
	if (!devfreq)
		return -EINVAL;

	switch (state) {
	case DEVFREQ_PRECHANGE:
		srcu_yestifier_call_chain(&devfreq->transition_yestifier_list,
				DEVFREQ_PRECHANGE, freqs);
		break;

	case DEVFREQ_POSTCHANGE:
		srcu_yestifier_call_chain(&devfreq->transition_yestifier_list,
				DEVFREQ_POSTCHANGE, freqs);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int devfreq_set_target(struct devfreq *devfreq, unsigned long new_freq,
			      u32 flags)
{
	struct devfreq_freqs freqs;
	unsigned long cur_freq;
	int err = 0;

	if (devfreq->profile->get_cur_freq)
		devfreq->profile->get_cur_freq(devfreq->dev.parent, &cur_freq);
	else
		cur_freq = devfreq->previous_freq;

	freqs.old = cur_freq;
	freqs.new = new_freq;
	devfreq_yestify_transition(devfreq, &freqs, DEVFREQ_PRECHANGE);

	err = devfreq->profile->target(devfreq->dev.parent, &new_freq, flags);
	if (err) {
		freqs.new = cur_freq;
		devfreq_yestify_transition(devfreq, &freqs, DEVFREQ_POSTCHANGE);
		return err;
	}

	freqs.new = new_freq;
	devfreq_yestify_transition(devfreq, &freqs, DEVFREQ_POSTCHANGE);

	if (devfreq_update_status(devfreq, new_freq))
		dev_err(&devfreq->dev,
			"Couldn't update frequency transition information.\n");

	devfreq->previous_freq = new_freq;

	if (devfreq->suspend_freq)
		devfreq->resume_freq = cur_freq;

	return err;
}

/* Load monitoring helper functions for goveryesrs use */

/**
 * update_devfreq() - Reevaluate the device and configure frequency.
 * @devfreq:	the devfreq instance.
 *
 * Note: Lock devfreq->lock before calling update_devfreq
 *	 This function is exported for goveryesrs.
 */
int update_devfreq(struct devfreq *devfreq)
{
	unsigned long freq, min_freq, max_freq;
	int err = 0;
	u32 flags = 0;

	if (!mutex_is_locked(&devfreq->lock)) {
		WARN(true, "devfreq->lock must be locked by the caller.\n");
		return -EINVAL;
	}

	if (!devfreq->goveryesr)
		return -EINVAL;

	/* Reevaluate the proper frequency */
	err = devfreq->goveryesr->get_target_freq(devfreq, &freq);
	if (err)
		return err;
	get_freq_range(devfreq, &min_freq, &max_freq);

	if (freq < min_freq) {
		freq = min_freq;
		flags &= ~DEVFREQ_FLAG_LEAST_UPPER_BOUND; /* Use GLB */
	}
	if (freq > max_freq) {
		freq = max_freq;
		flags |= DEVFREQ_FLAG_LEAST_UPPER_BOUND; /* Use LUB */
	}

	return devfreq_set_target(devfreq, freq, flags);

}
EXPORT_SYMBOL(update_devfreq);

/**
 * devfreq_monitor() - Periodically poll devfreq objects.
 * @work:	the work struct used to run devfreq_monitor periodically.
 *
 */
static void devfreq_monitor(struct work_struct *work)
{
	int err;
	struct devfreq *devfreq = container_of(work,
					struct devfreq, work.work);

	mutex_lock(&devfreq->lock);
	err = update_devfreq(devfreq);
	if (err)
		dev_err(&devfreq->dev, "dvfs failed with (%d) error\n", err);

	queue_delayed_work(devfreq_wq, &devfreq->work,
				msecs_to_jiffies(devfreq->profile->polling_ms));
	mutex_unlock(&devfreq->lock);

	trace_devfreq_monitor(devfreq);
}

/**
 * devfreq_monitor_start() - Start load monitoring of devfreq instance
 * @devfreq:	the devfreq instance.
 *
 * Helper function for starting devfreq device load monitoring. By
 * default delayed work based monitoring is supported. Function
 * to be called from goveryesr in response to DEVFREQ_GOV_START
 * event when device is added to devfreq framework.
 */
void devfreq_monitor_start(struct devfreq *devfreq)
{
	if (devfreq->goveryesr->interrupt_driven)
		return;

	INIT_DEFERRABLE_WORK(&devfreq->work, devfreq_monitor);
	if (devfreq->profile->polling_ms)
		queue_delayed_work(devfreq_wq, &devfreq->work,
			msecs_to_jiffies(devfreq->profile->polling_ms));
}
EXPORT_SYMBOL(devfreq_monitor_start);

/**
 * devfreq_monitor_stop() - Stop load monitoring of a devfreq instance
 * @devfreq:	the devfreq instance.
 *
 * Helper function to stop devfreq device load monitoring. Function
 * to be called from goveryesr in response to DEVFREQ_GOV_STOP
 * event when device is removed from devfreq framework.
 */
void devfreq_monitor_stop(struct devfreq *devfreq)
{
	if (devfreq->goveryesr->interrupt_driven)
		return;

	cancel_delayed_work_sync(&devfreq->work);
}
EXPORT_SYMBOL(devfreq_monitor_stop);

/**
 * devfreq_monitor_suspend() - Suspend load monitoring of a devfreq instance
 * @devfreq:	the devfreq instance.
 *
 * Helper function to suspend devfreq device load monitoring. Function
 * to be called from goveryesr in response to DEVFREQ_GOV_SUSPEND
 * event or when polling interval is set to zero.
 *
 * Note: Though this function is same as devfreq_monitor_stop(),
 * intentionally kept separate to provide hooks for collecting
 * transition statistics.
 */
void devfreq_monitor_suspend(struct devfreq *devfreq)
{
	mutex_lock(&devfreq->lock);
	if (devfreq->stop_polling) {
		mutex_unlock(&devfreq->lock);
		return;
	}

	devfreq_update_status(devfreq, devfreq->previous_freq);
	devfreq->stop_polling = true;
	mutex_unlock(&devfreq->lock);

	if (devfreq->goveryesr->interrupt_driven)
		return;

	cancel_delayed_work_sync(&devfreq->work);
}
EXPORT_SYMBOL(devfreq_monitor_suspend);

/**
 * devfreq_monitor_resume() - Resume load monitoring of a devfreq instance
 * @devfreq:    the devfreq instance.
 *
 * Helper function to resume devfreq device load monitoring. Function
 * to be called from goveryesr in response to DEVFREQ_GOV_RESUME
 * event or when polling interval is set to yesn-zero.
 */
void devfreq_monitor_resume(struct devfreq *devfreq)
{
	unsigned long freq;

	mutex_lock(&devfreq->lock);
	if (!devfreq->stop_polling)
		goto out;

	if (devfreq->goveryesr->interrupt_driven)
		goto out_update;

	if (!delayed_work_pending(&devfreq->work) &&
			devfreq->profile->polling_ms)
		queue_delayed_work(devfreq_wq, &devfreq->work,
			msecs_to_jiffies(devfreq->profile->polling_ms));

out_update:
	devfreq->last_stat_updated = jiffies;
	devfreq->stop_polling = false;

	if (devfreq->profile->get_cur_freq &&
		!devfreq->profile->get_cur_freq(devfreq->dev.parent, &freq))
		devfreq->previous_freq = freq;

out:
	mutex_unlock(&devfreq->lock);
}
EXPORT_SYMBOL(devfreq_monitor_resume);

/**
 * devfreq_interval_update() - Update device devfreq monitoring interval
 * @devfreq:    the devfreq instance.
 * @delay:      new polling interval to be set.
 *
 * Helper function to set new load monitoring polling interval. Function
 * to be called from goveryesr in response to DEVFREQ_GOV_INTERVAL event.
 */
void devfreq_interval_update(struct devfreq *devfreq, unsigned int *delay)
{
	unsigned int cur_delay = devfreq->profile->polling_ms;
	unsigned int new_delay = *delay;

	mutex_lock(&devfreq->lock);
	devfreq->profile->polling_ms = new_delay;

	if (devfreq->stop_polling)
		goto out;

	if (devfreq->goveryesr->interrupt_driven)
		goto out;

	/* if new delay is zero, stop polling */
	if (!new_delay) {
		mutex_unlock(&devfreq->lock);
		cancel_delayed_work_sync(&devfreq->work);
		return;
	}

	/* if current delay is zero, start polling with new delay */
	if (!cur_delay) {
		queue_delayed_work(devfreq_wq, &devfreq->work,
			msecs_to_jiffies(devfreq->profile->polling_ms));
		goto out;
	}

	/* if current delay is greater than new delay, restart polling */
	if (cur_delay > new_delay) {
		mutex_unlock(&devfreq->lock);
		cancel_delayed_work_sync(&devfreq->work);
		mutex_lock(&devfreq->lock);
		if (!devfreq->stop_polling)
			queue_delayed_work(devfreq_wq, &devfreq->work,
				msecs_to_jiffies(devfreq->profile->polling_ms));
	}
out:
	mutex_unlock(&devfreq->lock);
}
EXPORT_SYMBOL(devfreq_interval_update);

/**
 * devfreq_yestifier_call() - Notify that the device frequency requirements
 *			     has been changed out of devfreq framework.
 * @nb:		the yestifier_block (supposed to be devfreq->nb)
 * @type:	yest used
 * @devp:	yest used
 *
 * Called by a yestifier that uses devfreq->nb.
 */
static int devfreq_yestifier_call(struct yestifier_block *nb, unsigned long type,
				 void *devp)
{
	struct devfreq *devfreq = container_of(nb, struct devfreq, nb);
	int err = -EINVAL;

	mutex_lock(&devfreq->lock);

	devfreq->scaling_min_freq = find_available_min_freq(devfreq);
	if (!devfreq->scaling_min_freq)
		goto out;

	devfreq->scaling_max_freq = find_available_max_freq(devfreq);
	if (!devfreq->scaling_max_freq) {
		devfreq->scaling_max_freq = ULONG_MAX;
		goto out;
	}

	err = update_devfreq(devfreq);

out:
	mutex_unlock(&devfreq->lock);
	if (err)
		dev_err(devfreq->dev.parent,
			"failed to update frequency from OPP yestifier (%d)\n",
			err);

	return NOTIFY_OK;
}

/**
 * qos_yestifier_call() - Common handler for QoS constraints.
 * @devfreq:    the devfreq instance.
 */
static int qos_yestifier_call(struct devfreq *devfreq)
{
	int err;

	mutex_lock(&devfreq->lock);
	err = update_devfreq(devfreq);
	mutex_unlock(&devfreq->lock);
	if (err)
		dev_err(devfreq->dev.parent,
			"failed to update frequency from PM QoS (%d)\n",
			err);

	return NOTIFY_OK;
}

/**
 * qos_min_yestifier_call() - Callback for QoS min_freq changes.
 * @nb:		Should be devfreq->nb_min
 */
static int qos_min_yestifier_call(struct yestifier_block *nb,
					 unsigned long val, void *ptr)
{
	return qos_yestifier_call(container_of(nb, struct devfreq, nb_min));
}

/**
 * qos_max_yestifier_call() - Callback for QoS max_freq changes.
 * @nb:		Should be devfreq->nb_max
 */
static int qos_max_yestifier_call(struct yestifier_block *nb,
					 unsigned long val, void *ptr)
{
	return qos_yestifier_call(container_of(nb, struct devfreq, nb_max));
}

/**
 * devfreq_dev_release() - Callback for struct device to release the device.
 * @dev:	the devfreq device
 *
 * Remove devfreq from the list and release its resources.
 */
static void devfreq_dev_release(struct device *dev)
{
	struct devfreq *devfreq = to_devfreq(dev);
	int err;

	mutex_lock(&devfreq_list_lock);
	list_del(&devfreq->yesde);
	mutex_unlock(&devfreq_list_lock);

	err = dev_pm_qos_remove_yestifier(devfreq->dev.parent, &devfreq->nb_max,
					 DEV_PM_QOS_MAX_FREQUENCY);
	if (err && err != -ENOENT)
		dev_warn(dev->parent,
			"Failed to remove max_freq yestifier: %d\n", err);
	err = dev_pm_qos_remove_yestifier(devfreq->dev.parent, &devfreq->nb_min,
					 DEV_PM_QOS_MIN_FREQUENCY);
	if (err && err != -ENOENT)
		dev_warn(dev->parent,
			"Failed to remove min_freq yestifier: %d\n", err);

	if (dev_pm_qos_request_active(&devfreq->user_max_freq_req)) {
		err = dev_pm_qos_remove_request(&devfreq->user_max_freq_req);
		if (err)
			dev_warn(dev->parent,
				"Failed to remove max_freq request: %d\n", err);
	}
	if (dev_pm_qos_request_active(&devfreq->user_min_freq_req)) {
		err = dev_pm_qos_remove_request(&devfreq->user_min_freq_req);
		if (err)
			dev_warn(dev->parent,
				"Failed to remove min_freq request: %d\n", err);
	}

	if (devfreq->profile->exit)
		devfreq->profile->exit(devfreq->dev.parent);

	mutex_destroy(&devfreq->lock);
	kfree(devfreq);
}

/**
 * devfreq_add_device() - Add devfreq feature to the device
 * @dev:	the device to add devfreq feature.
 * @profile:	device-specific profile to run devfreq.
 * @goveryesr_name:	name of the policy to choose frequency.
 * @data:	private data for the goveryesr. The devfreq framework does yest
 *		touch this value.
 */
struct devfreq *devfreq_add_device(struct device *dev,
				   struct devfreq_dev_profile *profile,
				   const char *goveryesr_name,
				   void *data)
{
	struct devfreq *devfreq;
	struct devfreq_goveryesr *goveryesr;
	static atomic_t devfreq_yes = ATOMIC_INIT(-1);
	int err = 0;

	if (!dev || !profile || !goveryesr_name) {
		dev_err(dev, "%s: Invalid parameters.\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	mutex_lock(&devfreq_list_lock);
	devfreq = find_device_devfreq(dev);
	mutex_unlock(&devfreq_list_lock);
	if (!IS_ERR(devfreq)) {
		dev_err(dev, "%s: devfreq device already exists!\n",
			__func__);
		err = -EINVAL;
		goto err_out;
	}

	devfreq = kzalloc(sizeof(struct devfreq), GFP_KERNEL);
	if (!devfreq) {
		err = -ENOMEM;
		goto err_out;
	}

	mutex_init(&devfreq->lock);
	mutex_lock(&devfreq->lock);
	devfreq->dev.parent = dev;
	devfreq->dev.class = devfreq_class;
	devfreq->dev.release = devfreq_dev_release;
	INIT_LIST_HEAD(&devfreq->yesde);
	devfreq->profile = profile;
	strncpy(devfreq->goveryesr_name, goveryesr_name, DEVFREQ_NAME_LEN);
	devfreq->previous_freq = profile->initial_freq;
	devfreq->last_status.current_frequency = profile->initial_freq;
	devfreq->data = data;
	devfreq->nb.yestifier_call = devfreq_yestifier_call;

	if (!devfreq->profile->max_state && !devfreq->profile->freq_table) {
		mutex_unlock(&devfreq->lock);
		err = set_freq_table(devfreq);
		if (err < 0)
			goto err_dev;
		mutex_lock(&devfreq->lock);
	}

	devfreq->scaling_min_freq = find_available_min_freq(devfreq);
	if (!devfreq->scaling_min_freq) {
		mutex_unlock(&devfreq->lock);
		err = -EINVAL;
		goto err_dev;
	}

	devfreq->scaling_max_freq = find_available_max_freq(devfreq);
	if (!devfreq->scaling_max_freq) {
		mutex_unlock(&devfreq->lock);
		err = -EINVAL;
		goto err_dev;
	}

	devfreq->suspend_freq = dev_pm_opp_get_suspend_opp_freq(dev);
	atomic_set(&devfreq->suspend_count, 0);

	dev_set_name(&devfreq->dev, "devfreq%d",
				atomic_inc_return(&devfreq_yes));
	err = device_register(&devfreq->dev);
	if (err) {
		mutex_unlock(&devfreq->lock);
		put_device(&devfreq->dev);
		goto err_out;
	}

	devfreq->trans_table = devm_kzalloc(&devfreq->dev,
			array3_size(sizeof(unsigned int),
				    devfreq->profile->max_state,
				    devfreq->profile->max_state),
			GFP_KERNEL);
	if (!devfreq->trans_table) {
		mutex_unlock(&devfreq->lock);
		err = -ENOMEM;
		goto err_devfreq;
	}

	devfreq->time_in_state = devm_kcalloc(&devfreq->dev,
			devfreq->profile->max_state,
			sizeof(unsigned long),
			GFP_KERNEL);
	if (!devfreq->time_in_state) {
		mutex_unlock(&devfreq->lock);
		err = -ENOMEM;
		goto err_devfreq;
	}

	devfreq->last_stat_updated = jiffies;

	srcu_init_yestifier_head(&devfreq->transition_yestifier_list);

	mutex_unlock(&devfreq->lock);

	err = dev_pm_qos_add_request(dev, &devfreq->user_min_freq_req,
				     DEV_PM_QOS_MIN_FREQUENCY, 0);
	if (err < 0)
		goto err_devfreq;
	err = dev_pm_qos_add_request(dev, &devfreq->user_max_freq_req,
				     DEV_PM_QOS_MAX_FREQUENCY,
				     PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE);
	if (err < 0)
		goto err_devfreq;

	devfreq->nb_min.yestifier_call = qos_min_yestifier_call;
	err = dev_pm_qos_add_yestifier(devfreq->dev.parent, &devfreq->nb_min,
				      DEV_PM_QOS_MIN_FREQUENCY);
	if (err)
		goto err_devfreq;

	devfreq->nb_max.yestifier_call = qos_max_yestifier_call;
	err = dev_pm_qos_add_yestifier(devfreq->dev.parent, &devfreq->nb_max,
				      DEV_PM_QOS_MAX_FREQUENCY);
	if (err)
		goto err_devfreq;

	mutex_lock(&devfreq_list_lock);

	goveryesr = try_then_request_goveryesr(devfreq->goveryesr_name);
	if (IS_ERR(goveryesr)) {
		dev_err(dev, "%s: Unable to find goveryesr for the device\n",
			__func__);
		err = PTR_ERR(goveryesr);
		goto err_init;
	}

	devfreq->goveryesr = goveryesr;
	err = devfreq->goveryesr->event_handler(devfreq, DEVFREQ_GOV_START,
						NULL);
	if (err) {
		dev_err(dev, "%s: Unable to start goveryesr for the device\n",
			__func__);
		goto err_init;
	}

	list_add(&devfreq->yesde, &devfreq_list);

	mutex_unlock(&devfreq_list_lock);

	return devfreq;

err_init:
	mutex_unlock(&devfreq_list_lock);
err_devfreq:
	devfreq_remove_device(devfreq);
	devfreq = NULL;
err_dev:
	kfree(devfreq);
err_out:
	return ERR_PTR(err);
}
EXPORT_SYMBOL(devfreq_add_device);

/**
 * devfreq_remove_device() - Remove devfreq feature from a device.
 * @devfreq:	the devfreq instance to be removed
 *
 * The opposite of devfreq_add_device().
 */
int devfreq_remove_device(struct devfreq *devfreq)
{
	if (!devfreq)
		return -EINVAL;

	if (devfreq->goveryesr)
		devfreq->goveryesr->event_handler(devfreq,
						 DEVFREQ_GOV_STOP, NULL);
	device_unregister(&devfreq->dev);

	return 0;
}
EXPORT_SYMBOL(devfreq_remove_device);

static int devm_devfreq_dev_match(struct device *dev, void *res, void *data)
{
	struct devfreq **r = res;

	if (WARN_ON(!r || !*r))
		return 0;

	return *r == data;
}

static void devm_devfreq_dev_release(struct device *dev, void *res)
{
	devfreq_remove_device(*(struct devfreq **)res);
}

/**
 * devm_devfreq_add_device() - Resource-managed devfreq_add_device()
 * @dev:	the device to add devfreq feature.
 * @profile:	device-specific profile to run devfreq.
 * @goveryesr_name:	name of the policy to choose frequency.
 * @data:	private data for the goveryesr. The devfreq framework does yest
 *		touch this value.
 *
 * This function manages automatically the memory of devfreq device using device
 * resource management and simplify the free operation for memory of devfreq
 * device.
 */
struct devfreq *devm_devfreq_add_device(struct device *dev,
					struct devfreq_dev_profile *profile,
					const char *goveryesr_name,
					void *data)
{
	struct devfreq **ptr, *devfreq;

	ptr = devres_alloc(devm_devfreq_dev_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	devfreq = devfreq_add_device(dev, profile, goveryesr_name, data);
	if (IS_ERR(devfreq)) {
		devres_free(ptr);
		return devfreq;
	}

	*ptr = devfreq;
	devres_add(dev, ptr);

	return devfreq;
}
EXPORT_SYMBOL(devm_devfreq_add_device);

#ifdef CONFIG_OF
/*
 * devfreq_get_devfreq_by_phandle - Get the devfreq device from devicetree
 * @dev - instance to the given device
 * @index - index into list of devfreq
 *
 * return the instance of devfreq device
 */
struct devfreq *devfreq_get_devfreq_by_phandle(struct device *dev, int index)
{
	struct device_yesde *yesde;
	struct devfreq *devfreq;

	if (!dev)
		return ERR_PTR(-EINVAL);

	if (!dev->of_yesde)
		return ERR_PTR(-EINVAL);

	yesde = of_parse_phandle(dev->of_yesde, "devfreq", index);
	if (!yesde)
		return ERR_PTR(-ENODEV);

	mutex_lock(&devfreq_list_lock);
	list_for_each_entry(devfreq, &devfreq_list, yesde) {
		if (devfreq->dev.parent
			&& devfreq->dev.parent->of_yesde == yesde) {
			mutex_unlock(&devfreq_list_lock);
			of_yesde_put(yesde);
			return devfreq;
		}
	}
	mutex_unlock(&devfreq_list_lock);
	of_yesde_put(yesde);

	return ERR_PTR(-EPROBE_DEFER);
}
#else
struct devfreq *devfreq_get_devfreq_by_phandle(struct device *dev, int index)
{
	return ERR_PTR(-ENODEV);
}
#endif /* CONFIG_OF */
EXPORT_SYMBOL_GPL(devfreq_get_devfreq_by_phandle);

/**
 * devm_devfreq_remove_device() - Resource-managed devfreq_remove_device()
 * @dev:	the device from which to remove devfreq feature.
 * @devfreq:	the devfreq instance to be removed
 */
void devm_devfreq_remove_device(struct device *dev, struct devfreq *devfreq)
{
	WARN_ON(devres_release(dev, devm_devfreq_dev_release,
			       devm_devfreq_dev_match, devfreq));
}
EXPORT_SYMBOL(devm_devfreq_remove_device);

/**
 * devfreq_suspend_device() - Suspend devfreq of a device.
 * @devfreq: the devfreq instance to be suspended
 *
 * This function is intended to be called by the pm callbacks
 * (e.g., runtime_suspend, suspend) of the device driver that
 * holds the devfreq.
 */
int devfreq_suspend_device(struct devfreq *devfreq)
{
	int ret;

	if (!devfreq)
		return -EINVAL;

	if (atomic_inc_return(&devfreq->suspend_count) > 1)
		return 0;

	if (devfreq->goveryesr) {
		ret = devfreq->goveryesr->event_handler(devfreq,
					DEVFREQ_GOV_SUSPEND, NULL);
		if (ret)
			return ret;
	}

	if (devfreq->suspend_freq) {
		mutex_lock(&devfreq->lock);
		ret = devfreq_set_target(devfreq, devfreq->suspend_freq, 0);
		mutex_unlock(&devfreq->lock);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(devfreq_suspend_device);

/**
 * devfreq_resume_device() - Resume devfreq of a device.
 * @devfreq: the devfreq instance to be resumed
 *
 * This function is intended to be called by the pm callbacks
 * (e.g., runtime_resume, resume) of the device driver that
 * holds the devfreq.
 */
int devfreq_resume_device(struct devfreq *devfreq)
{
	int ret;

	if (!devfreq)
		return -EINVAL;

	if (atomic_dec_return(&devfreq->suspend_count) >= 1)
		return 0;

	if (devfreq->resume_freq) {
		mutex_lock(&devfreq->lock);
		ret = devfreq_set_target(devfreq, devfreq->resume_freq, 0);
		mutex_unlock(&devfreq->lock);
		if (ret)
			return ret;
	}

	if (devfreq->goveryesr) {
		ret = devfreq->goveryesr->event_handler(devfreq,
					DEVFREQ_GOV_RESUME, NULL);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(devfreq_resume_device);

/**
 * devfreq_suspend() - Suspend devfreq goveryesrs and devices
 *
 * Called during system wide Suspend/Hibernate cycles for suspending goveryesrs
 * and devices preserving the state for resume. On some platforms the devfreq
 * device must have precise state (frequency) after resume in order to provide
 * fully operating setup.
 */
void devfreq_suspend(void)
{
	struct devfreq *devfreq;
	int ret;

	mutex_lock(&devfreq_list_lock);
	list_for_each_entry(devfreq, &devfreq_list, yesde) {
		ret = devfreq_suspend_device(devfreq);
		if (ret)
			dev_err(&devfreq->dev,
				"failed to suspend devfreq device\n");
	}
	mutex_unlock(&devfreq_list_lock);
}

/**
 * devfreq_resume() - Resume devfreq goveryesrs and devices
 *
 * Called during system wide Suspend/Hibernate cycle for resuming goveryesrs and
 * devices that are suspended with devfreq_suspend().
 */
void devfreq_resume(void)
{
	struct devfreq *devfreq;
	int ret;

	mutex_lock(&devfreq_list_lock);
	list_for_each_entry(devfreq, &devfreq_list, yesde) {
		ret = devfreq_resume_device(devfreq);
		if (ret)
			dev_warn(&devfreq->dev,
				 "failed to resume devfreq device\n");
	}
	mutex_unlock(&devfreq_list_lock);
}

/**
 * devfreq_add_goveryesr() - Add devfreq goveryesr
 * @goveryesr:	the devfreq goveryesr to be added
 */
int devfreq_add_goveryesr(struct devfreq_goveryesr *goveryesr)
{
	struct devfreq_goveryesr *g;
	struct devfreq *devfreq;
	int err = 0;

	if (!goveryesr) {
		pr_err("%s: Invalid parameters.\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&devfreq_list_lock);
	g = find_devfreq_goveryesr(goveryesr->name);
	if (!IS_ERR(g)) {
		pr_err("%s: goveryesr %s already registered\n", __func__,
		       g->name);
		err = -EINVAL;
		goto err_out;
	}

	list_add(&goveryesr->yesde, &devfreq_goveryesr_list);

	list_for_each_entry(devfreq, &devfreq_list, yesde) {
		int ret = 0;
		struct device *dev = devfreq->dev.parent;

		if (!strncmp(devfreq->goveryesr_name, goveryesr->name,
			     DEVFREQ_NAME_LEN)) {
			/* The following should never occur */
			if (devfreq->goveryesr) {
				dev_warn(dev,
					 "%s: Goveryesr %s already present\n",
					 __func__, devfreq->goveryesr->name);
				ret = devfreq->goveryesr->event_handler(devfreq,
							DEVFREQ_GOV_STOP, NULL);
				if (ret) {
					dev_warn(dev,
						 "%s: Goveryesr %s stop = %d\n",
						 __func__,
						 devfreq->goveryesr->name, ret);
				}
				/* Fall through */
			}
			devfreq->goveryesr = goveryesr;
			ret = devfreq->goveryesr->event_handler(devfreq,
						DEVFREQ_GOV_START, NULL);
			if (ret) {
				dev_warn(dev, "%s: Goveryesr %s start=%d\n",
					 __func__, devfreq->goveryesr->name,
					 ret);
			}
		}
	}

err_out:
	mutex_unlock(&devfreq_list_lock);

	return err;
}
EXPORT_SYMBOL(devfreq_add_goveryesr);

/**
 * devfreq_remove_goveryesr() - Remove devfreq feature from a device.
 * @goveryesr:	the devfreq goveryesr to be removed
 */
int devfreq_remove_goveryesr(struct devfreq_goveryesr *goveryesr)
{
	struct devfreq_goveryesr *g;
	struct devfreq *devfreq;
	int err = 0;

	if (!goveryesr) {
		pr_err("%s: Invalid parameters.\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&devfreq_list_lock);
	g = find_devfreq_goveryesr(goveryesr->name);
	if (IS_ERR(g)) {
		pr_err("%s: goveryesr %s yest registered\n", __func__,
		       goveryesr->name);
		err = PTR_ERR(g);
		goto err_out;
	}
	list_for_each_entry(devfreq, &devfreq_list, yesde) {
		int ret;
		struct device *dev = devfreq->dev.parent;

		if (!strncmp(devfreq->goveryesr_name, goveryesr->name,
			     DEVFREQ_NAME_LEN)) {
			/* we should have a devfreq goveryesr! */
			if (!devfreq->goveryesr) {
				dev_warn(dev, "%s: Goveryesr %s NOT present\n",
					 __func__, goveryesr->name);
				continue;
				/* Fall through */
			}
			ret = devfreq->goveryesr->event_handler(devfreq,
						DEVFREQ_GOV_STOP, NULL);
			if (ret) {
				dev_warn(dev, "%s: Goveryesr %s stop=%d\n",
					 __func__, devfreq->goveryesr->name,
					 ret);
			}
			devfreq->goveryesr = NULL;
		}
	}

	list_del(&goveryesr->yesde);
err_out:
	mutex_unlock(&devfreq_list_lock);

	return err;
}
EXPORT_SYMBOL(devfreq_remove_goveryesr);

static ssize_t goveryesr_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	if (!to_devfreq(dev)->goveryesr)
		return -EINVAL;

	return sprintf(buf, "%s\n", to_devfreq(dev)->goveryesr->name);
}

static ssize_t goveryesr_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct devfreq *df = to_devfreq(dev);
	int ret;
	char str_goveryesr[DEVFREQ_NAME_LEN + 1];
	const struct devfreq_goveryesr *goveryesr, *prev_goveryesr;

	ret = sscanf(buf, "%" __stringify(DEVFREQ_NAME_LEN) "s", str_goveryesr);
	if (ret != 1)
		return -EINVAL;

	mutex_lock(&devfreq_list_lock);
	goveryesr = try_then_request_goveryesr(str_goveryesr);
	if (IS_ERR(goveryesr)) {
		ret = PTR_ERR(goveryesr);
		goto out;
	}
	if (df->goveryesr == goveryesr) {
		ret = 0;
		goto out;
	} else if ((df->goveryesr && df->goveryesr->immutable) ||
					goveryesr->immutable) {
		ret = -EINVAL;
		goto out;
	}

	if (df->goveryesr) {
		ret = df->goveryesr->event_handler(df, DEVFREQ_GOV_STOP, NULL);
		if (ret) {
			dev_warn(dev, "%s: Goveryesr %s yest stopped(%d)\n",
				 __func__, df->goveryesr->name, ret);
			goto out;
		}
	}
	prev_goveryesr = df->goveryesr;
	df->goveryesr = goveryesr;
	strncpy(df->goveryesr_name, goveryesr->name, DEVFREQ_NAME_LEN);
	ret = df->goveryesr->event_handler(df, DEVFREQ_GOV_START, NULL);
	if (ret) {
		dev_warn(dev, "%s: Goveryesr %s yest started(%d)\n",
			 __func__, df->goveryesr->name, ret);
		df->goveryesr = prev_goveryesr;
		strncpy(df->goveryesr_name, prev_goveryesr->name,
			DEVFREQ_NAME_LEN);
		ret = df->goveryesr->event_handler(df, DEVFREQ_GOV_START, NULL);
		if (ret) {
			dev_err(dev,
				"%s: reverting to Goveryesr %s failed (%d)\n",
				__func__, df->goveryesr_name, ret);
			df->goveryesr = NULL;
		}
	}
out:
	mutex_unlock(&devfreq_list_lock);

	if (!ret)
		ret = count;
	return ret;
}
static DEVICE_ATTR_RW(goveryesr);

static ssize_t available_goveryesrs_show(struct device *d,
					struct device_attribute *attr,
					char *buf)
{
	struct devfreq *df = to_devfreq(d);
	ssize_t count = 0;

	mutex_lock(&devfreq_list_lock);

	/*
	 * The devfreq with immutable goveryesr (e.g., passive) shows
	 * only own goveryesr.
	 */
	if (df->goveryesr && df->goveryesr->immutable) {
		count = scnprintf(&buf[count], DEVFREQ_NAME_LEN,
				  "%s ", df->goveryesr_name);
	/*
	 * The devfreq device shows the registered goveryesr except for
	 * immutable goveryesrs such as passive goveryesr .
	 */
	} else {
		struct devfreq_goveryesr *goveryesr;

		list_for_each_entry(goveryesr, &devfreq_goveryesr_list, yesde) {
			if (goveryesr->immutable)
				continue;
			count += scnprintf(&buf[count], (PAGE_SIZE - count - 2),
					   "%s ", goveryesr->name);
		}
	}

	mutex_unlock(&devfreq_list_lock);

	/* Truncate the trailing space */
	if (count)
		count--;

	count += sprintf(&buf[count], "\n");

	return count;
}
static DEVICE_ATTR_RO(available_goveryesrs);

static ssize_t cur_freq_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	unsigned long freq;
	struct devfreq *devfreq = to_devfreq(dev);

	if (devfreq->profile->get_cur_freq &&
		!devfreq->profile->get_cur_freq(devfreq->dev.parent, &freq))
		return sprintf(buf, "%lu\n", freq);

	return sprintf(buf, "%lu\n", devfreq->previous_freq);
}
static DEVICE_ATTR_RO(cur_freq);

static ssize_t target_freq_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", to_devfreq(dev)->previous_freq);
}
static DEVICE_ATTR_RO(target_freq);

static ssize_t polling_interval_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", to_devfreq(dev)->profile->polling_ms);
}

static ssize_t polling_interval_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct devfreq *df = to_devfreq(dev);
	unsigned int value;
	int ret;

	if (!df->goveryesr)
		return -EINVAL;

	ret = sscanf(buf, "%u", &value);
	if (ret != 1)
		return -EINVAL;

	df->goveryesr->event_handler(df, DEVFREQ_GOV_INTERVAL, &value);
	ret = count;

	return ret;
}
static DEVICE_ATTR_RW(polling_interval);

static ssize_t min_freq_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct devfreq *df = to_devfreq(dev);
	unsigned long value;
	int ret;

	/*
	 * Protect against theoretical sysfs writes between
	 * device_add and dev_pm_qos_add_request
	 */
	if (!dev_pm_qos_request_active(&df->user_min_freq_req))
		return -EAGAIN;

	ret = sscanf(buf, "%lu", &value);
	if (ret != 1)
		return -EINVAL;

	/* Round down to kHz for PM QoS */
	ret = dev_pm_qos_update_request(&df->user_min_freq_req,
					value / HZ_PER_KHZ);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t min_freq_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct devfreq *df = to_devfreq(dev);
	unsigned long min_freq, max_freq;

	mutex_lock(&df->lock);
	get_freq_range(df, &min_freq, &max_freq);
	mutex_unlock(&df->lock);

	return sprintf(buf, "%lu\n", min_freq);
}

static ssize_t max_freq_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct devfreq *df = to_devfreq(dev);
	unsigned long value;
	int ret;

	/*
	 * Protect against theoretical sysfs writes between
	 * device_add and dev_pm_qos_add_request
	 */
	if (!dev_pm_qos_request_active(&df->user_max_freq_req))
		return -EINVAL;

	ret = sscanf(buf, "%lu", &value);
	if (ret != 1)
		return -EINVAL;

	/*
	 * PM QoS frequencies are in kHz so we need to convert. Convert by
	 * rounding upwards so that the acceptable interval never shrinks.
	 *
	 * For example if the user writes "666666666" to sysfs this value will
	 * be converted to 666667 kHz and back to 666667000 Hz before an OPP
	 * lookup, this ensures that an OPP of 666666666Hz is still accepted.
	 *
	 * A value of zero means "yes limit".
	 */
	if (value)
		value = DIV_ROUND_UP(value, HZ_PER_KHZ);
	else
		value = PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE;

	ret = dev_pm_qos_update_request(&df->user_max_freq_req, value);
	if (ret < 0)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(min_freq);

static ssize_t max_freq_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct devfreq *df = to_devfreq(dev);
	unsigned long min_freq, max_freq;

	mutex_lock(&df->lock);
	get_freq_range(df, &min_freq, &max_freq);
	mutex_unlock(&df->lock);

	return sprintf(buf, "%lu\n", max_freq);
}
static DEVICE_ATTR_RW(max_freq);

static ssize_t available_frequencies_show(struct device *d,
					  struct device_attribute *attr,
					  char *buf)
{
	struct devfreq *df = to_devfreq(d);
	ssize_t count = 0;
	int i;

	mutex_lock(&df->lock);

	for (i = 0; i < df->profile->max_state; i++)
		count += scnprintf(&buf[count], (PAGE_SIZE - count - 2),
				"%lu ", df->profile->freq_table[i]);

	mutex_unlock(&df->lock);
	/* Truncate the trailing space */
	if (count)
		count--;

	count += sprintf(&buf[count], "\n");

	return count;
}
static DEVICE_ATTR_RO(available_frequencies);

static ssize_t trans_stat_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct devfreq *devfreq = to_devfreq(dev);
	ssize_t len;
	int i, j;
	unsigned int max_state = devfreq->profile->max_state;

	if (max_state == 0)
		return sprintf(buf, "Not Supported.\n");

	mutex_lock(&devfreq->lock);
	if (!devfreq->stop_polling &&
			devfreq_update_status(devfreq, devfreq->previous_freq)) {
		mutex_unlock(&devfreq->lock);
		return 0;
	}
	mutex_unlock(&devfreq->lock);

	len = sprintf(buf, "     From  :   To\n");
	len += sprintf(buf + len, "           :");
	for (i = 0; i < max_state; i++)
		len += sprintf(buf + len, "%10lu",
				devfreq->profile->freq_table[i]);

	len += sprintf(buf + len, "   time(ms)\n");

	for (i = 0; i < max_state; i++) {
		if (devfreq->profile->freq_table[i]
					== devfreq->previous_freq) {
			len += sprintf(buf + len, "*");
		} else {
			len += sprintf(buf + len, " ");
		}
		len += sprintf(buf + len, "%10lu:",
				devfreq->profile->freq_table[i]);
		for (j = 0; j < max_state; j++)
			len += sprintf(buf + len, "%10u",
				devfreq->trans_table[(i * max_state) + j]);
		len += sprintf(buf + len, "%10u\n",
			jiffies_to_msecs(devfreq->time_in_state[i]));
	}

	len += sprintf(buf + len, "Total transition : %u\n",
					devfreq->total_trans);
	return len;
}
static DEVICE_ATTR_RO(trans_stat);

static struct attribute *devfreq_attrs[] = {
	&dev_attr_goveryesr.attr,
	&dev_attr_available_goveryesrs.attr,
	&dev_attr_cur_freq.attr,
	&dev_attr_available_frequencies.attr,
	&dev_attr_target_freq.attr,
	&dev_attr_polling_interval.attr,
	&dev_attr_min_freq.attr,
	&dev_attr_max_freq.attr,
	&dev_attr_trans_stat.attr,
	NULL,
};
ATTRIBUTE_GROUPS(devfreq);

static int __init devfreq_init(void)
{
	devfreq_class = class_create(THIS_MODULE, "devfreq");
	if (IS_ERR(devfreq_class)) {
		pr_err("%s: couldn't create class\n", __FILE__);
		return PTR_ERR(devfreq_class);
	}

	devfreq_wq = create_freezable_workqueue("devfreq_wq");
	if (!devfreq_wq) {
		class_destroy(devfreq_class);
		pr_err("%s: couldn't create workqueue\n", __FILE__);
		return -ENOMEM;
	}
	devfreq_class->dev_groups = devfreq_groups;

	return 0;
}
subsys_initcall(devfreq_init);

/*
 * The following are helper functions for devfreq user device drivers with
 * OPP framework.
 */

/**
 * devfreq_recommended_opp() - Helper function to get proper OPP for the
 *			     freq value given to target callback.
 * @dev:	The devfreq user device. (parent of devfreq)
 * @freq:	The frequency given to target function
 * @flags:	Flags handed from devfreq framework.
 *
 * The callers are required to call dev_pm_opp_put() for the returned OPP after
 * use.
 */
struct dev_pm_opp *devfreq_recommended_opp(struct device *dev,
					   unsigned long *freq,
					   u32 flags)
{
	struct dev_pm_opp *opp;

	if (flags & DEVFREQ_FLAG_LEAST_UPPER_BOUND) {
		/* The freq is an upper bound. opp should be lower */
		opp = dev_pm_opp_find_freq_floor(dev, freq);

		/* If yest available, use the closest opp */
		if (opp == ERR_PTR(-ERANGE))
			opp = dev_pm_opp_find_freq_ceil(dev, freq);
	} else {
		/* The freq is an lower bound. opp should be higher */
		opp = dev_pm_opp_find_freq_ceil(dev, freq);

		/* If yest available, use the closest opp */
		if (opp == ERR_PTR(-ERANGE))
			opp = dev_pm_opp_find_freq_floor(dev, freq);
	}

	return opp;
}
EXPORT_SYMBOL(devfreq_recommended_opp);

/**
 * devfreq_register_opp_yestifier() - Helper function to get devfreq yestified
 *				     for any changes in the OPP availability
 *				     changes
 * @dev:	The devfreq user device. (parent of devfreq)
 * @devfreq:	The devfreq object.
 */
int devfreq_register_opp_yestifier(struct device *dev, struct devfreq *devfreq)
{
	return dev_pm_opp_register_yestifier(dev, &devfreq->nb);
}
EXPORT_SYMBOL(devfreq_register_opp_yestifier);

/**
 * devfreq_unregister_opp_yestifier() - Helper function to stop getting devfreq
 *				       yestified for any changes in the OPP
 *				       availability changes anymore.
 * @dev:	The devfreq user device. (parent of devfreq)
 * @devfreq:	The devfreq object.
 *
 * At exit() callback of devfreq_dev_profile, this must be included if
 * devfreq_recommended_opp is used.
 */
int devfreq_unregister_opp_yestifier(struct device *dev, struct devfreq *devfreq)
{
	return dev_pm_opp_unregister_yestifier(dev, &devfreq->nb);
}
EXPORT_SYMBOL(devfreq_unregister_opp_yestifier);

static void devm_devfreq_opp_release(struct device *dev, void *res)
{
	devfreq_unregister_opp_yestifier(dev, *(struct devfreq **)res);
}

/**
 * devm_devfreq_register_opp_yestifier() - Resource-managed
 *					  devfreq_register_opp_yestifier()
 * @dev:	The devfreq user device. (parent of devfreq)
 * @devfreq:	The devfreq object.
 */
int devm_devfreq_register_opp_yestifier(struct device *dev,
				       struct devfreq *devfreq)
{
	struct devfreq **ptr;
	int ret;

	ptr = devres_alloc(devm_devfreq_opp_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = devfreq_register_opp_yestifier(dev, devfreq);
	if (ret) {
		devres_free(ptr);
		return ret;
	}

	*ptr = devfreq;
	devres_add(dev, ptr);

	return 0;
}
EXPORT_SYMBOL(devm_devfreq_register_opp_yestifier);

/**
 * devm_devfreq_unregister_opp_yestifier() - Resource-managed
 *					    devfreq_unregister_opp_yestifier()
 * @dev:	The devfreq user device. (parent of devfreq)
 * @devfreq:	The devfreq object.
 */
void devm_devfreq_unregister_opp_yestifier(struct device *dev,
					 struct devfreq *devfreq)
{
	WARN_ON(devres_release(dev, devm_devfreq_opp_release,
			       devm_devfreq_dev_match, devfreq));
}
EXPORT_SYMBOL(devm_devfreq_unregister_opp_yestifier);

/**
 * devfreq_register_yestifier() - Register a driver with devfreq
 * @devfreq:	The devfreq object.
 * @nb:		The yestifier block to register.
 * @list:	DEVFREQ_TRANSITION_NOTIFIER.
 */
int devfreq_register_yestifier(struct devfreq *devfreq,
			      struct yestifier_block *nb,
			      unsigned int list)
{
	int ret = 0;

	if (!devfreq)
		return -EINVAL;

	switch (list) {
	case DEVFREQ_TRANSITION_NOTIFIER:
		ret = srcu_yestifier_chain_register(
				&devfreq->transition_yestifier_list, nb);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}
EXPORT_SYMBOL(devfreq_register_yestifier);

/*
 * devfreq_unregister_yestifier() - Unregister a driver with devfreq
 * @devfreq:	The devfreq object.
 * @nb:		The yestifier block to be unregistered.
 * @list:	DEVFREQ_TRANSITION_NOTIFIER.
 */
int devfreq_unregister_yestifier(struct devfreq *devfreq,
				struct yestifier_block *nb,
				unsigned int list)
{
	int ret = 0;

	if (!devfreq)
		return -EINVAL;

	switch (list) {
	case DEVFREQ_TRANSITION_NOTIFIER:
		ret = srcu_yestifier_chain_unregister(
				&devfreq->transition_yestifier_list, nb);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}
EXPORT_SYMBOL(devfreq_unregister_yestifier);

struct devfreq_yestifier_devres {
	struct devfreq *devfreq;
	struct yestifier_block *nb;
	unsigned int list;
};

static void devm_devfreq_yestifier_release(struct device *dev, void *res)
{
	struct devfreq_yestifier_devres *this = res;

	devfreq_unregister_yestifier(this->devfreq, this->nb, this->list);
}

/**
 * devm_devfreq_register_yestifier()
	- Resource-managed devfreq_register_yestifier()
 * @dev:	The devfreq user device. (parent of devfreq)
 * @devfreq:	The devfreq object.
 * @nb:		The yestifier block to be unregistered.
 * @list:	DEVFREQ_TRANSITION_NOTIFIER.
 */
int devm_devfreq_register_yestifier(struct device *dev,
				struct devfreq *devfreq,
				struct yestifier_block *nb,
				unsigned int list)
{
	struct devfreq_yestifier_devres *ptr;
	int ret;

	ptr = devres_alloc(devm_devfreq_yestifier_release, sizeof(*ptr),
				GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = devfreq_register_yestifier(devfreq, nb, list);
	if (ret) {
		devres_free(ptr);
		return ret;
	}

	ptr->devfreq = devfreq;
	ptr->nb = nb;
	ptr->list = list;
	devres_add(dev, ptr);

	return 0;
}
EXPORT_SYMBOL(devm_devfreq_register_yestifier);

/**
 * devm_devfreq_unregister_yestifier()
	- Resource-managed devfreq_unregister_yestifier()
 * @dev:	The devfreq user device. (parent of devfreq)
 * @devfreq:	The devfreq object.
 * @nb:		The yestifier block to be unregistered.
 * @list:	DEVFREQ_TRANSITION_NOTIFIER.
 */
void devm_devfreq_unregister_yestifier(struct device *dev,
				      struct devfreq *devfreq,
				      struct yestifier_block *nb,
				      unsigned int list)
{
	WARN_ON(devres_release(dev, devm_devfreq_yestifier_release,
			       devm_devfreq_dev_match, devfreq));
}
EXPORT_SYMBOL(devm_devfreq_unregister_yestifier);
