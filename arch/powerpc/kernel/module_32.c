// SPDX-License-Identifier: GPL-2.0-or-later
/*  Kernel module help for PPC.
    Copyright (C) 2001 Rusty Russell.

*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ftrace.h>
#include <linux/cache.h>
#include <linux/bug.h>
#include <linux/sort.h>
#include <asm/setup.h>
#include <asm/code-patching.h>

/* Count how many different relocations (different symbol, different
   addend) */
static unsigned int count_relocs(const Elf32_Rela *rela, unsigned int num)
{
	unsigned int i, r_info, r_addend, _count_relocs;

	_count_relocs = 0;
	r_info = 0;
	r_addend = 0;
	for (i = 0; i < num; i++)
		/* Only count 24-bit relocs, others don't need stubs */
		if (ELF32_R_TYPE(rela[i].r_info) == R_PPC_REL24 &&
		    (r_info != ELF32_R_SYM(rela[i].r_info) ||
		     r_addend != rela[i].r_addend)) {
			_count_relocs++;
			r_info = ELF32_R_SYM(rela[i].r_info);
			r_addend = rela[i].r_addend;
		}

#ifdef CONFIG_DYNAMIC_FTRACE
	_count_relocs++;	/* add one for ftrace_caller */
#endif
	return _count_relocs;
}

static int relacmp(const void *_x, const void *_y)
{
	const Elf32_Rela *x, *y;

	y = (Elf32_Rela *)_x;
	x = (Elf32_Rela *)_y;

	/* Compare the entire r_info (as opposed to ELF32_R_SYM(r_info) only) to
	 * make the comparison cheaper/faster. It won't affect the sorting or
	 * the counting algorithms' performance
	 */
	if (x->r_info < y->r_info)
		return -1;
	else if (x->r_info > y->r_info)
		return 1;
	else if (x->r_addend < y->r_addend)
		return -1;
	else if (x->r_addend > y->r_addend)
		return 1;
	else
		return 0;
}

/* Get the potential trampolines size required of the init and
   non-init sections */
static unsigned long get_plt_size(const Elf32_Ehdr *hdr,
				  const Elf32_Shdr *sechdrs,
				  const char *secstrings,
				  int is_init)
{
	unsigned long ret = 0;
	unsigned i;

	/* Everything marked ALLOC (this includes the exported
           symbols) */
	for (i = 1; i < hdr->e_shnum; i++) {
		/* If it's called *.init*, and we're not init, we're
                   not interested */
		if ((strstr(secstrings + sechdrs[i].sh_name, ".init") != NULL)
		    != is_init)
			continue;

		/* We don't want to look at debug sections. */
		if (strstr(secstrings + sechdrs[i].sh_name, ".debug"))
			continue;

		if (sechdrs[i].sh_type == SHT_RELA) {
			pr_debug("Found relocations in section %u\n", i);
			pr_debug("Ptr: %p.  Number: %u\n",
			       (void *)hdr + sechdrs[i].sh_offset,
			       sechdrs[i].sh_size / sizeof(Elf32_Rela));

			/* Sort the relocation information based on a symbol and
			 * addend key. This is a stable O(n*log n) complexity
			 * algorithm but it will reduce the complexity of
			 * count_relocs() to linear complexity O(n)
			 */
			sort((void *)hdr + sechdrs[i].sh_offset,
			     sechdrs[i].sh_size / sizeof(Elf32_Rela),
			     sizeof(Elf32_Rela), relacmp, NULL);

			ret += count_relocs((void *)hdr
					     + sechdrs[i].sh_offset,
					     sechdrs[i].sh_size
					     / sizeof(Elf32_Rela))
				* sizeof(struct ppc_plt_entry);
		}
	}

	return ret;
}

int module_frob_arch_sections(Elf32_Ehdr *hdr,
			      Elf32_Shdr *sechdrs,
			      char *secstrings,
			      struct module *me)
{
	unsigned int i;

