/*
 * Sonics Silicon Backplane
 * GPIO driver
 *
 * Copyright 2011, Broadcom Corporation
 * Copyright 2012, Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/gpio/driver.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/export.h>
#include <linux/ssb/ssb.h>

#include "ssb_private.h"


/**************************************************
 * Shared
 **************************************************/

#if IS_ENABLED(CONFIG_SSB_EMBEDDED)
static int ssb_gpio_to_irq(struct gpio_chip *chip, unsigned int gpio)
{
	struct ssb_bus *bus = gpiochip_get_data(chip);

	if (bus->bustype == SSB_BUSTYPE_SSB)
		return irq_find_mapping(bus->irq_domain, gpio);
	else
		return -EINVAL;
}
#endif

/**************************************************
 * ChipCommon
 **************************************************/

static int ssb_gpio_chipco_get_value(struct gpio_chip *chip, unsigned int gpio)
{
	struct ssb_bus *bus = gpiochip_get_data(chip);

	return !!ssb_chipco_gpio_in(&bus->chipco, 1 << gpio);
}

static void ssb_gpio_chipco_set_value(struct gpio_chip *chip, unsigned int gpio,
				      int value)
{
	struct ssb_bus *bus = gpiochip_get_data(chip);

	ssb_chipco_gpio_out(&bus->chipco, 1 << gpio, value ? 1 << gpio : 0);
}

static int ssb_gpio_chipco_direction_input(struct gpio_chip *chip,
					   unsigned int gpio)
{
	struct ssb_bus *bus = gpiochip_get_data(chip);

	ssb_chipco_gpio_outen(&bus->chipco, 1 << gpio, 0);
	return 0;
}

static int ssb_gpio_chipco_direction_output(struct gpio_chip *chip,
					    unsigned int gpio, int value)
{
	struct ssb_bus *bus = gpiochip_get_data(chip);

	ssb_chipco_gpio_outen(&bus->chipco, 1 << gpio, 1 << gpio);
	ssb_chipco_gpio_out(&bus->chipco, 1 << gpio, value ? 1 << gpio : 0);
	return 0;
}

static int ssb_gpio_chipco_request(struct gpio_chip *chip, unsigned int gpio)
{
	struct ssb_bus *bus = gpiochip_get_data(chip);

	ssb_chipco_gpio_control(&bus->chipco, 1 << gpio, 0);
	/* clear pulldown */
	ssb_chipco_gpio_pulldown(&bus->chipco, 1 << gpio, 0);
	/* Set pullup */
	ssb_chipco_gpio_pullup(&bus->chipco, 1 << gpio, 1 << gpio);

	return 0;
}

static void ssb_gpio_chipco_free(struct gpio_chip *chip, unsigned int gpio)
{
	struct ssb_bus *bus = gpiochip_get_data(chip);

	/* clear pullup */
	ssb_chipco_gpio_pullup(&bus->chipco, 1 << gpio, 0);
}

#if IS_ENABLED(CONFIG_SSB_EMBEDDED)
static void ssb_gpio_irq_chipco_mask(struct irq_data *d)
{
	struct ssb_bus *bus = irq_data_get_irq_chip_data(d);
	int gpio = irqd_to_hwirq(d);

	ssb_chipco_gpio_intmask(&bus->chipco, BIT(gpio), 0);
}

static void ssb_gpio_irq_chipco_unmask(struct irq_data *d)
{
	struct ssb_bus *bus = irq_data_get_irq_chip_data(d);
	int gpio = irqd_to_hwirq(d);
	u32 val = ssb_chipco_gpio_in(&bus->chipco, BIT(gpio));

	ssb_chipco_gpio_polarity(&bus->chipco, BIT(gpio), val);
	ssb_chipco_gpio_intmask(&bus->chipco, BIT(gpio), BIT(gpio));
}

static struct irq_chip ssb_gpio_irq_chipco_chip = {
	.name		= "SSB-GPIO-CC",
	.irq_mask	= ssb_gpio_irq_chipco_mask,
	.irq_unmask	= ssb_gpio_irq_chipco_unmask,
};

