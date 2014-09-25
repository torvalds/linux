/*
 *  Driver for NEC VR4100 series General-purpose I/O Unit.
 *
 *  Copyright (C) 2002 MontaVista Software Inc.
 *	Author: Yoichi Yuasa <source@mvista.com>
 *  Copyright (C) 2003-2009  Yoichi Yuasa <yuasa@linux-mips.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <asm/vr41xx/giu.h>
#include <asm/vr41xx/irq.h>
#include <asm/vr41xx/vr41xx.h>

MODULE_AUTHOR("Yoichi Yuasa <yuasa@linux-mips.org>");
MODULE_DESCRIPTION("NEC VR4100 series General-purpose I/O Unit driver");
MODULE_LICENSE("GPL");

#define GIUIOSELL	0x00
#define GIUIOSELH	0x02
#define GIUPIODL	0x04
#define GIUPIODH	0x06
#define GIUINTSTATL	0x08
#define GIUINTSTATH	0x0a
#define GIUINTENL	0x0c
#define GIUINTENH	0x0e
#define GIUINTTYPL	0x10
#define GIUINTTYPH	0x12
#define GIUINTALSELL	0x14
#define GIUINTALSELH	0x16
#define GIUINTHTSELL	0x18
#define GIUINTHTSELH	0x1a
#define GIUPODATL	0x1c
#define GIUPODATEN	0x1c
#define GIUPODATH	0x1e
 #define PIOEN0		0x0100
 #define PIOEN1		0x0200
#define GIUPODAT	0x1e
#define GIUFEDGEINHL	0x20
#define GIUFEDGEINHH	0x22
#define GIUREDGEINHL	0x24
#define GIUREDGEINHH	0x26

#define GIUUSEUPDN	0x1e0
#define GIUTERMUPDN	0x1e2

#define GPIO_HAS_PULLUPDOWN_IO		0x0001
#define GPIO_HAS_OUTPUT_ENABLE		0x0002
#define GPIO_HAS_INTERRUPT_EDGE_SELECT	0x0100

enum {
	GPIO_INPUT,
	GPIO_OUTPUT,
};

static DEFINE_SPINLOCK(giu_lock);
static unsigned long giu_flags;

static void __iomem *giu_base;
static struct gpio_chip vr41xx_gpio_chip;

#define giu_read(offset)		readw(giu_base + (offset))
#define giu_write(offset, value)	writew((value), giu_base + (offset))

#define GPIO_PIN_OF_IRQ(irq)	((irq) - GIU_IRQ_BASE)
#define GIUINT_HIGH_OFFSET	16
#define GIUINT_HIGH_MAX		32

static inline u16 giu_set(u16 offset, u16 set)
{
	u16 data;

	data = giu_read(offset);
	data |= set;
	giu_write(offset, data);

	return data;
}

static inline u16 giu_clear(u16 offset, u16 clear)
{
	u16 data;

	data = giu_read(offset);
	data &= ~clear;
	giu_write(offset, data);

	return data;
}

static void ack_giuint_low(struct irq_data *d)
{
	giu_write(GIUINTSTATL, 1 << GPIO_PIN_OF_IRQ(d->irq));
}

static void mask_giuint_low(struct irq_data *d)
{
	giu_clear(GIUINTENL, 1 << GPIO_PIN_OF_IRQ(d->irq));
}

static void mask_ack_giuint_low(struct irq_data *d)
{
	unsigned int pin;

	pin = GPIO_PIN_OF_IRQ(d->irq);
	giu_clear(GIUINTENL, 1 << pin);
	giu_write(GIUINTSTATL, 1 << pin);
}

static void unmask_giuint_low(struct irq_data *d)
{
	giu_set(GIUINTENL, 1 << GPIO_PIN_OF_IRQ(d->irq));
}

static unsigned int startup_giuint(struct irq_data *data)
{
	if (gpio_lock_as_irq(&vr41xx_gpio_chip, data->hwirq))
		dev_err(vr41xx_gpio_chip.dev,
			"unable to lock HW IRQ %lu for IRQ\n",
			data->hwirq);
	/* Satisfy the .enable semantics by unmasking the line */
	unmask_giuint_low(data);
	return 0;
}

static void shutdown_giuint(struct irq_data *data)
{
	mask_giuint_low(data);
	gpio_unlock_as_irq(&vr41xx_gpio_chip, data->hwirq);
}

