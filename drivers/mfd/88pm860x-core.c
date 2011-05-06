/*
 * Base driver for Marvell 88PM8607
 *
 * Copyright (C) 2009 Marvell International Ltd.
 * 	Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/88pm860x.h>
#include <linux/regulator/machine.h>

#define INT_STATUS_NUM			3

static struct resource bk_resources[] __devinitdata = {
	{PM8606_BACKLIGHT1, PM8606_BACKLIGHT1, "backlight-0", IORESOURCE_IO,},
	{PM8606_BACKLIGHT2, PM8606_BACKLIGHT2, "backlight-1", IORESOURCE_IO,},
	{PM8606_BACKLIGHT3, PM8606_BACKLIGHT3, "backlight-2", IORESOURCE_IO,},
};

static struct resource led_resources[] __devinitdata = {
	{PM8606_LED1_RED,   PM8606_LED1_RED,   "led0-red",   IORESOURCE_IO,},
	{PM8606_LED1_GREEN, PM8606_LED1_GREEN, "led0-green", IORESOURCE_IO,},
	{PM8606_LED1_BLUE,  PM8606_LED1_BLUE,  "led0-blue",  IORESOURCE_IO,},
	{PM8606_LED2_RED,   PM8606_LED2_RED,   "led1-red",   IORESOURCE_IO,},
	{PM8606_LED2_GREEN, PM8606_LED2_GREEN, "led1-green", IORESOURCE_IO,},
	{PM8606_LED2_BLUE,  PM8606_LED2_BLUE,  "led1-blue",  IORESOURCE_IO,},
};

static struct resource regulator_resources[] __devinitdata = {
	{PM8607_ID_BUCK1, PM8607_ID_BUCK1, "buck-1", IORESOURCE_IO,},
	{PM8607_ID_BUCK2, PM8607_ID_BUCK2, "buck-2", IORESOURCE_IO,},
	{PM8607_ID_BUCK3, PM8607_ID_BUCK3, "buck-3", IORESOURCE_IO,},
	{PM8607_ID_LDO1,  PM8607_ID_LDO1,  "ldo-01", IORESOURCE_IO,},
	{PM8607_ID_LDO2,  PM8607_ID_LDO2,  "ldo-02", IORESOURCE_IO,},
	{PM8607_ID_LDO3,  PM8607_ID_LDO3,  "ldo-03", IORESOURCE_IO,},
	{PM8607_ID_LDO4,  PM8607_ID_LDO4,  "ldo-04", IORESOURCE_IO,},
	{PM8607_ID_LDO5,  PM8607_ID_LDO5,  "ldo-05", IORESOURCE_IO,},
	{PM8607_ID_LDO6,  PM8607_ID_LDO6,  "ldo-06", IORESOURCE_IO,},
	{PM8607_ID_LDO7,  PM8607_ID_LDO7,  "ldo-07", IORESOURCE_IO,},
	{PM8607_ID_LDO8,  PM8607_ID_LDO8,  "ldo-08", IORESOURCE_IO,},
	{PM8607_ID_LDO9,  PM8607_ID_LDO9,  "ldo-09", IORESOURCE_IO,},
	{PM8607_ID_LDO10, PM8607_ID_LDO10, "ldo-10", IORESOURCE_IO,},
	{PM8607_ID_LDO11, PM8607_ID_LDO11, "ldo-11", IORESOURCE_IO,},
	{PM8607_ID_LDO12, PM8607_ID_LDO12, "ldo-12", IORESOURCE_IO,},
	{PM8607_ID_LDO13, PM8607_ID_LDO13, "ldo-13", IORESOURCE_IO,},
	{PM8607_ID_LDO14, PM8607_ID_LDO14, "ldo-14", IORESOURCE_IO,},
	{PM8607_ID_LDO15, PM8607_ID_LDO15, "ldo-15", IORESOURCE_IO,},
};

static struct resource touch_resources[] __devinitdata = {
	{PM8607_IRQ_PEN, PM8607_IRQ_PEN, "touch", IORESOURCE_IRQ,},
};

static struct resource onkey_resources[] __devinitdata = {
	{PM8607_IRQ_ONKEY, PM8607_IRQ_ONKEY, "onkey", IORESOURCE_IRQ,},
};

static struct resource codec_resources[] __devinitdata = {
	/* Headset microphone insertion or removal */
	{PM8607_IRQ_MICIN,   PM8607_IRQ_MICIN,   "micin",   IORESOURCE_IRQ,},
	/* Hook-switch press or release */
	{PM8607_IRQ_HOOK,    PM8607_IRQ_HOOK,    "hook",    IORESOURCE_IRQ,},
	/* Headset insertion or removal */
	{PM8607_IRQ_HEADSET, PM8607_IRQ_HEADSET, "headset", IORESOURCE_IRQ,},
	/* Audio short */
	{PM8607_IRQ_AUDIO_SHORT, PM8607_IRQ_AUDIO_SHORT, "audio-short", IORESOURCE_IRQ,},
};

