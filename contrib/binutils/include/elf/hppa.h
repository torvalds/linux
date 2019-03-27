/* HPPA ELF support for BFD.
   Copyright 1993, 1994, 1995, 1998, 1999, 2000, 2005, 2006
   Free Software Foundation, Inc.

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

/* This file holds definitions specific to the HPPA ELF ABI.  Note
   that most of this is not actually implemented by BFD.  */

#ifndef _ELF_HPPA_H
#define _ELF_HPPA_H

/* Processor specific flags for the ELF header e_flags field.  */

/* Trap null address dereferences.  */
#define EF_PARISC_TRAPNIL	0x00010000

/* .PARISC.archext section is present.  */
#define EF_PARISC_EXT		0x00020000

/* Program expects little-endian mode.  */
#define EF_PARISC_LSB		0x00040000

/* Program expects wide mode.  */
#define EF_PARISC_WIDE		0x00080000

/* Do not allow kernel-assisted branch prediction.  */
#define EF_PARISC_NO_KABP	0x00100000

/* Allow lazy swap for dynamically allocated program segments.  */
#define EF_PARISC_LAZYSWAP	0x00400000

/* Architecture version */
#define EF_PARISC_ARCH		0x0000ffff

#define EFA_PARISC_1_0			0x020b
#define EFA_PARISC_1_1			0x0210
#define EFA_PARISC_2_0			0x0214

/* Special section indices.  */
/* A symbol that has been declared as a tentative definition in an ANSI C
   compilation.  */
#define SHN_PARISC_ANSI_COMMON 	0xff00

/* A symbol that has been declared as a common block using the
   huge memory model.  */
#define SHN_PARISC_HUGE_COMMON	0xff01

/* Processor specific section types.  */

/* Section contains product specific extension bits.  */
#define SHT_PARISC_EXT		0x70000000

/* Section contains unwind table entries.  */
#define SHT_PARISC_UNWIND	0x70000001

/* Section contains debug information for optimized code.  */
#define SHT_PARISC_DOC		0x70000002

/* Section contains code annotations.  */
#define SHT_PARISC_ANNOT	0x70000003

/* DLKM special section.  */
#define SHT_PARISC_DLKM		0x70000004

/* These are strictly for compatibility with the older elf32-hppa
   implementation.  Hopefully we can eliminate them in the future.  */
/* Optional section holding argument location/relocation info.  */
#define SHT_PARISC_SYMEXTN    SHT_LOPROC + 8

/* Option section for linker stubs.  */
#define SHT_PARISC_STUBS      SHT_LOPROC + 9

/* Processor specific section flags.  */

/* Section contains code compiled for static branch prediction.  */
#define SHF_PARISC_SBP		0x80000000

/* Section should be allocated from from GP.  */
#define SHF_PARISC_HUGE		0x40000000

/* Section should go near GP.  */
#define SHF_PARISC_SHORT	0x20000000

/* Section is weak ordered.  */
#define SHF_PARISC_WEAKORDER	0x10000000

/* Identifies the entry point of a millicode routine.  */
#define STT_PARISC_MILLI	13

/* ELF/HPPA relocation types */

/* Note: PA-ELF is defined to use only RELA relocations.  */
#include "elf/reloc-macros.h"

START_RELOC_NUMBERS (elf_hppa_reloc_type)
RELOC_NUMBER (R_PARISC_NONE,	         0) /* No reloc */

/*		Data / Inst. Format	   Relocation Expression	  */

RELOC_NUMBER (R_PARISC_DIR32,	   	 1)
/*		32-bit word            	   symbol + addend    		  */

RELOC_NUMBER (R_PARISC_DIR21L,	   	 2)
/*		long immediate (7)	   LR(symbol, addend) 		  */

RELOC_NUMBER (R_PARISC_DIR17R,	   	 3)
/*		branch external (19)	   RR(symbol, addend) 		  */

RELOC_NUMBER (R_PARISC_DIR17F,	   	 4)
/*		branch external (19)	   symbol + addend    		  */

RELOC_NUMBER (R_PARISC_DIR14R,	   	 6)
/*		load/store (1)		   RR(symbol, addend) 		  */

RELOC_NUMBER (R_PARISC_DIR14F,	   	 7)
/*		load/store (1)		   symbol, addend 		  */

