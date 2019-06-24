// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 Zihao Yu
 */

#include <linux/export.h>
#include <linux/uaccess.h>

/*
 * Assembly functions that may be used (directly or indirectly) by modules
 */
EXPORT_SYMBOL(__clear_user);
EXPORT_SYMBOL(__asm_copy_to_user);
EXPORT_SYMBOL(__asm_copy_from_user);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memcpy);
