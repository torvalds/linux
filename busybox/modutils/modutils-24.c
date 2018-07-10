/* vi: set sw=4 ts=4: */
/*
 * Mini insmod implementation for busybox
 *
 * This version of insmod supports ARM, CRIS, H8/300, x86, ia64, x86_64,
 * m68k, MIPS, PowerPC, S390, SH3/4/5, Sparc, v850e, and x86_64.
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 * and Ron Alder <alder@lineo.com>
 *
 * Rodney Radford <rradford@mindspring.com> 17-Aug-2004.
 *   Added x86_64 support.
 *
 * Miles Bader <miles@gnu.org> added NEC V850E support.
 *
 * Modified by Bryan Rittmeyer <bryan@ixiacom.com> to support SH4
 * and (theoretically) SH3. I have only tested SH4 in little endian mode.
 *
 * Modified by Alcove, Julien Gaulmin <julien.gaulmin@alcove.fr> and
 * Nicolas Ferre <nicolas.ferre@alcove.fr> to support ARM7TDMI.  Only
 * very minor changes required to also work with StrongArm and presumably
 * all ARM based systems.
 *
 * Yoshinori Sato <ysato@users.sourceforge.jp> 19-May-2004.
 *   added Renesas H8/300 support.
 *
 * Paul Mundt <lethal@linux-sh.org> 08-Aug-2003.
 *   Integrated support for sh64 (SH-5), from preliminary modutils
 *   patches from Benedict Gaster <benedict.gaster@superh.com>.
 *   Currently limited to support for 32bit ABI.
 *
 * Magnus Damm <damm@opensource.se> 22-May-2002.
 *   The plt and got code are now using the same structs.
 *   Added generic linked list code to fully support PowerPC.
 *   Replaced the mess in arch_apply_relocation() with architecture blocks.
 *   The arch_create_got() function got cleaned up with architecture blocks.
 *   These blocks should be easy maintain and sync with obj_xxx.c in modutils.
 *
 * Magnus Damm <damm@opensource.se> added PowerPC support 20-Feb-2001.
 *   PowerPC specific code stolen from modutils-2.3.16,
 *   written by Paul Mackerras, Copyright 1996, 1997 Linux International.
 *   I've only tested the code on mpc8xx platforms in big-endian mode.
 *   Did some cleanup and added USE_xxx_ENTRIES...
 *
 * Quinn Jensen <jensenq@lineo.com> added MIPS support 23-Feb-2001.
 *   based on modutils-2.4.2
 *   MIPS specific support for Elf loading and relocation.
 *   Copyright 1996, 1997 Linux International.
 *   Contributed by Ralf Baechle <ralf@gnu.ai.mit.edu>
 *
 * Based almost entirely on the Linux modutils-2.3.11 implementation.
 *   Copyright 1996, 1997 Linux International.
 *   New implementation contributed by Richard Henderson <rth@tamu.edu>
 *   Based on original work by Bjorn Ekwall <bj0rn@blox.se>
 *   Restructured (and partly rewritten) by:
 *   Björn Ekwall <bj0rn@blox.se> February 1999
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

//kbuild:lib-$(CONFIG_FEATURE_2_4_MODULES) += modutils-24.o

#include "libbb.h"
#include "modutils.h"
#include <sys/utsname.h>

#if ENABLE_FEATURE_INSMOD_LOADINKMEM
#define LOADBITS 0
#else
#define LOADBITS 1
#endif

/* Alpha */
#if defined(__alpha__)
#define MATCH_MACHINE(x) (x == EM_ALPHA)
#define SHT_RELM       SHT_RELA
#define Elf64_RelM     Elf64_Rela
#define ELFCLASSM      ELFCLASS64
#endif

/* ARM support */
#if defined(__arm__)
#define MATCH_MACHINE(x) (x == EM_ARM)
#define SHT_RELM	SHT_REL
#define Elf32_RelM	Elf32_Rel
#define ELFCLASSM	ELFCLASS32
#define USE_PLT_ENTRIES
#define PLT_ENTRY_SIZE 8
#define USE_GOT_ENTRIES
#define GOT_ENTRY_SIZE 8
#define USE_SINGLE
#endif

/* NDS32 support */
#if defined(__nds32__) || defined(__NDS32__)
#define CONFIG_USE_GOT_ENTRIES
#define CONFIG_GOT_ENTRY_SIZE 4
#define CONFIG_USE_SINGLE

#if defined(__NDS32_EB__)
#define MATCH_MACHINE(x) (x == EM_NDS32)
#define SHT_RELM    SHT_RELA
#define Elf32_RelM  Elf32_Rela
#define ELFCLASSM   ELFCLASS32
#endif

#if defined(__NDS32_EL__)
#define MATCH_MACHINE(x) (x == EM_NDS32)
#define SHT_RELM    SHT_RELA
#define Elf32_RelM  Elf32_Rela
#define ELFCLASSM   ELFCLASS32
#endif
#endif

/* blackfin */
#if defined(BFIN)
#define MATCH_MACHINE(x) (x == EM_BLACKFIN)
#define SHT_RELM	SHT_RELA
#define Elf32_RelM	Elf32_Rela
#define ELFCLASSM	ELFCLASS32
#endif

/* CRIS */
#if defined(__cris__)
#define MATCH_MACHINE(x) (x == EM_CRIS)
#define SHT_RELM	SHT_RELA
#define Elf32_RelM	Elf32_Rela
#define ELFCLASSM	ELFCLASS32
#ifndef EM_CRIS
#define EM_CRIS 76
#define R_CRIS_NONE 0
#define R_CRIS_32   3
#endif
#endif

/* H8/300 */
#if defined(__H8300H__) || defined(__H8300S__)
#define MATCH_MACHINE(x) (x == EM_H8_300)
#define SHT_RELM	SHT_RELA
#define Elf32_RelM	Elf32_Rela
#define ELFCLASSM	ELFCLASS32
#define USE_SINGLE
#define SYMBOL_PREFIX	"_"
#endif

/* PA-RISC / HP-PA */
#if defined(__hppa__)
#define MATCH_MACHINE(x) (x == EM_PARISC)
#define SHT_RELM       SHT_RELA
#if defined(__LP64__)
#define Elf64_RelM     Elf64_Rela
#define ELFCLASSM      ELFCLASS64
#else
#define Elf32_RelM     Elf32_Rela
#define ELFCLASSM      ELFCLASS32
#endif
#endif

/* x86 */
#if defined(__i386__)
#ifndef EM_486
#define MATCH_MACHINE(x) (x == EM_386)
#else
#define MATCH_MACHINE(x) (x == EM_386 || x == EM_486)
#endif
#define SHT_RELM	SHT_REL
#define Elf32_RelM	Elf32_Rel
#define ELFCLASSM	ELFCLASS32
#define USE_GOT_ENTRIES
#define GOT_ENTRY_SIZE 4
#define USE_SINGLE
#endif

/* IA64, aka Itanium */
#if defined(__ia64__)
#define MATCH_MACHINE(x) (x == EM_IA_64)
#define SHT_RELM       SHT_RELA
#define Elf64_RelM     Elf64_Rela
#define ELFCLASSM      ELFCLASS64
#endif

/* m68k */
#if defined(__mc68000__)
#define MATCH_MACHINE(x) (x == EM_68K)
#define SHT_RELM	SHT_RELA
#define Elf32_RelM	Elf32_Rela
#define ELFCLASSM	ELFCLASS32
#define USE_GOT_ENTRIES
#define GOT_ENTRY_SIZE 4
#define USE_SINGLE
#endif

/* Microblaze */
#if defined(__microblaze__)
#define USE_SINGLE
#include <linux/elf-em.h>
#define MATCH_MACHINE(x) (x == EM_XILINX_MICROBLAZE)
#define SHT_RELM	SHT_RELA
#define Elf32_RelM	Elf32_Rela
#define ELFCLASSM	ELFCLASS32
#endif

/* MIPS */
#if defined(__mips__)
#define MATCH_MACHINE(x) (x == EM_MIPS || x == EM_MIPS_RS3_LE)
#define SHT_RELM	SHT_REL
#define Elf32_RelM	Elf32_Rel
#define ELFCLASSM	ELFCLASS32
/* Account for ELF spec changes.  */
#ifndef EM_MIPS_RS3_LE
#ifdef EM_MIPS_RS4_BE
#define EM_MIPS_RS3_LE	EM_MIPS_RS4_BE
#else
#define EM_MIPS_RS3_LE	10
#endif
#endif /* !EM_MIPS_RS3_LE */
#define ARCHDATAM       "__dbe_table"
#endif

/* Nios II */
#if defined(__nios2__)
#define MATCH_MACHINE(x) (x == EM_ALTERA_NIOS2)
#define SHT_RELM	SHT_RELA
#define Elf32_RelM	Elf32_Rela
#define ELFCLASSM	ELFCLASS32
#endif

/* PowerPC */
#if defined(__powerpc64__)
#define MATCH_MACHINE(x) (x == EM_PPC64)
#define SHT_RELM	SHT_RELA
#define Elf64_RelM	Elf64_Rela
#define ELFCLASSM	ELFCLASS64
#elif defined(__powerpc__)
#define MATCH_MACHINE(x) (x == EM_PPC)
#define SHT_RELM	SHT_RELA
#define Elf32_RelM	Elf32_Rela
#define ELFCLASSM	ELFCLASS32
#define USE_PLT_ENTRIES
#define PLT_ENTRY_SIZE 16
#define USE_PLT_LIST
#define LIST_ARCHTYPE ElfW(Addr)
#define USE_LIST
#define ARCHDATAM       "__ftr_fixup"
#endif

/* S390 */
#if defined(__s390__)
#define MATCH_MACHINE(x) (x == EM_S390)
#define SHT_RELM	SHT_RELA
#define Elf32_RelM	Elf32_Rela
#define ELFCLASSM	ELFCLASS32
#define USE_PLT_ENTRIES
#define PLT_ENTRY_SIZE 8
#define USE_GOT_ENTRIES
#define GOT_ENTRY_SIZE 8
#define USE_SINGLE
#endif

/* SuperH */
#if defined(__sh__)
#define MATCH_MACHINE(x) (x == EM_SH)
#define SHT_RELM	SHT_RELA
#define Elf32_RelM	Elf32_Rela
#define ELFCLASSM	ELFCLASS32
#define USE_GOT_ENTRIES
#define GOT_ENTRY_SIZE 4
#define USE_SINGLE
/* the SH changes have only been tested in =little endian= mode */
/* I'm not sure about big endian, so let's warn: */
#if defined(__sh__) && BB_BIG_ENDIAN
# error insmod.c may require changes for use on big endian SH
#endif
/* it may or may not work on the SH1/SH2... Error on those also */
#if ((!(defined(__SH3__) || defined(__SH4__) || defined(__SH5__)))) && (defined(__sh__))
#error insmod.c may require changes for SH1 or SH2 use
#endif
#endif

/* Sparc */
#if defined(__sparc__)
#define MATCH_MACHINE(x) (x == EM_SPARC)
#define SHT_RELM       SHT_RELA
#define Elf32_RelM     Elf32_Rela
#define ELFCLASSM      ELFCLASS32
#endif

/* v850e */
#if defined(__v850e__)
#define MATCH_MACHINE(x) ((x) == EM_V850 || (x) == EM_CYGNUS_V850)
#define SHT_RELM	SHT_RELA
#define Elf32_RelM	Elf32_Rela
#define ELFCLASSM	ELFCLASS32
#define USE_PLT_ENTRIES
#define PLT_ENTRY_SIZE 8
#define USE_SINGLE
#ifndef EM_CYGNUS_V850	/* grumble */
#define EM_CYGNUS_V850	0x9080
#endif
#define SYMBOL_PREFIX	"_"
#endif

/* X86_64  */
#if defined(__x86_64__)
#define MATCH_MACHINE(x) (x == EM_X86_64)
#define SHT_RELM	SHT_RELA
#define USE_GOT_ENTRIES
#define GOT_ENTRY_SIZE 8
#define USE_SINGLE
#define Elf64_RelM	Elf64_Rela
#define ELFCLASSM	ELFCLASS64
#endif

#ifndef SHT_RELM
#error Sorry, but insmod.c does not yet support this architecture...
#endif


//----------------------------------------------------------------------------
//--------modutils module.h, lines 45-242
//----------------------------------------------------------------------------

/* Definitions for the Linux module syscall interface.
   Copyright 1996, 1997 Linux International.

   Contributed by Richard Henderson <rth@tamu.edu>

   This file is part of the Linux modutils.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2 of the License, or (at your
   option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */


#ifndef MODUTILS_MODULE_H

/*======================================================================*/
/* For sizeof() which are related to the module platform and not to the
   environment isnmod is running in, use sizeof_xx instead of sizeof(xx).  */

#define tgt_sizeof_char		sizeof(char)
#define tgt_sizeof_short	sizeof(short)
#define tgt_sizeof_int		sizeof(int)
#define tgt_sizeof_long		sizeof(long)
#define tgt_sizeof_char_p	sizeof(char *)
#define tgt_sizeof_void_p	sizeof(void *)
#define tgt_long		long

#if defined(__sparc__) && !defined(__sparc_v9__) && defined(ARCH_sparc64)
#undef tgt_sizeof_long
#undef tgt_sizeof_char_p
#undef tgt_sizeof_void_p
#undef tgt_long
enum {
	tgt_sizeof_long = 8,
	tgt_sizeof_char_p = 8,
	tgt_sizeof_void_p = 8
};
#define tgt_long		long long
#endif

/*======================================================================*/
/* The structures used in Linux 2.1.  */

/* Note: new_module_symbol does not use tgt_long intentionally */
struct new_module_symbol {
	unsigned long value;
	unsigned long name;
};

struct new_module_persist;

struct new_module_ref {
	unsigned tgt_long dep;		/* kernel addresses */
	unsigned tgt_long ref;
	unsigned tgt_long next_ref;
};

struct new_module {
	unsigned tgt_long size_of_struct;	/* == sizeof(module) */
	unsigned tgt_long next;
	unsigned tgt_long name;
	unsigned tgt_long size;

	tgt_long usecount;
	unsigned tgt_long flags;		/* AUTOCLEAN et al */

	unsigned nsyms;
	unsigned ndeps;

	unsigned tgt_long syms;
	unsigned tgt_long deps;
	unsigned tgt_long refs;
	unsigned tgt_long init;
	unsigned tgt_long cleanup;
	unsigned tgt_long ex_table_start;
	unsigned tgt_long ex_table_end;
#ifdef __alpha__
	unsigned tgt_long gp;
#endif
	/* Everything after here is extension.  */
	unsigned tgt_long persist_start;
	unsigned tgt_long persist_end;
	unsigned tgt_long can_unload;
	unsigned tgt_long runsize;
	const char *kallsyms_start;     /* All symbols for kernel debugging */
	const char *kallsyms_end;
	const char *archdata_start;     /* arch specific data for module */
	const char *archdata_end;
	const char *kernel_data;        /* Reserved for kernel internal use */
};

