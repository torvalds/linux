// SPDX-License-Identifier: GPL-2.0-only
/*
 * ChromeOS EC multi-function device
 *
 * Copyright (C) 2012 Google, Inc
 *
 * The ChromeOS EC multi function device is used to mux all the requests
 * to the EC device for its multiple features: keyboard controller,
 * battery charging and regulator control, firmware update.
 */

#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/suspend.h>

#include "cros_ec.h"

#define CROS_EC_DEV_EC_INDEX 0
#define CROS_EC_DEV_PD_INDEX 1

static struct cros_ec_platform ec_p = {
	.ec_name = CROS_EC_DEV_NAME,
	.cmd_offset = EC_CMD_PASSTHRU_OFFSET(CROS_EC_DEV_EC_INDEX),
};

static struct cros_ec_platform pd_p = {
	.ec_name = CROS_EC_DEV_PD_NAME,
	.cmd_offset = EC_CMD_PASSTHRU_OFFSET(CROS_EC_DEV_PD_INDEX),
};

static irqreturn_t ec_irq_handler(int irq, void *data)
{
	struct cros_ec_device *ec_dev = data;

	ec_dev->last_event_time = cros_ec_get_time_ns();

	return IRQ_WAKE_THREAD;
}

/**
 * cros_ec_handle_event() - process and forward pending events on EC
 * @ec_dev: Device with events to process.
 *
 * Call this function in a loop when the kernel is notified that the EC has
 * pending events.
 *
 * Return: true if more events are still pending and this function should be
 * called again.
 */
bool cros_ec_handle_event(struct cros_ec_device *ec_dev)
{
	bool wake_event;
	bool ec_has_more_events;
	int ret;

	ret = cros_ec_get_next_event(ec_dev, &wake_event, &ec_has_more_events);

	/*
	 * Signal only if wake host events or any interrupt if
	 * cros_ec_get_next_event() returned an error (default value for
	 * wake_event is true)
	 */
	if (wake_event && device_may_wakeup(ec_dev->dev))
		pm_wakeup_event(ec_dev->dev, 0);

	if (ret > 0)
		blocking_notifier_call_chain(&ec_dev->event_notifier,
					     0, ec_dev);

	return ec_has_more_events;
}
EXPORT_SYMBOL(cros_ec_handle_event);

static irqreturn_t ec_irq_thread(int irq, void *data)
{
	struct cros_ec_device *ec_dev = data;
	bool ec_has_more_events;

	do {
		ec_has_more_events = cros_ec_handle_event(ec_dev);
	} while (ec_has_more_events);

	return IRQ_HANDLED;
}

static int cros_ec_sleep_event(struct cros_ec_device *ec_dev, u8 sleep_event)
{
	int ret;
	struct {
		struct cros_ec_command msg;
		union {
			struct ec_params_host_sleep_event req0;
			struct ec_params_host_sleep_event_v1 req1;
			struct ec_response_host_sleep_event_v1 resp1;
		} u;
	} __packed buf;

	memset(&buf, 0, sizeof(buf));

	if (ec_dev->host_sleep_v1) {
		buf.u.req1.sleep_event = sleep_event;
		buf.u.req1.suspend_params.sleep_timeout_ms =
				EC_HOST_SLEEP_TIMEOUT_DEFAULT;

		buf.msg.outsize = sizeof(buf.u.req1);
		if ((sleep_event == HOST_SLEEP_EVENT_S3_RESUME) ||
		    (sleep_event == HOST_SLEEP_EVENT_S0IX_RESUME))
			buf.msg.insize = sizeof(buf.u.resp1);

		buf.msg.version = 1;

	} else {
		buf.u.req0.sleep_event = sleep_event;
		buf.msg.outsize = sizeof(buf.u.req0);
	}

	buf.msg.command = EC_CMD_HOST_SLEEP_EVENT;

	ret = cros_ec_cmd_xfer(ec_dev, &buf.msg);

	/* For now, report failure to transition to S0ix with a warning. */
	if (ret >= 0 && ec_dev->host_sleep_v1 &&
	    (sleep_event == HOST_SLEEP_EVENT_S0IX_RESUME)) {
		ec_dev->last_resume_result =
			buf.u.resp1.resume_response.sleep_transitions;

		WARN_ONCE(buf.u.resp1.resume_response.sleep_transitions &
			  EC_HOST_RESUME_SLEEP_TIMEOUT,
			  "EC detected sleep transition timeout. Total slp_s0 transitions: %d",
			  buf.u.resp1.resume_response.sleep_transitions &
			  EC_HOST_RESUME_SLEEP_TRANSITIONS_MASK);
	}

	return ret;
}

