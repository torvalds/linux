/* Functions to support general ended bitmaps.
   Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#ifndef GCC_BITMAP_H
#define GCC_BITMAP_H
#include "hashtab.h"

/* Fundamental storage type for bitmap.  */

typedef unsigned long BITMAP_WORD;
/* BITMAP_WORD_BITS needs to be unsigned, but cannot contain casts as
   it is used in preprocessor directives -- hence the 1u.  */
#define BITMAP_WORD_BITS (CHAR_BIT * SIZEOF_LONG * 1u)

/* Number of words to use for each element in the linked list.  */

#ifndef BITMAP_ELEMENT_WORDS
#define BITMAP_ELEMENT_WORDS ((128 + BITMAP_WORD_BITS - 1) / BITMAP_WORD_BITS)
#endif

/* Number of bits in each actual element of a bitmap.  */

#define BITMAP_ELEMENT_ALL_BITS (BITMAP_ELEMENT_WORDS * BITMAP_WORD_BITS)

/* Obstack for allocating bitmaps and elements from.  */
typedef struct bitmap_obstack GTY (())
{
  struct bitmap_element_def *elements;
  struct bitmap_head_def *heads;
  struct obstack GTY ((skip)) obstack;
} bitmap_obstack;

/* Bitmap set element.  We use a linked list to hold only the bits that
   are set.  This allows for use to grow the bitset dynamically without
   having to realloc and copy a giant bit array.

   The free list is implemented as a list of lists.  There is one
   outer list connected together by prev fields.  Each element of that
   outer is an inner list (that may consist only of the outer list
   element) that are connected by the next fields.  The prev pointer
   is undefined for interior elements.  This allows
   bitmap_elt_clear_from to be implemented in unit time rather than
   linear in the number of elements to be freed.  */

typedef struct bitmap_element_def GTY(())
{
  struct bitmap_element_def *next;		/* Next element.  */
  struct bitmap_element_def *prev;		/* Previous element.  */
  unsigned int indx;			/* regno/BITMAP_ELEMENT_ALL_BITS.  */
  BITMAP_WORD bits[BITMAP_ELEMENT_WORDS]; /* Bits that are set.  */
} bitmap_element;

/* Head of bitmap linked list.  */
typedef struct bitmap_head_def GTY(()) {
  bitmap_element *first;	/* First element in linked list.  */
  bitmap_element *current;	/* Last element looked at.  */
  unsigned int indx;		/* Index of last element looked at.  */
  bitmap_obstack *obstack;	/* Obstack to allocate elements from.
				   If NULL, then use ggc_alloc.  */
} bitmap_head;


/* Global data */
extern bitmap_element bitmap_zero_bits;	/* Zero bitmap element */
extern bitmap_obstack bitmap_default_obstack;   /* Default bitmap obstack */

/* Clear a bitmap by freeing up the linked list.  */
extern void bitmap_clear (bitmap);

/* Copy a bitmap to another bitmap.  */
extern void bitmap_copy (bitmap, bitmap);

/* True if two bitmaps are identical.  */
extern bool bitmap_equal_p (bitmap, bitmap);

/* True if the bitmaps intersect (their AND is non-empty).  */
extern bool bitmap_intersect_p (bitmap, bitmap);

/* True if the complement of the second intersects the first (their
   AND_COMPL is non-empty).  */
extern bool bitmap_intersect_compl_p (bitmap, bitmap);

/* True if MAP is an empty bitmap.  */
#define bitmap_empty_p(MAP) (!(MAP)->first)

/* Count the number of bits set in the bitmap.  */
extern unsigned long bitmap_count_bits (bitmap);

/* Boolean operations on bitmaps.  The _into variants are two operand
   versions that modify the first source operand.  The other variants
   are three operand versions that to not destroy the source bitmaps.
   The operations supported are &, & ~, |, ^.  */
