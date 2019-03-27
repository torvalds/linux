/* hash.c -- hash table routines for BFD
   Copyright 1993, 1994, 1995, 1997, 1999, 2001, 2002, 2003, 2004, 2005,
   2006, 2007 Free Software Foundation, Inc.
   Written by Steve Chamberlain <sac@cygnus.com>

   This file is part of BFD, the Binary File Descriptor library.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "objalloc.h"
#include "libiberty.h"

/*
SECTION
	Hash Tables

@cindex Hash tables
	BFD provides a simple set of hash table functions.  Routines
	are provided to initialize a hash table, to free a hash table,
	to look up a string in a hash table and optionally create an
	entry for it, and to traverse a hash table.  There is
	currently no routine to delete an string from a hash table.

	The basic hash table does not permit any data to be stored
	with a string.  However, a hash table is designed to present a
	base class from which other types of hash tables may be
	derived.  These derived types may store additional information
	with the string.  Hash tables were implemented in this way,
	rather than simply providing a data pointer in a hash table
	entry, because they were designed for use by the linker back
	ends.  The linker may create thousands of hash table entries,
	and the overhead of allocating private data and storing and
	following pointers becomes noticeable.

	The basic hash table code is in <<hash.c>>.

@menu
@* Creating and Freeing a Hash Table::
@* Looking Up or Entering a String::
@* Traversing a Hash Table::
@* Deriving a New Hash Table Type::
@end menu

INODE
Creating and Freeing a Hash Table, Looking Up or Entering a String, Hash Tables, Hash Tables
SUBSECTION
	Creating and freeing a hash table

@findex bfd_hash_table_init
@findex bfd_hash_table_init_n
	To create a hash table, create an instance of a <<struct
	bfd_hash_table>> (defined in <<bfd.h>>) and call
	<<bfd_hash_table_init>> (if you know approximately how many
	entries you will need, the function <<bfd_hash_table_init_n>>,
	which takes a @var{size} argument, may be used).
	<<bfd_hash_table_init>> returns <<FALSE>> if some sort of
	error occurs.

@findex bfd_hash_newfunc
	The function <<bfd_hash_table_init>> take as an argument a
	function to use to create new entries.  For a basic hash
	table, use the function <<bfd_hash_newfunc>>.  @xref{Deriving
	a New Hash Table Type}, for why you would want to use a
	different value for this argument.

@findex bfd_hash_allocate
	<<bfd_hash_table_init>> will create an objalloc which will be
	used to allocate new entries.  You may allocate memory on this
	objalloc using <<bfd_hash_allocate>>.

@findex bfd_hash_table_free
	Use <<bfd_hash_table_free>> to free up all the memory that has
	been allocated for a hash table.  This will not free up the
	<<struct bfd_hash_table>> itself, which you must provide.

@findex bfd_hash_set_default_size
	Use <<bfd_hash_set_default_size>> to set the default size of
	hash table to use.

INODE
Looking Up or Entering a String, Traversing a Hash Table, Creating and Freeing a Hash Table, Hash Tables
SUBSECTION
	Looking up or entering a string

@findex bfd_hash_lookup
	The function <<bfd_hash_lookup>> is used both to look up a
	string in the hash table and to create a new entry.

	If the @var{create} argument is <<FALSE>>, <<bfd_hash_lookup>>
	will look up a string.  If the string is found, it will
	returns a pointer to a <<struct bfd_hash_entry>>.  If the
	string is not found in the table <<bfd_hash_lookup>> will
	return <<NULL>>.  You should not modify any of the fields in
	the returns <<struct bfd_hash_entry>>.

	If the @var{create} argument is <<TRUE>>, the string will be
	entered into the hash table if it is not already there.
	Either way a pointer to a <<struct bfd_hash_entry>> will be
	returned, either to the existing structure or to a newly
	created one.  In this case, a <<NULL>> return means that an
	error occurred.

	If the @var{create} argument is <<TRUE>>, and a new entry is
	created, the @var{copy} argument is used to decide whether to
	copy the string onto the hash table objalloc or not.  If
	@var{copy} is passed as <<FALSE>>, you must be careful not to
	deallocate or modify the string as long as the hash table
	exists.

INODE
Traversing a Hash Table, Deriving a New Hash Table Type, Looking Up or Entering a String, Hash Tables
SUBSECTION
	Traversing a hash table

@findex bfd_hash_traverse
	The function <<bfd_hash_traverse>> may be used to traverse a
	hash table, calling a function on each element.  The traversal
	is done in a random order.

	<<bfd_hash_traverse>> takes as arguments a function and a
	generic <<void *>> pointer.  The function is called with a
	hash table entry (a <<struct bfd_hash_entry *>>) and the
	generic pointer passed to <<bfd_hash_traverse>>.  The function
	must return a <<boolean>> value, which indicates whether to
	continue traversing the hash table.  If the function returns
	<<FALSE>>, <<bfd_hash_traverse>> will stop the traversal and
	return immediately.

INODE
Deriving a New Hash Table Type, , Traversing a Hash Table, Hash Tables
SUBSECTION
	Deriving a new hash table type

	Many uses of hash tables want to store additional information
	which each entry in the hash table.  Some also find it
	convenient to store additional information with the hash table
	itself.  This may be done using a derived hash table.

	Since C is not an object oriented language, creating a derived
	hash table requires sticking together some boilerplate
	routines with a few differences specific to the type of hash
	table you want to create.

	An example of a derived hash table is the linker hash table.
	The structures for this are defined in <<bfdlink.h>>.  The
	functions are in <<linker.c>>.

	You may also derive a hash table from an already derived hash
	table.  For example, the a.out linker backend code uses a hash
	table derived from the linker hash table.

@menu
@* Define the Derived Structures::
@* Write the Derived Creation Routine::
@* Write Other Derived Routines::
@end menu

INODE
Define the Derived Structures, Write the Derived Creation Routine, Deriving a New Hash Table Type, Deriving a New Hash Table Type
SUBSUBSECTION
	Define the derived structures

	You must define a structure for an entry in the hash table,
	and a structure for the hash table itself.

	The first field in the structure for an entry in the hash
	table must be of the type used for an entry in the hash table
	you are deriving from.  If you are deriving from a basic hash
	table this is <<struct bfd_hash_entry>>, which is defined in
	<<bfd.h>>.  The first field in the structure for the hash
	table itself must be of the type of the hash table you are
	deriving from itself.  If you are deriving from a basic hash
	table, this is <<struct bfd_hash_table>>.

	For example, the linker hash table defines <<struct
	bfd_link_hash_entry>> (in <<bfdlink.h>>).  The first field,
	<<root>>, is of type <<struct bfd_hash_entry>>.  Similarly,
	the first field in <<struct bfd_link_hash_table>>, <<table>>,
	is of type <<struct bfd_hash_table>>.

INODE
Write the Derived Creation Routine, Write Other Derived Routines, Define the Derived Structures, Deriving a New Hash Table Type
SUBSUBSECTION
	Write the derived creation routine

	You must write a routine which will create and initialize an
	entry in the hash table.  This routine is passed as the
	function argument to <<bfd_hash_table_init>>.

	In order to permit other hash tables to be derived from the
	hash table you are creating, this routine must be written in a
	standard way.

	The first argument to the creation routine is a pointer to a
	hash table entry.  This may be <<NULL>>, in which case the
	routine should allocate the right amount of space.  Otherwise
	the space has already been allocated by a hash table type
	derived from this one.

	After allocating space, the creation routine must call the
	creation routine of the hash table type it is derived from,
	passing in a pointer to the space it just allocated.  This
	will initialize any fields used by the base hash table.

	Finally the creation routine must initialize any local fields
	for the new hash table type.

	Here is a boilerplate example of a creation routine.
	@var{function_name} is the name of the routine.
	@var{entry_type} is the type of an entry in the hash table you
	are creating.  @var{base_newfunc} is the name of the creation
	routine of the hash table type your hash table is derived
	from.

EXAMPLE

.struct bfd_hash_entry *
.@var{function_name} (struct bfd_hash_entry *entry,
.                     struct bfd_hash_table *table,
.                     const char *string)
.{
.  struct @var{entry_type} *ret = (@var{entry_type} *) entry;
.
. {* Allocate the structure if it has not already been allocated by a
.    derived class.  *}
.  if (ret == NULL)
.    {
.      ret = bfd_hash_allocate (table, sizeof (* ret));
.      if (ret == NULL)
.        return NULL;
.    }
.
. {* Call the allocation method of the base class.  *}
.  ret = ((@var{entry_type} *)
.	 @var{base_newfunc} ((struct bfd_hash_entry *) ret, table, string));
.
. {* Initialize the local fields here.  *}
.
.  return (struct bfd_hash_entry *) ret;
.}

DESCRIPTION
	The creation routine for the linker hash table, which is in
	<<linker.c>>, looks just like this example.
	@var{function_name} is <<_bfd_link_hash_newfunc>>.
	@var{entry_type} is <<struct bfd_link_hash_entry>>.
	@var{base_newfunc} is <<bfd_hash_newfunc>>, the creation
	routine for a basic hash table.

	<<_bfd_link_hash_newfunc>> also initializes the local fields
	in a linker hash table entry: <<type>>, <<written>> and
	<<next>>.

INODE
Write Other Derived Routines, , Write the Derived Creation Routine, Deriving a New Hash Table Type
SUBSUBSECTION
	Write other derived routines

	You will want to write other routines for your new hash table,
	as well.

	You will want an initialization routine which calls the
	initialization routine of the hash table you are deriving from
	and initializes any other local fields.  For the linker hash
	table, this is <<_bfd_link_hash_table_init>> in <<linker.c>>.

	You will want a lookup routine which calls the lookup routine
	of the hash table you are deriving from and casts the result.
	The linker hash table uses <<bfd_link_hash_lookup>> in
	<<linker.c>> (this actually takes an additional argument which
	it uses to decide how to return the looked up value).

	You may want a traversal routine.  This should just call the
	traversal routine of the hash table you are deriving from with
	appropriate casts.  The linker hash table uses
	<<bfd_link_hash_traverse>> in <<linker.c>>.

	These routines may simply be defined as macros.  For example,
	the a.out backend linker hash table, which is derived from the
	linker hash table, uses macros for the lookup and traversal
	routines.  These are <<aout_link_hash_lookup>> and
	<<aout_link_hash_traverse>> in aoutx.h.
*/

