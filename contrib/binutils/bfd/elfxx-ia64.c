/* IA-64 support for 64-bit ELF
   Copyright 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.
   Contributed by David Mosberger-Tang <davidm@hpl.hp.com>

   This file is part of BFD, the Binary File Descriptor library.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "elf-bfd.h"
#include "opcode/ia64.h"
#include "elf/ia64.h"
#include "objalloc.h"
#include "hashtab.h"

#define ARCH_SIZE	NN

#if ARCH_SIZE == 64
#define	LOG_SECTION_ALIGN	3
#endif

#if ARCH_SIZE == 32
#define	LOG_SECTION_ALIGN	2
#endif

/* THE RULES for all the stuff the linker creates --

  GOT		Entries created in response to LTOFF or LTOFF_FPTR
 		relocations.  Dynamic relocs created for dynamic
 		symbols in an application; REL relocs for locals
 		in a shared library.

  FPTR		The canonical function descriptor.  Created for local
 		symbols in applications.  Descriptors for dynamic symbols
 		and local symbols in shared libraries are created by
 		ld.so.  Thus there are no dynamic relocs against these
 		objects.  The FPTR relocs for such _are_ passed through
 		to the dynamic relocation tables.

  FULL_PLT	Created for a PCREL21B relocation against a dynamic symbol.
 		Requires the creation of a PLTOFF entry.  This does not
 		require any dynamic relocations.

  PLTOFF	Created by PLTOFF relocations.  For local symbols, this
 		is an alternate function descriptor, and in shared libraries
 		requires two REL relocations.  Note that this cannot be
 		transformed into an FPTR relocation, since it must be in
 		range of the GP.  For dynamic symbols, this is a function
 		descriptor for a MIN_PLT entry, and requires one IPLT reloc.

  MIN_PLT	Created by PLTOFF entries against dynamic symbols.  This
 		does not require dynamic relocations.  */

#define NELEMS(a)	((int) (sizeof (a) / sizeof ((a)[0])))

typedef struct bfd_hash_entry *(*new_hash_entry_func)
  PARAMS ((struct bfd_hash_entry *, struct bfd_hash_table *, const char *));

/* In dynamically (linker-) created sections, we generally need to keep track
   of the place a symbol or expression got allocated to. This is done via hash
   tables that store entries of the following type.  */

struct elfNN_ia64_dyn_sym_info
{
  /* The addend for which this entry is relevant.  */
  bfd_vma addend;

  bfd_vma got_offset;
  bfd_vma fptr_offset;
  bfd_vma pltoff_offset;
  bfd_vma plt_offset;
  bfd_vma plt2_offset;
  bfd_vma tprel_offset;
  bfd_vma dtpmod_offset;
  bfd_vma dtprel_offset;

  /* The symbol table entry, if any, that this was derived from.  */
  struct elf_link_hash_entry *h;

  /* Used to count non-got, non-plt relocations for delayed sizing
     of relocation sections.  */
  struct elfNN_ia64_dyn_reloc_entry
  {
    struct elfNN_ia64_dyn_reloc_entry *next;
    asection *srel;
    int type;
    int count;

    /* Is this reloc against readonly section? */
    bfd_boolean reltext;
  } *reloc_entries;

  /* TRUE when the section contents have been updated.  */
  unsigned got_done : 1;
  unsigned fptr_done : 1;
  unsigned pltoff_done : 1;
  unsigned tprel_done : 1;
  unsigned dtpmod_done : 1;
  unsigned dtprel_done : 1;

  /* TRUE for the different kinds of linker data we want created.  */
  unsigned want_got : 1;
  unsigned want_gotx : 1;
  unsigned want_fptr : 1;
  unsigned want_ltoff_fptr : 1;
  unsigned want_plt : 1;
  unsigned want_plt2 : 1;
  unsigned want_pltoff : 1;
  unsigned want_tprel : 1;
  unsigned want_dtpmod : 1;
  unsigned want_dtprel : 1;
};

struct elfNN_ia64_local_hash_entry
{
  int id;
  unsigned int r_sym;
  /* The number of elements in elfNN_ia64_dyn_sym_info array.  */
  unsigned int count;
  /* The number of sorted elements in elfNN_ia64_dyn_sym_info array.  */
  unsigned int sorted_count;
  /* The size of elfNN_ia64_dyn_sym_info array.  */
  unsigned int size;
  /* The array of elfNN_ia64_dyn_sym_info.  */
  struct elfNN_ia64_dyn_sym_info *info;

  /* TRUE if this hash entry's addends was translated for
     SHF_MERGE optimization.  */
  unsigned sec_merge_done : 1;
};

struct elfNN_ia64_link_hash_entry
{
  struct elf_link_hash_entry root;
  /* The number of elements in elfNN_ia64_dyn_sym_info array.  */
  unsigned int count;
  /* The number of sorted elements in elfNN_ia64_dyn_sym_info array.  */
  unsigned int sorted_count;
  /* The size of elfNN_ia64_dyn_sym_info array.  */
  unsigned int size;
  /* The array of elfNN_ia64_dyn_sym_info.  */
  struct elfNN_ia64_dyn_sym_info *info;
};

struct elfNN_ia64_link_hash_table
{
  /* The main hash table.  */
  struct elf_link_hash_table root;

  asection *got_sec;		/* the linkage table section (or NULL) */
  asection *rel_got_sec;	/* dynamic relocation section for same */
  asection *fptr_sec;		/* function descriptor table (or NULL) */
  asection *rel_fptr_sec;	/* dynamic relocation section for same */
  asection *plt_sec;		/* the primary plt section (or NULL) */
  asection *pltoff_sec;		/* private descriptors for plt (or NULL) */
  asection *rel_pltoff_sec;	/* dynamic relocation section for same */

  bfd_size_type minplt_entries;	/* number of minplt entries */
  unsigned reltext : 1;		/* are there relocs against readonly sections? */
  unsigned self_dtpmod_done : 1;/* has self DTPMOD entry been finished? */
  bfd_vma self_dtpmod_offset;	/* .got offset to self DTPMOD entry */

  htab_t loc_hash_table;
  void *loc_hash_memory;
};

struct elfNN_ia64_allocate_data
{
  struct bfd_link_info *info;
  bfd_size_type ofs;
  bfd_boolean only_got;
};

#define elfNN_ia64_hash_table(p) \
  ((struct elfNN_ia64_link_hash_table *) ((p)->hash))

static bfd_reloc_status_type elfNN_ia64_reloc
  PARAMS ((bfd *abfd, arelent *reloc, asymbol *sym, PTR data,
	   asection *input_section, bfd *output_bfd, char **error_message));
static reloc_howto_type * lookup_howto
  PARAMS ((unsigned int rtype));
static reloc_howto_type *elfNN_ia64_reloc_type_lookup
  PARAMS ((bfd *abfd, bfd_reloc_code_real_type bfd_code));
static void elfNN_ia64_info_to_howto
  PARAMS ((bfd *abfd, arelent *bfd_reloc, Elf_Internal_Rela *elf_reloc));
static bfd_boolean elfNN_ia64_relax_section
  PARAMS((bfd *abfd, asection *sec, struct bfd_link_info *link_info,
	  bfd_boolean *again));
static void elfNN_ia64_relax_ldxmov
  PARAMS((bfd_byte *contents, bfd_vma off));
static bfd_boolean is_unwind_section_name
  PARAMS ((bfd *abfd, const char *));
static bfd_boolean elfNN_ia64_section_flags
  PARAMS ((flagword *, const Elf_Internal_Shdr *));
static bfd_boolean elfNN_ia64_fake_sections
  PARAMS ((bfd *abfd, Elf_Internal_Shdr *hdr, asection *sec));
static void elfNN_ia64_final_write_processing
  PARAMS ((bfd *abfd, bfd_boolean linker));
static bfd_boolean elfNN_ia64_add_symbol_hook
  PARAMS ((bfd *abfd, struct bfd_link_info *info, Elf_Internal_Sym *sym,
	   const char **namep, flagword *flagsp, asection **secp,
	   bfd_vma *valp));
static bfd_boolean elfNN_ia64_is_local_label_name
  PARAMS ((bfd *abfd, const char *name));
static bfd_boolean elfNN_ia64_dynamic_symbol_p
  PARAMS ((struct elf_link_hash_entry *h, struct bfd_link_info *info, int));
static struct bfd_hash_entry *elfNN_ia64_new_elf_hash_entry
  PARAMS ((struct bfd_hash_entry *entry, struct bfd_hash_table *table,
	   const char *string));
static void elfNN_ia64_hash_copy_indirect
  PARAMS ((struct bfd_link_info *, struct elf_link_hash_entry *,
	   struct elf_link_hash_entry *));
static void elfNN_ia64_hash_hide_symbol
  PARAMS ((struct bfd_link_info *, struct elf_link_hash_entry *, bfd_boolean));
static hashval_t elfNN_ia64_local_htab_hash PARAMS ((const void *));
static int elfNN_ia64_local_htab_eq PARAMS ((const void *ptr1,
					     const void *ptr2));
static struct bfd_link_hash_table *elfNN_ia64_hash_table_create
  PARAMS ((bfd *abfd));
static void elfNN_ia64_hash_table_free
  PARAMS ((struct bfd_link_hash_table *hash));
static bfd_boolean elfNN_ia64_global_dyn_sym_thunk
  PARAMS ((struct bfd_hash_entry *, PTR));
static int elfNN_ia64_local_dyn_sym_thunk
  PARAMS ((void **, PTR));
static void elfNN_ia64_dyn_sym_traverse
  PARAMS ((struct elfNN_ia64_link_hash_table *ia64_info,
	   bfd_boolean (*func) (struct elfNN_ia64_dyn_sym_info *, PTR),
	   PTR info));
static bfd_boolean elfNN_ia64_create_dynamic_sections
  PARAMS ((bfd *abfd, struct bfd_link_info *info));
static struct elfNN_ia64_local_hash_entry * get_local_sym_hash
  PARAMS ((struct elfNN_ia64_link_hash_table *ia64_info,
	   bfd *abfd, const Elf_Internal_Rela *rel, bfd_boolean create));
static struct elfNN_ia64_dyn_sym_info * get_dyn_sym_info
  PARAMS ((struct elfNN_ia64_link_hash_table *ia64_info,
	   struct elf_link_hash_entry *h,
	   bfd *abfd, const Elf_Internal_Rela *rel, bfd_boolean create));
static asection *get_got
  PARAMS ((bfd *abfd, struct bfd_link_info *info,
	   struct elfNN_ia64_link_hash_table *ia64_info));
static asection *get_fptr
  PARAMS ((bfd *abfd, struct bfd_link_info *info,
	   struct elfNN_ia64_link_hash_table *ia64_info));
static asection *get_pltoff
  PARAMS ((bfd *abfd, struct bfd_link_info *info,
	   struct elfNN_ia64_link_hash_table *ia64_info));
static asection *get_reloc_section
  PARAMS ((bfd *abfd, struct elfNN_ia64_link_hash_table *ia64_info,
	   asection *sec, bfd_boolean create));
static bfd_boolean elfNN_ia64_check_relocs
  PARAMS ((bfd *abfd, struct bfd_link_info *info, asection *sec,
	   const Elf_Internal_Rela *relocs));
static bfd_boolean elfNN_ia64_adjust_dynamic_symbol
  PARAMS ((struct bfd_link_info *info, struct elf_link_hash_entry *h));
static long global_sym_index
  PARAMS ((struct elf_link_hash_entry *h));
static bfd_boolean allocate_fptr
  PARAMS ((struct elfNN_ia64_dyn_sym_info *dyn_i, PTR data));
static bfd_boolean allocate_global_data_got
  PARAMS ((struct elfNN_ia64_dyn_sym_info *dyn_i, PTR data));
static bfd_boolean allocate_global_fptr_got
  PARAMS ((struct elfNN_ia64_dyn_sym_info *dyn_i, PTR data));
static bfd_boolean allocate_local_got
  PARAMS ((struct elfNN_ia64_dyn_sym_info *dyn_i, PTR data));
static bfd_boolean allocate_pltoff_entries
  PARAMS ((struct elfNN_ia64_dyn_sym_info *dyn_i, PTR data));
static bfd_boolean allocate_plt_entries
  PARAMS ((struct elfNN_ia64_dyn_sym_info *dyn_i, PTR data));
static bfd_boolean allocate_plt2_entries
  PARAMS ((struct elfNN_ia64_dyn_sym_info *dyn_i, PTR data));
static bfd_boolean allocate_dynrel_entries
  PARAMS ((struct elfNN_ia64_dyn_sym_info *dyn_i, PTR data));
static bfd_boolean elfNN_ia64_size_dynamic_sections
  PARAMS ((bfd *output_bfd, struct bfd_link_info *info));
static bfd_reloc_status_type elfNN_ia64_install_value
  PARAMS ((bfd_byte *hit_addr, bfd_vma val, unsigned int r_type));
static void elfNN_ia64_install_dyn_reloc
  PARAMS ((bfd *abfd, struct bfd_link_info *info, asection *sec,
	   asection *srel, bfd_vma offset, unsigned int type,
	   long dynindx, bfd_vma addend));
static bfd_vma set_got_entry
  PARAMS ((bfd *abfd, struct bfd_link_info *info,
	   struct elfNN_ia64_dyn_sym_info *dyn_i, long dynindx,
	   bfd_vma addend, bfd_vma value, unsigned int dyn_r_type));
static bfd_vma set_fptr_entry
  PARAMS ((bfd *abfd, struct bfd_link_info *info,
	   struct elfNN_ia64_dyn_sym_info *dyn_i,
	   bfd_vma value));
static bfd_vma set_pltoff_entry
  PARAMS ((bfd *abfd, struct bfd_link_info *info,
	   struct elfNN_ia64_dyn_sym_info *dyn_i,
	   bfd_vma value, bfd_boolean));
static bfd_vma elfNN_ia64_tprel_base
  PARAMS ((struct bfd_link_info *info));
static bfd_vma elfNN_ia64_dtprel_base
  PARAMS ((struct bfd_link_info *info));
static int elfNN_ia64_unwind_entry_compare
  PARAMS ((const PTR, const PTR));
static bfd_boolean elfNN_ia64_choose_gp
  PARAMS ((bfd *abfd, struct bfd_link_info *info));
static bfd_boolean elfNN_ia64_final_link
  PARAMS ((bfd *abfd, struct bfd_link_info *info));
static bfd_boolean elfNN_ia64_relocate_section
  PARAMS ((bfd *output_bfd, struct bfd_link_info *info, bfd *input_bfd,
	   asection *input_section, bfd_byte *contents,
	   Elf_Internal_Rela *relocs, Elf_Internal_Sym *local_syms,
	   asection **local_sections));
static bfd_boolean elfNN_ia64_finish_dynamic_symbol
  PARAMS ((bfd *output_bfd, struct bfd_link_info *info,
	   struct elf_link_hash_entry *h, Elf_Internal_Sym *sym));
static bfd_boolean elfNN_ia64_finish_dynamic_sections
  PARAMS ((bfd *abfd, struct bfd_link_info *info));
static bfd_boolean elfNN_ia64_set_private_flags
  PARAMS ((bfd *abfd, flagword flags));
static bfd_boolean elfNN_ia64_merge_private_bfd_data
  PARAMS ((bfd *ibfd, bfd *obfd));
static bfd_boolean elfNN_ia64_print_private_bfd_data
  PARAMS ((bfd *abfd, PTR ptr));
static enum elf_reloc_type_class elfNN_ia64_reloc_type_class
  PARAMS ((const Elf_Internal_Rela *));
static bfd_boolean elfNN_ia64_hpux_vec
  PARAMS ((const bfd_target *vec));
static void elfNN_hpux_post_process_headers
  PARAMS ((bfd *abfd, struct bfd_link_info *info));
bfd_boolean elfNN_hpux_backend_section_from_bfd_section
  PARAMS ((bfd *abfd, asection *sec, int *retval));

/* ia64-specific relocation.  */

/* Perform a relocation.  Not much to do here as all the hard work is
   done in elfNN_ia64_final_link_relocate.  */
static bfd_reloc_status_type
elfNN_ia64_reloc (abfd, reloc, sym, data, input_section,
		  output_bfd, error_message)
     bfd *abfd ATTRIBUTE_UNUSED;
     arelent *reloc;
     asymbol *sym ATTRIBUTE_UNUSED;
     PTR data ATTRIBUTE_UNUSED;
     asection *input_section;
     bfd *output_bfd;
     char **error_message;
{
  if (output_bfd)
    {
      reloc->address += input_section->output_offset;
      return bfd_reloc_ok;
    }

  if (input_section->flags & SEC_DEBUGGING)
    return bfd_reloc_continue;

  *error_message = "Unsupported call to elfNN_ia64_reloc";
  return bfd_reloc_notsupported;
}

#define IA64_HOWTO(TYPE, NAME, SIZE, PCREL, IN)			\
  HOWTO (TYPE, 0, SIZE, 0, PCREL, 0, complain_overflow_signed,	\
	 elfNN_ia64_reloc, NAME, FALSE, 0, -1, IN)

/* This table has to be sorted according to increasing number of the
   TYPE field.  */
static reloc_howto_type ia64_howto_table[] =
  {
    IA64_HOWTO (R_IA64_NONE,	    "NONE",	   0, FALSE, TRUE),

    IA64_HOWTO (R_IA64_IMM14,	    "IMM14",	   0, FALSE, TRUE),
    IA64_HOWTO (R_IA64_IMM22,	    "IMM22",	   0, FALSE, TRUE),
    IA64_HOWTO (R_IA64_IMM64,	    "IMM64",	   0, FALSE, TRUE),
    IA64_HOWTO (R_IA64_DIR32MSB,    "DIR32MSB",	   2, FALSE, TRUE),
    IA64_HOWTO (R_IA64_DIR32LSB,    "DIR32LSB",	   2, FALSE, TRUE),
    IA64_HOWTO (R_IA64_DIR64MSB,    "DIR64MSB",	   4, FALSE, TRUE),
    IA64_HOWTO (R_IA64_DIR64LSB,    "DIR64LSB",	   4, FALSE, TRUE),

    IA64_HOWTO (R_IA64_GPREL22,	    "GPREL22",	   0, FALSE, TRUE),
    IA64_HOWTO (R_IA64_GPREL64I,    "GPREL64I",	   0, FALSE, TRUE),
    IA64_HOWTO (R_IA64_GPREL32MSB,  "GPREL32MSB",  2, FALSE, TRUE),
    IA64_HOWTO (R_IA64_GPREL32LSB,  "GPREL32LSB",  2, FALSE, TRUE),
    IA64_HOWTO (R_IA64_GPREL64MSB,  "GPREL64MSB",  4, FALSE, TRUE),
    IA64_HOWTO (R_IA64_GPREL64LSB,  "GPREL64LSB",  4, FALSE, TRUE),

    IA64_HOWTO (R_IA64_LTOFF22,	    "LTOFF22",	   0, FALSE, TRUE),
    IA64_HOWTO (R_IA64_LTOFF64I,    "LTOFF64I",	   0, FALSE, TRUE),

    IA64_HOWTO (R_IA64_PLTOFF22,    "PLTOFF22",	   0, FALSE, TRUE),
    IA64_HOWTO (R_IA64_PLTOFF64I,   "PLTOFF64I",   0, FALSE, TRUE),
    IA64_HOWTO (R_IA64_PLTOFF64MSB, "PLTOFF64MSB", 4, FALSE, TRUE),
    IA64_HOWTO (R_IA64_PLTOFF64LSB, "PLTOFF64LSB", 4, FALSE, TRUE),

    IA64_HOWTO (R_IA64_FPTR64I,	    "FPTR64I",	   0, FALSE, TRUE),
    IA64_HOWTO (R_IA64_FPTR32MSB,   "FPTR32MSB",   2, FALSE, TRUE),
    IA64_HOWTO (R_IA64_FPTR32LSB,   "FPTR32LSB",   2, FALSE, TRUE),
    IA64_HOWTO (R_IA64_FPTR64MSB,   "FPTR64MSB",   4, FALSE, TRUE),
    IA64_HOWTO (R_IA64_FPTR64LSB,   "FPTR64LSB",   4, FALSE, TRUE),

    IA64_HOWTO (R_IA64_PCREL60B,    "PCREL60B",	   0, TRUE, TRUE),
    IA64_HOWTO (R_IA64_PCREL21B,    "PCREL21B",	   0, TRUE, TRUE),
    IA64_HOWTO (R_IA64_PCREL21M,    "PCREL21M",	   0, TRUE, TRUE),
    IA64_HOWTO (R_IA64_PCREL21F,    "PCREL21F",	   0, TRUE, TRUE),
    IA64_HOWTO (R_IA64_PCREL32MSB,  "PCREL32MSB",  2, TRUE, TRUE),
    IA64_HOWTO (R_IA64_PCREL32LSB,  "PCREL32LSB",  2, TRUE, TRUE),
    IA64_HOWTO (R_IA64_PCREL64MSB,  "PCREL64MSB",  4, TRUE, TRUE),
    IA64_HOWTO (R_IA64_PCREL64LSB,  "PCREL64LSB",  4, TRUE, TRUE),

    IA64_HOWTO (R_IA64_LTOFF_FPTR22, "LTOFF_FPTR22", 0, FALSE, TRUE),
    IA64_HOWTO (R_IA64_LTOFF_FPTR64I, "LTOFF_FPTR64I", 0, FALSE, TRUE),
    IA64_HOWTO (R_IA64_LTOFF_FPTR32MSB, "LTOFF_FPTR32MSB", 2, FALSE, TRUE),
    IA64_HOWTO (R_IA64_LTOFF_FPTR32LSB, "LTOFF_FPTR32LSB", 2, FALSE, TRUE),
    IA64_HOWTO (R_IA64_LTOFF_FPTR64MSB, "LTOFF_FPTR64MSB", 4, FALSE, TRUE),
    IA64_HOWTO (R_IA64_LTOFF_FPTR64LSB, "LTOFF_FPTR64LSB", 4, FALSE, TRUE),

    IA64_HOWTO (R_IA64_SEGREL32MSB, "SEGREL32MSB", 2, FALSE, TRUE),
    IA64_HOWTO (R_IA64_SEGREL32LSB, "SEGREL32LSB", 2, FALSE, TRUE),
    IA64_HOWTO (R_IA64_SEGREL64MSB, "SEGREL64MSB", 4, FALSE, TRUE),
    IA64_HOWTO (R_IA64_SEGREL64LSB, "SEGREL64LSB", 4, FALSE, TRUE),

    IA64_HOWTO (R_IA64_SECREL32MSB, "SECREL32MSB", 2, FALSE, TRUE),
    IA64_HOWTO (R_IA64_SECREL32LSB, "SECREL32LSB", 2, FALSE, TRUE),
    IA64_HOWTO (R_IA64_SECREL64MSB, "SECREL64MSB", 4, FALSE, TRUE),
    IA64_HOWTO (R_IA64_SECREL64LSB, "SECREL64LSB", 4, FALSE, TRUE),

    IA64_HOWTO (R_IA64_REL32MSB,    "REL32MSB",	   2, FALSE, TRUE),
    IA64_HOWTO (R_IA64_REL32LSB,    "REL32LSB",	   2, FALSE, TRUE),
    IA64_HOWTO (R_IA64_REL64MSB,    "REL64MSB",	   4, FALSE, TRUE),
    IA64_HOWTO (R_IA64_REL64LSB,    "REL64LSB",	   4, FALSE, TRUE),

    IA64_HOWTO (R_IA64_LTV32MSB,    "LTV32MSB",	   2, FALSE, TRUE),
    IA64_HOWTO (R_IA64_LTV32LSB,    "LTV32LSB",	   2, FALSE, TRUE),
    IA64_HOWTO (R_IA64_LTV64MSB,    "LTV64MSB",	   4, FALSE, TRUE),
    IA64_HOWTO (R_IA64_LTV64LSB,    "LTV64LSB",	   4, FALSE, TRUE),

    IA64_HOWTO (R_IA64_PCREL21BI,   "PCREL21BI",   0, TRUE, TRUE),
    IA64_HOWTO (R_IA64_PCREL22,     "PCREL22",     0, TRUE, TRUE),
    IA64_HOWTO (R_IA64_PCREL64I,    "PCREL64I",    0, TRUE, TRUE),

    IA64_HOWTO (R_IA64_IPLTMSB,	    "IPLTMSB",	   4, FALSE, TRUE),
    IA64_HOWTO (R_IA64_IPLTLSB,	    "IPLTLSB",	   4, FALSE, TRUE),
    IA64_HOWTO (R_IA64_COPY,	    "COPY",	   4, FALSE, TRUE),
    IA64_HOWTO (R_IA64_LTOFF22X,    "LTOFF22X",	   0, FALSE, TRUE),
    IA64_HOWTO (R_IA64_LDXMOV,	    "LDXMOV",	   0, FALSE, TRUE),

    IA64_HOWTO (R_IA64_TPREL14,	    "TPREL14",	   0, FALSE, FALSE),
    IA64_HOWTO (R_IA64_TPREL22,	    "TPREL22",	   0, FALSE, FALSE),
    IA64_HOWTO (R_IA64_TPREL64I,    "TPREL64I",	   0, FALSE, FALSE),
    IA64_HOWTO (R_IA64_TPREL64MSB,  "TPREL64MSB",  4, FALSE, FALSE),
    IA64_HOWTO (R_IA64_TPREL64LSB,  "TPREL64LSB",  4, FALSE, FALSE),
    IA64_HOWTO (R_IA64_LTOFF_TPREL22, "LTOFF_TPREL22",  0, FALSE, FALSE),

    IA64_HOWTO (R_IA64_DTPMOD64MSB, "DTPMOD64MSB",  4, FALSE, FALSE),
    IA64_HOWTO (R_IA64_DTPMOD64LSB, "DTPMOD64LSB",  4, FALSE, FALSE),
    IA64_HOWTO (R_IA64_LTOFF_DTPMOD22, "LTOFF_DTPMOD22", 0, FALSE, FALSE),

    IA64_HOWTO (R_IA64_DTPREL14,    "DTPREL14",	   0, FALSE, FALSE),
    IA64_HOWTO (R_IA64_DTPREL22,    "DTPREL22",	   0, FALSE, FALSE),
    IA64_HOWTO (R_IA64_DTPREL64I,   "DTPREL64I",   0, FALSE, FALSE),
    IA64_HOWTO (R_IA64_DTPREL32MSB, "DTPREL32MSB", 2, FALSE, FALSE),
    IA64_HOWTO (R_IA64_DTPREL32LSB, "DTPREL32LSB", 2, FALSE, FALSE),
    IA64_HOWTO (R_IA64_DTPREL64MSB, "DTPREL64MSB", 4, FALSE, FALSE),
    IA64_HOWTO (R_IA64_DTPREL64LSB, "DTPREL64LSB", 4, FALSE, FALSE),
    IA64_HOWTO (R_IA64_LTOFF_DTPREL22, "LTOFF_DTPREL22", 0, FALSE, FALSE),
  };

static unsigned char elf_code_to_howto_index[R_IA64_MAX_RELOC_CODE + 1];

/* Given a BFD reloc type, return the matching HOWTO structure.  */

static reloc_howto_type *
lookup_howto (rtype)
     unsigned int rtype;
{
  static int inited = 0;
  int i;

  if (!inited)
    {
      inited = 1;

      memset (elf_code_to_howto_index, 0xff, sizeof (elf_code_to_howto_index));
      for (i = 0; i < NELEMS (ia64_howto_table); ++i)
	elf_code_to_howto_index[ia64_howto_table[i].type] = i;
    }

  if (rtype > R_IA64_MAX_RELOC_CODE)
    return 0;
  i = elf_code_to_howto_index[rtype];
  if (i >= NELEMS (ia64_howto_table))
    return 0;
  return ia64_howto_table + i;
}

