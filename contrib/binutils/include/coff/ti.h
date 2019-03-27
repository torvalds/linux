/* COFF information for TI COFF support.  Definitions in this file should be
   customized in a target-specific file, and then this file included (see
   tic54x.h for an example).
   
   Copyright 2000, 2001, 2002, 2003 Free Software Foundation, Inc.

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
#ifndef COFF_TI_H
#define COFF_TI_H

/* Note "coff/external.h is not used because TI adds extra fields to the structures.  */

/********************** FILE HEADER **********************/

struct external_filehdr
  {
    char f_magic[2];	/* magic number			*/
    char f_nscns[2];	/* number of sections		*/
    char f_timdat[4];	/* time & date stamp		*/
    char f_symptr[4];	/* file pointer to symtab	*/
    char f_nsyms[4];	/* number of symtab entries	*/
    char f_opthdr[2];	/* sizeof(optional hdr)		*/
    char f_flags[2];	/* flags			*/
    char f_target_id[2];    /* magic no. (TI COFF-specific) */
  };

/* COFF0 has magic number in f_magic, and omits f_target_id from the file
   header; for later versions, f_magic is 0xC1 for COFF1 and 0xC2 for COFF2
   and the target-specific magic number is found in f_target_id */ 

#define TICOFF0MAGIC    TI_TARGET_ID
#define TICOFF1MAGIC    0x00C1
#define TICOFF2MAGIC    0x00C2
#define TICOFF_AOUT_MAGIC    0x0108 /* magic number in optional header */
#define TICOFF          1 /* customize coffcode.h */

/* The target_id field changes depending on the particular CPU target */
/* for COFF0, the target id appeared in f_magic, where COFFX magic is now */
#ifndef TI_TARGET_ID
#error "TI_TARGET_ID needs to be defined for your CPU"
#endif

/* Which bfd_arch to use... */
#ifndef TICOFF_TARGET_ARCH
#error "TICOFF_TARGET_ARCH needs to be defined for your CPU"
#endif

#ifndef TICOFF_TARGET_MACHINE_GET
#define TICOFF_TARGET_MACHINE_GET(FLAGS) 0
#endif

#ifndef TICOFF_TARGET_MACHINE_SET
#define TICOFF_TARGET_MACHINE_SET(FLAGSP, MACHINE)
#endif

/* Default to COFF2 for file output */
#ifndef TICOFF_DEFAULT_MAGIC
#define TICOFF_DEFAULT_MAGIC TICOFF2MAGIC
#endif

/* This value is made available in the rare case where a bfd is unavailable */
#ifndef OCTETS_PER_BYTE_POWER
#error "OCTETS_PER_BYTE_POWER not defined for this CPU"
#else
#define OCTETS_PER_BYTE (1<<OCTETS_PER_BYTE_POWER)
#endif

/* default alignment is on a byte (not octet!) boundary */
#ifndef COFF_DEFAULT_SECTION_ALIGNMENT_POWER
#define COFF_DEFAULT_SECTION_ALIGNMENT_POWER 0
#endif

/* TI COFF encodes the section alignment in the section header flags */
#define COFF_ALIGN_IN_SECTION_HEADER 1
#define COFF_ALIGN_IN_S_FLAGS 1
/* requires a power-of-two argument */
#define COFF_ENCODE_ALIGNMENT(S,X) ((S).s_flags |= (((unsigned)(X)&0xF)<<8))
/* result is a power of two */
#define COFF_DECODE_ALIGNMENT(X) (((X)>>8)&0xF)

#define COFF0_P(ABFD) (bfd_coff_filhsz(ABFD) == FILHSZ_V0)
#define COFF2_P(ABFD) (bfd_coff_scnhsz(ABFD) != SCNHSZ_V01)

#define COFF0_BADMAG(x) ((x).f_magic != TICOFF0MAGIC)
#define COFF1_BADMAG(x) ((x).f_magic != TICOFF1MAGIC || (x).f_target_id != TI_TARGET_ID)
#define COFF2_BADMAG(x) ((x).f_magic != TICOFF2MAGIC || (x).f_target_id != TI_TARGET_ID)

