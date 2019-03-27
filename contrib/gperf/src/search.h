/* This may look like C code, but it is really -*- C++ -*- */

/* Search algorithm.

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

#ifndef search_h
#define search_h 1

#include "keyword-list.h"
#include "positions.h"
#include "bool-array.h"

struct EquivalenceClass;

class Search
{
public:
                        Search (KeywordExt_List *list);
                        ~Search ();
  void                  optimize ();
private:
  void                  prepare ();

  /* Computes the upper bound on the indices passed to asso_values[],
     assuming no alpha_increments.  */
  unsigned int          compute_alpha_size () const;

  /* Computes the unification rules between different asso_values[c],
     assuming no alpha_increments.  */
  unsigned int *        compute_alpha_unify () const;

  /* Initializes each keyword's _selchars array.  */
  void                  init_selchars_tuple (const Positions& positions, const unsigned int *alpha_unify) const;
  /* Deletes each keyword's _selchars array.  */
  void                  delete_selchars () const;

  /* Count the duplicate keywords that occur with a given set of positions.  */
  unsigned int          count_duplicates_tuple (const Positions& positions, const unsigned int *alpha_unify) const;

  /* Find good key positions.  */
  void                  find_positions ();

  /* Count the duplicate keywords that occur with the found set of positions.  */
  unsigned int          count_duplicates_tuple () const;

  /* Computes the upper bound on the indices passed to asso_values[].  */
  unsigned int          compute_alpha_size (const unsigned int *alpha_inc) const;

  /* Computes the unification rules between different asso_values[c].  */
  unsigned int *        compute_alpha_unify (const Positions& positions, const unsigned int *alpha_inc) const;

  /* Initializes each keyword's _selchars array.  */
  void                  init_selchars_multiset (const Positions& positions, const unsigned int *alpha_unify, const unsigned int *alpha_inc) const;

  /* Count the duplicate keywords that occur with the given set of positions
     and a given alpha_inc[] array.  */
  unsigned int          count_duplicates_multiset (const unsigned int *alpha_inc) const;

  /* Find good _alpha_inc[].  */
  void                  find_alpha_inc ();

  /* Initializes the asso_values[] related parameters.  */
  void                  prepare_asso_values ();

  EquivalenceClass *    compute_partition (bool *undetermined) const;

  unsigned int          count_possible_collisions (EquivalenceClass *partition, unsigned int c) const;

  bool                  unchanged_partition (EquivalenceClass *partition, unsigned int c) const;

  /* Finds some _asso_values[] that fit.  */
  void                  find_asso_values ();

  /* Computes a keyword's hash value, relative to the current _asso_values[],
     and stores it in keyword->_hash_value.  */
  int                   compute_hash (KeywordExt *keyword) const;

  /* Finds good _asso_values[].  */
  void                  find_good_asso_values ();

  /* Sorts the keyword list by hash value.  */
  void                  sort ();

public:

  /* Linked list of keywords.  */
  KeywordExt_List *     _head;

  /* Total number of keywords, counting duplicates.  */
  int                   _total_keys;

  /* Maximum length of the longest keyword.  */
  int                   _max_key_len;

  /* Minimum length of the shortest keyword.  */
  int                   _min_key_len;

  /* User-specified or computed key positions.  */
  Positions             _key_positions;

  /* Adjustments to add to bytes add specific key positions.  */
  unsigned int *        _alpha_inc;

  /* Size of alphabet.  */
  unsigned int          _alpha_size;

  /* Alphabet character unification, either the identity or a mapping from
     upper case characters to lower case characters (and maybe more).  */
  unsigned int *        _alpha_unify;

  /* Maximum _selchars_length over all keywords.  */
  unsigned int          _max_selchars_length;

  /* Total number of duplicates that have been moved to _duplicate_link lists
     (not counting their representatives which stay on the main list).  */
  int                   _total_duplicates;

  /* Counts occurrences of each key set character.
     _occurrences[c] is the number of times that c occurs among the _selchars
     of a keyword.  */
  int *                 _occurrences;
  /* Value associated with each character. */
  int *                 _asso_values;

private:

  /* Length of _head list.  Number of keywords, not counting duplicates.  */
  int                   _list_len;

  /* Exclusive upper bound for every _asso_values[c].  A power of 2.  */
  unsigned int          _asso_value_max;

  /* Initial value for asso_values table.  -1 means random.  */
  int                   _initial_asso_value;
  /* Jump length when trying alternative values.  0 means random.  */
  int                   _jump;

  /* Maximal possible hash value.  */
  int                   _max_hash_value;

  /* Sparse bit vector for collision detection.  */
  Bool_Array *          _collision_detector;
};

#endif
