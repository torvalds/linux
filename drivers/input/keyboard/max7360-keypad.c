// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Bootlin
 *
 * Author: Mathieu Dubois-Briand <mathieu.dubois-briand@bootlin.com>
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/dev_printk.h>
#include <linux/device/devres.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/interrupt.h>
#include <linux/mfd/max7360.h>
#include <linux/mod_devicetable.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/regmap.h>

struct max7360_keypad {
	struct input_dev *input;
	unsigned int rows;
	unsigned int cols;
	unsigned int debounce_ms;
	int irq;
	struct regmap *regmap;
	unsigned short keycodes[MAX7360_MAX_KEY_ROWS * MAX7360_MAX_KEY_COLS];
};

static irqreturn_t max7360_keypad_irq(int irq, void *data)
{
	struct max7360_keypad *max7360_keypad = data;
	struct device *dev = max7360_keypad->input->dev.parent;
	unsigned int val;
	unsigned int row, col;
	unsigned int release;
	unsigned int code;
	int error;

	error = regmap_read(max7360_keypad->regmap, MAX7360_REG_KEYFIFO, &val);
	if (error) {
		dev_err(dev, "Failed to read MAX7360 FIFO");
		return IRQ_NONE;
	}

	/* FIFO overflow: ignore it and get next event. */
	if (val == MAX7360_FIFO_OVERFLOW) {
		dev_warn(dev, "max7360 FIFO overflow");
		error = regmap_read_poll_timeout(max7360_keypad->regmap, MAX7360_REG_KEYFIFO,
						 val, val != MAX7360_FIFO_OVERFLOW, 0, 1000);
		if (error) {
			dev_err(dev, "Failed to empty MAX7360 FIFO");
			return IRQ_NONE;
		}
	}

	if (val == MAX7360_FIFO_EMPTY) {
		dev_dbg(dev, "Got a spurious interrupt");

		return IRQ_NONE;
	}

	row = FIELD_GET(MAX7360_FIFO_ROW, val);
	col = FIELD_GET(MAX7360_FIFO_COL, val);
	release = val & MAX7360_FIFO_RELEASE;

	code = MATRIX_SCAN_CODE(row, col, get_count_order(max7360_keypad->cols));

	dev_dbg(dev, "key[%d:%d] %s\n", row, col, release ? "release" : "press");

	input_event(max7360_keypad->input, EV_MSC, MSC_SCAN, code);
	input_report_key(max7360_keypad->input, max7360_keypad->keycodes[code], !release);
	input_sync(max7360_keypad->input);

	return IRQ_HANDLED;
}

static int max7360_keypad_open(struct input_dev *pdev)
{
	struct max7360_keypad *max7360_keypad = input_get_drvdata(pdev);
	struct device *dev = max7360_keypad->input->dev.parent;
	int error;

	/* Somebody is using the device: get out of sleep. */
	error = regmap_write_bits(max7360_keypad->regmap, MAX7360_REG_CONFIG,
				  MAX7360_CFG_SLEEP, MAX7360_CFG_SLEEP);
	if (error)
		dev_err(dev, "Failed to write max7360 configuration: %d\n", error);

	return error;
}

static void max7360_keypad_close(struct input_dev *pdev)
{
	struct max7360_keypad *max7360_keypad = input_get_drvdata(pdev);
	struct device *dev = max7360_keypad->input->dev.parent;
	int error;

	/* Nobody is using the device anymore: go to sleep. */
	error = regmap_write_bits(max7360_keypad->regmap, MAX7360_REG_CONFIG, MAX7360_CFG_SLEEP, 0);
	if (error)
		dev_err(dev, "Failed to write max7360 configuration: %d\n", error);
}

static int max7360_keypad_hw_init(struct max7360_keypad *max7360_keypad)
{
	struct device *dev = max7360_keypad->input->dev.parent;
	unsigned int val;
	int error;

	val = max7360_keypad->debounce_ms - MAX7360_DEBOUNCE_MIN;
	error = regmap_write_bits(max7360_keypad->regmap, MAX7360_REG_DEBOUNCE,
				  MAX7360_DEBOUNCE,
				  FIELD_PREP(MAX7360_DEBOUNCE, val));
	if (error)
		return dev_err_probe(dev, error,
				     "Failed to write max7360 debounce configuration\n");

	error = regmap_write_bits(max7360_keypad->regmap, MAX7360_REG_INTERRUPT,
				  MAX7360_INTERRUPT_TIME_MASK,
				  FIELD_PREP(MAX7360_INTERRUPT_TIME_MASK, 1));
	if (error)
		return dev_err_probe(dev, error,
				     "Failed to write max7360 keypad interrupt configuration\n");

	return 0;
}

static int max7360_keypad_build_keymap(struct max7360_keypad *max7360_keypad)
{
	struct input_dev *input_dev = max7360_keypad->input;
	struct device *dev = input_dev->dev.parent->parent;
	struct matrix_keymap_data keymap_data;
	const char *propname = "linux,keymap";
	unsigned int max_keys;
	int error;
	int size;

	size = device_property_count_u32(dev, propname);
	if (size <= 0) {
		dev_err(dev, "missing or malformed property %s: %d\n", propname, size);
		return size < 0 ? size : -EINVAL;
	}

	max_keys = max7360_keypad->cols * max7360_keypad->rows;
	if (size > max_keys) {
		dev_err(dev, "%s size overflow (%d vs max %u)\n", propname, size, max_keys);
		return -EINVAL;
	}

	u32 *keys __free(kfree) = kmalloc_array(size, sizeof(*keys), GFP_KERNEL);
	if (!keys)
		return -ENOMEM;

	error = device_property_read_u32_array(dev, propname, keys, size);
	if (error) {
		dev_err(dev, "failed to read %s property: %d\n", propname, error);
		return error;
	}

	keymap_data.keymap = keys;
	keymap_data.keymap_size = size;
	error = matrix_keypad_build_keymap(&keymap_data, NULL,
					   max7360_keypad->rows, max7360_keypad->cols,
					   max7360_keypad->keycodes, max7360_keypad->input);
	if (error)
		return error;

	return 0;
}

