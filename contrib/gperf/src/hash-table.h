/* This may look like C code, but it is really -*- C++ -*- */

/* Hash table used to check for duplicate keyword entries.

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

#ifndef hash_table_h
#define hash_table_h 1

#include "keyword.h"

/* Hash table of KeywordExt* entries.
   Two entries are considered equal if their _selchars are the same and
   - if !ignore_length - if their _allchars_length are the same.  */

class Hash_Table
{
public:
  /* Constructor.
     size is the maximum number of entries.
     ignore_length determines a detail in the comparison function.  */
                        Hash_Table (unsigned int size, bool ignore_length);
  /* Destructor.  */
                        ~Hash_Table ();
  /* Attempts to insert ITEM in the table.  If there is already an equal
     entry in it, returns it.  Otherwise inserts ITEM and returns NULL.  */
  KeywordExt *          insert (KeywordExt *item);
  /* Print the table's contents.  */
  void                  dump () const;

private:
  /* Vector of entries.  */
  KeywordExt **         _table;
  /* Size of the vector.  */
  unsigned int          _size;
  /* log2(_size).  */
  unsigned int          _log_size;
  /* A detail of the comparison function.  */
  bool const            _ignore_length;
  /* Statistics: Number of collisions so far.  */
  unsigned int          _collisions;

  /* Compares two items.  */
  bool                  equal (KeywordExt *item1, KeywordExt *item2) const;
};

#endif
