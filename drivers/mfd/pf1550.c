// SPDX-License-Identifier: GPL-2.0
/*
 * Core driver for the PF1550
 *
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Robin Gong <yibin.gong@freescale.com>
 *
 * Portions Copyright (c) 2025 Savoir-faire Linux Inc.
 * Samuel Kayode <samuel.kayode@savoirfairelinux.com>
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/mfd/pf1550.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

static const struct regmap_config pf1550_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = PF1550_PMIC_REG_END,
};

static const struct regmap_irq pf1550_irqs[] = {
	REGMAP_IRQ_REG(PF1550_IRQ_CHG, 0, IRQ_CHG),
	REGMAP_IRQ_REG(PF1550_IRQ_REGULATOR, 0, IRQ_REGULATOR),
	REGMAP_IRQ_REG(PF1550_IRQ_ONKEY, 0, IRQ_ONKEY),
};

static const struct regmap_irq_chip pf1550_irq_chip = {
	.name = "pf1550",
	.status_base = PF1550_PMIC_REG_INT_CATEGORY,
	.init_ack_masked = 1,
	.num_regs = 1,
	.irqs = pf1550_irqs,
	.num_irqs = ARRAY_SIZE(pf1550_irqs),
};

static const struct regmap_irq pf1550_regulator_irqs[] = {
	REGMAP_IRQ_REG(PF1550_PMIC_IRQ_SW1_LS, 0, PMIC_IRQ_SW1_LS),
	REGMAP_IRQ_REG(PF1550_PMIC_IRQ_SW2_LS, 0, PMIC_IRQ_SW2_LS),
	REGMAP_IRQ_REG(PF1550_PMIC_IRQ_SW3_LS, 0, PMIC_IRQ_SW3_LS),
	REGMAP_IRQ_REG(PF1550_PMIC_IRQ_SW1_HS, 3, PMIC_IRQ_SW1_HS),
	REGMAP_IRQ_REG(PF1550_PMIC_IRQ_SW2_HS, 3, PMIC_IRQ_SW2_HS),
	REGMAP_IRQ_REG(PF1550_PMIC_IRQ_SW3_HS, 3, PMIC_IRQ_SW3_HS),
	REGMAP_IRQ_REG(PF1550_PMIC_IRQ_LDO1_FAULT, 16, PMIC_IRQ_LDO1_FAULT),
	REGMAP_IRQ_REG(PF1550_PMIC_IRQ_LDO2_FAULT, 16, PMIC_IRQ_LDO2_FAULT),
	REGMAP_IRQ_REG(PF1550_PMIC_IRQ_LDO3_FAULT, 16, PMIC_IRQ_LDO3_FAULT),
	REGMAP_IRQ_REG(PF1550_PMIC_IRQ_TEMP_110, 24, PMIC_IRQ_TEMP_110),
	REGMAP_IRQ_REG(PF1550_PMIC_IRQ_TEMP_125, 24, PMIC_IRQ_TEMP_125),
};

static const struct regmap_irq_chip pf1550_regulator_irq_chip = {
	.name = "pf1550-regulator",
	.status_base = PF1550_PMIC_REG_SW_INT_STAT0,
	.ack_base = PF1550_PMIC_REG_SW_INT_STAT0,
	.mask_base = PF1550_PMIC_REG_SW_INT_MASK0,
	.use_ack = 1,
	.init_ack_masked = 1,
	.num_regs = 25,
	.irqs = pf1550_regulator_irqs,
	.num_irqs = ARRAY_SIZE(pf1550_regulator_irqs),
};

static const struct resource regulator_resources[] = {
	DEFINE_RES_IRQ(PF1550_PMIC_IRQ_SW1_LS),
	DEFINE_RES_IRQ(PF1550_PMIC_IRQ_SW2_LS),
	DEFINE_RES_IRQ(PF1550_PMIC_IRQ_SW3_LS),
	DEFINE_RES_IRQ(PF1550_PMIC_IRQ_SW1_HS),
	DEFINE_RES_IRQ(PF1550_PMIC_IRQ_SW2_HS),
	DEFINE_RES_IRQ(PF1550_PMIC_IRQ_SW3_HS),
	DEFINE_RES_IRQ(PF1550_PMIC_IRQ_LDO1_FAULT),
	DEFINE_RES_IRQ(PF1550_PMIC_IRQ_LDO2_FAULT),
	DEFINE_RES_IRQ(PF1550_PMIC_IRQ_LDO3_FAULT),
	DEFINE_RES_IRQ(PF1550_PMIC_IRQ_TEMP_110),
	DEFINE_RES_IRQ(PF1550_PMIC_IRQ_TEMP_125),
};

static const struct regmap_irq pf1550_onkey_irqs[] = {
	REGMAP_IRQ_REG(PF1550_ONKEY_IRQ_PUSHI, 0, ONKEY_IRQ_PUSHI),
	REGMAP_IRQ_REG(PF1550_ONKEY_IRQ_1SI, 0, ONKEY_IRQ_1SI),
	REGMAP_IRQ_REG(PF1550_ONKEY_IRQ_2SI, 0, ONKEY_IRQ_2SI),
	REGMAP_IRQ_REG(PF1550_ONKEY_IRQ_3SI, 0, ONKEY_IRQ_3SI),
	REGMAP_IRQ_REG(PF1550_ONKEY_IRQ_4SI, 0, ONKEY_IRQ_4SI),
	REGMAP_IRQ_REG(PF1550_ONKEY_IRQ_8SI, 0, ONKEY_IRQ_8SI),
};

static const struct regmap_irq_chip pf1550_onkey_irq_chip = {
	.name = "pf1550-onkey",
	.status_base = PF1550_PMIC_REG_ONKEY_INT_STAT0,
	.ack_base = PF1550_PMIC_REG_ONKEY_INT_STAT0,
	.mask_base = PF1550_PMIC_REG_ONKEY_INT_MASK0,
	.use_ack = 1,
	.init_ack_masked = 1,
	.num_regs = 1,
	.irqs = pf1550_onkey_irqs,
	.num_irqs = ARRAY_SIZE(pf1550_onkey_irqs),
};

static const struct resource onkey_resources[] = {
	DEFINE_RES_IRQ(PF1550_ONKEY_IRQ_PUSHI),
	DEFINE_RES_IRQ(PF1550_ONKEY_IRQ_1SI),
	DEFINE_RES_IRQ(PF1550_ONKEY_IRQ_2SI),
	DEFINE_RES_IRQ(PF1550_ONKEY_IRQ_3SI),
	DEFINE_RES_IRQ(PF1550_ONKEY_IRQ_4SI),
	DEFINE_RES_IRQ(PF1550_ONKEY_IRQ_8SI),
};

static const struct regmap_irq pf1550_charger_irqs[] = {
	REGMAP_IRQ_REG(PF1550_CHARG_IRQ_BAT2SOCI, 0, CHARG_IRQ_BAT2SOCI),
	REGMAP_IRQ_REG(PF1550_CHARG_IRQ_BATI, 0, CHARG_IRQ_BATI),
	REGMAP_IRQ_REG(PF1550_CHARG_IRQ_CHGI, 0, CHARG_IRQ_CHGI),
	REGMAP_IRQ_REG(PF1550_CHARG_IRQ_VBUSI, 0, CHARG_IRQ_VBUSI),
	REGMAP_IRQ_REG(PF1550_CHARG_IRQ_THMI, 0, CHARG_IRQ_THMI),
};

static const struct regmap_irq_chip pf1550_charger_irq_chip = {
	.name = "pf1550-charger",
	.status_base = PF1550_CHARG_REG_CHG_INT,
	.ack_base = PF1550_CHARG_REG_CHG_INT,
	.mask_base = PF1550_CHARG_REG_CHG_INT_MASK,
	.use_ack = 1,
	.init_ack_masked = 1,
	.num_regs = 1,
	.irqs = pf1550_charger_irqs,
	.num_irqs = ARRAY_SIZE(pf1550_charger_irqs),
};

static const struct resource charger_resources[] = {
	DEFINE_RES_IRQ(PF1550_CHARG_IRQ_BAT2SOCI),
	DEFINE_RES_IRQ(PF1550_CHARG_IRQ_BATI),
	DEFINE_RES_IRQ(PF1550_CHARG_IRQ_CHGI),
	DEFINE_RES_IRQ(PF1550_CHARG_IRQ_VBUSI),
	DEFINE_RES_IRQ(PF1550_CHARG_IRQ_THMI),
};

static const struct mfd_cell pf1550_regulator_cell = {
	.name = "pf1550-regulator",
	.num_resources = ARRAY_SIZE(regulator_resources),
	.resources = regulator_resources,
};

static const struct mfd_cell pf1550_onkey_cell = {
	.name = "pf1550-onkey",
	.num_resources = ARRAY_SIZE(onkey_resources),
	.resources = onkey_resources,
};

static const struct mfd_cell pf1550_charger_cell = {
	.name = "pf1550-charger",
	.num_resources = ARRAY_SIZE(charger_resources),
	.resources = charger_resources,
};

/*
 * The PF1550 is shipped in variants of A0, A1,...A9. Each variant defines a
 * configuration of the PMIC in a One-Time Programmable (OTP) memory.
 * This memory is accessed indirectly by writing valid keys to specific
 * registers of the PMIC. To read the OTP memory after writing the valid keys,
 * the OTP register address to be read is written to pf1550 register 0xc4 and
 * its value read from pf1550 register 0xc5.
 */
