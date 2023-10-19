/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/bugs.h
 *
 *  Copyright (C) 1995-2003 Russell King
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
