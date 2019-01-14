/*
 * Copyright (C) 2017 Zihao Yu
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
