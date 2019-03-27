/* This file is aout_gnu.h

   Copyright 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1994, 2000, 2002
   Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifndef __A_OUT_GNU_H__
#define __A_OUT_GNU_H__

/* There are two main flavours of a.out, one which uses the standard
   relocations, and one which uses extended relocations.

   Today, the extended reloc uses are
   TC_SPARC

   each must define the enum reloc_type

*/

#define USE_EXTENDED_RELOC defined(TC_SPARC)

#if defined(TC_SPARC)
enum reloc_type
  {
    RELOC_8, RELOC_16, RELOC_32,/* simple relocations */
    RELOC_DISP8, RELOC_DISP16, RELOC_DISP32,	/* pc-rel displacement */
    RELOC_WDISP30, RELOC_WDISP22,
    RELOC_HI22, RELOC_22,
    RELOC_13, RELOC_LO10,
    RELOC_SFA_BASE, RELOC_SFA_OFF13,
    RELOC_BASE10, RELOC_BASE13, RELOC_BASE22,	/* P.I.C. (base-relative) */
    RELOC_PC10, RELOC_PC22,	/* for some sort of pc-rel P.I.C. (?) */
    RELOC_JMP_TBL,		/* P.I.C. jump table */
    RELOC_SEGOFF16,		/* reputedly for shared libraries somehow */
    RELOC_GLOB_DAT, RELOC_JMP_SLOT, RELOC_RELATIVE,
    RELOC_10, RELOC_11,
    RELOC_WDISP2_14,
    RELOC_WDISP19,
    RELOC_HHI22,
    RELOC_HLO10,

    /* 29K relocation types */
    RELOC_JUMPTARG, RELOC_CONST, RELOC_CONSTH,

    RELOC_WDISP14, RELOC_WDISP21,

    NO_RELOC
  };

#endif /* TC_SPARC */

#define __GNU_EXEC_MACROS__

#ifndef __STRUCT_EXEC_OVERRIDE__

/* This is the layout on disk of a Unix V7, Berkeley, SunOS, Vax Ultrix
   "struct exec".  Don't assume that on this machine, the "struct exec"
   will lay out the same sizes or alignments.  */

struct exec_bytes
  {
    unsigned char a_info[4];
    unsigned char a_text[4];
    unsigned char a_data[4];
    unsigned char a_bss[4];
    unsigned char a_syms[4];
    unsigned char a_entry[4];
    unsigned char a_trsize[4];
    unsigned char a_drsize[4];
  };

/* How big the "struct exec" is on disk */
#define	EXEC_BYTES_SIZE	(8 * 4)

/* This is the layout in memory of a "struct exec" while we process it.  */

struct exec
{
  unsigned long a_info;		/* Use macros N_MAGIC, etc for access */
  unsigned a_text;		/* length of text, in bytes */
  unsigned a_data;		/* length of data, in bytes */
  unsigned a_bss;		/* length of uninitialized data area for file, in bytes */
  unsigned a_syms;		/* length of symbol table data in file, in bytes */
  unsigned a_entry;		/* start address */
  unsigned a_trsize;		/* length of relocation info for text, in bytes */
  unsigned a_drsize;		/* length of relocation info for data, in bytes */
};

#endif /* __STRUCT_EXEC_OVERRIDE__ */

/* these go in the N_MACHTYPE field */
/* These symbols could be defined by code from Suns...punt 'em */
#undef M_UNKNOWN
#undef M_68010
#undef M_68020
#undef M_SPARC
enum machine_type
  {
    M_UNKNOWN = 0,
    M_68010 = 1,
    M_68020 = 2,
    M_SPARC = 3,
    /* skip a bunch so we don't run into any of sun's numbers */
    M_386 = 100,
    M_29K = 101,
    M_RS6000 = 102,		/* IBM RS/6000 */
    M_VAX4K_NETBSD = 150,
    /* HP/BSD formats */
    M_HP200 = 200,		/* hp200 (68010) BSD binary */
    M_HP300 = 300,		/* hp300 (68020+68881) BSD binary */
    M_HPUX23 = 0x020C		/* hp200/300 HPUX binary */
  };

