/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_BUG_H
#define _ASM_X86_BUG_H

#include <linux/stringify.h>
#include <linux/instrumentation.h>
#include <linux/objtool.h>
#include <asm/asm.h>

/*
 * Despite that some emulators terminate on UD2, we use it for WARN().
 */
#define ASM_UD2		_ASM_BYTES(0x0f, 0x0b)
#define INSN_UD2	0x0b0f
#define LEN_UD2		2

#define ASM_UDB		_ASM_BYTES(0xd6)
#define INSN_UDB	0xd6
#define LEN_UDB		1

/*
 * In clang we have UD1s reporting UBSAN failures on X86, 64 and 32bit.
 */
#define INSN_ASOP		0x67
#define INSN_LOCK		0xf0
#define OPCODE_ESCAPE		0x0f
#define SECOND_BYTE_OPCODE_UD1	0xb9
#define SECOND_BYTE_OPCODE_UD2	0x0b

#define BUG_NONE		0xffff
#define BUG_UD2			0xfffe
#define BUG_UD1			0xfffd
#define BUG_UD1_UBSAN		0xfffc
#define BUG_UDB			0xffd6
#define BUG_LOCK		0xfff0

#ifdef CONFIG_GENERIC_BUG

#ifdef CONFIG_X86_32
# define __BUG_REL(val)	".long " val
#else
# define __BUG_REL(val)	".long " val " - ."
#endif

#ifdef CONFIG_DEBUG_BUGVERBOSE
#define __BUG_ENTRY(file, line, flags)					\
	"2:\t" __BUG_REL("1b") "\t# bug_entry::bug_addr\n"		\
	"\t" __BUG_REL(file)   "\t# bug_entry::file\n"			\
	"\t.word " line        "\t# bug_entry::line\n"			\
	"\t.word " flags       "\t# bug_entry::flags\n"
#else
#define __BUG_ENTRY(file, line, flags)					\
	"2:\t" __BUG_REL("1b") "\t# bug_entry::bug_addr\n"		\
	"\t.word " flags       "\t# bug_entry::flags\n"
#endif

#define _BUG_FLAGS_ASM(ins, file, line, flags, size, extra)		\
	"1:\t" ins "\n"							\
	".pushsection __bug_table,\"aw\"\n"				\
	__BUG_ENTRY(file, line, flags)					\
	"\t.org 2b + " size "\n"					\
	".popsection\n"							\
	extra

#define _BUG_FLAGS(ins, flags, extra)					\
do {									\
	asm_inline volatile(_BUG_FLAGS_ASM(ins, "%c0",			\
					   "%c1", "%c2", "%c3", extra)	\
		     : : "i" (__FILE__), "i" (__LINE__),		\
			 "i" (flags),					\
			 "i" (sizeof(struct bug_entry)));		\
} while (0)

#define ARCH_WARN_ASM(file, line, flags, size)				\
	_BUG_FLAGS_ASM(ASM_UD2, file, line, flags, size, "")

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

#define ARCH_WARN_REACHABLE	ANNOTATE_REACHABLE(1b)

#define __WARN_FLAGS(flags)					\
do {								\
	__auto_type __flags = BUGFLAG_WARNING|(flags);		\
	instrumentation_begin();				\
	_BUG_FLAGS(ASM_UD2, __flags, ARCH_WARN_REACHABLE);	\
	instrumentation_end();					\
} while (0)

#include <asm-generic/bug.h>

#endif /* _ASM_X86_BUG_H */
