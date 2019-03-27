/* Register protocol definition structures for the GNU Debugger
   Copyright 2001, 2002 Free Software Foundation, Inc.

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

#ifndef REGDEF_H
#define REGDEF_H

struct reg
{
  /* The name of this register - NULL for pad entries.  */
  const char *name;

  /* At the moment, both of the following bit counts must be divisible
     by eight (to match the representation as two hex digits) and divisible
     by the size of a byte (to match the layout of each register in
     memory).  */

  /* The offset (in bits) of the value of this register in the buffer.  */
  int offset;

  /* The size (in bits) of the value of this register, as transmitted.  */
  int size;
};

/* Set the current remote protocol and register cache according to the array
   ``regs'', with ``n'' elements.  */

void set_register_cache (struct reg *regs, int n);

#endif /* REGDEF_H */
