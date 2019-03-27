/* Internal demangler interface for g++ V3 ABI.
   Copyright (C) 2003, 2004 Free Software Foundation, Inc.
   Written by Ian Lance Taylor <ian@wasabisystems.com>.

   This file is part of the libiberty library, which is part of GCC.

   This file is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   In addition to the permissions in the GNU General Public License, the
   Free Software Foundation gives you unlimited permission to link the
   compiled version of this file into combinations with other programs,
   and to distribute those combinations without any restriction coming
   from the use of this file.  (The General Public License restrictions
   do apply in other respects; for example, they cover modification of
   the file, and distribution when not linked into a combined
   executable.)

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA. 
*/

/* This file provides some definitions shared by cp-demangle.c and
   cp-demint.c.  It should not be included by any other files.  */

/* Information we keep for operators.  */

struct demangle_operator_info
{
  /* Mangled name.  */
  const char *code;
  /* Real name.  */
  const char *name;
  /* Length of real name.  */
  int len;
  /* Number of arguments.  */
  int args;
};

/* How to print the value of a builtin type.  */

enum d_builtin_type_print
{
  /* Print as (type)val.  */
  D_PRINT_DEFAULT,
  /* Print as integer.  */
  D_PRINT_INT,
  /* Print as unsigned integer, with trailing "u".  */
  D_PRINT_UNSIGNED,
  /* Print as long, with trailing "l".  */
  D_PRINT_LONG,
  /* Print as unsigned long, with trailing "ul".  */
  D_PRINT_UNSIGNED_LONG,
  /* Print as long long, with trailing "ll".  */
  D_PRINT_LONG_LONG,
  /* Print as unsigned long long, with trailing "ull".  */
  D_PRINT_UNSIGNED_LONG_LONG,
  /* Print as bool.  */
  D_PRINT_BOOL,
  /* Print as float--put value in square brackets.  */
  D_PRINT_FLOAT,
  /* Print in usual way, but here to detect void.  */
  D_PRINT_VOID
};

/* Information we keep for a builtin type.  */

struct demangle_builtin_type_info
{
  /* Type name.  */
  const char *name;
  /* Length of type name.  */
  int len;
  /* Type name when using Java.  */
  const char *java_name;
  /* Length of java name.  */
  int java_len;
  /* How to print a value of this type.  */
  enum d_builtin_type_print print;
};

/* The information structure we pass around.  */

struct d_info
{
  /* The string we are demangling.  */
  const char *s;
  /* The end of the string we are demangling.  */
  const char *send;
  /* The options passed to the demangler.  */
  int options;
  /* The next character in the string to consider.  */
  const char *n;
  /* The array of components.  */
  struct demangle_component *comps;
  /* The index of the next available component.  */
  int next_comp;
  /* The number of available component structures.  */
  int num_comps;
  /* The array of substitutions.  */
  struct demangle_component **subs;
  /* The index of the next substitution.  */
  int next_sub;
  /* The number of available entries in the subs array.  */
  int num_subs;
  /* The number of substitutions which we actually made from the subs
     array, plus the number of template parameter references we
     saw.  */
  int did_subs;
  /* The last name we saw, for constructors and destructors.  */
  struct demangle_component *last_name;
  /* A running total of the length of large expansions from the
     mangled name to the demangled name, such as standard
     substitutions and builtin types.  */
  int expansion;
};

/* To avoid running past the ending '\0', don't:
   - call d_peek_next_char if d_peek_char returned '\0'
   - call d_advance with an 'i' that is too large
   - call d_check_char(di, '\0')
   Everything else is safe.  */
#define d_peek_char(di) (*((di)->n))
#define d_peek_next_char(di) ((di)->n[1])
#define d_advance(di, i) ((di)->n += (i))
#define d_check_char(di, c) (d_peek_char(di) == c ? ((di)->n++, 1) : 0)
#define d_next_char(di) (d_peek_char(di) == '\0' ? '\0' : *((di)->n++))
#define d_str(di) ((di)->n)

/* Functions and arrays in cp-demangle.c which are referenced by
   functions in cp-demint.c.  */
#ifdef IN_GLIBCPP_V3
#define CP_STATIC_IF_GLIBCPP_V3 static
#else
#define CP_STATIC_IF_GLIBCPP_V3 extern
#endif

CP_STATIC_IF_GLIBCPP_V3
const struct demangle_operator_info cplus_demangle_operators[];

#define D_BUILTIN_TYPE_COUNT (26)

CP_STATIC_IF_GLIBCPP_V3
const struct demangle_builtin_type_info
cplus_demangle_builtin_types[D_BUILTIN_TYPE_COUNT];

CP_STATIC_IF_GLIBCPP_V3
struct demangle_component *
cplus_demangle_mangled_name (struct d_info *, int);

CP_STATIC_IF_GLIBCPP_V3
struct demangle_component *
cplus_demangle_type (struct d_info *);

extern void
cplus_demangle_init_info (const char *, int, size_t, struct d_info *);

/* cp-demangle.c needs to define this a little differently */
#undef CP_STATIC_IF_GLIBCPP_V3
