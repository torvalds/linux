// SPDX-License-Identifier: GPL-2.0-or-later
/*  Kernel module help for PPC64.
    Copyright (C) 2001, 2003 Rusty Russell IBM Corporation.

*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/elf.h>
#include <linux/moduleloader.h>
#include <linux/err.h>
#include <linux/vmalloc.h>
#include <linux/ftrace.h>
#include <linux/bug.h>
#include <linux/uaccess.h>
#include <asm/module.h>
#include <asm/firmware.h>
#include <asm/code-patching.h>
#include <linux/sort.h>
#include <asm/setup.h>
#include <asm/sections.h>
#include <asm/inst.h>

/* FIXME: We don't do .init separately.  To do this, we'd need to have
   a separate r2 value in the init and core section, and stub between
   them, too.

   Using a magic allocator which places modules within 32MB solves
   this, and makes other things simpler.  Anton?
   --RR.  */

#ifdef PPC64_ELF_ABI_v2

/* An address is simply the address of the function. */
typedef unsigned long func_desc_t;

static func_desc_t func_desc(unsigned long addr)
{
	return addr;
}
static unsigned long func_addr(unsigned long addr)
{
	return addr;
}
static unsigned long stub_func_addr(func_desc_t func)
{
	return func;
}

/* PowerPC64 specific values for the Elf64_Sym st_other field.  */
#define STO_PPC64_LOCAL_BIT	5
#define STO_PPC64_LOCAL_MASK	(7 << STO_PPC64_LOCAL_BIT)
#define PPC64_LOCAL_ENTRY_OFFSET(other)					\
 (((1 << (((other) & STO_PPC64_LOCAL_MASK) >> STO_PPC64_LOCAL_BIT)) >> 2) << 2)

static unsigned int local_entry_offset(const Elf64_Sym *sym)
{
	/* sym->st_other indicates offset to local entry point
	 * (otherwise it will assume r12 is the address of the start
	 * of function and try to derive r2 from it). */
	return PPC64_LOCAL_ENTRY_OFFSET(sym->st_other);
}
#else

/* An address is address of the OPD entry, which contains address of fn. */
typedef struct ppc64_opd_entry func_desc_t;

static func_desc_t func_desc(unsigned long addr)
{
	return *(struct ppc64_opd_entry *)addr;
}
static unsigned long func_addr(unsigned long addr)
{
	return func_desc(addr).funcaddr;
}
static unsigned long stub_func_addr(func_desc_t func)
{
	return func.funcaddr;
}
static unsigned int local_entry_offset(const Elf64_Sym *sym)
{
	return 0;
}

void *dereference_module_function_descriptor(struct module *mod, void *ptr)
{
	if (ptr < (void *)mod->arch.start_opd ||
			ptr >= (void *)mod->arch.end_opd)
		return ptr;

	return dereference_function_descriptor(ptr);
}
#endif

#define STUB_MAGIC 0x73747562 /* stub */

/* Like PPC32, we need little trampolines to do > 24-bit jumps (into
   the kernel itself).  But on PPC64, these need to be used for every
   jump, actually, to reset r2 (TOC+0x8000). */
struct ppc64_stub_entry
{
	/* 28 byte jump instruction sequence (7 instructions). We only
	 * need 6 instructions on ABIv2 but we always allocate 7 so
	 * so we don't have to modify the trampoline load instruction. */
	u32 jump[7];
	/* Used by ftrace to identify stubs */
	u32 magic;
	/* Data for the above code */
	func_desc_t funcdata;
};

