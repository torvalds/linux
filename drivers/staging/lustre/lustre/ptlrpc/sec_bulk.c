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
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/ptlrpc/sec_bulk.c
 *
 * Author: Eric Mei <ericm@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_SEC

#include <linux/libcfs/libcfs.h>

#include <obd.h>
#include <obd_cksum.h>
#include <obd_class.h>
#include <obd_support.h>
#include <lustre_net.h>
#include <lustre_import.h>
#include <lustre_dlm.h>
#include <lustre_sec.h>

#include "ptlrpc_internal.h"

/****************************************
 * bulk encryption page pools	   *
 ****************************************/

#define POINTERS_PER_PAGE	(PAGE_SIZE / sizeof(void *))
#define PAGES_PER_POOL		(POINTERS_PER_PAGE)

#define IDLE_IDX_MAX	 (100)
#define IDLE_IDX_WEIGHT	 (3)

#define CACHE_QUIESCENT_PERIOD  (20)

static struct ptlrpc_enc_page_pool {
	/*
	 * constants
	 */
	unsigned long    epp_max_pages;   /* maximum pages can hold, const */
	unsigned int     epp_max_pools;   /* number of pools, const */

	/*
	 * wait queue in case of not enough free pages.
	 */
	wait_queue_head_t      epp_waitq;       /* waiting threads */
	unsigned int     epp_waitqlen;    /* wait queue length */
	unsigned long    epp_pages_short; /* # of pages wanted of in-q users */
	unsigned int     epp_growing:1;   /* during adding pages */

	/*
	 * indicating how idle the pools are, from 0 to MAX_IDLE_IDX
	 * this is counted based on each time when getting pages from
	 * the pools, not based on time. which means in case that system
	 * is idled for a while but the idle_idx might still be low if no
	 * activities happened in the pools.
	 */
	unsigned long    epp_idle_idx;

	/* last shrink time due to mem tight */
	time64_t         epp_last_shrink;
	time64_t         epp_last_access;

	/*
	 * in-pool pages bookkeeping
	 */
	spinlock_t	 epp_lock;	   /* protect following fields */
	unsigned long    epp_total_pages; /* total pages in pools */
	unsigned long    epp_free_pages;  /* current pages available */

	/*
	 * statistics
	 */
	unsigned long    epp_st_max_pages;      /* # of pages ever reached */
	unsigned int     epp_st_grows;	  /* # of grows */
	unsigned int     epp_st_grow_fails;     /* # of add pages failures */
	unsigned int     epp_st_shrinks;	/* # of shrinks */
	unsigned long    epp_st_access;	 /* # of access */
	unsigned long    epp_st_missings;       /* # of cache missing */
	unsigned long    epp_st_lowfree;	/* lowest free pages reached */
	unsigned int     epp_st_max_wqlen;      /* highest waitqueue length */
	unsigned long       epp_st_max_wait;       /* in jiffies */
	unsigned long	 epp_st_outofmem;	/* # of out of mem requests */
	/*
	 * pointers to pools
	 */
	struct page    ***epp_pools;
} page_pools;

/*
 * /sys/kernel/debug/lustre/sptlrpc/encrypt_page_pools
 */
