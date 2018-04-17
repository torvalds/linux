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

#include <linux/types.h>
#include <linux/hash.h>
#include <linux/bug.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/sched/clock.h>
#include <asm/div64.h>
#include <linux/chash.h>

/**
 * chash_table_alloc - Allocate closed hash table
 * @table: Pointer to the table structure
 * @bits: Table size will be 2^bits entries
 * @key_size: Size of hash keys in bytes, 4 or 8
 * @value_size: Size of data values in bytes, can be 0
 */
int chash_table_alloc(struct chash_table *table, u8 bits, u8 key_size,
		      unsigned int value_size, gfp_t gfp_mask)
{
	if (bits > 31)
		return -EINVAL;

	if (key_size != 4 && key_size != 8)
		return -EINVAL;

	table->data = kcalloc(__CHASH_DATA_SIZE(bits, key_size, value_size),
		       sizeof(long), gfp_mask);
	if (!table->data)
		return -ENOMEM;

	__CHASH_TABLE_INIT(table->table, table->data,
			   bits, key_size, value_size);

	return 0;
}
EXPORT_SYMBOL(chash_table_alloc);

/**
 * chash_table_free - Free closed hash table
 * @table: Pointer to the table structure
 */
void chash_table_free(struct chash_table *table)
{
	kfree(table->data);
}
EXPORT_SYMBOL(chash_table_free);

#ifdef CONFIG_CHASH_STATS

#define DIV_FRAC(nom, denom, quot, frac, frac_digits) do {		\
		u64 __nom = (nom);					\
		u64 __denom = (denom);					\
		u64 __quot, __frac;					\
		u32 __rem;						\
									\
		while (__denom >> 32) {					\
			__nom   >>= 1;					\
			__denom >>= 1;					\
		}							\
		__quot = __nom;						\
		__rem  = do_div(__quot, __denom);			\
		__frac = __rem * (frac_digits) + (__denom >> 1);	\
		do_div(__frac, __denom);				\
		(quot) = __quot;					\
		(frac) = __frac;					\
	} while (0)

void __chash_table_dump_stats(struct __chash_table *table)
{
	struct chash_iter iter = CHASH_ITER_INIT(table, 0);
	u32 filled = 0, empty = 0, tombstones = 0;
	u64 quot1, quot2;
	u32 frac1, frac2;

	do {
		if (chash_iter_is_valid(iter))
			filled++;
		else if (chash_iter_is_empty(iter))
			empty++;
		else
			tombstones++;
		CHASH_ITER_INC(iter);
	} while (iter.slot);

	pr_debug("chash: key size %u, value size %u\n",
		 table->key_size, table->value_size);
	pr_debug("  Slots total/filled/empty/tombstones: %u / %u / %u / %u\n",
		 1 << table->bits, filled, empty, tombstones);
	if (table->hits > 0) {
		DIV_FRAC(table->hits_steps, table->hits, quot1, frac1, 1000);
		DIV_FRAC(table->hits * 1000, table->hits_time_ns,
			 quot2, frac2, 1000);
	} else {
		quot1 = quot2 = 0;
		frac1 = frac2 = 0;
	}
	pr_debug("  Hits   (avg.cost, rate): %llu (%llu.%03u, %llu.%03u M/s)\n",
		 table->hits, quot1, frac1, quot2, frac2);
	if (table->miss > 0) {
		DIV_FRAC(table->miss_steps, table->miss, quot1, frac1, 1000);
		DIV_FRAC(table->miss * 1000, table->miss_time_ns,
			 quot2, frac2, 1000);
	} else {
		quot1 = quot2 = 0;
		frac1 = frac2 = 0;
	}
	pr_debug("  Misses (avg.cost, rate): %llu (%llu.%03u, %llu.%03u M/s)\n",
		 table->miss, quot1, frac1, quot2, frac2);
	if (table->hits + table->miss > 0) {
		DIV_FRAC(table->hits_steps + table->miss_steps,
			 table->hits + table->miss, quot1, frac1, 1000);
		DIV_FRAC((table->hits + table->miss) * 1000,
			 (table->hits_time_ns + table->miss_time_ns),
			 quot2, frac2, 1000);
	} else {
		quot1 = quot2 = 0;
		frac1 = frac2 = 0;
	}
	pr_debug("  Total  (avg.cost, rate): %llu (%llu.%03u, %llu.%03u M/s)\n",
		 table->hits + table->miss, quot1, frac1, quot2, frac2);
	if (table->relocs > 0) {
		DIV_FRAC(table->hits + table->miss, table->relocs,
			 quot1, frac1, 1000);
		DIV_FRAC(table->reloc_dist, table->relocs, quot2, frac2, 1000);
		pr_debug("  Relocations (freq, avg.dist): %llu (1:%llu.%03u, %llu.%03u)\n",
			 table->relocs, quot1, frac1, quot2, frac2);
	} else {
		pr_debug("  No relocations\n");
	}
}
EXPORT_SYMBOL(__chash_table_dump_stats);