#define N_MAGIC(exec) ((exec).a_info & 0xffff)
#define N_MACHTYPE(exec) ((enum machine_type)(((exec).a_info >> 16) & 0xff))
#define N_FLAGS(exec) (((exec).a_info >> 24) & 0xff)
#define N_SET_INFO(exec, magic, type, flags) \
    ((exec).a_info = ((magic) & 0xffff) \
     | (((int)(type) & 0xff) << 16) \
     | (((flags) & 0xff) << 24))
#define N_SET_MAGIC(exec, magic) \
    ((exec).a_info = (((exec).a_info & 0xffff0000) | ((magic) & 0xffff)))

#define N_SET_MACHTYPE(exec, machtype) \
    ((exec).a_info = \
     ((exec).a_info&0xff00ffff) | ((((int)(machtype))&0xff) << 16))

#define N_SET_FLAGS(exec, flags) \
    ((exec).a_info = \
     ((exec).a_info&0x00ffffff) | (((flags) & 0xff) << 24))

/* Code indicating object file or impure executable.  */
#ifndef OMAGIC
#define OMAGIC 0407
#endif
/* Code indicating pure executable.  */
#define NMAGIC 0410
/* Code indicating demand-paged executable.  */
#define ZMAGIC 0413

/* Virtual Address of text segment from the a.out file.  For OMAGIC,
   (almost always "unlinked .o's" these days), should be zero.
   For linked files, should reflect reality if we know it.  */

#ifndef N_TXTADDR
#define N_TXTADDR(x)	(N_MAGIC(x)==OMAGIC? 0 : TEXT_START_ADDR)
#endif

#ifndef N_BADMAG
#define N_BADMAG(x)	  (N_MAGIC(x) != OMAGIC		\
			   && N_MAGIC(x) != NMAGIC		\
			   && N_MAGIC(x) != ZMAGIC)
#endif

/* By default, segment size is constant.  But on some machines, it can
   be a function of the a.out header (e.g. machine type).  */
#ifndef	N_SEGSIZE
#define	N_SEGSIZE(x)	SEGMENT_SIZE
#endif

/* This complexity is for encapsulated COFF support */
#ifndef _N_HDROFF
#define _N_HDROFF(x)	(N_SEGSIZE(x) - sizeof (struct exec))
#endif

#ifndef N_TXTOFF
#define N_TXTOFF(x)	(N_MAGIC(x) == ZMAGIC ?	\
			 _N_HDROFF((x)) + sizeof (struct exec) :	\
			 sizeof (struct exec))
#endif

#ifndef N_DATOFF
#define N_DATOFF(x)	( N_TXTOFF(x) + (x).a_text )
#endif

#ifndef N_TRELOFF
#define N_TRELOFF(x)	( N_DATOFF(x) + (x).a_data )
#endif

#ifndef N_DRELOFF
#define N_DRELOFF(x)	( N_TRELOFF(x) + (x).a_trsize )
#endif

#ifndef N_SYMOFF
#define N_SYMOFF(x)	( N_DRELOFF(x) + (x).a_drsize )
#endif

#ifndef N_STROFF
#define N_STROFF(x)	( N_SYMOFF(x) + (x).a_syms )
#endif

/* Address of text segment in memory after it is loaded.  */
#ifndef N_TXTADDR
#define	N_TXTADDR(x)	0
#endif

#ifndef N_DATADDR
#define N_DATADDR(x) \
    (N_MAGIC(x)==OMAGIC? (N_TXTADDR(x)+(x).a_text) \
     :  (N_SEGSIZE(x) + ((N_TXTADDR(x)+(x).a_text-1) & ~(N_SEGSIZE(x)-1))))
#endif

/* Address of bss segment in memory after it is loaded.  */
#define N_BSSADDR(x) (N_DATADDR(x) + (x).a_data)

struct nlist
  {
    union
      {
	char *n_name;
	struct nlist *n_next;
	long n_strx;
      }
    n_un;
    unsigned char n_type;
    char n_other;
    short n_desc;
    unsigned long n_value;
  };

#define N_UNDF 0
#define N_ABS 2
#define N_TEXT 4
#define N_DATA 6
#define N_BSS 8
#define	N_COMM	0x12		/* common (visible in shared lib commons) */
#define N_FN 0x1F		/* File name of a .o file */

