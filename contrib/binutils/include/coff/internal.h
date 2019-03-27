/* Internal format of COFF object file data structures, for GNU BFD.
   This file is part of BFD, the Binary File Descriptor library.
   
   Copyright 1999, 2000, 2001, 2002, 2003, 2004. 2005, 2006, 2007, 2009
   Free Software Foundation, Inc.

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

#ifndef GNU_COFF_INTERNAL_H
#define GNU_COFF_INTERNAL_H 1

/* First, make "signed char" work, even on old compilers. */
#ifndef signed
#ifndef __STDC__
#define	signed			/**/
#endif
#endif

/********************** FILE HEADER **********************/

/* extra stuff in a PE header. */

struct internal_extra_pe_filehdr
{
  /* DOS header data follows for PE stuff */
  unsigned short e_magic;	/* Magic number, 0x5a4d */
  unsigned short e_cblp;	/* Bytes on last page of file, 0x90 */
  unsigned short e_cp;		/* Pages in file, 0x3 */
  unsigned short e_crlc;	/* Relocations, 0x0 */
  unsigned short e_cparhdr;	/* Size of header in paragraphs, 0x4 */
  unsigned short e_minalloc;	/* Minimum extra paragraphs needed, 0x0 */
  unsigned short e_maxalloc;	/* Maximum extra paragraphs needed, 0xFFFF */
  unsigned short e_ss;		/* Initial (relative) SS value, 0x0 */
  unsigned short e_sp;		/* Initial SP value, 0xb8 */
  unsigned short e_csum;	/* Checksum, 0x0 */
  unsigned short e_ip;		/* Initial IP value, 0x0 */
  unsigned short e_cs;		/* Initial (relative) CS value, 0x0 */
  unsigned short e_lfarlc;	/* File address of relocation table, 0x40 */
  unsigned short e_ovno;	/* Overlay number, 0x0 */
  unsigned short e_res[4];	/* Reserved words, all 0x0 */
  unsigned short e_oemid;	/* OEM identifier (for e_oeminfo), 0x0 */
  unsigned short e_oeminfo;	/* OEM information; e_oemid specific, 0x0 */
  unsigned short e_res2[10];	/* Reserved words, all 0x0 */
  bfd_vma  e_lfanew;		/* File address of new exe header, 0x80 */
  unsigned long dos_message[16]; /* text which always follows dos header */
  bfd_vma  nt_signature;   	/* required NT signature, 0x4550 */ 
};

#define GO32_STUBSIZE 2048

struct internal_filehdr
{
  struct internal_extra_pe_filehdr pe;

  /* coff-stgo32 EXE stub header before BFD tdata has been allocated.
     Its data is kept in INTERNAL_FILEHDR.GO32STUB afterwards.
     
     F_GO32STUB is set iff go32stub contains a valid data.  Artifical headers
     created in BFD have no pre-set go32stub.  */
  char go32stub[GO32_STUBSIZE];

  /* Standard coff internal info.  */
  unsigned short f_magic;	/* magic number			*/
  unsigned short f_nscns;	/* number of sections		*/
  long f_timdat;		/* time & date stamp		*/
  bfd_vma f_symptr;		/* file pointer to symtab	*/
  long f_nsyms;			/* number of symtab entries	*/
  unsigned short f_opthdr;	/* sizeof(optional hdr)		*/
  unsigned short f_flags;	/* flags			*/
  unsigned short f_target_id;	/* (TI COFF specific)		*/
};


/* Bits for f_flags:
 	F_RELFLG	relocation info stripped from file
 	F_EXEC		file is executable (no unresolved external references)
 	F_LNNO		line numbers stripped from file
 	F_LSYMS		local symbols stripped from file
 	F_AR16WR	file is 16-bit little-endian
 	F_AR32WR	file is 32-bit little-endian
 	F_AR32W		file is 32-bit big-endian
 	F_DYNLOAD	rs/6000 aix: dynamically loadable w/imports & exports
 	F_SHROBJ	rs/6000 aix: file is a shared object
	F_DLL           PE format DLL
	F_GO32STUB      Field go32stub contains valid data.  */

#define	F_RELFLG	(0x0001)
#define	F_EXEC		(0x0002)
#define	F_LNNO		(0x0004)
#define	F_LSYMS		(0x0008)
#define	F_AR16WR	(0x0080)
#define	F_AR32WR	(0x0100)
#define	F_AR32W     	(0x0200)
#define	F_DYNLOAD	(0x1000)
#define	F_SHROBJ	(0x2000)
#define F_DLL           (0x2000)
#define F_GO32STUB      (0x4000)

