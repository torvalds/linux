/*
 *  linux/arch/arm/kernel/module.c
 *
 *  Copyright (C) 2002 Russell King.
 *  Modified for nommu by Hyok S. Choi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Module allocation method suggested by Andi Kleen.
 */
#include <linux/module.h>
#include <linux/moduleloader.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/gfp.h>

#include <asm/pgtable.h>
#include <asm/sections.h>
#include <asm/smp_plat.h>
#include <asm/unwind.h>
#include <asm/opcodes.h>

#ifdef CONFIG_XIP_KERNEL
/*
 * The XIP kernel text is mapped in the module area for modules and
 * some other stuff to work without any indirect relocations.
 * MODULES_VADDR is redefined here and not in asm/memory.h to avoid
 * recompiling the whole kernel when CONFIG_XIP_KERNEL is turned on/off.
 */
#undef MODULES_VADDR
#define MODULES_VADDR	(((unsigned long)_etext + ~PMD_MASK) & PMD_MASK)
#endif

#ifdef CONFIG_MMU
void *module_alloc(unsigned long size)
{
	void *p = __vmalloc_node_range(size, 1, MODULES_VADDR, MODULES_END,
				GFP_KERNEL, PAGE_KERNEL_EXEC, 0, NUMA_NO_NODE,
				__builtin_return_address(0));
	if (!IS_ENABLED(CONFIG_ARM_MODULE_PLTS) || p)
		return p;
	return __vmalloc_node_range(size, 1,  VMALLOC_START, VMALLOC_END,
				GFP_KERNEL, PAGE_KERNEL_EXEC, 0, NUMA_NO_NODE,
				__builtin_return_address(0));
}
#endif

