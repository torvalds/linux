/*
 * Copyright (C) 2014 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/stddef.h>
#include <linux/types.h>

#define DEFAULT_CLK_HZ			31250
#define MAX_ROWS			8
#define MAX_COLS			8

/* Register/field definitions */
#define KPCR_OFFSET			0x00000080
#define KPCR_MODE			0x00000002
#define KPCR_MODE_SHIFT			1
#define KPCR_MODE_MASK			1
#define KPCR_ENABLE			0x00000001
#define KPCR_STATUSFILTERENABLE		0x00008000
#define KPCR_STATUSFILTERTYPE_SHIFT	12
#define KPCR_COLFILTERENABLE		0x00000800
#define KPCR_COLFILTERTYPE_SHIFT	8
#define KPCR_ROWWIDTH_SHIFT		20
#define KPCR_COLUMNWIDTH_SHIFT		16

#define KPIOR_OFFSET			0x00000084
#define KPIOR_ROWOCONTRL_SHIFT		24
#define KPIOR_ROWOCONTRL_MASK		0xFF000000
#define KPIOR_COLUMNOCONTRL_SHIFT	16
#define KPIOR_COLUMNOCONTRL_MASK	0x00FF0000
#define KPIOR_COLUMN_IO_DATA_SHIFT	0

#define KPEMR0_OFFSET			0x00000090
#define KPEMR1_OFFSET			0x00000094
#define KPEMR2_OFFSET			0x00000098
#define KPEMR3_OFFSET			0x0000009C
#define KPEMR_EDGETYPE_BOTH		3

#define KPSSR0_OFFSET			0x000000A0
#define KPSSR1_OFFSET			0x000000A4
#define KPSSRN_OFFSET(reg_n)		(KPSSR0_OFFSET + 4 * (reg_n))
#define KPIMR0_OFFSET			0x000000B0
#define KPIMR1_OFFSET			0x000000B4
#define KPICR0_OFFSET			0x000000B8
#define KPICR1_OFFSET			0x000000BC
#define KPICRN_OFFSET(reg_n)		(KPICR0_OFFSET + 4 * (reg_n))
#define KPISR0_OFFSET			0x000000C0
#define KPISR1_OFFSET			0x000000C4

#define KPCR_STATUSFILTERTYPE_MAX	7
#define KPCR_COLFILTERTYPE_MAX		7

/* Macros to determine the row/column from a bit that is set in SSR0/1. */
#define BIT_TO_ROW_SSRN(bit_nr, reg_n)	(((bit_nr) >> 3) + 4 * (reg_n))
#define BIT_TO_COL(bit_nr)		((bit_nr) % 8)

/* Structure representing various run-time entities */
struct bcm_kp {
	void __iomem *base;
	int irq;
	struct clk *clk;
	struct input_dev *input_dev;
	unsigned long last_state[2];
	unsigned int n_rows;
	unsigned int n_cols;
	u32 kpcr;
	u32 kpior;
	u32 kpemr;
	u32 imr0_val;
	u32 imr1_val;
};

/*
 * Returns the keycode from the input device keymap given the row and
 * column.
 */
static int bcm_kp_get_keycode(struct bcm_kp *kp, int row, int col)
{
	unsigned int row_shift = get_count_order(kp->n_cols);
	unsigned short *keymap = kp->input_dev->keycode;

	return keymap[MATRIX_SCAN_CODE(row, col, row_shift)];
}

static void bcm_kp_report_keys(struct bcm_kp *kp, int reg_num, int pull_mode)
{
	unsigned long state, change;
	int bit_nr;
	int key_press;
	int row, col;
	unsigned int keycode;

	/* Clear interrupts */
	writel(0xFFFFFFFF, kp->base + KPICRN_OFFSET(reg_num));

	state = readl(kp->base + KPSSRN_OFFSET(reg_num));
	change = kp->last_state[reg_num] ^ state;
	kp->last_state[reg_num] = state;

	for_each_set_bit(bit_nr, &change, BITS_PER_LONG) {
		key_press = state & BIT(bit_nr);
		/* The meaning of SSR register depends on pull mode. */
		key_press = pull_mode ? !key_press : key_press;
		row = BIT_TO_ROW_SSRN(bit_nr, reg_num);
		col = BIT_TO_COL(bit_nr);
		keycode = bcm_kp_get_keycode(kp, row, col);
		input_report_key(kp->input_dev, keycode, key_press);
	}
}

static irqreturn_t bcm_kp_isr_thread(int irq, void *dev_id)
{
	struct bcm_kp *kp = dev_id;
	int pull_mode = (kp->kpcr >> KPCR_MODE_SHIFT) & KPCR_MODE_MASK;
	int reg_num;

	for (reg_num = 0; reg_num <= 1; reg_num++)
		bcm_kp_report_keys(kp, reg_num, pull_mode);

	input_sync(kp->input_dev);

	return IRQ_HANDLED;
}

