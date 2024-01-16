// SPDX-License-Identifier: GPL-2.0+
/*
 *  Kernel module help for s390.
 *
 *  S390 version
 *    Copyright IBM Corp. 2002, 2003
 *    Author(s): Arnd Bergmann (arndb@de.ibm.com)
 *		 Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  based on i386 version
 *    Copyright (C) 2001 Rusty Russell.
 */
#include <linux/module.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/ftrace.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/kasan.h>
#include <linux/moduleloader.h>
#include <linux/bug.h>
#include <linux/memory.h>
#include <asm/alternative.h>
#include <asm/nospec-branch.h>
#include <asm/facility.h>
#include <asm/ftrace.lds.h>
#include <asm/set_memory.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(fmt , ...)
#endif

#define PLT_ENTRY_SIZE 22

void *module_alloc(unsigned long size)
{
	gfp_t gfp_mask = GFP_KERNEL;
	void *p;

	if (PAGE_ALIGN(size) > MODULES_LEN)
		return NULL;
	p = __vmalloc_node_range(size, MODULE_ALIGN, MODULES_VADDR, MODULES_END,
				 gfp_mask, PAGE_KERNEL_EXEC, VM_DEFER_KMEMLEAK, NUMA_NO_NODE,
				 __builtin_return_address(0));
	if (p && (kasan_alloc_module_shadow(p, size, gfp_mask) < 0)) {
		vfree(p);
		return NULL;
	}
	return p;
}

#ifdef CONFIG_FUNCTION_TRACER
void module_arch_cleanup(struct module *mod)
{
	module_memfree(mod->arch.trampolines_start);
}
#endif

void module_arch_freeing_init(struct module *mod)
{
	if (is_livepatch_module(mod) &&
	    mod->state == MODULE_STATE_LIVE)
		return;

	vfree(mod->arch.syminfo);
	mod->arch.syminfo = NULL;
}

static void check_rela(Elf_Rela *rela, struct module *me)
{
	struct mod_arch_syminfo *info;

	info = me->arch.syminfo + ELF_R_SYM (rela->r_info);
	switch (ELF_R_TYPE (rela->r_info)) {
	case R_390_GOT12:	/* 12 bit GOT offset.  */
	case R_390_GOT16:	/* 16 bit GOT offset.  */
	case R_390_GOT20:	/* 20 bit GOT offset.  */
	case R_390_GOT32:	/* 32 bit GOT offset.  */
	case R_390_GOT64:	/* 64 bit GOT offset.  */
	case R_390_GOTENT:	/* 32 bit PC rel. to GOT entry shifted by 1. */
	case R_390_GOTPLT12:	/* 12 bit offset to jump slot.	*/
	case R_390_GOTPLT16:	/* 16 bit offset to jump slot.  */
	case R_390_GOTPLT20:	/* 20 bit offset to jump slot.  */
	case R_390_GOTPLT32:	/* 32 bit offset to jump slot.  */
	case R_390_GOTPLT64:	/* 64 bit offset to jump slot.	*/
	case R_390_GOTPLTENT:	/* 32 bit rel. offset to jump slot >> 1. */
		if (info->got_offset == -1UL) {
			info->got_offset = me->arch.got_size;
			me->arch.got_size += sizeof(void*);
		}
		break;
	case R_390_PLT16DBL:	/* 16 bit PC rel. PLT shifted by 1.  */
	case R_390_PLT32DBL:	/* 32 bit PC rel. PLT shifted by 1.  */
	case R_390_PLT32:	/* 32 bit PC relative PLT address.  */
	case R_390_PLT64:	/* 64 bit PC relative PLT address.  */
	case R_390_PLTOFF16:	/* 16 bit offset from GOT to PLT. */
	case R_390_PLTOFF32:	/* 32 bit offset from GOT to PLT. */
	case R_390_PLTOFF64:	/* 16 bit offset from GOT to PLT. */
		if (info->plt_offset == -1UL) {
			info->plt_offset = me->arch.plt_size;
			me->arch.plt_size += PLT_ENTRY_SIZE;
		}
		break;
	case R_390_COPY:
	case R_390_GLOB_DAT:
	case R_390_JMP_SLOT:
	case R_390_RELATIVE:
		/* Only needed if we want to support loading of 
		   modules linked with -shared. */
		break;
	}
}

