// SPDX-License-Identifier: GPL-2.0
/*
 * IA-64-specific support for kernel module loader.
 *
 * Copyright (C) 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * Loosely based on patch by Rusty Russell.
 */

/* relocs tested so far:

   DIR64LSB
   FPTR64LSB
   GPREL22
   LDXMOV
   LDXMOV
   LTOFF22
   LTOFF22X
   LTOFF22X
   LTOFF_FPTR22
   PCREL21B	(for br.call only; br.cond is not supported out of modules!)
   PCREL60B	(for brl.cond only; brl.call is not supported for modules!)
   PCREL64LSB
   SECREL32LSB
   SEGREL64LSB
 */


#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/elf.h>
#include <linux/moduleloader.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

#include <asm/patch.h>
#include <asm/unaligned.h>
#include <asm/sections.h>

#define ARCH_MODULE_DEBUG 0

#if ARCH_MODULE_DEBUG
# define DEBUGP printk
# define inline
#else
# define DEBUGP(fmt , a...)
#endif

#ifdef CONFIG_ITANIUM
# define USE_BRL	0
#else
# define USE_BRL	1
#endif

#define MAX_LTOFF	((uint64_t) (1 << 22))	/* max. allowable linkage-table offset */

/* Define some relocation helper macros/types: */

#define FORMAT_SHIFT	0
#define FORMAT_BITS	3
#define FORMAT_MASK	((1 << FORMAT_BITS) - 1)
#define VALUE_SHIFT	3
#define VALUE_BITS	5
#define VALUE_MASK	((1 << VALUE_BITS) - 1)

enum reloc_target_format {
	/* direct encoded formats: */
	RF_NONE = 0,
	RF_INSN14 = 1,
	RF_INSN22 = 2,
	RF_INSN64 = 3,
	RF_32MSB = 4,
	RF_32LSB = 5,
	RF_64MSB = 6,
	RF_64LSB = 7,

	/* formats that cannot be directly decoded: */
	RF_INSN60,
	RF_INSN21B,	/* imm21 form 1 */
	RF_INSN21M,	/* imm21 form 2 */
	RF_INSN21F	/* imm21 form 3 */
};

enum reloc_value_formula {
	RV_DIRECT = 4,		/* S + A */
	RV_GPREL = 5,		/* @gprel(S + A) */
	RV_LTREL = 6,		/* @ltoff(S + A) */
	RV_PLTREL = 7,		/* @pltoff(S + A) */
	RV_FPTR = 8,		/* @fptr(S + A) */
	RV_PCREL = 9,		/* S + A - P */
	RV_LTREL_FPTR = 10,	/* @ltoff(@fptr(S + A)) */
	RV_SEGREL = 11,		/* @segrel(S + A) */
	RV_SECREL = 12,		/* @secrel(S + A) */
	RV_BDREL = 13,		/* BD + A */
	RV_LTV = 14,		/* S + A (like RV_DIRECT, except frozen at static link-time) */
	RV_PCREL2 = 15,		/* S + A - P */
	RV_SPECIAL = 16,	/* various (see below) */
	RV_RSVD17 = 17,
	RV_TPREL = 18,		/* @tprel(S + A) */
	RV_LTREL_TPREL = 19,	/* @ltoff(@tprel(S + A)) */
	RV_DTPMOD = 20,		/* @dtpmod(S + A) */
	RV_LTREL_DTPMOD = 21,	/* @ltoff(@dtpmod(S + A)) */
	RV_DTPREL = 22,		/* @dtprel(S + A) */
	RV_LTREL_DTPREL = 23,	/* @ltoff(@dtprel(S + A)) */
	RV_RSVD24 = 24,
	RV_RSVD25 = 25,
	RV_RSVD26 = 26,
	RV_RSVD27 = 27
	/* 28-31 reserved for implementation-specific purposes.  */
};

#define N(reloc)	[R_IA64_##reloc] = #reloc

