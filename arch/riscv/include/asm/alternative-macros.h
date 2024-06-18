/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ALTERNATIVE_MACROS_H
#define __ASM_ALTERNATIVE_MACROS_H

#ifdef CONFIG_RISCV_ALTERNATIVE

#ifdef __ASSEMBLY__

.macro ALT_ENTRY oldptr newptr vendor_id patch_id new_len
	.4byte \oldptr - .
	.4byte \newptr - .
	.2byte \vendor_id
	.2byte \new_len
	.4byte \patch_id
.endm

.macro ALT_NEW_CONTENT vendor_id, patch_id, enable = 1, new_c
	.if \enable
	.pushsection .alternative, "a"
	ALT_ENTRY 886b, 888f, \vendor_id, \patch_id, 889f - 888f
	.popsection
	.subsection 1
888 :
	.option push
	.option norvc
	.option norelax
	\new_c
	.option pop
889 :
	.org    . - (889b - 888b) + (887b - 886b)
	.org    . - (887b - 886b) + (889b - 888b)
	.previous
	.endif
.endm

.macro ALTERNATIVE_CFG old_c, new_c, vendor_id, patch_id, enable
886 :
	.option push
	.option norvc
	.option norelax
	\old_c
	.option pop
887 :
	ALT_NEW_CONTENT \vendor_id, \patch_id, \enable, "\new_c"
.endm

.macro ALTERNATIVE_CFG_2 old_c, new_c_1, vendor_id_1, patch_id_1, enable_1,	\
				new_c_2, vendor_id_2, patch_id_2, enable_2
	ALTERNATIVE_CFG "\old_c", "\new_c_1", \vendor_id_1, \patch_id_1, \enable_1
	ALT_NEW_CONTENT \vendor_id_2, \patch_id_2, \enable_2, "\new_c_2"
.endm

#define __ALTERNATIVE_CFG(...)		ALTERNATIVE_CFG __VA_ARGS__
#define __ALTERNATIVE_CFG_2(...)	ALTERNATIVE_CFG_2 __VA_ARGS__

#else /* !__ASSEMBLY__ */

#include <asm/asm.h>
#include <linux/stringify.h>

#define ALT_ENTRY(oldptr, newptr, vendor_id, patch_id, newlen)		\
	".4byte	((" oldptr ") - .) \n"					\
	".4byte	((" newptr ") - .) \n"					\
	".2byte	" vendor_id "\n"					\
	".2byte " newlen "\n"						\
	".4byte	" patch_id "\n"

#define ALT_NEW_CONTENT(vendor_id, patch_id, enable, new_c)		\
	".if " __stringify(enable) " == 1\n"				\
	".pushsection .alternative, \"a\"\n"				\
	ALT_ENTRY("886b", "888f", __stringify(vendor_id), __stringify(patch_id), "889f - 888f") \
	".popsection\n"							\
	".subsection 1\n"						\
	"888 :\n"							\
	".option push\n"						\
	".option norvc\n"						\
	".option norelax\n"						\
	new_c "\n"							\
	".option pop\n"							\
	"889 :\n"							\
	".org	. - (887b - 886b) + (889b - 888b)\n"			\
	".org	. - (889b - 888b) + (887b - 886b)\n"			\
	".previous\n"							\
	".endif\n"

#define __ALTERNATIVE_CFG(old_c, new_c, vendor_id, patch_id, enable)	\
	"886 :\n"							\
	".option push\n"						\
	".option norvc\n"						\
	".option norelax\n"						\
	old_c "\n"							\
	".option pop\n"							\
	"887 :\n"							\
	ALT_NEW_CONTENT(vendor_id, patch_id, enable, new_c)

#define __ALTERNATIVE_CFG_2(old_c, new_c_1, vendor_id_1, patch_id_1, enable_1,	\
				   new_c_2, vendor_id_2, patch_id_2, enable_2)	\
	__ALTERNATIVE_CFG(old_c, new_c_1, vendor_id_1, patch_id_1, enable_1)	\
	ALT_NEW_CONTENT(vendor_id_2, patch_id_2, enable_2, new_c_2)

#endif /* __ASSEMBLY__ */

#define _ALTERNATIVE_CFG(old_c, new_c, vendor_id, patch_id, CONFIG_k)	\
	__ALTERNATIVE_CFG(old_c, new_c, vendor_id, patch_id, IS_ENABLED(CONFIG_k))

#define _ALTERNATIVE_CFG_2(old_c, new_c_1, vendor_id_1, patch_id_1, CONFIG_k_1,		\
				  new_c_2, vendor_id_2, patch_id_2, CONFIG_k_2)		\
	__ALTERNATIVE_CFG_2(old_c, new_c_1, vendor_id_1, patch_id_1, IS_ENABLED(CONFIG_k_1),	\
				   new_c_2, vendor_id_2, patch_id_2, IS_ENABLED(CONFIG_k_2))

#else /* CONFIG_RISCV_ALTERNATIVE */
#ifdef __ASSEMBLY__

.macro ALTERNATIVE_CFG old_c
	\old_c
.endm

#define _ALTERNATIVE_CFG(old_c, ...)	\
	ALTERNATIVE_CFG old_c

#define _ALTERNATIVE_CFG_2(old_c, ...)	\
	ALTERNATIVE_CFG old_c

#else /* !__ASSEMBLY__ */

#define __ALTERNATIVE_CFG(old_c)	\
	old_c "\n"

#define _ALTERNATIVE_CFG(old_c, ...)	\
	__ALTERNATIVE_CFG(old_c)

#define _ALTERNATIVE_CFG_2(old_c, ...)	\
	__ALTERNATIVE_CFG(old_c)

#endif /* __ASSEMBLY__ */
#endif /* CONFIG_RISCV_ALTERNATIVE */

/*
 * Usage:
 *   ALTERNATIVE(old_content, new_content, vendor_id, patch_id, CONFIG_k)
 * in the assembly code. Otherwise,
 *   asm(ALTERNATIVE(old_content, new_content, vendor_id, patch_id, CONFIG_k));
 *
 * old_content: The old content which is probably replaced with new content.
 * new_content: The new content.
 * vendor_id: The CPU vendor ID.
 * patch_id: The patch ID (erratum ID or cpufeature ID).
 * CONFIG_k: The Kconfig of this patch ID. When Kconfig is disabled, the old
 *	     content will always be executed.
 */
#define ALTERNATIVE(old_content, new_content, vendor_id, patch_id, CONFIG_k) \
	_ALTERNATIVE_CFG(old_content, new_content, vendor_id, patch_id, CONFIG_k)

/*
 * A vendor wants to replace an old_content, but another vendor has used
 * ALTERNATIVE() to patch its customized content at the same location. In
 * this case, this vendor can create a new macro ALTERNATIVE_2() based
 * on the following sample code and then replace ALTERNATIVE() with
 * ALTERNATIVE_2() to append its customized content.
 */
#define ALTERNATIVE_2(old_content, new_content_1, vendor_id_1, patch_id_1, CONFIG_k_1,		\
				   new_content_2, vendor_id_2, patch_id_2, CONFIG_k_2)		\
	_ALTERNATIVE_CFG_2(old_content, new_content_1, vendor_id_1, patch_id_1, CONFIG_k_1,	\
					new_content_2, vendor_id_2, patch_id_2, CONFIG_k_2)

#endif
