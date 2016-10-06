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
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>

#define DEBUG_SUBSYSTEM S_LLITE

#include "../include/obd_support.h"
#include "../include/lustre_dlm.h"
#include "llite_internal.h"

#define SA_OMITTED_ENTRY_MAX 8ULL

enum se_stat {
	/** negative values are for error cases */
	SA_ENTRY_INIT = 0,      /** init entry */
	SA_ENTRY_SUCC = 1,      /** stat succeed */
	SA_ENTRY_INVA = 2,      /** invalid entry */
};

/*
 * sa_entry is not refcounted: statahead thread allocates it and do async stat,
 * and in async stat callback ll_statahead_interpret() will add it into
 * sai_interim_entries, later statahead thread will call sa_handle_callback() to
 * instantiate entry and move it into sai_entries, and then only scanner process
 * can access and free it.
 */
struct sa_entry {
	/* link into sai_interim_entries or sai_entries */
	struct list_head	      se_list;
	/* link into sai hash table locally */
	struct list_head	      se_hash;
	/* entry index in the sai */
	__u64		   se_index;
	/* low layer ldlm lock handle */
	__u64		   se_handle;
	/* entry status */
	enum se_stat		se_state;
	/* entry size, contains name */
	int		     se_size;
	/* pointer to async getattr enqueue info */
	struct md_enqueue_info *se_minfo;
	/* pointer to the async getattr request */
	struct ptlrpc_request  *se_req;
	/* pointer to the target inode */
	struct inode	   *se_inode;
	/* entry name */
	struct qstr	     se_qstr;
};

static unsigned int sai_generation;
static DEFINE_SPINLOCK(sai_generation_lock);

/* sa_entry is ready to use */
static inline int sa_ready(struct sa_entry *entry)
{
	smp_rmb();
	return (entry->se_state != SA_ENTRY_INIT);
}

/* hash value to put in sai_cache */
static inline int sa_hash(int val)
{
	return val & LL_SA_CACHE_MASK;
}

/* hash entry into sai_cache */
static inline void
sa_rehash(struct ll_statahead_info *sai, struct sa_entry *entry)
{
	int i = sa_hash(entry->se_qstr.hash);

	spin_lock(&sai->sai_cache_lock[i]);
	list_add_tail(&entry->se_hash, &sai->sai_cache[i]);
	spin_unlock(&sai->sai_cache_lock[i]);
}

/*
 * Remove entry from SA table.
 */
static inline void
sa_unhash(struct ll_statahead_info *sai, struct sa_entry *entry)
{
	int i = sa_hash(entry->se_qstr.hash);

	spin_lock(&sai->sai_cache_lock[i]);
	list_del_init(&entry->se_hash);
	spin_unlock(&sai->sai_cache_lock[i]);
}

static inline int agl_should_run(struct ll_statahead_info *sai,
				 struct inode *inode)
{
	return (inode && S_ISREG(inode->i_mode) && sai->sai_agl_valid);
}

/* statahead window is full */
static inline int sa_sent_full(struct ll_statahead_info *sai)
{
	return atomic_read(&sai->sai_cache_count) >= sai->sai_max;
}

/* got async stat replies */
static inline int sa_has_callback(struct ll_statahead_info *sai)
{
	return !list_empty(&sai->sai_interim_entries);
}

static inline int agl_list_empty(struct ll_statahead_info *sai)
{
	return list_empty(&sai->sai_agls);
}

/**
 * (1) hit ratio less than 80%
 * or
 * (2) consecutive miss more than 8
 * then means low hit.
 */
static inline int sa_low_hit(struct ll_statahead_info *sai)
{
	return ((sai->sai_hit > 7 && sai->sai_hit < 4 * sai->sai_miss) ||
		(sai->sai_consecutive_miss > 8));
}

/*
 * if the given index is behind of statahead window more than
 * SA_OMITTED_ENTRY_MAX, then it is old.
 */
static inline int is_omitted_entry(struct ll_statahead_info *sai, __u64 index)
{
	return ((__u64)sai->sai_max + index + SA_OMITTED_ENTRY_MAX <
		 sai->sai_index);
}

/* allocate sa_entry and hash it to allow scanner process to find it */
static struct sa_entry *
sa_alloc(struct dentry *parent, struct ll_statahead_info *sai, __u64 index,
	 const char *name, int len)
{
	struct ll_inode_info *lli;
	struct sa_entry   *entry;
	int		   entry_size;
	char		 *dname;

	entry_size = sizeof(struct sa_entry) + (len & ~3) + 4;
	entry = kzalloc(entry_size, GFP_NOFS);
	if (unlikely(!entry))
		return ERR_PTR(-ENOMEM);

	CDEBUG(D_READA, "alloc sa entry %.*s(%p) index %llu\n",
	       len, name, entry, index);

	entry->se_index = index;
	entry->se_state = SA_ENTRY_INIT;
	entry->se_size = entry_size;
	dname = (char *)entry + sizeof(struct sa_entry);
	memcpy(dname, name, len);
	dname[len] = 0;

	entry->se_qstr.hash = full_name_hash(parent, name, len);
	entry->se_qstr.len = len;
	entry->se_qstr.name = dname;

	lli = ll_i2info(sai->sai_dentry->d_inode);
	spin_lock(&lli->lli_sa_lock);
	INIT_LIST_HEAD(&entry->se_list);
	sa_rehash(sai, entry);
	spin_unlock(&lli->lli_sa_lock);

	atomic_inc(&sai->sai_cache_count);

	return entry;
}

/* free sa_entry, which should have been unhashed and not in any list */
static void sa_free(struct ll_statahead_info *sai, struct sa_entry *entry)
{
	CDEBUG(D_READA, "free sa entry %.*s(%p) index %llu\n",
	       entry->se_qstr.len, entry->se_qstr.name, entry,
	       entry->se_index);

	LASSERT(list_empty(&entry->se_list));
	LASSERT(list_empty(&entry->se_hash));

	kfree(entry);
	atomic_dec(&sai->sai_cache_count);
}

/*
 * find sa_entry by name, used by directory scanner, lock is not needed because
 * only scanner can remove the entry from cache.
 */
static struct sa_entry *
sa_get(struct ll_statahead_info *sai, const struct qstr *qstr)
{
	struct sa_entry *entry;
	int i = sa_hash(qstr->hash);

	list_for_each_entry(entry, &sai->sai_cache[i], se_hash) {
		if (entry->se_qstr.hash == qstr->hash &&
		    entry->se_qstr.len == qstr->len &&
		    memcmp(entry->se_qstr.name, qstr->name, qstr->len) == 0)
			return entry;
	}
	return NULL;
}

