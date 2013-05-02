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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2010, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/ldlm/ldlm_resource.c
 *
 * Author: Phil Schwan <phil@clusterfs.com>
 * Author: Peter Braam <braam@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_LDLM
# include <lustre_dlm.h>

#include <lustre_fid.h>
#include <obd_class.h>
#include "ldlm_internal.h"

struct kmem_cache *ldlm_resource_slab, *ldlm_lock_slab;

atomic_t ldlm_srv_namespace_nr = ATOMIC_INIT(0);
atomic_t ldlm_cli_namespace_nr = ATOMIC_INIT(0);

struct mutex ldlm_srv_namespace_lock;
LIST_HEAD(ldlm_srv_namespace_list);

struct mutex ldlm_cli_namespace_lock;
LIST_HEAD(ldlm_cli_namespace_list);

proc_dir_entry_t *ldlm_type_proc_dir = NULL;
proc_dir_entry_t *ldlm_ns_proc_dir = NULL;
proc_dir_entry_t *ldlm_svc_proc_dir = NULL;

extern unsigned int ldlm_cancel_unused_locks_before_replay;

/* during debug dump certain amount of granted locks for one resource to avoid
 * DDOS. */
unsigned int ldlm_dump_granted_max = 256;

#ifdef LPROCFS
static int ldlm_proc_dump_ns(struct file *file, const char *buffer,
			     unsigned long count, void *data)
{
	ldlm_dump_all_namespaces(LDLM_NAMESPACE_SERVER, D_DLMTRACE);
	ldlm_dump_all_namespaces(LDLM_NAMESPACE_CLIENT, D_DLMTRACE);
	RETURN(count);
}

int ldlm_proc_setup(void)
{
	int rc;
	struct lprocfs_vars list[] = {
		{ "dump_namespaces", NULL, ldlm_proc_dump_ns, NULL },
		{ "dump_granted_max",
		  lprocfs_rd_uint, lprocfs_wr_uint,
		  &ldlm_dump_granted_max, NULL },
		{ "cancel_unused_locks_before_replay",
		  lprocfs_rd_uint, lprocfs_wr_uint,
		  &ldlm_cancel_unused_locks_before_replay, NULL },
		{ NULL }};
	ENTRY;
	LASSERT(ldlm_ns_proc_dir == NULL);

	ldlm_type_proc_dir = lprocfs_register(OBD_LDLM_DEVICENAME,
					      proc_lustre_root,
					      NULL, NULL);
	if (IS_ERR(ldlm_type_proc_dir)) {
		CERROR("LProcFS failed in ldlm-init\n");
		rc = PTR_ERR(ldlm_type_proc_dir);
		GOTO(err, rc);
	}

	ldlm_ns_proc_dir = lprocfs_register("namespaces",
					    ldlm_type_proc_dir,
					    NULL, NULL);
	if (IS_ERR(ldlm_ns_proc_dir)) {
		CERROR("LProcFS failed in ldlm-init\n");
		rc = PTR_ERR(ldlm_ns_proc_dir);
		GOTO(err_type, rc);
	}

	ldlm_svc_proc_dir = lprocfs_register("services",
					    ldlm_type_proc_dir,
					    NULL, NULL);
	if (IS_ERR(ldlm_svc_proc_dir)) {
		CERROR("LProcFS failed in ldlm-init\n");
		rc = PTR_ERR(ldlm_svc_proc_dir);
		GOTO(err_ns, rc);
	}

	rc = lprocfs_add_vars(ldlm_type_proc_dir, list, NULL);

	RETURN(0);

err_ns:
	lprocfs_remove(&ldlm_ns_proc_dir);
err_type:
	lprocfs_remove(&ldlm_type_proc_dir);
err:
	ldlm_svc_proc_dir = NULL;
	RETURN(rc);
}

void ldlm_proc_cleanup(void)
{
	if (ldlm_svc_proc_dir)
		lprocfs_remove(&ldlm_svc_proc_dir);

	if (ldlm_ns_proc_dir)
		lprocfs_remove(&ldlm_ns_proc_dir);

	if (ldlm_type_proc_dir)
		lprocfs_remove(&ldlm_type_proc_dir);
}

static int lprocfs_rd_ns_resources(char *page, char **start, off_t off,
				   int count, int *eof, void *data)
{
	struct ldlm_namespace *ns  = data;
	__u64		  res = 0;
	cfs_hash_bd_t	  bd;
	int		    i;

	/* result is not strictly consistant */
	cfs_hash_for_each_bucket(ns->ns_rs_hash, &bd, i)
		res += cfs_hash_bd_count_get(&bd);
	return lprocfs_rd_u64(page, start, off, count, eof, &res);
}

static int lprocfs_rd_ns_locks(char *page, char **start, off_t off,
			       int count, int *eof, void *data)
{
	struct ldlm_namespace *ns = data;
	__u64		  locks;

	locks = lprocfs_stats_collector(ns->ns_stats, LDLM_NSS_LOCKS,
					LPROCFS_FIELDS_FLAGS_SUM);
	return lprocfs_rd_u64(page, start, off, count, eof, &locks);
}

static int lprocfs_rd_lru_size(char *page, char **start, off_t off,
			       int count, int *eof, void *data)
{
	struct ldlm_namespace *ns = data;
	__u32 *nr = &ns->ns_max_unused;

	if (ns_connect_lru_resize(ns))
		nr = &ns->ns_nr_unused;
	return lprocfs_rd_uint(page, start, off, count, eof, nr);
}

