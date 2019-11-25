// SPDX-License-Identifier: GPL-2.0
/* 
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <linux/module.h>

extern void mcount(void);
EXPORT_SYMBOL(mcount);
