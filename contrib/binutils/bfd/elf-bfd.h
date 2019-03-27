/* BFD back-end data structures for ELF files.
   Copyright 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001,
   2002, 2003, 2004, 2005, 2006, 2007 Free Software Foundation, Inc.
   Written by Cygnus Support.

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

#ifndef _LIBELF_H_
#define _LIBELF_H_ 1

#include "elf/common.h"
#include "elf/internal.h"
#include "elf/external.h"
#include "bfdlink.h"

/* The number of entries in a section is its size divided by the size
   of a single entry.  This is normally only applicable to reloc and
   symbol table sections.  */
#define NUM_SHDR_ENTRIES(shdr) ((shdr)->sh_size / (shdr)->sh_entsize)

/* If size isn't specified as 64 or 32, NAME macro should fail.  */
#ifndef NAME
#if ARCH_SIZE == 64
#define NAME(x, y) x ## 64 ## _ ## y
#endif
#if ARCH_SIZE == 32
#define NAME(x, y) x ## 32 ## _ ## y
#endif
#endif

#ifndef NAME
#define NAME(x, y) x ## NOSIZE ## _ ## y
#endif

#define ElfNAME(X)	NAME(Elf,X)
#define elfNAME(X)	NAME(elf,X)

/* Information held for an ELF symbol.  The first field is the
   corresponding asymbol.  Every symbol is an ELF file is actually a
   pointer to this structure, although it is often handled as a
   pointer to an asymbol.  */

typedef struct
{
  /* The BFD symbol.  */
  asymbol symbol;
  /* ELF symbol information.  */
  Elf_Internal_Sym internal_elf_sym;
  /* Backend specific information.  */
  union
    {
      unsigned int hppa_arg_reloc;
      void *mips_extr;
      void *any;
    }
  tc_data;

  /* Version information.  This is from an Elf_Internal_Versym
     structure in a SHT_GNU_versym section.  It is zero if there is no
     version information.  */
  unsigned short version;

} elf_symbol_type;

struct elf_strtab_hash;
struct got_entry;
struct plt_entry;

/* ELF linker hash table entries.  */

struct elf_link_hash_entry
{
  struct bfd_link_hash_entry root;

  /* Symbol index in output file.  This is initialized to -1.  It is
     set to -2 if the symbol is used by a reloc.  */
  long indx;

  /* Symbol index as a dynamic symbol.  Initialized to -1, and remains
     -1 if this is not a dynamic symbol.  */
  /* ??? Note that this is consistently used as a synonym for tests
     against whether we can perform various simplifying transformations
     to the code.  (E.g. changing a pc-relative jump to a PLT entry
     into a pc-relative jump to the target function.)  That test, which
     is often relatively complex, and someplaces wrong or incomplete,
     should really be replaced by a predicate in elflink.c.

     End result: this field -1 does not indicate that the symbol is
     not in the dynamic symbol table, but rather that the symbol is
     not visible outside this DSO.  */
  long dynindx;

  /* If this symbol requires an entry in the global offset table, the
     processor specific backend uses this field to track usage and
     final offset.  Two schemes are supported:  The first assumes that
     a symbol may only have one GOT entry, and uses REFCOUNT until
     size_dynamic_sections, at which point the contents of the .got is
     fixed.  Afterward, if OFFSET is -1, then the symbol does not
     require a global offset table entry.  The second scheme allows
     multiple GOT entries per symbol, managed via a linked list
     pointed to by GLIST.  */
  union gotplt_union
    {
      bfd_signed_vma refcount;
      bfd_vma offset;
      struct got_entry *glist;
      struct plt_entry *plist;
    } got;

  /* Same, but tracks a procedure linkage table entry.  */
  union gotplt_union plt;

  /* Symbol size.  */
  bfd_size_type size;

  /* Symbol type (STT_NOTYPE, STT_OBJECT, etc.).  */
  unsigned int type : 8;

  /* Symbol st_other value, symbol visibility.  */
  unsigned int other : 8;

  /* Symbol is referenced by a non-shared object.  */
  unsigned int ref_regular : 1;
  /* Symbol is defined by a non-shared object.  */
  unsigned int def_regular : 1;
  /* Symbol is referenced by a shared object.  */
  unsigned int ref_dynamic : 1;
  /* Symbol is defined by a shared object.  */
  unsigned int def_dynamic : 1;
  /* Symbol has a non-weak reference from a non-shared object.  */
  unsigned int ref_regular_nonweak : 1;
  /* Dynamic symbol has been adjustd.  */
  unsigned int dynamic_adjusted : 1;
  /* Symbol needs a copy reloc.  */
  unsigned int needs_copy : 1;
  /* Symbol needs a procedure linkage table entry.  */
  unsigned int needs_plt : 1;
  /* Symbol appears in a non-ELF input file.  */
  unsigned int non_elf : 1;
  /* Symbol should be marked as hidden in the version information.  */
  unsigned int hidden : 1;
  /* Symbol was forced to local scope due to a version script file.  */
  unsigned int forced_local : 1;
  /* Symbol was forced to be dynamic due to a version script file.  */
  unsigned int dynamic : 1;
  /* Symbol was marked during garbage collection.  */
  unsigned int mark : 1;
  /* Symbol is referenced by a non-GOT/non-PLT relocation.  This is
     not currently set by all the backends.  */
  unsigned int non_got_ref : 1;
  /* Symbol has a definition in a shared object.
     FIXME: There is no real need for this field if def_dynamic is never
     cleared and all places that test def_dynamic also test def_regular.  */
  unsigned int dynamic_def : 1;
  /* Symbol is weak in all shared objects.  */
  unsigned int dynamic_weak : 1;
  /* Symbol is referenced with a relocation where C/C++ pointer equality
     matters.  */
  unsigned int pointer_equality_needed : 1;

  /* String table index in .dynstr if this is a dynamic symbol.  */
  unsigned long dynstr_index;

  union
  {
    /* If this is a weak defined symbol from a dynamic object, this
       field points to a defined symbol with the same value, if there is
       one.  Otherwise it is NULL.  */
    struct elf_link_hash_entry *weakdef;

    /* Hash value of the name computed using the ELF hash function.
       Used part way through size_dynamic_sections, after we've finished
       with weakdefs.  */
    unsigned long elf_hash_value;
  } u;

  /* Version information.  */
  union
  {
    /* This field is used for a symbol which is not defined in a
       regular object.  It points to the version information read in
       from the dynamic object.  */
    Elf_Internal_Verdef *verdef;
    /* This field is used for a symbol which is defined in a regular
       object.  It is set up in size_dynamic_sections.  It points to
       the version information we should write out for this symbol.  */
    struct bfd_elf_version_tree *vertree;
  } verinfo;

  struct
  {
    /* Virtual table entry use information.  This array is nominally of size
       size/sizeof(target_void_pointer), though we have to be able to assume
       and track a size while the symbol is still undefined.  It is indexed
       via offset/sizeof(target_void_pointer).  */
    size_t size;
    bfd_boolean *used;

    /* Virtual table derivation info.  */
    struct elf_link_hash_entry *parent;
  } *vtable;
};

/* Will references to this symbol always reference the symbol
   in this object?  STV_PROTECTED is excluded from the visibility test
   here so that function pointer comparisons work properly.  Since
   function symbols not defined in an app are set to their .plt entry,
   it's necessary for shared libs to also reference the .plt even
   though the symbol is really local to the shared lib.  */
#define SYMBOL_REFERENCES_LOCAL(INFO, H) \
  _bfd_elf_symbol_refs_local_p (H, INFO, 0)

/* Will _calls_ to this symbol always call the version in this object?  */
#define SYMBOL_CALLS_LOCAL(INFO, H) \
  _bfd_elf_symbol_refs_local_p (H, INFO, 1)

/* Common symbols that are turned into definitions don't have the
   DEF_REGULAR flag set, so they might appear to be undefined.  */
#define ELF_COMMON_DEF_P(H) \
  (!(H)->def_regular							\
   && !(H)->def_dynamic							\
   && (H)->root.type == bfd_link_hash_defined)

/* Records local symbols to be emitted in the dynamic symbol table.  */

struct elf_link_local_dynamic_entry
{
  struct elf_link_local_dynamic_entry *next;

  /* The input bfd this symbol came from.  */
  bfd *input_bfd;

  /* The index of the local symbol being copied.  */
  long input_indx;

  /* The index in the outgoing dynamic symbol table.  */
  long dynindx;

  /* A copy of the input symbol.  */
  Elf_Internal_Sym isym;
};

struct elf_link_loaded_list
{
  struct elf_link_loaded_list *next;
  bfd *abfd;
};

/* Structures used by the eh_frame optimization code.  */
struct eh_cie_fde
{
  /* For FDEs, this points to the CIE used.  */
  struct eh_cie_fde *cie_inf;
  unsigned int size;
  unsigned int offset;
  unsigned int new_offset;
  unsigned char fde_encoding;
  unsigned char lsda_encoding;
  unsigned char lsda_offset;
  unsigned int cie : 1;
  unsigned int removed : 1;
  unsigned int add_augmentation_size : 1;
  unsigned int add_fde_encoding : 1;
  unsigned int make_relative : 1;
  unsigned int make_lsda_relative : 1;
  unsigned int need_lsda_relative : 1;
  unsigned int per_encoding_relative : 1;
  unsigned int *set_loc;
};

struct eh_frame_sec_info
{
  unsigned int count;
  struct eh_cie_fde entry[1];
};

struct eh_frame_array_ent
{
  bfd_vma initial_loc;
  bfd_vma fde;
};

struct htab;

struct eh_frame_hdr_info
{
  struct htab *cies;
  asection *hdr_sec;
  unsigned int fde_count, array_count;
  struct eh_frame_array_ent *array;
  /* TRUE if .eh_frame_hdr should contain the sorted search table.
     We build it if we successfully read all .eh_frame input sections
     and recognize them.  */
  bfd_boolean table;
  bfd_boolean offsets_adjusted;
};

/* ELF linker hash table.  */

struct elf_link_hash_table
{
  struct bfd_link_hash_table root;

  /* Whether we have created the special dynamic sections required
     when linking against or generating a shared object.  */
  bfd_boolean dynamic_sections_created;

  /* True if this target has relocatable executables, so needs dynamic
     section symbols.  */
  bfd_boolean is_relocatable_executable;

  /* The BFD used to hold special sections created by the linker.
     This will be the first BFD found which requires these sections to
     be created.  */
  bfd *dynobj;

  /* The value to use when initialising got.refcount/offset and
     plt.refcount/offset in an elf_link_hash_entry.  Set to zero when
     the values are refcounts.  Set to init_got_offset/init_plt_offset
     in size_dynamic_sections when the values may be offsets.  */
  union gotplt_union init_got_refcount;
  union gotplt_union init_plt_refcount;

  /* The value to use for got.refcount/offset and plt.refcount/offset
     when the values may be offsets.  Normally (bfd_vma) -1.  */
  union gotplt_union init_got_offset;
  union gotplt_union init_plt_offset;