static int lprocfs_wr_lru_size(struct file *file, const char *buffer,
			       unsigned long count, void *data)
{
	struct ldlm_namespace *ns = data;
	char dummy[MAX_STRING_SIZE + 1], *end;
	unsigned long tmp;
	int lru_resize;

	dummy[MAX_STRING_SIZE] = '\0';
	if (copy_from_user(dummy, buffer, MAX_STRING_SIZE))
		return -EFAULT;

	if (strncmp(dummy, "clear", 5) == 0) {
		CDEBUG(D_DLMTRACE,
		       "dropping all unused locks from namespace %s\n",
		       ldlm_ns_name(ns));
		if (ns_connect_lru_resize(ns)) {
			int canceled, unused  = ns->ns_nr_unused;

			/* Try to cancel all @ns_nr_unused locks. */
			canceled = ldlm_cancel_lru(ns, unused, 0,
						   LDLM_CANCEL_PASSED);
			if (canceled < unused) {
				CDEBUG(D_DLMTRACE,
				       "not all requested locks are canceled, "
				       "requested: %d, canceled: %d\n", unused,
				       canceled);
				return -EINVAL;
			}
		} else {
			tmp = ns->ns_max_unused;
			ns->ns_max_unused = 0;
			ldlm_cancel_lru(ns, 0, 0, LDLM_CANCEL_PASSED);
			ns->ns_max_unused = tmp;
		}
		return count;
	}

	tmp = simple_strtoul(dummy, &end, 0);
	if (dummy == end) {
		CERROR("invalid value written\n");
		return -EINVAL;
	}
	lru_resize = (tmp == 0);

	if (ns_connect_lru_resize(ns)) {
		if (!lru_resize)
			ns->ns_max_unused = (unsigned int)tmp;

		if (tmp > ns->ns_nr_unused)
			tmp = ns->ns_nr_unused;
		tmp = ns->ns_nr_unused - tmp;

		CDEBUG(D_DLMTRACE,
		       "changing namespace %s unused locks from %u to %u\n",
		       ldlm_ns_name(ns), ns->ns_nr_unused,
		       (unsigned int)tmp);
		ldlm_cancel_lru(ns, tmp, LCF_ASYNC, LDLM_CANCEL_PASSED);

		if (!lru_resize) {
			CDEBUG(D_DLMTRACE,
			       "disable lru_resize for namespace %s\n",
			       ldlm_ns_name(ns));
			ns->ns_connect_flags &= ~OBD_CONNECT_LRU_RESIZE;
		}
	} else {
		CDEBUG(D_DLMTRACE,
		       "changing namespace %s max_unused from %u to %u\n",
		       ldlm_ns_name(ns), ns->ns_max_unused,
		       (unsigned int)tmp);
		ns->ns_max_unused = (unsigned int)tmp;
		ldlm_cancel_lru(ns, 0, LCF_ASYNC, LDLM_CANCEL_PASSED);

		/* Make sure that LRU resize was originally supported before
		 * turning it on here. */
		if (lru_resize &&
		    (ns->ns_orig_connect_flags & OBD_CONNECT_LRU_RESIZE)) {
			CDEBUG(D_DLMTRACE,
			       "enable lru_resize for namespace %s\n",
			       ldlm_ns_name(ns));
			ns->ns_connect_flags |= OBD_CONNECT_LRU_RESIZE;
		}
	}

	return count;
}

static int lprocfs_rd_elc(char *page, char **start, off_t off,
			  int count, int *eof, void *data)
{
	struct ldlm_namespace *ns = data;
	unsigned int supp = ns_connect_cancelset(ns);

	return lprocfs_rd_uint(page, start, off, count, eof, &supp);
}

static int lprocfs_wr_elc(struct file *file, const char *buffer,
			       unsigned long count, void *data)
{
	struct ldlm_namespace *ns = data;
	unsigned int supp = -1;
	int rc;

	rc = lprocfs_wr_uint(file, buffer, count, &supp);
	if (rc < 0)
		return rc;

	if (supp == 0)
		ns->ns_connect_flags &= ~OBD_CONNECT_CANCELSET;
	else if (ns->ns_orig_connect_flags & OBD_CONNECT_CANCELSET)
		ns->ns_connect_flags |= OBD_CONNECT_CANCELSET;
	return count;
}

void ldlm_namespace_proc_unregister(struct ldlm_namespace *ns)
{
	struct proc_dir_entry *dir;

	dir = lprocfs_srch(ldlm_ns_proc_dir, ldlm_ns_name(ns));
	if (dir == NULL) {
		CERROR("dlm namespace %s has no procfs dir?\n",
		       ldlm_ns_name(ns));
	} else {
		lprocfs_remove(&dir);
	}

	if (ns->ns_stats != NULL)
		lprocfs_free_stats(&ns->ns_stats);
}

int ldlm_namespace_proc_register(struct ldlm_namespace *ns)
{
	struct lprocfs_vars lock_vars[2];
	char lock_name[MAX_STRING_SIZE + 1];

	LASSERT(ns != NULL);
	LASSERT(ns->ns_rs_hash != NULL);

	ns->ns_stats = lprocfs_alloc_stats(LDLM_NSS_LAST, 0);
	if (ns->ns_stats == NULL)
		return -ENOMEM;

	lprocfs_counter_init(ns->ns_stats, LDLM_NSS_LOCKS,
			     LPROCFS_CNTR_AVGMINMAX, "locks", "locks");

	lock_name[MAX_STRING_SIZE] = '\0';

	memset(lock_vars, 0, sizeof(lock_vars));
	lock_vars[0].name = lock_name;

	snprintf(lock_name, MAX_STRING_SIZE, "%s/resource_count",
		 ldlm_ns_name(ns));
	lock_vars[0].data = ns;
	lock_vars[0].read_fptr = lprocfs_rd_ns_resources;
	lprocfs_add_vars(ldlm_ns_proc_dir, lock_vars, 0);

	snprintf(lock_name, MAX_STRING_SIZE, "%s/lock_count",
		 ldlm_ns_name(ns));
	lock_vars[0].data = ns;
	lock_vars[0].read_fptr = lprocfs_rd_ns_locks;
	lprocfs_add_vars(ldlm_ns_proc_dir, lock_vars, 0);

	if (ns_is_client(ns)) {
		snprintf(lock_name, MAX_STRING_SIZE, "%s/lock_unused_count",
			 ldlm_ns_name(ns));
		lock_vars[0].data = &ns->ns_nr_unused;
		lock_vars[0].read_fptr = lprocfs_rd_uint;
		lprocfs_add_vars(ldlm_ns_proc_dir, lock_vars, 0);

		snprintf(lock_name, MAX_STRING_SIZE, "%s/lru_size",
			 ldlm_ns_name(ns));
		lock_vars[0].data = ns;
		lock_vars[0].read_fptr = lprocfs_rd_lru_size;
		lock_vars[0].write_fptr = lprocfs_wr_lru_size;
		lprocfs_add_vars(ldlm_ns_proc_dir, lock_vars, 0);

		snprintf(lock_name, MAX_STRING_SIZE, "%s/lru_max_age",
			 ldlm_ns_name(ns));
		lock_vars[0].data = &ns->ns_max_age;
		lock_vars[0].read_fptr = lprocfs_rd_uint;
		lock_vars[0].write_fptr = lprocfs_wr_uint;
		lprocfs_add_vars(ldlm_ns_proc_dir, lock_vars, 0);

		snprintf(lock_name, MAX_STRING_SIZE, "%s/early_lock_cancel",
			 ldlm_ns_name(ns));
		lock_vars[0].data = ns;
		lock_vars[0].read_fptr = lprocfs_rd_elc;
		lock_vars[0].write_fptr = lprocfs_wr_elc;
		lprocfs_add_vars(ldlm_ns_proc_dir, lock_vars, 0);
	} else {
		snprintf(lock_name, MAX_STRING_SIZE, "%s/ctime_age_limit",
			 ldlm_ns_name(ns));
		lock_vars[0].data = &ns->ns_ctime_age_limit;
		lock_vars[0].read_fptr = lprocfs_rd_uint;
		lock_vars[0].write_fptr = lprocfs_wr_uint;
		lprocfs_add_vars(ldlm_ns_proc_dir, lock_vars, 0);

		snprintf(lock_name, MAX_STRING_SIZE, "%s/lock_timeouts",
			 ldlm_ns_name(ns));
		lock_vars[0].data = &ns->ns_timeouts;
		lock_vars[0].read_fptr = lprocfs_rd_uint;
		lprocfs_add_vars(ldlm_ns_proc_dir, lock_vars, 0);

		snprintf(lock_name, MAX_STRING_SIZE, "%s/max_nolock_bytes",
			 ldlm_ns_name(ns));
		lock_vars[0].data = &ns->ns_max_nolock_size;
		lock_vars[0].read_fptr = lprocfs_rd_uint;
		lock_vars[0].write_fptr = lprocfs_wr_uint;
		lprocfs_add_vars(ldlm_ns_proc_dir, lock_vars, 0);

		snprintf(lock_name, MAX_STRING_SIZE, "%s/contention_seconds",
			 ldlm_ns_name(ns));
		lock_vars[0].data = &ns->ns_contention_time;
		lock_vars[0].read_fptr = lprocfs_rd_uint;
		lock_vars[0].write_fptr = lprocfs_wr_uint;
		lprocfs_add_vars(ldlm_ns_proc_dir, lock_vars, 0);

		snprintf(lock_name, MAX_STRING_SIZE, "%s/contended_locks",
			 ldlm_ns_name(ns));
		lock_vars[0].data = &ns->ns_contended_locks;
		lock_vars[0].read_fptr = lprocfs_rd_uint;
		lock_vars[0].write_fptr = lprocfs_wr_uint;
		lprocfs_add_vars(ldlm_ns_proc_dir, lock_vars, 0);

		snprintf(lock_name, MAX_STRING_SIZE, "%s/max_parallel_ast",
			 ldlm_ns_name(ns));
		lock_vars[0].data = &ns->ns_max_parallel_ast;
		lock_vars[0].read_fptr = lprocfs_rd_uint;
		lock_vars[0].write_fptr = lprocfs_wr_uint;
		lprocfs_add_vars(ldlm_ns_proc_dir, lock_vars, 0);
	}
	return 0;
}
#undef MAX_STRING_SIZE
#else /* LPROCFS */

