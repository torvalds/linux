/* hash.c -- gas hash table code
   Copyright 1987, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1998, 1999,
   2000, 2001, 2002, 2003, 2005
   Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* This version of the hash table code is a wholescale replacement of
   the old hash table code, which was fairly bad.  This is based on
   the hash table code in BFD, but optimized slightly for the
   assembler.  The assembler does not need to derive structures that
   are stored in the hash table.  Instead, it always stores a pointer.
   The assembler uses the hash table mostly to store symbols, and we
   don't need to confuse the symbol structure with a hash table
   structure.  */

#include "as.h"
#include "safe-ctype.h"
#include "obstack.h"

/* An entry in a hash table.  */

struct hash_entry {
  /* Next entry for this hash code.  */
  struct hash_entry *next;
  /* String being hashed.  */
  const char *string;
  /* Hash code.  This is the full hash code, not the index into the
     table.  */
  unsigned long hash;
  /* Pointer being stored in the hash table.  */
  PTR data;
};

/* A hash table.  */

struct hash_control {
  /* The hash array.  */
  struct hash_entry **table;
  /* The number of slots in the hash table.  */
  unsigned int size;
  /* An obstack for this hash table.  */
  struct obstack memory;

#ifdef HASH_STATISTICS
  /* Statistics.  */
  unsigned long lookups;
  unsigned long hash_compares;
  unsigned long string_compares;
  unsigned long insertions;
  unsigned long replacements;
  unsigned long deletions;
#endif /* HASH_STATISTICS */
};

/* The default number of entries to use when creating a hash table.
   Note this value can be reduced to 4051 by using the command line
   switch --reduce-memory-overheads, or set to other values by using
   the --hash-size=<NUMBER> switch.  */

static unsigned long gas_hash_table_size = 65537;

void
set_gas_hash_table_size (unsigned long size)
{
  gas_hash_table_size = size;
}

/* FIXME: This function should be amalgmated with bfd/hash.c:bfd_hash_set_default_size().  */
static unsigned long
get_gas_hash_table_size (void)
{
  /* Extend this prime list if you want more granularity of hash table size.  */
  static const unsigned long hash_size_primes[] =
    {
      1021, 4051, 8599, 16699, 65537
    };
  unsigned int index;

  /* Work out the best prime number near the hash_size.
     FIXME: This could be a more sophisticated algorithm,
     but is it really worth implementing it ?   */
  for (index = 0; index < ARRAY_SIZE (hash_size_primes) - 1; ++index)
    if (gas_hash_table_size <= hash_size_primes[index])
      break;

  return hash_size_primes[index];
}

/* Create a hash table.  This return a control block.  */

struct hash_control *
hash_new (void)
{
  unsigned long size;
  unsigned long alloc;
  struct hash_control *ret;

  size = get_gas_hash_table_size ();

  ret = xmalloc (sizeof *ret);
  obstack_begin (&ret->memory, chunksize);
  alloc = size * sizeof (struct hash_entry *);
  ret->table = obstack_alloc (&ret->memory, alloc);
  memset (ret->table, 0, alloc);
  ret->size = size;

#ifdef HASH_STATISTICS
  ret->lookups = 0;
  ret->hash_compares = 0;
  ret->string_compares = 0;
  ret->insertions = 0;
  ret->replacements = 0;
  ret->deletions = 0;
#endif

  return ret;
}

/* Delete a hash table, freeing all allocated memory.  */

void
hash_die (struct hash_control *table)
{
  obstack_free (&table->memory, 0);
  free (table);
}

/* Look up a string in a hash table.  This returns a pointer to the
   hash_entry, or NULL if the string is not in the table.  If PLIST is
   not NULL, this sets *PLIST to point to the start of the list which
   would hold this hash entry.  If PHASH is not NULL, this sets *PHASH
   to the hash code for KEY.

   Each time we look up a string, we move it to the start of the list
   for its hash code, to take advantage of referential locality.  */

static struct hash_entry *
hash_lookup (struct hash_control *table, const char *key, size_t len,
	     struct hash_entry ***plist, unsigned long *phash)
{
  unsigned long hash;
  size_t n;
  unsigned int c;
  unsigned int index;
  struct hash_entry **list;
  struct hash_entry *p;
  struct hash_entry *prev;

#ifdef HASH_STATISTICS
  ++table->lookups;
#endif

  hash = 0;
  for (n = 0; n < len; n++)
    {
      c = key[n];
      hash += c + (c << 17);
      hash ^= hash >> 2;
    }
  hash += len + (len << 17);
  hash ^= hash >> 2;

  if (phash != NULL)
    *phash = hash;

  index = hash % table->size;
  list = table->table + index;

  if (plist != NULL)
    *plist = list;

  prev = NULL;
  for (p = *list; p != NULL; p = p->next)
    {
#ifdef HASH_STATISTICS
      ++table->hash_compares;
#endif

      if (p->hash == hash)
	{
#ifdef HASH_STATISTICS
	  ++table->string_compares;
#endif

	  if (strncmp (p->string, key, len) == 0 && p->string[len] == '\0')
	    {
	      if (prev != NULL)
		{
		  prev->next = p->next;
		  p->next = *list;
		  *list = p;
		}

	      return p;
	    }
	}

      prev = p;
    }

