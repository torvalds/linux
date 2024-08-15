/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014-15 Synopsys, Inc. (www.synopsys.com)
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef __ASM_ARC_IRQFLAGS_H
#define __ASM_ARC_IRQFLAGS_H

#ifdef CONFIG_ISA_ARCOMPACT
#include <asm/irqflags-compact.h>
#else
#include <asm/irqflags-arcv2.h>
#endif

#endif