#define ldlm_namespace_proc_unregister(ns)      ({;})
#define ldlm_namespace_proc_register(ns)	({0;})

#endif /* LPROCFS */

static unsigned ldlm_res_hop_hash(cfs_hash_t *hs,
				  const void *key, unsigned mask)
{
	const struct ldlm_res_id     *id  = key;
	unsigned		val = 0;
	unsigned		i;

	for (i = 0; i < RES_NAME_SIZE; i++)
		val += id->name[i];
	return val & mask;
}

static unsigned ldlm_res_hop_fid_hash(cfs_hash_t *hs,
				      const void *key, unsigned mask)
{
	const struct ldlm_res_id *id = key;
	struct lu_fid       fid;
	__u32	       hash;
	__u32	       val;

	fid.f_seq = id->name[LUSTRE_RES_ID_SEQ_OFF];
	fid.f_oid = (__u32)id->name[LUSTRE_RES_ID_VER_OID_OFF];
	fid.f_ver = (__u32)(id->name[LUSTRE_RES_ID_VER_OID_OFF] >> 32);

	hash = fid_flatten32(&fid);
	hash += (hash >> 4) + (hash << 12); /* mixing oid and seq */
	if (id->name[LUSTRE_RES_ID_HSH_OFF] != 0) {
		val = id->name[LUSTRE_RES_ID_HSH_OFF];
		hash += (val >> 5) + (val << 11);
	} else {
		val = fid_oid(&fid);
	}
	hash = cfs_hash_long(hash, hs->hs_bkt_bits);
	/* give me another random factor */
	hash -= cfs_hash_long((unsigned long)hs, val % 11 + 3);

	hash <<= hs->hs_cur_bits - hs->hs_bkt_bits;
	hash |= ldlm_res_hop_hash(hs, key, CFS_HASH_NBKT(hs) - 1);

	return hash & mask;
}

static void *ldlm_res_hop_key(struct hlist_node *hnode)
{
	struct ldlm_resource   *res;

	res = hlist_entry(hnode, struct ldlm_resource, lr_hash);
	return &res->lr_name;
}

static int ldlm_res_hop_keycmp(const void *key, struct hlist_node *hnode)
{
	struct ldlm_resource   *res;

	res = hlist_entry(hnode, struct ldlm_resource, lr_hash);
	return ldlm_res_eq((const struct ldlm_res_id *)key,
			   (const struct ldlm_res_id *)&res->lr_name);
}

static void *ldlm_res_hop_object(struct hlist_node *hnode)
{
	return hlist_entry(hnode, struct ldlm_resource, lr_hash);
}

static void ldlm_res_hop_get_locked(cfs_hash_t *hs, struct hlist_node *hnode)
{
	struct ldlm_resource *res;

	res = hlist_entry(hnode, struct ldlm_resource, lr_hash);
	ldlm_resource_getref(res);
}

static void ldlm_res_hop_put_locked(cfs_hash_t *hs, struct hlist_node *hnode)
{
	struct ldlm_resource *res;

	res = hlist_entry(hnode, struct ldlm_resource, lr_hash);
	/* cfs_hash_for_each_nolock is the only chance we call it */
	ldlm_resource_putref_locked(res);
}

static void ldlm_res_hop_put(cfs_hash_t *hs, struct hlist_node *hnode)
{
	struct ldlm_resource *res;

	res = hlist_entry(hnode, struct ldlm_resource, lr_hash);
	ldlm_resource_putref(res);
}

cfs_hash_ops_t ldlm_ns_hash_ops = {
	.hs_hash	= ldlm_res_hop_hash,
	.hs_key	 = ldlm_res_hop_key,
	.hs_keycmp      = ldlm_res_hop_keycmp,
	.hs_keycpy      = NULL,
	.hs_object      = ldlm_res_hop_object,
	.hs_get	 = ldlm_res_hop_get_locked,
	.hs_put_locked  = ldlm_res_hop_put_locked,
	.hs_put	 = ldlm_res_hop_put
};

cfs_hash_ops_t ldlm_ns_fid_hash_ops = {
	.hs_hash	= ldlm_res_hop_fid_hash,
	.hs_key	 = ldlm_res_hop_key,
	.hs_keycmp      = ldlm_res_hop_keycmp,
	.hs_keycpy      = NULL,
	.hs_object      = ldlm_res_hop_object,
	.hs_get	 = ldlm_res_hop_get_locked,
	.hs_put_locked  = ldlm_res_hop_put_locked,
	.hs_put	 = ldlm_res_hop_put
};

typedef struct {
	ldlm_ns_type_t  nsd_type;
	/** hash bucket bits */
	unsigned	nsd_bkt_bits;
	/** hash bits */
	unsigned	nsd_all_bits;
	/** hash operations */
	cfs_hash_ops_t *nsd_hops;
} ldlm_ns_hash_def_t;

