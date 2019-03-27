/* BFD back-end for ARM COFF files.
   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.
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

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "coff/arm.h"
#include "coff/internal.h"

#ifdef COFF_WITH_PE
#include "coff/pe.h"
#endif

#include "libcoff.h"

/* Macros for manipulation the bits in the flags field of the coff data
   structure.  */
#define APCS_26_FLAG(abfd) \
  (coff_data (abfd)->flags & F_APCS_26)

#define APCS_FLOAT_FLAG(abfd) \
  (coff_data (abfd)->flags & F_APCS_FLOAT)

#define PIC_FLAG(abfd) \
  (coff_data (abfd)->flags & F_PIC)

#define APCS_SET(abfd) \
  (coff_data (abfd)->flags & F_APCS_SET)

#define SET_APCS_FLAGS(abfd, flgs) \
  do									\
    {									\
      coff_data (abfd)->flags &= ~(F_APCS_26 | F_APCS_FLOAT | F_PIC);	\
      coff_data (abfd)->flags |= (flgs) | F_APCS_SET;			\
    }									\
  while (0)

#define INTERWORK_FLAG(abfd) \
  (coff_data (abfd)->flags & F_INTERWORK)

#define INTERWORK_SET(abfd) \
  (coff_data (abfd)->flags & F_INTERWORK_SET)

#define SET_INTERWORK_FLAG(abfd, flg) \
  do									\
    {									\
      coff_data (abfd)->flags &= ~F_INTERWORK;				\
      coff_data (abfd)->flags |= (flg) | F_INTERWORK_SET;		\
    }									\
  while (0)

#ifndef NUM_ELEM
#define NUM_ELEM(a) ((sizeof (a)) / sizeof ((a)[0]))
#endif

typedef enum {bunknown, b9, b12, b23} thumb_pcrel_branchtype;
/* Some typedefs for holding instructions.  */
typedef unsigned long int insn32;
typedef unsigned short int insn16;

/* The linker script knows the section names for placement.
   The entry_names are used to do simple name mangling on the stubs.
   Given a function name, and its type, the stub can be found. The
   name can be changed. The only requirement is the %s be present.  */

#define THUMB2ARM_GLUE_SECTION_NAME ".glue_7t"
#define THUMB2ARM_GLUE_ENTRY_NAME   "__%s_from_thumb"

#define ARM2THUMB_GLUE_SECTION_NAME ".glue_7"
#define ARM2THUMB_GLUE_ENTRY_NAME   "__%s_from_arm"

/* Used by the assembler.  */

static bfd_reloc_status_type
coff_arm_reloc (bfd *abfd,
		arelent *reloc_entry,
		asymbol *symbol ATTRIBUTE_UNUSED,
		void * data,
		asection *input_section ATTRIBUTE_UNUSED,
		bfd *output_bfd,
		char **error_message ATTRIBUTE_UNUSED)
{
  symvalue diff;

  if (output_bfd == NULL)
    return bfd_reloc_continue;

  diff = reloc_entry->addend;

#define DOIT(x)							\
  x = ((x & ~howto->dst_mask)					\
       | (((x & howto->src_mask) + diff) & howto->dst_mask))

    if (diff != 0)
      {
	reloc_howto_type *howto = reloc_entry->howto;
	unsigned char *addr = (unsigned char *) data + reloc_entry->address;

	switch (howto->size)
	  {
	  case 0:
	    {
	      char x = bfd_get_8 (abfd, addr);
	      DOIT (x);
	      bfd_put_8 (abfd, x, addr);
	    }
	    break;

	  case 1:
	    {
	      short x = bfd_get_16 (abfd, addr);
	      DOIT (x);
	      bfd_put_16 (abfd, (bfd_vma) x, addr);
	    }
	    break;

	  case 2:
	    {
	      long x = bfd_get_32 (abfd, addr);
	      DOIT (x);
	      bfd_put_32 (abfd, (bfd_vma) x, addr);
	    }
	    break;

	  default:
	    abort ();
	  }
      }

  /* Now let bfd_perform_relocation finish everything up.  */
  return bfd_reloc_continue;
}

/* If USER_LABEL_PREFIX is defined as "_" (see coff_arm_is_local_label_name()
   in this file), then TARGET_UNDERSCORE should be defined, otherwise it
   should not.  */
#ifndef TARGET_UNDERSCORE
#define TARGET_UNDERSCORE '_'
#endif

#ifndef PCRELOFFSET
#define PCRELOFFSET TRUE
#endif

/* These most certainly belong somewhere else. Just had to get rid of
   the manifest constants in the code.  */

#ifdef ARM_WINCE

#define ARM_26D      0
#define ARM_32       1
#define ARM_RVA32    2
#define ARM_26	     3
#define ARM_THUMB12  4
#define ARM_SECTION  14
#define ARM_SECREL   15

#else

#define ARM_8        0
#define ARM_16       1
#define ARM_32       2
#define ARM_26       3
#define ARM_DISP8    4
#define ARM_DISP16   5
#define ARM_DISP32   6
#define ARM_26D      7
/* 8 is unused.  */
#define ARM_NEG16    9
#define ARM_NEG32   10
#define ARM_RVA32   11
#define ARM_THUMB9  12
#define ARM_THUMB12 13
#define ARM_THUMB23 14

#endif

static bfd_reloc_status_type aoutarm_fix_pcrel_26_done
  (bfd *, arelent *, asymbol *, void *, asection *, bfd *, char **);
static bfd_reloc_status_type aoutarm_fix_pcrel_26
  (bfd *, arelent *, asymbol *, void *, asection *, bfd *, char **);
static bfd_reloc_status_type coff_thumb_pcrel_12
  (bfd *, arelent *, asymbol *, void *, asection *, bfd *, char **);
#ifndef ARM_WINCE
static bfd_reloc_status_type coff_thumb_pcrel_9
  (bfd *, arelent *, asymbol *, void *, asection *, bfd *, char **);
static bfd_reloc_status_type coff_thumb_pcrel_23
  (bfd *, arelent *, asymbol *, void *, asection *, bfd *, char **);
#endif

static reloc_howto_type aoutarm_std_reloc_howto[] =
  {
#ifdef ARM_WINCE
    HOWTO (ARM_26D,
	   2,
	   2,
	   24,
	   TRUE,
	   0,
	   complain_overflow_dont,
	   aoutarm_fix_pcrel_26_done,
	   "ARM_26D",
	   TRUE, 	/* partial_inplace.  */
	   0x00ffffff,
	   0x0,
	   PCRELOFFSET),
    HOWTO (ARM_32,
	   0,
	   2,
	   32,
	   FALSE,
	   0,
	   complain_overflow_bitfield,
	   coff_arm_reloc,
	   "ARM_32",
	   TRUE, 	/* partial_inplace.  */
	   0xffffffff,
	   0xffffffff,
	   PCRELOFFSET),
    HOWTO (ARM_RVA32,
	   0,
	   2,
	   32,
	   FALSE,
	   0,
	   complain_overflow_bitfield,
	   coff_arm_reloc,
	   "ARM_RVA32",
	   TRUE, 	/* partial_inplace.  */
	   0xffffffff,
	   0xffffffff,
	   PCRELOFFSET),
    HOWTO (ARM_26,
	   2,
	   2,
	   24,
	   TRUE,
	   0,
	   complain_overflow_signed,
	   aoutarm_fix_pcrel_26 ,
	   "ARM_26",
	   FALSE,
	   0x00ffffff,
	   0x00ffffff,
	   PCRELOFFSET),
    HOWTO (ARM_THUMB12,
	   1,
	   1,
	   11,
	   TRUE,
	   0,
	   complain_overflow_signed,
	   coff_thumb_pcrel_12 ,
	   "ARM_THUMB12",
	   FALSE,
	   0x000007ff,
	   0x000007ff,
	   PCRELOFFSET),
    EMPTY_HOWTO (-1),
    EMPTY_HOWTO (-1),
    EMPTY_HOWTO (-1),
    EMPTY_HOWTO (-1),
    EMPTY_HOWTO (-1),
    EMPTY_HOWTO (-1),
    EMPTY_HOWTO (-1),
    EMPTY_HOWTO (-1),
    EMPTY_HOWTO (-1),
    HOWTO (ARM_SECTION,
	   0,
	   1,
	   16,
	   FALSE,
	   0,
	   complain_overflow_bitfield,
	   coff_arm_reloc,
	   "ARM_SECTION",
	   TRUE, 	/* partial_inplace.  */
	   0x0000ffff,
	   0x0000ffff,
	   PCRELOFFSET),
    HOWTO (ARM_SECREL,
	   0,
	   2,
	   32,
	   FALSE,
	   0,
	   complain_overflow_bitfield,
	   coff_arm_reloc,
	   "ARM_SECREL",
	   TRUE, 	/* partial_inplace.  */
	   0xffffffff,
	   0xffffffff,
	   PCRELOFFSET),
#else /* not ARM_WINCE */
    HOWTO (ARM_8,
	   0,
	   0,
	   8,
	   FALSE,
	   0,
	   complain_overflow_bitfield,
	   coff_arm_reloc,
	   "ARM_8",
	   TRUE,
	   0x000000ff,
	   0x000000ff,
	   PCRELOFFSET),
    HOWTO (ARM_16,
	   0,
	   1,
	   16,
	   FALSE,
	   0,
	   complain_overflow_bitfield,
	   coff_arm_reloc,
	   "ARM_16",
	   TRUE,
	   0x0000ffff,
	   0x0000ffff,
	   PCRELOFFSET),
    HOWTO (ARM_32,
	   0,
	   2,
	   32,
	   FALSE,
	   0,
	   complain_overflow_bitfield,
	   coff_arm_reloc,
	   "ARM_32",
	   TRUE,
	   0xffffffff,
	   0xffffffff,
	   PCRELOFFSET),
    HOWTO (ARM_26,
	   2,
	   2,
	   24,
	   TRUE,
	   0,
	   complain_overflow_signed,
	   aoutarm_fix_pcrel_26 ,
	   "ARM_26",
	   FALSE,
	   0x00ffffff,
	   0x00ffffff,
	   PCRELOFFSET),
    HOWTO (ARM_DISP8,
	   0,
	   0,
	   8,
	   TRUE,
	   0,
	   complain_overflow_signed,
	   coff_arm_reloc,
	   "ARM_DISP8",
	   TRUE,
	   0x000000ff,
	   0x000000ff,
	   TRUE),
    HOWTO (ARM_DISP16,
	   0,
	   1,
	   16,
	   TRUE,
	   0,
	   complain_overflow_signed,
	   coff_arm_reloc,
	   "ARM_DISP16",
	   TRUE,
	   0x0000ffff,
	   0x0000ffff,
	   TRUE),
    HOWTO (ARM_DISP32,
	   0,
	   2,
	   32,
	   TRUE,
	   0,
	   complain_overflow_signed,
	   coff_arm_reloc,
	   "ARM_DISP32",
	   TRUE,
	   0xffffffff,
	   0xffffffff,
	   TRUE),
    HOWTO (ARM_26D,
	   2,
	   2,
	   24,
	   FALSE,
	   0,
	   complain_overflow_dont,
	   aoutarm_fix_pcrel_26_done,
	   "ARM_26D",
	   TRUE,
	   0x00ffffff,
	   0x0,
	   FALSE),
    /* 8 is unused */
    EMPTY_HOWTO (-1),
    HOWTO (ARM_NEG16,
	   0,
	   -1,
	   16,
	   FALSE,
	   0,
	   complain_overflow_bitfield,
	   coff_arm_reloc,
	   "ARM_NEG16",
	   TRUE,
	   0x0000ffff,
	   0x0000ffff,
	   FALSE),
    HOWTO (ARM_NEG32,
	   0,
	   -2,
	   32,
	   FALSE,
	   0,
	   complain_overflow_bitfield,
	   coff_arm_reloc,
	   "ARM_NEG32",
	   TRUE,
	   0xffffffff,
	   0xffffffff,
	   FALSE),
    HOWTO (ARM_RVA32,
	   0,
	   2,
	   32,
	   FALSE,
	   0,
	   complain_overflow_bitfield,
	   coff_arm_reloc,
	   "ARM_RVA32",
	   TRUE,
	   0xffffffff,
	   0xffffffff,
	   PCRELOFFSET),
    HOWTO (ARM_THUMB9,
	   1,
	   1,
	   8,
	   TRUE,
	   0,
	   complain_overflow_signed,
	   coff_thumb_pcrel_9 ,
	   "ARM_THUMB9",
	   FALSE,
	   0x000000ff,
	   0x000000ff,
	   PCRELOFFSET),
    HOWTO (ARM_THUMB12,
	   1,
	   1,
	   11,
	   TRUE,
	   0,
	   complain_overflow_signed,
	   coff_thumb_pcrel_12 ,
	   "ARM_THUMB12",
	   FALSE,
	   0x000007ff,
	   0x000007ff,
	   PCRELOFFSET),
    HOWTO (ARM_THUMB23,
	   1,
	   2,
	   22,
	   TRUE,
	   0,
	   complain_overflow_signed,
	   coff_thumb_pcrel_23 ,
	   "ARM_THUMB23",
	   FALSE,
	   0x07ff07ff,
	   0x07ff07ff,
	   PCRELOFFSET)
#endif /* not ARM_WINCE */
  };

