/* Xtensa ELF support for BFD.
   Copyright 2003, 2004 Free Software Foundation, Inc.
   Contributed by Bob Wilson (bwilson@tensilica.com) at Tensilica.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301,
   USA.  */

/* This file holds definitions specific to the Xtensa ELF ABI.  */

#ifndef _ELF_XTENSA_H
#define _ELF_XTENSA_H

#include "elf/reloc-macros.h"

/* Relocations.  */
START_RELOC_NUMBERS (elf_xtensa_reloc_type)
     RELOC_NUMBER (R_XTENSA_NONE, 0)
     RELOC_NUMBER (R_XTENSA_32, 1)
     RELOC_NUMBER (R_XTENSA_RTLD, 2)
     RELOC_NUMBER (R_XTENSA_GLOB_DAT, 3)
     RELOC_NUMBER (R_XTENSA_JMP_SLOT, 4)
     RELOC_NUMBER (R_XTENSA_RELATIVE, 5)
     RELOC_NUMBER (R_XTENSA_PLT, 6)
     RELOC_NUMBER (R_XTENSA_OP0, 8)
     RELOC_NUMBER (R_XTENSA_OP1, 9)
     RELOC_NUMBER (R_XTENSA_OP2, 10) 
     RELOC_NUMBER (R_XTENSA_ASM_EXPAND, 11)
     RELOC_NUMBER (R_XTENSA_ASM_SIMPLIFY, 12)
     RELOC_NUMBER (R_XTENSA_GNU_VTINHERIT, 15)
     RELOC_NUMBER (R_XTENSA_GNU_VTENTRY, 16)
     RELOC_NUMBER (R_XTENSA_DIFF8, 17)
     RELOC_NUMBER (R_XTENSA_DIFF16, 18)
     RELOC_NUMBER (R_XTENSA_DIFF32, 19)
     RELOC_NUMBER (R_XTENSA_SLOT0_OP, 20)
     RELOC_NUMBER (R_XTENSA_SLOT1_OP, 21)
     RELOC_NUMBER (R_XTENSA_SLOT2_OP, 22)
     RELOC_NUMBER (R_XTENSA_SLOT3_OP, 23)
     RELOC_NUMBER (R_XTENSA_SLOT4_OP, 24)
     RELOC_NUMBER (R_XTENSA_SLOT5_OP, 25)
     RELOC_NUMBER (R_XTENSA_SLOT6_OP, 26)
     RELOC_NUMBER (R_XTENSA_SLOT7_OP, 27)
     RELOC_NUMBER (R_XTENSA_SLOT8_OP, 28)
     RELOC_NUMBER (R_XTENSA_SLOT9_OP, 29)
     RELOC_NUMBER (R_XTENSA_SLOT10_OP, 30)
     RELOC_NUMBER (R_XTENSA_SLOT11_OP, 31)
     RELOC_NUMBER (R_XTENSA_SLOT12_OP, 32)
     RELOC_NUMBER (R_XTENSA_SLOT13_OP, 33)
     RELOC_NUMBER (R_XTENSA_SLOT14_OP, 34)
     RELOC_NUMBER (R_XTENSA_SLOT0_ALT, 35)
     RELOC_NUMBER (R_XTENSA_SLOT1_ALT, 36)
     RELOC_NUMBER (R_XTENSA_SLOT2_ALT, 37)
     RELOC_NUMBER (R_XTENSA_SLOT3_ALT, 38)
     RELOC_NUMBER (R_XTENSA_SLOT4_ALT, 39)
     RELOC_NUMBER (R_XTENSA_SLOT5_ALT, 40)
     RELOC_NUMBER (R_XTENSA_SLOT6_ALT, 41)
     RELOC_NUMBER (R_XTENSA_SLOT7_ALT, 42)
     RELOC_NUMBER (R_XTENSA_SLOT8_ALT, 43)
     RELOC_NUMBER (R_XTENSA_SLOT9_ALT, 44)
     RELOC_NUMBER (R_XTENSA_SLOT10_ALT, 45)
     RELOC_NUMBER (R_XTENSA_SLOT11_ALT, 46)
     RELOC_NUMBER (R_XTENSA_SLOT12_ALT, 47)
     RELOC_NUMBER (R_XTENSA_SLOT13_ALT, 48)
     RELOC_NUMBER (R_XTENSA_SLOT14_ALT, 49)
END_RELOC_NUMBERS (R_XTENSA_max)

/* Processor-specific flags for the ELF header e_flags field.  */

/* Four-bit Xtensa machine type field.  */
#define EF_XTENSA_MACH			0x0000000f

/* Various CPU types.  */
#define E_XTENSA_MACH			0x00000000

/* Leave bits 0xf0 alone in case we ever have more than 16 cpu types.
   Highly unlikely, but what the heck.  */

#define EF_XTENSA_XT_INSN		0x00000100
#define EF_XTENSA_XT_LIT		0x00000200


/* Processor-specific dynamic array tags.  */