/**
 * cros_ec_register() - Register a new ChromeOS EC, using the provided info.
 * @ec_dev: Device to register.
 *
 * Before calling this, allocate a pointer to a new device and then fill
 * in all the fields up to the --private-- marker.
 *
 * Return: 0 on success or negative error code.
 */
int cros_ec_register(struct cros_ec_device *ec_dev)
{
	struct device *dev = ec_dev->dev;
	int err = 0;

	BLOCKING_INIT_NOTIFIER_HEAD(&ec_dev->event_notifier);

	ec_dev->max_request = sizeof(struct ec_params_hello);
	ec_dev->max_response = sizeof(struct ec_response_get_protocol_info);
	ec_dev->max_passthru = 0;

	ec_dev->din = devm_kzalloc(dev, ec_dev->din_size, GFP_KERNEL);
	if (!ec_dev->din)
		return -ENOMEM;

	ec_dev->dout = devm_kzalloc(dev, ec_dev->dout_size, GFP_KERNEL);
	if (!ec_dev->dout)
		return -ENOMEM;

	mutex_init(&ec_dev->lock);

	err = cros_ec_query_all(ec_dev);
	if (err) {
		dev_err(dev, "Cannot identify the EC: error %d\n", err);
		return err;
	}

	if (ec_dev->irq > 0) {
		err = devm_request_threaded_irq(dev, ec_dev->irq,
						ec_irq_handler,
						ec_irq_thread,
						IRQF_TRIGGER_LOW | IRQF_ONESHOT,
						"chromeos-ec", ec_dev);
		if (err) {
			dev_err(dev, "Failed to request IRQ %d: %d",
				ec_dev->irq, err);
			return err;
		}
	}

	/* Register a platform device for the main EC instance */
	ec_dev->ec = platform_device_register_data(ec_dev->dev, "cros-ec-dev",
					PLATFORM_DEVID_AUTO, &ec_p,
					sizeof(struct cros_ec_platform));
	if (IS_ERR(ec_dev->ec)) {
		dev_err(ec_dev->dev,
			"Failed to create CrOS EC platform device\n");
		return PTR_ERR(ec_dev->ec);
	}

	if (ec_dev->max_passthru) {
		/*
		 * Register a platform device for the PD behind the main EC.
		 * We make the following assumptions:
		 * - behind an EC, we have a pd
		 * - only one device added.
		 * - the EC is responsive at init time (it is not true for a
		 *   sensor hub).
		 */
		ec_dev->pd = platform_device_register_data(ec_dev->dev,
					"cros-ec-dev",
					PLATFORM_DEVID_AUTO, &pd_p,
					sizeof(struct cros_ec_platform));
		if (IS_ERR(ec_dev->pd)) {
			dev_err(ec_dev->dev,
				"Failed to create CrOS PD platform device\n");
			platform_device_unregister(ec_dev->ec);
			return PTR_ERR(ec_dev->pd);
		}
	}

	if (IS_ENABLED(CONFIG_OF) && dev->of_node) {
		err = devm_of_platform_populate(dev);
		if (err) {
			platform_device_unregister(ec_dev->pd);
			platform_device_unregister(ec_dev->ec);
			dev_err(dev, "Failed to register sub-devices\n");
			return err;
		}
	}

	/*
	 * Clear sleep event - this will fail harmlessly on platforms that
	 * don't implement the sleep event host command.
	 */
	err = cros_ec_sleep_event(ec_dev, 0);
	if (err < 0)
		dev_dbg(ec_dev->dev, "Error %d clearing sleep event to ec",
			err);

	dev_info(dev, "Chrome EC device registered\n");

	return 0;
}
EXPORT_SYMBOL(cros_ec_register);

