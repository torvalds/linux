/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/lov/lov_pool.c
 *
 * OST pool methods
 *
 * Author: Jacques-Charles LAFOUCRIERE <jc.lafoucriere@cea.fr>
 * Author: Alex Lyashkov <Alexey.Lyashkov@Sun.COM>
 * Author: Nathaniel Rutman <Nathan.Rutman@Sun.COM>
 */

#define DEBUG_SUBSYSTEM S_LOV

#include <linux/libcfs/libcfs.h>

#include <obd.h>
#include "lov_internal.h"

#define pool_tgt(_p, _i) \
		_p->pool_lobd->u.lov.lov_tgts[_p->pool_obds.op_array[_i]]

static void lov_pool_getref(struct pool_desc *pool)
{
	CDEBUG(D_INFO, "pool %p\n", pool);
	atomic_inc(&pool->pool_refcount);
}

void lov_pool_putref(struct pool_desc *pool)
{
	CDEBUG(D_INFO, "pool %p\n", pool);
	if (atomic_dec_and_test(&pool->pool_refcount)) {
		LASSERT(hlist_unhashed(&pool->pool_hash));
		LASSERT(list_empty(&pool->pool_list));
		LASSERT(!pool->pool_debugfs_entry);
		lov_ost_pool_free(&pool->pool_obds);
		kfree(pool);
	}
}

static void lov_pool_putref_locked(struct pool_desc *pool)
{
	CDEBUG(D_INFO, "pool %p\n", pool);
	LASSERT(atomic_read(&pool->pool_refcount) > 1);

	atomic_dec(&pool->pool_refcount);
}

/*
 * hash function using a Rotating Hash algorithm
 * Knuth, D. The Art of Computer Programming,
 * Volume 3: Sorting and Searching,
 * Chapter 6.4.
 * Addison Wesley, 1973
 */
static __u32 pool_hashfn(struct cfs_hash *hash_body, const void *key,
			 unsigned int mask)
{
	int i;
	__u32 result;
	char *poolname;

	result = 0;
	poolname = (char *)key;
	for (i = 0; i < LOV_MAXPOOLNAME; i++) {
		if (poolname[i] == '\0')
			break;
		result = (result << 4) ^ (result >> 28) ^  poolname[i];
	}
	return (result % mask);
}

static void *pool_key(struct hlist_node *hnode)
{
	struct pool_desc *pool;

	pool = hlist_entry(hnode, struct pool_desc, pool_hash);
	return pool->pool_name;
}

static int pool_hashkey_keycmp(const void *key, struct hlist_node *compared_hnode)
{
	char *pool_name;
	struct pool_desc *pool;

	pool_name = (char *)key;
	pool = hlist_entry(compared_hnode, struct pool_desc, pool_hash);
	return !strncmp(pool_name, pool->pool_name, LOV_MAXPOOLNAME);
}

static void *pool_hashobject(struct hlist_node *hnode)
{
	return hlist_entry(hnode, struct pool_desc, pool_hash);
}

static void pool_hashrefcount_get(struct cfs_hash *hs, struct hlist_node *hnode)
{
	struct pool_desc *pool;

	pool = hlist_entry(hnode, struct pool_desc, pool_hash);
	lov_pool_getref(pool);
}

static void pool_hashrefcount_put_locked(struct cfs_hash *hs,
					 struct hlist_node *hnode)
{
	struct pool_desc *pool;

	pool = hlist_entry(hnode, struct pool_desc, pool_hash);
	lov_pool_putref_locked(pool);
}

struct cfs_hash_ops pool_hash_operations = {
	.hs_hash	= pool_hashfn,
	.hs_key		= pool_key,
	.hs_keycmp      = pool_hashkey_keycmp,
	.hs_object      = pool_hashobject,
	.hs_get		= pool_hashrefcount_get,
	.hs_put_locked  = pool_hashrefcount_put_locked,

};

/*
 * pool debugfs seq_file methods
 */
/*
 * iterator is used to go through the target pool entries
 * index is the current entry index in the lp_array[] array
 * index >= pos returned to the seq_file interface
 * pos is from 0 to (pool->pool_obds.op_count - 1)
 */
#define POOL_IT_MAGIC 0xB001CEA0
struct pool_iterator {
	int magic;
	struct pool_desc *pool;
	int idx;	/* from 0 to pool_tgt_size - 1 */
};