/*
 * Account for GOT and PLT relocations. We can't add sections for
 * got and plt but we can increase the core module size.
 */
int module_frob_arch_sections(Elf_Ehdr *hdr, Elf_Shdr *sechdrs,
			      char *secstrings, struct module *me)
{
	Elf_Shdr *symtab;
	Elf_Sym *symbols;
	Elf_Rela *rela;
	char *strings;
	int nrela, i, j;

	/* Find symbol table and string table. */
	symtab = NULL;
	for (i = 0; i < hdr->e_shnum; i++)
		switch (sechdrs[i].sh_type) {
		case SHT_SYMTAB:
			symtab = sechdrs + i;
			break;
		}
	if (!symtab) {
		printk(KERN_ERR "module %s: no symbol table\n", me->name);
		return -ENOEXEC;
	}

	/* Allocate one syminfo structure per symbol. */
	me->arch.nsyms = symtab->sh_size / sizeof(Elf_Sym);
	me->arch.syminfo = vmalloc(array_size(sizeof(struct mod_arch_syminfo),
					      me->arch.nsyms));
	if (!me->arch.syminfo)
		return -ENOMEM;
	symbols = (void *) hdr + symtab->sh_offset;
	strings = (void *) hdr + sechdrs[symtab->sh_link].sh_offset;
	for (i = 0; i < me->arch.nsyms; i++) {
		if (symbols[i].st_shndx == SHN_UNDEF &&
		    strcmp(strings + symbols[i].st_name,
			   "_GLOBAL_OFFSET_TABLE_") == 0)
			/* "Define" it as absolute. */
			symbols[i].st_shndx = SHN_ABS;
		me->arch.syminfo[i].got_offset = -1UL;
		me->arch.syminfo[i].plt_offset = -1UL;
		me->arch.syminfo[i].got_initialized = 0;
		me->arch.syminfo[i].plt_initialized = 0;
	}

	/* Search for got/plt relocations. */
	me->arch.got_size = me->arch.plt_size = 0;
	for (i = 0; i < hdr->e_shnum; i++) {
		if (sechdrs[i].sh_type != SHT_RELA)
			continue;
		nrela = sechdrs[i].sh_size / sizeof(Elf_Rela);
		rela = (void *) hdr + sechdrs[i].sh_offset;
		for (j = 0; j < nrela; j++)
			check_rela(rela + j, me);
	}

	/* Increase core size by size of got & plt and set start
	   offsets for got and plt. */
	me->core_layout.size = ALIGN(me->core_layout.size, 4);
	me->arch.got_offset = me->core_layout.size;
	me->core_layout.size += me->arch.got_size;
	me->arch.plt_offset = me->core_layout.size;
	if (me->arch.plt_size) {
		if (IS_ENABLED(CONFIG_EXPOLINE) && !nospec_disable)
			me->arch.plt_size += PLT_ENTRY_SIZE;
		me->core_layout.size += me->arch.plt_size;
	}
	return 0;
}

