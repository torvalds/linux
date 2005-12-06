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
#include <linux/module.h>
#include <linux/elf.h>
#include <linux/moduleloader.h>
#include <linux/err.h>
#include <linux/vmalloc.h>
#include <asm/module.h>
#include <asm/uaccess.h>

/* FIXME: We don't do .init separately.  To do this, we'd need to have
   a separate r2 value in the init and core section, and stub between
   them, too.

   Using a magic allocator which places modules within 32MB solves
   this, and makes other things simpler.  Anton?
   --RR.  */
#if 0
#define DEBUGP printk
#else
#define DEBUGP(fmt , ...)
#endif

/* There's actually a third entry here, but it's unused */
struct ppc64_opd_entry
{
	unsigned long funcaddr;
	unsigned long r2;
};

/* Like PPC32, we need little trampolines to do > 24-bit jumps (into
   the kernel itself).  But on PPC64, these need to be used for every
   jump, actually, to reset r2 (TOC+0x8000). */
struct ppc64_stub_entry
{
	/* 28 byte jump instruction sequence (7 instructions) */
	unsigned char jump[28];
	unsigned char unused[4];
	/* Data for the above code */
	struct ppc64_opd_entry opd;
};

/* We use a stub to fix up r2 (TOC ptr) and to jump to the (external)
   function which may be more than 24-bits away.  We could simply
   patch the new r2 value and function pointer into the stub, but it's
   significantly shorter to put these values at the end of the stub
   code, and patch the stub address (32-bits relative to the TOC ptr,
   r2) into the stub. */
static struct ppc64_stub_entry ppc64_stub =
{ .jump = {
	0x3d, 0x82, 0x00, 0x00, /* addis   r12,r2, <high> */
	0x39, 0x8c, 0x00, 0x00, /* addi    r12,r12, <low> */
	/* Save current r2 value in magic place on the stack. */
	0xf8, 0x41, 0x00, 0x28, /* std     r2,40(r1) */
	0xe9, 0x6c, 0x00, 0x20, /* ld      r11,32(r12) */
	0xe8, 0x4c, 0x00, 0x28, /* ld      r2,40(r12) */
	0x7d, 0x69, 0x03, 0xa6, /* mtctr   r11 */
	0x4e, 0x80, 0x04, 0x20  /* bctr */
} };

/* Count how many different 24-bit relocations (different symbol,
   different addend) */
static unsigned int count_relocs(const Elf64_Rela *rela, unsigned int num)
{
	unsigned int i, j, ret = 0;

	/* FIXME: Only count external ones --RR */
	/* Sure, this is order(n^2), but it's usually short, and not
           time critical */
	for (i = 0; i < num; i++) {
		/* Only count 24-bit relocs, others don't need stubs */
		if (ELF64_R_TYPE(rela[i].r_info) != R_PPC_REL24)
			continue;
		for (j = 0; j < i; j++) {
			/* If this addend appeared before, it's
                           already been counted */
			if (rela[i].r_info == rela[j].r_info
			    && rela[i].r_addend == rela[j].r_addend)
				break;
		}
		if (j == i) ret++;
	}
	return ret;
}

void *module_alloc(unsigned long size)
{
	if (size == 0)
		return NULL;

	return vmalloc_exec(size);
}

/* Free memory returned from module_alloc */
void module_free(struct module *mod, void *module_region)
{
	vfree(module_region);
	/* FIXME: If module_region == mod->init_region, trim exception
           table entries. */
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
			DEBUGP("Found relocations in section %u\n", i);
			DEBUGP("Ptr: %p.  Number: %lu\n",
			       (void *)sechdrs[i].sh_addr,
			       sechdrs[i].sh_size / sizeof(Elf64_Rela));
			relocs += count_relocs((void *)sechdrs[i].sh_addr,
					       sechdrs[i].sh_size
					       / sizeof(Elf64_Rela));
		}
	}

	DEBUGP("Looks like a total of %lu stubs, max\n", relocs);
	return relocs * sizeof(struct ppc64_stub_entry);
}

static void dedotify_versions(struct modversion_info *vers,
			      unsigned long size)
{
	struct modversion_info *end;

	for (end = (void *)vers + size; vers < end; vers++)
		if (vers->name[0] == '.')
			memmove(vers->name, vers->name+1, strlen(vers->name));
}

/* Undefined symbols which refer to .funcname, hack to funcname */
static void dedotify(Elf64_Sym *syms, unsigned int numsyms, char *strtab)
{
	unsigned int i;

	for (i = 1; i < numsyms; i++) {
		if (syms[i].st_shndx == SHN_UNDEF) {
			char *name = strtab + syms[i].st_name;
			if (name[0] == '.')
				memmove(name, name+1, strlen(name));
		}
	}
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
	if (!me->arch.stubs_section || !me->arch.toc_section) {
		printk("%s: doesn't contain .toc or .stubs.\n", me->name);
		return -ENOEXEC;
	}

	/* Override the stubs size */
	sechdrs[me->arch.stubs_section].sh_size = get_stubs_size(hdr, sechdrs);
	return 0;
}

int apply_relocate(Elf64_Shdr *sechdrs,
		   const char *strtab,
		   unsigned int symindex,
		   unsigned int relsec,
		   struct module *me)
{
	printk(KERN_ERR "%s: Non-ADD RELOCATION unsupported\n", me->name);
	return -ENOEXEC;
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
			      struct ppc64_opd_entry *opd,
			      struct module *me)
{
	Elf64_Half *loc1, *loc2;
	long reladdr;

	*entry = ppc64_stub;

	loc1 = (Elf64_Half *)&entry->jump[2];
	loc2 = (Elf64_Half *)&entry->jump[6];

