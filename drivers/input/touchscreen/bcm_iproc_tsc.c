/*
* Copyright (C) 2015 Broadcom Corporation
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License as
* published by the Free Software Foundation version 2.
*
* This program is distributed "as is" WITHOUT ANY WARRANTY of any
* kind, whether express or implied; without even the implied warranty
* of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/keyboard.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <asm/irq.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/serio.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#define IPROC_TS_NAME "iproc-ts"

#define PEN_DOWN_STATUS     1
#define PEN_UP_STATUS       0

#define X_MIN               0
#define Y_MIN               0
#define X_MAX               0xFFF
#define Y_MAX               0xFFF

/* Value given by controller for invalid coordinate. */
#define INVALID_COORD       0xFFFFFFFF

/* Register offsets */
#define REGCTL1             0x00
#define REGCTL2             0x04
#define INTERRUPT_THRES     0x08
#define INTERRUPT_MASK      0x0c

#define INTERRUPT_STATUS    0x10
#define CONTROLLER_STATUS   0x14
#define FIFO_DATA           0x18
#define FIFO_DATA_X_Y_MASK  0xFFFF
#define ANALOG_CONTROL      0x1c

#define AUX_DATA            0x20
#define DEBOUNCE_CNTR_STAT  0x24
#define SCAN_CNTR_STAT      0x28
#define REM_CNTR_STAT       0x2c

#define SETTLING_TIMER_STAT 0x30
#define SPARE_REG           0x34
#define SOFT_BYPASS_CONTROL 0x38
#define SOFT_BYPASS_DATA    0x3c


/* Bit values for INTERRUPT_MASK and INTERRUPT_STATUS regs */
#define TS_PEN_INTR_MASK        BIT(0)
#define TS_FIFO_INTR_MASK       BIT(2)

/* Bit values for CONTROLLER_STATUS reg1 */
#define TS_PEN_DOWN             BIT(0)

/* Shift values for control reg1 */
#define SCANNING_PERIOD_SHIFT   24
#define DEBOUNCE_TIMEOUT_SHIFT  16
#define SETTLING_TIMEOUT_SHIFT  8
#define TOUCH_TIMEOUT_SHIFT     0

/* Shift values for coordinates from fifo */
#define X_COORD_SHIFT  0
#define Y_COORD_SHIFT  16

/* Bit values for REGCTL2 */
#define TS_CONTROLLER_EN_BIT    BIT(16)
#define TS_CONTROLLER_AVGDATA_SHIFT 8
#define TS_CONTROLLER_AVGDATA_MASK (0x7 << TS_CONTROLLER_AVGDATA_SHIFT)
#define TS_CONTROLLER_PWR_LDO   BIT(5)
#define TS_CONTROLLER_PWR_ADC   BIT(4)
#define TS_CONTROLLER_PWR_BGP   BIT(3)
#define TS_CONTROLLER_PWR_TS    BIT(2)
#define TS_WIRE_MODE_BIT        BIT(1)