/* Extra structure which is used in the optional header.  */
typedef struct _IMAGE_DATA_DIRECTORY 
{
  bfd_vma VirtualAddress;
  long    Size;
}  IMAGE_DATA_DIRECTORY;
#define PE_EXPORT_TABLE			0
#define PE_IMPORT_TABLE			1
#define PE_RESOURCE_TABLE		2
#define PE_EXCEPTION_TABLE		3
#define PE_CERTIFICATE_TABLE		4
#define PE_BASE_RELOCATION_TABLE	5
#define PE_DEBUG_DATA			6
#define PE_ARCHITECTURE			7
#define PE_GLOBAL_PTR			8
#define PE_TLS_TABLE			9
#define PE_LOAD_CONFIG_TABLE		10
#define PE_BOUND_IMPORT_TABLE		11
#define PE_IMPORT_ADDRESS_TABLE		12
#define PE_DELAY_IMPORT_DESCRIPTOR	13
#define PE_CLR_RUNTIME_HEADER		14
/* DataDirectory[15] is currently reserved, so no define. */
#define IMAGE_NUMBEROF_DIRECTORY_ENTRIES  16

/* Default image base for NT.  */
#define NT_EXE_IMAGE_BASE 0x400000
#define NT_DLL_IMAGE_BASE 0x10000000

/* Default image base for BeOS. */
#define BEOS_EXE_IMAGE_BASE 0x80000000
#define BEOS_DLL_IMAGE_BASE 0x10000000

/* Extra stuff in a PE aouthdr */

#define PE_DEF_SECTION_ALIGNMENT 0x1000
#ifndef PE_DEF_FILE_ALIGNMENT
# define PE_DEF_FILE_ALIGNMENT 0x200
#endif

struct internal_extra_pe_aouthdr 
{
  /* FIXME: The following entries are in AOUTHDR.  But they aren't
     available internally in bfd.  We add them here so that objdump
     can dump them.  */
  /* The state of the image file  */
  short Magic;
  /* Linker major version number */
  char MajorLinkerVersion;
  /* Linker minor version number  */
  char MinorLinkerVersion;	
  /* Total size of all code sections  */
  long SizeOfCode;
  /* Total size of all initialized data sections  */
  long SizeOfInitializedData;
  /* Total size of all uninitialized data sections  */
  long SizeOfUninitializedData;
  /* Address of entry point relative to image base.  */
  bfd_vma AddressOfEntryPoint;
  /* Address of the first code section relative to image base.  */
  bfd_vma BaseOfCode;
  /* Address of the first data section relative to image base.  */
  bfd_vma BaseOfData;
 
  /* PE stuff  */
  bfd_vma ImageBase;		/* address of specific location in memory that
				   file is located, NT default 0x10000 */

  bfd_vma SectionAlignment;	/* section alignment default 0x1000 */
  bfd_vma FileAlignment;	/* file alignment default 0x200 */
  short   MajorOperatingSystemVersion; /* minimum version of the operating */
  short   MinorOperatingSystemVersion; /* system req'd for exe, default to 1*/
  short   MajorImageVersion;	/* user defineable field to store version of */
  short   MinorImageVersion;	/* exe or dll being created, default to 0 */ 
  short   MajorSubsystemVersion; /* minimum subsystem version required to */
  short   MinorSubsystemVersion; /* run exe; default to 3.1 */
  long    Reserved1;		/* seems to be 0 */
  long    SizeOfImage;		/* size of memory to allocate for prog */
  long    SizeOfHeaders;	/* size of PE header and section table */
  long    CheckSum;		/* set to 0 */
  short   Subsystem;	

  /* type of subsystem exe uses for user interface,
     possible values:
     1 - NATIVE   Doesn't require a subsystem
     2 - WINDOWS_GUI runs in Windows GUI subsystem
     3 - WINDOWS_CUI runs in Windows char sub. (console app)
     5 - OS2_CUI runs in OS/2 character subsystem
     7 - POSIX_CUI runs in Posix character subsystem */
  unsigned short DllCharacteristics; /* flags for DLL init  */
  bfd_vma SizeOfStackReserve;	/* amount of memory to reserve  */
  bfd_vma SizeOfStackCommit;	/* amount of memory initially committed for 
				   initial thread's stack, default is 0x1000 */
  bfd_vma SizeOfHeapReserve;	/* amount of virtual memory to reserve and */
  bfd_vma SizeOfHeapCommit;	/* commit, don't know what to defaut it to */
  long    LoaderFlags;		/* can probably set to 0 */
  long    NumberOfRvaAndSizes;	/* number of entries in next entry, 16 */
  IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
};

