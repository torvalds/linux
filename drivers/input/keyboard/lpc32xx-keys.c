// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NXP LPC32xx SoC Key Scan Interface
 *
 * Authors:
 *    Kevin Wells <kevin.wells@nxp.com>
 *    Roland Stigge <stigge@antcom.de>
 *
 * Copyright (C) 2010 NXP Semiconductors
 * Copyright (C) 2012 Roland Stigge
 *
 * This controller supports square key matrices from 1x1 up to 8x8
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/input/matrix_keypad.h>

#define DRV_NAME				"lpc32xx_keys"

/*
 * Key scanner register offsets
 */
#define LPC32XX_KS_DEB(x)			((x) + 0x00)
#define LPC32XX_KS_STATE_COND(x)		((x) + 0x04)
#define LPC32XX_KS_IRQ(x)			((x) + 0x08)
#define LPC32XX_KS_SCAN_CTL(x)			((x) + 0x0C)
#define LPC32XX_KS_FAST_TST(x)			((x) + 0x10)
#define LPC32XX_KS_MATRIX_DIM(x)		((x) + 0x14) /* 1..8 */
#define LPC32XX_KS_DATA(x, y)			((x) + 0x40 + ((y) << 2))

#define LPC32XX_KSCAN_DEB_NUM_DEB_PASS(n)	((n) & 0xFF)

#define LPC32XX_KSCAN_SCOND_IN_IDLE		0x0
#define LPC32XX_KSCAN_SCOND_IN_SCANONCE		0x1
#define LPC32XX_KSCAN_SCOND_IN_IRQGEN		0x2
#define LPC32XX_KSCAN_SCOND_IN_SCAN_MATRIX	0x3

#define LPC32XX_KSCAN_IRQ_PENDING_CLR		0x1

#define LPC32XX_KSCAN_SCTRL_SCAN_DELAY(n)	((n) & 0xFF)

#define LPC32XX_KSCAN_FTST_FORCESCANONCE	0x1
#define LPC32XX_KSCAN_FTST_USE32K_CLK		0x2

#define LPC32XX_KSCAN_MSEL_SELECT(n)		((n) & 0xF)

struct lpc32xx_kscan_drv {
	struct input_dev *input;
	struct clk *clk;
	void __iomem *kscan_base;
	unsigned int irq;

	u32 matrix_sz;		/* Size of matrix in XxY, ie. 3 = 3x3 */
	u32 deb_clks;		/* Debounce clocks (based on 32KHz clock) */
	u32 scan_delay;		/* Scan delay (based on 32KHz clock) */

	unsigned short *keymap;	/* Pointer to key map for the scan matrix */
	unsigned int row_shift;

	u8 lastkeystates[8];
};

static void lpc32xx_mod_states(struct lpc32xx_kscan_drv *kscandat, int col)
{
	struct input_dev *input = kscandat->input;
	unsigned row, changed, scancode, keycode;
	u8 key;

	key = readl(LPC32XX_KS_DATA(kscandat->kscan_base, col));
	changed = key ^ kscandat->lastkeystates[col];
	kscandat->lastkeystates[col] = key;

	for (row = 0; changed; row++, changed >>= 1) {
		if (changed & 1) {
			/* Key state changed, signal an event */
			scancode = MATRIX_SCAN_CODE(row, col,
						    kscandat->row_shift);
			keycode = kscandat->keymap[scancode];
			input_event(input, EV_MSC, MSC_SCAN, scancode);
			input_report_key(input, keycode, key & (1 << row));
		}
	}
}

static irqreturn_t lpc32xx_kscan_irq(int irq, void *dev_id)
{
	struct lpc32xx_kscan_drv *kscandat = dev_id;
	int i;

	for (i = 0; i < kscandat->matrix_sz; i++)
		lpc32xx_mod_states(kscandat, i);

	writel(1, LPC32XX_KS_IRQ(kscandat->kscan_base));

	input_sync(kscandat->input);

	return IRQ_HANDLED;
}

static int lpc32xx_kscan_open(struct input_dev *dev)
{
	struct lpc32xx_kscan_drv *kscandat = input_get_drvdata(dev);
	int error;

	error = clk_prepare_enable(kscandat->clk);
	if (error)
		return error;

	writel(1, LPC32XX_KS_IRQ(kscandat->kscan_base));

	return 0;
}

static void lpc32xx_kscan_close(struct input_dev *dev)
{
	struct lpc32xx_kscan_drv *kscandat = input_get_drvdata(dev);

	writel(1, LPC32XX_KS_IRQ(kscandat->kscan_base));
	clk_disable_unprepare(kscandat->clk);
}

static int lpc32xx_parse_dt(struct device *dev,
				      struct lpc32xx_kscan_drv *kscandat)
{
	struct device_node *np = dev->of_node;
	u32 rows = 0, columns = 0;
	int err;

	err = matrix_keypad_parse_properties(dev, &rows, &columns);
	if (err)
		return err;
	if (rows != columns) {
		dev_err(dev, "rows and columns must be equal!\n");
		return -EINVAL;
	}

	kscandat->matrix_sz = rows;
	kscandat->row_shift = get_count_order(columns);

	of_property_read_u32(np, "nxp,debounce-delay-ms", &kscandat->deb_clks);
	of_property_read_u32(np, "nxp,scan-delay-ms", &kscandat->scan_delay);
	if (!kscandat->deb_clks || !kscandat->scan_delay) {
		dev_err(dev, "debounce or scan delay not specified\n");
		return -EINVAL;
	}

