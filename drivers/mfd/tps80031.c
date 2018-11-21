/*
 * tps80031.c -- TI TPS80031/TPS80032 mfd core driver.
 *
 * MFD core driver for TI TPS80031/TPS80032 Fully Integrated
 * Power Management with Power Path and Battery Charger
 *
 * Copyright (c) 2012, NVIDIA Corporation.
 *
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tps80031.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/slab.h>

static struct resource tps80031_rtc_resources[] = {
	{
		.start = TPS80031_INT_RTC_ALARM,
		.end = TPS80031_INT_RTC_ALARM,
		.flags = IORESOURCE_IRQ,
	},
};

/* TPS80031 sub mfd devices */
static const struct mfd_cell tps80031_cell[] = {
	{
		.name = "tps80031-pmic",
	},
	{
		.name = "tps80031-clock",
	},
	{
		.name = "tps80031-rtc",
		.num_resources = ARRAY_SIZE(tps80031_rtc_resources),
		.resources = tps80031_rtc_resources,
	},
	{
		.name = "tps80031-gpadc",
	},
	{
		.name = "tps80031-fuel-gauge",
	},
	{
		.name = "tps80031-charger",
	},
};

static int tps80031_slave_address[TPS80031_NUM_SLAVES] = {
	TPS80031_I2C_ID0_ADDR,
	TPS80031_I2C_ID1_ADDR,
	TPS80031_I2C_ID2_ADDR,
	TPS80031_I2C_ID3_ADDR,
};

struct tps80031_pupd_data {
	u8	reg;
	u8	pullup_bit;
	u8	pulldown_bit;
};

