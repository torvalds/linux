/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Yixun Lan <dlan@gentoo.org> */

#ifndef _PINCTRL_SPACEMIT_K1_H
#define _PINCTRL_SPACEMIT_K1_H

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf.h>

enum spacemit_pin_io_type {
	IO_TYPE_NONE = 0,
	IO_TYPE_1V8,
	IO_TYPE_3V3,
	IO_TYPE_EXTERNAL,
};

#define PIN_POWER_STATE_1V8		1800
#define PIN_POWER_STATE_3V3		3300

#define K1_PIN_IO_TYPE		GENMASK(2, 1)

#define K1_PIN_CAP_IO_TYPE(type)				\
	FIELD_PREP_CONST(K1_PIN_IO_TYPE, type)
#define K1_PIN_GET_IO_TYPE(val)					\
	FIELD_GET(K1_PIN_IO_TYPE, val)

#define K1_FUNC_PIN(_id, _gpiofunc, _io)			\
	{							\
		.pin		= (_id),			\
		.gpiofunc	= (_gpiofunc),			\
		.flags		= (K1_PIN_CAP_IO_TYPE(_io)),	\
	}

#endif /* _PINCTRL_SPACEMIT_K1_H */