#ifdef ARCHDATAM
#define ARCHDATA_SEC_NAME ARCHDATAM
#else
#define ARCHDATA_SEC_NAME "__archdata"
#endif
#define KALLSYMS_SEC_NAME "__kallsyms"


struct new_module_info {
	unsigned long addr;
	unsigned long size;
	unsigned long flags;
	long usecount;
};

/* Bits of module.flags.  */
enum {
	NEW_MOD_RUNNING = 1,
	NEW_MOD_DELETED = 2,
	NEW_MOD_AUTOCLEAN = 4,
	NEW_MOD_VISITED = 8,
	NEW_MOD_USED_ONCE = 16
};

int init_module(const char *name, const struct new_module *);
int query_module(const char *name, int which, void *buf,
		size_t bufsize, size_t *ret);

/* Values for query_module's which.  */
enum {
	QM_MODULES = 1,
	QM_DEPS = 2,
	QM_REFS = 3,
	QM_SYMBOLS = 4,
	QM_INFO = 5
};

/*======================================================================*/
/* The system calls unchanged between 2.0 and 2.1.  */

unsigned long create_module(const char *, size_t);
int delete_module(const char *module, unsigned int flags);


#endif /* module.h */

//----------------------------------------------------------------------------
//--------end of modutils module.h
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//--------modutils obj.h, lines 253-462
//----------------------------------------------------------------------------

/* Elf object file loading and relocation routines.
   Copyright 1996, 1997 Linux International.

   Contributed by Richard Henderson <rth@tamu.edu>

   This file is part of the Linux modutils.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2 of the License, or (at your
   option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */


#ifndef MODUTILS_OBJ_H

/* The relocatable object is manipulated using elfin types.  */

#include <elf.h>
#include <endian.h>

#ifndef ElfW
# if ELFCLASSM == ELFCLASS32
#  define ElfW(x)  Elf32_ ## x
#  define ELFW(x)  ELF32_ ## x
# else
#  define ElfW(x)  Elf64_ ## x
#  define ELFW(x)  ELF64_ ## x
# endif
#endif

/* For some reason this is missing from some ancient C libraries....  */
#ifndef ELF32_ST_INFO
# define ELF32_ST_INFO(bind, type)       (((bind) << 4) + ((type) & 0xf))
#endif

#ifndef ELF64_ST_INFO
# define ELF64_ST_INFO(bind, type)       (((bind) << 4) + ((type) & 0xf))
#endif

#define ELF_ST_BIND(info) ELFW(ST_BIND)(info)
#define ELF_ST_TYPE(info) ELFW(ST_TYPE)(info)
#define ELF_ST_INFO(bind, type) ELFW(ST_INFO)(bind, type)
#define ELF_R_TYPE(val) ELFW(R_TYPE)(val)
#define ELF_R_SYM(val) ELFW(R_SYM)(val)

struct obj_string_patch;
struct obj_symbol_patch;

struct obj_section {
	ElfW(Shdr) header;
	const char *name;
	char *contents;
	struct obj_section *load_next;
	int idx;
};

struct obj_symbol {
	struct obj_symbol *next;	/* hash table link */
	const char *name;
	unsigned long value;
	unsigned long size;
	int secidx;			/* the defining section index/module */
	int info;
	int ksymidx;			/* for export to the kernel symtab */
	int referenced;		/* actually used in the link */
};

/* Hardcode the hash table size.  We shouldn't be needing so many
   symbols that we begin to degrade performance, and we get a big win
   by giving the compiler a constant divisor.  */

#define HASH_BUCKETS  521

struct obj_file {
	ElfW(Ehdr) header;
	ElfW(Addr) baseaddr;
	struct obj_section **sections;
	struct obj_section *load_order;
	struct obj_section **load_order_search_start;
	struct obj_string_patch *string_patches;
	struct obj_symbol_patch *symbol_patches;
	int (*symbol_cmp)(const char *, const char *); /* cant be FAST_FUNC */
	unsigned long (*symbol_hash)(const char *) FAST_FUNC;
	unsigned long local_symtab_size;
	struct obj_symbol **local_symtab;
	struct obj_symbol *symtab[HASH_BUCKETS];
};

enum obj_reloc {
	obj_reloc_ok,
	obj_reloc_overflow,
	obj_reloc_dangerous,
	obj_reloc_unhandled
};

struct obj_string_patch {
	struct obj_string_patch *next;
	int reloc_secidx;
	ElfW(Addr) reloc_offset;
	ElfW(Addr) string_offset;
};

struct obj_symbol_patch {
	struct obj_symbol_patch *next;
	int reloc_secidx;
	ElfW(Addr) reloc_offset;
	struct obj_symbol *sym;
};


/* Generic object manipulation routines.  */

static unsigned long FAST_FUNC obj_elf_hash(const char *);

static unsigned long obj_elf_hash_n(const char *, unsigned long len);

static struct obj_symbol *obj_find_symbol(struct obj_file *f,
		const char *name);

static ElfW(Addr) obj_symbol_final_value(struct obj_file *f,
		struct obj_symbol *sym);

#if ENABLE_FEATURE_INSMOD_VERSION_CHECKING
static void obj_set_symbol_compare(struct obj_file *f,
		int (*cmp)(const char *, const char *),
		unsigned long (*hash)(const char *) FAST_FUNC);
#endif

static struct obj_section *obj_find_section(struct obj_file *f,
		const char *name);

static void obj_insert_section_load_order(struct obj_file *f,
		struct obj_section *sec);

static struct obj_section *obj_create_alloced_section(struct obj_file *f,
		const char *name,
		unsigned long align,
		unsigned long size);

static struct obj_section *obj_create_alloced_section_first(struct obj_file *f,
		const char *name,
		unsigned long align,
		unsigned long size);

static void *obj_extend_section(struct obj_section *sec, unsigned long more);

static void obj_string_patch(struct obj_file *f, int secidx, ElfW(Addr) offset,
		const char *string);

static void obj_symbol_patch(struct obj_file *f, int secidx, ElfW(Addr) offset,
		struct obj_symbol *sym);

static void obj_check_undefineds(struct obj_file *f);

static void obj_allocate_commons(struct obj_file *f);

static unsigned long obj_load_size(struct obj_file *f);

static int obj_relocate(struct obj_file *f, ElfW(Addr) base);

#if !LOADBITS
#define obj_load(image, image_size, loadprogbits) \
	obj_load(image, image_size)
#endif
static struct obj_file *obj_load(char *image, size_t image_size, int loadprogbits);

static int obj_create_image(struct obj_file *f, char *image);

/* Architecture specific manipulation routines.  */

static struct obj_file *arch_new_file(void);

static struct obj_section *arch_new_section(void);

static struct obj_symbol *arch_new_symbol(void);

static enum obj_reloc arch_apply_relocation(struct obj_file *f,
		struct obj_section *targsec,
		/*struct obj_section *symsec,*/
		struct obj_symbol *sym,
		ElfW(RelM) *rel, ElfW(Addr) value);

static void arch_create_got(struct obj_file *f);
#if ENABLE_FEATURE_CHECK_TAINTED_MODULE
static int obj_gpl_license(struct obj_file *f, const char **license);
#endif
#endif /* obj.h */
//----------------------------------------------------------------------------
//--------end of modutils obj.h
//----------------------------------------------------------------------------


/* SPFX is always a string, so it can be concatenated to string constants.  */
#ifdef SYMBOL_PREFIX
#define SPFX	SYMBOL_PREFIX
#else
#define SPFX	""
#endif

enum { STRVERSIONLEN = 64 };

/*======================================================================*/

#define flag_force_load (option_mask32 & INSMOD_OPT_FORCE)
#define flag_autoclean (option_mask32 & INSMOD_OPT_KERNELD)
#define flag_verbose (option_mask32 & INSMOD_OPT_VERBOSE)
#define flag_quiet (option_mask32 & INSMOD_OPT_SILENT)
#define flag_noexport (option_mask32 & INSMOD_OPT_NO_EXPORT)
#define flag_print_load_map (option_mask32 & INSMOD_OPT_PRINT_MAP)

/*======================================================================*/

#if defined(USE_LIST)

struct arch_list_entry {
	struct arch_list_entry *next;
	LIST_ARCHTYPE addend;
	int offset;
	int inited : 1;
};

#endif

#if defined(USE_SINGLE)

struct arch_single_entry {
	int offset;
	int inited : 1;
	int allocated : 1;
};

#endif

#if defined(__mips__)
struct mips_hi16 {
	struct mips_hi16 *next;
	ElfW(Addr) *addr;
	ElfW(Addr) value;
};
#endif

struct arch_file {
	struct obj_file root;
#if defined(USE_PLT_ENTRIES)
	struct obj_section *plt;
#endif
#if defined(USE_GOT_ENTRIES)
	struct obj_section *got;
#endif
#if defined(__mips__)
	struct mips_hi16 *mips_hi16_list;
#endif
};

struct arch_symbol {
	struct obj_symbol root;
#if defined(USE_PLT_ENTRIES)
#if defined(USE_PLT_LIST)
	struct arch_list_entry *pltent;
#else
	struct arch_single_entry pltent;
#endif
#endif
#if defined(USE_GOT_ENTRIES)
	struct arch_single_entry gotent;
#endif
};


struct external_module {
	const char *name;
	ElfW(Addr) addr;
	int used;
	size_t nsyms;
	struct new_module_symbol *syms;
};

static struct new_module_symbol *ksyms;
static size_t nksyms;

static struct external_module *ext_modules;
static int n_ext_modules;
static int n_ext_modules_used;

/*======================================================================*/


static struct obj_file *arch_new_file(void)
{
	struct arch_file *f;
	f = xzalloc(sizeof(*f));
	return &f->root; /* it's a first member */
}

static struct obj_section *arch_new_section(void)
{
	return xzalloc(sizeof(struct obj_section));
}

static struct obj_symbol *arch_new_symbol(void)
{
	struct arch_symbol *sym;
	sym = xzalloc(sizeof(*sym));
	return &sym->root;
}