static void *pool_proc_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct pool_iterator *iter = (struct pool_iterator *)s->private;
	int prev_idx;

	LASSERTF(iter->magic == POOL_IT_MAGIC, "%08X\n", iter->magic);

	/* test if end of file */
	if (*pos >= pool_tgt_count(iter->pool))
		return NULL;

	/* iterate to find a non empty entry */
	prev_idx = iter->idx;
	down_read(&pool_tgt_rw_sem(iter->pool));
	iter->idx++;
	if (iter->idx == pool_tgt_count(iter->pool)) {
		iter->idx = prev_idx; /* we stay on the last entry */
		up_read(&pool_tgt_rw_sem(iter->pool));
		return NULL;
	}
	up_read(&pool_tgt_rw_sem(iter->pool));
	(*pos)++;
	/* return != NULL to continue */
	return iter;
}

static void *pool_proc_start(struct seq_file *s, loff_t *pos)
{
	struct pool_desc *pool = (struct pool_desc *)s->private;
	struct pool_iterator *iter;

	lov_pool_getref(pool);
	if ((pool_tgt_count(pool) == 0) ||
	    (*pos >= pool_tgt_count(pool))) {
		/* iter is not created, so stop() has no way to
		 * find pool to dec ref
		 */
		lov_pool_putref(pool);
		return NULL;
	}

	iter = kzalloc(sizeof(*iter), GFP_NOFS);
	if (!iter)
		return ERR_PTR(-ENOMEM);
	iter->magic = POOL_IT_MAGIC;
	iter->pool = pool;
	iter->idx = 0;

	/* we use seq_file private field to memorized iterator so
	 * we can free it at stop()
	 */
	/* /!\ do not forget to restore it to pool before freeing it */
	s->private = iter;
	if (*pos > 0) {
		loff_t i;
		void *ptr;

		i = 0;
		do {
			ptr = pool_proc_next(s, &iter, &i);
		} while ((i < *pos) && ptr);
		return ptr;
	}
	return iter;
}

static void pool_proc_stop(struct seq_file *s, void *v)
{
	struct pool_iterator *iter = (struct pool_iterator *)s->private;

	/* in some cases stop() method is called 2 times, without
	 * calling start() method (see seq_read() from fs/seq_file.c)
	 * we have to free only if s->private is an iterator
	 */
	if ((iter) && (iter->magic == POOL_IT_MAGIC)) {
		/* we restore s->private so next call to pool_proc_start()
		 * will work
		 */
		s->private = iter->pool;
		lov_pool_putref(iter->pool);
		kfree(iter);
	}
}

static int pool_proc_show(struct seq_file *s, void *v)
{
	struct pool_iterator *iter = (struct pool_iterator *)v;
	struct lov_tgt_desc *tgt;

	LASSERTF(iter->magic == POOL_IT_MAGIC, "%08X\n", iter->magic);
	LASSERT(iter->pool);
	LASSERT(iter->idx <= pool_tgt_count(iter->pool));

	down_read(&pool_tgt_rw_sem(iter->pool));
	tgt = pool_tgt(iter->pool, iter->idx);
	up_read(&pool_tgt_rw_sem(iter->pool));
	if (tgt)
		seq_printf(s, "%s\n", obd_uuid2str(&tgt->ltd_uuid));

	return 0;
}

static const struct seq_operations pool_proc_ops = {
	.start	  = pool_proc_start,
	.next	   = pool_proc_next,
	.stop	   = pool_proc_stop,
	.show	   = pool_proc_show,
};

static int pool_proc_open(struct inode *inode, struct file *file)
{
	int rc;

	rc = seq_open(file, &pool_proc_ops);
	if (!rc) {
		struct seq_file *s = file->private_data;

		s->private = inode->i_private;
	}
	return rc;
}

static const struct file_operations pool_proc_operations = {
	.open	   = pool_proc_open,
	.read	   = seq_read,
	.llseek	 = seq_lseek,
	.release	= seq_release,
};

#define LOV_POOL_INIT_COUNT 2
int lov_ost_pool_init(struct ost_pool *op, unsigned int count)
{
	if (count == 0)
		count = LOV_POOL_INIT_COUNT;
	op->op_array = NULL;
	op->op_count = 0;
	init_rwsem(&op->op_rw_sem);
	op->op_size = count;
	op->op_array = kcalloc(op->op_size, sizeof(op->op_array[0]), GFP_NOFS);
	if (!op->op_array) {
		op->op_size = 0;
		return -ENOMEM;
	}
	return 0;
}

