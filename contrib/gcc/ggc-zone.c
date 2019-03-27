/* "Bag-of-pages" zone garbage collector for the GNU compiler.
   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.

   Contributed by Richard Henderson (rth@redhat.com) and Daniel Berlin
   (dberlin@dberlin.org).  Rewritten by Daniel Jacobowitz
   <dan@codesourcery.com>.

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "toplev.h"
#include "varray.h"
#include "flags.h"
#include "ggc.h"
#include "timevar.h"
#include "params.h"
#include "bitmap.h"

#ifdef ENABLE_VALGRIND_CHECKING
# ifdef HAVE_VALGRIND_MEMCHECK_H
#  include <valgrind/memcheck.h>
# elif defined HAVE_MEMCHECK_H
#  include <memcheck.h>
# else
#  include <valgrind.h>
# endif
#else
/* Avoid #ifdef:s when we can help it.  */
#define VALGRIND_DISCARD(x)
#define VALGRIND_MALLOCLIKE_BLOCK(w,x,y,z)
#define VALGRIND_FREELIKE_BLOCK(x,y)
#endif

/* Prefer MAP_ANON(YMOUS) to /dev/zero, since we don't need to keep a
   file open.  Prefer either to valloc.  */
#ifdef HAVE_MMAP_ANON
# undef HAVE_MMAP_DEV_ZERO

# include <sys/mman.h>
# ifndef MAP_FAILED
#  define MAP_FAILED -1
# endif
# if !defined (MAP_ANONYMOUS) && defined (MAP_ANON)
#  define MAP_ANONYMOUS MAP_ANON
# endif
# define USING_MMAP
#endif

#ifdef HAVE_MMAP_DEV_ZERO
# include <sys/mman.h>
# ifndef MAP_FAILED
#  define MAP_FAILED -1
# endif
# define USING_MMAP
#endif

#ifndef USING_MMAP
#error Zone collector requires mmap
#endif

#if (GCC_VERSION < 3001)
#define prefetch(X) ((void) X)
#define prefetchw(X) ((void) X)
#else
#define prefetch(X) __builtin_prefetch (X)
#define prefetchw(X) __builtin_prefetch (X, 1, 3)
#endif

/* FUTURE NOTES:

   If we track inter-zone pointers, we can mark single zones at a
   time.

   If we have a zone where we guarantee no inter-zone pointers, we
   could mark that zone separately.

   The garbage zone should not be marked, and we should return 1 in
   ggc_set_mark for any object in the garbage zone, which cuts off
   marking quickly.  */

/* Strategy:

   This garbage-collecting allocator segregates objects into zones.
   It also segregates objects into "large" and "small" bins.  Large
   objects are greater than page size.

   Pages for small objects are broken up into chunks.  The page has
   a bitmap which marks the start position of each chunk (whether
   allocated or free).  Free chunks are on one of the zone's free
   lists and contain a pointer to the next free chunk.  Chunks in
   most of the free lists have a fixed size determined by the
   free list.  Chunks in the "other" sized free list have their size
   stored right after their chain pointer.

   Empty pages (of all sizes) are kept on a single page cache list,
   and are considered first when new pages are required; they are
   deallocated at the start of the next collection if they haven't
   been recycled by then.  The free page list is currently per-zone.  */

/* Define GGC_DEBUG_LEVEL to print debugging information.
     0: No debugging output.
     1: GC statistics only.
     2: Page-entry allocations/deallocations as well.
     3: Object allocations as well.
     4: Object marks as well.  */
#define GGC_DEBUG_LEVEL (0)

#ifndef HOST_BITS_PER_PTR
#define HOST_BITS_PER_PTR  HOST_BITS_PER_LONG
#endif

/* This structure manages small free chunks.  The SIZE field is only
   initialized if the chunk is in the "other" sized free list.  Large
   chunks are allocated one at a time to their own page, and so don't
   come in here.  */

struct alloc_chunk {
  struct alloc_chunk *next_free;
  unsigned int size;
};

/* The size of the fixed-size portion of a small page descriptor.  */
#define PAGE_OVERHEAD   (offsetof (struct small_page_entry, alloc_bits))

/* The collector's idea of the page size.  This must be a power of two
   no larger than the system page size, because pages must be aligned
   to this amount and are tracked at this granularity in the page
   table.  We choose a size at compile time for efficiency.

   We could make a better guess at compile time if PAGE_SIZE is a
   constant in system headers, and PAGE_SHIFT is defined...  */
#define GGC_PAGE_SIZE	4096
#define GGC_PAGE_MASK	(GGC_PAGE_SIZE - 1)
#define GGC_PAGE_SHIFT	12

#if 0
/* Alternative definitions which use the runtime page size.  */
#define GGC_PAGE_SIZE	G.pagesize
#define GGC_PAGE_MASK	G.page_mask
#define GGC_PAGE_SHIFT	G.lg_pagesize
#endif

/* The size of a small page managed by the garbage collector.  This
   must currently be GGC_PAGE_SIZE, but with a few changes could
   be any multiple of it to reduce certain kinds of overhead.  */
#define SMALL_PAGE_SIZE GGC_PAGE_SIZE

/* Free bin information.  These numbers may be in need of re-tuning.
   In general, decreasing the number of free bins would seem to
   increase the time it takes to allocate... */

/* FIXME: We can't use anything but MAX_ALIGNMENT for the bin size
   today.  */

#define NUM_FREE_BINS		64
#define FREE_BIN_DELTA		MAX_ALIGNMENT
#define SIZE_BIN_DOWN(SIZE)	((SIZE) / FREE_BIN_DELTA)

/* Allocation and marking parameters.  */

/* The smallest allocatable unit to keep track of.  */
#define BYTES_PER_ALLOC_BIT	MAX_ALIGNMENT

/* The smallest markable unit.  If we require each allocated object
   to contain at least two allocatable units, we can use half as many
   bits for the mark bitmap.  But this adds considerable complexity
   to sweeping.  */
#define BYTES_PER_MARK_BIT	BYTES_PER_ALLOC_BIT

#define BYTES_PER_MARK_WORD	(8 * BYTES_PER_MARK_BIT * sizeof (mark_type))

/* We use this structure to determine the alignment required for
   allocations.

   There are several things wrong with this estimation of alignment.

   The maximum alignment for a structure is often less than the
   maximum alignment for a basic data type; for instance, on some
   targets long long must be aligned to sizeof (int) in a structure
   and sizeof (long long) in a variable.  i386-linux is one example;
   Darwin is another (sometimes, depending on the compiler in use).

   Also, long double is not included.  Nothing in GCC uses long
   double, so we assume that this is OK.  On powerpc-darwin, adding
   long double would bring the maximum alignment up to 16 bytes,
   and until we need long double (or to vectorize compiler operations)
   that's painfully wasteful.  This will need to change, some day.  */

struct max_alignment {
  char c;
  union {
    HOST_WIDEST_INT i;
    double d;
  } u;
};

/* The biggest alignment required.  */

#define MAX_ALIGNMENT (offsetof (struct max_alignment, u))

/* Compute the smallest multiple of F that is >= X.  */

#define ROUND_UP(x, f) (CEIL (x, f) * (f))

/* Types to use for the allocation and mark bitmaps.  It might be
   a good idea to add ffsl to libiberty and use unsigned long
   instead; that could speed us up where long is wider than int.  */

typedef unsigned int alloc_type;
typedef unsigned int mark_type;
#define alloc_ffs(x) ffs(x)

/* A page_entry records the status of an allocation page.  This is the
   common data between all three kinds of pages - small, large, and
   PCH.  */
typedef struct page_entry
{
  /* The address at which the memory is allocated.  */
  char *page;

  /* The zone that this page entry belongs to.  */
  struct alloc_zone *zone;

#ifdef GATHER_STATISTICS
  /* How many collections we've survived.  */
  size_t survived;
#endif

  /* Does this page contain small objects, or one large object?  */
  bool large_p;

  /* Is this page part of the loaded PCH?  */
  bool pch_p;
} page_entry;

/* Additional data needed for small pages.  */
struct small_page_entry
{
  struct page_entry common;

  /* The next small page entry, or NULL if this is the last.  */
  struct small_page_entry *next;

  /* If currently marking this zone, a pointer to the mark bits
     for this page.  If we aren't currently marking this zone,
     this pointer may be stale (pointing to freed memory).  */
  mark_type *mark_bits;

  /* The allocation bitmap.  This array extends far enough to have
     one bit for every BYTES_PER_ALLOC_BIT bytes in the page.  */
  alloc_type alloc_bits[1];
};

/* Additional data needed for large pages.  */
struct large_page_entry
{
  struct page_entry common;

  /* The next large page entry, or NULL if this is the last.  */
  struct large_page_entry *next;

  /* The number of bytes allocated, not including the page entry.  */
  size_t bytes;

  /* The previous page in the list, so that we can unlink this one.  */
  struct large_page_entry *prev;

  /* During marking, is this object marked?  */
  bool mark_p;
};

/* A two-level tree is used to look up the page-entry for a given
   pointer.  Two chunks of the pointer's bits are extracted to index
   the first and second levels of the tree, as follows:

				   HOST_PAGE_SIZE_BITS
			   32		|      |
       msb +----------------+----+------+------+ lsb
			    |    |      |
			 PAGE_L1_BITS   |
				 |      |
			       PAGE_L2_BITS

   The bottommost HOST_PAGE_SIZE_BITS are ignored, since page-entry
   pages are aligned on system page boundaries.  The next most
   significant PAGE_L2_BITS and PAGE_L1_BITS are the second and first
   index values in the lookup table, respectively.

   For 32-bit architectures and the settings below, there are no
   leftover bits.  For architectures with wider pointers, the lookup
   tree points to a list of pages, which must be scanned to find the
   correct one.  */

