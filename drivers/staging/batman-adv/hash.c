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

/* iterate though the hash. First element is selected if an iterator
 * initialized with HASHIT() is supplied as iter. Use the returned
 * (or supplied) iterator to access the elements until hash_iterate returns
 * NULL. */

struct hash_it_t *hash_iterate(struct hashtable_t *hash,
			       struct hash_it_t *iter)
{
	if (!hash)
		return NULL;
	if (!iter)
		return NULL;

	/* sanity checks first (if our bucket got deleted in the last
	 * iteration): */
	if (iter->bucket != NULL) {
		if (iter->first_bucket != NULL) {
			/* we're on the first element and it got removed after
			 * the last iteration. */
			if ((*iter->first_bucket) != iter->bucket) {
				/* there are still other elements in the list */
				if ((*iter->first_bucket) != NULL) {
					iter->prev_bucket = NULL;
					iter->bucket = (*iter->first_bucket);
					iter->first_bucket =
						&hash->table[iter->index];
					return iter;
				} else {
					iter->bucket = NULL;
				}
			}
		} else if (iter->prev_bucket != NULL) {
			/*
			* we're not on the first element, and the bucket got
			* removed after the last iteration.  the last bucket's
			* next pointer is not pointing to our actual bucket
			* anymore.  select the next.
			*/
			if (iter->prev_bucket->next != iter->bucket)
				iter->bucket = iter->prev_bucket;
		}
	}

	/* now as we are sane, select the next one if there is some */
	if (iter->bucket != NULL) {
		if (iter->bucket->next != NULL) {
			iter->prev_bucket = iter->bucket;
			iter->bucket = iter->bucket->next;
			iter->first_bucket = NULL;
			return iter;
		}
	}

	/* if not returned yet, we've reached the last one on the index and have
	 * to search forward */
	iter->index++;
	/* go through the entries of the hash table */
	while (iter->index < hash->size) {
		if ((hash->table[iter->index]) != NULL) {
			iter->prev_bucket = NULL;
			iter->bucket = hash->table[iter->index];
			iter->first_bucket = &hash->table[iter->index];
			return iter;
		} else {
			iter->index++;
		}
	}

	/* nothing to iterate over anymore */
	return NULL;
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
