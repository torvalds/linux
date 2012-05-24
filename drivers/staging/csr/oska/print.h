/*
 * OSKA Linux implementation -- console printing
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef __OSKA_LINUX_PRINT_H
#define __OSKA_LINUX_PRINT_H

#include <linux/kernel.h>

/**
 * Severity of a console or log message.
 *
 * @ingroup print
 */
enum os_print_level {
    OS_PRINT_ERROR,
    OS_PRINT_WARNING,
    OS_PRINT_INFO,
    OS_PRINT_DEBUG,
};

void os_print(enum os_print_level level, const char *prefix, const char *name,
              const char *format, ...);
void os_vprint(enum os_print_level level, const char *prefix, const char *name,
               const char *format, va_list args);


#endif /* #ifndef __OSKA_LINUX_PRINT_H */
