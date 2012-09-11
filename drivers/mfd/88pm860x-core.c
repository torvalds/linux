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

static struct resource bk0_resources[] __devinitdata = {
	{2, 2, "duty cycle", IORESOURCE_REG, },
	{3, 3, "always on",  IORESOURCE_REG, },
	{3, 3, "current",    IORESOURCE_REG, },
};
static struct resource bk1_resources[] __devinitdata = {
	{4, 4, "duty cycle", IORESOURCE_REG, },
	{5, 5, "always on",  IORESOURCE_REG, },
	{5, 5, "current",    IORESOURCE_REG, },
};
static struct resource bk2_resources[] __devinitdata = {
	{6, 6, "duty cycle", IORESOURCE_REG, },
	{7, 7, "always on",  IORESOURCE_REG, },
	{5, 5, "current",    IORESOURCE_REG, },
};

static struct resource led0_resources[] __devinitdata = {
	/* RGB1 Red LED */
	{0xd, 0xd, "control", IORESOURCE_REG, },
	{0xc, 0xc, "blink",   IORESOURCE_REG, },
};
static struct resource led1_resources[] __devinitdata = {
	/* RGB1 Green LED */
	{0xe, 0xe, "control", IORESOURCE_REG, },
	{0xc, 0xc, "blink",   IORESOURCE_REG, },
};
static struct resource led2_resources[] __devinitdata = {
	/* RGB1 Blue LED */
	{0xf, 0xf, "control", IORESOURCE_REG, },
	{0xc, 0xc, "blink",   IORESOURCE_REG, },
};
static struct resource led3_resources[] __devinitdata = {
	/* RGB2 Red LED */
	{0x9, 0x9, "control", IORESOURCE_REG, },
	{0x8, 0x8, "blink",   IORESOURCE_REG, },
};
static struct resource led4_resources[] __devinitdata = {
	/* RGB2 Green LED */
	{0xa, 0xa, "control", IORESOURCE_REG, },
	{0x8, 0x8, "blink",   IORESOURCE_REG, },
};
static struct resource led5_resources[] __devinitdata = {
	/* RGB2 Blue LED */
	{0xb, 0xb, "control", IORESOURCE_REG, },
	{0x8, 0x8, "blink",   IORESOURCE_REG, },
};

