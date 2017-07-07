/* MN10300 Miscellaneous and library kernel exports
 *
 * Copyright (C) 2007 Matsushita Electric Industrial Co., Ltd.
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/uaccess.h>
#include <asm/pgtable.h>


EXPORT_SYMBOL(empty_zero_page);

EXPORT_SYMBOL(change_bit);
EXPORT_SYMBOL(test_and_change_bit);

EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memset);

EXPORT_SYMBOL(strncpy_from_user);
EXPORT_SYMBOL(clear_user);
EXPORT_SYMBOL(__clear_user);
EXPORT_SYMBOL(strnlen_user);

extern u64 __ashrdi3(u64, unsigned);
extern u64 __ashldi3(u64, unsigned);
extern u64 __lshrdi3(u64, unsigned);
extern s64 __negdi2(s64);
extern int __ucmpdi2(u64, u64);
EXPORT_SYMBOL(__ashrdi3);
EXPORT_SYMBOL(__ashldi3);
EXPORT_SYMBOL(__lshrdi3);
EXPORT_SYMBOL(__negdi2);
EXPORT_SYMBOL(__ucmpdi2);
