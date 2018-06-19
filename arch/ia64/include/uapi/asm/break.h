/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_IA64_BREAK_H
#define _ASM_IA64_BREAK_H

/*
 * IA-64 Linux break numbers.
 *
 * Copyright (C) 1999 Hewlett-Packard Co
 * Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */

/*
 * OS-specific debug break numbers:
 */
#define __IA64_BREAK_KDB		0x80100
#define __IA64_BREAK_KPROBE		0x81000 /* .. 0x81fff */

/*
 * OS-specific break numbers:
 */
#define __IA64_BREAK_SYSCALL		0x100000

#endif /* _ASM_IA64_BREAK_H */