	return 0;
}

static int lpc32xx_kscan_probe(struct platform_device *pdev)
{
	struct lpc32xx_kscan_drv *kscandat;
	struct input_dev *input;
	size_t keymap_size;
	int error;
	int irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -EINVAL;

	kscandat = devm_kzalloc(&pdev->dev, sizeof(*kscandat),
				GFP_KERNEL);
	if (!kscandat)
		return -ENOMEM;

	error = lpc32xx_parse_dt(&pdev->dev, kscandat);
	if (error) {
		dev_err(&pdev->dev, "failed to parse device tree\n");
		return error;
	}

	keymap_size = sizeof(kscandat->keymap[0]) *
				(kscandat->matrix_sz << kscandat->row_shift);
	kscandat->keymap = devm_kzalloc(&pdev->dev, keymap_size, GFP_KERNEL);
	if (!kscandat->keymap)
		return -ENOMEM;

	kscandat->input = input = devm_input_allocate_device(&pdev->dev);
	if (!input) {
		dev_err(&pdev->dev, "failed to allocate input device\n");
		return -ENOMEM;
	}

	/* Setup key input */
	input->name		= pdev->name;
	input->phys		= "lpc32xx/input0";
	input->id.vendor	= 0x0001;
	input->id.product	= 0x0001;
	input->id.version	= 0x0100;
	input->open		= lpc32xx_kscan_open;
	input->close		= lpc32xx_kscan_close;
	input->dev.parent	= &pdev->dev;

	input_set_capability(input, EV_MSC, MSC_SCAN);

	error = matrix_keypad_build_keymap(NULL, NULL,
					   kscandat->matrix_sz,
					   kscandat->matrix_sz,
					   kscandat->keymap, kscandat->input);
	if (error) {
		dev_err(&pdev->dev, "failed to build keymap\n");
		return error;
	}

	input_set_drvdata(kscandat->input, kscandat);

	kscandat->kscan_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(kscandat->kscan_base))
		return PTR_ERR(kscandat->kscan_base);

	/* Get the key scanner clock */
	kscandat->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(kscandat->clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(kscandat->clk);
	}

	/* Configure the key scanner */
	error = clk_prepare_enable(kscandat->clk);
	if (error)
		return error;

	writel(kscandat->deb_clks, LPC32XX_KS_DEB(kscandat->kscan_base));
	writel(kscandat->scan_delay, LPC32XX_KS_SCAN_CTL(kscandat->kscan_base));
	writel(LPC32XX_KSCAN_FTST_USE32K_CLK,
	       LPC32XX_KS_FAST_TST(kscandat->kscan_base));
	writel(kscandat->matrix_sz,
	       LPC32XX_KS_MATRIX_DIM(kscandat->kscan_base));
	writel(1, LPC32XX_KS_IRQ(kscandat->kscan_base));
	clk_disable_unprepare(kscandat->clk);

	error = devm_request_irq(&pdev->dev, irq, lpc32xx_kscan_irq, 0,
				 pdev->name, kscandat);
	if (error) {
		dev_err(&pdev->dev, "failed to request irq\n");
		return error;
	}

	error = input_register_device(kscandat->input);
	if (error) {
		dev_err(&pdev->dev, "failed to register input device\n");
		return error;
	}

	platform_set_drvdata(pdev, kscandat);

	return 0;
}

static int lpc32xx_kscan_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lpc32xx_kscan_drv *kscandat = platform_get_drvdata(pdev);
	struct input_dev *input = kscandat->input;

	mutex_lock(&input->mutex);

	if (input_device_enabled(input)) {
		/* Clear IRQ and disable clock */
		writel(1, LPC32XX_KS_IRQ(kscandat->kscan_base));
		clk_disable_unprepare(kscandat->clk);
	}

	mutex_unlock(&input->mutex);
	return 0;
}

static int lpc32xx_kscan_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lpc32xx_kscan_drv *kscandat = platform_get_drvdata(pdev);
	struct input_dev *input = kscandat->input;
	int retval = 0;

	mutex_lock(&input->mutex);

	if (input_device_enabled(input)) {
		/* Enable clock and clear IRQ */
		retval = clk_prepare_enable(kscandat->clk);
		if (retval == 0)
			writel(1, LPC32XX_KS_IRQ(kscandat->kscan_base));
	}

	mutex_unlock(&input->mutex);
	return retval;
}

static DEFINE_SIMPLE_DEV_PM_OPS(lpc32xx_kscan_pm_ops, lpc32xx_kscan_suspend,
				lpc32xx_kscan_resume);

static const struct of_device_id lpc32xx_kscan_match[] = {
	{ .compatible = "nxp,lpc3220-key" },
	{},
};
MODULE_DEVICE_TABLE(of, lpc32xx_kscan_match);

static struct platform_driver lpc32xx_kscan_driver = {
	.probe		= lpc32xx_kscan_probe,
	.driver		= {
		.name	= DRV_NAME,
		.pm	= pm_sleep_ptr(&lpc32xx_kscan_pm_ops),
		.of_match_table = lpc32xx_kscan_match,
	}
};

module_platform_driver(lpc32xx_kscan_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kevin Wells <kevin.wells@nxp.com>");
MODULE_AUTHOR("Roland Stigge <stigge@antcom.de>");
MODULE_DESCRIPTION("Key scanner driver for LPC32XX devices");
