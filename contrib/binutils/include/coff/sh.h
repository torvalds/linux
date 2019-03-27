/* coff information for Renesas SH
   
   Copyright 2000, 2003 Free Software Foundation, Inc.

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

#ifdef COFF_WITH_PE
#define L_LNNO_SIZE 2
#else
#define L_LNNO_SIZE 4
#endif
#define INCLUDE_COMDAT_FIELDS_IN_AUXENT
#include "coff/external.h"

#define	SH_ARCH_MAGIC_BIG	0x0500
#define	SH_ARCH_MAGIC_LITTLE	0x0550  /* Little endian SH */
#define SH_ARCH_MAGIC_WINCE	0x01a2  /* Windows CE - little endian */
#define SH_PE_MAGIC		0x010b

#define SHBADMAG(x) \
 (((x).f_magic != SH_ARCH_MAGIC_BIG) && \
  ((x).f_magic != SH_ARCH_MAGIC_WINCE) && \
  ((x).f_magic != SH_ARCH_MAGIC_LITTLE))

/* Define some NT default values.  */
/*  #define NT_IMAGE_BASE        0x400000 moved to internal.h */
#define NT_SECTION_ALIGNMENT 0x1000
#define NT_FILE_ALIGNMENT    0x200
#define NT_DEF_RESERVE       0x100000
#define NT_DEF_COMMIT        0x1000

/********************** RELOCATION DIRECTIVES **********************/

/* The external reloc has an offset field, because some of the reloc
   types on the h8 don't have room in the instruction for the entire
   offset - eg the strange jump and high page addressing modes.  */

#ifndef COFF_WITH_PE
struct external_reloc
{
  char r_vaddr[4];
  char r_symndx[4];
  char r_offset[4];
  char r_type[2];
  char r_stuff[2];
};
#else
struct external_reloc
{
  char r_vaddr[4];
  char r_symndx[4];
  char r_type[2];
};
#endif

#define RELOC struct external_reloc
#ifdef COFF_WITH_PE
#define RELSZ 10
#else
#define RELSZ 16
#endif

/* SH relocation types.  Not all of these are actually used.  */

#define R_SH_UNUSED	0		/* only used internally */
#define R_SH_IMM32CE	2		/* 32 bit immediate for WinCE */
#define R_SH_PCREL8 	3		/*  8 bit pcrel 	*/
#define R_SH_PCREL16 	4		/* 16 bit pcrel 	*/
#define R_SH_HIGH8  	5		/* high 8 bits of 24 bit address */
#define R_SH_LOW16 	7		/* low 16 bits of 24 bit immediate */
#define R_SH_IMM24	6		/* 24 bit immediate */
#define R_SH_PCDISP8BY4	9  		/* PC rel 8 bits *4 +ve */
#define R_SH_PCDISP8BY2	10  		/* PC rel 8 bits *2 +ve */
#define R_SH_PCDISP8    11  		/* 8 bit branch */
#define R_SH_PCDISP     12  		/* 12 bit branch */
#define R_SH_IMM32      14    		/* 32 bit immediate */
#define R_SH_IMM8   	16		/* 8 bit immediate */
#define R_SH_IMAGEBASE	16		/* Windows CE */
#define R_SH_IMM8BY2    17		/* 8 bit immediate *2 */
#define R_SH_IMM8BY4    18		/* 8 bit immediate *4 */
#define R_SH_IMM4   	19		/* 4 bit immediate */
#define R_SH_IMM4BY2    20		/* 4 bit immediate *2 */
#define R_SH_IMM4BY4    21		/* 4 bit immediate *4 */
#define R_SH_PCRELIMM8BY2   22		/* PC rel 8 bits *2 unsigned */
#define R_SH_PCRELIMM8BY4   23		/* PC rel 8 bits *4 unsigned */
#define R_SH_IMM16      24    		/* 16 bit immediate */

/* The switch table reloc types are used for relaxing.  They are
   generated for expressions such as
     .word L1 - L2
   The r_offset field holds the difference between the reloc address
   and L2.  */
#define R_SH_SWITCH8	33		/* 8 bit switch table entry */
#define R_SH_SWITCH16	25		/* 16 bit switch table entry */
#define R_SH_SWITCH32	26		/* 32 bit switch table entry */

/* The USES reloc type is used for relaxing.  The compiler will
   generate .uses pseudo-ops when it finds a function call which it
   can relax.  The r_offset field of the USES reloc holds the PC
   relative offset to the instruction which loads the register used in
   the function call.  */
#define R_SH_USES	27		/* .uses pseudo-op */

/* The COUNT reloc type is used for relaxing.  The assembler will
   generate COUNT relocs for addresses referred to by the register
   loads associated with USES relocs.  The r_offset field of the COUNT
   reloc holds the number of times the address is referenced in the
   object file.  */
#define R_SH_COUNT	28		/* Count of constant pool uses */

/* The ALIGN reloc type is used for relaxing.  The r_offset field is
   the power of two to which subsequent portions of the object file
   must be aligned.  */
#define R_SH_ALIGN	29		/* .align pseudo-op */

/* The CODE and DATA reloc types are used for aligning load and store
   instructions.  The assembler will generate a CODE reloc before a
   block of instructions.  It will generate a DATA reloc before data.
   A section should be processed assuming it contains data, unless a
   CODE reloc is seen.  The only relevant pieces of information in the
   CODE and DATA relocs are the section and the address.  The symbol
   and offset are meaningless.  */
#define R_SH_CODE	30		/* start of code */
#define R_SH_DATA	31		/* start of data */

/* The LABEL reloc type is used for aligning load and store
   instructions.  The assembler will generate a LABEL reloc for each
   label within a block of instructions.  This permits the linker to
   avoid swapping instructions which are the targets of branches.  */
#define R_SH_LABEL	32		/* label */

/* NB: R_SH_SWITCH8 is 33 */

#define R_SH_LOOP_START	34
#define R_SH_LOOP_END	35
