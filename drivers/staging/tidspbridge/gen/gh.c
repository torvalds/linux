/*
 * gh.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/types.h>

#include <dspbridge/host_os.h>

#include <dspbridge/gs.h>

#include <dspbridge/gh.h>

struct element {
	struct element *next;
	u8 data[1];
};

struct gh_t_hash_tab {
	u16 max_bucket;
	u16 val_size;
	struct element **buckets;
	 u16(*hash) (void *, u16);
	 bool(*match) (void *, void *);
	void (*delete) (void *);
};

static void noop(void *p);
static s32 cur_init;
static void myfree(void *ptr, s32 size);

/*
 *  ======== gh_create ========
 */

struct gh_t_hash_tab *gh_create(u16 max_bucket, u16 val_size,
				u16(*hash) (void *, u16), bool(*match) (void *,
									void *),
				void (*delete) (void *))
{
	struct gh_t_hash_tab *hash_tab;
	u16 i;
	hash_tab =
	    (struct gh_t_hash_tab *)gs_alloc(sizeof(struct gh_t_hash_tab));
	if (hash_tab == NULL)
		return NULL;
	hash_tab->max_bucket = max_bucket;
	hash_tab->val_size = val_size;
	hash_tab->hash = hash;
	hash_tab->match = match;
	hash_tab->delete = delete == NULL ? noop : delete;

	hash_tab->buckets = (struct element **)
	    gs_alloc(sizeof(struct element *) * max_bucket);
	if (hash_tab->buckets == NULL) {
		gh_delete(hash_tab);
		return NULL;
	}

	for (i = 0; i < max_bucket; i++)
		hash_tab->buckets[i] = NULL;

	return hash_tab;
}

/*
 *  ======== gh_delete ========
 */
void gh_delete(struct gh_t_hash_tab *hash_tab)
{
	struct element *elem, *next;
	u16 i;

	if (hash_tab != NULL) {
		if (hash_tab->buckets != NULL) {
			for (i = 0; i < hash_tab->max_bucket; i++) {
				for (elem = hash_tab->buckets[i]; elem != NULL;
				     elem = next) {
					next = elem->next;
					(*hash_tab->delete) (elem->data);
					myfree(elem,
					       sizeof(struct element) - 1 +
					       hash_tab->val_size);
				}
			}

			myfree(hash_tab->buckets, sizeof(struct element *)
			       * hash_tab->max_bucket);
		}

		myfree(hash_tab, sizeof(struct gh_t_hash_tab));
	}
}

/*
 *  ======== gh_exit ========
 */

void gh_exit(void)
{
	if (cur_init-- == 1)
		gs_exit();

}

/*
 *  ======== gh_find ========
 */

void *gh_find(struct gh_t_hash_tab *hash_tab, void *key)
{
	struct element *elem;

	elem = hash_tab->buckets[(*hash_tab->hash) (key, hash_tab->max_bucket)];

	for (; elem; elem = elem->next) {
		if ((*hash_tab->match) (key, elem->data))
			return elem->data;
	}

	return NULL;
}

/*
 *  ======== gh_init ========
 */

void gh_init(void)
{
	if (cur_init++ == 0)
		gs_init();
}

/*
 *  ======== gh_insert ========
 */

void *gh_insert(struct gh_t_hash_tab *hash_tab, void *key, void *value)
{
	struct element *elem;
	u16 i;
	char *src, *dst;

	elem = (struct element *)gs_alloc(sizeof(struct element) - 1 +
					  hash_tab->val_size);
	if (elem != NULL) {

		dst = (char *)elem->data;
		src = (char *)value;
		for (i = 0; i < hash_tab->val_size; i++)
			*dst++ = *src++;

		i = (*hash_tab->hash) (key, hash_tab->max_bucket);
		elem->next = hash_tab->buckets[i];
		hash_tab->buckets[i] = elem;

		return elem->data;
	}

	return NULL;
}

/*
 *  ======== noop ========
 */
/* ARGSUSED */
static void noop(void *p)
{
	p = p;			/* stifle compiler warning */
}

/*
 *  ======== myfree ========
 */
static void myfree(void *ptr, s32 size)
{
	gs_free(ptr);
}

#ifdef CONFIG_TIDSPBRIDGE_BACKTRACE
/**
 * gh_iterate() - This function goes through all the elements in the hash table
 *		looking for the dsp symbols.
 * @hash_tab:	Hash table
 * @callback:	pointer to callback function
 * @user_data:	User data, contains the find_symbol_context pointer
 *
 */
void gh_iterate(struct gh_t_hash_tab *hash_tab,
		void (*callback)(void *, void *), void *user_data)
{
	struct element *elem;
	u32 i;

	if (hash_tab && hash_tab->buckets)
		for (i = 0; i < hash_tab->max_bucket; i++) {
			elem = hash_tab->buckets[i];
			while (elem) {
				callback(&elem->data, user_data);
				elem = elem->next;
			}
		}
}
#endif