	/* Find .plt and .init.plt sections */
	for (i = 0; i < hdr->e_shnum; i++) {
		if (strcmp(secstrings + sechdrs[i].sh_name, ".init.plt") == 0)
			me->arch.init_plt_section = i;
		else if (strcmp(secstrings + sechdrs[i].sh_name, ".plt") == 0)
			me->arch.core_plt_section = i;
	}
	if (!me->arch.core_plt_section || !me->arch.init_plt_section) {
		pr_err("Module doesn't contain .plt or .init.plt sections.\n");
		return -ENOEXEC;
	}

	/* Override their sizes */
	sechdrs[me->arch.core_plt_section].sh_size
		= get_plt_size(hdr, sechdrs, secstrings, 0);
	sechdrs[me->arch.init_plt_section].sh_size
		= get_plt_size(hdr, sechdrs, secstrings, 1);
	return 0;
}

static inline int entry_matches(struct ppc_plt_entry *entry, Elf32_Addr val)
{
	if (entry->jump[0] != PPC_RAW_LIS(_R12, PPC_HA(val)))
		return 0;
	if (entry->jump[1] != PPC_RAW_ADDI(_R12, _R12, PPC_LO(val)))
		return 0;
	return 1;
}

/* Set up a trampoline in the PLT to bounce us to the distant function */
static uint32_t do_plt_call(void *location,
			    Elf32_Addr val,
			    const Elf32_Shdr *sechdrs,
			    struct module *mod)
{
	struct ppc_plt_entry *entry;

	pr_debug("Doing plt for call to 0x%x at 0x%x\n", val, (unsigned int)location);
	/* Init, or core PLT? */
	if (location >= mod->core_layout.base
	    && location < mod->core_layout.base + mod->core_layout.size)
		entry = (void *)sechdrs[mod->arch.core_plt_section].sh_addr;
	else
		entry = (void *)sechdrs[mod->arch.init_plt_section].sh_addr;

	/* Find this entry, or if that fails, the next avail. entry */
	while (entry->jump[0]) {
		if (entry_matches(entry, val)) return (uint32_t)entry;
		entry++;
	}

	if (patch_instruction(&entry->jump[0], ppc_inst(PPC_RAW_LIS(_R12, PPC_HA(val)))))
		return 0;
	if (patch_instruction(&entry->jump[1], ppc_inst(PPC_RAW_ADDI(_R12, _R12, PPC_LO(val)))))
		return 0;
	if (patch_instruction(&entry->jump[2], ppc_inst(PPC_RAW_MTCTR(_R12))))
		return 0;
	if (patch_instruction(&entry->jump[3], ppc_inst(PPC_RAW_BCTR())))
		return 0;

	pr_debug("Initialized plt for 0x%x at %p\n", val, entry);
	return (uint32_t)entry;
}

static int patch_location_16(uint32_t *loc, u16 value)
{
	loc = PTR_ALIGN_DOWN(loc, sizeof(u32));
	return patch_instruction(loc, ppc_inst((*loc & 0xffff0000) | value));
}

