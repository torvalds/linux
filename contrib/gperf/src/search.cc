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

/* Specification. */
#include "search.h"

#include <stdio.h>
#include <stdlib.h> /* declares exit(), rand(), srand() */
#include <string.h> /* declares memset(), memcmp() */
#include <time.h> /* declares time() */
#include <math.h> /* declares exp() */
#include <limits.h> /* defines INT_MIN, INT_MAX, UINT_MAX */
#include "options.h"
#include "hash-table.h"
#include "config.h"

/* ============================== Portability ============================== */

/* Assume ISO C++ 'for' scoping rule.  */
/* This code is used to work around scoping issues with visual studio 6 from
 * 1998.  Comment it out here to queisce numerous -Wdangling-else warnings
 * from clang.
#define for if (0) ; else for */

/* Dynamically allocated array with dynamic extent:

   Example:
       DYNAMIC_ARRAY (my_array, int, n);
       ...
       FREE_DYNAMIC_ARRAY (my_array);

   Attention: depending on your implementation my_array is either the array
   itself or a pointer to the array! Always use my_array only as expression!
 */
#if HAVE_DYNAMIC_ARRAY
  #define DYNAMIC_ARRAY(var,eltype,size) eltype var[size]
  #define FREE_DYNAMIC_ARRAY(var)
#else
  #define DYNAMIC_ARRAY(var,eltype,size) eltype *var = new eltype[size]
  #define FREE_DYNAMIC_ARRAY(var) delete[] var
#endif

/* ================================ Theory ================================= */

/* The general form of the hash function is

      hash (keyword) = sum (asso_values[keyword[i] + alpha_inc[i]] : i in Pos)
                       + len (keyword)

   where Pos is a set of byte positions,
   each alpha_inc[i] is a nonnegative integer,
   each asso_values[c] is a nonnegative integer,
   len (keyword) is the keyword's length if !option[NOLENGTH], or 0 otherwise.

   Theorem 1: If all keywords are different, there is a set Pos such that
   all tuples (keyword[i] : i in Pos) are different.

   Theorem 2: If all tuples (keyword[i] : i in Pos) are different, there
   are nonnegative integers alpha_inc[i] such that all multisets
   {keyword[i] + alpha_inc[i] : i in Pos} are different.

   Define selchars[keyword] := {keyword[i] + alpha_inc[i] : i in Pos}.

   Theorem 3: If all multisets selchars[keyword] are different, there are
   nonnegative integers asso_values[c] such that all hash values
   sum (asso_values[c] : c in selchars[keyword]) are different.

   Based on these three facts, we find the hash function in three steps:

   Step 1 (Finding good byte positions):
   Find a set Pos, as small as possible, such that all tuples
   (keyword[i] : i in Pos) are different.

   Step 2 (Finding good alpha increments):
   Find nonnegative integers alpha_inc[i], as many of them as possible being
   zero, and the others being as small as possible, such that all multisets
   {keyword[i] + alpha_inc[i] : i in Pos} are different.

   Step 3 (Finding good asso_values):
   Find asso_values[c] such that all hash (keyword) are different.

   In other words, each step finds a projection that is injective on the
   given finite set:
     proj1 : String --> Map (Pos --> N)
     proj2 : Map (Pos --> N) --> Map (Pos --> N) / S(Pos)
     proj3 : Map (Pos --> N) / S(Pos) --> N
   where
     N denotes the set of nonnegative integers,
     Map (A --> B) := Hom_Set (A, B) is the set of maps from A to B, and
     S(Pos) is the symmetric group over Pos.

   This was the theory for option[NOLENGTH]; if !option[NOLENGTH], slight
   modifications apply:
     proj1 : String --> Map (Pos --> N) x N
     proj2 : Map (Pos --> N) x N --> Map (Pos --> N) / S(Pos) x N
     proj3 : Map (Pos --> N) / S(Pos) x N --> N

   For a case-insensitive hash function, the general form is

      hash (keyword) =
        sum (asso_values[alpha_unify[keyword[i] + alpha_inc[i]]] : i in Pos)
        + len (keyword)

   where alpha_unify[c] is chosen so that an upper/lower case change in
   keyword[i] doesn't change  alpha_unify[keyword[i] + alpha_inc[i]].
 */

/* ==================== Initialization and Preparation ===================== */

Search::Search (KeywordExt_List *list)
  : _head (list)
{
}

void
Search::prepare ()
{
  /* Compute the total number of keywords.  */
  _total_keys = 0;
  for (KeywordExt_List *temp = _head; temp; temp = temp->rest())
    _total_keys++;

  /* Compute the minimum and maximum keyword length.  */
  _max_key_len = INT_MIN;
  _min_key_len = INT_MAX;
  for (KeywordExt_List *temp = _head; temp; temp = temp->rest())
    {
      KeywordExt *keyword = temp->first();

      if (_max_key_len < keyword->_allchars_length)
        _max_key_len = keyword->_allchars_length;
      if (_min_key_len > keyword->_allchars_length)
        _min_key_len = keyword->_allchars_length;
    }

  /* Exit program if an empty string is used as keyword, since the comparison
     expressions don't work correctly for looking up an empty string.  */
  if (_min_key_len == 0)
    {
      fprintf (stderr, "Empty input keyword is not allowed.\n"
               "To recognize an empty input keyword, your code should check for\n"
               "len == 0 before calling the gperf generated lookup function.\n");
      exit (1);
    }

  /* Exit program if the characters in the keywords are not in the required
     range.  */
  if (option[SEVENBIT])
    for (KeywordExt_List *temp = _head; temp; temp = temp->rest())
      {
        KeywordExt *keyword = temp->first();

        const char *k = keyword->_allchars;
        for (int i = keyword->_allchars_length; i > 0; k++, i--)
          if (!(static_cast<unsigned char>(*k) < 128))
            {
              fprintf (stderr, "Option --seven-bit has been specified,\n"
                       "but keyword \"%.*s\" contains non-ASCII characters.\n"
                       "Try removing option --seven-bit.\n",
                       keyword->_allchars_length, keyword->_allchars);
              exit (1);
            }
      }
}

/* ====================== Finding good byte positions ====================== */

/* Computes the upper bound on the indices passed to asso_values[],
   assuming no alpha_increments.  */
unsigned int
Search::compute_alpha_size () const
{
  return (option[SEVENBIT] ? 128 : 256);
}

/* Computes the unification rules between different asso_values[c],
   assuming no alpha_increments.  */
unsigned int *
Search::compute_alpha_unify () const
{
  if (option[UPPERLOWER])
    {
      /* Uppercase to lowercase mapping.  */
      unsigned int alpha_size = compute_alpha_size();
      unsigned int *alpha_unify = new unsigned int[alpha_size];
      for (unsigned int c = 0; c < alpha_size; c++)
        alpha_unify[c] = c;
      for (unsigned int c = 'A'; c <= 'Z'; c++)
        alpha_unify[c] = c + ('a'-'A');
      return alpha_unify;
    }
  else
    /* Identity mapping.  */
    return NULL;
}

