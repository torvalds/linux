/* BFD backend for SunOS binaries.
   Copyright 1990, 1991, 1992, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.
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

#define TARGETNAME "a.out-sunos-big"

/* Do not "beautify" the CONCAT* macro args.  Traditional C will not
   remove whitespace added here, and thus will fail to concatenate
   the tokens.  */
#define MY(OP) CONCAT2 (sunos_big_,OP)

#include "bfd.h"
#include "bfdlink.h"
#include "libaout.h"

/* ??? Where should this go?  */
#define MACHTYPE_OK(mtype) \
  (((mtype) == M_SPARC && bfd_lookup_arch (bfd_arch_sparc, 0) != NULL) \
   || ((mtype) == M_SPARCLET \
       && bfd_lookup_arch (bfd_arch_sparc, bfd_mach_sparc_sparclet) != NULL) \
   || ((mtype) == M_SPARCLITE_LE \
       && bfd_lookup_arch (bfd_arch_sparc, bfd_mach_sparc_sparclet) != NULL) \
   || (((mtype) == M_UNKNOWN || (mtype) == M_68010 || (mtype) == M_68020) \
       && bfd_lookup_arch (bfd_arch_m68k, 0) != NULL))

#define MY_get_dynamic_symtab_upper_bound  sunos_get_dynamic_symtab_upper_bound
#define MY_canonicalize_dynamic_symtab     sunos_canonicalize_dynamic_symtab
#define MY_get_synthetic_symtab            _bfd_nodynamic_get_synthetic_symtab
#define MY_get_dynamic_reloc_upper_bound   sunos_get_dynamic_reloc_upper_bound
#define MY_canonicalize_dynamic_reloc      sunos_canonicalize_dynamic_reloc
#define MY_bfd_link_hash_table_create      sunos_link_hash_table_create
#define MY_add_dynamic_symbols             sunos_add_dynamic_symbols
#define MY_add_one_symbol                  sunos_add_one_symbol
#define MY_link_dynamic_object             sunos_link_dynamic_object
#define MY_write_dynamic_symbol            sunos_write_dynamic_symbol
#define MY_check_dynamic_reloc             sunos_check_dynamic_reloc
#define MY_finish_dynamic_link             sunos_finish_dynamic_link

static bfd_boolean sunos_add_dynamic_symbols            (bfd *, struct bfd_link_info *, struct external_nlist **, bfd_size_type *, char **);
static bfd_boolean sunos_add_one_symbol                 (struct bfd_link_info *, bfd *, const char *, flagword, asection *, bfd_vma, const char *, bfd_boolean, bfd_boolean, struct bfd_link_hash_entry **);
static bfd_boolean sunos_link_dynamic_object            (struct bfd_link_info *, bfd *);
static bfd_boolean sunos_write_dynamic_symbol           (bfd *, struct bfd_link_info *, struct aout_link_hash_entry *);
static bfd_boolean sunos_check_dynamic_reloc            (struct bfd_link_info *, bfd *, asection *, struct aout_link_hash_entry *, void *, bfd_byte *, bfd_boolean *, bfd_vma *);
static bfd_boolean sunos_finish_dynamic_link            (bfd *, struct bfd_link_info *);
static struct bfd_link_hash_table *sunos_link_hash_table_create  (bfd *);
static long        sunos_get_dynamic_symtab_upper_bound (bfd *);
static long        sunos_canonicalize_dynamic_symtab    (bfd *, asymbol **);
static long        sunos_get_dynamic_reloc_upper_bound  (bfd *);
static long        sunos_canonicalize_dynamic_reloc     (bfd *, arelent **, asymbol **);

/* Include the usual a.out support.  */
#include "aoutf1.h"

/* The SunOS 4.1.4 /usr/include/locale.h defines valid as a macro.  */
#undef valid

/* SunOS shared library support.  We store a pointer to this structure
   in obj_aout_dynamic_info (abfd).  */

struct sunos_dynamic_info
{
  /* Whether we found any dynamic information.  */
  bfd_boolean valid;
  /* Dynamic information.  */
  struct internal_sun4_dynamic_link dyninfo;
  /* Number of dynamic symbols.  */
  unsigned long dynsym_count;
  /* Read in nlists for dynamic symbols.  */
  struct external_nlist *dynsym;
  /* asymbol structures for dynamic symbols.  */
  aout_symbol_type *canonical_dynsym;
  /* Read in dynamic string table.  */
  char *dynstr;
  /* Number of dynamic relocs.  */
  unsigned long dynrel_count;
  /* Read in dynamic relocs.  This may be reloc_std_external or
     reloc_ext_external.  */
  void * dynrel;
  /* arelent structures for dynamic relocs.  */
  arelent *canonical_dynrel;
};

/* The hash table of dynamic symbols is composed of two word entries.
   See include/aout/sun4.h for details.  */

#define HASH_ENTRY_SIZE (2 * BYTES_IN_WORD)

/* Read in the basic dynamic information.  This locates the __DYNAMIC
   structure and uses it to find the dynamic_link structure.  It
   creates and saves a sunos_dynamic_info structure.  If it can't find
   __DYNAMIC, it sets the valid field of the sunos_dynamic_info
   structure to FALSE to avoid doing this work again.  */

static bfd_boolean
sunos_read_dynamic_info (bfd *abfd)
{
  struct sunos_dynamic_info *info;
  asection *dynsec;
  bfd_vma dynoff;
  struct external_sun4_dynamic dyninfo;
  unsigned long dynver;
  struct external_sun4_dynamic_link linkinfo;
  bfd_size_type amt;

  if (obj_aout_dynamic_info (abfd) != NULL)
    return TRUE;

  if ((abfd->flags & DYNAMIC) == 0)
    {
      bfd_set_error (bfd_error_invalid_operation);
      return FALSE;
    }

  amt = sizeof (struct sunos_dynamic_info);
  info = bfd_zalloc (abfd, amt);
  if (!info)
    return FALSE;
  info->valid = FALSE;
  info->dynsym = NULL;
  info->dynstr = NULL;
  info->canonical_dynsym = NULL;
  info->dynrel = NULL;
  info->canonical_dynrel = NULL;
  obj_aout_dynamic_info (abfd) = (void *) info;

  /* This code used to look for the __DYNAMIC symbol to locate the dynamic
     linking information.
     However this inhibits recovering the dynamic symbols from a
     stripped object file, so blindly assume that the dynamic linking
     information is located at the start of the data section.
     We could verify this assumption later by looking through the dynamic
     symbols for the __DYNAMIC symbol.  */
  if ((abfd->flags & DYNAMIC) == 0)
    return TRUE;
  if (! bfd_get_section_contents (abfd, obj_datasec (abfd), (void *) &dyninfo,
				  (file_ptr) 0,
				  (bfd_size_type) sizeof dyninfo))
    return TRUE;

  dynver = GET_WORD (abfd, dyninfo.ld_version);
  if (dynver != 2 && dynver != 3)
    return TRUE;

  dynoff = GET_WORD (abfd, dyninfo.ld);

  /* dynoff is a virtual address.  It is probably always in the .data
     section, but this code should work even if it moves.  */
  if (dynoff < bfd_get_section_vma (abfd, obj_datasec (abfd)))
    dynsec = obj_textsec (abfd);
  else
    dynsec = obj_datasec (abfd);
  dynoff -= bfd_get_section_vma (abfd, dynsec);
  if (dynoff > dynsec->size)
    return TRUE;

  /* This executable appears to be dynamically linked in a way that we
     can understand.  */
  if (! bfd_get_section_contents (abfd, dynsec, (void *) &linkinfo,
				  (file_ptr) dynoff,
				  (bfd_size_type) sizeof linkinfo))
    return TRUE;

  /* Swap in the dynamic link information.  */
  info->dyninfo.ld_loaded = GET_WORD (abfd, linkinfo.ld_loaded);
  info->dyninfo.ld_need = GET_WORD (abfd, linkinfo.ld_need);
  info->dyninfo.ld_rules = GET_WORD (abfd, linkinfo.ld_rules);
  info->dyninfo.ld_got = GET_WORD (abfd, linkinfo.ld_got);
  info->dyninfo.ld_plt = GET_WORD (abfd, linkinfo.ld_plt);
  info->dyninfo.ld_rel = GET_WORD (abfd, linkinfo.ld_rel);
  info->dyninfo.ld_hash = GET_WORD (abfd, linkinfo.ld_hash);
  info->dyninfo.ld_stab = GET_WORD (abfd, linkinfo.ld_stab);
  info->dyninfo.ld_stab_hash = GET_WORD (abfd, linkinfo.ld_stab_hash);
  info->dyninfo.ld_buckets = GET_WORD (abfd, linkinfo.ld_buckets);
  info->dyninfo.ld_symbols = GET_WORD (abfd, linkinfo.ld_symbols);
  info->dyninfo.ld_symb_size = GET_WORD (abfd, linkinfo.ld_symb_size);
  info->dyninfo.ld_text = GET_WORD (abfd, linkinfo.ld_text);
  info->dyninfo.ld_plt_sz = GET_WORD (abfd, linkinfo.ld_plt_sz);

  /* Reportedly the addresses need to be offset by the size of the
     exec header in an NMAGIC file.  */
  if (adata (abfd).magic == n_magic)
    {
      unsigned long exec_bytes_size = adata (abfd).exec_bytes_size;

      info->dyninfo.ld_need += exec_bytes_size;
      info->dyninfo.ld_rules += exec_bytes_size;
      info->dyninfo.ld_rel += exec_bytes_size;
      info->dyninfo.ld_hash += exec_bytes_size;
      info->dyninfo.ld_stab += exec_bytes_size;
      info->dyninfo.ld_symbols += exec_bytes_size;
    }

  /* The only way to get the size of the symbol information appears to
     be to determine the distance between it and the string table.  */
  info->dynsym_count = ((info->dyninfo.ld_symbols - info->dyninfo.ld_stab)
			/ EXTERNAL_NLIST_SIZE);
  BFD_ASSERT (info->dynsym_count * EXTERNAL_NLIST_SIZE
	      == (unsigned long) (info->dyninfo.ld_symbols
				  - info->dyninfo.ld_stab));

  /* Similarly, the relocs end at the hash table.  */
  info->dynrel_count = ((info->dyninfo.ld_hash - info->dyninfo.ld_rel)
			/ obj_reloc_entry_size (abfd));
  BFD_ASSERT (info->dynrel_count * obj_reloc_entry_size (abfd)
	      == (unsigned long) (info->dyninfo.ld_hash
				  - info->dyninfo.ld_rel));

  info->valid = TRUE;

  return TRUE;
}

/* Return the amount of memory required for the dynamic symbols.  */

static long
sunos_get_dynamic_symtab_upper_bound (bfd *abfd)
{
  struct sunos_dynamic_info *info;

  if (! sunos_read_dynamic_info (abfd))
    return -1;

  info = (struct sunos_dynamic_info *) obj_aout_dynamic_info (abfd);
  if (! info->valid)
    {
      bfd_set_error (bfd_error_no_symbols);
      return -1;
    }

  return (info->dynsym_count + 1) * sizeof (asymbol *);
}

/* Read the external dynamic symbols.  */

static bfd_boolean
sunos_slurp_dynamic_symtab (bfd *abfd)
{
  struct sunos_dynamic_info *info;
  bfd_size_type amt;

  /* Get the general dynamic information.  */
  if (obj_aout_dynamic_info (abfd) == NULL)
    {
      if (! sunos_read_dynamic_info (abfd))
	  return FALSE;
    }

  info = (struct sunos_dynamic_info *) obj_aout_dynamic_info (abfd);
  if (! info->valid)
    {
      bfd_set_error (bfd_error_no_symbols);
      return FALSE;
    }

  /* Get the dynamic nlist structures.  */
  if (info->dynsym == NULL)
    {
      amt = (bfd_size_type) info->dynsym_count * EXTERNAL_NLIST_SIZE;
      info->dynsym = bfd_alloc (abfd, amt);
      if (info->dynsym == NULL && info->dynsym_count != 0)
	return FALSE;
      if (bfd_seek (abfd, (file_ptr) info->dyninfo.ld_stab, SEEK_SET) != 0
	  || bfd_bread ((void *) info->dynsym, amt, abfd) != amt)
	{
	  if (info->dynsym != NULL)
	    {
	      bfd_release (abfd, info->dynsym);
	      info->dynsym = NULL;
	    }
	  return FALSE;
	}
    }

  /* Get the dynamic strings.  */
  if (info->dynstr == NULL)
    {
      amt = info->dyninfo.ld_symb_size;
      info->dynstr = bfd_alloc (abfd, amt);
      if (info->dynstr == NULL && info->dyninfo.ld_symb_size != 0)
	return FALSE;
      if (bfd_seek (abfd, (file_ptr) info->dyninfo.ld_symbols, SEEK_SET) != 0
	  || bfd_bread ((void *) info->dynstr, amt, abfd) != amt)
	{
	  if (info->dynstr != NULL)
	    {
	      bfd_release (abfd, info->dynstr);
	      info->dynstr = NULL;
	    }
	  return FALSE;
	}
    }

  return TRUE;
}

