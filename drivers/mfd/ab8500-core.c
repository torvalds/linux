/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 * Author: Srinidhi Kasagar <srinidhi.kasagar@stericsson.com>
 * Author: Rabin Vincent <rabin.vincent@stericsson.com>
 * Changes: Mattias Wallin <mattias.wallin@stericsson.com>
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/ab8500.h>
#include <linux/regulator/ab8500.h>

/*
 * Interrupt register offsets
 * Bank : 0x0E
 */
#define AB8500_IT_SOURCE1_REG		0x00
#define AB8500_IT_SOURCE2_REG		0x01
#define AB8500_IT_SOURCE3_REG		0x02
#define AB8500_IT_SOURCE4_REG		0x03
#define AB8500_IT_SOURCE5_REG		0x04
#define AB8500_IT_SOURCE6_REG		0x05
#define AB8500_IT_SOURCE7_REG		0x06
#define AB8500_IT_SOURCE8_REG		0x07
#define AB8500_IT_SOURCE19_REG		0x12
#define AB8500_IT_SOURCE20_REG		0x13
#define AB8500_IT_SOURCE21_REG		0x14
#define AB8500_IT_SOURCE22_REG		0x15
#define AB8500_IT_SOURCE23_REG		0x16
#define AB8500_IT_SOURCE24_REG		0x17

/*
 * latch registers
 */
#define AB8500_IT_LATCH1_REG		0x20
#define AB8500_IT_LATCH2_REG		0x21
#define AB8500_IT_LATCH3_REG		0x22
#define AB8500_IT_LATCH4_REG		0x23
#define AB8500_IT_LATCH5_REG		0x24
#define AB8500_IT_LATCH6_REG		0x25
#define AB8500_IT_LATCH7_REG		0x26
#define AB8500_IT_LATCH8_REG		0x27
#define AB8500_IT_LATCH9_REG		0x28
#define AB8500_IT_LATCH10_REG		0x29
#define AB8500_IT_LATCH12_REG		0x2B
#define AB8500_IT_LATCH19_REG		0x32
#define AB8500_IT_LATCH20_REG		0x33
#define AB8500_IT_LATCH21_REG		0x34
#define AB8500_IT_LATCH22_REG		0x35
#define AB8500_IT_LATCH23_REG		0x36
#define AB8500_IT_LATCH24_REG		0x37

/*
 * mask registers
 */

#define AB8500_IT_MASK1_REG		0x40
#define AB8500_IT_MASK2_REG		0x41
#define AB8500_IT_MASK3_REG		0x42
#define AB8500_IT_MASK4_REG		0x43
#define AB8500_IT_MASK5_REG		0x44
#define AB8500_IT_MASK6_REG		0x45
#define AB8500_IT_MASK7_REG		0x46
#define AB8500_IT_MASK8_REG		0x47
#define AB8500_IT_MASK9_REG		0x48
#define AB8500_IT_MASK10_REG		0x49
#define AB8500_IT_MASK11_REG		0x4A
#define AB8500_IT_MASK12_REG		0x4B
#define AB8500_IT_MASK13_REG		0x4C
#define AB8500_IT_MASK14_REG		0x4D
#define AB8500_IT_MASK15_REG		0x4E
#define AB8500_IT_MASK16_REG		0x4F
#define AB8500_IT_MASK17_REG		0x50
#define AB8500_IT_MASK18_REG		0x51
#define AB8500_IT_MASK19_REG		0x52
#define AB8500_IT_MASK20_REG		0x53
#define AB8500_IT_MASK21_REG		0x54
#define AB8500_IT_MASK22_REG		0x55
#define AB8500_IT_MASK23_REG		0x56
#define AB8500_IT_MASK24_REG		0x57

#define AB8500_REV_REG			0x80

/*
 * Map interrupt numbers to the LATCH and MASK register offsets, Interrupt
 * numbers are indexed into this array with (num / 8).
 *
 * This is one off from the register names, i.e. AB8500_IT_MASK1_REG is at
 * offset 0.
 */
