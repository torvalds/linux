/* BFD backend for MIPS BSD (a.out) binaries.
   Copyright 1993, 1994, 1995, 1997, 1998, 1999, 2000, 2001, 2002, 2003,
   2007 Free Software Foundation, Inc.
   Written by Ralph Campbell.

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

/* #define ENTRY_CAN_BE_ZERO */
#define N_HEADER_IN_TEXT(x) 1
#define N_SHARED_LIB(x) 0
#define N_TXTADDR(x) \
    (N_MAGIC(x) != ZMAGIC ? (x).a_entry :	/* object file or NMAGIC */\
	    TEXT_START_ADDR + EXEC_BYTES_SIZE	/* no padding */\
    )
#define N_DATADDR(x) (N_TXTADDR(x)+N_TXTSIZE(x))
#define TEXT_START_ADDR 4096
#define TARGET_PAGE_SIZE 4096
#define SEGMENT_SIZE TARGET_PAGE_SIZE
#define DEFAULT_ARCH bfd_arch_mips
#define MACHTYPE_OK(mtype) ((mtype) == M_UNKNOWN \
			    || (mtype) == M_MIPS1 || (mtype) == M_MIPS2)
#define MY_symbol_leading_char '\0'

/* Do not "beautify" the CONCAT* macro args.  Traditional C will not
   remove whitespace added here, and thus will fail to concatenate
   the tokens.  */
#define MY(OP) CONCAT2 (mipsbsd_,OP)

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "libaout.h"

#define SET_ARCH_MACH(ABFD, EXEC) \
  MY(set_arch_mach) (ABFD, N_MACHTYPE (EXEC)); \
  MY(choose_reloc_size) (ABFD);
static void MY(set_arch_mach) PARAMS ((bfd *abfd, unsigned long machtype));
static void MY(choose_reloc_size) PARAMS ((bfd *abfd));

#define MY_write_object_contents MY(write_object_contents)
static bfd_boolean MY(write_object_contents) PARAMS ((bfd *abfd));

/* We can't use MY(x) here because it leads to a recursive call to CONCAT2
   when expanded inside JUMP_TABLE.  */
#define MY_bfd_reloc_type_lookup mipsbsd_reloc_type_lookup
#define MY_bfd_reloc_name_lookup mipsbsd_reloc_name_lookup
#define MY_canonicalize_reloc mipsbsd_canonicalize_reloc

#define MY_bfd_link_hash_table_create _bfd_generic_link_hash_table_create
#define MY_bfd_link_add_symbols _bfd_generic_link_add_symbols
#define MY_final_link_callback unused
#define MY_bfd_final_link _bfd_generic_final_link

#define MY_backend_data &MY(backend_data)
#define MY_BFD_TARGET

#include "aout-target.h"

static bfd_reloc_status_type mips_fix_jmp_addr
  PARAMS ((bfd *, arelent *, struct bfd_symbol *, PTR, asection *,
	   bfd *, char **));

long MY(canonicalize_reloc) PARAMS ((bfd *, sec_ptr, arelent **, asymbol **));

static void
MY(set_arch_mach) (abfd, machtype)
     bfd *abfd;
     unsigned long machtype;
{
  enum bfd_architecture arch;
  unsigned int machine;

  /* Determine the architecture and machine type of the object file.  */
  switch (machtype)
    {
    case M_MIPS1:
      arch = bfd_arch_mips;
      machine = bfd_mach_mips3000;
      break;

    case M_MIPS2:
      arch = bfd_arch_mips;
      machine = bfd_mach_mips4000;
      break;

    default:
      arch = bfd_arch_obscure;
      machine = 0;
      break;
    }

  bfd_set_arch_mach (abfd, arch, machine);
}

/* Determine the size of a relocation entry, based on the architecture */
static void
MY (choose_reloc_size) (abfd)
     bfd *abfd;
{
  switch (bfd_get_arch (abfd))
    {
    case bfd_arch_sparc:
    case bfd_arch_mips:
      obj_reloc_entry_size (abfd) = RELOC_EXT_SIZE;
      break;
    default:
      obj_reloc_entry_size (abfd) = RELOC_STD_SIZE;
      break;
    }
}

/* Write an object file in BSD a.out format.
  Section contents have already been written.  We write the
  file header, symbols, and relocation.  */

