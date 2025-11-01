// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>

#include <dt-bindings/clock/maxim,max9485.h>

#define MAX9485_NUM_CLKS 4

/* This chip has only one register of 8 bit width. */

#define MAX9485_FS_12KHZ	(0 << 0)
#define MAX9485_FS_32KHZ	(1 << 0)
#define MAX9485_FS_44_1KHZ	(2 << 0)
#define MAX9485_FS_48KHZ	(3 << 0)

#define MAX9485_SCALE_256	(0 << 2)
#define MAX9485_SCALE_384	(1 << 2)
#define MAX9485_SCALE_768	(2 << 2)

#define MAX9485_DOUBLE		BIT(4)
#define MAX9485_CLKOUT1_ENABLE	BIT(5)
#define MAX9485_CLKOUT2_ENABLE	BIT(6)
#define MAX9485_MCLK_ENABLE	BIT(7)
#define MAX9485_FREQ_MASK	0x1f

struct max9485_rate {
	unsigned long out;
	u8 reg_value;
};

/*
 * Ordered by frequency. For frequency the hardware can generate with
 * multiple settings, the one with lowest jitter is listed first.
 */
static const struct max9485_rate max9485_rates[] = {
	{  3072000, MAX9485_FS_12KHZ   | MAX9485_SCALE_256 },
	{  4608000, MAX9485_FS_12KHZ   | MAX9485_SCALE_384 },
	{  8192000, MAX9485_FS_32KHZ   | MAX9485_SCALE_256 },
	{  9126000, MAX9485_FS_12KHZ   | MAX9485_SCALE_768 },
	{ 11289600, MAX9485_FS_44_1KHZ | MAX9485_SCALE_256 },
	{ 12288000, MAX9485_FS_48KHZ   | MAX9485_SCALE_256 },
	{ 12288000, MAX9485_FS_32KHZ   | MAX9485_SCALE_384 },
	{ 16384000, MAX9485_FS_32KHZ   | MAX9485_SCALE_256 | MAX9485_DOUBLE },
	{ 16934400, MAX9485_FS_44_1KHZ | MAX9485_SCALE_384 },
	{ 18384000, MAX9485_FS_48KHZ   | MAX9485_SCALE_384 },
	{ 22579200, MAX9485_FS_44_1KHZ | MAX9485_SCALE_256 | MAX9485_DOUBLE },
	{ 24576000, MAX9485_FS_48KHZ   | MAX9485_SCALE_256 | MAX9485_DOUBLE },
	{ 24576000, MAX9485_FS_32KHZ   | MAX9485_SCALE_384 | MAX9485_DOUBLE },
	{ 24576000, MAX9485_FS_32KHZ   | MAX9485_SCALE_768 },
	{ 33868800, MAX9485_FS_44_1KHZ | MAX9485_SCALE_384 | MAX9485_DOUBLE },
	{ 33868800, MAX9485_FS_44_1KHZ | MAX9485_SCALE_768 },
	{ 36864000, MAX9485_FS_48KHZ   | MAX9485_SCALE_384 | MAX9485_DOUBLE },
	{ 36864000, MAX9485_FS_48KHZ   | MAX9485_SCALE_768 },
	{ 49152000, MAX9485_FS_32KHZ   | MAX9485_SCALE_768 | MAX9485_DOUBLE },
	{ 67737600, MAX9485_FS_44_1KHZ | MAX9485_SCALE_768 | MAX9485_DOUBLE },
	{ 73728000, MAX9485_FS_48KHZ   | MAX9485_SCALE_768 | MAX9485_DOUBLE },
	{ } /* sentinel */
};

struct max9485_driver_data;

struct max9485_clk_hw {
	struct clk_hw hw;
	struct clk_init_data init;
	u8 enable_bit;
	struct max9485_driver_data *drvdata;
};

struct max9485_driver_data {
	struct clk *xclk;
	struct i2c_client *client;
	u8 reg_value;
	struct regulator *supply;
	struct gpio_desc *reset_gpio;
	struct max9485_clk_hw hw[MAX9485_NUM_CLKS];
};

static inline struct max9485_clk_hw *to_max9485_clk(struct clk_hw *hw)
{
	return container_of(hw, struct max9485_clk_hw, hw);
}

