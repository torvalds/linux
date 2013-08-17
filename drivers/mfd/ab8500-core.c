/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 * Author: Srinidhi Kasagar <srinidhi.kasagar@stericsson.com>
 * Author: Rabin Vincent <rabin.vincent@stericsson.com>
 * Author: Mattias Wallin <mattias.wallin@stericsson.com>
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
#include <linux/mfd/abx500/ab8500.h>
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
#define AB9540_IT_SOURCE13_REG		0x0C
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
#define AB9540_IT_LATCH13_REG		0x2C
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
#define AB8500_IC_NAME_REG		0x82
#define AB8500_SWITCH_OFF_STATUS	0x00

#define AB8500_TURN_ON_STATUS		0x00

#define AB9540_MODEM_CTRL2_REG			0x23
#define AB9540_MODEM_CTRL2_SWDBBRSTN_BIT	BIT(2)

/*
 * Map interrupt numbers to the LATCH and MASK register offsets, Interrupt
 * numbers are indexed into this array with (num / 8). The interupts are
 * defined in linux/mfd/ab8500.h
 *
 * This is one off from the register names, i.e. AB8500_IT_MASK1_REG is at
 * offset 0.
 */
/* AB8500 support */
static const int ab8500_irq_regoffset[AB8500_NUM_IRQ_REGS] = {
	0, 1, 2, 3, 4, 6, 7, 8, 9, 11, 18, 19, 20, 21,
};

/* AB9540 support */
static const int ab9540_irq_regoffset[AB9540_NUM_IRQ_REGS] = {
	0, 1, 2, 3, 4, 6, 7, 8, 9, 11, 18, 19, 20, 21, 12, 13, 24,
};

