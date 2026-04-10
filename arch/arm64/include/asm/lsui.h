/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_LSUI_H
#define __ASM_LSUI_H

#include <linux/compiler_types.h>
#include <linux/stringify.h>
#include <asm/alternative.h>
#include <asm/alternative-macros.h>
#include <asm/cpucaps.h>

#define __LSUI_PREAMBLE	".arch_extension lsui\n"

#ifdef CONFIG_ARM64_LSUI

#define __lsui_llsc_body(op, ...)					\
({									\
	alternative_has_cap_unlikely(ARM64_HAS_LSUI) ?			\
		__lsui_##op(__VA_ARGS__) : __llsc_##op(__VA_ARGS__);	\
})

#else	/* CONFIG_ARM64_LSUI */

#define __lsui_llsc_body(op, ...)	__llsc_##op(__VA_ARGS__)

#endif	/* CONFIG_ARM64_LSUI */

#endif	/* __ASM_LSUI_H */
