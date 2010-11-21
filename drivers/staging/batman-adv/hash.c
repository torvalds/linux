/*
 * Copyright (C) 2006-2010 B.A.T.M.A.N. contributors:
 *
 * Simon Wunderlich, Marek Lindner
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#include "main.h"
#include "hash.h"

/* clears the hash */
static void hash_init(struct hashtable_t *hash)
{
	int i;

	hash->elements = 0;

	for (i = 0 ; i < hash->size; i++)
		hash->table[i] = NULL;
}

/* free only the hashtable and the hash itself. */
void hash_destroy(struct hashtable_t *hash)
{
	kfree(hash->table);
	kfree(hash);
}

/* allocates and clears the hash */
struct hashtable_t *hash_new(int size)
{
	struct hashtable_t *hash;

	hash = kmalloc(sizeof(struct hashtable_t) , GFP_ATOMIC);

	if (hash == NULL)
		return NULL;

	hash->size = size;
	hash->table = kmalloc(sizeof(struct element_t *) * size, GFP_ATOMIC);

	if (hash->table == NULL) {
		kfree(hash);
		return NULL;
	}

	hash_init(hash);

	return hash;
}

/* remove bucket (this might be used in hash_iterate() if you already found the
 * bucket you want to delete and don't need the overhead to find it again with
 * hash_remove(). But usually, you don't want to use this function, as it
 * fiddles with hash-internals. */
void *hash_remove_bucket(struct hashtable_t *hash, struct hash_it_t *hash_it_t)
{
	void *data_save;

	data_save = hash_it_t->bucket->data;

	if (hash_it_t->prev_bucket != NULL)
		hash_it_t->prev_bucket->next = hash_it_t->bucket->next;
	else if (hash_it_t->first_bucket != NULL)
		(*hash_it_t->first_bucket) = hash_it_t->bucket->next;

	kfree(hash_it_t->bucket);
	hash->elements--;

	return data_save;
}
