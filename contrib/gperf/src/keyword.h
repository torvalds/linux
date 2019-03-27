/* This may look like C code, but it is really -*- C++ -*- */

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

#ifndef keyword_h
#define keyword_h 1

/* Class defined in "positions.h".  */
class Positions;

/* An instance of this class is a keyword, as specified in the input file.  */

struct Keyword
{
  /* Constructor.  */
                        Keyword (const char *allchars, int allchars_length,
                                 const char *rest);

  /* Data members defined immediately by the input file.  */
  /* The keyword as a string, possibly containing NUL bytes.  */
  const char *const     _allchars;
  int const             _allchars_length;
  /* Additional stuff seen on the same line of the input file.  */
  const char *const     _rest;
  /* Line number of this keyword in the input file.  */
  unsigned int          _lineno;
};

/* A keyword, in the context of a given keyposition list.  */

struct KeywordExt : public Keyword
{
  /* Constructor.  */
                        KeywordExt (const char *allchars, int allchars_length,
                                    const char *rest);

  /* Data members depending on the keyposition list.  */
  /* The selected characters that participate for the hash function,
     selected according to the keyposition list, as a canonically reordered
     multiset.  */
  const unsigned int *  _selchars;
  int                   _selchars_length;
  /* Chained list of keywords having the same _selchars and
     - if !option[NOLENGTH] - also the same _allchars_length.
     Note that these duplicates are not members of the main keyword list.  */
  KeywordExt *          _duplicate_link;

  /* Methods depending on the keyposition list.  */
  /* Initializes selchars and selchars_length, without reordering.  */
  void                  init_selchars_tuple (const Positions& positions, const unsigned int *alpha_unify);
  /* Initializes selchars and selchars_length, with reordering.  */
  void                  init_selchars_multiset (const Positions& positions, const unsigned int *alpha_unify, const unsigned int *alpha_inc);
  /* Deletes selchars.  */
  void                  delete_selchars ();

  /* Data members used by the algorithm.  */
  int                   _hash_value; /* Hash value for the keyword.  */

  /* Data members used by the output routines.  */
  int                   _final_index;

private:
  unsigned int *        init_selchars_low (const Positions& positions, const unsigned int *alpha_unify, const unsigned int *alpha_inc);
};

/* An abstract factory for creating Keyword instances.
   This factory is used to make the Input class independent of the concrete
   class KeywordExt.  */

class Keyword_Factory
{
public:
  /* Constructor.  */
                        Keyword_Factory ();
  /* Destructor.  */
  virtual               ~Keyword_Factory ();

  /* Creates a new Keyword.  */
  virtual /*abstract*/ Keyword *
                        create_keyword (const char *allchars, int allchars_length,
                                        const char *rest) = 0;
};

/* A statically allocated empty string.  */
extern char empty_string[1];

#ifdef __OPTIMIZE__

#define INLINE inline
#include "keyword.icc"
#undef INLINE

#endif

#endif