#define TPS80031_IRQ(_reg, _mask)			\
	{							\
		.reg_offset = (TPS80031_INT_MSK_LINE_##_reg) -	\
				TPS80031_INT_MSK_LINE_A,	\
		.mask = BIT(_mask),				\
	}

static const struct regmap_irq tps80031_main_irqs[] = {
	[TPS80031_INT_PWRON]		= TPS80031_IRQ(A, 0),
	[TPS80031_INT_RPWRON]		= TPS80031_IRQ(A, 1),
	[TPS80031_INT_SYS_VLOW]		= TPS80031_IRQ(A, 2),
	[TPS80031_INT_RTC_ALARM]	= TPS80031_IRQ(A, 3),
	[TPS80031_INT_RTC_PERIOD]	= TPS80031_IRQ(A, 4),
	[TPS80031_INT_HOT_DIE]		= TPS80031_IRQ(A, 5),
	[TPS80031_INT_VXX_SHORT]	= TPS80031_IRQ(A, 6),
	[TPS80031_INT_SPDURATION]	= TPS80031_IRQ(A, 7),
	[TPS80031_INT_WATCHDOG]		= TPS80031_IRQ(B, 0),
	[TPS80031_INT_BAT]		= TPS80031_IRQ(B, 1),
	[TPS80031_INT_SIM]		= TPS80031_IRQ(B, 2),
	[TPS80031_INT_MMC]		= TPS80031_IRQ(B, 3),
	[TPS80031_INT_RES]		= TPS80031_IRQ(B, 4),
	[TPS80031_INT_GPADC_RT]		= TPS80031_IRQ(B, 5),
	[TPS80031_INT_GPADC_SW2_EOC]	= TPS80031_IRQ(B, 6),
	[TPS80031_INT_CC_AUTOCAL]	= TPS80031_IRQ(B, 7),
	[TPS80031_INT_ID_WKUP]		= TPS80031_IRQ(C, 0),
	[TPS80031_INT_VBUSS_WKUP]	= TPS80031_IRQ(C, 1),
	[TPS80031_INT_ID]		= TPS80031_IRQ(C, 2),
	[TPS80031_INT_VBUS]		= TPS80031_IRQ(C, 3),
	[TPS80031_INT_CHRG_CTRL]	= TPS80031_IRQ(C, 4),
	[TPS80031_INT_EXT_CHRG]		= TPS80031_IRQ(C, 5),
	[TPS80031_INT_INT_CHRG]		= TPS80031_IRQ(C, 6),
	[TPS80031_INT_RES2]		= TPS80031_IRQ(C, 7),
};

static struct regmap_irq_chip tps80031_irq_chip = {
	.name = "tps80031",
	.irqs = tps80031_main_irqs,
	.num_irqs = ARRAY_SIZE(tps80031_main_irqs),
	.num_regs = 3,
	.status_base = TPS80031_INT_STS_A,
	.mask_base = TPS80031_INT_MSK_LINE_A,
};

#define PUPD_DATA(_reg, _pulldown_bit, _pullup_bit)	\
	{						\
		.reg = TPS80031_CFG_INPUT_PUPD##_reg,	\
		.pulldown_bit = _pulldown_bit,		\
		.pullup_bit = _pullup_bit,		\
	}

static const struct tps80031_pupd_data tps80031_pupds[] = {
	[TPS80031_PREQ1]		= PUPD_DATA(1, BIT(0),	BIT(1)),
	[TPS80031_PREQ2A]		= PUPD_DATA(1, BIT(2),	BIT(3)),
	[TPS80031_PREQ2B]		= PUPD_DATA(1, BIT(4),	BIT(5)),
	[TPS80031_PREQ2C]		= PUPD_DATA(1, BIT(6),	BIT(7)),
	[TPS80031_PREQ3]		= PUPD_DATA(2, BIT(0),	BIT(1)),
	[TPS80031_NRES_WARM]		= PUPD_DATA(2, 0,	BIT(2)),
	[TPS80031_PWM_FORCE]		= PUPD_DATA(2, BIT(5),	0),
	[TPS80031_CHRG_EXT_CHRG_STATZ]	= PUPD_DATA(2, 0,	BIT(6)),
	[TPS80031_SIM]			= PUPD_DATA(3, BIT(0),	BIT(1)),
	[TPS80031_MMC]			= PUPD_DATA(3, BIT(2),	BIT(3)),
	[TPS80031_GPADC_START]		= PUPD_DATA(3, BIT(4),	0),
	[TPS80031_DVSI2C_SCL]		= PUPD_DATA(4, 0,	BIT(0)),
	[TPS80031_DVSI2C_SDA]		= PUPD_DATA(4, 0,	BIT(1)),
	[TPS80031_CTLI2C_SCL]		= PUPD_DATA(4, 0,	BIT(2)),
	[TPS80031_CTLI2C_SDA]		= PUPD_DATA(4, 0,	BIT(3)),
};
static struct tps80031 *tps80031_power_off_dev;

int tps80031_ext_power_req_config(struct device *dev,
		unsigned long ext_ctrl_flag, int preq_bit,
		int state_reg_add, int trans_reg_add)
{
	u8 res_ass_reg = 0;
	int preq_mask_bit = 0;
	int ret;

	if (!(ext_ctrl_flag & TPS80031_EXT_PWR_REQ))
		return 0;

	if (ext_ctrl_flag & TPS80031_PWR_REQ_INPUT_PREQ1) {
		res_ass_reg = TPS80031_PREQ1_RES_ASS_A + (preq_bit >> 3);
		preq_mask_bit = 5;
	} else if (ext_ctrl_flag & TPS80031_PWR_REQ_INPUT_PREQ2) {
		res_ass_reg = TPS80031_PREQ2_RES_ASS_A + (preq_bit >> 3);
		preq_mask_bit = 6;
	} else if (ext_ctrl_flag & TPS80031_PWR_REQ_INPUT_PREQ3) {
		res_ass_reg = TPS80031_PREQ3_RES_ASS_A + (preq_bit >> 3);
		preq_mask_bit = 7;
	}

	/* Configure REQ_ASS registers */
	ret = tps80031_set_bits(dev, TPS80031_SLAVE_ID1, res_ass_reg,
					BIT(preq_bit & 0x7));
	if (ret < 0) {
		dev_err(dev, "reg 0x%02x setbit failed, err = %d\n",
				res_ass_reg, ret);
		return ret;
	}

	/* Unmask the PREQ */
	ret = tps80031_clr_bits(dev, TPS80031_SLAVE_ID1,
			TPS80031_PHOENIX_MSK_TRANSITION, BIT(preq_mask_bit));
	if (ret < 0) {
		dev_err(dev, "reg 0x%02x clrbit failed, err = %d\n",
			TPS80031_PHOENIX_MSK_TRANSITION, ret);
		return ret;
	}

	/* Switch regulator control to resource now */
	if (ext_ctrl_flag & (TPS80031_PWR_REQ_INPUT_PREQ2 |
					TPS80031_PWR_REQ_INPUT_PREQ3)) {
		ret = tps80031_update(dev, TPS80031_SLAVE_ID1, state_reg_add,
						0x0, TPS80031_STATE_MASK);
		if (ret < 0)
			dev_err(dev, "reg 0x%02x update failed, err = %d\n",
				state_reg_add, ret);
	} else {
		ret = tps80031_update(dev, TPS80031_SLAVE_ID1, trans_reg_add,
				TPS80031_TRANS_SLEEP_OFF,
				TPS80031_TRANS_SLEEP_MASK);
		if (ret < 0)
			dev_err(dev, "reg 0x%02x update failed, err = %d\n",
				trans_reg_add, ret);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(tps80031_ext_power_req_config);

static void tps80031_power_off(void)
{
	dev_info(tps80031_power_off_dev->dev, "switching off PMU\n");
	tps80031_write(tps80031_power_off_dev->dev, TPS80031_SLAVE_ID1,
				TPS80031_PHOENIX_DEV_ON, TPS80031_DEVOFF);
}

static void tps80031_pupd_init(struct tps80031 *tps80031,
			       struct tps80031_platform_data *pdata)
{
	struct tps80031_pupd_init_data *pupd_init_data = pdata->pupd_init_data;
	int data_size = pdata->pupd_init_data_size;
	int i;

	for (i = 0; i < data_size; ++i) {
		struct tps80031_pupd_init_data *pupd_init = &pupd_init_data[i];
		const struct tps80031_pupd_data *pupd =
			&tps80031_pupds[pupd_init->input_pin];
		u8 update_value = 0;
		u8 update_mask = pupd->pulldown_bit | pupd->pullup_bit;

		if (pupd_init->setting == TPS80031_PUPD_PULLDOWN)
			update_value = pupd->pulldown_bit;
		else if (pupd_init->setting == TPS80031_PUPD_PULLUP)
			update_value = pupd->pullup_bit;

		tps80031_update(tps80031->dev, TPS80031_SLAVE_ID1, pupd->reg,
				update_value, update_mask);
	}
}

static int tps80031_init_ext_control(struct tps80031 *tps80031,
			struct tps80031_platform_data *pdata)
{
	struct device *dev = tps80031->dev;
	int ret;
	int i;

	/* Clear all external control for this rail */
	for (i = 0; i < 9; ++i) {
		ret = tps80031_write(dev, TPS80031_SLAVE_ID1,
				TPS80031_PREQ1_RES_ASS_A + i, 0);
		if (ret < 0) {
			dev_err(dev, "reg 0x%02x write failed, err = %d\n",
				TPS80031_PREQ1_RES_ASS_A + i, ret);
			return ret;
		}
	}

	/* Mask the PREQ */
	ret = tps80031_set_bits(dev, TPS80031_SLAVE_ID1,
			TPS80031_PHOENIX_MSK_TRANSITION, 0x7 << 5);
	if (ret < 0) {
		dev_err(dev, "reg 0x%02x set_bits failed, err = %d\n",
			TPS80031_PHOENIX_MSK_TRANSITION, ret);
		return ret;
	}
	return ret;
}

static int tps80031_irq_init(struct tps80031 *tps80031, int irq, int irq_base)
{
	struct device *dev = tps80031->dev;
	int i, ret;

	/*
	 * The MASK register used for updating status register when
	 * interrupt occurs and LINE register used to pass the status
	 * to actual interrupt line.  As per datasheet:
	 * When INT_MSK_LINE [i] is set to 1, the associated interrupt
	 * number i is INT line masked, which means that no interrupt is
	 * generated on the INT line.
	 * When INT_MSK_LINE [i] is set to 0, the associated interrupt
	 * number i is  line enabled: An interrupt is generated on the
	 * INT line.
	 * In any case, the INT_STS [i] status bit may or may not be updated,
	 * only linked to the INT_MSK_STS [i] configuration register bit.
	 *
	 * When INT_MSK_STS [i] is set to 1, the associated interrupt number
	 * i is status masked, which means that no interrupt is stored in
	 * the INT_STS[i] status bit. Note that no interrupt number i is
	 * generated on the INT line, even if the INT_MSK_LINE [i] register
	 * bit is set to 0.
	 * When INT_MSK_STS [i] is set to 0, the associated interrupt number i
	 * is status enabled: An interrupt status is updated in the INT_STS [i]
	 * register. The interrupt may or may not be generated on the INT line,
	 * depending on the INT_MSK_LINE [i] configuration register bit.
	 */
	for (i = 0; i < 3; i++)
		tps80031_write(dev, TPS80031_SLAVE_ID2,
				TPS80031_INT_MSK_STS_A + i, 0x00);

	ret = regmap_add_irq_chip(tps80031->regmap[TPS80031_SLAVE_ID2], irq,
			IRQF_ONESHOT, irq_base,
			&tps80031_irq_chip, &tps80031->irq_data);
	if (ret < 0) {
		dev_err(dev, "add irq failed, err = %d\n", ret);
		return ret;
	}
	return ret;
}

static bool rd_wr_reg_id0(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TPS80031_SMPS1_CFG_FORCE ... TPS80031_SMPS2_CFG_VOLTAGE:
		return true;
	default:
		return false;
	}
}

static bool rd_wr_reg_id1(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TPS80031_SECONDS_REG ... TPS80031_RTC_RESET_STATUS_REG:
	case TPS80031_VALIDITY0 ... TPS80031_VALIDITY7:
	case TPS80031_PHOENIX_START_CONDITION ... TPS80031_KEY_PRESS_DUR_CFG:
	case TPS80031_SMPS4_CFG_TRANS ... TPS80031_SMPS3_CFG_VOLTAGE:
	case TPS80031_BROADCAST_ADDR_ALL ... TPS80031_BROADCAST_ADDR_CLK_RST:
	case TPS80031_VANA_CFG_TRANS ... TPS80031_LDO7_CFG_VOLTAGE:
	case TPS80031_REGEN1_CFG_TRANS ... TPS80031_TMP_CFG_STATE:
	case TPS80031_PREQ1_RES_ASS_A ... TPS80031_PREQ3_RES_ASS_C:
	case TPS80031_SMPS_OFFSET ... TPS80031_BATDEBOUNCING:
	case TPS80031_CFG_INPUT_PUPD1 ... TPS80031_CFG_SMPS_PD:
	case TPS80031_BACKUP_REG:
		return true;
	default:
		return false;
	}
}

static bool is_volatile_reg_id1(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TPS80031_SMPS4_CFG_TRANS ... TPS80031_SMPS3_CFG_VOLTAGE:
	case TPS80031_VANA_CFG_TRANS ... TPS80031_LDO7_CFG_VOLTAGE:
	case TPS80031_REGEN1_CFG_TRANS ... TPS80031_TMP_CFG_STATE:
	case TPS80031_PREQ1_RES_ASS_A ... TPS80031_PREQ3_RES_ASS_C:
	case TPS80031_SMPS_OFFSET ... TPS80031_BATDEBOUNCING:
	case TPS80031_CFG_INPUT_PUPD1 ... TPS80031_CFG_SMPS_PD:
		return true;
	default:
		return false;
	}
}