static struct resource battery_resources[] __devinitdata = {
	{PM8607_IRQ_CC,  PM8607_IRQ_CC,  "columb counter", IORESOURCE_IRQ,},
	{PM8607_IRQ_BAT, PM8607_IRQ_BAT, "battery",        IORESOURCE_IRQ,},
};

static struct resource charger_resources[] __devinitdata = {
	{PM8607_IRQ_CHG,  PM8607_IRQ_CHG,  "charger detect",  IORESOURCE_IRQ,},
	{PM8607_IRQ_CHG_DONE,  PM8607_IRQ_CHG_DONE,  "charging done",       IORESOURCE_IRQ,},
	{PM8607_IRQ_CHG_FAULT, PM8607_IRQ_CHG_FAULT, "charging timeout",    IORESOURCE_IRQ,},
	{PM8607_IRQ_GPADC1,    PM8607_IRQ_GPADC1,    "battery temperature", IORESOURCE_IRQ,},
	{PM8607_IRQ_VBAT, PM8607_IRQ_VBAT, "battery voltage", IORESOURCE_IRQ,},
	{PM8607_IRQ_VCHG, PM8607_IRQ_VCHG, "vchg voltage",    IORESOURCE_IRQ,},
};

static struct resource rtc_resources[] __devinitdata = {
	{PM8607_IRQ_RTC, PM8607_IRQ_RTC, "rtc", IORESOURCE_IRQ,},
};

static struct mfd_cell bk_devs[] = {
	{"88pm860x-backlight", 0,},
	{"88pm860x-backlight", 1,},
	{"88pm860x-backlight", 2,},
};

static struct mfd_cell led_devs[] = {
	{"88pm860x-led", 0,},
	{"88pm860x-led", 1,},
	{"88pm860x-led", 2,},
	{"88pm860x-led", 3,},
	{"88pm860x-led", 4,},
	{"88pm860x-led", 5,},
};

static struct mfd_cell regulator_devs[] = {
	{"88pm860x-regulator", 0,},
	{"88pm860x-regulator", 1,},
	{"88pm860x-regulator", 2,},
	{"88pm860x-regulator", 3,},
	{"88pm860x-regulator", 4,},
	{"88pm860x-regulator", 5,},
	{"88pm860x-regulator", 6,},
	{"88pm860x-regulator", 7,},
	{"88pm860x-regulator", 8,},
	{"88pm860x-regulator", 9,},
	{"88pm860x-regulator", 10,},
	{"88pm860x-regulator", 11,},
	{"88pm860x-regulator", 12,},
	{"88pm860x-regulator", 13,},
	{"88pm860x-regulator", 14,},
	{"88pm860x-regulator", 15,},
	{"88pm860x-regulator", 16,},
	{"88pm860x-regulator", 17,},
};

static struct mfd_cell touch_devs[] = {
	{"88pm860x-touch", -1,},
};

static struct mfd_cell onkey_devs[] = {
	{"88pm860x-onkey", -1,},
};

