// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 MediaTek Inc.
 * Author Fengping Yu <fengping.yu@mediatek.com>
 */
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/input/matrix_keypad.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define MTK_KPD_NAME		"mt6779-keypad"
#define MTK_KPD_MEM		0x0004
#define MTK_KPD_DEBOUNCE	0x0018
#define MTK_KPD_DEBOUNCE_MASK	GENMASK(13, 0)
#define MTK_KPD_DEBOUNCE_MAX_MS	256
#define MTK_KPD_NUM_MEMS	5
#define MTK_KPD_NUM_BITS	136	/* 4*32+8 MEM5 only use 8 BITS */

struct mt6779_keypad {
	struct regmap *regmap;
	struct input_dev *input_dev;
	struct clk *clk;
	u32 n_rows;
	u32 n_cols;
	DECLARE_BITMAP(keymap_state, MTK_KPD_NUM_BITS);
};

static const struct regmap_config mt6779_keypad_regmap_cfg = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = sizeof(u32),
	.max_register = 36,
};

static irqreturn_t mt6779_keypad_irq_handler(int irq, void *dev_id)
{
	struct mt6779_keypad *keypad = dev_id;
	const unsigned short *keycode = keypad->input_dev->keycode;
	DECLARE_BITMAP(new_state, MTK_KPD_NUM_BITS);
	DECLARE_BITMAP(change, MTK_KPD_NUM_BITS);
	unsigned int bit_nr, key;
	unsigned int row, col;
	unsigned int scancode;
	unsigned int row_shift = get_count_order(keypad->n_cols);
	bool pressed;

	regmap_bulk_read(keypad->regmap, MTK_KPD_MEM,
			 new_state, MTK_KPD_NUM_MEMS);

	bitmap_xor(change, new_state, keypad->keymap_state, MTK_KPD_NUM_BITS);

	for_each_set_bit(bit_nr, change, MTK_KPD_NUM_BITS) {
		/*
		 * Registers are 32bits, but only bits [15:0] are used to
		 * indicate key status.
		 */
		if (bit_nr % 32 >= 16)
			continue;

		key = bit_nr / 32 * 16 + bit_nr % 32;
		row = key / 9;
		col = key % 9;

		scancode = MATRIX_SCAN_CODE(row, col, row_shift);
		/* 1: not pressed, 0: pressed */
		pressed = !test_bit(bit_nr, new_state);
		dev_dbg(&keypad->input_dev->dev, "%s",
			pressed ? "pressed" : "released");

		input_event(keypad->input_dev, EV_MSC, MSC_SCAN, scancode);
		input_report_key(keypad->input_dev, keycode[scancode], pressed);
		input_sync(keypad->input_dev);

		dev_dbg(&keypad->input_dev->dev,
			"report Linux keycode = %d\n", keycode[scancode]);
	}

	bitmap_copy(keypad->keymap_state, new_state, MTK_KPD_NUM_BITS);

	return IRQ_HANDLED;
}

static void mt6779_keypad_clk_disable(void *data)
{
	clk_disable_unprepare(data);
}

static int mt6779_keypad_pdrv_probe(struct platform_device *pdev)
{
	struct mt6779_keypad *keypad;
	void __iomem *base;
	int irq;
	u32 debounce;
	bool wakeup;
	int error;

	keypad = devm_kzalloc(&pdev->dev, sizeof(*keypad), GFP_KERNEL);
	if (!keypad)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	keypad->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					       &mt6779_keypad_regmap_cfg);
	if (IS_ERR(keypad->regmap)) {
		dev_err(&pdev->dev,
			"regmap init failed:%pe\n", keypad->regmap);
		return PTR_ERR(keypad->regmap);
	}

	bitmap_fill(keypad->keymap_state, MTK_KPD_NUM_BITS);

	keypad->input_dev = devm_input_allocate_device(&pdev->dev);
	if (!keypad->input_dev) {
		dev_err(&pdev->dev, "Failed to allocate input dev\n");
		return -ENOMEM;
	}

	keypad->input_dev->name = MTK_KPD_NAME;
	keypad->input_dev->id.bustype = BUS_HOST;

	error = matrix_keypad_parse_properties(&pdev->dev, &keypad->n_rows,
					       &keypad->n_cols);
	if (error) {
		dev_err(&pdev->dev, "Failed to parse keypad params\n");
		return error;
	}

	if (device_property_read_u32(&pdev->dev, "debounce-delay-ms",
				     &debounce))
		debounce = 16;

	if (debounce > MTK_KPD_DEBOUNCE_MAX_MS) {
		dev_err(&pdev->dev,
			"Debounce time exceeds the maximum allowed time %dms\n",
			MTK_KPD_DEBOUNCE_MAX_MS);
		return -EINVAL;
	}

	wakeup = device_property_read_bool(&pdev->dev, "wakeup-source");

	dev_dbg(&pdev->dev, "n_row=%d n_col=%d debounce=%d\n",
		keypad->n_rows, keypad->n_cols, debounce);

	error = matrix_keypad_build_keymap(NULL, NULL,
					   keypad->n_rows, keypad->n_cols,
					   NULL, keypad->input_dev);
	if (error) {
		dev_err(&pdev->dev, "Failed to build keymap\n");
		return error;
	}

	input_set_capability(keypad->input_dev, EV_MSC, MSC_SCAN);

	regmap_write(keypad->regmap, MTK_KPD_DEBOUNCE,
		     (debounce * (1 << 5)) & MTK_KPD_DEBOUNCE_MASK);

	keypad->clk = devm_clk_get(&pdev->dev, "kpd");
	if (IS_ERR(keypad->clk))
		return PTR_ERR(keypad->clk);

	error = clk_prepare_enable(keypad->clk);
	if (error) {
		dev_err(&pdev->dev, "cannot prepare/enable keypad clock\n");
		return error;
	}

	error = devm_add_action_or_reset(&pdev->dev, mt6779_keypad_clk_disable,
					 keypad->clk);
	if (error)
		return error;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	error = devm_request_threaded_irq(&pdev->dev, irq,
					  NULL, mt6779_keypad_irq_handler,
					  IRQF_ONESHOT, MTK_KPD_NAME, keypad);
	if (error) {
		dev_err(&pdev->dev, "Failed to request IRQ#%d: %d\n",
			irq, error);
		return error;
	}

	error = input_register_device(keypad->input_dev);
	if (error) {
		dev_err(&pdev->dev, "Failed to register device\n");
		return error;
	}

	error = device_init_wakeup(&pdev->dev, wakeup);
	if (error)
		dev_warn(&pdev->dev, "device_init_wakeup() failed: %d\n",
			 error);

	return 0;
}

static const struct of_device_id mt6779_keypad_of_match[] = {
	{ .compatible = "mediatek,mt6779-keypad" },
	{ .compatible = "mediatek,mt6873-keypad" },
	{ /* sentinel */ }
};

static struct platform_driver mt6779_keypad_pdrv = {
	.probe = mt6779_keypad_pdrv_probe,
	.driver = {
		   .name = MTK_KPD_NAME,
		   .of_match_table = mt6779_keypad_of_match,
	},
};
module_platform_driver(mt6779_keypad_pdrv);

MODULE_AUTHOR("Mediatek Corporation");
MODULE_DESCRIPTION("MTK Keypad (KPD) Driver");
MODULE_LICENSE("GPL");