static int max9485_update_bits(struct max9485_driver_data *drvdata,
			       u8 mask, u8 value)
{
	int ret;

	drvdata->reg_value &= ~mask;
	drvdata->reg_value |= value;

	dev_dbg(&drvdata->client->dev,
		"updating mask 0x%02x value 0x%02x -> 0x%02x\n",
		mask, value, drvdata->reg_value);

	ret = i2c_master_send(drvdata->client,
			      &drvdata->reg_value,
			      sizeof(drvdata->reg_value));

	return ret < 0 ? ret : 0;
}

static int max9485_clk_prepare(struct clk_hw *hw)
{
	struct max9485_clk_hw *clk_hw = to_max9485_clk(hw);

	return max9485_update_bits(clk_hw->drvdata,
				   clk_hw->enable_bit,
				   clk_hw->enable_bit);
}

static void max9485_clk_unprepare(struct clk_hw *hw)
{
	struct max9485_clk_hw *clk_hw = to_max9485_clk(hw);

	max9485_update_bits(clk_hw->drvdata, clk_hw->enable_bit, 0);
}

/*
 * CLKOUT - configurable clock output
 */
static int max9485_clkout_set_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long parent_rate)
{
	struct max9485_clk_hw *clk_hw = to_max9485_clk(hw);
	const struct max9485_rate *entry;

	for (entry = max9485_rates; entry->out != 0; entry++)
		if (entry->out == rate)
			break;

	if (entry->out == 0)
		return -EINVAL;

	return max9485_update_bits(clk_hw->drvdata,
				   MAX9485_FREQ_MASK,
				   entry->reg_value);
}

static unsigned long max9485_clkout_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct max9485_clk_hw *clk_hw = to_max9485_clk(hw);
	struct max9485_driver_data *drvdata = clk_hw->drvdata;
	u8 val = drvdata->reg_value & MAX9485_FREQ_MASK;
	const struct max9485_rate *entry;

	for (entry = max9485_rates; entry->out != 0; entry++)
		if (val == entry->reg_value)
			return entry->out;

	return 0;
}

static int max9485_clkout_determine_rate(struct clk_hw *hw,
					 struct clk_rate_request *req)
{
	const struct max9485_rate *curr, *prev = NULL;

	for (curr = max9485_rates; curr->out != 0; curr++) {
		/* Exact matches */
		if (curr->out == req->rate)
			return 0;

		/*
		 * Find the first entry that has a frequency higher than the
		 * requested one.
		 */
		if (curr->out > req->rate) {
			unsigned int mid;

			/*
			 * If this is the first entry, clamp the value to the
			 * lowest possible frequency.
			 */
			if (!prev) {
				req->rate = curr->out;

				return 0;
			}

			/*
			 * Otherwise, determine whether the previous entry or
			 * current one is closer.
			 */
			mid = prev->out + ((curr->out - prev->out) / 2);

			req->rate = mid > req->rate ? prev->out : curr->out;

			return 0;
		}

		prev = curr;
	}

	/* If the last entry was still too high, clamp the value */
	req->rate = prev->out;

	return 0;
}

struct max9485_clk {
	const char *name;
	int parent_index;
	const struct clk_ops ops;
	u8 enable_bit;
};

static const struct max9485_clk max9485_clks[MAX9485_NUM_CLKS] = {
	[MAX9485_MCLKOUT] = {
		.name = "mclkout",
		.parent_index = -1,
		.enable_bit = MAX9485_MCLK_ENABLE,
		.ops = {
			.prepare	= max9485_clk_prepare,
			.unprepare	= max9485_clk_unprepare,
		},
	},
	[MAX9485_CLKOUT] = {
		.name = "clkout",
		.parent_index = -1,
		.ops = {
			.set_rate	= max9485_clkout_set_rate,
			.determine_rate = max9485_clkout_determine_rate,
			.recalc_rate	= max9485_clkout_recalc_rate,
		},
	},
	[MAX9485_CLKOUT1] = {
		.name = "clkout1",
		.parent_index = MAX9485_CLKOUT,
		.enable_bit = MAX9485_CLKOUT1_ENABLE,
		.ops = {
			.prepare	= max9485_clk_prepare,
			.unprepare	= max9485_clk_unprepare,
		},
	},
	[MAX9485_CLKOUT2] = {
		.name = "clkout2",
		.parent_index = MAX9485_CLKOUT,
		.enable_bit = MAX9485_CLKOUT2_ENABLE,
		.ops = {
			.prepare	= max9485_clk_prepare,
			.unprepare	= max9485_clk_unprepare,
		},
	},
};