/* Note: N_EXT can only usefully be OR-ed with N_UNDF, N_ABS, N_TEXT,
   N_DATA, or N_BSS.  When the low-order bit of other types is set,
   (e.g. N_WARNING versus N_FN), they are two different types.  */
#define N_EXT 1
#define N_TYPE 036
#define N_STAB 0340

/* The following type indicates the definition of a symbol as being
   an indirect reference to another symbol.  The other symbol
   appears as an undefined reference, immediately following this symbol.

   Indirection is asymmetrical.  The other symbol's value will be used
   to satisfy requests for the indirect symbol, but not vice versa.
   If the other symbol does not have a definition, libraries will
   be searched to find a definition.  */

#define N_INDR 0xa

/* The following symbols refer to set elements.
   All the N_SET[ATDB] symbols with the same name form one set.
   Space is allocated for the set in the text section, and each set
   element's value is stored into one word of the space.
   The first word of the space is the length of the set (number of elements).

   The address of the set is made into an N_SETV symbol
   whose name is the same as the name of the set.
   This symbol acts like a N_DATA global symbol
   in that it can satisfy undefined external references.  */

/* These appear as input to LD, in a .o file.  */
#define	N_SETA	0x14		/* Absolute set element symbol */
#define	N_SETT	0x16		/* Text set element symbol */
#define	N_SETD	0x18		/* Data set element symbol */
#define	N_SETB	0x1A		/* Bss set element symbol */

/* This is output from LD.  */
#define N_SETV	0x1C		/* Pointer to set vector in data area.  */

/* Warning symbol. The text gives a warning message, the next symbol
   in the table will be undefined. When the symbol is referenced, the
   message is printed.  */

#define	N_WARNING 0x1e

/* Weak symbols.  These are a GNU extension to the a.out format.  The
   semantics are those of ELF weak symbols.  Weak symbols are always
   externally visible.  The N_WEAK? values are squeezed into the
   available slots.  The value of a N_WEAKU symbol is 0.  The values
   of the other types are the definitions.  */
#define N_WEAKU	0x0d		/* Weak undefined symbol.  */
#define N_WEAKA 0x0e		/* Weak absolute symbol.  */
#define N_WEAKT 0x0f		/* Weak text symbol.  */
#define N_WEAKD 0x10		/* Weak data symbol.  */
#define N_WEAKB 0x11		/* Weak bss symbol.  */

/* This structure describes a single relocation to be performed.
   The text-relocation section of the file is a vector of these structures,
   all of which apply to the text section.
   Likewise, the data-relocation section applies to the data section.  */

/* The following enum and struct were borrowed from SunOS's
   /usr/include/sun4/a.out.h  and extended to handle
   other machines.  It is currently used on SPARC and AMD 29000.

   reloc_ext_bytes is how it looks on disk.  reloc_info_extended is
   how we might process it on a native host.  */
#if USE_EXTENDED_RELOC

struct reloc_ext_bytes
  {
    unsigned char r_address[4];
    unsigned char r_index[3];
    unsigned char r_bits[1];
    unsigned char r_addend[4];
  };

#define	RELOC_EXT_BITS_EXTERN_BIG	0x80
#define	RELOC_EXT_BITS_EXTERN_LITTLE	0x01

#define	RELOC_EXT_BITS_TYPE_BIG		0x1F
#define	RELOC_EXT_BITS_TYPE_SH_BIG	0
#define	RELOC_EXT_BITS_TYPE_LITTLE	0xF8
#define	RELOC_EXT_BITS_TYPE_SH_LITTLE	3

#define	RELOC_EXT_SIZE	12	/* Bytes per relocation entry */

struct reloc_info_extended
{
  unsigned long r_address;
  unsigned int r_index:24;
# define	r_symbolnum  r_index
  unsigned r_extern:1;
  unsigned:2;
  /*  RS/6000 compiler does not support enum bitfield
	    enum reloc_type r_type:5; */
  enum reloc_type r_type;
  long int r_addend;
};

#else

/* The standard, old-fashioned, Berkeley compatible relocation struct */

