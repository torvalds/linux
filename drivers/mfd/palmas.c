// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * TI Palmas MFD Driver
 *
 * Copyright 2011-2012 Texas Instruments Inc.
 *
 * Author: Graeme Gregory <gg@slimlogic.co.uk>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/regmap.h>
#include <linux/err.h>
#include <linux/mfd/core.h>
#include <linux/mfd/palmas.h>
#include <linux/of_device.h>

static const struct regmap_config palmas_regmap_config[PALMAS_NUM_CLIENTS] = {
	{
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = PALMAS_BASE_TO_REG(PALMAS_PU_PD_OD_BASE,
					PALMAS_PRIMARY_SECONDARY_PAD3),
	},
	{
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = PALMAS_BASE_TO_REG(PALMAS_GPADC_BASE,
					PALMAS_GPADC_SMPS_VSEL_MONITORING),
	},
	{
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = PALMAS_BASE_TO_REG(PALMAS_TRIM_GPADC_BASE,
					PALMAS_GPADC_TRIM16),
	},
};

static const struct regmap_irq tps65917_irqs[] = {
	/* INT1 IRQs */
	[TPS65917_RESERVED1] = {
		.mask = TPS65917_RESERVED,
	},
	[TPS65917_PWRON_IRQ] = {
		.mask = TPS65917_INT1_STATUS_PWRON,
	},
	[TPS65917_LONG_PRESS_KEY_IRQ] = {
		.mask = TPS65917_INT1_STATUS_LONG_PRESS_KEY,
	},
	[TPS65917_RESERVED2] = {
		.mask = TPS65917_RESERVED,
	},
	[TPS65917_PWRDOWN_IRQ] = {
		.mask = TPS65917_INT1_STATUS_PWRDOWN,
	},
	[TPS65917_HOTDIE_IRQ] = {
		.mask = TPS65917_INT1_STATUS_HOTDIE,
	},
	[TPS65917_VSYS_MON_IRQ] = {
		.mask = TPS65917_INT1_STATUS_VSYS_MON,
	},
	[TPS65917_RESERVED3] = {
		.mask = TPS65917_RESERVED,
	},
	/* INT2 IRQs*/
	[TPS65917_RESERVED4] = {
		.mask = TPS65917_RESERVED,
		.reg_offset = 1,
	},
	[TPS65917_OTP_ERROR_IRQ] = {
		.mask = TPS65917_INT2_STATUS_OTP_ERROR,
		.reg_offset = 1,
	},
	[TPS65917_WDT_IRQ] = {
		.mask = TPS65917_INT2_STATUS_WDT,
		.reg_offset = 1,
	},
	[TPS65917_RESERVED5] = {
		.mask = TPS65917_RESERVED,
		.reg_offset = 1,
	},
	[TPS65917_RESET_IN_IRQ] = {
		.mask = TPS65917_INT2_STATUS_RESET_IN,
		.reg_offset = 1,
	},
	[TPS65917_FSD_IRQ] = {
		.mask = TPS65917_INT2_STATUS_FSD,
		.reg_offset = 1,
	},
	[TPS65917_SHORT_IRQ] = {
		.mask = TPS65917_INT2_STATUS_SHORT,
		.reg_offset = 1,
	},
	[TPS65917_RESERVED6] = {
		.mask = TPS65917_RESERVED,
		.reg_offset = 1,
	},
	/* INT3 IRQs */
	[TPS65917_GPADC_AUTO_0_IRQ] = {
		.mask = TPS65917_INT3_STATUS_GPADC_AUTO_0,
		.reg_offset = 2,
	},
	[TPS65917_GPADC_AUTO_1_IRQ] = {
		.mask = TPS65917_INT3_STATUS_GPADC_AUTO_1,
		.reg_offset = 2,
	},
	[TPS65917_GPADC_EOC_SW_IRQ] = {
		.mask = TPS65917_INT3_STATUS_GPADC_EOC_SW,
		.reg_offset = 2,
	},
	[TPS65917_RESREVED6] = {
		.mask = TPS65917_RESERVED6,
		.reg_offset = 2,
	},
	[TPS65917_RESERVED7] = {
		.mask = TPS65917_RESERVED,
		.reg_offset = 2,
	},
	[TPS65917_RESERVED8] = {
		.mask = TPS65917_RESERVED,
		.reg_offset = 2,
	},
	[TPS65917_RESERVED9] = {
		.mask = TPS65917_RESERVED,
		.reg_offset = 2,
	},
	[TPS65917_VBUS_IRQ] = {
		.mask = TPS65917_INT3_STATUS_VBUS,
		.reg_offset = 2,
	},
	/* INT4 IRQs */
	[TPS65917_GPIO_0_IRQ] = {
		.mask = TPS65917_INT4_STATUS_GPIO_0,
		.reg_offset = 3,
	},
	[TPS65917_GPIO_1_IRQ] = {
		.mask = TPS65917_INT4_STATUS_GPIO_1,
		.reg_offset = 3,
	},
	[TPS65917_GPIO_2_IRQ] = {
		.mask = TPS65917_INT4_STATUS_GPIO_2,
		.reg_offset = 3,
	},
	[TPS65917_GPIO_3_IRQ] = {
		.mask = TPS65917_INT4_STATUS_GPIO_3,
		.reg_offset = 3,
	},
	[TPS65917_GPIO_4_IRQ] = {
		.mask = TPS65917_INT4_STATUS_GPIO_4,
		.reg_offset = 3,
	},
	[TPS65917_GPIO_5_IRQ] = {
		.mask = TPS65917_INT4_STATUS_GPIO_5,
		.reg_offset = 3,
	},
	[TPS65917_GPIO_6_IRQ] = {
		.mask = TPS65917_INT4_STATUS_GPIO_6,
		.reg_offset = 3,
	},
	[TPS65917_RESERVED10] = {
		.mask = TPS65917_RESERVED10,
		.reg_offset = 3,
	},
};

