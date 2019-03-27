/* This may look like C code, but it is really -*- C++ -*- */

/* A set of byte positions.

   Copyright (C) 1989-1998, 2000, 2002, 2005 Free Software Foundation, Inc.
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

#ifndef positions_h
#define positions_h 1

/* Classes defined below.  */
class PositionIterator;
class PositionReverseIterator;

/* This class denotes a set of byte positions, used to access a keyword.  */

class Positions
{
  friend class PositionIterator;
  friend class PositionReverseIterator;
public:
  /* Denotes the last char of a keyword, depending on the keyword's length.  */
  enum {                LASTCHAR = -1 };

  /* Maximum key position specifiable by the user, 1-based.
     Note that MAX_KEY_POS-1 must fit into the element type of _positions[],
     below.  */
  enum {                MAX_KEY_POS = 255 };

  /* Maximum possible size.  Since duplicates are eliminated and the possible
     0-based positions are -1 .. MAX_KEY_POS-1, this is:  */
  enum {                MAX_SIZE = MAX_KEY_POS + 1 };

  /* Constructors.  */
                        Positions ();
                        Positions (int pos1);
                        Positions (int pos1, int pos2);

  /* Copy constructor.  */
                        Positions (const Positions& src);

  /* Assignment operator.  */
  Positions&            operator= (const Positions& src);

  /* Accessors.  */
  bool                  is_useall () const;
  int                   operator[] (unsigned int index) const;
  unsigned int          get_size () const;

  /* Write access.  */
  void                  set_useall (bool useall);
  int *                 pointer ();
  void                  set_size (unsigned int size);

  /* Sorts the array in reverse order.
     Returns true if there are no duplicates, false otherwise.  */
  bool                  sort ();

  /* Creates an iterator, returning the positions in descending order.  */
  PositionIterator      iterator () const;
  /* Creates an iterator, returning the positions in descending order,
     that apply to strings of length <= maxlen.  */
  PositionIterator      iterator (int maxlen) const;
  /* Creates an iterator, returning the positions in ascending order.  */
  PositionReverseIterator reviterator () const;
  /* Creates an iterator, returning the positions in ascending order,
     that apply to strings of length <= maxlen.  */
  PositionReverseIterator reviterator (int maxlen) const;

  /* Set operations.  Assumes the array is in reverse order.  */
  bool                  contains (int pos) const;
  void                  add (int pos);
  void                  remove (int pos);

  /* Output in external syntax.  */
  void                  print () const;

private:
  /* The special case denoted by '*'.  */
  bool                  _useall;
  /* Number of positions.  */
  unsigned int          _size;
  /* Array of positions.  0 for the first char, 1 for the second char etc.,
     LASTCHAR for the last char.  */
  int                   _positions[MAX_SIZE];
};

/* This class denotes an iterator through a set of byte positions.  */

class PositionIterator
{
  friend class Positions;
public:
  /* Copy constructor.  */
                        PositionIterator (const PositionIterator& src);

  /* End of iteration marker.  */
  enum {                EOS = -2 };

  /* Retrieves the next position, or EOS past the end.  */
  int                   next ();

  /* Returns the number of remaining positions, i.e. how often next() will
     return a value != EOS.  */
  unsigned int          remaining () const;

private:
  /* Initializes an iterator through POSITIONS.  */
                        PositionIterator (Positions const& positions);
  /* Initializes an iterator through POSITIONS, ignoring positions >= maxlen.  */
                        PositionIterator (Positions const& positions, int maxlen);

  const Positions&      _set;
  unsigned int          _index;
};

/* This class denotes an iterator in reverse direction through a set of
   byte positions.  */

class PositionReverseIterator
{
  friend class Positions;
public:
  /* Copy constructor.  */
                        PositionReverseIterator (const PositionReverseIterator& src);

  /* End of iteration marker.  */
  enum {                EOS = -2 };

  /* Retrieves the next position, or EOS past the end.  */
  int                   next ();

  /* Returns the number of remaining positions, i.e. how often next() will
     return a value != EOS.  */
  unsigned int          remaining () const;

private:
  /* Initializes an iterator through POSITIONS.  */
                        PositionReverseIterator (Positions const& positions);
  /* Initializes an iterator through POSITIONS, ignoring positions >= maxlen.  */
                        PositionReverseIterator (Positions const& positions, int maxlen);

  const Positions&      _set;
  unsigned int          _index;
  unsigned int          _minindex;
};

#ifdef __OPTIMIZE__

#include <string.h>
#define INLINE inline
#include "positions.icc"
#undef INLINE

#endif

#endif