/* PC-relative relocation types
   Typically used for calls.
   Note PCREL17C and PCREL17F differ only in overflow handling.
   PCREL17C never reports a relocation error.

   When supporting argument relocations, function calls must be
   accompanied by parameter relocation information.  This information is
   carried in the ten high-order bits of the addend field.  The remaining
   22 bits of of the addend field are sign-extended to form the Addend.

   Note the code to build argument relocations depends on the
   addend being zero.  A consequence of this limitation is GAS
   can not perform relocation reductions for function symbols.  */

RELOC_NUMBER (R_PARISC_PCREL12F,  	 8)
/*		op & branch (17)	   symbol - PC - 8 + addend    	  */

RELOC_NUMBER (R_PARISC_PCREL32,   	 9)
/*		32-bit word		   symbol - PC - 8 + addend    	  */

RELOC_NUMBER (R_PARISC_PCREL21L,  	10)
/*		long immediate (7)	   L(symbol - PC - 8 + addend) 	  */

RELOC_NUMBER (R_PARISC_PCREL17R,  	11)
/*		branch external (19)	   R(symbol - PC - 8 + addend) 	  */

RELOC_NUMBER (R_PARISC_PCREL17F,  	12)
/*		branch (20)		   symbol - PC - 8 + addend    	  */

RELOC_NUMBER (R_PARISC_PCREL17C,  	13)
/*		branch (20)		   symbol - PC - 8 + addend    	  */

RELOC_NUMBER (R_PARISC_PCREL14R,  	14)
/*		load/store (1)		   R(symbol - PC - 8 + addend) 	  */

RELOC_NUMBER (R_PARISC_PCREL14F,  	15)
/*		load/store (1)             symbol - PC - 8 + addend    	  */


/* DP-relative relocation types.  */
RELOC_NUMBER (R_PARISC_DPREL21L,  	18)
/*		long immediate (7)         LR(symbol - GP, addend)  	  */

RELOC_NUMBER (R_PARISC_DPREL14WR, 	19)
/*		load/store mod. comp. (2)  RR(symbol - GP, addend)  	  */

RELOC_NUMBER (R_PARISC_DPREL14DR, 	20)
/*		load/store doubleword (3)  RR(symbol - GP, addend)  	  */

RELOC_NUMBER (R_PARISC_DPREL14R,  	22)
/*		load/store (1)             RR(symbol - GP, addend)  	  */

RELOC_NUMBER (R_PARISC_DPREL14F,  	23)
/*		load/store (1)             symbol - GP + addend     	  */


/* Data linkage table (DLT) relocation types

   SOM DLT_REL fixup requests are used to for static data references
   from position-independent code within shared libraries.  They are
   similar to the GOT relocation types in some SVR4 implementations.  */

RELOC_NUMBER (R_PARISC_DLTREL21L,     	26)
/*		long immediate (7)         LR(symbol - GP, addend) 	  */

RELOC_NUMBER (R_PARISC_DLTREL14R,     	30)
/*		load/store (1)             RR(symbol - GP, addend) 	  */

RELOC_NUMBER (R_PARISC_DLTREL14F,     	31)
/*		load/store (1)             symbol - GP + addend    	  */


/* DLT indirect relocation types  */
RELOC_NUMBER (R_PARISC_DLTIND21L,     	34)
/*		long immediate (7)         L(ltoff(symbol + addend)) 	  */

RELOC_NUMBER (R_PARISC_DLTIND14R,     	38)
/*		load/store (1)             R(ltoff(symbol + addend)) 	  */

RELOC_NUMBER (R_PARISC_DLTIND14F,     	39)
/*		load/store (1)             ltoff(symbol + addend)    	  */


/* Base relative relocation types.  Ugh.  These imply lots of state */
RELOC_NUMBER (R_PARISC_SETBASE,       	40)
/*		none                       no reloc; base := sym     	  */

RELOC_NUMBER (R_PARISC_SECREL32,      	41)
/*		32-bit word                symbol - SECT + addend    	  */

RELOC_NUMBER (R_PARISC_BASEREL21L,    	42)
/*		long immediate (7)         LR(symbol - base, addend) 	  */

RELOC_NUMBER (R_PARISC_BASEREL17R,    	43)
/*		branch external (19)       RR(symbol - base, addend) 	  */

RELOC_NUMBER (R_PARISC_BASEREL17F,    	44)
/*		branch external (19)       symbol - base + addend    	  */

