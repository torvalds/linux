/* An expandable hash tables datatype.  
   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004
   Free Software Foundation, Inc.
   Contributed by Vladimir Makarov (vmakarov@cygnus.com).

This file is part of the libiberty library.
Libiberty is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

Libiberty is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with libiberty; see the file COPYING.LIB.  If
not, write to the Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* This package implements basic hash table functionality.  It is possible
   to search for an entry, create an entry and destroy an entry.

   Elements in the table are generic pointers.

   The size of the table is not fixed; if the occupancy of the table
   grows too high the hash table will be expanded.

   The abstract data implementation is based on generalized Algorithm D
   from Knuth's book "The art of computer programming".  Hash table is
   expanded by creation of new hash table and transferring elements from
   the old table to the new table. */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#include <stdio.h>

#include "libiberty.h"
#include "ansidecl.h"
#include "hashtab.h"

#ifndef CHAR_BIT
#define CHAR_BIT 8
#endif

static unsigned int higher_prime_index (unsigned long);
static hashval_t htab_mod_1 (hashval_t, hashval_t, hashval_t, int);
static hashval_t htab_mod (hashval_t, htab_t);
static hashval_t htab_mod_m2 (hashval_t, htab_t);
static hashval_t hash_pointer (const void *);
static int eq_pointer (const void *, const void *);
static int htab_expand (htab_t);
static PTR *find_empty_slot_for_expand (htab_t, hashval_t);

/* At some point, we could make these be NULL, and modify the
   hash-table routines to handle NULL specially; that would avoid
   function-call overhead for the common case of hashing pointers.  */
htab_hash htab_hash_pointer = hash_pointer;
htab_eq htab_eq_pointer = eq_pointer;

/* Table of primes and multiplicative inverses.

   Note that these are not minimally reduced inverses.  Unlike when generating
   code to divide by a constant, we want to be able to use the same algorithm
   all the time.  All of these inverses (are implied to) have bit 32 set.

   For the record, here's the function that computed the table; it's a 
   vastly simplified version of the function of the same name from gcc.  */

#if 0
unsigned int
ceil_log2 (unsigned int x)
{
  int i;
  for (i = 31; i >= 0 ; --i)
    if (x > (1u << i))
      return i+1;
  abort ();
}

unsigned int
choose_multiplier (unsigned int d, unsigned int *mlp, unsigned char *shiftp)
{
  unsigned long long mhigh;
  double nx;
  int lgup, post_shift;
  int pow, pow2;
  int n = 32, precision = 32;

  lgup = ceil_log2 (d);
  pow = n + lgup;
  pow2 = n + lgup - precision;

  nx = ldexp (1.0, pow) + ldexp (1.0, pow2);
  mhigh = nx / d;

  *shiftp = lgup - 1;
  *mlp = mhigh;
  return mhigh >> 32;
}
#endif

struct prime_ent
{
  hashval_t prime;
  hashval_t inv;
  hashval_t inv_m2;	/* inverse of prime-2 */
  hashval_t shift;
};

static struct prime_ent const prime_tab[] = {
  {          7, 0x24924925, 0x9999999b, 2 },
  {         13, 0x3b13b13c, 0x745d1747, 3 },
  {         31, 0x08421085, 0x1a7b9612, 4 },
  {         61, 0x0c9714fc, 0x15b1e5f8, 5 },
  {        127, 0x02040811, 0x0624dd30, 6 },
  {        251, 0x05197f7e, 0x073260a5, 7 },
  {        509, 0x01824366, 0x02864fc8, 8 },
  {       1021, 0x00c0906d, 0x014191f7, 9 },
  {       2039, 0x0121456f, 0x0161e69e, 10 },
  {       4093, 0x00300902, 0x00501908, 11 },
  {       8191, 0x00080041, 0x00180241, 12 },
  {      16381, 0x000c0091, 0x00140191, 13 },
  {      32749, 0x002605a5, 0x002a06e6, 14 },
  {      65521, 0x000f00e2, 0x00110122, 15 },
  {     131071, 0x00008001, 0x00018003, 16 },
  {     262139, 0x00014002, 0x0001c004, 17 },
  {     524287, 0x00002001, 0x00006001, 18 },
  {    1048573, 0x00003001, 0x00005001, 19 },
  {    2097143, 0x00004801, 0x00005801, 20 },
  {    4194301, 0x00000c01, 0x00001401, 21 },
  {    8388593, 0x00001e01, 0x00002201, 22 },
  {   16777213, 0x00000301, 0x00000501, 23 },
  {   33554393, 0x00001381, 0x00001481, 24 },
  {   67108859, 0x00000141, 0x000001c1, 25 },
  {  134217689, 0x000004e1, 0x00000521, 26 },
  {  268435399, 0x00000391, 0x000003b1, 27 },
  {  536870909, 0x00000019, 0x00000029, 28 },
  { 1073741789, 0x0000008d, 0x00000095, 29 },
  { 2147483647, 0x00000003, 0x00000007, 30 },
  /* Avoid "decimal constant so large it is unsigned" for 4294967291.  */
  { 0xfffffffb, 0x00000006, 0x00000008, 31 }
};

