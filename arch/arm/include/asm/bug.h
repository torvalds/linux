#ifndef _ASMARM_BUG_H
#define _ASMARM_BUG_H


#ifdef CONFIG_BUG

/*
 * Use a suitable undefined instruction to use for ARM/Thumb2 bug handling.
 * We need to be careful not to conflict with those used by other modules and
 * the register_undef_hook() system.
 */
#ifdef CONFIG_THUMB2_KERNEL
#define BUG_INSTR_VALUE 0xde02
#define BUG_INSTR_TYPE ".hword "
#else
#define BUG_INSTR_VALUE 0xe7f001f2
#define BUG_INSTR_TYPE ".word "
#endif


#define BUG() _BUG(__FILE__, __LINE__, BUG_INSTR_VALUE)
#define _BUG(file, line, value) __BUG(file, line, value)

#ifdef CONFIG_DEBUG_BUGVERBOSE

/*
 * The extra indirection is to ensure that the __FILE__ string comes through
 * OK. Many version of gcc do not support the asm %c parameter which would be
 * preferable to this unpleasantness. We use mergeable string sections to
 * avoid multiple copies of the string appearing in the kernel image.
 */

#define __BUG(__file, __line, __value)				\
do {								\
	BUILD_BUG_ON(sizeof(struct bug_entry) != 12);		\
	asm volatile("1:\t" BUG_INSTR_TYPE #__value "\n"	\
		".pushsection .rodata.str, \"aMS\", %progbits, 1\n" \
		"2:\t.asciz " #__file "\n" 			\
		".popsection\n" 				\
		".pushsection __bug_table,\"a\"\n"		\
		"3:\t.word 1b, 2b\n"				\
		"\t.hword " #__line ", 0\n"			\
		".popsection");					\
	unreachable();						\
} while (0)

#else  /* not CONFIG_DEBUG_BUGVERBOSE */

#define __BUG(__file, __line, __value)				\
do {								\
	asm volatile(BUG_INSTR_TYPE #__value);			\
	unreachable();						\
} while (0)
#endif  /* CONFIG_DEBUG_BUGVERBOSE */

#define HAVE_ARCH_BUG
#endif  /* CONFIG_BUG */

#include <asm-generic/bug.h>

#endif
