/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  include/asm-m68k/s.h
 *
 *  Copyright (C) 1994  Linus Torvalds
 */

/*
 * This is included by init/main.c to check for architecture-dependent s.
 *
 * Needs:
 *	void check_s(void);
 */

#ifdef CONFIG_MMU
extern void check_s(void);	/* in arch/m68k/kernel/setup.c */
#else
static void check_s(void)
{
}
#endif