static reloc_howto_type*
elfNN_ia64_reloc_type_lookup (abfd, bfd_code)
     bfd *abfd ATTRIBUTE_UNUSED;
     bfd_reloc_code_real_type bfd_code;
{
  unsigned int rtype;

  switch (bfd_code)
    {
    case BFD_RELOC_NONE:		rtype = R_IA64_NONE; break;

    case BFD_RELOC_IA64_IMM14:		rtype = R_IA64_IMM14; break;
    case BFD_RELOC_IA64_IMM22:		rtype = R_IA64_IMM22; break;
    case BFD_RELOC_IA64_IMM64:		rtype = R_IA64_IMM64; break;

    case BFD_RELOC_IA64_DIR32MSB:	rtype = R_IA64_DIR32MSB; break;
    case BFD_RELOC_IA64_DIR32LSB:	rtype = R_IA64_DIR32LSB; break;
    case BFD_RELOC_IA64_DIR64MSB:	rtype = R_IA64_DIR64MSB; break;
    case BFD_RELOC_IA64_DIR64LSB:	rtype = R_IA64_DIR64LSB; break;

    case BFD_RELOC_IA64_GPREL22:	rtype = R_IA64_GPREL22; break;
    case BFD_RELOC_IA64_GPREL64I:	rtype = R_IA64_GPREL64I; break;
    case BFD_RELOC_IA64_GPREL32MSB:	rtype = R_IA64_GPREL32MSB; break;
    case BFD_RELOC_IA64_GPREL32LSB:	rtype = R_IA64_GPREL32LSB; break;
    case BFD_RELOC_IA64_GPREL64MSB:	rtype = R_IA64_GPREL64MSB; break;
    case BFD_RELOC_IA64_GPREL64LSB:	rtype = R_IA64_GPREL64LSB; break;

    case BFD_RELOC_IA64_LTOFF22:	rtype = R_IA64_LTOFF22; break;
    case BFD_RELOC_IA64_LTOFF64I:	rtype = R_IA64_LTOFF64I; break;

    case BFD_RELOC_IA64_PLTOFF22:	rtype = R_IA64_PLTOFF22; break;
    case BFD_RELOC_IA64_PLTOFF64I:	rtype = R_IA64_PLTOFF64I; break;
    case BFD_RELOC_IA64_PLTOFF64MSB:	rtype = R_IA64_PLTOFF64MSB; break;
    case BFD_RELOC_IA64_PLTOFF64LSB:	rtype = R_IA64_PLTOFF64LSB; break;
    case BFD_RELOC_IA64_FPTR64I:	rtype = R_IA64_FPTR64I; break;
    case BFD_RELOC_IA64_FPTR32MSB:	rtype = R_IA64_FPTR32MSB; break;
    case BFD_RELOC_IA64_FPTR32LSB:	rtype = R_IA64_FPTR32LSB; break;
    case BFD_RELOC_IA64_FPTR64MSB:	rtype = R_IA64_FPTR64MSB; break;
    case BFD_RELOC_IA64_FPTR64LSB:	rtype = R_IA64_FPTR64LSB; break;

    case BFD_RELOC_IA64_PCREL21B:	rtype = R_IA64_PCREL21B; break;
    case BFD_RELOC_IA64_PCREL21BI:	rtype = R_IA64_PCREL21BI; break;
    case BFD_RELOC_IA64_PCREL21M:	rtype = R_IA64_PCREL21M; break;
    case BFD_RELOC_IA64_PCREL21F:	rtype = R_IA64_PCREL21F; break;
    case BFD_RELOC_IA64_PCREL22:	rtype = R_IA64_PCREL22; break;
    case BFD_RELOC_IA64_PCREL60B:	rtype = R_IA64_PCREL60B; break;
    case BFD_RELOC_IA64_PCREL64I:	rtype = R_IA64_PCREL64I; break;
    case BFD_RELOC_IA64_PCREL32MSB:	rtype = R_IA64_PCREL32MSB; break;
    case BFD_RELOC_IA64_PCREL32LSB:	rtype = R_IA64_PCREL32LSB; break;
    case BFD_RELOC_IA64_PCREL64MSB:	rtype = R_IA64_PCREL64MSB; break;
    case BFD_RELOC_IA64_PCREL64LSB:	rtype = R_IA64_PCREL64LSB; break;

    case BFD_RELOC_IA64_LTOFF_FPTR22:	rtype = R_IA64_LTOFF_FPTR22; break;
    case BFD_RELOC_IA64_LTOFF_FPTR64I:	rtype = R_IA64_LTOFF_FPTR64I; break;
    case BFD_RELOC_IA64_LTOFF_FPTR32MSB: rtype = R_IA64_LTOFF_FPTR32MSB; break;
    case BFD_RELOC_IA64_LTOFF_FPTR32LSB: rtype = R_IA64_LTOFF_FPTR32LSB; break;
    case BFD_RELOC_IA64_LTOFF_FPTR64MSB: rtype = R_IA64_LTOFF_FPTR64MSB; break;
    case BFD_RELOC_IA64_LTOFF_FPTR64LSB: rtype = R_IA64_LTOFF_FPTR64LSB; break;

    case BFD_RELOC_IA64_SEGREL32MSB:	rtype = R_IA64_SEGREL32MSB; break;
    case BFD_RELOC_IA64_SEGREL32LSB:	rtype = R_IA64_SEGREL32LSB; break;
    case BFD_RELOC_IA64_SEGREL64MSB:	rtype = R_IA64_SEGREL64MSB; break;
    case BFD_RELOC_IA64_SEGREL64LSB:	rtype = R_IA64_SEGREL64LSB; break;

    case BFD_RELOC_IA64_SECREL32MSB:	rtype = R_IA64_SECREL32MSB; break;
    case BFD_RELOC_IA64_SECREL32LSB:	rtype = R_IA64_SECREL32LSB; break;
    case BFD_RELOC_IA64_SECREL64MSB:	rtype = R_IA64_SECREL64MSB; break;
    case BFD_RELOC_IA64_SECREL64LSB:	rtype = R_IA64_SECREL64LSB; break;

    case BFD_RELOC_IA64_REL32MSB:	rtype = R_IA64_REL32MSB; break;
    case BFD_RELOC_IA64_REL32LSB:	rtype = R_IA64_REL32LSB; break;
    case BFD_RELOC_IA64_REL64MSB:	rtype = R_IA64_REL64MSB; break;
    case BFD_RELOC_IA64_REL64LSB:	rtype = R_IA64_REL64LSB; break;

    case BFD_RELOC_IA64_LTV32MSB:	rtype = R_IA64_LTV32MSB; break;
    case BFD_RELOC_IA64_LTV32LSB:	rtype = R_IA64_LTV32LSB; break;
    case BFD_RELOC_IA64_LTV64MSB:	rtype = R_IA64_LTV64MSB; break;
    case BFD_RELOC_IA64_LTV64LSB:	rtype = R_IA64_LTV64LSB; break;

    case BFD_RELOC_IA64_IPLTMSB:	rtype = R_IA64_IPLTMSB; break;
    case BFD_RELOC_IA64_IPLTLSB:	rtype = R_IA64_IPLTLSB; break;
    case BFD_RELOC_IA64_COPY:		rtype = R_IA64_COPY; break;
    case BFD_RELOC_IA64_LTOFF22X:	rtype = R_IA64_LTOFF22X; break;
    case BFD_RELOC_IA64_LDXMOV:		rtype = R_IA64_LDXMOV; break;

    case BFD_RELOC_IA64_TPREL14:	rtype = R_IA64_TPREL14; break;
    case BFD_RELOC_IA64_TPREL22:	rtype = R_IA64_TPREL22; break;
    case BFD_RELOC_IA64_TPREL64I:	rtype = R_IA64_TPREL64I; break;
    case BFD_RELOC_IA64_TPREL64MSB:	rtype = R_IA64_TPREL64MSB; break;
    case BFD_RELOC_IA64_TPREL64LSB:	rtype = R_IA64_TPREL64LSB; break;
    case BFD_RELOC_IA64_LTOFF_TPREL22:	rtype = R_IA64_LTOFF_TPREL22; break;

    case BFD_RELOC_IA64_DTPMOD64MSB:	rtype = R_IA64_DTPMOD64MSB; break;
    case BFD_RELOC_IA64_DTPMOD64LSB:	rtype = R_IA64_DTPMOD64LSB; break;
    case BFD_RELOC_IA64_LTOFF_DTPMOD22:	rtype = R_IA64_LTOFF_DTPMOD22; break;

    case BFD_RELOC_IA64_DTPREL14:	rtype = R_IA64_DTPREL14; break;
    case BFD_RELOC_IA64_DTPREL22:	rtype = R_IA64_DTPREL22; break;
    case BFD_RELOC_IA64_DTPREL64I:	rtype = R_IA64_DTPREL64I; break;
    case BFD_RELOC_IA64_DTPREL32MSB:	rtype = R_IA64_DTPREL32MSB; break;
    case BFD_RELOC_IA64_DTPREL32LSB:	rtype = R_IA64_DTPREL32LSB; break;
    case BFD_RELOC_IA64_DTPREL64MSB:	rtype = R_IA64_DTPREL64MSB; break;
    case BFD_RELOC_IA64_DTPREL64LSB:	rtype = R_IA64_DTPREL64LSB; break;
    case BFD_RELOC_IA64_LTOFF_DTPREL22:	rtype = R_IA64_LTOFF_DTPREL22; break;

    default: return 0;
    }
  return lookup_howto (rtype);
}

static reloc_howto_type *
elfNN_ia64_reloc_name_lookup (bfd *abfd ATTRIBUTE_UNUSED,
			      const char *r_name)
{
  unsigned int i;

  for (i = 0;
       i < sizeof (ia64_howto_table) / sizeof (ia64_howto_table[0]);
       i++)
    if (ia64_howto_table[i].name != NULL
	&& strcasecmp (ia64_howto_table[i].name, r_name) == 0)
      return &ia64_howto_table[i];

  return NULL;
}

/* Given a ELF reloc, return the matching HOWTO structure.  */

static void
elfNN_ia64_info_to_howto (abfd, bfd_reloc, elf_reloc)
     bfd *abfd ATTRIBUTE_UNUSED;
     arelent *bfd_reloc;
     Elf_Internal_Rela *elf_reloc;
{
  bfd_reloc->howto
    = lookup_howto ((unsigned int) ELFNN_R_TYPE (elf_reloc->r_info));
}

#define PLT_HEADER_SIZE		(3 * 16)
#define PLT_MIN_ENTRY_SIZE	(1 * 16)
#define PLT_FULL_ENTRY_SIZE	(2 * 16)
#define PLT_RESERVED_WORDS	3

static const bfd_byte plt_header[PLT_HEADER_SIZE] =
{
  0x0b, 0x10, 0x00, 0x1c, 0x00, 0x21,  /*   [MMI]       mov r2=r14;;       */
  0xe0, 0x00, 0x08, 0x00, 0x48, 0x00,  /*               addl r14=0,r2      */
  0x00, 0x00, 0x04, 0x00,              /*               nop.i 0x0;;        */
  0x0b, 0x80, 0x20, 0x1c, 0x18, 0x14,  /*   [MMI]       ld8 r16=[r14],8;;  */
  0x10, 0x41, 0x38, 0x30, 0x28, 0x00,  /*               ld8 r17=[r14],8    */
  0x00, 0x00, 0x04, 0x00,              /*               nop.i 0x0;;        */
  0x11, 0x08, 0x00, 0x1c, 0x18, 0x10,  /*   [MIB]       ld8 r1=[r14]       */
  0x60, 0x88, 0x04, 0x80, 0x03, 0x00,  /*               mov b6=r17         */
  0x60, 0x00, 0x80, 0x00               /*               br.few b6;;        */
};

static const bfd_byte plt_min_entry[PLT_MIN_ENTRY_SIZE] =
{
  0x11, 0x78, 0x00, 0x00, 0x00, 0x24,  /*   [MIB]       mov r15=0          */
  0x00, 0x00, 0x00, 0x02, 0x00, 0x00,  /*               nop.i 0x0          */
  0x00, 0x00, 0x00, 0x40               /*               br.few 0 <PLT0>;;  */
};

static const bfd_byte plt_full_entry[PLT_FULL_ENTRY_SIZE] =
{
  0x0b, 0x78, 0x00, 0x02, 0x00, 0x24,  /*   [MMI]       addl r15=0,r1;;    */
  0x00, 0x41, 0x3c, 0x70, 0x29, 0xc0,  /*               ld8.acq r16=[r15],8*/
  0x01, 0x08, 0x00, 0x84,              /*               mov r14=r1;;       */
  0x11, 0x08, 0x00, 0x1e, 0x18, 0x10,  /*   [MIB]       ld8 r1=[r15]       */
  0x60, 0x80, 0x04, 0x80, 0x03, 0x00,  /*               mov b6=r16         */
  0x60, 0x00, 0x80, 0x00               /*               br.few b6;;        */
};

#define ELF_DYNAMIC_INTERPRETER "/usr/lib/ld.so.1"

static const bfd_byte oor_brl[16] =
{
  0x05, 0x00, 0x00, 0x00, 0x01, 0x00,  /*  [MLX]        nop.m 0            */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /*               brl.sptk.few tgt;; */
  0x00, 0x00, 0x00, 0xc0
};

static const bfd_byte oor_ip[48] =
{
  0x04, 0x00, 0x00, 0x00, 0x01, 0x00,  /*  [MLX]        nop.m 0            */
  0x00, 0x00, 0x00, 0x00, 0x00, 0xe0,  /*               movl r15=0         */
  0x01, 0x00, 0x00, 0x60,
  0x03, 0x00, 0x00, 0x00, 0x01, 0x00,  /*  [MII]        nop.m 0            */
  0x00, 0x01, 0x00, 0x60, 0x00, 0x00,  /*               mov r16=ip;;       */
  0xf2, 0x80, 0x00, 0x80,              /*               add r16=r15,r16;;  */
  0x11, 0x00, 0x00, 0x00, 0x01, 0x00,  /*  [MIB]        nop.m 0            */
  0x60, 0x80, 0x04, 0x80, 0x03, 0x00,  /*               mov b6=r16         */
  0x60, 0x00, 0x80, 0x00               /*               br b6;;            */
};

static size_t oor_branch_size = sizeof (oor_brl);

void
bfd_elfNN_ia64_after_parse (int itanium)
{
  oor_branch_size = itanium ? sizeof (oor_ip) : sizeof (oor_brl);
}

#define BTYPE_SHIFT	6
#define Y_SHIFT		26
#define X6_SHIFT	27
#define X4_SHIFT	27
#define X3_SHIFT	33
#define X2_SHIFT	31
#define X_SHIFT		33
#define OPCODE_SHIFT	37

#define OPCODE_BITS	(0xfLL << OPCODE_SHIFT)
#define X6_BITS		(0x3fLL << X6_SHIFT)
#define X4_BITS		(0xfLL << X4_SHIFT)
#define X3_BITS		(0x7LL << X3_SHIFT)
#define X2_BITS		(0x3LL << X2_SHIFT)
#define X_BITS		(0x1LL << X_SHIFT)
#define Y_BITS		(0x1LL << Y_SHIFT)
#define BTYPE_BITS	(0x7LL << BTYPE_SHIFT)
#define PREDICATE_BITS	(0x3fLL)

#define IS_NOP_B(i) \
  (((i) & (OPCODE_BITS | X6_BITS)) == (2LL << OPCODE_SHIFT))
#define IS_NOP_F(i) \
  (((i) & (OPCODE_BITS | X_BITS | X6_BITS | Y_BITS)) \
   == (0x1LL << X6_SHIFT))
#define IS_NOP_I(i) \
  (((i) & (OPCODE_BITS | X3_BITS | X6_BITS | Y_BITS)) \
   == (0x1LL << X6_SHIFT))
#define IS_NOP_M(i) \
  (((i) & (OPCODE_BITS | X3_BITS | X2_BITS | X4_BITS | Y_BITS)) \
   == (0x1LL << X4_SHIFT))
#define IS_BR_COND(i) \
  (((i) & (OPCODE_BITS | BTYPE_BITS)) == (0x4LL << OPCODE_SHIFT))
#define IS_BR_CALL(i) \
  (((i) & OPCODE_BITS) == (0x5LL << OPCODE_SHIFT))

static bfd_boolean
elfNN_ia64_relax_br (bfd_byte *contents, bfd_vma off)
{
  unsigned int template, mlx;
  bfd_vma t0, t1, s0, s1, s2, br_code;
  long br_slot;
  bfd_byte *hit_addr;

  hit_addr = (bfd_byte *) (contents + off);
  br_slot = (long) hit_addr & 0x3;
  hit_addr -= br_slot;
  t0 = bfd_getl64 (hit_addr + 0);
  t1 = bfd_getl64 (hit_addr + 8);

  /* Check if we can turn br into brl.  A label is always at the start
     of the bundle.  Even if there are predicates on NOPs, we still
     perform this optimization.  */
  template = t0 & 0x1e;
  s0 = (t0 >> 5) & 0x1ffffffffffLL;
  s1 = ((t0 >> 46) | (t1 << 18)) & 0x1ffffffffffLL;
  s2 = (t1 >> 23) & 0x1ffffffffffLL;
  switch (br_slot)
    {
    case 0:
      /* Check if slot 1 and slot 2 are NOPs. Possible template is
         BBB.  We only need to check nop.b.  */
      if (!(IS_NOP_B (s1) && IS_NOP_B (s2)))
	return FALSE;
      br_code = s0;
      break;
    case 1:
      /* Check if slot 2 is NOP. Possible templates are MBB and BBB.
	 For BBB, slot 0 also has to be nop.b.  */
      if (!((template == 0x12				/* MBB */
	     && IS_NOP_B (s2))
	    || (template == 0x16			/* BBB */
		&& IS_NOP_B (s0)
		&& IS_NOP_B (s2))))
	return FALSE;
      br_code = s1;
      break;
    case 2:
      /* Check if slot 1 is NOP. Possible templates are MIB, MBB, BBB,
	 MMB and MFB. For BBB, slot 0 also has to be nop.b.  */
      if (!((template == 0x10				/* MIB */
	     && IS_NOP_I (s1))
	    || (template == 0x12			/* MBB */
		&& IS_NOP_B (s1))
	    || (template == 0x16			/* BBB */
		&& IS_NOP_B (s0)
		&& IS_NOP_B (s1))
	    || (template == 0x18			/* MMB */
		&& IS_NOP_M (s1))
	    || (template == 0x1c			/* MFB */
		&& IS_NOP_F (s1))))
	return FALSE;
      br_code = s2;
      break;
    default:
      /* It should never happen.  */
      abort ();
    }
  
  /* We can turn br.cond/br.call into brl.cond/brl.call.  */
  if (!(IS_BR_COND (br_code) || IS_BR_CALL (br_code)))
    return FALSE;

  /* Turn br into brl by setting bit 40.  */
  br_code |= 0x1LL << 40;

  /* Turn the old bundle into a MLX bundle with the same stop-bit
     variety.  */
  if (t0 & 0x1)
    mlx = 0x5;
  else
    mlx = 0x4;

  if (template == 0x16)
    {
      /* For BBB, we need to put nop.m in slot 0.  We keep the original
	 predicate only if slot 0 isn't br.  */
      if (br_slot == 0)
	t0 = 0LL;
      else
	t0 &= PREDICATE_BITS << 5;
      t0 |= 0x1LL << (X4_SHIFT + 5);
    }
  else
    {
      /* Keep the original instruction in slot 0.  */
      t0 &= 0x1ffffffffffLL << 5;
    }

  t0 |= mlx;

  /* Put brl in slot 1.  */
  t1 = br_code << 23;

  bfd_putl64 (t0, hit_addr);
  bfd_putl64 (t1, hit_addr + 8);
  return TRUE;
}

static void
elfNN_ia64_relax_brl (bfd_byte *contents, bfd_vma off)
{
  int template;
  bfd_byte *hit_addr;
  bfd_vma t0, t1, i0, i1, i2;

  hit_addr = (bfd_byte *) (contents + off);
  hit_addr -= (long) hit_addr & 0x3;
  t0 = bfd_getl64 (hit_addr);
  t1 = bfd_getl64 (hit_addr + 8);

  /* Keep the instruction in slot 0. */
  i0 = (t0 >> 5) & 0x1ffffffffffLL;
  /* Use nop.b for slot 1. */
  i1 = 0x4000000000LL;
  /* For slot 2, turn brl into br by masking out bit 40.  */
  i2 = (t1 >> 23) & 0x0ffffffffffLL;

  /* Turn a MLX bundle into a MBB bundle with the same stop-bit
     variety.  */
  if (t0 & 0x1)
    template = 0x13;
  else
    template = 0x12;
  t0 = (i1 << 46) | (i0 << 5) | template;
  t1 = (i2 << 23) | (i1 >> 18);

  bfd_putl64 (t0, hit_addr);
  bfd_putl64 (t1, hit_addr + 8);
}

/* Rename some of the generic section flags to better document how they
   are used here.  */
#define skip_relax_pass_0 need_finalize_relax
#define skip_relax_pass_1 has_gp_reloc


/* These functions do relaxation for IA-64 ELF.  */