#undef DIV_FRAC
#endif

#define CHASH_INC(table, a) ((a) = ((a) + 1) & (table)->size_mask)
#define CHASH_ADD(table, a, b) (((a) + (b)) & (table)->size_mask)
#define CHASH_SUB(table, a, b) (((a) - (b)) & (table)->size_mask)
#define CHASH_IN_RANGE(table, slot, first, last) \
	(CHASH_SUB(table, slot, first) <= CHASH_SUB(table, last, first))

/*#define CHASH_DEBUG Uncomment this to enable verbose debug output*/
#ifdef CHASH_DEBUG
static void chash_table_dump(struct __chash_table *table)
{
	struct chash_iter iter = CHASH_ITER_INIT(table, 0);

	do {
		if ((iter.slot & 3) == 0)
			pr_debug("%04x: ", iter.slot);

		if (chash_iter_is_valid(iter))
			pr_debug("[%016llx] ", chash_iter_key(iter));
		else if (chash_iter_is_empty(iter))
			pr_debug("[    <empty>     ] ");
		else
			pr_debug("[  <tombstone>   ] ");

		if ((iter.slot & 3) == 3)
			pr_debug("\n");

		CHASH_ITER_INC(iter);
	} while (iter.slot);

	if ((iter.slot & 3) != 0)
		pr_debug("\n");
}

static int chash_table_check(struct __chash_table *table)
{
	u32 hash;
	struct chash_iter iter = CHASH_ITER_INIT(table, 0);
	struct chash_iter cur = CHASH_ITER_INIT(table, 0);

	do {
		if (!chash_iter_is_valid(iter)) {
			CHASH_ITER_INC(iter);
			continue;
		}

		hash = chash_iter_hash(iter);
		CHASH_ITER_SET(cur, hash);
		while (cur.slot != iter.slot) {
			if (chash_iter_is_empty(cur)) {
				pr_err("Path to element at %x with hash %x broken at slot %x\n",
				       iter.slot, hash, cur.slot);
				chash_table_dump(table);
				return -EINVAL;
			}
			CHASH_ITER_INC(cur);
		}

		CHASH_ITER_INC(iter);
	} while (iter.slot);

	return 0;
}
#endif

static void chash_iter_relocate(struct chash_iter dst, struct chash_iter src)
{
	BUG_ON(src.table == dst.table && src.slot == dst.slot);
	BUG_ON(src.table->key_size != dst.table->key_size);
	BUG_ON(src.table->value_size != dst.table->value_size);

	if (dst.table->key_size == 4)
		dst.table->keys32[dst.slot] = src.table->keys32[src.slot];
	else
		dst.table->keys64[dst.slot] = src.table->keys64[src.slot];

	if (dst.table->value_size)
		memcpy(chash_iter_value(dst), chash_iter_value(src),
		       dst.table->value_size);

	chash_iter_set_valid(dst);
	chash_iter_set_invalid(src);

#ifdef CONFIG_CHASH_STATS
	if (src.table == dst.table) {
		dst.table->relocs++;
		dst.table->reloc_dist +=
			CHASH_SUB(dst.table, src.slot, dst.slot);
	}
#endif
}

