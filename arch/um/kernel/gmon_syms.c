// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <linux/module.h>

extern void __bb_init_func(void *)  __attribute__((weak));
EXPORT_SYMBOL(__bb_init_func);

extern void __gcov_init(void *)  __attribute__((weak));
EXPORT_SYMBOL(__gcov_init);
extern void __gcov_merge_add(void *, unsigned int)  __attribute__((weak));
EXPORT_SYMBOL(__gcov_merge_add);
extern void __gcov_exit(void)  __attribute__((weak));
EXPORT_SYMBOL(__gcov_exit);