  /* The number of symbols found in the link which must be put into
     the .dynsym section.  */
  bfd_size_type dynsymcount;

  /* The string table of dynamic symbols, which becomes the .dynstr
     section.  */
  struct elf_strtab_hash *dynstr;

  /* The number of buckets in the hash table in the .hash section.
     This is based on the number of dynamic symbols.  */
  bfd_size_type bucketcount;

  /* A linked list of DT_NEEDED names found in dynamic objects
     included in the link.  */
  struct bfd_link_needed_list *needed;

  /* Sections in the output bfd that provides a section symbol
     to be used by relocations emitted against local symbols.
     Most targets will not use data_index_section.  */
  asection *text_index_section;
  asection *data_index_section;

  /* The _GLOBAL_OFFSET_TABLE_ symbol.  */
  struct elf_link_hash_entry *hgot;

  /* The _PROCEDURE_LINKAGE_TABLE_ symbol.  */
  struct elf_link_hash_entry *hplt;

  /* A pointer to information used to merge SEC_MERGE sections.  */
  void *merge_info;

  /* Used to link stabs in sections.  */
  struct stab_info stab_info;

  /* Used by eh_frame code when editing .eh_frame.  */
  struct eh_frame_hdr_info eh_info;

  /* A linked list of local symbols to be added to .dynsym.  */
  struct elf_link_local_dynamic_entry *dynlocal;

  /* A linked list of DT_RPATH/DT_RUNPATH names found in dynamic
     objects included in the link.  */
  struct bfd_link_needed_list *runpath;

  /* Cached first output tls section and size of PT_TLS segment.  */
  asection *tls_sec;
  bfd_size_type tls_size;

  /* A linked list of BFD's loaded in the link.  */
  struct elf_link_loaded_list *loaded;
};

/* Look up an entry in an ELF linker hash table.  */

#define elf_link_hash_lookup(table, string, create, copy, follow)	\
  ((struct elf_link_hash_entry *)					\
   bfd_link_hash_lookup (&(table)->root, (string), (create),		\
			 (copy), (follow)))

/* Traverse an ELF linker hash table.  */

#define elf_link_hash_traverse(table, func, info)			\
  (bfd_link_hash_traverse						\
   (&(table)->root,							\
    (bfd_boolean (*) (struct bfd_link_hash_entry *, void *)) (func),	\
    (info)))

/* Get the ELF linker hash table from a link_info structure.  */

#define elf_hash_table(p) ((struct elf_link_hash_table *) ((p)->hash))

/* Returns TRUE if the hash table is a struct elf_link_hash_table.  */
#define is_elf_hash_table(htab)					      	\
  (((struct bfd_link_hash_table *) (htab))->type == bfd_link_elf_hash_table)

/* Used by bfd_section_from_r_symndx to cache a small number of local
   symbol to section mappings.  */
#define LOCAL_SYM_CACHE_SIZE 32
struct sym_sec_cache
{
  bfd *abfd;
  unsigned long indx[LOCAL_SYM_CACHE_SIZE];
  asection *sec[LOCAL_SYM_CACHE_SIZE];
};

/* Constant information held for an ELF backend.  */

struct elf_size_info {
  unsigned char sizeof_ehdr, sizeof_phdr, sizeof_shdr;
  unsigned char sizeof_rel, sizeof_rela, sizeof_sym, sizeof_dyn, sizeof_note;

  /* The size of entries in the .hash section.  */
  unsigned char sizeof_hash_entry;

  /* The number of internal relocations to allocate per external
     relocation entry.  */
  unsigned char int_rels_per_ext_rel;
  /* We use some fixed size arrays.  This should be large enough to
     handle all back-ends.  */
#define MAX_INT_RELS_PER_EXT_REL 3

  unsigned char arch_size, log_file_align;
  unsigned char elfclass, ev_current;
  int (*write_out_phdrs)
    (bfd *, const Elf_Internal_Phdr *, unsigned int);
  bfd_boolean
    (*write_shdrs_and_ehdr) (bfd *);
  void (*write_relocs)
    (bfd *, asection *, void *);
  bfd_boolean (*swap_symbol_in)
    (bfd *, const void *, const void *, Elf_Internal_Sym *);
  void (*swap_symbol_out)
    (bfd *, const Elf_Internal_Sym *, void *, void *);
  bfd_boolean (*slurp_reloc_table)
    (bfd *, asection *, asymbol **, bfd_boolean);
  long (*slurp_symbol_table)
    (bfd *, asymbol **, bfd_boolean);
  void (*swap_dyn_in)
    (bfd *, const void *, Elf_Internal_Dyn *);
  void (*swap_dyn_out)
    (bfd *, const Elf_Internal_Dyn *, void *);

  /* This function is called to swap in a REL relocation.  If an
     external relocation corresponds to more than one internal
     relocation, then all relocations are swapped in at once.  */
  void (*swap_reloc_in)
    (bfd *, const bfd_byte *, Elf_Internal_Rela *);

  /* This function is called to swap out a REL relocation.  */
  void (*swap_reloc_out)
    (bfd *, const Elf_Internal_Rela *, bfd_byte *);

  /* This function is called to swap in a RELA relocation.  If an
     external relocation corresponds to more than one internal
     relocation, then all relocations are swapped in at once.  */
  void (*swap_reloca_in)
    (bfd *, const bfd_byte *, Elf_Internal_Rela *);

  /* This function is called to swap out a RELA relocation.  */
  void (*swap_reloca_out)
    (bfd *, const Elf_Internal_Rela *, bfd_byte *);
};

#define elf_symbol_from(ABFD,S) \
	(((S)->the_bfd->xvec->flavour == bfd_target_elf_flavour \
	  && (S)->the_bfd->tdata.elf_obj_data != 0) \
	 ? (elf_symbol_type *) (S) \
	 : 0)

enum elf_reloc_type_class {
  reloc_class_normal,
  reloc_class_relative,
  reloc_class_plt,
  reloc_class_copy
};

struct elf_reloc_cookie
{
  Elf_Internal_Rela *rels, *rel, *relend;
  Elf_Internal_Sym *locsyms;
  bfd *abfd;
  size_t locsymcount;
  size_t extsymoff;
  struct elf_link_hash_entry **sym_hashes;
  int r_sym_shift;
  bfd_boolean bad_symtab;
};

/* The level of IRIX compatibility we're striving for.  */

typedef enum {
  ict_none,
  ict_irix5,
  ict_irix6
} irix_compat_t;

/* Mapping of ELF section names and types.  */
struct bfd_elf_special_section
{
  const char *prefix;
  int prefix_length;
  /* 0 means name must match PREFIX exactly.
     -1 means name must start with PREFIX followed by an arbitrary string.
     -2 means name must match PREFIX exactly or consist of PREFIX followed
     by a dot then anything.
     > 0 means name must start with the first PREFIX_LENGTH chars of
     PREFIX and finish with the last SUFFIX_LENGTH chars of PREFIX.  */
  int suffix_length;
  int type;
  int attr;
};

enum action_discarded
  {
    COMPLAIN = 1,
    PRETEND = 2
  };

typedef asection * (*elf_gc_mark_hook_fn)
  (asection *, struct bfd_link_info *, Elf_Internal_Rela *,
   struct elf_link_hash_entry *, Elf_Internal_Sym *);

struct elf_backend_data
{
  /* The architecture for this backend.  */
  enum bfd_architecture arch;

  /* The ELF machine code (EM_xxxx) for this backend.  */
  int elf_machine_code;

  /* EI_OSABI. */
  int elf_osabi;

  /* The maximum page size for this backend.  */
  bfd_vma maxpagesize;

  /* The minimum page size for this backend.  An input object will not be
     considered page aligned unless its sections are correctly aligned for
     pages at least this large.  May be smaller than maxpagesize.  */
  bfd_vma minpagesize;

  /* The common page size for this backend.  */
  bfd_vma commonpagesize;

  /* The BFD flags applied to sections created for dynamic linking.  */
  flagword dynamic_sec_flags;

  /* A function to translate an ELF RELA relocation to a BFD arelent
     structure.  */
  void (*elf_info_to_howto)
    (bfd *, arelent *, Elf_Internal_Rela *);

  /* A function to translate an ELF REL relocation to a BFD arelent
     structure.  */
  void (*elf_info_to_howto_rel)
    (bfd *, arelent *, Elf_Internal_Rela *);

  /* A function to determine whether a symbol is global when
     partitioning the symbol table into local and global symbols.
     This should be NULL for most targets, in which case the correct
     thing will be done.  MIPS ELF, at least on the Irix 5, has
     special requirements.  */
  bfd_boolean (*elf_backend_sym_is_global)
    (bfd *, asymbol *);

  /* The remaining functions are hooks which are called only if they
     are not NULL.  */

  /* A function to permit a backend specific check on whether a
     particular BFD format is relevant for an object file, and to
     permit the backend to set any global information it wishes.  When
     this is called elf_elfheader is set, but anything else should be
     used with caution.  If this returns FALSE, the check_format
     routine will return a bfd_error_wrong_format error.  */
  bfd_boolean (*elf_backend_object_p)
    (bfd *);

  /* A function to do additional symbol processing when reading the
     ELF symbol table.  This is where any processor-specific special
     section indices are handled.  */
  void (*elf_backend_symbol_processing)
    (bfd *, asymbol *);

  /* A function to do additional symbol processing after reading the
     entire ELF symbol table.  */
  bfd_boolean (*elf_backend_symbol_table_processing)
    (bfd *, elf_symbol_type *, unsigned int);

  /* A function to set the type of the info field.  Processor-specific
     types should be handled here.  */
  int (*elf_backend_get_symbol_type)
    (Elf_Internal_Sym *, int);

  /* A function to return the linker hash table entry of a symbol that
     might be satisfied by an archive symbol.  */
  struct elf_link_hash_entry * (*elf_backend_archive_symbol_lookup)
    (bfd *, struct bfd_link_info *, const char *);

  /* Return true if local section symbols should have a non-null st_name.
     NULL implies false.  */
  bfd_boolean (*elf_backend_name_local_section_symbols)
    (bfd *);

  /* A function to do additional processing on the ELF section header
     just before writing it out.  This is used to set the flags and
     type fields for some sections, or to actually write out data for
     unusual sections.  */
  bfd_boolean (*elf_backend_section_processing)
    (bfd *, Elf_Internal_Shdr *);

  /* A function to handle unusual section types when creating BFD
     sections from ELF sections.  */
  bfd_boolean (*elf_backend_section_from_shdr)
    (bfd *, Elf_Internal_Shdr *, const char *, int);

  /* A function to convert machine dependent ELF section header flags to
     BFD internal section header flags.  */
  bfd_boolean (*elf_backend_section_flags)
    (flagword *, const Elf_Internal_Shdr *);

  /* A function that returns a struct containing ELF section flags and
     type for the given BFD section.   */
  const struct bfd_elf_special_section * (*get_sec_type_attr)
    (bfd *, asection *);

  /* A function to handle unusual program segment types when creating BFD
     sections from ELF program segments.  */
  bfd_boolean (*elf_backend_section_from_phdr)
    (bfd *, Elf_Internal_Phdr *, int, const char *);

