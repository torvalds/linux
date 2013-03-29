/*
 * Base driver for Maxim MAX8925
 *
 * Copyright (C) 2009-2010 Marvell International Ltd.
 *	Haojian Zhuang <haojian.zhuang@marvell.com>
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
#include <linux/irqdomain.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max8925.h>
#include <linux/of.h>
#include <linux/of_platform.h>

static struct resource bk_resources[] = {
	{ 0x84, 0x84, "mode control", IORESOURCE_REG, },
	{ 0x85, 0x85, "control",      IORESOURCE_REG, },
};

static struct mfd_cell bk_devs[] = {
	{
		.name		= "max8925-backlight",
		.num_resources	= ARRAY_SIZE(bk_resources),
		.resources	= &bk_resources[0],
		.id		= -1,
	},
};

static struct resource touch_resources[] = {
	{
		.name	= "max8925-tsc",
		.start	= MAX8925_TSC_IRQ,
		.end	= MAX8925_ADC_RES_END,
		.flags	= IORESOURCE_REG,
	},
};

static struct mfd_cell touch_devs[] = {
	{
		.name		= "max8925-touch",
		.num_resources	= 1,
		.resources	= &touch_resources[0],
		.id		= -1,
	},
};

static struct resource power_supply_resources[] = {
	{
		.name	= "max8925-power",
		.start	= MAX8925_CHG_IRQ1,
		.end	= MAX8925_CHG_IRQ1_MASK,
		.flags	= IORESOURCE_REG,
	},
};

static struct mfd_cell power_devs[] = {
	{
		.name		= "max8925-power",
		.num_resources	= 1,
		.resources	= &power_supply_resources[0],
		.id		= -1,
	},
};

static struct resource rtc_resources[] = {
	{
		.name	= "max8925-rtc",
		.start	= MAX8925_IRQ_RTC_ALARM0,
		.end	= MAX8925_IRQ_RTC_ALARM0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mfd_cell rtc_devs[] = {
	{
		.name		= "max8925-rtc",
		.num_resources	= 1,
		.resources	= &rtc_resources[0],
		.id		= -1,
	},
};

static struct resource onkey_resources[] = {
	{
		.name	= "max8925-onkey",
		.start	= MAX8925_IRQ_GPM_SW_R,
		.end	= MAX8925_IRQ_GPM_SW_R,
		.flags	= IORESOURCE_IRQ,
	}, {
		.name	= "max8925-onkey",
		.start	= MAX8925_IRQ_GPM_SW_F,
		.end	= MAX8925_IRQ_GPM_SW_F,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mfd_cell onkey_devs[] = {
	{
		.name		= "max8925-onkey",
		.num_resources	= 2,
		.resources	= &onkey_resources[0],
		.id		= -1,
	},
};

static struct resource sd1_resources[] = {
	{0x06, 0x06, "sdv", IORESOURCE_REG, },
};

static struct resource sd2_resources[] = {
	{0x09, 0x09, "sdv", IORESOURCE_REG, },
};

static struct resource sd3_resources[] = {
	{0x0c, 0x0c, "sdv", IORESOURCE_REG, },
};

static struct resource ldo1_resources[] = {
	{0x1a, 0x1a, "ldov", IORESOURCE_REG, },
};

static struct resource ldo2_resources[] = {
	{0x1e, 0x1e, "ldov", IORESOURCE_REG, },
};

static struct resource ldo3_resources[] = {
	{0x22, 0x22, "ldov", IORESOURCE_REG, },
};

static struct resource ldo4_resources[] = {
	{0x26, 0x26, "ldov", IORESOURCE_REG, },
};

static struct resource ldo5_resources[] = {
	{0x2a, 0x2a, "ldov", IORESOURCE_REG, },
};

static struct resource ldo6_resources[] = {
	{0x2e, 0x2e, "ldov", IORESOURCE_REG, },
};

static struct resource ldo7_resources[] = {
	{0x32, 0x32, "ldov", IORESOURCE_REG, },
};

static struct resource ldo8_resources[] = {
	{0x36, 0x36, "ldov", IORESOURCE_REG, },
};

static struct resource ldo9_resources[] = {
	{0x3a, 0x3a, "ldov", IORESOURCE_REG, },
};

static struct resource ldo10_resources[] = {
	{0x3e, 0x3e, "ldov", IORESOURCE_REG, },
};

static struct resource ldo11_resources[] = {
	{0x42, 0x42, "ldov", IORESOURCE_REG, },
};

static struct resource ldo12_resources[] = {
	{0x46, 0x46, "ldov", IORESOURCE_REG, },
};

static struct resource ldo13_resources[] = {
	{0x4a, 0x4a, "ldov", IORESOURCE_REG, },
};

static struct resource ldo14_resources[] = {
	{0x4e, 0x4e, "ldov", IORESOURCE_REG, },
};

static struct resource ldo15_resources[] = {
	{0x52, 0x52, "ldov", IORESOURCE_REG, },
};

static struct resource ldo16_resources[] = {
	{0x12, 0x12, "ldov", IORESOURCE_REG, },
};

static struct resource ldo17_resources[] = {
	{0x16, 0x16, "ldov", IORESOURCE_REG, },
};

static struct resource ldo18_resources[] = {
	{0x74, 0x74, "ldov", IORESOURCE_REG, },
};

static struct resource ldo19_resources[] = {
	{0x5e, 0x5e, "ldov", IORESOURCE_REG, },
};

static struct resource ldo20_resources[] = {
	{0x9e, 0x9e, "ldov", IORESOURCE_REG, },
};

static struct mfd_cell reg_devs[] = {
	{
		.name = "max8925-regulator",
		.id = 0,
		.num_resources = ARRAY_SIZE(sd1_resources),
		.resources = sd1_resources,
	}, {
		.name = "max8925-regulator",
		.id = 1,
		.num_resources = ARRAY_SIZE(sd2_resources),
		.resources = sd2_resources,
	}, {
		.name = "max8925-regulator",
		.id = 2,
		.num_resources = ARRAY_SIZE(sd3_resources),
		.resources = sd3_resources,
	}, {
		.name = "max8925-regulator",
		.id = 3,
		.num_resources = ARRAY_SIZE(ldo1_resources),
		.resources = ldo1_resources,
	}, {
		.name = "max8925-regulator",
		.id = 4,
		.num_resources = ARRAY_SIZE(ldo2_resources),
		.resources = ldo2_resources,
	}, {
		.name = "max8925-regulator",
		.id = 5,
		.num_resources = ARRAY_SIZE(ldo3_resources),
		.resources = ldo3_resources,
	}, {
		.name = "max8925-regulator",
		.id = 6,
		.num_resources = ARRAY_SIZE(ldo4_resources),
		.resources = ldo4_resources,
	}, {
		.name = "max8925-regulator",
		.id = 7,
		.num_resources = ARRAY_SIZE(ldo5_resources),
		.resources = ldo5_resources,
	}, {
		.name = "max8925-regulator",
		.id = 8,
		.num_resources = ARRAY_SIZE(ldo6_resources),
		.resources = ldo6_resources,
	}, {
		.name = "max8925-regulator",
		.id = 9,
		.num_resources = ARRAY_SIZE(ldo7_resources),
		.resources = ldo7_resources,
	}, {
		.name = "max8925-regulator",
		.id = 10,
		.num_resources = ARRAY_SIZE(ldo8_resources),
		.resources = ldo8_resources,
	}, {
		.name = "max8925-regulator",
		.id = 11,
		.num_resources = ARRAY_SIZE(ldo9_resources),
		.resources = ldo9_resources,
	}, {
		.name = "max8925-regulator",
		.id = 12,
		.num_resources = ARRAY_SIZE(ldo10_resources),
		.resources = ldo10_resources,
	}, {
		.name = "max8925-regulator",
		.id = 13,
		.num_resources = ARRAY_SIZE(ldo11_resources),
		.resources = ldo11_resources,
	}, {
		.name = "max8925-regulator",
		.id = 14,
		.num_resources = ARRAY_SIZE(ldo12_resources),
		.resources = ldo12_resources,
	}, {
		.name = "max8925-regulator",
		.id = 15,
		.num_resources = ARRAY_SIZE(ldo13_resources),
		.resources = ldo13_resources,
	}, {
		.name = "max8925-regulator",
		.id = 16,
		.num_resources = ARRAY_SIZE(ldo14_resources),
		.resources = ldo14_resources,
	}, {
		.name = "max8925-regulator",
		.id = 17,
		.num_resources = ARRAY_SIZE(ldo15_resources),
		.resources = ldo15_resources,
	}, {
		.name = "max8925-regulator",
		.id = 18,
		.num_resources = ARRAY_SIZE(ldo16_resources),
		.resources = ldo16_resources,
	}, {
		.name = "max8925-regulator",
		.id = 19,
		.num_resources = ARRAY_SIZE(ldo17_resources),
		.resources = ldo17_resources,
	}, {
		.name = "max8925-regulator",
		.id = 20,
		.num_resources = ARRAY_SIZE(ldo18_resources),
		.resources = ldo18_resources,
	}, {
		.name = "max8925-regulator",
		.id = 21,
		.num_resources = ARRAY_SIZE(ldo19_resources),
		.resources = ldo19_resources,
	}, {
		.name = "max8925-regulator",
		.id = 22,
		.num_resources = ARRAY_SIZE(ldo20_resources),
		.resources = ldo20_resources,
	},
};

enum {
	FLAGS_ADC = 1,	/* register in ADC component */
	FLAGS_RTC,	/* register in RTC component */
};