int apply_relocate_add(Elf32_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *module)
{
	unsigned int i;
	Elf32_Rela *rela = (void *)sechdrs[relsec].sh_addr;
	Elf32_Sym *sym;
	uint32_t *location;
	uint32_t value;

	pr_debug("Applying ADD relocate section %u to %u\n", relsec,
	       sechdrs[relsec].sh_info);
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rela); i++) {
		/* This is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rela[i].r_offset;
		/* This is the symbol it is referring to.  Note that all
		   undefined symbols have been resolved.  */
		sym = (Elf32_Sym *)sechdrs[symindex].sh_addr
			+ ELF32_R_SYM(rela[i].r_info);
		/* `Everything is relative'. */
		value = sym->st_value + rela[i].r_addend;

		switch (ELF32_R_TYPE(rela[i].r_info)) {
		case R_PPC_ADDR32:
			/* Simply set it */
			*(uint32_t *)location = value;
			break;

		case R_PPC_ADDR16_LO:
			/* Low half of the symbol */
			if (patch_location_16(location, PPC_LO(value)))
				return -EFAULT;
			break;

		case R_PPC_ADDR16_HI:
			/* Higher half of the symbol */
			if (patch_location_16(location, PPC_HI(value)))
				return -EFAULT;
			break;

		case R_PPC_ADDR16_HA:
			if (patch_location_16(location, PPC_HA(value)))
				return -EFAULT;
			break;

		case R_PPC_REL24:
			if ((int)(value - (uint32_t)location) < -0x02000000
			    || (int)(value - (uint32_t)location) >= 0x02000000) {
				value = do_plt_call(location, value,
						    sechdrs, module);
				if (!value)
					return -EFAULT;
			}

			/* Only replace bits 2 through 26 */
			pr_debug("REL24 value = %08X. location = %08X\n",
			       value, (uint32_t)location);
			pr_debug("Location before: %08X.\n",
			       *(uint32_t *)location);
			value = (*(uint32_t *)location & ~PPC_LI_MASK) |
				PPC_LI(value - (uint32_t)location);

			if (patch_instruction(location, ppc_inst(value)))
				return -EFAULT;

			pr_debug("Location after: %08X.\n",
			       *(uint32_t *)location);
			pr_debug("ie. jump to %08X+%08X = %08X\n",
				 *(uint32_t *)PPC_LI((uint32_t)location), (uint32_t)location,
				 (*(uint32_t *)PPC_LI((uint32_t)location)) + (uint32_t)location);
			break;

		case R_PPC_REL32:
			/* 32-bit relative jump. */
			*(uint32_t *)location = value - (uint32_t)location;
			break;

		default:
			pr_err("%s: unknown ADD relocation: %u\n",
			       module->name,
			       ELF32_R_TYPE(rela[i].r_info));
			return -ENOEXEC;
		}
	}

	return 0;
}

#ifdef CONFIG_DYNAMIC_FTRACE
notrace int module_trampoline_target(struct module *mod, unsigned long addr,
				     unsigned long *target)
{
	ppc_inst_t jmp[4];

	/* Find where the trampoline jumps to */
	if (copy_inst_from_kernel_nofault(jmp, (void *)addr))
		return -EFAULT;
	if (__copy_inst_from_kernel_nofault(jmp + 1, (void *)addr + 4))
		return -EFAULT;
	if (__copy_inst_from_kernel_nofault(jmp + 2, (void *)addr + 8))
		return -EFAULT;
	if (__copy_inst_from_kernel_nofault(jmp + 3, (void *)addr + 12))
		return -EFAULT;

	/* verify that this is what we expect it to be */
	if ((ppc_inst_val(jmp[0]) & 0xffff0000) != PPC_RAW_LIS(_R12, 0))
		return -EINVAL;
	if ((ppc_inst_val(jmp[1]) & 0xffff0000) != PPC_RAW_ADDI(_R12, _R12, 0))
		return -EINVAL;
	if (ppc_inst_val(jmp[2]) != PPC_RAW_MTCTR(_R12))
		return -EINVAL;
	if (ppc_inst_val(jmp[3]) != PPC_RAW_BCTR())
		return -EINVAL;

	addr = (ppc_inst_val(jmp[1]) & 0xffff) | ((ppc_inst_val(jmp[0]) & 0xffff) << 16);
	if (addr & 0x8000)
		addr -= 0x10000;

	*target = addr;

	return 0;
}

int module_finalize_ftrace(struct module *module, const Elf_Shdr *sechdrs)
{
	module->arch.tramp = do_plt_call(module->core_layout.base,
					 (unsigned long)ftrace_caller,
					 sechdrs, module);
	if (!module->arch.tramp)
		return -ENOENT;

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
	module->arch.tramp_regs = do_plt_call(module->core_layout.base,
					      (unsigned long)ftrace_regs_caller,
					      sechdrs, module);
	if (!module->arch.tramp_regs)
		return -ENOENT;
#endif

	return 0;
}
#endif
