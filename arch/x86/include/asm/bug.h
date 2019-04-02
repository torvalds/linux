/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86__H
#define _ASM_X86__H

#include <linux/stringify.h>

/*
 * Despite that some emulators terminate on UD2, we use it for WARN().
 *
 * Since various instruction decoders/specs disagree on the encoding of
 * UD0/UD1.
 */

#define ASM_UD0		".byte 0x0f, 0xff" /* + ModRM (for Intel) */
#define ASM_UD1		".byte 0x0f, 0xb9" /* + ModRM */
#define ASM_UD2		".byte 0x0f, 0x0b"

#define INSN_UD0	0xff0f
#define INSN_UD2	0x0b0f

#define LEN_UD2		2

#ifdef CONFIG_GENERIC_

#ifdef CONFIG_X86_32
# define ___REL(val)	".long " __stringify(val)
#else
# define ___REL(val)	".long " __stringify(val) " - 2b"
#endif

#ifdef CONFIG_DE_VERBOSE

#define __FLAGS(ins, flags)						\
do {									\
	asm volatile("1:\t" ins "\n"					\
		     ".pushsection ___table,\"aw\"\n"		\
		     "2:\t" ___REL(1b) "\t# _entry::_addr\n"	\
		     "\t"  ___REL(%c0) "\t# _entry::file\n"	\
		     "\t.word %c1"        "\t# _entry::line\n"	\
		     "\t.word %c2"        "\t# _entry::flags\n"	\
		     "\t.org 2b+%c3\n"					\
		     ".popsection"					\
		     : : "i" (__FILE__), "i" (__LINE__),		\
			 "i" (flags),					\
			 "i" (sizeof(struct _entry)));		\
} while (0)

#else /* !CONFIG_DE_VERBOSE */

#define __FLAGS(ins, flags)						\
do {									\
	asm volatile("1:\t" ins "\n"					\
		     ".pushsection ___table,\"aw\"\n"		\
		     "2:\t" ___REL(1b) "\t# _entry::_addr\n"	\
		     "\t.word %c0"        "\t# _entry::flags\n"	\
		     "\t.org 2b+%c1\n"					\
		     ".popsection"					\
		     : : "i" (flags),					\
			 "i" (sizeof(struct _entry)));		\
} while (0)

#endif /* CONFIG_DE_VERBOSE */

#else

#define __FLAGS(ins, flags)  asm volatile(ins)

#endif /* CONFIG_GENERIC_ */

#define HAVE_ARCH_
#define ()							\
do {								\
	__FLAGS(ASM_UD2, 0);					\
	unreachable();						\
} while (0)

#define __WARN_FLAGS(flags)					\
do {								\
	__FLAGS(ASM_UD2, FLAG_WARNING|(flags));		\
	annotate_reachable();					\
} while (0)

#include <asm-generic/.h>

#endif /* _ASM_X86__H */
