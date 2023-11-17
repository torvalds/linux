/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef UDS_STRING_UTILS_H
#define UDS_STRING_UTILS_H

#include <linux/kernel.h>
#include <linux/string.h>

/* Utilities related to string manipulation */

static inline const char *uds_bool_to_string(bool value)
{
	return value ? "true" : "false";
}

/* Append a formatted string to the end of a buffer. */
char *uds_append_to_buffer(char *buffer, char *buf_end, const char *fmt, ...)
	__printf(3, 4);

#endif /* UDS_STRING_UTILS_H */