/* The default number of entries to use when creating a hash table.  */
#define DEFAULT_SIZE 4051

/* The following function returns a nearest prime number which is
   greater than N, and near a power of two.  Copied from libiberty.
   Returns zero for ridiculously large N to signify an error.  */

static unsigned long
higher_prime_number (unsigned long n)
{
  /* These are primes that are near, but slightly smaller than, a
     power of two.  */
  static const unsigned long primes[] = {
    (unsigned long) 127,
    (unsigned long) 2039,
    (unsigned long) 32749,
    (unsigned long) 65521,
    (unsigned long) 131071,
    (unsigned long) 262139,
    (unsigned long) 524287,
    (unsigned long) 1048573,
    (unsigned long) 2097143,
    (unsigned long) 4194301,
    (unsigned long) 8388593,
    (unsigned long) 16777213,
    (unsigned long) 33554393,
    (unsigned long) 67108859,
    (unsigned long) 134217689,
    (unsigned long) 268435399,
    (unsigned long) 536870909,
    (unsigned long) 1073741789,
    (unsigned long) 2147483647,
					/* 4294967291L */
    ((unsigned long) 2147483647) + ((unsigned long) 2147483644),
  };

  const unsigned long *low = &primes[0];
  const unsigned long *high = &primes[sizeof (primes) / sizeof (primes[0])];

  while (low != high)
    {
      const unsigned long *mid = low + (high - low) / 2;
      if (n >= *mid)
	low = mid + 1;
      else
	high = mid;
    }

  if (n >= *low)
    return 0;

  return *low;
}

