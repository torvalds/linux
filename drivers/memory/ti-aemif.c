// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI AEMIF driver
 *
 * Copyright (C) 2010 - 2013 Texas Instruments Incorporated. http://www.ti.com/
 *
 * Authors:
 * Murali Karicheri <m-karicheri2@ti.com>
 * Ivan Khoronzhuk <ivan.khoronzhuk@ti.com>
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/memory/ti-aemif.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#define TA_SHIFT	2
#define RHOLD_SHIFT	4
#define RSTROBE_SHIFT	7
#define RSETUP_SHIFT	13
#define WHOLD_SHIFT	17
#define WSTROBE_SHIFT	20
#define WSETUP_SHIFT	26
#define EW_SHIFT	30
#define SSTROBE_SHIFT	31

#define TA(x)		((x) << TA_SHIFT)
#define RHOLD(x)	((x) << RHOLD_SHIFT)
#define RSTROBE(x)	((x) << RSTROBE_SHIFT)
#define RSETUP(x)	((x) << RSETUP_SHIFT)
#define WHOLD(x)	((x) << WHOLD_SHIFT)
#define WSTROBE(x)	((x) << WSTROBE_SHIFT)
#define WSETUP(x)	((x) << WSETUP_SHIFT)
#define EW(x)		((x) << EW_SHIFT)
#define SSTROBE(x)	((x) << SSTROBE_SHIFT)

#define ASIZE_MAX	0x1
#define TA_MAX		0x3
#define RHOLD_MAX	0x7
#define RSTROBE_MAX	0x3f
#define RSETUP_MAX	0xf
#define WHOLD_MAX	0x7
#define WSTROBE_MAX	0x3f
#define WSETUP_MAX	0xf
#define EW_MAX		0x1
#define SSTROBE_MAX	0x1
#define NUM_CS		4

#define TA_VAL(x)	(((x) & TA(TA_MAX)) >> TA_SHIFT)
#define RHOLD_VAL(x)	(((x) & RHOLD(RHOLD_MAX)) >> RHOLD_SHIFT)
#define RSTROBE_VAL(x)	(((x) & RSTROBE(RSTROBE_MAX)) >> RSTROBE_SHIFT)
#define RSETUP_VAL(x)	(((x) & RSETUP(RSETUP_MAX)) >> RSETUP_SHIFT)
#define WHOLD_VAL(x)	(((x) & WHOLD(WHOLD_MAX)) >> WHOLD_SHIFT)
#define WSTROBE_VAL(x)	(((x) & WSTROBE(WSTROBE_MAX)) >> WSTROBE_SHIFT)
#define WSETUP_VAL(x)	(((x) & WSETUP(WSETUP_MAX)) >> WSETUP_SHIFT)
#define EW_VAL(x)	(((x) & EW(EW_MAX)) >> EW_SHIFT)
#define SSTROBE_VAL(x)	(((x) & SSTROBE(SSTROBE_MAX)) >> SSTROBE_SHIFT)

#define NRCSR_OFFSET	0x00
#define AWCCR_OFFSET	0x04
#define A1CR_OFFSET	0x10

#define ACR_ASIZE_MASK	0x3
#define ACR_EW_MASK	BIT(30)
#define ACR_SSTROBE_MASK	BIT(31)
#define ASIZE_16BIT	1

#define TIMINGS_MASK	(TA(TA_MAX) | \
			RHOLD(RHOLD_MAX) | \
			RSTROBE(RSTROBE_MAX) |	\
			RSETUP(RSETUP_MAX) | \
			WHOLD(WHOLD_MAX) | \
			WSTROBE(WSTROBE_MAX) | \
			WSETUP(WSETUP_MAX))

#define CONFIG_MASK	(EW(EW_MAX) | SSTROBE(SSTROBE_MAX) | ASIZE_MAX)

/**
 * struct aemif_cs_data: structure to hold CS parameters
 * @timings: timings configuration
 * @cs: chip-select number
 * @enable_ss: enable/disable select strobe mode
 * @enable_ew: enable/disable extended wait mode
 * @asize: width of the asynchronous device's data bus
 */
