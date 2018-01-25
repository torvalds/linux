// SPDX-License-Identifier: GPL-2.0
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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/fld/fld_internal.h
 *
 * Subsystem Description:
 * FLD is FID Location Database, which stores where (IE, on which MDT)
 * FIDs are located.
 * The database is basically a record file, each record consists of a FID
 * sequence range, MDT/OST index, and flags. The FLD for the whole FS
 * is only stored on the sequence controller(MDT0) right now, but each target
 * also has its local FLD, which only stores the local sequence.
 *
 * The FLD subsystem usually has two tasks:
 * 1. maintain the database, i.e. when the sequence controller allocates
 * new sequence ranges to some nodes, it will call the FLD API to insert the
 * location information <sequence_range, node_index> in FLDB.
 *
 * 2. Handle requests from other nodes, i.e. if client needs to know where
 * the FID is located, if it can not find the information in the local cache,
 * it will send a FLD lookup RPC to the FLD service, and the FLD service will
 * look up the FLDB entry and return the location information to client.
 *
 *
 * Author: Yury Umanets <umka@clusterfs.com>
 * Author: Tom WangDi <wangdi@clusterfs.com>
 */
#ifndef __FLD_INTERNAL_H
#define __FLD_INTERNAL_H

#include <uapi/linux/lustre/lustre_idl.h>

#include <linux/libcfs/libcfs.h>
#include <lustre_req_layout.h>
#include <lustre_fld.h>

struct fld_stats {
	__u64   fst_count;
	__u64   fst_cache;
	__u64   fst_inflight;
};

struct lu_fld_hash {
	const char	      *fh_name;
	int (*fh_hash_func)(struct lu_client_fld *, __u64);
	struct lu_fld_target *(*fh_scan_func)(struct lu_client_fld *, __u64);
};

struct fld_cache_entry {
	struct list_head	       fce_lru;
	struct list_head	       fce_list;
	/** fld cache entries are sorted on range->lsr_start field. */
	struct lu_seq_range      fce_range;
};

struct fld_cache {
	/**
	 * Cache guard, protects fci_hash mostly because others immutable after
	 * init is finished.
	 */
	rwlock_t		 fci_lock;

	/** Cache shrink threshold */
	int		      fci_threshold;

	/** Preferred number of cached entries */
	int		      fci_cache_size;

	/** Current number of cached entries. Protected by \a fci_lock */
	int		      fci_cache_count;

	/** LRU list fld entries. */
	struct list_head	       fci_lru;

	/** sorted fld entries. */
	struct list_head	       fci_entries_head;

	/** Cache statistics. */
	struct fld_stats	 fci_stat;

	/** Cache name used for debug and messages. */
	char		     fci_name[LUSTRE_MDT_MAXNAMELEN];
	unsigned int		 fci_no_shrink:1;
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
		   struct lu_seq_range *range, __u32 fld_op,
		   struct ptlrpc_request **reqp);

extern struct lprocfs_vars fld_client_debugfs_list[];

struct fld_cache *fld_cache_init(const char *name,
				 int cache_size, int cache_threshold);

void fld_cache_fini(struct fld_cache *cache);

void fld_cache_flush(struct fld_cache *cache);

int fld_cache_insert(struct fld_cache *cache,
		     const struct lu_seq_range *range);

struct fld_cache_entry
*fld_cache_entry_create(const struct lu_seq_range *range);

int fld_cache_lookup(struct fld_cache *cache,
		     const u64 seq, struct lu_seq_range *range);

struct fld_cache_entry*
fld_cache_entry_lookup(struct fld_cache *cache, struct lu_seq_range *range);

struct fld_cache_entry
*fld_cache_entry_lookup_nolock(struct fld_cache *cache,
			      struct lu_seq_range *range);

static inline const char *
fld_target_name(struct lu_fld_target *tar)
{
	if (tar->ft_srv)
		return tar->ft_srv->lsf_name;

	return (const char *)tar->ft_exp->exp_obd->obd_name;
}

#endif /* __FLD_INTERNAL_H */