/********************** AOUT "OPTIONAL HEADER" **********************/
struct internal_aouthdr
{
  short magic;			/* type of file				*/
  short vstamp;			/* version stamp			*/
  bfd_vma tsize;		/* text size in bytes, padded to FW bdry*/
  bfd_vma dsize;		/* initialized data "  "		*/
  bfd_vma bsize;		/* uninitialized data "   "		*/
  bfd_vma entry;		/* entry pt.				*/
  bfd_vma text_start;		/* base of text used for this file */
  bfd_vma data_start;		/* base of data used for this file */

  /* i960 stuff */
  unsigned long tagentries;	/* number of tag entries to follow */

  /* RS/6000 stuff */
  bfd_vma o_toc;		/* address of TOC			*/
  short o_snentry;		/* section number for entry point */
  short o_sntext;		/* section number for text	*/
  short o_sndata;		/* section number for data	*/
  short o_sntoc;		/* section number for toc	*/
  short o_snloader;		/* section number for loader section */
  short o_snbss;		/* section number for bss	*/
  short o_algntext;		/* max alignment for text	*/
  short o_algndata;		/* max alignment for data	*/
  short o_modtype;		/* Module type field, 1R,RE,RO	*/
  short o_cputype;		/* Encoded CPU type		*/
  bfd_vma o_maxstack;	/* max stack size allowed.	*/
  bfd_vma o_maxdata;	/* max data size allowed.	*/

  /* ECOFF stuff */
  bfd_vma bss_start;		/* Base of bss section.		*/
  bfd_vma gp_value;		/* GP register value.		*/
  unsigned long gprmask;	/* General registers used.	*/
  unsigned long cprmask[4];	/* Coprocessor registers used.	*/
  unsigned long fprmask;	/* Floating pointer registers used.  */

  /* Apollo stuff */
  long o_inlib;			/* inlib data */
  long o_sri;			/* Static Resource Information */
  long vid[2];			/* Version id */

  struct internal_extra_pe_aouthdr pe;
};

/********************** STORAGE CLASSES **********************/

/* This used to be defined as -1, but now n_sclass is unsigned.  */
#define C_EFCN		0xff	/* physical end of function	*/
#define C_NULL		0
#define C_AUTO		1	/* automatic variable		*/
#define C_EXT		2	/* external symbol		*/
#define C_STAT		3	/* static			*/
#define C_REG		4	/* register variable		*/
#define C_EXTDEF	5	/* external definition		*/
#define C_LABEL		6	/* label			*/
#define C_ULABEL	7	/* undefined label		*/
#define C_MOS		8	/* member of structure		*/
#define C_ARG		9	/* function argument		*/
#define C_STRTAG	10	/* structure tag		*/
#define C_MOU		11	/* member of union		*/
#define C_UNTAG		12	/* union tag			*/
#define C_TPDEF		13	/* type definition		*/
#define C_USTATIC	14	/* undefined static		*/
#define C_ENTAG		15	/* enumeration tag		*/
#define C_MOE		16	/* member of enumeration	*/
#define C_REGPARM	17	/* register parameter		*/
#define C_FIELD		18	/* bit field			*/
#define C_AUTOARG	19	/* auto argument		*/
#define C_LASTENT	20	/* dummy entry (end of block)	*/
#define C_BLOCK		100	/* ".bb" or ".eb"		*/
#define C_FCN		101	/* ".bf" or ".ef"		*/
#define C_EOS		102	/* end of structure		*/
#define C_FILE		103	/* file name			*/
#define C_LINE		104	/* line # reformatted as symbol table entry */
#define C_ALIAS	 	105	/* duplicate tag		*/
#define C_HIDDEN	106	/* ext symbol in dmert public lib */
#define C_WEAKEXT	127	/* weak symbol -- GNU extension.  */

/* New storage classes for TI COFF */
#define C_UEXT		19	/* Tentative external definition */
#define C_STATLAB	20	/* Static load time label */
#define C_EXTLAB	21	/* External load time label */
#define C_SYSTEM	23	/* System Wide variable */

/* New storage classes for WINDOWS_NT   */
#define C_SECTION       104     /* section name */
#define C_NT_WEAK	105	/* weak external */

 /* New storage classes for 80960 */