static const char *reloc_name[256] = {
	N(NONE),		N(IMM14),		N(IMM22),		N(IMM64),
	N(DIR32MSB),		N(DIR32LSB),		N(DIR64MSB),		N(DIR64LSB),
	N(GPREL22),		N(GPREL64I),		N(GPREL32MSB),		N(GPREL32LSB),
	N(GPREL64MSB),		N(GPREL64LSB),		N(LTOFF22),		N(LTOFF64I),
	N(PLTOFF22),		N(PLTOFF64I),		N(PLTOFF64MSB),		N(PLTOFF64LSB),
	N(FPTR64I),		N(FPTR32MSB),		N(FPTR32LSB),		N(FPTR64MSB),
	N(FPTR64LSB),		N(PCREL60B),		N(PCREL21B),		N(PCREL21M),
	N(PCREL21F),		N(PCREL32MSB),		N(PCREL32LSB),		N(PCREL64MSB),
	N(PCREL64LSB),		N(LTOFF_FPTR22),	N(LTOFF_FPTR64I),	N(LTOFF_FPTR32MSB),
	N(LTOFF_FPTR32LSB),	N(LTOFF_FPTR64MSB),	N(LTOFF_FPTR64LSB),	N(SEGREL32MSB),
	N(SEGREL32LSB),		N(SEGREL64MSB),		N(SEGREL64LSB),		N(SECREL32MSB),
	N(SECREL32LSB),		N(SECREL64MSB),		N(SECREL64LSB),		N(REL32MSB),
	N(REL32LSB),		N(REL64MSB),		N(REL64LSB),		N(LTV32MSB),
	N(LTV32LSB),		N(LTV64MSB),		N(LTV64LSB),		N(PCREL21BI),
	N(PCREL22),		N(PCREL64I),		N(IPLTMSB),		N(IPLTLSB),
	N(COPY),		N(LTOFF22X),		N(LDXMOV),		N(TPREL14),
	N(TPREL22),		N(TPREL64I),		N(TPREL64MSB),		N(TPREL64LSB),
	N(LTOFF_TPREL22),	N(DTPMOD64MSB),		N(DTPMOD64LSB),		N(LTOFF_DTPMOD22),
	N(DTPREL14),		N(DTPREL22),		N(DTPREL64I),		N(DTPREL32MSB),
	N(DTPREL32LSB),		N(DTPREL64MSB),		N(DTPREL64LSB),		N(LTOFF_DTPREL22)
};

#undef N

/* Opaque struct for insns, to protect against derefs. */
struct insn;

static inline uint64_t
bundle (const struct insn *insn)
{
	return (uint64_t) insn & ~0xfUL;
}

static inline int
slot (const struct insn *insn)
{
	return (uint64_t) insn & 0x3;
}

static int
apply_imm64 (struct module *mod, struct insn *insn, uint64_t val)
{
	if (slot(insn) != 1 && slot(insn) != 2) {
		printk(KERN_ERR "%s: invalid slot number %d for IMM64\n",
		       mod->name, slot(insn));
		return 0;
	}
	ia64_patch_imm64((u64) insn, val);
	return 1;
}

static int
apply_imm60 (struct module *mod, struct insn *insn, uint64_t val)
{
	if (slot(insn) != 1 && slot(insn) != 2) {
		printk(KERN_ERR "%s: invalid slot number %d for IMM60\n",
		       mod->name, slot(insn));
		return 0;
	}
	if (val + ((uint64_t) 1 << 59) >= (1UL << 60)) {
		printk(KERN_ERR "%s: value %ld out of IMM60 range\n",
			mod->name, (long) val);
		return 0;
	}
	ia64_patch_imm60((u64) insn, val);
	return 1;
}

static int
apply_imm22 (struct module *mod, struct insn *insn, uint64_t val)
{
	if (val + (1 << 21) >= (1 << 22)) {
		printk(KERN_ERR "%s: value %li out of IMM22 range\n",
			mod->name, (long)val);
		return 0;
	}
	ia64_patch((u64) insn, 0x01fffcfe000UL, (  ((val & 0x200000UL) << 15) /* bit 21 -> 36 */
					         | ((val & 0x1f0000UL) <<  6) /* bit 16 -> 22 */
					         | ((val & 0x00ff80UL) << 20) /* bit  7 -> 27 */
					         | ((val & 0x00007fUL) << 13) /* bit  0 -> 13 */));
	return 1;
}

static int
apply_imm21b (struct module *mod, struct insn *insn, uint64_t val)
{
	if (val + (1 << 20) >= (1 << 21)) {
		printk(KERN_ERR "%s: value %li out of IMM21b range\n",
			mod->name, (long)val);
		return 0;
	}
	ia64_patch((u64) insn, 0x11ffffe000UL, (  ((val & 0x100000UL) << 16) /* bit 20 -> 36 */
					        | ((val & 0x0fffffUL) << 13) /* bit  0 -> 13 */));
	return 1;
}

#if USE_BRL

struct plt_entry {
	/* Three instruction bundles in PLT. */
 	unsigned char bundle[2][16];
};

