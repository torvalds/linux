/*
 * Broadcom specific AMBA
 * GPIO driver
 *
 * Copyright 2011, Broadcom Corporation
 * Copyright 2012, Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/export.h>
#include <linux/bcma/bcma.h>

#include "bcma_private.h"

#define BCMA_GPIO_MAX_PINS	32

static int bcma_gpio_get_value(struct gpio_chip *chip, unsigned gpio)
{
	struct bcma_drv_cc *cc = gpiochip_get_data(chip);

	return !!bcma_chipco_gpio_in(cc, 1 << gpio);
}

static void bcma_gpio_set_value(struct gpio_chip *chip, unsigned gpio,
				int value)
{
	struct bcma_drv_cc *cc = gpiochip_get_data(chip);

	bcma_chipco_gpio_out(cc, 1 << gpio, value ? 1 << gpio : 0);
}

static int bcma_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	struct bcma_drv_cc *cc = gpiochip_get_data(chip);

	bcma_chipco_gpio_outen(cc, 1 << gpio, 0);
	return 0;
}

static int bcma_gpio_direction_output(struct gpio_chip *chip, unsigned gpio,
				      int value)
{
	struct bcma_drv_cc *cc = gpiochip_get_data(chip);

	bcma_chipco_gpio_outen(cc, 1 << gpio, 1 << gpio);
	bcma_chipco_gpio_out(cc, 1 << gpio, value ? 1 << gpio : 0);
	return 0;
}

static int bcma_gpio_request(struct gpio_chip *chip, unsigned gpio)
{
	struct bcma_drv_cc *cc = gpiochip_get_data(chip);

	bcma_chipco_gpio_control(cc, 1 << gpio, 0);
	/* clear pulldown */
	bcma_chipco_gpio_pulldown(cc, 1 << gpio, 0);
	/* Set pullup */
	bcma_chipco_gpio_pullup(cc, 1 << gpio, 1 << gpio);

	return 0;
}

static void bcma_gpio_free(struct gpio_chip *chip, unsigned gpio)
{
	struct bcma_drv_cc *cc = gpiochip_get_data(chip);

	/* clear pullup */
	bcma_chipco_gpio_pullup(cc, 1 << gpio, 0);
}

#if IS_BUILTIN(CONFIG_BCM47XX) || IS_BUILTIN(CONFIG_ARCH_BCM_5301X)

static void bcma_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct bcma_drv_cc *cc = gpiochip_get_data(gc);
	int gpio = irqd_to_hwirq(d);
	u32 val = bcma_chipco_gpio_in(cc, BIT(gpio));

	bcma_chipco_gpio_polarity(cc, BIT(gpio), val);
	bcma_chipco_gpio_intmask(cc, BIT(gpio), BIT(gpio));
}

static void bcma_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct bcma_drv_cc *cc = gpiochip_get_data(gc);
	int gpio = irqd_to_hwirq(d);

	bcma_chipco_gpio_intmask(cc, BIT(gpio), 0);
}

static struct irq_chip bcma_gpio_irq_chip = {
	.name		= "BCMA-GPIO",
	.irq_mask	= bcma_gpio_irq_mask,
	.irq_unmask	= bcma_gpio_irq_unmask,
};

static irqreturn_t bcma_gpio_irq_handler(int irq, void *dev_id)
{
	struct bcma_drv_cc *cc = dev_id;
	struct gpio_chip *gc = &cc->gpio;
	u32 val = bcma_cc_read32(cc, BCMA_CC_GPIOIN);
	u32 mask = bcma_cc_read32(cc, BCMA_CC_GPIOIRQ);
	u32 pol = bcma_cc_read32(cc, BCMA_CC_GPIOPOL);
	unsigned long irqs = (val ^ pol) & mask;
	int gpio;

	if (!irqs)
		return IRQ_NONE;

	for_each_set_bit(gpio, &irqs, gc->ngpio)
		generic_handle_irq(irq_find_mapping(gc->irqdomain, gpio));
	bcma_chipco_gpio_polarity(cc, irqs, val & irqs);

	return IRQ_HANDLED;
}

