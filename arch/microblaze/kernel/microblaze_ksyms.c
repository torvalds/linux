/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/cryptohash.h>
#include <linux/delay.h>
#include <linux/in6.h>
#include <linux/syscalls.h>

#include <asm/checksum.h>
#include <linux/io.h>
#include <asm/page.h>
#include <asm/system.h>
#include <linux/uaccess.h>

/*
 * libgcc functions - functions that are used internally by the
 * compiler... (prototypes are not correct though, but that
 * doesn't really matter since they're not versioned).
 */
extern void __ashldi3(void);
EXPORT_SYMBOL(__ashldi3);
extern void __ashrdi3(void);
EXPORT_SYMBOL(__ashrdi3);
extern void __divsi3(void);
EXPORT_SYMBOL(__divsi3);
extern void __lshrdi3(void);
EXPORT_SYMBOL(__lshrdi3);
extern void __modsi3(void);
EXPORT_SYMBOL(__modsi3);
extern void __mulsi3(void);
EXPORT_SYMBOL(__mulsi3);
extern void __muldi3(void);
EXPORT_SYMBOL(__muldi3);
extern void __ucmpdi2(void);
EXPORT_SYMBOL(__ucmpdi2);
extern void __udivsi3(void);
EXPORT_SYMBOL(__udivsi3);
extern void __umodsi3(void);
EXPORT_SYMBOL(__umodsi3);
extern char *_ebss;
EXPORT_SYMBOL_GPL(_ebss);