static const struct plt_entry ia64_plt_template = {
	{
		{
			0x04, 0x00, 0x00, 0x00, 0x01, 0x00, /* [MLX] nop.m 0 */
			0x00, 0x00, 0x00, 0x00, 0x00, 0x20, /*	     movl gp=TARGET_GP */
			0x00, 0x00, 0x00, 0x60
		},
		{
			0x05, 0x00, 0x00, 0x00, 0x01, 0x00, /* [MLX] nop.m 0 */
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /*	     brl.many gp=TARGET_GP */
			0x08, 0x00, 0x00, 0xc0
		}
	}
};

static int
patch_plt (struct module *mod, struct plt_entry *plt, long target_ip, unsigned long target_gp)
{
	if (apply_imm64(mod, (struct insn *) (plt->bundle[0] + 2), target_gp)
	    && apply_imm60(mod, (struct insn *) (plt->bundle[1] + 2),
			   (target_ip - (int64_t) plt->bundle[1]) / 16))
		return 1;
	return 0;
}

unsigned long
plt_target (struct plt_entry *plt)
{
	uint64_t b0, b1, *b = (uint64_t *) plt->bundle[1];
	long off;

	b0 = b[0]; b1 = b[1];
	off = (  ((b1 & 0x00fffff000000000UL) >> 36)		/* imm20b -> bit 0 */
	       | ((b0 >> 48) << 20) | ((b1 & 0x7fffffUL) << 36)	/* imm39 -> bit 20 */
	       | ((b1 & 0x0800000000000000UL) << 0));		/* i -> bit 59 */
	return (long) plt->bundle[1] + 16*off;
}

#else /* !USE_BRL */

struct plt_entry {
	/* Three instruction bundles in PLT. */
 	unsigned char bundle[3][16];
};

static const struct plt_entry ia64_plt_template = {
	{
		{
			0x05, 0x00, 0x00, 0x00, 0x01, 0x00, /* [MLX] nop.m 0 */
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /*	     movl r16=TARGET_IP */
			0x02, 0x00, 0x00, 0x60
		},
		{
			0x04, 0x00, 0x00, 0x00, 0x01, 0x00, /* [MLX] nop.m 0 */
			0x00, 0x00, 0x00, 0x00, 0x00, 0x20, /*	     movl gp=TARGET_GP */
			0x00, 0x00, 0x00, 0x60
		},
		{
			0x11, 0x00, 0x00, 0x00, 0x01, 0x00, /* [MIB] nop.m 0 */
			0x60, 0x80, 0x04, 0x80, 0x03, 0x00, /*	     mov b6=r16 */
			0x60, 0x00, 0x80, 0x00		    /*	     br.few b6 */
		}
	}
};

static int
patch_plt (struct module *mod, struct plt_entry *plt, long target_ip, unsigned long target_gp)
{
	if (apply_imm64(mod, (struct insn *) (plt->bundle[0] + 2), target_ip)
	    && apply_imm64(mod, (struct insn *) (plt->bundle[1] + 2), target_gp))
		return 1;
	return 0;
}

unsigned long
plt_target (struct plt_entry *plt)
{
	uint64_t b0, b1, *b = (uint64_t *) plt->bundle[0];

	b0 = b[0]; b1 = b[1];
	return (  ((b1 & 0x000007f000000000) >> 36)		/* imm7b -> bit 0 */
		| ((b1 & 0x07fc000000000000) >> 43)		/* imm9d -> bit 7 */
		| ((b1 & 0x0003e00000000000) >> 29)		/* imm5c -> bit 16 */
		| ((b1 & 0x0000100000000000) >> 23)		/* ic -> bit 21 */
		| ((b0 >> 46) << 22) | ((b1 & 0x7fffff) << 40)	/* imm41 -> bit 22 */
		| ((b1 & 0x0800000000000000) <<  4));		/* i -> bit 63 */
}

#endif /* !USE_BRL */

void
module_arch_freeing_init (struct module *mod)
{
	if (mod->arch.init_unw_table) {
		unw_remove_unwind_table(mod->arch.init_unw_table);
		mod->arch.init_unw_table = NULL;
	}
}

/* Have we already seen one of these relocations? */
/* FIXME: we could look in other sections, too --RR */
static int
duplicate_reloc (const Elf64_Rela *rela, unsigned int num)
{
	unsigned int i;

	for (i = 0; i < num; i++) {
		if (rela[i].r_info == rela[num].r_info && rela[i].r_addend == rela[num].r_addend)
			return 1;
	}
	return 0;
}