struct max8925_irq_data {
	int	reg;
	int	mask_reg;
	int	enable;		/* enable or not */
	int	offs;		/* bit offset in mask register */
	int	flags;
	int	tsc_irq;
};

static struct max8925_irq_data max8925_irqs[] = {
	[MAX8925_IRQ_VCHG_DC_OVP] = {
		.reg		= MAX8925_CHG_IRQ1,
		.mask_reg	= MAX8925_CHG_IRQ1_MASK,
		.offs		= 1 << 0,
	},
	[MAX8925_IRQ_VCHG_DC_F] = {
		.reg		= MAX8925_CHG_IRQ1,
		.mask_reg	= MAX8925_CHG_IRQ1_MASK,
		.offs		= 1 << 1,
	},
	[MAX8925_IRQ_VCHG_DC_R] = {
		.reg		= MAX8925_CHG_IRQ1,
		.mask_reg	= MAX8925_CHG_IRQ1_MASK,
		.offs		= 1 << 2,
	},
	[MAX8925_IRQ_VCHG_THM_OK_R] = {
		.reg		= MAX8925_CHG_IRQ2,
		.mask_reg	= MAX8925_CHG_IRQ2_MASK,
		.offs		= 1 << 0,
	},
	[MAX8925_IRQ_VCHG_THM_OK_F] = {
		.reg		= MAX8925_CHG_IRQ2,
		.mask_reg	= MAX8925_CHG_IRQ2_MASK,
		.offs		= 1 << 1,
	},
	[MAX8925_IRQ_VCHG_SYSLOW_F] = {
		.reg		= MAX8925_CHG_IRQ2,
		.mask_reg	= MAX8925_CHG_IRQ2_MASK,
		.offs		= 1 << 2,
	},
	[MAX8925_IRQ_VCHG_SYSLOW_R] = {
		.reg		= MAX8925_CHG_IRQ2,
		.mask_reg	= MAX8925_CHG_IRQ2_MASK,
		.offs		= 1 << 3,
	},
	[MAX8925_IRQ_VCHG_RST] = {
		.reg		= MAX8925_CHG_IRQ2,
		.mask_reg	= MAX8925_CHG_IRQ2_MASK,
		.offs		= 1 << 4,
	},
	[MAX8925_IRQ_VCHG_DONE] = {
		.reg		= MAX8925_CHG_IRQ2,
		.mask_reg	= MAX8925_CHG_IRQ2_MASK,
		.offs		= 1 << 5,
	},
	[MAX8925_IRQ_VCHG_TOPOFF] = {
		.reg		= MAX8925_CHG_IRQ2,
		.mask_reg	= MAX8925_CHG_IRQ2_MASK,
		.offs		= 1 << 6,
	},
	[MAX8925_IRQ_VCHG_TMR_FAULT] = {
		.reg		= MAX8925_CHG_IRQ2,
		.mask_reg	= MAX8925_CHG_IRQ2_MASK,
		.offs		= 1 << 7,
	},
	[MAX8925_IRQ_GPM_RSTIN] = {
		.reg		= MAX8925_ON_OFF_IRQ1,
		.mask_reg	= MAX8925_ON_OFF_IRQ1_MASK,
		.offs		= 1 << 0,
	},
	[MAX8925_IRQ_GPM_MPL] = {
		.reg		= MAX8925_ON_OFF_IRQ1,
		.mask_reg	= MAX8925_ON_OFF_IRQ1_MASK,
		.offs		= 1 << 1,
	},
	[MAX8925_IRQ_GPM_SW_3SEC] = {
		.reg		= MAX8925_ON_OFF_IRQ1,
		.mask_reg	= MAX8925_ON_OFF_IRQ1_MASK,
		.offs		= 1 << 2,
	},
	[MAX8925_IRQ_GPM_EXTON_F] = {
		.reg		= MAX8925_ON_OFF_IRQ1,
		.mask_reg	= MAX8925_ON_OFF_IRQ1_MASK,
		.offs		= 1 << 3,
	},
	[MAX8925_IRQ_GPM_EXTON_R] = {
		.reg		= MAX8925_ON_OFF_IRQ1,
		.mask_reg	= MAX8925_ON_OFF_IRQ1_MASK,
		.offs		= 1 << 4,
	},
	[MAX8925_IRQ_GPM_SW_1SEC] = {
		.reg		= MAX8925_ON_OFF_IRQ1,
		.mask_reg	= MAX8925_ON_OFF_IRQ1_MASK,
		.offs		= 1 << 5,
	},
	[MAX8925_IRQ_GPM_SW_F] = {
		.reg		= MAX8925_ON_OFF_IRQ1,
		.mask_reg	= MAX8925_ON_OFF_IRQ1_MASK,
		.offs		= 1 << 6,
	},
	[MAX8925_IRQ_GPM_SW_R] = {
		.reg		= MAX8925_ON_OFF_IRQ1,
		.mask_reg	= MAX8925_ON_OFF_IRQ1_MASK,
		.offs		= 1 << 7,
	},
	[MAX8925_IRQ_GPM_SYSCKEN_F] = {
		.reg		= MAX8925_ON_OFF_IRQ2,
		.mask_reg	= MAX8925_ON_OFF_IRQ2_MASK,
		.offs		= 1 << 0,
	},
	[MAX8925_IRQ_GPM_SYSCKEN_R] = {
		.reg		= MAX8925_ON_OFF_IRQ2,
		.mask_reg	= MAX8925_ON_OFF_IRQ2_MASK,
		.offs		= 1 << 1,
	},
	[MAX8925_IRQ_RTC_ALARM1] = {
		.reg		= MAX8925_RTC_IRQ,
		.mask_reg	= MAX8925_RTC_IRQ_MASK,
		.offs		= 1 << 2,
		.flags		= FLAGS_RTC,
	},
	[MAX8925_IRQ_RTC_ALARM0] = {
		.reg		= MAX8925_RTC_IRQ,
		.mask_reg	= MAX8925_RTC_IRQ_MASK,
		.offs		= 1 << 3,
		.flags		= FLAGS_RTC,
	},
	[MAX8925_IRQ_TSC_STICK] = {
		.reg		= MAX8925_TSC_IRQ,
		.mask_reg	= MAX8925_TSC_IRQ_MASK,
		.offs		= 1 << 0,
		.flags		= FLAGS_ADC,
		.tsc_irq	= 1,
	},
	[MAX8925_IRQ_TSC_NSTICK] = {
		.reg		= MAX8925_TSC_IRQ,
		.mask_reg	= MAX8925_TSC_IRQ_MASK,
		.offs		= 1 << 1,
		.flags		= FLAGS_ADC,
		.tsc_irq	= 1,
	},
};

