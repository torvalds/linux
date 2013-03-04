/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Vineetg: May 16th, 2008
 *  - Current macro is now implemented as "global register" r25
 */

#ifndef _ASM_ARC_CURRENT_H
#define _ASM_ARC_CURRENT_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

#ifdef CONFIG_ARC_CURR_IN_REG

register struct task_struct *curr_arc asm("r25");
#define current (curr_arc)

#else
#include <asm-generic/current.h>
#endif /* ! CONFIG_ARC_CURR_IN_REG */

#endif /* ! __ASSEMBLY__ */

#endif	/* __KERNEL__ */

#endif /* _ASM_ARC_CURRENT_H */
