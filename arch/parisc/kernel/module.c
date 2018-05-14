/*    Kernel dynamically loadable module help for PARISC.
 *
 *    The best reference for this stuff is probably the Processor-
 *    Specific ELF Supplement for PA-RISC:
 *        http://ftp.parisc-linux.org/docs/arch/elf-pa-hp.pdf
 *
 *    Linux/PA-RISC Project (http://www.parisc-linux.org/)
 *    Copyright (C) 2003 Randolph Chung <tausq at debian . org>
 *    Copyright (C) 2008 Helge Deller <deller@gmx.de>
 *
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 *    Notes:
 *    - PLT stub handling
 *      On 32bit (and sometimes 64bit) and with big kernel modules like xfs or
 *      ipv6 the relocation types R_PARISC_PCREL17F and R_PARISC_PCREL22F may
 *      fail to reach their PLT stub if we only create one big stub array for
 *      all sections at the beginning of the core or init section.
 *      Instead we now insert individual PLT stub entries directly in front of
 *      of the code sections where the stubs are actually called.
 *      This reduces the distance between the PCREL location and the stub entry
 *      so that the relocations can be fulfilled.
 *      While calculating the final layout of the kernel module in memory, the
 *      kernel module loader calls arch_mod_section_prepend() to request the
 *      to be reserved amount of memory in front of each individual section.
 *
 *    - SEGREL32 handling
 *      We are not doing SEGREL32 handling correctly. According to the ABI, we
 *      should do a value offset, like this:
 *			if (in_init(me, (void *)val))
 *				val -= (uint32_t)me->init_layout.base;
 *			else
 *				val -= (uint32_t)me->core_layout.base;
 *	However, SEGREL32 is used only for PARISC unwind entries, and we want
 *	those entries to have an absolute address, and not just an offset.
 *
 *	The unwind table mechanism has the ability to specify an offset for 
 *	the unwind table; however, because we split off the init functions into
 *	a different piece of memory, it is not possible to do this using a 
 *	single offset. Instead, we use the above hack for now.
 */

#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/bug.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include <asm/pgtable.h>
#include <asm/unwind.h>
#include <asm/sections.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(fmt...)
#endif

#define RELOC_REACHABLE(val, bits) \
	(( ( !((val) & (1<<((bits)-1))) && ((val)>>(bits)) != 0 )  ||	\
	     ( ((val) & (1<<((bits)-1))) && ((val)>>(bits)) != (((__typeof__(val))(~0))>>((bits)+2)))) ? \
	0 : 1)

#define CHECK_RELOC(val, bits) \
	if (!RELOC_REACHABLE(val, bits)) { \
		printk(KERN_ERR "module %s relocation of symbol %s is out of range (0x%lx in %d bits)\n", \
		me->name, strtab + sym->st_name, (unsigned long)val, bits); \
		return -ENOEXEC;			\
	}

/* Maximum number of GOT entries. We use a long displacement ldd from
 * the bottom of the table, which has a maximum signed displacement of
 * 0x3fff; however, since we're only going forward, this becomes
 * 0x1fff, and thus, since each GOT entry is 8 bytes long we can have
 * at most 1023 entries.
 * To overcome this 14bit displacement with some kernel modules, we'll
 * use instead the unusal 16bit displacement method (see reassemble_16a)
 * which gives us a maximum positive displacement of 0x7fff, and as such
 * allows us to allocate up to 4095 GOT entries. */
#define MAX_GOTS	4095

/* three functions to determine where in the module core
 * or init pieces the location is */
static inline int in_init(struct module *me, void *loc)
{
	return (loc >= me->init_layout.base &&
		loc <= (me->init_layout.base + me->init_layout.size));
}

static inline int in_core(struct module *me, void *loc)
{
	return (loc >= me->core_layout.base &&
		loc <= (me->core_layout.base + me->core_layout.size));
}

static inline int in_local(struct module *me, void *loc)
{
	return in_init(me, loc) || in_core(me, loc);
}

#ifndef CONFIG_64BIT
struct got_entry {
	Elf32_Addr addr;
};

