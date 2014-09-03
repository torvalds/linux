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
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
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
 * lustre/llite/llite_rmtacl.c
 *
 * Lustre Remote User Access Control List.
 *
 * Author: Fan Yong <fanyong@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_LLITE

#ifdef CONFIG_FS_POSIX_ACL

#include "../include/lustre_lite.h"
#include "../include/lustre_eacl.h"
#include "llite_internal.h"

static inline __u32 rce_hashfunc(uid_t id)
{
	return id & (RCE_HASHES - 1);
}

static inline __u32 ee_hashfunc(uid_t id)
{
	return id & (EE_HASHES - 1);
}

obd_valid rce_ops2valid(int ops)
{
	switch (ops) {
	case RMT_LSETFACL:
		return OBD_MD_FLRMTLSETFACL;
	case RMT_LGETFACL:
		return OBD_MD_FLRMTLGETFACL;
	case RMT_RSETFACL:
		return OBD_MD_FLRMTRSETFACL;
	case RMT_RGETFACL:
		return OBD_MD_FLRMTRGETFACL;
	default:
		return 0;
	}
}

static struct rmtacl_ctl_entry *rce_alloc(pid_t key, int ops)
{
	struct rmtacl_ctl_entry *rce;

	OBD_ALLOC_PTR(rce);
	if (!rce)
		return NULL;

	INIT_LIST_HEAD(&rce->rce_list);
	rce->rce_key = key;
	rce->rce_ops = ops;

	return rce;
}

static void rce_free(struct rmtacl_ctl_entry *rce)
{
	if (!list_empty(&rce->rce_list))
		list_del(&rce->rce_list);

	OBD_FREE_PTR(rce);
}

static struct rmtacl_ctl_entry *__rct_search(struct rmtacl_ctl_table *rct,
					   pid_t key)
{
	struct rmtacl_ctl_entry *rce;
	struct list_head *head = &rct->rct_entries[rce_hashfunc(key)];

	list_for_each_entry(rce, head, rce_list)
		if (rce->rce_key == key)
			return rce;

	return NULL;
}

struct rmtacl_ctl_entry *rct_search(struct rmtacl_ctl_table *rct, pid_t key)
{
	struct rmtacl_ctl_entry *rce;

	spin_lock(&rct->rct_lock);
	rce = __rct_search(rct, key);
	spin_unlock(&rct->rct_lock);
	return rce;
}

int rct_add(struct rmtacl_ctl_table *rct, pid_t key, int ops)
{
	struct rmtacl_ctl_entry *rce, *e;

	rce = rce_alloc(key, ops);
	if (rce == NULL)
		return -ENOMEM;

	spin_lock(&rct->rct_lock);
	e = __rct_search(rct, key);
	if (unlikely(e != NULL)) {
		CWARN("Unexpected stale rmtacl_entry found: "
		      "[key: %d] [ops: %d]\n", (int)key, ops);
		rce_free(e);
	}
	list_add_tail(&rce->rce_list, &rct->rct_entries[rce_hashfunc(key)]);
	spin_unlock(&rct->rct_lock);

	return 0;
}

int rct_del(struct rmtacl_ctl_table *rct, pid_t key)
{
	struct rmtacl_ctl_entry *rce;

	spin_lock(&rct->rct_lock);
	rce = __rct_search(rct, key);
	if (rce)
		rce_free(rce);
	spin_unlock(&rct->rct_lock);

	return rce ? 0 : -ENOENT;
}

void rct_init(struct rmtacl_ctl_table *rct)
{
	int i;

	spin_lock_init(&rct->rct_lock);
	for (i = 0; i < RCE_HASHES; i++)
		INIT_LIST_HEAD(&rct->rct_entries[i]);
}

void rct_fini(struct rmtacl_ctl_table *rct)
{
	struct rmtacl_ctl_entry *rce;
	int i;

	spin_lock(&rct->rct_lock);
	for (i = 0; i < RCE_HASHES; i++)
		while (!list_empty(&rct->rct_entries[i])) {
			rce = list_entry(rct->rct_entries[i].next,
					     struct rmtacl_ctl_entry, rce_list);
			rce_free(rce);
		}
	spin_unlock(&rct->rct_lock);
}


static struct eacl_entry *ee_alloc(pid_t key, struct lu_fid *fid, int type,
				   ext_acl_xattr_header *header)
{
	struct eacl_entry *ee;

	OBD_ALLOC_PTR(ee);
	if (!ee)
		return NULL;

	INIT_LIST_HEAD(&ee->ee_list);
	ee->ee_key = key;
	ee->ee_fid = *fid;
	ee->ee_type = type;
	ee->ee_acl = header;

	return ee;
}

void ee_free(struct eacl_entry *ee)
{
	if (!list_empty(&ee->ee_list))
		list_del(&ee->ee_list);

	if (ee->ee_acl)
		lustre_ext_acl_xattr_free(ee->ee_acl);

	OBD_FREE_PTR(ee);
}

static struct eacl_entry *__et_search_del(struct eacl_table *et, pid_t key,
					struct lu_fid *fid, int type)
{
	struct eacl_entry *ee;
	struct list_head *head = &et->et_entries[ee_hashfunc(key)];

	LASSERT(fid != NULL);
	list_for_each_entry(ee, head, ee_list)
		if (ee->ee_key == key) {
			if (lu_fid_eq(&ee->ee_fid, fid) &&
			    ee->ee_type == type) {
				list_del_init(&ee->ee_list);
				return ee;
			}
		}

	return NULL;
}

struct eacl_entry *et_search_del(struct eacl_table *et, pid_t key,
				 struct lu_fid *fid, int type)
{
	struct eacl_entry *ee;

	spin_lock(&et->et_lock);
	ee = __et_search_del(et, key, fid, type);
	spin_unlock(&et->et_lock);
	return ee;
}

void et_search_free(struct eacl_table *et, pid_t key)
{
	struct eacl_entry *ee, *next;
	struct list_head *head = &et->et_entries[ee_hashfunc(key)];

	spin_lock(&et->et_lock);
	list_for_each_entry_safe(ee, next, head, ee_list)
		if (ee->ee_key == key)
			ee_free(ee);

	spin_unlock(&et->et_lock);
}

int ee_add(struct eacl_table *et, pid_t key, struct lu_fid *fid, int type,
	   ext_acl_xattr_header *header)
{
	struct eacl_entry *ee, *e;

	ee = ee_alloc(key, fid, type, header);
	if (ee == NULL)
		return -ENOMEM;

	spin_lock(&et->et_lock);
	e = __et_search_del(et, key, fid, type);
	if (unlikely(e != NULL)) {
		CWARN("Unexpected stale eacl_entry found: "
		      "[key: %d] [fid: "DFID"] [type: %d]\n",
		      (int)key, PFID(fid), type);
		ee_free(e);
	}
	list_add_tail(&ee->ee_list, &et->et_entries[ee_hashfunc(key)]);
	spin_unlock(&et->et_lock);

	return 0;
}

void et_init(struct eacl_table *et)
{
	int i;

	spin_lock_init(&et->et_lock);
	for (i = 0; i < EE_HASHES; i++)
		INIT_LIST_HEAD(&et->et_entries[i]);
}

void et_fini(struct eacl_table *et)
{
	struct eacl_entry *ee;
	int i;

	spin_lock(&et->et_lock);
	for (i = 0; i < EE_HASHES; i++)
		while (!list_empty(&et->et_entries[i])) {
			ee = list_entry(et->et_entries[i].next,
					    struct eacl_entry, ee_list);
			ee_free(ee);
		}
	spin_unlock(&et->et_lock);
}

#endif