static enum obj_reloc
arch_apply_relocation(struct obj_file *f,
		struct obj_section *targsec,
		/*struct obj_section *symsec,*/
		struct obj_symbol *sym,
		ElfW(RelM) *rel, ElfW(Addr) v)
{
#if defined(__arm__) || defined(__i386__) || defined(__mc68000__) \
 || defined(__sh__) || defined(__s390__) || defined(__x86_64__) \
 || defined(__powerpc__) || defined(__mips__)
	struct arch_file *ifile = (struct arch_file *) f;
#endif
	enum obj_reloc ret = obj_reloc_ok;
	ElfW(Addr) *loc = (ElfW(Addr) *) (targsec->contents + rel->r_offset);
#if defined(__arm__) || defined(__H8300H__) || defined(__H8300S__) \
 || defined(__i386__) || defined(__mc68000__) || defined(__microblaze__) \
 || defined(__mips__) || defined(__nios2__) || defined(__powerpc__) \
 || defined(__s390__) || defined(__sh__) || defined(__x86_64__)
	ElfW(Addr) dot = targsec->header.sh_addr + rel->r_offset;
#endif
#if defined(USE_GOT_ENTRIES) || defined(USE_PLT_ENTRIES)
	struct arch_symbol *isym = (struct arch_symbol *) sym;
#endif
#if defined(__arm__) || defined(__i386__) || defined(__mc68000__) \
 || defined(__sh__) || defined(__s390__)
#if defined(USE_GOT_ENTRIES)
	ElfW(Addr) got = ifile->got ? ifile->got->header.sh_addr : 0;
#endif
#endif
#if defined(USE_PLT_ENTRIES)
	ElfW(Addr) plt = ifile->plt ? ifile->plt->header.sh_addr : 0;
	unsigned long *ip;
# if defined(USE_PLT_LIST)
	struct arch_list_entry *pe;
# else
	struct arch_single_entry *pe;
# endif
#endif

	switch (ELF_R_TYPE(rel->r_info)) {

#if defined(__arm__)

		case R_ARM_NONE:
			break;

		case R_ARM_ABS32:
			*loc += v;
			break;

		case R_ARM_GOT32:
			goto bb_use_got;

		case R_ARM_GOTPC:
			/* relative reloc, always to _GLOBAL_OFFSET_TABLE_
			 * (which is .got) similar to branch,
			 * but is full 32 bits relative */

			*loc += got - dot;
			break;

		case R_ARM_PC24:
		case R_ARM_PLT32:
			goto bb_use_plt;

		case R_ARM_GOTOFF: /* address relative to the got */
			*loc += v - got;
			break;

#elif defined(__cris__)

		case R_CRIS_NONE:
			break;

		case R_CRIS_32:
			/* CRIS keeps the relocation value in the r_addend field and
			 * should not use whats in *loc at all
			 */
			*loc = v;
			break;

#elif defined(__H8300H__) || defined(__H8300S__)

		case R_H8_DIR24R8:
			loc = (ElfW(Addr) *)((ElfW(Addr))loc - 1);
			*loc = (*loc & 0xff000000) | ((*loc & 0xffffff) + v);
			break;
		case R_H8_DIR24A8:
			*loc += v;
			break;
		case R_H8_DIR32:
		case R_H8_DIR32A16:
			*loc += v;
			break;
		case R_H8_PCREL16:
			v -= dot + 2;
			if ((ElfW(Sword))v > 0x7fff
			 || (ElfW(Sword))v < -(ElfW(Sword))0x8000
			) {
				ret = obj_reloc_overflow;
			} else {
				*(unsigned short *)loc = v;
			}
			break;
		case R_H8_PCREL8:
			v -= dot + 1;
			if ((ElfW(Sword))v > 0x7f
			 || (ElfW(Sword))v < -(ElfW(Sword))0x80
			) {
				ret = obj_reloc_overflow;
			} else {
				*(unsigned char *)loc = v;
			}
			break;

#elif defined(__i386__)

		case R_386_NONE:
			break;

		case R_386_32:
			*loc += v;
			break;

		case R_386_PLT32:
		case R_386_PC32:
		case R_386_GOTOFF:
			*loc += v - dot;
			break;

		case R_386_GLOB_DAT:
		case R_386_JMP_SLOT:
			*loc = v;
			break;

		case R_386_RELATIVE:
			*loc += f->baseaddr;
			break;

		case R_386_GOTPC:
			*loc += got - dot;
			break;

		case R_386_GOT32:
			goto bb_use_got;
			break;

#elif defined(__microblaze__)
		case R_MICROBLAZE_NONE:
		case R_MICROBLAZE_64_NONE:
		case R_MICROBLAZE_32_SYM_OP_SYM:
		case R_MICROBLAZE_32_PCREL:
			break;

		case R_MICROBLAZE_64_PCREL: {
			/* dot is the address of the current instruction.
			 * v is the target symbol address.
			 * So we need to extract the offset in the code,
			 * adding v, then subtrating the current address
			 * of this instruction.
			 * Ex: "IMM 0xFFFE  bralid 0x0000" = "bralid 0xFFFE0000"
			 */

			/* Get split offset stored in code */
			unsigned int temp = (loc[0] & 0xFFFF) << 16 |
						(loc[1] & 0xFFFF);

			/* Adjust relative offset. -4 adjustment required
			 * because dot points to the IMM insn, but branch
			 * is computed relative to the branch instruction itself.
			 */
			temp += v - dot - 4;

			/* Store back into code */
			loc[0] = (loc[0] & 0xFFFF0000) | temp >> 16;
			loc[1] = (loc[1] & 0xFFFF0000) | (temp & 0xFFFF);

			break;
		}

		case R_MICROBLAZE_32:
			*loc += v;
			break;

		case R_MICROBLAZE_64: {
			/* Get split pointer stored in code */
			unsigned int temp1 = (loc[0] & 0xFFFF) << 16 |
						(loc[1] & 0xFFFF);

			/* Add reloc offset */
			temp1+=v;

			/* Store back into code */
			loc[0] = (loc[0] & 0xFFFF0000) | temp1 >> 16;
			loc[1] = (loc[1] & 0xFFFF0000) | (temp1 & 0xFFFF);

			break;
		}

		case R_MICROBLAZE_32_PCREL_LO:
		case R_MICROBLAZE_32_LO:
		case R_MICROBLAZE_SRO32:
		case R_MICROBLAZE_SRW32:
			ret = obj_reloc_unhandled;
			break;

#elif defined(__mc68000__)

		case R_68K_NONE:
			break;

		case R_68K_32:
			*loc += v;
			break;

		case R_68K_8:
			if (v > 0xff) {
				ret = obj_reloc_overflow;
			}
			*(char *)loc = v;
			break;

		case R_68K_16:
			if (v > 0xffff) {
				ret = obj_reloc_overflow;
			}
			*(short *)loc = v;
			break;

		case R_68K_PC8:
			v -= dot;
			if ((ElfW(Sword))v > 0x7f
			 || (ElfW(Sword))v < -(ElfW(Sword))0x80
			) {
				ret = obj_reloc_overflow;
			}
			*(char *)loc = v;
			break;

		case R_68K_PC16:
			v -= dot;
			if ((ElfW(Sword))v > 0x7fff
			 || (ElfW(Sword))v < -(ElfW(Sword))0x8000
			) {
				ret = obj_reloc_overflow;
			}
			*(short *)loc = v;
			break;

		case R_68K_PC32:
			*(int *)loc = v - dot;
			break;

		case R_68K_GLOB_DAT:
		case R_68K_JMP_SLOT:
			*loc = v;
			break;

		case R_68K_RELATIVE:
			*(int *)loc += f->baseaddr;
			break;

		case R_68K_GOT32:
			goto bb_use_got;

# ifdef R_68K_GOTOFF
		case R_68K_GOTOFF:
			*loc += v - got;
			break;
# endif

#elif defined(__mips__)

		case R_MIPS_NONE:
			break;

		case R_MIPS_32:
			*loc += v;
			break;

		case R_MIPS_26:
			if (v % 4)
				ret = obj_reloc_dangerous;
			if ((v & 0xf0000000) != ((dot + 4) & 0xf0000000))
				ret = obj_reloc_overflow;
			*loc =
				(*loc & ~0x03ffffff) | ((*loc + (v >> 2)) &
										0x03ffffff);
			break;

		case R_MIPS_HI16:
			{
				struct mips_hi16 *n;

				/* We cannot relocate this one now because we don't know the value
				   of the carry we need to add.  Save the information, and let LO16
				   do the actual relocation.  */
				n = xmalloc(sizeof *n);
				n->addr = loc;
				n->value = v;
				n->next = ifile->mips_hi16_list;
				ifile->mips_hi16_list = n;
				break;
			}

		case R_MIPS_LO16:
			{
				unsigned long insnlo = *loc;
				ElfW(Addr) val, vallo;

				/* Sign extend the addend we extract from the lo insn.  */
				vallo = ((insnlo & 0xffff) ^ 0x8000) - 0x8000;

				if (ifile->mips_hi16_list != NULL) {
					struct mips_hi16 *l;

					l = ifile->mips_hi16_list;
					while (l != NULL) {
						struct mips_hi16 *next;
						unsigned long insn;

						/* Do the HI16 relocation.  Note that we actually don't
						   need to know anything about the LO16 itself, except where
						   to find the low 16 bits of the addend needed by the LO16.  */
						insn = *l->addr;
						val =
							((insn & 0xffff) << 16) +
							vallo;
						val += v;

						/* Account for the sign extension that will happen in the
						   low bits.  */
						val =
							((val >> 16) +
							 ((val & 0x8000) !=
							  0)) & 0xffff;

						insn = (insn & ~0xffff) | val;
						*l->addr = insn;

						next = l->next;
						free(l);
						l = next;
					}

					ifile->mips_hi16_list = NULL;
				}

				/* Ok, we're done with the HI16 relocs.  Now deal with the LO16.  */
				val = v + vallo;
				insnlo = (insnlo & ~0xffff) | (val & 0xffff);
				*loc = insnlo;
				break;
			}

#elif defined(__nios2__)

		case R_NIOS2_NONE:
			break;

		case R_NIOS2_BFD_RELOC_32:
			*loc += v;
			break;

		case R_NIOS2_BFD_RELOC_16:
			if (v > 0xffff) {
				ret = obj_reloc_overflow;
			}
			*(short *)loc = v;
			break;

		case R_NIOS2_BFD_RELOC_8:
			if (v > 0xff) {
				ret = obj_reloc_overflow;
			}
			*(char *)loc = v;
			break;

		case R_NIOS2_S16:
			{
				Elf32_Addr word;

				if ((Elf32_Sword)v > 0x7fff
				 || (Elf32_Sword)v < -(Elf32_Sword)0x8000
				) {
					ret = obj_reloc_overflow;
				}

				word = *loc;
				*loc = ((((word >> 22) << 16) | (v & 0xffff)) << 6) |
				       (word & 0x3f);
			}
			break;

		case R_NIOS2_U16:
			{
				Elf32_Addr word;

				if (v > 0xffff) {
					ret = obj_reloc_overflow;
				}

				word = *loc;
				*loc = ((((word >> 22) << 16) | (v & 0xffff)) << 6) |
				       (word & 0x3f);
			}
			break;

		case R_NIOS2_PCREL16:
			{
				Elf32_Addr word;

				v -= dot + 4;
				if ((Elf32_Sword)v > 0x7fff
				 || (Elf32_Sword)v < -(Elf32_Sword)0x8000
				) {
					ret = obj_reloc_overflow;
				}

				word = *loc;
				*loc = ((((word >> 22) << 16) | (v & 0xffff)) << 6) | (word & 0x3f);
			}
			break;

		case R_NIOS2_GPREL:
			{
				Elf32_Addr word, gp;
				/* get _gp */
				gp = obj_symbol_final_value(f, obj_find_symbol(f, SPFX "_gp"));
				v -= gp;
				if ((Elf32_Sword)v > 0x7fff
				 || (Elf32_Sword)v < -(Elf32_Sword)0x8000
				) {
					ret = obj_reloc_overflow;
				}

				word = *loc;
				*loc = ((((word >> 22) << 16) | (v & 0xffff)) << 6) | (word & 0x3f);
			}
			break;

		case R_NIOS2_CALL26:
			if (v & 3)
				ret = obj_reloc_dangerous;
			if ((v >> 28) != (dot >> 28))
				ret = obj_reloc_overflow;
			*loc = (*loc & 0x3f) | ((v >> 2) << 6);
			break;

		case R_NIOS2_IMM5:
			{
				Elf32_Addr word;

				if (v > 0x1f) {
					ret = obj_reloc_overflow;
				}

				word = *loc & ~0x7c0;
				*loc = word | ((v & 0x1f) << 6);
			}
			break;

		case R_NIOS2_IMM6:
			{
				Elf32_Addr word;

				if (v > 0x3f) {
					ret = obj_reloc_overflow;
				}

				word = *loc & ~0xfc0;
				*loc = word | ((v & 0x3f) << 6);
			}
			break;

		case R_NIOS2_IMM8:
			{
				Elf32_Addr word;

				if (v > 0xff) {
					ret = obj_reloc_overflow;
				}

				word = *loc & ~0x3fc0;
				*loc = word | ((v & 0xff) << 6);
			}
			break;

		case R_NIOS2_HI16:
			{
				Elf32_Addr word;

				word = *loc;
				*loc = ((((word >> 22) << 16) | ((v >>16) & 0xffff)) << 6) |
				       (word & 0x3f);
			}
			break;

		case R_NIOS2_LO16:
			{
				Elf32_Addr word;

				word = *loc;
				*loc = ((((word >> 22) << 16) | (v & 0xffff)) << 6) |
				       (word & 0x3f);
			}
			break;

		case R_NIOS2_HIADJ16:
			{
				Elf32_Addr word1, word2;

				word1 = *loc;
				word2 = ((v >> 16) + ((v >> 15) & 1)) & 0xffff;
				*loc = ((((word1 >> 22) << 16) | word2) << 6) |
				       (word1 & 0x3f);
			}
			break;

#elif defined(__powerpc64__)
		/* PPC64 needs a 2.6 kernel, 2.4 module relocation irrelevant */

#elif defined(__powerpc__)

		case R_PPC_ADDR16_HA:
			*(unsigned short *)loc = (v + 0x8000) >> 16;
			break;

		case R_PPC_ADDR16_HI:
			*(unsigned short *)loc = v >> 16;
			break;

		case R_PPC_ADDR16_LO:
			*(unsigned short *)loc = v;
			break;

		case R_PPC_REL24:
			goto bb_use_plt;

		case R_PPC_REL32:
			*loc = v - dot;
			break;

		case R_PPC_ADDR32:
			*loc = v;
			break;

#elif defined(__s390__)

		case R_390_32:
			*(unsigned int *) loc += v;
			break;
		case R_390_16:
			*(unsigned short *) loc += v;
			break;
		case R_390_8:
			*(unsigned char *) loc += v;
			break;

		case R_390_PC32:
			*(unsigned int *) loc += v - dot;
			break;
		case R_390_PC16DBL:
			*(unsigned short *) loc += (v - dot) >> 1;
			break;
		case R_390_PC16:
			*(unsigned short *) loc += v - dot;
			break;

		case R_390_PLT32:
		case R_390_PLT16DBL:
			/* find the plt entry and initialize it.  */
			pe = (struct arch_single_entry *) &isym->pltent;
			if (pe->inited == 0) {
				ip = (unsigned long *)(ifile->plt->contents + pe->offset);
				ip[0] = 0x0d105810; /* basr 1,0; lg 1,10(1); br 1 */
				ip[1] = 0x100607f1;
				if (ELF_R_TYPE(rel->r_info) == R_390_PLT16DBL)
					ip[2] = v - 2;
				else
					ip[2] = v;
				pe->inited = 1;
			}

			/* Insert relative distance to target.  */
			v = plt + pe->offset - dot;
			if (ELF_R_TYPE(rel->r_info) == R_390_PLT32)
				*(unsigned int *) loc = (unsigned int) v;
			else if (ELF_R_TYPE(rel->r_info) == R_390_PLT16DBL)
				*(unsigned short *) loc = (unsigned short) ((v + 2) >> 1);
			break;

		case R_390_GLOB_DAT:
		case R_390_JMP_SLOT:
			*loc = v;
			break;

		case R_390_RELATIVE:
			*loc += f->baseaddr;
			break;

		case R_390_GOTPC:
			*(unsigned long *) loc += got - dot;
			break;

		case R_390_GOT12:
		case R_390_GOT16:
		case R_390_GOT32:
			if (!isym->gotent.inited)
			{
				isym->gotent.inited = 1;
				*(ElfW(Addr) *)(ifile->got->contents + isym->gotent.offset) = v;
			}
			if (ELF_R_TYPE(rel->r_info) == R_390_GOT12)
				*(unsigned short *) loc |= (*(unsigned short *) loc + isym->gotent.offset) & 0xfff;
			else if (ELF_R_TYPE(rel->r_info) == R_390_GOT16)
				*(unsigned short *) loc += isym->gotent.offset;
			else if (ELF_R_TYPE(rel->r_info) == R_390_GOT32)
				*(unsigned int *) loc += isym->gotent.offset;
			break;

# ifndef R_390_GOTOFF32
#  define R_390_GOTOFF32 R_390_GOTOFF
# endif
		case R_390_GOTOFF32:
			*loc += v - got;
			break;

#elif defined(__sh__)

		case R_SH_NONE:
			break;

		case R_SH_DIR32:
			*loc += v;
			break;

		case R_SH_REL32:
			*loc += v - dot;
			break;

		case R_SH_PLT32:
			*loc = v - dot;
			break;

		case R_SH_GLOB_DAT:
		case R_SH_JMP_SLOT:
			*loc = v;
			break;

		case R_SH_RELATIVE:
			*loc = f->baseaddr + rel->r_addend;
			break;

		case R_SH_GOTPC:
			*loc = got - dot + rel->r_addend;
			break;

		case R_SH_GOT32:
			goto bb_use_got;

		case R_SH_GOTOFF:
			*loc = v - got;
			break;

# if defined(__SH5__)
		case R_SH_IMM_MEDLOW16:
		case R_SH_IMM_LOW16:
			{
				ElfW(Addr) word;

				if (ELF_R_TYPE(rel->r_info) == R_SH_IMM_MEDLOW16)
					v >>= 16;

				/*
				 *  movi and shori have the format:
				 *
				 *  |  op  | imm  | reg | reserved |
				 *   31..26 25..10 9.. 4 3   ..   0
				 *
				 * so we simply mask and or in imm.
				 */
				word = *loc & ~0x3fffc00;
				word |= (v & 0xffff) << 10;

				*loc = word;

				break;
			}

		case R_SH_IMM_MEDLOW16_PCREL:
		case R_SH_IMM_LOW16_PCREL:
			{
				ElfW(Addr) word;

				word = *loc & ~0x3fffc00;

				v -= dot;

				if (ELF_R_TYPE(rel->r_info) == R_SH_IMM_MEDLOW16_PCREL)
					v >>= 16;

				word |= (v & 0xffff) << 10;

				*loc = word;

				break;
			}
# endif /* __SH5__ */

#elif defined(__v850e__)

		case R_V850_NONE:
			break;

		case R_V850_32:
			/* We write two shorts instead of a long because even
			   32-bit insns only need half-word alignment, but
			   32-bit data needs to be long-word aligned.  */
			v += ((unsigned short *)loc)[0];
			v += ((unsigned short *)loc)[1] << 16;
			((unsigned short *)loc)[0] = v & 0xffff;
			((unsigned short *)loc)[1] = (v >> 16) & 0xffff;
			break;

		case R_V850_22_PCREL:
			goto bb_use_plt;

#elif defined(__x86_64__)

		case R_X86_64_NONE:
			break;

		case R_X86_64_64:
			*loc += v;
			break;

		case R_X86_64_32:
			*(unsigned int *) loc += v;
			if (v > 0xffffffff)
			{
				ret = obj_reloc_overflow; /* Kernel module compiled without -mcmodel=kernel. */
				/* error("Possibly is module compiled without -mcmodel=kernel!"); */
			}
			break;

		case R_X86_64_32S:
			*(signed int *) loc += v;
			break;

		case R_X86_64_16:
			*(unsigned short *) loc += v;
			break;

		case R_X86_64_8:
			*(unsigned char *) loc += v;
			break;

		case R_X86_64_PC32:
			*(unsigned int *) loc += v - dot;
			break;

		case R_X86_64_PC16:
			*(unsigned short *) loc += v - dot;
			break;

		case R_X86_64_PC8:
			*(unsigned char *) loc += v - dot;
			break;

		case R_X86_64_GLOB_DAT:
		case R_X86_64_JUMP_SLOT:
			*loc = v;
			break;

		case R_X86_64_RELATIVE:
			*loc += f->baseaddr;
			break;

		case R_X86_64_GOT32:
		case R_X86_64_GOTPCREL:
			goto bb_use_got;
# if 0
			if (!isym->gotent.reloc_done)
			{
				isym->gotent.reloc_done = 1;
				*(Elf64_Addr *)(ifile->got->contents + isym->gotent.offset) = v;
			}
			/* XXX are these really correct?  */
			if (ELF64_R_TYPE(rel->r_info) == R_X86_64_GOTPCREL)
				*(unsigned int *) loc += v + isym->gotent.offset;
			else
				*loc += isym->gotent.offset;
			break;
# endif

#else
# warning "no idea how to handle relocations on your arch"
#endif

		default:
			printf("Warning: unhandled reloc %d\n", (int)ELF_R_TYPE(rel->r_info));
			ret = obj_reloc_unhandled;
			break;

#if defined(USE_PLT_ENTRIES)

bb_use_plt:

			/* find the plt entry and initialize it if necessary */

#if defined(USE_PLT_LIST)
			for (pe = isym->pltent; pe != NULL && pe->addend != rel->r_addend;)
				pe = pe->next;
#else
			pe = &isym->pltent;
#endif

			if (! pe->inited) {
				ip = (unsigned long *) (ifile->plt->contents + pe->offset);

				/* generate some machine code */

#if defined(__arm__)
				ip[0] = 0xe51ff004;			/* ldr pc,[pc,#-4] */
				ip[1] = v;				/* sym@ */
#endif
#if defined(__powerpc__)
				ip[0] = 0x3d600000 + ((v + 0x8000) >> 16);  /* lis r11,sym@ha */
				ip[1] = 0x396b0000 + (v & 0xffff);          /* addi r11,r11,sym@l */
				ip[2] = 0x7d6903a6;			      /* mtctr r11 */
				ip[3] = 0x4e800420;			      /* bctr */
#endif
#if defined(__v850e__)
				/* We have to trash a register, so we assume that any control
				   transfer more than 21-bits away must be a function call
				   (so we can use a call-clobbered register).  */
				ip[0] = 0x0621 + ((v & 0xffff) << 16);   /* mov sym, r1 ... */
				ip[1] = ((v >> 16) & 0xffff) + 0x610000; /* ...; jmp r1 */
#endif
				pe->inited = 1;
			}

			/* relative distance to target */
			v -= dot;
			/* if the target is too far away.... */
#if defined(__arm__) || defined(__powerpc__)
			if ((int)v < -0x02000000 || (int)v >= 0x02000000)
#elif defined(__v850e__)
				if ((ElfW(Sword))v > 0x1fffff || (ElfW(Sword))v < (ElfW(Sword))-0x200000)
#endif
					/* go via the plt */
					v = plt + pe->offset - dot;

#if defined(__v850e__)
			if (v & 1)
#else
				if (v & 3)
#endif
					ret = obj_reloc_dangerous;

			/* merge the offset into the instruction. */
#if defined(__arm__)
			/* Convert to words. */
			v >>= 2;

			*loc = (*loc & ~0x00ffffff) | ((v + *loc) & 0x00ffffff);
#endif
#if defined(__powerpc__)
			*loc = (*loc & ~0x03fffffc) | (v & 0x03fffffc);
#endif
#if defined(__v850e__)
			/* We write two shorts instead of a long because even 32-bit insns
			   only need half-word alignment, but the 32-bit data write needs
			   to be long-word aligned.  */
			((unsigned short *)loc)[0] =
				(*(unsigned short *)loc & 0xffc0) /* opcode + reg */
				| ((v >> 16) & 0x3f);             /* offs high part */
			((unsigned short *)loc)[1] =
				(v & 0xffff);                    /* offs low part */
#endif
			break;
#endif /* USE_PLT_ENTRIES */

#if defined(USE_GOT_ENTRIES)
bb_use_got:

			/* needs an entry in the .got: set it, once */
			if (!isym->gotent.inited) {
				isym->gotent.inited = 1;
				*(ElfW(Addr) *) (ifile->got->contents + isym->gotent.offset) = v;
			}
			/* make the reloc with_respect_to_.got */
#if defined(__sh__)
			*loc += isym->gotent.offset + rel->r_addend;
#elif defined(__i386__) || defined(__arm__) || defined(__mc68000__)
			*loc += isym->gotent.offset;
#endif
			break;

#endif /* USE_GOT_ENTRIES */
	}

	return ret;
}