static struct irq_chip giuint_low_irq_chip = {
	.name		= "GIUINTL",
	.irq_ack	= ack_giuint_low,
	.irq_mask	= mask_giuint_low,
	.irq_mask_ack	= mask_ack_giuint_low,
	.irq_unmask	= unmask_giuint_low,
	.irq_startup	= startup_giuint,
	.irq_shutdown	= shutdown_giuint,
};

static void ack_giuint_high(struct irq_data *d)
{
	giu_write(GIUINTSTATH,
		  1 << (GPIO_PIN_OF_IRQ(d->irq) - GIUINT_HIGH_OFFSET));
}

static void mask_giuint_high(struct irq_data *d)
{
	giu_clear(GIUINTENH, 1 << (GPIO_PIN_OF_IRQ(d->irq) - GIUINT_HIGH_OFFSET));
}

static void mask_ack_giuint_high(struct irq_data *d)
{
	unsigned int pin;

	pin = GPIO_PIN_OF_IRQ(d->irq) - GIUINT_HIGH_OFFSET;
	giu_clear(GIUINTENH, 1 << pin);
	giu_write(GIUINTSTATH, 1 << pin);
}

static void unmask_giuint_high(struct irq_data *d)
{
	giu_set(GIUINTENH, 1 << (GPIO_PIN_OF_IRQ(d->irq) - GIUINT_HIGH_OFFSET));
}

static struct irq_chip giuint_high_irq_chip = {
	.name		= "GIUINTH",
	.irq_ack	= ack_giuint_high,
	.irq_mask	= mask_giuint_high,
	.irq_mask_ack	= mask_ack_giuint_high,
	.irq_unmask	= unmask_giuint_high,
};

static int giu_get_irq(unsigned int irq)
{
	u16 pendl, pendh, maskl, maskh;
	int i;

	pendl = giu_read(GIUINTSTATL);
	pendh = giu_read(GIUINTSTATH);
	maskl = giu_read(GIUINTENL);
	maskh = giu_read(GIUINTENH);

	maskl &= pendl;
	maskh &= pendh;

	if (maskl) {
		for (i = 0; i < 16; i++) {
			if (maskl & (1 << i))
				return GIU_IRQ(i);
		}
	} else if (maskh) {
		for (i = 0; i < 16; i++) {
			if (maskh & (1 << i))
				return GIU_IRQ(i + GIUINT_HIGH_OFFSET);
		}
	}

	printk(KERN_ERR "spurious GIU interrupt: %04x(%04x),%04x(%04x)\n",
	       maskl, pendl, maskh, pendh);

	atomic_inc(&irq_err_count);

	return -EINVAL;
}

void vr41xx_set_irq_trigger(unsigned int pin, irq_trigger_t trigger,
			    irq_signal_t signal)
{
	u16 mask;

	if (pin < GIUINT_HIGH_OFFSET) {
		mask = 1 << pin;
		if (trigger != IRQ_TRIGGER_LEVEL) {
			giu_set(GIUINTTYPL, mask);
			if (signal == IRQ_SIGNAL_HOLD)
				giu_set(GIUINTHTSELL, mask);
			else
				giu_clear(GIUINTHTSELL, mask);
			if (giu_flags & GPIO_HAS_INTERRUPT_EDGE_SELECT) {
				switch (trigger) {
				case IRQ_TRIGGER_EDGE_FALLING:
					giu_set(GIUFEDGEINHL, mask);
					giu_clear(GIUREDGEINHL, mask);
					break;
				case IRQ_TRIGGER_EDGE_RISING:
					giu_clear(GIUFEDGEINHL, mask);
					giu_set(GIUREDGEINHL, mask);
					break;
				default:
					giu_set(GIUFEDGEINHL, mask);
					giu_set(GIUREDGEINHL, mask);
					break;
				}
			}
			irq_set_chip_and_handler(GIU_IRQ(pin),
						 &giuint_low_irq_chip,
						 handle_edge_irq);
		} else {
			giu_clear(GIUINTTYPL, mask);
			giu_clear(GIUINTHTSELL, mask);
			irq_set_chip_and_handler(GIU_IRQ(pin),
						 &giuint_low_irq_chip,
						 handle_level_irq);
		}
		giu_write(GIUINTSTATL, mask);
	} else if (pin < GIUINT_HIGH_MAX) {
		mask = 1 << (pin - GIUINT_HIGH_OFFSET);
		if (trigger != IRQ_TRIGGER_LEVEL) {
			giu_set(GIUINTTYPH, mask);
			if (signal == IRQ_SIGNAL_HOLD)
				giu_set(GIUINTHTSELH, mask);
			else
				giu_clear(GIUINTHTSELH, mask);
			if (giu_flags & GPIO_HAS_INTERRUPT_EDGE_SELECT) {
				switch (trigger) {
				case IRQ_TRIGGER_EDGE_FALLING:
					giu_set(GIUFEDGEINHH, mask);
					giu_clear(GIUREDGEINHH, mask);
					break;
				case IRQ_TRIGGER_EDGE_RISING:
					giu_clear(GIUFEDGEINHH, mask);
					giu_set(GIUREDGEINHH, mask);
					break;
				default:
					giu_set(GIUFEDGEINHH, mask);
					giu_set(GIUREDGEINHH, mask);
					break;
				}
			}
			irq_set_chip_and_handler(GIU_IRQ(pin),
						 &giuint_high_irq_chip,
						 handle_edge_irq);
		} else {
			giu_clear(GIUINTTYPH, mask);
			giu_clear(GIUINTHTSELH, mask);
			irq_set_chip_and_handler(GIU_IRQ(pin),
						 &giuint_high_irq_chip,
						 handle_level_irq);
		}
		giu_write(GIUINTSTATH, mask);
	}
}
EXPORT_SYMBOL_GPL(vr41xx_set_irq_trigger);

