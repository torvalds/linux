/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Inochi Amaoto <inochiama@outlook.com>
 */

#ifndef _PINCTRL_SOPHGO_SG2042_H
#define _PINCTRL_SOPHGO_SG2042_H

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf.h>

#include "pinctrl-sophgo.h"

#define PIN_FLAG_DEFAULT			0
#define PIN_FLAG_WRITE_HIGH			BIT(0)
#define PIN_FLAG_ONLY_ONE_PULL			BIT(1)
#define PIN_FLAG_NO_PINMUX			BIT(2)
#define PIN_FLAG_NO_OEX_EN			BIT(3)
#define PIN_FLAG_IS_ETH				BIT(4)

struct sg2042_pin {
	struct sophgo_pin		pin;
	u16				offset;
};

#define sophgo_to_sg2042_pin(_pin)		\
	container_of((_pin), struct sg2042_pin, pin)

extern const struct pinctrl_ops sg2042_pctrl_ops;
extern const struct pinmux_ops sg2042_pmx_ops;
extern const struct pinconf_ops sg2042_pconf_ops;
extern const struct sophgo_cfg_ops sg2042_cfg_ops;

#define SG2042_GENERAL_PIN(_id,	_offset, _flag)				\
	{								\
		.pin = {						\
			.id = (_id),					\
			.flags = (_flag),				\
		},							\
		.offset = (_offset),					\
	}

#endif