/* Initializes each keyword's _selchars array.  */
void
Search::init_selchars_tuple (const Positions& positions, const unsigned int *alpha_unify) const
{
  for (KeywordExt_List *temp = _head; temp; temp = temp->rest())
    temp->first()->init_selchars_tuple(positions, alpha_unify);
}

/* Deletes each keyword's _selchars array.  */
void
Search::delete_selchars () const
{
  for (KeywordExt_List *temp = _head; temp; temp = temp->rest())
    temp->first()->delete_selchars();
}

/* Count the duplicate keywords that occur with a given set of positions.
   In other words, it returns the difference
     # K - # proj1 (K)
   where K is the multiset of given keywords.  */
unsigned int
Search::count_duplicates_tuple (const Positions& positions, const unsigned int *alpha_unify) const
{
  /* Run through the keyword list and count the duplicates incrementally.
     The result does not depend on the order of the keyword list, thanks to
     the formula above.  */
  init_selchars_tuple (positions, alpha_unify);

  unsigned int count = 0;
  {
    Hash_Table representatives (_total_keys, option[NOLENGTH]);
    for (KeywordExt_List *temp = _head; temp; temp = temp->rest())
      {
        KeywordExt *keyword = temp->first();
        if (representatives.insert (keyword))
          count++;
      }
  }

  delete_selchars ();

  return count;
}

/* Find good key positions.  */

void
Search::find_positions ()
{
  /* If the user gave the key positions, we use them.  */
  if (option[POSITIONS])
    {
      _key_positions = option.get_key_positions();
      return;
    }

  /* Compute preliminary alpha_unify table.  */
  unsigned int *alpha_unify = compute_alpha_unify ();

  /* 1. Find positions that must occur in order to distinguish duplicates.  */
  Positions mandatory;

  if (!option[DUP])
    {
      for (KeywordExt_List *l1 = _head; l1 && l1->rest(); l1 = l1->rest())
        {
          KeywordExt *keyword1 = l1->first();
          for (KeywordExt_List *l2 = l1->rest(); l2; l2 = l2->rest())
            {
              KeywordExt *keyword2 = l2->first();

              /* If keyword1 and keyword2 have the same length and differ
                 in just one position, and it is not the last character,
                 this position is mandatory.  */
              if (keyword1->_allchars_length == keyword2->_allchars_length)
                {
                  int n = keyword1->_allchars_length;
                  int i;
                  for (i = 0; i < n - 1; i++)
                    {
                      unsigned char c1 = keyword1->_allchars[i];
                      unsigned char c2 = keyword2->_allchars[i];
                      if (option[UPPERLOWER])
                        {
                          if (c1 >= 'A' && c1 <= 'Z')
                            c1 += 'a' - 'A';
                          if (c2 >= 'A' && c2 <= 'Z')
                            c2 += 'a' - 'A';
                        }
                      if (c1 != c2)
                        break;
                    }
                  if (i < n - 1)
                    {
                      int j;
                      for (j = i + 1; j < n; j++)
                        {
                          unsigned char c1 = keyword1->_allchars[j];
                          unsigned char c2 = keyword2->_allchars[j];
                          if (option[UPPERLOWER])
                            {
                              if (c1 >= 'A' && c1 <= 'Z')
                                c1 += 'a' - 'A';
                              if (c2 >= 'A' && c2 <= 'Z')
                                c2 += 'a' - 'A';
                            }
                          if (c1 != c2)
                            break;
                        }
                      if (j >= n)
                        {
                          /* Position i is mandatory.  */
                          if (!mandatory.contains (i))
                            mandatory.add (i);
                        }
                    }
                }
            }
        }
    }

  /* 2. Add positions, as long as this decreases the duplicates count.  */
  int imax = (_max_key_len - 1 < Positions::MAX_KEY_POS - 1
              ? _max_key_len - 1 : Positions::MAX_KEY_POS - 1);
  Positions current = mandatory;
  unsigned int current_duplicates_count =
    count_duplicates_tuple (current, alpha_unify);
  for (;;)
    {
      Positions best;
      unsigned int best_duplicates_count = UINT_MAX;

      for (int i = imax; i >= -1; i--)
        if (!current.contains (i))
          {
            Positions tryal = current;
            tryal.add (i);
            unsigned int try_duplicates_count =
              count_duplicates_tuple (tryal, alpha_unify);

            /* We prefer 'try' to 'best' if it produces less duplicates,
               or if it produces the same number of duplicates but with
               a more efficient hash function.  */
            if (try_duplicates_count < best_duplicates_count
                || (try_duplicates_count == best_duplicates_count && i >= 0))
              {
                best = tryal;
                best_duplicates_count = try_duplicates_count;
              }
          }

      /* Stop adding positions when it gives no improvement.  */
      if (best_duplicates_count >= current_duplicates_count)
        break;

      current = best;
      current_duplicates_count = best_duplicates_count;
    }

  /* 3. Remove positions, as long as this doesn't increase the duplicates
     count.  */
  for (;;)
    {
      Positions best;
      unsigned int best_duplicates_count = UINT_MAX;

      for (int i = imax; i >= -1; i--)
        if (current.contains (i) && !mandatory.contains (i))
          {
            Positions tryal = current;
            tryal.remove (i);
            unsigned int try_duplicates_count =
              count_duplicates_tuple (tryal, alpha_unify);

            /* We prefer 'try' to 'best' if it produces less duplicates,
               or if it produces the same number of duplicates but with
               a more efficient hash function.  */
            if (try_duplicates_count < best_duplicates_count
                || (try_duplicates_count == best_duplicates_count && i == -1))
              {
                best = tryal;
                best_duplicates_count = try_duplicates_count;
              }
          }

      /* Stop removing positions when it gives no improvement.  */
      if (best_duplicates_count > current_duplicates_count)
        break;

      current = best;
      current_duplicates_count = best_duplicates_count;
    }

  /* 4. Replace two positions by one, as long as this doesn't increase the
     duplicates count.  */
  for (;;)
    {
      Positions best;
      unsigned int best_duplicates_count = UINT_MAX;

      for (int i1 = imax; i1 >= -1; i1--)
        if (current.contains (i1) && !mandatory.contains (i1))
          for (int i2 = imax; i2 >= -1; i2--)
            if (current.contains (i2) && !mandatory.contains (i2) && i2 != i1)
              for (int i3 = imax; i3 >= 0; i3--)
                if (!current.contains (i3))
                  {
                    Positions tryal = current;
                    tryal.remove (i1);
                    tryal.remove (i2);
                    tryal.add (i3);
                    unsigned int try_duplicates_count =
                      count_duplicates_tuple (tryal, alpha_unify);

                    /* We prefer 'try' to 'best' if it produces less duplicates,
                       or if it produces the same number of duplicates but with
                       a more efficient hash function.  */
                    if (try_duplicates_count < best_duplicates_count
                        || (try_duplicates_count == best_duplicates_count
                            && (i1 == -1 || i2 == -1 || i3 >= 0)))
                      {
                        best = tryal;
                        best_duplicates_count = try_duplicates_count;
                      }
                  }

      /* Stop removing positions when it gives no improvement.  */
      if (best_duplicates_count > current_duplicates_count)
        break;

      current = best;
      current_duplicates_count = best_duplicates_count;
    }

  /* That's it.  Hope it's good enough.  */
  _key_positions = current;

  if (option[DEBUG])
    {
      /* Print the result.  */
      fprintf (stderr, "\nComputed positions: ");
      PositionReverseIterator iter = _key_positions.reviterator();
      bool seen_lastchar = false;
      bool first = true;
      for (int i; (i = iter.next ()) != PositionReverseIterator::EOS; )
        {
          if (!first)
            fprintf (stderr, ", ");
          if (i == Positions::LASTCHAR)
            seen_lastchar = true;
          else
            {
              fprintf (stderr, "%d", i + 1);
              first = false;
            }
        }
      if (seen_lastchar)
        {
          if (!first)
            fprintf (stderr, ", ");
          fprintf (stderr, "$");
        }
      fprintf (stderr, "\n");
    }

  /* Free preliminary alpha_unify table.  */
  delete[] alpha_unify;
}

