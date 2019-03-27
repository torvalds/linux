/* getline.c -- Replacement for GNU C library function getline

Copyright (C) 1993, 1996, 2001-2002 Free Software Foundation, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
USA. */

/* Written by Jan Brittenson, bson@gnu.ai.mit.edu.  */

/* Specification.  */
#include "getline.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Always add at least this many bytes when extending the buffer.  */
#define MIN_CHUNK 64

/* Reads up to (and including) a TERMINATOR from STREAM into *LINEPTR + OFFSET
   (and null-terminate it). *LINEPTR is a pointer returned from new [] (or
   NULL), pointing to *N characters of space.  It is realloc'd as
   necessary.  Returns the number of characters read (not including the
   null terminator), or -1 on error or immediate EOF.
   NOTE: There is another getstr() function declared in <curses.h>.  */

static int
getstr (char **lineptr, size_t *n, FILE *stream, char terminator, size_t offset)
{
  size_t nchars_avail;          /* Allocated but unused chars in *LINEPTR.  */
  char *read_pos;               /* Where we're reading into *LINEPTR. */

  if (!lineptr || !n || !stream)
    return -1;

  if (!*lineptr)
    {
      *n = MIN_CHUNK;
      *lineptr = new char[*n];
    }

  nchars_avail = *n - offset;
  read_pos = *lineptr + offset;

  for (;;)
    {
      int c = getc (stream);

      /* We always want at least one char left in the buffer, since we
         always (unless we get an error while reading the first char)
         NUL-terminate the line buffer.  */

      assert (*n - nchars_avail == (size_t) (read_pos - *lineptr));
      if (nchars_avail < 2)
        {
          if (*n > MIN_CHUNK)
            *n *= 2;
          else
            *n += MIN_CHUNK;

          nchars_avail = *n + *lineptr - read_pos;
          char *new_line = new char[*n];
          if (*lineptr)
            {
              memcpy (new_line, *lineptr, read_pos - *lineptr);
              delete[] *lineptr;
            }
          *lineptr = new_line;
          read_pos = *n - nchars_avail + *lineptr;
          assert (*n - nchars_avail == (size_t) (read_pos - *lineptr));
        }

      if (c == EOF || ferror (stream))
        {
          /* Return partial line, if any.  */
          if (read_pos == *lineptr)
            return -1;
          else
            break;
        }

      *read_pos++ = c;
      nchars_avail--;

      if (c == terminator)
        /* Return the line.  */
        break;
    }

  /* Done - NUL terminate and return the number of chars read.  */
  *read_pos = '\0';

  return read_pos - (*lineptr + offset);
}

int
get_line (char **lineptr, size_t *n, FILE *stream)
{
  return getstr (lineptr, n, stream, '\n', 0);
}

int
get_delim (char **lineptr, size_t *n, int delimiter, FILE *stream)
{
  return getstr (lineptr, n, stream, delimiter, 0);
}