static int apply_rela_bits(Elf_Addr loc, Elf_Addr val,
			   int sign, int bits, int shift,
			   void *(*write)(void *dest, const void *src, size_t len))
{
	unsigned long umax;
	long min, max;
	void *dest = (void *)loc;

	if (val & ((1UL << shift) - 1))
		return -ENOEXEC;
	if (sign) {
		val = (Elf_Addr)(((long) val) >> shift);
		min = -(1L << (bits - 1));
		max = (1L << (bits - 1)) - 1;
		if ((long) val < min || (long) val > max)
			return -ENOEXEC;
	} else {
		val >>= shift;
		umax = ((1UL << (bits - 1)) << 1) - 1;
		if ((unsigned long) val > umax)
			return -ENOEXEC;
	}

	if (bits == 8) {
		unsigned char tmp = val;
		write(dest, &tmp, 1);
	} else if (bits == 12) {
		unsigned short tmp = (val & 0xfff) |
			(*(unsigned short *) loc & 0xf000);
		write(dest, &tmp, 2);
	} else if (bits == 16) {
		unsigned short tmp = val;
		write(dest, &tmp, 2);
	} else if (bits == 20) {
		unsigned int tmp = (val & 0xfff) << 16 |
			(val & 0xff000) >> 4 | (*(unsigned int *) loc & 0xf00000ff);
		write(dest, &tmp, 4);
	} else if (bits == 32) {
		unsigned int tmp = val;
		write(dest, &tmp, 4);
	} else if (bits == 64) {
		unsigned long tmp = val;
		write(dest, &tmp, 8);
	}
	return 0;
}