struct stub_entry {
	Elf32_Word insns[2]; /* each stub entry has two insns */
};
#else
struct got_entry {
	Elf64_Addr addr;
};

struct stub_entry {
	Elf64_Word insns[4]; /* each stub entry has four insns */
};
#endif

/* Field selection types defined by hppa */
#define rnd(x)			(((x)+0x1000)&~0x1fff)
/* fsel: full 32 bits */
#define fsel(v,a)		((v)+(a))
/* lsel: select left 21 bits */
#define lsel(v,a)		(((v)+(a))>>11)
/* rsel: select right 11 bits */
#define rsel(v,a)		(((v)+(a))&0x7ff)
/* lrsel with rounding of addend to nearest 8k */
#define lrsel(v,a)		(((v)+rnd(a))>>11)
/* rrsel with rounding of addend to nearest 8k */
#define rrsel(v,a)		((((v)+rnd(a))&0x7ff)+((a)-rnd(a)))

#define mask(x,sz)		((x) & ~((1<<(sz))-1))


/* The reassemble_* functions prepare an immediate value for
   insertion into an opcode. pa-risc uses all sorts of weird bitfields
   in the instruction to hold the value.  */
static inline int sign_unext(int x, int len)
{
	int len_ones;

	len_ones = (1 << len) - 1;
	return x & len_ones;
}

static inline int low_sign_unext(int x, int len)
{
	int sign, temp;

	sign = (x >> (len-1)) & 1;
	temp = sign_unext(x, len-1);
	return (temp << 1) | sign;
}

static inline int reassemble_14(int as14)
{
	return (((as14 & 0x1fff) << 1) |
		((as14 & 0x2000) >> 13));
}

static inline int reassemble_16a(int as16)
{
	int s, t;

	/* Unusual 16-bit encoding, for wide mode only.  */
	t = (as16 << 1) & 0xffff;
	s = (as16 & 0x8000);
	return (t ^ s ^ (s >> 1)) | (s >> 15);
}


static inline int reassemble_17(int as17)
{
	return (((as17 & 0x10000) >> 16) |
		((as17 & 0x0f800) << 5) |
		((as17 & 0x00400) >> 8) |
		((as17 & 0x003ff) << 3));
}

static inline int reassemble_21(int as21)
{
	return (((as21 & 0x100000) >> 20) |
		((as21 & 0x0ffe00) >> 8) |
		((as21 & 0x000180) << 7) |
		((as21 & 0x00007c) << 14) |
		((as21 & 0x000003) << 12));
}

static inline int reassemble_22(int as22)
{
	return (((as22 & 0x200000) >> 21) |
		((as22 & 0x1f0000) << 5) |
		((as22 & 0x00f800) << 5) |
		((as22 & 0x000400) >> 8) |
		((as22 & 0x0003ff) << 3));
}

void *module_alloc(unsigned long size)
{
	/* using RWX means less protection for modules, but it's
	 * easier than trying to map the text, data, init_text and
	 * init_data correctly */
	return __vmalloc_node_range(size, 1, VMALLOC_START, VMALLOC_END,
				    GFP_KERNEL,
				    PAGE_KERNEL_RWX, 0, NUMA_NO_NODE,
				    __builtin_return_address(0));
}

#ifndef CONFIG_64BIT
static inline unsigned long count_gots(const Elf_Rela *rela, unsigned long n)
{
	return 0;
}

static inline unsigned long count_fdescs(const Elf_Rela *rela, unsigned long n)
{
	return 0;
}

static inline unsigned long count_stubs(const Elf_Rela *rela, unsigned long n)
{
	unsigned long cnt = 0;

	for (; n > 0; n--, rela++)
	{
		switch (ELF32_R_TYPE(rela->r_info)) {
			case R_PARISC_PCREL17F:
			case R_PARISC_PCREL22F:
				cnt++;
		}
	}

	return cnt;
}
#else
static inline unsigned long count_gots(const Elf_Rela *rela, unsigned long n)
{
	unsigned long cnt = 0;

	for (; n > 0; n--, rela++)
	{
		switch (ELF64_R_TYPE(rela->r_info)) {
			case R_PARISC_LTOFF21L:
			case R_PARISC_LTOFF14R:
			case R_PARISC_PCREL22F:
				cnt++;
		}
	}

	return cnt;
}

