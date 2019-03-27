/* ldmain.h -
   Copyright 1991, 1992, 1993, 1994, 1995, 1996, 1999, 2002, 2003, 2004,
   2005 Free Software Foundation, Inc.

   This file is part of GLD, the Gnu Linker.

   GLD is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 1, or (at your option)
   any later version.

   GLD is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GLD; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef LDMAIN_H
#define LDMAIN_H

extern char *program_name;
extern const char *ld_sysroot;
extern char *ld_canon_sysroot;
extern int ld_canon_sysroot_len;
extern bfd *output_bfd;
extern char *default_target;
extern bfd_boolean trace_files;
extern bfd_boolean trace_file_tries;
extern bfd_boolean version_printed;
extern bfd_boolean whole_archive;
extern bfd_boolean as_needed;
extern bfd_boolean add_needed;
extern bfd_boolean demangling;
extern int g_switch_value;
extern const char *output_filename;
extern struct bfd_link_info link_info;
extern int overflow_cutoff_limit;

extern void add_ysym (const char *);
extern void add_wrap (const char *);
extern void add_keepsyms_file (const char *);

#endif