ldlm_ns_hash_def_t ldlm_ns_hash_defs[] =
{
	{
		.nsd_type       = LDLM_NS_TYPE_MDC,
		.nsd_bkt_bits   = 11,
		.nsd_all_bits   = 16,
		.nsd_hops       = &ldlm_ns_fid_hash_ops,
	},
	{
		.nsd_type       = LDLM_NS_TYPE_MDT,
		.nsd_bkt_bits   = 14,
		.nsd_all_bits   = 21,
		.nsd_hops       = &ldlm_ns_fid_hash_ops,
	},
	{
		.nsd_type       = LDLM_NS_TYPE_OSC,
		.nsd_bkt_bits   = 8,
		.nsd_all_bits   = 12,
		.nsd_hops       = &ldlm_ns_hash_ops,
	},
	{
		.nsd_type       = LDLM_NS_TYPE_OST,
		.nsd_bkt_bits   = 11,
		.nsd_all_bits   = 17,
		.nsd_hops       = &ldlm_ns_hash_ops,
	},
	{
		.nsd_type       = LDLM_NS_TYPE_MGC,
		.nsd_bkt_bits   = 4,
		.nsd_all_bits   = 4,
		.nsd_hops       = &ldlm_ns_hash_ops,
	},
	{
		.nsd_type       = LDLM_NS_TYPE_MGT,
		.nsd_bkt_bits   = 4,
		.nsd_all_bits   = 4,
		.nsd_hops       = &ldlm_ns_hash_ops,
	},
	{
		.nsd_type       = LDLM_NS_TYPE_UNKNOWN,
	},
};

/**
 * Create and initialize new empty namespace.
 */
struct ldlm_namespace *ldlm_namespace_new(struct obd_device *obd, char *name,
					  ldlm_side_t client,
					  ldlm_appetite_t apt,
					  ldlm_ns_type_t ns_type)
{
	struct ldlm_namespace *ns = NULL;
	struct ldlm_ns_bucket *nsb;
	ldlm_ns_hash_def_t    *nsd;
	cfs_hash_bd_t	  bd;
	int		    idx;
	int		    rc;
	ENTRY;

	LASSERT(obd != NULL);

	rc = ldlm_get_ref();
	if (rc) {
		CERROR("ldlm_get_ref failed: %d\n", rc);
		RETURN(NULL);
	}

	for (idx = 0;;idx++) {
		nsd = &ldlm_ns_hash_defs[idx];
		if (nsd->nsd_type == LDLM_NS_TYPE_UNKNOWN) {
			CERROR("Unknown type %d for ns %s\n", ns_type, name);
			GOTO(out_ref, NULL);
		}

		if (nsd->nsd_type == ns_type)
			break;
	}

	OBD_ALLOC_PTR(ns);
	if (!ns)
		GOTO(out_ref, NULL);

	ns->ns_rs_hash = cfs_hash_create(name,
					 nsd->nsd_all_bits, nsd->nsd_all_bits,
					 nsd->nsd_bkt_bits, sizeof(*nsb),
					 CFS_HASH_MIN_THETA,
					 CFS_HASH_MAX_THETA,
					 nsd->nsd_hops,
					 CFS_HASH_DEPTH |
					 CFS_HASH_BIGNAME |
					 CFS_HASH_SPIN_BKTLOCK |
					 CFS_HASH_NO_ITEMREF);
	if (ns->ns_rs_hash == NULL)
		GOTO(out_ns, NULL);

	cfs_hash_for_each_bucket(ns->ns_rs_hash, &bd, idx) {
		nsb = cfs_hash_bd_extra_get(ns->ns_rs_hash, &bd);
		at_init(&nsb->nsb_at_estimate, ldlm_enqueue_min, 0);
		nsb->nsb_namespace = ns;
	}

	ns->ns_obd      = obd;
	ns->ns_appetite = apt;
	ns->ns_client   = client;

	INIT_LIST_HEAD(&ns->ns_list_chain);
	INIT_LIST_HEAD(&ns->ns_unused_list);
	spin_lock_init(&ns->ns_lock);
	atomic_set(&ns->ns_bref, 0);
	init_waitqueue_head(&ns->ns_waitq);

	ns->ns_max_nolock_size    = NS_DEFAULT_MAX_NOLOCK_BYTES;
	ns->ns_contention_time    = NS_DEFAULT_CONTENTION_SECONDS;
	ns->ns_contended_locks    = NS_DEFAULT_CONTENDED_LOCKS;

	ns->ns_max_parallel_ast   = LDLM_DEFAULT_PARALLEL_AST_LIMIT;
	ns->ns_nr_unused	  = 0;
	ns->ns_max_unused	 = LDLM_DEFAULT_LRU_SIZE;
	ns->ns_max_age	    = LDLM_DEFAULT_MAX_ALIVE;
	ns->ns_ctime_age_limit    = LDLM_CTIME_AGE_LIMIT;
	ns->ns_timeouts	   = 0;
	ns->ns_orig_connect_flags = 0;
	ns->ns_connect_flags      = 0;
	ns->ns_stopping	   = 0;
	rc = ldlm_namespace_proc_register(ns);
	if (rc != 0) {
		CERROR("Can't initialize ns proc, rc %d\n", rc);
		GOTO(out_hash, rc);
	}

	idx = atomic_read(ldlm_namespace_nr(client));
	rc = ldlm_pool_init(&ns->ns_pool, ns, idx, client);
	if (rc) {
		CERROR("Can't initialize lock pool, rc %d\n", rc);
		GOTO(out_proc, rc);
	}

	ldlm_namespace_register(ns, client);
	RETURN(ns);
out_proc:
	ldlm_namespace_proc_unregister(ns);
	ldlm_namespace_cleanup(ns, 0);
out_hash:
	cfs_hash_putref(ns->ns_rs_hash);
out_ns:
	OBD_FREE_PTR(ns);
out_ref:
	ldlm_put_ref();
	RETURN(NULL);
}
EXPORT_SYMBOL(ldlm_namespace_new);

extern struct ldlm_lock *ldlm_lock_get(struct ldlm_lock *lock);

/**
 * Cancel and destroy all locks on a resource.
 *
 * If flags contains FL_LOCAL_ONLY, don't try to tell the server, just
 * clean up.  This is currently only used for recovery, and we make
 * certain assumptions as a result--notably, that we shouldn't cancel
 * locks with refs.
 */
