// SPDX-License-Identifier: GPL-2.0-or-later
//
// Copyright (C) 2019 ROHM Semiconductors
//
// ROHM BD70528 PMIC driver

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rohm-bd70528.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/types.h>

#define BD70528_NUM_OF_GPIOS 4

static const struct resource rtc_irqs[] = {
	DEFINE_RES_IRQ_NAMED(BD70528_INT_RTC_ALARM, "bd70528-rtc-alm"),
	DEFINE_RES_IRQ_NAMED(BD70528_INT_ELPS_TIM, "bd70528-elapsed-timer"),
};

static const struct resource charger_irqs[] = {
	DEFINE_RES_IRQ_NAMED(BD70528_INT_BAT_OV_RES, "bd70528-bat-ov-res"),
	DEFINE_RES_IRQ_NAMED(BD70528_INT_BAT_OV_DET, "bd70528-bat-ov-det"),
	DEFINE_RES_IRQ_NAMED(BD70528_INT_DBAT_DET, "bd70528-bat-dead"),
	DEFINE_RES_IRQ_NAMED(BD70528_INT_BATTSD_COLD_RES, "bd70528-bat-warmed"),
	DEFINE_RES_IRQ_NAMED(BD70528_INT_BATTSD_COLD_DET, "bd70528-bat-cold"),
	DEFINE_RES_IRQ_NAMED(BD70528_INT_BATTSD_HOT_RES, "bd70528-bat-cooled"),
	DEFINE_RES_IRQ_NAMED(BD70528_INT_BATTSD_HOT_DET, "bd70528-bat-hot"),
	DEFINE_RES_IRQ_NAMED(BD70528_INT_CHG_TSD, "bd70528-chg-tshd"),
	DEFINE_RES_IRQ_NAMED(BD70528_INT_BAT_RMV, "bd70528-bat-removed"),
	DEFINE_RES_IRQ_NAMED(BD70528_INT_BAT_DET, "bd70528-bat-detected"),
	DEFINE_RES_IRQ_NAMED(BD70528_INT_DCIN2_OV_RES, "bd70528-dcin2-ov-res"),
	DEFINE_RES_IRQ_NAMED(BD70528_INT_DCIN2_OV_DET, "bd70528-dcin2-ov-det"),
	DEFINE_RES_IRQ_NAMED(BD70528_INT_DCIN2_RMV, "bd70528-dcin2-removed"),
	DEFINE_RES_IRQ_NAMED(BD70528_INT_DCIN2_DET, "bd70528-dcin2-detected"),
	DEFINE_RES_IRQ_NAMED(BD70528_INT_DCIN1_RMV, "bd70528-dcin1-removed"),
	DEFINE_RES_IRQ_NAMED(BD70528_INT_DCIN1_DET, "bd70528-dcin1-detected"),
};

static struct mfd_cell bd70528_mfd_cells[] = {
	{ .name = "bd70528-pmic", },
	{ .name = "bd70528-gpio", },
	/*
	 * We use BD71837 driver to drive the clock block. Only differences to
	 * BD70528 clock gate are the register address and mask.
	 */
	{ .name = "bd718xx-clk", },
	{ .name = "bd70528-wdt", },
	{
		.name = "bd70528-power",
		.resources = charger_irqs,
		.num_resources = ARRAY_SIZE(charger_irqs),
	}, {
		.name = "bd70528-rtc",
		.resources = rtc_irqs,
		.num_resources = ARRAY_SIZE(rtc_irqs),
	},
};

static const struct regmap_range volatile_ranges[] = {
	{
		.range_min = BD70528_REG_INT_MAIN,
		.range_max = BD70528_REG_INT_OP_FAIL,
	}, {
		.range_min = BD70528_REG_RTC_COUNT_H,
		.range_max = BD70528_REG_RTC_ALM_REPEAT,
	}, {
		/*
		 * WDT control reg is special. Magic values must be written to
		 * it in order to change the control. Should not be cached.
		 */
		.range_min = BD70528_REG_WDT_CTRL,
		.range_max = BD70528_REG_WDT_CTRL,
	}, {
		/*
		 * BD70528 also contains a few other registers which require
		 * magic sequences to be written in order to update the value.
		 * At least SHIPMODE, HWRESET, WARMRESET,and STANDBY
		 */
		.range_min = BD70528_REG_SHIPMODE,
		.range_max = BD70528_REG_STANDBY,
	},
};

static const struct regmap_access_table volatile_regs = {
	.yes_ranges = &volatile_ranges[0],
	.n_yes_ranges = ARRAY_SIZE(volatile_ranges),
};

