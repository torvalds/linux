/* Fortran language support definitions for GDB, the GNU debugger.
   Copyright 1992, 1993, 1994, 1995, 1998, 2000
   Free Software Foundation, Inc.
   Contributed by Motorola.  Adapted from the C definitions by Farooq Butt
   (fmbutt@engage.sps.mot.com).

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

extern int f_parse (void);

extern void f_error (char *);	/* Defined in f-exp.y */

extern void f_print_type (struct type *, char *, struct ui_file *, int,
			  int);

extern int f_val_print (struct type *, char *, int, CORE_ADDR,
			struct ui_file *, int, int, int,
			enum val_prettyprint);

/* Language-specific data structures */

struct common_entry
  {
    struct symbol *symbol;	/* The symbol node corresponding
				   to this component */
    struct common_entry *next;	/* The next component */
  };

struct saved_f77_common
  {
    char *name;			/* Name of COMMON */
    char *owning_function;	/* Name of parent function */
    int secnum;			/* Section # of .bss */
    CORE_ADDR offset;		/* Offset from .bss for 
				   this block */
    struct common_entry *entries;	/* List of block's components */
    struct common_entry *end_of_entries;	/* ptr. to end of components */
    struct saved_f77_common *next;	/* Next saved COMMON block */
  };

typedef struct saved_f77_common SAVED_F77_COMMON, *SAVED_F77_COMMON_PTR;

typedef struct common_entry COMMON_ENTRY, *COMMON_ENTRY_PTR;

extern SAVED_F77_COMMON_PTR head_common_list;	/* Ptr to 1st saved COMMON  */
extern SAVED_F77_COMMON_PTR tail_common_list;	/* Ptr to last saved COMMON  */
extern SAVED_F77_COMMON_PTR current_common;	/* Ptr to current COMMON */

extern SAVED_F77_COMMON_PTR find_common_for_function (char *, char *);

#define UNINITIALIZED_SECNUM -1
#define COMMON_NEEDS_PATCHING(blk) ((blk)->secnum == UNINITIALIZED_SECNUM)

#define BLANK_COMMON_NAME_ORIGINAL "#BLNK_COM"	/* XLF assigned  */
#define BLANK_COMMON_NAME_MF77     "__BLNK__"	/* MF77 assigned  */
#define BLANK_COMMON_NAME_LOCAL    "__BLANK"	/* Local GDB */

#define BOUND_FETCH_OK 1
#define BOUND_FETCH_ERROR -999

/* When reasonable array bounds cannot be fetched, such as when 
   you ask to 'mt print symbols' and there is no stack frame and 
   therefore no way of knowing the bounds of stack-based arrays, 
   we have to assign default bounds, these are as good as any... */

#define DEFAULT_UPPER_BOUND 999999
#define DEFAULT_LOWER_BOUND -999999

extern char *real_main_name;	/* Name of main function */
extern int real_main_c_value;	/* C_value field of main function */

extern int f77_get_dynamic_upperbound (struct type *, int *);

extern int f77_get_dynamic_lowerbound (struct type *, int *);

extern void f77_get_dynamic_array_length (struct type *);

extern int calc_f77_array_dims (struct type *);

#define DEFAULT_DOTMAIN_NAME_IN_MF77            ".MAIN_"
#define DEFAULT_MAIN_NAME_IN_MF77               "MAIN_"
#define DEFAULT_DOTMAIN_NAME_IN_XLF_BUGGY       ".main "
#define DEFAULT_DOTMAIN_NAME_IN_XLF             ".main"
