/* Set up combined include path for the preprocessor.
   Copyright (C) 2003, 2004, 2005, 2006 Free Software Foundation, Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

extern void split_quote_chain (void);
extern void add_path (char *, int, int, bool);
extern void register_include_chains (cpp_reader *, const char *,
				     const char *, const char *,
				     int, int, int);
extern void add_cpp_dir_path (struct cpp_dir *, int);

struct target_c_incpath_s {
  /* Do extra includes processing.  STDINC is false iff -nostdinc was given.  */
  void (*extra_pre_includes) (const char *, const char *, int);
  void (*extra_includes) (const char *, const char *, int);
};

extern struct target_c_incpath_s target_c_incpath;

enum { QUOTE = 0, BRACKET, SYSTEM, AFTER };
