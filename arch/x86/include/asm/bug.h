/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_BUG_H
#define _ASM_X86_BUG_H

#include <linux/stringify.h>
#include <linux/instrumentation.h>
#include <linux/objtool.h>

/*
 * Despite that some emulators terminate on UD2, we use it for WARN().
 */
#define ASM_UD2		".byte 0x0f, 0x0b"
#define INSN_UD2	0x0b0f
#define LEN_UD2		2

/*
 * In clang we have UD1s reporting UBSAN failures on X86, 64 and 32bit.
 */
#define INSN_ASOP		0x67
#define OPCODE_ESCAPE		0x0f
#define SECOND_BYTE_OPCODE_UD1	0xb9
#define SECOND_BYTE_OPCODE_UD2	0x0b

#define BUG_NONE		0xffff
#define BUG_UD1			0xfffe
#define BUG_UD2			0xfffd

#ifdef CONFIG_GENERIC_BUG

#ifdef CONFIG_X86_32
# define __BUG_REL(val)	".long " __stringify(val)
#else
# define __BUG_REL(val)	".long " __stringify(val) " - ."
#endif

#ifdef CONFIG_DEBUG_BUGVERBOSE

#define _BUG_FLAGS(ins, flags, extra)					\
do {									\
	asm_inline volatile("1:\t" ins "\n"				\
		     ".pushsection __bug_table,\"aw\"\n"		\
		     "2:\t" __BUG_REL(1b) "\t# bug_entry::bug_addr\n"	\
		     "\t"  __BUG_REL(%c0) "\t# bug_entry::file\n"	\
		     "\t.word %c1"        "\t# bug_entry::line\n"	\
		     "\t.word %c2"        "\t# bug_entry::flags\n"	\
		     "\t.org 2b+%c3\n"					\
		     ".popsection\n"					\
		     extra						\
		     : : "i" (__FILE__), "i" (__LINE__),		\
			 "i" (flags),					\
			 "i" (sizeof(struct bug_entry)));		\
} while (0)

#else /* !CONFIG_DEBUG_BUGVERBOSE */

#define _BUG_FLAGS(ins, flags, extra)					\
do {									\
	asm_inline volatile("1:\t" ins "\n"				\
		     ".pushsection __bug_table,\"aw\"\n"		\
		     "2:\t" __BUG_REL(1b) "\t# bug_entry::bug_addr\n"	\
		     "\t.word %c0"        "\t# bug_entry::flags\n"	\
		     "\t.org 2b+%c1\n"					\
		     ".popsection\n"					\
		     extra						\
		     : : "i" (flags),					\
			 "i" (sizeof(struct bug_entry)));		\
} while (0)

#endif /* CONFIG_DEBUG_BUGVERBOSE */

#else

#define _BUG_FLAGS(ins, flags, extra)  asm volatile(ins)

#endif /* CONFIG_GENERIC_BUG */

#define HAVE_ARCH_BUG
#define BUG()							\
do {								\
	instrumentation_begin();				\
	_BUG_FLAGS(ASM_UD2, 0, "");				\
	__builtin_unreachable();				\
} while (0)

/*
 * This instrumentation_begin() is strictly speaking incorrect; but it
 * suppresses the complaints from WARN()s in noinstr code. If such a WARN()
 * were to trigger, we'd rather wreck the machine in an attempt to get the
 * message out than not know about it.
 */
#define __WARN_FLAGS(flags)					\
do {								\
	__auto_type __flags = BUGFLAG_WARNING|(flags);		\
	instrumentation_begin();				\
	_BUG_FLAGS(ASM_UD2, __flags, ANNOTATE_REACHABLE(1b));	\
	instrumentation_end();					\
} while (0)

#include <asm-generic/bug.h>

#endif /* _ASM_X86_BUG_H */
