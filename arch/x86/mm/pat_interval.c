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

#include <asm/pgtable.h>
#include <asm/pat.h>

#include "pat_internal.h"

/*
 * The memtype tree keeps track of memory type for specific
 * physical memory areas. Without proper tracking, conflicting memory
 * types in different mappings can cause CPU cache corruption.
 *
 * The tree is an interval tree (augmented rbtree) with tree ordered
 * on starting address. Tree can contain multiple entries for
 * different regions which overlap. All the aliases have the same
 * cache attributes of course.
 *
 * memtype_lock protects the rbtree.
 */
static inline u64 memtype_interval_start(struct memtype *memtype)
{
	return memtype->start;
}

static inline u64 memtype_interval_end(struct memtype *memtype)
{
	return memtype->end - 1;
}
INTERVAL_TREE_DEFINE(struct memtype, rb, u64, subtree_max_end,
		     memtype_interval_start, memtype_interval_end,
		     static, memtype_interval)

static struct rb_root_cached memtype_rbroot = RB_ROOT_CACHED;

enum {
	MEMTYPE_EXACT_MATCH	= 0,
	MEMTYPE_END_MATCH	= 1
};

static struct memtype *memtype_match(u64 start, u64 end, int match_type)
{
	struct memtype *match;

	match = memtype_interval_iter_first(&memtype_rbroot, start, end-1);
	while (match != NULL && match->start < end) {
		if ((match_type == MEMTYPE_EXACT_MATCH) &&
		    (match->start == start) && (match->end == end))
			return match;

		if ((match_type == MEMTYPE_END_MATCH) &&
		    (match->start < start) && (match->end == end))
			return match;

		match = memtype_interval_iter_next(match, start, end-1);
	}

	return NULL; /* Returns NULL if there is no match */
}

static int memtype_check_conflict(u64 start, u64 end,
				  enum page_cache_mode reqtype,
				  enum page_cache_mode *newtype)
{
	struct memtype *match;
	enum page_cache_mode found_type = reqtype;

	match = memtype_interval_iter_first(&memtype_rbroot, start, end-1);
	if (match == NULL)
		goto success;

	if (match->type != found_type && newtype == NULL)
		goto failure;

	dprintk("Overlap at 0x%Lx-0x%Lx\n", match->start, match->end);
	found_type = match->type;

	match = memtype_interval_iter_next(match, start, end-1);
	while (match) {
		if (match->type != found_type)
			goto failure;

		match = memtype_interval_iter_next(match, start, end-1);
	}
success:
	if (newtype)
		*newtype = found_type;

	return 0;

failure:
	pr_info("x86/PAT: %s:%d conflicting memory types %Lx-%Lx %s<->%s\n",
		current->comm, current->pid, start, end,
		cattr_name(found_type), cattr_name(match->type));
	return -EBUSY;
}

int memtype_check_insert(struct memtype *new,
			 enum page_cache_mode *ret_type)
{
	int err = 0;

	err = memtype_check_conflict(new->start, new->end, new->type, ret_type);
	if (err)
		return err;

	if (ret_type)
		new->type = *ret_type;

	memtype_interval_insert(new, &memtype_rbroot);
	return 0;
}

struct memtype *memtype_erase(u64 start, u64 end)
{
	struct memtype *data;

	/*
	 * Since the memtype_rbroot tree allows overlapping ranges,
	 * memtype_erase() checks with EXACT_MATCH first, i.e. free
	 * a whole node for the munmap case.  If no such entry is found,
	 * it then checks with END_MATCH, i.e. shrink the size of a node
	 * from the end for the mremap case.
	 */
	data = memtype_match(start, end, MEMTYPE_EXACT_MATCH);
	if (!data) {
		data = memtype_match(start, end, MEMTYPE_END_MATCH);
		if (!data)
			return ERR_PTR(-EINVAL);
	}

	if (data->start == start) {
		/* munmap: erase this node */
		memtype_interval_remove(data, &memtype_rbroot);
	} else {
		/* mremap: update the end value of this node */
		memtype_interval_remove(data, &memtype_rbroot);
		data->end = start;
		memtype_interval_insert(data, &memtype_rbroot);
		return NULL;
	}

	return data;
}

struct memtype *memtype_lookup(u64 addr)
{
	return memtype_interval_iter_first(&memtype_rbroot, addr,
					   addr + PAGE_SIZE-1);
}

#if defined(CONFIG_DEBUG_FS)
int memtype_copy_nth_element(struct memtype *out, loff_t pos)
{
	struct memtype *match;
	int i = 1;

	match = memtype_interval_iter_first(&memtype_rbroot, 0, ULONG_MAX);
	while (match && pos != i) {
		match = memtype_interval_iter_next(match, 0, ULONG_MAX);
		i++;
	}

	if (match) { /* pos == i */
		*out = *match;
		return 0;
	} else {
		return 1;
	}
}
#endif