static inline unsigned long count_fdescs(const Elf_Rela *rela, unsigned long n)
{
	unsigned long cnt = 0;

	for (; n > 0; n--, rela++)
	{
		switch (ELF64_R_TYPE(rela->r_info)) {
			case R_PARISC_FPTR64:
				cnt++;
		}
	}

	return cnt;
}

static inline unsigned long count_stubs(const Elf_Rela *rela, unsigned long n)
{
	unsigned long cnt = 0;

	for (; n > 0; n--, rela++)
	{
		switch (ELF64_R_TYPE(rela->r_info)) {
			case R_PARISC_PCREL22F:
				cnt++;
		}
	}

	return cnt;
}
#endif

void module_arch_freeing_init(struct module *mod)
{
	kfree(mod->arch.section);
	mod->arch.section = NULL;
}

/* Additional bytes needed in front of individual sections */
unsigned int arch_mod_section_prepend(struct module *mod,
				      unsigned int section)
{
	/* size needed for all stubs of this section (including
	 * one additional for correct alignment of the stubs) */
	return (mod->arch.section[section].stub_entries + 1)
		* sizeof(struct stub_entry);
}

#define CONST 
int module_frob_arch_sections(CONST Elf_Ehdr *hdr,
			      CONST Elf_Shdr *sechdrs,
			      CONST char *secstrings,
			      struct module *me)
{
	unsigned long gots = 0, fdescs = 0, len;
	unsigned int i;

	len = hdr->e_shnum * sizeof(me->arch.section[0]);
	me->arch.section = kzalloc(len, GFP_KERNEL);
	if (!me->arch.section)
		return -ENOMEM;

	for (i = 1; i < hdr->e_shnum; i++) {
		const Elf_Rela *rels = (void *)sechdrs[i].sh_addr;
		unsigned long nrels = sechdrs[i].sh_size / sizeof(*rels);
		unsigned int count, s;

		if (strncmp(secstrings + sechdrs[i].sh_name,
			    ".PARISC.unwind", 14) == 0)
			me->arch.unwind_section = i;

		if (sechdrs[i].sh_type != SHT_RELA)
			continue;

		/* some of these are not relevant for 32-bit/64-bit
		 * we leave them here to make the code common. the
		 * compiler will do its thing and optimize out the
		 * stuff we don't need
		 */
		gots += count_gots(rels, nrels);
		fdescs += count_fdescs(rels, nrels);

		/* XXX: By sorting the relocs and finding duplicate entries
		 *  we could reduce the number of necessary stubs and save
		 *  some memory. */
		count = count_stubs(rels, nrels);
		if (!count)
			continue;

		/* so we need relocation stubs. reserve necessary memory. */
		/* sh_info gives the section for which we need to add stubs. */
		s = sechdrs[i].sh_info;

		/* each code section should only have one relocation section */
		WARN_ON(me->arch.section[s].stub_entries);

		/* store number of stubs we need for this section */
		me->arch.section[s].stub_entries += count;
	}

	/* align things a bit */
	me->core_layout.size = ALIGN(me->core_layout.size, 16);
	me->arch.got_offset = me->core_layout.size;
	me->core_layout.size += gots * sizeof(struct got_entry);

	me->core_layout.size = ALIGN(me->core_layout.size, 16);
	me->arch.fdesc_offset = me->core_layout.size;
	me->core_layout.size += fdescs * sizeof(Elf_Fdesc);

	me->arch.got_max = gots;
	me->arch.fdesc_max = fdescs;

	return 0;
}

#ifdef CONFIG_64BIT
static Elf64_Word get_got(struct module *me, unsigned long value, long addend)
{
	unsigned int i;
	struct got_entry *got;

	value += addend;

	BUG_ON(value == 0);

	got = me->core_layout.base + me->arch.got_offset;
	for (i = 0; got[i].addr; i++)
		if (got[i].addr == value)
			goto out;

	BUG_ON(++me->arch.got_count > me->arch.got_max);

	got[i].addr = value;
 out:
	DEBUGP("GOT ENTRY %d[%x] val %lx\n", i, i*sizeof(struct got_entry),
	       value);
	return i * sizeof(struct got_entry);
}
#endif /* CONFIG_64BIT */