static const char ab8500_version_str[][7] = {
	[AB8500_VERSION_AB8500] = "AB8500",
	[AB8500_VERSION_AB8505] = "AB8505",
	[AB8500_VERSION_AB9540] = "AB9540",
	[AB8500_VERSION_AB8540] = "AB8540",
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

	mutex_lock(&ab8500->lock);

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

	mutex_lock(&ab8500->lock);

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
	/* put the u8 bank and u8 reg together into a an u16.
	 * bank on higher 8 bits and reg in lower */
	u16 addr = ((u16)bank) << 8 | reg;

	mutex_lock(&ab8500->lock);

	if (ab8500->write_masked == NULL) {
		u8 data;

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

		dev_vdbg(ab8500->dev, "mask: addr %#x => data %#x\n", addr,
			data);
		goto out;
	}
	ret = ab8500->write_masked(ab8500, addr, bitmask, bitvalues);
	if (ret < 0)
		dev_err(ab8500->dev, "failed to modify reg %#x: %d\n", addr,
			ret);
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

	for (i = 0; i < ab8500->mask_size; i++) {
		u8 old = ab8500->oldmask[i];
		u8 new = ab8500->mask[i];
		int reg;

		if (new == old)
			continue;

		/*
		 * Interrupt register 12 doesn't exist prior to AB8500 version
		 * 2.0
		 */
		if (ab8500->irq_reg_offset[i] == 11 &&
			is_ab8500_1p1_or_earlier(ab8500))
			continue;

		ab8500->oldmask[i] = new;

		reg = AB8500_IT_MASK1_REG + ab8500->irq_reg_offset[i];
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
	.irq_disable		= ab8500_irq_mask,
	.irq_unmask		= ab8500_irq_unmask,
};

static irqreturn_t ab8500_irq(int irq, void *dev)
{
	struct ab8500 *ab8500 = dev;
	int i;

	dev_vdbg(ab8500->dev, "interrupt\n");

	for (i = 0; i < ab8500->mask_size; i++) {
		int regoffset = ab8500->irq_reg_offset[i];
		int status;
		u8 value;

		/*
		 * Interrupt register 12 doesn't exist prior to AB8500 version
		 * 2.0
		 */
		if (regoffset == 11 && is_ab8500_1p1_or_earlier(ab8500))
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
	int num_irqs;

	if (is_ab9540(ab8500))
		num_irqs = AB9540_NR_IRQS;
	else if (is_ab8505(ab8500))
		num_irqs = AB8505_NR_IRQS;
	else
		num_irqs = AB8500_NR_IRQS;

	for (irq = base; irq < base + num_irqs; irq++) {
		irq_set_chip_data(irq, ab8500);
		irq_set_chip_and_handler(irq, &ab8500_irq_chip,
					 handle_simple_irq);
		irq_set_nested_thread(irq, 1);
#ifdef CONFIG_ARM
		set_irq_flags(irq, IRQF_VALID);
#else
		irq_set_noprobe(irq);
#endif
	}

	return 0;
}

static void ab8500_irq_remove(struct ab8500 *ab8500)
{
	int base = ab8500->irq_base;
	int irq;
	int num_irqs;

	if (is_ab9540(ab8500))
		num_irqs = AB9540_NR_IRQS;
	else if (is_ab8505(ab8500))
		num_irqs = AB8505_NR_IRQS;
	else
		num_irqs = AB8500_NR_IRQS;

	for (irq = base; irq < base + num_irqs; irq++) {
#ifdef CONFIG_ARM
		set_irq_flags(irq, 0);
#endif
		irq_set_chip_and_handler(irq, NULL, NULL);
		irq_set_chip_data(irq, NULL);
	}
}

/* AB8500 GPIO Resources */
static struct resource __devinitdata ab8500_gpio_resources[] = {
	{
		.name	= "GPIO_INT6",
		.start	= AB8500_INT_GPIO6R,
		.end	= AB8500_INT_GPIO41F,
		.flags	= IORESOURCE_IRQ,
	}
};

/* AB9540 GPIO Resources */
static struct resource __devinitdata ab9540_gpio_resources[] = {
	{
		.name	= "GPIO_INT6",
		.start	= AB8500_INT_GPIO6R,
		.end	= AB8500_INT_GPIO41F,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "GPIO_INT14",
		.start	= AB9540_INT_GPIO50R,
		.end	= AB9540_INT_GPIO54R,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "GPIO_INT15",
		.start	= AB9540_INT_GPIO50F,
		.end	= AB9540_INT_GPIO54F,
		.flags	= IORESOURCE_IRQ,
	}
};

static struct resource __devinitdata ab8500_gpadc_resources[] = {
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

static struct resource __devinitdata ab8500_rtc_resources[] = {
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

static struct resource __devinitdata ab8500_poweronkey_db_resources[] = {
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

static struct resource __devinitdata ab8500_av_acc_detect_resources[] = {
	{
	       .name = "ACC_DETECT_1DB_F",
	       .start = AB8500_INT_ACC_DETECT_1DB_F,
	       .end = AB8500_INT_ACC_DETECT_1DB_F,
	       .flags = IORESOURCE_IRQ,
	},
	{
	       .name = "ACC_DETECT_1DB_R",
	       .start = AB8500_INT_ACC_DETECT_1DB_R,
	       .end = AB8500_INT_ACC_DETECT_1DB_R,
	       .flags = IORESOURCE_IRQ,
	},
	{
	       .name = "ACC_DETECT_21DB_F",
	       .start = AB8500_INT_ACC_DETECT_21DB_F,
	       .end = AB8500_INT_ACC_DETECT_21DB_F,
	       .flags = IORESOURCE_IRQ,
	},
	{
	       .name = "ACC_DETECT_21DB_R",
	       .start = AB8500_INT_ACC_DETECT_21DB_R,
	       .end = AB8500_INT_ACC_DETECT_21DB_R,
	       .flags = IORESOURCE_IRQ,
	},
	{
	       .name = "ACC_DETECT_22DB_F",
	       .start = AB8500_INT_ACC_DETECT_22DB_F,
	       .end = AB8500_INT_ACC_DETECT_22DB_F,
	       .flags = IORESOURCE_IRQ,
	},
	{
	       .name = "ACC_DETECT_22DB_R",
	       .start = AB8500_INT_ACC_DETECT_22DB_R,
	       .end = AB8500_INT_ACC_DETECT_22DB_R,
	       .flags = IORESOURCE_IRQ,
	},
};

static struct resource __devinitdata ab8500_charger_resources[] = {
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
		.name = "VBUS_DET_R",
		.start = AB8500_INT_VBUS_DET_R,
		.end = AB8500_INT_VBUS_DET_R,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "VBUS_DET_F",
		.start = AB8500_INT_VBUS_DET_F,
		.end = AB8500_INT_VBUS_DET_F,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "USB_LINK_STATUS",
		.start = AB8500_INT_USB_LINK_STATUS,
		.end = AB8500_INT_USB_LINK_STATUS,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "VBUS_OVV",
		.start = AB8500_INT_VBUS_OVV,
		.end = AB8500_INT_VBUS_OVV,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "USB_CH_TH_PROT_R",
		.start = AB8500_INT_USB_CH_TH_PROT_R,
		.end = AB8500_INT_USB_CH_TH_PROT_R,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "USB_CH_TH_PROT_F",
		.start = AB8500_INT_USB_CH_TH_PROT_F,
		.end = AB8500_INT_USB_CH_TH_PROT_F,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "MAIN_EXT_CH_NOT_OK",
		.start = AB8500_INT_MAIN_EXT_CH_NOT_OK,
		.end = AB8500_INT_MAIN_EXT_CH_NOT_OK,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "MAIN_CH_TH_PROT_R",
		.start = AB8500_INT_MAIN_CH_TH_PROT_R,
		.end = AB8500_INT_MAIN_CH_TH_PROT_R,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "MAIN_CH_TH_PROT_F",
		.start = AB8500_INT_MAIN_CH_TH_PROT_F,
		.end = AB8500_INT_MAIN_CH_TH_PROT_F,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "USB_CHARGER_NOT_OKR",
		.start = AB8500_INT_USB_CHARGER_NOT_OKR,
		.end = AB8500_INT_USB_CHARGER_NOT_OKR,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "CH_WD_EXP",
		.start = AB8500_INT_CH_WD_EXP,
		.end = AB8500_INT_CH_WD_EXP,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource __devinitdata ab8500_btemp_resources[] = {
	{
		.name = "BAT_CTRL_INDB",
		.start = AB8500_INT_BAT_CTRL_INDB,
		.end = AB8500_INT_BAT_CTRL_INDB,
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
		.name = "BTEMP_LOW_MEDIUM",
		.start = AB8500_INT_BTEMP_LOW_MEDIUM,
		.end = AB8500_INT_BTEMP_LOW_MEDIUM,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "BTEMP_MEDIUM_HIGH",
		.start = AB8500_INT_BTEMP_MEDIUM_HIGH,
		.end = AB8500_INT_BTEMP_MEDIUM_HIGH,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource __devinitdata ab8500_fg_resources[] = {
	{
		.name = "NCONV_ACCU",
		.start = AB8500_INT_CCN_CONV_ACC,
		.end = AB8500_INT_CCN_CONV_ACC,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "BATT_OVV",
		.start = AB8500_INT_BATT_OVV,
		.end = AB8500_INT_BATT_OVV,
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
		.name = "CC_INT_CALIB",
		.start = AB8500_INT_CC_INT_CALIB,
		.end = AB8500_INT_CC_INT_CALIB,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "CCEOC",
		.start = AB8500_INT_CCEOC,
		.end = AB8500_INT_CCEOC,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource __devinitdata ab8500_chargalg_resources[] = {};

#ifdef CONFIG_DEBUG_FS
static struct resource __devinitdata ab8500_debug_resources[] = {
	{
		.name	= "IRQ_FIRST",
		.start	= AB8500_INT_MAIN_EXT_CH_NOT_OK,
		.end	= AB8500_INT_MAIN_EXT_CH_NOT_OK,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "IRQ_LAST",
		.start	= AB8500_INT_XTAL32K_KO,
		.end	= AB8500_INT_XTAL32K_KO,
		.flags	= IORESOURCE_IRQ,
	},
};
#endif

static struct resource __devinitdata ab8500_usb_resources[] = {
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
	{
		.name = "USB_ADP_PROBE_PLUG",
		.start = AB8500_INT_ADP_PROBE_PLUG,
		.end = AB8500_INT_ADP_PROBE_PLUG,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "USB_ADP_PROBE_UNPLUG",
		.start = AB8500_INT_ADP_PROBE_UNPLUG,
		.end = AB8500_INT_ADP_PROBE_UNPLUG,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource __devinitdata ab8500_temp_resources[] = {
	{
		.name  = "AB8500_TEMP_WARM",
		.start = AB8500_INT_TEMP_WARM,
		.end   = AB8500_INT_TEMP_WARM,
		.flags = IORESOURCE_IRQ,
	},
};

static struct mfd_cell __devinitdata abx500_common_devs[] = {
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
		.name = "ab8500-charger",
		.num_resources = ARRAY_SIZE(ab8500_charger_resources),
		.resources = ab8500_charger_resources,
	},
	{
		.name = "ab8500-btemp",
		.num_resources = ARRAY_SIZE(ab8500_btemp_resources),
		.resources = ab8500_btemp_resources,
	},
	{
		.name = "ab8500-fg",
		.num_resources = ARRAY_SIZE(ab8500_fg_resources),
		.resources = ab8500_fg_resources,
	},
	{
		.name = "ab8500-chargalg",
		.num_resources = ARRAY_SIZE(ab8500_chargalg_resources),
		.resources = ab8500_chargalg_resources,
	},
	{
		.name = "ab8500-acc-det",
		.num_resources = ARRAY_SIZE(ab8500_av_acc_detect_resources),
		.resources = ab8500_av_acc_detect_resources,
	},
	{
		.name = "ab8500-codec",
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

static struct mfd_cell __devinitdata ab8500_devs[] = {
	{
		.name = "ab8500-gpio",
		.num_resources = ARRAY_SIZE(ab8500_gpio_resources),
		.resources = ab8500_gpio_resources,
	},
	{
		.name = "ab8500-usb",
		.num_resources = ARRAY_SIZE(ab8500_usb_resources),
		.resources = ab8500_usb_resources,
	},
};

static struct mfd_cell __devinitdata ab9540_devs[] = {
	{
		.name = "ab8500-gpio",
		.num_resources = ARRAY_SIZE(ab9540_gpio_resources),
		.resources = ab9540_gpio_resources,
	},
	{
		.name = "ab9540-usb",
		.num_resources = ARRAY_SIZE(ab8500_usb_resources),
		.resources = ab8500_usb_resources,
	},
};

static ssize_t show_chip_id(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct ab8500 *ab8500;

	ab8500 = dev_get_drvdata(dev);
	return sprintf(buf, "%#x\n", ab8500 ? ab8500->chip_id : -EINVAL);
}

/*
 * ab8500 has switched off due to (SWITCH_OFF_STATUS):
 * 0x01 Swoff bit programming
 * 0x02 Thermal protection activation
 * 0x04 Vbat lower then BattOk falling threshold
 * 0x08 Watchdog expired
 * 0x10 Non presence of 32kHz clock
 * 0x20 Battery level lower than power on reset threshold
 * 0x40 Power on key 1 pressed longer than 10 seconds
 * 0x80 DB8500 thermal shutdown
 */
static ssize_t show_switch_off_status(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret;
	u8 value;
	struct ab8500 *ab8500;

	ab8500 = dev_get_drvdata(dev);
	ret = get_register_interruptible(ab8500, AB8500_RTC,
		AB8500_SWITCH_OFF_STATUS, &value);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%#x\n", value);
}

/*
 * ab8500 has turned on due to (TURN_ON_STATUS):
 * 0x01 PORnVbat
 * 0x02 PonKey1dbF
 * 0x04 PonKey2dbF
 * 0x08 RTCAlarm
 * 0x10 MainChDet
 * 0x20 VbusDet
 * 0x40 UsbIDDetect
 * 0x80 Reserved
 */
static ssize_t show_turn_on_status(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret;
	u8 value;
	struct ab8500 *ab8500;

	ab8500 = dev_get_drvdata(dev);
	ret = get_register_interruptible(ab8500, AB8500_SYS_CTRL1_BLOCK,
		AB8500_TURN_ON_STATUS, &value);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%#x\n", value);
}

static ssize_t show_ab9540_dbbrstn(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct ab8500 *ab8500;
	int ret;
	u8 value;

	ab8500 = dev_get_drvdata(dev);

	ret = get_register_interruptible(ab8500, AB8500_REGU_CTRL2,
		AB9540_MODEM_CTRL2_REG, &value);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n",
			(value & AB9540_MODEM_CTRL2_SWDBBRSTN_BIT) ? 1 : 0);
}

static ssize_t store_ab9540_dbbrstn(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct ab8500 *ab8500;
	int ret = count;
	int err;
	u8 bitvalues;

	ab8500 = dev_get_drvdata(dev);

	if (count > 0) {
		switch (buf[0]) {
		case '0':
			bitvalues = 0;
			break;
		case '1':
			bitvalues = AB9540_MODEM_CTRL2_SWDBBRSTN_BIT;
			break;
		default:
			goto exit;
		}

		err = mask_and_set_register_interruptible(ab8500,
			AB8500_REGU_CTRL2, AB9540_MODEM_CTRL2_REG,
			AB9540_MODEM_CTRL2_SWDBBRSTN_BIT, bitvalues);
		if (err)
			dev_info(ab8500->dev,
				"Failed to set DBBRSTN %c, err %#x\n",
				buf[0], err);
	}

exit:
	return ret;
}

static DEVICE_ATTR(chip_id, S_IRUGO, show_chip_id, NULL);
static DEVICE_ATTR(switch_off_status, S_IRUGO, show_switch_off_status, NULL);
static DEVICE_ATTR(turn_on_status, S_IRUGO, show_turn_on_status, NULL);
static DEVICE_ATTR(dbbrstn, S_IRUGO | S_IWUSR,
			show_ab9540_dbbrstn, store_ab9540_dbbrstn);

static struct attribute *ab8500_sysfs_entries[] = {
	&dev_attr_chip_id.attr,
	&dev_attr_switch_off_status.attr,
	&dev_attr_turn_on_status.attr,
	NULL,
};

static struct attribute *ab9540_sysfs_entries[] = {
	&dev_attr_chip_id.attr,
	&dev_attr_switch_off_status.attr,
	&dev_attr_turn_on_status.attr,
	&dev_attr_dbbrstn.attr,
	NULL,
};

static struct attribute_group ab8500_attr_group = {
	.attrs	= ab8500_sysfs_entries,
};

static struct attribute_group ab9540_attr_group = {
	.attrs	= ab9540_sysfs_entries,
};

int __devinit ab8500_init(struct ab8500 *ab8500, enum ab8500_version version)
{
	struct ab8500_platform_data *plat = dev_get_platdata(ab8500->dev);
	int ret;
	int i;
	u8 value;

	if (plat)
		ab8500->irq_base = plat->irq_base;

	mutex_init(&ab8500->lock);
	mutex_init(&ab8500->irq_lock);

	if (version != AB8500_VERSION_UNDEFINED)
		ab8500->version = version;
	else {
		ret = get_register_interruptible(ab8500, AB8500_MISC,
			AB8500_IC_NAME_REG, &value);
		if (ret < 0)
			return ret;

		ab8500->version = value;
	}

	ret = get_register_interruptible(ab8500, AB8500_MISC,
		AB8500_REV_REG, &value);
	if (ret < 0)
		return ret;

	ab8500->chip_id = value;

	dev_info(ab8500->dev, "detected chip, %s rev. %1x.%1x\n",
			ab8500_version_str[ab8500->version],
			ab8500->chip_id >> 4,
			ab8500->chip_id & 0x0F);

	/* Configure AB8500 or AB9540 IRQ */
	if (is_ab9540(ab8500) || is_ab8505(ab8500)) {
		ab8500->mask_size = AB9540_NUM_IRQ_REGS;
		ab8500->irq_reg_offset = ab9540_irq_regoffset;
	} else {
		ab8500->mask_size = AB8500_NUM_IRQ_REGS;
		ab8500->irq_reg_offset = ab8500_irq_regoffset;
	}
	ab8500->mask = kzalloc(ab8500->mask_size, GFP_KERNEL);
	if (!ab8500->mask)
		return -ENOMEM;
	ab8500->oldmask = kzalloc(ab8500->mask_size, GFP_KERNEL);
	if (!ab8500->oldmask) {
		ret = -ENOMEM;
		goto out_freemask;
	}
	/*
	 * ab8500 has switched off due to (SWITCH_OFF_STATUS):
	 * 0x01 Swoff bit programming
	 * 0x02 Thermal protection activation
	 * 0x04 Vbat lower then BattOk falling threshold
	 * 0x08 Watchdog expired
	 * 0x10 Non presence of 32kHz clock
	 * 0x20 Battery level lower than power on reset threshold
	 * 0x40 Power on key 1 pressed longer than 10 seconds
	 * 0x80 DB8500 thermal shutdown
	 */

	ret = get_register_interruptible(ab8500, AB8500_RTC,
		AB8500_SWITCH_OFF_STATUS, &value);
	if (ret < 0)
		return ret;
	dev_info(ab8500->dev, "switch off status: %#x", value);

	if (plat && plat->init)
		plat->init(ab8500);

	/* Clear and mask all interrupts */
	for (i = 0; i < ab8500->mask_size; i++) {
		/*
		 * Interrupt register 12 doesn't exist prior to AB8500 version
		 * 2.0
		 */
		if (ab8500->irq_reg_offset[i] == 11 &&
				is_ab8500_1p1_or_earlier(ab8500))
			continue;

		get_register_interruptible(ab8500, AB8500_INTERRUPT,
			AB8500_IT_LATCH1_REG + ab8500->irq_reg_offset[i],
			&value);
		set_register_interruptible(ab8500, AB8500_INTERRUPT,
			AB8500_IT_MASK1_REG + ab8500->irq_reg_offset[i], 0xff);
	}

	ret = abx500_register_ops(ab8500->dev, &ab8500_ops);
	if (ret)
		goto out_freeoldmask;

	for (i = 0; i < ab8500->mask_size; i++)
		ab8500->mask[i] = ab8500->oldmask[i] = 0xff;

	if (ab8500->irq_base) {
		ret = ab8500_irq_init(ab8500);
		if (ret)
			goto out_freeoldmask;

		ret = request_threaded_irq(ab8500->irq, NULL, ab8500_irq,
					   IRQF_ONESHOT | IRQF_NO_SUSPEND,
					   "ab8500", ab8500);
		if (ret)
			goto out_removeirq;
	}

	ret = mfd_add_devices(ab8500->dev, 0, abx500_common_devs,
			      ARRAY_SIZE(abx500_common_devs), NULL,
			      ab8500->irq_base);

	if (ret)
		goto out_freeirq;

	if (is_ab9540(ab8500))
		ret = mfd_add_devices(ab8500->dev, 0, ab9540_devs,
			      ARRAY_SIZE(ab9540_devs), NULL,
			      ab8500->irq_base);
	else
		ret = mfd_add_devices(ab8500->dev, 0, ab8500_devs,
			      ARRAY_SIZE(ab9540_devs), NULL,
			      ab8500->irq_base);
	if (ret)
		goto out_freeirq;

	if (is_ab9540(ab8500))
		ret = sysfs_create_group(&ab8500->dev->kobj,
					&ab9540_attr_group);
	else
		ret = sysfs_create_group(&ab8500->dev->kobj,
					&ab8500_attr_group);
	if (ret)
		dev_err(ab8500->dev, "error creating sysfs entries\n");
	else
		return ret;

out_freeirq:
	if (ab8500->irq_base)
		free_irq(ab8500->irq, ab8500);
out_removeirq:
	if (ab8500->irq_base)
		ab8500_irq_remove(ab8500);
out_freeoldmask:
	kfree(ab8500->oldmask);
out_freemask:
	kfree(ab8500->mask);

	return ret;
}

int __devexit ab8500_exit(struct ab8500 *ab8500)
{
	if (is_ab9540(ab8500))
		sysfs_remove_group(&ab8500->dev->kobj, &ab9540_attr_group);
	else
		sysfs_remove_group(&ab8500->dev->kobj, &ab8500_attr_group);
	mfd_remove_devices(ab8500->dev);
	if (ab8500->irq_base) {
		free_irq(ab8500->irq, ab8500);
		ab8500_irq_remove(ab8500);
	}
	kfree(ab8500->oldmask);
	kfree(ab8500->mask);

	return 0;
}

MODULE_AUTHOR("Mattias Wallin, Srinidhi Kasagar, Rabin Vincent");
MODULE_DESCRIPTION("AB8500 MFD core");
MODULE_LICENSE("GPL v2");
