/* ECOFF support on MIPS machines.
   coff/ecoff.h must be included before this file.
   
   Copyright 1999, 2004 Free Software Foundation, Inc.

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

#define DO_NOT_DEFINE_AOUTHDR
#define L_LNNO_SIZE 4
#include "coff/external.h"

/* Magic numbers are defined in coff/ecoff.h.  */
#define MIPS_ECOFF_BADMAG(x) (((x).f_magic!=MIPS_MAGIC_1) && \
			      ((x).f_magic!=MIPS_MAGIC_LITTLE) &&\
			      ((x).f_magic!=MIPS_MAGIC_BIG) && \
			      ((x).f_magic!=MIPS_MAGIC_LITTLE2) && \
			      ((x).f_magic!=MIPS_MAGIC_BIG2) && \
			      ((x).f_magic!=MIPS_MAGIC_LITTLE3) && \
			      ((x).f_magic!=MIPS_MAGIC_BIG3))


/********************** AOUT "OPTIONAL HEADER" **********************/

typedef struct external_aouthdr
{
  unsigned char magic[2];	/* type of file				*/
  unsigned char	vstamp[2];	/* version stamp			*/
  unsigned char	tsize[4];	/* text size in bytes, padded to FW bdry*/
  unsigned char	dsize[4];	/* initialized data "  "		*/
  unsigned char	bsize[4];	/* uninitialized data "   "		*/
  unsigned char	entry[4];	/* entry pt.				*/
  unsigned char text_start[4];	/* base of text used for this file */
  unsigned char data_start[4];	/* base of data used for this file */
  unsigned char bss_start[4];	/* base of bss used for this file */
  unsigned char gprmask[4];	/* ?? */
  unsigned char cprmask[4][4];	/* ?? */
  unsigned char gp_value[4];	/* value for gp register */
} AOUTHDR;

/* compute size of a header */

#define AOUTSZ 56
#define AOUTHDRSZ 56

/********************** RELOCATION DIRECTIVES **********************/

struct external_reloc
  {
    unsigned char r_vaddr[4];
    unsigned char r_bits[4];
  };

#define RELOC struct external_reloc
#define RELSZ 8

/* MIPS ECOFF uses a packed 8 byte format for relocs.  These constants
   are used to unpack the r_bits field.  */

#define RELOC_BITS0_SYMNDX_SH_LEFT_BIG		16
#define RELOC_BITS0_SYMNDX_SH_LEFT_LITTLE	0

#define RELOC_BITS1_SYMNDX_SH_LEFT_BIG		8
#define RELOC_BITS1_SYMNDX_SH_LEFT_LITTLE	8

#define RELOC_BITS2_SYMNDX_SH_LEFT_BIG		0
#define RELOC_BITS2_SYMNDX_SH_LEFT_LITTLE	16

/* Originally, ECOFF used four bits for the reloc type and had three
   reserved bits.  Irix 4 added another bit for the reloc type, which
   was easy because it was big endian and one of the spare bits became
   the new most significant bit.  To make this also work for little
   endian ECOFF, we need to wrap one of the reserved bits around to
   become the most significant bit of the reloc type.  */
#define RELOC_BITS3_TYPE_BIG			0x3E
#define RELOC_BITS3_TYPE_SH_BIG			1
#define RELOC_BITS3_TYPE_LITTLE			0x78
#define RELOC_BITS3_TYPE_SH_LITTLE		3
#define RELOC_BITS3_TYPEHI_LITTLE		0x04
#define RELOC_BITS3_TYPEHI_SH_LITTLE		2

#define RELOC_BITS3_EXTERN_BIG			0x01
#define RELOC_BITS3_EXTERN_LITTLE		0x80

/* The r_type field in a reloc is one of the following values.  I
   don't know if any other values can appear.  These seem to be all
   that occur in the Ultrix 4.2 libraries.  */
#define MIPS_R_IGNORE	0
#define MIPS_R_REFHALF	1
#define MIPS_R_REFWORD	2
#define MIPS_R_JMPADDR	3
#define MIPS_R_REFHI	4
#define MIPS_R_REFLO	5
#define MIPS_R_GPREL	6
#define MIPS_R_LITERAL	7

