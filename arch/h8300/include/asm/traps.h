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

extern void _system_call(void);
extern void _interrupt_entry(void);
extern void _trace_break(void);
extern void _nmi(void);
extern void _interrupt_entry(void);

extern unsigned long *_interrupt_redirect_table;

#define JMP_OP 0x5a000000
#define JSR_OP 0x5e000000
#define VECTOR(address) ((JMP_OP)|((unsigned long)address))
#define REDIRECT(address) ((JSR_OP)|((unsigned long)address))
#define CPU_VECTOR ((unsigned long *)0x000000)
#define ADDR_MASK (0xffffff)

#define TRACE_VEC 5

#define TRAP0_VEC 8
#define TRAP1_VEC 9
#define TRAP2_VEC 10
#define TRAP3_VEC 11

extern char _start, _etext;
#define check_kernel_text(addr) \
	((addr >= (unsigned long)(&_start)) && \
	 (addr <  (unsigned long)(&_etext)) && !(addr & 1))

#endif /* _H8300_TRAPS_H */
