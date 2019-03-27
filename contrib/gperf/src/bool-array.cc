/* Fast lookup table abstraction implemented as an Iteration Number Array
   Copyright (C) 1989-1998, 2002 Free Software Foundation, Inc.
   Written by Douglas C. Schmidt <schmidt@ics.uci.edu>
   and Bruno Haible <bruno@clisp.org>.

   This file is part of GNU GPERF.

   GNU GPERF is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GNU GPERF is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.
   If not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

/* Specification. */
#include "bool-array.h"

#include <stdio.h>
#include <string.h>
#include "options.h"

/* Frees this object.  */
Bool_Array::~Bool_Array ()
{
  /* Print out debugging diagnostics. */
  if (option[DEBUG])
    fprintf (stderr, "\ndumping boolean array information\n"
             "size = %d\niteration number = %d\nend of array dump\n",
             _size, _iteration_number);
  delete[] const_cast<unsigned int *>(_storage_array);
}

#ifndef __OPTIMIZE__

#define INLINE /* not inline */
#include "bool-array.icc"
#undef INLINE

#endif /* not defined __OPTIMIZE__ */
