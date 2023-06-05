// SPDX-License-Identifier: GPL-2.0+
/*
 * Azoteq IQS620A/621/622/624/625 Keys and Switches
 *
 * Copyright (C) 2019 Jeff LaBundy <jeff@labundy.com>
 */

#include <linux/device.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/mfd/iqs62x.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/slab.h>

enum {
	IQS62X_SW_HALL_N,
	IQS62X_SW_HALL_S,
};

static const char * const iqs62x_switch_names[] = {
	[IQS62X_SW_HALL_N] = "hall-switch-north",
	[IQS62X_SW_HALL_S] = "hall-switch-south",
};

struct iqs62x_switch_desc {
	enum iqs62x_event_flag flag;
	unsigned int code;
	bool enabled;
};

struct iqs62x_keys_private {
	struct iqs62x_core *iqs62x;
	struct input_dev *input;
	struct notifier_block notifier;
	struct iqs62x_switch_desc switches[ARRAY_SIZE(iqs62x_switch_names)];
	unsigned int keycode[IQS62X_NUM_KEYS];
	unsigned int keycodemax;
	u8 interval;
};

static int iqs62x_keys_parse_prop(struct platform_device *pdev,
				  struct iqs62x_keys_private *iqs62x_keys)
{
	struct fwnode_handle *child;
	unsigned int val;
	int ret, i;

	ret = device_property_count_u32(&pdev->dev, "linux,keycodes");
	if (ret > IQS62X_NUM_KEYS) {
		dev_err(&pdev->dev, "Too many keycodes present\n");
		return -EINVAL;
	} else if (ret < 0) {
		dev_err(&pdev->dev, "Failed to count keycodes: %d\n", ret);
		return ret;
	}
	iqs62x_keys->keycodemax = ret;