/* C_LEAFPROC is obsolete.  Use C_LEAFEXT or C_LEAFSTAT */
#define C_LEAFPROC	108	/* Leaf procedure, "call" via BAL */

#define C_SCALL		107	/* Procedure reachable via system call */
#define C_LEAFEXT       108	/* External leaf */
#define C_LEAFSTAT      113	/* Static leaf */
#define C_OPTVAR	109	/* Optimized variable		*/
#define C_DEFINE	110	/* Preprocessor #define		*/
#define C_PRAGMA	111	/* Advice to compiler or linker	*/
#define C_SEGMENT	112	/* 80960 segment name		*/

  /* Storage classes for m88k */
#define C_SHADOW        107     /* shadow symbol                */
#define C_VERSION       108     /* coff version symbol          */

 /* New storage classes for RS/6000 */
#define C_HIDEXT        107	/* Un-named external symbol */
#define C_BINCL         108	/* Marks beginning of include file */
#define C_EINCL         109	/* Marks ending of include file */
#define C_AIX_WEAKEXT   111	/* AIX definition of C_WEAKEXT.  */

#if defined _AIX52 || defined AIX_WEAK_SUPPORT
#undef C_WEAKEXT
#define C_WEAKEXT       C_AIX_WEAKEXT
#endif

 /* storage classes for stab symbols for RS/6000 */
#define C_GSYM          (0x80)
#define C_LSYM          (0x81)
#define C_PSYM          (0x82)
#define C_RSYM          (0x83)
#define C_RPSYM         (0x84)
#define C_STSYM         (0x85)
#define C_TCSYM         (0x86)
#define C_BCOMM         (0x87)
#define C_ECOML         (0x88)
#define C_ECOMM         (0x89)
#define C_DECL          (0x8c)
#define C_ENTRY         (0x8d)
#define C_FUN           (0x8e)
#define C_BSTAT         (0x8f)
#define C_ESTAT         (0x90)

/* Storage classes for Thumb symbols */
#define C_THUMBEXT      (128 + C_EXT)		/* 130 */
#define C_THUMBSTAT     (128 + C_STAT)		/* 131 */
#define C_THUMBLABEL    (128 + C_LABEL)		/* 134 */
#define C_THUMBEXTFUNC  (C_THUMBEXT  + 20)	/* 150 */
#define C_THUMBSTATFUNC (C_THUMBSTAT + 20)	/* 151 */

/* True if XCOFF symbols of class CLASS have auxillary csect information.  */
#define CSECT_SYM_P(CLASS) \
  ((CLASS) == C_EXT || (CLASS) == C_AIX_WEAKEXT || (CLASS) == C_HIDEXT)

/********************** SECTION HEADER **********************/

#define SCNNMLEN (8)

struct internal_scnhdr
{
  char s_name[SCNNMLEN];	/* section name			*/

  /* Physical address, aliased s_nlib.
     In the pei format, this field is the virtual section size
     (the size of the section after being loaded int memory),
     NOT the physical address.  */
  bfd_vma s_paddr;

  bfd_vma s_vaddr;		/* virtual address		*/
  bfd_vma s_size;		/* section size			*/
  bfd_vma s_scnptr;		/* file ptr to raw data for section */
  bfd_vma s_relptr;		/* file ptr to relocation	*/
  bfd_vma s_lnnoptr;		/* file ptr to line numbers	*/
  unsigned long s_nreloc;	/* number of relocation entries	*/
  unsigned long s_nlnno;	/* number of line number entries*/
  long s_flags;			/* flags			*/
  long s_align;			/* used on I960			*/
  unsigned char s_page;         /* TI COFF load page            */
};

/* s_flags "type".  */
#define STYP_REG	 (0x0000)	/* "regular": allocated, relocated, loaded */
#define STYP_DSECT	 (0x0001)	/* "dummy":  relocated only*/
#define STYP_NOLOAD	 (0x0002)	/* "noload": allocated, relocated, not loaded */
#define STYP_GROUP	 (0x0004)	/* "grouped": formed of input sections */
#define STYP_PAD	 (0x0008)	/* "padding": not allocated, not relocated, loaded */
#define STYP_COPY	 (0x0010)	/* "copy": for decision function used by field update;  not allocated, not relocated,
									     loaded; reloc & lineno entries processed normally */
#define STYP_TEXT	 (0x0020)	/* section contains text only */
#define S_SHRSEG	 (0x0020)	/* In 3b Update files (output of ogen), sections which appear in SHARED segments of the Pfile
									     will have the S_SHRSEG flag set by ogen, to inform dufr that updating 1 copy of the proc. will
									     update all process invocations. */