static struct mfd_cell codec_devs[] = {
	{"88pm860x-codec", -1,},
};

static struct mfd_cell power_devs[] = {
	{"88pm860x-battery", -1,},
	{"88pm860x-charger", -1,},
};

static struct mfd_cell rtc_devs[] = {
	{"88pm860x-rtc", -1,},
};


struct pm860x_irq_data {
	int	reg;
	int	mask_reg;
	int	enable;		/* enable or not */
	int	offs;		/* bit offset in mask register */
};

static struct pm860x_irq_data pm860x_irqs[] = {
	[PM8607_IRQ_ONKEY] = {
		.reg		= PM8607_INT_STATUS1,
		.mask_reg	= PM8607_INT_MASK_1,
		.offs		= 1 << 0,
	},
	[PM8607_IRQ_EXTON] = {
		.reg		= PM8607_INT_STATUS1,
		.mask_reg	= PM8607_INT_MASK_1,
		.offs		= 1 << 1,
	},
	[PM8607_IRQ_CHG] = {
		.reg		= PM8607_INT_STATUS1,
		.mask_reg	= PM8607_INT_MASK_1,
		.offs		= 1 << 2,
	},
	[PM8607_IRQ_BAT] = {
		.reg		= PM8607_INT_STATUS1,
		.mask_reg	= PM8607_INT_MASK_1,
		.offs		= 1 << 3,
	},
	[PM8607_IRQ_RTC] = {
		.reg		= PM8607_INT_STATUS1,
		.mask_reg	= PM8607_INT_MASK_1,
		.offs		= 1 << 4,
	},
	[PM8607_IRQ_CC] = {
		.reg		= PM8607_INT_STATUS1,
		.mask_reg	= PM8607_INT_MASK_1,
		.offs		= 1 << 5,
	},
	[PM8607_IRQ_VBAT] = {
		.reg		= PM8607_INT_STATUS2,
		.mask_reg	= PM8607_INT_MASK_2,
		.offs		= 1 << 0,
	},
	[PM8607_IRQ_VCHG] = {
		.reg		= PM8607_INT_STATUS2,
		.mask_reg	= PM8607_INT_MASK_2,
		.offs		= 1 << 1,
	},
	[PM8607_IRQ_VSYS] = {
		.reg		= PM8607_INT_STATUS2,
		.mask_reg	= PM8607_INT_MASK_2,
		.offs		= 1 << 2,
	},
	[PM8607_IRQ_TINT] = {
		.reg		= PM8607_INT_STATUS2,
		.mask_reg	= PM8607_INT_MASK_2,
		.offs		= 1 << 3,
	},
	[PM8607_IRQ_GPADC0] = {
		.reg		= PM8607_INT_STATUS2,
		.mask_reg	= PM8607_INT_MASK_2,
		.offs		= 1 << 4,
	},
	[PM8607_IRQ_GPADC1] = {
		.reg		= PM8607_INT_STATUS2,
		.mask_reg	= PM8607_INT_MASK_2,
		.offs		= 1 << 5,
	},
	[PM8607_IRQ_GPADC2] = {
		.reg		= PM8607_INT_STATUS2,
		.mask_reg	= PM8607_INT_MASK_2,
		.offs		= 1 << 6,
	},
	[PM8607_IRQ_GPADC3] = {
		.reg		= PM8607_INT_STATUS2,
		.mask_reg	= PM8607_INT_MASK_2,
		.offs		= 1 << 7,
	},
	[PM8607_IRQ_AUDIO_SHORT] = {
		.reg		= PM8607_INT_STATUS3,
		.mask_reg	= PM8607_INT_MASK_3,
		.offs		= 1 << 0,
	},
	[PM8607_IRQ_PEN] = {
		.reg		= PM8607_INT_STATUS3,
		.mask_reg	= PM8607_INT_MASK_3,
		.offs		= 1 << 1,
	},
	[PM8607_IRQ_HEADSET] = {
		.reg		= PM8607_INT_STATUS3,
		.mask_reg	= PM8607_INT_MASK_3,
		.offs		= 1 << 2,
	},
	[PM8607_IRQ_HOOK] = {
		.reg		= PM8607_INT_STATUS3,
		.mask_reg	= PM8607_INT_MASK_3,
		.offs		= 1 << 3,
	},
	[PM8607_IRQ_MICIN] = {
		.reg		= PM8607_INT_STATUS3,
		.mask_reg	= PM8607_INT_MASK_3,
		.offs		= 1 << 4,
	},
	[PM8607_IRQ_CHG_FAIL] = {
		.reg		= PM8607_INT_STATUS3,
		.mask_reg	= PM8607_INT_MASK_3,
		.offs		= 1 << 5,
	},
	[PM8607_IRQ_CHG_DONE] = {
		.reg		= PM8607_INT_STATUS3,
		.mask_reg	= PM8607_INT_MASK_3,
		.offs		= 1 << 6,
	},
	[PM8607_IRQ_CHG_FAULT] = {
		.reg		= PM8607_INT_STATUS3,
		.mask_reg	= PM8607_INT_MASK_3,
		.offs		= 1 << 7,
	},
};