extern void bitmap_and (bitmap, bitmap, bitmap);
extern void bitmap_and_into (bitmap, bitmap);
extern void bitmap_and_compl (bitmap, bitmap, bitmap);
extern bool bitmap_and_compl_into (bitmap, bitmap);
#define bitmap_compl_and(DST, A, B) bitmap_and_compl (DST, B, A)
extern void bitmap_compl_and_into (bitmap, bitmap);
extern void bitmap_clear_range (bitmap, unsigned int, unsigned int);
extern bool bitmap_ior (bitmap, bitmap, bitmap);
extern bool bitmap_ior_into (bitmap, bitmap);
extern void bitmap_xor (bitmap, bitmap, bitmap);
extern void bitmap_xor_into (bitmap, bitmap);

/* DST = A | (B & ~C).  Return true if DST changes.  */
extern bool bitmap_ior_and_compl (bitmap DST, bitmap A, bitmap B, bitmap C);
/* A |= (B & ~C).  Return true if A changes.  */
extern bool bitmap_ior_and_compl_into (bitmap DST, bitmap B, bitmap C);

/* Clear a single register in a register set.  */
extern void bitmap_clear_bit (bitmap, int);

/* Set a single register in a register set.  */
extern void bitmap_set_bit (bitmap, int);

/* Return true if a register is set in a register set.  */
extern int bitmap_bit_p (bitmap, int);

/* Debug functions to print a bitmap linked list.  */
extern void debug_bitmap (bitmap);
extern void debug_bitmap_file (FILE *, bitmap);

/* Print a bitmap.  */
extern void bitmap_print (FILE *, bitmap, const char *, const char *);

/* Initialize and release a bitmap obstack.  */
extern void bitmap_obstack_initialize (bitmap_obstack *);
extern void bitmap_obstack_release (bitmap_obstack *);

/* Initialize a bitmap header.  OBSTACK indicates the bitmap obstack
   to allocate from, NULL for GC'd bitmap.  */

static inline void
bitmap_initialize (bitmap head, bitmap_obstack *obstack)
{
  head->first = head->current = NULL;
  head->obstack = obstack;
}

/* Allocate and free bitmaps from obstack, malloc and gc'd memory.  */
extern bitmap bitmap_obstack_alloc (bitmap_obstack *obstack);
extern bitmap bitmap_gc_alloc (void);
extern void bitmap_obstack_free (bitmap);

/* A few compatibility/functions macros for compatibility with sbitmaps */
#define dump_bitmap(file, bitmap) bitmap_print (file, bitmap, "", "\n")
#define bitmap_zero(a) bitmap_clear (a)
extern unsigned bitmap_first_set_bit (bitmap);

/* Compute bitmap hash (for purposes of hashing etc.)  */
extern hashval_t bitmap_hash(bitmap);

/* Allocate a bitmap from a bit obstack.  */
#define BITMAP_ALLOC(OBSTACK) bitmap_obstack_alloc (OBSTACK)

/* Allocate a gc'd bitmap.  */
#define BITMAP_GGC_ALLOC() bitmap_gc_alloc ()

/* Do any cleanup needed on a bitmap when it is no longer used.  */
#define BITMAP_FREE(BITMAP)			\
	((void)(bitmap_obstack_free (BITMAP), (BITMAP) = NULL))

/* Iterator for bitmaps.  */

typedef struct
{
  /* Pointer to the current bitmap element.  */
  bitmap_element *elt1;

  /* Pointer to 2nd bitmap element when two are involved.  */
  bitmap_element *elt2;

  /* Word within the current element.  */
  unsigned word_no;

  /* Contents of the actually processed word.  When finding next bit
     it is shifted right, so that the actual bit is always the least
     significant bit of ACTUAL.  */
  BITMAP_WORD bits;
} bitmap_iterator;

/* Initialize a single bitmap iterator.  START_BIT is the first bit to
   iterate from.  */