  return NULL;
}

/* Insert an entry into a hash table.  This returns NULL on success.
   On error, it returns a printable string indicating the error.  It
   is considered to be an error if the entry already exists in the
   hash table.  */

const char *
hash_insert (struct hash_control *table, const char *key, PTR value)
{
  struct hash_entry *p;
  struct hash_entry **list;
  unsigned long hash;

  p = hash_lookup (table, key, strlen (key), &list, &hash);
  if (p != NULL)
    return "exists";

#ifdef HASH_STATISTICS
  ++table->insertions;
#endif

  p = (struct hash_entry *) obstack_alloc (&table->memory, sizeof (*p));
  p->string = key;
  p->hash = hash;
  p->data = value;

  p->next = *list;
  *list = p;

  return NULL;
}

/* Insert or replace an entry in a hash table.  This returns NULL on
   success.  On error, it returns a printable string indicating the
   error.  If an entry already exists, its value is replaced.  */

const char *
hash_jam (struct hash_control *table, const char *key, PTR value)
{
  struct hash_entry *p;
  struct hash_entry **list;
  unsigned long hash;

  p = hash_lookup (table, key, strlen (key), &list, &hash);
  if (p != NULL)
    {
#ifdef HASH_STATISTICS
      ++table->replacements;
#endif

      p->data = value;
    }
  else
    {
#ifdef HASH_STATISTICS
      ++table->insertions;
#endif

      p = (struct hash_entry *) obstack_alloc (&table->memory, sizeof (*p));
      p->string = key;
      p->hash = hash;
      p->data = value;

      p->next = *list;
      *list = p;
    }

  return NULL;
}

/* Replace an existing entry in a hash table.  This returns the old
   value stored for the entry.  If the entry is not found in the hash
   table, this does nothing and returns NULL.  */

PTR
hash_replace (struct hash_control *table, const char *key, PTR value)
{
  struct hash_entry *p;
  PTR ret;

  p = hash_lookup (table, key, strlen (key), NULL, NULL);
  if (p == NULL)
    return NULL;

#ifdef HASH_STATISTICS
  ++table->replacements;
#endif

  ret = p->data;

  p->data = value;

  return ret;
}

/* Find an entry in a hash table, returning its value.  Returns NULL
   if the entry is not found.  */

PTR
hash_find (struct hash_control *table, const char *key)
{
  struct hash_entry *p;

  p = hash_lookup (table, key, strlen (key), NULL, NULL);
  if (p == NULL)
    return NULL;

  return p->data;
}

/* As hash_find, but KEY is of length LEN and is not guaranteed to be
   NUL-terminated.  */

PTR
hash_find_n (struct hash_control *table, const char *key, size_t len)
{
  struct hash_entry *p;

  p = hash_lookup (table, key, len, NULL, NULL);
  if (p == NULL)
    return NULL;

  return p->data;
}

/* Delete an entry from a hash table.  This returns the value stored
   for that entry, or NULL if there is no such entry.  */

PTR
hash_delete (struct hash_control *table, const char *key)
{
  struct hash_entry *p;
  struct hash_entry **list;

  p = hash_lookup (table, key, strlen (key), &list, NULL);
  if (p == NULL)
    return NULL;

  if (p != *list)
    abort ();

#ifdef HASH_STATISTICS
  ++table->deletions;
#endif

  *list = p->next;

  /* Note that we never reclaim the memory for this entry.  If gas
     ever starts deleting hash table entries in a big way, this will
     have to change.  */

  return p->data;
}

/* Traverse a hash table.  Call the function on every entry in the
   hash table.  */

void
hash_traverse (struct hash_control *table,
	       void (*pfn) (const char *key, PTR value))
{
  unsigned int i;

  for (i = 0; i < table->size; ++i)
    {
      struct hash_entry *p;

      for (p = table->table[i]; p != NULL; p = p->next)
	(*pfn) (p->string, p->data);
    }
}

/* Print hash table statistics on the specified file.  NAME is the
   name of the hash table, used for printing a header.  */

void
hash_print_statistics (FILE *f ATTRIBUTE_UNUSED,
		       const char *name ATTRIBUTE_UNUSED,
		       struct hash_control *table ATTRIBUTE_UNUSED)
{
#ifdef HASH_STATISTICS
  unsigned int i;
  unsigned long total;
  unsigned long empty;

  fprintf (f, "%s hash statistics:\n", name);
  fprintf (f, "\t%lu lookups\n", table->lookups);
  fprintf (f, "\t%lu hash comparisons\n", table->hash_compares);
  fprintf (f, "\t%lu string comparisons\n", table->string_compares);
  fprintf (f, "\t%lu insertions\n", table->insertions);
  fprintf (f, "\t%lu replacements\n", table->replacements);
  fprintf (f, "\t%lu deletions\n", table->deletions);

