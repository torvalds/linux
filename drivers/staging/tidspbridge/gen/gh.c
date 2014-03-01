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

#include <linux/err.h>
#include <linux/hashtable.h>
#include <linux/slab.h>

struct gh_node {
	struct hlist_node hl;
	u8 data[0];
};

#define GH_HASH_ORDER 8

struct gh_t_hash_tab {
	u32 val_size;
	DECLARE_HASHTABLE(hash_table, GH_HASH_ORDER);
	u32 (*hash)(const void *key);
	bool (*match)(const void *key, const void *value);
	void (*delete)(void *key);
};

/*
 *  ======== gh_create ========
 */

struct gh_t_hash_tab *gh_create(u32 val_size, u32 (*hash)(const void *),
				bool (*match)(const void *, const void *),
				void (*delete)(void *))
{
	struct gh_t_hash_tab *hash_tab;

	hash_tab = kzalloc(sizeof(struct gh_t_hash_tab), GFP_KERNEL);
	if (!hash_tab)
		return ERR_PTR(-ENOMEM);

	hash_init(hash_tab->hash_table);

	hash_tab->val_size = val_size;
	hash_tab->hash = hash;
	hash_tab->match = match;
	hash_tab->delete = delete;

	return hash_tab;
}

/*
 *  ======== gh_delete ========
 */
void gh_delete(struct gh_t_hash_tab *hash_tab)
{
	struct gh_node *n;
	struct hlist_node *tmp;
	u32 i;

	if (hash_tab) {
		hash_for_each_safe(hash_tab->hash_table, i, tmp, n, hl) {
			hash_del(&n->hl);
			if (hash_tab->delete)
				hash_tab->delete(n->data);
			kfree(n);
		}

		kfree(hash_tab);
	}
}

/*
 *  ======== gh_find ========
 */

void *gh_find(struct gh_t_hash_tab *hash_tab, const void *key)
{
	struct gh_node *n;
	u32 key_hash = hash_tab->hash(key);

	hash_for_each_possible(hash_tab->hash_table, n, hl, key_hash) {
		if (hash_tab->match(key, n->data))
			return n->data;
	}

	return ERR_PTR(-ENODATA);
}

/*
 *  ======== gh_insert ========
 */

void *gh_insert(struct gh_t_hash_tab *hash_tab, const void *key,
		const void *value)
{
	struct gh_node *n;

	n = kmalloc(sizeof(struct gh_node) + hash_tab->val_size,
			GFP_KERNEL);

	if (!n)
		return ERR_PTR(-ENOMEM);

	INIT_HLIST_NODE(&n->hl);
	hash_add(hash_tab->hash_table, &n->hl, hash_tab->hash(key));
	memcpy(n->data, value, hash_tab->val_size);

	return n->data;
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
	struct gh_node *n;
	u32 i;

	if (!hash_tab)
		return;

	hash_for_each(hash_tab->hash_table, i, n, hl)
		callback(&n->data, user_data);
}
#endif