#ifdef CONFIG_64BIT
static Elf_Addr get_fdesc(struct module *me, unsigned long value)
{
	Elf_Fdesc *fdesc = me->core_layout.base + me->arch.fdesc_offset;

	if (!value) {
		printk(KERN_ERR "%s: zero OPD requested!\n", me->name);
		return 0;
	}

	/* Look for existing fdesc entry. */
	while (fdesc->addr) {
		if (fdesc->addr == value)
			return (Elf_Addr)fdesc;
		fdesc++;
	}

	BUG_ON(++me->arch.fdesc_count > me->arch.fdesc_max);

	/* Create new one */
	fdesc->addr = value;
	fdesc->gp = (Elf_Addr)me->core_layout.base + me->arch.got_offset;
	return (Elf_Addr)fdesc;
}
#endif /* CONFIG_64BIT */

enum elf_stub_type {
	ELF_STUB_GOT,
	ELF_STUB_MILLI,
	ELF_STUB_DIRECT,
};

static Elf_Addr get_stub(struct module *me, unsigned long value, long addend,
	enum elf_stub_type stub_type, Elf_Addr loc0, unsigned int targetsec)
{
	struct stub_entry *stub;
	int __maybe_unused d;

	/* initialize stub_offset to point in front of the section */
	if (!me->arch.section[targetsec].stub_offset) {
		loc0 -= (me->arch.section[targetsec].stub_entries + 1) *
				sizeof(struct stub_entry);
		/* get correct alignment for the stubs */
		loc0 = ALIGN(loc0, sizeof(struct stub_entry));
		me->arch.section[targetsec].stub_offset = loc0;
	}

	/* get address of stub entry */
	stub = (void *) me->arch.section[targetsec].stub_offset;
	me->arch.section[targetsec].stub_offset += sizeof(struct stub_entry);

	/* do not write outside available stub area */
	BUG_ON(0 == me->arch.section[targetsec].stub_entries--);


#ifndef CONFIG_64BIT
/* for 32-bit the stub looks like this:
 * 	ldil L'XXX,%r1
 * 	be,n R'XXX(%sr4,%r1)
 */
	//value = *(unsigned long *)((value + addend) & ~3); /* why? */

	stub->insns[0] = 0x20200000;	/* ldil L'XXX,%r1	*/
	stub->insns[1] = 0xe0202002;	/* be,n R'XXX(%sr4,%r1)	*/

	stub->insns[0] |= reassemble_21(lrsel(value, addend));
	stub->insns[1] |= reassemble_17(rrsel(value, addend) / 4);

#else
/* for 64-bit we have three kinds of stubs:
 * for normal function calls:
 * 	ldd 0(%dp),%dp
 * 	ldd 10(%dp), %r1
 * 	bve (%r1)
 * 	ldd 18(%dp), %dp
 *
 * for millicode:
 * 	ldil 0, %r1
 * 	ldo 0(%r1), %r1
 * 	ldd 10(%r1), %r1
 * 	bve,n (%r1)
 *
 * for direct branches (jumps between different section of the
 * same module):
 *	ldil 0, %r1
 *	ldo 0(%r1), %r1
 *	bve,n (%r1)
 */
	switch (stub_type) {
	case ELF_STUB_GOT:
		d = get_got(me, value, addend);
		if (d <= 15) {
			/* Format 5 */
			stub->insns[0] = 0x0f6010db; /* ldd 0(%dp),%dp	*/
			stub->insns[0] |= low_sign_unext(d, 5) << 16;
		} else {
			/* Format 3 */
			stub->insns[0] = 0x537b0000; /* ldd 0(%dp),%dp	*/
			stub->insns[0] |= reassemble_16a(d);
		}
		stub->insns[1] = 0x53610020;	/* ldd 10(%dp),%r1	*/
		stub->insns[2] = 0xe820d000;	/* bve (%r1)		*/
		stub->insns[3] = 0x537b0030;	/* ldd 18(%dp),%dp	*/
		break;
	case ELF_STUB_MILLI:
		stub->insns[0] = 0x20200000;	/* ldil 0,%r1		*/
		stub->insns[1] = 0x34210000;	/* ldo 0(%r1), %r1	*/
		stub->insns[2] = 0x50210020;	/* ldd 10(%r1),%r1	*/
		stub->insns[3] = 0xe820d002;	/* bve,n (%r1)		*/

		stub->insns[0] |= reassemble_21(lrsel(value, addend));
		stub->insns[1] |= reassemble_14(rrsel(value, addend));
		break;
	case ELF_STUB_DIRECT:
		stub->insns[0] = 0x20200000;    /* ldil 0,%r1           */
		stub->insns[1] = 0x34210000;    /* ldo 0(%r1), %r1      */
		stub->insns[2] = 0xe820d002;    /* bve,n (%r1)          */

		stub->insns[0] |= reassemble_21(lrsel(value, addend));
		stub->insns[1] |= reassemble_14(rrsel(value, addend));
		break;
	}

#endif

