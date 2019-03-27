/* dlltool.h -- header file for dlltool
   Copyright 1997, 1998, 2003, 2004 Free Software Foundation, Inc.

   This file is part of GNU Binutils.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "ansidecl.h"
#include <stdio.h>

extern void def_code (int);
extern void def_data (int);
extern void def_description (const char *);
extern void def_exports (const char *, const char *, int, int, int, int, int);
extern void def_heapsize (int, int);
extern void def_import
  (const char *, const char *, const char *, const char *, int);
extern void def_library (const char *, int);
extern void def_name (const char *, int);
extern void def_section (const char *, int);
extern void def_stacksize (int, int);
extern void def_version (int, int);
extern int  yyparse (void);
extern int  yyerror (const char *);
extern int  yylex (void);

extern int yydebug;
extern FILE *yyin;
extern int linenumber;
