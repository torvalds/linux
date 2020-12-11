// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#include <linux/vmalloc.h>
#include "core.h"
#include "debug.h"

void ath11k_info(struct ath11k_base *ab, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	dev_info(ab->dev, "%pV", &vaf);
	/* TODO: Trace the log */
	va_end(args);
}
EXPORT_SYMBOL(ath11k_info);

void ath11k_err(struct ath11k_base *ab, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	dev_err(ab->dev, "%pV", &vaf);
	/* TODO: Trace the log */
	va_end(args);
}
EXPORT_SYMBOL(ath11k_err);

void ath11k_warn(struct ath11k_base *ab, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	dev_warn_ratelimited(ab->dev, "%pV", &vaf);
	/* TODO: Trace the log */
	va_end(args);
}
EXPORT_SYMBOL(ath11k_warn);

#ifdef CONFIG_ATH11K_DEBUG

void __ath11k_dbg(struct ath11k_base *ab, enum ath11k_debug_mask mask,
		  const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	if (ath11k_debug_mask & mask)
		dev_printk(KERN_DEBUG, ab->dev, "%pV", &vaf);

	/* TODO: trace log */

	va_end(args);
}
EXPORT_SYMBOL(__ath11k_dbg);

void ath11k_dbg_dump(struct ath11k_base *ab,
		     enum ath11k_debug_mask mask,
		     const char *msg, const char *prefix,
		     const void *buf, size_t len)
{
	char linebuf[256];
	size_t linebuflen;
	const void *ptr;

	if (ath11k_debug_mask & mask) {
		if (msg)
			__ath11k_dbg(ab, mask, "%s\n", msg);

		for (ptr = buf; (ptr - buf) < len; ptr += 16) {
			linebuflen = 0;
			linebuflen += scnprintf(linebuf + linebuflen,
						sizeof(linebuf) - linebuflen,
						"%s%08x: ",
						(prefix ? prefix : ""),
						(unsigned int)(ptr - buf));
			hex_dump_to_buffer(ptr, len - (ptr - buf), 16, 1,
					   linebuf + linebuflen,
					   sizeof(linebuf) - linebuflen, true);
			dev_printk(KERN_DEBUG, ab->dev, "%s\n", linebuf);
		}
	}
}
EXPORT_SYMBOL(ath11k_dbg_dump);

#endif /* CONFIG_ATH11K_DEBUG */
