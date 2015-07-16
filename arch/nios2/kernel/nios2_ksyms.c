/*
 * Copyright (C) 2004 Microtronix Datacom Ltd
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 */

#include <linux/export.h>
#include <linux/string.h>

/* string functions */

EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memmove);

/*
 * libgcc functions - functions that are used internally by the
 * compiler...  (prototypes are not correct though, but that
 * doesn't really matter since they're not versioned).
 */
#define DECLARE_EXPORT(name)	extern void name(void); EXPORT_SYMBOL(name)

DECLARE_EXPORT(__gcc_bcmp);
DECLARE_EXPORT(__divsi3);
DECLARE_EXPORT(__moddi3);
DECLARE_EXPORT(__modsi3);
DECLARE_EXPORT(__udivmoddi4);
DECLARE_EXPORT(__udivsi3);
DECLARE_EXPORT(__umoddi3);
DECLARE_EXPORT(__umodsi3);
DECLARE_EXPORT(__muldi3);