/* The following function returns an index into the above table of the
   nearest prime number which is greater than N, and near a power of two. */

static unsigned int
higher_prime_index (unsigned long n)
{
  unsigned int low = 0;
  unsigned int high = sizeof(prime_tab) / sizeof(prime_tab[0]);

  while (low != high)
    {
      unsigned int mid = low + (high - low) / 2;
      if (n > prime_tab[mid].prime)
	low = mid + 1;
      else
	high = mid;
    }

  /* If we've run out of primes, abort.  */
  if (n > prime_tab[low].prime)
    {
      fprintf (stderr, "Cannot find prime bigger than %lu\n", n);
      abort ();
    }

  return low;
}

/* Returns a hash code for P.  */

static hashval_t
hash_pointer (const PTR p)
{
  return (hashval_t) ((long)p >> 3);
}

/* Returns non-zero if P1 and P2 are equal.  */

static int
eq_pointer (const PTR p1, const PTR p2)
{
  return p1 == p2;
}


/* The parens around the function names in the next two definitions
   are essential in order to prevent macro expansions of the name.
   The bodies, however, are expanded as expected, so they are not
   recursive definitions.  */

/* Return the current size of given hash table.  */

#define htab_size(htab)  ((htab)->size)

size_t
(htab_size) (htab_t htab)
{
  return htab_size (htab);
}

/* Return the current number of elements in given hash table. */

#define htab_elements(htab)  ((htab)->n_elements - (htab)->n_deleted)

size_t
(htab_elements) (htab_t htab)
{
  return htab_elements (htab);
}

/* Return X % Y.  */

static inline hashval_t
htab_mod_1 (hashval_t x, hashval_t y, hashval_t inv, int shift)
{
  /* The multiplicative inverses computed above are for 32-bit types, and
     requires that we be able to compute a highpart multiply.  */
#ifdef UNSIGNED_64BIT_TYPE
  __extension__ typedef UNSIGNED_64BIT_TYPE ull;
  if (sizeof (hashval_t) * CHAR_BIT <= 32)
    {
      hashval_t t1, t2, t3, t4, q, r;

      t1 = ((ull)x * inv) >> 32;
      t2 = x - t1;
      t3 = t2 >> 1;
      t4 = t1 + t3;
      q  = t4 >> shift;
      r  = x - (q * y);

      return r;
    }
#endif

  /* Otherwise just use the native division routines.  */
  return x % y;
}

/* Compute the primary hash for HASH given HTAB's current size.  */

static inline hashval_t
htab_mod (hashval_t hash, htab_t htab)
{
  const struct prime_ent *p = &prime_tab[htab->size_prime_index];
  return htab_mod_1 (hash, p->prime, p->inv, p->shift);
}

/* Compute the secondary hash for HASH given HTAB's current size.  */

static inline hashval_t
htab_mod_m2 (hashval_t hash, htab_t htab)
{
  const struct prime_ent *p = &prime_tab[htab->size_prime_index];
  return 1 + htab_mod_1 (hash, p->prime - 2, p->inv_m2, p->shift);
}

/* This function creates table with length slightly longer than given
   source length.  Created hash table is initiated as empty (all the
   hash table entries are HTAB_EMPTY_ENTRY).  The function returns the
   created hash table, or NULL if memory allocation fails.  */