/* Count how many GOT entries we may need */
static unsigned int
count_gots (const Elf64_Rela *rela, unsigned int num)
{
	unsigned int i, ret = 0;

	/* Sure, this is order(n^2), but it's usually short, and not
           time critical */
	for (i = 0; i < num; i++) {
		switch (ELF64_R_TYPE(rela[i].r_info)) {
		      case R_IA64_LTOFF22:
		      case R_IA64_LTOFF22X:
		      case R_IA64_LTOFF64I:
		      case R_IA64_LTOFF_FPTR22:
		      case R_IA64_LTOFF_FPTR64I:
		      case R_IA64_LTOFF_FPTR32MSB:
		      case R_IA64_LTOFF_FPTR32LSB:
		      case R_IA64_LTOFF_FPTR64MSB:
		      case R_IA64_LTOFF_FPTR64LSB:
			if (!duplicate_reloc(rela, i))
				ret++;
			break;
		}
	}
	return ret;
}

/* Count how many PLT entries we may need */
static unsigned int
count_plts (const Elf64_Rela *rela, unsigned int num)
{
	unsigned int i, ret = 0;

	/* Sure, this is order(n^2), but it's usually short, and not
           time critical */
	for (i = 0; i < num; i++) {
		switch (ELF64_R_TYPE(rela[i].r_info)) {
		      case R_IA64_PCREL21B:
		      case R_IA64_PLTOFF22:
		      case R_IA64_PLTOFF64I:
		      case R_IA64_PLTOFF64MSB:
		      case R_IA64_PLTOFF64LSB:
		      case R_IA64_IPLTMSB:
		      case R_IA64_IPLTLSB:
			if (!duplicate_reloc(rela, i))
				ret++;
			break;
		}
	}
	return ret;
}

/* We need to create an function-descriptors for any internal function
   which is referenced. */
static unsigned int
count_fdescs (const Elf64_Rela *rela, unsigned int num)
{
	unsigned int i, ret = 0;

	/* Sure, this is order(n^2), but it's usually short, and not time critical.  */
	for (i = 0; i < num; i++) {
		switch (ELF64_R_TYPE(rela[i].r_info)) {
		      case R_IA64_FPTR64I:
		      case R_IA64_FPTR32LSB:
		      case R_IA64_FPTR32MSB:
		      case R_IA64_FPTR64LSB:
		      case R_IA64_FPTR64MSB:
		      case R_IA64_LTOFF_FPTR22:
		      case R_IA64_LTOFF_FPTR32LSB:
		      case R_IA64_LTOFF_FPTR32MSB:
		      case R_IA64_LTOFF_FPTR64I:
		      case R_IA64_LTOFF_FPTR64LSB:
		      case R_IA64_LTOFF_FPTR64MSB:
		      case R_IA64_IPLTMSB:
		      case R_IA64_IPLTLSB:
			/*
			 * Jumps to static functions sometimes go straight to their
			 * offset.  Of course, that may not be possible if the jump is
			 * from init -> core or vice. versa, so we need to generate an
			 * FDESC (and PLT etc) for that.
			 */
		      case R_IA64_PCREL21B:
			if (!duplicate_reloc(rela, i))
				ret++;
			break;
		}
	}
	return ret;
}

int
module_frob_arch_sections (Elf_Ehdr *ehdr, Elf_Shdr *sechdrs, char *secstrings,
			   struct module *mod)
{
	unsigned long core_plts = 0, init_plts = 0, gots = 0, fdescs = 0;
	Elf64_Shdr *s, *sechdrs_end = sechdrs + ehdr->e_shnum;

	/*
	 * To store the PLTs and function-descriptors, we expand the .text section for
	 * core module-code and the .init.text section for initialization code.
	 */
	for (s = sechdrs; s < sechdrs_end; ++s)
		if (strcmp(".core.plt", secstrings + s->sh_name) == 0)
			mod->arch.core_plt = s;
		else if (strcmp(".init.plt", secstrings + s->sh_name) == 0)
			mod->arch.init_plt = s;
		else if (strcmp(".got", secstrings + s->sh_name) == 0)
			mod->arch.got = s;
		else if (strcmp(".opd", secstrings + s->sh_name) == 0)
			mod->arch.opd = s;
		else if (strcmp(".IA_64.unwind", secstrings + s->sh_name) == 0)
			mod->arch.unwind = s;

	if (!mod->arch.core_plt || !mod->arch.init_plt || !mod->arch.got || !mod->arch.opd) {
		printk(KERN_ERR "%s: sections missing\n", mod->name);
		return -ENOEXEC;
	}

