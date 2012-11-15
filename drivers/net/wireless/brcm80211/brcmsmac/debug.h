#ifndef _BRCMS_DEBUG_H_
#define _BRCMS_DEBUG_H_

#include <linux/device.h>
#include <linux/bcma/bcma.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include "main.h"
#include "mac80211_if.h"

void __brcms_info(struct device *dev, const char *fmt, ...);
void __brcms_warn(struct device *dev, const char *fmt, ...);
void __brcms_err(struct device *dev, const char *fmt, ...);
void __brcms_crit(struct device *dev, const char *fmt, ...);

#if defined(CONFIG_BRCMDBG) || defined(CONFIG_BRCM_TRACING)
void __brcms_dbg(struct device *dev, u32 level, const char *func,
		 const char *fmt, ...);
#else
static inline void __brcms_dbg(struct device *dev, u32 level,
			       const char *func, const char *fmt, ...)
{
}
#endif

/*
 * Debug macros cannot be used when wlc is uninitialized. Generally
 * this means any code that could run before brcms_c_attach() has
 * returned successfully probably shouldn't use the following macros.
 */

#define brcms_dbg(core, l, f, a...)	__brcms_dbg(&(core)->dev, l, __func__, f, ##a)
#define brcms_info(core, f, a...)	__brcms_info(&(core)->dev, f, ##a)
#define brcms_warn(core, f, a...)	__brcms_warn(&(core)->dev, f, ##a)
#define brcms_err(core, f, a...)	__brcms_err(&(core)->dev, f, ##a)
#define brcms_crit(core, f, a...)	__brcms_crit(&(core)->dev, f, ##a)

#define brcms_dbg_info(core, f, a...)	brcms_dbg(core, BRCM_DL_INFO, f, ##a)

#endif /* _BRCMS_DEBUG_H_ */