/* Caller must hold write op_rwlock */
int lov_ost_pool_extend(struct ost_pool *op, unsigned int min_count)
{
	__u32 *new;
	int new_size;

	LASSERT(min_count != 0);

	if (op->op_count < op->op_size)
		return 0;

	new_size = max(min_count, 2 * op->op_size);
	new = kcalloc(new_size, sizeof(op->op_array[0]), GFP_NOFS);
	if (!new)
		return -ENOMEM;

	/* copy old array to new one */
	memcpy(new, op->op_array, op->op_size * sizeof(op->op_array[0]));
	kfree(op->op_array);
	op->op_array = new;
	op->op_size = new_size;
	return 0;
}

int lov_ost_pool_add(struct ost_pool *op, __u32 idx, unsigned int min_count)
{
	int rc = 0, i;

	down_write(&op->op_rw_sem);

	rc = lov_ost_pool_extend(op, min_count);
	if (rc)
		goto out;

	/* search ost in pool array */
	for (i = 0; i < op->op_count; i++) {
		if (op->op_array[i] == idx) {
			rc = -EEXIST;
			goto out;
		}
	}
	/* ost not found we add it */
	op->op_array[op->op_count] = idx;
	op->op_count++;
out:
	up_write(&op->op_rw_sem);
	return rc;
}

int lov_ost_pool_remove(struct ost_pool *op, __u32 idx)
{
	int i;

	down_write(&op->op_rw_sem);

	for (i = 0; i < op->op_count; i++) {
		if (op->op_array[i] == idx) {
			memmove(&op->op_array[i], &op->op_array[i + 1],
				(op->op_count - i - 1) * sizeof(op->op_array[0]));
			op->op_count--;
			up_write(&op->op_rw_sem);
			return 0;
		}
	}

	up_write(&op->op_rw_sem);
	return -EINVAL;
}

int lov_ost_pool_free(struct ost_pool *op)
{
	if (op->op_size == 0)
		return 0;

	down_write(&op->op_rw_sem);

	kfree(op->op_array);
	op->op_array = NULL;
	op->op_count = 0;
	op->op_size = 0;

	up_write(&op->op_rw_sem);
	return 0;
}

int lov_pool_new(struct obd_device *obd, char *poolname)
{
	struct lov_obd *lov;
	struct pool_desc *new_pool;
	int rc;

	lov = &obd->u.lov;

	if (strlen(poolname) > LOV_MAXPOOLNAME)
		return -ENAMETOOLONG;

	new_pool = kzalloc(sizeof(*new_pool), GFP_NOFS);
	if (!new_pool)
		return -ENOMEM;

	strlcpy(new_pool->pool_name, poolname, sizeof(new_pool->pool_name));
	new_pool->pool_lobd = obd;
	/* ref count init to 1 because when created a pool is always used
	 * up to deletion
	 */
	atomic_set(&new_pool->pool_refcount, 1);
	rc = lov_ost_pool_init(&new_pool->pool_obds, 0);
	if (rc)
		goto out_err;

	INIT_HLIST_NODE(&new_pool->pool_hash);

	/* get ref for debugfs file */
	lov_pool_getref(new_pool);
	new_pool->pool_debugfs_entry = ldebugfs_add_simple(
						lov->lov_pool_debugfs_entry,
						poolname, new_pool,
						&pool_proc_operations);
	if (IS_ERR_OR_NULL(new_pool->pool_debugfs_entry)) {
		CWARN("Cannot add debugfs pool entry " LOV_POOLNAMEF "\n",
		      poolname);
		new_pool->pool_debugfs_entry = NULL;
		lov_pool_putref(new_pool);
	}
	CDEBUG(D_INFO, "pool %p - proc %p\n",
	       new_pool, new_pool->pool_debugfs_entry);

	spin_lock(&obd->obd_dev_lock);
	list_add_tail(&new_pool->pool_list, &lov->lov_pool_list);
	lov->lov_pool_count++;
	spin_unlock(&obd->obd_dev_lock);

	/* add to find only when it fully ready  */
	rc = cfs_hash_add_unique(lov->lov_pools_hash_body, poolname,
				 &new_pool->pool_hash);
	if (rc) {
		rc = -EEXIST;
		goto out_err;
	}

	CDEBUG(D_CONFIG, LOV_POOLNAMEF " is pool #%d\n",
	       poolname, lov->lov_pool_count);

	return 0;

out_err:
	spin_lock(&obd->obd_dev_lock);
	list_del_init(&new_pool->pool_list);
	lov->lov_pool_count--;
	spin_unlock(&obd->obd_dev_lock);
	ldebugfs_remove(&new_pool->pool_debugfs_entry);
	lov_ost_pool_free(&new_pool->pool_obds);
	kfree(new_pool);

	return rc;
}

