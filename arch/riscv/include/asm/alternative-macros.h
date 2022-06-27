/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ALTERNATIVE_MACROS_H
#define __ASM_ALTERNATIVE_MACROS_H

#ifdef CONFIG_RISCV_ERRATA_ALTERNATIVE

#ifdef __ASSEMBLY__

.macro ALT_ENTRY oldptr newptr vendor_id errata_id new_len
	RISCV_PTR \oldptr
	RISCV_PTR \newptr
	REG_ASM \vendor_id
	REG_ASM \new_len
	.word	\errata_id
.endm

.macro ALT_NEW_CONTENT vendor_id, errata_id, enable = 1, new_c : vararg
	.if \enable
	.pushsection .alternative, "a"
	ALT_ENTRY 886b, 888f, \vendor_id, \errata_id, 889f - 888f
	.popsection
	.subsection 1
888 :
	\new_c
889 :
	.org    . - (889b - 888b) + (887b - 886b)
	.org    . - (887b - 886b) + (889b - 888b)
	.previous
	.endif
.endm

.macro __ALTERNATIVE_CFG old_c, new_c, vendor_id, errata_id, enable
886 :
	\old_c
887 :
	ALT_NEW_CONTENT \vendor_id, \errata_id, \enable, \new_c
.endm

#define _ALTERNATIVE_CFG(old_c, new_c, vendor_id, errata_id, CONFIG_k) \
	__ALTERNATIVE_CFG old_c, new_c, vendor_id, errata_id, IS_ENABLED(CONFIG_k)

#else /* !__ASSEMBLY__ */

#include <asm/asm.h>
#include <linux/stringify.h>

#define ALT_ENTRY(oldptr, newptr, vendor_id, errata_id, newlen) \
	RISCV_PTR " " oldptr "\n" \
	RISCV_PTR " " newptr "\n" \
	REG_ASM " " vendor_id "\n" \
	REG_ASM " " newlen "\n" \
	".word " errata_id "\n"

#define ALT_NEW_CONTENT(vendor_id, errata_id, enable, new_c) \
	".if " __stringify(enable) " == 1\n"				\
	".pushsection .alternative, \"a\"\n"				\
	ALT_ENTRY("886b", "888f", __stringify(vendor_id), __stringify(errata_id), "889f - 888f") \
	".popsection\n"							\
	".subsection 1\n"						\
	"888 :\n"							\
	new_c "\n"							\
	"889 :\n"							\
	".org	. - (887b - 886b) + (889b - 888b)\n"			\
	".org	. - (889b - 888b) + (887b - 886b)\n"			\
	".previous\n"							\
	".endif\n"

#define __ALTERNATIVE_CFG(old_c, new_c, vendor_id, errata_id, enable) \
	"886 :\n"	\
	old_c "\n"	\
	"887 :\n"	\
	ALT_NEW_CONTENT(vendor_id, errata_id, enable, new_c)

#define _ALTERNATIVE_CFG(old_c, new_c, vendor_id, errata_id, CONFIG_k)	\
	__ALTERNATIVE_CFG(old_c, new_c, vendor_id, errata_id, IS_ENABLED(CONFIG_k))

#endif /* __ASSEMBLY__ */

#else /* !CONFIG_RISCV_ERRATA_ALTERNATIVE*/
#ifdef __ASSEMBLY__

.macro __ALTERNATIVE_CFG old_c
	\old_c
.endm

#define _ALTERNATIVE_CFG(old_c, new_c, vendor_id, errata_id, CONFIG_k) \
	__ALTERNATIVE_CFG old_c

#else /* !__ASSEMBLY__ */

#define __ALTERNATIVE_CFG(old_c)  \
	old_c "\n"

#define _ALTERNATIVE_CFG(old_c, new_c, vendor_id, errata_id, CONFIG_k) \
	__ALTERNATIVE_CFG(old_c)

#endif /* __ASSEMBLY__ */
#endif /* CONFIG_RISCV_ERRATA_ALTERNATIVE */
/*
 * Usage:
 *   ALTERNATIVE(old_content, new_content, vendor_id, errata_id, CONFIG_k)
 * in the assembly code. Otherwise,
 *   asm(ALTERNATIVE(old_content, new_content, vendor_id, errata_id, CONFIG_k));
 *
 * old_content: The old content which is probably replaced with new content.
 * new_content: The new content.
 * vendor_id: The CPU vendor ID.
 * errata_id: The errata ID.
 * CONFIG_k: The Kconfig of this errata. When Kconfig is disabled, the old
 *	     content will alwyas be executed.
 */
#define ALTERNATIVE(old_content, new_content, vendor_id, errata_id, CONFIG_k) \
	_ALTERNATIVE_CFG(old_content, new_content, vendor_id, errata_id, CONFIG_k)

/*
 * A vendor wants to replace an old_content, but another vendor has used
 * ALTERNATIVE() to patch its customized content at the same location. In
 * this case, this vendor can create a new macro ALTERNATIVE_2() based
 * on the following sample code and then replace ALTERNATIVE() with
 * ALTERNATIVE_2() to append its customized content.
 *
 * .macro __ALTERNATIVE_CFG_2 old_c, new_c_1, vendor_id_1, errata_id_1, enable_1, \
 *                                   new_c_2, vendor_id_2, errata_id_2, enable_2
 * 886 :
 *      \old_c
 * 887 :
 *      ALT_NEW_CONTENT \vendor_id_1, \errata_id_1, \enable_1, \new_c_1
 *      ALT_NEW_CONTENT \vendor_id_2, \errata_id_2, \enable_2, \new_c_2
 * .endm
 *
 * #define _ALTERNATIVE_CFG_2(old_c, new_c_1, vendor_id_1, errata_id_1, CONFIG_k_1, \
 *                                   new_c_2, vendor_id_2, errata_id_2, CONFIG_k_2) \
 *        __ALTERNATIVE_CFG_2 old_c, new_c_1, vendor_id_1, errata_id_1, IS_ENABLED(CONFIG_k_1), \
 *                                   new_c_2, vendor_id_2, errata_id_2, IS_ENABLED(CONFIG_k_2) \
 *
 * #define ALTERNATIVE_2(old_content, new_content_1, vendor_id_1, errata_id_1, CONFIG_k_1, \
 *                                    new_content_2, vendor_id_2, errata_id_2, CONFIG_k_2) \
 *         _ALTERNATIVE_CFG_2(old_content, new_content_1, vendor_id_1, errata_id_1, CONFIG_k_1, \
 *                                         new_content_2, vendor_id_2, errata_id_2, CONFIG_k_2)
 *
 */
#endif
