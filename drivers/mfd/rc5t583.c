/*
 * Core driver access RC5T583 power management chip.
 *
 * Copyright (c) 2011-2012, NVIDIA CORPORATION.  All rights reserved.
 * Author: Laxman dewangan <ldewangan@nvidia.com>
 *
 * Based on code
 *	Copyright (C) 2011 RICOH COMPANY,LTD
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rc5t583.h>
#include <linux/regmap.h>

#define RICOH_ONOFFSEL_REG	0x10
#define RICOH_SWCTL_REG		0x5E

struct deepsleep_control_data {
	u8 reg_add;
	u8 ds_pos_bit;
};

#define DEEPSLEEP_INIT(_id, _reg, _pos)		\
	{					\
		.reg_add = RC5T583_##_reg,	\
		.ds_pos_bit = _pos,		\
	}

static struct deepsleep_control_data deepsleep_data[] = {
	DEEPSLEEP_INIT(DC0, SLPSEQ1, 0),
	DEEPSLEEP_INIT(DC1, SLPSEQ1, 4),
	DEEPSLEEP_INIT(DC2, SLPSEQ2, 0),
	DEEPSLEEP_INIT(DC3, SLPSEQ2, 4),
	DEEPSLEEP_INIT(LDO0, SLPSEQ3, 0),
	DEEPSLEEP_INIT(LDO1, SLPSEQ3, 4),
	DEEPSLEEP_INIT(LDO2, SLPSEQ4, 0),
	DEEPSLEEP_INIT(LDO3, SLPSEQ4, 4),
	DEEPSLEEP_INIT(LDO4, SLPSEQ5, 0),
	DEEPSLEEP_INIT(LDO5, SLPSEQ5, 4),
	DEEPSLEEP_INIT(LDO6, SLPSEQ6, 0),
	DEEPSLEEP_INIT(LDO7, SLPSEQ6, 4),
	DEEPSLEEP_INIT(LDO8, SLPSEQ7, 0),
	DEEPSLEEP_INIT(LDO9, SLPSEQ7, 4),
	DEEPSLEEP_INIT(PSO0, SLPSEQ8, 0),
	DEEPSLEEP_INIT(PSO1, SLPSEQ8, 4),
	DEEPSLEEP_INIT(PSO2, SLPSEQ9, 0),
	DEEPSLEEP_INIT(PSO3, SLPSEQ9, 4),
	DEEPSLEEP_INIT(PSO4, SLPSEQ10, 0),
	DEEPSLEEP_INIT(PSO5, SLPSEQ10, 4),
	DEEPSLEEP_INIT(PSO6, SLPSEQ11, 0),
	DEEPSLEEP_INIT(PSO7, SLPSEQ11, 4),
};

#define EXT_PWR_REQ		\
	(RC5T583_EXT_PWRREQ1_CONTROL | RC5T583_EXT_PWRREQ2_CONTROL)

static const struct mfd_cell rc5t583_subdevs[] = {
	{.name = "rc5t583-gpio",},
	{.name = "rc5t583-regulator",},
	{.name = "rc5t583-rtc",      },
	{.name = "rc5t583-key",      }
};

static int __rc5t583_set_ext_pwrreq1_control(struct device *dev,
	int id, int ext_pwr, int slots)
{
	int ret;
	uint8_t sleepseq_val = 0;
	unsigned int en_bit;
	unsigned int slot_bit;

	if (id == RC5T583_DS_DC0) {
		dev_err(dev, "PWRREQ1 is invalid control for rail %d\n", id);
		return -EINVAL;
	}

	en_bit = deepsleep_data[id].ds_pos_bit;
	slot_bit = en_bit + 1;
	ret = rc5t583_read(dev, deepsleep_data[id].reg_add, &sleepseq_val);
	if (ret < 0) {
		dev_err(dev, "Error in reading reg 0x%x\n",
				deepsleep_data[id].reg_add);
		return ret;
	}

	sleepseq_val &= ~(0xF << en_bit);
	sleepseq_val |= BIT(en_bit);
	sleepseq_val |= ((slots & 0x7) << slot_bit);
	ret = rc5t583_set_bits(dev, RICOH_ONOFFSEL_REG, BIT(1));
	if (ret < 0) {
		dev_err(dev, "Error in updating the 0x%02x register\n",
				RICOH_ONOFFSEL_REG);
		return ret;
	}

	ret = rc5t583_write(dev, deepsleep_data[id].reg_add, sleepseq_val);
	if (ret < 0) {
		dev_err(dev, "Error in writing reg 0x%x\n",
				deepsleep_data[id].reg_add);
		return ret;
	}

	if (id == RC5T583_DS_LDO4) {
		ret = rc5t583_write(dev, RICOH_SWCTL_REG, 0x1);
		if (ret < 0)
			dev_err(dev, "Error in writing reg 0x%x\n",
				RICOH_SWCTL_REG);
	}
	return ret;
}

static int __rc5t583_set_ext_pwrreq2_control(struct device *dev,
	int id, int ext_pwr)
{
	int ret;

	if (id != RC5T583_DS_DC0) {
		dev_err(dev, "PWRREQ2 is invalid control for rail %d\n", id);
		return -EINVAL;
	}

	ret = rc5t583_set_bits(dev, RICOH_ONOFFSEL_REG, BIT(2));
	if (ret < 0)
		dev_err(dev, "Error in updating the ONOFFSEL 0x10 register\n");
	return ret;
}

int rc5t583_ext_power_req_config(struct device *dev, int ds_id,
	int ext_pwr_req, int deepsleep_slot_nr)
{
	if ((ext_pwr_req & EXT_PWR_REQ) == EXT_PWR_REQ)
		return -EINVAL;

	if (ext_pwr_req & RC5T583_EXT_PWRREQ1_CONTROL)
		return __rc5t583_set_ext_pwrreq1_control(dev, ds_id,
				ext_pwr_req, deepsleep_slot_nr);

	if (ext_pwr_req & RC5T583_EXT_PWRREQ2_CONTROL)
		return __rc5t583_set_ext_pwrreq2_control(dev,
			ds_id, ext_pwr_req);
	return 0;
}
EXPORT_SYMBOL(rc5t583_ext_power_req_config);

static int rc5t583_clear_ext_power_req(struct rc5t583 *rc5t583,
	struct rc5t583_platform_data *pdata)
{
	int ret;
	int i;
	uint8_t on_off_val = 0;

	/*  Clear ONOFFSEL register */
	if (pdata->enable_shutdown)
		on_off_val = 0x1;

	ret = rc5t583_write(rc5t583->dev, RICOH_ONOFFSEL_REG, on_off_val);
	if (ret < 0)
		dev_warn(rc5t583->dev, "Error in writing reg %d error: %d\n",
					RICOH_ONOFFSEL_REG, ret);

	ret = rc5t583_write(rc5t583->dev, RICOH_SWCTL_REG, 0x0);
	if (ret < 0)
		dev_warn(rc5t583->dev, "Error in writing reg %d error: %d\n",
					RICOH_SWCTL_REG, ret);

	/* Clear sleep sequence register */
	for (i = RC5T583_SLPSEQ1; i <= RC5T583_SLPSEQ11; ++i) {
		ret = rc5t583_write(rc5t583->dev, i, 0x0);
		if (ret < 0)
			dev_warn(rc5t583->dev,
				"Error in writing reg 0x%02x error: %d\n",
				i, ret);
	}
	return 0;
}

