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
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/include/lustre_capa.h
 *
 * Author: Lai Siyao <lsy@clusterfs.com>
 */

#ifndef __LINUX_CAPA_H_
#define __LINUX_CAPA_H_

/** \defgroup capa capa
 *
 * @{
 */

/*
 * capability
 */
#include <linux/crypto.h>
#include "lustre/lustre_idl.h"

#define CAPA_TIMEOUT 1800		/* sec, == 30 min */
#define CAPA_KEY_TIMEOUT (24 * 60 * 60)  /* sec, == 1 days */

struct capa_hmac_alg {
	const char     *ha_name;
	int	     ha_len;
	int	     ha_keylen;
};

#define DEF_CAPA_HMAC_ALG(name, type, len, keylen)      \
[CAPA_HMAC_ALG_ ## type] = {			    \
	.ha_name	 = name,			\
	.ha_len	  = len,			 \
	.ha_keylen       = keylen,		      \
}

struct client_capa {
	struct inode	     *inode;
	struct list_head		lli_list;     /* link to lli_oss_capas */
};

struct target_capa {
	struct hlist_node	  c_hash;       /* link to capa hash */
};

struct obd_capa {
	struct list_head		c_list;       /* link to capa_list */

	struct lustre_capa	c_capa;       /* capa */
	atomic_t	      c_refc;       /* ref count */
	unsigned long		c_expiry;     /* jiffies */
	spinlock_t		c_lock;	/* protect capa content */
	int			c_site;

	union {
		struct client_capa	cli;
		struct target_capa	tgt;
	} u;
};

enum {
	CAPA_SITE_CLIENT = 0,
	CAPA_SITE_SERVER,
	CAPA_SITE_MAX
};

static inline struct lu_fid *capa_fid(struct lustre_capa *capa)
{
	return &capa->lc_fid;
}

static inline __u64 capa_opc(struct lustre_capa *capa)
{
	return capa->lc_opc;
}

static inline __u64 capa_uid(struct lustre_capa *capa)
{
	return capa->lc_uid;
}

static inline __u64 capa_gid(struct lustre_capa *capa)
{
	return capa->lc_gid;
}

static inline __u32 capa_flags(struct lustre_capa *capa)
{
	return capa->lc_flags & 0xffffff;
}

static inline __u32 capa_alg(struct lustre_capa *capa)
{
	return (capa->lc_flags >> 24);
}

static inline __u32 capa_keyid(struct lustre_capa *capa)
{
	return capa->lc_keyid;
}

static inline __u64 capa_key_seq(struct lustre_capa_key *key)
{
	return key->lk_seq;
}

static inline __u32 capa_key_keyid(struct lustre_capa_key *key)
{
	return key->lk_keyid;
}

static inline __u32 capa_timeout(struct lustre_capa *capa)
{
	return capa->lc_timeout;
}

static inline __u32 capa_expiry(struct lustre_capa *capa)
{
	return capa->lc_expiry;
}

void _debug_capa(struct lustre_capa *, struct libcfs_debug_msg_data *,
		 const char *fmt, ... );
