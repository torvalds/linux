/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  include/asm-m68k/bugs.h
 *
 *  Copyright (C) 1994  Linus Torvalds
 */

/*
 * This is included by init/main.c to check for architecture-dependent bugs.
 *
 * Needs:
 *	void check_bugs(void);
 */

#ifdef CONFIG_MMU
extern void check_bugs(void);	/* in arch/m68k/kernel/setup.c */
#else
static void check_bugs(void)
{
}
#endif
