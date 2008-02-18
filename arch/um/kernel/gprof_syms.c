/* 
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include "linux/module.h"

extern void mcount(void);
EXPORT_SYMBOL(mcount);
