// SPDX-License-Identifier: GPL-2.0
// Copyright (C) STMicroelectronics 2018
// Author: Pascal Paillet <p.paillet@st.com>

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/mfd/stpmic1.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pm_wakeirq.h>
#include <linux/regmap.h>

#include <dt-bindings/mfd/st,stpmic1.h>

#define STPMIC1_MAIN_IRQ 0

static const struct regmap_range stpmic1_readable_ranges[] = {
	regmap_reg_range(TURN_ON_SR, VERSION_SR),
	regmap_reg_range(MAIN_CR, LDO6_STDBY_CR),
	regmap_reg_range(BST_SW_CR, BST_SW_CR),
	regmap_reg_range(INT_PENDING_R1, INT_PENDING_R4),
	regmap_reg_range(INT_CLEAR_R1, INT_CLEAR_R4),
	regmap_reg_range(INT_MASK_R1, INT_MASK_R4),
	regmap_reg_range(INT_SET_MASK_R1, INT_SET_MASK_R4),
	regmap_reg_range(INT_CLEAR_MASK_R1, INT_CLEAR_MASK_R4),
	regmap_reg_range(INT_SRC_R1, INT_SRC_R1),
};

static const struct regmap_range stpmic1_writeable_ranges[] = {
	regmap_reg_range(MAIN_CR, LDO6_STDBY_CR),
	regmap_reg_range(BST_SW_CR, BST_SW_CR),
	regmap_reg_range(INT_CLEAR_R1, INT_CLEAR_R4),
	regmap_reg_range(INT_SET_MASK_R1, INT_SET_MASK_R4),
	regmap_reg_range(INT_CLEAR_MASK_R1, INT_CLEAR_MASK_R4),
};

static const struct regmap_range stpmic1_volatile_ranges[] = {
	regmap_reg_range(TURN_ON_SR, VERSION_SR),
	regmap_reg_range(WCHDG_CR, WCHDG_CR),
	regmap_reg_range(INT_PENDING_R1, INT_PENDING_R4),
	regmap_reg_range(INT_SRC_R1, INT_SRC_R4),
};

static const struct regmap_access_table stpmic1_readable_table = {
	.yes_ranges = stpmic1_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(stpmic1_readable_ranges),
};

static const struct regmap_access_table stpmic1_writeable_table = {
	.yes_ranges = stpmic1_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(stpmic1_writeable_ranges),
};

static const struct regmap_access_table stpmic1_volatile_table = {
	.yes_ranges = stpmic1_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(stpmic1_volatile_ranges),
};

static const struct regmap_config stpmic1_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.max_register = PMIC_MAX_REGISTER_ADDRESS,
	.rd_table = &stpmic1_readable_table,
	.wr_table = &stpmic1_writeable_table,
	.volatile_table = &stpmic1_volatile_table,
};

static const struct regmap_irq stpmic1_irqs[] = {
	REGMAP_IRQ_REG(IT_PONKEY_F, 0, 0x01),
	REGMAP_IRQ_REG(IT_PONKEY_R, 0, 0x02),
	REGMAP_IRQ_REG(IT_WAKEUP_F, 0, 0x04),
	REGMAP_IRQ_REG(IT_WAKEUP_R, 0, 0x08),
	REGMAP_IRQ_REG(IT_VBUS_OTG_F, 0, 0x10),
	REGMAP_IRQ_REG(IT_VBUS_OTG_R, 0, 0x20),
	REGMAP_IRQ_REG(IT_SWOUT_F, 0, 0x40),
	REGMAP_IRQ_REG(IT_SWOUT_R, 0, 0x80),

	REGMAP_IRQ_REG(IT_CURLIM_BUCK1, 1, 0x01),
	REGMAP_IRQ_REG(IT_CURLIM_BUCK2, 1, 0x02),
	REGMAP_IRQ_REG(IT_CURLIM_BUCK3, 1, 0x04),
	REGMAP_IRQ_REG(IT_CURLIM_BUCK4, 1, 0x08),
	REGMAP_IRQ_REG(IT_OCP_OTG, 1, 0x10),
	REGMAP_IRQ_REG(IT_OCP_SWOUT, 1, 0x20),
	REGMAP_IRQ_REG(IT_OCP_BOOST, 1, 0x40),
	REGMAP_IRQ_REG(IT_OVP_BOOST, 1, 0x80),

	REGMAP_IRQ_REG(IT_CURLIM_LDO1, 2, 0x01),
	REGMAP_IRQ_REG(IT_CURLIM_LDO2, 2, 0x02),
	REGMAP_IRQ_REG(IT_CURLIM_LDO3, 2, 0x04),
	REGMAP_IRQ_REG(IT_CURLIM_LDO4, 2, 0x08),
	REGMAP_IRQ_REG(IT_CURLIM_LDO5, 2, 0x10),
	REGMAP_IRQ_REG(IT_CURLIM_LDO6, 2, 0x20),
	REGMAP_IRQ_REG(IT_SHORT_SWOTG, 2, 0x40),
	REGMAP_IRQ_REG(IT_SHORT_SWOUT, 2, 0x80),

	REGMAP_IRQ_REG(IT_TWARN_F, 3, 0x01),
	REGMAP_IRQ_REG(IT_TWARN_R, 3, 0x02),
	REGMAP_IRQ_REG(IT_VINLOW_F, 3, 0x04),
	REGMAP_IRQ_REG(IT_VINLOW_R, 3, 0x08),
	REGMAP_IRQ_REG(IT_SWIN_F, 3, 0x40),
	REGMAP_IRQ_REG(IT_SWIN_R, 3, 0x80),
};