static inline void
bmp_iter_set_init (bitmap_iterator *bi, bitmap map,
		   unsigned start_bit, unsigned *bit_no)
{
  bi->elt1 = map->first;
  bi->elt2 = NULL;

  /* Advance elt1 until it is not before the block containing start_bit.  */
  while (1)
    {
      if (!bi->elt1)
	{
	  bi->elt1 = &bitmap_zero_bits;
	  break;
	}

      if (bi->elt1->indx >= start_bit / BITMAP_ELEMENT_ALL_BITS)
	break;
      bi->elt1 = bi->elt1->next;
    }

  /* We might have gone past the start bit, so reinitialize it.  */
  if (bi->elt1->indx != start_bit / BITMAP_ELEMENT_ALL_BITS)
    start_bit = bi->elt1->indx * BITMAP_ELEMENT_ALL_BITS;

  /* Initialize for what is now start_bit.  */
  bi->word_no = start_bit / BITMAP_WORD_BITS % BITMAP_ELEMENT_WORDS;
  bi->bits = bi->elt1->bits[bi->word_no];
  bi->bits >>= start_bit % BITMAP_WORD_BITS;

  /* If this word is zero, we must make sure we're not pointing at the
     first bit, otherwise our incrementing to the next word boundary
     will fail.  It won't matter if this increment moves us into the
     next word.  */
  start_bit += !bi->bits;

  *bit_no = start_bit;
}

/* Initialize an iterator to iterate over the intersection of two
   bitmaps.  START_BIT is the bit to commence from.  */

static inline void
bmp_iter_and_init (bitmap_iterator *bi, bitmap map1, bitmap map2,
		   unsigned start_bit, unsigned *bit_no)
{
  bi->elt1 = map1->first;
  bi->elt2 = map2->first;

  /* Advance elt1 until it is not before the block containing
     start_bit.  */
  while (1)
    {
      if (!bi->elt1)
	{
	  bi->elt2 = NULL;
	  break;
	}

      if (bi->elt1->indx >= start_bit / BITMAP_ELEMENT_ALL_BITS)
	break;
      bi->elt1 = bi->elt1->next;
    }

  /* Advance elt2 until it is not before elt1.  */
  while (1)
    {
      if (!bi->elt2)
	{
	  bi->elt1 = bi->elt2 = &bitmap_zero_bits;
	  break;
	}

      if (bi->elt2->indx >= bi->elt1->indx)
	break;
      bi->elt2 = bi->elt2->next;
    }

  /* If we're at the same index, then we have some intersecting bits.  */
  if (bi->elt1->indx == bi->elt2->indx)
    {
      /* We might have advanced beyond the start_bit, so reinitialize
	 for that.  */
      if (bi->elt1->indx != start_bit / BITMAP_ELEMENT_ALL_BITS)
	start_bit = bi->elt1->indx * BITMAP_ELEMENT_ALL_BITS;

      bi->word_no = start_bit / BITMAP_WORD_BITS % BITMAP_ELEMENT_WORDS;
      bi->bits = bi->elt1->bits[bi->word_no] & bi->elt2->bits[bi->word_no];
      bi->bits >>= start_bit % BITMAP_WORD_BITS;
    }
  else
    {
      /* Otherwise we must immediately advance elt1, so initialize for
	 that.  */
      bi->word_no = BITMAP_ELEMENT_WORDS - 1;
      bi->bits = 0;
    }

  /* If this word is zero, we must make sure we're not pointing at the
     first bit, otherwise our incrementing to the next word boundary
     will fail.  It won't matter if this increment moves us into the
     next word.  */
  start_bit += !bi->bits;

  *bit_no = start_bit;
}

/* Initialize an iterator to iterate over the bits in MAP1 & ~MAP2.
   */

static inline void
bmp_iter_and_compl_init (bitmap_iterator *bi, bitmap map1, bitmap map2,
			 unsigned start_bit, unsigned *bit_no)
{
  bi->elt1 = map1->first;
  bi->elt2 = map2->first;

  /* Advance elt1 until it is not before the block containing start_bit.  */
  while (1)
    {
      if (!bi->elt1)
	{
	  bi->elt1 = &bitmap_zero_bits;
	  break;
	}

      if (bi->elt1->indx >= start_bit / BITMAP_ELEMENT_ALL_BITS)
	break;
      bi->elt1 = bi->elt1->next;
    }

  /* Advance elt2 until it is not before elt1.  */
  while (bi->elt2 && bi->elt2->indx < bi->elt1->indx)
    bi->elt2 = bi->elt2->next;

  /* We might have advanced beyond the start_bit, so reinitialize for
     that.  */
  if (bi->elt1->indx != start_bit / BITMAP_ELEMENT_ALL_BITS)
    start_bit = bi->elt1->indx * BITMAP_ELEMENT_ALL_BITS;

  bi->word_no = start_bit / BITMAP_WORD_BITS % BITMAP_ELEMENT_WORDS;
  bi->bits = bi->elt1->bits[bi->word_no];
  if (bi->elt2 && bi->elt1->indx == bi->elt2->indx)
    bi->bits &= ~bi->elt2->bits[bi->word_no];
  bi->bits >>= start_bit % BITMAP_WORD_BITS;

  /* If this word is zero, we must make sure we're not pointing at the
     first bit, otherwise our incrementing to the next word boundary
     will fail.  It won't matter if this increment moves us into the
     next word.  */
  start_bit += !bi->bits;

  *bit_no = start_bit;
}