#define PAGE_L1_BITS	(8)
#define PAGE_L2_BITS	(32 - PAGE_L1_BITS - GGC_PAGE_SHIFT)
#define PAGE_L1_SIZE	((size_t) 1 << PAGE_L1_BITS)
#define PAGE_L2_SIZE	((size_t) 1 << PAGE_L2_BITS)

#define LOOKUP_L1(p) \
  (((size_t) (p) >> (32 - PAGE_L1_BITS)) & ((1 << PAGE_L1_BITS) - 1))

#define LOOKUP_L2(p) \
  (((size_t) (p) >> GGC_PAGE_SHIFT) & ((1 << PAGE_L2_BITS) - 1))

#if HOST_BITS_PER_PTR <= 32

/* On 32-bit hosts, we use a two level page table, as pictured above.  */
typedef page_entry **page_table[PAGE_L1_SIZE];

#else

/* On 64-bit hosts, we use the same two level page tables plus a linked
   list that disambiguates the top 32-bits.  There will almost always be
   exactly one entry in the list.  */
typedef struct page_table_chain
{
  struct page_table_chain *next;
  size_t high_bits;
  page_entry **table[PAGE_L1_SIZE];
} *page_table;

#endif

/* The global variables.  */
static struct globals
{
  /* The linked list of zones.  */
  struct alloc_zone *zones;

  /* Lookup table for associating allocation pages with object addresses.  */
  page_table lookup;

  /* The system's page size, and related constants.  */
  size_t pagesize;
  size_t lg_pagesize;
  size_t page_mask;

  /* The size to allocate for a small page entry.  This includes
     the size of the structure and the size of the allocation
     bitmap.  */
  size_t small_page_overhead;

#if defined (HAVE_MMAP_DEV_ZERO)
  /* A file descriptor open to /dev/zero for reading.  */
  int dev_zero_fd;
#endif

  /* Allocate pages in chunks of this size, to throttle calls to memory
     allocation routines.  The first page is used, the rest go onto the
     free list.  */
  size_t quire_size;

  /* The file descriptor for debugging output.  */
  FILE *debug_file;
} G;

/* A zone allocation structure.  There is one of these for every
   distinct allocation zone.  */
struct alloc_zone
{
  /* The most recent free chunk is saved here, instead of in the linked
     free list, to decrease list manipulation.  It is most likely that we
     will want this one.  */
  char *cached_free;
  size_t cached_free_size;

  /* Linked lists of free storage.  Slots 1 ... NUM_FREE_BINS have chunks of size
     FREE_BIN_DELTA.  All other chunks are in slot 0.  */
  struct alloc_chunk *free_chunks[NUM_FREE_BINS + 1];

  /* The highest bin index which might be non-empty.  It may turn out
     to be empty, in which case we have to search downwards.  */
  size_t high_free_bin;

  /* Bytes currently allocated in this zone.  */
  size_t allocated;

  /* Linked list of the small pages in this zone.  */
  struct small_page_entry *pages;

  /* Doubly linked list of large pages in this zone.  */
  struct large_page_entry *large_pages;

  /* If we are currently marking this zone, a pointer to the mark bits.  */
  mark_type *mark_bits;

  /* Name of the zone.  */
  const char *name;

  /* The number of small pages currently allocated in this zone.  */
  size_t n_small_pages;

  /* Bytes allocated at the end of the last collection.  */
  size_t allocated_last_gc;

  /* Total amount of memory mapped.  */
  size_t bytes_mapped;

  /* A cache of free system pages.  */
  struct small_page_entry *free_pages;

  /* Next zone in the linked list of zones.  */
  struct alloc_zone *next_zone;

  /* True if this zone was collected during this collection.  */
  bool was_collected;

  /* True if this zone should be destroyed after the next collection.  */
  bool dead;

#ifdef GATHER_STATISTICS
  struct
  {
    /* Total memory allocated with ggc_alloc.  */
    unsigned long long total_allocated;
    /* Total overhead for memory to be allocated with ggc_alloc.  */
    unsigned long long total_overhead;

    /* Total allocations and overhead for sizes less than 32, 64 and 128.
       These sizes are interesting because they are typical cache line
       sizes.  */
   
    unsigned long long total_allocated_under32;
    unsigned long long total_overhead_under32;
  
    unsigned long long total_allocated_under64;
    unsigned long long total_overhead_under64;
  
    unsigned long long total_allocated_under128;
    unsigned long long total_overhead_under128;
  } stats;
#endif
} main_zone;

/* Some default zones.  */
struct alloc_zone rtl_zone;
struct alloc_zone tree_zone;
struct alloc_zone tree_id_zone;

/* The PCH zone does not need a normal zone structure, and it does
   not live on the linked list of zones.  */
struct pch_zone
{
  /* The start of the PCH zone.  NULL if there is none.  */
  char *page;

  /* The end of the PCH zone.  NULL if there is none.  */
  char *end;

  /* The size of the PCH zone.  0 if there is none.  */
  size_t bytes;

  /* The allocation bitmap for the PCH zone.  */
  alloc_type *alloc_bits;

  /* If we are currently marking, the mark bitmap for the PCH zone.
     When it is first read in, we could avoid marking the PCH,
     because it will not contain any pointers to GC memory outside
     of the PCH; however, the PCH is currently mapped as writable,
     so we must mark it in case new pointers are added.  */
  mark_type *mark_bits;
} pch_zone;

#ifdef USING_MMAP
static char *alloc_anon (char *, size_t, struct alloc_zone *);
#endif
static struct small_page_entry * alloc_small_page (struct alloc_zone *);
static struct large_page_entry * alloc_large_page (size_t, struct alloc_zone *);
static void free_chunk (char *, size_t, struct alloc_zone *);
static void free_small_page (struct small_page_entry *);
static void free_large_page (struct large_page_entry *);
static void release_pages (struct alloc_zone *);
static void sweep_pages (struct alloc_zone *);
static bool ggc_collect_1 (struct alloc_zone *, bool);
static void new_ggc_zone_1 (struct alloc_zone *, const char *);

/* Traverse the page table and find the entry for a page.
   Die (probably) if the object wasn't allocated via GC.  */

static inline page_entry *
lookup_page_table_entry (const void *p)
{
  page_entry ***base;
  size_t L1, L2;

#if HOST_BITS_PER_PTR <= 32
  base = &G.lookup[0];
#else
  page_table table = G.lookup;
  size_t high_bits = (size_t) p & ~ (size_t) 0xffffffff;
  while (table->high_bits != high_bits)
    table = table->next;
  base = &table->table[0];
#endif

  /* Extract the level 1 and 2 indices.  */
  L1 = LOOKUP_L1 (p);
  L2 = LOOKUP_L2 (p);

  return base[L1][L2];
}

/* Set the page table entry for the page that starts at P.  If ENTRY
   is NULL, clear the entry.  */

static void
set_page_table_entry (void *p, page_entry *entry)
{
  page_entry ***base;
  size_t L1, L2;

#if HOST_BITS_PER_PTR <= 32
  base = &G.lookup[0];
#else
  page_table table;
  size_t high_bits = (size_t) p & ~ (size_t) 0xffffffff;
  for (table = G.lookup; table; table = table->next)
    if (table->high_bits == high_bits)
      goto found;

  /* Not found -- allocate a new table.  */
  table = xcalloc (1, sizeof(*table));
  table->next = G.lookup;
  table->high_bits = high_bits;
  G.lookup = table;
found:
  base = &table->table[0];
#endif

  /* Extract the level 1 and 2 indices.  */
  L1 = LOOKUP_L1 (p);
  L2 = LOOKUP_L2 (p);

  if (base[L1] == NULL)
    base[L1] = xcalloc (PAGE_L2_SIZE, sizeof (page_entry *));

  base[L1][L2] = entry;
}

/* Find the page table entry associated with OBJECT.  */

static inline struct page_entry *
zone_get_object_page (const void *object)
{
  return lookup_page_table_entry (object);
}

/* Find which element of the alloc_bits array OBJECT should be
   recorded in.  */
static inline unsigned int
zone_get_object_alloc_word (const void *object)
{
  return (((size_t) object & (GGC_PAGE_SIZE - 1))
	  / (8 * sizeof (alloc_type) * BYTES_PER_ALLOC_BIT));
}

/* Find which bit of the appropriate word in the alloc_bits array
   OBJECT should be recorded in.  */
static inline unsigned int
zone_get_object_alloc_bit (const void *object)
{
  return (((size_t) object / BYTES_PER_ALLOC_BIT)
	  % (8 * sizeof (alloc_type)));
}

/* Find which element of the mark_bits array OBJECT should be recorded
   in.  */
static inline unsigned int
zone_get_object_mark_word (const void *object)
{
  return (((size_t) object & (GGC_PAGE_SIZE - 1))
	  / (8 * sizeof (mark_type) * BYTES_PER_MARK_BIT));
}

/* Find which bit of the appropriate word in the mark_bits array
   OBJECT should be recorded in.  */