#define NUM_RELOCS NUM_ELEM (aoutarm_std_reloc_howto)

#ifdef COFF_WITH_PE
/* Return TRUE if this relocation should
   appear in the output .reloc section.  */

static bfd_boolean
in_reloc_p (bfd * abfd ATTRIBUTE_UNUSED,
	    reloc_howto_type * howto)
{
  return !howto->pc_relative && howto->type != ARM_RVA32;
}
#endif

#define RTYPE2HOWTO(cache_ptr, dst)		\
  (cache_ptr)->howto =				\
    (dst)->r_type < NUM_RELOCS			\
    ? aoutarm_std_reloc_howto + (dst)->r_type	\
    : NULL

#define coff_rtype_to_howto coff_arm_rtype_to_howto

static reloc_howto_type *
coff_arm_rtype_to_howto (bfd *abfd ATTRIBUTE_UNUSED,
			 asection *sec,
			 struct internal_reloc *rel,
			 struct coff_link_hash_entry *h ATTRIBUTE_UNUSED,
			 struct internal_syment *sym ATTRIBUTE_UNUSED,
			 bfd_vma *addendp)
{
  reloc_howto_type * howto;

  if (rel->r_type >= NUM_RELOCS)
    return NULL;

  howto = aoutarm_std_reloc_howto + rel->r_type;

  if (rel->r_type == ARM_RVA32)
    *addendp -= pe_data (sec->output_section->owner)->pe_opthdr.ImageBase;

#if defined COFF_WITH_PE && defined ARM_WINCE
  if (rel->r_type == ARM_SECREL)
    {
      bfd_vma osect_vma;

      if (h && (h->type == bfd_link_hash_defined
		|| h->type == bfd_link_hash_defweak))
	osect_vma = h->root.u.def.section->output_section->vma;
      else
	{
	  asection *sec;
	  int i;

	  /* Sigh, the only way to get the section to offset against
	     is to find it the hard way.  */

	  for (sec = abfd->sections, i = 1; i < sym->n_scnum; i++)
	    sec = sec->next;

	  osect_vma = sec->output_section->vma;
	}

      *addendp -= osect_vma;
    }
#endif

  return howto;
}

/* Used by the assembler.  */

static bfd_reloc_status_type
aoutarm_fix_pcrel_26_done (bfd *abfd ATTRIBUTE_UNUSED,
			   arelent *reloc_entry ATTRIBUTE_UNUSED,
			   asymbol *symbol ATTRIBUTE_UNUSED,
			   void * data ATTRIBUTE_UNUSED,
			   asection *input_section ATTRIBUTE_UNUSED,
			   bfd *output_bfd ATTRIBUTE_UNUSED,
			   char **error_message ATTRIBUTE_UNUSED)
{
  /* This is dead simple at present.  */
  return bfd_reloc_ok;
}

/* Used by the assembler.  */

static bfd_reloc_status_type
aoutarm_fix_pcrel_26 (bfd *abfd,
		      arelent *reloc_entry,
		      asymbol *symbol,
		      void * data,
		      asection *input_section,
		      bfd *output_bfd,
		      char **error_message ATTRIBUTE_UNUSED)
{
  bfd_vma relocation;
  bfd_size_type addr = reloc_entry->address;
  long target = bfd_get_32 (abfd, (bfd_byte *) data + addr);
  bfd_reloc_status_type flag = bfd_reloc_ok;

  /* If this is an undefined symbol, return error.  */
  if (symbol->section == &bfd_und_section
      && (symbol->flags & BSF_WEAK) == 0)
    return output_bfd ? bfd_reloc_continue : bfd_reloc_undefined;

  /* If the sections are different, and we are doing a partial relocation,
     just ignore it for now.  */
  if (symbol->section->name != input_section->name
      && output_bfd != (bfd *)NULL)
    return bfd_reloc_continue;

  relocation = (target & 0x00ffffff) << 2;
  relocation = (relocation ^ 0x02000000) - 0x02000000; /* Sign extend.  */
  relocation += symbol->value;
  relocation += symbol->section->output_section->vma;
  relocation += symbol->section->output_offset;
  relocation += reloc_entry->addend;
  relocation -= input_section->output_section->vma;
  relocation -= input_section->output_offset;
  relocation -= addr;

  if (relocation & 3)
    return bfd_reloc_overflow;

  /* Check for overflow.  */
  if (relocation & 0x02000000)
    {
      if ((relocation & ~ (bfd_vma) 0x03ffffff) != ~ (bfd_vma) 0x03ffffff)
	flag = bfd_reloc_overflow;
    }
  else if (relocation & ~(bfd_vma) 0x03ffffff)
    flag = bfd_reloc_overflow;

  target &= ~0x00ffffff;
  target |= (relocation >> 2) & 0x00ffffff;
  bfd_put_32 (abfd, (bfd_vma) target, (bfd_byte *) data + addr);

  /* Now the ARM magic... Change the reloc type so that it is marked as done.
     Strictly this is only necessary if we are doing a partial relocation.  */
  reloc_entry->howto = &aoutarm_std_reloc_howto[ARM_26D];

  return flag;
}

static bfd_reloc_status_type
coff_thumb_pcrel_common (bfd *abfd,
			 arelent *reloc_entry,
			 asymbol *symbol,
			 void * data,
			 asection *input_section,
			 bfd *output_bfd,
			 char **error_message ATTRIBUTE_UNUSED,
			 thumb_pcrel_branchtype btype)
{
  bfd_vma relocation = 0;
  bfd_size_type addr = reloc_entry->address;
  long target = bfd_get_32 (abfd, (bfd_byte *) data + addr);
  bfd_reloc_status_type flag = bfd_reloc_ok;
  bfd_vma dstmsk;
  bfd_vma offmsk;
  bfd_vma signbit;

  /* NOTE: This routine is currently used by GAS, but not by the link
     phase.  */
  switch (btype)
    {
    case b9:
      dstmsk  = 0x000000ff;
      offmsk  = 0x000001fe;
      signbit = 0x00000100;
      break;

    case b12:
      dstmsk  = 0x000007ff;
      offmsk  = 0x00000ffe;
      signbit = 0x00000800;
      break;

    case b23:
      dstmsk  = 0x07ff07ff;
      offmsk  = 0x007fffff;
      signbit = 0x00400000;
      break;

    default:
      abort ();
    }

  /* If this is an undefined symbol, return error.  */
  if (symbol->section == &bfd_und_section
      && (symbol->flags & BSF_WEAK) == 0)
    return output_bfd ? bfd_reloc_continue : bfd_reloc_undefined;

  /* If the sections are different, and we are doing a partial relocation,
     just ignore it for now.  */
  if (symbol->section->name != input_section->name
      && output_bfd != (bfd *)NULL)
    return bfd_reloc_continue;

  switch (btype)
    {
    case b9:
    case b12:
      relocation = ((target & dstmsk) << 1);
      break;

    case b23:
      if (bfd_big_endian (abfd))
	relocation = ((target & 0x7ff) << 1)  | ((target & 0x07ff0000) >> 4);
      else
	relocation = ((target & 0x7ff) << 12) | ((target & 0x07ff0000) >> 15);
      break;

    default:
      abort ();
    }

  relocation = (relocation ^ signbit) - signbit; /* Sign extend.  */
  relocation += symbol->value;
  relocation += symbol->section->output_section->vma;
  relocation += symbol->section->output_offset;
  relocation += reloc_entry->addend;
  relocation -= input_section->output_section->vma;
  relocation -= input_section->output_offset;
  relocation -= addr;

  if (relocation & 1)
    return bfd_reloc_overflow;

  /* Check for overflow.  */
  if (relocation & signbit)
    {
      if ((relocation & ~offmsk) != ~offmsk)
	flag = bfd_reloc_overflow;
    }
  else if (relocation & ~offmsk)
    flag = bfd_reloc_overflow;

  target &= ~dstmsk;
  switch (btype)
   {
   case b9:
   case b12:
     target |= (relocation >> 1);
     break;

   case b23:
     if (bfd_big_endian (abfd))
       target |= (((relocation & 0xfff) >> 1)
		  | ((relocation << 4)  & 0x07ff0000));
     else
       target |= (((relocation & 0xffe) << 15)
		  | ((relocation >> 12) & 0x7ff));
     break;

   default:
     abort ();
   }

  bfd_put_32 (abfd, (bfd_vma) target, (bfd_byte *) data + addr);

  /* Now the ARM magic... Change the reloc type so that it is marked as done.
     Strictly this is only necessary if we are doing a partial relocation.  */
  reloc_entry->howto = & aoutarm_std_reloc_howto [ARM_26D];

  /* TODO: We should possibly have DONE entries for the THUMB PCREL relocations.  */
  return flag;
}

