// SPDX-License-Identifier: GPL-2.0-only

/*
 * Driver for input events on QNAP-MCUs
 *
 * Copyright (C) 2024 Heiko Stuebner <heiko@sntech.de>
 */

#include <linux/input.h>
#include <linux/mfd/qnap-mcu.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <uapi/linux/input-event-codes.h>

/*
 * The power-key needs to be pressed for a while to create an event,
 * so there is no use for overly frequent polling.
 */
#define POLL_INTERVAL		500

struct qnap_mcu_input_dev {
	struct input_dev *input;
	struct qnap_mcu *mcu;
	struct device *dev;

	struct work_struct beep_work;
	int beep_type;
};

static void qnap_mcu_input_poll(struct input_dev *input)
{
	struct qnap_mcu_input_dev *idev = input_get_drvdata(input);
	static const u8 cmd[] = { '@', 'C', 'V' };
	u8 reply[4];
	int state, ret;

	/* poll the power button */
	ret = qnap_mcu_exec(idev->mcu, cmd, sizeof(cmd), reply, sizeof(reply));
	if (ret)
		return;

	/* First bytes must mirror the sent command */
	if (memcmp(cmd, reply, sizeof(cmd))) {
		dev_err(idev->dev, "malformed data received\n");
		return;
	}

	state = reply[3] - 0x30;
	input_event(input, EV_KEY, KEY_POWER, state);
	input_sync(input);
}

static void qnap_mcu_input_beeper_work(struct work_struct *work)
{
	struct qnap_mcu_input_dev *idev =
		container_of(work, struct qnap_mcu_input_dev, beep_work);
	const u8 cmd[] = { '@', 'C', (idev->beep_type == SND_TONE) ? '3' : '2' };

	qnap_mcu_exec_with_ack(idev->mcu, cmd, sizeof(cmd));
}

static int qnap_mcu_input_event(struct input_dev *input, unsigned int type,
				unsigned int code, int value)
{
	struct qnap_mcu_input_dev *idev = input_get_drvdata(input);

	if (type != EV_SND || (code != SND_BELL && code != SND_TONE))
		return -EOPNOTSUPP;

	if (value < 0)
		return -EINVAL;

	/* beep runtime is determined by the MCU */
	if (value == 0)
		return 0;

	/* Schedule work to actually turn the beeper on */
	idev->beep_type = code;
	schedule_work(&idev->beep_work);

	return 0;
}

static void qnap_mcu_input_close(struct input_dev *input)
{
	struct qnap_mcu_input_dev *idev = input_get_drvdata(input);

	cancel_work_sync(&idev->beep_work);
}

static int qnap_mcu_input_probe(struct platform_device *pdev)
{
	struct qnap_mcu *mcu = dev_get_drvdata(pdev->dev.parent);
	struct qnap_mcu_input_dev *idev;
	struct device *dev = &pdev->dev;
	struct input_dev *input;
	int ret;

	idev = devm_kzalloc(dev, sizeof(*idev), GFP_KERNEL);
	if (!idev)
		return -ENOMEM;

	input = devm_input_allocate_device(dev);
	if (!input)
		return dev_err_probe(dev, -ENOMEM, "no memory for input device\n");

	idev->input = input;
	idev->dev = dev;
	idev->mcu = mcu;

	input_set_drvdata(input, idev);

	input->name		= "qnap-mcu";
	input->phys		= "qnap-mcu-input/input0";
	input->id.bustype	= BUS_HOST;
	input->id.vendor	= 0x0001;
	input->id.product	= 0x0001;
	input->id.version	= 0x0100;
	input->event		= qnap_mcu_input_event;
	input->close		= qnap_mcu_input_close;

	input_set_capability(input, EV_KEY, KEY_POWER);
	input_set_capability(input, EV_SND, SND_BELL);
	input_set_capability(input, EV_SND, SND_TONE);

	INIT_WORK(&idev->beep_work, qnap_mcu_input_beeper_work);

	ret = input_setup_polling(input, qnap_mcu_input_poll);
	if (ret)
		return dev_err_probe(dev, ret, "unable to set up polling\n");

	input_set_poll_interval(input, POLL_INTERVAL);

	ret = input_register_device(input);
	if (ret)
		return dev_err_probe(dev, ret, "unable to register input device\n");

	return 0;
}

static struct platform_driver qnap_mcu_input_driver = {
	.probe = qnap_mcu_input_probe,
	.driver = {
		.name = "qnap-mcu-input",
	},
};
module_platform_driver(qnap_mcu_input_driver);

MODULE_ALIAS("platform:qnap-mcu-input");
MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("QNAP MCU input driver");
MODULE_LICENSE("GPL");