static size_t bfd_default_hash_table_size = DEFAULT_SIZE;

/* Create a new hash table, given a number of entries.  */

bfd_boolean
bfd_hash_table_init_n (struct bfd_hash_table *table,
		       struct bfd_hash_entry *(*newfunc) (struct bfd_hash_entry *,
							  struct bfd_hash_table *,
							  const char *),
		       unsigned int entsize,
		       unsigned int size)
{
  unsigned int alloc;

  alloc = size * sizeof (struct bfd_hash_entry *);

  table->memory = (void *) objalloc_create ();
  if (table->memory == NULL)
    {
      bfd_set_error (bfd_error_no_memory);
      return FALSE;
    }
  table->table = objalloc_alloc ((struct objalloc *) table->memory, alloc);
  if (table->table == NULL)
    {
      bfd_set_error (bfd_error_no_memory);
      return FALSE;
    }
  memset ((void *) table->table, 0, alloc);
  table->size = size;
  table->entsize = entsize;
  table->count = 0;
  table->frozen = 0;
  table->newfunc = newfunc;
  return TRUE;
}

/* Create a new hash table with the default number of entries.  */

bfd_boolean
bfd_hash_table_init (struct bfd_hash_table *table,
		     struct bfd_hash_entry *(*newfunc) (struct bfd_hash_entry *,
							struct bfd_hash_table *,
							const char *),
		     unsigned int entsize)
{
  return bfd_hash_table_init_n (table, newfunc, entsize,
				bfd_default_hash_table_size);
}