static bfd_boolean
MY (write_object_contents) (abfd)
     bfd *abfd;
{
  struct external_exec exec_bytes;
  struct internal_exec *execp = exec_hdr (abfd);

  /* Magic number, maestro, please!  */
  switch (bfd_get_arch (abfd))
    {
    case bfd_arch_m68k:
      switch (bfd_get_mach (abfd))
	{
	case bfd_mach_m68010:
	  N_SET_MACHTYPE (*execp, M_68010);
	  break;
	default:
	case bfd_mach_m68020:
	  N_SET_MACHTYPE (*execp, M_68020);
	  break;
	}
      break;
    case bfd_arch_sparc:
      N_SET_MACHTYPE (*execp, M_SPARC);
      break;
    case bfd_arch_i386:
      N_SET_MACHTYPE (*execp, M_386);
      break;
    case bfd_arch_mips:
      switch (bfd_get_mach (abfd))
	{
	case bfd_mach_mips4000:
	case bfd_mach_mips6000:
	  N_SET_MACHTYPE (*execp, M_MIPS2);
	  break;
	default:
	  N_SET_MACHTYPE (*execp, M_MIPS1);
	  break;
	}
      break;
    default:
      N_SET_MACHTYPE (*execp, M_UNKNOWN);
    }

  MY (choose_reloc_size) (abfd);

  WRITE_HEADERS (abfd, execp);

  return TRUE;
}

/* MIPS relocation types.  */
#define MIPS_RELOC_32		0
#define MIPS_RELOC_JMP		1
#define MIPS_RELOC_WDISP16	2
#define MIPS_RELOC_HI16		3
#define MIPS_RELOC_HI16_S	4
#define MIPS_RELOC_LO16		5

/* This is only called when performing a BFD_RELOC_MIPS_JMP relocation.
   The jump destination address is formed from the upper 4 bits of the
   "current" program counter concatenated with the jump instruction's
   26 bit field and two trailing zeros.
   If the destination address is not in the same segment as the "current"
   program counter, then we need to signal an error.  */

static bfd_reloc_status_type
mips_fix_jmp_addr (abfd, reloc_entry, symbol, data, input_section, output_bfd,
		   error_message)
     bfd *abfd ATTRIBUTE_UNUSED;
     arelent *reloc_entry;
     struct bfd_symbol *symbol;
     PTR data ATTRIBUTE_UNUSED;
     asection *input_section;
     bfd *output_bfd;
     char **error_message ATTRIBUTE_UNUSED;
{
  bfd_vma relocation, pc;

  /* If this is a partial relocation, just continue.  */
  if (output_bfd != (bfd *)NULL)
    return bfd_reloc_continue;

  /* If this is an undefined symbol, return error */
  if (bfd_is_und_section (symbol->section)
      && (symbol->flags & BSF_WEAK) == 0)
    return bfd_reloc_undefined;

  /* Work out which section the relocation is targeted at and the
     initial relocation command value.  */
  if (bfd_is_com_section (symbol->section))
    relocation = 0;
  else
    relocation = symbol->value;

  relocation += symbol->section->output_section->vma;
  relocation += symbol->section->output_offset;
  relocation += reloc_entry->addend;

  pc = input_section->output_section->vma + input_section->output_offset +
    reloc_entry->address + 4;

  if ((relocation & 0xF0000000) != (pc & 0xF0000000))
    return bfd_reloc_overflow;

  return bfd_reloc_continue;
}

/* This is only called when performing a BFD_RELOC_HI16_S relocation.
   We need to see if bit 15 is set in the result. If it is, we add
   0x10000 and continue normally. This will compensate for the sign extension
   when the low bits are added at run time.  */

static bfd_reloc_status_type
mips_fix_hi16_s PARAMS ((bfd *, arelent *, asymbol *, PTR,
			 asection *, bfd *, char **));

static bfd_reloc_status_type
mips_fix_hi16_s (abfd, reloc_entry, symbol, data, input_section,
		 output_bfd, error_message)
     bfd *abfd ATTRIBUTE_UNUSED;
     arelent *reloc_entry;
     asymbol *symbol;
     PTR data ATTRIBUTE_UNUSED;
     asection *input_section ATTRIBUTE_UNUSED;
     bfd *output_bfd;
     char **error_message ATTRIBUTE_UNUSED;
{
  bfd_vma relocation;

  /* If this is a partial relocation, just continue.  */
  if (output_bfd != (bfd *)NULL)
    return bfd_reloc_continue;

  /* If this is an undefined symbol, return error.  */
  if (bfd_is_und_section (symbol->section)
      && (symbol->flags & BSF_WEAK) == 0)
    return bfd_reloc_undefined;

  /* Work out which section the relocation is targeted at and the
     initial relocation command value.  */
  if (bfd_is_com_section (symbol->section))
    relocation = 0;
  else
    relocation = symbol->value;

  relocation += symbol->section->output_section->vma;
  relocation += symbol->section->output_offset;
  relocation += reloc_entry->addend;

  if (relocation & 0x8000)
    reloc_entry->addend += 0x10000;

  return bfd_reloc_continue;
}

