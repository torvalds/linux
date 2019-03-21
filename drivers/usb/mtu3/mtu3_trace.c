// SPDX-License-Identifier: GPL-2.0
/**
 * mtu3_trace.c - trace support
 *
 * Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Chunfeng Yun <chunfeng.yun@mediatek.com>
 */

#define CREATE_TRACE_POINTS
#include "mtu3_trace.h"

void mtu3_dbg_trace(struct device *dev, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	trace_mtu3_log(dev, &vaf);
	va_end(args);
}