static irqreturn_t pm860x_irq(int irq, void *data)
{
	struct pm860x_chip *chip = data;
	struct pm860x_irq_data *irq_data;
	struct i2c_client *i2c;
	int read_reg = -1, value = 0;
	int i;

	i2c = (chip->id == CHIP_PM8607) ? chip->client : chip->companion;
	for (i = 0; i < ARRAY_SIZE(pm860x_irqs); i++) {
		irq_data = &pm860x_irqs[i];
		if (read_reg != irq_data->reg) {
			read_reg = irq_data->reg;
			value = pm860x_reg_read(i2c, irq_data->reg);
		}
		if (value & irq_data->enable)
			handle_nested_irq(chip->irq_base + i);
	}
	return IRQ_HANDLED;
}

static void pm860x_irq_lock(struct irq_data *data)
{
	struct pm860x_chip *chip = irq_data_get_irq_chip_data(data);

	mutex_lock(&chip->irq_lock);
}

static void pm860x_irq_sync_unlock(struct irq_data *data)
{
	struct pm860x_chip *chip = irq_data_get_irq_chip_data(data);
	struct pm860x_irq_data *irq_data;
	struct i2c_client *i2c;
	static unsigned char cached[3] = {0x0, 0x0, 0x0};
	unsigned char mask[3];
	int i;

	i2c = (chip->id == CHIP_PM8607) ? chip->client : chip->companion;
	/* Load cached value. In initial, all IRQs are masked */
	for (i = 0; i < 3; i++)
		mask[i] = cached[i];
	for (i = 0; i < ARRAY_SIZE(pm860x_irqs); i++) {
		irq_data = &pm860x_irqs[i];
		switch (irq_data->mask_reg) {
		case PM8607_INT_MASK_1:
			mask[0] &= ~irq_data->offs;
			mask[0] |= irq_data->enable;
			break;
		case PM8607_INT_MASK_2:
			mask[1] &= ~irq_data->offs;
			mask[1] |= irq_data->enable;
			break;
		case PM8607_INT_MASK_3:
			mask[2] &= ~irq_data->offs;
			mask[2] |= irq_data->enable;
			break;
		default:
			dev_err(chip->dev, "wrong IRQ\n");
			break;
		}
	}
	/* update mask into registers */
	for (i = 0; i < 3; i++) {
		if (mask[i] != cached[i]) {
			cached[i] = mask[i];
			pm860x_reg_write(i2c, PM8607_INT_MASK_1 + i, mask[i]);
		}
	}

	mutex_unlock(&chip->irq_lock);
}

static void pm860x_irq_enable(struct irq_data *data)
{
	struct pm860x_chip *chip = irq_data_get_irq_chip_data(data);
	pm860x_irqs[data->irq - chip->irq_base].enable
		= pm860x_irqs[data->irq - chip->irq_base].offs;
}