#ifndef ARM_WINCE
static bfd_reloc_status_type
coff_thumb_pcrel_23 (bfd *abfd,
		     arelent *reloc_entry,
		     asymbol *symbol,
		     void * data,
		     asection *input_section,
		     bfd *output_bfd,
		     char **error_message)
{
  return coff_thumb_pcrel_common (abfd, reloc_entry, symbol, data,
                                  input_section, output_bfd, error_message,
				  b23);
}

static bfd_reloc_status_type
coff_thumb_pcrel_9 (bfd *abfd,
		    arelent *reloc_entry,
		    asymbol *symbol,
		    void * data,
		    asection *input_section,
		    bfd *output_bfd,
		    char **error_message)
{
  return coff_thumb_pcrel_common (abfd, reloc_entry, symbol, data,
                                  input_section, output_bfd, error_message,
				  b9);
}
#endif /* not ARM_WINCE */

static bfd_reloc_status_type
coff_thumb_pcrel_12 (bfd *abfd,
		     arelent *reloc_entry,
		     asymbol *symbol,
		     void * data,
		     asection *input_section,
		     bfd *output_bfd,
		     char **error_message)
{
  return coff_thumb_pcrel_common (abfd, reloc_entry, symbol, data,
                                  input_section, output_bfd, error_message,
				  b12);
}

static const struct reloc_howto_struct *
coff_arm_reloc_type_lookup (bfd * abfd, bfd_reloc_code_real_type code)
{
#define ASTD(i,j)       case i: return aoutarm_std_reloc_howto + j

  if (code == BFD_RELOC_CTOR)
    switch (bfd_get_arch_info (abfd)->bits_per_address)
      {
      case 32:
        code = BFD_RELOC_32;
        break;
      default:
	return NULL;
      }

  switch (code)
    {
#ifdef ARM_WINCE
      ASTD (BFD_RELOC_32,                   ARM_32);
      ASTD (BFD_RELOC_RVA,                  ARM_RVA32);
      ASTD (BFD_RELOC_ARM_PCREL_BRANCH,     ARM_26);
      ASTD (BFD_RELOC_THUMB_PCREL_BRANCH12, ARM_THUMB12);
      ASTD (BFD_RELOC_32_SECREL,            ARM_SECREL);
#else
      ASTD (BFD_RELOC_8,                    ARM_8);
      ASTD (BFD_RELOC_16,                   ARM_16);
      ASTD (BFD_RELOC_32,                   ARM_32);
      ASTD (BFD_RELOC_ARM_PCREL_BRANCH,     ARM_26);
      ASTD (BFD_RELOC_ARM_PCREL_BLX,        ARM_26);
      ASTD (BFD_RELOC_8_PCREL,              ARM_DISP8);
      ASTD (BFD_RELOC_16_PCREL,             ARM_DISP16);
      ASTD (BFD_RELOC_32_PCREL,             ARM_DISP32);
      ASTD (BFD_RELOC_RVA,                  ARM_RVA32);
      ASTD (BFD_RELOC_THUMB_PCREL_BRANCH9,  ARM_THUMB9);
      ASTD (BFD_RELOC_THUMB_PCREL_BRANCH12, ARM_THUMB12);
      ASTD (BFD_RELOC_THUMB_PCREL_BRANCH23, ARM_THUMB23);
      ASTD (BFD_RELOC_THUMB_PCREL_BLX,      ARM_THUMB23);
#endif
    default: return NULL;
    }
}

static reloc_howto_type *
coff_arm_reloc_name_lookup (bfd *abfd ATTRIBUTE_UNUSED,
			    const char *r_name)
{
  unsigned int i;

  for (i = 0;
       i < (sizeof (aoutarm_std_reloc_howto)
	    / sizeof (aoutarm_std_reloc_howto[0]));
       i++)
    if (aoutarm_std_reloc_howto[i].name != NULL
	&& strcasecmp (aoutarm_std_reloc_howto[i].name, r_name) == 0)
      return &aoutarm_std_reloc_howto[i];

  return NULL;
}

#define COFF_DEFAULT_SECTION_ALIGNMENT_POWER  2
#define COFF_PAGE_SIZE                        0x1000

/* Turn a howto into a reloc  nunmber.  */
#define SELECT_RELOC(x,howto) { x.r_type = howto->type; }
#define BADMAG(x)             ARMBADMAG(x)
#define ARM                   1			/* Customize coffcode.h.  */

#ifndef ARM_WINCE
/* Make sure that the 'r_offset' field is copied properly
   so that identical binaries will compare the same.  */
#define SWAP_IN_RELOC_OFFSET	H_GET_32
#define SWAP_OUT_RELOC_OFFSET	H_PUT_32
#endif

/* Extend the coff_link_hash_table structure with a few ARM specific fields.
   This allows us to store global data here without actually creating any
   global variables, which is a no-no in the BFD world.  */
struct coff_arm_link_hash_table
  {
    /* The original coff_link_hash_table structure.  MUST be first field.  */
    struct coff_link_hash_table	root;

    /* The size in bytes of the section containing the Thumb-to-ARM glue.  */
    bfd_size_type		thumb_glue_size;

    /* The size in bytes of the section containing the ARM-to-Thumb glue.  */
    bfd_size_type		arm_glue_size;

    /* An arbitrary input BFD chosen to hold the glue sections.  */
    bfd *			bfd_of_glue_owner;

    /* Support interworking with old, non-interworking aware ARM code.  */
    int 			support_old_code;
};

/* Get the ARM coff linker hash table from a link_info structure.  */
#define coff_arm_hash_table(info) \
  ((struct coff_arm_link_hash_table *) ((info)->hash))

/* Create an ARM coff linker hash table.  */

static struct bfd_link_hash_table *
coff_arm_link_hash_table_create (bfd * abfd)
{
  struct coff_arm_link_hash_table * ret;
  bfd_size_type amt = sizeof (struct coff_arm_link_hash_table);

  ret = bfd_malloc (amt);
  if (ret == NULL)
    return NULL;

  if (!_bfd_coff_link_hash_table_init (&ret->root,
				       abfd,
				       _bfd_coff_link_hash_newfunc,
				       sizeof (struct coff_link_hash_entry)))
    {
      free (ret);
      return NULL;
    }

  ret->thumb_glue_size   = 0;
  ret->arm_glue_size     = 0;
  ret->bfd_of_glue_owner = NULL;

  return & ret->root.root;
}

static void
arm_emit_base_file_entry (struct bfd_link_info *info,
			  bfd *output_bfd,
			  asection *input_section,
			  bfd_vma reloc_offset)
{
  bfd_vma addr = reloc_offset
                - input_section->vma
                + input_section->output_offset
                  + input_section->output_section->vma;

  if (coff_data (output_bfd)->pe)
     addr -= pe_data (output_bfd)->pe_opthdr.ImageBase;
  fwrite (& addr, 1, sizeof (addr), (FILE *) info->base_file);

}

#ifndef ARM_WINCE
/* The thumb form of a long branch is a bit finicky, because the offset
   encoding is split over two fields, each in it's own instruction. They
   can occur in any order. So given a thumb form of long branch, and an
   offset, insert the offset into the thumb branch and return finished
   instruction.

   It takes two thumb instructions to encode the target address. Each has
   11 bits to invest. The upper 11 bits are stored in one (identified by
   H-0.. see below), the lower 11 bits are stored in the other (identified
   by H-1).

   Combine together and shifted left by 1 (it's a half word address) and
   there you have it.

     Op: 1111 = F,
     H-0, upper address-0 = 000
     Op: 1111 = F,
     H-1, lower address-0 = 800

   They can be ordered either way, but the arm tools I've seen always put
   the lower one first. It probably doesn't matter. krk@cygnus.com

   XXX:  Actually the order does matter.  The second instruction (H-1)
   moves the computed address into the PC, so it must be the second one
   in the sequence.  The problem, however is that whilst little endian code
   stores the instructions in HI then LOW order, big endian code does the
   reverse.  nickc@cygnus.com.  */

#define LOW_HI_ORDER 0xF800F000
#define HI_LOW_ORDER 0xF000F800

static insn32
insert_thumb_branch (insn32 br_insn, int rel_off)
{
  unsigned int low_bits;
  unsigned int high_bits;

  BFD_ASSERT ((rel_off & 1) != 1);

  rel_off >>= 1;                              /* Half word aligned address.  */
  low_bits = rel_off & 0x000007FF;            /* The bottom 11 bits.  */
  high_bits = (rel_off >> 11) & 0x000007FF;   /* The top 11 bits.  */

  if ((br_insn & LOW_HI_ORDER) == LOW_HI_ORDER)
    br_insn = LOW_HI_ORDER | (low_bits << 16) | high_bits;
  else if ((br_insn & HI_LOW_ORDER) == HI_LOW_ORDER)
    br_insn = HI_LOW_ORDER | (high_bits << 16) | low_bits;
  else
    /* FIXME: the BFD library should never abort except for internal errors
       - it should return an error status.  */
    abort (); /* Error - not a valid branch instruction form.  */

  return br_insn;
}