static const int ab8500_irq_regoffset[AB8500_NUM_IRQ_REGS] = {
	0, 1, 2, 3, 4, 6, 7, 8, 9, 11, 18, 19, 20, 21,
};

static int ab8500_get_chip_id(struct device *dev)
{
	struct ab8500 *ab8500;

	if (!dev)
		return -EINVAL;
	ab8500 = dev_get_drvdata(dev->parent);
	return ab8500 ? (int)ab8500->chip_id : -EINVAL;
}

static int set_register_interruptible(struct ab8500 *ab8500, u8 bank,
	u8 reg, u8 data)
{
	int ret;
	/*
	 * Put the u8 bank and u8 register together into a an u16.
	 * The bank on higher 8 bits and register in lower 8 bits.
	 * */
	u16 addr = ((u16)bank) << 8 | reg;

	dev_vdbg(ab8500->dev, "wr: addr %#x <= %#x\n", addr, data);

	ret = mutex_lock_interruptible(&ab8500->lock);
	if (ret)
		return ret;

	ret = ab8500->write(ab8500, addr, data);
	if (ret < 0)
		dev_err(ab8500->dev, "failed to write reg %#x: %d\n",
			addr, ret);
	mutex_unlock(&ab8500->lock);

	return ret;
}

static int ab8500_set_register(struct device *dev, u8 bank,
	u8 reg, u8 value)
{
	struct ab8500 *ab8500 = dev_get_drvdata(dev->parent);

	return set_register_interruptible(ab8500, bank, reg, value);
}

static int get_register_interruptible(struct ab8500 *ab8500, u8 bank,
	u8 reg, u8 *value)
{
	int ret;
	/* put the u8 bank and u8 reg together into a an u16.
	 * bank on higher 8 bits and reg in lower */
	u16 addr = ((u16)bank) << 8 | reg;

	ret = mutex_lock_interruptible(&ab8500->lock);
	if (ret)
		return ret;

	ret = ab8500->read(ab8500, addr);
	if (ret < 0)
		dev_err(ab8500->dev, "failed to read reg %#x: %d\n",
			addr, ret);
	else
		*value = ret;

	mutex_unlock(&ab8500->lock);
	dev_vdbg(ab8500->dev, "rd: addr %#x => data %#x\n", addr, ret);

	return ret;
}

static int ab8500_get_register(struct device *dev, u8 bank,
	u8 reg, u8 *value)
{
	struct ab8500 *ab8500 = dev_get_drvdata(dev->parent);

	return get_register_interruptible(ab8500, bank, reg, value);
}

static int mask_and_set_register_interruptible(struct ab8500 *ab8500, u8 bank,
	u8 reg, u8 bitmask, u8 bitvalues)
{
	int ret;
	u8 data;
	/* put the u8 bank and u8 reg together into a an u16.
	 * bank on higher 8 bits and reg in lower */
	u16 addr = ((u16)bank) << 8 | reg;

	ret = mutex_lock_interruptible(&ab8500->lock);
	if (ret)
		return ret;

	ret = ab8500->read(ab8500, addr);
	if (ret < 0) {
		dev_err(ab8500->dev, "failed to read reg %#x: %d\n",
			addr, ret);
		goto out;
	}

	data = (u8)ret;
	data = (~bitmask & data) | (bitmask & bitvalues);

	ret = ab8500->write(ab8500, addr, data);
	if (ret < 0)
		dev_err(ab8500->dev, "failed to write reg %#x: %d\n",
			addr, ret);

	dev_vdbg(ab8500->dev, "mask: addr %#x => data %#x\n", addr, data);
out:
	mutex_unlock(&ab8500->lock);
	return ret;
}

static int ab8500_mask_and_set_register(struct device *dev,
	u8 bank, u8 reg, u8 bitmask, u8 bitvalues)
{
	struct ab8500 *ab8500 = dev_get_drvdata(dev->parent);

	return mask_and_set_register_interruptible(ab8500, bank, reg,
		bitmask, bitvalues);

}

