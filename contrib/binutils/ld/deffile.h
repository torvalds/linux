/* deffile.h - header for .DEF file parser
   Copyright 1998, 1999, 2000, 2002, 2003 Free Software Foundation, Inc.
   Written by DJ Delorie dj@cygnus.com

   This file is part of GLD, the Gnu Linker.

   GLD is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GLD is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GLD; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef DEFFILE_H
#define DEFFILE_H

/* DEF storage definitions.  Note that any ordinal may be zero, and
   any pointer may be NULL, if not defined by the DEF file.  */

typedef struct def_file_section {
  char *name;			/* always set */
  char *class;			/* may be NULL */
  char flag_read, flag_write, flag_execute, flag_shared;
} def_file_section;

typedef struct def_file_export {
  char *name;			/* always set */
  char *internal_name;		/* always set, may == name */
  int ordinal;			/* -1 if not specified */
  int hint;
  char flag_private, flag_constant, flag_noname, flag_data, flag_forward;
} def_file_export;

typedef struct def_file_module {
  struct def_file_module *next;
  void *user_data;
  char name[1];			/* extended via malloc */
} def_file_module;

typedef struct def_file_import {
  char *internal_name;		/* always set */
  def_file_module *module;	/* always set */
  char *name;			/* may be NULL; either this or ordinal will be set */
  int ordinal;			/* may be -1 */
  int data;			/* = 1 if data */
} def_file_import;

typedef struct def_file {
  /* From the NAME or LIBRARY command.  */
  char *name;
  int is_dll;			/* -1 if NAME/LIBRARY not given */
  bfd_vma base_address;		/* (bfd_vma)(-1) if unspecified */

  /* From the DESCRIPTION command.  */
  char *description;

  /* From the STACK/HEAP command, -1 if unspecified.  */
  int stack_reserve, stack_commit;
  int heap_reserve, heap_commit;

  /* From the SECTION/SEGMENT commands.  */
  int num_section_defs;
  def_file_section *section_defs;

  /* From the EXPORTS commands.  */
  int num_exports;
  def_file_export *exports;

  /* Used by imports for module names.  */
  def_file_module *modules;

  /* From the IMPORTS commands.  */
  int num_imports;
  def_file_import *imports;

  /* From the VERSION command, -1 if not specified.  */
  int version_major, version_minor;
} def_file;

extern def_file *def_file_empty (void);

/* The second arg may be NULL.  If not, this .def is appended to it.  */
extern def_file *def_file_parse (const char *, def_file *);
extern void def_file_free (def_file *);
extern def_file_export *def_file_add_export (def_file *, const char *,
					     const char *, int);
extern def_file_import *def_file_add_import (def_file *, const char *,
					     const char *, int, const char *);
extern void def_file_add_directive (def_file *, const char *, int);
extern def_file_module *def_get_module (def_file *, const char *);
#ifdef DEF_FILE_PRINT
extern void def_file_print (FILE *, def_file *);
#endif

#endif /* DEFFILE_H */