static const struct regmap_irq palmas_irqs[] = {
	/* INT1 IRQs */
	[PALMAS_CHARG_DET_N_VBUS_OVV_IRQ] = {
		.mask = PALMAS_INT1_STATUS_CHARG_DET_N_VBUS_OVV,
	},
	[PALMAS_PWRON_IRQ] = {
		.mask = PALMAS_INT1_STATUS_PWRON,
	},
	[PALMAS_LONG_PRESS_KEY_IRQ] = {
		.mask = PALMAS_INT1_STATUS_LONG_PRESS_KEY,
	},
	[PALMAS_RPWRON_IRQ] = {
		.mask = PALMAS_INT1_STATUS_RPWRON,
	},
	[PALMAS_PWRDOWN_IRQ] = {
		.mask = PALMAS_INT1_STATUS_PWRDOWN,
	},
	[PALMAS_HOTDIE_IRQ] = {
		.mask = PALMAS_INT1_STATUS_HOTDIE,
	},
	[PALMAS_VSYS_MON_IRQ] = {
		.mask = PALMAS_INT1_STATUS_VSYS_MON,
	},
	[PALMAS_VBAT_MON_IRQ] = {
		.mask = PALMAS_INT1_STATUS_VBAT_MON,
	},
	/* INT2 IRQs*/
	[PALMAS_RTC_ALARM_IRQ] = {
		.mask = PALMAS_INT2_STATUS_RTC_ALARM,
		.reg_offset = 1,
	},
	[PALMAS_RTC_TIMER_IRQ] = {
		.mask = PALMAS_INT2_STATUS_RTC_TIMER,
		.reg_offset = 1,
	},
	[PALMAS_WDT_IRQ] = {
		.mask = PALMAS_INT2_STATUS_WDT,
		.reg_offset = 1,
	},
	[PALMAS_BATREMOVAL_IRQ] = {
		.mask = PALMAS_INT2_STATUS_BATREMOVAL,
		.reg_offset = 1,
	},
	[PALMAS_RESET_IN_IRQ] = {
		.mask = PALMAS_INT2_STATUS_RESET_IN,
		.reg_offset = 1,
	},
	[PALMAS_FBI_BB_IRQ] = {
		.mask = PALMAS_INT2_STATUS_FBI_BB,
		.reg_offset = 1,
	},
	[PALMAS_SHORT_IRQ] = {
		.mask = PALMAS_INT2_STATUS_SHORT,
		.reg_offset = 1,
	},
	[PALMAS_VAC_ACOK_IRQ] = {
		.mask = PALMAS_INT2_STATUS_VAC_ACOK,
		.reg_offset = 1,
	},
	/* INT3 IRQs */
	[PALMAS_GPADC_AUTO_0_IRQ] = {
		.mask = PALMAS_INT3_STATUS_GPADC_AUTO_0,
		.reg_offset = 2,
	},
	[PALMAS_GPADC_AUTO_1_IRQ] = {
		.mask = PALMAS_INT3_STATUS_GPADC_AUTO_1,
		.reg_offset = 2,
	},
	[PALMAS_GPADC_EOC_SW_IRQ] = {
		.mask = PALMAS_INT3_STATUS_GPADC_EOC_SW,
		.reg_offset = 2,
	},
	[PALMAS_GPADC_EOC_RT_IRQ] = {
		.mask = PALMAS_INT3_STATUS_GPADC_EOC_RT,
		.reg_offset = 2,
	},
	[PALMAS_ID_OTG_IRQ] = {
		.mask = PALMAS_INT3_STATUS_ID_OTG,
		.reg_offset = 2,
	},
	[PALMAS_ID_IRQ] = {
		.mask = PALMAS_INT3_STATUS_ID,
		.reg_offset = 2,
	},
	[PALMAS_VBUS_OTG_IRQ] = {
		.mask = PALMAS_INT3_STATUS_VBUS_OTG,
		.reg_offset = 2,
	},
	[PALMAS_VBUS_IRQ] = {
		.mask = PALMAS_INT3_STATUS_VBUS,
		.reg_offset = 2,
	},
	/* INT4 IRQs */
	[PALMAS_GPIO_0_IRQ] = {
		.mask = PALMAS_INT4_STATUS_GPIO_0,
		.reg_offset = 3,
	},
	[PALMAS_GPIO_1_IRQ] = {
		.mask = PALMAS_INT4_STATUS_GPIO_1,
		.reg_offset = 3,
	},
	[PALMAS_GPIO_2_IRQ] = {
		.mask = PALMAS_INT4_STATUS_GPIO_2,
		.reg_offset = 3,
	},
	[PALMAS_GPIO_3_IRQ] = {
		.mask = PALMAS_INT4_STATUS_GPIO_3,
		.reg_offset = 3,
	},
	[PALMAS_GPIO_4_IRQ] = {
		.mask = PALMAS_INT4_STATUS_GPIO_4,
		.reg_offset = 3,
	},
	[PALMAS_GPIO_5_IRQ] = {
		.mask = PALMAS_INT4_STATUS_GPIO_5,
		.reg_offset = 3,
	},
	[PALMAS_GPIO_6_IRQ] = {
		.mask = PALMAS_INT4_STATUS_GPIO_6,
		.reg_offset = 3,
	},
	[PALMAS_GPIO_7_IRQ] = {
		.mask = PALMAS_INT4_STATUS_GPIO_7,
		.reg_offset = 3,
	},
};

