#ifndef __BACKPORT_LINUX_KCONFIG_H
#define __BACKPORT_LINUX_KCONFIG_H
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0)
#include_next <linux/kconfig.h>
#endif

#ifndef __ARG_PLACEHOLDER_1
#define __ARG_PLACEHOLDER_1 0,
#define config_enabled(cfg) _config_enabled(cfg)
#define _config_enabled(value) __config_enabled(__ARG_PLACEHOLDER_##value)
#define __config_enabled(arg1_or_junk) ___config_enabled(arg1_or_junk 1, 0)
#define ___config_enabled(__ignored, val, ...) val

/*
 * 3.1 - 3.3 had a broken version of this, so undef
 * (they didn't have __ARG_PLACEHOLDER_1)
 */
#undef IS_ENABLED
#define IS_ENABLED(option) \
        (config_enabled(option) || config_enabled(option##_MODULE))
#endif

#undef IS_BUILTIN
#define IS_BUILTIN(option) config_enabled(option)

#endif