static struct clk_hw *
max9485_of_clk_get(struct of_phandle_args *clkspec, void *data)
{
	struct max9485_driver_data *drvdata = data;
	unsigned int idx = clkspec->args[0];

	return &drvdata->hw[idx].hw;
}

static int max9485_i2c_probe(struct i2c_client *client)
{
	struct max9485_driver_data *drvdata;
	struct device *dev = &client->dev;
	const char *xclk_name;
	int i, ret;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->xclk = devm_clk_get(dev, "xclk");
	if (IS_ERR(drvdata->xclk))
		return PTR_ERR(drvdata->xclk);

	xclk_name = __clk_get_name(drvdata->xclk);

	drvdata->supply = devm_regulator_get(dev, "vdd");
	if (IS_ERR(drvdata->supply))
		return PTR_ERR(drvdata->supply);

	ret = regulator_enable(drvdata->supply);
	if (ret < 0)
		return ret;

	drvdata->reset_gpio =
		devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(drvdata->reset_gpio))
		return PTR_ERR(drvdata->reset_gpio);

	i2c_set_clientdata(client, drvdata);
	drvdata->client = client;

	ret = i2c_master_recv(drvdata->client, &drvdata->reg_value,
			      sizeof(drvdata->reg_value));
	if (ret < 0) {
		dev_warn(dev, "Unable to read device register: %d\n", ret);
		return ret;
	}

	for (i = 0; i < MAX9485_NUM_CLKS; i++) {
		int parent_index = max9485_clks[i].parent_index;
		const char *name;

		if (of_property_read_string_index(dev->of_node,
						  "clock-output-names",
						  i, &name) == 0) {
			drvdata->hw[i].init.name = name;
		} else {
			drvdata->hw[i].init.name = max9485_clks[i].name;
		}

		drvdata->hw[i].init.ops = &max9485_clks[i].ops;
		drvdata->hw[i].init.num_parents = 1;
		drvdata->hw[i].init.flags = 0;

		if (parent_index > 0) {
			drvdata->hw[i].init.parent_names =
				&drvdata->hw[parent_index].init.name;
			drvdata->hw[i].init.flags |= CLK_SET_RATE_PARENT;
		} else {
			drvdata->hw[i].init.parent_names = &xclk_name;
		}

		drvdata->hw[i].enable_bit = max9485_clks[i].enable_bit;
		drvdata->hw[i].hw.init = &drvdata->hw[i].init;
		drvdata->hw[i].drvdata = drvdata;

		ret = devm_clk_hw_register(dev, &drvdata->hw[i].hw);
		if (ret < 0)
			return ret;
	}

	return devm_of_clk_add_hw_provider(dev, max9485_of_clk_get, drvdata);
}

static int __maybe_unused max9485_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max9485_driver_data *drvdata = i2c_get_clientdata(client);

	gpiod_set_value_cansleep(drvdata->reset_gpio, 0);

	return 0;
}

static int __maybe_unused max9485_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max9485_driver_data *drvdata = i2c_get_clientdata(client);
	int ret;

	gpiod_set_value_cansleep(drvdata->reset_gpio, 1);

	ret = i2c_master_send(client, &drvdata->reg_value,
			      sizeof(drvdata->reg_value));

	return ret < 0 ? ret : 0;
}

static const struct dev_pm_ops max9485_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(max9485_suspend, max9485_resume)
};

static const struct of_device_id max9485_dt_ids[] = {
	{ .compatible = "maxim,max9485", },
	{ }
};
MODULE_DEVICE_TABLE(of, max9485_dt_ids);

static const struct i2c_device_id max9485_i2c_ids[] = {
	{ .name = "max9485", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max9485_i2c_ids);

static struct i2c_driver max9485_driver = {
	.driver = {
		.name		= "max9485",
		.pm		= &max9485_pm_ops,
		.of_match_table	= max9485_dt_ids,
	},
	.probe = max9485_i2c_probe,
	.id_table = max9485_i2c_ids,
};
module_i2c_driver(max9485_driver);

MODULE_AUTHOR("Daniel Mack <daniel@zonque.org>");
MODULE_DESCRIPTION("MAX9485 Programmable Audio Clock Generator");
MODULE_LICENSE("GPL v2");