/* Advance to the next bit in BI.  We don't advance to the next
   nonzero bit yet.  */

static inline void
bmp_iter_next (bitmap_iterator *bi, unsigned *bit_no)
{
  bi->bits >>= 1;
  *bit_no += 1;
}

/* Advance to the next nonzero bit of a single bitmap, we will have
   already advanced past the just iterated bit.  Return true if there
   is a bit to iterate.  */

static inline bool
bmp_iter_set (bitmap_iterator *bi, unsigned *bit_no)
{
  /* If our current word is nonzero, it contains the bit we want.  */
  if (bi->bits)
    {
    next_bit:
      while (!(bi->bits & 1))
	{
	  bi->bits >>= 1;
	  *bit_no += 1;
	}
      return true;
    }

  /* Round up to the word boundary.  We might have just iterated past
     the end of the last word, hence the -1.  It is not possible for
     bit_no to point at the beginning of the now last word.  */
  *bit_no = ((*bit_no + BITMAP_WORD_BITS - 1)
	     / BITMAP_WORD_BITS * BITMAP_WORD_BITS);
  bi->word_no++;

  while (1)
    {
      /* Find the next nonzero word in this elt.  */
      while (bi->word_no != BITMAP_ELEMENT_WORDS)
	{
	  bi->bits = bi->elt1->bits[bi->word_no];
	  if (bi->bits)
	    goto next_bit;
	  *bit_no += BITMAP_WORD_BITS;
	  bi->word_no++;
	}

      /* Advance to the next element.  */
      bi->elt1 = bi->elt1->next;
      if (!bi->elt1)
	return false;
      *bit_no = bi->elt1->indx * BITMAP_ELEMENT_ALL_BITS;
      bi->word_no = 0;
    }
}

/* Advance to the next nonzero bit of an intersecting pair of
   bitmaps.  We will have already advanced past the just iterated bit.
   Return true if there is a bit to iterate.  */

static inline bool
bmp_iter_and (bitmap_iterator *bi, unsigned *bit_no)
{
  /* If our current word is nonzero, it contains the bit we want.  */
  if (bi->bits)
    {
    next_bit:
      while (!(bi->bits & 1))
	{
	  bi->bits >>= 1;
	  *bit_no += 1;
	}
      return true;
    }

  /* Round up to the word boundary.  We might have just iterated past
     the end of the last word, hence the -1.  It is not possible for
     bit_no to point at the beginning of the now last word.  */
  *bit_no = ((*bit_no + BITMAP_WORD_BITS - 1)
	     / BITMAP_WORD_BITS * BITMAP_WORD_BITS);
  bi->word_no++;

  while (1)
    {
      /* Find the next nonzero word in this elt.  */
      while (bi->word_no != BITMAP_ELEMENT_WORDS)
	{
	  bi->bits = bi->elt1->bits[bi->word_no] & bi->elt2->bits[bi->word_no];
	  if (bi->bits)
	    goto next_bit;
	  *bit_no += BITMAP_WORD_BITS;
	  bi->word_no++;
	}

      /* Advance to the next identical element.  */
      do
	{
	  /* Advance elt1 while it is less than elt2.  We always want
	     to advance one elt.  */
	  do
	    {
	      bi->elt1 = bi->elt1->next;
	      if (!bi->elt1)
		return false;
	    }
	  while (bi->elt1->indx < bi->elt2->indx);

	  /* Advance elt2 to be no less than elt1.  This might not
	     advance.  */
	  while (bi->elt2->indx < bi->elt1->indx)
	    {
	      bi->elt2 = bi->elt2->next;
	      if (!bi->elt2)
		return false;
	    }
	}
      while (bi->elt1->indx != bi->elt2->indx);

      *bit_no = bi->elt1->indx * BITMAP_ELEMENT_ALL_BITS;
      bi->word_no = 0;
    }
}