/* Free a hash table.  */

void
bfd_hash_table_free (struct bfd_hash_table *table)
{
  objalloc_free (table->memory);
  table->memory = NULL;
}

/* Look up a string in a hash table.  */

struct bfd_hash_entry *
bfd_hash_lookup (struct bfd_hash_table *table,
		 const char *string,
		 bfd_boolean create,
		 bfd_boolean copy)
{
  const unsigned char *s;
  unsigned long hash;
  unsigned int c;
  struct bfd_hash_entry *hashp;
  unsigned int len;
  unsigned int index;

  hash = 0;
  len = 0;
  s = (const unsigned char *) string;
  while ((c = *s++) != '\0')
    {
      hash += c + (c << 17);
      hash ^= hash >> 2;
    }
  len = (s - (const unsigned char *) string) - 1;
  hash += len + (len << 17);
  hash ^= hash >> 2;

  index = hash % table->size;
  for (hashp = table->table[index];
       hashp != NULL;
       hashp = hashp->next)
    {
      if (hashp->hash == hash
	  && strcmp (hashp->string, string) == 0)
	return hashp;
    }

  if (! create)
    return NULL;

  hashp = (*table->newfunc) (NULL, table, string);
  if (hashp == NULL)
    return NULL;
  if (copy)
    {
      char *new;

      new = objalloc_alloc ((struct objalloc *) table->memory, len + 1);
      if (!new)
	{
	  bfd_set_error (bfd_error_no_memory);
	  return NULL;
	}
      memcpy (new, string, len + 1);
      string = new;
    }
  hashp->string = string;
  hashp->hash = hash;
  hashp->next = table->table[index];
  table->table[index] = hashp;
  table->count++;

  if (!table->frozen && table->count > table->size * 3 / 4)
    {
      unsigned long newsize = higher_prime_number (table->size);
      struct bfd_hash_entry **newtable;
      unsigned int hi;
      unsigned long alloc = newsize * sizeof (struct bfd_hash_entry *);

      /* If we can't find a higher prime, or we can't possibly alloc
	 that much memory, don't try to grow the table.  */
      if (newsize == 0 || alloc / sizeof (struct bfd_hash_entry *) != newsize)
	{
	  table->frozen = 1;
	  return hashp;
	}

      newtable = ((struct bfd_hash_entry **)
		  objalloc_alloc ((struct objalloc *) table->memory, alloc));
      memset ((PTR) newtable, 0, alloc);

      for (hi = 0; hi < table->size; hi ++)
	while (table->table[hi])
	  {
	    struct bfd_hash_entry *chain = table->table[hi];
	    struct bfd_hash_entry *chain_end = chain;
	    int index;

	    while (chain_end->next && chain_end->next->hash == chain->hash)
	      chain_end = chain_end->next;

	    table->table[hi] = chain_end->next;
	    index = chain->hash % newsize;
	    chain_end->next = newtable[index];
	    newtable[index] = chain;
	  }
      table->table = newtable;
      table->size = newsize;
    }

  return hashp;
}

/* Replace an entry in a hash table.  */