/* FIXME: This relocation is used (internally only) to represent branches
   when assembling.  It should never appear in output files, and  
   be removed.  (It used to be used for embedded-PIC support.)  */
#define MIPS_R_PCREL16	12

/********************** STABS **********************/

#define MIPS_IS_STAB ECOFF_IS_STAB
#define MIPS_MARK_STAB ECOFF_MARK_STAB
#define MIPS_UNMARK_STAB ECOFF_UNMARK_STAB

/********************** SYMBOLIC INFORMATION **********************/

/* Written by John Gilmore.  */

/* ECOFF uses COFF-like section structures, but its own symbol format.
   This file defines the symbol format in fields whose size and alignment
   will not vary on different host systems.  */

/* File header as a set of bytes */

struct hdr_ext
{
	unsigned char 	h_magic[2];
	unsigned char	h_vstamp[2];
	unsigned char	h_ilineMax[4];
	unsigned char	h_cbLine[4];
	unsigned char	h_cbLineOffset[4];
	unsigned char	h_idnMax[4];
	unsigned char	h_cbDnOffset[4];
	unsigned char	h_ipdMax[4];
	unsigned char	h_cbPdOffset[4];
	unsigned char	h_isymMax[4];
	unsigned char	h_cbSymOffset[4];
	unsigned char	h_ioptMax[4];
	unsigned char	h_cbOptOffset[4];
	unsigned char	h_iauxMax[4];
	unsigned char	h_cbAuxOffset[4];
	unsigned char	h_issMax[4];
	unsigned char	h_cbSsOffset[4];
	unsigned char	h_issExtMax[4];
	unsigned char	h_cbSsExtOffset[4];
	unsigned char	h_ifdMax[4];
	unsigned char	h_cbFdOffset[4];
	unsigned char	h_crfd[4];
	unsigned char	h_cbRfdOffset[4];
	unsigned char	h_iextMax[4];
	unsigned char	h_cbExtOffset[4];
};

/* File descriptor external record */

struct fdr_ext
{
	unsigned char	f_adr[4];
	unsigned char	f_rss[4];
	unsigned char	f_issBase[4];
	unsigned char	f_cbSs[4];
	unsigned char	f_isymBase[4];
	unsigned char	f_csym[4];
	unsigned char	f_ilineBase[4];
	unsigned char	f_cline[4];
	unsigned char	f_ioptBase[4];
	unsigned char	f_copt[4];
	unsigned char	f_ipdFirst[2];
	unsigned char	f_cpd[2];
	unsigned char	f_iauxBase[4];
	unsigned char	f_caux[4];
	unsigned char	f_rfdBase[4];
	unsigned char	f_crfd[4];
	unsigned char	f_bits1[1];
	unsigned char	f_bits2[3];
	unsigned char	f_cbLineOffset[4];
	unsigned char	f_cbLine[4];
};

#define	FDR_BITS1_LANG_BIG		0xF8
#define	FDR_BITS1_LANG_SH_BIG		3
#define	FDR_BITS1_LANG_LITTLE		0x1F
#define	FDR_BITS1_LANG_SH_LITTLE	0

#define	FDR_BITS1_FMERGE_BIG		0x04
#define	FDR_BITS1_FMERGE_LITTLE		0x20

#define	FDR_BITS1_FREADIN_BIG		0x02
#define	FDR_BITS1_FREADIN_LITTLE	0x40

#define	FDR_BITS1_FBIGENDIAN_BIG	0x01
#define	FDR_BITS1_FBIGENDIAN_LITTLE	0x80

#define	FDR_BITS2_GLEVEL_BIG		0xC0
#define	FDR_BITS2_GLEVEL_SH_BIG		6
#define	FDR_BITS2_GLEVEL_LITTLE		0x03
#define	FDR_BITS2_GLEVEL_SH_LITTLE	0

/* We ignore the `reserved' field in bits2. */

/* Procedure descriptor external record */

struct pdr_ext
{
	unsigned char	p_adr[4];
	unsigned char	p_isym[4];
	unsigned char	p_iline[4];
	unsigned char	p_regmask[4];
	unsigned char	p_regoffset[4];
	unsigned char	p_iopt[4];
	unsigned char	p_fregmask[4];
	unsigned char	p_fregoffset[4];
	unsigned char	p_frameoffset[4];
	unsigned char	p_framereg[2];
	unsigned char	p_pcreg[2];
	unsigned char	p_lnLow[4];
	unsigned char	p_lnHigh[4];
	unsigned char	p_cbLineOffset[4];
};

