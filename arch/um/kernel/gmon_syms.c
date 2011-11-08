/*
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include "linux/module.h"

extern void __bb_init_func(void *)  __attribute__((weak));
EXPORT_SYMBOL(__bb_init_func);