#define DEBUG_CAPA(level, capa, fmt, args...)				  \
do {									   \
	if (((level) & D_CANTMASK) != 0 ||				     \
	    ((libcfs_debug & (level)) != 0 &&				  \
	     (libcfs_subsystem_debug & DEBUG_SUBSYSTEM) != 0)) {	       \
		LIBCFS_DEBUG_MSG_DATA_DECL(msgdata, level, NULL);	      \
		_debug_capa((capa), &msgdata, fmt, ##args);		    \
	}								      \
} while (0)

#define DEBUG_CAPA_KEY(level, k, fmt, args...)				 \
do {									   \
CDEBUG(level, fmt " capability key@%p seq "LPU64" keyid %u\n",		 \
       ##args, k, capa_key_seq(k), capa_key_keyid(k));			 \
} while (0)

typedef int (* renew_capa_cb_t)(struct obd_capa *, struct lustre_capa *);

/* obdclass/capa.c */
extern struct list_head capa_list[];
extern spinlock_t capa_lock;
extern int capa_count[];
extern struct kmem_cache *capa_cachep;

struct hlist_head *init_capa_hash(void);
void cleanup_capa_hash(struct hlist_head *hash);

struct obd_capa *capa_add(struct hlist_head *hash,
			  struct lustre_capa *capa);
struct obd_capa *capa_lookup(struct hlist_head *hash,
			     struct lustre_capa *capa, int alive);

int capa_hmac(__u8 *hmac, struct lustre_capa *capa, __u8 *key);
int capa_encrypt_id(__u32 *d, __u32 *s, __u8 *key, int keylen);
int capa_decrypt_id(__u32 *d, __u32 *s, __u8 *key, int keylen);
void capa_cpy(void *dst, struct obd_capa *ocapa);
static inline struct obd_capa *alloc_capa(int site)
{
	struct obd_capa *ocapa;

	if (unlikely(site != CAPA_SITE_CLIENT && site != CAPA_SITE_SERVER))
		return ERR_PTR(-EINVAL);

	OBD_SLAB_ALLOC_PTR(ocapa, capa_cachep);
	if (unlikely(!ocapa))
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&ocapa->c_list);
	atomic_set(&ocapa->c_refc, 1);
	spin_lock_init(&ocapa->c_lock);
	ocapa->c_site = site;
	if (ocapa->c_site == CAPA_SITE_CLIENT)
		INIT_LIST_HEAD(&ocapa->u.cli.lli_list);
	else
		INIT_HLIST_NODE(&ocapa->u.tgt.c_hash);

	return ocapa;
}

static inline struct obd_capa *capa_get(struct obd_capa *ocapa)
{
	if (!ocapa)
		return NULL;

	atomic_inc(&ocapa->c_refc);
	return ocapa;
}

static inline void capa_put(struct obd_capa *ocapa)
{
	if (!ocapa)
		return;

	if (atomic_read(&ocapa->c_refc) == 0) {
		DEBUG_CAPA(D_ERROR, &ocapa->c_capa, "refc is 0 for");
		LBUG();
	}

	if (atomic_dec_and_test(&ocapa->c_refc)) {
		LASSERT(list_empty(&ocapa->c_list));
		if (ocapa->c_site == CAPA_SITE_CLIENT) {
			LASSERT(list_empty(&ocapa->u.cli.lli_list));
		} else {
			struct hlist_node *hnode;

			hnode = &ocapa->u.tgt.c_hash;
			LASSERT(!hnode->next && !hnode->pprev);
		}
		OBD_SLAB_FREE(ocapa, capa_cachep, sizeof(*ocapa));
	}
}

static inline int open_flags_to_accmode(int flags)
{
	int mode = flags;

	if ((mode + 1) & O_ACCMODE)
		mode++;
	if (mode & O_TRUNC)
		mode |= 2;

	return mode;
}

static inline __u64 capa_open_opc(int mode)
{
	return mode & FMODE_WRITE ? CAPA_OPC_OSS_WRITE : CAPA_OPC_OSS_READ;
}

static inline void set_capa_expiry(struct obd_capa *ocapa)
{
	unsigned long expiry = cfs_time_sub((unsigned long)ocapa->c_capa.lc_expiry,
					 get_seconds());
	ocapa->c_expiry = cfs_time_add(cfs_time_current(),
				       cfs_time_seconds(expiry));
}

static inline int capa_is_expired_sec(struct lustre_capa *capa)
{
	return (capa->lc_expiry - get_seconds() <= 0);
}

static inline int capa_is_expired(struct obd_capa *ocapa)
{
	return time_before_eq(ocapa->c_expiry, cfs_time_current());
}

static inline int capa_opc_supported(struct lustre_capa *capa, __u64 opc)
{
	return (capa_opc(capa) & opc) == opc;
}

struct filter_capa_key {
	struct list_head	      k_list;
	struct lustre_capa_key  k_key;
};

enum {
	LC_ID_NONE      = 0,
	LC_ID_PLAIN     = 1,
	LC_ID_CONVERT   = 2
};

#define BYPASS_CAPA (struct lustre_capa *)ERR_PTR(-ENOENT)

/** @} capa */

#endif /* __LINUX_CAPA_H_ */