static bfd_boolean
elfNN_ia64_relax_section (abfd, sec, link_info, again)
     bfd *abfd;
     asection *sec;
     struct bfd_link_info *link_info;
     bfd_boolean *again;
{
  struct one_fixup
    {
      struct one_fixup *next;
      asection *tsec;
      bfd_vma toff;
      bfd_vma trampoff;
    };

  Elf_Internal_Shdr *symtab_hdr;
  Elf_Internal_Rela *internal_relocs;
  Elf_Internal_Rela *irel, *irelend;
  bfd_byte *contents;
  Elf_Internal_Sym *isymbuf = NULL;
  struct elfNN_ia64_link_hash_table *ia64_info;
  struct one_fixup *fixups = NULL;
  bfd_boolean changed_contents = FALSE;
  bfd_boolean changed_relocs = FALSE;
  bfd_boolean changed_got = FALSE;
  bfd_boolean skip_relax_pass_0 = TRUE;
  bfd_boolean skip_relax_pass_1 = TRUE;
  bfd_vma gp = 0;

  /* Assume we're not going to change any sizes, and we'll only need
     one pass.  */
  *again = FALSE;

  /* Don't even try to relax for non-ELF outputs.  */
  if (!is_elf_hash_table (link_info->hash))
    return FALSE;

  /* Nothing to do if there are no relocations or there is no need for
     the current pass.  */
  if ((sec->flags & SEC_RELOC) == 0
      || sec->reloc_count == 0
      || (link_info->relax_pass == 0 && sec->skip_relax_pass_0)
      || (link_info->relax_pass == 1 && sec->skip_relax_pass_1))
    return TRUE;

  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;

  /* Load the relocations for this section.  */
  internal_relocs = (_bfd_elf_link_read_relocs
		     (abfd, sec, (PTR) NULL, (Elf_Internal_Rela *) NULL,
		      link_info->keep_memory));
  if (internal_relocs == NULL)
    return FALSE;

  ia64_info = elfNN_ia64_hash_table (link_info);
  irelend = internal_relocs + sec->reloc_count;

  /* Get the section contents.  */
  if (elf_section_data (sec)->this_hdr.contents != NULL)
    contents = elf_section_data (sec)->this_hdr.contents;
  else
    {
      if (!bfd_malloc_and_get_section (abfd, sec, &contents))
	goto error_return;
    }

  for (irel = internal_relocs; irel < irelend; irel++)
    {
      unsigned long r_type = ELFNN_R_TYPE (irel->r_info);
      bfd_vma symaddr, reladdr, trampoff, toff, roff;
      asection *tsec;
      struct one_fixup *f;
      bfd_size_type amt;
      bfd_boolean is_branch;
      struct elfNN_ia64_dyn_sym_info *dyn_i;
      char symtype;

      switch (r_type)
	{
	case R_IA64_PCREL21B:
	case R_IA64_PCREL21BI:
	case R_IA64_PCREL21M:
	case R_IA64_PCREL21F:
	  /* In pass 1, all br relaxations are done. We can skip it. */
	  if (link_info->relax_pass == 1)
	    continue;
	  skip_relax_pass_0 = FALSE;
	  is_branch = TRUE;
	  break;

	case R_IA64_PCREL60B:
	  /* We can't optimize brl to br in pass 0 since br relaxations
	     will increase the code size. Defer it to pass 1.  */
	  if (link_info->relax_pass == 0)
	    {
	      skip_relax_pass_1 = FALSE;
	      continue;
	    }
	  is_branch = TRUE;
	  break;

	case R_IA64_LTOFF22X:
	case R_IA64_LDXMOV:
	  /* We can't relax ldx/mov in pass 0 since br relaxations will
	     increase the code size. Defer it to pass 1.  */
	  if (link_info->relax_pass == 0)
	    {
	      skip_relax_pass_1 = FALSE;
	      continue;
	    }
	  is_branch = FALSE;
	  break;

	default:
	  continue;
	}

      /* Get the value of the symbol referred to by the reloc.  */
      if (ELFNN_R_SYM (irel->r_info) < symtab_hdr->sh_info)
	{
	  /* A local symbol.  */
	  Elf_Internal_Sym *isym;

	  /* Read this BFD's local symbols.  */
	  if (isymbuf == NULL)
	    {
	      isymbuf = (Elf_Internal_Sym *) symtab_hdr->contents;
	      if (isymbuf == NULL)
		isymbuf = bfd_elf_get_elf_syms (abfd, symtab_hdr,
						symtab_hdr->sh_info, 0,
						NULL, NULL, NULL);
	      if (isymbuf == 0)
		goto error_return;
	    }

	  isym = isymbuf + ELFNN_R_SYM (irel->r_info);
	  if (isym->st_shndx == SHN_UNDEF)
	    continue;	/* We can't do anything with undefined symbols.  */
	  else if (isym->st_shndx == SHN_ABS)
	    tsec = bfd_abs_section_ptr;
	  else if (isym->st_shndx == SHN_COMMON)
	    tsec = bfd_com_section_ptr;
	  else if (isym->st_shndx == SHN_IA_64_ANSI_COMMON)
	    tsec = bfd_com_section_ptr;
	  else
	    tsec = bfd_section_from_elf_index (abfd, isym->st_shndx);

	  toff = isym->st_value;
	  dyn_i = get_dyn_sym_info (ia64_info, NULL, abfd, irel, FALSE);
	  symtype = ELF_ST_TYPE (isym->st_info);
	}
      else
	{
	  unsigned long indx;
	  struct elf_link_hash_entry *h;

	  indx = ELFNN_R_SYM (irel->r_info) - symtab_hdr->sh_info;
	  h = elf_sym_hashes (abfd)[indx];
	  BFD_ASSERT (h != NULL);

	  while (h->root.type == bfd_link_hash_indirect
		 || h->root.type == bfd_link_hash_warning)
	    h = (struct elf_link_hash_entry *) h->root.u.i.link;

	  dyn_i = get_dyn_sym_info (ia64_info, h, abfd, irel, FALSE);

	  /* For branches to dynamic symbols, we're interested instead
	     in a branch to the PLT entry.  */
	  if (is_branch && dyn_i && dyn_i->want_plt2)
	    {
	      /* Internal branches shouldn't be sent to the PLT.
		 Leave this for now and we'll give an error later.  */
	      if (r_type != R_IA64_PCREL21B)
		continue;

	      tsec = ia64_info->plt_sec;
	      toff = dyn_i->plt2_offset;
	      BFD_ASSERT (irel->r_addend == 0);
	    }

	  /* Can't do anything else with dynamic symbols.  */
	  else if (elfNN_ia64_dynamic_symbol_p (h, link_info, r_type))
	    continue;

	  else
	    {
	      /* We can't do anything with undefined symbols.  */
	      if (h->root.type == bfd_link_hash_undefined
		  || h->root.type == bfd_link_hash_undefweak)
		continue;

	      tsec = h->root.u.def.section;
	      toff = h->root.u.def.value;
	    }

	  symtype = h->type;
	}

      if (tsec->sec_info_type == ELF_INFO_TYPE_MERGE)
	{
	  /* At this stage in linking, no SEC_MERGE symbol has been
	     adjusted, so all references to such symbols need to be
	     passed through _bfd_merged_section_offset.  (Later, in
	     relocate_section, all SEC_MERGE symbols *except* for
	     section symbols have been adjusted.)

	     gas may reduce relocations against symbols in SEC_MERGE
	     sections to a relocation against the section symbol when
	     the original addend was zero.  When the reloc is against
	     a section symbol we should include the addend in the
	     offset passed to _bfd_merged_section_offset, since the
	     location of interest is the original symbol.  On the
	     other hand, an access to "sym+addend" where "sym" is not
	     a section symbol should not include the addend;  Such an
	     access is presumed to be an offset from "sym";  The
	     location of interest is just "sym".  */
	   if (symtype == STT_SECTION)
	     toff += irel->r_addend;

	   toff = _bfd_merged_section_offset (abfd, &tsec,
					      elf_section_data (tsec)->sec_info,
					      toff);

	   if (symtype != STT_SECTION)
	     toff += irel->r_addend;
	}
      else
	toff += irel->r_addend;

      symaddr = tsec->output_section->vma + tsec->output_offset + toff;

      roff = irel->r_offset;

      if (is_branch)
	{
	  bfd_signed_vma offset;

	  reladdr = (sec->output_section->vma
		     + sec->output_offset
		     + roff) & (bfd_vma) -4;

	  /* If the branch is in range, no need to do anything.  */
	  if ((bfd_signed_vma) (symaddr - reladdr) >= -0x1000000
	      && (bfd_signed_vma) (symaddr - reladdr) <= 0x0FFFFF0)
	    {
	      /* If the 60-bit branch is in 21-bit range, optimize it. */
	      if (r_type == R_IA64_PCREL60B)
		{
		  elfNN_ia64_relax_brl (contents, roff);

		  irel->r_info
		    = ELFNN_R_INFO (ELFNN_R_SYM (irel->r_info),
				    R_IA64_PCREL21B);

		  /* If the original relocation offset points to slot
		     1, change it to slot 2.  */
		  if ((irel->r_offset & 3) == 1)
		    irel->r_offset += 1;
		}

	      continue;
	    }
	  else if (r_type == R_IA64_PCREL60B)
	    continue;
	  else if (elfNN_ia64_relax_br (contents, roff))
	    {
	      irel->r_info
		= ELFNN_R_INFO (ELFNN_R_SYM (irel->r_info),
				R_IA64_PCREL60B);

	      /* Make the relocation offset point to slot 1.  */
	      irel->r_offset = (irel->r_offset & ~((bfd_vma) 0x3)) + 1;
	      continue;
	    }

	  /* We can't put a trampoline in a .init/.fini section. Issue
	     an error.  */
	  if (strcmp (sec->output_section->name, ".init") == 0
	      || strcmp (sec->output_section->name, ".fini") == 0)
	    {
	      (*_bfd_error_handler)
		(_("%B: Can't relax br at 0x%lx in section `%A'. Please use brl or indirect branch."),
		 sec->owner, sec, (unsigned long) roff);
	      bfd_set_error (bfd_error_bad_value);
	      goto error_return;
	    }

	  /* If the branch and target are in the same section, you've
	     got one honking big section and we can't help you unless
	     you are branching backwards.  You'll get an error message
	     later.  */
	  if (tsec == sec && toff > roff)
	    continue;

	  /* Look for an existing fixup to this address.  */
	  for (f = fixups; f ; f = f->next)
	    if (f->tsec == tsec && f->toff == toff)
	      break;

	  if (f == NULL)
	    {
	      /* Two alternatives: If it's a branch to a PLT entry, we can
		 make a copy of the FULL_PLT entry.  Otherwise, we'll have
		 to use a `brl' insn to get where we're going.  */

	      size_t size;

	      if (tsec == ia64_info->plt_sec)
		size = sizeof (plt_full_entry);
	      else
		size = oor_branch_size;

	      /* Resize the current section to make room for the new branch. */
	      trampoff = (sec->size + 15) & (bfd_vma) -16;

	      /* If trampoline is out of range, there is nothing we
		 can do.  */
	      offset = trampoff - (roff & (bfd_vma) -4);
	      if (offset < -0x1000000 || offset > 0x0FFFFF0)
		continue;

	      amt = trampoff + size;
	      contents = (bfd_byte *) bfd_realloc (contents, amt);
	      if (contents == NULL)
		goto error_return;
	      sec->size = amt;

	      if (tsec == ia64_info->plt_sec)
		{
		  memcpy (contents + trampoff, plt_full_entry, size);

		  /* Hijack the old relocation for use as the PLTOFF reloc.  */
		  irel->r_info = ELFNN_R_INFO (ELFNN_R_SYM (irel->r_info),
					       R_IA64_PLTOFF22);
		  irel->r_offset = trampoff;
		}
	      else
		{
		  if (size == sizeof (oor_ip))
		    {
		      memcpy (contents + trampoff, oor_ip, size);
		      irel->r_info = ELFNN_R_INFO (ELFNN_R_SYM (irel->r_info),
						   R_IA64_PCREL64I);
		      irel->r_addend -= 16;
		      irel->r_offset = trampoff + 2;
		    }
		  else
		    {
		      memcpy (contents + trampoff, oor_brl, size);
		      irel->r_info = ELFNN_R_INFO (ELFNN_R_SYM (irel->r_info),
						   R_IA64_PCREL60B);
		      irel->r_offset = trampoff + 2;
		    }

		}

	      /* Record the fixup so we don't do it again this section.  */
	      f = (struct one_fixup *)
		bfd_malloc ((bfd_size_type) sizeof (*f));
	      f->next = fixups;
	      f->tsec = tsec;
	      f->toff = toff;
	      f->trampoff = trampoff;
	      fixups = f;
	    }
	  else
	    {
	      /* If trampoline is out of range, there is nothing we
		 can do.  */
	      offset = f->trampoff - (roff & (bfd_vma) -4);
	      if (offset < -0x1000000 || offset > 0x0FFFFF0)
		continue;

	      /* Nop out the reloc, since we're finalizing things here.  */
	      irel->r_info = ELFNN_R_INFO (0, R_IA64_NONE);
	    }

	  /* Fix up the existing branch to hit the trampoline.  */
	  if (elfNN_ia64_install_value (contents + roff, offset, r_type)
	      != bfd_reloc_ok)
	    goto error_return;

	  changed_contents = TRUE;
	  changed_relocs = TRUE;
	}
      else
	{
	  /* Fetch the gp.  */
	  if (gp == 0)
	    {
	      bfd *obfd = sec->output_section->owner;
	      gp = _bfd_get_gp_value (obfd);
	      if (gp == 0)
		{
		  if (!elfNN_ia64_choose_gp (obfd, link_info))
		    goto error_return;
		  gp = _bfd_get_gp_value (obfd);
		}
	    }

	  /* If the data is out of range, do nothing.  */
	  if ((bfd_signed_vma) (symaddr - gp) >= 0x200000
	      ||(bfd_signed_vma) (symaddr - gp) < -0x200000)
	    continue;

	  if (r_type == R_IA64_LTOFF22X)
	    {
	      irel->r_info = ELFNN_R_INFO (ELFNN_R_SYM (irel->r_info),
					   R_IA64_GPREL22);
	      changed_relocs = TRUE;
	      if (dyn_i->want_gotx)
		{
		  dyn_i->want_gotx = 0;
		  changed_got |= !dyn_i->want_got;
		}
	    }
	  else
	    {
	      elfNN_ia64_relax_ldxmov (contents, roff);
	      irel->r_info = ELFNN_R_INFO (0, R_IA64_NONE);
	      changed_contents = TRUE;
	      changed_relocs = TRUE;
	    }
	}
    }

  /* ??? If we created fixups, this may push the code segment large
     enough that the data segment moves, which will change the GP.
     Reset the GP so that we re-calculate next round.  We need to
     do this at the _beginning_ of the next round; now will not do.  */

  /* Clean up and go home.  */
  while (fixups)
    {
      struct one_fixup *f = fixups;
      fixups = fixups->next;
      free (f);
    }

  if (isymbuf != NULL
      && symtab_hdr->contents != (unsigned char *) isymbuf)
    {
      if (! link_info->keep_memory)
	free (isymbuf);
      else
	{
	  /* Cache the symbols for elf_link_input_bfd.  */
	  symtab_hdr->contents = (unsigned char *) isymbuf;
	}
    }

  if (contents != NULL
      && elf_section_data (sec)->this_hdr.contents != contents)
    {
      if (!changed_contents && !link_info->keep_memory)
	free (contents);
      else
	{
	  /* Cache the section contents for elf_link_input_bfd.  */
	  elf_section_data (sec)->this_hdr.contents = contents;
	}
    }

  if (elf_section_data (sec)->relocs != internal_relocs)
    {
      if (!changed_relocs)
	free (internal_relocs);
      else
	elf_section_data (sec)->relocs = internal_relocs;
    }

  if (changed_got)
    {
      struct elfNN_ia64_allocate_data data;
      data.info = link_info;
      data.ofs = 0;
      ia64_info->self_dtpmod_offset = (bfd_vma) -1;

      elfNN_ia64_dyn_sym_traverse (ia64_info, allocate_global_data_got, &data);
      elfNN_ia64_dyn_sym_traverse (ia64_info, allocate_global_fptr_got, &data);
      elfNN_ia64_dyn_sym_traverse (ia64_info, allocate_local_got, &data);
      ia64_info->got_sec->size = data.ofs;

      if (ia64_info->root.dynamic_sections_created
	  && ia64_info->rel_got_sec != NULL)
	{
	  /* Resize .rela.got.  */
	  ia64_info->rel_got_sec->size = 0;
	  if (link_info->shared
	      && ia64_info->self_dtpmod_offset != (bfd_vma) -1)
	    ia64_info->rel_got_sec->size += sizeof (ElfNN_External_Rela);
	  data.only_got = TRUE;
	  elfNN_ia64_dyn_sym_traverse (ia64_info, allocate_dynrel_entries,
				       &data);
	}
    }

  if (link_info->relax_pass == 0)
    {
      /* Pass 0 is only needed to relax br.  */
      sec->skip_relax_pass_0 = skip_relax_pass_0;
      sec->skip_relax_pass_1 = skip_relax_pass_1;
    }

  *again = changed_contents || changed_relocs;
  return TRUE;

 error_return:
  if (isymbuf != NULL && (unsigned char *) isymbuf != symtab_hdr->contents)
    free (isymbuf);
  if (contents != NULL
      && elf_section_data (sec)->this_hdr.contents != contents)
    free (contents);
  if (internal_relocs != NULL
      && elf_section_data (sec)->relocs != internal_relocs)
    free (internal_relocs);
  return FALSE;
}
#undef skip_relax_pass_0
#undef skip_relax_pass_1

static void
elfNN_ia64_relax_ldxmov (contents, off)
     bfd_byte *contents;
     bfd_vma off;
{
  int shift, r1, r3;
  bfd_vma dword, insn;

  switch ((int)off & 0x3)
    {
    case 0: shift =  5; break;
    case 1: shift = 14; off += 3; break;
    case 2: shift = 23; off += 6; break;
    default:
      abort ();
    }

  dword = bfd_getl64 (contents + off);
  insn = (dword >> shift) & 0x1ffffffffffLL;

  r1 = (insn >> 6) & 127;
  r3 = (insn >> 20) & 127;
  if (r1 == r3)
    insn = 0x8000000;				   /* nop */
  else
    insn = (insn & 0x7f01fff) | 0x10800000000LL;   /* (qp) mov r1 = r3 */

  dword &= ~(0x1ffffffffffLL << shift);
  dword |= (insn << shift);
  bfd_putl64 (dword, contents + off);
}

/* Return TRUE if NAME is an unwind table section name.  */

static inline bfd_boolean
is_unwind_section_name (bfd *abfd, const char *name)
{
  if (elfNN_ia64_hpux_vec (abfd->xvec)
      && !strcmp (name, ELF_STRING_ia64_unwind_hdr))
    return FALSE;

  return ((CONST_STRNEQ (name, ELF_STRING_ia64_unwind)
	   && ! CONST_STRNEQ (name, ELF_STRING_ia64_unwind_info))
	  || CONST_STRNEQ (name, ELF_STRING_ia64_unwind_once));
}

/* Handle an IA-64 specific section when reading an object file.  This
   is called when bfd_section_from_shdr finds a section with an unknown
   type.  */

static bfd_boolean
elfNN_ia64_section_from_shdr (bfd *abfd,
			      Elf_Internal_Shdr *hdr,
			      const char *name,
			      int shindex)
{
  asection *newsect;

  /* There ought to be a place to keep ELF backend specific flags, but
     at the moment there isn't one.  We just keep track of the
     sections by their name, instead.  Fortunately, the ABI gives
     suggested names for all the MIPS specific sections, so we will
     probably get away with this.  */
  switch (hdr->sh_type)
    {
    case SHT_IA_64_UNWIND:
    case SHT_IA_64_HP_OPT_ANOT:
      break;

    case SHT_IA_64_EXT:
      if (strcmp (name, ELF_STRING_ia64_archext) != 0)
	return FALSE;
      break;

    default:
      return FALSE;
    }

  if (! _bfd_elf_make_section_from_shdr (abfd, hdr, name, shindex))
    return FALSE;
  newsect = hdr->bfd_section;

  return TRUE;
}

/* Convert IA-64 specific section flags to bfd internal section flags.  */

/* ??? There is no bfd internal flag equivalent to the SHF_IA_64_NORECOV
   flag.  */

static bfd_boolean
elfNN_ia64_section_flags (flags, hdr)
     flagword *flags;
     const Elf_Internal_Shdr *hdr;
{
  if (hdr->sh_flags & SHF_IA_64_SHORT)
    *flags |= SEC_SMALL_DATA;

  return TRUE;
}

/* Set the correct type for an IA-64 ELF section.  We do this by the
   section name, which is a hack, but ought to work.  */

static bfd_boolean
elfNN_ia64_fake_sections (abfd, hdr, sec)
     bfd *abfd ATTRIBUTE_UNUSED;
     Elf_Internal_Shdr *hdr;
     asection *sec;
{
  register const char *name;

  name = bfd_get_section_name (abfd, sec);

  if (is_unwind_section_name (abfd, name))
    {
      /* We don't have the sections numbered at this point, so sh_info
	 is set later, in elfNN_ia64_final_write_processing.  */
      hdr->sh_type = SHT_IA_64_UNWIND;
      hdr->sh_flags |= SHF_LINK_ORDER;
    }
  else if (strcmp (name, ELF_STRING_ia64_archext) == 0)
    hdr->sh_type = SHT_IA_64_EXT;
  else if (strcmp (name, ".HP.opt_annot") == 0)
    hdr->sh_type = SHT_IA_64_HP_OPT_ANOT;
  else if (strcmp (name, ".reloc") == 0)
    /* This is an ugly, but unfortunately necessary hack that is
       needed when producing EFI binaries on IA-64. It tells
       elf.c:elf_fake_sections() not to consider ".reloc" as a section
       containing ELF relocation info.  We need this hack in order to
       be able to generate ELF binaries that can be translated into
       EFI applications (which are essentially COFF objects).  Those
       files contain a COFF ".reloc" section inside an ELFNN object,
       which would normally cause BFD to segfault because it would
       attempt to interpret this section as containing relocation
       entries for section "oc".  With this hack enabled, ".reloc"
       will be treated as a normal data section, which will avoid the
       segfault.  However, you won't be able to create an ELFNN binary
       with a section named "oc" that needs relocations, but that's
       the kind of ugly side-effects you get when detecting section
       types based on their names...  In practice, this limitation is
       unlikely to bite.  */
    hdr->sh_type = SHT_PROGBITS;

  if (sec->flags & SEC_SMALL_DATA)
    hdr->sh_flags |= SHF_IA_64_SHORT;

  /* Some HP linkers look for the SHF_IA_64_HP_TLS flag instead of SHF_TLS. */

  if (elfNN_ia64_hpux_vec (abfd->xvec) && (sec->flags & SHF_TLS))
    hdr->sh_flags |= SHF_IA_64_HP_TLS;

  return TRUE;
}

/* The final processing done just before writing out an IA-64 ELF
   object file.  */

static void
elfNN_ia64_final_write_processing (abfd, linker)
     bfd *abfd;
     bfd_boolean linker ATTRIBUTE_UNUSED;
{
  Elf_Internal_Shdr *hdr;
  asection *s;

  for (s = abfd->sections; s; s = s->next)
    {
      hdr = &elf_section_data (s)->this_hdr;
      switch (hdr->sh_type)
	{
	case SHT_IA_64_UNWIND:
	  /* The IA-64 processor-specific ABI requires setting sh_link
	     to the unwind section, whereas HP-UX requires sh_info to
	     do so.  For maximum compatibility, we'll set both for
	     now... */
	  hdr->sh_info = hdr->sh_link;
	  break;
	}
    }

  if (! elf_flags_init (abfd))
    {
      unsigned long flags = 0;

      if (abfd->xvec->byteorder == BFD_ENDIAN_BIG)
	flags |= EF_IA_64_BE;
      if (bfd_get_mach (abfd) == bfd_mach_ia64_elf64)
	flags |= EF_IA_64_ABI64;

      elf_elfheader(abfd)->e_flags = flags;
      elf_flags_init (abfd) = TRUE;
    }
}

/* Hook called by the linker routine which adds symbols from an object
   file.  We use it to put .comm items in .sbss, and not .bss.  */

static bfd_boolean
elfNN_ia64_add_symbol_hook (abfd, info, sym, namep, flagsp, secp, valp)
     bfd *abfd;
     struct bfd_link_info *info;
     Elf_Internal_Sym *sym;
     const char **namep ATTRIBUTE_UNUSED;
     flagword *flagsp ATTRIBUTE_UNUSED;
     asection **secp;
     bfd_vma *valp;
{
  if (sym->st_shndx == SHN_COMMON
      && !info->relocatable
      && sym->st_size <= elf_gp_size (abfd))
    {
      /* Common symbols less than or equal to -G nn bytes are
	 automatically put into .sbss.  */

      asection *scomm = bfd_get_section_by_name (abfd, ".scommon");

      if (scomm == NULL)
	{
	  scomm = bfd_make_section_with_flags (abfd, ".scommon",
					       (SEC_ALLOC
						| SEC_IS_COMMON
						| SEC_LINKER_CREATED));
	  if (scomm == NULL)
	    return FALSE;
	}

      *secp = scomm;
      *valp = sym->st_size;
    }

  return TRUE;
}

/* Return the number of additional phdrs we will need.  */

static int
elfNN_ia64_additional_program_headers (bfd *abfd,
				       struct bfd_link_info *info ATTRIBUTE_UNUSED)
{
  asection *s;
  int ret = 0;

  /* See if we need a PT_IA_64_ARCHEXT segment.  */
  s = bfd_get_section_by_name (abfd, ELF_STRING_ia64_archext);
  if (s && (s->flags & SEC_LOAD))
    ++ret;

  /* Count how many PT_IA_64_UNWIND segments we need.  */
  for (s = abfd->sections; s; s = s->next)
    if (is_unwind_section_name (abfd, s->name) && (s->flags & SEC_LOAD))
      ++ret;

  return ret;
}

static bfd_boolean
elfNN_ia64_modify_segment_map (bfd *abfd,
			       struct bfd_link_info *info ATTRIBUTE_UNUSED)
{
  struct elf_segment_map *m, **pm;
  Elf_Internal_Shdr *hdr;
  asection *s;

  /* If we need a PT_IA_64_ARCHEXT segment, it must come before
     all PT_LOAD segments.  */
  s = bfd_get_section_by_name (abfd, ELF_STRING_ia64_archext);
  if (s && (s->flags & SEC_LOAD))
    {
      for (m = elf_tdata (abfd)->segment_map; m != NULL; m = m->next)
	if (m->p_type == PT_IA_64_ARCHEXT)
	  break;
      if (m == NULL)
	{
	  m = ((struct elf_segment_map *)
	       bfd_zalloc (abfd, (bfd_size_type) sizeof *m));
	  if (m == NULL)
	    return FALSE;

	  m->p_type = PT_IA_64_ARCHEXT;
	  m->count = 1;
	  m->sections[0] = s;

	  /* We want to put it after the PHDR and INTERP segments.  */
	  pm = &elf_tdata (abfd)->segment_map;
	  while (*pm != NULL
		 && ((*pm)->p_type == PT_PHDR
		     || (*pm)->p_type == PT_INTERP))
	    pm = &(*pm)->next;

	  m->next = *pm;
	  *pm = m;
	}
    }

  /* Install PT_IA_64_UNWIND segments, if needed.  */
  for (s = abfd->sections; s; s = s->next)
    {
      hdr = &elf_section_data (s)->this_hdr;
      if (hdr->sh_type != SHT_IA_64_UNWIND)
	continue;

      if (s && (s->flags & SEC_LOAD))
	{
	  for (m = elf_tdata (abfd)->segment_map; m != NULL; m = m->next)
	    if (m->p_type == PT_IA_64_UNWIND)
	      {
		int i;

		/* Look through all sections in the unwind segment
		   for a match since there may be multiple sections
		   to a segment.  */
		for (i = m->count - 1; i >= 0; --i)
		  if (m->sections[i] == s)
		    break;

		if (i >= 0)
		  break;
	      }

	  if (m == NULL)
	    {
	      m = ((struct elf_segment_map *)
		   bfd_zalloc (abfd, (bfd_size_type) sizeof *m));
	      if (m == NULL)
		return FALSE;

	      m->p_type = PT_IA_64_UNWIND;
	      m->count = 1;
	      m->sections[0] = s;
	      m->next = NULL;

	      /* We want to put it last.  */
	      pm = &elf_tdata (abfd)->segment_map;
	      while (*pm != NULL)
		pm = &(*pm)->next;
	      *pm = m;
	    }
	}
    }

  return TRUE;
}

/* Turn on PF_IA_64_NORECOV if needed.  This involves traversing all of
   the input sections for each output section in the segment and testing
   for SHF_IA_64_NORECOV on each.  */

static bfd_boolean
elfNN_ia64_modify_program_headers (bfd *abfd,
				   struct bfd_link_info *info ATTRIBUTE_UNUSED)
{
  struct elf_obj_tdata *tdata = elf_tdata (abfd);
  struct elf_segment_map *m;
  Elf_Internal_Phdr *p;

  for (p = tdata->phdr, m = tdata->segment_map; m != NULL; m = m->next, p++)
    if (m->p_type == PT_LOAD)
      {
	int i;
	for (i = m->count - 1; i >= 0; --i)
	  {
	    struct bfd_link_order *order = m->sections[i]->map_head.link_order;

	    while (order != NULL)
	      {
		if (order->type == bfd_indirect_link_order)
		  {
		    asection *is = order->u.indirect.section;
		    bfd_vma flags = elf_section_data(is)->this_hdr.sh_flags;
		    if (flags & SHF_IA_64_NORECOV)
		      {
			p->p_flags |= PF_IA_64_NORECOV;
			goto found;
		      }
		  }
		order = order->next;
	      }
	  }
      found:;
      }

  return TRUE;
}

/* According to the Tahoe assembler spec, all labels starting with a
   '.' are local.  */

static bfd_boolean
elfNN_ia64_is_local_label_name (abfd, name)
     bfd *abfd ATTRIBUTE_UNUSED;
     const char *name;
{
  return name[0] == '.';
}

/* Should we do dynamic things to this symbol?  */

static bfd_boolean
elfNN_ia64_dynamic_symbol_p (h, info, r_type)
     struct elf_link_hash_entry *h;
     struct bfd_link_info *info;
     int r_type;
{
  bfd_boolean ignore_protected
    = ((r_type & 0xf8) == 0x40		/* FPTR relocs */
       || (r_type & 0xf8) == 0x50);	/* LTOFF_FPTR relocs */

  return _bfd_elf_dynamic_symbol_p (h, info, ignore_protected);
}

static struct bfd_hash_entry*
elfNN_ia64_new_elf_hash_entry (entry, table, string)
     struct bfd_hash_entry *entry;
     struct bfd_hash_table *table;
     const char *string;
{
  struct elfNN_ia64_link_hash_entry *ret;
  ret = (struct elfNN_ia64_link_hash_entry *) entry;

  /* Allocate the structure if it has not already been allocated by a
     subclass.  */
  if (!ret)
    ret = bfd_hash_allocate (table, sizeof (*ret));

  if (!ret)
    return 0;

  /* Call the allocation method of the superclass.  */
  ret = ((struct elfNN_ia64_link_hash_entry *)
	 _bfd_elf_link_hash_newfunc ((struct bfd_hash_entry *) ret,
				     table, string));

  ret->info = NULL;
  ret->count = 0;
  ret->sorted_count = 0;
  ret->size = 0;
  return (struct bfd_hash_entry *) ret;
}

static void
elfNN_ia64_hash_copy_indirect (info, xdir, xind)
     struct bfd_link_info *info;
     struct elf_link_hash_entry *xdir, *xind;
{
  struct elfNN_ia64_link_hash_entry *dir, *ind;

  dir = (struct elfNN_ia64_link_hash_entry *) xdir;
  ind = (struct elfNN_ia64_link_hash_entry *) xind;

  /* Copy down any references that we may have already seen to the
     symbol which just became indirect.  */

  dir->root.ref_dynamic |= ind->root.ref_dynamic;
  dir->root.ref_regular |= ind->root.ref_regular;
  dir->root.ref_regular_nonweak |= ind->root.ref_regular_nonweak;
  dir->root.needs_plt |= ind->root.needs_plt;

  if (ind->root.root.type != bfd_link_hash_indirect)
    return;

  /* Copy over the got and plt data.  This would have been done
     by check_relocs.  */

  if (ind->info != NULL)
    {
      struct elfNN_ia64_dyn_sym_info *dyn_i;
      unsigned int count;

      if (dir->info)
	free (dir->info);

      dir->info = ind->info;
      dir->count = ind->count;
      dir->sorted_count = ind->sorted_count;
      dir->size = ind->size;

      ind->info = NULL;
      ind->count = 0;
      ind->sorted_count = 0;
      ind->size = 0;

      /* Fix up the dyn_sym_info pointers to the global symbol.  */
      for (count = dir->count, dyn_i = dir->info;
	   count != 0;
	   count--, dyn_i++)
	dyn_i->h = &dir->root;
    }

  /* Copy over the dynindx.  */

  if (ind->root.dynindx != -1)
    {
      if (dir->root.dynindx != -1)
	_bfd_elf_strtab_delref (elf_hash_table (info)->dynstr,
				dir->root.dynstr_index);
      dir->root.dynindx = ind->root.dynindx;
      dir->root.dynstr_index = ind->root.dynstr_index;
      ind->root.dynindx = -1;
      ind->root.dynstr_index = 0;
    }
}

