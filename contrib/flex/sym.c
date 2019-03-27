/* sym - symbol table routines */

/*  Copyright (c) 1990 The Regents of the University of California. */
/*  All rights reserved. */

/*  This code is derived from software contributed to Berkeley by */
/*  Vern Paxson. */

/*  The United States Government has rights in this work pursuant */
/*  to contract no. DE-AC03-76SF00098 between the United States */
/*  Department of Energy and the University of California. */

/*  This file is part of flex. */

/*  Redistribution and use in source and binary forms, with or without */
/*  modification, are permitted provided that the following conditions */
/*  are met: */

/*  1. Redistributions of source code must retain the above copyright */
/*     notice, this list of conditions and the following disclaimer. */
/*  2. Redistributions in binary form must reproduce the above copyright */
/*     notice, this list of conditions and the following disclaimer in the */
/*     documentation and/or other materials provided with the distribution. */

/*  Neither the name of the University nor the names of its contributors */
/*  may be used to endorse or promote products derived from this software */
/*  without specific prior written permission. */

/*  THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR */
/*  IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED */
/*  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR */
/*  PURPOSE. */

#include "flexdef.h"

/* Variables for symbol tables:
 * sctbl - start-condition symbol table
 * ndtbl - name-definition symbol table
 * ccltab - character class text symbol table
 */

struct hash_entry {
	struct hash_entry *prev, *next;
	char   *name;
	char   *str_val;
	int     int_val;
};

typedef struct hash_entry **hash_table;

#define NAME_TABLE_HASH_SIZE 101
#define START_COND_HASH_SIZE 101
#define CCL_HASH_SIZE 101

static struct hash_entry *ndtbl[NAME_TABLE_HASH_SIZE];
static struct hash_entry *sctbl[START_COND_HASH_SIZE];
static struct hash_entry *ccltab[CCL_HASH_SIZE];


/* declare functions that have forward references */

static int addsym PROTO ((char[], char *, int, hash_table, int));
static struct hash_entry *findsym PROTO ((const char *sym,
					  hash_table table,

					  int table_size));
static int hashfunct PROTO ((const char *, int));


/* addsym - add symbol and definitions to symbol table
 *
 * -1 is returned if the symbol already exists, and the change not made.
 */

static int addsym (sym, str_def, int_def, table, table_size)
     char sym[];
     char   *str_def;
     int     int_def;
     hash_table table;
     int     table_size;
{
	int     hash_val = hashfunct (sym, table_size);
	struct hash_entry *sym_entry = table[hash_val];
	struct hash_entry *new_entry;
	struct hash_entry *successor;

	while (sym_entry) {
		if (!strcmp (sym, sym_entry->name)) {	/* entry already exists */
			return -1;
		}

		sym_entry = sym_entry->next;
	}

	/* create new entry */
	new_entry = (struct hash_entry *)
		flex_alloc (sizeof (struct hash_entry));

	if (new_entry == NULL)
		flexfatal (_("symbol table memory allocation failed"));

	if ((successor = table[hash_val]) != 0) {
		new_entry->next = successor;
		successor->prev = new_entry;
	}
	else
		new_entry->next = NULL;

	new_entry->prev = NULL;
	new_entry->name = sym;
	new_entry->str_val = str_def;
	new_entry->int_val = int_def;

	table[hash_val] = new_entry;

	return 0;
}


/* cclinstal - save the text of a character class */

void    cclinstal (ccltxt, cclnum)
     Char    ccltxt[];
     int     cclnum;
{
	/* We don't bother checking the return status because we are not
	 * called unless the symbol is new.
	 */

	(void) addsym ((char *) copy_unsigned_string (ccltxt),
		       (char *) 0, cclnum, ccltab, CCL_HASH_SIZE);
}


/* ccllookup - lookup the number associated with character class text
 *
 * Returns 0 if there's no CCL associated with the text.
 */

int     ccllookup (ccltxt)
     Char    ccltxt[];
{
	return findsym ((char *) ccltxt, ccltab, CCL_HASH_SIZE)->int_val;
}


/* findsym - find symbol in symbol table */

static struct hash_entry *findsym (sym, table, table_size)
     const char *sym;
     hash_table table;
     int     table_size;
{
	static struct hash_entry empty_entry = {
		(struct hash_entry *) 0, (struct hash_entry *) 0,
		(char *) 0, (char *) 0, 0,
	};
	struct hash_entry *sym_entry =

		table[hashfunct (sym, table_size)];

	while (sym_entry) {
		if (!strcmp (sym, sym_entry->name))
			return sym_entry;
		sym_entry = sym_entry->next;
	}

	return &empty_entry;
}

/* hashfunct - compute the hash value for "str" and hash size "hash_size" */

static int hashfunct (str, hash_size)
     const char *str;
     int     hash_size;
{
	int hashval;
	int locstr;

	hashval = 0;
	locstr = 0;

	while (str[locstr]) {
		hashval = (hashval << 1) + (unsigned char) str[locstr++];
		hashval %= hash_size;
	}

	return hashval;
}


/* ndinstal - install a name definition */

void    ndinstal (name, definition)
     const char *name;
     Char    definition[];
{

	if (addsym (copy_string (name),
		    (char *) copy_unsigned_string (definition), 0,
		    ndtbl, NAME_TABLE_HASH_SIZE))
			synerr (_("name defined twice"));
}


/* ndlookup - lookup a name definition
 *
 * Returns a nil pointer if the name definition does not exist.
 */

Char   *ndlookup (nd)
     const char *nd;
{
	return (Char *) findsym (nd, ndtbl, NAME_TABLE_HASH_SIZE)->str_val;
}


/* scextend - increase the maximum number of start conditions */

void    scextend ()
{
	current_max_scs += MAX_SCS_INCREMENT;

	++num_reallocs;

	scset = reallocate_integer_array (scset, current_max_scs);
	scbol = reallocate_integer_array (scbol, current_max_scs);
	scxclu = reallocate_integer_array (scxclu, current_max_scs);
	sceof = reallocate_integer_array (sceof, current_max_scs);
	scname = reallocate_char_ptr_array (scname, current_max_scs);
}


/* scinstal - make a start condition
 *
 * NOTE
 *    The start condition is "exclusive" if xcluflg is true.
 */

void    scinstal (str, xcluflg)
     const char *str;
     int     xcluflg;
{

	if (++lastsc >= current_max_scs)
		scextend ();

	scname[lastsc] = copy_string (str);

	if (addsym (scname[lastsc], (char *) 0, lastsc,
		    sctbl, START_COND_HASH_SIZE))
			format_pinpoint_message (_
						 ("start condition %s declared twice"),
str);

	scset[lastsc] = mkstate (SYM_EPSILON);
	scbol[lastsc] = mkstate (SYM_EPSILON);
	scxclu[lastsc] = xcluflg;
	sceof[lastsc] = false;
}


/* sclookup - lookup the number associated with a start condition
 *
 * Returns 0 if no such start condition.
 */

int     sclookup (str)
     const char *str;
{
	return findsym (str, sctbl, START_COND_HASH_SIZE)->int_val;
}
