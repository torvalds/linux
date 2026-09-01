/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright 2026 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef _EFA_AH_CACHE_H_
#define _EFA_AH_CACHE_H_

#include <linux/refcount.h>
#include <linux/rhashtable.h>

#define EFA_AH_GID_SIZE 16

struct efa_ah_cache_key {
	u8 gid[EFA_AH_GID_SIZE];
	u16 pd;
};

struct efa_ah_cache_entry {
	struct efa_ah_cache_key key;
	u16 ah;
	unsigned int usecnt;
	refcount_t refcount;
	struct rhash_head linkage;
	struct mutex lock; /* Serializes device commands per cache entry */
};

struct efa_ah_cache {
	struct rhashtable hashtable;
	struct mutex lock; /* Protects AH cache hashtable */
};

int efa_ah_cache_init(struct efa_ah_cache *ah_cache);
void efa_ah_cache_destroy(struct efa_ah_cache *ah_cache);
struct efa_ah_cache_entry *efa_ah_cache_get(struct efa_ah_cache *ah_cache, u16 pd, u8 *gid);
struct efa_ah_cache_entry *efa_ah_cache_lookup(struct efa_ah_cache *ah_cache, u16 pd, u8 *gid);
void efa_ah_cache_put(struct efa_ah_cache *ah_cache, struct efa_ah_cache_entry *entry);

#endif /* _EFA_AH_CACHE_H_ */
