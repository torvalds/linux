/* ARC-specific support for 32-bit ELF
   Copyright 1994, 1995, 1997, 1999, 2001, 2002, 2005, 2007
   Free Software Foundation, Inc.
   Contributed by Doug Evans (dje@cygnus.com).

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "elf-bfd.h"
#include "elf/arc.h"
#include "libiberty.h"

/* Try to minimize the amount of space occupied by relocation tables
   on the ROM (not that the ROM won't be swamped by other ELF overhead).  */

#define USE_REL	1

static bfd_reloc_status_type
arc_elf_b22_pcrel (bfd * abfd,
		   arelent * reloc_entry,
		   asymbol * symbol,
		   void * data,
		   asection * input_section,
		   bfd * output_bfd,
		   char ** error_message)
{
  /* If linking, back up the final symbol address by the address of the
     reloc.  This cannot be accomplished by setting the pcrel_offset
     field to TRUE, as bfd_install_relocation will detect this and refuse
     to install the offset in the first place, but bfd_perform_relocation
     will still insist on removing it.  */
  if (output_bfd == NULL)
    reloc_entry->addend -= reloc_entry->address;

  /* Fall through to the default elf reloc handler.  */
  return bfd_elf_generic_reloc (abfd, reloc_entry, symbol, data,
				input_section, output_bfd, error_message);
}

static reloc_howto_type elf_arc_howto_table[] =
{
  /* This reloc does nothing.  */
  HOWTO (R_ARC_NONE,		/* Type.  */
	 0,			/* Rightshift.  */
	 2,			/* Size (0 = byte, 1 = short, 2 = long).  */
	 32,			/* Bitsize.  */
	 FALSE,			/* PC_relative.  */
	 0,			/* Bitpos.  */
	 complain_overflow_bitfield, /* Complain_on_overflow.  */
	 bfd_elf_generic_reloc,	/* Special_function.  */
	 "R_ARC_NONE",		/* Name.  */
	 TRUE,			/* Partial_inplace.  */
	 0,			/* Src_mask.  */
	 0,			/* Dst_mask.  */
	 FALSE),		/* PCrel_offset.  */

  /* A standard 32 bit relocation.  */
  HOWTO (R_ARC_32,		/* Type.  */
	 0,			/* Rightshift.  */
	 2,			/* Size (0 = byte, 1 = short, 2 = long).  */
	 32,			/* Bitsize.  */
	 FALSE,			/* PC_relative.  */
	 0,			/* Bitpos.  */
	 complain_overflow_bitfield, /* Complain_on_overflow.  */
	 bfd_elf_generic_reloc,	/* Special_function.  */
	 "R_ARC_32",		/* Name.  */
	 TRUE,			/* Partial_inplace.  */
	 0xffffffff,		/* Src_mask.  */
	 0xffffffff,		/* Dst_mask.  */
	 FALSE),		/* PCrel_offset.  */

  /* A 26 bit absolute branch, right shifted by 2.  */
  HOWTO (R_ARC_B26,		/* Type.  */
	 2,			/* Rightshift.  */
	 2,			/* Size (0 = byte, 1 = short, 2 = long).  */
	 26,			/* Bitsize.  */
	 FALSE,			/* PC_relative.  */
	 0,			/* Bitpos.  */
	 complain_overflow_bitfield, /* Complain_on_overflow.  */
	 bfd_elf_generic_reloc,	/* Special_function.  */
	 "R_ARC_B26",		/* Name.  */
	 TRUE,			/* Partial_inplace.  */
	 0x00ffffff,		/* Src_mask.  */
	 0x00ffffff,		/* Dst_mask.  */
	 FALSE),		/* PCrel_offset.  */

  /* A relative 22 bit branch; bits 21-2 are stored in bits 26-7.  */
  HOWTO (R_ARC_B22_PCREL,	/* Type.  */
	 2,			/* Rightshift.  */
	 2,			/* Size (0 = byte, 1 = short, 2 = long).  */
	 22,			/* Bitsize.  */
	 TRUE,			/* PC_relative.  */
	 7,			/* Bitpos.  */
	 complain_overflow_signed, /* Complain_on_overflow.  */
	 arc_elf_b22_pcrel,	/* Special_function.  */
	 "R_ARC_B22_PCREL",	/* Name.  */
	 TRUE,			/* Partial_inplace.  */
	 0x07ffff80,		/* Src_mask.  */
	 0x07ffff80,		/* Dst_mask.  */
	 FALSE),		/* PCrel_offset.  */
};

/* Map BFD reloc types to ARC ELF reloc types.  */