static int apply_rela(Elf_Rela *rela, Elf_Addr base, Elf_Sym *symtab,
		      const char *strtab, struct module *me,
		      void *(*write)(void *dest, const void *src, size_t len))
{
	struct mod_arch_syminfo *info;
	Elf_Addr loc, val;
	int r_type, r_sym;
	int rc = -ENOEXEC;

	/* This is where to make the change */
	loc = base + rela->r_offset;
	/* This is the symbol it is referring to.  Note that all
	   undefined symbols have been resolved.  */
	r_sym = ELF_R_SYM(rela->r_info);
	r_type = ELF_R_TYPE(rela->r_info);
	info = me->arch.syminfo + r_sym;
	val = symtab[r_sym].st_value;

	switch (r_type) {
	case R_390_NONE:	/* No relocation.  */
		rc = 0;
		break;
	case R_390_8:		/* Direct 8 bit.   */
	case R_390_12:		/* Direct 12 bit.  */
	case R_390_16:		/* Direct 16 bit.  */
	case R_390_20:		/* Direct 20 bit.  */
	case R_390_32:		/* Direct 32 bit.  */
	case R_390_64:		/* Direct 64 bit.  */
		val += rela->r_addend;
		if (r_type == R_390_8)
			rc = apply_rela_bits(loc, val, 0, 8, 0, write);
		else if (r_type == R_390_12)
			rc = apply_rela_bits(loc, val, 0, 12, 0, write);
		else if (r_type == R_390_16)
			rc = apply_rela_bits(loc, val, 0, 16, 0, write);
		else if (r_type == R_390_20)
			rc = apply_rela_bits(loc, val, 1, 20, 0, write);
		else if (r_type == R_390_32)
			rc = apply_rela_bits(loc, val, 0, 32, 0, write);
		else if (r_type == R_390_64)
			rc = apply_rela_bits(loc, val, 0, 64, 0, write);
		break;
	case R_390_PC16:	/* PC relative 16 bit.  */
	case R_390_PC16DBL:	/* PC relative 16 bit shifted by 1.  */
	case R_390_PC32DBL:	/* PC relative 32 bit shifted by 1.  */
	case R_390_PC32:	/* PC relative 32 bit.  */
	case R_390_PC64:	/* PC relative 64 bit.	*/
		val += rela->r_addend - loc;
		if (r_type == R_390_PC16)
			rc = apply_rela_bits(loc, val, 1, 16, 0, write);
		else if (r_type == R_390_PC16DBL)
			rc = apply_rela_bits(loc, val, 1, 16, 1, write);
		else if (r_type == R_390_PC32DBL)
			rc = apply_rela_bits(loc, val, 1, 32, 1, write);
		else if (r_type == R_390_PC32)
			rc = apply_rela_bits(loc, val, 1, 32, 0, write);
		else if (r_type == R_390_PC64)
			rc = apply_rela_bits(loc, val, 1, 64, 0, write);
		break;
	case R_390_GOT12:	/* 12 bit GOT offset.  */
	case R_390_GOT16:	/* 16 bit GOT offset.  */
	case R_390_GOT20:	/* 20 bit GOT offset.  */
	case R_390_GOT32:	/* 32 bit GOT offset.  */
	case R_390_GOT64:	/* 64 bit GOT offset.  */
	case R_390_GOTENT:	/* 32 bit PC rel. to GOT entry shifted by 1. */
	case R_390_GOTPLT12:	/* 12 bit offset to jump slot.	*/
	case R_390_GOTPLT20:	/* 20 bit offset to jump slot.  */
	case R_390_GOTPLT16:	/* 16 bit offset to jump slot.  */
	case R_390_GOTPLT32:	/* 32 bit offset to jump slot.  */
	case R_390_GOTPLT64:	/* 64 bit offset to jump slot.	*/
	case R_390_GOTPLTENT:	/* 32 bit rel. offset to jump slot >> 1. */
		if (info->got_initialized == 0) {
			Elf_Addr *gotent = me->core_layout.base +
					   me->arch.got_offset +
					   info->got_offset;

			write(gotent, &val, sizeof(*gotent));
			info->got_initialized = 1;
		}
		val = info->got_offset + rela->r_addend;
		if (r_type == R_390_GOT12 ||
		    r_type == R_390_GOTPLT12)
			rc = apply_rela_bits(loc, val, 0, 12, 0, write);
		else if (r_type == R_390_GOT16 ||
			 r_type == R_390_GOTPLT16)
			rc = apply_rela_bits(loc, val, 0, 16, 0, write);
		else if (r_type == R_390_GOT20 ||
			 r_type == R_390_GOTPLT20)
			rc = apply_rela_bits(loc, val, 1, 20, 0, write);
		else if (r_type == R_390_GOT32 ||
			 r_type == R_390_GOTPLT32)
			rc = apply_rela_bits(loc, val, 0, 32, 0, write);
		else if (r_type == R_390_GOT64 ||
			 r_type == R_390_GOTPLT64)
			rc = apply_rela_bits(loc, val, 0, 64, 0, write);
		else if (r_type == R_390_GOTENT ||
			 r_type == R_390_GOTPLTENT) {
			val += (Elf_Addr) me->core_layout.base - loc;
			rc = apply_rela_bits(loc, val, 1, 32, 1, write);
		}
		break;
	case R_390_PLT16DBL:	/* 16 bit PC rel. PLT shifted by 1.  */
	case R_390_PLT32DBL:	/* 32 bit PC rel. PLT shifted by 1.  */
	case R_390_PLT32:	/* 32 bit PC relative PLT address.  */
	case R_390_PLT64:	/* 64 bit PC relative PLT address.  */
	case R_390_PLTOFF16:	/* 16 bit offset from GOT to PLT. */
	case R_390_PLTOFF32:	/* 32 bit offset from GOT to PLT. */
	case R_390_PLTOFF64:	/* 16 bit offset from GOT to PLT. */
		if (info->plt_initialized == 0) {
			unsigned char insn[PLT_ENTRY_SIZE];
			char *plt_base;
			char *ip;

			plt_base = me->core_layout.base + me->arch.plt_offset;
			ip = plt_base + info->plt_offset;
			*(int *)insn = 0x0d10e310;	/* basr 1,0  */
			*(int *)&insn[4] = 0x100c0004;	/* lg	1,12(1) */
			if (IS_ENABLED(CONFIG_EXPOLINE) && !nospec_disable) {
				char *jump_r1;

				jump_r1 = plt_base + me->arch.plt_size -
					PLT_ENTRY_SIZE;
				/* brcl	0xf,__jump_r1 */
				*(short *)&insn[8] = 0xc0f4;
				*(int *)&insn[10] = (jump_r1 - (ip + 8)) / 2;
			} else {
				*(int *)&insn[8] = 0x07f10000;	/* br %r1 */
			}
			*(long *)&insn[14] = val;

			write(ip, insn, sizeof(insn));
			info->plt_initialized = 1;
		}
		if (r_type == R_390_PLTOFF16 ||
		    r_type == R_390_PLTOFF32 ||
		    r_type == R_390_PLTOFF64)
			val = me->arch.plt_offset - me->arch.got_offset +
				info->plt_offset + rela->r_addend;
		else {
			if (!((r_type == R_390_PLT16DBL &&
			       val - loc + 0xffffUL < 0x1ffffeUL) ||
			      (r_type == R_390_PLT32DBL &&
			       val - loc + 0xffffffffULL < 0x1fffffffeULL)))
				val = (Elf_Addr) me->core_layout.base +
					me->arch.plt_offset +
					info->plt_offset;
			val += rela->r_addend - loc;
		}
		if (r_type == R_390_PLT16DBL)
			rc = apply_rela_bits(loc, val, 1, 16, 1, write);
		else if (r_type == R_390_PLTOFF16)
			rc = apply_rela_bits(loc, val, 0, 16, 0, write);
		else if (r_type == R_390_PLT32DBL)
			rc = apply_rela_bits(loc, val, 1, 32, 1, write);
		else if (r_type == R_390_PLT32 ||
			 r_type == R_390_PLTOFF32)
			rc = apply_rela_bits(loc, val, 0, 32, 0, write);
		else if (r_type == R_390_PLT64 ||
			 r_type == R_390_PLTOFF64)
			rc = apply_rela_bits(loc, val, 0, 64, 0, write);
		break;
	case R_390_GOTOFF16:	/* 16 bit offset to GOT.  */
	case R_390_GOTOFF32:	/* 32 bit offset to GOT.  */
	case R_390_GOTOFF64:	/* 64 bit offset to GOT. */
		val = val + rela->r_addend -
			((Elf_Addr) me->core_layout.base + me->arch.got_offset);
		if (r_type == R_390_GOTOFF16)
			rc = apply_rela_bits(loc, val, 0, 16, 0, write);
		else if (r_type == R_390_GOTOFF32)
			rc = apply_rela_bits(loc, val, 0, 32, 0, write);
		else if (r_type == R_390_GOTOFF64)
			rc = apply_rela_bits(loc, val, 0, 64, 0, write);
		break;
	case R_390_GOTPC:	/* 32 bit PC relative offset to GOT. */
	case R_390_GOTPCDBL:	/* 32 bit PC rel. off. to GOT shifted by 1. */
		val = (Elf_Addr) me->core_layout.base + me->arch.got_offset +
			rela->r_addend - loc;
		if (r_type == R_390_GOTPC)
			rc = apply_rela_bits(loc, val, 1, 32, 0, write);
		else if (r_type == R_390_GOTPCDBL)
			rc = apply_rela_bits(loc, val, 1, 32, 1, write);
		break;
	case R_390_COPY:
	case R_390_GLOB_DAT:	/* Create GOT entry.  */
	case R_390_JMP_SLOT:	/* Create PLT entry.  */
	case R_390_RELATIVE:	/* Adjust by program base.  */
		/* Only needed if we want to support loading of 
		   modules linked with -shared. */
		return -ENOEXEC;
	default:
		printk(KERN_ERR "module %s: unknown relocation: %u\n",
		       me->name, r_type);
		return -ENOEXEC;
	}
	if (rc) {
		printk(KERN_ERR "module %s: relocation error for symbol %s "
		       "(r_type %i, value 0x%lx)\n",
		       me->name, strtab + symtab[r_sym].st_name,
		       r_type, (unsigned long) val);
		return rc;
	}
	return 0;
}

