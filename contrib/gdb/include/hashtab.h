/* An expandable hash tables datatype.  
   Copyright (C) 1999, 2000, 2002, 2003 Free Software Foundation, Inc.
   Contributed by Vladimir Makarov (vmakarov@cygnus.com).

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* This package implements basic hash table functionality.  It is possible
   to search for an entry, create an entry and destroy an entry.

   Elements in the table are generic pointers.

   The size of the table is not fixed; if the occupancy of the table
   grows too high the hash table will be expanded.

   The abstract data implementation is based on generalized Algorithm D
   from Knuth's book "The art of computer programming".  Hash table is
   expanded by creation of new hash table and transferring elements from
   the old table to the new table.  */

#ifndef __HASHTAB_H__
#define __HASHTAB_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "ansidecl.h"

#ifndef GTY
#define GTY(X)
#endif

/* The type for a hash code.  */
typedef unsigned int hashval_t;

/* Callback function pointer types.  */

/* Calculate hash of a table entry.  */
typedef hashval_t (*htab_hash) PARAMS ((const void *));

/* Compare a table entry with a possible entry.  The entry already in
   the table always comes first, so the second element can be of a
   different type (but in this case htab_find and htab_find_slot
   cannot be used; instead the variants that accept a hash value
   must be used).  */
typedef int (*htab_eq) PARAMS ((const void *, const void *));

/* Cleanup function called whenever a live element is removed from
   the hash table.  */
typedef void (*htab_del) PARAMS ((void *));
  
/* Function called by htab_traverse for each live element.  The first
   arg is the slot of the element (which can be passed to htab_clear_slot
   if desired), the second arg is the auxiliary pointer handed to
   htab_traverse.  Return 1 to continue scan, 0 to stop.  */
typedef int (*htab_trav) PARAMS ((void **, void *));

/* Memory-allocation function, with the same functionality as calloc().
   Iff it returns NULL, the hash table implementation will pass an error
   code back to the user, so if your code doesn't handle errors,
   best if you use xcalloc instead.  */
typedef PTR (*htab_alloc) PARAMS ((size_t, size_t));

/* We also need a free() routine.  */
typedef void (*htab_free) PARAMS ((PTR));

/* Memory allocation and deallocation; variants which take an extra
   argument.  */
typedef PTR (*htab_alloc_with_arg) PARAMS ((void *, size_t, size_t));
typedef void (*htab_free_with_arg) PARAMS ((void *, void *));

/* Hash tables are of the following type.  The structure
   (implementation) of this type is not needed for using the hash
   tables.  All work with hash table should be executed only through
   functions mentioned below.  The size of this structure is subject to
   change.  */

struct htab GTY(())
{
  /* Pointer to hash function.  */
  htab_hash hash_f;

  /* Pointer to comparison function.  */
  htab_eq eq_f;

  /* Pointer to cleanup function.  */
  htab_del del_f;

  /* Table itself.  */
  PTR * GTY ((use_param (""), length ("%h.size"))) entries;

  /* Current size (in entries) of the hash table */
  size_t size;

  /* Current number of elements including also deleted elements */
  size_t n_elements;

  /* Current number of deleted elements in the table */
  size_t n_deleted;

  /* The following member is used for debugging. Its value is number
     of all calls of `htab_find_slot' for the hash table. */
  unsigned int searches;

  /* The following member is used for debugging.  Its value is number
     of collisions fixed for time of work with the hash table. */
  unsigned int collisions;

  /* Pointers to allocate/free functions.  */
  htab_alloc alloc_f;
  htab_free free_f;

  /* Alternate allocate/free functions, which take an extra argument.  */
  PTR GTY((skip (""))) alloc_arg;
  htab_alloc_with_arg alloc_with_arg_f;
  htab_free_with_arg free_with_arg_f;
};

typedef struct htab *htab_t;

/* An enum saying whether we insert into the hash table or not.  */
enum insert_option {NO_INSERT, INSERT};

/* The prototypes of the package functions. */

extern htab_t	htab_create_alloc	PARAMS ((size_t, htab_hash,
						 htab_eq, htab_del,
						 htab_alloc, htab_free));

extern htab_t	htab_create_alloc_ex	PARAMS ((size_t, htab_hash,
						    htab_eq, htab_del,
						    PTR, htab_alloc_with_arg,
						    htab_free_with_arg));

/* Backward-compatibility functions.  */
extern htab_t htab_create PARAMS ((size_t, htab_hash, htab_eq, htab_del));
extern htab_t htab_try_create PARAMS ((size_t, htab_hash, htab_eq, htab_del));

extern void	htab_set_functions_ex	PARAMS ((htab_t, htab_hash,
						 htab_eq, htab_del,
						 PTR, htab_alloc_with_arg,
						 htab_free_with_arg));

extern void	htab_delete	PARAMS ((htab_t));
extern void	htab_empty	PARAMS ((htab_t));

extern PTR	htab_find	PARAMS ((htab_t, const void *));
extern PTR     *htab_find_slot	PARAMS ((htab_t, const void *,
					 enum insert_option));
extern PTR	htab_find_with_hash	  PARAMS ((htab_t, const void *,
						   hashval_t));
extern PTR     *htab_find_slot_with_hash  PARAMS ((htab_t, const void *,
						   hashval_t,
						   enum insert_option));
extern void	htab_clear_slot	PARAMS ((htab_t, void **));
extern void	htab_remove_elt	PARAMS ((htab_t, void *));

extern void	htab_traverse	PARAMS ((htab_t, htab_trav, void *));
extern void	htab_traverse_noresize	PARAMS ((htab_t, htab_trav, void *));

extern size_t	htab_size	PARAMS ((htab_t));
extern size_t	htab_elements	PARAMS ((htab_t));
extern double	htab_collisions	PARAMS ((htab_t));

/* A hash function for pointers.  */
extern htab_hash htab_hash_pointer;

/* An equality function for pointers.  */
extern htab_eq htab_eq_pointer;

/* A hash function for null-terminated strings.  */
extern hashval_t htab_hash_string PARAMS ((const PTR));

/* An iterative hash function for arbitrary data.  */
extern hashval_t iterative_hash PARAMS ((const PTR, size_t, hashval_t));
/* Shorthand for hashing something with an intrinsic size.  */
#define iterative_hash_object(OB,INIT) iterative_hash (&OB, sizeof (OB), INIT)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __HASHTAB_H */