/* Count the duplicate keywords that occur with the found set of positions.
   In other words, it returns the difference
     # K - # proj1 (K)
   where K is the multiset of given keywords.  */
unsigned int
Search::count_duplicates_tuple () const
{
  unsigned int *alpha_unify = compute_alpha_unify ();
  unsigned int count = count_duplicates_tuple (_key_positions, alpha_unify);
  delete[] alpha_unify;
  return count;
}

/* ===================== Finding good alpha increments ===================== */

/* Computes the upper bound on the indices passed to asso_values[].  */
unsigned int
Search::compute_alpha_size (const unsigned int *alpha_inc) const
{
  unsigned int max_alpha_inc = 0;
  for (int i = 0; i < _max_key_len; i++)
    if (max_alpha_inc < alpha_inc[i])
      max_alpha_inc = alpha_inc[i];
  return (option[SEVENBIT] ? 128 : 256) + max_alpha_inc;
}

/* Computes the unification rules between different asso_values[c].  */
unsigned int *
Search::compute_alpha_unify (const Positions& positions, const unsigned int *alpha_inc) const
{
  if (option[UPPERLOWER])
    {
      /* Without alpha increments, we would simply unify
           'A' -> 'a', ..., 'Z' -> 'z'.
         But when a keyword contains at position i a character c,
         we have the constraint
            asso_values[tolower(c) + alpha_inc[i]] ==
            asso_values[toupper(c) + alpha_inc[i]].
         This introduces a unification
           toupper(c) + alpha_inc[i] -> tolower(c) + alpha_inc[i].
         Note that this unification can extend outside the range of
         ASCII letters!  But still every unified character pair is at
         a distance of 'a'-'A' = 32, or (after chained unification)
         at a multiple of 32.  So in the end the alpha_unify vector has
         the form    c -> c + 32 * f(c)   where f(c) is a nonnegative
         integer.  */
      unsigned int alpha_size = compute_alpha_size (alpha_inc);

      unsigned int *alpha_unify = new unsigned int[alpha_size];
      for (unsigned int c = 0; c < alpha_size; c++)
        alpha_unify[c] = c;

      for (KeywordExt_List *temp = _head; temp; temp = temp->rest())
        {
          KeywordExt *keyword = temp->first();

          /* Iterate through the selected character positions.  */
          PositionIterator iter = positions.iterator(keyword->_allchars_length);

          for (int i; (i = iter.next ()) != PositionIterator::EOS; )
            {
              unsigned int c;
              if (i == Positions::LASTCHAR)
                c = static_cast<unsigned char>(keyword->_allchars[keyword->_allchars_length - 1]);
              else if (i < keyword->_allchars_length)
                c = static_cast<unsigned char>(keyword->_allchars[i]);
              else
                abort ();
              if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
              if (c >= 'a' && c <= 'z')
                {
                  if (i != Positions::LASTCHAR)
                    c += alpha_inc[i];
                  /* Unify c with c - ('a'-'A').  */
                  unsigned int d = alpha_unify[c];
                  unsigned int b = c - ('a'-'A');
                  for (int a = b; a >= 0 && alpha_unify[a] == b; a -= ('a'-'A'))
                    alpha_unify[a] = d;
                }
            }
        }
      return alpha_unify;
    }
  else
    /* Identity mapping.  */
    return NULL;
}

/* Initializes each keyword's _selchars array.  */
void
Search::init_selchars_multiset (const Positions& positions, const unsigned int *alpha_unify, const unsigned int *alpha_inc) const
{
  for (KeywordExt_List *temp = _head; temp; temp = temp->rest())
    temp->first()->init_selchars_multiset(positions, alpha_unify, alpha_inc);
}

/* Count the duplicate keywords that occur with the given set of positions
   and a given alpha_inc[] array.
   In other words, it returns the difference
     # K - # proj2 (proj1 (K))
   where K is the multiset of given keywords.  */
unsigned int
Search::count_duplicates_multiset (const unsigned int *alpha_inc) const
{
  /* Run through the keyword list and count the duplicates incrementally.
     The result does not depend on the order of the keyword list, thanks to
     the formula above.  */
  unsigned int *alpha_unify = compute_alpha_unify (_key_positions, alpha_inc);
  init_selchars_multiset (_key_positions, alpha_unify, alpha_inc);

  unsigned int count = 0;
  {
    Hash_Table representatives (_total_keys, option[NOLENGTH]);
    for (KeywordExt_List *temp = _head; temp; temp = temp->rest())
      {
        KeywordExt *keyword = temp->first();
        if (representatives.insert (keyword))
          count++;
      }
  }

  delete_selchars ();
  delete[] alpha_unify;

  return count;
}

/* Find good _alpha_inc[].  */