  /* A function to set up the ELF section header for a BFD section in
     preparation for writing it out.  This is where the flags and type
     fields are set for unusual sections.  */
  bfd_boolean (*elf_backend_fake_sections)
    (bfd *, Elf_Internal_Shdr *, asection *);

  /* A function to get the ELF section index for a BFD section.  If
     this returns TRUE, the section was found.  If it is a normal ELF
     section, *RETVAL should be left unchanged.  If it is not a normal
     ELF section *RETVAL should be set to the SHN_xxxx index.  */
  bfd_boolean (*elf_backend_section_from_bfd_section)
    (bfd *, asection *, int *retval);

  /* If this field is not NULL, it is called by the add_symbols phase
     of a link just before adding a symbol to the global linker hash
     table.  It may modify any of the fields as it wishes.  If *NAME
     is set to NULL, the symbol will be skipped rather than being
     added to the hash table.  This function is responsible for
     handling all processor dependent symbol bindings and section
     indices, and must set at least *FLAGS and *SEC for each processor
     dependent case; failure to do so will cause a link error.  */
  bfd_boolean (*elf_add_symbol_hook)
    (bfd *abfd, struct bfd_link_info *info, Elf_Internal_Sym *,
     const char **name, flagword *flags, asection **sec, bfd_vma *value);

  /* If this field is not NULL, it is called by the elf_link_output_sym
     phase of a link for each symbol which will appear in the object file.  */
  bfd_boolean (*elf_backend_link_output_symbol_hook)
    (struct bfd_link_info *info, const char *, Elf_Internal_Sym *,
     asection *, struct elf_link_hash_entry *);

  /* The CREATE_DYNAMIC_SECTIONS function is called by the ELF backend
     linker the first time it encounters a dynamic object in the link.
     This function must create any sections required for dynamic
     linking.  The ABFD argument is a dynamic object.  The .interp,
     .dynamic, .dynsym, .dynstr, and .hash functions have already been
     created, and this function may modify the section flags if
     desired.  This function will normally create the .got and .plt
     sections, but different backends have different requirements.  */
  bfd_boolean (*elf_backend_create_dynamic_sections)
    (bfd *abfd, struct bfd_link_info *info);

  /* When creating a shared library, determine whether to omit the
     dynamic symbol for the section.  */
  bfd_boolean (*elf_backend_omit_section_dynsym)
    (bfd *output_bfd, struct bfd_link_info *info, asection *osec);

  /* Return TRUE if relocations of targets are compatible to the extent
     that CHECK_RELOCS will properly process them.  PR 4424.  */
  bfd_boolean (*relocs_compatible) (const bfd_target *, const bfd_target *);

  /* The CHECK_RELOCS function is called by the add_symbols phase of
     the ELF backend linker.  It is called once for each section with
     relocs of an object file, just after the symbols for the object
     file have been added to the global linker hash table.  The
     function must look through the relocs and do any special handling
     required.  This generally means allocating space in the global
     offset table, and perhaps allocating space for a reloc.  The
     relocs are always passed as Rela structures; if the section
     actually uses Rel structures, the r_addend field will always be
     zero.  */
  bfd_boolean (*check_relocs)
    (bfd *abfd, struct bfd_link_info *info, asection *o,
     const Elf_Internal_Rela *relocs);

  /* The CHECK_DIRECTIVES function is called once per input file by
     the add_symbols phase of the ELF backend linker.  The function
     must inspect the bfd and create any additional symbols according
     to any custom directives in the bfd.  */
  bfd_boolean (*check_directives)
    (bfd *abfd, struct bfd_link_info *info);

  /* The AS_NEEDED_CLEANUP function is called once per --as-needed
     input file that was not needed by the add_symbols phase of the
     ELF backend linker.  The function must undo any target specific
     changes in the symbol hash table.  */
  bfd_boolean (*as_needed_cleanup)
    (bfd *abfd, struct bfd_link_info *info);

  /* The ADJUST_DYNAMIC_SYMBOL function is called by the ELF backend
     linker for every symbol which is defined by a dynamic object and
     referenced by a regular object.  This is called after all the
     input files have been seen, but before the SIZE_DYNAMIC_SECTIONS
     function has been called.  The hash table entry should be
     bfd_link_hash_defined ore bfd_link_hash_defweak, and it should be
     defined in a section from a dynamic object.  Dynamic object
     sections are not included in the final link, and this function is
     responsible for changing the value to something which the rest of
     the link can deal with.  This will normally involve adding an
     entry to the .plt or .got or some such section, and setting the
     symbol to point to that.  */
  bfd_boolean (*elf_backend_adjust_dynamic_symbol)
    (struct bfd_link_info *info, struct elf_link_hash_entry *h);

  /* The ALWAYS_SIZE_SECTIONS function is called by the backend linker
     after all the linker input files have been seen but before the
     section sizes have been set.  This is called after
     ADJUST_DYNAMIC_SYMBOL, but before SIZE_DYNAMIC_SECTIONS.  */
  bfd_boolean (*elf_backend_always_size_sections)
    (bfd *output_bfd, struct bfd_link_info *info);

  /* The SIZE_DYNAMIC_SECTIONS function is called by the ELF backend
     linker after all the linker input files have been seen but before
     the sections sizes have been set.  This is called after
     ADJUST_DYNAMIC_SYMBOL has been called on all appropriate symbols.
     It is only called when linking against a dynamic object.  It must
     set the sizes of the dynamic sections, and may fill in their
     contents as well.  The generic ELF linker can handle the .dynsym,
     .dynstr and .hash sections.  This function must handle the
     .interp section and any sections created by the
     CREATE_DYNAMIC_SECTIONS entry point.  */
  bfd_boolean (*elf_backend_size_dynamic_sections)
    (bfd *output_bfd, struct bfd_link_info *info);

  /* Set TEXT_INDEX_SECTION and DATA_INDEX_SECTION, the output sections
     we keep to use as a base for relocs and symbols.  */
  void (*elf_backend_init_index_section)
    (bfd *output_bfd, struct bfd_link_info *info);

  /* The RELOCATE_SECTION function is called by the ELF backend linker
     to handle the relocations for a section.

     The relocs are always passed as Rela structures; if the section
     actually uses Rel structures, the r_addend field will always be
     zero.

     This function is responsible for adjust the section contents as
     necessary, and (if using Rela relocs and generating a
     relocatable output file) adjusting the reloc addend as
     necessary.

     This function does not have to worry about setting the reloc
     address or the reloc symbol index.

     LOCAL_SYMS is a pointer to the swapped in local symbols.

     LOCAL_SECTIONS is an array giving the section in the input file
     corresponding to the st_shndx field of each local symbol.

     The global hash table entry for the global symbols can be found
     via elf_sym_hashes (input_bfd).

     When generating relocatable output, this function must handle
     STB_LOCAL/STT_SECTION symbols specially.  The output symbol is
     going to be the section symbol corresponding to the output
     section, which means that the addend must be adjusted
     accordingly.

     Returns FALSE on error, TRUE on success, 2 if successful and
     relocations should be written for this section.  */
  int (*elf_backend_relocate_section)
    (bfd *output_bfd, struct bfd_link_info *info, bfd *input_bfd,
     asection *input_section, bfd_byte *contents, Elf_Internal_Rela *relocs,
     Elf_Internal_Sym *local_syms, asection **local_sections);

  /* The FINISH_DYNAMIC_SYMBOL function is called by the ELF backend
     linker just before it writes a symbol out to the .dynsym section.
     The processor backend may make any required adjustment to the
     symbol.  It may also take the opportunity to set contents of the
     dynamic sections.  Note that FINISH_DYNAMIC_SYMBOL is called on
     all .dynsym symbols, while ADJUST_DYNAMIC_SYMBOL is only called
     on those symbols which are defined by a dynamic object.  */
  bfd_boolean (*elf_backend_finish_dynamic_symbol)
    (bfd *output_bfd, struct bfd_link_info *info,
     struct elf_link_hash_entry *h, Elf_Internal_Sym *sym);

  /* The FINISH_DYNAMIC_SECTIONS function is called by the ELF backend
     linker just before it writes all the dynamic sections out to the
     output file.  The FINISH_DYNAMIC_SYMBOL will have been called on
     all dynamic symbols.  */
  bfd_boolean (*elf_backend_finish_dynamic_sections)
    (bfd *output_bfd, struct bfd_link_info *info);

  /* A function to do any beginning processing needed for the ELF file
     before building the ELF headers and computing file positions.  */
  void (*elf_backend_begin_write_processing)
    (bfd *, struct bfd_link_info *);

  /* A function to do any final processing needed for the ELF file
     before writing it out.  The LINKER argument is TRUE if this BFD
     was created by the ELF backend linker.  */
  void (*elf_backend_final_write_processing)
    (bfd *, bfd_boolean linker);

  /* This function is called by get_program_header_size.  It should
     return the number of additional program segments which this BFD
     will need.  It should return -1 on error.  */
  int (*elf_backend_additional_program_headers)
    (bfd *, struct bfd_link_info *);

  /* This function is called to modify an existing segment map in a
     backend specific fashion.  */
  bfd_boolean (*elf_backend_modify_segment_map)
    (bfd *, struct bfd_link_info *);

  /* This function is called to modify program headers just before
     they are written.  */
  bfd_boolean (*elf_backend_modify_program_headers)
    (bfd *, struct bfd_link_info *);

  /* This function is called during section garbage collection to
     mark sections that define global symbols.  */
  bfd_boolean (*gc_mark_dynamic_ref)
    (struct elf_link_hash_entry *h, void *inf);

  /* This function is called during section gc to discover the section a
     particular relocation refers to.  */
  elf_gc_mark_hook_fn gc_mark_hook;

  /* This function, if defined, is called after the first gc marking pass
     to allow the backend to mark additional sections.  */
  bfd_boolean (*gc_mark_extra_sections)
    (struct bfd_link_info *info, elf_gc_mark_hook_fn gc_mark_hook);

  /* This function, if defined, is called during the sweep phase of gc
     in order that a backend might update any data structures it might
     be maintaining.  */
  bfd_boolean (*gc_sweep_hook)
    (bfd *abfd, struct bfd_link_info *info, asection *o,
     const Elf_Internal_Rela *relocs);

  /* This function, if defined, is called after the ELF headers have
     been created.  This allows for things like the OS and ABI versions
     to be changed.  */
  void (*elf_backend_post_process_headers)
    (bfd *, struct bfd_link_info *);

  /* This function, if defined, prints a symbol to file and returns the
     name of the symbol to be printed.  It should return NULL to fall
     back to default symbol printing.  */
  const char *(*elf_backend_print_symbol_all)
    (bfd *, void *, asymbol *);

  /* This function, if defined, is called after all local symbols and
     global symbols converted to locals are emitted into the symtab
     section.  It allows the backend to emit special local symbols
     not handled in the hash table.  */
  bfd_boolean (*elf_backend_output_arch_local_syms)
    (bfd *, struct bfd_link_info *, void *,
     bfd_boolean (*) (void *, const char *, Elf_Internal_Sym *, asection *,
		      struct elf_link_hash_entry *));