static int bcm_kp_start(struct bcm_kp *kp)
{
	int error;

	if (kp->clk) {
		error = clk_prepare_enable(kp->clk);
		if (error)
			return error;
	}

	writel(kp->kpior, kp->base + KPIOR_OFFSET);

	writel(kp->imr0_val, kp->base + KPIMR0_OFFSET);
	writel(kp->imr1_val, kp->base + KPIMR1_OFFSET);

	writel(kp->kpemr, kp->base + KPEMR0_OFFSET);
	writel(kp->kpemr, kp->base + KPEMR1_OFFSET);
	writel(kp->kpemr, kp->base + KPEMR2_OFFSET);
	writel(kp->kpemr, kp->base + KPEMR3_OFFSET);

	writel(0xFFFFFFFF, kp->base + KPICR0_OFFSET);
	writel(0xFFFFFFFF, kp->base + KPICR1_OFFSET);

	kp->last_state[0] = readl(kp->base + KPSSR0_OFFSET);
	kp->last_state[0] = readl(kp->base + KPSSR1_OFFSET);

	writel(kp->kpcr | KPCR_ENABLE, kp->base + KPCR_OFFSET);

	return 0;
}

static void bcm_kp_stop(const struct bcm_kp *kp)
{
	u32 val;

	val = readl(kp->base + KPCR_OFFSET);
	val &= ~KPCR_ENABLE;
	writel(0, kp->base + KPCR_OFFSET);
	writel(0, kp->base + KPIMR0_OFFSET);
	writel(0, kp->base + KPIMR1_OFFSET);
	writel(0xFFFFFFFF, kp->base + KPICR0_OFFSET);
	writel(0xFFFFFFFF, kp->base + KPICR1_OFFSET);

	clk_disable_unprepare(kp->clk);
}

static int bcm_kp_open(struct input_dev *dev)
{
	struct bcm_kp *kp = input_get_drvdata(dev);

	return bcm_kp_start(kp);
}

static void bcm_kp_close(struct input_dev *dev)
{
	struct bcm_kp *kp = input_get_drvdata(dev);

	bcm_kp_stop(kp);
}

static int bcm_kp_matrix_key_parse_dt(struct bcm_kp *kp)
{
	struct device *dev = kp->input_dev->dev.parent;
	struct device_node *np = dev->of_node;
	int error;
	unsigned int dt_val;
	unsigned int i;
	unsigned int num_rows, col_mask, rows_set;

	/* Initialize the KPCR Keypad Configuration Register */
	kp->kpcr = KPCR_STATUSFILTERENABLE | KPCR_COLFILTERENABLE;

	error = matrix_keypad_parse_properties(dev, &kp->n_rows, &kp->n_cols);
	if (error) {
		dev_err(dev, "failed to parse kp params\n");
		return error;
	}

	/* Set row width for the ASIC block. */
	kp->kpcr |= (kp->n_rows - 1) << KPCR_ROWWIDTH_SHIFT;

	/* Set column width for the ASIC block. */
	kp->kpcr |= (kp->n_cols - 1) << KPCR_COLUMNWIDTH_SHIFT;

	/* Configure the IMR registers */

	/*
	 * IMR registers contain interrupt enable bits for 8x8 matrix
	 * IMR0 register format: <row3> <row2> <row1> <row0>
	 * IMR1 register format: <row7> <row6> <row5> <row4>
	 */
	col_mask = (1 << (kp->n_cols)) - 1;
	num_rows = kp->n_rows;

	/* Set column bits in rows 0 to 3 in IMR0 */
	kp->imr0_val = col_mask;

	rows_set = 1;
	while (--num_rows && rows_set++ < 4)
		kp->imr0_val |= kp->imr0_val << MAX_COLS;

	/* Set column bits in rows 4 to 7 in IMR1 */
	kp->imr1_val = 0;
	if (num_rows) {
		kp->imr1_val = col_mask;
		while (--num_rows)
			kp->imr1_val |= kp->imr1_val << MAX_COLS;
	}

	/* Initialize the KPEMR Keypress Edge Mode Registers */
	/* Trigger on both edges */
	kp->kpemr = 0;
	for (i = 0; i <= 30; i += 2)
		kp->kpemr |= (KPEMR_EDGETYPE_BOTH << i);

	/*
	 * Obtain the Status filter debounce value and verify against the
	 * possible values specified in the DT binding.
	 */
	of_property_read_u32(np, "status-debounce-filter-period", &dt_val);

	if (dt_val > KPCR_STATUSFILTERTYPE_MAX) {
		dev_err(dev, "Invalid Status filter debounce value %d\n",
			dt_val);
		return -EINVAL;
	}

	kp->kpcr |= dt_val << KPCR_STATUSFILTERTYPE_SHIFT;

	/*
	 * Obtain the Column filter debounce value and verify against the
	 * possible values specified in the DT binding.
	 */
	of_property_read_u32(np, "col-debounce-filter-period", &dt_val);

	if (dt_val > KPCR_COLFILTERTYPE_MAX) {
		dev_err(dev, "Invalid Column filter debounce value %d\n",
			dt_val);
		return -EINVAL;
	}

	kp->kpcr |= dt_val << KPCR_COLFILTERTYPE_SHIFT;

	/*
	 * Determine between the row and column,
	 * which should be configured as output.
	 */
	if (of_property_read_bool(np, "row-output-enabled")) {
		/*
		* Set RowOContrl or ColumnOContrl in KPIOR
		* to the number of pins to drive as outputs
		*/
		kp->kpior = ((1 << kp->n_rows) - 1) <<
				KPIOR_ROWOCONTRL_SHIFT;
	} else {
		kp->kpior = ((1 << kp->n_cols) - 1) <<
				KPIOR_COLUMNOCONTRL_SHIFT;
	}

	/*
	 * Determine if the scan pull up needs to be enabled
	 */
	if (of_property_read_bool(np, "pull-up-enabled"))
		kp->kpcr |= KPCR_MODE;

	dev_dbg(dev, "n_rows=%d n_col=%d kpcr=%x kpior=%x kpemr=%x\n",
		kp->n_rows, kp->n_cols,
		kp->kpcr, kp->kpior, kp->kpemr);

	return 0;
}