/* Runtime procedure table */

struct rpdr_ext
{
	unsigned char	p_adr[4];
	unsigned char	p_regmask[4];
	unsigned char	p_regoffset[4];
	unsigned char	p_fregmask[4];
	unsigned char	p_fregoffset[4];
	unsigned char	p_frameoffset[4];
	unsigned char	p_framereg[2];
	unsigned char	p_pcreg[2];
	unsigned char	p_irpss[4];
	unsigned char	p_reserved[4];
	unsigned char	p_exception_info[4];
};

/* Line numbers */

struct line_ext
{
	unsigned char	l_line[4];
};

/* Symbol external record */

struct sym_ext
{
	unsigned char	s_iss[4];
	unsigned char	s_value[4];
	unsigned char	s_bits1[1];
	unsigned char	s_bits2[1];
	unsigned char	s_bits3[1];
	unsigned char	s_bits4[1];
};

#define	SYM_BITS1_ST_BIG		0xFC
#define	SYM_BITS1_ST_SH_BIG		2
#define	SYM_BITS1_ST_LITTLE		0x3F
#define	SYM_BITS1_ST_SH_LITTLE		0

#define	SYM_BITS1_SC_BIG		0x03
#define	SYM_BITS1_SC_SH_LEFT_BIG	3
#define	SYM_BITS1_SC_LITTLE		0xC0
#define	SYM_BITS1_SC_SH_LITTLE		6

#define	SYM_BITS2_SC_BIG		0xE0
#define	SYM_BITS2_SC_SH_BIG		5
#define	SYM_BITS2_SC_LITTLE		0x07
#define	SYM_BITS2_SC_SH_LEFT_LITTLE	2

#define	SYM_BITS2_RESERVED_BIG		0x10
#define	SYM_BITS2_RESERVED_LITTLE	0x08

#define	SYM_BITS2_INDEX_BIG		0x0F
#define	SYM_BITS2_INDEX_SH_LEFT_BIG	16
#define	SYM_BITS2_INDEX_LITTLE		0xF0
#define	SYM_BITS2_INDEX_SH_LITTLE	4

#define	SYM_BITS3_INDEX_SH_LEFT_BIG	8
#define	SYM_BITS3_INDEX_SH_LEFT_LITTLE	4

#define	SYM_BITS4_INDEX_SH_LEFT_BIG	0
#define	SYM_BITS4_INDEX_SH_LEFT_LITTLE	12

/* External symbol external record */

struct ext_ext
{
	unsigned char	es_bits1[1];
	unsigned char	es_bits2[1];
	unsigned char	es_ifd[2];
	struct	sym_ext es_asym;
};

#define	EXT_BITS1_JMPTBL_BIG		0x80
#define	EXT_BITS1_JMPTBL_LITTLE		0x01

#define	EXT_BITS1_COBOL_MAIN_BIG	0x40
#define	EXT_BITS1_COBOL_MAIN_LITTLE	0x02

#define	EXT_BITS1_WEAKEXT_BIG		0x20
#define	EXT_BITS1_WEAKEXT_LITTLE	0x04

/* Dense numbers external record */

struct dnr_ext
{
	unsigned char	d_rfd[4];
	unsigned char	d_index[4];
};

/* Relative file descriptor */

struct rfd_ext
{
  unsigned char	rfd[4];
};

/* Optimizer symbol external record */

struct opt_ext
{
  unsigned char o_bits1[1];
  unsigned char o_bits2[1];
  unsigned char o_bits3[1];
  unsigned char o_bits4[1];
  struct rndx_ext o_rndx;
  unsigned char o_offset[4];
};

#define OPT_BITS2_VALUE_SH_LEFT_BIG	16
#define OPT_BITS2_VALUE_SH_LEFT_LITTLE	0

#define OPT_BITS3_VALUE_SH_LEFT_BIG	8
#define OPT_BITS3_VALUE_SH_LEFT_LITTLE	8

#define OPT_BITS4_VALUE_SH_LEFT_BIG	0
#define OPT_BITS4_VALUE_SH_LEFT_LITTLE	16
