/* `a.out' object-file definitions, including extensions to 64-bit fields

   Copyright 1999, 2000, 2001, 2003 Free Software Foundation, Inc.

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

#ifndef __A_OUT_64_H__
#define __A_OUT_64_H__

#ifndef BYTES_IN_WORD
#define BYTES_IN_WORD 4
#endif

/* This is the layout on disk of the 32-bit or 64-bit exec header.  */

#ifndef external_exec
struct external_exec 
{
  bfd_byte e_info[4];		    /* Magic number and stuff.  */
  bfd_byte e_text[BYTES_IN_WORD];   /* Length of text section in bytes.  */
  bfd_byte e_data[BYTES_IN_WORD];   /* Length of data section in bytes.  */
  bfd_byte e_bss[BYTES_IN_WORD];    /* Length of bss area in bytes.  */
  bfd_byte e_syms[BYTES_IN_WORD];   /* Length of symbol table in bytes.  */
  bfd_byte e_entry[BYTES_IN_WORD];  /* Start address.  */
  bfd_byte e_trsize[BYTES_IN_WORD]; /* Length of text relocation info.  */
  bfd_byte e_drsize[BYTES_IN_WORD]; /* Length of data relocation info.  */
};

#define	EXEC_BYTES_SIZE	(4 + BYTES_IN_WORD * 7)

/* Magic numbers for a.out files.  */

#if ARCH_SIZE==64
#define OMAGIC 0x1001		/* Code indicating object file.  */
#define ZMAGIC 0x1002		/* Code indicating demand-paged executable.  */
#define NMAGIC 0x1003		/* Code indicating pure executable.  */

/* There is no 64-bit QMAGIC as far as I know.  */

#define N_BADMAG(x)	  (N_MAGIC(x) != OMAGIC		\
			&& N_MAGIC(x) != NMAGIC		\
  			&& N_MAGIC(x) != ZMAGIC)
#else
#define OMAGIC 0407		/* Object file or impure executable.  */
#define NMAGIC 0410		/* Code indicating pure executable.  */
#define ZMAGIC 0413		/* Code indicating demand-paged executable.  */
#define BMAGIC 0415		/* Used by a b.out object.  */

/* This indicates a demand-paged executable with the header in the text.
   It is used by 386BSD (and variants) and Linux, at least.  */
#ifndef QMAGIC
#define QMAGIC 0314
#endif
# ifndef N_BADMAG
#  define N_BADMAG(x)	  (N_MAGIC(x) != OMAGIC		\
			&& N_MAGIC(x) != NMAGIC		\
  			&& N_MAGIC(x) != ZMAGIC \
		        && N_MAGIC(x) != QMAGIC)
# endif /* N_BADMAG */
#endif

#endif

#ifdef QMAGIC
#define N_IS_QMAGIC(x) (N_MAGIC (x) == QMAGIC)
#else
#define N_IS_QMAGIC(x) (0)
#endif

/* The difference between TARGET_PAGE_SIZE and N_SEGSIZE is that TARGET_PAGE_SIZE is
   the finest granularity at which you can page something, thus it
   controls the padding (if any) before the text segment of a ZMAGIC
   file.  N_SEGSIZE is the resolution at which things can be marked as
   read-only versus read/write, so it controls the padding between the
   text segment and the data segment (in memory; on disk the padding
   between them is TARGET_PAGE_SIZE).  TARGET_PAGE_SIZE and N_SEGSIZE are the same
   for most machines, but different for sun3.  */

/* By default, segment size is constant.  But some machines override this
   to be a function of the a.out header (e.g. machine type).  */

#ifndef	N_SEGSIZE
#define	N_SEGSIZE(x)	SEGMENT_SIZE
#endif

