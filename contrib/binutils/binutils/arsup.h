/* arsup.h - archive support header file
   Copyright 1992, 1993, 1994, 1996, 2001, 2002, 2003
   Free Software Foundation, Inc.

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
Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

struct list {
	char *name;
	struct list *next;
};

void maybequit (void);

void prompt (void);

void ar_clear (void);

void ar_replace (struct list *);

void ar_delete (struct list *);

void ar_save (void);

void ar_list (void);

void ar_open (char *, int);

void ar_directory (char *, struct list *, char *);

void ar_addmod (struct list *);

void ar_addlib (char *, struct list *);

void ar_end (void);

void ar_extract (struct list *);

bfd *open_inarch (const char *archive_filename, const char *);

extern int yylex (void);

int yyparse (void);

/* Functions from ar.c */

void extract_file (bfd * abfd);

extern int interactive;
