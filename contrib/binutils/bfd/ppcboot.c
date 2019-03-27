/* BFD back-end for PPCbug boot records.
   Copyright 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2006,
   2007 Free Software Foundation, Inc.
   Written by Michael Meissner, Cygnus Support, <meissner@cygnus.com>

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

/* This is a BFD backend which may be used to write PowerPCBug boot objects.
   It may only be used for output, not input.  The intention is that this may
   be used as an output format for objcopy in order to generate raw binary
   data.

   This is very simple.  The only complication is that the real data
   will start at some address X, and in some cases we will not want to
   include X zeroes just to get to that point.  Since the start
   address is not meaningful for this object file format, we use it
   instead to indicate the number of zeroes to skip at the start of
   the file.  objcopy cooperates by specially setting the start
   address to zero by default.  */

#include "sysdep.h"
#include "safe-ctype.h"
#include "bfd.h"
#include "libbfd.h"

/* PPCbug location structure */
typedef struct ppcboot_location {
  bfd_byte	ind;
  bfd_byte	head;
  bfd_byte	sector;
  bfd_byte	cylinder;
} ppcboot_location_t;

/* PPCbug partition table layout */
typedef struct ppcboot_partition {
  ppcboot_location_t	partition_begin;	/* partition begin */
  ppcboot_location_t	partition_end;		/* partition end */
  bfd_byte		sector_begin[4];	/* 32-bit start RBA (zero-based), little endian */
  bfd_byte		sector_length[4];	/* 32-bit RBA count (one-based), little endian */
} ppcboot_partition_t;

/* PPCbug boot layout.  */
typedef struct ppcboot_hdr {
  bfd_byte		pc_compatibility[446];	/* x86 instruction field */
  ppcboot_partition_t	partition[4];		/* partition information */
  bfd_byte		signature[2];		/* 0x55 and 0xaa */
  bfd_byte		entry_offset[4];	/* entry point offset, little endian */
  bfd_byte		length[4];		/* load image length, little endian */
  bfd_byte		flags;			/* flag field */
  bfd_byte		os_id;			/* OS_ID */
  char			partition_name[32];	/* partition name */
  bfd_byte		reserved1[470];		/* reserved */
}
#ifdef __GNUC__
  __attribute__ ((packed))
#endif
ppcboot_hdr_t;

/* Signature bytes for last 2 bytes of the 512 byte record */
#define SIGNATURE0 0x55
#define SIGNATURE1 0xaa

/* PowerPC boot type */
#define PPC_IND 0x41

/* Information needed for ppcboot header */
typedef struct ppcboot_data {
  ppcboot_hdr_t	header;				/* raw header */
  asection *sec;				/* single section */
} ppcboot_data_t;

/* Any bfd we create by reading a ppcboot file has three symbols:
   a start symbol, an end symbol, and an absolute length symbol.  */
#define PPCBOOT_SYMS 3

static bfd_boolean ppcboot_mkobject PARAMS ((bfd *));
static const bfd_target *ppcboot_object_p PARAMS ((bfd *));
static bfd_boolean ppcboot_set_arch_mach
  PARAMS ((bfd *, enum bfd_architecture, unsigned long));
static bfd_boolean ppcboot_get_section_contents
  PARAMS ((bfd *, asection *, PTR, file_ptr, bfd_size_type));
static long ppcboot_get_symtab_upper_bound PARAMS ((bfd *));
static char *mangle_name PARAMS ((bfd *, char *));
static long ppcboot_canonicalize_symtab PARAMS ((bfd *, asymbol **));
static void ppcboot_get_symbol_info PARAMS ((bfd *, asymbol *, symbol_info *));
static bfd_boolean ppcboot_set_section_contents
  PARAMS ((bfd *, asection *, const PTR, file_ptr, bfd_size_type));
static bfd_boolean ppcboot_bfd_print_private_bfd_data PARAMS ((bfd *, PTR));

#define ppcboot_set_tdata(abfd, ptr) ((abfd)->tdata.any = (PTR) (ptr))
#define ppcboot_get_tdata(abfd) ((ppcboot_data_t *) ((abfd)->tdata.any))