static reloc_howto_type mips_howto_table_ext[] = {
  {MIPS_RELOC_32,      0, 2, 32, FALSE, 0,  complain_overflow_bitfield, 0,
	"32",       FALSE, 0, 0xffffffff, FALSE},
  {MIPS_RELOC_JMP,     2, 2, 26, FALSE, 0, complain_overflow_dont,
	mips_fix_jmp_addr,
	"MIPS_JMP", FALSE, 0, 0x03ffffff, FALSE},
  {MIPS_RELOC_WDISP16, 2, 2, 16, TRUE,  0, complain_overflow_signed, 0,
	"WDISP16",  FALSE, 0, 0x0000ffff, FALSE},
  {MIPS_RELOC_HI16,   16, 2, 16, FALSE, 0, complain_overflow_bitfield, 0,
	"HI16",     FALSE, 0, 0x0000ffff, FALSE},
  {MIPS_RELOC_HI16_S, 16, 2, 16, FALSE, 0, complain_overflow_bitfield,
        mips_fix_hi16_s,
        "HI16_S",   FALSE, 0, 0x0000ffff, FALSE},
  {MIPS_RELOC_LO16,    0, 2, 16, FALSE, 0, complain_overflow_dont, 0,
	"LO16",     FALSE, 0, 0x0000ffff, FALSE},
};

static reloc_howto_type *
MY(reloc_type_lookup) (bfd *abfd, bfd_reloc_code_real_type code)
{

  if (bfd_get_arch (abfd) != bfd_arch_mips)
    return 0;

  switch (code)
    {
    case BFD_RELOC_CTOR:
    case BFD_RELOC_32:
      return (&mips_howto_table_ext[MIPS_RELOC_32]);
    case BFD_RELOC_MIPS_JMP:
      return (&mips_howto_table_ext[MIPS_RELOC_JMP]);
    case BFD_RELOC_16_PCREL_S2:
      return (&mips_howto_table_ext[MIPS_RELOC_WDISP16]);
    case BFD_RELOC_HI16:
      return (&mips_howto_table_ext[MIPS_RELOC_HI16]);
    case BFD_RELOC_HI16_S:
      return (&mips_howto_table_ext[MIPS_RELOC_HI16_S]);
    case BFD_RELOC_LO16:
      return (&mips_howto_table_ext[MIPS_RELOC_LO16]);
    default:
      return 0;
    }
}

static reloc_howto_type *
MY(reloc_name_lookup) (bfd *abfd ATTRIBUTE_UNUSED,
			     const char *r_name)
{
  unsigned int i;

  for (i = 0;
       i < sizeof (mips_howto_table_ext) / sizeof (mips_howto_table_ext[0]);
       i++)
    if (mips_howto_table_ext[i].name != NULL
	&& strcasecmp (mips_howto_table_ext[i].name, r_name) == 0)
      return &mips_howto_table_ext[i];

  return NULL;
}

/* This is just like the standard aoutx.h version but we need to do our
   own mapping of external reloc type values to howto entries.  */
long
MY(canonicalize_reloc) (abfd, section, relptr, symbols)
      bfd *abfd;
      sec_ptr section;
      arelent **relptr;
      asymbol **symbols;
{
  arelent *tblptr = section->relocation;
  unsigned int count, c;
  extern reloc_howto_type NAME(aout,ext_howto_table)[];

  /* If we have already read in the relocation table, return the values.  */
  if (section->flags & SEC_CONSTRUCTOR)
    {
      arelent_chain *chain = section->constructor_chain;

      for (count = 0; count < section->reloc_count; count++)
	{
	  *relptr++ = &chain->relent;
	  chain = chain->next;
	}
      *relptr = 0;
      return section->reloc_count;
    }

  if (tblptr && section->reloc_count)
    {
      for (count = 0; count++ < section->reloc_count;)
	*relptr++ = tblptr++;
      *relptr = 0;
      return section->reloc_count;
    }

  if (!NAME(aout,slurp_reloc_table) (abfd, section, symbols))
    return -1;
  tblptr = section->relocation;

  /* fix up howto entries.  */
  for (count = 0; count++ < section->reloc_count;)
    {
      c = tblptr->howto - NAME(aout,ext_howto_table);
      tblptr->howto = &mips_howto_table_ext[c];

      *relptr++ = tblptr++;
    }
  *relptr = 0;
  return section->reloc_count;
}

static const struct aout_backend_data MY(backend_data) = {
  0,				/* zmagic contiguous */
  1,				/* text incl header */
  0,				/* entry is text address */
  0,				/* exec_hdr_flags */
  TARGET_PAGE_SIZE,			/* text vma */
  MY_set_sizes,
  0,				/* text size includes exec header */
  0,				/* add_dynamic_symbols */
  0,				/* add_one_symbol */
  0,				/* link_dynamic_object */
  0,				/* write_dynamic_symbol */
  0,				/* check_dynamic_reloc */
  0				/* finish_dynamic_link */
};

