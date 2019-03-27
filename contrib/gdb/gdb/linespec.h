/* Header for GDB line completion.
   Copyright 2000 Free Software Foundation, Inc.

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

#if !defined (LINESPEC_H)
#define LINESPEC_H 1

struct symtab;

extern struct symtabs_and_lines
	decode_line_1 (char **argptr, int funfirstline,
		       struct symtab *default_symtab, int default_line,
		       char ***canonical, int *not_found_ptr);

#endif /* defined (LINESPEC_H) */