static void pm860x_irq_disable(struct irq_data *data)
{
	struct pm860x_chip *chip = irq_data_get_irq_chip_data(data);
	pm860x_irqs[data->irq - chip->irq_base].enable = 0;
}

static struct irq_chip pm860x_irq_chip = {
	.name		= "88pm860x",
	.irq_bus_lock	= pm860x_irq_lock,
	.irq_bus_sync_unlock = pm860x_irq_sync_unlock,
	.irq_enable	= pm860x_irq_enable,
	.irq_disable	= pm860x_irq_disable,
};

static int __devinit device_gpadc_init(struct pm860x_chip *chip,
				       struct pm860x_platform_data *pdata)
{
	struct i2c_client *i2c = (chip->id == CHIP_PM8607) ? chip->client \
				: chip->companion;
	int data;
	int ret;

	/* initialize GPADC without activating it */

	if (!pdata || !pdata->touch)
		return -EINVAL;

	/* set GPADC MISC1 register */
	data = 0;
	data |= (pdata->touch->gpadc_prebias << 1) & PM8607_GPADC_PREBIAS_MASK;
	data |= (pdata->touch->slot_cycle << 3) & PM8607_GPADC_SLOT_CYCLE_MASK;
	data |= (pdata->touch->off_scale << 5) & PM8607_GPADC_OFF_SCALE_MASK;
	data |= (pdata->touch->sw_cal << 7) & PM8607_GPADC_SW_CAL_MASK;
	if (data) {
		ret = pm860x_reg_write(i2c, PM8607_GPADC_MISC1, data);
		if (ret < 0)
			goto out;
	}
	/* set tsi prebias time */
	if (pdata->touch->tsi_prebias) {
		data = pdata->touch->tsi_prebias;
		ret = pm860x_reg_write(i2c, PM8607_TSI_PREBIAS, data);
		if (ret < 0)
			goto out;
	}
	/* set prebias & prechg time of pen detect */
	data = 0;
	data |= pdata->touch->pen_prebias & PM8607_PD_PREBIAS_MASK;
	data |= (pdata->touch->pen_prechg << 5) & PM8607_PD_PRECHG_MASK;
	if (data) {
		ret = pm860x_reg_write(i2c, PM8607_PD_PREBIAS, data);
		if (ret < 0)
			goto out;
	}

	ret = pm860x_set_bits(i2c, PM8607_GPADC_MISC1,
			      PM8607_GPADC_EN, PM8607_GPADC_EN);
out:
	return ret;
}

static int __devinit device_irq_init(struct pm860x_chip *chip,
				     struct pm860x_platform_data *pdata)
{
	struct i2c_client *i2c = (chip->id == CHIP_PM8607) ? chip->client \
				: chip->companion;
	unsigned char status_buf[INT_STATUS_NUM];
	unsigned long flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;
	int i, data, mask, ret = -EINVAL;
	int __irq;

	if (!pdata || !pdata->irq_base) {
		dev_warn(chip->dev, "No interrupt support on IRQ base\n");
		return -EINVAL;
	}

	mask = PM8607_B0_MISC1_INV_INT | PM8607_B0_MISC1_INT_CLEAR
		| PM8607_B0_MISC1_INT_MASK;
	data = 0;
	chip->irq_mode = 0;
	if (pdata && pdata->irq_mode) {
		/*
		 * irq_mode defines the way of clearing interrupt. If it's 1,
		 * clear IRQ by write. Otherwise, clear it by read.
		 * This control bit is valid from 88PM8607 B0 steping.
		 */
		data |= PM8607_B0_MISC1_INT_CLEAR;
		chip->irq_mode = 1;
	}
	ret = pm860x_set_bits(i2c, PM8607_B0_MISC1, mask, data);
	if (ret < 0)
		goto out;

	/* mask all IRQs */
	memset(status_buf, 0, INT_STATUS_NUM);
	ret = pm860x_bulk_write(i2c, PM8607_INT_MASK_1,
				INT_STATUS_NUM, status_buf);
	if (ret < 0)
		goto out;