int sptlrpc_proc_enc_pool_seq_show(struct seq_file *m, void *v)
{
	spin_lock(&page_pools.epp_lock);

	seq_printf(m,
		   "physical pages:	  %lu\n"
		   "pages per pool:	  %lu\n"
		   "max pages:	       %lu\n"
		   "max pools:	       %u\n"
		   "total pages:	     %lu\n"
		   "total free:	      %lu\n"
		   "idle index:	      %lu/100\n"
		   "last shrink:	     %lds\n"
		   "last access:	     %lds\n"
		   "max pages reached:       %lu\n"
		   "grows:		   %u\n"
		   "grows failure:	   %u\n"
		   "shrinks:		 %u\n"
		   "cache access:	    %lu\n"
		   "cache missing:	   %lu\n"
		   "low free mark:	   %lu\n"
		   "max waitqueue depth:     %u\n"
		   "max wait time:	   %ld/%lu\n"
		   "out of mem:		 %lu\n",
		   totalram_pages,
		   PAGES_PER_POOL,
		   page_pools.epp_max_pages,
		   page_pools.epp_max_pools,
		   page_pools.epp_total_pages,
		   page_pools.epp_free_pages,
		   page_pools.epp_idle_idx,
		   (long)(ktime_get_seconds() - page_pools.epp_last_shrink),
		   (long)(ktime_get_seconds() - page_pools.epp_last_access),
		   page_pools.epp_st_max_pages,
		   page_pools.epp_st_grows,
		   page_pools.epp_st_grow_fails,
		   page_pools.epp_st_shrinks,
		   page_pools.epp_st_access,
		   page_pools.epp_st_missings,
		   page_pools.epp_st_lowfree,
		   page_pools.epp_st_max_wqlen,
		   page_pools.epp_st_max_wait,
		   msecs_to_jiffies(MSEC_PER_SEC),
		   page_pools.epp_st_outofmem);

	spin_unlock(&page_pools.epp_lock);

	return 0;
}

static void enc_pools_release_free_pages(long npages)
{
	int p_idx, g_idx;
	int p_idx_max1, p_idx_max2;

	LASSERT(npages > 0);
	LASSERT(npages <= page_pools.epp_free_pages);
	LASSERT(page_pools.epp_free_pages <= page_pools.epp_total_pages);

	/* max pool index before the release */
	p_idx_max2 = (page_pools.epp_total_pages - 1) / PAGES_PER_POOL;

	page_pools.epp_free_pages -= npages;
	page_pools.epp_total_pages -= npages;

	/* max pool index after the release */
	p_idx_max1 = page_pools.epp_total_pages == 0 ? -1 :
		     ((page_pools.epp_total_pages - 1) / PAGES_PER_POOL);

	p_idx = page_pools.epp_free_pages / PAGES_PER_POOL;
	g_idx = page_pools.epp_free_pages % PAGES_PER_POOL;
	LASSERT(page_pools.epp_pools[p_idx]);

	while (npages--) {
		LASSERT(page_pools.epp_pools[p_idx]);
		LASSERT(page_pools.epp_pools[p_idx][g_idx]);

		__free_page(page_pools.epp_pools[p_idx][g_idx]);
		page_pools.epp_pools[p_idx][g_idx] = NULL;

		if (++g_idx == PAGES_PER_POOL) {
			p_idx++;
			g_idx = 0;
		}
	}

	/* free unused pools */
	while (p_idx_max1 < p_idx_max2) {
		LASSERT(page_pools.epp_pools[p_idx_max2]);
		kfree(page_pools.epp_pools[p_idx_max2]);
		page_pools.epp_pools[p_idx_max2] = NULL;
		p_idx_max2--;
	}
}

/*
 * we try to keep at least PTLRPC_MAX_BRW_PAGES pages in the pool.
 */
static unsigned long enc_pools_shrink_count(struct shrinker *s,
					    struct shrink_control *sc)
{
	/*
	 * if no pool access for a long time, we consider it's fully idle.
	 * a little race here is fine.
	 */
	if (unlikely(ktime_get_seconds() - page_pools.epp_last_access >
		     CACHE_QUIESCENT_PERIOD)) {
		spin_lock(&page_pools.epp_lock);
		page_pools.epp_idle_idx = IDLE_IDX_MAX;
		spin_unlock(&page_pools.epp_lock);
	}

	LASSERT(page_pools.epp_idle_idx <= IDLE_IDX_MAX);
	return max((int)page_pools.epp_free_pages - PTLRPC_MAX_BRW_PAGES, 0) *
		(IDLE_IDX_MAX - page_pools.epp_idle_idx) / IDLE_IDX_MAX;
}

/*
 * we try to keep at least PTLRPC_MAX_BRW_PAGES pages in the pool.
 */
