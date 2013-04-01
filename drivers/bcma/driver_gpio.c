/*
 * Broadcom specific AMBA
 * GPIO driver
 *
 * Copyright 2011, Broadcom Corporation
 * Copyright 2012, Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/gpio.h>
#include <linux/export.h>
#include <linux/bcma/bcma.h>

#include "bcma_private.h"

static inline struct bcma_drv_cc *bcma_gpio_get_cc(struct gpio_chip *chip)
{
	return container_of(chip, struct bcma_drv_cc, gpio);
}

static int bcma_gpio_get_value(struct gpio_chip *chip, unsigned gpio)
{
	struct bcma_drv_cc *cc = bcma_gpio_get_cc(chip);

	return !!bcma_chipco_gpio_in(cc, 1 << gpio);
}

static void bcma_gpio_set_value(struct gpio_chip *chip, unsigned gpio,
				int value)
{
	struct bcma_drv_cc *cc = bcma_gpio_get_cc(chip);

	bcma_chipco_gpio_out(cc, 1 << gpio, value ? 1 << gpio : 0);
}

static int bcma_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	struct bcma_drv_cc *cc = bcma_gpio_get_cc(chip);

	bcma_chipco_gpio_outen(cc, 1 << gpio, 0);
	return 0;
}

static int bcma_gpio_direction_output(struct gpio_chip *chip, unsigned gpio,
				      int value)
{
	struct bcma_drv_cc *cc = bcma_gpio_get_cc(chip);

	bcma_chipco_gpio_outen(cc, 1 << gpio, 1 << gpio);
	bcma_chipco_gpio_out(cc, 1 << gpio, value ? 1 << gpio : 0);
	return 0;
}

static int bcma_gpio_request(struct gpio_chip *chip, unsigned gpio)
{
	struct bcma_drv_cc *cc = bcma_gpio_get_cc(chip);

	bcma_chipco_gpio_control(cc, 1 << gpio, 0);
	/* clear pulldown */
	bcma_chipco_gpio_pulldown(cc, 1 << gpio, 0);
	/* Set pullup */
	bcma_chipco_gpio_pullup(cc, 1 << gpio, 1 << gpio);

	return 0;
}

static void bcma_gpio_free(struct gpio_chip *chip, unsigned gpio)
{
	struct bcma_drv_cc *cc = bcma_gpio_get_cc(chip);

	/* clear pullup */
	bcma_chipco_gpio_pullup(cc, 1 << gpio, 0);
}

static int bcma_gpio_to_irq(struct gpio_chip *chip, unsigned gpio)
{
	struct bcma_drv_cc *cc = bcma_gpio_get_cc(chip);

	if (cc->core->bus->hosttype == BCMA_HOSTTYPE_SOC)
		return bcma_core_irq(cc->core);
	else
		return -EINVAL;
}

int bcma_gpio_init(struct bcma_drv_cc *cc)
{
	struct gpio_chip *chip = &cc->gpio;

	chip->label		= "bcma_gpio";
	chip->owner		= THIS_MODULE;
	chip->request		= bcma_gpio_request;
	chip->free		= bcma_gpio_free;
	chip->get		= bcma_gpio_get_value;
	chip->set		= bcma_gpio_set_value;
	chip->direction_input	= bcma_gpio_direction_input;
	chip->direction_output	= bcma_gpio_direction_output;
	chip->to_irq		= bcma_gpio_to_irq;
	chip->ngpio		= 16;
	/* There is just one SoC in one device and its GPIO addresses should be
	 * deterministic to address them more easily. The other buses could get
	 * a random base number. */
	if (cc->core->bus->hosttype == BCMA_HOSTTYPE_SOC)
		chip->base		= 0;
	else
		chip->base		= -1;

	return gpiochip_add(chip);
}

int bcma_gpio_unregister(struct bcma_drv_cc *cc)
{
	return gpiochip_remove(&cc->gpio);
}
