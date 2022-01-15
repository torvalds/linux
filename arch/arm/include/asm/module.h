/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM_MODULE_H
#define _ASM_ARM_MODULE_H

#include <asm-generic/module.h>

struct unwind_table;

#ifdef CONFIG_ARM_UNWIND
enum {
	ARM_SEC_INIT,
	ARM_SEC_DEVINIT,
	ARM_SEC_CORE,
	ARM_SEC_EXIT,
	ARM_SEC_DEVEXIT,
	ARM_SEC_HOT,
	ARM_SEC_UNLIKELY,
	ARM_SEC_MAX,
};
#endif

#define PLT_ENT_STRIDE		L1_CACHE_BYTES
#define PLT_ENT_COUNT		(PLT_ENT_STRIDE / sizeof(u32))
#define PLT_ENT_SIZE		(sizeof(struct plt_entries) / PLT_ENT_COUNT)

struct plt_entries {
	u32	ldr[PLT_ENT_COUNT];
	u32	lit[PLT_ENT_COUNT];
};

struct mod_plt_sec {
	struct elf32_shdr	*plt;
	struct plt_entries	*plt_ent;
	int			plt_count;
};

struct mod_arch_specific {
#ifdef CONFIG_ARM_UNWIND
	struct unwind_table *unwind[ARM_SEC_MAX];
#endif
#ifdef CONFIG_ARM_MODULE_PLTS
	struct mod_plt_sec	core;
	struct mod_plt_sec	init;
#endif
};

struct module;
u32 get_module_plt(struct module *mod, unsigned long loc, Elf32_Addr val);

#ifdef CONFIG_THUMB2_KERNEL
#define HAVE_ARCH_KALLSYMS_SYMBOL_VALUE
static inline unsigned long kallsyms_symbol_value(const Elf_Sym *sym)
{
	if (ELF_ST_TYPE(sym->st_info) == STT_FUNC)
		return sym->st_value & ~1;

	return sym->st_value;
}
#endif

#endif /* _ASM_ARM_MODULE_H */