#if defined(USE_LIST)

static int arch_list_add(ElfW(RelM) *rel, struct arch_list_entry **list,
			  int offset, int size)
{
	struct arch_list_entry *pe;

	for (pe = *list; pe != NULL; pe = pe->next) {
		if (pe->addend == rel->r_addend) {
			break;
		}
	}

	if (pe == NULL) {
		pe = xzalloc(sizeof(struct arch_list_entry));
		pe->next = *list;
		pe->addend = rel->r_addend;
		pe->offset = offset;
		/*pe->inited = 0;*/
		*list = pe;
		return size;
	}
	return 0;
}

#endif

#if defined(USE_SINGLE)

static int arch_single_init(/*ElfW(RelM) *rel,*/ struct arch_single_entry *single,
		int offset, int size)
{
	if (single->allocated == 0) {
		single->allocated = 1;
		single->offset = offset;
		single->inited = 0;
		return size;
	}
	return 0;
}

#endif

#if defined(USE_GOT_ENTRIES) || defined(USE_PLT_ENTRIES)

static struct obj_section *arch_xsect_init(struct obj_file *f, const char *name,
		int offset, int size)
{
	struct obj_section *myrelsec = obj_find_section(f, name);

	if (offset == 0) {
		offset += size;
	}

	if (myrelsec) {
		obj_extend_section(myrelsec, offset);
	} else {
		myrelsec = obj_create_alloced_section(f, name,
				size, offset);
	}

	return myrelsec;
}

#endif

static void arch_create_got(struct obj_file *f)
{
#if defined(USE_GOT_ENTRIES) || defined(USE_PLT_ENTRIES)
	struct arch_file *ifile = (struct arch_file *) f;
	int i;
#if defined(USE_GOT_ENTRIES)
	int got_offset = 0, got_needed = 0, got_allocate;
#endif
#if defined(USE_PLT_ENTRIES)
	int plt_offset = 0, plt_needed = 0, plt_allocate;
#endif
	struct obj_section *relsec, *symsec, *strsec;
	ElfW(RelM) *rel, *relend;
	ElfW(Sym) *symtab, *extsym;
	const char *strtab, *name;
	struct arch_symbol *intsym;

	for (i = 0; i < f->header.e_shnum; ++i) {
		relsec = f->sections[i];
		if (relsec->header.sh_type != SHT_RELM)
			continue;

		symsec = f->sections[relsec->header.sh_link];
		strsec = f->sections[symsec->header.sh_link];

		rel = (ElfW(RelM) *) relsec->contents;
		relend = rel + (relsec->header.sh_size / sizeof(ElfW(RelM)));
		symtab = (ElfW(Sym) *) symsec->contents;
		strtab = (const char *) strsec->contents;

		for (; rel < relend; ++rel) {
			extsym = &symtab[ELF_R_SYM(rel->r_info)];

#if defined(USE_GOT_ENTRIES)
			got_allocate = 0;
#endif
#if defined(USE_PLT_ENTRIES)
			plt_allocate = 0;
#endif

			switch (ELF_R_TYPE(rel->r_info)) {
#if defined(__arm__)
			case R_ARM_PC24:
			case R_ARM_PLT32:
				plt_allocate = 1;
				break;

			case R_ARM_GOTOFF:
			case R_ARM_GOTPC:
				got_needed = 1;
				continue;

			case R_ARM_GOT32:
				got_allocate = 1;
				break;

#elif defined(__i386__)
			case R_386_GOTPC:
			case R_386_GOTOFF:
				got_needed = 1;
				continue;

			case R_386_GOT32:
				got_allocate = 1;
				break;

#elif defined(__powerpc__)
			case R_PPC_REL24:
				plt_allocate = 1;
				break;

#elif defined(__mc68000__)
			case R_68K_GOT32:
				got_allocate = 1;
				break;

#ifdef R_68K_GOTOFF
			case R_68K_GOTOFF:
				got_needed = 1;
				continue;
#endif

#elif defined(__sh__)
			case R_SH_GOT32:
				got_allocate = 1;
				break;

			case R_SH_GOTPC:
			case R_SH_GOTOFF:
				got_needed = 1;
				continue;

#elif defined(__v850e__)
			case R_V850_22_PCREL:
				plt_needed = 1;
				break;

#endif
			default:
				continue;
			}

			if (extsym->st_name != 0) {
				name = strtab + extsym->st_name;
			} else {
				name = f->sections[extsym->st_shndx]->name;
			}
			intsym = (struct arch_symbol *) obj_find_symbol(f, name);
#if defined(USE_GOT_ENTRIES)
			if (got_allocate) {
				got_offset += arch_single_init(
						/*rel,*/ &intsym->gotent,
						got_offset, GOT_ENTRY_SIZE);

				got_needed = 1;
			}
#endif
#if defined(USE_PLT_ENTRIES)
			if (plt_allocate) {
#if defined(USE_PLT_LIST)
				plt_offset += arch_list_add(
						rel, &intsym->pltent,
						plt_offset, PLT_ENTRY_SIZE);
#else
				plt_offset += arch_single_init(
						/*rel,*/ &intsym->pltent,
						plt_offset, PLT_ENTRY_SIZE);
#endif
				plt_needed = 1;
			}
#endif
		}
	}

#if defined(USE_GOT_ENTRIES)
	if (got_needed) {
		ifile->got = arch_xsect_init(f, ".got", got_offset,
				GOT_ENTRY_SIZE);
	}
#endif

#if defined(USE_PLT_ENTRIES)
	if (plt_needed) {
		ifile->plt = arch_xsect_init(f, ".plt", plt_offset,
				PLT_ENTRY_SIZE);
	}
#endif

#endif /* defined(USE_GOT_ENTRIES) || defined(USE_PLT_ENTRIES) */
}

/*======================================================================*/

/* Standard ELF hash function.  */
static unsigned long obj_elf_hash_n(const char *name, unsigned long n)
{
	unsigned long h = 0;
	unsigned long g;
	unsigned char ch;

	while (n > 0) {
		ch = *name++;
		h = (h << 4) + ch;
		g = (h & 0xf0000000);
		if (g != 0) {
			h ^= g >> 24;
			h &= ~g;
		}
		n--;
	}
	return h;
}

static unsigned long FAST_FUNC obj_elf_hash(const char *name)
{
	return obj_elf_hash_n(name, strlen(name));
}

#if ENABLE_FEATURE_INSMOD_VERSION_CHECKING
/* String comparison for non-co-versioned kernel and module.  */

static int ncv_strcmp(const char *a, const char *b)
{
	size_t alen = strlen(a), blen = strlen(b);

	if (blen == alen + 10 && b[alen] == '_' && b[alen + 1] == 'R')
		return strncmp(a, b, alen);
	else if (alen == blen + 10 && a[blen] == '_' && a[blen + 1] == 'R')
		return strncmp(a, b, blen);
	else
		return strcmp(a, b);
}

/* String hashing for non-co-versioned kernel and module.  Here
   we are simply forced to drop the crc from the hash.  */

static unsigned long FAST_FUNC ncv_symbol_hash(const char *str)
{
	size_t len = strlen(str);
	if (len > 10 && str[len - 10] == '_' && str[len - 9] == 'R')
		len -= 10;
	return obj_elf_hash_n(str, len);
}

static void
obj_set_symbol_compare(struct obj_file *f,
		int (*cmp) (const char *, const char *),
		unsigned long (*hash) (const char *) FAST_FUNC)
{
	if (cmp)
		f->symbol_cmp = cmp;
	if (hash) {
		struct obj_symbol *tmptab[HASH_BUCKETS], *sym, *next;
		int i;

		f->symbol_hash = hash;

		memcpy(tmptab, f->symtab, sizeof(tmptab));
		memset(f->symtab, 0, sizeof(f->symtab));

		for (i = 0; i < HASH_BUCKETS; ++i) {
			for (sym = tmptab[i]; sym; sym = next) {
				unsigned long h = hash(sym->name) % HASH_BUCKETS;
				next = sym->next;
				sym->next = f->symtab[h];
				f->symtab[h] = sym;
			}
		}
	}
}

#endif /* FEATURE_INSMOD_VERSION_CHECKING */

static struct obj_symbol *
obj_add_symbol(struct obj_file *f, const char *name,
		unsigned long symidx, int info,
		int secidx, ElfW(Addr) value,
		unsigned long size)
{
	struct obj_symbol *sym;
	unsigned long hash = f->symbol_hash(name) % HASH_BUCKETS;
	int n_type = ELF_ST_TYPE(info);
	int n_binding = ELF_ST_BIND(info);

	for (sym = f->symtab[hash]; sym; sym = sym->next) {
		if (f->symbol_cmp(sym->name, name) == 0) {
			int o_secidx = sym->secidx;
			int o_info = sym->info;
			int o_type = ELF_ST_TYPE(o_info);
			int o_binding = ELF_ST_BIND(o_info);

			/* A redefinition!  Is it legal?  */

			if (secidx == SHN_UNDEF)
				return sym;
			else if (o_secidx == SHN_UNDEF)
				goto found;
			else if (n_binding == STB_GLOBAL && o_binding == STB_LOCAL) {
				/* Cope with local and global symbols of the same name
				   in the same object file, as might have been created
				   by ld -r.  The only reason locals are now seen at this
				   level at all is so that we can do semi-sensible things
				   with parameters.  */

				struct obj_symbol *nsym, **p;

				nsym = arch_new_symbol();
				nsym->next = sym->next;
				nsym->ksymidx = -1;

				/* Excise the old (local) symbol from the hash chain.  */
				for (p = &f->symtab[hash]; *p != sym; p = &(*p)->next)
					continue;
				*p = sym = nsym;
				goto found;
			} else if (n_binding == STB_LOCAL) {
				/* Another symbol of the same name has already been defined.
				   Just add this to the local table.  */
				sym = arch_new_symbol();
				sym->next = NULL;
				sym->ksymidx = -1;
				f->local_symtab[symidx] = sym;
				goto found;
			} else if (n_binding == STB_WEAK)
				return sym;
			else if (o_binding == STB_WEAK)
				goto found;
			/* Don't unify COMMON symbols with object types the programmer
			   doesn't expect.  */
			else if (secidx == SHN_COMMON
					&& (o_type == STT_NOTYPE || o_type == STT_OBJECT))
				return sym;
			else if (o_secidx == SHN_COMMON
					&& (n_type == STT_NOTYPE || n_type == STT_OBJECT))
				goto found;
			else {
				/* Don't report an error if the symbol is coming from
				   the kernel or some external module.  */
				if (secidx <= SHN_HIRESERVE)
					bb_error_msg("%s multiply defined", name);
				return sym;
			}
		}
	}

	/* Completely new symbol.  */
	sym = arch_new_symbol();
	sym->next = f->symtab[hash];
	f->symtab[hash] = sym;
	sym->ksymidx = -1;
	if (ELF_ST_BIND(info) == STB_LOCAL && symidx != (unsigned long)(-1)) {
		if (symidx >= f->local_symtab_size)
			bb_error_msg("local symbol %s with index %ld exceeds local_symtab_size %ld",
					name, (long) symidx, (long) f->local_symtab_size);
		else
			f->local_symtab[symidx] = sym;
	}

found:
	sym->name = name;
	sym->value = value;
	sym->size = size;
	sym->secidx = secidx;
	sym->info = info;

	return sym;
}