void
bfd_hash_replace (struct bfd_hash_table *table,
		  struct bfd_hash_entry *old,
		  struct bfd_hash_entry *nw)
{
  unsigned int index;
  struct bfd_hash_entry **pph;

  index = old->hash % table->size;
  for (pph = &table->table[index];
       (*pph) != NULL;
       pph = &(*pph)->next)
    {
      if (*pph == old)
	{
	  *pph = nw;
	  return;
	}
    }

  abort ();
}

/* Allocate space in a hash table.  */

void *
bfd_hash_allocate (struct bfd_hash_table *table,
		   unsigned int size)
{
  void * ret;

  ret = objalloc_alloc ((struct objalloc *) table->memory, size);
  if (ret == NULL && size != 0)
    bfd_set_error (bfd_error_no_memory);
  return ret;
}

/* Base method for creating a new hash table entry.  */

struct bfd_hash_entry *
bfd_hash_newfunc (struct bfd_hash_entry *entry,
		  struct bfd_hash_table *table,
		  const char *string ATTRIBUTE_UNUSED)
{
  if (entry == NULL)
    entry = bfd_hash_allocate (table, sizeof (* entry));
  return entry;
}

/* Traverse a hash table.  */

void
bfd_hash_traverse (struct bfd_hash_table *table,
		   bfd_boolean (*func) (struct bfd_hash_entry *, void *),
		   void * info)
{
  unsigned int i;

  table->frozen = 1;
  for (i = 0; i < table->size; i++)
    {
      struct bfd_hash_entry *p;

      for (p = table->table[i]; p != NULL; p = p->next)
	if (! (*func) (p, info))
	  goto out;
    }
 out:
  table->frozen = 0;
}

void
bfd_hash_set_default_size (bfd_size_type hash_size)
{
  /* Extend this prime list if you want more granularity of hash table size.  */
  static const bfd_size_type hash_size_primes[] =
    {
      251, 509, 1021, 2039, 4051, 8599, 16699, 32749
    };
  size_t index;

  /* Work out best prime number near the hash_size.  */
  for (index = 0; index < ARRAY_SIZE (hash_size_primes) - 1; ++index)
    if (hash_size <= hash_size_primes[index])
      break;

  bfd_default_hash_table_size = hash_size_primes[index];
}

/* A few different object file formats (a.out, COFF, ELF) use a string
   table.  These functions support adding strings to a string table,
   returning the byte offset, and writing out the table.

   Possible improvements:
   + look for strings matching trailing substrings of other strings
   + better data structures?  balanced trees?
   + look at reducing memory use elsewhere -- maybe if we didn't have
     to construct the entire symbol table at once, we could get by
     with smaller amounts of VM?  (What effect does that have on the
     string table reductions?)  */

/* An entry in the strtab hash table.  */

struct strtab_hash_entry
{
  struct bfd_hash_entry root;
  /* Index in string table.  */
  bfd_size_type index;
  /* Next string in strtab.  */
  struct strtab_hash_entry *next;
};

/* The strtab hash table.  */

struct bfd_strtab_hash
{
  struct bfd_hash_table table;
  /* Size of strtab--also next available index.  */
  bfd_size_type size;
  /* First string in strtab.  */
  struct strtab_hash_entry *first;
  /* Last string in strtab.  */
  struct strtab_hash_entry *last;
  /* Whether to precede strings with a two byte length, as in the
     XCOFF .debug section.  */
  bfd_boolean xcoff;
};

/* Routine to create an entry in a strtab.  */

static struct bfd_hash_entry *
strtab_hash_newfunc (struct bfd_hash_entry *entry,
		     struct bfd_hash_table *table,
		     const char *string)
{
  struct strtab_hash_entry *ret = (struct strtab_hash_entry *) entry;

  /* Allocate the structure if it has not already been allocated by a
     subclass.  */
  if (ret == NULL)
    ret = bfd_hash_allocate (table, sizeof (* ret));
  if (ret == NULL)
    return NULL;

  /* Call the allocation method of the superclass.  */
  ret = (struct strtab_hash_entry *)
	 bfd_hash_newfunc ((struct bfd_hash_entry *) ret, table, string);

  if (ret)
    {
      /* Initialize the local fields.  */
      ret->index = (bfd_size_type) -1;
      ret->next = NULL;
    }

  return (struct bfd_hash_entry *) ret;
}

