// SPDX-License-Identifier: GPL-2.0
/*
 * Chipidea Device Mode Trace Support
 *
 * Copyright (C) 2020 NXP
 *
 * Author: Peter Chen <peter.chen@nxp.com>
 */

#define CREATE_TRACE_POINTS
#include "trace.h"

void ci_log(struct ci_hdrc *ci, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	trace_ci_log(ci, &vaf);
	va_end(args);
}