static struct regmap_irq_chip palmas_irq_chip = {
	.name = "palmas",
	.irqs = palmas_irqs,
	.num_irqs = ARRAY_SIZE(palmas_irqs),

	.num_regs = 4,
	.irq_reg_stride = 5,
	.status_base = PALMAS_BASE_TO_REG(PALMAS_INTERRUPT_BASE,
			PALMAS_INT1_STATUS),
	.mask_base = PALMAS_BASE_TO_REG(PALMAS_INTERRUPT_BASE,
			PALMAS_INT1_MASK),
};

static struct regmap_irq_chip tps65917_irq_chip = {
	.name = "tps65917",
	.irqs = tps65917_irqs,
	.num_irqs = ARRAY_SIZE(tps65917_irqs),

	.num_regs = 4,
	.irq_reg_stride = 5,
	.status_base = PALMAS_BASE_TO_REG(PALMAS_INTERRUPT_BASE,
			PALMAS_INT1_STATUS),
	.mask_base = PALMAS_BASE_TO_REG(PALMAS_INTERRUPT_BASE,
			PALMAS_INT1_MASK),
};

int palmas_ext_control_req_config(struct palmas *palmas,
	enum palmas_external_requestor_id id,  int ext_ctrl, bool enable)
{
	struct palmas_pmic_driver_data *pmic_ddata = palmas->pmic_ddata;
	int preq_mask_bit = 0;
	int reg_add = 0;
	int bit_pos, ret;

