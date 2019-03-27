/* A set of byte positions.
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
#include "positions.h"

#include <stdio.h>
#include <stdlib.h> /* declares exit() */
#include <string.h>

/* ---------------------------- Class Positions ---------------------------- */

/* Set operations.  Assumes the array is in reverse order.  */

bool
Positions::contains (int pos) const
{
  unsigned int count = _size;
  const int *p = _positions + _size - 1;

  for (; count > 0; p--, count--)
    {
      if (*p == pos)
        return true;
      if (*p > pos)
        break;
    }
  return false;
}

void
Positions::add (int pos)
{
  set_useall (false);

  unsigned int count = _size;

  if (count == MAX_SIZE)
    {
      fprintf (stderr, "Positions::add internal error: overflow\n");
      exit (1);
    }

  int *p = _positions + _size - 1;

  for (; count > 0; p--, count--)
    {
      if (*p == pos)
        {
          fprintf (stderr, "Positions::add internal error: duplicate\n");
          exit (1);
        }
      if (*p > pos)
        break;
      p[1] = p[0];
    }
  p[1] = pos;
  _size++;
}

void
Positions::remove (int pos)
{
  set_useall (false);

  unsigned int count = _size;
  if (count > 0)
    {
      int *p = _positions + _size - 1;

      if (*p == pos)
        {
          _size--;
          return;
        }
      if (*p < pos)
        {
          int prev = *p;

          for (;;)
            {
              p--;
              count--;
              if (count == 0)
                break;
              if (*p == pos)
                {
                  *p = prev;
                  _size--;
                  return;
                }
              if (*p > pos)
                break;
              int curr = *p;
              *p = prev;
              prev = curr;
            }
        }
    }
  fprintf (stderr, "Positions::remove internal error: not found\n");
  exit (1);
}

/* Output in external syntax.  */
void
Positions::print () const
{
  if (_useall)
    printf ("*");
  else
    {
      bool first = true;
      bool seen_LASTCHAR = false;
      unsigned int count = _size;
      const int *p = _positions + _size - 1;

      for (; count > 0; p--)
        {
          count--;
          if (*p == LASTCHAR)
            seen_LASTCHAR = true;
          else
            {
              if (!first)
                printf (",");
              printf ("%d", *p + 1);
              if (count > 0 && p[-1] == *p + 1)
                {
                  printf ("-");
                  do
                    {
                      p--;
                      count--;
                    }
                  while (count > 0 && p[-1] == *p + 1);
                  printf ("%d", *p + 1);
                }
              first = false;
            }
        }
      if (seen_LASTCHAR)
        {
          if (!first)
            printf (",");
          printf ("$");
        }
    }
}

/* ------------------------------------------------------------------------- */

#ifndef __OPTIMIZE__

#define INLINE /* not inline */
#include "positions.icc"
#undef INLINE

#endif /* not defined __OPTIMIZE__ */
