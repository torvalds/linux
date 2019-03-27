/* ECOFF support on Alpha machines.
   coff/ecoff.h must be included before this file.

   Copyright 2001, 2005 Free Software Foundation, Inc.

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
   
/********************** FILE HEADER **********************/

struct external_filehdr
{
  unsigned char f_magic[2];	/* magic number			*/
  unsigned char f_nscns[2];	/* number of sections		*/
  unsigned char f_timdat[4];	/* time & date stamp		*/
  unsigned char f_symptr[8];	/* file pointer to symtab	*/
  unsigned char f_nsyms[4];	/* number of symtab entries	*/
  unsigned char f_opthdr[2];	/* sizeof(optional hdr)		*/
  unsigned char f_flags[2];	/* flags			*/
};

/* Magic numbers are defined in coff/ecoff.h.  */
#define ALPHA_ECOFF_BADMAG(x) \
  ((x).f_magic != ALPHA_MAGIC && (x).f_magic != ALPHA_MAGIC_BSD)

#define ALPHA_ECOFF_COMPRESSEDMAG(x) \
  ((x).f_magic == ALPHA_MAGIC_COMPRESSED)

/* The object type is encoded in the f_flags.  */
#define F_ALPHA_OBJECT_TYPE_MASK	0x3000
#define F_ALPHA_NO_SHARED		0x1000
#define F_ALPHA_SHARABLE		0x2000
#define F_ALPHA_CALL_SHARED		0x3000

#define	FILHDR	struct external_filehdr
#define	FILHSZ	24

/********************** AOUT "OPTIONAL HEADER" **********************/

typedef struct external_aouthdr
{
  unsigned char magic[2];	/* type of file				*/
  unsigned char	vstamp[2];	/* version stamp			*/
  unsigned char bldrev[2];	/* ?? */
  unsigned char padding[2];	/* pad to quadword boundary		*/
  unsigned char	tsize[8];	/* text size in bytes			*/
  unsigned char	dsize[8];	/* initialized data "  "		*/
  unsigned char	bsize[8];	/* uninitialized data "   "		*/
  unsigned char	entry[8];	/* entry pt.				*/
  unsigned char text_start[8];	/* base of text used for this file */
  unsigned char data_start[8];	/* base of data used for this file */
  unsigned char bss_start[8];	/* base of bss used for this file */
  unsigned char gprmask[4];	/* bitmask of general registers used */
  unsigned char fprmask[4];	/* bitmask of floating point registers used */
  unsigned char gp_value[8];	/* value for gp register */
} AOUTHDR;

/* compute size of a header */

#define AOUTSZ 80
#define AOUTHDRSZ 80

/********************** SECTION HEADER **********************/

struct external_scnhdr
{
  unsigned char	s_name[8];	/* section name			*/
  unsigned char	s_paddr[8];	/* physical address, aliased s_nlib */
  unsigned char	s_vaddr[8];	/* virtual address		*/
  unsigned char	s_size[8];	/* section size			*/
  unsigned char	s_scnptr[8];	/* file ptr to raw data for section */
  unsigned char	s_relptr[8];	/* file ptr to relocation	*/
  unsigned char	s_lnnoptr[8];	/* file ptr to line numbers	*/
  unsigned char	s_nreloc[2];	/* number of relocation entries	*/
  unsigned char	s_nlnno[2];	/* number of line number entries*/
  unsigned char	s_flags[4];	/* flags			*/
};

#define	SCNHDR	struct external_scnhdr
#define	SCNHSZ	64

/********************** RELOCATION DIRECTIVES **********************/

struct external_reloc 
{
  unsigned char r_vaddr[8];
  unsigned char r_symndx[4];
  unsigned char r_bits[4];
};

#define RELOC struct external_reloc
#define RELSZ 16

/* Constants to unpack the r_bits field.  The Alpha seems to always be
   little endian, so I haven't bothered to define big endian variants
   of these.  */

#define RELOC_BITS0_TYPE_LITTLE			0xff
#define RELOC_BITS0_TYPE_SH_LITTLE		0

#define RELOC_BITS1_EXTERN_LITTLE		0x01

#define RELOC_BITS1_OFFSET_LITTLE		0x7e
#define RELOC_BITS1_OFFSET_SH_LITTLE		1

#define RELOC_BITS1_RESERVED_LITTLE		0x80
#define RELOC_BITS1_RESERVED_SH_LITTLE		7
#define RELOC_BITS2_RESERVED_LITTLE		0xff
#define RELOC_BITS2_RESERVED_SH_LEFT_LITTLE	1
#define RELOC_BITS3_RESERVED_LITTLE		0x03
#define RELOC_BITS3_RESERVED_SH_LEFT_LITTLE	9

#define RELOC_BITS3_SIZE_LITTLE			0xfc
#define RELOC_BITS3_SIZE_SH_LITTLE		2

/* The r_type field in a reloc is one of the following values.  */
#define ALPHA_R_IGNORE		0
#define ALPHA_R_REFLONG		1
#define ALPHA_R_REFQUAD		2
#define ALPHA_R_GPREL32		3
#define ALPHA_R_LITERAL		4
#define ALPHA_R_LITUSE		5
#define ALPHA_R_GPDISP		6
#define ALPHA_R_BRADDR		7
#define ALPHA_R_HINT		8
#define ALPHA_R_SREL16		9
#define ALPHA_R_SREL32	       10
#define ALPHA_R_SREL64	       11
#define ALPHA_R_OP_PUSH	       12
#define ALPHA_R_OP_STORE       13
#define ALPHA_R_OP_PSUB	       14
#define ALPHA_R_OP_PRSHIFT     15
#define ALPHA_R_GPVALUE	       16
#define ALPHA_R_GPRELHIGH      17
#define ALPHA_R_GPRELLOW       18
#define ALPHA_R_IMMED          19