  /* This function, if defined, is called after all symbols are emitted
     into the symtab section.  It allows the backend to emit special
     global symbols not handled in the hash table.  */
  bfd_boolean (*elf_backend_output_arch_syms)
    (bfd *, struct bfd_link_info *, void *,
     bfd_boolean (*) (void *, const char *, Elf_Internal_Sym *, asection *,
		      struct elf_link_hash_entry *));

  /* Copy any information related to dynamic linking from a pre-existing
     symbol to a newly created symbol.  Also called to copy flags and
     other back-end info to a weakdef, in which case the symbol is not
     newly created and plt/got refcounts and dynamic indices should not
     be copied.  */
  void (*elf_backend_copy_indirect_symbol)
    (struct bfd_link_info *, struct elf_link_hash_entry *,
     struct elf_link_hash_entry *);

  /* Modify any information related to dynamic linking such that the
     symbol is not exported.  */
  void (*elf_backend_hide_symbol)
    (struct bfd_link_info *, struct elf_link_hash_entry *, bfd_boolean);

  /* A function to do additional symbol fixup, called by
     _bfd_elf_fix_symbol_flags.  */
  bfd_boolean (*elf_backend_fixup_symbol)
    (struct bfd_link_info *, struct elf_link_hash_entry *);

  /* Merge the backend specific symbol attribute.  */
  void (*elf_backend_merge_symbol_attribute)
    (struct elf_link_hash_entry *, const Elf_Internal_Sym *, bfd_boolean,
     bfd_boolean);

  /* Decide whether an undefined symbol is special and can be ignored.
     This is the case for OPTIONAL symbols on IRIX.  */
  bfd_boolean (*elf_backend_ignore_undef_symbol)
    (struct elf_link_hash_entry *);

  /* Emit relocations.  Overrides default routine for emitting relocs,
     except during a relocatable link, or if all relocs are being emitted.  */
  bfd_boolean (*elf_backend_emit_relocs)
    (bfd *, asection *, Elf_Internal_Shdr *, Elf_Internal_Rela *,
     struct elf_link_hash_entry **);

  /* Count relocations.  Not called for relocatable links
     or if all relocs are being preserved in the output.  */
  unsigned int (*elf_backend_count_relocs)
    (asection *, Elf_Internal_Rela *);

  /* This function, if defined, is called when an NT_PRSTATUS note is found
     in a core file.  */
  bfd_boolean (*elf_backend_grok_prstatus)
    (bfd *, Elf_Internal_Note *);

  /* This function, if defined, is called when an NT_PSINFO or NT_PRPSINFO
     note is found in a core file.  */
  bfd_boolean (*elf_backend_grok_psinfo)
    (bfd *, Elf_Internal_Note *);

  /* This function, if defined, is called to write a note to a corefile.  */
  char *(*elf_backend_write_core_note)
    (bfd *abfd, char *buf, int *bufsiz, int note_type, ...);

  /* Functions to print VMAs.  Special code to handle 64 bit ELF files.  */
  void (* elf_backend_sprintf_vma)
    (bfd *, char *, bfd_vma);
  void (* elf_backend_fprintf_vma)
    (bfd *, void *, bfd_vma);

  /* This function returns class of a reloc type.  */
  enum elf_reloc_type_class (*elf_backend_reloc_type_class)
    (const Elf_Internal_Rela *);

  /* This function, if defined, removes information about discarded functions
     from other sections which mention them.  */
  bfd_boolean (*elf_backend_discard_info)
    (bfd *, struct elf_reloc_cookie *, struct bfd_link_info *);

  /* This function, if defined, signals that the function above has removed
     the discarded relocations for this section.  */
  bfd_boolean (*elf_backend_ignore_discarded_relocs)
    (asection *);

  /* What to do when ld finds relocations against symbols defined in
     discarded sections.  */
  unsigned int (*action_discarded)
    (asection *);

  /* This function returns the width of FDE pointers in bytes, or 0 if
     that can't be determined for some reason.  The default definition
     goes by the bfd's EI_CLASS.  */
  unsigned int (*elf_backend_eh_frame_address_size)
    (bfd *, asection *);

  /* These functions tell elf-eh-frame whether to attempt to turn
     absolute or lsda encodings into pc-relative ones.  The default
     definition enables these transformations.  */
  bfd_boolean (*elf_backend_can_make_relative_eh_frame)
     (bfd *, struct bfd_link_info *, asection *);
  bfd_boolean (*elf_backend_can_make_lsda_relative_eh_frame)
     (bfd *, struct bfd_link_info *, asection *);

  /* This function returns an encoding after computing the encoded
     value (and storing it in ENCODED) for the given OFFSET into OSEC,
     to be stored in at LOC_OFFSET into the LOC_SEC input section.
     The default definition chooses a 32-bit PC-relative encoding.  */
  bfd_byte (*elf_backend_encode_eh_address)
     (bfd *abfd, struct bfd_link_info *info,
      asection *osec, bfd_vma offset,
      asection *loc_sec, bfd_vma loc_offset,
      bfd_vma *encoded);

  /* This function, if defined, may write out the given section.
     Returns TRUE if it did so and FALSE if the caller should.  */
  bfd_boolean (*elf_backend_write_section)
    (bfd *, struct bfd_link_info *, asection *, bfd_byte *);

  /* The level of IRIX compatibility we're striving for.
     MIPS ELF specific function.  */
  irix_compat_t (*elf_backend_mips_irix_compat)
    (bfd *);

  reloc_howto_type *(*elf_backend_mips_rtype_to_howto)
    (unsigned int, bfd_boolean);

  /* The swapping table to use when dealing with ECOFF information.
     Used for the MIPS ELF .mdebug section.  */
  const struct ecoff_debug_swap *elf_backend_ecoff_debug_swap;

  /* This function implements `bfd_elf_bfd_from_remote_memory';
     see elf.c, elfcode.h.  */
  bfd *(*elf_backend_bfd_from_remote_memory)
     (bfd *templ, bfd_vma ehdr_vma, bfd_vma *loadbasep,
      int (*target_read_memory) (bfd_vma vma, bfd_byte *myaddr, int len));

  /* This function is used by `_bfd_elf_get_synthetic_symtab';
     see elf.c.  */
  bfd_vma (*plt_sym_val) (bfd_vma, const asection *, const arelent *);

  /* Is symbol defined in common section?  */
  bfd_boolean (*common_definition) (Elf_Internal_Sym *);

  /* Return a common section index for section.  */
  unsigned int (*common_section_index) (asection *);

  /* Return a common section for section.  */
  asection *(*common_section) (asection *);

  /* Return TRUE if we can merge 2 definitions.  */
  bfd_boolean (*merge_symbol) (struct bfd_link_info *,
			       struct elf_link_hash_entry **,
			       struct elf_link_hash_entry *,
			       Elf_Internal_Sym *, asection **,
			       bfd_vma *, unsigned int *,
			       bfd_boolean *, bfd_boolean *,
			       bfd_boolean *, bfd_boolean *,
			       bfd_boolean *, bfd_boolean *,
			       bfd_boolean *, bfd_boolean *,
			       bfd *, asection **,
			       bfd_boolean *, bfd_boolean *,
			       bfd_boolean *, bfd_boolean *,
			       bfd *, asection **);

  /* Return TRUE if symbol should be hashed in the `.gnu.hash' section.  */
  bfd_boolean (*elf_hash_symbol) (struct elf_link_hash_entry *);

  /* Return TRUE if type is a function symbol type.  */
  bfd_boolean (*is_function_type) (unsigned int type);

  /* Used to handle bad SHF_LINK_ORDER input.  */
  bfd_error_handler_type link_order_error_handler;

  /* Name of the PLT relocation section.  */
  const char *relplt_name;

  /* Alternate EM_xxxx machine codes for this backend.  */
  int elf_machine_alt1;
  int elf_machine_alt2;

  const struct elf_size_info *s;

  /* An array of target specific special sections.  */
  const struct bfd_elf_special_section *special_sections;

  /* The size in bytes of the header for the GOT.  This includes the
     so-called reserved entries on some systems.  */
  bfd_vma got_header_size;

  /* The vendor name to use for a processor-standard attributes section.  */
  const char *obj_attrs_vendor;

  /* The section name to use for a processor-standard attributes section.  */
  const char *obj_attrs_section;

  /* Return 1, 2 or 3 to indicate what type of arguments a
     processor-specific tag takes.  */
  int (*obj_attrs_arg_type) (int);

  /* The section type to use for an attributes section.  */
  unsigned int obj_attrs_section_type;

  /* This is TRUE if the linker should act like collect and gather
     global constructors and destructors by name.  This is TRUE for
     MIPS ELF because the Irix 5 tools can not handle the .init
     section.  */
  unsigned collect : 1;

  /* This is TRUE if the linker should ignore changes to the type of a
     symbol.  This is TRUE for MIPS ELF because some Irix 5 objects
     record undefined functions as STT_OBJECT although the definitions
     are STT_FUNC.  */
  unsigned type_change_ok : 1;

  /* Whether the backend may use REL relocations.  (Some backends use
     both REL and RELA relocations, and this flag is set for those
     backends.)  */
  unsigned may_use_rel_p : 1;

  /* Whether the backend may use RELA relocations.  (Some backends use
     both REL and RELA relocations, and this flag is set for those
     backends.)  */
  unsigned may_use_rela_p : 1;

  /* Whether the default relocation type is RELA.  If a backend with
     this flag set wants REL relocations for a particular section,
     it must note that explicitly.  Similarly, if this flag is clear,
     and the backend wants RELA relocations for a particular
     section.  */
  unsigned default_use_rela_p : 1;

  /* Set if RELA relocations for a relocatable link can be handled by
     generic code.  Backends that set this flag need do nothing in the
     backend relocate_section routine for relocatable linking.  */
  unsigned rela_normal : 1;

  /* TRUE if addresses "naturally" sign extend.  This is used when
     swapping in from Elf32 when BFD64.  */
  unsigned sign_extend_vma : 1;

  unsigned want_got_plt : 1;
  unsigned plt_readonly : 1;
  unsigned want_plt_sym : 1;
  unsigned plt_not_loaded : 1;
  unsigned plt_alignment : 4;
  unsigned can_gc_sections : 1;
  unsigned can_refcount : 1;
  unsigned want_got_sym : 1;
  unsigned want_dynbss : 1;

  /* Targets which do not support physical addressing often require
     that the p_paddr field in the section header to be set to zero.
     This field indicates whether this behavior is required.  */
  unsigned want_p_paddr_set_to_zero : 1;

  /* True if an object file lacking a .note.GNU-stack section
     should be assumed to be requesting exec stack.  At least one
     other file in the link needs to have a .note.GNU-stack section
     for a PT_GNU_STACK segment to be created.  */
  unsigned default_execstack : 1;
};

/* Information stored for each BFD section in an ELF file.  This
   structure is allocated by elf_new_section_hook.  */

struct bfd_elf_section_data
{
  /* The ELF header for this section.  */
  Elf_Internal_Shdr this_hdr;