static struct abx500_ops ab8500_ops = {
	.get_chip_id = ab8500_get_chip_id,
	.get_register = ab8500_get_register,
	.set_register = ab8500_set_register,
	.get_register_page = NULL,
	.set_register_page = NULL,
	.mask_and_set_register = ab8500_mask_and_set_register,
	.event_registers_startup_state_get = NULL,
	.startup_irq_enabled = NULL,
};

static void ab8500_irq_lock(struct irq_data *data)
{
	struct ab8500 *ab8500 = irq_data_get_irq_chip_data(data);

	mutex_lock(&ab8500->irq_lock);
}

static void ab8500_irq_sync_unlock(struct irq_data *data)
{
	struct ab8500 *ab8500 = irq_data_get_irq_chip_data(data);
	int i;

	for (i = 0; i < AB8500_NUM_IRQ_REGS; i++) {
		u8 old = ab8500->oldmask[i];
		u8 new = ab8500->mask[i];
		int reg;

		if (new == old)
			continue;

		/* Interrupt register 12 does'nt exist prior to version 0x20 */
		if (ab8500_irq_regoffset[i] == 11 && ab8500->chip_id < 0x20)
			continue;

		ab8500->oldmask[i] = new;

		reg = AB8500_IT_MASK1_REG + ab8500_irq_regoffset[i];
		set_register_interruptible(ab8500, AB8500_INTERRUPT, reg, new);
	}

	mutex_unlock(&ab8500->irq_lock);
}

static void ab8500_irq_mask(struct irq_data *data)
{
	struct ab8500 *ab8500 = irq_data_get_irq_chip_data(data);
	int offset = data->irq - ab8500->irq_base;
	int index = offset / 8;
	int mask = 1 << (offset % 8);

	ab8500->mask[index] |= mask;
}

static void ab8500_irq_unmask(struct irq_data *data)
{
	struct ab8500 *ab8500 = irq_data_get_irq_chip_data(data);
	int offset = data->irq - ab8500->irq_base;
	int index = offset / 8;
	int mask = 1 << (offset % 8);

	ab8500->mask[index] &= ~mask;
}

static struct irq_chip ab8500_irq_chip = {
	.name			= "ab8500",
	.irq_bus_lock		= ab8500_irq_lock,
	.irq_bus_sync_unlock	= ab8500_irq_sync_unlock,
	.irq_mask		= ab8500_irq_mask,
	.irq_unmask		= ab8500_irq_unmask,
};

static irqreturn_t ab8500_irq(int irq, void *dev)
{
	struct ab8500 *ab8500 = dev;
	int i;

	dev_vdbg(ab8500->dev, "interrupt\n");

	for (i = 0; i < AB8500_NUM_IRQ_REGS; i++) {
		int regoffset = ab8500_irq_regoffset[i];
		int status;
		u8 value;

		/* Interrupt register 12 does'nt exist prior to version 0x20 */
		if (regoffset == 11 && ab8500->chip_id < 0x20)
			continue;

		status = get_register_interruptible(ab8500, AB8500_INTERRUPT,
			AB8500_IT_LATCH1_REG + regoffset, &value);
		if (status < 0 || value == 0)
			continue;

		do {
			int bit = __ffs(value);
			int line = i * 8 + bit;

			handle_nested_irq(ab8500->irq_base + line);
			value &= ~(1 << bit);
		} while (value);
	}

	return IRQ_HANDLED;
}

static int ab8500_irq_init(struct ab8500 *ab8500)
{
	int base = ab8500->irq_base;
	int irq;

	for (irq = base; irq < base + AB8500_NR_IRQS; irq++) {
		set_irq_chip_data(irq, ab8500);
		set_irq_chip_and_handler(irq, &ab8500_irq_chip,
					 handle_simple_irq);
		set_irq_nested_thread(irq, 1);
#ifdef CONFIG_ARM
		set_irq_flags(irq, IRQF_VALID);
#else
		set_irq_noprobe(irq);
#endif
	}

	return 0;
}