/* Create a ppcboot object.  Invoked via bfd_set_format.  */

static bfd_boolean
ppcboot_mkobject (abfd)
     bfd *abfd;
{
  if (!ppcboot_get_tdata (abfd))
    {
      bfd_size_type amt = sizeof (ppcboot_data_t);
      ppcboot_set_tdata (abfd, bfd_zalloc (abfd, amt));
    }

  return TRUE;
}


/* Set the architecture to PowerPC */
static bfd_boolean
ppcboot_set_arch_mach (abfd, arch, machine)
     bfd *abfd;
     enum bfd_architecture arch;
     unsigned long machine;
{
  if (arch == bfd_arch_unknown)
    arch = bfd_arch_powerpc;

  else if (arch != bfd_arch_powerpc)
    return FALSE;

  return bfd_default_set_arch_mach (abfd, arch, machine);
}


/* Any file may be considered to be a ppcboot file, provided the target
   was not defaulted.  That is, it must be explicitly specified as
   being ppcboot.  */

static const bfd_target *
ppcboot_object_p (abfd)
     bfd *abfd;
{
  struct stat statbuf;
  asection *sec;
  ppcboot_hdr_t hdr;
  size_t i;
  ppcboot_data_t *tdata;
  flagword flags;

  BFD_ASSERT (sizeof (ppcboot_hdr_t) == 1024);

  if (abfd->target_defaulted)
    {
      bfd_set_error (bfd_error_wrong_format);
      return NULL;
    }

  /* Find the file size.  */
  if (bfd_stat (abfd, &statbuf) < 0)
    {
      bfd_set_error (bfd_error_system_call);
      return NULL;
    }

  if ((size_t) statbuf.st_size < sizeof (ppcboot_hdr_t))
    {
      bfd_set_error (bfd_error_wrong_format);
      return NULL;
    }

  if (bfd_bread ((PTR) &hdr, (bfd_size_type) sizeof (hdr), abfd)
      != sizeof (hdr))
    {
      if (bfd_get_error () != bfd_error_system_call)
	bfd_set_error (bfd_error_wrong_format);

      return NULL;
    }

  /* Now do some basic checks.  */
  for (i = 0; i < sizeof (hdr.pc_compatibility); i++)
    if (hdr.pc_compatibility[i])
      {
	bfd_set_error (bfd_error_wrong_format);
	return NULL;
      }

  if (hdr.signature[0] != SIGNATURE0 || hdr.signature[1] != SIGNATURE1)
    {
      bfd_set_error (bfd_error_wrong_format);
      return NULL;
    }

  if (hdr.partition[0].partition_end.ind != PPC_IND)
    {
      bfd_set_error (bfd_error_wrong_format);
      return NULL;
    }

  abfd->symcount = PPCBOOT_SYMS;

  /* One data section.  */
  flags = SEC_ALLOC | SEC_LOAD | SEC_DATA | SEC_CODE | SEC_HAS_CONTENTS;
  sec = bfd_make_section_with_flags (abfd, ".data", flags);
  if (sec == NULL)
    return NULL;
  sec->vma = 0;
  sec->size = statbuf.st_size - sizeof (ppcboot_hdr_t);
  sec->filepos = sizeof (ppcboot_hdr_t);

  ppcboot_mkobject (abfd);
  tdata = ppcboot_get_tdata (abfd);
  tdata->sec = sec;
  memcpy ((PTR) &tdata->header, (PTR) &hdr, sizeof (ppcboot_hdr_t));

  ppcboot_set_arch_mach (abfd, bfd_arch_powerpc, 0L);
  return abfd->xvec;
}

#define ppcboot_close_and_cleanup _bfd_generic_close_and_cleanup
#define ppcboot_bfd_free_cached_info _bfd_generic_bfd_free_cached_info
#define ppcboot_new_section_hook _bfd_generic_new_section_hook


/* Get contents of the only section.  */