struct arc_reloc_map
{
  bfd_reloc_code_real_type bfd_reloc_val;
  unsigned char elf_reloc_val;
};

static const struct arc_reloc_map arc_reloc_map[] =
{
  { BFD_RELOC_NONE, R_ARC_NONE, },
  { BFD_RELOC_32, R_ARC_32 },
  { BFD_RELOC_CTOR, R_ARC_32 },
  { BFD_RELOC_ARC_B26, R_ARC_B26 },
  { BFD_RELOC_ARC_B22_PCREL, R_ARC_B22_PCREL },
};

static reloc_howto_type *
bfd_elf32_bfd_reloc_type_lookup (bfd *abfd ATTRIBUTE_UNUSED,
				 bfd_reloc_code_real_type code)
{
  unsigned int i;

  for (i = ARRAY_SIZE (arc_reloc_map); i--;)
    if (arc_reloc_map[i].bfd_reloc_val == code)
      return elf_arc_howto_table + arc_reloc_map[i].elf_reloc_val;

  return NULL;
}

static reloc_howto_type *
bfd_elf32_bfd_reloc_name_lookup (bfd *abfd ATTRIBUTE_UNUSED,
				 const char *r_name)
{
  unsigned int i;

  for (i = 0;
       i < sizeof (elf_arc_howto_table) / sizeof (elf_arc_howto_table[0]);
       i++)
    if (elf_arc_howto_table[i].name != NULL
	&& strcasecmp (elf_arc_howto_table[i].name, r_name) == 0)
      return &elf_arc_howto_table[i];

  return NULL;
}

/* Set the howto pointer for an ARC ELF reloc.  */

static void
arc_info_to_howto_rel (bfd *abfd ATTRIBUTE_UNUSED,
		       arelent *cache_ptr,
		       Elf_Internal_Rela *dst)
{
  unsigned int r_type;

  r_type = ELF32_R_TYPE (dst->r_info);
  BFD_ASSERT (r_type < (unsigned int) R_ARC_max);
  cache_ptr->howto = &elf_arc_howto_table[r_type];
}

/* Set the right machine number for an ARC ELF file.  */

static bfd_boolean
arc_elf_object_p (bfd *abfd)
{
  unsigned int mach = bfd_mach_arc_6;

  if (elf_elfheader(abfd)->e_machine == EM_ARC)
    {
      unsigned long arch = elf_elfheader (abfd)->e_flags & EF_ARC_MACH;

      switch (arch)
	{
	case E_ARC_MACH_ARC5:
	  mach = bfd_mach_arc_5;
	  break;
	default:
	case E_ARC_MACH_ARC6:
	  mach = bfd_mach_arc_6;
	  break;
	case E_ARC_MACH_ARC7:
	  mach = bfd_mach_arc_7;
	  break;
	case E_ARC_MACH_ARC8:
	  mach = bfd_mach_arc_8;
	  break;
	}
    }
  return bfd_default_set_arch_mach (abfd, bfd_arch_arc, mach);
}

/* The final processing done just before writing out an ARC ELF object file.
   This gets the ARC architecture right based on the machine number.  */

static void
arc_elf_final_write_processing (bfd *abfd,
				bfd_boolean linker ATTRIBUTE_UNUSED)
{
  unsigned long val;

  switch (bfd_get_mach (abfd))
    {
    case bfd_mach_arc_5:
      val = E_ARC_MACH_ARC5;
      break;
    default:
    case bfd_mach_arc_6:
      val = E_ARC_MACH_ARC6;
      break;
    case bfd_mach_arc_7:
      val = E_ARC_MACH_ARC7;
      break;
    case bfd_mach_arc_8:
      val = E_ARC_MACH_ARC8;
      break;
    }
  elf_elfheader (abfd)->e_flags &=~ EF_ARC_MACH;
  elf_elfheader (abfd)->e_flags |= val;
}

#define TARGET_LITTLE_SYM   bfd_elf32_littlearc_vec
#define TARGET_LITTLE_NAME  "elf32-littlearc"
#define TARGET_BIG_SYM      bfd_elf32_bigarc_vec
#define TARGET_BIG_NAME	    "elf32-bigarc"
#define ELF_ARCH            bfd_arch_arc
#define ELF_MACHINE_CODE    EM_ARC
#define ELF_MAXPAGESIZE     0x1000

#define elf_info_to_howto                   0
#define elf_info_to_howto_rel               arc_info_to_howto_rel
#define elf_backend_object_p                arc_elf_object_p
#define elf_backend_final_write_processing  arc_elf_final_write_processing

#include "elf32-target.h"