static inline struct max8925_irq_data *irq_to_max8925(struct max8925_chip *chip,
						      int irq)
{
	return &max8925_irqs[irq - chip->irq_base];
}

static irqreturn_t max8925_irq(int irq, void *data)
{
	struct max8925_chip *chip = data;
	struct max8925_irq_data *irq_data;
	struct i2c_client *i2c;
	int read_reg = -1, value = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(max8925_irqs); i++) {
		irq_data = &max8925_irqs[i];
		/* TSC IRQ should be serviced in max8925_tsc_irq() */
		if (irq_data->tsc_irq)
			continue;
		if (irq_data->flags == FLAGS_RTC)
			i2c = chip->rtc;
		else if (irq_data->flags == FLAGS_ADC)
			i2c = chip->adc;
		else
			i2c = chip->i2c;
		if (read_reg != irq_data->reg) {
			read_reg = irq_data->reg;
			value = max8925_reg_read(i2c, irq_data->reg);
		}
		if (value & irq_data->enable)
			handle_nested_irq(chip->irq_base + i);
	}
	return IRQ_HANDLED;
}

static irqreturn_t max8925_tsc_irq(int irq, void *data)
{
	struct max8925_chip *chip = data;
	struct max8925_irq_data *irq_data;
	struct i2c_client *i2c;
	int read_reg = -1, value = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(max8925_irqs); i++) {
		irq_data = &max8925_irqs[i];
		/* non TSC IRQ should be serviced in max8925_irq() */
		if (!irq_data->tsc_irq)
			continue;
		if (irq_data->flags == FLAGS_RTC)
			i2c = chip->rtc;
		else if (irq_data->flags == FLAGS_ADC)
			i2c = chip->adc;
		else
			i2c = chip->i2c;
		if (read_reg != irq_data->reg) {
			read_reg = irq_data->reg;
			value = max8925_reg_read(i2c, irq_data->reg);
		}
		if (value & irq_data->enable)
			handle_nested_irq(chip->irq_base + i);
	}
	return IRQ_HANDLED;
}