/* unhash and unlink sa_entry, and then free it */
static inline void
sa_kill(struct ll_statahead_info *sai, struct sa_entry *entry)
{
	struct ll_inode_info *lli = ll_i2info(sai->sai_dentry->d_inode);

	LASSERT(!list_empty(&entry->se_hash));
	LASSERT(!list_empty(&entry->se_list));
	LASSERT(sa_ready(entry));

	sa_unhash(sai, entry);

	spin_lock(&lli->lli_sa_lock);
	list_del_init(&entry->se_list);
	spin_unlock(&lli->lli_sa_lock);

	if (entry->se_inode)
		iput(entry->se_inode);

	sa_free(sai, entry);
}

/* called by scanner after use, sa_entry will be killed */
static void
sa_put(struct ll_statahead_info *sai, struct sa_entry *entry)
{
	struct sa_entry *tmp, *next;

	if (entry && entry->se_state == SA_ENTRY_SUCC) {
		struct ll_sb_info *sbi = ll_i2sbi(sai->sai_dentry->d_inode);

		sai->sai_hit++;
		sai->sai_consecutive_miss = 0;
		sai->sai_max = min(2 * sai->sai_max, sbi->ll_sa_max);
	} else {
		sai->sai_miss++;
		sai->sai_consecutive_miss++;
	}

	if (entry)
		sa_kill(sai, entry);

	/*
	 * kill old completed entries, only scanner process does this, no need
	 * to lock
	 */
	list_for_each_entry_safe(tmp, next, &sai->sai_entries, se_list) {
		if (!is_omitted_entry(sai, tmp->se_index))
			break;
		sa_kill(sai, tmp);
	}

	wake_up(&sai->sai_thread.t_ctl_waitq);
}

/*
 * update state and sort add entry to sai_entries by index, return true if
 * scanner is waiting on this entry.
 */
static bool
__sa_make_ready(struct ll_statahead_info *sai, struct sa_entry *entry, int ret)
{
	struct list_head *pos = &sai->sai_entries;
	__u64 index = entry->se_index;
	struct sa_entry *se;

	LASSERT(!sa_ready(entry));
	LASSERT(list_empty(&entry->se_list));

	list_for_each_entry_reverse(se, &sai->sai_entries, se_list) {
		if (se->se_index < entry->se_index) {
			pos = &se->se_list;
			break;
		}
	}
	list_add(&entry->se_list, pos);
	entry->se_state = ret < 0 ? SA_ENTRY_INVA : SA_ENTRY_SUCC;

	return (index == sai->sai_index_wait);
}

/*
 * release resources used in async stat RPC, update entry state and wakeup if
 * scanner process it waiting on this entry.
 */
static void
sa_make_ready(struct ll_statahead_info *sai, struct sa_entry *entry, int ret)
{
	struct ll_inode_info *lli = ll_i2info(sai->sai_dentry->d_inode);
	struct md_enqueue_info *minfo = entry->se_minfo;
	struct ptlrpc_request *req = entry->se_req;
	bool wakeup;

	/* release resources used in RPC */
	if (minfo) {
		entry->se_minfo = NULL;
		ll_intent_release(&minfo->mi_it);
		iput(minfo->mi_dir);
		kfree(minfo);
	}

	if (req) {
		entry->se_req = NULL;
		ptlrpc_req_finished(req);
	}

	spin_lock(&lli->lli_sa_lock);
	wakeup = __sa_make_ready(sai, entry, ret);
	spin_unlock(&lli->lli_sa_lock);

	if (wakeup)
		wake_up(&sai->sai_waitq);
}

/* Insert inode into the list of sai_agls. */
static void ll_agl_add(struct ll_statahead_info *sai,
		       struct inode *inode, int index)
{
	struct ll_inode_info *child  = ll_i2info(inode);
	struct ll_inode_info *parent = ll_i2info(sai->sai_dentry->d_inode);
	int		   added  = 0;

	spin_lock(&child->lli_agl_lock);
	if (child->lli_agl_index == 0) {
		child->lli_agl_index = index;
		spin_unlock(&child->lli_agl_lock);

		LASSERT(list_empty(&child->lli_agl_list));

		igrab(inode);
		spin_lock(&parent->lli_agl_lock);
		if (list_empty(&sai->sai_agls))
			added = 1;
		list_add_tail(&child->lli_agl_list, &sai->sai_agls);
		spin_unlock(&parent->lli_agl_lock);
	} else {
		spin_unlock(&child->lli_agl_lock);
	}

	if (added > 0)
		wake_up(&sai->sai_agl_thread.t_ctl_waitq);
}

/* allocate sai */
static struct ll_statahead_info *ll_sai_alloc(struct dentry *dentry)
{
	struct ll_inode_info *lli = ll_i2info(dentry->d_inode);
	struct ll_statahead_info *sai;
	int		       i;

	sai = kzalloc(sizeof(*sai), GFP_NOFS);
	if (!sai)
		return NULL;

	sai->sai_dentry = dget(dentry);
	atomic_set(&sai->sai_refcount, 1);

	sai->sai_max = LL_SA_RPC_MIN;
	sai->sai_index = 1;
	init_waitqueue_head(&sai->sai_waitq);
	init_waitqueue_head(&sai->sai_thread.t_ctl_waitq);
	init_waitqueue_head(&sai->sai_agl_thread.t_ctl_waitq);

	INIT_LIST_HEAD(&sai->sai_interim_entries);
	INIT_LIST_HEAD(&sai->sai_entries);
	INIT_LIST_HEAD(&sai->sai_agls);

	for (i = 0; i < LL_SA_CACHE_SIZE; i++) {
		INIT_LIST_HEAD(&sai->sai_cache[i]);
		spin_lock_init(&sai->sai_cache_lock[i]);
	}
	atomic_set(&sai->sai_cache_count, 0);

	spin_lock(&sai_generation_lock);
	lli->lli_sa_generation = ++sai_generation;
	if (unlikely(!sai_generation))
		lli->lli_sa_generation = ++sai_generation;
	spin_unlock(&sai_generation_lock);

	return sai;
}

/* free sai */
static inline void ll_sai_free(struct ll_statahead_info *sai)
{
	LASSERT(sai->sai_dentry);
	dput(sai->sai_dentry);
	kfree(sai);
}

/*
 * take refcount of sai if sai for @dir exists, which means statahead is on for
 * this directory.
 */
static inline struct ll_statahead_info *ll_sai_get(struct inode *dir)
{
	struct ll_inode_info *lli = ll_i2info(dir);
	struct ll_statahead_info *sai = NULL;

	spin_lock(&lli->lli_sa_lock);
	sai = lli->lli_sai;
	if (sai)
		atomic_inc(&sai->sai_refcount);
	spin_unlock(&lli->lli_sa_lock);

	return sai;
}

