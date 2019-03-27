/* Keyword data.
   Copyright (C) 1989-1998, 2000, 2002 Free Software Foundation, Inc.
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
#include "keyword.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "positions.h"


/* --------------------------- KeywordExt class --------------------------- */

/* Sort a small set of 'unsigned int', base[0..len-1], in place.  */
static inline void sort_char_set (unsigned int *base, int len)
{
  /* Bubble sort is sufficient here.  */
  for (int i = 1; i < len; i++)
    {
      int j;
      unsigned int tmp;

      for (j = i, tmp = base[j]; j > 0 && tmp < base[j - 1]; j--)
        base[j] = base[j - 1];

      base[j] = tmp;
    }
}

/* Initializes selchars and selchars_length.

   General idea:
     The hash function will be computed as
         asso_values[allchars[key_pos[0]]] +
         asso_values[allchars[key_pos[1]]] + ...
     We compute selchars as the multiset
         { allchars[key_pos[0]], allchars[key_pos[1]], ... }
     so that the hash function becomes
         asso_values[selchars[0]] + asso_values[selchars[1]] + ...
   Furthermore we sort the selchars array, to ease detection of duplicates
   later.

   More in detail: The arguments alpha_unify (used for case-insensitive
   hash functions) and alpha_inc (used to disambiguate permutations)
   apply slight modifications. The hash function will be computed as
       sum (j=0,1,...: k = key_pos[j]:
            asso_values[alpha_unify[allchars[k]+alpha_inc[k]]])
       + (allchars_length if !option[NOLENGTH], 0 otherwise).
   We compute selchars as the multiset
       { alpha_unify[allchars[k]+alpha_inc[k]] : j=0,1,..., k = key_pos[j] }
   so that the hash function becomes
       asso_values[selchars[0]] + asso_values[selchars[1]] + ...
       + (allchars_length if !option[NOLENGTH], 0 otherwise).
 */

unsigned int *
KeywordExt::init_selchars_low (const Positions& positions, const unsigned int *alpha_unify, const unsigned int *alpha_inc)
{
  /* Iterate through the list of positions, initializing selchars
     (via ptr).  */
  PositionIterator iter = positions.iterator(_allchars_length);

  unsigned int *key_set = new unsigned int[iter.remaining()];
  unsigned int *ptr = key_set;

  for (int i; (i = iter.next ()) != PositionIterator::EOS; )
    {
      unsigned int c;
      if (i == Positions::LASTCHAR)
        /* Special notation for last KEY position, i.e. '$'.  */
        c = static_cast<unsigned char>(_allchars[_allchars_length - 1]);
      else if (i < _allchars_length)
        {
          /* Within range of KEY length, so we'll keep it.  */
          c = static_cast<unsigned char>(_allchars[i]);
          if (alpha_inc)
            c += alpha_inc[i];
        }
      else
        /* Out of range of KEY length, the iterator should not have
           produced this.  */
        abort ();
      if (alpha_unify)
        c = alpha_unify[c];
      *ptr = c;
      ptr++;
    }

  _selchars = key_set;
  _selchars_length = ptr - key_set;

  return key_set;
}

void
KeywordExt::init_selchars_tuple (const Positions& positions, const unsigned int *alpha_unify)
{
  init_selchars_low (positions, alpha_unify, NULL);
}

void
KeywordExt::init_selchars_multiset (const Positions& positions, const unsigned int *alpha_unify, const unsigned int *alpha_inc)
{
  unsigned int *selchars =
    init_selchars_low (positions, alpha_unify, alpha_inc);

  /* Sort the selchars elements alphabetically.  */
  sort_char_set (selchars, _selchars_length);
}

/* Deletes selchars.  */
void
KeywordExt::delete_selchars ()
{
  delete[] const_cast<unsigned int *>(_selchars);
}


/* ------------------------- Keyword_Factory class ------------------------- */

Keyword_Factory::Keyword_Factory ()
{
}

Keyword_Factory::~Keyword_Factory ()
{
}


/* ------------------------------------------------------------------------- */

char empty_string[1] = "";


#ifndef __OPTIMIZE__

#define INLINE /* not inline */
#include "keyword.icc"
#undef INLINE

#endif /* not defined __OPTIMIZE__ */