static inline unsigned int
zone_get_object_mark_bit (const void *object)
{
  return (((size_t) object / BYTES_PER_MARK_BIT)
	  % (8 * sizeof (mark_type)));
}

/* Set the allocation bit corresponding to OBJECT in its page's
   bitmap.  Used to split this object from the preceding one.  */
static inline void
zone_set_object_alloc_bit (const void *object)
{
  struct small_page_entry *page
    = (struct small_page_entry *) zone_get_object_page (object);
  unsigned int start_word = zone_get_object_alloc_word (object);
  unsigned int start_bit = zone_get_object_alloc_bit (object);

  page->alloc_bits[start_word] |= 1L << start_bit;
}

/* Clear the allocation bit corresponding to OBJECT in PAGE's
   bitmap.  Used to coalesce this object with the preceding
   one.  */
static inline void
zone_clear_object_alloc_bit (struct small_page_entry *page,
			     const void *object)
{
  unsigned int start_word = zone_get_object_alloc_word (object);
  unsigned int start_bit = zone_get_object_alloc_bit (object);

  /* Would xor be quicker?  */
  page->alloc_bits[start_word] &= ~(1L << start_bit);
}

/* Find the size of the object which starts at START_WORD and
   START_BIT in ALLOC_BITS, which is at most MAX_SIZE bytes.
   Helper function for ggc_get_size and zone_find_object_size.  */

static inline size_t
zone_object_size_1 (alloc_type *alloc_bits,
		    size_t start_word, size_t start_bit,
		    size_t max_size)
{
  size_t size;
  alloc_type alloc_word;
  int indx;

  /* Load the first word.  */
  alloc_word = alloc_bits[start_word++];

  /* If that was the last bit in this word, we'll want to continue
     with the next word.  Otherwise, handle the rest of this word.  */
  if (start_bit)
    {
      indx = alloc_ffs (alloc_word >> start_bit);
      if (indx)
	/* indx is 1-based.  We started at the bit after the object's
	   start, but we also ended at the bit after the object's end.
	   It cancels out.  */
	return indx * BYTES_PER_ALLOC_BIT;

      /* The extra 1 accounts for the starting unit, before start_bit.  */
      size = (sizeof (alloc_type) * 8 - start_bit + 1) * BYTES_PER_ALLOC_BIT;

      if (size >= max_size)
	return max_size;

      alloc_word = alloc_bits[start_word++];
    }
  else
    size = BYTES_PER_ALLOC_BIT;

  while (alloc_word == 0)
    {
      size += sizeof (alloc_type) * 8 * BYTES_PER_ALLOC_BIT;
      if (size >= max_size)
	return max_size;
      alloc_word = alloc_bits[start_word++];
    }

  indx = alloc_ffs (alloc_word);
  return size + (indx - 1) * BYTES_PER_ALLOC_BIT;
}

/* Find the size of OBJECT on small page PAGE.  */

static inline size_t
zone_find_object_size (struct small_page_entry *page,
		       const void *object)
{
  const char *object_midptr = (const char *) object + BYTES_PER_ALLOC_BIT;
  unsigned int start_word = zone_get_object_alloc_word (object_midptr);
  unsigned int start_bit = zone_get_object_alloc_bit (object_midptr);
  size_t max_size = (page->common.page + SMALL_PAGE_SIZE
		     - (char *) object);

  return zone_object_size_1 (page->alloc_bits, start_word, start_bit,
			     max_size);
}

/* Allocate the mark bits for every zone, and set the pointers on each
   page.  */
static void
zone_allocate_marks (void)
{
  struct alloc_zone *zone;

  for (zone = G.zones; zone; zone = zone->next_zone)
    {
      struct small_page_entry *page;
      mark_type *cur_marks;
      size_t mark_words, mark_words_per_page;
#ifdef ENABLE_CHECKING
      size_t n = 0;
#endif

      mark_words_per_page
	= (GGC_PAGE_SIZE + BYTES_PER_MARK_WORD - 1) / BYTES_PER_MARK_WORD;
      mark_words = zone->n_small_pages * mark_words_per_page;
      zone->mark_bits = (mark_type *) xcalloc (sizeof (mark_type),
						   mark_words);
      cur_marks = zone->mark_bits;
      for (page = zone->pages; page; page = page->next)
	{
	  page->mark_bits = cur_marks;
	  cur_marks += mark_words_per_page;
#ifdef ENABLE_CHECKING
	  n++;
#endif
	}
#ifdef ENABLE_CHECKING
      gcc_assert (n == zone->n_small_pages);
#endif
    }

  /* We don't collect the PCH zone, but we do have to mark it
     (for now).  */
  if (pch_zone.bytes)
    pch_zone.mark_bits
      = (mark_type *) xcalloc (sizeof (mark_type),
			       CEIL (pch_zone.bytes, BYTES_PER_MARK_WORD));
}

/* After marking and sweeping, release the memory used for mark bits.  */
static void
zone_free_marks (void)
{
  struct alloc_zone *zone;

  for (zone = G.zones; zone; zone = zone->next_zone)
    if (zone->mark_bits)
      {
	free (zone->mark_bits);
	zone->mark_bits = NULL;
      }

  if (pch_zone.bytes)
    {
      free (pch_zone.mark_bits);
      pch_zone.mark_bits = NULL;
    }
}

#ifdef USING_MMAP
/* Allocate SIZE bytes of anonymous memory, preferably near PREF,
   (if non-null).  The ifdef structure here is intended to cause a
   compile error unless exactly one of the HAVE_* is defined.  */