/*
 * put sai refcount after use, if refcount reaches zero, free sai and sa_entries
 * attached to it.
 */
static void ll_sai_put(struct ll_statahead_info *sai)
{
	struct ll_inode_info *lli = ll_i2info(sai->sai_dentry->d_inode);

	if (atomic_dec_and_lock(&sai->sai_refcount, &lli->lli_sa_lock)) {
		struct ll_sb_info *sbi = ll_i2sbi(sai->sai_dentry->d_inode);
		struct sa_entry *entry, *next;

		lli->lli_sai = NULL;
		spin_unlock(&lli->lli_sa_lock);

		LASSERT(thread_is_stopped(&sai->sai_thread));
		LASSERT(thread_is_stopped(&sai->sai_agl_thread));
		LASSERT(sai->sai_sent == sai->sai_replied);
		LASSERT(!sa_has_callback(sai));

		list_for_each_entry_safe(entry, next, &sai->sai_entries,
					 se_list)
			sa_kill(sai, entry);

		LASSERT(atomic_read(&sai->sai_cache_count) == 0);
		LASSERT(list_empty(&sai->sai_agls));

		ll_sai_free(sai);
		atomic_dec(&sbi->ll_sa_running);
	}
}

/* Do NOT forget to drop inode refcount when into sai_agls. */
static void ll_agl_trigger(struct inode *inode, struct ll_statahead_info *sai)
{
	struct ll_inode_info *lli   = ll_i2info(inode);
	__u64		 index = lli->lli_agl_index;
	int		   rc;

	LASSERT(list_empty(&lli->lli_agl_list));

	/* AGL maybe fall behind statahead with one entry */
	if (is_omitted_entry(sai, index + 1)) {
		lli->lli_agl_index = 0;
		iput(inode);
		return;
	}

	/* Someone is in glimpse (sync or async), do nothing. */
	rc = down_write_trylock(&lli->lli_glimpse_sem);
	if (rc == 0) {
		lli->lli_agl_index = 0;
		iput(inode);
		return;
	}

	/*
	 * Someone triggered glimpse within 1 sec before.
	 * 1) The former glimpse succeeded with glimpse lock granted by OST, and
	 *    if the lock is still cached on client, AGL needs to do nothing. If
	 *    it is cancelled by other client, AGL maybe cannot obtain new lock
	 *    for no glimpse callback triggered by AGL.
	 * 2) The former glimpse succeeded, but OST did not grant glimpse lock.
	 *    Under such case, it is quite possible that the OST will not grant
	 *    glimpse lock for AGL also.
	 * 3) The former glimpse failed, compared with other two cases, it is
	 *    relative rare. AGL can ignore such case, and it will not muchly
	 *    affect the performance.
	 */
	if (lli->lli_glimpse_time != 0 &&
	    time_before(cfs_time_shift(-1), lli->lli_glimpse_time)) {
		up_write(&lli->lli_glimpse_sem);
		lli->lli_agl_index = 0;
		iput(inode);
		return;
	}

	CDEBUG(D_READA, "Handling (init) async glimpse: inode = "
	       DFID", idx = %llu\n", PFID(&lli->lli_fid), index);

	cl_agl(inode);
	lli->lli_agl_index = 0;
	lli->lli_glimpse_time = cfs_time_current();
	up_write(&lli->lli_glimpse_sem);

	CDEBUG(D_READA, "Handled (init) async glimpse: inode= "
	       DFID", idx = %llu, rc = %d\n",
	       PFID(&lli->lli_fid), index, rc);

	iput(inode);
}

/*
 * prepare inode for sa entry, add it into agl list, now sa_entry is ready
 * to be used by scanner process.
 */
static void sa_instantiate(struct ll_statahead_info *sai,
			   struct sa_entry *entry)
{
	struct inode *dir = sai->sai_dentry->d_inode;
	struct inode	   *child;
	struct md_enqueue_info *minfo;
	struct lookup_intent   *it;
	struct ptlrpc_request  *req;
	struct mdt_body	*body;
	int		     rc    = 0;

	LASSERT(entry->se_handle != 0);

	minfo = entry->se_minfo;
	it = &minfo->mi_it;
	req = entry->se_req;
	body = req_capsule_server_get(&req->rq_pill, &RMF_MDT_BODY);
	if (!body) {
		rc = -EFAULT;
		goto out;
	}

	child = entry->se_inode;
	if (!child) {
		/*
		 * lookup.
		 */
		LASSERT(fid_is_zero(&minfo->mi_data.op_fid2));

		/* XXX: No fid in reply, this is probably cross-ref case.
		 * SA can't handle it yet.
		 */
		if (body->mbo_valid & OBD_MD_MDS) {
			rc = -EAGAIN;
			goto out;
		}
	} else {
		/*
		 * revalidate.
		 */
		/* unlinked and re-created with the same name */
		if (unlikely(!lu_fid_eq(&minfo->mi_data.op_fid2, &body->mbo_fid1))) {
			entry->se_inode = NULL;
			iput(child);
			child = NULL;
		}
	}

	it->it_lock_handle = entry->se_handle;
	rc = md_revalidate_lock(ll_i2mdexp(dir), it, ll_inode2fid(dir), NULL);
	if (rc != 1) {
		rc = -EAGAIN;
		goto out;
	}

	rc = ll_prep_inode(&child, req, dir->i_sb, it);
	if (rc)
		goto out;

	CDEBUG(D_READA, "%s: setting %.*s" DFID " l_data to inode %p\n",
	       ll_get_fsname(child->i_sb, NULL, 0),
	       entry->se_qstr.len, entry->se_qstr.name,
	       PFID(ll_inode2fid(child)), child);
	ll_set_lock_data(ll_i2sbi(dir)->ll_md_exp, child, it, NULL);

	entry->se_inode = child;

	if (agl_should_run(sai, child))
		ll_agl_add(sai, child, entry->se_index);

out:
	/*
	 * sa_make_ready() will drop ldlm ibits lock refcount by calling
	 * ll_intent_drop_lock() in spite of failures. Do not worry about
	 * calling ll_intent_drop_lock() more than once.
	 */
	sa_make_ready(sai, entry, rc);
}

/* once there are async stat replies, instantiate sa_entry from replies */
static void sa_handle_callback(struct ll_statahead_info *sai)
{
	struct ll_inode_info *lli;

	lli = ll_i2info(sai->sai_dentry->d_inode);

	while (sa_has_callback(sai)) {
		struct sa_entry *entry;

		spin_lock(&lli->lli_sa_lock);
		if (unlikely(!sa_has_callback(sai))) {
			spin_unlock(&lli->lli_sa_lock);
			break;
		}
		entry = list_entry(sai->sai_interim_entries.next,
				   struct sa_entry, se_list);
		list_del_init(&entry->se_list);
		spin_unlock(&lli->lli_sa_lock);

		sa_instantiate(sai, entry);
	}
}

