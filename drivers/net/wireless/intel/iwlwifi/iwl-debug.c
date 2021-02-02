// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2005-2011 Intel Corporation
 */
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/export.h>
#include "iwl-drv.h"
#include "iwl-debug.h"
#include "iwl-devtrace.h"

#define __iwl_fn(fn)						\
void __iwl_ ##fn(struct device *dev, const char *fmt, ...)	\
{								\
	struct va_format vaf = {				\
		.fmt = fmt,					\
	};							\
	va_list args;						\
								\
	va_start(args, fmt);					\
	vaf.va = &args;						\
	dev_ ##fn(dev, "%pV", &vaf);				\
	trace_iwlwifi_ ##fn(&vaf);				\
	va_end(args);						\
}

__iwl_fn(warn)
IWL_EXPORT_SYMBOL(__iwl_warn);
__iwl_fn(info)
IWL_EXPORT_SYMBOL(__iwl_info);
__iwl_fn(crit)
IWL_EXPORT_SYMBOL(__iwl_crit);

void __iwl_err(struct device *dev, bool rfkill_prefix, bool trace_only,
		const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	if (!trace_only) {
		if (rfkill_prefix)
			dev_err(dev, "(RFKILL) %pV", &vaf);
		else
			dev_err(dev, "%pV", &vaf);
	}
	trace_iwlwifi_err(&vaf);
	va_end(args);
}
IWL_EXPORT_SYMBOL(__iwl_err);

#if defined(CONFIG_IWLWIFI_DEBUG) || defined(CONFIG_IWLWIFI_DEVICE_TRACING)
void __iwl_dbg(struct device *dev,
	       u32 level, bool limit, const char *function,
	       const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
#ifdef CONFIG_IWLWIFI_DEBUG
	if (iwl_have_debug_level(level) &&
	    (!limit || net_ratelimit()))
		dev_printk(KERN_DEBUG, dev, "%s %pV", function, &vaf);
#endif
	trace_iwlwifi_dbg(level, function, &vaf);
	va_end(args);
}
IWL_EXPORT_SYMBOL(__iwl_dbg);
#endif
