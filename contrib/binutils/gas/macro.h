/* macro.h - header file for macro support for gas
   Copyright 1994, 1995, 1996, 1997, 1998, 2000, 2002, 2003, 2004, 2006
   Free Software Foundation, Inc.

   Written by Steve and Judy Chamberlain of Cygnus Support,
      sac@cygnus.com

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
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef MACRO_H

#define MACRO_H

/* Structures used to store macros.

   Each macro knows its name and included text.  It gets built with a
   list of formal arguments, and also keeps a hash table which points
   into the list to speed up formal search.  Each formal knows its
   name and its default value.  Each time the macro is expanded, the
   formals get the actual values attached to them.  */

/* Describe the formal arguments to a macro.  */

typedef struct formal_struct {
  struct formal_struct *next;	/* Next formal in list.  */
  sb name;			/* Name of the formal.  */
  sb def;			/* The default value.  */
  sb actual;			/* The actual argument (changed on each expansion).  */
  int index;			/* The index of the formal 0..formal_count - 1.  */
  enum formal_type
    {
      FORMAL_OPTIONAL,
      FORMAL_REQUIRED,
      FORMAL_VARARG
    } type;			/* The kind of the formal.  */
} formal_entry;

/* Other values found in the index field of a formal_entry.  */
#define QUAL_INDEX (-1)
#define NARG_INDEX (-2)
#define LOCAL_INDEX (-3)

/* Describe the macro.  */

typedef struct macro_struct
{
  sb sub;				/* Substitution text.  */
  int formal_count;			/* Number of formal args.  */
  formal_entry *formals;		/* Pointer to list of formal_structs.  */
  struct hash_control *formal_hash;	/* Hash table of formals.  */
  const char *name;			/* Macro name.  */
  char *file;				/* File the macro was defined in.  */
  unsigned int line;			/* Line number of definition.  */
} macro_entry;

/* Whether any macros have been defined.  */

extern int macro_defined;

/* The macro nesting level.  */

extern int macro_nest;

/* The macro hash table.  */

extern struct hash_control *macro_hash;

extern int buffer_and_nest (const char *, const char *, sb *, int (*) (sb *));
extern void macro_init
  (int, int, int, int (*) (const char *, int, sb *, int *));
extern void macro_set_alternate (int);
extern void macro_mri_mode (int);
extern const char *define_macro
  (int, sb *, sb *, int (*) (sb *), char *, unsigned int, const char **);
extern int check_macro (const char *, sb *, const char **, macro_entry **);
extern void delete_macro (const char *);
extern const char *expand_irp (int, int, sb *, sb *, int (*) (sb *));

#endif