/*
 * PPC64 uses 24 bit jumps, but we need to jump into other modules or
 * the kernel which may be further.  So we jump to a stub.
 *
 * For ELFv1 we need to use this to set up the new r2 value (aka TOC
 * pointer).  For ELFv2 it's the callee's responsibility to set up the
 * new r2, but for both we need to save the old r2.
 *
 * We could simply patch the new r2 value and function pointer into
 * the stub, but it's significantly shorter to put these values at the
 * end of the stub code, and patch the stub address (32-bits relative
 * to the TOC ptr, r2) into the stub.
 *
 * addis   r11,r2, <high>
 * addi    r11,r11, <low>
 * std     r2,R2_STACK_OFFSET(r1)
 * ld      r12,32(r11)
 * ld      r2,40(r11)
 * mtctr   r12
 * bctr
 */
static u32 ppc64_stub_insns[] = {
	PPC_INST_ADDIS | __PPC_RT(R11) | __PPC_RA(R2),
	PPC_INST_ADDI | __PPC_RT(R11) | __PPC_RA(R11),
	/* Save current r2 value in magic place on the stack. */
	PPC_INST_STD | __PPC_RS(R2) | __PPC_RA(R1) | R2_STACK_OFFSET,
	PPC_INST_LD | __PPC_RT(R12) | __PPC_RA(R11) | 32,
#ifdef PPC64_ELF_ABI_v1
	/* Set up new r2 from function descriptor */
	PPC_INST_LD | __PPC_RT(R2) | __PPC_RA(R11) | 40,
#endif
	PPC_INST_MTCTR | __PPC_RS(R12),
	PPC_INST_BCTR,
};

/* Count how many different 24-bit relocations (different symbol,
   different addend) */
static unsigned int count_relocs(const Elf64_Rela *rela, unsigned int num)
{
	unsigned int i, r_info, r_addend, _count_relocs;

	/* FIXME: Only count external ones --RR */
	_count_relocs = 0;
	r_info = 0;
	r_addend = 0;
	for (i = 0; i < num; i++)
		/* Only count 24-bit relocs, others don't need stubs */
		if (ELF64_R_TYPE(rela[i].r_info) == R_PPC_REL24 &&
		    (r_info != ELF64_R_SYM(rela[i].r_info) ||
		     r_addend != rela[i].r_addend)) {
			_count_relocs++;
			r_info = ELF64_R_SYM(rela[i].r_info);
			r_addend = rela[i].r_addend;
		}

	return _count_relocs;
}