/* Advance to the next nonzero bit in the intersection of
   complemented bitmaps.  We will have already advanced past the just
   iterated bit.  */

static inline bool
bmp_iter_and_compl (bitmap_iterator *bi, unsigned *bit_no)
{
  /* If our current word is nonzero, it contains the bit we want.  */
  if (bi->bits)
    {
    next_bit:
      while (!(bi->bits & 1))
	{
	  bi->bits >>= 1;
	  *bit_no += 1;
	}
      return true;
    }

  /* Round up to the word boundary.  We might have just iterated past
     the end of the last word, hence the -1.  It is not possible for
     bit_no to point at the beginning of the now last word.  */
  *bit_no = ((*bit_no + BITMAP_WORD_BITS - 1)
	     / BITMAP_WORD_BITS * BITMAP_WORD_BITS);
  bi->word_no++;

  while (1)
    {
      /* Find the next nonzero word in this elt.  */
      while (bi->word_no != BITMAP_ELEMENT_WORDS)
	{
	  bi->bits = bi->elt1->bits[bi->word_no];
	  if (bi->elt2 && bi->elt2->indx == bi->elt1->indx)
	    bi->bits &= ~bi->elt2->bits[bi->word_no];
	  if (bi->bits)
	    goto next_bit;
	  *bit_no += BITMAP_WORD_BITS;
	  bi->word_no++;
	}

      /* Advance to the next element of elt1.  */
      bi->elt1 = bi->elt1->next;
      if (!bi->elt1)
	return false;

      /* Advance elt2 until it is no less than elt1.  */
      while (bi->elt2 && bi->elt2->indx < bi->elt1->indx)
	bi->elt2 = bi->elt2->next;

      *bit_no = bi->elt1->indx * BITMAP_ELEMENT_ALL_BITS;
      bi->word_no = 0;
    }
}

/* Loop over all bits set in BITMAP, starting with MIN and setting
   BITNUM to the bit number.  ITER is a bitmap iterator.  BITNUM
   should be treated as a read-only variable as it contains loop
   state.  */

#define EXECUTE_IF_SET_IN_BITMAP(BITMAP, MIN, BITNUM, ITER)		\
  for (bmp_iter_set_init (&(ITER), (BITMAP), (MIN), &(BITNUM));		\
       bmp_iter_set (&(ITER), &(BITNUM));				\
       bmp_iter_next (&(ITER), &(BITNUM)))

/* Loop over all the bits set in BITMAP1 & BITMAP2, starting with MIN
   and setting BITNUM to the bit number.  ITER is a bitmap iterator.
   BITNUM should be treated as a read-only variable as it contains
   loop state.  */

#define EXECUTE_IF_AND_IN_BITMAP(BITMAP1, BITMAP2, MIN, BITNUM, ITER)	\
  for (bmp_iter_and_init (&(ITER), (BITMAP1), (BITMAP2), (MIN),		\
			  &(BITNUM));					\
       bmp_iter_and (&(ITER), &(BITNUM));				\
       bmp_iter_next (&(ITER), &(BITNUM)))

/* Loop over all the bits set in BITMAP1 & ~BITMAP2, starting with MIN
   and setting BITNUM to the bit number.  ITER is a bitmap iterator.
   BITNUM should be treated as a read-only variable as it contains
   loop state.  */

#define EXECUTE_IF_AND_COMPL_IN_BITMAP(BITMAP1, BITMAP2, MIN, BITNUM, ITER) \
  for (bmp_iter_and_compl_init (&(ITER), (BITMAP1), (BITMAP2), (MIN),	\
				&(BITNUM));				\
       bmp_iter_and_compl (&(ITER), &(BITNUM));				\
       bmp_iter_next (&(ITER), &(BITNUM)))

#endif /* GCC_BITMAP_H */