/*
 * callback for async stat, because this is called in ptlrpcd context, we only
 * put sa_entry in sai_cb_entries list, and let sa_handle_callback() to really
 * prepare inode and instantiate sa_entry later.
 */
static int ll_statahead_interpret(struct ptlrpc_request *req,
				  struct md_enqueue_info *minfo, int rc)
{
	struct lookup_intent     *it  = &minfo->mi_it;
	struct inode	     *dir = minfo->mi_dir;
	struct ll_inode_info     *lli = ll_i2info(dir);
	struct ll_statahead_info *sai = lli->lli_sai;
	struct sa_entry *entry = (struct sa_entry *)minfo->mi_cbdata;
	__u64 handle = 0;
	bool wakeup;

	if (it_disposition(it, DISP_LOOKUP_NEG))
		rc = -ENOENT;

	/*
	 * because statahead thread will wait for all inflight RPC to finish,
	 * sai should be always valid, no need to refcount
	 */
	LASSERT(sai);
	LASSERT(!thread_is_stopped(&sai->sai_thread));
	LASSERT(entry);

	CDEBUG(D_READA, "sa_entry %.*s rc %d\n",
	       entry->se_qstr.len, entry->se_qstr.name, rc);

	if (rc) {
		ll_intent_release(it);
		iput(dir);
		kfree(minfo);
	} else {
		/*
		 * release ibits lock ASAP to avoid deadlock when statahead
		 * thread enqueues lock on parent in readdir and another
		 * process enqueues lock on child with parent lock held, eg.
		 * unlink.
		 */
		handle = it->it_lock_handle;
		ll_intent_drop_lock(it);
	}

	spin_lock(&lli->lli_sa_lock);
	if (rc) {
		wakeup = __sa_make_ready(sai, entry, rc);
	} else {
		entry->se_minfo = minfo;
		entry->se_req = ptlrpc_request_addref(req);
		/*
		 * Release the async ibits lock ASAP to avoid deadlock
		 * when statahead thread tries to enqueue lock on parent
		 * for readpage and other tries to enqueue lock on child
		 * with parent's lock held, for example: unlink.
		 */
		entry->se_handle = handle;
		wakeup = !sa_has_callback(sai);
		list_add_tail(&entry->se_list, &sai->sai_interim_entries);
	}
	sai->sai_replied++;

	if (wakeup)
		wake_up(&sai->sai_thread.t_ctl_waitq);
	spin_unlock(&lli->lli_sa_lock);

	return rc;
}

/* finish async stat RPC arguments */
static void sa_fini_data(struct md_enqueue_info *minfo,
			 struct ldlm_enqueue_info *einfo)
{
	LASSERT(minfo && einfo);
	iput(minfo->mi_dir);
	kfree(minfo);
	kfree(einfo);
}

/**
 * prepare arguments for async stat RPC.
 */
static int sa_prep_data(struct inode *dir, struct inode *child,
			struct sa_entry *entry, struct md_enqueue_info **pmi,
			struct ldlm_enqueue_info **pei)
{
	const struct qstr      *qstr = &entry->se_qstr;
	struct md_enqueue_info   *minfo;
	struct ldlm_enqueue_info *einfo;
	struct md_op_data	*op_data;

	einfo = kzalloc(sizeof(*einfo), GFP_NOFS);
	if (!einfo)
		return -ENOMEM;

	minfo = kzalloc(sizeof(*minfo), GFP_NOFS);
	if (!minfo) {
		kfree(einfo);
		return -ENOMEM;
	}

	op_data = ll_prep_md_op_data(&minfo->mi_data, dir, child, qstr->name,
				     qstr->len, 0, LUSTRE_OPC_ANY, NULL);
	if (IS_ERR(op_data)) {
		kfree(einfo);
		kfree(minfo);
		return PTR_ERR(op_data);
	}

	minfo->mi_it.it_op = IT_GETATTR;
	minfo->mi_dir = igrab(dir);
	minfo->mi_cb = ll_statahead_interpret;
	minfo->mi_cbdata = entry;

	einfo->ei_type   = LDLM_IBITS;
	einfo->ei_mode   = it_to_lock_mode(&minfo->mi_it);
	einfo->ei_cb_bl  = ll_md_blocking_ast;
	einfo->ei_cb_cp  = ldlm_completion_ast;
	einfo->ei_cb_gl  = NULL;
	einfo->ei_cbdata = NULL;

	*pmi = minfo;
	*pei = einfo;

	return 0;
}

/* async stat for file not found in dcache */
static int sa_lookup(struct inode *dir, struct sa_entry *entry)
{
	struct md_enqueue_info   *minfo;
	struct ldlm_enqueue_info *einfo;
	int		       rc;

	rc = sa_prep_data(dir, NULL, entry, &minfo, &einfo);
	if (rc)
		return rc;

	rc = md_intent_getattr_async(ll_i2mdexp(dir), minfo, einfo);
	if (rc)
		sa_fini_data(minfo, einfo);

	return rc;
}

/**
 * async stat for file found in dcache, similar to .revalidate
 *
 * \retval	1 dentry valid, no RPC sent
 * \retval	0 dentry invalid, will send async stat RPC
 * \retval	negative number upon error
 */
static int sa_revalidate(struct inode *dir, struct sa_entry *entry,
			 struct dentry *dentry)
{
	struct inode	     *inode = d_inode(dentry);
	struct lookup_intent      it = { .it_op = IT_GETATTR,
					 .it_lock_handle = 0 };
	struct md_enqueue_info   *minfo;
	struct ldlm_enqueue_info *einfo;
	int rc;

	if (unlikely(!inode))
		return 1;

	if (d_mountpoint(dentry))
		return 1;

	entry->se_inode = igrab(inode);
	rc = md_revalidate_lock(ll_i2mdexp(dir), &it, ll_inode2fid(inode),
				NULL);
	if (rc == 1) {
		entry->se_handle = it.it_lock_handle;
		ll_intent_release(&it);
		return 1;
	}

	rc = sa_prep_data(dir, inode, entry, &minfo, &einfo);
	if (rc) {
		entry->se_inode = NULL;
		iput(inode);
		return rc;
	}

	rc = md_intent_getattr_async(ll_i2mdexp(dir), minfo, einfo);
	if (rc) {
		entry->se_inode = NULL;
		iput(inode);
		sa_fini_data(minfo, einfo);
	}

	return rc;
}

