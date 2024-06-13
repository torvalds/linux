/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_M68K_MODULE_H
#define _ASM_M68K_MODULE_H

#include <asm-generic/module.h>

enum m68k_fixup_type {
	m68k_fixup_memoffset,
	m68k_fixup_vnode_shift,
};

struct m68k_fixup_info {
	enum m68k_fixup_type type;
	void *addr;
};

struct mod_arch_specific {
	struct m68k_fixup_info *fixup_start, *fixup_end;
};

#ifdef CONFIG_MMU

#define MODULE_ARCH_INIT {				\
	.fixup_start		= __start_fixup,	\
	.fixup_end		= __stop_fixup,		\
}


#define m68k_fixup(type, addr)			\
	"	.section \".m68k_fixup\",\"aw\"\n"	\
	"	.long " #type "," #addr "\n"	\
	"	.previous\n"

#endif /* CONFIG_MMU */

extern struct m68k_fixup_info __start_fixup[], __stop_fixup[];

struct module;
extern void module_fixup(struct module *mod, struct m68k_fixup_info *start,
			 struct m68k_fixup_info *end);

#endif /* _ASM_M68K_MODULE_H */
