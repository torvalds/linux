// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Renesas 9-series PCIe clock generator driver
 *
 * The following series can be supported:
 *   - 9FGV/9DBV/9DMV/9FGL/9DML/9QXL/9SQ
 * Currently supported:
 *   - 9FGV0241
 *   - 9FGV0441
 *
 * Copyright (C) 2022 Marek Vasut <marex@denx.de>
 */

#include <linux/clk-provider.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

#define RS9_REG_OE				0x0
#define RS9_REG_SS				0x1
#define RS9_REG_SS_AMP_0V6			0x0
#define RS9_REG_SS_AMP_0V7			0x1
#define RS9_REG_SS_AMP_0V8			0x2
#define RS9_REG_SS_AMP_0V9			0x3
#define RS9_REG_SS_AMP_MASK			0x3
#define RS9_REG_SS_SSC_100			0
#define RS9_REG_SS_SSC_M025			(1 << 3)
#define RS9_REG_SS_SSC_M050			(3 << 3)
#define RS9_REG_SS_SSC_MASK			(3 << 3)
#define RS9_REG_SS_SSC_LOCK			BIT(5)
#define RS9_REG_SR				0x2
#define RS9_REG_REF				0x3
#define RS9_REG_REF_OE				BIT(4)
#define RS9_REG_REF_OD				BIT(5)
#define RS9_REG_REF_SR_SLOWEST			0
#define RS9_REG_REF_SR_SLOW			(1 << 6)
#define RS9_REG_REF_SR_FAST			(2 << 6)
#define RS9_REG_REF_SR_FASTER			(3 << 6)
#define RS9_REG_VID				0x5
#define RS9_REG_DID				0x6
#define RS9_REG_BCP				0x7

#define RS9_REG_VID_IDT				0x01

#define RS9_REG_DID_TYPE_FGV			(0x0 << RS9_REG_DID_TYPE_SHIFT)
#define RS9_REG_DID_TYPE_DBV			(0x1 << RS9_REG_DID_TYPE_SHIFT)
#define RS9_REG_DID_TYPE_DMV			(0x2 << RS9_REG_DID_TYPE_SHIFT)
#define RS9_REG_DID_TYPE_SHIFT			0x6

/* Supported Renesas 9-series models. */
enum rs9_model {
	RENESAS_9FGV0241,
	RENESAS_9FGV0441,
};

/* Structure to describe features of a particular 9-series model */
struct rs9_chip_info {
	const enum rs9_model	model;
	unsigned int		num_clks;
	u8			did;
};

struct rs9_driver_data {
	struct i2c_client	*client;
	struct regmap		*regmap;
	const struct rs9_chip_info *chip_info;
	struct clk_hw		*clk_dif[4];
	u8			pll_amplitude;
	u8			pll_ssc;
	u8			clk_dif_sr;
};

/*
 * Renesas 9-series i2c regmap
 */
static const struct regmap_range rs9_readable_ranges[] = {
	regmap_reg_range(RS9_REG_OE, RS9_REG_REF),
	regmap_reg_range(RS9_REG_VID, RS9_REG_BCP),
};

static const struct regmap_access_table rs9_readable_table = {
	.yes_ranges = rs9_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(rs9_readable_ranges),
};

static const struct regmap_range rs9_writeable_ranges[] = {
	regmap_reg_range(RS9_REG_OE, RS9_REG_REF),
	regmap_reg_range(RS9_REG_BCP, RS9_REG_BCP),
};

static const struct regmap_access_table rs9_writeable_table = {
	.yes_ranges = rs9_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(rs9_writeable_ranges),
};

static int rs9_regmap_i2c_write(void *context,
				unsigned int reg, unsigned int val)
{
	struct i2c_client *i2c = context;
	const u8 data[3] = { reg, 1, val };
	const int count = ARRAY_SIZE(data);
	int ret;

	ret = i2c_master_send(i2c, data, count);
	if (ret == count)
		return 0;
	else if (ret < 0)
		return ret;
	else
		return -EIO;
}

static int rs9_regmap_i2c_read(void *context,
			       unsigned int reg, unsigned int *val)
{
	struct i2c_client *i2c = context;
	struct i2c_msg xfer[2];
	u8 txdata = reg;
	u8 rxdata[2];
	int ret;

	xfer[0].addr = i2c->addr;
	xfer[0].flags = 0;
	xfer[0].len = 1;
	xfer[0].buf = (void *)&txdata;

	xfer[1].addr = i2c->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = 2;
	xfer[1].buf = (void *)rxdata;

	ret = i2c_transfer(i2c->adapter, xfer, 2);
	if (ret < 0)
		return ret;
	if (ret != 2)
		return -EIO;

	/*
	 * Byte 0 is transfer length, which is always 1 due
	 * to BCP register programming to 1 in rs9_probe(),
	 * ignore it and use data from Byte 1.
	 */
	*val = rxdata[1];
	return 0;
}