  /* The ELF header for the reloc section associated with this
     section, if any.  */
  Elf_Internal_Shdr rel_hdr;

  /* If there is a second reloc section associated with this section,
     as can happen on Irix 6, this field points to the header.  */
  Elf_Internal_Shdr *rel_hdr2;

  /* The number of relocations currently assigned to REL_HDR.  */
  unsigned int rel_count;

  /* The number of relocations currently assigned to REL_HDR2.  */
  unsigned int rel_count2;

  /* The ELF section number of this section.  */
  int this_idx;

  /* The ELF section number of the reloc section indicated by
     REL_HDR if any.  Only used for an output file.  */
  int rel_idx;

  /* The ELF section number of the reloc section indicated by
     REL_HDR2 if any.  Only used for an output file.  */
  int rel_idx2;

  /* Used by the backend linker when generating a shared library to
     record the dynamic symbol index for a section symbol
     corresponding to this section.  A value of 0 means that there is
     no dynamic symbol for this section.  */
  int dynindx;

  /* A pointer to the linked-to section for SHF_LINK_ORDER.  */
  asection *linked_to;

  /* Used by the backend linker to store the symbol hash table entries
     associated with relocs against global symbols.  */
  struct elf_link_hash_entry **rel_hashes;

  /* A pointer to the swapped relocs.  If the section uses REL relocs,
     rather than RELA, all the r_addend fields will be zero.  This
     pointer may be NULL.  It is used by the backend linker.  */
  Elf_Internal_Rela *relocs;

  /* A pointer to a linked list tracking dynamic relocs copied for
     local symbols.  */
  void *local_dynrel;

  /* A pointer to the bfd section used for dynamic relocs.  */
  asection *sreloc;

  union {
    /* Group name, if this section is a member of a group.  */
    const char *name;

    /* Group signature sym, if this is the SHT_GROUP section.  */
    struct bfd_symbol *id;
  } group;

  /* For a member of a group, points to the SHT_GROUP section.
     NULL for the SHT_GROUP section itself and non-group sections.  */
  asection *sec_group;

  /* A linked list of member sections in the group.  Circular when used by
     the linker.  For the SHT_GROUP section, points at first member.  */
  asection *next_in_group;

  /* A pointer used for various section optimizations.  */
  void *sec_info;
};

#define elf_section_data(sec)  ((struct bfd_elf_section_data*)(sec)->used_by_bfd)
#define elf_linked_to_section(sec) (elf_section_data(sec)->linked_to)
#define elf_section_type(sec)  (elf_section_data(sec)->this_hdr.sh_type)
#define elf_section_flags(sec) (elf_section_data(sec)->this_hdr.sh_flags)
#define elf_group_name(sec)    (elf_section_data(sec)->group.name)
#define elf_group_id(sec)      (elf_section_data(sec)->group.id)
#define elf_next_in_group(sec) (elf_section_data(sec)->next_in_group)
#define elf_sec_group(sec)	(elf_section_data(sec)->sec_group)

#define xvec_get_elf_backend_data(xvec) \
  ((struct elf_backend_data *) (xvec)->backend_data)

#define get_elf_backend_data(abfd) \
   xvec_get_elf_backend_data ((abfd)->xvec)

/* This struct is used to pass information to routines called via
   elf_link_hash_traverse which must return failure.  */

struct elf_info_failed
{
  bfd_boolean failed;
  struct bfd_link_info *info;
  struct bfd_elf_version_tree *verdefs;
};

/* This structure is used to pass information to
   _bfd_elf_link_assign_sym_version.  */

struct elf_assign_sym_version_info
{
  /* Output BFD.  */
  bfd *output_bfd;
  /* General link information.  */
  struct bfd_link_info *info;
  /* Version tree.  */
  struct bfd_elf_version_tree *verdefs;
  /* Whether we had a failure.  */
  bfd_boolean failed;
};

/* This structure is used to pass information to
   _bfd_elf_link_find_version_dependencies.  */

struct elf_find_verdep_info
{
  /* Output BFD.  */
  bfd *output_bfd;
  /* General link information.  */
  struct bfd_link_info *info;
  /* The number of dependencies.  */
  unsigned int vers;
  /* Whether we had a failure.  */
  bfd_boolean failed;
};

/* The maximum number of known object attributes for any target.  */
#define NUM_KNOWN_OBJ_ATTRIBUTES 71

/* The value of an object attribute.  type & 1 indicates whether there
   is an integer value; type & 2 indicates whether there is a string
   value.  */

typedef struct obj_attribute
{
  int type;
  unsigned int i;
  char *s;
} obj_attribute;

typedef struct obj_attribute_list
{
  struct obj_attribute_list *next;
  int tag;
  obj_attribute attr;
} obj_attribute_list;

/* Object attributes may either be defined by the processor ABI, index
   OBJ_ATTR_PROC in the *_obj_attributes arrays, or be GNU-specific
   (and possibly also processor-specific), index OBJ_ATTR_GNU.  */
#define OBJ_ATTR_PROC 0
#define OBJ_ATTR_GNU 1
#define OBJ_ATTR_FIRST OBJ_ATTR_PROC
#define OBJ_ATTR_LAST OBJ_ATTR_GNU

/* The following object attribute tags are taken as generic, for all
   targets and for "gnu" where there is no target standard.  */
enum
{
  Tag_NULL = 0,
  Tag_File = 1,
  Tag_Section = 2,
  Tag_Symbol = 3,
  Tag_compatibility = 32
};

/* Some private data is stashed away for future use using the tdata pointer
   in the bfd structure.  */

struct elf_obj_tdata
{
  Elf_Internal_Ehdr elf_header[1];	/* Actual data, but ref like ptr */
  Elf_Internal_Shdr **elf_sect_ptr;
  Elf_Internal_Phdr *phdr;
  struct elf_segment_map *segment_map;
  struct elf_strtab_hash *strtab_ptr;
  int num_locals;
  int num_globals;
  unsigned int num_elf_sections;	/* elf_sect_ptr size */
  int num_section_syms;
  asymbol **section_syms;		/* STT_SECTION symbols for each section */
  Elf_Internal_Shdr symtab_hdr;
  Elf_Internal_Shdr shstrtab_hdr;
  Elf_Internal_Shdr strtab_hdr;
  Elf_Internal_Shdr dynsymtab_hdr;
  Elf_Internal_Shdr dynstrtab_hdr;
  Elf_Internal_Shdr dynversym_hdr;
  Elf_Internal_Shdr dynverref_hdr;
  Elf_Internal_Shdr dynverdef_hdr;
  Elf_Internal_Shdr symtab_shndx_hdr;
  unsigned int symtab_section, shstrtab_section;
  unsigned int strtab_section, dynsymtab_section;
  unsigned int symtab_shndx_section;
  unsigned int dynversym_section, dynverdef_section, dynverref_section;
  file_ptr next_file_pos;
  bfd_vma gp;				/* The gp value */
  unsigned int gp_size;			/* The gp size */

  /* Information grabbed from an elf core file.  */
  int core_signal;
  int core_pid;
  int core_lwpid;
  char* core_program;
  char* core_command;

  /* A mapping from external symbols to entries in the linker hash
     table, used when linking.  This is indexed by the symbol index
     minus the sh_info field of the symbol table header.  */
  struct elf_link_hash_entry **sym_hashes;

  /* Track usage and final offsets of GOT entries for local symbols.
     This array is indexed by symbol index.  Elements are used
     identically to "got" in struct elf_link_hash_entry.  */
  union
    {
      bfd_signed_vma *refcounts;
      bfd_vma *offsets;
      struct got_entry **ents;
    } local_got;

  /* The linker ELF emulation code needs to let the backend ELF linker
     know what filename should be used for a dynamic object if the
     dynamic object is found using a search.  The emulation code then
     sometimes needs to know what name was actually used.  Until the
     file has been added to the linker symbol table, this field holds
     the name the linker wants.  After it has been added, it holds the
     name actually used, which will be the DT_SONAME entry if there is
     one.  */
  const char *dt_name;

  /* Records the result of `get_program_header_size'.  */
  bfd_size_type program_header_size;

  /* Used by find_nearest_line entry point.  */
  void *line_info;

  /* Used by MIPS ELF find_nearest_line entry point.  The structure
     could be included directly in this one, but there's no point to
     wasting the memory just for the infrequently called
     find_nearest_line.  */
  struct mips_elf_find_line *find_line_info;

  /* A place to stash dwarf1 info for this bfd.  */
  struct dwarf1_debug *dwarf1_find_line_info;

  /* A place to stash dwarf2 info for this bfd.  */
  void *dwarf2_find_line_info;

  /* An array of stub sections indexed by symbol number, used by the
     MIPS ELF linker.  FIXME: We should figure out some way to only
     include this field for a MIPS ELF target.  */
  asection **local_stubs;
  asection **local_call_stubs;

  /* Used to determine if PT_GNU_EH_FRAME segment header should be
     created.  */
  asection *eh_frame_hdr;

  Elf_Internal_Shdr **group_sect_ptr;
  int num_group;

  /* Number of symbol version definitions we are about to emit.  */
  unsigned int cverdefs;

  /* Number of symbol version references we are about to emit.  */
  unsigned int cverrefs;

  /* Segment flags for the PT_GNU_STACK segment.  */
  unsigned int stack_flags;

  /* Should the PT_GNU_RELRO segment be emitted?  */
  bfd_boolean relro;

  /* Symbol version definitions in external objects.  */
  Elf_Internal_Verdef *verdef;

  /* Symbol version references to external objects.  */
  Elf_Internal_Verneed *verref;

  /* The Irix 5 support uses two virtual sections, which represent
     text/data symbols defined in dynamic objects.  */
  asymbol *elf_data_symbol;
  asymbol *elf_text_symbol;
  asection *elf_data_section;
  asection *elf_text_section;

  /* Whether a dyanmic object was specified normally on the linker
     command line, or was specified when --as-needed was in effect,
     or was found via a DT_NEEDED entry.  */
  enum dynamic_lib_link_class dyn_lib_class;

  /* This is set to TRUE if the object was created by the backend
     linker.  */
  bfd_boolean linker;

  /* Irix 5 often screws up the symbol table, sorting local symbols
     after global symbols.  This flag is set if the symbol table in
     this BFD appears to be screwed up.  If it is, we ignore the
     sh_info field in the symbol table header, and always read all the
     symbols.  */
  bfd_boolean bad_symtab;

  /* Used to determine if the e_flags field has been initialized */
  bfd_boolean flags_init;

  /* Symbol buffer.  */
  void *symbuf;

  obj_attribute known_obj_attributes[2][NUM_KNOWN_OBJ_ATTRIBUTES];
  obj_attribute_list *other_obj_attributes[2];
};

