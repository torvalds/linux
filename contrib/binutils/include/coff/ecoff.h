/* Generic ECOFF support.
   This does not include symbol information, found in sym.h and
   symconst.h.

   Copyright 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

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

#ifndef ECOFF_H
#define ECOFF_H

/* Mips magic numbers used in filehdr.  MIPS_MAGIC_LITTLE is used on
   little endian machines.  MIPS_MAGIC_BIG is used on big endian
   machines.  Where is MIPS_MAGIC_1 from?  */
#define MIPS_MAGIC_1 0x0180
#define MIPS_MAGIC_LITTLE 0x0162
#define MIPS_MAGIC_BIG 0x0160

/* These are the magic numbers used for MIPS code compiled at ISA
   level 2.  */
#define MIPS_MAGIC_LITTLE2 0x0166
#define MIPS_MAGIC_BIG2 0x0163

/* These are the magic numbers used for MIPS code compiled at ISA
   level 3.  */
#define MIPS_MAGIC_LITTLE3 0x142
#define MIPS_MAGIC_BIG3 0x140

/* Alpha magic numbers used in filehdr.  */
#define ALPHA_MAGIC 0x183
#define ALPHA_MAGIC_BSD 0x185
/* A compressed version of an ALPHA_MAGIC file created by DEC's tools.  */
#define ALPHA_MAGIC_COMPRESSED 0x188

/* Magic numbers used in a.out header.  */
#define ECOFF_AOUT_OMAGIC 0407	/* not demand paged (ld -N).  */
#define ECOFF_AOUT_ZMAGIC 0413	/* demand load format, eg normal ld output */

/* Names of special sections.  */
#define _TEXT   ".text"
#define _DATA   ".data"
#define _BSS    ".bss"
#define _RDATA	".rdata"
#define _SDATA	".sdata"
#define _SBSS	".sbss"
#define _LITA	".lita"
#define _LIT4	".lit4"
#define _LIT8	".lit8"
#define _LIB	".lib"
#define _INIT	".init"
#define _FINI	".fini"
#define _PDATA	".pdata"
#define _XDATA	".xdata"
#define _GOT	".got"
#define _HASH	".hash"
#define _DYNSYM	".dynsym"
#define _DYNSTR	".dynstr"
#define _RELDYN	".rel.dyn"
#define _CONFLIC ".conflic"
#define _COMMENT ".comment"
#define _LIBLIST ".liblist"
#define _DYNAMIC ".dynamic"
#define _RCONST	".rconst"

/* ECOFF uses some additional section flags.  */
#define STYP_RDATA	     0x100
#define STYP_SDATA	     0x200
#define STYP_SBSS	     0x400
#define STYP_GOT	    0x1000
#define STYP_DYNAMIC	    0x2000
#define STYP_DYNSYM	    0x4000
#define STYP_RELDYN	    0x8000
#define STYP_DYNSTR	   0x10000
#define STYP_HASH	   0x20000
#define STYP_LIBLIST	   0x40000
#define STYP_CONFLIC	  0x100000
#define STYP_ECOFF_FINI	 0x1000000
#define STYP_EXTENDESC	 0x2000000 /* 0x02FFF000 bits => scn type, rest clr */
#define STYP_LITA	 0x4000000
#define STYP_LIT8	 0x8000000
#define STYP_LIT4	0x10000000
#define STYP_ECOFF_LIB	0x40000000
#define STYP_ECOFF_INIT 0x80000000
#define STYP_OTHER_LOAD (STYP_ECOFF_INIT | STYP_ECOFF_FINI)

/* extended section types */
#define STYP_COMMENT	 0x2100000
#define STYP_RCONST	 0x2200000
#define STYP_XDATA	 0x2400000
#define STYP_PDATA	 0x2800000

/* The linker needs a section to hold small common variables while
   linking.  There is no convenient way to create it when the linker
   needs it, so we always create one for each BFD.  We then avoid
   writing it out.  */
#define SCOMMON ".scommon"

/* If the extern bit in a reloc is 1, then r_symndx is an index into
   the external symbol table.  If the extern bit is 0, then r_symndx
   indicates a section, and is one of the following values.  */
