/*
 * Linux console printing functions.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#include <linux/module.h>

#include "print.h"

void os_print(enum os_print_level level, const char *prefix, const char *name,
              const char *format, ...)
{
    va_list va_args;

    va_start(va_args, format);
    os_vprint(level, prefix, name, format, va_args);
    va_end(va_args);
}
EXPORT_SYMBOL(os_print);

void os_vprint(enum os_print_level level, const char *prefix, const char *name,
               const char *format, va_list args)
{
    const char *level_str[] = {
        [OS_PRINT_ERROR]   = KERN_ERR,
        [OS_PRINT_WARNING] = KERN_WARNING,
        [OS_PRINT_INFO]    = KERN_INFO,
        [OS_PRINT_DEBUG]   = KERN_DEBUG,
    };
    char buf[80];
    int w = 0;

    if (name) {
        w += snprintf(buf + w, sizeof(buf) - w, "%s%s%s: ", level_str[level], prefix, name);
    } else {
        w += snprintf(buf + w, sizeof(buf) - w, "%s%s", level_str[level], prefix);
    }
    w += vsnprintf(buf + w, sizeof(buf) - w, format, args);
    printk("%s\n", buf);
}
EXPORT_SYMBOL(os_vprint);
