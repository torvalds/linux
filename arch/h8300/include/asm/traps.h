/*
 *  linux/include/asm-h8300/traps.h
 *
 *  Copyright (C) 2003 Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#ifndef _H8300_TRAPS_H
#define _H8300_TRAPS_H

extern void system_call(void);
extern void interrupt_entry(void);
extern void trace_break(void);

#define JMP_OP 0x5a000000
#define JSR_OP 0x5e000000
#define VECTOR(address) ((JMP_OP)|((unsigned long)address))
#define REDIRECT(address) ((JSR_OP)|((unsigned long)address))

#define TRACE_VEC 5

#define TRAP0_VEC 8
#define TRAP1_VEC 9
#define TRAP2_VEC 10
#define TRAP3_VEC 11

#if defined(__H8300H__)
#define NR_TRAPS 12
#endif
#if defined(__H8300S__)
#define NR_TRAPS 16
#endif

#endif /* _H8300_TRAPS_H */