void vr41xx_set_irq_level(unsigned int pin, irq_level_t level)
{
	u16 mask;

	if (pin < GIUINT_HIGH_OFFSET) {
		mask = 1 << pin;
		if (level == IRQ_LEVEL_HIGH)
			giu_set(GIUINTALSELL, mask);
		else
			giu_clear(GIUINTALSELL, mask);
		giu_write(GIUINTSTATL, mask);
	} else if (pin < GIUINT_HIGH_MAX) {
		mask = 1 << (pin - GIUINT_HIGH_OFFSET);
		if (level == IRQ_LEVEL_HIGH)
			giu_set(GIUINTALSELH, mask);
		else
			giu_clear(GIUINTALSELH, mask);
		giu_write(GIUINTSTATH, mask);
	}
}
EXPORT_SYMBOL_GPL(vr41xx_set_irq_level);

static int giu_set_direction(struct gpio_chip *chip, unsigned pin, int dir)
{
	u16 offset, mask, reg;
	unsigned long flags;

	if (pin >= chip->ngpio)
		return -EINVAL;

	if (pin < 16) {
		offset = GIUIOSELL;
		mask = 1 << pin;
	} else if (pin < 32) {
		offset = GIUIOSELH;
		mask = 1 << (pin - 16);
	} else {
		if (giu_flags & GPIO_HAS_OUTPUT_ENABLE) {
			offset = GIUPODATEN;
			mask = 1 << (pin - 32);
		} else {
			switch (pin) {
			case 48:
				offset = GIUPODATH;
				mask = PIOEN0;
				break;
			case 49:
				offset = GIUPODATH;
				mask = PIOEN1;
				break;
			default:
				return -EINVAL;
			}
		}
	}

	spin_lock_irqsave(&giu_lock, flags);

	reg = giu_read(offset);
	if (dir == GPIO_OUTPUT)
		reg |= mask;
	else
		reg &= ~mask;
	giu_write(offset, reg);

	spin_unlock_irqrestore(&giu_lock, flags);

	return 0;
}