#define RELOC_SECTION_NONE	0
#define RELOC_SECTION_TEXT	1
#define RELOC_SECTION_RDATA	2
#define RELOC_SECTION_DATA	3
#define RELOC_SECTION_SDATA	4
#define RELOC_SECTION_SBSS	5
#define RELOC_SECTION_BSS	6
#define RELOC_SECTION_INIT	7
#define RELOC_SECTION_LIT8	8
#define RELOC_SECTION_LIT4	9
#define RELOC_SECTION_XDATA    10
#define RELOC_SECTION_PDATA    11
#define RELOC_SECTION_FINI     12
#define RELOC_SECTION_LITA     13
#define RELOC_SECTION_ABS      14
#define RELOC_SECTION_RCONST   15

#define NUM_RELOC_SECTIONS     16

/********************** STABS **********************/

/* gcc uses mips-tfile to output type information in special stabs
   entries.  These must match the corresponding definition in
   gcc/config/mips.h.  At some point, these should probably go into a
   shared include file, but currently gcc and gdb do not share any
   directories. */
#define CODE_MASK 0x8F300
#define ECOFF_IS_STAB(sym) (((sym)->index & 0xFFF00) == CODE_MASK)
#define ECOFF_MARK_STAB(code) ((code)+CODE_MASK)
#define ECOFF_UNMARK_STAB(code) ((code)-CODE_MASK)
#define STABS_SYMBOL "@stabs"

/********************** COFF **********************/

/* gcc also uses mips-tfile to output COFF debugging information.
   These are the values it uses when outputting the .type directive.
   These should also be in a shared include file.  */
#define N_BTMASK	(017)
#define N_TMASK		(060)
#define N_BTSHFT	(4)
#define N_TSHIFT	(2)

/********************** AUX **********************/

/* The auxiliary type information is the same on all known ECOFF
   targets.  I can't see any reason that it would ever change, so I am
   going to gamble and define the external structures here, in the
   target independent ECOFF header file.  The internal forms are
   defined in coff/sym.h, which was originally donated by MIPS
   Computer Systems.  */

/* Type information external record */

struct tir_ext {
	unsigned char	t_bits1[1];
	unsigned char	t_tq45[1];
	unsigned char	t_tq01[1];
	unsigned char	t_tq23[1];
};

#define	TIR_BITS1_FBITFIELD_BIG		((unsigned int) 0x80)
#define	TIR_BITS1_FBITFIELD_LITTLE	((unsigned int) 0x01)

#define	TIR_BITS1_CONTINUED_BIG		((unsigned int) 0x40)
#define	TIR_BITS1_CONTINUED_LITTLE	((unsigned int) 0x02)

#define	TIR_BITS1_BT_BIG		((unsigned int) 0x3F)
#define	TIR_BITS1_BT_SH_BIG		0
#define	TIR_BITS1_BT_LITTLE		((unsigned int) 0xFC)
#define	TIR_BITS1_BT_SH_LITTLE		2

#define	TIR_BITS_TQ4_BIG		((unsigned int) 0xF0)
#define	TIR_BITS_TQ4_SH_BIG		4
#define	TIR_BITS_TQ5_BIG		((unsigned int) 0x0F)
#define	TIR_BITS_TQ5_SH_BIG		0
#define	TIR_BITS_TQ4_LITTLE		((unsigned int) 0x0F)
#define	TIR_BITS_TQ4_SH_LITTLE		0
#define	TIR_BITS_TQ5_LITTLE		((unsigned int) 0xF0)
#define	TIR_BITS_TQ5_SH_LITTLE		4

#define	TIR_BITS_TQ0_BIG		((unsigned int) 0xF0)
#define	TIR_BITS_TQ0_SH_BIG		4
#define	TIR_BITS_TQ1_BIG		((unsigned int) 0x0F)
#define	TIR_BITS_TQ1_SH_BIG		0
#define	TIR_BITS_TQ0_LITTLE		((unsigned int) 0x0F)
#define	TIR_BITS_TQ0_SH_LITTLE		0
#define	TIR_BITS_TQ1_LITTLE		((unsigned int) 0xF0)
#define	TIR_BITS_TQ1_SH_LITTLE		4

#define	TIR_BITS_TQ2_BIG		((unsigned int) 0xF0)
#define	TIR_BITS_TQ2_SH_BIG		4
#define	TIR_BITS_TQ3_BIG		((unsigned int) 0x0F)
#define	TIR_BITS_TQ3_SH_BIG		0
#define	TIR_BITS_TQ2_LITTLE		((unsigned int) 0x0F)
#define	TIR_BITS_TQ2_SH_LITTLE		0
#define	TIR_BITS_TQ3_LITTLE		((unsigned int) 0xF0)
#define	TIR_BITS_TQ3_SH_LITTLE		4

/* Relative symbol external record */

struct rndx_ext {
	unsigned char	r_bits[4];
};

