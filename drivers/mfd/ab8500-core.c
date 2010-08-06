/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 * Author: Srinidhi Kasagar <srinidhi.kasagar@stericsson.com>
 * Author: Rabin Vincent <rabin.vincent@stericsson.com>
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
#include <linux/mfd/ab8500.h>

/*
 * Interrupt register offsets
 * Bank : 0x0E
 */
#define AB8500_IT_SOURCE1_REG		0x0E00
#define AB8500_IT_SOURCE2_REG		0x0E01
#define AB8500_IT_SOURCE3_REG		0x0E02
#define AB8500_IT_SOURCE4_REG		0x0E03
#define AB8500_IT_SOURCE5_REG		0x0E04
#define AB8500_IT_SOURCE6_REG		0x0E05
#define AB8500_IT_SOURCE7_REG		0x0E06
#define AB8500_IT_SOURCE8_REG		0x0E07
#define AB8500_IT_SOURCE19_REG		0x0E12
#define AB8500_IT_SOURCE20_REG		0x0E13
#define AB8500_IT_SOURCE21_REG		0x0E14
#define AB8500_IT_SOURCE22_REG		0x0E15
#define AB8500_IT_SOURCE23_REG		0x0E16
#define AB8500_IT_SOURCE24_REG		0x0E17

/*
 * latch registers
 */
#define AB8500_IT_LATCH1_REG		0x0E20
#define AB8500_IT_LATCH2_REG		0x0E21
#define AB8500_IT_LATCH3_REG		0x0E22
#define AB8500_IT_LATCH4_REG		0x0E23
#define AB8500_IT_LATCH5_REG		0x0E24
#define AB8500_IT_LATCH6_REG		0x0E25
#define AB8500_IT_LATCH7_REG		0x0E26
#define AB8500_IT_LATCH8_REG		0x0E27
#define AB8500_IT_LATCH9_REG		0x0E28
#define AB8500_IT_LATCH10_REG		0x0E29
#define AB8500_IT_LATCH19_REG		0x0E32
#define AB8500_IT_LATCH20_REG		0x0E33
#define AB8500_IT_LATCH21_REG		0x0E34
#define AB8500_IT_LATCH22_REG		0x0E35
#define AB8500_IT_LATCH23_REG		0x0E36
#define AB8500_IT_LATCH24_REG		0x0E37

/*
 * mask registers
 */

#define AB8500_IT_MASK1_REG		0x0E40
#define AB8500_IT_MASK2_REG		0x0E41
#define AB8500_IT_MASK3_REG		0x0E42
#define AB8500_IT_MASK4_REG		0x0E43
#define AB8500_IT_MASK5_REG		0x0E44
#define AB8500_IT_MASK6_REG		0x0E45
#define AB8500_IT_MASK7_REG		0x0E46
#define AB8500_IT_MASK8_REG		0x0E47
#define AB8500_IT_MASK9_REG		0x0E48
#define AB8500_IT_MASK10_REG		0x0E49
#define AB8500_IT_MASK11_REG		0x0E4A
#define AB8500_IT_MASK12_REG		0x0E4B
#define AB8500_IT_MASK13_REG		0x0E4C
#define AB8500_IT_MASK14_REG		0x0E4D
#define AB8500_IT_MASK15_REG		0x0E4E
#define AB8500_IT_MASK16_REG		0x0E4F
#define AB8500_IT_MASK17_REG		0x0E50
#define AB8500_IT_MASK18_REG		0x0E51
#define AB8500_IT_MASK19_REG		0x0E52
#define AB8500_IT_MASK20_REG		0x0E53
#define AB8500_IT_MASK21_REG		0x0E54
#define AB8500_IT_MASK22_REG		0x0E55
#define AB8500_IT_MASK23_REG		0x0E56
#define AB8500_IT_MASK24_REG		0x0E57

#define AB8500_REV_REG			0x1080

/*
 * Map interrupt numbers to the LATCH and MASK register offsets, Interrupt
 * numbers are indexed into this array with (num / 8).
 *
 * This is one off from the register names, i.e. AB8500_IT_MASK1_REG is at
 * offset 0.
 */
static const int ab8500_irq_regoffset[AB8500_NUM_IRQ_REGS] = {
	0, 1, 2, 3, 4, 6, 7, 8, 9, 18, 19, 20, 21,
};

static int __ab8500_write(struct ab8500 *ab8500, u16 addr, u8 data)
{
	int ret;

	dev_vdbg(ab8500->dev, "wr: addr %#x <= %#x\n", addr, data);

	ret = ab8500->write(ab8500, addr, data);
	if (ret < 0)
		dev_err(ab8500->dev, "failed to write reg %#x: %d\n",
			addr, ret);

	return ret;
}

/**
 * ab8500_write() - write an AB8500 register
 * @ab8500: device to write to
 * @addr: address of the register
 * @data: value to write
 */