static struct regmap_config bd70528_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &volatile_regs,
	.max_register = BD70528_MAX_REGISTER,
	.cache_type = REGCACHE_RBTREE,
};

/*
 * Mapping of main IRQ register bits to sub-IRQ register offsets so that we can
 * access corect sub-IRQ registers based on bits that are set in main IRQ
 * register.
 */

/* bit [0] - Shutdown register */
unsigned int bit0_offsets[] = {0};	/* Shutdown register */
unsigned int bit1_offsets[] = {1};	/* Power failure register */
unsigned int bit2_offsets[] = {2};	/* VR FAULT register */
unsigned int bit3_offsets[] = {3};	/* PMU register interrupts */
unsigned int bit4_offsets[] = {4, 5};	/* Charger 1 and Charger 2 registers */
unsigned int bit5_offsets[] = {6};	/* RTC register */
unsigned int bit6_offsets[] = {7};	/* GPIO register */
unsigned int bit7_offsets[] = {8};	/* Invalid operation register */

static struct regmap_irq_sub_irq_map bd70528_sub_irq_offsets[] = {
	REGMAP_IRQ_MAIN_REG_OFFSET(bit0_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit1_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit2_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit3_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit4_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit5_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit6_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit7_offsets),
};

static struct regmap_irq bd70528_irqs[] = {
	REGMAP_IRQ_REG(BD70528_INT_LONGPUSH, 0, BD70528_INT_LONGPUSH_MASK),
	REGMAP_IRQ_REG(BD70528_INT_WDT, 0, BD70528_INT_WDT_MASK),
	REGMAP_IRQ_REG(BD70528_INT_HWRESET, 0, BD70528_INT_HWRESET_MASK),
	REGMAP_IRQ_REG(BD70528_INT_RSTB_FAULT, 0, BD70528_INT_RSTB_FAULT_MASK),
	REGMAP_IRQ_REG(BD70528_INT_VBAT_UVLO, 0, BD70528_INT_VBAT_UVLO_MASK),
	REGMAP_IRQ_REG(BD70528_INT_TSD, 0, BD70528_INT_TSD_MASK),
	REGMAP_IRQ_REG(BD70528_INT_RSTIN, 0, BD70528_INT_RSTIN_MASK),
	REGMAP_IRQ_REG(BD70528_INT_BUCK1_FAULT, 1,
		       BD70528_INT_BUCK1_FAULT_MASK),
	REGMAP_IRQ_REG(BD70528_INT_BUCK2_FAULT, 1,
		       BD70528_INT_BUCK2_FAULT_MASK),
	REGMAP_IRQ_REG(BD70528_INT_BUCK3_FAULT, 1,
		       BD70528_INT_BUCK3_FAULT_MASK),
	REGMAP_IRQ_REG(BD70528_INT_LDO1_FAULT, 1, BD70528_INT_LDO1_FAULT_MASK),
	REGMAP_IRQ_REG(BD70528_INT_LDO2_FAULT, 1, BD70528_INT_LDO2_FAULT_MASK),
	REGMAP_IRQ_REG(BD70528_INT_LDO3_FAULT, 1, BD70528_INT_LDO3_FAULT_MASK),
	REGMAP_IRQ_REG(BD70528_INT_LED1_FAULT, 1, BD70528_INT_LED1_FAULT_MASK),
	REGMAP_IRQ_REG(BD70528_INT_LED2_FAULT, 1, BD70528_INT_LED2_FAULT_MASK),
	REGMAP_IRQ_REG(BD70528_INT_BUCK1_OCP, 2, BD70528_INT_BUCK1_OCP_MASK),
	REGMAP_IRQ_REG(BD70528_INT_BUCK2_OCP, 2, BD70528_INT_BUCK2_OCP_MASK),
	REGMAP_IRQ_REG(BD70528_INT_BUCK3_OCP, 2, BD70528_INT_BUCK3_OCP_MASK),
	REGMAP_IRQ_REG(BD70528_INT_LED1_OCP, 2, BD70528_INT_LED1_OCP_MASK),
	REGMAP_IRQ_REG(BD70528_INT_LED2_OCP, 2, BD70528_INT_LED2_OCP_MASK),
	REGMAP_IRQ_REG(BD70528_INT_BUCK1_FULLON, 2,
		       BD70528_INT_BUCK1_FULLON_MASK),
	REGMAP_IRQ_REG(BD70528_INT_BUCK2_FULLON, 2,
		       BD70528_INT_BUCK2_FULLON_MASK),
	REGMAP_IRQ_REG(BD70528_INT_SHORTPUSH, 3, BD70528_INT_SHORTPUSH_MASK),
	REGMAP_IRQ_REG(BD70528_INT_AUTO_WAKEUP, 3,
		       BD70528_INT_AUTO_WAKEUP_MASK),
	REGMAP_IRQ_REG(BD70528_INT_STATE_CHANGE, 3,
		       BD70528_INT_STATE_CHANGE_MASK),
	REGMAP_IRQ_REG(BD70528_INT_BAT_OV_RES, 4, BD70528_INT_BAT_OV_RES_MASK),
	REGMAP_IRQ_REG(BD70528_INT_BAT_OV_DET, 4, BD70528_INT_BAT_OV_DET_MASK),
	REGMAP_IRQ_REG(BD70528_INT_DBAT_DET, 4, BD70528_INT_DBAT_DET_MASK),
	REGMAP_IRQ_REG(BD70528_INT_BATTSD_COLD_RES, 4,
		       BD70528_INT_BATTSD_COLD_RES_MASK),
	REGMAP_IRQ_REG(BD70528_INT_BATTSD_COLD_DET, 4,
		       BD70528_INT_BATTSD_COLD_DET_MASK),
	REGMAP_IRQ_REG(BD70528_INT_BATTSD_HOT_RES, 4,
		       BD70528_INT_BATTSD_HOT_RES_MASK),
	REGMAP_IRQ_REG(BD70528_INT_BATTSD_HOT_DET, 4,
		       BD70528_INT_BATTSD_HOT_DET_MASK),
	REGMAP_IRQ_REG(BD70528_INT_CHG_TSD, 4, BD70528_INT_CHG_TSD_MASK),
	REGMAP_IRQ_REG(BD70528_INT_BAT_RMV, 5, BD70528_INT_BAT_RMV_MASK),
	REGMAP_IRQ_REG(BD70528_INT_BAT_DET, 5, BD70528_INT_BAT_DET_MASK),
	REGMAP_IRQ_REG(BD70528_INT_DCIN2_OV_RES, 5,
		       BD70528_INT_DCIN2_OV_RES_MASK),
	REGMAP_IRQ_REG(BD70528_INT_DCIN2_OV_DET, 5,
		       BD70528_INT_DCIN2_OV_DET_MASK),
	REGMAP_IRQ_REG(BD70528_INT_DCIN2_RMV, 5, BD70528_INT_DCIN2_RMV_MASK),
	REGMAP_IRQ_REG(BD70528_INT_DCIN2_DET, 5, BD70528_INT_DCIN2_DET_MASK),
	REGMAP_IRQ_REG(BD70528_INT_DCIN1_RMV, 5, BD70528_INT_DCIN1_RMV_MASK),
	REGMAP_IRQ_REG(BD70528_INT_DCIN1_DET, 5, BD70528_INT_DCIN1_DET_MASK),
	REGMAP_IRQ_REG(BD70528_INT_RTC_ALARM, 6, BD70528_INT_RTC_ALARM_MASK),
	REGMAP_IRQ_REG(BD70528_INT_ELPS_TIM, 6, BD70528_INT_ELPS_TIM_MASK),
	REGMAP_IRQ_REG(BD70528_INT_GPIO0, 7, BD70528_INT_GPIO0_MASK),
	REGMAP_IRQ_REG(BD70528_INT_GPIO1, 7, BD70528_INT_GPIO1_MASK),
	REGMAP_IRQ_REG(BD70528_INT_GPIO2, 7, BD70528_INT_GPIO2_MASK),
	REGMAP_IRQ_REG(BD70528_INT_GPIO3, 7, BD70528_INT_GPIO3_MASK),
	REGMAP_IRQ_REG(BD70528_INT_BUCK1_DVS_OPFAIL, 8,
		       BD70528_INT_BUCK1_DVS_OPFAIL_MASK),
	REGMAP_IRQ_REG(BD70528_INT_BUCK2_DVS_OPFAIL, 8,
		       BD70528_INT_BUCK2_DVS_OPFAIL_MASK),
	REGMAP_IRQ_REG(BD70528_INT_BUCK3_DVS_OPFAIL, 8,
		       BD70528_INT_BUCK3_DVS_OPFAIL_MASK),
	REGMAP_IRQ_REG(BD70528_INT_LED1_VOLT_OPFAIL, 8,
		       BD70528_INT_LED1_VOLT_OPFAIL_MASK),
	REGMAP_IRQ_REG(BD70528_INT_LED2_VOLT_OPFAIL, 8,
		       BD70528_INT_LED2_VOLT_OPFAIL_MASK),
};