htab_t
htab_create_alloc (size_t size, htab_hash hash_f, htab_eq eq_f,
                   htab_del del_f, htab_alloc alloc_f, htab_free free_f)
{
  htab_t result;
  unsigned int size_prime_index;

  size_prime_index = higher_prime_index (size);
  size = prime_tab[size_prime_index].prime;

  result = (htab_t) (*alloc_f) (1, sizeof (struct htab));
  if (result == NULL)
    return NULL;
  result->entries = (PTR *) (*alloc_f) (size, sizeof (PTR));
  if (result->entries == NULL)
    {
      if (free_f != NULL)
	(*free_f) (result);
      return NULL;
    }
  result->size = size;
  result->size_prime_index = size_prime_index;
  result->hash_f = hash_f;
  result->eq_f = eq_f;
  result->del_f = del_f;
  result->alloc_f = alloc_f;
  result->free_f = free_f;
  return result;
}

/* As above, but use the variants of alloc_f and free_f which accept
   an extra argument.  */

htab_t
htab_create_alloc_ex (size_t size, htab_hash hash_f, htab_eq eq_f,
                      htab_del del_f, void *alloc_arg,
                      htab_alloc_with_arg alloc_f,
		      htab_free_with_arg free_f)
{
  htab_t result;
  unsigned int size_prime_index;

  size_prime_index = higher_prime_index (size);
  size = prime_tab[size_prime_index].prime;

  result = (htab_t) (*alloc_f) (alloc_arg, 1, sizeof (struct htab));
  if (result == NULL)
    return NULL;
  result->entries = (PTR *) (*alloc_f) (alloc_arg, size, sizeof (PTR));
  if (result->entries == NULL)
    {
      if (free_f != NULL)
	(*free_f) (alloc_arg, result);
      return NULL;
    }
  result->size = size;
  result->size_prime_index = size_prime_index;
  result->hash_f = hash_f;
  result->eq_f = eq_f;
  result->del_f = del_f;
  result->alloc_arg = alloc_arg;
  result->alloc_with_arg_f = alloc_f;
  result->free_with_arg_f = free_f;
  return result;
}

/* Update the function pointers and allocation parameter in the htab_t.  */

void
htab_set_functions_ex (htab_t htab, htab_hash hash_f, htab_eq eq_f,
                       htab_del del_f, PTR alloc_arg,
                       htab_alloc_with_arg alloc_f, htab_free_with_arg free_f)
{
  htab->hash_f = hash_f;
  htab->eq_f = eq_f;
  htab->del_f = del_f;
  htab->alloc_arg = alloc_arg;
  htab->alloc_with_arg_f = alloc_f;
  htab->free_with_arg_f = free_f;
}

/* These functions exist solely for backward compatibility.  */

#undef htab_create
htab_t
htab_create (size_t size, htab_hash hash_f, htab_eq eq_f, htab_del del_f)
{
  return htab_create_alloc (size, hash_f, eq_f, del_f, xcalloc, free);
}

htab_t
htab_try_create (size_t size, htab_hash hash_f, htab_eq eq_f, htab_del del_f)
{
  return htab_create_alloc (size, hash_f, eq_f, del_f, calloc, free);
}

/* This function frees all memory allocated for given hash table.
   Naturally the hash table must already exist. */

void
htab_delete (htab_t htab)
{
  size_t size = htab_size (htab);
  PTR *entries = htab->entries;
  int i;

  if (htab->del_f)
    for (i = size - 1; i >= 0; i--)
      if (entries[i] != HTAB_EMPTY_ENTRY && entries[i] != HTAB_DELETED_ENTRY)
	(*htab->del_f) (entries[i]);

  if (htab->free_f != NULL)
    {
      (*htab->free_f) (entries);
      (*htab->free_f) (htab);
    }
  else if (htab->free_with_arg_f != NULL)
    {
      (*htab->free_with_arg_f) (htab->alloc_arg, entries);
      (*htab->free_with_arg_f) (htab->alloc_arg, htab);
    }
}

/* This function clears all entries in the given hash table.  */

void
htab_empty (htab_t htab)
{
  size_t size = htab_size (htab);
  PTR *entries = htab->entries;
  int i;

  if (htab->del_f)
    for (i = size - 1; i >= 0; i--)
      if (entries[i] != HTAB_EMPTY_ENTRY && entries[i] != HTAB_DELETED_ENTRY)
	(*htab->del_f) (entries[i]);

  /* Instead of clearing megabyte, downsize the table.  */
  if (size > 1024*1024 / sizeof (PTR))
    {
      int nindex = higher_prime_index (1024 / sizeof (PTR));
      int nsize = prime_tab[nindex].prime;

      if (htab->free_f != NULL)
	(*htab->free_f) (htab->entries);
      else if (htab->free_with_arg_f != NULL)
	(*htab->free_with_arg_f) (htab->alloc_arg, htab->entries);
      if (htab->alloc_with_arg_f != NULL)
	htab->entries = (PTR *) (*htab->alloc_with_arg_f) (htab->alloc_arg, nsize,
						           sizeof (PTR *));
      else
	htab->entries = (PTR *) (*htab->alloc_f) (nsize, sizeof (PTR *));
     htab->size = nsize;
     htab->size_prime_index = nindex;
    }
  else
    memset (entries, 0, size * sizeof (PTR));
  htab->n_deleted = 0;
  htab->n_elements = 0;
}