static inline char *
alloc_anon (char *pref ATTRIBUTE_UNUSED, size_t size, struct alloc_zone *zone)
{
#ifdef HAVE_MMAP_ANON
  char *page = (char *) mmap (pref, size, PROT_READ | PROT_WRITE,
			      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
#ifdef HAVE_MMAP_DEV_ZERO
  char *page = (char *) mmap (pref, size, PROT_READ | PROT_WRITE,
			      MAP_PRIVATE, G.dev_zero_fd, 0);
#endif

  if (page == (char *) MAP_FAILED)
    {
      perror ("virtual memory exhausted");
      exit (FATAL_EXIT_CODE);
    }

  /* Remember that we allocated this memory.  */
  zone->bytes_mapped += size;

  /* Pretend we don't have access to the allocated pages.  We'll enable
     access to smaller pieces of the area in ggc_alloc.  Discard the
     handle to avoid handle leak.  */
  VALGRIND_DISCARD (VALGRIND_MAKE_NOACCESS (page, size));

  return page;
}
#endif

/* Allocate a new page for allocating small objects in ZONE, and
   return an entry for it.  */

static struct small_page_entry *
alloc_small_page (struct alloc_zone *zone)
{
  struct small_page_entry *entry;

  /* Check the list of free pages for one we can use.  */
  entry = zone->free_pages;
  if (entry != NULL)
    {
      /* Recycle the allocated memory from this page ...  */
      zone->free_pages = entry->next;
    }
  else
    {
      /* We want just one page.  Allocate a bunch of them and put the
	 extras on the freelist.  (Can only do this optimization with
	 mmap for backing store.)  */
      struct small_page_entry *e, *f = zone->free_pages;
      int i;
      char *page;

      page = alloc_anon (NULL, GGC_PAGE_SIZE * G.quire_size, zone);

      /* This loop counts down so that the chain will be in ascending
	 memory order.  */
      for (i = G.quire_size - 1; i >= 1; i--)
	{
	  e = xcalloc (1, G.small_page_overhead);
	  e->common.page = page + (i << GGC_PAGE_SHIFT);
	  e->common.zone = zone;
	  e->next = f;
	  f = e;
	  set_page_table_entry (e->common.page, &e->common);
	}

      zone->free_pages = f;

      entry = xcalloc (1, G.small_page_overhead);
      entry->common.page = page;
      entry->common.zone = zone;
      set_page_table_entry (page, &entry->common);
    }

  zone->n_small_pages++;

  if (GGC_DEBUG_LEVEL >= 2)
    fprintf (G.debug_file,
	     "Allocating %s page at %p, data %p-%p\n",
	     entry->common.zone->name, (PTR) entry, entry->common.page,
	     entry->common.page + SMALL_PAGE_SIZE - 1);

  return entry;
}

/* Allocate a large page of size SIZE in ZONE.  */

static struct large_page_entry *
alloc_large_page (size_t size, struct alloc_zone *zone)
{
  struct large_page_entry *entry;
  char *page;
  size_t needed_size;

  needed_size = size + sizeof (struct large_page_entry);
  page = xmalloc (needed_size);

  entry = (struct large_page_entry *) page;

  entry->next = NULL;
  entry->common.page = page + sizeof (struct large_page_entry);
  entry->common.large_p = true;
  entry->common.pch_p = false;
  entry->common.zone = zone;
#ifdef GATHER_STATISTICS
  entry->common.survived = 0;
#endif
  entry->mark_p = false;
  entry->bytes = size;
  entry->prev = NULL;

  set_page_table_entry (entry->common.page, &entry->common);

  if (GGC_DEBUG_LEVEL >= 2)
    fprintf (G.debug_file,
	     "Allocating %s large page at %p, data %p-%p\n",
	     entry->common.zone->name, (PTR) entry, entry->common.page,
	     entry->common.page + SMALL_PAGE_SIZE - 1);

  return entry;
}


/* For a page that is no longer needed, put it on the free page list.  */

static inline void
free_small_page (struct small_page_entry *entry)
{
  if (GGC_DEBUG_LEVEL >= 2)
    fprintf (G.debug_file,
	     "Deallocating %s page at %p, data %p-%p\n",
	     entry->common.zone->name, (PTR) entry,
	     entry->common.page, entry->common.page + SMALL_PAGE_SIZE - 1);

  gcc_assert (!entry->common.large_p);

  /* Mark the page as inaccessible.  Discard the handle to
     avoid handle leak.  */
  VALGRIND_DISCARD (VALGRIND_MAKE_NOACCESS (entry->common.page,
					    SMALL_PAGE_SIZE));

  entry->next = entry->common.zone->free_pages;
  entry->common.zone->free_pages = entry;
  entry->common.zone->n_small_pages--;
}

/* Release a large page that is no longer needed.  */

static inline void
free_large_page (struct large_page_entry *entry)
{
  if (GGC_DEBUG_LEVEL >= 2)
    fprintf (G.debug_file,
	     "Deallocating %s page at %p, data %p-%p\n",
	     entry->common.zone->name, (PTR) entry,
	     entry->common.page, entry->common.page + SMALL_PAGE_SIZE - 1);

  gcc_assert (entry->common.large_p);

  set_page_table_entry (entry->common.page, NULL);
  free (entry);
}

/* Release the free page cache to the system.  */

static void
release_pages (struct alloc_zone *zone)
{
#ifdef USING_MMAP
  struct small_page_entry *p, *next;
  char *start;
  size_t len;

  /* Gather up adjacent pages so they are unmapped together.  */
  p = zone->free_pages;

  while (p)
    {
      start = p->common.page;
      next = p->next;
      len = SMALL_PAGE_SIZE;
      set_page_table_entry (p->common.page, NULL);
      p = next;

      while (p && p->common.page == start + len)
	{
	  next = p->next;
	  len += SMALL_PAGE_SIZE;
	  set_page_table_entry (p->common.page, NULL);
	  p = next;
	}

      munmap (start, len);
      zone->bytes_mapped -= len;
    }

  zone->free_pages = NULL;
#endif
}

/* Place the block at PTR of size SIZE on the free list for ZONE.  */

static inline void
free_chunk (char *ptr, size_t size, struct alloc_zone *zone)
{
  struct alloc_chunk *chunk = (struct alloc_chunk *) ptr;
  size_t bin = 0;

  bin = SIZE_BIN_DOWN (size);
  gcc_assert (bin != 0);
  if (bin > NUM_FREE_BINS)
    {
      bin = 0;
      VALGRIND_DISCARD (VALGRIND_MAKE_WRITABLE (chunk, sizeof (struct alloc_chunk)));
      chunk->size = size;
      chunk->next_free = zone->free_chunks[bin];
      VALGRIND_DISCARD (VALGRIND_MAKE_NOACCESS (ptr + sizeof (struct alloc_chunk),
						size - sizeof (struct alloc_chunk)));
    }
  else
    {
      VALGRIND_DISCARD (VALGRIND_MAKE_WRITABLE (chunk, sizeof (struct alloc_chunk *)));
      chunk->next_free = zone->free_chunks[bin];
      VALGRIND_DISCARD (VALGRIND_MAKE_NOACCESS (ptr + sizeof (struct alloc_chunk *),
						size - sizeof (struct alloc_chunk *)));
    }

  zone->free_chunks[bin] = chunk;
  if (bin > zone->high_free_bin)
    zone->high_free_bin = bin;
  if (GGC_DEBUG_LEVEL >= 3)
    fprintf (G.debug_file, "Deallocating object, chunk=%p\n", (void *)chunk);
}

/* Allocate a chunk of memory of at least ORIG_SIZE bytes, in ZONE.  */

void *
ggc_alloc_zone_stat (size_t orig_size, struct alloc_zone *zone
		     MEM_STAT_DECL)
{
  size_t bin;
  size_t csize;
  struct small_page_entry *entry;
  struct alloc_chunk *chunk, **pp;
  void *result;
  size_t size = orig_size;

  /* Make sure that zero-sized allocations get a unique and freeable
     pointer.  */
  if (size == 0)
    size = MAX_ALIGNMENT;
  else
    size = (size + MAX_ALIGNMENT - 1) & -MAX_ALIGNMENT;

  /* Try to allocate the object from several different sources.  Each
     of these cases is responsible for setting RESULT and SIZE to
     describe the allocated block, before jumping to FOUND.  If a
     chunk is split, the allocate bit for the new chunk should also be
     set.

     Large objects are handled specially.  However, they'll just fail
     the next couple of conditions, so we can wait to check for them
     below.  The large object case is relatively rare (< 1%), so this
     is a win.  */

  /* First try to split the last chunk we allocated.  For best
     fragmentation behavior it would be better to look for a
     free bin of the appropriate size for a small object.  However,
     we're unlikely (1% - 7%) to find one, and this gives better
     locality behavior anyway.  This case handles the lion's share
     of all calls to this function.  */
  if (size <= zone->cached_free_size)
    {
      result = zone->cached_free;

      zone->cached_free_size -= size;
      if (zone->cached_free_size)
	{
	  zone->cached_free += size;
	  zone_set_object_alloc_bit (zone->cached_free);
	}

      goto found;
    }

  /* Next, try to find a free bin of the exactly correct size.  */

  /* We want to round SIZE up, rather than down, but we know it's
     already aligned to at least FREE_BIN_DELTA, so we can just
     shift.  */
  bin = SIZE_BIN_DOWN (size);

  if (bin <= NUM_FREE_BINS
      && (chunk = zone->free_chunks[bin]) != NULL)
    {
      /* We have a chunk of the right size.  Pull it off the free list
	 and use it.  */

      zone->free_chunks[bin] = chunk->next_free;

      /* NOTE: SIZE is only guaranteed to be right if MAX_ALIGNMENT
	 == FREE_BIN_DELTA.  */
      result = chunk;

      /* The allocation bits are already set correctly.  HIGH_FREE_BIN
	 may now be wrong, if this was the last chunk in the high bin.
	 Rather than fixing it up now, wait until we need to search
	 the free bins.  */

      goto found;
    }

  /* Next, if there wasn't a chunk of the ideal size, look for a chunk
     to split.  We can find one in the too-big bin, or in the largest
     sized bin with a chunk in it.  Try the largest normal-sized bin
     first.  */

  if (zone->high_free_bin > bin)
    {
      /* Find the highest numbered free bin.  It will be at or below
	 the watermark.  */
      while (zone->high_free_bin > bin
	     && zone->free_chunks[zone->high_free_bin] == NULL)
	zone->high_free_bin--;

      if (zone->high_free_bin > bin)
	{
	  size_t tbin = zone->high_free_bin;
	  chunk = zone->free_chunks[tbin];

	  /* Remove the chunk from its previous bin.  */
	  zone->free_chunks[tbin] = chunk->next_free;

	  result = (char *) chunk;

	  /* Save the rest of the chunk for future allocation.  */
	  if (zone->cached_free_size)
	    free_chunk (zone->cached_free, zone->cached_free_size, zone);

	  chunk = (struct alloc_chunk *) ((char *) result + size);
	  zone->cached_free = (char *) chunk;
	  zone->cached_free_size = (tbin - bin) * FREE_BIN_DELTA;

	  /* Mark the new free chunk as an object, so that we can
	     find the size of the newly allocated object.  */
	  zone_set_object_alloc_bit (chunk);

	  /* HIGH_FREE_BIN may now be wrong, if this was the last
	     chunk in the high bin.  Rather than fixing it up now,
	     wait until we need to search the free bins.  */

	  goto found;
	}
    }

  /* Failing that, look through the "other" bucket for a chunk
     that is large enough.  */
  pp = &(zone->free_chunks[0]);
  chunk = *pp;
  while (chunk && chunk->size < size)
    {
      pp = &chunk->next_free;
      chunk = *pp;
    }

  if (chunk)
    {
      /* Remove the chunk from its previous bin.  */
      *pp = chunk->next_free;

      result = (char *) chunk;

      /* Save the rest of the chunk for future allocation, if there's any
	 left over.  */
      csize = chunk->size;
      if (csize > size)
	{
	  if (zone->cached_free_size)
	    free_chunk (zone->cached_free, zone->cached_free_size, zone);

	  chunk = (struct alloc_chunk *) ((char *) result + size);
	  zone->cached_free = (char *) chunk;
	  zone->cached_free_size = csize - size;

	  /* Mark the new free chunk as an object.  */
	  zone_set_object_alloc_bit (chunk);
	}

      goto found;
    }

  /* Handle large allocations.  We could choose any threshold between
     GGC_PAGE_SIZE - sizeof (struct large_page_entry) and
     GGC_PAGE_SIZE.  It can't be smaller, because then it wouldn't
     be guaranteed to have a unique entry in the lookup table.  Large
     allocations will always fall through to here.  */
  if (size > GGC_PAGE_SIZE)
    {
      struct large_page_entry *entry = alloc_large_page (size, zone);

#ifdef GATHER_STATISTICS
      entry->common.survived = 0;
#endif

      entry->next = zone->large_pages;
      if (zone->large_pages)
	zone->large_pages->prev = entry;
      zone->large_pages = entry;

      result = entry->common.page;

      goto found;
    }

  /* Failing everything above, allocate a new small page.  */

  entry = alloc_small_page (zone);
  entry->next = zone->pages;
  zone->pages = entry;

  /* Mark the first chunk in the new page.  */
  entry->alloc_bits[0] = 1;

  result = entry->common.page;
  if (size < SMALL_PAGE_SIZE)
    {
      if (zone->cached_free_size)
	free_chunk (zone->cached_free, zone->cached_free_size, zone);

      zone->cached_free = (char *) result + size;
      zone->cached_free_size = SMALL_PAGE_SIZE - size;

      /* Mark the new free chunk as an object.  */
      zone_set_object_alloc_bit (zone->cached_free);
    }

 found:

  /* We could save TYPE in the chunk, but we don't use that for
     anything yet.  If we wanted to, we could do it by adding it
     either before the beginning of the chunk or after its end,
     and adjusting the size and pointer appropriately.  */

  /* We'll probably write to this after we return.  */
  prefetchw (result);

#ifdef ENABLE_GC_CHECKING
  /* `Poison' the entire allocated object.  */
  VALGRIND_DISCARD (VALGRIND_MAKE_WRITABLE (result, size));
  memset (result, 0xaf, size);
  VALGRIND_DISCARD (VALGRIND_MAKE_NOACCESS (result + orig_size,
					    size - orig_size));
#endif

  /* Tell Valgrind that the memory is there, but its content isn't
     defined.  The bytes at the end of the object are still marked
     unaccessible.  */
  VALGRIND_DISCARD (VALGRIND_MAKE_WRITABLE (result, orig_size));

  /* Keep track of how many bytes are being allocated.  This
     information is used in deciding when to collect.  */
  zone->allocated += size;
  
  timevar_ggc_mem_total += size;

#ifdef GATHER_STATISTICS
  ggc_record_overhead (orig_size, size - orig_size, result PASS_MEM_STAT);

  {
    size_t object_size = size;
    size_t overhead = object_size - orig_size;

    zone->stats.total_overhead += overhead;
    zone->stats.total_allocated += object_size;

    if (orig_size <= 32)
      {
	zone->stats.total_overhead_under32 += overhead;
	zone->stats.total_allocated_under32 += object_size;
      }
    if (orig_size <= 64)
      {
	zone->stats.total_overhead_under64 += overhead;
	zone->stats.total_allocated_under64 += object_size;
      }
    if (orig_size <= 128)
      {
	zone->stats.total_overhead_under128 += overhead;
	zone->stats.total_allocated_under128 += object_size;
      }
  }
#endif

  if (GGC_DEBUG_LEVEL >= 3)
    fprintf (G.debug_file, "Allocating object, size=%lu at %p\n",
	     (unsigned long) size, result);

  return result;
}

/* Allocate a SIZE of chunk memory of GTE type, into an appropriate zone
   for that type.  */

void *
ggc_alloc_typed_stat (enum gt_types_enum gte, size_t size
		      MEM_STAT_DECL)
{
  switch (gte)
    {
    case gt_ggc_e_14lang_tree_node:
      return ggc_alloc_zone_pass_stat (size, &tree_zone);

    case gt_ggc_e_7rtx_def:
      return ggc_alloc_zone_pass_stat (size, &rtl_zone);

    case gt_ggc_e_9rtvec_def:
      return ggc_alloc_zone_pass_stat (size, &rtl_zone);

    default:
      return ggc_alloc_zone_pass_stat (size, &main_zone);
    }
}

/* Normal ggc_alloc simply allocates into the main zone.  */

void *
ggc_alloc_stat (size_t size MEM_STAT_DECL)
{
  return ggc_alloc_zone_pass_stat (size, &main_zone);
}

/* Poison the chunk.  */
#ifdef ENABLE_GC_CHECKING
#define poison_region(PTR, SIZE) \
  memset ((PTR), 0xa5, (SIZE))
#else
#define poison_region(PTR, SIZE)
#endif

/* Free the object at P.  */

void
ggc_free (void *p)
{
  struct page_entry *page;

#ifdef GATHER_STATISTICS
  ggc_free_overhead (p);
#endif

  poison_region (p, ggc_get_size (p));

  page = zone_get_object_page (p);

  if (page->large_p)
    {
      struct large_page_entry *large_page
	= (struct large_page_entry *) page;

      /* Remove the page from the linked list.  */
      if (large_page->prev)
	large_page->prev->next = large_page->next;
      else
	{
	  gcc_assert (large_page->common.zone->large_pages == large_page);
	  large_page->common.zone->large_pages = large_page->next;
	}
      if (large_page->next)
	large_page->next->prev = large_page->prev;

      large_page->common.zone->allocated -= large_page->bytes;

      /* Release the memory associated with this object.  */
      free_large_page (large_page);
    }
  else if (page->pch_p)
    /* Don't do anything.  We won't allocate a new object from the
       PCH zone so there's no point in releasing anything.  */
    ;
  else
    {
      size_t size = ggc_get_size (p);

      page->zone->allocated -= size;

      /* Add the chunk to the free list.  We don't bother with coalescing,
	 since we are likely to want a chunk of this size again.  */
      free_chunk (p, size, page->zone);
    }
}

/* If P is not marked, mark it and return false.  Otherwise return true.
   P must have been allocated by the GC allocator; it mustn't point to
   static objects, stack variables, or memory allocated with malloc.  */

int
ggc_set_mark (const void *p)
{
  struct page_entry *page;
  const char *ptr = (const char *) p;

  page = zone_get_object_page (p);

  if (page->pch_p)
    {
      size_t mark_word, mark_bit, offset;
      offset = (ptr - pch_zone.page) / BYTES_PER_MARK_BIT;
      mark_word = offset / (8 * sizeof (mark_type));
      mark_bit = offset % (8 * sizeof (mark_type));
      
      if (pch_zone.mark_bits[mark_word] & (1 << mark_bit))
	return 1;
      pch_zone.mark_bits[mark_word] |= (1 << mark_bit);
    }
  else if (page->large_p)
    {
      struct large_page_entry *large_page
	= (struct large_page_entry *) page;

      if (large_page->mark_p)
	return 1;
      large_page->mark_p = true;
    }
  else
    {
      struct small_page_entry *small_page
	= (struct small_page_entry *) page;

      if (small_page->mark_bits[zone_get_object_mark_word (p)]
	  & (1 << zone_get_object_mark_bit (p)))
	return 1;
      small_page->mark_bits[zone_get_object_mark_word (p)]
	|= (1 << zone_get_object_mark_bit (p));
    }

  if (GGC_DEBUG_LEVEL >= 4)
    fprintf (G.debug_file, "Marking %p\n", p);

  return 0;
}

/* Return 1 if P has been marked, zero otherwise.
   P must have been allocated by the GC allocator; it mustn't point to
   static objects, stack variables, or memory allocated with malloc.  */

int
ggc_marked_p (const void *p)
{
  struct page_entry *page;
  const char *ptr = p;

  page = zone_get_object_page (p);

  if (page->pch_p)
    {
      size_t mark_word, mark_bit, offset;
      offset = (ptr - pch_zone.page) / BYTES_PER_MARK_BIT;
      mark_word = offset / (8 * sizeof (mark_type));
      mark_bit = offset % (8 * sizeof (mark_type));
      
      return (pch_zone.mark_bits[mark_word] & (1 << mark_bit)) != 0;
    }

  if (page->large_p)
    {
      struct large_page_entry *large_page
	= (struct large_page_entry *) page;

      return large_page->mark_p;
    }
  else
    {
      struct small_page_entry *small_page
	= (struct small_page_entry *) page;

      return 0 != (small_page->mark_bits[zone_get_object_mark_word (p)]
		   & (1 << zone_get_object_mark_bit (p)));
    }
}

/* Return the size of the gc-able object P.  */

size_t
ggc_get_size (const void *p)
{
  struct page_entry *page;
  const char *ptr = (const char *) p;

  page = zone_get_object_page (p);

  if (page->pch_p)
    {
      size_t alloc_word, alloc_bit, offset, max_size;
      offset = (ptr - pch_zone.page) / BYTES_PER_ALLOC_BIT + 1;
      alloc_word = offset / (8 * sizeof (alloc_type));
      alloc_bit = offset % (8 * sizeof (alloc_type));
      max_size = pch_zone.bytes - (ptr - pch_zone.page);
      return zone_object_size_1 (pch_zone.alloc_bits, alloc_word, alloc_bit,
				 max_size);
    }

  if (page->large_p)
    return ((struct large_page_entry *)page)->bytes;
  else
    return zone_find_object_size ((struct small_page_entry *) page, p);
}

/* Initialize the ggc-zone-mmap allocator.  */
void
init_ggc (void)
{
  /* The allocation size must be greater than BYTES_PER_MARK_BIT, and
     a multiple of both BYTES_PER_ALLOC_BIT and FREE_BIN_DELTA, for
     the current assumptions to hold.  */

  gcc_assert (FREE_BIN_DELTA == MAX_ALIGNMENT);

  /* Set up the main zone by hand.  */
  main_zone.name = "Main zone";
  G.zones = &main_zone;

  /* Allocate the default zones.  */
  new_ggc_zone_1 (&rtl_zone, "RTL zone");
  new_ggc_zone_1 (&tree_zone, "Tree zone");
  new_ggc_zone_1 (&tree_id_zone, "Tree identifier zone");

  G.pagesize = getpagesize();
  G.lg_pagesize = exact_log2 (G.pagesize);
  G.page_mask = ~(G.pagesize - 1);

  /* Require the system page size to be a multiple of GGC_PAGE_SIZE.  */
  gcc_assert ((G.pagesize & (GGC_PAGE_SIZE - 1)) == 0);

  /* Allocate 16 system pages at a time.  */
  G.quire_size = 16 * G.pagesize / GGC_PAGE_SIZE;

  /* Calculate the size of the allocation bitmap and other overhead.  */
  /* Right now we allocate bits for the page header and bitmap.  These
     are wasted, but a little tricky to eliminate.  */
  G.small_page_overhead
    = PAGE_OVERHEAD + (GGC_PAGE_SIZE / BYTES_PER_ALLOC_BIT / 8);
  /* G.small_page_overhead = ROUND_UP (G.small_page_overhead, MAX_ALIGNMENT); */

#ifdef HAVE_MMAP_DEV_ZERO
  G.dev_zero_fd = open ("/dev/zero", O_RDONLY);
  gcc_assert (G.dev_zero_fd != -1);
#endif

#if 0
  G.debug_file = fopen ("ggc-mmap.debug", "w");
  setlinebuf (G.debug_file);
#else
  G.debug_file = stdout;
#endif

#ifdef USING_MMAP
  /* StunOS has an amazing off-by-one error for the first mmap allocation
     after fiddling with RLIMIT_STACK.  The result, as hard as it is to
     believe, is an unaligned page allocation, which would cause us to
     hork badly if we tried to use it.  */
  {
    char *p = alloc_anon (NULL, G.pagesize, &main_zone);
    struct small_page_entry *e;
    if ((size_t)p & (G.pagesize - 1))
      {
	/* How losing.  Discard this one and try another.  If we still
	   can't get something useful, give up.  */

	p = alloc_anon (NULL, G.pagesize, &main_zone);
	gcc_assert (!((size_t)p & (G.pagesize - 1)));
      }

    if (GGC_PAGE_SIZE == G.pagesize)
      {
	/* We have a good page, might as well hold onto it...  */
	e = xcalloc (1, G.small_page_overhead);
	e->common.page = p;
	e->common.zone = &main_zone;
	e->next = main_zone.free_pages;
	set_page_table_entry (e->common.page, &e->common);
	main_zone.free_pages = e;
      }
    else
      {
	munmap (p, G.pagesize);
      }
  }
#endif
}

/* Start a new GGC zone.  */

static void
new_ggc_zone_1 (struct alloc_zone *new_zone, const char * name)
{
  new_zone->name = name;
  new_zone->next_zone = G.zones->next_zone;
  G.zones->next_zone = new_zone;
}

struct alloc_zone *
new_ggc_zone (const char * name)
{
  struct alloc_zone *new_zone = xcalloc (1, sizeof (struct alloc_zone));
  new_ggc_zone_1 (new_zone, name);
  return new_zone;
}

/* Destroy a GGC zone.  */
void
destroy_ggc_zone (struct alloc_zone * dead_zone)
{
  struct alloc_zone *z;

  for (z = G.zones; z && z->next_zone != dead_zone; z = z->next_zone)
    /* Just find that zone.  */
    continue;

  /* We should have found the zone in the list.  Anything else is fatal.  */
  gcc_assert (z);

  /* z is dead, baby. z is dead.  */
  z->dead = true;
}

/* Free all empty pages and objects within a page for a given zone  */

static void
sweep_pages (struct alloc_zone *zone)
{
  struct large_page_entry **lpp, *lp, *lnext;
  struct small_page_entry **spp, *sp, *snext;
  char *last_free;
  size_t allocated = 0;
  bool nomarksinpage;

  /* First, reset the free_chunks lists, since we are going to
     re-free free chunks in hopes of coalescing them into large chunks.  */
  memset (zone->free_chunks, 0, sizeof (zone->free_chunks));
  zone->high_free_bin = 0;
  zone->cached_free = NULL;
  zone->cached_free_size = 0;

  /* Large pages are all or none affairs. Either they are completely
     empty, or they are completely full.  */
  lpp = &zone->large_pages;
  for (lp = zone->large_pages; lp != NULL; lp = lnext)
    {
      gcc_assert (lp->common.large_p);

      lnext = lp->next;

#ifdef GATHER_STATISTICS
      /* This page has now survived another collection.  */
      lp->common.survived++;
#endif

      if (lp->mark_p)
	{
	  lp->mark_p = false;
	  allocated += lp->bytes;
	  lpp = &lp->next;
	}
      else
	{
	  *lpp = lnext;
#ifdef ENABLE_GC_CHECKING
	  /* Poison the page.  */
	  memset (lp->common.page, 0xb5, SMALL_PAGE_SIZE);
#endif
	  if (lp->prev)
	    lp->prev->next = lp->next;
	  if (lp->next)
	    lp->next->prev = lp->prev;
	  free_large_page (lp);
	}
    }

  spp = &zone->pages;
  for (sp = zone->pages; sp != NULL; sp = snext)
    {
      char *object, *last_object;
      char *end;
      alloc_type *alloc_word_p;
      mark_type *mark_word_p;

      gcc_assert (!sp->common.large_p);

      snext = sp->next;

#ifdef GATHER_STATISTICS
      /* This page has now survived another collection.  */
      sp->common.survived++;
#endif

      /* Step through all chunks, consolidate those that are free and
	 insert them into the free lists.  Note that consolidation
	 slows down collection slightly.  */

      last_object = object = sp->common.page;
      end = sp->common.page + SMALL_PAGE_SIZE;
      last_free = NULL;
      nomarksinpage = true;
      mark_word_p = sp->mark_bits;
      alloc_word_p = sp->alloc_bits;

      gcc_assert (BYTES_PER_ALLOC_BIT == BYTES_PER_MARK_BIT);

      object = sp->common.page;
      do
	{
	  unsigned int i, n;
	  alloc_type alloc_word;
	  mark_type mark_word;

	  alloc_word = *alloc_word_p++;
	  mark_word = *mark_word_p++;

	  if (mark_word)
	    nomarksinpage = false;

	  /* There ought to be some way to do this without looping...  */
	  i = 0;
	  while ((n = alloc_ffs (alloc_word)) != 0)
	    {
	      /* Extend the current state for n - 1 bits.  We can't
		 shift alloc_word by n, even though it isn't used in the
		 loop, in case only the highest bit was set.  */
	      alloc_word >>= n - 1;
	      mark_word >>= n - 1;
	      object += BYTES_PER_MARK_BIT * (n - 1);

	      if (mark_word & 1)
		{
		  if (last_free)
		    {
		      VALGRIND_DISCARD (VALGRIND_MAKE_WRITABLE (last_free,
								object
								- last_free));
		      poison_region (last_free, object - last_free);
		      free_chunk (last_free, object - last_free, zone);
		      last_free = NULL;
		    }
		  else
		    allocated += object - last_object;
		  last_object = object;
		}
	      else
		{
		  if (last_free == NULL)
		    {
		      last_free = object;
		      allocated += object - last_object;
		    }
		  else
		    zone_clear_object_alloc_bit (sp, object);
		}

	      /* Shift to just after the alloc bit we handled.  */
	      alloc_word >>= 1;
	      mark_word >>= 1;
	      object += BYTES_PER_MARK_BIT;

	      i += n;
	    }

	  object += BYTES_PER_MARK_BIT * (8 * sizeof (alloc_type) - i);
	}
      while (object < end);

      if (nomarksinpage)
	{
	  *spp = snext;
#ifdef ENABLE_GC_CHECKING
	  VALGRIND_DISCARD (VALGRIND_MAKE_WRITABLE (sp->common.page, SMALL_PAGE_SIZE));
	  /* Poison the page.  */
	  memset (sp->common.page, 0xb5, SMALL_PAGE_SIZE);
#endif
	  free_small_page (sp);
	  continue;
	}
      else if (last_free)
	{
	  VALGRIND_DISCARD (VALGRIND_MAKE_WRITABLE (last_free,
						    object - last_free));
	  poison_region (last_free, object - last_free);
	  free_chunk (last_free, object - last_free, zone);
	}
      else
	allocated += object - last_object;

      spp = &sp->next;
    }

  zone->allocated = allocated;
}

/* mark-and-sweep routine for collecting a single zone.  NEED_MARKING
   is true if we need to mark before sweeping, false if some other
   zone collection has already performed marking for us.  Returns true
   if we collected, false otherwise.  */

static bool
ggc_collect_1 (struct alloc_zone *zone, bool need_marking)
{
#if 0
  /* */
  {
    int i;
    for (i = 0; i < NUM_FREE_BINS + 1; i++)
      {
	struct alloc_chunk *chunk;
	int n, tot;

	n = 0;
	tot = 0;
	chunk = zone->free_chunks[i];
	while (chunk)
	  {
	    n++;
	    tot += chunk->size;
	    chunk = chunk->next_free;
	  }
	fprintf (stderr, "Bin %d: %d free chunks (%d bytes)\n",
		 i, n, tot);
      }
  }
  /* */
#endif

  if (!quiet_flag)
    fprintf (stderr, " {%s GC %luk -> ",
	     zone->name, (unsigned long) zone->allocated / 1024);

  /* Zero the total allocated bytes.  This will be recalculated in the
     sweep phase.  */
  zone->allocated = 0;

  /* Release the pages we freed the last time we collected, but didn't
     reuse in the interim.  */
  release_pages (zone);

  if (need_marking)
    {
      zone_allocate_marks ();
      ggc_mark_roots ();
#ifdef GATHER_STATISTICS
      ggc_prune_overhead_list ();
#endif
    }
  
  sweep_pages (zone);
  zone->was_collected = true;
  zone->allocated_last_gc = zone->allocated;

  if (!quiet_flag)
    fprintf (stderr, "%luk}", (unsigned long) zone->allocated / 1024);
  return true;
}

#ifdef GATHER_STATISTICS
/* Calculate the average page survival rate in terms of number of
   collections.  */

static float
calculate_average_page_survival (struct alloc_zone *zone)
{
  float count = 0.0;
  float survival = 0.0;
  struct small_page_entry *p;
  struct large_page_entry *lp;
  for (p = zone->pages; p; p = p->next)
    {
      count += 1.0;
      survival += p->common.survived;
    }
  for (lp = zone->large_pages; lp; lp = lp->next)
    {
      count += 1.0;
      survival += lp->common.survived;
    }
  return survival/count;
}
#endif

/* Top level collection routine.  */

void
ggc_collect (void)
{
  struct alloc_zone *zone;
  bool marked = false;

  timevar_push (TV_GC);

  if (!ggc_force_collect)
    {
      float allocated_last_gc = 0, allocated = 0, min_expand;

      for (zone = G.zones; zone; zone = zone->next_zone)
	{
	  allocated_last_gc += zone->allocated_last_gc;
	  allocated += zone->allocated;
	}

      allocated_last_gc =
	MAX (allocated_last_gc,
	     (size_t) PARAM_VALUE (GGC_MIN_HEAPSIZE) * 1024);
      min_expand = allocated_last_gc * PARAM_VALUE (GGC_MIN_EXPAND) / 100;

      if (allocated < allocated_last_gc + min_expand)
	{
	  timevar_pop (TV_GC);
	  return;
	}
    }

  /* Start by possibly collecting the main zone.  */
  main_zone.was_collected = false;
  marked |= ggc_collect_1 (&main_zone, true);

  /* In order to keep the number of collections down, we don't
     collect other zones unless we are collecting the main zone.  This
     gives us roughly the same number of collections as we used to
     have with the old gc.  The number of collection is important
     because our main slowdown (according to profiling) is now in
     marking.  So if we mark twice as often as we used to, we'll be
     twice as slow.  Hopefully we'll avoid this cost when we mark
     zone-at-a-time.  */
  /* NOTE drow/2004-07-28: We now always collect the main zone, but
     keep this code in case the heuristics are further refined.  */

  if (main_zone.was_collected)
    {
      struct alloc_zone *zone;

      for (zone = main_zone.next_zone; zone; zone = zone->next_zone)
	{
	  zone->was_collected = false;
	  marked |= ggc_collect_1 (zone, !marked);
	}
    }

#ifdef GATHER_STATISTICS
  /* Print page survival stats, if someone wants them.  */
  if (GGC_DEBUG_LEVEL >= 2)
    {
      for (zone = G.zones; zone; zone = zone->next_zone)
	{
	  if (zone->was_collected)
	    {
	      float f = calculate_average_page_survival (zone);
	      printf ("Average page survival in zone `%s' is %f\n",
		      zone->name, f);
	    }
	}
    }
#endif

  if (marked)
    zone_free_marks ();

  /* Free dead zones.  */
  for (zone = G.zones; zone && zone->next_zone; zone = zone->next_zone)
    {
      if (zone->next_zone->dead)
	{
	  struct alloc_zone *dead_zone = zone->next_zone;

	  printf ("Zone `%s' is dead and will be freed.\n", dead_zone->name);

	  /* The zone must be empty.  */
	  gcc_assert (!dead_zone->allocated);

	  /* Unchain the dead zone, release all its pages and free it.  */
	  zone->next_zone = zone->next_zone->next_zone;
	  release_pages (dead_zone);
	  free (dead_zone);
	}
    }

  timevar_pop (TV_GC);
}

/* Print allocation statistics.  */
#define SCALE(x) ((unsigned long) ((x) < 1024*10 \
		  ? (x) \
		  : ((x) < 1024*1024*10 \
		     ? (x) / 1024 \
		     : (x) / (1024*1024))))
