/* This may look like C code, but it is really -*- C++ -*- */

/* Handles parsing the Options provided to the user.

   Copyright (C) 1989-1998, 2000, 2002-2004 Free Software Foundation, Inc.
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

/* This module provides a uniform interface to the various options available
   to a user of the gperf hash function generator.  */

#ifndef options_h
#define options_h 1

#include <stdio.h>
#include "positions.h"

/* Enumeration of the possible boolean options.  */

enum Option_Type
{
  /* --- Input file interpretation --- */

  /* Handle user-defined type structured keyword input.  */
  TYPE         = 1 << 0,

  /* Ignore case of ASCII characters.  */
  UPPERLOWER   = 1 << 1,

  /* --- Language for the output code --- */

  /* Generate K&R C code: no prototypes, no const.  */
  KRC          = 1 << 2,

  /* Generate C code: no prototypes, but const (user can #define it away).  */
  C            = 1 << 3,

  /* Generate ISO/ANSI C code: prototypes and const, but no class.  */
  ANSIC        = 1 << 4,

  /* Generate C++ code: prototypes, const, class, inline, enum.  */
  CPLUSPLUS    = 1 << 5,

  /* --- Details in the output code --- */

  /* Assume 7-bit, not 8-bit, characters.  */
  SEVENBIT     = 1 << 6,

  /* Generate a length table for string comparison.  */
  LENTABLE     = 1 << 7,

  /* Generate strncmp rather than strcmp.  */
  COMP         = 1 << 8,

  /* Make the generated tables readonly (const).  */
  CONST        = 1 << 9,

  /* Use enum for constants.  */
  ENUM         = 1 << 10,

  /* Generate #include statements.  */
  INCLUDE      = 1 << 11,

  /* Make the keyword table a global variable.  */
  GLOBAL       = 1 << 12,

  /* Use NULL strings instead of empty strings for empty table entries.  */
  NULLSTRINGS  = 1 << 13,

  /* Optimize for position-independent code.  */
  SHAREDLIB    = 1 << 14,

  /* Generate switch output to save space.  */
  SWITCH       = 1 << 15,

  /* Don't include user-defined type definition in output -- it's already
     defined elsewhere.  */
  NOTYPE       = 1 << 16,

  /* --- Algorithm employed by gperf --- */

  /* Use the given key positions.  */
  POSITIONS    = 1 << 17,

  /* Handle duplicate hash values for keywords.  */
  DUP          = 1 << 18,

  /* Don't include keyword length in hash computations.  */
  NOLENGTH     = 1 << 19,

  /* Randomly initialize the associated values table.  */
  RANDOM       = 1 << 20,

  /* --- Informative output --- */

  /* Enable debugging (prints diagnostics to stderr).  */
  DEBUG        = 1 << 21
};

/* Class manager for gperf program Options.  */

class Options
{
public:
  /* Constructor.  */
                        Options ();

  /* Destructor.  */
                        ~Options ();

  /* Parses the options given in the command-line arguments.  */
  void                  parse_options (int argc, char *argv[]);

  /* Prints the given options.  */
  void                  print_options () const;

  /* Accessors.  */

  /* Tests a given boolean option.  Returns true if set, false otherwise.  */
  bool                  operator[] (Option_Type option) const;
  /* Sets a given boolean option.  */
  void                  set (Option_Type option);

  /* Returns the input file name.  */
  const char *          get_input_file_name () const;

  /* Returns the output file name.  */
  const char *          get_output_file_name () const;

  /* Sets the output language, if not already set.  */
  void                  set_language (const char *language);

  /* Returns the jump value.  */
  int                   get_jump () const;

  /* Returns the initial associated character value.  */
  int                   get_initial_asso_value () const;

  /* Returns the number of iterations for finding good asso_values.  */
  int                   get_asso_iterations () const;

  /* Returns the total number of switch statements to generate.  */
  int                   get_total_switches () const;
  /* Sets the total number of switch statements, if not already set.  */
  void                  set_total_switches (int total_switches);

  /* Returns the factor by which to multiply the generated table's size.  */
  float                 get_size_multiple () const;

  /* Returns the generated function name.  */
  const char *          get_function_name () const;
  /* Sets the generated function name, if not already set.  */
  void                  set_function_name (const char *name);

  /* Returns the keyword key name.  */
  const char *          get_slot_name () const;
  /* Sets the keyword key name, if not already set.  */
  void                  set_slot_name (const char *name);

  /* Returns the struct initializer suffix.  */
  const char *          get_initializer_suffix () const;
  /* Sets the struct initializer suffix, if not already set.  */
  void                  set_initializer_suffix (const char *initializers);

  /* Returns the generated class name.  */
  const char *          get_class_name () const;
  /* Sets the generated class name, if not already set.  */
  void                  set_class_name (const char *name);

  /* Returns the hash function name.  */
  const char *          get_hash_name () const;
  /* Sets the hash function name, if not already set.  */
  void                  set_hash_name (const char *name);

  /* Returns the hash table array name.  */
  const char *          get_wordlist_name () const;
  /* Sets the hash table array name, if not already set.  */
  void                  set_wordlist_name (const char *name);

  /* Returns the length table array name.  */
  const char *          get_lengthtable_name () const;
  /* Sets the length table array name, if not already set.  */
  void                  set_lengthtable_name (const char *name);

  /* Returns the string pool name.  */
  const char *          get_stringpool_name () const;
  /* Sets the string pool name, if not already set.  */
  void                  set_stringpool_name (const char *name);

  /* Returns the string used to delimit keywords from other attributes.  */
  const char *          get_delimiters () const;
  /* Sets the delimiters string, if not already set.  */
  void                  set_delimiters (const char *delimiters);

  /* Returns key positions.  */
  const Positions&      get_key_positions () const;

private:
  /* Prints program usage to given stream.  */
  static void           short_usage (FILE * stream);

  /* Prints program usage to given stream.  */
  static void           long_usage (FILE * stream);

  /* Records count of command-line arguments.  */
  int                   _argument_count;

  /* Stores a pointer to command-line argument vector.  */
  char **               _argument_vector;

  /* Holds the boolean options.  */
  int                   _option_word;

  /* Name of input file.  */
  char *                _input_file_name;

  /* Name of output file.  */
  char *                _output_file_name;

  /* The output language.  */
  const char *          _language;

  /* Jump length when trying alternative values.  */
  int                   _jump;

  /* Initial value for asso_values table.  */
  int                   _initial_asso_value;

  /* Number of attempts at finding good asso_values.  */
  int                   _asso_iterations;

  /* Number of switch statements to generate.  */
  int                   _total_switches;

  /* Factor by which to multiply the generated table's size.  */
  float                 _size_multiple;

  /* Names used for generated lookup function.  */
  const char *          _function_name;

  /* Name used for keyword key.  */
  const char *          _slot_name;

  /* Suffix for empty struct initializers.  */
  const char *          _initializer_suffix;

  /* Name used for generated C++ class.  */
  const char *          _class_name;

  /* Name used for generated hash function.  */
  const char *          _hash_name;

  /* Name used for hash table array.  */
  const char *          _wordlist_name;

  /* Name used for length table array.  */
  const char *          _lengthtable_name;

  /* Name used for the string pool.  */
  const char *          _stringpool_name;

  /* Separates keywords from other attributes.  */
  const char *          _delimiters;

  /* Contains user-specified key choices.  */
  Positions             _key_positions;
};

/* Global option coordinator for the entire program.  */
extern Options option;

#ifdef __OPTIMIZE__

#define INLINE inline
#include "options.icc"
#undef INLINE

#endif

#endif