/* Overloaded reloc value used by Net- and OpenBSD.  */
#define ALPHA_R_LITERALSLEAZY  17

/* With ALPHA_R_LITUSE, the r_size field is one of the following values.  */
#define ALPHA_R_LU_BASE         1
#define ALPHA_R_LU_BYTOFF       2
#define ALPHA_R_LU_JSR          3

/* With ALPHA_R_IMMED, the r_size field is one of the following values.  */
#define ALPHA_R_IMMED_GP_16     1
#define ALPHA_R_IMMED_GP_HI32   2
#define ALPHA_R_IMMED_SCN_HI32  3
#define ALPHA_R_IMMED_BR_HI32   4
#define ALPHA_R_IMMED_LO32      5

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
	unsigned char	h_idnMax[4];
	unsigned char	h_ipdMax[4];
	unsigned char	h_isymMax[4];
	unsigned char	h_ioptMax[4];
	unsigned char	h_iauxMax[4];
	unsigned char	h_issMax[4];
	unsigned char	h_issExtMax[4];
	unsigned char	h_ifdMax[4];
	unsigned char	h_crfd[4];
	unsigned char	h_iextMax[4];
	unsigned char	h_cbLine[8];
	unsigned char	h_cbLineOffset[8];
	unsigned char	h_cbDnOffset[8];
	unsigned char	h_cbPdOffset[8];
	unsigned char	h_cbSymOffset[8];
	unsigned char	h_cbOptOffset[8];
	unsigned char	h_cbAuxOffset[8];
	unsigned char	h_cbSsOffset[8];
	unsigned char	h_cbSsExtOffset[8];
	unsigned char	h_cbFdOffset[8];
	unsigned char	h_cbRfdOffset[8];
	unsigned char	h_cbExtOffset[8];
};

/* File descriptor external record */

struct fdr_ext
{
	unsigned char	f_adr[8];
	unsigned char	f_cbLineOffset[8];
	unsigned char	f_cbLine[8];
	unsigned char	f_cbSs[8];
	unsigned char	f_rss[4];
	unsigned char	f_issBase[4];
	unsigned char	f_isymBase[4];
	unsigned char	f_csym[4];
	unsigned char	f_ilineBase[4];
	unsigned char	f_cline[4];
	unsigned char	f_ioptBase[4];
	unsigned char	f_copt[4];
	unsigned char	f_ipdFirst[4];
	unsigned char	f_cpd[4];
	unsigned char	f_iauxBase[4];
	unsigned char	f_caux[4];
	unsigned char	f_rfdBase[4];
	unsigned char	f_crfd[4];
	unsigned char	f_bits1[1];
	unsigned char	f_bits2[3];
	unsigned char	f_padding[4];
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

struct pdr_ext {
	unsigned char	p_adr[8];
	unsigned char	p_cbLineOffset[8];
	unsigned char	p_isym[4];
	unsigned char	p_iline[4];
	unsigned char	p_regmask[4];
	unsigned char	p_regoffset[4];
	unsigned char	p_iopt[4];
	unsigned char	p_fregmask[4];
	unsigned char	p_fregoffset[4];
	unsigned char	p_frameoffset[4];
	unsigned char	p_lnLow[4];
	unsigned char	p_lnHigh[4];
	unsigned char	p_gp_prologue[1];
	unsigned char	p_bits1[1];
	unsigned char	p_bits2[1];
	unsigned char	p_localoff[1];
	unsigned char	p_framereg[2];
	unsigned char	p_pcreg[2];
};

#define PDR_BITS1_GP_USED_BIG		0x80
#define PDR_BITS1_REG_FRAME_BIG		0x40
#define PDR_BITS1_PROF_BIG		0x20
#define PDR_BITS1_RESERVED_BIG		0x1f
#define PDR_BITS1_RESERVED_SH_LEFT_BIG	8
#define PDR_BITS2_RESERVED_BIG		0xff
#define PDR_BITS2_RESERVED_SH_BIG	0

#define PDR_BITS1_GP_USED_LITTLE	0x01
#define PDR_BITS1_REG_FRAME_LITTLE	0x02
#define PDR_BITS1_PROF_LITTLE		0x04
#define PDR_BITS1_RESERVED_LITTLE	0xf8
#define PDR_BITS1_RESERVED_SH_LITTLE	3
#define PDR_BITS2_RESERVED_LITTLE	0xff
#define PDR_BITS2_RESERVED_SH_LEFT_LITTLE 5

/* Line numbers */

struct line_ext {
	unsigned char	l_line[4];
};

/* Symbol external record */

struct sym_ext {
	unsigned char	s_value[8];
	unsigned char	s_iss[4];
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

struct ext_ext {
	struct	sym_ext es_asym;
	unsigned char	es_bits1[1];
	unsigned char	es_bits2[3];
	unsigned char	es_ifd[4];
};

#define	EXT_BITS1_JMPTBL_BIG		0x80
#define	EXT_BITS1_JMPTBL_LITTLE		0x01

#define	EXT_BITS1_COBOL_MAIN_BIG	0x40
#define	EXT_BITS1_COBOL_MAIN_LITTLE	0x02

#define	EXT_BITS1_WEAKEXT_BIG		0x20
#define	EXT_BITS1_WEAKEXT_LITTLE	0x04

/* Dense numbers external record */

struct dnr_ext {
	unsigned char	d_rfd[4];
	unsigned char	d_index[4];
};

/* Relative file descriptor */

struct rfd_ext {
  unsigned char	rfd[4];
};

/* Optimizer symbol external record */

struct opt_ext {
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