static bool rd_wr_reg_id2(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TPS80031_USB_VENDOR_ID_LSB ... TPS80031_USB_OTG_REVISION:
	case TPS80031_GPADC_CTRL ... TPS80031_CTRL_P1:
	case TPS80031_RTCH0_LSB ... TPS80031_GPCH0_MSB:
	case TPS80031_TOGGLE1 ... TPS80031_VIBMODE:
	case TPS80031_PWM1ON ... TPS80031_PWM2OFF:
	case TPS80031_FG_REG_00 ... TPS80031_FG_REG_11:
	case TPS80031_INT_STS_A ... TPS80031_INT_MSK_STS_C:
	case TPS80031_CONTROLLER_CTRL2 ... TPS80031_LED_PWM_CTRL2:
		return true;
	default:
		return false;
	}
}

static bool rd_wr_reg_id3(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TPS80031_GPADC_TRIM0 ... TPS80031_GPADC_TRIM18:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config tps80031_regmap_configs[] = {
	{
		.reg_bits = 8,
		.val_bits = 8,
		.writeable_reg = rd_wr_reg_id0,
		.readable_reg = rd_wr_reg_id0,
		.max_register = TPS80031_MAX_REGISTER,
	},
	{
		.reg_bits = 8,
		.val_bits = 8,
		.writeable_reg = rd_wr_reg_id1,
		.readable_reg = rd_wr_reg_id1,
		.volatile_reg = is_volatile_reg_id1,
		.max_register = TPS80031_MAX_REGISTER,
	},
	{
		.reg_bits = 8,
		.val_bits = 8,
		.writeable_reg = rd_wr_reg_id2,
		.readable_reg = rd_wr_reg_id2,
		.max_register = TPS80031_MAX_REGISTER,
	},
	{
		.reg_bits = 8,
		.val_bits = 8,
		.writeable_reg = rd_wr_reg_id3,
		.readable_reg = rd_wr_reg_id3,
		.max_register = TPS80031_MAX_REGISTER,
	},
};

static int tps80031_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct tps80031_platform_data *pdata = dev_get_platdata(&client->dev);
	struct tps80031 *tps80031;
	int ret;
	uint8_t es_version;
	uint8_t ep_ver;
	int i;

	if (!pdata) {
		dev_err(&client->dev, "tps80031 requires platform data\n");
		return -EINVAL;
	}

	tps80031 = devm_kzalloc(&client->dev, sizeof(*tps80031), GFP_KERNEL);
	if (!tps80031)
		return -ENOMEM;

	for (i = 0; i < TPS80031_NUM_SLAVES; i++) {
		if (tps80031_slave_address[i] == client->addr)
			tps80031->clients[i] = client;
		else
			tps80031->clients[i] = i2c_new_dummy(client->adapter,
						tps80031_slave_address[i]);
		if (!tps80031->clients[i]) {
			dev_err(&client->dev, "can't attach client %d\n", i);
			ret = -ENOMEM;
			goto fail_client_reg;
		}

		i2c_set_clientdata(tps80031->clients[i], tps80031);
		tps80031->regmap[i] = devm_regmap_init_i2c(tps80031->clients[i],
					&tps80031_regmap_configs[i]);
		if (IS_ERR(tps80031->regmap[i])) {
			ret = PTR_ERR(tps80031->regmap[i]);
			dev_err(&client->dev,
				"regmap %d init failed, err %d\n", i, ret);
			goto fail_client_reg;
		}
	}

	ret = tps80031_read(&client->dev, TPS80031_SLAVE_ID3,
			TPS80031_JTAGVERNUM, &es_version);
	if (ret < 0) {
		dev_err(&client->dev,
			"Silicon version number read failed: %d\n", ret);
		goto fail_client_reg;
	}

	ret = tps80031_read(&client->dev, TPS80031_SLAVE_ID3,
			TPS80031_EPROM_REV, &ep_ver);
	if (ret < 0) {
		dev_err(&client->dev,
			"Silicon eeprom version read failed: %d\n", ret);
		goto fail_client_reg;
	}

	dev_info(&client->dev, "ES version 0x%02x and EPROM version 0x%02x\n",
					es_version, ep_ver);
	tps80031->es_version = es_version;
	tps80031->dev = &client->dev;
	i2c_set_clientdata(client, tps80031);
	tps80031->chip_info = id->driver_data;

	ret = tps80031_irq_init(tps80031, client->irq, pdata->irq_base);
	if (ret) {
		dev_err(&client->dev, "IRQ init failed: %d\n", ret);
		goto fail_client_reg;
	}

	tps80031_pupd_init(tps80031, pdata);

	tps80031_init_ext_control(tps80031, pdata);

	ret = mfd_add_devices(tps80031->dev, -1,
			tps80031_cell, ARRAY_SIZE(tps80031_cell),
			NULL, 0,
			regmap_irq_get_domain(tps80031->irq_data));
	if (ret < 0) {
		dev_err(&client->dev, "mfd_add_devices failed: %d\n", ret);
		goto fail_mfd_add;
	}

	if (pdata->use_power_off && !pm_power_off) {
		tps80031_power_off_dev = tps80031;
		pm_power_off = tps80031_power_off;
	}
	return 0;

fail_mfd_add:
	regmap_del_irq_chip(client->irq, tps80031->irq_data);

fail_client_reg:
	for (i = 0; i < TPS80031_NUM_SLAVES; i++) {
		if (tps80031->clients[i]  && (tps80031->clients[i] != client))
			i2c_unregister_device(tps80031->clients[i]);
	}
	return ret;
}