static void cleanup_resource(struct ldlm_resource *res, struct list_head *q,
			     __u64 flags)
{
	struct list_head *tmp;
	int rc = 0, client = ns_is_client(ldlm_res_to_ns(res));
	bool local_only = !!(flags & LDLM_FL_LOCAL_ONLY);

	do {
		struct ldlm_lock *lock = NULL;

		/* First, we look for non-cleaned-yet lock
		 * all cleaned locks are marked by CLEANED flag. */
		lock_res(res);
		list_for_each(tmp, q) {
			lock = list_entry(tmp, struct ldlm_lock,
					      l_res_link);
			if (lock->l_flags & LDLM_FL_CLEANED) {
				lock = NULL;
				continue;
			}
			LDLM_LOCK_GET(lock);
			lock->l_flags |= LDLM_FL_CLEANED;
			break;
		}

		if (lock == NULL) {
			unlock_res(res);
			break;
		}

		/* Set CBPENDING so nothing in the cancellation path
		 * can match this lock. */
		lock->l_flags |= LDLM_FL_CBPENDING;
		lock->l_flags |= LDLM_FL_FAILED;
		lock->l_flags |= flags;

		/* ... without sending a CANCEL message for local_only. */
		if (local_only)
			lock->l_flags |= LDLM_FL_LOCAL_ONLY;

		if (local_only && (lock->l_readers || lock->l_writers)) {
			/* This is a little bit gross, but much better than the
			 * alternative: pretend that we got a blocking AST from
			 * the server, so that when the lock is decref'd, it
			 * will go away ... */
			unlock_res(res);
			LDLM_DEBUG(lock, "setting FL_LOCAL_ONLY");
			if (lock->l_completion_ast)
				lock->l_completion_ast(lock, 0, NULL);
			LDLM_LOCK_RELEASE(lock);
			continue;
		}

		if (client) {
			struct lustre_handle lockh;

			unlock_res(res);
			ldlm_lock2handle(lock, &lockh);
			rc = ldlm_cli_cancel(&lockh, LCF_ASYNC);
			if (rc)
				CERROR("ldlm_cli_cancel: %d\n", rc);
		} else {
			ldlm_resource_unlink_lock(lock);
			unlock_res(res);
			LDLM_DEBUG(lock, "Freeing a lock still held by a "
				   "client node");
			ldlm_lock_destroy(lock);
		}
		LDLM_LOCK_RELEASE(lock);
	} while (1);
}

static int ldlm_resource_clean(cfs_hash_t *hs, cfs_hash_bd_t *bd,
			       struct hlist_node *hnode, void *arg)
{
	struct ldlm_resource *res = cfs_hash_object(hs, hnode);
	__u64 flags = *(__u64 *)arg;

	cleanup_resource(res, &res->lr_granted, flags);
	cleanup_resource(res, &res->lr_converting, flags);
	cleanup_resource(res, &res->lr_waiting, flags);

	return 0;
}

static int ldlm_resource_complain(cfs_hash_t *hs, cfs_hash_bd_t *bd,
				  struct hlist_node *hnode, void *arg)
{
	struct ldlm_resource  *res = cfs_hash_object(hs, hnode);

	lock_res(res);
	CERROR("Namespace %s resource refcount nonzero "
	       "(%d) after lock cleanup; forcing "
	       "cleanup.\n",
	       ldlm_ns_name(ldlm_res_to_ns(res)),
	       atomic_read(&res->lr_refcount) - 1);

	CERROR("Resource: %p ("LPU64"/"LPU64"/"LPU64"/"
	       LPU64") (rc: %d)\n", res,
	       res->lr_name.name[0], res->lr_name.name[1],
	       res->lr_name.name[2], res->lr_name.name[3],
	       atomic_read(&res->lr_refcount) - 1);

	ldlm_resource_dump(D_ERROR, res);
	unlock_res(res);
	return 0;
}

/**
 * Cancel and destroy all locks in the namespace.
 *
 * Typically used during evictions when server notified client that it was
 * evicted and all of its state needs to be destroyed.
 * Also used during shutdown.
 */
int ldlm_namespace_cleanup(struct ldlm_namespace *ns, __u64 flags)
{
	if (ns == NULL) {
		CDEBUG(D_INFO, "NULL ns, skipping cleanup\n");
		return ELDLM_OK;
	}

	cfs_hash_for_each_nolock(ns->ns_rs_hash, ldlm_resource_clean, &flags);
	cfs_hash_for_each_nolock(ns->ns_rs_hash, ldlm_resource_complain, NULL);
	return ELDLM_OK;
}
EXPORT_SYMBOL(ldlm_namespace_cleanup);

/**
 * Attempts to free namespace.
 *
 * Only used when namespace goes away, like during an unmount.
 */
static int __ldlm_namespace_free(struct ldlm_namespace *ns, int force)
{
	ENTRY;

	/* At shutdown time, don't call the cancellation callback */
	ldlm_namespace_cleanup(ns, force ? LDLM_FL_LOCAL_ONLY : 0);

	if (atomic_read(&ns->ns_bref) > 0) {
		struct l_wait_info lwi = LWI_INTR(LWI_ON_SIGNAL_NOOP, NULL);
		int rc;
		CDEBUG(D_DLMTRACE,
		       "dlm namespace %s free waiting on refcount %d\n",
		       ldlm_ns_name(ns), atomic_read(&ns->ns_bref));
force_wait:
		if (force)
			lwi = LWI_TIMEOUT(obd_timeout * HZ / 4, NULL, NULL);

		rc = l_wait_event(ns->ns_waitq,
				  atomic_read(&ns->ns_bref) == 0, &lwi);

		/* Forced cleanups should be able to reclaim all references,
		 * so it's safe to wait forever... we can't leak locks... */
		if (force && rc == -ETIMEDOUT) {
			LCONSOLE_ERROR("Forced cleanup waiting for %s "
				       "namespace with %d resources in use, "
				       "(rc=%d)\n", ldlm_ns_name(ns),
				       atomic_read(&ns->ns_bref), rc);
			GOTO(force_wait, rc);
		}

		if (atomic_read(&ns->ns_bref)) {
			LCONSOLE_ERROR("Cleanup waiting for %s namespace "
				       "with %d resources in use, (rc=%d)\n",
				       ldlm_ns_name(ns),
				       atomic_read(&ns->ns_bref), rc);
			RETURN(ELDLM_NAMESPACE_EXISTS);
		}
		CDEBUG(D_DLMTRACE, "dlm namespace %s free done waiting\n",
		       ldlm_ns_name(ns));
	}

	RETURN(ELDLM_OK);
}

/**
 * Performs various cleanups for passed \a ns to make it drop refc and be
 * ready for freeing. Waits for refc == 0.
 *
 * The following is done:
 * (0) Unregister \a ns from its list to make inaccessible for potential
 * users like pools thread and others;
 * (1) Clear all locks in \a ns.
 */