static struct regmap_irq_chip bd70528_irq_chip = {
	.name = "bd70528_irq",
	.main_status = BD70528_REG_INT_MAIN,
	.irqs = &bd70528_irqs[0],
	.num_irqs = ARRAY_SIZE(bd70528_irqs),
	.status_base = BD70528_REG_INT_SHDN,
	.mask_base = BD70528_REG_INT_SHDN_MASK,
	.ack_base = BD70528_REG_INT_SHDN,
	.type_base = BD70528_REG_GPIO1_IN,
	.init_ack_masked = true,
	.num_regs = 9,
	.num_main_regs = 1,
	.num_type_reg = 4,
	.sub_reg_offsets = &bd70528_sub_irq_offsets[0],
	.num_main_status_bits = 8,
	.irq_reg_stride = 1,
};

static int bd70528_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct bd70528_data *bd70528;
	struct regmap_irq_chip_data *irq_data;
	int ret, i;

	if (!i2c->irq) {
		dev_err(&i2c->dev, "No IRQ configured\n");
		return -EINVAL;
	}

	bd70528 = devm_kzalloc(&i2c->dev, sizeof(*bd70528), GFP_KERNEL);
	if (!bd70528)
		return -ENOMEM;

	mutex_init(&bd70528->rtc_timer_lock);

	dev_set_drvdata(&i2c->dev, &bd70528->chip);

	bd70528->chip.chip_type = ROHM_CHIP_TYPE_BD70528;
	bd70528->chip.regmap = devm_regmap_init_i2c(i2c, &bd70528_regmap);
	if (IS_ERR(bd70528->chip.regmap)) {
		dev_err(&i2c->dev, "Failed to initialize Regmap\n");
		return PTR_ERR(bd70528->chip.regmap);
	}

	/*
	 * Disallow type setting for all IRQs by default as most of them do not
	 * support setting type.
	 */
	for (i = 0; i < ARRAY_SIZE(bd70528_irqs); i++)
		bd70528_irqs[i].type.types_supported = 0;

	/* Set IRQ typesetting information for GPIO pins 0 - 3 */
	for (i = 0; i < BD70528_NUM_OF_GPIOS; i++) {
		struct regmap_irq_type *type;

		type = &bd70528_irqs[BD70528_INT_GPIO0 + i].type;
		type->type_reg_offset = 2 * i;
		type->type_rising_val = 0x20;
		type->type_falling_val = 0x10;
		type->type_level_high_val = 0x40;
		type->type_level_low_val = 0x50;
		type->types_supported = (IRQ_TYPE_EDGE_BOTH |
				IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW);
	}

	ret = devm_regmap_add_irq_chip(&i2c->dev, bd70528->chip.regmap,
				       i2c->irq, IRQF_ONESHOT, 0,
				       &bd70528_irq_chip, &irq_data);
	if (ret) {
		dev_err(&i2c->dev, "Failed to add IRQ chip\n");
		return ret;
	}
	dev_dbg(&i2c->dev, "Registered %d IRQs for chip\n",
		bd70528_irq_chip.num_irqs);

	/*
	 * BD70528 IRQ controller is not touching the main mask register.
	 * So enable the GPIO block interrupts at main level. We can just leave
	 * them enabled as the IRQ controller should disable IRQs from
	 * sub-registers when IRQ is disabled or freed.
	 */
	ret = regmap_update_bits(bd70528->chip.regmap,
				 BD70528_REG_INT_MAIN_MASK,
				 BD70528_INT_GPIO_MASK, 0);

	ret = devm_mfd_add_devices(&i2c->dev, PLATFORM_DEVID_AUTO,
				   bd70528_mfd_cells,
				   ARRAY_SIZE(bd70528_mfd_cells), NULL, 0,
				   regmap_irq_get_domain(irq_data));
	if (ret)
		dev_err(&i2c->dev, "Failed to create subdevices\n");

	return ret;
}

static const struct of_device_id bd70528_of_match[] = {
	{ .compatible = "rohm,bd70528", },
	{ },
};
MODULE_DEVICE_TABLE(of, bd70528_of_match);

static struct i2c_driver bd70528_drv = {
	.driver = {
		.name = "rohm-bd70528",
		.of_match_table = bd70528_of_match,
	},
	.probe = &bd70528_i2c_probe,
};

module_i2c_driver(bd70528_drv);

MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_DESCRIPTION("ROHM BD70528 Power Management IC driver");
MODULE_LICENSE("GPL");