	ret = device_property_read_u32_array(&pdev->dev, "linux,keycodes",
					     iqs62x_keys->keycode,
					     iqs62x_keys->keycodemax);
	if (ret) {
		dev_err(&pdev->dev, "Failed to read keycodes: %d\n", ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(iqs62x_keys->switches); i++) {
		child = device_get_named_child_node(&pdev->dev,
						    iqs62x_switch_names[i]);
		if (!child)
			continue;

		ret = fwnode_property_read_u32(child, "linux,code", &val);
		if (ret) {
			dev_err(&pdev->dev, "Failed to read switch code: %d\n",
				ret);
			fwnode_handle_put(child);
			return ret;
		}
		iqs62x_keys->switches[i].code = val;
		iqs62x_keys->switches[i].enabled = true;

		if (fwnode_property_present(child, "azoteq,use-prox"))
			iqs62x_keys->switches[i].flag = (i == IQS62X_SW_HALL_N ?
							 IQS62X_EVENT_HALL_N_P :
							 IQS62X_EVENT_HALL_S_P);
		else
			iqs62x_keys->switches[i].flag = (i == IQS62X_SW_HALL_N ?
							 IQS62X_EVENT_HALL_N_T :
							 IQS62X_EVENT_HALL_S_T);

		fwnode_handle_put(child);
	}

	return 0;
}

static int iqs62x_keys_init(struct iqs62x_keys_private *iqs62x_keys)
{
	struct iqs62x_core *iqs62x = iqs62x_keys->iqs62x;
	enum iqs62x_event_flag flag;
	unsigned int event_reg, val;
	unsigned int event_mask = 0;
	int ret, i;

	switch (iqs62x->dev_desc->prod_num) {
	case IQS620_PROD_NUM:
	case IQS621_PROD_NUM:
	case IQS622_PROD_NUM:
		event_reg = IQS620_GLBL_EVENT_MASK;

		/*
		 * Discreet button, hysteresis and SAR UI flags represent keys
		 * and are unmasked if mapped to a valid keycode.
		 */
		for (i = 0; i < iqs62x_keys->keycodemax; i++) {
			if (iqs62x_keys->keycode[i] == KEY_RESERVED)
				continue;

			if (iqs62x_events[i].reg == IQS62X_EVENT_PROX)
				event_mask |= iqs62x->dev_desc->prox_mask;
			else if (iqs62x_events[i].reg == IQS62X_EVENT_HYST)
				event_mask |= (iqs62x->dev_desc->hyst_mask |
					       iqs62x->dev_desc->sar_mask);
		}

		ret = regmap_read(iqs62x->regmap, iqs62x->dev_desc->hall_flags,
				  &val);
		if (ret)
			return ret;

		/*
		 * Hall UI flags represent switches and are unmasked if their
		 * corresponding child nodes are present.
		 */
		for (i = 0; i < ARRAY_SIZE(iqs62x_keys->switches); i++) {
			if (!(iqs62x_keys->switches[i].enabled))
				continue;

			flag = iqs62x_keys->switches[i].flag;

			if (iqs62x_events[flag].reg != IQS62X_EVENT_HALL)
				continue;

			event_mask |= iqs62x->dev_desc->hall_mask;

			input_report_switch(iqs62x_keys->input,
					    iqs62x_keys->switches[i].code,
					    (val & iqs62x_events[flag].mask) ==
					    iqs62x_events[flag].val);
		}

		input_sync(iqs62x_keys->input);
		break;

	case IQS624_PROD_NUM:
		event_reg = IQS624_HALL_UI;

		/*
		 * Interval change events represent keys and are unmasked if
		 * either wheel movement flag is mapped to a valid keycode.
		 */
		if (iqs62x_keys->keycode[IQS62X_EVENT_WHEEL_UP] != KEY_RESERVED)
			event_mask |= IQS624_HALL_UI_INT_EVENT;

		if (iqs62x_keys->keycode[IQS62X_EVENT_WHEEL_DN] != KEY_RESERVED)
			event_mask |= IQS624_HALL_UI_INT_EVENT;

		ret = regmap_read(iqs62x->regmap, iqs62x->dev_desc->interval,
				  &val);
		if (ret)
			return ret;

		iqs62x_keys->interval = val;
		break;

	default:
		return 0;
	}

	return regmap_update_bits(iqs62x->regmap, event_reg, event_mask, 0);
}

static int iqs62x_keys_notifier(struct notifier_block *notifier,
				unsigned long event_flags, void *context)
{
	struct iqs62x_event_data *event_data = context;
	struct iqs62x_keys_private *iqs62x_keys;
	int ret, i;

	iqs62x_keys = container_of(notifier, struct iqs62x_keys_private,
				   notifier);

	if (event_flags & BIT(IQS62X_EVENT_SYS_RESET)) {
		ret = iqs62x_keys_init(iqs62x_keys);
		if (ret) {
			dev_err(iqs62x_keys->input->dev.parent,
				"Failed to re-initialize device: %d\n", ret);
			return NOTIFY_BAD;
		}

		return NOTIFY_OK;
	}

	for (i = 0; i < iqs62x_keys->keycodemax; i++) {
		if (iqs62x_events[i].reg == IQS62X_EVENT_WHEEL &&
		    event_data->interval == iqs62x_keys->interval)
			continue;

		input_report_key(iqs62x_keys->input, iqs62x_keys->keycode[i],
				 event_flags & BIT(i));
	}

	for (i = 0; i < ARRAY_SIZE(iqs62x_keys->switches); i++)
		if (iqs62x_keys->switches[i].enabled)
			input_report_switch(iqs62x_keys->input,
					    iqs62x_keys->switches[i].code,
					    event_flags &
					    BIT(iqs62x_keys->switches[i].flag));

	input_sync(iqs62x_keys->input);

	if (event_data->interval == iqs62x_keys->interval)
		return NOTIFY_OK;

	/*
	 * Each frame contains at most one wheel event (up or down), in which
	 * case a complementary release cycle is emulated.
	 */
	if (event_flags & BIT(IQS62X_EVENT_WHEEL_UP)) {
		input_report_key(iqs62x_keys->input,
				 iqs62x_keys->keycode[IQS62X_EVENT_WHEEL_UP],
				 0);
		input_sync(iqs62x_keys->input);
	} else if (event_flags & BIT(IQS62X_EVENT_WHEEL_DN)) {
		input_report_key(iqs62x_keys->input,
				 iqs62x_keys->keycode[IQS62X_EVENT_WHEEL_DN],
				 0);
		input_sync(iqs62x_keys->input);
	}

	iqs62x_keys->interval = event_data->interval;

	return NOTIFY_OK;
}

static int iqs62x_keys_probe(struct platform_device *pdev)
{
	struct iqs62x_core *iqs62x = dev_get_drvdata(pdev->dev.parent);
	struct iqs62x_keys_private *iqs62x_keys;
	struct input_dev *input;
	int ret, i;

	iqs62x_keys = devm_kzalloc(&pdev->dev, sizeof(*iqs62x_keys),
				   GFP_KERNEL);
	if (!iqs62x_keys)
		return -ENOMEM;

	platform_set_drvdata(pdev, iqs62x_keys);

	ret = iqs62x_keys_parse_prop(pdev, iqs62x_keys);
	if (ret)
		return ret;

	input = devm_input_allocate_device(&pdev->dev);
	if (!input)
		return -ENOMEM;

	input->keycodemax = iqs62x_keys->keycodemax;
	input->keycode = iqs62x_keys->keycode;
	input->keycodesize = sizeof(*iqs62x_keys->keycode);

	input->name = iqs62x->dev_desc->dev_name;
	input->id.bustype = BUS_I2C;

	for (i = 0; i < iqs62x_keys->keycodemax; i++)
		if (iqs62x_keys->keycode[i] != KEY_RESERVED)
			input_set_capability(input, EV_KEY,
					     iqs62x_keys->keycode[i]);

	for (i = 0; i < ARRAY_SIZE(iqs62x_keys->switches); i++)
		if (iqs62x_keys->switches[i].enabled)
			input_set_capability(input, EV_SW,
					     iqs62x_keys->switches[i].code);

	iqs62x_keys->iqs62x = iqs62x;
	iqs62x_keys->input = input;

	ret = iqs62x_keys_init(iqs62x_keys);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize device: %d\n", ret);
		return ret;
	}