/**
 * cros_ec_unregister() - Remove a ChromeOS EC.
 * @ec_dev: Device to unregister.
 *
 * Call this to deregister a ChromeOS EC, then clean up any private data.
 *
 * Return: 0 on success or negative error code.
 */
int cros_ec_unregister(struct cros_ec_device *ec_dev)
{
	if (ec_dev->pd)
		platform_device_unregister(ec_dev->pd);
	platform_device_unregister(ec_dev->ec);

	return 0;
}
EXPORT_SYMBOL(cros_ec_unregister);

#ifdef CONFIG_PM_SLEEP
/**
 * cros_ec_suspend() - Handle a suspend operation for the ChromeOS EC device.
 * @ec_dev: Device to suspend.
 *
 * This can be called by drivers to handle a suspend event.
 *
 * Return: 0 on success or negative error code.
 */
int cros_ec_suspend(struct cros_ec_device *ec_dev)
{
	struct device *dev = ec_dev->dev;
	int ret;
	u8 sleep_event;

	sleep_event = (!IS_ENABLED(CONFIG_ACPI) || pm_suspend_via_firmware()) ?
		      HOST_SLEEP_EVENT_S3_SUSPEND :
		      HOST_SLEEP_EVENT_S0IX_SUSPEND;

	ret = cros_ec_sleep_event(ec_dev, sleep_event);
	if (ret < 0)
		dev_dbg(ec_dev->dev, "Error %d sending suspend event to ec",
			ret);

	if (device_may_wakeup(dev))
		ec_dev->wake_enabled = !enable_irq_wake(ec_dev->irq);

	disable_irq(ec_dev->irq);
	ec_dev->was_wake_device = ec_dev->wake_enabled;
	ec_dev->suspended = true;

	return 0;
}
EXPORT_SYMBOL(cros_ec_suspend);

static void cros_ec_report_events_during_suspend(struct cros_ec_device *ec_dev)
{
	while (ec_dev->mkbp_event_supported &&
	       cros_ec_get_next_event(ec_dev, NULL, NULL) > 0)
		blocking_notifier_call_chain(&ec_dev->event_notifier,
					     1, ec_dev);
}

/**
 * cros_ec_resume() - Handle a resume operation for the ChromeOS EC device.
 * @ec_dev: Device to resume.
 *
 * This can be called by drivers to handle a resume event.
 *
 * Return: 0 on success or negative error code.
 */
int cros_ec_resume(struct cros_ec_device *ec_dev)
{
	int ret;
	u8 sleep_event;

	ec_dev->suspended = false;
	enable_irq(ec_dev->irq);

	sleep_event = (!IS_ENABLED(CONFIG_ACPI) || pm_suspend_via_firmware()) ?
		      HOST_SLEEP_EVENT_S3_RESUME :
		      HOST_SLEEP_EVENT_S0IX_RESUME;

	ret = cros_ec_sleep_event(ec_dev, sleep_event);
	if (ret < 0)
		dev_dbg(ec_dev->dev, "Error %d sending resume event to ec",
			ret);

	if (ec_dev->wake_enabled) {
		disable_irq_wake(ec_dev->irq);
		ec_dev->wake_enabled = 0;
	}
	/*
	 * Let the mfd devices know about events that occur during
	 * suspend. This way the clients know what to do with them.
	 */
	cros_ec_report_events_during_suspend(ec_dev);


	return 0;
}
EXPORT_SYMBOL(cros_ec_resume);

#endif

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ChromeOS EC core driver");