static int max7360_keypad_parse_fw(struct device *dev,
				   struct max7360_keypad *max7360_keypad,
				   bool *autorepeat)
{
	int error;

	error = matrix_keypad_parse_properties(dev->parent, &max7360_keypad->rows,
					       &max7360_keypad->cols);
	if (error)
		return error;

	if (!max7360_keypad->rows || !max7360_keypad->cols ||
	    max7360_keypad->rows > MAX7360_MAX_KEY_ROWS ||
	    max7360_keypad->cols > MAX7360_MAX_KEY_COLS) {
		dev_err(dev, "Invalid number of columns or rows (%ux%u)\n",
			max7360_keypad->cols, max7360_keypad->rows);
		return -EINVAL;
	}

	*autorepeat = device_property_read_bool(dev->parent, "autorepeat");

	max7360_keypad->debounce_ms = MAX7360_DEBOUNCE_MIN;
	error = device_property_read_u32(dev->parent, "keypad-debounce-delay-ms",
					 &max7360_keypad->debounce_ms);
	if (error == -EINVAL) {
		dev_info(dev, "Using default keypad-debounce-delay-ms: %u\n",
			 max7360_keypad->debounce_ms);
	} else if (error < 0) {
		dev_err(dev, "Failed to read keypad-debounce-delay-ms property\n");
		return error;
	}

	if (!in_range(max7360_keypad->debounce_ms, MAX7360_DEBOUNCE_MIN,
		      MAX7360_DEBOUNCE_MAX - MAX7360_DEBOUNCE_MIN + 1)) {
		dev_err(dev, "Invalid keypad-debounce-delay-ms: %u, should be between %u and %u.\n",
			max7360_keypad->debounce_ms, MAX7360_DEBOUNCE_MIN, MAX7360_DEBOUNCE_MAX);
		return -EINVAL;
	}

	return 0;
}

static int max7360_keypad_probe(struct platform_device *pdev)
{
	struct max7360_keypad *max7360_keypad;
	struct device *dev = &pdev->dev;
	struct input_dev *input;
	struct regmap *regmap;
	bool autorepeat;
	int error;
	int irq;

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap)
		return dev_err_probe(dev, -ENODEV, "Could not get parent regmap\n");

	irq = fwnode_irq_get_byname(dev_fwnode(dev->parent), "intk");
	if (irq < 0)
		return dev_err_probe(dev, irq, "Failed to get IRQ\n");

	max7360_keypad = devm_kzalloc(dev, sizeof(*max7360_keypad), GFP_KERNEL);
	if (!max7360_keypad)
		return -ENOMEM;

	max7360_keypad->regmap = regmap;

	error = max7360_keypad_parse_fw(dev, max7360_keypad, &autorepeat);
	if (error)
		return error;

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	max7360_keypad->input = input;

	input->id.bustype = BUS_I2C;
	input->name = pdev->name;
	input->open = max7360_keypad_open;
	input->close = max7360_keypad_close;

	error = max7360_keypad_build_keymap(max7360_keypad);
	if (error)
		return dev_err_probe(dev, error, "Failed to build keymap\n");

	input_set_capability(input, EV_MSC, MSC_SCAN);
	if (autorepeat)
		__set_bit(EV_REP, input->evbit);

	input_set_drvdata(input, max7360_keypad);

	error = devm_request_threaded_irq(dev, irq, NULL, max7360_keypad_irq,
					  IRQF_ONESHOT,
					  "max7360-keypad", max7360_keypad);
	if (error)
		return dev_err_probe(dev, error, "Failed to register interrupt\n");

	error = input_register_device(input);
	if (error)
		return dev_err_probe(dev, error, "Could not register input device\n");

	error = max7360_keypad_hw_init(max7360_keypad);
	if (error)
		return dev_err_probe(dev, error, "Failed to initialize max7360 keypad\n");

	device_init_wakeup(dev, true);
	error = dev_pm_set_wake_irq(dev, irq);
	if (error)
		dev_warn(dev, "Failed to set up wakeup irq: %d\n", error);

	return 0;
}

static void max7360_keypad_remove(struct platform_device *pdev)
{
	dev_pm_clear_wake_irq(&pdev->dev);
	device_init_wakeup(&pdev->dev, false);
}

static struct platform_driver max7360_keypad_driver = {
	.driver = {
		.name	= "max7360-keypad",
	},
	.probe		= max7360_keypad_probe,
	.remove		= max7360_keypad_remove,
};
module_platform_driver(max7360_keypad_driver);

MODULE_DESCRIPTION("MAX7360 Keypad driver");
MODULE_AUTHOR("Mathieu Dubois-Briand <mathieu.dubois-briand@bootlin.com>");
MODULE_LICENSE("GPL");