static void
elfNN_ia64_hash_hide_symbol (info, xh, force_local)
     struct bfd_link_info *info;
     struct elf_link_hash_entry *xh;
     bfd_boolean force_local;
{
  struct elfNN_ia64_link_hash_entry *h;
  struct elfNN_ia64_dyn_sym_info *dyn_i;
  unsigned int count;

  h = (struct elfNN_ia64_link_hash_entry *)xh;

  _bfd_elf_link_hash_hide_symbol (info, &h->root, force_local);

  for (count = h->count, dyn_i = h->info;
       count != 0;
       count--, dyn_i++)
    {
      dyn_i->want_plt2 = 0;
      dyn_i->want_plt = 0;
    }
}

/* Compute a hash of a local hash entry.  */

static hashval_t
elfNN_ia64_local_htab_hash (ptr)
     const void *ptr;
{
  struct elfNN_ia64_local_hash_entry *entry
    = (struct elfNN_ia64_local_hash_entry *) ptr;

  return (((entry->id & 0xff) << 24) | ((entry->id & 0xff00) << 8))
	  ^ entry->r_sym ^ (entry->id >> 16);
}

/* Compare local hash entries.  */

static int
elfNN_ia64_local_htab_eq (ptr1, ptr2)
     const void *ptr1, *ptr2;
{
  struct elfNN_ia64_local_hash_entry *entry1
    = (struct elfNN_ia64_local_hash_entry *) ptr1;
  struct elfNN_ia64_local_hash_entry *entry2
    = (struct elfNN_ia64_local_hash_entry *) ptr2;

  return entry1->id == entry2->id && entry1->r_sym == entry2->r_sym;
}

/* Create the derived linker hash table.  The IA-64 ELF port uses this
   derived hash table to keep information specific to the IA-64 ElF
   linker (without using static variables).  */

static struct bfd_link_hash_table*
elfNN_ia64_hash_table_create (abfd)
     bfd *abfd;
{
  struct elfNN_ia64_link_hash_table *ret;

  ret = bfd_zmalloc ((bfd_size_type) sizeof (*ret));
  if (!ret)
    return 0;

  if (!_bfd_elf_link_hash_table_init (&ret->root, abfd,
				      elfNN_ia64_new_elf_hash_entry,
				      sizeof (struct elfNN_ia64_link_hash_entry)))
    {
      free (ret);
      return 0;
    }

  ret->loc_hash_table = htab_try_create (1024, elfNN_ia64_local_htab_hash,
					 elfNN_ia64_local_htab_eq, NULL);
  ret->loc_hash_memory = objalloc_create ();
  if (!ret->loc_hash_table || !ret->loc_hash_memory)
    {
      free (ret);
      return 0;
    }

  return &ret->root.root;
}

/* Free the global elfNN_ia64_dyn_sym_info array.  */

static bfd_boolean
elfNN_ia64_global_dyn_info_free (void **xentry,
				PTR unused ATTRIBUTE_UNUSED)
{
  struct elfNN_ia64_link_hash_entry *entry
    = (struct elfNN_ia64_link_hash_entry *) xentry;

  if (entry->root.root.type == bfd_link_hash_warning)
    entry = (struct elfNN_ia64_link_hash_entry *) entry->root.root.u.i.link;

  if (entry->info)
    {
      free (entry->info);
      entry->info = NULL;
      entry->count = 0;
      entry->sorted_count = 0;
      entry->size = 0;
    }

  return TRUE;
}

/* Free the local elfNN_ia64_dyn_sym_info array.  */

static bfd_boolean
elfNN_ia64_local_dyn_info_free (void **slot,
				PTR unused ATTRIBUTE_UNUSED)
{
  struct elfNN_ia64_local_hash_entry *entry
    = (struct elfNN_ia64_local_hash_entry *) *slot;

  if (entry->info)
    {
      free (entry->info);
      entry->info = NULL;
      entry->count = 0;
      entry->sorted_count = 0;
      entry->size = 0;
    }

  return TRUE;
}

/* Destroy IA-64 linker hash table.  */

static void
elfNN_ia64_hash_table_free (hash)
     struct bfd_link_hash_table *hash;
{
  struct elfNN_ia64_link_hash_table *ia64_info
    = (struct elfNN_ia64_link_hash_table *) hash;
  if (ia64_info->loc_hash_table)
    {
      htab_traverse (ia64_info->loc_hash_table,
		     elfNN_ia64_local_dyn_info_free, NULL);
      htab_delete (ia64_info->loc_hash_table);
    }
  if (ia64_info->loc_hash_memory)
    objalloc_free ((struct objalloc *) ia64_info->loc_hash_memory);
  elf_link_hash_traverse (&ia64_info->root,
			  elfNN_ia64_global_dyn_info_free, NULL);
  _bfd_generic_link_hash_table_free (hash);
}

/* Traverse both local and global hash tables.  */

struct elfNN_ia64_dyn_sym_traverse_data
{
  bfd_boolean (*func) PARAMS ((struct elfNN_ia64_dyn_sym_info *, PTR));
  PTR data;
};

static bfd_boolean
elfNN_ia64_global_dyn_sym_thunk (xentry, xdata)
     struct bfd_hash_entry *xentry;
     PTR xdata;
{
  struct elfNN_ia64_link_hash_entry *entry
    = (struct elfNN_ia64_link_hash_entry *) xentry;
  struct elfNN_ia64_dyn_sym_traverse_data *data
    = (struct elfNN_ia64_dyn_sym_traverse_data *) xdata;
  struct elfNN_ia64_dyn_sym_info *dyn_i;
  unsigned int count;

  if (entry->root.root.type == bfd_link_hash_warning)
    entry = (struct elfNN_ia64_link_hash_entry *) entry->root.root.u.i.link;

  for (count = entry->count, dyn_i = entry->info;
       count != 0;
       count--, dyn_i++)
    if (! (*data->func) (dyn_i, data->data))
      return FALSE;
  return TRUE;
}

static bfd_boolean
elfNN_ia64_local_dyn_sym_thunk (slot, xdata)
     void **slot;
     PTR xdata;
{
  struct elfNN_ia64_local_hash_entry *entry
    = (struct elfNN_ia64_local_hash_entry *) *slot;
  struct elfNN_ia64_dyn_sym_traverse_data *data
    = (struct elfNN_ia64_dyn_sym_traverse_data *) xdata;
  struct elfNN_ia64_dyn_sym_info *dyn_i;
  unsigned int count;

  for (count = entry->count, dyn_i = entry->info;
       count != 0;
       count--, dyn_i++)
    if (! (*data->func) (dyn_i, data->data))
      return FALSE;
  return TRUE;
}

static void
elfNN_ia64_dyn_sym_traverse (ia64_info, func, data)
     struct elfNN_ia64_link_hash_table *ia64_info;
     bfd_boolean (*func) PARAMS ((struct elfNN_ia64_dyn_sym_info *, PTR));
     PTR data;
{
  struct elfNN_ia64_dyn_sym_traverse_data xdata;

  xdata.func = func;
  xdata.data = data;

  elf_link_hash_traverse (&ia64_info->root,
			  elfNN_ia64_global_dyn_sym_thunk, &xdata);
  htab_traverse (ia64_info->loc_hash_table,
		 elfNN_ia64_local_dyn_sym_thunk, &xdata);
}

static bfd_boolean
elfNN_ia64_create_dynamic_sections (abfd, info)
     bfd *abfd;
     struct bfd_link_info *info;
{
  struct elfNN_ia64_link_hash_table *ia64_info;
  asection *s;

  if (! _bfd_elf_create_dynamic_sections (abfd, info))
    return FALSE;

  ia64_info = elfNN_ia64_hash_table (info);

  ia64_info->plt_sec = bfd_get_section_by_name (abfd, ".plt");
  ia64_info->got_sec = bfd_get_section_by_name (abfd, ".got");

  {
    flagword flags = bfd_get_section_flags (abfd, ia64_info->got_sec);
    bfd_set_section_flags (abfd, ia64_info->got_sec, SEC_SMALL_DATA | flags);
    /* The .got section is always aligned at 8 bytes.  */
    bfd_set_section_alignment (abfd, ia64_info->got_sec, 3);
  }

  if (!get_pltoff (abfd, info, ia64_info))
    return FALSE;

  s = bfd_make_section_with_flags (abfd, ".rela.IA_64.pltoff",
				   (SEC_ALLOC | SEC_LOAD
				    | SEC_HAS_CONTENTS
				    | SEC_IN_MEMORY
				    | SEC_LINKER_CREATED
				    | SEC_READONLY));
  if (s == NULL
      || !bfd_set_section_alignment (abfd, s, LOG_SECTION_ALIGN))
    return FALSE;
  ia64_info->rel_pltoff_sec = s;

  s = bfd_make_section_with_flags (abfd, ".rela.got",
				   (SEC_ALLOC | SEC_LOAD
				    | SEC_HAS_CONTENTS
				    | SEC_IN_MEMORY
				    | SEC_LINKER_CREATED
				    | SEC_READONLY));
  if (s == NULL
      || !bfd_set_section_alignment (abfd, s, LOG_SECTION_ALIGN))
    return FALSE;
  ia64_info->rel_got_sec = s;

  return TRUE;
}

/* Find and/or create a hash entry for local symbol.  */
static struct elfNN_ia64_local_hash_entry *
get_local_sym_hash (ia64_info, abfd, rel, create)
     struct elfNN_ia64_link_hash_table *ia64_info;
     bfd *abfd;
     const Elf_Internal_Rela *rel;
     bfd_boolean create;
{
  struct elfNN_ia64_local_hash_entry e, *ret;
  asection *sec = abfd->sections;
  hashval_t h = (((sec->id & 0xff) << 24) | ((sec->id & 0xff00) << 8))
		^ ELFNN_R_SYM (rel->r_info) ^ (sec->id >> 16);
  void **slot;

  e.id = sec->id;
  e.r_sym = ELFNN_R_SYM (rel->r_info);
  slot = htab_find_slot_with_hash (ia64_info->loc_hash_table, &e, h,
				   create ? INSERT : NO_INSERT);

  if (!slot)
    return NULL;

  if (*slot)
    return (struct elfNN_ia64_local_hash_entry *) *slot;

  ret = (struct elfNN_ia64_local_hash_entry *)
	objalloc_alloc ((struct objalloc *) ia64_info->loc_hash_memory,
			sizeof (struct elfNN_ia64_local_hash_entry));
  if (ret)
    {
      memset (ret, 0, sizeof (*ret));
      ret->id = sec->id;
      ret->r_sym = ELFNN_R_SYM (rel->r_info);
      *slot = ret;
    }
  return ret;
}

/* Used to sort elfNN_ia64_dyn_sym_info array.  */

static int
addend_compare (const void *xp, const void *yp)
{
  const struct elfNN_ia64_dyn_sym_info *x
    = (const struct elfNN_ia64_dyn_sym_info *) xp;
  const struct elfNN_ia64_dyn_sym_info *y
    = (const struct elfNN_ia64_dyn_sym_info *) yp;

  return x->addend < y->addend ? -1 : x->addend > y->addend ? 1 : 0;
}

/* Sort elfNN_ia64_dyn_sym_info array and remove duplicates.  */

static unsigned int
sort_dyn_sym_info (struct elfNN_ia64_dyn_sym_info *info,
		   unsigned int count)
{
  bfd_vma curr, prev, got_offset;
  unsigned int i, kept, dup, diff, dest, src, len;

  qsort (info, count, sizeof (*info), addend_compare);

  /* Find the first duplicate.  */
  prev = info [0].addend;
  got_offset = info [0].got_offset;
  for (i = 1; i < count; i++)
    {
      curr = info [i].addend;
      if (curr == prev)
	{
	  /* For duplicates, make sure that GOT_OFFSET is valid.  */
	  if (got_offset == (bfd_vma) -1)
	    got_offset = info [i].got_offset;
	  break;
	}
      got_offset = info [i].got_offset;
      prev = curr;
    }

  /* We may move a block of elements to here.  */
  dest = i++;

  /* Remove duplicates.  */
  if (i < count)
    {
      while (i < count)
	{
	  /* For duplicates, make sure that the kept one has a valid
	     got_offset.  */
	  kept = dest - 1;
	  if (got_offset != (bfd_vma) -1)
	    info [kept].got_offset = got_offset;

	  curr = info [i].addend;
	  got_offset = info [i].got_offset;

	  /* Move a block of elements whose first one is different from
	     the previous.  */
	  if (curr == prev)
	    {
	      for (src = i + 1; src < count; src++)
		{
		  if (info [src].addend != curr)
		    break;
		  /* For duplicates, make sure that GOT_OFFSET is
		     valid.  */
		  if (got_offset == (bfd_vma) -1)
		    got_offset = info [src].got_offset;
		}

	      /* Make sure that the kept one has a valid got_offset.  */
	      if (got_offset != (bfd_vma) -1)
		info [kept].got_offset = got_offset;
	    }
	  else
	    src = i;

	  if (src >= count)
	    break;

	  /* Find the next duplicate.  SRC will be kept.  */
	  prev = info [src].addend;
	  got_offset = info [src].got_offset;
	  for (dup = src + 1; dup < count; dup++)
	    {
	      curr = info [dup].addend;
	      if (curr == prev)
		{
		  /* Make sure that got_offset is valid.  */
		  if (got_offset == (bfd_vma) -1)
		    got_offset = info [dup].got_offset;

		  /* For duplicates, make sure that the kept one has
		     a valid got_offset.  */
		  if (got_offset != (bfd_vma) -1)
		    info [dup - 1].got_offset = got_offset;
		  break;
		}
	      got_offset = info [dup].got_offset;
	      prev = curr;
	    }

	  /* How much to move.  */
	  len = dup - src;
	  i = dup + 1;

	  if (len == 1 && dup < count)
	    {
	      /* If we only move 1 element, we combine it with the next
		 one.  There must be at least a duplicate.  Find the
		 next different one.  */
	      for (diff = dup + 1, src++; diff < count; diff++, src++)
		{
		  if (info [diff].addend != curr)
		    break;
		  /* Make sure that got_offset is valid.  */
		  if (got_offset == (bfd_vma) -1)
		    got_offset = info [diff].got_offset;
		}

	      /* Makre sure that the last duplicated one has an valid
		 offset.  */
	      BFD_ASSERT (curr == prev);
	      if (got_offset != (bfd_vma) -1)
		info [diff - 1].got_offset = got_offset;

	      if (diff < count)
		{
		  /* Find the next duplicate.  Track the current valid
		     offset.  */
		  prev = info [diff].addend;
		  got_offset = info [diff].got_offset;
		  for (dup = diff + 1; dup < count; dup++)
		    {
		      curr = info [dup].addend;
		      if (curr == prev)
			{
			  /* For duplicates, make sure that GOT_OFFSET
			     is valid.  */
			  if (got_offset == (bfd_vma) -1)
			    got_offset = info [dup].got_offset;
			  break;
			}
		      got_offset = info [dup].got_offset;
		      prev = curr;
		      diff++;
		    }

		  len = diff - src + 1;
		  i = diff + 1;
		}
	    }

	  memmove (&info [dest], &info [src], len * sizeof (*info));

	  dest += len;
	}

      count = dest;
    }
  else
    {
      /* When we get here, either there is no duplicate at all or
	 the only duplicate is the last element.  */
      if (dest < count)
	{
	  /* If the last element is a duplicate, make sure that the
	     kept one has a valid got_offset.  We also update count.  */
	  if (got_offset != (bfd_vma) -1)
	    info [dest - 1].got_offset = got_offset;
	  count = dest;
	}
    }

  return count;
}

/* Find and/or create a descriptor for dynamic symbol info.  This will
   vary based on global or local symbol, and the addend to the reloc.

   We don't sort when inserting.  Also, we sort and eliminate
   duplicates if there is an unsorted section.  Typically, this will
   only happen once, because we do all insertions before lookups.  We
   then use bsearch to do a lookup.  This also allows lookups to be
   fast.  So we have fast insertion (O(log N) due to duplicate check),
   fast lookup (O(log N)) and one sort (O(N log N) expected time).
   Previously, all lookups were O(N) because of the use of the linked
   list and also all insertions were O(N) because of the check for
   duplicates.  There are some complications here because the array
   size grows occasionally, which may add an O(N) factor, but this
   should be rare.  Also,  we free the excess array allocation, which
   requires a copy which is O(N), but this only happens once.  */

static struct elfNN_ia64_dyn_sym_info *
get_dyn_sym_info (ia64_info, h, abfd, rel, create)
     struct elfNN_ia64_link_hash_table *ia64_info;
     struct elf_link_hash_entry *h;
     bfd *abfd;
     const Elf_Internal_Rela *rel;
     bfd_boolean create;
{
  struct elfNN_ia64_dyn_sym_info **info_p, *info, *dyn_i, key;
  unsigned int *count_p, *sorted_count_p, *size_p;
  unsigned int count, sorted_count, size;
  bfd_vma addend = rel ? rel->r_addend : 0;
  bfd_size_type amt;

  if (h)
    {
      struct elfNN_ia64_link_hash_entry *global_h;

      global_h = (struct elfNN_ia64_link_hash_entry *) h;
      info_p = &global_h->info;
      count_p = &global_h->count;
      sorted_count_p = &global_h->sorted_count;
      size_p = &global_h->size;
    }
  else
    {
      struct elfNN_ia64_local_hash_entry *loc_h;

      loc_h = get_local_sym_hash (ia64_info, abfd, rel, create);
      if (!loc_h)
	{
	  BFD_ASSERT (!create);
	  return NULL;
	}

      info_p = &loc_h->info;
      count_p = &loc_h->count;
      sorted_count_p = &loc_h->sorted_count;
      size_p = &loc_h->size;
    }

  count = *count_p;
  sorted_count = *sorted_count_p;
  size = *size_p;
  info = *info_p;
  if (create)
    {
      /* When we create the array, we don't check for duplicates,
         except in the previously sorted section if one exists, and
	 against the last inserted entry.  This allows insertions to
	 be fast.  */
      if (info)
	{
	  if (sorted_count)
	    {
	      /* Try bsearch first on the sorted section.  */
	      key.addend = addend;
	      dyn_i = bsearch (&key, info, sorted_count,
			       sizeof (*info), addend_compare);

	      if (dyn_i)
		{
		  return dyn_i;
		}
	    }

	  /* Do a quick check for the last inserted entry.  */
	  dyn_i = info + count - 1;
	  if (dyn_i->addend == addend)
	    {
	      return dyn_i;
	    }
	}

      if (size == 0)
	{
	  /* It is the very first element. We create the array of size
	     1.  */
	  size = 1;
	  amt = size * sizeof (*info);
	  info = bfd_malloc (amt);
	}
      else if (size <= count)
	{
	  /* We double the array size every time when we reach the
	     size limit.  */
	  size += size;
	  amt = size * sizeof (*info);
	  info = bfd_realloc (info, amt);
	}
      else
	goto has_space;

      if (info == NULL)
	return NULL;
      *size_p = size;
      *info_p = info;

has_space:
      /* Append the new one to the array.  */
      dyn_i = info + count;
      memset (dyn_i, 0, sizeof (*dyn_i));
      dyn_i->got_offset = (bfd_vma) -1;
      dyn_i->addend = addend;
      
      /* We increment count only since the new ones are unsorted and
	 may have duplicate.  */
      (*count_p)++;
    }
  else
    {
      /* It is a lookup without insertion.  Sort array if part of the
	 array isn't sorted.  */
      if (count != sorted_count)
	{
	  count = sort_dyn_sym_info (info, count);
	  *count_p = count;
	  *sorted_count_p = count;
	}

      /* Free unused memory.  */
      if (size != count)
	{
	  amt = count * sizeof (*info);
	  info = bfd_malloc (amt);
	  if (info != NULL)
	    {
	      memcpy (info, *info_p, amt);
	      free (*info_p);
	      *size_p = count;
	      *info_p = info;
	    }
	}

      key.addend = addend;
      dyn_i = bsearch (&key, info, count,
		       sizeof (*info), addend_compare);
    }

  return dyn_i;
}

static asection *
get_got (abfd, info, ia64_info)
     bfd *abfd;
     struct bfd_link_info *info;
     struct elfNN_ia64_link_hash_table *ia64_info;
{
  asection *got;
  bfd *dynobj;

  got = ia64_info->got_sec;
  if (!got)
    {
      flagword flags;

      dynobj = ia64_info->root.dynobj;
      if (!dynobj)
	ia64_info->root.dynobj = dynobj = abfd;
      if (!_bfd_elf_create_got_section (dynobj, info))
	return 0;

      got = bfd_get_section_by_name (dynobj, ".got");
      BFD_ASSERT (got);
      ia64_info->got_sec = got;

      /* The .got section is always aligned at 8 bytes.  */
      if (!bfd_set_section_alignment (abfd, got, 3))
	return 0;

      flags = bfd_get_section_flags (abfd, got);
      bfd_set_section_flags (abfd, got, SEC_SMALL_DATA | flags);
    }

  return got;
}

/* Create function descriptor section (.opd).  This section is called .opd
   because it contains "official procedure descriptors".  The "official"
   refers to the fact that these descriptors are used when taking the address
   of a procedure, thus ensuring a unique address for each procedure.  */

static asection *
get_fptr (abfd, info, ia64_info)
     bfd *abfd;
     struct bfd_link_info *info;
     struct elfNN_ia64_link_hash_table *ia64_info;
{
  asection *fptr;
  bfd *dynobj;

  fptr = ia64_info->fptr_sec;
  if (!fptr)
    {
      dynobj = ia64_info->root.dynobj;
      if (!dynobj)
	ia64_info->root.dynobj = dynobj = abfd;

      fptr = bfd_make_section_with_flags (dynobj, ".opd",
					  (SEC_ALLOC
					   | SEC_LOAD
					   | SEC_HAS_CONTENTS
					   | SEC_IN_MEMORY
					   | (info->pie ? 0 : SEC_READONLY)
					   | SEC_LINKER_CREATED));
      if (!fptr
	  || !bfd_set_section_alignment (abfd, fptr, 4))
	{
	  BFD_ASSERT (0);
	  return NULL;
	}

      ia64_info->fptr_sec = fptr;

      if (info->pie)
	{
	  asection *fptr_rel;
	  fptr_rel = bfd_make_section_with_flags (dynobj, ".rela.opd",
						  (SEC_ALLOC | SEC_LOAD
						   | SEC_HAS_CONTENTS
						   | SEC_IN_MEMORY
						   | SEC_LINKER_CREATED
						   | SEC_READONLY));
	  if (fptr_rel == NULL
	      || !bfd_set_section_alignment (abfd, fptr_rel,
					     LOG_SECTION_ALIGN))
	    {
	      BFD_ASSERT (0);
	      return NULL;
	    }

	  ia64_info->rel_fptr_sec = fptr_rel;
	}
    }

  return fptr;
}

static asection *
get_pltoff (abfd, info, ia64_info)
     bfd *abfd;
     struct bfd_link_info *info ATTRIBUTE_UNUSED;
     struct elfNN_ia64_link_hash_table *ia64_info;
{
  asection *pltoff;
  bfd *dynobj;

  pltoff = ia64_info->pltoff_sec;
  if (!pltoff)
    {
      dynobj = ia64_info->root.dynobj;
      if (!dynobj)
	ia64_info->root.dynobj = dynobj = abfd;

      pltoff = bfd_make_section_with_flags (dynobj,
					    ELF_STRING_ia64_pltoff,
					    (SEC_ALLOC
					     | SEC_LOAD
					     | SEC_HAS_CONTENTS
					     | SEC_IN_MEMORY
					     | SEC_SMALL_DATA
					     | SEC_LINKER_CREATED));
      if (!pltoff
	  || !bfd_set_section_alignment (abfd, pltoff, 4))
	{
	  BFD_ASSERT (0);
	  return NULL;
	}

      ia64_info->pltoff_sec = pltoff;
    }

  return pltoff;
}

static asection *
get_reloc_section (abfd, ia64_info, sec, create)
     bfd *abfd;
     struct elfNN_ia64_link_hash_table *ia64_info;
     asection *sec;
     bfd_boolean create;
{
  const char *srel_name;
  asection *srel;
  bfd *dynobj;

  srel_name = (bfd_elf_string_from_elf_section
	       (abfd, elf_elfheader(abfd)->e_shstrndx,
		elf_section_data(sec)->rel_hdr.sh_name));
  if (srel_name == NULL)
    return NULL;

  BFD_ASSERT ((CONST_STRNEQ (srel_name, ".rela")
	       && strcmp (bfd_get_section_name (abfd, sec),
			  srel_name+5) == 0)
	      || (CONST_STRNEQ (srel_name, ".rel")
		  && strcmp (bfd_get_section_name (abfd, sec),
			     srel_name+4) == 0));

  dynobj = ia64_info->root.dynobj;
  if (!dynobj)
    ia64_info->root.dynobj = dynobj = abfd;

  srel = bfd_get_section_by_name (dynobj, srel_name);
  if (srel == NULL && create)
    {
      srel = bfd_make_section_with_flags (dynobj, srel_name,
					  (SEC_ALLOC | SEC_LOAD
					   | SEC_HAS_CONTENTS
					   | SEC_IN_MEMORY
					   | SEC_LINKER_CREATED
					   | SEC_READONLY));
      if (srel == NULL
	  || !bfd_set_section_alignment (dynobj, srel,
					 LOG_SECTION_ALIGN))
	return NULL;
    }

  return srel;
}

static bfd_boolean
count_dyn_reloc (bfd *abfd, struct elfNN_ia64_dyn_sym_info *dyn_i,
		 asection *srel, int type, bfd_boolean reltext)
{
  struct elfNN_ia64_dyn_reloc_entry *rent;

  for (rent = dyn_i->reloc_entries; rent; rent = rent->next)
    if (rent->srel == srel && rent->type == type)
      break;

  if (!rent)
    {
      rent = ((struct elfNN_ia64_dyn_reloc_entry *)
	      bfd_alloc (abfd, (bfd_size_type) sizeof (*rent)));
      if (!rent)
	return FALSE;

      rent->next = dyn_i->reloc_entries;
      rent->srel = srel;
      rent->type = type;
      rent->count = 0;
      dyn_i->reloc_entries = rent;
    }
  rent->reltext = reltext;
  rent->count++;

  return TRUE;
}

