#ifndef _BCACHE_EXTENTS_H
#define _BCACHE_EXTENTS_H

extern const struct btree_keys_ops bch_btree_keys_ops;
extern const struct btree_keys_ops bch_extent_keys_ops;

struct bkey;
struct cache_set;

void bch_extent_to_text(char *, size_t, const struct bkey *);
bool __bch_btree_ptr_invalid(struct cache_set *, const struct bkey *);
bool __bch_extent_invalid(struct cache_set *, const struct bkey *);

#endif /* _BCACHE_EXTENTS_H */
