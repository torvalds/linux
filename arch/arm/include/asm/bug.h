/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASMARM__H
#define _ASMARM__H

#include <linux/linkage.h>
#include <linux/types.h>
#include <asm/opcodes.h>

/*
 * Use a suitable undefined instruction to use for ARM/Thumb2  handling.
 * We need to be careful not to conflict with those used by other modules and
 * the register_undef_hook() system.
 */
#ifdef CONFIG_THUMB2_KERNEL
#define _INSTR_VALUE 0xde02
#define _INSTR(__value) __inst_thumb16(__value)
#else
#define _INSTR_VALUE 0xe7f001f2
#define _INSTR(__value) __inst_arm(__value)
#endif


#define () _(__FILE__, __LINE__, _INSTR_VALUE)
#define _(file, line, value) __(file, line, value)

#ifdef CONFIG_DE_VERBOSE

/*
 * The extra indirection is to ensure that the __FILE__ string comes through
 * OK. Many version of gcc do not support the asm %c parameter which would be
 * preferable to this unpleasantness. We use mergeable string sections to
 * avoid multiple copies of the string appearing in the kernel image.
 */

#define __(__file, __line, __value)				\
do {								\
	asm volatile("1:\t" _INSTR(__value) "\n"  \
		".pushsection .rodata.str, \"aMS\", %progbits, 1\n" \
		"2:\t.asciz " #__file "\n" 			\
		".popsection\n" 				\
		".pushsection ___table,\"aw\"\n"		\
		".align 2\n"					\
		"3:\t.word 1b, 2b\n"				\
		"\t.hword " #__line ", 0\n"			\
		".popsection");					\
	unreachable();						\
} while (0)

#else

#define __(__file, __line, __value)				\
do {								\
	asm volatile(_INSTR(__value) "\n");			\
	unreachable();						\
} while (0)
#endif  /* CONFIG_DE_VERBOSE */

#define HAVE_ARCH_

#include <asm-generic/.h>

struct pt_regs;
void die(const char *msg, struct pt_regs *regs, int err);

void arm_notify_die(const char *str, struct pt_regs *regs,
		int signo, int si_code, void __user *addr,
		unsigned long err, unsigned long trap);

#ifdef CONFIG_ARM_LPAE
#define FAULT_CODE_ALIGNMENT	33
#define FAULT_CODE_DE	34
#else
#define FAULT_CODE_ALIGNMENT	1
#define FAULT_CODE_DE	2
#endif

void hook_fault_code(int nr, int (*fn)(unsigned long, unsigned int,
				       struct pt_regs *),
		     int sig, int code, const char *name);

void hook_ifault_code(int nr, int (*fn)(unsigned long, unsigned int,
				       struct pt_regs *),
		     int sig, int code, const char *name);

extern asmlinkage void c_backtrace(unsigned long fp, int pmode);

struct mm_struct;
extern void show_pte(struct mm_struct *mm, unsigned long addr);
extern void __show_regs(struct pt_regs *);

#endif
