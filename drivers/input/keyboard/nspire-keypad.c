// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2013 Daniel Tang <tangrs@tangrs.id.au>
 */

#include <linux/input/matrix_keypad.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>

#define KEYPAD_SCAN_MODE	0x00
#define KEYPAD_CNTL		0x04
#define KEYPAD_INT		0x08
#define KEYPAD_INTMSK		0x0C

#define KEYPAD_DATA		0x10
#define KEYPAD_GPIO		0x30

#define KEYPAD_UNKNOWN_INT	0x40
#define KEYPAD_UNKNOWN_INT_STS	0x44

#define KEYPAD_BITMASK_COLS	11
#define KEYPAD_BITMASK_ROWS	8

struct nspire_keypad {
	void __iomem *reg_base;
	u32 int_mask;

	struct input_dev *input;
	struct clk *clk;

	struct matrix_keymap_data *keymap;
	int row_shift;

	/* Maximum delay estimated assuming 33MHz APB */
	u32 scan_interval;	/* In microseconds (~2000us max) */
	u32 row_delay;		/* In microseconds (~500us max) */

	u16 state[KEYPAD_BITMASK_ROWS];

	bool active_low;
};

static irqreturn_t nspire_keypad_irq(int irq, void *dev_id)
{
	struct nspire_keypad *keypad = dev_id;
	struct input_dev *input = keypad->input;
	unsigned short *keymap = input->keycode;
	unsigned int code;
	int row, col;
	u32 int_sts;
	u16 state[8];
	u16 bits, changed;

	int_sts = readl(keypad->reg_base + KEYPAD_INT) & keypad->int_mask;
	if (!int_sts)
		return IRQ_NONE;

	memcpy_fromio(state, keypad->reg_base + KEYPAD_DATA, sizeof(state));

	for (row = 0; row < KEYPAD_BITMASK_ROWS; row++) {
		bits = state[row];
		if (keypad->active_low)
			bits = ~bits;

		changed = bits ^ keypad->state[row];
		if (!changed)
			continue;

		keypad->state[row] = bits;

		for (col = 0; col < KEYPAD_BITMASK_COLS; col++) {
			if (!(changed & (1U << col)))
				continue;

			code = MATRIX_SCAN_CODE(row, col, keypad->row_shift);
			input_event(input, EV_MSC, MSC_SCAN, code);
			input_report_key(input, keymap[code],
					 bits & (1U << col));
		}
	}

	input_sync(input);

	writel(0x3, keypad->reg_base + KEYPAD_INT);

	return IRQ_HANDLED;
}

static int nspire_keypad_open(struct input_dev *input)
{
	struct nspire_keypad *keypad = input_get_drvdata(input);
	unsigned long val = 0, cycles_per_us, delay_cycles, row_delay_cycles;
	int error;

	error = clk_prepare_enable(keypad->clk);
	if (error)
		return error;

	cycles_per_us = (clk_get_rate(keypad->clk) / 1000000);
	if (cycles_per_us == 0)
		cycles_per_us = 1;

	delay_cycles = cycles_per_us * keypad->scan_interval;
	WARN_ON(delay_cycles >= (1 << 16)); /* Overflow */
	delay_cycles &= 0xffff;

	row_delay_cycles = cycles_per_us * keypad->row_delay;
	WARN_ON(row_delay_cycles >= (1 << 14)); /* Overflow */
	row_delay_cycles &= 0x3fff;

	val |= 3 << 0; /* Set scan mode to 3 (continuous scan) */
	val |= row_delay_cycles << 2; /* Delay between scanning each row */
	val |= delay_cycles << 16; /* Delay between scans */
	writel(val, keypad->reg_base + KEYPAD_SCAN_MODE);

	val = (KEYPAD_BITMASK_ROWS & 0xff) | (KEYPAD_BITMASK_COLS & 0xff)<<8;
	writel(val, keypad->reg_base + KEYPAD_CNTL);

	/* Enable interrupts */
	keypad->int_mask = 1 << 1;
	writel(keypad->int_mask, keypad->reg_base + KEYPAD_INTMSK);

	return 0;
}

static void nspire_keypad_close(struct input_dev *input)
{
	struct nspire_keypad *keypad = input_get_drvdata(input);

	/* Disable interrupts */
	writel(0, keypad->reg_base + KEYPAD_INTMSK);
	/* Acknowledge existing interrupts */
	writel(~0, keypad->reg_base + KEYPAD_INT);

	clk_disable_unprepare(keypad->clk);
}