static unsigned long enc_pools_shrink_scan(struct shrinker *s,
					   struct shrink_control *sc)
{
	spin_lock(&page_pools.epp_lock);
	sc->nr_to_scan = min_t(unsigned long, sc->nr_to_scan,
			      page_pools.epp_free_pages - PTLRPC_MAX_BRW_PAGES);
	if (sc->nr_to_scan > 0) {
		enc_pools_release_free_pages(sc->nr_to_scan);
		CDEBUG(D_SEC, "released %ld pages, %ld left\n",
		       (long)sc->nr_to_scan, page_pools.epp_free_pages);

		page_pools.epp_st_shrinks++;
		page_pools.epp_last_shrink = ktime_get_seconds();
	}
	spin_unlock(&page_pools.epp_lock);

	/*
	 * if no pool access for a long time, we consider it's fully idle.
	 * a little race here is fine.
	 */
	if (unlikely(ktime_get_seconds() - page_pools.epp_last_access >
		     CACHE_QUIESCENT_PERIOD)) {
		spin_lock(&page_pools.epp_lock);
		page_pools.epp_idle_idx = IDLE_IDX_MAX;
		spin_unlock(&page_pools.epp_lock);
	}

	LASSERT(page_pools.epp_idle_idx <= IDLE_IDX_MAX);
	return sc->nr_to_scan;
}

static inline
int npages_to_npools(unsigned long npages)
{
	return (int)DIV_ROUND_UP(npages, PAGES_PER_POOL);
}

/*
 * return how many pages cleaned up.
 */
static unsigned long enc_pools_cleanup(struct page ***pools, int npools)
{
	unsigned long cleaned = 0;
	int i, j;

	for (i = 0; i < npools; i++) {
		if (pools[i]) {
			for (j = 0; j < PAGES_PER_POOL; j++) {
				if (pools[i][j]) {
					__free_page(pools[i][j]);
					cleaned++;
				}
			}
			kfree(pools[i]);
			pools[i] = NULL;
		}
	}

	return cleaned;
}

static inline void enc_pools_wakeup(void)
{
	assert_spin_locked(&page_pools.epp_lock);

	if (unlikely(page_pools.epp_waitqlen)) {
		LASSERT(waitqueue_active(&page_pools.epp_waitq));
		wake_up_all(&page_pools.epp_waitq);
	}
}

/*
 * Export the number of free pages in the pool
 */
int get_free_pages_in_pool(void)
{
	return page_pools.epp_free_pages;
}

/*
 * Let outside world know if enc_pool full capacity is reached
 */
int pool_is_at_full_capacity(void)
{
	return (page_pools.epp_total_pages == page_pools.epp_max_pages);
}

void sptlrpc_enc_pool_put_pages(struct ptlrpc_bulk_desc *desc)
{
	int p_idx, g_idx;
	int i;

	LASSERT(ptlrpc_is_bulk_desc_kiov(desc->bd_type));

	if (!GET_ENC_KIOV(desc))
		return;

	LASSERT(desc->bd_iov_count > 0);

	spin_lock(&page_pools.epp_lock);

	p_idx = page_pools.epp_free_pages / PAGES_PER_POOL;
	g_idx = page_pools.epp_free_pages % PAGES_PER_POOL;

	LASSERT(page_pools.epp_free_pages + desc->bd_iov_count <=
		page_pools.epp_total_pages);
	LASSERT(page_pools.epp_pools[p_idx]);

	for (i = 0; i < desc->bd_iov_count; i++) {
		LASSERT(BD_GET_ENC_KIOV(desc, i).bv_page);
		LASSERT(g_idx != 0 || page_pools.epp_pools[p_idx]);
		LASSERT(!page_pools.epp_pools[p_idx][g_idx]);

		page_pools.epp_pools[p_idx][g_idx] =
			BD_GET_ENC_KIOV(desc, i).bv_page;

		if (++g_idx == PAGES_PER_POOL) {
			p_idx++;
			g_idx = 0;
		}
	}

	page_pools.epp_free_pages += desc->bd_iov_count;

	enc_pools_wakeup();

	spin_unlock(&page_pools.epp_lock);

	kfree(GET_ENC_KIOV(desc));
	GET_ENC_KIOV(desc) = NULL;
}