static struct obj_symbol *
obj_find_symbol(struct obj_file *f, const char *name)
{
	struct obj_symbol *sym;
	unsigned long hash = f->symbol_hash(name) % HASH_BUCKETS;

	for (sym = f->symtab[hash]; sym; sym = sym->next)
		if (f->symbol_cmp(sym->name, name) == 0)
			return sym;
	return NULL;
}

static ElfW(Addr) obj_symbol_final_value(struct obj_file * f, struct obj_symbol * sym)
{
	if (sym) {
		if (sym->secidx >= SHN_LORESERVE)
			return sym->value;
		return sym->value + f->sections[sym->secidx]->header.sh_addr;
	}
	/* As a special case, a NULL sym has value zero.  */
	return 0;
}

static struct obj_section *obj_find_section(struct obj_file *f, const char *name)
{
	int i, n = f->header.e_shnum;

	for (i = 0; i < n; ++i)
		if (strcmp(f->sections[i]->name, name) == 0)
			return f->sections[i];
	return NULL;
}

static int obj_load_order_prio(struct obj_section *a)
{
	unsigned long af, ac;

	af = a->header.sh_flags;

	ac = 0;
	if (a->name[0] != '.' || strlen(a->name) != 10
	 || strcmp(a->name + 5, ".init") != 0
	) {
		ac |= 32;
	}
	if (af & SHF_ALLOC)
		ac |= 16;
	if (!(af & SHF_WRITE))
		ac |= 8;
	if (af & SHF_EXECINSTR)
		ac |= 4;
	if (a->header.sh_type != SHT_NOBITS)
		ac |= 2;

	return ac;
}

static void
obj_insert_section_load_order(struct obj_file *f, struct obj_section *sec)
{
	struct obj_section **p;
	int prio = obj_load_order_prio(sec);
	for (p = f->load_order_search_start; *p; p = &(*p)->load_next)
		if (obj_load_order_prio(*p) < prio)
			break;
	sec->load_next = *p;
	*p = sec;
}

static struct obj_section *helper_create_alloced_section(struct obj_file *f,
		const char *name,
		unsigned long align,
		unsigned long size)
{
	int newidx = f->header.e_shnum++;
	struct obj_section *sec;

	f->sections = xrealloc_vector(f->sections, 2, newidx);
	f->sections[newidx] = sec = arch_new_section();

	sec->header.sh_type = SHT_PROGBITS;
	sec->header.sh_flags = SHF_WRITE | SHF_ALLOC;
	sec->header.sh_size = size;
	sec->header.sh_addralign = align;
	sec->name = name;
	sec->idx = newidx;
	if (size)
		sec->contents = xzalloc(size);

	return sec;
}

static struct obj_section *obj_create_alloced_section(struct obj_file *f,
		const char *name,
		unsigned long align,
		unsigned long size)
{
	struct obj_section *sec;

	sec = helper_create_alloced_section(f, name, align, size);
	obj_insert_section_load_order(f, sec);
	return sec;
}

static struct obj_section *obj_create_alloced_section_first(struct obj_file *f,
		const char *name,
		unsigned long align,
		unsigned long size)
{
	struct obj_section *sec;

	sec = helper_create_alloced_section(f, name, align, size);
	sec->load_next = f->load_order;
	f->load_order = sec;
	if (f->load_order_search_start == &f->load_order)
		f->load_order_search_start = &sec->load_next;

	return sec;
}

static void *obj_extend_section(struct obj_section *sec, unsigned long more)
{
	unsigned long oldsize = sec->header.sh_size;
	if (more) {
		sec->header.sh_size += more;
		sec->contents = xrealloc(sec->contents, sec->header.sh_size);
	}
	return sec->contents + oldsize;
}


/* Conditionally add the symbols from the given symbol set to the
   new module.  */

static int add_symbols_from(struct obj_file *f,
		int idx,
		struct new_module_symbol *syms,
		size_t nsyms)
{
	struct new_module_symbol *s;
	size_t i;
	int used = 0;
#ifdef SYMBOL_PREFIX
	char *name_buf = NULL;
	size_t name_alloced_size = 0;
#endif
#if ENABLE_FEATURE_CHECK_TAINTED_MODULE
	int gpl;

	gpl = obj_gpl_license(f, NULL) == 0;
#endif
	for (i = 0, s = syms; i < nsyms; ++i, ++s) {
		/* Only add symbols that are already marked external.
		   If we override locals we may cause problems for
		   argument initialization.  We will also create a false
		   dependency on the module.  */
		struct obj_symbol *sym;
		char *name;

		/* GPL licensed modules can use symbols exported with
		 * EXPORT_SYMBOL_GPL, so ignore any GPLONLY_ prefix on the
		 * exported names.  Non-GPL modules never see any GPLONLY_
		 * symbols so they cannot fudge it by adding the prefix on
		 * their references.
		 */
		if (is_prefixed_with((char *)s->name, "GPLONLY_")) {
#if ENABLE_FEATURE_CHECK_TAINTED_MODULE
			if (gpl)
				s->name += 8;
			else
#endif
				continue;
		}
		name = (char *)s->name;

#ifdef SYMBOL_PREFIX
		/* Prepend SYMBOL_PREFIX to the symbol's name (the
		   kernel exports 'C names', but module object files
		   reference 'linker names').  */
		size_t extra = sizeof SYMBOL_PREFIX;
		size_t name_size = strlen(name) + extra;
		if (name_size > name_alloced_size) {
			name_alloced_size = name_size * 2;
			name_buf = alloca(name_alloced_size);
		}
		strcpy(name_buf, SYMBOL_PREFIX);
		strcpy(name_buf + extra - 1, name);
		name = name_buf;
#endif

		sym = obj_find_symbol(f, name);
		if (sym && !(ELF_ST_BIND(sym->info) == STB_LOCAL)) {
#ifdef SYMBOL_PREFIX
			/* Put NAME_BUF into more permanent storage.  */
			name = xmalloc(name_size);
			strcpy(name, name_buf);
#endif
			sym = obj_add_symbol(f, name, -1,
					ELF_ST_INFO(STB_GLOBAL,
						STT_NOTYPE),
					idx, s->value, 0);
			/* Did our symbol just get installed?  If so, mark the
			   module as "used".  */
			if (sym->secidx == idx)
				used = 1;
		}
	}

	return used;
}

static void add_kernel_symbols(struct obj_file *f)
{
	struct external_module *m;
	int i, nused = 0;

	/* Add module symbols first.  */

	for (i = 0, m = ext_modules; i < n_ext_modules; ++i, ++m) {
		if (m->nsyms
		 && add_symbols_from(f, SHN_HIRESERVE + 2 + i, m->syms, m->nsyms)
		) {
			m->used = 1;
			++nused;
		}
	}

	n_ext_modules_used = nused;

	/* And finally the symbols from the kernel proper.  */

	if (nksyms)
		add_symbols_from(f, SHN_HIRESERVE + 1, ksyms, nksyms);
}

static char *get_modinfo_value(struct obj_file *f, const char *key)
{
	struct obj_section *sec;
	char *p, *v, *n, *ep;
	size_t klen = strlen(key);

	sec = obj_find_section(f, ".modinfo");
	if (sec == NULL)
		return NULL;
	p = sec->contents;
	ep = p + sec->header.sh_size;
	while (p < ep) {
		v = strchr(p, '=');
		n = strchr(p, '\0');
		if (v) {
			if (p + klen == v && strncmp(p, key, klen) == 0)
				return v + 1;
		} else {
			if (p + klen == n && strcmp(p, key) == 0)
				return n;
		}
		p = n + 1;
	}

	return NULL;
}


/*======================================================================*/
/* Functions relating to module loading after 2.1.18.  */

/* From Linux-2.6 sources */
/* You can use " around spaces, but can't escape ". */
/* Hyphens and underscores equivalent in parameter names. */
static char *next_arg(char *args, char **param, char **val)
{
	unsigned int i, equals = 0;
	int in_quote = 0, quoted = 0;
	char *next;

	if (*args == '"') {
		args++;
		in_quote = 1;
		quoted = 1;
	}

	for (i = 0; args[i]; i++) {
		if (args[i] == ' ' && !in_quote)
			break;
		if (equals == 0) {
			if (args[i] == '=')
				equals = i;
		}
		if (args[i] == '"')
			in_quote = !in_quote;
	}

	*param = args;
	if (!equals)
		*val = NULL;
	else {
		args[equals] = '\0';
		*val = args + equals + 1;

		/* Don't include quotes in value. */
		if (**val == '"') {
			(*val)++;
			if (args[i-1] == '"')
				args[i-1] = '\0';
		}
		if (quoted && args[i-1] == '"')
			args[i-1] = '\0';
	}

	if (args[i]) {
		args[i] = '\0';
		next = args + i + 1;
	} else
		next = args + i;

	/* Chew up trailing spaces. */
	return skip_whitespace(next);
}

static void
new_process_module_arguments(struct obj_file *f, const char *options)
{
	char *xoptions, *pos;
	char *param, *val;

	xoptions = pos = xstrdup(skip_whitespace(options));
	while (*pos) {
		unsigned long charssize = 0;
		char *tmp, *contents, *loc, *pinfo, *p;
		struct obj_symbol *sym;
		int min, max, n, len;

		pos = next_arg(pos, &param, &val);

		tmp = xasprintf("parm_%s", param);
		pinfo = get_modinfo_value(f, tmp);
		free(tmp);
		if (pinfo == NULL)
			bb_error_msg_and_die("invalid parameter %s", param);

#ifdef SYMBOL_PREFIX
		tmp = xasprintf(SYMBOL_PREFIX "%s", param);
		sym = obj_find_symbol(f, tmp);
		free(tmp);
#else
		sym = obj_find_symbol(f, param);
#endif

		/* Also check that the parameter was not resolved from the kernel.  */
		if (sym == NULL || sym->secidx > SHN_HIRESERVE)
			bb_error_msg_and_die("symbol for parameter %s not found", param);

		/* Number of parameters */
		min = max = 1;
		if (isdigit(*pinfo)) {
			min = max = strtoul(pinfo, &pinfo, 10);
			if (*pinfo == '-')
				max = strtoul(pinfo + 1, &pinfo, 10);
		}

		contents = f->sections[sym->secidx]->contents;
		loc = contents + sym->value;

		if (*pinfo == 'c') {
			if (!isdigit(pinfo[1])) {
				bb_error_msg_and_die("parameter type 'c' for %s must be followed by"
						     " the maximum size", param);
			}
			charssize = strtoul(pinfo + 1, NULL, 10);
		}

		if (val == NULL) {
			if (*pinfo != 'b')
				bb_error_msg_and_die("argument expected for parameter %s", param);
			val = (char *) "1";
		}

		/* Parse parameter values */
		n = 0;
		p = val;
		while (*p) {
			char sv_ch;
			char *endp;

			if (++n > max)
				bb_error_msg_and_die("too many values for %s (max %d)", param, max);

			switch (*pinfo) {
			case 's':
				len = strcspn(p, ",");
				sv_ch = p[len];
				p[len] = '\0';
				obj_string_patch(f, sym->secidx,
						 loc - contents, p);
				loc += tgt_sizeof_char_p;
				p += len;
				*p = sv_ch;
				break;
			case 'c':
				len = strcspn(p, ",");
				sv_ch = p[len];
				p[len] = '\0';
				if (len >= charssize)
					bb_error_msg_and_die("string too long for %s (max %ld)", param,
							     charssize - 1);
				strcpy((char *) loc, p);
				loc += charssize;
				p += len;
				*p = sv_ch;
				break;
			case 'b':
				*loc++ = strtoul(p, &endp, 0);
				p = endp; /* gcc likes temp var for &endp */
				break;
			case 'h':
				*(short *) loc = strtoul(p, &endp, 0);
				loc += tgt_sizeof_short;
				p = endp;
				break;
			case 'i':
				*(int *) loc = strtoul(p, &endp, 0);
				loc += tgt_sizeof_int;
				p = endp;
				break;
			case 'l':
				*(long *) loc = strtoul(p, &endp, 0);
				loc += tgt_sizeof_long;
				p = endp;
				break;
			default:
				bb_error_msg_and_die("unknown parameter type '%c' for %s",
						     *pinfo, param);
			}

			p = skip_whitespace(p);
			if (*p != ',')
				break;
			p = skip_whitespace(p + 1);
		}

		if (n < min)
			bb_error_msg_and_die("parameter %s requires at least %d arguments", param, min);
		if (*p != '\0')
			bb_error_msg_and_die("invalid argument syntax for %s", param);
	}

	free(xoptions);
}

#if ENABLE_FEATURE_INSMOD_VERSION_CHECKING
static int new_is_module_checksummed(struct obj_file *f)
{
	const char *p = get_modinfo_value(f, "using_checksums");
	if (p)
		return xatoi(p);
	return 0;
}

/* Get the module's kernel version in the canonical integer form.  */

static int
new_get_module_version(struct obj_file *f, char str[STRVERSIONLEN])
{
	char *p, *q;
	int a, b, c;

	p = get_modinfo_value(f, "kernel_version");
	if (p == NULL)
		return -1;
	safe_strncpy(str, p, STRVERSIONLEN);

	a = strtoul(p, &p, 10);
	if (*p != '.')
		return -1;
	b = strtoul(p + 1, &p, 10);
	if (*p != '.')
		return -1;
	c = strtoul(p + 1, &q, 10);
	if (p + 1 == q)
		return -1;

	return a << 16 | b << 8 | c;
}

#endif   /* FEATURE_INSMOD_VERSION_CHECKING */


/* Fetch the loaded modules, and all currently exported symbols.  */