/* Read in the dynamic symbols.  */

static long
sunos_canonicalize_dynamic_symtab (bfd *abfd, asymbol **storage)
{
  struct sunos_dynamic_info *info;
  unsigned long i;

  if (! sunos_slurp_dynamic_symtab (abfd))
    return -1;

  info = (struct sunos_dynamic_info *) obj_aout_dynamic_info (abfd);

#ifdef CHECK_DYNAMIC_HASH
  /* Check my understanding of the dynamic hash table by making sure
     that each symbol can be located in the hash table.  */
  {
    bfd_size_type table_size;
    bfd_byte *table;
    bfd_size_type i;

    if (info->dyninfo.ld_buckets > info->dynsym_count)
      abort ();
    table_size = info->dyninfo.ld_stab - info->dyninfo.ld_hash;
    table = bfd_malloc (table_size);
    if (table == NULL && table_size != 0)
      abort ();
    if (bfd_seek (abfd, (file_ptr) info->dyninfo.ld_hash, SEEK_SET) != 0
	|| bfd_bread ((void *) table, table_size, abfd) != table_size)
      abort ();
    for (i = 0; i < info->dynsym_count; i++)
      {
	unsigned char *name;
	unsigned long hash;

	name = ((unsigned char *) info->dynstr
		+ GET_WORD (abfd, info->dynsym[i].e_strx));
	hash = 0;
	while (*name != '\0')
	  hash = (hash << 1) + *name++;
	hash &= 0x7fffffff;
	hash %= info->dyninfo.ld_buckets;
	while (GET_WORD (abfd, table + hash * HASH_ENTRY_SIZE) != i)
	  {
	    hash = GET_WORD (abfd,
			     table + hash * HASH_ENTRY_SIZE + BYTES_IN_WORD);
	    if (hash == 0 || hash >= table_size / HASH_ENTRY_SIZE)
	      abort ();
	  }
      }
    free (table);
  }
#endif /* CHECK_DYNAMIC_HASH */

  /* Get the asymbol structures corresponding to the dynamic nlist
     structures.  */
  if (info->canonical_dynsym == NULL)
    {
      bfd_size_type size;
      bfd_size_type strsize = info->dyninfo.ld_symb_size;

      size = (bfd_size_type) info->dynsym_count * sizeof (aout_symbol_type);
      info->canonical_dynsym = bfd_alloc (abfd, size);
      if (info->canonical_dynsym == NULL && info->dynsym_count != 0)
	return -1;

      if (! aout_32_translate_symbol_table (abfd, info->canonical_dynsym,
					    info->dynsym,
					    (bfd_size_type) info->dynsym_count,
					    info->dynstr, strsize, TRUE))
	{
	  if (info->canonical_dynsym != NULL)
	    {
	      bfd_release (abfd, info->canonical_dynsym);
	      info->canonical_dynsym = NULL;
	    }
	  return -1;
	}
    }

  /* Return pointers to the dynamic asymbol structures.  */
  for (i = 0; i < info->dynsym_count; i++)
    *storage++ = (asymbol *) (info->canonical_dynsym + i);
  *storage = NULL;

  return info->dynsym_count;
}

/* Return the amount of memory required for the dynamic relocs.  */

static long
sunos_get_dynamic_reloc_upper_bound (bfd *abfd)
{
  struct sunos_dynamic_info *info;

  if (! sunos_read_dynamic_info (abfd))
    return -1;

  info = (struct sunos_dynamic_info *) obj_aout_dynamic_info (abfd);
  if (! info->valid)
    {
      bfd_set_error (bfd_error_no_symbols);
      return -1;
    }

  return (info->dynrel_count + 1) * sizeof (arelent *);
}

/* Read in the dynamic relocs.  */

static long
sunos_canonicalize_dynamic_reloc (bfd *abfd, arelent **storage, asymbol **syms)
{
  struct sunos_dynamic_info *info;
  unsigned long i;
  bfd_size_type size;

  /* Get the general dynamic information.  */
  if (obj_aout_dynamic_info (abfd) == NULL)
    {
      if (! sunos_read_dynamic_info (abfd))
	return -1;
    }

  info = (struct sunos_dynamic_info *) obj_aout_dynamic_info (abfd);
  if (! info->valid)
    {
      bfd_set_error (bfd_error_no_symbols);
      return -1;
    }

  /* Get the dynamic reloc information.  */
  if (info->dynrel == NULL)
    {
      size = (bfd_size_type) info->dynrel_count * obj_reloc_entry_size (abfd);
      info->dynrel = bfd_alloc (abfd, size);
      if (info->dynrel == NULL && size != 0)
	return -1;
      if (bfd_seek (abfd, (file_ptr) info->dyninfo.ld_rel, SEEK_SET) != 0
	  || bfd_bread ((void *) info->dynrel, size, abfd) != size)
	{
	  if (info->dynrel != NULL)
	    {
	      bfd_release (abfd, info->dynrel);
	      info->dynrel = NULL;
	    }
	  return -1;
	}
    }

  /* Get the arelent structures corresponding to the dynamic reloc
     information.  */
  if (info->canonical_dynrel == NULL)
    {
      arelent *to;

      size = (bfd_size_type) info->dynrel_count * sizeof (arelent);
      info->canonical_dynrel = bfd_alloc (abfd, size);
      if (info->canonical_dynrel == NULL && info->dynrel_count != 0)
	return -1;

      to = info->canonical_dynrel;

      if (obj_reloc_entry_size (abfd) == RELOC_EXT_SIZE)
	{
	  struct reloc_ext_external *p;
	  struct reloc_ext_external *pend;

	  p = (struct reloc_ext_external *) info->dynrel;
	  pend = p + info->dynrel_count;
	  for (; p < pend; p++, to++)
	    NAME (aout, swap_ext_reloc_in) (abfd, p, to, syms,
					    (bfd_size_type) info->dynsym_count);
	}
      else
	{
	  struct reloc_std_external *p;
	  struct reloc_std_external *pend;

	  p = (struct reloc_std_external *) info->dynrel;
	  pend = p + info->dynrel_count;
	  for (; p < pend; p++, to++)
	    NAME (aout, swap_std_reloc_in) (abfd, p, to, syms,
					    (bfd_size_type) info->dynsym_count);
	}
    }

  /* Return pointers to the dynamic arelent structures.  */
  for (i = 0; i < info->dynrel_count; i++)
    *storage++ = info->canonical_dynrel + i;
  *storage = NULL;

  return info->dynrel_count;
}

/* Code to handle linking of SunOS shared libraries.  */

/* A SPARC procedure linkage table entry is 12 bytes.  The first entry
   in the table is a jump which is filled in by the runtime linker.
   The remaining entries are branches back to the first entry,
   followed by an index into the relocation table encoded to look like
   a sethi of %g0.  */

#define SPARC_PLT_ENTRY_SIZE (12)

static const bfd_byte sparc_plt_first_entry[SPARC_PLT_ENTRY_SIZE] =
{
  /* sethi %hi(0),%g1; address filled in by runtime linker.  */
  0x3, 0, 0, 0,
  /* jmp %g1; offset filled in by runtime linker.  */
  0x81, 0xc0, 0x60, 0,
  /* nop */
  0x1, 0, 0, 0
};

/* save %sp, -96, %sp */
#define SPARC_PLT_ENTRY_WORD0 ((bfd_vma) 0x9de3bfa0)
/* call; address filled in later.  */
#define SPARC_PLT_ENTRY_WORD1 ((bfd_vma) 0x40000000)
/* sethi; reloc index filled in later.  */
#define SPARC_PLT_ENTRY_WORD2 ((bfd_vma) 0x01000000)

/* This sequence is used when for the jump table entry to a defined
   symbol in a complete executable.  It is used when linking PIC
   compiled code which is not being put into a shared library.  */
/* sethi <address to be filled in later>, %g1 */
#define SPARC_PLT_PIC_WORD0 ((bfd_vma) 0x03000000)
/* jmp %g1 + <address to be filled in later> */
#define SPARC_PLT_PIC_WORD1 ((bfd_vma) 0x81c06000)
/* nop */
#define SPARC_PLT_PIC_WORD2 ((bfd_vma) 0x01000000)

/* An m68k procedure linkage table entry is 8 bytes.  The first entry
   in the table is a jump which is filled in the by the runtime
   linker.  The remaining entries are branches back to the first
   entry, followed by a two byte index into the relocation table.  */

#define M68K_PLT_ENTRY_SIZE (8)

static const bfd_byte m68k_plt_first_entry[M68K_PLT_ENTRY_SIZE] =
{
  /* jmps @# */
  0x4e, 0xf9,
  /* Filled in by runtime linker with a magic address.  */
  0, 0, 0, 0,
  /* Not used?  */
  0, 0
};

/* bsrl */
#define M68K_PLT_ENTRY_WORD0 ((bfd_vma) 0x61ff)
/* Remaining words filled in later.  */

/* An entry in the SunOS linker hash table.  */

struct sunos_link_hash_entry
{
  struct aout_link_hash_entry root;

  /* If this is a dynamic symbol, this is its index into the dynamic
     symbol table.  This is initialized to -1.  As the linker looks at
     the input files, it changes this to -2 if it will be added to the
     dynamic symbol table.  After all the input files have been seen,
     the linker will know whether to build a dynamic symbol table; if
     it does build one, this becomes the index into the table.  */
  long dynindx;

  /* If this is a dynamic symbol, this is the index of the name in the
     dynamic symbol string table.  */
  long dynstr_index;

  /* The offset into the global offset table used for this symbol.  If
     the symbol does not require a GOT entry, this is 0.  */
  bfd_vma got_offset;

  /* The offset into the procedure linkage table used for this symbol.
     If the symbol does not require a PLT entry, this is 0.  */
  bfd_vma plt_offset;

  /* Some linker flags.  */
  unsigned char flags;
  /* Symbol is referenced by a regular object.  */
#define SUNOS_REF_REGULAR 01
  /* Symbol is defined by a regular object.  */
#define SUNOS_DEF_REGULAR 02
  /* Symbol is referenced by a dynamic object.  */
#define SUNOS_REF_DYNAMIC 04
  /* Symbol is defined by a dynamic object.  */
#define SUNOS_DEF_DYNAMIC 010
  /* Symbol is a constructor symbol in a regular object.  */
#define SUNOS_CONSTRUCTOR 020
};

/* The SunOS linker hash table.  */

struct sunos_link_hash_table
{
  struct aout_link_hash_table root;

  /* The object which holds the dynamic sections.  */
  bfd *dynobj;

  /* Whether we have created the dynamic sections.  */
  bfd_boolean dynamic_sections_created;

  /* Whether we need the dynamic sections.  */
  bfd_boolean dynamic_sections_needed;

  /* Whether we need the .got table.  */
  bfd_boolean got_needed;

  /* The number of dynamic symbols.  */
  size_t dynsymcount;

  /* The number of buckets in the hash table.  */
  size_t bucketcount;

  /* The list of dynamic objects needed by dynamic objects included in
     the link.  */
  struct bfd_link_needed_list *needed;

  /* The offset of __GLOBAL_OFFSET_TABLE_ into the .got section.  */
  bfd_vma got_base;
};

/* Routine to create an entry in an SunOS link hash table.  */

static struct bfd_hash_entry *
sunos_link_hash_newfunc (struct bfd_hash_entry *entry,
			 struct bfd_hash_table *table,
			 const char *string)
{
  struct sunos_link_hash_entry *ret = (struct sunos_link_hash_entry *) entry;

  /* Allocate the structure if it has not already been allocated by a
     subclass.  */
  if (ret ==  NULL)
    ret = bfd_hash_allocate (table, sizeof (* ret));
  if (ret == NULL)
    return NULL;

  /* Call the allocation method of the superclass.  */
  ret = ((struct sunos_link_hash_entry *)
	 NAME (aout, link_hash_newfunc) ((struct bfd_hash_entry *) ret,
					 table, string));
  if (ret != NULL)
    {
      /* Set local fields.  */
      ret->dynindx = -1;
      ret->dynstr_index = -1;
      ret->got_offset = 0;
      ret->plt_offset = 0;
      ret->flags = 0;
    }

  return (struct bfd_hash_entry *) ret;
}

/* Create a SunOS link hash table.  */

static struct bfd_link_hash_table *
sunos_link_hash_table_create (bfd *abfd)
{
  struct sunos_link_hash_table *ret;
  bfd_size_type amt = sizeof (struct sunos_link_hash_table);

  ret = bfd_malloc (amt);
  if (ret ==  NULL)
    return NULL;
  if (!NAME (aout, link_hash_table_init) (&ret->root, abfd,
					  sunos_link_hash_newfunc,
					  sizeof (struct sunos_link_hash_entry)))
    {
      free (ret);
      return NULL;
    }

  ret->dynobj = NULL;
  ret->dynamic_sections_created = FALSE;
  ret->dynamic_sections_needed = FALSE;
  ret->got_needed = FALSE;
  ret->dynsymcount = 0;
  ret->bucketcount = 0;
  ret->needed = NULL;
  ret->got_base = 0;

  return &ret->root.root;
}