	return (Elf_Addr)stub;
}

#ifndef CONFIG_64BIT
int apply_relocate_add(Elf_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *me)
{
	int i;
	Elf32_Rela *rel = (void *)sechdrs[relsec].sh_addr;
	Elf32_Sym *sym;
	Elf32_Word *loc;
	Elf32_Addr val;
	Elf32_Sword addend;
	Elf32_Addr dot;
	Elf_Addr loc0;
	unsigned int targetsec = sechdrs[relsec].sh_info;
	//unsigned long dp = (unsigned long)$global$;
	register unsigned long dp asm ("r27");

	DEBUGP("Applying relocate section %u to %u\n", relsec,
	       targetsec);
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		loc = (void *)sechdrs[targetsec].sh_addr
		      + rel[i].r_offset;
		/* This is the start of the target section */
		loc0 = sechdrs[targetsec].sh_addr;
		/* This is the symbol it is referring to */
		sym = (Elf32_Sym *)sechdrs[symindex].sh_addr
			+ ELF32_R_SYM(rel[i].r_info);
		if (!sym->st_value) {
			printk(KERN_WARNING "%s: Unknown symbol %s\n",
			       me->name, strtab + sym->st_name);
			return -ENOENT;
		}
		//dot = (sechdrs[relsec].sh_addr + rel->r_offset) & ~0x03;
		dot =  (Elf32_Addr)loc & ~0x03;

		val = sym->st_value;
		addend = rel[i].r_addend;

#if 0
#define r(t) ELF32_R_TYPE(rel[i].r_info)==t ? #t :
		DEBUGP("Symbol %s loc 0x%x val 0x%x addend 0x%x: %s\n",
			strtab + sym->st_name,
			(uint32_t)loc, val, addend,
			r(R_PARISC_PLABEL32)
			r(R_PARISC_DIR32)
			r(R_PARISC_DIR21L)
			r(R_PARISC_DIR14R)
			r(R_PARISC_SEGREL32)
			r(R_PARISC_DPREL21L)
			r(R_PARISC_DPREL14R)
			r(R_PARISC_PCREL17F)
			r(R_PARISC_PCREL22F)
			"UNKNOWN");
#undef r
#endif

		switch (ELF32_R_TYPE(rel[i].r_info)) {
		case R_PARISC_PLABEL32:
			/* 32-bit function address */
			/* no function descriptors... */
			*loc = fsel(val, addend);
			break;
		case R_PARISC_DIR32:
			/* direct 32-bit ref */
			*loc = fsel(val, addend);
			break;
		case R_PARISC_DIR21L:
			/* left 21 bits of effective address */
			val = lrsel(val, addend);
			*loc = mask(*loc, 21) | reassemble_21(val);
			break;
		case R_PARISC_DIR14R:
			/* right 14 bits of effective address */
			val = rrsel(val, addend);
			*loc = mask(*loc, 14) | reassemble_14(val);
			break;
		case R_PARISC_SEGREL32:
			/* 32-bit segment relative address */
			/* See note about special handling of SEGREL32 at
			 * the beginning of this file.
			 */
			*loc = fsel(val, addend); 
			break;
		case R_PARISC_SECREL32:
			/* 32-bit section relative address. */
			*loc = fsel(val, addend);
			break;
		case R_PARISC_DPREL21L:
			/* left 21 bit of relative address */
			val = lrsel(val - dp, addend);
			*loc = mask(*loc, 21) | reassemble_21(val);
			break;
		case R_PARISC_DPREL14R:
			/* right 14 bit of relative address */
			val = rrsel(val - dp, addend);
			*loc = mask(*loc, 14) | reassemble_14(val);
			break;
		case R_PARISC_PCREL17F:
			/* 17-bit PC relative address */
			/* calculate direct call offset */
			val += addend;
			val = (val - dot - 8)/4;
			if (!RELOC_REACHABLE(val, 17)) {
				/* direct distance too far, create
				 * stub entry instead */
				val = get_stub(me, sym->st_value, addend,
					ELF_STUB_DIRECT, loc0, targetsec);
				val = (val - dot - 8)/4;
				CHECK_RELOC(val, 17);
			}
			*loc = (*loc & ~0x1f1ffd) | reassemble_17(val);
			break;
		case R_PARISC_PCREL22F:
			/* 22-bit PC relative address; only defined for pa20 */
			/* calculate direct call offset */
			val += addend;
			val = (val - dot - 8)/4;
			if (!RELOC_REACHABLE(val, 22)) {
				/* direct distance too far, create
				 * stub entry instead */
				val = get_stub(me, sym->st_value, addend,
					ELF_STUB_DIRECT, loc0, targetsec);
				val = (val - dot - 8)/4;
				CHECK_RELOC(val, 22);
			}
			*loc = (*loc & ~0x3ff1ffd) | reassemble_22(val);
			break;
		case R_PARISC_PCREL32:
			/* 32-bit PC relative address */
			*loc = val - dot - 8 + addend;
			break;

		default:
			printk(KERN_ERR "module %s: Unknown relocation: %u\n",
			       me->name, ELF32_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}
	}

	return 0;
}

