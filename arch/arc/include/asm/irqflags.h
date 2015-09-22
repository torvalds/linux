/*
 * Copyright (C) 2014-15 Synopsys, Inc. (www.synopsys.com)
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARC_IRQFLAGS_H
#define __ASM_ARC_IRQFLAGS_H

#ifdef CONFIG_ISA_ARCOMPACT
#include <asm/irqflags-compact.h>
#else
#include <asm/irqflags-arcv2.h>
#endif

#endif