#define	RNDX_BITS0_RFD_SH_LEFT_BIG	4
#define	RNDX_BITS1_RFD_BIG		((unsigned int) 0xF0)
#define	RNDX_BITS1_RFD_SH_BIG		4

#define	RNDX_BITS0_RFD_SH_LEFT_LITTLE	0
#define	RNDX_BITS1_RFD_LITTLE		((unsigned int) 0x0F)
#define	RNDX_BITS1_RFD_SH_LEFT_LITTLE	8

#define	RNDX_BITS1_INDEX_BIG		((unsigned int) 0x0F)
#define	RNDX_BITS1_INDEX_SH_LEFT_BIG	16
#define	RNDX_BITS2_INDEX_SH_LEFT_BIG	8
#define	RNDX_BITS3_INDEX_SH_LEFT_BIG	0

#define	RNDX_BITS1_INDEX_LITTLE		((unsigned int) 0xF0)
#define	RNDX_BITS1_INDEX_SH_LITTLE	4
#define	RNDX_BITS2_INDEX_SH_LEFT_LITTLE	4
#define	RNDX_BITS3_INDEX_SH_LEFT_LITTLE	12

/* Auxiliary symbol information external record */

union aux_ext {
	struct tir_ext	a_ti;
	struct rndx_ext	a_rndx;
	unsigned char	a_dnLow[4];
	unsigned char	a_dnHigh[4];
	unsigned char	a_isym[4];
	unsigned char	a_iss[4];
	unsigned char	a_width[4];
	unsigned char	a_count[4];
};

#define AUX_GET_ANY(bigend, ax, field) \
  ((bigend) ? bfd_getb32 ((ax)->field) : bfd_getl32 ((ax)->field))

#define	AUX_GET_DNLOW(bigend, ax)	AUX_GET_ANY ((bigend), (ax), a_dnLow)
#define	AUX_GET_DNHIGH(bigend, ax)	AUX_GET_ANY ((bigend), (ax), a_dnHigh)
#define	AUX_GET_ISYM(bigend, ax)	AUX_GET_ANY ((bigend), (ax), a_isym)
#define AUX_GET_ISS(bigend, ax)		AUX_GET_ANY ((bigend), (ax), a_iss)
#define AUX_GET_WIDTH(bigend, ax)	AUX_GET_ANY ((bigend), (ax), a_width)
#define AUX_GET_COUNT(bigend, ax)	AUX_GET_ANY ((bigend), (ax), a_count)

#define AUX_PUT_ANY(bigend, val, ax, field) \
  ((bigend) \
   ? (bfd_putb32 ((bfd_vma) (val), (ax)->field), 0) \
   : (bfd_putl32 ((bfd_vma) (val), (ax)->field), 0))

#define AUX_PUT_DNLOW(bigend, val, ax) \
  AUX_PUT_ANY ((bigend), (val), (ax), a_dnLow)
#define AUX_PUT_DNHIGH(bigend, val, ax) \
  AUX_PUT_ANY ((bigend), (val), (ax), a_dnHigh)
#define AUX_PUT_ISYM(bigend, val, ax) \
  AUX_PUT_ANY ((bigend), (val), (ax), a_isym)
#define AUX_PUT_ISS(bigend, val, ax) \
  AUX_PUT_ANY ((bigend), (val), (ax), a_iss)
#define AUX_PUT_WIDTH(bigend, val, ax) \
  AUX_PUT_ANY ((bigend), (val), (ax), a_width)
#define AUX_PUT_COUNT(bigend, val, ax) \
  AUX_PUT_ANY ((bigend), (val), (ax), a_count)

/********************** SYMBOLS **********************/

/* For efficiency, gdb deals directly with the unswapped symbolic
   information (that way it only takes the time to swap information
   that it really needs to read).  gdb originally retrieved the
   information directly from the BFD backend information, but that
   strategy, besides being sort of ugly, does not work for MIPS ELF,
   which also uses ECOFF debugging information.  This structure holds
   pointers to the (mostly) unswapped symbolic information.  */

struct ecoff_debug_info
{
  /* The swapped ECOFF symbolic header.  */
  HDRR symbolic_header;

