/* bug in tracepoint.h, it should include this */
#include <linux/module.h>

/* sparse isn't too happy with all macros... */
#ifndef __CHECKER__
#include <net/cfg80211.h>
#include "driver-ops.h"
#include "debug.h"
#define CREATE_TRACE_POINTS
#include "trace.h"

#ifdef CONFIG_MAC80211_MESSAGE_TRACING
void __sdata_info(const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args, args2;

	va_start(args, fmt);

	va_copy(args2, args);
	vaf.va = &args2;
	pr_info("%pV", &vaf);
	va_end(args2);

	vaf.va = &args;
	trace_mac80211_info(&vaf);
	va_end(args);
}

void __sdata_dbg(bool print, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);

	if (print) {
		va_list args2;

		va_copy(args2, args);
		vaf.va = &args2;
		pr_debug("%pV", &vaf);
		va_end(args2);
	}
	vaf.va = &args;
	trace_mac80211_dbg(&vaf);
	va_end(args);
}

void __sdata_err(const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args, args2;

	va_start(args, fmt);

	va_copy(args2, args);
	vaf.va = &args2;
	pr_err("%pV", &vaf);
	va_end(args2);

	vaf.va = &args;
	trace_mac80211_err(&vaf);
	va_end(args);
}

void __wiphy_dbg(struct wiphy *wiphy, bool print, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);

	if (print) {
		va_list args2;

		va_copy(args2, args);
		vaf.va = &args2;
		pr_debug("%pV", &vaf);
		va_end(args2);
	}
	vaf.va = &args;
	trace_mac80211_dbg(&vaf);
	va_end(args);
}
#endif
#endif