static bfd_boolean
ppcboot_get_section_contents (abfd, section, location, offset, count)
     bfd *abfd;
     asection *section ATTRIBUTE_UNUSED;
     PTR location;
     file_ptr offset;
     bfd_size_type count;
{
  if (bfd_seek (abfd, offset + (file_ptr) sizeof (ppcboot_hdr_t), SEEK_SET) != 0
      || bfd_bread (location, count, abfd) != count)
    return FALSE;
  return TRUE;
}


/* Return the amount of memory needed to read the symbol table.  */

static long
ppcboot_get_symtab_upper_bound (abfd)
     bfd *abfd ATTRIBUTE_UNUSED;
{
  return (PPCBOOT_SYMS + 1) * sizeof (asymbol *);
}


/* Create a symbol name based on the bfd's filename.  */

static char *
mangle_name (abfd, suffix)
     bfd *abfd;
     char *suffix;
{
  bfd_size_type size;
  char *buf;
  char *p;

  size = (strlen (bfd_get_filename (abfd))
	  + strlen (suffix)
	  + sizeof "_ppcboot__");

  buf = (char *) bfd_alloc (abfd, size);
  if (buf == NULL)
    return "";

  sprintf (buf, "_ppcboot_%s_%s", bfd_get_filename (abfd), suffix);

  /* Change any non-alphanumeric characters to underscores.  */
  for (p = buf; *p; p++)
    if (! ISALNUM (*p))
      *p = '_';

  return buf;
}


/* Return the symbol table.  */

static long
ppcboot_canonicalize_symtab (abfd, alocation)
     bfd *abfd;
     asymbol **alocation;
{
  asection *sec = ppcboot_get_tdata (abfd)->sec;
  asymbol *syms;
  unsigned int i;
  bfd_size_type amt = PPCBOOT_SYMS * sizeof (asymbol);

  syms = (asymbol *) bfd_alloc (abfd, amt);
  if (syms == NULL)
    return FALSE;

  /* Start symbol.  */
  syms[0].the_bfd = abfd;
  syms[0].name = mangle_name (abfd, "start");
  syms[0].value = 0;
  syms[0].flags = BSF_GLOBAL;
  syms[0].section = sec;
  syms[0].udata.p = NULL;

  /* End symbol.  */
  syms[1].the_bfd = abfd;
  syms[1].name = mangle_name (abfd, "end");
  syms[1].value = sec->size;
  syms[1].flags = BSF_GLOBAL;
  syms[1].section = sec;
  syms[1].udata.p = NULL;

  /* Size symbol.  */
  syms[2].the_bfd = abfd;
  syms[2].name = mangle_name (abfd, "size");
  syms[2].value = sec->size;
  syms[2].flags = BSF_GLOBAL;
  syms[2].section = bfd_abs_section_ptr;
  syms[2].udata.p = NULL;

  for (i = 0; i < PPCBOOT_SYMS; i++)
    *alocation++ = syms++;
  *alocation = NULL;

  return PPCBOOT_SYMS;
}

#define ppcboot_make_empty_symbol _bfd_generic_make_empty_symbol
#define ppcboot_print_symbol _bfd_nosymbols_print_symbol

/* Get information about a symbol.  */

static void
ppcboot_get_symbol_info (ignore_abfd, symbol, ret)
     bfd *ignore_abfd ATTRIBUTE_UNUSED;
     asymbol *symbol;
     symbol_info *ret;
{
  bfd_symbol_info (symbol, ret);
}

#define ppcboot_bfd_is_target_special_symbol \
  ((bfd_boolean (*) (bfd *, asymbol *)) bfd_false)
#define ppcboot_bfd_is_local_label_name bfd_generic_is_local_label_name
#define ppcboot_get_lineno _bfd_nosymbols_get_lineno
#define ppcboot_find_nearest_line _bfd_nosymbols_find_nearest_line
#define ppcboot_find_inliner_info _bfd_nosymbols_find_inliner_info
#define ppcboot_bfd_make_debug_symbol _bfd_nosymbols_bfd_make_debug_symbol
#define ppcboot_read_minisymbols _bfd_generic_read_minisymbols
#define ppcboot_minisymbol_to_symbol _bfd_generic_minisymbol_to_symbol

/* Write section contents of a ppcboot file.  */

