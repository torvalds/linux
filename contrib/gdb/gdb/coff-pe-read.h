/* Interface to coff-pe-read.c (portable-executable-specific symbol reader).

   Copyright 2003 Free Software Foundation, Inc.

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
   Boston, MA 02111-1307, USA.

   Contributed by Raoul M. Gough (RaoulGough@yahoo.co.uk). */

#if !defined (COFF_PE_READ_H)
#define COFF_PE_READ_H

struct objfile;

/* Read the export table and convert it to minimal symbol table entries */
extern void read_pe_exported_syms (struct objfile *objfile);

#endif /* !defined (COFF_PE_READ_H) */