static void max8925_irq_lock(struct irq_data *data)
{
	struct max8925_chip *chip = irq_data_get_irq_chip_data(data);

	mutex_lock(&chip->irq_lock);
}

static void max8925_irq_sync_unlock(struct irq_data *data)
{
	struct max8925_chip *chip = irq_data_get_irq_chip_data(data);
	struct max8925_irq_data *irq_data;
	static unsigned char cache_chg[2] = {0xff, 0xff};
	static unsigned char cache_on[2] = {0xff, 0xff};
	static unsigned char cache_rtc = 0xff, cache_tsc = 0xff;
	unsigned char irq_chg[2], irq_on[2];
	unsigned char irq_rtc, irq_tsc;
	int i;

	/* Load cached value. In initial, all IRQs are masked */
	irq_chg[0] = cache_chg[0];
	irq_chg[1] = cache_chg[1];
	irq_on[0] = cache_on[0];
	irq_on[1] = cache_on[1];
	irq_rtc = cache_rtc;
	irq_tsc = cache_tsc;
	for (i = 0; i < ARRAY_SIZE(max8925_irqs); i++) {
		irq_data = &max8925_irqs[i];
		/* 1 -- disable, 0 -- enable */
		switch (irq_data->mask_reg) {
		case MAX8925_CHG_IRQ1_MASK:
			irq_chg[0] &= ~irq_data->enable;
			break;
		case MAX8925_CHG_IRQ2_MASK:
			irq_chg[1] &= ~irq_data->enable;
			break;
		case MAX8925_ON_OFF_IRQ1_MASK:
			irq_on[0] &= ~irq_data->enable;
			break;
		case MAX8925_ON_OFF_IRQ2_MASK:
			irq_on[1] &= ~irq_data->enable;
			break;
		case MAX8925_RTC_IRQ_MASK:
			irq_rtc &= ~irq_data->enable;
			break;
		case MAX8925_TSC_IRQ_MASK:
			irq_tsc &= ~irq_data->enable;
			break;
		default:
			dev_err(chip->dev, "wrong IRQ\n");
			break;
		}
	}
	/* update mask into registers */
	if (cache_chg[0] != irq_chg[0]) {
		cache_chg[0] = irq_chg[0];
		max8925_reg_write(chip->i2c, MAX8925_CHG_IRQ1_MASK,
			irq_chg[0]);
	}
	if (cache_chg[1] != irq_chg[1]) {
		cache_chg[1] = irq_chg[1];
		max8925_reg_write(chip->i2c, MAX8925_CHG_IRQ2_MASK,
			irq_chg[1]);
	}
	if (cache_on[0] != irq_on[0]) {
		cache_on[0] = irq_on[0];
		max8925_reg_write(chip->i2c, MAX8925_ON_OFF_IRQ1_MASK,
				irq_on[0]);
	}
	if (cache_on[1] != irq_on[1]) {
		cache_on[1] = irq_on[1];
		max8925_reg_write(chip->i2c, MAX8925_ON_OFF_IRQ2_MASK,
				irq_on[1]);
	}
	if (cache_rtc != irq_rtc) {
		cache_rtc = irq_rtc;
		max8925_reg_write(chip->rtc, MAX8925_RTC_IRQ_MASK, irq_rtc);
	}
	if (cache_tsc != irq_tsc) {
		cache_tsc = irq_tsc;
		max8925_reg_write(chip->adc, MAX8925_TSC_IRQ_MASK, irq_tsc);
	}

	mutex_unlock(&chip->irq_lock);
}

