// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2026 Axiado Corporation
 *
 * Driver for the Axiado on-chip temperature sensor (TSADC)
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>

/* TSADC register offsets */
#define AXIADO_TSADC_MODE	0x0000
#define AXIADO_TSADC_FUNC_CTRL	0x0004
#define AXIADO_TSADC_AVG_DOUT	0x000c
#define AXIADO_TSADC_INTR_MASK	0x0010
#define AXIADO_TSADC_ANA_REG	0x0020
#define AXIADO_TSADC_SEL	0x0028
#define AXIADO_TSADC_TSEN_EN	0x002c
#define AXIADO_TSADC_EN	0x0030

/* TSADC mode register: continuous conversion */
#define AXIADO_TSADC_MODE_CONT	0x2

/*
 * TSADC functional control register: bit 0 starts the conversion and
 * bits [31:16] specify the number of samples. Average over 16 samples.
 */
#define AXIADO_TSADC_FUNC_CTRL_START		BIT(0)
#define AXIADO_TSADC_FUNC_CTRL_NUM_SAMPLES	GENMASK(31, 16)
#define AXIADO_TSADC_FUNC_CTRL_CLR		0x0
#define AXIADO_TSADC_FUNC_CTRL_SET				\
	(FIELD_PREP(AXIADO_TSADC_FUNC_CTRL_NUM_SAMPLES, 16) |	\
	 AXIADO_TSADC_FUNC_CTRL_START)

/* TSADC average output register: 12-bit ADC result */
#define AXIADO_TSADC_AVG_DOUT_MASK	GENMASK(11, 0)

/* Interrupt mask bits */
#define AXIADO_TSADC_INTR_BIST_DONE		BIT(0)
#define AXIADO_TSADC_INTR_LOOP_DONE		BIT(1)
#define AXIADO_TSADC_INTR_THRESHOLD		BIT(2)
#define AXIADO_TSADC_INTR_MASK_ALL		\
	(AXIADO_TSADC_INTR_BIST_DONE |		\
	 AXIADO_TSADC_INTR_LOOP_DONE |		\
	 AXIADO_TSADC_INTR_THRESHOLD)

/* TSADC analog block configuration required for temperature measurement */
#define AXIADO_TSADC_ANA_REG_INIT	0x6

/* TSADC SEL: select temperature measurement */
#define AXIADO_TSADC_SEL_TEMP	0x00

/* TSADC EN / TSEN_EN field values */
#define AXIADO_TSADC_DIS	0x00
#define AXIADO_TSADC_ENA	0x01

struct axiado_tsadc {
	void __iomem *regs;
};

/*
 * Lookup table: raw ADC output (12-bit) vs temperature in millidegrees
 * Celsius. Raw values decrease as temperature increases, from -40°C
 * to 125°C in 5°C steps.
 */
struct axiado_tsadc_point {
	u32 raw;
	int temp_mc;
};

static const struct axiado_tsadc_point axiado_tsadc_table[] = {
	{ .raw = 2776, .temp_mc = -40000 },
	{ .raw = 2748, .temp_mc = -35000 },
	{ .raw = 2721, .temp_mc = -30000 },
	{ .raw = 2694, .temp_mc = -25000 },
	{ .raw = 2667, .temp_mc = -20000 },
	{ .raw = 2640, .temp_mc = -15000 },
	{ .raw = 2612, .temp_mc = -10000 },
	{ .raw = 2584, .temp_mc =  -5000 },
	{ .raw = 2556, .temp_mc =      0 },
	{ .raw = 2528, .temp_mc =   5000 },
	{ .raw = 2500, .temp_mc =  10000 },
	{ .raw = 2472, .temp_mc =  15000 },
	{ .raw = 2444, .temp_mc =  20000 },
	{ .raw = 2416, .temp_mc =  25000 },
	{ .raw = 2389, .temp_mc =  30000 },
	{ .raw = 2362, .temp_mc =  35000 },
	{ .raw = 2335, .temp_mc =  40000 },
	{ .raw = 2307, .temp_mc =  45000 },
	{ .raw = 2279, .temp_mc =  50000 },
	{ .raw = 2251, .temp_mc =  55000 },
	{ .raw = 2223, .temp_mc =  60000 },
	{ .raw = 2195, .temp_mc =  65000 },
	{ .raw = 2167, .temp_mc =  70000 },
	{ .raw = 2138, .temp_mc =  75000 },
	{ .raw = 2110, .temp_mc =  80000 },
	{ .raw = 2081, .temp_mc =  85000 },
	{ .raw = 2052, .temp_mc =  90000 },
	{ .raw = 2024, .temp_mc =  95000 },
	{ .raw = 1996, .temp_mc = 100000 },
	{ .raw = 1967, .temp_mc = 105000 },
	{ .raw = 1938, .temp_mc = 110000 },
	{ .raw = 1909, .temp_mc = 115000 },
	{ .raw = 1880, .temp_mc = 120000 },
	{ .raw = 1852, .temp_mc = 125000 },
};