static bfd_boolean
ppcboot_set_section_contents (abfd, sec, data, offset, size)
     bfd *abfd;
     asection *sec;
     const PTR data;
     file_ptr offset;
     bfd_size_type size;
{
  if (! abfd->output_has_begun)
    {
      bfd_vma low;
      asection *s;

      /* The lowest section VMA sets the virtual address of the start
         of the file.  We use the set the file position of all the
         sections.  */
      low = abfd->sections->vma;
      for (s = abfd->sections->next; s != NULL; s = s->next)
	if (s->vma < low)
	  low = s->vma;

      for (s = abfd->sections; s != NULL; s = s->next)
	s->filepos = s->vma - low;

      abfd->output_has_begun = TRUE;
    }

  return _bfd_generic_set_section_contents (abfd, sec, data, offset, size);
}


static int
ppcboot_sizeof_headers (bfd *abfd ATTRIBUTE_UNUSED,
			struct bfd_link_info *info ATTRIBUTE_UNUSED)
{
  return sizeof (ppcboot_hdr_t);
}


/* Print out the program headers.  */

static bfd_boolean
ppcboot_bfd_print_private_bfd_data (abfd, farg)
     bfd *abfd;
     PTR farg;
{
  FILE *f = (FILE *)farg;
  ppcboot_data_t *tdata = ppcboot_get_tdata (abfd);
  long entry_offset = bfd_getl_signed_32 ((PTR) tdata->header.entry_offset);
  long length = bfd_getl_signed_32 ((PTR) tdata->header.length);
  int i;

  fprintf (f, _("\nppcboot header:\n"));
  fprintf (f, _("Entry offset        = 0x%.8lx (%ld)\n"), entry_offset, entry_offset);
  fprintf (f, _("Length              = 0x%.8lx (%ld)\n"), length, length);

  if (tdata->header.flags)
    fprintf (f, _("Flag field          = 0x%.2x\n"), tdata->header.flags);

  if (tdata->header.os_id)
    fprintf (f, "OS_ID               = 0x%.2x\n", tdata->header.os_id);

  if (tdata->header.partition_name[0])
    fprintf (f, _("Partition name      = \"%s\"\n"), tdata->header.partition_name);

  for (i = 0; i < 4; i++)
    {
      long sector_begin  = bfd_getl_signed_32 ((PTR) tdata->header.partition[i].sector_begin);
      long sector_length = bfd_getl_signed_32 ((PTR) tdata->header.partition[i].sector_length);

      /* Skip all 0 entries */
      if (!tdata->header.partition[i].partition_begin.ind
	  && !tdata->header.partition[i].partition_begin.head
	  && !tdata->header.partition[i].partition_begin.sector
	  && !tdata->header.partition[i].partition_begin.cylinder
	  && !tdata->header.partition[i].partition_end.ind
	  && !tdata->header.partition[i].partition_end.head
	  && !tdata->header.partition[i].partition_end.sector
	  && !tdata->header.partition[i].partition_end.cylinder
	  && !sector_begin && !sector_length)
	continue;

      fprintf (f, _("\nPartition[%d] start  = { 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x }\n"), i,
	       tdata->header.partition[i].partition_begin.ind,
	       tdata->header.partition[i].partition_begin.head,
	       tdata->header.partition[i].partition_begin.sector,
	       tdata->header.partition[i].partition_begin.cylinder);

      fprintf (f, _("Partition[%d] end    = { 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x }\n"), i,
	       tdata->header.partition[i].partition_end.ind,
	       tdata->header.partition[i].partition_end.head,
	       tdata->header.partition[i].partition_end.sector,
	       tdata->header.partition[i].partition_end.cylinder);

      fprintf (f, _("Partition[%d] sector = 0x%.8lx (%ld)\n"), i, sector_begin, sector_begin);
      fprintf (f, _("Partition[%d] length = 0x%.8lx (%ld)\n"), i, sector_length, sector_length);
    }

  fprintf (f, "\n");
  return TRUE;
}


#define ppcboot_bfd_get_relocated_section_contents \
  bfd_generic_get_relocated_section_contents