	if (!(ext_ctrl & PALMAS_EXT_REQ))
		return 0;

	if (id >= PALMAS_EXTERNAL_REQSTR_ID_MAX)
		return 0;

	if (ext_ctrl & PALMAS_EXT_CONTROL_NSLEEP) {
		reg_add = PALMAS_NSLEEP_RES_ASSIGN;
		preq_mask_bit = 0;
	} else if (ext_ctrl & PALMAS_EXT_CONTROL_ENABLE1) {
		reg_add = PALMAS_ENABLE1_RES_ASSIGN;
		preq_mask_bit = 1;
	} else if (ext_ctrl & PALMAS_EXT_CONTROL_ENABLE2) {
		reg_add = PALMAS_ENABLE2_RES_ASSIGN;
		preq_mask_bit = 2;
	}

	bit_pos = pmic_ddata->sleep_req_info[id].bit_pos;
	reg_add += pmic_ddata->sleep_req_info[id].reg_offset;
	if (enable)
		ret = palmas_update_bits(palmas, PALMAS_RESOURCE_BASE,
				reg_add, BIT(bit_pos), BIT(bit_pos));
	else
		ret = palmas_update_bits(palmas, PALMAS_RESOURCE_BASE,
				reg_add, BIT(bit_pos), 0);
	if (ret < 0) {
		dev_err(palmas->dev, "Resource reg 0x%02x update failed %d\n",
			reg_add, ret);
		return ret;
	}

