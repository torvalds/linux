/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _LINUX_CHASH_H
#define _LINUX_CHASH_H

#include <linux/types.h>
#include <linux/hash.h>
#include <linux/bug.h>
#include <asm/bitsperlong.h>

#if BITS_PER_LONG == 32
# define _CHASH_LONG_SHIFT 5
#elif BITS_PER_LONG == 64
# define _CHASH_LONG_SHIFT 6
#else
# error "Unexpected BITS_PER_LONG"
#endif

struct __chash_table {
	u8 bits;
	u8 key_size;
	unsigned int value_size;
	u32 size_mask;
	unsigned long *occup_bitmap, *valid_bitmap;
	union {
		u32 *keys32;
		u64 *keys64;
	};
	u8 *values;

#ifdef CONFIG_CHASH_STATS
	u64 hits, hits_steps, hits_time_ns;
	u64 miss, miss_steps, miss_time_ns;
	u64 relocs, reloc_dist;
#endif
};

#define __CHASH_BITMAP_SIZE(bits)				\
	(((1 << (bits)) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define __CHASH_ARRAY_SIZE(bits, size)				\
	((((size) << (bits)) + sizeof(long) - 1) / sizeof(long))

#define __CHASH_DATA_SIZE(bits, key_size, value_size)	\
	(__CHASH_BITMAP_SIZE(bits) * 2 +		\
	 __CHASH_ARRAY_SIZE(bits, key_size) +		\
	 __CHASH_ARRAY_SIZE(bits, value_size))

#define STRUCT_CHASH_TABLE(bits, key_size, value_size)			\
	struct {							\
		struct __chash_table table;				\
		unsigned long data					\
			[__CHASH_DATA_SIZE(bits, key_size, value_size)];\
	}

/**
 * struct chash_table - Dynamically allocated closed hash table
 *
 * Use this struct for dynamically allocated hash tables (using
 * chash_table_alloc and chash_table_free), where the size is
 * determined at runtime.
 */
struct chash_table {
	struct __chash_table table;
	unsigned long *data;
};

/**
 * DECLARE_CHASH_TABLE - macro to declare a closed hash table
 * @table: name of the declared hash table
 * @bts: Table size will be 2^bits entries
 * @key_sz: Size of hash keys in bytes, 4 or 8
 * @val_sz: Size of data values in bytes, can be 0
 *
 * This declares the hash table variable with a static size.
 *
 * The closed hash table stores key-value pairs with low memory and
 * lookup overhead. In operation it performs no dynamic memory
 * management. The data being stored does not require any
 * list_heads. The hash table performs best with small @val_sz and as
 * long as some space (about 50%) is left free in the table. But the
 * table can still work reasonably efficiently even when filled up to
 * about 90%. If bigger data items need to be stored and looked up,
 * store the pointer to it as value in the hash table.
 *
 * @val_sz may be 0. This can be useful when all the stored
 * information is contained in the key itself and the fact that it is
 * in the hash table (or not).
 */
#define DECLARE_CHASH_TABLE(table, bts, key_sz, val_sz)		\
	STRUCT_CHASH_TABLE(bts, key_sz, val_sz) table

#ifdef CONFIG_CHASH_STATS
#define __CHASH_STATS_INIT(prefix),		\
		prefix.hits = 0,		\
		prefix.hits_steps = 0,		\
		prefix.hits_time_ns = 0,	\
		prefix.miss = 0,		\
		prefix.miss_steps = 0,		\
		prefix.miss_time_ns = 0,	\
		prefix.relocs = 0,		\
		prefix.reloc_dist = 0
#else
#define __CHASH_STATS_INIT(prefix)
#endif

#define __CHASH_TABLE_INIT(prefix, data, bts, key_sz, val_sz)	\
	prefix.bits = (bts),					\
		prefix.key_size = (key_sz),			\
		prefix.value_size = (val_sz),			\
		prefix.size_mask = ((1 << bts) - 1),		\
		prefix.occup_bitmap = &data[0],			\
		prefix.valid_bitmap = &data			\
			[__CHASH_BITMAP_SIZE(bts)],		\
		prefix.keys64 = (u64 *)&data			\
			[__CHASH_BITMAP_SIZE(bts) * 2],		\
		prefix.values = (u8 *)&data			\
			[__CHASH_BITMAP_SIZE(bts) * 2 +		\
			 __CHASH_ARRAY_SIZE(bts, key_sz)]	\
		__CHASH_STATS_INIT(prefix)

/**
 * DEFINE_CHASH_TABLE - macro to define and initialize a closed hash table
 * @tbl: name of the declared hash table
 * @bts: Table size will be 2^bits entries
 * @key_sz: Size of hash keys in bytes, 4 or 8
 * @val_sz: Size of data values in bytes, can be 0
 *
 * Note: the macro can be used for global and local hash table variables.
 */
#define DEFINE_CHASH_TABLE(tbl, bts, key_sz, val_sz)			\
	DECLARE_CHASH_TABLE(tbl, bts, key_sz, val_sz) = {		\
		.table = {						\
			__CHASH_TABLE_INIT(, (tbl).data, bts, key_sz, val_sz) \
		},							\
		.data = {0}						\
	}

/**
 * INIT_CHASH_TABLE - Initialize a hash table declared by DECLARE_CHASH_TABLE
 * @tbl: name of the declared hash table
 * @bts: Table size will be 2^bits entries
 * @key_sz: Size of hash keys in bytes, 4 or 8
 * @val_sz: Size of data values in bytes, can be 0
 */
#define INIT_CHASH_TABLE(tbl, bts, key_sz, val_sz)			\
	__CHASH_TABLE_INIT(((tbl).table), (tbl).data, bts, key_sz, val_sz)

int chash_table_alloc(struct chash_table *table, u8 bits, u8 key_size,
		      unsigned int value_size, gfp_t gfp_mask);
void chash_table_free(struct chash_table *table);

/**
 * chash_table_dump_stats - Dump statistics of a closed hash table
 * @tbl: Pointer to the table structure
 *
 * Dumps some performance statistics of the table gathered in operation
 * in the kernel log using pr_debug. If CONFIG_DYNAMIC_DEBUG is enabled,
 * user must turn on messages for chash.c (file chash.c +p).
 */
#ifdef CONFIG_CHASH_STATS
#define chash_table_dump_stats(tbl) __chash_table_dump_stats(&(*tbl).table)

void __chash_table_dump_stats(struct __chash_table *table);
#else
#define chash_table_dump_stats(tbl)
#endif

/**
 * chash_table_reset_stats - Reset statistics of a closed hash table
 * @tbl: Pointer to the table structure
 */
#ifdef CONFIG_CHASH_STATS
#define chash_table_reset_stats(tbl) __chash_table_reset_stats(&(*tbl).table)

static inline void __chash_table_reset_stats(struct __chash_table *table)
{
	(void)table __CHASH_STATS_INIT((*table));
}
#else
#define chash_table_reset_stats(tbl)
#endif

/**
 * chash_table_copy_in - Copy a new value into the hash table
 * @tbl: Pointer to the table structure
 * @key: Key of the entry to add or update
 * @value: Pointer to value to copy, may be NULL
 *
 * If @key already has an entry, its value is replaced. Otherwise a
 * new entry is added. If @value is NULL, the value is left unchanged
 * or uninitialized. Returns 1 if an entry already existed, 0 if a new
 * entry was added or %-ENOMEM if there was no free space in the
 * table.
 */
#define chash_table_copy_in(tbl, key, value)			\
	__chash_table_copy_in(&(*tbl).table, key, value)

int __chash_table_copy_in(struct __chash_table *table, u64 key,
			  const void *value);

/**
 * chash_table_copy_out - Copy a value out of the hash table
 * @tbl: Pointer to the table structure
 * @key: Key of the entry to find
 * @value: Pointer to value to copy, may be NULL
 *
 * If @value is not NULL and the table has a non-0 value_size, the
 * value at @key is copied to @value. Returns the slot index of the
 * entry or %-EINVAL if @key was not found.
 */
#define chash_table_copy_out(tbl, key, value)			\
	__chash_table_copy_out(&(*tbl).table, key, value, false)

int __chash_table_copy_out(struct __chash_table *table, u64 key,
			   void *value, bool remove);

/**
 * chash_table_remove - Remove an entry from the hash table
 * @tbl: Pointer to the table structure
 * @key: Key of the entry to find
 * @value: Pointer to value to copy, may be NULL
 *
 * If @value is not NULL and the table has a non-0 value_size, the
 * value at @key is copied to @value. The entry is removed from the
 * table. Returns the slot index of the removed entry or %-EINVAL if
 * @key was not found.
 */
#define chash_table_remove(tbl, key, value)			\
	__chash_table_copy_out(&(*tbl).table, key, value, true)

/*
 * Low level iterator API used internally by the above functions.
 */
struct chash_iter {
	struct __chash_table *table;
	unsigned long mask;
	int slot;
};

/**
 * CHASH_ITER_INIT - Initialize a hash table iterator
 * @tbl: Pointer to hash table to iterate over
 * @s: Initial slot number
 */
#define CHASH_ITER_INIT(table, s) {			\
		table,					\
		1UL << ((s) & (BITS_PER_LONG - 1)),	\
		s					\
	}
/**
 * CHASH_ITER_SET - Set hash table iterator to new slot
 * @iter: Iterator
 * @s: Slot number
 */
#define CHASH_ITER_SET(iter, s)					\
	(iter).mask = 1UL << ((s) & (BITS_PER_LONG - 1)),	\
	(iter).slot = (s)
/**
 * CHASH_ITER_INC - Increment hash table iterator
 * @table: Hash table to iterate over
 *
 * Wraps around at the end.
 */
#define CHASH_ITER_INC(iter) do {					\
		(iter).mask = (iter).mask << 1 |			\
			(iter).mask >> (BITS_PER_LONG - 1);		\
		(iter).slot = ((iter).slot + 1) & (iter).table->size_mask; \
	} while (0)