/* Virtual memory address of the text section.
   This is getting very complicated.  A good reason to discard a.out format
   for something that specifies these fields explicitly.  But til then...

   * OMAGIC and NMAGIC files:
       (object files: text for "relocatable addr 0" right after the header)
       start at 0, offset is EXEC_BYTES_SIZE, size as stated.
   * The text address, offset, and size of ZMAGIC files depend
     on the entry point of the file:
     * entry point below TEXT_START_ADDR:
       (hack for SunOS shared libraries)
       start at 0, offset is 0, size as stated.
     * If N_HEADER_IN_TEXT(x) is true (which defaults to being the
       case when the entry point is EXEC_BYTES_SIZE or further into a page):
       no padding is needed; text can start after exec header.  Sun
       considers the text segment of such files to include the exec header;
       for BFD's purposes, we don't, which makes more work for us.
       start at TEXT_START_ADDR + EXEC_BYTES_SIZE, offset is EXEC_BYTES_SIZE,
       size as stated minus EXEC_BYTES_SIZE.
     * If N_HEADER_IN_TEXT(x) is false (which defaults to being the case when
       the entry point is less than EXEC_BYTES_SIZE into a page (e.g. page
       aligned)): (padding is needed so that text can start at a page boundary)
       start at TEXT_START_ADDR, offset TARGET_PAGE_SIZE, size as stated.

    Specific configurations may want to hardwire N_HEADER_IN_TEXT,
    for efficiency or to allow people to play games with the entry point.
    In that case, you would #define N_HEADER_IN_TEXT(x) as 1 for sunos,
    and as 0 for most other hosts (Sony News, Vax Ultrix, etc).
    (Do this in the appropriate bfd target file.)
    (The default is a heuristic that will break if people try changing
    the entry point, perhaps with the ld -e flag.)

    * QMAGIC is always like a ZMAGIC for which N_HEADER_IN_TEXT is true,
    and for which the starting address is TARGET_PAGE_SIZE (or should this be
    SEGMENT_SIZE?) (TEXT_START_ADDR only applies to ZMAGIC, not to QMAGIC).  */

/* This macro is only relevant for ZMAGIC files; QMAGIC always has the header
   in the text.  */
#ifndef N_HEADER_IN_TEXT
#define N_HEADER_IN_TEXT(x) \
  (((x).a_entry & (TARGET_PAGE_SIZE-1)) >= EXEC_BYTES_SIZE)
#endif

/* Sun shared libraries, not linux.  This macro is only relevant for ZMAGIC
   files.  */
#ifndef N_SHARED_LIB
#if defined (TEXT_START_ADDR) && TEXT_START_ADDR == 0
#define N_SHARED_LIB(x) (0)
#else
#define N_SHARED_LIB(x) ((x).a_entry < TEXT_START_ADDR)
#endif
#endif

/* Returning 0 not TEXT_START_ADDR for OMAGIC and NMAGIC is based on
   the assumption that we are dealing with a .o file, not an
   executable.  This is necessary for OMAGIC (but means we don't work
   right on the output from ld -N); more questionable for NMAGIC.  */

#ifndef N_TXTADDR
#define N_TXTADDR(x) \
    (/* The address of a QMAGIC file is always one page in,		\
        with the header in the text.  */				\
     N_IS_QMAGIC (x)							\
     ? (bfd_vma) TARGET_PAGE_SIZE + EXEC_BYTES_SIZE			\
     : (N_MAGIC (x) != ZMAGIC						\
	? (bfd_vma) 0	/* Object file or NMAGIC.  */			\
	: (N_SHARED_LIB (x)						\
	   ? (bfd_vma) 0						\
	   : (N_HEADER_IN_TEXT (x)					\
	      ? (bfd_vma) TEXT_START_ADDR + EXEC_BYTES_SIZE		\
	      : (bfd_vma) TEXT_START_ADDR))))
#endif

/* If N_HEADER_IN_TEXT is not true for ZMAGIC, there is some padding
   to make the text segment start at a certain boundary.  For most
   systems, this boundary is TARGET_PAGE_SIZE.  But for Linux, in the
   time-honored tradition of crazy ZMAGIC hacks, it is 1024 which is
   not what TARGET_PAGE_SIZE needs to be for QMAGIC.  */

#ifndef ZMAGIC_DISK_BLOCK_SIZE
#define ZMAGIC_DISK_BLOCK_SIZE TARGET_PAGE_SIZE
#endif

#define N_DISK_BLOCK_SIZE(x) \
  (N_MAGIC(x) == ZMAGIC ? ZMAGIC_DISK_BLOCK_SIZE : TARGET_PAGE_SIZE)

/* Offset in an a.out of the start of the text section. */
#ifndef N_TXTOFF
#define N_TXTOFF(x)							\
    (/* For {O,N,Q}MAGIC, no padding.  */				\
     N_MAGIC (x) != ZMAGIC						\
     ? EXEC_BYTES_SIZE							\
     : (N_SHARED_LIB (x)						\
	? 0								\
	: (N_HEADER_IN_TEXT (x)						\
	   ? EXEC_BYTES_SIZE		/* No padding.  */		\
	   : ZMAGIC_DISK_BLOCK_SIZE	/* A page of padding.  */)))
#endif
/* Size of the text section.  It's always as stated, except that we
   offset it to `undo' the adjustment to N_TXTADDR and N_TXTOFF
   for ZMAGIC files that nominally include the exec header
   as part of the first page of text.  (BFD doesn't consider the
   exec header to be part of the text segment.)  */