#define STYP_DATA	 (0x0040)	/* section contains data only */
#define STYP_BSS	 (0x0080)	/* section contains bss only */
#define S_NEWFCN	 (0x0100)	/* In a minimal file or an update file, a new function (as compared with a replaced function) */
#define STYP_INFO	 (0x0200)	/* comment: not allocated not relocated, not loaded */
#define STYP_OVER	 (0x0400)	/* overlay: relocated not allocated or loaded */
#define STYP_LIB	 (0x0800)	/* for .lib: same as INFO */
#define STYP_MERGE	 (0x2000)	/* merge section -- combines with text, data or bss sections only */
#define STYP_REVERSE_PAD (0x4000)	/* section will be padded with no-op instructions
					   wherever padding is necessary and there is a
					   word of contiguous bytes beginning on a word
					   boundary. */

#define STYP_LIT	0x8020	/* Literal data (like STYP_TEXT) */


/********************** LINE NUMBERS **********************/

/* 1 line number entry for every "breakpointable" source line in a section.
   Line numbers are grouped on a per function basis; first entry in a function
   grouping will have l_lnno = 0 and in place of physical address will be the
   symbol table index of the function name.  */

struct internal_lineno
{
  union
  {
    bfd_signed_vma l_symndx;		/* function name symbol index, iff l_lnno == 0*/
    bfd_signed_vma l_paddr;		/* (physical) address of line number	*/
  }     l_addr;
  unsigned long l_lnno;		/* line number		*/
};

/********************** SYMBOLS **********************/

#define SYMNMLEN	8	/* # characters in a symbol name	*/
#define FILNMLEN	14	/* # characters in a file name		*/
#define DIMNUM		4	/* # array dimensions in auxiliary entry */

struct internal_syment
{
  union
  {
    char _n_name[SYMNMLEN];	/* old COFF version		*/
    struct
    {
      long _n_zeroes;		/* new == 0			*/
      long _n_offset;		/* offset into string table	*/
    }      _n_n;
    char *_n_nptr[2];		/* allows for overlaying	*/
  }     _n;
  bfd_vma n_value;		/* value of symbol		*/
  short n_scnum;		/* section number		*/
  unsigned short n_flags;	/* copy of flags from filhdr	*/
  unsigned short n_type;	/* type and derived type	*/
  unsigned char n_sclass;	/* storage class		*/
  unsigned char n_numaux;	/* number of aux. entries	*/
};

#define n_name		_n._n_name
#define n_zeroes	_n._n_n._n_zeroes
#define n_offset	_n._n_n._n_offset

/* Relocatable symbols have number of the section in which they are defined,
   or one of the following:  */

#define N_UNDEF	((short)0)	/* undefined symbol */
#define N_ABS	((short)-1)	/* value of symbol is absolute */
#define N_DEBUG	((short)-2)	/* debugging symbol -- value is meaningless */
#define N_TV	((short)-3)	/* indicates symbol needs preload transfer vector */
#define P_TV	((short)-4)	/* indicates symbol needs postload transfer vector*/

/* Type of a symbol, in low N bits of the word.  */

#define T_NULL		0
#define T_VOID		1	/* function argument (only used by compiler) */
#define T_CHAR		2	/* character		*/
#define T_SHORT		3	/* short integer	*/
#define T_INT		4	/* integer		*/
#define T_LONG		5	/* long integer		*/
#define T_FLOAT		6	/* floating point	*/
#define T_DOUBLE	7	/* double word		*/
#define T_STRUCT	8	/* structure 		*/
#define T_UNION		9	/* union 		*/
#define T_ENUM		10	/* enumeration 		*/
#define T_MOE		11	/* member of enumeration*/
#define T_UCHAR		12	/* unsigned character	*/
#define T_USHORT	13	/* unsigned short	*/
#define T_UINT		14	/* unsigned integer	*/
#define T_ULONG		15	/* unsigned long	*/
#define T_LNGDBL	16	/* long double		*/

/* Derived types, in n_type.  */

#define DT_NON		(0)	/* no derived type */
#define DT_PTR		(1)	/* pointer */
#define DT_FCN		(2)	/* function */
#define DT_ARY		(3)	/* array */

#define BTYPE(x)	((x) & N_BTMASK)
#define DTYPE(x)	(((x) & N_TMASK) >> N_BTSHFT)

#define ISPTR(x) \
  (((unsigned long) (x) & N_TMASK) == ((unsigned long) DT_PTR << N_BTSHFT))
