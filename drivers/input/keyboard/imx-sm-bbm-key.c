// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP.
 */

#include <linux/input.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/scmi_protocol.h>
#include <linux/scmi_imx_protocol.h>
#include <linux/suspend.h>

#define DEBOUNCE_TIME		30
#define REPEAT_INTERVAL		60

struct scmi_imx_bbm {
	struct scmi_protocol_handle *ph;
	const struct scmi_imx_bbm_proto_ops *ops;
	struct notifier_block nb;
	int keycode;
	int keystate;  /* 1:pressed */
	bool suspended;
	struct delayed_work check_work;
	struct input_dev *input;
};

static void scmi_imx_bbm_pwrkey_check_for_events(struct work_struct *work)
{
	struct scmi_imx_bbm *bbnsm = container_of(to_delayed_work(work),
						  struct scmi_imx_bbm, check_work);
	struct scmi_protocol_handle *ph = bbnsm->ph;
	struct input_dev *input = bbnsm->input;
	u32 state = 0;
	int ret;

	ret = bbnsm->ops->button_get(ph, &state);
	if (ret) {
		pr_err("%s: %d\n", __func__, ret);
		return;
	}

	pr_debug("%s: state: %d, keystate %d\n", __func__, state, bbnsm->keystate);

	/* only report new event if status changed */
	if (state ^ bbnsm->keystate) {
		bbnsm->keystate = state;
		input_event(input, EV_KEY, bbnsm->keycode, state);
		input_sync(input);
		pm_relax(bbnsm->input->dev.parent);
		pr_debug("EV_KEY: %x\n", bbnsm->keycode);
	}

	/* repeat check if pressed long */
	if (state)
		schedule_delayed_work(&bbnsm->check_work, msecs_to_jiffies(REPEAT_INTERVAL));
}

static int scmi_imx_bbm_pwrkey_event(struct scmi_imx_bbm *bbnsm)
{
	struct input_dev *input = bbnsm->input;

	pm_wakeup_event(input->dev.parent, 0);

	/*
	 * Directly report key event after resume to make no key press
	 * event is missed.
	 */
	if (READ_ONCE(bbnsm->suspended)) {
		bbnsm->keystate = 1;
		input_event(input, EV_KEY, bbnsm->keycode, 1);
		input_sync(input);
		WRITE_ONCE(bbnsm->suspended, false);
	}

	schedule_delayed_work(&bbnsm->check_work, msecs_to_jiffies(DEBOUNCE_TIME));

	return 0;
}

static void scmi_imx_bbm_pwrkey_act(void *pdata)
{
	struct scmi_imx_bbm *bbnsm = pdata;

	cancel_delayed_work_sync(&bbnsm->check_work);
}

static int scmi_imx_bbm_key_notifier(struct notifier_block *nb, unsigned long event, void *data)
{
	struct scmi_imx_bbm *bbnsm = container_of(nb, struct scmi_imx_bbm, nb);
	struct scmi_imx_bbm_notif_report *r = data;

	if (r->is_button) {
		pr_debug("BBM Button Power key pressed\n");
		scmi_imx_bbm_pwrkey_event(bbnsm);
	} else {
		/* Should never reach here */
		pr_err("Unexpected BBM event: %s\n", __func__);
	}

	return 0;
}

static int scmi_imx_bbm_pwrkey_init(struct scmi_device *sdev)
{
	const struct scmi_handle *handle = sdev->handle;
	struct device *dev = &sdev->dev;
	struct scmi_imx_bbm *bbnsm = dev_get_drvdata(dev);
	struct input_dev *input;
	int ret;

	if (device_property_read_u32(dev, "linux,code", &bbnsm->keycode)) {
		bbnsm->keycode = KEY_POWER;
		dev_warn(dev, "key code is not specified, using default KEY_POWER\n");
	}

	INIT_DELAYED_WORK(&bbnsm->check_work, scmi_imx_bbm_pwrkey_check_for_events);

	input = devm_input_allocate_device(dev);
	if (!input) {
		dev_err(dev, "failed to allocate the input device for SCMI IMX BBM\n");
		return -ENOMEM;
	}

	input->name = dev_name(dev);
	input->phys = "bbnsm-pwrkey/input0";
	input->id.bustype = BUS_HOST;

	input_set_capability(input, EV_KEY, bbnsm->keycode);

	ret = devm_add_action_or_reset(dev, scmi_imx_bbm_pwrkey_act, bbnsm);
	if (ret) {
		dev_err(dev, "failed to register remove action\n");
		return ret;
	}

	bbnsm->input = input;

	bbnsm->nb.notifier_call = &scmi_imx_bbm_key_notifier;
	ret = handle->notify_ops->devm_event_notifier_register(sdev, SCMI_PROTOCOL_IMX_BBM,
							       SCMI_EVENT_IMX_BBM_BUTTON,
							       NULL, &bbnsm->nb);

	if (ret)
		dev_err(dev, "Failed to register BBM Button Events %d:", ret);

	ret = input_register_device(input);
	if (ret) {
		dev_err(dev, "failed to register input device\n");
		return ret;
	}

	return 0;
}

static int scmi_imx_bbm_key_probe(struct scmi_device *sdev)
{
	const struct scmi_handle *handle = sdev->handle;
	struct device *dev = &sdev->dev;
	struct scmi_protocol_handle *ph;
	struct scmi_imx_bbm *bbnsm;
	int ret;

	if (!handle)
		return -ENODEV;

	bbnsm = devm_kzalloc(dev, sizeof(*bbnsm), GFP_KERNEL);
	if (!bbnsm)
		return -ENOMEM;

	bbnsm->ops = handle->devm_protocol_get(sdev, SCMI_PROTOCOL_IMX_BBM, &ph);
	if (IS_ERR(bbnsm->ops))
		return PTR_ERR(bbnsm->ops);

	bbnsm->ph = ph;

	device_init_wakeup(dev, true);

	dev_set_drvdata(dev, bbnsm);

	ret = scmi_imx_bbm_pwrkey_init(sdev);
	if (ret)
		device_init_wakeup(dev, false);

	return ret;
}

static int __maybe_unused scmi_imx_bbm_key_suspend(struct device *dev)
{
	struct scmi_imx_bbm *bbnsm = dev_get_drvdata(dev);

	WRITE_ONCE(bbnsm->suspended, true);

	return 0;
}

static int __maybe_unused scmi_imx_bbm_key_resume(struct device *dev)
{
	return 0;
}

static SIMPLE_DEV_PM_OPS(scmi_imx_bbm_pm_key_ops, scmi_imx_bbm_key_suspend,
			 scmi_imx_bbm_key_resume);

static const struct scmi_device_id scmi_id_table[] = {
	{ SCMI_PROTOCOL_IMX_BBM, "imx-bbm-key" },
	{ },
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_imx_bbm_key_driver = {
	.driver = {
		.pm = &scmi_imx_bbm_pm_key_ops,
	},
	.name = "scmi-imx-bbm-key",
	.probe = scmi_imx_bbm_key_probe,
	.id_table = scmi_id_table,
};
module_scmi_driver(scmi_imx_bbm_key_driver);

MODULE_AUTHOR("Peng Fan <peng.fan@nxp.com>");
MODULE_DESCRIPTION("IMX SM BBM Key driver");
MODULE_LICENSE("GPL");