static int __apply_relocate_add(Elf_Shdr *sechdrs, const char *strtab,
		       unsigned int symindex, unsigned int relsec,
		       struct module *me,
		       void *(*write)(void *dest, const void *src, size_t len))
{
	Elf_Addr base;
	Elf_Sym *symtab;
	Elf_Rela *rela;
	unsigned long i, n;
	int rc;

	DEBUGP("Applying relocate section %u to %u\n",
	       relsec, sechdrs[relsec].sh_info);
	base = sechdrs[sechdrs[relsec].sh_info].sh_addr;
	symtab = (Elf_Sym *) sechdrs[symindex].sh_addr;
	rela = (Elf_Rela *) sechdrs[relsec].sh_addr;
	n = sechdrs[relsec].sh_size / sizeof(Elf_Rela);

	for (i = 0; i < n; i++, rela++) {
		rc = apply_rela(rela, base, symtab, strtab, me, write);
		if (rc)
			return rc;
	}
	return 0;
}

int apply_relocate_add(Elf_Shdr *sechdrs, const char *strtab,
		       unsigned int symindex, unsigned int relsec,
		       struct module *me)
{
	bool early = me->state == MODULE_STATE_UNFORMED;
	void *(*write)(void *, const void *, size_t) = memcpy;

	if (!early)
		write = s390_kernel_write;