/* Similar to htab_find_slot, but without several unwanted side effects:
    - Does not call htab->eq_f when it finds an existing entry.
    - Does not change the count of elements/searches/collisions in the
      hash table.
   This function also assumes there are no deleted entries in the table.
   HASH is the hash value for the element to be inserted.  */

static PTR *
find_empty_slot_for_expand (htab_t htab, hashval_t hash)
{
  hashval_t index = htab_mod (hash, htab);
  size_t size = htab_size (htab);
  PTR *slot = htab->entries + index;
  hashval_t hash2;

  if (*slot == HTAB_EMPTY_ENTRY)
    return slot;
  else if (*slot == HTAB_DELETED_ENTRY)
    abort ();

  hash2 = htab_mod_m2 (hash, htab);
  for (;;)
    {
      index += hash2;
      if (index >= size)
	index -= size;

      slot = htab->entries + index;
      if (*slot == HTAB_EMPTY_ENTRY)
	return slot;
      else if (*slot == HTAB_DELETED_ENTRY)
	abort ();
    }
}

/* The following function changes size of memory allocated for the
   entries and repeatedly inserts the table elements.  The occupancy
   of the table after the call will be about 50%.  Naturally the hash
   table must already exist.  Remember also that the place of the
   table entries is changed.  If memory allocation failures are allowed,
   this function will return zero, indicating that the table could not be
   expanded.  If all goes well, it will return a non-zero value.  */

static int
htab_expand (htab_t htab)
{
  PTR *oentries;
  PTR *olimit;
  PTR *p;
  PTR *nentries;
  size_t nsize, osize, elts;
  unsigned int oindex, nindex;

  oentries = htab->entries;
  oindex = htab->size_prime_index;
  osize = htab->size;
  olimit = oentries + osize;
  elts = htab_elements (htab);

  /* Resize only when table after removal of unused elements is either
     too full or too empty.  */
  if (elts * 2 > osize || (elts * 8 < osize && osize > 32))
    {
      nindex = higher_prime_index (elts * 2);
      nsize = prime_tab[nindex].prime;
    }
  else
    {
      nindex = oindex;
      nsize = osize;
    }

  if (htab->alloc_with_arg_f != NULL)
    nentries = (PTR *) (*htab->alloc_with_arg_f) (htab->alloc_arg, nsize,
						  sizeof (PTR *));
  else
    nentries = (PTR *) (*htab->alloc_f) (nsize, sizeof (PTR *));
  if (nentries == NULL)
    return 0;
  htab->entries = nentries;
  htab->size = nsize;
  htab->size_prime_index = nindex;
  htab->n_elements -= htab->n_deleted;
  htab->n_deleted = 0;

  p = oentries;
  do
    {
      PTR x = *p;

      if (x != HTAB_EMPTY_ENTRY && x != HTAB_DELETED_ENTRY)
	{
	  PTR *q = find_empty_slot_for_expand (htab, (*htab->hash_f) (x));

	  *q = x;
	}

      p++;
    }
  while (p < olimit);

  if (htab->free_f != NULL)
    (*htab->free_f) (oentries);
  else if (htab->free_with_arg_f != NULL)
    (*htab->free_with_arg_f) (htab->alloc_arg, oentries);
  return 1;
}

/* This function searches for a hash table entry equal to the given
   element.  It cannot be used to insert or delete an element.  */