#else
int apply_relocate_add(Elf_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *me)
{
	int i;
	Elf64_Rela *rel = (void *)sechdrs[relsec].sh_addr;
	Elf64_Sym *sym;
	Elf64_Word *loc;
	Elf64_Xword *loc64;
	Elf64_Addr val;
	Elf64_Sxword addend;
	Elf64_Addr dot;
	Elf_Addr loc0;
	unsigned int targetsec = sechdrs[relsec].sh_info;

	DEBUGP("Applying relocate section %u to %u\n", relsec,
	       targetsec);
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		loc = (void *)sechdrs[targetsec].sh_addr
		      + rel[i].r_offset;
		/* This is the start of the target section */
		loc0 = sechdrs[targetsec].sh_addr;
		/* This is the symbol it is referring to */
		sym = (Elf64_Sym *)sechdrs[symindex].sh_addr
			+ ELF64_R_SYM(rel[i].r_info);
		if (!sym->st_value) {
			printk(KERN_WARNING "%s: Unknown symbol %s\n",
			       me->name, strtab + sym->st_name);
			return -ENOENT;
		}
		//dot = (sechdrs[relsec].sh_addr + rel->r_offset) & ~0x03;
		dot = (Elf64_Addr)loc & ~0x03;
		loc64 = (Elf64_Xword *)loc;

		val = sym->st_value;
		addend = rel[i].r_addend;

#if 0
#define r(t) ELF64_R_TYPE(rel[i].r_info)==t ? #t :
		printk("Symbol %s loc %p val 0x%Lx addend 0x%Lx: %s\n",
			strtab + sym->st_name,
			loc, val, addend,
			r(R_PARISC_LTOFF14R)
			r(R_PARISC_LTOFF21L)
			r(R_PARISC_PCREL22F)
			r(R_PARISC_DIR64)
			r(R_PARISC_SEGREL32)
			r(R_PARISC_FPTR64)
			"UNKNOWN");
#undef r
#endif

		switch (ELF64_R_TYPE(rel[i].r_info)) {
		case R_PARISC_LTOFF21L:
			/* LT-relative; left 21 bits */
			val = get_got(me, val, addend);
			DEBUGP("LTOFF21L Symbol %s loc %p val %lx\n",
			       strtab + sym->st_name,
			       loc, val);
			val = lrsel(val, 0);
			*loc = mask(*loc, 21) | reassemble_21(val);
			break;
		case R_PARISC_LTOFF14R:
			/* L(ltoff(val+addend)) */
			/* LT-relative; right 14 bits */
			val = get_got(me, val, addend);
			val = rrsel(val, 0);
			DEBUGP("LTOFF14R Symbol %s loc %p val %lx\n",
			       strtab + sym->st_name,
			       loc, val);
			*loc = mask(*loc, 14) | reassemble_14(val);
			break;
		case R_PARISC_PCREL22F:
			/* PC-relative; 22 bits */
			DEBUGP("PCREL22F Symbol %s loc %p val %lx\n",
			       strtab + sym->st_name,
			       loc, val);
			val += addend;
			/* can we reach it locally? */
			if (in_local(me, (void *)val)) {
				/* this is the case where the symbol is local
				 * to the module, but in a different section,
				 * so stub the jump in case it's more than 22
				 * bits away */
				val = (val - dot - 8)/4;
				if (!RELOC_REACHABLE(val, 22)) {
					/* direct distance too far, create
					 * stub entry instead */
					val = get_stub(me, sym->st_value,
						addend, ELF_STUB_DIRECT,
						loc0, targetsec);
				} else {
					/* Ok, we can reach it directly. */
					val = sym->st_value;
					val += addend;
				}
			} else {
				val = sym->st_value;
				if (strncmp(strtab + sym->st_name, "$$", 2)
				    == 0)
					val = get_stub(me, val, addend, ELF_STUB_MILLI,
						       loc0, targetsec);
				else
					val = get_stub(me, val, addend, ELF_STUB_GOT,
						       loc0, targetsec);
			}
			DEBUGP("STUB FOR %s loc %lx, val %lx+%lx at %lx\n", 
			       strtab + sym->st_name, loc, sym->st_value,
			       addend, val);
			val = (val - dot - 8)/4;
			CHECK_RELOC(val, 22);
			*loc = (*loc & ~0x3ff1ffd) | reassemble_22(val);
			break;
		case R_PARISC_PCREL32:
			/* 32-bit PC relative address */
			*loc = val - dot - 8 + addend;
			break;
		case R_PARISC_DIR64:
			/* 64-bit effective address */
			*loc64 = val + addend;
			break;
		case R_PARISC_SEGREL32:
			/* 32-bit segment relative address */
			/* See note about special handling of SEGREL32 at
			 * the beginning of this file.
			 */
			*loc = fsel(val, addend); 
			break;
		case R_PARISC_SECREL32:
			/* 32-bit section relative address. */
			*loc = fsel(val, addend);
			break;
		case R_PARISC_FPTR64:
			/* 64-bit function address */
			if(in_local(me, (void *)(val + addend))) {
				*loc64 = get_fdesc(me, val+addend);
				DEBUGP("FDESC for %s at %p points to %lx\n",
				       strtab + sym->st_name, *loc64,
				       ((Elf_Fdesc *)*loc64)->addr);
			} else {
				/* if the symbol is not local to this
				 * module then val+addend is a pointer
				 * to the function descriptor */
				DEBUGP("Non local FPTR64 Symbol %s loc %p val %lx\n",
				       strtab + sym->st_name,
				       loc, val);
				*loc64 = val + addend;
			}
			break;

		default:
			printk(KERN_ERR "module %s: Unknown relocation: %Lu\n",
			       me->name, ELF64_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}
	}
	return 0;
}
#endif