static bfd_boolean
elfNN_ia64_check_relocs (abfd, info, sec, relocs)
     bfd *abfd;
     struct bfd_link_info *info;
     asection *sec;
     const Elf_Internal_Rela *relocs;
{
  struct elfNN_ia64_link_hash_table *ia64_info;
  const Elf_Internal_Rela *relend;
  Elf_Internal_Shdr *symtab_hdr;
  const Elf_Internal_Rela *rel;
  asection *got, *fptr, *srel, *pltoff;
  enum {
    NEED_GOT = 1,
    NEED_GOTX = 2,
    NEED_FPTR = 4,
    NEED_PLTOFF = 8,
    NEED_MIN_PLT = 16,
    NEED_FULL_PLT = 32,
    NEED_DYNREL = 64,
    NEED_LTOFF_FPTR = 128,
    NEED_TPREL = 256,
    NEED_DTPMOD = 512,
    NEED_DTPREL = 1024
  };
  int need_entry;
  struct elf_link_hash_entry *h;
  unsigned long r_symndx;
  bfd_boolean maybe_dynamic;

  if (info->relocatable)
    return TRUE;

  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;
  ia64_info = elfNN_ia64_hash_table (info);

  got = fptr = srel = pltoff = NULL;

  relend = relocs + sec->reloc_count;

  /* We scan relocations first to create dynamic relocation arrays.  We
     modified get_dyn_sym_info to allow fast insertion and support fast
     lookup in the next loop.  */
  for (rel = relocs; rel < relend; ++rel)
    {
      r_symndx = ELFNN_R_SYM (rel->r_info);
      if (r_symndx >= symtab_hdr->sh_info)
	{
	  long indx = r_symndx - symtab_hdr->sh_info;
	  h = elf_sym_hashes (abfd)[indx];
	  while (h->root.type == bfd_link_hash_indirect
		 || h->root.type == bfd_link_hash_warning)
	    h = (struct elf_link_hash_entry *) h->root.u.i.link;
	}
      else
	h = NULL;

      /* We can only get preliminary data on whether a symbol is
	 locally or externally defined, as not all of the input files
	 have yet been processed.  Do something with what we know, as
	 this may help reduce memory usage and processing time later.  */
      maybe_dynamic = (h && ((!info->executable
			      && (!SYMBOLIC_BIND (info, h)
				  || info->unresolved_syms_in_shared_libs == RM_IGNORE))
			     || !h->def_regular
			     || h->root.type == bfd_link_hash_defweak));

      need_entry = 0;
      switch (ELFNN_R_TYPE (rel->r_info))
	{
	case R_IA64_TPREL64MSB:
	case R_IA64_TPREL64LSB:
	  if (info->shared || maybe_dynamic)
	    need_entry = NEED_DYNREL;
	  break;

	case R_IA64_LTOFF_TPREL22:
	  need_entry = NEED_TPREL;
	  if (info->shared)
	    info->flags |= DF_STATIC_TLS;
	  break;

	case R_IA64_DTPREL32MSB:
	case R_IA64_DTPREL32LSB:
	case R_IA64_DTPREL64MSB:
	case R_IA64_DTPREL64LSB:
	  if (info->shared || maybe_dynamic)
	    need_entry = NEED_DYNREL;
	  break;

	case R_IA64_LTOFF_DTPREL22:
	  need_entry = NEED_DTPREL;
	  break;

	case R_IA64_DTPMOD64MSB:
	case R_IA64_DTPMOD64LSB:
	  if (info->shared || maybe_dynamic)
	    need_entry = NEED_DYNREL;
	  break;

	case R_IA64_LTOFF_DTPMOD22:
	  need_entry = NEED_DTPMOD;
	  break;

	case R_IA64_LTOFF_FPTR22:
	case R_IA64_LTOFF_FPTR64I:
	case R_IA64_LTOFF_FPTR32MSB:
	case R_IA64_LTOFF_FPTR32LSB:
	case R_IA64_LTOFF_FPTR64MSB:
	case R_IA64_LTOFF_FPTR64LSB:
	  need_entry = NEED_FPTR | NEED_GOT | NEED_LTOFF_FPTR;
	  break;

	case R_IA64_FPTR64I:
	case R_IA64_FPTR32MSB:
	case R_IA64_FPTR32LSB:
	case R_IA64_FPTR64MSB:
	case R_IA64_FPTR64LSB:
	  if (info->shared || h)
	    need_entry = NEED_FPTR | NEED_DYNREL;
	  else
	    need_entry = NEED_FPTR;
	  break;

	case R_IA64_LTOFF22:
	case R_IA64_LTOFF64I:
	  need_entry = NEED_GOT;
	  break;

	case R_IA64_LTOFF22X:
	  need_entry = NEED_GOTX;
	  break;

	case R_IA64_PLTOFF22:
	case R_IA64_PLTOFF64I:
	case R_IA64_PLTOFF64MSB:
	case R_IA64_PLTOFF64LSB:
	  need_entry = NEED_PLTOFF;
	  if (h)
	    {
	      if (maybe_dynamic)
		need_entry |= NEED_MIN_PLT;
	    }
	  else
	    {
	      (*info->callbacks->warning)
		(info, _("@pltoff reloc against local symbol"), 0,
		 abfd, 0, (bfd_vma) 0);
	    }
	  break;

	case R_IA64_PCREL21B:
        case R_IA64_PCREL60B:
	  /* Depending on where this symbol is defined, we may or may not
	     need a full plt entry.  Only skip if we know we'll not need
	     the entry -- static or symbolic, and the symbol definition
	     has already been seen.  */
	  if (maybe_dynamic && rel->r_addend == 0)
	    need_entry = NEED_FULL_PLT;
	  break;

	case R_IA64_IMM14:
	case R_IA64_IMM22:
	case R_IA64_IMM64:
	case R_IA64_DIR32MSB:
	case R_IA64_DIR32LSB:
	case R_IA64_DIR64MSB:
	case R_IA64_DIR64LSB:
	  /* Shared objects will always need at least a REL relocation.  */
	  if (info->shared || maybe_dynamic)
	    need_entry = NEED_DYNREL;
	  break;

	case R_IA64_IPLTMSB:
	case R_IA64_IPLTLSB:
	  /* Shared objects will always need at least a REL relocation.  */
	  if (info->shared || maybe_dynamic)
	    need_entry = NEED_DYNREL;
	  break;

	case R_IA64_PCREL22:
	case R_IA64_PCREL64I:
	case R_IA64_PCREL32MSB:
	case R_IA64_PCREL32LSB:
	case R_IA64_PCREL64MSB:
	case R_IA64_PCREL64LSB:
	  if (maybe_dynamic)
	    need_entry = NEED_DYNREL;
	  break;
	}

      if (!need_entry)
	continue;

      if ((need_entry & NEED_FPTR) != 0
	  && rel->r_addend)
	{
	  (*info->callbacks->warning)
	    (info, _("non-zero addend in @fptr reloc"), 0,
	     abfd, 0, (bfd_vma) 0);
	}

      if (get_dyn_sym_info (ia64_info, h, abfd, rel, TRUE) == NULL)
	return FALSE;
    }

  /* Now, we only do lookup without insertion, which is very fast
     with the modified get_dyn_sym_info.  */ 
  for (rel = relocs; rel < relend; ++rel)
    {
      struct elfNN_ia64_dyn_sym_info *dyn_i;
      int dynrel_type = R_IA64_NONE;

      r_symndx = ELFNN_R_SYM (rel->r_info);
      if (r_symndx >= symtab_hdr->sh_info)
	{
	  /* We're dealing with a global symbol -- find its hash entry
	     and mark it as being referenced.  */
	  long indx = r_symndx - symtab_hdr->sh_info;
	  h = elf_sym_hashes (abfd)[indx];
	  while (h->root.type == bfd_link_hash_indirect
		 || h->root.type == bfd_link_hash_warning)
	    h = (struct elf_link_hash_entry *) h->root.u.i.link;

	  h->ref_regular = 1;
	}
      else
	h = NULL;

      /* We can only get preliminary data on whether a symbol is
	 locally or externally defined, as not all of the input files
	 have yet been processed.  Do something with what we know, as
	 this may help reduce memory usage and processing time later.  */
      maybe_dynamic = (h && ((!info->executable
			      && (!SYMBOLIC_BIND (info, h)
				  || info->unresolved_syms_in_shared_libs == RM_IGNORE))
			     || !h->def_regular
			     || h->root.type == bfd_link_hash_defweak));

      need_entry = 0;
      switch (ELFNN_R_TYPE (rel->r_info))
	{
	case R_IA64_TPREL64MSB:
	case R_IA64_TPREL64LSB:
	  if (info->shared || maybe_dynamic)
	    need_entry = NEED_DYNREL;
	  dynrel_type = R_IA64_TPREL64LSB;
	  if (info->shared)
	    info->flags |= DF_STATIC_TLS;
	  break;

	case R_IA64_LTOFF_TPREL22:
	  need_entry = NEED_TPREL;
	  if (info->shared)
	    info->flags |= DF_STATIC_TLS;
	  break;

	case R_IA64_DTPREL32MSB:
	case R_IA64_DTPREL32LSB:
	case R_IA64_DTPREL64MSB:
	case R_IA64_DTPREL64LSB:
	  if (info->shared || maybe_dynamic)
	    need_entry = NEED_DYNREL;
	  dynrel_type = R_IA64_DTPRELNNLSB;
	  break;

	case R_IA64_LTOFF_DTPREL22:
	  need_entry = NEED_DTPREL;
	  break;

	case R_IA64_DTPMOD64MSB:
	case R_IA64_DTPMOD64LSB:
	  if (info->shared || maybe_dynamic)
	    need_entry = NEED_DYNREL;
	  dynrel_type = R_IA64_DTPMOD64LSB;
	  break;

	case R_IA64_LTOFF_DTPMOD22:
	  need_entry = NEED_DTPMOD;
	  break;

	case R_IA64_LTOFF_FPTR22:
	case R_IA64_LTOFF_FPTR64I:
	case R_IA64_LTOFF_FPTR32MSB:
	case R_IA64_LTOFF_FPTR32LSB:
	case R_IA64_LTOFF_FPTR64MSB:
	case R_IA64_LTOFF_FPTR64LSB:
	  need_entry = NEED_FPTR | NEED_GOT | NEED_LTOFF_FPTR;
	  break;

	case R_IA64_FPTR64I:
	case R_IA64_FPTR32MSB:
	case R_IA64_FPTR32LSB:
	case R_IA64_FPTR64MSB:
	case R_IA64_FPTR64LSB:
	  if (info->shared || h)
	    need_entry = NEED_FPTR | NEED_DYNREL;
	  else
	    need_entry = NEED_FPTR;
	  dynrel_type = R_IA64_FPTRNNLSB;
	  break;

	case R_IA64_LTOFF22:
	case R_IA64_LTOFF64I:
	  need_entry = NEED_GOT;
	  break;

	case R_IA64_LTOFF22X:
	  need_entry = NEED_GOTX;
	  break;

	case R_IA64_PLTOFF22:
	case R_IA64_PLTOFF64I:
	case R_IA64_PLTOFF64MSB:
	case R_IA64_PLTOFF64LSB:
	  need_entry = NEED_PLTOFF;
	  if (h)
	    {
	      if (maybe_dynamic)
		need_entry |= NEED_MIN_PLT;
	    }
	  break;

	case R_IA64_PCREL21B:
        case R_IA64_PCREL60B:
	  /* Depending on where this symbol is defined, we may or may not
	     need a full plt entry.  Only skip if we know we'll not need
	     the entry -- static or symbolic, and the symbol definition
	     has already been seen.  */
	  if (maybe_dynamic && rel->r_addend == 0)
	    need_entry = NEED_FULL_PLT;
	  break;

	case R_IA64_IMM14:
	case R_IA64_IMM22:
	case R_IA64_IMM64:
	case R_IA64_DIR32MSB:
	case R_IA64_DIR32LSB:
	case R_IA64_DIR64MSB:
	case R_IA64_DIR64LSB:
	  /* Shared objects will always need at least a REL relocation.  */
	  if (info->shared || maybe_dynamic)
	    need_entry = NEED_DYNREL;
	  dynrel_type = R_IA64_DIRNNLSB;
	  break;

	case R_IA64_IPLTMSB:
	case R_IA64_IPLTLSB:
	  /* Shared objects will always need at least a REL relocation.  */
	  if (info->shared || maybe_dynamic)
	    need_entry = NEED_DYNREL;
	  dynrel_type = R_IA64_IPLTLSB;
	  break;

	case R_IA64_PCREL22:
	case R_IA64_PCREL64I:
	case R_IA64_PCREL32MSB:
	case R_IA64_PCREL32LSB:
	case R_IA64_PCREL64MSB:
	case R_IA64_PCREL64LSB:
	  if (maybe_dynamic)
	    need_entry = NEED_DYNREL;
	  dynrel_type = R_IA64_PCRELNNLSB;
	  break;
	}

      if (!need_entry)
	continue;

      dyn_i = get_dyn_sym_info (ia64_info, h, abfd, rel, FALSE);

      /* Record whether or not this is a local symbol.  */
      dyn_i->h = h;

      /* Create what's needed.  */
      if (need_entry & (NEED_GOT | NEED_GOTX | NEED_TPREL
			| NEED_DTPMOD | NEED_DTPREL))
	{
	  if (!got)
	    {
	      got = get_got (abfd, info, ia64_info);
	      if (!got)
		return FALSE;
	    }
	  if (need_entry & NEED_GOT)
	    dyn_i->want_got = 1;
	  if (need_entry & NEED_GOTX)
	    dyn_i->want_gotx = 1;
	  if (need_entry & NEED_TPREL)
	    dyn_i->want_tprel = 1;
	  if (need_entry & NEED_DTPMOD)
	    dyn_i->want_dtpmod = 1;
	  if (need_entry & NEED_DTPREL)
	    dyn_i->want_dtprel = 1;
	}
      if (need_entry & NEED_FPTR)
	{
	  if (!fptr)
	    {
	      fptr = get_fptr (abfd, info, ia64_info);
	      if (!fptr)
		return FALSE;
	    }

	  /* FPTRs for shared libraries are allocated by the dynamic
	     linker.  Make sure this local symbol will appear in the
	     dynamic symbol table.  */
	  if (!h && info->shared)
	    {
	      if (! (bfd_elf_link_record_local_dynamic_symbol
		     (info, abfd, (long) r_symndx)))
		return FALSE;
	    }

	  dyn_i->want_fptr = 1;
	}
      if (need_entry & NEED_LTOFF_FPTR)
	dyn_i->want_ltoff_fptr = 1;
      if (need_entry & (NEED_MIN_PLT | NEED_FULL_PLT))
	{
          if (!ia64_info->root.dynobj)
	    ia64_info->root.dynobj = abfd;
	  h->needs_plt = 1;
	  dyn_i->want_plt = 1;
	}
      if (need_entry & NEED_FULL_PLT)
	dyn_i->want_plt2 = 1;
      if (need_entry & NEED_PLTOFF)
	{
	  /* This is needed here, in case @pltoff is used in a non-shared
	     link.  */
	  if (!pltoff)
	    {
	      pltoff = get_pltoff (abfd, info, ia64_info);
	      if (!pltoff)
		return FALSE;
	    }

	  dyn_i->want_pltoff = 1;
	}
      if ((need_entry & NEED_DYNREL) && (sec->flags & SEC_ALLOC))
	{
	  if (!srel)
	    {
	      srel = get_reloc_section (abfd, ia64_info, sec, TRUE);
	      if (!srel)
		return FALSE;
	    }
	  if (!count_dyn_reloc (abfd, dyn_i, srel, dynrel_type,
				(sec->flags & SEC_READONLY) != 0))
	    return FALSE;
	}
    }

  return TRUE;
}

/* For cleanliness, and potentially faster dynamic loading, allocate
   external GOT entries first.  */

static bfd_boolean
allocate_global_data_got (dyn_i, data)
     struct elfNN_ia64_dyn_sym_info *dyn_i;
     PTR data;
{
  struct elfNN_ia64_allocate_data *x = (struct elfNN_ia64_allocate_data *)data;

  if ((dyn_i->want_got || dyn_i->want_gotx)
      && ! dyn_i->want_fptr
      && elfNN_ia64_dynamic_symbol_p (dyn_i->h, x->info, 0))
     {
       dyn_i->got_offset = x->ofs;
       x->ofs += 8;
     }
  if (dyn_i->want_tprel)
    {
      dyn_i->tprel_offset = x->ofs;
      x->ofs += 8;
    }
  if (dyn_i->want_dtpmod)
    {
      if (elfNN_ia64_dynamic_symbol_p (dyn_i->h, x->info, 0))
	{
	  dyn_i->dtpmod_offset = x->ofs;
	  x->ofs += 8;
	}
      else
	{
	  struct elfNN_ia64_link_hash_table *ia64_info;

	  ia64_info = elfNN_ia64_hash_table (x->info);
	  if (ia64_info->self_dtpmod_offset == (bfd_vma) -1)
	    {
	      ia64_info->self_dtpmod_offset = x->ofs;
	      x->ofs += 8;
	    }
	  dyn_i->dtpmod_offset = ia64_info->self_dtpmod_offset;
	}
    }
  if (dyn_i->want_dtprel)
    {
      dyn_i->dtprel_offset = x->ofs;
      x->ofs += 8;
    }
  return TRUE;
}

/* Next, allocate all the GOT entries used by LTOFF_FPTR relocs.  */

static bfd_boolean
allocate_global_fptr_got (dyn_i, data)
     struct elfNN_ia64_dyn_sym_info *dyn_i;
     PTR data;
{
  struct elfNN_ia64_allocate_data *x = (struct elfNN_ia64_allocate_data *)data;

  if (dyn_i->want_got
      && dyn_i->want_fptr
      && elfNN_ia64_dynamic_symbol_p (dyn_i->h, x->info, R_IA64_FPTRNNLSB))
    {
      dyn_i->got_offset = x->ofs;
      x->ofs += 8;
    }
  return TRUE;
}

/* Lastly, allocate all the GOT entries for local data.  */

static bfd_boolean
allocate_local_got (dyn_i, data)
     struct elfNN_ia64_dyn_sym_info *dyn_i;
     PTR data;
{
  struct elfNN_ia64_allocate_data *x = (struct elfNN_ia64_allocate_data *)data;

  if ((dyn_i->want_got || dyn_i->want_gotx)
      && !elfNN_ia64_dynamic_symbol_p (dyn_i->h, x->info, 0))
    {
      dyn_i->got_offset = x->ofs;
      x->ofs += 8;
    }
  return TRUE;
}

/* Search for the index of a global symbol in it's defining object file.  */

static long
global_sym_index (h)
     struct elf_link_hash_entry *h;
{
  struct elf_link_hash_entry **p;
  bfd *obj;

  BFD_ASSERT (h->root.type == bfd_link_hash_defined
	      || h->root.type == bfd_link_hash_defweak);

  obj = h->root.u.def.section->owner;
  for (p = elf_sym_hashes (obj); *p != h; ++p)
    continue;

  return p - elf_sym_hashes (obj) + elf_tdata (obj)->symtab_hdr.sh_info;
}

/* Allocate function descriptors.  We can do these for every function
   in a main executable that is not exported.  */

static bfd_boolean
allocate_fptr (dyn_i, data)
     struct elfNN_ia64_dyn_sym_info *dyn_i;
     PTR data;
{
  struct elfNN_ia64_allocate_data *x = (struct elfNN_ia64_allocate_data *)data;

  if (dyn_i->want_fptr)
    {
      struct elf_link_hash_entry *h = dyn_i->h;

      if (h)
	while (h->root.type == bfd_link_hash_indirect
	       || h->root.type == bfd_link_hash_warning)
	  h = (struct elf_link_hash_entry *) h->root.u.i.link;

      if (!x->info->executable
	  && (!h
	      || ELF_ST_VISIBILITY (h->other) == STV_DEFAULT
	      || (h->root.type != bfd_link_hash_undefweak
		  && h->root.type != bfd_link_hash_undefined)))
	{
	  if (h && h->dynindx == -1)
	    {
	      BFD_ASSERT ((h->root.type == bfd_link_hash_defined)
			  || (h->root.type == bfd_link_hash_defweak));

	      if (!bfd_elf_link_record_local_dynamic_symbol
		    (x->info, h->root.u.def.section->owner,
		     global_sym_index (h)))
		return FALSE;
	    }

	  dyn_i->want_fptr = 0;
	}
      else if (h == NULL || h->dynindx == -1)
	{
	  dyn_i->fptr_offset = x->ofs;
	  x->ofs += 16;
	}
      else
	dyn_i->want_fptr = 0;
    }
  return TRUE;
}

/* Allocate all the minimal PLT entries.  */

static bfd_boolean
allocate_plt_entries (dyn_i, data)
     struct elfNN_ia64_dyn_sym_info *dyn_i;
     PTR data;
{
  struct elfNN_ia64_allocate_data *x = (struct elfNN_ia64_allocate_data *)data;

  if (dyn_i->want_plt)
    {
      struct elf_link_hash_entry *h = dyn_i->h;

      if (h)
	while (h->root.type == bfd_link_hash_indirect
	       || h->root.type == bfd_link_hash_warning)
	  h = (struct elf_link_hash_entry *) h->root.u.i.link;

      /* ??? Versioned symbols seem to lose NEEDS_PLT.  */
      if (elfNN_ia64_dynamic_symbol_p (h, x->info, 0))
	{
	  bfd_size_type offset = x->ofs;
	  if (offset == 0)
	    offset = PLT_HEADER_SIZE;
	  dyn_i->plt_offset = offset;
	  x->ofs = offset + PLT_MIN_ENTRY_SIZE;

	  dyn_i->want_pltoff = 1;
	}
      else
	{
	  dyn_i->want_plt = 0;
	  dyn_i->want_plt2 = 0;
	}
    }
  return TRUE;
}

/* Allocate all the full PLT entries.  */

static bfd_boolean
allocate_plt2_entries (dyn_i, data)
     struct elfNN_ia64_dyn_sym_info *dyn_i;
     PTR data;
{
  struct elfNN_ia64_allocate_data *x = (struct elfNN_ia64_allocate_data *)data;

  if (dyn_i->want_plt2)
    {
      struct elf_link_hash_entry *h = dyn_i->h;
      bfd_size_type ofs = x->ofs;

      dyn_i->plt2_offset = ofs;
      x->ofs = ofs + PLT_FULL_ENTRY_SIZE;

      while (h->root.type == bfd_link_hash_indirect
	     || h->root.type == bfd_link_hash_warning)
	h = (struct elf_link_hash_entry *) h->root.u.i.link;
      dyn_i->h->plt.offset = ofs;
    }
  return TRUE;
}

/* Allocate all the PLTOFF entries requested by relocations and
   plt entries.  We can't share space with allocated FPTR entries,
   because the latter are not necessarily addressable by the GP.
   ??? Relaxation might be able to determine that they are.  */

static bfd_boolean
allocate_pltoff_entries (dyn_i, data)
     struct elfNN_ia64_dyn_sym_info *dyn_i;
     PTR data;
{
  struct elfNN_ia64_allocate_data *x = (struct elfNN_ia64_allocate_data *)data;

  if (dyn_i->want_pltoff)
    {
      dyn_i->pltoff_offset = x->ofs;
      x->ofs += 16;
    }
  return TRUE;
}

/* Allocate dynamic relocations for those symbols that turned out
   to be dynamic.  */

static bfd_boolean
allocate_dynrel_entries (dyn_i, data)
     struct elfNN_ia64_dyn_sym_info *dyn_i;
     PTR data;
{
  struct elfNN_ia64_allocate_data *x = (struct elfNN_ia64_allocate_data *)data;
  struct elfNN_ia64_link_hash_table *ia64_info;
  struct elfNN_ia64_dyn_reloc_entry *rent;
  bfd_boolean dynamic_symbol, shared, resolved_zero;

  ia64_info = elfNN_ia64_hash_table (x->info);

  /* Note that this can't be used in relation to FPTR relocs below.  */
  dynamic_symbol = elfNN_ia64_dynamic_symbol_p (dyn_i->h, x->info, 0);

  shared = x->info->shared;
  resolved_zero = (dyn_i->h
		   && ELF_ST_VISIBILITY (dyn_i->h->other)
		   && dyn_i->h->root.type == bfd_link_hash_undefweak);

  /* Take care of the GOT and PLT relocations.  */

  if ((!resolved_zero
       && (dynamic_symbol || shared)
       && (dyn_i->want_got || dyn_i->want_gotx))
      || (dyn_i->want_ltoff_fptr
	  && dyn_i->h
	  && dyn_i->h->dynindx != -1))
    {
      if (!dyn_i->want_ltoff_fptr
	  || !x->info->pie
	  || dyn_i->h == NULL
	  || dyn_i->h->root.type != bfd_link_hash_undefweak)
	ia64_info->rel_got_sec->size += sizeof (ElfNN_External_Rela);
    }
  if ((dynamic_symbol || shared) && dyn_i->want_tprel)
    ia64_info->rel_got_sec->size += sizeof (ElfNN_External_Rela);
  if (dynamic_symbol && dyn_i->want_dtpmod)
    ia64_info->rel_got_sec->size += sizeof (ElfNN_External_Rela);
  if (dynamic_symbol && dyn_i->want_dtprel)
    ia64_info->rel_got_sec->size += sizeof (ElfNN_External_Rela);

  if (x->only_got)
    return TRUE;

  if (ia64_info->rel_fptr_sec && dyn_i->want_fptr)
    {
      if (dyn_i->h == NULL || dyn_i->h->root.type != bfd_link_hash_undefweak)
	ia64_info->rel_fptr_sec->size += sizeof (ElfNN_External_Rela);
    }

  if (!resolved_zero && dyn_i->want_pltoff)
    {
      bfd_size_type t = 0;

      /* Dynamic symbols get one IPLT relocation.  Local symbols in
	 shared libraries get two REL relocations.  Local symbols in
	 main applications get nothing.  */
      if (dynamic_symbol)
	t = sizeof (ElfNN_External_Rela);
      else if (shared)
	t = 2 * sizeof (ElfNN_External_Rela);

      ia64_info->rel_pltoff_sec->size += t;
    }

  /* Take care of the normal data relocations.  */

  for (rent = dyn_i->reloc_entries; rent; rent = rent->next)
    {
      int count = rent->count;

      switch (rent->type)
	{
	case R_IA64_FPTR32LSB:
	case R_IA64_FPTR64LSB:
	  /* Allocate one iff !want_fptr and not PIE, which by this point
	     will be true only if we're actually allocating one statically
	     in the main executable.  Position independent executables
	     need a relative reloc.  */
	  if (dyn_i->want_fptr && !x->info->pie)
	    continue;
	  break;
	case R_IA64_PCREL32LSB:
	case R_IA64_PCREL64LSB:
	  if (!dynamic_symbol)
	    continue;
	  break;
	case R_IA64_DIR32LSB:
	case R_IA64_DIR64LSB:
	  if (!dynamic_symbol && !shared)
	    continue;
	  break;
	case R_IA64_IPLTLSB:
	  if (!dynamic_symbol && !shared)
	    continue;
	  /* Use two REL relocations for IPLT relocations
	     against local symbols.  */
	  if (!dynamic_symbol)
	    count *= 2;
	  break;
	case R_IA64_DTPREL32LSB:
	case R_IA64_TPREL64LSB:
	case R_IA64_DTPREL64LSB:
	case R_IA64_DTPMOD64LSB:
	  break;
	default:
	  abort ();
	}
      if (rent->reltext)
	ia64_info->reltext = 1;
      rent->srel->size += sizeof (ElfNN_External_Rela) * count;
    }

  return TRUE;
}

static bfd_boolean
elfNN_ia64_adjust_dynamic_symbol (info, h)
     struct bfd_link_info *info ATTRIBUTE_UNUSED;
     struct elf_link_hash_entry *h;
{
  /* ??? Undefined symbols with PLT entries should be re-defined
     to be the PLT entry.  */

  /* If this is a weak symbol, and there is a real definition, the
     processor independent code will have arranged for us to see the
     real definition first, and we can just use the same value.  */
  if (h->u.weakdef != NULL)
    {
      BFD_ASSERT (h->u.weakdef->root.type == bfd_link_hash_defined
                  || h->u.weakdef->root.type == bfd_link_hash_defweak);
      h->root.u.def.section = h->u.weakdef->root.u.def.section;
      h->root.u.def.value = h->u.weakdef->root.u.def.value;
      return TRUE;
    }

  /* If this is a reference to a symbol defined by a dynamic object which
     is not a function, we might allocate the symbol in our .dynbss section
     and allocate a COPY dynamic relocation.

     But IA-64 code is canonically PIC, so as a rule we can avoid this sort
     of hackery.  */

  return TRUE;
}

static bfd_boolean
elfNN_ia64_size_dynamic_sections (output_bfd, info)
     bfd *output_bfd ATTRIBUTE_UNUSED;
     struct bfd_link_info *info;
{
  struct elfNN_ia64_allocate_data data;
  struct elfNN_ia64_link_hash_table *ia64_info;
  asection *sec;
  bfd *dynobj;
  bfd_boolean relplt = FALSE;

