/* Routines for name->symbol lookups in GDB.
   
   Copyright 2003 Free Software Foundation, Inc.

   Contributed by David Carlton <carlton@bactrian.org> and by Kealia,
   Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#ifndef DICTIONARY_H
#define DICTIONARY_H

/* An opaque type for dictionaries; only dictionary.c should know
   about its innards.  */

struct dictionary;

/* Other types needed for declarations.  */

struct symbol;
struct obstack;
struct pending;


/* The creation functions for various implementations of
   dictionaries.  */

/* Create a dictionary implemented via a fixed-size hashtable.  All
   memory it uses is allocated on OBSTACK; the environment is
   initialized from SYMBOL_LIST.  */

extern struct dictionary *dict_create_hashed (struct obstack *obstack,
					      const struct pending
					      *symbol_list);

/* Create a dictionary implemented via a hashtable that grows as
   necessary.  The dictionary is initially empty; to add symbols to
   it, call dict_add_symbol().  Call dict_free() when you're done with
   it.  */

extern struct dictionary *dict_create_hashed_expandable (void);

/* Create a dictionary implemented via a fixed-size array.  All memory
   it uses is allocated on OBSTACK; the environment is initialized
   from the SYMBOL_LIST.  The symbols are ordered in the same order
   that they're found in SYMBOL_LIST.  */

extern struct dictionary *dict_create_linear (struct obstack *obstack,
					      const struct pending
					      *symbol_list);

/* Create a dictionary implemented via an array that grows as
   necessary.  The dictionary is initially empty; to add symbols to
   it, call dict_add_symbol().  Call dict_free() when you're done with
   it.  */

extern struct dictionary *dict_create_linear_expandable (void);


/* The functions providing the interface to dictionaries.  Note that
   the most common parts of the interface, namely symbol lookup, are
   only provided via iterator functions.  */

/* Free the memory used by a dictionary that's not on an obstack.  (If
   any.)  */

extern void dict_free (struct dictionary *dict);

/* Add a symbol to an expandable dictionary.  */

extern void dict_add_symbol (struct dictionary *dict, struct symbol *sym);

/* Is the dictionary empty?  */

extern int dict_empty (struct dictionary *dict);

/* A type containing data that is used when iterating over all symbols
   in a dictionary.  Don't ever look at its innards; this type would
   be opaque if we didn't need to be able to allocate it on the
   stack.  */

struct dict_iterator
{
  /* The dictionary that this iterator is associated to.  */
  const struct dictionary *dict;
  /* The next two members are data that is used in a way that depends
     on DICT's implementation type.  */
  int index;
  struct symbol *current;
};

/* Initialize ITERATOR to point at the first symbol in DICT, and
   return that first symbol, or NULL if DICT is empty.  */

extern struct symbol *dict_iterator_first (const struct dictionary *dict,
					   struct dict_iterator *iterator);

/* Advance ITERATOR, and return the next symbol, or NULL if there are
   no more symbols.  Don't call this if you've previously received
   NULL from dict_iterator_first or dict_iterator_next on this
   iteration.  */

extern struct symbol *dict_iterator_next (struct dict_iterator *iterator);

/* Initialize ITERATOR to point at the first symbol in DICT whose
   SYMBOL_BEST_NAME is NAME (as tested using strcmp_iw), and return
   that first symbol, or NULL if there are no such symbols.  */

extern struct symbol *dict_iter_name_first (const struct dictionary *dict,
					    const char *name,
					    struct dict_iterator *iterator);

/* Advance ITERATOR to point at the next symbol in DICT whose
   SYMBOL_BEST_NAME is NAME (as tested using strcmp_iw), or NULL if
   there are no more such symbols.  Don't call this if you've
   previously received NULL from dict_iterator_first or
   dict_iterator_next on this iteration.  And don't call it unless
   ITERATOR was created by a previous call to dict_iter_name_first
   with the same NAME.  */

extern struct symbol *dict_iter_name_next (const char *name,
					   struct dict_iterator *iterator);

/* Return some notion of the size of the dictionary: the number of
   symbols if we have that, the number of hash buckets otherwise.  */

extern int dict_size (const struct dictionary *dict);

/* Macro to loop through all symbols in a dictionary DICT, in no
   particular order.  ITER is a struct dict_iterator (NOTE: __not__ a
   struct dict_iterator *), and SYM points to the current symbol.

   It's implemented as a single loop, so you can terminate the loop
   early by a break if you desire.  */

#define ALL_DICT_SYMBOLS(dict, iter, sym)			\
	for ((sym) = dict_iterator_first ((dict), &(iter));	\
	     (sym);						\
	     (sym) = dict_iterator_next (&(iter)))

#endif /* DICTIONARY_H */