static void
register_unwind_table(struct module *me,
		      const Elf_Shdr *sechdrs)
{
	unsigned char *table, *end;
	unsigned long gp;

	if (!me->arch.unwind_section)
		return;

	table = (unsigned char *)sechdrs[me->arch.unwind_section].sh_addr;
	end = table + sechdrs[me->arch.unwind_section].sh_size;
	gp = (Elf_Addr)me->core_layout.base + me->arch.got_offset;

	DEBUGP("register_unwind_table(), sect = %d at 0x%p - 0x%p (gp=0x%lx)\n",
	       me->arch.unwind_section, table, end, gp);
	me->arch.unwind = unwind_table_add(me->name, 0, gp, table, end);
}

static void
deregister_unwind_table(struct module *me)
{
	if (me->arch.unwind)
		unwind_table_remove(me->arch.unwind);
}

int module_finalize(const Elf_Ehdr *hdr,
		    const Elf_Shdr *sechdrs,
		    struct module *me)
{
	int i;
	unsigned long nsyms;
	const char *strtab = NULL;
	Elf_Sym *newptr, *oldptr;
	Elf_Shdr *symhdr = NULL;
#ifdef DEBUG
	Elf_Fdesc *entry;
	u32 *addr;

	entry = (Elf_Fdesc *)me->init;
	printk("FINALIZE, ->init FPTR is %p, GP %lx ADDR %lx\n", entry,
	       entry->gp, entry->addr);
	addr = (u32 *)entry->addr;
	printk("INSNS: %x %x %x %x\n",
	       addr[0], addr[1], addr[2], addr[3]);
	printk("got entries used %ld, gots max %ld\n"
	       "fdescs used %ld, fdescs max %ld\n",
	       me->arch.got_count, me->arch.got_max,
	       me->arch.fdesc_count, me->arch.fdesc_max);
#endif