  dynobj = elf_hash_table(info)->dynobj;
  ia64_info = elfNN_ia64_hash_table (info);
  ia64_info->self_dtpmod_offset = (bfd_vma) -1;
  BFD_ASSERT(dynobj != NULL);
  data.info = info;

  /* Set the contents of the .interp section to the interpreter.  */
  if (ia64_info->root.dynamic_sections_created
      && info->executable)
    {
      sec = bfd_get_section_by_name (dynobj, ".interp");
      BFD_ASSERT (sec != NULL);
      sec->contents = (bfd_byte *) ELF_DYNAMIC_INTERPRETER;
      sec->size = strlen (ELF_DYNAMIC_INTERPRETER) + 1;
    }

  /* Allocate the GOT entries.  */

  if (ia64_info->got_sec)
    {
      data.ofs = 0;
      elfNN_ia64_dyn_sym_traverse (ia64_info, allocate_global_data_got, &data);
      elfNN_ia64_dyn_sym_traverse (ia64_info, allocate_global_fptr_got, &data);
      elfNN_ia64_dyn_sym_traverse (ia64_info, allocate_local_got, &data);
      ia64_info->got_sec->size = data.ofs;
    }

  /* Allocate the FPTR entries.  */

  if (ia64_info->fptr_sec)
    {
      data.ofs = 0;
      elfNN_ia64_dyn_sym_traverse (ia64_info, allocate_fptr, &data);
      ia64_info->fptr_sec->size = data.ofs;
    }

  /* Now that we've seen all of the input files, we can decide which
     symbols need plt entries.  Allocate the minimal PLT entries first.
     We do this even though dynamic_sections_created may be FALSE, because
     this has the side-effect of clearing want_plt and want_plt2.  */

  data.ofs = 0;
  elfNN_ia64_dyn_sym_traverse (ia64_info, allocate_plt_entries, &data);

  ia64_info->minplt_entries = 0;
  if (data.ofs)
    {
      ia64_info->minplt_entries
	= (data.ofs - PLT_HEADER_SIZE) / PLT_MIN_ENTRY_SIZE;
    }

  /* Align the pointer for the plt2 entries.  */
  data.ofs = (data.ofs + 31) & (bfd_vma) -32;

  elfNN_ia64_dyn_sym_traverse (ia64_info, allocate_plt2_entries, &data);
  if (data.ofs != 0 || ia64_info->root.dynamic_sections_created)
    {
      /* FIXME: we always reserve the memory for dynamic linker even if
	 there are no PLT entries since dynamic linker may assume the
	 reserved memory always exists.  */

      BFD_ASSERT (ia64_info->root.dynamic_sections_created);

      ia64_info->plt_sec->size = data.ofs;

      /* If we've got a .plt, we need some extra memory for the dynamic
	 linker.  We stuff these in .got.plt.  */
      sec = bfd_get_section_by_name (dynobj, ".got.plt");
      sec->size = 8 * PLT_RESERVED_WORDS;
    }

  /* Allocate the PLTOFF entries.  */

  if (ia64_info->pltoff_sec)
    {
      data.ofs = 0;
      elfNN_ia64_dyn_sym_traverse (ia64_info, allocate_pltoff_entries, &data);
      ia64_info->pltoff_sec->size = data.ofs;
    }

  if (ia64_info->root.dynamic_sections_created)
    {
      /* Allocate space for the dynamic relocations that turned out to be
	 required.  */

      if (info->shared && ia64_info->self_dtpmod_offset != (bfd_vma) -1)
	ia64_info->rel_got_sec->size += sizeof (ElfNN_External_Rela);
      data.only_got = FALSE;
      elfNN_ia64_dyn_sym_traverse (ia64_info, allocate_dynrel_entries, &data);
    }

  /* We have now determined the sizes of the various dynamic sections.
     Allocate memory for them.  */
  for (sec = dynobj->sections; sec != NULL; sec = sec->next)
    {
      bfd_boolean strip;

      if (!(sec->flags & SEC_LINKER_CREATED))
	continue;

      /* If we don't need this section, strip it from the output file.
	 There were several sections primarily related to dynamic
	 linking that must be create before the linker maps input
	 sections to output sections.  The linker does that before
	 bfd_elf_size_dynamic_sections is called, and it is that
	 function which decides whether anything needs to go into
	 these sections.  */

      strip = (sec->size == 0);

      if (sec == ia64_info->got_sec)
	strip = FALSE;
      else if (sec == ia64_info->rel_got_sec)
	{
	  if (strip)
	    ia64_info->rel_got_sec = NULL;
	  else
	    /* We use the reloc_count field as a counter if we need to
	       copy relocs into the output file.  */
	    sec->reloc_count = 0;
	}
      else if (sec == ia64_info->fptr_sec)
	{
	  if (strip)
	    ia64_info->fptr_sec = NULL;
	}
      else if (sec == ia64_info->rel_fptr_sec)
	{
	  if (strip)
	    ia64_info->rel_fptr_sec = NULL;
	  else
	    /* We use the reloc_count field as a counter if we need to
	       copy relocs into the output file.  */
	    sec->reloc_count = 0;
	}
      else if (sec == ia64_info->plt_sec)
	{
	  if (strip)
	    ia64_info->plt_sec = NULL;
	}
      else if (sec == ia64_info->pltoff_sec)
	{
	  if (strip)
	    ia64_info->pltoff_sec = NULL;
	}
      else if (sec == ia64_info->rel_pltoff_sec)
	{
	  if (strip)
	    ia64_info->rel_pltoff_sec = NULL;
	  else
	    {
	      relplt = TRUE;
	      /* We use the reloc_count field as a counter if we need to
		 copy relocs into the output file.  */
	      sec->reloc_count = 0;
	    }
	}
      else
	{
	  const char *name;

	  /* It's OK to base decisions on the section name, because none
	     of the dynobj section names depend upon the input files.  */
	  name = bfd_get_section_name (dynobj, sec);

	  if (strcmp (name, ".got.plt") == 0)
	    strip = FALSE;
	  else if (CONST_STRNEQ (name, ".rel"))
	    {
	      if (!strip)
		{
		  /* We use the reloc_count field as a counter if we need to
		     copy relocs into the output file.  */
		  sec->reloc_count = 0;
		}
	    }
	  else
	    continue;
	}

      if (strip)
	sec->flags |= SEC_EXCLUDE;
      else
	{
	  /* Allocate memory for the section contents.  */
	  sec->contents = (bfd_byte *) bfd_zalloc (dynobj, sec->size);
	  if (sec->contents == NULL && sec->size != 0)
	    return FALSE;
	}
    }

  if (elf_hash_table (info)->dynamic_sections_created)
    {
      /* Add some entries to the .dynamic section.  We fill in the values
	 later (in finish_dynamic_sections) but we must add the entries now
	 so that we get the correct size for the .dynamic section.  */

      if (info->executable)
	{
	  /* The DT_DEBUG entry is filled in by the dynamic linker and used
	     by the debugger.  */
#define add_dynamic_entry(TAG, VAL) \
  _bfd_elf_add_dynamic_entry (info, TAG, VAL)

	  if (!add_dynamic_entry (DT_DEBUG, 0))
	    return FALSE;
	}

      if (!add_dynamic_entry (DT_IA_64_PLT_RESERVE, 0))
	return FALSE;
      if (!add_dynamic_entry (DT_PLTGOT, 0))
	return FALSE;

      if (relplt)
	{
	  if (!add_dynamic_entry (DT_PLTRELSZ, 0)
	      || !add_dynamic_entry (DT_PLTREL, DT_RELA)
	      || !add_dynamic_entry (DT_JMPREL, 0))
	    return FALSE;
	}

      if (!add_dynamic_entry (DT_RELA, 0)
	  || !add_dynamic_entry (DT_RELASZ, 0)
	  || !add_dynamic_entry (DT_RELAENT, sizeof (ElfNN_External_Rela)))
	return FALSE;

      if (ia64_info->reltext)
	{
	  if (!add_dynamic_entry (DT_TEXTREL, 0))
	    return FALSE;
	  info->flags |= DF_TEXTREL;
	}
    }

  /* ??? Perhaps force __gp local.  */

  return TRUE;
}

static bfd_reloc_status_type
elfNN_ia64_install_value (hit_addr, v, r_type)
     bfd_byte *hit_addr;
     bfd_vma v;
     unsigned int r_type;
{
  const struct ia64_operand *op;
  int bigendian = 0, shift = 0;
  bfd_vma t0, t1, dword;
  ia64_insn insn;
  enum ia64_opnd opnd;
  const char *err;
  size_t size = 8;
#ifdef BFD_HOST_U_64_BIT
  BFD_HOST_U_64_BIT val = (BFD_HOST_U_64_BIT) v;
#else
  bfd_vma val = v;
#endif

  opnd = IA64_OPND_NIL;
  switch (r_type)
    {
    case R_IA64_NONE:
    case R_IA64_LDXMOV:
      return bfd_reloc_ok;

      /* Instruction relocations.  */

    case R_IA64_IMM14:
    case R_IA64_TPREL14:
    case R_IA64_DTPREL14:
      opnd = IA64_OPND_IMM14;
      break;

    case R_IA64_PCREL21F:	opnd = IA64_OPND_TGT25; break;
    case R_IA64_PCREL21M:	opnd = IA64_OPND_TGT25b; break;
    case R_IA64_PCREL60B:	opnd = IA64_OPND_TGT64; break;
    case R_IA64_PCREL21B:
    case R_IA64_PCREL21BI:
      opnd = IA64_OPND_TGT25c;
      break;

    case R_IA64_IMM22:
    case R_IA64_GPREL22:
    case R_IA64_LTOFF22:
    case R_IA64_LTOFF22X:
    case R_IA64_PLTOFF22:
    case R_IA64_PCREL22:
    case R_IA64_LTOFF_FPTR22:
    case R_IA64_TPREL22:
    case R_IA64_DTPREL22:
    case R_IA64_LTOFF_TPREL22:
    case R_IA64_LTOFF_DTPMOD22:
    case R_IA64_LTOFF_DTPREL22:
      opnd = IA64_OPND_IMM22;
      break;

    case R_IA64_IMM64:
    case R_IA64_GPREL64I:
    case R_IA64_LTOFF64I:
    case R_IA64_PLTOFF64I:
    case R_IA64_PCREL64I:
    case R_IA64_FPTR64I:
    case R_IA64_LTOFF_FPTR64I:
    case R_IA64_TPREL64I:
    case R_IA64_DTPREL64I:
      opnd = IA64_OPND_IMMU64;
      break;

      /* Data relocations.  */

    case R_IA64_DIR32MSB:
    case R_IA64_GPREL32MSB:
    case R_IA64_FPTR32MSB:
    case R_IA64_PCREL32MSB:
    case R_IA64_LTOFF_FPTR32MSB:
    case R_IA64_SEGREL32MSB:
    case R_IA64_SECREL32MSB:
    case R_IA64_LTV32MSB:
    case R_IA64_DTPREL32MSB:
      size = 4; bigendian = 1;
      break;

    case R_IA64_DIR32LSB:
    case R_IA64_GPREL32LSB:
    case R_IA64_FPTR32LSB:
    case R_IA64_PCREL32LSB:
    case R_IA64_LTOFF_FPTR32LSB:
    case R_IA64_SEGREL32LSB:
    case R_IA64_SECREL32LSB:
    case R_IA64_LTV32LSB:
    case R_IA64_DTPREL32LSB:
      size = 4; bigendian = 0;
      break;

    case R_IA64_DIR64MSB:
    case R_IA64_GPREL64MSB:
    case R_IA64_PLTOFF64MSB:
    case R_IA64_FPTR64MSB:
    case R_IA64_PCREL64MSB:
    case R_IA64_LTOFF_FPTR64MSB:
    case R_IA64_SEGREL64MSB:
    case R_IA64_SECREL64MSB:
    case R_IA64_LTV64MSB:
    case R_IA64_TPREL64MSB:
    case R_IA64_DTPMOD64MSB:
    case R_IA64_DTPREL64MSB:
      size = 8; bigendian = 1;
      break;

    case R_IA64_DIR64LSB:
    case R_IA64_GPREL64LSB:
    case R_IA64_PLTOFF64LSB:
    case R_IA64_FPTR64LSB:
    case R_IA64_PCREL64LSB:
    case R_IA64_LTOFF_FPTR64LSB:
    case R_IA64_SEGREL64LSB:
    case R_IA64_SECREL64LSB:
    case R_IA64_LTV64LSB:
    case R_IA64_TPREL64LSB:
    case R_IA64_DTPMOD64LSB:
    case R_IA64_DTPREL64LSB:
      size = 8; bigendian = 0;
      break;

      /* Unsupported / Dynamic relocations.  */
    default:
      return bfd_reloc_notsupported;
    }

  switch (opnd)
    {
    case IA64_OPND_IMMU64:
      hit_addr -= (long) hit_addr & 0x3;
      t0 = bfd_getl64 (hit_addr);
      t1 = bfd_getl64 (hit_addr + 8);

      /* tmpl/s: bits  0.. 5 in t0
	 slot 0: bits  5..45 in t0
	 slot 1: bits 46..63 in t0, bits 0..22 in t1
	 slot 2: bits 23..63 in t1 */

      /* First, clear the bits that form the 64 bit constant.  */
      t0 &= ~(0x3ffffLL << 46);
      t1 &= ~(0x7fffffLL
	      | ((  (0x07fLL << 13) | (0x1ffLL << 27)
		    | (0x01fLL << 22) | (0x001LL << 21)
		    | (0x001LL << 36)) << 23));

      t0 |= ((val >> 22) & 0x03ffffLL) << 46;		/* 18 lsbs of imm41 */
      t1 |= ((val >> 40) & 0x7fffffLL) <<  0;		/* 23 msbs of imm41 */
      t1 |= (  (((val >>  0) & 0x07f) << 13)		/* imm7b */
	       | (((val >>  7) & 0x1ff) << 27)		/* imm9d */
	       | (((val >> 16) & 0x01f) << 22)		/* imm5c */
	       | (((val >> 21) & 0x001) << 21)		/* ic */
	       | (((val >> 63) & 0x001) << 36)) << 23;	/* i */

      bfd_putl64 (t0, hit_addr);
      bfd_putl64 (t1, hit_addr + 8);
      break;

    case IA64_OPND_TGT64:
      hit_addr -= (long) hit_addr & 0x3;
      t0 = bfd_getl64 (hit_addr);
      t1 = bfd_getl64 (hit_addr + 8);

      /* tmpl/s: bits  0.. 5 in t0
	 slot 0: bits  5..45 in t0
	 slot 1: bits 46..63 in t0, bits 0..22 in t1
	 slot 2: bits 23..63 in t1 */

      /* First, clear the bits that form the 64 bit constant.  */
      t0 &= ~(0x3ffffLL << 46);
      t1 &= ~(0x7fffffLL
	      | ((1LL << 36 | 0xfffffLL << 13) << 23));

      val >>= 4;
      t0 |= ((val >> 20) & 0xffffLL) << 2 << 46;	/* 16 lsbs of imm39 */
      t1 |= ((val >> 36) & 0x7fffffLL) << 0;		/* 23 msbs of imm39 */
      t1 |= ((((val >> 0) & 0xfffffLL) << 13)		/* imm20b */
	      | (((val >> 59) & 0x1LL) << 36)) << 23;	/* i */

      bfd_putl64 (t0, hit_addr);
      bfd_putl64 (t1, hit_addr + 8);
      break;

    default:
      switch ((long) hit_addr & 0x3)
	{
	case 0: shift =  5; break;
	case 1: shift = 14; hit_addr += 3; break;
	case 2: shift = 23; hit_addr += 6; break;
	case 3: return bfd_reloc_notsupported; /* shouldn't happen...  */
	}
      dword = bfd_getl64 (hit_addr);
      insn = (dword >> shift) & 0x1ffffffffffLL;

      op = elf64_ia64_operands + opnd;
      err = (*op->insert) (op, val, &insn);
      if (err)
	return bfd_reloc_overflow;

      dword &= ~(0x1ffffffffffLL << shift);
      dword |= (insn << shift);
      bfd_putl64 (dword, hit_addr);
      break;

    case IA64_OPND_NIL:
      /* A data relocation.  */
      if (bigendian)
	if (size == 4)
	  bfd_putb32 (val, hit_addr);
	else
	  bfd_putb64 (val, hit_addr);
      else
	if (size == 4)
	  bfd_putl32 (val, hit_addr);
	else
	  bfd_putl64 (val, hit_addr);
      break;
    }

  return bfd_reloc_ok;
}

static void
elfNN_ia64_install_dyn_reloc (abfd, info, sec, srel, offset, type,
			      dynindx, addend)
     bfd *abfd;
     struct bfd_link_info *info;
     asection *sec;
     asection *srel;
     bfd_vma offset;
     unsigned int type;
     long dynindx;
     bfd_vma addend;
{
  Elf_Internal_Rela outrel;
  bfd_byte *loc;

  BFD_ASSERT (dynindx != -1);
  outrel.r_info = ELFNN_R_INFO (dynindx, type);
  outrel.r_addend = addend;
  outrel.r_offset = _bfd_elf_section_offset (abfd, info, sec, offset);
  if (outrel.r_offset >= (bfd_vma) -2)
    {
      /* Run for the hills.  We shouldn't be outputting a relocation
	 for this.  So do what everyone else does and output a no-op.  */
      outrel.r_info = ELFNN_R_INFO (0, R_IA64_NONE);
      outrel.r_addend = 0;
      outrel.r_offset = 0;
    }
  else
    outrel.r_offset += sec->output_section->vma + sec->output_offset;

  loc = srel->contents;
  loc += srel->reloc_count++ * sizeof (ElfNN_External_Rela);
  bfd_elfNN_swap_reloca_out (abfd, &outrel, loc);
  BFD_ASSERT (sizeof (ElfNN_External_Rela) * srel->reloc_count <= srel->size);
}

/* Store an entry for target address TARGET_ADDR in the linkage table
   and return the gp-relative address of the linkage table entry.  */

static bfd_vma
set_got_entry (abfd, info, dyn_i, dynindx, addend, value, dyn_r_type)
     bfd *abfd;
     struct bfd_link_info *info;
     struct elfNN_ia64_dyn_sym_info *dyn_i;
     long dynindx;
     bfd_vma addend;
     bfd_vma value;
     unsigned int dyn_r_type;
{
  struct elfNN_ia64_link_hash_table *ia64_info;
  asection *got_sec;
  bfd_boolean done;
  bfd_vma got_offset;

  ia64_info = elfNN_ia64_hash_table (info);
  got_sec = ia64_info->got_sec;

  switch (dyn_r_type)
    {
    case R_IA64_TPREL64LSB:
      done = dyn_i->tprel_done;
      dyn_i->tprel_done = TRUE;
      got_offset = dyn_i->tprel_offset;
      break;
    case R_IA64_DTPMOD64LSB:
      if (dyn_i->dtpmod_offset != ia64_info->self_dtpmod_offset)
	{
	  done = dyn_i->dtpmod_done;
	  dyn_i->dtpmod_done = TRUE;
	}
      else
	{
	  done = ia64_info->self_dtpmod_done;
	  ia64_info->self_dtpmod_done = TRUE;
	  dynindx = 0;
	}
      got_offset = dyn_i->dtpmod_offset;
      break;
    case R_IA64_DTPREL32LSB:
    case R_IA64_DTPREL64LSB:
      done = dyn_i->dtprel_done;
      dyn_i->dtprel_done = TRUE;
      got_offset = dyn_i->dtprel_offset;
      break;
    default:
      done = dyn_i->got_done;
      dyn_i->got_done = TRUE;
      got_offset = dyn_i->got_offset;
      break;
    }

  BFD_ASSERT ((got_offset & 7) == 0);

  if (! done)
    {
      /* Store the target address in the linkage table entry.  */
      bfd_put_64 (abfd, value, got_sec->contents + got_offset);

      /* Install a dynamic relocation if needed.  */
      if (((info->shared
	    && (!dyn_i->h
		|| ELF_ST_VISIBILITY (dyn_i->h->other) == STV_DEFAULT
		|| dyn_i->h->root.type != bfd_link_hash_undefweak)
	    && dyn_r_type != R_IA64_DTPREL32LSB
	    && dyn_r_type != R_IA64_DTPREL64LSB)
           || elfNN_ia64_dynamic_symbol_p (dyn_i->h, info, dyn_r_type)
	   || (dynindx != -1
	       && (dyn_r_type == R_IA64_FPTR32LSB
		   || dyn_r_type == R_IA64_FPTR64LSB)))
	  && (!dyn_i->want_ltoff_fptr
	      || !info->pie
	      || !dyn_i->h
	      || dyn_i->h->root.type != bfd_link_hash_undefweak))
	{
	  if (dynindx == -1
	      && dyn_r_type != R_IA64_TPREL64LSB
	      && dyn_r_type != R_IA64_DTPMOD64LSB
	      && dyn_r_type != R_IA64_DTPREL32LSB
	      && dyn_r_type != R_IA64_DTPREL64LSB)
	    {
	      dyn_r_type = R_IA64_RELNNLSB;
	      dynindx = 0;
	      addend = value;
	    }

	  if (bfd_big_endian (abfd))
	    {
	      switch (dyn_r_type)
		{
		case R_IA64_REL32LSB:
		  dyn_r_type = R_IA64_REL32MSB;
		  break;
		case R_IA64_DIR32LSB:
		  dyn_r_type = R_IA64_DIR32MSB;
		  break;
		case R_IA64_FPTR32LSB:
		  dyn_r_type = R_IA64_FPTR32MSB;
		  break;
		case R_IA64_DTPREL32LSB:
		  dyn_r_type = R_IA64_DTPREL32MSB;
		  break;
		case R_IA64_REL64LSB:
		  dyn_r_type = R_IA64_REL64MSB;
		  break;
		case R_IA64_DIR64LSB:
		  dyn_r_type = R_IA64_DIR64MSB;
		  break;
		case R_IA64_FPTR64LSB:
		  dyn_r_type = R_IA64_FPTR64MSB;
		  break;
		case R_IA64_TPREL64LSB:
		  dyn_r_type = R_IA64_TPREL64MSB;
		  break;
		case R_IA64_DTPMOD64LSB:
		  dyn_r_type = R_IA64_DTPMOD64MSB;
		  break;
		case R_IA64_DTPREL64LSB:
		  dyn_r_type = R_IA64_DTPREL64MSB;
		  break;
		default:
		  BFD_ASSERT (FALSE);
		  break;
		}
	    }

	  elfNN_ia64_install_dyn_reloc (abfd, NULL, got_sec,
					ia64_info->rel_got_sec,
					got_offset, dyn_r_type,
					dynindx, addend);
	}
    }

  /* Return the address of the linkage table entry.  */
  value = (got_sec->output_section->vma
	   + got_sec->output_offset
	   + got_offset);

  return value;
}

/* Fill in a function descriptor consisting of the function's code
   address and its global pointer.  Return the descriptor's address.  */

static bfd_vma
set_fptr_entry (abfd, info, dyn_i, value)
     bfd *abfd;
     struct bfd_link_info *info;
     struct elfNN_ia64_dyn_sym_info *dyn_i;
     bfd_vma value;
{
  struct elfNN_ia64_link_hash_table *ia64_info;
  asection *fptr_sec;

  ia64_info = elfNN_ia64_hash_table (info);
  fptr_sec = ia64_info->fptr_sec;

  if (!dyn_i->fptr_done)
    {
      dyn_i->fptr_done = 1;

      /* Fill in the function descriptor.  */
      bfd_put_64 (abfd, value, fptr_sec->contents + dyn_i->fptr_offset);
      bfd_put_64 (abfd, _bfd_get_gp_value (abfd),
		  fptr_sec->contents + dyn_i->fptr_offset + 8);
      if (ia64_info->rel_fptr_sec)
	{
	  Elf_Internal_Rela outrel;
	  bfd_byte *loc;

	  if (bfd_little_endian (abfd))
	    outrel.r_info = ELFNN_R_INFO (0, R_IA64_IPLTLSB);
	  else
	    outrel.r_info = ELFNN_R_INFO (0, R_IA64_IPLTMSB);
	  outrel.r_addend = value;
	  outrel.r_offset = (fptr_sec->output_section->vma
			     + fptr_sec->output_offset
			     + dyn_i->fptr_offset);
	  loc = ia64_info->rel_fptr_sec->contents;
	  loc += ia64_info->rel_fptr_sec->reloc_count++
		 * sizeof (ElfNN_External_Rela);
	  bfd_elfNN_swap_reloca_out (abfd, &outrel, loc);
	}
    }

  /* Return the descriptor's address.  */
  value = (fptr_sec->output_section->vma
	   + fptr_sec->output_offset
	   + dyn_i->fptr_offset);

  return value;
}

/* Fill in a PLTOFF entry consisting of the function's code address
   and its global pointer.  Return the descriptor's address.  */

static bfd_vma
set_pltoff_entry (abfd, info, dyn_i, value, is_plt)
     bfd *abfd;
     struct bfd_link_info *info;
     struct elfNN_ia64_dyn_sym_info *dyn_i;
     bfd_vma value;
     bfd_boolean is_plt;
{
  struct elfNN_ia64_link_hash_table *ia64_info;
  asection *pltoff_sec;

  ia64_info = elfNN_ia64_hash_table (info);
  pltoff_sec = ia64_info->pltoff_sec;

  /* Don't do anything if this symbol uses a real PLT entry.  In
     that case, we'll fill this in during finish_dynamic_symbol.  */
  if ((! dyn_i->want_plt || is_plt)
      && !dyn_i->pltoff_done)
    {
      bfd_vma gp = _bfd_get_gp_value (abfd);

      /* Fill in the function descriptor.  */
      bfd_put_64 (abfd, value, pltoff_sec->contents + dyn_i->pltoff_offset);
      bfd_put_64 (abfd, gp, pltoff_sec->contents + dyn_i->pltoff_offset + 8);

      /* Install dynamic relocations if needed.  */
      if (!is_plt
	  && info->shared
	  && (!dyn_i->h
	      || ELF_ST_VISIBILITY (dyn_i->h->other) == STV_DEFAULT
	      || dyn_i->h->root.type != bfd_link_hash_undefweak))
	{
	  unsigned int dyn_r_type;

	  if (bfd_big_endian (abfd))
	    dyn_r_type = R_IA64_RELNNMSB;
	  else
	    dyn_r_type = R_IA64_RELNNLSB;

	  elfNN_ia64_install_dyn_reloc (abfd, NULL, pltoff_sec,
					ia64_info->rel_pltoff_sec,
					dyn_i->pltoff_offset,
					dyn_r_type, 0, value);
	  elfNN_ia64_install_dyn_reloc (abfd, NULL, pltoff_sec,
					ia64_info->rel_pltoff_sec,
					dyn_i->pltoff_offset + ARCH_SIZE / 8,
					dyn_r_type, 0, gp);
	}

      dyn_i->pltoff_done = 1;
    }

  /* Return the descriptor's address.  */
  value = (pltoff_sec->output_section->vma
	   + pltoff_sec->output_offset
	   + dyn_i->pltoff_offset);

  return value;
}

/* Return the base VMA address which should be subtracted from real addresses
   when resolving @tprel() relocation.
   Main program TLS (whose template starts at PT_TLS p_vaddr)
   is assigned offset round(2 * size of pointer, PT_TLS p_align).  */

static bfd_vma
elfNN_ia64_tprel_base (info)
     struct bfd_link_info *info;
{
  asection *tls_sec = elf_hash_table (info)->tls_sec;

  BFD_ASSERT (tls_sec != NULL);
  return tls_sec->vma - align_power ((bfd_vma) ARCH_SIZE / 4,
				     tls_sec->alignment_power);
}

