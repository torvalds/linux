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

#ifndef _NET_BATMAN_ADV_HASH_H_
#define _NET_BATMAN_ADV_HASH_H_

#include <linux/list.h>

#define HASHIT(name) struct hash_it_t name = { \
		.index = 0, .walk = NULL, \
		.safe = NULL}

/* callback to a compare function.  should
 * compare 2 element datas for their keys,
 * return 0 if same and not 0 if not
 * same */
typedef int (*hashdata_compare_cb)(void *, void *);

/* the hashfunction, should return an index
 * based on the key in the data of the first
 * argument and the size the second */
typedef int (*hashdata_choose_cb)(void *, int);
typedef void (*hashdata_free_cb)(void *, void *);

struct element_t {
	void *data;		/* pointer to the data */
	struct hlist_node hlist;	/* bucket list pointer */
};

struct hash_it_t {
	size_t index;
	struct hlist_node *walk;
	struct hlist_node *safe;
};

struct hashtable_t {
	struct hlist_head *table;   /* the hashtable itself, with the buckets */
	int elements;		    /* number of elements registered */
	int size;		    /* size of hashtable */
};

/* allocates and clears the hash */
struct hashtable_t *hash_new(int size);

/* remove bucket (this might be used in hash_iterate() if you already found the
 * bucket you want to delete and don't need the overhead to find it again with
 * hash_remove().  But usually, you don't want to use this function, as it
 * fiddles with hash-internals. */
void *hash_remove_bucket(struct hashtable_t *hash, struct hash_it_t *hash_it_t);

/* free only the hashtable and the hash itself. */
void hash_destroy(struct hashtable_t *hash);

/* remove the hash structure. if hashdata_free_cb != NULL, this function will be
 * called to remove the elements inside of the hash.  if you don't remove the
 * elements, memory might be leaked. */
static inline void hash_delete(struct hashtable_t *hash,
			       hashdata_free_cb free_cb, void *arg)
{
	struct hlist_head *head;
	struct hlist_node *walk, *safe;
	struct element_t *bucket;
	int i;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		hlist_for_each_safe(walk, safe, head) {
			bucket = hlist_entry(walk, struct element_t, hlist);
			if (free_cb != NULL)
				free_cb(bucket->data, arg);

			hlist_del(walk);
			kfree(bucket);
		}
	}

	hash_destroy(hash);
}

/* adds data to the hashtable. returns 0 on success, -1 on error */
static inline int hash_add(struct hashtable_t *hash,
			   hashdata_compare_cb compare,
			   hashdata_choose_cb choose, void *data)
{
	int index;
	struct hlist_head *head;
	struct hlist_node *walk, *safe;
	struct element_t *bucket;

	if (!hash)
		return -1;

	index = choose(data, hash->size);
	head = &hash->table[index];

	hlist_for_each_safe(walk, safe, head) {
		bucket = hlist_entry(walk, struct element_t, hlist);
		if (compare(bucket->data, data))
			return -1;
	}

	/* no duplicate found in list, add new element */
	bucket = kmalloc(sizeof(struct element_t), GFP_ATOMIC);

	if (bucket == NULL)
		return -1;

	bucket->data = data;
	hlist_add_head(&bucket->hlist, head);

	hash->elements++;
	return 0;
}

/* removes data from hash, if found. returns pointer do data on success, so you
 * can remove the used structure yourself, or NULL on error .  data could be the
 * structure you use with just the key filled, we just need the key for
 * comparing. */
static inline void *hash_remove(struct hashtable_t *hash,
				hashdata_compare_cb compare,
				hashdata_choose_cb choose, void *data)
{
	struct hash_it_t hash_it_t;
	struct element_t *bucket;
	struct hlist_head *head;

	hash_it_t.index = choose(data, hash->size);
	head = &hash->table[hash_it_t.index];

	hlist_for_each(hash_it_t.walk, head) {
		bucket = hlist_entry(hash_it_t.walk, struct element_t, hlist);
		if (compare(bucket->data, data))
			return hash_remove_bucket(hash, &hash_it_t);
	}

	return NULL;
}

/* finds data, based on the key in keydata. returns the found data on success,
 * or NULL on error */
static inline void *hash_find(struct hashtable_t *hash,
			      hashdata_compare_cb compare,
			      hashdata_choose_cb choose, void *keydata)
{
	int index;
	struct hlist_head *head;
	struct hlist_node *walk;
	struct element_t *bucket;

	if (!hash)
		return NULL;

	index = choose(keydata , hash->size);
	head = &hash->table[index];

	hlist_for_each(walk, head) {
		bucket = hlist_entry(walk, struct element_t, hlist);
		if (compare(bucket->data, keydata))
			return bucket->data;
	}

	return NULL;
}

/* resize the hash, returns the pointer to the new hash or NULL on
 * error. removes the old hash on success */
static inline struct hashtable_t *hash_resize(struct hashtable_t *hash,
					      hashdata_choose_cb choose,
					      int size)
{
	struct hashtable_t *new_hash;
	struct hlist_head *head, *new_head;
	struct hlist_node *walk, *safe;
	struct element_t *bucket;
	int i, new_index;

	/* initialize a new hash with the new size */
	new_hash = hash_new(size);

	if (new_hash == NULL)
		return NULL;

	/* copy the elements */
	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		hlist_for_each_safe(walk, safe, head) {
			bucket = hlist_entry(walk, struct element_t, hlist);

			new_index = choose(bucket->data, size);
			new_head = &new_hash->table[new_index];

			hlist_del(walk);
			hlist_add_head(walk, new_head);
		}
	}

	hash_destroy(hash);

	return new_hash;
}

/* iterate though the hash. First element is selected if an iterator
 * initialized with HASHIT() is supplied as iter. Use the returned
 * (or supplied) iterator to access the elements until hash_iterate returns
 * NULL. */
static inline struct hash_it_t *hash_iterate(struct hashtable_t *hash,
					     struct hash_it_t *iter)
{
	if (!hash)
		return NULL;
	if (!iter)
		return NULL;

	iter->walk = iter->safe;

	/* we search for the next head with list entries */
	if (!iter->walk) {
		while (iter->index < hash->size) {
			if (hlist_empty(&hash->table[iter->index]))
				iter->index++;
			else {
				iter->walk = hash->table[iter->index].first;

				/* search next time */
				++iter->index;
				break;
			}
		}
	}

	/* return iter when we found bucket otherwise null */
	if (!iter->walk)
		return NULL;

	iter->safe = iter->walk->next;
	return iter;
}

#endif /* _NET_BATMAN_ADV_HASH_H_ */
