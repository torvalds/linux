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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, 2013, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/fld/fld_internal.h
 *
 * Author: Yury Umanets <umka@clusterfs.com>
 * Author: Tom WangDi <wangdi@clusterfs.com>
 */
#ifndef __FLD_INTERNAL_H
#define __FLD_INTERNAL_H

#include "../include/lustre/lustre_idl.h"
#include "../include/dt_object.h"

#include "../../include/linux/libcfs/libcfs.h"
#include "../include/lustre_req_layout.h"
#include "../include/lustre_fld.h"

enum {
	LUSTRE_FLD_INIT = 1 << 0,
	LUSTRE_FLD_RUN  = 1 << 1
};

struct fld_stats {
	__u64   fst_count;
	__u64   fst_cache;
	__u64   fst_inflight;
};

typedef int (*fld_hash_func_t) (struct lu_client_fld *, __u64);

typedef struct lu_fld_target *
(*fld_scan_func_t) (struct lu_client_fld *, __u64);

struct lu_fld_hash {
	const char	      *fh_name;
	fld_hash_func_t	  fh_hash_func;
	fld_scan_func_t	  fh_scan_func;
};

struct fld_cache_entry {
	struct list_head	       fce_lru;
	struct list_head	       fce_list;
	/**
	 * fld cache entries are sorted on range->lsr_start field. */
	struct lu_seq_range      fce_range;
};

struct fld_cache {
	/**
	 * Cache guard, protects fci_hash mostly because others immutable after
	 * init is finished.
	 */
	rwlock_t		 fci_lock;

	/**
	 * Cache shrink threshold */
	int		      fci_threshold;

	/**
	 * Preferred number of cached entries */
	int		      fci_cache_size;

	/**
	 * Current number of cached entries. Protected by \a fci_lock */
	int		      fci_cache_count;

	/**
	 * LRU list fld entries. */
	struct list_head	       fci_lru;

	/**
	 * sorted fld entries. */
	struct list_head	       fci_entries_head;

	/**
	 * Cache statistics. */
	struct fld_stats	 fci_stat;

	/**
	 * Cache name used for debug and messages. */
	char		     fci_name[80];
	unsigned int		 fci_no_shrink:1;
};

enum fld_op {
	FLD_CREATE = 0,
	FLD_DELETE = 1,
	FLD_LOOKUP = 2
};

enum {
	/* 4M of FLD cache will not hurt client a lot. */
	FLD_SERVER_CACHE_SIZE      = (4 * 0x100000),

	/* 1M of FLD cache will not hurt client a lot. */
	FLD_CLIENT_CACHE_SIZE      = (1 * 0x100000)
};

enum {
	/* Cache threshold is 10 percent of size. */
	FLD_SERVER_CACHE_THRESHOLD = 10,

	/* Cache threshold is 10 percent of size. */
	FLD_CLIENT_CACHE_THRESHOLD = 10
};

extern struct lu_fld_hash fld_hash[];

int fld_client_rpc(struct obd_export *exp,
		   struct lu_seq_range *range, __u32 fld_op);

#ifdef LPROCFS
extern struct lprocfs_vars fld_client_proc_list[];
#endif


struct fld_cache *fld_cache_init(const char *name,
				 int cache_size, int cache_threshold);

void fld_cache_fini(struct fld_cache *cache);

void fld_cache_flush(struct fld_cache *cache);

int fld_cache_insert(struct fld_cache *cache,
		     const struct lu_seq_range *range);

struct fld_cache_entry
*fld_cache_entry_create(const struct lu_seq_range *range);

int fld_cache_insert_nolock(struct fld_cache *cache,
			    struct fld_cache_entry *f_new);
void fld_cache_delete(struct fld_cache *cache,
		      const struct lu_seq_range *range);
void fld_cache_delete_nolock(struct fld_cache *cache,
			     const struct lu_seq_range *range);
int fld_cache_lookup(struct fld_cache *cache,
		     const seqno_t seq, struct lu_seq_range *range);

struct fld_cache_entry*
fld_cache_entry_lookup(struct fld_cache *cache, struct lu_seq_range *range);
void fld_cache_entry_delete(struct fld_cache *cache,
			    struct fld_cache_entry *node);
void fld_dump_cache_entries(struct fld_cache *cache);

struct fld_cache_entry
*fld_cache_entry_lookup_nolock(struct fld_cache *cache,
			      struct lu_seq_range *range);
int fld_write_range(const struct lu_env *env, struct dt_object *dt,
		    const struct lu_seq_range *range, struct thandle *th);

static inline const char *
fld_target_name(struct lu_fld_target *tar)
{
	if (tar->ft_srv != NULL)
		return tar->ft_srv->lsf_name;

	return (const char *)tar->ft_exp->exp_obd->obd_name;
}

#endif /* __FLD_INTERNAL_H */