static struct resource buck1_resources[] __devinitdata = {
	{0x24, 0x24, "buck set", IORESOURCE_REG, },
};
static struct resource buck2_resources[] __devinitdata = {
	{0x25, 0x25, "buck set", IORESOURCE_REG, },
};
static struct resource buck3_resources[] __devinitdata = {
	{0x26, 0x26, "buck set", IORESOURCE_REG, },
};
static struct resource ldo1_resources[] __devinitdata = {
	{0x10, 0x10, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo2_resources[] __devinitdata = {
	{0x11, 0x11, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo3_resources[] __devinitdata = {
	{0x12, 0x12, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo4_resources[] __devinitdata = {
	{0x13, 0x13, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo5_resources[] __devinitdata = {
	{0x14, 0x14, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo6_resources[] __devinitdata = {
	{0x15, 0x15, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo7_resources[] __devinitdata = {
	{0x16, 0x16, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo8_resources[] __devinitdata = {
	{0x17, 0x17, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo9_resources[] __devinitdata = {
	{0x18, 0x18, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo10_resources[] __devinitdata = {
	{0x19, 0x19, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo12_resources[] __devinitdata = {
	{0x1a, 0x1a, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo_vibrator_resources[] __devinitdata = {
	{0x28, 0x28, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo14_resources[] __devinitdata = {
	{0x1b, 0x1b, "ldo set", IORESOURCE_REG, },
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

static struct resource preg_resources[] __devinitdata = {
	{PM8606_ID_PREG,  PM8606_ID_PREG,  "preg",   IORESOURCE_REG,},
};

static struct resource rtc_resources[] __devinitdata = {
	{PM8607_IRQ_RTC, PM8607_IRQ_RTC, "rtc", IORESOURCE_IRQ,},
};

static struct mfd_cell bk_devs[] = {
	{
		.name = "88pm860x-backlight",
		.id = 0,
		.num_resources = ARRAY_SIZE(bk0_resources),
		.resources = bk0_resources,
	}, {
		.name = "88pm860x-backlight",
		.id = 1,
		.num_resources = ARRAY_SIZE(bk1_resources),
		.resources = bk1_resources,
	}, {
		.name = "88pm860x-backlight",
		.id = 2,
		.num_resources = ARRAY_SIZE(bk2_resources),
		.resources = bk2_resources,
	},
};

static struct mfd_cell led_devs[] = {
	{
		.name = "88pm860x-led",
		.id = 0,
		.num_resources = ARRAY_SIZE(led0_resources),
		.resources = led0_resources,
	}, {
		.name = "88pm860x-led",
		.id = 1,
		.num_resources = ARRAY_SIZE(led1_resources),
		.resources = led1_resources,
	}, {
		.name = "88pm860x-led",
		.id = 2,
		.num_resources = ARRAY_SIZE(led2_resources),
		.resources = led2_resources,
	}, {
		.name = "88pm860x-led",
		.id = 3,
		.num_resources = ARRAY_SIZE(led3_resources),
		.resources = led3_resources,
	}, {
		.name = "88pm860x-led",
		.id = 4,
		.num_resources = ARRAY_SIZE(led4_resources),
		.resources = led4_resources,
	}, {
		.name = "88pm860x-led",
		.id = 5,
		.num_resources = ARRAY_SIZE(led5_resources),
		.resources = led5_resources,
	},
};

static struct mfd_cell reg_devs[] = {
	{
		.name = "88pm860x-regulator",
		.id = 0,
		.num_resources = ARRAY_SIZE(buck1_resources),
		.resources = buck1_resources,
	}, {
		.name = "88pm860x-regulator",
		.id = 1,
		.num_resources = ARRAY_SIZE(buck2_resources),
		.resources = buck2_resources,
	}, {
		.name = "88pm860x-regulator",
		.id = 2,
		.num_resources = ARRAY_SIZE(buck3_resources),
		.resources = buck3_resources,
	}, {
		.name = "88pm860x-regulator",
		.id = 3,
		.num_resources = ARRAY_SIZE(ldo1_resources),
		.resources = ldo1_resources,
	}, {
		.name = "88pm860x-regulator",
		.id = 4,
		.num_resources = ARRAY_SIZE(ldo2_resources),
		.resources = ldo2_resources,
	}, {
		.name = "88pm860x-regulator",
		.id = 5,
		.num_resources = ARRAY_SIZE(ldo3_resources),
		.resources = ldo3_resources,
	}, {
		.name = "88pm860x-regulator",
		.id = 6,
		.num_resources = ARRAY_SIZE(ldo4_resources),
		.resources = ldo4_resources,
	}, {
		.name = "88pm860x-regulator",
		.id = 7,
		.num_resources = ARRAY_SIZE(ldo5_resources),
		.resources = ldo5_resources,
	}, {
		.name = "88pm860x-regulator",
		.id = 8,
		.num_resources = ARRAY_SIZE(ldo6_resources),
		.resources = ldo6_resources,
	}, {
		.name = "88pm860x-regulator",
		.id = 9,
		.num_resources = ARRAY_SIZE(ldo7_resources),
		.resources = ldo7_resources,
	}, {
		.name = "88pm860x-regulator",
		.id = 10,
		.num_resources = ARRAY_SIZE(ldo8_resources),
		.resources = ldo8_resources,
	}, {
		.name = "88pm860x-regulator",
		.id = 11,
		.num_resources = ARRAY_SIZE(ldo9_resources),
		.resources = ldo9_resources,
	}, {
		.name = "88pm860x-regulator",
		.id = 12,
		.num_resources = ARRAY_SIZE(ldo10_resources),
		.resources = ldo10_resources,
	}, {
		.name = "88pm860x-regulator",
		.id = 13,
		.num_resources = ARRAY_SIZE(ldo12_resources),
		.resources = ldo12_resources,
	}, {
		.name = "88pm860x-regulator",
		.id = 14,
		.num_resources = ARRAY_SIZE(ldo_vibrator_resources),
		.resources = ldo_vibrator_resources,
	}, {
		.name = "88pm860x-regulator",
		.id = 15,
		.num_resources = ARRAY_SIZE(ldo14_resources),
		.resources = ldo14_resources,
	},
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

static struct regulator_consumer_supply preg_supply[] = {
	REGULATOR_SUPPLY("preg", "charger-manager"),
};

static struct regulator_init_data preg_init_data = {
	.num_consumer_supplies	= ARRAY_SIZE(preg_supply),
	.consumer_supplies	= &preg_supply[0],
};

static struct mfd_cell power_devs[] = {
	{"88pm860x-battery", -1,},
	{"88pm860x-charger", -1,},
	{"88pm860x-preg",    -1,},
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

int pm8606_osc_enable(struct pm860x_chip *chip, unsigned short client)
{
	int ret = -EIO;
	struct i2c_client *i2c = (chip->id == CHIP_PM8606) ?
		chip->client : chip->companion;

	dev_dbg(chip->dev, "%s(B): client=0x%x\n", __func__, client);
	dev_dbg(chip->dev, "%s(B): vote=0x%x status=%d\n",
			__func__, chip->osc_vote,
			chip->osc_status);

	mutex_lock(&chip->osc_lock);
	/* Update voting status */
	chip->osc_vote |= client;
	/* If reference group is off - turn on*/
	if (chip->osc_status != PM8606_REF_GP_OSC_ON) {
		chip->osc_status = PM8606_REF_GP_OSC_UNKNOWN;
		/* Enable Reference group Vsys */
		if (pm860x_set_bits(i2c, PM8606_VSYS,
				PM8606_VSYS_EN, PM8606_VSYS_EN))
			goto out;

		/*Enable Internal Oscillator */
		if (pm860x_set_bits(i2c, PM8606_MISC,
				PM8606_MISC_OSC_EN, PM8606_MISC_OSC_EN))
			goto out;
		/* Update status (only if writes succeed) */
		chip->osc_status = PM8606_REF_GP_OSC_ON;
	}
	mutex_unlock(&chip->osc_lock);

	dev_dbg(chip->dev, "%s(A): vote=0x%x status=%d ret=%d\n",
			__func__, chip->osc_vote,
			chip->osc_status, ret);
	return 0;
out:
	mutex_unlock(&chip->osc_lock);
	return ret;
}
EXPORT_SYMBOL(pm8606_osc_enable);

int pm8606_osc_disable(struct pm860x_chip *chip, unsigned short client)
{
	int ret = -EIO;
	struct i2c_client *i2c = (chip->id == CHIP_PM8606) ?
		chip->client : chip->companion;

	dev_dbg(chip->dev, "%s(B): client=0x%x\n", __func__, client);
	dev_dbg(chip->dev, "%s(B): vote=0x%x status=%d\n",
			__func__, chip->osc_vote,
			chip->osc_status);

	mutex_lock(&chip->osc_lock);
	/*Update voting status */
	chip->osc_vote &= ~(client);
	/* If reference group is off and this is the last client to release
	 * - turn off */
	if ((chip->osc_status != PM8606_REF_GP_OSC_OFF) &&
			(chip->osc_vote == REF_GP_NO_CLIENTS)) {
		chip->osc_status = PM8606_REF_GP_OSC_UNKNOWN;
		/* Disable Reference group Vsys */
		if (pm860x_set_bits(i2c, PM8606_VSYS, PM8606_VSYS_EN, 0))
			goto out;
		/* Disable Internal Oscillator */
		if (pm860x_set_bits(i2c, PM8606_MISC, PM8606_MISC_OSC_EN, 0))
			goto out;
		chip->osc_status = PM8606_REF_GP_OSC_OFF;
	}
	mutex_unlock(&chip->osc_lock);

	dev_dbg(chip->dev, "%s(A): vote=0x%x status=%d ret=%d\n",
			__func__, chip->osc_vote,
			chip->osc_status, ret);
	return 0;
out:
	mutex_unlock(&chip->osc_lock);
	return ret;
}
EXPORT_SYMBOL(pm8606_osc_disable);

static void __devinit device_osc_init(struct i2c_client *i2c)
{
	struct pm860x_chip *chip = i2c_get_clientdata(i2c);

	mutex_init(&chip->osc_lock);
	/* init portofino reference group voting and status */
	/* Disable Reference group Vsys */
	pm860x_set_bits(i2c, PM8606_VSYS, PM8606_VSYS_EN, 0);
	/* Disable Internal Oscillator */
	pm860x_set_bits(i2c, PM8606_MISC, PM8606_MISC_OSC_EN, 0);

	chip->osc_vote = REF_GP_NO_CLIENTS;
	chip->osc_status = PM8606_REF_GP_OSC_OFF;
}

static void __devinit device_bk_init(struct pm860x_chip *chip,
				     struct pm860x_platform_data *pdata)
{
	int ret, i;

	if (pdata && pdata->backlight) {
		if (pdata->num_backlights > ARRAY_SIZE(bk_devs))
			pdata->num_backlights = ARRAY_SIZE(bk_devs);
		for (i = 0; i < pdata->num_backlights; i++) {
			bk_devs[i].platform_data = &pdata->backlight[i];
			bk_devs[i].pdata_size =
				sizeof(struct pm860x_backlight_pdata);
		}
	}
	ret = mfd_add_devices(chip->dev, 0, bk_devs,
			      ARRAY_SIZE(bk_devs), NULL, 0, NULL);
	if (ret < 0)
		dev_err(chip->dev, "Failed to add backlight subdev\n");
}

static void __devinit device_led_init(struct pm860x_chip *chip,
				      struct pm860x_platform_data *pdata)
{
	int ret, i;

	if (pdata && pdata->led) {
		if (pdata->num_leds > ARRAY_SIZE(led_devs))
			pdata->num_leds = ARRAY_SIZE(led_devs);
		for (i = 0; i < pdata->num_leds; i++) {
			led_devs[i].platform_data = &pdata->led[i];
			led_devs[i].pdata_size =
				sizeof(struct pm860x_led_pdata);
		}
	}
	ret = mfd_add_devices(chip->dev, 0, led_devs,
			      ARRAY_SIZE(led_devs), NULL, 0, NULL);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to add led subdev\n");
		return;
	}
}

static void __devinit device_regulator_init(struct pm860x_chip *chip,
					    struct pm860x_platform_data *pdata)
{
	int ret;

	if (pdata == NULL)
		return;
	if (pdata->buck1) {
		reg_devs[0].platform_data = pdata->buck1;
		reg_devs[0].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->buck2) {
		reg_devs[1].platform_data = pdata->buck2;
		reg_devs[1].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->buck3) {
		reg_devs[2].platform_data = pdata->buck3;
		reg_devs[2].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo1) {
		reg_devs[3].platform_data = pdata->ldo1;
		reg_devs[3].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo2) {
		reg_devs[4].platform_data = pdata->ldo2;
		reg_devs[4].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo3) {
		reg_devs[5].platform_data = pdata->ldo3;
		reg_devs[5].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo4) {
		reg_devs[6].platform_data = pdata->ldo4;
		reg_devs[6].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo5) {
		reg_devs[7].platform_data = pdata->ldo5;
		reg_devs[7].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo6) {
		reg_devs[8].platform_data = pdata->ldo6;
		reg_devs[8].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo7) {
		reg_devs[9].platform_data = pdata->ldo7;
		reg_devs[9].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo8) {
		reg_devs[10].platform_data = pdata->ldo8;
		reg_devs[10].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo9) {
		reg_devs[11].platform_data = pdata->ldo9;
		reg_devs[11].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo10) {
		reg_devs[12].platform_data = pdata->ldo10;
		reg_devs[12].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo12) {
		reg_devs[13].platform_data = pdata->ldo12;
		reg_devs[13].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo_vibrator) {
		reg_devs[14].platform_data = pdata->ldo_vibrator;
		reg_devs[14].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo14) {
		reg_devs[15].platform_data = pdata->ldo14;
		reg_devs[15].pdata_size = sizeof(struct regulator_init_data);
	}
	ret = mfd_add_devices(chip->dev, 0, reg_devs,
			      ARRAY_SIZE(reg_devs), NULL, 0, NULL);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to add regulator subdev\n");
		return;
	}
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
			      chip->irq_base, NULL);
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
			      chip->irq_base, NULL);
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
			      &battery_resources[0], chip->irq_base, NULL);
	if (ret < 0)
		dev_err(chip->dev, "Failed to add battery subdev\n");

	power_devs[1].platform_data = pdata->power;
	power_devs[1].pdata_size = sizeof(struct pm860x_power_pdata);
	power_devs[1].num_resources = ARRAY_SIZE(charger_resources);
	power_devs[1].resources = &charger_resources[0],
	ret = mfd_add_devices(chip->dev, 0, &power_devs[1], 1,
			      &charger_resources[0], chip->irq_base, NULL);
	if (ret < 0)
		dev_err(chip->dev, "Failed to add charger subdev\n");

	power_devs[2].platform_data = &preg_init_data;
	power_devs[2].pdata_size = sizeof(struct regulator_init_data);
	power_devs[2].num_resources = ARRAY_SIZE(preg_resources);
	power_devs[2].resources = &preg_resources[0],
	ret = mfd_add_devices(chip->dev, 0, &power_devs[2], 1,
			      &preg_resources[0], chip->irq_base, NULL);
	if (ret < 0)
		dev_err(chip->dev, "Failed to add preg subdev\n");
}

static void __devinit device_onkey_init(struct pm860x_chip *chip,
					struct pm860x_platform_data *pdata)
{
	int ret;

	onkey_devs[0].num_resources = ARRAY_SIZE(onkey_resources);
	onkey_devs[0].resources = &onkey_resources[0],
	ret = mfd_add_devices(chip->dev, 0, &onkey_devs[0],
			      ARRAY_SIZE(onkey_devs), &onkey_resources[0],
			      chip->irq_base, NULL);
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
			      ARRAY_SIZE(codec_devs), &codec_resources[0], 0,
			      NULL);
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

static void __devinit device_8606_init(struct pm860x_chip *chip,
				       struct i2c_client *i2c,
				       struct pm860x_platform_data *pdata)
{
	device_osc_init(i2c);
	device_bk_init(chip, pdata);
	device_led_init(chip, pdata);
}

int __devinit pm860x_device_init(struct pm860x_chip *chip,
		       struct pm860x_platform_data *pdata)
{
	chip->core_irq = 0;

	switch (chip->id) {
	case CHIP_PM8606:
		device_8606_init(chip, chip->client, pdata);
		break;
	case CHIP_PM8607:
		device_8607_init(chip, chip->client, pdata);
		break;
	}

	if (chip->companion) {
		switch (chip->id) {
		case CHIP_PM8607:
			device_8606_init(chip, chip->companion, pdata);
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