#ifdef TC_I860
/* NOTE: three bits max, see struct reloc_info_i860.r_type */
enum i860_reloc_type
  {
    NO_RELOC = 0, BRADDR, LOW0, LOW1, LOW2, LOW3, LOW4, SPLIT0, SPLIT1, SPLIT2, RELOC_32,
  };

typedef enum i860_reloc_type reloc_type;

/* NOTE: two bits max, see reloc_info_i860.r_type */
enum highlow_type
  {
    NO_SPEC = 0, PAIR, HIGH, HIGHADJ,
  };

struct reloc_info_i860
{
  unsigned long r_address;
  /*
	 * Using bit fields here is a bad idea because the order is not portable. :-(
	 */
  unsigned int r_symbolnum:24;
  unsigned int r_pcrel:1;
  unsigned int r_extern:1;
  /* combining the two field simplifies the argument passing in "new_fix()" */
  /* and is compatible with the existing Sparc #ifdef's */
  /* r_type:  highlow_type - bits 5,4; reloc_type - bits 3-0 */
  unsigned int r_type:6;
  long r_addend;
};

#endif /* TC_I860 */

struct reloc_std_bytes
  {
    unsigned char r_address[4];
    unsigned char r_index[3];
    unsigned char r_bits[1];
  };

#define	RELOC_STD_BITS_PCREL_BIG	0x80
#define	RELOC_STD_BITS_PCREL_LITTLE	0x01

#define	RELOC_STD_BITS_LENGTH_BIG	0x60
#define	RELOC_STD_BITS_LENGTH_SH_BIG	5	/* To shift to units place */
#define	RELOC_STD_BITS_LENGTH_LITTLE	0x06
#define	RELOC_STD_BITS_LENGTH_SH_LITTLE	1

#define	RELOC_STD_BITS_EXTERN_BIG	0x10
#define	RELOC_STD_BITS_EXTERN_LITTLE	0x08

#define	RELOC_STD_BITS_BASEREL_BIG	0x08
#define	RELOC_STD_BITS_BASEREL_LITTLE	0x08

#define	RELOC_STD_BITS_JMPTABLE_BIG	0x04
#define	RELOC_STD_BITS_JMPTABLE_LITTLE	0x04

#define	RELOC_STD_BITS_RELATIVE_BIG	0x02
#define	RELOC_STD_BITS_RELATIVE_LITTLE	0x02

#define	RELOC_STD_SIZE	8	/* Bytes per relocation entry */

#endif /* USE_EXTENDED_RELOC */

#ifndef CUSTOM_RELOC_FORMAT
struct relocation_info
{
  /* Address (within segment) to be relocated.  */
  int r_address;
  /* The meaning of r_symbolnum depends on r_extern.  */
  unsigned int r_symbolnum:24;
  /* Nonzero means value is a pc-relative offset
	   and it should be relocated for changes in its own address
	   as well as for changes in the symbol or section specified.  */
  unsigned int r_pcrel:1;
  /* Length (as exponent of 2) of the field to be relocated.
	   Thus, a value of 2 indicates 1<<2 bytes.  */
  unsigned int r_length:2;
  /* 1 => relocate with value of symbol.
	   r_symbolnum is the index of the symbol
	   in file's the symbol table.
	   0 => relocate with the address of a segment.
	   r_symbolnum is N_TEXT, N_DATA, N_BSS or N_ABS
	   (the N_EXT bit may be set also, but signifies nothing).  */
  unsigned int r_extern:1;
  /* The next three bits are for SunOS shared libraries, and seem to
	   be undocumented.  */
#ifdef TC_NS32K
  unsigned int r_bsr:1;
  unsigned int r_disp:2;
#else
  unsigned int r_baserel:1;	/* Linkage table relative */
  unsigned int r_jmptable:1;	/* pc-relative to jump table */
  unsigned int r_relative:1;	/* "relative relocation" */
#endif /* TC_NS32K */
  /* unused */
  unsigned int r_pad:1;		/* Padding -- set to zero */
};

#endif /* CUSTOM_RELOC_FORMAT */

#endif /* __A_OUT_GNU_H__ */

/* end of aout_gnu.h */