RELOC_NUMBER (R_PARISC_BASEREL14R,    	46)
/*		load/store (1)             RR(symbol - base, addend) 	  */

RELOC_NUMBER (R_PARISC_BASEREL14F,    	47)
/*		load/store (1)             symbol - base, addend     	  */


/* Segment relative relocation types.  */
RELOC_NUMBER (R_PARISC_SEGBASE,       	48)
/*		none                       no relocation; SB := sym  	  */

RELOC_NUMBER (R_PARISC_SEGREL32,      	49)
/*		32-bit word                symbol - SB + addend 	  */
  

/* Offsets from the PLT.  */  
RELOC_NUMBER (R_PARISC_PLTOFF21L,     	50)
/*		long immediate (7)         LR(pltoff(symbol), addend) 	  */

RELOC_NUMBER (R_PARISC_PLTOFF14R,     	54)
/*		load/store (1)             RR(pltoff(symbol), addend) 	  */

RELOC_NUMBER (R_PARISC_PLTOFF14F,     	55)
/*		load/store (1)             pltoff(symbol) + addend    	  */


RELOC_NUMBER (R_PARISC_LTOFF_FPTR32,  	57)
/*		32-bit word                ltoff(fptr(symbol+addend))     */

RELOC_NUMBER (R_PARISC_LTOFF_FPTR21L, 	58)
/*		long immediate (7)         L(ltoff(fptr(symbol+addend)))  */

RELOC_NUMBER (R_PARISC_LTOFF_FPTR14R, 	62)
/*		load/store (1)             R(ltoff(fptr(symbol+addend)))  */


RELOC_NUMBER (R_PARISC_FPTR64,        	64)
/*		64-bit doubleword          fptr(symbol+addend) 		  */


/* Plabel relocation types.  */	 
RELOC_NUMBER (R_PARISC_PLABEL32,      	65)
/*		32-bit word	  	   fptr(symbol) 		  */

RELOC_NUMBER (R_PARISC_PLABEL21L,     	66)
/*		long immediate (7)         L(fptr(symbol))		  */

RELOC_NUMBER (R_PARISC_PLABEL14R,     	70)
/*		load/store (1)             R(fptr(symbol))		  */

  
/* PCREL relocations.  */  
RELOC_NUMBER (R_PARISC_PCREL64,       	72)
/*		64-bit doubleword          symbol - PC - 8 + addend       */

RELOC_NUMBER (R_PARISC_PCREL22C,      	73)
/*		branch & link (21)         symbol - PC - 8 + addend       */

RELOC_NUMBER (R_PARISC_PCREL22F,      	74)
/*		branch & link (21)         symbol - PC - 8 + addend       */

RELOC_NUMBER (R_PARISC_PCREL14WR,     	75)
/*		load/store mod. comp. (2)  R(symbol - PC - 8 + addend)    */

RELOC_NUMBER (R_PARISC_PCREL14DR,     	76)
/*		load/store doubleword (3)  R(symbol - PC - 8 + addend)    */

RELOC_NUMBER (R_PARISC_PCREL16F,      	77)
/*		load/store (1)             symbol - PC - 8 + addend       */

RELOC_NUMBER (R_PARISC_PCREL16WF,     	78)
/*		load/store mod. comp. (2)  symbol - PC - 8 + addend       */

RELOC_NUMBER (R_PARISC_PCREL16DF,     	79)
/*		load/store doubleword (3)  symbol - PC - 8 + addend       */


RELOC_NUMBER (R_PARISC_DIR64,         	80)
/*		64-bit doubleword          symbol + addend    		  */

RELOC_NUMBER (R_PARISC_DIR14WR,       	83)
/*		load/store mod. comp. (2)  RR(symbol, addend) 		  */

RELOC_NUMBER (R_PARISC_DIR14DR,       	84)
/*		load/store doubleword (3)  RR(symbol, addend) 		  */

RELOC_NUMBER (R_PARISC_DIR16F,        	85)
/*		load/store (1)             symbol + addend    		  */

RELOC_NUMBER (R_PARISC_DIR16WF,       	86)
/*		load/store mod. comp. (2)  symbol + addend    		  */

RELOC_NUMBER (R_PARISC_DIR16DF,       	87)
/*		load/store doubleword (3)  symbol + addend    		  */
  