/* Look up an entry in an SunOS link hash table.  */

#define sunos_link_hash_lookup(table, string, create, copy, follow) \
  ((struct sunos_link_hash_entry *) \
   aout_link_hash_lookup (&(table)->root, (string), (create), (copy),\
			  (follow)))

/* Traverse a SunOS link hash table.  */

#define sunos_link_hash_traverse(table, func, info)			\
  (aout_link_hash_traverse						\
   (&(table)->root,							\
    (bfd_boolean (*) (struct aout_link_hash_entry *, void *)) (func),	\
    (info)))

/* Get the SunOS link hash table from the info structure.  This is
   just a cast.  */

#define sunos_hash_table(p) ((struct sunos_link_hash_table *) ((p)->hash))

/* Create the dynamic sections needed if we are linking against a
   dynamic object, or if we are linking PIC compiled code.  ABFD is a
   bfd we can attach the dynamic sections to.  The linker script will
   look for these special sections names and put them in the right
   place in the output file.  See include/aout/sun4.h for more details
   of the dynamic linking information.  */

static bfd_boolean
sunos_create_dynamic_sections (bfd *abfd,
			       struct bfd_link_info *info,
			       bfd_boolean needed)
{
  asection *s;

  if (! sunos_hash_table (info)->dynamic_sections_created)
    {
      flagword flags;

      sunos_hash_table (info)->dynobj = abfd;

      flags = (SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS | SEC_IN_MEMORY
	       | SEC_LINKER_CREATED);

      /* The .dynamic section holds the basic dynamic information: the
	 sun4_dynamic structure, the dynamic debugger information, and
	 the sun4_dynamic_link structure.  */
      s = bfd_make_section_with_flags (abfd, ".dynamic", flags);
      if (s == NULL
	  || ! bfd_set_section_alignment (abfd, s, 2))
	return FALSE;

      /* The .got section holds the global offset table.  The address
	 is put in the ld_got field.  */
      s = bfd_make_section_with_flags (abfd, ".got", flags);
      if (s == NULL
	  || ! bfd_set_section_alignment (abfd, s, 2))
	return FALSE;

      /* The .plt section holds the procedure linkage table.  The
	 address is put in the ld_plt field.  */
      s = bfd_make_section_with_flags (abfd, ".plt", flags | SEC_CODE);
      if (s == NULL
	  || ! bfd_set_section_alignment (abfd, s, 2))
	return FALSE;

      /* The .dynrel section holds the dynamic relocs.  The address is
	 put in the ld_rel field.  */
      s = bfd_make_section_with_flags (abfd, ".dynrel", flags | SEC_READONLY);
      if (s == NULL
	  || ! bfd_set_section_alignment (abfd, s, 2))
	return FALSE;

      /* The .hash section holds the dynamic hash table.  The address
	 is put in the ld_hash field.  */
      s = bfd_make_section_with_flags (abfd, ".hash", flags | SEC_READONLY);
      if (s == NULL
	  || ! bfd_set_section_alignment (abfd, s, 2))
	return FALSE;

      /* The .dynsym section holds the dynamic symbols.  The address
	 is put in the ld_stab field.  */
      s = bfd_make_section_with_flags (abfd, ".dynsym", flags | SEC_READONLY);
      if (s == NULL
	  || ! bfd_set_section_alignment (abfd, s, 2))
	return FALSE;

      /* The .dynstr section holds the dynamic symbol string table.
	 The address is put in the ld_symbols field.  */
      s = bfd_make_section_with_flags (abfd, ".dynstr", flags | SEC_READONLY);
      if (s == NULL
	  || ! bfd_set_section_alignment (abfd, s, 2))
	return FALSE;

      sunos_hash_table (info)->dynamic_sections_created = TRUE;
    }

  if ((needed && ! sunos_hash_table (info)->dynamic_sections_needed)
      || info->shared)
    {
      bfd *dynobj;

      dynobj = sunos_hash_table (info)->dynobj;

      s = bfd_get_section_by_name (dynobj, ".got");
      if (s->size == 0)
	s->size = BYTES_IN_WORD;

      sunos_hash_table (info)->dynamic_sections_needed = TRUE;
      sunos_hash_table (info)->got_needed = TRUE;
    }

  return TRUE;
}

/* Add dynamic symbols during a link.  This is called by the a.out
   backend linker for each object it encounters.  */

static bfd_boolean
sunos_add_dynamic_symbols (bfd *abfd,
			   struct bfd_link_info *info,
			   struct external_nlist **symsp,
			   bfd_size_type *sym_countp,
			   char **stringsp)
{
  bfd *dynobj;
  struct sunos_dynamic_info *dinfo;
  unsigned long need;

  /* Make sure we have all the required sections.  */
  if (info->hash->creator == abfd->xvec)
    {
      if (! sunos_create_dynamic_sections (abfd, info,
					   ((abfd->flags & DYNAMIC) != 0
					    && !info->relocatable)))
	return FALSE;
    }

  /* There is nothing else to do for a normal object.  */
  if ((abfd->flags & DYNAMIC) == 0)
    return TRUE;

  dynobj = sunos_hash_table (info)->dynobj;

  /* We do not want to include the sections in a dynamic object in the
     output file.  We hack by simply clobbering the list of sections
     in the BFD.  This could be handled more cleanly by, say, a new
     section flag; the existing SEC_NEVER_LOAD flag is not the one we
     want, because that one still implies that the section takes up
     space in the output file.  If this is the first object we have
     seen, we must preserve the dynamic sections we just created.  */
  if (abfd != dynobj)
    abfd->sections = NULL;
  else
    {
      asection *s;

      for (s = abfd->sections; s != NULL; s = s->next)
	{
	  if ((s->flags & SEC_LINKER_CREATED) == 0)
	    bfd_section_list_remove (abfd, s);
	}
    }

  /* The native linker seems to just ignore dynamic objects when -r is
     used.  */
  if (info->relocatable)
    return TRUE;

  /* There's no hope of using a dynamic object which does not exactly
     match the format of the output file.  */
  if (info->hash->creator != abfd->xvec)
    {
      bfd_set_error (bfd_error_invalid_operation);
      return FALSE;
    }

  /* Make sure we have a .need and a .rules sections.  These are only
     needed if there really is a dynamic object in the link, so they
     are not added by sunos_create_dynamic_sections.  */
  if (bfd_get_section_by_name (dynobj, ".need") == NULL)
    {
      /* The .need section holds the list of names of shared objets
	 which must be included at runtime.  The address of this
	 section is put in the ld_need field.  */
      flagword flags = (SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS
			| SEC_IN_MEMORY | SEC_READONLY);
      asection *s = bfd_make_section_with_flags (dynobj, ".need", flags);
      if (s == NULL
	  || ! bfd_set_section_alignment (dynobj, s, 2))
	return FALSE;
    }

  if (bfd_get_section_by_name (dynobj, ".rules") == NULL)
    {
      /* The .rules section holds the path to search for shared
	 objects.  The address of this section is put in the ld_rules
	 field.  */
      flagword flags = (SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS
			| SEC_IN_MEMORY | SEC_READONLY);
      asection *s = bfd_make_section_with_flags (dynobj, ".rules", flags);
      if (s == NULL
	  || ! bfd_set_section_alignment (dynobj, s, 2))
	return FALSE;
    }

  /* Pick up the dynamic symbols and return them to the caller.  */
  if (! sunos_slurp_dynamic_symtab (abfd))
    return FALSE;

  dinfo = (struct sunos_dynamic_info *) obj_aout_dynamic_info (abfd);
  *symsp = dinfo->dynsym;
  *sym_countp = dinfo->dynsym_count;
  *stringsp = dinfo->dynstr;

  /* Record information about any other objects needed by this one.  */
  need = dinfo->dyninfo.ld_need;
  while (need != 0)
    {
      bfd_byte buf[16];
      unsigned long name, flags;
      unsigned short major_vno, minor_vno;
      struct bfd_link_needed_list *needed, **pp;
      char *namebuf, *p;
      bfd_size_type alc;
      bfd_byte b;
      char *namecopy;

      if (bfd_seek (abfd, (file_ptr) need, SEEK_SET) != 0
	  || bfd_bread (buf, (bfd_size_type) 16, abfd) != 16)
	return FALSE;

      /* For the format of an ld_need entry, see aout/sun4.h.  We
	 should probably define structs for this manipulation.  */
      name = bfd_get_32 (abfd, buf);
      flags = bfd_get_32 (abfd, buf + 4);
      major_vno = (unsigned short) bfd_get_16 (abfd, buf + 8);
      minor_vno = (unsigned short) bfd_get_16 (abfd, buf + 10);
      need = bfd_get_32 (abfd, buf + 12);

      alc = sizeof (struct bfd_link_needed_list);
      needed = bfd_alloc (abfd, alc);
      if (needed == NULL)
	return FALSE;
      needed->by = abfd;

      /* We return the name as [-l]name[.maj][.min].  */
      alc = 30;
      namebuf = bfd_malloc (alc + 1);
      if (namebuf == NULL)
	return FALSE;
      p = namebuf;

      if ((flags & 0x80000000) != 0)
	{
	  *p++ = '-';
	  *p++ = 'l';
	}
      if (bfd_seek (abfd, (file_ptr) name, SEEK_SET) != 0)
	{
	  free (namebuf);
	  return FALSE;
	}

      do
	{
	  if (bfd_bread (&b, (bfd_size_type) 1, abfd) != 1)
	    {
	      free (namebuf);
	      return FALSE;
	    }

	  if ((bfd_size_type) (p - namebuf) >= alc)
	    {
	      char *n;

	      alc *= 2;
	      n = bfd_realloc (namebuf, alc + 1);
	      if (n == NULL)
		{
		  free (namebuf);
		  return FALSE;
		}
	      p = n + (p - namebuf);
	      namebuf = n;
	    }

	  *p++ = b;
	}
      while (b != '\0');

      if (major_vno == 0)
	*p = '\0';
      else
	{
	  char majbuf[30];
	  char minbuf[30];

	  sprintf (majbuf, ".%d", major_vno);
	  if (minor_vno == 0)
	    minbuf[0] = '\0';
	  else
	    sprintf (minbuf, ".%d", minor_vno);

	  if ((p - namebuf) + strlen (majbuf) + strlen (minbuf) >= alc)
	    {
	      char *n;

	      alc = (p - namebuf) + strlen (majbuf) + strlen (minbuf);
	      n = bfd_realloc (namebuf, alc + 1);
	      if (n == NULL)
		{
		  free (namebuf);
		  return FALSE;
		}
	      p = n + (p - namebuf);
	      namebuf = n;
	    }

	  strcpy (p, majbuf);
	  strcat (p, minbuf);
	}

      namecopy = bfd_alloc (abfd, (bfd_size_type) strlen (namebuf) + 1);
      if (namecopy == NULL)
	{
	  free (namebuf);
	  return FALSE;
	}
      strcpy (namecopy, namebuf);
      free (namebuf);
      needed->name = namecopy;

      needed->next = NULL;

      for (pp = &sunos_hash_table (info)->needed;
	   *pp != NULL;
	   pp = &(*pp)->next)
	;
      *pp = needed;
    }

  return TRUE;
}

/* Function to add a single symbol to the linker hash table.  This is
   a wrapper around _bfd_generic_link_add_one_symbol which handles the
   tweaking needed for dynamic linking support.  */

static bfd_boolean
sunos_add_one_symbol (struct bfd_link_info *info,
		      bfd *abfd,
		      const char *name,
		      flagword flags,
		      asection *section,
		      bfd_vma value,
		      const char *string,
		      bfd_boolean copy,
		      bfd_boolean collect,
		      struct bfd_link_hash_entry **hashp)
{
  struct sunos_link_hash_entry *h;
  int new_flag;

  if ((flags & (BSF_INDIRECT | BSF_WARNING | BSF_CONSTRUCTOR)) != 0
      || ! bfd_is_und_section (section))
    h = sunos_link_hash_lookup (sunos_hash_table (info), name, TRUE, copy,
				FALSE);
  else
    h = ((struct sunos_link_hash_entry *)
	 bfd_wrapped_link_hash_lookup (abfd, info, name, TRUE, copy, FALSE));
  if (h == NULL)
    return FALSE;

  if (hashp != NULL)
    *hashp = (struct bfd_link_hash_entry *) h;

  /* Treat a common symbol in a dynamic object as defined in the .bss
     section of the dynamic object.  We don't want to allocate space
     for it in our process image.  */
  if ((abfd->flags & DYNAMIC) != 0
      && bfd_is_com_section (section))
    section = obj_bsssec (abfd);