/* we need to read/write an extra field in the coff file header */
#ifndef COFF_ADJUST_FILEHDR_IN_POST
#define COFF_ADJUST_FILEHDR_IN_POST(abfd, src, dst) \
  do									\
    {									\
      ((struct internal_filehdr *)(dst))->f_target_id =			\
	H_GET_16 (abfd, ((FILHDR *)(src))->f_target_id);		\
    }									\
  while (0)
#endif

#ifndef COFF_ADJUST_FILEHDR_OUT_POST
#define COFF_ADJUST_FILEHDR_OUT_POST(abfd, src, dst) \
  do									\
    {									\
      H_PUT_16 (abfd, ((struct internal_filehdr *)(src))->f_target_id,	\
	       ((FILHDR *)(dst))->f_target_id);				\
    }									\
  while (0)
#endif

#define	FILHDR	struct external_filehdr
#define	FILHSZ	22
#define FILHSZ_V0 20                /* COFF0 omits target_id field */

/* File header flags */
#define	F_RELFLG	(0x0001)
#define	F_EXEC		(0x0002)
#define	F_LNNO		(0x0004)
#define F_VERS          (0x0010) /* TMS320C4x code */
/* F_LSYMS needs to be redefined in your source file */
#define	F_LSYMS_TICOFF	(0x0010) /* normal COFF is 0x8 */

#define F_10            0x00    /* file built for TMS320C1x devices */
#define F_20            0x10    /* file built for TMS320C2x devices */
#define F_25            0x20    /* file built for TMS320C2x/C5x devices */
#define F_LENDIAN       0x0100  /* 16 bits/word, LSB first */
#define F_SYMMERGE      0x1000  /* duplicate symbols were removed */

/********************** OPTIONAL HEADER **********************/


typedef struct 
{
  char 	magic[2];		/* type of file (0x108) 		*/
  char	vstamp[2];		/* version stamp			*/
  char	tsize[4];		/* text size in bytes, padded to FW bdry*/
  char	dsize[4];		/* initialized data "  "		*/
  char	bsize[4];		/* uninitialized data "   "		*/
  char	entry[4];		/* entry pt.				*/
  char 	text_start[4];		/* base of text used for this file */
  char 	data_start[4];		/* base of data used for this file */
}
AOUTHDR;


#define AOUTHDRSZ 28
#define AOUTSZ 28


/********************** SECTION HEADER **********************/
/* COFF0, COFF1 */
struct external_scnhdr_v01 {
	char		s_name[8];	/* section name			*/
	char		s_paddr[4];	/* physical address, aliased s_nlib */
	char		s_vaddr[4];	/* virtual address		*/
	char		s_size[4];	/* section size (in WORDS)      */
	char		s_scnptr[4];	/* file ptr to raw data for section */
	char		s_relptr[4];	/* file ptr to relocation	*/
	char		s_lnnoptr[4];	/* file ptr to line numbers	*/
	char		s_nreloc[2];	/* number of relocation entries	*/
	char		s_nlnno[2];	/* number of line number entries*/
	char		s_flags[2];	/* flags			*/
        char            s_reserved[1];  /* reserved                     */ 
        char            s_page[1];      /* section page number (LOAD)   */
};

/* COFF2 */
struct external_scnhdr {
	char		s_name[8];	/* section name			*/
	char		s_paddr[4];	/* physical address, aliased s_nlib */
	char		s_vaddr[4];	/* virtual address		*/
	char		s_size[4];	/* section size (in WORDS)      */
	char		s_scnptr[4];	/* file ptr to raw data for section */
	char		s_relptr[4];	/* file ptr to relocation	*/
	char		s_lnnoptr[4];	/* file ptr to line numbers	*/
	char		s_nreloc[4];	/* number of relocation entries	*/
	char		s_nlnno[4];	/* number of line number entries*/
	char		s_flags[4];	/* flags			*/
        char            s_reserved[2];  /* reserved                     */ 
        char            s_page[2];      /* section page number (LOAD)   */
};

/*
 * Special section flags
 */

