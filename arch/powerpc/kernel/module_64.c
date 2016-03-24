/*  Kernel module help for PPC64.
    Copyright (C) 2001, 2003 Rusty Russell IBM Corporation.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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

/* FIXME: We don't do .init separately.  To do this, we'd need to have
   a separate r2 value in the init and core section, and stub between
   them, too.

   Using a magic allocator which places modules within 32MB solves
   this, and makes other things simpler.  Anton?
   --RR.  */

#if defined(_CALL_ELF) && _CALL_ELF == 2
#define R2_STACK_OFFSET 24

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
#define R2_STACK_OFFSET 40

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
#endif

/* Like PPC32, we need little trampolines to do > 24-bit jumps (into
   the kernel itself).  But on PPC64, these need to be used for every
   jump, actually, to reset r2 (TOC+0x8000). */
struct ppc64_stub_entry
{
	/* 28 byte jump instruction sequence (7 instructions). We only
	 * need 6 instructions on ABIv2 but we always allocate 7 so
	 * so we don't have to modify the trampoline load instruction. */
	u32 jump[7];
	u32 unused;
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
 */

static u32 ppc64_stub_insns[] = {
	0x3d620000,			/* addis   r11,r2, <high> */
	0x396b0000,			/* addi    r11,r11, <low> */
	/* Save current r2 value in magic place on the stack. */
	0xf8410000|R2_STACK_OFFSET,	/* std     r2,R2_STACK_OFFSET(r1) */
	0xe98b0020,			/* ld      r12,32(r11) */
#if !defined(_CALL_ELF) || _CALL_ELF != 2
	/* Set up new r2 from function descriptor */
	0xe84b0028,			/* ld      r2,40(r11) */
#endif
	0x7d8903a6,			/* mtctr   r12 */
	0x4e800420			/* bctr */
};

#ifdef CONFIG_DYNAMIC_FTRACE

static u32 ppc64_stub_mask[] = {
	0xffff0000,
	0xffff0000,
	0xffffffff,
	0xffffffff,
#if !defined(_CALL_ELF) || _CALL_ELF != 2
	0xffffffff,
#endif
	0xffffffff,
	0xffffffff
};

bool is_module_trampoline(u32 *p)
{
	unsigned int i;
	u32 insns[ARRAY_SIZE(ppc64_stub_insns)];

	BUILD_BUG_ON(sizeof(ppc64_stub_insns) != sizeof(ppc64_stub_mask));

	if (probe_kernel_read(insns, p, sizeof(insns)))
		return -EFAULT;

	for (i = 0; i < ARRAY_SIZE(ppc64_stub_insns); i++) {
		u32 insna = insns[i];
		u32 insnb = ppc64_stub_insns[i];
		u32 mask = ppc64_stub_mask[i];

		if ((insna & mask) != (insnb & mask))
			return false;
	}

	return true;
}

int module_trampoline_target(struct module *mod, u32 *trampoline,
			     unsigned long *target)
{
	u32 buf[2];
	u16 upper, lower;
	long offset;
	void *toc_entry;

	if (probe_kernel_read(buf, trampoline, sizeof(buf)))
		return -EFAULT;

	upper = buf[0] & 0xffff;
	lower = buf[1] & 0xffff;

	/* perform the addis/addi, both signed */
	offset = ((short)upper << 16) + (short)lower;

	/*
	 * Now get the address this trampoline jumps to. This
	 * is always 32 bytes into our trampoline stub.
	 */
	toc_entry = (void *)mod->arch.toc + offset + 32;

	if (probe_kernel_read(target, toc_entry, sizeof(*target)))
		return -EFAULT;

	return 0;
}

#endif

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

static void relaswap(void *_x, void *_y, int size)
{
	uint64_t *x, *y, tmp;
	int i;

	y = (uint64_t *)_x;
	x = (uint64_t *)_y;

	for (i = 0; i < sizeof(Elf64_Rela) / sizeof(uint64_t); i++) {
		tmp = x[i];
		x[i] = y[i];
		y[i] = tmp;
	}
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
			     sizeof(Elf64_Rela), relacmp, relaswap);

			relocs += count_relocs((void *)sechdrs[i].sh_addr,
					       sechdrs[i].sh_size
					       / sizeof(Elf64_Rela));
		}
	}

