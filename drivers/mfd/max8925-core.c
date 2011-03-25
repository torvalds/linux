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
#include <linux/platform_device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max8925.h>

static struct resource backlight_resources[] = {
	{
		.name	= "max8925-backlight",
		.start	= MAX8925_WLED_MODE_CNTL,
		.end	= MAX8925_WLED_CNTL,
		.flags	= IORESOURCE_IO,
	},
};

static struct mfd_cell backlight_devs[] = {
	{
		.name		= "max8925-backlight",
		.num_resources	= 1,
		.resources	= &backlight_resources[0],
		.id		= -1,
	},
};

static struct resource touch_resources[] = {
	{
		.name	= "max8925-tsc",
		.start	= MAX8925_TSC_IRQ,
		.end	= MAX8925_ADC_RES_END,
		.flags	= IORESOURCE_IO,
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
		.flags	= IORESOURCE_IO,
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
		.start	= MAX8925_RTC_IRQ,
		.end	= MAX8925_RTC_IRQ_MASK,
		.flags	= IORESOURCE_IO,
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

#define MAX8925_REG_RESOURCE(_start, _end)	\
{						\
	.start	= MAX8925_##_start,		\
	.end	= MAX8925_##_end,		\
	.flags	= IORESOURCE_IO,		\
}

static struct resource regulator_resources[] = {
	MAX8925_REG_RESOURCE(SDCTL1, SDCTL1),
	MAX8925_REG_RESOURCE(SDCTL2, SDCTL2),
	MAX8925_REG_RESOURCE(SDCTL3, SDCTL3),
	MAX8925_REG_RESOURCE(LDOCTL1, LDOCTL1),
	MAX8925_REG_RESOURCE(LDOCTL2, LDOCTL2),
	MAX8925_REG_RESOURCE(LDOCTL3, LDOCTL3),
	MAX8925_REG_RESOURCE(LDOCTL4, LDOCTL4),
	MAX8925_REG_RESOURCE(LDOCTL5, LDOCTL5),
	MAX8925_REG_RESOURCE(LDOCTL6, LDOCTL6),
	MAX8925_REG_RESOURCE(LDOCTL7, LDOCTL7),
	MAX8925_REG_RESOURCE(LDOCTL8, LDOCTL8),
	MAX8925_REG_RESOURCE(LDOCTL9, LDOCTL9),
	MAX8925_REG_RESOURCE(LDOCTL10, LDOCTL10),
	MAX8925_REG_RESOURCE(LDOCTL11, LDOCTL11),
	MAX8925_REG_RESOURCE(LDOCTL12, LDOCTL12),
	MAX8925_REG_RESOURCE(LDOCTL13, LDOCTL13),
	MAX8925_REG_RESOURCE(LDOCTL14, LDOCTL14),
	MAX8925_REG_RESOURCE(LDOCTL15, LDOCTL15),
	MAX8925_REG_RESOURCE(LDOCTL16, LDOCTL16),
	MAX8925_REG_RESOURCE(LDOCTL17, LDOCTL17),
	MAX8925_REG_RESOURCE(LDOCTL18, LDOCTL18),
	MAX8925_REG_RESOURCE(LDOCTL19, LDOCTL19),
	MAX8925_REG_RESOURCE(LDOCTL20, LDOCTL20),
};

#define MAX8925_REG_DEVS(_id)						\
{									\
	.name		= "max8925-regulator",				\
	.num_resources	= 1,						\
	.resources	= &regulator_resources[MAX8925_ID_##_id],	\
	.id		= MAX8925_ID_##_id,				\
}

static struct mfd_cell regulator_devs[] = {
	MAX8925_REG_DEVS(SD1),
	MAX8925_REG_DEVS(SD2),
	MAX8925_REG_DEVS(SD3),
	MAX8925_REG_DEVS(LDO1),
	MAX8925_REG_DEVS(LDO2),
	MAX8925_REG_DEVS(LDO3),
	MAX8925_REG_DEVS(LDO4),
	MAX8925_REG_DEVS(LDO5),
	MAX8925_REG_DEVS(LDO6),
	MAX8925_REG_DEVS(LDO7),
	MAX8925_REG_DEVS(LDO8),
	MAX8925_REG_DEVS(LDO9),
	MAX8925_REG_DEVS(LDO10),
	MAX8925_REG_DEVS(LDO11),
	MAX8925_REG_DEVS(LDO12),
	MAX8925_REG_DEVS(LDO13),
	MAX8925_REG_DEVS(LDO14),
	MAX8925_REG_DEVS(LDO15),
	MAX8925_REG_DEVS(LDO16),
	MAX8925_REG_DEVS(LDO17),
	MAX8925_REG_DEVS(LDO18),
	MAX8925_REG_DEVS(LDO19),
	MAX8925_REG_DEVS(LDO20),
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
	[MAX8925_IRQ_VCHG_USB_OVP] = {
		.reg		= MAX8925_CHG_IRQ1,
		.mask_reg	= MAX8925_CHG_IRQ1_MASK,
		.offs		= 1 << 3,
	},
	[MAX8925_IRQ_VCHG_USB_F] =  {
		.reg		= MAX8925_CHG_IRQ1,
		.mask_reg	= MAX8925_CHG_IRQ1_MASK,
		.offs		= 1 << 4,
	},
	[MAX8925_IRQ_VCHG_USB_R] = {
		.reg		= MAX8925_CHG_IRQ1,
		.mask_reg	= MAX8925_CHG_IRQ1_MASK,
		.offs		= 1 << 5,
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

static int max8925_irq_init(struct max8925_chip *chip, int irq,
			    struct max8925_platform_data *pdata)
{
	unsigned long flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;
	int i, ret;
	int __irq;

	if (!pdata || !pdata->irq_base) {
		dev_warn(chip->dev, "No interrupt support on IRQ base\n");
		return -EINVAL;
	}
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
	chip->core_irq = irq;
	chip->irq_base = pdata->irq_base;

	/* register with genirq */
	for (i = 0; i < ARRAY_SIZE(max8925_irqs); i++) {
		__irq = i + chip->irq_base;
		irq_set_chip_data(__irq, chip);
		irq_set_chip_and_handler(__irq, &max8925_irq_chip,
					 handle_edge_irq);
		irq_set_nested_thread(__irq, 1);
#ifdef CONFIG_ARM
		set_irq_flags(__irq, IRQF_VALID);
#else
		irq_set_noprobe(__irq);
#endif
	}
	if (!irq) {
		dev_warn(chip->dev, "No interrupt support on core IRQ\n");
		goto tsc_irq;
	}

	ret = request_threaded_irq(irq, NULL, max8925_irq, flags,
				   "max8925", chip);
	if (ret) {
		dev_err(chip->dev, "Failed to request core IRQ: %d\n", ret);
		chip->core_irq = 0;
	}

tsc_irq:
	/* mask TSC interrupt */
	max8925_reg_write(chip->adc, MAX8925_TSC_IRQ_MASK, 0x0f);

	if (!pdata->tsc_irq) {
		dev_warn(chip->dev, "No interrupt support on TSC IRQ\n");
		return 0;
	}
	chip->tsc_irq = pdata->tsc_irq;

	ret = request_threaded_irq(chip->tsc_irq, NULL, max8925_tsc_irq,
				   flags, "max8925-tsc", chip);
	if (ret) {
		dev_err(chip->dev, "Failed to request TSC IRQ: %d\n", ret);
		chip->tsc_irq = 0;
	}
	return 0;
}

int __devinit max8925_device_init(struct max8925_chip *chip,
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
			      &rtc_resources[0], 0);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to add rtc subdev\n");
		goto out;
	}

	ret = mfd_add_devices(chip->dev, 0, &onkey_devs[0],
			      ARRAY_SIZE(onkey_devs),
			      &onkey_resources[0], 0);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to add onkey subdev\n");
		goto out_dev;
	}

	if (pdata && pdata->regulator[0]) {
		ret = mfd_add_devices(chip->dev, 0, &regulator_devs[0],
				      ARRAY_SIZE(regulator_devs),
				      &regulator_resources[0], 0);
		if (ret < 0) {
			dev_err(chip->dev, "Failed to add regulator subdev\n");
			goto out_dev;
		}
	}

	if (pdata && pdata->backlight) {
		ret = mfd_add_devices(chip->dev, 0, &backlight_devs[0],
				      ARRAY_SIZE(backlight_devs),
				      &backlight_resources[0], 0);
		if (ret < 0) {
			dev_err(chip->dev, "Failed to add backlight subdev\n");
			goto out_dev;
		}
	}

	if (pdata && pdata->power) {
		ret = mfd_add_devices(chip->dev, 0, &power_devs[0],
					ARRAY_SIZE(power_devs),
					&power_supply_resources[0], 0);
		if (ret < 0) {
			dev_err(chip->dev, "Failed to add power supply "
				"subdev\n");
			goto out_dev;
		}
	}

	if (pdata && pdata->touch) {
		ret = mfd_add_devices(chip->dev, 0, &touch_devs[0],
				      ARRAY_SIZE(touch_devs),
				      &touch_resources[0], 0);
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

void __devexit max8925_device_exit(struct max8925_chip *chip)
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