RELOC_NUMBER (R_PARISC_GPREL64,       	88)
/*		64-bit doubleword          symbol - GP + addend 	  */
  
RELOC_NUMBER (R_PARISC_DLTREL14WR,    	91)
/*		load/store mod. comp. (2)  RR(symbol - GP, addend) 	  */

RELOC_NUMBER (R_PARISC_DLTREL14DR,    	92)
/*		load/store doubleword (3)  RR(symbol - GP, addend) 	  */

RELOC_NUMBER (R_PARISC_GPREL16F,      	93)
/*		load/store (1)             symbol - GP + addend    	  */

RELOC_NUMBER (R_PARISC_GPREL16WF,     	94)
/*		load/store mod. comp. (2)  symbol - GP + addend    	  */

RELOC_NUMBER (R_PARISC_GPREL16DF,     	95)
/*		load/store doubleword (3)  symbol - GP + addend    	  */


RELOC_NUMBER (R_PARISC_LTOFF64,      	96)
/*		64-bit doubleword          ltoff(symbol + addend)    	  */

RELOC_NUMBER (R_PARISC_DLTIND14WR,   	99)
/*		load/store mod. comp. (2)  R(ltoff(symbol + addend)) 	  */

RELOC_NUMBER (R_PARISC_DLTIND14DR,     100)
/*		load/store doubleword (3)  R(ltoff(symbol + addend)) 	  */

RELOC_NUMBER (R_PARISC_LTOFF16F,       101)
/*		load/store (1)             ltoff(symbol + addend)    	  */

RELOC_NUMBER (R_PARISC_LTOFF16WF,      102)
/*		load/store mod. comp. (2)  ltoff(symbol + addend)    	  */

RELOC_NUMBER (R_PARISC_LTOFF16DF,      103)
/*		load/store doubleword (3)  ltoff(symbol + addend)    	  */


RELOC_NUMBER (R_PARISC_SECREL64,       104)
/*		64-bit doubleword          symbol - SECT + addend 	  */

RELOC_NUMBER (R_PARISC_BASEREL14WR,    107)
/*		load/store mod. comp. (2)  RR(symbol - base, addend) 	  */

RELOC_NUMBER (R_PARISC_BASEREL14DR,    108)
/*		load/store doubleword (3)  RR(symbol - base, addend) 	  */


RELOC_NUMBER (R_PARISC_SEGREL64,       112)
/*		64-bit doubleword          symbol - SB + addend 	  */
  
RELOC_NUMBER (R_PARISC_PLTOFF14WR,     115)
/*		load/store mod. comp. (2)  RR(pltoff(symbol), addend) 	  */

RELOC_NUMBER (R_PARISC_PLTOFF14DR,     116)    
/*		load/store doubleword (3)  RR(pltoff(symbol), addend) 	  */

RELOC_NUMBER (R_PARISC_PLTOFF16F,      117)    
/*		load/store (1)             pltoff(symbol) + addend    	  */

RELOC_NUMBER (R_PARISC_PLTOFF16WF,     118)    
/*		load/store mod. comp. (2)  pltoff(symbol) + addend    	  */

RELOC_NUMBER (R_PARISC_PLTOFF16DF,     119)    
/*		load/store doubleword (3)  pltoff(symbol) + addend    	  */


RELOC_NUMBER (R_PARISC_LTOFF_FPTR64,   120)
/*		64-bit doubleword          ltoff(fptr(symbol+addend))     */

RELOC_NUMBER (R_PARISC_LTOFF_FPTR14WR, 123)
/*		load/store mod. comp. (2)  R(ltoff(fptr(symbol+addend)))  */

RELOC_NUMBER (R_PARISC_LTOFF_FPTR14DR, 124)
/*		load/store doubleword (3)  R(ltoff(fptr(symbol+addend)))  */

RELOC_NUMBER (R_PARISC_LTOFF_FPTR16F,  125)
/*		load/store (1)             ltoff(fptr(symbol+addend))     */

RELOC_NUMBER (R_PARISC_LTOFF_FPTR16WF, 126)
/*		load/store mod. comp. (2)  ltoff(fptr(symbol+addend))     */

RELOC_NUMBER (R_PARISC_LTOFF_FPTR16DF, 127)
/*		load/store doubleword (3)  ltoff(fptr(symbol+addend))     */