#ifndef N_TXTSIZE
#define	N_TXTSIZE(x) \
  (/* For QMAGIC, we don't consider the header part of the text section.  */\
   N_IS_QMAGIC (x)							\
   ? (x).a_text - EXEC_BYTES_SIZE					\
   : ((N_MAGIC (x) != ZMAGIC || N_SHARED_LIB (x))			\
      ? (x).a_text							\
      : (N_HEADER_IN_TEXT (x)						\
	 ? (x).a_text - EXEC_BYTES_SIZE	/* No padding.  */		\
	 : (x).a_text			/* A page of padding.  */ )))
#endif
/* The address of the data segment in virtual memory.
   It is the text segment address, plus text segment size, rounded
   up to a N_SEGSIZE boundary for pure or pageable files.  */
#ifndef N_DATADDR
#define N_DATADDR(x) \
  (N_MAGIC (x) == OMAGIC						\
   ? (N_TXTADDR (x) + N_TXTSIZE (x))					\
   : (N_SEGSIZE (x) + ((N_TXTADDR (x) + N_TXTSIZE (x) - 1)		\
		       & ~ (bfd_vma) (N_SEGSIZE (x) - 1))))
#endif
/* The address of the BSS segment -- immediately after the data segment.  */

#define N_BSSADDR(x)	(N_DATADDR (x) + (x).a_data)

/* Offsets of the various portions of the file after the text segment.  */

/* For {Q,Z}MAGIC, there is padding to make the data segment start on
   a page boundary.  Most of the time the a_text field (and thus
   N_TXTSIZE) already contains this padding.  It is possible that for
   BSDI and/or 386BSD it sometimes doesn't contain the padding, and
   perhaps we should be adding it here.  But this seems kind of
   questionable and probably should be BSDI/386BSD-specific if we do
   do it.

   For NMAGIC (at least for hp300 BSD, probably others), there is
   padding in memory only, not on disk, so we must *not* ever pad here
   for NMAGIC.  */

#ifndef N_DATOFF
#define N_DATOFF(x)	(N_TXTOFF (x) + N_TXTSIZE (x))
#endif
#ifndef N_TRELOFF
#define N_TRELOFF(x)	(N_DATOFF (x) + (x).a_data)
#endif
#ifndef N_DRELOFF
#define N_DRELOFF(x)	(N_TRELOFF (x) + (x).a_trsize)
#endif
#ifndef N_SYMOFF
#define N_SYMOFF(x)	(N_DRELOFF (x) + (x).a_drsize)
#endif
#ifndef N_STROFF
#define N_STROFF(x)	(N_SYMOFF (x) + (x).a_syms)
#endif

/* Symbols */
#ifndef external_nlist
struct external_nlist
{
  bfd_byte e_strx[BYTES_IN_WORD];	/* Index into string table of name.  */
  bfd_byte e_type[1];			/* Type of symbol.  */
  bfd_byte e_other[1];			/* Misc info (usually empty).  */
  bfd_byte e_desc[2];			/* Description field.  */
  bfd_byte e_value[BYTES_IN_WORD];	/* Value of symbol.  */
};
#define EXTERNAL_NLIST_SIZE (BYTES_IN_WORD+4+BYTES_IN_WORD)
#endif

struct internal_nlist
{
  unsigned long n_strx;			/* Index into string table of name.  */
  unsigned char n_type;			/* Type of symbol.  */
  unsigned char n_other;		/* Misc info (usually empty).  */
  unsigned short n_desc;		/* Description field.  */
  bfd_vma n_value;			/* Value of symbol.  */
};

/* The n_type field is the symbol type, containing:  */

#define N_UNDF	0	/* Undefined symbol.  */
#define N_ABS 	2	/* Absolute symbol -- defined at particular addr.  */
#define N_TEXT 	4	/* Text sym -- defined at offset in text seg.  */
#define N_DATA 	6	/* Data sym -- defined at offset in data seg.  */
#define N_BSS 	8	/* BSS  sym -- defined at offset in zero'd seg.  */
#define	N_COMM	0x12	/* Common symbol (visible after shared lib dynlink).  */
#define N_FN	0x1f	/* File name of .o file.  */
#define	N_FN_SEQ 0x0C	/* N_FN from Sequent compilers (sigh).  */
/* Note: N_EXT can only be usefully OR-ed with N_UNDF, N_ABS, N_TEXT,
   N_DATA, or N_BSS.  When the low-order bit of other types is set,
   (e.g. N_WARNING versus N_FN), they are two different types.  */
