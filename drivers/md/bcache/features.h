/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _BCACHE_FEATURES_H
#define _BCACHE_FEATURES_H

#include <linux/kernel.h>
#include <linux/types.h>

#include "bcache_ondisk.h"

#define BCH_FEATURE_COMPAT		0
#define BCH_FEATURE_RO_COMPAT		1
#define BCH_FEATURE_INCOMPAT		2
#define BCH_FEATURE_TYPE_MASK		0x03

/* Feature set definition */
/* Incompat feature set */
/* 32bit bucket size, obsoleted */
#define BCH_FEATURE_INCOMPAT_OBSO_LARGE_BUCKET		0x0001
/* real bucket size is (1 << bucket_size) */
#define BCH_FEATURE_INCOMPAT_LOG_LARGE_BUCKET_SIZE	0x0002

#define BCH_FEATURE_COMPAT_SUPP		0
#define BCH_FEATURE_RO_COMPAT_SUPP	0
#define BCH_FEATURE_INCOMPAT_SUPP	(BCH_FEATURE_INCOMPAT_OBSO_LARGE_BUCKET| \
					 BCH_FEATURE_INCOMPAT_LOG_LARGE_BUCKET_SIZE)

#define BCH_HAS_COMPAT_FEATURE(sb, mask) \
		((sb)->feature_compat & (mask))
#define BCH_HAS_RO_COMPAT_FEATURE(sb, mask) \
		((sb)->feature_ro_compat & (mask))
#define BCH_HAS_INCOMPAT_FEATURE(sb, mask) \
		((sb)->feature_incompat & (mask))

#define BCH_FEATURE_COMPAT_FUNCS(name, flagname) \
static inline int bch_has_feature_##name(struct cache_sb *sb) \
{ \
	if (sb->version < BCACHE_SB_VERSION_CDEV_WITH_FEATURES) \
		return 0; \
	return (((sb)->feature_compat & \
		BCH##_FEATURE_COMPAT_##flagname) != 0); \
} \
static inline void bch_set_feature_##name(struct cache_sb *sb) \
{ \
	(sb)->feature_compat |= \
		BCH##_FEATURE_COMPAT_##flagname; \
} \
static inline void bch_clear_feature_##name(struct cache_sb *sb) \
{ \
	(sb)->feature_compat &= \
		~BCH##_FEATURE_COMPAT_##flagname; \
}

#define BCH_FEATURE_RO_COMPAT_FUNCS(name, flagname) \
static inline int bch_has_feature_##name(struct cache_sb *sb) \
{ \
	if (sb->version < BCACHE_SB_VERSION_CDEV_WITH_FEATURES) \
		return 0; \
	return (((sb)->feature_ro_compat & \
		BCH##_FEATURE_RO_COMPAT_##flagname) != 0); \
} \
static inline void bch_set_feature_##name(struct cache_sb *sb) \
{ \
	(sb)->feature_ro_compat |= \
		BCH##_FEATURE_RO_COMPAT_##flagname; \
} \
static inline void bch_clear_feature_##name(struct cache_sb *sb) \
{ \
	(sb)->feature_ro_compat &= \
		~BCH##_FEATURE_RO_COMPAT_##flagname; \
}

#define BCH_FEATURE_INCOMPAT_FUNCS(name, flagname) \
static inline int bch_has_feature_##name(struct cache_sb *sb) \
{ \
	if (sb->version < BCACHE_SB_VERSION_CDEV_WITH_FEATURES) \
		return 0; \
	return (((sb)->feature_incompat & \
		BCH##_FEATURE_INCOMPAT_##flagname) != 0); \
} \
static inline void bch_set_feature_##name(struct cache_sb *sb) \
{ \
	(sb)->feature_incompat |= \
		BCH##_FEATURE_INCOMPAT_##flagname; \
} \
static inline void bch_clear_feature_##name(struct cache_sb *sb) \
{ \
	(sb)->feature_incompat &= \
		~BCH##_FEATURE_INCOMPAT_##flagname; \
}

BCH_FEATURE_INCOMPAT_FUNCS(obso_large_bucket, OBSO_LARGE_BUCKET);
BCH_FEATURE_INCOMPAT_FUNCS(large_bucket, LOG_LARGE_BUCKET_SIZE);

static inline bool bch_has_unknown_compat_features(struct cache_sb *sb)
{
	return ((sb->feature_compat & ~BCH_FEATURE_COMPAT_SUPP) != 0);
}

static inline bool bch_has_unknown_ro_compat_features(struct cache_sb *sb)
{
	return ((sb->feature_ro_compat & ~BCH_FEATURE_RO_COMPAT_SUPP) != 0);
}

static inline bool bch_has_unknown_incompat_features(struct cache_sb *sb)
{
	return ((sb->feature_incompat & ~BCH_FEATURE_INCOMPAT_SUPP) != 0);
}

int bch_print_cache_set_feature_compat(struct cache_set *c, char *buf, int size);
int bch_print_cache_set_feature_ro_compat(struct cache_set *c, char *buf, int size);
int bch_print_cache_set_feature_incompat(struct cache_set *c, char *buf, int size);

#endif