	/* GOT and PLTs can occur in any relocated section... */
	for (s = sechdrs + 1; s < sechdrs_end; ++s) {
		const Elf64_Rela *rels = (void *)ehdr + s->sh_offset;
		unsigned long numrels = s->sh_size/sizeof(Elf64_Rela);

		if (s->sh_type != SHT_RELA)
			continue;

		gots += count_gots(rels, numrels);
		fdescs += count_fdescs(rels, numrels);
		if (strstr(secstrings + s->sh_name, ".init"))
			init_plts += count_plts(rels, numrels);
		else
			core_plts += count_plts(rels, numrels);
	}

	mod->arch.core_plt->sh_type = SHT_NOBITS;
	mod->arch.core_plt->sh_flags = SHF_EXECINSTR | SHF_ALLOC;
	mod->arch.core_plt->sh_addralign = 16;
	mod->arch.core_plt->sh_size = core_plts * sizeof(struct plt_entry);
	mod->arch.init_plt->sh_type = SHT_NOBITS;
	mod->arch.init_plt->sh_flags = SHF_EXECINSTR | SHF_ALLOC;
	mod->arch.init_plt->sh_addralign = 16;
	mod->arch.init_plt->sh_size = init_plts * sizeof(struct plt_entry);
	mod->arch.got->sh_type = SHT_NOBITS;
	mod->arch.got->sh_flags = ARCH_SHF_SMALL | SHF_ALLOC;
	mod->arch.got->sh_addralign = 8;
	mod->arch.got->sh_size = gots * sizeof(struct got_entry);
	mod->arch.opd->sh_type = SHT_NOBITS;
	mod->arch.opd->sh_flags = SHF_ALLOC;
	mod->arch.opd->sh_addralign = 8;
	mod->arch.opd->sh_size = fdescs * sizeof(struct fdesc);
	DEBUGP("%s: core.plt=%lx, init.plt=%lx, got=%lx, fdesc=%lx\n",
	       __func__, mod->arch.core_plt->sh_size, mod->arch.init_plt->sh_size,
	       mod->arch.got->sh_size, mod->arch.opd->sh_size);
	return 0;
}

static inline int
in_init (const struct module *mod, uint64_t addr)
{
	return addr - (uint64_t) mod->init_layout.base < mod->init_layout.size;
}

static inline int
in_core (const struct module *mod, uint64_t addr)
{
	return addr - (uint64_t) mod->core_layout.base < mod->core_layout.size;
}

static inline int
is_internal (const struct module *mod, uint64_t value)
{
	return in_init(mod, value) || in_core(mod, value);
}

/*
 * Get gp-relative offset for the linkage-table entry of VALUE.
 */
static uint64_t
get_ltoff (struct module *mod, uint64_t value, int *okp)
{
	struct got_entry *got, *e;

	if (!*okp)
		return 0;

	got = (void *) mod->arch.got->sh_addr;
	for (e = got; e < got + mod->arch.next_got_entry; ++e)
		if (e->val == value)
			goto found;

	/* Not enough GOT entries? */
	BUG_ON(e >= (struct got_entry *) (mod->arch.got->sh_addr + mod->arch.got->sh_size));

	e->val = value;
	++mod->arch.next_got_entry;
  found:
	return (uint64_t) e - mod->arch.gp;
}

static inline int
gp_addressable (struct module *mod, uint64_t value)
{
	return value - mod->arch.gp + MAX_LTOFF/2 < MAX_LTOFF;
}

/* Get PC-relative PLT entry for this value.  Returns 0 on failure. */
static uint64_t
get_plt (struct module *mod, const struct insn *insn, uint64_t value, int *okp)
{
	struct plt_entry *plt, *plt_end;
	uint64_t target_ip, target_gp;

	if (!*okp)
		return 0;

	if (in_init(mod, (uint64_t) insn)) {
		plt = (void *) mod->arch.init_plt->sh_addr;
		plt_end = (void *) plt + mod->arch.init_plt->sh_size;
	} else {
		plt = (void *) mod->arch.core_plt->sh_addr;
		plt_end = (void *) plt + mod->arch.core_plt->sh_size;
	}

	/* "value" is a pointer to a function-descriptor; fetch the target ip/gp from it: */
	target_ip = ((uint64_t *) value)[0];
	target_gp = ((uint64_t *) value)[1];

	/* Look for existing PLT entry. */
	while (plt->bundle[0][0]) {
		if (plt_target(plt) == target_ip)
			goto found;
		if (++plt >= plt_end)
			BUG();
	}
	*plt = ia64_plt_template;
	if (!patch_plt(mod, plt, target_ip, target_gp)) {
		*okp = 0;
		return 0;
	}
#if ARCH_MODULE_DEBUG
	if (plt_target(plt) != target_ip) {
		printk("%s: mistargeted PLT: wanted %lx, got %lx\n",
		       __func__, target_ip, plt_target(plt));
		*okp = 0;
		return 0;
	}
#endif
  found:
	return (uint64_t) plt;
}