/**
 * __chash_table_find - Helper for looking up a hash table entry
 * @iter: Pointer to hash table iterator
 * @key: Key of the entry to find
 * @for_removal: set to true if the element will be removed soon
 *
 * Searches for an entry in the hash table with a given key. iter must
 * be initialized by the caller to point to the home position of the
 * hypothetical entry, i.e. it must be initialized with the hash table
 * and the key's hash as the initial slot for the search.
 *
 * This function also does some local clean-up to speed up future
 * look-ups by relocating entries to better slots and removing
 * tombstones that are no longer needed.
 *
 * If @for_removal is true, the function avoids relocating the entry
 * that is being returned.
 *
 * Returns 0 if the search is successful. In this case iter is updated
 * to point to the found entry. Otherwise %-EINVAL is returned and the
 * iter is updated to point to the first available slot for the given
 * key. If the table is full, the slot is set to -1.
 */
static int chash_table_find(struct chash_iter *iter, u64 key,
			    bool for_removal)
{
#ifdef CONFIG_CHASH_STATS
	u64 ts1 = local_clock();
#endif
	u32 hash = iter->slot;
	struct chash_iter first_redundant = CHASH_ITER_INIT(iter->table, -1);
	int first_avail = (for_removal ? -2 : -1);

	while (!chash_iter_is_valid(*iter) || chash_iter_key(*iter) != key) {
		if (chash_iter_is_empty(*iter)) {
			/* Found an empty slot, which ends the
			 * search. Clean up any preceding tombstones
			 * that are no longer needed because they lead
			 * to no-where
			 */
			if ((int)first_redundant.slot < 0)
				goto not_found;
			while (first_redundant.slot != iter->slot) {
				if (!chash_iter_is_valid(first_redundant))
					chash_iter_set_empty(first_redundant);
				CHASH_ITER_INC(first_redundant);
			}
#ifdef CHASH_DEBUG
			chash_table_check(iter->table);
#endif
			goto not_found;
		} else if (!chash_iter_is_valid(*iter)) {
			/* Found a tombstone. Remember it as candidate
			 * for relocating the entry we're looking for
			 * or for adding a new entry with the given key
			 */
			if (first_avail == -1)
				first_avail = iter->slot;
			/* Or mark it as the start of a series of
			 * potentially redundant tombstones
			 */
			else if (first_redundant.slot == -1)
				CHASH_ITER_SET(first_redundant, iter->slot);
		} else if (first_redundant.slot >= 0) {
			/* Found a valid, occupied slot with a
			 * preceding series of tombstones. Relocate it
			 * to a better position that no longer depends
			 * on those tombstones
			 */
			u32 cur_hash = chash_iter_hash(*iter);

			if (!CHASH_IN_RANGE(iter->table, cur_hash,
					    first_redundant.slot + 1,
					    iter->slot)) {
				/* This entry has a hash at or before
				 * the first tombstone we found. We
				 * can relocate it to that tombstone
				 * and advance to the next tombstone
				 */
				chash_iter_relocate(first_redundant, *iter);
				do {
					CHASH_ITER_INC(first_redundant);
				} while (chash_iter_is_valid(first_redundant));
			} else if (cur_hash != iter->slot) {
				/* Relocate entry to its home position
				 * or as close as possible so it no
				 * longer depends on any preceding
				 * tombstones
				 */
				struct chash_iter new_iter =
					CHASH_ITER_INIT(iter->table, cur_hash);

				while (new_iter.slot != iter->slot &&
				       chash_iter_is_valid(new_iter))
					CHASH_ITER_INC(new_iter);

				if (new_iter.slot != iter->slot)
					chash_iter_relocate(new_iter, *iter);
			}
		}

		CHASH_ITER_INC(*iter);
		if (iter->slot == hash) {
			iter->slot = -1;
			goto not_found;
		}
	}

#ifdef CONFIG_CHASH_STATS
	iter->table->hits++;
	iter->table->hits_steps += CHASH_SUB(iter->table, iter->slot, hash) + 1;
#endif

	if (first_avail >= 0) {
		CHASH_ITER_SET(first_redundant, first_avail);
		chash_iter_relocate(first_redundant, *iter);
		iter->slot = first_redundant.slot;
		iter->mask = first_redundant.mask;
	}

#ifdef CONFIG_CHASH_STATS
	iter->table->hits_time_ns += local_clock() - ts1;
#endif

	return 0;

not_found:
#ifdef CONFIG_CHASH_STATS
	iter->table->miss++;
	iter->table->miss_steps += (iter->slot < 0) ?
		(1 << iter->table->bits) :
		CHASH_SUB(iter->table, iter->slot, hash) + 1;
#endif

	if (first_avail >= 0)
		CHASH_ITER_SET(*iter, first_avail);

#ifdef CONFIG_CHASH_STATS
	iter->table->miss_time_ns += local_clock() - ts1;
#endif

	return -EINVAL;
}

