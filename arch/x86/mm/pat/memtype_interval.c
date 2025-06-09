// SPDX-License-Identifier: GPL-2.0
/*
 * Handle caching attributes in page tables (PAT)
 *
 * Authors: Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 *          Suresh B Siddha <suresh.b.siddha@intel.com>
 *
 * Interval tree used to store the PAT memory type reservations.
 */

#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/interval_tree_generic.h>
#include <linux/sched.h>
#include <linux/gfp.h>
#include <linux/pgtable.h>

#include <asm/memtype.h>

#include "memtype.h"

/*
 * The memtype tree keeps track of memory type for specific
 * physical memory areas. Without proper tracking, conflicting memory
 * types in different mappings can cause CPU cache corruption.
 *
 * The tree is an interval tree (augmented rbtree) which tree is ordered
 * by the starting address. The tree can contain multiple entries for
 * different regions which overlap. All the aliases have the same
 * cache attributes of course, as enforced by the PAT logic.
 *
 * memtype_lock protects the rbtree.
 */

static inline u64 interval_start(struct memtype *entry)
{
	return entry->start;
}

static inline u64 interval_end(struct memtype *entry)
{
	return entry->end - 1;
}

INTERVAL_TREE_DEFINE(struct memtype, rb, u64, subtree_max_end,
		     interval_start, interval_end,
		     static, interval)

static struct rb_root_cached memtype_rbroot = RB_ROOT_CACHED;

static int memtype_check_conflict(u64 start, u64 end,
				  enum page_cache_mode reqtype,
				  enum page_cache_mode *newtype)
{
	struct memtype *entry_match;
	enum page_cache_mode found_type = reqtype;

	entry_match = interval_iter_first(&memtype_rbroot, start, end-1);
	if (entry_match == NULL)
		goto success;

	if (entry_match->type != found_type && newtype == NULL)
		goto failure;

	dprintk("Overlap at 0x%Lx-0x%Lx\n", entry_match->start, entry_match->end);
	found_type = entry_match->type;

	entry_match = interval_iter_next(entry_match, start, end-1);
	while (entry_match) {
		if (entry_match->type != found_type)
			goto failure;

		entry_match = interval_iter_next(entry_match, start, end-1);
	}
success:
	if (newtype)
		*newtype = found_type;

	return 0;

failure:
	pr_info("x86/PAT: %s:%d conflicting memory types %Lx-%Lx %s<->%s\n",
		current->comm, current->pid, start, end,
		cattr_name(found_type), cattr_name(entry_match->type));

	return -EBUSY;
}

int memtype_check_insert(struct memtype *entry_new, enum page_cache_mode *ret_type)
{
	int err = 0;

	err = memtype_check_conflict(entry_new->start, entry_new->end, entry_new->type, ret_type);
	if (err)
		return err;

	if (ret_type)
		entry_new->type = *ret_type;

	interval_insert(entry_new, &memtype_rbroot);
	return 0;
}

struct memtype *memtype_erase(u64 start, u64 end)
{
	struct memtype *entry = interval_iter_first(&memtype_rbroot, start, end - 1);

	while (entry && entry->start < end) {
		if (entry->start == start && entry->end == end) {
			interval_remove(entry, &memtype_rbroot);
			return entry;
		}
		entry = interval_iter_next(entry, start, end - 1);
	}
	return ERR_PTR(-EINVAL);
}

struct memtype *memtype_lookup(u64 addr)
{
	return interval_iter_first(&memtype_rbroot, addr, addr + PAGE_SIZE-1);
}

/*
 * Debugging helper, copy the Nth entry of the tree into a
 * a copy for printout. This allows us to print out the tree
 * via debugfs, without holding the memtype_lock too long:
 */
#ifdef CONFIG_DEBUG_FS
int memtype_copy_nth_element(struct memtype *entry_out, loff_t pos)
{
	struct memtype *entry_match;
	int i = 1;

	entry_match = interval_iter_first(&memtype_rbroot, 0, ULONG_MAX);

	while (entry_match && pos != i) {
		entry_match = interval_iter_next(entry_match, 0, ULONG_MAX);
		i++;
	}

	if (entry_match) { /* pos == i */
		*entry_out = *entry_match;
		return 0;
	} else {
		return 1;
	}
}
#endif
