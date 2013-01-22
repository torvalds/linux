/*
 * Copyright (C) 2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_ARC_PROM_H_
#define _ASM_ARC_PROM_H_

#define HAVE_ARCH_DEVTREE_FIXUPS
extern int __init setup_machine_fdt(void *dt);

#endif