void
Search::find_alpha_inc ()
{
  /* The goal is to choose _alpha_inc[] such that it doesn't introduce
     artificial duplicates.
     In other words, the goal is  # proj2 (proj1 (K)) = # proj1 (K).  */
  unsigned int duplicates_goal = count_duplicates_tuple ();

  /* Start with zero increments.  This is sufficient in most cases.  */
  unsigned int *current = new unsigned int [_max_key_len];
  for (int i = 0; i < _max_key_len; i++)
    current[i] = 0;
  unsigned int current_duplicates_count = count_duplicates_multiset (current);

  if (current_duplicates_count > duplicates_goal)
    {
      /* Look which _alpha_inc[i] we are free to increment.  */
      unsigned int nindices;
      {
        nindices = 0;
        PositionIterator iter = _key_positions.iterator(_max_key_len);
        for (;;)
          {
            int key_pos = iter.next ();
            if (key_pos == PositionIterator::EOS)
              break;
            if (key_pos != Positions::LASTCHAR)
              nindices++;
          }
      }

      DYNAMIC_ARRAY (indices, unsigned int, nindices);
      {
        unsigned int j = 0;
        PositionIterator iter = _key_positions.iterator(_max_key_len);
        for (;;)
          {
            int key_pos = iter.next ();
            if (key_pos == PositionIterator::EOS)
              break;
            if (key_pos != Positions::LASTCHAR)
              indices[j++] = key_pos;
          }
        if (!(j == nindices))
          abort ();
      }

      /* Perform several rounds of searching for a good alpha increment.
         Each round reduces the number of artificial collisions by adding
         an increment in a single key position.  */
      DYNAMIC_ARRAY (best, unsigned int, _max_key_len);
      DYNAMIC_ARRAY (tryal, unsigned int, _max_key_len);
      do
        {
          /* An increment of 1 is not always enough.  Try higher increments
             also.  */
          for (unsigned int inc = 1; ; inc++)
            {
              unsigned int best_duplicates_count = UINT_MAX;

              for (unsigned int j = 0; j < nindices; j++)
                {
                  memcpy (tryal, current, _max_key_len * sizeof (unsigned int));
                  tryal[indices[j]] += inc;
                  unsigned int try_duplicates_count =
                    count_duplicates_multiset (tryal);

                  /* We prefer 'try' to 'best' if it produces less
                     duplicates.  */
                  if (try_duplicates_count < best_duplicates_count)
                    {
                      memcpy (best, tryal, _max_key_len * sizeof (unsigned int));
                      best_duplicates_count = try_duplicates_count;
                    }
                }

              /* Stop this round when we got an improvement.  */
              if (best_duplicates_count < current_duplicates_count)
                {
                  memcpy (current, best, _max_key_len * sizeof (unsigned int));
                  current_duplicates_count = best_duplicates_count;
                  break;
                }
            }
        }
      while (current_duplicates_count > duplicates_goal);
      FREE_DYNAMIC_ARRAY (tryal);
      FREE_DYNAMIC_ARRAY (best);

      if (option[DEBUG])
        {
          /* Print the result.  */
          fprintf (stderr, "\nComputed alpha increments: ");
          bool first = true;
          for (unsigned int j = nindices; j-- > 0; )
            if (current[indices[j]] != 0)
              {
                if (!first)
                  fprintf (stderr, ", ");
                fprintf (stderr, "%u:+%u",
                         indices[j] + 1, current[indices[j]]);
                first = false;
              }
          fprintf (stderr, "\n");
        }
      FREE_DYNAMIC_ARRAY (indices);
    }

  _alpha_inc = current;
  _alpha_size = compute_alpha_size (_alpha_inc);
  _alpha_unify = compute_alpha_unify (_key_positions, _alpha_inc);
}

/* ======================= Finding good asso_values ======================== */

/* Initializes the asso_values[] related parameters.  */

void
Search::prepare_asso_values ()
{
  KeywordExt_List *temp;

  /* Initialize each keyword's _selchars array.  */
  init_selchars_multiset(_key_positions, _alpha_unify, _alpha_inc);

  /* Compute the maximum _selchars_length over all keywords.  */
  _max_selchars_length = _key_positions.iterator(_max_key_len).remaining();

  /* Check for duplicates, i.e. keywords with the same _selchars array
     (and - if !option[NOLENGTH] - also the same length).
     We deal with these by building an equivalence class, so that only
     1 keyword is representative of the entire collection.  Only this
     representative remains in the keyword list; the others are accessible
     through the _duplicate_link chain, starting at the representative.
     This *greatly* simplifies processing during later stages of the program.
     Set _total_duplicates and _list_len = _total_keys - _total_duplicates.  */
  {
    _list_len = _total_keys;
    _total_duplicates = 0;
    /* Make hash table for efficiency.  */
    Hash_Table representatives (_list_len, option[NOLENGTH]);

    KeywordExt_List *prev = NULL; /* list node before temp */
    for (temp = _head; temp; )
      {
        KeywordExt *keyword = temp->first();
        KeywordExt *other_keyword = representatives.insert (keyword);
        KeywordExt_List *garbage = NULL;

        if (other_keyword)
          {
            _total_duplicates++;
            _list_len--;
            /* Remove keyword from the main list.  */
            prev->rest() = temp->rest();
            garbage = temp;
            /* And insert it on other_keyword's duplicate list.  */
            keyword->_duplicate_link = other_keyword->_duplicate_link;
            other_keyword->_duplicate_link = keyword;

            /* Complain if user hasn't enabled the duplicate option. */
            if (!option[DUP] || option[DEBUG])
              {
                fprintf (stderr, "Key link: \"%.*s\" = \"%.*s\", with key set \"",
                         keyword->_allchars_length, keyword->_allchars,
                         other_keyword->_allchars_length, other_keyword->_allchars);
                for (int j = 0; j < keyword->_selchars_length; j++)
                  putc (keyword->_selchars[j], stderr);
                fprintf (stderr, "\".\n");
              }
          }
        else
          {
            keyword->_duplicate_link = NULL;
            prev = temp;
          }
        temp = temp->rest();
        if (garbage)
          delete garbage;
      }
    if (option[DEBUG])
      representatives.dump();
  }

  /* Exit program if duplicates exists and option[DUP] not set, since we
     don't want to continue in this case.  (We don't want to turn on
     option[DUP] implicitly, because the generated code is usually much
     slower.  */
  if (_total_duplicates)
    {
      if (option[DUP])
        fprintf (stderr, "%d input keys have identical hash values, examine output carefully...\n",
                         _total_duplicates);
      else
        {
          fprintf (stderr, "%d input keys have identical hash values,\n",
                           _total_duplicates);
          if (option[POSITIONS])
            fprintf (stderr, "try different key positions or use option -D.\n");
          else
            fprintf (stderr, "use option -D.\n");
          exit (1);
        }
    }

  /* Compute the occurrences of each character in the alphabet.  */
  _occurrences = new int[_alpha_size];
  memset (_occurrences, 0, _alpha_size * sizeof (_occurrences[0]));
  for (temp = _head; temp; temp = temp->rest())
    {
      KeywordExt *keyword = temp->first();
      const unsigned int *ptr = keyword->_selchars;
      for (int count = keyword->_selchars_length; count > 0; ptr++, count--)
        _occurrences[*ptr]++;
    }

  /* Memory allocation.  */
  _asso_values = new int[_alpha_size];

  int non_linked_length = _list_len;
  unsigned int asso_value_max;

  asso_value_max =
    static_cast<unsigned int>(non_linked_length * option.get_size_multiple());
  /* Round up to the next power of two.  This makes it easy to ensure
     an _asso_value[c] is >= 0 and < asso_value_max.  Also, the jump value
     being odd, it guarantees that Search::try_asso_value() will iterate
     through different values for _asso_value[c].  */
  if (asso_value_max == 0)
    asso_value_max = 1;
  asso_value_max |= asso_value_max >> 1;
  asso_value_max |= asso_value_max >> 2;
  asso_value_max |= asso_value_max >> 4;
  asso_value_max |= asso_value_max >> 8;
  asso_value_max |= asso_value_max >> 16;
  asso_value_max++;
  _asso_value_max = asso_value_max;

  /* Given the bound for _asso_values[c], we have a bound for the possible
     hash values, as computed in compute_hash().  */
  _max_hash_value = (option[NOLENGTH] ? 0 : _max_key_len)
                    + (_asso_value_max - 1) * _max_selchars_length;
  /* Allocate a sparse bit vector for detection of collisions of hash
     values.  */
  _collision_detector = new Bool_Array (_max_hash_value + 1);

  if (option[DEBUG])
    {
      fprintf (stderr, "total non-linked keys = %d\nmaximum associated value is %d"
               "\nmaximum size of generated hash table is %d\n",
               non_linked_length, asso_value_max, _max_hash_value);

      int field_width;

      field_width = 0;
      {
        for (KeywordExt_List *temp = _head; temp; temp = temp->rest())
          {
            KeywordExt *keyword = temp->first();
            if (field_width < keyword->_selchars_length)
              field_width = keyword->_selchars_length;
          }
      }

      fprintf (stderr, "\ndumping the keyword list without duplicates\n");
      fprintf (stderr, "keyword #, %*s, keyword\n", field_width, "keysig");
      int i = 0;
      for (KeywordExt_List *temp = _head; temp; temp = temp->rest())
        {
          KeywordExt *keyword = temp->first();
          fprintf (stderr, "%9d, ", ++i);
          if (field_width > keyword->_selchars_length)
            fprintf (stderr, "%*s", field_width - keyword->_selchars_length, "");
          for (int j = 0; j < keyword->_selchars_length; j++)
            putc (keyword->_selchars[j], stderr);
          fprintf (stderr, ", %.*s\n",
                   keyword->_allchars_length, keyword->_allchars);
        }
      fprintf (stderr, "\nend of keyword list\n\n");
    }

  if (option[RANDOM] || option.get_jump () == 0)
    /* We will use rand(), so initialize the random number generator.  */
    srand (static_cast<long>(time (0)));

  _initial_asso_value = (option[RANDOM] ? -1 : option.get_initial_asso_value ());
  _jump = option.get_jump ();
}