  if (! bfd_is_und_section (section)
      && h->root.root.type != bfd_link_hash_new
      && h->root.root.type != bfd_link_hash_undefined
      && h->root.root.type != bfd_link_hash_defweak)
    {
      /* We are defining the symbol, and it is already defined.  This
	 is a potential multiple definition error.  */
      if ((abfd->flags & DYNAMIC) != 0)
	{
	  /* The definition we are adding is from a dynamic object.
	     We do not want this new definition to override the
	     existing definition, so we pretend it is just a
	     reference.  */
	  section = bfd_und_section_ptr;
	}
      else if (h->root.root.type == bfd_link_hash_defined
	       && h->root.root.u.def.section->owner != NULL
	       && (h->root.root.u.def.section->owner->flags & DYNAMIC) != 0)
	{
	  /* The existing definition is from a dynamic object.  We
	     want to override it with the definition we just found.
	     Clobber the existing definition.  */
	  h->root.root.type = bfd_link_hash_undefined;
	  h->root.root.u.undef.abfd = h->root.root.u.def.section->owner;
	}
      else if (h->root.root.type == bfd_link_hash_common
	       && (h->root.root.u.c.p->section->owner->flags & DYNAMIC) != 0)
	{
	  /* The existing definition is from a dynamic object.  We
	     want to override it with the definition we just found.
	     Clobber the existing definition.  We can't set it to new,
	     because it is on the undefined list.  */
	  h->root.root.type = bfd_link_hash_undefined;
	  h->root.root.u.undef.abfd = h->root.root.u.c.p->section->owner;
	}
    }

  if ((abfd->flags & DYNAMIC) != 0
      && abfd->xvec == info->hash->creator
      && (h->flags & SUNOS_CONSTRUCTOR) != 0)
    /* The existing symbol is a constructor symbol, and this symbol
       is from a dynamic object.  A constructor symbol is actually a
       definition, although the type will be bfd_link_hash_undefined
       at this point.  We want to ignore the definition from the
       dynamic object.  */
    section = bfd_und_section_ptr;
  else if ((flags & BSF_CONSTRUCTOR) != 0
	   && (abfd->flags & DYNAMIC) == 0
	   && h->root.root.type == bfd_link_hash_defined
	   && h->root.root.u.def.section->owner != NULL
	   && (h->root.root.u.def.section->owner->flags & DYNAMIC) != 0)
    /* The existing symbol is defined by a dynamic object, and this
       is a constructor symbol.  As above, we want to force the use
       of the constructor symbol from the regular object.  */
    h->root.root.type = bfd_link_hash_new;

  /* Do the usual procedure for adding a symbol.  */
  if (! _bfd_generic_link_add_one_symbol (info, abfd, name, flags, section,
					  value, string, copy, collect,
					  hashp))
    return FALSE;

  if (abfd->xvec == info->hash->creator)
    {
      /* Set a flag in the hash table entry indicating the type of
	 reference or definition we just found.  Keep a count of the
	 number of dynamic symbols we find.  A dynamic symbol is one
	 which is referenced or defined by both a regular object and a
	 shared object.  */
      if ((abfd->flags & DYNAMIC) == 0)
	{
	  if (bfd_is_und_section (section))
	    new_flag = SUNOS_REF_REGULAR;
	  else
	    new_flag = SUNOS_DEF_REGULAR;
	}
      else
	{
	  if (bfd_is_und_section (section))
	    new_flag = SUNOS_REF_DYNAMIC;
	  else
	    new_flag = SUNOS_DEF_DYNAMIC;
	}
      h->flags |= new_flag;

      if (h->dynindx == -1
	  && (h->flags & (SUNOS_DEF_REGULAR | SUNOS_REF_REGULAR)) != 0)
	{
	  ++sunos_hash_table (info)->dynsymcount;
	  h->dynindx = -2;
	}

      if ((flags & BSF_CONSTRUCTOR) != 0
	  && (abfd->flags & DYNAMIC) == 0)
	h->flags |= SUNOS_CONSTRUCTOR;
    }

  return TRUE;
}

extern const bfd_target MY (vec);

/* Return the list of objects needed by BFD.  */

struct bfd_link_needed_list *
bfd_sunos_get_needed_list (bfd *abfd ATTRIBUTE_UNUSED,
			   struct bfd_link_info *info)
{
  if (info->hash->creator != &MY (vec))
    return NULL;
  return sunos_hash_table (info)->needed;
}

/* Record an assignment made to a symbol by a linker script.  We need
   this in case some dynamic object refers to this symbol.  */

bfd_boolean
bfd_sunos_record_link_assignment (bfd *output_bfd,
				  struct bfd_link_info *info,
				  const char *name)
{
  struct sunos_link_hash_entry *h;

  if (output_bfd->xvec != &MY(vec))
    return TRUE;

  /* This is called after we have examined all the input objects.  If
     the symbol does not exist, it merely means that no object refers
     to it, and we can just ignore it at this point.  */
  h = sunos_link_hash_lookup (sunos_hash_table (info), name,
			      FALSE, FALSE, FALSE);
  if (h == NULL)
    return TRUE;

  /* In a shared library, the __DYNAMIC symbol does not appear in the
     dynamic symbol table.  */
  if (! info->shared || strcmp (name, "__DYNAMIC") != 0)
    {
      h->flags |= SUNOS_DEF_REGULAR;

      if (h->dynindx == -1)
	{
	  ++sunos_hash_table (info)->dynsymcount;
	  h->dynindx = -2;
	}
    }

  return TRUE;
}

/* Scan the relocs for an input section using standard relocs.  We
   need to figure out what to do for each reloc against a dynamic
   symbol.  If the symbol is in the .text section, an entry is made in
   the procedure linkage table.  Note that this will do the wrong
   thing if the symbol is actually data; I don't think the Sun 3
   native linker handles this case correctly either.  If the symbol is
   not in the .text section, we must preserve the reloc as a dynamic
   reloc.  FIXME: We should also handle the PIC relocs here by
   building global offset table entries.  */

static bfd_boolean
sunos_scan_std_relocs (struct bfd_link_info *info,
		       bfd *abfd,
		       asection *sec ATTRIBUTE_UNUSED,
		       const struct reloc_std_external *relocs,
		       bfd_size_type rel_size)
{
  bfd *dynobj;
  asection *splt = NULL;
  asection *srel = NULL;
  struct sunos_link_hash_entry **sym_hashes;
  const struct reloc_std_external *rel, *relend;

  /* We only know how to handle m68k plt entries.  */
  if (bfd_get_arch (abfd) != bfd_arch_m68k)
    {
      bfd_set_error (bfd_error_invalid_target);
      return FALSE;
    }

  dynobj = NULL;

  sym_hashes = (struct sunos_link_hash_entry **) obj_aout_sym_hashes (abfd);

  relend = relocs + rel_size / RELOC_STD_SIZE;
  for (rel = relocs; rel < relend; rel++)
    {
      int r_index;
      struct sunos_link_hash_entry *h;

      /* We only want relocs against external symbols.  */
      if (bfd_header_big_endian (abfd))
	{
	  if ((rel->r_type[0] & RELOC_STD_BITS_EXTERN_BIG) == 0)
	    continue;
	}
      else
	{
	  if ((rel->r_type[0] & RELOC_STD_BITS_EXTERN_LITTLE) == 0)
	    continue;
	}

      /* Get the symbol index.  */
      if (bfd_header_big_endian (abfd))
	r_index = ((rel->r_index[0] << 16)
		   | (rel->r_index[1] << 8)
		   | rel->r_index[2]);
      else
	r_index = ((rel->r_index[2] << 16)
		   | (rel->r_index[1] << 8)
		   | rel->r_index[0]);

      /* Get the hash table entry.  */
      h = sym_hashes[r_index];
      if (h == NULL)
	/* This should not normally happen, but it will in any case
	   be caught in the relocation phase.  */
	continue;

      /* At this point common symbols have already been allocated, so
	 we don't have to worry about them.  We need to consider that
	 we may have already seen this symbol and marked it undefined;
	 if the symbol is really undefined, then SUNOS_DEF_DYNAMIC
	 will be zero.  */
      if (h->root.root.type != bfd_link_hash_defined
	  && h->root.root.type != bfd_link_hash_defweak
	  && h->root.root.type != bfd_link_hash_undefined)
	continue;

      if ((h->flags & SUNOS_DEF_DYNAMIC) == 0
	  || (h->flags & SUNOS_DEF_REGULAR) != 0)
	continue;

      if (dynobj == NULL)
	{
	  asection *sgot;

	  if (! sunos_create_dynamic_sections (abfd, info, FALSE))
	    return FALSE;
	  dynobj = sunos_hash_table (info)->dynobj;
	  splt = bfd_get_section_by_name (dynobj, ".plt");
	  srel = bfd_get_section_by_name (dynobj, ".dynrel");
	  BFD_ASSERT (splt != NULL && srel != NULL);

	  sgot = bfd_get_section_by_name (dynobj, ".got");
	  BFD_ASSERT (sgot != NULL);
	  if (sgot->size == 0)
	    sgot->size = BYTES_IN_WORD;
	  sunos_hash_table (info)->got_needed = TRUE;
	}

      BFD_ASSERT ((h->flags & SUNOS_REF_REGULAR) != 0);
      BFD_ASSERT (h->plt_offset != 0
		  || ((h->root.root.type == bfd_link_hash_defined
		       || h->root.root.type == bfd_link_hash_defweak)
		      ? (h->root.root.u.def.section->owner->flags
			 & DYNAMIC) != 0
		      : (h->root.root.u.undef.abfd->flags & DYNAMIC) != 0));

      /* This reloc is against a symbol defined only by a dynamic
	 object.  */
      if (h->root.root.type == bfd_link_hash_undefined)
	/* Presumably this symbol was marked as being undefined by
	   an earlier reloc.  */
	srel->size += RELOC_STD_SIZE;
      else if ((h->root.root.u.def.section->flags & SEC_CODE) == 0)
	{
	  bfd *sub;

	  /* This reloc is not in the .text section.  It must be
	     copied into the dynamic relocs.  We mark the symbol as
	     being undefined.  */
	  srel->size += RELOC_STD_SIZE;
	  sub = h->root.root.u.def.section->owner;
	  h->root.root.type = bfd_link_hash_undefined;
	  h->root.root.u.undef.abfd = sub;
	}
      else
	{
	  /* This symbol is in the .text section.  We must give it an
	     entry in the procedure linkage table, if we have not
	     already done so.  We change the definition of the symbol
	     to the .plt section; this will cause relocs against it to
	     be handled correctly.  */
	  if (h->plt_offset == 0)
	    {
	      if (splt->size == 0)
		splt->size = M68K_PLT_ENTRY_SIZE;
	      h->plt_offset = splt->size;

	      if ((h->flags & SUNOS_DEF_REGULAR) == 0)
		{
		  h->root.root.u.def.section = splt;
		  h->root.root.u.def.value = splt->size;
		}

	      splt->size += M68K_PLT_ENTRY_SIZE;

	      /* We may also need a dynamic reloc entry.  */
	      if ((h->flags & SUNOS_DEF_REGULAR) == 0)
		srel->size += RELOC_STD_SIZE;
	    }
	}
    }

  return TRUE;
}

/* Scan the relocs for an input section using extended relocs.  We
   need to figure out what to do for each reloc against a dynamic
   symbol.  If the reloc is a WDISP30, and the symbol is in the .text
   section, an entry is made in the procedure linkage table.
   Otherwise, we must preserve the reloc as a dynamic reloc.  */

