/* Definitions for symbol-reading containing "stabs", for GDB.
   Copyright 1992, 1993, 1995, 1996, 1997, 1999, 2000
   Free Software Foundation, Inc.
   Contributed by Cygnus Support.  Written by John Gilmore.

   This file is part of GDB.

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
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* This file exists to hold the common definitions required of most of
   the symbol-readers that end up using stabs.  The common use of
   these `symbol-type-specific' customizations of the generic data
   structures makes the stabs-oriented symbol readers able to call
   each others' functions as required.  */

#if !defined (GDBSTABS_H)
#define GDBSTABS_H

/* The stab_section_info chain remembers info from the ELF symbol table,
   while psymtabs are being built for the other symbol tables in the 
   objfile.  It is destroyed at the complation of psymtab-reading.
   Any info that was used from it has been copied into psymtabs.  */

struct stab_section_info
  {
    char *filename;
    struct stab_section_info *next;
    int found;			/* Count of times it's found in searching */
    size_t num_sections;
    CORE_ADDR sections[1];
  };

/* Information is passed among various dbxread routines for accessing
   symbol files.  A pointer to this structure is kept in the sym_stab_info
   field of the objfile struct.  */

struct dbx_symfile_info
  {
    CORE_ADDR text_addr;	/* Start of text section */
    int text_size;		/* Size of text section */
    int symcount;		/* How many symbols are there in the file */
    char *stringtab;		/* The actual string table */
    int stringtab_size;		/* Its size */
    file_ptr symtab_offset;	/* Offset in file to symbol table */
    int symbol_size;		/* Bytes in a single symbol */
    struct stab_section_info *stab_section_info;	/* section starting points
							   of the original .o files before linking. */

    /* See stabsread.h for the use of the following. */
    struct header_file *header_files;
    int n_header_files;
    int n_allocated_header_files;

    /* Pointers to BFD sections.  These are used to speed up the building of
       minimal symbols.  */
    asection *text_section;
    asection *data_section;
    asection *bss_section;

    /* Pointer to the separate ".stab" section, if there is one.  */
    asection *stab_section;
  };

#define DBX_SYMFILE_INFO(o)	((o)->sym_stab_info)
#define DBX_TEXT_ADDR(o)	(DBX_SYMFILE_INFO(o)->text_addr)
#define DBX_TEXT_SIZE(o)	(DBX_SYMFILE_INFO(o)->text_size)
#define DBX_SYMCOUNT(o)		(DBX_SYMFILE_INFO(o)->symcount)
#define DBX_STRINGTAB(o)	(DBX_SYMFILE_INFO(o)->stringtab)
#define DBX_STRINGTAB_SIZE(o)	(DBX_SYMFILE_INFO(o)->stringtab_size)
#define DBX_SYMTAB_OFFSET(o)	(DBX_SYMFILE_INFO(o)->symtab_offset)
#define DBX_SYMBOL_SIZE(o)	(DBX_SYMFILE_INFO(o)->symbol_size)
#define DBX_TEXT_SECTION(o)	(DBX_SYMFILE_INFO(o)->text_section)
#define DBX_DATA_SECTION(o)	(DBX_SYMFILE_INFO(o)->data_section)
#define DBX_BSS_SECTION(o)	(DBX_SYMFILE_INFO(o)->bss_section)
#define DBX_STAB_SECTION(o)	(DBX_SYMFILE_INFO(o)->stab_section)

#endif /* GDBSTABS_H */