int lov_pool_del(struct obd_device *obd, char *poolname)
{
	struct lov_obd *lov;
	struct pool_desc *pool;

	lov = &obd->u.lov;

	/* lookup and kill hash reference */
	pool = cfs_hash_del_key(lov->lov_pools_hash_body, poolname);
	if (!pool)
		return -ENOENT;

	if (!IS_ERR_OR_NULL(pool->pool_debugfs_entry)) {
		CDEBUG(D_INFO, "proc entry %p\n", pool->pool_debugfs_entry);
		ldebugfs_remove(&pool->pool_debugfs_entry);
		lov_pool_putref(pool);
	}

	spin_lock(&obd->obd_dev_lock);
	list_del_init(&pool->pool_list);
	lov->lov_pool_count--;
	spin_unlock(&obd->obd_dev_lock);

	/* release last reference */
	lov_pool_putref(pool);

	return 0;
}

int lov_pool_add(struct obd_device *obd, char *poolname, char *ostname)
{
	struct obd_uuid ost_uuid;
	struct lov_obd *lov;
	struct pool_desc *pool;
	unsigned int lov_idx;
	int rc;

	lov = &obd->u.lov;

	pool = cfs_hash_lookup(lov->lov_pools_hash_body, poolname);
	if (!pool)
		return -ENOENT;

	obd_str2uuid(&ost_uuid, ostname);

	/* search ost in lov array */
	obd_getref(obd);
	for (lov_idx = 0; lov_idx < lov->desc.ld_tgt_count; lov_idx++) {
		if (!lov->lov_tgts[lov_idx])
			continue;
		if (obd_uuid_equals(&ost_uuid,
				    &lov->lov_tgts[lov_idx]->ltd_uuid))
			break;
	}
	/* test if ost found in lov */
	if (lov_idx == lov->desc.ld_tgt_count) {
		rc = -EINVAL;
		goto out;
	}

	rc = lov_ost_pool_add(&pool->pool_obds, lov_idx, lov->lov_tgt_size);
	if (rc)
		goto out;

	CDEBUG(D_CONFIG, "Added %s to " LOV_POOLNAMEF " as member %d\n",
	       ostname, poolname,  pool_tgt_count(pool));

out:
	obd_putref(obd);
	lov_pool_putref(pool);
	return rc;
}

int lov_pool_remove(struct obd_device *obd, char *poolname, char *ostname)
{
	struct obd_uuid ost_uuid;
	struct lov_obd *lov;
	struct pool_desc *pool;
	unsigned int lov_idx;
	int rc = 0;

	lov = &obd->u.lov;

	pool = cfs_hash_lookup(lov->lov_pools_hash_body, poolname);
	if (!pool)
		return -ENOENT;

	obd_str2uuid(&ost_uuid, ostname);

	obd_getref(obd);
	/* search ost in lov array, to get index */
	for (lov_idx = 0; lov_idx < lov->desc.ld_tgt_count; lov_idx++) {
		if (!lov->lov_tgts[lov_idx])
			continue;

		if (obd_uuid_equals(&ost_uuid,
				    &lov->lov_tgts[lov_idx]->ltd_uuid))
			break;
	}

	/* test if ost found in lov */
	if (lov_idx == lov->desc.ld_tgt_count) {
		rc = -EINVAL;
		goto out;
	}

	lov_ost_pool_remove(&pool->pool_obds, lov_idx);

	CDEBUG(D_CONFIG, "%s removed from " LOV_POOLNAMEF "\n", ostname,
	       poolname);

out:
	obd_putref(obd);
	lov_pool_putref(pool);
	return rc;
}