static irqreturn_t ssb_gpio_irq_chipco_handler(int irq, void *dev_id)
{
	struct ssb_bus *bus = dev_id;
	struct ssb_chipcommon *chipco = &bus->chipco;
	u32 val = chipco_read32(chipco, SSB_CHIPCO_GPIOIN);
	u32 mask = chipco_read32(chipco, SSB_CHIPCO_GPIOIRQ);
	u32 pol = chipco_read32(chipco, SSB_CHIPCO_GPIOPOL);
	unsigned long irqs = (val ^ pol) & mask;
	int gpio;

	if (!irqs)
		return IRQ_NONE;

	for_each_set_bit(gpio, &irqs, bus->gpio.ngpio)
		generic_handle_irq(ssb_gpio_to_irq(&bus->gpio, gpio));
	ssb_chipco_gpio_polarity(chipco, irqs, val & irqs);

	return IRQ_HANDLED;
}

static int ssb_gpio_irq_chipco_domain_init(struct ssb_bus *bus)
{
	struct ssb_chipcommon *chipco = &bus->chipco;
	struct gpio_chip *chip = &bus->gpio;
	int gpio, hwirq, err;

	if (bus->bustype != SSB_BUSTYPE_SSB)
		return 0;

	bus->irq_domain = irq_domain_add_linear(NULL, chip->ngpio,
						&irq_domain_simple_ops, chipco);
	if (!bus->irq_domain) {
		err = -ENODEV;
		goto err_irq_domain;
	}
	for (gpio = 0; gpio < chip->ngpio; gpio++) {
		int irq = irq_create_mapping(bus->irq_domain, gpio);

		irq_set_chip_data(irq, bus);
		irq_set_chip_and_handler(irq, &ssb_gpio_irq_chipco_chip,
					 handle_simple_irq);
	}

	hwirq = ssb_mips_irq(bus->chipco.dev) + 2;
	err = request_irq(hwirq, ssb_gpio_irq_chipco_handler, IRQF_SHARED,
			  "gpio", bus);
	if (err)
		goto err_req_irq;

	ssb_chipco_gpio_intmask(&bus->chipco, ~0, 0);
	chipco_set32(chipco, SSB_CHIPCO_IRQMASK, SSB_CHIPCO_IRQ_GPIO);

	return 0;

err_req_irq:
	for (gpio = 0; gpio < chip->ngpio; gpio++) {
		int irq = irq_find_mapping(bus->irq_domain, gpio);

		irq_dispose_mapping(irq);
	}
	irq_domain_remove(bus->irq_domain);
err_irq_domain:
	return err;
}

static void ssb_gpio_irq_chipco_domain_exit(struct ssb_bus *bus)
{
	struct ssb_chipcommon *chipco = &bus->chipco;
	struct gpio_chip *chip = &bus->gpio;
	int gpio;

	if (bus->bustype != SSB_BUSTYPE_SSB)
		return;

	chipco_mask32(chipco, SSB_CHIPCO_IRQMASK, ~SSB_CHIPCO_IRQ_GPIO);
	free_irq(ssb_mips_irq(bus->chipco.dev) + 2, chipco);
	for (gpio = 0; gpio < chip->ngpio; gpio++) {
		int irq = irq_find_mapping(bus->irq_domain, gpio);

		irq_dispose_mapping(irq);
	}
	irq_domain_remove(bus->irq_domain);
}
#else
static int ssb_gpio_irq_chipco_domain_init(struct ssb_bus *bus)
{
	return 0;
}

static void ssb_gpio_irq_chipco_domain_exit(struct ssb_bus *bus)
{
}
#endif

static int ssb_gpio_chipco_init(struct ssb_bus *bus)
{
	struct gpio_chip *chip = &bus->gpio;
	int err;

	chip->label		= "ssb_chipco_gpio";
	chip->owner		= THIS_MODULE;
	chip->request		= ssb_gpio_chipco_request;
	chip->free		= ssb_gpio_chipco_free;
	chip->get		= ssb_gpio_chipco_get_value;
	chip->set		= ssb_gpio_chipco_set_value;
	chip->direction_input	= ssb_gpio_chipco_direction_input;
	chip->direction_output	= ssb_gpio_chipco_direction_output;
#if IS_ENABLED(CONFIG_SSB_EMBEDDED)
	chip->to_irq		= ssb_gpio_to_irq;
#endif
	chip->ngpio		= 16;
	/* There is just one SoC in one device and its GPIO addresses should be
	 * deterministic to address them more easily. The other buses could get
	 * a random base number. */
	if (bus->bustype == SSB_BUSTYPE_SSB)
		chip->base		= 0;
	else
		chip->base		= -1;

	err = ssb_gpio_irq_chipco_domain_init(bus);
	if (err)
		return err;

	err = gpiochip_add_data(chip, bus);
	if (err) {
		ssb_gpio_irq_chipco_domain_exit(bus);
		return err;
	}

	return 0;
}

