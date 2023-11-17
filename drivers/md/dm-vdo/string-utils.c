// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "string-utils.h"

#include "errors.h"
#include "logger.h"
#include "memory-alloc.h"
#include "permassert.h"
#include "uds.h"

char *uds_append_to_buffer(char *buffer, char *buf_end, const char *fmt, ...)
{
	va_list args;
	size_t n;

	va_start(args, fmt);
	n = vsnprintf(buffer, buf_end - buffer, fmt, args);
	if (n >= (size_t) (buf_end - buffer))
		buffer = buf_end;
	else
		buffer += n;
	va_end(args);

	return buffer;
}