/* Get function descriptor for VALUE. */
static uint64_t
get_fdesc (struct module *mod, uint64_t value, int *okp)
{
	struct fdesc *fdesc = (void *) mod->arch.opd->sh_addr;

	if (!*okp)
		return 0;

	if (!value) {
		printk(KERN_ERR "%s: fdesc for zero requested!\n", mod->name);
		return 0;
	}

	if (!is_internal(mod, value))
		/*
		 * If it's not a module-local entry-point, "value" already points to a
		 * function-descriptor.
		 */
		return value;

	/* Look for existing function descriptor. */
	while (fdesc->ip) {
		if (fdesc->ip == value)
			return (uint64_t)fdesc;
		if ((uint64_t) ++fdesc >= mod->arch.opd->sh_addr + mod->arch.opd->sh_size)
			BUG();
	}

	/* Create new one */
	fdesc->ip = value;
	fdesc->gp = mod->arch.gp;
	return (uint64_t) fdesc;
}

static inline int
do_reloc (struct module *mod, uint8_t r_type, Elf64_Sym *sym, uint64_t addend,
	  Elf64_Shdr *sec, void *location)
{
	enum reloc_target_format format = (r_type >> FORMAT_SHIFT) & FORMAT_MASK;
	enum reloc_value_formula formula = (r_type >> VALUE_SHIFT) & VALUE_MASK;
	uint64_t val;
	int ok = 1;

	val = sym->st_value + addend;

	switch (formula) {
	      case RV_SEGREL:	/* segment base is arbitrarily chosen to be 0 for kernel modules */
	      case RV_DIRECT:
		break;

	      case RV_GPREL:	  val -= mod->arch.gp; break;
	      case RV_LTREL:	  val = get_ltoff(mod, val, &ok); break;
	      case RV_PLTREL:	  val = get_plt(mod, location, val, &ok); break;
	      case RV_FPTR:	  val = get_fdesc(mod, val, &ok); break;
	      case RV_SECREL:	  val -= sec->sh_addr; break;
	      case RV_LTREL_FPTR: val = get_ltoff(mod, get_fdesc(mod, val, &ok), &ok); break;

	      case RV_PCREL:
		switch (r_type) {
		      case R_IA64_PCREL21B:
			if ((in_init(mod, val) && in_core(mod, (uint64_t)location)) ||
			    (in_core(mod, val) && in_init(mod, (uint64_t)location))) {
				/*
				 * Init section may have been allocated far away from core,
				 * if the branch won't reach, then allocate a plt for it.
				 */
				uint64_t delta = ((int64_t)val - (int64_t)location) / 16;
				if (delta + (1 << 20) >= (1 << 21)) {
					val = get_fdesc(mod, val, &ok);
					val = get_plt(mod, location, val, &ok);
				}
			} else if (!is_internal(mod, val))
				val = get_plt(mod, location, val, &ok);
			fallthrough;
		      default:
			val -= bundle(location);
			break;

		      case R_IA64_PCREL32MSB:
		      case R_IA64_PCREL32LSB:
		      case R_IA64_PCREL64MSB:
		      case R_IA64_PCREL64LSB:
			val -= (uint64_t) location;
			break;

		}
		switch (r_type) {
		      case R_IA64_PCREL60B: format = RF_INSN60; break;
		      case R_IA64_PCREL21B: format = RF_INSN21B; break;
		      case R_IA64_PCREL21M: format = RF_INSN21M; break;
		      case R_IA64_PCREL21F: format = RF_INSN21F; break;
		      default: break;
		}
		break;

	      case RV_BDREL:
		val -= (uint64_t) (in_init(mod, val) ? mod->init_layout.base : mod->core_layout.base);
		break;

	      case RV_LTV:
		/* can link-time value relocs happen here?  */
		BUG();
		break;

	      case RV_PCREL2:
		if (r_type == R_IA64_PCREL21BI) {
			if (!is_internal(mod, val)) {
				printk(KERN_ERR "%s: %s reloc against "
					"non-local symbol (%lx)\n", __func__,
					reloc_name[r_type], (unsigned long)val);
				return -ENOEXEC;
			}
			format = RF_INSN21B;
		}
		val -= bundle(location);
		break;

	      case RV_SPECIAL:
		switch (r_type) {
		      case R_IA64_IPLTMSB:
		      case R_IA64_IPLTLSB:
			val = get_fdesc(mod, get_plt(mod, location, val, &ok), &ok);
			format = RF_64LSB;
			if (r_type == R_IA64_IPLTMSB)
				format = RF_64MSB;
			break;

		      case R_IA64_SUB:
			val = addend - sym->st_value;
			format = RF_INSN64;
			break;

		      case R_IA64_LTOFF22X:
			if (gp_addressable(mod, val))
				val -= mod->arch.gp;
			else
				val = get_ltoff(mod, val, &ok);
			format = RF_INSN22;
			break;

		      case R_IA64_LDXMOV:
			if (gp_addressable(mod, val)) {
				/* turn "ld8" into "mov": */
				DEBUGP("%s: patching ld8 at %p to mov\n", __func__, location);
				ia64_patch((u64) location, 0x1fff80fe000UL, 0x10000000000UL);
			}
			return 0;

		      default:
			if (reloc_name[r_type])
				printk(KERN_ERR "%s: special reloc %s not supported",
				       mod->name, reloc_name[r_type]);
			else
				printk(KERN_ERR "%s: unknown special reloc %x\n",
				       mod->name, r_type);
			return -ENOEXEC;
		}
		break;

	      case RV_TPREL:
	      case RV_LTREL_TPREL:
	      case RV_DTPMOD:
	      case RV_LTREL_DTPMOD:
	      case RV_DTPREL:
	      case RV_LTREL_DTPREL:
		printk(KERN_ERR "%s: %s reloc not supported\n",
		       mod->name, reloc_name[r_type] ? reloc_name[r_type] : "?");
		return -ENOEXEC;

	      default:
		printk(KERN_ERR "%s: unknown reloc %x\n", mod->name, r_type);
		return -ENOEXEC;
	}

	if (!ok)
		return -ENOEXEC;

	DEBUGP("%s: [%p]<-%016lx = %s(%lx)\n", __func__, location, val,
	       reloc_name[r_type] ? reloc_name[r_type] : "?", sym->st_value + addend);

	switch (format) {
	      case RF_INSN21B:	ok = apply_imm21b(mod, location, (int64_t) val / 16); break;
	      case RF_INSN22:	ok = apply_imm22(mod, location, val); break;
	      case RF_INSN64:	ok = apply_imm64(mod, location, val); break;
	      case RF_INSN60:	ok = apply_imm60(mod, location, (int64_t) val / 16); break;
	      case RF_32LSB:	put_unaligned(val, (uint32_t *) location); break;
	      case RF_64LSB:	put_unaligned(val, (uint64_t *) location); break;
	      case RF_32MSB:	/* ia64 Linux is little-endian... */
	      case RF_64MSB:	/* ia64 Linux is little-endian... */
	      case RF_INSN14:	/* must be within-module, i.e., resolved by "ld -r" */
	      case RF_INSN21M:	/* must be within-module, i.e., resolved by "ld -r" */
	      case RF_INSN21F:	/* must be within-module, i.e., resolved by "ld -r" */
		printk(KERN_ERR "%s: format %u needed by %s reloc is not supported\n",
		       mod->name, format, reloc_name[r_type] ? reloc_name[r_type] : "?");
		return -ENOEXEC;

	      default:
		printk(KERN_ERR "%s: relocation %s resulted in unknown format %u\n",
		       mod->name, reloc_name[r_type] ? reloc_name[r_type] : "?", format);
		return -ENOEXEC;
	}
	return ok ? 0 : -ENOEXEC;
}