	/* Stub uses address relative to r2. */
	reladdr = (unsigned long)entry - my_r2(sechdrs, me);
	if (reladdr > 0x7FFFFFFF || reladdr < -(0x80000000L)) {
		printk("%s: Address %p of stub out of range of %p.\n",
		       me->name, (void *)reladdr, (void *)my_r2);
		return 0;
	}
	DEBUGP("Stub %p get data from reladdr %li\n", entry, reladdr);

	*loc1 = PPC_HA(reladdr);
	*loc2 = PPC_LO(reladdr);
	entry->opd.funcaddr = opd->funcaddr;
	entry->opd.r2 = opd->r2;
	return 1;
}

/* Create stub to jump to function described in this OPD: we need the
   stub to set up the TOC ptr (r2) for the function. */
static unsigned long stub_for_addr(Elf64_Shdr *sechdrs,
				   unsigned long opdaddr,
				   struct module *me)
{
	struct ppc64_stub_entry *stubs;
	struct ppc64_opd_entry *opd = (void *)opdaddr;
	unsigned int i, num_stubs;

	num_stubs = sechdrs[me->arch.stubs_section].sh_size / sizeof(*stubs);

	/* Find this stub, or if that fails, the next avail. entry */
	stubs = (void *)sechdrs[me->arch.stubs_section].sh_addr;
	for (i = 0; stubs[i].opd.funcaddr; i++) {
		BUG_ON(i >= num_stubs);

		if (stubs[i].opd.funcaddr == opd->funcaddr)
			return (unsigned long)&stubs[i];
	}

	if (!create_stub(sechdrs, &stubs[i], opd, me))
		return 0;

	return (unsigned long)&stubs[i];
}

/* We expect a noop next: if it is, replace it with instruction to
   restore r2. */
static int restore_r2(u32 *instruction, struct module *me)
{
	if (*instruction != 0x60000000) {
		printk("%s: Expect noop after relocate, got %08x\n",
		       me->name, *instruction);
		return 0;
	}
	*instruction = 0xe8410028;	/* ld r2,40(r1) */
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

	DEBUGP("Applying ADD relocate section %u to %u\n", relsec,
	       sechdrs[relsec].sh_info);
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rela); i++) {
		/* This is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rela[i].r_offset;
		/* This is the symbol it is referring to */
		sym = (Elf64_Sym *)sechdrs[symindex].sh_addr
			+ ELF64_R_SYM(rela[i].r_info);

		DEBUGP("RELOC at %p: %li-type as %s (%lu) + %li\n",
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
			/* Subtact TOC pointer */
			value -= my_r2(sechdrs, me);
			if (value + 0x8000 > 0xffff) {
				printk("%s: bad TOC16 relocation (%lu)\n",
				       me->name, value);
				return -ENOEXEC;
			}
			*((uint16_t *) location)
				= (*((uint16_t *) location) & ~0xffff)
				| (value & 0xffff);
			break;

		case R_PPC64_TOC16_DS:
			/* Subtact TOC pointer */
			value -= my_r2(sechdrs, me);
			if ((value & 3) != 0 || value + 0x8000 > 0xffff) {
				printk("%s: bad TOC16_DS relocation (%lu)\n",
				       me->name, value);
				return -ENOEXEC;
			}
			*((uint16_t *) location)
				= (*((uint16_t *) location) & ~0xfffc)
				| (value & 0xfffc);
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
			}

			/* Convert value to relative */
			value -= (unsigned long)location;
			if (value + 0x2000000 > 0x3ffffff || (value & 3) != 0){
				printk("%s: REL24 %li out of range!\n",
				       me->name, (long int)value);
				return -ENOEXEC;
			}

			/* Only replace bits 2 through 26 */
			*(uint32_t *)location 
				= (*(uint32_t *)location & ~0x03fffffc)
				| (value & 0x03fffffc);
			break;

		default:
			printk("%s: Unknown ADD relocation: %lu\n",
			       me->name,
			       (unsigned long)ELF64_R_TYPE(rela[i].r_info));
			return -ENOEXEC;
		}
	}

	return 0;
}

LIST_HEAD(module_bug_list);

int module_finalize(const Elf_Ehdr *hdr,
		const Elf_Shdr *sechdrs, struct module *me)
{
	char *secstrings;
	unsigned int i;

	me->arch.bug_table = NULL;
	me->arch.num_bugs = 0;

	/* Find the __bug_table section, if present */
	secstrings = (char *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;
	for (i = 1; i < hdr->e_shnum; i++) {
		if (strcmp(secstrings+sechdrs[i].sh_name, "__bug_table"))
			continue;
		me->arch.bug_table = (void *) sechdrs[i].sh_addr;
		me->arch.num_bugs = sechdrs[i].sh_size / sizeof(struct bug_entry);
		break;
	}

	/*
	 * Strictly speaking this should have a spinlock to protect against
	 * traversals, but since we only traverse on BUG()s, a spinlock
	 * could potentially lead to deadlock and thus be counter-productive.
	 */
	list_add(&me->arch.bug_list, &module_bug_list);

	return 0;
}

void module_arch_cleanup(struct module *mod)
{
	list_del(&mod->arch.bug_list);
}

struct bug_entry *module_find_bug(unsigned long bugaddr)
{
	struct mod_arch_specific *mod;
	unsigned int i;
	struct bug_entry *bug;

	list_for_each_entry(mod, &module_bug_list, bug_list) {
		bug = mod->bug_table;
		for (i = 0; i < mod->num_bugs; ++i, ++bug)
			if (bugaddr == bug->bug_addr)
				return bug;
	}
	return NULL;
}