static inline bool chash_iter_is_valid(const struct chash_iter iter)
{
	BUG_ON((unsigned)iter.slot >= (1 << iter.table->bits));
	return !!(iter.table->valid_bitmap[iter.slot >> _CHASH_LONG_SHIFT] &
		  iter.mask);
}
static inline bool chash_iter_is_empty(const struct chash_iter iter)
{
	BUG_ON((unsigned)iter.slot >= (1 << iter.table->bits));
	return !(iter.table->occup_bitmap[iter.slot >> _CHASH_LONG_SHIFT] &
		 iter.mask);
}

static inline void chash_iter_set_valid(const struct chash_iter iter)
{
	BUG_ON((unsigned)iter.slot >= (1 << iter.table->bits));
	iter.table->valid_bitmap[iter.slot >> _CHASH_LONG_SHIFT] |= iter.mask;
	iter.table->occup_bitmap[iter.slot >> _CHASH_LONG_SHIFT] |= iter.mask;
}
static inline void chash_iter_set_invalid(const struct chash_iter iter)
{
	BUG_ON((unsigned)iter.slot >= (1 << iter.table->bits));
	iter.table->valid_bitmap[iter.slot >> _CHASH_LONG_SHIFT] &= ~iter.mask;
}
static inline void chash_iter_set_empty(const struct chash_iter iter)
{
	BUG_ON((unsigned)iter.slot >= (1 << iter.table->bits));
	iter.table->occup_bitmap[iter.slot >> _CHASH_LONG_SHIFT] &= ~iter.mask;
}