static bfd_boolean
sunos_scan_ext_relocs (struct bfd_link_info *info,
		       bfd *abfd,
		       asection *sec ATTRIBUTE_UNUSED,
		       const struct reloc_ext_external *relocs,
		       bfd_size_type rel_size)
{
  bfd *dynobj;
  struct sunos_link_hash_entry **sym_hashes;
  const struct reloc_ext_external *rel, *relend;
  asection *splt = NULL;
  asection *sgot = NULL;
  asection *srel = NULL;
  bfd_size_type amt;

  /* We only know how to handle SPARC plt entries.  */
  if (bfd_get_arch (abfd) != bfd_arch_sparc)
    {
      bfd_set_error (bfd_error_invalid_target);
      return FALSE;
    }

  dynobj = NULL;

  sym_hashes = (struct sunos_link_hash_entry **) obj_aout_sym_hashes (abfd);

  relend = relocs + rel_size / RELOC_EXT_SIZE;
  for (rel = relocs; rel < relend; rel++)
    {
      unsigned int r_index;
      int r_extern;
      int r_type;
      struct sunos_link_hash_entry *h = NULL;

      /* Swap in the reloc information.  */
      if (bfd_header_big_endian (abfd))
	{
	  r_index = ((rel->r_index[0] << 16)
		     | (rel->r_index[1] << 8)
		     | rel->r_index[2]);
	  r_extern = (0 != (rel->r_type[0] & RELOC_EXT_BITS_EXTERN_BIG));
	  r_type = ((rel->r_type[0] & RELOC_EXT_BITS_TYPE_BIG)
		    >> RELOC_EXT_BITS_TYPE_SH_BIG);
	}
      else
	{
	  r_index = ((rel->r_index[2] << 16)
		     | (rel->r_index[1] << 8)
		     | rel->r_index[0]);
	  r_extern = (0 != (rel->r_type[0] & RELOC_EXT_BITS_EXTERN_LITTLE));
	  r_type = ((rel->r_type[0] & RELOC_EXT_BITS_TYPE_LITTLE)
		    >> RELOC_EXT_BITS_TYPE_SH_LITTLE);
	}

      if (r_extern)
	{
	  h = sym_hashes[r_index];
	  if (h == NULL)
	    {
	      /* This should not normally happen, but it will in any
		 case be caught in the relocation phase.  */
	      continue;
	    }
	}

      /* If this is a base relative reloc, we need to make an entry in
	 the .got section.  */
      if (r_type == RELOC_BASE10
	  || r_type == RELOC_BASE13
	  || r_type == RELOC_BASE22)
	{
	  if (dynobj == NULL)
	    {
	      if (! sunos_create_dynamic_sections (abfd, info, FALSE))
		return FALSE;
	      dynobj = sunos_hash_table (info)->dynobj;
	      splt = bfd_get_section_by_name (dynobj, ".plt");
	      sgot = bfd_get_section_by_name (dynobj, ".got");
	      srel = bfd_get_section_by_name (dynobj, ".dynrel");
	      BFD_ASSERT (splt != NULL && sgot != NULL && srel != NULL);

	      /* Make sure we have an initial entry in the .got table.  */
	      if (sgot->size == 0)
		sgot->size = BYTES_IN_WORD;
	      sunos_hash_table (info)->got_needed = TRUE;
	    }

	  if (r_extern)
	    {
	      if (h->got_offset != 0)
		continue;

	      h->got_offset = sgot->size;
	    }
	  else
	    {
	      if (r_index >= bfd_get_symcount (abfd))
		/* This is abnormal, but should be caught in the
		   relocation phase.  */
		continue;

	      if (adata (abfd).local_got_offsets == NULL)
		{
		  amt = bfd_get_symcount (abfd);
		  amt *= sizeof (bfd_vma);
		  adata (abfd).local_got_offsets = bfd_zalloc (abfd, amt);
		  if (adata (abfd).local_got_offsets == NULL)
		    return FALSE;
		}

	      if (adata (abfd).local_got_offsets[r_index] != 0)
		continue;

	      adata (abfd).local_got_offsets[r_index] = sgot->size;
	    }

	  sgot->size += BYTES_IN_WORD;

	  /* If we are making a shared library, or if the symbol is
	     defined by a dynamic object, we will need a dynamic reloc
	     entry.  */
	  if (info->shared
	      || (h != NULL
		  && (h->flags & SUNOS_DEF_DYNAMIC) != 0
		  && (h->flags & SUNOS_DEF_REGULAR) == 0))
	    srel->size += RELOC_EXT_SIZE;

	  continue;
	}

      /* Otherwise, we are only interested in relocs against symbols
	 defined in dynamic objects but not in regular objects.  We
	 only need to consider relocs against external symbols.  */
      if (! r_extern)
	{
	  /* But, if we are creating a shared library, we need to
	     generate an absolute reloc.  */
	  if (info->shared)
	    {
	      if (dynobj == NULL)
		{
		  if (! sunos_create_dynamic_sections (abfd, info, TRUE))
		    return FALSE;
		  dynobj = sunos_hash_table (info)->dynobj;
		  splt = bfd_get_section_by_name (dynobj, ".plt");
		  sgot = bfd_get_section_by_name (dynobj, ".got");
		  srel = bfd_get_section_by_name (dynobj, ".dynrel");
		  BFD_ASSERT (splt != NULL && sgot != NULL && srel != NULL);
		}

	      srel->size += RELOC_EXT_SIZE;
	    }

	  continue;
	}

      /* At this point common symbols have already been allocated, so
	 we don't have to worry about them.  We need to consider that
	 we may have already seen this symbol and marked it undefined;
	 if the symbol is really undefined, then SUNOS_DEF_DYNAMIC
	 will be zero.  */
      if (h->root.root.type != bfd_link_hash_defined
	  && h->root.root.type != bfd_link_hash_defweak
	  && h->root.root.type != bfd_link_hash_undefined)
	continue;

      if (r_type != RELOC_JMP_TBL
	  && ! info->shared
	  && ((h->flags & SUNOS_DEF_DYNAMIC) == 0
	      || (h->flags & SUNOS_DEF_REGULAR) != 0))
	continue;

      if (r_type == RELOC_JMP_TBL
	  && ! info->shared
	  && (h->flags & SUNOS_DEF_DYNAMIC) == 0
	  && (h->flags & SUNOS_DEF_REGULAR) == 0)
	{
	  /* This symbol is apparently undefined.  Don't do anything
	     here; just let the relocation routine report an undefined
	     symbol.  */
	  continue;
	}

      if (strcmp (h->root.root.root.string, "__GLOBAL_OFFSET_TABLE_") == 0)
	continue;

      if (dynobj == NULL)
	{
	  if (! sunos_create_dynamic_sections (abfd, info, FALSE))
	    return FALSE;
	  dynobj = sunos_hash_table (info)->dynobj;
	  splt = bfd_get_section_by_name (dynobj, ".plt");
	  sgot = bfd_get_section_by_name (dynobj, ".got");
	  srel = bfd_get_section_by_name (dynobj, ".dynrel");
	  BFD_ASSERT (splt != NULL && sgot != NULL && srel != NULL);

	  /* Make sure we have an initial entry in the .got table.  */
	  if (sgot->size == 0)
	    sgot->size = BYTES_IN_WORD;
	  sunos_hash_table (info)->got_needed = TRUE;
	}

      BFD_ASSERT (r_type == RELOC_JMP_TBL
		  || info->shared
		  || (h->flags & SUNOS_REF_REGULAR) != 0);
      BFD_ASSERT (r_type == RELOC_JMP_TBL
		  || info->shared
		  || h->plt_offset != 0
		  || ((h->root.root.type == bfd_link_hash_defined
		       || h->root.root.type == bfd_link_hash_defweak)
		      ? (h->root.root.u.def.section->owner->flags
			 & DYNAMIC) != 0
		      : (h->root.root.u.undef.abfd->flags & DYNAMIC) != 0));

      /* This reloc is against a symbol defined only by a dynamic
	 object, or it is a jump table reloc from PIC compiled code.  */

      if (r_type != RELOC_JMP_TBL
	  && h->root.root.type == bfd_link_hash_undefined)
	/* Presumably this symbol was marked as being undefined by
	   an earlier reloc.  */
	srel->size += RELOC_EXT_SIZE;

      else if (r_type != RELOC_JMP_TBL
	       && (h->root.root.u.def.section->flags & SEC_CODE) == 0)
	{
	  bfd *sub;

	  /* This reloc is not in the .text section.  It must be
	     copied into the dynamic relocs.  We mark the symbol as
	     being undefined.  */
	  srel->size += RELOC_EXT_SIZE;
	  if ((h->flags & SUNOS_DEF_REGULAR) == 0)
	    {
	      sub = h->root.root.u.def.section->owner;
	      h->root.root.type = bfd_link_hash_undefined;
	      h->root.root.u.undef.abfd = sub;
	    }
	}
      else
	{
	  /* This symbol is in the .text section.  We must give it an
	     entry in the procedure linkage table, if we have not
	     already done so.  We change the definition of the symbol
	     to the .plt section; this will cause relocs against it to
	     be handled correctly.  */
	  if (h->plt_offset == 0)
	    {
	      if (splt->size == 0)
		splt->size = SPARC_PLT_ENTRY_SIZE;
	      h->plt_offset = splt->size;

	      if ((h->flags & SUNOS_DEF_REGULAR) == 0)
		{
		  if (h->root.root.type == bfd_link_hash_undefined)
		    h->root.root.type = bfd_link_hash_defined;
		  h->root.root.u.def.section = splt;
		  h->root.root.u.def.value = splt->size;
		}

	      splt->size += SPARC_PLT_ENTRY_SIZE;

	      /* We will also need a dynamic reloc entry, unless this
		 is a JMP_TBL reloc produced by linking PIC compiled
		 code, and we are not making a shared library.  */
	      if (info->shared || (h->flags & SUNOS_DEF_REGULAR) == 0)
		srel->size += RELOC_EXT_SIZE;
	    }

	  /* If we are creating a shared library, we need to copy over
	     any reloc other than a jump table reloc.  */
	  if (info->shared && r_type != RELOC_JMP_TBL)
	    srel->size += RELOC_EXT_SIZE;
	}
    }

  return TRUE;
}

/* Scan the relocs for an input section.  */

static bfd_boolean
sunos_scan_relocs (struct bfd_link_info *info,
		   bfd *abfd,
		   asection *sec,
		   bfd_size_type rel_size)
{
  void * relocs;
  void * free_relocs = NULL;

  if (rel_size == 0)
    return TRUE;

  if (! info->keep_memory)
    relocs = free_relocs = bfd_malloc (rel_size);
  else
    {
      struct aout_section_data_struct *n;
      bfd_size_type amt = sizeof (struct aout_section_data_struct);

      n = bfd_alloc (abfd, amt);
      if (n == NULL)
	relocs = NULL;
      else
	{
	  set_aout_section_data (sec, n);
	  relocs = bfd_malloc (rel_size);
	  aout_section_data (sec)->relocs = relocs;
	}
    }
  if (relocs == NULL)
    return FALSE;

  if (bfd_seek (abfd, sec->rel_filepos, SEEK_SET) != 0
      || bfd_bread (relocs, rel_size, abfd) != rel_size)
    goto error_return;

  if (obj_reloc_entry_size (abfd) == RELOC_STD_SIZE)
    {
      if (! sunos_scan_std_relocs (info, abfd, sec,
				   (struct reloc_std_external *) relocs,
				   rel_size))
	goto error_return;
    }
  else
    {
      if (! sunos_scan_ext_relocs (info, abfd, sec,
				   (struct reloc_ext_external *) relocs,
				   rel_size))
	goto error_return;
    }

  if (free_relocs != NULL)
    free (free_relocs);

  return TRUE;

 error_return:
  if (free_relocs != NULL)
    free (free_relocs);
  return FALSE;
}

/* Build the hash table of dynamic symbols, and to mark as written all
   symbols from dynamic objects which we do not plan to write out.  */

static bfd_boolean
sunos_scan_dynamic_symbol (struct sunos_link_hash_entry *h, void * data)
{
  struct bfd_link_info *info = (struct bfd_link_info *) data;

  if (h->root.root.type == bfd_link_hash_warning)
    h = (struct sunos_link_hash_entry *) h->root.root.u.i.link;

  /* Set the written flag for symbols we do not want to write out as
     part of the regular symbol table.  This is all symbols which are
     not defined in a regular object file.  For some reason symbols
     which are referenced by a regular object and defined by a dynamic
     object do not seem to show up in the regular symbol table.  It is
     possible for a symbol to have only SUNOS_REF_REGULAR set here, it
     is an undefined symbol which was turned into a common symbol
     because it was found in an archive object which was not included
     in the link.  */
  if ((h->flags & SUNOS_DEF_REGULAR) == 0
      && (h->flags & SUNOS_DEF_DYNAMIC) != 0
      && strcmp (h->root.root.root.string, "__DYNAMIC") != 0)
    h->root.written = TRUE;

  /* If this symbol is defined by a dynamic object and referenced by a
     regular object, see whether we gave it a reasonable value while
     scanning the relocs.  */
  if ((h->flags & SUNOS_DEF_REGULAR) == 0
      && (h->flags & SUNOS_DEF_DYNAMIC) != 0
      && (h->flags & SUNOS_REF_REGULAR) != 0)
    {
      if ((h->root.root.type == bfd_link_hash_defined
	   || h->root.root.type == bfd_link_hash_defweak)
	  && ((h->root.root.u.def.section->owner->flags & DYNAMIC) != 0)
	  && h->root.root.u.def.section->output_section == NULL)
	{
	  bfd *sub;

	  /* This symbol is currently defined in a dynamic section
	     which is not being put into the output file.  This
	     implies that there is no reloc against the symbol.  I'm
	     not sure why this case would ever occur.  In any case, we
	     change the symbol to be undefined.  */
	  sub = h->root.root.u.def.section->owner;
	  h->root.root.type = bfd_link_hash_undefined;
	  h->root.root.u.undef.abfd = sub;
	}
    }

  /* If this symbol is defined or referenced by a regular file, add it
     to the dynamic symbols.  */
  if ((h->flags & (SUNOS_DEF_REGULAR | SUNOS_REF_REGULAR)) != 0)
    {
      asection *s;
      size_t len;
      bfd_byte *contents;
      unsigned char *name;
      unsigned long hash;
      bfd *dynobj;

      BFD_ASSERT (h->dynindx == -2);

      dynobj = sunos_hash_table (info)->dynobj;

      h->dynindx = sunos_hash_table (info)->dynsymcount;
      ++sunos_hash_table (info)->dynsymcount;

      len = strlen (h->root.root.root.string);

      /* We don't bother to construct a BFD hash table for the strings
	 which are the names of the dynamic symbols.  Using a hash
	 table for the regular symbols is beneficial, because the
	 regular symbols includes the debugging symbols, which have
	 long names and are often duplicated in several object files.
	 There are no debugging symbols in the dynamic symbols.  */
      s = bfd_get_section_by_name (dynobj, ".dynstr");
      BFD_ASSERT (s != NULL);
      contents = bfd_realloc (s->contents, s->size + len + 1);
      if (contents == NULL)
	return FALSE;
      s->contents = contents;

      h->dynstr_index = s->size;
      strcpy ((char *) contents + s->size, h->root.root.root.string);
      s->size += len + 1;

      /* Add it to the dynamic hash table.  */
      name = (unsigned char *) h->root.root.root.string;
      hash = 0;
      while (*name != '\0')
	hash = (hash << 1) + *name++;
      hash &= 0x7fffffff;
      hash %= sunos_hash_table (info)->bucketcount;

      s = bfd_get_section_by_name (dynobj, ".hash");
      BFD_ASSERT (s != NULL);

      if (GET_SWORD (dynobj, s->contents + hash * HASH_ENTRY_SIZE) == -1)
	PUT_WORD (dynobj, h->dynindx, s->contents + hash * HASH_ENTRY_SIZE);
      else
	{
	  bfd_vma next;

	  next = GET_WORD (dynobj,
			   (s->contents
			    + hash * HASH_ENTRY_SIZE
			    + BYTES_IN_WORD));
	  PUT_WORD (dynobj, s->size / HASH_ENTRY_SIZE,
		    s->contents + hash * HASH_ENTRY_SIZE + BYTES_IN_WORD);
	  PUT_WORD (dynobj, h->dynindx, s->contents + s->size);
	  PUT_WORD (dynobj, next, s->contents + s->size + BYTES_IN_WORD);
	  s->size += HASH_ENTRY_SIZE;
	}
    }

  return TRUE;
}