static void new_get_kernel_symbols(void)
{
	char *module_names, *mn;
	struct external_module *modules, *m;
	struct new_module_symbol *syms, *s;
	size_t ret, bufsize, nmod, nsyms, i, j;

	/* Collect the loaded modules.  */

	bufsize = 256;
	module_names = xmalloc(bufsize);

 retry_modules_load:
	if (query_module(NULL, QM_MODULES, module_names, bufsize, &ret)) {
		if (errno == ENOSPC && bufsize < ret) {
			bufsize = ret;
			module_names = xrealloc(module_names, bufsize);
			goto retry_modules_load;
		}
		bb_perror_msg_and_die("QM_MODULES");
	}

	n_ext_modules = nmod = ret;

	/* Collect the modules' symbols.  */

	if (nmod) {
		ext_modules = modules = xzalloc(nmod * sizeof(*modules));
		for (i = 0, mn = module_names, m = modules;
				i < nmod; ++i, ++m, mn += strlen(mn) + 1) {
			struct new_module_info info;

			if (query_module(mn, QM_INFO, &info, sizeof(info), &ret)) {
				if (errno == ENOENT) {
					/* The module was removed out from underneath us.  */
					continue;
				}
				bb_perror_msg_and_die("query_module: QM_INFO: %s", mn);
			}

			bufsize = 1024;
			syms = xmalloc(bufsize);
 retry_mod_sym_load:
			if (query_module(mn, QM_SYMBOLS, syms, bufsize, &ret)) {
				switch (errno) {
					case ENOSPC:
						bufsize = ret;
						syms = xrealloc(syms, bufsize);
						goto retry_mod_sym_load;
					case ENOENT:
						/* The module was removed out from underneath us.  */
						continue;
					default:
						bb_perror_msg_and_die("query_module: QM_SYMBOLS: %s", mn);
				}
			}
			nsyms = ret;

			m->name = mn;
			m->addr = info.addr;
			m->nsyms = nsyms;
			m->syms = syms;

			for (j = 0, s = syms; j < nsyms; ++j, ++s) {
				s->name += (unsigned long) syms;
			}
		}
	}

	/* Collect the kernel's symbols.  */

	bufsize = 16 * 1024;
	syms = xmalloc(bufsize);
 retry_kern_sym_load:
	if (query_module(NULL, QM_SYMBOLS, syms, bufsize, &ret)) {
		if (errno == ENOSPC && bufsize < ret) {
			bufsize = ret;
			syms = xrealloc(syms, bufsize);
			goto retry_kern_sym_load;
		}
		bb_perror_msg_and_die("kernel: QM_SYMBOLS");
	}
	nksyms = nsyms = ret;
	ksyms = syms;

	for (j = 0, s = syms; j < nsyms; ++j, ++s) {
		s->name += (unsigned long) syms;
	}
}


/* Return the kernel symbol checksum version, or zero if not used.  */

static int new_is_kernel_checksummed(void)
{
	struct new_module_symbol *s;
	size_t i;

	/* Using_Versions is not the first symbol, but it should be in there.  */

	for (i = 0, s = ksyms; i < nksyms; ++i, ++s)
		if (strcmp((char *) s->name, "Using_Versions") == 0)
			return s->value;

	return 0;
}


static void new_create_this_module(struct obj_file *f, const char *m_name)
{
	struct obj_section *sec;

	sec = obj_create_alloced_section_first(f, ".this", tgt_sizeof_long,
			sizeof(struct new_module));
	/* done by obj_create_alloced_section_first: */
	/*memset(sec->contents, 0, sizeof(struct new_module));*/

	obj_add_symbol(f, SPFX "__this_module", -1,
			ELF_ST_INFO(STB_LOCAL, STT_OBJECT), sec->idx, 0,
			sizeof(struct new_module));

	obj_string_patch(f, sec->idx, offsetof(struct new_module, name),
			m_name);
}

#if ENABLE_FEATURE_INSMOD_KSYMOOPS_SYMBOLS
/* add an entry to the __ksymtab section, creating it if necessary */
static void new_add_ksymtab(struct obj_file *f, struct obj_symbol *sym)
{
	struct obj_section *sec;
	ElfW(Addr) ofs;

	/* ensure __ksymtab is allocated, EXPORT_NOSYMBOLS creates a non-alloc section.
	 * If __ksymtab is defined but not marked alloc, x out the first character
	 * (no obj_delete routine) and create a new __ksymtab with the correct
	 * characteristics.
	 */
	sec = obj_find_section(f, "__ksymtab");
	if (sec && !(sec->header.sh_flags & SHF_ALLOC)) {
		*((char *)(sec->name)) = 'x';	/* override const */
		sec = NULL;
	}
	if (!sec)
		sec = obj_create_alloced_section(f, "__ksymtab",
				tgt_sizeof_void_p, 0);
	if (!sec)
		return;
	sec->header.sh_flags |= SHF_ALLOC;
	/* Empty section might be byte-aligned */
	sec->header.sh_addralign = tgt_sizeof_void_p;
	ofs = sec->header.sh_size;
	obj_symbol_patch(f, sec->idx, ofs, sym);
	obj_string_patch(f, sec->idx, ofs + tgt_sizeof_void_p, sym->name);
	obj_extend_section(sec, 2 * tgt_sizeof_char_p);
}
#endif /* FEATURE_INSMOD_KSYMOOPS_SYMBOLS */

static int new_create_module_ksymtab(struct obj_file *f)
{
	struct obj_section *sec;
	int i;

	/* We must always add the module references.  */

	if (n_ext_modules_used) {
		struct new_module_ref *dep;
		struct obj_symbol *tm;

		sec = obj_create_alloced_section(f, ".kmodtab", tgt_sizeof_void_p,
				(sizeof(struct new_module_ref)
				 * n_ext_modules_used));
		if (!sec)
			return 0;

		tm = obj_find_symbol(f, SPFX "__this_module");
		dep = (struct new_module_ref *) sec->contents;
		for (i = 0; i < n_ext_modules; ++i)
			if (ext_modules[i].used) {
				dep->dep = ext_modules[i].addr;
				obj_symbol_patch(f, sec->idx,
						(char *) &dep->ref - sec->contents, tm);
				dep->next_ref = 0;
				++dep;
			}
	}

	if (!flag_noexport && !obj_find_section(f, "__ksymtab")) {
		size_t nsyms;
		int *loaded;

		sec = obj_create_alloced_section(f, "__ksymtab", tgt_sizeof_void_p, 0);

		/* We don't want to export symbols residing in sections that
		   aren't loaded.  There are a number of these created so that
		   we make sure certain module options don't appear twice.  */
		i = f->header.e_shnum;
		loaded = alloca(sizeof(int) * i);
		while (--i >= 0)
			loaded[i] = (f->sections[i]->header.sh_flags & SHF_ALLOC) != 0;

		for (nsyms = i = 0; i < HASH_BUCKETS; ++i) {
			struct obj_symbol *sym;
			for (sym = f->symtab[i]; sym; sym = sym->next) {
				if (ELF_ST_BIND(sym->info) != STB_LOCAL
				 && sym->secidx <= SHN_HIRESERVE
				 && (sym->secidx >= SHN_LORESERVE || loaded[sym->secidx])
				) {
					ElfW(Addr) ofs = nsyms * 2 * tgt_sizeof_void_p;

					obj_symbol_patch(f, sec->idx, ofs, sym);
					obj_string_patch(f, sec->idx, ofs + tgt_sizeof_void_p,
							sym->name);
					nsyms++;
				}
			}
		}

		obj_extend_section(sec, nsyms * 2 * tgt_sizeof_char_p);
	}

	return 1;
}


static int
new_init_module(const char *m_name, struct obj_file *f, unsigned long m_size)
{
	struct new_module *module;
	struct obj_section *sec;
	void *image;
	int ret;
	tgt_long m_addr;

	sec = obj_find_section(f, ".this");
	if (!sec || !sec->contents) {
		bb_perror_msg_and_die("corrupt module %s?", m_name);
	}
	module = (struct new_module *) sec->contents;
	m_addr = sec->header.sh_addr;

	module->size_of_struct = sizeof(*module);
	module->size = m_size;
	module->flags = flag_autoclean ? NEW_MOD_AUTOCLEAN : 0;

	sec = obj_find_section(f, "__ksymtab");
	if (sec && sec->header.sh_size) {
		module->syms = sec->header.sh_addr;
		module->nsyms = sec->header.sh_size / (2 * tgt_sizeof_char_p);
	}

	if (n_ext_modules_used) {
		sec = obj_find_section(f, ".kmodtab");
		module->deps = sec->header.sh_addr;
		module->ndeps = n_ext_modules_used;
	}

	module->init = obj_symbol_final_value(f, obj_find_symbol(f, SPFX "init_module"));
	module->cleanup = obj_symbol_final_value(f, obj_find_symbol(f, SPFX "cleanup_module"));

	sec = obj_find_section(f, "__ex_table");
	if (sec) {
		module->ex_table_start = sec->header.sh_addr;
		module->ex_table_end = sec->header.sh_addr + sec->header.sh_size;
	}

	sec = obj_find_section(f, ".text.init");
	if (sec) {
		module->runsize = sec->header.sh_addr - m_addr;
	}
	sec = obj_find_section(f, ".data.init");
	if (sec) {
		if (!module->runsize
		 || module->runsize > sec->header.sh_addr - m_addr
		) {
			module->runsize = sec->header.sh_addr - m_addr;
		}
	}
	sec = obj_find_section(f, ARCHDATA_SEC_NAME);
	if (sec && sec->header.sh_size) {
		module->archdata_start = (void*)sec->header.sh_addr;
		module->archdata_end = module->archdata_start + sec->header.sh_size;
	}
	sec = obj_find_section(f, KALLSYMS_SEC_NAME);
	if (sec && sec->header.sh_size) {
		module->kallsyms_start = (void*)sec->header.sh_addr;
		module->kallsyms_end = module->kallsyms_start + sec->header.sh_size;
	}

	/* Whew!  All of the initialization is complete.  Collect the final
	   module image and give it to the kernel.  */

	image = xmalloc(m_size);
	obj_create_image(f, image);

	ret = init_module(m_name, (struct new_module *) image);
	if (ret)
		bb_perror_msg("init_module: %s", m_name);

	free(image);

	return ret == 0;
}


/*======================================================================*/

static void
obj_string_patch(struct obj_file *f, int secidx, ElfW(Addr) offset,
				 const char *string)
{
	struct obj_string_patch *p;
	struct obj_section *strsec;
	size_t len = strlen(string) + 1;
	char *loc;

	p = xzalloc(sizeof(*p));
	p->next = f->string_patches;
	p->reloc_secidx = secidx;
	p->reloc_offset = offset;
	f->string_patches = p;

	strsec = obj_find_section(f, ".kstrtab");
	if (strsec == NULL) {
		strsec = obj_create_alloced_section(f, ".kstrtab", 1, len);
		/*p->string_offset = 0;*/
		loc = strsec->contents;
	} else {
		p->string_offset = strsec->header.sh_size;
		loc = obj_extend_section(strsec, len);
	}
	memcpy(loc, string, len);
}

static void
obj_symbol_patch(struct obj_file *f, int secidx, ElfW(Addr) offset,
		struct obj_symbol *sym)
{
	struct obj_symbol_patch *p;

	p = xmalloc(sizeof(*p));
	p->next = f->symbol_patches;
	p->reloc_secidx = secidx;
	p->reloc_offset = offset;
	p->sym = sym;
	f->symbol_patches = p;
}

static void obj_check_undefineds(struct obj_file *f)
{
	unsigned i;

	for (i = 0; i < HASH_BUCKETS; ++i) {
		struct obj_symbol *sym;
		for (sym = f->symtab[i]; sym; sym = sym->next) {
			if (sym->secidx == SHN_UNDEF) {
				if (ELF_ST_BIND(sym->info) == STB_WEAK) {
					sym->secidx = SHN_ABS;
					sym->value = 0;
				} else {
					if (!flag_quiet)
						bb_error_msg_and_die("unresolved symbol %s", sym->name);
				}
			}
		}
	}
}

static void obj_allocate_commons(struct obj_file *f)
{
	struct common_entry {
		struct common_entry *next;
		struct obj_symbol *sym;
	} *common_head = NULL;

	unsigned long i;

	for (i = 0; i < HASH_BUCKETS; ++i) {
		struct obj_symbol *sym;
		for (sym = f->symtab[i]; sym; sym = sym->next) {
			if (sym->secidx == SHN_COMMON) {
				/* Collect all COMMON symbols and sort them by size so as to
				   minimize space wasted by alignment requirements.  */
				struct common_entry **p, *n;
				for (p = &common_head; *p; p = &(*p)->next)
					if (sym->size <= (*p)->sym->size)
						break;
				n = alloca(sizeof(*n));
				n->next = *p;
				n->sym = sym;
				*p = n;
			}
		}
	}

	for (i = 1; i < f->local_symtab_size; ++i) {
		struct obj_symbol *sym = f->local_symtab[i];
		if (sym && sym->secidx == SHN_COMMON) {
			struct common_entry **p, *n;
			for (p = &common_head; *p; p = &(*p)->next) {
				if (sym == (*p)->sym)
					break;
				if (sym->size < (*p)->sym->size) {
					n = alloca(sizeof(*n));
					n->next = *p;
					n->sym = sym;
					*p = n;
					break;
				}
			}
		}
	}

	if (common_head) {
		/* Find the bss section.  */
		for (i = 0; i < f->header.e_shnum; ++i)
			if (f->sections[i]->header.sh_type == SHT_NOBITS)
				break;

		/* If for some reason there hadn't been one, create one.  */
		if (i == f->header.e_shnum) {
			struct obj_section *sec;

			f->header.e_shnum++;
			f->sections = xrealloc_vector(f->sections, 2, i);
			f->sections[i] = sec = arch_new_section();

			sec->header.sh_type = SHT_PROGBITS;
			sec->header.sh_flags = SHF_WRITE | SHF_ALLOC;
			sec->name = ".bss";
			sec->idx = i;
		}

		/* Allocate the COMMONS.  */
		{
			ElfW(Addr) bss_size = f->sections[i]->header.sh_size;
			ElfW(Addr) max_align = f->sections[i]->header.sh_addralign;
			struct common_entry *c;

			for (c = common_head; c; c = c->next) {
				ElfW(Addr) align = c->sym->value;

				if (align > max_align)
					max_align = align;
				if (bss_size & (align - 1))
					bss_size = (bss_size | (align - 1)) + 1;

				c->sym->secidx = i;
				c->sym->value = bss_size;

				bss_size += c->sym->size;
			}

			f->sections[i]->header.sh_size = bss_size;
			f->sections[i]->header.sh_addralign = max_align;
		}
	}

	/* For the sake of patch relocation and parameter initialization,
	   allocate zeroed data for NOBITS sections now.  Note that after
	   this we cannot assume NOBITS are really empty.  */
	for (i = 0; i < f->header.e_shnum; ++i) {
		struct obj_section *s = f->sections[i];
		if (s->header.sh_type == SHT_NOBITS) {
			s->contents = NULL;
			if (s->header.sh_size != 0)
				s->contents = xzalloc(s->header.sh_size);
			s->header.sh_type = SHT_PROGBITS;
		}
	}
}