static inline void enc_pools_alloc(void)
{
	LASSERT(page_pools.epp_max_pools);
	page_pools.epp_pools =
		kvzalloc(page_pools.epp_max_pools *
				sizeof(*page_pools.epp_pools),
				GFP_KERNEL);
}

static inline void enc_pools_free(void)
{
	LASSERT(page_pools.epp_max_pools);
	LASSERT(page_pools.epp_pools);

	kvfree(page_pools.epp_pools);
}

static struct shrinker pools_shrinker = {
	.count_objects	= enc_pools_shrink_count,
	.scan_objects	= enc_pools_shrink_scan,
	.seeks		= DEFAULT_SEEKS,
};

int sptlrpc_enc_pool_init(void)
{
	int rc;

	/*
	 * maximum capacity is 1/8 of total physical memory.
	 * is the 1/8 a good number?
	 */
	page_pools.epp_max_pages = totalram_pages / 8;
	page_pools.epp_max_pools = npages_to_npools(page_pools.epp_max_pages);

	init_waitqueue_head(&page_pools.epp_waitq);
	page_pools.epp_waitqlen = 0;
	page_pools.epp_pages_short = 0;

	page_pools.epp_growing = 0;

	page_pools.epp_idle_idx = 0;
	page_pools.epp_last_shrink = ktime_get_seconds();
	page_pools.epp_last_access = ktime_get_seconds();

	spin_lock_init(&page_pools.epp_lock);
	page_pools.epp_total_pages = 0;
	page_pools.epp_free_pages = 0;

	page_pools.epp_st_max_pages = 0;
	page_pools.epp_st_grows = 0;
	page_pools.epp_st_grow_fails = 0;
	page_pools.epp_st_shrinks = 0;
	page_pools.epp_st_access = 0;
	page_pools.epp_st_missings = 0;
	page_pools.epp_st_lowfree = 0;
	page_pools.epp_st_max_wqlen = 0;
	page_pools.epp_st_max_wait = 0;
	page_pools.epp_st_outofmem = 0;

	enc_pools_alloc();
	if (!page_pools.epp_pools)
		return -ENOMEM;

	rc = register_shrinker(&pools_shrinker);
	if (rc)
		enc_pools_free();

	return rc;
}

void sptlrpc_enc_pool_fini(void)
{
	unsigned long cleaned, npools;

	LASSERT(page_pools.epp_pools);
	LASSERT(page_pools.epp_total_pages == page_pools.epp_free_pages);

	unregister_shrinker(&pools_shrinker);

	npools = npages_to_npools(page_pools.epp_total_pages);
	cleaned = enc_pools_cleanup(page_pools.epp_pools, npools);
	LASSERT(cleaned == page_pools.epp_total_pages);

	enc_pools_free();

	if (page_pools.epp_st_access > 0) {
		CDEBUG(D_SEC,
		       "max pages %lu, grows %u, grow fails %u, shrinks %u, access %lu, missing %lu, max qlen %u, max wait %ld/%ld, out of mem %lu\n",
		       page_pools.epp_st_max_pages, page_pools.epp_st_grows,
		       page_pools.epp_st_grow_fails,
		       page_pools.epp_st_shrinks, page_pools.epp_st_access,
		       page_pools.epp_st_missings, page_pools.epp_st_max_wqlen,
		       page_pools.epp_st_max_wait,
		       msecs_to_jiffies(MSEC_PER_SEC),
		       page_pools.epp_st_outofmem);
	}
}