int __chash_table_copy_in(struct __chash_table *table, u64 key,
			  const void *value)
{
	u32 hash = (table->key_size == 4) ?
		hash_32(key, table->bits) : hash_64(key, table->bits);
	struct chash_iter iter = CHASH_ITER_INIT(table, hash);
	int r = chash_table_find(&iter, key, false);

	/* Found an existing entry */
	if (!r) {
		if (value && table->value_size)
			memcpy(chash_iter_value(iter), value,
			       table->value_size);
		return 1;
	}

	/* Is there a place to add a new entry? */
	if (iter.slot < 0) {
		pr_err("Hash table overflow\n");
		return -ENOMEM;
	}

	chash_iter_set_valid(iter);

	if (table->key_size == 4)
		table->keys32[iter.slot] = key;
	else
		table->keys64[iter.slot] = key;
	if (value && table->value_size)
		memcpy(chash_iter_value(iter), value, table->value_size);

	return 0;
}
EXPORT_SYMBOL(__chash_table_copy_in);

int __chash_table_copy_out(struct __chash_table *table, u64 key,
			   void *value, bool remove)
{
	u32 hash = (table->key_size == 4) ?
		hash_32(key, table->bits) : hash_64(key, table->bits);
	struct chash_iter iter = CHASH_ITER_INIT(table, hash);
	int r = chash_table_find(&iter, key, remove);

	if (r < 0)
		return r;

	if (value && table->value_size)
		memcpy(value, chash_iter_value(iter), table->value_size);

	if (remove)
		chash_iter_set_invalid(iter);

	return iter.slot;
}
EXPORT_SYMBOL(__chash_table_copy_out);

#ifdef CONFIG_CHASH_SELFTEST
/**
 * chash_self_test - Run a self-test of the hash table implementation
 * @bits: Table size will be 2^bits entries
 * @key_size: Size of hash keys in bytes, 4 or 8
 * @min_fill: Minimum fill level during the test
 * @max_fill: Maximum fill level during the test
 * @iterations: Number of test iterations
 *
 * The test adds and removes entries from a hash table, cycling the
 * fill level between min_fill and max_fill entries. Also tests lookup
 * and value retrieval.
 */
static int __init chash_self_test(u8 bits, u8 key_size,
				  int min_fill, int max_fill,
				  u64 iterations)
{
	struct chash_table table;
	int ret;
	u64 add_count, rmv_count;
	u64 value;

	if (key_size == 4 && iterations > 0xffffffff)
		return -EINVAL;
	if (min_fill >= max_fill)
		return -EINVAL;

	ret = chash_table_alloc(&table, bits, key_size, sizeof(u64),
				GFP_KERNEL);
	if (ret) {
		pr_err("chash_table_alloc failed: %d\n", ret);
		return ret;
	}

	for (add_count = 0, rmv_count = 0; add_count < iterations;
	     add_count++) {
		/* When we hit the max_fill level, remove entries down
		 * to min_fill
		 */
		if (add_count - rmv_count == max_fill) {
			u64 find_count = rmv_count;

			/* First try to find all entries that we're
			 * about to remove, confirm their value, test
			 * writing them back a second time.
			 */
			for (; add_count - find_count > min_fill;
			     find_count++) {
				ret = chash_table_copy_out(&table, find_count,
							   &value);
				if (ret < 0) {
					pr_err("chash_table_copy_out failed: %d\n",
					       ret);
					goto out;
				}
				if (value != ~find_count) {
					pr_err("Wrong value retrieved for key 0x%llx, expected 0x%llx got 0x%llx\n",
					       find_count, ~find_count, value);
#ifdef CHASH_DEBUG
					chash_table_dump(&table.table);
#endif
					ret = -EFAULT;
					goto out;
				}
				ret = chash_table_copy_in(&table, find_count,
							  &value);
				if (ret != 1) {
					pr_err("copy_in second time returned %d, expected 1\n",
					       ret);
					ret = -EFAULT;
					goto out;
				}
			}
			/* Remove them until we hit min_fill level */
			for (; add_count - rmv_count > min_fill; rmv_count++) {
				ret = chash_table_remove(&table, rmv_count,
							 NULL);
				if (ret < 0) {
					pr_err("chash_table_remove failed: %d\n",
					       ret);
					goto out;
				}
			}
		}

		/* Add a new value */
		value = ~add_count;
		ret = chash_table_copy_in(&table, add_count, &value);
		if (ret != 0) {
			pr_err("copy_in first time returned %d, expected 0\n",
			       ret);
			ret = -EFAULT;
			goto out;
		}
	}

	chash_table_dump_stats(&table);
	chash_table_reset_stats(&table);

out:
	chash_table_free(&table);
	return ret;
}