/* Return the base VMA address which should be subtracted from real addresses
   when resolving @dtprel() relocation.
   This is PT_TLS segment p_vaddr.  */

static bfd_vma
elfNN_ia64_dtprel_base (info)
     struct bfd_link_info *info;
{
  BFD_ASSERT (elf_hash_table (info)->tls_sec != NULL);
  return elf_hash_table (info)->tls_sec->vma;
}

/* Called through qsort to sort the .IA_64.unwind section during a
   non-relocatable link.  Set elfNN_ia64_unwind_entry_compare_bfd
   to the output bfd so we can do proper endianness frobbing.  */

static bfd *elfNN_ia64_unwind_entry_compare_bfd;

static int
elfNN_ia64_unwind_entry_compare (a, b)
     const PTR a;
     const PTR b;
{
  bfd_vma av, bv;

  av = bfd_get_64 (elfNN_ia64_unwind_entry_compare_bfd, a);
  bv = bfd_get_64 (elfNN_ia64_unwind_entry_compare_bfd, b);

  return (av < bv ? -1 : av > bv ? 1 : 0);
}

/* Make sure we've got ourselves a nice fat __gp value.  */
static bfd_boolean
elfNN_ia64_choose_gp (abfd, info)
     bfd *abfd;
     struct bfd_link_info *info;
{
  bfd_vma min_vma = (bfd_vma) -1, max_vma = 0;
  bfd_vma min_short_vma = min_vma, max_short_vma = 0;
  struct elf_link_hash_entry *gp;
  bfd_vma gp_val;
  asection *os;
  struct elfNN_ia64_link_hash_table *ia64_info;

  ia64_info = elfNN_ia64_hash_table (info);

  /* Find the min and max vma of all sections marked short.  Also collect
     min and max vma of any type, for use in selecting a nice gp.  */
  for (os = abfd->sections; os ; os = os->next)
    {
      bfd_vma lo, hi;

      if ((os->flags & SEC_ALLOC) == 0)
	continue;

      lo = os->vma;
      hi = os->vma + (os->rawsize ? os->rawsize : os->size);
      if (hi < lo)
	hi = (bfd_vma) -1;

      if (min_vma > lo)
	min_vma = lo;
      if (max_vma < hi)
	max_vma = hi;
      if (os->flags & SEC_SMALL_DATA)
	{
	  if (min_short_vma > lo)
	    min_short_vma = lo;
	  if (max_short_vma < hi)
	    max_short_vma = hi;
	}
    }

  /* See if the user wants to force a value.  */
  gp = elf_link_hash_lookup (elf_hash_table (info), "__gp", FALSE,
			     FALSE, FALSE);

  if (gp
      && (gp->root.type == bfd_link_hash_defined
	  || gp->root.type == bfd_link_hash_defweak))
    {
      asection *gp_sec = gp->root.u.def.section;
      gp_val = (gp->root.u.def.value
		+ gp_sec->output_section->vma
		+ gp_sec->output_offset);
    }
  else
    {
      /* Pick a sensible value.  */

      asection *got_sec = ia64_info->got_sec;

      /* Start with just the address of the .got.  */
      if (got_sec)
	gp_val = got_sec->output_section->vma;
      else if (max_short_vma != 0)
	gp_val = min_short_vma;
      else if (max_vma - min_vma < 0x200000)
	gp_val = min_vma;
      else
	gp_val = max_vma - 0x200000 + 8;

      /* If it is possible to address the entire image, but we
	 don't with the choice above, adjust.  */
      if (max_vma - min_vma < 0x400000
	  && (max_vma - gp_val >= 0x200000
	      || gp_val - min_vma > 0x200000))
	gp_val = min_vma + 0x200000;
      else if (max_short_vma != 0)
	{
	  /* If we don't cover all the short data, adjust.  */
	  if (max_short_vma - gp_val >= 0x200000)
	    gp_val = min_short_vma + 0x200000;

	  /* If we're addressing stuff past the end, adjust back.  */
	  if (gp_val > max_vma)
	    gp_val = max_vma - 0x200000 + 8;
	}
    }

  /* Validate whether all SHF_IA_64_SHORT sections are within
     range of the chosen GP.  */

  if (max_short_vma != 0)
    {
      if (max_short_vma - min_short_vma >= 0x400000)
	{
	  (*_bfd_error_handler)
	    (_("%s: short data segment overflowed (0x%lx >= 0x400000)"),
	     bfd_get_filename (abfd),
	     (unsigned long) (max_short_vma - min_short_vma));
	  return FALSE;
	}
      else if ((gp_val > min_short_vma
		&& gp_val - min_short_vma > 0x200000)
	       || (gp_val < max_short_vma
		   && max_short_vma - gp_val >= 0x200000))
	{
	  (*_bfd_error_handler)
	    (_("%s: __gp does not cover short data segment"),
	     bfd_get_filename (abfd));
	  return FALSE;
	}
    }

  _bfd_set_gp_value (abfd, gp_val);

  return TRUE;
}

static bfd_boolean
elfNN_ia64_final_link (abfd, info)
     bfd *abfd;
     struct bfd_link_info *info;
{
  struct elfNN_ia64_link_hash_table *ia64_info;
  asection *unwind_output_sec;

  ia64_info = elfNN_ia64_hash_table (info);

  /* Make sure we've got ourselves a nice fat __gp value.  */
  if (!info->relocatable)
    {
      bfd_vma gp_val;
      struct elf_link_hash_entry *gp;

      /* We assume after gp is set, section size will only decrease. We
	 need to adjust gp for it.  */
      _bfd_set_gp_value (abfd, 0);
      if (! elfNN_ia64_choose_gp (abfd, info))
	return FALSE;
      gp_val = _bfd_get_gp_value (abfd);

      gp = elf_link_hash_lookup (elf_hash_table (info), "__gp", FALSE,
			         FALSE, FALSE);
      if (gp)
	{
	  gp->root.type = bfd_link_hash_defined;
	  gp->root.u.def.value = gp_val;
	  gp->root.u.def.section = bfd_abs_section_ptr;
	}
    }

  /* If we're producing a final executable, we need to sort the contents
     of the .IA_64.unwind section.  Force this section to be relocated
     into memory rather than written immediately to the output file.  */
  unwind_output_sec = NULL;
  if (!info->relocatable)
    {
      asection *s = bfd_get_section_by_name (abfd, ELF_STRING_ia64_unwind);
      if (s)
	{
	  unwind_output_sec = s->output_section;
	  unwind_output_sec->contents
	    = bfd_malloc (unwind_output_sec->size);
	  if (unwind_output_sec->contents == NULL)
	    return FALSE;
	}
    }

  /* Invoke the regular ELF backend linker to do all the work.  */
  if (!bfd_elf_final_link (abfd, info))
    return FALSE;

  if (unwind_output_sec)
    {
      elfNN_ia64_unwind_entry_compare_bfd = abfd;
      qsort (unwind_output_sec->contents,
	     (size_t) (unwind_output_sec->size / 24),
	     24,
	     elfNN_ia64_unwind_entry_compare);

      if (! bfd_set_section_contents (abfd, unwind_output_sec,
				      unwind_output_sec->contents, (bfd_vma) 0,
				      unwind_output_sec->size))
	return FALSE;
    }

  return TRUE;
}

static bfd_boolean
elfNN_ia64_relocate_section (output_bfd, info, input_bfd, input_section,
			     contents, relocs, local_syms, local_sections)
     bfd *output_bfd;
     struct bfd_link_info *info;
     bfd *input_bfd;
     asection *input_section;
     bfd_byte *contents;
     Elf_Internal_Rela *relocs;
     Elf_Internal_Sym *local_syms;
     asection **local_sections;
{
  struct elfNN_ia64_link_hash_table *ia64_info;
  Elf_Internal_Shdr *symtab_hdr;
  Elf_Internal_Rela *rel;
  Elf_Internal_Rela *relend;
  asection *srel;
  bfd_boolean ret_val = TRUE;	/* for non-fatal errors */
  bfd_vma gp_val;

  symtab_hdr = &elf_tdata (input_bfd)->symtab_hdr;
  ia64_info = elfNN_ia64_hash_table (info);

  /* Infect various flags from the input section to the output section.  */
  if (info->relocatable)
    {
      bfd_vma flags;

      flags = elf_section_data(input_section)->this_hdr.sh_flags;
      flags &= SHF_IA_64_NORECOV;

      elf_section_data(input_section->output_section)
	->this_hdr.sh_flags |= flags;
    }

  gp_val = _bfd_get_gp_value (output_bfd);
  srel = get_reloc_section (input_bfd, ia64_info, input_section, FALSE);

  rel = relocs;
  relend = relocs + input_section->reloc_count;
  for (; rel < relend; ++rel)
    {
      struct elf_link_hash_entry *h;
      struct elfNN_ia64_dyn_sym_info *dyn_i;
      bfd_reloc_status_type r;
      reloc_howto_type *howto;
      unsigned long r_symndx;
      Elf_Internal_Sym *sym;
      unsigned int r_type;
      bfd_vma value;
      asection *sym_sec;
      bfd_byte *hit_addr;
      bfd_boolean dynamic_symbol_p;
      bfd_boolean undef_weak_ref;

      r_type = ELFNN_R_TYPE (rel->r_info);
      if (r_type > R_IA64_MAX_RELOC_CODE)
	{
	  (*_bfd_error_handler)
	    (_("%B: unknown relocation type %d"),
	     input_bfd, (int) r_type);
	  bfd_set_error (bfd_error_bad_value);
	  ret_val = FALSE;
	  continue;
	}

      howto = lookup_howto (r_type);
      r_symndx = ELFNN_R_SYM (rel->r_info);
      h = NULL;
      sym = NULL;
      sym_sec = NULL;
      undef_weak_ref = FALSE;

      if (r_symndx < symtab_hdr->sh_info)
	{
	  /* Reloc against local symbol.  */
	  asection *msec;
	  sym = local_syms + r_symndx;
	  sym_sec = local_sections[r_symndx];
	  msec = sym_sec;
	  value = _bfd_elf_rela_local_sym (output_bfd, sym, &msec, rel);
	  if (!info->relocatable
	      && (sym_sec->flags & SEC_MERGE) != 0
	      && ELF_ST_TYPE (sym->st_info) == STT_SECTION
	      && sym_sec->sec_info_type == ELF_INFO_TYPE_MERGE)
 	    {
	      struct elfNN_ia64_local_hash_entry *loc_h;

	      loc_h = get_local_sym_hash (ia64_info, input_bfd, rel, FALSE);
	      if (loc_h && ! loc_h->sec_merge_done)
		{
		  struct elfNN_ia64_dyn_sym_info *dynent;
		  unsigned int count;

		  for (count = loc_h->count, dynent = loc_h->info;
		       count != 0;
		       count--, dynent++)
		    {
		      msec = sym_sec;
		      dynent->addend =
			_bfd_merged_section_offset (output_bfd, &msec,
						    elf_section_data (msec)->
						    sec_info,
						    sym->st_value
						    + dynent->addend);
		      dynent->addend -= sym->st_value;
		      dynent->addend += msec->output_section->vma
					+ msec->output_offset
					- sym_sec->output_section->vma
					- sym_sec->output_offset;
		    }

		  /* We may have introduced duplicated entries. We need
		     to remove them properly.  */
		  count = sort_dyn_sym_info (loc_h->info, loc_h->count);
		  if (count != loc_h->count)
		    {
		      loc_h->count = count;
		      loc_h->sorted_count = count;
		    }

		  loc_h->sec_merge_done = 1;
		}
	    }
	}
      else
	{
	  bfd_boolean unresolved_reloc;
	  bfd_boolean warned;
	  struct elf_link_hash_entry **sym_hashes = elf_sym_hashes (input_bfd);

	  RELOC_FOR_GLOBAL_SYMBOL (info, input_bfd, input_section, rel,
				   r_symndx, symtab_hdr, sym_hashes,
				   h, sym_sec, value,
				   unresolved_reloc, warned);

	  if (h->root.type == bfd_link_hash_undefweak)
	    undef_weak_ref = TRUE;
	  else if (warned)
	    continue;
	}

      /* For relocs against symbols from removed linkonce sections,
	 or sections discarded by a linker script, we just want the
	 section contents zeroed.  Avoid any special processing.  */
      if (sym_sec != NULL && elf_discarded_section (sym_sec))
	{
	  _bfd_clear_contents (howto, input_bfd, contents + rel->r_offset);
	  rel->r_info = 0;
	  rel->r_addend = 0;
	  continue;
	}

      if (info->relocatable)
	continue;

      hit_addr = contents + rel->r_offset;
      value += rel->r_addend;
      dynamic_symbol_p = elfNN_ia64_dynamic_symbol_p (h, info, r_type);

      switch (r_type)
	{
	case R_IA64_NONE:
	case R_IA64_LDXMOV:
	  continue;

	case R_IA64_IMM14:
	case R_IA64_IMM22:
	case R_IA64_IMM64:
	case R_IA64_DIR32MSB:
	case R_IA64_DIR32LSB:
	case R_IA64_DIR64MSB:
	case R_IA64_DIR64LSB:
	  /* Install a dynamic relocation for this reloc.  */
	  if ((dynamic_symbol_p || info->shared)
	      && r_symndx != 0
	      && (input_section->flags & SEC_ALLOC) != 0)
	    {
	      unsigned int dyn_r_type;
	      long dynindx;
	      bfd_vma addend;

	      BFD_ASSERT (srel != NULL);

	      switch (r_type)
		{
		case R_IA64_IMM14:
		case R_IA64_IMM22:
		case R_IA64_IMM64:
		  /* ??? People shouldn't be doing non-pic code in
		     shared libraries nor dynamic executables.  */
		  (*_bfd_error_handler)
		    (_("%B: non-pic code with imm relocation against dynamic symbol `%s'"),
		     input_bfd,
		     h ? h->root.root.string
		       : bfd_elf_sym_name (input_bfd, symtab_hdr, sym,
					   sym_sec));
		  ret_val = FALSE;
		  continue;

		default:
		  break;
		}

	      /* If we don't need dynamic symbol lookup, find a
		 matching RELATIVE relocation.  */
	      dyn_r_type = r_type;
	      if (dynamic_symbol_p)
		{
		  dynindx = h->dynindx;
		  addend = rel->r_addend;
		  value = 0;
		}
	      else
		{
		  switch (r_type)
		    {
		    case R_IA64_DIR32MSB:
		      dyn_r_type = R_IA64_REL32MSB;
		      break;
		    case R_IA64_DIR32LSB:
		      dyn_r_type = R_IA64_REL32LSB;
		      break;
		    case R_IA64_DIR64MSB:
		      dyn_r_type = R_IA64_REL64MSB;
		      break;
		    case R_IA64_DIR64LSB:
		      dyn_r_type = R_IA64_REL64LSB;
		      break;

		    default:
		      break;
		    }
		  dynindx = 0;
		  addend = value;
		}

	      elfNN_ia64_install_dyn_reloc (output_bfd, info, input_section,
					    srel, rel->r_offset, dyn_r_type,
					    dynindx, addend);
	    }
	  /* Fall through.  */

	case R_IA64_LTV32MSB:
	case R_IA64_LTV32LSB:
	case R_IA64_LTV64MSB:
	case R_IA64_LTV64LSB:
	  r = elfNN_ia64_install_value (hit_addr, value, r_type);
	  break;

	case R_IA64_GPREL22:
	case R_IA64_GPREL64I:
	case R_IA64_GPREL32MSB:
	case R_IA64_GPREL32LSB:
	case R_IA64_GPREL64MSB:
	case R_IA64_GPREL64LSB:
	  if (dynamic_symbol_p)
	    {
	      (*_bfd_error_handler)
		(_("%B: @gprel relocation against dynamic symbol %s"),
		 input_bfd,
		 h ? h->root.root.string
		   : bfd_elf_sym_name (input_bfd, symtab_hdr, sym,
				       sym_sec));
	      ret_val = FALSE;
	      continue;
	    }
	  value -= gp_val;
	  r = elfNN_ia64_install_value (hit_addr, value, r_type);
	  break;

	case R_IA64_LTOFF22:
	case R_IA64_LTOFF22X:
	case R_IA64_LTOFF64I:
          dyn_i = get_dyn_sym_info (ia64_info, h, input_bfd, rel, FALSE);
	  value = set_got_entry (input_bfd, info, dyn_i, (h ? h->dynindx : -1),
				 rel->r_addend, value, R_IA64_DIRNNLSB);
	  value -= gp_val;
	  r = elfNN_ia64_install_value (hit_addr, value, r_type);
	  break;

	case R_IA64_PLTOFF22:
	case R_IA64_PLTOFF64I:
	case R_IA64_PLTOFF64MSB:
	case R_IA64_PLTOFF64LSB:
          dyn_i = get_dyn_sym_info (ia64_info, h, input_bfd, rel, FALSE);
	  value = set_pltoff_entry (output_bfd, info, dyn_i, value, FALSE);
	  value -= gp_val;
	  r = elfNN_ia64_install_value (hit_addr, value, r_type);
	  break;

	case R_IA64_FPTR64I:
	case R_IA64_FPTR32MSB:
	case R_IA64_FPTR32LSB:
	case R_IA64_FPTR64MSB:
	case R_IA64_FPTR64LSB:
          dyn_i = get_dyn_sym_info (ia64_info, h, input_bfd, rel, FALSE);
	  if (dyn_i->want_fptr)
	    {
	      if (!undef_weak_ref)
		value = set_fptr_entry (output_bfd, info, dyn_i, value);
	    }
	  if (!dyn_i->want_fptr || info->pie)
	    {
	      long dynindx;
	      unsigned int dyn_r_type = r_type;
	      bfd_vma addend = rel->r_addend;

	      /* Otherwise, we expect the dynamic linker to create
		 the entry.  */

	      if (dyn_i->want_fptr)
		{
		  if (r_type == R_IA64_FPTR64I)
		    {
		      /* We can't represent this without a dynamic symbol.
			 Adjust the relocation to be against an output
			 section symbol, which are always present in the
			 dynamic symbol table.  */
		      /* ??? People shouldn't be doing non-pic code in
			 shared libraries.  Hork.  */
		      (*_bfd_error_handler)
			(_("%B: linking non-pic code in a position independent executable"),
			 input_bfd);
		      ret_val = FALSE;
		      continue;
		    }
		  dynindx = 0;
		  addend = value;
		  dyn_r_type = r_type + R_IA64_RELNNLSB - R_IA64_FPTRNNLSB;
		}
	      else if (h)
		{
		  if (h->dynindx != -1)
		    dynindx = h->dynindx;
		  else
		    dynindx = (_bfd_elf_link_lookup_local_dynindx
			       (info, h->root.u.def.section->owner,
				global_sym_index (h)));
		  value = 0;
		}
	      else
		{
		  dynindx = (_bfd_elf_link_lookup_local_dynindx
			     (info, input_bfd, (long) r_symndx));
		  value = 0;
		}

	      elfNN_ia64_install_dyn_reloc (output_bfd, info, input_section,
					    srel, rel->r_offset, dyn_r_type,
					    dynindx, addend);
	    }

	  r = elfNN_ia64_install_value (hit_addr, value, r_type);
	  break;

	case R_IA64_LTOFF_FPTR22:
	case R_IA64_LTOFF_FPTR64I:
	case R_IA64_LTOFF_FPTR32MSB:
	case R_IA64_LTOFF_FPTR32LSB:
	case R_IA64_LTOFF_FPTR64MSB:
	case R_IA64_LTOFF_FPTR64LSB:
	  {
	    long dynindx;

	    dyn_i = get_dyn_sym_info (ia64_info, h, input_bfd, rel, FALSE);
	    if (dyn_i->want_fptr)
	      {
		BFD_ASSERT (h == NULL || h->dynindx == -1);
	        if (!undef_weak_ref)
	          value = set_fptr_entry (output_bfd, info, dyn_i, value);
		dynindx = -1;
	      }
	    else
	      {
	        /* Otherwise, we expect the dynamic linker to create
		   the entry.  */
	        if (h)
		  {
		    if (h->dynindx != -1)
		      dynindx = h->dynindx;
		    else
		      dynindx = (_bfd_elf_link_lookup_local_dynindx
				 (info, h->root.u.def.section->owner,
				  global_sym_index (h)));
		  }
		else
		  dynindx = (_bfd_elf_link_lookup_local_dynindx
			     (info, input_bfd, (long) r_symndx));
		value = 0;
	      }

	    value = set_got_entry (output_bfd, info, dyn_i, dynindx,
				   rel->r_addend, value, R_IA64_FPTRNNLSB);
	    value -= gp_val;
	    r = elfNN_ia64_install_value (hit_addr, value, r_type);
	  }
	  break;

	case R_IA64_PCREL32MSB:
	case R_IA64_PCREL32LSB:
	case R_IA64_PCREL64MSB:
	case R_IA64_PCREL64LSB:
	  /* Install a dynamic relocation for this reloc.  */
	  if (dynamic_symbol_p && r_symndx != 0)
	    {
	      BFD_ASSERT (srel != NULL);

	      elfNN_ia64_install_dyn_reloc (output_bfd, info, input_section,
					    srel, rel->r_offset, r_type,
					    h->dynindx, rel->r_addend);
	    }
	  goto finish_pcrel;

	case R_IA64_PCREL21B:
	case R_IA64_PCREL60B:
	  /* We should have created a PLT entry for any dynamic symbol.  */
	  dyn_i = NULL;
	  if (h)
	    dyn_i = get_dyn_sym_info (ia64_info, h, NULL, NULL, FALSE);

	  if (dyn_i && dyn_i->want_plt2)
	    {
	      /* Should have caught this earlier.  */
	      BFD_ASSERT (rel->r_addend == 0);

	      value = (ia64_info->plt_sec->output_section->vma
		       + ia64_info->plt_sec->output_offset
		       + dyn_i->plt2_offset);
	    }
	  else
	    {
	      /* Since there's no PLT entry, Validate that this is
		 locally defined.  */
	      BFD_ASSERT (undef_weak_ref || sym_sec->output_section != NULL);

	      /* If the symbol is undef_weak, we shouldn't be trying
		 to call it.  There's every chance that we'd wind up
		 with an out-of-range fixup here.  Don't bother setting
		 any value at all.  */
	      if (undef_weak_ref)
		continue;
	    }
	  goto finish_pcrel;

	case R_IA64_PCREL21BI:
	case R_IA64_PCREL21F:
	case R_IA64_PCREL21M:
	case R_IA64_PCREL22:
	case R_IA64_PCREL64I:
	  /* The PCREL21BI reloc is specifically not intended for use with
	     dynamic relocs.  PCREL21F and PCREL21M are used for speculation
	     fixup code, and thus probably ought not be dynamic.  The
	     PCREL22 and PCREL64I relocs aren't emitted as dynamic relocs.  */
	  if (dynamic_symbol_p)
	    {
	      const char *msg;

	      if (r_type == R_IA64_PCREL21BI)
		msg = _("%B: @internal branch to dynamic symbol %s");
	      else if (r_type == R_IA64_PCREL21F || r_type == R_IA64_PCREL21M)
		msg = _("%B: speculation fixup to dynamic symbol %s");
	      else
		msg = _("%B: @pcrel relocation against dynamic symbol %s");
	      (*_bfd_error_handler) (msg, input_bfd,
				     h ? h->root.root.string
				       : bfd_elf_sym_name (input_bfd,
							   symtab_hdr,
							   sym,
							   sym_sec));
	      ret_val = FALSE;
	      continue;
	    }
	  goto finish_pcrel;

	finish_pcrel:
	  /* Make pc-relative.  */
	  value -= (input_section->output_section->vma
		    + input_section->output_offset
		    + rel->r_offset) & ~ (bfd_vma) 0x3;
	  r = elfNN_ia64_install_value (hit_addr, value, r_type);
	  break;

	case R_IA64_SEGREL32MSB:
	case R_IA64_SEGREL32LSB:
	case R_IA64_SEGREL64MSB:
	case R_IA64_SEGREL64LSB:
	    {
	      struct elf_segment_map *m;
	      Elf_Internal_Phdr *p;

	      /* Find the segment that contains the output_section.  */
	      for (m = elf_tdata (output_bfd)->segment_map,
		     p = elf_tdata (output_bfd)->phdr;
		   m != NULL;
		   m = m->next, p++)
		{
		  int i;
		  for (i = m->count - 1; i >= 0; i--)
		    if (m->sections[i] == input_section->output_section)
		      break;
		  if (i >= 0)
		    break;
		}

	      if (m == NULL)
		{
		  r = bfd_reloc_notsupported;
		}
	      else
		{
		  /* The VMA of the segment is the vaddr of the associated
		     program header.  */
		  if (value > p->p_vaddr)
		    value -= p->p_vaddr;
		  else
		    value = 0;
		  r = elfNN_ia64_install_value (hit_addr, value, r_type);
		}
	      break;
	    }

	case R_IA64_SECREL32MSB:
	case R_IA64_SECREL32LSB:
	case R_IA64_SECREL64MSB:
	case R_IA64_SECREL64LSB:
	  /* Make output-section relative to section where the symbol
	     is defined. PR 475  */
	  if (sym_sec)
	    value -= sym_sec->output_section->vma;
	  r = elfNN_ia64_install_value (hit_addr, value, r_type);
	  break;

	case R_IA64_IPLTMSB:
	case R_IA64_IPLTLSB:
	  /* Install a dynamic relocation for this reloc.  */
	  if ((dynamic_symbol_p || info->shared)
	      && (input_section->flags & SEC_ALLOC) != 0)
	    {
	      BFD_ASSERT (srel != NULL);

	      /* If we don't need dynamic symbol lookup, install two
		 RELATIVE relocations.  */
	      if (!dynamic_symbol_p)
		{
		  unsigned int dyn_r_type;

		  if (r_type == R_IA64_IPLTMSB)
		    dyn_r_type = R_IA64_REL64MSB;
		  else
		    dyn_r_type = R_IA64_REL64LSB;

		  elfNN_ia64_install_dyn_reloc (output_bfd, info,
						input_section,
						srel, rel->r_offset,
						dyn_r_type, 0, value);
		  elfNN_ia64_install_dyn_reloc (output_bfd, info,
						input_section,
						srel, rel->r_offset + 8,
						dyn_r_type, 0, gp_val);
		}
	      else
		elfNN_ia64_install_dyn_reloc (output_bfd, info, input_section,
					      srel, rel->r_offset, r_type,
					      h->dynindx, rel->r_addend);
	    }

	  if (r_type == R_IA64_IPLTMSB)
	    r_type = R_IA64_DIR64MSB;
	  else
	    r_type = R_IA64_DIR64LSB;
	  elfNN_ia64_install_value (hit_addr, value, r_type);
	  r = elfNN_ia64_install_value (hit_addr + 8, gp_val, r_type);
	  break;

	case R_IA64_TPREL14:
	case R_IA64_TPREL22:
	case R_IA64_TPREL64I:
	  value -= elfNN_ia64_tprel_base (info);
	  r = elfNN_ia64_install_value (hit_addr, value, r_type);
	  break;

	case R_IA64_DTPREL14:
	case R_IA64_DTPREL22:
	case R_IA64_DTPREL64I:
	case R_IA64_DTPREL32LSB:
	case R_IA64_DTPREL32MSB:
	case R_IA64_DTPREL64LSB:
	case R_IA64_DTPREL64MSB:
	  value -= elfNN_ia64_dtprel_base (info);
	  r = elfNN_ia64_install_value (hit_addr, value, r_type);
	  break;

	case R_IA64_LTOFF_TPREL22:
	case R_IA64_LTOFF_DTPMOD22:
	case R_IA64_LTOFF_DTPREL22:
	  {
	    int got_r_type;
	    long dynindx = h ? h->dynindx : -1;
	    bfd_vma r_addend = rel->r_addend;

	    switch (r_type)
	      {
	      default:
	      case R_IA64_LTOFF_TPREL22:
		if (!dynamic_symbol_p)
		  {
		    if (!info->shared)
		      value -= elfNN_ia64_tprel_base (info);
		    else
		      {
			r_addend += value - elfNN_ia64_dtprel_base (info);
			dynindx = 0;
		      }
		  }
		got_r_type = R_IA64_TPREL64LSB;
		break;
	      case R_IA64_LTOFF_DTPMOD22:
		if (!dynamic_symbol_p && !info->shared)
		  value = 1;
		got_r_type = R_IA64_DTPMOD64LSB;
		break;
	      case R_IA64_LTOFF_DTPREL22:
		if (!dynamic_symbol_p)
		  value -= elfNN_ia64_dtprel_base (info);
		got_r_type = R_IA64_DTPRELNNLSB;
		break;
	      }
	    dyn_i = get_dyn_sym_info (ia64_info, h, input_bfd, rel, FALSE);
	    value = set_got_entry (input_bfd, info, dyn_i, dynindx, r_addend,
				   value, got_r_type);
	    value -= gp_val;
	    r = elfNN_ia64_install_value (hit_addr, value, r_type);
	  }
	  break;

	default:
	  r = bfd_reloc_notsupported;
	  break;
	}

      switch (r)
	{
	case bfd_reloc_ok:
	  break;

	case bfd_reloc_undefined:
	  /* This can happen for global table relative relocs if
	     __gp is undefined.  This is a panic situation so we
	     don't try to continue.  */
	  (*info->callbacks->undefined_symbol)
	    (info, "__gp", input_bfd, input_section, rel->r_offset, 1);
	  return FALSE;

	case bfd_reloc_notsupported:
	  {
	    const char *name;

	    if (h)
	      name = h->root.root.string;
	    else
	      name = bfd_elf_sym_name (input_bfd, symtab_hdr, sym,
				       sym_sec);
	    if (!(*info->callbacks->warning) (info, _("unsupported reloc"),
					      name, input_bfd,
					      input_section, rel->r_offset))
	      return FALSE;
	    ret_val = FALSE;
	  }
	  break;

	case bfd_reloc_dangerous:
	case bfd_reloc_outofrange:
	case bfd_reloc_overflow:
	default:
	  {
	    const char *name;

	    if (h)
	      name = h->root.root.string;
	    else
	      name = bfd_elf_sym_name (input_bfd, symtab_hdr, sym,
				       sym_sec);

	    switch (r_type)
	      {
	      case R_IA64_PCREL21B:
	      case R_IA64_PCREL21BI:
	      case R_IA64_PCREL21M:
	      case R_IA64_PCREL21F:
		if (is_elf_hash_table (info->hash))
		  {
		    /* Relaxtion is always performed for ELF output.
		       Overflow failures for those relocations mean
		       that the section is too big to relax.  */
		    (*_bfd_error_handler)
		      (_("%B: Can't relax br (%s) to `%s' at 0x%lx in section `%A' with size 0x%lx (> 0x1000000)."),
		       input_bfd, input_section, howto->name, name,
		       rel->r_offset, input_section->size);
		    break;
		  }
	      default:
		if (!(*info->callbacks->reloc_overflow) (info,
							 &h->root,
							 name,
							 howto->name,
							 (bfd_vma) 0,
							 input_bfd,
							 input_section,
							 rel->r_offset))
		  return FALSE;
		break;
	      }

	    ret_val = FALSE;
	  }
	  break;
	}
    }

  return ret_val;
}

