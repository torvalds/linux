/*
 * HID Sensors Driver
 * Copyright (c) 2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/hid-sensor-hub.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/iio/sysfs.h>
#include "hid-sensor-trigger.h"

static int _hid_sensor_power_state(struct hid_sensor_common *st, bool state)
{
	int state_val;
	int report_val;
	s32 poll_value = 0;

	if (state) {
		if (!atomic_read(&st->user_requested_state))
			return 0;
		if (sensor_hub_device_open(st->hsdev))
			return -EIO;

		atomic_inc(&st->data_ready);

		state_val = hid_sensor_get_usage_index(st->hsdev,
			st->power_state.report_id,
			st->power_state.index,
			HID_USAGE_SENSOR_PROP_POWER_STATE_D0_FULL_POWER_ENUM);
		report_val = hid_sensor_get_usage_index(st->hsdev,
			st->report_state.report_id,
			st->report_state.index,
			HID_USAGE_SENSOR_PROP_REPORTING_STATE_ALL_EVENTS_ENUM);

		poll_value = hid_sensor_read_poll_value(st);
	} else {
		int val;

		val = atomic_dec_if_positive(&st->data_ready);
		if (val < 0)
			return 0;

		sensor_hub_device_close(st->hsdev);
		state_val = hid_sensor_get_usage_index(st->hsdev,
			st->power_state.report_id,
			st->power_state.index,
			HID_USAGE_SENSOR_PROP_POWER_STATE_D4_POWER_OFF_ENUM);
		report_val = hid_sensor_get_usage_index(st->hsdev,
			st->report_state.report_id,
			st->report_state.index,
			HID_USAGE_SENSOR_PROP_REPORTING_STATE_NO_EVENTS_ENUM);
	}

	if (state_val >= 0) {
		state_val += st->power_state.logical_minimum;
		sensor_hub_set_feature(st->hsdev, st->power_state.report_id,
				       st->power_state.index, sizeof(state_val),
				       &state_val);
	}

	if (report_val >= 0) {
		report_val += st->report_state.logical_minimum;
		sensor_hub_set_feature(st->hsdev, st->report_state.report_id,
				       st->report_state.index,
				       sizeof(report_val),
				       &report_val);
	}

	sensor_hub_get_feature(st->hsdev, st->power_state.report_id,
			       st->power_state.index,
			       sizeof(state_val), &state_val);
	if (state && poll_value)
		msleep_interruptible(poll_value * 2);

	return 0;
}
EXPORT_SYMBOL(hid_sensor_power_state);

int hid_sensor_power_state(struct hid_sensor_common *st, bool state)
{

#ifdef CONFIG_PM
	int ret;

	atomic_set(&st->user_requested_state, state);
	if (state)
		ret = pm_runtime_get_sync(&st->pdev->dev);
	else {
		pm_runtime_mark_last_busy(&st->pdev->dev);
		ret = pm_runtime_put_autosuspend(&st->pdev->dev);
	}
	if (ret < 0) {
		if (state)
			pm_runtime_put_noidle(&st->pdev->dev);
		return ret;
	}

	return 0;
#else
	atomic_set(&st->user_requested_state, state);
	return _hid_sensor_power_state(st, state);
#endif
}

static void hid_sensor_set_power_work(struct work_struct *work)
{
	struct hid_sensor_common *attrb = container_of(work,
						       struct hid_sensor_common,
						       work);

	if (attrb->poll_interval >= 0)
		sensor_hub_set_feature(attrb->hsdev, attrb->poll.report_id,
				       attrb->poll.index,
				       sizeof(attrb->poll_interval),
				       &attrb->poll_interval);

	if (attrb->raw_hystersis >= 0)
		sensor_hub_set_feature(attrb->hsdev,
				       attrb->sensitivity.report_id,
				       attrb->sensitivity.index,
				       sizeof(attrb->raw_hystersis),
				       &attrb->raw_hystersis);

	_hid_sensor_power_state(attrb, true);
}

static int hid_sensor_data_rdy_trigger_set_state(struct iio_trigger *trig,
						bool state)
{
	return hid_sensor_power_state(iio_trigger_get_drvdata(trig), state);
}

void hid_sensor_remove_trigger(struct hid_sensor_common *attrb)
{
	pm_runtime_disable(&attrb->pdev->dev);
	pm_runtime_set_suspended(&attrb->pdev->dev);
	pm_runtime_put_noidle(&attrb->pdev->dev);

	cancel_work_sync(&attrb->work);
	iio_trigger_unregister(attrb->trigger);
	iio_trigger_free(attrb->trigger);
}
EXPORT_SYMBOL(hid_sensor_remove_trigger);

static const struct iio_trigger_ops hid_sensor_trigger_ops = {
	.owner = THIS_MODULE,
	.set_trigger_state = &hid_sensor_data_rdy_trigger_set_state,
};

int hid_sensor_setup_trigger(struct iio_dev *indio_dev, const char *name,
				struct hid_sensor_common *attrb)
{
	int ret;
	struct iio_trigger *trig;

	trig = iio_trigger_alloc("%s-dev%d", name, indio_dev->id);
	if (trig == NULL) {
		dev_err(&indio_dev->dev, "Trigger Allocate Failed\n");
		ret = -ENOMEM;
		goto error_ret;
	}

	trig->dev.parent = indio_dev->dev.parent;
	iio_trigger_set_drvdata(trig, attrb);
	trig->ops = &hid_sensor_trigger_ops;
	ret = iio_trigger_register(trig);

	if (ret) {
		dev_err(&indio_dev->dev, "Trigger Register Failed\n");
		goto error_free_trig;
	}
	attrb->trigger = trig;
	indio_dev->trig = iio_trigger_get(trig);

	ret = pm_runtime_set_active(&indio_dev->dev);
	if (ret)
		goto error_unreg_trigger;

	iio_device_set_drvdata(indio_dev, attrb);

	INIT_WORK(&attrb->work, hid_sensor_set_power_work);

	pm_suspend_ignore_children(&attrb->pdev->dev, true);
	pm_runtime_enable(&attrb->pdev->dev);
	/* Default to 3 seconds, but can be changed from sysfs */
	pm_runtime_set_autosuspend_delay(&attrb->pdev->dev,
					 3000);
	pm_runtime_use_autosuspend(&attrb->pdev->dev);

	return ret;
error_unreg_trigger:
	iio_trigger_unregister(trig);
error_free_trig:
	iio_trigger_free(trig);
error_ret:
	return ret;
}
EXPORT_SYMBOL(hid_sensor_setup_trigger);

static int __maybe_unused hid_sensor_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct hid_sensor_common *attrb = iio_device_get_drvdata(indio_dev);

	return _hid_sensor_power_state(attrb, false);
}

static int __maybe_unused hid_sensor_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct hid_sensor_common *attrb = iio_device_get_drvdata(indio_dev);
	schedule_work(&attrb->work);
	return 0;
}

static int __maybe_unused hid_sensor_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct hid_sensor_common *attrb = iio_device_get_drvdata(indio_dev);
	return _hid_sensor_power_state(attrb, true);
}

const struct dev_pm_ops hid_sensor_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(hid_sensor_suspend, hid_sensor_resume)
	SET_RUNTIME_PM_OPS(hid_sensor_suspend,
			   hid_sensor_runtime_resume, NULL)
};
EXPORT_SYMBOL(hid_sensor_pm_ops);

MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@intel.com>");
MODULE_DESCRIPTION("HID Sensor trigger processing");
MODULE_LICENSE("GPL");
