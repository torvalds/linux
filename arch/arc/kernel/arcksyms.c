/*
 * arcksyms.c - Exporting symbols not exportable from their own sources
 *
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>

/* libgcc functions, not part of kernel sources */
extern void __ashldi3(void);
extern void __ashrdi3(void);
extern void __divsi3(void);
extern void __divsf3(void);
extern void __lshrdi3(void);
extern void __modsi3(void);
extern void __muldi3(void);
extern void __ucmpdi2(void);
extern void __udivsi3(void);
extern void __umodsi3(void);
extern void __cmpdi2(void);
extern void __fixunsdfsi(void);
extern void __muldf3(void);
extern void __divdf3(void);
extern void __floatunsidf(void);
extern void __floatunsisf(void);
extern void __udivdi3(void);

EXPORT_SYMBOL(__ashldi3);
EXPORT_SYMBOL(__ashrdi3);
EXPORT_SYMBOL(__divsi3);
EXPORT_SYMBOL(__divsf3);
EXPORT_SYMBOL(__lshrdi3);
EXPORT_SYMBOL(__modsi3);
EXPORT_SYMBOL(__muldi3);
EXPORT_SYMBOL(__ucmpdi2);
EXPORT_SYMBOL(__udivsi3);
EXPORT_SYMBOL(__umodsi3);
EXPORT_SYMBOL(__cmpdi2);
EXPORT_SYMBOL(__fixunsdfsi);
EXPORT_SYMBOL(__muldf3);
EXPORT_SYMBOL(__divdf3);
EXPORT_SYMBOL(__floatunsidf);
EXPORT_SYMBOL(__floatunsisf);
EXPORT_SYMBOL(__udivdi3);

/* ARC optimised assembler routines */
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL(strchr);
EXPORT_SYMBOL(strcpy);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strlen);
