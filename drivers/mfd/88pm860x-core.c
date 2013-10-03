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
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/mfd/core.h>
#include <linux/mfd/88pm860x.h>
#include <linux/regulator/machine.h>
#include <linux/power/charger-manager.h>

#define INT_STATUS_NUM			3

static struct resource bk0_resources[] = {
	{2, 2, "duty cycle", IORESOURCE_REG, },
	{3, 3, "always on",  IORESOURCE_REG, },
	{3, 3, "current",    IORESOURCE_REG, },
};
static struct resource bk1_resources[] = {
	{4, 4, "duty cycle", IORESOURCE_REG, },
	{5, 5, "always on",  IORESOURCE_REG, },
	{5, 5, "current",    IORESOURCE_REG, },
};
static struct resource bk2_resources[] = {
	{6, 6, "duty cycle", IORESOURCE_REG, },
	{7, 7, "always on",  IORESOURCE_REG, },
	{5, 5, "current",    IORESOURCE_REG, },
};

static struct resource led0_resources[] = {
	/* RGB1 Red LED */
	{0xd, 0xd, "control", IORESOURCE_REG, },
	{0xc, 0xc, "blink",   IORESOURCE_REG, },
};
static struct resource led1_resources[] = {
	/* RGB1 Green LED */
	{0xe, 0xe, "control", IORESOURCE_REG, },
	{0xc, 0xc, "blink",   IORESOURCE_REG, },
};
static struct resource led2_resources[] = {
	/* RGB1 Blue LED */
	{0xf, 0xf, "control", IORESOURCE_REG, },
	{0xc, 0xc, "blink",   IORESOURCE_REG, },
};
static struct resource led3_resources[] = {
	/* RGB2 Red LED */
	{0x9, 0x9, "control", IORESOURCE_REG, },
	{0x8, 0x8, "blink",   IORESOURCE_REG, },
};
static struct resource led4_resources[] = {
	/* RGB2 Green LED */
	{0xa, 0xa, "control", IORESOURCE_REG, },
	{0x8, 0x8, "blink",   IORESOURCE_REG, },
};
static struct resource led5_resources[] = {
	/* RGB2 Blue LED */
	{0xb, 0xb, "control", IORESOURCE_REG, },
	{0x8, 0x8, "blink",   IORESOURCE_REG, },
};

