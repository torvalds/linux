/*
 * Core driver for STw4810/STw4811
 *
 * Copyright (C) 2013 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/mfd/core.h>
#include <linux/mfd/stw481x.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

/*
 * This driver can only access the non-USB portions of STw4811, the register
 * range 0x00-0x10 dealing with USB is bound to the two special I2C pins used
 * for USB control.
 */

/* Registers inside the power control address space */
#define STW_PC_VCORE_SEL	0x05U
#define STW_PC_VAUX_SEL		0x06U
#define STW_PC_VPLL_SEL		0x07U

/**
 * stw481x_get_pctl_reg() - get a power control register
 * @stw481x: handle to the stw481x chip
 * @reg: power control register to fetch
 *
 * The power control registers is a set of one-time-programmable registers
 * in its own register space, accessed by writing addess bits to these
 * two registers: bits 7,6,5 of PCTL_REG_LO corresponds to the 3 LSBs of
 * the address and bits 8,9 of PCTL_REG_HI corresponds to the 2 MSBs of
 * the address, forming an address space of 5 bits, i.e. 32 registers
 * 0x00 ... 0x1f can be obtained.
 */
static int stw481x_get_pctl_reg(struct stw481x *stw481x, u8 reg)
{
	u8 msb = (reg >> 3) & 0x03;
	u8 lsb = (reg << 5) & 0xe0;
	unsigned int val;
	u8 vrfy;
	int ret;

	ret = regmap_write(stw481x->map, STW_PCTL_REG_HI, msb);
	if (ret)
		return ret;
	ret = regmap_write(stw481x->map, STW_PCTL_REG_LO, lsb);
	if (ret)
		return ret;
	ret = regmap_read(stw481x->map, STW_PCTL_REG_HI, &val);
	if (ret)
		return ret;
	vrfy = (val & 0x03) << 3;
	ret = regmap_read(stw481x->map, STW_PCTL_REG_LO, &val);
	if (ret)
		return ret;
	vrfy |= ((val >> 5) & 0x07);
	if (vrfy != reg)
		return -EIO;
	return (val >> 1) & 0x0f;
}

static int stw481x_startup(struct stw481x *stw481x)
{
	/* Voltages multiplied by 100 */
	u8 vcore_val[] = { 100, 105, 110, 115, 120, 122, 124, 126, 128,
			   130, 132, 134, 136, 138, 140, 145 };
	u8 vpll_val[] = { 105, 120, 130, 180 };
	u8 vaux_val[] = { 15, 18, 25, 28 };
	u8 vcore;
	u8 vcore_slp;
	u8 vpll;
	u8 vaux;
	bool vaux_en;
	bool it_warn;
	int ret;
	unsigned int val;

	ret = regmap_read(stw481x->map, STW_CONF1, &val);
	if (ret)
		return ret;
	vaux_en = !!(val & STW_CONF1_PDN_VAUX);
	it_warn = !!(val & STW_CONF1_IT_WARN);

	dev_info(&stw481x->client->dev, "voltages %s\n",
		(val & STW_CONF1_V_MONITORING) ? "OK" : "LOW");
	dev_info(&stw481x->client->dev, "MMC level shifter %s\n",
		(val & STW_CONF1_MMC_LS_STATUS) ? "high impedance" : "ON");
	dev_info(&stw481x->client->dev, "VMMC: %s\n",
		(val & STW_CONF1_PDN_VMMC) ? "ON" : "disabled");

	dev_info(&stw481x->client->dev, "STw481x power control registers:\n");

	ret = stw481x_get_pctl_reg(stw481x, STW_PC_VCORE_SEL);
	if (ret < 0)
		return ret;
	vcore = ret & 0x0f;

	ret = stw481x_get_pctl_reg(stw481x, STW_PC_VAUX_SEL);
	if (ret < 0)
		return ret;
	vaux = (ret >> 2) & 3;
	vpll = (ret >> 4) & 1; /* Save bit 4 */

	ret = stw481x_get_pctl_reg(stw481x, STW_PC_VPLL_SEL);
	if (ret < 0)
		return ret;
	vpll |= (ret >> 1) & 2;

	dev_info(&stw481x->client->dev, "VCORE: %u.%uV %s\n",
		vcore_val[vcore] / 100, vcore_val[vcore] % 100,
		(ret & 4) ? "ON" : "OFF");

	dev_info(&stw481x->client->dev, "VPLL:  %u.%uV %s\n",
		vpll_val[vpll] / 100, vpll_val[vpll] % 100,
		(ret & 0x10) ? "ON" : "OFF");

	dev_info(&stw481x->client->dev, "VAUX:  %u.%uV %s\n",
		vaux_val[vaux] / 10, vaux_val[vaux] % 10,
		vaux_en ? "ON" : "OFF");

	ret = regmap_read(stw481x->map, STW_CONF2, &val);
	if (ret)
		return ret;

	dev_info(&stw481x->client->dev, "TWARN: %s threshold, %s\n",
		it_warn ? "below" : "above",
		(val & STW_CONF2_MASK_TWARN) ?
		 "enabled" : "mask through VDDOK");
	dev_info(&stw481x->client->dev, "VMMC: %s\n",
		(val & STW_CONF2_VMMC_EXT) ? "internal" : "external");
	dev_info(&stw481x->client->dev, "IT WAKE UP: %s\n",
		(val & STW_CONF2_MASK_IT_WAKE_UP) ? "enabled" : "masked");
	dev_info(&stw481x->client->dev, "GPO1: %s\n",
		(val & STW_CONF2_GPO1) ? "low" : "high impedance");
	dev_info(&stw481x->client->dev, "GPO2: %s\n",
		(val & STW_CONF2_GPO2) ? "low" : "high impedance");

	ret = regmap_read(stw481x->map, STW_VCORE_SLEEP, &val);
	if (ret)
		return ret;
	vcore_slp = val & 0x0f;
	dev_info(&stw481x->client->dev, "VCORE SLEEP: %u.%uV\n",
		vcore_val[vcore_slp] / 100, vcore_val[vcore_slp] % 100);

	return 0;
}