static const struct regmap_config rs9_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_FLAT,
	.max_register = RS9_REG_BCP,
	.num_reg_defaults_raw = 0x8,
	.rd_table = &rs9_readable_table,
	.wr_table = &rs9_writeable_table,
	.reg_write = rs9_regmap_i2c_write,
	.reg_read = rs9_regmap_i2c_read,
};

static u8 rs9_calc_dif(const struct rs9_driver_data *rs9, int idx)
{
	enum rs9_model model = rs9->chip_info->model;

	if (model == RENESAS_9FGV0241)
		return BIT(idx) + 1;
	else if (model == RENESAS_9FGV0441)
		return BIT(idx);

	return 0;
}

static int rs9_get_output_config(struct rs9_driver_data *rs9, int idx)
{
	struct i2c_client *client = rs9->client;
	u8 dif = rs9_calc_dif(rs9, idx);
	unsigned char name[5] = "DIF0";
	struct device_node *np;
	int ret;
	u32 sr;

	/* Set defaults */
	rs9->clk_dif_sr |= dif;

	snprintf(name, 5, "DIF%d", idx);
	np = of_get_child_by_name(client->dev.of_node, name);
	if (!np)
		return 0;

	/* Output clock slew rate */
	ret = of_property_read_u32(np, "renesas,slew-rate", &sr);
	of_node_put(np);
	if (!ret) {
		if (sr == 2000000) {		/* 2V/ns */
			rs9->clk_dif_sr &= ~dif;
		} else if (sr == 3000000) {	/* 3V/ns (default) */
			rs9->clk_dif_sr |= dif;
		} else
			ret = dev_err_probe(&client->dev, -EINVAL,
					    "Invalid renesas,slew-rate value\n");
	}

	return ret;
}

static int rs9_get_common_config(struct rs9_driver_data *rs9)
{
	struct i2c_client *client = rs9->client;
	struct device_node *np = client->dev.of_node;
	unsigned int amp, ssc;
	int ret;

	/* Set defaults */
	rs9->pll_amplitude = RS9_REG_SS_AMP_0V7;
	rs9->pll_ssc = RS9_REG_SS_SSC_100;

	/* Output clock amplitude */
	ret = of_property_read_u32(np, "renesas,out-amplitude-microvolt",
				   &amp);
	if (!ret) {
		if (amp == 600000)	/* 0.6V */
			rs9->pll_amplitude = RS9_REG_SS_AMP_0V6;
		else if (amp == 700000)	/* 0.7V (default) */
			rs9->pll_amplitude = RS9_REG_SS_AMP_0V7;
		else if (amp == 800000)	/* 0.8V */
			rs9->pll_amplitude = RS9_REG_SS_AMP_0V8;
		else if (amp == 900000)	/* 0.9V */
			rs9->pll_amplitude = RS9_REG_SS_AMP_0V9;
		else
			return dev_err_probe(&client->dev, -EINVAL,
					     "Invalid renesas,out-amplitude-microvolt value\n");
	}

	/* Output clock spread spectrum */
	ret = of_property_read_u32(np, "renesas,out-spread-spectrum", &ssc);
	if (!ret) {
		if (ssc == 100000)	/* 100% ... no spread (default) */
			rs9->pll_ssc = RS9_REG_SS_SSC_100;
		else if (ssc == 99750)	/* -0.25% ... down spread */
			rs9->pll_ssc = RS9_REG_SS_SSC_M025;
		else if (ssc == 99500)	/* -0.50% ... down spread */
			rs9->pll_ssc = RS9_REG_SS_SSC_M050;
		else
			return dev_err_probe(&client->dev, -EINVAL,
					     "Invalid renesas,out-spread-spectrum value\n");
	}

	return 0;
}

static void rs9_update_config(struct rs9_driver_data *rs9)
{
	int i;

	/* If amplitude is non-default, update it. */
	if (rs9->pll_amplitude != RS9_REG_SS_AMP_0V7) {
		regmap_update_bits(rs9->regmap, RS9_REG_SS, RS9_REG_SS_AMP_MASK,
				   rs9->pll_amplitude);
	}

	/* If SSC is non-default, update it. */
	if (rs9->pll_ssc != RS9_REG_SS_SSC_100) {
		regmap_update_bits(rs9->regmap, RS9_REG_SS, RS9_REG_SS_SSC_MASK,
				   rs9->pll_ssc);
	}

	for (i = 0; i < rs9->chip_info->num_clks; i++) {
		u8 dif = rs9_calc_dif(rs9, i);

		if (rs9->clk_dif_sr & dif)
			continue;

		regmap_update_bits(rs9->regmap, RS9_REG_SR, dif,
				   rs9->clk_dif_sr & dif);
	}
}

static struct clk_hw *
rs9_of_clk_get(struct of_phandle_args *clkspec, void *data)
{
	struct rs9_driver_data *rs9 = data;
	unsigned int idx = clkspec->args[0];

	return rs9->clk_dif[idx];
}