#define LABEL(x) ((x) < 1024*10 ? ' ' : ((x) < 1024*1024*10 ? 'k' : 'M'))

void
ggc_print_statistics (void)
{
  struct alloc_zone *zone;
  struct ggc_statistics stats;
  size_t total_overhead = 0, total_allocated = 0, total_bytes_mapped = 0;
  size_t pte_overhead, i;

  /* Clear the statistics.  */
  memset (&stats, 0, sizeof (stats));

  /* Make sure collection will really occur.  */
  ggc_force_collect = true;

  /* Collect and print the statistics common across collectors.  */
  ggc_print_common_statistics (stderr, &stats);

  ggc_force_collect = false;

  /* Release free pages so that we will not count the bytes allocated
     there as part of the total allocated memory.  */
  for (zone = G.zones; zone; zone = zone->next_zone)
    release_pages (zone);

  /* Collect some information about the various sizes of
     allocation.  */
  fprintf (stderr,
           "Memory still allocated at the end of the compilation process\n");

  fprintf (stderr, "%20s %10s  %10s  %10s\n",
	   "Zone", "Allocated", "Used", "Overhead");
  for (zone = G.zones; zone; zone = zone->next_zone)
    {
      struct large_page_entry *large_page;
      size_t overhead, allocated, in_use;

      /* Skip empty zones.  */
      if (!zone->pages && !zone->large_pages)
	continue;

      allocated = in_use = 0;

      overhead = sizeof (struct alloc_zone);

      for (large_page = zone->large_pages; large_page != NULL;
	   large_page = large_page->next)
	{
	  allocated += large_page->bytes;
	  in_use += large_page->bytes;
	  overhead += sizeof (struct large_page_entry);
	}

      /* There's no easy way to walk through the small pages finding
	 used and unused objects.  Instead, add all the pages, and
	 subtract out the free list.  */

      allocated += GGC_PAGE_SIZE * zone->n_small_pages;
      in_use += GGC_PAGE_SIZE * zone->n_small_pages;
      overhead += G.small_page_overhead * zone->n_small_pages;

      for (i = 0; i <= NUM_FREE_BINS; i++)
	{
	  struct alloc_chunk *chunk = zone->free_chunks[i];
	  while (chunk)
	    {
	      in_use -= ggc_get_size (chunk);
	      chunk = chunk->next_free;
	    }
	}
      
      fprintf (stderr, "%20s %10lu%c %10lu%c %10lu%c\n",
	       zone->name,
	       SCALE (allocated), LABEL (allocated),
	       SCALE (in_use), LABEL (in_use),
	       SCALE (overhead), LABEL (overhead));

      gcc_assert (in_use == zone->allocated);

      total_overhead += overhead;
      total_allocated += zone->allocated;
      total_bytes_mapped += zone->bytes_mapped;
    }

  /* Count the size of the page table as best we can.  */
#if HOST_BITS_PER_PTR <= 32
  pte_overhead = sizeof (G.lookup);
  for (i = 0; i < PAGE_L1_SIZE; i++)
    if (G.lookup[i])
      pte_overhead += PAGE_L2_SIZE * sizeof (struct page_entry *);
#else
  {
    page_table table = G.lookup;
    pte_overhead = 0;
    while (table)
      {
	pte_overhead += sizeof (*table);
	for (i = 0; i < PAGE_L1_SIZE; i++)
	  if (table->table[i])
	    pte_overhead += PAGE_L2_SIZE * sizeof (struct page_entry *);
	table = table->next;
      }
  }
#endif
  fprintf (stderr, "%20s %11s %11s %10lu%c\n", "Page Table",
	   "", "", SCALE (pte_overhead), LABEL (pte_overhead));
  total_overhead += pte_overhead;

  fprintf (stderr, "%20s %10lu%c %10lu%c %10lu%c\n", "Total",
	   SCALE (total_bytes_mapped), LABEL (total_bytes_mapped),
	   SCALE (total_allocated), LABEL(total_allocated),
	   SCALE (total_overhead), LABEL (total_overhead));

#ifdef GATHER_STATISTICS  
  {
    unsigned long long all_overhead = 0, all_allocated = 0;
    unsigned long long all_overhead_under32 = 0, all_allocated_under32 = 0;
    unsigned long long all_overhead_under64 = 0, all_allocated_under64 = 0;
    unsigned long long all_overhead_under128 = 0, all_allocated_under128 = 0;

    fprintf (stderr, "\nTotal allocations and overheads during the compilation process\n");

    for (zone = G.zones; zone; zone = zone->next_zone)
      {
	all_overhead += zone->stats.total_overhead;
	all_allocated += zone->stats.total_allocated;

	all_allocated_under32 += zone->stats.total_allocated_under32;
	all_overhead_under32 += zone->stats.total_overhead_under32;

	all_allocated_under64 += zone->stats.total_allocated_under64;
	all_overhead_under64 += zone->stats.total_overhead_under64;
	
	all_allocated_under128 += zone->stats.total_allocated_under128;
	all_overhead_under128 += zone->stats.total_overhead_under128;

	fprintf (stderr, "%20s:                  %10lld\n",
		 zone->name, zone->stats.total_allocated);
      }

    fprintf (stderr, "\n");

    fprintf (stderr, "Total Overhead:                        %10lld\n",
             all_overhead);
    fprintf (stderr, "Total Allocated:                       %10lld\n",
             all_allocated);

    fprintf (stderr, "Total Overhead  under  32B:            %10lld\n",
             all_overhead_under32);
    fprintf (stderr, "Total Allocated under  32B:            %10lld\n",
             all_allocated_under32);
    fprintf (stderr, "Total Overhead  under  64B:            %10lld\n",
             all_overhead_under64);
    fprintf (stderr, "Total Allocated under  64B:            %10lld\n",
             all_allocated_under64);
    fprintf (stderr, "Total Overhead  under 128B:            %10lld\n",
             all_overhead_under128);
    fprintf (stderr, "Total Allocated under 128B:            %10lld\n",
             all_allocated_under128);
  }
#endif
}

