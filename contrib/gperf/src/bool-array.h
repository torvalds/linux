/* This may look like C code, but it is really -*- C++ -*- */

/* Simple lookup table abstraction implemented as an Iteration Number Array.

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

#ifndef bool_array_h
#define bool_array_h 1

/* A Bool_Array instance is a bit array of fixed size, optimized for being
   filled sparsely and cleared frequently.  For example, when processing
   tests/chill.gperf, the array will be:
     - of size 15391,
     - clear will be called 3509 times,
     - set_bit will be called 300394 times.
   With a conventional bit array implementation, clear would be too slow.
   With a tree/hash based bit array implementation, set_bit would be slower. */

class Bool_Array
{
public:
  /* Initializes the bit array with room for SIZE bits, numbered from
     0 to SIZE-1. */
                        Bool_Array (unsigned int size);

  /* Frees this object.  */
                        ~Bool_Array ();

  /* Resets all bits to zero.  */
  void                  clear ();

  /* Sets the specified bit to true.
     Returns its previous value (false or true).  */
  bool                  set_bit (unsigned int index);

private:
  /* Size of array.  */
  unsigned int const    _size;

  /* Current iteration number.  Always nonzero.  Starts out as 1, and is
     incremented each time clear() is called.  */
  unsigned int          _iteration_number;

  /* For each index, we store in storage_array[index] the iteration_number at
     the time set_bit(index) was last called.  */
  unsigned int * const  _storage_array;
};

#ifdef __OPTIMIZE__  /* efficiency hack! */

#include <stdio.h>
#include <string.h>
#include "options.h"
#define INLINE inline
#include "bool-array.icc"
#undef INLINE

#endif

#endif