PTR
htab_find_with_hash (htab_t htab, const PTR element, hashval_t hash)
{
  hashval_t index, hash2;
  size_t size;
  PTR entry;

  htab->searches++;
  size = htab_size (htab);
  index = htab_mod (hash, htab);

  entry = htab->entries[index];
  if (entry == HTAB_EMPTY_ENTRY
      || (entry != HTAB_DELETED_ENTRY && (*htab->eq_f) (entry, element)))
    return entry;

  hash2 = htab_mod_m2 (hash, htab);
  for (;;)
    {
      htab->collisions++;
      index += hash2;
      if (index >= size)
	index -= size;

      entry = htab->entries[index];
      if (entry == HTAB_EMPTY_ENTRY
	  || (entry != HTAB_DELETED_ENTRY && (*htab->eq_f) (entry, element)))
	return entry;
    }
}

/* Like htab_find_slot_with_hash, but compute the hash value from the
   element.  */

PTR
htab_find (htab_t htab, const PTR element)
{
  return htab_find_with_hash (htab, element, (*htab->hash_f) (element));
}

/* This function searches for a hash table slot containing an entry
   equal to the given element.  To delete an entry, call this with
   insert=NO_INSERT, then call htab_clear_slot on the slot returned
   (possibly after doing some checks).  To insert an entry, call this
   with insert=INSERT, then write the value you want into the returned
   slot.  When inserting an entry, NULL may be returned if memory
   allocation fails.  */

PTR *
htab_find_slot_with_hash (htab_t htab, const PTR element,
                          hashval_t hash, enum insert_option insert)
{
  PTR *first_deleted_slot;
  hashval_t index, hash2;
  size_t size;
  PTR entry;

  size = htab_size (htab);
  if (insert == INSERT && size * 3 <= htab->n_elements * 4)
    {
      if (htab_expand (htab) == 0)
	return NULL;
      size = htab_size (htab);
    }

  index = htab_mod (hash, htab);

  htab->searches++;
  first_deleted_slot = NULL;

  entry = htab->entries[index];
  if (entry == HTAB_EMPTY_ENTRY)
    goto empty_entry;
  else if (entry == HTAB_DELETED_ENTRY)
    first_deleted_slot = &htab->entries[index];
  else if ((*htab->eq_f) (entry, element))
    return &htab->entries[index];
      
  hash2 = htab_mod_m2 (hash, htab);
  for (;;)
    {
      htab->collisions++;
      index += hash2;
      if (index >= size)
	index -= size;
      
      entry = htab->entries[index];
      if (entry == HTAB_EMPTY_ENTRY)
	goto empty_entry;
      else if (entry == HTAB_DELETED_ENTRY)
	{
	  if (!first_deleted_slot)
	    first_deleted_slot = &htab->entries[index];
	}
      else if ((*htab->eq_f) (entry, element))
	return &htab->entries[index];
    }

 empty_entry:
  if (insert == NO_INSERT)
    return NULL;

  if (first_deleted_slot)
    {
      htab->n_deleted--;
      *first_deleted_slot = HTAB_EMPTY_ENTRY;
      return first_deleted_slot;
    }

  htab->n_elements++;
  return &htab->entries[index];
}

/* Like htab_find_slot_with_hash, but compute the hash value from the
   element.  */

PTR *
htab_find_slot (htab_t htab, const PTR element, enum insert_option insert)
{
  return htab_find_slot_with_hash (htab, element, (*htab->hash_f) (element),
				   insert);
}

/* This function deletes an element with the given value from hash
   table (the hash is computed from the element).  If there is no matching
   element in the hash table, this function does nothing.  */

void
htab_remove_elt (htab_t htab, PTR element)
{
  htab_remove_elt_with_hash (htab, element, (*htab->hash_f) (element));
}


/* This function deletes an element with the given value from hash
   table.  If there is no matching element in the hash table, this
   function does nothing.  */

void
htab_remove_elt_with_hash (htab_t htab, PTR element, hashval_t hash)
{
  PTR *slot;

  slot = htab_find_slot_with_hash (htab, element, hash, NO_INSERT);
  if (*slot == HTAB_EMPTY_ENTRY)
    return;

  if (htab->del_f)
    (*htab->del_f) (*slot);

  *slot = HTAB_DELETED_ENTRY;
  htab->n_deleted++;
}

/* This function clears a specified slot in a hash table.  It is
   useful when you've already done the lookup and don't want to do it
   again.  */

void
htab_clear_slot (htab_t htab, PTR *slot)
{
  if (slot < htab->entries || slot >= htab->entries + htab_size (htab)
      || *slot == HTAB_EMPTY_ENTRY || *slot == HTAB_DELETED_ENTRY)
    abort ();

  if (htab->del_f)
    (*htab->del_f) (*slot);

  *slot = HTAB_DELETED_ENTRY;
  htab->n_deleted++;
}

