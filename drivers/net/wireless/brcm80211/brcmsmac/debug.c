#include <linux/net.h>
#include "types.h"
#include "debug.h"
#include "brcms_trace_events.h"

#define __brcms_fn(fn)						\
void __brcms_ ##fn(struct device *dev, const char *fmt, ...)	\
{								\
	struct va_format vaf = {				\
		.fmt = fmt,					\
	};							\
	va_list args;						\
								\
	va_start(args, fmt);					\
	vaf.va = &args;						\
	dev_ ##fn(dev, "%pV", &vaf);				\
	trace_brcms_ ##fn(&vaf);				\
	va_end(args);						\
}

__brcms_fn(info)
__brcms_fn(warn)
__brcms_fn(err)
__brcms_fn(crit)

#if defined(CONFIG_BRCMDBG) || defined(CONFIG_BRCM_TRACING)
void __brcms_dbg(struct device *dev, u32 level, const char *func,
		 const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
#ifdef CONFIG_BRCMDBG
	if ((brcm_msg_level & level) && net_ratelimit())
		dev_err(dev, "%s %pV", func, &vaf);
#endif
	trace_brcms_dbg(level, func, &vaf);
	va_end(args);
}
#endif