/* async stat for file with @name */
static void sa_statahead(struct dentry *parent, const char *name, int len)
{
	struct inode	     *dir    = d_inode(parent);
	struct ll_inode_info     *lli    = ll_i2info(dir);
	struct ll_statahead_info *sai    = lli->lli_sai;
	struct dentry	    *dentry = NULL;
	struct sa_entry *entry;
	int		       rc;

	entry = sa_alloc(parent, sai, sai->sai_index, name, len);
	if (IS_ERR(entry))
		return;

	dentry = d_lookup(parent, &entry->se_qstr);
	if (!dentry) {
		rc = sa_lookup(dir, entry);
	} else {
		rc = sa_revalidate(dir, entry, dentry);
		if (rc == 1 && agl_should_run(sai, d_inode(dentry)))
			ll_agl_add(sai, d_inode(dentry), entry->se_index);
	}

	if (dentry)
		dput(dentry);

	if (rc)
		sa_make_ready(sai, entry, rc);
	else
		sai->sai_sent++;

	sai->sai_index++;
}

/* async glimpse (agl) thread main function */
static int ll_agl_thread(void *arg)
{
	struct dentry	    *parent = arg;
	struct inode	     *dir    = d_inode(parent);
	struct ll_inode_info     *plli   = ll_i2info(dir);
	struct ll_inode_info     *clli;
	struct ll_sb_info	*sbi    = ll_i2sbi(dir);
	struct ll_statahead_info *sai;
	struct ptlrpc_thread *thread;
	struct l_wait_info	lwi    = { 0 };

	sai = ll_sai_get(dir);
	thread = &sai->sai_agl_thread;
	thread->t_pid = current_pid();
	CDEBUG(D_READA, "agl thread started: sai %p, parent %pd\n",
	       sai, parent);

	atomic_inc(&sbi->ll_agl_total);
	spin_lock(&plli->lli_agl_lock);
	sai->sai_agl_valid = 1;
	if (thread_is_init(thread))
		/* If someone else has changed the thread state
		 * (e.g. already changed to SVC_STOPPING), we can't just
		 * blindly overwrite that setting.
		 */
		thread_set_flags(thread, SVC_RUNNING);
	spin_unlock(&plli->lli_agl_lock);
	wake_up(&thread->t_ctl_waitq);

	while (1) {
		l_wait_event(thread->t_ctl_waitq,
			     !list_empty(&sai->sai_agls) ||
			     !thread_is_running(thread),
			     &lwi);

		if (!thread_is_running(thread))
			break;

		spin_lock(&plli->lli_agl_lock);
		/* The statahead thread maybe help to process AGL entries,
		 * so check whether list empty again.
		 */
		if (!list_empty(&sai->sai_agls)) {
			clli = list_entry(sai->sai_agls.next,
					  struct ll_inode_info, lli_agl_list);
			list_del_init(&clli->lli_agl_list);
			spin_unlock(&plli->lli_agl_lock);
			ll_agl_trigger(&clli->lli_vfs_inode, sai);
		} else {
			spin_unlock(&plli->lli_agl_lock);
		}
	}

	spin_lock(&plli->lli_agl_lock);
	sai->sai_agl_valid = 0;
	while (!list_empty(&sai->sai_agls)) {
		clli = list_entry(sai->sai_agls.next,
				  struct ll_inode_info, lli_agl_list);
		list_del_init(&clli->lli_agl_list);
		spin_unlock(&plli->lli_agl_lock);
		clli->lli_agl_index = 0;
		iput(&clli->lli_vfs_inode);
		spin_lock(&plli->lli_agl_lock);
	}
	thread_set_flags(thread, SVC_STOPPED);
	spin_unlock(&plli->lli_agl_lock);
	wake_up(&thread->t_ctl_waitq);
	ll_sai_put(sai);
	CDEBUG(D_READA, "agl thread stopped: sai %p, parent %pd\n",
	       sai, parent);
	return 0;
}

/* start agl thread */
static void ll_start_agl(struct dentry *parent, struct ll_statahead_info *sai)
{
	struct ptlrpc_thread *thread = &sai->sai_agl_thread;
	struct l_wait_info    lwi    = { 0 };
	struct ll_inode_info  *plli;
	struct task_struct *task;

	CDEBUG(D_READA, "start agl thread: sai %p, parent %pd\n",
	       sai, parent);

	plli = ll_i2info(d_inode(parent));
	task = kthread_run(ll_agl_thread, parent, "ll_agl_%u",
			   plli->lli_opendir_pid);
	if (IS_ERR(task)) {
		CERROR("can't start ll_agl thread, rc: %ld\n", PTR_ERR(task));
		thread_set_flags(thread, SVC_STOPPED);
		return;
	}

	l_wait_event(thread->t_ctl_waitq,
		     thread_is_running(thread) || thread_is_stopped(thread),
		     &lwi);
}

