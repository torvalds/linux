/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_BUG_H
#define _ASM_X86_BUG_H

#include <linux/stringify.h>

#ifndef __ASSEMBLY__

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

#define _BUG_FLAGS(ins, flags)						\
do {									\
	asm volatile("ASM_BUG ins=\"" ins "\" file=%c0 line=%c1 "	\
		     "flags=%c2 size=%c3"				\
		     : : "i" (__FILE__), "i" (__LINE__),                \
			 "i" (flags),                                   \
			 "i" (sizeof(struct bug_entry)));		\
} while (0)

#define HAVE_ARCH_BUG
#define BUG()							\
do {								\
	_BUG_FLAGS(ASM_UD2, 0);					\
	unreachable();						\
} while (0)

#define __WARN_FLAGS(flags)					\
do {								\
	_BUG_FLAGS(ASM_UD2, BUGFLAG_WARNING|(flags));		\
	annotate_reachable();					\
} while (0)

#include <asm-generic/bug.h>

#else /* __ASSEMBLY__ */

#ifdef CONFIG_GENERIC_BUG

#ifdef CONFIG_X86_32
.macro __BUG_REL val:req
	.long \val
.endm
#else
.macro __BUG_REL val:req
	.long \val - 2b
.endm
#endif

#ifdef CONFIG_DEBUG_BUGVERBOSE

.macro ASM_BUG ins:req file:req line:req flags:req size:req
1:	\ins
	.pushsection __bug_table,"aw"
2:	__BUG_REL val=1b	# bug_entry::bug_addr
	__BUG_REL val=\file	# bug_entry::file
	.word \line		# bug_entry::line
	.word \flags		# bug_entry::flags
	.org 2b+\size
	.popsection
.endm

#else /* !CONFIG_DEBUG_BUGVERBOSE */

.macro ASM_BUG ins:req file:req line:req flags:req size:req
1:	\ins
	.pushsection __bug_table,"aw"
2:	__BUG_REL val=1b	# bug_entry::bug_addr
	.word \flags		# bug_entry::flags
	.org 2b+\size
	.popsection
.endm

#endif /* CONFIG_DEBUG_BUGVERBOSE */

#else /* CONFIG_GENERIC_BUG */

.macro ASM_BUG ins:req file:req line:req flags:req size:req
	\ins
.endm

#endif /* CONFIG_GENERIC_BUG */

#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_BUG_H */
