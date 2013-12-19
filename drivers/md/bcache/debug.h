#ifndef _BCACHE_DEBUG_H
#define _BCACHE_DEBUG_H

/* Btree/bkey debug printing */

int bch_bkey_to_text(char *buf, size_t size, const struct bkey *k);

#ifdef CONFIG_BCACHE_DEBUG

void bch_btree_verify(struct btree *, struct bset *);
void bch_data_verify(struct cached_dev *, struct bio *);
int __bch_count_data(struct btree *);
void __bch_check_keys(struct btree *, const char *, ...);
void bch_btree_iter_next_check(struct btree_iter *);

#define EBUG_ON(cond)			BUG_ON(cond)
#define expensive_debug_checks(c)	((c)->expensive_debug_checks)
#define key_merging_disabled(c)		((c)->key_merging_disabled)
#define bypass_torture_test(d)		((d)->bypass_torture_test)

#else /* DEBUG */

static inline void bch_btree_verify(struct btree *b, struct bset *i) {}
static inline void bch_data_verify(struct cached_dev *dc, struct bio *bio) {}
static inline int __bch_count_data(struct btree *b) { return -1; }
static inline void __bch_check_keys(struct btree *b, const char *fmt, ...) {}
static inline void bch_btree_iter_next_check(struct btree_iter *iter) {}

#define EBUG_ON(cond)			do { if (cond); } while (0)
#define expensive_debug_checks(c)	0
#define key_merging_disabled(c)		0
#define bypass_torture_test(d)		0

#endif

#define bch_count_data(b)						\
	(expensive_debug_checks((b)->c) ? __bch_count_data(b) : -1)

#define bch_check_keys(b, ...)						\
do {									\
	if (expensive_debug_checks((b)->c))				\
		__bch_check_keys(b, __VA_ARGS__);			\
} while (0)

#ifdef CONFIG_DEBUG_FS
void bch_debug_init_cache_set(struct cache_set *);
#else
static inline void bch_debug_init_cache_set(struct cache_set *c) {}
#endif

#endif
