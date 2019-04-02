/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHE_DE_H
#define _BCACHE_DE_H

struct bio;
struct cached_dev;
struct cache_set;

#ifdef CONFIG_BCACHE_DE

void bch_btree_verify(struct btree *b);
void bch_data_verify(struct cached_dev *dc, struct bio *bio);

#define expensive_de_checks(c)	((c)->expensive_de_checks)
#define key_merging_disabled(c)		((c)->key_merging_disabled)
#define bypass_torture_test(d)		((d)->bypass_torture_test)

#else /* DE */

static inline void bch_btree_verify(struct btree *b) {}
static inline void bch_data_verify(struct cached_dev *dc, struct bio *bio) {}

#define expensive_de_checks(c)	0
#define key_merging_disabled(c)		0
#define bypass_torture_test(d)		0

#endif

#ifdef CONFIG_DE_FS
void bch_de_init_cache_set(struct cache_set *c);
#else
static inline void bch_de_init_cache_set(struct cache_set *c) {}
#endif

#endif