#define elf_tdata(bfd)		((bfd) -> tdata.elf_obj_data)
#define elf_elfheader(bfd)	(elf_tdata(bfd) -> elf_header)
#define elf_elfsections(bfd)	(elf_tdata(bfd) -> elf_sect_ptr)
#define elf_numsections(bfd)	(elf_tdata(bfd) -> num_elf_sections)
#define elf_shstrtab(bfd)	(elf_tdata(bfd) -> strtab_ptr)
#define elf_onesymtab(bfd)	(elf_tdata(bfd) -> symtab_section)
#define elf_symtab_shndx(bfd)	(elf_tdata(bfd) -> symtab_shndx_section)
#define elf_dynsymtab(bfd)	(elf_tdata(bfd) -> dynsymtab_section)
#define elf_dynversym(bfd)	(elf_tdata(bfd) -> dynversym_section)
#define elf_dynverdef(bfd)	(elf_tdata(bfd) -> dynverdef_section)
#define elf_dynverref(bfd)	(elf_tdata(bfd) -> dynverref_section)
#define elf_num_locals(bfd)	(elf_tdata(bfd) -> num_locals)
#define elf_num_globals(bfd)	(elf_tdata(bfd) -> num_globals)
#define elf_section_syms(bfd)	(elf_tdata(bfd) -> section_syms)
#define elf_num_section_syms(bfd) (elf_tdata(bfd) -> num_section_syms)
#define core_prpsinfo(bfd)	(elf_tdata(bfd) -> prpsinfo)
#define core_prstatus(bfd)	(elf_tdata(bfd) -> prstatus)
#define elf_gp(bfd)		(elf_tdata(bfd) -> gp)
#define elf_gp_size(bfd)	(elf_tdata(bfd) -> gp_size)
#define elf_sym_hashes(bfd)	(elf_tdata(bfd) -> sym_hashes)
#define elf_local_got_refcounts(bfd) (elf_tdata(bfd) -> local_got.refcounts)
#define elf_local_got_offsets(bfd) (elf_tdata(bfd) -> local_got.offsets)
#define elf_local_got_ents(bfd) (elf_tdata(bfd) -> local_got.ents)
#define elf_dt_name(bfd)	(elf_tdata(bfd) -> dt_name)
#define elf_dyn_lib_class(bfd)	(elf_tdata(bfd) -> dyn_lib_class)
#define elf_bad_symtab(bfd)	(elf_tdata(bfd) -> bad_symtab)
#define elf_flags_init(bfd)	(elf_tdata(bfd) -> flags_init)
#define elf_known_obj_attributes(bfd) (elf_tdata (bfd) -> known_obj_attributes)
#define elf_other_obj_attributes(bfd) (elf_tdata (bfd) -> other_obj_attributes)
#define elf_known_obj_attributes_proc(bfd) \
  (elf_known_obj_attributes (bfd) [OBJ_ATTR_PROC])
#define elf_other_obj_attributes_proc(bfd) \
  (elf_other_obj_attributes (bfd) [OBJ_ATTR_PROC])

extern void _bfd_elf_swap_verdef_in
  (bfd *, const Elf_External_Verdef *, Elf_Internal_Verdef *);
extern void _bfd_elf_swap_verdef_out
  (bfd *, const Elf_Internal_Verdef *, Elf_External_Verdef *);
extern void _bfd_elf_swap_verdaux_in
  (bfd *, const Elf_External_Verdaux *, Elf_Internal_Verdaux *);
extern void _bfd_elf_swap_verdaux_out
  (bfd *, const Elf_Internal_Verdaux *, Elf_External_Verdaux *);
extern void _bfd_elf_swap_verneed_in
  (bfd *, const Elf_External_Verneed *, Elf_Internal_Verneed *);
extern void _bfd_elf_swap_verneed_out
  (bfd *, const Elf_Internal_Verneed *, Elf_External_Verneed *);
extern void _bfd_elf_swap_vernaux_in
  (bfd *, const Elf_External_Vernaux *, Elf_Internal_Vernaux *);
extern void _bfd_elf_swap_vernaux_out
  (bfd *, const Elf_Internal_Vernaux *, Elf_External_Vernaux *);
extern void _bfd_elf_swap_versym_in
  (bfd *, const Elf_External_Versym *, Elf_Internal_Versym *);
extern void _bfd_elf_swap_versym_out
  (bfd *, const Elf_Internal_Versym *, Elf_External_Versym *);

extern int _bfd_elf_section_from_bfd_section
  (bfd *, asection *);
extern char *bfd_elf_string_from_elf_section
  (bfd *, unsigned, unsigned);
extern char *bfd_elf_get_str_section
  (bfd *, unsigned);
extern Elf_Internal_Sym *bfd_elf_get_elf_syms
  (bfd *, Elf_Internal_Shdr *, size_t, size_t, Elf_Internal_Sym *, void *,
   Elf_External_Sym_Shndx *);
extern const char *bfd_elf_sym_name
  (bfd *, Elf_Internal_Shdr *, Elf_Internal_Sym *, asection *);

extern bfd_boolean _bfd_elf_copy_private_bfd_data
  (bfd *, bfd *);
extern bfd_boolean _bfd_elf_print_private_bfd_data
  (bfd *, void *);
extern void bfd_elf_print_symbol
  (bfd *, void *, asymbol *, bfd_print_symbol_type);

extern void _bfd_elf_sprintf_vma
  (bfd *, char *, bfd_vma);
extern void _bfd_elf_fprintf_vma
  (bfd *, void *, bfd_vma);

extern unsigned int _bfd_elf_eh_frame_address_size
  (bfd *, asection *);
extern bfd_byte _bfd_elf_encode_eh_address
  (bfd *abfd, struct bfd_link_info *info, asection *osec, bfd_vma offset,
   asection *loc_sec, bfd_vma loc_offset, bfd_vma *encoded);
extern bfd_boolean _bfd_elf_can_make_relative
  (bfd *input_bfd, struct bfd_link_info *info, asection *eh_frame_section);

extern enum elf_reloc_type_class _bfd_elf_reloc_type_class
  (const Elf_Internal_Rela *);
extern bfd_vma _bfd_elf_rela_local_sym
  (bfd *, Elf_Internal_Sym *, asection **, Elf_Internal_Rela *);
extern bfd_vma _bfd_elf_rel_local_sym
  (bfd *, Elf_Internal_Sym *, asection **, bfd_vma);
extern bfd_vma _bfd_elf_section_offset
  (bfd *, struct bfd_link_info *, asection *, bfd_vma);

extern unsigned long bfd_elf_hash
  (const char *);
extern unsigned long bfd_elf_gnu_hash
  (const char *);

extern bfd_reloc_status_type bfd_elf_generic_reloc
  (bfd *, arelent *, asymbol *, void *, asection *, bfd *, char **);
extern bfd_boolean bfd_elf_mkobject
  (bfd *);
extern bfd_boolean bfd_elf_mkcorefile
  (bfd *);
extern Elf_Internal_Shdr *bfd_elf_find_section
  (bfd *, char *);
extern bfd_boolean _bfd_elf_make_section_from_shdr
  (bfd *, Elf_Internal_Shdr *, const char *, int);
extern bfd_boolean _bfd_elf_make_section_from_phdr
  (bfd *, Elf_Internal_Phdr *, int, const char *);
extern struct bfd_hash_entry *_bfd_elf_link_hash_newfunc
  (struct bfd_hash_entry *, struct bfd_hash_table *, const char *);
extern struct bfd_link_hash_table *_bfd_elf_link_hash_table_create
  (bfd *);
extern void _bfd_elf_link_hash_copy_indirect
  (struct bfd_link_info *, struct elf_link_hash_entry *,
   struct elf_link_hash_entry *);
extern void _bfd_elf_link_hash_hide_symbol
  (struct bfd_link_info *, struct elf_link_hash_entry *, bfd_boolean);
extern bfd_boolean _bfd_elf_link_hash_fixup_symbol
  (struct bfd_link_info *, struct elf_link_hash_entry *);
extern bfd_boolean _bfd_elf_link_hash_table_init
  (struct elf_link_hash_table *, bfd *,
   struct bfd_hash_entry *(*)
     (struct bfd_hash_entry *, struct bfd_hash_table *, const char *),
   unsigned int);
extern bfd_boolean _bfd_elf_slurp_version_tables
  (bfd *, bfd_boolean);
extern bfd_boolean _bfd_elf_merge_sections
  (bfd *, struct bfd_link_info *);
extern bfd_boolean _bfd_elf_match_sections_by_type
  (bfd *, const asection *, bfd *, const asection *);
extern bfd_boolean bfd_elf_is_group_section
  (bfd *, const struct bfd_section *);
extern void _bfd_elf_section_already_linked
  (bfd *, struct bfd_section *, struct bfd_link_info *);
extern void bfd_elf_set_group_contents
  (bfd *, asection *, void *);
extern asection *_bfd_elf_check_kept_section
  (asection *, struct bfd_link_info *);
extern void _bfd_elf_link_just_syms
  (asection *, struct bfd_link_info *);
extern bfd_boolean _bfd_elf_copy_private_header_data
  (bfd *, bfd *);
extern bfd_boolean _bfd_elf_copy_private_symbol_data
  (bfd *, asymbol *, bfd *, asymbol *);
#define _bfd_generic_init_private_section_data \
  _bfd_elf_init_private_section_data
extern bfd_boolean _bfd_elf_init_private_section_data
  (bfd *, asection *, bfd *, asection *, struct bfd_link_info *);
extern bfd_boolean _bfd_elf_copy_private_section_data
  (bfd *, asection *, bfd *, asection *);
extern bfd_boolean _bfd_elf_write_object_contents
  (bfd *);
extern bfd_boolean _bfd_elf_write_corefile_contents
  (bfd *);
extern bfd_boolean _bfd_elf_set_section_contents
  (bfd *, sec_ptr, const void *, file_ptr, bfd_size_type);
extern long _bfd_elf_get_symtab_upper_bound
  (bfd *);
extern long _bfd_elf_canonicalize_symtab
  (bfd *, asymbol **);
extern long _bfd_elf_get_dynamic_symtab_upper_bound
  (bfd *);
extern long _bfd_elf_canonicalize_dynamic_symtab
  (bfd *, asymbol **);
extern long _bfd_elf_get_synthetic_symtab
  (bfd *, long, asymbol **, long, asymbol **, asymbol **);
extern long _bfd_elf_get_reloc_upper_bound
  (bfd *, sec_ptr);
extern long _bfd_elf_canonicalize_reloc
  (bfd *, sec_ptr, arelent **, asymbol **);
extern long _bfd_elf_get_dynamic_reloc_upper_bound
  (bfd *);
extern long _bfd_elf_canonicalize_dynamic_reloc
  (bfd *, arelent **, asymbol **);
extern asymbol *_bfd_elf_make_empty_symbol
  (bfd *);
extern void _bfd_elf_get_symbol_info
  (bfd *, asymbol *, symbol_info *);
extern bfd_boolean _bfd_elf_is_local_label_name
  (bfd *, const char *);
extern alent *_bfd_elf_get_lineno
  (bfd *, asymbol *);
extern bfd_boolean _bfd_elf_set_arch_mach
  (bfd *, enum bfd_architecture, unsigned long);
extern bfd_boolean _bfd_elf_find_nearest_line
  (bfd *, asection *, asymbol **, bfd_vma, const char **, const char **,
   unsigned int *);
