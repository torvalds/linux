// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics Key Scanning driver
 *
 * Copyright (c) 2014 STMicroelectonics Ltd.
 * Author: Stuart Menefy <stuart.menefy@st.com>
 *
 * Based on sh_keysc.c, copyright 2008 Magnus Damm
 */

#include <linux/clk.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define ST_KEYSCAN_MAXKEYS 16

#define KEYSCAN_CONFIG_OFF		0x0
#define KEYSCAN_CONFIG_ENABLE		0x1
#define KEYSCAN_DEBOUNCE_TIME_OFF	0x4
#define KEYSCAN_MATRIX_STATE_OFF	0x8
#define KEYSCAN_MATRIX_DIM_OFF		0xc
#define KEYSCAN_MATRIX_DIM_X_SHIFT	0x0
#define KEYSCAN_MATRIX_DIM_Y_SHIFT	0x2

struct st_keyscan {
	void __iomem *base;
	int irq;
	struct clk *clk;
	struct input_dev *input_dev;
	unsigned long last_state;
	unsigned int n_rows;
	unsigned int n_cols;
	unsigned int debounce_us;
};

static irqreturn_t keyscan_isr(int irq, void *dev_id)
{
	struct st_keyscan *keypad = dev_id;
	unsigned short *keycode = keypad->input_dev->keycode;
	unsigned long state, change;
	int bit_nr;

	state = readl(keypad->base + KEYSCAN_MATRIX_STATE_OFF) & 0xffff;
	change = keypad->last_state ^ state;
	keypad->last_state = state;

	for_each_set_bit(bit_nr, &change, BITS_PER_LONG)
		input_report_key(keypad->input_dev,
				 keycode[bit_nr], state & BIT(bit_nr));

	input_sync(keypad->input_dev);

	return IRQ_HANDLED;
}

static int keyscan_start(struct st_keyscan *keypad)
{
	int error;

	error = clk_enable(keypad->clk);
	if (error)
		return error;

	writel(keypad->debounce_us * (clk_get_rate(keypad->clk) / 1000000),
	       keypad->base + KEYSCAN_DEBOUNCE_TIME_OFF);

	writel(((keypad->n_cols - 1) << KEYSCAN_MATRIX_DIM_X_SHIFT) |
	       ((keypad->n_rows - 1) << KEYSCAN_MATRIX_DIM_Y_SHIFT),
	       keypad->base + KEYSCAN_MATRIX_DIM_OFF);

	writel(KEYSCAN_CONFIG_ENABLE, keypad->base + KEYSCAN_CONFIG_OFF);

	return 0;
}

static void keyscan_stop(struct st_keyscan *keypad)
{
	writel(0, keypad->base + KEYSCAN_CONFIG_OFF);

	clk_disable(keypad->clk);
}

static int keyscan_open(struct input_dev *dev)
{
	struct st_keyscan *keypad = input_get_drvdata(dev);

	return keyscan_start(keypad);
}

static void keyscan_close(struct input_dev *dev)
{
	struct st_keyscan *keypad = input_get_drvdata(dev);

	keyscan_stop(keypad);
}

static int keypad_matrix_key_parse_dt(struct st_keyscan *keypad_data)
{
	struct device *dev = keypad_data->input_dev->dev.parent;
	struct device_node *np = dev->of_node;
	int error;

	error = matrix_keypad_parse_properties(dev, &keypad_data->n_rows,
					       &keypad_data->n_cols);
	if (error) {
		dev_err(dev, "failed to parse keypad params\n");
		return error;
	}

	of_property_read_u32(np, "st,debounce-us", &keypad_data->debounce_us);

	dev_dbg(dev, "n_rows=%d n_col=%d debounce=%d\n",
		keypad_data->n_rows, keypad_data->n_cols,
		keypad_data->debounce_us);

	return 0;
}

