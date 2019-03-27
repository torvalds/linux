/* MIPS-specific support for ELF
   Copyright 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002,
   2003, 2004, 2005, 2006, 2007 Free Software Foundation, Inc.

   Most of the information added by Ian Lance Taylor, Cygnus Support,
   <ian@cygnus.com>.
   N32/64 ABI support added by Mark Mitchell, CodeSourcery, LLC.
   <mark@codesourcery.com>
   Traditional MIPS targets support added by Koundinya.K, Dansk Data
   Elektronik & Operations Research Group. <kk@ddeorg.soft.net>

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

/* This file handles functionality common to the different MIPS ABI's.  */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "libiberty.h"
#include "elf-bfd.h"
#include "elfxx-mips.h"
#include "elf/mips.h"
#include "elf-vxworks.h"

/* Get the ECOFF swapping routines.  */
#include "coff/sym.h"
#include "coff/symconst.h"
#include "coff/ecoff.h"
#include "coff/mips.h"

#include "hashtab.h"

/* This structure is used to hold information about one GOT entry.
   There are three types of entry:

      (1) absolute addresses
	    (abfd == NULL)
      (2) SYMBOL + OFFSET addresses, where SYMBOL is local to an input bfd
	    (abfd != NULL, symndx >= 0)
      (3) global and forced-local symbols
	    (abfd != NULL, symndx == -1)

   Type (3) entries are treated differently for different types of GOT.
   In the "master" GOT -- i.e.  the one that describes every GOT
   reference needed in the link -- the mips_got_entry is keyed on both
   the symbol and the input bfd that references it.  If it turns out
   that we need multiple GOTs, we can then use this information to
   create separate GOTs for each input bfd.

   However, we want each of these separate GOTs to have at most one
   entry for a given symbol, so their type (3) entries are keyed only
   on the symbol.  The input bfd given by the "abfd" field is somewhat
   arbitrary in this case.

   This means that when there are multiple GOTs, each GOT has a unique
   mips_got_entry for every symbol within it.  We can therefore use the
   mips_got_entry fields (tls_type and gotidx) to track the symbol's
   GOT index.

   However, if it turns out that we need only a single GOT, we continue
   to use the master GOT to describe it.  There may therefore be several
   mips_got_entries for the same symbol, each with a different input bfd.
   We want to make sure that each symbol gets a unique GOT entry, so when
   there's a single GOT, we use the symbol's hash entry, not the
   mips_got_entry fields, to track a symbol's GOT index.  */
struct mips_got_entry
{
  /* The input bfd in which the symbol is defined.  */
  bfd *abfd;
  /* The index of the symbol, as stored in the relocation r_info, if
     we have a local symbol; -1 otherwise.  */
  long symndx;
  union
  {
    /* If abfd == NULL, an address that must be stored in the got.  */
    bfd_vma address;
    /* If abfd != NULL && symndx != -1, the addend of the relocation
       that should be added to the symbol value.  */
    bfd_vma addend;
    /* If abfd != NULL && symndx == -1, the hash table entry
       corresponding to a global symbol in the got (or, local, if
       h->forced_local).  */
    struct mips_elf_link_hash_entry *h;
  } d;

  /* The TLS types included in this GOT entry (specifically, GD and
     IE).  The GD and IE flags can be added as we encounter new
     relocations.  LDM can also be set; it will always be alone, not
     combined with any GD or IE flags.  An LDM GOT entry will be
     a local symbol entry with r_symndx == 0.  */
  unsigned char tls_type;

  /* The offset from the beginning of the .got section to the entry
     corresponding to this symbol+addend.  If it's a global symbol
     whose offset is yet to be decided, it's going to be -1.  */
  long gotidx;
};

/* This structure is used to hold .got information when linking.  */

struct mips_got_info
{
  /* The global symbol in the GOT with the lowest index in the dynamic
     symbol table.  */
  struct elf_link_hash_entry *global_gotsym;
  /* The number of global .got entries.  */
  unsigned int global_gotno;
  /* The number of .got slots used for TLS.  */
  unsigned int tls_gotno;
  /* The first unused TLS .got entry.  Used only during
     mips_elf_initialize_tls_index.  */
  unsigned int tls_assigned_gotno;
  /* The number of local .got entries.  */
  unsigned int local_gotno;
  /* The number of local .got entries we have used.  */
  unsigned int assigned_gotno;
  /* A hash table holding members of the got.  */
  struct htab *got_entries;
  /* A hash table mapping input bfds to other mips_got_info.  NULL
     unless multi-got was necessary.  */
  struct htab *bfd2got;
  /* In multi-got links, a pointer to the next got (err, rather, most
     of the time, it points to the previous got).  */
  struct mips_got_info *next;
  /* This is the GOT index of the TLS LDM entry for the GOT, MINUS_ONE
     for none, or MINUS_TWO for not yet assigned.  This is needed
     because a single-GOT link may have multiple hash table entries
     for the LDM.  It does not get initialized in multi-GOT mode.  */
  bfd_vma tls_ldm_offset;
};

/* Map an input bfd to a got in a multi-got link.  */

struct mips_elf_bfd2got_hash {
  bfd *bfd;
  struct mips_got_info *g;
};

/* Structure passed when traversing the bfd2got hash table, used to
   create and merge bfd's gots.  */

struct mips_elf_got_per_bfd_arg
{
  /* A hashtable that maps bfds to gots.  */
  htab_t bfd2got;
  /* The output bfd.  */
  bfd *obfd;
  /* The link information.  */
  struct bfd_link_info *info;
  /* A pointer to the primary got, i.e., the one that's going to get
     the implicit relocations from DT_MIPS_LOCAL_GOTNO and
     DT_MIPS_GOTSYM.  */
  struct mips_got_info *primary;
  /* A non-primary got we're trying to merge with other input bfd's
     gots.  */
  struct mips_got_info *current;
  /* The maximum number of got entries that can be addressed with a
     16-bit offset.  */
  unsigned int max_count;
  /* The number of local and global entries in the primary got.  */
  unsigned int primary_count;
  /* The number of local and global entries in the current got.  */
  unsigned int current_count;
  /* The total number of global entries which will live in the
     primary got and be automatically relocated.  This includes
     those not referenced by the primary GOT but included in
     the "master" GOT.  */
  unsigned int global_count;
};

/* Another structure used to pass arguments for got entries traversal.  */

struct mips_elf_set_global_got_offset_arg
{
  struct mips_got_info *g;
  int value;
  unsigned int needed_relocs;
  struct bfd_link_info *info;
};

/* A structure used to count TLS relocations or GOT entries, for GOT
   entry or ELF symbol table traversal.  */

struct mips_elf_count_tls_arg
{
  struct bfd_link_info *info;
  unsigned int needed;
};

struct _mips_elf_section_data
{
  struct bfd_elf_section_data elf;
  union
  {
    struct mips_got_info *got_info;
    bfd_byte *tdata;
  } u;
};

#define mips_elf_section_data(sec) \
  ((struct _mips_elf_section_data *) elf_section_data (sec))

/* This structure is passed to mips_elf_sort_hash_table_f when sorting
   the dynamic symbols.  */

struct mips_elf_hash_sort_data
{
  /* The symbol in the global GOT with the lowest dynamic symbol table
     index.  */
  struct elf_link_hash_entry *low;
  /* The least dynamic symbol table index corresponding to a non-TLS
     symbol with a GOT entry.  */
  long min_got_dynindx;
  /* The greatest dynamic symbol table index corresponding to a symbol
     with a GOT entry that is not referenced (e.g., a dynamic symbol
     with dynamic relocations pointing to it from non-primary GOTs).  */
  long max_unref_got_dynindx;
  /* The greatest dynamic symbol table index not corresponding to a
     symbol without a GOT entry.  */
  long max_non_got_dynindx;
};

/* The MIPS ELF linker needs additional information for each symbol in
   the global hash table.  */

struct mips_elf_link_hash_entry
{
  struct elf_link_hash_entry root;

  /* External symbol information.  */
  EXTR esym;

  /* Number of R_MIPS_32, R_MIPS_REL32, or R_MIPS_64 relocs against
     this symbol.  */
  unsigned int possibly_dynamic_relocs;

  /* If the R_MIPS_32, R_MIPS_REL32, or R_MIPS_64 reloc is against
     a readonly section.  */
  bfd_boolean readonly_reloc;

  /* We must not create a stub for a symbol that has relocations
     related to taking the function's address, i.e. any but
     R_MIPS_CALL*16 ones -- see "MIPS ABI Supplement, 3rd Edition",
     p. 4-20.  */
  bfd_boolean no_fn_stub;

  /* If there is a stub that 32 bit functions should use to call this
     16 bit function, this points to the section containing the stub.  */
  asection *fn_stub;

  /* Whether we need the fn_stub; this is set if this symbol appears
     in any relocs other than a 16 bit call.  */
  bfd_boolean need_fn_stub;

  /* If there is a stub that 16 bit functions should use to call this
     32 bit function, this points to the section containing the stub.  */
  asection *call_stub;

  /* This is like the call_stub field, but it is used if the function
     being called returns a floating point value.  */
  asection *call_fp_stub;

  /* Are we forced local?  This will only be set if we have converted
     the initial global GOT entry to a local GOT entry.  */
  bfd_boolean forced_local;

  /* Are we referenced by some kind of relocation?  */
  bfd_boolean is_relocation_target;

  /* Are we referenced by branch relocations?  */
  bfd_boolean is_branch_target;

#define GOT_NORMAL	0
#define GOT_TLS_GD	1
#define GOT_TLS_LDM	2
#define GOT_TLS_IE	4
#define GOT_TLS_OFFSET_DONE    0x40
#define GOT_TLS_DONE    0x80
  unsigned char tls_type;
  /* This is only used in single-GOT mode; in multi-GOT mode there
     is one mips_got_entry per GOT entry, so the offset is stored
     there.  In single-GOT mode there may be many mips_got_entry
     structures all referring to the same GOT slot.  It might be
     possible to use root.got.offset instead, but that field is
     overloaded already.  */
  bfd_vma tls_got_offset;
};

/* MIPS ELF linker hash table.  */

struct mips_elf_link_hash_table
{
  struct elf_link_hash_table root;
#if 0
  /* We no longer use this.  */
  /* String section indices for the dynamic section symbols.  */
  bfd_size_type dynsym_sec_strindex[SIZEOF_MIPS_DYNSYM_SECNAMES];
#endif
  /* The number of .rtproc entries.  */
  bfd_size_type procedure_count;
  /* The size of the .compact_rel section (if SGI_COMPAT).  */
  bfd_size_type compact_rel_size;
  /* This flag indicates that the value of DT_MIPS_RLD_MAP dynamic
     entry is set to the address of __rld_obj_head as in IRIX5.  */
  bfd_boolean use_rld_obj_head;
  /* This is the value of the __rld_map or __rld_obj_head symbol.  */
  bfd_vma rld_value;
  /* This is set if we see any mips16 stub sections.  */
  bfd_boolean mips16_stubs_seen;
  /* True if we're generating code for VxWorks.  */
  bfd_boolean is_vxworks;
  /* Shortcuts to some dynamic sections, or NULL if they are not
     being used.  */
  asection *srelbss;
  asection *sdynbss;
  asection *srelplt;
  asection *srelplt2;
  asection *sgotplt;
  asection *splt;
  /* The size of the PLT header in bytes (VxWorks only).  */
  bfd_vma plt_header_size;
  /* The size of a PLT entry in bytes (VxWorks only).  */
  bfd_vma plt_entry_size;
  /* The size of a function stub entry in bytes.  */
  bfd_vma function_stub_size;
};

#define TLS_RELOC_P(r_type) \
  (r_type == R_MIPS_TLS_DTPMOD32		\
   || r_type == R_MIPS_TLS_DTPMOD64		\
   || r_type == R_MIPS_TLS_DTPREL32		\
   || r_type == R_MIPS_TLS_DTPREL64		\
   || r_type == R_MIPS_TLS_GD			\
   || r_type == R_MIPS_TLS_LDM			\
   || r_type == R_MIPS_TLS_DTPREL_HI16		\
   || r_type == R_MIPS_TLS_DTPREL_LO16		\
   || r_type == R_MIPS_TLS_GOTTPREL		\
   || r_type == R_MIPS_TLS_TPREL32		\
   || r_type == R_MIPS_TLS_TPREL64		\
   || r_type == R_MIPS_TLS_TPREL_HI16		\
   || r_type == R_MIPS_TLS_TPREL_LO16)

/* Structure used to pass information to mips_elf_output_extsym.  */

struct extsym_info
{
  bfd *abfd;
  struct bfd_link_info *info;
  struct ecoff_debug_info *debug;
  const struct ecoff_debug_swap *swap;
  bfd_boolean failed;
};

/* The names of the runtime procedure table symbols used on IRIX5.  */

static const char * const mips_elf_dynsym_rtproc_names[] =
{
  "_procedure_table",
  "_procedure_string_table",
  "_procedure_table_size",
  NULL
};

/* These structures are used to generate the .compact_rel section on
   IRIX5.  */

typedef struct
{
  unsigned long id1;		/* Always one?  */
  unsigned long num;		/* Number of compact relocation entries.  */
  unsigned long id2;		/* Always two?  */
  unsigned long offset;		/* The file offset of the first relocation.  */
  unsigned long reserved0;	/* Zero?  */
  unsigned long reserved1;	/* Zero?  */
} Elf32_compact_rel;

typedef struct
{
  bfd_byte id1[4];
  bfd_byte num[4];
  bfd_byte id2[4];
  bfd_byte offset[4];
  bfd_byte reserved0[4];
  bfd_byte reserved1[4];
} Elf32_External_compact_rel;

typedef struct
{
  unsigned int ctype : 1;	/* 1: long 0: short format. See below.  */
  unsigned int rtype : 4;	/* Relocation types. See below.  */
  unsigned int dist2to : 8;
  unsigned int relvaddr : 19;	/* (VADDR - vaddr of the previous entry)/ 4 */
  unsigned long konst;		/* KONST field. See below.  */
  unsigned long vaddr;		/* VADDR to be relocated.  */
} Elf32_crinfo;

typedef struct
{
  unsigned int ctype : 1;	/* 1: long 0: short format. See below.  */
  unsigned int rtype : 4;	/* Relocation types. See below.  */
  unsigned int dist2to : 8;
  unsigned int relvaddr : 19;	/* (VADDR - vaddr of the previous entry)/ 4 */
  unsigned long konst;		/* KONST field. See below.  */
} Elf32_crinfo2;

typedef struct
{
  bfd_byte info[4];
  bfd_byte konst[4];
  bfd_byte vaddr[4];
} Elf32_External_crinfo;

typedef struct
{
  bfd_byte info[4];
  bfd_byte konst[4];
} Elf32_External_crinfo2;

/* These are the constants used to swap the bitfields in a crinfo.  */

#define CRINFO_CTYPE (0x1)
#define CRINFO_CTYPE_SH (31)
#define CRINFO_RTYPE (0xf)
#define CRINFO_RTYPE_SH (27)
#define CRINFO_DIST2TO (0xff)
#define CRINFO_DIST2TO_SH (19)
#define CRINFO_RELVADDR (0x7ffff)
#define CRINFO_RELVADDR_SH (0)

/* A compact relocation info has long (3 words) or short (2 words)
   formats.  A short format doesn't have VADDR field and relvaddr
   fields contains ((VADDR - vaddr of the previous entry) >> 2).  */
#define CRF_MIPS_LONG			1
#define CRF_MIPS_SHORT			0

/* There are 4 types of compact relocation at least. The value KONST
   has different meaning for each type:

   (type)		(konst)
   CT_MIPS_REL32	Address in data
   CT_MIPS_WORD		Address in word (XXX)
   CT_MIPS_GPHI_LO	GP - vaddr
   CT_MIPS_JMPAD	Address to jump
   */

#define CRT_MIPS_REL32			0xa
#define CRT_MIPS_WORD			0xb
#define CRT_MIPS_GPHI_LO		0xc
#define CRT_MIPS_JMPAD			0xd

#define mips_elf_set_cr_format(x,format)	((x).ctype = (format))
#define mips_elf_set_cr_type(x,type)		((x).rtype = (type))
#define mips_elf_set_cr_dist2to(x,v)		((x).dist2to = (v))
#define mips_elf_set_cr_relvaddr(x,d)		((x).relvaddr = (d)<<2)

/* The structure of the runtime procedure descriptor created by the
   loader for use by the static exception system.  */

typedef struct runtime_pdr {
	bfd_vma	adr;		/* Memory address of start of procedure.  */
	long	regmask;	/* Save register mask.  */
	long	regoffset;	/* Save register offset.  */
	long	fregmask;	/* Save floating point register mask.  */
	long	fregoffset;	/* Save floating point register offset.  */
	long	frameoffset;	/* Frame size.  */
	short	framereg;	/* Frame pointer register.  */
	short	pcreg;		/* Offset or reg of return pc.  */
	long	irpss;		/* Index into the runtime string table.  */
	long	reserved;
	struct exception_info *exception_info;/* Pointer to exception array.  */
} RPDR, *pRPDR;
#define cbRPDR sizeof (RPDR)
#define rpdNil ((pRPDR) 0)

static struct mips_got_entry *mips_elf_create_local_got_entry
  (bfd *, struct bfd_link_info *, bfd *, struct mips_got_info *, asection *,
   bfd_vma, unsigned long, struct mips_elf_link_hash_entry *, int);
static bfd_boolean mips_elf_sort_hash_table_f
  (struct mips_elf_link_hash_entry *, void *);
static bfd_vma mips_elf_high
  (bfd_vma);
static bfd_boolean mips16_stub_section_p
  (bfd *, asection *);
static bfd_boolean mips_elf_create_dynamic_relocation
  (bfd *, struct bfd_link_info *, const Elf_Internal_Rela *,
   struct mips_elf_link_hash_entry *, asection *, bfd_vma,
   bfd_vma *, asection *);
static hashval_t mips_elf_got_entry_hash
  (const void *);
static bfd_vma mips_elf_adjust_gp
  (bfd *, struct mips_got_info *, bfd *);
static struct mips_got_info *mips_elf_got_for_ibfd
  (struct mips_got_info *, bfd *);

/* This will be used when we sort the dynamic relocation records.  */
static bfd *reldyn_sorting_bfd;

/* Nonzero if ABFD is using the N32 ABI.  */
#define ABI_N32_P(abfd) \
  ((elf_elfheader (abfd)->e_flags & EF_MIPS_ABI2) != 0)

/* Nonzero if ABFD is using the N64 ABI.  */
#define ABI_64_P(abfd) \
  (get_elf_backend_data (abfd)->s->elfclass == ELFCLASS64)

/* Nonzero if ABFD is using NewABI conventions.  */
#define NEWABI_P(abfd) (ABI_N32_P (abfd) || ABI_64_P (abfd))

/* The IRIX compatibility level we are striving for.  */
#define IRIX_COMPAT(abfd) \
  (get_elf_backend_data (abfd)->elf_backend_mips_irix_compat (abfd))

/* Whether we are trying to be compatible with IRIX at all.  */
#define SGI_COMPAT(abfd) \
  (IRIX_COMPAT (abfd) != ict_none)

/* The name of the options section.  */
#define MIPS_ELF_OPTIONS_SECTION_NAME(abfd) \
  (NEWABI_P (abfd) ? ".MIPS.options" : ".options")

/* True if NAME is the recognized name of any SHT_MIPS_OPTIONS section.
   Some IRIX system files do not use MIPS_ELF_OPTIONS_SECTION_NAME.  */
#define MIPS_ELF_OPTIONS_SECTION_NAME_P(NAME) \
  (strcmp (NAME, ".MIPS.options") == 0 || strcmp (NAME, ".options") == 0)

/* Whether the section is readonly.  */
#define MIPS_ELF_READONLY_SECTION(sec) \
  ((sec->flags & (SEC_ALLOC | SEC_LOAD | SEC_READONLY))		\
   == (SEC_ALLOC | SEC_LOAD | SEC_READONLY))

/* The name of the stub section.  */
#define MIPS_ELF_STUB_SECTION_NAME(abfd) ".MIPS.stubs"

/* The size of an external REL relocation.  */
#define MIPS_ELF_REL_SIZE(abfd) \
  (get_elf_backend_data (abfd)->s->sizeof_rel)

/* The size of an external RELA relocation.  */
#define MIPS_ELF_RELA_SIZE(abfd) \
  (get_elf_backend_data (abfd)->s->sizeof_rela)

/* The size of an external dynamic table entry.  */
#define MIPS_ELF_DYN_SIZE(abfd) \
  (get_elf_backend_data (abfd)->s->sizeof_dyn)

/* The size of the rld_map pointer.  */
#define MIPS_ELF_RLD_MAP_SIZE(abfd) \
  (get_elf_backend_data (abfd)->s->arch_size / 8)

/* The size of a GOT entry.  */
#define MIPS_ELF_GOT_SIZE(abfd) \
  (get_elf_backend_data (abfd)->s->arch_size / 8)

/* The size of a symbol-table entry.  */
#define MIPS_ELF_SYM_SIZE(abfd) \
  (get_elf_backend_data (abfd)->s->sizeof_sym)

/* The default alignment for sections, as a power of two.  */
#define MIPS_ELF_LOG_FILE_ALIGN(abfd)				\
  (get_elf_backend_data (abfd)->s->log_file_align)

/* Get word-sized data.  */
#define MIPS_ELF_GET_WORD(abfd, ptr) \
  (ABI_64_P (abfd) ? bfd_get_64 (abfd, ptr) : bfd_get_32 (abfd, ptr))

/* Put out word-sized data.  */
#define MIPS_ELF_PUT_WORD(abfd, val, ptr)	\
  (ABI_64_P (abfd) 				\
   ? bfd_put_64 (abfd, val, ptr) 		\
   : bfd_put_32 (abfd, val, ptr))

/* Add a dynamic symbol table-entry.  */
#define MIPS_ELF_ADD_DYNAMIC_ENTRY(info, tag, val)	\
  _bfd_elf_add_dynamic_entry (info, tag, val)

#define MIPS_ELF_RTYPE_TO_HOWTO(abfd, rtype, rela)			\
  (get_elf_backend_data (abfd)->elf_backend_mips_rtype_to_howto (rtype, rela))

/* Determine whether the internal relocation of index REL_IDX is REL
   (zero) or RELA (non-zero).  The assumption is that, if there are
   two relocation sections for this section, one of them is REL and
   the other is RELA.  If the index of the relocation we're testing is
   in range for the first relocation section, check that the external
   relocation size is that for RELA.  It is also assumed that, if
   rel_idx is not in range for the first section, and this first
   section contains REL relocs, then the relocation is in the second
   section, that is RELA.  */
#define MIPS_RELOC_RELA_P(abfd, sec, rel_idx)				\
  ((NUM_SHDR_ENTRIES (&elf_section_data (sec)->rel_hdr)			\
    * get_elf_backend_data (abfd)->s->int_rels_per_ext_rel		\
    > (bfd_vma)(rel_idx))						\
   == (elf_section_data (sec)->rel_hdr.sh_entsize			\
       == (ABI_64_P (abfd) ? sizeof (Elf64_External_Rela)		\
	   : sizeof (Elf32_External_Rela))))

/* The name of the dynamic relocation section.  */
#define MIPS_ELF_REL_DYN_NAME(INFO) \
  (mips_elf_hash_table (INFO)->is_vxworks ? ".rela.dyn" : ".rel.dyn")

/* In case we're on a 32-bit machine, construct a 64-bit "-1" value
   from smaller values.  Start with zero, widen, *then* decrement.  */
#define MINUS_ONE	(((bfd_vma)0) - 1)
#define MINUS_TWO	(((bfd_vma)0) - 2)

/* The number of local .got entries we reserve.  */
#define MIPS_RESERVED_GOTNO(INFO) \
  (mips_elf_hash_table (INFO)->is_vxworks ? 3 : 2)

/* The offset of $gp from the beginning of the .got section.  */
#define ELF_MIPS_GP_OFFSET(INFO) \
  (mips_elf_hash_table (INFO)->is_vxworks ? 0x0 : 0x7ff0)

/* The maximum size of the GOT for it to be addressable using 16-bit
   offsets from $gp.  */
#define MIPS_ELF_GOT_MAX_SIZE(INFO) (ELF_MIPS_GP_OFFSET (INFO) + 0x7fff)

/* Instructions which appear in a stub.  */
#define STUB_LW(abfd)							\
  ((ABI_64_P (abfd)							\
    ? 0xdf998010				/* ld t9,0x8010(gp) */	\
    : 0x8f998010))              		/* lw t9,0x8010(gp) */
#define STUB_MOVE(abfd)							\
   ((ABI_64_P (abfd)							\
     ? 0x03e0782d				/* daddu t7,ra */	\
     : 0x03e07821))				/* addu t7,ra */
#define STUB_LUI(VAL) (0x3c180000 + (VAL))	/* lui t8,VAL */
#define STUB_JALR 0x0320f809			/* jalr t9,ra */
#define STUB_ORI(VAL) (0x37180000 + (VAL))	/* ori t8,t8,VAL */
#define STUB_LI16U(VAL) (0x34180000 + (VAL))	/* ori t8,zero,VAL unsigned */
#define STUB_LI16S(abfd, VAL)						\
   ((ABI_64_P (abfd)							\
    ? (0x64180000 + (VAL))	/* daddiu t8,zero,VAL sign extended */	\
    : (0x24180000 + (VAL))))	/* addiu t8,zero,VAL sign extended */

#define MIPS_FUNCTION_STUB_NORMAL_SIZE 16
#define MIPS_FUNCTION_STUB_BIG_SIZE 20

/* The name of the dynamic interpreter.  This is put in the .interp
   section.  */

#define ELF_DYNAMIC_INTERPRETER(abfd) 		\
   (ABI_N32_P (abfd) ? "/usr/lib32/libc.so.1" 	\
    : ABI_64_P (abfd) ? "/usr/lib64/libc.so.1" 	\
    : "/usr/lib/libc.so.1")

#ifdef BFD64
#define MNAME(bfd,pre,pos) \
  (ABI_64_P (bfd) ? CONCAT4 (pre,64,_,pos) : CONCAT4 (pre,32,_,pos))
#define ELF_R_SYM(bfd, i)					\
  (ABI_64_P (bfd) ? ELF64_R_SYM (i) : ELF32_R_SYM (i))
#define ELF_R_TYPE(bfd, i)					\
  (ABI_64_P (bfd) ? ELF64_MIPS_R_TYPE (i) : ELF32_R_TYPE (i))
#define ELF_R_INFO(bfd, s, t)					\
  (ABI_64_P (bfd) ? ELF64_R_INFO (s, t) : ELF32_R_INFO (s, t))
#else
#define MNAME(bfd,pre,pos) CONCAT4 (pre,32,_,pos)
#define ELF_R_SYM(bfd, i)					\
  (ELF32_R_SYM (i))
#define ELF_R_TYPE(bfd, i)					\
  (ELF32_R_TYPE (i))
#define ELF_R_INFO(bfd, s, t)					\
  (ELF32_R_INFO (s, t))
#endif

  /* The mips16 compiler uses a couple of special sections to handle
     floating point arguments.

     Section names that look like .mips16.fn.FNNAME contain stubs that
     copy floating point arguments from the fp regs to the gp regs and
     then jump to FNNAME.  If any 32 bit function calls FNNAME, the
     call should be redirected to the stub instead.  If no 32 bit
     function calls FNNAME, the stub should be discarded.  We need to
     consider any reference to the function, not just a call, because
     if the address of the function is taken we will need the stub,
     since the address might be passed to a 32 bit function.

     Section names that look like .mips16.call.FNNAME contain stubs
     that copy floating point arguments from the gp regs to the fp
     regs and then jump to FNNAME.  If FNNAME is a 32 bit function,
     then any 16 bit function that calls FNNAME should be redirected
     to the stub instead.  If FNNAME is not a 32 bit function, the
     stub should be discarded.

     .mips16.call.fp.FNNAME sections are similar, but contain stubs
     which call FNNAME and then copy the return value from the fp regs
     to the gp regs.  These stubs store the return value in $18 while
     calling FNNAME; any function which might call one of these stubs
     must arrange to save $18 around the call.  (This case is not
     needed for 32 bit functions that call 16 bit functions, because
     16 bit functions always return floating point values in both
     $f0/$f1 and $2/$3.)

     Note that in all cases FNNAME might be defined statically.
     Therefore, FNNAME is not used literally.  Instead, the relocation
     information will indicate which symbol the section is for.

     We record any stubs that we find in the symbol table.  */

#define FN_STUB ".mips16.fn."
#define CALL_STUB ".mips16.call."
#define CALL_FP_STUB ".mips16.call.fp."

#define FN_STUB_P(name) CONST_STRNEQ (name, FN_STUB)
#define CALL_STUB_P(name) CONST_STRNEQ (name, CALL_STUB)
#define CALL_FP_STUB_P(name) CONST_STRNEQ (name, CALL_FP_STUB)

/* The format of the first PLT entry in a VxWorks executable.  */
static const bfd_vma mips_vxworks_exec_plt0_entry[] = {
  0x3c190000,	/* lui t9, %hi(_GLOBAL_OFFSET_TABLE_)		*/
  0x27390000,	/* addiu t9, t9, %lo(_GLOBAL_OFFSET_TABLE_)	*/
  0x8f390008,	/* lw t9, 8(t9)					*/
  0x00000000,	/* nop						*/
  0x03200008,	/* jr t9					*/
  0x00000000	/* nop						*/
};

/* The format of subsequent PLT entries.  */
static const bfd_vma mips_vxworks_exec_plt_entry[] = {
  0x10000000,	/* b .PLT_resolver			*/
  0x24180000,	/* li t8, <pltindex>			*/
  0x3c190000,	/* lui t9, %hi(<.got.plt slot>)		*/
  0x27390000,	/* addiu t9, t9, %lo(<.got.plt slot>)	*/
  0x8f390000,	/* lw t9, 0(t9)				*/
  0x00000000,	/* nop					*/
  0x03200008,	/* jr t9				*/
  0x00000000	/* nop					*/
};

/* The format of the first PLT entry in a VxWorks shared object.  */
static const bfd_vma mips_vxworks_shared_plt0_entry[] = {
  0x8f990008,	/* lw t9, 8(gp)		*/
  0x00000000,	/* nop			*/
  0x03200008,	/* jr t9		*/
  0x00000000,	/* nop			*/
  0x00000000,	/* nop			*/
  0x00000000	/* nop			*/
};

/* The format of subsequent PLT entries.  */
static const bfd_vma mips_vxworks_shared_plt_entry[] = {
  0x10000000,	/* b .PLT_resolver	*/
  0x24180000	/* li t8, <pltindex>	*/
};

/* Look up an entry in a MIPS ELF linker hash table.  */

#define mips_elf_link_hash_lookup(table, string, create, copy, follow)	\
  ((struct mips_elf_link_hash_entry *)					\
   elf_link_hash_lookup (&(table)->root, (string), (create),		\
			 (copy), (follow)))

/* Traverse a MIPS ELF linker hash table.  */

#define mips_elf_link_hash_traverse(table, func, info)			\
  (elf_link_hash_traverse						\
   (&(table)->root,							\
    (bfd_boolean (*) (struct elf_link_hash_entry *, void *)) (func),	\
    (info)))

/* Get the MIPS ELF linker hash table from a link_info structure.  */

#define mips_elf_hash_table(p) \
  ((struct mips_elf_link_hash_table *) ((p)->hash))

/* Find the base offsets for thread-local storage in this object,
   for GD/LD and IE/LE respectively.  */

#define TP_OFFSET 0x7000
#define DTP_OFFSET 0x8000

static bfd_vma
dtprel_base (struct bfd_link_info *info)
{
  /* If tls_sec is NULL, we should have signalled an error already.  */
  if (elf_hash_table (info)->tls_sec == NULL)
    return 0;
  return elf_hash_table (info)->tls_sec->vma + DTP_OFFSET;
}

static bfd_vma
tprel_base (struct bfd_link_info *info)
{
  /* If tls_sec is NULL, we should have signalled an error already.  */
  if (elf_hash_table (info)->tls_sec == NULL)
    return 0;
  return elf_hash_table (info)->tls_sec->vma + TP_OFFSET;
}

/* Create an entry in a MIPS ELF linker hash table.  */

static struct bfd_hash_entry *
mips_elf_link_hash_newfunc (struct bfd_hash_entry *entry,
			    struct bfd_hash_table *table, const char *string)
{
  struct mips_elf_link_hash_entry *ret =
    (struct mips_elf_link_hash_entry *) entry;

  /* Allocate the structure if it has not already been allocated by a
     subclass.  */
  if (ret == NULL)
    ret = bfd_hash_allocate (table, sizeof (struct mips_elf_link_hash_entry));
  if (ret == NULL)
    return (struct bfd_hash_entry *) ret;

  /* Call the allocation method of the superclass.  */
  ret = ((struct mips_elf_link_hash_entry *)
	 _bfd_elf_link_hash_newfunc ((struct bfd_hash_entry *) ret,
				     table, string));
  if (ret != NULL)
    {
      /* Set local fields.  */
      memset (&ret->esym, 0, sizeof (EXTR));
      /* We use -2 as a marker to indicate that the information has
	 not been set.  -1 means there is no associated ifd.  */
      ret->esym.ifd = -2;
      ret->possibly_dynamic_relocs = 0;
      ret->readonly_reloc = FALSE;
      ret->no_fn_stub = FALSE;
      ret->fn_stub = NULL;
      ret->need_fn_stub = FALSE;
      ret->call_stub = NULL;
      ret->call_fp_stub = NULL;
      ret->forced_local = FALSE;
      ret->is_branch_target = FALSE;
      ret->is_relocation_target = FALSE;
      ret->tls_type = GOT_NORMAL;
    }

  return (struct bfd_hash_entry *) ret;
}

bfd_boolean
_bfd_mips_elf_new_section_hook (bfd *abfd, asection *sec)
{
  if (!sec->used_by_bfd)
    {
      struct _mips_elf_section_data *sdata;
      bfd_size_type amt = sizeof (*sdata);

      sdata = bfd_zalloc (abfd, amt);
      if (sdata == NULL)
	return FALSE;
      sec->used_by_bfd = sdata;
    }

  return _bfd_elf_new_section_hook (abfd, sec);
}

/* Read ECOFF debugging information from a .mdebug section into a
   ecoff_debug_info structure.  */

bfd_boolean
_bfd_mips_elf_read_ecoff_info (bfd *abfd, asection *section,
			       struct ecoff_debug_info *debug)
{
  HDRR *symhdr;
  const struct ecoff_debug_swap *swap;
  char *ext_hdr;

  swap = get_elf_backend_data (abfd)->elf_backend_ecoff_debug_swap;
  memset (debug, 0, sizeof (*debug));

  ext_hdr = bfd_malloc (swap->external_hdr_size);
  if (ext_hdr == NULL && swap->external_hdr_size != 0)
    goto error_return;

  if (! bfd_get_section_contents (abfd, section, ext_hdr, 0,
				  swap->external_hdr_size))
    goto error_return;

  symhdr = &debug->symbolic_header;
  (*swap->swap_hdr_in) (abfd, ext_hdr, symhdr);

  /* The symbolic header contains absolute file offsets and sizes to
     read.  */
#define READ(ptr, offset, count, size, type)				\
  if (symhdr->count == 0)						\
    debug->ptr = NULL;							\
  else									\
    {									\
      bfd_size_type amt = (bfd_size_type) size * symhdr->count;		\
      debug->ptr = bfd_malloc (amt);					\
      if (debug->ptr == NULL)						\
	goto error_return;						\
      if (bfd_seek (abfd, symhdr->offset, SEEK_SET) != 0		\
	  || bfd_bread (debug->ptr, amt, abfd) != amt)			\
	goto error_return;						\
    }

  READ (line, cbLineOffset, cbLine, sizeof (unsigned char), unsigned char *);
  READ (external_dnr, cbDnOffset, idnMax, swap->external_dnr_size, void *);
  READ (external_pdr, cbPdOffset, ipdMax, swap->external_pdr_size, void *);
  READ (external_sym, cbSymOffset, isymMax, swap->external_sym_size, void *);
  READ (external_opt, cbOptOffset, ioptMax, swap->external_opt_size, void *);
  READ (external_aux, cbAuxOffset, iauxMax, sizeof (union aux_ext),
	union aux_ext *);
  READ (ss, cbSsOffset, issMax, sizeof (char), char *);
  READ (ssext, cbSsExtOffset, issExtMax, sizeof (char), char *);
  READ (external_fdr, cbFdOffset, ifdMax, swap->external_fdr_size, void *);
  READ (external_rfd, cbRfdOffset, crfd, swap->external_rfd_size, void *);
  READ (external_ext, cbExtOffset, iextMax, swap->external_ext_size, void *);
#undef READ

  debug->fdr = NULL;

  return TRUE;

 error_return:
  if (ext_hdr != NULL)
    free (ext_hdr);
  if (debug->line != NULL)
    free (debug->line);
  if (debug->external_dnr != NULL)
    free (debug->external_dnr);
  if (debug->external_pdr != NULL)
    free (debug->external_pdr);
  if (debug->external_sym != NULL)
    free (debug->external_sym);
  if (debug->external_opt != NULL)
    free (debug->external_opt);
  if (debug->external_aux != NULL)
    free (debug->external_aux);
  if (debug->ss != NULL)
    free (debug->ss);
  if (debug->ssext != NULL)
    free (debug->ssext);
  if (debug->external_fdr != NULL)
    free (debug->external_fdr);
  if (debug->external_rfd != NULL)
    free (debug->external_rfd);
  if (debug->external_ext != NULL)
    free (debug->external_ext);
  return FALSE;
}

/* Swap RPDR (runtime procedure table entry) for output.  */

static void
ecoff_swap_rpdr_out (bfd *abfd, const RPDR *in, struct rpdr_ext *ex)
{
  H_PUT_S32 (abfd, in->adr, ex->p_adr);
  H_PUT_32 (abfd, in->regmask, ex->p_regmask);
  H_PUT_32 (abfd, in->regoffset, ex->p_regoffset);
  H_PUT_32 (abfd, in->fregmask, ex->p_fregmask);
  H_PUT_32 (abfd, in->fregoffset, ex->p_fregoffset);
  H_PUT_32 (abfd, in->frameoffset, ex->p_frameoffset);

  H_PUT_16 (abfd, in->framereg, ex->p_framereg);
  H_PUT_16 (abfd, in->pcreg, ex->p_pcreg);

  H_PUT_32 (abfd, in->irpss, ex->p_irpss);
}

/* Create a runtime procedure table from the .mdebug section.  */

static bfd_boolean
mips_elf_create_procedure_table (void *handle, bfd *abfd,
				 struct bfd_link_info *info, asection *s,
				 struct ecoff_debug_info *debug)
{
  const struct ecoff_debug_swap *swap;
  HDRR *hdr = &debug->symbolic_header;
  RPDR *rpdr, *rp;
  struct rpdr_ext *erp;
  void *rtproc;
  struct pdr_ext *epdr;
  struct sym_ext *esym;
  char *ss, **sv;
  char *str;
  bfd_size_type size;
  bfd_size_type count;
  unsigned long sindex;
  unsigned long i;
  PDR pdr;
  SYMR sym;
  const char *no_name_func = _("static procedure (no name)");

  epdr = NULL;
  rpdr = NULL;
  esym = NULL;
  ss = NULL;
  sv = NULL;

  swap = get_elf_backend_data (abfd)->elf_backend_ecoff_debug_swap;

  sindex = strlen (no_name_func) + 1;
  count = hdr->ipdMax;
  if (count > 0)
    {
      size = swap->external_pdr_size;

      epdr = bfd_malloc (size * count);
      if (epdr == NULL)
	goto error_return;

      if (! _bfd_ecoff_get_accumulated_pdr (handle, (bfd_byte *) epdr))
	goto error_return;

      size = sizeof (RPDR);
      rp = rpdr = bfd_malloc (size * count);
      if (rpdr == NULL)
	goto error_return;

      size = sizeof (char *);
      sv = bfd_malloc (size * count);
      if (sv == NULL)
	goto error_return;

      count = hdr->isymMax;
      size = swap->external_sym_size;
      esym = bfd_malloc (size * count);
      if (esym == NULL)
	goto error_return;

      if (! _bfd_ecoff_get_accumulated_sym (handle, (bfd_byte *) esym))
	goto error_return;

      count = hdr->issMax;
      ss = bfd_malloc (count);
      if (ss == NULL)
	goto error_return;
      if (! _bfd_ecoff_get_accumulated_ss (handle, (bfd_byte *) ss))
	goto error_return;

      count = hdr->ipdMax;
      for (i = 0; i < (unsigned long) count; i++, rp++)
	{
	  (*swap->swap_pdr_in) (abfd, epdr + i, &pdr);
	  (*swap->swap_sym_in) (abfd, &esym[pdr.isym], &sym);
	  rp->adr = sym.value;
	  rp->regmask = pdr.regmask;
	  rp->regoffset = pdr.regoffset;
	  rp->fregmask = pdr.fregmask;
	  rp->fregoffset = pdr.fregoffset;
	  rp->frameoffset = pdr.frameoffset;
	  rp->framereg = pdr.framereg;
	  rp->pcreg = pdr.pcreg;
	  rp->irpss = sindex;
	  sv[i] = ss + sym.iss;
	  sindex += strlen (sv[i]) + 1;
	}
    }

  size = sizeof (struct rpdr_ext) * (count + 2) + sindex;
  size = BFD_ALIGN (size, 16);
  rtproc = bfd_alloc (abfd, size);
  if (rtproc == NULL)
    {
      mips_elf_hash_table (info)->procedure_count = 0;
      goto error_return;
    }

  mips_elf_hash_table (info)->procedure_count = count + 2;

  erp = rtproc;
  memset (erp, 0, sizeof (struct rpdr_ext));
  erp++;
  str = (char *) rtproc + sizeof (struct rpdr_ext) * (count + 2);
  strcpy (str, no_name_func);
  str += strlen (no_name_func) + 1;
  for (i = 0; i < count; i++)
    {
      ecoff_swap_rpdr_out (abfd, rpdr + i, erp + i);
      strcpy (str, sv[i]);
      str += strlen (sv[i]) + 1;
    }
  H_PUT_S32 (abfd, -1, (erp + count)->p_adr);

  /* Set the size and contents of .rtproc section.  */
  s->size = size;
  s->contents = rtproc;

  /* Skip this section later on (I don't think this currently
     matters, but someday it might).  */
  s->map_head.link_order = NULL;

  if (epdr != NULL)
    free (epdr);
  if (rpdr != NULL)
    free (rpdr);
  if (esym != NULL)
    free (esym);
  if (ss != NULL)
    free (ss);
  if (sv != NULL)
    free (sv);

  return TRUE;

 error_return:
  if (epdr != NULL)
    free (epdr);
  if (rpdr != NULL)
    free (rpdr);
  if (esym != NULL)
    free (esym);
  if (ss != NULL)
    free (ss);
  if (sv != NULL)
    free (sv);
  return FALSE;
}

/* Check the mips16 stubs for a particular symbol, and see if we can
   discard them.  */

static bfd_boolean
mips_elf_check_mips16_stubs (struct mips_elf_link_hash_entry *h,
			     void *data ATTRIBUTE_UNUSED)
{
  if (h->root.root.type == bfd_link_hash_warning)
    h = (struct mips_elf_link_hash_entry *) h->root.root.u.i.link;

  if (h->fn_stub != NULL
      && ! h->need_fn_stub)
    {
      /* We don't need the fn_stub; the only references to this symbol
         are 16 bit calls.  Clobber the size to 0 to prevent it from
         being included in the link.  */
      h->fn_stub->size = 0;
      h->fn_stub->flags &= ~SEC_RELOC;
      h->fn_stub->reloc_count = 0;
      h->fn_stub->flags |= SEC_EXCLUDE;
    }

  if (h->call_stub != NULL
      && h->root.other == STO_MIPS16)
    {
      /* We don't need the call_stub; this is a 16 bit function, so
         calls from other 16 bit functions are OK.  Clobber the size
         to 0 to prevent it from being included in the link.  */
      h->call_stub->size = 0;
      h->call_stub->flags &= ~SEC_RELOC;
      h->call_stub->reloc_count = 0;
      h->call_stub->flags |= SEC_EXCLUDE;
    }

  if (h->call_fp_stub != NULL
      && h->root.other == STO_MIPS16)
    {
      /* We don't need the call_stub; this is a 16 bit function, so
         calls from other 16 bit functions are OK.  Clobber the size
         to 0 to prevent it from being included in the link.  */
      h->call_fp_stub->size = 0;
      h->call_fp_stub->flags &= ~SEC_RELOC;
      h->call_fp_stub->reloc_count = 0;
      h->call_fp_stub->flags |= SEC_EXCLUDE;
    }

  return TRUE;
}

/* R_MIPS16_26 is used for the mips16 jal and jalx instructions.
   Most mips16 instructions are 16 bits, but these instructions
   are 32 bits.

   The format of these instructions is:

   +--------------+--------------------------------+
   |     JALX     | X|   Imm 20:16  |   Imm 25:21  |
   +--------------+--------------------------------+
   |                Immediate  15:0                |
   +-----------------------------------------------+

   JALX is the 5-bit value 00011.  X is 0 for jal, 1 for jalx.
   Note that the immediate value in the first word is swapped.

   When producing a relocatable object file, R_MIPS16_26 is
   handled mostly like R_MIPS_26.  In particular, the addend is
   stored as a straight 26-bit value in a 32-bit instruction.
   (gas makes life simpler for itself by never adjusting a
   R_MIPS16_26 reloc to be against a section, so the addend is
   always zero).  However, the 32 bit instruction is stored as 2
   16-bit values, rather than a single 32-bit value.  In a
   big-endian file, the result is the same; in a little-endian
   file, the two 16-bit halves of the 32 bit value are swapped.
   This is so that a disassembler can recognize the jal
   instruction.

   When doing a final link, R_MIPS16_26 is treated as a 32 bit
   instruction stored as two 16-bit values.  The addend A is the
   contents of the targ26 field.  The calculation is the same as
   R_MIPS_26.  When storing the calculated value, reorder the
   immediate value as shown above, and don't forget to store the
   value as two 16-bit values.

   To put it in MIPS ABI terms, the relocation field is T-targ26-16,
   defined as

   big-endian:
   +--------+----------------------+
   |        |                      |
   |        |    targ26-16         |
   |31    26|25                   0|
   +--------+----------------------+

   little-endian:
   +----------+------+-------------+
   |          |      |             |
   |  sub1    |      |     sub2    |
   |0        9|10  15|16         31|
   +----------+--------------------+
   where targ26-16 is sub1 followed by sub2 (i.e., the addend field A is
   ((sub1 << 16) | sub2)).

   When producing a relocatable object file, the calculation is
   (((A < 2) | ((P + 4) & 0xf0000000) + S) >> 2)
   When producing a fully linked file, the calculation is
   let R = (((A < 2) | ((P + 4) & 0xf0000000) + S) >> 2)
   ((R & 0x1f0000) << 5) | ((R & 0x3e00000) >> 5) | (R & 0xffff)

   R_MIPS16_GPREL is used for GP-relative addressing in mips16
   mode.  A typical instruction will have a format like this:

   +--------------+--------------------------------+
   |    EXTEND    |     Imm 10:5    |   Imm 15:11  |
   +--------------+--------------------------------+
   |    Major     |   rx   |   ry   |   Imm  4:0   |
   +--------------+--------------------------------+

   EXTEND is the five bit value 11110.  Major is the instruction
   opcode.

   This is handled exactly like R_MIPS_GPREL16, except that the
   addend is retrieved and stored as shown in this diagram; that
   is, the Imm fields above replace the V-rel16 field.

   All we need to do here is shuffle the bits appropriately.  As
   above, the two 16-bit halves must be swapped on a
   little-endian system.

   R_MIPS16_HI16 and R_MIPS16_LO16 are used in mips16 mode to
   access data when neither GP-relative nor PC-relative addressing
   can be used.  They are handled like R_MIPS_HI16 and R_MIPS_LO16,
   except that the addend is retrieved and stored as shown above
   for R_MIPS16_GPREL.
  */
void
_bfd_mips16_elf_reloc_unshuffle (bfd *abfd, int r_type,
				 bfd_boolean jal_shuffle, bfd_byte *data)
{
  bfd_vma extend, insn, val;

  if (r_type != R_MIPS16_26 && r_type != R_MIPS16_GPREL
      && r_type != R_MIPS16_HI16 && r_type != R_MIPS16_LO16)
    return;

  /* Pick up the mips16 extend instruction and the real instruction.  */
  extend = bfd_get_16 (abfd, data);
  insn = bfd_get_16 (abfd, data + 2);
  if (r_type == R_MIPS16_26)
    {
      if (jal_shuffle)
	val = ((extend & 0xfc00) << 16) | ((extend & 0x3e0) << 11)
	      | ((extend & 0x1f) << 21) | insn;
      else
	val = extend << 16 | insn;
    }
  else
    val = ((extend & 0xf800) << 16) | ((insn & 0xffe0) << 11)
	  | ((extend & 0x1f) << 11) | (extend & 0x7e0) | (insn & 0x1f);
  bfd_put_32 (abfd, val, data);
}

void
_bfd_mips16_elf_reloc_shuffle (bfd *abfd, int r_type,
			       bfd_boolean jal_shuffle, bfd_byte *data)
{
  bfd_vma extend, insn, val;

  if (r_type != R_MIPS16_26 && r_type != R_MIPS16_GPREL
      && r_type != R_MIPS16_HI16 && r_type != R_MIPS16_LO16)
    return;

  val = bfd_get_32 (abfd, data);
  if (r_type == R_MIPS16_26)
    {
      if (jal_shuffle)
	{
	  insn = val & 0xffff;
	  extend = ((val >> 16) & 0xfc00) | ((val >> 11) & 0x3e0)
		   | ((val >> 21) & 0x1f);
	}
      else
	{
	  insn = val & 0xffff;
	  extend = val >> 16;
	}
    }
  else
    {
      insn = ((val >> 11) & 0xffe0) | (val & 0x1f);
      extend = ((val >> 16) & 0xf800) | ((val >> 11) & 0x1f) | (val & 0x7e0);
    }
  bfd_put_16 (abfd, insn, data + 2);
  bfd_put_16 (abfd, extend, data);
}

bfd_reloc_status_type
_bfd_mips_elf_gprel16_with_gp (bfd *abfd, asymbol *symbol,
			       arelent *reloc_entry, asection *input_section,
			       bfd_boolean relocatable, void *data, bfd_vma gp)
{
  bfd_vma relocation;
  bfd_signed_vma val;
  bfd_reloc_status_type status;

  if (bfd_is_com_section (symbol->section))
    relocation = 0;
  else
    relocation = symbol->value;

  relocation += symbol->section->output_section->vma;
  relocation += symbol->section->output_offset;

  if (reloc_entry->address > bfd_get_section_limit (abfd, input_section))
    return bfd_reloc_outofrange;

  /* Set val to the offset into the section or symbol.  */
  val = reloc_entry->addend;

  _bfd_mips_elf_sign_extend (val, 16);

  /* Adjust val for the final section location and GP value.  If we
     are producing relocatable output, we don't want to do this for
     an external symbol.  */
  if (! relocatable
      || (symbol->flags & BSF_SECTION_SYM) != 0)
    val += relocation - gp;

  if (reloc_entry->howto->partial_inplace)
    {
      status = _bfd_relocate_contents (reloc_entry->howto, abfd, val,
				       (bfd_byte *) data
				       + reloc_entry->address);
      if (status != bfd_reloc_ok)
	return status;
    }
  else
    reloc_entry->addend = val;

  if (relocatable)
    reloc_entry->address += input_section->output_offset;

  return bfd_reloc_ok;
}

/* Used to store a REL high-part relocation such as R_MIPS_HI16 or
   R_MIPS_GOT16.  REL is the relocation, INPUT_SECTION is the section
   that contains the relocation field and DATA points to the start of
   INPUT_SECTION.  */

struct mips_hi16
{
  struct mips_hi16 *next;
  bfd_byte *data;
  asection *input_section;
  arelent rel;
};

/* FIXME: This should not be a static variable.  */

static struct mips_hi16 *mips_hi16_list;

/* A howto special_function for REL *HI16 relocations.  We can only
   calculate the correct value once we've seen the partnering
   *LO16 relocation, so just save the information for later.

   The ABI requires that the *LO16 immediately follow the *HI16.
   However, as a GNU extension, we permit an arbitrary number of
   *HI16s to be associated with a single *LO16.  This significantly
   simplies the relocation handling in gcc.  */

bfd_reloc_status_type
_bfd_mips_elf_hi16_reloc (bfd *abfd ATTRIBUTE_UNUSED, arelent *reloc_entry,
			  asymbol *symbol ATTRIBUTE_UNUSED, void *data,
			  asection *input_section, bfd *output_bfd,
			  char **error_message ATTRIBUTE_UNUSED)
{
  struct mips_hi16 *n;

  if (reloc_entry->address > bfd_get_section_limit (abfd, input_section))
    return bfd_reloc_outofrange;

  n = bfd_malloc (sizeof *n);
  if (n == NULL)
    return bfd_reloc_outofrange;

  n->next = mips_hi16_list;
  n->data = data;
  n->input_section = input_section;
  n->rel = *reloc_entry;
  mips_hi16_list = n;

  if (output_bfd != NULL)
    reloc_entry->address += input_section->output_offset;

  return bfd_reloc_ok;
}

/* A howto special_function for REL R_MIPS_GOT16 relocations.  This is just
   like any other 16-bit relocation when applied to global symbols, but is
   treated in the same as R_MIPS_HI16 when applied to local symbols.  */

bfd_reloc_status_type
_bfd_mips_elf_got16_reloc (bfd *abfd, arelent *reloc_entry, asymbol *symbol,
			   void *data, asection *input_section,
			   bfd *output_bfd, char **error_message)
{
  if ((symbol->flags & (BSF_GLOBAL | BSF_WEAK)) != 0
      || bfd_is_und_section (bfd_get_section (symbol))
      || bfd_is_com_section (bfd_get_section (symbol)))
    /* The relocation is against a global symbol.  */
    return _bfd_mips_elf_generic_reloc (abfd, reloc_entry, symbol, data,
					input_section, output_bfd,
					error_message);

  return _bfd_mips_elf_hi16_reloc (abfd, reloc_entry, symbol, data,
				   input_section, output_bfd, error_message);
}

/* A howto special_function for REL *LO16 relocations.  The *LO16 itself
   is a straightforward 16 bit inplace relocation, but we must deal with
   any partnering high-part relocations as well.  */

bfd_reloc_status_type
_bfd_mips_elf_lo16_reloc (bfd *abfd, arelent *reloc_entry, asymbol *symbol,
			  void *data, asection *input_section,
			  bfd *output_bfd, char **error_message)
{
  bfd_vma vallo;
  bfd_byte *location = (bfd_byte *) data + reloc_entry->address;

  if (reloc_entry->address > bfd_get_section_limit (abfd, input_section))
    return bfd_reloc_outofrange;

  _bfd_mips16_elf_reloc_unshuffle (abfd, reloc_entry->howto->type, FALSE,
				   location);
  vallo = bfd_get_32 (abfd, location);
  _bfd_mips16_elf_reloc_shuffle (abfd, reloc_entry->howto->type, FALSE,
				 location);

  while (mips_hi16_list != NULL)
    {
      bfd_reloc_status_type ret;
      struct mips_hi16 *hi;

      hi = mips_hi16_list;

      /* R_MIPS_GOT16 relocations are something of a special case.  We
	 want to install the addend in the same way as for a R_MIPS_HI16
	 relocation (with a rightshift of 16).  However, since GOT16
	 relocations can also be used with global symbols, their howto
	 has a rightshift of 0.  */
      if (hi->rel.howto->type == R_MIPS_GOT16)
	hi->rel.howto = MIPS_ELF_RTYPE_TO_HOWTO (abfd, R_MIPS_HI16, FALSE);

      /* VALLO is a signed 16-bit number.  Bias it by 0x8000 so that any
	 carry or borrow will induce a change of +1 or -1 in the high part.  */
      hi->rel.addend += (vallo + 0x8000) & 0xffff;

      ret = _bfd_mips_elf_generic_reloc (abfd, &hi->rel, symbol, hi->data,
					 hi->input_section, output_bfd,
					 error_message);
      if (ret != bfd_reloc_ok)
	return ret;

      mips_hi16_list = hi->next;
      free (hi);
    }

  return _bfd_mips_elf_generic_reloc (abfd, reloc_entry, symbol, data,
				      input_section, output_bfd,
				      error_message);
}

/* A generic howto special_function.  This calculates and installs the
   relocation itself, thus avoiding the oft-discussed problems in
   bfd_perform_relocation and bfd_install_relocation.  */

bfd_reloc_status_type
_bfd_mips_elf_generic_reloc (bfd *abfd ATTRIBUTE_UNUSED, arelent *reloc_entry,
			     asymbol *symbol, void *data ATTRIBUTE_UNUSED,
			     asection *input_section, bfd *output_bfd,
			     char **error_message ATTRIBUTE_UNUSED)
{
  bfd_signed_vma val;
  bfd_reloc_status_type status;
  bfd_boolean relocatable;

  relocatable = (output_bfd != NULL);

  if (reloc_entry->address > bfd_get_section_limit (abfd, input_section))
    return bfd_reloc_outofrange;

  /* Build up the field adjustment in VAL.  */
  val = 0;
  if (!relocatable || (symbol->flags & BSF_SECTION_SYM) != 0)
    {
      /* Either we're calculating the final field value or we have a
	 relocation against a section symbol.  Add in the section's
	 offset or address.  */
      val += symbol->section->output_section->vma;
      val += symbol->section->output_offset;
    }

  if (!relocatable)
    {
      /* We're calculating the final field value.  Add in the symbol's value
	 and, if pc-relative, subtract the address of the field itself.  */
      val += symbol->value;
      if (reloc_entry->howto->pc_relative)
	{
	  val -= input_section->output_section->vma;
	  val -= input_section->output_offset;
	  val -= reloc_entry->address;
	}
    }

  /* VAL is now the final adjustment.  If we're keeping this relocation
     in the output file, and if the relocation uses a separate addend,
     we just need to add VAL to that addend.  Otherwise we need to add
     VAL to the relocation field itself.  */
  if (relocatable && !reloc_entry->howto->partial_inplace)
    reloc_entry->addend += val;
  else
    {
      bfd_byte *location = (bfd_byte *) data + reloc_entry->address;

      /* Add in the separate addend, if any.  */
      val += reloc_entry->addend;

      /* Add VAL to the relocation field.  */
      _bfd_mips16_elf_reloc_unshuffle (abfd, reloc_entry->howto->type, FALSE,
				       location);
      status = _bfd_relocate_contents (reloc_entry->howto, abfd, val,
				       location);
      _bfd_mips16_elf_reloc_shuffle (abfd, reloc_entry->howto->type, FALSE,
				     location);

      if (status != bfd_reloc_ok)
	return status;
    }

  if (relocatable)
    reloc_entry->address += input_section->output_offset;

  return bfd_reloc_ok;
}

/* Swap an entry in a .gptab section.  Note that these routines rely
   on the equivalence of the two elements of the union.  */

static void
bfd_mips_elf32_swap_gptab_in (bfd *abfd, const Elf32_External_gptab *ex,
			      Elf32_gptab *in)
{
  in->gt_entry.gt_g_value = H_GET_32 (abfd, ex->gt_entry.gt_g_value);
  in->gt_entry.gt_bytes = H_GET_32 (abfd, ex->gt_entry.gt_bytes);
}

static void
bfd_mips_elf32_swap_gptab_out (bfd *abfd, const Elf32_gptab *in,
			       Elf32_External_gptab *ex)
{
  H_PUT_32 (abfd, in->gt_entry.gt_g_value, ex->gt_entry.gt_g_value);
  H_PUT_32 (abfd, in->gt_entry.gt_bytes, ex->gt_entry.gt_bytes);
}

static void
bfd_elf32_swap_compact_rel_out (bfd *abfd, const Elf32_compact_rel *in,
				Elf32_External_compact_rel *ex)
{
  H_PUT_32 (abfd, in->id1, ex->id1);
  H_PUT_32 (abfd, in->num, ex->num);
  H_PUT_32 (abfd, in->id2, ex->id2);
  H_PUT_32 (abfd, in->offset, ex->offset);
  H_PUT_32 (abfd, in->reserved0, ex->reserved0);
  H_PUT_32 (abfd, in->reserved1, ex->reserved1);
}

static void
bfd_elf32_swap_crinfo_out (bfd *abfd, const Elf32_crinfo *in,
			   Elf32_External_crinfo *ex)
{
  unsigned long l;

  l = (((in->ctype & CRINFO_CTYPE) << CRINFO_CTYPE_SH)
       | ((in->rtype & CRINFO_RTYPE) << CRINFO_RTYPE_SH)
       | ((in->dist2to & CRINFO_DIST2TO) << CRINFO_DIST2TO_SH)
       | ((in->relvaddr & CRINFO_RELVADDR) << CRINFO_RELVADDR_SH));
  H_PUT_32 (abfd, l, ex->info);
  H_PUT_32 (abfd, in->konst, ex->konst);
  H_PUT_32 (abfd, in->vaddr, ex->vaddr);
}

/* A .reginfo section holds a single Elf32_RegInfo structure.  These
   routines swap this structure in and out.  They are used outside of
   BFD, so they are globally visible.  */

void
bfd_mips_elf32_swap_reginfo_in (bfd *abfd, const Elf32_External_RegInfo *ex,
				Elf32_RegInfo *in)
{
  in->ri_gprmask = H_GET_32 (abfd, ex->ri_gprmask);
  in->ri_cprmask[0] = H_GET_32 (abfd, ex->ri_cprmask[0]);
  in->ri_cprmask[1] = H_GET_32 (abfd, ex->ri_cprmask[1]);
  in->ri_cprmask[2] = H_GET_32 (abfd, ex->ri_cprmask[2]);
  in->ri_cprmask[3] = H_GET_32 (abfd, ex->ri_cprmask[3]);
  in->ri_gp_value = H_GET_32 (abfd, ex->ri_gp_value);
}

void
bfd_mips_elf32_swap_reginfo_out (bfd *abfd, const Elf32_RegInfo *in,
				 Elf32_External_RegInfo *ex)
{
  H_PUT_32 (abfd, in->ri_gprmask, ex->ri_gprmask);
  H_PUT_32 (abfd, in->ri_cprmask[0], ex->ri_cprmask[0]);
  H_PUT_32 (abfd, in->ri_cprmask[1], ex->ri_cprmask[1]);
  H_PUT_32 (abfd, in->ri_cprmask[2], ex->ri_cprmask[2]);
  H_PUT_32 (abfd, in->ri_cprmask[3], ex->ri_cprmask[3]);
  H_PUT_32 (abfd, in->ri_gp_value, ex->ri_gp_value);
}

/* In the 64 bit ABI, the .MIPS.options section holds register
   information in an Elf64_Reginfo structure.  These routines swap
   them in and out.  They are globally visible because they are used
   outside of BFD.  These routines are here so that gas can call them
   without worrying about whether the 64 bit ABI has been included.  */

void
bfd_mips_elf64_swap_reginfo_in (bfd *abfd, const Elf64_External_RegInfo *ex,
				Elf64_Internal_RegInfo *in)
{
  in->ri_gprmask = H_GET_32 (abfd, ex->ri_gprmask);
  in->ri_pad = H_GET_32 (abfd, ex->ri_pad);
  in->ri_cprmask[0] = H_GET_32 (abfd, ex->ri_cprmask[0]);
  in->ri_cprmask[1] = H_GET_32 (abfd, ex->ri_cprmask[1]);
  in->ri_cprmask[2] = H_GET_32 (abfd, ex->ri_cprmask[2]);
  in->ri_cprmask[3] = H_GET_32 (abfd, ex->ri_cprmask[3]);
  in->ri_gp_value = H_GET_64 (abfd, ex->ri_gp_value);
}

void
bfd_mips_elf64_swap_reginfo_out (bfd *abfd, const Elf64_Internal_RegInfo *in,
				 Elf64_External_RegInfo *ex)
{
  H_PUT_32 (abfd, in->ri_gprmask, ex->ri_gprmask);
  H_PUT_32 (abfd, in->ri_pad, ex->ri_pad);
  H_PUT_32 (abfd, in->ri_cprmask[0], ex->ri_cprmask[0]);
  H_PUT_32 (abfd, in->ri_cprmask[1], ex->ri_cprmask[1]);
  H_PUT_32 (abfd, in->ri_cprmask[2], ex->ri_cprmask[2]);
  H_PUT_32 (abfd, in->ri_cprmask[3], ex->ri_cprmask[3]);
  H_PUT_64 (abfd, in->ri_gp_value, ex->ri_gp_value);
}

/* Swap in an options header.  */

void
bfd_mips_elf_swap_options_in (bfd *abfd, const Elf_External_Options *ex,
			      Elf_Internal_Options *in)
{
  in->kind = H_GET_8 (abfd, ex->kind);
  in->size = H_GET_8 (abfd, ex->size);
  in->section = H_GET_16 (abfd, ex->section);
  in->info = H_GET_32 (abfd, ex->info);
}

/* Swap out an options header.  */

void
bfd_mips_elf_swap_options_out (bfd *abfd, const Elf_Internal_Options *in,
			       Elf_External_Options *ex)
{
  H_PUT_8 (abfd, in->kind, ex->kind);
  H_PUT_8 (abfd, in->size, ex->size);
  H_PUT_16 (abfd, in->section, ex->section);
  H_PUT_32 (abfd, in->info, ex->info);
}

/* This function is called via qsort() to sort the dynamic relocation
   entries by increasing r_symndx value.  */

static int
sort_dynamic_relocs (const void *arg1, const void *arg2)
{
  Elf_Internal_Rela int_reloc1;
  Elf_Internal_Rela int_reloc2;
  int diff;

  bfd_elf32_swap_reloc_in (reldyn_sorting_bfd, arg1, &int_reloc1);
  bfd_elf32_swap_reloc_in (reldyn_sorting_bfd, arg2, &int_reloc2);

  diff = ELF32_R_SYM (int_reloc1.r_info) - ELF32_R_SYM (int_reloc2.r_info);
  if (diff != 0)
    return diff;

  if (int_reloc1.r_offset < int_reloc2.r_offset)
    return -1;
  if (int_reloc1.r_offset > int_reloc2.r_offset)
    return 1;
  return 0;
}

/* Like sort_dynamic_relocs, but used for elf64 relocations.  */

static int
sort_dynamic_relocs_64 (const void *arg1 ATTRIBUTE_UNUSED,
			const void *arg2 ATTRIBUTE_UNUSED)
{
#ifdef BFD64
  Elf_Internal_Rela int_reloc1[3];
  Elf_Internal_Rela int_reloc2[3];

  (*get_elf_backend_data (reldyn_sorting_bfd)->s->swap_reloc_in)
    (reldyn_sorting_bfd, arg1, int_reloc1);
  (*get_elf_backend_data (reldyn_sorting_bfd)->s->swap_reloc_in)
    (reldyn_sorting_bfd, arg2, int_reloc2);

  if (ELF64_R_SYM (int_reloc1[0].r_info) < ELF64_R_SYM (int_reloc2[0].r_info))
    return -1;
  if (ELF64_R_SYM (int_reloc1[0].r_info) > ELF64_R_SYM (int_reloc2[0].r_info))
    return 1;

  if (int_reloc1[0].r_offset < int_reloc2[0].r_offset)
    return -1;
  if (int_reloc1[0].r_offset > int_reloc2[0].r_offset)
    return 1;
  return 0;
#else
  abort ();
#endif
}


/* This routine is used to write out ECOFF debugging external symbol
   information.  It is called via mips_elf_link_hash_traverse.  The
   ECOFF external symbol information must match the ELF external
   symbol information.  Unfortunately, at this point we don't know
   whether a symbol is required by reloc information, so the two
   tables may wind up being different.  We must sort out the external
   symbol information before we can set the final size of the .mdebug
   section, and we must set the size of the .mdebug section before we
   can relocate any sections, and we can't know which symbols are
   required by relocation until we relocate the sections.
   Fortunately, it is relatively unlikely that any symbol will be
   stripped but required by a reloc.  In particular, it can not happen
   when generating a final executable.  */

static bfd_boolean
mips_elf_output_extsym (struct mips_elf_link_hash_entry *h, void *data)
{
  struct extsym_info *einfo = data;
  bfd_boolean strip;
  asection *sec, *output_section;

  if (h->root.root.type == bfd_link_hash_warning)
    h = (struct mips_elf_link_hash_entry *) h->root.root.u.i.link;

  if (h->root.indx == -2)
    strip = FALSE;
  else if ((h->root.def_dynamic
	    || h->root.ref_dynamic
	    || h->root.type == bfd_link_hash_new)
	   && !h->root.def_regular
	   && !h->root.ref_regular)
    strip = TRUE;
  else if (einfo->info->strip == strip_all
	   || (einfo->info->strip == strip_some
	       && bfd_hash_lookup (einfo->info->keep_hash,
				   h->root.root.root.string,
				   FALSE, FALSE) == NULL))
    strip = TRUE;
  else
    strip = FALSE;

  if (strip)
    return TRUE;

  if (h->esym.ifd == -2)
    {
      h->esym.jmptbl = 0;
      h->esym.cobol_main = 0;
      h->esym.weakext = 0;
      h->esym.reserved = 0;
      h->esym.ifd = ifdNil;
      h->esym.asym.value = 0;
      h->esym.asym.st = stGlobal;

      if (h->root.root.type == bfd_link_hash_undefined
	  || h->root.root.type == bfd_link_hash_undefweak)
	{
	  const char *name;

	  /* Use undefined class.  Also, set class and type for some
             special symbols.  */
	  name = h->root.root.root.string;
	  if (strcmp (name, mips_elf_dynsym_rtproc_names[0]) == 0
	      || strcmp (name, mips_elf_dynsym_rtproc_names[1]) == 0)
	    {
	      h->esym.asym.sc = scData;
	      h->esym.asym.st = stLabel;
	      h->esym.asym.value = 0;
	    }
	  else if (strcmp (name, mips_elf_dynsym_rtproc_names[2]) == 0)
	    {
	      h->esym.asym.sc = scAbs;
	      h->esym.asym.st = stLabel;
	      h->esym.asym.value =
		mips_elf_hash_table (einfo->info)->procedure_count;
	    }
	  else if (strcmp (name, "_gp_disp") == 0 && ! NEWABI_P (einfo->abfd))
	    {
	      h->esym.asym.sc = scAbs;
	      h->esym.asym.st = stLabel;
	      h->esym.asym.value = elf_gp (einfo->abfd);
	    }
	  else
	    h->esym.asym.sc = scUndefined;
	}
      else if (h->root.root.type != bfd_link_hash_defined
	  && h->root.root.type != bfd_link_hash_defweak)
	h->esym.asym.sc = scAbs;
      else
	{
	  const char *name;

	  sec = h->root.root.u.def.section;
	  output_section = sec->output_section;

	  /* When making a shared library and symbol h is the one from
	     the another shared library, OUTPUT_SECTION may be null.  */
	  if (output_section == NULL)
	    h->esym.asym.sc = scUndefined;
	  else
	    {
	      name = bfd_section_name (output_section->owner, output_section);

	      if (strcmp (name, ".text") == 0)
		h->esym.asym.sc = scText;
	      else if (strcmp (name, ".data") == 0)
		h->esym.asym.sc = scData;
	      else if (strcmp (name, ".sdata") == 0)
		h->esym.asym.sc = scSData;
	      else if (strcmp (name, ".rodata") == 0
		       || strcmp (name, ".rdata") == 0)
		h->esym.asym.sc = scRData;
	      else if (strcmp (name, ".bss") == 0)
		h->esym.asym.sc = scBss;
	      else if (strcmp (name, ".sbss") == 0)
		h->esym.asym.sc = scSBss;
	      else if (strcmp (name, ".init") == 0)
		h->esym.asym.sc = scInit;
	      else if (strcmp (name, ".fini") == 0)
		h->esym.asym.sc = scFini;
	      else
		h->esym.asym.sc = scAbs;
	    }
	}

      h->esym.asym.reserved = 0;
      h->esym.asym.index = indexNil;
    }

  if (h->root.root.type == bfd_link_hash_common)
    h->esym.asym.value = h->root.root.u.c.size;
  else if (h->root.root.type == bfd_link_hash_defined
	   || h->root.root.type == bfd_link_hash_defweak)
    {
      if (h->esym.asym.sc == scCommon)
	h->esym.asym.sc = scBss;
      else if (h->esym.asym.sc == scSCommon)
	h->esym.asym.sc = scSBss;

      sec = h->root.root.u.def.section;
      output_section = sec->output_section;
      if (output_section != NULL)
	h->esym.asym.value = (h->root.root.u.def.value
			      + sec->output_offset
			      + output_section->vma);
      else
	h->esym.asym.value = 0;
    }
  else if (h->root.needs_plt)
    {
      struct mips_elf_link_hash_entry *hd = h;
      bfd_boolean no_fn_stub = h->no_fn_stub;

      while (hd->root.root.type == bfd_link_hash_indirect)
	{
	  hd = (struct mips_elf_link_hash_entry *)h->root.root.u.i.link;
	  no_fn_stub = no_fn_stub || hd->no_fn_stub;
	}

      if (!no_fn_stub)
	{
	  /* Set type and value for a symbol with a function stub.  */
	  h->esym.asym.st = stProc;
	  sec = hd->root.root.u.def.section;
	  if (sec == NULL)
	    h->esym.asym.value = 0;
	  else
	    {
	      output_section = sec->output_section;
	      if (output_section != NULL)
		h->esym.asym.value = (hd->root.plt.offset
				      + sec->output_offset
				      + output_section->vma);
	      else
		h->esym.asym.value = 0;
	    }
	}
    }

  if (! bfd_ecoff_debug_one_external (einfo->abfd, einfo->debug, einfo->swap,
				      h->root.root.root.string,
				      &h->esym))
    {
      einfo->failed = TRUE;
      return FALSE;
    }

  return TRUE;
}

/* A comparison routine used to sort .gptab entries.  */

static int
gptab_compare (const void *p1, const void *p2)
{
  const Elf32_gptab *a1 = p1;
  const Elf32_gptab *a2 = p2;

  return a1->gt_entry.gt_g_value - a2->gt_entry.gt_g_value;
}

/* Functions to manage the got entry hash table.  */

/* Use all 64 bits of a bfd_vma for the computation of a 32-bit
   hash number.  */

static INLINE hashval_t
mips_elf_hash_bfd_vma (bfd_vma addr)
{
#ifdef BFD64
  return addr + (addr >> 32);
#else
  return addr;
#endif
}

/* got_entries only match if they're identical, except for gotidx, so
   use all fields to compute the hash, and compare the appropriate
   union members.  */

static hashval_t
mips_elf_got_entry_hash (const void *entry_)
{
  const struct mips_got_entry *entry = (struct mips_got_entry *)entry_;

  return entry->symndx
    + ((entry->tls_type & GOT_TLS_LDM) << 17)
    + (! entry->abfd ? mips_elf_hash_bfd_vma (entry->d.address)
       : entry->abfd->id
         + (entry->symndx >= 0 ? mips_elf_hash_bfd_vma (entry->d.addend)
	    : entry->d.h->root.root.root.hash));
}

static int
mips_elf_got_entry_eq (const void *entry1, const void *entry2)
{
  const struct mips_got_entry *e1 = (struct mips_got_entry *)entry1;
  const struct mips_got_entry *e2 = (struct mips_got_entry *)entry2;

  /* An LDM entry can only match another LDM entry.  */
  if ((e1->tls_type ^ e2->tls_type) & GOT_TLS_LDM)
    return 0;

  return e1->abfd == e2->abfd && e1->symndx == e2->symndx
    && (! e1->abfd ? e1->d.address == e2->d.address
	: e1->symndx >= 0 ? e1->d.addend == e2->d.addend
	: e1->d.h == e2->d.h);
}

/* multi_got_entries are still a match in the case of global objects,
   even if the input bfd in which they're referenced differs, so the
   hash computation and compare functions are adjusted
   accordingly.  */

static hashval_t
mips_elf_multi_got_entry_hash (const void *entry_)
{
  const struct mips_got_entry *entry = (struct mips_got_entry *)entry_;

  return entry->symndx
    + (! entry->abfd
       ? mips_elf_hash_bfd_vma (entry->d.address)
       : entry->symndx >= 0
       ? ((entry->tls_type & GOT_TLS_LDM)
	  ? (GOT_TLS_LDM << 17)
	  : (entry->abfd->id
	     + mips_elf_hash_bfd_vma (entry->d.addend)))
       : entry->d.h->root.root.root.hash);
}

static int
mips_elf_multi_got_entry_eq (const void *entry1, const void *entry2)
{
  const struct mips_got_entry *e1 = (struct mips_got_entry *)entry1;
  const struct mips_got_entry *e2 = (struct mips_got_entry *)entry2;

  /* Any two LDM entries match.  */
  if (e1->tls_type & e2->tls_type & GOT_TLS_LDM)
    return 1;

  /* Nothing else matches an LDM entry.  */
  if ((e1->tls_type ^ e2->tls_type) & GOT_TLS_LDM)
    return 0;

  return e1->symndx == e2->symndx
    && (e1->symndx >= 0 ? e1->abfd == e2->abfd && e1->d.addend == e2->d.addend
	: e1->abfd == NULL || e2->abfd == NULL
	? e1->abfd == e2->abfd && e1->d.address == e2->d.address
	: e1->d.h == e2->d.h);
}

/* Return the dynamic relocation section.  If it doesn't exist, try to
   create a new it if CREATE_P, otherwise return NULL.  Also return NULL
   if creation fails.  */

static asection *
mips_elf_rel_dyn_section (struct bfd_link_info *info, bfd_boolean create_p)
{
  const char *dname;
  asection *sreloc;
  bfd *dynobj;

  dname = MIPS_ELF_REL_DYN_NAME (info);
  dynobj = elf_hash_table (info)->dynobj;
  sreloc = bfd_get_section_by_name (dynobj, dname);
  if (sreloc == NULL && create_p)
    {
      sreloc = bfd_make_section_with_flags (dynobj, dname,
					    (SEC_ALLOC
					     | SEC_LOAD
					     | SEC_HAS_CONTENTS
					     | SEC_IN_MEMORY
					     | SEC_LINKER_CREATED
					     | SEC_READONLY));
      if (sreloc == NULL
	  || ! bfd_set_section_alignment (dynobj, sreloc,
					  MIPS_ELF_LOG_FILE_ALIGN (dynobj)))
	return NULL;
    }
  return sreloc;
}

/* Returns the GOT section for ABFD.  */

static asection *
mips_elf_got_section (bfd *abfd, bfd_boolean maybe_excluded)
{
  asection *sgot = bfd_get_section_by_name (abfd, ".got");
  if (sgot == NULL
      || (! maybe_excluded && (sgot->flags & SEC_EXCLUDE) != 0))
    return NULL;
  return sgot;
}

/* Returns the GOT information associated with the link indicated by
   INFO.  If SGOTP is non-NULL, it is filled in with the GOT
   section.  */

static struct mips_got_info *
mips_elf_got_info (bfd *abfd, asection **sgotp)
{
  asection *sgot;
  struct mips_got_info *g;

  sgot = mips_elf_got_section (abfd, TRUE);
  BFD_ASSERT (sgot != NULL);
  BFD_ASSERT (mips_elf_section_data (sgot) != NULL);
  g = mips_elf_section_data (sgot)->u.got_info;
  BFD_ASSERT (g != NULL);

  if (sgotp)
    *sgotp = (sgot->flags & SEC_EXCLUDE) == 0 ? sgot : NULL;

  return g;
}

/* Count the number of relocations needed for a TLS GOT entry, with
   access types from TLS_TYPE, and symbol H (or a local symbol if H
   is NULL).  */

static int
mips_tls_got_relocs (struct bfd_link_info *info, unsigned char tls_type,
		     struct elf_link_hash_entry *h)
{
  int indx = 0;
  int ret = 0;
  bfd_boolean need_relocs = FALSE;
  bfd_boolean dyn = elf_hash_table (info)->dynamic_sections_created;

  if (h && WILL_CALL_FINISH_DYNAMIC_SYMBOL (dyn, info->shared, h)
      && (!info->shared || !SYMBOL_REFERENCES_LOCAL (info, h)))
    indx = h->dynindx;

  if ((info->shared || indx != 0)
      && (h == NULL
	  || ELF_ST_VISIBILITY (h->other) == STV_DEFAULT
	  || h->root.type != bfd_link_hash_undefweak))
    need_relocs = TRUE;

  if (!need_relocs)
    return FALSE;

  if (tls_type & GOT_TLS_GD)
    {
      ret++;
      if (indx != 0)
	ret++;
    }

  if (tls_type & GOT_TLS_IE)
    ret++;

  if ((tls_type & GOT_TLS_LDM) && info->shared)
    ret++;

  return ret;
}

/* Count the number of TLS relocations required for the GOT entry in
   ARG1, if it describes a local symbol.  */

static int
mips_elf_count_local_tls_relocs (void **arg1, void *arg2)
{
  struct mips_got_entry *entry = * (struct mips_got_entry **) arg1;
  struct mips_elf_count_tls_arg *arg = arg2;

  if (entry->abfd != NULL && entry->symndx != -1)
    arg->needed += mips_tls_got_relocs (arg->info, entry->tls_type, NULL);

  return 1;
}

/* Count the number of TLS GOT entries required for the global (or
   forced-local) symbol in ARG1.  */

static int
mips_elf_count_global_tls_entries (void *arg1, void *arg2)
{
  struct mips_elf_link_hash_entry *hm
    = (struct mips_elf_link_hash_entry *) arg1;
  struct mips_elf_count_tls_arg *arg = arg2;

  if (hm->tls_type & GOT_TLS_GD)
    arg->needed += 2;
  if (hm->tls_type & GOT_TLS_IE)
    arg->needed += 1;

  return 1;
}

/* Count the number of TLS relocations required for the global (or
   forced-local) symbol in ARG1.  */

static int
mips_elf_count_global_tls_relocs (void *arg1, void *arg2)
{
  struct mips_elf_link_hash_entry *hm
    = (struct mips_elf_link_hash_entry *) arg1;
  struct mips_elf_count_tls_arg *arg = arg2;

  arg->needed += mips_tls_got_relocs (arg->info, hm->tls_type, &hm->root);

  return 1;
}

/* Output a simple dynamic relocation into SRELOC.  */

static void
mips_elf_output_dynamic_relocation (bfd *output_bfd,
				    asection *sreloc,
				    unsigned long indx,
				    int r_type,
				    bfd_vma offset)
{
  Elf_Internal_Rela rel[3];

  memset (rel, 0, sizeof (rel));

  rel[0].r_info = ELF_R_INFO (output_bfd, indx, r_type);
  rel[0].r_offset = rel[1].r_offset = rel[2].r_offset = offset;

  if (ABI_64_P (output_bfd))
    {
      (*get_elf_backend_data (output_bfd)->s->swap_reloc_out)
	(output_bfd, &rel[0],
	 (sreloc->contents
	  + sreloc->reloc_count * sizeof (Elf64_Mips_External_Rel)));
    }
  else
    bfd_elf32_swap_reloc_out
      (output_bfd, &rel[0],
       (sreloc->contents
	+ sreloc->reloc_count * sizeof (Elf32_External_Rel)));
  ++sreloc->reloc_count;
}

/* Initialize a set of TLS GOT entries for one symbol.  */

static void
mips_elf_initialize_tls_slots (bfd *abfd, bfd_vma got_offset,
			       unsigned char *tls_type_p,
			       struct bfd_link_info *info,
			       struct mips_elf_link_hash_entry *h,
			       bfd_vma value)
{
  int indx;
  asection *sreloc, *sgot;
  bfd_vma offset, offset2;
  bfd *dynobj;
  bfd_boolean need_relocs = FALSE;

  dynobj = elf_hash_table (info)->dynobj;
  sgot = mips_elf_got_section (dynobj, FALSE);

  indx = 0;
  if (h != NULL)
    {
      bfd_boolean dyn = elf_hash_table (info)->dynamic_sections_created;

      if (WILL_CALL_FINISH_DYNAMIC_SYMBOL (dyn, info->shared, &h->root)
	  && (!info->shared || !SYMBOL_REFERENCES_LOCAL (info, &h->root)))
	indx = h->root.dynindx;
    }

  if (*tls_type_p & GOT_TLS_DONE)
    return;

  if ((info->shared || indx != 0)
      && (h == NULL
	  || ELF_ST_VISIBILITY (h->root.other) == STV_DEFAULT
	  || h->root.type != bfd_link_hash_undefweak))
    need_relocs = TRUE;

  /* MINUS_ONE means the symbol is not defined in this object.  It may not
     be defined at all; assume that the value doesn't matter in that
     case.  Otherwise complain if we would use the value.  */
  BFD_ASSERT (value != MINUS_ONE || (indx != 0 && need_relocs)
	      || h->root.root.type == bfd_link_hash_undefweak);

  /* Emit necessary relocations.  */
  sreloc = mips_elf_rel_dyn_section (info, FALSE);

  /* General Dynamic.  */
  if (*tls_type_p & GOT_TLS_GD)
    {
      offset = got_offset;
      offset2 = offset + MIPS_ELF_GOT_SIZE (abfd);

      if (need_relocs)
	{
	  mips_elf_output_dynamic_relocation
	    (abfd, sreloc, indx,
	     ABI_64_P (abfd) ? R_MIPS_TLS_DTPMOD64 : R_MIPS_TLS_DTPMOD32,
	     sgot->output_offset + sgot->output_section->vma + offset);

	  if (indx)
	    mips_elf_output_dynamic_relocation
	      (abfd, sreloc, indx,
	       ABI_64_P (abfd) ? R_MIPS_TLS_DTPREL64 : R_MIPS_TLS_DTPREL32,
	       sgot->output_offset + sgot->output_section->vma + offset2);
	  else
	    MIPS_ELF_PUT_WORD (abfd, value - dtprel_base (info),
			       sgot->contents + offset2);
	}
      else
	{
	  MIPS_ELF_PUT_WORD (abfd, 1,
			     sgot->contents + offset);
	  MIPS_ELF_PUT_WORD (abfd, value - dtprel_base (info),
			     sgot->contents + offset2);
	}

      got_offset += 2 * MIPS_ELF_GOT_SIZE (abfd);
    }

  /* Initial Exec model.  */
  if (*tls_type_p & GOT_TLS_IE)
    {
      offset = got_offset;

      if (need_relocs)
	{
	  if (indx == 0)
	    MIPS_ELF_PUT_WORD (abfd, value - elf_hash_table (info)->tls_sec->vma,
			       sgot->contents + offset);
	  else
	    MIPS_ELF_PUT_WORD (abfd, 0,
			       sgot->contents + offset);

	  mips_elf_output_dynamic_relocation
	    (abfd, sreloc, indx,
	     ABI_64_P (abfd) ? R_MIPS_TLS_TPREL64 : R_MIPS_TLS_TPREL32,
	     sgot->output_offset + sgot->output_section->vma + offset);
	}
      else
	MIPS_ELF_PUT_WORD (abfd, value - tprel_base (info),
			   sgot->contents + offset);
    }

  if (*tls_type_p & GOT_TLS_LDM)
    {
      /* The initial offset is zero, and the LD offsets will include the
	 bias by DTP_OFFSET.  */
      MIPS_ELF_PUT_WORD (abfd, 0,
			 sgot->contents + got_offset
			 + MIPS_ELF_GOT_SIZE (abfd));

      if (!info->shared)
	MIPS_ELF_PUT_WORD (abfd, 1,
			   sgot->contents + got_offset);
      else
	mips_elf_output_dynamic_relocation
	  (abfd, sreloc, indx,
	   ABI_64_P (abfd) ? R_MIPS_TLS_DTPMOD64 : R_MIPS_TLS_DTPMOD32,
	   sgot->output_offset + sgot->output_section->vma + got_offset);
    }

  *tls_type_p |= GOT_TLS_DONE;
}

/* Return the GOT index to use for a relocation of type R_TYPE against
   a symbol accessed using TLS_TYPE models.  The GOT entries for this
   symbol in this GOT start at GOT_INDEX.  This function initializes the
   GOT entries and corresponding relocations.  */

static bfd_vma
mips_tls_got_index (bfd *abfd, bfd_vma got_index, unsigned char *tls_type,
		    int r_type, struct bfd_link_info *info,
		    struct mips_elf_link_hash_entry *h, bfd_vma symbol)
{
  BFD_ASSERT (r_type == R_MIPS_TLS_GOTTPREL || r_type == R_MIPS_TLS_GD
	      || r_type == R_MIPS_TLS_LDM);

  mips_elf_initialize_tls_slots (abfd, got_index, tls_type, info, h, symbol);

  if (r_type == R_MIPS_TLS_GOTTPREL)
    {
      BFD_ASSERT (*tls_type & GOT_TLS_IE);
      if (*tls_type & GOT_TLS_GD)
	return got_index + 2 * MIPS_ELF_GOT_SIZE (abfd);
      else
	return got_index;
    }

  if (r_type == R_MIPS_TLS_GD)
    {
      BFD_ASSERT (*tls_type & GOT_TLS_GD);
      return got_index;
    }

  if (r_type == R_MIPS_TLS_LDM)
    {
      BFD_ASSERT (*tls_type & GOT_TLS_LDM);
      return got_index;
    }

  return got_index;
}

/* Return the offset from _GLOBAL_OFFSET_TABLE_ of the .got.plt entry
   for global symbol H.  .got.plt comes before the GOT, so the offset
   will be negative.  */

static bfd_vma
mips_elf_gotplt_index (struct bfd_link_info *info,
		       struct elf_link_hash_entry *h)
{
  bfd_vma plt_index, got_address, got_value;
  struct mips_elf_link_hash_table *htab;

  htab = mips_elf_hash_table (info);
  BFD_ASSERT (h->plt.offset != (bfd_vma) -1);

  /* Calculate the index of the symbol's PLT entry.  */
  plt_index = (h->plt.offset - htab->plt_header_size) / htab->plt_entry_size;

  /* Calculate the address of the associated .got.plt entry.  */
  got_address = (htab->sgotplt->output_section->vma
		 + htab->sgotplt->output_offset
		 + plt_index * 4);

  /* Calculate the value of _GLOBAL_OFFSET_TABLE_.  */
  got_value = (htab->root.hgot->root.u.def.section->output_section->vma
	       + htab->root.hgot->root.u.def.section->output_offset
	       + htab->root.hgot->root.u.def.value);

  return got_address - got_value;
}

/* Return the GOT offset for address VALUE.   If there is not yet a GOT
   entry for this value, create one.  If R_SYMNDX refers to a TLS symbol,
   create a TLS GOT entry instead.  Return -1 if no satisfactory GOT
   offset can be found.  */

static bfd_vma
mips_elf_local_got_index (bfd *abfd, bfd *ibfd, struct bfd_link_info *info,
			  bfd_vma value, unsigned long r_symndx,
			  struct mips_elf_link_hash_entry *h, int r_type)
{
  asection *sgot;
  struct mips_got_info *g;
  struct mips_got_entry *entry;

  g = mips_elf_got_info (elf_hash_table (info)->dynobj, &sgot);

  entry = mips_elf_create_local_got_entry (abfd, info, ibfd, g, sgot,
					   value, r_symndx, h, r_type);
  if (!entry)
    return MINUS_ONE;

  if (TLS_RELOC_P (r_type))
    {
      if (entry->symndx == -1 && g->next == NULL)
	/* A type (3) entry in the single-GOT case.  We use the symbol's
	   hash table entry to track the index.  */
	return mips_tls_got_index (abfd, h->tls_got_offset, &h->tls_type,
				   r_type, info, h, value);
      else
	return mips_tls_got_index (abfd, entry->gotidx, &entry->tls_type,
				   r_type, info, h, value);
    }
  else
    return entry->gotidx;
}

/* Returns the GOT index for the global symbol indicated by H.  */

static bfd_vma
mips_elf_global_got_index (bfd *abfd, bfd *ibfd, struct elf_link_hash_entry *h,
			   int r_type, struct bfd_link_info *info)
{
  bfd_vma index;
  asection *sgot;
  struct mips_got_info *g, *gg;
  long global_got_dynindx = 0;

  gg = g = mips_elf_got_info (abfd, &sgot);
  if (g->bfd2got && ibfd)
    {
      struct mips_got_entry e, *p;

      BFD_ASSERT (h->dynindx >= 0);

      g = mips_elf_got_for_ibfd (g, ibfd);
      if (g->next != gg || TLS_RELOC_P (r_type))
	{
	  e.abfd = ibfd;
	  e.symndx = -1;
	  e.d.h = (struct mips_elf_link_hash_entry *)h;
	  e.tls_type = 0;

	  p = htab_find (g->got_entries, &e);

	  BFD_ASSERT (p->gotidx > 0);

	  if (TLS_RELOC_P (r_type))
	    {
	      bfd_vma value = MINUS_ONE;
	      if ((h->root.type == bfd_link_hash_defined
		   || h->root.type == bfd_link_hash_defweak)
		  && h->root.u.def.section->output_section)
		value = (h->root.u.def.value
			 + h->root.u.def.section->output_offset
			 + h->root.u.def.section->output_section->vma);

	      return mips_tls_got_index (abfd, p->gotidx, &p->tls_type, r_type,
					 info, e.d.h, value);
	    }
	  else
	    return p->gotidx;
	}
    }

  if (gg->global_gotsym != NULL)
    global_got_dynindx = gg->global_gotsym->dynindx;

  if (TLS_RELOC_P (r_type))
    {
      struct mips_elf_link_hash_entry *hm
	= (struct mips_elf_link_hash_entry *) h;
      bfd_vma value = MINUS_ONE;

      if ((h->root.type == bfd_link_hash_defined
	   || h->root.type == bfd_link_hash_defweak)
	  && h->root.u.def.section->output_section)
	value = (h->root.u.def.value
		 + h->root.u.def.section->output_offset
		 + h->root.u.def.section->output_section->vma);

      index = mips_tls_got_index (abfd, hm->tls_got_offset, &hm->tls_type,
				  r_type, info, hm, value);
    }
  else
    {
      /* Once we determine the global GOT entry with the lowest dynamic
	 symbol table index, we must put all dynamic symbols with greater
	 indices into the GOT.  That makes it easy to calculate the GOT
	 offset.  */
      BFD_ASSERT (h->dynindx >= global_got_dynindx);
      index = ((h->dynindx - global_got_dynindx + g->local_gotno)
	       * MIPS_ELF_GOT_SIZE (abfd));
    }
  BFD_ASSERT (index < sgot->size);

  return index;
}

/* Find a GOT page entry that points to within 32KB of VALUE.  These
   entries are supposed to be placed at small offsets in the GOT, i.e.,
   within 32KB of GP.  Return the index of the GOT entry, or -1 if no
   entry could be created.  If OFFSETP is nonnull, use it to return the
   offset of the GOT entry from VALUE.  */

static bfd_vma
mips_elf_got_page (bfd *abfd, bfd *ibfd, struct bfd_link_info *info,
		   bfd_vma value, bfd_vma *offsetp)
{
  asection *sgot;
  struct mips_got_info *g;
  bfd_vma page, index;
  struct mips_got_entry *entry;

  g = mips_elf_got_info (elf_hash_table (info)->dynobj, &sgot);

  page = (value + 0x8000) & ~(bfd_vma) 0xffff;
  entry = mips_elf_create_local_got_entry (abfd, info, ibfd, g, sgot,
					   page, 0, NULL, R_MIPS_GOT_PAGE);

  if (!entry)
    return MINUS_ONE;

  index = entry->gotidx;

  if (offsetp)
    *offsetp = value - entry->d.address;

  return index;
}

/* Find a local GOT entry for an R_MIPS_GOT16 relocation against VALUE.
   EXTERNAL is true if the relocation was against a global symbol
   that has been forced local.  */

static bfd_vma
mips_elf_got16_entry (bfd *abfd, bfd *ibfd, struct bfd_link_info *info,
		      bfd_vma value, bfd_boolean external)
{
  asection *sgot;
  struct mips_got_info *g;
  struct mips_got_entry *entry;

  /* GOT16 relocations against local symbols are followed by a LO16
     relocation; those against global symbols are not.  Thus if the
     symbol was originally local, the GOT16 relocation should load the
     equivalent of %hi(VALUE), otherwise it should load VALUE itself.  */
  if (! external)
    value = mips_elf_high (value) << 16;

  g = mips_elf_got_info (elf_hash_table (info)->dynobj, &sgot);

  entry = mips_elf_create_local_got_entry (abfd, info, ibfd, g, sgot,
					   value, 0, NULL, R_MIPS_GOT16);
  if (entry)
    return entry->gotidx;
  else
    return MINUS_ONE;
}

/* Returns the offset for the entry at the INDEXth position
   in the GOT.  */

static bfd_vma
mips_elf_got_offset_from_index (bfd *dynobj, bfd *output_bfd,
				bfd *input_bfd, bfd_vma index)
{
  asection *sgot;
  bfd_vma gp;
  struct mips_got_info *g;

  g = mips_elf_got_info (dynobj, &sgot);
  gp = _bfd_get_gp_value (output_bfd)
    + mips_elf_adjust_gp (output_bfd, g, input_bfd);

  return sgot->output_section->vma + sgot->output_offset + index - gp;
}

/* Create and return a local GOT entry for VALUE, which was calculated
   from a symbol belonging to INPUT_SECTON.  Return NULL if it could not
   be created.  If R_SYMNDX refers to a TLS symbol, create a TLS entry
   instead.  */

static struct mips_got_entry *
mips_elf_create_local_got_entry (bfd *abfd, struct bfd_link_info *info,
				 bfd *ibfd, struct mips_got_info *gg,
				 asection *sgot, bfd_vma value,
				 unsigned long r_symndx,
				 struct mips_elf_link_hash_entry *h,
				 int r_type)
{
  struct mips_got_entry entry, **loc;
  struct mips_got_info *g;
  struct mips_elf_link_hash_table *htab;

  htab = mips_elf_hash_table (info);

  entry.abfd = NULL;
  entry.symndx = -1;
  entry.d.address = value;
  entry.tls_type = 0;

  g = mips_elf_got_for_ibfd (gg, ibfd);
  if (g == NULL)
    {
      g = mips_elf_got_for_ibfd (gg, abfd);
      BFD_ASSERT (g != NULL);
    }

  /* We might have a symbol, H, if it has been forced local.  Use the
     global entry then.  It doesn't matter whether an entry is local
     or global for TLS, since the dynamic linker does not
     automatically relocate TLS GOT entries.  */
  BFD_ASSERT (h == NULL || h->root.forced_local);
  if (TLS_RELOC_P (r_type))
    {
      struct mips_got_entry *p;

      entry.abfd = ibfd;
      if (r_type == R_MIPS_TLS_LDM)
	{
	  entry.tls_type = GOT_TLS_LDM;
	  entry.symndx = 0;
	  entry.d.addend = 0;
	}
      else if (h == NULL)
	{
	  entry.symndx = r_symndx;
	  entry.d.addend = 0;
	}
      else
	entry.d.h = h;

      p = (struct mips_got_entry *)
	htab_find (g->got_entries, &entry);

      BFD_ASSERT (p);
      return p;
    }

  loc = (struct mips_got_entry **) htab_find_slot (g->got_entries, &entry,
						   INSERT);
  if (*loc)
    return *loc;

  entry.gotidx = MIPS_ELF_GOT_SIZE (abfd) * g->assigned_gotno++;
  entry.tls_type = 0;

  *loc = (struct mips_got_entry *)bfd_alloc (abfd, sizeof entry);

  if (! *loc)
    return NULL;

  memcpy (*loc, &entry, sizeof entry);

  if (g->assigned_gotno >= g->local_gotno)
    {
      (*loc)->gotidx = -1;
      /* We didn't allocate enough space in the GOT.  */
      (*_bfd_error_handler)
	(_("not enough GOT space for local GOT entries"));
      bfd_set_error (bfd_error_bad_value);
      return NULL;
    }

  MIPS_ELF_PUT_WORD (abfd, value,
		     (sgot->contents + entry.gotidx));

  /* These GOT entries need a dynamic relocation on VxWorks.  */
  if (htab->is_vxworks)
    {
      Elf_Internal_Rela outrel;
      asection *s;
      bfd_byte *loc;
      bfd_vma got_address;

      s = mips_elf_rel_dyn_section (info, FALSE);
      got_address = (sgot->output_section->vma
		     + sgot->output_offset
		     + entry.gotidx);

      loc = s->contents + (s->reloc_count++ * sizeof (Elf32_External_Rela));
      outrel.r_offset = got_address;
      outrel.r_info = ELF32_R_INFO (STN_UNDEF, R_MIPS_32);
      outrel.r_addend = value;
      bfd_elf32_swap_reloca_out (abfd, &outrel, loc);
    }

  return *loc;
}

/* Sort the dynamic symbol table so that symbols that need GOT entries
   appear towards the end.  This reduces the amount of GOT space
   required.  MAX_LOCAL is used to set the number of local symbols
   known to be in the dynamic symbol table.  During
   _bfd_mips_elf_size_dynamic_sections, this value is 1.  Afterward, the
   section symbols are added and the count is higher.  */

static bfd_boolean
mips_elf_sort_hash_table (struct bfd_link_info *info, unsigned long max_local)
{
  struct mips_elf_hash_sort_data hsd;
  struct mips_got_info *g;
  bfd *dynobj;

  dynobj = elf_hash_table (info)->dynobj;

  g = mips_elf_got_info (dynobj, NULL);

  hsd.low = NULL;
  hsd.max_unref_got_dynindx =
  hsd.min_got_dynindx = elf_hash_table (info)->dynsymcount
    /* In the multi-got case, assigned_gotno of the master got_info
       indicate the number of entries that aren't referenced in the
       primary GOT, but that must have entries because there are
       dynamic relocations that reference it.  Since they aren't
       referenced, we move them to the end of the GOT, so that they
       don't prevent other entries that are referenced from getting
       too large offsets.  */
    - (g->next ? g->assigned_gotno : 0);
  hsd.max_non_got_dynindx = max_local;
  mips_elf_link_hash_traverse (((struct mips_elf_link_hash_table *)
				elf_hash_table (info)),
			       mips_elf_sort_hash_table_f,
			       &hsd);

  /* There should have been enough room in the symbol table to
     accommodate both the GOT and non-GOT symbols.  */
  BFD_ASSERT (hsd.max_non_got_dynindx <= hsd.min_got_dynindx);
  BFD_ASSERT ((unsigned long)hsd.max_unref_got_dynindx
	      <= elf_hash_table (info)->dynsymcount);

  /* Now we know which dynamic symbol has the lowest dynamic symbol
     table index in the GOT.  */
  g->global_gotsym = hsd.low;

  return TRUE;
}

/* If H needs a GOT entry, assign it the highest available dynamic
   index.  Otherwise, assign it the lowest available dynamic
   index.  */

static bfd_boolean
mips_elf_sort_hash_table_f (struct mips_elf_link_hash_entry *h, void *data)
{
  struct mips_elf_hash_sort_data *hsd = data;

  if (h->root.root.type == bfd_link_hash_warning)
    h = (struct mips_elf_link_hash_entry *) h->root.root.u.i.link;

  /* Symbols without dynamic symbol table entries aren't interesting
     at all.  */
  if (h->root.dynindx == -1)
    return TRUE;

  /* Global symbols that need GOT entries that are not explicitly
     referenced are marked with got offset 2.  Those that are
     referenced get a 1, and those that don't need GOT entries get
     -1.  */
  if (h->root.got.offset == 2)
    {
      BFD_ASSERT (h->tls_type == GOT_NORMAL);

      if (hsd->max_unref_got_dynindx == hsd->min_got_dynindx)
	hsd->low = (struct elf_link_hash_entry *) h;
      h->root.dynindx = hsd->max_unref_got_dynindx++;
    }
  else if (h->root.got.offset != 1)
    h->root.dynindx = hsd->max_non_got_dynindx++;
  else
    {
      BFD_ASSERT (h->tls_type == GOT_NORMAL);

      h->root.dynindx = --hsd->min_got_dynindx;
      hsd->low = (struct elf_link_hash_entry *) h;
    }

  return TRUE;
}

/* If H is a symbol that needs a global GOT entry, but has a dynamic
   symbol table index lower than any we've seen to date, record it for
   posterity.  */

static bfd_boolean
mips_elf_record_global_got_symbol (struct elf_link_hash_entry *h,
				   bfd *abfd, struct bfd_link_info *info,
				   struct mips_got_info *g,
				   unsigned char tls_flag)
{
  struct mips_got_entry entry, **loc;

  /* A global symbol in the GOT must also be in the dynamic symbol
     table.  */
  if (h->dynindx == -1)
    {
      switch (ELF_ST_VISIBILITY (h->other))
	{
	case STV_INTERNAL:
	case STV_HIDDEN:
	  _bfd_mips_elf_hide_symbol (info, h, TRUE);
	  break;
	}
      if (!bfd_elf_link_record_dynamic_symbol (info, h))
	return FALSE;
    }

  /* Make sure we have a GOT to put this entry into.  */
  BFD_ASSERT (g != NULL);

  entry.abfd = abfd;
  entry.symndx = -1;
  entry.d.h = (struct mips_elf_link_hash_entry *) h;
  entry.tls_type = 0;

  loc = (struct mips_got_entry **) htab_find_slot (g->got_entries, &entry,
						   INSERT);

  /* If we've already marked this entry as needing GOT space, we don't
     need to do it again.  */
  if (*loc)
    {
      (*loc)->tls_type |= tls_flag;
      return TRUE;
    }

  *loc = (struct mips_got_entry *)bfd_alloc (abfd, sizeof entry);

  if (! *loc)
    return FALSE;

  entry.gotidx = -1;
  entry.tls_type = tls_flag;

  memcpy (*loc, &entry, sizeof entry);

  if (h->got.offset != MINUS_ONE)
    return TRUE;

  /* By setting this to a value other than -1, we are indicating that
     there needs to be a GOT entry for H.  Avoid using zero, as the
     generic ELF copy_indirect_symbol tests for <= 0.  */
  if (tls_flag == 0)
    h->got.offset = 1;

  return TRUE;
}

/* Reserve space in G for a GOT entry containing the value of symbol
   SYMNDX in input bfd ABDF, plus ADDEND.  */

static bfd_boolean
mips_elf_record_local_got_symbol (bfd *abfd, long symndx, bfd_vma addend,
				  struct mips_got_info *g,
				  unsigned char tls_flag)
{
  struct mips_got_entry entry, **loc;

  entry.abfd = abfd;
  entry.symndx = symndx;
  entry.d.addend = addend;
  entry.tls_type = tls_flag;
  loc = (struct mips_got_entry **)
    htab_find_slot (g->got_entries, &entry, INSERT);

  if (*loc)
    {
      if (tls_flag == GOT_TLS_GD && !((*loc)->tls_type & GOT_TLS_GD))
	{
	  g->tls_gotno += 2;
	  (*loc)->tls_type |= tls_flag;
	}
      else if (tls_flag == GOT_TLS_IE && !((*loc)->tls_type & GOT_TLS_IE))
	{
	  g->tls_gotno += 1;
	  (*loc)->tls_type |= tls_flag;
	}
      return TRUE;
    }

  if (tls_flag != 0)
    {
      entry.gotidx = -1;
      entry.tls_type = tls_flag;
      if (tls_flag == GOT_TLS_IE)
	g->tls_gotno += 1;
      else if (tls_flag == GOT_TLS_GD)
	g->tls_gotno += 2;
      else if (g->tls_ldm_offset == MINUS_ONE)
	{
	  g->tls_ldm_offset = MINUS_TWO;
	  g->tls_gotno += 2;
	}
    }
  else
    {
      entry.gotidx = g->local_gotno++;
      entry.tls_type = 0;
    }

  *loc = (struct mips_got_entry *)bfd_alloc (abfd, sizeof entry);

  if (! *loc)
    return FALSE;

  memcpy (*loc, &entry, sizeof entry);

  return TRUE;
}

/* Compute the hash value of the bfd in a bfd2got hash entry.  */

static hashval_t
mips_elf_bfd2got_entry_hash (const void *entry_)
{
  const struct mips_elf_bfd2got_hash *entry
    = (struct mips_elf_bfd2got_hash *)entry_;

  return entry->bfd->id;
}

/* Check whether two hash entries have the same bfd.  */

static int
mips_elf_bfd2got_entry_eq (const void *entry1, const void *entry2)
{
  const struct mips_elf_bfd2got_hash *e1
    = (const struct mips_elf_bfd2got_hash *)entry1;
  const struct mips_elf_bfd2got_hash *e2
    = (const struct mips_elf_bfd2got_hash *)entry2;

  return e1->bfd == e2->bfd;
}

/* In a multi-got link, determine the GOT to be used for IBFD.  G must
   be the master GOT data.  */

static struct mips_got_info *
mips_elf_got_for_ibfd (struct mips_got_info *g, bfd *ibfd)
{
  struct mips_elf_bfd2got_hash e, *p;

  if (! g->bfd2got)
    return g;

  e.bfd = ibfd;
  p = htab_find (g->bfd2got, &e);
  return p ? p->g : NULL;
}

/* Create one separate got for each bfd that has entries in the global
   got, such that we can tell how many local and global entries each
   bfd requires.  */

static int
mips_elf_make_got_per_bfd (void **entryp, void *p)
{
  struct mips_got_entry *entry = (struct mips_got_entry *)*entryp;
  struct mips_elf_got_per_bfd_arg *arg = (struct mips_elf_got_per_bfd_arg *)p;
  htab_t bfd2got = arg->bfd2got;
  struct mips_got_info *g;
  struct mips_elf_bfd2got_hash bfdgot_entry, *bfdgot;
  void **bfdgotp;

  /* Find the got_info for this GOT entry's input bfd.  Create one if
     none exists.  */
  bfdgot_entry.bfd = entry->abfd;
  bfdgotp = htab_find_slot (bfd2got, &bfdgot_entry, INSERT);
  bfdgot = (struct mips_elf_bfd2got_hash *)*bfdgotp;

  if (bfdgot != NULL)
    g = bfdgot->g;
  else
    {
      bfdgot = (struct mips_elf_bfd2got_hash *)bfd_alloc
	(arg->obfd, sizeof (struct mips_elf_bfd2got_hash));

      if (bfdgot == NULL)
	{
	  arg->obfd = 0;
	  return 0;
	}

      *bfdgotp = bfdgot;

      bfdgot->bfd = entry->abfd;
      bfdgot->g = g = (struct mips_got_info *)
	bfd_alloc (arg->obfd, sizeof (struct mips_got_info));
      if (g == NULL)
	{
	  arg->obfd = 0;
	  return 0;
	}

      g->global_gotsym = NULL;
      g->global_gotno = 0;
      g->local_gotno = 0;
      g->assigned_gotno = -1;
      g->tls_gotno = 0;
      g->tls_assigned_gotno = 0;
      g->tls_ldm_offset = MINUS_ONE;
      g->got_entries = htab_try_create (1, mips_elf_multi_got_entry_hash,
					mips_elf_multi_got_entry_eq, NULL);
      if (g->got_entries == NULL)
	{
	  arg->obfd = 0;
	  return 0;
	}

      g->bfd2got = NULL;
      g->next = NULL;
    }

  /* Insert the GOT entry in the bfd's got entry hash table.  */
  entryp = htab_find_slot (g->got_entries, entry, INSERT);
  if (*entryp != NULL)
    return 1;

  *entryp = entry;

  if (entry->tls_type)
    {
      if (entry->tls_type & (GOT_TLS_GD | GOT_TLS_LDM))
	g->tls_gotno += 2;
      if (entry->tls_type & GOT_TLS_IE)
	g->tls_gotno += 1;
    }
  else if (entry->symndx >= 0 || entry->d.h->forced_local)
    ++g->local_gotno;
  else
    ++g->global_gotno;

  return 1;
}

/* Attempt to merge gots of different input bfds.  Try to use as much
   as possible of the primary got, since it doesn't require explicit
   dynamic relocations, but don't use bfds that would reference global
   symbols out of the addressable range.  Failing the primary got,
   attempt to merge with the current got, or finish the current got
   and then make make the new got current.  */

static int
mips_elf_merge_gots (void **bfd2got_, void *p)
{
  struct mips_elf_bfd2got_hash *bfd2got
    = (struct mips_elf_bfd2got_hash *)*bfd2got_;
  struct mips_elf_got_per_bfd_arg *arg = (struct mips_elf_got_per_bfd_arg *)p;
  unsigned int lcount = bfd2got->g->local_gotno;
  unsigned int gcount = bfd2got->g->global_gotno;
  unsigned int tcount = bfd2got->g->tls_gotno;
  unsigned int maxcnt = arg->max_count;
  bfd_boolean too_many_for_tls = FALSE;

  /* We place TLS GOT entries after both locals and globals.  The globals
     for the primary GOT may overflow the normal GOT size limit, so be
     sure not to merge a GOT which requires TLS with the primary GOT in that
     case.  This doesn't affect non-primary GOTs.  */
  if (tcount > 0)
    {
      unsigned int primary_total = lcount + tcount + arg->global_count;
      if (primary_total > maxcnt)
	too_many_for_tls = TRUE;
    }

  /* If we don't have a primary GOT and this is not too big, use it as
     a starting point for the primary GOT.  */
  if (! arg->primary && lcount + gcount + tcount <= maxcnt
      && ! too_many_for_tls)
    {
      arg->primary = bfd2got->g;
      arg->primary_count = lcount + gcount;
    }
  /* If it looks like we can merge this bfd's entries with those of
     the primary, merge them.  The heuristics is conservative, but we
     don't have to squeeze it too hard.  */
  else if (arg->primary && ! too_many_for_tls
	   && (arg->primary_count + lcount + gcount + tcount) <= maxcnt)
    {
      struct mips_got_info *g = bfd2got->g;
      int old_lcount = arg->primary->local_gotno;
      int old_gcount = arg->primary->global_gotno;
      int old_tcount = arg->primary->tls_gotno;

      bfd2got->g = arg->primary;

      htab_traverse (g->got_entries,
		     mips_elf_make_got_per_bfd,
		     arg);
      if (arg->obfd == NULL)
	return 0;

      htab_delete (g->got_entries);
      /* We don't have to worry about releasing memory of the actual
	 got entries, since they're all in the master got_entries hash
	 table anyway.  */

      BFD_ASSERT (old_lcount + lcount >= arg->primary->local_gotno);
      BFD_ASSERT (old_gcount + gcount >= arg->primary->global_gotno);
      BFD_ASSERT (old_tcount + tcount >= arg->primary->tls_gotno);

      arg->primary_count = arg->primary->local_gotno
	+ arg->primary->global_gotno + arg->primary->tls_gotno;
    }
  /* If we can merge with the last-created got, do it.  */
  else if (arg->current
	   && arg->current_count + lcount + gcount + tcount <= maxcnt)
    {
      struct mips_got_info *g = bfd2got->g;
      int old_lcount = arg->current->local_gotno;
      int old_gcount = arg->current->global_gotno;
      int old_tcount = arg->current->tls_gotno;

      bfd2got->g = arg->current;

      htab_traverse (g->got_entries,
		     mips_elf_make_got_per_bfd,
		     arg);
      if (arg->obfd == NULL)
	return 0;

      htab_delete (g->got_entries);

      BFD_ASSERT (old_lcount + lcount >= arg->current->local_gotno);
      BFD_ASSERT (old_gcount + gcount >= arg->current->global_gotno);
      BFD_ASSERT (old_tcount + tcount >= arg->current->tls_gotno);

      arg->current_count = arg->current->local_gotno
	+ arg->current->global_gotno + arg->current->tls_gotno;
    }
  /* Well, we couldn't merge, so create a new GOT.  Don't check if it
     fits; if it turns out that it doesn't, we'll get relocation
     overflows anyway.  */
  else
    {
      bfd2got->g->next = arg->current;
      arg->current = bfd2got->g;

      arg->current_count = lcount + gcount + 2 * tcount;
    }

  return 1;
}

/* Set the TLS GOT index for the GOT entry in ENTRYP.  ENTRYP's NEXT field
   is null iff there is just a single GOT.  */

static int
mips_elf_initialize_tls_index (void **entryp, void *p)
{
  struct mips_got_entry *entry = (struct mips_got_entry *)*entryp;
  struct mips_got_info *g = p;
  bfd_vma next_index;
  unsigned char tls_type;

  /* We're only interested in TLS symbols.  */
  if (entry->tls_type == 0)
    return 1;

  next_index = MIPS_ELF_GOT_SIZE (entry->abfd) * (long) g->tls_assigned_gotno;

  if (entry->symndx == -1 && g->next == NULL)
    {
      /* A type (3) got entry in the single-GOT case.  We use the symbol's
	 hash table entry to track its index.  */
      if (entry->d.h->tls_type & GOT_TLS_OFFSET_DONE)
	return 1;
      entry->d.h->tls_type |= GOT_TLS_OFFSET_DONE;
      entry->d.h->tls_got_offset = next_index;
      tls_type = entry->d.h->tls_type;
    }
  else
    {
      if (entry->tls_type & GOT_TLS_LDM)
	{
	  /* There are separate mips_got_entry objects for each input bfd
	     that requires an LDM entry.  Make sure that all LDM entries in
	     a GOT resolve to the same index.  */
	  if (g->tls_ldm_offset != MINUS_TWO && g->tls_ldm_offset != MINUS_ONE)
	    {
	      entry->gotidx = g->tls_ldm_offset;
	      return 1;
	    }
	  g->tls_ldm_offset = next_index;
	}
      entry->gotidx = next_index;
      tls_type = entry->tls_type;
    }

  /* Account for the entries we've just allocated.  */
  if (tls_type & (GOT_TLS_GD | GOT_TLS_LDM))
    g->tls_assigned_gotno += 2;
  if (tls_type & GOT_TLS_IE)
    g->tls_assigned_gotno += 1;

  return 1;
}

/* If passed a NULL mips_got_info in the argument, set the marker used
   to tell whether a global symbol needs a got entry (in the primary
   got) to the given VALUE.

   If passed a pointer G to a mips_got_info in the argument (it must
   not be the primary GOT), compute the offset from the beginning of
   the (primary) GOT section to the entry in G corresponding to the
   global symbol.  G's assigned_gotno must contain the index of the
   first available global GOT entry in G.  VALUE must contain the size
   of a GOT entry in bytes.  For each global GOT entry that requires a
   dynamic relocation, NEEDED_RELOCS is incremented, and the symbol is
   marked as not eligible for lazy resolution through a function
   stub.  */
static int
mips_elf_set_global_got_offset (void **entryp, void *p)
{
  struct mips_got_entry *entry = (struct mips_got_entry *)*entryp;
  struct mips_elf_set_global_got_offset_arg *arg
    = (struct mips_elf_set_global_got_offset_arg *)p;
  struct mips_got_info *g = arg->g;

  if (g && entry->tls_type != GOT_NORMAL)
    arg->needed_relocs +=
      mips_tls_got_relocs (arg->info, entry->tls_type,
			   entry->symndx == -1 ? &entry->d.h->root : NULL);

  if (entry->abfd != NULL && entry->symndx == -1
      && entry->d.h->root.dynindx != -1
      && entry->d.h->tls_type == GOT_NORMAL)
    {
      if (g)
	{
	  BFD_ASSERT (g->global_gotsym == NULL);

	  entry->gotidx = arg->value * (long) g->assigned_gotno++;
	  if (arg->info->shared
	      || (elf_hash_table (arg->info)->dynamic_sections_created
		  && entry->d.h->root.def_dynamic
		  && !entry->d.h->root.def_regular))
	    ++arg->needed_relocs;
	}
      else
	entry->d.h->root.got.offset = arg->value;
    }

  return 1;
}

/* Mark any global symbols referenced in the GOT we are iterating over
   as inelligible for lazy resolution stubs.  */
static int
mips_elf_set_no_stub (void **entryp, void *p ATTRIBUTE_UNUSED)
{
  struct mips_got_entry *entry = (struct mips_got_entry *)*entryp;

  if (entry->abfd != NULL
      && entry->symndx == -1
      && entry->d.h->root.dynindx != -1)
    entry->d.h->no_fn_stub = TRUE;

  return 1;
}

/* Follow indirect and warning hash entries so that each got entry
   points to the final symbol definition.  P must point to a pointer
   to the hash table we're traversing.  Since this traversal may
   modify the hash table, we set this pointer to NULL to indicate
   we've made a potentially-destructive change to the hash table, so
   the traversal must be restarted.  */
static int
mips_elf_resolve_final_got_entry (void **entryp, void *p)
{
  struct mips_got_entry *entry = (struct mips_got_entry *)*entryp;
  htab_t got_entries = *(htab_t *)p;

  if (entry->abfd != NULL && entry->symndx == -1)
    {
      struct mips_elf_link_hash_entry *h = entry->d.h;

      while (h->root.root.type == bfd_link_hash_indirect
 	     || h->root.root.type == bfd_link_hash_warning)
	h = (struct mips_elf_link_hash_entry *) h->root.root.u.i.link;

      if (entry->d.h == h)
	return 1;

      entry->d.h = h;

      /* If we can't find this entry with the new bfd hash, re-insert
	 it, and get the traversal restarted.  */
      if (! htab_find (got_entries, entry))
	{
	  htab_clear_slot (got_entries, entryp);
	  entryp = htab_find_slot (got_entries, entry, INSERT);
	  if (! *entryp)
	    *entryp = entry;
	  /* Abort the traversal, since the whole table may have
	     moved, and leave it up to the parent to restart the
	     process.  */
	  *(htab_t *)p = NULL;
	  return 0;
	}
      /* We might want to decrement the global_gotno count, but it's
	 either too early or too late for that at this point.  */
    }

  return 1;
}

/* Turn indirect got entries in a got_entries table into their final
   locations.  */
static void
mips_elf_resolve_final_got_entries (struct mips_got_info *g)
{
  htab_t got_entries;

  do
    {
      got_entries = g->got_entries;

      htab_traverse (got_entries,
		     mips_elf_resolve_final_got_entry,
		     &got_entries);
    }
  while (got_entries == NULL);
}

/* Return the offset of an input bfd IBFD's GOT from the beginning of
   the primary GOT.  */
static bfd_vma
mips_elf_adjust_gp (bfd *abfd, struct mips_got_info *g, bfd *ibfd)
{
  if (g->bfd2got == NULL)
    return 0;

  g = mips_elf_got_for_ibfd (g, ibfd);
  if (! g)
    return 0;

  BFD_ASSERT (g->next);

  g = g->next;

  return (g->local_gotno + g->global_gotno + g->tls_gotno)
    * MIPS_ELF_GOT_SIZE (abfd);
}

/* Turn a single GOT that is too big for 16-bit addressing into
   a sequence of GOTs, each one 16-bit addressable.  */

static bfd_boolean
mips_elf_multi_got (bfd *abfd, struct bfd_link_info *info,
		    struct mips_got_info *g, asection *got,
		    bfd_size_type pages)
{
  struct mips_elf_got_per_bfd_arg got_per_bfd_arg;
  struct mips_elf_set_global_got_offset_arg set_got_offset_arg;
  struct mips_got_info *gg;
  unsigned int assign;

  g->bfd2got = htab_try_create (1, mips_elf_bfd2got_entry_hash,
				mips_elf_bfd2got_entry_eq, NULL);
  if (g->bfd2got == NULL)
    return FALSE;

  got_per_bfd_arg.bfd2got = g->bfd2got;
  got_per_bfd_arg.obfd = abfd;
  got_per_bfd_arg.info = info;

  /* Count how many GOT entries each input bfd requires, creating a
     map from bfd to got info while at that.  */
  htab_traverse (g->got_entries, mips_elf_make_got_per_bfd, &got_per_bfd_arg);
  if (got_per_bfd_arg.obfd == NULL)
    return FALSE;

  got_per_bfd_arg.current = NULL;
  got_per_bfd_arg.primary = NULL;
  /* Taking out PAGES entries is a worst-case estimate.  We could
     compute the maximum number of pages that each separate input bfd
     uses, but it's probably not worth it.  */
  got_per_bfd_arg.max_count = ((MIPS_ELF_GOT_MAX_SIZE (info)
				/ MIPS_ELF_GOT_SIZE (abfd))
			       - MIPS_RESERVED_GOTNO (info) - pages);
  /* The number of globals that will be included in the primary GOT.
     See the calls to mips_elf_set_global_got_offset below for more
     information.  */
  got_per_bfd_arg.global_count = g->global_gotno;

  /* Try to merge the GOTs of input bfds together, as long as they
     don't seem to exceed the maximum GOT size, choosing one of them
     to be the primary GOT.  */
  htab_traverse (g->bfd2got, mips_elf_merge_gots, &got_per_bfd_arg);
  if (got_per_bfd_arg.obfd == NULL)
    return FALSE;

  /* If we do not find any suitable primary GOT, create an empty one.  */
  if (got_per_bfd_arg.primary == NULL)
    {
      g->next = (struct mips_got_info *)
	bfd_alloc (abfd, sizeof (struct mips_got_info));
      if (g->next == NULL)
	return FALSE;

      g->next->global_gotsym = NULL;
      g->next->global_gotno = 0;
      g->next->local_gotno = 0;
      g->next->tls_gotno = 0;
      g->next->assigned_gotno = 0;
      g->next->tls_assigned_gotno = 0;
      g->next->tls_ldm_offset = MINUS_ONE;
      g->next->got_entries = htab_try_create (1, mips_elf_multi_got_entry_hash,
					      mips_elf_multi_got_entry_eq,
					      NULL);
      if (g->next->got_entries == NULL)
	return FALSE;
      g->next->bfd2got = NULL;
    }
  else
    g->next = got_per_bfd_arg.primary;
  g->next->next = got_per_bfd_arg.current;

  /* GG is now the master GOT, and G is the primary GOT.  */
  gg = g;
  g = g->next;

  /* Map the output bfd to the primary got.  That's what we're going
     to use for bfds that use GOT16 or GOT_PAGE relocations that we
     didn't mark in check_relocs, and we want a quick way to find it.
     We can't just use gg->next because we're going to reverse the
     list.  */
  {
    struct mips_elf_bfd2got_hash *bfdgot;
    void **bfdgotp;

    bfdgot = (struct mips_elf_bfd2got_hash *)bfd_alloc
      (abfd, sizeof (struct mips_elf_bfd2got_hash));

    if (bfdgot == NULL)
      return FALSE;

    bfdgot->bfd = abfd;
    bfdgot->g = g;
    bfdgotp = htab_find_slot (gg->bfd2got, bfdgot, INSERT);

    BFD_ASSERT (*bfdgotp == NULL);
    *bfdgotp = bfdgot;
  }

  /* The IRIX dynamic linker requires every symbol that is referenced
     in a dynamic relocation to be present in the primary GOT, so
     arrange for them to appear after those that are actually
     referenced.

     GNU/Linux could very well do without it, but it would slow down
     the dynamic linker, since it would have to resolve every dynamic
     symbol referenced in other GOTs more than once, without help from
     the cache.  Also, knowing that every external symbol has a GOT
     helps speed up the resolution of local symbols too, so GNU/Linux
     follows IRIX's practice.

     The number 2 is used by mips_elf_sort_hash_table_f to count
     global GOT symbols that are unreferenced in the primary GOT, with
     an initial dynamic index computed from gg->assigned_gotno, where
     the number of unreferenced global entries in the primary GOT is
     preserved.  */
  if (1)
    {
      gg->assigned_gotno = gg->global_gotno - g->global_gotno;
      g->global_gotno = gg->global_gotno;
      set_got_offset_arg.value = 2;
    }
  else
    {
      /* This could be used for dynamic linkers that don't optimize
	 symbol resolution while applying relocations so as to use
	 primary GOT entries or assuming the symbol is locally-defined.
	 With this code, we assign lower dynamic indices to global
	 symbols that are not referenced in the primary GOT, so that
	 their entries can be omitted.  */
      gg->assigned_gotno = 0;
      set_got_offset_arg.value = -1;
    }

  /* Reorder dynamic symbols as described above (which behavior
     depends on the setting of VALUE).  */
  set_got_offset_arg.g = NULL;
  htab_traverse (gg->got_entries, mips_elf_set_global_got_offset,
		 &set_got_offset_arg);
  set_got_offset_arg.value = 1;
  htab_traverse (g->got_entries, mips_elf_set_global_got_offset,
		 &set_got_offset_arg);
  if (! mips_elf_sort_hash_table (info, 1))
    return FALSE;

  /* Now go through the GOTs assigning them offset ranges.
     [assigned_gotno, local_gotno[ will be set to the range of local
     entries in each GOT.  We can then compute the end of a GOT by
     adding local_gotno to global_gotno.  We reverse the list and make
     it circular since then we'll be able to quickly compute the
     beginning of a GOT, by computing the end of its predecessor.  To
     avoid special cases for the primary GOT, while still preserving
     assertions that are valid for both single- and multi-got links,
     we arrange for the main got struct to have the right number of
     global entries, but set its local_gotno such that the initial
     offset of the primary GOT is zero.  Remember that the primary GOT
     will become the last item in the circular linked list, so it
     points back to the master GOT.  */
  gg->local_gotno = -g->global_gotno;
  gg->global_gotno = g->global_gotno;
  gg->tls_gotno = 0;
  assign = 0;
  gg->next = gg;

  do
    {
      struct mips_got_info *gn;

      assign += MIPS_RESERVED_GOTNO (info);
      g->assigned_gotno = assign;
      g->local_gotno += assign + pages;
      assign = g->local_gotno + g->global_gotno + g->tls_gotno;

      /* Take g out of the direct list, and push it onto the reversed
	 list that gg points to.  g->next is guaranteed to be nonnull after
	 this operation, as required by mips_elf_initialize_tls_index. */
      gn = g->next;
      g->next = gg->next;
      gg->next = g;

      /* Set up any TLS entries.  We always place the TLS entries after
	 all non-TLS entries.  */
      g->tls_assigned_gotno = g->local_gotno + g->global_gotno;
      htab_traverse (g->got_entries, mips_elf_initialize_tls_index, g);

      /* Move onto the next GOT.  It will be a secondary GOT if nonull.  */
      g = gn;

      /* Mark global symbols in every non-primary GOT as ineligible for
	 stubs.  */
      if (g)
	htab_traverse (g->got_entries, mips_elf_set_no_stub, NULL);
    }
  while (g);

  got->size = (gg->next->local_gotno
		    + gg->next->global_gotno
		    + gg->next->tls_gotno) * MIPS_ELF_GOT_SIZE (abfd);

  return TRUE;
}


/* Returns the first relocation of type r_type found, beginning with
   RELOCATION.  RELEND is one-past-the-end of the relocation table.  */

static const Elf_Internal_Rela *
mips_elf_next_relocation (bfd *abfd ATTRIBUTE_UNUSED, unsigned int r_type,
			  const Elf_Internal_Rela *relocation,
			  const Elf_Internal_Rela *relend)
{
  unsigned long r_symndx = ELF_R_SYM (abfd, relocation->r_info);

  while (relocation < relend)
    {
      if (ELF_R_TYPE (abfd, relocation->r_info) == r_type
	  && ELF_R_SYM (abfd, relocation->r_info) == r_symndx)
	return relocation;

      ++relocation;
    }

  /* We didn't find it.  */
  return NULL;
}

/* Return whether a relocation is against a local symbol.  */

static bfd_boolean
mips_elf_local_relocation_p (bfd *input_bfd,
			     const Elf_Internal_Rela *relocation,
			     asection **local_sections,
			     bfd_boolean check_forced)
{
  unsigned long r_symndx;
  Elf_Internal_Shdr *symtab_hdr;
  struct mips_elf_link_hash_entry *h;
  size_t extsymoff;

  r_symndx = ELF_R_SYM (input_bfd, relocation->r_info);
  symtab_hdr = &elf_tdata (input_bfd)->symtab_hdr;
  extsymoff = (elf_bad_symtab (input_bfd)) ? 0 : symtab_hdr->sh_info;

  if (r_symndx < extsymoff)
    return TRUE;
  if (elf_bad_symtab (input_bfd) && local_sections[r_symndx] != NULL)
    return TRUE;

  if (check_forced)
    {
      /* Look up the hash table to check whether the symbol
 	 was forced local.  */
      h = (struct mips_elf_link_hash_entry *)
	elf_sym_hashes (input_bfd) [r_symndx - extsymoff];
      /* Find the real hash-table entry for this symbol.  */
      while (h->root.root.type == bfd_link_hash_indirect
 	     || h->root.root.type == bfd_link_hash_warning)
	h = (struct mips_elf_link_hash_entry *) h->root.root.u.i.link;
      if (h->root.forced_local)
	return TRUE;
    }

  return FALSE;
}

/* Sign-extend VALUE, which has the indicated number of BITS.  */

bfd_vma
_bfd_mips_elf_sign_extend (bfd_vma value, int bits)
{
  if (value & ((bfd_vma) 1 << (bits - 1)))
    /* VALUE is negative.  */
    value |= ((bfd_vma) - 1) << bits;

  return value;
}

/* Return non-zero if the indicated VALUE has overflowed the maximum
   range expressible by a signed number with the indicated number of
   BITS.  */

static bfd_boolean
mips_elf_overflow_p (bfd_vma value, int bits)
{
  bfd_signed_vma svalue = (bfd_signed_vma) value;

  if (svalue > (1 << (bits - 1)) - 1)
    /* The value is too big.  */
    return TRUE;
  else if (svalue < -(1 << (bits - 1)))
    /* The value is too small.  */
    return TRUE;

  /* All is well.  */
  return FALSE;
}

/* Calculate the %high function.  */

static bfd_vma
mips_elf_high (bfd_vma value)
{
  return ((value + (bfd_vma) 0x8000) >> 16) & 0xffff;
}

/* Calculate the %higher function.  */

static bfd_vma
mips_elf_higher (bfd_vma value ATTRIBUTE_UNUSED)
{
#ifdef BFD64
  return ((value + (bfd_vma) 0x80008000) >> 32) & 0xffff;
#else
  abort ();
  return MINUS_ONE;
#endif
}

/* Calculate the %highest function.  */

static bfd_vma
mips_elf_highest (bfd_vma value ATTRIBUTE_UNUSED)
{
#ifdef BFD64
  return ((value + (((bfd_vma) 0x8000 << 32) | 0x80008000)) >> 48) & 0xffff;
#else
  abort ();
  return MINUS_ONE;
#endif
}

/* Create the .compact_rel section.  */

static bfd_boolean
mips_elf_create_compact_rel_section
  (bfd *abfd, struct bfd_link_info *info ATTRIBUTE_UNUSED)
{
  flagword flags;
  register asection *s;

  if (bfd_get_section_by_name (abfd, ".compact_rel") == NULL)
    {
      flags = (SEC_HAS_CONTENTS | SEC_IN_MEMORY | SEC_LINKER_CREATED
	       | SEC_READONLY);

      s = bfd_make_section_with_flags (abfd, ".compact_rel", flags);
      if (s == NULL
	  || ! bfd_set_section_alignment (abfd, s,
					  MIPS_ELF_LOG_FILE_ALIGN (abfd)))
	return FALSE;

      s->size = sizeof (Elf32_External_compact_rel);
    }

  return TRUE;
}

/* Create the .got section to hold the global offset table.  */

static bfd_boolean
mips_elf_create_got_section (bfd *abfd, struct bfd_link_info *info,
			     bfd_boolean maybe_exclude)
{
  flagword flags;
  register asection *s;
  struct elf_link_hash_entry *h;
  struct bfd_link_hash_entry *bh;
  struct mips_got_info *g;
  bfd_size_type amt;
  struct mips_elf_link_hash_table *htab;

  htab = mips_elf_hash_table (info);

  /* This function may be called more than once.  */
  s = mips_elf_got_section (abfd, TRUE);
  if (s)
    {
      if (! maybe_exclude)
	s->flags &= ~SEC_EXCLUDE;
      return TRUE;
    }

  flags = (SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS | SEC_IN_MEMORY
	   | SEC_LINKER_CREATED);

  if (maybe_exclude)
    flags |= SEC_EXCLUDE;

  /* We have to use an alignment of 2**4 here because this is hardcoded
     in the function stub generation and in the linker script.  */
  s = bfd_make_section_with_flags (abfd, ".got", flags);
  if (s == NULL
      || ! bfd_set_section_alignment (abfd, s, 4))
    return FALSE;

  /* Define the symbol _GLOBAL_OFFSET_TABLE_.  We don't do this in the
     linker script because we don't want to define the symbol if we
     are not creating a global offset table.  */
  bh = NULL;
  if (! (_bfd_generic_link_add_one_symbol
	 (info, abfd, "_GLOBAL_OFFSET_TABLE_", BSF_GLOBAL, s,
	  0, NULL, FALSE, get_elf_backend_data (abfd)->collect, &bh)))
    return FALSE;

  h = (struct elf_link_hash_entry *) bh;
  h->non_elf = 0;
  h->def_regular = 1;
  h->type = STT_OBJECT;
  elf_hash_table (info)->hgot = h;

  if (info->shared
      && ! bfd_elf_link_record_dynamic_symbol (info, h))
    return FALSE;

  amt = sizeof (struct mips_got_info);
  g = bfd_alloc (abfd, amt);
  if (g == NULL)
    return FALSE;
  g->global_gotsym = NULL;
  g->global_gotno = 0;
  g->tls_gotno = 0;
  g->local_gotno = MIPS_RESERVED_GOTNO (info);
  g->assigned_gotno = MIPS_RESERVED_GOTNO (info);
  g->bfd2got = NULL;
  g->next = NULL;
  g->tls_ldm_offset = MINUS_ONE;
  g->got_entries = htab_try_create (1, mips_elf_got_entry_hash,
				    mips_elf_got_entry_eq, NULL);
  if (g->got_entries == NULL)
    return FALSE;
  mips_elf_section_data (s)->u.got_info = g;
  mips_elf_section_data (s)->elf.this_hdr.sh_flags
    |= SHF_ALLOC | SHF_WRITE | SHF_MIPS_GPREL;

  /* VxWorks also needs a .got.plt section.  */
  if (htab->is_vxworks)
    {
      s = bfd_make_section_with_flags (abfd, ".got.plt",
				       SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS
				       | SEC_IN_MEMORY | SEC_LINKER_CREATED);
      if (s == NULL || !bfd_set_section_alignment (abfd, s, 4))
	return FALSE;

      htab->sgotplt = s;
    }
  return TRUE;
}

/* Return true if H refers to the special VxWorks __GOTT_BASE__ or
   __GOTT_INDEX__ symbols.  These symbols are only special for
   shared objects; they are not used in executables.  */

static bfd_boolean
is_gott_symbol (struct bfd_link_info *info, struct elf_link_hash_entry *h)
{
  return (mips_elf_hash_table (info)->is_vxworks
	  && info->shared
	  && (strcmp (h->root.root.string, "__GOTT_BASE__") == 0
	      || strcmp (h->root.root.string, "__GOTT_INDEX__") == 0));
}

/* Calculate the value produced by the RELOCATION (which comes from
   the INPUT_BFD).  The ADDEND is the addend to use for this
   RELOCATION; RELOCATION->R_ADDEND is ignored.

   The result of the relocation calculation is stored in VALUEP.
   REQUIRE_JALXP indicates whether or not the opcode used with this
   relocation must be JALX.

   This function returns bfd_reloc_continue if the caller need take no
   further action regarding this relocation, bfd_reloc_notsupported if
   something goes dramatically wrong, bfd_reloc_overflow if an
   overflow occurs, and bfd_reloc_ok to indicate success.  */

static bfd_reloc_status_type
mips_elf_calculate_relocation (bfd *abfd, bfd *input_bfd,
			       asection *input_section,
			       struct bfd_link_info *info,
			       const Elf_Internal_Rela *relocation,
			       bfd_vma addend, reloc_howto_type *howto,
			       Elf_Internal_Sym *local_syms,
			       asection **local_sections, bfd_vma *valuep,
			       const char **namep, bfd_boolean *require_jalxp,
			       bfd_boolean save_addend)
{
  /* The eventual value we will return.  */
  bfd_vma value;
  /* The address of the symbol against which the relocation is
     occurring.  */
  bfd_vma symbol = 0;
  /* The final GP value to be used for the relocatable, executable, or
     shared object file being produced.  */
  bfd_vma gp = MINUS_ONE;
  /* The place (section offset or address) of the storage unit being
     relocated.  */
  bfd_vma p;
  /* The value of GP used to create the relocatable object.  */
  bfd_vma gp0 = MINUS_ONE;
  /* The offset into the global offset table at which the address of
     the relocation entry symbol, adjusted by the addend, resides
     during execution.  */
  bfd_vma g = MINUS_ONE;
  /* The section in which the symbol referenced by the relocation is
     located.  */
  asection *sec = NULL;
  struct mips_elf_link_hash_entry *h = NULL;
  /* TRUE if the symbol referred to by this relocation is a local
     symbol.  */
  bfd_boolean local_p, was_local_p;
  /* TRUE if the symbol referred to by this relocation is "_gp_disp".  */
  bfd_boolean gp_disp_p = FALSE;
  /* TRUE if the symbol referred to by this relocation is
     "__gnu_local_gp".  */
  bfd_boolean gnu_local_gp_p = FALSE;
  Elf_Internal_Shdr *symtab_hdr;
  size_t extsymoff;
  unsigned long r_symndx;
  int r_type;
  /* TRUE if overflow occurred during the calculation of the
     relocation value.  */
  bfd_boolean overflowed_p;
  /* TRUE if this relocation refers to a MIPS16 function.  */
  bfd_boolean target_is_16_bit_code_p = FALSE;
  struct mips_elf_link_hash_table *htab;
  bfd *dynobj;

  dynobj = elf_hash_table (info)->dynobj;
  htab = mips_elf_hash_table (info);

  /* Parse the relocation.  */
  r_symndx = ELF_R_SYM (input_bfd, relocation->r_info);
  r_type = ELF_R_TYPE (input_bfd, relocation->r_info);
  p = (input_section->output_section->vma
       + input_section->output_offset
       + relocation->r_offset);

  /* Assume that there will be no overflow.  */
  overflowed_p = FALSE;

  /* Figure out whether or not the symbol is local, and get the offset
     used in the array of hash table entries.  */
  symtab_hdr = &elf_tdata (input_bfd)->symtab_hdr;
  local_p = mips_elf_local_relocation_p (input_bfd, relocation,
					 local_sections, FALSE);
  was_local_p = local_p;
  if (! elf_bad_symtab (input_bfd))
    extsymoff = symtab_hdr->sh_info;
  else
    {
      /* The symbol table does not follow the rule that local symbols
	 must come before globals.  */
      extsymoff = 0;
    }

  /* Figure out the value of the symbol.  */
  if (local_p)
    {
      Elf_Internal_Sym *sym;

      sym = local_syms + r_symndx;
      sec = local_sections[r_symndx];

      symbol = sec->output_section->vma + sec->output_offset;
      if (ELF_ST_TYPE (sym->st_info) != STT_SECTION
	  || (sec->flags & SEC_MERGE))
	symbol += sym->st_value;
      if ((sec->flags & SEC_MERGE)
	  && ELF_ST_TYPE (sym->st_info) == STT_SECTION)
	{
	  addend = _bfd_elf_rel_local_sym (abfd, sym, &sec, addend);
	  addend -= symbol;
	  addend += sec->output_section->vma + sec->output_offset;
	}

      /* MIPS16 text labels should be treated as odd.  */
      if (sym->st_other == STO_MIPS16)
	++symbol;

      /* Record the name of this symbol, for our caller.  */
      *namep = bfd_elf_string_from_elf_section (input_bfd,
						symtab_hdr->sh_link,
						sym->st_name);
      if (*namep == '\0')
	*namep = bfd_section_name (input_bfd, sec);

      target_is_16_bit_code_p = (sym->st_other == STO_MIPS16);
    }
  else
    {
      /* ??? Could we use RELOC_FOR_GLOBAL_SYMBOL here ?  */

      /* For global symbols we look up the symbol in the hash-table.  */
      h = ((struct mips_elf_link_hash_entry *)
	   elf_sym_hashes (input_bfd) [r_symndx - extsymoff]);
      /* Find the real hash-table entry for this symbol.  */
      while (h->root.root.type == bfd_link_hash_indirect
	     || h->root.root.type == bfd_link_hash_warning)
	h = (struct mips_elf_link_hash_entry *) h->root.root.u.i.link;

      /* Record the name of this symbol, for our caller.  */
      *namep = h->root.root.root.string;

      /* See if this is the special _gp_disp symbol.  Note that such a
	 symbol must always be a global symbol.  */
      if (strcmp (*namep, "_gp_disp") == 0
	  && ! NEWABI_P (input_bfd))
	{
	  /* Relocations against _gp_disp are permitted only with
	     R_MIPS_HI16 and R_MIPS_LO16 relocations.  */
	  if (r_type != R_MIPS_HI16 && r_type != R_MIPS_LO16
	      && r_type != R_MIPS16_HI16 && r_type != R_MIPS16_LO16)
	    return bfd_reloc_notsupported;

	  gp_disp_p = TRUE;
	}
      /* See if this is the special _gp symbol.  Note that such a
	 symbol must always be a global symbol.  */
      else if (strcmp (*namep, "__gnu_local_gp") == 0)
	gnu_local_gp_p = TRUE;


      /* If this symbol is defined, calculate its address.  Note that
	 _gp_disp is a magic symbol, always implicitly defined by the
	 linker, so it's inappropriate to check to see whether or not
	 its defined.  */
      else if ((h->root.root.type == bfd_link_hash_defined
		|| h->root.root.type == bfd_link_hash_defweak)
	       && h->root.root.u.def.section)
	{
	  sec = h->root.root.u.def.section;
	  if (sec->output_section)
	    symbol = (h->root.root.u.def.value
		      + sec->output_section->vma
		      + sec->output_offset);
	  else
	    symbol = h->root.root.u.def.value;
	}
      else if (h->root.root.type == bfd_link_hash_undefweak)
	/* We allow relocations against undefined weak symbols, giving
	   it the value zero, so that you can undefined weak functions
	   and check to see if they exist by looking at their
	   addresses.  */
	symbol = 0;
      else if (info->unresolved_syms_in_objects == RM_IGNORE
	       && ELF_ST_VISIBILITY (h->root.other) == STV_DEFAULT)
	symbol = 0;
      else if (strcmp (*namep, SGI_COMPAT (input_bfd)
		       ? "_DYNAMIC_LINK" : "_DYNAMIC_LINKING") == 0)
	{
	  /* If this is a dynamic link, we should have created a
	     _DYNAMIC_LINK symbol or _DYNAMIC_LINKING(for normal mips) symbol
	     in in _bfd_mips_elf_create_dynamic_sections.
	     Otherwise, we should define the symbol with a value of 0.
	     FIXME: It should probably get into the symbol table
	     somehow as well.  */
	  BFD_ASSERT (! info->shared);
	  BFD_ASSERT (bfd_get_section_by_name (abfd, ".dynamic") == NULL);
	  symbol = 0;
	}
      else if (ELF_MIPS_IS_OPTIONAL (h->root.other))
	{
	  /* This is an optional symbol - an Irix specific extension to the
	     ELF spec.  Ignore it for now.
	     XXX - FIXME - there is more to the spec for OPTIONAL symbols
	     than simply ignoring them, but we do not handle this for now.
	     For information see the "64-bit ELF Object File Specification"
	     which is available from here:
	     http://techpubs.sgi.com/library/manuals/4000/007-4658-001/pdf/007-4658-001.pdf  */
	  symbol = 0;
	}
      else
	{
	  if (! ((*info->callbacks->undefined_symbol)
		 (info, h->root.root.root.string, input_bfd,
		  input_section, relocation->r_offset,
		  (info->unresolved_syms_in_objects == RM_GENERATE_ERROR)
		   || ELF_ST_VISIBILITY (h->root.other))))
	    return bfd_reloc_undefined;
	  symbol = 0;
	}

      target_is_16_bit_code_p = (h->root.other == STO_MIPS16);
    }

  /* If this is a 32- or 64-bit call to a 16-bit function with a stub, we
     need to redirect the call to the stub, unless we're already *in*
     a stub.  */
  if (r_type != R_MIPS16_26 && !info->relocatable
      && ((h != NULL && h->fn_stub != NULL)
	  || (local_p
	      && elf_tdata (input_bfd)->local_stubs != NULL
	      && elf_tdata (input_bfd)->local_stubs[r_symndx] != NULL))
      && !mips16_stub_section_p (input_bfd, input_section))
    {
      /* This is a 32- or 64-bit call to a 16-bit function.  We should
	 have already noticed that we were going to need the
	 stub.  */
      if (local_p)
	sec = elf_tdata (input_bfd)->local_stubs[r_symndx];
      else
	{
	  BFD_ASSERT (h->need_fn_stub);
	  sec = h->fn_stub;
	}

      symbol = sec->output_section->vma + sec->output_offset;
      /* The target is 16-bit, but the stub isn't.  */
      target_is_16_bit_code_p = FALSE;
    }
  /* If this is a 16-bit call to a 32- or 64-bit function with a stub, we
     need to redirect the call to the stub.  */
  else if (r_type == R_MIPS16_26 && !info->relocatable
	   && ((h != NULL && (h->call_stub != NULL || h->call_fp_stub != NULL))
	       || (local_p
		   && elf_tdata (input_bfd)->local_call_stubs != NULL
		   && elf_tdata (input_bfd)->local_call_stubs[r_symndx] != NULL))
	   && !target_is_16_bit_code_p)
    {
      if (local_p)
	sec = elf_tdata (input_bfd)->local_call_stubs[r_symndx];
      else
	{
	  /* If both call_stub and call_fp_stub are defined, we can figure
	     out which one to use by checking which one appears in the input
	     file.  */
	  if (h->call_stub != NULL && h->call_fp_stub != NULL)
	    {
	      asection *o;
	      
	      sec = NULL;
	      for (o = input_bfd->sections; o != NULL; o = o->next)
		{
		  if (CALL_FP_STUB_P (bfd_get_section_name (input_bfd, o)))
		    {
		      sec = h->call_fp_stub;
		      break;
		    }
		}
	      if (sec == NULL)
		sec = h->call_stub;
	    }
	  else if (h->call_stub != NULL)
	    sec = h->call_stub;
	  else
	    sec = h->call_fp_stub;
  	}

      BFD_ASSERT (sec->size > 0);
      symbol = sec->output_section->vma + sec->output_offset;
    }

  /* Calls from 16-bit code to 32-bit code and vice versa require the
     special jalx instruction.  */
  *require_jalxp = (!info->relocatable
                    && (((r_type == R_MIPS16_26) && !target_is_16_bit_code_p)
                        || ((r_type == R_MIPS_26) && target_is_16_bit_code_p)));

  local_p = mips_elf_local_relocation_p (input_bfd, relocation,
					 local_sections, TRUE);

  /* If we haven't already determined the GOT offset, or the GP value,
     and we're going to need it, get it now.  */
  switch (r_type)
    {
    case R_MIPS_GOT_PAGE:
    case R_MIPS_GOT_OFST:
      /* We need to decay to GOT_DISP/addend if the symbol doesn't
	 bind locally.  */
      local_p = local_p || _bfd_elf_symbol_refs_local_p (&h->root, info, 1);
      if (local_p || r_type == R_MIPS_GOT_OFST)
	break;
      /* Fall through.  */

    case R_MIPS_CALL16:
    case R_MIPS_GOT16:
    case R_MIPS_GOT_DISP:
    case R_MIPS_GOT_HI16:
    case R_MIPS_CALL_HI16:
    case R_MIPS_GOT_LO16:
    case R_MIPS_CALL_LO16:
    case R_MIPS_TLS_GD:
    case R_MIPS_TLS_GOTTPREL:
    case R_MIPS_TLS_LDM:
      /* Find the index into the GOT where this value is located.  */
      if (r_type == R_MIPS_TLS_LDM)
	{
	  g = mips_elf_local_got_index (abfd, input_bfd, info,
					0, 0, NULL, r_type);
	  if (g == MINUS_ONE)
	    return bfd_reloc_outofrange;
	}
      else if (!local_p)
	{
	  /* On VxWorks, CALL relocations should refer to the .got.plt
	     entry, which is initialized to point at the PLT stub.  */
	  if (htab->is_vxworks
	      && (r_type == R_MIPS_CALL_HI16
		  || r_type == R_MIPS_CALL_LO16
		  || r_type == R_MIPS_CALL16))
	    {
	      BFD_ASSERT (addend == 0);
	      BFD_ASSERT (h->root.needs_plt);
	      g = mips_elf_gotplt_index (info, &h->root);
	    }
	  else
	    {
	      /* GOT_PAGE may take a non-zero addend, that is ignored in a
		 GOT_PAGE relocation that decays to GOT_DISP because the
		 symbol turns out to be global.  The addend is then added
		 as GOT_OFST.  */
	      BFD_ASSERT (addend == 0 || r_type == R_MIPS_GOT_PAGE);
	      g = mips_elf_global_got_index (dynobj, input_bfd,
					     &h->root, r_type, info);
	      if (h->tls_type == GOT_NORMAL
		  && (! elf_hash_table(info)->dynamic_sections_created
		      || (info->shared
			  && (info->symbolic || h->root.forced_local)
			  && h->root.def_regular)))
		{
		  /* This is a static link or a -Bsymbolic link.  The
		     symbol is defined locally, or was forced to be local.
		     We must initialize this entry in the GOT.  */
		  asection *sgot = mips_elf_got_section (dynobj, FALSE);
		  MIPS_ELF_PUT_WORD (dynobj, symbol, sgot->contents + g);
		}
	    }
	}
      else if (!htab->is_vxworks
	       && (r_type == R_MIPS_CALL16 || (r_type == R_MIPS_GOT16)))
	/* The calculation below does not involve "g".  */
	break;
      else
	{
	  g = mips_elf_local_got_index (abfd, input_bfd, info,
					symbol + addend, r_symndx, h, r_type);
	  if (g == MINUS_ONE)
	    return bfd_reloc_outofrange;
	}

      /* Convert GOT indices to actual offsets.  */
      g = mips_elf_got_offset_from_index (dynobj, abfd, input_bfd, g);
      break;

    case R_MIPS_HI16:
    case R_MIPS_LO16:
    case R_MIPS_GPREL16:
    case R_MIPS_GPREL32:
    case R_MIPS_LITERAL:
    case R_MIPS16_HI16:
    case R_MIPS16_LO16:
    case R_MIPS16_GPREL:
      gp0 = _bfd_get_gp_value (input_bfd);
      gp = _bfd_get_gp_value (abfd);
      if (dynobj)
	gp += mips_elf_adjust_gp (abfd, mips_elf_got_info (dynobj, NULL),
				  input_bfd);
      break;

    default:
      break;
    }

  if (gnu_local_gp_p)
    symbol = gp;

  /* Relocations against the VxWorks __GOTT_BASE__ and __GOTT_INDEX__
     symbols are resolved by the loader.  Add them to .rela.dyn.  */
  if (h != NULL && is_gott_symbol (info, &h->root))
    {
      Elf_Internal_Rela outrel;
      bfd_byte *loc;
      asection *s;

      s = mips_elf_rel_dyn_section (info, FALSE);
      loc = s->contents + s->reloc_count++ * sizeof (Elf32_External_Rela);

      outrel.r_offset = (input_section->output_section->vma
			 + input_section->output_offset
			 + relocation->r_offset);
      outrel.r_info = ELF32_R_INFO (h->root.dynindx, r_type);
      outrel.r_addend = addend;
      bfd_elf32_swap_reloca_out (abfd, &outrel, loc);

      /* If we've written this relocation for a readonly section,
	 we need to set DF_TEXTREL again, so that we do not delete the
	 DT_TEXTREL tag.  */
      if (MIPS_ELF_READONLY_SECTION (input_section))
	info->flags |= DF_TEXTREL;

      *valuep = 0;
      return bfd_reloc_ok;
    }

  /* Figure out what kind of relocation is being performed.  */
  switch (r_type)
    {
    case R_MIPS_NONE:
      return bfd_reloc_continue;

    case R_MIPS_16:
      value = symbol + _bfd_mips_elf_sign_extend (addend, 16);
      overflowed_p = mips_elf_overflow_p (value, 16);
      break;

    case R_MIPS_32:
    case R_MIPS_REL32:
    case R_MIPS_64:
      if ((info->shared
	   || (!htab->is_vxworks
	       && htab->root.dynamic_sections_created
	       && h != NULL
	       && h->root.def_dynamic
	       && !h->root.def_regular))
	  && r_symndx != 0
	  && (input_section->flags & SEC_ALLOC) != 0)
	{
	  /* If we're creating a shared library, or this relocation is
	     against a symbol in a shared library, then we can't know
	     where the symbol will end up.  So, we create a relocation
	     record in the output, and leave the job up to the dynamic
	     linker.

	     In VxWorks executables, references to external symbols
	     are handled using copy relocs or PLT stubs, so there's
	     no need to add a dynamic relocation here.  */
	  value = addend;
	  if (!mips_elf_create_dynamic_relocation (abfd,
						   info,
						   relocation,
						   h,
						   sec,
						   symbol,
						   &value,
						   input_section))
	    return bfd_reloc_undefined;
	}
      else
	{
	  if (r_type != R_MIPS_REL32)
	    value = symbol + addend;
	  else
	    value = addend;
	}
      value &= howto->dst_mask;
      break;

    case R_MIPS_PC32:
      value = symbol + addend - p;
      value &= howto->dst_mask;
      break;

    case R_MIPS16_26:
      /* The calculation for R_MIPS16_26 is just the same as for an
	 R_MIPS_26.  It's only the storage of the relocated field into
	 the output file that's different.  That's handled in
	 mips_elf_perform_relocation.  So, we just fall through to the
	 R_MIPS_26 case here.  */
    case R_MIPS_26:
      if (local_p)
	value = ((addend | ((p + 4) & 0xf0000000)) + symbol) >> 2;
      else
	{
	  value = (_bfd_mips_elf_sign_extend (addend, 28) + symbol) >> 2;
	  if (h->root.root.type != bfd_link_hash_undefweak)
	    overflowed_p = (value >> 26) != ((p + 4) >> 28);
	}
      value &= howto->dst_mask;
      break;

    case R_MIPS_TLS_DTPREL_HI16:
      value = (mips_elf_high (addend + symbol - dtprel_base (info))
	       & howto->dst_mask);
      break;

    case R_MIPS_TLS_DTPREL_LO16:
    case R_MIPS_TLS_DTPREL32:
    case R_MIPS_TLS_DTPREL64:
      value = (symbol + addend - dtprel_base (info)) & howto->dst_mask;
      break;

    case R_MIPS_TLS_TPREL_HI16:
      value = (mips_elf_high (addend + symbol - tprel_base (info))
	       & howto->dst_mask);
      break;

    case R_MIPS_TLS_TPREL_LO16:
      value = (symbol + addend - tprel_base (info)) & howto->dst_mask;
      break;

    case R_MIPS_HI16:
    case R_MIPS16_HI16:
      if (!gp_disp_p)
	{
	  value = mips_elf_high (addend + symbol);
	  value &= howto->dst_mask;
	}
      else
	{
	  /* For MIPS16 ABI code we generate this sequence
	        0: li      $v0,%hi(_gp_disp)
	        4: addiupc $v1,%lo(_gp_disp)
	        8: sll     $v0,16
	       12: addu    $v0,$v1
	       14: move    $gp,$v0
	     So the offsets of hi and lo relocs are the same, but the
	     $pc is four higher than $t9 would be, so reduce
	     both reloc addends by 4. */
	  if (r_type == R_MIPS16_HI16)
	    value = mips_elf_high (addend + gp - p - 4);
	  else
	    value = mips_elf_high (addend + gp - p);
	  overflowed_p = mips_elf_overflow_p (value, 16);
	}
      break;

    case R_MIPS_LO16:
    case R_MIPS16_LO16:
      if (!gp_disp_p)
	value = (symbol + addend) & howto->dst_mask;
      else
	{
	  /* See the comment for R_MIPS16_HI16 above for the reason
	     for this conditional.  */
	  if (r_type == R_MIPS16_LO16)
	    value = addend + gp - p;
	  else
	    value = addend + gp - p + 4;
	  /* The MIPS ABI requires checking the R_MIPS_LO16 relocation
	     for overflow.  But, on, say, IRIX5, relocations against
	     _gp_disp are normally generated from the .cpload
	     pseudo-op.  It generates code that normally looks like
	     this:

	       lui    $gp,%hi(_gp_disp)
	       addiu  $gp,$gp,%lo(_gp_disp)
	       addu   $gp,$gp,$t9

	     Here $t9 holds the address of the function being called,
	     as required by the MIPS ELF ABI.  The R_MIPS_LO16
	     relocation can easily overflow in this situation, but the
	     R_MIPS_HI16 relocation will handle the overflow.
	     Therefore, we consider this a bug in the MIPS ABI, and do
	     not check for overflow here.  */
	}
      break;

    case R_MIPS_LITERAL:
      /* Because we don't merge literal sections, we can handle this
	 just like R_MIPS_GPREL16.  In the long run, we should merge
	 shared literals, and then we will need to additional work
	 here.  */

      /* Fall through.  */

    case R_MIPS16_GPREL:
      /* The R_MIPS16_GPREL performs the same calculation as
	 R_MIPS_GPREL16, but stores the relocated bits in a different
	 order.  We don't need to do anything special here; the
	 differences are handled in mips_elf_perform_relocation.  */
    case R_MIPS_GPREL16:
      /* Only sign-extend the addend if it was extracted from the
	 instruction.  If the addend was separate, leave it alone,
	 otherwise we may lose significant bits.  */
      if (howto->partial_inplace)
	addend = _bfd_mips_elf_sign_extend (addend, 16);
      value = symbol + addend - gp;
      /* If the symbol was local, any earlier relocatable links will
	 have adjusted its addend with the gp offset, so compensate
	 for that now.  Don't do it for symbols forced local in this
	 link, though, since they won't have had the gp offset applied
	 to them before.  */
      if (was_local_p)
	value += gp0;
      overflowed_p = mips_elf_overflow_p (value, 16);
      break;

    case R_MIPS_GOT16:
    case R_MIPS_CALL16:
      /* VxWorks does not have separate local and global semantics for
	 R_MIPS_GOT16; every relocation evaluates to "G".  */
      if (!htab->is_vxworks && local_p)
	{
	  bfd_boolean forced;

	  forced = ! mips_elf_local_relocation_p (input_bfd, relocation,
						  local_sections, FALSE);
	  value = mips_elf_got16_entry (abfd, input_bfd, info,
					symbol + addend, forced);
	  if (value == MINUS_ONE)
	    return bfd_reloc_outofrange;
	  value
	    = mips_elf_got_offset_from_index (dynobj, abfd, input_bfd, value);
	  overflowed_p = mips_elf_overflow_p (value, 16);
	  break;
	}

      /* Fall through.  */

    case R_MIPS_TLS_GD:
    case R_MIPS_TLS_GOTTPREL:
    case R_MIPS_TLS_LDM:
    case R_MIPS_GOT_DISP:
    got_disp:
      value = g;
      overflowed_p = mips_elf_overflow_p (value, 16);
      break;

    case R_MIPS_GPREL32:
      value = (addend + symbol + gp0 - gp);
      if (!save_addend)
	value &= howto->dst_mask;
      break;

    case R_MIPS_PC16:
    case R_MIPS_GNU_REL16_S2:
      value = symbol + _bfd_mips_elf_sign_extend (addend, 18) - p;
      overflowed_p = mips_elf_overflow_p (value, 18);
      value >>= howto->rightshift;
      value &= howto->dst_mask;
      break;

    case R_MIPS_GOT_HI16:
    case R_MIPS_CALL_HI16:
      /* We're allowed to handle these two relocations identically.
	 The dynamic linker is allowed to handle the CALL relocations
	 differently by creating a lazy evaluation stub.  */
      value = g;
      value = mips_elf_high (value);
      value &= howto->dst_mask;
      break;

    case R_MIPS_GOT_LO16:
    case R_MIPS_CALL_LO16:
      value = g & howto->dst_mask;
      break;

    case R_MIPS_GOT_PAGE:
      /* GOT_PAGE relocations that reference non-local symbols decay
	 to GOT_DISP.  The corresponding GOT_OFST relocation decays to
	 0.  */
      if (! local_p)
	goto got_disp;
      value = mips_elf_got_page (abfd, input_bfd, info, symbol + addend, NULL);
      if (value == MINUS_ONE)
	return bfd_reloc_outofrange;
      value = mips_elf_got_offset_from_index (dynobj, abfd, input_bfd, value);
      overflowed_p = mips_elf_overflow_p (value, 16);
      break;

    case R_MIPS_GOT_OFST:
      if (local_p)
	mips_elf_got_page (abfd, input_bfd, info, symbol + addend, &value);
      else
	value = addend;
      overflowed_p = mips_elf_overflow_p (value, 16);
      break;

    case R_MIPS_SUB:
      value = symbol - addend;
      value &= howto->dst_mask;
      break;

    case R_MIPS_HIGHER:
      value = mips_elf_higher (addend + symbol);
      value &= howto->dst_mask;
      break;

    case R_MIPS_HIGHEST:
      value = mips_elf_highest (addend + symbol);
      value &= howto->dst_mask;
      break;

    case R_MIPS_SCN_DISP:
      value = symbol + addend - sec->output_offset;
      value &= howto->dst_mask;
      break;

    case R_MIPS_JALR:
      /* This relocation is only a hint.  In some cases, we optimize
	 it into a bal instruction.  But we don't try to optimize
	 branches to the PLT; that will wind up wasting time.  */
      if (h != NULL && h->root.plt.offset != (bfd_vma) -1)
	return bfd_reloc_continue;
      value = symbol + addend;
      break;

    case R_MIPS_PJUMP:
    case R_MIPS_GNU_VTINHERIT:
    case R_MIPS_GNU_VTENTRY:
      /* We don't do anything with these at present.  */
      return bfd_reloc_continue;

    default:
      /* An unrecognized relocation type.  */
      return bfd_reloc_notsupported;
    }

  /* Store the VALUE for our caller.  */
  *valuep = value;
  return overflowed_p ? bfd_reloc_overflow : bfd_reloc_ok;
}

/* Obtain the field relocated by RELOCATION.  */

static bfd_vma
mips_elf_obtain_contents (reloc_howto_type *howto,
			  const Elf_Internal_Rela *relocation,
			  bfd *input_bfd, bfd_byte *contents)
{
  bfd_vma x;
  bfd_byte *location = contents + relocation->r_offset;

  /* Obtain the bytes.  */
  x = bfd_get ((8 * bfd_get_reloc_size (howto)), input_bfd, location);

  return x;
}

/* It has been determined that the result of the RELOCATION is the
   VALUE.  Use HOWTO to place VALUE into the output file at the
   appropriate position.  The SECTION is the section to which the
   relocation applies.  If REQUIRE_JALX is TRUE, then the opcode used
   for the relocation must be either JAL or JALX, and it is
   unconditionally converted to JALX.

   Returns FALSE if anything goes wrong.  */

static bfd_boolean
mips_elf_perform_relocation (struct bfd_link_info *info,
			     reloc_howto_type *howto,
			     const Elf_Internal_Rela *relocation,
			     bfd_vma value, bfd *input_bfd,
			     asection *input_section, bfd_byte *contents,
			     bfd_boolean require_jalx)
{
  bfd_vma x;
  bfd_byte *location;
  int r_type = ELF_R_TYPE (input_bfd, relocation->r_info);

  /* Figure out where the relocation is occurring.  */
  location = contents + relocation->r_offset;

  _bfd_mips16_elf_reloc_unshuffle (input_bfd, r_type, FALSE, location);

  /* Obtain the current value.  */
  x = mips_elf_obtain_contents (howto, relocation, input_bfd, contents);

  /* Clear the field we are setting.  */
  x &= ~howto->dst_mask;

  /* Set the field.  */
  x |= (value & howto->dst_mask);

  /* If required, turn JAL into JALX.  */
  if (require_jalx)
    {
      bfd_boolean ok;
      bfd_vma opcode = x >> 26;
      bfd_vma jalx_opcode;

      /* Check to see if the opcode is already JAL or JALX.  */
      if (r_type == R_MIPS16_26)
	{
	  ok = ((opcode == 0x6) || (opcode == 0x7));
	  jalx_opcode = 0x7;
	}
      else
	{
	  ok = ((opcode == 0x3) || (opcode == 0x1d));
	  jalx_opcode = 0x1d;
	}

      /* If the opcode is not JAL or JALX, there's a problem.  */
      if (!ok)
	{
	  (*_bfd_error_handler)
	    (_("%B: %A+0x%lx: jump to stub routine which is not jal"),
	     input_bfd,
	     input_section,
	     (unsigned long) relocation->r_offset);
	  bfd_set_error (bfd_error_bad_value);
	  return FALSE;
	}

      /* Make this the JALX opcode.  */
      x = (x & ~(0x3f << 26)) | (jalx_opcode << 26);
    }

  /* On the RM9000, bal is faster than jal, because bal uses branch
     prediction hardware.  If we are linking for the RM9000, and we
     see jal, and bal fits, use it instead.  Note that this
     transformation should be safe for all architectures.  */
  if (bfd_get_mach (input_bfd) == bfd_mach_mips9000
      && !info->relocatable
      && !require_jalx
      && ((r_type == R_MIPS_26 && (x >> 26) == 0x3)	    /* jal addr */
	  || (r_type == R_MIPS_JALR && x == 0x0320f809)))   /* jalr t9 */
    {
      bfd_vma addr;
      bfd_vma dest;
      bfd_signed_vma off;

      addr = (input_section->output_section->vma
	      + input_section->output_offset
	      + relocation->r_offset
	      + 4);
      if (r_type == R_MIPS_26)
	dest = (value << 2) | ((addr >> 28) << 28);
      else
	dest = value;
      off = dest - addr;
      if (off <= 0x1ffff && off >= -0x20000)
	x = 0x04110000 | (((bfd_vma) off >> 2) & 0xffff);   /* bal addr */
    }

  /* Put the value into the output.  */
  bfd_put (8 * bfd_get_reloc_size (howto), input_bfd, x, location);

  _bfd_mips16_elf_reloc_shuffle(input_bfd, r_type, !info->relocatable,
				location);

  return TRUE;
}

/* Returns TRUE if SECTION is a MIPS16 stub section.  */

static bfd_boolean
mips16_stub_section_p (bfd *abfd ATTRIBUTE_UNUSED, asection *section)
{
  const char *name = bfd_get_section_name (abfd, section);

  return FN_STUB_P (name) || CALL_STUB_P (name) || CALL_FP_STUB_P (name);
}

/* Add room for N relocations to the .rel(a).dyn section in ABFD.  */

static void
mips_elf_allocate_dynamic_relocations (bfd *abfd, struct bfd_link_info *info,
				       unsigned int n)
{
  asection *s;
  struct mips_elf_link_hash_table *htab;

  htab = mips_elf_hash_table (info);
  s = mips_elf_rel_dyn_section (info, FALSE);
  BFD_ASSERT (s != NULL);

  if (htab->is_vxworks)
    s->size += n * MIPS_ELF_RELA_SIZE (abfd);
  else
    {
      if (s->size == 0)
	{
	  /* Make room for a null element.  */
	  s->size += MIPS_ELF_REL_SIZE (abfd);
	  ++s->reloc_count;
	}
      s->size += n * MIPS_ELF_REL_SIZE (abfd);
    }
}

/* Create a rel.dyn relocation for the dynamic linker to resolve.  REL
   is the original relocation, which is now being transformed into a
   dynamic relocation.  The ADDENDP is adjusted if necessary; the
   caller should store the result in place of the original addend.  */

static bfd_boolean
mips_elf_create_dynamic_relocation (bfd *output_bfd,
				    struct bfd_link_info *info,
				    const Elf_Internal_Rela *rel,
				    struct mips_elf_link_hash_entry *h,
				    asection *sec, bfd_vma symbol,
				    bfd_vma *addendp, asection *input_section)
{
  Elf_Internal_Rela outrel[3];
  asection *sreloc;
  bfd *dynobj;
  int r_type;
  long indx;
  bfd_boolean defined_p;
  struct mips_elf_link_hash_table *htab;

  htab = mips_elf_hash_table (info);
  r_type = ELF_R_TYPE (output_bfd, rel->r_info);
  dynobj = elf_hash_table (info)->dynobj;
  sreloc = mips_elf_rel_dyn_section (info, FALSE);
  BFD_ASSERT (sreloc != NULL);
  BFD_ASSERT (sreloc->contents != NULL);
  BFD_ASSERT (sreloc->reloc_count * MIPS_ELF_REL_SIZE (output_bfd)
	      < sreloc->size);

  outrel[0].r_offset =
    _bfd_elf_section_offset (output_bfd, info, input_section, rel[0].r_offset);
  if (ABI_64_P (output_bfd))
    {
      outrel[1].r_offset =
	_bfd_elf_section_offset (output_bfd, info, input_section, rel[1].r_offset);
      outrel[2].r_offset =
	_bfd_elf_section_offset (output_bfd, info, input_section, rel[2].r_offset);
    }

  if (outrel[0].r_offset == MINUS_ONE)
    /* The relocation field has been deleted.  */
    return TRUE;

  if (outrel[0].r_offset == MINUS_TWO)
    {
      /* The relocation field has been converted into a relative value of
	 some sort.  Functions like _bfd_elf_write_section_eh_frame expect
	 the field to be fully relocated, so add in the symbol's value.  */
      *addendp += symbol;
      return TRUE;
    }

  /* We must now calculate the dynamic symbol table index to use
     in the relocation.  */
  if (h != NULL
      && (sec == NULL || !h->root.def_regular
	  || (info->shared && !info->symbolic && !h->root.forced_local)))
    {
      indx = h->root.dynindx;
      if (SGI_COMPAT (output_bfd))
	defined_p = h->root.def_regular;
      else
	/* ??? glibc's ld.so just adds the final GOT entry to the
	   relocation field.  It therefore treats relocs against
	   defined symbols in the same way as relocs against
	   undefined symbols.  */
	defined_p = FALSE;
    }
  else
    {
      if (sec != NULL && bfd_is_abs_section (sec))
	indx = 0;
      else if (sec == NULL || sec->owner == NULL)
	{
	  bfd_set_error (bfd_error_bad_value);
	  return FALSE;
	}
      else
	{
	  indx = elf_section_data (sec->output_section)->dynindx;
	  if (indx == 0)
	    {
	      asection *osec = htab->root.text_index_section;
	      indx = elf_section_data (osec)->dynindx;
	    }
	  if (indx == 0)
	    abort ();
	}

      /* Instead of generating a relocation using the section
	 symbol, we may as well make it a fully relative
	 relocation.  We want to avoid generating relocations to
	 local symbols because we used to generate them
	 incorrectly, without adding the original symbol value,
	 which is mandated by the ABI for section symbols.  In
	 order to give dynamic loaders and applications time to
	 phase out the incorrect use, we refrain from emitting
	 section-relative relocations.  It's not like they're
	 useful, after all.  This should be a bit more efficient
	 as well.  */
      /* ??? Although this behavior is compatible with glibc's ld.so,
	 the ABI says that relocations against STN_UNDEF should have
	 a symbol value of 0.  Irix rld honors this, so relocations
	 against STN_UNDEF have no effect.  */
      if (!SGI_COMPAT (output_bfd))
	indx = 0;
      defined_p = TRUE;
    }

  /* If the relocation was previously an absolute relocation and
     this symbol will not be referred to by the relocation, we must
     adjust it by the value we give it in the dynamic symbol table.
     Otherwise leave the job up to the dynamic linker.  */
  if (defined_p && r_type != R_MIPS_REL32)
    *addendp += symbol;

  if (htab->is_vxworks)
    /* VxWorks uses non-relative relocations for this.  */
    outrel[0].r_info = ELF32_R_INFO (indx, R_MIPS_32);
  else
    /* The relocation is always an REL32 relocation because we don't
       know where the shared library will wind up at load-time.  */
    outrel[0].r_info = ELF_R_INFO (output_bfd, (unsigned long) indx,
				   R_MIPS_REL32);

  /* For strict adherence to the ABI specification, we should
     generate a R_MIPS_64 relocation record by itself before the
     _REL32/_64 record as well, such that the addend is read in as
     a 64-bit value (REL32 is a 32-bit relocation, after all).
     However, since none of the existing ELF64 MIPS dynamic
     loaders seems to care, we don't waste space with these
     artificial relocations.  If this turns out to not be true,
     mips_elf_allocate_dynamic_relocation() should be tweaked so
     as to make room for a pair of dynamic relocations per
     invocation if ABI_64_P, and here we should generate an
     additional relocation record with R_MIPS_64 by itself for a
     NULL symbol before this relocation record.  */
  outrel[1].r_info = ELF_R_INFO (output_bfd, 0,
				 ABI_64_P (output_bfd)
				 ? R_MIPS_64
				 : R_MIPS_NONE);
  outrel[2].r_info = ELF_R_INFO (output_bfd, 0, R_MIPS_NONE);

  /* Adjust the output offset of the relocation to reference the
     correct location in the output file.  */
  outrel[0].r_offset += (input_section->output_section->vma
			 + input_section->output_offset);
  outrel[1].r_offset += (input_section->output_section->vma
			 + input_section->output_offset);
  outrel[2].r_offset += (input_section->output_section->vma
			 + input_section->output_offset);

  /* Put the relocation back out.  We have to use the special
     relocation outputter in the 64-bit case since the 64-bit
     relocation format is non-standard.  */
  if (ABI_64_P (output_bfd))
    {
      (*get_elf_backend_data (output_bfd)->s->swap_reloc_out)
	(output_bfd, &outrel[0],
	 (sreloc->contents
	  + sreloc->reloc_count * sizeof (Elf64_Mips_External_Rel)));
    }
  else if (htab->is_vxworks)
    {
      /* VxWorks uses RELA rather than REL dynamic relocations.  */
      outrel[0].r_addend = *addendp;
      bfd_elf32_swap_reloca_out
	(output_bfd, &outrel[0],
	 (sreloc->contents
	  + sreloc->reloc_count * sizeof (Elf32_External_Rela)));
    }
  else
    bfd_elf32_swap_reloc_out
      (output_bfd, &outrel[0],
       (sreloc->contents + sreloc->reloc_count * sizeof (Elf32_External_Rel)));

  /* We've now added another relocation.  */
  ++sreloc->reloc_count;

  /* Make sure the output section is writable.  The dynamic linker
     will be writing to it.  */
  elf_section_data (input_section->output_section)->this_hdr.sh_flags
    |= SHF_WRITE;

  /* On IRIX5, make an entry of compact relocation info.  */
  if (IRIX_COMPAT (output_bfd) == ict_irix5)
    {
      asection *scpt = bfd_get_section_by_name (dynobj, ".compact_rel");
      bfd_byte *cr;

      if (scpt)
	{
	  Elf32_crinfo cptrel;

	  mips_elf_set_cr_format (cptrel, CRF_MIPS_LONG);
	  cptrel.vaddr = (rel->r_offset
			  + input_section->output_section->vma
			  + input_section->output_offset);
	  if (r_type == R_MIPS_REL32)
	    mips_elf_set_cr_type (cptrel, CRT_MIPS_REL32);
	  else
	    mips_elf_set_cr_type (cptrel, CRT_MIPS_WORD);
	  mips_elf_set_cr_dist2to (cptrel, 0);
	  cptrel.konst = *addendp;

	  cr = (scpt->contents
		+ sizeof (Elf32_External_compact_rel));
	  mips_elf_set_cr_relvaddr (cptrel, 0);
	  bfd_elf32_swap_crinfo_out (output_bfd, &cptrel,
				     ((Elf32_External_crinfo *) cr
				      + scpt->reloc_count));
	  ++scpt->reloc_count;
	}
    }

  /* If we've written this relocation for a readonly section,
     we need to set DF_TEXTREL again, so that we do not delete the
     DT_TEXTREL tag.  */
  if (MIPS_ELF_READONLY_SECTION (input_section))
    info->flags |= DF_TEXTREL;

  return TRUE;
}

/* Return the MACH for a MIPS e_flags value.  */

unsigned long
_bfd_elf_mips_mach (flagword flags)
{
  switch (flags & EF_MIPS_MACH)
    {
    case E_MIPS_MACH_3900:
      return bfd_mach_mips3900;

    case E_MIPS_MACH_4010:
      return bfd_mach_mips4010;

    case E_MIPS_MACH_4100:
      return bfd_mach_mips4100;

    case E_MIPS_MACH_4111:
      return bfd_mach_mips4111;

    case E_MIPS_MACH_4120:
      return bfd_mach_mips4120;

    case E_MIPS_MACH_4650:
      return bfd_mach_mips4650;

    case E_MIPS_MACH_5400:
      return bfd_mach_mips5400;

    case E_MIPS_MACH_5500:
      return bfd_mach_mips5500;

    case E_MIPS_MACH_9000:
      return bfd_mach_mips9000;

    case E_MIPS_MACH_OCTEON:
      return bfd_mach_mips_octeon;

    case E_MIPS_MACH_SB1:
      return bfd_mach_mips_sb1;

    default:
      switch (flags & EF_MIPS_ARCH)
	{
	default:
	case E_MIPS_ARCH_1:
	  return bfd_mach_mips3000;

	case E_MIPS_ARCH_2:
	  return bfd_mach_mips6000;

	case E_MIPS_ARCH_3:
	  return bfd_mach_mips4000;

	case E_MIPS_ARCH_4:
	  return bfd_mach_mips8000;

	case E_MIPS_ARCH_5:
	  return bfd_mach_mips5;

	case E_MIPS_ARCH_32:
	  return bfd_mach_mipsisa32;

	case E_MIPS_ARCH_64:
	  return bfd_mach_mipsisa64;

	case E_MIPS_ARCH_32R2:
	  return bfd_mach_mipsisa32r2;

	case E_MIPS_ARCH_64R2:
	  return bfd_mach_mipsisa64r2;
	}
    }

  return 0;
}

/* Return printable name for ABI.  */

static INLINE char *
elf_mips_abi_name (bfd *abfd)
{
  flagword flags;

  flags = elf_elfheader (abfd)->e_flags;
  switch (flags & EF_MIPS_ABI)
    {
    case 0:
      if (ABI_N32_P (abfd))
	return "N32";
      else if (ABI_64_P (abfd))
	return "64";
      else
	return "none";
    case E_MIPS_ABI_O32:
      return "O32";
    case E_MIPS_ABI_O64:
      return "O64";
    case E_MIPS_ABI_EABI32:
      return "EABI32";
    case E_MIPS_ABI_EABI64:
      return "EABI64";
    default:
      return "unknown abi";
    }
}

/* MIPS ELF uses two common sections.  One is the usual one, and the
   other is for small objects.  All the small objects are kept
   together, and then referenced via the gp pointer, which yields
   faster assembler code.  This is what we use for the small common
   section.  This approach is copied from ecoff.c.  */
static asection mips_elf_scom_section;
static asymbol mips_elf_scom_symbol;
static asymbol *mips_elf_scom_symbol_ptr;

/* MIPS ELF also uses an acommon section, which represents an
   allocated common symbol which may be overridden by a
   definition in a shared library.  */
static asection mips_elf_acom_section;
static asymbol mips_elf_acom_symbol;
static asymbol *mips_elf_acom_symbol_ptr;

/* Handle the special MIPS section numbers that a symbol may use.
   This is used for both the 32-bit and the 64-bit ABI.  */

void
_bfd_mips_elf_symbol_processing (bfd *abfd, asymbol *asym)
{
  elf_symbol_type *elfsym;

  elfsym = (elf_symbol_type *) asym;
  switch (elfsym->internal_elf_sym.st_shndx)
    {
    case SHN_MIPS_ACOMMON:
      /* This section is used in a dynamically linked executable file.
	 It is an allocated common section.  The dynamic linker can
	 either resolve these symbols to something in a shared
	 library, or it can just leave them here.  For our purposes,
	 we can consider these symbols to be in a new section.  */
      if (mips_elf_acom_section.name == NULL)
	{
	  /* Initialize the acommon section.  */
	  mips_elf_acom_section.name = ".acommon";
	  mips_elf_acom_section.flags = SEC_ALLOC;
	  mips_elf_acom_section.output_section = &mips_elf_acom_section;
	  mips_elf_acom_section.symbol = &mips_elf_acom_symbol;
	  mips_elf_acom_section.symbol_ptr_ptr = &mips_elf_acom_symbol_ptr;
	  mips_elf_acom_symbol.name = ".acommon";
	  mips_elf_acom_symbol.flags = BSF_SECTION_SYM;
	  mips_elf_acom_symbol.section = &mips_elf_acom_section;
	  mips_elf_acom_symbol_ptr = &mips_elf_acom_symbol;
	}
      asym->section = &mips_elf_acom_section;
      break;

    case SHN_COMMON:
      /* Common symbols less than the GP size are automatically
	 treated as SHN_MIPS_SCOMMON symbols on IRIX5.  */
      if (asym->value > elf_gp_size (abfd)
	  || ELF_ST_TYPE (elfsym->internal_elf_sym.st_info) == STT_TLS
	  || IRIX_COMPAT (abfd) == ict_irix6)
	break;
      /* Fall through.  */
    case SHN_MIPS_SCOMMON:
      if (mips_elf_scom_section.name == NULL)
	{
	  /* Initialize the small common section.  */
	  mips_elf_scom_section.name = ".scommon";
	  mips_elf_scom_section.flags = SEC_IS_COMMON;
	  mips_elf_scom_section.output_section = &mips_elf_scom_section;
	  mips_elf_scom_section.symbol = &mips_elf_scom_symbol;
	  mips_elf_scom_section.symbol_ptr_ptr = &mips_elf_scom_symbol_ptr;
	  mips_elf_scom_symbol.name = ".scommon";
	  mips_elf_scom_symbol.flags = BSF_SECTION_SYM;
	  mips_elf_scom_symbol.section = &mips_elf_scom_section;
	  mips_elf_scom_symbol_ptr = &mips_elf_scom_symbol;
	}
      asym->section = &mips_elf_scom_section;
      asym->value = elfsym->internal_elf_sym.st_size;
      break;

    case SHN_MIPS_SUNDEFINED:
      asym->section = bfd_und_section_ptr;
      break;

    case SHN_MIPS_TEXT:
      {
	asection *section = bfd_get_section_by_name (abfd, ".text");

	BFD_ASSERT (SGI_COMPAT (abfd));
	if (section != NULL)
	  {
	    asym->section = section;
	    /* MIPS_TEXT is a bit special, the address is not an offset
	       to the base of the .text section.  So substract the section
	       base address to make it an offset.  */
	    asym->value -= section->vma;
	  }
      }
      break;

    case SHN_MIPS_DATA:
      {
	asection *section = bfd_get_section_by_name (abfd, ".data");

	BFD_ASSERT (SGI_COMPAT (abfd));
	if (section != NULL)
	  {
	    asym->section = section;
	    /* MIPS_DATA is a bit special, the address is not an offset
	       to the base of the .data section.  So substract the section
	       base address to make it an offset.  */
	    asym->value -= section->vma;
	  }
      }
      break;
    }
}

/* Implement elf_backend_eh_frame_address_size.  This differs from
   the default in the way it handles EABI64.

   EABI64 was originally specified as an LP64 ABI, and that is what
   -mabi=eabi normally gives on a 64-bit target.  However, gcc has
   historically accepted the combination of -mabi=eabi and -mlong32,
   and this ILP32 variation has become semi-official over time.
   Both forms use elf32 and have pointer-sized FDE addresses.

   If an EABI object was generated by GCC 4.0 or above, it will have
   an empty .gcc_compiled_longXX section, where XX is the size of longs
   in bits.  Unfortunately, ILP32 objects generated by earlier compilers
   have no special marking to distinguish them from LP64 objects.

   We don't want users of the official LP64 ABI to be punished for the
   existence of the ILP32 variant, but at the same time, we don't want
   to mistakenly interpret pre-4.0 ILP32 objects as being LP64 objects.
   We therefore take the following approach:

      - If ABFD contains a .gcc_compiled_longXX section, use it to
        determine the pointer size.

      - Otherwise check the type of the first relocation.  Assume that
        the LP64 ABI is being used if the relocation is of type R_MIPS_64.

      - Otherwise punt.

   The second check is enough to detect LP64 objects generated by pre-4.0
   compilers because, in the kind of output generated by those compilers,
   the first relocation will be associated with either a CIE personality
   routine or an FDE start address.  Furthermore, the compilers never
   used a special (non-pointer) encoding for this ABI.

   Checking the relocation type should also be safe because there is no
   reason to use R_MIPS_64 in an ILP32 object.  Pre-4.0 compilers never
   did so.  */

unsigned int
_bfd_mips_elf_eh_frame_address_size (bfd *abfd, asection *sec)
{
  if (elf_elfheader (abfd)->e_ident[EI_CLASS] == ELFCLASS64)
    return 8;
  if ((elf_elfheader (abfd)->e_flags & EF_MIPS_ABI) == E_MIPS_ABI_EABI64)
    {
      bfd_boolean long32_p, long64_p;

      long32_p = bfd_get_section_by_name (abfd, ".gcc_compiled_long32") != 0;
      long64_p = bfd_get_section_by_name (abfd, ".gcc_compiled_long64") != 0;
      if (long32_p && long64_p)
	return 0;
      if (long32_p)
	return 4;
      if (long64_p)
	return 8;

      if (sec->reloc_count > 0
	  && elf_section_data (sec)->relocs != NULL
	  && (ELF32_R_TYPE (elf_section_data (sec)->relocs[0].r_info)
	      == R_MIPS_64))
	return 8;

      return 0;
    }
  return 4;
}

/* There appears to be a bug in the MIPSpro linker that causes GOT_DISP
   relocations against two unnamed section symbols to resolve to the
   same address.  For example, if we have code like:

	lw	$4,%got_disp(.data)($gp)
	lw	$25,%got_disp(.text)($gp)
	jalr	$25

   then the linker will resolve both relocations to .data and the program
   will jump there rather than to .text.

   We can work around this problem by giving names to local section symbols.
   This is also what the MIPSpro tools do.  */

bfd_boolean
_bfd_mips_elf_name_local_section_symbols (bfd *abfd)
{
  return SGI_COMPAT (abfd);
}

/* Work over a section just before writing it out.  This routine is
   used by both the 32-bit and the 64-bit ABI.  FIXME: We recognize
   sections that need the SHF_MIPS_GPREL flag by name; there has to be
   a better way.  */

bfd_boolean
_bfd_mips_elf_section_processing (bfd *abfd, Elf_Internal_Shdr *hdr)
{
  if (hdr->sh_type == SHT_MIPS_REGINFO
      && hdr->sh_size > 0)
    {
      bfd_byte buf[4];

      BFD_ASSERT (hdr->sh_size == sizeof (Elf32_External_RegInfo));
      BFD_ASSERT (hdr->contents == NULL);

      if (bfd_seek (abfd,
		    hdr->sh_offset + sizeof (Elf32_External_RegInfo) - 4,
		    SEEK_SET) != 0)
	return FALSE;
      H_PUT_32 (abfd, elf_gp (abfd), buf);
      if (bfd_bwrite (buf, 4, abfd) != 4)
	return FALSE;
    }

  if (hdr->sh_type == SHT_MIPS_OPTIONS
      && hdr->bfd_section != NULL
      && mips_elf_section_data (hdr->bfd_section) != NULL
      && mips_elf_section_data (hdr->bfd_section)->u.tdata != NULL)
    {
      bfd_byte *contents, *l, *lend;

      /* We stored the section contents in the tdata field in the
	 set_section_contents routine.  We save the section contents
	 so that we don't have to read them again.
	 At this point we know that elf_gp is set, so we can look
	 through the section contents to see if there is an
	 ODK_REGINFO structure.  */

      contents = mips_elf_section_data (hdr->bfd_section)->u.tdata;
      l = contents;
      lend = contents + hdr->sh_size;
      while (l + sizeof (Elf_External_Options) <= lend)
	{
	  Elf_Internal_Options intopt;

	  bfd_mips_elf_swap_options_in (abfd, (Elf_External_Options *) l,
					&intopt);
	  if (intopt.size < sizeof (Elf_External_Options))
	    {
	      (*_bfd_error_handler)
		(_("%B: Warning: bad `%s' option size %u smaller than its header"),
		abfd, MIPS_ELF_OPTIONS_SECTION_NAME (abfd), intopt.size);
	      break;
	    }
	  if (ABI_64_P (abfd) && intopt.kind == ODK_REGINFO)
	    {
	      bfd_byte buf[8];

	      if (bfd_seek (abfd,
			    (hdr->sh_offset
			     + (l - contents)
			     + sizeof (Elf_External_Options)
			     + (sizeof (Elf64_External_RegInfo) - 8)),
			     SEEK_SET) != 0)
		return FALSE;
	      H_PUT_64 (abfd, elf_gp (abfd), buf);
	      if (bfd_bwrite (buf, 8, abfd) != 8)
		return FALSE;
	    }
	  else if (intopt.kind == ODK_REGINFO)
	    {
	      bfd_byte buf[4];

	      if (bfd_seek (abfd,
			    (hdr->sh_offset
			     + (l - contents)
			     + sizeof (Elf_External_Options)
			     + (sizeof (Elf32_External_RegInfo) - 4)),
			    SEEK_SET) != 0)
		return FALSE;
	      H_PUT_32 (abfd, elf_gp (abfd), buf);
	      if (bfd_bwrite (buf, 4, abfd) != 4)
		return FALSE;
	    }
	  l += intopt.size;
	}
    }

  if (hdr->bfd_section != NULL)
    {
      const char *name = bfd_get_section_name (abfd, hdr->bfd_section);

      if (strcmp (name, ".sdata") == 0
	  || strcmp (name, ".lit8") == 0
	  || strcmp (name, ".lit4") == 0)
	{
	  hdr->sh_flags |= SHF_ALLOC | SHF_WRITE | SHF_MIPS_GPREL;
	  hdr->sh_type = SHT_PROGBITS;
	}
      else if (strcmp (name, ".sbss") == 0)
	{
	  hdr->sh_flags |= SHF_ALLOC | SHF_WRITE | SHF_MIPS_GPREL;
	  hdr->sh_type = SHT_NOBITS;
	}
      else if (strcmp (name, ".srdata") == 0)
	{
	  hdr->sh_flags |= SHF_ALLOC | SHF_MIPS_GPREL;
	  hdr->sh_type = SHT_PROGBITS;
	}
      else if (strcmp (name, ".compact_rel") == 0)
	{
	  hdr->sh_flags = 0;
	  hdr->sh_type = SHT_PROGBITS;
	}
      else if (strcmp (name, ".rtproc") == 0)
	{
	  if (hdr->sh_addralign != 0 && hdr->sh_entsize == 0)
	    {
	      unsigned int adjust;

	      adjust = hdr->sh_size % hdr->sh_addralign;
	      if (adjust != 0)
		hdr->sh_size += hdr->sh_addralign - adjust;
	    }
	}
    }

  return TRUE;
}

/* Handle a MIPS specific section when reading an object file.  This
   is called when elfcode.h finds a section with an unknown type.
   This routine supports both the 32-bit and 64-bit ELF ABI.

   FIXME: We need to handle the SHF_MIPS_GPREL flag, but I'm not sure
   how to.  */

bfd_boolean
_bfd_mips_elf_section_from_shdr (bfd *abfd,
				 Elf_Internal_Shdr *hdr,
				 const char *name,
				 int shindex)
{
  flagword flags = 0;

  /* There ought to be a place to keep ELF backend specific flags, but
     at the moment there isn't one.  We just keep track of the
     sections by their name, instead.  Fortunately, the ABI gives
     suggested names for all the MIPS specific sections, so we will
     probably get away with this.  */
  switch (hdr->sh_type)
    {
    case SHT_MIPS_LIBLIST:
      if (strcmp (name, ".liblist") != 0)
	return FALSE;
      break;
    case SHT_MIPS_MSYM:
      if (strcmp (name, ".msym") != 0)
	return FALSE;
      break;
    case SHT_MIPS_CONFLICT:
      if (strcmp (name, ".conflict") != 0)
	return FALSE;
      break;
    case SHT_MIPS_GPTAB:
      if (! CONST_STRNEQ (name, ".gptab."))
	return FALSE;
      break;
    case SHT_MIPS_UCODE:
      if (strcmp (name, ".ucode") != 0)
	return FALSE;
      break;
    case SHT_MIPS_DEBUG:
      if (strcmp (name, ".mdebug") != 0)
	return FALSE;
      flags = SEC_DEBUGGING;
      break;
    case SHT_MIPS_REGINFO:
      if (strcmp (name, ".reginfo") != 0
	  || hdr->sh_size != sizeof (Elf32_External_RegInfo))
	return FALSE;
      flags = (SEC_LINK_ONCE | SEC_LINK_DUPLICATES_SAME_SIZE);
      break;
    case SHT_MIPS_IFACE:
      if (strcmp (name, ".MIPS.interfaces") != 0)
	return FALSE;
      break;
    case SHT_MIPS_CONTENT:
      if (! CONST_STRNEQ (name, ".MIPS.content"))
	return FALSE;
      break;
    case SHT_MIPS_OPTIONS:
      if (!MIPS_ELF_OPTIONS_SECTION_NAME_P (name))
	return FALSE;
      break;
    case SHT_MIPS_DWARF:
      if (! CONST_STRNEQ (name, ".debug_"))
	return FALSE;
      break;
    case SHT_MIPS_SYMBOL_LIB:
      if (strcmp (name, ".MIPS.symlib") != 0)
	return FALSE;
      break;
    case SHT_MIPS_EVENTS:
      if (! CONST_STRNEQ (name, ".MIPS.events")
	  && ! CONST_STRNEQ (name, ".MIPS.post_rel"))
	return FALSE;
      break;
    default:
      break;
    }

  if (! _bfd_elf_make_section_from_shdr (abfd, hdr, name, shindex))
    return FALSE;

  if (flags)
    {
      if (! bfd_set_section_flags (abfd, hdr->bfd_section,
				   (bfd_get_section_flags (abfd,
							   hdr->bfd_section)
				    | flags)))
	return FALSE;
    }

  /* FIXME: We should record sh_info for a .gptab section.  */

  /* For a .reginfo section, set the gp value in the tdata information
     from the contents of this section.  We need the gp value while
     processing relocs, so we just get it now.  The .reginfo section
     is not used in the 64-bit MIPS ELF ABI.  */
  if (hdr->sh_type == SHT_MIPS_REGINFO)
    {
      Elf32_External_RegInfo ext;
      Elf32_RegInfo s;

      if (! bfd_get_section_contents (abfd, hdr->bfd_section,
				      &ext, 0, sizeof ext))
	return FALSE;
      bfd_mips_elf32_swap_reginfo_in (abfd, &ext, &s);
      elf_gp (abfd) = s.ri_gp_value;
    }

  /* For a SHT_MIPS_OPTIONS section, look for a ODK_REGINFO entry, and
     set the gp value based on what we find.  We may see both
     SHT_MIPS_REGINFO and SHT_MIPS_OPTIONS/ODK_REGINFO; in that case,
     they should agree.  */
  if (hdr->sh_type == SHT_MIPS_OPTIONS)
    {
      bfd_byte *contents, *l, *lend;

      contents = bfd_malloc (hdr->sh_size);
      if (contents == NULL)
	return FALSE;
      if (! bfd_get_section_contents (abfd, hdr->bfd_section, contents,
				      0, hdr->sh_size))
	{
	  free (contents);
	  return FALSE;
	}
      l = contents;
      lend = contents + hdr->sh_size;
      while (l + sizeof (Elf_External_Options) <= lend)
	{
	  Elf_Internal_Options intopt;

	  bfd_mips_elf_swap_options_in (abfd, (Elf_External_Options *) l,
					&intopt);
	  if (intopt.size < sizeof (Elf_External_Options))
	    {
	      (*_bfd_error_handler)
		(_("%B: Warning: bad `%s' option size %u smaller than its header"),
		abfd, MIPS_ELF_OPTIONS_SECTION_NAME (abfd), intopt.size);
	      break;
	    }
	  if (ABI_64_P (abfd) && intopt.kind == ODK_REGINFO)
	    {
	      Elf64_Internal_RegInfo intreg;

	      bfd_mips_elf64_swap_reginfo_in
		(abfd,
		 ((Elf64_External_RegInfo *)
		  (l + sizeof (Elf_External_Options))),
		 &intreg);
	      elf_gp (abfd) = intreg.ri_gp_value;
	    }
	  else if (intopt.kind == ODK_REGINFO)
	    {
	      Elf32_RegInfo intreg;

	      bfd_mips_elf32_swap_reginfo_in
		(abfd,
		 ((Elf32_External_RegInfo *)
		  (l + sizeof (Elf_External_Options))),
		 &intreg);
	      elf_gp (abfd) = intreg.ri_gp_value;
	    }
	  l += intopt.size;
	}
      free (contents);
    }

  return TRUE;
}

/* Set the correct type for a MIPS ELF section.  We do this by the
   section name, which is a hack, but ought to work.  This routine is
   used by both the 32-bit and the 64-bit ABI.  */

bfd_boolean
_bfd_mips_elf_fake_sections (bfd *abfd, Elf_Internal_Shdr *hdr, asection *sec)
{
  const char *name = bfd_get_section_name (abfd, sec);

  if (strcmp (name, ".liblist") == 0)
    {
      hdr->sh_type = SHT_MIPS_LIBLIST;
      hdr->sh_info = sec->size / sizeof (Elf32_Lib);
      /* The sh_link field is set in final_write_processing.  */
    }
  else if (strcmp (name, ".conflict") == 0)
    hdr->sh_type = SHT_MIPS_CONFLICT;
  else if (CONST_STRNEQ (name, ".gptab."))
    {
      hdr->sh_type = SHT_MIPS_GPTAB;
      hdr->sh_entsize = sizeof (Elf32_External_gptab);
      /* The sh_info field is set in final_write_processing.  */
    }
  else if (strcmp (name, ".ucode") == 0)
    hdr->sh_type = SHT_MIPS_UCODE;
  else if (strcmp (name, ".mdebug") == 0)
    {
      hdr->sh_type = SHT_MIPS_DEBUG;
      /* In a shared object on IRIX 5.3, the .mdebug section has an
         entsize of 0.  FIXME: Does this matter?  */
      if (SGI_COMPAT (abfd) && (abfd->flags & DYNAMIC) != 0)
	hdr->sh_entsize = 0;
      else
	hdr->sh_entsize = 1;
    }
  else if (strcmp (name, ".reginfo") == 0)
    {
      hdr->sh_type = SHT_MIPS_REGINFO;
      /* In a shared object on IRIX 5.3, the .reginfo section has an
         entsize of 0x18.  FIXME: Does this matter?  */
      if (SGI_COMPAT (abfd))
	{
	  if ((abfd->flags & DYNAMIC) != 0)
	    hdr->sh_entsize = sizeof (Elf32_External_RegInfo);
	  else
	    hdr->sh_entsize = 1;
	}
      else
	hdr->sh_entsize = sizeof (Elf32_External_RegInfo);
    }
  else if (SGI_COMPAT (abfd)
	   && (strcmp (name, ".hash") == 0
	       || strcmp (name, ".dynamic") == 0
	       || strcmp (name, ".dynstr") == 0))
    {
      if (SGI_COMPAT (abfd))
	hdr->sh_entsize = 0;
#if 0
      /* This isn't how the IRIX6 linker behaves.  */
      hdr->sh_info = SIZEOF_MIPS_DYNSYM_SECNAMES;
#endif
    }
  else if (strcmp (name, ".got") == 0
	   || strcmp (name, ".srdata") == 0
	   || strcmp (name, ".sdata") == 0
	   || strcmp (name, ".sbss") == 0
	   || strcmp (name, ".lit4") == 0
	   || strcmp (name, ".lit8") == 0)
    hdr->sh_flags |= SHF_MIPS_GPREL;
  else if (strcmp (name, ".MIPS.interfaces") == 0)
    {
      hdr->sh_type = SHT_MIPS_IFACE;
      hdr->sh_flags |= SHF_MIPS_NOSTRIP;
    }
  else if (CONST_STRNEQ (name, ".MIPS.content"))
    {
      hdr->sh_type = SHT_MIPS_CONTENT;
      hdr->sh_flags |= SHF_MIPS_NOSTRIP;
      /* The sh_info field is set in final_write_processing.  */
    }
  else if (MIPS_ELF_OPTIONS_SECTION_NAME_P (name))
    {
      hdr->sh_type = SHT_MIPS_OPTIONS;
      hdr->sh_entsize = 1;
      hdr->sh_flags |= SHF_MIPS_NOSTRIP;
    }
  else if (CONST_STRNEQ (name, ".debug_"))
    hdr->sh_type = SHT_MIPS_DWARF;
  else if (strcmp (name, ".MIPS.symlib") == 0)
    {
      hdr->sh_type = SHT_MIPS_SYMBOL_LIB;
      /* The sh_link and sh_info fields are set in
         final_write_processing.  */
    }
  else if (CONST_STRNEQ (name, ".MIPS.events")
	   || CONST_STRNEQ (name, ".MIPS.post_rel"))
    {
      hdr->sh_type = SHT_MIPS_EVENTS;
      hdr->sh_flags |= SHF_MIPS_NOSTRIP;
      /* The sh_link field is set in final_write_processing.  */
    }
  else if (strcmp (name, ".msym") == 0)
    {
      hdr->sh_type = SHT_MIPS_MSYM;
      hdr->sh_flags |= SHF_ALLOC;
      hdr->sh_entsize = 8;
    }

  /* The generic elf_fake_sections will set up REL_HDR using the default
   kind of relocations.  We used to set up a second header for the
   non-default kind of relocations here, but only NewABI would use
   these, and the IRIX ld doesn't like resulting empty RELA sections.
   Thus we create those header only on demand now.  */

  return TRUE;
}

/* Given a BFD section, try to locate the corresponding ELF section
   index.  This is used by both the 32-bit and the 64-bit ABI.
   Actually, it's not clear to me that the 64-bit ABI supports these,
   but for non-PIC objects we will certainly want support for at least
   the .scommon section.  */

bfd_boolean
_bfd_mips_elf_section_from_bfd_section (bfd *abfd ATTRIBUTE_UNUSED,
					asection *sec, int *retval)
{
  if (strcmp (bfd_get_section_name (abfd, sec), ".scommon") == 0)
    {
      *retval = SHN_MIPS_SCOMMON;
      return TRUE;
    }
  if (strcmp (bfd_get_section_name (abfd, sec), ".acommon") == 0)
    {
      *retval = SHN_MIPS_ACOMMON;
      return TRUE;
    }
  return FALSE;
}

/* Hook called by the linker routine which adds symbols from an object
   file.  We must handle the special MIPS section numbers here.  */

bfd_boolean
_bfd_mips_elf_add_symbol_hook (bfd *abfd, struct bfd_link_info *info,
			       Elf_Internal_Sym *sym, const char **namep,
			       flagword *flagsp ATTRIBUTE_UNUSED,
			       asection **secp, bfd_vma *valp)
{
  if (SGI_COMPAT (abfd)
      && (abfd->flags & DYNAMIC) != 0
      && strcmp (*namep, "_rld_new_interface") == 0)
    {
      /* Skip IRIX5 rld entry name.  */
      *namep = NULL;
      return TRUE;
    }

  /* Shared objects may have a dynamic symbol '_gp_disp' defined as
     a SECTION *ABS*.  This causes ld to think it can resolve _gp_disp
     by setting a DT_NEEDED for the shared object.  Since _gp_disp is
     a magic symbol resolved by the linker, we ignore this bogus definition
     of _gp_disp.  New ABI objects do not suffer from this problem so this
     is not done for them. */
  if (!NEWABI_P(abfd)
      && (sym->st_shndx == SHN_ABS)
      && (strcmp (*namep, "_gp_disp") == 0))
    {
      *namep = NULL;
      return TRUE;
    }

  switch (sym->st_shndx)
    {
    case SHN_COMMON:
      /* Common symbols less than the GP size are automatically
	 treated as SHN_MIPS_SCOMMON symbols.  */
      if (sym->st_size > elf_gp_size (abfd)
	  || ELF_ST_TYPE (sym->st_info) == STT_TLS
	  || IRIX_COMPAT (abfd) == ict_irix6)
	break;
      /* Fall through.  */
    case SHN_MIPS_SCOMMON:
      *secp = bfd_make_section_old_way (abfd, ".scommon");
      (*secp)->flags |= SEC_IS_COMMON;
      *valp = sym->st_size;
      break;

    case SHN_MIPS_TEXT:
      /* This section is used in a shared object.  */
      if (elf_tdata (abfd)->elf_text_section == NULL)
	{
	  asymbol *elf_text_symbol;
	  asection *elf_text_section;
	  bfd_size_type amt = sizeof (asection);

	  elf_text_section = bfd_zalloc (abfd, amt);
	  if (elf_text_section == NULL)
	    return FALSE;

	  amt = sizeof (asymbol);
	  elf_text_symbol = bfd_zalloc (abfd, amt);
	  if (elf_text_symbol == NULL)
	    return FALSE;

	  /* Initialize the section.  */

	  elf_tdata (abfd)->elf_text_section = elf_text_section;
	  elf_tdata (abfd)->elf_text_symbol = elf_text_symbol;

	  elf_text_section->symbol = elf_text_symbol;
	  elf_text_section->symbol_ptr_ptr = &elf_tdata (abfd)->elf_text_symbol;

	  elf_text_section->name = ".text";
	  elf_text_section->flags = SEC_NO_FLAGS;
	  elf_text_section->output_section = NULL;
	  elf_text_section->owner = abfd;
	  elf_text_symbol->name = ".text";
	  elf_text_symbol->flags = BSF_SECTION_SYM | BSF_DYNAMIC;
	  elf_text_symbol->section = elf_text_section;
	}
      /* This code used to do *secp = bfd_und_section_ptr if
         info->shared.  I don't know why, and that doesn't make sense,
         so I took it out.  */
      *secp = elf_tdata (abfd)->elf_text_section;
      break;

    case SHN_MIPS_ACOMMON:
      /* Fall through. XXX Can we treat this as allocated data?  */
    case SHN_MIPS_DATA:
      /* This section is used in a shared object.  */
      if (elf_tdata (abfd)->elf_data_section == NULL)
	{
	  asymbol *elf_data_symbol;
	  asection *elf_data_section;
	  bfd_size_type amt = sizeof (asection);

	  elf_data_section = bfd_zalloc (abfd, amt);
	  if (elf_data_section == NULL)
	    return FALSE;

	  amt = sizeof (asymbol);
	  elf_data_symbol = bfd_zalloc (abfd, amt);
	  if (elf_data_symbol == NULL)
	    return FALSE;

	  /* Initialize the section.  */

	  elf_tdata (abfd)->elf_data_section = elf_data_section;
	  elf_tdata (abfd)->elf_data_symbol = elf_data_symbol;

	  elf_data_section->symbol = elf_data_symbol;
	  elf_data_section->symbol_ptr_ptr = &elf_tdata (abfd)->elf_data_symbol;

	  elf_data_section->name = ".data";
	  elf_data_section->flags = SEC_NO_FLAGS;
	  elf_data_section->output_section = NULL;
	  elf_data_section->owner = abfd;
	  elf_data_symbol->name = ".data";
	  elf_data_symbol->flags = BSF_SECTION_SYM | BSF_DYNAMIC;
	  elf_data_symbol->section = elf_data_section;
	}
      /* This code used to do *secp = bfd_und_section_ptr if
         info->shared.  I don't know why, and that doesn't make sense,
         so I took it out.  */
      *secp = elf_tdata (abfd)->elf_data_section;
      break;

    case SHN_MIPS_SUNDEFINED:
      *secp = bfd_und_section_ptr;
      break;
    }

  if (SGI_COMPAT (abfd)
      && ! info->shared
      && info->hash->creator == abfd->xvec
      && strcmp (*namep, "__rld_obj_head") == 0)
    {
      struct elf_link_hash_entry *h;
      struct bfd_link_hash_entry *bh;

      /* Mark __rld_obj_head as dynamic.  */
      bh = NULL;
      if (! (_bfd_generic_link_add_one_symbol
	     (info, abfd, *namep, BSF_GLOBAL, *secp, *valp, NULL, FALSE,
	      get_elf_backend_data (abfd)->collect, &bh)))
	return FALSE;

      h = (struct elf_link_hash_entry *) bh;
      h->non_elf = 0;
      h->def_regular = 1;
      h->type = STT_OBJECT;

      if (! bfd_elf_link_record_dynamic_symbol (info, h))
	return FALSE;

      mips_elf_hash_table (info)->use_rld_obj_head = TRUE;
    }

  /* If this is a mips16 text symbol, add 1 to the value to make it
     odd.  This will cause something like .word SYM to come up with
     the right value when it is loaded into the PC.  */
  if (sym->st_other == STO_MIPS16)
    ++*valp;

  return TRUE;
}

/* This hook function is called before the linker writes out a global
   symbol.  We mark symbols as small common if appropriate.  This is
   also where we undo the increment of the value for a mips16 symbol.  */

bfd_boolean
_bfd_mips_elf_link_output_symbol_hook
  (struct bfd_link_info *info ATTRIBUTE_UNUSED,
   const char *name ATTRIBUTE_UNUSED, Elf_Internal_Sym *sym,
   asection *input_sec, struct elf_link_hash_entry *h ATTRIBUTE_UNUSED)
{
  /* If we see a common symbol, which implies a relocatable link, then
     if a symbol was small common in an input file, mark it as small
     common in the output file.  */
  if (sym->st_shndx == SHN_COMMON
      && strcmp (input_sec->name, ".scommon") == 0)
    sym->st_shndx = SHN_MIPS_SCOMMON;

  if (sym->st_other == STO_MIPS16)
    sym->st_value &= ~1;

  return TRUE;
}

/* Functions for the dynamic linker.  */

/* Create dynamic sections when linking against a dynamic object.  */

bfd_boolean
_bfd_mips_elf_create_dynamic_sections (bfd *abfd, struct bfd_link_info *info)
{
  struct elf_link_hash_entry *h;
  struct bfd_link_hash_entry *bh;
  flagword flags;
  register asection *s;
  const char * const *namep;
  struct mips_elf_link_hash_table *htab;

  htab = mips_elf_hash_table (info);
  flags = (SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS | SEC_IN_MEMORY
	   | SEC_LINKER_CREATED | SEC_READONLY);

  /* The psABI requires a read-only .dynamic section, but the VxWorks
     EABI doesn't.  */
  if (!htab->is_vxworks)
    {
      s = bfd_get_section_by_name (abfd, ".dynamic");
      if (s != NULL)
	{
	  if (! bfd_set_section_flags (abfd, s, flags))
	    return FALSE;
	}
    }

  /* We need to create .got section.  */
  if (! mips_elf_create_got_section (abfd, info, FALSE))
    return FALSE;

  if (! mips_elf_rel_dyn_section (info, TRUE))
    return FALSE;

  /* Create .stub section.  */
  if (bfd_get_section_by_name (abfd,
			       MIPS_ELF_STUB_SECTION_NAME (abfd)) == NULL)
    {
      s = bfd_make_section_with_flags (abfd,
				       MIPS_ELF_STUB_SECTION_NAME (abfd),
				       flags | SEC_CODE);
      if (s == NULL
	  || ! bfd_set_section_alignment (abfd, s,
					  MIPS_ELF_LOG_FILE_ALIGN (abfd)))
	return FALSE;
    }

  if ((IRIX_COMPAT (abfd) == ict_irix5 || IRIX_COMPAT (abfd) == ict_none)
      && !info->shared
      && bfd_get_section_by_name (abfd, ".rld_map") == NULL)
    {
      s = bfd_make_section_with_flags (abfd, ".rld_map",
				       flags &~ (flagword) SEC_READONLY);
      if (s == NULL
	  || ! bfd_set_section_alignment (abfd, s,
					  MIPS_ELF_LOG_FILE_ALIGN (abfd)))
	return FALSE;
    }

  /* On IRIX5, we adjust add some additional symbols and change the
     alignments of several sections.  There is no ABI documentation
     indicating that this is necessary on IRIX6, nor any evidence that
     the linker takes such action.  */
  if (IRIX_COMPAT (abfd) == ict_irix5)
    {
      for (namep = mips_elf_dynsym_rtproc_names; *namep != NULL; namep++)
	{
	  bh = NULL;
	  if (! (_bfd_generic_link_add_one_symbol
		 (info, abfd, *namep, BSF_GLOBAL, bfd_und_section_ptr, 0,
		  NULL, FALSE, get_elf_backend_data (abfd)->collect, &bh)))
	    return FALSE;

	  h = (struct elf_link_hash_entry *) bh;
	  h->non_elf = 0;
	  h->def_regular = 1;
	  h->type = STT_SECTION;

	  if (! bfd_elf_link_record_dynamic_symbol (info, h))
	    return FALSE;
	}

      /* We need to create a .compact_rel section.  */
      if (SGI_COMPAT (abfd))
	{
	  if (!mips_elf_create_compact_rel_section (abfd, info))
	    return FALSE;
	}

      /* Change alignments of some sections.  */
      s = bfd_get_section_by_name (abfd, ".hash");
      if (s != NULL)
	bfd_set_section_alignment (abfd, s, MIPS_ELF_LOG_FILE_ALIGN (abfd));
      s = bfd_get_section_by_name (abfd, ".dynsym");
      if (s != NULL)
	bfd_set_section_alignment (abfd, s, MIPS_ELF_LOG_FILE_ALIGN (abfd));
      s = bfd_get_section_by_name (abfd, ".dynstr");
      if (s != NULL)
	bfd_set_section_alignment (abfd, s, MIPS_ELF_LOG_FILE_ALIGN (abfd));
      s = bfd_get_section_by_name (abfd, ".reginfo");
      if (s != NULL)
	bfd_set_section_alignment (abfd, s, MIPS_ELF_LOG_FILE_ALIGN (abfd));
      s = bfd_get_section_by_name (abfd, ".dynamic");
      if (s != NULL)
	bfd_set_section_alignment (abfd, s, MIPS_ELF_LOG_FILE_ALIGN (abfd));
    }

  if (!info->shared)
    {
      const char *name;

      name = SGI_COMPAT (abfd) ? "_DYNAMIC_LINK" : "_DYNAMIC_LINKING";
      bh = NULL;
      if (!(_bfd_generic_link_add_one_symbol
	    (info, abfd, name, BSF_GLOBAL, bfd_abs_section_ptr, 0,
	     NULL, FALSE, get_elf_backend_data (abfd)->collect, &bh)))
	return FALSE;

      h = (struct elf_link_hash_entry *) bh;
      h->non_elf = 0;
      h->def_regular = 1;
      h->type = STT_SECTION;

      if (! bfd_elf_link_record_dynamic_symbol (info, h))
	return FALSE;

      if (! mips_elf_hash_table (info)->use_rld_obj_head)
	{
	  /* __rld_map is a four byte word located in the .data section
	     and is filled in by the rtld to contain a pointer to
	     the _r_debug structure. Its symbol value will be set in
	     _bfd_mips_elf_finish_dynamic_symbol.  */
	  s = bfd_get_section_by_name (abfd, ".rld_map");
	  BFD_ASSERT (s != NULL);

	  name = SGI_COMPAT (abfd) ? "__rld_map" : "__RLD_MAP";
	  bh = NULL;
	  if (!(_bfd_generic_link_add_one_symbol
		(info, abfd, name, BSF_GLOBAL, s, 0, NULL, FALSE,
		 get_elf_backend_data (abfd)->collect, &bh)))
	    return FALSE;

	  h = (struct elf_link_hash_entry *) bh;
	  h->non_elf = 0;
	  h->def_regular = 1;
	  h->type = STT_OBJECT;

	  if (! bfd_elf_link_record_dynamic_symbol (info, h))
	    return FALSE;
	}
    }

  if (htab->is_vxworks)
    {
      /* Create the .plt, .rela.plt, .dynbss and .rela.bss sections.
	 Also create the _PROCEDURE_LINKAGE_TABLE symbol.  */
      if (!_bfd_elf_create_dynamic_sections (abfd, info))
	return FALSE;

      /* Cache the sections created above.  */
      htab->sdynbss = bfd_get_section_by_name (abfd, ".dynbss");
      htab->srelbss = bfd_get_section_by_name (abfd, ".rela.bss");
      htab->srelplt = bfd_get_section_by_name (abfd, ".rela.plt");
      htab->splt = bfd_get_section_by_name (abfd, ".plt");
      if (!htab->sdynbss
	  || (!htab->srelbss && !info->shared)
	  || !htab->srelplt
	  || !htab->splt)
	abort ();

      /* Do the usual VxWorks handling.  */
      if (!elf_vxworks_create_dynamic_sections (abfd, info, &htab->srelplt2))
	return FALSE;

      /* Work out the PLT sizes.  */
      if (info->shared)
	{
	  htab->plt_header_size
	    = 4 * ARRAY_SIZE (mips_vxworks_shared_plt0_entry);
	  htab->plt_entry_size
	    = 4 * ARRAY_SIZE (mips_vxworks_shared_plt_entry);
	}
      else
	{
	  htab->plt_header_size
	    = 4 * ARRAY_SIZE (mips_vxworks_exec_plt0_entry);
	  htab->plt_entry_size
	    = 4 * ARRAY_SIZE (mips_vxworks_exec_plt_entry);
	}
    }

  return TRUE;
}

/* Look through the relocs for a section during the first phase, and
   allocate space in the global offset table.  */

bfd_boolean
_bfd_mips_elf_check_relocs (bfd *abfd, struct bfd_link_info *info,
			    asection *sec, const Elf_Internal_Rela *relocs)
{
  const char *name;
  bfd *dynobj;
  Elf_Internal_Shdr *symtab_hdr;
  struct elf_link_hash_entry **sym_hashes;
  struct mips_got_info *g;
  size_t extsymoff;
  const Elf_Internal_Rela *rel;
  const Elf_Internal_Rela *rel_end;
  asection *sgot;
  asection *sreloc;
  const struct elf_backend_data *bed;
  struct mips_elf_link_hash_table *htab;

  if (info->relocatable)
    return TRUE;

  htab = mips_elf_hash_table (info);
  dynobj = elf_hash_table (info)->dynobj;
  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;
  sym_hashes = elf_sym_hashes (abfd);
  extsymoff = (elf_bad_symtab (abfd)) ? 0 : symtab_hdr->sh_info;

  /* Check for the mips16 stub sections.  */

  name = bfd_get_section_name (abfd, sec);
  if (FN_STUB_P (name))
    {
      unsigned long r_symndx;

      /* Look at the relocation information to figure out which symbol
         this is for.  */

      r_symndx = ELF_R_SYM (abfd, relocs->r_info);

      if (r_symndx < extsymoff
	  || sym_hashes[r_symndx - extsymoff] == NULL)
	{
	  asection *o;

	  /* This stub is for a local symbol.  This stub will only be
             needed if there is some relocation in this BFD, other
             than a 16 bit function call, which refers to this symbol.  */
	  for (o = abfd->sections; o != NULL; o = o->next)
	    {
	      Elf_Internal_Rela *sec_relocs;
	      const Elf_Internal_Rela *r, *rend;

	      /* We can ignore stub sections when looking for relocs.  */
	      if ((o->flags & SEC_RELOC) == 0
		  || o->reloc_count == 0
		  || mips16_stub_section_p (abfd, o))
		continue;

	      sec_relocs
		= _bfd_elf_link_read_relocs (abfd, o, NULL, NULL,
					     info->keep_memory);
	      if (sec_relocs == NULL)
		return FALSE;

	      rend = sec_relocs + o->reloc_count;
	      for (r = sec_relocs; r < rend; r++)
		if (ELF_R_SYM (abfd, r->r_info) == r_symndx
		    && ELF_R_TYPE (abfd, r->r_info) != R_MIPS16_26)
		  break;

	      if (elf_section_data (o)->relocs != sec_relocs)
		free (sec_relocs);

	      if (r < rend)
		break;
	    }

	  if (o == NULL)
	    {
	      /* There is no non-call reloc for this stub, so we do
                 not need it.  Since this function is called before
                 the linker maps input sections to output sections, we
                 can easily discard it by setting the SEC_EXCLUDE
                 flag.  */
	      sec->flags |= SEC_EXCLUDE;
	      return TRUE;
	    }

	  /* Record this stub in an array of local symbol stubs for
             this BFD.  */
	  if (elf_tdata (abfd)->local_stubs == NULL)
	    {
	      unsigned long symcount;
	      asection **n;
	      bfd_size_type amt;

	      if (elf_bad_symtab (abfd))
		symcount = NUM_SHDR_ENTRIES (symtab_hdr);
	      else
		symcount = symtab_hdr->sh_info;
	      amt = symcount * sizeof (asection *);
	      n = bfd_zalloc (abfd, amt);
	      if (n == NULL)
		return FALSE;
	      elf_tdata (abfd)->local_stubs = n;
	    }

	  sec->flags |= SEC_KEEP;
	  elf_tdata (abfd)->local_stubs[r_symndx] = sec;

	  /* We don't need to set mips16_stubs_seen in this case.
             That flag is used to see whether we need to look through
             the global symbol table for stubs.  We don't need to set
             it here, because we just have a local stub.  */
	}
      else
	{
	  struct mips_elf_link_hash_entry *h;

	  h = ((struct mips_elf_link_hash_entry *)
	       sym_hashes[r_symndx - extsymoff]);

	  while (h->root.root.type == bfd_link_hash_indirect
		 || h->root.root.type == bfd_link_hash_warning)
	    h = (struct mips_elf_link_hash_entry *) h->root.root.u.i.link;

	  /* H is the symbol this stub is for.  */

	  /* If we already have an appropriate stub for this function, we
	     don't need another one, so we can discard this one.  Since
	     this function is called before the linker maps input sections
	     to output sections, we can easily discard it by setting the
	     SEC_EXCLUDE flag.  */
	  if (h->fn_stub != NULL)
	    {
	      sec->flags |= SEC_EXCLUDE;
	      return TRUE;
	    }

	  sec->flags |= SEC_KEEP;
	  h->fn_stub = sec;
	  mips_elf_hash_table (info)->mips16_stubs_seen = TRUE;
	}
    }
  else if (CALL_STUB_P (name) || CALL_FP_STUB_P (name))
    {
      unsigned long r_symndx;
      struct mips_elf_link_hash_entry *h;
      asection **loc;

      /* Look at the relocation information to figure out which symbol
         this is for.  */

      r_symndx = ELF_R_SYM (abfd, relocs->r_info);

      if (r_symndx < extsymoff
	  || sym_hashes[r_symndx - extsymoff] == NULL)
	{
	  asection *o;

	  /* This stub is for a local symbol.  This stub will only be
             needed if there is some relocation (R_MIPS16_26) in this BFD
             that refers to this symbol.  */
	  for (o = abfd->sections; o != NULL; o = o->next)
	    {
	      Elf_Internal_Rela *sec_relocs;
	      const Elf_Internal_Rela *r, *rend;

	      /* We can ignore stub sections when looking for relocs.  */
	      if ((o->flags & SEC_RELOC) == 0
		  || o->reloc_count == 0
		  || mips16_stub_section_p (abfd, o))
		continue;

	      sec_relocs
		= _bfd_elf_link_read_relocs (abfd, o, NULL, NULL,
					     info->keep_memory);
	      if (sec_relocs == NULL)
		return FALSE;

	      rend = sec_relocs + o->reloc_count;
	      for (r = sec_relocs; r < rend; r++)
		if (ELF_R_SYM (abfd, r->r_info) == r_symndx
		    && ELF_R_TYPE (abfd, r->r_info) == R_MIPS16_26)
		    break;

	      if (elf_section_data (o)->relocs != sec_relocs)
		free (sec_relocs);

	      if (r < rend)
		break;
	    }

	  if (o == NULL)
	    {
	      /* There is no non-call reloc for this stub, so we do
                 not need it.  Since this function is called before
                 the linker maps input sections to output sections, we
                 can easily discard it by setting the SEC_EXCLUDE
                 flag.  */
	      sec->flags |= SEC_EXCLUDE;
	      return TRUE;
	    }

	  /* Record this stub in an array of local symbol call_stubs for
             this BFD.  */
	  if (elf_tdata (abfd)->local_call_stubs == NULL)
	    {
	      unsigned long symcount;
	      asection **n;
	      bfd_size_type amt;

	      if (elf_bad_symtab (abfd))
		symcount = NUM_SHDR_ENTRIES (symtab_hdr);
	      else
		symcount = symtab_hdr->sh_info;
	      amt = symcount * sizeof (asection *);
	      n = bfd_zalloc (abfd, amt);
	      if (n == NULL)
		return FALSE;
	      elf_tdata (abfd)->local_call_stubs = n;
	    }

	  sec->flags |= SEC_KEEP;
	  elf_tdata (abfd)->local_call_stubs[r_symndx] = sec;

	  /* We don't need to set mips16_stubs_seen in this case.
             That flag is used to see whether we need to look through
             the global symbol table for stubs.  We don't need to set
             it here, because we just have a local stub.  */
	}
      else
	{
	  h = ((struct mips_elf_link_hash_entry *)
	       sym_hashes[r_symndx - extsymoff]);
	  
	  /* H is the symbol this stub is for.  */
	  
	  if (CALL_FP_STUB_P (name))
	    loc = &h->call_fp_stub;
	  else
	    loc = &h->call_stub;
	  
	  /* If we already have an appropriate stub for this function, we
	     don't need another one, so we can discard this one.  Since
	     this function is called before the linker maps input sections
	     to output sections, we can easily discard it by setting the
	     SEC_EXCLUDE flag.  */
	  if (*loc != NULL)
	    {
	      sec->flags |= SEC_EXCLUDE;
	      return TRUE;
	    }

	  sec->flags |= SEC_KEEP;
	  *loc = sec;
	  mips_elf_hash_table (info)->mips16_stubs_seen = TRUE;
	}
    }

  if (dynobj == NULL)
    {
      sgot = NULL;
      g = NULL;
    }
  else
    {
      sgot = mips_elf_got_section (dynobj, FALSE);
      if (sgot == NULL)
	g = NULL;
      else
	{
	  BFD_ASSERT (mips_elf_section_data (sgot) != NULL);
	  g = mips_elf_section_data (sgot)->u.got_info;
	  BFD_ASSERT (g != NULL);
	}
    }

  sreloc = NULL;
  bed = get_elf_backend_data (abfd);
  rel_end = relocs + sec->reloc_count * bed->s->int_rels_per_ext_rel;
  for (rel = relocs; rel < rel_end; ++rel)
    {
      unsigned long r_symndx;
      unsigned int r_type;
      struct elf_link_hash_entry *h;

      r_symndx = ELF_R_SYM (abfd, rel->r_info);
      r_type = ELF_R_TYPE (abfd, rel->r_info);

      if (r_symndx < extsymoff)
	h = NULL;
      else if (r_symndx >= extsymoff + NUM_SHDR_ENTRIES (symtab_hdr))
	{
	  (*_bfd_error_handler)
	    (_("%B: Malformed reloc detected for section %s"),
	     abfd, name);
	  bfd_set_error (bfd_error_bad_value);
	  return FALSE;
	}
      else
	{
	  h = sym_hashes[r_symndx - extsymoff];

	  /* This may be an indirect symbol created because of a version.  */
	  if (h != NULL)
	    {
	      while (h->root.type == bfd_link_hash_indirect)
		h = (struct elf_link_hash_entry *) h->root.u.i.link;
	    }
	}

      /* Some relocs require a global offset table.  */
      if (dynobj == NULL || sgot == NULL)
	{
	  switch (r_type)
	    {
	    case R_MIPS_GOT16:
	    case R_MIPS_CALL16:
	    case R_MIPS_CALL_HI16:
	    case R_MIPS_CALL_LO16:
	    case R_MIPS_GOT_HI16:
	    case R_MIPS_GOT_LO16:
	    case R_MIPS_GOT_PAGE:
	    case R_MIPS_GOT_OFST:
	    case R_MIPS_GOT_DISP:
	    case R_MIPS_TLS_GOTTPREL:
	    case R_MIPS_TLS_GD:
	    case R_MIPS_TLS_LDM:
	      if (dynobj == NULL)
		elf_hash_table (info)->dynobj = dynobj = abfd;
	      if (! mips_elf_create_got_section (dynobj, info, FALSE))
		return FALSE;
	      g = mips_elf_got_info (dynobj, &sgot);
	      if (htab->is_vxworks && !info->shared)
		{
		  (*_bfd_error_handler)
		    (_("%B: GOT reloc at 0x%lx not expected in executables"),
		     abfd, (unsigned long) rel->r_offset);
		  bfd_set_error (bfd_error_bad_value);
		  return FALSE;
		}
	      break;

	    case R_MIPS_32:
	    case R_MIPS_REL32:
	    case R_MIPS_64:
	      /* In VxWorks executables, references to external symbols
		 are handled using copy relocs or PLT stubs, so there's
		 no need to add a dynamic relocation here.  */
	      if (dynobj == NULL
		  && (info->shared || (h != NULL && !htab->is_vxworks))
		  && (sec->flags & SEC_ALLOC) != 0)
		elf_hash_table (info)->dynobj = dynobj = abfd;
	      break;

	    default:
	      break;
	    }
	}

      if (h)
	{
	  ((struct mips_elf_link_hash_entry *) h)->is_relocation_target = TRUE;

	  /* Relocations against the special VxWorks __GOTT_BASE__ and
	     __GOTT_INDEX__ symbols must be left to the loader.  Allocate
	     room for them in .rela.dyn.  */
	  if (is_gott_symbol (info, h))
	    {
	      if (sreloc == NULL)
		{
		  sreloc = mips_elf_rel_dyn_section (info, TRUE);
		  if (sreloc == NULL)
		    return FALSE;
		}
	      mips_elf_allocate_dynamic_relocations (dynobj, info, 1);
	      if (MIPS_ELF_READONLY_SECTION (sec))
		/* We tell the dynamic linker that there are
		   relocations against the text segment.  */
		info->flags |= DF_TEXTREL;
	    }
	}
      else if (r_type == R_MIPS_CALL_LO16
	       || r_type == R_MIPS_GOT_LO16
	       || r_type == R_MIPS_GOT_DISP
	       || (r_type == R_MIPS_GOT16 && htab->is_vxworks))
	{
	  /* We may need a local GOT entry for this relocation.  We
	     don't count R_MIPS_GOT_PAGE because we can estimate the
	     maximum number of pages needed by looking at the size of
	     the segment.  Similar comments apply to R_MIPS_GOT16 and
	     R_MIPS_CALL16, except on VxWorks, where GOT relocations
	     always evaluate to "G".  We don't count R_MIPS_GOT_HI16, or
	     R_MIPS_CALL_HI16 because these are always followed by an
	     R_MIPS_GOT_LO16 or R_MIPS_CALL_LO16.  */
	  if (! mips_elf_record_local_got_symbol (abfd, r_symndx,
						  rel->r_addend, g, 0))
	    return FALSE;
	}

      switch (r_type)
	{
	case R_MIPS_CALL16:
	  if (h == NULL)
	    {
	      (*_bfd_error_handler)
		(_("%B: CALL16 reloc at 0x%lx not against global symbol"),
		 abfd, (unsigned long) rel->r_offset);
	      bfd_set_error (bfd_error_bad_value);
	      return FALSE;
	    }
	  /* Fall through.  */

	case R_MIPS_CALL_HI16:
	case R_MIPS_CALL_LO16:
	  if (h != NULL)
	    {
	      /* VxWorks call relocations point the function's .got.plt
		 entry, which will be allocated by adjust_dynamic_symbol.
		 Otherwise, this symbol requires a global GOT entry.  */
	      if (!htab->is_vxworks
		  && !mips_elf_record_global_got_symbol (h, abfd, info, g, 0))
		return FALSE;

	      /* We need a stub, not a plt entry for the undefined
		 function.  But we record it as if it needs plt.  See
		 _bfd_elf_adjust_dynamic_symbol.  */
	      h->needs_plt = 1;
	      h->type = STT_FUNC;
	    }
	  break;

	case R_MIPS_GOT_PAGE:
	  /* If this is a global, overridable symbol, GOT_PAGE will
	     decay to GOT_DISP, so we'll need a GOT entry for it.  */
	  if (h == NULL)
	    break;
	  else
	    {
	      struct mips_elf_link_hash_entry *hmips =
		(struct mips_elf_link_hash_entry *) h;

	      while (hmips->root.root.type == bfd_link_hash_indirect
		     || hmips->root.root.type == bfd_link_hash_warning)
		hmips = (struct mips_elf_link_hash_entry *)
		  hmips->root.root.u.i.link;

	      if (hmips->root.def_regular
		  && ! (info->shared && ! info->symbolic
			&& ! hmips->root.forced_local))
		break;
	    }
	  /* Fall through.  */

	case R_MIPS_GOT16:
	case R_MIPS_GOT_HI16:
	case R_MIPS_GOT_LO16:
	case R_MIPS_GOT_DISP:
	  if (h && ! mips_elf_record_global_got_symbol (h, abfd, info, g, 0))
	    return FALSE;
	  break;

	case R_MIPS_TLS_GOTTPREL:
	  if (info->shared)
	    info->flags |= DF_STATIC_TLS;
	  /* Fall through */

	case R_MIPS_TLS_LDM:
	  if (r_type == R_MIPS_TLS_LDM)
	    {
	      r_symndx = 0;
	      h = NULL;
	    }
	  /* Fall through */

	case R_MIPS_TLS_GD:
	  /* This symbol requires a global offset table entry, or two
	     for TLS GD relocations.  */
	  {
	    unsigned char flag = (r_type == R_MIPS_TLS_GD
				  ? GOT_TLS_GD
				  : r_type == R_MIPS_TLS_LDM
				  ? GOT_TLS_LDM
				  : GOT_TLS_IE);
	    if (h != NULL)
	      {
		struct mips_elf_link_hash_entry *hmips =
		  (struct mips_elf_link_hash_entry *) h;
		hmips->tls_type |= flag;

		if (h && ! mips_elf_record_global_got_symbol (h, abfd, info, g, flag))
		  return FALSE;
	      }
	    else
	      {
		BFD_ASSERT (flag == GOT_TLS_LDM || r_symndx != 0);

		if (! mips_elf_record_local_got_symbol (abfd, r_symndx,
							rel->r_addend, g, flag))
		  return FALSE;
	      }
	  }
	  break;

	case R_MIPS_32:
	case R_MIPS_REL32:
	case R_MIPS_64:
	  /* In VxWorks executables, references to external symbols
	     are handled using copy relocs or PLT stubs, so there's
	     no need to add a .rela.dyn entry for this relocation.  */
	  if ((info->shared || (h != NULL && !htab->is_vxworks))
	      && (sec->flags & SEC_ALLOC) != 0)
	    {
	      if (sreloc == NULL)
		{
		  sreloc = mips_elf_rel_dyn_section (info, TRUE);
		  if (sreloc == NULL)
		    return FALSE;
		}
	      if (info->shared)
		{
		  /* When creating a shared object, we must copy these
		     reloc types into the output file as R_MIPS_REL32
		     relocs.  Make room for this reloc in .rel(a).dyn.  */
		  mips_elf_allocate_dynamic_relocations (dynobj, info, 1);
		  if (MIPS_ELF_READONLY_SECTION (sec))
		    /* We tell the dynamic linker that there are
		       relocations against the text segment.  */
		    info->flags |= DF_TEXTREL;
		}
	      else
		{
		  struct mips_elf_link_hash_entry *hmips;

		  /* We only need to copy this reloc if the symbol is
                     defined in a dynamic object.  */
		  hmips = (struct mips_elf_link_hash_entry *) h;
		  ++hmips->possibly_dynamic_relocs;
		  if (MIPS_ELF_READONLY_SECTION (sec))
		    /* We need it to tell the dynamic linker if there
		       are relocations against the text segment.  */
		    hmips->readonly_reloc = TRUE;
		}

	      /* Even though we don't directly need a GOT entry for
		 this symbol, a symbol must have a dynamic symbol
		 table index greater that DT_MIPS_GOTSYM if there are
		 dynamic relocations against it.  This does not apply
		 to VxWorks, which does not have the usual coupling
		 between global GOT entries and .dynsym entries.  */
	      if (h != NULL && !htab->is_vxworks)
		{
		  if (dynobj == NULL)
		    elf_hash_table (info)->dynobj = dynobj = abfd;
		  if (! mips_elf_create_got_section (dynobj, info, TRUE))
		    return FALSE;
		  g = mips_elf_got_info (dynobj, &sgot);
		  if (! mips_elf_record_global_got_symbol (h, abfd, info, g, 0))
		    return FALSE;
		}
	    }

	  if (SGI_COMPAT (abfd))
	    mips_elf_hash_table (info)->compact_rel_size +=
	      sizeof (Elf32_External_crinfo);
	  break;

	case R_MIPS_PC16:
	  if (h)
	    ((struct mips_elf_link_hash_entry *) h)->is_branch_target = TRUE;
	  break;

	case R_MIPS_26:
	  if (h)
	    ((struct mips_elf_link_hash_entry *) h)->is_branch_target = TRUE;
	  /* Fall through.  */

	case R_MIPS_GPREL16:
	case R_MIPS_LITERAL:
	case R_MIPS_GPREL32:
	  if (SGI_COMPAT (abfd))
	    mips_elf_hash_table (info)->compact_rel_size +=
	      sizeof (Elf32_External_crinfo);
	  break;

	  /* This relocation describes the C++ object vtable hierarchy.
	     Reconstruct it for later use during GC.  */
	case R_MIPS_GNU_VTINHERIT:
	  if (!bfd_elf_gc_record_vtinherit (abfd, sec, h, rel->r_offset))
	    return FALSE;
	  break;

	  /* This relocation describes which C++ vtable entries are actually
	     used.  Record for later use during GC.  */
	case R_MIPS_GNU_VTENTRY:
	  if (!bfd_elf_gc_record_vtentry (abfd, sec, h, rel->r_offset))
	    return FALSE;
	  break;

	default:
	  break;
	}

      /* We must not create a stub for a symbol that has relocations
	 related to taking the function's address.  This doesn't apply to
	 VxWorks, where CALL relocs refer to a .got.plt entry instead of
	 a normal .got entry.  */
      if (!htab->is_vxworks && h != NULL)
	switch (r_type)
	  {
	  default:
	    ((struct mips_elf_link_hash_entry *) h)->no_fn_stub = TRUE;
	    break;
	  case R_MIPS_CALL16:
	  case R_MIPS_CALL_HI16:
	  case R_MIPS_CALL_LO16:
	  case R_MIPS_JALR:
	    break;
	  }

      /* If this reloc is not a 16 bit call, and it has a global
         symbol, then we will need the fn_stub if there is one.
         References from a stub section do not count.  */
      if (h != NULL
	  && r_type != R_MIPS16_26
	  && !mips16_stub_section_p (abfd, sec))
	{
	  struct mips_elf_link_hash_entry *mh;

	  mh = (struct mips_elf_link_hash_entry *) h;
	  mh->need_fn_stub = TRUE;
	}
    }

  return TRUE;
}

bfd_boolean
_bfd_mips_relax_section (bfd *abfd, asection *sec,
			 struct bfd_link_info *link_info,
			 bfd_boolean *again)
{
  Elf_Internal_Rela *internal_relocs;
  Elf_Internal_Rela *irel, *irelend;
  Elf_Internal_Shdr *symtab_hdr;
  bfd_byte *contents = NULL;
  size_t extsymoff;
  bfd_boolean changed_contents = FALSE;
  bfd_vma sec_start = sec->output_section->vma + sec->output_offset;
  Elf_Internal_Sym *isymbuf = NULL;

  /* We are not currently changing any sizes, so only one pass.  */
  *again = FALSE;

  if (link_info->relocatable)
    return TRUE;

  internal_relocs = _bfd_elf_link_read_relocs (abfd, sec, NULL, NULL,
					       link_info->keep_memory);
  if (internal_relocs == NULL)
    return TRUE;

  irelend = internal_relocs + sec->reloc_count
    * get_elf_backend_data (abfd)->s->int_rels_per_ext_rel;
  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;
  extsymoff = (elf_bad_symtab (abfd)) ? 0 : symtab_hdr->sh_info;

  for (irel = internal_relocs; irel < irelend; irel++)
    {
      bfd_vma symval;
      bfd_signed_vma sym_offset;
      unsigned int r_type;
      unsigned long r_symndx;
      asection *sym_sec;
      unsigned long instruction;

      /* Turn jalr into bgezal, and jr into beq, if they're marked
	 with a JALR relocation, that indicate where they jump to.
	 This saves some pipeline bubbles.  */
      r_type = ELF_R_TYPE (abfd, irel->r_info);
      if (r_type != R_MIPS_JALR)
	continue;

      r_symndx = ELF_R_SYM (abfd, irel->r_info);
      /* Compute the address of the jump target.  */
      if (r_symndx >= extsymoff)
	{
	  struct mips_elf_link_hash_entry *h
	    = ((struct mips_elf_link_hash_entry *)
	       elf_sym_hashes (abfd) [r_symndx - extsymoff]);

	  while (h->root.root.type == bfd_link_hash_indirect
		 || h->root.root.type == bfd_link_hash_warning)
	    h = (struct mips_elf_link_hash_entry *) h->root.root.u.i.link;

	  /* If a symbol is undefined, or if it may be overridden,
	     skip it.  */
	  if (! ((h->root.root.type == bfd_link_hash_defined
		  || h->root.root.type == bfd_link_hash_defweak)
		 && h->root.root.u.def.section)
	      || (link_info->shared && ! link_info->symbolic
		  && !h->root.forced_local))
	    continue;

	  sym_sec = h->root.root.u.def.section;
	  if (sym_sec->output_section)
	    symval = (h->root.root.u.def.value
		      + sym_sec->output_section->vma
		      + sym_sec->output_offset);
	  else
	    symval = h->root.root.u.def.value;
	}
      else
	{
	  Elf_Internal_Sym *isym;

	  /* Read this BFD's symbols if we haven't done so already.  */
	  if (isymbuf == NULL && symtab_hdr->sh_info != 0)
	    {
	      isymbuf = (Elf_Internal_Sym *) symtab_hdr->contents;
	      if (isymbuf == NULL)
		isymbuf = bfd_elf_get_elf_syms (abfd, symtab_hdr,
						symtab_hdr->sh_info, 0,
						NULL, NULL, NULL);
	      if (isymbuf == NULL)
		goto relax_return;
	    }

	  isym = isymbuf + r_symndx;
	  if (isym->st_shndx == SHN_UNDEF)
	    continue;
	  else if (isym->st_shndx == SHN_ABS)
	    sym_sec = bfd_abs_section_ptr;
	  else if (isym->st_shndx == SHN_COMMON)
	    sym_sec = bfd_com_section_ptr;
	  else
	    sym_sec
	      = bfd_section_from_elf_index (abfd, isym->st_shndx);
	  symval = isym->st_value
	    + sym_sec->output_section->vma
	    + sym_sec->output_offset;
	}

      /* Compute branch offset, from delay slot of the jump to the
	 branch target.  */
      sym_offset = (symval + irel->r_addend)
	- (sec_start + irel->r_offset + 4);

      /* Branch offset must be properly aligned.  */
      if ((sym_offset & 3) != 0)
	continue;

      sym_offset >>= 2;

      /* Check that it's in range.  */
      if (sym_offset < -0x8000 || sym_offset >= 0x8000)
	continue;

      /* Get the section contents if we haven't done so already.  */
      if (contents == NULL)
	{
	  /* Get cached copy if it exists.  */
	  if (elf_section_data (sec)->this_hdr.contents != NULL)
	    contents = elf_section_data (sec)->this_hdr.contents;
	  else
	    {
	      if (!bfd_malloc_and_get_section (abfd, sec, &contents))
		goto relax_return;
	    }
	}

      instruction = bfd_get_32 (abfd, contents + irel->r_offset);

      /* If it was jalr <reg>, turn it into bgezal $zero, <target>.  */
      if ((instruction & 0xfc1fffff) == 0x0000f809)
	instruction = 0x04110000;
      /* If it was jr <reg>, turn it into b <target>.  */
      else if ((instruction & 0xfc1fffff) == 0x00000008)
	instruction = 0x10000000;
      else
	continue;

      instruction |= (sym_offset & 0xffff);
      bfd_put_32 (abfd, instruction, contents + irel->r_offset);
      changed_contents = TRUE;
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
  return TRUE;

 relax_return:
  if (contents != NULL
      && elf_section_data (sec)->this_hdr.contents != contents)
    free (contents);
  return FALSE;
}

/* Adjust a symbol defined by a dynamic object and referenced by a
   regular object.  The current definition is in some section of the
   dynamic object, but we're not including those sections.  We have to
   change the definition to something the rest of the link can
   understand.  */

bfd_boolean
_bfd_mips_elf_adjust_dynamic_symbol (struct bfd_link_info *info,
				     struct elf_link_hash_entry *h)
{
  bfd *dynobj;
  struct mips_elf_link_hash_entry *hmips;
  asection *s;
  struct mips_elf_link_hash_table *htab;

  htab = mips_elf_hash_table (info);
  dynobj = elf_hash_table (info)->dynobj;

  /* Make sure we know what is going on here.  */
  BFD_ASSERT (dynobj != NULL
	      && (h->needs_plt
		  || h->u.weakdef != NULL
		  || (h->def_dynamic
		      && h->ref_regular
		      && !h->def_regular)));

  /* If this symbol is defined in a dynamic object, we need to copy
     any R_MIPS_32 or R_MIPS_REL32 relocs against it into the output
     file.  */
  hmips = (struct mips_elf_link_hash_entry *) h;
  if (! info->relocatable
      && hmips->possibly_dynamic_relocs != 0
      && (h->root.type == bfd_link_hash_defweak
	  || !h->def_regular))
    {
      mips_elf_allocate_dynamic_relocations
	(dynobj, info, hmips->possibly_dynamic_relocs);
      if (hmips->readonly_reloc)
	/* We tell the dynamic linker that there are relocations
	   against the text segment.  */
	info->flags |= DF_TEXTREL;
    }

  /* For a function, create a stub, if allowed.  */
  if (! hmips->no_fn_stub
      && h->needs_plt)
    {
      if (! elf_hash_table (info)->dynamic_sections_created)
	return TRUE;

      /* If this symbol is not defined in a regular file, then set
	 the symbol to the stub location.  This is required to make
	 function pointers compare as equal between the normal
	 executable and the shared library.  */
      if (!h->def_regular)
	{
	  /* We need .stub section.  */
	  s = bfd_get_section_by_name (dynobj,
				       MIPS_ELF_STUB_SECTION_NAME (dynobj));
	  BFD_ASSERT (s != NULL);

	  h->root.u.def.section = s;
	  h->root.u.def.value = s->size;

	  /* XXX Write this stub address somewhere.  */
	  h->plt.offset = s->size;

	  /* Make room for this stub code.  */
	  s->size += htab->function_stub_size;

	  /* The last half word of the stub will be filled with the index
	     of this symbol in .dynsym section.  */
	  return TRUE;
	}
    }
  else if ((h->type == STT_FUNC)
	   && !h->needs_plt)
    {
      /* This will set the entry for this symbol in the GOT to 0, and
         the dynamic linker will take care of this.  */
      h->root.u.def.value = 0;
      return TRUE;
    }

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

  /* This is a reference to a symbol defined by a dynamic object which
     is not a function.  */

  return TRUE;
}

/* Likewise, for VxWorks.  */

bfd_boolean
_bfd_mips_vxworks_adjust_dynamic_symbol (struct bfd_link_info *info,
					 struct elf_link_hash_entry *h)
{
  bfd *dynobj;
  struct mips_elf_link_hash_entry *hmips;
  struct mips_elf_link_hash_table *htab;

  htab = mips_elf_hash_table (info);
  dynobj = elf_hash_table (info)->dynobj;
  hmips = (struct mips_elf_link_hash_entry *) h;

  /* Make sure we know what is going on here.  */
  BFD_ASSERT (dynobj != NULL
	      && (h->needs_plt
		  || h->needs_copy
		  || h->u.weakdef != NULL
		  || (h->def_dynamic
		      && h->ref_regular
		      && !h->def_regular)));

  /* If the symbol is defined by a dynamic object, we need a PLT stub if
     either (a) we want to branch to the symbol or (b) we're linking an
     executable that needs a canonical function address.  In the latter
     case, the canonical address will be the address of the executable's
     load stub.  */
  if ((hmips->is_branch_target
       || (!info->shared
	   && h->type == STT_FUNC
	   && hmips->is_relocation_target))
      && h->def_dynamic
      && h->ref_regular
      && !h->def_regular
      && !h->forced_local)
    h->needs_plt = 1;

  /* Locally-binding symbols do not need a PLT stub; we can refer to
     the functions directly.  */
  else if (h->needs_plt
	   && (SYMBOL_CALLS_LOCAL (info, h)
	       || (ELF_ST_VISIBILITY (h->other) != STV_DEFAULT
		   && h->root.type == bfd_link_hash_undefweak)))
    {
      h->needs_plt = 0;
      return TRUE;
    }

  if (h->needs_plt)
    {
      /* If this is the first symbol to need a PLT entry, allocate room
	 for the header, and for the header's .rela.plt.unloaded entries.  */
      if (htab->splt->size == 0)
	{
	  htab->splt->size += htab->plt_header_size;
	  if (!info->shared)
	    htab->srelplt2->size += 2 * sizeof (Elf32_External_Rela);
	}

      /* Assign the next .plt entry to this symbol.  */
      h->plt.offset = htab->splt->size;
      htab->splt->size += htab->plt_entry_size;

      /* If the output file has no definition of the symbol, set the
	 symbol's value to the address of the stub.  For executables,
	 point at the PLT load stub rather than the lazy resolution stub;
	 this stub will become the canonical function address.  */
      if (!h->def_regular)
	{
	  h->root.u.def.section = htab->splt;
	  h->root.u.def.value = h->plt.offset;
	  if (!info->shared)
	    h->root.u.def.value += 8;
	}

      /* Make room for the .got.plt entry and the R_JUMP_SLOT relocation.  */
      htab->sgotplt->size += 4;
      htab->srelplt->size += sizeof (Elf32_External_Rela);

      /* Make room for the .rela.plt.unloaded relocations.  */
      if (!info->shared)
	htab->srelplt2->size += 3 * sizeof (Elf32_External_Rela);

      return TRUE;
    }

  /* If a function symbol is defined by a dynamic object, and we do not
     need a PLT stub for it, the symbol's value should be zero.  */
  if (h->type == STT_FUNC
      && h->def_dynamic
      && h->ref_regular
      && !h->def_regular)
    {
      h->root.u.def.value = 0;
      return TRUE;
    }

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

  /* This is a reference to a symbol defined by a dynamic object which
     is not a function.  */
  if (info->shared)
    return TRUE;

  /* We must allocate the symbol in our .dynbss section, which will
     become part of the .bss section of the executable.  There will be
     an entry for this symbol in the .dynsym section.  The dynamic
     object will contain position independent code, so all references
     from the dynamic object to this symbol will go through the global
     offset table.  The dynamic linker will use the .dynsym entry to
     determine the address it must put in the global offset table, so
     both the dynamic object and the regular object will refer to the
     same memory location for the variable.  */

  if ((h->root.u.def.section->flags & SEC_ALLOC) != 0)
    {
      htab->srelbss->size += sizeof (Elf32_External_Rela);
      h->needs_copy = 1;
    }

  return _bfd_elf_adjust_dynamic_copy (h, htab->sdynbss);
}

/* Return the number of dynamic section symbols required by OUTPUT_BFD.
   The number might be exact or a worst-case estimate, depending on how
   much information is available to elf_backend_omit_section_dynsym at
   the current linking stage.  */

static bfd_size_type
count_section_dynsyms (bfd *output_bfd, struct bfd_link_info *info)
{
  bfd_size_type count;

  count = 0;
  if (info->shared || elf_hash_table (info)->is_relocatable_executable)
    {
      asection *p;
      const struct elf_backend_data *bed;

      bed = get_elf_backend_data (output_bfd);
      for (p = output_bfd->sections; p ; p = p->next)
	if ((p->flags & SEC_EXCLUDE) == 0
	    && (p->flags & SEC_ALLOC) != 0
	    && !(*bed->elf_backend_omit_section_dynsym) (output_bfd, info, p))
	  ++count;
    }
  return count;
}

/* This function is called after all the input files have been read,
   and the input sections have been assigned to output sections.  We
   check for any mips16 stub sections that we can discard.  */

bfd_boolean
_bfd_mips_elf_always_size_sections (bfd *output_bfd,
				    struct bfd_link_info *info)
{
  asection *ri;

  bfd *dynobj;
  asection *s;
  struct mips_got_info *g;
  int i;
  bfd_size_type loadable_size = 0;
  bfd_size_type local_gotno;
  bfd_size_type dynsymcount;
  bfd *sub;
  struct mips_elf_count_tls_arg count_tls_arg;
  struct mips_elf_link_hash_table *htab;

  htab = mips_elf_hash_table (info);

  /* The .reginfo section has a fixed size.  */
  ri = bfd_get_section_by_name (output_bfd, ".reginfo");
  if (ri != NULL)
    bfd_set_section_size (output_bfd, ri, sizeof (Elf32_External_RegInfo));

  if (! (info->relocatable
	 || ! mips_elf_hash_table (info)->mips16_stubs_seen))
    mips_elf_link_hash_traverse (mips_elf_hash_table (info),
				 mips_elf_check_mips16_stubs, NULL);

  dynobj = elf_hash_table (info)->dynobj;
  if (dynobj == NULL)
    /* Relocatable links don't have it.  */
    return TRUE;

  g = mips_elf_got_info (dynobj, &s);
  if (s == NULL)
    return TRUE;

  /* Calculate the total loadable size of the output.  That
     will give us the maximum number of GOT_PAGE entries
     required.  */
  for (sub = info->input_bfds; sub; sub = sub->link_next)
    {
      asection *subsection;

      for (subsection = sub->sections;
	   subsection;
	   subsection = subsection->next)
	{
	  if ((subsection->flags & SEC_ALLOC) == 0)
	    continue;
	  loadable_size += ((subsection->size + 0xf)
			    &~ (bfd_size_type) 0xf);
	}
    }

  /* There has to be a global GOT entry for every symbol with
     a dynamic symbol table index of DT_MIPS_GOTSYM or
     higher.  Therefore, it make sense to put those symbols
     that need GOT entries at the end of the symbol table.  We
     do that here.  */
  if (! mips_elf_sort_hash_table (info, 1))
    return FALSE;

  if (g->global_gotsym != NULL)
    i = elf_hash_table (info)->dynsymcount - g->global_gotsym->dynindx;
  else
    /* If there are no global symbols, or none requiring
       relocations, then GLOBAL_GOTSYM will be NULL.  */
    i = 0;

  /* Get a worst-case estimate of the number of dynamic symbols needed.
     At this point, dynsymcount does not account for section symbols
     and count_section_dynsyms may overestimate the number that will
     be needed.  */
  dynsymcount = (elf_hash_table (info)->dynsymcount
		 + count_section_dynsyms (output_bfd, info));

  /* Determine the size of one stub entry.  */
  htab->function_stub_size = (dynsymcount > 0x10000
			      ? MIPS_FUNCTION_STUB_BIG_SIZE
			      : MIPS_FUNCTION_STUB_NORMAL_SIZE);

  /* In the worst case, we'll get one stub per dynamic symbol, plus
     one to account for the dummy entry at the end required by IRIX
     rld.  */
  loadable_size += htab->function_stub_size * (i + 1);

  if (htab->is_vxworks)
    /* There's no need to allocate page entries for VxWorks; R_MIPS_GOT16
       relocations against local symbols evaluate to "G", and the EABI does
       not include R_MIPS_GOT_PAGE.  */
    local_gotno = 0;
  else
    /* Assume there are two loadable segments consisting of contiguous
       sections.  Is 5 enough?  */
    local_gotno = (loadable_size >> 16) + 5;

  g->local_gotno += local_gotno;
  s->size += g->local_gotno * MIPS_ELF_GOT_SIZE (output_bfd);

  g->global_gotno = i;
  s->size += i * MIPS_ELF_GOT_SIZE (output_bfd);

  /* We need to calculate tls_gotno for global symbols at this point
     instead of building it up earlier, to avoid doublecounting
     entries for one global symbol from multiple input files.  */
  count_tls_arg.info = info;
  count_tls_arg.needed = 0;
  elf_link_hash_traverse (elf_hash_table (info),
			  mips_elf_count_global_tls_entries,
			  &count_tls_arg);
  g->tls_gotno += count_tls_arg.needed;
  s->size += g->tls_gotno * MIPS_ELF_GOT_SIZE (output_bfd);

  mips_elf_resolve_final_got_entries (g);

  /* VxWorks does not support multiple GOTs.  It initializes $gp to
     __GOTT_BASE__[__GOTT_INDEX__], the value of which is set by the
     dynamic loader.  */
  if (!htab->is_vxworks && s->size > MIPS_ELF_GOT_MAX_SIZE (info))
    {
      if (! mips_elf_multi_got (output_bfd, info, g, s, local_gotno))
	return FALSE;
    }
  else
    {
      /* Set up TLS entries for the first GOT.  */
      g->tls_assigned_gotno = g->global_gotno + g->local_gotno;
      htab_traverse (g->got_entries, mips_elf_initialize_tls_index, g);
    }

  return TRUE;
}

/* Set the sizes of the dynamic sections.  */

bfd_boolean
_bfd_mips_elf_size_dynamic_sections (bfd *output_bfd,
				     struct bfd_link_info *info)
{
  bfd *dynobj;
  asection *s, *sreldyn;
  bfd_boolean reltext;
  struct mips_elf_link_hash_table *htab;

  htab = mips_elf_hash_table (info);
  dynobj = elf_hash_table (info)->dynobj;
  BFD_ASSERT (dynobj != NULL);

  if (elf_hash_table (info)->dynamic_sections_created)
    {
      /* Set the contents of the .interp section to the interpreter.  */
      if (info->executable)
	{
	  s = bfd_get_section_by_name (dynobj, ".interp");
	  BFD_ASSERT (s != NULL);
	  s->size
	    = strlen (ELF_DYNAMIC_INTERPRETER (output_bfd)) + 1;
	  s->contents
	    = (bfd_byte *) ELF_DYNAMIC_INTERPRETER (output_bfd);
	}
    }

  /* The check_relocs and adjust_dynamic_symbol entry points have
     determined the sizes of the various dynamic sections.  Allocate
     memory for them.  */
  reltext = FALSE;
  sreldyn = NULL;
  for (s = dynobj->sections; s != NULL; s = s->next)
    {
      const char *name;

      /* It's OK to base decisions on the section name, because none
	 of the dynobj section names depend upon the input files.  */
      name = bfd_get_section_name (dynobj, s);

      if ((s->flags & SEC_LINKER_CREATED) == 0)
	continue;

      if (CONST_STRNEQ (name, ".rel"))
	{
	  if (s->size != 0)
	    {
	      const char *outname;
	      asection *target;

	      /* If this relocation section applies to a read only
                 section, then we probably need a DT_TEXTREL entry.
                 If the relocation section is .rel(a).dyn, we always
                 assert a DT_TEXTREL entry rather than testing whether
                 there exists a relocation to a read only section or
                 not.  */
	      outname = bfd_get_section_name (output_bfd,
					      s->output_section);
	      target = bfd_get_section_by_name (output_bfd, outname + 4);
	      if ((target != NULL
		   && (target->flags & SEC_READONLY) != 0
		   && (target->flags & SEC_ALLOC) != 0)
		  || strcmp (outname, MIPS_ELF_REL_DYN_NAME (info)) == 0)
		reltext = TRUE;

	      /* We use the reloc_count field as a counter if we need
		 to copy relocs into the output file.  */
	      if (strcmp (name, MIPS_ELF_REL_DYN_NAME (info)) != 0)
		s->reloc_count = 0;

	      /* If combreloc is enabled, elf_link_sort_relocs() will
		 sort relocations, but in a different way than we do,
		 and before we're done creating relocations.  Also, it
		 will move them around between input sections'
		 relocation's contents, so our sorting would be
		 broken, so don't let it run.  */
	      info->combreloc = 0;
	    }
	}
      else if (htab->is_vxworks && strcmp (name, ".got") == 0)
	{
	  /* Executables do not need a GOT.  */
	  if (info->shared)
	    {
	      /* Allocate relocations for all but the reserved entries.  */
	      struct mips_got_info *g;
	      unsigned int count;

	      g = mips_elf_got_info (dynobj, NULL);
	      count = (g->global_gotno
		       + g->local_gotno
		       - MIPS_RESERVED_GOTNO (info));
	      mips_elf_allocate_dynamic_relocations (dynobj, info, count);
	    }
	}
      else if (!htab->is_vxworks && CONST_STRNEQ (name, ".got"))
	{
	  /* _bfd_mips_elf_always_size_sections() has already done
	     most of the work, but some symbols may have been mapped
	     to versions that we must now resolve in the got_entries
	     hash tables.  */
	  struct mips_got_info *gg = mips_elf_got_info (dynobj, NULL);
	  struct mips_got_info *g = gg;
	  struct mips_elf_set_global_got_offset_arg set_got_offset_arg;
	  unsigned int needed_relocs = 0;

	  if (gg->next)
	    {
	      set_got_offset_arg.value = MIPS_ELF_GOT_SIZE (output_bfd);
	      set_got_offset_arg.info = info;

	      /* NOTE 2005-02-03: How can this call, or the next, ever
		 find any indirect entries to resolve?  They were all
		 resolved in mips_elf_multi_got.  */
	      mips_elf_resolve_final_got_entries (gg);
	      for (g = gg->next; g && g->next != gg; g = g->next)
		{
		  unsigned int save_assign;

		  mips_elf_resolve_final_got_entries (g);

		  /* Assign offsets to global GOT entries.  */
		  save_assign = g->assigned_gotno;
		  g->assigned_gotno = g->local_gotno;
		  set_got_offset_arg.g = g;
		  set_got_offset_arg.needed_relocs = 0;
		  htab_traverse (g->got_entries,
				 mips_elf_set_global_got_offset,
				 &set_got_offset_arg);
		  needed_relocs += set_got_offset_arg.needed_relocs;
		  BFD_ASSERT (g->assigned_gotno - g->local_gotno
			      <= g->global_gotno);

		  g->assigned_gotno = save_assign;
		  if (info->shared)
		    {
		      needed_relocs += g->local_gotno - g->assigned_gotno;
		      BFD_ASSERT (g->assigned_gotno == g->next->local_gotno
				  + g->next->global_gotno
				  + g->next->tls_gotno
				  + MIPS_RESERVED_GOTNO (info));
		    }
		}
	    }
	  else
	    {
	      struct mips_elf_count_tls_arg arg;
	      arg.info = info;
	      arg.needed = 0;

	      htab_traverse (gg->got_entries, mips_elf_count_local_tls_relocs,
			     &arg);
	      elf_link_hash_traverse (elf_hash_table (info),
				      mips_elf_count_global_tls_relocs,
				      &arg);

	      needed_relocs += arg.needed;
	    }

	  if (needed_relocs)
	    mips_elf_allocate_dynamic_relocations (dynobj, info,
						   needed_relocs);
	}
      else if (strcmp (name, MIPS_ELF_STUB_SECTION_NAME (output_bfd)) == 0)
	{
	  /* IRIX rld assumes that the function stub isn't at the end
	     of .text section.  So put a dummy.  XXX  */
	  s->size += htab->function_stub_size;
	}
      else if (! info->shared
	       && ! mips_elf_hash_table (info)->use_rld_obj_head
	       && CONST_STRNEQ (name, ".rld_map"))
	{
	  /* We add a room for __rld_map.  It will be filled in by the
	     rtld to contain a pointer to the _r_debug structure.  */
	  s->size += MIPS_ELF_RLD_MAP_SIZE (output_bfd);
	}
      else if (SGI_COMPAT (output_bfd)
	       && CONST_STRNEQ (name, ".compact_rel"))
	s->size += mips_elf_hash_table (info)->compact_rel_size;
      else if (! CONST_STRNEQ (name, ".init")
	       && s != htab->sgotplt
	       && s != htab->splt)
	{
	  /* It's not one of our sections, so don't allocate space.  */
	  continue;
	}

      if (s->size == 0)
	{
	  s->flags |= SEC_EXCLUDE;
	  continue;
	}

      if ((s->flags & SEC_HAS_CONTENTS) == 0)
	continue;

      /* Allocate memory for this section last, since we may increase its
	 size above.  */
      if (strcmp (name, MIPS_ELF_REL_DYN_NAME (info)) == 0)
	{
	  sreldyn = s;
	  continue;
	}

      /* Allocate memory for the section contents.  */
      s->contents = bfd_zalloc (dynobj, s->size);
      if (s->contents == NULL)
	{
	  bfd_set_error (bfd_error_no_memory);
	  return FALSE;
	}
    }

  /* Allocate memory for the .rel(a).dyn section.  */
  if (sreldyn != NULL)
    {
      sreldyn->contents = bfd_zalloc (dynobj, sreldyn->size);
      if (sreldyn->contents == NULL)
	{
	  bfd_set_error (bfd_error_no_memory);
	  return FALSE;
	}
    }

  if (elf_hash_table (info)->dynamic_sections_created)
    {
      /* Add some entries to the .dynamic section.  We fill in the
	 values later, in _bfd_mips_elf_finish_dynamic_sections, but we
	 must add the entries now so that we get the correct size for
	 the .dynamic section.  */

      /* SGI object has the equivalence of DT_DEBUG in the
	 DT_MIPS_RLD_MAP entry.  This must come first because glibc
	 only fills in DT_MIPS_RLD_MAP (not DT_DEBUG) and GDB only
	 looks at the first one it sees.  */
      if (!info->shared
	  && !MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_MIPS_RLD_MAP, 0))
	return FALSE;

      /* The DT_DEBUG entry may be filled in by the dynamic linker and
	 used by the debugger.  */
      if (info->executable
	  && !SGI_COMPAT (output_bfd)
	  && !MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_DEBUG, 0))
	return FALSE;

      if (reltext && (SGI_COMPAT (output_bfd) || htab->is_vxworks))
	info->flags |= DF_TEXTREL;

      if ((info->flags & DF_TEXTREL) != 0)
	{
	  if (! MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_TEXTREL, 0))
	    return FALSE;

	  /* Clear the DF_TEXTREL flag.  It will be set again if we
	     write out an actual text relocation; we may not, because
	     at this point we do not know whether e.g. any .eh_frame
	     absolute relocations have been converted to PC-relative.  */
	  info->flags &= ~DF_TEXTREL;
	}

      if (! MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_PLTGOT, 0))
	return FALSE;

      if (htab->is_vxworks)
	{
	  /* VxWorks uses .rela.dyn instead of .rel.dyn.  It does not
	     use any of the DT_MIPS_* tags.  */
	  if (mips_elf_rel_dyn_section (info, FALSE))
	    {
	      if (! MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_RELA, 0))
		return FALSE;

	      if (! MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_RELASZ, 0))
		return FALSE;

	      if (! MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_RELAENT, 0))
		return FALSE;
	    }
	  if (htab->splt->size > 0)
	    {
	      if (! MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_PLTREL, 0))
		return FALSE;

	      if (! MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_JMPREL, 0))
		return FALSE;

	      if (! MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_PLTRELSZ, 0))
		return FALSE;
	    }
	}
      else
	{
	  if (mips_elf_rel_dyn_section (info, FALSE))
	    {
	      if (! MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_REL, 0))
		return FALSE;

	      if (! MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_RELSZ, 0))
		return FALSE;

	      if (! MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_RELENT, 0))
		return FALSE;
	    }

	  if (! MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_MIPS_RLD_VERSION, 0))
	    return FALSE;

	  if (! MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_MIPS_FLAGS, 0))
	    return FALSE;

	  if (! MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_MIPS_BASE_ADDRESS, 0))
	    return FALSE;

	  if (! MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_MIPS_LOCAL_GOTNO, 0))
	    return FALSE;

	  if (! MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_MIPS_SYMTABNO, 0))
	    return FALSE;

	  if (! MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_MIPS_UNREFEXTNO, 0))
	    return FALSE;

	  if (! MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_MIPS_GOTSYM, 0))
	    return FALSE;

	  if (IRIX_COMPAT (dynobj) == ict_irix5
	      && ! MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_MIPS_HIPAGENO, 0))
	    return FALSE;

	  if (IRIX_COMPAT (dynobj) == ict_irix6
	      && (bfd_get_section_by_name
		  (dynobj, MIPS_ELF_OPTIONS_SECTION_NAME (dynobj)))
	      && !MIPS_ELF_ADD_DYNAMIC_ENTRY (info, DT_MIPS_OPTIONS, 0))
	    return FALSE;
	}
    }

  return TRUE;
}

/* REL is a relocation in INPUT_BFD that is being copied to OUTPUT_BFD.
   Adjust its R_ADDEND field so that it is correct for the output file.
   LOCAL_SYMS and LOCAL_SECTIONS are arrays of INPUT_BFD's local symbols
   and sections respectively; both use symbol indexes.  */

static void
mips_elf_adjust_addend (bfd *output_bfd, struct bfd_link_info *info,
			bfd *input_bfd, Elf_Internal_Sym *local_syms,
			asection **local_sections, Elf_Internal_Rela *rel)
{
  unsigned int r_type, r_symndx;
  Elf_Internal_Sym *sym;
  asection *sec;

  if (mips_elf_local_relocation_p (input_bfd, rel, local_sections, FALSE))
    {
      r_type = ELF_R_TYPE (output_bfd, rel->r_info);
      if (r_type == R_MIPS16_GPREL
	  || r_type == R_MIPS_GPREL16
	  || r_type == R_MIPS_GPREL32
	  || r_type == R_MIPS_LITERAL)
	{
	  rel->r_addend += _bfd_get_gp_value (input_bfd);
	  rel->r_addend -= _bfd_get_gp_value (output_bfd);
	}

      r_symndx = ELF_R_SYM (output_bfd, rel->r_info);
      sym = local_syms + r_symndx;

      /* Adjust REL's addend to account for section merging.  */
      if (!info->relocatable)
	{
	  sec = local_sections[r_symndx];
	  _bfd_elf_rela_local_sym (output_bfd, sym, &sec, rel);
	}

      /* This would normally be done by the rela_normal code in elflink.c.  */
      if (ELF_ST_TYPE (sym->st_info) == STT_SECTION)
	rel->r_addend += local_sections[r_symndx]->output_offset;
    }
}

/* Relocate a MIPS ELF section.  */

bfd_boolean
_bfd_mips_elf_relocate_section (bfd *output_bfd, struct bfd_link_info *info,
				bfd *input_bfd, asection *input_section,
				bfd_byte *contents, Elf_Internal_Rela *relocs,
				Elf_Internal_Sym *local_syms,
				asection **local_sections)
{
  Elf_Internal_Rela *rel;
  const Elf_Internal_Rela *relend;
  bfd_vma addend = 0;
  bfd_boolean use_saved_addend_p = FALSE;
  const struct elf_backend_data *bed;

  bed = get_elf_backend_data (output_bfd);
  relend = relocs + input_section->reloc_count * bed->s->int_rels_per_ext_rel;
  for (rel = relocs; rel < relend; ++rel)
    {
      const char *name;
      bfd_vma value = 0;
      reloc_howto_type *howto;
      bfd_boolean require_jalx;
      /* TRUE if the relocation is a RELA relocation, rather than a
         REL relocation.  */
      bfd_boolean rela_relocation_p = TRUE;
      unsigned int r_type = ELF_R_TYPE (output_bfd, rel->r_info);
      const char *msg;
      unsigned long r_symndx;
      asection *sec;
      Elf_Internal_Shdr *symtab_hdr;
      struct elf_link_hash_entry *h;

      /* Find the relocation howto for this relocation.  */
      howto = MIPS_ELF_RTYPE_TO_HOWTO (input_bfd, r_type,
				       NEWABI_P (input_bfd)
				       && (MIPS_RELOC_RELA_P
					   (input_bfd, input_section,
					    rel - relocs)));

      r_symndx = ELF_R_SYM (input_bfd, rel->r_info);
      symtab_hdr = &elf_tdata (input_bfd)->symtab_hdr;
      if (mips_elf_local_relocation_p (input_bfd, rel, local_sections, FALSE))
	{
	  sec = local_sections[r_symndx];
	  h = NULL;
	}
      else
	{
	  unsigned long extsymoff;

	  extsymoff = 0;
	  if (!elf_bad_symtab (input_bfd))
	    extsymoff = symtab_hdr->sh_info;
	  h = elf_sym_hashes (input_bfd) [r_symndx - extsymoff];
	  while (h->root.type == bfd_link_hash_indirect
		 || h->root.type == bfd_link_hash_warning)
	    h = (struct elf_link_hash_entry *) h->root.u.i.link;

	  sec = NULL;
	  if (h->root.type == bfd_link_hash_defined
	      || h->root.type == bfd_link_hash_defweak)
	    sec = h->root.u.def.section;
	}

      if (sec != NULL && elf_discarded_section (sec))
	{
	  /* For relocs against symbols from removed linkonce sections,
	     or sections discarded by a linker script, we just want the
	     section contents zeroed.  Avoid any special processing.  */
	  _bfd_clear_contents (howto, input_bfd, contents + rel->r_offset);
	  rel->r_info = 0;
	  rel->r_addend = 0;
	  continue;
	}

      if (r_type == R_MIPS_64 && ! NEWABI_P (input_bfd))
	{
	  /* Some 32-bit code uses R_MIPS_64.  In particular, people use
	     64-bit code, but make sure all their addresses are in the
	     lowermost or uppermost 32-bit section of the 64-bit address
	     space.  Thus, when they use an R_MIPS_64 they mean what is
	     usually meant by R_MIPS_32, with the exception that the
	     stored value is sign-extended to 64 bits.  */
	  howto = MIPS_ELF_RTYPE_TO_HOWTO (input_bfd, R_MIPS_32, FALSE);

	  /* On big-endian systems, we need to lie about the position
	     of the reloc.  */
	  if (bfd_big_endian (input_bfd))
	    rel->r_offset += 4;
	}

      if (!use_saved_addend_p)
	{
	  Elf_Internal_Shdr *rel_hdr;

	  /* If these relocations were originally of the REL variety,
	     we must pull the addend out of the field that will be
	     relocated.  Otherwise, we simply use the contents of the
	     RELA relocation.  To determine which flavor or relocation
	     this is, we depend on the fact that the INPUT_SECTION's
	     REL_HDR is read before its REL_HDR2.  */
	  rel_hdr = &elf_section_data (input_section)->rel_hdr;
	  if ((size_t) (rel - relocs)
	      >= (NUM_SHDR_ENTRIES (rel_hdr) * bed->s->int_rels_per_ext_rel))
	    rel_hdr = elf_section_data (input_section)->rel_hdr2;
	  if (rel_hdr->sh_entsize == MIPS_ELF_REL_SIZE (input_bfd))
	    {
	      bfd_byte *location = contents + rel->r_offset;

	      /* Note that this is a REL relocation.  */
	      rela_relocation_p = FALSE;

	      /* Get the addend, which is stored in the input file.  */
	      _bfd_mips16_elf_reloc_unshuffle (input_bfd, r_type, FALSE,
					       location);
	      addend = mips_elf_obtain_contents (howto, rel, input_bfd,
						 contents);
	      _bfd_mips16_elf_reloc_shuffle(input_bfd, r_type, FALSE,
					    location);

	      addend &= howto->src_mask;

	      /* For some kinds of relocations, the ADDEND is a
		 combination of the addend stored in two different
		 relocations.   */
	      if (r_type == R_MIPS_HI16 || r_type == R_MIPS16_HI16
		  || (r_type == R_MIPS_GOT16
		      && mips_elf_local_relocation_p (input_bfd, rel,
						      local_sections, FALSE)))
		{
		  const Elf_Internal_Rela *lo16_relocation;
		  reloc_howto_type *lo16_howto;
		  int lo16_type;

		  if (r_type == R_MIPS16_HI16)
		    lo16_type = R_MIPS16_LO16;
		  else
		    lo16_type = R_MIPS_LO16;

		  /* The combined value is the sum of the HI16 addend,
		     left-shifted by sixteen bits, and the LO16
		     addend, sign extended.  (Usually, the code does
		     a `lui' of the HI16 value, and then an `addiu' of
		     the LO16 value.)

		     Scan ahead to find a matching LO16 relocation.

		     According to the MIPS ELF ABI, the R_MIPS_LO16
		     relocation must be immediately following.
		     However, for the IRIX6 ABI, the next relocation
		     may be a composed relocation consisting of
		     several relocations for the same address.  In
		     that case, the R_MIPS_LO16 relocation may occur
		     as one of these.  We permit a similar extension
		     in general, as that is useful for GCC.

		     In some cases GCC dead code elimination removes
		     the LO16 but keeps the corresponding HI16.  This
		     is strictly speaking a violation of the ABI but
		     not immediately harmful.  */
		  lo16_relocation = mips_elf_next_relocation (input_bfd,
							      lo16_type,
							      rel, relend);
		  if (lo16_relocation == NULL)
		    {
		      const char *name;

		      if (h)
			name = h->root.root.string;
		      else
			name = bfd_elf_sym_name (input_bfd, symtab_hdr,
						 local_syms + r_symndx,
						 sec);
		      (*_bfd_error_handler)
			(_("%B: Can't find matching LO16 reloc against `%s' for %s at 0x%lx in section `%A'"),
			 input_bfd, input_section, name, howto->name,
			 rel->r_offset);
		    }
		  else
		    {
		      bfd_byte *lo16_location;
		      bfd_vma l;

		      lo16_location = contents + lo16_relocation->r_offset;

		      /* Obtain the addend kept there.  */
		      lo16_howto = MIPS_ELF_RTYPE_TO_HOWTO (input_bfd,
							    lo16_type, FALSE);
		      _bfd_mips16_elf_reloc_unshuffle (input_bfd, lo16_type,
						       FALSE, lo16_location);
		      l = mips_elf_obtain_contents (lo16_howto,
						    lo16_relocation,
						    input_bfd, contents);
		      _bfd_mips16_elf_reloc_shuffle (input_bfd, lo16_type,
						     FALSE, lo16_location);
		      l &= lo16_howto->src_mask;
		      l <<= lo16_howto->rightshift;
		      l = _bfd_mips_elf_sign_extend (l, 16);

		      addend <<= 16;

		      /* Compute the combined addend.  */
		      addend += l;
		    }
		}
	      else
		addend <<= howto->rightshift;
	    }
	  else
	    addend = rel->r_addend;
	  mips_elf_adjust_addend (output_bfd, info, input_bfd,
				  local_syms, local_sections, rel);
	}

      if (info->relocatable)
	{
	  if (r_type == R_MIPS_64 && ! NEWABI_P (output_bfd)
	      && bfd_big_endian (input_bfd))
	    rel->r_offset -= 4;

	  if (!rela_relocation_p && rel->r_addend)
	    {
	      addend += rel->r_addend;
	      if (r_type == R_MIPS_HI16
		  || r_type == R_MIPS_GOT16)
		addend = mips_elf_high (addend);
	      else if (r_type == R_MIPS_HIGHER)
		addend = mips_elf_higher (addend);
	      else if (r_type == R_MIPS_HIGHEST)
		addend = mips_elf_highest (addend);
	      else
		addend >>= howto->rightshift;

	      /* We use the source mask, rather than the destination
		 mask because the place to which we are writing will be
		 source of the addend in the final link.  */
	      addend &= howto->src_mask;

	      if (r_type == R_MIPS_64 && ! NEWABI_P (output_bfd))
		/* See the comment above about using R_MIPS_64 in the 32-bit
		   ABI.  Here, we need to update the addend.  It would be
		   possible to get away with just using the R_MIPS_32 reloc
		   but for endianness.  */
		{
		  bfd_vma sign_bits;
		  bfd_vma low_bits;
		  bfd_vma high_bits;

		  if (addend & ((bfd_vma) 1 << 31))
#ifdef BFD64
		    sign_bits = ((bfd_vma) 1 << 32) - 1;
#else
		    sign_bits = -1;
#endif
		  else
		    sign_bits = 0;

		  /* If we don't know that we have a 64-bit type,
		     do two separate stores.  */
		  if (bfd_big_endian (input_bfd))
		    {
		      /* Store the sign-bits (which are most significant)
			 first.  */
		      low_bits = sign_bits;
		      high_bits = addend;
		    }
		  else
		    {
		      low_bits = addend;
		      high_bits = sign_bits;
		    }
		  bfd_put_32 (input_bfd, low_bits,
			      contents + rel->r_offset);
		  bfd_put_32 (input_bfd, high_bits,
			      contents + rel->r_offset + 4);
		  continue;
		}

	      if (! mips_elf_perform_relocation (info, howto, rel, addend,
						 input_bfd, input_section,
						 contents, FALSE))
		return FALSE;
	    }

	  /* Go on to the next relocation.  */
	  continue;
	}

      /* In the N32 and 64-bit ABIs there may be multiple consecutive
	 relocations for the same offset.  In that case we are
	 supposed to treat the output of each relocation as the addend
	 for the next.  */
      if (rel + 1 < relend
	  && rel->r_offset == rel[1].r_offset
	  && ELF_R_TYPE (input_bfd, rel[1].r_info) != R_MIPS_NONE)
	use_saved_addend_p = TRUE;
      else
	use_saved_addend_p = FALSE;

      /* Figure out what value we are supposed to relocate.  */
      switch (mips_elf_calculate_relocation (output_bfd, input_bfd,
					     input_section, info, rel,
					     addend, howto, local_syms,
					     local_sections, &value,
					     &name, &require_jalx,
					     use_saved_addend_p))
	{
	case bfd_reloc_continue:
	  /* There's nothing to do.  */
	  continue;

	case bfd_reloc_undefined:
	  /* mips_elf_calculate_relocation already called the
	     undefined_symbol callback.  There's no real point in
	     trying to perform the relocation at this point, so we
	     just skip ahead to the next relocation.  */
	  continue;

	case bfd_reloc_notsupported:
	  msg = _("internal error: unsupported relocation error");
	  info->callbacks->warning
	    (info, msg, name, input_bfd, input_section, rel->r_offset);
	  return FALSE;

	case bfd_reloc_overflow:
	  if (use_saved_addend_p)
	    /* Ignore overflow until we reach the last relocation for
	       a given location.  */
	    ;
	  else
	    {
	      BFD_ASSERT (name != NULL);
	      if (! ((*info->callbacks->reloc_overflow)
		     (info, NULL, name, howto->name, (bfd_vma) 0,
		      input_bfd, input_section, rel->r_offset)))
		return FALSE;
	    }
	  break;

	case bfd_reloc_ok:
	  break;

	default:
	  abort ();
	  break;
	}

      /* If we've got another relocation for the address, keep going
	 until we reach the last one.  */
      if (use_saved_addend_p)
	{
	  addend = value;
	  continue;
	}

      if (r_type == R_MIPS_64 && ! NEWABI_P (output_bfd))
	/* See the comment above about using R_MIPS_64 in the 32-bit
	   ABI.  Until now, we've been using the HOWTO for R_MIPS_32;
	   that calculated the right value.  Now, however, we
	   sign-extend the 32-bit result to 64-bits, and store it as a
	   64-bit value.  We are especially generous here in that we
	   go to extreme lengths to support this usage on systems with
	   only a 32-bit VMA.  */
	{
	  bfd_vma sign_bits;
	  bfd_vma low_bits;
	  bfd_vma high_bits;

	  if (value & ((bfd_vma) 1 << 31))
#ifdef BFD64
	    sign_bits = ((bfd_vma) 1 << 32) - 1;
#else
	    sign_bits = -1;
#endif
	  else
	    sign_bits = 0;

	  /* If we don't know that we have a 64-bit type,
	     do two separate stores.  */
	  if (bfd_big_endian (input_bfd))
	    {
	      /* Undo what we did above.  */
	      rel->r_offset -= 4;
	      /* Store the sign-bits (which are most significant)
		 first.  */
	      low_bits = sign_bits;
	      high_bits = value;
	    }
	  else
	    {
	      low_bits = value;
	      high_bits = sign_bits;
	    }
	  bfd_put_32 (input_bfd, low_bits,
		      contents + rel->r_offset);
	  bfd_put_32 (input_bfd, high_bits,
		      contents + rel->r_offset + 4);
	  continue;
	}

      /* Actually perform the relocation.  */
      if (! mips_elf_perform_relocation (info, howto, rel, value,
					 input_bfd, input_section,
					 contents, require_jalx))
	return FALSE;
    }

  return TRUE;
}

/* If NAME is one of the special IRIX6 symbols defined by the linker,
   adjust it appropriately now.  */

static void
mips_elf_irix6_finish_dynamic_symbol (bfd *abfd ATTRIBUTE_UNUSED,
				      const char *name, Elf_Internal_Sym *sym)
{
  /* The linker script takes care of providing names and values for
     these, but we must place them into the right sections.  */
  static const char* const text_section_symbols[] = {
    "_ftext",
    "_etext",
    "__dso_displacement",
    "__elf_header",
    "__program_header_table",
    NULL
  };

  static const char* const data_section_symbols[] = {
    "_fdata",
    "_edata",
    "_end",
    "_fbss",
    NULL
  };

  const char* const *p;
  int i;

  for (i = 0; i < 2; ++i)
    for (p = (i == 0) ? text_section_symbols : data_section_symbols;
	 *p;
	 ++p)
      if (strcmp (*p, name) == 0)
	{
	  /* All of these symbols are given type STT_SECTION by the
	     IRIX6 linker.  */
	  sym->st_info = ELF_ST_INFO (STB_GLOBAL, STT_SECTION);
	  sym->st_other = STO_PROTECTED;

	  /* The IRIX linker puts these symbols in special sections.  */
	  if (i == 0)
	    sym->st_shndx = SHN_MIPS_TEXT;
	  else
	    sym->st_shndx = SHN_MIPS_DATA;

	  break;
	}
}

/* Finish up dynamic symbol handling.  We set the contents of various
   dynamic sections here.  */

bfd_boolean
_bfd_mips_elf_finish_dynamic_symbol (bfd *output_bfd,
				     struct bfd_link_info *info,
				     struct elf_link_hash_entry *h,
				     Elf_Internal_Sym *sym)
{
  bfd *dynobj;
  asection *sgot;
  struct mips_got_info *g, *gg;
  const char *name;
  int idx;
  struct mips_elf_link_hash_table *htab;

  htab = mips_elf_hash_table (info);
  dynobj = elf_hash_table (info)->dynobj;

  if (h->plt.offset != MINUS_ONE)
    {
      asection *s;
      bfd_byte stub[MIPS_FUNCTION_STUB_BIG_SIZE];

      /* This symbol has a stub.  Set it up.  */

      BFD_ASSERT (h->dynindx != -1);

      s = bfd_get_section_by_name (dynobj,
				   MIPS_ELF_STUB_SECTION_NAME (dynobj));
      BFD_ASSERT (s != NULL);

      BFD_ASSERT ((htab->function_stub_size == MIPS_FUNCTION_STUB_BIG_SIZE)
                  || (h->dynindx <= 0xffff));

      /* Values up to 2^31 - 1 are allowed.  Larger values would cause
	 sign extension at runtime in the stub, resulting in a negative
	 index value.  */
      if (h->dynindx & ~0x7fffffff)
	return FALSE;

      /* Fill the stub.  */
      idx = 0;
      bfd_put_32 (output_bfd, STUB_LW (output_bfd), stub + idx);
      idx += 4;
      bfd_put_32 (output_bfd, STUB_MOVE (output_bfd), stub + idx);
      idx += 4;
      if (htab->function_stub_size == MIPS_FUNCTION_STUB_BIG_SIZE)
        {
          bfd_put_32 (output_bfd, STUB_LUI ((h->dynindx >> 16) & 0x7fff),
                      stub + idx);
          idx += 4;
        }
      bfd_put_32 (output_bfd, STUB_JALR, stub + idx);
      idx += 4;

      /* If a large stub is not required and sign extension is not a
         problem, then use legacy code in the stub.  */
      if (htab->function_stub_size == MIPS_FUNCTION_STUB_BIG_SIZE)
	bfd_put_32 (output_bfd, STUB_ORI (h->dynindx & 0xffff), stub + idx);
      else if (h->dynindx & ~0x7fff)
        bfd_put_32 (output_bfd, STUB_LI16U (h->dynindx & 0xffff), stub + idx);
      else
        bfd_put_32 (output_bfd, STUB_LI16S (output_bfd, h->dynindx),
		    stub + idx);

      BFD_ASSERT (h->plt.offset <= s->size);
      memcpy (s->contents + h->plt.offset, stub, htab->function_stub_size);

      /* Mark the symbol as undefined.  plt.offset != -1 occurs
	 only for the referenced symbol.  */
      sym->st_shndx = SHN_UNDEF;

      /* The run-time linker uses the st_value field of the symbol
	 to reset the global offset table entry for this external
	 to its stub address when unlinking a shared object.  */
      sym->st_value = (s->output_section->vma + s->output_offset
		       + h->plt.offset);
    }

  BFD_ASSERT (h->dynindx != -1
	      || h->forced_local);

  sgot = mips_elf_got_section (dynobj, FALSE);
  BFD_ASSERT (sgot != NULL);
  BFD_ASSERT (mips_elf_section_data (sgot) != NULL);
  g = mips_elf_section_data (sgot)->u.got_info;
  BFD_ASSERT (g != NULL);

  /* Run through the global symbol table, creating GOT entries for all
     the symbols that need them.  */
  if (g->global_gotsym != NULL
      && h->dynindx >= g->global_gotsym->dynindx)
    {
      bfd_vma offset;
      bfd_vma value;

      value = sym->st_value;
      offset = mips_elf_global_got_index (dynobj, output_bfd, h, R_MIPS_GOT16, info);
      MIPS_ELF_PUT_WORD (output_bfd, value, sgot->contents + offset);
    }

  if (g->next && h->dynindx != -1 && h->type != STT_TLS)
    {
      struct mips_got_entry e, *p;
      bfd_vma entry;
      bfd_vma offset;

      gg = g;

      e.abfd = output_bfd;
      e.symndx = -1;
      e.d.h = (struct mips_elf_link_hash_entry *)h;
      e.tls_type = 0;

      for (g = g->next; g->next != gg; g = g->next)
	{
	  if (g->got_entries
	      && (p = (struct mips_got_entry *) htab_find (g->got_entries,
							   &e)))
	    {
	      offset = p->gotidx;
	      if (info->shared
		  || (elf_hash_table (info)->dynamic_sections_created
		      && p->d.h != NULL
		      && p->d.h->root.def_dynamic
		      && !p->d.h->root.def_regular))
		{
		  /* Create an R_MIPS_REL32 relocation for this entry.  Due to
		     the various compatibility problems, it's easier to mock
		     up an R_MIPS_32 or R_MIPS_64 relocation and leave
		     mips_elf_create_dynamic_relocation to calculate the
		     appropriate addend.  */
		  Elf_Internal_Rela rel[3];

		  memset (rel, 0, sizeof (rel));
		  if (ABI_64_P (output_bfd))
		    rel[0].r_info = ELF_R_INFO (output_bfd, 0, R_MIPS_64);
		  else
		    rel[0].r_info = ELF_R_INFO (output_bfd, 0, R_MIPS_32);
		  rel[0].r_offset = rel[1].r_offset = rel[2].r_offset = offset;

		  entry = 0;
		  if (! (mips_elf_create_dynamic_relocation
			 (output_bfd, info, rel,
			  e.d.h, NULL, sym->st_value, &entry, sgot)))
		    return FALSE;
		}
	      else
		entry = sym->st_value;
	      MIPS_ELF_PUT_WORD (output_bfd, entry, sgot->contents + offset);
	    }
	}
    }

  /* Mark _DYNAMIC and _GLOBAL_OFFSET_TABLE_ as absolute.  */
  name = h->root.root.string;
  if (strcmp (name, "_DYNAMIC") == 0
      || h == elf_hash_table (info)->hgot)
    sym->st_shndx = SHN_ABS;
  else if (strcmp (name, "_DYNAMIC_LINK") == 0
	   || strcmp (name, "_DYNAMIC_LINKING") == 0)
    {
      sym->st_shndx = SHN_ABS;
      sym->st_info = ELF_ST_INFO (STB_GLOBAL, STT_SECTION);
      sym->st_value = 1;
    }
  else if (strcmp (name, "_gp_disp") == 0 && ! NEWABI_P (output_bfd))
    {
      sym->st_shndx = SHN_ABS;
      sym->st_info = ELF_ST_INFO (STB_GLOBAL, STT_SECTION);
      sym->st_value = elf_gp (output_bfd);
    }
  else if (SGI_COMPAT (output_bfd))
    {
      if (strcmp (name, mips_elf_dynsym_rtproc_names[0]) == 0
	  || strcmp (name, mips_elf_dynsym_rtproc_names[1]) == 0)
	{
	  sym->st_info = ELF_ST_INFO (STB_GLOBAL, STT_SECTION);
	  sym->st_other = STO_PROTECTED;
	  sym->st_value = 0;
	  sym->st_shndx = SHN_MIPS_DATA;
	}
      else if (strcmp (name, mips_elf_dynsym_rtproc_names[2]) == 0)
	{
	  sym->st_info = ELF_ST_INFO (STB_GLOBAL, STT_SECTION);
	  sym->st_other = STO_PROTECTED;
	  sym->st_value = mips_elf_hash_table (info)->procedure_count;
	  sym->st_shndx = SHN_ABS;
	}
      else if (sym->st_shndx != SHN_UNDEF && sym->st_shndx != SHN_ABS)
	{
	  if (h->type == STT_FUNC)
	    sym->st_shndx = SHN_MIPS_TEXT;
	  else if (h->type == STT_OBJECT)
	    sym->st_shndx = SHN_MIPS_DATA;
	}
    }

  /* Handle the IRIX6-specific symbols.  */
  if (IRIX_COMPAT (output_bfd) == ict_irix6)
    mips_elf_irix6_finish_dynamic_symbol (output_bfd, name, sym);

  if (! info->shared)
    {
      if (! mips_elf_hash_table (info)->use_rld_obj_head
	  && (strcmp (name, "__rld_map") == 0
	      || strcmp (name, "__RLD_MAP") == 0))
	{
	  asection *s = bfd_get_section_by_name (dynobj, ".rld_map");
	  BFD_ASSERT (s != NULL);
	  sym->st_value = s->output_section->vma + s->output_offset;
	  bfd_put_32 (output_bfd, 0, s->contents);
	  if (mips_elf_hash_table (info)->rld_value == 0)
	    mips_elf_hash_table (info)->rld_value = sym->st_value;
	}
      else if (mips_elf_hash_table (info)->use_rld_obj_head
	       && strcmp (name, "__rld_obj_head") == 0)
	{
	  /* IRIX6 does not use a .rld_map section.  */
	  if (IRIX_COMPAT (output_bfd) == ict_irix5
              || IRIX_COMPAT (output_bfd) == ict_none)
	    BFD_ASSERT (bfd_get_section_by_name (dynobj, ".rld_map")
			!= NULL);
	  mips_elf_hash_table (info)->rld_value = sym->st_value;
	}
    }

  /* If this is a mips16 symbol, force the value to be even.  */
  if (sym->st_other == STO_MIPS16)
    sym->st_value &= ~1;

  return TRUE;
}

/* Likewise, for VxWorks.  */

bfd_boolean
_bfd_mips_vxworks_finish_dynamic_symbol (bfd *output_bfd,
					 struct bfd_link_info *info,
					 struct elf_link_hash_entry *h,
					 Elf_Internal_Sym *sym)
{
  bfd *dynobj;
  asection *sgot;
  struct mips_got_info *g;
  struct mips_elf_link_hash_table *htab;

  htab = mips_elf_hash_table (info);
  dynobj = elf_hash_table (info)->dynobj;

  if (h->plt.offset != (bfd_vma) -1)
    {
      bfd_byte *loc;
      bfd_vma plt_address, plt_index, got_address, got_offset, branch_offset;
      Elf_Internal_Rela rel;
      static const bfd_vma *plt_entry;

      BFD_ASSERT (h->dynindx != -1);
      BFD_ASSERT (htab->splt != NULL);
      BFD_ASSERT (h->plt.offset <= htab->splt->size);

      /* Calculate the address of the .plt entry.  */
      plt_address = (htab->splt->output_section->vma
		     + htab->splt->output_offset
		     + h->plt.offset);

      /* Calculate the index of the entry.  */
      plt_index = ((h->plt.offset - htab->plt_header_size)
		   / htab->plt_entry_size);

      /* Calculate the address of the .got.plt entry.  */
      got_address = (htab->sgotplt->output_section->vma
		     + htab->sgotplt->output_offset
		     + plt_index * 4);

      /* Calculate the offset of the .got.plt entry from
	 _GLOBAL_OFFSET_TABLE_.  */
      got_offset = mips_elf_gotplt_index (info, h);

      /* Calculate the offset for the branch at the start of the PLT
	 entry.  The branch jumps to the beginning of .plt.  */
      branch_offset = -(h->plt.offset / 4 + 1) & 0xffff;

      /* Fill in the initial value of the .got.plt entry.  */
      bfd_put_32 (output_bfd, plt_address,
		  htab->sgotplt->contents + plt_index * 4);

      /* Find out where the .plt entry should go.  */
      loc = htab->splt->contents + h->plt.offset;

      if (info->shared)
	{
	  plt_entry = mips_vxworks_shared_plt_entry;
	  bfd_put_32 (output_bfd, plt_entry[0] | branch_offset, loc);
	  bfd_put_32 (output_bfd, plt_entry[1] | plt_index, loc + 4);
	}
      else
	{
	  bfd_vma got_address_high, got_address_low;

	  plt_entry = mips_vxworks_exec_plt_entry;
	  got_address_high = ((got_address + 0x8000) >> 16) & 0xffff;
	  got_address_low = got_address & 0xffff;

	  bfd_put_32 (output_bfd, plt_entry[0] | branch_offset, loc);
	  bfd_put_32 (output_bfd, plt_entry[1] | plt_index, loc + 4);
	  bfd_put_32 (output_bfd, plt_entry[2] | got_address_high, loc + 8);
	  bfd_put_32 (output_bfd, plt_entry[3] | got_address_low, loc + 12);
	  bfd_put_32 (output_bfd, plt_entry[4], loc + 16);
	  bfd_put_32 (output_bfd, plt_entry[5], loc + 20);
	  bfd_put_32 (output_bfd, plt_entry[6], loc + 24);
	  bfd_put_32 (output_bfd, plt_entry[7], loc + 28);

	  loc = (htab->srelplt2->contents
		 + (plt_index * 3 + 2) * sizeof (Elf32_External_Rela));

	  /* Emit a relocation for the .got.plt entry.  */
	  rel.r_offset = got_address;
	  rel.r_info = ELF32_R_INFO (htab->root.hplt->indx, R_MIPS_32);
	  rel.r_addend = h->plt.offset;
	  bfd_elf32_swap_reloca_out (output_bfd, &rel, loc);

	  /* Emit a relocation for the lui of %hi(<.got.plt slot>).  */
	  loc += sizeof (Elf32_External_Rela);
	  rel.r_offset = plt_address + 8;
	  rel.r_info = ELF32_R_INFO (htab->root.hgot->indx, R_MIPS_HI16);
	  rel.r_addend = got_offset;
	  bfd_elf32_swap_reloca_out (output_bfd, &rel, loc);

	  /* Emit a relocation for the addiu of %lo(<.got.plt slot>).  */
	  loc += sizeof (Elf32_External_Rela);
	  rel.r_offset += 4;
	  rel.r_info = ELF32_R_INFO (htab->root.hgot->indx, R_MIPS_LO16);
	  bfd_elf32_swap_reloca_out (output_bfd, &rel, loc);
	}

      /* Emit an R_MIPS_JUMP_SLOT relocation against the .got.plt entry.  */
      loc = htab->srelplt->contents + plt_index * sizeof (Elf32_External_Rela);
      rel.r_offset = got_address;
      rel.r_info = ELF32_R_INFO (h->dynindx, R_MIPS_JUMP_SLOT);
      rel.r_addend = 0;
      bfd_elf32_swap_reloca_out (output_bfd, &rel, loc);

      if (!h->def_regular)
	sym->st_shndx = SHN_UNDEF;
    }

  BFD_ASSERT (h->dynindx != -1 || h->forced_local);

  sgot = mips_elf_got_section (dynobj, FALSE);
  BFD_ASSERT (sgot != NULL);
  BFD_ASSERT (mips_elf_section_data (sgot) != NULL);
  g = mips_elf_section_data (sgot)->u.got_info;
  BFD_ASSERT (g != NULL);

  /* See if this symbol has an entry in the GOT.  */
  if (g->global_gotsym != NULL
      && h->dynindx >= g->global_gotsym->dynindx)
    {
      bfd_vma offset;
      Elf_Internal_Rela outrel;
      bfd_byte *loc;
      asection *s;

      /* Install the symbol value in the GOT.   */
      offset = mips_elf_global_got_index (dynobj, output_bfd, h,
					  R_MIPS_GOT16, info);
      MIPS_ELF_PUT_WORD (output_bfd, sym->st_value, sgot->contents + offset);

      /* Add a dynamic relocation for it.  */
      s = mips_elf_rel_dyn_section (info, FALSE);
      loc = s->contents + (s->reloc_count++ * sizeof (Elf32_External_Rela));
      outrel.r_offset = (sgot->output_section->vma
			 + sgot->output_offset
			 + offset);
      outrel.r_info = ELF32_R_INFO (h->dynindx, R_MIPS_32);
      outrel.r_addend = 0;
      bfd_elf32_swap_reloca_out (dynobj, &outrel, loc);
    }

  /* Emit a copy reloc, if needed.  */
  if (h->needs_copy)
    {
      Elf_Internal_Rela rel;

      BFD_ASSERT (h->dynindx != -1);

      rel.r_offset = (h->root.u.def.section->output_section->vma
		      + h->root.u.def.section->output_offset
		      + h->root.u.def.value);
      rel.r_info = ELF32_R_INFO (h->dynindx, R_MIPS_COPY);
      rel.r_addend = 0;
      bfd_elf32_swap_reloca_out (output_bfd, &rel,
				 htab->srelbss->contents
				 + (htab->srelbss->reloc_count
				    * sizeof (Elf32_External_Rela)));
      ++htab->srelbss->reloc_count;
    }

  /* If this is a mips16 symbol, force the value to be even.  */
  if (sym->st_other == STO_MIPS16)
    sym->st_value &= ~1;

  return TRUE;
}

/* Install the PLT header for a VxWorks executable and finalize the
   contents of .rela.plt.unloaded.  */

static void
mips_vxworks_finish_exec_plt (bfd *output_bfd, struct bfd_link_info *info)
{
  Elf_Internal_Rela rela;
  bfd_byte *loc;
  bfd_vma got_value, got_value_high, got_value_low, plt_address;
  static const bfd_vma *plt_entry;
  struct mips_elf_link_hash_table *htab;

  htab = mips_elf_hash_table (info);
  plt_entry = mips_vxworks_exec_plt0_entry;

  /* Calculate the value of _GLOBAL_OFFSET_TABLE_.  */
  got_value = (htab->root.hgot->root.u.def.section->output_section->vma
	       + htab->root.hgot->root.u.def.section->output_offset
	       + htab->root.hgot->root.u.def.value);

  got_value_high = ((got_value + 0x8000) >> 16) & 0xffff;
  got_value_low = got_value & 0xffff;

  /* Calculate the address of the PLT header.  */
  plt_address = htab->splt->output_section->vma + htab->splt->output_offset;

  /* Install the PLT header.  */
  loc = htab->splt->contents;
  bfd_put_32 (output_bfd, plt_entry[0] | got_value_high, loc);
  bfd_put_32 (output_bfd, plt_entry[1] | got_value_low, loc + 4);
  bfd_put_32 (output_bfd, plt_entry[2], loc + 8);
  bfd_put_32 (output_bfd, plt_entry[3], loc + 12);
  bfd_put_32 (output_bfd, plt_entry[4], loc + 16);
  bfd_put_32 (output_bfd, plt_entry[5], loc + 20);

  /* Output the relocation for the lui of %hi(_GLOBAL_OFFSET_TABLE_).  */
  loc = htab->srelplt2->contents;
  rela.r_offset = plt_address;
  rela.r_info = ELF32_R_INFO (htab->root.hgot->indx, R_MIPS_HI16);
  rela.r_addend = 0;
  bfd_elf32_swap_reloca_out (output_bfd, &rela, loc);
  loc += sizeof (Elf32_External_Rela);

  /* Output the relocation for the following addiu of
     %lo(_GLOBAL_OFFSET_TABLE_).  */
  rela.r_offset += 4;
  rela.r_info = ELF32_R_INFO (htab->root.hgot->indx, R_MIPS_LO16);
  bfd_elf32_swap_reloca_out (output_bfd, &rela, loc);
  loc += sizeof (Elf32_External_Rela);

  /* Fix up the remaining relocations.  They may have the wrong
     symbol index for _G_O_T_ or _P_L_T_ depending on the order
     in which symbols were output.  */
  while (loc < htab->srelplt2->contents + htab->srelplt2->size)
    {
      Elf_Internal_Rela rel;

      bfd_elf32_swap_reloca_in (output_bfd, loc, &rel);
      rel.r_info = ELF32_R_INFO (htab->root.hplt->indx, R_MIPS_32);
      bfd_elf32_swap_reloca_out (output_bfd, &rel, loc);
      loc += sizeof (Elf32_External_Rela);

      bfd_elf32_swap_reloca_in (output_bfd, loc, &rel);
      rel.r_info = ELF32_R_INFO (htab->root.hgot->indx, R_MIPS_HI16);
      bfd_elf32_swap_reloca_out (output_bfd, &rel, loc);
      loc += sizeof (Elf32_External_Rela);

      bfd_elf32_swap_reloca_in (output_bfd, loc, &rel);
      rel.r_info = ELF32_R_INFO (htab->root.hgot->indx, R_MIPS_LO16);
      bfd_elf32_swap_reloca_out (output_bfd, &rel, loc);
      loc += sizeof (Elf32_External_Rela);
    }
}

/* Install the PLT header for a VxWorks shared library.  */

static void
mips_vxworks_finish_shared_plt (bfd *output_bfd, struct bfd_link_info *info)
{
  unsigned int i;
  struct mips_elf_link_hash_table *htab;

  htab = mips_elf_hash_table (info);

  /* We just need to copy the entry byte-by-byte.  */
  for (i = 0; i < ARRAY_SIZE (mips_vxworks_shared_plt0_entry); i++)
    bfd_put_32 (output_bfd, mips_vxworks_shared_plt0_entry[i],
		htab->splt->contents + i * 4);
}

/* Finish up the dynamic sections.  */

bfd_boolean
_bfd_mips_elf_finish_dynamic_sections (bfd *output_bfd,
				       struct bfd_link_info *info)
{
  bfd *dynobj;
  asection *sdyn;
  asection *sgot;
  struct mips_got_info *gg, *g;
  struct mips_elf_link_hash_table *htab;

  htab = mips_elf_hash_table (info);
  dynobj = elf_hash_table (info)->dynobj;

  sdyn = bfd_get_section_by_name (dynobj, ".dynamic");

  sgot = mips_elf_got_section (dynobj, FALSE);
  if (sgot == NULL)
    gg = g = NULL;
  else
    {
      BFD_ASSERT (mips_elf_section_data (sgot) != NULL);
      gg = mips_elf_section_data (sgot)->u.got_info;
      BFD_ASSERT (gg != NULL);
      g = mips_elf_got_for_ibfd (gg, output_bfd);
      BFD_ASSERT (g != NULL);
    }

  if (elf_hash_table (info)->dynamic_sections_created)
    {
      bfd_byte *b;
      int dyn_to_skip = 0, dyn_skipped = 0;

      BFD_ASSERT (sdyn != NULL);
      BFD_ASSERT (g != NULL);

      for (b = sdyn->contents;
	   b < sdyn->contents + sdyn->size;
	   b += MIPS_ELF_DYN_SIZE (dynobj))
	{
	  Elf_Internal_Dyn dyn;
	  const char *name;
	  size_t elemsize;
	  asection *s;
	  bfd_boolean swap_out_p;

	  /* Read in the current dynamic entry.  */
	  (*get_elf_backend_data (dynobj)->s->swap_dyn_in) (dynobj, b, &dyn);

	  /* Assume that we're going to modify it and write it out.  */
	  swap_out_p = TRUE;

	  switch (dyn.d_tag)
	    {
	    case DT_RELENT:
	      dyn.d_un.d_val = MIPS_ELF_REL_SIZE (dynobj);
	      break;

	    case DT_RELAENT:
	      BFD_ASSERT (htab->is_vxworks);
	      dyn.d_un.d_val = MIPS_ELF_RELA_SIZE (dynobj);
	      break;

	    case DT_STRSZ:
	      /* Rewrite DT_STRSZ.  */
	      dyn.d_un.d_val =
		_bfd_elf_strtab_size (elf_hash_table (info)->dynstr);
	      break;

	    case DT_PLTGOT:
	      name = ".got";
	      if (htab->is_vxworks)
		{
		  /* _GLOBAL_OFFSET_TABLE_ is defined to be the beginning
		     of the ".got" section in DYNOBJ.  */
		  s = bfd_get_section_by_name (dynobj, name);
		  BFD_ASSERT (s != NULL);
		  dyn.d_un.d_ptr = s->output_section->vma + s->output_offset;
		}
	      else
		{
		  s = bfd_get_section_by_name (output_bfd, name);
		  BFD_ASSERT (s != NULL);
		  dyn.d_un.d_ptr = s->vma;
		}
	      break;

	    case DT_MIPS_RLD_VERSION:
	      dyn.d_un.d_val = 1; /* XXX */
	      break;

	    case DT_MIPS_FLAGS:
	      dyn.d_un.d_val = RHF_NOTPOT; /* XXX */
	      break;

	    case DT_MIPS_TIME_STAMP:
	      {
		time_t t;
		time (&t);
		dyn.d_un.d_val = t;
	      }
	      break;

	    case DT_MIPS_ICHECKSUM:
	      /* XXX FIXME: */
	      swap_out_p = FALSE;
	      break;

	    case DT_MIPS_IVERSION:
	      /* XXX FIXME: */
	      swap_out_p = FALSE;
	      break;

	    case DT_MIPS_BASE_ADDRESS:
	      s = output_bfd->sections;
	      BFD_ASSERT (s != NULL);
	      dyn.d_un.d_ptr = s->vma & ~(bfd_vma) 0xffff;
	      break;

	    case DT_MIPS_LOCAL_GOTNO:
	      dyn.d_un.d_val = g->local_gotno;
	      break;

	    case DT_MIPS_UNREFEXTNO:
	      /* The index into the dynamic symbol table which is the
		 entry of the first external symbol that is not
		 referenced within the same object.  */
	      dyn.d_un.d_val = bfd_count_sections (output_bfd) + 1;
	      break;

	    case DT_MIPS_GOTSYM:
	      if (gg->global_gotsym)
		{
		  dyn.d_un.d_val = gg->global_gotsym->dynindx;
		  break;
		}
	      /* In case if we don't have global got symbols we default
		 to setting DT_MIPS_GOTSYM to the same value as
		 DT_MIPS_SYMTABNO, so we just fall through.  */

	    case DT_MIPS_SYMTABNO:
	      name = ".dynsym";
	      elemsize = MIPS_ELF_SYM_SIZE (output_bfd);
	      s = bfd_get_section_by_name (output_bfd, name);
	      BFD_ASSERT (s != NULL);

	      dyn.d_un.d_val = s->size / elemsize;
	      break;

	    case DT_MIPS_HIPAGENO:
	      dyn.d_un.d_val = g->local_gotno - MIPS_RESERVED_GOTNO (info);
	      break;

	    case DT_MIPS_RLD_MAP:
	      dyn.d_un.d_ptr = mips_elf_hash_table (info)->rld_value;
	      break;

	    case DT_MIPS_OPTIONS:
	      s = (bfd_get_section_by_name
		   (output_bfd, MIPS_ELF_OPTIONS_SECTION_NAME (output_bfd)));
	      dyn.d_un.d_ptr = s->vma;
	      break;

	    case DT_RELASZ:
	      BFD_ASSERT (htab->is_vxworks);
	      /* The count does not include the JUMP_SLOT relocations.  */
	      if (htab->srelplt)
		dyn.d_un.d_val -= htab->srelplt->size;
	      break;

	    case DT_PLTREL:
	      BFD_ASSERT (htab->is_vxworks);
	      dyn.d_un.d_val = DT_RELA;
	      break;

	    case DT_PLTRELSZ:
	      BFD_ASSERT (htab->is_vxworks);
	      dyn.d_un.d_val = htab->srelplt->size;
	      break;

	    case DT_JMPREL:
	      BFD_ASSERT (htab->is_vxworks);
	      dyn.d_un.d_val = (htab->srelplt->output_section->vma
				+ htab->srelplt->output_offset);
	      break;

	    case DT_TEXTREL:
	      /* If we didn't need any text relocations after all, delete
		 the dynamic tag.  */
	      if (!(info->flags & DF_TEXTREL))
		{
		  dyn_to_skip = MIPS_ELF_DYN_SIZE (dynobj);
		  swap_out_p = FALSE;
		}
	      break;

	    case DT_FLAGS:
	      /* If we didn't need any text relocations after all, clear
		 DF_TEXTREL from DT_FLAGS.  */
	      if (!(info->flags & DF_TEXTREL))
		dyn.d_un.d_val &= ~DF_TEXTREL;
	      else
		swap_out_p = FALSE;
	      break;

	    default:
	      swap_out_p = FALSE;
	      break;
	    }

	  if (swap_out_p || dyn_skipped)
	    (*get_elf_backend_data (dynobj)->s->swap_dyn_out)
	      (dynobj, &dyn, b - dyn_skipped);

	  if (dyn_to_skip)
	    {
	      dyn_skipped += dyn_to_skip;
	      dyn_to_skip = 0;
	    }
	}

      /* Wipe out any trailing entries if we shifted down a dynamic tag.  */
      if (dyn_skipped > 0)
	memset (b - dyn_skipped, 0, dyn_skipped);
    }

  if (sgot != NULL && sgot->size > 0)
    {
      if (htab->is_vxworks)
	{
	  /* The first entry of the global offset table points to the
	     ".dynamic" section.  The second is initialized by the
	     loader and contains the shared library identifier.
	     The third is also initialized by the loader and points
	     to the lazy resolution stub.  */
	  MIPS_ELF_PUT_WORD (output_bfd,
			     sdyn->output_offset + sdyn->output_section->vma,
			     sgot->contents);
	  MIPS_ELF_PUT_WORD (output_bfd, 0,
			     sgot->contents + MIPS_ELF_GOT_SIZE (output_bfd));
	  MIPS_ELF_PUT_WORD (output_bfd, 0,
			     sgot->contents
			     + 2 * MIPS_ELF_GOT_SIZE (output_bfd));
	}
      else
	{
	  /* The first entry of the global offset table will be filled at
	     runtime. The second entry will be used by some runtime loaders.
	     This isn't the case of IRIX rld.  */
	  MIPS_ELF_PUT_WORD (output_bfd, (bfd_vma) 0, sgot->contents);
	  MIPS_ELF_PUT_WORD (output_bfd, (bfd_vma) 0x80000000,
			     sgot->contents + MIPS_ELF_GOT_SIZE (output_bfd));
	}

      elf_section_data (sgot->output_section)->this_hdr.sh_entsize
	 = MIPS_ELF_GOT_SIZE (output_bfd);
    }

  /* Generate dynamic relocations for the non-primary gots.  */
  if (gg != NULL && gg->next)
    {
      Elf_Internal_Rela rel[3];
      bfd_vma addend = 0;

      memset (rel, 0, sizeof (rel));
      rel[0].r_info = ELF_R_INFO (output_bfd, 0, R_MIPS_REL32);

      for (g = gg->next; g->next != gg; g = g->next)
	{
	  bfd_vma index = g->next->local_gotno + g->next->global_gotno
	    + g->next->tls_gotno;

	  MIPS_ELF_PUT_WORD (output_bfd, 0, sgot->contents
			     + index++ * MIPS_ELF_GOT_SIZE (output_bfd));
	  MIPS_ELF_PUT_WORD (output_bfd, 0x80000000, sgot->contents
			     + index++ * MIPS_ELF_GOT_SIZE (output_bfd));

	  if (! info->shared)
	    continue;

	  while (index < g->assigned_gotno)
	    {
	      rel[0].r_offset = rel[1].r_offset = rel[2].r_offset
		= index++ * MIPS_ELF_GOT_SIZE (output_bfd);
	      if (!(mips_elf_create_dynamic_relocation
		    (output_bfd, info, rel, NULL,
		     bfd_abs_section_ptr,
		     0, &addend, sgot)))
		return FALSE;
	      BFD_ASSERT (addend == 0);
	    }
	}
    }

  /* The generation of dynamic relocations for the non-primary gots
     adds more dynamic relocations.  We cannot count them until
     here.  */

  if (elf_hash_table (info)->dynamic_sections_created)
    {
      bfd_byte *b;
      bfd_boolean swap_out_p;

      BFD_ASSERT (sdyn != NULL);

      for (b = sdyn->contents;
	   b < sdyn->contents + sdyn->size;
	   b += MIPS_ELF_DYN_SIZE (dynobj))
	{
	  Elf_Internal_Dyn dyn;
	  asection *s;

	  /* Read in the current dynamic entry.  */
	  (*get_elf_backend_data (dynobj)->s->swap_dyn_in) (dynobj, b, &dyn);

	  /* Assume that we're going to modify it and write it out.  */
	  swap_out_p = TRUE;

	  switch (dyn.d_tag)
	    {
	    case DT_RELSZ:
	      /* Reduce DT_RELSZ to account for any relocations we
		 decided not to make.  This is for the n64 irix rld,
		 which doesn't seem to apply any relocations if there
		 are trailing null entries.  */
	      s = mips_elf_rel_dyn_section (info, FALSE);
	      dyn.d_un.d_val = (s->reloc_count
				* (ABI_64_P (output_bfd)
				   ? sizeof (Elf64_Mips_External_Rel)
				   : sizeof (Elf32_External_Rel)));
	      /* Adjust the section size too.  Tools like the prelinker
		 can reasonably expect the values to the same.  */
	      elf_section_data (s->output_section)->this_hdr.sh_size
		= dyn.d_un.d_val;
	      break;

	    default:
	      swap_out_p = FALSE;
	      break;
	    }

	  if (swap_out_p)
	    (*get_elf_backend_data (dynobj)->s->swap_dyn_out)
	      (dynobj, &dyn, b);
	}
    }

  {
    asection *s;
    Elf32_compact_rel cpt;

    if (SGI_COMPAT (output_bfd))
      {
	/* Write .compact_rel section out.  */
	s = bfd_get_section_by_name (dynobj, ".compact_rel");
	if (s != NULL)
	  {
	    cpt.id1 = 1;
	    cpt.num = s->reloc_count;
	    cpt.id2 = 2;
	    cpt.offset = (s->output_section->filepos
			  + sizeof (Elf32_External_compact_rel));
	    cpt.reserved0 = 0;
	    cpt.reserved1 = 0;
	    bfd_elf32_swap_compact_rel_out (output_bfd, &cpt,
					    ((Elf32_External_compact_rel *)
					     s->contents));

	    /* Clean up a dummy stub function entry in .text.  */
	    s = bfd_get_section_by_name (dynobj,
					 MIPS_ELF_STUB_SECTION_NAME (dynobj));
	    if (s != NULL)
	      {
		file_ptr dummy_offset;

		BFD_ASSERT (s->size >= htab->function_stub_size);
		dummy_offset = s->size - htab->function_stub_size;
		memset (s->contents + dummy_offset, 0,
			htab->function_stub_size);
	      }
	  }
      }

    /* The psABI says that the dynamic relocations must be sorted in
       increasing order of r_symndx.  The VxWorks EABI doesn't require
       this, and because the code below handles REL rather than RELA
       relocations, using it for VxWorks would be outright harmful.  */
    if (!htab->is_vxworks)
      {
	s = mips_elf_rel_dyn_section (info, FALSE);
	if (s != NULL
	    && s->size > (bfd_vma)2 * MIPS_ELF_REL_SIZE (output_bfd))
	  {
	    reldyn_sorting_bfd = output_bfd;

	    if (ABI_64_P (output_bfd))
	      qsort ((Elf64_External_Rel *) s->contents + 1,
		     s->reloc_count - 1, sizeof (Elf64_Mips_External_Rel),
		     sort_dynamic_relocs_64);
	    else
	      qsort ((Elf32_External_Rel *) s->contents + 1,
		     s->reloc_count - 1, sizeof (Elf32_External_Rel),
		     sort_dynamic_relocs);
	  }
      }
  }

  if (htab->is_vxworks && htab->splt->size > 0)
    {
      if (info->shared)
	mips_vxworks_finish_shared_plt (output_bfd, info);
      else
	mips_vxworks_finish_exec_plt (output_bfd, info);
    }
  return TRUE;
}


/* Set ABFD's EF_MIPS_ARCH and EF_MIPS_MACH flags.  */

static void
mips_set_isa_flags (bfd *abfd)
{
  flagword val;

  switch (bfd_get_mach (abfd))
    {
    default:
    case bfd_mach_mips3000:
      val = E_MIPS_ARCH_1;
      break;

    case bfd_mach_mips3900:
      val = E_MIPS_ARCH_1 | E_MIPS_MACH_3900;
      break;

    case bfd_mach_mips6000:
      val = E_MIPS_ARCH_2;
      break;

    case bfd_mach_mips4000:
    case bfd_mach_mips4300:
    case bfd_mach_mips4400:
    case bfd_mach_mips4600:
      val = E_MIPS_ARCH_3;
      break;

    case bfd_mach_mips4010:
      val = E_MIPS_ARCH_3 | E_MIPS_MACH_4010;
      break;

    case bfd_mach_mips4100:
      val = E_MIPS_ARCH_3 | E_MIPS_MACH_4100;
      break;

    case bfd_mach_mips4111:
      val = E_MIPS_ARCH_3 | E_MIPS_MACH_4111;
      break;

    case bfd_mach_mips4120:
      val = E_MIPS_ARCH_3 | E_MIPS_MACH_4120;
      break;

    case bfd_mach_mips4650:
      val = E_MIPS_ARCH_3 | E_MIPS_MACH_4650;
      break;

    case bfd_mach_mips5400:
      val = E_MIPS_ARCH_4 | E_MIPS_MACH_5400;
      break;

    case bfd_mach_mips5500:
      val = E_MIPS_ARCH_4 | E_MIPS_MACH_5500;
      break;

    case bfd_mach_mips9000:
      val = E_MIPS_ARCH_4 | E_MIPS_MACH_9000;
      break;

    case bfd_mach_mips5000:
    case bfd_mach_mips7000:
    case bfd_mach_mips8000:
    case bfd_mach_mips10000:
    case bfd_mach_mips12000:
      val = E_MIPS_ARCH_4;
      break;

    case bfd_mach_mips5:
      val = E_MIPS_ARCH_5;
      break;

    case bfd_mach_mips_octeon:
      val = E_MIPS_ARCH_64R2 | E_MIPS_MACH_OCTEON;
      break;

    case bfd_mach_mips_sb1:
      val = E_MIPS_ARCH_64 | E_MIPS_MACH_SB1;
      break;

    case bfd_mach_mipsisa32:
      val = E_MIPS_ARCH_32;
      break;

    case bfd_mach_mipsisa64:
      val = E_MIPS_ARCH_64;
      break;

    case bfd_mach_mipsisa32r2:
      val = E_MIPS_ARCH_32R2;
      break;

    case bfd_mach_mipsisa64r2:
      val = E_MIPS_ARCH_64R2;
      break;
    }
  elf_elfheader (abfd)->e_flags &= ~(EF_MIPS_ARCH | EF_MIPS_MACH);
  elf_elfheader (abfd)->e_flags |= val;

}


/* The final processing done just before writing out a MIPS ELF object
   file.  This gets the MIPS architecture right based on the machine
   number.  This is used by both the 32-bit and the 64-bit ABI.  */

void
_bfd_mips_elf_final_write_processing (bfd *abfd,
				      bfd_boolean linker ATTRIBUTE_UNUSED)
{
  unsigned int i;
  Elf_Internal_Shdr **hdrpp;
  const char *name;
  asection *sec;

  /* Keep the existing EF_MIPS_MACH and EF_MIPS_ARCH flags if the former
     is nonzero.  This is for compatibility with old objects, which used
     a combination of a 32-bit EF_MIPS_ARCH and a 64-bit EF_MIPS_MACH.  */
  if ((elf_elfheader (abfd)->e_flags & EF_MIPS_MACH) == 0)
    mips_set_isa_flags (abfd);

  /* Set the sh_info field for .gptab sections and other appropriate
     info for each special section.  */
  for (i = 1, hdrpp = elf_elfsections (abfd) + 1;
       i < elf_numsections (abfd);
       i++, hdrpp++)
    {
      switch ((*hdrpp)->sh_type)
	{
	case SHT_MIPS_MSYM:
	case SHT_MIPS_LIBLIST:
	  sec = bfd_get_section_by_name (abfd, ".dynstr");
	  if (sec != NULL)
	    (*hdrpp)->sh_link = elf_section_data (sec)->this_idx;
	  break;

	case SHT_MIPS_GPTAB:
	  BFD_ASSERT ((*hdrpp)->bfd_section != NULL);
	  name = bfd_get_section_name (abfd, (*hdrpp)->bfd_section);
	  BFD_ASSERT (name != NULL
		      && CONST_STRNEQ (name, ".gptab."));
	  sec = bfd_get_section_by_name (abfd, name + sizeof ".gptab" - 1);
	  BFD_ASSERT (sec != NULL);
	  (*hdrpp)->sh_info = elf_section_data (sec)->this_idx;
	  break;

	case SHT_MIPS_CONTENT:
	  BFD_ASSERT ((*hdrpp)->bfd_section != NULL);
	  name = bfd_get_section_name (abfd, (*hdrpp)->bfd_section);
	  BFD_ASSERT (name != NULL
		      && CONST_STRNEQ (name, ".MIPS.content"));
	  sec = bfd_get_section_by_name (abfd,
					 name + sizeof ".MIPS.content" - 1);
	  BFD_ASSERT (sec != NULL);
	  (*hdrpp)->sh_link = elf_section_data (sec)->this_idx;
	  break;

	case SHT_MIPS_SYMBOL_LIB:
	  sec = bfd_get_section_by_name (abfd, ".dynsym");
	  if (sec != NULL)
	    (*hdrpp)->sh_link = elf_section_data (sec)->this_idx;
	  sec = bfd_get_section_by_name (abfd, ".liblist");
	  if (sec != NULL)
	    (*hdrpp)->sh_info = elf_section_data (sec)->this_idx;
	  break;

	case SHT_MIPS_EVENTS:
	  BFD_ASSERT ((*hdrpp)->bfd_section != NULL);
	  name = bfd_get_section_name (abfd, (*hdrpp)->bfd_section);
	  BFD_ASSERT (name != NULL);
	  if (CONST_STRNEQ (name, ".MIPS.events"))
	    sec = bfd_get_section_by_name (abfd,
					   name + sizeof ".MIPS.events" - 1);
	  else
	    {
	      BFD_ASSERT (CONST_STRNEQ (name, ".MIPS.post_rel"));
	      sec = bfd_get_section_by_name (abfd,
					     (name
					      + sizeof ".MIPS.post_rel" - 1));
	    }
	  BFD_ASSERT (sec != NULL);
	  (*hdrpp)->sh_link = elf_section_data (sec)->this_idx;
	  break;

	}
    }
}

/* When creating an IRIX5 executable, we need REGINFO and RTPROC
   segments.  */

int
_bfd_mips_elf_additional_program_headers (bfd *abfd,
					  struct bfd_link_info *info ATTRIBUTE_UNUSED)
{
  asection *s;
  int ret = 0;

  /* See if we need a PT_MIPS_REGINFO segment.  */
  s = bfd_get_section_by_name (abfd, ".reginfo");
  if (s && (s->flags & SEC_LOAD))
    ++ret;

  /* See if we need a PT_MIPS_OPTIONS segment.  */
  if (IRIX_COMPAT (abfd) == ict_irix6
      && bfd_get_section_by_name (abfd,
				  MIPS_ELF_OPTIONS_SECTION_NAME (abfd)))
    ++ret;

  /* See if we need a PT_MIPS_RTPROC segment.  */
  if (IRIX_COMPAT (abfd) == ict_irix5
      && bfd_get_section_by_name (abfd, ".dynamic")
      && bfd_get_section_by_name (abfd, ".mdebug"))
    ++ret;

  /* Allocate a PT_NULL header in dynamic objects.  See
     _bfd_mips_elf_modify_segment_map for details.  */
  if (!SGI_COMPAT (abfd)
      && bfd_get_section_by_name (abfd, ".dynamic"))
    ++ret;

  return ret;
}

/* Modify the segment map for an IRIX5 executable.  */

bfd_boolean
_bfd_mips_elf_modify_segment_map (bfd *abfd,
				  struct bfd_link_info *info ATTRIBUTE_UNUSED)
{
  asection *s;
  struct elf_segment_map *m, **pm;
  bfd_size_type amt;

  /* If there is a .reginfo section, we need a PT_MIPS_REGINFO
     segment.  */
  s = bfd_get_section_by_name (abfd, ".reginfo");
  if (s != NULL && (s->flags & SEC_LOAD) != 0)
    {
      for (m = elf_tdata (abfd)->segment_map; m != NULL; m = m->next)
	if (m->p_type == PT_MIPS_REGINFO)
	  break;
      if (m == NULL)
	{
	  amt = sizeof *m;
	  m = bfd_zalloc (abfd, amt);
	  if (m == NULL)
	    return FALSE;

	  m->p_type = PT_MIPS_REGINFO;
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

  /* For IRIX 6, we don't have .mdebug sections, nor does anything but
     .dynamic end up in PT_DYNAMIC.  However, we do have to insert a
     PT_MIPS_OPTIONS segment immediately following the program header
     table.  */
  if (NEWABI_P (abfd)
      /* On non-IRIX6 new abi, we'll have already created a segment
	 for this section, so don't create another.  I'm not sure this
	 is not also the case for IRIX 6, but I can't test it right
	 now.  */
      && IRIX_COMPAT (abfd) == ict_irix6)
    {
      for (s = abfd->sections; s; s = s->next)
	if (elf_section_data (s)->this_hdr.sh_type == SHT_MIPS_OPTIONS)
	  break;

      if (s)
	{
	  struct elf_segment_map *options_segment;

	  pm = &elf_tdata (abfd)->segment_map;
	  while (*pm != NULL
		 && ((*pm)->p_type == PT_PHDR
		     || (*pm)->p_type == PT_INTERP))
	    pm = &(*pm)->next;

	  if (*pm == NULL || (*pm)->p_type != PT_MIPS_OPTIONS)
	    {
	      amt = sizeof (struct elf_segment_map);
	      options_segment = bfd_zalloc (abfd, amt);
	      options_segment->next = *pm;
	      options_segment->p_type = PT_MIPS_OPTIONS;
	      options_segment->p_flags = PF_R;
	      options_segment->p_flags_valid = TRUE;
	      options_segment->count = 1;
	      options_segment->sections[0] = s;
	      *pm = options_segment;
	    }
	}
    }
  else
    {
      if (IRIX_COMPAT (abfd) == ict_irix5)
	{
	  /* If there are .dynamic and .mdebug sections, we make a room
	     for the RTPROC header.  FIXME: Rewrite without section names.  */
	  if (bfd_get_section_by_name (abfd, ".interp") == NULL
	      && bfd_get_section_by_name (abfd, ".dynamic") != NULL
	      && bfd_get_section_by_name (abfd, ".mdebug") != NULL)
	    {
	      for (m = elf_tdata (abfd)->segment_map; m != NULL; m = m->next)
		if (m->p_type == PT_MIPS_RTPROC)
		  break;
	      if (m == NULL)
		{
		  amt = sizeof *m;
		  m = bfd_zalloc (abfd, amt);
		  if (m == NULL)
		    return FALSE;

		  m->p_type = PT_MIPS_RTPROC;

		  s = bfd_get_section_by_name (abfd, ".rtproc");
		  if (s == NULL)
		    {
		      m->count = 0;
		      m->p_flags = 0;
		      m->p_flags_valid = 1;
		    }
		  else
		    {
		      m->count = 1;
		      m->sections[0] = s;
		    }

		  /* We want to put it after the DYNAMIC segment.  */
		  pm = &elf_tdata (abfd)->segment_map;
		  while (*pm != NULL && (*pm)->p_type != PT_DYNAMIC)
		    pm = &(*pm)->next;
		  if (*pm != NULL)
		    pm = &(*pm)->next;

		  m->next = *pm;
		  *pm = m;
		}
	    }
	}
      /* On IRIX5, the PT_DYNAMIC segment includes the .dynamic,
	 .dynstr, .dynsym, and .hash sections, and everything in
	 between.  */
      for (pm = &elf_tdata (abfd)->segment_map; *pm != NULL;
	   pm = &(*pm)->next)
	if ((*pm)->p_type == PT_DYNAMIC)
	  break;
      m = *pm;
      if (m != NULL && IRIX_COMPAT (abfd) == ict_none)
	{
	  /* For a normal mips executable the permissions for the PT_DYNAMIC
	     segment are read, write and execute. We do that here since
	     the code in elf.c sets only the read permission. This matters
	     sometimes for the dynamic linker.  */
	  if (bfd_get_section_by_name (abfd, ".dynamic") != NULL)
	    {
	      m->p_flags = PF_R | PF_W | PF_X;
	      m->p_flags_valid = 1;
	    }
	}
      /* GNU/Linux binaries do not need the extended PT_DYNAMIC section.
	 glibc's dynamic linker has traditionally derived the number of
	 tags from the p_filesz field, and sometimes allocates stack
	 arrays of that size.  An overly-big PT_DYNAMIC segment can
	 be actively harmful in such cases.  Making PT_DYNAMIC contain
	 other sections can also make life hard for the prelinker,
	 which might move one of the other sections to a different
	 PT_LOAD segment.  */
      if (SGI_COMPAT (abfd)
	  && m != NULL
	  && m->count == 1
	  && strcmp (m->sections[0]->name, ".dynamic") == 0)
	{
	  static const char *sec_names[] =
	  {
	    ".dynamic", ".dynstr", ".dynsym", ".hash"
	  };
	  bfd_vma low, high;
	  unsigned int i, c;
	  struct elf_segment_map *n;

	  low = ~(bfd_vma) 0;
	  high = 0;
	  for (i = 0; i < sizeof sec_names / sizeof sec_names[0]; i++)
	    {
	      s = bfd_get_section_by_name (abfd, sec_names[i]);
	      if (s != NULL && (s->flags & SEC_LOAD) != 0)
		{
		  bfd_size_type sz;

		  if (low > s->vma)
		    low = s->vma;
		  sz = s->size;
		  if (high < s->vma + sz)
		    high = s->vma + sz;
		}
	    }

	  c = 0;
	  for (s = abfd->sections; s != NULL; s = s->next)
	    if ((s->flags & SEC_LOAD) != 0
		&& s->vma >= low
		&& s->vma + s->size <= high)
	      ++c;

	  amt = sizeof *n + (bfd_size_type) (c - 1) * sizeof (asection *);
	  n = bfd_zalloc (abfd, amt);
	  if (n == NULL)
	    return FALSE;
	  *n = *m;
	  n->count = c;

	  i = 0;
	  for (s = abfd->sections; s != NULL; s = s->next)
	    {
	      if ((s->flags & SEC_LOAD) != 0
		  && s->vma >= low
		  && s->vma + s->size <= high)
		{
		  n->sections[i] = s;
		  ++i;
		}
	    }

	  *pm = n;
	}
    }

  /* Allocate a spare program header in dynamic objects so that tools
     like the prelinker can add an extra PT_LOAD entry.

     If the prelinker needs to make room for a new PT_LOAD entry, its
     standard procedure is to move the first (read-only) sections into
     the new (writable) segment.  However, the MIPS ABI requires
     .dynamic to be in a read-only segment, and the section will often
     start within sizeof (ElfNN_Phdr) bytes of the last program header.

     Although the prelinker could in principle move .dynamic to a
     writable segment, it seems better to allocate a spare program
     header instead, and avoid the need to move any sections.
     There is a long tradition of allocating spare dynamic tags,
     so allocating a spare program header seems like a natural
     extension.  */
  if (!SGI_COMPAT (abfd)
      && bfd_get_section_by_name (abfd, ".dynamic"))
    {
      for (pm = &elf_tdata (abfd)->segment_map; *pm != NULL; pm = &(*pm)->next)
	if ((*pm)->p_type == PT_NULL)
	  break;
      if (*pm == NULL)
	{
	  m = bfd_zalloc (abfd, sizeof (*m));
	  if (m == NULL)
	    return FALSE;

	  m->p_type = PT_NULL;
	  *pm = m;
	}
    }

  return TRUE;
}

/* Return the section that should be marked against GC for a given
   relocation.  */

asection *
_bfd_mips_elf_gc_mark_hook (asection *sec,
			    struct bfd_link_info *info,
			    Elf_Internal_Rela *rel,
			    struct elf_link_hash_entry *h,
			    Elf_Internal_Sym *sym)
{
  /* ??? Do mips16 stub sections need to be handled special?  */

  if (h != NULL)
    switch (ELF_R_TYPE (sec->owner, rel->r_info))
      {
      case R_MIPS_GNU_VTINHERIT:
      case R_MIPS_GNU_VTENTRY:
	return NULL;
      }

  return _bfd_elf_gc_mark_hook (sec, info, rel, h, sym);
}

/* Update the got entry reference counts for the section being removed.  */

bfd_boolean
_bfd_mips_elf_gc_sweep_hook (bfd *abfd ATTRIBUTE_UNUSED,
			     struct bfd_link_info *info ATTRIBUTE_UNUSED,
			     asection *sec ATTRIBUTE_UNUSED,
			     const Elf_Internal_Rela *relocs ATTRIBUTE_UNUSED)
{
#if 0
  Elf_Internal_Shdr *symtab_hdr;
  struct elf_link_hash_entry **sym_hashes;
  bfd_signed_vma *local_got_refcounts;
  const Elf_Internal_Rela *rel, *relend;
  unsigned long r_symndx;
  struct elf_link_hash_entry *h;

  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;
  sym_hashes = elf_sym_hashes (abfd);
  local_got_refcounts = elf_local_got_refcounts (abfd);

  relend = relocs + sec->reloc_count;
  for (rel = relocs; rel < relend; rel++)
    switch (ELF_R_TYPE (abfd, rel->r_info))
      {
      case R_MIPS_GOT16:
      case R_MIPS_CALL16:
      case R_MIPS_CALL_HI16:
      case R_MIPS_CALL_LO16:
      case R_MIPS_GOT_HI16:
      case R_MIPS_GOT_LO16:
      case R_MIPS_GOT_DISP:
      case R_MIPS_GOT_PAGE:
      case R_MIPS_GOT_OFST:
	/* ??? It would seem that the existing MIPS code does no sort
	   of reference counting or whatnot on its GOT and PLT entries,
	   so it is not possible to garbage collect them at this time.  */
	break;

      default:
	break;
      }
#endif

  return TRUE;
}

/* Copy data from a MIPS ELF indirect symbol to its direct symbol,
   hiding the old indirect symbol.  Process additional relocation
   information.  Also called for weakdefs, in which case we just let
   _bfd_elf_link_hash_copy_indirect copy the flags for us.  */

void
_bfd_mips_elf_copy_indirect_symbol (struct bfd_link_info *info,
				    struct elf_link_hash_entry *dir,
				    struct elf_link_hash_entry *ind)
{
  struct mips_elf_link_hash_entry *dirmips, *indmips;

  _bfd_elf_link_hash_copy_indirect (info, dir, ind);

  if (ind->root.type != bfd_link_hash_indirect)
    return;

  dirmips = (struct mips_elf_link_hash_entry *) dir;
  indmips = (struct mips_elf_link_hash_entry *) ind;
  dirmips->possibly_dynamic_relocs += indmips->possibly_dynamic_relocs;
  if (indmips->readonly_reloc)
    dirmips->readonly_reloc = TRUE;
  if (indmips->no_fn_stub)
    dirmips->no_fn_stub = TRUE;

  if (dirmips->tls_type == 0)
    dirmips->tls_type = indmips->tls_type;
}

void
_bfd_mips_elf_hide_symbol (struct bfd_link_info *info,
			   struct elf_link_hash_entry *entry,
			   bfd_boolean force_local)
{
  bfd *dynobj;
  asection *got;
  struct mips_got_info *g;
  struct mips_elf_link_hash_entry *h;

  h = (struct mips_elf_link_hash_entry *) entry;
  if (h->forced_local)
    return;
  h->forced_local = force_local;

  dynobj = elf_hash_table (info)->dynobj;
  if (dynobj != NULL && force_local && h->root.type != STT_TLS
      && (got = mips_elf_got_section (dynobj, TRUE)) != NULL
      && (g = mips_elf_section_data (got)->u.got_info) != NULL)
    {
      if (g->next)
	{
	  struct mips_got_entry e;
	  struct mips_got_info *gg = g;

	  /* Since we're turning what used to be a global symbol into a
	     local one, bump up the number of local entries of each GOT
	     that had an entry for it.  This will automatically decrease
	     the number of global entries, since global_gotno is actually
	     the upper limit of global entries.  */
	  e.abfd = dynobj;
	  e.symndx = -1;
	  e.d.h = h;
	  e.tls_type = 0;

	  for (g = g->next; g != gg; g = g->next)
	    if (htab_find (g->got_entries, &e))
	      {
		BFD_ASSERT (g->global_gotno > 0);
		g->local_gotno++;
		g->global_gotno--;
	      }

	  /* If this was a global symbol forced into the primary GOT, we
	     no longer need an entry for it.  We can't release the entry
	     at this point, but we must at least stop counting it as one
	     of the symbols that required a forced got entry.  */
	  if (h->root.got.offset == 2)
	    {
	      BFD_ASSERT (gg->assigned_gotno > 0);
	      gg->assigned_gotno--;
	    }
	}
      else if (g->global_gotno == 0 && g->global_gotsym == NULL)
	/* If we haven't got through GOT allocation yet, just bump up the
	   number of local entries, as this symbol won't be counted as
	   global.  */
	g->local_gotno++;
      else if (h->root.got.offset == 1)
	{
	  /* If we're past non-multi-GOT allocation and this symbol had
	     been marked for a global got entry, give it a local entry
	     instead.  */
	  BFD_ASSERT (g->global_gotno > 0);
	  g->local_gotno++;
	  g->global_gotno--;
	}
    }

  _bfd_elf_link_hash_hide_symbol (info, &h->root, force_local);
}

#define PDR_SIZE 32

bfd_boolean
_bfd_mips_elf_discard_info (bfd *abfd, struct elf_reloc_cookie *cookie,
			    struct bfd_link_info *info)
{
  asection *o;
  bfd_boolean ret = FALSE;
  unsigned char *tdata;
  size_t i, skip;

  o = bfd_get_section_by_name (abfd, ".pdr");
  if (! o)
    return FALSE;
  if (o->size == 0)
    return FALSE;
  if (o->size % PDR_SIZE != 0)
    return FALSE;
  if (o->output_section != NULL
      && bfd_is_abs_section (o->output_section))
    return FALSE;

  tdata = bfd_zmalloc (o->size / PDR_SIZE);
  if (! tdata)
    return FALSE;

  cookie->rels = _bfd_elf_link_read_relocs (abfd, o, NULL, NULL,
					    info->keep_memory);
  if (!cookie->rels)
    {
      free (tdata);
      return FALSE;
    }

  cookie->rel = cookie->rels;
  cookie->relend = cookie->rels + o->reloc_count;

  for (i = 0, skip = 0; i < o->size / PDR_SIZE; i ++)
    {
      if (bfd_elf_reloc_symbol_deleted_p (i * PDR_SIZE, cookie))
	{
	  tdata[i] = 1;
	  skip ++;
	}
    }

  if (skip != 0)
    {
      mips_elf_section_data (o)->u.tdata = tdata;
      o->size -= skip * PDR_SIZE;
      ret = TRUE;
    }
  else
    free (tdata);

  if (! info->keep_memory)
    free (cookie->rels);

  return ret;
}

bfd_boolean
_bfd_mips_elf_ignore_discarded_relocs (asection *sec)
{
  if (strcmp (sec->name, ".pdr") == 0)
    return TRUE;
  return FALSE;
}

bfd_boolean
_bfd_mips_elf_write_section (bfd *output_bfd,
			     struct bfd_link_info *link_info ATTRIBUTE_UNUSED,
                             asection *sec, bfd_byte *contents)
{
  bfd_byte *to, *from, *end;
  int i;

  if (strcmp (sec->name, ".pdr") != 0)
    return FALSE;

  if (mips_elf_section_data (sec)->u.tdata == NULL)
    return FALSE;

  to = contents;
  end = contents + sec->size;
  for (from = contents, i = 0;
       from < end;
       from += PDR_SIZE, i++)
    {
      if ((mips_elf_section_data (sec)->u.tdata)[i] == 1)
	continue;
      if (to != from)
	memcpy (to, from, PDR_SIZE);
      to += PDR_SIZE;
    }
  bfd_set_section_contents (output_bfd, sec->output_section, contents,
			    sec->output_offset, sec->size);
  return TRUE;
}

/* MIPS ELF uses a special find_nearest_line routine in order the
   handle the ECOFF debugging information.  */

struct mips_elf_find_line
{
  struct ecoff_debug_info d;
  struct ecoff_find_line i;
};

bfd_boolean
_bfd_mips_elf_find_nearest_line (bfd *abfd, asection *section,
				 asymbol **symbols, bfd_vma offset,
				 const char **filename_ptr,
				 const char **functionname_ptr,
				 unsigned int *line_ptr)
{
  asection *msec;

  if (_bfd_dwarf1_find_nearest_line (abfd, section, symbols, offset,
				     filename_ptr, functionname_ptr,
				     line_ptr))
    return TRUE;

  if (_bfd_dwarf2_find_nearest_line (abfd, section, symbols, offset,
				     filename_ptr, functionname_ptr,
				     line_ptr, ABI_64_P (abfd) ? 8 : 0,
				     &elf_tdata (abfd)->dwarf2_find_line_info))
    return TRUE;

  msec = bfd_get_section_by_name (abfd, ".mdebug");
  if (msec != NULL)
    {
      flagword origflags;
      struct mips_elf_find_line *fi;
      const struct ecoff_debug_swap * const swap =
	get_elf_backend_data (abfd)->elf_backend_ecoff_debug_swap;

      /* If we are called during a link, mips_elf_final_link may have
	 cleared the SEC_HAS_CONTENTS field.  We force it back on here
	 if appropriate (which it normally will be).  */
      origflags = msec->flags;
      if (elf_section_data (msec)->this_hdr.sh_type != SHT_NOBITS)
	msec->flags |= SEC_HAS_CONTENTS;

      fi = elf_tdata (abfd)->find_line_info;
      if (fi == NULL)
	{
	  bfd_size_type external_fdr_size;
	  char *fraw_src;
	  char *fraw_end;
	  struct fdr *fdr_ptr;
	  bfd_size_type amt = sizeof (struct mips_elf_find_line);

	  fi = bfd_zalloc (abfd, amt);
	  if (fi == NULL)
	    {
	      msec->flags = origflags;
	      return FALSE;
	    }

	  if (! _bfd_mips_elf_read_ecoff_info (abfd, msec, &fi->d))
	    {
	      msec->flags = origflags;
	      return FALSE;
	    }

	  /* Swap in the FDR information.  */
	  amt = fi->d.symbolic_header.ifdMax * sizeof (struct fdr);
	  fi->d.fdr = bfd_alloc (abfd, amt);
	  if (fi->d.fdr == NULL)
	    {
	      msec->flags = origflags;
	      return FALSE;
	    }
	  external_fdr_size = swap->external_fdr_size;
	  fdr_ptr = fi->d.fdr;
	  fraw_src = (char *) fi->d.external_fdr;
	  fraw_end = (fraw_src
		      + fi->d.symbolic_header.ifdMax * external_fdr_size);
	  for (; fraw_src < fraw_end; fraw_src += external_fdr_size, fdr_ptr++)
	    (*swap->swap_fdr_in) (abfd, fraw_src, fdr_ptr);

	  elf_tdata (abfd)->find_line_info = fi;

	  /* Note that we don't bother to ever free this information.
             find_nearest_line is either called all the time, as in
             objdump -l, so the information should be saved, or it is
             rarely called, as in ld error messages, so the memory
             wasted is unimportant.  Still, it would probably be a
             good idea for free_cached_info to throw it away.  */
	}

      if (_bfd_ecoff_locate_line (abfd, section, offset, &fi->d, swap,
				  &fi->i, filename_ptr, functionname_ptr,
				  line_ptr))
	{
	  msec->flags = origflags;
	  return TRUE;
	}

      msec->flags = origflags;
    }

  /* Fall back on the generic ELF find_nearest_line routine.  */

  return _bfd_elf_find_nearest_line (abfd, section, symbols, offset,
				     filename_ptr, functionname_ptr,
				     line_ptr);
}

bfd_boolean
_bfd_mips_elf_find_inliner_info (bfd *abfd,
				 const char **filename_ptr,
				 const char **functionname_ptr,
				 unsigned int *line_ptr)
{
  bfd_boolean found;
  found = _bfd_dwarf2_find_inliner_info (abfd, filename_ptr,
					 functionname_ptr, line_ptr,
					 & elf_tdata (abfd)->dwarf2_find_line_info);
  return found;
}


/* When are writing out the .options or .MIPS.options section,
   remember the bytes we are writing out, so that we can install the
   GP value in the section_processing routine.  */

bfd_boolean
_bfd_mips_elf_set_section_contents (bfd *abfd, sec_ptr section,
				    const void *location,
				    file_ptr offset, bfd_size_type count)
{
  if (MIPS_ELF_OPTIONS_SECTION_NAME_P (section->name))
    {
      bfd_byte *c;

      if (elf_section_data (section) == NULL)
	{
	  bfd_size_type amt = sizeof (struct bfd_elf_section_data);
	  section->used_by_bfd = bfd_zalloc (abfd, amt);
	  if (elf_section_data (section) == NULL)
	    return FALSE;
	}
      c = mips_elf_section_data (section)->u.tdata;
      if (c == NULL)
	{
	  c = bfd_zalloc (abfd, section->size);
	  if (c == NULL)
	    return FALSE;
	  mips_elf_section_data (section)->u.tdata = c;
	}

      memcpy (c + offset, location, count);
    }

  return _bfd_elf_set_section_contents (abfd, section, location, offset,
					count);
}

/* This is almost identical to bfd_generic_get_... except that some
   MIPS relocations need to be handled specially.  Sigh.  */

bfd_byte *
_bfd_elf_mips_get_relocated_section_contents
  (bfd *abfd,
   struct bfd_link_info *link_info,
   struct bfd_link_order *link_order,
   bfd_byte *data,
   bfd_boolean relocatable,
   asymbol **symbols)
{
  /* Get enough memory to hold the stuff */
  bfd *input_bfd = link_order->u.indirect.section->owner;
  asection *input_section = link_order->u.indirect.section;
  bfd_size_type sz;

  long reloc_size = bfd_get_reloc_upper_bound (input_bfd, input_section);
  arelent **reloc_vector = NULL;
  long reloc_count;

  if (reloc_size < 0)
    goto error_return;

  reloc_vector = bfd_malloc (reloc_size);
  if (reloc_vector == NULL && reloc_size != 0)
    goto error_return;

  /* read in the section */
  sz = input_section->rawsize ? input_section->rawsize : input_section->size;
  if (!bfd_get_section_contents (input_bfd, input_section, data, 0, sz))
    goto error_return;

  reloc_count = bfd_canonicalize_reloc (input_bfd,
					input_section,
					reloc_vector,
					symbols);
  if (reloc_count < 0)
    goto error_return;

  if (reloc_count > 0)
    {
      arelent **parent;
      /* for mips */
      int gp_found;
      bfd_vma gp = 0x12345678;	/* initialize just to shut gcc up */

      {
	struct bfd_hash_entry *h;
	struct bfd_link_hash_entry *lh;
	/* Skip all this stuff if we aren't mixing formats.  */
	if (abfd && input_bfd
	    && abfd->xvec == input_bfd->xvec)
	  lh = 0;
	else
	  {
	    h = bfd_hash_lookup (&link_info->hash->table, "_gp", FALSE, FALSE);
	    lh = (struct bfd_link_hash_entry *) h;
	  }
      lookup:
	if (lh)
	  {
	    switch (lh->type)
	      {
	      case bfd_link_hash_undefined:
	      case bfd_link_hash_undefweak:
	      case bfd_link_hash_common:
		gp_found = 0;
		break;
	      case bfd_link_hash_defined:
	      case bfd_link_hash_defweak:
		gp_found = 1;
		gp = lh->u.def.value;
		break;
	      case bfd_link_hash_indirect:
	      case bfd_link_hash_warning:
		lh = lh->u.i.link;
		/* @@FIXME  ignoring warning for now */
		goto lookup;
	      case bfd_link_hash_new:
	      default:
		abort ();
	      }
	  }
	else
	  gp_found = 0;
      }
      /* end mips */
      for (parent = reloc_vector; *parent != NULL; parent++)
	{
	  char *error_message = NULL;
	  bfd_reloc_status_type r;

	  /* Specific to MIPS: Deal with relocation types that require
	     knowing the gp of the output bfd.  */
	  asymbol *sym = *(*parent)->sym_ptr_ptr;

	  /* If we've managed to find the gp and have a special
	     function for the relocation then go ahead, else default
	     to the generic handling.  */
	  if (gp_found
	      && (*parent)->howto->special_function
	      == _bfd_mips_elf32_gprel16_reloc)
	    r = _bfd_mips_elf_gprel16_with_gp (input_bfd, sym, *parent,
					       input_section, relocatable,
					       data, gp);
	  else
	    r = bfd_perform_relocation (input_bfd, *parent, data,
					input_section,
					relocatable ? abfd : NULL,
					&error_message);

	  if (relocatable)
	    {
	      asection *os = input_section->output_section;

	      /* A partial link, so keep the relocs */
	      os->orelocation[os->reloc_count] = *parent;
	      os->reloc_count++;
	    }

	  if (r != bfd_reloc_ok)
	    {
	      switch (r)
		{
		case bfd_reloc_undefined:
		  if (!((*link_info->callbacks->undefined_symbol)
			(link_info, bfd_asymbol_name (*(*parent)->sym_ptr_ptr),
			 input_bfd, input_section, (*parent)->address, TRUE)))
		    goto error_return;
		  break;
		case bfd_reloc_dangerous:
		  BFD_ASSERT (error_message != NULL);
		  if (!((*link_info->callbacks->reloc_dangerous)
			(link_info, error_message, input_bfd, input_section,
			 (*parent)->address)))
		    goto error_return;
		  break;
		case bfd_reloc_overflow:
		  if (!((*link_info->callbacks->reloc_overflow)
			(link_info, NULL,
			 bfd_asymbol_name (*(*parent)->sym_ptr_ptr),
			 (*parent)->howto->name, (*parent)->addend,
			 input_bfd, input_section, (*parent)->address)))
		    goto error_return;
		  break;
		case bfd_reloc_outofrange:
		default:
		  abort ();
		  break;
		}

	    }
	}
    }
  if (reloc_vector != NULL)
    free (reloc_vector);
  return data;

error_return:
  if (reloc_vector != NULL)
    free (reloc_vector);
  return NULL;
}

/* Create a MIPS ELF linker hash table.  */

struct bfd_link_hash_table *
_bfd_mips_elf_link_hash_table_create (bfd *abfd)
{
  struct mips_elf_link_hash_table *ret;
  bfd_size_type amt = sizeof (struct mips_elf_link_hash_table);

  ret = bfd_malloc (amt);
  if (ret == NULL)
    return NULL;

  if (!_bfd_elf_link_hash_table_init (&ret->root, abfd,
				      mips_elf_link_hash_newfunc,
				      sizeof (struct mips_elf_link_hash_entry)))
    {
      free (ret);
      return NULL;
    }

#if 0
  /* We no longer use this.  */
  for (i = 0; i < SIZEOF_MIPS_DYNSYM_SECNAMES; i++)
    ret->dynsym_sec_strindex[i] = (bfd_size_type) -1;
#endif
  ret->procedure_count = 0;
  ret->compact_rel_size = 0;
  ret->use_rld_obj_head = FALSE;
  ret->rld_value = 0;
  ret->mips16_stubs_seen = FALSE;
  ret->is_vxworks = FALSE;
  ret->srelbss = NULL;
  ret->sdynbss = NULL;
  ret->srelplt = NULL;
  ret->srelplt2 = NULL;
  ret->sgotplt = NULL;
  ret->splt = NULL;
  ret->plt_header_size = 0;
  ret->plt_entry_size = 0;
  ret->function_stub_size = 0;

  return &ret->root.root;
}

/* Likewise, but indicate that the target is VxWorks.  */

struct bfd_link_hash_table *
_bfd_mips_vxworks_link_hash_table_create (bfd *abfd)
{
  struct bfd_link_hash_table *ret;

  ret = _bfd_mips_elf_link_hash_table_create (abfd);
  if (ret)
    {
      struct mips_elf_link_hash_table *htab;

      htab = (struct mips_elf_link_hash_table *) ret;
      htab->is_vxworks = 1;
    }
  return ret;
}

/* We need to use a special link routine to handle the .reginfo and
   the .mdebug sections.  We need to merge all instances of these
   sections together, not write them all out sequentially.  */

bfd_boolean
_bfd_mips_elf_final_link (bfd *abfd, struct bfd_link_info *info)
{
  asection *o;
  struct bfd_link_order *p;
  asection *reginfo_sec, *mdebug_sec, *gptab_data_sec, *gptab_bss_sec;
  asection *rtproc_sec;
  Elf32_RegInfo reginfo;
  struct ecoff_debug_info debug;
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
  const struct ecoff_debug_swap *swap = bed->elf_backend_ecoff_debug_swap;
  HDRR *symhdr = &debug.symbolic_header;
  void *mdebug_handle = NULL;
  asection *s;
  EXTR esym;
  unsigned int i;
  bfd_size_type amt;
  struct mips_elf_link_hash_table *htab;

  static const char * const secname[] =
  {
    ".text", ".init", ".fini", ".data",
    ".rodata", ".sdata", ".sbss", ".bss"
  };
  static const int sc[] =
  {
    scText, scInit, scFini, scData,
    scRData, scSData, scSBss, scBss
  };

  /* We'd carefully arranged the dynamic symbol indices, and then the
     generic size_dynamic_sections renumbered them out from under us.
     Rather than trying somehow to prevent the renumbering, just do
     the sort again.  */
  htab = mips_elf_hash_table (info);
  if (elf_hash_table (info)->dynamic_sections_created)
    {
      bfd *dynobj;
      asection *got;
      struct mips_got_info *g;
      bfd_size_type dynsecsymcount;

      /* When we resort, we must tell mips_elf_sort_hash_table what
	 the lowest index it may use is.  That's the number of section
	 symbols we're going to add.  The generic ELF linker only
	 adds these symbols when building a shared object.  Note that
	 we count the sections after (possibly) removing the .options
	 section above.  */

      dynsecsymcount = count_section_dynsyms (abfd, info);
      if (! mips_elf_sort_hash_table (info, dynsecsymcount + 1))
	return FALSE;

      /* Make sure we didn't grow the global .got region.  */
      dynobj = elf_hash_table (info)->dynobj;
      got = mips_elf_got_section (dynobj, FALSE);
      g = mips_elf_section_data (got)->u.got_info;

      if (g->global_gotsym != NULL)
	BFD_ASSERT ((elf_hash_table (info)->dynsymcount
		     - g->global_gotsym->dynindx)
		    <= g->global_gotno);
    }

  /* Get a value for the GP register.  */
  if (elf_gp (abfd) == 0)
    {
      struct bfd_link_hash_entry *h;

      h = bfd_link_hash_lookup (info->hash, "_gp", FALSE, FALSE, TRUE);
      if (h != NULL && h->type == bfd_link_hash_defined)
	elf_gp (abfd) = (h->u.def.value
			 + h->u.def.section->output_section->vma
			 + h->u.def.section->output_offset);
      else if (htab->is_vxworks
	       && (h = bfd_link_hash_lookup (info->hash,
					     "_GLOBAL_OFFSET_TABLE_",
					     FALSE, FALSE, TRUE))
	       && h->type == bfd_link_hash_defined)
	elf_gp (abfd) = (h->u.def.section->output_section->vma
			 + h->u.def.section->output_offset
			 + h->u.def.value);
      else if (info->relocatable)
	{
	  bfd_vma lo = MINUS_ONE;

	  /* Find the GP-relative section with the lowest offset.  */
	  for (o = abfd->sections; o != NULL; o = o->next)
	    if (o->vma < lo
		&& (elf_section_data (o)->this_hdr.sh_flags & SHF_MIPS_GPREL))
	      lo = o->vma;

	  /* And calculate GP relative to that.  */
	  elf_gp (abfd) = lo + ELF_MIPS_GP_OFFSET (info);
	}
      else
	{
	  /* If the relocate_section function needs to do a reloc
	     involving the GP value, it should make a reloc_dangerous
	     callback to warn that GP is not defined.  */
	}
    }

  /* Go through the sections and collect the .reginfo and .mdebug
     information.  */
  reginfo_sec = NULL;
  mdebug_sec = NULL;
  gptab_data_sec = NULL;
  gptab_bss_sec = NULL;
  for (o = abfd->sections; o != NULL; o = o->next)
    {
      if (strcmp (o->name, ".reginfo") == 0)
	{
	  memset (&reginfo, 0, sizeof reginfo);

	  /* We have found the .reginfo section in the output file.
	     Look through all the link_orders comprising it and merge
	     the information together.  */
	  for (p = o->map_head.link_order; p != NULL; p = p->next)
	    {
	      asection *input_section;
	      bfd *input_bfd;
	      Elf32_External_RegInfo ext;
	      Elf32_RegInfo sub;

	      if (p->type != bfd_indirect_link_order)
		{
		  if (p->type == bfd_data_link_order)
		    continue;
		  abort ();
		}

	      input_section = p->u.indirect.section;
	      input_bfd = input_section->owner;

	      if (! bfd_get_section_contents (input_bfd, input_section,
					      &ext, 0, sizeof ext))
		return FALSE;

	      bfd_mips_elf32_swap_reginfo_in (input_bfd, &ext, &sub);

	      reginfo.ri_gprmask |= sub.ri_gprmask;
	      reginfo.ri_cprmask[0] |= sub.ri_cprmask[0];
	      reginfo.ri_cprmask[1] |= sub.ri_cprmask[1];
	      reginfo.ri_cprmask[2] |= sub.ri_cprmask[2];
	      reginfo.ri_cprmask[3] |= sub.ri_cprmask[3];

	      /* ri_gp_value is set by the function
		 mips_elf32_section_processing when the section is
		 finally written out.  */

	      /* Hack: reset the SEC_HAS_CONTENTS flag so that
		 elf_link_input_bfd ignores this section.  */
	      input_section->flags &= ~SEC_HAS_CONTENTS;
	    }

	  /* Size has been set in _bfd_mips_elf_always_size_sections.  */
	  BFD_ASSERT(o->size == sizeof (Elf32_External_RegInfo));

	  /* Skip this section later on (I don't think this currently
	     matters, but someday it might).  */
	  o->map_head.link_order = NULL;

	  reginfo_sec = o;
	}

      if (strcmp (o->name, ".mdebug") == 0)
	{
	  struct extsym_info einfo;
	  bfd_vma last;

	  /* We have found the .mdebug section in the output file.
	     Look through all the link_orders comprising it and merge
	     the information together.  */
	  symhdr->magic = swap->sym_magic;
	  /* FIXME: What should the version stamp be?  */
	  symhdr->vstamp = 0;
	  symhdr->ilineMax = 0;
	  symhdr->cbLine = 0;
	  symhdr->idnMax = 0;
	  symhdr->ipdMax = 0;
	  symhdr->isymMax = 0;
	  symhdr->ioptMax = 0;
	  symhdr->iauxMax = 0;
	  symhdr->issMax = 0;
	  symhdr->issExtMax = 0;
	  symhdr->ifdMax = 0;
	  symhdr->crfd = 0;
	  symhdr->iextMax = 0;

	  /* We accumulate the debugging information itself in the
	     debug_info structure.  */
	  debug.line = NULL;
	  debug.external_dnr = NULL;
	  debug.external_pdr = NULL;
	  debug.external_sym = NULL;
	  debug.external_opt = NULL;
	  debug.external_aux = NULL;
	  debug.ss = NULL;
	  debug.ssext = debug.ssext_end = NULL;
	  debug.external_fdr = NULL;
	  debug.external_rfd = NULL;
	  debug.external_ext = debug.external_ext_end = NULL;

	  mdebug_handle = bfd_ecoff_debug_init (abfd, &debug, swap, info);
	  if (mdebug_handle == NULL)
	    return FALSE;

	  esym.jmptbl = 0;
	  esym.cobol_main = 0;
	  esym.weakext = 0;
	  esym.reserved = 0;
	  esym.ifd = ifdNil;
	  esym.asym.iss = issNil;
	  esym.asym.st = stLocal;
	  esym.asym.reserved = 0;
	  esym.asym.index = indexNil;
	  last = 0;
	  for (i = 0; i < sizeof (secname) / sizeof (secname[0]); i++)
	    {
	      esym.asym.sc = sc[i];
	      s = bfd_get_section_by_name (abfd, secname[i]);
	      if (s != NULL)
		{
		  esym.asym.value = s->vma;
		  last = s->vma + s->size;
		}
	      else
		esym.asym.value = last;
	      if (!bfd_ecoff_debug_one_external (abfd, &debug, swap,
						 secname[i], &esym))
		return FALSE;
	    }

	  for (p = o->map_head.link_order; p != NULL; p = p->next)
	    {
	      asection *input_section;
	      bfd *input_bfd;
	      const struct ecoff_debug_swap *input_swap;
	      struct ecoff_debug_info input_debug;
	      char *eraw_src;
	      char *eraw_end;

	      if (p->type != bfd_indirect_link_order)
		{
		  if (p->type == bfd_data_link_order)
		    continue;
		  abort ();
		}

	      input_section = p->u.indirect.section;
	      input_bfd = input_section->owner;

	      if (bfd_get_flavour (input_bfd) != bfd_target_elf_flavour
		  || (get_elf_backend_data (input_bfd)
		      ->elf_backend_ecoff_debug_swap) == NULL)
		{
		  /* I don't know what a non MIPS ELF bfd would be
		     doing with a .mdebug section, but I don't really
		     want to deal with it.  */
		  continue;
		}

	      input_swap = (get_elf_backend_data (input_bfd)
			    ->elf_backend_ecoff_debug_swap);

	      BFD_ASSERT (p->size == input_section->size);

	      /* The ECOFF linking code expects that we have already
		 read in the debugging information and set up an
		 ecoff_debug_info structure, so we do that now.  */
	      if (! _bfd_mips_elf_read_ecoff_info (input_bfd, input_section,
						   &input_debug))
		return FALSE;

	      if (! (bfd_ecoff_debug_accumulate
		     (mdebug_handle, abfd, &debug, swap, input_bfd,
		      &input_debug, input_swap, info)))
		return FALSE;

	      /* Loop through the external symbols.  For each one with
		 interesting information, try to find the symbol in
		 the linker global hash table and save the information
		 for the output external symbols.  */
	      eraw_src = input_debug.external_ext;
	      eraw_end = (eraw_src
			  + (input_debug.symbolic_header.iextMax
			     * input_swap->external_ext_size));
	      for (;
		   eraw_src < eraw_end;
		   eraw_src += input_swap->external_ext_size)
		{
		  EXTR ext;
		  const char *name;
		  struct mips_elf_link_hash_entry *h;

		  (*input_swap->swap_ext_in) (input_bfd, eraw_src, &ext);
		  if (ext.asym.sc == scNil
		      || ext.asym.sc == scUndefined
		      || ext.asym.sc == scSUndefined)
		    continue;

		  name = input_debug.ssext + ext.asym.iss;
		  h = mips_elf_link_hash_lookup (mips_elf_hash_table (info),
						 name, FALSE, FALSE, TRUE);
		  if (h == NULL || h->esym.ifd != -2)
		    continue;

		  if (ext.ifd != -1)
		    {
		      BFD_ASSERT (ext.ifd
				  < input_debug.symbolic_header.ifdMax);
		      ext.ifd = input_debug.ifdmap[ext.ifd];
		    }

		  h->esym = ext;
		}

	      /* Free up the information we just read.  */
	      free (input_debug.line);
	      free (input_debug.external_dnr);
	      free (input_debug.external_pdr);
	      free (input_debug.external_sym);
	      free (input_debug.external_opt);
	      free (input_debug.external_aux);
	      free (input_debug.ss);
	      free (input_debug.ssext);
	      free (input_debug.external_fdr);
	      free (input_debug.external_rfd);
	      free (input_debug.external_ext);

	      /* Hack: reset the SEC_HAS_CONTENTS flag so that
		 elf_link_input_bfd ignores this section.  */
	      input_section->flags &= ~SEC_HAS_CONTENTS;
	    }

	  if (SGI_COMPAT (abfd) && info->shared)
	    {
	      /* Create .rtproc section.  */
	      rtproc_sec = bfd_get_section_by_name (abfd, ".rtproc");
	      if (rtproc_sec == NULL)
		{
		  flagword flags = (SEC_HAS_CONTENTS | SEC_IN_MEMORY
				    | SEC_LINKER_CREATED | SEC_READONLY);

		  rtproc_sec = bfd_make_section_with_flags (abfd,
							    ".rtproc",
							    flags);
		  if (rtproc_sec == NULL
		      || ! bfd_set_section_alignment (abfd, rtproc_sec, 4))
		    return FALSE;
		}

	      if (! mips_elf_create_procedure_table (mdebug_handle, abfd,
						     info, rtproc_sec,
						     &debug))
		return FALSE;
	    }

	  /* Build the external symbol information.  */
	  einfo.abfd = abfd;
	  einfo.info = info;
	  einfo.debug = &debug;
	  einfo.swap = swap;
	  einfo.failed = FALSE;
	  mips_elf_link_hash_traverse (mips_elf_hash_table (info),
				       mips_elf_output_extsym, &einfo);
	  if (einfo.failed)
	    return FALSE;

	  /* Set the size of the .mdebug section.  */
	  o->size = bfd_ecoff_debug_size (abfd, &debug, swap);

	  /* Skip this section later on (I don't think this currently
	     matters, but someday it might).  */
	  o->map_head.link_order = NULL;

	  mdebug_sec = o;
	}

      if (CONST_STRNEQ (o->name, ".gptab."))
	{
	  const char *subname;
	  unsigned int c;
	  Elf32_gptab *tab;
	  Elf32_External_gptab *ext_tab;
	  unsigned int j;

	  /* The .gptab.sdata and .gptab.sbss sections hold
	     information describing how the small data area would
	     change depending upon the -G switch.  These sections
	     not used in executables files.  */
	  if (! info->relocatable)
	    {
	      for (p = o->map_head.link_order; p != NULL; p = p->next)
		{
		  asection *input_section;

		  if (p->type != bfd_indirect_link_order)
		    {
		      if (p->type == bfd_data_link_order)
			continue;
		      abort ();
		    }

		  input_section = p->u.indirect.section;

		  /* Hack: reset the SEC_HAS_CONTENTS flag so that
		     elf_link_input_bfd ignores this section.  */
		  input_section->flags &= ~SEC_HAS_CONTENTS;
		}

	      /* Skip this section later on (I don't think this
		 currently matters, but someday it might).  */
	      o->map_head.link_order = NULL;

	      /* Really remove the section.  */
	      bfd_section_list_remove (abfd, o);
	      --abfd->section_count;

	      continue;
	    }

	  /* There is one gptab for initialized data, and one for
	     uninitialized data.  */
	  if (strcmp (o->name, ".gptab.sdata") == 0)
	    gptab_data_sec = o;
	  else if (strcmp (o->name, ".gptab.sbss") == 0)
	    gptab_bss_sec = o;
	  else
	    {
	      (*_bfd_error_handler)
		(_("%s: illegal section name `%s'"),
		 bfd_get_filename (abfd), o->name);
	      bfd_set_error (bfd_error_nonrepresentable_section);
	      return FALSE;
	    }

	  /* The linker script always combines .gptab.data and
	     .gptab.sdata into .gptab.sdata, and likewise for
	     .gptab.bss and .gptab.sbss.  It is possible that there is
	     no .sdata or .sbss section in the output file, in which
	     case we must change the name of the output section.  */
	  subname = o->name + sizeof ".gptab" - 1;
	  if (bfd_get_section_by_name (abfd, subname) == NULL)
	    {
	      if (o == gptab_data_sec)
		o->name = ".gptab.data";
	      else
		o->name = ".gptab.bss";
	      subname = o->name + sizeof ".gptab" - 1;
	      BFD_ASSERT (bfd_get_section_by_name (abfd, subname) != NULL);
	    }

	  /* Set up the first entry.  */
	  c = 1;
	  amt = c * sizeof (Elf32_gptab);
	  tab = bfd_malloc (amt);
	  if (tab == NULL)
	    return FALSE;
	  tab[0].gt_header.gt_current_g_value = elf_gp_size (abfd);
	  tab[0].gt_header.gt_unused = 0;

	  /* Combine the input sections.  */
	  for (p = o->map_head.link_order; p != NULL; p = p->next)
	    {
	      asection *input_section;
	      bfd *input_bfd;
	      bfd_size_type size;
	      unsigned long last;
	      bfd_size_type gpentry;

	      if (p->type != bfd_indirect_link_order)
		{
		  if (p->type == bfd_data_link_order)
		    continue;
		  abort ();
		}

	      input_section = p->u.indirect.section;
	      input_bfd = input_section->owner;

	      /* Combine the gptab entries for this input section one
		 by one.  We know that the input gptab entries are
		 sorted by ascending -G value.  */
	      size = input_section->size;
	      last = 0;
	      for (gpentry = sizeof (Elf32_External_gptab);
		   gpentry < size;
		   gpentry += sizeof (Elf32_External_gptab))
		{
		  Elf32_External_gptab ext_gptab;
		  Elf32_gptab int_gptab;
		  unsigned long val;
		  unsigned long add;
		  bfd_boolean exact;
		  unsigned int look;

		  if (! (bfd_get_section_contents
			 (input_bfd, input_section, &ext_gptab, gpentry,
			  sizeof (Elf32_External_gptab))))
		    {
		      free (tab);
		      return FALSE;
		    }

		  bfd_mips_elf32_swap_gptab_in (input_bfd, &ext_gptab,
						&int_gptab);
		  val = int_gptab.gt_entry.gt_g_value;
		  add = int_gptab.gt_entry.gt_bytes - last;

		  exact = FALSE;
		  for (look = 1; look < c; look++)
		    {
		      if (tab[look].gt_entry.gt_g_value >= val)
			tab[look].gt_entry.gt_bytes += add;

		      if (tab[look].gt_entry.gt_g_value == val)
			exact = TRUE;
		    }

		  if (! exact)
		    {
		      Elf32_gptab *new_tab;
		      unsigned int max;

		      /* We need a new table entry.  */
		      amt = (bfd_size_type) (c + 1) * sizeof (Elf32_gptab);
		      new_tab = bfd_realloc (tab, amt);
		      if (new_tab == NULL)
			{
			  free (tab);
			  return FALSE;
			}
		      tab = new_tab;
		      tab[c].gt_entry.gt_g_value = val;
		      tab[c].gt_entry.gt_bytes = add;

		      /* Merge in the size for the next smallest -G
			 value, since that will be implied by this new
			 value.  */
		      max = 0;
		      for (look = 1; look < c; look++)
			{
			  if (tab[look].gt_entry.gt_g_value < val
			      && (max == 0
				  || (tab[look].gt_entry.gt_g_value
				      > tab[max].gt_entry.gt_g_value)))
			    max = look;
			}
		      if (max != 0)
			tab[c].gt_entry.gt_bytes +=
			  tab[max].gt_entry.gt_bytes;

		      ++c;
		    }

		  last = int_gptab.gt_entry.gt_bytes;
		}

	      /* Hack: reset the SEC_HAS_CONTENTS flag so that
		 elf_link_input_bfd ignores this section.  */
	      input_section->flags &= ~SEC_HAS_CONTENTS;
	    }

	  /* The table must be sorted by -G value.  */
	  if (c > 2)
	    qsort (tab + 1, c - 1, sizeof (tab[0]), gptab_compare);

	  /* Swap out the table.  */
	  amt = (bfd_size_type) c * sizeof (Elf32_External_gptab);
	  ext_tab = bfd_alloc (abfd, amt);
	  if (ext_tab == NULL)
	    {
	      free (tab);
	      return FALSE;
	    }

	  for (j = 0; j < c; j++)
	    bfd_mips_elf32_swap_gptab_out (abfd, tab + j, ext_tab + j);
	  free (tab);

	  o->size = c * sizeof (Elf32_External_gptab);
	  o->contents = (bfd_byte *) ext_tab;

	  /* Skip this section later on (I don't think this currently
	     matters, but someday it might).  */
	  o->map_head.link_order = NULL;
	}
    }

  /* Invoke the regular ELF backend linker to do all the work.  */
  if (!bfd_elf_final_link (abfd, info))
    return FALSE;

  /* Now write out the computed sections.  */

  if (reginfo_sec != NULL)
    {
      Elf32_External_RegInfo ext;

      bfd_mips_elf32_swap_reginfo_out (abfd, &reginfo, &ext);
      if (! bfd_set_section_contents (abfd, reginfo_sec, &ext, 0, sizeof ext))
	return FALSE;
    }

  if (mdebug_sec != NULL)
    {
      BFD_ASSERT (abfd->output_has_begun);
      if (! bfd_ecoff_write_accumulated_debug (mdebug_handle, abfd, &debug,
					       swap, info,
					       mdebug_sec->filepos))
	return FALSE;

      bfd_ecoff_debug_free (mdebug_handle, abfd, &debug, swap, info);
    }

  if (gptab_data_sec != NULL)
    {
      if (! bfd_set_section_contents (abfd, gptab_data_sec,
				      gptab_data_sec->contents,
				      0, gptab_data_sec->size))
	return FALSE;
    }

  if (gptab_bss_sec != NULL)
    {
      if (! bfd_set_section_contents (abfd, gptab_bss_sec,
				      gptab_bss_sec->contents,
				      0, gptab_bss_sec->size))
	return FALSE;
    }

  if (SGI_COMPAT (abfd))
    {
      rtproc_sec = bfd_get_section_by_name (abfd, ".rtproc");
      if (rtproc_sec != NULL)
	{
	  if (! bfd_set_section_contents (abfd, rtproc_sec,
					  rtproc_sec->contents,
					  0, rtproc_sec->size))
	    return FALSE;
	}
    }

  return TRUE;
}

/* Structure for saying that BFD machine EXTENSION extends BASE.  */

struct mips_mach_extension {
  unsigned long extension, base;
};


/* An array describing how BFD machines relate to one another.  The entries
   are ordered topologically with MIPS I extensions listed last.  */

static const struct mips_mach_extension mips_mach_extensions[] = {
  /* MIPS64r2 extensions.  */
  { bfd_mach_mips_octeon, bfd_mach_mipsisa64r2 },

  /* MIPS64 extensions.  */
  { bfd_mach_mipsisa64r2, bfd_mach_mipsisa64 },
  { bfd_mach_mips_sb1, bfd_mach_mipsisa64 },

  /* MIPS V extensions.  */
  { bfd_mach_mipsisa64, bfd_mach_mips5 },

  /* R10000 extensions.  */
  { bfd_mach_mips12000, bfd_mach_mips10000 },

  /* R5000 extensions.  Note: the vr5500 ISA is an extension of the core
     vr5400 ISA, but doesn't include the multimedia stuff.  It seems
     better to allow vr5400 and vr5500 code to be merged anyway, since
     many libraries will just use the core ISA.  Perhaps we could add
     some sort of ASE flag if this ever proves a problem.  */
  { bfd_mach_mips5500, bfd_mach_mips5400 },
  { bfd_mach_mips5400, bfd_mach_mips5000 },

  /* MIPS IV extensions.  */
  { bfd_mach_mips5, bfd_mach_mips8000 },
  { bfd_mach_mips10000, bfd_mach_mips8000 },
  { bfd_mach_mips5000, bfd_mach_mips8000 },
  { bfd_mach_mips7000, bfd_mach_mips8000 },
  { bfd_mach_mips9000, bfd_mach_mips8000 },

  /* VR4100 extensions.  */
  { bfd_mach_mips4120, bfd_mach_mips4100 },
  { bfd_mach_mips4111, bfd_mach_mips4100 },

  /* MIPS III extensions.  */
  { bfd_mach_mips8000, bfd_mach_mips4000 },
  { bfd_mach_mips4650, bfd_mach_mips4000 },
  { bfd_mach_mips4600, bfd_mach_mips4000 },
  { bfd_mach_mips4400, bfd_mach_mips4000 },
  { bfd_mach_mips4300, bfd_mach_mips4000 },
  { bfd_mach_mips4100, bfd_mach_mips4000 },
  { bfd_mach_mips4010, bfd_mach_mips4000 },

  /* MIPS32 extensions.  */
  { bfd_mach_mipsisa32r2, bfd_mach_mipsisa32 },

  /* MIPS II extensions.  */
  { bfd_mach_mips4000, bfd_mach_mips6000 },
  { bfd_mach_mipsisa32, bfd_mach_mips6000 },

  /* MIPS I extensions.  */
  { bfd_mach_mips6000, bfd_mach_mips3000 },
  { bfd_mach_mips3900, bfd_mach_mips3000 }
};


/* Return true if bfd machine EXTENSION is an extension of machine BASE.  */

static bfd_boolean
mips_mach_extends_p (unsigned long base, unsigned long extension)
{
  size_t i;

  if (extension == base)
    return TRUE;

  if (base == bfd_mach_mipsisa32
      && mips_mach_extends_p (bfd_mach_mipsisa64, extension))
    return TRUE;

  if (base == bfd_mach_mipsisa32r2
      && mips_mach_extends_p (bfd_mach_mipsisa64r2, extension))
    return TRUE;

  for (i = 0; i < ARRAY_SIZE (mips_mach_extensions); i++)
    if (extension == mips_mach_extensions[i].extension)
      {
	extension = mips_mach_extensions[i].base;
	if (extension == base)
	  return TRUE;
      }

  return FALSE;
}


/* Return true if the given ELF header flags describe a 32-bit binary.  */

static bfd_boolean
mips_32bit_flags_p (flagword flags)
{
  return ((flags & EF_MIPS_32BITMODE) != 0
	  || (flags & EF_MIPS_ABI) == E_MIPS_ABI_O32
	  || (flags & EF_MIPS_ABI) == E_MIPS_ABI_EABI32
	  || (flags & EF_MIPS_ARCH) == E_MIPS_ARCH_1
	  || (flags & EF_MIPS_ARCH) == E_MIPS_ARCH_2
	  || (flags & EF_MIPS_ARCH) == E_MIPS_ARCH_32
	  || (flags & EF_MIPS_ARCH) == E_MIPS_ARCH_32R2);
}


/* Merge object attributes from IBFD into OBFD.  Raise an error if
   there are conflicting attributes.  */
static bfd_boolean
mips_elf_merge_obj_attributes (bfd *ibfd, bfd *obfd)
{
  obj_attribute *in_attr;
  obj_attribute *out_attr;

  if (!elf_known_obj_attributes_proc (obfd)[0].i)
    {
      /* This is the first object.  Copy the attributes.  */
      _bfd_elf_copy_obj_attributes (ibfd, obfd);

      /* Use the Tag_null value to indicate the attributes have been
	 initialized.  */
      elf_known_obj_attributes_proc (obfd)[0].i = 1;

      return TRUE;
    }

  /* Check for conflicting Tag_GNU_MIPS_ABI_FP attributes and merge
     non-conflicting ones.  */
  in_attr = elf_known_obj_attributes (ibfd)[OBJ_ATTR_GNU];
  out_attr = elf_known_obj_attributes (obfd)[OBJ_ATTR_GNU];
  if (in_attr[Tag_GNU_MIPS_ABI_FP].i != out_attr[Tag_GNU_MIPS_ABI_FP].i)
    {
      out_attr[Tag_GNU_MIPS_ABI_FP].type = 1;
      if (out_attr[Tag_GNU_MIPS_ABI_FP].i == 0)
	out_attr[Tag_GNU_MIPS_ABI_FP].i = in_attr[Tag_GNU_MIPS_ABI_FP].i;
      else if (in_attr[Tag_GNU_MIPS_ABI_FP].i == 0)
	;
      else if (in_attr[Tag_GNU_MIPS_ABI_FP].i > 3)
	_bfd_error_handler
	  (_("Warning: %B uses unknown floating point ABI %d"), ibfd,
	   in_attr[Tag_GNU_MIPS_ABI_FP].i);
      else if (out_attr[Tag_GNU_MIPS_ABI_FP].i > 3)
	_bfd_error_handler
	  (_("Warning: %B uses unknown floating point ABI %d"), obfd,
	   out_attr[Tag_GNU_MIPS_ABI_FP].i);
      else
	switch (out_attr[Tag_GNU_MIPS_ABI_FP].i)
	  {
	  case 1:
	    switch (in_attr[Tag_GNU_MIPS_ABI_FP].i)
	      {
	      case 2:
		_bfd_error_handler
		  (_("Warning: %B uses -msingle-float, %B uses -mdouble-float"),
		   obfd, ibfd);

	      case 3:
		_bfd_error_handler
		  (_("Warning: %B uses hard float, %B uses soft float"),
		   obfd, ibfd);
		break;

	      default:
		abort ();
	      }
	    break;

	  case 2:
	    switch (in_attr[Tag_GNU_MIPS_ABI_FP].i)
	      {
	      case 1:
		_bfd_error_handler
		  (_("Warning: %B uses -msingle-float, %B uses -mdouble-float"),
		   ibfd, obfd);

	      case 3:
		_bfd_error_handler
		  (_("Warning: %B uses hard float, %B uses soft float"),
		   obfd, ibfd);
		break;

	      default:
		abort ();
	      }
	    break;

	  case 3:
	    switch (in_attr[Tag_GNU_MIPS_ABI_FP].i)
	      {
	      case 1:
	      case 2:
		_bfd_error_handler
		  (_("Warning: %B uses hard float, %B uses soft float"),
		   ibfd, obfd);
		break;

	      default:
		abort ();
	      }
	    break;

	  default:
	    abort ();
	  }
    }

  /* Merge Tag_compatibility attributes and any common GNU ones.  */
  _bfd_elf_merge_object_attributes (ibfd, obfd);

  return TRUE;
}

/* Merge backend specific data from an object file to the output
   object file when linking.  */

bfd_boolean
_bfd_mips_elf_merge_private_bfd_data (bfd *ibfd, bfd *obfd)
{
  flagword old_flags;
  flagword new_flags;
  bfd_boolean ok;
  bfd_boolean null_input_bfd = TRUE;
  asection *sec;

  /* Check if we have the same endianess */
  if (! _bfd_generic_verify_endian_match (ibfd, obfd))
    {
      (*_bfd_error_handler)
	(_("%B: endianness incompatible with that of the selected emulation"),
	 ibfd);
      return FALSE;
    }

  if (bfd_get_flavour (ibfd) != bfd_target_elf_flavour
      || bfd_get_flavour (obfd) != bfd_target_elf_flavour)
    return TRUE;

  if (strcmp (bfd_get_target (ibfd), bfd_get_target (obfd)) != 0)
    {
      (*_bfd_error_handler)
	(_("%B: ABI is incompatible with that of the selected emulation"),
	 ibfd);
      return FALSE;
    }

  if (!mips_elf_merge_obj_attributes (ibfd, obfd))
    return FALSE;

  new_flags = elf_elfheader (ibfd)->e_flags;
  elf_elfheader (obfd)->e_flags |= new_flags & EF_MIPS_NOREORDER;
  old_flags = elf_elfheader (obfd)->e_flags;

  if (! elf_flags_init (obfd))
    {
      elf_flags_init (obfd) = TRUE;
      elf_elfheader (obfd)->e_flags = new_flags;
      elf_elfheader (obfd)->e_ident[EI_CLASS]
	= elf_elfheader (ibfd)->e_ident[EI_CLASS];

      if (bfd_get_arch (obfd) == bfd_get_arch (ibfd)
	  && (bfd_get_arch_info (obfd)->the_default
	      || mips_mach_extends_p (bfd_get_mach (obfd), 
				      bfd_get_mach (ibfd))))
	{
	  if (! bfd_set_arch_mach (obfd, bfd_get_arch (ibfd),
				   bfd_get_mach (ibfd)))
	    return FALSE;
	}

      return TRUE;
    }

  /* Check flag compatibility.  */

  new_flags &= ~EF_MIPS_NOREORDER;
  old_flags &= ~EF_MIPS_NOREORDER;

  /* Some IRIX 6 BSD-compatibility objects have this bit set.  It
     doesn't seem to matter.  */
  new_flags &= ~EF_MIPS_XGOT;
  old_flags &= ~EF_MIPS_XGOT;

  /* MIPSpro generates ucode info in n64 objects.  Again, we should
     just be able to ignore this.  */
  new_flags &= ~EF_MIPS_UCODE;
  old_flags &= ~EF_MIPS_UCODE;

  /* Don't care about the PIC flags from dynamic objects; they are
     PIC by design.  */
  if ((new_flags & (EF_MIPS_PIC | EF_MIPS_CPIC)) != 0
      && (ibfd->flags & DYNAMIC) != 0)
    new_flags &= ~ (EF_MIPS_PIC | EF_MIPS_CPIC);

  if (new_flags == old_flags)
    return TRUE;

  /* Check to see if the input BFD actually contains any sections.
     If not, its flags may not have been initialised either, but it cannot
     actually cause any incompatibility.  */
  for (sec = ibfd->sections; sec != NULL; sec = sec->next)
    {
      /* Ignore synthetic sections and empty .text, .data and .bss sections
	  which are automatically generated by gas.  */
      if (strcmp (sec->name, ".reginfo")
	  && strcmp (sec->name, ".mdebug")
	  && (sec->size != 0
	      || (strcmp (sec->name, ".text")
		  && strcmp (sec->name, ".data")
		  && strcmp (sec->name, ".bss"))))
	{
	  null_input_bfd = FALSE;
	  break;
	}
    }
  if (null_input_bfd)
    return TRUE;

  ok = TRUE;

  if (((new_flags & (EF_MIPS_PIC | EF_MIPS_CPIC)) != 0)
      != ((old_flags & (EF_MIPS_PIC | EF_MIPS_CPIC)) != 0))
    {
      (*_bfd_error_handler)
	(_("%B: warning: linking PIC files with non-PIC files"),
	 ibfd);
      ok = TRUE;
    }

  if (new_flags & (EF_MIPS_PIC | EF_MIPS_CPIC))
    elf_elfheader (obfd)->e_flags |= EF_MIPS_CPIC;
  if (! (new_flags & EF_MIPS_PIC))
    elf_elfheader (obfd)->e_flags &= ~EF_MIPS_PIC;

  new_flags &= ~ (EF_MIPS_PIC | EF_MIPS_CPIC);
  old_flags &= ~ (EF_MIPS_PIC | EF_MIPS_CPIC);

  /* Compare the ISAs.  */
  if (mips_32bit_flags_p (old_flags) != mips_32bit_flags_p (new_flags))
    {
      (*_bfd_error_handler)
	(_("%B: linking 32-bit code with 64-bit code"),
	 ibfd);
      ok = FALSE;
    }
  else if (!mips_mach_extends_p (bfd_get_mach (ibfd), bfd_get_mach (obfd)))
    {
      /* OBFD's ISA isn't the same as, or an extension of, IBFD's.  */
      if (mips_mach_extends_p (bfd_get_mach (obfd), bfd_get_mach (ibfd)))
	{
	  /* Copy the architecture info from IBFD to OBFD.  Also copy
	     the 32-bit flag (if set) so that we continue to recognise
	     OBFD as a 32-bit binary.  */
	  bfd_set_arch_info (obfd, bfd_get_arch_info (ibfd));
	  elf_elfheader (obfd)->e_flags &= ~(EF_MIPS_ARCH | EF_MIPS_MACH);
	  elf_elfheader (obfd)->e_flags
	    |= new_flags & (EF_MIPS_ARCH | EF_MIPS_MACH | EF_MIPS_32BITMODE);

	  /* Copy across the ABI flags if OBFD doesn't use them
	     and if that was what caused us to treat IBFD as 32-bit.  */
	  if ((old_flags & EF_MIPS_ABI) == 0
	      && mips_32bit_flags_p (new_flags)
	      && !mips_32bit_flags_p (new_flags & ~EF_MIPS_ABI))
	    elf_elfheader (obfd)->e_flags |= new_flags & EF_MIPS_ABI;
	}
      else
	{
	  /* The ISAs aren't compatible.  */
	  (*_bfd_error_handler)
	    (_("%B: linking %s module with previous %s modules"),
	     ibfd,
	     bfd_printable_name (ibfd),
	     bfd_printable_name (obfd));
	  ok = FALSE;
	}
    }

  new_flags &= ~(EF_MIPS_ARCH | EF_MIPS_MACH | EF_MIPS_32BITMODE);
  old_flags &= ~(EF_MIPS_ARCH | EF_MIPS_MACH | EF_MIPS_32BITMODE);

  /* Compare ABIs.  The 64-bit ABI does not use EF_MIPS_ABI.  But, it
     does set EI_CLASS differently from any 32-bit ABI.  */
  if ((new_flags & EF_MIPS_ABI) != (old_flags & EF_MIPS_ABI)
      || (elf_elfheader (ibfd)->e_ident[EI_CLASS]
	  != elf_elfheader (obfd)->e_ident[EI_CLASS]))
    {
      /* Only error if both are set (to different values).  */
      if (((new_flags & EF_MIPS_ABI) && (old_flags & EF_MIPS_ABI))
	  || (elf_elfheader (ibfd)->e_ident[EI_CLASS]
	      != elf_elfheader (obfd)->e_ident[EI_CLASS]))
	{
	  (*_bfd_error_handler)
	    (_("%B: ABI mismatch: linking %s module with previous %s modules"),
	     ibfd,
	     elf_mips_abi_name (ibfd),
	     elf_mips_abi_name (obfd));
	  ok = FALSE;
	}
      new_flags &= ~EF_MIPS_ABI;
      old_flags &= ~EF_MIPS_ABI;
    }

  /* For now, allow arbitrary mixing of ASEs (retain the union).  */
  if ((new_flags & EF_MIPS_ARCH_ASE) != (old_flags & EF_MIPS_ARCH_ASE))
    {
      elf_elfheader (obfd)->e_flags |= new_flags & EF_MIPS_ARCH_ASE;

      new_flags &= ~ EF_MIPS_ARCH_ASE;
      old_flags &= ~ EF_MIPS_ARCH_ASE;
    }

  /* Warn about any other mismatches */
  if (new_flags != old_flags)
    {
      (*_bfd_error_handler)
	(_("%B: uses different e_flags (0x%lx) fields than previous modules (0x%lx)"),
	 ibfd, (unsigned long) new_flags,
	 (unsigned long) old_flags);
      ok = FALSE;
    }

  if (! ok)
    {
      bfd_set_error (bfd_error_bad_value);
      return FALSE;
    }

  return TRUE;
}

/* Function to keep MIPS specific file flags like as EF_MIPS_PIC.  */

bfd_boolean
_bfd_mips_elf_set_private_flags (bfd *abfd, flagword flags)
{
  BFD_ASSERT (!elf_flags_init (abfd)
	      || elf_elfheader (abfd)->e_flags == flags);

  elf_elfheader (abfd)->e_flags = flags;
  elf_flags_init (abfd) = TRUE;
  return TRUE;
}

bfd_boolean
_bfd_mips_elf_print_private_bfd_data (bfd *abfd, void *ptr)
{
  FILE *file = ptr;

  BFD_ASSERT (abfd != NULL && ptr != NULL);

  /* Print normal ELF private data.  */
  _bfd_elf_print_private_bfd_data (abfd, ptr);

  /* xgettext:c-format */
  fprintf (file, _("private flags = %lx:"), elf_elfheader (abfd)->e_flags);

  if ((elf_elfheader (abfd)->e_flags & EF_MIPS_ABI) == E_MIPS_ABI_O32)
    fprintf (file, _(" [abi=O32]"));
  else if ((elf_elfheader (abfd)->e_flags & EF_MIPS_ABI) == E_MIPS_ABI_O64)
    fprintf (file, _(" [abi=O64]"));
  else if ((elf_elfheader (abfd)->e_flags & EF_MIPS_ABI) == E_MIPS_ABI_EABI32)
    fprintf (file, _(" [abi=EABI32]"));
  else if ((elf_elfheader (abfd)->e_flags & EF_MIPS_ABI) == E_MIPS_ABI_EABI64)
    fprintf (file, _(" [abi=EABI64]"));
  else if ((elf_elfheader (abfd)->e_flags & EF_MIPS_ABI))
    fprintf (file, _(" [abi unknown]"));
  else if (ABI_N32_P (abfd))
    fprintf (file, _(" [abi=N32]"));
  else if (ABI_64_P (abfd))
    fprintf (file, _(" [abi=64]"));
  else
    fprintf (file, _(" [no abi set]"));

  if ((elf_elfheader (abfd)->e_flags & EF_MIPS_ARCH) == E_MIPS_ARCH_1)
    fprintf (file, " [mips1]");
  else if ((elf_elfheader (abfd)->e_flags & EF_MIPS_ARCH) == E_MIPS_ARCH_2)
    fprintf (file, " [mips2]");
  else if ((elf_elfheader (abfd)->e_flags & EF_MIPS_ARCH) == E_MIPS_ARCH_3)
    fprintf (file, " [mips3]");
  else if ((elf_elfheader (abfd)->e_flags & EF_MIPS_ARCH) == E_MIPS_ARCH_4)
    fprintf (file, " [mips4]");
  else if ((elf_elfheader (abfd)->e_flags & EF_MIPS_ARCH) == E_MIPS_ARCH_5)
    fprintf (file, " [mips5]");
  else if ((elf_elfheader (abfd)->e_flags & EF_MIPS_ARCH) == E_MIPS_ARCH_32)
    fprintf (file, " [mips32]");
  else if ((elf_elfheader (abfd)->e_flags & EF_MIPS_ARCH) == E_MIPS_ARCH_64)
    fprintf (file, " [mips64]");
  else if ((elf_elfheader (abfd)->e_flags & EF_MIPS_ARCH) == E_MIPS_ARCH_32R2)
    fprintf (file, " [mips32r2]");
  else if ((elf_elfheader (abfd)->e_flags & EF_MIPS_ARCH) == E_MIPS_ARCH_64R2)
    fprintf (file, " [mips64r2]");
  else
    fprintf (file, _(" [unknown ISA]"));

  if (elf_elfheader (abfd)->e_flags & EF_MIPS_ARCH_ASE_MDMX)
    fprintf (file, " [mdmx]");

  if (elf_elfheader (abfd)->e_flags & EF_MIPS_ARCH_ASE_M16)
    fprintf (file, " [mips16]");

  if (elf_elfheader (abfd)->e_flags & EF_MIPS_32BITMODE)
    fprintf (file, " [32bitmode]");
  else
    fprintf (file, _(" [not 32bitmode]"));

  if (elf_elfheader (abfd)->e_flags & EF_MIPS_NOREORDER)
    fprintf (file, " [noreorder]");

  if (elf_elfheader (abfd)->e_flags & EF_MIPS_PIC)
    fprintf (file, " [PIC]");

  if (elf_elfheader (abfd)->e_flags & EF_MIPS_CPIC)
    fprintf (file, " [CPIC]");

  if (elf_elfheader (abfd)->e_flags & EF_MIPS_XGOT)
    fprintf (file, " [XGOT]");

  if (elf_elfheader (abfd)->e_flags & EF_MIPS_UCODE)
    fprintf (file, " [UCODE]");

  fputc ('\n', file);

  return TRUE;
}

const struct bfd_elf_special_section _bfd_mips_elf_special_sections[] =
{
  { STRING_COMMA_LEN (".lit4"),   0, SHT_PROGBITS,   SHF_ALLOC + SHF_WRITE + SHF_MIPS_GPREL },
  { STRING_COMMA_LEN (".lit8"),   0, SHT_PROGBITS,   SHF_ALLOC + SHF_WRITE + SHF_MIPS_GPREL },
  { STRING_COMMA_LEN (".mdebug"), 0, SHT_MIPS_DEBUG, 0 },
  { STRING_COMMA_LEN (".sbss"),  -2, SHT_NOBITS,     SHF_ALLOC + SHF_WRITE + SHF_MIPS_GPREL },
  { STRING_COMMA_LEN (".sdata"), -2, SHT_PROGBITS,   SHF_ALLOC + SHF_WRITE + SHF_MIPS_GPREL },
  { STRING_COMMA_LEN (".ucode"),  0, SHT_MIPS_UCODE, 0 },
  { NULL,                     0,  0, 0,              0 }
};

/* Merge non visibility st_other attributes.  Ensure that the
   STO_OPTIONAL flag is copied into h->other, even if this is not a
   definiton of the symbol.  */
void
_bfd_mips_elf_merge_symbol_attribute (struct elf_link_hash_entry *h,
				      const Elf_Internal_Sym *isym,
				      bfd_boolean definition,
				      bfd_boolean dynamic ATTRIBUTE_UNUSED)
{
  if ((isym->st_other & ~ELF_ST_VISIBILITY (-1)) != 0)
    {
      unsigned char other;

      other = (definition ? isym->st_other : h->other);
      other &= ~ELF_ST_VISIBILITY (-1);
      h->other = other | ELF_ST_VISIBILITY (h->other);
    }

  if (!definition
      && ELF_MIPS_IS_OPTIONAL (isym->st_other))
    h->other |= STO_OPTIONAL;
}

/* Decide whether an undefined symbol is special and can be ignored.
   This is the case for OPTIONAL symbols on IRIX.  */
bfd_boolean
_bfd_mips_elf_ignore_undef_symbol (struct elf_link_hash_entry *h)
{
  return ELF_MIPS_IS_OPTIONAL (h->other) ? TRUE : FALSE;
}

bfd_boolean
_bfd_mips_elf_common_definition (Elf_Internal_Sym *sym)
{
  return (sym->st_shndx == SHN_COMMON
	  || sym->st_shndx == SHN_MIPS_ACOMMON
	  || sym->st_shndx == SHN_MIPS_SCOMMON);
}