static int nspire_keypad_probe(struct platform_device *pdev)
{
	const struct device_node *of_node = pdev->dev.of_node;
	struct nspire_keypad *keypad;
	struct input_dev *input;
	struct resource *res;
	int irq;
	int error;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -EINVAL;

	keypad = devm_kzalloc(&pdev->dev, sizeof(struct nspire_keypad),
			      GFP_KERNEL);
	if (!keypad) {
		dev_err(&pdev->dev, "failed to allocate keypad memory\n");
		return -ENOMEM;
	}

	keypad->row_shift = get_count_order(KEYPAD_BITMASK_COLS);

	error = of_property_read_u32(of_node, "scan-interval",
				     &keypad->scan_interval);
	if (error) {
		dev_err(&pdev->dev, "failed to get scan-interval\n");
		return error;
	}

	error = of_property_read_u32(of_node, "row-delay",
				     &keypad->row_delay);
	if (error) {
		dev_err(&pdev->dev, "failed to get row-delay\n");
		return error;
	}

	keypad->active_low = of_property_read_bool(of_node, "active-low");

	keypad->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(keypad->clk)) {
		dev_err(&pdev->dev, "unable to get clock\n");
		return PTR_ERR(keypad->clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	keypad->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(keypad->reg_base))
		return PTR_ERR(keypad->reg_base);

	keypad->input = input = devm_input_allocate_device(&pdev->dev);
	if (!input) {
		dev_err(&pdev->dev, "failed to allocate input device\n");
		return -ENOMEM;
	}

	error = clk_prepare_enable(keypad->clk);
	if (error) {
		dev_err(&pdev->dev, "failed to enable clock\n");
		return error;
	}

	/* Disable interrupts */
	writel(0, keypad->reg_base + KEYPAD_INTMSK);
	/* Acknowledge existing interrupts */
	writel(~0, keypad->reg_base + KEYPAD_INT);

	/* Disable GPIO interrupts to prevent hanging on touchpad */
	/* Possibly used to detect touchpad events */
	writel(0, keypad->reg_base + KEYPAD_UNKNOWN_INT);
	/* Acknowledge existing GPIO interrupts */
	writel(~0, keypad->reg_base + KEYPAD_UNKNOWN_INT_STS);

	clk_disable_unprepare(keypad->clk);

	input_set_drvdata(input, keypad);

	input->id.bustype = BUS_HOST;
	input->name = "nspire-keypad";
	input->open = nspire_keypad_open;
	input->close = nspire_keypad_close;

	__set_bit(EV_KEY, input->evbit);
	__set_bit(EV_REP, input->evbit);
	input_set_capability(input, EV_MSC, MSC_SCAN);

	error = matrix_keypad_build_keymap(NULL, NULL,
					   KEYPAD_BITMASK_ROWS,
					   KEYPAD_BITMASK_COLS,
					   NULL, input);
	if (error) {
		dev_err(&pdev->dev, "building keymap failed\n");
		return error;
	}

	error = devm_request_irq(&pdev->dev, irq, nspire_keypad_irq, 0,
				 "nspire_keypad", keypad);
	if (error) {
		dev_err(&pdev->dev, "allocate irq %d failed\n", irq);
		return error;
	}

	error = input_register_device(input);
	if (error) {
		dev_err(&pdev->dev,
			"unable to register input device: %d\n", error);
		return error;
	}

	dev_dbg(&pdev->dev,
		"TI-NSPIRE keypad at %pR (scan_interval=%uus, row_delay=%uus%s)\n",
		res, keypad->row_delay, keypad->scan_interval,
		keypad->active_low ? ", active_low" : "");

	return 0;
}

static const struct of_device_id nspire_keypad_dt_match[] = {
	{ .compatible = "ti,nspire-keypad" },
	{ },
};
MODULE_DEVICE_TABLE(of, nspire_keypad_dt_match);

static struct platform_driver nspire_keypad_driver = {
	.driver = {
		.name = "nspire-keypad",
		.of_match_table = nspire_keypad_dt_match,
	},
	.probe = nspire_keypad_probe,
};

module_platform_driver(nspire_keypad_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TI-NSPIRE Keypad Driver");