RELOC_NUMBER (R_PARISC_COPY, 	       128)
/*		data                       Dynamic relocations only 	  */

RELOC_NUMBER (R_PARISC_IPLT, 	       129)
/*		plt                                                 	  */

RELOC_NUMBER (R_PARISC_EPLT, 	       130)
/*		plt                                                 	  */


RELOC_NUMBER (R_PARISC_TPREL32,        153)
/*		32-bit word                symbol - TP + addend    	  */

RELOC_NUMBER (R_PARISC_TPREL21L,       154)
/*		long immediate (7)         LR(symbol - TP, addend) 	  */

RELOC_NUMBER (R_PARISC_TPREL14R,       158)
/*		load/store (1)             RR(symbol - TP, addend) 	  */


RELOC_NUMBER (R_PARISC_LTOFF_TP21L,    162)
/*		long immediate (7)         L(ltoff(symbol - TP + addend)) */

RELOC_NUMBER (R_PARISC_LTOFF_TP14R,    166)
/*		load/store (1)             R(ltoff(symbol - TP + addend)) */

RELOC_NUMBER (R_PARISC_LTOFF_TP14F,    167)
/*		load/store (1)             ltoff(symbol - TP + addend)    */


RELOC_NUMBER (R_PARISC_TPREL64,        216)
/*		64-bit word                symbol - TP + addend        	  */

RELOC_NUMBER (R_PARISC_TPREL14WR,      219)    	  
/*		load/store mod. comp. (2)  RR(symbol - TP, addend)     	  */

RELOC_NUMBER (R_PARISC_TPREL14DR,      220)    	  
/*		load/store doubleword (3)  RR(symbol - TP, addend)     	  */

RELOC_NUMBER (R_PARISC_TPREL16F,       221)    	  
/*		load/store (1)             symbol - TP + addend        	  */

RELOC_NUMBER (R_PARISC_TPREL16WF,      222)    	  
/*		load/store mod. comp. (2)  symbol - TP + addend        	  */

RELOC_NUMBER (R_PARISC_TPREL16DF,      223)    	  
/*		load/store doubleword (3)  symbol - TP + addend        	  */


RELOC_NUMBER (R_PARISC_LTOFF_TP64,     224)
/*		64-bit doubleword          ltoff(symbol - TP + addend)    */

RELOC_NUMBER (R_PARISC_LTOFF_TP14WR,   227)
/*		load/store mod. comp. (2)  R(ltoff(symbol - TP + addend)) */

RELOC_NUMBER (R_PARISC_LTOFF_TP14DR,   228)
/*		load/store doubleword (3)  R(ltoff(symbol - TP + addend)) */

RELOC_NUMBER (R_PARISC_LTOFF_TP16F,    229)
/*		load/store (1)             ltoff(symbol - TP + addend)    */

RELOC_NUMBER (R_PARISC_LTOFF_TP16WF,   230)
/*		load/store mod. comp. (2)  ltoff(symbol - TP + addend)    */

RELOC_NUMBER (R_PARISC_LTOFF_TP16DF,   231)
/*		load/store doubleword (3)  ltoff(symbol - TP + addend)    */

RELOC_NUMBER (R_PARISC_GNU_VTENTRY,    232)
RELOC_NUMBER (R_PARISC_GNU_VTINHERIT,  233)

RELOC_NUMBER (R_PARISC_TLS_GD21L,      234)
RELOC_NUMBER (R_PARISC_TLS_GD14R,      235)
RELOC_NUMBER (R_PARISC_TLS_GDCALL,     236)
RELOC_NUMBER (R_PARISC_TLS_LDM21L,     237)
RELOC_NUMBER (R_PARISC_TLS_LDM14R,     238)
RELOC_NUMBER (R_PARISC_TLS_LDMCALL,    239)
RELOC_NUMBER (R_PARISC_TLS_LDO21L,     240)
RELOC_NUMBER (R_PARISC_TLS_LDO14R,     241)
RELOC_NUMBER (R_PARISC_TLS_DTPMOD32,   242)
RELOC_NUMBER (R_PARISC_TLS_DTPMOD64,   243)
RELOC_NUMBER (R_PARISC_TLS_DTPOFF32,   244)
RELOC_NUMBER (R_PARISC_TLS_DTPOFF64,   245)

END_RELOC_NUMBERS (R_PARISC_UNIMPLEMENTED)

