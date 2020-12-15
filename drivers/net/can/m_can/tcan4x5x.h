/* SPDX-License-Identifier: GPL-2.0
 *
 * tcan4x5x - Texas Instruments TCAN4x5x Family CAN controller driver
 *
 * Copyright (c) 2020 Pengutronix,
 *                    Marc Kleine-Budde <kernel@pengutronix.de>
 */

#ifndef _TCAN4X5X_H
#define _TCAN4X5X_H

#include <linux/gpio/consumer.h>
#include <linux/regmap.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include "m_can.h"

struct tcan4x5x_priv {
	struct m_can_classdev cdev;

	struct regmap *regmap;
	struct spi_device *spi;

	struct gpio_desc *reset_gpio;
	struct gpio_desc *device_wake_gpio;
	struct gpio_desc *device_state_gpio;
	struct regulator *power;
};

int tcan4x5x_regmap_init(struct tcan4x5x_priv *priv);

#endif