extern bfd_boolean _bfd_elf_find_line
  (bfd *, asymbol **, asymbol *, const char **, unsigned int *);
#define _bfd_generic_find_line _bfd_elf_find_line
extern bfd_boolean _bfd_elf_find_inliner_info
  (bfd *, const char **, const char **, unsigned int *);
#define _bfd_elf_read_minisymbols _bfd_generic_read_minisymbols
#define _bfd_elf_minisymbol_to_symbol _bfd_generic_minisymbol_to_symbol
extern int _bfd_elf_sizeof_headers
  (bfd *, struct bfd_link_info *);
extern bfd_boolean _bfd_elf_new_section_hook
  (bfd *, asection *);
extern bfd_boolean _bfd_elf_init_reloc_shdr
  (bfd *, Elf_Internal_Shdr *, asection *, bfd_boolean);
extern const struct bfd_elf_special_section *_bfd_elf_get_special_section
  (const char *, const struct bfd_elf_special_section *, unsigned int);
extern const struct bfd_elf_special_section *_bfd_elf_get_sec_type_attr
  (bfd *, asection *);

/* If the target doesn't have reloc handling written yet:  */
extern void _bfd_elf_no_info_to_howto
  (bfd *, arelent *, Elf_Internal_Rela *);

extern bfd_boolean bfd_section_from_shdr
  (bfd *, unsigned int shindex);
extern bfd_boolean bfd_section_from_phdr
  (bfd *, Elf_Internal_Phdr *, int);

extern int _bfd_elf_symbol_from_bfd_symbol
  (bfd *, asymbol **);

extern asection *bfd_section_from_r_symndx
  (bfd *, struct sym_sec_cache *, asection *, unsigned long);
extern asection *bfd_section_from_elf_index
  (bfd *, unsigned int);
extern struct bfd_strtab_hash *_bfd_elf_stringtab_init
  (void);

extern struct elf_strtab_hash * _bfd_elf_strtab_init
  (void);
extern void _bfd_elf_strtab_free
  (struct elf_strtab_hash *);
extern bfd_size_type _bfd_elf_strtab_add
  (struct elf_strtab_hash *, const char *, bfd_boolean);
extern void _bfd_elf_strtab_addref
  (struct elf_strtab_hash *, bfd_size_type);
extern void _bfd_elf_strtab_delref
  (struct elf_strtab_hash *, bfd_size_type);
extern void _bfd_elf_strtab_clear_all_refs
  (struct elf_strtab_hash *);
extern bfd_size_type _bfd_elf_strtab_size
  (struct elf_strtab_hash *);
extern bfd_size_type _bfd_elf_strtab_offset
  (struct elf_strtab_hash *, bfd_size_type);
extern bfd_boolean _bfd_elf_strtab_emit
  (bfd *, struct elf_strtab_hash *);
extern void _bfd_elf_strtab_finalize
  (struct elf_strtab_hash *);

extern bfd_boolean _bfd_elf_discard_section_eh_frame
  (bfd *, struct bfd_link_info *, asection *,
   bfd_boolean (*) (bfd_vma, void *), struct elf_reloc_cookie *);
extern bfd_boolean _bfd_elf_discard_section_eh_frame_hdr
  (bfd *, struct bfd_link_info *);
extern bfd_vma _bfd_elf_eh_frame_section_offset
  (bfd *, struct bfd_link_info *, asection *, bfd_vma);
extern bfd_boolean _bfd_elf_write_section_eh_frame
  (bfd *, struct bfd_link_info *, asection *, bfd_byte *);
extern bfd_boolean _bfd_elf_write_section_eh_frame_hdr
  (bfd *, struct bfd_link_info *);
extern bfd_boolean _bfd_elf_maybe_strip_eh_frame_hdr
  (struct bfd_link_info *);

extern bfd_boolean _bfd_elf_merge_symbol
  (bfd *, struct bfd_link_info *, const char *, Elf_Internal_Sym *,
   asection **, bfd_vma *, unsigned int *,
   struct elf_link_hash_entry **, bfd_boolean *,
   bfd_boolean *, bfd_boolean *, bfd_boolean *);

extern bfd_boolean _bfd_elf_hash_symbol (struct elf_link_hash_entry *);

extern bfd_boolean _bfd_elf_add_default_symbol
  (bfd *, struct bfd_link_info *, struct elf_link_hash_entry *,
   const char *, Elf_Internal_Sym *, asection **, bfd_vma *,
   bfd_boolean *, bfd_boolean);

extern bfd_boolean _bfd_elf_export_symbol
  (struct elf_link_hash_entry *, void *);

extern bfd_boolean _bfd_elf_link_find_version_dependencies
  (struct elf_link_hash_entry *, void *);

extern bfd_boolean _bfd_elf_link_assign_sym_version
  (struct elf_link_hash_entry *, void *);

extern long _bfd_elf_link_lookup_local_dynindx
  (struct bfd_link_info *, bfd *, long);
extern bfd_boolean _bfd_elf_compute_section_file_positions
  (bfd *, struct bfd_link_info *);
extern void _bfd_elf_assign_file_positions_for_relocs
  (bfd *);
extern file_ptr _bfd_elf_assign_file_position_for_section
  (Elf_Internal_Shdr *, file_ptr, bfd_boolean);

extern bfd_boolean _bfd_elf_validate_reloc
  (bfd *, arelent *);

extern bfd_boolean _bfd_elf_link_create_dynamic_sections
  (bfd *, struct bfd_link_info *);
extern bfd_boolean _bfd_elf_link_omit_section_dynsym
  (bfd *, struct bfd_link_info *, asection *);
extern bfd_boolean _bfd_elf_create_dynamic_sections
  (bfd *, struct bfd_link_info *);
extern bfd_boolean _bfd_elf_create_got_section
  (bfd *, struct bfd_link_info *);
extern struct elf_link_hash_entry *_bfd_elf_define_linkage_sym
  (bfd *, struct bfd_link_info *, asection *, const char *);
extern void _bfd_elf_init_1_index_section
  (bfd *, struct bfd_link_info *);
extern void _bfd_elf_init_2_index_sections
  (bfd *, struct bfd_link_info *);

extern bfd_boolean _bfd_elfcore_make_pseudosection
  (bfd *, char *, size_t, ufile_ptr);
extern char *_bfd_elfcore_strndup
  (bfd *, char *, size_t);

extern Elf_Internal_Rela *_bfd_elf_link_read_relocs
  (bfd *, asection *, void *, Elf_Internal_Rela *, bfd_boolean);

extern bfd_boolean _bfd_elf_link_size_reloc_section
  (bfd *, Elf_Internal_Shdr *, asection *);

extern bfd_boolean _bfd_elf_link_output_relocs
  (bfd *, asection *, Elf_Internal_Shdr *, Elf_Internal_Rela *,
   struct elf_link_hash_entry **);

extern bfd_boolean _bfd_elf_fix_symbol_flags
  (struct elf_link_hash_entry *, struct elf_info_failed *);

extern bfd_boolean _bfd_elf_adjust_dynamic_symbol
  (struct elf_link_hash_entry *, void *);

extern bfd_boolean _bfd_elf_adjust_dynamic_copy
  (struct elf_link_hash_entry *, asection *);

extern bfd_boolean _bfd_elf_link_sec_merge_syms
  (struct elf_link_hash_entry *, void *);

extern bfd_boolean _bfd_elf_dynamic_symbol_p
  (struct elf_link_hash_entry *, struct bfd_link_info *, bfd_boolean);

extern bfd_boolean _bfd_elf_symbol_refs_local_p
  (struct elf_link_hash_entry *, struct bfd_link_info *, bfd_boolean);

extern bfd_boolean bfd_elf_match_symbols_in_sections
  (asection *, asection *, struct bfd_link_info *);

extern void bfd_elf_perform_complex_relocation
  (bfd *                   output_bfd ATTRIBUTE_UNUSED,
   struct bfd_link_info *  info,
   bfd *                   input_bfd,
   asection *              input_section,
   bfd_byte *              contents,
   Elf_Internal_Rela *     rel,
   Elf_Internal_Sym *      local_syms,
   asection **             local_sections);

extern bfd_boolean _bfd_elf_setup_sections
  (bfd *);

extern void _bfd_elf_set_osabi (bfd * , struct bfd_link_info *);

extern const bfd_target *bfd_elf32_object_p
  (bfd *);
extern const bfd_target *bfd_elf32_core_file_p
  (bfd *);
extern char *bfd_elf32_core_file_failing_command
  (bfd *);
extern int bfd_elf32_core_file_failing_signal
  (bfd *);
extern bfd_boolean bfd_elf32_core_file_matches_executable_p
  (bfd *, bfd *);

extern bfd_boolean bfd_elf32_swap_symbol_in
  (bfd *, const void *, const void *, Elf_Internal_Sym *);
extern void bfd_elf32_swap_symbol_out
  (bfd *, const Elf_Internal_Sym *, void *, void *);
extern void bfd_elf32_swap_reloc_in
  (bfd *, const bfd_byte *, Elf_Internal_Rela *);
extern void bfd_elf32_swap_reloc_out
  (bfd *, const Elf_Internal_Rela *, bfd_byte *);
extern void bfd_elf32_swap_reloca_in
  (bfd *, const bfd_byte *, Elf_Internal_Rela *);
extern void bfd_elf32_swap_reloca_out
  (bfd *, const Elf_Internal_Rela *, bfd_byte *);
extern void bfd_elf32_swap_phdr_in
  (bfd *, const Elf32_External_Phdr *, Elf_Internal_Phdr *);
extern void bfd_elf32_swap_phdr_out
  (bfd *, const Elf_Internal_Phdr *, Elf32_External_Phdr *);
extern void bfd_elf32_swap_dyn_in
  (bfd *, const void *, Elf_Internal_Dyn *);
extern void bfd_elf32_swap_dyn_out
  (bfd *, const Elf_Internal_Dyn *, void *);
extern long bfd_elf32_slurp_symbol_table
  (bfd *, asymbol **, bfd_boolean);
extern bfd_boolean bfd_elf32_write_shdrs_and_ehdr
  (bfd *);
extern int bfd_elf32_write_out_phdrs
  (bfd *, const Elf_Internal_Phdr *, unsigned int);
extern void bfd_elf32_write_relocs
  (bfd *, asection *, void *);
extern bfd_boolean bfd_elf32_slurp_reloc_table
  (bfd *, asection *, asymbol **, bfd_boolean);

extern const bfd_target *bfd_elf64_object_p
  (bfd *);
extern const bfd_target *bfd_elf64_core_file_p
  (bfd *);
extern char *bfd_elf64_core_file_failing_command
  (bfd *);
extern int bfd_elf64_core_file_failing_signal
  (bfd *);
extern bfd_boolean bfd_elf64_core_file_matches_executable_p
  (bfd *, bfd *);

extern bfd_boolean bfd_elf64_swap_symbol_in
  (bfd *, const void *, const void *, Elf_Internal_Sym *);
