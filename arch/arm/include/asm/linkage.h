/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_LINKAGE_H
#define __ASM_LINKAGE_H

#define __ALIGN .align 0
#define __ALIGN_STR ".align 0"

#define ENDPROC(name) \
  .type name, %function; \
  END(name)

#ifdef __ASSEMBLY__

/*
 * Variants of SYM_TYPED_START/SYM_TYPED_FUNC_START that align the
 * function entry itself instead of the kCFI type hash preceding it,
 * for functions whose entry point must meet an alignment requirement,
 * such as the 8-byte alignment fncpy() demands of its source.
 */
#ifdef CONFIG_CFI

#define SYM_TYPED_START_ALIGNED(name, linkage, align)	\
	linkage(name) ASM_NL				\
	.balign align ASM_NL				\
	.fill (align) - 4, 1, 0 ASM_NL			\
	__CFI_TYPE(name) ASM_NL				\
	name:

#else /* CONFIG_CFI */

#define SYM_TYPED_START_ALIGNED(name, linkage, align)	\
	SYM_START(name, linkage, .balign align)

#endif /* CONFIG_CFI */

#define SYM_TYPED_FUNC_START_ALIGNED(name, align)	\
	SYM_TYPED_START_ALIGNED(name, SYM_L_GLOBAL, align)

#endif /* __ASSEMBLY__ */

#endif
