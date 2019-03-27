/* BFD back-end for MIPS Extended-Coff files.
   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2007
   Free Software Foundation, Inc.
   Original version by Per Bothner.
   Full support added by Ian Lance Taylor, ian@cygnus.com.

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
#include "bfdlink.h"
#include "libbfd.h"
#include "coff/internal.h"
#include "coff/sym.h"
#include "coff/symconst.h"
#include "coff/ecoff.h"
#include "coff/mips.h"
#include "libcoff.h"
#include "libecoff.h"

/* Prototypes for static functions.  */

static bfd_boolean mips_ecoff_bad_format_hook
  PARAMS ((bfd *abfd, PTR filehdr));
static void mips_ecoff_swap_reloc_in
  PARAMS ((bfd *, PTR, struct internal_reloc *));
static void mips_ecoff_swap_reloc_out
  PARAMS ((bfd *, const struct internal_reloc *, PTR));
static void mips_adjust_reloc_in
  PARAMS ((bfd *, const struct internal_reloc *, arelent *));
static void mips_adjust_reloc_out
  PARAMS ((bfd *, const arelent *, struct internal_reloc *));
static bfd_reloc_status_type mips_generic_reloc
  PARAMS ((bfd *abfd, arelent *reloc, asymbol *symbol, PTR data,
	   asection *section, bfd *output_bfd, char **error));
static bfd_reloc_status_type mips_refhi_reloc
  PARAMS ((bfd *abfd, arelent *reloc, asymbol *symbol, PTR data,
	   asection *section, bfd *output_bfd, char **error));
static bfd_reloc_status_type mips_reflo_reloc
  PARAMS ((bfd *abfd, arelent *reloc, asymbol *symbol, PTR data,
	   asection *section, bfd *output_bfd, char **error));
static bfd_reloc_status_type mips_gprel_reloc
  PARAMS ((bfd *abfd, arelent *reloc, asymbol *symbol, PTR data,
	   asection *section, bfd *output_bfd, char **error));
static void mips_relocate_hi
  PARAMS ((struct internal_reloc *refhi, struct internal_reloc *reflo,
	   bfd *input_bfd, asection *input_section, bfd_byte *contents,
	   bfd_vma relocation));
static bfd_boolean mips_relocate_section
  PARAMS ((bfd *, struct bfd_link_info *, bfd *, asection *, bfd_byte *, PTR));
static reloc_howto_type *mips_bfd_reloc_type_lookup
  PARAMS ((bfd *, bfd_reloc_code_real_type));

/* ECOFF has COFF sections, but the debugging information is stored in
   a completely different format.  ECOFF targets use some of the
   swapping routines from coffswap.h, and some of the generic COFF
   routines in coffgen.c, but, unlike the real COFF targets, do not
   use coffcode.h itself.

   Get the generic COFF swapping routines, except for the reloc,
   symbol, and lineno ones.  Give them ECOFF names.  */
#define MIPSECOFF
#define NO_COFF_RELOCS
#define NO_COFF_SYMBOLS
#define NO_COFF_LINENOS
#define coff_swap_filehdr_in mips_ecoff_swap_filehdr_in
#define coff_swap_filehdr_out mips_ecoff_swap_filehdr_out
#define coff_swap_aouthdr_in mips_ecoff_swap_aouthdr_in
#define coff_swap_aouthdr_out mips_ecoff_swap_aouthdr_out
#define coff_swap_scnhdr_in mips_ecoff_swap_scnhdr_in
#define coff_swap_scnhdr_out mips_ecoff_swap_scnhdr_out
#include "coffswap.h"

/* Get the ECOFF swapping routines.  */
#define ECOFF_32
#include "ecoffswap.h"

/* How to process the various relocs types.  */