static void max8925_irq_enable(struct irq_data *data)
{
	struct max8925_chip *chip = irq_data_get_irq_chip_data(data);
	max8925_irqs[data->irq - chip->irq_base].enable
		= max8925_irqs[data->irq - chip->irq_base].offs;
}

static void max8925_irq_disable(struct irq_data *data)
{
	struct max8925_chip *chip = irq_data_get_irq_chip_data(data);
	max8925_irqs[data->irq - chip->irq_base].enable = 0;
}

static struct irq_chip max8925_irq_chip = {
	.name		= "max8925",
	.irq_bus_lock	= max8925_irq_lock,
	.irq_bus_sync_unlock = max8925_irq_sync_unlock,
	.irq_enable	= max8925_irq_enable,
	.irq_disable	= max8925_irq_disable,
};

static int max8925_irq_domain_map(struct irq_domain *d, unsigned int virq,
				 irq_hw_number_t hw)
{
	irq_set_chip_data(virq, d->host_data);
	irq_set_chip_and_handler(virq, &max8925_irq_chip, handle_edge_irq);
	irq_set_nested_thread(virq, 1);
#ifdef CONFIG_ARM
	set_irq_flags(virq, IRQF_VALID);
#else
	irq_set_noprobe(virq);
#endif
	return 0;
}