/* Look up an entry in an strtab.  */

#define strtab_hash_lookup(t, string, create, copy) \
  ((struct strtab_hash_entry *) \
   bfd_hash_lookup (&(t)->table, (string), (create), (copy)))

/* Create a new strtab.  */

struct bfd_strtab_hash *
_bfd_stringtab_init (void)
{
  struct bfd_strtab_hash *table;
  bfd_size_type amt = sizeof (* table);

  table = bfd_malloc (amt);
  if (table == NULL)
    return NULL;

  if (!bfd_hash_table_init (&table->table, strtab_hash_newfunc,
			    sizeof (struct strtab_hash_entry)))
    {
      free (table);
      return NULL;
    }

  table->size = 0;
  table->first = NULL;
  table->last = NULL;
  table->xcoff = FALSE;

  return table;
}

/* Create a new strtab in which the strings are output in the format
   used in the XCOFF .debug section: a two byte length precedes each
   string.  */

struct bfd_strtab_hash *
_bfd_xcoff_stringtab_init (void)
{
  struct bfd_strtab_hash *ret;

  ret = _bfd_stringtab_init ();
  if (ret != NULL)
    ret->xcoff = TRUE;
  return ret;
}

/* Free a strtab.  */

void
_bfd_stringtab_free (struct bfd_strtab_hash *table)
{
  bfd_hash_table_free (&table->table);
  free (table);
}

/* Get the index of a string in a strtab, adding it if it is not
   already present.  If HASH is FALSE, we don't really use the hash
   table, and we don't eliminate duplicate strings.  */

bfd_size_type
_bfd_stringtab_add (struct bfd_strtab_hash *tab,
		    const char *str,
		    bfd_boolean hash,
		    bfd_boolean copy)
{
  struct strtab_hash_entry *entry;

  if (hash)
    {
      entry = strtab_hash_lookup (tab, str, TRUE, copy);
      if (entry == NULL)
	return (bfd_size_type) -1;
    }
  else
    {
      entry = bfd_hash_allocate (&tab->table, sizeof (* entry));
      if (entry == NULL)
	return (bfd_size_type) -1;
      if (! copy)
	entry->root.string = str;
      else
	{
	  char *n;

	  n = bfd_hash_allocate (&tab->table, strlen (str) + 1);
	  if (n == NULL)
	    return (bfd_size_type) -1;
	  entry->root.string = n;
	}
      entry->index = (bfd_size_type) -1;
      entry->next = NULL;
    }

  if (entry->index == (bfd_size_type) -1)
    {
      entry->index = tab->size;
      tab->size += strlen (str) + 1;
      if (tab->xcoff)
	{
	  entry->index += 2;
	  tab->size += 2;
	}
      if (tab->first == NULL)
	tab->first = entry;
      else
	tab->last->next = entry;
      tab->last = entry;
    }

  return entry->index;
}

/* Get the number of bytes in a strtab.  */

bfd_size_type
_bfd_stringtab_size (struct bfd_strtab_hash *tab)
{
  return tab->size;
}

/* Write out a strtab.  ABFD must already be at the right location in
   the file.  */

bfd_boolean
_bfd_stringtab_emit (bfd *abfd, struct bfd_strtab_hash *tab)
{
  bfd_boolean xcoff;
  struct strtab_hash_entry *entry;

  xcoff = tab->xcoff;

  for (entry = tab->first; entry != NULL; entry = entry->next)
    {
      const char *str;
      size_t len;

      str = entry->root.string;
      len = strlen (str) + 1;

      if (xcoff)
	{
	  bfd_byte buf[2];

	  /* The output length includes the null byte.  */
	  bfd_put_16 (abfd, (bfd_vma) len, buf);
	  if (bfd_bwrite ((void *) buf, (bfd_size_type) 2, abfd) != 2)
	    return FALSE;
	}

      if (bfd_bwrite ((void *) str, (bfd_size_type) len, abfd) != len)
	return FALSE;
    }

  return TRUE;
}