	register_unwind_table(me, sechdrs);

	/* haven't filled in me->symtab yet, so have to find it
	 * ourselves */
	for (i = 1; i < hdr->e_shnum; i++) {
		if(sechdrs[i].sh_type == SHT_SYMTAB
		   && (sechdrs[i].sh_flags & SHF_ALLOC)) {
			int strindex = sechdrs[i].sh_link;
			/* FIXME: AWFUL HACK
			 * The cast is to drop the const from
			 * the sechdrs pointer */
			symhdr = (Elf_Shdr *)&sechdrs[i];
			strtab = (char *)sechdrs[strindex].sh_addr;
			break;
		}
	}

	DEBUGP("module %s: strtab %p, symhdr %p\n",
	       me->name, strtab, symhdr);

	if(me->arch.got_count > MAX_GOTS) {
		printk(KERN_ERR "%s: Global Offset Table overflow (used %ld, allowed %d)\n",
				me->name, me->arch.got_count, MAX_GOTS);
		return -EINVAL;
	}

	kfree(me->arch.section);
	me->arch.section = NULL;

	/* no symbol table */
	if(symhdr == NULL)
		return 0;

	oldptr = (void *)symhdr->sh_addr;
	newptr = oldptr + 1;	/* we start counting at 1 */
	nsyms = symhdr->sh_size / sizeof(Elf_Sym);
	DEBUGP("OLD num_symtab %lu\n", nsyms);

	for (i = 1; i < nsyms; i++) {
		oldptr++;	/* note, count starts at 1 so preincrement */
		if(strncmp(strtab + oldptr->st_name,
			      ".L", 2) == 0)
			continue;

		if(newptr != oldptr)
			*newptr++ = *oldptr;
		else
			newptr++;

	}
	nsyms = newptr - (Elf_Sym *)symhdr->sh_addr;
	DEBUGP("NEW num_symtab %lu\n", nsyms);
	symhdr->sh_size = nsyms * sizeof(Elf_Sym);
	return 0;
}

void module_arch_cleanup(struct module *mod)
{
	deregister_unwind_table(mod);
}

#ifdef CONFIG_64BIT
void *dereference_module_function_descriptor(struct module *mod, void *ptr)
{
	unsigned long start_opd = (Elf64_Addr)mod->core_layout.base +
				   mod->arch.fdesc_offset;
	unsigned long end_opd = start_opd +
				mod->arch.fdesc_count * sizeof(Elf64_Fdesc);

	if (ptr < (void *)start_opd || ptr >= (void *)end_opd)
		return ptr;

	return dereference_function_descriptor(ptr);
}
#endif