/* TI COFF defines these flags; 
   STYP_CLINK: the section should be excluded from the final
   linker output if there are no references found to any symbol in the section
   STYP_BLOCK: the section should be blocked, i.e. if the section would cross
   a page boundary, it is started at a page boundary instead.
   TI COFF puts the section alignment power of two in the section flags
   e.g. 2**N is alignment, flags |= (N & 0xF) << 8
*/ 
#define STYP_CLINK      (0x4000)
#define STYP_BLOCK      (0x1000)
#define STYP_ALIGN      (0x0F00) /* TI COFF stores section alignment here */

#define	SCNHDR_V01 struct external_scnhdr_v01
#define SCNHDR struct external_scnhdr
#define	SCNHSZ_V01 40                  /* for v0 and v1 */
#define SCNHSZ 48

/* COFF2 changes the offsets and sizes of these fields 
   Assume we're dealing with the COFF2 scnhdr structure, and adjust
   accordingly 
 */
#define GET_SCNHDR_NRELOC(ABFD, LOC) \
  (COFF2_P (ABFD) ? H_GET_32 (ABFD, LOC) : H_GET_16 (ABFD, LOC))
#define PUT_SCNHDR_NRELOC(ABFD, VAL, LOC) \
  (COFF2_P (ABFD) ? H_PUT_32 (ABFD, VAL, LOC) : H_PUT_16 (ABFD, VAL, LOC))
#define GET_SCNHDR_NLNNO(ABFD, LOC) \
  (COFF2_P (ABFD) ? H_GET_32 (ABFD, LOC) : H_GET_16 (ABFD, (LOC) - 2))
#define PUT_SCNHDR_NLNNO(ABFD, VAL, LOC) \
  (COFF2_P (ABFD) ? H_PUT_32 (ABFD, VAL, LOC) : H_PUT_16 (ABFD, VAL, (LOC) - 2))
#define GET_SCNHDR_FLAGS(ABFD, LOC) \
  (COFF2_P (ABFD) ? H_GET_32 (ABFD, LOC) : H_GET_16 (ABFD, (LOC) - 4))
#define PUT_SCNHDR_FLAGS(ABFD, VAL, LOC) \
  (COFF2_P (ABFD) ? H_PUT_32 (ABFD, VAL, LOC) : H_PUT_16 (ABFD, VAL, (LOC) - 4))
#define GET_SCNHDR_PAGE(ABFD, LOC) \
  (COFF2_P (ABFD) ? H_GET_16 (ABFD, LOC) : (unsigned) H_GET_8 (ABFD, (LOC) - 7))
/* on output, make sure that the "reserved" field is zero */
#define PUT_SCNHDR_PAGE(ABFD, VAL, LOC) \
  (COFF2_P (ABFD) \
   ? H_PUT_16 (ABFD, VAL, LOC) \
   : H_PUT_8 (ABFD, VAL, (LOC) - 7), H_PUT_8 (ABFD, 0, (LOC) - 8))

/* TI COFF stores section size as number of bytes (address units, not octets),
   so adjust to be number of octets, which is what BFD expects */ 
#define GET_SCNHDR_SIZE(ABFD, SZP) \
  (H_GET_32 (ABFD, SZP) * bfd_octets_per_byte (ABFD))
#define PUT_SCNHDR_SIZE(ABFD, SZ, SZP) \
  H_PUT_32 (ABFD, (SZ) / bfd_octets_per_byte (ABFD), SZP)

#define COFF_ADJUST_SCNHDR_IN_POST(ABFD, EXT, INT) \
  do									\
    {									\
      ((struct internal_scnhdr *)(INT))->s_page =			\
	GET_SCNHDR_PAGE (ABFD, ((SCNHDR *)(EXT))->s_page);		\
    }									\
   while (0)

/* The line number and reloc overflow checking in coff_swap_scnhdr_out in
   coffswap.h doesn't use PUT_X for s_nlnno and s_nreloc.
   Due to different sized v0/v1/v2 section headers, we have to re-write these
   fields.
 */