#define R_PARISC_TLS_LE21L     R_PARISC_TPREL21L
#define R_PARISC_TLS_LE14R     R_PARISC_TPREL14R
#define R_PARISC_TLS_IE21L     R_PARISC_LTOFF_TP21L
#define R_PARISC_TLS_IE14R     R_PARISC_LTOFF_TP14R
#define R_PARISC_TLS_TPREL32   R_PARISC_TPREL32
#define R_PARISC_TLS_TPREL64   R_PARISC_TPREL64

#ifndef RELOC_MACROS_GEN_FUNC
typedef enum elf_hppa_reloc_type elf_hppa_reloc_type;
#endif

#define PT_PARISC_ARCHEXT	0x70000000
#define PT_PARISC_UNWIND	0x70000001
#define PT_PARISC_WEAKORDER	0x70000002

/* Flag bits in sh_flags of ElfXX_Shdr.  */
#define SHF_HP_TLS              0x01000000
#define SHF_HP_NEAR_SHARED      0x02000000
#define SHF_HP_FAR_SHARED       0x04000000
#define SHF_HP_COMDAT           0x08000000
#define SHF_HP_CONST            0x00800000

/* Reserved section header indices.  */
#define SHN_TLS_COMMON          (SHN_LOOS + 0x0)
#define SHN_NS_COMMON           (SHN_LOOS + 0x1)
#define SHN_FS_COMMON           (SHN_LOOS + 0x2)
#define SHN_NS_UNDEF            (SHN_LOOS + 0x3)
#define SHN_FS_UNDEF            (SHN_LOOS + 0x4)
#define SHN_HP_EXTERN           (SHN_LOOS + 0x5)
#define SHN_HP_EXTHINT          (SHN_LOOS + 0x6)
#define SHN_HP_UNDEF_BIND_IMM   (SHN_LOOS + 0x7)

/* Values of sh_type in ElfXX_Shdr.  */
#define SHT_HP_OVLBITS  (SHT_LOOS + 0x0)
#define SHT_HP_DLKM     (SHT_LOOS + 0x1)
#define SHT_HP_COMDAT   (SHT_LOOS + 0x2)
#define SHT_HP_OBJDICT  (SHT_LOOS + 0x3)
#define SHT_HP_ANNOT    (SHT_LOOS + 0x4)

/* Flag bits in p_flags of ElfXX_Phdr.  */
#define PF_HP_CODE		0x00040000
#define PF_HP_MODIFY		0x00080000
#define PF_HP_PAGE_SIZE		0x00100000
#define PF_HP_FAR_SHARED	0x00200000
#define PF_HP_NEAR_SHARED	0x00400000
#define PF_HP_LAZYSWAP		0x00800000
#define PF_HP_CODE_DEPR		0x01000000
#define PF_HP_MODIFY_DEPR	0x02000000
#define PF_HP_LAZYSWAP_DEPR	0x04000000
#define PF_PARISC_SBP		0x08000000
#define PF_HP_SBP		0x08000000


/* Processor specific dynamic array tags.  */

/* Arggh.  HP's tools define these symbols based on the
   old value of DT_LOOS.  So we must do the same to be
   compatible.  */
#define DT_HP_LOAD_MAP		(OLD_DT_LOOS + 0x0)
#define DT_HP_DLD_FLAGS		(OLD_DT_LOOS + 0x1)
#define DT_HP_DLD_HOOK		(OLD_DT_LOOS + 0x2)
#define DT_HP_UX10_INIT		(OLD_DT_LOOS + 0x3)
#define DT_HP_UX10_INITSZ	(OLD_DT_LOOS + 0x4)
#define DT_HP_PREINIT		(OLD_DT_LOOS + 0x5)
#define DT_HP_PREINITSZ		(OLD_DT_LOOS + 0x6)
#define DT_HP_NEEDED		(OLD_DT_LOOS + 0x7)
#define DT_HP_TIME_STAMP	(OLD_DT_LOOS + 0x8)
#define DT_HP_CHECKSUM		(OLD_DT_LOOS + 0x9)
#define DT_HP_GST_SIZE		(OLD_DT_LOOS + 0xa)
#define DT_HP_GST_VERSION	(OLD_DT_LOOS + 0xb)
#define DT_HP_GST_HASHVAL	(OLD_DT_LOOS + 0xc)
#define DT_HP_EPLTREL		(OLD_DT_LOOS + 0xd)
#define DT_HP_EPLTRELSZ		(OLD_DT_LOOS + 0xe)
#define DT_HP_FILTERED		(OLD_DT_LOOS + 0xf)
#define DT_HP_FILTER_TLS	(OLD_DT_LOOS + 0x10)
#define DT_HP_COMPAT_FILTERED	(OLD_DT_LOOS + 0x11)
#define DT_HP_LAZYLOAD		(OLD_DT_LOOS + 0x12)
#define DT_HP_BIND_NOW_COUNT	(OLD_DT_LOOS + 0x13)
#define DT_PLT			(OLD_DT_LOOS + 0x14)
#define DT_PLT_SIZE		(OLD_DT_LOOS + 0x15)
#define DT_DLT			(OLD_DT_LOOS + 0x16)
#define DT_DLT_SIZE		(OLD_DT_LOOS + 0x17)