#define ISFCN(x) \
  (((unsigned long) (x) & N_TMASK) == ((unsigned long) DT_FCN << N_BTSHFT))
#define ISARY(x) \
  (((unsigned long) (x) & N_TMASK) == ((unsigned long) DT_ARY << N_BTSHFT))
#define ISTAG(x) \
  ((x) == C_STRTAG || (x) == C_UNTAG || (x) == C_ENTAG)
#define DECREF(x) \
  ((((x) >> N_TSHIFT) & ~ N_BTMASK) | ((x) & N_BTMASK))

union internal_auxent
{
  struct
  {

    union
    {
      long l;			/* str, un, or enum tag indx */
      struct coff_ptr_struct *p;
    }     x_tagndx;

    union
    {
      struct
      {
	unsigned short x_lnno;	/* declaration line number */
	unsigned short x_size;	/* str/union/array size */
      }      x_lnsz;
      long x_fsize;		/* size of function */
    }     x_misc;

    union
    {
      struct
      {				/* if ISFCN, tag, or .bb */
	bfd_signed_vma x_lnnoptr;		/* ptr to fcn line # */
	union
	{			/* entry ndx past block end */
	  long l;
	  struct coff_ptr_struct *p;
	}     x_endndx;
      }      x_fcn;

      struct
      {				/* if ISARY, up to 4 dimen. */
	unsigned short x_dimen[DIMNUM];
      }      x_ary;
    }     x_fcnary;

    unsigned short x_tvndx;	/* tv index */
  }      x_sym;

  union
  {
    char x_fname[FILNMLEN];
    struct
    {
      long x_zeroes;
      long x_offset;
    }      x_n;
  }     x_file;

  struct
  {
    long x_scnlen;		/* section length */
    unsigned short x_nreloc;	/* # relocation entries */
    unsigned short x_nlinno;	/* # line numbers */
    unsigned long x_checksum;	/* section COMDAT checksum for PE */
    unsigned short x_associated; /* COMDAT associated section index for PE */
    unsigned char x_comdat;	/* COMDAT selection number for PE */
  }      x_scn;

  struct
  {
    long x_tvfill;		/* tv fill value */
    unsigned short x_tvlen;	/* length of .tv */
    unsigned short x_tvran[2];	/* tv range */
  }      x_tv;			/* info about .tv section (in auxent of symbol .tv)) */

  /******************************************
   * RS/6000-specific auxent - last auxent for every external symbol
   ******************************************/
  struct
  {
    union
      {				/* csect length or enclosing csect */
	bfd_signed_vma l;
	struct coff_ptr_struct *p;
      } x_scnlen;
    long x_parmhash;		/* parm type hash index */
    unsigned short x_snhash;	/* sect num with parm hash */
    unsigned char x_smtyp;	/* symbol align and type */
    /* 0-4 - Log 2 of alignment */
    /* 5-7 - symbol type */
    unsigned char x_smclas;	/* storage mapping class */
    long x_stab;		/* dbx stab info index */
    unsigned short x_snstab;	/* sect num with dbx stab */
  }      x_csect;		/* csect definition information */

/* x_smtyp values:  */

#define	SMTYP_ALIGN(x)	((x) >> 3)	/* log2 of alignment */
#define	SMTYP_SMTYP(x)	((x) & 0x7)	/* symbol type */
/* Symbol type values:  */
#define	XTY_ER	0		/* External reference */
#define	XTY_SD	1		/* Csect definition */
#define	XTY_LD	2		/* Label definition */
#define XTY_CM	3		/* .BSS */
#define	XTY_EM	4		/* Error message */
#define	XTY_US	5		/* "Reserved for internal use" */

/* x_smclas values:  */

#define	XMC_PR	0		/* Read-only program code */
#define	XMC_RO	1		/* Read-only constant */
#define	XMC_DB	2		/* Read-only debug dictionary table */
#define	XMC_TC	3		/* Read-write general TOC entry */
#define	XMC_UA	4		/* Read-write unclassified */
#define	XMC_RW	5		/* Read-write data */
#define	XMC_GL	6		/* Read-only global linkage */
#define	XMC_XO	7		/* Read-only extended operation */
#define	XMC_SV	8		/* Read-only supervisor call */
#define	XMC_BS	9		/* Read-write BSS */
#define	XMC_DS	10		/* Read-write descriptor csect */
#define	XMC_UC	11		/* Read-write unnamed Fortran common */
#define	XMC_TI	12		/* Read-only traceback index csect */
#define	XMC_TB	13		/* Read-only traceback table csect */
/* 		14	??? */
#define	XMC_TC0	15		/* Read-write TOC anchor */
#define XMC_TD	16		/* Read-write data in TOC */