/* Precompiled header support.  */

/* For precompiled headers, we sort objects based on their type.  We
   also sort various objects into their own buckets; currently this
   covers strings and IDENTIFIER_NODE trees.  The choices of how
   to sort buckets have not yet been tuned.  */

#define NUM_PCH_BUCKETS		(gt_types_enum_last + 3)

#define OTHER_BUCKET		(gt_types_enum_last + 0)
#define IDENTIFIER_BUCKET	(gt_types_enum_last + 1)
#define STRING_BUCKET		(gt_types_enum_last + 2)

struct ggc_pch_ondisk
{
  size_t total;
  size_t type_totals[NUM_PCH_BUCKETS];
};

struct ggc_pch_data
{
  struct ggc_pch_ondisk d;
  size_t base;
  size_t orig_base;
  size_t alloc_size;
  alloc_type *alloc_bits;
  size_t type_bases[NUM_PCH_BUCKETS];
  size_t start_offset;
};

/* Initialize the PCH data structure.  */

struct ggc_pch_data *
init_ggc_pch (void)
{
  return xcalloc (sizeof (struct ggc_pch_data), 1);
}

/* Return which of the page-aligned buckets the object at X, with type
   TYPE, should be sorted into in the PCH.  Strings will have
   IS_STRING set and TYPE will be gt_types_enum_last.  Other objects
   of unknown type will also have TYPE equal to gt_types_enum_last.  */