static int bcma_gpio_irq_init(struct bcma_drv_cc *cc)
{
	struct gpio_chip *chip = &cc->gpio;
	int hwirq, err;

	if (cc->core->bus->hosttype != BCMA_HOSTTYPE_SOC)
		return 0;

	hwirq = bcma_core_irq(cc->core, 0);
	err = request_irq(hwirq, bcma_gpio_irq_handler, IRQF_SHARED, "gpio",
			  cc);
	if (err)
		return err;

	bcma_chipco_gpio_intmask(cc, ~0, 0);
	bcma_cc_set32(cc, BCMA_CC_IRQMASK, BCMA_CC_IRQ_GPIO);

	err =  gpiochip_irqchip_add(chip,
				    &bcma_gpio_irq_chip,
				    0,
				    handle_simple_irq,
				    IRQ_TYPE_NONE);
	if (err) {
		free_irq(hwirq, cc);
		return err;
	}

	return 0;
}

static void bcma_gpio_irq_exit(struct bcma_drv_cc *cc)
{
	if (cc->core->bus->hosttype != BCMA_HOSTTYPE_SOC)
		return;

	bcma_cc_mask32(cc, BCMA_CC_IRQMASK, ~BCMA_CC_IRQ_GPIO);
	free_irq(bcma_core_irq(cc->core, 0), cc);
}
#else
static int bcma_gpio_irq_init(struct bcma_drv_cc *cc)
{
	return 0;
}

static void bcma_gpio_irq_exit(struct bcma_drv_cc *cc)
{
}
#endif

int bcma_gpio_init(struct bcma_drv_cc *cc)
{
	struct bcma_bus *bus = cc->core->bus;
	struct gpio_chip *chip = &cc->gpio;
	int err;

	chip->label		= "bcma_gpio";
	chip->owner		= THIS_MODULE;
	chip->request		= bcma_gpio_request;
	chip->free		= bcma_gpio_free;
	chip->get		= bcma_gpio_get_value;
	chip->set		= bcma_gpio_set_value;
	chip->direction_input	= bcma_gpio_direction_input;
	chip->direction_output	= bcma_gpio_direction_output;
	chip->owner		= THIS_MODULE;
	chip->parent		= bcma_bus_get_host_dev(bus);
#if IS_BUILTIN(CONFIG_OF)
	if (cc->core->bus->hosttype == BCMA_HOSTTYPE_SOC)
		chip->of_node	= cc->core->dev.of_node;
#endif
	switch (bus->chipinfo.id) {
	case BCMA_CHIP_ID_BCM4707:
	case BCMA_CHIP_ID_BCM5357:
	case BCMA_CHIP_ID_BCM53572:
	case BCMA_CHIP_ID_BCM47094:
		chip->ngpio	= 32;
		break;
	default:
		chip->ngpio	= 16;
	}

	/*
	 * Register SoC GPIO devices with absolute GPIO pin base.
	 * On MIPS, we don't have Device Tree and we can't use relative (per chip)
	 * GPIO numbers.
	 * On some ARM devices, user space may want to access some system GPIO
	 * pins directly, which is easier to do with a predictable GPIO base.
	 */
	if (IS_BUILTIN(CONFIG_BCM47XX) ||
	    cc->core->bus->hosttype == BCMA_HOSTTYPE_SOC)
		chip->base		= bus->num * BCMA_GPIO_MAX_PINS;
	else
		chip->base		= -1;

	err = gpiochip_add_data(chip, cc);
	if (err)
		return err;

	err = bcma_gpio_irq_init(cc);
	if (err) {
		gpiochip_remove(chip);
		return err;
	}

	return 0;
}

int bcma_gpio_unregister(struct bcma_drv_cc *cc)
{
	bcma_gpio_irq_exit(cc);
	gpiochip_remove(&cc->gpio);
	return 0;
}