static struct irq_domain_ops max8925_irq_domain_ops = {
	.map	= max8925_irq_domain_map,
	.xlate	= irq_domain_xlate_onetwocell,
};


static int max8925_irq_init(struct max8925_chip *chip, int irq,
			    struct max8925_platform_data *pdata)
{
	unsigned long flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;
	int ret;
	struct device_node *node = chip->dev->of_node;

	/* clear all interrupts */
	max8925_reg_read(chip->i2c, MAX8925_CHG_IRQ1);
	max8925_reg_read(chip->i2c, MAX8925_CHG_IRQ2);
	max8925_reg_read(chip->i2c, MAX8925_ON_OFF_IRQ1);
	max8925_reg_read(chip->i2c, MAX8925_ON_OFF_IRQ2);
	max8925_reg_read(chip->rtc, MAX8925_RTC_IRQ);
	max8925_reg_read(chip->adc, MAX8925_TSC_IRQ);
	/* mask all interrupts except for TSC */
	max8925_reg_write(chip->rtc, MAX8925_ALARM0_CNTL, 0);
	max8925_reg_write(chip->rtc, MAX8925_ALARM1_CNTL, 0);
	max8925_reg_write(chip->i2c, MAX8925_CHG_IRQ1_MASK, 0xff);
	max8925_reg_write(chip->i2c, MAX8925_CHG_IRQ2_MASK, 0xff);
	max8925_reg_write(chip->i2c, MAX8925_ON_OFF_IRQ1_MASK, 0xff);
	max8925_reg_write(chip->i2c, MAX8925_ON_OFF_IRQ2_MASK, 0xff);
	max8925_reg_write(chip->rtc, MAX8925_RTC_IRQ_MASK, 0xff);