extern const bfd_target aout_mips_big_vec;

const bfd_target aout_mips_little_vec =
  {
    "a.out-mips-little",		/* name */
    bfd_target_aout_flavour,
    BFD_ENDIAN_LITTLE,		/* target byte order (little) */
    BFD_ENDIAN_LITTLE,		/* target headers byte order (little) */
    (HAS_RELOC | EXEC_P |		/* object flags */
     HAS_LINENO | HAS_DEBUG |
     HAS_SYMS | HAS_LOCALS | WP_TEXT | D_PAGED),
    (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_RELOC | SEC_CODE | SEC_DATA),
    MY_symbol_leading_char,
    ' ',				/* ar_pad_char */
    15,				/* ar_max_namelen */
    bfd_getl64, bfd_getl_signed_64, bfd_putl64,
    bfd_getl32, bfd_getl_signed_32, bfd_putl32,
    bfd_getl16, bfd_getl_signed_16, bfd_putl16, /* data */
    bfd_getl64, bfd_getl_signed_64, bfd_putl64,
    bfd_getl32, bfd_getl_signed_32, bfd_putl32,
    bfd_getl16, bfd_getl_signed_16, bfd_putl16, /* hdrs */
    {_bfd_dummy_target, MY_object_p, /* bfd_check_format */
     bfd_generic_archive_p, MY_core_file_p},
    {bfd_false, MY_mkobject,	/* bfd_set_format */
     _bfd_generic_mkarchive, bfd_false},
    {bfd_false, MY_write_object_contents, /* bfd_write_contents */
     _bfd_write_archive_contents, bfd_false},

    BFD_JUMP_TABLE_GENERIC (MY),
    BFD_JUMP_TABLE_COPY (MY),
    BFD_JUMP_TABLE_CORE (MY),
    BFD_JUMP_TABLE_ARCHIVE (MY),
    BFD_JUMP_TABLE_SYMBOLS (MY),
    BFD_JUMP_TABLE_RELOCS (MY),
    BFD_JUMP_TABLE_WRITE (MY),
    BFD_JUMP_TABLE_LINK (MY),
    BFD_JUMP_TABLE_DYNAMIC (_bfd_nodynamic),

    & aout_mips_big_vec,

    (PTR) MY_backend_data
  };

const bfd_target aout_mips_big_vec =
  {
    "a.out-mips-big",		/* name */
    bfd_target_aout_flavour,
    BFD_ENDIAN_BIG,		/* target byte order (big) */
    BFD_ENDIAN_BIG,		/* target headers byte order (big) */
    (HAS_RELOC | EXEC_P |		/* object flags */
     HAS_LINENO | HAS_DEBUG |
     HAS_SYMS | HAS_LOCALS | WP_TEXT | D_PAGED),
    (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_RELOC | SEC_CODE | SEC_DATA),
    MY_symbol_leading_char,
    ' ',				/* ar_pad_char */
    15,				/* ar_max_namelen */
    bfd_getb64, bfd_getb_signed_64, bfd_putb64,
    bfd_getb32, bfd_getb_signed_32, bfd_putb32,
    bfd_getb16, bfd_getb_signed_16, bfd_putb16, /* data */
    bfd_getb64, bfd_getb_signed_64, bfd_putb64,
    bfd_getb32, bfd_getb_signed_32, bfd_putb32,
    bfd_getb16, bfd_getb_signed_16, bfd_putb16, /* hdrs */
    {_bfd_dummy_target, MY_object_p, /* bfd_check_format */
     bfd_generic_archive_p, MY_core_file_p},
    {bfd_false, MY_mkobject,	/* bfd_set_format */
     _bfd_generic_mkarchive, bfd_false},
    {bfd_false, MY_write_object_contents, /* bfd_write_contents */
     _bfd_write_archive_contents, bfd_false},

    BFD_JUMP_TABLE_GENERIC (MY),
    BFD_JUMP_TABLE_COPY (MY),
    BFD_JUMP_TABLE_CORE (MY),
    BFD_JUMP_TABLE_ARCHIVE (MY),
    BFD_JUMP_TABLE_SYMBOLS (MY),
    BFD_JUMP_TABLE_RELOCS (MY),
    BFD_JUMP_TABLE_WRITE (MY),
    BFD_JUMP_TABLE_LINK (MY),
    BFD_JUMP_TABLE_DYNAMIC (_bfd_nodynamic),

    & aout_mips_little_vec,

    (PTR) MY_backend_data
  };