int ab8500_write(struct ab8500 *ab8500, u16 addr, u8 data)
{
	int ret;

	mutex_lock(&ab8500->lock);
	ret = __ab8500_write(ab8500, addr, data);
	mutex_unlock(&ab8500->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(ab8500_write);

static int __ab8500_read(struct ab8500 *ab8500, u16 addr)
{
	int ret;

	ret = ab8500->read(ab8500, addr);
	if (ret < 0)
		dev_err(ab8500->dev, "failed to read reg %#x: %d\n",
			addr, ret);

	dev_vdbg(ab8500->dev, "rd: addr %#x => data %#x\n", addr, ret);

	return ret;
}

/**
 * ab8500_read() - read an AB8500 register
 * @ab8500: device to read from
 * @addr: address of the register
 */
int ab8500_read(struct ab8500 *ab8500, u16 addr)
{
	int ret;

	mutex_lock(&ab8500->lock);
	ret = __ab8500_read(ab8500, addr);
	mutex_unlock(&ab8500->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(ab8500_read);

/**
 * ab8500_set_bits() - set a bitfield in an AB8500 register
 * @ab8500: device to read from
 * @addr: address of the register
 * @mask: mask of the bitfield to modify
 * @data: value to set to the bitfield
 */
int ab8500_set_bits(struct ab8500 *ab8500, u16 addr, u8 mask, u8 data)
{
	int ret;

	mutex_lock(&ab8500->lock);

	ret = __ab8500_read(ab8500, addr);
	if (ret < 0)
		goto out;

	ret &= ~mask;
	ret |= data;

	ret = __ab8500_write(ab8500, addr, ret);

out:
	mutex_unlock(&ab8500->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(ab8500_set_bits);

static void ab8500_irq_lock(unsigned int irq)
{
	struct ab8500 *ab8500 = get_irq_chip_data(irq);

	mutex_lock(&ab8500->irq_lock);
}

static void ab8500_irq_sync_unlock(unsigned int irq)
{
	struct ab8500 *ab8500 = get_irq_chip_data(irq);
	int i;

	for (i = 0; i < AB8500_NUM_IRQ_REGS; i++) {
		u8 old = ab8500->oldmask[i];
		u8 new = ab8500->mask[i];
		int reg;

		if (new == old)
			continue;

		ab8500->oldmask[i] = new;

		reg = AB8500_IT_MASK1_REG + ab8500_irq_regoffset[i];
		ab8500_write(ab8500, reg, new);
	}

	mutex_unlock(&ab8500->irq_lock);
}

static void ab8500_irq_mask(unsigned int irq)
{
	struct ab8500 *ab8500 = get_irq_chip_data(irq);
	int offset = irq - ab8500->irq_base;
	int index = offset / 8;
	int mask = 1 << (offset % 8);

	ab8500->mask[index] |= mask;
}

static void ab8500_irq_unmask(unsigned int irq)
{
	struct ab8500 *ab8500 = get_irq_chip_data(irq);
	int offset = irq - ab8500->irq_base;
	int index = offset / 8;
	int mask = 1 << (offset % 8);

	ab8500->mask[index] &= ~mask;
}

static struct irq_chip ab8500_irq_chip = {
	.name			= "ab8500",
	.bus_lock		= ab8500_irq_lock,
	.bus_sync_unlock	= ab8500_irq_sync_unlock,
	.mask			= ab8500_irq_mask,
	.unmask			= ab8500_irq_unmask,
};

static irqreturn_t ab8500_irq(int irq, void *dev)
{
	struct ab8500 *ab8500 = dev;
	int i;

	dev_vdbg(ab8500->dev, "interrupt\n");

	for (i = 0; i < AB8500_NUM_IRQ_REGS; i++) {
		int regoffset = ab8500_irq_regoffset[i];
		int status;

		status = ab8500_read(ab8500, AB8500_IT_LATCH1_REG + regoffset);
		if (status <= 0)
			continue;

		do {
			int bit = __ffs(status);
			int line = i * 8 + bit;

			handle_nested_irq(ab8500->irq_base + line);
			status &= ~(1 << bit);
		} while (status);
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

static struct mfd_cell ab8500_devs[] = {
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
	{ .name = "ab8500-charger", },
	{ .name = "ab8500-audio", },
	{ .name = "ab8500-usb", },
	{ .name = "ab8500-pwm", },
};

int __devinit ab8500_init(struct ab8500 *ab8500)
{
	struct ab8500_platform_data *plat = dev_get_platdata(ab8500->dev);
	int ret;
	int i;

	if (plat)
		ab8500->irq_base = plat->irq_base;

	mutex_init(&ab8500->lock);
	mutex_init(&ab8500->irq_lock);

	ret = ab8500_read(ab8500, AB8500_REV_REG);
	if (ret < 0)
		return ret;

	/*
	 * 0x0 - Early Drop
	 * 0x10 - Cut 1.0
	 * 0x11 - Cut 1.1
	 */
	if (ret == 0x0 || ret == 0x10 || ret == 0x11) {
		ab8500->revision = ret;
		dev_info(ab8500->dev, "detected chip, revision: %#x\n", ret);
	} else {
		dev_err(ab8500->dev, "unknown chip, revision: %#x\n", ret);
		return -EINVAL;
	}

	if (plat && plat->init)
		plat->init(ab8500);

	/* Clear and mask all interrupts */
	for (i = 0; i < 10; i++) {
		ab8500_read(ab8500, AB8500_IT_LATCH1_REG + i);
		ab8500_write(ab8500, AB8500_IT_MASK1_REG + i, 0xff);
	}

	for (i = 18; i < 24; i++) {
		ab8500_read(ab8500, AB8500_IT_LATCH1_REG + i);
		ab8500_write(ab8500, AB8500_IT_MASK1_REG + i, 0xff);
	}

	for (i = 0; i < AB8500_NUM_IRQ_REGS; i++)
		ab8500->mask[i] = ab8500->oldmask[i] = 0xff;

	if (ab8500->irq_base) {
		ret = ab8500_irq_init(ab8500);
		if (ret)
			return ret;

		ret = request_threaded_irq(ab8500->irq, NULL, ab8500_irq,
					   IRQF_ONESHOT, "ab8500", ab8500);
		if (ret)
			goto out_removeirq;
	}

	ret = mfd_add_devices(ab8500->dev, -1, ab8500_devs,
			      ARRAY_SIZE(ab8500_devs), NULL,
			      ab8500->irq_base);
	if (ret)
		goto out_freeirq;

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