static struct coff_link_hash_entry *
find_thumb_glue (struct bfd_link_info *info,
		 const char *name,
		 bfd *input_bfd)
{
  char *tmp_name;
  struct coff_link_hash_entry *myh;
  bfd_size_type amt = strlen (name) + strlen (THUMB2ARM_GLUE_ENTRY_NAME) + 1;

  tmp_name = bfd_malloc (amt);

  BFD_ASSERT (tmp_name);

  sprintf (tmp_name, THUMB2ARM_GLUE_ENTRY_NAME, name);

  myh = coff_link_hash_lookup
    (coff_hash_table (info), tmp_name, FALSE, FALSE, TRUE);

  if (myh == NULL)
    /* xgettext:c-format */
    _bfd_error_handler (_("%B: unable to find THUMB glue '%s' for `%s'"),
			input_bfd, tmp_name, name);

  free (tmp_name);

  return myh;
}
#endif /* not ARM_WINCE */

static struct coff_link_hash_entry *
find_arm_glue (struct bfd_link_info *info,
	       const char *name,
	       bfd *input_bfd)
{
  char *tmp_name;
  struct coff_link_hash_entry * myh;
  bfd_size_type amt = strlen (name) + strlen (ARM2THUMB_GLUE_ENTRY_NAME) + 1;

  tmp_name = bfd_malloc (amt);

  BFD_ASSERT (tmp_name);

  sprintf (tmp_name, ARM2THUMB_GLUE_ENTRY_NAME, name);

  myh = coff_link_hash_lookup
    (coff_hash_table (info), tmp_name, FALSE, FALSE, TRUE);

  if (myh == NULL)
    /* xgettext:c-format */
    _bfd_error_handler (_("%B: unable to find ARM glue '%s' for `%s'"),
			input_bfd, tmp_name, name);

  free (tmp_name);

  return myh;
}

/*
  ARM->Thumb glue:

       .arm
       __func_from_arm:
	     ldr r12, __func_addr
	     bx  r12
       __func_addr:
            .word func    @ behave as if you saw a ARM_32 reloc
*/

#define ARM2THUMB_GLUE_SIZE 12
static const insn32 a2t1_ldr_insn       = 0xe59fc000;
static const insn32 a2t2_bx_r12_insn    = 0xe12fff1c;
static const insn32 a2t3_func_addr_insn = 0x00000001;

/*
   Thumb->ARM:				Thumb->(non-interworking aware) ARM

   .thumb				.thumb
   .align 2				.align 2
      __func_from_thumb:		   __func_from_thumb:
	   bx pc				push {r6, lr}
	   nop					ldr  r6, __func_addr
   .arm						mov  lr, pc
      __func_change_to_arm:			bx   r6
	   b func   			.arm
					   __func_back_to_thumb:
   		  				ldmia r13! {r6, lr}
   					        bx    lr
   					   __func_addr:
					        .word	func
*/

#define THUMB2ARM_GLUE_SIZE (globals->support_old_code ? 20 : 8)
#ifndef ARM_WINCE
static const insn16 t2a1_bx_pc_insn = 0x4778;
static const insn16 t2a2_noop_insn  = 0x46c0;
static const insn32 t2a3_b_insn     = 0xea000000;

static const insn16 t2a1_push_insn  = 0xb540;
static const insn16 t2a2_ldr_insn   = 0x4e03;
static const insn16 t2a3_mov_insn   = 0x46fe;
static const insn16 t2a4_bx_insn    = 0x4730;
static const insn32 t2a5_pop_insn   = 0xe8bd4040;
static const insn32 t2a6_bx_insn    = 0xe12fff1e;
#endif

/* TODO:
     We should really create new local (static) symbols in destination
     object for each stub we create.  We should also create local
     (static) symbols within the stubs when switching between ARM and
     Thumb code.  This will ensure that the debugger and disassembler
     can present a better view of stubs.

     We can treat stubs like literal sections, and for the THUMB9 ones
     (short addressing range) we should be able to insert the stubs
     between sections. i.e. the simplest approach (since relocations
     are done on a section basis) is to dump the stubs at the end of
     processing a section. That way we can always try and minimise the
     offset to and from a stub. However, this does not map well onto
     the way that the linker/BFD does its work: mapping all input
     sections to output sections via the linker script before doing
     all the processing.

     Unfortunately it may be easier to just to disallow short range
     Thumb->ARM stubs (i.e. no conditional inter-working branches,
     only branch-and-link (BL) calls.  This will simplify the processing
     since we can then put all of the stubs into their own section.

  TODO:
     On a different subject, rather than complaining when a
     branch cannot fit in the number of bits available for the
     instruction we should generate a trampoline stub (needed to
     address the complete 32bit address space).  */

/* The standard COFF backend linker does not cope with the special
   Thumb BRANCH23 relocation.  The alternative would be to split the
   BRANCH23 into seperate HI23 and LO23 relocations. However, it is a
   bit simpler simply providing our own relocation driver.  */

/* The reloc processing routine for the ARM/Thumb COFF linker.  NOTE:
   This code is a very slightly modified copy of
   _bfd_coff_generic_relocate_section.  It would be a much more
   maintainable solution to have a MACRO that could be expanded within
   _bfd_coff_generic_relocate_section that would only be provided for
   ARM/Thumb builds.  It is only the code marked THUMBEXTENSION that
   is different from the original.  */