static int
pch_bucket (void *x, enum gt_types_enum type,
	    bool is_string)
{
  /* Sort identifiers into their own bucket, to improve locality
     when searching the identifier hash table.  */
  if (type == gt_ggc_e_14lang_tree_node
      && TREE_CODE ((tree) x) == IDENTIFIER_NODE)
    return IDENTIFIER_BUCKET;
  else if (type == gt_types_enum_last)
    {
      if (is_string)
	return STRING_BUCKET;
      return OTHER_BUCKET;
    }
  return type;
}

/* Add the size of object X to the size of the PCH data.  */

void
ggc_pch_count_object (struct ggc_pch_data *d, void *x ATTRIBUTE_UNUSED,
		      size_t size, bool is_string, enum gt_types_enum type)
{
  /* NOTE: Right now we don't need to align up the size of any objects.
     Strings can be unaligned, and everything else is allocated to a
     MAX_ALIGNMENT boundary already.  */

  d->d.type_totals[pch_bucket (x, type, is_string)] += size;
}

/* Return the total size of the PCH data.  */

size_t
ggc_pch_total_size (struct ggc_pch_data *d)
{
  enum gt_types_enum i;
  size_t alloc_size, total_size;

  total_size = 0;
  for (i = 0; i < NUM_PCH_BUCKETS; i++)
    {
      d->d.type_totals[i] = ROUND_UP (d->d.type_totals[i], GGC_PAGE_SIZE);
      total_size += d->d.type_totals[i];
    }
  d->d.total = total_size;

  /* Include the size of the allocation bitmap.  */
  alloc_size = CEIL (d->d.total, BYTES_PER_ALLOC_BIT * 8);
  alloc_size = ROUND_UP (alloc_size, MAX_ALIGNMENT);
  d->alloc_size = alloc_size;

  return d->d.total + alloc_size;
}