/* Finds some _asso_values[] that fit.  */

/* The idea is to choose the _asso_values[] one by one, in a way that
   a choice that has been made never needs to be undone later.  This
   means that we split the work into several steps.  Each step chooses
   one or more _asso_values[c].  The result of choosing one or more
   _asso_values[c] is that the partitioning of the keyword set gets
   broader.
   Look at this partitioning:  After every step, the _asso_values[] of a
   certain set C of characters are undetermined.  (At the beginning, C
   is the set of characters c with _occurrences[c] > 0.  At the end, C
   is empty.)  To each keyword K, we associate the multiset of _selchars
   for which the _asso_values[] are undetermined:
                    K  -->  K->_selchars intersect C.
   Consider two keywords equivalent if their value under this mapping is
   the same.  This introduces an equivalence relation on the set of
   keywords.  The equivalence classes partition the keyword set.  (At the
   beginning, the partition is the finest possible: each K is an equivalence
   class by itself, because all K have a different _selchars.  At the end,
   all K have been merged into a single equivalence class.)
   The partition before a step is always a refinement of the partition
   after the step.
   We choose the steps in such a way that the partition really becomes
   broader at each step.  (A step that only chooses an _asso_values[c]
   without changing the partition is better merged with the previous step,
   to avoid useless backtracking.)  */

struct EquivalenceClass
{
  /* The keywords in this equivalence class.  */
  KeywordExt_List *     _keywords;
  KeywordExt_List *     _keywords_last;
  /* The number of keywords in this equivalence class.  */
  unsigned int          _cardinality;
  /* The undetermined selected characters for the keywords in this
     equivalence class, as a canonically reordered multiset.  */
  unsigned int *        _undetermined_chars;
  unsigned int          _undetermined_chars_length;

  EquivalenceClass *    _next;
};

struct Step
{
  /* The characters whose values are being determined in this step.  */
  unsigned int          _changing_count;
  unsigned int *        _changing;
  /* Exclusive upper bound for the _asso_values[c] of this step.
     A power of 2.  */
  unsigned int          _asso_value_max;
  /* The characters whose values will be determined after this step.  */
  bool *                _undetermined;
  /* The keyword set partition after this step.  */
  EquivalenceClass *    _partition;
  /* The expected number of iterations in this step.  */
  double                _expected_lower;
  double                _expected_upper;

  Step *                _next;
};

static inline bool
equals (const unsigned int *ptr1, const unsigned int *ptr2, unsigned int len)
{
  while (len > 0)
    {
      if (*ptr1 != *ptr2)
        return false;
      ptr1++;
      ptr2++;
      len--;
    }
  return true;
}

EquivalenceClass *
Search::compute_partition (bool *undetermined) const
{
  EquivalenceClass *partition = NULL;
  EquivalenceClass *partition_last = NULL;
  for (KeywordExt_List *temp = _head; temp; temp = temp->rest())
    {
      KeywordExt *keyword = temp->first();

      /* Compute the undetermined characters for this keyword.  */
      unsigned int *undetermined_chars =
        new unsigned int[keyword->_selchars_length];
      unsigned int undetermined_chars_length = 0;

      for (int i = 0; i < keyword->_selchars_length; i++)
        if (undetermined[keyword->_selchars[i]])
          undetermined_chars[undetermined_chars_length++] = keyword->_selchars[i];

      /* Look up the equivalence class to which this keyword belongs.  */
      EquivalenceClass *equclass;
      for (equclass = partition; equclass; equclass = equclass->_next)
        if (equclass->_undetermined_chars_length == undetermined_chars_length
            && equals (equclass->_undetermined_chars, undetermined_chars,
                       undetermined_chars_length))
          break;
      if (equclass == NULL)
        {
          equclass = new EquivalenceClass();
          equclass->_keywords = NULL;
          equclass->_keywords_last = NULL;
          equclass->_cardinality = 0;
          equclass->_undetermined_chars = undetermined_chars;
          equclass->_undetermined_chars_length = undetermined_chars_length;
          equclass->_next = NULL;
          if (partition)
            partition_last->_next = equclass;
          else
            partition = equclass;
          partition_last = equclass;
        }
      else
        delete[] undetermined_chars;

      /* Add the keyword to the equivalence class.  */
      KeywordExt_List *cons = new KeywordExt_List(keyword);
      if (equclass->_keywords)
        equclass->_keywords_last->rest() = cons;
      else
        equclass->_keywords = cons;
      equclass->_keywords_last = cons;
      equclass->_cardinality++;
    }

  /* Free some of the allocated memory.  The caller doesn't need it.  */
  for (EquivalenceClass *cls = partition; cls; cls = cls->_next)
    delete[] cls->_undetermined_chars;

  return partition;
}