	if (chip->irq_mode) {
		/* clear interrupt status by write */
		memset(status_buf, 0xFF, INT_STATUS_NUM);
		ret = pm860x_bulk_write(i2c, PM8607_INT_STATUS1,
					INT_STATUS_NUM, status_buf);
	} else {
		/* clear interrupt status by read */
		ret = pm860x_bulk_read(i2c, PM8607_INT_STATUS1,
					INT_STATUS_NUM, status_buf);
	}
	if (ret < 0)
		goto out;

	mutex_init(&chip->irq_lock);
	chip->irq_base = pdata->irq_base;
	chip->core_irq = i2c->irq;
	if (!chip->core_irq)
		goto out;

	/* register IRQ by genirq */
	for (i = 0; i < ARRAY_SIZE(pm860x_irqs); i++) {
		__irq = i + chip->irq_base;
		irq_set_chip_data(__irq, chip);
		irq_set_chip_and_handler(__irq, &pm860x_irq_chip,
					 handle_edge_irq);
		irq_set_nested_thread(__irq, 1);
#ifdef CONFIG_ARM
		set_irq_flags(__irq, IRQF_VALID);
#else
		irq_set_noprobe(__irq);
#endif
	}

	ret = request_threaded_irq(chip->core_irq, NULL, pm860x_irq, flags,
				   "88pm860x", chip);
	if (ret) {
		dev_err(chip->dev, "Failed to request IRQ: %d\n", ret);
		chip->core_irq = 0;
	}

	return 0;
out:
	chip->core_irq = 0;
	return ret;
}

static void device_irq_exit(struct pm860x_chip *chip)
{
	if (chip->core_irq)
		free_irq(chip->core_irq, chip);
}

static void __devinit device_bk_init(struct pm860x_chip *chip,
				     struct pm860x_platform_data *pdata)
{
	int ret;
	int i, j, id;

	if ((pdata == NULL) || (pdata->backlight == NULL))
		return;

	if (pdata->num_backlights > ARRAY_SIZE(bk_devs))
		pdata->num_backlights = ARRAY_SIZE(bk_devs);

	for (i = 0; i < pdata->num_backlights; i++) {
		bk_devs[i].platform_data = &pdata->backlight[i];
		bk_devs[i].pdata_size = sizeof(struct pm860x_backlight_pdata);

		for (j = 0; j < ARRAY_SIZE(bk_devs); j++) {
			id = bk_resources[j].start;
			if (pdata->backlight[i].flags != id)
				continue;

			bk_devs[i].num_resources = 1;
			bk_devs[i].resources = &bk_resources[j];
			ret = mfd_add_devices(chip->dev, 0,
					      &bk_devs[i], 1,
					      &bk_resources[j], 0);
			if (ret < 0) {
				dev_err(chip->dev, "Failed to add "
					"backlight subdev\n");
				return;
			}
		}
	}
}

static void __devinit device_led_init(struct pm860x_chip *chip,
				      struct pm860x_platform_data *pdata)
{
	int ret;
	int i, j, id;

	if ((pdata == NULL) || (pdata->led == NULL))
		return;

	if (pdata->num_leds > ARRAY_SIZE(led_devs))
		pdata->num_leds = ARRAY_SIZE(led_devs);

	for (i = 0; i < pdata->num_leds; i++) {
		led_devs[i].platform_data = &pdata->led[i];
		led_devs[i].pdata_size = sizeof(struct pm860x_led_pdata);

		for (j = 0; j < ARRAY_SIZE(led_devs); j++) {
			id = led_resources[j].start;
			if (pdata->led[i].flags != id)
				continue;

			led_devs[i].num_resources = 1;
			led_devs[i].resources = &led_resources[j],
			ret = mfd_add_devices(chip->dev, 0,
					      &led_devs[i], 1,
					      &led_resources[j], 0);
			if (ret < 0) {
				dev_err(chip->dev, "Failed to add "
					"led subdev\n");
				return;
			}
		}
	}
}