#define dbg_reg(dev, priv, reg) \
do { \
	u32 val; \
	regmap_read(priv->regmap, reg, &val); \
	dev_dbg(dev, "%20s= 0x%08x\n", #reg, val); \
} while (0)

struct tsc_param {
	/* Each step is 1024 us.  Valid 1-256 */
	u32 scanning_period;

	/*  Each step is 512 us.  Valid 0-255 */
	u32 debounce_timeout;

	/*
	 * The settling duration (in ms) is the amount of time the tsc
	 * waits to allow the voltage to settle after turning on the
	 * drivers in detection mode. Valid values: 0-11
	 *   0 =  0.008 ms
	 *   1 =  0.01 ms
	 *   2 =  0.02 ms
	 *   3 =  0.04 ms
	 *   4 =  0.08 ms
	 *   5 =  0.16 ms
	 *   6 =  0.32 ms
	 *   7 =  0.64 ms
	 *   8 =  1.28 ms
	 *   9 =  2.56 ms
	 *   10 = 5.12 ms
	 *   11 = 10.24 ms
	 */
	u32 settling_timeout;

	/* touch timeout in sample counts */
	u32 touch_timeout;

	/*
	 * Number of data samples which are averaged before a final data point
	 * is placed into the FIFO
	 */
	u32 average_data;

	/* FIFO threshold */
	u32 fifo_threshold;

	/* Optional standard touchscreen properties. */
	u32 max_x;
	u32 max_y;
	u32 fuzz_x;
	u32 fuzz_y;
	bool invert_x;
	bool invert_y;
};

struct iproc_ts_priv {
	struct platform_device *pdev;
	struct input_dev *idev;

	struct regmap *regmap;
	struct clk *tsc_clk;

	int  pen_status;
	struct tsc_param cfg_params;
};

/*
 * Set default values the same as hardware reset values
 * except for fifo_threshold with is set to 1.
 */
static const struct tsc_param iproc_default_config = {
	.scanning_period  = 0x5,  /* 1 to 256 */
	.debounce_timeout = 0x28, /* 0 to 255 */
	.settling_timeout = 0x7,  /* 0 to 11 */
	.touch_timeout    = 0xa,  /* 0 to 255 */
	.average_data     = 5,    /* entry 5 = 32 pts */
	.fifo_threshold   = 1,    /* 0 to 31 */
	.max_x            = X_MAX,
	.max_y            = Y_MAX,
};

static void ts_reg_dump(struct iproc_ts_priv *priv)
{
	struct device *dev = &priv->pdev->dev;

	dbg_reg(dev, priv, REGCTL1);
	dbg_reg(dev, priv, REGCTL2);
	dbg_reg(dev, priv, INTERRUPT_THRES);
	dbg_reg(dev, priv, INTERRUPT_MASK);
	dbg_reg(dev, priv, INTERRUPT_STATUS);
	dbg_reg(dev, priv, CONTROLLER_STATUS);
	dbg_reg(dev, priv, FIFO_DATA);
	dbg_reg(dev, priv, ANALOG_CONTROL);
	dbg_reg(dev, priv, AUX_DATA);
	dbg_reg(dev, priv, DEBOUNCE_CNTR_STAT);
	dbg_reg(dev, priv, SCAN_CNTR_STAT);
	dbg_reg(dev, priv, REM_CNTR_STAT);
	dbg_reg(dev, priv, SETTLING_TIMER_STAT);
	dbg_reg(dev, priv, SPARE_REG);
	dbg_reg(dev, priv, SOFT_BYPASS_CONTROL);
	dbg_reg(dev, priv, SOFT_BYPASS_DATA);
}

static irqreturn_t iproc_touchscreen_interrupt(int irq, void *data)
{
	struct platform_device *pdev = data;
	struct iproc_ts_priv *priv = platform_get_drvdata(pdev);
	u32 intr_status;
	u32 raw_coordinate;
	u16 x;
	u16 y;
	int i;
	bool needs_sync = false;

	regmap_read(priv->regmap, INTERRUPT_STATUS, &intr_status);
	intr_status &= TS_PEN_INTR_MASK | TS_FIFO_INTR_MASK;
	if (intr_status == 0)
		return IRQ_NONE;

	/* Clear all interrupt status bits, write-1-clear */
	regmap_write(priv->regmap, INTERRUPT_STATUS, intr_status);
	/* Pen up/down */
	if (intr_status & TS_PEN_INTR_MASK) {
		regmap_read(priv->regmap, CONTROLLER_STATUS, &priv->pen_status);
		if (priv->pen_status & TS_PEN_DOWN)
			priv->pen_status = PEN_DOWN_STATUS;
		else
			priv->pen_status = PEN_UP_STATUS;

		input_report_key(priv->idev, BTN_TOUCH,	priv->pen_status);
		needs_sync = true;

		dev_dbg(&priv->pdev->dev,
			"pen up-down (%d)\n", priv->pen_status);
	}

	/* coordinates in FIFO exceed the theshold */
	if (intr_status & TS_FIFO_INTR_MASK) {
		for (i = 0; i < priv->cfg_params.fifo_threshold; i++) {
			regmap_read(priv->regmap, FIFO_DATA, &raw_coordinate);
			if (raw_coordinate == INVALID_COORD)
				continue;

			/*
			 * The x and y coordinate are 16 bits each
			 * with the x in the lower 16 bits and y in the
			 * upper 16 bits.
			 */
			x = (raw_coordinate >> X_COORD_SHIFT) &
				FIFO_DATA_X_Y_MASK;
			y = (raw_coordinate >> Y_COORD_SHIFT) &
				FIFO_DATA_X_Y_MASK;

			/* We only want to retain the 12 msb of the 16 */
			x = (x >> 4) & 0x0FFF;
			y = (y >> 4) & 0x0FFF;

			/* Adjust x y according to LCD tsc mount angle. */
			if (priv->cfg_params.invert_x)
				x = priv->cfg_params.max_x - x;

			if (priv->cfg_params.invert_y)
				y = priv->cfg_params.max_y - y;

			input_report_abs(priv->idev, ABS_X, x);
			input_report_abs(priv->idev, ABS_Y, y);
			needs_sync = true;

			dev_dbg(&priv->pdev->dev, "xy (0x%x 0x%x)\n", x, y);
		}
	}

	if (needs_sync)
		input_sync(priv->idev);

	return IRQ_HANDLED;
}

static int iproc_ts_start(struct input_dev *idev)
{
	u32 val;
	u32 mask;
	int error;
	struct iproc_ts_priv *priv = input_get_drvdata(idev);

	/* Enable clock */
	error = clk_prepare_enable(priv->tsc_clk);
	if (error) {
		dev_err(&priv->pdev->dev, "%s clk_prepare_enable failed %d\n",
			__func__, error);
		return error;
	}

	/*
	 * Interrupt is generated when:
	 *  FIFO reaches the int_th value, and pen event(up/down)
	 */
	val = TS_PEN_INTR_MASK | TS_FIFO_INTR_MASK;
	regmap_update_bits(priv->regmap, INTERRUPT_MASK, val, val);

	val = priv->cfg_params.fifo_threshold;
	regmap_write(priv->regmap, INTERRUPT_THRES, val);

	/* Initialize control reg1 */
	val = 0;
	val |= priv->cfg_params.scanning_period << SCANNING_PERIOD_SHIFT;
	val |= priv->cfg_params.debounce_timeout << DEBOUNCE_TIMEOUT_SHIFT;
	val |= priv->cfg_params.settling_timeout << SETTLING_TIMEOUT_SHIFT;
	val |= priv->cfg_params.touch_timeout << TOUCH_TIMEOUT_SHIFT;
	regmap_write(priv->regmap, REGCTL1, val);

	/* Try to clear all interrupt status */
	val = TS_FIFO_INTR_MASK | TS_PEN_INTR_MASK;
	regmap_update_bits(priv->regmap, INTERRUPT_STATUS, val, val);

	/* Initialize control reg2 */
	val = TS_CONTROLLER_EN_BIT | TS_WIRE_MODE_BIT;
	val |= priv->cfg_params.average_data << TS_CONTROLLER_AVGDATA_SHIFT;

	mask = (TS_CONTROLLER_AVGDATA_MASK);
	mask |= (TS_CONTROLLER_PWR_LDO |	/* PWR up LDO */
		   TS_CONTROLLER_PWR_ADC |	/* PWR up ADC */
		   TS_CONTROLLER_PWR_BGP |	/* PWR up BGP */
		   TS_CONTROLLER_PWR_TS);	/* PWR up TS */
	mask |= val;
	regmap_update_bits(priv->regmap, REGCTL2, mask, val);

	ts_reg_dump(priv);

	return 0;
}

static void iproc_ts_stop(struct input_dev *dev)
{
	u32 val;
	struct iproc_ts_priv *priv = input_get_drvdata(dev);

	/*
	 * Disable FIFO int_th and pen event(up/down)Interrupts only
	 * as the interrupt mask register is shared between ADC, TS and
	 * flextimer.
	 */
	val = TS_PEN_INTR_MASK | TS_FIFO_INTR_MASK;
	regmap_update_bits(priv->regmap, INTERRUPT_MASK, val, 0);

	/* Only power down touch screen controller */
	val = TS_CONTROLLER_PWR_TS;
	regmap_update_bits(priv->regmap, REGCTL2, val, val);

	clk_disable(priv->tsc_clk);
}

static int iproc_get_tsc_config(struct device *dev, struct iproc_ts_priv *priv)
{
	struct device_node *np = dev->of_node;
	u32 val;

	priv->cfg_params = iproc_default_config;

	if (!np)
		return 0;

	if (of_property_read_u32(np, "scanning_period", &val) >= 0) {
		if (val < 1 || val > 256) {
			dev_err(dev, "scanning_period (%u) must be [1-256]\n",
				val);
			return -EINVAL;
		}
		priv->cfg_params.scanning_period = val;
	}

	if (of_property_read_u32(np, "debounce_timeout", &val) >= 0) {
		if (val > 255) {
			dev_err(dev, "debounce_timeout (%u) must be [0-255]\n",
				val);
			return -EINVAL;
		}
		priv->cfg_params.debounce_timeout = val;
	}

	if (of_property_read_u32(np, "settling_timeout", &val) >= 0) {
		if (val > 11) {
			dev_err(dev, "settling_timeout (%u) must be [0-11]\n",
				val);
			return -EINVAL;
		}
		priv->cfg_params.settling_timeout = val;
	}

	if (of_property_read_u32(np, "touch_timeout", &val) >= 0) {
		if (val > 255) {
			dev_err(dev, "touch_timeout (%u) must be [0-255]\n",
				val);
			return -EINVAL;
		}
		priv->cfg_params.touch_timeout = val;
	}

	if (of_property_read_u32(np, "average_data", &val) >= 0) {
		if (val > 8) {
			dev_err(dev, "average_data (%u) must be [0-8]\n", val);
			return -EINVAL;
		}
		priv->cfg_params.average_data = val;
	}

	if (of_property_read_u32(np, "fifo_threshold", &val) >= 0) {
		if (val > 31) {
			dev_err(dev, "fifo_threshold (%u)) must be [0-31]\n",
				val);
			return -EINVAL;
		}
		priv->cfg_params.fifo_threshold = val;
	}

	/* Parse optional properties. */
	of_property_read_u32(np, "touchscreen-size-x", &priv->cfg_params.max_x);
	of_property_read_u32(np, "touchscreen-size-y", &priv->cfg_params.max_y);

	of_property_read_u32(np, "touchscreen-fuzz-x",
			     &priv->cfg_params.fuzz_x);
	of_property_read_u32(np, "touchscreen-fuzz-y",
			     &priv->cfg_params.fuzz_y);

	priv->cfg_params.invert_x =
		of_property_read_bool(np, "touchscreen-inverted-x");
	priv->cfg_params.invert_y =
		of_property_read_bool(np, "touchscreen-inverted-y");

	return 0;
}

static int iproc_ts_probe(struct platform_device *pdev)
{
	struct iproc_ts_priv *priv;
	struct input_dev *idev;
	int irq;
	int error;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* touchscreen controller memory mapped regs via syscon*/
	priv->regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
							"ts_syscon");
	if (IS_ERR(priv->regmap)) {
		error = PTR_ERR(priv->regmap);
		dev_err(&pdev->dev, "unable to map I/O memory:%d\n", error);
		return error;
	}

	priv->tsc_clk = devm_clk_get(&pdev->dev, "tsc_clk");
	if (IS_ERR(priv->tsc_clk)) {
		error = PTR_ERR(priv->tsc_clk);
		dev_err(&pdev->dev,
			"failed getting clock tsc_clk: %d\n", error);
		return error;
	}

	priv->pdev = pdev;
	error = iproc_get_tsc_config(&pdev->dev, priv);
	if (error) {
		dev_err(&pdev->dev, "get_tsc_config failed: %d\n", error);
		return error;
	}

	idev = devm_input_allocate_device(&pdev->dev);
	if (!idev) {
		dev_err(&pdev->dev, "failed to allocate input device\n");
		return -ENOMEM;
	}

	priv->idev = idev;
	priv->pen_status = PEN_UP_STATUS;

	/* Set input device info  */
	idev->name = IPROC_TS_NAME;
	idev->dev.parent = &pdev->dev;

	idev->id.bustype = BUS_HOST;
	idev->id.vendor = SERIO_UNKNOWN;
	idev->id.product = 0;
	idev->id.version = 0;

	idev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	__set_bit(BTN_TOUCH, idev->keybit);

	input_set_abs_params(idev, ABS_X, X_MIN, priv->cfg_params.max_x,
			     priv->cfg_params.fuzz_x, 0);
	input_set_abs_params(idev, ABS_Y, Y_MIN, priv->cfg_params.max_y,
			     priv->cfg_params.fuzz_y, 0);

	idev->open = iproc_ts_start;
	idev->close = iproc_ts_stop;

	input_set_drvdata(idev, priv);
	platform_set_drvdata(pdev, priv);

	/* get interrupt */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "platform_get_irq failed: %d\n", irq);
		return irq;
	}

	error = devm_request_irq(&pdev->dev, irq,
				 iproc_touchscreen_interrupt,
				 IRQF_SHARED, IPROC_TS_NAME, pdev);
	if (error)
		return error;

	error = input_register_device(priv->idev);
	if (error) {
		dev_err(&pdev->dev,
			"failed to register input device: %d\n", error);
		return error;
	}

	return 0;
}

static const struct of_device_id iproc_ts_of_match[] = {
	{.compatible = "brcm,iproc-touchscreen", },
	{ },
};
MODULE_DEVICE_TABLE(of, iproc_ts_of_match);

static struct platform_driver iproc_ts_driver = {
	.probe = iproc_ts_probe,
	.driver = {
		.name	= IPROC_TS_NAME,
		.of_match_table = of_match_ptr(iproc_ts_of_match),
	},
};

module_platform_driver(iproc_ts_driver);

MODULE_DESCRIPTION("IPROC Touchscreen driver");
MODULE_AUTHOR("Broadcom");
MODULE_LICENSE("GPL v2");