#define COFF_ADJUST_SCNHDR_OUT_POST(ABFD, INT, EXT) \
  do									   \
    {									   \
      PUT_SCNHDR_NLNNO (ABFD, ((struct internal_scnhdr *)(INT))->s_nlnno,  \
			((SCNHDR *)(EXT))->s_nlnno);			   \
      PUT_SCNHDR_NRELOC (ABFD, ((struct internal_scnhdr *)(INT))->s_nreloc,\
			 ((SCNHDR *)(EXT))->s_nreloc);			   \
      PUT_SCNHDR_FLAGS (ABFD, ((struct internal_scnhdr *)(INT))->s_flags,  \
			((SCNHDR *)(EXT))->s_flags);			   \
      PUT_SCNHDR_PAGE (ABFD, ((struct internal_scnhdr *)(INT))->s_page,    \
		       ((SCNHDR *)(EXT))->s_page);			   \
    }									   \
   while (0)

/*
 * names of "special" sections
 */
#define _TEXT	".text"
#define _DATA	".data"
#define _BSS	".bss"
#define _CINIT  ".cinit"            /* initialized C data */
#define _SCONST  ".const"           /* constants */
#define _SWITCH ".switch"           /* switch tables */
#define _STACK  ".stack"            /* C stack */
#define _SYSMEM ".sysmem"           /* used for malloc et al. syscalls */

/********************** LINE NUMBERS **********************/

/* 1 line number entry for every "breakpointable" source line in a section.
 * Line numbers are grouped on a per function basis; first entry in a function
 * grouping will have l_lnno = 0 and in place of physical address will be the
 * symbol table index of the function name.
 */
struct external_lineno {
  union {
    char l_symndx[4];	/* function name symbol index, iff l_lnno == 0*/
    char l_paddr[4];	/* (physical) address of line number	*/
  } l_addr;
  char l_lnno[2];	/* line number		*/
};

#define	LINENO	struct external_lineno
#define	LINESZ	6


/********************** SYMBOLS **********************/

/* NOTE: this is what a local label looks like in assembly source; what it
   looks like in COFF output is undefined */
#define TICOFF_LOCAL_LABEL_P(NAME) \
((NAME[0] == '$' && NAME[1] >= '0' && NAME[1] <= '9' && NAME[2] == '\0') \
 || NAME[strlen(NAME)-1] == '?')

#define E_SYMNMLEN	8	/* # characters in a symbol name	*/
#define E_FILNMLEN	14	/* # characters in a file name		*/
#define E_DIMNUM	4	/* # array dimensions in auxiliary entry */

struct external_syment 
{
  union {
    char e_name[E_SYMNMLEN];
    struct {
      char e_zeroes[4];
      char e_offset[4];
    } e;
  } e;
  char e_value[4];
  char e_scnum[2];
  char e_type[2];
  char e_sclass[1];
  char e_numaux[1];
};


#define N_BTMASK	(017)
#define N_TMASK		(060)
#define N_BTSHFT	(4)
#define N_TSHIFT	(2)
  

union external_auxent {
  struct {
	char x_tagndx[4];	/* str, un, or enum tag indx */
	union {
	  struct {
		char  x_lnno[2]; /* declaration line number */
		char  x_size[2]; /* str/union/array size */
	  } x_lnsz;
	  char x_fsize[4];	/* size of function */
	} x_misc;
	union {
	  struct {		/* if ISFCN, tag, or .bb */
		char x_lnnoptr[4];	/* ptr to fcn line # */
		char x_endndx[4];	/* entry ndx past block end */
	  } x_fcn;
	  struct {		/* if ISARY, up to 4 dimen. */
		char x_dimen[E_DIMNUM][2];
	  } x_ary;
	} x_fcnary;
	char x_tvndx[2];		/* tv index */
  } x_sym;
  
  union {
	char x_fname[E_FILNMLEN];
	struct {
	  char x_zeroes[4];
	  char x_offset[4];
	} x_n;
  } x_file;
  
  struct {
	char x_scnlen[4];			/* section length */
	char x_nreloc[2];	/* # relocation entries */
	char x_nlinno[2];	/* # line numbers */
  } x_scn;
  
  struct {
	char x_tvfill[4];	/* tv fill value */
	char x_tvlen[2];	/* length of .tv */
	char x_tvran[2][2];	/* tv range */
  } x_tv;		/* info about .tv section (in auxent of symbol .tv)) */
  

};

