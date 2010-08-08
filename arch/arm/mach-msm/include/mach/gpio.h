/*
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2009-2010, Code Aurora Forum. All rights reserved.
 * Author: Mike Lockwood <lockwood@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __ASM_ARCH_MSM_GPIO_H
#define __ASM_ARCH_MSM_GPIO_H

#include <asm-generic/gpio.h>

#define gpio_get_value  __gpio_get_value
#define gpio_set_value  __gpio_set_value
#define gpio_cansleep   __gpio_cansleep
#define gpio_to_irq     __gpio_to_irq

/**
 * struct msm_gpio - GPIO pin description
 * @gpio_cfg - configuration bitmap, as per gpio_tlmm_config()
 * @label - textual label
 *
 * Usually, GPIO's are operated by sets.
 * This struct accumulate all GPIO information in single source
 * and facilitete group operations provided by msm_gpios_xxx()
 */
struct msm_gpio {
	u32 gpio_cfg;
	const char *label;
};

/**
 * msm_gpios_request_enable() - request and enable set of GPIOs
 *
 * Request and configure set of GPIO's
 * In case of error, all operations rolled back.
 * Return error code.
 *
 * @table: GPIO table
 * @size:  number of entries in @table
 */
int msm_gpios_request_enable(const struct msm_gpio *table, int size);

/**
 * msm_gpios_disable_free() - disable and free set of GPIOs
 *
 * @table: GPIO table
 * @size:  number of entries in @table
 */
void msm_gpios_disable_free(const struct msm_gpio *table, int size);

/**
 * msm_gpios_request() - request set of GPIOs
 * In case of error, all operations rolled back.
 * Return error code.
 *
 * @table: GPIO table
 * @size:  number of entries in @table
 */
int msm_gpios_request(const struct msm_gpio *table, int size);

/**
 * msm_gpios_free() - free set of GPIOs
 *
 * @table: GPIO table
 * @size:  number of entries in @table
 */
void msm_gpios_free(const struct msm_gpio *table, int size);

/**
 * msm_gpios_enable() - enable set of GPIOs
 * In case of error, all operations rolled back.
 * Return error code.
 *
 * @table: GPIO table
 * @size:  number of entries in @table
 */
int msm_gpios_enable(const struct msm_gpio *table, int size);

/**
 * msm_gpios_disable() - disable set of GPIOs
 *
 * @table: GPIO table
 * @size:  number of entries in @table
 */
void msm_gpios_disable(const struct msm_gpio *table, int size);

/* GPIO TLMM (Top Level Multiplexing) Definitions */

/* GPIO TLMM: Function -- GPIO specific */

/* GPIO TLMM: Direction */
enum {
	GPIO_INPUT,
	GPIO_OUTPUT,
};

/* GPIO TLMM: Pullup/Pulldown */
enum {
	GPIO_NO_PULL,
	GPIO_PULL_DOWN,
	GPIO_KEEPER,
	GPIO_PULL_UP,
};

/* GPIO TLMM: Drive Strength */
enum {
	GPIO_2MA,
	GPIO_4MA,
	GPIO_6MA,
	GPIO_8MA,
	GPIO_10MA,
	GPIO_12MA,
	GPIO_14MA,
	GPIO_16MA,
};

enum {
	GPIO_ENABLE,
	GPIO_DISABLE,
};

#define GPIO_CFG(gpio, func, dir, pull, drvstr) \
	((((gpio) & 0x3FF) << 4)        |	  \
	 ((func) & 0xf)                  |	  \
	 (((dir) & 0x1) << 14)           |	  \
	 (((pull) & 0x3) << 15)          |	  \
	 (((drvstr) & 0xF) << 17))

/**
 * extract GPIO pin from bit-field used for gpio_tlmm_config
 */
#define GPIO_PIN(gpio_cfg)    (((gpio_cfg) >>  4) & 0x3ff)
#define GPIO_FUNC(gpio_cfg)   (((gpio_cfg) >>  0) & 0xf)
#define GPIO_DIR(gpio_cfg)    (((gpio_cfg) >> 14) & 0x1)
#define GPIO_PULL(gpio_cfg)   (((gpio_cfg) >> 15) & 0x3)
#define GPIO_DRVSTR(gpio_cfg) (((gpio_cfg) >> 17) & 0xf)

int gpio_tlmm_config(unsigned config, unsigned disable);

#endif /* __ASM_ARCH_MSM_GPIO_H */