static struct resource buck1_resources[] = {
	{0x24, 0x24, "buck set", IORESOURCE_REG, },
};
static struct resource buck2_resources[] = {
	{0x25, 0x25, "buck set", IORESOURCE_REG, },
};
static struct resource buck3_resources[] = {
	{0x26, 0x26, "buck set", IORESOURCE_REG, },
};
static struct resource ldo1_resources[] = {
	{0x10, 0x10, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo2_resources[] = {
	{0x11, 0x11, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo3_resources[] = {
	{0x12, 0x12, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo4_resources[] = {
	{0x13, 0x13, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo5_resources[] = {
	{0x14, 0x14, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo6_resources[] = {
	{0x15, 0x15, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo7_resources[] = {
	{0x16, 0x16, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo8_resources[] = {
	{0x17, 0x17, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo9_resources[] = {
	{0x18, 0x18, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo10_resources[] = {
	{0x19, 0x19, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo12_resources[] = {
	{0x1a, 0x1a, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo_vibrator_resources[] = {
	{0x28, 0x28, "ldo set", IORESOURCE_REG, },
};
static struct resource ldo14_resources[] = {
	{0x1b, 0x1b, "ldo set", IORESOURCE_REG, },
};

static struct resource touch_resources[] = {
	{PM8607_IRQ_PEN, PM8607_IRQ_PEN, "touch", IORESOURCE_IRQ,},
};

static struct resource onkey_resources[] = {
	{PM8607_IRQ_ONKEY, PM8607_IRQ_ONKEY, "onkey", IORESOURCE_IRQ,},
};

static struct resource codec_resources[] = {
	/* Headset microphone insertion or removal */
	{PM8607_IRQ_MICIN,   PM8607_IRQ_MICIN,   "micin",   IORESOURCE_IRQ,},
	/* Hook-switch press or release */
	{PM8607_IRQ_HOOK,    PM8607_IRQ_HOOK,    "hook",    IORESOURCE_IRQ,},
	/* Headset insertion or removal */
	{PM8607_IRQ_HEADSET, PM8607_IRQ_HEADSET, "headset", IORESOURCE_IRQ,},
	/* Audio short */
	{PM8607_IRQ_AUDIO_SHORT, PM8607_IRQ_AUDIO_SHORT, "audio-short", IORESOURCE_IRQ,},
};

static struct resource battery_resources[] = {
	{PM8607_IRQ_CC,  PM8607_IRQ_CC,  "columb counter", IORESOURCE_IRQ,},
	{PM8607_IRQ_BAT, PM8607_IRQ_BAT, "battery",        IORESOURCE_IRQ,},
};

static struct resource charger_resources[] = {
	{PM8607_IRQ_CHG,  PM8607_IRQ_CHG,  "charger detect",  IORESOURCE_IRQ,},
	{PM8607_IRQ_CHG_DONE,  PM8607_IRQ_CHG_DONE,  "charging done",       IORESOURCE_IRQ,},
	{PM8607_IRQ_CHG_FAIL,  PM8607_IRQ_CHG_FAIL,  "charging timeout",    IORESOURCE_IRQ,},
	{PM8607_IRQ_CHG_FAULT, PM8607_IRQ_CHG_FAULT, "charging fault",	    IORESOURCE_IRQ,},
	{PM8607_IRQ_GPADC1,    PM8607_IRQ_GPADC1,    "battery temperature", IORESOURCE_IRQ,},
	{PM8607_IRQ_VBAT, PM8607_IRQ_VBAT, "battery voltage", IORESOURCE_IRQ,},
	{PM8607_IRQ_VCHG, PM8607_IRQ_VCHG, "vchg voltage",    IORESOURCE_IRQ,},
};

static struct resource rtc_resources[] = {
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

static struct charger_regulator chg_desc_regulator_data[] = {
	{ .regulator_name = "preg", },
};

static struct mfd_cell power_devs[] = {
	{"88pm860x-battery", -1,},
	{"88pm860x-charger", -1,},
	{"88pm860x-preg",    -1,},
	{"charger-manager", -1,},
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
	pm860x_irqs[data->hwirq].enable = pm860x_irqs[data->hwirq].offs;
}

static void pm860x_irq_disable(struct irq_data *data)
{
	pm860x_irqs[data->hwirq].enable = 0;
}

static struct irq_chip pm860x_irq_chip = {
	.name		= "88pm860x",
	.irq_bus_lock	= pm860x_irq_lock,
	.irq_bus_sync_unlock = pm860x_irq_sync_unlock,
	.irq_enable	= pm860x_irq_enable,
	.irq_disable	= pm860x_irq_disable,
};

static int pm860x_irq_domain_map(struct irq_domain *d, unsigned int virq,
				 irq_hw_number_t hw)
{
	irq_set_chip_data(virq, d->host_data);
	irq_set_chip_and_handler(virq, &pm860x_irq_chip, handle_edge_irq);
	irq_set_nested_thread(virq, 1);
#ifdef CONFIG_ARM
	set_irq_flags(virq, IRQF_VALID);
#else
	irq_set_noprobe(virq);
#endif
	return 0;
}

static struct irq_domain_ops pm860x_irq_domain_ops = {
	.map	= pm860x_irq_domain_map,
	.xlate	= irq_domain_xlate_onetwocell,
};

static int device_irq_init(struct pm860x_chip *chip,
				     struct pm860x_platform_data *pdata)
{
	struct i2c_client *i2c = (chip->id == CHIP_PM8607) ? chip->client \
				: chip->companion;
	unsigned char status_buf[INT_STATUS_NUM];
	unsigned long flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;
	int data, mask, ret = -EINVAL;
	int nr_irqs, irq_base = -1;
	struct device_node *node = i2c->dev.of_node;

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

	if (pdata && pdata->irq_base)
		irq_base = pdata->irq_base;
	nr_irqs = ARRAY_SIZE(pm860x_irqs);
	chip->irq_base = irq_alloc_descs(irq_base, 0, nr_irqs, 0);
	if (chip->irq_base < 0) {
		dev_err(&i2c->dev, "Failed to allocate interrupts, ret:%d\n",
			chip->irq_base);
		ret = -EBUSY;
		goto out;
	}
	irq_domain_add_legacy(node, nr_irqs, chip->irq_base, 0,
			      &pm860x_irq_domain_ops, chip);
	chip->core_irq = i2c->irq;
	if (!chip->core_irq)
		goto out;

	ret = request_threaded_irq(chip->core_irq, NULL, pm860x_irq, flags | IRQF_ONESHOT,
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

static void device_osc_init(struct i2c_client *i2c)
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

static void device_bk_init(struct pm860x_chip *chip,
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

static void device_led_init(struct pm860x_chip *chip,
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

static void device_regulator_init(struct pm860x_chip *chip,
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

static void device_rtc_init(struct pm860x_chip *chip,
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

static void device_touch_init(struct pm860x_chip *chip,
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

static void device_power_init(struct pm860x_chip *chip,
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
	ret = mfd_add_devices(chip->dev, 0, &power_devs[2], 1,
			      NULL, chip->irq_base, NULL);
	if (ret < 0)
		dev_err(chip->dev, "Failed to add preg subdev\n");

	if (pdata->chg_desc) {
		pdata->chg_desc->charger_regulators =
			&chg_desc_regulator_data[0];
		pdata->chg_desc->num_charger_regulators	=
			ARRAY_SIZE(chg_desc_regulator_data),
		power_devs[3].platform_data = pdata->chg_desc;
		power_devs[3].pdata_size = sizeof(*pdata->chg_desc);
		ret = mfd_add_devices(chip->dev, 0, &power_devs[3], 1,
				      NULL, chip->irq_base, NULL);
		if (ret < 0)
			dev_err(chip->dev, "Failed to add chg-manager subdev\n");
	}
}

static void device_onkey_init(struct pm860x_chip *chip,
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

static void device_codec_init(struct pm860x_chip *chip,
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

static void device_8607_init(struct pm860x_chip *chip,
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

static void device_8606_init(struct pm860x_chip *chip,
				       struct i2c_client *i2c,
				       struct pm860x_platform_data *pdata)
{
	device_osc_init(i2c);
	device_bk_init(chip, pdata);
	device_led_init(chip, pdata);
}

static int pm860x_device_init(struct pm860x_chip *chip,
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

static void pm860x_device_exit(struct pm860x_chip *chip)
{
	device_irq_exit(chip);
	mfd_remove_devices(chip->dev);
}

static int verify_addr(struct i2c_client *i2c)
{
	unsigned short addr_8607[] = {0x30, 0x34};
	unsigned short addr_8606[] = {0x10, 0x11};
	int size, i;

	if (i2c == NULL)
		return 0;
	size = ARRAY_SIZE(addr_8606);
	for (i = 0; i < size; i++) {
		if (i2c->addr == *(addr_8606 + i))
			return CHIP_PM8606;
	}
	size = ARRAY_SIZE(addr_8607);
	for (i = 0; i < size; i++) {
		if (i2c->addr == *(addr_8607 + i))
			return CHIP_PM8607;
	}
	return 0;
}

static struct regmap_config pm860x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int pm860x_dt_init(struct device_node *np,
				    struct device *dev,
				    struct pm860x_platform_data *pdata)
{
	int ret;

	if (of_get_property(np, "marvell,88pm860x-irq-read-clr", NULL))
		pdata->irq_mode = 1;
	ret = of_property_read_u32(np, "marvell,88pm860x-slave-addr",
				   &pdata->companion_addr);
	if (ret) {
		dev_err(dev, "Not found \"marvell,88pm860x-slave-addr\" "
			"property\n");
		pdata->companion_addr = 0;
	}
	return 0;
}

static int pm860x_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	struct pm860x_platform_data *pdata = dev_get_platdata(&client->dev);
	struct device_node *node = client->dev.of_node;
	struct pm860x_chip *chip;
	int ret;

	if (node && !pdata) {
		/* parse DT to get platform data */
		pdata = devm_kzalloc(&client->dev,
				     sizeof(struct pm860x_platform_data),
				     GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;
		ret = pm860x_dt_init(node, &client->dev, pdata);
		if (ret)
			return ret;
	} else if (!pdata) {
		pr_info("No platform data in %s!\n", __func__);
		return -EINVAL;
	}

	chip = devm_kzalloc(&client->dev,
			    sizeof(struct pm860x_chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	chip->id = verify_addr(client);
	chip->regmap = devm_regmap_init_i2c(client, &pm860x_regmap_config);
	if (IS_ERR(chip->regmap)) {
		ret = PTR_ERR(chip->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
				ret);
		return ret;
	}
	chip->client = client;
	i2c_set_clientdata(client, chip);
	chip->dev = &client->dev;
	dev_set_drvdata(chip->dev, chip);

	/*
	 * Both client and companion client shares same platform driver.
	 * Driver distinguishes them by pdata->companion_addr.
	 * pdata->companion_addr is only assigned if companion chip exists.
	 * At the same time, the companion_addr shouldn't equal to client
	 * address.
	 */
	if (pdata->companion_addr && (pdata->companion_addr != client->addr)) {
		chip->companion_addr = pdata->companion_addr;
		chip->companion = i2c_new_dummy(chip->client->adapter,
						chip->companion_addr);
		chip->regmap_companion = regmap_init_i2c(chip->companion,
							&pm860x_regmap_config);
		if (IS_ERR(chip->regmap_companion)) {
			ret = PTR_ERR(chip->regmap_companion);
			dev_err(&chip->companion->dev,
				"Failed to allocate register map: %d\n", ret);
			return ret;
		}
		i2c_set_clientdata(chip->companion, chip);
	}

	pm860x_device_init(chip, pdata);
	return 0;
}

static int pm860x_remove(struct i2c_client *client)
{
	struct pm860x_chip *chip = i2c_get_clientdata(client);

	pm860x_device_exit(chip);
	if (chip->companion) {
		regmap_exit(chip->regmap_companion);
		i2c_unregister_device(chip->companion);
	}
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int pm860x_suspend(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct pm860x_chip *chip = i2c_get_clientdata(client);

	if (device_may_wakeup(dev) && chip->wakeup_flag)
		enable_irq_wake(chip->core_irq);
	return 0;
}

static int pm860x_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct pm860x_chip *chip = i2c_get_clientdata(client);

	if (device_may_wakeup(dev) && chip->wakeup_flag)
		disable_irq_wake(chip->core_irq);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(pm860x_pm_ops, pm860x_suspend, pm860x_resume);

static const struct i2c_device_id pm860x_id_table[] = {
	{ "88PM860x", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, pm860x_id_table);

static const struct of_device_id pm860x_dt_ids[] = {
	{ .compatible = "marvell,88pm860x", },
	{},
};
MODULE_DEVICE_TABLE(of, pm860x_dt_ids);

static struct i2c_driver pm860x_driver = {
	.driver	= {
		.name	= "88PM860x",
		.owner	= THIS_MODULE,
		.pm     = &pm860x_pm_ops,
		.of_match_table	= of_match_ptr(pm860x_dt_ids),
	},
	.probe		= pm860x_probe,
	.remove		= pm860x_remove,
	.id_table	= pm860x_id_table,
};

static int __init pm860x_i2c_init(void)
{
	int ret;
	ret = i2c_add_driver(&pm860x_driver);
	if (ret != 0)
		pr_err("Failed to register 88PM860x I2C driver: %d\n", ret);
	return ret;
}
subsys_initcall(pm860x_i2c_init);

static void __exit pm860x_i2c_exit(void)
{
	i2c_del_driver(&pm860x_driver);
}
module_exit(pm860x_i2c_exit);

MODULE_DESCRIPTION("PMIC Driver for Marvell 88PM860x");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_LICENSE("GPL");
