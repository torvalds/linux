/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASMARC_SETUP_H
#define __ASMARC_SETUP_H

#include <linux/types.h>

#define COMMAND_LINE_SIZE 256

extern int root_mountflags, end_mem;
extern int running_on_hw;

void __init setup_processor(void);
void __init setup_arch_memory(void);

#endif /* __ASMARC_SETUP_H */