/* Offset of the table that records the GOT location(s).  */
#define DT_XTENSA_GOT_LOC_OFF		0x70000000

/* Number of entries in the GOT location table.  */
#define DT_XTENSA_GOT_LOC_SZ		0x70000001


/* Definitions for instruction and literal property tables.  The
   tables for ".gnu.linkonce.*" sections are placed in the following
   sections:

   instruction tables:	.gnu.linkonce.x.*
   literal tables:	.gnu.linkonce.p.*
*/

#define XTENSA_INSN_SEC_NAME ".xt.insn"
#define XTENSA_LIT_SEC_NAME  ".xt.lit"
#define XTENSA_PROP_SEC_NAME ".xt.prop"

typedef struct property_table_entry_t
{
  bfd_vma address;
  bfd_vma size;
  flagword flags;
} property_table_entry;

/* Flags in the property tables to specify whether blocks of memory are
   literals, instructions, data, or unreachable.  For instructions,
   blocks that begin loop targets and branch targets are designated.
   Blocks that do not allow density instructions, instruction reordering
   or transformation are also specified.  Finally, for branch targets,
   branch target alignment priority is included.  Alignment of the next
   block is specified in the current block and the size of the current
   block does not include any fill required to align to the next
   block.  */
   
#define XTENSA_PROP_LITERAL		0x00000001
#define XTENSA_PROP_INSN		0x00000002
#define XTENSA_PROP_DATA		0x00000004
#define XTENSA_PROP_UNREACHABLE		0x00000008
/* Instruction-only properties at beginning of code. */
#define XTENSA_PROP_INSN_LOOP_TARGET	0x00000010
#define XTENSA_PROP_INSN_BRANCH_TARGET	0x00000020
/* Instruction-only properties about code. */
#define XTENSA_PROP_INSN_NO_DENSITY	0x00000040
#define XTENSA_PROP_INSN_NO_REORDER	0x00000080
/* Historically, NO_TRANSFORM was a property of instructions, 
   but it should apply to literals under certain circumstances.  */
#define XTENSA_PROP_NO_TRANSFORM	0x00000100

/*  Branch target alignment information.  This transmits information
    to the linker optimization about the priority of aligning a
    particular block for branch target alignment: None, low priority,
    high priority, or required.  These only need to be checked in
    instruction blocks marked as XTENSA_PROP_INSN_BRANCH_TARGET.
    Common usage is:

    switch (GET_XTENSA_PROP_BT_ALIGN(flags))
    case XTENSA_PROP_BT_ALIGN_NONE:
    case XTENSA_PROP_BT_ALIGN_LOW:
    case XTENSA_PROP_BT_ALIGN_HIGH:
    case XTENSA_PROP_BT_ALIGN_REQUIRE:
*/
#define XTENSA_PROP_BT_ALIGN_MASK       0x00000600

/* No branch target alignment.  */
#define XTENSA_PROP_BT_ALIGN_NONE       0x0
/* Low priority branch target alignment.  */
#define XTENSA_PROP_BT_ALIGN_LOW        0x1
/* High priority branch target alignment. */
#define XTENSA_PROP_BT_ALIGN_HIGH       0x2
/* Required branch target alignment.  */
#define XTENSA_PROP_BT_ALIGN_REQUIRE    0x3

#define GET_XTENSA_PROP_BT_ALIGN(flag) \
  (((unsigned)((flag) & (XTENSA_PROP_BT_ALIGN_MASK))) >> 9)
#define SET_XTENSA_PROP_BT_ALIGN(flag, align) \
  (((flag) & (~XTENSA_PROP_BT_ALIGN_MASK)) | \
    (((align) << 9) & XTENSA_PROP_BT_ALIGN_MASK))

/* Alignment is specified in the block BEFORE the one that needs
   alignment.  Up to 5 bits.  Use GET_XTENSA_PROP_ALIGNMENT(flags) to
   get the required alignment specified as a power of 2.  Use
   SET_XTENSA_PROP_ALIGNMENT(flags, pow2) to set the required
   alignment.  Be careful of side effects since the SET will evaluate
   flags twice.  Also, note that the SIZE of a block in the property
   table does not include the alignment size, so the alignment fill
   must be calculated to determine if two blocks are contiguous.
   TEXT_ALIGN is not currently implemented but is a placeholder for a
   possible future implementation.  */

#define XTENSA_PROP_ALIGN		0x00000800

#define XTENSA_PROP_ALIGNMENT_MASK      0x0001f000

#define GET_XTENSA_PROP_ALIGNMENT(flag) \
  (((unsigned)((flag) & (XTENSA_PROP_ALIGNMENT_MASK))) >> 12)
#define SET_XTENSA_PROP_ALIGNMENT(flag, align) \
  (((flag) & (~XTENSA_PROP_ALIGNMENT_MASK)) | \
    (((align) << 12) & XTENSA_PROP_ALIGNMENT_MASK))

#define XTENSA_PROP_INSN_ABSLIT        0x00020000

#endif /* _ELF_XTENSA_H */
