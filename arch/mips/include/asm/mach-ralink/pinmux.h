/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 *  Copyright (C) 2012 John Crispin <john@phrozen.org>
 */

#ifndef _RT288X_PINMUX_H__
#define _RT288X_PINMUX_H__

#define FUNC(name, value, pin_first, pin_count) \
	{ name, value, pin_first, pin_count }

#define GRP(_name, _func, _mask, _shift) \
	{ .name = _name, .mask = _mask, .shift = _shift, \
	  .func = _func, .gpio = _mask, \
	  .func_count = ARRAY_SIZE(_func) }

#define GRP_G(_name, _func, _mask, _gpio, _shift) \
	{ .name = _name, .mask = _mask, .shift = _shift, \
	  .func = _func, .gpio = _gpio, \
	  .func_count = ARRAY_SIZE(_func) }

struct rt2880_pmx_group;

struct rt2880_pmx_func {
	const char *name;
	const char value;

	int pin_first;
	int pin_count;
	int *pins;

	int *groups;
	int group_count;

	int enabled;
};

struct rt2880_pmx_group {
	const char *name;
	int enabled;

	const u32 shift;
	const char mask;
	const char gpio;

	struct rt2880_pmx_func *func;
	int func_count;
};

extern struct rt2880_pmx_group *rt2880_pinmux_data;

#endif