static int tps80031_remove(struct i2c_client *client)
{
	struct tps80031 *tps80031 = i2c_get_clientdata(client);
	int i;

	if (tps80031_power_off_dev == tps80031) {
		tps80031_power_off_dev = NULL;
		pm_power_off = NULL;
	}

	mfd_remove_devices(tps80031->dev);

	regmap_del_irq_chip(client->irq, tps80031->irq_data);

	for (i = 0; i < TPS80031_NUM_SLAVES; i++) {
		if (tps80031->clients[i] != client)
			i2c_unregister_device(tps80031->clients[i]);
	}
	return 0;
}

static const struct i2c_device_id tps80031_id_table[] = {
	{ "tps80031", TPS80031 },
	{ "tps80032", TPS80032 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tps80031_id_table);

static struct i2c_driver tps80031_driver = {
	.driver	= {
		.name	= "tps80031",
	},
	.probe		= tps80031_probe,
	.remove		= tps80031_remove,
	.id_table	= tps80031_id_table,
};

static int __init tps80031_init(void)
{
	return i2c_add_driver(&tps80031_driver);
}
subsys_initcall(tps80031_init);

static void __exit tps80031_exit(void)
{
	i2c_del_driver(&tps80031_driver);
}
module_exit(tps80031_exit);

MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_DESCRIPTION("TPS80031 core driver");
MODULE_LICENSE("GPL v2");
