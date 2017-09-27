/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_MODULE_H
#define __ASM_MODULE_H

#include <asm-generic/module.h>

#define MODULE_ARCH_VERMAGIC	"aarch64"

#ifdef CONFIG_ARM64_MODULE_PLTS
struct mod_plt_sec {
	struct elf64_shdr	*plt;
	int			plt_num_entries;
	int			plt_max_entries;
};

struct mod_arch_specific {
	struct mod_plt_sec	core;
	struct mod_plt_sec	init;

	/* for CONFIG_DYNAMIC_FTRACE */
	void			*ftrace_trampoline;
};
#endif

u64 module_emit_plt_entry(struct module *mod, void *loc, const Elf64_Rela *rela,
			  Elf64_Sym *sym);

#ifdef CONFIG_RANDOMIZE_BASE
extern u64 module_alloc_base;
#else
#define module_alloc_base	((u64)_etext - MODULES_VSIZE)
#endif

#endif /* __ASM_MODULE_H */