/* Set up the sizes and contents of the dynamic sections created in
   sunos_add_dynamic_symbols.  This is called by the SunOS linker
   emulation before_allocation routine.  We must set the sizes of the
   sections before the linker sets the addresses of the various
   sections.  This unfortunately requires reading all the relocs so
   that we can work out which ones need to become dynamic relocs.  If
   info->keep_memory is TRUE, we keep the relocs in memory; otherwise,
   we discard them, and will read them again later.  */

bfd_boolean
bfd_sunos_size_dynamic_sections (bfd *output_bfd,
				 struct bfd_link_info *info,
				 asection **sdynptr,
				 asection **sneedptr,
				 asection **srulesptr)
{
  bfd *dynobj;
  bfd_size_type dynsymcount;
  struct sunos_link_hash_entry *h;
  asection *s;
  size_t bucketcount;
  bfd_size_type hashalloc;
  size_t i;
  bfd *sub;

  *sdynptr = NULL;
  *sneedptr = NULL;
  *srulesptr = NULL;

  if (info->relocatable)
    return TRUE;

  if (output_bfd->xvec != &MY(vec))
    return TRUE;

  /* Look through all the input BFD's and read their relocs.  It would
     be better if we didn't have to do this, but there is no other way
     to determine the number of dynamic relocs we need, and, more
     importantly, there is no other way to know which symbols should
     get an entry in the procedure linkage table.  */
  for (sub = info->input_bfds; sub != NULL; sub = sub->link_next)
    {
      if ((sub->flags & DYNAMIC) == 0
	  && sub->xvec == output_bfd->xvec)
	{
	  if (! sunos_scan_relocs (info, sub, obj_textsec (sub),
				   exec_hdr (sub)->a_trsize)
	      || ! sunos_scan_relocs (info, sub, obj_datasec (sub),
				      exec_hdr (sub)->a_drsize))
	    return FALSE;
	}
    }

  dynobj = sunos_hash_table (info)->dynobj;
  dynsymcount = sunos_hash_table (info)->dynsymcount;

  /* If there were no dynamic objects in the link, and we don't need
     to build a global offset table, there is nothing to do here.  */
  if (! sunos_hash_table (info)->dynamic_sections_needed
      && ! sunos_hash_table (info)->got_needed)
    return TRUE;

  /* If __GLOBAL_OFFSET_TABLE_ was mentioned, define it.  */
  h = sunos_link_hash_lookup (sunos_hash_table (info),
			      "__GLOBAL_OFFSET_TABLE_", FALSE, FALSE, FALSE);
  if (h != NULL && (h->flags & SUNOS_REF_REGULAR) != 0)
    {
      h->flags |= SUNOS_DEF_REGULAR;
      if (h->dynindx == -1)
	{
	  ++sunos_hash_table (info)->dynsymcount;
	  h->dynindx = -2;
	}
      h->root.root.type = bfd_link_hash_defined;
      h->root.root.u.def.section = bfd_get_section_by_name (dynobj, ".got");

      /* If the .got section is more than 0x1000 bytes, we set
	 __GLOBAL_OFFSET_TABLE_ to be 0x1000 bytes into the section,
	 so that 13 bit relocations have a greater chance of working.  */
      s = bfd_get_section_by_name (dynobj, ".got");
      BFD_ASSERT (s != NULL);
      if (s->size >= 0x1000)
	h->root.root.u.def.value = 0x1000;
      else
	h->root.root.u.def.value = 0;

      sunos_hash_table (info)->got_base = h->root.root.u.def.value;
    }

  /* If there are any shared objects in the link, then we need to set
     up the dynamic linking information.  */
  if (sunos_hash_table (info)->dynamic_sections_needed)
    {
      *sdynptr = bfd_get_section_by_name (dynobj, ".dynamic");

      /* The .dynamic section is always the same size.  */
      s = *sdynptr;
      BFD_ASSERT (s != NULL);
      s->size = (sizeof (struct external_sun4_dynamic)
		      + EXTERNAL_SUN4_DYNAMIC_DEBUGGER_SIZE
		      + sizeof (struct external_sun4_dynamic_link));

      /* Set the size of the .dynsym and .hash sections.  We counted
	 the number of dynamic symbols as we read the input files.  We
	 will build the dynamic symbol table (.dynsym) and the hash
	 table (.hash) when we build the final symbol table, because
	 until then we do not know the correct value to give the
	 symbols.  We build the dynamic symbol string table (.dynstr)
	 in a traversal of the symbol table using
	 sunos_scan_dynamic_symbol.  */
      s = bfd_get_section_by_name (dynobj, ".dynsym");
      BFD_ASSERT (s != NULL);
      s->size = dynsymcount * sizeof (struct external_nlist);
      s->contents = bfd_alloc (output_bfd, s->size);
      if (s->contents == NULL && s->size != 0)
	return FALSE;

      /* The number of buckets is just the number of symbols divided
	 by four.  To compute the final size of the hash table, we
	 must actually compute the hash table.  Normally we need
	 exactly as many entries in the hash table as there are
	 dynamic symbols, but if some of the buckets are not used we
	 will need additional entries.  In the worst case, every
	 symbol will hash to the same bucket, and we will need
	 BUCKETCOUNT - 1 extra entries.  */
      if (dynsymcount >= 4)
	bucketcount = dynsymcount / 4;
      else if (dynsymcount > 0)
	bucketcount = dynsymcount;
      else
	bucketcount = 1;
      s = bfd_get_section_by_name (dynobj, ".hash");
      BFD_ASSERT (s != NULL);
      hashalloc = (dynsymcount + bucketcount - 1) * HASH_ENTRY_SIZE;
      s->contents = bfd_zalloc (dynobj, hashalloc);
      if (s->contents == NULL && dynsymcount > 0)
	return FALSE;
      for (i = 0; i < bucketcount; i++)
	PUT_WORD (output_bfd, (bfd_vma) -1, s->contents + i * HASH_ENTRY_SIZE);
      s->size = bucketcount * HASH_ENTRY_SIZE;

      sunos_hash_table (info)->bucketcount = bucketcount;

      /* Scan all the symbols, place them in the dynamic symbol table,
	 and build the dynamic hash table.  We reuse dynsymcount as a
	 counter for the number of symbols we have added so far.  */
      sunos_hash_table (info)->dynsymcount = 0;
      sunos_link_hash_traverse (sunos_hash_table (info),
				sunos_scan_dynamic_symbol,
				(void *) info);
      BFD_ASSERT (sunos_hash_table (info)->dynsymcount == dynsymcount);

      /* The SunOS native linker seems to align the total size of the
	 symbol strings to a multiple of 8.  I don't know if this is
	 important, but it can't hurt much.  */
      s = bfd_get_section_by_name (dynobj, ".dynstr");
      BFD_ASSERT (s != NULL);
      if ((s->size & 7) != 0)
	{
	  bfd_size_type add;
	  bfd_byte *contents;

	  add = 8 - (s->size & 7);
	  contents = bfd_realloc (s->contents, s->size + add);
	  if (contents == NULL)
	    return FALSE;
	  memset (contents + s->size, 0, (size_t) add);
	  s->contents = contents;
	  s->size += add;
	}
    }

  /* Now that we have worked out the sizes of the procedure linkage
     table and the dynamic relocs, allocate storage for them.  */
  s = bfd_get_section_by_name (dynobj, ".plt");
  BFD_ASSERT (s != NULL);
  if (s->size != 0)
    {
      s->contents = bfd_alloc (dynobj, s->size);
      if (s->contents == NULL)
	return FALSE;

      /* Fill in the first entry in the table.  */
      switch (bfd_get_arch (dynobj))
	{
	case bfd_arch_sparc:
	  memcpy (s->contents, sparc_plt_first_entry, SPARC_PLT_ENTRY_SIZE);
	  break;

	case bfd_arch_m68k:
	  memcpy (s->contents, m68k_plt_first_entry, M68K_PLT_ENTRY_SIZE);
	  break;

	default:
	  abort ();
	}
    }

  s = bfd_get_section_by_name (dynobj, ".dynrel");
  if (s->size != 0)
    {
      s->contents = bfd_alloc (dynobj, s->size);
      if (s->contents == NULL)
	return FALSE;
    }
  /* We use the reloc_count field to keep track of how many of the
     relocs we have output so far.  */
  s->reloc_count = 0;

  /* Make space for the global offset table.  */
  s = bfd_get_section_by_name (dynobj, ".got");
  s->contents = bfd_alloc (dynobj, s->size);
  if (s->contents == NULL)
    return FALSE;

  *sneedptr = bfd_get_section_by_name (dynobj, ".need");
  *srulesptr = bfd_get_section_by_name (dynobj, ".rules");

  return TRUE;
}

/* Link a dynamic object.  We actually don't have anything to do at
   this point.  This entry point exists to prevent the regular linker
   code from doing anything with the object.  */

static bfd_boolean
sunos_link_dynamic_object (struct bfd_link_info *info ATTRIBUTE_UNUSED,
			   bfd *abfd ATTRIBUTE_UNUSED)
{
  return TRUE;
}

/* Write out a dynamic symbol.  This is called by the final traversal
   over the symbol table.  */

static bfd_boolean
sunos_write_dynamic_symbol (bfd *output_bfd,
			    struct bfd_link_info *info,
			    struct aout_link_hash_entry *harg)
{
  struct sunos_link_hash_entry *h = (struct sunos_link_hash_entry *) harg;
  int type;
  bfd_vma val;
  asection *s;
  struct external_nlist *outsym;