  /******************************************
   *  I960-specific *2nd* aux. entry formats
   ******************************************/
  struct
  {
    /* This is a very old typo that keeps getting propagated. */
#define x_stdindx x_stindx
    long x_stindx;		/* sys. table entry */
  }      x_sc;			/* system call entry */

  struct
  {
    unsigned long x_balntry;	/* BAL entry point */
  }      x_bal;			/* BAL-callable function */

  struct
  {
    unsigned long x_timestamp;	/* time stamp */
    char x_idstring[20];	/* producer identity string */
  }      x_ident;		/* Producer ident info */

};

/********************** RELOCATION DIRECTIVES **********************/

struct internal_reloc
{
  bfd_vma r_vaddr;		/* Virtual address of reference */
  long r_symndx;		/* Index into symbol table	*/
  unsigned short r_type;	/* Relocation type		*/
  unsigned char r_size;		/* Used by RS/6000 and ECOFF	*/
  unsigned char r_extern;	/* Used by ECOFF		*/
  unsigned long r_offset;	/* Used by Alpha ECOFF, SPARC, others */
};

/* X86-64 relocations.  */
#define R_AMD64_ABS 		 0 /* Reference is absolute, no relocation is necessary.  */
#define R_AMD64_DIR64		 1 /* 64-bit address (VA).  */
#define R_AMD64_DIR32		 2 /* 32-bit address (VA) R_DIR32.  */
#define R_AMD64_IMAGEBASE	 3 /* 32-bit absolute ref w/o base R_IMAGEBASE.  */
#define R_AMD64_PCRLONG		 4 /* 32-bit relative address from byte following reloc R_PCRLONG.  */
#define R_AMD64_PCRLONG_1	 5 /* 32-bit relative address from byte distance 1 from reloc.  */
#define R_AMD64_PCRLONG_2	 6 /* 32-bit relative address from byte distance 2 from reloc.  */
#define R_AMD64_PCRLONG_3	 7 /* 32-bit relative address from byte distance 3 from reloc.  */
#define R_AMD64_PCRLONG_4	 8 /* 32-bit relative address from byte distance 4 from reloc.  */
#define R_AMD64_PCRLONG_5	 9 /* 32-bit relative address from byte distance 5 from reloc.  */
#define R_AMD64_SECTION		10 /* Section index.  */
#define R_AMD64_SECREL		11 /* 32 bit offset from base of section containing target R_SECREL.  */
#define R_AMD64_SECREL7		12 /* 7 bit unsigned offset from base of section containing target.  */
#define R_AMD64_TOKEN		13 /* 32 bit metadata token.  */
#define R_AMD64_PCRQUAD		14 /* Pseude PC64 relocation - Note: not specified by MS/AMD but need for gas pc-relative 64bit wide relocation generated by ELF.  */

/* i386 Relocations.  */

#define R_DIR16 	 1
#define R_REL24          5
#define R_DIR32 	 6
#define R_IMAGEBASE	 7
#define R_SECREL32	11
#define R_RELBYTE	15
#define R_RELWORD	16
#define R_RELLONG	17
#define R_PCRBYTE	18
#define R_PCRWORD	19
#define R_PCRLONG	20
#define R_PCR24         21
#define R_IPRSHORT	24
#define R_IPRLONG	26
#define R_GETSEG	29
#define R_GETPA 	30
#define R_TAGWORD	31
#define R_JUMPTARG	32	/* strange 29k 00xx00xx reloc */
#define R_PARTLS16      32
#define R_PARTMS8       33

#define R_PCR16L       128
#define R_PCR26L       129
#define R_VRT16        130
#define R_HVRT16       131
#define R_LVRT16       132
#define R_VRT32        133


/* This reloc identifies mov.b instructions with a 16bit absolute
   address.  The linker tries to turn insns with this reloc into
   an absolute 8-bit address.  */
#define R_MOV16B1    	0x41

/* This reloc identifies mov.b instructions which had a 16bit
   absolute address which have been shortened into a 8-bit
   absolute address.  */
#define R_MOV16B2 	0x42

/* This reloc identifies jmp insns with a 16bit target address;
   the linker tries to turn these insns into bra insns with
   an 8bit pc-relative target.  */
#define R_JMP1     	0x43

/* This reloc identifies a bra with an 8-bit pc-relative
   target that was formerly a jmp insn with a 16bit target.  */