/* Set the base address for the objects in the PCH file.  */

void
ggc_pch_this_base (struct ggc_pch_data *d, void *base_)
{
  int i;
  size_t base = (size_t) base_;

  d->base = d->orig_base = base;
  for (i = 0; i < NUM_PCH_BUCKETS; i++)
    {
      d->type_bases[i] = base;
      base += d->d.type_totals[i];
    }

  if (d->alloc_bits == NULL)
    d->alloc_bits = xcalloc (1, d->alloc_size);
}

/* Allocate a place for object X of size SIZE in the PCH file.  */

char *
ggc_pch_alloc_object (struct ggc_pch_data *d, void *x,
		      size_t size, bool is_string,
		      enum gt_types_enum type)
{
  size_t alloc_word, alloc_bit;
  char *result;
  int bucket = pch_bucket (x, type, is_string);

  /* Record the start of the object in the allocation bitmap.  We
     can't assert that the allocation bit is previously clear, because
     strings may violate the invariant that they are at least
     BYTES_PER_ALLOC_BIT long.  This is harmless - ggc_get_size
     should not be called for strings.  */
  alloc_word = ((d->type_bases[bucket] - d->orig_base)
		/ (8 * sizeof (alloc_type) * BYTES_PER_ALLOC_BIT));
  alloc_bit = ((d->type_bases[bucket] - d->orig_base)
	       / BYTES_PER_ALLOC_BIT) % (8 * sizeof (alloc_type));
  d->alloc_bits[alloc_word] |= 1L << alloc_bit;

  /* Place the object at the current pointer for this bucket.  */
  result = (char *) d->type_bases[bucket];
  d->type_bases[bucket] += size;
  return result;
}

/* Prepare to write out the PCH data to file F.  */

void
ggc_pch_prepare_write (struct ggc_pch_data *d,
		       FILE *f)
{
  /* We seek around a lot while writing.  Record where the end
     of the padding in the PCH file is, so that we can
     locate each object's offset.  */
  d->start_offset = ftell (f);
}

/* Write out object X of SIZE to file F.  */

void
ggc_pch_write_object (struct ggc_pch_data *d,
		      FILE *f, void *x, void *newx,
		      size_t size, bool is_string ATTRIBUTE_UNUSED)
{
  if (fseek (f, (size_t) newx - d->orig_base + d->start_offset, SEEK_SET) != 0)
    fatal_error ("can't seek PCH file: %m");

  if (fwrite (x, size, 1, f) != 1)
    fatal_error ("can't write PCH file: %m");
}

void
ggc_pch_finish (struct ggc_pch_data *d, FILE *f)
{
  /* Write out the allocation bitmap.  */
  if (fseek (f, d->start_offset + d->d.total, SEEK_SET) != 0)
    fatal_error ("can't seek PCH file: %m");

  if (fwrite (d->alloc_bits, d->alloc_size, 1, f) != 1)
    fatal_error ("can't write PCH fle: %m");

  /* Done with the PCH, so write out our footer.  */
  if (fwrite (&d->d, sizeof (d->d), 1, f) != 1)
    fatal_error ("can't write PCH file: %m");

  free (d->alloc_bits);
  free (d);
}

/* The PCH file from F has been mapped at ADDR.  Read in any
   additional data from the file and set up the GC state.  */

void
ggc_pch_read (FILE *f, void *addr)
{
  struct ggc_pch_ondisk d;
  size_t alloc_size;
  struct alloc_zone *zone;
  struct page_entry *pch_page;
  char *p;

  if (fread (&d, sizeof (d), 1, f) != 1)
    fatal_error ("can't read PCH file: %m");

  alloc_size = CEIL (d.total, BYTES_PER_ALLOC_BIT * 8);
  alloc_size = ROUND_UP (alloc_size, MAX_ALIGNMENT);

  pch_zone.bytes = d.total;
  pch_zone.alloc_bits = (alloc_type *) ((char *) addr + pch_zone.bytes);
  pch_zone.page = (char *) addr;
  pch_zone.end = (char *) pch_zone.alloc_bits;

  /* We've just read in a PCH file.  So, every object that used to be
     allocated is now free.  */
  for (zone = G.zones; zone; zone = zone->next_zone)
    {
      struct small_page_entry *page, *next_page;
      struct large_page_entry *large_page, *next_large_page;

      zone->allocated = 0;

      /* Clear the zone's free chunk list.  */
      memset (zone->free_chunks, 0, sizeof (zone->free_chunks));
      zone->high_free_bin = 0;
      zone->cached_free = NULL;
      zone->cached_free_size = 0;

      /* Move all the small pages onto the free list.  */
      for (page = zone->pages; page != NULL; page = next_page)
	{
	  next_page = page->next;
	  memset (page->alloc_bits, 0,
		  G.small_page_overhead - PAGE_OVERHEAD);
	  free_small_page (page);
	}

      /* Discard all the large pages.  */
      for (large_page = zone->large_pages; large_page != NULL;
	   large_page = next_large_page)
	{
	  next_large_page = large_page->next;
	  free_large_page (large_page);
	}

      zone->pages = NULL;
      zone->large_pages = NULL;
    }

  /* Allocate the dummy page entry for the PCH, and set all pages
     mapped into the PCH to reference it.  */
  pch_page = xcalloc (1, sizeof (struct page_entry));
  pch_page->page = pch_zone.page;
  pch_page->pch_p = true;

  for (p = pch_zone.page; p < pch_zone.end; p += GGC_PAGE_SIZE)
    set_page_table_entry (p, pch_page);
}
