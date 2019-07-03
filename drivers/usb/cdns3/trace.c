// SPDX-License-Identifier: GPL-2.0
/*
 * USBSS device controller driver Trace Support
 *
 * Copyright (C) 2018 Cadence.
 *
 * Author: Pawel Laszczak <pawell@cadence.com>
 */

#define CREATE_TRACE_POINTS
#include "trace.h"

void cdns3_dbg(struct cdns3_device *priv_dev, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	trace_cdns3_log(priv_dev, &vaf);
	va_end(args);
}