static int cfs_hash_alg_id[] = {
	[BULK_HASH_ALG_NULL]	= CFS_HASH_ALG_NULL,
	[BULK_HASH_ALG_ADLER32]	= CFS_HASH_ALG_ADLER32,
	[BULK_HASH_ALG_CRC32]	= CFS_HASH_ALG_CRC32,
	[BULK_HASH_ALG_MD5]	= CFS_HASH_ALG_MD5,
	[BULK_HASH_ALG_SHA1]	= CFS_HASH_ALG_SHA1,
	[BULK_HASH_ALG_SHA256]	= CFS_HASH_ALG_SHA256,
	[BULK_HASH_ALG_SHA384]	= CFS_HASH_ALG_SHA384,
	[BULK_HASH_ALG_SHA512]	= CFS_HASH_ALG_SHA512,
};

const char *sptlrpc_get_hash_name(__u8 hash_alg)
{
	return cfs_crypto_hash_name(cfs_hash_alg_id[hash_alg]);
}

__u8 sptlrpc_get_hash_alg(const char *algname)
{
	return cfs_crypto_hash_alg(algname);
}

int bulk_sec_desc_unpack(struct lustre_msg *msg, int offset, int swabbed)
{
	struct ptlrpc_bulk_sec_desc *bsd;
	int			  size = msg->lm_buflens[offset];

	bsd = lustre_msg_buf(msg, offset, sizeof(*bsd));
	if (!bsd) {
		CERROR("Invalid bulk sec desc: size %d\n", size);
		return -EINVAL;
	}

	if (swabbed)
		__swab32s(&bsd->bsd_nob);

	if (unlikely(bsd->bsd_version != 0)) {
		CERROR("Unexpected version %u\n", bsd->bsd_version);
		return -EPROTO;
	}

	if (unlikely(bsd->bsd_type >= SPTLRPC_BULK_MAX)) {
		CERROR("Invalid type %u\n", bsd->bsd_type);
		return -EPROTO;
	}

	/* FIXME more sanity check here */

	if (unlikely(bsd->bsd_svc != SPTLRPC_BULK_SVC_NULL &&
		     bsd->bsd_svc != SPTLRPC_BULK_SVC_INTG &&
		     bsd->bsd_svc != SPTLRPC_BULK_SVC_PRIV)) {
		CERROR("Invalid svc %u\n", bsd->bsd_svc);
		return -EPROTO;
	}

	return 0;
}
EXPORT_SYMBOL(bulk_sec_desc_unpack);

int sptlrpc_get_bulk_checksum(struct ptlrpc_bulk_desc *desc, __u8 alg,
			      void *buf, int buflen)
{
	struct ahash_request *hdesc;
	int hashsize;
	unsigned int bufsize;
	int i, err;

	LASSERT(alg > BULK_HASH_ALG_NULL && alg < BULK_HASH_ALG_MAX);
	LASSERT(buflen >= 4);

	hdesc = cfs_crypto_hash_init(cfs_hash_alg_id[alg], NULL, 0);
	if (IS_ERR(hdesc)) {
		CERROR("Unable to initialize checksum hash %s\n",
		       cfs_crypto_hash_name(cfs_hash_alg_id[alg]));
		return PTR_ERR(hdesc);
	}

	hashsize = cfs_crypto_hash_digestsize(cfs_hash_alg_id[alg]);

	for (i = 0; i < desc->bd_iov_count; i++) {
		cfs_crypto_hash_update_page(hdesc,
					    BD_GET_KIOV(desc, i).bv_page,
					    BD_GET_KIOV(desc, i).bv_offset &
					    ~PAGE_MASK,
					    BD_GET_KIOV(desc, i).bv_len);
	}

	if (hashsize > buflen) {
		unsigned char hashbuf[CFS_CRYPTO_HASH_DIGESTSIZE_MAX];

		bufsize = sizeof(hashbuf);
		LASSERTF(bufsize >= hashsize, "bufsize = %u < hashsize %u\n",
			 bufsize, hashsize);
		err = cfs_crypto_hash_final(hdesc, hashbuf, &bufsize);
		memcpy(buf, hashbuf, buflen);
	} else {
		bufsize = buflen;
		err = cfs_crypto_hash_final(hdesc, buf, &bufsize);
	}

	return err;
}
