/* This file is a modified version of 'a.out.h'.  It is to be used in all
   GNU tools modified to support the i80960 (or tools that operate on
   object files created by such tools).
   
   Copyright 2001 Free Software Foundation, Inc.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */
 
/* All i80960 development is done in a CROSS-DEVELOPMENT environment.  I.e.,
   object code is generated on, and executed under the direction of a symbolic
   debugger running on, a host system.  We do not want to be subject to the
   vagaries of which host it is or whether it supports COFF or a.out format,
   or anything else.  We DO want to:
  
  	o always generate the same format object files, regardless of host.
 
 	o have an 'a.out' header that we can modify for our own purposes
 	  (the 80960 is typically an embedded processor and may require
 	  enhanced linker support that the normal a.out.h header can't
 	  accommodate).
 
  As for byte-ordering, the following rules apply:
 
 	o Text and data that is actually downloaded to the target is always
 	  in i80960 (little-endian) order.
 
 	o All other numbers (in the header, symbols, relocation directives)
 	  are in host byte-order:  object files CANNOT be lifted from a
 	  little-end host and used on a big-endian (or vice versa) without
 	  modification.
  ==> THIS IS NO LONGER TRUE USING BFD.  WE CAN GENERATE ANY BYTE ORDER
      FOR THE HEADER, AND READ ANY BYTE ORDER.  PREFERENCE WOULD BE TO
      USE LITTLE-ENDIAN BYTE ORDER THROUGHOUT, REGARDLESS OF HOST.  <==
 
 	o The downloader ('comm960') takes care to generate a pseudo-header
 	  with correct (i80960) byte-ordering before shipping text and data
 	  off to the NINDY monitor in the target systems.  Symbols and
 	  relocation info are never sent to the target.  */

#define BMAGIC	0415
/* We don't accept the following (see N_BADMAG macro).
   They're just here so GNU code will compile.  */
#define	OMAGIC	0407		/* old impure format */
#define	NMAGIC	0410		/* read-only text */
#define	ZMAGIC	0413		/* demand load format */

/* FILE HEADER
  	All 'lengths' are given as a number of bytes.
  	All 'alignments' are for relinkable files only;  an alignment of
  		'n' indicates the corresponding segment must begin at an
  		address that is a multiple of (2**n).  */
struct external_exec
  {
    /* Standard stuff */
    unsigned char e_info[4];	/* Identifies this as a b.out file */
    unsigned char e_text[4];	/* Length of text */
    unsigned char e_data[4];	/* Length of data */
    unsigned char e_bss[4];	/* Length of uninitialized data area */
    unsigned char e_syms[4];	/* Length of symbol table */
    unsigned char e_entry[4];	/* Runtime start address */
    unsigned char e_trsize[4];	/* Length of text relocation info */
    unsigned char e_drsize[4];	/* Length of data relocation info */

    /* Added for i960 */
    unsigned char e_tload[4];	/* Text runtime load address */
    unsigned char e_dload[4];	/* Data runtime load address */
    unsigned char e_talign[1];	/* Alignment of text segment */
    unsigned char e_dalign[1];	/* Alignment of data segment */
    unsigned char e_balign[1];	/* Alignment of bss segment */
    unsigned char e_relaxable[1];/* Assembled with enough info to allow linker to relax */
  };

#define	EXEC_BYTES_SIZE	(sizeof (struct external_exec))

/* These macros use the a_xxx field names, since they operate on the exec
   structure after it's been byte-swapped and realigned on the host machine.  */
#define N_BADMAG(x)	(((x).a_info)!=BMAGIC)
#define N_TXTOFF(x)	EXEC_BYTES_SIZE
#define N_DATOFF(x)	( N_TXTOFF(x) + (x).a_text )
#define N_TROFF(x)	( N_DATOFF(x) + (x).a_data )
#define N_TRELOFF	N_TROFF
#define N_DROFF(x)	( N_TROFF(x) + (x).a_trsize )
#define N_DRELOFF	N_DROFF
#define N_SYMOFF(x)	( N_DROFF(x) + (x).a_drsize )
#define N_STROFF(x)	( N_SYMOFF(x) + (x).a_syms )
#define N_DATADDR(x)	( (x).a_dload )    