extern void bfd_elf64_swap_symbol_out
  (bfd *, const Elf_Internal_Sym *, void *, void *);
extern void bfd_elf64_swap_reloc_in
  (bfd *, const bfd_byte *, Elf_Internal_Rela *);
extern void bfd_elf64_swap_reloc_out
  (bfd *, const Elf_Internal_Rela *, bfd_byte *);
extern void bfd_elf64_swap_reloca_in
  (bfd *, const bfd_byte *, Elf_Internal_Rela *);
extern void bfd_elf64_swap_reloca_out
  (bfd *, const Elf_Internal_Rela *, bfd_byte *);
extern void bfd_elf64_swap_phdr_in
  (bfd *, const Elf64_External_Phdr *, Elf_Internal_Phdr *);
extern void bfd_elf64_swap_phdr_out
  (bfd *, const Elf_Internal_Phdr *, Elf64_External_Phdr *);
extern void bfd_elf64_swap_dyn_in
  (bfd *, const void *, Elf_Internal_Dyn *);
extern void bfd_elf64_swap_dyn_out
  (bfd *, const Elf_Internal_Dyn *, void *);
extern long bfd_elf64_slurp_symbol_table
  (bfd *, asymbol **, bfd_boolean);
extern bfd_boolean bfd_elf64_write_shdrs_and_ehdr
  (bfd *);
extern int bfd_elf64_write_out_phdrs
  (bfd *, const Elf_Internal_Phdr *, unsigned int);
extern void bfd_elf64_write_relocs
  (bfd *, asection *, void *);
extern bfd_boolean bfd_elf64_slurp_reloc_table
  (bfd *, asection *, asymbol **, bfd_boolean);

extern bfd_boolean _bfd_elf_default_relocs_compatible
  (const bfd_target *, const bfd_target *);

extern bfd_boolean _bfd_elf_relocs_compatible
  (const bfd_target *, const bfd_target *);

extern struct elf_link_hash_entry *_bfd_elf_archive_symbol_lookup
  (bfd *, struct bfd_link_info *, const char *);
extern bfd_boolean bfd_elf_link_add_symbols
  (bfd *, struct bfd_link_info *);
extern bfd_boolean _bfd_elf_add_dynamic_entry
  (struct bfd_link_info *, bfd_vma, bfd_vma);

extern bfd_boolean bfd_elf_link_record_dynamic_symbol
  (struct bfd_link_info *, struct elf_link_hash_entry *);

extern int bfd_elf_link_record_local_dynamic_symbol
  (struct bfd_link_info *, bfd *, long);

extern void bfd_elf_link_mark_dynamic_symbol
  (struct bfd_link_info *, struct elf_link_hash_entry *,
   Elf_Internal_Sym *);

extern bfd_boolean _bfd_elf_close_and_cleanup
  (bfd *);

extern bfd_boolean _bfd_elf_common_definition
  (Elf_Internal_Sym *);

extern unsigned int _bfd_elf_common_section_index
  (asection *);

extern asection *_bfd_elf_common_section
  (asection *);

extern void _bfd_dwarf2_cleanup_debug_info
  (bfd *);

extern bfd_reloc_status_type _bfd_elf_rel_vtable_reloc_fn
  (bfd *, arelent *, struct bfd_symbol *, void *,
   asection *, bfd *, char **);

extern bfd_boolean bfd_elf_final_link
  (bfd *, struct bfd_link_info *);

extern bfd_boolean bfd_elf_gc_mark_dynamic_ref_symbol
  (struct elf_link_hash_entry *h, void *inf);

extern bfd_boolean bfd_elf_gc_sections
  (bfd *, struct bfd_link_info *);

extern bfd_boolean bfd_elf_gc_record_vtinherit
  (bfd *, asection *, struct elf_link_hash_entry *, bfd_vma);

extern bfd_boolean bfd_elf_gc_record_vtentry
  (bfd *, asection *, struct elf_link_hash_entry *, bfd_vma);

extern asection *_bfd_elf_gc_mark_hook
  (asection *, struct bfd_link_info *, Elf_Internal_Rela *,
   struct elf_link_hash_entry *, Elf_Internal_Sym *);

extern bfd_boolean _bfd_elf_gc_mark
  (struct bfd_link_info *, asection *,
   asection * (*) (asection *, struct bfd_link_info *, Elf_Internal_Rela *,
		   struct elf_link_hash_entry *, Elf_Internal_Sym *));

extern bfd_boolean bfd_elf_gc_common_finalize_got_offsets
  (bfd *, struct bfd_link_info *);

extern bfd_boolean bfd_elf_gc_common_final_link
  (bfd *, struct bfd_link_info *);

extern bfd_boolean bfd_elf_reloc_symbol_deleted_p
  (bfd_vma, void *);

extern struct elf_segment_map * _bfd_elf_make_dynamic_segment
  (bfd *, asection *);

extern bfd_boolean _bfd_elf_map_sections_to_segments
  (bfd *, struct bfd_link_info *);

extern bfd_boolean _bfd_elf_is_function_type (unsigned int);

/* Exported interface for writing elf corefile notes. */
extern char *elfcore_write_note
  (bfd *, char *, int *, const char *, int, const void *, int);
extern char *elfcore_write_prpsinfo
  (bfd *, char *, int *, const char *, const char *);
extern char *elfcore_write_prstatus
  (bfd *, char *, int *, long, int, const void *);
extern char * elfcore_write_pstatus
  (bfd *, char *, int *, long, int, const void *);
extern char *elfcore_write_prfpreg
  (bfd *, char *, int *, const void *, int);
extern char *elfcore_write_thrmisc
  (bfd *, char *, int *, const char *, int);
extern char *elfcore_write_prxfpreg
  (bfd *, char *, int *, const void *, int);
extern char *elfcore_write_lwpstatus
  (bfd *, char *, int *, long, int, const void *);

extern bfd *_bfd_elf32_bfd_from_remote_memory
  (bfd *templ, bfd_vma ehdr_vma, bfd_vma *loadbasep,
   int (*target_read_memory) (bfd_vma, bfd_byte *, int));
extern bfd *_bfd_elf64_bfd_from_remote_memory
  (bfd *templ, bfd_vma ehdr_vma, bfd_vma *loadbasep,
   int (*target_read_memory) (bfd_vma, bfd_byte *, int));

extern bfd_vma bfd_elf_obj_attr_size (bfd *);
extern void bfd_elf_set_obj_attr_contents (bfd *, bfd_byte *, bfd_vma);
extern int bfd_elf_get_obj_attr_int (bfd *, int, int);
extern void bfd_elf_add_obj_attr_int (bfd *, int, int, unsigned int);
#define bfd_elf_add_proc_attr_int(BFD, TAG, VALUE) \
  bfd_elf_add_obj_attr_int ((BFD), OBJ_ATTR_PROC, (TAG), (VALUE))
extern void bfd_elf_add_obj_attr_string (bfd *, int, int, const char *);
#define bfd_elf_add_proc_attr_string(BFD, TAG, VALUE) \
  bfd_elf_add_obj_attr_string ((BFD), OBJ_ATTR_PROC, (TAG), (VALUE))
extern void bfd_elf_add_obj_attr_compat (bfd *, int, unsigned int,
					 const char *);
#define bfd_elf_add_proc_attr_compat(BFD, INTVAL, STRVAL) \
  bfd_elf_add_obj_attr_compat ((BFD), OBJ_ATTR_PROC, (INTVAL), (STRVAL))

extern char *_bfd_elf_attr_strdup (bfd *, const char *);
extern void _bfd_elf_copy_obj_attributes (bfd *, bfd *);
extern int _bfd_elf_obj_attrs_arg_type (bfd *, int, int);
extern void _bfd_elf_parse_attributes (bfd *, Elf_Internal_Shdr *);
extern bfd_boolean _bfd_elf_merge_object_attributes (bfd *, bfd *);

/* Large common section.  */
extern asection _bfd_elf_large_com_section;

/* SH ELF specific routine.  */

extern bfd_boolean _sh_elf_set_mach_from_flags
  (bfd *);

/* This is the condition under which finish_dynamic_symbol will be called.
   If our finish_dynamic_symbol isn't called, we'll need to do something
   about initializing any .plt and .got entries in relocate_section.  */
#define WILL_CALL_FINISH_DYNAMIC_SYMBOL(DYN, SHARED, H) \
  ((DYN)								\
   && ((SHARED) || !(H)->forced_local)					\
   && ((H)->dynindx != -1 || (H)->forced_local))

/* This macro is to avoid lots of duplicated code in the body
   of xxx_relocate_section() in the various elfxx-xxxx.c files.  */
#define RELOC_FOR_GLOBAL_SYMBOL(info, input_bfd, input_section, rel,	\
				r_symndx, symtab_hdr, sym_hashes,	\
				h, sec, relocation,			\
				unresolved_reloc, warned)		\
  do									\
    {									\
      /* It seems this can happen with erroneous or unsupported		\
	 input (mixing a.out and elf in an archive, for example.)  */	\
      if (sym_hashes == NULL)						\
	return FALSE;							\
									\
      h = sym_hashes[r_symndx - symtab_hdr->sh_info];			\
									\
      while (h->root.type == bfd_link_hash_indirect			\
	     || h->root.type == bfd_link_hash_warning)			\
	h = (struct elf_link_hash_entry *) h->root.u.i.link;		\
									\
      warned = FALSE;							\
      unresolved_reloc = FALSE;						\
      relocation = 0;							\
      if (h->root.type == bfd_link_hash_defined				\
	  || h->root.type == bfd_link_hash_defweak)			\
	{								\
	  sec = h->root.u.def.section;					\
	  if (sec == NULL						\
	      || sec->output_section == NULL)				\
	    /* Set a flag that will be cleared later if we find a	\
	       relocation value for this symbol.  output_section	\
	       is typically NULL for symbols satisfied by a shared	\
	       library.  */						\
	    unresolved_reloc = TRUE;					\
	  else								\
	    relocation = (h->root.u.def.value				\
			  + sec->output_section->vma			\
			  + sec->output_offset);			\
	}								\
      else if (h->root.type == bfd_link_hash_undefweak)			\
	;								\
      else if (info->unresolved_syms_in_objects == RM_IGNORE		\
	       && ELF_ST_VISIBILITY (h->other) == STV_DEFAULT)		\
	;								\
      else if (!info->relocatable)					\
	{								\
	  bfd_boolean err;						\
	  err = (info->unresolved_syms_in_objects == RM_GENERATE_ERROR	\
		 || ELF_ST_VISIBILITY (h->other) != STV_DEFAULT);	\
	  if (!info->callbacks->undefined_symbol (info,			\
						  h->root.root.string,	\
						  input_bfd,		\
						  input_section,	\
						  rel->r_offset, err))	\
	    return FALSE;						\
	  warned = TRUE;						\
	}								\
    }									\
  while (0)

/* Will a symbol be bound to the the definition within the shared
   library, if any.  */
#define SYMBOLIC_BIND(INFO, H) \
    ((INFO)->symbolic || ((INFO)->dynamic && !(H)->dynamic))

#endif /* _LIBELF_H_ */
