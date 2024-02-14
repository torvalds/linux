// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2013,2016 Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include "wil6210.h"
#include "trace.h"

void __wil_err(struct wil6210_priv *wil, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	netdev_err(wil->main_ndev, "%pV", &vaf);
	trace_wil6210_log_err(&vaf);
	va_end(args);
}

void __wil_err_ratelimited(struct wil6210_priv *wil, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	if (!net_ratelimit())
		return;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	netdev_err(wil->main_ndev, "%pV", &vaf);
	trace_wil6210_log_err(&vaf);
	va_end(args);
}

void wil_dbg_ratelimited(const struct wil6210_priv *wil, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	if (!net_ratelimit())
		return;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	netdev_dbg(wil->main_ndev, "%pV", &vaf);
	trace_wil6210_log_dbg(&vaf);
	va_end(args);
}

void __wil_info(struct wil6210_priv *wil, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	netdev_info(wil->main_ndev, "%pV", &vaf);
	trace_wil6210_log_info(&vaf);
	va_end(args);
}

void wil_dbg_trace(struct wil6210_priv *wil, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	trace_wil6210_log_dbg(&vaf);
	va_end(args);
}