static int pf1550_read_otp(const struct pf1550_ddata *pf1550, unsigned int index,
			   unsigned int *val)
{
	int ret = 0;

	ret = regmap_write(pf1550->regmap, PF1550_PMIC_REG_KEY, PF1550_OTP_PMIC_KEY);
	if (ret)
		goto read_err;

	ret = regmap_write(pf1550->regmap, PF1550_CHARG_REG_CHGR_KEY2, PF1550_OTP_CHGR_KEY);
	if (ret)
		goto read_err;

	ret = regmap_write(pf1550->regmap, PF1550_TEST_REG_KEY3, PF1550_OTP_TEST_KEY);
	if (ret)
		goto read_err;

	ret = regmap_write(pf1550->regmap, PF1550_TEST_REG_FMRADDR, index);
	if (ret)
		goto read_err;

	ret = regmap_read(pf1550->regmap, PF1550_TEST_REG_FMRDATA, val);
	if (ret)
		goto read_err;

	return 0;

read_err:
	return dev_err_probe(pf1550->dev, ret, "OTP reg %x not found!\n", index);
}

static int pf1550_i2c_probe(struct i2c_client *i2c)
{
	const struct mfd_cell *regulator = &pf1550_regulator_cell;
	const struct mfd_cell *charger = &pf1550_charger_cell;
	const struct mfd_cell *onkey = &pf1550_onkey_cell;
	unsigned int reg_data = 0, otp_data = 0;
	struct pf1550_ddata *pf1550;
	struct irq_domain *domain;
	int irq, ret = 0;

	pf1550 = devm_kzalloc(&i2c->dev, sizeof(*pf1550), GFP_KERNEL);
	if (!pf1550)
		return -ENOMEM;

	i2c_set_clientdata(i2c, pf1550);
	pf1550->dev = &i2c->dev;
	pf1550->irq = i2c->irq;

	pf1550->regmap = devm_regmap_init_i2c(i2c, &pf1550_regmap_config);
	if (IS_ERR(pf1550->regmap))
		return dev_err_probe(pf1550->dev, PTR_ERR(pf1550->regmap),
				     "failed to allocate register map\n");

	ret = regmap_read(pf1550->regmap, PF1550_PMIC_REG_DEVICE_ID, &reg_data);
	if (ret < 0)
		return dev_err_probe(pf1550->dev, ret, "cannot read chip ID\n");
	if (reg_data != PF1550_DEVICE_ID)
		return dev_err_probe(pf1550->dev, -ENODEV, "invalid device ID: 0x%02x\n", reg_data);

	/* Regulator DVS for SW2 */
	ret = pf1550_read_otp(pf1550, PF1550_OTP_SW2_SW3, &otp_data);
	if (ret)
		return ret;

	/* When clear, DVS should be enabled */
	if (!(otp_data & OTP_SW2_DVS_ENB))
		pf1550->dvs2_enable = true;

	/* Regulator DVS for SW1 */
	ret = pf1550_read_otp(pf1550, PF1550_OTP_SW1_SW2, &otp_data);
	if (ret)
		return ret;

	if (!(otp_data & OTP_SW1_DVS_ENB))
		pf1550->dvs1_enable = true;

	/* Add top level interrupts */
	ret = devm_regmap_add_irq_chip(pf1550->dev, pf1550->regmap, pf1550->irq,
				       IRQF_ONESHOT | IRQF_SHARED |
				       IRQF_TRIGGER_FALLING,
				       0, &pf1550_irq_chip,
				       &pf1550->irq_data);
	if (ret)
		return ret;

	/* Add regulator */
	irq = regmap_irq_get_virq(pf1550->irq_data, PF1550_IRQ_REGULATOR);
	if (irq < 0)
		return dev_err_probe(pf1550->dev, irq,
				     "Failed to get parent vIRQ(%d) for chip %s\n",
				     PF1550_IRQ_REGULATOR, pf1550_irq_chip.name);

	ret = devm_regmap_add_irq_chip(pf1550->dev, pf1550->regmap, irq,
				       IRQF_ONESHOT | IRQF_SHARED |
				       IRQF_TRIGGER_FALLING, 0,
				       &pf1550_regulator_irq_chip,
				       &pf1550->irq_data_regulator);
	if (ret)
		return dev_err_probe(pf1550->dev, ret, "Failed to add %s IRQ chip\n",
				     pf1550_regulator_irq_chip.name);

	domain = regmap_irq_get_domain(pf1550->irq_data_regulator);

	ret = devm_mfd_add_devices(pf1550->dev, PLATFORM_DEVID_NONE, regulator, 1, NULL, 0, domain);
	if (ret)
		return ret;

	/* Add onkey */
	irq = regmap_irq_get_virq(pf1550->irq_data, PF1550_IRQ_ONKEY);
	if (irq < 0)
		return dev_err_probe(pf1550->dev, irq,
				     "Failed to get parent vIRQ(%d) for chip %s\n",
				     PF1550_IRQ_ONKEY, pf1550_irq_chip.name);

	ret = devm_regmap_add_irq_chip(pf1550->dev, pf1550->regmap, irq,
				       IRQF_ONESHOT | IRQF_SHARED |
				       IRQF_TRIGGER_FALLING, 0,
				       &pf1550_onkey_irq_chip,
				       &pf1550->irq_data_onkey);
	if (ret)
		return dev_err_probe(pf1550->dev, ret, "Failed to add %s IRQ chip\n",
				     pf1550_onkey_irq_chip.name);

	domain = regmap_irq_get_domain(pf1550->irq_data_onkey);

	ret = devm_mfd_add_devices(pf1550->dev, PLATFORM_DEVID_NONE, onkey, 1, NULL, 0, domain);
	if (ret)
		return ret;

	/* Add battery charger */
	irq = regmap_irq_get_virq(pf1550->irq_data, PF1550_IRQ_CHG);
	if (irq < 0)
		return dev_err_probe(pf1550->dev, irq,
				     "Failed to get parent vIRQ(%d) for chip %s\n",
				     PF1550_IRQ_CHG, pf1550_irq_chip.name);

	ret = devm_regmap_add_irq_chip(pf1550->dev, pf1550->regmap, irq,
				       IRQF_ONESHOT | IRQF_SHARED |
				       IRQF_TRIGGER_FALLING, 0,
				       &pf1550_charger_irq_chip,
				       &pf1550->irq_data_charger);
	if (ret)
		return dev_err_probe(pf1550->dev, ret, "Failed to add %s IRQ chip\n",
				     pf1550_charger_irq_chip.name);

	domain = regmap_irq_get_domain(pf1550->irq_data_charger);

	return devm_mfd_add_devices(pf1550->dev, PLATFORM_DEVID_NONE, charger, 1, NULL, 0, domain);
}