/* statahead thread main function */
static int ll_statahead_thread(void *arg)
{
	struct dentry	    *parent = arg;
	struct inode	     *dir    = d_inode(parent);
	struct ll_inode_info     *lli   = ll_i2info(dir);
	struct ll_sb_info	*sbi    = ll_i2sbi(dir);
	struct ll_statahead_info *sai;
	struct ptlrpc_thread *sa_thread;
	struct ptlrpc_thread *agl_thread;
	struct page	      *page = NULL;
	__u64		     pos    = 0;
	int		       first  = 0;
	int		       rc     = 0;
	struct md_op_data *op_data;
	struct l_wait_info	lwi    = { 0 };

	sai = ll_sai_get(dir);
	sa_thread = &sai->sai_thread;
	agl_thread = &sai->sai_agl_thread;
	sa_thread->t_pid = current_pid();
	CDEBUG(D_READA, "statahead thread starting: sai %p, parent %pd\n",
	       sai, parent);

	op_data = ll_prep_md_op_data(NULL, dir, dir, NULL, 0, 0,
				     LUSTRE_OPC_ANY, dir);
	if (IS_ERR(op_data)) {
		rc = PTR_ERR(op_data);
		goto out;
	}

	op_data->op_max_pages = ll_i2sbi(dir)->ll_md_brw_pages;

	if (sbi->ll_flags & LL_SBI_AGL_ENABLED)
		ll_start_agl(parent, sai);

	atomic_inc(&sbi->ll_sa_total);
	spin_lock(&lli->lli_sa_lock);
	if (thread_is_init(sa_thread))
		/* If someone else has changed the thread state
		 * (e.g. already changed to SVC_STOPPING), we can't just
		 * blindly overwrite that setting.
		 */
		thread_set_flags(sa_thread, SVC_RUNNING);
	spin_unlock(&lli->lli_sa_lock);
	wake_up(&sa_thread->t_ctl_waitq);

	while (pos != MDS_DIR_END_OFF && thread_is_running(sa_thread)) {
		struct lu_dirpage *dp;
		struct lu_dirent  *ent;

		sai->sai_in_readpage = 1;
		page = ll_get_dir_page(dir, op_data, pos);
		sai->sai_in_readpage = 0;
		if (IS_ERR(page)) {
			rc = PTR_ERR(page);
			CDEBUG(D_READA, "error reading dir "DFID" at %llu/%llu: opendir_pid = %u: rc = %d\n",
			       PFID(ll_inode2fid(dir)), pos, sai->sai_index,
			       lli->lli_opendir_pid, rc);
			break;
		}

		dp = page_address(page);
		for (ent = lu_dirent_start(dp);
		     ent && thread_is_running(sa_thread) && !sa_low_hit(sai);
		     ent = lu_dirent_next(ent)) {
			__u64 hash;
			int namelen;
			char *name;

			hash = le64_to_cpu(ent->lde_hash);
			if (unlikely(hash < pos))
				/*
				 * Skip until we find target hash value.
				 */
				continue;

			namelen = le16_to_cpu(ent->lde_namelen);
			if (unlikely(namelen == 0))
				/*
				 * Skip dummy record.
				 */
				continue;

			name = ent->lde_name;
			if (name[0] == '.') {
				if (namelen == 1) {
					/*
					 * skip "."
					 */
					continue;
				} else if (name[1] == '.' && namelen == 2) {
					/*
					 * skip ".."
					 */
					continue;
				} else if (!sai->sai_ls_all) {
					/*
					 * skip hidden files.
					 */
					sai->sai_skip_hidden++;
					continue;
				}
			}

			/*
			 * don't stat-ahead first entry.
			 */
			if (unlikely(++first == 1))
				continue;

			/* wait for spare statahead window */
			do {
				l_wait_event(sa_thread->t_ctl_waitq,
					     !sa_sent_full(sai) ||
					     sa_has_callback(sai) ||
					     !list_empty(&sai->sai_agls) ||
					     !thread_is_running(sa_thread),
					     &lwi);
				sa_handle_callback(sai);

				spin_lock(&lli->lli_agl_lock);
				while (sa_sent_full(sai) &&
				       !agl_list_empty(sai)) {
					struct ll_inode_info *clli;

					clli = list_entry(sai->sai_agls.next,
							  struct ll_inode_info, lli_agl_list);
					list_del_init(&clli->lli_agl_list);
					spin_unlock(&lli->lli_agl_lock);

					ll_agl_trigger(&clli->lli_vfs_inode,
						       sai);

					spin_lock(&lli->lli_agl_lock);
				}
				spin_unlock(&lli->lli_agl_lock);
			} while (sa_sent_full(sai) &&
				 thread_is_running(sa_thread));

			sa_statahead(parent, name, namelen);
		}

		pos = le64_to_cpu(dp->ldp_hash_end);
		ll_release_page(dir, page,
				le32_to_cpu(dp->ldp_flags) & LDF_COLLIDE);

		if (sa_low_hit(sai)) {
			rc = -EFAULT;
			atomic_inc(&sbi->ll_sa_wrong);
			CDEBUG(D_READA, "Statahead for dir "DFID" hit ratio too low: hit/miss %llu/%llu, sent/replied %llu/%llu, stopping statahead thread: pid %d\n",
			       PFID(&lli->lli_fid), sai->sai_hit,
			       sai->sai_miss, sai->sai_sent,
			       sai->sai_replied, current_pid());
			break;
		}
	}
	ll_finish_md_op_data(op_data);

	if (rc < 0) {
		spin_lock(&lli->lli_sa_lock);
		thread_set_flags(sa_thread, SVC_STOPPING);
		lli->lli_sa_enabled = 0;
		spin_unlock(&lli->lli_sa_lock);
	}

	/*
	 * statahead is finished, but statahead entries need to be cached, wait
	 * for file release to stop me.
	 */
	while (thread_is_running(sa_thread)) {
		l_wait_event(sa_thread->t_ctl_waitq,
			     sa_has_callback(sai) ||
			     !agl_list_empty(sai) ||
			     !thread_is_running(sa_thread),
			     &lwi);

		sa_handle_callback(sai);
	}
out:
	if (sai->sai_agl_valid) {
		spin_lock(&lli->lli_agl_lock);
		thread_set_flags(agl_thread, SVC_STOPPING);
		spin_unlock(&lli->lli_agl_lock);
		wake_up(&agl_thread->t_ctl_waitq);

		CDEBUG(D_READA, "stop agl thread: sai %p pid %u\n",
		       sai, (unsigned int)agl_thread->t_pid);
		l_wait_event(agl_thread->t_ctl_waitq,
			     thread_is_stopped(agl_thread),
			     &lwi);
	} else {
		/* Set agl_thread flags anyway. */
		thread_set_flags(agl_thread, SVC_STOPPED);
	}

	/*
	 * wait for inflight statahead RPCs to finish, and then we can free sai
	 * safely because statahead RPC will access sai data
	 */
	while (sai->sai_sent != sai->sai_replied) {
		/* in case we're not woken up, timeout wait */
		lwi = LWI_TIMEOUT(msecs_to_jiffies(MSEC_PER_SEC >> 3),
				  NULL, NULL);
		l_wait_event(sa_thread->t_ctl_waitq,
			     sai->sai_sent == sai->sai_replied, &lwi);
	}

	/* release resources held by statahead RPCs */
	sa_handle_callback(sai);

	spin_lock(&lli->lli_sa_lock);
	thread_set_flags(sa_thread, SVC_STOPPED);
	spin_unlock(&lli->lli_sa_lock);

	CDEBUG(D_READA, "statahead thread stopped: sai %p, parent %pd\n",
	       sai, parent);

	wake_up(&sai->sai_waitq);
	wake_up(&sa_thread->t_ctl_waitq);
	ll_sai_put(sai);

	return rc;
}

/* authorize opened dir handle @key to statahead */
void ll_authorize_statahead(struct inode *dir, void *key)
{
	struct ll_inode_info *lli = ll_i2info(dir);

	spin_lock(&lli->lli_sa_lock);
	if (!lli->lli_opendir_key && !lli->lli_sai) {
		/*
		 * if lli_sai is not NULL, it means previous statahead is not
		 * finished yet, we'd better not start a new statahead for now.
		 */
		LASSERT(!lli->lli_opendir_pid);
		lli->lli_opendir_key = key;
		lli->lli_opendir_pid = current_pid();
		lli->lli_sa_enabled = 1;
	}
	spin_unlock(&lli->lli_sa_lock);
}

/*
 * deauthorize opened dir handle @key to statahead, but statahead thread may
 * still be running, notify it to quit.
 */