	mutex_init(&chip->irq_lock);
	chip->irq_base = irq_alloc_descs(-1, 0, MAX8925_NR_IRQS, 0);
	if (chip->irq_base < 0) {
		dev_err(chip->dev, "Failed to allocate interrupts, ret:%d\n",
			chip->irq_base);
		return -EBUSY;
	}

	irq_domain_add_legacy(node, MAX8925_NR_IRQS, chip->irq_base, 0,
			      &max8925_irq_domain_ops, chip);

	/* request irq handler for pmic main irq*/
	chip->core_irq = irq;
	if (!chip->core_irq)
		return -EBUSY;
	ret = request_threaded_irq(irq, NULL, max8925_irq, flags | IRQF_ONESHOT,
				   "max8925", chip);
	if (ret) {
		dev_err(chip->dev, "Failed to request core IRQ: %d\n", ret);
		chip->core_irq = 0;
		return -EBUSY;
	}

	/* request irq handler for pmic tsc irq*/

	/* mask TSC interrupt */
	max8925_reg_write(chip->adc, MAX8925_TSC_IRQ_MASK, 0x0f);

	if (!pdata->tsc_irq) {
		dev_warn(chip->dev, "No interrupt support on TSC IRQ\n");
		return 0;
	}
	chip->tsc_irq = pdata->tsc_irq;
	ret = request_threaded_irq(chip->tsc_irq, NULL, max8925_tsc_irq,
				   flags | IRQF_ONESHOT, "max8925-tsc", chip);
	if (ret) {
		dev_err(chip->dev, "Failed to request TSC IRQ: %d\n", ret);
		chip->tsc_irq = 0;
	}
	return 0;
}

static void init_regulator(struct max8925_chip *chip,
				     struct max8925_platform_data *pdata)
{
	int ret;

	if (!pdata)
		return;
	if (pdata->sd1) {
		reg_devs[0].platform_data = pdata->sd1;
		reg_devs[0].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->sd2) {
		reg_devs[1].platform_data = pdata->sd2;
		reg_devs[1].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->sd3) {
		reg_devs[2].platform_data = pdata->sd3;
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
	if (pdata->ldo11) {
		reg_devs[13].platform_data = pdata->ldo11;
		reg_devs[13].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo12) {
		reg_devs[14].platform_data = pdata->ldo12;
		reg_devs[14].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo13) {
		reg_devs[15].platform_data = pdata->ldo13;
		reg_devs[15].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo14) {
		reg_devs[16].platform_data = pdata->ldo14;
		reg_devs[16].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo15) {
		reg_devs[17].platform_data = pdata->ldo15;
		reg_devs[17].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo16) {
		reg_devs[18].platform_data = pdata->ldo16;
		reg_devs[18].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo17) {
		reg_devs[19].platform_data = pdata->ldo17;
		reg_devs[19].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo18) {
		reg_devs[20].platform_data = pdata->ldo18;
		reg_devs[20].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo19) {
		reg_devs[21].platform_data = pdata->ldo19;
		reg_devs[21].pdata_size = sizeof(struct regulator_init_data);
	}
	if (pdata->ldo20) {
		reg_devs[22].platform_data = pdata->ldo20;
		reg_devs[22].pdata_size = sizeof(struct regulator_init_data);
	}
	ret = mfd_add_devices(chip->dev, 0, reg_devs, ARRAY_SIZE(reg_devs),
			      NULL, 0, NULL);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to add regulator subdev\n");
		return;
	}
}

