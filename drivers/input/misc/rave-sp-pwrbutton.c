// SPDX-License-Identifier: GPL-2.0+
//
// Power Button driver for RAVE SP
//
// Copyright (C) 2017 Zodiac Inflight Innovations
//
//

#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mfd/rave-sp.h>
#include <linux/platform_device.h>

#define RAVE_SP_EVNT_BUTTON_PRESS	(RAVE_SP_EVNT_BASE + 0x00)

struct rave_sp_power_button {
	struct input_dev *idev;
	struct notifier_block nb;
};

static int rave_sp_power_button_event(struct notifier_block *nb,
				      unsigned long action, void *data)
{
	struct rave_sp_power_button *pb =
		container_of(nb, struct rave_sp_power_button, nb);
	const u8 event = rave_sp_action_unpack_event(action);
	const u8 value = rave_sp_action_unpack_value(action);
	struct input_dev *idev = pb->idev;

	if (event == RAVE_SP_EVNT_BUTTON_PRESS) {
		input_report_key(idev, KEY_POWER, value);
		input_sync(idev);

		return NOTIFY_STOP;
	}

	return NOTIFY_DONE;
}

static int rave_sp_pwrbutton_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rave_sp_power_button *pb;
	struct input_dev *idev;
	int error;

	pb = devm_kzalloc(dev, sizeof(*pb), GFP_KERNEL);
	if (!pb)
		return -ENOMEM;

	idev = devm_input_allocate_device(dev);
	if (!idev)
		return -ENOMEM;

	idev->name = pdev->name;

	input_set_capability(idev, EV_KEY, KEY_POWER);

	error = input_register_device(idev);
	if (error)
		return error;

	pb->idev = idev;
	pb->nb.notifier_call = rave_sp_power_button_event;
	pb->nb.priority = 128;

	error = devm_rave_sp_register_event_notifier(dev, &pb->nb);
	if (error)
		return error;

	return 0;
}

static const struct of_device_id rave_sp_pwrbutton_of_match[] = {
	{ .compatible = "zii,rave-sp-pwrbutton" },
	{}
};

static struct platform_driver rave_sp_pwrbutton_driver = {
	.probe = rave_sp_pwrbutton_probe,
	.driver	= {
		.name = KBUILD_MODNAME,
		.of_match_table = rave_sp_pwrbutton_of_match,
	},
};
module_platform_driver(rave_sp_pwrbutton_driver);

MODULE_DEVICE_TABLE(of, rave_sp_pwrbutton_of_match);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrey Vostrikov <andrey.vostrikov@cogentembedded.com>");
MODULE_AUTHOR("Nikita Yushchenko <nikita.yoush@cogentembedded.com>");
MODULE_AUTHOR("Andrey Smirnov <andrew.smirnov@gmail.com>");
MODULE_DESCRIPTION("RAVE SP Power Button driver");