/* Address of text segment in memory after it is loaded.  */
#if !defined (N_TXTADDR)
#define N_TXTADDR(x) 0
#endif

/* A single entry in the symbol table.  */
struct nlist
  {
    union
      {
	char*          n_name;
	struct nlist * n_next;
	long	       n_strx;	/* Index into string table	*/
      } n_un;

    unsigned char n_type;	/* See below				*/
    char	  n_other;	/* Used in i80960 support -- see below	*/
    short	  n_desc;
    unsigned long n_value;
  };


/* Legal values of n_type.  */
#define N_UNDF	0	/* Undefined symbol	*/
#define N_ABS	2	/* Absolute symbol	*/
#define N_TEXT	4	/* Text symbol		*/
#define N_DATA	6	/* Data symbol		*/
#define N_BSS	8	/* BSS symbol		*/
#define N_FN	31	/* Filename symbol	*/

#define N_EXT	1	/* External symbol (OR'd in with one of above)	*/
#define N_TYPE	036	/* Mask for all the type bits			*/
#define N_STAB	0340	/* Mask for all bits used for SDB entries 	*/

/* MEANING OF 'n_other'
 
  If non-zero, the 'n_other' fields indicates either a leaf procedure or
  a system procedure, as follows:
 
 	1 <= n_other <= 32 :
 		The symbol is the entry point to a system procedure.
 		'n_value' is the address of the entry, as for any other
 		procedure.  The system procedure number (which can be used in
 		a 'calls' instruction) is (n_other-1).  These entries come from
 		'.sysproc' directives.
 
 	n_other == N_CALLNAME
 		the symbol is the 'call' entry point to a leaf procedure.
 		The *next* symbol in the symbol table must be the corresponding
 		'bal' entry point to the procedure (see following).  These
 		entries come from '.leafproc' directives in which two different
 		symbols are specified (the first one is represented here).
 	
 
 	n_other == N_BALNAME
 		the symbol is the 'bal' entry point to a leaf procedure.
 		These entries result from '.leafproc' directives in which only
 		one symbol is specified, or in which the same symbol is
 		specified twice.
 
  Note that an N_CALLNAME entry *must* have a corresponding N_BALNAME entry,
  but not every N_BALNAME entry must have an N_CALLNAME entry.  */
#define N_CALLNAME	((char)-1)
#define N_BALNAME	((char)-2)
#define IS_CALLNAME(x)	(N_CALLNAME == (x))
#define IS_BALNAME(x)	(N_BALNAME == (x))
#define IS_OTHER(x)	((x)>0 && (x) <=32)

#define b_out_relocation_info relocation_info
struct relocation_info
  {
    int	 r_address;	/* File address of item to be relocated.  */
    unsigned
#define r_index r_symbolnum
    r_symbolnum:24,	/* Index of symbol on which relocation is based,
			   if r_extern is set.  Otherwise set to
			   either N_TEXT, N_DATA, or N_BSS to
			   indicate section on which relocation is
			   based.  */
      r_pcrel:1,	/* 1 => relocate PC-relative; else absolute
			   On i960, pc-relative implies 24-bit
			   address, absolute implies 32-bit.  */
      r_length:2,	/* Number of bytes to relocate:
			   0 => 1 byte
			   1 => 2 bytes -- used for 13 bit pcrel
			   2 => 4 bytes.  */
      r_extern:1,
      r_bsr:1,		/* Something for the GNU NS32K assembler.  */
      r_disp:1,		/* Something for the GNU NS32K assembler.  */
      r_callj:1,	/* 1 if relocation target is an i960 'callj'.  */
      r_relaxable:1;	/* 1 if enough info is left to relax the data.  */
};