/* Values for DT_HP_DLD_FLAGS.  */
#define DT_HP_DEBUG_PRIVATE		0x00001 /* Map text private */
#define DT_HP_DEBUG_CALLBACK		0x00002 /* Callback */
#define DT_HP_DEBUG_CALLBACK_BOR	0x00004 /* BOR callback */
#define DT_HP_NO_ENVVAR			0x00008 /* No env var */
#define DT_HP_BIND_NOW			0x00010 /* Bind now */
#define DT_HP_BIND_NONFATAL		0x00020 /* Bind non-fatal */
#define DT_HP_BIND_VERBOSE		0x00040 /* Bind verbose */
#define DT_HP_BIND_RESTRICTED		0x00080 /* Bind restricted */
#define DT_HP_BIND_SYMBOLIC		0x00100 /* Bind symbolic */
#define DT_HP_RPATH_FIRST		0x00200 /* RPATH first */
#define DT_HP_BIND_DEPTH_FIRST		0x00400 /* Bind depth-first */
#define DT_HP_GST			0x00800 /* Dld global sym table */
#define DT_HP_SHLIB_FIXED		0x01000 /* shared vtable support */
#define DT_HP_MERGE_SHLIB_SEG		0x02000 /* merge shlib data segs */
#define DT_HP_NODELETE			0x04000 /* never unload */
#define DT_HP_GROUP			0x08000 /* bind only within group */
#define DT_HP_PROTECT_LINKAGE_TABLE	0x10000 /* protected linkage table */

/* Program header extensions.  */
#define PT_HP_TLS		(PT_LOOS + 0x0)
#define PT_HP_CORE_NONE		(PT_LOOS + 0x1)
#define PT_HP_CORE_VERSION	(PT_LOOS + 0x2)
#define PT_HP_CORE_KERNEL	(PT_LOOS + 0x3)
#define PT_HP_CORE_COMM		(PT_LOOS + 0x4)
#define PT_HP_CORE_PROC		(PT_LOOS + 0x5)
#define PT_HP_CORE_LOADABLE	(PT_LOOS + 0x6)
#define PT_HP_CORE_STACK	(PT_LOOS + 0x7)
#define PT_HP_CORE_SHM		(PT_LOOS + 0x8)
#define PT_HP_CORE_MMF		(PT_LOOS + 0x9)
#define PT_HP_PARALLEL		(PT_LOOS + 0x10)
#define PT_HP_FASTBIND		(PT_LOOS + 0x11)
#define PT_HP_OPT_ANNOT		(PT_LOOS + 0x12)
#define PT_HP_HSL_ANNOT		(PT_LOOS + 0x13)
#define PT_HP_STACK		(PT_LOOS + 0x14)
#define PT_HP_CORE_UTSNAME	(PT_LOOS + 0x15)

/* Binding information.  */
#define STB_HP_ALIAS		(STB_LOOS + 0x0)

/* Additional symbol types.  */
#define STT_HP_OPAQUE		(STT_LOOS + 0x1)
#define STT_HP_STUB		(STT_LOOS + 0x2)

/* Note types.  */
#define NT_HP_COMPILER		1
#define NT_HP_COPYRIGHT		2
#define NT_HP_VERSION		3
#define NT_HP_SRCFILE_INFO	4
#define NT_HP_LINKER		5
#define NT_HP_INSTRUMENTED	6
#define NT_HP_UX_OPTIONS	7

#endif /* _ELF_HPPA_H */