#define ppcboot_bfd_relax_section bfd_generic_relax_section
#define ppcboot_bfd_gc_sections bfd_generic_gc_sections
#define ppcboot_bfd_merge_sections bfd_generic_merge_sections
#define ppcboot_bfd_is_group_section bfd_generic_is_group_section
#define ppcboot_bfd_discard_group bfd_generic_discard_group
#define ppcboot_section_already_linked \
  _bfd_generic_section_already_linked
#define ppcboot_bfd_link_hash_table_create _bfd_generic_link_hash_table_create
#define ppcboot_bfd_link_hash_table_free _bfd_generic_link_hash_table_free
#define ppcboot_bfd_link_add_symbols _bfd_generic_link_add_symbols
#define ppcboot_bfd_link_just_syms _bfd_generic_link_just_syms
#define ppcboot_bfd_final_link _bfd_generic_final_link
#define ppcboot_bfd_link_split_section _bfd_generic_link_split_section
#define ppcboot_get_section_contents_in_window \
  _bfd_generic_get_section_contents_in_window

#define ppcboot_bfd_copy_private_bfd_data _bfd_generic_bfd_copy_private_bfd_data
#define ppcboot_bfd_merge_private_bfd_data _bfd_generic_bfd_merge_private_bfd_data
#define ppcboot_bfd_copy_private_section_data _bfd_generic_bfd_copy_private_section_data
#define ppcboot_bfd_copy_private_symbol_data _bfd_generic_bfd_copy_private_symbol_data
#define ppcboot_bfd_copy_private_header_data _bfd_generic_bfd_copy_private_header_data
#define ppcboot_bfd_set_private_flags _bfd_generic_bfd_set_private_flags
#define ppcboot_bfd_print_private_bfd_dat ppcboot_bfd_print_private_bfd_data

const bfd_target ppcboot_vec =
{
  "ppcboot",			/* name */
  bfd_target_unknown_flavour,	/* flavour */
  BFD_ENDIAN_BIG,		/* byteorder is big endian for code */
  BFD_ENDIAN_LITTLE,		/* header_byteorder */
  EXEC_P,			/* object_flags */
  (SEC_ALLOC | SEC_LOAD | SEC_READONLY | SEC_CODE | SEC_DATA
   | SEC_ROM | SEC_HAS_CONTENTS), /* section_flags */
  0,				/* symbol_leading_char */
  ' ',				/* ar_pad_char */
  16,				/* ar_max_namelen */
  bfd_getb64, bfd_getb_signed_64, bfd_putb64,
  bfd_getb32, bfd_getb_signed_32, bfd_putb32,
  bfd_getb16, bfd_getb_signed_16, bfd_putb16,	/* data */
  bfd_getl64, bfd_getl_signed_64, bfd_putl64,
  bfd_getl32, bfd_getl_signed_32, bfd_putl32,
  bfd_getl16, bfd_getl_signed_16, bfd_putl16,	/* hdrs */
  {				/* bfd_check_format */
    _bfd_dummy_target,
    ppcboot_object_p,		/* bfd_check_format */
    _bfd_dummy_target,
    _bfd_dummy_target,
  },
  {				/* bfd_set_format */
    bfd_false,
    ppcboot_mkobject,
    bfd_false,
    bfd_false,
  },
  {				/* bfd_write_contents */
    bfd_false,
    bfd_true,
    bfd_false,
    bfd_false,
  },

  BFD_JUMP_TABLE_GENERIC (ppcboot),
  BFD_JUMP_TABLE_COPY (ppcboot),
  BFD_JUMP_TABLE_CORE (_bfd_nocore),
  BFD_JUMP_TABLE_ARCHIVE (_bfd_noarchive),
  BFD_JUMP_TABLE_SYMBOLS (ppcboot),
  BFD_JUMP_TABLE_RELOCS (_bfd_norelocs),
  BFD_JUMP_TABLE_WRITE (ppcboot),
  BFD_JUMP_TABLE_LINK (ppcboot),
  BFD_JUMP_TABLE_DYNAMIC (_bfd_nodynamic),

  NULL,

  NULL
};
