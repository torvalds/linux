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

#define HASHIT(name) struct hash_it_t name = { \
		.index = -1, .bucket = NULL, \
		.prev_bucket = NULL, \
		.first_bucket = NULL }

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
	struct element_t *next;	/* overflow bucket pointer */
};

struct hash_it_t {
	int index;
	struct element_t *bucket;
	struct element_t *prev_bucket;
	struct element_t **first_bucket;
};

struct hashtable_t {
	struct element_t **table;   /* the hashtable itself, with the buckets */
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
	struct element_t *bucket, *last_bucket;
	int i;

	for (i = 0; i < hash->size; i++) {
		bucket = hash->table[i];

		while (bucket != NULL) {
			if (free_cb != NULL)
				free_cb(bucket->data, arg);

			last_bucket = bucket;
			bucket = bucket->next;
			kfree(last_bucket);
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
	struct element_t *bucket, *prev_bucket = NULL;

	if (!hash)
		return -1;

	index = choose(data, hash->size);
	bucket = hash->table[index];

	while (bucket != NULL) {
		if (compare(bucket->data, data))
			return -1;

		prev_bucket = bucket;
		bucket = bucket->next;
	}

	/* found the tail of the list, add new element */
	bucket = kmalloc(sizeof(struct element_t), GFP_ATOMIC);

	if (bucket == NULL)
		return -1;

	bucket->data = data;
	bucket->next = NULL;

	/* and link it */
	if (prev_bucket == NULL)
		hash->table[index] = bucket;
	else
		prev_bucket->next = bucket;

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

	hash_it_t.index = choose(data, hash->size);
	hash_it_t.bucket = hash->table[hash_it_t.index];
	hash_it_t.prev_bucket = NULL;

	while (hash_it_t.bucket != NULL) {
		if (compare(hash_it_t.bucket->data, data)) {
			hash_it_t.first_bucket =
				(hash_it_t.bucket ==
				 hash->table[hash_it_t.index] ?
				 &hash->table[hash_it_t.index] : NULL);
			return hash_remove_bucket(hash, &hash_it_t);
		}

		hash_it_t.prev_bucket = hash_it_t.bucket;
		hash_it_t.bucket = hash_it_t.bucket->next;
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
	struct element_t *bucket;

	if (!hash)
		return NULL;

	index = choose(keydata , hash->size);
	bucket = hash->table[index];

	while (bucket != NULL) {
		if (compare(bucket->data, keydata))
			return bucket->data;

		bucket = bucket->next;
	}

	return NULL;
}

/* resize the hash, returns the pointer to the new hash or NULL on
 * error. removes the old hash on success */
static inline struct hashtable_t *hash_resize(struct hashtable_t *hash,
					      hashdata_compare_cb compare,
					      hashdata_choose_cb choose,
					      int size)
{
	struct hashtable_t *new_hash;
	struct element_t *bucket;
	int i;

	/* initialize a new hash with the new size */
	new_hash = hash_new(size);

	if (new_hash == NULL)
		return NULL;

	/* copy the elements */
	for (i = 0; i < hash->size; i++) {
		bucket = hash->table[i];

		while (bucket != NULL) {
			hash_add(new_hash, compare, choose, bucket->data);
			bucket = bucket->next;
		}
	}

	/* remove hash and eventual overflow buckets but not the content
	 * itself. */
	hash_delete(hash, NULL, NULL);

	return new_hash;
}

/* iterate though the hash. first element is selected with iter_in NULL.  use
 * the returned iterator to access the elements until hash_it_t returns NULL. */
struct hash_it_t *hash_iterate(struct hashtable_t *hash,
			       struct hash_it_t *iter_in);

#endif /* _NET_BATMAN_ADV_HASH_H_ */
