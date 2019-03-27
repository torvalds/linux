/* This may look like C code, but it is really -*- C++ -*- */

/* Output routines.

   Copyright (C) 1989-1998, 2000, 2002-2003 Free Software Foundation, Inc.
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

#ifndef output_h
#define output_h 1

#include "keyword-list.h"
#include "positions.h"

/* OSF/1 cxx needs these forward declarations. */
struct Output_Constants;
struct Output_Compare;

class Output
{
public:
  /* Constructor.  */
                        Output (KeywordExt_List *head,
                                const char *struct_decl,
                                unsigned int struct_decl_lineno,
                                const char *return_type,
                                const char *struct_tag,
                                const char *verbatim_declarations,
                                const char *verbatim_declarations_end,
                                unsigned int verbatim_declarations_lineno,
                                const char *verbatim_code,
                                const char *verbatim_code_end,
                                unsigned int verbatim_code_lineno,
                                bool charset_dependent,
                                int total_keys,
                                int max_key_len, int min_key_len,
                                const Positions& positions,
                                const unsigned int *alpha_inc,
                                int total_duplicates,
                                unsigned int alpha_size,
                                const int *asso_values);

  /* Generates the hash function and the key word recognizer function.  */
  void                  output ();

private:

  /* Computes the minimum and maximum hash values, and stores them
     in _min_hash_value and _max_hash_value.  */
  void                  compute_min_max ();

  /* Returns the number of different hash values.  */
  int                   num_hash_values () const;

  /* Outputs the maximum and minimum hash values etc.  */
  void                  output_constants (struct Output_Constants&) const;

  /* Generates a C expression for an asso_values[] reference.  */
  void                  output_asso_values_ref (int pos) const;

  /* Generates C code for the hash function that returns the
     proper encoding for each keyword.  */
  void                  output_hash_function () const;

  /* Prints out a table of keyword lengths, for use with the
     comparison code in generated function 'in_word_set'.  */
  void                  output_keylength_table () const;

  /* Prints out the string pool, containing the strings of the keyword table.
   */
  void                  output_string_pool () const;

  /* Prints out the array containing the keywords for the hash function.  */
  void                  output_keyword_table () const;

  /* Generates the large, sparse table that maps hash values into
     the smaller, contiguous range of the keyword table.  */
  void                  output_lookup_array () const;

  /* Generate all pools needed for the lookup function.  */
  void                  output_lookup_pools () const;

  /* Generate all the tables needed for the lookup function.  */
  void                  output_lookup_tables () const;

  /* Generates C code to perform the keyword lookup.  */
  void                  output_lookup_function_body (const struct Output_Compare&) const;

  /* Generates C code for the lookup function.  */
  void                  output_lookup_function () const;

  /* Linked list of keywords.  */
  KeywordExt_List *     _head;

  /* Declaration of struct type for a keyword and its attributes.  */
  const char * const    _struct_decl;
  unsigned int const    _struct_decl_lineno;
  /* Pointer to return type for lookup function. */
  const char *          _return_type;
  /* Shorthand for user-defined struct tag type. */
  const char *          _struct_tag;
  /* Element type of keyword array.  */
  const char *          _wordlist_eltype;
  /* The C code from the declarations section.  */
  const char * const    _verbatim_declarations;
  const char * const    _verbatim_declarations_end;
  unsigned int const    _verbatim_declarations_lineno;
  /* The C code from the end of the file.  */
  const char * const    _verbatim_code;
  const char * const    _verbatim_code_end;
  unsigned int const    _verbatim_code_lineno;
  /* Whether the keyword chars would have different values in a different
     character set.  */
  bool                  _charset_dependent;
  /* Total number of keys, counting duplicates. */
  int const             _total_keys;
  /* Maximum length of the longest keyword. */
  int const             _max_key_len;
  /* Minimum length of the shortest keyword. */
  int const             _min_key_len;
  /* Key positions.  */
  Positions const       _key_positions;
  /* Adjustments to add to bytes add specific key positions.  */
  const unsigned int * const _alpha_inc;
  /* Total number of duplicate hash values. */
  int const             _total_duplicates;
  /* Minimum hash value for all keywords. */
  int                   _min_hash_value;
  /* Maximum hash value for all keywords. */
  int                   _max_hash_value;
  /* Size of alphabet. */
  unsigned int const    _alpha_size;
  /* Value associated with each character. */
  const int * const     _asso_values;
};

#endif