static void
delete_partition (EquivalenceClass *partition)
{
  while (partition != NULL)
    {
      EquivalenceClass *equclass = partition;
      partition = equclass->_next;
      delete_list (equclass->_keywords);
      //delete[] equclass->_undetermined_chars; // already freed above
      delete equclass;
    }
}

/* Compute the possible number of collisions when _asso_values[c] is
   chosen, leading to the given partition.  */
unsigned int
Search::count_possible_collisions (EquivalenceClass *partition, unsigned int c) const
{
  /* Every equivalence class p is split according to the frequency of
     occurrence of c, leading to equivalence classes p1, p2, ...
     This leads to   (|p|^2 - |p1|^2 - |p2|^2 - ...)/2  possible collisions.
     Return the sum of this expression over all equivalence classes.  */
  unsigned int sum = 0;
  unsigned int m = _max_selchars_length;
  DYNAMIC_ARRAY (split_cardinalities, unsigned int, m + 1);
  for (EquivalenceClass *cls = partition; cls; cls = cls->_next)
    {
      for (unsigned int i = 0; i <= m; i++)
        split_cardinalities[i] = 0;

      for (KeywordExt_List *temp = cls->_keywords; temp; temp = temp->rest())
        {
          KeywordExt *keyword = temp->first();

          unsigned int count = 0;
          for (int i = 0; i < keyword->_selchars_length; i++)
            if (keyword->_selchars[i] == c)
              count++;

          split_cardinalities[count]++;
        }

      sum += cls->_cardinality * cls->_cardinality;
      for (unsigned int i = 0; i <= m; i++)
        sum -= split_cardinalities[i] * split_cardinalities[i];
    }
  FREE_DYNAMIC_ARRAY (split_cardinalities);
  return sum;
}

/* Test whether adding c to the undetermined characters changes the given
   partition.  */
bool
Search::unchanged_partition (EquivalenceClass *partition, unsigned int c) const
{
  for (EquivalenceClass *cls = partition; cls; cls = cls->_next)
    {
      unsigned int first_count = UINT_MAX;

      for (KeywordExt_List *temp = cls->_keywords; temp; temp = temp->rest())
        {
          KeywordExt *keyword = temp->first();

          unsigned int count = 0;
          for (int i = 0; i < keyword->_selchars_length; i++)
            if (keyword->_selchars[i] == c)
              count++;

          if (temp == cls->_keywords)
            first_count = count;
          else if (count != first_count)
            /* c would split this equivalence class.  */
            return false;
        }
    }
  return true;
}