  /* If this symbol is in the procedure linkage table, fill in the
     table entry.  */
  if (h->plt_offset != 0)
    {
      bfd *dynobj;
      asection *splt;
      bfd_byte *p;
      bfd_vma r_address;

      dynobj = sunos_hash_table (info)->dynobj;
      splt = bfd_get_section_by_name (dynobj, ".plt");
      p = splt->contents + h->plt_offset;

      s = bfd_get_section_by_name (dynobj, ".dynrel");

      r_address = (splt->output_section->vma
		   + splt->output_offset
		   + h->plt_offset);

      switch (bfd_get_arch (output_bfd))
	{
	case bfd_arch_sparc:
	  if (info->shared || (h->flags & SUNOS_DEF_REGULAR) == 0)
	    {
	      bfd_put_32 (output_bfd, SPARC_PLT_ENTRY_WORD0, p);
	      bfd_put_32 (output_bfd,
			  (SPARC_PLT_ENTRY_WORD1
			   + (((- (h->plt_offset + 4) >> 2)
			       & 0x3fffffff))),
			  p + 4);
	      bfd_put_32 (output_bfd, SPARC_PLT_ENTRY_WORD2 + s->reloc_count,
			  p + 8);
	    }
	  else
	    {
	      val = (h->root.root.u.def.section->output_section->vma
		     + h->root.root.u.def.section->output_offset
		     + h->root.root.u.def.value);
	      bfd_put_32 (output_bfd,
			  SPARC_PLT_PIC_WORD0 + ((val >> 10) & 0x3fffff),
			  p);
	      bfd_put_32 (output_bfd,
			  SPARC_PLT_PIC_WORD1 + (val & 0x3ff),
			  p + 4);
	      bfd_put_32 (output_bfd, SPARC_PLT_PIC_WORD2, p + 8);
	    }
	  break;

	case bfd_arch_m68k:
	  if (! info->shared && (h->flags & SUNOS_DEF_REGULAR) != 0)
	    abort ();
	  bfd_put_16 (output_bfd, M68K_PLT_ENTRY_WORD0, p);
	  bfd_put_32 (output_bfd, (- (h->plt_offset + 2)), p + 2);
	  bfd_put_16 (output_bfd, (bfd_vma) s->reloc_count, p + 6);
	  r_address += 2;
	  break;

	default:
	  abort ();
	}

      /* We also need to add a jump table reloc, unless this is the
	 result of a JMP_TBL reloc from PIC compiled code.  */
      if (info->shared || (h->flags & SUNOS_DEF_REGULAR) == 0)
	{
	  BFD_ASSERT (h->dynindx >= 0);
	  BFD_ASSERT (s->reloc_count * obj_reloc_entry_size (dynobj)
		      < s->size);
	  p = s->contents + s->reloc_count * obj_reloc_entry_size (output_bfd);
	  if (obj_reloc_entry_size (output_bfd) == RELOC_STD_SIZE)
	    {
	      struct reloc_std_external *srel;

	      srel = (struct reloc_std_external *) p;
	      PUT_WORD (output_bfd, r_address, srel->r_address);
	      if (bfd_header_big_endian (output_bfd))
		{
		  srel->r_index[0] = (bfd_byte) (h->dynindx >> 16);
		  srel->r_index[1] = (bfd_byte) (h->dynindx >> 8);
		  srel->r_index[2] = (bfd_byte) (h->dynindx);
		  srel->r_type[0] = (RELOC_STD_BITS_EXTERN_BIG
				     | RELOC_STD_BITS_JMPTABLE_BIG);
		}
	      else
		{
		  srel->r_index[2] = (bfd_byte) (h->dynindx >> 16);
		  srel->r_index[1] = (bfd_byte) (h->dynindx >> 8);
		  srel->r_index[0] = (bfd_byte)h->dynindx;
		  srel->r_type[0] = (RELOC_STD_BITS_EXTERN_LITTLE
				     | RELOC_STD_BITS_JMPTABLE_LITTLE);
		}
	    }
	  else
	    {
	      struct reloc_ext_external *erel;

	      erel = (struct reloc_ext_external *) p;
	      PUT_WORD (output_bfd, r_address, erel->r_address);
	      if (bfd_header_big_endian (output_bfd))
		{
		  erel->r_index[0] = (bfd_byte) (h->dynindx >> 16);
		  erel->r_index[1] = (bfd_byte) (h->dynindx >> 8);
		  erel->r_index[2] = (bfd_byte)h->dynindx;
		  erel->r_type[0] =
		    (RELOC_EXT_BITS_EXTERN_BIG
		     | (RELOC_JMP_SLOT << RELOC_EXT_BITS_TYPE_SH_BIG));
		}
	      else
		{
		  erel->r_index[2] = (bfd_byte) (h->dynindx >> 16);
		  erel->r_index[1] = (bfd_byte) (h->dynindx >> 8);
		  erel->r_index[0] = (bfd_byte)h->dynindx;
		  erel->r_type[0] =
		    (RELOC_EXT_BITS_EXTERN_LITTLE
		     | (RELOC_JMP_SLOT << RELOC_EXT_BITS_TYPE_SH_LITTLE));
		}
	      PUT_WORD (output_bfd, (bfd_vma) 0, erel->r_addend);
	    }

	  ++s->reloc_count;
	}
    }

  /* If this is not a dynamic symbol, we don't have to do anything
     else.  We only check this after handling the PLT entry, because
     we can have a PLT entry for a nondynamic symbol when linking PIC
     compiled code from a regular object.  */
  if (h->dynindx < 0)
    return TRUE;

  switch (h->root.root.type)
    {
    default:
    case bfd_link_hash_new:
      abort ();
      /* Avoid variable not initialized warnings.  */
      return TRUE;
    case bfd_link_hash_undefined:
      type = N_UNDF | N_EXT;
      val = 0;
      break;
    case bfd_link_hash_defined:
    case bfd_link_hash_defweak:
      {
	asection *sec;
	asection *output_section;

	sec = h->root.root.u.def.section;
	output_section = sec->output_section;
	BFD_ASSERT (bfd_is_abs_section (output_section)
		    || output_section->owner == output_bfd);
	if (h->plt_offset != 0
	    && (h->flags & SUNOS_DEF_REGULAR) == 0)
	  {
	    type = N_UNDF | N_EXT;
	    val = 0;
	  }
	else
	  {
	    if (output_section == obj_textsec (output_bfd))
	      type = (h->root.root.type == bfd_link_hash_defined
		      ? N_TEXT
		      : N_WEAKT);
	    else if (output_section == obj_datasec (output_bfd))
	      type = (h->root.root.type == bfd_link_hash_defined
		      ? N_DATA
		      : N_WEAKD);
	    else if (output_section == obj_bsssec (output_bfd))
	      type = (h->root.root.type == bfd_link_hash_defined
		      ? N_BSS
		      : N_WEAKB);
	    else
	      type = (h->root.root.type == bfd_link_hash_defined
		      ? N_ABS
		      : N_WEAKA);
	    type |= N_EXT;
	    val = (h->root.root.u.def.value
		   + output_section->vma
		   + sec->output_offset);
	  }
      }
      break;
    case bfd_link_hash_common:
      type = N_UNDF | N_EXT;
      val = h->root.root.u.c.size;
      break;
    case bfd_link_hash_undefweak:
      type = N_WEAKU;
      val = 0;
      break;
    case bfd_link_hash_indirect:
    case bfd_link_hash_warning:
      /* FIXME: Ignore these for now.  The circumstances under which
	 they should be written out are not clear to me.  */
      return TRUE;
    }

  s = bfd_get_section_by_name (sunos_hash_table (info)->dynobj, ".dynsym");
  BFD_ASSERT (s != NULL);
  outsym = ((struct external_nlist *)
	    (s->contents + h->dynindx * EXTERNAL_NLIST_SIZE));

  H_PUT_8 (output_bfd, type, outsym->e_type);
  H_PUT_8 (output_bfd, 0, outsym->e_other);

  /* FIXME: The native linker doesn't use 0 for desc.  It seems to use
     one less than the desc value in the shared library, although that
     seems unlikely.  */
  H_PUT_16 (output_bfd, 0, outsym->e_desc);

  PUT_WORD (output_bfd, h->dynstr_index, outsym->e_strx);
  PUT_WORD (output_bfd, val, outsym->e_value);

  return TRUE;
}

/* This is called for each reloc against an external symbol.  If this
   is a reloc which are are going to copy as a dynamic reloc, then
   copy it over, and tell the caller to not bother processing this
   reloc.  */