static unsigned int chash_test_bits = 10;
MODULE_PARM_DESC(test_bits,
		 "Selftest number of hash bits ([4..20], default=10)");
module_param_named(test_bits, chash_test_bits, uint, 0444);

static unsigned int chash_test_keysize = 8;
MODULE_PARM_DESC(test_keysize, "Selftest keysize (4 or 8, default=8)");
module_param_named(test_keysize, chash_test_keysize, uint, 0444);

static unsigned int chash_test_minfill;
MODULE_PARM_DESC(test_minfill, "Selftest minimum #entries (default=50%)");
module_param_named(test_minfill, chash_test_minfill, uint, 0444);

static unsigned int chash_test_maxfill;
MODULE_PARM_DESC(test_maxfill, "Selftest maximum #entries (default=80%)");
module_param_named(test_maxfill, chash_test_maxfill, uint, 0444);

static unsigned long chash_test_iters;
MODULE_PARM_DESC(test_iters, "Selftest iterations (default=1000 x #entries)");
module_param_named(test_iters, chash_test_iters, ulong, 0444);

static int __init chash_init(void)
{
	int ret;
	u64 ts1_ns;

	/* Skip self test on user errors */
	if (chash_test_bits < 4 || chash_test_bits > 20) {
		pr_err("chash: test_bits out of range [4..20].\n");
		return 0;
	}
	if (chash_test_keysize != 4 && chash_test_keysize != 8) {
		pr_err("chash: test_keysize invalid. Must be 4 or 8.\n");
		return 0;
	}

	if (!chash_test_minfill)
		chash_test_minfill = (1 << chash_test_bits) / 2;
	if (!chash_test_maxfill)
		chash_test_maxfill = (1 << chash_test_bits) * 4 / 5;
	if (!chash_test_iters)
		chash_test_iters = (1 << chash_test_bits) * 1000;

	if (chash_test_minfill >= (1 << chash_test_bits)) {
		pr_err("chash: test_minfill too big. Must be < table size.\n");
		return 0;
	}
	if (chash_test_maxfill >= (1 << chash_test_bits)) {
		pr_err("chash: test_maxfill too big. Must be < table size.\n");
		return 0;
	}
	if (chash_test_minfill >= chash_test_maxfill) {
		pr_err("chash: test_minfill must be < test_maxfill.\n");
		return 0;
	}
	if (chash_test_keysize == 4 && chash_test_iters > 0xffffffff) {
		pr_err("chash: test_iters must be < 4G for 4 byte keys.\n");
		return 0;
	}

	ts1_ns = local_clock();
	ret = chash_self_test(chash_test_bits, chash_test_keysize,
			      chash_test_minfill, chash_test_maxfill,
			      chash_test_iters);
	if (!ret) {
		u64 ts_delta_us = local_clock() - ts1_ns;
		u64 iters_per_second = (u64)chash_test_iters * 1000000;

		do_div(ts_delta_us, 1000);
		do_div(iters_per_second, ts_delta_us);
		pr_info("chash: self test took %llu us, %llu iterations/s\n",
			ts_delta_us, iters_per_second);
	} else {
		pr_err("chash: self test failed: %d\n", ret);
	}

	return ret;
}

module_init(chash_init);

#endif /* CONFIG_CHASH_SELFTEST */

MODULE_DESCRIPTION("Closed hash table");
MODULE_LICENSE("GPL and additional rights");