void
Search::find_asso_values ()
{
  Step *steps;

  /* Determine the steps, starting with the last one.  */
  {
    bool *undetermined;
    bool *determined;

    steps = NULL;

    undetermined = new bool[_alpha_size];
    for (unsigned int c = 0; c < _alpha_size; c++)
      undetermined[c] = false;

    determined = new bool[_alpha_size];
    for (unsigned int c = 0; c < _alpha_size; c++)
      determined[c] = true;

    for (;;)
      {
        /* Compute the partition that needs to be refined.  */
        EquivalenceClass *partition = compute_partition (undetermined);

        /* Determine the main character to be chosen in this step.
           Choosing such a character c has the effect of splitting every
           equivalence class (according the the frequency of occurrence of c).
           We choose the c with the minimum number of possible collisions,
           so that characters which lead to a large number of collisions get
           handled early during the search.  */
        unsigned int chosen_c;
        unsigned int chosen_possible_collisions;
        {
          unsigned int best_c = 0;
          unsigned int best_possible_collisions = UINT_MAX;
          for (unsigned int c = 0; c < _alpha_size; c++)
            if (_occurrences[c] > 0 && determined[c])
              {
                unsigned int possible_collisions =
                  count_possible_collisions (partition, c);
                if (possible_collisions < best_possible_collisions)
                  {
                    best_c = c;
                    best_possible_collisions = possible_collisions;
                  }
              }
          if (best_possible_collisions == UINT_MAX)
            {
              /* All c with _occurrences[c] > 0 are undetermined.  We are
                 are the starting situation and don't need any more step.  */
              delete_partition (partition);
              break;
            }
          chosen_c = best_c;
          chosen_possible_collisions = best_possible_collisions;
        }

        /* We need one more step.  */
        Step *step = new Step();

        step->_undetermined = new bool[_alpha_size];
        memcpy (step->_undetermined, undetermined, _alpha_size*sizeof(bool));

        step->_partition = partition;

        /* Now determine how the equivalence classes will be before this
           step.  */
        undetermined[chosen_c] = true;
        partition = compute_partition (undetermined);

        /* Now determine which other characters should be determined in this
           step, because they will not change the equivalence classes at
           this point.  It is the set of all c which, for all equivalence
           classes, have the same frequency of occurrence in every keyword
           of the equivalence class.  */
        for (unsigned int c = 0; c < _alpha_size; c++)
          if (_occurrences[c] > 0 && determined[c]
              && unchanged_partition (partition, c))
            {
              undetermined[c] = true;
              determined[c] = false;
            }

        /* main_c must be one of these.  */
        if (determined[chosen_c])
          abort ();

        /* Now the set of changing characters of this step.  */
        unsigned int changing_count;

        changing_count = 0;
        for (unsigned int c = 0; c < _alpha_size; c++)
          if (undetermined[c] && !step->_undetermined[c])
            changing_count++;

        unsigned int *changing = new unsigned int[changing_count];
        changing_count = 0;
        for (unsigned int c = 0; c < _alpha_size; c++)
          if (undetermined[c] && !step->_undetermined[c])
            changing[changing_count++] = c;

        step->_changing = changing;
        step->_changing_count = changing_count;

        step->_asso_value_max = _asso_value_max;

        step->_expected_lower =
          exp (static_cast<double>(chosen_possible_collisions)
               / static_cast<double>(_max_hash_value));
        step->_expected_upper =
          exp (static_cast<double>(chosen_possible_collisions)
               / static_cast<double>(_asso_value_max));

        delete_partition (partition);

        step->_next = steps;
        steps = step;
      }

    delete[] determined;
    delete[] undetermined;
  }

  if (option[DEBUG])
    {
      unsigned int stepno = 0;
      for (Step *step = steps; step; step = step->_next)
        {
          stepno++;
          fprintf (stderr, "Step %u chooses _asso_values[", stepno);
          for (unsigned int i = 0; i < step->_changing_count; i++)
            {
              if (i > 0)
                fprintf (stderr, ",");
              fprintf (stderr, "'%c'", step->_changing[i]);
            }
          fprintf (stderr, "], expected number of iterations between %g and %g.\n",
                   step->_expected_lower, step->_expected_upper);
          fprintf (stderr, "Keyword equivalence classes:\n");
          for (EquivalenceClass *cls = step->_partition; cls; cls = cls->_next)
            {
              fprintf (stderr, "\n");
              for (KeywordExt_List *temp = cls->_keywords; temp; temp = temp->rest())
                {
                  KeywordExt *keyword = temp->first();
                  fprintf (stderr, "  %.*s\n",
                           keyword->_allchars_length, keyword->_allchars);
                }
            }
          fprintf (stderr, "\n");
        }
    }

  /* Initialize _asso_values[].  (The value given here matters only
     for those c which occur in all keywords with equal multiplicity.)  */
  for (unsigned int c = 0; c < _alpha_size; c++)
    _asso_values[c] = 0;

  unsigned int stepno = 0;
  for (Step *step = steps; step; step = step->_next)
    {
      stepno++;

      /* Initialize the asso_values[].  */
      unsigned int k = step->_changing_count;
      for (unsigned int i = 0; i < k; i++)
        {
          unsigned int c = step->_changing[i];
          _asso_values[c] =
            (_initial_asso_value < 0 ? rand () : _initial_asso_value)
            & (step->_asso_value_max - 1);
        }

      unsigned int iterations = 0;
      DYNAMIC_ARRAY (iter, unsigned int, k);
      for (unsigned int i = 0; i < k; i++)
        iter[i] = 0;
      unsigned int ii = (_jump != 0 ? k - 1 : 0);

      for (;;)
        {
          /* Test whether these asso_values[] lead to collisions among
             the equivalence classes that should be collision-free.  */
          bool has_collision = false;
          for (EquivalenceClass *cls = step->_partition; cls; cls = cls->_next)
            {
              /* Iteration Number array is a win, O(1) initialization time!  */
              _collision_detector->clear ();

              for (KeywordExt_List *ptr = cls->_keywords; ptr; ptr = ptr->rest())
                {
                  KeywordExt *keyword = ptr->first();

                  /* Compute the new hash code for the keyword, leaving apart
                     the yet undetermined asso_values[].  */
                  int hashcode;
                  {
                    int sum = option[NOLENGTH] ? 0 : keyword->_allchars_length;
                    const unsigned int *p = keyword->_selchars;
                    int i = keyword->_selchars_length;
                    for (; i > 0; p++, i--)
                      if (!step->_undetermined[*p])
                        sum += _asso_values[*p];
                    hashcode = sum;
                  }

                  /* See whether it collides with another keyword's hash code,
                     from the same equivalence class.  */
                  if (_collision_detector->set_bit (hashcode))
                    {
                      has_collision = true;
                      break;
                    }
                }

              /* Don't need to continue looking at the other equivalence
                 classes if we already have found a collision.  */
              if (has_collision)
                break;
            }

          iterations++;
          if (!has_collision)
            break;

          /* Try other asso_values[].  */
          if (_jump != 0)
            {
              /* The way we try various values for
                   asso_values[step->_changing[0],...step->_changing[k-1]]
                 is like this:
                 for (bound = 0,1,...)
                   for (ii = 0,...,k-1)
                     iter[ii] := bound
                     iter[0..ii-1] := values <= bound
                     iter[ii+1..k-1] := values < bound
                 and
                   asso_values[step->_changing[i]] =
                     _initial_asso_value + iter[i] * _jump.
                 This makes it more likely to find small asso_values[].
               */
              unsigned int bound = iter[ii];
              unsigned int i = 0;
              while (i < ii)
                {
                  unsigned int c = step->_changing[i];
                  iter[i]++;
                  _asso_values[c] =
                    (_asso_values[c] + _jump) & (step->_asso_value_max - 1);
                  if (iter[i] <= bound)
                    goto found_next;
                  _asso_values[c] =
                    (_asso_values[c] - iter[i] * _jump)
                    & (step->_asso_value_max - 1);
                  iter[i] = 0;
                  i++;
                }
              i = ii + 1;
              while (i < k)
                {
                  unsigned int c = step->_changing[i];
                  iter[i]++;
                  _asso_values[c] =
                    (_asso_values[c] + _jump) & (step->_asso_value_max - 1);
                  if (iter[i] < bound)
                    goto found_next;
                  _asso_values[c] =
                    (_asso_values[c] - iter[i] * _jump)
                    & (step->_asso_value_max - 1);
                  iter[i] = 0;
                  i++;
                }
              /* Switch from one ii to the next.  */
              {
                unsigned int c = step->_changing[ii];
                _asso_values[c] =
                  (_asso_values[c] - bound * _jump)
                  & (step->_asso_value_max - 1);
                iter[ii] = 0;
              }
              /* Here all iter[i] == 0.  */
              ii++;
              if (ii == k)
                {
                  ii = 0;
                  bound++;
                  if (bound == step->_asso_value_max)
                    {
                      /* Out of search space!  We can either backtrack, or
                         increase the available search space of this step.
                         It seems simpler to choose the latter solution.  */
                      step->_asso_value_max = 2 * step->_asso_value_max;
                      if (step->_asso_value_max > _asso_value_max)
                        {
                          _asso_value_max = step->_asso_value_max;
                          /* Reinitialize _max_hash_value.  */
                          _max_hash_value =
                            (option[NOLENGTH] ? 0 : _max_key_len)
                            + (_asso_value_max - 1) * _max_selchars_length;
                          /* Reinitialize _collision_detector.  */
                          delete _collision_detector;
                          _collision_detector =
                            new Bool_Array (_max_hash_value + 1);
                        }
                    }
                }
              {
                unsigned int c = step->_changing[ii];
                iter[ii] = bound;
                _asso_values[c] =
                  (_asso_values[c] + bound * _jump)
                  & (step->_asso_value_max - 1);
              }
             found_next: ;
            }
          else
            {
              /* Random.  */
              unsigned int c = step->_changing[ii];
              _asso_values[c] =
                (_asso_values[c] + rand ()) & (step->_asso_value_max - 1);
              /* Next time, change the next c.  */
              ii++;
              if (ii == k)
                ii = 0;
            }
        }
      FREE_DYNAMIC_ARRAY (iter);

      if (option[DEBUG])
        {
          fprintf (stderr, "Step %u chose _asso_values[", stepno);
          for (unsigned int i = 0; i < step->_changing_count; i++)
            {
              if (i > 0)
                fprintf (stderr, ",");
              fprintf (stderr, "'%c'", step->_changing[i]);
            }
          fprintf (stderr, "] in %u iterations.\n", iterations);
        }
    }

  /* Free allocated memory.  */
  while (steps != NULL)
    {
      Step *step = steps;
      steps = step->_next;
      delete[] step->_changing;
      delete[] step->_undetermined;
      delete_partition (step->_partition);
      delete step;
    }
}

/* Computes a keyword's hash value, relative to the current _asso_values[],
   and stores it in keyword->_hash_value.  */

inline int
Search::compute_hash (KeywordExt *keyword) const
{
  int sum = option[NOLENGTH] ? 0 : keyword->_allchars_length;

  const unsigned int *p = keyword->_selchars;
  int i = keyword->_selchars_length;
  for (; i > 0; p++, i--)
    sum += _asso_values[*p];

  return keyword->_hash_value = sum;
}