int
apply_relocate(Elf32_Shdr *sechdrs, const char *strtab, unsigned int symindex,
	       unsigned int relindex, struct module *module)
{
	Elf32_Shdr *symsec = sechdrs + symindex;
	Elf32_Shdr *relsec = sechdrs + relindex;
	Elf32_Shdr *dstsec = sechdrs + relsec->sh_info;
	Elf32_Rel *rel = (void *)relsec->sh_addr;
	unsigned int i;

	for (i = 0; i < relsec->sh_size / sizeof(Elf32_Rel); i++, rel++) {
		unsigned long loc;
		Elf32_Sym *sym;
		const char *symname;
		s32 offset;
		u32 tmp;
#ifdef CONFIG_THUMB2_KERNEL
		u32 upper, lower, sign, j1, j2;
#endif

		offset = ELF32_R_SYM(rel->r_info);
		if (offset < 0 || offset > (symsec->sh_size / sizeof(Elf32_Sym))) {
			pr_err("%s: section %u reloc %u: bad relocation sym offset\n",
				module->name, relindex, i);
			return -ENOEXEC;
		}

		sym = ((Elf32_Sym *)symsec->sh_addr) + offset;
		symname = strtab + sym->st_name;

		if (rel->r_offset < 0 || rel->r_offset > dstsec->sh_size - sizeof(u32)) {
			pr_err("%s: section %u reloc %u sym '%s': out of bounds relocation, offset %d size %u\n",
			       module->name, relindex, i, symname,
			       rel->r_offset, dstsec->sh_size);
			return -ENOEXEC;
		}

		loc = dstsec->sh_addr + rel->r_offset;

		switch (ELF32_R_TYPE(rel->r_info)) {
		case R_ARM_NONE:
			/* ignore */
			break;

		case R_ARM_ABS32:
		case R_ARM_TARGET1:
			*(u32 *)loc += sym->st_value;
			break;

		case R_ARM_PC24:
		case R_ARM_CALL:
		case R_ARM_JUMP24:
			if (sym->st_value & 3) {
				pr_err("%s: section %u reloc %u sym '%s': unsupported interworking call (ARM -> Thumb)\n",
				       module->name, relindex, i, symname);
				return -ENOEXEC;
			}

			offset = __mem_to_opcode_arm(*(u32 *)loc);
			offset = (offset & 0x00ffffff) << 2;
			if (offset & 0x02000000)
				offset -= 0x04000000;

			offset += sym->st_value - loc;

			/*
			 * Route through a PLT entry if 'offset' exceeds the
			 * supported range. Note that 'offset + loc + 8'
			 * contains the absolute jump target, i.e.,
			 * @sym + addend, corrected for the +8 PC bias.
			 */
			if (IS_ENABLED(CONFIG_ARM_MODULE_PLTS) &&
			    (offset <= (s32)0xfe000000 ||
			     offset >= (s32)0x02000000))
				offset = get_module_plt(module, loc,
							offset + loc + 8)
					 - loc - 8;

			if (offset <= (s32)0xfe000000 ||
			    offset >= (s32)0x02000000) {
				pr_err("%s: section %u reloc %u sym '%s': relocation %u out of range (%#lx -> %#x)\n",
				       module->name, relindex, i, symname,
				       ELF32_R_TYPE(rel->r_info), loc,
				       sym->st_value);
				return -ENOEXEC;
			}

			offset >>= 2;
			offset &= 0x00ffffff;

			*(u32 *)loc &= __opcode_to_mem_arm(0xff000000);
			*(u32 *)loc |= __opcode_to_mem_arm(offset);
			break;

	       case R_ARM_V4BX:
		       /* Preserve Rm and the condition code. Alter
			* other bits to re-code instruction as
			* MOV PC,Rm.
			*/
		       *(u32 *)loc &= __opcode_to_mem_arm(0xf000000f);
		       *(u32 *)loc |= __opcode_to_mem_arm(0x01a0f000);
		       break;

		case R_ARM_PREL31:
			offset = *(u32 *)loc + sym->st_value - loc;
			*(u32 *)loc = offset & 0x7fffffff;
			break;

		case R_ARM_MOVW_ABS_NC:
		case R_ARM_MOVT_ABS:
			offset = tmp = __mem_to_opcode_arm(*(u32 *)loc);
			offset = ((offset & 0xf0000) >> 4) | (offset & 0xfff);
			offset = (offset ^ 0x8000) - 0x8000;

			offset += sym->st_value;
			if (ELF32_R_TYPE(rel->r_info) == R_ARM_MOVT_ABS)
				offset >>= 16;

			tmp &= 0xfff0f000;
			tmp |= ((offset & 0xf000) << 4) |
				(offset & 0x0fff);

			*(u32 *)loc = __opcode_to_mem_arm(tmp);
			break;

#ifdef CONFIG_THUMB2_KERNEL
		case R_ARM_THM_CALL:
		case R_ARM_THM_JUMP24:
			/*
			 * For function symbols, only Thumb addresses are
			 * allowed (no interworking).
			 *
			 * For non-function symbols, the destination
			 * has no specific ARM/Thumb disposition, so
			 * the branch is resolved under the assumption
			 * that interworking is not required.
			 */
			if (ELF32_ST_TYPE(sym->st_info) == STT_FUNC &&
			    !(sym->st_value & 1)) {
				pr_err("%s: section %u reloc %u sym '%s': unsupported interworking call (Thumb -> ARM)\n",
				       module->name, relindex, i, symname);
				return -ENOEXEC;
			}

			upper = __mem_to_opcode_thumb16(*(u16 *)loc);
			lower = __mem_to_opcode_thumb16(*(u16 *)(loc + 2));

			/*
			 * 25 bit signed address range (Thumb-2 BL and B.W
			 * instructions):
			 *   S:I1:I2:imm10:imm11:0
			 * where:
			 *   S     = upper[10]   = offset[24]
			 *   I1    = ~(J1 ^ S)   = offset[23]
			 *   I2    = ~(J2 ^ S)   = offset[22]
			 *   imm10 = upper[9:0]  = offset[21:12]
			 *   imm11 = lower[10:0] = offset[11:1]
			 *   J1    = lower[13]
			 *   J2    = lower[11]
			 */
			sign = (upper >> 10) & 1;
			j1 = (lower >> 13) & 1;
			j2 = (lower >> 11) & 1;
			offset = (sign << 24) | ((~(j1 ^ sign) & 1) << 23) |
				((~(j2 ^ sign) & 1) << 22) |
				((upper & 0x03ff) << 12) |
				((lower & 0x07ff) << 1);
			if (offset & 0x01000000)
				offset -= 0x02000000;
			offset += sym->st_value - loc;

			/*
			 * Route through a PLT entry if 'offset' exceeds the
			 * supported range.
			 */
			if (IS_ENABLED(CONFIG_ARM_MODULE_PLTS) &&
			    (offset <= (s32)0xff000000 ||
			     offset >= (s32)0x01000000))
				offset = get_module_plt(module, loc,
							offset + loc + 4)
					 - loc - 4;

			if (offset <= (s32)0xff000000 ||
			    offset >= (s32)0x01000000) {
				pr_err("%s: section %u reloc %u sym '%s': relocation %u out of range (%#lx -> %#x)\n",
				       module->name, relindex, i, symname,
				       ELF32_R_TYPE(rel->r_info), loc,
				       sym->st_value);
				return -ENOEXEC;
			}

			sign = (offset >> 24) & 1;
			j1 = sign ^ (~(offset >> 23) & 1);
			j2 = sign ^ (~(offset >> 22) & 1);
			upper = (u16)((upper & 0xf800) | (sign << 10) |
					    ((offset >> 12) & 0x03ff));
			lower = (u16)((lower & 0xd000) |
				      (j1 << 13) | (j2 << 11) |
				      ((offset >> 1) & 0x07ff));

			*(u16 *)loc = __opcode_to_mem_thumb16(upper);
			*(u16 *)(loc + 2) = __opcode_to_mem_thumb16(lower);
			break;

		case R_ARM_THM_MOVW_ABS_NC:
		case R_ARM_THM_MOVT_ABS:
			upper = __mem_to_opcode_thumb16(*(u16 *)loc);
			lower = __mem_to_opcode_thumb16(*(u16 *)(loc + 2));

			/*
			 * MOVT/MOVW instructions encoding in Thumb-2:
			 *
			 * i	= upper[10]
			 * imm4	= upper[3:0]
			 * imm3	= lower[14:12]
			 * imm8	= lower[7:0]
			 *
			 * imm16 = imm4:i:imm3:imm8
			 */
			offset = ((upper & 0x000f) << 12) |
				((upper & 0x0400) << 1) |
				((lower & 0x7000) >> 4) | (lower & 0x00ff);
			offset = (offset ^ 0x8000) - 0x8000;
			offset += sym->st_value;

			if (ELF32_R_TYPE(rel->r_info) == R_ARM_THM_MOVT_ABS)
				offset >>= 16;

			upper = (u16)((upper & 0xfbf0) |
				      ((offset & 0xf000) >> 12) |
				      ((offset & 0x0800) >> 1));
			lower = (u16)((lower & 0x8f00) |
				      ((offset & 0x0700) << 4) |
				      (offset & 0x00ff));
			*(u16 *)loc = __opcode_to_mem_thumb16(upper);
			*(u16 *)(loc + 2) = __opcode_to_mem_thumb16(lower);
			break;
#endif

		default:
			pr_err("%s: unknown relocation: %u\n",
			       module->name, ELF32_R_TYPE(rel->r_info));
			return -ENOEXEC;
		}
	}
	return 0;
}