static bool volatile_reg(struct device *dev, unsigned int reg)
{
	/* Enable caching in interrupt registers */
	switch (reg) {
	case RC5T583_INT_EN_SYS1:
	case RC5T583_INT_EN_SYS2:
	case RC5T583_INT_EN_DCDC:
	case RC5T583_INT_EN_RTC:
	case RC5T583_INT_EN_ADC1:
	case RC5T583_INT_EN_ADC2:
	case RC5T583_INT_EN_ADC3:
	case RC5T583_GPIO_GPEDGE1:
	case RC5T583_GPIO_GPEDGE2:
	case RC5T583_GPIO_EN_INT:
		return false;

	case RC5T583_GPIO_MON_IOIN:
		/* This is gpio input register */
		return true;

	default:
		/* Enable caching in gpio registers */
		if ((reg >= RC5T583_GPIO_IOSEL) &&
				(reg <= RC5T583_GPIO_GPOFUNC))
			return false;

		/* Enable caching in sleep seq registers */
		if ((reg >= RC5T583_SLPSEQ1) && (reg <= RC5T583_SLPSEQ11))
			return false;

		/* Enable caching of regulator registers */
		if ((reg >= RC5T583_REG_DC0CTL) && (reg <= RC5T583_REG_SR3CTL))
			return false;
		if ((reg >= RC5T583_REG_LDOEN1) &&
					(reg <= RC5T583_REG_LDO9DAC_DS))
			return false;

		break;
	}

	return true;
}

