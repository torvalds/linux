/* Modula 2 language support definitions for GDB, the GNU debugger.
   Copyright 1992, 1998, 2000 Free Software Foundation, Inc.

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

extern int m2_parse (void);	/* Defined in m2-exp.y */

extern void m2_error (char *);	/* Defined in m2-exp.y */

/* Defined in m2-typeprint.c */
extern void m2_print_type (struct type *, char *, struct ui_file *, int,
			   int);

extern int m2_val_print (struct type *, char *, int, CORE_ADDR,
			 struct ui_file *, int, int, int,
			 enum val_prettyprint);
