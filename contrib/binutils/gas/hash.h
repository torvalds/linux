/* hash.h -- header file for gas hash table routines
   Copyright 1987, 1992, 1993, 1995, 1999, 2003
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

#ifndef HASH_H
#define HASH_H

struct hash_control;

/* Set the size of the hash table used.  */

void set_gas_hash_table_size (unsigned long);

/* Create a hash table.  This return a control block.  */

extern struct hash_control *hash_new (void);

/* Delete a hash table, freeing all allocated memory.  */

extern void hash_die (struct hash_control *);

/* Insert an entry into a hash table.  This returns NULL on success.
   On error, it returns a printable string indicating the error.  It
   is considered to be an error if the entry already exists in the
   hash table.  */

extern const char *hash_insert (struct hash_control *,
				const char *key, PTR value);

/* Insert or replace an entry in a hash table.  This returns NULL on
   success.  On error, it returns a printable string indicating the
   error.  If an entry already exists, its value is replaced.  */

extern const char *hash_jam (struct hash_control *,
			     const char *key, PTR value);

/* Replace an existing entry in a hash table.  This returns the old
   value stored for the entry.  If the entry is not found in the hash
   table, this does nothing and returns NULL.  */

extern PTR hash_replace (struct hash_control *, const char *key,
			 PTR value);

/* Find an entry in a hash table, returning its value.  Returns NULL
   if the entry is not found.  */

extern PTR hash_find (struct hash_control *, const char *key);

/* As hash_find, but KEY is of length LEN and is not guaranteed to be
   NUL-terminated.  */

extern PTR hash_find_n (struct hash_control *, const char *key, size_t len);

/* Delete an entry from a hash table.  This returns the value stored
   for that entry, or NULL if there is no such entry.  */

extern PTR hash_delete (struct hash_control *, const char *key);

/* Traverse a hash table.  Call the function on every entry in the
   hash table.  */

extern void hash_traverse (struct hash_control *,
			   void (*pfn) (const char *key, PTR value));

/* Print hash table statistics on the specified file.  NAME is the
   name of the hash table, used for printing a header.  */

extern void hash_print_statistics (FILE *, const char *name,
				   struct hash_control *);

#endif /* HASH_H */