#ifdef CONFIG_DYNAMIC_FTRACE
	/* make the trampoline to the ftrace_caller */
	relocs++;
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
#ifdef ARCH_RELOCATES_KCRCTAB
			/* The TOC symbol has no CRC computed. To avoid CRC
			 * check failing, we must force it to the expected
			 * value (see CRC check in module.c).
			 */
			if (!strcmp(vers->name, "TOC."))
				vers->crc = -(unsigned long)reloc_start;
#endif
		}
}

/* Undefined symbols which refer to .funcname, hack to funcname (or .TOC.) */
static void dedotify(Elf64_Sym *syms, unsigned int numsyms, char *strtab)
{
	unsigned int i;

	for (i = 1; i < numsyms; i++) {
		if (syms[i].st_shndx == SHN_UNDEF) {
			char *name = strtab + syms[i].st_name;
			if (name[0] == '.')
				syms[i].st_name++;
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
		if (syms[i].st_shndx == SHN_UNDEF
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
		else if (strcmp(secstrings + sechdrs[i].sh_name, ".toc") == 0)
			me->arch.toc_section = i;
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

/* r2 is the TOC pointer: it actually points 0x8000 into the TOC (this
   gives the value maximum span in an instruction which uses a signed
   offset) */
static inline unsigned long my_r2(Elf64_Shdr *sechdrs, struct module *me)
{
	return sechdrs[me->arch.toc_section].sh_addr + 0x8000;
}

/* Both low and high 16 bits are added as SIGNED additions, so if low
   16 bits has high bit set, high 16 bits must be adjusted.  These
   macros do that (stolen from binutils). */
#define PPC_LO(v) ((v) & 0xffff)
#define PPC_HI(v) (((v) >> 16) & 0xffff)
#define PPC_HA(v) PPC_HI ((v) + 0x8000)

/* Patch stub to reference function and correct r2 value. */
static inline int create_stub(Elf64_Shdr *sechdrs,
			      struct ppc64_stub_entry *entry,
			      unsigned long addr,
			      struct module *me)
{
	long reladdr;

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
	return 1;
}

/* Create stub to jump to function described in this OPD/ptr: we need the
   stub to set up the TOC ptr (r2) for the function. */
static unsigned long stub_for_addr(Elf64_Shdr *sechdrs,
				   unsigned long addr,
				   struct module *me)
{
	struct ppc64_stub_entry *stubs;
	unsigned int i, num_stubs;

	num_stubs = sechdrs[me->arch.stubs_section].sh_size / sizeof(*stubs);

	/* Find this stub, or if that fails, the next avail. entry */
	stubs = (void *)sechdrs[me->arch.stubs_section].sh_addr;
	for (i = 0; stub_func_addr(stubs[i].funcdata); i++) {
		BUG_ON(i >= num_stubs);

		if (stub_func_addr(stubs[i].funcdata) == func_addr(addr))
			return (unsigned long)&stubs[i];
	}

	if (!create_stub(sechdrs, &stubs[i], addr, me))
		return 0;

	return (unsigned long)&stubs[i];
}

/* We expect a noop next: if it is, replace it with instruction to
   restore r2. */
static int restore_r2(u32 *instruction, struct module *me)
{
	if (*instruction != PPC_INST_NOP) {
		pr_err("%s: Expect noop after relocate, got %08x\n",
		       me->name, *instruction);
		return 0;
	}
	/* ld r2,R2_STACK_OFFSET(r1) */
	*instruction = 0xe8410000 | R2_STACK_OFFSET;
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
			if (sym->st_shndx == SHN_UNDEF) {
				/* External: go via stub */
				value = stub_for_addr(sechdrs, value, me);
				if (!value)
					return -ENOENT;
				if (!restore_r2((u32 *)location + 1, me))
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
			if ((((uint32_t *)location)[0] & ~0xfffc)
			    != 0xe84c0000)
				break;
			if (((uint32_t *)location)[1] != 0x7c426214)
				break;
			/*
			 * If found, replace it with:
			 *	addis r2, r12, (.TOC.-func)@ha
			 *	addi r2, r12, (.TOC.-func)@l
			 */
			((uint32_t *)location)[0] = 0x3c4c0000 + PPC_HA(value);
			((uint32_t *)location)[1] = 0x38420000 + PPC_LO(value);
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

#ifdef CONFIG_DYNAMIC_FTRACE
	me->arch.toc = my_r2(sechdrs, me);
	me->arch.tramp = stub_for_addr(sechdrs,
				       (unsigned long)ftrace_caller,
				       me);
#endif

	return 0;
}