static unsigned long obj_load_size(struct obj_file *f)
{
	unsigned long dot = 0;
	struct obj_section *sec;

	/* Finalize the positions of the sections relative to one another.  */

	for (sec = f->load_order; sec; sec = sec->load_next) {
		ElfW(Addr) align;

		align = sec->header.sh_addralign;
		if (align && (dot & (align - 1)))
			dot = (dot | (align - 1)) + 1;

		sec->header.sh_addr = dot;
		dot += sec->header.sh_size;
	}

	return dot;
}

static int obj_relocate(struct obj_file *f, ElfW(Addr) base)
{
	int i, n = f->header.e_shnum;
	int ret = 1;

	/* Finalize the addresses of the sections.  */

	f->baseaddr = base;
	for (i = 0; i < n; ++i)
		f->sections[i]->header.sh_addr += base;

	/* And iterate over all of the relocations.  */

	for (i = 0; i < n; ++i) {
		struct obj_section *relsec, *symsec, *targsec, *strsec;
		ElfW(RelM) * rel, *relend;
		ElfW(Sym) * symtab;
		const char *strtab;

		relsec = f->sections[i];
		if (relsec->header.sh_type != SHT_RELM)
			continue;

		symsec = f->sections[relsec->header.sh_link];
		targsec = f->sections[relsec->header.sh_info];
		strsec = f->sections[symsec->header.sh_link];

		rel = (ElfW(RelM) *) relsec->contents;
		relend = rel + (relsec->header.sh_size / sizeof(ElfW(RelM)));
		symtab = (ElfW(Sym) *) symsec->contents;
		strtab = (const char *) strsec->contents;

		for (; rel < relend; ++rel) {
			ElfW(Addr) value = 0;
			struct obj_symbol *intsym = NULL;
			unsigned long symndx;
			ElfW(Sym) *extsym = NULL;
			const char *errmsg;

			/* Attempt to find a value to use for this relocation.  */

			symndx = ELF_R_SYM(rel->r_info);
			if (symndx) {
				/* Note we've already checked for undefined symbols.  */

				extsym = &symtab[symndx];
				if (ELF_ST_BIND(extsym->st_info) == STB_LOCAL) {
					/* Local symbols we look up in the local table to be sure
					   we get the one that is really intended.  */
					intsym = f->local_symtab[symndx];
				} else {
					/* Others we look up in the hash table.  */
					const char *name;
					if (extsym->st_name)
						name = strtab + extsym->st_name;
					else
						name = f->sections[extsym->st_shndx]->name;
					intsym = obj_find_symbol(f, name);
				}

				value = obj_symbol_final_value(f, intsym);
				intsym->referenced = 1;
			}
#if SHT_RELM == SHT_RELA
#if defined(__alpha__) && defined(AXP_BROKEN_GAS)
			/* Work around a nasty GAS bug, that is fixed as of 2.7.0.9.  */
			if (!extsym || !extsym->st_name
			 || ELF_ST_BIND(extsym->st_info) != STB_LOCAL)
#endif
				value += rel->r_addend;
#endif

			/* Do it! */
			switch (arch_apply_relocation
					(f, targsec, /*symsec,*/ intsym, rel, value)
			) {
			case obj_reloc_ok:
				break;

			case obj_reloc_overflow:
				errmsg = "Relocation overflow";
				goto bad_reloc;
			case obj_reloc_dangerous:
				errmsg = "Dangerous relocation";
				goto bad_reloc;
			case obj_reloc_unhandled:
				errmsg = "Unhandled relocation";
bad_reloc:
				if (extsym) {
					bb_error_msg("%s of type %ld for %s", errmsg,
							(long) ELF_R_TYPE(rel->r_info),
							strtab + extsym->st_name);
				} else {
					bb_error_msg("%s of type %ld", errmsg,
							(long) ELF_R_TYPE(rel->r_info));
				}
				ret = 0;
				break;
			}
		}
	}

	/* Finally, take care of the patches.  */

	if (f->string_patches) {
		struct obj_string_patch *p;
		struct obj_section *strsec;
		ElfW(Addr) strsec_base;
		strsec = obj_find_section(f, ".kstrtab");
		strsec_base = strsec->header.sh_addr;

		for (p = f->string_patches; p; p = p->next) {
			struct obj_section *targsec = f->sections[p->reloc_secidx];
			*(ElfW(Addr) *) (targsec->contents + p->reloc_offset)
				= strsec_base + p->string_offset;
		}
	}

	if (f->symbol_patches) {
		struct obj_symbol_patch *p;

		for (p = f->symbol_patches; p; p = p->next) {
			struct obj_section *targsec = f->sections[p->reloc_secidx];
			*(ElfW(Addr) *) (targsec->contents + p->reloc_offset)
				= obj_symbol_final_value(f, p->sym);
		}
	}

	return ret;
}

static int obj_create_image(struct obj_file *f, char *image)
{
	struct obj_section *sec;
	ElfW(Addr) base = f->baseaddr;

	for (sec = f->load_order; sec; sec = sec->load_next) {
		char *secimg;

		if (sec->contents == 0 || sec->header.sh_size == 0)
			continue;

		secimg = image + (sec->header.sh_addr - base);

		/* Note that we allocated data for NOBITS sections earlier.  */
		memcpy(secimg, sec->contents, sec->header.sh_size);
	}

	return 1;
}

/*======================================================================*/

static struct obj_file *obj_load(char *image, size_t image_size, int loadprogbits)
{
	typedef uint32_t aliased_uint32_t FIX_ALIASING;
#if BB_LITTLE_ENDIAN
# define ELFMAG_U32 ((uint32_t)(ELFMAG0 + 0x100 * (ELFMAG1 + (0x100 * (ELFMAG2 + 0x100 * ELFMAG3)))))
#else
# define ELFMAG_U32 ((uint32_t)((((ELFMAG0 * 0x100) + ELFMAG1) * 0x100 + ELFMAG2) * 0x100 + ELFMAG3))
#endif
	struct obj_file *f;
	ElfW(Shdr) * section_headers;
	size_t shnum, i;
	char *shstrtab;

	/* Read the file header.  */

	f = arch_new_file();
	f->symbol_cmp = strcmp;
	f->symbol_hash = obj_elf_hash;
	f->load_order_search_start = &f->load_order;

	if (image_size < sizeof(f->header))
		bb_error_msg_and_die("error while loading ELF header");
	memcpy(&f->header, image, sizeof(f->header));

	if (*(aliased_uint32_t*)(&f->header.e_ident) != ELFMAG_U32) {
		bb_error_msg_and_die("not an ELF file");
	}
	if (f->header.e_ident[EI_CLASS] != ELFCLASSM
	 || f->header.e_ident[EI_DATA] != (BB_BIG_ENDIAN ? ELFDATA2MSB : ELFDATA2LSB)
	 || f->header.e_ident[EI_VERSION] != EV_CURRENT
	 || !MATCH_MACHINE(f->header.e_machine)
	) {
		bb_error_msg_and_die("ELF file not for this architecture");
	}
	if (f->header.e_type != ET_REL) {
		bb_error_msg_and_die("ELF file not a relocatable object");
	}

	/* Read the section headers.  */

	if (f->header.e_shentsize != sizeof(ElfW(Shdr))) {
		bb_error_msg_and_die("section header size mismatch: %lu != %lu",
				(unsigned long) f->header.e_shentsize,
				(unsigned long) sizeof(ElfW(Shdr)));
	}

	shnum = f->header.e_shnum;
	/* Growth of ->sections vector will be done by
	 * xrealloc_vector(..., 2, ...), therefore we must allocate
	 * at least 2^2 = 4 extra elements here. */
	f->sections = xzalloc(sizeof(f->sections[0]) * (shnum + 4));

	section_headers = alloca(sizeof(ElfW(Shdr)) * shnum);
	if (image_size < f->header.e_shoff + sizeof(ElfW(Shdr)) * shnum)
		bb_error_msg_and_die("error while loading section headers");
	memcpy(section_headers, image + f->header.e_shoff, sizeof(ElfW(Shdr)) * shnum);

	/* Read the section data.  */

	for (i = 0; i < shnum; ++i) {
		struct obj_section *sec;

		f->sections[i] = sec = arch_new_section();

		sec->header = section_headers[i];
		sec->idx = i;

		if (sec->header.sh_size) {
			switch (sec->header.sh_type) {
			case SHT_NULL:
			case SHT_NOTE:
			case SHT_NOBITS:
				/* ignore */
				break;
			case SHT_PROGBITS:
#if LOADBITS
				if (!loadprogbits) {
					sec->contents = NULL;
					break;
				}
#endif
			case SHT_SYMTAB:
			case SHT_STRTAB:
			case SHT_RELM:
#if defined(__mips__)
			case SHT_MIPS_DWARF:
#endif
				sec->contents = NULL;
				if (sec->header.sh_size > 0) {
					sec->contents = xmalloc(sec->header.sh_size);
					if (image_size < (sec->header.sh_offset + sec->header.sh_size))
						bb_error_msg_and_die("error while loading section data");
					memcpy(sec->contents, image + sec->header.sh_offset, sec->header.sh_size);
				}
				break;
#if SHT_RELM == SHT_REL
			case SHT_RELA:
				bb_error_msg_and_die("RELA relocations not supported on this architecture");
#else
			case SHT_REL:
				bb_error_msg_and_die("REL relocations not supported on this architecture");
#endif
			default:
				if (sec->header.sh_type >= SHT_LOPROC) {
					/* Assume processor specific section types are debug
					   info and can safely be ignored.  If this is ever not
					   the case (Hello MIPS?), don't put ifdefs here but
					   create an arch_load_proc_section().  */
					break;
				}

				bb_error_msg_and_die("can't handle sections of type %ld",
						(long) sec->header.sh_type);
			}
		}
	}

	/* Do what sort of interpretation as needed by each section.  */

	shstrtab = f->sections[f->header.e_shstrndx]->contents;

	for (i = 0; i < shnum; ++i) {
		struct obj_section *sec = f->sections[i];
		sec->name = shstrtab + sec->header.sh_name;
	}

	for (i = 0; i < shnum; ++i) {
		struct obj_section *sec = f->sections[i];

		/* .modinfo should be contents only but gcc has no attribute for that.
		 * The kernel may have marked .modinfo as ALLOC, ignore this bit.
		 */
		if (strcmp(sec->name, ".modinfo") == 0)
			sec->header.sh_flags &= ~SHF_ALLOC;

		if (sec->header.sh_flags & SHF_ALLOC)
			obj_insert_section_load_order(f, sec);

		switch (sec->header.sh_type) {
		case SHT_SYMTAB:
			{
				unsigned long nsym, j;
				char *strtab;
				ElfW(Sym) * sym;

				if (sec->header.sh_entsize != sizeof(ElfW(Sym))) {
					bb_error_msg_and_die("symbol size mismatch: %lu != %lu",
							(unsigned long) sec->header.sh_entsize,
							(unsigned long) sizeof(ElfW(Sym)));
				}

				nsym = sec->header.sh_size / sizeof(ElfW(Sym));
				strtab = f->sections[sec->header.sh_link]->contents;
				sym = (ElfW(Sym) *) sec->contents;

				/* Allocate space for a table of local symbols.  */
				j = f->local_symtab_size = sec->header.sh_info;
				f->local_symtab = xzalloc(j * sizeof(struct obj_symbol *));

				/* Insert all symbols into the hash table.  */
				for (j = 1, ++sym; j < nsym; ++j, ++sym) {
					ElfW(Addr) val = sym->st_value;
					const char *name;
					if (sym->st_name)
						name = strtab + sym->st_name;
					else if (sym->st_shndx < shnum)
						name = f->sections[sym->st_shndx]->name;
					else
						continue;
#if defined(__SH5__)
					/*
					 * For sh64 it is possible that the target of a branch
					 * requires a mode switch (32 to 16 and back again).
					 *
					 * This is implied by the lsb being set in the target
					 * address for SHmedia mode and clear for SHcompact.
					 */
					val |= sym->st_other & 4;
#endif
					obj_add_symbol(f, name, j, sym->st_info, sym->st_shndx,
							val, sym->st_size);
				}
			}
			break;

		case SHT_RELM:
			if (sec->header.sh_entsize != sizeof(ElfW(RelM))) {
				bb_error_msg_and_die("relocation entry size mismatch: %lu != %lu",
						(unsigned long) sec->header.sh_entsize,
						(unsigned long) sizeof(ElfW(RelM)));
			}
			break;
			/* XXX  Relocation code from modutils-2.3.19 is not here.
			 * Why?  That's about 20 lines of code from obj/obj_load.c,
			 * which gets done in a second pass through the sections.
			 * This BusyBox insmod does similar work in obj_relocate(). */
		}
	}

	return f;
}

#if ENABLE_FEATURE_INSMOD_LOADINKMEM
/*
 * load the unloaded sections directly into the memory allocated by
 * kernel for the module
 */

static int obj_load_progbits(char *image, size_t image_size, struct obj_file *f, char *imagebase)
{
	ElfW(Addr) base = f->baseaddr;
	struct obj_section* sec;

	for (sec = f->load_order; sec; sec = sec->load_next) {
		/* section already loaded? */
		if (sec->contents != NULL)
			continue;
		if (sec->header.sh_size == 0)
			continue;
		sec->contents = imagebase + (sec->header.sh_addr - base);
		if (image_size < (sec->header.sh_offset + sec->header.sh_size)) {
			bb_error_msg("error reading ELF section data");
			return 0; /* need to delete half-loaded module! */
		}
		memcpy(sec->contents, image + sec->header.sh_offset, sec->header.sh_size);
	}
	return 1;
}
#endif

static void hide_special_symbols(struct obj_file *f)
{
	static const char *const specials[] = {
		SPFX "cleanup_module",
		SPFX "init_module",
		SPFX "kernel_version",
		NULL
	};

	struct obj_symbol *sym;
	const char *const *p;

	for (p = specials; *p; ++p) {
		sym = obj_find_symbol(f, *p);
		if (sym != NULL)
			sym->info = ELF_ST_INFO(STB_LOCAL, ELF_ST_TYPE(sym->info));
	}
}


#if ENABLE_FEATURE_CHECK_TAINTED_MODULE
static int obj_gpl_license(struct obj_file *f, const char **license)
{
	struct obj_section *sec;
	/* This list must match *exactly* the list of allowable licenses in
	 * linux/include/linux/module.h.  Checking for leading "GPL" will not
	 * work, somebody will use "GPL sucks, this is proprietary".
	 */
	static const char *const gpl_licenses[] = {
		"GPL",
		"GPL v2",
		"GPL and additional rights",
		"Dual BSD/GPL",
		"Dual MPL/GPL"
	};

	sec = obj_find_section(f, ".modinfo");
	if (sec) {
		const char *value, *ptr, *endptr;
		ptr = sec->contents;
		endptr = ptr + sec->header.sh_size;
		while (ptr < endptr) {
			value = strchr(ptr, '=');
			if (value && strncmp(ptr, "license", value-ptr) == 0) {
				unsigned i;
				if (license)
					*license = value+1;
				for (i = 0; i < ARRAY_SIZE(gpl_licenses); ++i) {
					if (strcmp(value+1, gpl_licenses[i]) == 0)
						return 0;
				}
				return 2;
			}
			ptr = strchr(ptr, '\0');
			if (ptr)
				ptr++;
			else
				ptr = endptr;
		}
	}
	return 1;
}

