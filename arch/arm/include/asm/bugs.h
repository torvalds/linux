/*
 *  arch/arm/include/asm/bugs.h
 *
 *  Copyright (C) 1995-2003 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_BUGS_H
#define __ASM_BUGS_H

extern void check_writebuffer_bugs(void);

#ifdef CONFIG_MMU
extern void check_bugs(void);
extern void check_other_bugs(void);
#else
#define check_bugs() do { } while (0)
#define check_other_bugs() do { } while (0)
#endif

#endif