int
apply_relocate_add (Elf64_Shdr *sechdrs, const char *strtab, unsigned int symindex,
		    unsigned int relsec, struct module *mod)
{
	unsigned int i, n = sechdrs[relsec].sh_size / sizeof(Elf64_Rela);
	Elf64_Rela *rela = (void *) sechdrs[relsec].sh_addr;
	Elf64_Shdr *target_sec;
	int ret;

	DEBUGP("%s: applying section %u (%u relocs) to %u\n", __func__,
	       relsec, n, sechdrs[relsec].sh_info);

	target_sec = sechdrs + sechdrs[relsec].sh_info;

	if (target_sec->sh_entsize == ~0UL)
		/*
		 * If target section wasn't allocated, we don't need to relocate it.
		 * Happens, e.g., for debug sections.
		 */
		return 0;

	if (!mod->arch.gp) {
		/*
		 * XXX Should have an arch-hook for running this after final section
		 *     addresses have been selected...
		 */
		uint64_t gp;
		if (mod->core_layout.size > MAX_LTOFF)
			/*
			 * This takes advantage of fact that SHF_ARCH_SMALL gets allocated
			 * at the end of the module.
			 */
			gp = mod->core_layout.size - MAX_LTOFF / 2;
		else
			gp = mod->core_layout.size / 2;
		gp = (uint64_t) mod->core_layout.base + ((gp + 7) & -8);
		mod->arch.gp = gp;
		DEBUGP("%s: placing gp at 0x%lx\n", __func__, gp);
	}

	for (i = 0; i < n; i++) {
		ret = do_reloc(mod, ELF64_R_TYPE(rela[i].r_info),
			       ((Elf64_Sym *) sechdrs[symindex].sh_addr
				+ ELF64_R_SYM(rela[i].r_info)),
			       rela[i].r_addend, target_sec,
			       (void *) target_sec->sh_addr + rela[i].r_offset);
		if (ret < 0)
			return ret;
	}
	return 0;
}