static bfd_boolean
elfNN_ia64_finish_dynamic_symbol (output_bfd, info, h, sym)
     bfd *output_bfd;
     struct bfd_link_info *info;
     struct elf_link_hash_entry *h;
     Elf_Internal_Sym *sym;
{
  struct elfNN_ia64_link_hash_table *ia64_info;
  struct elfNN_ia64_dyn_sym_info *dyn_i;

  ia64_info = elfNN_ia64_hash_table (info);
  dyn_i = get_dyn_sym_info (ia64_info, h, NULL, NULL, FALSE);

  /* Fill in the PLT data, if required.  */
  if (dyn_i && dyn_i->want_plt)
    {
      Elf_Internal_Rela outrel;
      bfd_byte *loc;
      asection *plt_sec;
      bfd_vma plt_addr, pltoff_addr, gp_val, index;

      gp_val = _bfd_get_gp_value (output_bfd);

      /* Initialize the minimal PLT entry.  */

      index = (dyn_i->plt_offset - PLT_HEADER_SIZE) / PLT_MIN_ENTRY_SIZE;
      plt_sec = ia64_info->plt_sec;
      loc = plt_sec->contents + dyn_i->plt_offset;

      memcpy (loc, plt_min_entry, PLT_MIN_ENTRY_SIZE);
      elfNN_ia64_install_value (loc, index, R_IA64_IMM22);
      elfNN_ia64_install_value (loc+2, -dyn_i->plt_offset, R_IA64_PCREL21B);

      plt_addr = (plt_sec->output_section->vma
		  + plt_sec->output_offset
		  + dyn_i->plt_offset);
      pltoff_addr = set_pltoff_entry (output_bfd, info, dyn_i, plt_addr, TRUE);

      /* Initialize the FULL PLT entry, if needed.  */
      if (dyn_i->want_plt2)
	{
	  loc = plt_sec->contents + dyn_i->plt2_offset;

	  memcpy (loc, plt_full_entry, PLT_FULL_ENTRY_SIZE);
	  elfNN_ia64_install_value (loc, pltoff_addr - gp_val, R_IA64_IMM22);

	  /* Mark the symbol as undefined, rather than as defined in the
	     plt section.  Leave the value alone.  */
	  /* ??? We didn't redefine it in adjust_dynamic_symbol in the
	     first place.  But perhaps elflink.c did some for us.  */
	  if (!h->def_regular)
	    sym->st_shndx = SHN_UNDEF;
	}

      /* Create the dynamic relocation.  */
      outrel.r_offset = pltoff_addr;
      if (bfd_little_endian (output_bfd))
	outrel.r_info = ELFNN_R_INFO (h->dynindx, R_IA64_IPLTLSB);
      else
	outrel.r_info = ELFNN_R_INFO (h->dynindx, R_IA64_IPLTMSB);
      outrel.r_addend = 0;

      /* This is fun.  In the .IA_64.pltoff section, we've got entries
	 that correspond both to real PLT entries, and those that
	 happened to resolve to local symbols but need to be created
	 to satisfy @pltoff relocations.  The .rela.IA_64.pltoff
	 relocations for the real PLT should come at the end of the
	 section, so that they can be indexed by plt entry at runtime.

	 We emitted all of the relocations for the non-PLT @pltoff
	 entries during relocate_section.  So we can consider the
	 existing sec->reloc_count to be the base of the array of
	 PLT relocations.  */

      loc = ia64_info->rel_pltoff_sec->contents;
      loc += ((ia64_info->rel_pltoff_sec->reloc_count + index)
	      * sizeof (ElfNN_External_Rela));
      bfd_elfNN_swap_reloca_out (output_bfd, &outrel, loc);
    }

  /* Mark some specially defined symbols as absolute.  */
  if (strcmp (h->root.root.string, "_DYNAMIC") == 0
      || h == ia64_info->root.hgot
      || h == ia64_info->root.hplt)
    sym->st_shndx = SHN_ABS;

  return TRUE;
}

static bfd_boolean
elfNN_ia64_finish_dynamic_sections (abfd, info)
     bfd *abfd;
     struct bfd_link_info *info;
{
  struct elfNN_ia64_link_hash_table *ia64_info;
  bfd *dynobj;

  ia64_info = elfNN_ia64_hash_table (info);
  dynobj = ia64_info->root.dynobj;

  if (elf_hash_table (info)->dynamic_sections_created)
    {
      ElfNN_External_Dyn *dyncon, *dynconend;
      asection *sdyn, *sgotplt;
      bfd_vma gp_val;

      sdyn = bfd_get_section_by_name (dynobj, ".dynamic");
      sgotplt = bfd_get_section_by_name (dynobj, ".got.plt");
      BFD_ASSERT (sdyn != NULL);
      dyncon = (ElfNN_External_Dyn *) sdyn->contents;
      dynconend = (ElfNN_External_Dyn *) (sdyn->contents + sdyn->size);

      gp_val = _bfd_get_gp_value (abfd);

      for (; dyncon < dynconend; dyncon++)
	{
	  Elf_Internal_Dyn dyn;

	  bfd_elfNN_swap_dyn_in (dynobj, dyncon, &dyn);

	  switch (dyn.d_tag)
	    {
	    case DT_PLTGOT:
	      dyn.d_un.d_ptr = gp_val;
	      break;

	    case DT_PLTRELSZ:
	      dyn.d_un.d_val = (ia64_info->minplt_entries
				* sizeof (ElfNN_External_Rela));
	      break;

	    case DT_JMPREL:
	      /* See the comment above in finish_dynamic_symbol.  */
	      dyn.d_un.d_ptr = (ia64_info->rel_pltoff_sec->output_section->vma
				+ ia64_info->rel_pltoff_sec->output_offset
				+ (ia64_info->rel_pltoff_sec->reloc_count
				   * sizeof (ElfNN_External_Rela)));
	      break;

	    case DT_IA_64_PLT_RESERVE:
	      dyn.d_un.d_ptr = (sgotplt->output_section->vma
				+ sgotplt->output_offset);
	      break;

	    case DT_RELASZ:
	      /* Do not have RELASZ include JMPREL.  This makes things
		 easier on ld.so.  This is not what the rest of BFD set up.  */
	      dyn.d_un.d_val -= (ia64_info->minplt_entries
				 * sizeof (ElfNN_External_Rela));
	      break;
	    }

	  bfd_elfNN_swap_dyn_out (abfd, &dyn, dyncon);
	}

      /* Initialize the PLT0 entry.  */
      if (ia64_info->plt_sec)
	{
	  bfd_byte *loc = ia64_info->plt_sec->contents;
	  bfd_vma pltres;

	  memcpy (loc, plt_header, PLT_HEADER_SIZE);

	  pltres = (sgotplt->output_section->vma
		    + sgotplt->output_offset
		    - gp_val);

	  elfNN_ia64_install_value (loc+1, pltres, R_IA64_GPREL22);
	}
    }

  return TRUE;
}

/* ELF file flag handling:  */

/* Function to keep IA-64 specific file flags.  */
static bfd_boolean
elfNN_ia64_set_private_flags (abfd, flags)
     bfd *abfd;
     flagword flags;
{
  BFD_ASSERT (!elf_flags_init (abfd)
	      || elf_elfheader (abfd)->e_flags == flags);

  elf_elfheader (abfd)->e_flags = flags;
  elf_flags_init (abfd) = TRUE;
  return TRUE;
}

/* Merge backend specific data from an object file to the output
   object file when linking.  */
static bfd_boolean
elfNN_ia64_merge_private_bfd_data (ibfd, obfd)
     bfd *ibfd, *obfd;
{
  flagword out_flags;
  flagword in_flags;
  bfd_boolean ok = TRUE;

  /* Don't even pretend to support mixed-format linking.  */
  if (bfd_get_flavour (ibfd) != bfd_target_elf_flavour
      || bfd_get_flavour (obfd) != bfd_target_elf_flavour)
    return FALSE;

  in_flags  = elf_elfheader (ibfd)->e_flags;
  out_flags = elf_elfheader (obfd)->e_flags;

  if (! elf_flags_init (obfd))
    {
      elf_flags_init (obfd) = TRUE;
      elf_elfheader (obfd)->e_flags = in_flags;

      if (bfd_get_arch (obfd) == bfd_get_arch (ibfd)
	  && bfd_get_arch_info (obfd)->the_default)
	{
	  return bfd_set_arch_mach (obfd, bfd_get_arch (ibfd),
				    bfd_get_mach (ibfd));
	}

      return TRUE;
    }

  /* Check flag compatibility.  */
  if (in_flags == out_flags)
    return TRUE;

  /* Output has EF_IA_64_REDUCEDFP set only if all inputs have it set.  */
  if (!(in_flags & EF_IA_64_REDUCEDFP) && (out_flags & EF_IA_64_REDUCEDFP))
    elf_elfheader (obfd)->e_flags &= ~EF_IA_64_REDUCEDFP;

  if ((in_flags & EF_IA_64_TRAPNIL) != (out_flags & EF_IA_64_TRAPNIL))
    {
      (*_bfd_error_handler)
	(_("%B: linking trap-on-NULL-dereference with non-trapping files"),
	 ibfd);

      bfd_set_error (bfd_error_bad_value);
      ok = FALSE;
    }
  if ((in_flags & EF_IA_64_BE) != (out_flags & EF_IA_64_BE))
    {
      (*_bfd_error_handler)
	(_("%B: linking big-endian files with little-endian files"),
	 ibfd);

      bfd_set_error (bfd_error_bad_value);
      ok = FALSE;
    }
  if ((in_flags & EF_IA_64_ABI64) != (out_flags & EF_IA_64_ABI64))
    {
      (*_bfd_error_handler)
	(_("%B: linking 64-bit files with 32-bit files"),
	 ibfd);

      bfd_set_error (bfd_error_bad_value);
      ok = FALSE;
    }
  if ((in_flags & EF_IA_64_CONS_GP) != (out_flags & EF_IA_64_CONS_GP))
    {
      (*_bfd_error_handler)
	(_("%B: linking constant-gp files with non-constant-gp files"),
	 ibfd);

      bfd_set_error (bfd_error_bad_value);
      ok = FALSE;
    }
  if ((in_flags & EF_IA_64_NOFUNCDESC_CONS_GP)
      != (out_flags & EF_IA_64_NOFUNCDESC_CONS_GP))
    {
      (*_bfd_error_handler)
	(_("%B: linking auto-pic files with non-auto-pic files"),
	 ibfd);

      bfd_set_error (bfd_error_bad_value);
      ok = FALSE;
    }

  return ok;
}

static bfd_boolean
elfNN_ia64_print_private_bfd_data (abfd, ptr)
     bfd *abfd;
     PTR ptr;
{
  FILE *file = (FILE *) ptr;
  flagword flags = elf_elfheader (abfd)->e_flags;

  BFD_ASSERT (abfd != NULL && ptr != NULL);

  fprintf (file, "private flags = %s%s%s%s%s%s%s%s\n",
	   (flags & EF_IA_64_TRAPNIL) ? "TRAPNIL, " : "",
	   (flags & EF_IA_64_EXT) ? "EXT, " : "",
	   (flags & EF_IA_64_BE) ? "BE, " : "LE, ",
	   (flags & EF_IA_64_REDUCEDFP) ? "REDUCEDFP, " : "",
	   (flags & EF_IA_64_CONS_GP) ? "CONS_GP, " : "",
	   (flags & EF_IA_64_NOFUNCDESC_CONS_GP) ? "NOFUNCDESC_CONS_GP, " : "",
	   (flags & EF_IA_64_ABSOLUTE) ? "ABSOLUTE, " : "",
	   (flags & EF_IA_64_ABI64) ? "ABI64" : "ABI32");

  _bfd_elf_print_private_bfd_data (abfd, ptr);
  return TRUE;
}

static enum elf_reloc_type_class
elfNN_ia64_reloc_type_class (rela)
     const Elf_Internal_Rela *rela;
{
  switch ((int) ELFNN_R_TYPE (rela->r_info))
    {
    case R_IA64_REL32MSB:
    case R_IA64_REL32LSB:
    case R_IA64_REL64MSB:
    case R_IA64_REL64LSB:
      return reloc_class_relative;
    case R_IA64_IPLTMSB:
    case R_IA64_IPLTLSB:
      return reloc_class_plt;
    case R_IA64_COPY:
      return reloc_class_copy;
    default:
      return reloc_class_normal;
    }
}

static const struct bfd_elf_special_section elfNN_ia64_special_sections[] =
{
  { STRING_COMMA_LEN (".sbss"),  -1, SHT_NOBITS,   SHF_ALLOC + SHF_WRITE + SHF_IA_64_SHORT },
  { STRING_COMMA_LEN (".sdata"), -1, SHT_PROGBITS, SHF_ALLOC + SHF_WRITE + SHF_IA_64_SHORT },
  { NULL,                    0,   0, 0,            0 }
};

static bfd_boolean
elfNN_ia64_object_p (bfd *abfd)
{
  asection *sec;
  asection *group, *unwi, *unw;
  flagword flags;
  const char *name;
  char *unwi_name, *unw_name;
  bfd_size_type amt;

  if (abfd->flags & DYNAMIC)
    return TRUE;

  /* Flags for fake group section.  */
  flags = (SEC_LINKER_CREATED | SEC_GROUP | SEC_LINK_ONCE
	   | SEC_EXCLUDE);

  /* We add a fake section group for each .gnu.linkonce.t.* section,
     which isn't in a section group, and its unwind sections.  */
  for (sec = abfd->sections; sec != NULL; sec = sec->next)
    {
      if (elf_sec_group (sec) == NULL
	  && ((sec->flags & (SEC_LINK_ONCE | SEC_CODE | SEC_GROUP))
	      == (SEC_LINK_ONCE | SEC_CODE))
	  && CONST_STRNEQ (sec->name, ".gnu.linkonce.t."))
	{
	  name = sec->name + 16;

	  amt = strlen (name) + sizeof (".gnu.linkonce.ia64unwi.");
	  unwi_name = bfd_alloc (abfd, amt);
	  if (!unwi_name)
	    return FALSE;

	  strcpy (stpcpy (unwi_name, ".gnu.linkonce.ia64unwi."), name);
	  unwi = bfd_get_section_by_name (abfd, unwi_name);

	  amt = strlen (name) + sizeof (".gnu.linkonce.ia64unw.");
	  unw_name = bfd_alloc (abfd, amt);
	  if (!unw_name)
	    return FALSE;

	  strcpy (stpcpy (unw_name, ".gnu.linkonce.ia64unw."), name);
	  unw = bfd_get_section_by_name (abfd, unw_name);

	  /* We need to create a fake group section for it and its
	     unwind sections.  */
	  group = bfd_make_section_anyway_with_flags (abfd, name,
						      flags);
	  if (group == NULL)
	    return FALSE;

	  /* Move the fake group section to the beginning.  */
	  bfd_section_list_remove (abfd, group);
	  bfd_section_list_prepend (abfd, group);

	  elf_next_in_group (group) = sec;

	  elf_group_name (sec) = name;
	  elf_next_in_group (sec) = sec;
	  elf_sec_group (sec) = group;

	  if (unwi)
	    {
	      elf_group_name (unwi) = name;
	      elf_next_in_group (unwi) = sec;
	      elf_next_in_group (sec) = unwi;
	      elf_sec_group (unwi) = group;
	    }

	   if (unw)
	     {
	       elf_group_name (unw) = name;
	       if (unwi)
		 {
		   elf_next_in_group (unw) = elf_next_in_group (unwi);
		   elf_next_in_group (unwi) = unw;
		 }
	       else
		 {
		   elf_next_in_group (unw) = sec;
		   elf_next_in_group (sec) = unw;
		 }
	       elf_sec_group (unw) = group;
	     }

	   /* Fake SHT_GROUP section header.  */
	  elf_section_data (group)->this_hdr.bfd_section = group;
	  elf_section_data (group)->this_hdr.sh_type = SHT_GROUP;
	}
    }
  return TRUE;
}

static bfd_boolean
elfNN_ia64_hpux_vec (const bfd_target *vec)
{
  extern const bfd_target bfd_elfNN_ia64_hpux_big_vec;
  return (vec == & bfd_elfNN_ia64_hpux_big_vec);
}

static void
elfNN_hpux_post_process_headers (abfd, info)
	bfd *abfd;
	struct bfd_link_info *info ATTRIBUTE_UNUSED;
{
  Elf_Internal_Ehdr *i_ehdrp = elf_elfheader (abfd);

  i_ehdrp->e_ident[EI_OSABI] = get_elf_backend_data (abfd)->elf_osabi;
  i_ehdrp->e_ident[EI_ABIVERSION] = 1;
}

bfd_boolean
elfNN_hpux_backend_section_from_bfd_section (abfd, sec, retval)
	bfd *abfd ATTRIBUTE_UNUSED;
	asection *sec;
	int *retval;
{
  if (bfd_is_com_section (sec))
    {
      *retval = SHN_IA_64_ANSI_COMMON;
      return TRUE;
    }
  return FALSE;
}

static void
elfNN_hpux_backend_symbol_processing (bfd *abfd ATTRIBUTE_UNUSED,
				      asymbol *asym)
{
  elf_symbol_type *elfsym = (elf_symbol_type *) asym;

  switch (elfsym->internal_elf_sym.st_shndx)
    {
    case SHN_IA_64_ANSI_COMMON:
      asym->section = bfd_com_section_ptr;
      asym->value = elfsym->internal_elf_sym.st_size;
      asym->flags &= ~BSF_GLOBAL;
      break;
    }
}


#define TARGET_LITTLE_SYM		bfd_elfNN_ia64_little_vec
#define TARGET_LITTLE_NAME		"elfNN-ia64-little"
#define TARGET_BIG_SYM			bfd_elfNN_ia64_big_vec
#define TARGET_BIG_NAME			"elfNN-ia64-big"
#define ELF_ARCH			bfd_arch_ia64
#define ELF_MACHINE_CODE		EM_IA_64
#define ELF_MACHINE_ALT1		1999	/* EAS2.3 */
#define ELF_MACHINE_ALT2		1998	/* EAS2.2 */
#define ELF_MAXPAGESIZE			0x10000	/* 64KB */
#define ELF_COMMONPAGESIZE		0x4000	/* 16KB */

#define elf_backend_section_from_shdr \
	elfNN_ia64_section_from_shdr
#define elf_backend_section_flags \
	elfNN_ia64_section_flags
#define elf_backend_fake_sections \
	elfNN_ia64_fake_sections
#define elf_backend_final_write_processing \
	elfNN_ia64_final_write_processing
#define elf_backend_add_symbol_hook \
	elfNN_ia64_add_symbol_hook
#define elf_backend_additional_program_headers \
	elfNN_ia64_additional_program_headers
#define elf_backend_modify_segment_map \
	elfNN_ia64_modify_segment_map
#define elf_backend_modify_program_headers \
	elfNN_ia64_modify_program_headers
#define elf_info_to_howto \
	elfNN_ia64_info_to_howto

#define bfd_elfNN_bfd_reloc_type_lookup \
	elfNN_ia64_reloc_type_lookup
#define bfd_elfNN_bfd_reloc_name_lookup \
	elfNN_ia64_reloc_name_lookup
#define bfd_elfNN_bfd_is_local_label_name \
	elfNN_ia64_is_local_label_name
#define bfd_elfNN_bfd_relax_section \
	elfNN_ia64_relax_section

#define elf_backend_object_p \
	elfNN_ia64_object_p

/* Stuff for the BFD linker: */
#define bfd_elfNN_bfd_link_hash_table_create \
	elfNN_ia64_hash_table_create
#define bfd_elfNN_bfd_link_hash_table_free \
	elfNN_ia64_hash_table_free
#define elf_backend_create_dynamic_sections \
	elfNN_ia64_create_dynamic_sections
#define elf_backend_check_relocs \
	elfNN_ia64_check_relocs
#define elf_backend_adjust_dynamic_symbol \
	elfNN_ia64_adjust_dynamic_symbol
#define elf_backend_size_dynamic_sections \
	elfNN_ia64_size_dynamic_sections
#define elf_backend_omit_section_dynsym \
  ((bfd_boolean (*) (bfd *, struct bfd_link_info *, asection *)) bfd_true)
#define elf_backend_relocate_section \
	elfNN_ia64_relocate_section
#define elf_backend_finish_dynamic_symbol \
	elfNN_ia64_finish_dynamic_symbol
#define elf_backend_finish_dynamic_sections \
	elfNN_ia64_finish_dynamic_sections
#define bfd_elfNN_bfd_final_link \
	elfNN_ia64_final_link

#define bfd_elfNN_bfd_merge_private_bfd_data \
	elfNN_ia64_merge_private_bfd_data
#define bfd_elfNN_bfd_set_private_flags \
	elfNN_ia64_set_private_flags
#define bfd_elfNN_bfd_print_private_bfd_data \
	elfNN_ia64_print_private_bfd_data

#define elf_backend_plt_readonly	1
#define elf_backend_want_plt_sym	0
#define elf_backend_plt_alignment	5
#define elf_backend_got_header_size	0
#define elf_backend_want_got_plt	1
#define elf_backend_may_use_rel_p	1
#define elf_backend_may_use_rela_p	1
#define elf_backend_default_use_rela_p	1
#define elf_backend_want_dynbss		0
#define elf_backend_copy_indirect_symbol elfNN_ia64_hash_copy_indirect
#define elf_backend_hide_symbol		elfNN_ia64_hash_hide_symbol
#define elf_backend_fixup_symbol	_bfd_elf_link_hash_fixup_symbol
#define elf_backend_reloc_type_class	elfNN_ia64_reloc_type_class
#define elf_backend_rela_normal		1
#define elf_backend_special_sections	elfNN_ia64_special_sections
#define elf_backend_default_execstack	0

/* FIXME: PR 290: The Intel C compiler generates SHT_IA_64_UNWIND with
   SHF_LINK_ORDER. But it doesn't set the sh_link or sh_info fields.
   We don't want to flood users with so many error messages. We turn
   off the warning for now. It will be turned on later when the Intel
   compiler is fixed.   */
#define elf_backend_link_order_error_handler NULL

#include "elfNN-target.h"

/* FreeBSD support.  */

#undef  TARGET_LITTLE_SYM
#define TARGET_LITTLE_SYM		bfd_elfNN_ia64_freebsd_vec
#undef  TARGET_LITTLE_NAME
#define TARGET_LITTLE_NAME		"elfNN-ia64-freebsd"
#undef  TARGET_BIG_SYM
#undef  TARGET_BIG_NAME

#undef  ELF_OSABI
#define ELF_OSABI			ELFOSABI_FREEBSD

#undef  elf_backend_post_process_headers
#define elf_backend_post_process_headers _bfd_elf_set_osabi

#undef  elfNN_bed
#define elfNN_bed elfNN_ia64_fbsd_bed

#include "elfNN-target.h"

/* HPUX-specific vectors.  */

#undef  TARGET_LITTLE_SYM
#undef  TARGET_LITTLE_NAME
#undef  TARGET_BIG_SYM
#define TARGET_BIG_SYM                  bfd_elfNN_ia64_hpux_big_vec
#undef  TARGET_BIG_NAME
#define TARGET_BIG_NAME                 "elfNN-ia64-hpux-big"

/* These are HP-UX specific functions.  */

#undef  elf_backend_post_process_headers
#define elf_backend_post_process_headers elfNN_hpux_post_process_headers

#undef  elf_backend_section_from_bfd_section
#define elf_backend_section_from_bfd_section elfNN_hpux_backend_section_from_bfd_section

#undef elf_backend_symbol_processing
#define elf_backend_symbol_processing elfNN_hpux_backend_symbol_processing

#undef  elf_backend_want_p_paddr_set_to_zero
#define elf_backend_want_p_paddr_set_to_zero 1

#undef  ELF_MAXPAGESIZE
#define ELF_MAXPAGESIZE                 0x1000  /* 4K */
#undef ELF_COMMONPAGESIZE
#undef ELF_OSABI
#define ELF_OSABI			ELFOSABI_HPUX

#undef  elfNN_bed
#define elfNN_bed elfNN_ia64_hpux_bed

#include "elfNN-target.h"

#undef  elf_backend_want_p_paddr_set_to_zero