	/* Unmask the PREQ */
	ret = palmas_update_bits(palmas, PALMAS_PMU_CONTROL_BASE,
			PALMAS_POWER_CTRL, BIT(preq_mask_bit), 0);
	if (ret < 0) {
		dev_err(palmas->dev, "POWER_CTRL register update failed %d\n",
			ret);
		return ret;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(palmas_ext_control_req_config);

static int palmas_set_pdata_irq_flag(struct i2c_client *i2c,
		struct palmas_platform_data *pdata)
{
	struct irq_data *irq_data = irq_get_irq_data(i2c->irq);
	if (!irq_data) {
		dev_err(&i2c->dev, "Invalid IRQ: %d\n", i2c->irq);
		return -EINVAL;
	}

	pdata->irq_flags = irqd_get_trigger_type(irq_data);
	dev_info(&i2c->dev, "Irq flag is 0x%08x\n", pdata->irq_flags);
	return 0;
}

static void palmas_dt_to_pdata(struct i2c_client *i2c,
		struct palmas_platform_data *pdata)
{
	struct device_node *node = i2c->dev.of_node;
	int ret;
	u32 prop;

	ret = of_property_read_u32(node, "ti,mux-pad1", &prop);
	if (!ret) {
		pdata->mux_from_pdata = 1;
		pdata->pad1 = prop;
	}

	ret = of_property_read_u32(node, "ti,mux-pad2", &prop);
	if (!ret) {
		pdata->mux_from_pdata = 1;
		pdata->pad2 = prop;
	}

	/* The default for this register is all masked */
	ret = of_property_read_u32(node, "ti,power-ctrl", &prop);
	if (!ret)
		pdata->power_ctrl = prop;
	else
		pdata->power_ctrl = PALMAS_POWER_CTRL_NSLEEP_MASK |
					PALMAS_POWER_CTRL_ENABLE1_MASK |
					PALMAS_POWER_CTRL_ENABLE2_MASK;
	if (i2c->irq)
		palmas_set_pdata_irq_flag(i2c, pdata);

	pdata->pm_off = of_property_read_bool(node,
			"ti,system-power-controller");
}

static struct palmas *palmas_dev;
static void palmas_power_off(void)
{
	unsigned int addr;
	int ret, slave;
	u8 powerhold_mask;
	struct device_node *np = palmas_dev->dev->of_node;

	if (of_property_read_bool(np, "ti,palmas-override-powerhold")) {
		addr = PALMAS_BASE_TO_REG(PALMAS_PU_PD_OD_BASE,
					  PALMAS_PRIMARY_SECONDARY_PAD2);
		slave = PALMAS_BASE_TO_SLAVE(PALMAS_PU_PD_OD_BASE);

		if (of_device_is_compatible(np, "ti,tps65917"))
			powerhold_mask =
				TPS65917_PRIMARY_SECONDARY_PAD2_GPIO_5_MASK;
		else
			powerhold_mask =
				PALMAS_PRIMARY_SECONDARY_PAD2_GPIO_7_MASK;

		ret = regmap_update_bits(palmas_dev->regmap[slave], addr,
					 powerhold_mask, 0);
		if (ret)
			dev_err(palmas_dev->dev,
				"Unable to write PRIMARY_SECONDARY_PAD2 %d\n",
				ret);
	}

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_PMU_CONTROL_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_PMU_CONTROL_BASE, PALMAS_DEV_CTRL);

	ret = regmap_update_bits(
			palmas_dev->regmap[slave],
			addr,
			PALMAS_DEV_CTRL_DEV_ON,
			0);

	if (ret)
		pr_err("%s: Unable to write to DEV_CTRL_DEV_ON: %d\n",
				__func__, ret);
}

static unsigned int palmas_features = PALMAS_PMIC_FEATURE_SMPS10_BOOST;
static unsigned int tps659038_features;

struct palmas_driver_data {
	unsigned int *features;
	struct regmap_irq_chip *irq_chip;
};

static struct palmas_driver_data palmas_data = {
	.features = &palmas_features,
	.irq_chip = &palmas_irq_chip,
};

static struct palmas_driver_data tps659038_data = {
	.features = &tps659038_features,
	.irq_chip = &palmas_irq_chip,
};

static struct palmas_driver_data tps65917_data = {
	.features = &tps659038_features,
	.irq_chip = &tps65917_irq_chip,
};

static const struct of_device_id of_palmas_match_tbl[] = {
	{
		.compatible = "ti,palmas",
		.data = &palmas_data,
	},
	{
		.compatible = "ti,tps659038",
		.data = &tps659038_data,
	},
	{
		.compatible = "ti,tps65917",
		.data = &tps65917_data,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, of_palmas_match_tbl);

static int palmas_i2c_probe(struct i2c_client *i2c)
{
	struct palmas *palmas;
	struct palmas_platform_data *pdata;
	struct palmas_driver_data *driver_data;
	struct device_node *node = i2c->dev.of_node;
	int ret = 0, i;
	unsigned int reg, addr;
	int slave;

	pdata = dev_get_platdata(&i2c->dev);

	if (node && !pdata) {
		pdata = devm_kzalloc(&i2c->dev, sizeof(*pdata), GFP_KERNEL);

		if (!pdata)
			return -ENOMEM;

		palmas_dt_to_pdata(i2c, pdata);
	}

	if (!pdata)
		return -EINVAL;

	palmas = devm_kzalloc(&i2c->dev, sizeof(struct palmas), GFP_KERNEL);
	if (palmas == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, palmas);
	palmas->dev = &i2c->dev;
	palmas->irq = i2c->irq;

	driver_data = (struct palmas_driver_data *) device_get_match_data(&i2c->dev);
	palmas->features = *driver_data->features;

	for (i = 0; i < PALMAS_NUM_CLIENTS; i++) {
		if (i == 0)
			palmas->i2c_clients[i] = i2c;
		else {
			palmas->i2c_clients[i] =
					i2c_new_dummy_device(i2c->adapter,
							i2c->addr + i);
			if (IS_ERR(palmas->i2c_clients[i])) {
				dev_err(palmas->dev,
					"can't attach client %d\n", i);
				ret = PTR_ERR(palmas->i2c_clients[i]);
				goto err_i2c;
			}
			palmas->i2c_clients[i]->dev.of_node = of_node_get(node);
		}
		palmas->regmap[i] = devm_regmap_init_i2c(palmas->i2c_clients[i],
				&palmas_regmap_config[i]);
		if (IS_ERR(palmas->regmap[i])) {
			ret = PTR_ERR(palmas->regmap[i]);
			dev_err(palmas->dev,
				"Failed to allocate regmap %d, err: %d\n",
				i, ret);
			goto err_i2c;
		}
	}

	if (!palmas->irq) {
		dev_warn(palmas->dev, "IRQ missing: skipping irq request\n");
		goto no_irq;
	}

	/* Change interrupt line output polarity */
	if (pdata->irq_flags & IRQ_TYPE_LEVEL_HIGH)
		reg = PALMAS_POLARITY_CTRL_INT_POLARITY;
	else
		reg = 0;
	ret = palmas_update_bits(palmas, PALMAS_PU_PD_OD_BASE,
			PALMAS_POLARITY_CTRL, PALMAS_POLARITY_CTRL_INT_POLARITY,
			reg);
	if (ret < 0) {
		dev_err(palmas->dev, "POLARITY_CTRL update failed: %d\n", ret);
		goto err_i2c;
	}

	/* Change IRQ into clear on read mode for efficiency */
	slave = PALMAS_BASE_TO_SLAVE(PALMAS_INTERRUPT_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_INTERRUPT_BASE, PALMAS_INT_CTRL);
	reg = PALMAS_INT_CTRL_INT_CLEAR;

	regmap_write(palmas->regmap[slave], addr, reg);

	ret = regmap_add_irq_chip(palmas->regmap[slave], palmas->irq,
				  IRQF_ONESHOT | pdata->irq_flags, 0,
				  driver_data->irq_chip, &palmas->irq_data);
	if (ret < 0)
		goto err_i2c;

no_irq:
	slave = PALMAS_BASE_TO_SLAVE(PALMAS_PU_PD_OD_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_PU_PD_OD_BASE,
			PALMAS_PRIMARY_SECONDARY_PAD1);

	if (pdata->mux_from_pdata) {
		reg = pdata->pad1;
		ret = regmap_write(palmas->regmap[slave], addr, reg);
		if (ret)
			goto err_irq;
	} else {
		ret = regmap_read(palmas->regmap[slave], addr, &reg);
		if (ret)
			goto err_irq;
	}

	if (!(reg & PALMAS_PRIMARY_SECONDARY_PAD1_GPIO_0))
		palmas->gpio_muxed |= PALMAS_GPIO_0_MUXED;
	if (!(reg & PALMAS_PRIMARY_SECONDARY_PAD1_GPIO_1_MASK))
		palmas->gpio_muxed |= PALMAS_GPIO_1_MUXED;
	else if ((reg & PALMAS_PRIMARY_SECONDARY_PAD1_GPIO_1_MASK) ==
			(2 << PALMAS_PRIMARY_SECONDARY_PAD1_GPIO_1_SHIFT))
		palmas->led_muxed |= PALMAS_LED1_MUXED;
	else if ((reg & PALMAS_PRIMARY_SECONDARY_PAD1_GPIO_1_MASK) ==
			(3 << PALMAS_PRIMARY_SECONDARY_PAD1_GPIO_1_SHIFT))
		palmas->pwm_muxed |= PALMAS_PWM1_MUXED;
	if (!(reg & PALMAS_PRIMARY_SECONDARY_PAD1_GPIO_2_MASK))
		palmas->gpio_muxed |= PALMAS_GPIO_2_MUXED;
	else if ((reg & PALMAS_PRIMARY_SECONDARY_PAD1_GPIO_2_MASK) ==
			(2 << PALMAS_PRIMARY_SECONDARY_PAD1_GPIO_2_SHIFT))
		palmas->led_muxed |= PALMAS_LED2_MUXED;
	else if ((reg & PALMAS_PRIMARY_SECONDARY_PAD1_GPIO_2_MASK) ==
			(3 << PALMAS_PRIMARY_SECONDARY_PAD1_GPIO_2_SHIFT))
		palmas->pwm_muxed |= PALMAS_PWM2_MUXED;
	if (!(reg & PALMAS_PRIMARY_SECONDARY_PAD1_GPIO_3))
		palmas->gpio_muxed |= PALMAS_GPIO_3_MUXED;

	addr = PALMAS_BASE_TO_REG(PALMAS_PU_PD_OD_BASE,
			PALMAS_PRIMARY_SECONDARY_PAD2);

	if (pdata->mux_from_pdata) {
		reg = pdata->pad2;
		ret = regmap_write(palmas->regmap[slave], addr, reg);
		if (ret)
			goto err_irq;
	} else {
		ret = regmap_read(palmas->regmap[slave], addr, &reg);
		if (ret)
			goto err_irq;
	}

	if (!(reg & PALMAS_PRIMARY_SECONDARY_PAD2_GPIO_4))
		palmas->gpio_muxed |= PALMAS_GPIO_4_MUXED;
	if (!(reg & PALMAS_PRIMARY_SECONDARY_PAD2_GPIO_5_MASK))
		palmas->gpio_muxed |= PALMAS_GPIO_5_MUXED;
	if (!(reg & PALMAS_PRIMARY_SECONDARY_PAD2_GPIO_6))
		palmas->gpio_muxed |= PALMAS_GPIO_6_MUXED;
	if (!(reg & PALMAS_PRIMARY_SECONDARY_PAD2_GPIO_7_MASK))
		palmas->gpio_muxed |= PALMAS_GPIO_7_MUXED;

	dev_info(palmas->dev, "Muxing GPIO %x, PWM %x, LED %x\n",
			palmas->gpio_muxed, palmas->pwm_muxed,
			palmas->led_muxed);

	reg = pdata->power_ctrl;

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_PMU_CONTROL_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_PMU_CONTROL_BASE, PALMAS_POWER_CTRL);

	ret = regmap_write(palmas->regmap[slave], addr, reg);
	if (ret)
		goto err_irq;

	/*
	 * If we are probing with DT do this the DT way and return here
	 * otherwise continue and add devices using mfd helpers.
	 */
	if (node) {
		ret = devm_of_platform_populate(&i2c->dev);
		if (ret < 0) {
			goto err_irq;
		} else if (pdata->pm_off && !pm_power_off) {
			palmas_dev = palmas;
			pm_power_off = palmas_power_off;
		}
	}

	return ret;

err_irq:
	regmap_del_irq_chip(palmas->irq, palmas->irq_data);
err_i2c:
	for (i = 1; i < PALMAS_NUM_CLIENTS; i++) {
		if (palmas->i2c_clients[i])
			i2c_unregister_device(palmas->i2c_clients[i]);
	}
	return ret;
}

static void palmas_i2c_remove(struct i2c_client *i2c)
{
	struct palmas *palmas = i2c_get_clientdata(i2c);
	int i;

	regmap_del_irq_chip(palmas->irq, palmas->irq_data);

	for (i = 1; i < PALMAS_NUM_CLIENTS; i++) {
		if (palmas->i2c_clients[i])
			i2c_unregister_device(palmas->i2c_clients[i]);
	}

	if (palmas == palmas_dev) {
		pm_power_off = NULL;
		palmas_dev = NULL;
	}
}

static const struct i2c_device_id palmas_i2c_id[] = {
	{ "palmas", },
	{ "twl6035", },
	{ "twl6037", },
	{ "tps65913", },
	{ /* end */ }
};
MODULE_DEVICE_TABLE(i2c, palmas_i2c_id);

static struct i2c_driver palmas_i2c_driver = {
	.driver = {
		   .name = "palmas",
		   .of_match_table = of_palmas_match_tbl,
	},
	.probe = palmas_i2c_probe,
	.remove = palmas_i2c_remove,
	.id_table = palmas_i2c_id,
};

static int __init palmas_i2c_init(void)
{
	return i2c_add_driver(&palmas_i2c_driver);
}
/* init early so consumer devices can complete system boot */
subsys_initcall(palmas_i2c_init);

static void __exit palmas_i2c_exit(void)
{
	i2c_del_driver(&palmas_i2c_driver);
}
module_exit(palmas_i2c_exit);

MODULE_AUTHOR("Graeme Gregory <gg@slimlogic.co.uk>");
MODULE_DESCRIPTION("Palmas chip family multi-function driver");
MODULE_LICENSE("GPL");