static bfd_boolean
coff_arm_relocate_section (bfd *output_bfd,
			   struct bfd_link_info *info,
			   bfd *input_bfd,
			   asection *input_section,
			   bfd_byte *contents,
			   struct internal_reloc *relocs,
			   struct internal_syment *syms,
			   asection **sections)
{
  struct internal_reloc * rel;
  struct internal_reloc * relend;
#ifndef ARM_WINCE
  bfd_vma high_address = bfd_get_section_limit (input_bfd, input_section);
#endif

  rel = relocs;
  relend = rel + input_section->reloc_count;

  for (; rel < relend; rel++)
    {
      int                            done = 0;
      long                           symndx;
      struct coff_link_hash_entry *  h;
      struct internal_syment *       sym;
      bfd_vma                        addend;
      bfd_vma                        val;
      reloc_howto_type *             howto;
      bfd_reloc_status_type          rstat;
      bfd_vma                        h_val;

      symndx = rel->r_symndx;

      if (symndx == -1)
	{
	  h = NULL;
	  sym = NULL;
	}
      else
	{
	  h = obj_coff_sym_hashes (input_bfd)[symndx];
	  sym = syms + symndx;
	}

      /* COFF treats common symbols in one of two ways.  Either the
         size of the symbol is included in the section contents, or it
         is not.  We assume that the size is not included, and force
         the rtype_to_howto function to adjust the addend as needed.  */

      if (sym != NULL && sym->n_scnum != 0)
	addend = - sym->n_value;
      else
	addend = 0;

      howto = coff_rtype_to_howto (input_bfd, input_section, rel, h,
				       sym, &addend);
      if (howto == NULL)
	return FALSE;

      /* The relocation_section function will skip pcrel_offset relocs
         when doing a relocatable link.  However, we want to convert
         ARM_26 to ARM_26D relocs if possible.  We return a fake howto in
         this case without pcrel_offset set, and adjust the addend to
         compensate.  'partial_inplace' is also set, since we want 'done'
         relocations to be reflected in section's data.  */
      if (rel->r_type == ARM_26
          && h != NULL
          && info->relocatable
          && (h->root.type == bfd_link_hash_defined
	      || h->root.type == bfd_link_hash_defweak)
          && (h->root.u.def.section->output_section
	      == input_section->output_section))
        {
          static reloc_howto_type fake_arm26_reloc =
	    HOWTO (ARM_26,
    	       2,
    	       2,
    	       24,
    	       TRUE,
    	       0,
    	       complain_overflow_signed,
    	       aoutarm_fix_pcrel_26 ,
    	       "ARM_26",
    	       TRUE,
    	       0x00ffffff,
    	       0x00ffffff,
    	       FALSE);

          addend -= rel->r_vaddr - input_section->vma;
#ifdef ARM_WINCE
          /* FIXME: I don't know why, but the hack is necessary for correct
                    generation of bl's instruction offset.  */
          addend -= 8;
#endif
          howto = & fake_arm26_reloc;
        }

#ifdef ARM_WINCE
      /* MS ARM-CE makes the reloc relative to the opcode's pc, not
	 the next opcode's pc, so is off by one.  */
      if (howto->pc_relative && !info->relocatable)
	addend -= 8;
#endif

      /* If we are doing a relocatable link, then we can just ignore
         a PC relative reloc that is pcrel_offset.  It will already
         have the correct value.  If this is not a relocatable link,
         then we should ignore the symbol value.  */
      if (howto->pc_relative && howto->pcrel_offset)
        {
          if (info->relocatable)
            continue;
	  /* FIXME - it is not clear which targets need this next test
	     and which do not.  It is known that it is needed for the
	     VxWorks and EPOC-PE targets, but it is also known that it
	     was suppressed for other ARM targets.  This ought to be
	     sorted out one day.  */
#ifdef ARM_COFF_BUGFIX
	  /* We must not ignore the symbol value.  If the symbol is
	     within the same section, the relocation should have already
	     been fixed, but if it is not, we'll be handed a reloc into
	     the beginning of the symbol's section, so we must not cancel
	     out the symbol's value, otherwise we'll be adding it in
	     twice.  */
          if (sym != NULL && sym->n_scnum != 0)
            addend += sym->n_value;
#endif
        }

      val = 0;

      if (h == NULL)
	{
	  asection *sec;

	  if (symndx == -1)
	    {
	      sec = bfd_abs_section_ptr;
	      val = 0;
	    }
	  else
	    {
	      sec = sections[symndx];
              val = (sec->output_section->vma
		     + sec->output_offset
		     + sym->n_value
		     - sec->vma);
	    }
	}
      else
	{
          /* We don't output the stubs if we are generating a
             relocatable output file, since we may as well leave the
             stub generation to the final linker pass. If we fail to
	     verify that the name is defined, we'll try to build stubs
	     for an undefined name...  */
          if (! info->relocatable
	      && (   h->root.type == bfd_link_hash_defined
		  || h->root.type == bfd_link_hash_defweak))
            {
	      asection *   h_sec = h->root.u.def.section;
	      const char * name  = h->root.root.string;

	      /* h locates the symbol referenced in the reloc.  */
	      h_val = (h->root.u.def.value
		       + h_sec->output_section->vma
		       + h_sec->output_offset);

              if (howto->type == ARM_26)
                {
                  if (   h->class == C_THUMBSTATFUNC
		      || h->class == C_THUMBEXTFUNC)
		    {
		      /* Arm code calling a Thumb function.  */
		      unsigned long int                 tmp;
		      bfd_vma                           my_offset;
		      asection *                        s;
		      long int                          ret_offset;
		      struct coff_link_hash_entry *     myh;
		      struct coff_arm_link_hash_table * globals;

		      myh = find_arm_glue (info, name, input_bfd);
		      if (myh == NULL)
			return FALSE;

		      globals = coff_arm_hash_table (info);

		      BFD_ASSERT (globals != NULL);
		      BFD_ASSERT (globals->bfd_of_glue_owner != NULL);

		      my_offset = myh->root.u.def.value;

		      s = bfd_get_section_by_name (globals->bfd_of_glue_owner,
						  ARM2THUMB_GLUE_SECTION_NAME);
		      BFD_ASSERT (s != NULL);
		      BFD_ASSERT (s->contents != NULL);
		      BFD_ASSERT (s->output_section != NULL);

		      if ((my_offset & 0x01) == 0x01)
			{
			  if (h_sec->owner != NULL
			      && INTERWORK_SET (h_sec->owner)
			      && ! INTERWORK_FLAG (h_sec->owner))
			    _bfd_error_handler
			      /* xgettext:c-format */
			      (_("%B(%s): warning: interworking not enabled.\n"
				 "  first occurrence: %B: arm call to thumb"),
			       h_sec->owner, input_bfd, name);

			  --my_offset;
			  myh->root.u.def.value = my_offset;

			  bfd_put_32 (output_bfd, (bfd_vma) a2t1_ldr_insn,
				      s->contents + my_offset);

			  bfd_put_32 (output_bfd, (bfd_vma) a2t2_bx_r12_insn,
				      s->contents + my_offset + 4);

			  /* It's a thumb address.  Add the low order bit.  */
			  bfd_put_32 (output_bfd, h_val | a2t3_func_addr_insn,
				      s->contents + my_offset + 8);

                          if (info->base_file)
                            arm_emit_base_file_entry (info, output_bfd, s,
                                                      my_offset + 8);

			}

		      BFD_ASSERT (my_offset <= globals->arm_glue_size);

		      tmp = bfd_get_32 (input_bfd, contents + rel->r_vaddr
					- input_section->vma);

		      tmp = tmp & 0xFF000000;

		      /* Somehow these are both 4 too far, so subtract 8.  */
		      ret_offset =
			s->output_offset
			+ my_offset
			+ s->output_section->vma
			- (input_section->output_offset
			   + input_section->output_section->vma
			   + rel->r_vaddr)
			- 8;

		      tmp = tmp | ((ret_offset >> 2) & 0x00FFFFFF);

		      bfd_put_32 (output_bfd, (bfd_vma) tmp,
				  contents + rel->r_vaddr - input_section->vma);
		      done = 1;
		    }
                }

#ifndef ARM_WINCE
	      /* Note: We used to check for ARM_THUMB9 and ARM_THUMB12.  */
              else if (howto->type == ARM_THUMB23)
                {
                  if (   h->class == C_EXT
		      || h->class == C_STAT
		      || h->class == C_LABEL)
		    {
		      /* Thumb code calling an ARM function.  */
		      asection *                         s = 0;
		      bfd_vma                            my_offset;
		      unsigned long int                  tmp;
		      long int                           ret_offset;
		      struct coff_link_hash_entry *      myh;
		      struct coff_arm_link_hash_table *  globals;

		      myh = find_thumb_glue (info, name, input_bfd);
		      if (myh == NULL)
			return FALSE;

		      globals = coff_arm_hash_table (info);

		      BFD_ASSERT (globals != NULL);
		      BFD_ASSERT (globals->bfd_of_glue_owner != NULL);

		      my_offset = myh->root.u.def.value;

		      s = bfd_get_section_by_name (globals->bfd_of_glue_owner,
						   THUMB2ARM_GLUE_SECTION_NAME);

		      BFD_ASSERT (s != NULL);
		      BFD_ASSERT (s->contents != NULL);
		      BFD_ASSERT (s->output_section != NULL);

		      if ((my_offset & 0x01) == 0x01)
			{
			  if (h_sec->owner != NULL
			      && INTERWORK_SET (h_sec->owner)
			      && ! INTERWORK_FLAG (h_sec->owner)
			      && ! globals->support_old_code)
			    _bfd_error_handler
			      /* xgettext:c-format */
			      (_("%B(%s): warning: interworking not enabled.\n"
				 "  first occurrence: %B: thumb call to arm\n"
				 "  consider relinking with --support-old-code enabled"),
			       h_sec->owner, input_bfd, name);

			  -- my_offset;
			  myh->root.u.def.value = my_offset;

			  if (globals->support_old_code)
			    {
			      bfd_put_16 (output_bfd, (bfd_vma) t2a1_push_insn,
					  s->contents + my_offset);

			      bfd_put_16 (output_bfd, (bfd_vma) t2a2_ldr_insn,
					  s->contents + my_offset + 2);

			      bfd_put_16 (output_bfd, (bfd_vma) t2a3_mov_insn,
					  s->contents + my_offset + 4);

			      bfd_put_16 (output_bfd, (bfd_vma) t2a4_bx_insn,
					  s->contents + my_offset + 6);

			      bfd_put_32 (output_bfd, (bfd_vma) t2a5_pop_insn,
					  s->contents + my_offset + 8);

			      bfd_put_32 (output_bfd, (bfd_vma) t2a6_bx_insn,
					  s->contents + my_offset + 12);

			      /* Store the address of the function in the last word of the stub.  */
			      bfd_put_32 (output_bfd, h_val,
					  s->contents + my_offset + 16);

                              if (info->base_file)
                                arm_emit_base_file_entry (info, output_bfd, s,
							  my_offset + 16);
			    }
			  else
			    {
			      bfd_put_16 (output_bfd, (bfd_vma) t2a1_bx_pc_insn,
					  s->contents + my_offset);

			      bfd_put_16 (output_bfd, (bfd_vma) t2a2_noop_insn,
					  s->contents + my_offset + 2);

			      ret_offset =
		/* Address of destination of the stub.  */
				((bfd_signed_vma) h_val)
				- ((bfd_signed_vma)
		/* Offset from the start of the current section to the start of the stubs.  */
				   (s->output_offset
		/* Offset of the start of this stub from the start of the stubs.  */
				    + my_offset
		/* Address of the start of the current section.  */
				    + s->output_section->vma)
		/* The branch instruction is 4 bytes into the stub.  */
				   + 4
		/* ARM branches work from the pc of the instruction + 8.  */
				   + 8);

			      bfd_put_32 (output_bfd,
					  (bfd_vma) t2a3_b_insn | ((ret_offset >> 2) & 0x00FFFFFF),
					  s->contents + my_offset + 4);

			    }
			}

		      BFD_ASSERT (my_offset <= globals->thumb_glue_size);

		      /* Now go back and fix up the original BL insn to point
			 to here.  */
		      ret_offset =
			s->output_offset
			+ my_offset
			- (input_section->output_offset
			   + rel->r_vaddr)
			-4;

		      tmp = bfd_get_32 (input_bfd, contents + rel->r_vaddr
					- input_section->vma);

		      bfd_put_32 (output_bfd,
				  (bfd_vma) insert_thumb_branch (tmp,
								 ret_offset),
				  contents + rel->r_vaddr - input_section->vma);

		      done = 1;
                    }
                }
#endif
            }

          /* If the relocation type and destination symbol does not
             fall into one of the above categories, then we can just
             perform a direct link.  */

	  if (done)
	    rstat = bfd_reloc_ok;
	  else
	    if (   h->root.type == bfd_link_hash_defined
		|| h->root.type == bfd_link_hash_defweak)
	    {
	      asection *sec;

	      sec = h->root.u.def.section;
	      val = (h->root.u.def.value
		     + sec->output_section->vma
		     + sec->output_offset);
	      }

	  else if (! info->relocatable)
	    {
	      if (! ((*info->callbacks->undefined_symbol)
		     (info, h->root.root.string, input_bfd, input_section,
		      rel->r_vaddr - input_section->vma, TRUE)))
		return FALSE;
	    }
	}

      if (info->base_file)
	{
	  /* Emit a reloc if the backend thinks it needs it.  */
	  if (sym && pe_data(output_bfd)->in_reloc_p(output_bfd, howto))
            arm_emit_base_file_entry (info, output_bfd, input_section,
				      rel->r_vaddr);
	}

      if (done)
	rstat = bfd_reloc_ok;
#ifndef ARM_WINCE
      /* Only perform this fix during the final link, not a relocatable link.  */
      else if (! info->relocatable
	       && howto->type == ARM_THUMB23)
        {
          /* This is pretty much a copy of what the default
             _bfd_final_link_relocate and _bfd_relocate_contents
             routines do to perform a relocation, with special
             processing for the split addressing of the Thumb BL
             instruction.  Again, it would probably be simpler adding a
             ThumbBRANCH23 specific macro expansion into the default
             code.  */

          bfd_vma address = rel->r_vaddr - input_section->vma;

	  if (address > high_address)
	    rstat = bfd_reloc_outofrange;
          else
            {
              bfd_vma relocation = val + addend;
	      int size = bfd_get_reloc_size (howto);
	      bfd_boolean overflow = FALSE;
	      bfd_byte *location = contents + address;
	      bfd_vma x = bfd_get_32 (input_bfd, location);
	      bfd_vma src_mask = 0x007FFFFE;
	      bfd_signed_vma reloc_signed_max = (1 << (howto->bitsize - 1)) - 1;
	      bfd_signed_vma reloc_signed_min = ~reloc_signed_max;
	      bfd_vma check;
	      bfd_signed_vma signed_check;
	      bfd_vma add;
	      bfd_signed_vma signed_add;

	      BFD_ASSERT (size == 4);

              /* howto->pc_relative should be TRUE for type 14 BRANCH23.  */
              relocation -= (input_section->output_section->vma
                             + input_section->output_offset);

              /* howto->pcrel_offset should be TRUE for type 14 BRANCH23.  */
              relocation -= address;

	      /* No need to negate the relocation with BRANCH23.  */
	      /* howto->complain_on_overflow == complain_overflow_signed for BRANCH23.  */
	      /* howto->rightshift == 1 */

	      /* Drop unwanted bits from the value we are relocating to.  */
	      check = relocation >> howto->rightshift;

	      /* If this is a signed value, the rightshift just dropped
		 leading 1 bits (assuming twos complement).  */
	      if ((bfd_signed_vma) relocation >= 0)
		signed_check = check;
	      else
		signed_check = (check
				| ((bfd_vma) - 1
				   & ~((bfd_vma) - 1 >> howto->rightshift)));

	      /* Get the value from the object file.  */
	      if (bfd_big_endian (input_bfd))
		add = (((x) & 0x07ff0000) >> 4) | (((x) & 0x7ff) << 1);
	      else
		add = ((((x) & 0x7ff) << 12) | (((x) & 0x07ff0000) >> 15));

	      /* Get the value from the object file with an appropriate sign.
		 The expression involving howto->src_mask isolates the upper
		 bit of src_mask.  If that bit is set in the value we are
		 adding, it is negative, and we subtract out that number times
		 two.  If src_mask includes the highest possible bit, then we
		 can not get the upper bit, but that does not matter since
		 signed_add needs no adjustment to become negative in that
		 case.  */
	      signed_add = add;

	      if ((add & (((~ src_mask) >> 1) & src_mask)) != 0)
		signed_add -= (((~ src_mask) >> 1) & src_mask) << 1;

	      /* howto->bitpos == 0 */
	      /* Add the value from the object file, shifted so that it is a
		 straight number.  */
	      signed_check += signed_add;
	      relocation   += signed_add;

	      BFD_ASSERT (howto->complain_on_overflow == complain_overflow_signed);

	      /* Assumes two's complement.  */
	      if (   signed_check > reloc_signed_max
		  || signed_check < reloc_signed_min)
		overflow = TRUE;

	      /* Put the relocation into the correct bits.
		 For a BLX instruction, make sure that the relocation is rounded up
		 to a word boundary.  This follows the semantics of the instruction
		 which specifies that bit 1 of the target address will come from bit
		 1 of the base address.  */
	      if (bfd_big_endian (input_bfd))
	        {
		  if ((x & 0x1800) == 0x0800 && (relocation & 0x02))
		    relocation += 2;
		  relocation = (((relocation & 0xffe) >> 1)  | ((relocation << 4) & 0x07ff0000));
		}
	      else
	        {
		  if ((x & 0x18000000) == 0x08000000 && (relocation & 0x02))
		    relocation += 2;
		  relocation = (((relocation & 0xffe) << 15) | ((relocation >> 12) & 0x7ff));
		}

	      /* Add the relocation to the correct bits of X.  */
	      x = ((x & ~howto->dst_mask) | relocation);

	      /* Put the relocated value back in the object file.  */
	      bfd_put_32 (input_bfd, x, location);

	      rstat = overflow ? bfd_reloc_overflow : bfd_reloc_ok;
            }
        }
#endif
      else
        if (info->relocatable && ! howto->partial_inplace)
            rstat = bfd_reloc_ok;
        else
	  rstat = _bfd_final_link_relocate (howto, input_bfd, input_section,
					    contents,
					    rel->r_vaddr - input_section->vma,
					    val, addend);
      /* Only perform this fix during the final link, not a relocatable link.  */
      if (! info->relocatable
	  && (rel->r_type == ARM_32 || rel->r_type == ARM_RVA32))
	{
	  /* Determine if we need to set the bottom bit of a relocated address
	     because the address is the address of a Thumb code symbol.  */
	  int patchit = FALSE;

	  if (h != NULL
	      && (   h->class == C_THUMBSTATFUNC
		  || h->class == C_THUMBEXTFUNC))
	    {
	      patchit = TRUE;
	    }
	  else if (sym != NULL
		   && sym->n_scnum > N_UNDEF)
	    {
	      /* No hash entry - use the symbol instead.  */
	      if (   sym->n_sclass == C_THUMBSTATFUNC
		  || sym->n_sclass == C_THUMBEXTFUNC)
		patchit = TRUE;
	    }

	  if (patchit)
	    {
	      bfd_byte * location = contents + rel->r_vaddr - input_section->vma;
	      bfd_vma    x        = bfd_get_32 (input_bfd, location);

	      bfd_put_32 (input_bfd, x | 1, location);
	    }
	}

      switch (rstat)
	{
	default:
	  abort ();
	case bfd_reloc_ok:
	  break;
	case bfd_reloc_outofrange:
	  (*_bfd_error_handler)
	    (_("%B: bad reloc address 0x%lx in section `%A'"),
	     input_bfd, input_section, (unsigned long) rel->r_vaddr);
	  return FALSE;
	case bfd_reloc_overflow:
	  {
	    const char *name;
	    char buf[SYMNMLEN + 1];

	    if (symndx == -1)
	      name = "*ABS*";
	    else if (h != NULL)
	      name = NULL;
	    else
	      {
		name = _bfd_coff_internal_syment_name (input_bfd, sym, buf);
		if (name == NULL)
		  return FALSE;
	      }

	    if (! ((*info->callbacks->reloc_overflow)
		   (info, (h ? &h->root : NULL), name, howto->name,
		    (bfd_vma) 0, input_bfd, input_section,
		    rel->r_vaddr - input_section->vma)))
	      return FALSE;
	  }
	}
    }

  return TRUE;
}