void ldlm_namespace_free_prior(struct ldlm_namespace *ns,
			       struct obd_import *imp,
			       int force)
{
	int rc;
	ENTRY;
	if (!ns) {
		EXIT;
		return;
	}

	spin_lock(&ns->ns_lock);
	ns->ns_stopping = 1;
	spin_unlock(&ns->ns_lock);

	/*
	 * Can fail with -EINTR when force == 0 in which case try harder.
	 */
	rc = __ldlm_namespace_free(ns, force);
	if (rc != ELDLM_OK) {
		if (imp) {
			ptlrpc_disconnect_import(imp, 0);
			ptlrpc_invalidate_import(imp);
		}

		/*
		 * With all requests dropped and the import inactive
		 * we are gaurenteed all reference will be dropped.
		 */
		rc = __ldlm_namespace_free(ns, 1);
		LASSERT(rc == 0);
	}
	EXIT;
}

/**
 * Performs freeing memory structures related to \a ns. This is only done
 * when ldlm_namespce_free_prior() successfully removed all resources
 * referencing \a ns and its refc == 0.
 */
void ldlm_namespace_free_post(struct ldlm_namespace *ns)
{
	ENTRY;
	if (!ns) {
		EXIT;
		return;
	}

	/* Make sure that nobody can find this ns in its list. */
	ldlm_namespace_unregister(ns, ns->ns_client);
	/* Fini pool _before_ parent proc dir is removed. This is important as
	 * ldlm_pool_fini() removes own proc dir which is child to @dir.
	 * Removing it after @dir may cause oops. */
	ldlm_pool_fini(&ns->ns_pool);

	ldlm_namespace_proc_unregister(ns);
	cfs_hash_putref(ns->ns_rs_hash);
	/* Namespace \a ns should be not on list at this time, otherwise
	 * this will cause issues related to using freed \a ns in poold
	 * thread. */
	LASSERT(list_empty(&ns->ns_list_chain));
	OBD_FREE_PTR(ns);
	ldlm_put_ref();
	EXIT;
}

/**
 * Cleanup the resource, and free namespace.
 * bug 12864:
 * Deadlock issue:
 * proc1: destroy import
 *	class_disconnect_export(grab cl_sem) ->
 *	      -> ldlm_namespace_free ->
 *	      -> lprocfs_remove(grab _lprocfs_lock).
 * proc2: read proc info
 *	lprocfs_fops_read(grab _lprocfs_lock) ->
 *	      -> osc_rd_active, etc(grab cl_sem).
 *
 * So that I have to split the ldlm_namespace_free into two parts - the first
 * part ldlm_namespace_free_prior is used to cleanup the resource which is
 * being used; the 2nd part ldlm_namespace_free_post is used to unregister the
 * lprocfs entries, and then free memory. It will be called w/o cli->cl_sem
 * held.
 */
void ldlm_namespace_free(struct ldlm_namespace *ns,
			 struct obd_import *imp,
			 int force)
{
	ldlm_namespace_free_prior(ns, imp, force);
	ldlm_namespace_free_post(ns);
}
EXPORT_SYMBOL(ldlm_namespace_free);

void ldlm_namespace_get(struct ldlm_namespace *ns)
{
	atomic_inc(&ns->ns_bref);
}
EXPORT_SYMBOL(ldlm_namespace_get);

void ldlm_namespace_put(struct ldlm_namespace *ns)
{
	if (atomic_dec_and_lock(&ns->ns_bref, &ns->ns_lock)) {
		wake_up(&ns->ns_waitq);
		spin_unlock(&ns->ns_lock);
	}
}
EXPORT_SYMBOL(ldlm_namespace_put);

/** Register \a ns in the list of namespaces */
void ldlm_namespace_register(struct ldlm_namespace *ns, ldlm_side_t client)
{
	mutex_lock(ldlm_namespace_lock(client));
	LASSERT(list_empty(&ns->ns_list_chain));
	list_add(&ns->ns_list_chain, ldlm_namespace_list(client));
	atomic_inc(ldlm_namespace_nr(client));
	mutex_unlock(ldlm_namespace_lock(client));
}

/** Unregister \a ns from the list of namespaces. */
void ldlm_namespace_unregister(struct ldlm_namespace *ns, ldlm_side_t client)
{
	mutex_lock(ldlm_namespace_lock(client));
	LASSERT(!list_empty(&ns->ns_list_chain));
	/* Some asserts and possibly other parts of the code are still
	 * using list_empty(&ns->ns_list_chain). This is why it is
	 * important to use list_del_init() here. */
	list_del_init(&ns->ns_list_chain);
	atomic_dec(ldlm_namespace_nr(client));
	mutex_unlock(ldlm_namespace_lock(client));
}

/** Should be called with ldlm_namespace_lock(client) taken. */
void ldlm_namespace_move_locked(struct ldlm_namespace *ns, ldlm_side_t client)
{
	LASSERT(!list_empty(&ns->ns_list_chain));
	LASSERT(mutex_is_locked(ldlm_namespace_lock(client)));
	list_move_tail(&ns->ns_list_chain, ldlm_namespace_list(client));
}

/** Should be called with ldlm_namespace_lock(client) taken. */
struct ldlm_namespace *ldlm_namespace_first_locked(ldlm_side_t client)
{
	LASSERT(mutex_is_locked(ldlm_namespace_lock(client)));
	LASSERT(!list_empty(ldlm_namespace_list(client)));
	return container_of(ldlm_namespace_list(client)->next,
		struct ldlm_namespace, ns_list_chain);
}

/** Create and initialize new resource. */
static struct ldlm_resource *ldlm_resource_new(void)
{
	struct ldlm_resource *res;
	int idx;

	OBD_SLAB_ALLOC_PTR_GFP(res, ldlm_resource_slab, __GFP_IO);
	if (res == NULL)
		return NULL;

	INIT_LIST_HEAD(&res->lr_granted);
	INIT_LIST_HEAD(&res->lr_converting);
	INIT_LIST_HEAD(&res->lr_waiting);

	/* Initialize interval trees for each lock mode. */
	for (idx = 0; idx < LCK_MODE_NUM; idx++) {
		res->lr_itree[idx].lit_size = 0;
		res->lr_itree[idx].lit_mode = 1 << idx;
		res->lr_itree[idx].lit_root = NULL;
	}

	atomic_set(&res->lr_refcount, 1);
	spin_lock_init(&res->lr_lock);
	lu_ref_init(&res->lr_reference);

	/* The creator of the resource must unlock the mutex after LVB
	 * initialization. */
	mutex_init(&res->lr_lvb_mutex);
	mutex_lock(&res->lr_lvb_mutex);

	return res;
}

/**
 * Return a reference to resource with given name, creating it if necessary.
 * Args: namespace with ns_lock unlocked
 * Locks: takes and releases NS hash-lock and res->lr_lock
 * Returns: referenced, unlocked ldlm_resource or NULL
 */