static inline u32 chash_iter_key32(const struct chash_iter iter)
{
	BUG_ON(iter.table->key_size != 4);
	BUG_ON((unsigned)iter.slot >= (1 << iter.table->bits));
	return iter.table->keys32[iter.slot];
}
static inline u64 chash_iter_key64(const struct chash_iter iter)
{
	BUG_ON(iter.table->key_size != 8);
	BUG_ON((unsigned)iter.slot >= (1 << iter.table->bits));
	return iter.table->keys64[iter.slot];
}
static inline u64 chash_iter_key(const struct chash_iter iter)
{
	BUG_ON((unsigned)iter.slot >= (1 << iter.table->bits));
	return (iter.table->key_size == 4) ?
		iter.table->keys32[iter.slot] : iter.table->keys64[iter.slot];
}

static inline u32 chash_iter_hash32(const struct chash_iter iter)
{
	BUG_ON(iter.table->key_size != 4);
	return hash_32(chash_iter_key32(iter), iter.table->bits);
}

static inline u32 chash_iter_hash64(const struct chash_iter iter)
{
	BUG_ON(iter.table->key_size != 8);
	return hash_64(chash_iter_key64(iter), iter.table->bits);
}

static inline u32 chash_iter_hash(const struct chash_iter iter)
{
	return (iter.table->key_size == 4) ?
		hash_32(chash_iter_key32(iter), iter.table->bits) :
		hash_64(chash_iter_key64(iter), iter.table->bits);
}

static inline void *chash_iter_value(const struct chash_iter iter)
{
	BUG_ON((unsigned)iter.slot >= (1 << iter.table->bits));
	return iter.table->values +
		((unsigned long)iter.slot * iter.table->value_size);
}

#endif /* _LINUX_CHASH_H */