static const struct regmap_irq_chip stpmic1_regmap_irq_chip = {
	.name = "pmic_irq",
	.status_base = INT_PENDING_R1,
	.mask_base = INT_SET_MASK_R1,
	.unmask_base = INT_CLEAR_MASK_R1,
	.mask_unmask_non_inverted = true,
	.ack_base = INT_CLEAR_R1,
	.num_regs = STPMIC1_PMIC_NUM_IRQ_REGS,
	.irqs = stpmic1_irqs,
	.num_irqs = ARRAY_SIZE(stpmic1_irqs),
};

static int stpmic1_power_off(struct sys_off_data *data)
{
	struct stpmic1 *ddata = data->cb_data;

	regmap_update_bits(ddata->regmap, MAIN_CR,
			   SOFTWARE_SWITCH_OFF, SOFTWARE_SWITCH_OFF);

	return NOTIFY_DONE;
}

static int stpmic1_probe(struct i2c_client *i2c)
{
	struct stpmic1 *ddata;
	struct device *dev = &i2c->dev;
	int ret;
	struct device_node *np = dev->of_node;
	u32 reg;

	ddata = devm_kzalloc(dev, sizeof(struct stpmic1), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	i2c_set_clientdata(i2c, ddata);
	ddata->dev = dev;

	ddata->regmap = devm_regmap_init_i2c(i2c, &stpmic1_regmap_config);
	if (IS_ERR(ddata->regmap))
		return PTR_ERR(ddata->regmap);

	ddata->irq = of_irq_get(np, STPMIC1_MAIN_IRQ);
	if (ddata->irq < 0) {
		dev_err(dev, "Failed to get main IRQ: %d\n", ddata->irq);
		return ddata->irq;
	}

	ret = regmap_read(ddata->regmap, VERSION_SR, &reg);
	if (ret) {
		dev_err(dev, "Unable to read PMIC version\n");
		return ret;
	}
	dev_info(dev, "PMIC Chip Version: 0x%x\n", reg);

	/* Initialize PMIC IRQ Chip & associated IRQ domains */
	ret = devm_regmap_add_irq_chip(dev, ddata->regmap, ddata->irq,
				       IRQF_ONESHOT | IRQF_SHARED,
				       0, &stpmic1_regmap_irq_chip,
				       &ddata->irq_data);
	if (ret) {
		dev_err(dev, "IRQ Chip registration failed: %d\n", ret);
		return ret;
	}

	ret = devm_register_sys_off_handler(ddata->dev,
					    SYS_OFF_MODE_POWER_OFF,
					    SYS_OFF_PRIO_DEFAULT,
					    stpmic1_power_off,
					    ddata);
	if (ret) {
		dev_err(ddata->dev, "failed to register sys-off handler: %d\n", ret);
		return ret;
	}

	return devm_of_platform_populate(dev);
}

static int stpmic1_suspend(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct stpmic1 *pmic_dev = i2c_get_clientdata(i2c);

	disable_irq(pmic_dev->irq);

	return 0;
}

static int stpmic1_resume(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct stpmic1 *pmic_dev = i2c_get_clientdata(i2c);
	int ret;

	ret = regcache_sync(pmic_dev->regmap);
	if (ret)
		return ret;

	enable_irq(pmic_dev->irq);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(stpmic1_pm, stpmic1_suspend, stpmic1_resume);

static const struct of_device_id stpmic1_of_match[] = {
	{ .compatible = "st,stpmic1", },
	{},
};
MODULE_DEVICE_TABLE(of, stpmic1_of_match);

static struct i2c_driver stpmic1_driver = {
	.driver = {
		.name = "stpmic1",
		.of_match_table = stpmic1_of_match,
		.pm = pm_sleep_ptr(&stpmic1_pm),
	},
	.probe = stpmic1_probe,
};

module_i2c_driver(stpmic1_driver);

MODULE_DESCRIPTION("STPMIC1 PMIC Driver");
MODULE_AUTHOR("Pascal Paillet <p.paillet@st.com>");
MODULE_LICENSE("GPL v2");