struct mod_unwind_map {
	const Elf_Shdr *unw_sec;
	const Elf_Shdr *txt_sec;
};

static const Elf_Shdr *find_mod_section(const Elf32_Ehdr *hdr,
	const Elf_Shdr *sechdrs, const char *name)
{
	const Elf_Shdr *s, *se;
	const char *secstrs = (void *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;

	for (s = sechdrs, se = sechdrs + hdr->e_shnum; s < se; s++)
		if (strcmp(name, secstrs + s->sh_name) == 0)
			return s;

	return NULL;
}

extern void fixup_pv_table(const void *, unsigned long);
extern void fixup_smp(const void *, unsigned long);

int module_finalize(const Elf32_Ehdr *hdr, const Elf_Shdr *sechdrs,
		    struct module *mod)
{
	const Elf_Shdr *s = NULL;
#ifdef CONFIG_ARM_UNWIND
	const char *secstrs = (void *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;
	const Elf_Shdr *sechdrs_end = sechdrs + hdr->e_shnum;
	struct mod_unwind_map maps[ARM_SEC_MAX];
	int i;

	memset(maps, 0, sizeof(maps));

	for (s = sechdrs; s < sechdrs_end; s++) {
		const char *secname = secstrs + s->sh_name;

		if (!(s->sh_flags & SHF_ALLOC))
			continue;

		if (strcmp(".ARM.exidx.init.text", secname) == 0)
			maps[ARM_SEC_INIT].unw_sec = s;
		else if (strcmp(".ARM.exidx", secname) == 0)
			maps[ARM_SEC_CORE].unw_sec = s;
		else if (strcmp(".ARM.exidx.exit.text", secname) == 0)
			maps[ARM_SEC_EXIT].unw_sec = s;
		else if (strcmp(".ARM.exidx.text.unlikely", secname) == 0)
			maps[ARM_SEC_UNLIKELY].unw_sec = s;
		else if (strcmp(".ARM.exidx.text.hot", secname) == 0)
			maps[ARM_SEC_HOT].unw_sec = s;
		else if (strcmp(".init.text", secname) == 0)
			maps[ARM_SEC_INIT].txt_sec = s;
		else if (strcmp(".text", secname) == 0)
			maps[ARM_SEC_CORE].txt_sec = s;
		else if (strcmp(".exit.text", secname) == 0)
			maps[ARM_SEC_EXIT].txt_sec = s;
		else if (strcmp(".text.unlikely", secname) == 0)
			maps[ARM_SEC_UNLIKELY].txt_sec = s;
		else if (strcmp(".text.hot", secname) == 0)
			maps[ARM_SEC_HOT].txt_sec = s;
	}

	for (i = 0; i < ARM_SEC_MAX; i++)
		if (maps[i].unw_sec && maps[i].txt_sec)
			mod->arch.unwind[i] =
				unwind_table_add(maps[i].unw_sec->sh_addr,
					         maps[i].unw_sec->sh_size,
					         maps[i].txt_sec->sh_addr,
					         maps[i].txt_sec->sh_size);
#endif
#ifdef CONFIG_ARM_PATCH_PHYS_VIRT
	s = find_mod_section(hdr, sechdrs, ".pv_table");
	if (s)
		fixup_pv_table((void *)s->sh_addr, s->sh_size);
#endif
	s = find_mod_section(hdr, sechdrs, ".alt.smp.init");
	if (s && !is_smp())
#ifdef CONFIG_SMP_ON_UP
		fixup_smp((void *)s->sh_addr, s->sh_size);
#else
		return -EINVAL;
#endif
	return 0;
}

void
module_arch_cleanup(struct module *mod)
{
#ifdef CONFIG_ARM_UNWIND
	int i;

	for (i = 0; i < ARM_SEC_MAX; i++)
		if (mod->arch.unwind[i])
			unwind_table_del(mod->arch.unwind[i]);
#endif
}