static int bcm_kp_probe(struct platform_device *pdev)
{
	struct bcm_kp *kp;
	struct input_dev *input_dev;
	struct resource *res;
	int error;

	kp = devm_kzalloc(&pdev->dev, sizeof(*kp), GFP_KERNEL);
	if (!kp)
		return -ENOMEM;

	input_dev = devm_input_allocate_device(&pdev->dev);
	if (!input_dev) {
		dev_err(&pdev->dev, "failed to allocate the input device\n");
		return -ENOMEM;
	}

	__set_bit(EV_KEY, input_dev->evbit);

	/* Enable auto repeat feature of Linux input subsystem */
	if (of_property_read_bool(pdev->dev.of_node, "autorepeat"))
		__set_bit(EV_REP, input_dev->evbit);

	input_dev->name = pdev->name;
	input_dev->phys = "keypad/input0";
	input_dev->dev.parent = &pdev->dev;
	input_dev->open = bcm_kp_open;
	input_dev->close = bcm_kp_close;

	input_dev->id.bustype = BUS_HOST;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;

	input_set_drvdata(input_dev, kp);

	kp->input_dev = input_dev;

	error = bcm_kp_matrix_key_parse_dt(kp);
	if (error)
		return error;

	error = matrix_keypad_build_keymap(NULL, NULL,
					   kp->n_rows, kp->n_cols,
					   NULL, input_dev);
	if (error) {
		dev_err(&pdev->dev, "failed to build keymap\n");
		return error;
	}

	/* Get the KEYPAD base address */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Missing keypad base address resource\n");
		return -ENODEV;
	}

	kp->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(kp->base))
		return PTR_ERR(kp->base);

	/* Enable clock */
	kp->clk = devm_clk_get(&pdev->dev, "peri_clk");
	if (IS_ERR(kp->clk)) {
		error = PTR_ERR(kp->clk);
		if (error != -ENOENT) {
			if (error != -EPROBE_DEFER)
				dev_err(&pdev->dev, "Failed to get clock\n");
			return error;
		}
		dev_dbg(&pdev->dev,
			"No clock specified. Assuming it's enabled\n");
		kp->clk = NULL;
	} else {
		unsigned int desired_rate;
		long actual_rate;

		error = of_property_read_u32(pdev->dev.of_node,
					     "clock-frequency", &desired_rate);
		if (error < 0)
			desired_rate = DEFAULT_CLK_HZ;

		actual_rate = clk_round_rate(kp->clk, desired_rate);
		if (actual_rate <= 0)
			return -EINVAL;

		error = clk_set_rate(kp->clk, actual_rate);
		if (error)
			return error;

		error = clk_prepare_enable(kp->clk);
		if (error)
			return error;
	}

	/* Put the kp into a known sane state */
	bcm_kp_stop(kp);

	kp->irq = platform_get_irq(pdev, 0);
	if (kp->irq < 0)
		return -EINVAL;

	error = devm_request_threaded_irq(&pdev->dev, kp->irq,
					  NULL, bcm_kp_isr_thread,
					  IRQF_ONESHOT, pdev->name, kp);
	if (error) {
		dev_err(&pdev->dev, "failed to request IRQ\n");
		return error;
	}

	error = input_register_device(input_dev);
	if (error) {
		dev_err(&pdev->dev, "failed to register input device\n");
		return error;
	}

	return 0;
}

static const struct of_device_id bcm_kp_of_match[] = {
	{ .compatible = "brcm,bcm-keypad" },
	{ },
};
MODULE_DEVICE_TABLE(of, bcm_kp_of_match);

static struct platform_driver bcm_kp_device_driver = {
	.probe		= bcm_kp_probe,
	.driver		= {
		.name	= "bcm-keypad",
		.of_match_table = of_match_ptr(bcm_kp_of_match),
	}
};

module_platform_driver(bcm_kp_device_driver);

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("BCM Keypad Driver");
MODULE_LICENSE("GPL v2");
