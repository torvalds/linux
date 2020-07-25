/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _BCACHE_FEATURES_H
#define _BCACHE_FEATURES_H

#include <linux/bcache.h>
#include <linux/kernel.h>
#include <linux/types.h>

#define BCH_FEATURE_COMPAT		0
#define BCH_FEATURE_RO_COMPAT		1
#define BCH_FEATURE_INCOMPAT		2
#define BCH_FEATURE_TYPE_MASK		0x03

#define BCH_FEATURE_COMPAT_SUUP		0
#define BCH_FEATURE_RO_COMPAT_SUUP	0
#define BCH_FEATURE_INCOMPAT_SUUP	0

#define BCH_HAS_COMPAT_FEATURE(sb, mask) \
		((sb)->feature_compat & (mask))
#define BCH_HAS_RO_COMPAT_FEATURE(sb, mask) \
		((sb)->feature_ro_compat & (mask))
#define BCH_HAS_INCOMPAT_FEATURE(sb, mask) \
		((sb)->feature_incompat & (mask))

/* Feature set definition */

#define BCH_FEATURE_COMPAT_FUNCS(name, flagname) \
static inline int bch_has_feature_##name(struct cache_sb *sb) \
{ \
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

#endif