	return __apply_relocate_add(sechdrs, strtab, symindex, relsec, me,
				    write);
}

#ifdef CONFIG_FUNCTION_TRACER
static int module_alloc_ftrace_hotpatch_trampolines(struct module *me,
						    const Elf_Shdr *s)
{
	char *start, *end;
	int numpages;
	size_t size;

	size = FTRACE_HOTPATCH_TRAMPOLINES_SIZE(s->sh_size);
	numpages = DIV_ROUND_UP(size, PAGE_SIZE);
	start = module_alloc(numpages * PAGE_SIZE);
	if (!start)
		return -ENOMEM;
	set_memory_ro((unsigned long)start, numpages);
	end = start + size;

	me->arch.trampolines_start = (struct ftrace_hotpatch_trampoline *)start;
	me->arch.trampolines_end = (struct ftrace_hotpatch_trampoline *)end;
	me->arch.next_trampoline = me->arch.trampolines_start;

	return 0;
}
#endif /* CONFIG_FUNCTION_TRACER */

int module_finalize(const Elf_Ehdr *hdr,
		    const Elf_Shdr *sechdrs,
		    struct module *me)
{
	const Elf_Shdr *s;
	char *secstrings, *secname;
	void *aseg;
#ifdef CONFIG_FUNCTION_TRACER
	int ret;
#endif

	if (IS_ENABLED(CONFIG_EXPOLINE) &&
	    !nospec_disable && me->arch.plt_size) {
		unsigned int *ij;

		ij = me->core_layout.base + me->arch.plt_offset +
			me->arch.plt_size - PLT_ENTRY_SIZE;
		ij[0] = 0xc6000000;	/* exrl	%r0,.+10	*/
		ij[1] = 0x0005a7f4;	/* j	.		*/
		ij[2] = 0x000007f1;	/* br	%r1		*/
	}

	secstrings = (void *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;
	for (s = sechdrs; s < sechdrs + hdr->e_shnum; s++) {
		aseg = (void *) s->sh_addr;
		secname = secstrings + s->sh_name;

		if (!strcmp(".altinstructions", secname))
			/* patch .altinstructions */
			apply_alternatives(aseg, aseg + s->sh_size);

		if (IS_ENABLED(CONFIG_EXPOLINE) &&
		    (str_has_prefix(secname, ".s390_indirect")))
			nospec_revert(aseg, aseg + s->sh_size);

		if (IS_ENABLED(CONFIG_EXPOLINE) &&
		    (str_has_prefix(secname, ".s390_return")))
			nospec_revert(aseg, aseg + s->sh_size);

#ifdef CONFIG_FUNCTION_TRACER
		if (!strcmp(FTRACE_CALLSITE_SECTION, secname)) {
			ret = module_alloc_ftrace_hotpatch_trampolines(me, s);
			if (ret < 0)
				return ret;
		}
#endif /* CONFIG_FUNCTION_TRACER */
	}

	return 0;
}