static const struct regmap_config rc5t583_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_reg = volatile_reg,
	.max_register = RC5T583_MAX_REG,
	.num_reg_defaults_raw = RC5T583_NUM_REGS,
	.cache_type = REGCACHE_RBTREE,
};

static int rc5t583_i2c_probe(struct i2c_client *i2c,
			      const struct i2c_device_id *id)
{
	struct rc5t583 *rc5t583;
	struct rc5t583_platform_data *pdata = dev_get_platdata(&i2c->dev);
	int ret;

	if (!pdata) {
		dev_err(&i2c->dev, "Err: Platform data not found\n");
		return -EINVAL;
	}

	rc5t583 = devm_kzalloc(&i2c->dev, sizeof(struct rc5t583), GFP_KERNEL);
	if (!rc5t583) {
		dev_err(&i2c->dev, "Memory allocation failed\n");
		return -ENOMEM;
	}

	rc5t583->dev = &i2c->dev;
	i2c_set_clientdata(i2c, rc5t583);

	rc5t583->regmap = devm_regmap_init_i2c(i2c, &rc5t583_regmap_config);
	if (IS_ERR(rc5t583->regmap)) {
		ret = PTR_ERR(rc5t583->regmap);
		dev_err(&i2c->dev, "regmap initialization failed: %d\n", ret);
		return ret;
	}

	ret = rc5t583_clear_ext_power_req(rc5t583, pdata);
	if (ret < 0)
		return ret;

	if (i2c->irq) {
		ret = rc5t583_irq_init(rc5t583, i2c->irq, pdata->irq_base);
		/* Still continue with warning, if irq init fails */
		if (ret)
			dev_warn(&i2c->dev, "IRQ init failed: %d\n", ret);
	}

	ret = devm_mfd_add_devices(rc5t583->dev, -1, rc5t583_subdevs,
				   ARRAY_SIZE(rc5t583_subdevs), NULL, 0, NULL);
	if (ret) {
		dev_err(&i2c->dev, "add mfd devices failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct i2c_device_id rc5t583_i2c_id[] = {
	{.name = "rc5t583", .driver_data = 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, rc5t583_i2c_id);

static struct i2c_driver rc5t583_i2c_driver = {
	.driver = {
		   .name = "rc5t583",
		   },
	.probe = rc5t583_i2c_probe,
	.id_table = rc5t583_i2c_id,
};

static int __init rc5t583_i2c_init(void)
{
	return i2c_add_driver(&rc5t583_i2c_driver);
}
subsys_initcall(rc5t583_i2c_init);

static void __exit rc5t583_i2c_exit(void)
{
	i2c_del_driver(&rc5t583_i2c_driver);
}

module_exit(rc5t583_i2c_exit);

MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_DESCRIPTION("RICOH RC5T583 power management system device driver");
MODULE_LICENSE("GPL v2");