static int relacmp(const void *_x, const void *_y)
{
	const Elf64_Rela *x, *y;

	y = (Elf64_Rela *)_x;
	x = (Elf64_Rela *)_y;

	/* Compare the entire r_info (as opposed to ELF64_R_SYM(r_info) only) to
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

/* Get size of potential trampolines required. */
static unsigned long get_stubs_size(const Elf64_Ehdr *hdr,
				    const Elf64_Shdr *sechdrs)
{
	/* One extra reloc so it's always 0-funcaddr terminated */
	unsigned long relocs = 1;
	unsigned i;

	/* Every relocated section... */
	for (i = 1; i < hdr->e_shnum; i++) {
		if (sechdrs[i].sh_type == SHT_RELA) {
			pr_debug("Found relocations in section %u\n", i);
			pr_debug("Ptr: %p.  Number: %Lu\n",
			       (void *)sechdrs[i].sh_addr,
			       sechdrs[i].sh_size / sizeof(Elf64_Rela));

			/* Sort the relocation information based on a symbol and
			 * addend key. This is a stable O(n*log n) complexity
			 * alogrithm but it will reduce the complexity of
			 * count_relocs() to linear complexity O(n)
			 */
			sort((void *)sechdrs[i].sh_addr,
			     sechdrs[i].sh_size / sizeof(Elf64_Rela),
			     sizeof(Elf64_Rela), relacmp, NULL);

			relocs += count_relocs((void *)sechdrs[i].sh_addr,
					       sechdrs[i].sh_size
					       / sizeof(Elf64_Rela));
		}
	}

#ifdef CONFIG_DYNAMIC_FTRACE
	/* make the trampoline to the ftrace_caller */
	relocs++;
#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
	/* an additional one for ftrace_regs_caller */
	relocs++;
#endif
#endif

	pr_debug("Looks like a total of %lu stubs, max\n", relocs);
	return relocs * sizeof(struct ppc64_stub_entry);
}

/* Still needed for ELFv2, for .TOC. */
static void dedotify_versions(struct modversion_info *vers,
			      unsigned long size)
{
	struct modversion_info *end;

	for (end = (void *)vers + size; vers < end; vers++)
		if (vers->name[0] == '.') {
			memmove(vers->name, vers->name+1, strlen(vers->name));
		}
}

/*
 * Undefined symbols which refer to .funcname, hack to funcname. Make .TOC.
 * seem to be defined (value set later).
 */
static void dedotify(Elf64_Sym *syms, unsigned int numsyms, char *strtab)
{
	unsigned int i;

	for (i = 1; i < numsyms; i++) {
		if (syms[i].st_shndx == SHN_UNDEF) {
			char *name = strtab + syms[i].st_name;
			if (name[0] == '.') {
				if (strcmp(name+1, "TOC.") == 0)
					syms[i].st_shndx = SHN_ABS;
				syms[i].st_name++;
			}
		}
	}
}

static Elf64_Sym *find_dot_toc(Elf64_Shdr *sechdrs,
			       const char *strtab,
			       unsigned int symindex)
{
	unsigned int i, numsyms;
	Elf64_Sym *syms;

	syms = (Elf64_Sym *)sechdrs[symindex].sh_addr;
	numsyms = sechdrs[symindex].sh_size / sizeof(Elf64_Sym);

	for (i = 1; i < numsyms; i++) {
		if (syms[i].st_shndx == SHN_ABS
		    && strcmp(strtab + syms[i].st_name, "TOC.") == 0)
			return &syms[i];
	}
	return NULL;
}

int module_frob_arch_sections(Elf64_Ehdr *hdr,
			      Elf64_Shdr *sechdrs,
			      char *secstrings,
			      struct module *me)
{
	unsigned int i;

	/* Find .toc and .stubs sections, symtab and strtab */
	for (i = 1; i < hdr->e_shnum; i++) {
		char *p;
		if (strcmp(secstrings + sechdrs[i].sh_name, ".stubs") == 0)
			me->arch.stubs_section = i;
		else if (strcmp(secstrings + sechdrs[i].sh_name, ".toc") == 0) {
			me->arch.toc_section = i;
			if (sechdrs[i].sh_addralign < 8)
				sechdrs[i].sh_addralign = 8;
		}
		else if (strcmp(secstrings+sechdrs[i].sh_name,"__versions")==0)
			dedotify_versions((void *)hdr + sechdrs[i].sh_offset,
					  sechdrs[i].sh_size);

		/* We don't handle .init for the moment: rename to _init */
		while ((p = strstr(secstrings + sechdrs[i].sh_name, ".init")))
			p[0] = '_';

		if (sechdrs[i].sh_type == SHT_SYMTAB)
			dedotify((void *)hdr + sechdrs[i].sh_offset,
				 sechdrs[i].sh_size / sizeof(Elf64_Sym),
				 (void *)hdr
				 + sechdrs[sechdrs[i].sh_link].sh_offset);
	}

	if (!me->arch.stubs_section) {
		pr_err("%s: doesn't contain .stubs.\n", me->name);
		return -ENOEXEC;
	}

	/* If we don't have a .toc, just use .stubs.  We need to set r2
	   to some reasonable value in case the module calls out to
	   other functions via a stub, or if a function pointer escapes
	   the module by some means.  */
	if (!me->arch.toc_section)
		me->arch.toc_section = me->arch.stubs_section;

	/* Override the stubs size */
	sechdrs[me->arch.stubs_section].sh_size = get_stubs_size(hdr, sechdrs);
	return 0;
}

#ifdef CONFIG_MPROFILE_KERNEL

#define PACATOC offsetof(struct paca_struct, kernel_toc)

/*
 * ld      r12,PACATOC(r13)
 * addis   r12,r12,<high>
 * addi    r12,r12,<low>
 * mtctr   r12
 * bctr
 */
static u32 stub_insns[] = {
	PPC_INST_LD | __PPC_RT(R12) | __PPC_RA(R13) | PACATOC,
	PPC_INST_ADDIS | __PPC_RT(R12) | __PPC_RA(R12),
	PPC_INST_ADDI | __PPC_RT(R12) | __PPC_RA(R12),
	PPC_INST_MTCTR | __PPC_RS(R12),
	PPC_INST_BCTR,
};

/*
 * For mprofile-kernel we use a special stub for ftrace_caller() because we
 * can't rely on r2 containing this module's TOC when we enter the stub.
 *
 * That can happen if the function calling us didn't need to use the toc. In
 * that case it won't have setup r2, and the r2 value will be either the
 * kernel's toc, or possibly another modules toc.
 *
 * To deal with that this stub uses the kernel toc, which is always accessible
 * via the paca (in r13). The target (ftrace_caller()) is responsible for
 * saving and restoring the toc before returning.
 */
static inline int create_ftrace_stub(struct ppc64_stub_entry *entry,
					unsigned long addr,
					struct module *me)
{
	long reladdr;

	memcpy(entry->jump, stub_insns, sizeof(stub_insns));

	/* Stub uses address relative to kernel toc (from the paca) */
	reladdr = addr - kernel_toc_addr();
	if (reladdr > 0x7FFFFFFF || reladdr < -(0x80000000L)) {
		pr_err("%s: Address of %ps out of range of kernel_toc.\n",
							me->name, (void *)addr);
		return 0;
	}

	entry->jump[1] |= PPC_HA(reladdr);
	entry->jump[2] |= PPC_LO(reladdr);

	/* Eventhough we don't use funcdata in the stub, it's needed elsewhere. */
	entry->funcdata = func_desc(addr);
	entry->magic = STUB_MAGIC;

	return 1;
}

static bool is_mprofile_ftrace_call(const char *name)
{
	if (!strcmp("_mcount", name))
		return true;
#ifdef CONFIG_DYNAMIC_FTRACE
	if (!strcmp("ftrace_caller", name))
		return true;
#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
	if (!strcmp("ftrace_regs_caller", name))
		return true;
#endif
#endif

	return false;
}
#else
static inline int create_ftrace_stub(struct ppc64_stub_entry *entry,
					unsigned long addr,
					struct module *me)
{
	return 0;
}

static bool is_mprofile_ftrace_call(const char *name)
{
	return false;
}
#endif

/*
 * r2 is the TOC pointer: it actually points 0x8000 into the TOC (this gives the
 * value maximum span in an instruction which uses a signed offset). Round down
 * to a 256 byte boundary for the odd case where we are setting up r2 without a
 * .toc section.
 */
static inline unsigned long my_r2(const Elf64_Shdr *sechdrs, struct module *me)
{
	return (sechdrs[me->arch.toc_section].sh_addr & ~0xfful) + 0x8000;
}

/* Patch stub to reference function and correct r2 value. */
static inline int create_stub(const Elf64_Shdr *sechdrs,
			      struct ppc64_stub_entry *entry,
			      unsigned long addr,
			      struct module *me,
			      const char *name)
{
	long reladdr;

	if (is_mprofile_ftrace_call(name))
		return create_ftrace_stub(entry, addr, me);

	memcpy(entry->jump, ppc64_stub_insns, sizeof(ppc64_stub_insns));

	/* Stub uses address relative to r2. */
	reladdr = (unsigned long)entry - my_r2(sechdrs, me);
	if (reladdr > 0x7FFFFFFF || reladdr < -(0x80000000L)) {
		pr_err("%s: Address %p of stub out of range of %p.\n",
		       me->name, (void *)reladdr, (void *)my_r2);
		return 0;
	}
	pr_debug("Stub %p get data from reladdr %li\n", entry, reladdr);

	entry->jump[0] |= PPC_HA(reladdr);
	entry->jump[1] |= PPC_LO(reladdr);
	entry->funcdata = func_desc(addr);
	entry->magic = STUB_MAGIC;

	return 1;
}

/* Create stub to jump to function described in this OPD/ptr: we need the
   stub to set up the TOC ptr (r2) for the function. */
static unsigned long stub_for_addr(const Elf64_Shdr *sechdrs,
				   unsigned long addr,
				   struct module *me,
				   const char *name)
{
	struct ppc64_stub_entry *stubs;
	unsigned int i, num_stubs;

	num_stubs = sechdrs[me->arch.stubs_section].sh_size / sizeof(*stubs);

	/* Find this stub, or if that fails, the next avail. entry */
	stubs = (void *)sechdrs[me->arch.stubs_section].sh_addr;
	for (i = 0; stub_func_addr(stubs[i].funcdata); i++) {
		if (WARN_ON(i >= num_stubs))
			return 0;

		if (stub_func_addr(stubs[i].funcdata) == func_addr(addr))
			return (unsigned long)&stubs[i];
	}

	if (!create_stub(sechdrs, &stubs[i], addr, me, name))
		return 0;

	return (unsigned long)&stubs[i];
}

/* We expect a noop next: if it is, replace it with instruction to
   restore r2. */
static int restore_r2(const char *name, u32 *instruction, struct module *me)
{
	u32 *prev_insn = instruction - 1;

	if (is_mprofile_ftrace_call(name))
		return 1;

	/*
	 * Make sure the branch isn't a sibling call.  Sibling calls aren't
	 * "link" branches and they don't return, so they don't need the r2
	 * restore afterwards.
	 */
	if (!instr_is_relative_link_branch(ppc_inst(*prev_insn)))
		return 1;

	if (*instruction != PPC_INST_NOP) {
		pr_err("%s: Expected nop after call, got %08x at %pS\n",
			me->name, *instruction, instruction);
		return 0;
	}
	/* ld r2,R2_STACK_OFFSET(r1) */
	*instruction = PPC_INST_LD_TOC;
	return 1;
}

int apply_relocate_add(Elf64_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *me)
{
	unsigned int i;
	Elf64_Rela *rela = (void *)sechdrs[relsec].sh_addr;
	Elf64_Sym *sym;
	unsigned long *location;
	unsigned long value;

	pr_debug("Applying ADD relocate section %u to %u\n", relsec,
	       sechdrs[relsec].sh_info);

	/* First time we're called, we can fix up .TOC. */
	if (!me->arch.toc_fixed) {
		sym = find_dot_toc(sechdrs, strtab, symindex);
		/* It's theoretically possible that a module doesn't want a
		 * .TOC. so don't fail it just for that. */
		if (sym)
			sym->st_value = my_r2(sechdrs, me);
		me->arch.toc_fixed = true;
	}

	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rela); i++) {
		/* This is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rela[i].r_offset;
		/* This is the symbol it is referring to */
		sym = (Elf64_Sym *)sechdrs[symindex].sh_addr
			+ ELF64_R_SYM(rela[i].r_info);

		pr_debug("RELOC at %p: %li-type as %s (0x%lx) + %li\n",
		       location, (long)ELF64_R_TYPE(rela[i].r_info),
		       strtab + sym->st_name, (unsigned long)sym->st_value,
		       (long)rela[i].r_addend);

		/* `Everything is relative'. */
		value = sym->st_value + rela[i].r_addend;

		switch (ELF64_R_TYPE(rela[i].r_info)) {
		case R_PPC64_ADDR32:
			/* Simply set it */
			*(u32 *)location = value;
			break;

		case R_PPC64_ADDR64:
			/* Simply set it */
			*(unsigned long *)location = value;
			break;

		case R_PPC64_TOC:
			*(unsigned long *)location = my_r2(sechdrs, me);
			break;

		case R_PPC64_TOC16:
			/* Subtract TOC pointer */
			value -= my_r2(sechdrs, me);
			if (value + 0x8000 > 0xffff) {
				pr_err("%s: bad TOC16 relocation (0x%lx)\n",
				       me->name, value);
				return -ENOEXEC;
			}
			*((uint16_t *) location)
				= (*((uint16_t *) location) & ~0xffff)
				| (value & 0xffff);
			break;

		case R_PPC64_TOC16_LO:
			/* Subtract TOC pointer */
			value -= my_r2(sechdrs, me);
			*((uint16_t *) location)
				= (*((uint16_t *) location) & ~0xffff)
				| (value & 0xffff);
			break;

		case R_PPC64_TOC16_DS:
			/* Subtract TOC pointer */
			value -= my_r2(sechdrs, me);
			if ((value & 3) != 0 || value + 0x8000 > 0xffff) {
				pr_err("%s: bad TOC16_DS relocation (0x%lx)\n",
				       me->name, value);
				return -ENOEXEC;
			}
			*((uint16_t *) location)
				= (*((uint16_t *) location) & ~0xfffc)
				| (value & 0xfffc);
			break;

		case R_PPC64_TOC16_LO_DS:
			/* Subtract TOC pointer */
			value -= my_r2(sechdrs, me);
			if ((value & 3) != 0) {
				pr_err("%s: bad TOC16_LO_DS relocation (0x%lx)\n",
				       me->name, value);
				return -ENOEXEC;
			}
			*((uint16_t *) location)
				= (*((uint16_t *) location) & ~0xfffc)
				| (value & 0xfffc);
			break;

		case R_PPC64_TOC16_HA:
			/* Subtract TOC pointer */
			value -= my_r2(sechdrs, me);
			value = ((value + 0x8000) >> 16);
			*((uint16_t *) location)
				= (*((uint16_t *) location) & ~0xffff)
				| (value & 0xffff);
			break;

		case R_PPC_REL24:
			/* FIXME: Handle weak symbols here --RR */
			if (sym->st_shndx == SHN_UNDEF ||
			    sym->st_shndx == SHN_LIVEPATCH) {
				/* External: go via stub */
				value = stub_for_addr(sechdrs, value, me,
						strtab + sym->st_name);
				if (!value)
					return -ENOENT;
				if (!restore_r2(strtab + sym->st_name,
							(u32 *)location + 1, me))
					return -ENOEXEC;
			} else
				value += local_entry_offset(sym);

			/* Convert value to relative */
			value -= (unsigned long)location;
			if (value + 0x2000000 > 0x3ffffff || (value & 3) != 0){
				pr_err("%s: REL24 %li out of range!\n",
				       me->name, (long int)value);
				return -ENOEXEC;
			}

			/* Only replace bits 2 through 26 */
			*(uint32_t *)location
				= (*(uint32_t *)location & ~0x03fffffc)
				| (value & 0x03fffffc);
			break;

		case R_PPC64_REL64:
			/* 64 bits relative (used by features fixups) */
			*location = value - (unsigned long)location;
			break;

		case R_PPC64_REL32:
			/* 32 bits relative (used by relative exception tables) */
			/* Convert value to relative */
			value -= (unsigned long)location;
			if (value + 0x80000000 > 0xffffffff) {
				pr_err("%s: REL32 %li out of range!\n",
				       me->name, (long int)value);
				return -ENOEXEC;
			}
			*(u32 *)location = value;
			break;

		case R_PPC64_TOCSAVE:
			/*
			 * Marker reloc indicates we don't have to save r2.
			 * That would only save us one instruction, so ignore
			 * it.
			 */
			break;

		case R_PPC64_ENTRY:
			/*
			 * Optimize ELFv2 large code model entry point if
			 * the TOC is within 2GB range of current location.
			 */
			value = my_r2(sechdrs, me) - (unsigned long)location;
			if (value + 0x80008000 > 0xffffffff)
				break;
			/*
			 * Check for the large code model prolog sequence:
		         *	ld r2, ...(r12)
			 *	add r2, r2, r12
			 */
			if ((((uint32_t *)location)[0] & ~0xfffc) !=
			    (PPC_INST_LD | __PPC_RT(R2) | __PPC_RA(R12)))
				break;
			if (((uint32_t *)location)[1] !=
			    (PPC_INST_ADD | __PPC_RT(R2) | __PPC_RA(R2) | __PPC_RB(R12)))
				break;
			/*
			 * If found, replace it with:
			 *	addis r2, r12, (.TOC.-func)@ha
			 *	addi  r2,  r2, (.TOC.-func)@l
			 */
			((uint32_t *)location)[0] = PPC_INST_ADDIS | __PPC_RT(R2) |
						    __PPC_RA(R12) | PPC_HA(value);
			((uint32_t *)location)[1] = PPC_INST_ADDI | __PPC_RT(R2) |
						    __PPC_RA(R2) | PPC_LO(value);
			break;

		case R_PPC64_REL16_HA:
			/* Subtract location pointer */
			value -= (unsigned long)location;
			value = ((value + 0x8000) >> 16);
			*((uint16_t *) location)
				= (*((uint16_t *) location) & ~0xffff)
				| (value & 0xffff);
			break;

		case R_PPC64_REL16_LO:
			/* Subtract location pointer */
			value -= (unsigned long)location;
			*((uint16_t *) location)
				= (*((uint16_t *) location) & ~0xffff)
				| (value & 0xffff);
			break;

		default:
			pr_err("%s: Unknown ADD relocation: %lu\n",
			       me->name,
			       (unsigned long)ELF64_R_TYPE(rela[i].r_info));
			return -ENOEXEC;
		}
	}

	return 0;
}