void ll_deauthorize_statahead(struct inode *dir, void *key)
{
	struct ll_inode_info *lli = ll_i2info(dir);
	struct ll_statahead_info *sai;

	LASSERT(lli->lli_opendir_key == key);
	LASSERT(lli->lli_opendir_pid);

	CDEBUG(D_READA, "deauthorize statahead for "DFID"\n",
	       PFID(&lli->lli_fid));

	spin_lock(&lli->lli_sa_lock);
	lli->lli_opendir_key = NULL;
	lli->lli_opendir_pid = 0;
	lli->lli_sa_enabled = 0;
	sai = lli->lli_sai;
	if (sai && thread_is_running(&sai->sai_thread)) {
		/*
		 * statahead thread may not quit yet because it needs to cache
		 * entries, now it's time to tell it to quit.
		 */
		thread_set_flags(&sai->sai_thread, SVC_STOPPING);
		wake_up(&sai->sai_thread.t_ctl_waitq);
	}
	spin_unlock(&lli->lli_sa_lock);
}

enum {
	/**
	 * not first dirent, or is "."
	 */
	LS_NOT_FIRST_DE = 0,
	/**
	 * the first non-hidden dirent
	 */
	LS_FIRST_DE,
	/**
	 * the first hidden dirent, that is "."
	 */
	LS_FIRST_DOT_DE
};

/* file is first dirent under @dir */
static int is_first_dirent(struct inode *dir, struct dentry *dentry)
{
	const struct qstr  *target = &dentry->d_name;
	struct md_op_data *op_data;
	struct page	  *page;
	__u64		 pos    = 0;
	int		   dot_de;
	int rc = LS_NOT_FIRST_DE;

	op_data = ll_prep_md_op_data(NULL, dir, dir, NULL, 0, 0,
				     LUSTRE_OPC_ANY, dir);
	if (IS_ERR(op_data))
		return PTR_ERR(op_data);
	/**
	 * FIXME choose the start offset of the readdir
	 */
	op_data->op_max_pages = ll_i2sbi(dir)->ll_md_brw_pages;

	page = ll_get_dir_page(dir, op_data, pos);

	while (1) {
		struct lu_dirpage *dp;
		struct lu_dirent  *ent;

		if (IS_ERR(page)) {
			struct ll_inode_info *lli = ll_i2info(dir);

			rc = PTR_ERR(page);
			CERROR("%s: error reading dir "DFID" at %llu: opendir_pid = %u : rc = %d\n",
			       ll_get_fsname(dir->i_sb, NULL, 0),
			       PFID(ll_inode2fid(dir)), pos,
			       lli->lli_opendir_pid, rc);
			break;
		}

		dp = page_address(page);
		for (ent = lu_dirent_start(dp); ent;
		     ent = lu_dirent_next(ent)) {
			__u64 hash;
			int namelen;
			char *name;

			hash = le64_to_cpu(ent->lde_hash);
			/* The ll_get_dir_page() can return any page containing
			 * the given hash which may be not the start hash.
			 */
			if (unlikely(hash < pos))
				continue;

			namelen = le16_to_cpu(ent->lde_namelen);
			if (unlikely(namelen == 0))
				/*
				 * skip dummy record.
				 */
				continue;

			name = ent->lde_name;
			if (name[0] == '.') {
				if (namelen == 1)
					/*
					 * skip "."
					 */
					continue;
				else if (name[1] == '.' && namelen == 2)
					/*
					 * skip ".."
					 */
					continue;
				else
					dot_de = 1;
			} else {
				dot_de = 0;
			}

			if (dot_de && target->name[0] != '.') {
				CDEBUG(D_READA, "%.*s skip hidden file %.*s\n",
				       target->len, target->name,
				       namelen, name);
				continue;
			}

			if (target->len != namelen ||
			    memcmp(target->name, name, namelen) != 0)
				rc = LS_NOT_FIRST_DE;
			else if (!dot_de)
				rc = LS_FIRST_DE;
			else
				rc = LS_FIRST_DOT_DE;

			ll_release_page(dir, page, false);
			goto out;
		}
		pos = le64_to_cpu(dp->ldp_hash_end);
		if (pos == MDS_DIR_END_OFF) {
			/*
			 * End of directory reached.
			 */
			ll_release_page(dir, page, false);
			goto out;
		} else {
			/*
			 * chain is exhausted
			 * Normal case: continue to the next page.
			 */
			ll_release_page(dir, page,
					le32_to_cpu(dp->ldp_flags) &
					LDF_COLLIDE);
			page = ll_get_dir_page(dir, op_data, pos);
		}
	}
out:
	ll_finish_md_op_data(op_data);
	return rc;
}

/**
 * revalidate @dentryp from statahead cache
 *
 * \param[in]  dir	parent directory
 * \param[in]  sai	sai structure
 * \param[out] dentryp	pointer to dentry which will be revalidated
 * \param[in]  unplug	unplug statahead window only (normally for negative
 *			dentry)
 * \retval		1 on success, dentry is saved in @dentryp
 * \retval		0 if revalidation failed (no proper lock on client)
 * \retval		negative number upon error
 */
