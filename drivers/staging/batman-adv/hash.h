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
	hashdata_choose_cb choose;  /* the hashfunction, should return an index
				     * based on the key in the data of the first
				     * argument and the size the second */
};

/* allocates and clears the hash */
struct hashtable_t *hash_new(int size, hashdata_choose_cb choose);

/* remove bucket (this might be used in hash_iterate() if you already found the
 * bucket you want to delete and don't need the overhead to find it again with
 * hash_remove().  But usually, you don't want to use this function, as it
 * fiddles with hash-internals. */
void *hash_remove_bucket(struct hashtable_t *hash, struct hash_it_t *hash_it_t);

/* remove the hash structure. if hashdata_free_cb != NULL, this function will be
 * called to remove the elements inside of the hash.  if you don't remove the
 * elements, memory might be leaked. */
void hash_delete(struct hashtable_t *hash, hashdata_free_cb free_cb, void *arg);

/* free only the hashtable and the hash itself. */
void hash_destroy(struct hashtable_t *hash);

/* adds data to the hashtable. returns 0 on success, -1 on error */
int hash_add(struct hashtable_t *hash, hashdata_compare_cb compare, void *data);

/* removes data from hash, if found. returns pointer do data on success, so you
 * can remove the used structure yourself, or NULL on error .  data could be the
 * structure you use with just the key filled, we just need the key for
 * comparing. */
void *hash_remove(struct hashtable_t *hash, hashdata_compare_cb compare,
		  void *data);

/* finds data, based on the key in keydata. returns the found data on success,
 * or NULL on error */
void *hash_find(struct hashtable_t *hash, hashdata_compare_cb compare,
		void *keydata);

/* resize the hash, returns the pointer to the new hash or NULL on
 * error. removes the old hash on success */
struct hashtable_t *hash_resize(struct hashtable_t *hash,
				hashdata_compare_cb compare, int size);

/* iterate though the hash. first element is selected with iter_in NULL.  use
 * the returned iterator to access the elements until hash_it_t returns NULL. */
struct hash_it_t *hash_iterate(struct hashtable_t *hash,
			       struct hash_it_t *iter_in);

#endif /* _NET_BATMAN_ADV_HASH_H_ */