/**************************************************
 * EXTIF
 **************************************************/

#ifdef CONFIG_SSB_DRIVER_EXTIF

static int ssb_gpio_extif_get_value(struct gpio_chip *chip, unsigned int gpio)
{
	struct ssb_bus *bus = gpiochip_get_data(chip);

	return !!ssb_extif_gpio_in(&bus->extif, 1 << gpio);
}

static void ssb_gpio_extif_set_value(struct gpio_chip *chip, unsigned int gpio,
				     int value)
{
	struct ssb_bus *bus = gpiochip_get_data(chip);

	ssb_extif_gpio_out(&bus->extif, 1 << gpio, value ? 1 << gpio : 0);
}

static int ssb_gpio_extif_direction_input(struct gpio_chip *chip,
					  unsigned int gpio)
{
	struct ssb_bus *bus = gpiochip_get_data(chip);

	ssb_extif_gpio_outen(&bus->extif, 1 << gpio, 0);
	return 0;
}

static int ssb_gpio_extif_direction_output(struct gpio_chip *chip,
					   unsigned int gpio, int value)
{
	struct ssb_bus *bus = gpiochip_get_data(chip);

	ssb_extif_gpio_outen(&bus->extif, 1 << gpio, 1 << gpio);
	ssb_extif_gpio_out(&bus->extif, 1 << gpio, value ? 1 << gpio : 0);
	return 0;
}

#if IS_ENABLED(CONFIG_SSB_EMBEDDED)
static void ssb_gpio_irq_extif_mask(struct irq_data *d)
{
	struct ssb_bus *bus = irq_data_get_irq_chip_data(d);
	int gpio = irqd_to_hwirq(d);

	ssb_extif_gpio_intmask(&bus->extif, BIT(gpio), 0);
}

static void ssb_gpio_irq_extif_unmask(struct irq_data *d)
{
	struct ssb_bus *bus = irq_data_get_irq_chip_data(d);
	int gpio = irqd_to_hwirq(d);
	u32 val = ssb_extif_gpio_in(&bus->extif, BIT(gpio));

	ssb_extif_gpio_polarity(&bus->extif, BIT(gpio), val);
	ssb_extif_gpio_intmask(&bus->extif, BIT(gpio), BIT(gpio));
}

static struct irq_chip ssb_gpio_irq_extif_chip = {
	.name		= "SSB-GPIO-EXTIF",
	.irq_mask	= ssb_gpio_irq_extif_mask,
	.irq_unmask	= ssb_gpio_irq_extif_unmask,
};

static irqreturn_t ssb_gpio_irq_extif_handler(int irq, void *dev_id)
{
	struct ssb_bus *bus = dev_id;
	struct ssb_extif *extif = &bus->extif;
	u32 val = ssb_read32(extif->dev, SSB_EXTIF_GPIO_IN);
	u32 mask = ssb_read32(extif->dev, SSB_EXTIF_GPIO_INTMASK);
	u32 pol = ssb_read32(extif->dev, SSB_EXTIF_GPIO_INTPOL);
	unsigned long irqs = (val ^ pol) & mask;
	int gpio;

	if (!irqs)
		return IRQ_NONE;

	for_each_set_bit(gpio, &irqs, bus->gpio.ngpio)
		generic_handle_irq(ssb_gpio_to_irq(&bus->gpio, gpio));
	ssb_extif_gpio_polarity(extif, irqs, val & irqs);

	return IRQ_HANDLED;
}

