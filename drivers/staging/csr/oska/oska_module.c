/*
 * Linux kernel module support.
 *
 * Copyright (C) 2010 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#include <linux/module.h>

#include "all.h"
#include "refcount.h"

EXPORT_SYMBOL(os_refcount_init);
EXPORT_SYMBOL(os_refcount_destroy);
EXPORT_SYMBOL(os_refcount_get);
EXPORT_SYMBOL(os_refcount_put);

MODULE_DESCRIPTION("Operating System Kernel Abstraction");
MODULE_AUTHOR("Cambridge Silicon Radio Ltd.");
MODULE_LICENSE("GPL and additional rights");