/* Finds good _asso_values[].  */

void
Search::find_good_asso_values ()
{
  prepare_asso_values ();

  /* Search for good _asso_values[].  */
  int asso_iteration;
  if ((asso_iteration = option.get_asso_iterations ()) == 0)
    /* Try only the given _initial_asso_value and _jump.  */
    find_asso_values ();
  else
    {
      /* Try different pairs of _initial_asso_value and _jump, in the
         following order:
           (0, 1)
           (1, 1)
           (2, 1) (0, 3)
           (3, 1) (1, 3)
           (4, 1) (2, 3) (0, 5)
           (5, 1) (3, 3) (1, 5)
           ..... */
      KeywordExt_List *saved_head = _head;
      int best_initial_asso_value = 0;
      int best_jump = 1;
      int *best_asso_values = new int[_alpha_size];
      int best_collisions = INT_MAX;
      int best_max_hash_value = INT_MAX;

      _initial_asso_value = 0; _jump = 1;
      for (;;)
        {
          /* Restore the keyword list in its original order.  */
          _head = copy_list (saved_head);
          /* Find good _asso_values[].  */
          find_asso_values ();
          /* Test whether it is the best solution so far.  */
          int collisions = 0;
          int max_hash_value = INT_MIN;
          _collision_detector->clear ();
          for (KeywordExt_List *ptr = _head; ptr; ptr = ptr->rest())
            {
              KeywordExt *keyword = ptr->first();
              int hashcode = compute_hash (keyword);
              if (max_hash_value < hashcode)
                max_hash_value = hashcode;
              if (_collision_detector->set_bit (hashcode))
                collisions++;
            }
          if (collisions < best_collisions
              || (collisions == best_collisions
                  && max_hash_value < best_max_hash_value))
            {
              memcpy (best_asso_values, _asso_values,
                      _alpha_size * sizeof (_asso_values[0]));
              best_collisions = collisions;
              best_max_hash_value = max_hash_value;
            }
          /* Delete the copied keyword list.  */
          delete_list (_head);

          if (--asso_iteration == 0)
            break;
          /* Prepare for next iteration.  */
          if (_initial_asso_value >= 2)
            _initial_asso_value -= 2, _jump += 2;
          else
            _initial_asso_value += _jump, _jump = 1;
        }
      _head = saved_head;
      /* Install the best found asso_values.  */
      _initial_asso_value = best_initial_asso_value;
      _jump = best_jump;
      memcpy (_asso_values, best_asso_values,
              _alpha_size * sizeof (_asso_values[0]));
      delete[] best_asso_values;
      /* The keywords' _hash_value fields are recomputed below.  */
    }
}

/* ========================================================================= */

/* Comparison function for sorting by increasing _hash_value.  */
static bool
less_by_hash_value (KeywordExt *keyword1, KeywordExt *keyword2)
{
  return keyword1->_hash_value < keyword2->_hash_value;
}

/* Sorts the keyword list by hash value.  */

void
Search::sort ()
{
  _head = mergesort_list (_head, less_by_hash_value);
}

void
Search::optimize ()
{
  /* Preparations.  */
  prepare ();

  /* Step 1: Finding good byte positions.  */
  find_positions ();

  /* Step 2: Finding good alpha increments.  */
  find_alpha_inc ();

  /* Step 3: Finding good asso_values.  */
  find_good_asso_values ();

  /* Make one final check, just to make sure nothing weird happened.... */
  _collision_detector->clear ();
  for (KeywordExt_List *curr_ptr = _head; curr_ptr; curr_ptr = curr_ptr->rest())
    {
      KeywordExt *curr = curr_ptr->first();
      unsigned int hashcode = compute_hash (curr);
      if (_collision_detector->set_bit (hashcode))
        {
          /* This shouldn't happen.  proj1, proj2, proj3 must have been
             computed to be injective on the given keyword set.  */
          fprintf (stderr,
                   "\nInternal error, unexpected duplicate hash code\n");
          if (option[POSITIONS])
            fprintf (stderr, "try options -m or -r, or use new key positions.\n\n");
          else
            fprintf (stderr, "try options -m or -r.\n\n");
          exit (1);
        }
    }

  /* Sorts the keyword list by hash value.  */
  sort ();

  /* Set unused asso_values[c] to max_hash_value + 1.  This is not absolutely
     necessary, but speeds up the lookup function in many cases of lookup
     failure: no string comparison is needed once the hash value of a string
     is larger than the hash value of any keyword.  */
  int max_hash_value;
  {
    KeywordExt_List *temp;
    for (temp = _head; temp->rest(); temp = temp->rest())
      ;
    max_hash_value = temp->first()->_hash_value;
  }
  for (unsigned int c = 0; c < _alpha_size; c++)
    if (_occurrences[c] == 0)
      _asso_values[c] = max_hash_value + 1;

  /* Propagate unified asso_values.  */
  if (_alpha_unify)
    for (unsigned int c = 0; c < _alpha_size; c++)
      if (_alpha_unify[c] != c)
        _asso_values[c] = _asso_values[_alpha_unify[c]];
}

/* Prints out some diagnostics upon completion.  */

Search::~Search ()
{
  delete _collision_detector;
  if (option[DEBUG])
    {
      fprintf (stderr, "\ndumping occurrence and associated values tables\n");

      for (unsigned int i = 0; i < _alpha_size; i++)
        if (_occurrences[i])
          fprintf (stderr, "asso_values[%c] = %6d, occurrences[%c] = %6d\n",
                   i, _asso_values[i], i, _occurrences[i]);

      fprintf (stderr, "end table dumping\n");

      fprintf (stderr, "\nDumping key list information:\ntotal non-static linked keywords = %d"
               "\ntotal keywords = %d\ntotal duplicates = %d\nmaximum key length = %d\n",
               _list_len, _total_keys, _total_duplicates, _max_key_len);

      int field_width = _max_selchars_length;
      fprintf (stderr, "\nList contents are:\n(hash value, key length, index, %*s, keyword):\n",
               field_width, "selchars");
      for (KeywordExt_List *ptr = _head; ptr; ptr = ptr->rest())
        {
          fprintf (stderr, "%11d,%11d,%6d, ",
                   ptr->first()->_hash_value, ptr->first()->_allchars_length, ptr->first()->_final_index);
          if (field_width > ptr->first()->_selchars_length)
            fprintf (stderr, "%*s", field_width - ptr->first()->_selchars_length, "");
          for (int j = 0; j < ptr->first()->_selchars_length; j++)
            putc (ptr->first()->_selchars[j], stderr);
          fprintf (stderr, ", %.*s\n",
                   ptr->first()->_allchars_length, ptr->first()->_allchars);
        }

      fprintf (stderr, "End dumping list.\n\n");
    }
  delete[] _asso_values;
  delete[] _occurrences;
  delete[] _alpha_unify;
  delete[] _alpha_inc;
}
