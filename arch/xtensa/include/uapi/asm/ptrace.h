/*
 * include/asm-xtensa/ptrace.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _UAPI_XTENSA_PTRACE_H
#define _UAPI_XTENSA_PTRACE_H

/*
 * Kernel stack
 *
 * 		+-----------------------+  -------- STACK_SIZE
 * 		|     register file     |  |
 * 		+-----------------------+  |
 * 		|    struct pt_regs     |  |
 * 		+-----------------------+  | ------ PT_REGS_OFFSET
 * double 	:  16 bytes spill area  :  |  ^
 * excetion 	:- - - - - - - - - - - -:  |  |
 * frame	:    struct pt_regs     :  |  |
 * 		:- - - - - - - - - - - -:  |  |
 * 		|                       |  |  |
 * 		|     memory stack      |  |  |
 * 		|                       |  |  |
 * 		~                       ~  ~  ~
 * 		~                       ~  ~  ~
 * 		|                       |  |  |
 * 		|                       |  |  |
 * 		+-----------------------+  |  | --- STACK_BIAS
 * 		|  struct task_struct   |  |  |  ^
 *  current --> +-----------------------+  |  |  |
 * 		|  struct thread_info   |  |  |  |
 *		+-----------------------+ --------
 */

#define KERNEL_STACK_SIZE (2 * PAGE_SIZE)

/*  Offsets for exception_handlers[] (3 x 64-entries x 4-byte tables). */

#define EXC_TABLE_KSTK		0x004	/* Kernel Stack */
#define EXC_TABLE_DOUBLE_SAVE	0x008	/* Double exception save area for a0 */
#define EXC_TABLE_FIXUP		0x00c	/* Fixup handler */
#define EXC_TABLE_PARAM		0x010	/* For passing a parameter to fixup */
#define EXC_TABLE_SYSCALL_SAVE	0x014	/* For fast syscall handler */
#define EXC_TABLE_FAST_USER	0x100	/* Fast user exception handler */
#define EXC_TABLE_FAST_KERNEL	0x200	/* Fast kernel exception handler */
#define EXC_TABLE_DEFAULT	0x300	/* Default C-Handler */
#define EXC_TABLE_SIZE		0x400

/* Registers used by strace */

#define REG_A_BASE	0x0000
#define REG_AR_BASE	0x0100
#define REG_PC		0x0020
#define REG_PS		0x02e6
#define REG_WB		0x0248
#define REG_WS		0x0249
#define REG_LBEG	0x0200
#define REG_LEND	0x0201
#define REG_LCOUNT	0x0202
#define REG_SAR		0x0203

#define SYSCALL_NR	0x00ff

/* Other PTRACE_ values defined in <linux/ptrace.h> using values 0-9,16,17,24 */

#define PTRACE_GETREGS		12
#define PTRACE_SETREGS		13
#define PTRACE_GETXTREGS	18
#define PTRACE_SETXTREGS	19


#endif /* _UAPI_XTENSA_PTRACE_H */