/* This function scans over the entire hash table calling
   CALLBACK for each live entry.  If CALLBACK returns false,
   the iteration stops.  INFO is passed as CALLBACK's second
   argument.  */

void
htab_traverse_noresize (htab_t htab, htab_trav callback, PTR info)
{
  PTR *slot;
  PTR *limit;
  
  slot = htab->entries;
  limit = slot + htab_size (htab);

  do
    {
      PTR x = *slot;

      if (x != HTAB_EMPTY_ENTRY && x != HTAB_DELETED_ENTRY)
	if (!(*callback) (slot, info))
	  break;
    }
  while (++slot < limit);
}

/* Like htab_traverse_noresize, but does resize the table when it is
   too empty to improve effectivity of subsequent calls.  */

void
htab_traverse (htab_t htab, htab_trav callback, PTR info)
{
  if (htab_elements (htab) * 8 < htab_size (htab))
    htab_expand (htab);

  htab_traverse_noresize (htab, callback, info);
}

/* Return the fraction of fixed collisions during all work with given
   hash table. */

double
htab_collisions (htab_t htab)
{
  if (htab->searches == 0)
    return 0.0;

  return (double) htab->collisions / (double) htab->searches;
}

/* Hash P as a null-terminated string.

   Copied from gcc/hashtable.c.  Zack had the following to say with respect
   to applicability, though note that unlike hashtable.c, this hash table
   implementation re-hashes rather than chain buckets.

   http://gcc.gnu.org/ml/gcc-patches/2001-08/msg01021.html
   From: Zack Weinberg <zackw@panix.com>
   Date: Fri, 17 Aug 2001 02:15:56 -0400

   I got it by extracting all the identifiers from all the source code
   I had lying around in mid-1999, and testing many recurrences of
   the form "H_n = H_{n-1} * K + c_n * L + M" where K, L, M were either
   prime numbers or the appropriate identity.  This was the best one.
   I don't remember exactly what constituted "best", except I was
   looking at bucket-length distributions mostly.
   
   So it should be very good at hashing identifiers, but might not be
   as good at arbitrary strings.
   
   I'll add that it thoroughly trounces the hash functions recommended
   for this use at http://burtleburtle.net/bob/hash/index.html, both
   on speed and bucket distribution.  I haven't tried it against the
   function they just started using for Perl's hashes.  */

hashval_t
htab_hash_string (const PTR p)
{
  const unsigned char *str = (const unsigned char *) p;
  hashval_t r = 0;
  unsigned char c;

  while ((c = *str++) != 0)
    r = r * 67 + c - 113;

  return r;
}

/* DERIVED FROM:
--------------------------------------------------------------------
lookup2.c, by Bob Jenkins, December 1996, Public Domain.
hash(), hash2(), hash3, and mix() are externally useful functions.
Routines to test the hash are included if SELF_TEST is defined.
You can use this free for any purpose.  It has no warranty.
--------------------------------------------------------------------
*/

/*
--------------------------------------------------------------------
mix -- mix 3 32-bit values reversibly.
For every delta with one or two bit set, and the deltas of all three
  high bits or all three low bits, whether the original value of a,b,c
  is almost all zero or is uniformly distributed,
* If mix() is run forward or backward, at least 32 bits in a,b,c
  have at least 1/4 probability of changing.
* If mix() is run forward, every bit of c will change between 1/3 and
  2/3 of the time.  (Well, 22/100 and 78/100 for some 2-bit deltas.)
mix() was built out of 36 single-cycle latency instructions in a 
  structure that could supported 2x parallelism, like so:
      a -= b; 
      a -= c; x = (c>>13);
      b -= c; a ^= x;
      b -= a; x = (a<<8);
      c -= a; b ^= x;
      c -= b; x = (b>>13);
      ...
  Unfortunately, superscalar Pentiums and Sparcs can't take advantage 
  of that parallelism.  They've also turned some of those single-cycle
  latency instructions into multi-cycle latency instructions.  Still,
  this is the fastest good hash I could find.  There were about 2^^68
  to choose from.  I only looked at a billion or so.
--------------------------------------------------------------------
*/
/* same, but slower, works on systems that might have 8 byte hashval_t's */
#define mix(a,b,c) \
{ \
  a -= b; a -= c; a ^= (c>>13); \
  b -= c; b -= a; b ^= (a<< 8); \
  c -= a; c -= b; c ^= ((b&0xffffffff)>>13); \
  a -= b; a -= c; a ^= ((c&0xffffffff)>>12); \
  b -= c; b -= a; b = (b ^ (a<<16)) & 0xffffffff; \
  c -= a; c -= b; c = (c ^ (b>> 5)) & 0xffffffff; \
  a -= b; a -= c; a = (a ^ (c>> 3)) & 0xffffffff; \
  b -= c; b -= a; b = (b ^ (a<<10)) & 0xffffffff; \
  c -= a; c -= b; c = (c ^ (b>>15)) & 0xffffffff; \
}