static void __devinit device_regulator_init(struct pm860x_chip *chip,
					    struct pm860x_platform_data *pdata)
{
	struct regulator_init_data *initdata;
	int ret;
	int i, seq;

	if ((pdata == NULL) || (pdata->regulator == NULL))
		return;

	if (pdata->num_regulators > ARRAY_SIZE(regulator_devs))
		pdata->num_regulators = ARRAY_SIZE(regulator_devs);

	for (i = 0, seq = -1; i < pdata->num_regulators; i++) {
		initdata = &pdata->regulator[i];
		seq = *(unsigned int *)initdata->driver_data;
		if ((seq < 0) || (seq > PM8607_ID_RG_MAX)) {
			dev_err(chip->dev, "Wrong ID(%d) on regulator(%s)\n",
				seq, initdata->constraints.name);
			goto out;
		}
		regulator_devs[i].platform_data = &pdata->regulator[i];
		regulator_devs[i].pdata_size = sizeof(struct regulator_init_data);
		regulator_devs[i].num_resources = 1;
		regulator_devs[i].resources = &regulator_resources[seq];

		ret = mfd_add_devices(chip->dev, 0, &regulator_devs[i], 1,
				      &regulator_resources[seq], 0);
		if (ret < 0) {
			dev_err(chip->dev, "Failed to add regulator subdev\n");
			goto out;
		}
	}
out:
	return;
}

static void __devinit device_rtc_init(struct pm860x_chip *chip,
				      struct pm860x_platform_data *pdata)
{
	int ret;

	if ((pdata == NULL))
		return;

	rtc_devs[0].platform_data = pdata->rtc;
	rtc_devs[0].pdata_size = sizeof(struct pm860x_rtc_pdata);
	rtc_devs[0].num_resources = ARRAY_SIZE(rtc_resources);
	rtc_devs[0].resources = &rtc_resources[0];
	ret = mfd_add_devices(chip->dev, 0, &rtc_devs[0],
			      ARRAY_SIZE(rtc_devs), &rtc_resources[0],
			      chip->irq_base);
	if (ret < 0)
		dev_err(chip->dev, "Failed to add rtc subdev\n");
}

static void __devinit device_touch_init(struct pm860x_chip *chip,
					struct pm860x_platform_data *pdata)
{
	int ret;

	if (pdata == NULL)
		return;

	touch_devs[0].platform_data = pdata->touch;
	touch_devs[0].pdata_size = sizeof(struct pm860x_touch_pdata);
	touch_devs[0].num_resources = ARRAY_SIZE(touch_resources);
	touch_devs[0].resources = &touch_resources[0];
	ret = mfd_add_devices(chip->dev, 0, &touch_devs[0],
			      ARRAY_SIZE(touch_devs), &touch_resources[0],
			      chip->irq_base);
	if (ret < 0)
		dev_err(chip->dev, "Failed to add touch subdev\n");
}

static void __devinit device_power_init(struct pm860x_chip *chip,
					struct pm860x_platform_data *pdata)
{
	int ret;

	if (pdata == NULL)
		return;

	power_devs[0].platform_data = pdata->power;
	power_devs[0].pdata_size = sizeof(struct pm860x_power_pdata);
	power_devs[0].num_resources = ARRAY_SIZE(battery_resources);
	power_devs[0].resources = &battery_resources[0],
	ret = mfd_add_devices(chip->dev, 0, &power_devs[0], 1,
			      &battery_resources[0], chip->irq_base);
	if (ret < 0)
		dev_err(chip->dev, "Failed to add battery subdev\n");

	power_devs[1].platform_data = pdata->power;
	power_devs[1].pdata_size = sizeof(struct pm860x_power_pdata);
	power_devs[1].num_resources = ARRAY_SIZE(charger_resources);
	power_devs[1].resources = &charger_resources[0],
	ret = mfd_add_devices(chip->dev, 0, &power_devs[1], 1,
			      &charger_resources[0], chip->irq_base);
	if (ret < 0)
		dev_err(chip->dev, "Failed to add charger subdev\n");
}

