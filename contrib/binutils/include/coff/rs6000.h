/* IBM RS/6000 "XCOFF" file definitions for BFD.
   Copyright (C) 1990, 1991, 2001 Free Software Foundation, Inc.
   Written by Mimi Phuong-Thao Vo of IBM
   and John Gilmore of Cygnus Support.  */

/********************** FILE HEADER **********************/

struct external_filehdr {
	char f_magic[2];	/* magic number			*/
	char f_nscns[2];	/* number of sections		*/
	char f_timdat[4];	/* time & date stamp		*/
	char f_symptr[4];	/* file pointer to symtab	*/
	char f_nsyms[4];	/* number of symtab entries	*/
	char f_opthdr[2];	/* sizeof(optional hdr)		*/
	char f_flags[2];	/* flags			*/
};

        /* IBM RS/6000 */
#define U802WRMAGIC     0730    /* writeable text segments **chh**      */
#define U802ROMAGIC     0735    /* readonly sharable text segments      */
#define U802TOCMAGIC    0737    /* readonly text segments and TOC       */

#define BADMAG(x)	\
	((x).f_magic != U802ROMAGIC && (x).f_magic != U802WRMAGIC && \
	 (x).f_magic != U802TOCMAGIC)

#define	FILHDR	struct external_filehdr
#define	FILHSZ	20


/********************** AOUT "OPTIONAL HEADER" **********************/


typedef struct 
{
  unsigned char	magic[2];	/* type of file			*/
  unsigned char	vstamp[2];	/* version stamp		*/
  unsigned char	tsize[4];	/* text size in bytes, padded to FW bdry */
  unsigned char	dsize[4];	/* initialized data "  "	*/
  unsigned char	bsize[4];	/* uninitialized data "   "	*/
  unsigned char	entry[4];	/* entry pt.			*/
  unsigned char	text_start[4];	/* base of text used for this file */
  unsigned char	data_start[4];	/* base of data used for this file */
  unsigned char	o_toc[4];	/* address of TOC */
  unsigned char	o_snentry[2];	/* section number of entry point */
  unsigned char	o_sntext[2];	/* section number of .text section */
  unsigned char	o_sndata[2];	/* section number of .data section */
  unsigned char	o_sntoc[2];	/* section number of TOC */
  unsigned char	o_snloader[2];	/* section number of .loader section */
  unsigned char	o_snbss[2];	/* section number of .bss section */
  unsigned char	o_algntext[2];	/* .text alignment */
  unsigned char	o_algndata[2];	/* .data alignment */
  unsigned char	o_modtype[2];	/* module type (??) */
  unsigned char o_cputype[2];	/* cpu type */
  unsigned char	o_maxstack[4];	/* max stack size (??) */
  unsigned char o_maxdata[4];	/* max data size (??) */
  unsigned char	o_resv2[12];	/* reserved */
}
AOUTHDR;

#define AOUTSZ 72
#define SMALL_AOUTSZ (28)
#define AOUTHDRSZ 72

/********************** SECTION HEADER **********************/


struct external_scnhdr {
	char		s_name[8];	/* section name			*/
	char		s_paddr[4];	/* physical address, aliased s_nlib */
	char		s_vaddr[4];	/* virtual address		*/
	char		s_size[4];	/* section size			*/
	char		s_scnptr[4];	/* file ptr to raw data for section */
	char		s_relptr[4];	/* file ptr to relocation	*/
	char		s_lnnoptr[4];	/* file ptr to line numbers	*/
	char		s_nreloc[2];	/* number of relocation entries	*/
	char		s_nlnno[2];	/* number of line number entries*/
	char		s_flags[4];	/* flags			*/
};

#define	SCNHDR	struct external_scnhdr
#define	SCNHSZ	40

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

	struct {
		unsigned char x_scnlen[4];
		unsigned char x_parmhash[4];
		unsigned char x_snhash[2];
		unsigned char x_smtyp[1];
		unsigned char x_smclas[1];
		unsigned char x_stab[4];
		unsigned char x_snstab[2];
	} x_csect;

};

#define	SYMENT	struct external_syment
#define	SYMESZ	18	
#define	AUXENT	union external_auxent
#define	AUXESZ	18
#define DBXMASK 0x80		/* for dbx storage mask */
#define SYMNAME_IN_DEBUG(symptr) ((symptr)->n_sclass & DBXMASK)



/********************** RELOCATION DIRECTIVES **********************/


struct external_reloc {
  char r_vaddr[4];
  char r_symndx[4];
  char r_size[1];
  char r_type[1];
};


#define RELOC struct external_reloc
#define RELSZ 10

#define DEFAULT_DATA_SECTION_ALIGNMENT 4
#define DEFAULT_BSS_SECTION_ALIGNMENT 4
#define DEFAULT_TEXT_SECTION_ALIGNMENT 4
/* For new sections we havn't heard of before */
#define DEFAULT_SECTION_ALIGNMENT 4

/* The ldhdr structure.  This appears at the start of the .loader
   section.  */

struct external_ldhdr
{
  bfd_byte l_version[4];
  bfd_byte l_nsyms[4];
  bfd_byte l_nreloc[4];
  bfd_byte l_istlen[4];
  bfd_byte l_nimpid[4];
  bfd_byte l_impoff[4];
  bfd_byte l_stlen[4];
  bfd_byte l_stoff[4];
};

#define LDHDRSZ (8 * 4)

struct external_ldsym
{
  union
    {
      bfd_byte _l_name[SYMNMLEN];
      struct
	{
	  bfd_byte _l_zeroes[4];
	  bfd_byte _l_offset[4];
	} _l_l;
    } _l;
  bfd_byte l_value[4];
  bfd_byte l_scnum[2];
  bfd_byte l_smtype[1];
  bfd_byte l_smclas[1];
  bfd_byte l_ifile[4];
  bfd_byte l_parm[4];
};

#define LDSYMSZ (8 + 3 * 4 + 2 + 2)

struct external_ldrel
{
  bfd_byte l_vaddr[4];
  bfd_byte l_symndx[4];
  bfd_byte l_rtype[2];
  bfd_byte l_rsecnm[2];
};

#define LDRELSZ (2 * 4 + 2 * 2)