static bfd_boolean
sunos_check_dynamic_reloc (struct bfd_link_info *info,
			   bfd *input_bfd,
			   asection *input_section,
			   struct aout_link_hash_entry *harg,
			   void * reloc,
			   bfd_byte *contents ATTRIBUTE_UNUSED,
			   bfd_boolean *skip,
			   bfd_vma *relocationp)
{
  struct sunos_link_hash_entry *h = (struct sunos_link_hash_entry *) harg;
  bfd *dynobj;
  bfd_boolean baserel;
  bfd_boolean jmptbl;
  bfd_boolean pcrel;
  asection *s;
  bfd_byte *p;
  long indx;

  *skip = FALSE;

  dynobj = sunos_hash_table (info)->dynobj;

  if (h != NULL
      && h->plt_offset != 0
      && (info->shared
	  || (h->flags & SUNOS_DEF_REGULAR) == 0))
    {
      asection *splt;

      /* Redirect the relocation to the PLT entry.  */
      splt = bfd_get_section_by_name (dynobj, ".plt");
      *relocationp = (splt->output_section->vma
		      + splt->output_offset
		      + h->plt_offset);
    }

  if (obj_reloc_entry_size (input_bfd) == RELOC_STD_SIZE)
    {
      struct reloc_std_external *srel;

      srel = (struct reloc_std_external *) reloc;
      if (bfd_header_big_endian (input_bfd))
	{
	  baserel = (0 != (srel->r_type[0] & RELOC_STD_BITS_BASEREL_BIG));
	  jmptbl = (0 != (srel->r_type[0] & RELOC_STD_BITS_JMPTABLE_BIG));
	  pcrel = (0 != (srel->r_type[0] & RELOC_STD_BITS_PCREL_BIG));
	}
      else
	{
	  baserel = (0 != (srel->r_type[0] & RELOC_STD_BITS_BASEREL_LITTLE));
	  jmptbl = (0 != (srel->r_type[0] & RELOC_STD_BITS_JMPTABLE_LITTLE));
	  pcrel = (0 != (srel->r_type[0] & RELOC_STD_BITS_PCREL_LITTLE));
	}
    }
  else
    {
      struct reloc_ext_external *erel;
      int r_type;

      erel = (struct reloc_ext_external *) reloc;
      if (bfd_header_big_endian (input_bfd))
	r_type = ((erel->r_type[0] & RELOC_EXT_BITS_TYPE_BIG)
		  >> RELOC_EXT_BITS_TYPE_SH_BIG);
      else
	r_type = ((erel->r_type[0] & RELOC_EXT_BITS_TYPE_LITTLE)
		  >> RELOC_EXT_BITS_TYPE_SH_LITTLE);
      baserel = (r_type == RELOC_BASE10
		 || r_type == RELOC_BASE13
		 || r_type == RELOC_BASE22);
      jmptbl = r_type == RELOC_JMP_TBL;
      pcrel = (r_type == RELOC_DISP8
	       || r_type == RELOC_DISP16
	       || r_type == RELOC_DISP32
	       || r_type == RELOC_WDISP30
	       || r_type == RELOC_WDISP22);
      /* We don't consider the PC10 and PC22 types to be PC relative,
	 because they are pcrel_offset.  */
    }

  if (baserel)
    {
      bfd_vma *got_offsetp;
      asection *sgot;

      if (h != NULL)
	got_offsetp = &h->got_offset;
      else if (adata (input_bfd).local_got_offsets == NULL)
	got_offsetp = NULL;
      else
	{
	  struct reloc_std_external *srel;
	  int r_index;

	  srel = (struct reloc_std_external *) reloc;
	  if (obj_reloc_entry_size (input_bfd) == RELOC_STD_SIZE)
	    {
	      if (bfd_header_big_endian (input_bfd))
		r_index = ((srel->r_index[0] << 16)
			   | (srel->r_index[1] << 8)
			   | srel->r_index[2]);
	      else
		r_index = ((srel->r_index[2] << 16)
			   | (srel->r_index[1] << 8)
			   | srel->r_index[0]);
	    }
	  else
	    {
	      struct reloc_ext_external *erel;

	      erel = (struct reloc_ext_external *) reloc;
	      if (bfd_header_big_endian (input_bfd))
		r_index = ((erel->r_index[0] << 16)
			   | (erel->r_index[1] << 8)
			   | erel->r_index[2]);
	      else
		r_index = ((erel->r_index[2] << 16)
			   | (erel->r_index[1] << 8)
			   | erel->r_index[0]);
	    }

	  got_offsetp = adata (input_bfd).local_got_offsets + r_index;
	}

      BFD_ASSERT (got_offsetp != NULL && *got_offsetp != 0);

      sgot = bfd_get_section_by_name (dynobj, ".got");

      /* We set the least significant bit to indicate whether we have
	 already initialized the GOT entry.  */
      if ((*got_offsetp & 1) == 0)
	{
	  if (h == NULL
	      || (! info->shared
		  && ((h->flags & SUNOS_DEF_DYNAMIC) == 0
		      || (h->flags & SUNOS_DEF_REGULAR) != 0)))
	    PUT_WORD (dynobj, *relocationp, sgot->contents + *got_offsetp);
	  else
	    PUT_WORD (dynobj, 0, sgot->contents + *got_offsetp);

	  if (info->shared
	      || (h != NULL
		  && (h->flags & SUNOS_DEF_DYNAMIC) != 0
		  && (h->flags & SUNOS_DEF_REGULAR) == 0))
	    {
	      /* We need to create a GLOB_DAT or 32 reloc to tell the
		 dynamic linker to fill in this entry in the table.  */

	      s = bfd_get_section_by_name (dynobj, ".dynrel");
	      BFD_ASSERT (s != NULL);
	      BFD_ASSERT (s->reloc_count * obj_reloc_entry_size (dynobj)
			  < s->size);

	      p = (s->contents
		   + s->reloc_count * obj_reloc_entry_size (dynobj));

	      if (h != NULL)
		indx = h->dynindx;
	      else
		indx = 0;

	      if (obj_reloc_entry_size (dynobj) == RELOC_STD_SIZE)
		{
		  struct reloc_std_external *srel;

		  srel = (struct reloc_std_external *) p;
		  PUT_WORD (dynobj,
			    (*got_offsetp
			     + sgot->output_section->vma
			     + sgot->output_offset),
			    srel->r_address);
		  if (bfd_header_big_endian (dynobj))
		    {
		      srel->r_index[0] = (bfd_byte) (indx >> 16);
		      srel->r_index[1] = (bfd_byte) (indx >> 8);
		      srel->r_index[2] = (bfd_byte)indx;
		      if (h == NULL)
			srel->r_type[0] = 2 << RELOC_STD_BITS_LENGTH_SH_BIG;
		      else
			srel->r_type[0] =
			  (RELOC_STD_BITS_EXTERN_BIG
			   | RELOC_STD_BITS_BASEREL_BIG
			   | RELOC_STD_BITS_RELATIVE_BIG
			   | (2 << RELOC_STD_BITS_LENGTH_SH_BIG));
		    }
		  else
		    {
		      srel->r_index[2] = (bfd_byte) (indx >> 16);
		      srel->r_index[1] = (bfd_byte) (indx >> 8);
		      srel->r_index[0] = (bfd_byte)indx;
		      if (h == NULL)
			srel->r_type[0] = 2 << RELOC_STD_BITS_LENGTH_SH_LITTLE;
		      else
			srel->r_type[0] =
			  (RELOC_STD_BITS_EXTERN_LITTLE
			   | RELOC_STD_BITS_BASEREL_LITTLE
			   | RELOC_STD_BITS_RELATIVE_LITTLE
			   | (2 << RELOC_STD_BITS_LENGTH_SH_LITTLE));
		    }
		}
	      else
		{
		  struct reloc_ext_external *erel;

		  erel = (struct reloc_ext_external *) p;
		  PUT_WORD (dynobj,
			    (*got_offsetp
			     + sgot->output_section->vma
			     + sgot->output_offset),
			    erel->r_address);
		  if (bfd_header_big_endian (dynobj))
		    {
		      erel->r_index[0] = (bfd_byte) (indx >> 16);
		      erel->r_index[1] = (bfd_byte) (indx >> 8);
		      erel->r_index[2] = (bfd_byte)indx;
		      if (h == NULL)
			erel->r_type[0] =
			  RELOC_32 << RELOC_EXT_BITS_TYPE_SH_BIG;
		      else
			erel->r_type[0] =
			  (RELOC_EXT_BITS_EXTERN_BIG
			   | (RELOC_GLOB_DAT << RELOC_EXT_BITS_TYPE_SH_BIG));
		    }
		  else
		    {
		      erel->r_index[2] = (bfd_byte) (indx >> 16);
		      erel->r_index[1] = (bfd_byte) (indx >> 8);
		      erel->r_index[0] = (bfd_byte)indx;
		      if (h == NULL)
			erel->r_type[0] =
			  RELOC_32 << RELOC_EXT_BITS_TYPE_SH_LITTLE;
		      else
			erel->r_type[0] =
			  (RELOC_EXT_BITS_EXTERN_LITTLE
			   | (RELOC_GLOB_DAT
			      << RELOC_EXT_BITS_TYPE_SH_LITTLE));
		    }
		  PUT_WORD (dynobj, 0, erel->r_addend);
		}

	      ++s->reloc_count;
	    }

	  *got_offsetp |= 1;
	}

      *relocationp = (sgot->vma
		      + (*got_offsetp &~ (bfd_vma) 1)
		      - sunos_hash_table (info)->got_base);

      /* There is nothing else to do for a base relative reloc.  */
      return TRUE;
    }

  if (! sunos_hash_table (info)->dynamic_sections_needed)
    return TRUE;
  if (! info->shared)
    {
      if (h == NULL
	  || h->dynindx == -1
	  || h->root.root.type != bfd_link_hash_undefined
	  || (h->flags & SUNOS_DEF_REGULAR) != 0
	  || (h->flags & SUNOS_DEF_DYNAMIC) == 0
	  || (h->root.root.u.undef.abfd->flags & DYNAMIC) == 0)
	return TRUE;
    }
  else
    {
      if (h != NULL
	  && (h->dynindx == -1
	      || jmptbl
	      || strcmp (h->root.root.root.string,
			 "__GLOBAL_OFFSET_TABLE_") == 0))
	return TRUE;
    }

  /* It looks like this is a reloc we are supposed to copy.  */

  s = bfd_get_section_by_name (dynobj, ".dynrel");
  BFD_ASSERT (s != NULL);
  BFD_ASSERT (s->reloc_count * obj_reloc_entry_size (dynobj) < s->size);

  p = s->contents + s->reloc_count * obj_reloc_entry_size (dynobj);

  /* Copy the reloc over.  */
  memcpy (p, reloc, obj_reloc_entry_size (dynobj));

  if (h != NULL)
    indx = h->dynindx;
  else
    indx = 0;

  /* Adjust the address and symbol index.  */
  if (obj_reloc_entry_size (dynobj) == RELOC_STD_SIZE)
    {
      struct reloc_std_external *srel;

      srel = (struct reloc_std_external *) p;
      PUT_WORD (dynobj,
		(GET_WORD (dynobj, srel->r_address)
		 + input_section->output_section->vma
		 + input_section->output_offset),
		srel->r_address);
      if (bfd_header_big_endian (dynobj))
	{
	  srel->r_index[0] = (bfd_byte) (indx >> 16);
	  srel->r_index[1] = (bfd_byte) (indx >> 8);
	  srel->r_index[2] = (bfd_byte)indx;
	}
      else
	{
	  srel->r_index[2] = (bfd_byte) (indx >> 16);
	  srel->r_index[1] = (bfd_byte) (indx >> 8);
	  srel->r_index[0] = (bfd_byte)indx;
	}
      /* FIXME: We may have to change the addend for a PC relative
	 reloc.  */
    }
  else
    {
      struct reloc_ext_external *erel;

      erel = (struct reloc_ext_external *) p;
      PUT_WORD (dynobj,
		(GET_WORD (dynobj, erel->r_address)
		 + input_section->output_section->vma
		 + input_section->output_offset),
		erel->r_address);
      if (bfd_header_big_endian (dynobj))
	{
	  erel->r_index[0] = (bfd_byte) (indx >> 16);
	  erel->r_index[1] = (bfd_byte) (indx >> 8);
	  erel->r_index[2] = (bfd_byte)indx;
	}
      else
	{
	  erel->r_index[2] = (bfd_byte) (indx >> 16);
	  erel->r_index[1] = (bfd_byte) (indx >> 8);
	  erel->r_index[0] = (bfd_byte)indx;
	}
      if (pcrel && h != NULL)
	{
	  /* Adjust the addend for the change in address.  */
	  PUT_WORD (dynobj,
		    (GET_WORD (dynobj, erel->r_addend)
		     - (input_section->output_section->vma
			+ input_section->output_offset
			- input_section->vma)),
		    erel->r_addend);
	}
    }

  ++s->reloc_count;

  if (h != NULL)
    *skip = TRUE;

  return TRUE;
}

/* Finish up the dynamic linking information.  */

static bfd_boolean
sunos_finish_dynamic_link (bfd *abfd, struct bfd_link_info *info)
{
  bfd *dynobj;
  asection *o;
  asection *s;
  asection *sdyn;

  if (! sunos_hash_table (info)->dynamic_sections_needed
      && ! sunos_hash_table (info)->got_needed)
    return TRUE;

  dynobj = sunos_hash_table (info)->dynobj;

  sdyn = bfd_get_section_by_name (dynobj, ".dynamic");
  BFD_ASSERT (sdyn != NULL);

  /* Finish up the .need section.  The linker emulation code filled it
     in, but with offsets from the start of the section instead of
     real addresses.  Now that we know the section location, we can
     fill in the final values.  */
  s = bfd_get_section_by_name (dynobj, ".need");
  if (s != NULL && s->size != 0)
    {
      file_ptr filepos;
      bfd_byte *p;

      filepos = s->output_section->filepos + s->output_offset;
      p = s->contents;
      while (1)
	{
	  bfd_vma val;

	  PUT_WORD (dynobj, GET_WORD (dynobj, p) + filepos, p);
	  val = GET_WORD (dynobj, p + 12);
	  if (val == 0)
	    break;
	  PUT_WORD (dynobj, val + filepos, p + 12);
	  p += 16;
	}
    }

  /* The first entry in the .got section is the address of the
     dynamic information, unless this is a shared library.  */
  s = bfd_get_section_by_name (dynobj, ".got");
  BFD_ASSERT (s != NULL);
  if (info->shared || sdyn->size == 0)
    PUT_WORD (dynobj, 0, s->contents);
  else
    PUT_WORD (dynobj, sdyn->output_section->vma + sdyn->output_offset,
	      s->contents);

  for (o = dynobj->sections; o != NULL; o = o->next)
    {
      if ((o->flags & SEC_HAS_CONTENTS) != 0
	  && o->contents != NULL)
	{
	  BFD_ASSERT (o->output_section != NULL
		      && o->output_section->owner == abfd);
	  if (! bfd_set_section_contents (abfd, o->output_section,
					  o->contents,
					  (file_ptr) o->output_offset,
					  o->size))
	    return FALSE;
	}
    }

  if (sdyn->size > 0)
    {
      struct external_sun4_dynamic esd;
      struct external_sun4_dynamic_link esdl;
      file_ptr pos;

      /* Finish up the dynamic link information.  */
      PUT_WORD (dynobj, (bfd_vma) 3, esd.ld_version);
      PUT_WORD (dynobj,
		sdyn->output_section->vma + sdyn->output_offset + sizeof esd,
		esd.ldd);
      PUT_WORD (dynobj,
		(sdyn->output_section->vma
		 + sdyn->output_offset
		 + sizeof esd
		 + EXTERNAL_SUN4_DYNAMIC_DEBUGGER_SIZE),
		esd.ld);

      if (! bfd_set_section_contents (abfd, sdyn->output_section, &esd,
				      (file_ptr) sdyn->output_offset,
				      (bfd_size_type) sizeof esd))
	return FALSE;

      PUT_WORD (dynobj, (bfd_vma) 0, esdl.ld_loaded);

      s = bfd_get_section_by_name (dynobj, ".need");
      if (s == NULL || s->size == 0)
	PUT_WORD (dynobj, (bfd_vma) 0, esdl.ld_need);
      else
	PUT_WORD (dynobj, s->output_section->filepos + s->output_offset,
		  esdl.ld_need);

      s = bfd_get_section_by_name (dynobj, ".rules");
      if (s == NULL || s->size == 0)
	PUT_WORD (dynobj, (bfd_vma) 0, esdl.ld_rules);
      else
	PUT_WORD (dynobj, s->output_section->filepos + s->output_offset,
		  esdl.ld_rules);

      s = bfd_get_section_by_name (dynobj, ".got");
      BFD_ASSERT (s != NULL);
      PUT_WORD (dynobj, s->output_section->vma + s->output_offset,
		esdl.ld_got);

      s = bfd_get_section_by_name (dynobj, ".plt");
      BFD_ASSERT (s != NULL);
      PUT_WORD (dynobj, s->output_section->vma + s->output_offset,
		esdl.ld_plt);
      PUT_WORD (dynobj, s->size, esdl.ld_plt_sz);

      s = bfd_get_section_by_name (dynobj, ".dynrel");
      BFD_ASSERT (s != NULL);
      BFD_ASSERT (s->reloc_count * obj_reloc_entry_size (dynobj)
		  == s->size);
      PUT_WORD (dynobj, s->output_section->filepos + s->output_offset,
		esdl.ld_rel);

      s = bfd_get_section_by_name (dynobj, ".hash");
      BFD_ASSERT (s != NULL);
      PUT_WORD (dynobj, s->output_section->filepos + s->output_offset,
		esdl.ld_hash);

      s = bfd_get_section_by_name (dynobj, ".dynsym");
      BFD_ASSERT (s != NULL);
      PUT_WORD (dynobj, s->output_section->filepos + s->output_offset,
		esdl.ld_stab);

      PUT_WORD (dynobj, (bfd_vma) 0, esdl.ld_stab_hash);

      PUT_WORD (dynobj, (bfd_vma) sunos_hash_table (info)->bucketcount,
		esdl.ld_buckets);

      s = bfd_get_section_by_name (dynobj, ".dynstr");
      BFD_ASSERT (s != NULL);
      PUT_WORD (dynobj, s->output_section->filepos + s->output_offset,
		esdl.ld_symbols);
      PUT_WORD (dynobj, s->size, esdl.ld_symb_size);

      /* The size of the text area is the size of the .text section
	 rounded up to a page boundary.  FIXME: Should the page size be
	 conditional on something?  */
      PUT_WORD (dynobj,
		BFD_ALIGN (obj_textsec (abfd)->size, 0x2000),
		esdl.ld_text);

      pos = sdyn->output_offset;
      pos += sizeof esd + EXTERNAL_SUN4_DYNAMIC_DEBUGGER_SIZE;
      if (! bfd_set_section_contents (abfd, sdyn->output_section, &esdl,
				      pos, (bfd_size_type) sizeof esdl))
	return FALSE;

      abfd->flags |= DYNAMIC;
    }

  return TRUE;
}
