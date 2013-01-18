/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_LINKAGE_H
#define __ASM_LINKAGE_H

#ifdef __ASSEMBLY__

/* Can't use the ENTRY macro in linux/linkage.h
 * gas considers ';' as comment vs. newline
 */
.macro ARC_ENTRY name
	.global \name
	.align 4
	\name:
.endm

.macro ARC_EXIT name
#define ASM_PREV_SYM_ADDR(name)  .-##name
	.size \ name, ASM_PREV_SYM_ADDR(\name)
.endm

#endif /* __ASSEMBLY__ */

#endif