#ifndef COFF_IMAGE_WITH_PE

bfd_boolean
bfd_arm_allocate_interworking_sections (struct bfd_link_info * info)
{
  asection *                        s;
  bfd_byte *                        foo;
  struct coff_arm_link_hash_table * globals;

  globals = coff_arm_hash_table (info);

  BFD_ASSERT (globals != NULL);

  if (globals->arm_glue_size != 0)
    {
      BFD_ASSERT (globals->bfd_of_glue_owner != NULL);

      s = bfd_get_section_by_name
	(globals->bfd_of_glue_owner, ARM2THUMB_GLUE_SECTION_NAME);

      BFD_ASSERT (s != NULL);

      foo = bfd_alloc (globals->bfd_of_glue_owner, globals->arm_glue_size);

      s->size = globals->arm_glue_size;
      s->contents = foo;
    }

  if (globals->thumb_glue_size != 0)
    {
      BFD_ASSERT (globals->bfd_of_glue_owner != NULL);

      s = bfd_get_section_by_name
	(globals->bfd_of_glue_owner, THUMB2ARM_GLUE_SECTION_NAME);

      BFD_ASSERT (s != NULL);

      foo = bfd_alloc (globals->bfd_of_glue_owner, globals->thumb_glue_size);

      s->size = globals->thumb_glue_size;
      s->contents = foo;
    }

  return TRUE;
}

static void
record_arm_to_thumb_glue (struct bfd_link_info *        info,
			  struct coff_link_hash_entry * h)
{
  const char *                      name = h->root.root.string;
  register asection *               s;
  char *                            tmp_name;
  struct coff_link_hash_entry *     myh;
  struct bfd_link_hash_entry *      bh;
  struct coff_arm_link_hash_table * globals;
  bfd_vma val;
  bfd_size_type amt;

  globals = coff_arm_hash_table (info);

  BFD_ASSERT (globals != NULL);
  BFD_ASSERT (globals->bfd_of_glue_owner != NULL);

  s = bfd_get_section_by_name
    (globals->bfd_of_glue_owner, ARM2THUMB_GLUE_SECTION_NAME);

  BFD_ASSERT (s != NULL);

  amt = strlen (name) + strlen (ARM2THUMB_GLUE_ENTRY_NAME) + 1;
  tmp_name = bfd_malloc (amt);

  BFD_ASSERT (tmp_name);

  sprintf (tmp_name, ARM2THUMB_GLUE_ENTRY_NAME, name);

  myh = coff_link_hash_lookup
    (coff_hash_table (info), tmp_name, FALSE, FALSE, TRUE);

  if (myh != NULL)
    {
      free (tmp_name);
      /* We've already seen this guy.  */
      return;
    }

  /* The only trick here is using globals->arm_glue_size as the value. Even
     though the section isn't allocated yet, this is where we will be putting
     it.  */
  bh = NULL;
  val = globals->arm_glue_size + 1;
  bfd_coff_link_add_one_symbol (info, globals->bfd_of_glue_owner, tmp_name,
				BSF_GLOBAL, s, val, NULL, TRUE, FALSE, &bh);

  free (tmp_name);

  globals->arm_glue_size += ARM2THUMB_GLUE_SIZE;

  return;
}

#ifndef ARM_WINCE
static void
record_thumb_to_arm_glue (struct bfd_link_info *        info,
			  struct coff_link_hash_entry * h)
{
  const char *                       name = h->root.root.string;
  asection *                         s;
  char *                             tmp_name;
  struct coff_link_hash_entry *      myh;
  struct bfd_link_hash_entry *       bh;
  struct coff_arm_link_hash_table *  globals;
  bfd_vma val;
  bfd_size_type amt;

  globals = coff_arm_hash_table (info);

  BFD_ASSERT (globals != NULL);
  BFD_ASSERT (globals->bfd_of_glue_owner != NULL);

  s = bfd_get_section_by_name
    (globals->bfd_of_glue_owner, THUMB2ARM_GLUE_SECTION_NAME);

  BFD_ASSERT (s != NULL);

  amt = strlen (name) + strlen (THUMB2ARM_GLUE_ENTRY_NAME) + 1;
  tmp_name = bfd_malloc (amt);

  BFD_ASSERT (tmp_name);

  sprintf (tmp_name, THUMB2ARM_GLUE_ENTRY_NAME, name);

  myh = coff_link_hash_lookup
    (coff_hash_table (info), tmp_name, FALSE, FALSE, TRUE);

  if (myh != NULL)
    {
      free (tmp_name);
      /* We've already seen this guy.  */
      return;
    }

  bh = NULL;
  val = globals->thumb_glue_size + 1;
  bfd_coff_link_add_one_symbol (info, globals->bfd_of_glue_owner, tmp_name,
				BSF_GLOBAL, s, val, NULL, TRUE, FALSE, &bh);

  /* If we mark it 'thumb', the disassembler will do a better job.  */
  myh = (struct coff_link_hash_entry *) bh;
  myh->class = C_THUMBEXTFUNC;

  free (tmp_name);

  /* Allocate another symbol to mark where we switch to arm mode.  */

#define CHANGE_TO_ARM "__%s_change_to_arm"
#define BACK_FROM_ARM "__%s_back_from_arm"

  amt = strlen (name) + strlen (CHANGE_TO_ARM) + 1;
  tmp_name = bfd_malloc (amt);