struct ldlm_resource *
ldlm_resource_get(struct ldlm_namespace *ns, struct ldlm_resource *parent,
		  const struct ldlm_res_id *name, ldlm_type_t type, int create)
{
	struct hlist_node     *hnode;
	struct ldlm_resource *res;
	cfs_hash_bd_t	 bd;
	__u64		 version;

	LASSERT(ns != NULL);
	LASSERT(parent == NULL);
	LASSERT(ns->ns_rs_hash != NULL);
	LASSERT(name->name[0] != 0);

	cfs_hash_bd_get_and_lock(ns->ns_rs_hash, (void *)name, &bd, 0);
	hnode = cfs_hash_bd_lookup_locked(ns->ns_rs_hash, &bd, (void *)name);
	if (hnode != NULL) {
		cfs_hash_bd_unlock(ns->ns_rs_hash, &bd, 0);
		res = hlist_entry(hnode, struct ldlm_resource, lr_hash);
		/* Synchronize with regard to resource creation. */
		if (ns->ns_lvbo && ns->ns_lvbo->lvbo_init) {
			mutex_lock(&res->lr_lvb_mutex);
			mutex_unlock(&res->lr_lvb_mutex);
		}

		if (unlikely(res->lr_lvb_len < 0)) {
			ldlm_resource_putref(res);
			res = NULL;
		}
		return res;
	}

	version = cfs_hash_bd_version_get(&bd);
	cfs_hash_bd_unlock(ns->ns_rs_hash, &bd, 0);

	if (create == 0)
		return NULL;

	LASSERTF(type >= LDLM_MIN_TYPE && type < LDLM_MAX_TYPE,
		 "type: %d\n", type);
	res = ldlm_resource_new();
	if (!res)
		return NULL;

	res->lr_ns_bucket  = cfs_hash_bd_extra_get(ns->ns_rs_hash, &bd);
	res->lr_name       = *name;
	res->lr_type       = type;
	res->lr_most_restr = LCK_NL;

	cfs_hash_bd_lock(ns->ns_rs_hash, &bd, 1);
	hnode = (version == cfs_hash_bd_version_get(&bd)) ?  NULL :
		cfs_hash_bd_lookup_locked(ns->ns_rs_hash, &bd, (void *)name);

	if (hnode != NULL) {
		/* Someone won the race and already added the resource. */
		cfs_hash_bd_unlock(ns->ns_rs_hash, &bd, 1);
		/* Clean lu_ref for failed resource. */
		lu_ref_fini(&res->lr_reference);
		/* We have taken lr_lvb_mutex. Drop it. */
		mutex_unlock(&res->lr_lvb_mutex);
		OBD_SLAB_FREE(res, ldlm_resource_slab, sizeof *res);

		res = hlist_entry(hnode, struct ldlm_resource, lr_hash);
		/* Synchronize with regard to resource creation. */
		if (ns->ns_lvbo && ns->ns_lvbo->lvbo_init) {
			mutex_lock(&res->lr_lvb_mutex);
			mutex_unlock(&res->lr_lvb_mutex);
		}

		if (unlikely(res->lr_lvb_len < 0)) {
			ldlm_resource_putref(res);
			res = NULL;
		}
		return res;
	}
	/* We won! Let's add the resource. */
	cfs_hash_bd_add_locked(ns->ns_rs_hash, &bd, &res->lr_hash);
	if (cfs_hash_bd_count_get(&bd) == 1)
		ldlm_namespace_get(ns);

	cfs_hash_bd_unlock(ns->ns_rs_hash, &bd, 1);
	if (ns->ns_lvbo && ns->ns_lvbo->lvbo_init) {
		int rc;

		OBD_FAIL_TIMEOUT(OBD_FAIL_LDLM_CREATE_RESOURCE, 2);
		rc = ns->ns_lvbo->lvbo_init(res);
		if (rc < 0) {
			CERROR("lvbo_init failed for resource "
			       LPU64": rc %d\n", name->name[0], rc);
			if (res->lr_lvb_data) {
				OBD_FREE(res->lr_lvb_data, res->lr_lvb_len);
				res->lr_lvb_data = NULL;
			}
			res->lr_lvb_len = rc;
			mutex_unlock(&res->lr_lvb_mutex);
			ldlm_resource_putref(res);
			return NULL;
		}
	}

	/* We create resource with locked lr_lvb_mutex. */
	mutex_unlock(&res->lr_lvb_mutex);

	return res;
}
EXPORT_SYMBOL(ldlm_resource_get);

struct ldlm_resource *ldlm_resource_getref(struct ldlm_resource *res)
{
	LASSERT(res != NULL);
	LASSERT(res != LP_POISON);
	atomic_inc(&res->lr_refcount);
	CDEBUG(D_INFO, "getref res: %p count: %d\n", res,
	       atomic_read(&res->lr_refcount));
	return res;
}

static void __ldlm_resource_putref_final(cfs_hash_bd_t *bd,
					 struct ldlm_resource *res)
{
	struct ldlm_ns_bucket *nsb = res->lr_ns_bucket;

	if (!list_empty(&res->lr_granted)) {
		ldlm_resource_dump(D_ERROR, res);
		LBUG();
	}

	if (!list_empty(&res->lr_converting)) {
		ldlm_resource_dump(D_ERROR, res);
		LBUG();
	}

	if (!list_empty(&res->lr_waiting)) {
		ldlm_resource_dump(D_ERROR, res);
		LBUG();
	}

	cfs_hash_bd_del_locked(nsb->nsb_namespace->ns_rs_hash,
			       bd, &res->lr_hash);
	lu_ref_fini(&res->lr_reference);
	if (cfs_hash_bd_count_get(bd) == 0)
		ldlm_namespace_put(nsb->nsb_namespace);
}