static void __devinit device_onkey_init(struct pm860x_chip *chip,
					struct pm860x_platform_data *pdata)
{
	int ret;

	onkey_devs[0].num_resources = ARRAY_SIZE(onkey_resources);
	onkey_devs[0].resources = &onkey_resources[0],
	ret = mfd_add_devices(chip->dev, 0, &onkey_devs[0],
			      ARRAY_SIZE(onkey_devs), &onkey_resources[0],
			      chip->irq_base);
	if (ret < 0)
		dev_err(chip->dev, "Failed to add onkey subdev\n");
}

static void __devinit device_codec_init(struct pm860x_chip *chip,
					struct pm860x_platform_data *pdata)
{
	int ret;

	codec_devs[0].num_resources = ARRAY_SIZE(codec_resources);
	codec_devs[0].resources = &codec_resources[0],
	ret = mfd_add_devices(chip->dev, 0, &codec_devs[0],
			      ARRAY_SIZE(codec_devs), &codec_resources[0], 0);
	if (ret < 0)
		dev_err(chip->dev, "Failed to add codec subdev\n");
}

static void __devinit device_8607_init(struct pm860x_chip *chip,
				       struct i2c_client *i2c,
				       struct pm860x_platform_data *pdata)
{
	int data, ret;

	ret = pm860x_reg_read(i2c, PM8607_CHIP_ID);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read CHIP ID: %d\n", ret);
		goto out;
	}
	switch (ret & PM8607_VERSION_MASK) {
	case 0x40:
	case 0x50:
		dev_info(chip->dev, "Marvell 88PM8607 (ID: %02x) detected\n",
			 ret);
		break;
	default:
		dev_err(chip->dev, "Failed to detect Marvell 88PM8607. "
			"Chip ID: %02x\n", ret);
		goto out;
	}

	ret = pm860x_reg_read(i2c, PM8607_BUCK3);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read BUCK3 register: %d\n", ret);
		goto out;
	}
	if (ret & PM8607_BUCK3_DOUBLE)
		chip->buck3_double = 1;

	ret = pm860x_reg_read(i2c, PM8607_B0_MISC1);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read MISC1 register: %d\n", ret);
		goto out;
	}

	if (pdata && (pdata->i2c_port == PI2C_PORT))
		data = PM8607_B0_MISC1_PI2C;
	else
		data = 0;
	ret = pm860x_set_bits(i2c, PM8607_B0_MISC1, PM8607_B0_MISC1_PI2C, data);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to access MISC1:%d\n", ret);
		goto out;
	}

	ret = device_gpadc_init(chip, pdata);
	if (ret < 0)
		goto out;

	ret = device_irq_init(chip, pdata);
	if (ret < 0)
		goto out;

	device_regulator_init(chip, pdata);
	device_rtc_init(chip, pdata);
	device_onkey_init(chip, pdata);
	device_touch_init(chip, pdata);
	device_power_init(chip, pdata);
	device_codec_init(chip, pdata);
out:
	return;
}

int __devinit pm860x_device_init(struct pm860x_chip *chip,
		       struct pm860x_platform_data *pdata)
{
	chip->core_irq = 0;

	switch (chip->id) {
	case CHIP_PM8606:
		device_bk_init(chip, pdata);
		device_led_init(chip, pdata);
		break;
	case CHIP_PM8607:
		device_8607_init(chip, chip->client, pdata);
		break;
	}

	if (chip->companion) {
		switch (chip->id) {
		case CHIP_PM8607:
			device_bk_init(chip, pdata);
			device_led_init(chip, pdata);
			break;
		case CHIP_PM8606:
			device_8607_init(chip, chip->companion, pdata);
			break;
		}
	}

	return 0;
}

void __devexit pm860x_device_exit(struct pm860x_chip *chip)
{
	device_irq_exit(chip);
	mfd_remove_devices(chip->dev);
}

MODULE_DESCRIPTION("PMIC Driver for Marvell 88PM860x");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_LICENSE("GPL");