#define R_JMP2 		0x44

/* ??? */
#define R_RELLONG_NEG  	0x45

/* This reloc identifies jmp insns with a 24bit target address;
   the linker tries to turn these insns into bra insns with
   an 8bit pc-relative target.  */
#define R_JMPL1     	0x46

/* This reloc identifies a bra with an 8-bit pc-relative
   target that was formerly a jmp insn with a 24bit target.  */
#define R_JMPL2		0x47

/* This reloc identifies mov.b instructions with a 24bit absolute
   address.  The linker tries to turn insns with this reloc into
   an absolute 8-bit address.  */

#define R_MOV24B1    	0x48

/* This reloc identifies mov.b instructions which had a 24bit
   absolute address which have been shortened into a 8-bit
   absolute address.  */
#define R_MOV24B2 	0x49

/* An h8300 memory indirect jump/call.  Forces the address of the jump/call
   target into the function vector (in page zero), and the address of the
   vector entry to be placed in the jump/call instruction.  */
#define R_MEM_INDIRECT	0x4a

/* This reloc identifies a 16bit pc-relative branch target which was
   shortened into an 8bit pc-relative branch target.  */
#define R_PCRWORD_B	0x4b

/* This reloc identifies mov.[wl] instructions with a 32/24 bit
   absolute address; the linker may turn this into a mov.[wl]
   insn with a 16bit absolute address.  */
#define R_MOVL1    	0x4c

/* This reloc identifies mov.[wl] insns which formerly had
   a 32/24bit absolute address and now have a 16bit absolute address.  */
#define R_MOVL2 	0x4d

/* This reloc identifies a bCC:8 which will have it's condition
   inverted and its target redirected to the target of the branch
   in the following insn.  */
#define R_BCC_INV	0x4e

/* This reloc identifies a jmp instruction that has been deleted.  */
#define R_JMP_DEL	0x4f

/* Z8k modes */
#define R_IMM16   0x01		/* 16 bit abs */
#define R_JR	  0x02		/* jr  8 bit disp */
#define R_IMM4L   0x23		/* low nibble */
#define R_IMM8    0x22		/* 8 bit abs */
#define R_IMM32   R_RELLONG	/* 32 bit abs */
#define R_CALL    R_DA		/* Absolute address which could be a callr */
#define R_JP	  R_DA		/* Absolute address which could be a jp */
#define R_REL16   0x04		/* 16 bit PC rel */
#define R_CALLR	  0x05		/* callr 12 bit disp */
#define R_SEG     0x10		/* set if in segmented mode */
#define R_IMM4H   0x24		/* high nibble */
#define R_DISP7   0x25          /* djnz displacement */

/* Z80 modes */
#define R_OFF8    0x32		/* 8 bit signed abs, for (i[xy]+d) */
#define R_IMM24   0x33          /* 24 bit abs */
/* R_JR, R_IMM8, R_IMM16, R_IMM32 - as for Z8k */

/* H8500 modes */

#define R_H8500_IMM8  	1		/*  8 bit immediate 	*/
#define R_H8500_IMM16 	2		/* 16 bit immediate	*/
#define R_H8500_PCREL8 	3		/*  8 bit pcrel 	*/
#define R_H8500_PCREL16 4		/* 16 bit pcrel 	*/
#define R_H8500_HIGH8  	5		/* high 8 bits of 24 bit address */
#define R_H8500_LOW16 	7		/* low 16 bits of 24 bit immediate */
#define R_H8500_IMM24	6		/* 24 bit immediate */
#define R_H8500_IMM32   8               /* 32 bit immediate */
#define R_H8500_HIGH16  9		/* high 16 bits of 32 bit immediate */

/* W65 modes */

#define R_W65_ABS8	1  /* addr & 0xff 		*/
#define R_W65_ABS16	2  /* addr & 0xffff 		*/
#define R_W65_ABS24	3  /* addr & 0xffffff 		*/

#define R_W65_ABS8S8    4  /* (addr >> 8) & 0xff 	*/
#define R_W65_ABS8S16   5  /* (addr >> 16) & 0xff 	*/

#define R_W65_ABS16S8   6  /* (addr >> 8) & 0ffff 	*/
#define R_W65_ABS16S16  7  /* (addr >> 16) & 0ffff 	*/

#define R_W65_PCR8	8
#define R_W65_PCR16	9

#define R_W65_DP       10  /* direct page 8 bits only   */

#endif /* GNU_COFF_INTERNAL_H */
