/* Set operations on pointers
   Copyright (C) 2004, 2006 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "pointer-set.h"

/* A pointer set is represented as a simple open-addressing hash
   table.  Simplifications: The hash code is based on the value of the
   pointer, not what it points to.  The number of buckets is always a
   power of 2.  Null pointers are a reserved value.  Deletion is not
   supported (yet).  There is no mechanism for user control of hash
   function, equality comparison, initial size, or resizing policy.  */

struct pointer_set_t
{
  size_t log_slots;
  size_t n_slots;		/* n_slots = 2^log_slots */
  size_t n_elements;

  void **slots;
};

/* Use the multiplicative method, as described in Knuth 6.4, to obtain
   a hash code for P in the range [0, MAX).  MAX == 2^LOGMAX.

   Summary of this method: Multiply p by some number A that's
   relatively prime to 2^sizeof(size_t).  The result is two words.
   Discard the most significant word, and return the most significant
   N bits of the least significant word.  As suggested by Knuth, our
   choice for A is the integer part of (ULONG_MAX + 1.0) / phi, where phi
   is the golden ratio.

   We don't need to do anything special for full-width multiplication
   because we're only interested in the least significant word of the
   product, and unsigned arithmetic in C is modulo the word size.  */

static inline size_t
hash1 (const void *p, unsigned long max, unsigned long logmax)
{
#if HOST_BITS_PER_LONG == 32
  const unsigned long A = 0x9e3779b9u;
#elif HOST_BITS_PER_LONG == 64
  const unsigned long A = 0x9e3779b97f4a7c16ul;
#else
  const unsigned long A
    = (ULONG_MAX + 1.0L) * 0.6180339887498948482045868343656381177203L;
#endif
  const unsigned long shift = HOST_BITS_PER_LONG - logmax;

  return ((A * (unsigned long) p) >> shift) & (max - 1);
}

/* Allocate an empty pointer set.  */
struct pointer_set_t *
pointer_set_create (void)
{
  struct pointer_set_t *result = XNEW (struct pointer_set_t);

  result->n_elements = 0;
  result->log_slots = 8;
  result->n_slots = (size_t) 1 << result->log_slots;

  result->slots = XCNEWVEC (void *, result->n_slots);
  return result;
}

/* Reclaims all memory associated with PSET.  */
void
pointer_set_destroy (struct pointer_set_t *pset)
{
  XDELETEVEC (pset->slots);
  XDELETE (pset);
}

/* Returns nonzero if PSET contains P.  P must be nonnull.

   Collisions are resolved by linear probing.  */
int
pointer_set_contains (struct pointer_set_t *pset, void *p)
{
  size_t n = hash1 (p, pset->n_slots, pset->log_slots);

  while (true)
    {
      if (pset->slots[n] == p)
       return 1;
      else if (pset->slots[n] == 0)
       return 0;
      else
       {
         ++n;
         if (n == pset->n_slots)
           n = 0;
       }
    }
}

/* Subroutine of pointer_set_insert.  Return the insertion slot for P into
   an empty element of SLOTS, an array of length N_SLOTS.  */
static inline size_t
insert_aux (void *p, void **slots, size_t n_slots, size_t log_slots)
{
  size_t n = hash1 (p, n_slots, log_slots);
  while (true)
    {
      if (slots[n] == p || slots[n] == 0)
	return n;
      else
	{
	  ++n;
	  if (n == n_slots)
	    n = 0;
	}
    }
}

/* Inserts P into PSET if it wasn't already there.  Returns nonzero
   if it was already there. P must be nonnull.  */
int
pointer_set_insert (struct pointer_set_t *pset, void *p)
{
  size_t n;

  /* For simplicity, expand the set even if P is already there.  This can be
     superfluous but can happen at most once.  */
  if (pset->n_elements > pset->n_slots / 4)
    {
      size_t new_log_slots = pset->log_slots + 1;
      size_t new_n_slots = pset->n_slots * 2;
      void **new_slots = XCNEWVEC (void *, new_n_slots);
      size_t i;

      for (i = 0; i < pset->n_slots; ++i)
        {
	  void *value = pset->slots[i];
	  n = insert_aux (value, new_slots, new_n_slots, new_log_slots);
	  new_slots[n] = value;
	}

      XDELETEVEC (pset->slots);
      pset->n_slots = new_n_slots;
      pset->log_slots = new_log_slots;
      pset->slots = new_slots;
    }

  n = insert_aux (p, pset->slots, pset->n_slots, pset->log_slots);
  if (pset->slots[n])
    return 1;

  pset->slots[n] = p;
  ++pset->n_elements;
  return 0;
}