struct aemif_cs_data {
	struct aemif_cs_timings timings;
	u8	cs;
	u8	enable_ss;
	u8	enable_ew;
	u8	asize;
};

/**
 * struct aemif_device: structure to hold device data
 * @base: base address of AEMIF registers
 * @clk: source clock
 * @clk_rate: clock's rate in kHz
 * @num_cs: number of assigned chip-selects
 * @cs_offset: start number of cs nodes
 * @cs_data: array of chip-select settings
 * @config_cs_lock: lock used to access CS configuration
 */
struct aemif_device {
	void __iomem *base;
	struct clk *clk;
	unsigned long clk_rate;
	u8 num_cs;
	int cs_offset;
	struct aemif_cs_data cs_data[NUM_CS];
	struct mutex config_cs_lock;
};

/**
 * aemif_check_cs_timings() - Check the validity of a CS timing configuration.
 * @timings: timings configuration
 *
 * @return: 0 if the timing configuration is valid, negative error number otherwise.
 */
int aemif_check_cs_timings(struct aemif_cs_timings *timings)
{
	if (timings->ta > TA_MAX)
		return -EINVAL;

	if (timings->rhold > RHOLD_MAX)
		return -EINVAL;

	if (timings->rstrobe > RSTROBE_MAX)
		return -EINVAL;

	if (timings->rsetup > RSETUP_MAX)
		return -EINVAL;

	if (timings->whold > WHOLD_MAX)
		return -EINVAL;

	if (timings->wstrobe > WSTROBE_MAX)
		return -EINVAL;

	if (timings->wsetup > WSETUP_MAX)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(aemif_check_cs_timings);

/**
 * aemif_set_cs_timings() - Set the timing configuration of a given chip select.
 * @aemif: aemif device to configure
 * @cs: index of the chip select to configure
 * @timings: timings configuration to set
 *
 * @return: 0 on success, else negative errno.
 */
int aemif_set_cs_timings(struct aemif_device *aemif, u8 cs,
			 struct aemif_cs_timings *timings)
{
	unsigned int offset;
	u32 val, set;
	int ret;

	if (!timings || !aemif)
		return -EINVAL;

	if (cs > aemif->num_cs)
		return -EINVAL;

	ret = aemif_check_cs_timings(timings);
	if (ret)
		return ret;

	set = TA(timings->ta) | RHOLD(timings->rhold) | RSTROBE(timings->rstrobe) |
	      RSETUP(timings->rsetup) | WHOLD(timings->whold) |
	      WSTROBE(timings->wstrobe) | WSETUP(timings->wsetup);

	offset = A1CR_OFFSET + cs * 4;

	mutex_lock(&aemif->config_cs_lock);
	val = readl(aemif->base + offset);
	val &= ~TIMINGS_MASK;
	val |= set;
	writel(val, aemif->base + offset);
	mutex_unlock(&aemif->config_cs_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(aemif_set_cs_timings);

/**
 * aemif_calc_rate - calculate timing data.
 * @pdev: platform device to calculate for
 * @wanted: The cycle time needed in nanoseconds.
 * @clk: The input clock rate in kHz.
 *
 * @return: the calculated timing value minus 1 for easy
 * programming into AEMIF timing registers.
 */
static u32 aemif_calc_rate(struct platform_device *pdev, int wanted, unsigned long clk)
{
	int result;

	result = DIV_ROUND_UP((wanted * clk), NSEC_PER_MSEC) - 1;

	dev_dbg(&pdev->dev, "%s: result %d from %ld, %d\n", __func__, result,
		clk, wanted);

	/* It is generally OK to have a more relaxed timing than requested... */
	if (result < 0)
		result = 0;

	return result;
}

/**
 * aemif_config_abus - configure async bus parameters
 * @pdev: platform device to configure for
 * @csnum: aemif chip select number
 *
 * This function programs the given timing values (in real clock) into the
 * AEMIF registers taking the AEMIF clock into account.
 *
 * This function does not use any locking while programming the AEMIF
 * because it is expected that there is only one user of a given
 * chip-select.
 *
 * Returns 0 on success, else negative errno.
 */
static int aemif_config_abus(struct platform_device *pdev, int csnum)
{
	struct aemif_device *aemif = platform_get_drvdata(pdev);
	struct aemif_cs_data *data = &aemif->cs_data[csnum];
	unsigned offset;
	u32 set, val;

	offset = A1CR_OFFSET + (data->cs - aemif->cs_offset) * 4;

	set = (data->asize & ACR_ASIZE_MASK);
	if (data->enable_ew)
		set |= ACR_EW_MASK;
	if (data->enable_ss)
		set |= ACR_SSTROBE_MASK;

	mutex_lock(&aemif->config_cs_lock);
	val = readl(aemif->base + offset);
	val &= ~CONFIG_MASK;
	val |= set;
	writel(val, aemif->base + offset);
	mutex_unlock(&aemif->config_cs_lock);

	return aemif_set_cs_timings(aemif, data->cs - aemif->cs_offset, &data->timings);
}

/**
 * aemif_get_hw_params - function to read hw register values
 * @pdev: platform device to read for
 * @csnum: aemif chip select number
 *
 * This function reads the defaults from the registers and update
 * the timing values. Required for get/set commands and also for
 * the case when driver needs to use defaults in hardware.
 */
static void aemif_get_hw_params(struct platform_device *pdev, int csnum)
{
	struct aemif_device *aemif = platform_get_drvdata(pdev);
	struct aemif_cs_data *data = &aemif->cs_data[csnum];
	u32 val, offset;

	offset = A1CR_OFFSET + (data->cs - aemif->cs_offset) * 4;
	val = readl(aemif->base + offset);

	data->timings.ta = TA_VAL(val);
	data->timings.rhold = RHOLD_VAL(val);
	data->timings.rstrobe = RSTROBE_VAL(val);
	data->timings.rsetup = RSETUP_VAL(val);
	data->timings.whold = WHOLD_VAL(val);
	data->timings.wstrobe = WSTROBE_VAL(val);
	data->timings.wsetup = WSETUP_VAL(val);
	data->enable_ew = EW_VAL(val);
	data->enable_ss = SSTROBE_VAL(val);
	data->asize = val & ASIZE_MAX;
}

/**
 * of_aemif_parse_abus_config - parse CS configuration from DT
 * @pdev: platform device to parse for
 * @np: device node ptr
 *
 * This function update the emif async bus configuration based on the values
 * configured in a cs device binding node.
 */
static int of_aemif_parse_abus_config(struct platform_device *pdev,
				      struct device_node *np)
{
	struct aemif_device *aemif = platform_get_drvdata(pdev);
	unsigned long clk_rate = aemif->clk_rate;
	struct aemif_cs_data *data;
	u32 cs;
	u32 val;

	if (of_property_read_u32(np, "ti,cs-chipselect", &cs)) {
		dev_dbg(&pdev->dev, "cs property is required");
		return -EINVAL;
	}

	if (cs - aemif->cs_offset >= NUM_CS || cs < aemif->cs_offset) {
		dev_dbg(&pdev->dev, "cs number is incorrect %d", cs);
		return -EINVAL;
	}

	if (aemif->num_cs >= NUM_CS) {
		dev_dbg(&pdev->dev, "cs count is more than %d", NUM_CS);
		return -EINVAL;
	}

	data = &aemif->cs_data[aemif->num_cs];
	data->cs = cs;

	/* read the current value in the hw register */
	aemif_get_hw_params(pdev, aemif->num_cs++);

	/* override the values from device node */
	if (!of_property_read_u32(np, "ti,cs-min-turnaround-ns", &val))
		data->timings.ta = aemif_calc_rate(pdev, val, clk_rate);

	if (!of_property_read_u32(np, "ti,cs-read-hold-ns", &val))
		data->timings.rhold = aemif_calc_rate(pdev, val, clk_rate);

	if (!of_property_read_u32(np, "ti,cs-read-strobe-ns", &val))
		data->timings.rstrobe = aemif_calc_rate(pdev, val, clk_rate);

	if (!of_property_read_u32(np, "ti,cs-read-setup-ns", &val))
		data->timings.rsetup = aemif_calc_rate(pdev, val, clk_rate);

	if (!of_property_read_u32(np, "ti,cs-write-hold-ns", &val))
		data->timings.whold = aemif_calc_rate(pdev, val, clk_rate);

	if (!of_property_read_u32(np, "ti,cs-write-strobe-ns", &val))
		data->timings.wstrobe = aemif_calc_rate(pdev, val, clk_rate);

	if (!of_property_read_u32(np, "ti,cs-write-setup-ns", &val))
		data->timings.wsetup = aemif_calc_rate(pdev, val, clk_rate);

	if (!of_property_read_u32(np, "ti,cs-bus-width", &val))
		if (val == 16)
			data->asize = 1;
	data->enable_ew = of_property_read_bool(np, "ti,cs-extended-wait-mode");
	data->enable_ss = of_property_read_bool(np, "ti,cs-select-strobe-mode");

	return aemif_check_cs_timings(&data->timings);
}

static const struct of_device_id aemif_of_match[] = {
	{ .compatible = "ti,davinci-aemif", },
	{ .compatible = "ti,da850-aemif", },
	{},
};
MODULE_DEVICE_TABLE(of, aemif_of_match);

static int aemif_probe(struct platform_device *pdev)
{
	int i;
	int ret = -ENODEV;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct aemif_device *aemif;

	aemif = devm_kzalloc(dev, sizeof(*aemif), GFP_KERNEL);
	if (!aemif)
		return -ENOMEM;

	platform_set_drvdata(pdev, aemif);

	aemif->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(aemif->clk))
		return dev_err_probe(dev, PTR_ERR(aemif->clk),
				     "cannot get clock 'aemif'\n");

	aemif->clk_rate = clk_get_rate(aemif->clk) / MSEC_PER_SEC;

	if (np && of_device_is_compatible(np, "ti,da850-aemif"))
		aemif->cs_offset = 2;

	aemif->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(aemif->base))
		return PTR_ERR(aemif->base);

	mutex_init(&aemif->config_cs_lock);
	if (np) {
		/*
		 * For every controller device node, there is a cs device node
		 * that describe the bus configuration parameters. This
		 * functions iterate over these nodes and update the cs data
		 * array.
		 */
		for_each_available_child_of_node_scoped(np, child_np) {
			ret = of_aemif_parse_abus_config(pdev, child_np);
			if (ret < 0)
				return ret;
		}
	}

	for (i = 0; i < aemif->num_cs; i++) {
		ret = aemif_config_abus(pdev, i);
		if (ret < 0) {
			dev_err(dev, "Error configuring chip select %d\n",
				aemif->cs_data[i].cs);
			return ret;
		}
	}

	/*
	 * Create a child devices explicitly from here to guarantee that the
	 * child will be probed after the AEMIF timing parameters are set.
	 */
	if (np) {
		for_each_available_child_of_node_scoped(np, child_np) {
			ret = of_platform_populate(child_np, NULL, NULL, dev);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static struct platform_driver aemif_driver = {
	.probe = aemif_probe,
	.driver = {
		.name = "ti-aemif",
		.of_match_table = of_match_ptr(aemif_of_match),
	},
};

module_platform_driver(aemif_driver);

MODULE_AUTHOR("Murali Karicheri <m-karicheri2@ti.com>");
MODULE_AUTHOR("Ivan Khoronzhuk <ivan.khoronzhuk@ti.com>");
MODULE_DESCRIPTION("Texas Instruments AEMIF driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" KBUILD_MODNAME);