int max8925_device_init(struct max8925_chip *chip,
				  struct max8925_platform_data *pdata)
{
	int ret;

	max8925_irq_init(chip, chip->i2c->irq, pdata);

	if (pdata && (pdata->power || pdata->touch)) {
		/* enable ADC to control internal reference */
		max8925_set_bits(chip->i2c, MAX8925_RESET_CNFG, 1, 1);
		/* enable internal reference for ADC */
		max8925_set_bits(chip->adc, MAX8925_TSC_CNFG1, 3, 2);
		/* check for internal reference IRQ */
		do {
			ret = max8925_reg_read(chip->adc, MAX8925_TSC_IRQ);
		} while (ret & MAX8925_NREF_OK);
		/* enaable ADC scheduler, interval is 1 second */
		max8925_set_bits(chip->adc, MAX8925_ADC_SCHED, 3, 2);
	}

	/* enable Momentary Power Loss */
	max8925_set_bits(chip->rtc, MAX8925_MPL_CNTL, 1 << 4, 1 << 4);

	ret = mfd_add_devices(chip->dev, 0, &rtc_devs[0],
			      ARRAY_SIZE(rtc_devs),
			      NULL, chip->irq_base, NULL);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to add rtc subdev\n");
		goto out;
	}

	ret = mfd_add_devices(chip->dev, 0, &onkey_devs[0],
			      ARRAY_SIZE(onkey_devs),
			      NULL, chip->irq_base, NULL);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to add onkey subdev\n");
		goto out_dev;
	}

	init_regulator(chip, pdata);

	if (pdata && pdata->backlight) {
		bk_devs[0].platform_data = &pdata->backlight;
		bk_devs[0].pdata_size = sizeof(struct max8925_backlight_pdata);
	}
	ret = mfd_add_devices(chip->dev, 0, bk_devs, ARRAY_SIZE(bk_devs),
			      NULL, 0, NULL);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to add backlight subdev\n");
		goto out_dev;
	}

	ret = mfd_add_devices(chip->dev, 0, &power_devs[0],
			      ARRAY_SIZE(power_devs),
			      NULL, 0, NULL);
	if (ret < 0) {
		dev_err(chip->dev,
			"Failed to add power supply subdev, err = %d\n", ret);
		goto out_dev;
	}

	if (pdata && pdata->touch) {
		ret = mfd_add_devices(chip->dev, 0, &touch_devs[0],
				      ARRAY_SIZE(touch_devs),
				      NULL, chip->tsc_irq, NULL);
		if (ret < 0) {
			dev_err(chip->dev, "Failed to add touch subdev\n");
			goto out_dev;
		}
	}

	return 0;
out_dev:
	mfd_remove_devices(chip->dev);
out:
	return ret;
}

void max8925_device_exit(struct max8925_chip *chip)
{
	if (chip->core_irq)
		free_irq(chip->core_irq, chip);
	if (chip->tsc_irq)
		free_irq(chip->tsc_irq, chip);
	mfd_remove_devices(chip->dev);
}


MODULE_DESCRIPTION("PMIC Driver for Maxim MAX8925");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com");
MODULE_LICENSE("GPL");