	ret = input_register_device(iqs62x_keys->input);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register device: %d\n", ret);
		return ret;
	}

	iqs62x_keys->notifier.notifier_call = iqs62x_keys_notifier;
	ret = blocking_notifier_chain_register(&iqs62x_keys->iqs62x->nh,
					       &iqs62x_keys->notifier);
	if (ret)
		dev_err(&pdev->dev, "Failed to register notifier: %d\n", ret);

	return ret;
}

static int iqs62x_keys_remove(struct platform_device *pdev)
{
	struct iqs62x_keys_private *iqs62x_keys = platform_get_drvdata(pdev);
	int ret;

	ret = blocking_notifier_chain_unregister(&iqs62x_keys->iqs62x->nh,
						 &iqs62x_keys->notifier);
	if (ret)
		dev_err(&pdev->dev, "Failed to unregister notifier: %d\n", ret);

	return 0;
}

static struct platform_driver iqs62x_keys_platform_driver = {
	.driver = {
		.name = "iqs62x-keys",
	},
	.probe = iqs62x_keys_probe,
	.remove = iqs62x_keys_remove,
};
module_platform_driver(iqs62x_keys_platform_driver);

MODULE_AUTHOR("Jeff LaBundy <jeff@labundy.com>");
MODULE_DESCRIPTION("Azoteq IQS620A/621/622/624/625 Keys and Switches");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:iqs62x-keys");
