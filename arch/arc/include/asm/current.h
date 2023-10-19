/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * Vineetg: May 16th, 2008
 *  - Current macro is now implemented as "global register" r25
 */

#ifndef _ASM_ARC_CURRENT_H
#define _ASM_ARC_CURRENT_H

#ifndef __ASSEMBLY__

#ifdef CONFIG_ARC_CURR_IN_REG

register struct task_struct *curr_arc asm("r25");
#define current (curr_arc)

#else
#include <asm-generic/current.h>
#endif /* ! CONFIG_ARC_CURR_IN_REG */

#endif /* ! __ASSEMBLY__ */

#endif /* _ASM_ARC_CURRENT_H */