  /* Pointers to the unswapped symbolic information.  Note that the
     pointers to external structures point to different sorts of
     information on different ECOFF targets.  The ecoff_debug_swap
     structure provides the sizes of the structures and the functions
     needed to swap the information in and out.  These pointers are
     all pointers to arrays, not single structures.  They will be NULL
     if there are no instances of the relevant structure.  These
     fields are also used by the assembler to output ECOFF debugging
     information.  */
  unsigned char *line;
  void *external_dnr;	/* struct dnr_ext */
  void *external_pdr;	/* struct pdr_ext */
  void *external_sym;	/* struct sym_ext */
  void *external_opt;	/* struct opt_ext */
  union aux_ext *external_aux;
  char *ss;
  char *ssext;
  void *external_fdr;	/* struct fdr_ext */
  void *external_rfd;	/* struct rfd_ext */
  void *external_ext;	/* struct ext_ext */

  /* These fields are used when linking.  They may disappear at some
     point.  */
  char *ssext_end;
  void *external_ext_end;

  /* When linking, this field holds a mapping from the input FDR
     numbers to the output numbers, and is used when writing out the
     external symbols.  It is NULL if no mapping is required.  */
  RFDT *ifdmap;

  /* The swapped FDR information.  Currently this is never NULL, but
     code using this structure should probably double-check in case
     this changes in the future.  This is a pointer to an array, not a
     single structure.  */
  FDR *fdr;
};

/* These structures are used by the ECOFF find_nearest_line function.  */

struct ecoff_fdrtab_entry
{
  /* Base address in .text of this FDR.  */
  bfd_vma base_addr;
  FDR *fdr;
};

struct ecoff_find_line
{
  /* Allocated memory to hold function and file names.  */
  char *find_buffer;

  /* FDR table, sorted by address: */
  long fdrtab_len;
  struct ecoff_fdrtab_entry *fdrtab;

  /* Cache entry for most recently found line information.  The sect
     field is NULL if this cache does not contain valid information.  */
  struct
    {
      asection *sect;
      bfd_vma start;
      bfd_vma stop;
      const char *filename;
      const char *functionname;
      unsigned int line_num;
    } cache;
};

/********************** SWAPPING **********************/

/* The generic ECOFF code needs to be able to swap debugging
   information in and out in the specific format used by a particular
   ECOFF implementation.  This structure provides the information
   needed to do this.  */

struct ecoff_debug_swap
{
  /* Symbol table magic number.  */
  int sym_magic;
  /* Alignment of debugging information.  E.g., 4.  */
  bfd_size_type debug_align;
  /* Sizes of external symbolic information.  */
  bfd_size_type external_hdr_size;
  bfd_size_type external_dnr_size;
  bfd_size_type external_pdr_size;
  bfd_size_type external_sym_size;
  bfd_size_type external_opt_size;
  bfd_size_type external_fdr_size;
  bfd_size_type external_rfd_size;
  bfd_size_type external_ext_size;
  /* Functions to swap in external symbolic data.  */
  void (*swap_hdr_in) (bfd *, void *, HDRR *);
  void (*swap_dnr_in) (bfd *, void *, DNR *);
  void (*swap_pdr_in) (bfd *, void *, PDR *);
  void (*swap_sym_in) (bfd *, void *, SYMR *);
  void (*swap_opt_in) (bfd *, void *, OPTR *);
  void (*swap_fdr_in) (bfd *, void *, FDR *);
  void (*swap_rfd_in) (bfd *, void *, RFDT *);
  void (*swap_ext_in) (bfd *, void *, EXTR *);
  void (*swap_tir_in) (int, const struct tir_ext *, TIR *);
  void (*swap_rndx_in) (int, const struct rndx_ext *, RNDXR *);
  /* Functions to swap out external symbolic data.  */
  void (*swap_hdr_out) (bfd *, const HDRR *, void *);
  void (*swap_dnr_out) (bfd *, const DNR *, void *);
  void (*swap_pdr_out) (bfd *, const PDR *, void *);
  void (*swap_sym_out) (bfd *, const SYMR *, void *);
  void (*swap_opt_out) (bfd *, const OPTR *, void *);
  void (*swap_fdr_out) (bfd *, const FDR *, void *);
  void (*swap_rfd_out) (bfd *, const RFDT *, void *);
  void (*swap_ext_out) (bfd *, const EXTR *, void *);
  void (*swap_tir_out) (int, const TIR *, struct tir_ext *);
  void (*swap_rndx_out) (int, const RNDXR *, struct rndx_ext *);
  /* Function to read symbol data and set up pointers in
     ecoff_debug_info structure.  The section argument is used for
     ELF, not straight ECOFF.  */
  bfd_boolean (*read_debug_info) (bfd *, asection *, struct ecoff_debug_info *);
};

#endif /* ! defined (ECOFF_H) */
