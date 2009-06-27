#ifndef _ASM_SCORE_UACCESS_H
#define _ASM_SCORE_UACCESS_H
/*
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */
struct pt_regs;
extern int fixup_exception(struct pt_regs *regs);

#ifndef __ASSEMBLY__

#define __range_ok(addr, size)					\
	((((unsigned long __force)(addr) >= 0x80000000)			\
	|| ((unsigned long)(size) > 0x80000000)			\
	|| (((unsigned long __force)(addr) + (unsigned long)(size)) > 0x80000000)))

#define __access_ok(addr, size) \
	(__range_ok((addr), (size)) == 0)

#include <asm-generic/uaccess.h>

#endif /* __ASSEMBLY__ */

#endif /* _ASM_SCORE_UACCESS_H */
