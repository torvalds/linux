/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Inochi Amaoto <inochiama@outlook.com>
 */

#ifndef _PINCTRL_SOPHGO_CV18XX_H
#define _PINCTRL_SOPHGO_CV18XX_H

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf.h>

#include "pinctrl-sophgo.h"

enum cv1800_pin_io_type {
	IO_TYPE_1V8_ONLY = 0,
	IO_TYPE_1V8_OR_3V3 = 1,
	IO_TYPE_AUDIO = 2,
	IO_TYPE_ETH = 3
};

#define CV1800_PINCONF_AREA_SYS		0
#define CV1800_PINCONF_AREA_RTC		1

struct cv1800_pinmux {
	u16	offset;
	u8	area;
	u8	max;
};

struct cv1800_pinmux2 {
	u16	offset;
	u8	area;
	u8	max;
	u8	pfunc;
};

struct cv1800_pinconf {
	u16	offset;
	u8	area;
};

#define	CV1800_PIN_HAVE_MUX2		BIT(0)
#define CV1800_PIN_IO_TYPE		GENMASK(2, 1)

#define CV1800_PIN_FLAG_IO_TYPE(type)		\
	FIELD_PREP_CONST(CV1800_PIN_IO_TYPE, type)
struct cv1800_pin {
	struct sophgo_pin		pin;
	u8				power_domain;
	struct cv1800_pinmux		mux;
	struct cv1800_pinmux2		mux2;
	struct cv1800_pinconf		conf;
};

#define sophgo_to_cv1800_pin(_pin)		\
	container_of((_pin), struct cv1800_pin, pin)

#define PIN_POWER_STATE_1V8		1800
#define PIN_POWER_STATE_3V3		3300

static inline enum cv1800_pin_io_type cv1800_pin_io_type(const struct cv1800_pin *pin)
{
	return FIELD_GET(CV1800_PIN_IO_TYPE, pin->pin.flags);
};

extern const struct pinctrl_ops cv1800_pctrl_ops;
extern const struct pinmux_ops cv1800_pmx_ops;
extern const struct pinconf_ops cv1800_pconf_ops;
extern const struct sophgo_cfg_ops cv1800_cfg_ops;

#define CV1800_FUNC_PIN(_id, _power_domain, _type,			\
			_mux_area, _mux_offset, _mux_func_max)		\
	{								\
		.pin = {						\
			.id = (_id),					\
			.flags = CV1800_PIN_FLAG_IO_TYPE(_type),	\
		},							\
		.power_domain = (_power_domain),			\
		.mux = {						\
			.area = (_mux_area),				\
			.offset = (_mux_offset),			\
			.max = (_mux_func_max),				\
		},							\
	}

#define CV1800_GENERAL_PIN(_id, _power_domain, _type,			\
			   _mux_area, _mux_offset, _mux_func_max,	\
			   _conf_area, _conf_offset)			\
	{								\
		.pin = {						\
			.id = (_id),					\
			.flags = CV1800_PIN_FLAG_IO_TYPE(_type),	\
		},							\
		.power_domain = (_power_domain),			\
		.mux = {						\
			.area = (_mux_area),				\
			.offset = (_mux_offset),			\
			.max = (_mux_func_max),				\
		},							\
		.conf = {						\
			.area = (_conf_area),				\
			.offset = (_conf_offset),			\
		},							\
	}

#define CV1800_GENERATE_PIN_MUX2(_id, _power_domain, _type,		\
				 _mux_area, _mux_offset, _mux_func_max,	\
				 _mux2_area, _mux2_offset,		\
				 _mux2_func_max,			\
				 _conf_area, _conf_offset)		\
	{								\
		.pin = {						\
			.id = (_id),					\
			.flags = CV1800_PIN_FLAG_IO_TYPE(_type) |	\
				 CV1800_PIN_HAVE_MUX2,			\
		},							\
		.power_domain = (_power_domain),			\
		.mux = {						\
			.area = (_mux_area),				\
			.offset = (_mux_offset),			\
			.max = (_mux_func_max),				\
		},							\
		.mux2 = {						\
			.area = (_mux2_area),				\
			.offset = (_mux2_offset),			\
			.max = (_mux2_func_max),			\
		},							\
		.conf = {						\
			.area = (_conf_area),				\
			.offset = (_conf_offset),			\
		},							\
	}

#endif