#define	SYMENT	struct external_syment
#define	SYMESZ	18	
#define	AUXENT	union external_auxent
#define	AUXESZ	18

/* section lengths are in target bytes (not host bytes) */
#define GET_SCN_SCNLEN(ABFD, EXT) \
  (H_GET_32 (ABFD, (EXT)->x_scn.x_scnlen) * bfd_octets_per_byte (ABFD))
#define PUT_SCN_SCNLEN(ABFD, INT, EXT) \
  H_PUT_32 (ABFD, (INT) / bfd_octets_per_byte (ABFD), (EXT)->x_scn.x_scnlen)

/* lnsz size is in bits in COFF file, in bytes in BFD */
#define GET_LNSZ_SIZE(abfd, ext) \
 (H_GET_16 (abfd, ext->x_sym.x_misc.x_lnsz.x_size) / (class != C_FIELD ? 8 : 1))

#define PUT_LNSZ_SIZE(abfd, in, ext) \
  H_PUT_16 (abfd, ((class != C_FIELD) ? (in) * 8 : (in)), \
	   ext->x_sym.x_misc.x_lnsz.x_size)
 
/* TI COFF stores offsets for MOS and MOU in bits; BFD expects bytes 
   Also put the load page flag of the section into the symbol value if it's an
   address.  */
#ifndef NEEDS_PAGE
#define NEEDS_PAGE(X) 0
#define PAGE_MASK 0
#endif
#define COFF_ADJUST_SYM_IN_POST(ABFD, EXT, INT) \
  do									\
    {									\
      struct internal_syment *dst = (struct internal_syment *)(INT);	\
      if (dst->n_sclass == C_MOS || dst->n_sclass == C_MOU)		\
	dst->n_value /= 8;						\
      else if (NEEDS_PAGE (dst->n_sclass)) {                            \
        asection *scn = coff_section_from_bfd_index (abfd, dst->n_scnum); \
        dst->n_value |= (scn->lma & PAGE_MASK);                         \
      }									\
    }									\
   while (0)

#define COFF_ADJUST_SYM_OUT_POST(ABFD, INT, EXT) \
  do									\
    {									\
       struct internal_syment *src = (struct internal_syment *)(INT);	\
       SYMENT *dst = (SYMENT *)(EXT);					\
       if (src->n_sclass == C_MOU || src->n_sclass == C_MOS)		\
	 H_PUT_32 (abfd, src->n_value * 8, dst->e_value);		\
       else if (NEEDS_PAGE (src->n_sclass)) {                           \
         H_PUT_32 (abfd, src->n_value &= ~PAGE_MASK, dst->e_value);     \
       }								\
    }									\
   while (0)

/* Detect section-relative absolute symbols so they get flagged with a sym
   index of -1.
*/
#define SECTION_RELATIVE_ABSOLUTE_SYMBOL_P(RELOC, SECT) \
  ((*(RELOC)->sym_ptr_ptr)->section->output_section == (SECT) \
   && (RELOC)->howto->name[0] == 'A')

/********************** RELOCATION DIRECTIVES **********************/

struct external_reloc_v0
{
  char r_vaddr[4];
  char r_symndx[2];
  char r_reserved[2];
  char r_type[2];
};

struct external_reloc
{
  char r_vaddr[4];
  char r_symndx[4];
  char r_reserved[2]; /* extended pmad byte for COFF2 */
  char r_type[2];
};

#define RELOC struct external_reloc
#define RELSZ_V0 10                 /* FIXME -- coffcode.h needs fixing */
#define RELSZ 12                    /* for COFF1/2 */

/* various relocation types.  */
#define R_ABS     0x0000            /* no relocation */
#define R_REL13   0x002A            /* 13-bit direct reference (???) */
#define R_PARTLS7 0x0028            /* 7 LSBs of an address */
#define R_PARTMS9 0x0029            /* 9MSBs of an address */
#define R_EXTWORD 0x002B            /* 23-bit direct reference */
#define R_EXTWORD16 0x002C          /* 16-bit direct reference to 23-bit addr*/
#define R_EXTWORDMS7 0x002D         /* upper 7 bits of 23-bit address */

#endif /* COFF_TI_H */