int vr41xx_gpio_pullupdown(unsigned int pin, gpio_pull_t pull)
{
	u16 reg, mask;
	unsigned long flags;

	if ((giu_flags & GPIO_HAS_PULLUPDOWN_IO) != GPIO_HAS_PULLUPDOWN_IO)
		return -EPERM;

	if (pin >= 15)
		return -EINVAL;

	mask = 1 << pin;

	spin_lock_irqsave(&giu_lock, flags);

	if (pull == GPIO_PULL_UP || pull == GPIO_PULL_DOWN) {
		reg = giu_read(GIUTERMUPDN);
		if (pull == GPIO_PULL_UP)
			reg |= mask;
		else
			reg &= ~mask;
		giu_write(GIUTERMUPDN, reg);

		reg = giu_read(GIUUSEUPDN);
		reg |= mask;
		giu_write(GIUUSEUPDN, reg);
	} else {
		reg = giu_read(GIUUSEUPDN);
		reg &= ~mask;
		giu_write(GIUUSEUPDN, reg);
	}

	spin_unlock_irqrestore(&giu_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(vr41xx_gpio_pullupdown);

static int vr41xx_gpio_get(struct gpio_chip *chip, unsigned pin)
{
	u16 reg, mask;

	if (pin >= chip->ngpio)
		return -EINVAL;

	if (pin < 16) {
		reg = giu_read(GIUPIODL);
		mask = 1 << pin;
	} else if (pin < 32) {
		reg = giu_read(GIUPIODH);
		mask = 1 << (pin - 16);
	} else if (pin < 48) {
		reg = giu_read(GIUPODATL);
		mask = 1 << (pin - 32);
	} else {
		reg = giu_read(GIUPODATH);
		mask = 1 << (pin - 48);
	}

	if (reg & mask)
		return 1;

	return 0;
}

static void vr41xx_gpio_set(struct gpio_chip *chip, unsigned pin,
			    int value)
{
	u16 offset, mask, reg;
	unsigned long flags;

	if (pin >= chip->ngpio)
		return;

	if (pin < 16) {
		offset = GIUPIODL;
		mask = 1 << pin;
	} else if (pin < 32) {
		offset = GIUPIODH;
		mask = 1 << (pin - 16);
	} else if (pin < 48) {
		offset = GIUPODATL;
		mask = 1 << (pin - 32);
	} else {
		offset = GIUPODATH;
		mask = 1 << (pin - 48);
	}

	spin_lock_irqsave(&giu_lock, flags);

	reg = giu_read(offset);
	if (value)
		reg |= mask;
	else
		reg &= ~mask;
	giu_write(offset, reg);

	spin_unlock_irqrestore(&giu_lock, flags);
}


static int vr41xx_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	return giu_set_direction(chip, offset, GPIO_INPUT);
}

static int vr41xx_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
				int value)
{
	vr41xx_gpio_set(chip, offset, value);

	return giu_set_direction(chip, offset, GPIO_OUTPUT);
}

static int vr41xx_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	if (offset >= chip->ngpio)
		return -EINVAL;

	return GIU_IRQ_BASE + offset;
}

static struct gpio_chip vr41xx_gpio_chip = {
	.label			= "vr41xx",
	.owner			= THIS_MODULE,
	.direction_input	= vr41xx_gpio_direction_input,
	.get			= vr41xx_gpio_get,
	.direction_output	= vr41xx_gpio_direction_output,
	.set			= vr41xx_gpio_set,
	.to_irq			= vr41xx_gpio_to_irq,
};

static int giu_probe(struct platform_device *pdev)
{
	struct resource *res;
	unsigned int trigger, i, pin;
	struct irq_chip *chip;
	int irq, ret;

	switch (pdev->id) {
	case GPIO_50PINS_PULLUPDOWN:
		giu_flags = GPIO_HAS_PULLUPDOWN_IO;
		vr41xx_gpio_chip.ngpio = 50;
		break;
	case GPIO_36PINS:
		vr41xx_gpio_chip.ngpio = 36;
		break;
	case GPIO_48PINS_EDGE_SELECT:
		giu_flags = GPIO_HAS_INTERRUPT_EDGE_SELECT;
		vr41xx_gpio_chip.ngpio = 48;
		break;
	default:
		dev_err(&pdev->dev, "GIU: unknown ID %d\n", pdev->id);
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EBUSY;

	giu_base = ioremap(res->start, resource_size(res));
	if (!giu_base)
		return -ENOMEM;

	vr41xx_gpio_chip.dev = &pdev->dev;

	ret = gpiochip_add(&vr41xx_gpio_chip);
	if (!ret) {
		iounmap(giu_base);
		return -ENODEV;
	}

	giu_write(GIUINTENL, 0);
	giu_write(GIUINTENH, 0);

	trigger = giu_read(GIUINTTYPH) << 16;
	trigger |= giu_read(GIUINTTYPL);
	for (i = GIU_IRQ_BASE; i <= GIU_IRQ_LAST; i++) {
		pin = GPIO_PIN_OF_IRQ(i);
		if (pin < GIUINT_HIGH_OFFSET)
			chip = &giuint_low_irq_chip;
		else
			chip = &giuint_high_irq_chip;

		if (trigger & (1 << pin))
			irq_set_chip_and_handler(i, chip, handle_edge_irq);
		else
			irq_set_chip_and_handler(i, chip, handle_level_irq);

	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0 || irq >= nr_irqs)
		return -EBUSY;

	return cascade_irq(irq, giu_get_irq);
}

static int giu_remove(struct platform_device *pdev)
{
	if (giu_base) {
		iounmap(giu_base);
		giu_base = NULL;
	}

	return 0;
}

static struct platform_driver giu_device_driver = {
	.probe		= giu_probe,
	.remove		= giu_remove,
	.driver		= {
		.name	= "GIU",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(giu_device_driver);
