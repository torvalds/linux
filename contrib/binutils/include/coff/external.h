/* external.h  -- External COFF structures
   
   Copyright 2001, 2006 Free Software Foundation, Inc.

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

#ifndef COFF_EXTERNAL_H
#define COFF_EXTERNAL_H

#ifndef DO_NOT_DEFINE_FILHDR
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
  };

#define	FILHDR	struct external_filehdr
#define	FILHSZ	20
#endif

#ifndef DO_NOT_DEFINE_AOUTHDR
/********************** AOUT "OPTIONAL HEADER" **********************/

typedef struct external_aouthdr
  {
    char magic[2];	/* type of file				*/
    char vstamp[2];	/* version stamp			*/
    char tsize[4];	/* text size in bytes, padded to FW bdry*/
    char dsize[4];	/* initialized data "  "		*/
    char bsize[4];	/* uninitialized data "   "		*/
    char entry[4];	/* entry pt.				*/
    char text_start[4];	/* base of text used for this file 	*/
    char data_start[4];	/* base of data used for this file 	*/
  } ATTRIBUTE_PACKED
AOUTHDR;

#define AOUTHDRSZ 28
#define AOUTSZ 28

typedef struct external_aouthdr64
{
  char magic[2];	/* Type of file.			*/
  char vstamp[2];	/* Version stamp.			*/
  char tsize[4];	/* Text size in bytes, padded to FW bdry*/
  char dsize[4];	/* Initialized data "  ".		*/
  char bsize[4];	/* Uninitialized data "   ".		*/
  char entry[4];	/* Entry pt.				*/
  char text_start[4];	/* Base of text used for this file. 	*/
}
AOUTHDR64;
#define AOUTHDRSZ64	24

#endif /* not DO_NOT_DEFINE_AOUTHDR */

#ifndef DO_NOT_DEFINE_SCNHDR
/********************** SECTION HEADER **********************/

struct external_scnhdr
  {
    char s_name[8];	/* section name				*/
    char s_paddr[4];	/* physical address, aliased s_nlib 	*/
    char s_vaddr[4];	/* virtual address			*/
    char s_size[4];	/* section size				*/
    char s_scnptr[4];	/* file ptr to raw data for section 	*/
    char s_relptr[4];	/* file ptr to relocation		*/
    char s_lnnoptr[4];	/* file ptr to line numbers		*/
    char s_nreloc[2];	/* number of relocation entries		*/
    char s_nlnno[2];	/* number of line number entries	*/
    char s_flags[4];	/* flags				*/
  };

#define	SCNHDR	struct external_scnhdr
#define	SCNHSZ	40

/* Names of "special" sections.  */

#define _TEXT	 ".text"
#define _DATA	 ".data"
#define _BSS	 ".bss"
#define _COMMENT ".comment"
#define _LIB     ".lib"
#endif /* not DO_NOT_DEFINE_SCNHDR */

#ifndef DO_NOT_DEFINE_LINENO

/********************** LINE NUMBERS **********************/

#ifndef L_LNNO_SIZE
#error  L_LNNO_SIZE needs to be defined
#endif

/* 1 line number entry for every "breakpointable" source line in a section.
   Line numbers are grouped on a per function basis; first entry in a function
   grouping will have l_lnno = 0 and in place of physical address will be the
   symbol table index of the function name.  */
struct external_lineno
{
  union
  {
    char l_symndx[4];	/* function name symbol index, iff l_lnno == 0*/
    char l_paddr[4];	/* (physical) address of line number	*/
  } l_addr;

  char l_lnno[L_LNNO_SIZE];	/* line number		*/
};

#define	LINENO	struct external_lineno
#define	LINESZ	(4 + L_LNNO_SIZE)

#if L_LNNO_SIZE == 4
#define GET_LINENO_LNNO(abfd, ext)      H_GET_32 (abfd,      (ext->l_lnno))
#define PUT_LINENO_LNNO(abfd, val, ext) H_PUT_32 (abfd, val, (ext->l_lnno))
#endif
#if L_LNNO_SIZE == 2
#define GET_LINENO_LNNO(abfd, ext)      H_GET_16 (abfd,      (ext->l_lnno))
#define PUT_LINENO_LNNO(abfd, val, ext) H_PUT_16 (abfd, val, (ext->l_lnno))
#endif

#endif /* not DO_NOT_DEFINE_LINENO */

#ifndef DO_NOT_DEFINE_SYMENT
/********************** SYMBOLS **********************/

#define E_SYMNMLEN	8	/* # characters in a symbol name	*/
#ifndef E_FILNMLEN
#define E_FILNMLEN	14
#endif
#define E_DIMNUM	4	/* # array dimensions in auxiliary entry */

struct external_syment 
{
  union
  {
    char e_name[E_SYMNMLEN];

    struct
    {
      char e_zeroes[4];
      char e_offset[4];
    } e;
  } e;

  char e_value[4];
  char e_scnum[2];
  char e_type[2];
  char e_sclass[1];
  char e_numaux[1];
} ATTRIBUTE_PACKED ;

#define	SYMENT	struct external_syment
#define	SYMESZ	18	

#ifndef N_BTMASK
#define N_BTMASK	0xf
#endif

#ifndef N_TMASK
#define N_TMASK		0x30
#endif

#ifndef N_BTSHFT
#define N_BTSHFT	4
#endif

#ifndef N_TSHIFT
#define N_TSHIFT	2
#endif

#endif /* not DO_NOT_DEFINE_SYMENT */

#ifndef DO_NOT_DEFINE_AUXENT

union external_auxent
{
  struct
  {
    char x_tagndx[4];		/* str, un, or enum tag indx */
    
    union
    {
      struct
      {
	char  x_lnno[2]; /* declaration line number */
	char  x_size[2]; /* str/union/array size */
      } x_lnsz;
      
      char x_fsize[4];	/* size of function */
      
    } x_misc;
    
    union
    {
      struct 		/* if ISFCN, tag, or .bb */
      {
	char x_lnnoptr[4];	/* ptr to fcn line # */
	char x_endndx[4];	/* entry ndx past block end */
      } x_fcn;
      
      struct 		/* if ISARY, up to 4 dimen. */
      {
	char x_dimen[E_DIMNUM][2];
      } x_ary;
      
    } x_fcnary;
    
    char x_tvndx[2];	/* tv index */
    
  } x_sym;
  
  union
  {
    char x_fname[E_FILNMLEN];
    
    struct
    {
      char x_zeroes[4];
      char x_offset[4];
    } x_n;
    
  } x_file;
  
  struct
  {
    char x_scnlen[4];	/* section length */
    char x_nreloc[2];	/* # relocation entries */
    char x_nlinno[2];	/* # line numbers */
#ifdef INCLUDE_COMDAT_FIELDS_IN_AUXENT
    char x_checksum[4];		   /* section COMDAT checksum	      */
    char x_associated[2];	   /* COMDAT associated section index */
    char x_comdat[1];		   /* COMDAT selection number	      */
#endif    
  } x_scn;
  
  struct
  {
    char x_tvfill[4];	/* tv fill value */
    char x_tvlen[2];	/* length of .tv */
    char x_tvran[2][2];	/* tv range */
  } x_tv;		/* info about .tv section (in auxent of symbol .tv)) */
} ATTRIBUTE_PACKED ;

#define	AUXENT	union external_auxent
#define	AUXESZ	18

#define _ETEXT	"etext"

#endif /* not DO_NOT_DEFINE_AUXENT */

#endif /* COFF_EXTERNAL_H */