#define N_EXT 	1	/* External symbol (as opposed to local-to-this-file).  */
#define N_TYPE  0x1e
#define N_STAB 	0xe0	/* If any of these bits are on, it's a debug symbol.  */

#define N_INDR 0x0a

/* The following symbols refer to set elements.
   All the N_SET[ATDB] symbols with the same name form one set.
   Space is allocated for the set in the text section, and each set
   elements value is stored into one word of the space.
   The first word of the space is the length of the set (number of elements).

   The address of the set is made into an N_SETV symbol
   whose name is the same as the name of the set.
   This symbol acts like a N_DATA global symbol
   in that it can satisfy undefined external references.  */

/* These appear as input to LD, in a .o file.  */
#define	N_SETA	0x14		/* Absolute set element symbol.  */
#define	N_SETT	0x16		/* Text set element symbol.  */
#define	N_SETD	0x18		/* Data set element symbol.  */
#define	N_SETB	0x1A		/* Bss set element symbol.  */

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

/* Relocations 

  There	are two types of relocation flavours for a.out systems,
  standard and extended. The standard form is used on systems where the
  instruction has room for all the bits of an offset to the operand, whilst
  the extended form is used when an address operand has to be split over n
  instructions. Eg, on the 68k, each move instruction can reference
  the target with a displacement of 16 or 32 bits. On the sparc, move
  instructions use an offset of 14 bits, so the offset is stored in
  the reloc field, and the data in the section is ignored.  */

/* This structure describes a single relocation to be performed.
   The text-relocation section of the file is a vector of these structures,
   all of which apply to the text section.
   Likewise, the data-relocation section applies to the data section.  */

struct reloc_std_external
{
  bfd_byte r_address[BYTES_IN_WORD];	/* Offset of of data to relocate.  */
  bfd_byte r_index[3];			/* Symbol table index of symbol.  */
  bfd_byte r_type[1];			/* Relocation type.  */
};

#define	RELOC_STD_BITS_PCREL_BIG	((unsigned int) 0x80)
#define	RELOC_STD_BITS_PCREL_LITTLE	((unsigned int) 0x01)

#define	RELOC_STD_BITS_LENGTH_BIG	((unsigned int) 0x60)
#define	RELOC_STD_BITS_LENGTH_SH_BIG	5
#define	RELOC_STD_BITS_LENGTH_LITTLE	((unsigned int) 0x06)
#define	RELOC_STD_BITS_LENGTH_SH_LITTLE	1

#define	RELOC_STD_BITS_EXTERN_BIG	((unsigned int) 0x10)
#define	RELOC_STD_BITS_EXTERN_LITTLE	((unsigned int) 0x08)

#define	RELOC_STD_BITS_BASEREL_BIG	((unsigned int) 0x08)
#define	RELOC_STD_BITS_BASEREL_LITTLE	((unsigned int) 0x10)

#define	RELOC_STD_BITS_JMPTABLE_BIG	((unsigned int) 0x04)
#define	RELOC_STD_BITS_JMPTABLE_LITTLE	((unsigned int) 0x20)

#define	RELOC_STD_BITS_RELATIVE_BIG	((unsigned int) 0x02)
#define	RELOC_STD_BITS_RELATIVE_LITTLE	((unsigned int) 0x40)

#define	RELOC_STD_SIZE	(BYTES_IN_WORD + 3 + 1)		/* Bytes per relocation entry.  */

struct reloc_std_internal
{
  bfd_vma r_address;		/* Address (within segment) to be relocated.  */
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
     in files the symbol table.
     0 => relocate with the address of a segment.
     r_symbolnum is N_TEXT, N_DATA, N_BSS or N_ABS
     (the N_EXT bit may be set also, but signifies nothing).  */
  unsigned int r_extern:1;
  /* The next three bits are for SunOS shared libraries, and seem to
     be undocumented.  */
  unsigned int r_baserel:1;	/* Linkage table relative.  */
  unsigned int r_jmptable:1;	/* pc-relative to jump table.  */
  unsigned int r_relative:1;	/* "relative relocation".  */
  /* unused */
  unsigned int r_pad:1;		/* Padding -- set to zero.  */
};


/* EXTENDED RELOCS.   */

struct reloc_ext_external
{
  bfd_byte r_address[BYTES_IN_WORD];	/* Offset of of data to relocate.  */
  bfd_byte r_index[3];			/* Symbol table index of symbol.  */
  bfd_byte r_type[1];			/* Relocation type.  */
  bfd_byte r_addend[BYTES_IN_WORD];	/* Datum addend.  */
};