static int revalidate_statahead_dentry(struct inode *dir,
				       struct ll_statahead_info *sai,
				       struct dentry **dentryp,
				       bool unplug)
{
	struct sa_entry *entry = NULL;
	struct l_wait_info lwi = { 0 };
	struct ll_dentry_data *ldd;
	struct ll_inode_info *lli;
	int rc = 0;

	if ((*dentryp)->d_name.name[0] == '.') {
		if (sai->sai_ls_all ||
		    sai->sai_miss_hidden >= sai->sai_skip_hidden) {
			/*
			 * Hidden dentry is the first one, or statahead
			 * thread does not skip so many hidden dentries
			 * before "sai_ls_all" enabled as below.
			 */
		} else {
			if (!sai->sai_ls_all)
				/*
				 * It maybe because hidden dentry is not
				 * the first one, "sai_ls_all" was not
				 * set, then "ls -al" missed. Enable
				 * "sai_ls_all" for such case.
				 */
				sai->sai_ls_all = 1;

			/*
			 * Such "getattr" has been skipped before
			 * "sai_ls_all" enabled as above.
			 */
			sai->sai_miss_hidden++;
			return -EAGAIN;
		}
	}

	if (unplug) {
		rc = 1;
		goto out_unplug;
	}

	entry = sa_get(sai, &(*dentryp)->d_name);
	if (!entry) {
		rc = -EAGAIN;
		goto out_unplug;
	}

	/* if statahead is busy in readdir, help it do post-work */
	if (!sa_ready(entry) && sai->sai_in_readpage)
		sa_handle_callback(sai);

	if (!sa_ready(entry)) {
		sai->sai_index_wait = entry->se_index;
		lwi = LWI_TIMEOUT_INTR(cfs_time_seconds(30), NULL,
				       LWI_ON_SIGNAL_NOOP, NULL);
		rc = l_wait_event(sai->sai_waitq, sa_ready(entry), &lwi);
		if (rc < 0) {
			/*
			 * entry may not be ready, so it may be used by inflight
			 * statahead RPC, don't free it.
			 */
			entry = NULL;
			rc = -EAGAIN;
			goto out_unplug;
		}
	}

	if (entry->se_state == SA_ENTRY_SUCC && entry->se_inode) {
		struct inode *inode = entry->se_inode;
		struct lookup_intent it = { .it_op = IT_GETATTR,
					    .it_lock_handle = entry->se_handle };
		__u64 bits;

		rc = md_revalidate_lock(ll_i2mdexp(dir), &it,
					ll_inode2fid(inode), &bits);
		if (rc == 1) {
			if (!(*dentryp)->d_inode) {
				struct dentry *alias;

				alias = ll_splice_alias(inode, *dentryp);
				if (IS_ERR(alias)) {
					rc = PTR_ERR(alias);
					goto out_unplug;
				}
				*dentryp = alias;
				/**
				 * statahead prepared this inode, transfer inode
				 * refcount from sa_entry to dentry
				 */
				entry->se_inode = NULL;
			} else if ((*dentryp)->d_inode != inode) {
				/* revalidate, but inode is recreated */
				CDEBUG(D_READA,
				       "%s: stale dentry %pd inode "DFID", statahead inode "DFID"\n",
				       ll_get_fsname((*dentryp)->d_inode->i_sb,
						     NULL, 0),
				       *dentryp,
				       PFID(ll_inode2fid((*dentryp)->d_inode)),
				       PFID(ll_inode2fid(inode)));
				rc = -ESTALE;
				goto out_unplug;
			}

			if ((bits & MDS_INODELOCK_LOOKUP) &&
			    d_lustre_invalid(*dentryp))
				d_lustre_revalidate(*dentryp);
			ll_intent_release(&it);
		}
	}
out_unplug:
	/*
	 * statahead cached sa_entry can be used only once, and will be killed
	 * right after use, so if lookup/revalidate accessed statahead cache,
	 * set dentry ldd_sa_generation to parent lli_sa_generation, later if we
	 * stat this file again, we know we've done statahead before, see
	 * dentry_may_statahead().
	 */
	ldd = ll_d2d(*dentryp);
	lli = ll_i2info(dir);
	/* ldd can be NULL if llite lookup failed. */
	if (ldd)
		ldd->lld_sa_generation = lli->lli_sa_generation;
	sa_put(sai, entry);
	return rc;
}

/**
 * start statahead thread
 *
 * \param[in] dir	parent directory
 * \param[in] dentry	dentry that triggers statahead, normally the first
 *			dirent under @dir
 * \retval		-EAGAIN on success, because when this function is
 *			called, it's already in lookup call, so client should
 *			do it itself instead of waiting for statahead thread
 *			to do it asynchronously.
 * \retval		negative number upon error
 */
static int start_statahead_thread(struct inode *dir, struct dentry *dentry)
{
	struct ll_inode_info *lli = ll_i2info(dir);
	struct ll_statahead_info *sai = NULL;
	struct l_wait_info lwi = { 0 };
	struct ptlrpc_thread *thread;
	struct task_struct *task;
	struct dentry *parent = dentry->d_parent;
	int rc;

	/* I am the "lli_opendir_pid" owner, only me can set "lli_sai". */
	rc = is_first_dirent(dir, dentry);
	if (rc == LS_NOT_FIRST_DE) {
		/* It is not "ls -{a}l" operation, no need statahead for it. */
		rc = -EFAULT;
		goto out;
	}

	sai = ll_sai_alloc(parent);
	if (!sai) {
		rc = -ENOMEM;
		goto out;
	}

	sai->sai_ls_all = (rc == LS_FIRST_DOT_DE);
	/*
	 * if current lli_opendir_key was deauthorized, or dir re-opened by
	 * another process, don't start statahead, otherwise the newly spawned
	 * statahead thread won't be notified to quit.
	 */
	spin_lock(&lli->lli_sa_lock);
	if (unlikely(lli->lli_sai || lli->lli_opendir_key ||
		     lli->lli_opendir_pid != current->pid)) {
		spin_unlock(&lli->lli_sa_lock);
		rc = -EPERM;
		goto out;
	}
	lli->lli_sai = sai;
	spin_unlock(&lli->lli_sa_lock);

	atomic_inc(&ll_i2sbi(parent->d_inode)->ll_sa_running);

	CDEBUG(D_READA, "start statahead thread: [pid %d] [parent %pd]\n",
	       current_pid(), parent);

	task = kthread_run(ll_statahead_thread, parent, "ll_sa_%u",
			   lli->lli_opendir_pid);
	thread = &sai->sai_thread;
	if (IS_ERR(task)) {
		rc = PTR_ERR(task);
		CERROR("can't start ll_sa thread, rc : %d\n", rc);
		goto out;
	}

	l_wait_event(thread->t_ctl_waitq,
		     thread_is_running(thread) || thread_is_stopped(thread),
		     &lwi);
	ll_sai_put(sai);

	/*
	 * We don't stat-ahead for the first dirent since we are already in
	 * lookup.
	 */
	return -EAGAIN;

out:
	/*
	 * once we start statahead thread failed, disable statahead so
	 * that subsequent stat won't waste time to try it.
	 */
	spin_lock(&lli->lli_sa_lock);
	lli->lli_sa_enabled = 0;
	lli->lli_sai = NULL;
	spin_unlock(&lli->lli_sa_lock);
	if (sai)
		ll_sai_free(sai);
	return rc;
}

/**
 * statahead entry function, this is called when client getattr on a file, it
 * will start statahead thread if this is the first dir entry, else revalidate
 * dentry from statahead cache.
 *
 * \param[in]  dir	parent directory
 * \param[out] dentryp	dentry to getattr
 * \param[in]  unplug	unplug statahead window only (normally for negative
 *			dentry)
 * \retval		1 on success
 * \retval		0 revalidation from statahead cache failed, caller needs
 *			to getattr from server directly
 * \retval		negative number on error, caller often ignores this and
 *			then getattr from server
 */
int ll_statahead(struct inode *dir, struct dentry **dentryp, bool unplug)
{
	struct ll_statahead_info *sai;

	sai = ll_sai_get(dir);
	if (sai) {
		int rc;

		rc = revalidate_statahead_dentry(dir, sai, dentryp, unplug);
		CDEBUG(D_READA, "revalidate statahead %pd: %d.\n",
		       *dentryp, rc);
		ll_sai_put(sai);
		return rc;
	}
	return start_statahead_thread(dir, *dentryp);
}
