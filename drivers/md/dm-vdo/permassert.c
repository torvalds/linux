// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "permassert.h"

#include "errors.h"
#include "logger.h"

int vdo_assertion_failed(const char *expression_string, const char *file_name,
			 int line_number, const char *format, ...)
{
	va_list args;

	va_start(args, format);

	vdo_log_embedded_message(VDO_LOG_ERR, VDO_LOGGING_MODULE_NAME, "assertion \"",
				 format, args, "\" (%s) failed at %s:%d",
				 expression_string, file_name, line_number);
	vdo_log_backtrace(VDO_LOG_ERR);

	va_end(args);

	return UDS_ASSERTION_FAILED;
}