/* Returns 1 if the resource was freed, 0 if it remains. */
int ldlm_resource_putref(struct ldlm_resource *res)
{
	struct ldlm_namespace *ns = ldlm_res_to_ns(res);
	cfs_hash_bd_t   bd;

	LASSERT_ATOMIC_GT_LT(&res->lr_refcount, 0, LI_POISON);
	CDEBUG(D_INFO, "putref res: %p count: %d\n",
	       res, atomic_read(&res->lr_refcount) - 1);

	cfs_hash_bd_get(ns->ns_rs_hash, &res->lr_name, &bd);
	if (cfs_hash_bd_dec_and_lock(ns->ns_rs_hash, &bd, &res->lr_refcount)) {
		__ldlm_resource_putref_final(&bd, res);
		cfs_hash_bd_unlock(ns->ns_rs_hash, &bd, 1);
		if (ns->ns_lvbo && ns->ns_lvbo->lvbo_free)
			ns->ns_lvbo->lvbo_free(res);
		OBD_SLAB_FREE(res, ldlm_resource_slab, sizeof *res);
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(ldlm_resource_putref);

/* Returns 1 if the resource was freed, 0 if it remains. */
int ldlm_resource_putref_locked(struct ldlm_resource *res)
{
	struct ldlm_namespace *ns = ldlm_res_to_ns(res);

	LASSERT_ATOMIC_GT_LT(&res->lr_refcount, 0, LI_POISON);
	CDEBUG(D_INFO, "putref res: %p count: %d\n",
	       res, atomic_read(&res->lr_refcount) - 1);

	if (atomic_dec_and_test(&res->lr_refcount)) {
		cfs_hash_bd_t bd;

		cfs_hash_bd_get(ldlm_res_to_ns(res)->ns_rs_hash,
				&res->lr_name, &bd);
		__ldlm_resource_putref_final(&bd, res);
		cfs_hash_bd_unlock(ns->ns_rs_hash, &bd, 1);
		/* NB: ns_rs_hash is created with CFS_HASH_NO_ITEMREF,
		 * so we should never be here while calling cfs_hash_del,
		 * cfs_hash_for_each_nolock is the only case we can get
		 * here, which is safe to release cfs_hash_bd_lock.
		 */
		if (ns->ns_lvbo && ns->ns_lvbo->lvbo_free)
			ns->ns_lvbo->lvbo_free(res);
		OBD_SLAB_FREE(res, ldlm_resource_slab, sizeof *res);

		cfs_hash_bd_lock(ns->ns_rs_hash, &bd, 1);
		return 1;
	}
	return 0;
}

/**
 * Add a lock into a given resource into specified lock list.
 */
void ldlm_resource_add_lock(struct ldlm_resource *res, struct list_head *head,
			    struct ldlm_lock *lock)
{
	check_res_locked(res);

	LDLM_DEBUG(lock, "About to add this lock:\n");

	if (lock->l_destroyed) {
		CDEBUG(D_OTHER, "Lock destroyed, not adding to resource\n");
		return;
	}

	LASSERT(list_empty(&lock->l_res_link));

	list_add_tail(&lock->l_res_link, head);
}

/**
 * Insert a lock into resource after specified lock.
 *
 * Obtain resource description from the lock we are inserting after.
 */
void ldlm_resource_insert_lock_after(struct ldlm_lock *original,
				     struct ldlm_lock *new)
{
	struct ldlm_resource *res = original->l_resource;

	check_res_locked(res);

	ldlm_resource_dump(D_INFO, res);
	LDLM_DEBUG(new, "About to insert this lock after %p:\n", original);

	if (new->l_destroyed) {
		CDEBUG(D_OTHER, "Lock destroyed, not adding to resource\n");
		goto out;
	}

	LASSERT(list_empty(&new->l_res_link));

	list_add(&new->l_res_link, &original->l_res_link);
 out:;
}

void ldlm_resource_unlink_lock(struct ldlm_lock *lock)
{
	int type = lock->l_resource->lr_type;

	check_res_locked(lock->l_resource);
	if (type == LDLM_IBITS || type == LDLM_PLAIN)
		ldlm_unlink_lock_skiplist(lock);
	else if (type == LDLM_EXTENT)
		ldlm_extent_unlink_lock(lock);
	list_del_init(&lock->l_res_link);
}
EXPORT_SYMBOL(ldlm_resource_unlink_lock);

void ldlm_res2desc(struct ldlm_resource *res, struct ldlm_resource_desc *desc)
{
	desc->lr_type = res->lr_type;
	desc->lr_name = res->lr_name;
}

/**
 * Print information about all locks in all namespaces on this node to debug
 * log.
 */
void ldlm_dump_all_namespaces(ldlm_side_t client, int level)
{
	struct list_head *tmp;

	if (!((libcfs_debug | D_ERROR) & level))
		return;

	mutex_lock(ldlm_namespace_lock(client));

	list_for_each(tmp, ldlm_namespace_list(client)) {
		struct ldlm_namespace *ns;
		ns = list_entry(tmp, struct ldlm_namespace, ns_list_chain);
		ldlm_namespace_dump(level, ns);
	}

	mutex_unlock(ldlm_namespace_lock(client));
}
EXPORT_SYMBOL(ldlm_dump_all_namespaces);

static int ldlm_res_hash_dump(cfs_hash_t *hs, cfs_hash_bd_t *bd,
			      struct hlist_node *hnode, void *arg)
{
	struct ldlm_resource *res = cfs_hash_object(hs, hnode);
	int    level = (int)(unsigned long)arg;

	lock_res(res);
	ldlm_resource_dump(level, res);
	unlock_res(res);

	return 0;
}

/**
 * Print information about all locks in this namespace on this node to debug
 * log.
 */
void ldlm_namespace_dump(int level, struct ldlm_namespace *ns)
{
	if (!((libcfs_debug | D_ERROR) & level))
		return;

	CDEBUG(level, "--- Namespace: %s (rc: %d, side: %s)\n",
	       ldlm_ns_name(ns), atomic_read(&ns->ns_bref),
	       ns_is_client(ns) ? "client" : "server");

	if (cfs_time_before(cfs_time_current(), ns->ns_next_dump))
		return;

	cfs_hash_for_each_nolock(ns->ns_rs_hash,
				 ldlm_res_hash_dump,
				 (void *)(unsigned long)level);
	spin_lock(&ns->ns_lock);
	ns->ns_next_dump = cfs_time_shift(10);
	spin_unlock(&ns->ns_lock);
}
EXPORT_SYMBOL(ldlm_namespace_dump);

/**
 * Print information about all locks in this resource to debug log.
 */
void ldlm_resource_dump(int level, struct ldlm_resource *res)
{
	struct ldlm_lock *lock;
	unsigned int granted = 0;

	CLASSERT(RES_NAME_SIZE == 4);

	if (!((libcfs_debug | D_ERROR) & level))
		return;

	CDEBUG(level, "--- Resource: %p ("LPU64"/"LPU64"/"LPU64"/"LPU64
	       ") (rc: %d)\n", res, res->lr_name.name[0], res->lr_name.name[1],
	       res->lr_name.name[2], res->lr_name.name[3],
	       atomic_read(&res->lr_refcount));

	if (!list_empty(&res->lr_granted)) {
		CDEBUG(level, "Granted locks (in reverse order):\n");
		list_for_each_entry_reverse(lock, &res->lr_granted,
						l_res_link) {
			LDLM_DEBUG_LIMIT(level, lock, "###");
			if (!(level & D_CANTMASK) &&
			    ++granted > ldlm_dump_granted_max) {
				CDEBUG(level, "only dump %d granted locks to "
				       "avoid DDOS.\n", granted);
				break;
			}
		}
	}
	if (!list_empty(&res->lr_converting)) {
		CDEBUG(level, "Converting locks:\n");
		list_for_each_entry(lock, &res->lr_converting, l_res_link)
			LDLM_DEBUG_LIMIT(level, lock, "###");
	}
	if (!list_empty(&res->lr_waiting)) {
		CDEBUG(level, "Waiting locks:\n");
		list_for_each_entry(lock, &res->lr_waiting, l_res_link)
			LDLM_DEBUG_LIMIT(level, lock, "###");
	}
}
