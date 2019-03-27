/* ldfile.h -
   Copyright 1991, 1992, 1993, 1994, 1995, 2000, 2002, 2003, 2004
   Free Software Foundation, Inc.

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
   along with GLD; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street - Fifth Floor,
   Boston, MA 02110-1301, USA.  */

#ifndef LDFILE_H
#define LDFILE_H

extern const char *ldfile_input_filename;
extern bfd_boolean ldfile_assumed_script;
extern unsigned long ldfile_output_machine;
extern enum bfd_architecture ldfile_output_architecture;
extern const char *ldfile_output_machine_name;

/* Structure used to hold the list of directories to search for
   libraries.  */

typedef struct search_dirs {
  /* Next directory on list.  */
  struct search_dirs *next;
  /* Name of directory.  */
  const char *name;
  /* TRUE if this is from the command line.  */
  bfd_boolean cmdline;
  /* true if this is from within the sys-root.  */
  bfd_boolean sysrooted;
} search_dirs_type;

extern search_dirs_type *search_head;

extern void ldfile_add_arch
  (const char *);
extern void ldfile_add_library_path
  (const char *, bfd_boolean cmdline);
extern void ldfile_open_command_file
  (const char *name);
extern void ldfile_open_file
  (struct lang_input_statement_struct *);
extern bfd_boolean ldfile_try_open_bfd
  (const char *, struct lang_input_statement_struct *);
extern void ldfile_set_output_arch
  (const char *, enum bfd_architecture);
extern bfd_boolean ldfile_open_file_search
  (const char *arch, struct lang_input_statement_struct *,
   const char *lib, const char *suffix);

#endif