/*
--------------------------------------------------------------------
hash() -- hash a variable-length key into a 32-bit value
  k     : the key (the unaligned variable-length array of bytes)
  len   : the length of the key, counting by bytes
  level : can be any 4-byte value
Returns a 32-bit value.  Every bit of the key affects every bit of
the return value.  Every 1-bit and 2-bit delta achieves avalanche.
About 36+6len instructions.

The best hash table sizes are powers of 2.  There is no need to do
mod a prime (mod is sooo slow!).  If you need less than 32 bits,
use a bitmask.  For example, if you need only 10 bits, do
  h = (h & hashmask(10));
In which case, the hash table should have hashsize(10) elements.

If you are hashing n strings (ub1 **)k, do it like this:
  for (i=0, h=0; i<n; ++i) h = hash( k[i], len[i], h);

By Bob Jenkins, 1996.  bob_jenkins@burtleburtle.net.  You may use this
code any way you wish, private, educational, or commercial.  It's free.

See http://burtleburtle.net/bob/hash/evahash.html
Use for hash table lookup, or anything where one collision in 2^32 is
acceptable.  Do NOT use for cryptographic purposes.
--------------------------------------------------------------------
*/

hashval_t
iterative_hash (const PTR k_in /* the key */,
                register size_t  length /* the length of the key */,
                register hashval_t initval /* the previous hash, or
                                              an arbitrary value */)
{
  register const unsigned char *k = (const unsigned char *)k_in;
  register hashval_t a,b,c,len;

  /* Set up the internal state */
  len = length;
  a = b = 0x9e3779b9;  /* the golden ratio; an arbitrary value */
  c = initval;           /* the previous hash value */

  /*---------------------------------------- handle most of the key */
#ifndef WORDS_BIGENDIAN
  /* On a little-endian machine, if the data is 4-byte aligned we can hash
     by word for better speed.  This gives nondeterministic results on
     big-endian machines.  */
  if (sizeof (hashval_t) == 4 && (((size_t)k)&3) == 0)
    while (len >= 12)    /* aligned */
      {
	a += *(hashval_t *)(k+0);
	b += *(hashval_t *)(k+4);
	c += *(hashval_t *)(k+8);
	mix(a,b,c);
	k += 12; len -= 12;
      }
  else /* unaligned */
#endif
    while (len >= 12)
      {
	a += (k[0] +((hashval_t)k[1]<<8) +((hashval_t)k[2]<<16) +((hashval_t)k[3]<<24));
	b += (k[4] +((hashval_t)k[5]<<8) +((hashval_t)k[6]<<16) +((hashval_t)k[7]<<24));
	c += (k[8] +((hashval_t)k[9]<<8) +((hashval_t)k[10]<<16)+((hashval_t)k[11]<<24));
	mix(a,b,c);
	k += 12; len -= 12;
      }

  /*------------------------------------- handle the last 11 bytes */
  c += length;
  switch(len)              /* all the case statements fall through */
    {
    case 11: c+=((hashval_t)k[10]<<24);
    case 10: c+=((hashval_t)k[9]<<16);
    case 9 : c+=((hashval_t)k[8]<<8);
      /* the first byte of c is reserved for the length */
    case 8 : b+=((hashval_t)k[7]<<24);
    case 7 : b+=((hashval_t)k[6]<<16);
    case 6 : b+=((hashval_t)k[5]<<8);
    case 5 : b+=k[4];
    case 4 : a+=((hashval_t)k[3]<<24);
    case 3 : a+=((hashval_t)k[2]<<16);
    case 2 : a+=((hashval_t)k[1]<<8);
    case 1 : a+=k[0];
      /* case 0: nothing left to add */
    }
  mix(a,b,c);
  /*-------------------------------------------- report the result */
  return c;
}