/*
 * MFD cells - we have one cell which is selected operation
 * mode, and we always have a GPIO cell.
 */
static struct mfd_cell stw481x_cells[] = {
	{
		.of_compatible = "st,stw481x-vmmc",
		.name = "stw481x-vmmc-regulator",
		.id = -1,
	},
};

static const struct regmap_config stw481x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int stw481x_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct stw481x			*stw481x;
	int ret;
	int i;

	stw481x = devm_kzalloc(&client->dev, sizeof(*stw481x), GFP_KERNEL);
	if (!stw481x)
		return -ENOMEM;

	i2c_set_clientdata(client, stw481x);
	stw481x->client = client;
	stw481x->map = devm_regmap_init_i2c(client, &stw481x_regmap_config);
	if (IS_ERR(stw481x->map)) {
		ret = PTR_ERR(stw481x->map);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	ret = stw481x_startup(stw481x);
	if (ret) {
		dev_err(&client->dev, "chip initialization failed\n");
		return ret;
	}

	/* Set up and register the platform devices. */
	for (i = 0; i < ARRAY_SIZE(stw481x_cells); i++) {
		/* One state holder for all drivers, this is simple */
		stw481x_cells[i].platform_data = stw481x;
		stw481x_cells[i].pdata_size = sizeof(*stw481x);
	}

	ret = devm_mfd_add_devices(&client->dev, 0, stw481x_cells,
				   ARRAY_SIZE(stw481x_cells), NULL, 0, NULL);
	if (ret)
		return ret;

	dev_info(&client->dev, "initialized STw481x device\n");

	return ret;
}

/*
 * This ID table is completely unused, as this is a pure
 * device-tree probed driver, but it has to be here due to
 * the structure of the I2C core.
 */
static const struct i2c_device_id stw481x_id[] = {
	{ "stw481x", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, stw481x_id);

static const struct of_device_id stw481x_match[] = {
	{ .compatible = "st,stw4810", },
	{ .compatible = "st,stw4811", },
	{ },
};
MODULE_DEVICE_TABLE(of, stw481x_match);

static struct i2c_driver stw481x_driver = {
	.driver = {
		.name	= "stw481x",
		.of_match_table = stw481x_match,
	},
	.probe		= stw481x_probe,
	.id_table	= stw481x_id,
};

module_i2c_driver(stw481x_driver);

MODULE_AUTHOR("Linus Walleij");
MODULE_DESCRIPTION("STw481x PMIC driver");
MODULE_LICENSE("GPL v2");
