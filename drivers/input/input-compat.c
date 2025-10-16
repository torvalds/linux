// SPDX-License-Identifier: GPL-2.0-only
/*
 * 32bit compatibility wrappers for the input subsystem.
 *
 * Very heavily based on evdev.c - Copyright (c) 1999-2002 Vojtech Pavlik
 */

#include <linux/export.h>
#include <linux/sprintf.h>
#include <linux/uaccess.h>
#include "input-compat.h"

#ifdef CONFIG_COMPAT

int input_event_from_user(const char __user *buffer,
			  struct input_event *event)
{
	if (in_compat_syscall() && !COMPAT_USE_64BIT_TIME) {
		struct input_event_compat compat_event;

		if (copy_from_user(&compat_event, buffer,
				   sizeof(struct input_event_compat)))
			return -EFAULT;

		event->input_event_sec = compat_event.sec;
		event->input_event_usec = compat_event.usec;
		event->type = compat_event.type;
		event->code = compat_event.code;
		event->value = compat_event.value;

	} else {
		if (copy_from_user(event, buffer, sizeof(struct input_event)))
			return -EFAULT;
	}

	return 0;
}

int input_event_to_user(char __user *buffer,
			const struct input_event *event)
{
	if (in_compat_syscall() && !COMPAT_USE_64BIT_TIME) {
		struct input_event_compat compat_event;

		compat_event.sec = event->input_event_sec;
		compat_event.usec = event->input_event_usec;
		compat_event.type = event->type;
		compat_event.code = event->code;
		compat_event.value = event->value;

		if (copy_to_user(buffer, &compat_event,
				 sizeof(struct input_event_compat)))
			return -EFAULT;

	} else {
		if (copy_to_user(buffer, event, sizeof(struct input_event)))
			return -EFAULT;
	}

	return 0;
}

int input_ff_effect_from_user(const char __user *buffer, size_t size,
			      struct ff_effect *effect)
{
	if (in_compat_syscall()) {
		struct ff_effect_compat *compat_effect;

		if (size != sizeof(struct ff_effect_compat))
			return -EINVAL;

		/*
		 * It so happens that the pointer which needs to be changed
		 * is the last field in the structure, so we can retrieve the
		 * whole thing and replace just the pointer.
		 */
		compat_effect = (struct ff_effect_compat *)effect;

		if (copy_from_user(compat_effect, buffer,
				   sizeof(struct ff_effect_compat)))
			return -EFAULT;

		if (compat_effect->type == FF_PERIODIC &&
		    compat_effect->u.periodic.waveform == FF_CUSTOM)
			effect->u.periodic.custom_data =
				compat_ptr(compat_effect->u.periodic.custom_data);
	} else {
		if (size != sizeof(struct ff_effect))
			return -EINVAL;

		if (copy_from_user(effect, buffer, sizeof(struct ff_effect)))
			return -EFAULT;
	}

	return 0;
}

int input_bits_to_string(char *buf, int buf_size, unsigned long bits,
			 bool skip_empty)
{
	int len = 0;

	if (in_compat_syscall()) {
		u32 dword = bits >> 32;
		if (dword || !skip_empty)
			len += snprintf(buf, buf_size, "%x ", dword);

		dword = bits & 0xffffffffUL;
		if (dword || !skip_empty || len)
			len += snprintf(buf + len, max(buf_size - len, 0),
					"%x", dword);
	} else {
		if (bits || !skip_empty)
			len += snprintf(buf, buf_size, "%lx", bits);
	}

	return len;
}

#else

int input_event_from_user(const char __user *buffer,
			 struct input_event *event)
{
	if (copy_from_user(event, buffer, sizeof(struct input_event)))
		return -EFAULT;

	return 0;
}

int input_event_to_user(char __user *buffer,
			const struct input_event *event)
{
	if (copy_to_user(buffer, event, sizeof(struct input_event)))
		return -EFAULT;

	return 0;
}

int input_ff_effect_from_user(const char __user *buffer, size_t size,
			      struct ff_effect *effect)
{
	if (size != sizeof(struct ff_effect))
		return -EINVAL;

	if (copy_from_user(effect, buffer, sizeof(struct ff_effect)))
		return -EFAULT;

	return 0;
}

int input_bits_to_string(char *buf, int buf_size, unsigned long bits,
			 bool skip_empty)
{
	return bits || !skip_empty ?
		snprintf(buf, buf_size, "%lx", bits) : 0;
}

#endif /* CONFIG_COMPAT */

EXPORT_SYMBOL_GPL(input_event_from_user);
EXPORT_SYMBOL_GPL(input_event_to_user);
EXPORT_SYMBOL_GPL(input_ff_effect_from_user);