#ifdef CONFIG_DYNAMIC_FTRACE
int module_trampoline_target(struct module *mod, unsigned long addr,
			     unsigned long *target)
{
	struct ppc64_stub_entry *stub;
	func_desc_t funcdata;
	u32 magic;

	if (!within_module_core(addr, mod)) {
		pr_err("%s: stub %lx not in module %s\n", __func__, addr, mod->name);
		return -EFAULT;
	}

	stub = (struct ppc64_stub_entry *)addr;

	if (copy_from_kernel_nofault(&magic, &stub->magic,
			sizeof(magic))) {
		pr_err("%s: fault reading magic for stub %lx for %s\n", __func__, addr, mod->name);
		return -EFAULT;
	}

	if (magic != STUB_MAGIC) {
		pr_err("%s: bad magic for stub %lx for %s\n", __func__, addr, mod->name);
		return -EFAULT;
	}

	if (copy_from_kernel_nofault(&funcdata, &stub->funcdata,
			sizeof(funcdata))) {
		pr_err("%s: fault reading funcdata for stub %lx for %s\n", __func__, addr, mod->name);
                return -EFAULT;
	}

	*target = stub_func_addr(funcdata);

	return 0;
}

int module_finalize_ftrace(struct module *mod, const Elf_Shdr *sechdrs)
{
	mod->arch.tramp = stub_for_addr(sechdrs,
					(unsigned long)ftrace_caller,
					mod,
					"ftrace_caller");
#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
	mod->arch.tramp_regs = stub_for_addr(sechdrs,
					(unsigned long)ftrace_regs_caller,
					mod,
					"ftrace_regs_caller");
	if (!mod->arch.tramp_regs)
		return -ENOENT;
#endif

	if (!mod->arch.tramp)
		return -ENOENT;

	return 0;
}
#endif