  total = 0;
  empty = 0;
  for (i = 0; i < table->size; ++i)
    {
      struct hash_entry *p;

      if (table->table[i] == NULL)
	++empty;
      else
	{
	  for (p = table->table[i]; p != NULL; p = p->next)
	    ++total;
	}
    }

  fprintf (f, "\t%g average chain length\n", (double) total / table->size);
  fprintf (f, "\t%lu empty slots\n", empty);
#endif
}

#ifdef TEST

/* This test program is left over from the old hash table code.  */

/* Number of hash tables to maintain (at once) in any testing.  */
#define TABLES (6)

/* We can have 12 statistics.  */
#define STATBUFSIZE (12)

/* Display statistics here.  */
int statbuf[STATBUFSIZE];

/* Human farts here.  */
char answer[100];

/* We test many hash tables at once.  */
char *hashtable[TABLES];

/* Points to current hash_control.  */
char *h;
char **pp;
char *p;
char *name;
char *value;
int size;
int used;
char command;

/* Number 0:TABLES-1 of current hashed symbol table.  */
int number;

int
main ()
{
  void applicatee ();
  void destroy ();
  char *what ();
  int *ip;

  number = 0;
  h = 0;
  printf ("type h <RETURN> for help\n");
  for (;;)
    {
      printf ("hash_test command: ");
      gets (answer);
      command = answer[0];
      command = TOLOWER (command);	/* Ecch!  */
      switch (command)
	{
	case '#':
	  printf ("old hash table #=%d.\n", number);
	  whattable ();
	  break;
	case '?':
	  for (pp = hashtable; pp < hashtable + TABLES; pp++)
	    {
	      printf ("address of hash table #%d control block is %xx\n",
		      pp - hashtable, *pp);
	    }
	  break;
	case 'a':
	  hash_traverse (h, applicatee);
	  break;
	case 'd':
	  hash_traverse (h, destroy);
	  hash_die (h);
	  break;
	case 'f':
	  p = hash_find (h, name = what ("symbol"));
	  printf ("value of \"%s\" is \"%s\"\n", name, p ? p : "NOT-PRESENT");
	  break;
	case 'h':
	  printf ("# show old, select new default hash table number\n");
	  printf ("? display all hashtable control block addresses\n");
	  printf ("a apply a simple display-er to each symbol in table\n");
	  printf ("d die: destroy hashtable\n");
	  printf ("f find value of nominated symbol\n");
	  printf ("h this help\n");
	  printf ("i insert value into symbol\n");
	  printf ("j jam value into symbol\n");
	  printf ("n new hashtable\n");
	  printf ("r replace a value with another\n");
	  printf ("s say what %% of table is used\n");
	  printf ("q exit this program\n");
	  printf ("x delete a symbol from table, report its value\n");
	  break;
	case 'i':
	  p = hash_insert (h, name = what ("symbol"), value = what ("value"));
	  if (p)
	    {
	      printf ("symbol=\"%s\"  value=\"%s\"  error=%s\n", name, value,
		      p);
	    }
	  break;
	case 'j':
	  p = hash_jam (h, name = what ("symbol"), value = what ("value"));
	  if (p)
	    {
	      printf ("symbol=\"%s\"  value=\"%s\"  error=%s\n", name, value, p);
	    }
	  break;
	case 'n':
	  h = hashtable[number] = (char *) hash_new ();
	  break;
	case 'q':
	  exit (EXIT_SUCCESS);
	case 'r':
	  p = hash_replace (h, name = what ("symbol"), value = what ("value"));
	  printf ("old value was \"%s\"\n", p ? p : "{}");
	  break;
	case 's':
	  hash_say (h, statbuf, STATBUFSIZE);
	  for (ip = statbuf; ip < statbuf + STATBUFSIZE; ip++)
	    {
	      printf ("%d ", *ip);
	    }
	  printf ("\n");
	  break;
	case 'x':
	  p = hash_delete (h, name = what ("symbol"));
	  printf ("old value was \"%s\"\n", p ? p : "{}");
	  break;
	default:
	  printf ("I can't understand command \"%c\"\n", command);
	  break;
	}
    }
}

char *
what (description)
     char *description;
{
  printf ("   %s : ", description);
  gets (answer);
  return xstrdup (answer);
}

void
destroy (string, value)
     char *string;
     char *value;
{
  free (string);
  free (value);
}

void
applicatee (string, value)
     char *string;
     char *value;
{
  printf ("%.20s-%.20s\n", string, value);
}

/* Determine number: what hash table to use.
   Also determine h: points to hash_control.  */

void
whattable ()
{
  for (;;)
    {
      printf ("   what hash table (%d:%d) ?  ", 0, TABLES - 1);
      gets (answer);
      sscanf (answer, "%d", &number);
      if (number >= 0 && number < TABLES)
	{
	  h = hashtable[number];
	  if (!h)
	    {
	      printf ("warning: current hash-table-#%d. has no hash-control\n", number);
	    }
	  return;
	}
      else
	{
	  printf ("invalid hash table number: %d\n", number);
	}
    }
}

#endif /* TEST */