#ifndef RELOC_EXT_BITS_EXTERN_BIG
#define	RELOC_EXT_BITS_EXTERN_BIG	((unsigned int) 0x80)
#endif

#ifndef RELOC_EXT_BITS_EXTERN_LITTLE
#define	RELOC_EXT_BITS_EXTERN_LITTLE	((unsigned int) 0x01)
#endif

#ifndef RELOC_EXT_BITS_TYPE_BIG
#define	RELOC_EXT_BITS_TYPE_BIG		((unsigned int) 0x1F)
#endif

#ifndef RELOC_EXT_BITS_TYPE_SH_BIG
#define	RELOC_EXT_BITS_TYPE_SH_BIG	0
#endif

#ifndef RELOC_EXT_BITS_TYPE_LITTLE
#define	RELOC_EXT_BITS_TYPE_LITTLE	((unsigned int) 0xF8)
#endif

#ifndef RELOC_EXT_BITS_TYPE_SH_LITTLE
#define	RELOC_EXT_BITS_TYPE_SH_LITTLE	3
#endif

/* Bytes per relocation entry.  */
#define	RELOC_EXT_SIZE	(BYTES_IN_WORD + 3 + 1 + BYTES_IN_WORD)

enum reloc_type
{
  /* Simple relocations.  */
  RELOC_8,			/* data[0:7] = addend + sv 		*/
  RELOC_16,			/* data[0:15] = addend + sv 		*/
  RELOC_32,			/* data[0:31] = addend + sv 		*/
  /* PC-rel displacement.  */
  RELOC_DISP8,			/* data[0:7] = addend - pc + sv 	*/
  RELOC_DISP16,			/* data[0:15] = addend - pc + sv 	*/
  RELOC_DISP32,			/* data[0:31] = addend - pc + sv 	*/
  /* Special.  */
  RELOC_WDISP30,		/* data[0:29] = (addend + sv - pc)>>2 	*/
  RELOC_WDISP22,		/* data[0:21] = (addend + sv - pc)>>2 	*/
  RELOC_HI22,			/* data[0:21] = (addend + sv)>>10 	*/
  RELOC_22,			/* data[0:21] = (addend + sv) 		*/
  RELOC_13,			/* data[0:12] = (addend + sv)		*/
  RELOC_LO10,			/* data[0:9] = (addend + sv)		*/
  RELOC_SFA_BASE,		
  RELOC_SFA_OFF13,
  /* P.I.C. (base-relative).  */
  RELOC_BASE10,  		/* Not sure - maybe we can do this the */
  RELOC_BASE13,			/* right way now */
  RELOC_BASE22,
  /* For some sort of pc-rel P.I.C. (?)  */
  RELOC_PC10,
  RELOC_PC22,
  /* P.I.C. jump table.  */
  RELOC_JMP_TBL,
  /* Reputedly for shared libraries somehow.  */
  RELOC_SEGOFF16,
  RELOC_GLOB_DAT,
  RELOC_JMP_SLOT,
  RELOC_RELATIVE,

  RELOC_11,	
  RELOC_WDISP2_14,
  RELOC_WDISP19,
  RELOC_HHI22,			/* data[0:21] = (addend + sv) >> 42     */
  RELOC_HLO10,			/* data[0:9] = (addend + sv) >> 32      */
  
  /* 29K relocation types.  */
  RELOC_JUMPTARG,
  RELOC_CONST,
  RELOC_CONSTH,
  
  /* All the new ones I can think of, for sparc v9.  */
  RELOC_64,			/* data[0:63] = addend + sv 		*/
  RELOC_DISP64,			/* data[0:63] = addend - pc + sv 	*/
  RELOC_WDISP21,		/* data[0:20] = (addend + sv - pc)>>2 	*/
  RELOC_DISP21,			/* data[0:20] = addend - pc + sv        */
  RELOC_DISP14,			/* data[0:13] = addend - pc + sv 	*/
  /* Q .
     What are the other ones,
     Since this is a clean slate, can we throw away the ones we dont
     understand ? Should we sort the values ? What about using a
     microcode format like the 68k ?  */
  NO_RELOC
  };


struct reloc_internal
{
  bfd_vma r_address;		/* Offset of of data to relocate.  */
  long	r_index;		/* Symbol table index of symbol.  */
  enum reloc_type r_type;	/* Relocation type.  */
  bfd_vma r_addend;		/* Datum addend.  */
};

/* Q.
   Should the length of the string table be 4 bytes or 8 bytes ?

   Q.
   What about archive indexes ?  */

#endif				/* __A_OUT_64_H__ */