static int ssb_gpio_irq_extif_domain_init(struct ssb_bus *bus)
{
	struct ssb_extif *extif = &bus->extif;
	struct gpio_chip *chip = &bus->gpio;
	int gpio, hwirq, err;

	if (bus->bustype != SSB_BUSTYPE_SSB)
		return 0;

	bus->irq_domain = irq_domain_add_linear(NULL, chip->ngpio,
						&irq_domain_simple_ops, extif);
	if (!bus->irq_domain) {
		err = -ENODEV;
		goto err_irq_domain;
	}
	for (gpio = 0; gpio < chip->ngpio; gpio++) {
		int irq = irq_create_mapping(bus->irq_domain, gpio);

		irq_set_chip_data(irq, bus);
		irq_set_chip_and_handler(irq, &ssb_gpio_irq_extif_chip,
					 handle_simple_irq);
	}

	hwirq = ssb_mips_irq(bus->extif.dev) + 2;
	err = request_irq(hwirq, ssb_gpio_irq_extif_handler, IRQF_SHARED,
			  "gpio", bus);
	if (err)
		goto err_req_irq;

	ssb_extif_gpio_intmask(&bus->extif, ~0, 0);

	return 0;

err_req_irq:
	for (gpio = 0; gpio < chip->ngpio; gpio++) {
		int irq = irq_find_mapping(bus->irq_domain, gpio);

		irq_dispose_mapping(irq);
	}
	irq_domain_remove(bus->irq_domain);
err_irq_domain:
	return err;
}

static void ssb_gpio_irq_extif_domain_exit(struct ssb_bus *bus)
{
	struct ssb_extif *extif = &bus->extif;
	struct gpio_chip *chip = &bus->gpio;
	int gpio;

	if (bus->bustype != SSB_BUSTYPE_SSB)
		return;

	free_irq(ssb_mips_irq(bus->extif.dev) + 2, extif);
	for (gpio = 0; gpio < chip->ngpio; gpio++) {
		int irq = irq_find_mapping(bus->irq_domain, gpio);

		irq_dispose_mapping(irq);
	}
	irq_domain_remove(bus->irq_domain);
}
#else
static int ssb_gpio_irq_extif_domain_init(struct ssb_bus *bus)
{
	return 0;
}

static void ssb_gpio_irq_extif_domain_exit(struct ssb_bus *bus)
{
}
#endif

static int ssb_gpio_extif_init(struct ssb_bus *bus)
{
	struct gpio_chip *chip = &bus->gpio;
	int err;

	chip->label		= "ssb_extif_gpio";
	chip->owner		= THIS_MODULE;
	chip->get		= ssb_gpio_extif_get_value;
	chip->set		= ssb_gpio_extif_set_value;
	chip->direction_input	= ssb_gpio_extif_direction_input;
	chip->direction_output	= ssb_gpio_extif_direction_output;
#if IS_ENABLED(CONFIG_SSB_EMBEDDED)
	chip->to_irq		= ssb_gpio_to_irq;
#endif
	chip->ngpio		= 5;
	/* There is just one SoC in one device and its GPIO addresses should be
	 * deterministic to address them more easily. The other buses could get
	 * a random base number. */
	if (bus->bustype == SSB_BUSTYPE_SSB)
		chip->base		= 0;
	else
		chip->base		= -1;

	err = ssb_gpio_irq_extif_domain_init(bus);
	if (err)
		return err;

	err = gpiochip_add_data(chip, bus);
	if (err) {
		ssb_gpio_irq_extif_domain_exit(bus);
		return err;
	}

	return 0;
}

#else
static int ssb_gpio_extif_init(struct ssb_bus *bus)
{
	return -ENOTSUPP;
}
#endif

/**************************************************
 * Init
 **************************************************/

int ssb_gpio_init(struct ssb_bus *bus)
{
	if (ssb_chipco_available(&bus->chipco))
		return ssb_gpio_chipco_init(bus);
	else if (ssb_extif_available(&bus->extif))
		return ssb_gpio_extif_init(bus);
	else
		SSB_WARN_ON(1);

	return -1;
}

int ssb_gpio_unregister(struct ssb_bus *bus)
{
	if (ssb_chipco_available(&bus->chipco) ||
	    ssb_extif_available(&bus->extif)) {
		gpiochip_remove(&bus->gpio);
		return 0;
	} else {
		SSB_WARN_ON(1);
	}

	return -1;
}
