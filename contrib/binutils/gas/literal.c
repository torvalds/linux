/* literal.c - GAS literal pool management.
   Copyright 1994, 2000, 2005 Free Software Foundation, Inc.
   Written by Ken Raeburn (raeburn@cygnus.com).

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
   along with GAS; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/* This isn't quite a "constant" pool.  Some of the values may get
   adjusted at run time, e.g., for symbolic relocations when shared
   libraries are in use.  It's more of a "literal" pool.

   On the Alpha, this should be used for .lita and .lit8.  (Is there
   ever a .lit4?)  On the MIPS, it could be used for .lit4 as well.

   The expressions passed here should contain either constants or symbols,
   not a combination of both.  Typically, the constant pool is accessed
   with some sort of GP register, so the size of the pool must be kept down
   if possible.  The exception is section offsets -- if you're storing a
   pointer to the start of .data, for example, and your machine provides
   for 16-bit signed addends, you might want to store .data+32K, so that
   you can access all of the first 64K of .data with the one pointer.

   This isn't a requirement, just a guideline that can help keep .o file
   size down.  */

#include "as.h"
#include "subsegs.h"

#ifdef NEED_LITERAL_POOL

valueT
add_to_literal_pool (sym, addend, sec, size)
     symbolS *sym;
     valueT addend;
     segT sec;
     int size;
{
  segT current_section = now_seg;
  int current_subsec = now_subseg;
  valueT offset;
  bfd_reloc_code_real_type reloc_type;
  char *p;
  segment_info_type *seginfo = seg_info (sec);
  fixS *fixp;

  offset = 0;
  /* @@ This assumes all entries in a given section will be of the same
     size...  Probably correct, but unwise to rely on.  */
  /* This must always be called with the same subsegment.  */
  if (seginfo->frchainP)
    for (fixp = seginfo->frchainP->fix_root;
	 fixp != (fixS *) NULL;
	 fixp = fixp->fx_next, offset += size)
      {
	if (fixp->fx_addsy == sym && fixp->fx_offset == addend)
	  return offset;
      }

  subseg_set (sec, 0);
  p = frag_more (size);
  memset (p, 0, size);

  switch (size)
    {
    case 4:
      reloc_type = BFD_RELOC_32;
      break;
    case 8:
      reloc_type = BFD_RELOC_64;
      break;
    default:
      abort ();
    }
  fix_new (frag_now, p - frag_now->fr_literal, size, sym, addend, 0,
	   reloc_type);

  subseg_set (current_section, current_subsec);
  offset = seginfo->literal_pool_size;
  seginfo->literal_pool_size += size;
  return offset;
}
#endif