static void ab8500_irq_remove(struct ab8500 *ab8500)
{
	int base = ab8500->irq_base;
	int irq;

	for (irq = base; irq < base + AB8500_NR_IRQS; irq++) {
#ifdef CONFIG_ARM
		set_irq_flags(irq, 0);
#endif
		set_irq_chip_and_handler(irq, NULL, NULL);
		set_irq_chip_data(irq, NULL);
	}
}

static struct resource ab8500_gpadc_resources[] = {
	{
		.name	= "HW_CONV_END",
		.start	= AB8500_INT_GP_HW_ADC_CONV_END,
		.end	= AB8500_INT_GP_HW_ADC_CONV_END,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "SW_CONV_END",
		.start	= AB8500_INT_GP_SW_ADC_CONV_END,
		.end	= AB8500_INT_GP_SW_ADC_CONV_END,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource ab8500_rtc_resources[] = {
	{
		.name	= "60S",
		.start	= AB8500_INT_RTC_60S,
		.end	= AB8500_INT_RTC_60S,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "ALARM",
		.start	= AB8500_INT_RTC_ALARM,
		.end	= AB8500_INT_RTC_ALARM,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource ab8500_poweronkey_db_resources[] = {
	{
		.name	= "ONKEY_DBF",
		.start	= AB8500_INT_PON_KEY1DB_F,
		.end	= AB8500_INT_PON_KEY1DB_F,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "ONKEY_DBR",
		.start	= AB8500_INT_PON_KEY1DB_R,
		.end	= AB8500_INT_PON_KEY1DB_R,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource ab8500_bm_resources[] = {
	{
		.name = "MAIN_EXT_CH_NOT_OK",
		.start = AB8500_INT_MAIN_EXT_CH_NOT_OK,
		.end = AB8500_INT_MAIN_EXT_CH_NOT_OK,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "BATT_OVV",
		.start = AB8500_INT_BATT_OVV,
		.end = AB8500_INT_BATT_OVV,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "MAIN_CH_UNPLUG_DET",
		.start = AB8500_INT_MAIN_CH_UNPLUG_DET,
		.end = AB8500_INT_MAIN_CH_UNPLUG_DET,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "MAIN_CHARGE_PLUG_DET",
		.start = AB8500_INT_MAIN_CH_PLUG_DET,
		.end = AB8500_INT_MAIN_CH_PLUG_DET,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "VBUS_DET_F",
		.start = AB8500_INT_VBUS_DET_F,
		.end = AB8500_INT_VBUS_DET_F,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "VBUS_DET_R",
		.start = AB8500_INT_VBUS_DET_R,
		.end = AB8500_INT_VBUS_DET_R,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "BAT_CTRL_INDB",
		.start = AB8500_INT_BAT_CTRL_INDB,
		.end = AB8500_INT_BAT_CTRL_INDB,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "CH_WD_EXP",
		.start = AB8500_INT_CH_WD_EXP,
		.end = AB8500_INT_CH_WD_EXP,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "VBUS_OVV",
		.start = AB8500_INT_VBUS_OVV,
		.end = AB8500_INT_VBUS_OVV,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "NCONV_ACCU",
		.start = AB8500_INT_CCN_CONV_ACC,
		.end = AB8500_INT_CCN_CONV_ACC,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "LOW_BAT_F",
		.start = AB8500_INT_LOW_BAT_F,
		.end = AB8500_INT_LOW_BAT_F,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "LOW_BAT_R",
		.start = AB8500_INT_LOW_BAT_R,
		.end = AB8500_INT_LOW_BAT_R,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "BTEMP_LOW",
		.start = AB8500_INT_BTEMP_LOW,
		.end = AB8500_INT_BTEMP_LOW,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "BTEMP_HIGH",
		.start = AB8500_INT_BTEMP_HIGH,
		.end = AB8500_INT_BTEMP_HIGH,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "USB_CHARGER_NOT_OKR",
		.start = AB8500_INT_USB_CHARGER_NOT_OK,
		.end = AB8500_INT_USB_CHARGER_NOT_OK,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "USB_CHARGE_DET_DONE",
		.start = AB8500_INT_USB_CHG_DET_DONE,
		.end = AB8500_INT_USB_CHG_DET_DONE,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "USB_CH_TH_PROT_R",
		.start = AB8500_INT_USB_CH_TH_PROT_R,
		.end = AB8500_INT_USB_CH_TH_PROT_R,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "MAIN_CH_TH_PROT_R",
		.start = AB8500_INT_MAIN_CH_TH_PROT_R,
		.end = AB8500_INT_MAIN_CH_TH_PROT_R,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "USB_CHARGER_NOT_OKF",
		.start = AB8500_INT_USB_CHARGER_NOT_OKF,
		.end = AB8500_INT_USB_CHARGER_NOT_OKF,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource ab8500_debug_resources[] = {
	{
		.name	= "IRQ_FIRST",
		.start	= AB8500_INT_MAIN_EXT_CH_NOT_OK,
		.end	= AB8500_INT_MAIN_EXT_CH_NOT_OK,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "IRQ_LAST",
		.start	= AB8500_INT_USB_CHARGER_NOT_OKF,
		.end	= AB8500_INT_USB_CHARGER_NOT_OKF,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource ab8500_usb_resources[] = {
	{
		.name = "ID_WAKEUP_R",
		.start = AB8500_INT_ID_WAKEUP_R,
		.end = AB8500_INT_ID_WAKEUP_R,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "ID_WAKEUP_F",
		.start = AB8500_INT_ID_WAKEUP_F,
		.end = AB8500_INT_ID_WAKEUP_F,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "VBUS_DET_F",
		.start = AB8500_INT_VBUS_DET_F,
		.end = AB8500_INT_VBUS_DET_F,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "VBUS_DET_R",
		.start = AB8500_INT_VBUS_DET_R,
		.end = AB8500_INT_VBUS_DET_R,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "USB_LINK_STATUS",
		.start = AB8500_INT_USB_LINK_STATUS,
		.end = AB8500_INT_USB_LINK_STATUS,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource ab8500_temp_resources[] = {
	{
		.name  = "AB8500_TEMP_WARM",
		.start = AB8500_INT_TEMP_WARM,
		.end   = AB8500_INT_TEMP_WARM,
		.flags = IORESOURCE_IRQ,
	},
};

static struct mfd_cell ab8500_devs[] = {
#ifdef CONFIG_DEBUG_FS
	{
		.name = "ab8500-debug",
		.num_resources = ARRAY_SIZE(ab8500_debug_resources),
		.resources = ab8500_debug_resources,
	},
#endif
	{
		.name = "ab8500-sysctrl",
	},
	{
		.name = "ab8500-regulator",
	},
	{
		.name = "ab8500-gpadc",
		.num_resources = ARRAY_SIZE(ab8500_gpadc_resources),
		.resources = ab8500_gpadc_resources,
	},
	{
		.name = "ab8500-rtc",
		.num_resources = ARRAY_SIZE(ab8500_rtc_resources),
		.resources = ab8500_rtc_resources,
	},
	{
		.name = "ab8500-bm",
		.num_resources = ARRAY_SIZE(ab8500_bm_resources),
		.resources = ab8500_bm_resources,
	},
	{ .name = "ab8500-codec", },
	{
		.name = "ab8500-usb",
		.num_resources = ARRAY_SIZE(ab8500_usb_resources),
		.resources = ab8500_usb_resources,
	},
	{
		.name = "ab8500-poweron-key",
		.num_resources = ARRAY_SIZE(ab8500_poweronkey_db_resources),
		.resources = ab8500_poweronkey_db_resources,
	},
	{
		.name = "ab8500-pwm",
		.id = 1,
	},
	{
		.name = "ab8500-pwm",
		.id = 2,
	},
	{
		.name = "ab8500-pwm",
		.id = 3,
	},
	{ .name = "ab8500-leds", },
	{
		.name = "ab8500-denc",
	},
	{
		.name = "ab8500-temp",
		.num_resources = ARRAY_SIZE(ab8500_temp_resources),
		.resources = ab8500_temp_resources,
	},
};

static ssize_t show_chip_id(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct ab8500 *ab8500;

	ab8500 = dev_get_drvdata(dev);
	return sprintf(buf, "%#x\n", ab8500 ? ab8500->chip_id : -EINVAL);
}

static DEVICE_ATTR(chip_id, S_IRUGO, show_chip_id, NULL);

static struct attribute *ab8500_sysfs_entries[] = {
	&dev_attr_chip_id.attr,
	NULL,
};

static struct attribute_group ab8500_attr_group = {
	.attrs	= ab8500_sysfs_entries,
};

int __devinit ab8500_init(struct ab8500 *ab8500)
{
	struct ab8500_platform_data *plat = dev_get_platdata(ab8500->dev);
	int ret;
	int i;
	u8 value;

	if (plat)
		ab8500->irq_base = plat->irq_base;

	mutex_init(&ab8500->lock);
	mutex_init(&ab8500->irq_lock);

	ret = get_register_interruptible(ab8500, AB8500_MISC,
		AB8500_REV_REG, &value);
	if (ret < 0)
		return ret;

	/*
	 * 0x0 - Early Drop
	 * 0x10 - Cut 1.0
	 * 0x11 - Cut 1.1
	 * 0x20 - Cut 2.0
	 */
	if (value == 0x0 || value == 0x10 || value == 0x11 || value == 0x20) {
		ab8500->revision = value;
		dev_info(ab8500->dev, "detected chip, revision: %#x\n", value);
	} else {
		dev_err(ab8500->dev, "unknown chip, revision: %#x\n", value);
		return -EINVAL;
	}
	ab8500->chip_id = value;

	if (plat && plat->init)
		plat->init(ab8500);

	/* Clear and mask all interrupts */
	for (i = 0; i < AB8500_NUM_IRQ_REGS; i++) {
		/* Interrupt register 12 does'nt exist prior to version 0x20 */
		if (ab8500_irq_regoffset[i] == 11 && ab8500->chip_id < 0x20)
			continue;

		get_register_interruptible(ab8500, AB8500_INTERRUPT,
			AB8500_IT_LATCH1_REG + ab8500_irq_regoffset[i],
			&value);
		set_register_interruptible(ab8500, AB8500_INTERRUPT,
			AB8500_IT_MASK1_REG + ab8500_irq_regoffset[i], 0xff);
	}

	ret = abx500_register_ops(ab8500->dev, &ab8500_ops);
	if (ret)
		return ret;

	for (i = 0; i < AB8500_NUM_IRQ_REGS; i++)
		ab8500->mask[i] = ab8500->oldmask[i] = 0xff;

	if (ab8500->irq_base) {
		ret = ab8500_irq_init(ab8500);
		if (ret)
			return ret;

		ret = request_threaded_irq(ab8500->irq, NULL, ab8500_irq,
					   IRQF_ONESHOT | IRQF_NO_SUSPEND,
					   "ab8500", ab8500);
		if (ret)
			goto out_removeirq;
	}

	ret = mfd_add_devices(ab8500->dev, 0, ab8500_devs,
			      ARRAY_SIZE(ab8500_devs), NULL,
			      ab8500->irq_base);
	if (ret)
		goto out_freeirq;

	ret = sysfs_create_group(&ab8500->dev->kobj, &ab8500_attr_group);
	if (ret)
		dev_err(ab8500->dev, "error creating sysfs entries\n");

	return ret;

out_freeirq:
	if (ab8500->irq_base) {
		free_irq(ab8500->irq, ab8500);
out_removeirq:
		ab8500_irq_remove(ab8500);
	}
	return ret;
}

int __devexit ab8500_exit(struct ab8500 *ab8500)
{
	sysfs_remove_group(&ab8500->dev->kobj, &ab8500_attr_group);
	mfd_remove_devices(ab8500->dev);
	if (ab8500->irq_base) {
		free_irq(ab8500->irq, ab8500);
		ab8500_irq_remove(ab8500);
	}

	return 0;
}

MODULE_AUTHOR("Srinidhi Kasagar, Rabin Vincent");
MODULE_DESCRIPTION("AB8500 MFD core");
MODULE_LICENSE("GPL v2");