static int keyscan_probe(struct platform_device *pdev)
{
	struct st_keyscan *keypad_data;
	struct input_dev *input_dev;
	int error;

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "no DT data present\n");
		return -EINVAL;
	}

	keypad_data = devm_kzalloc(&pdev->dev, sizeof(*keypad_data),
				   GFP_KERNEL);
	if (!keypad_data)
		return -ENOMEM;

	input_dev = devm_input_allocate_device(&pdev->dev);
	if (!input_dev) {
		dev_err(&pdev->dev, "failed to allocate the input device\n");
		return -ENOMEM;
	}

	input_dev->name = pdev->name;
	input_dev->phys = "keyscan-keys/input0";
	input_dev->dev.parent = &pdev->dev;
	input_dev->open = keyscan_open;
	input_dev->close = keyscan_close;

	input_dev->id.bustype = BUS_HOST;

	keypad_data->input_dev = input_dev;

	error = keypad_matrix_key_parse_dt(keypad_data);
	if (error)
		return error;

	error = matrix_keypad_build_keymap(NULL, NULL,
					   keypad_data->n_rows,
					   keypad_data->n_cols,
					   NULL, input_dev);
	if (error) {
		dev_err(&pdev->dev, "failed to build keymap\n");
		return error;
	}

	input_set_drvdata(input_dev, keypad_data);

	keypad_data->base = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(keypad_data->base))
		return PTR_ERR(keypad_data->base);

	keypad_data->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(keypad_data->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		return PTR_ERR(keypad_data->clk);
	}

	error = clk_enable(keypad_data->clk);
	if (error) {
		dev_err(&pdev->dev, "failed to enable clock\n");
		return error;
	}

	keyscan_stop(keypad_data);

	keypad_data->irq = platform_get_irq(pdev, 0);
	if (keypad_data->irq < 0)
		return -EINVAL;

	error = devm_request_irq(&pdev->dev, keypad_data->irq, keyscan_isr, 0,
				 pdev->name, keypad_data);
	if (error) {
		dev_err(&pdev->dev, "failed to request IRQ\n");
		return error;
	}

	error = input_register_device(input_dev);
	if (error) {
		dev_err(&pdev->dev, "failed to register input device\n");
		return error;
	}

	platform_set_drvdata(pdev, keypad_data);

	device_set_wakeup_capable(&pdev->dev, 1);

	return 0;
}

static int keyscan_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct st_keyscan *keypad = platform_get_drvdata(pdev);
	struct input_dev *input = keypad->input_dev;

	mutex_lock(&input->mutex);

	if (device_may_wakeup(dev))
		enable_irq_wake(keypad->irq);
	else if (input_device_enabled(input))
		keyscan_stop(keypad);

	mutex_unlock(&input->mutex);
	return 0;
}

static int keyscan_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct st_keyscan *keypad = platform_get_drvdata(pdev);
	struct input_dev *input = keypad->input_dev;
	int retval = 0;

	mutex_lock(&input->mutex);

	if (device_may_wakeup(dev))
		disable_irq_wake(keypad->irq);
	else if (input_device_enabled(input))
		retval = keyscan_start(keypad);

	mutex_unlock(&input->mutex);
	return retval;
}

static DEFINE_SIMPLE_DEV_PM_OPS(keyscan_dev_pm_ops,
				keyscan_suspend, keyscan_resume);

static const struct of_device_id keyscan_of_match[] = {
	{ .compatible = "st,sti-keyscan" },
	{ },
};
MODULE_DEVICE_TABLE(of, keyscan_of_match);

static struct platform_driver keyscan_device_driver = {
	.probe		= keyscan_probe,
	.driver		= {
		.name	= "st-keyscan",
		.pm	= pm_sleep_ptr(&keyscan_dev_pm_ops),
		.of_match_table = of_match_ptr(keyscan_of_match),
	}
};

module_platform_driver(keyscan_device_driver);

MODULE_AUTHOR("Stuart Menefy <stuart.menefy@st.com>");
MODULE_DESCRIPTION("STMicroelectronics keyscan device driver");
MODULE_LICENSE("GPL");
