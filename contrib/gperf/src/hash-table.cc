/* Hash table for checking keyword links.  Implemented using double hashing.
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
#include "hash-table.h"

#include <stdio.h>
#include <string.h> /* declares memset(), strcmp() */
#include <hash.h>
#include "options.h"

/* We use a hash table with double hashing.  This is the simplest kind of
   hash table, given that we always only insert and never remove entries
   from the hash table.  */

/* To make double hashing efficient, there need to be enough spare entries.  */
static const int size_factor = 10;

/* We make the size of the hash table a power of 2.  This allows for two
   optimizations: It eliminates the modulo instruction, and allows for an
   easy secondary hashing function.  */

/* Constructor.  */
Hash_Table::Hash_Table (unsigned int size, bool ignore_length)
  : _ignore_length (ignore_length),
    _collisions (0)
{
  /* There need to be enough spare entries.  */
  size = size * size_factor;

  /* Find smallest power of 2 that is >= size.  */
  unsigned int shift = 0;
  if ((size >> 16) > 0)
    {
      size = size >> 16;
      shift += 16;
    }
  if ((size >> 8) > 0)
    {
      size = size >> 8;
      shift += 8;
    }
  if ((size >> 4) > 0)
    {
      size = size >> 4;
      shift += 4;
    }
  if ((size >> 2) > 0)
    {
      size = size >> 2;
      shift += 2;
    }
  if ((size >> 1) > 0)
    {
      size = size >> 1;
      shift += 1;
    }
  _log_size = shift;
  _size = 1 << shift;

  /* Allocate table.  */
  _table = new KeywordExt*[_size];
  memset (_table, 0, _size * sizeof (*_table));
}

/* Destructor.  */
Hash_Table::~Hash_Table ()
{
  delete[] _table;
}

/* Print the table's contents.  */
void
Hash_Table::dump () const
{
  int field_width;

  field_width = 0;
  {
    for (int i = _size - 1; i >= 0; i--)
      if (_table[i])
        if (field_width < _table[i]->_selchars_length)
          field_width = _table[i]->_selchars_length;
  }

  fprintf (stderr,
           "\ndumping the hash table\n"
           "total available table slots = %d, total bytes = %d, total collisions = %d\n"
           "location, %*s, keyword\n",
           _size, _size * static_cast<unsigned int>(sizeof (*_table)),
           _collisions, field_width, "keysig");

  for (int i = _size - 1; i >= 0; i--)
    if (_table[i])
      {
        fprintf (stderr, "%8d, ", i);
        if (field_width > _table[i]->_selchars_length)
          fprintf (stderr, "%*s", field_width - _table[i]->_selchars_length, "");
        for (int j = 0; j < _table[i]->_selchars_length; j++)
          putc (_table[i]->_selchars[j], stderr);
        fprintf (stderr, ", %.*s\n",
                 _table[i]->_allchars_length, _table[i]->_allchars);
      }

  fprintf (stderr, "\nend dumping hash table\n\n");
}

/* Compares two items.  */
inline bool
Hash_Table::equal (KeywordExt *item1, KeywordExt *item2) const
{
  return item1->_selchars_length == item2->_selchars_length
         && memcmp (item1->_selchars, item2->_selchars,
                    item2->_selchars_length * sizeof (unsigned int))
            == 0
         && (_ignore_length
             || item1->_allchars_length == item2->_allchars_length);
}

/* Attempts to insert ITEM in the table.  If there is already an equal
   entry in it, returns it.  Otherwise inserts ITEM and returns NULL.  */
KeywordExt *
Hash_Table::insert (KeywordExt *item)
{
  unsigned hash_val =
    hashpjw (reinterpret_cast<const unsigned char *>(item->_selchars),
             item->_selchars_length * sizeof (unsigned int));
  unsigned int probe = hash_val & (_size - 1);
  unsigned int increment =
    (((hash_val >> _log_size)
      ^ (_ignore_length ? 0 : item->_allchars_length))
     << 1) + 1;
  /* Note that because _size is a power of 2 and increment is odd,
     we have gcd(increment,_size) = 1, which guarantees that we'll find
     an empty entry during the loop.  */

  while (_table[probe] != NULL)
    {
      if (equal (_table[probe], item))
        return _table[probe];

      _collisions++;
      probe = (probe + increment) & (_size - 1);
    }

  _table[probe] = item;
  return NULL;
}
