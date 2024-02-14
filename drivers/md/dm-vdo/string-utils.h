/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_STRING_UTILS_H
#define VDO_STRING_UTILS_H

#include <linux/kernel.h>
#include <linux/string.h>

/* Utilities related to string manipulation */

static inline const char *vdo_bool_to_string(bool value)
{
	return value ? "true" : "false";
}

/* Append a formatted string to the end of a buffer. */
char *vdo_append_to_buffer(char *buffer, char *buf_end, const char *fmt, ...)
	__printf(3, 4);

#endif /* VDO_STRING_UTILS_H */