/*
 * Modules contain a single unwind table which covers both the core and the init text
 * sections but since the two are not contiguous, we need to split this table up such that
 * we can register (and unregister) each "segment" separately.  Fortunately, this sounds
 * more complicated than it really is.
 */
static void
register_unwind_table (struct module *mod)
{
	struct unw_table_entry *start = (void *) mod->arch.unwind->sh_addr;
	struct unw_table_entry *end = start + mod->arch.unwind->sh_size / sizeof (*start);
	struct unw_table_entry tmp, *e1, *e2, *core, *init;
	unsigned long num_init = 0, num_core = 0;

	/* First, count how many init and core unwind-table entries there are.  */
	for (e1 = start; e1 < end; ++e1)
		if (in_init(mod, e1->start_offset))
			++num_init;
		else
			++num_core;
	/*
	 * Second, sort the table such that all unwind-table entries for the init and core
	 * text sections are nicely separated.  We do this with a stupid bubble sort
	 * (unwind tables don't get ridiculously huge).
	 */
	for (e1 = start; e1 < end; ++e1) {
		for (e2 = e1 + 1; e2 < end; ++e2) {
			if (e2->start_offset < e1->start_offset) {
				tmp = *e1;
				*e1 = *e2;
				*e2 = tmp;
			}
		}
	}
	/*
	 * Third, locate the init and core segments in the unwind table:
	 */
	if (in_init(mod, start->start_offset)) {
		init = start;
		core = start + num_init;
	} else {
		core = start;
		init = start + num_core;
	}

	DEBUGP("%s: name=%s, gp=%lx, num_init=%lu, num_core=%lu\n", __func__,
	       mod->name, mod->arch.gp, num_init, num_core);

	/*
	 * Fourth, register both tables (if not empty).
	 */
	if (num_core > 0) {
		mod->arch.core_unw_table = unw_add_unwind_table(mod->name, 0, mod->arch.gp,
								core, core + num_core);
		DEBUGP("%s:  core: handle=%p [%p-%p)\n", __func__,
		       mod->arch.core_unw_table, core, core + num_core);
	}
	if (num_init > 0) {
		mod->arch.init_unw_table = unw_add_unwind_table(mod->name, 0, mod->arch.gp,
								init, init + num_init);
		DEBUGP("%s:  init: handle=%p [%p-%p)\n", __func__,
		       mod->arch.init_unw_table, init, init + num_init);
	}
}

int
module_finalize (const Elf_Ehdr *hdr, const Elf_Shdr *sechdrs, struct module *mod)
{
	DEBUGP("%s: init: entry=%p\n", __func__, mod->init);
	if (mod->arch.unwind)
		register_unwind_table(mod);
	return 0;
}

void
module_arch_cleanup (struct module *mod)
{
	if (mod->arch.init_unw_table) {
		unw_remove_unwind_table(mod->arch.init_unw_table);
		mod->arch.init_unw_table = NULL;
	}
	if (mod->arch.core_unw_table) {
		unw_remove_unwind_table(mod->arch.core_unw_table);
		mod->arch.core_unw_table = NULL;
	}
}

void *dereference_module_function_descriptor(struct module *mod, void *ptr)
{
	Elf64_Shdr *opd = mod->arch.opd;

	if (ptr < (void *)opd->sh_addr ||
			ptr >= (void *)(opd->sh_addr + opd->sh_size))
		return ptr;

	return dereference_function_descriptor(ptr);
}