static int pf1550_suspend(struct device *dev)
{
	struct pf1550_ddata *pf1550 = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		enable_irq_wake(pf1550->irq);
		disable_irq(pf1550->irq);
	}

	return 0;
}

static int pf1550_resume(struct device *dev)
{
	struct pf1550_ddata *pf1550 = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		disable_irq_wake(pf1550->irq);
		enable_irq(pf1550->irq);
	}

	return 0;
}
static DEFINE_SIMPLE_DEV_PM_OPS(pf1550_pm, pf1550_suspend, pf1550_resume);

static const struct i2c_device_id pf1550_i2c_id[] = {
	{ "pf1550" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, pf1550_i2c_id);

static const struct of_device_id pf1550_dt_match[] = {
	{ .compatible = "nxp,pf1550" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pf1550_dt_match);

static struct i2c_driver pf1550_i2c_driver = {
	.driver = {
		   .name = "pf1550",
		   .pm = pm_sleep_ptr(&pf1550_pm),
		   .of_match_table = pf1550_dt_match,
	},
	.probe = pf1550_i2c_probe,
	.id_table = pf1550_i2c_id,
};
module_i2c_driver(pf1550_i2c_driver);

MODULE_DESCRIPTION("NXP PF1550 core driver");
MODULE_AUTHOR("Robin Gong <yibin.gong@freescale.com>");
MODULE_LICENSE("GPL");