static reloc_howto_type mips_howto_table[] =
{
  /* Reloc type 0 is ignored.  The reloc reading code ensures that
     this is a reference to the .abs section, which will cause
     bfd_perform_relocation to do nothing.  */
  HOWTO (MIPS_R_IGNORE,	/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 8,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 0,			/* special_function */
	 "IGNORE",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* A 16 bit reference to a symbol, normally from a data section.  */
  HOWTO (MIPS_R_REFHALF,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 mips_generic_reloc,	/* special_function */
	 "REFHALF",		/* name */
	 TRUE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* A 32 bit reference to a symbol, normally from a data section.  */
  HOWTO (MIPS_R_REFWORD,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 mips_generic_reloc,	/* special_function */
	 "REFWORD",		/* name */
	 TRUE,			/* partial_inplace */
	 0xffffffff,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* A 26 bit absolute jump address.  */
  HOWTO (MIPS_R_JMPADDR,	/* type */
	 2,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 26,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 			/* This needs complex overflow
				   detection, because the upper four
				   bits must match the PC.  */
	 mips_generic_reloc,	/* special_function */
	 "JMPADDR",		/* name */
	 TRUE,			/* partial_inplace */
	 0x3ffffff,		/* src_mask */
	 0x3ffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* The high 16 bits of a symbol value.  Handled by the function
     mips_refhi_reloc.  */
  HOWTO (MIPS_R_REFHI,		/* type */
	 16,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 mips_refhi_reloc,	/* special_function */
	 "REFHI",		/* name */
	 TRUE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* The low 16 bits of a symbol value.  */
  HOWTO (MIPS_R_REFLO,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 mips_reflo_reloc,	/* special_function */
	 "REFLO",		/* name */
	 TRUE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* A reference to an offset from the gp register.  Handled by the
     function mips_gprel_reloc.  */
  HOWTO (MIPS_R_GPREL,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 mips_gprel_reloc,	/* special_function */
	 "GPREL",		/* name */
	 TRUE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* A reference to a literal using an offset from the gp register.
     Handled by the function mips_gprel_reloc.  */
  HOWTO (MIPS_R_LITERAL,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 mips_gprel_reloc,	/* special_function */
	 "LITERAL",		/* name */
	 TRUE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  EMPTY_HOWTO (8),
  EMPTY_HOWTO (9),
  EMPTY_HOWTO (10),
  EMPTY_HOWTO (11),

  /* FIXME: This relocation is used (internally only) to represent branches
     when assembling.  It should never appear in output files, and
     be removed.  (It used to be used for embedded-PIC support.)  */
  HOWTO (MIPS_R_PCREL16,	/* type */
	 2,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 mips_generic_reloc,	/* special_function */
	 "PCREL16",		/* name */
	 TRUE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 TRUE),			/* pcrel_offset */
};

#define MIPS_HOWTO_COUNT \
  (sizeof mips_howto_table / sizeof mips_howto_table[0])

/* See whether the magic number matches.  */

static bfd_boolean
mips_ecoff_bad_format_hook (abfd, filehdr)
     bfd *abfd;
     PTR filehdr;
{
  struct internal_filehdr *internal_f = (struct internal_filehdr *) filehdr;

  switch (internal_f->f_magic)
    {
    case MIPS_MAGIC_1:
      /* I don't know what endianness this implies.  */
      return TRUE;

    case MIPS_MAGIC_BIG:
    case MIPS_MAGIC_BIG2:
    case MIPS_MAGIC_BIG3:
      return bfd_big_endian (abfd);

    case MIPS_MAGIC_LITTLE:
    case MIPS_MAGIC_LITTLE2:
    case MIPS_MAGIC_LITTLE3:
      return bfd_little_endian (abfd);

    default:
      return FALSE;
    }
}

/* Reloc handling.  MIPS ECOFF relocs are packed into 8 bytes in
   external form.  They use a bit which indicates whether the symbol
   is external.  */

/* Swap a reloc in.  */

static void
mips_ecoff_swap_reloc_in (abfd, ext_ptr, intern)
     bfd *abfd;
     PTR ext_ptr;
     struct internal_reloc *intern;
{
  const RELOC *ext = (RELOC *) ext_ptr;

  intern->r_vaddr = H_GET_32 (abfd, ext->r_vaddr);
  if (bfd_header_big_endian (abfd))
    {
      intern->r_symndx = (((int) ext->r_bits[0]
			   << RELOC_BITS0_SYMNDX_SH_LEFT_BIG)
			  | ((int) ext->r_bits[1]
			     << RELOC_BITS1_SYMNDX_SH_LEFT_BIG)
			  | ((int) ext->r_bits[2]
			     << RELOC_BITS2_SYMNDX_SH_LEFT_BIG));
      intern->r_type = ((ext->r_bits[3] & RELOC_BITS3_TYPE_BIG)
			>> RELOC_BITS3_TYPE_SH_BIG);
      intern->r_extern = (ext->r_bits[3] & RELOC_BITS3_EXTERN_BIG) != 0;
    }
  else
    {
      intern->r_symndx = (((int) ext->r_bits[0]
			   << RELOC_BITS0_SYMNDX_SH_LEFT_LITTLE)
			  | ((int) ext->r_bits[1]
			     << RELOC_BITS1_SYMNDX_SH_LEFT_LITTLE)
			  | ((int) ext->r_bits[2]
			     << RELOC_BITS2_SYMNDX_SH_LEFT_LITTLE));
      intern->r_type = (((ext->r_bits[3] & RELOC_BITS3_TYPE_LITTLE)
			 >> RELOC_BITS3_TYPE_SH_LITTLE)
			| ((ext->r_bits[3] & RELOC_BITS3_TYPEHI_LITTLE)
			   << RELOC_BITS3_TYPEHI_SH_LITTLE));
      intern->r_extern = (ext->r_bits[3] & RELOC_BITS3_EXTERN_LITTLE) != 0;
    }
}

/* Swap a reloc out.  */

static void
mips_ecoff_swap_reloc_out (abfd, intern, dst)
     bfd *abfd;
     const struct internal_reloc *intern;
     PTR dst;
{
  RELOC *ext = (RELOC *) dst;
  long r_symndx;

  BFD_ASSERT (intern->r_extern
	      || (intern->r_symndx >= 0 && intern->r_symndx <= 12));

  r_symndx = intern->r_symndx;

  H_PUT_32 (abfd, intern->r_vaddr, ext->r_vaddr);
  if (bfd_header_big_endian (abfd))
    {
      ext->r_bits[0] = r_symndx >> RELOC_BITS0_SYMNDX_SH_LEFT_BIG;
      ext->r_bits[1] = r_symndx >> RELOC_BITS1_SYMNDX_SH_LEFT_BIG;
      ext->r_bits[2] = r_symndx >> RELOC_BITS2_SYMNDX_SH_LEFT_BIG;
      ext->r_bits[3] = (((intern->r_type << RELOC_BITS3_TYPE_SH_BIG)
			 & RELOC_BITS3_TYPE_BIG)
			| (intern->r_extern ? RELOC_BITS3_EXTERN_BIG : 0));
    }
  else
    {
      ext->r_bits[0] = r_symndx >> RELOC_BITS0_SYMNDX_SH_LEFT_LITTLE;
      ext->r_bits[1] = r_symndx >> RELOC_BITS1_SYMNDX_SH_LEFT_LITTLE;
      ext->r_bits[2] = r_symndx >> RELOC_BITS2_SYMNDX_SH_LEFT_LITTLE;
      ext->r_bits[3] = (((intern->r_type << RELOC_BITS3_TYPE_SH_LITTLE)
			 & RELOC_BITS3_TYPE_LITTLE)
			| ((intern->r_type >> RELOC_BITS3_TYPEHI_SH_LITTLE
			    & RELOC_BITS3_TYPEHI_LITTLE))
			| (intern->r_extern ? RELOC_BITS3_EXTERN_LITTLE : 0));
    }
}

/* Finish canonicalizing a reloc.  Part of this is generic to all
   ECOFF targets, and that part is in ecoff.c.  The rest is done in
   this backend routine.  It must fill in the howto field.  */

static void
mips_adjust_reloc_in (abfd, intern, rptr)
     bfd *abfd;
     const struct internal_reloc *intern;
     arelent *rptr;
{
  if (intern->r_type > MIPS_R_PCREL16)
    abort ();

  if (! intern->r_extern
      && (intern->r_type == MIPS_R_GPREL
	  || intern->r_type == MIPS_R_LITERAL))
    rptr->addend += ecoff_data (abfd)->gp;

  /* If the type is MIPS_R_IGNORE, make sure this is a reference to
     the absolute section so that the reloc is ignored.  */
  if (intern->r_type == MIPS_R_IGNORE)
    rptr->sym_ptr_ptr = bfd_abs_section_ptr->symbol_ptr_ptr;

  rptr->howto = &mips_howto_table[intern->r_type];
}

/* Make any adjustments needed to a reloc before writing it out.  None
   are needed for MIPS.  */

static void
mips_adjust_reloc_out (abfd, rel, intern)
     bfd *abfd ATTRIBUTE_UNUSED;
     const arelent *rel ATTRIBUTE_UNUSED;
     struct internal_reloc *intern ATTRIBUTE_UNUSED;
{
}

/* ECOFF relocs are either against external symbols, or against
   sections.  If we are producing relocatable output, and the reloc
   is against an external symbol, and nothing has given us any
   additional addend, the resulting reloc will also be against the
   same symbol.  In such a case, we don't want to change anything
   about the way the reloc is handled, since it will all be done at
   final link time.  Rather than put special case code into
   bfd_perform_relocation, all the reloc types use this howto
   function.  It just short circuits the reloc if producing
   relocatable output against an external symbol.  */

static bfd_reloc_status_type
mips_generic_reloc (abfd,
		    reloc_entry,
		    symbol,
		    data,
		    input_section,
		    output_bfd,
		    error_message)
     bfd *abfd ATTRIBUTE_UNUSED;
     arelent *reloc_entry;
     asymbol *symbol;
     PTR data ATTRIBUTE_UNUSED;
     asection *input_section;
     bfd *output_bfd;
     char **error_message ATTRIBUTE_UNUSED;
{
  if (output_bfd != (bfd *) NULL
      && (symbol->flags & BSF_SECTION_SYM) == 0
      && reloc_entry->addend == 0)
    {
      reloc_entry->address += input_section->output_offset;
      return bfd_reloc_ok;
    }

  return bfd_reloc_continue;
}

/* Do a REFHI relocation.  This has to be done in combination with a
   REFLO reloc, because there is a carry from the REFLO to the REFHI.
   Here we just save the information we need; we do the actual
   relocation when we see the REFLO.  MIPS ECOFF requires that the
   REFLO immediately follow the REFHI.  As a GNU extension, we permit
   an arbitrary number of HI relocs to be associated with a single LO
   reloc.  This extension permits gcc to output the HI and LO relocs
   itself.  */

struct mips_hi
{
  struct mips_hi *next;
  bfd_byte *addr;
  bfd_vma addend;
};

/* FIXME: This should not be a static variable.  */

static struct mips_hi *mips_refhi_list;

static bfd_reloc_status_type
mips_refhi_reloc (abfd,
		  reloc_entry,
		  symbol,
		  data,
		  input_section,
		  output_bfd,
		  error_message)
     bfd *abfd ATTRIBUTE_UNUSED;
     arelent *reloc_entry;
     asymbol *symbol;
     PTR data;
     asection *input_section;
     bfd *output_bfd;
     char **error_message ATTRIBUTE_UNUSED;
{
  bfd_reloc_status_type ret;
  bfd_vma relocation;
  struct mips_hi *n;

  /* If we're relocating, and this an external symbol, we don't want
     to change anything.  */
  if (output_bfd != (bfd *) NULL
      && (symbol->flags & BSF_SECTION_SYM) == 0
      && reloc_entry->addend == 0)
    {
      reloc_entry->address += input_section->output_offset;
      return bfd_reloc_ok;
    }

  ret = bfd_reloc_ok;
  if (bfd_is_und_section (symbol->section)
      && output_bfd == (bfd *) NULL)
    ret = bfd_reloc_undefined;

  if (bfd_is_com_section (symbol->section))
    relocation = 0;
  else
    relocation = symbol->value;

  relocation += symbol->section->output_section->vma;
  relocation += symbol->section->output_offset;
  relocation += reloc_entry->addend;

  if (reloc_entry->address > bfd_get_section_limit (abfd, input_section))
    return bfd_reloc_outofrange;

  /* Save the information, and let REFLO do the actual relocation.  */
  n = (struct mips_hi *) bfd_malloc ((bfd_size_type) sizeof *n);
  if (n == NULL)
    return bfd_reloc_outofrange;
  n->addr = (bfd_byte *) data + reloc_entry->address;
  n->addend = relocation;
  n->next = mips_refhi_list;
  mips_refhi_list = n;

  if (output_bfd != (bfd *) NULL)
    reloc_entry->address += input_section->output_offset;

  return ret;
}

/* Do a REFLO relocation.  This is a straightforward 16 bit inplace
   relocation; this function exists in order to do the REFHI
   relocation described above.  */

static bfd_reloc_status_type
mips_reflo_reloc (abfd,
		  reloc_entry,
		  symbol,
		  data,
		  input_section,
		  output_bfd,
		  error_message)
     bfd *abfd;
     arelent *reloc_entry;
     asymbol *symbol;
     PTR data;
     asection *input_section;
     bfd *output_bfd;
     char **error_message;
{
  if (mips_refhi_list != NULL)
    {
      struct mips_hi *l;

      l = mips_refhi_list;
      while (l != NULL)
	{
	  unsigned long insn;
	  unsigned long val;
	  unsigned long vallo;
	  struct mips_hi *next;

	  /* Do the REFHI relocation.  Note that we actually don't
	     need to know anything about the REFLO itself, except
	     where to find the low 16 bits of the addend needed by the
	     REFHI.  */
	  insn = bfd_get_32 (abfd, l->addr);
	  vallo = (bfd_get_32 (abfd, (bfd_byte *) data + reloc_entry->address)
		   & 0xffff);
	  val = ((insn & 0xffff) << 16) + vallo;
	  val += l->addend;

	  /* The low order 16 bits are always treated as a signed
	     value.  Therefore, a negative value in the low order bits
	     requires an adjustment in the high order bits.  We need
	     to make this adjustment in two ways: once for the bits we
	     took from the data, and once for the bits we are putting
	     back in to the data.  */
	  if ((vallo & 0x8000) != 0)
	    val -= 0x10000;
	  if ((val & 0x8000) != 0)
	    val += 0x10000;

	  insn = (insn &~ (unsigned) 0xffff) | ((val >> 16) & 0xffff);
	  bfd_put_32 (abfd, (bfd_vma) insn, l->addr);

	  next = l->next;
	  free (l);
	  l = next;
	}

      mips_refhi_list = NULL;
    }

  /* Now do the REFLO reloc in the usual way.  */
  return mips_generic_reloc (abfd, reloc_entry, symbol, data,
			      input_section, output_bfd, error_message);
}

/* Do a GPREL relocation.  This is a 16 bit value which must become
   the offset from the gp register.  */

static bfd_reloc_status_type
mips_gprel_reloc (abfd,
		  reloc_entry,
		  symbol,
		  data,
		  input_section,
		  output_bfd,
		  error_message)
     bfd *abfd;
     arelent *reloc_entry;
     asymbol *symbol;
     PTR data;
     asection *input_section;
     bfd *output_bfd;
     char **error_message;
{
  bfd_boolean relocatable;
  bfd_vma gp;
  bfd_vma relocation;
  unsigned long val;
  unsigned long insn;

  /* If we're relocating, and this is an external symbol with no
     addend, we don't want to change anything.  We will only have an
     addend if this is a newly created reloc, not read from an ECOFF
     file.  */
  if (output_bfd != (bfd *) NULL
      && (symbol->flags & BSF_SECTION_SYM) == 0
      && reloc_entry->addend == 0)
    {
      reloc_entry->address += input_section->output_offset;
      return bfd_reloc_ok;
    }

  if (output_bfd != (bfd *) NULL)
    relocatable = TRUE;
  else
    {
      relocatable = FALSE;
      output_bfd = symbol->section->output_section->owner;
    }

  if (bfd_is_und_section (symbol->section) && ! relocatable)
    return bfd_reloc_undefined;

  /* We have to figure out the gp value, so that we can adjust the
     symbol value correctly.  We look up the symbol _gp in the output
     BFD.  If we can't find it, we're stuck.  We cache it in the ECOFF
     target data.  We don't need to adjust the symbol value for an
     external symbol if we are producing relocatable output.  */
  gp = _bfd_get_gp_value (output_bfd);
  if (gp == 0
      && (! relocatable
	  || (symbol->flags & BSF_SECTION_SYM) != 0))
    {
      if (relocatable)
	{
	  /* Make up a value.  */
	  gp = symbol->section->output_section->vma + 0x4000;
	  _bfd_set_gp_value (output_bfd, gp);
	}
      else
	{
	  unsigned int count;
	  asymbol **sym;
	  unsigned int i;

	  count = bfd_get_symcount (output_bfd);
	  sym = bfd_get_outsymbols (output_bfd);

	  if (sym == (asymbol **) NULL)
	    i = count;
	  else
	    {
	      for (i = 0; i < count; i++, sym++)
		{
		  register const char *name;

		  name = bfd_asymbol_name (*sym);
		  if (*name == '_' && strcmp (name, "_gp") == 0)
		    {
		      gp = bfd_asymbol_value (*sym);
		      _bfd_set_gp_value (output_bfd, gp);
		      break;
		    }
		}
	    }

	  if (i >= count)
	    {
	      /* Only get the error once.  */
	      gp = 4;
	      _bfd_set_gp_value (output_bfd, gp);
	      *error_message =
		(char *) _("GP relative relocation when _gp not defined");
	      return bfd_reloc_dangerous;
	    }
	}
    }

  if (bfd_is_com_section (symbol->section))
    relocation = 0;
  else
    relocation = symbol->value;

  relocation += symbol->section->output_section->vma;
  relocation += symbol->section->output_offset;

  if (reloc_entry->address > bfd_get_section_limit (abfd, input_section))
    return bfd_reloc_outofrange;

  insn = bfd_get_32 (abfd, (bfd_byte *) data + reloc_entry->address);

  /* Set val to the offset into the section or symbol.  */
  val = ((insn & 0xffff) + reloc_entry->addend) & 0xffff;
  if (val & 0x8000)
    val -= 0x10000;

  /* Adjust val for the final section location and GP value.  If we
     are producing relocatable output, we don't want to do this for
     an external symbol.  */
  if (! relocatable
      || (symbol->flags & BSF_SECTION_SYM) != 0)
    val += relocation - gp;

  insn = (insn &~ (unsigned) 0xffff) | (val & 0xffff);
  bfd_put_32 (abfd, (bfd_vma) insn, (bfd_byte *) data + reloc_entry->address);

  if (relocatable)
    reloc_entry->address += input_section->output_offset;

  /* Make sure it fit in 16 bits.  */
  if ((long) val >= 0x8000 || (long) val < -0x8000)
    return bfd_reloc_overflow;

  return bfd_reloc_ok;
}

/* Get the howto structure for a generic reloc type.  */

static reloc_howto_type *
mips_bfd_reloc_type_lookup (abfd, code)
     bfd *abfd ATTRIBUTE_UNUSED;
     bfd_reloc_code_real_type code;
{
  int mips_type;

  switch (code)
    {
    case BFD_RELOC_16:
      mips_type = MIPS_R_REFHALF;
      break;
    case BFD_RELOC_32:
    case BFD_RELOC_CTOR:
      mips_type = MIPS_R_REFWORD;
      break;
    case BFD_RELOC_MIPS_JMP:
      mips_type = MIPS_R_JMPADDR;
      break;
    case BFD_RELOC_HI16_S:
      mips_type = MIPS_R_REFHI;
      break;
    case BFD_RELOC_LO16:
      mips_type = MIPS_R_REFLO;
      break;
    case BFD_RELOC_GPREL16:
      mips_type = MIPS_R_GPREL;
      break;
    case BFD_RELOC_MIPS_LITERAL:
      mips_type = MIPS_R_LITERAL;
      break;
    case BFD_RELOC_16_PCREL_S2:
      mips_type = MIPS_R_PCREL16;
      break;
    default:
      return (reloc_howto_type *) NULL;
    }

  return &mips_howto_table[mips_type];
}

static reloc_howto_type *
mips_bfd_reloc_name_lookup (bfd *abfd ATTRIBUTE_UNUSED,
			    const char *r_name)
{
  unsigned int i;

  for (i = 0;
       i < sizeof (mips_howto_table) / sizeof (mips_howto_table[0]);
       i++)
    if (mips_howto_table[i].name != NULL
	&& strcasecmp (mips_howto_table[i].name, r_name) == 0)
      return &mips_howto_table[i];

  return NULL;
}

/* A helper routine for mips_relocate_section which handles the REFHI
   relocations.  The REFHI relocation must be followed by a REFLO
   relocation, and the addend used is formed from the addends of both
   instructions.  */

static void
mips_relocate_hi (refhi, reflo, input_bfd, input_section, contents,
		  relocation)
     struct internal_reloc *refhi;
     struct internal_reloc *reflo;
     bfd *input_bfd;
     asection *input_section;
     bfd_byte *contents;
     bfd_vma relocation;
{
  unsigned long insn;
  unsigned long val;
  unsigned long vallo;

  if (refhi == NULL)
    return;

  insn = bfd_get_32 (input_bfd,
		     contents + refhi->r_vaddr - input_section->vma);
  if (reflo == NULL)
    vallo = 0;
  else
    vallo = (bfd_get_32 (input_bfd,
			 contents + reflo->r_vaddr - input_section->vma)
	     & 0xffff);

  val = ((insn & 0xffff) << 16) + vallo;
  val += relocation;

  /* The low order 16 bits are always treated as a signed value.
     Therefore, a negative value in the low order bits requires an
     adjustment in the high order bits.  We need to make this
     adjustment in two ways: once for the bits we took from the data,
     and once for the bits we are putting back in to the data.  */
  if ((vallo & 0x8000) != 0)
    val -= 0x10000;

  if ((val & 0x8000) != 0)
    val += 0x10000;

  insn = (insn &~ (unsigned) 0xffff) | ((val >> 16) & 0xffff);
  bfd_put_32 (input_bfd, (bfd_vma) insn,
	      contents + refhi->r_vaddr - input_section->vma);
}

/* Relocate a section while linking a MIPS ECOFF file.  */

static bfd_boolean
mips_relocate_section (output_bfd, info, input_bfd, input_section,
		       contents, external_relocs)
     bfd *output_bfd;
     struct bfd_link_info *info;
     bfd *input_bfd;
     asection *input_section;
     bfd_byte *contents;
     PTR external_relocs;
{
  asection **symndx_to_section;
  struct ecoff_link_hash_entry **sym_hashes;
  bfd_vma gp;
  bfd_boolean gp_undefined;
  struct external_reloc *ext_rel;
  struct external_reloc *ext_rel_end;
  unsigned int i;
  bfd_boolean got_lo;
  struct internal_reloc lo_int_rel;
  bfd_size_type amt;

  BFD_ASSERT (input_bfd->xvec->byteorder
	      == output_bfd->xvec->byteorder);

  /* We keep a table mapping the symndx found in an internal reloc to
     the appropriate section.  This is faster than looking up the
     section by name each time.  */
  symndx_to_section = ecoff_data (input_bfd)->symndx_to_section;
  if (symndx_to_section == (asection **) NULL)
    {
      amt = NUM_RELOC_SECTIONS * sizeof (asection *);
      symndx_to_section = (asection **) bfd_alloc (input_bfd, amt);
      if (!symndx_to_section)
	return FALSE;

      symndx_to_section[RELOC_SECTION_NONE] = NULL;
      symndx_to_section[RELOC_SECTION_TEXT] =
	bfd_get_section_by_name (input_bfd, ".text");
      symndx_to_section[RELOC_SECTION_RDATA] =
	bfd_get_section_by_name (input_bfd, ".rdata");
      symndx_to_section[RELOC_SECTION_DATA] =
	bfd_get_section_by_name (input_bfd, ".data");
      symndx_to_section[RELOC_SECTION_SDATA] =
	bfd_get_section_by_name (input_bfd, ".sdata");
      symndx_to_section[RELOC_SECTION_SBSS] =
	bfd_get_section_by_name (input_bfd, ".sbss");
      symndx_to_section[RELOC_SECTION_BSS] =
	bfd_get_section_by_name (input_bfd, ".bss");
      symndx_to_section[RELOC_SECTION_INIT] =
	bfd_get_section_by_name (input_bfd, ".init");
      symndx_to_section[RELOC_SECTION_LIT8] =
	bfd_get_section_by_name (input_bfd, ".lit8");
      symndx_to_section[RELOC_SECTION_LIT4] =
	bfd_get_section_by_name (input_bfd, ".lit4");
      symndx_to_section[RELOC_SECTION_XDATA] = NULL;
      symndx_to_section[RELOC_SECTION_PDATA] = NULL;
      symndx_to_section[RELOC_SECTION_FINI] =
	bfd_get_section_by_name (input_bfd, ".fini");
      symndx_to_section[RELOC_SECTION_LITA] = NULL;
      symndx_to_section[RELOC_SECTION_ABS] = NULL;

      ecoff_data (input_bfd)->symndx_to_section = symndx_to_section;
    }

  sym_hashes = ecoff_data (input_bfd)->sym_hashes;

  gp = _bfd_get_gp_value (output_bfd);
  if (gp == 0)
    gp_undefined = TRUE;
  else
    gp_undefined = FALSE;

  got_lo = FALSE;

  ext_rel = (struct external_reloc *) external_relocs;
  ext_rel_end = ext_rel + input_section->reloc_count;
  for (i = 0; ext_rel < ext_rel_end; ext_rel++, i++)
    {
      struct internal_reloc int_rel;
      bfd_boolean use_lo = FALSE;
      bfd_vma addend;
      reloc_howto_type *howto;
      struct ecoff_link_hash_entry *h = NULL;
      asection *s = NULL;
      bfd_vma relocation;
      bfd_reloc_status_type r;

      if (! got_lo)
	mips_ecoff_swap_reloc_in (input_bfd, (PTR) ext_rel, &int_rel);
      else
	{
	  int_rel = lo_int_rel;
	  got_lo = FALSE;
	}

      BFD_ASSERT (int_rel.r_type
		  < sizeof mips_howto_table / sizeof mips_howto_table[0]);

      /* The REFHI reloc requires special handling.  It must be followed
	 by a REFLO reloc, and the addend is formed from both relocs.  */
      if (int_rel.r_type == MIPS_R_REFHI)
	{
	  struct external_reloc *lo_ext_rel;

	  /* As a GNU extension, permit an arbitrary number of REFHI
             relocs before the REFLO reloc.  This permits gcc to emit
	     the HI and LO relocs itself.  */
	  for (lo_ext_rel = ext_rel + 1;
	       lo_ext_rel < ext_rel_end;
	       lo_ext_rel++)
	    {
	      mips_ecoff_swap_reloc_in (input_bfd, (PTR) lo_ext_rel,
					&lo_int_rel);
	      if (lo_int_rel.r_type != int_rel.r_type)
		break;
	    }

	  if (lo_ext_rel < ext_rel_end
	      && lo_int_rel.r_type == MIPS_R_REFLO
	      && int_rel.r_extern == lo_int_rel.r_extern
	      && int_rel.r_symndx == lo_int_rel.r_symndx)
	    {
	      use_lo = TRUE;
	      if (lo_ext_rel == ext_rel + 1)
		got_lo = TRUE;
	    }
	}

      howto = &mips_howto_table[int_rel.r_type];

      if (int_rel.r_extern)
	{
	  h = sym_hashes[int_rel.r_symndx];
	  /* If h is NULL, that means that there is a reloc against an
	     external symbol which we thought was just a debugging
	     symbol.  This should not happen.  */
	  if (h == (struct ecoff_link_hash_entry *) NULL)
	    abort ();
	}
      else
	{
	  if (int_rel.r_symndx < 0 || int_rel.r_symndx >= NUM_RELOC_SECTIONS)
	    s = NULL;
	  else
	    s = symndx_to_section[int_rel.r_symndx];

	  if (s == (asection *) NULL)
	    abort ();
	}

      /* The GPREL reloc uses an addend: the difference in the GP
	 values.  */
      if (int_rel.r_type != MIPS_R_GPREL
	  && int_rel.r_type != MIPS_R_LITERAL)
	addend = 0;
      else
	{
	  if (gp_undefined)
	    {
	      if (! ((*info->callbacks->reloc_dangerous)
		     (info, _("GP relative relocation used when GP not defined"),
		      input_bfd, input_section,
		      int_rel.r_vaddr - input_section->vma)))
		return FALSE;
	      /* Only give the error once per link.  */
	      gp = 4;
	      _bfd_set_gp_value (output_bfd, gp);
	      gp_undefined = FALSE;
	    }
	  if (! int_rel.r_extern)
	    {
	      /* This is a relocation against a section.  The current
		 addend in the instruction is the difference between
		 INPUT_SECTION->vma and the GP value of INPUT_BFD.  We
		 must change this to be the difference between the
		 final definition (which will end up in RELOCATION)
		 and the GP value of OUTPUT_BFD (which is in GP).  */
	      addend = ecoff_data (input_bfd)->gp - gp;
	    }
	  else if (! info->relocatable
		   || h->root.type == bfd_link_hash_defined
		   || h->root.type == bfd_link_hash_defweak)
	    {
	      /* This is a relocation against a defined symbol.  The
		 current addend in the instruction is simply the
		 desired offset into the symbol (normally zero).  We
		 are going to change this into a relocation against a
		 defined symbol, so we want the instruction to hold
		 the difference between the final definition of the
		 symbol (which will end up in RELOCATION) and the GP
		 value of OUTPUT_BFD (which is in GP).  */
	      addend = - gp;
	    }
	  else
	    {
	      /* This is a relocation against an undefined or common
		 symbol.  The current addend in the instruction is
		 simply the desired offset into the symbol (normally
		 zero).  We are generating relocatable output, and we
		 aren't going to define this symbol, so we just leave
		 the instruction alone.  */
	      addend = 0;
	    }
	}

      if (info->relocatable)
	{
	  /* We are generating relocatable output, and must convert
	     the existing reloc.  */
	  if (int_rel.r_extern)
	    {
	      if ((h->root.type == bfd_link_hash_defined
		   || h->root.type == bfd_link_hash_defweak)
		  && ! bfd_is_abs_section (h->root.u.def.section))
		{
		  const char *name;

		  /* This symbol is defined in the output.  Convert
		     the reloc from being against the symbol to being
		     against the section.  */

		  /* Clear the r_extern bit.  */
		  int_rel.r_extern = 0;

		  /* Compute a new r_symndx value.  */
		  s = h->root.u.def.section;
		  name = bfd_get_section_name (output_bfd,
					       s->output_section);

		  int_rel.r_symndx = -1;
		  switch (name[1])
		    {
		    case 'b':
		      if (strcmp (name, ".bss") == 0)
			int_rel.r_symndx = RELOC_SECTION_BSS;
		      break;
		    case 'd':
		      if (strcmp (name, ".data") == 0)
			int_rel.r_symndx = RELOC_SECTION_DATA;
		      break;
		    case 'f':
		      if (strcmp (name, ".fini") == 0)
			int_rel.r_symndx = RELOC_SECTION_FINI;
		      break;
		    case 'i':
		      if (strcmp (name, ".init") == 0)
			int_rel.r_symndx = RELOC_SECTION_INIT;
		      break;
		    case 'l':
		      if (strcmp (name, ".lit8") == 0)
			int_rel.r_symndx = RELOC_SECTION_LIT8;
		      else if (strcmp (name, ".lit4") == 0)
			int_rel.r_symndx = RELOC_SECTION_LIT4;
		      break;
		    case 'r':
		      if (strcmp (name, ".rdata") == 0)
			int_rel.r_symndx = RELOC_SECTION_RDATA;
		      break;
		    case 's':
		      if (strcmp (name, ".sdata") == 0)
			int_rel.r_symndx = RELOC_SECTION_SDATA;
		      else if (strcmp (name, ".sbss") == 0)
			int_rel.r_symndx = RELOC_SECTION_SBSS;
		      break;
		    case 't':
		      if (strcmp (name, ".text") == 0)
			int_rel.r_symndx = RELOC_SECTION_TEXT;
		      break;
		    }

		  if (int_rel.r_symndx == -1)
		    abort ();

		  /* Add the section VMA and the symbol value.  */
		  relocation = (h->root.u.def.value
				+ s->output_section->vma
				+ s->output_offset);

		  /* For a PC relative relocation, the object file
		     currently holds just the addend.  We must adjust
		     by the address to get the right value.  */
		  if (howto->pc_relative)
		    relocation -= int_rel.r_vaddr - input_section->vma;

		  h = NULL;
		}
	      else
		{
		  /* Change the symndx value to the right one for the
		     output BFD.  */
		  int_rel.r_symndx = h->indx;
		  if (int_rel.r_symndx == -1)
		    {
		      /* This symbol is not being written out.  */
		      if (! ((*info->callbacks->unattached_reloc)
			     (info, h->root.root.string, input_bfd,
			      input_section,
			      int_rel.r_vaddr - input_section->vma)))
			return FALSE;
		      int_rel.r_symndx = 0;
		    }
		  relocation = 0;
		}
	    }
	  else
	    {
	      /* This is a relocation against a section.  Adjust the
		 value by the amount the section moved.  */
	      relocation = (s->output_section->vma
			    + s->output_offset
			    - s->vma);
	    }

	  relocation += addend;
	  addend = 0;

	  /* Adjust a PC relative relocation by removing the reference
	     to the original address in the section and including the
	     reference to the new address.  */
	  if (howto->pc_relative)
	    relocation -= (input_section->output_section->vma
			   + input_section->output_offset
			   - input_section->vma);

	  /* Adjust the contents.  */
	  if (relocation == 0)
	    r = bfd_reloc_ok;
	  else
	    {
	      if (int_rel.r_type != MIPS_R_REFHI)
		r = _bfd_relocate_contents (howto, input_bfd, relocation,
					    (contents
					     + int_rel.r_vaddr
					     - input_section->vma));
	      else
		{
		  mips_relocate_hi (&int_rel,
				    use_lo ? &lo_int_rel : NULL,
				    input_bfd, input_section, contents,
				    relocation);
		  r = bfd_reloc_ok;
		}
	    }

	  /* Adjust the reloc address.  */
	  int_rel.r_vaddr += (input_section->output_section->vma
			      + input_section->output_offset
			      - input_section->vma);

	  /* Save the changed reloc information.  */
	  mips_ecoff_swap_reloc_out (input_bfd, &int_rel, (PTR) ext_rel);
	}
      else
	{
	  /* We are producing a final executable.  */
	  if (int_rel.r_extern)
	    {
	      /* This is a reloc against a symbol.  */
	      if (h->root.type == bfd_link_hash_defined
		  || h->root.type == bfd_link_hash_defweak)
		{
		  asection *hsec;

		  hsec = h->root.u.def.section;
		  relocation = (h->root.u.def.value
				+ hsec->output_section->vma
				+ hsec->output_offset);
		}
	      else
		{
		  if (! ((*info->callbacks->undefined_symbol)
			 (info, h->root.root.string, input_bfd,
			  input_section,
			  int_rel.r_vaddr - input_section->vma, TRUE)))
		    return FALSE;
		  relocation = 0;
		}
	    }
	  else
	    {
	      /* This is a reloc against a section.  */
	      relocation = (s->output_section->vma
			    + s->output_offset
			    - s->vma);

	      /* A PC relative reloc is already correct in the object
		 file.  Make it look like a pcrel_offset relocation by
		 adding in the start address.  */
	      if (howto->pc_relative)
		relocation += int_rel.r_vaddr;
	    }

	  if (int_rel.r_type != MIPS_R_REFHI)
	    r = _bfd_final_link_relocate (howto,
					  input_bfd,
					  input_section,
					  contents,
					  (int_rel.r_vaddr
					   - input_section->vma),
					  relocation,
					  addend);
	  else
	    {
	      mips_relocate_hi (&int_rel,
				use_lo ? &lo_int_rel : NULL,
				input_bfd, input_section, contents,
				relocation);
	      r = bfd_reloc_ok;
	    }
	}

      /* MIPS_R_JMPADDR requires peculiar overflow detection.  The
	 instruction provides a 28 bit address (the two lower bits are
	 implicit zeroes) which is combined with the upper four bits
	 of the instruction address.  */
      if (r == bfd_reloc_ok
	  && int_rel.r_type == MIPS_R_JMPADDR
	  && (((relocation
		+ addend
		+ (int_rel.r_extern ? 0 : s->vma))
	       & 0xf0000000)
	      != ((input_section->output_section->vma
		   + input_section->output_offset
		   + (int_rel.r_vaddr - input_section->vma))
		  & 0xf0000000)))
	r = bfd_reloc_overflow;

      if (r != bfd_reloc_ok)
	{
	  switch (r)
	    {
	    default:
	    case bfd_reloc_outofrange:
	      abort ();
	    case bfd_reloc_overflow:
	      {
		const char *name;

		if (int_rel.r_extern)
		  name = NULL;
		else
		  name = bfd_section_name (input_bfd, s);
		if (! ((*info->callbacks->reloc_overflow)
		       (info, (h ? &h->root : NULL), name, howto->name,
			(bfd_vma) 0, input_bfd, input_section,
			int_rel.r_vaddr - input_section->vma)))
		  return FALSE;
	      }
	      break;
	    }
	}
    }

  return TRUE;
}

/* This is the ECOFF backend structure.  The backend field of the
   target vector points to this.  */

static const struct ecoff_backend_data mips_ecoff_backend_data =
{
  /* COFF backend structure.  */
  {
    (void (*) PARAMS ((bfd *,PTR,int,int,int,int,PTR))) bfd_void, /* aux_in */
    (void (*) PARAMS ((bfd *,PTR,PTR))) bfd_void, /* sym_in */
    (void (*) PARAMS ((bfd *,PTR,PTR))) bfd_void, /* lineno_in */
    (unsigned (*) PARAMS ((bfd *,PTR,int,int,int,int,PTR)))bfd_void,/*aux_out*/
    (unsigned (*) PARAMS ((bfd *,PTR,PTR))) bfd_void, /* sym_out */
    (unsigned (*) PARAMS ((bfd *,PTR,PTR))) bfd_void, /* lineno_out */
    (unsigned (*) PARAMS ((bfd *,PTR,PTR))) bfd_void, /* reloc_out */
    mips_ecoff_swap_filehdr_out, mips_ecoff_swap_aouthdr_out,
    mips_ecoff_swap_scnhdr_out,
    FILHSZ, AOUTSZ, SCNHSZ, 0, 0, 0, 0, FILNMLEN, TRUE, FALSE, 4, FALSE, 2,
    mips_ecoff_swap_filehdr_in, mips_ecoff_swap_aouthdr_in,
    mips_ecoff_swap_scnhdr_in, NULL,
    mips_ecoff_bad_format_hook, _bfd_ecoff_set_arch_mach_hook,
    _bfd_ecoff_mkobject_hook, _bfd_ecoff_styp_to_sec_flags,
    _bfd_ecoff_set_alignment_hook, _bfd_ecoff_slurp_symbol_table,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL
  },
  /* Supported architecture.  */
  bfd_arch_mips,
  /* Initial portion of armap string.  */
  "__________",
  /* The page boundary used to align sections in a demand-paged
     executable file.  E.g., 0x1000.  */
  0x1000,
  /* TRUE if the .rdata section is part of the text segment, as on the
     Alpha.  FALSE if .rdata is part of the data segment, as on the
     MIPS.  */
  FALSE,
  /* Bitsize of constructor entries.  */
  32,
  /* Reloc to use for constructor entries.  */
  &mips_howto_table[MIPS_R_REFWORD],
  {
    /* Symbol table magic number.  */
    magicSym,
    /* Alignment of debugging information.  E.g., 4.  */
    4,
    /* Sizes of external symbolic information.  */
    sizeof (struct hdr_ext),
    sizeof (struct dnr_ext),
    sizeof (struct pdr_ext),
    sizeof (struct sym_ext),
    sizeof (struct opt_ext),
    sizeof (struct fdr_ext),
    sizeof (struct rfd_ext),
    sizeof (struct ext_ext),
    /* Functions to swap in external symbolic data.  */
    ecoff_swap_hdr_in,
    ecoff_swap_dnr_in,
    ecoff_swap_pdr_in,
    ecoff_swap_sym_in,
    ecoff_swap_opt_in,
    ecoff_swap_fdr_in,
    ecoff_swap_rfd_in,
    ecoff_swap_ext_in,
    _bfd_ecoff_swap_tir_in,
    _bfd_ecoff_swap_rndx_in,
    /* Functions to swap out external symbolic data.  */
    ecoff_swap_hdr_out,
    ecoff_swap_dnr_out,
    ecoff_swap_pdr_out,
    ecoff_swap_sym_out,
    ecoff_swap_opt_out,
    ecoff_swap_fdr_out,
    ecoff_swap_rfd_out,
    ecoff_swap_ext_out,
    _bfd_ecoff_swap_tir_out,
    _bfd_ecoff_swap_rndx_out,
    /* Function to read in symbolic data.  */
    _bfd_ecoff_slurp_symbolic_info
  },
  /* External reloc size.  */
  RELSZ,
  /* Reloc swapping functions.  */
  mips_ecoff_swap_reloc_in,
  mips_ecoff_swap_reloc_out,
  /* Backend reloc tweaking.  */
  mips_adjust_reloc_in,
  mips_adjust_reloc_out,
  /* Relocate section contents while linking.  */
  mips_relocate_section,
  /* Do final adjustments to filehdr and aouthdr.  */
  NULL,
  /* Read an element from an archive at a given file position.  */
  _bfd_get_elt_at_filepos
};

/* Looking up a reloc type is MIPS specific.  */
#define _bfd_ecoff_bfd_reloc_type_lookup mips_bfd_reloc_type_lookup
#define _bfd_ecoff_bfd_reloc_name_lookup mips_bfd_reloc_name_lookup

/* Getting relocated section contents is generic.  */
#define _bfd_ecoff_bfd_get_relocated_section_contents \
  bfd_generic_get_relocated_section_contents

/* Handling file windows is generic.  */
#define _bfd_ecoff_get_section_contents_in_window \
  _bfd_generic_get_section_contents_in_window

/* Relaxing sections is MIPS specific.  */
#define _bfd_ecoff_bfd_relax_section bfd_generic_relax_section

/* GC of sections is not done.  */
#define _bfd_ecoff_bfd_gc_sections bfd_generic_gc_sections

/* Merging of sections is not done.  */
#define _bfd_ecoff_bfd_merge_sections bfd_generic_merge_sections

#define _bfd_ecoff_bfd_is_group_section bfd_generic_is_group_section
#define _bfd_ecoff_bfd_discard_group bfd_generic_discard_group
#define _bfd_ecoff_section_already_linked \
  _bfd_generic_section_already_linked

extern const bfd_target ecoff_big_vec;

const bfd_target ecoff_little_vec =
{
  "ecoff-littlemips",		/* name */
  bfd_target_ecoff_flavour,
  BFD_ENDIAN_LITTLE,		/* data byte order is little */
  BFD_ENDIAN_LITTLE,		/* header byte order is little */

  (HAS_RELOC | EXEC_P |		/* object flags */
   HAS_LINENO | HAS_DEBUG |
   HAS_SYMS | HAS_LOCALS | WP_TEXT | D_PAGED),

  (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_RELOC | SEC_CODE | SEC_DATA),
  0,				/* leading underscore */
  ' ',				/* ar_pad_char */
  15,				/* ar_max_namelen */
  bfd_getl64, bfd_getl_signed_64, bfd_putl64,
     bfd_getl32, bfd_getl_signed_32, bfd_putl32,
     bfd_getl16, bfd_getl_signed_16, bfd_putl16, /* data */
  bfd_getl64, bfd_getl_signed_64, bfd_putl64,
     bfd_getl32, bfd_getl_signed_32, bfd_putl32,
     bfd_getl16, bfd_getl_signed_16, bfd_putl16, /* hdrs */

  {_bfd_dummy_target, coff_object_p, /* bfd_check_format */
     _bfd_ecoff_archive_p, _bfd_dummy_target},
  {bfd_false, _bfd_ecoff_mkobject,  /* bfd_set_format */
     _bfd_generic_mkarchive, bfd_false},
  {bfd_false, _bfd_ecoff_write_object_contents, /* bfd_write_contents */
     _bfd_write_archive_contents, bfd_false},

     BFD_JUMP_TABLE_GENERIC (_bfd_ecoff),
     BFD_JUMP_TABLE_COPY (_bfd_ecoff),
     BFD_JUMP_TABLE_CORE (_bfd_nocore),
     BFD_JUMP_TABLE_ARCHIVE (_bfd_ecoff),
     BFD_JUMP_TABLE_SYMBOLS (_bfd_ecoff),
     BFD_JUMP_TABLE_RELOCS (_bfd_ecoff),
     BFD_JUMP_TABLE_WRITE (_bfd_ecoff),
     BFD_JUMP_TABLE_LINK (_bfd_ecoff),
     BFD_JUMP_TABLE_DYNAMIC (_bfd_nodynamic),

  & ecoff_big_vec,

  (PTR) &mips_ecoff_backend_data
};

const bfd_target ecoff_big_vec =
{
  "ecoff-bigmips",		/* name */
  bfd_target_ecoff_flavour,
  BFD_ENDIAN_BIG,		/* data byte order is big */
  BFD_ENDIAN_BIG,		/* header byte order is big */

  (HAS_RELOC | EXEC_P |		/* object flags */
   HAS_LINENO | HAS_DEBUG |
   HAS_SYMS | HAS_LOCALS | WP_TEXT | D_PAGED),

  (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_RELOC | SEC_CODE | SEC_DATA),
  0,				/* leading underscore */
  ' ',				/* ar_pad_char */
  15,				/* ar_max_namelen */
  bfd_getb64, bfd_getb_signed_64, bfd_putb64,
     bfd_getb32, bfd_getb_signed_32, bfd_putb32,
     bfd_getb16, bfd_getb_signed_16, bfd_putb16,
  bfd_getb64, bfd_getb_signed_64, bfd_putb64,
     bfd_getb32, bfd_getb_signed_32, bfd_putb32,
     bfd_getb16, bfd_getb_signed_16, bfd_putb16,
 {_bfd_dummy_target, coff_object_p, /* bfd_check_format */
    _bfd_ecoff_archive_p, _bfd_dummy_target},
 {bfd_false, _bfd_ecoff_mkobject, /* bfd_set_format */
    _bfd_generic_mkarchive, bfd_false},
 {bfd_false, _bfd_ecoff_write_object_contents, /* bfd_write_contents */
    _bfd_write_archive_contents, bfd_false},

     BFD_JUMP_TABLE_GENERIC (_bfd_ecoff),
     BFD_JUMP_TABLE_COPY (_bfd_ecoff),
     BFD_JUMP_TABLE_CORE (_bfd_nocore),
     BFD_JUMP_TABLE_ARCHIVE (_bfd_ecoff),
     BFD_JUMP_TABLE_SYMBOLS (_bfd_ecoff),
     BFD_JUMP_TABLE_RELOCS (_bfd_ecoff),
     BFD_JUMP_TABLE_WRITE (_bfd_ecoff),
     BFD_JUMP_TABLE_LINK (_bfd_ecoff),
     BFD_JUMP_TABLE_DYNAMIC (_bfd_nodynamic),

  & ecoff_little_vec,

  (PTR) &mips_ecoff_backend_data
};

const bfd_target ecoff_biglittle_vec =
{
  "ecoff-biglittlemips",		/* name */
  bfd_target_ecoff_flavour,
  BFD_ENDIAN_LITTLE,		/* data byte order is little */
  BFD_ENDIAN_BIG,		/* header byte order is big */

  (HAS_RELOC | EXEC_P |		/* object flags */
   HAS_LINENO | HAS_DEBUG |
   HAS_SYMS | HAS_LOCALS | WP_TEXT | D_PAGED),

  (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_RELOC | SEC_CODE | SEC_DATA),
  0,				/* leading underscore */
  ' ',				/* ar_pad_char */
  15,				/* ar_max_namelen */
  bfd_getl64, bfd_getl_signed_64, bfd_putl64,
     bfd_getl32, bfd_getl_signed_32, bfd_putl32,
     bfd_getl16, bfd_getl_signed_16, bfd_putl16, /* data */
  bfd_getb64, bfd_getb_signed_64, bfd_putb64,
     bfd_getb32, bfd_getb_signed_32, bfd_putb32,
     bfd_getb16, bfd_getb_signed_16, bfd_putb16, /* hdrs */

  {_bfd_dummy_target, coff_object_p, /* bfd_check_format */
     _bfd_ecoff_archive_p, _bfd_dummy_target},
  {bfd_false, _bfd_ecoff_mkobject,  /* bfd_set_format */
     _bfd_generic_mkarchive, bfd_false},
  {bfd_false, _bfd_ecoff_write_object_contents, /* bfd_write_contents */
     _bfd_write_archive_contents, bfd_false},

     BFD_JUMP_TABLE_GENERIC (_bfd_ecoff),
     BFD_JUMP_TABLE_COPY (_bfd_ecoff),
     BFD_JUMP_TABLE_CORE (_bfd_nocore),
     BFD_JUMP_TABLE_ARCHIVE (_bfd_ecoff),
     BFD_JUMP_TABLE_SYMBOLS (_bfd_ecoff),
     BFD_JUMP_TABLE_RELOCS (_bfd_ecoff),
     BFD_JUMP_TABLE_WRITE (_bfd_ecoff),
     BFD_JUMP_TABLE_LINK (_bfd_ecoff),
     BFD_JUMP_TABLE_DYNAMIC (_bfd_nodynamic),

  NULL,

  (PTR) &mips_ecoff_backend_data
};