#define TAINT_FILENAME                  "/proc/sys/kernel/tainted"
#define TAINT_PROPRIETORY_MODULE        (1 << 0)
#define TAINT_FORCED_MODULE             (1 << 1)
#define TAINT_UNSAFE_SMP                (1 << 2)
#define TAINT_URL                       "http://www.tux.org/lkml/#export-tainted"

static void set_tainted(int fd, const char *m_name,
		int kernel_has_tainted, int taint,
		const char *text1, const char *text2)
{
	static smallint printed_info;

	char buf[80];
	int oldval;

	if (fd < 0 && !kernel_has_tainted)
		return;		/* New modutils on old kernel */
	printf("Warning: loading %s will taint the kernel: %s%s\n",
			m_name, text1, text2);
	if (!printed_info) {
		printf("  See %s for information about tainted modules\n", TAINT_URL);
		printed_info = 1;
	}
	if (fd >= 0) {
		read(fd, buf, sizeof(buf)-1);
		buf[sizeof(buf)-1] = '\0';
		oldval = strtoul(buf, NULL, 10);
		sprintf(buf, "%d\n", oldval | taint);
		xwrite_str(fd, buf);
	}
}

/* Check if loading this module will taint the kernel. */
static void check_tainted_module(struct obj_file *f, const char *m_name)
{
	int fd, kernel_has_tainted;
	const char *ptr;

	kernel_has_tainted = 1;
	fd = open(TAINT_FILENAME, O_RDWR);
	if (fd < 0) {
		if (errno == ENOENT)
			kernel_has_tainted = 0;
		else if (errno == EACCES)
			kernel_has_tainted = 1;
		else {
			bb_simple_perror_msg(TAINT_FILENAME);
			kernel_has_tainted = 0;
		}
	}

	switch (obj_gpl_license(f, &ptr)) {
		case 0:
			break;
		case 1:
			set_tainted(fd, m_name, kernel_has_tainted, TAINT_PROPRIETORY_MODULE, "no license", "");
			break;
		default: /* case 2: */
			/* The module has a non-GPL license so we pretend that the
			 * kernel always has a taint flag to get a warning even on
			 * kernels without the proc flag.
			 */
			set_tainted(fd, m_name, 1, TAINT_PROPRIETORY_MODULE, "non-GPL license - ", ptr);
			break;
	}

	if (flag_force_load)
		set_tainted(fd, m_name, 1, TAINT_FORCED_MODULE, "forced load", "");

	if (fd >= 0)
		close(fd);
}
#else /* !FEATURE_CHECK_TAINTED_MODULE */
#define check_tainted_module(x, y) do { } while (0);
#endif

#if ENABLE_FEATURE_INSMOD_KSYMOOPS_SYMBOLS
/* add module source, timestamp, kernel version and a symbol for the
 * start of some sections.  this info is used by ksymoops to do better
 * debugging.
 */
#if !ENABLE_FEATURE_INSMOD_VERSION_CHECKING
#define get_module_version(f, str) get_module_version(str)
#endif
static int
get_module_version(struct obj_file *f, char str[STRVERSIONLEN])
{
#if ENABLE_FEATURE_INSMOD_VERSION_CHECKING
	return new_get_module_version(f, str);
#else
	strncpy(str, "???", sizeof(str));
	return -1;
#endif
}

/* add module source, timestamp, kernel version and a symbol for the
 * start of some sections.  this info is used by ksymoops to do better
 * debugging.
 */
static void
add_ksymoops_symbols(struct obj_file *f, const char *filename,
		const char *m_name)
{
	static const char symprefix[] ALIGN1 = "__insmod_";
	static const char section_names[][8] = {
		".text",
		".rodata",
		".data",
		".bss",
		".sbss"
	};

	struct obj_section *sec;
	struct obj_symbol *sym;
	char *name, *absolute_filename;
	char str[STRVERSIONLEN];
	unsigned i;
	int lm_name, lfilename, use_ksymtab, version;
	struct stat statbuf;

	/* WARNING: was using realpath, but replaced by readlink to stop using
	 * lots of stack. But here it seems to be able to cause problems? */
	absolute_filename = xmalloc_readlink(filename);
	if (!absolute_filename)
		absolute_filename = xstrdup(filename);

	lm_name = strlen(m_name);
	lfilename = strlen(absolute_filename);

	/* add to ksymtab if it already exists or there is no ksymtab and other symbols
	 * are not to be exported.  otherwise leave ksymtab alone for now, the
	 * "export all symbols" compatibility code will export these symbols later.
	 */
	use_ksymtab = obj_find_section(f, "__ksymtab") || flag_noexport;

	sec = obj_find_section(f, ".this");
	if (sec) {
		/* tag the module header with the object name, last modified
		 * timestamp and module version.  worst case for module version
		 * is 0xffffff, decimal 16777215.  putting all three fields in
		 * one symbol is less readable but saves kernel space.
		 */
		if (stat(absolute_filename, &statbuf) != 0)
			statbuf.st_mtime = 0;
		version = get_module_version(f, str);	/* -1 if not found */
		name = xasprintf("%s%s_O%s_M%0*lX_V%d",
				symprefix, m_name, absolute_filename,
				(int)(2 * sizeof(statbuf.st_mtime)),
				(long)statbuf.st_mtime,
				version);
		sym = obj_add_symbol(f, name, -1,
				ELF_ST_INFO(STB_GLOBAL, STT_NOTYPE),
				sec->idx, sec->header.sh_addr, 0);
		if (use_ksymtab)
			new_add_ksymtab(f, sym);
	}
	free(absolute_filename);
#ifdef _NOT_SUPPORTED_
	/* record where the persistent data is going, same address as previous symbol */
	if (f->persist) {
		name = xasprintf("%s%s_P%s",
				symprefix, m_name, f->persist);
		sym = obj_add_symbol(f, name, -1, ELF_ST_INFO(STB_GLOBAL, STT_NOTYPE),
				sec->idx, sec->header.sh_addr, 0);
		if (use_ksymtab)
			new_add_ksymtab(f, sym);
	}
#endif
	/* tag the desired sections if size is non-zero */
	for (i = 0; i < ARRAY_SIZE(section_names); ++i) {
		sec = obj_find_section(f, section_names[i]);
		if (sec && sec->header.sh_size) {
			name = xasprintf("%s%s_S%s_L%ld",
					symprefix, m_name, sec->name,
					(long)sec->header.sh_size);
			sym = obj_add_symbol(f, name, -1, ELF_ST_INFO(STB_GLOBAL, STT_NOTYPE),
					sec->idx, sec->header.sh_addr, 0);
			if (use_ksymtab)
				new_add_ksymtab(f, sym);
		}
	}
}
#endif /* FEATURE_INSMOD_KSYMOOPS_SYMBOLS */

#if ENABLE_FEATURE_INSMOD_LOAD_MAP
static void print_load_map(struct obj_file *f)
{
	struct obj_section *sec;
#if ENABLE_FEATURE_INSMOD_LOAD_MAP_FULL
	struct obj_symbol **all, **p;
	int i, nsyms;
	char *loaded; /* array of booleans */
	struct obj_symbol *sym;
#endif
	/* Report on the section layout.  */
	printf("Sections:       Size      %-*s  Align\n",
			(int) (2 * sizeof(void *)), "Address");

	for (sec = f->load_order; sec; sec = sec->load_next) {
		int a;
		unsigned long tmp;

		for (a = -1, tmp = sec->header.sh_addralign; tmp; ++a)
			tmp >>= 1;
		if (a == -1)
			a = 0;

		printf("%-15s %08lx  %0*lx  2**%d\n",
				sec->name,
				(long)sec->header.sh_size,
				(int) (2 * sizeof(void *)),
				(long)sec->header.sh_addr,
				a);
	}
#if ENABLE_FEATURE_INSMOD_LOAD_MAP_FULL
	/* Quick reference which section indices are loaded.  */
	i = f->header.e_shnum;
	loaded = alloca(i * sizeof(loaded[0]));
	while (--i >= 0)
		loaded[i] = ((f->sections[i]->header.sh_flags & SHF_ALLOC) != 0);

	/* Collect the symbols we'll be listing.  */
	for (nsyms = i = 0; i < HASH_BUCKETS; ++i)
		for (sym = f->symtab[i]; sym; sym = sym->next)
			if (sym->secidx <= SHN_HIRESERVE
			 && (sym->secidx >= SHN_LORESERVE || loaded[sym->secidx])
			) {
				++nsyms;
			}

	all = alloca(nsyms * sizeof(all[0]));

	for (i = 0, p = all; i < HASH_BUCKETS; ++i)
		for (sym = f->symtab[i]; sym; sym = sym->next)
			if (sym->secidx <= SHN_HIRESERVE
			 && (sym->secidx >= SHN_LORESERVE || loaded[sym->secidx])
			) {
				*p++ = sym;
			}

	/* And list them.  */
	printf("\nSymbols:\n");
	for (p = all; p < all + nsyms; ++p) {
		char type = '?';
		unsigned long value;

		sym = *p;
		if (sym->secidx == SHN_ABS) {
			type = 'A';
			value = sym->value;
		} else if (sym->secidx == SHN_UNDEF) {
			type = 'U';
			value = 0;
		} else {
			sec = f->sections[sym->secidx];

			if (sec->header.sh_type == SHT_NOBITS)
				type = 'B';
			else if (sec->header.sh_flags & SHF_ALLOC) {
				if (sec->header.sh_flags & SHF_EXECINSTR)
					type = 'T';
				else if (sec->header.sh_flags & SHF_WRITE)
					type = 'D';
				else
					type = 'R';
			}
			value = sym->value + sec->header.sh_addr;
		}

		if (ELF_ST_BIND(sym->info) == STB_LOCAL)
			type |= 0x20; /* tolower. safe for '?' too */

		printf("%0*lx %c %s\n", (int) (2 * sizeof(void *)), value,
				type, sym->name);
	}
#endif
}
#else /* !FEATURE_INSMOD_LOAD_MAP */
static void print_load_map(struct obj_file *f UNUSED_PARAM)
{
}
#endif

int FAST_FUNC bb_init_module_24(const char *m_filename, const char *options)
{
	int k_crcs;
	unsigned long m_size;
	ElfW(Addr) m_addr;
	struct obj_file *f;
	int exit_status = EXIT_FAILURE;
	char *m_name;
#if ENABLE_FEATURE_INSMOD_VERSION_CHECKING
	int m_has_modinfo;
#endif
	char *image;
	size_t image_size;
	bool mmaped;

	image_size = INT_MAX - 4095;
	mmaped = 0;
	image = try_to_mmap_module(m_filename, &image_size);
	if (image) {
		mmaped = 1;
	} else {
		/* Load module into memory and unzip if compressed */
		image = xmalloc_open_zipped_read_close(m_filename, &image_size);
		if (!image)
			return EXIT_FAILURE;
	}

	m_name = xstrdup(bb_basename(m_filename));
	/* "module.o[.gz]" -> "module" */
	*strchrnul(m_name, '.') = '\0';

	f = obj_load(image, image_size, LOADBITS);

#if ENABLE_FEATURE_INSMOD_VERSION_CHECKING
	/* Version correspondence?  */
	m_has_modinfo = (get_modinfo_value(f, "kernel_version") != NULL);
	if (!flag_quiet) {
		char m_strversion[STRVERSIONLEN];
		struct utsname uts;

		if (m_has_modinfo) {
			int m_version = new_get_module_version(f, m_strversion);
			if (m_version == -1) {
				bb_error_msg_and_die("can't find the kernel version "
					"the module was compiled for");
			}
		}

		uname(&uts);
		if (strncmp(uts.release, m_strversion, STRVERSIONLEN) != 0) {
			bb_error_msg("%skernel-module version mismatch\n"
				"\t%s was compiled for kernel version %s\n"
				"\twhile this kernel is version %s",
				flag_force_load ? "warning: " : "",
				m_name, m_strversion, uts.release);
			if (!flag_force_load)
				goto out;
		}
	}
#endif

	if (query_module(NULL, 0, NULL, 0, NULL))
		bb_error_msg_and_die("old (unsupported) kernel");
	new_get_kernel_symbols();
	k_crcs = new_is_kernel_checksummed();

#if ENABLE_FEATURE_INSMOD_VERSION_CHECKING
	{
		int m_crcs = 0;
		if (m_has_modinfo)
			m_crcs = new_is_module_checksummed(f);
		if (m_crcs != k_crcs)
			obj_set_symbol_compare(f, ncv_strcmp, ncv_symbol_hash);
	}
#endif

	/* Let the module know about the kernel symbols.  */
	add_kernel_symbols(f);

	/* Allocate common symbols, symbol tables, and string tables.  */
	new_create_this_module(f, m_name);
	obj_check_undefineds(f);
	obj_allocate_commons(f);
	check_tainted_module(f, m_name);

	/* Done with the module name, on to the optional var=value arguments */
	new_process_module_arguments(f, options);

	arch_create_got(f);
	hide_special_symbols(f);

#if ENABLE_FEATURE_INSMOD_KSYMOOPS_SYMBOLS
	add_ksymoops_symbols(f, m_filename, m_name);
#endif

	new_create_module_ksymtab(f);

	/* Find current size of the module */
	m_size = obj_load_size(f);

	m_addr = create_module(m_name, m_size);
	if (m_addr == (ElfW(Addr))(-1)) switch (errno) {
	case EEXIST:
		bb_error_msg_and_die("a module named %s already exists", m_name);
	case ENOMEM:
		bb_error_msg_and_die("can't allocate kernel memory for module; needed %lu bytes",
				m_size);
	default:
		bb_perror_msg_and_die("create_module: %s", m_name);
	}

#if !LOADBITS
	/*
	 * the PROGBITS section was not loaded by the obj_load
	 * now we can load them directly into the kernel memory
	 */
	if (!obj_load_progbits(image, image_size, f, (char*)m_addr)) {
		delete_module(m_name, 0);
		goto out;
	}
#endif

	if (!obj_relocate(f, m_addr)) {
		delete_module(m_name, 0);
		goto out;
	}

	if (!new_init_module(m_name, f, m_size)) {
		delete_module(m_name, 0);
		goto out;
	}

	if (flag_print_load_map)
		print_load_map(f);

	exit_status = EXIT_SUCCESS;

 out:
	if (mmaped)
		munmap(image, image_size);
	else
		free(image);
	free(m_name);

	return exit_status;
}