  BFD_ASSERT (tmp_name);

  sprintf (tmp_name, globals->support_old_code ? BACK_FROM_ARM : CHANGE_TO_ARM, name);

  bh = NULL;
  val = globals->thumb_glue_size + (globals->support_old_code ? 8 : 4);
  bfd_coff_link_add_one_symbol (info, globals->bfd_of_glue_owner, tmp_name,
				BSF_LOCAL, s, val, NULL, TRUE, FALSE, &bh);

  free (tmp_name);

  globals->thumb_glue_size += THUMB2ARM_GLUE_SIZE;

  return;
}
#endif /* not ARM_WINCE */

/* Select a BFD to be used to hold the sections used by the glue code.
   This function is called from the linker scripts in ld/emultempl/
   {armcoff/pe}.em  */

bfd_boolean
bfd_arm_get_bfd_for_interworking (bfd * 		 abfd,
				  struct bfd_link_info * info)
{
  struct coff_arm_link_hash_table * globals;
  flagword   			    flags;
  asection * 			    sec;

  /* If we are only performing a partial link do not bother
     getting a bfd to hold the glue.  */
  if (info->relocatable)
    return TRUE;

  globals = coff_arm_hash_table (info);

  BFD_ASSERT (globals != NULL);

  if (globals->bfd_of_glue_owner != NULL)
    return TRUE;

  sec = bfd_get_section_by_name (abfd, ARM2THUMB_GLUE_SECTION_NAME);

  if (sec == NULL)
    {
      flags = (SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS | SEC_IN_MEMORY
	       | SEC_CODE | SEC_READONLY);
      sec = bfd_make_section_with_flags (abfd, ARM2THUMB_GLUE_SECTION_NAME,
					 flags);
      if (sec == NULL
	  || ! bfd_set_section_alignment (abfd, sec, 2))
	return FALSE;
    }

  sec = bfd_get_section_by_name (abfd, THUMB2ARM_GLUE_SECTION_NAME);

  if (sec == NULL)
    {
      flags = (SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS | SEC_IN_MEMORY
	       | SEC_CODE | SEC_READONLY);
      sec = bfd_make_section_with_flags (abfd, THUMB2ARM_GLUE_SECTION_NAME,
					 flags);

      if (sec == NULL
	  || ! bfd_set_section_alignment (abfd, sec, 2))
	return FALSE;
    }

  /* Save the bfd for later use.  */
  globals->bfd_of_glue_owner = abfd;

  return TRUE;
}

bfd_boolean
bfd_arm_process_before_allocation (bfd *                   abfd,
				   struct bfd_link_info *  info,
				   int		           support_old_code)
{
  asection * sec;
  struct coff_arm_link_hash_table * globals;

  /* If we are only performing a partial link do not bother
     to construct any glue.  */
  if (info->relocatable)
    return TRUE;

  /* Here we have a bfd that is to be included on the link.  We have a hook
     to do reloc rummaging, before section sizes are nailed down.  */
  _bfd_coff_get_external_symbols (abfd);

  globals = coff_arm_hash_table (info);

  BFD_ASSERT (globals != NULL);
  BFD_ASSERT (globals->bfd_of_glue_owner != NULL);

  globals->support_old_code = support_old_code;

  /* Rummage around all the relocs and map the glue vectors.  */
  sec = abfd->sections;

  if (sec == NULL)
    return TRUE;

  for (; sec != NULL; sec = sec->next)
    {
      struct internal_reloc * i;
      struct internal_reloc * rel;

      if (sec->reloc_count == 0)
	continue;

      /* Load the relocs.  */
      /* FIXME: there may be a storage leak here.  */
      i = _bfd_coff_read_internal_relocs (abfd, sec, 1, 0, 0, 0);

      BFD_ASSERT (i != 0);

      for (rel = i; rel < i + sec->reloc_count; ++rel)
	{
	  unsigned short                 r_type  = rel->r_type;
	  long                           symndx;
	  struct coff_link_hash_entry *  h;

	  symndx = rel->r_symndx;

	  /* If the relocation is not against a symbol it cannot concern us.  */
	  if (symndx == -1)
	    continue;

	  /* If the index is outside of the range of our table, something has gone wrong.  */
	  if (symndx >= obj_conv_table_size (abfd))
	    {
	      _bfd_error_handler (_("%B: illegal symbol index in reloc: %d"),
				  abfd, symndx);
	      continue;
	    }

	  h = obj_coff_sym_hashes (abfd)[symndx];

	  /* If the relocation is against a static symbol it must be within
	     the current section and so cannot be a cross ARM/Thumb relocation.  */
	  if (h == NULL)
	    continue;

	  switch (r_type)
	    {
	    case ARM_26:
	      /* This one is a call from arm code.  We need to look up
		 the target of the call. If it is a thumb target, we
		 insert glue.  */

	      if (h->class == C_THUMBEXTFUNC)
		record_arm_to_thumb_glue (info, h);
	      break;

#ifndef ARM_WINCE
	    case ARM_THUMB23:
	      /* This one is a call from thumb code.  We used to look
		 for ARM_THUMB9 and ARM_THUMB12 as well.  We need to look
		 up the target of the call. If it is an arm target, we
		 insert glue.  If the symbol does not exist it will be
		 given a class of C_EXT and so we will generate a stub
		 for it.  This is not really a problem, since the link
		 is doomed anyway.  */

	      switch (h->class)
		{
		case C_EXT:
		case C_STAT:
		case C_LABEL:
		  record_thumb_to_arm_glue (info, h);
		  break;
		default:
		  ;
		}
	      break;
#endif

	    default:
	      break;
	    }
	}
    }

  return TRUE;
}

#endif /* ! defined (COFF_IMAGE_WITH_PE) */

#define coff_bfd_reloc_type_lookup 		coff_arm_reloc_type_lookup
#define coff_bfd_reloc_name_lookup	coff_arm_reloc_name_lookup
#define coff_relocate_section 			coff_arm_relocate_section
#define coff_bfd_is_local_label_name 		coff_arm_is_local_label_name
#define coff_adjust_symndx			coff_arm_adjust_symndx
#define coff_link_output_has_begun 		coff_arm_link_output_has_begun
#define coff_final_link_postscript		coff_arm_final_link_postscript
#define coff_bfd_merge_private_bfd_data		coff_arm_merge_private_bfd_data
#define coff_bfd_print_private_bfd_data		coff_arm_print_private_bfd_data
#define coff_bfd_set_private_flags              _bfd_coff_arm_set_private_flags
#define coff_bfd_copy_private_bfd_data          coff_arm_copy_private_bfd_data
#define coff_bfd_link_hash_table_create		coff_arm_link_hash_table_create

/* When doing a relocatable link, we want to convert ARM_26 relocs
   into ARM_26D relocs.  */

static bfd_boolean
coff_arm_adjust_symndx (bfd *obfd ATTRIBUTE_UNUSED,
			struct bfd_link_info *info ATTRIBUTE_UNUSED,
			bfd *ibfd,
			asection *sec,
			struct internal_reloc *irel,
			bfd_boolean *adjustedp)
{
  if (irel->r_type == ARM_26)
    {
      struct coff_link_hash_entry *h;

      h = obj_coff_sym_hashes (ibfd)[irel->r_symndx];
      if (h != NULL
	  && (h->root.type == bfd_link_hash_defined
	      || h->root.type == bfd_link_hash_defweak)
	  && h->root.u.def.section->output_section == sec->output_section)
	irel->r_type = ARM_26D;
    }
  *adjustedp = FALSE;
  return TRUE;
}

/* Called when merging the private data areas of two BFDs.
   This is important as it allows us to detect if we are
   attempting to merge binaries compiled for different ARM
   targets, eg different CPUs or different APCS's.     */

static bfd_boolean
coff_arm_merge_private_bfd_data (bfd * ibfd, bfd * obfd)
{
  BFD_ASSERT (ibfd != NULL && obfd != NULL);

  if (ibfd == obfd)
    return TRUE;

  /* If the two formats are different we cannot merge anything.
     This is not an error, since it is permissable to change the
     input and output formats.  */
  if (   ibfd->xvec->flavour != bfd_target_coff_flavour
      || obfd->xvec->flavour != bfd_target_coff_flavour)
    return TRUE;

  /* Determine what should happen if the input ARM architecture
     does not match the output ARM architecture.  */
  if (! bfd_arm_merge_machines (ibfd, obfd))
    return FALSE;

  /* Verify that the APCS is the same for the two BFDs.  */
  if (APCS_SET (ibfd))
    {
      if (APCS_SET (obfd))
	{
	  /* If the src and dest have different APCS flag bits set, fail.  */
	  if (APCS_26_FLAG (obfd) != APCS_26_FLAG (ibfd))
	    {
	      _bfd_error_handler
		/* xgettext: c-format */
		(_("ERROR: %B is compiled for APCS-%d, whereas %B is compiled for APCS-%d"),
		 ibfd, obfd,
		 APCS_26_FLAG (ibfd) ? 26 : 32,
		 APCS_26_FLAG (obfd) ? 26 : 32
		 );

	      bfd_set_error (bfd_error_wrong_format);
	      return FALSE;
	    }

	  if (APCS_FLOAT_FLAG (obfd) != APCS_FLOAT_FLAG (ibfd))
	    {
	      const char *msg;

	      if (APCS_FLOAT_FLAG (ibfd))
		/* xgettext: c-format */
		msg = _("ERROR: %B passes floats in float registers, whereas %B passes them in integer registers");
	      else
		/* xgettext: c-format */
		msg = _("ERROR: %B passes floats in integer registers, whereas %B passes them in float registers");

	      _bfd_error_handler (msg, ibfd, obfd);

	      bfd_set_error (bfd_error_wrong_format);
	      return FALSE;
	    }

	  if (PIC_FLAG (obfd) != PIC_FLAG (ibfd))
	    {
	      const char * msg;

	      if (PIC_FLAG (ibfd))
		/* xgettext: c-format */
		msg = _("ERROR: %B is compiled as position independent code, whereas target %B is absolute position");
	      else
		/* xgettext: c-format */
		msg = _("ERROR: %B is compiled as absolute position code, whereas target %B is position independent");
	      _bfd_error_handler (msg, ibfd, obfd);

	      bfd_set_error (bfd_error_wrong_format);
	      return FALSE;
	    }
	}
      else
	{
	  SET_APCS_FLAGS (obfd, APCS_26_FLAG (ibfd) | APCS_FLOAT_FLAG (ibfd) | PIC_FLAG (ibfd));

	  /* Set up the arch and fields as well as these are probably wrong.  */
	  bfd_set_arch_mach (obfd, bfd_get_arch (ibfd), bfd_get_mach (ibfd));
	}
    }

  /* Check the interworking support.  */
  if (INTERWORK_SET (ibfd))
    {
      if (INTERWORK_SET (obfd))
	{
	  /* If the src and dest differ in their interworking issue a warning.  */
	  if (INTERWORK_FLAG (obfd) != INTERWORK_FLAG (ibfd))
	    {
	      const char * msg;

	      if (INTERWORK_FLAG (ibfd))
		/* xgettext: c-format */
		msg = _("Warning: %B supports interworking, whereas %B does not");
	      else
		/* xgettext: c-format */
		msg = _("Warning: %B does not support interworking, whereas %B does");

	      _bfd_error_handler (msg, ibfd, obfd);
	    }
	}
      else
	{
	  SET_INTERWORK_FLAG (obfd, INTERWORK_FLAG (ibfd));
	}
    }

  return TRUE;
}

