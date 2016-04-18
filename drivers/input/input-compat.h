#ifndef _INPUT_COMPAT_H
#define _INPUT_COMPAT_H

/*
 * 32bit compatibility wrappers for the input subsystem.
 *
 * Very heavily based on evdev.c - Copyright (c) 1999-2002 Vojtech Pavlik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/compiler.h>
#include <linux/compat.h>
#include <linux/input.h>

#ifdef CONFIG_COMPAT

struct input_event_compat {
	struct compat_timeval time;
	__u16 type;
	__u16 code;
	__s32 value;
};

struct ff_periodic_effect_compat {
	__u16 waveform;
	__u16 period;
	__s16 magnitude;
	__s16 offset;
	__u16 phase;

	struct ff_envelope envelope;

	__u32 custom_len;
	compat_uptr_t custom_data;
};

struct ff_effect_compat {
	__u16 type;
	__s16 id;
	__u16 direction;
	struct ff_trigger trigger;
	struct ff_replay replay;

	union {
		struct ff_constant_effect constant;
		struct ff_ramp_effect ramp;
		struct ff_periodic_effect_compat periodic;
		struct ff_condition_effect condition[2]; /* One for each axis */
		struct ff_rumble_effect rumble;
	} u;
};

static inline size_t input_event_size(void)
{
	return (in_compat_syscall() && !COMPAT_USE_64BIT_TIME) ?
		sizeof(struct input_event_compat) : sizeof(struct input_event);
}

#else

static inline size_t input_event_size(void)
{
	return sizeof(struct input_event);
}

#endif /* CONFIG_COMPAT */

int input_event_from_user(const char __user *buffer,
			 struct input_event *event);

int input_event_to_user(char __user *buffer,
			const struct input_event *event);

int input_ff_effect_from_user(const char __user *buffer, size_t size,
			      struct ff_effect *effect);

#endif /* _INPUT_COMPAT_H */