/* Pass each pointer in PSET to the function in FN, together with the fixed
   parameter DATA.  If FN returns false, the iteration stops.  */

void pointer_set_traverse (struct pointer_set_t *pset,
			   bool (*fn) (void *, void *), void *data)
{
  size_t i;
  for (i = 0; i < pset->n_slots; ++i)
    if (pset->slots[i] && !fn (pset->slots[i], data))
      break;
}


/* A pointer map is represented the same way as a pointer_set, so
   the hash code is based on the address of the key, rather than
   its contents.  Null keys are a reserved value.  Deletion is not
   supported (yet).  There is no mechanism for user control of hash
   function, equality comparison, initial size, or resizing policy.  */

struct pointer_map_t
{
  size_t log_slots;
  size_t n_slots;		/* n_slots = 2^log_slots */
  size_t n_elements;

  void **keys;
  void **values;
};

/* Allocate an empty pointer map.  */
struct pointer_map_t *
pointer_map_create (void)
{
  struct pointer_map_t *result = XNEW (struct pointer_map_t);

  result->n_elements = 0;
  result->log_slots = 8;
  result->n_slots = (size_t) 1 << result->log_slots;

  result->keys = XCNEWVEC (void *, result->n_slots);
  result->values = XCNEWVEC (void *, result->n_slots);
  return result;
}

/* Reclaims all memory associated with PMAP.  */
void pointer_map_destroy (struct pointer_map_t *pmap)
{
  XDELETEVEC (pmap->keys);
  XDELETEVEC (pmap->values);
  XDELETE (pmap);
}

/* Returns a pointer to the value to which P maps, if PMAP contains P.  P
   must be nonnull.  Return NULL if PMAP does not contain P.

   Collisions are resolved by linear probing.  */
void **
pointer_map_contains (struct pointer_map_t *pmap, void *p)
{
  size_t n = hash1 (p, pmap->n_slots, pmap->log_slots);

  while (true)
    {
      if (pmap->keys[n] == p)
	return &pmap->values[n];
      else if (pmap->keys[n] == 0)
	return NULL;
      else
       {
         ++n;
         if (n == pmap->n_slots)
           n = 0;
       }
    }
}

/* Inserts P into PMAP if it wasn't already there.  Returns a pointer
   to the value.  P must be nonnull.  */
void **
pointer_map_insert (struct pointer_map_t *pmap, void *p)
{
  size_t n;

  /* For simplicity, expand the map even if P is already there.  This can be
     superfluous but can happen at most once.  */
  if (pmap->n_elements > pmap->n_slots / 4)
    {
      size_t new_log_slots = pmap->log_slots + 1;
      size_t new_n_slots = pmap->n_slots * 2;
      void **new_keys = XCNEWVEC (void *, new_n_slots);
      void **new_values = XCNEWVEC (void *, new_n_slots);
      size_t i;

      for (i = 0; i < pmap->n_slots; ++i)
	if (pmap->keys[i])
	  {
	    void *key = pmap->keys[i];
	    n = insert_aux (key, new_keys, new_n_slots, new_log_slots);
	    new_keys[n] = key;
	    new_values[n] = pmap->values[i];
	  }

      XDELETEVEC (pmap->keys);
      XDELETEVEC (pmap->values);
      pmap->n_slots = new_n_slots;
      pmap->log_slots = new_log_slots;
      pmap->keys = new_keys;
      pmap->values = new_values;
    }

  n = insert_aux (p, pmap->keys, pmap->n_slots, pmap->log_slots);
  if (!pmap->keys[n])
    {
      ++pmap->n_elements;
      pmap->keys[n] = p;
    }

  return &pmap->values[n];
}

/* Pass each pointer in PMAP to the function in FN, together with the pointer
   to the value and the fixed parameter DATA.  If FN returns false, the
   iteration stops.  */

void pointer_map_traverse (struct pointer_map_t *pmap,
			   bool (*fn) (void *, void **, void *), void *data)
{
  size_t i;
  for (i = 0; i < pmap->n_slots; ++i)
    if (pmap->keys[i] && !fn (pmap->keys[i], &pmap->values[i], data))
      break;
}