/* Display the flags field.  */

static bfd_boolean
coff_arm_print_private_bfd_data (bfd * abfd, void * ptr)
{
  FILE * file = (FILE *) ptr;

  BFD_ASSERT (abfd != NULL && ptr != NULL);

  /* xgettext:c-format */
  fprintf (file, _("private flags = %x:"), coff_data (abfd)->flags);

  if (APCS_SET (abfd))
    {
      /* xgettext: APCS is ARM Procedure Call Standard, it should not be translated.  */
      fprintf (file, " [APCS-%d]", APCS_26_FLAG (abfd) ? 26 : 32);

      if (APCS_FLOAT_FLAG (abfd))
	fprintf (file, _(" [floats passed in float registers]"));
      else
	fprintf (file, _(" [floats passed in integer registers]"));

      if (PIC_FLAG (abfd))
	fprintf (file, _(" [position independent]"));
      else
	fprintf (file, _(" [absolute position]"));
    }

  if (! INTERWORK_SET (abfd))
    fprintf (file, _(" [interworking flag not initialised]"));
  else if (INTERWORK_FLAG (abfd))
    fprintf (file, _(" [interworking supported]"));
  else
    fprintf (file, _(" [interworking not supported]"));

  fputc ('\n', file);

  return TRUE;
}

/* Copies the given flags into the coff_tdata.flags field.
   Typically these flags come from the f_flags[] field of
   the COFF filehdr structure, which contains important,
   target specific information.
   Note: Although this function is static, it is explicitly
   called from both coffcode.h and peicode.h.  */

static bfd_boolean
_bfd_coff_arm_set_private_flags (bfd * abfd, flagword flags)
{
  flagword flag;

  BFD_ASSERT (abfd != NULL);

  flag = (flags & F_APCS26) ? F_APCS_26 : 0;

  /* Make sure that the APCS field has not been initialised to the opposite
     value.  */
  if (APCS_SET (abfd)
      && (   (APCS_26_FLAG    (abfd) != flag)
	  || (APCS_FLOAT_FLAG (abfd) != (flags & F_APCS_FLOAT))
	  || (PIC_FLAG        (abfd) != (flags & F_PIC))
	  ))
    return FALSE;

  flag |= (flags & (F_APCS_FLOAT | F_PIC));

  SET_APCS_FLAGS (abfd, flag);

  flag = (flags & F_INTERWORK);

  /* If the BFD has already had its interworking flag set, but it
     is different from the value that we have been asked to set,
     then assume that that merged code will not support interworking
     and set the flag accordingly.  */
  if (INTERWORK_SET (abfd) && (INTERWORK_FLAG (abfd) != flag))
    {
      if (flag)
	/* xgettext: c-format */
	_bfd_error_handler (_("Warning: Not setting interworking flag of %B since it has already been specified as non-interworking"),
			    abfd);
      else
	/* xgettext: c-format */
	_bfd_error_handler (_("Warning: Clearing the interworking flag of %B due to outside request"),
			    abfd);
      flag = 0;
    }

  SET_INTERWORK_FLAG (abfd, flag);

  return TRUE;
}

/* Copy the important parts of the target specific data
   from one instance of a BFD to another.  */

static bfd_boolean
coff_arm_copy_private_bfd_data (bfd * src, bfd * dest)
{
  BFD_ASSERT (src != NULL && dest != NULL);

  if (src == dest)
    return TRUE;

  /* If the destination is not in the same format as the source, do not do
     the copy.  */
  if (src->xvec != dest->xvec)
    return TRUE;

  /* Copy the flags field.  */
  if (APCS_SET (src))
    {
      if (APCS_SET (dest))
	{
	  /* If the src and dest have different APCS flag bits set, fail.  */
	  if (APCS_26_FLAG (dest) != APCS_26_FLAG (src))
	    return FALSE;

	  if (APCS_FLOAT_FLAG (dest) != APCS_FLOAT_FLAG (src))
	    return FALSE;

	  if (PIC_FLAG (dest) != PIC_FLAG (src))
	    return FALSE;
	}
      else
	SET_APCS_FLAGS (dest, APCS_26_FLAG (src) | APCS_FLOAT_FLAG (src)
			| PIC_FLAG (src));
    }

  if (INTERWORK_SET (src))
    {
      if (INTERWORK_SET (dest))
	{
	  /* If the src and dest have different interworking flags then turn
	     off the interworking bit.  */
	  if (INTERWORK_FLAG (dest) != INTERWORK_FLAG (src))
	    {
	      if (INTERWORK_FLAG (dest))
		{
		  /* xgettext:c-format */
		  _bfd_error_handler (("\
Warning: Clearing the interworking flag of %B because non-interworking code in %B has been linked with it"),
				      dest, src);
		}

	      SET_INTERWORK_FLAG (dest, 0);
	    }
	}
      else
	{
	  SET_INTERWORK_FLAG (dest, INTERWORK_FLAG (src));
	}
    }

  return TRUE;
}

/* Note:  the definitions here of LOCAL_LABEL_PREFIX and USER_LABEL_PREIFX
   *must* match the definitions in gcc/config/arm/{coff|semi|aout}.h.  */
#ifndef LOCAL_LABEL_PREFIX
#define LOCAL_LABEL_PREFIX ""
#endif
#ifndef USER_LABEL_PREFIX
#define USER_LABEL_PREFIX "_"
#endif

/* Like _bfd_coff_is_local_label_name, but
   a) test against USER_LABEL_PREFIX, to avoid stripping labels known to be
      non-local.
   b) Allow other prefixes than ".", e.g. an empty prefix would cause all
      labels of the form Lxxx to be stripped.  */

static bfd_boolean
coff_arm_is_local_label_name (bfd *        abfd ATTRIBUTE_UNUSED,
			      const char * name)
{
#ifdef USER_LABEL_PREFIX
  if (USER_LABEL_PREFIX[0] != 0)
    {
      size_t len = strlen (USER_LABEL_PREFIX);

      if (strncmp (name, USER_LABEL_PREFIX, len) == 0)
	return FALSE;
    }
#endif

#ifdef LOCAL_LABEL_PREFIX
  /* If there is a prefix for local labels then look for this.
     If the prefix exists, but it is empty, then ignore the test.  */

  if (LOCAL_LABEL_PREFIX[0] != 0)
    {
      size_t len = strlen (LOCAL_LABEL_PREFIX);

      if (strncmp (name, LOCAL_LABEL_PREFIX, len) != 0)
	return FALSE;

      /* Perform the checks below for the rest of the name.  */
      name += len;
    }
#endif

  return name[0] == 'L';
}

/* This piece of machinery exists only to guarantee that the bfd that holds
   the glue section is written last.

   This does depend on bfd_make_section attaching a new section to the
   end of the section list for the bfd.  */

static bfd_boolean
coff_arm_link_output_has_begun (bfd * sub, struct coff_final_link_info * info)
{
  return (sub->output_has_begun
	  || sub == coff_arm_hash_table (info->info)->bfd_of_glue_owner);
}

static bfd_boolean
coff_arm_final_link_postscript (bfd * abfd ATTRIBUTE_UNUSED,
				struct coff_final_link_info * pfinfo)
{
  struct coff_arm_link_hash_table * globals;

  globals = coff_arm_hash_table (pfinfo->info);

  BFD_ASSERT (globals != NULL);

  if (globals->bfd_of_glue_owner != NULL)
    {
      if (! _bfd_coff_link_input_bfd (pfinfo, globals->bfd_of_glue_owner))
	return FALSE;

      globals->bfd_of_glue_owner->output_has_begun = TRUE;
    }

  return bfd_arm_update_notes (abfd, ARM_NOTE_SECTION);
}

#include "coffcode.h"

#ifndef TARGET_LITTLE_SYM
#define TARGET_LITTLE_SYM armcoff_little_vec
#endif
#ifndef TARGET_LITTLE_NAME
#define TARGET_LITTLE_NAME "coff-arm-little"
#endif
#ifndef TARGET_BIG_SYM
#define TARGET_BIG_SYM armcoff_big_vec
#endif
#ifndef TARGET_BIG_NAME
#define TARGET_BIG_NAME "coff-arm-big"
#endif

#ifndef TARGET_UNDERSCORE
#define TARGET_UNDERSCORE 0
#endif

#ifndef EXTRA_S_FLAGS
#ifdef COFF_WITH_PE
#define EXTRA_S_FLAGS (SEC_CODE | SEC_LINK_ONCE | SEC_LINK_DUPLICATES)
#else
#define EXTRA_S_FLAGS SEC_CODE
#endif
#endif

/* Forward declaration for use initialising alternative_target field.  */
extern const bfd_target TARGET_BIG_SYM ;

/* Target vectors.  */
CREATE_LITTLE_COFF_TARGET_VEC (TARGET_LITTLE_SYM, TARGET_LITTLE_NAME, D_PAGED, EXTRA_S_FLAGS, TARGET_UNDERSCORE, & TARGET_BIG_SYM, COFF_SWAP_TABLE)
CREATE_BIG_COFF_TARGET_VEC (TARGET_BIG_SYM, TARGET_BIG_NAME, D_PAGED, EXTRA_S_FLAGS, TARGET_UNDERSCORE, & TARGET_LITTLE_SYM, COFF_SWAP_TABLE)