static int rs9_probe(struct i2c_client *client)
{
	unsigned char name[5] = "DIF0";
	struct rs9_driver_data *rs9;
	unsigned int vid, did;
	struct clk_hw *hw;
	int i, ret;

	rs9 = devm_kzalloc(&client->dev, sizeof(*rs9), GFP_KERNEL);
	if (!rs9)
		return -ENOMEM;

	i2c_set_clientdata(client, rs9);
	rs9->client = client;
	rs9->chip_info = device_get_match_data(&client->dev);
	if (!rs9->chip_info)
		return -EINVAL;

	/* Fetch common configuration from DT (if specified) */
	ret = rs9_get_common_config(rs9);
	if (ret)
		return ret;

	/* Fetch DIFx output configuration from DT (if specified) */
	for (i = 0; i < rs9->chip_info->num_clks; i++) {
		ret = rs9_get_output_config(rs9, i);
		if (ret)
			return ret;
	}

	rs9->regmap = devm_regmap_init(&client->dev, NULL,
				       client, &rs9_regmap_config);
	if (IS_ERR(rs9->regmap))
		return dev_err_probe(&client->dev, PTR_ERR(rs9->regmap),
				     "Failed to allocate register map\n");

	/* Always read back 1 Byte via I2C */
	ret = regmap_write(rs9->regmap, RS9_REG_BCP, 1);
	if (ret < 0)
		return ret;

	ret = regmap_read(rs9->regmap, RS9_REG_VID, &vid);
	if (ret < 0)
		return ret;

	ret = regmap_read(rs9->regmap, RS9_REG_DID, &did);
	if (ret < 0)
		return ret;

	if (vid != RS9_REG_VID_IDT || did != rs9->chip_info->did)
		return dev_err_probe(&client->dev, -ENODEV,
				     "Incorrect VID/DID: %#02x, %#02x. Expected %#02x, %#02x\n",
				     vid, did, RS9_REG_VID_IDT,
				     rs9->chip_info->did);

	/* Register clock */
	for (i = 0; i < rs9->chip_info->num_clks; i++) {
		snprintf(name, 5, "DIF%d", i);
		hw = devm_clk_hw_register_fixed_factor_index(&client->dev, name,
						    0, 0, 4, 1);
		if (IS_ERR(hw))
			return PTR_ERR(hw);

		rs9->clk_dif[i] = hw;
	}

	ret = devm_of_clk_add_hw_provider(&client->dev, rs9_of_clk_get, rs9);
	if (!ret)
		rs9_update_config(rs9);

	return ret;
}

static int __maybe_unused rs9_suspend(struct device *dev)
{
	struct rs9_driver_data *rs9 = dev_get_drvdata(dev);

	regcache_cache_only(rs9->regmap, true);
	regcache_mark_dirty(rs9->regmap);

	return 0;
}

static int __maybe_unused rs9_resume(struct device *dev)
{
	struct rs9_driver_data *rs9 = dev_get_drvdata(dev);
	int ret;

	regcache_cache_only(rs9->regmap, false);
	ret = regcache_sync(rs9->regmap);
	if (ret)
		dev_err(dev, "Failed to restore register map: %d\n", ret);
	return ret;
}

static const struct rs9_chip_info renesas_9fgv0241_info = {
	.model		= RENESAS_9FGV0241,
	.num_clks	= 2,
	.did		= RS9_REG_DID_TYPE_FGV | 0x02,
};

static const struct rs9_chip_info renesas_9fgv0441_info = {
	.model		= RENESAS_9FGV0441,
	.num_clks	= 4,
	.did		= RS9_REG_DID_TYPE_FGV | 0x04,
};

static const struct i2c_device_id rs9_id[] = {
	{ "9fgv0241", .driver_data = (kernel_ulong_t)&renesas_9fgv0241_info },
	{ "9fgv0441", .driver_data = (kernel_ulong_t)&renesas_9fgv0441_info },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rs9_id);

static const struct of_device_id clk_rs9_of_match[] = {
	{ .compatible = "renesas,9fgv0241", .data = &renesas_9fgv0241_info },
	{ .compatible = "renesas,9fgv0441", .data = &renesas_9fgv0441_info },
	{ }
};
MODULE_DEVICE_TABLE(of, clk_rs9_of_match);

static SIMPLE_DEV_PM_OPS(rs9_pm_ops, rs9_suspend, rs9_resume);

static struct i2c_driver rs9_driver = {
	.driver = {
		.name = "clk-renesas-pcie-9series",
		.pm	= &rs9_pm_ops,
		.of_match_table = clk_rs9_of_match,
	},
	.probe		= rs9_probe,
	.id_table	= rs9_id,
};
module_i2c_driver(rs9_driver);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_DESCRIPTION("Renesas 9-series PCIe clock generator driver");
MODULE_LICENSE("GPL");