/* Convert raw ADC code to millidegrees Celsius with linear interpolation. */
static int axiado_tsadc_raw_to_mc(u32 raw, long *val)
{
	int i, n = ARRAY_SIZE(axiado_tsadc_table);
	long num, denom;

	if (raw >= axiado_tsadc_table[0].raw) {
		*val = axiado_tsadc_table[0].temp_mc;
		return 0;
	}

	if (raw <= axiado_tsadc_table[n - 1].raw) {
		*val = axiado_tsadc_table[n - 1].temp_mc;
		return 0;
	}

	/* Find i such that axiado_tsadc_table[i].raw >= raw > axiado_tsadc_table[i+1].raw */
	for (i = 0; i < n - 1; i++) {
		if (raw >= axiado_tsadc_table[i + 1].raw)
			break;
	}

	if (raw == axiado_tsadc_table[i].raw) {
		*val = axiado_tsadc_table[i].temp_mc;
		return 0;
	}

	/* Linear interpolation within the interval */
	num = (long)(axiado_tsadc_table[i + 1].temp_mc - axiado_tsadc_table[i].temp_mc) *
	      (axiado_tsadc_table[i].raw - raw);
	denom = axiado_tsadc_table[i].raw - axiado_tsadc_table[i + 1].raw;
	*val = axiado_tsadc_table[i].temp_mc + num / denom;
	return 0;
}

static int axiado_tsadc_read(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long *val)
{
	struct axiado_tsadc *tsadc = dev_get_drvdata(dev);
	u32 raw;

	if (type != hwmon_temp)
		return -EOPNOTSUPP;

	switch (attr) {
	case hwmon_temp_input:
		if (ioread32(tsadc->regs + AXIADO_TSADC_SEL) != AXIADO_TSADC_SEL_TEMP ||
		    ioread32(tsadc->regs + AXIADO_TSADC_EN) != AXIADO_TSADC_ENA)
			return -ENODATA;

		raw = ioread32(tsadc->regs + AXIADO_TSADC_AVG_DOUT) &
			       AXIADO_TSADC_AVG_DOUT_MASK;

		return axiado_tsadc_raw_to_mc(raw, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t axiado_tsadc_is_visible(const void *data,
				       enum hwmon_sensor_types type, u32 attr,
				       int channel)
{
	if (type == hwmon_temp) {
		switch (attr) {
		case hwmon_temp_input:
			return 0444;
		default:
			break;
		}
	}

	return 0;
}

static const struct hwmon_channel_info * const axiado_tsadc_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
	NULL
};

static const struct hwmon_ops axiado_tsadc_hwmon_ops = {
	.is_visible = axiado_tsadc_is_visible,
	.read = axiado_tsadc_read,
};

static const struct hwmon_chip_info axiado_tsadc_chip_info = {
	.ops = &axiado_tsadc_hwmon_ops,
	.info = axiado_tsadc_info,
};

static void axiado_tsadc_disable(void *data)
{
	struct axiado_tsadc *tsadc = data;

	iowrite32(AXIADO_TSADC_DIS, tsadc->regs + AXIADO_TSADC_TSEN_EN);
	iowrite32(AXIADO_TSADC_DIS, tsadc->regs + AXIADO_TSADC_EN);
}

static int axiado_tsadc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct axiado_tsadc *tsadc;
	struct device *hwmon_dev;
	struct clk *refclk;
	int ret;

	tsadc = devm_kzalloc(dev, sizeof(*tsadc), GFP_KERNEL);
	if (!tsadc)
		return -ENOMEM;

	tsadc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(tsadc->regs))
		return dev_err_probe(dev, PTR_ERR(tsadc->regs),
				     "failed to map registers\n");

	refclk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(refclk))
		return dev_err_probe(dev, PTR_ERR(refclk),
				     "failed to enable reference clock\n");

	/* Mask all interrupts before setup */
	iowrite32(AXIADO_TSADC_INTR_MASK_ALL, tsadc->regs + AXIADO_TSADC_INTR_MASK);

	iowrite32(AXIADO_TSADC_ANA_REG_INIT, tsadc->regs + AXIADO_TSADC_ANA_REG);
	iowrite32(AXIADO_TSADC_ENA, tsadc->regs + AXIADO_TSADC_EN);
	iowrite32(AXIADO_TSADC_SEL_TEMP, tsadc->regs + AXIADO_TSADC_SEL);
	iowrite32(AXIADO_TSADC_MODE_CONT, tsadc->regs + AXIADO_TSADC_MODE);
	iowrite32(AXIADO_TSADC_FUNC_CTRL_CLR, tsadc->regs + AXIADO_TSADC_FUNC_CTRL);
	iowrite32(AXIADO_TSADC_FUNC_CTRL_SET, tsadc->regs + AXIADO_TSADC_FUNC_CTRL);

	/* Enable the bandgap circuit once the block is fully configured */
	iowrite32(AXIADO_TSADC_ENA, tsadc->regs + AXIADO_TSADC_TSEN_EN);

	ret = devm_add_action_or_reset(dev, axiado_tsadc_disable, tsadc);
	if (ret)
		return ret;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, "axiado_tsadc",
							 tsadc,
							 &axiado_tsadc_chip_info,
							 NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct of_device_id axiado_tsadc_match[] = {
	{ .compatible = "axiado,ax3000-tsadc" },
	{ }
};
MODULE_DEVICE_TABLE(of, axiado_tsadc_match);

static struct platform_driver axiado_tsadc_driver = {
	.driver = {
		.name = "axiado-tsadc",
		.of_match_table	= axiado_tsadc_match,
	},
	.probe = axiado_tsadc_probe,
};
module_platform_driver(axiado_tsadc_driver);

MODULE_DESCRIPTION("Axiado TSADC temperature sensor driver");
MODULE_AUTHOR("Axiado Corporation");
MODULE_LICENSE("GPL");
