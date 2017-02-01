/*
 * Test cases for the drm_mm range manager
 */

#define pr_fmt(fmt) "drm_mm: " fmt

#include <linux/module.h>
#include <linux/prime_numbers.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/vmalloc.h>

#include <drm/drm_mm.h>

#include "../lib/drm_random.h"

#define TESTS "drm_mm_selftests.h"
#include "drm_selftest.h"

static unsigned int random_seed;
static unsigned int max_iterations = 8192;
static unsigned int max_prime = 128;

enum {
	DEFAULT,
	TOPDOWN,
	BEST,
};

static const struct insert_mode {
	const char *name;
	unsigned int search_flags;
	unsigned int create_flags;
} insert_modes[] = {
	[DEFAULT] = { "default", DRM_MM_SEARCH_DEFAULT, DRM_MM_CREATE_DEFAULT },
	[TOPDOWN] = { "top-down", DRM_MM_SEARCH_BELOW, DRM_MM_CREATE_TOP },
	[BEST] = { "best", DRM_MM_SEARCH_BEST, DRM_MM_CREATE_DEFAULT },
	{}
}, evict_modes[] = {
	{ "default", DRM_MM_SEARCH_DEFAULT, DRM_MM_CREATE_DEFAULT },
	{ "top-down", DRM_MM_SEARCH_BELOW, DRM_MM_CREATE_TOP },
	{}
};

static int igt_sanitycheck(void *ignored)
{
	pr_info("%s - ok!\n", __func__);
	return 0;
}

static bool assert_no_holes(const struct drm_mm *mm)
{
	struct drm_mm_node *hole;
	u64 hole_start, hole_end;
	unsigned long count;

	count = 0;
	drm_mm_for_each_hole(hole, mm, hole_start, hole_end)
		count++;
	if (count) {
		pr_err("Expected to find no holes (after reserve), found %lu instead\n", count);
		return false;
	}

	drm_mm_for_each_node(hole, mm) {
		if (drm_mm_hole_follows(hole)) {
			pr_err("Hole follows node, expected none!\n");
			return false;
		}
	}

	return true;
}

static bool assert_one_hole(const struct drm_mm *mm, u64 start, u64 end)
{
	struct drm_mm_node *hole;
	u64 hole_start, hole_end;
	unsigned long count;
	bool ok = true;

	if (end <= start)
		return true;

	count = 0;
	drm_mm_for_each_hole(hole, mm, hole_start, hole_end) {
		if (start != hole_start || end != hole_end) {
			if (ok)
				pr_err("empty mm has incorrect hole, found (%llx, %llx), expect (%llx, %llx)\n",
				       hole_start, hole_end,
				       start, end);
			ok = false;
		}
		count++;
	}
	if (count != 1) {
		pr_err("Expected to find one hole, found %lu instead\n", count);
		ok = false;
	}

	return ok;
}

static bool assert_continuous(const struct drm_mm *mm, u64 size)
{
	struct drm_mm_node *node, *check, *found;
	unsigned long n;
	u64 addr;

	if (!assert_no_holes(mm))
		return false;

	n = 0;
	addr = 0;
	drm_mm_for_each_node(node, mm) {
		if (node->start != addr) {
			pr_err("node[%ld] list out of order, expected %llx found %llx\n",
			       n, addr, node->start);
			return false;
		}

		if (node->size != size) {
			pr_err("node[%ld].size incorrect, expected %llx, found %llx\n",
			       n, size, node->size);
			return false;
		}

		if (drm_mm_hole_follows(node)) {
			pr_err("node[%ld] is followed by a hole!\n", n);
			return false;
		}

		found = NULL;
		drm_mm_for_each_node_in_range(check, mm, addr, addr + size) {
			if (node != check) {
				pr_err("lookup return wrong node, expected start %llx, found %llx\n",
				       node->start, check->start);
				return false;
			}
			found = check;
		}
		if (!found) {
			pr_err("lookup failed for node %llx + %llx\n",
			       addr, size);
			return false;
		}

		addr += size;
		n++;
	}

	return true;
}

static u64 misalignment(struct drm_mm_node *node, u64 alignment)
{
	u64 rem;

	if (!alignment)
		return 0;

	div64_u64_rem(node->start, alignment, &rem);
	return rem;
}

static bool assert_node(struct drm_mm_node *node, struct drm_mm *mm,
			u64 size, u64 alignment, unsigned long color)
{
	bool ok = true;

	if (!drm_mm_node_allocated(node) || node->mm != mm) {
		pr_err("node not allocated\n");
		ok = false;
	}

	if (node->size != size) {
		pr_err("node has wrong size, found %llu, expected %llu\n",
		       node->size, size);
		ok = false;
	}

	if (misalignment(node, alignment)) {
		pr_err("node is misalinged, start %llx rem %llu, expected alignment %llu\n",
		       node->start, misalignment(node, alignment), alignment);
		ok = false;
	}

	if (node->color != color) {
		pr_err("node has wrong color, found %lu, expected %lu\n",
		       node->color, color);
		ok = false;
	}

	return ok;
}

#define show_mm(mm) do { \
	struct drm_printer __p = drm_debug_printer(__func__); \
	drm_mm_print((mm), &__p); } while (0)

static int igt_init(void *ignored)
{
	const unsigned int size = 4096;
	struct drm_mm mm;
	struct drm_mm_node tmp;
	int ret = -EINVAL;

	/* Start with some simple checks on initialising the struct drm_mm */
	memset(&mm, 0, sizeof(mm));
	if (drm_mm_initialized(&mm)) {
		pr_err("zeroed mm claims to be initialized\n");
		return ret;
	}

	memset(&mm, 0xff, sizeof(mm));
	drm_mm_init(&mm, 0, size);
	if (!drm_mm_initialized(&mm)) {
		pr_err("mm claims not to be initialized\n");
		goto out;
	}

	if (!drm_mm_clean(&mm)) {
		pr_err("mm not empty on creation\n");
		goto out;
	}

	/* After creation, it should all be one massive hole */
	if (!assert_one_hole(&mm, 0, size)) {
		ret = -EINVAL;
		goto out;
	}

	memset(&tmp, 0, sizeof(tmp));
	tmp.start = 0;
	tmp.size = size;
	ret = drm_mm_reserve_node(&mm, &tmp);
	if (ret) {
		pr_err("failed to reserve whole drm_mm\n");
		goto out;
	}

	/* After filling the range entirely, there should be no holes */
	if (!assert_no_holes(&mm)) {
		ret = -EINVAL;
		goto out;
	}

	/* And then after emptying it again, the massive hole should be back */
	drm_mm_remove_node(&tmp);
	if (!assert_one_hole(&mm, 0, size)) {
		ret = -EINVAL;
		goto out;
	}

out:
	if (ret)
		show_mm(&mm);
	drm_mm_takedown(&mm);
	return ret;
}

static int igt_debug(void *ignored)
{
	struct drm_mm mm;
	struct drm_mm_node nodes[2];
	int ret;

	/* Create a small drm_mm with a couple of nodes and a few holes, and
	 * check that the debug iterator doesn't explode over a trivial drm_mm.
	 */

	drm_mm_init(&mm, 0, 4096);

	memset(nodes, 0, sizeof(nodes));
	nodes[0].start = 512;
	nodes[0].size = 1024;
	ret = drm_mm_reserve_node(&mm, &nodes[0]);
	if (ret) {
		pr_err("failed to reserve node[0] {start=%lld, size=%lld)\n",
		       nodes[0].start, nodes[0].size);
		return ret;
	}

	nodes[1].size = 1024;
	nodes[1].start = 4096 - 512 - nodes[1].size;
	ret = drm_mm_reserve_node(&mm, &nodes[1]);
	if (ret) {
		pr_err("failed to reserve node[1] {start=%lld, size=%lld)\n",
		       nodes[1].start, nodes[1].size);
		return ret;
	}

	show_mm(&mm);
	return 0;
}

static struct drm_mm_node *set_node(struct drm_mm_node *node,
				    u64 start, u64 size)
{
	node->start = start;
	node->size = size;
	return node;
}

static bool expect_reserve_fail(struct drm_mm *mm, struct drm_mm_node *node)
{
	int err;

	err = drm_mm_reserve_node(mm, node);
	if (likely(err == -ENOSPC))
		return true;

	if (!err) {
		pr_err("impossible reserve succeeded, node %llu + %llu\n",
		       node->start, node->size);
		drm_mm_remove_node(node);
	} else {
		pr_err("impossible reserve failed with wrong error %d [expected %d], node %llu + %llu\n",
		       err, -ENOSPC, node->start, node->size);
	}
	return false;
}

static bool check_reserve_boundaries(struct drm_mm *mm,
				     unsigned int count,
				     u64 size)
{
	const struct boundary {
		u64 start, size;
		const char *name;
	} boundaries[] = {
#define B(st, sz) { (st), (sz), "{ " #st ", " #sz "}" }
		B(0, 0),
		B(-size, 0),
		B(size, 0),
		B(size * count, 0),
		B(-size, size),
		B(-size, -size),
		B(-size, 2*size),
		B(0, -size),
		B(size, -size),
		B(count*size, size),
		B(count*size, -size),
		B(count*size, count*size),
		B(count*size, -count*size),
		B(count*size, -(count+1)*size),
		B((count+1)*size, size),
		B((count+1)*size, -size),
		B((count+1)*size, -2*size),
#undef B
	};
	struct drm_mm_node tmp = {};
	int n;

	for (n = 0; n < ARRAY_SIZE(boundaries); n++) {
		if (!expect_reserve_fail(mm,
					 set_node(&tmp,
						  boundaries[n].start,
						  boundaries[n].size))) {
			pr_err("boundary[%d:%s] failed, count=%u, size=%lld\n",
			       n, boundaries[n].name, count, size);
			return false;
		}
	}

	return true;
}

static int __igt_reserve(unsigned int count, u64 size)
{
	DRM_RND_STATE(prng, random_seed);
	struct drm_mm mm;
	struct drm_mm_node tmp, *nodes, *node, *next;
	unsigned int *order, n, m, o = 0;
	int ret, err;

	/* For exercising drm_mm_reserve_node(), we want to check that
	 * reservations outside of the drm_mm range are rejected, and to
	 * overlapping and otherwise already occupied ranges. Afterwards,
	 * the tree and nodes should be intact.
	 */

	DRM_MM_BUG_ON(!count);
	DRM_MM_BUG_ON(!size);

	ret = -ENOMEM;
	order = drm_random_order(count, &prng);
	if (!order)
		goto err;

	nodes = vzalloc(sizeof(*nodes) * count);
	if (!nodes)
		goto err_order;

	ret = -EINVAL;
	drm_mm_init(&mm, 0, count * size);

	if (!check_reserve_boundaries(&mm, count, size))
		goto out;

	for (n = 0; n < count; n++) {
		nodes[n].start = order[n] * size;
		nodes[n].size = size;

		err = drm_mm_reserve_node(&mm, &nodes[n]);
		if (err) {
			pr_err("reserve failed, step %d, start %llu\n",
			       n, nodes[n].start);
			ret = err;
			goto out;
		}

		if (!drm_mm_node_allocated(&nodes[n])) {
			pr_err("reserved node not allocated! step %d, start %llu\n",
			       n, nodes[n].start);
			goto out;
		}

		if (!expect_reserve_fail(&mm, &nodes[n]))
			goto out;
	}

	/* After random insertion the nodes should be in order */
	if (!assert_continuous(&mm, size))
		goto out;

	/* Repeated use should then fail */
	drm_random_reorder(order, count, &prng);
	for (n = 0; n < count; n++) {
		if (!expect_reserve_fail(&mm,
					 set_node(&tmp, order[n] * size, 1)))
			goto out;

		/* Remove and reinsert should work */
		drm_mm_remove_node(&nodes[order[n]]);
		err = drm_mm_reserve_node(&mm, &nodes[order[n]]);
		if (err) {
			pr_err("reserve failed, step %d, start %llu\n",
			       n, nodes[n].start);
			ret = err;
			goto out;
		}
	}

	if (!assert_continuous(&mm, size))
		goto out;

	/* Overlapping use should then fail */
	for (n = 0; n < count; n++) {
		if (!expect_reserve_fail(&mm, set_node(&tmp, 0, size*count)))
			goto out;
	}
	for (n = 0; n < count; n++) {
		if (!expect_reserve_fail(&mm,
					 set_node(&tmp,
						  size * n,
						  size * (count - n))))
			goto out;
	}

	/* Remove several, reinsert, check full */
	for_each_prime_number(n, min(max_prime, count)) {
		for (m = 0; m < n; m++) {
			node = &nodes[order[(o + m) % count]];
			drm_mm_remove_node(node);
		}

		for (m = 0; m < n; m++) {
			node = &nodes[order[(o + m) % count]];
			err = drm_mm_reserve_node(&mm, node);
			if (err) {
				pr_err("reserve failed, step %d/%d, start %llu\n",
				       m, n, node->start);
				ret = err;
				goto out;
			}
		}

		o += n;

		if (!assert_continuous(&mm, size))
			goto out;
	}

	ret = 0;
out:
	drm_mm_for_each_node_safe(node, next, &mm)
		drm_mm_remove_node(node);
	drm_mm_takedown(&mm);
	vfree(nodes);
err_order:
	kfree(order);
err:
	return ret;
}

static int igt_reserve(void *ignored)
{
	const unsigned int count = min_t(unsigned int, BIT(10), max_iterations);
	int n, ret;

	for_each_prime_number_from(n, 1, 54) {
		u64 size = BIT_ULL(n);

		ret = __igt_reserve(count, size - 1);
		if (ret)
			return ret;

		ret = __igt_reserve(count, size);
		if (ret)
			return ret;

		ret = __igt_reserve(count, size + 1);
		if (ret)
			return ret;
	}

	return 0;
}

static bool expect_insert(struct drm_mm *mm, struct drm_mm_node *node,
			  u64 size, u64 alignment, unsigned long color,
			  const struct insert_mode *mode)
{
	int err;

	err = drm_mm_insert_node_generic(mm, node,
					 size, alignment, color,
					 mode->search_flags,
					 mode->create_flags);
	if (err) {
		pr_err("insert (size=%llu, alignment=%llu, color=%lu, mode=%s) failed with err=%d\n",
		       size, alignment, color, mode->name, err);
		return false;
	}

	if (!assert_node(node, mm, size, alignment, color)) {
		drm_mm_remove_node(node);
		return false;
	}

	return true;
}

static bool expect_insert_fail(struct drm_mm *mm, u64 size)
{
	struct drm_mm_node tmp = {};
	int err;

	err = drm_mm_insert_node(mm, &tmp, size, 0, DRM_MM_SEARCH_DEFAULT);
	if (likely(err == -ENOSPC))
		return true;

	if (!err) {
		pr_err("impossible insert succeeded, node %llu + %llu\n",
		       tmp.start, tmp.size);
		drm_mm_remove_node(&tmp);
	} else {
		pr_err("impossible insert failed with wrong error %d [expected %d], size %llu\n",
		       err, -ENOSPC, size);
	}
	return false;
}

static int __igt_insert(unsigned int count, u64 size, bool replace)
{
	DRM_RND_STATE(prng, random_seed);
	const struct insert_mode *mode;
	struct drm_mm mm;
	struct drm_mm_node *nodes, *node, *next;
	unsigned int *order, n, m, o = 0;
	int ret;

	/* Fill a range with lots of nodes, check it doesn't fail too early */

	DRM_MM_BUG_ON(!count);
	DRM_MM_BUG_ON(!size);

	ret = -ENOMEM;
	nodes = vmalloc(count * sizeof(*nodes));
	if (!nodes)
		goto err;

	order = drm_random_order(count, &prng);
	if (!order)
		goto err_nodes;

	ret = -EINVAL;
	drm_mm_init(&mm, 0, count * size);

	for (mode = insert_modes; mode->name; mode++) {
		for (n = 0; n < count; n++) {
			struct drm_mm_node tmp;

			node = replace ? &tmp : &nodes[n];
			memset(node, 0, sizeof(*node));
			if (!expect_insert(&mm, node, size, 0, n, mode)) {
				pr_err("%s insert failed, size %llu step %d\n",
				       mode->name, size, n);
				goto out;
			}

			if (replace) {
				drm_mm_replace_node(&tmp, &nodes[n]);
				if (drm_mm_node_allocated(&tmp)) {
					pr_err("replaced old-node still allocated! step %d\n",
					       n);
					goto out;
				}

				if (!assert_node(&nodes[n], &mm, size, 0, n)) {
					pr_err("replaced node did not inherit parameters, size %llu step %d\n",
					       size, n);
					goto out;
				}

				if (tmp.start != nodes[n].start) {
					pr_err("replaced node mismatch location expected [%llx + %llx], found [%llx + %llx]\n",
					       tmp.start, size,
					       nodes[n].start, nodes[n].size);
					goto out;
				}
			}
		}

		/* After random insertion the nodes should be in order */
		if (!assert_continuous(&mm, size))
			goto out;

		/* Repeated use should then fail */
		if (!expect_insert_fail(&mm, size))
			goto out;

		/* Remove one and reinsert, as the only hole it should refill itself */
		for (n = 0; n < count; n++) {
			u64 addr = nodes[n].start;

			drm_mm_remove_node(&nodes[n]);
			if (!expect_insert(&mm, &nodes[n], size, 0, n, mode)) {
				pr_err("%s reinsert failed, size %llu step %d\n",
				       mode->name, size, n);
				goto out;
			}

			if (nodes[n].start != addr) {
				pr_err("%s reinsert node moved, step %d, expected %llx, found %llx\n",
				       mode->name, n, addr, nodes[n].start);
				goto out;
			}

			if (!assert_continuous(&mm, size))
				goto out;
		}

		/* Remove several, reinsert, check full */
		for_each_prime_number(n, min(max_prime, count)) {
			for (m = 0; m < n; m++) {
				node = &nodes[order[(o + m) % count]];
				drm_mm_remove_node(node);
			}

			for (m = 0; m < n; m++) {
				node = &nodes[order[(o + m) % count]];
				if (!expect_insert(&mm, node, size, 0, n, mode)) {
					pr_err("%s multiple reinsert failed, size %llu step %d\n",
					       mode->name, size, n);
					goto out;
				}
			}

			o += n;

			if (!assert_continuous(&mm, size))
				goto out;

			if (!expect_insert_fail(&mm, size))
				goto out;
		}

		drm_mm_for_each_node_safe(node, next, &mm)
			drm_mm_remove_node(node);
		DRM_MM_BUG_ON(!drm_mm_clean(&mm));
	}

	ret = 0;
out:
	drm_mm_for_each_node_safe(node, next, &mm)
		drm_mm_remove_node(node);
	drm_mm_takedown(&mm);
	kfree(order);
err_nodes:
	vfree(nodes);
err:
	return ret;
}

static int igt_insert(void *ignored)
{
	const unsigned int count = min_t(unsigned int, BIT(10), max_iterations);
	unsigned int n;
	int ret;

	for_each_prime_number_from(n, 1, 54) {
		u64 size = BIT_ULL(n);

		ret = __igt_insert(count, size - 1, false);
		if (ret)
			return ret;

		ret = __igt_insert(count, size, false);
		if (ret)
			return ret;

		ret = __igt_insert(count, size + 1, false);
	}

	return 0;
}

static int igt_replace(void *ignored)
{
	const unsigned int count = min_t(unsigned int, BIT(10), max_iterations);
	unsigned int n;
	int ret;

	/* Reuse igt_insert to exercise replacement by inserting a dummy node,
	 * then replacing it with the intended node. We want to check that
	 * the tree is intact and all the information we need is carried
	 * across to the target node.
	 */

	for_each_prime_number_from(n, 1, 54) {
		u64 size = BIT_ULL(n);

		ret = __igt_insert(count, size - 1, true);
		if (ret)
			return ret;

		ret = __igt_insert(count, size, true);
		if (ret)
			return ret;

		ret = __igt_insert(count, size + 1, true);
	}

	return 0;
}

static bool expect_insert_in_range(struct drm_mm *mm, struct drm_mm_node *node,
				   u64 size, u64 alignment, unsigned long color,
				   u64 range_start, u64 range_end,
				   const struct insert_mode *mode)
{
	int err;

	err = drm_mm_insert_node_in_range_generic(mm, node,
						  size, alignment, color,
						  range_start, range_end,
						  mode->search_flags,
						  mode->create_flags);
	if (err) {
		pr_err("insert (size=%llu, alignment=%llu, color=%lu, mode=%s) nto range [%llx, %llx] failed with err=%d\n",
		       size, alignment, color, mode->name,
		       range_start, range_end, err);
		return false;
	}

	if (!assert_node(node, mm, size, alignment, color)) {
		drm_mm_remove_node(node);
		return false;
	}

	return true;
}

static bool expect_insert_in_range_fail(struct drm_mm *mm,
					u64 size,
					u64 range_start,
					u64 range_end)
{
	struct drm_mm_node tmp = {};
	int err;

	err = drm_mm_insert_node_in_range_generic(mm, &tmp,
						  size, 0, 0,
						  range_start, range_end,
						  DRM_MM_SEARCH_DEFAULT,
						  DRM_MM_CREATE_DEFAULT);
	if (likely(err == -ENOSPC))
		return true;

	if (!err) {
		pr_err("impossible insert succeeded, node %llx + %llu, range [%llx, %llx]\n",
		       tmp.start, tmp.size, range_start, range_end);
		drm_mm_remove_node(&tmp);
	} else {
		pr_err("impossible insert failed with wrong error %d [expected %d], size %llu, range [%llx, %llx]\n",
		       err, -ENOSPC, size, range_start, range_end);
	}

	return false;
}

static bool assert_contiguous_in_range(struct drm_mm *mm,
				       u64 size,
				       u64 start,
				       u64 end)
{
	struct drm_mm_node *node;
	unsigned int n;

	if (!expect_insert_in_range_fail(mm, size, start, end))
		return false;

	n = div64_u64(start + size - 1, size);
	drm_mm_for_each_node(node, mm) {
		if (node->start < start || node->start + node->size > end) {
			pr_err("node %d out of range, address [%llx + %llu], range [%llx, %llx]\n",
			       n, node->start, node->start + node->size, start, end);
			return false;
		}

		if (node->start != n * size) {
			pr_err("node %d out of order, expected start %llx, found %llx\n",
			       n, n * size, node->start);
			return false;
		}

		if (node->size != size) {
			pr_err("node %d has wrong size, expected size %llx, found %llx\n",
			       n, size, node->size);
			return false;
		}

		if (drm_mm_hole_follows(node) &&
		    drm_mm_hole_node_end(node) < end) {
			pr_err("node %d is followed by a hole!\n", n);
			return false;
		}

		n++;
	}

	drm_mm_for_each_node_in_range(node, mm, 0, start) {
		if (node) {
			pr_err("node before start: node=%llx+%llu, start=%llx\n",
			       node->start, node->size, start);
			return false;
		}
	}

	drm_mm_for_each_node_in_range(node, mm, end, U64_MAX) {
		if (node) {
			pr_err("node after end: node=%llx+%llu, end=%llx\n",
			       node->start, node->size, end);
			return false;
		}
	}

	return true;
}

static int __igt_insert_range(unsigned int count, u64 size, u64 start, u64 end)
{
	const struct insert_mode *mode;
	struct drm_mm mm;
	struct drm_mm_node *nodes, *node, *next;
	unsigned int n, start_n, end_n;
	int ret;

	DRM_MM_BUG_ON(!count);
	DRM_MM_BUG_ON(!size);
	DRM_MM_BUG_ON(end <= start);

	/* Very similar to __igt_insert(), but now instead of populating the
	 * full range of the drm_mm, we try to fill a small portion of it.
	 */

	ret = -ENOMEM;
	nodes = vzalloc(count * sizeof(*nodes));
	if (!nodes)
		goto err;

	ret = -EINVAL;
	drm_mm_init(&mm, 0, count * size);

	start_n = div64_u64(start + size - 1, size);
	end_n = div64_u64(end - size, size);

	for (mode = insert_modes; mode->name; mode++) {
		for (n = start_n; n <= end_n; n++) {
			if (!expect_insert_in_range(&mm, &nodes[n],
						    size, size, n,
						    start, end, mode)) {
				pr_err("%s insert failed, size %llu, step %d [%d, %d], range [%llx, %llx]\n",
				       mode->name, size, n,
				       start_n, end_n,
				       start, end);
				goto out;
			}
		}

		if (!assert_contiguous_in_range(&mm, size, start, end)) {
			pr_err("%s: range [%llx, %llx] not full after initialisation, size=%llu\n",
			       mode->name, start, end, size);
			goto out;
		}

		/* Remove one and reinsert, it should refill itself */
		for (n = start_n; n <= end_n; n++) {
			u64 addr = nodes[n].start;

			drm_mm_remove_node(&nodes[n]);
			if (!expect_insert_in_range(&mm, &nodes[n],
						    size, size, n,
						    start, end, mode)) {
				pr_err("%s reinsert failed, step %d\n", mode->name, n);
				goto out;
			}

			if (nodes[n].start != addr) {
				pr_err("%s reinsert node moved, step %d, expected %llx, found %llx\n",
				       mode->name, n, addr, nodes[n].start);
				goto out;
			}
		}

		if (!assert_contiguous_in_range(&mm, size, start, end)) {
			pr_err("%s: range [%llx, %llx] not full after reinsertion, size=%llu\n",
			       mode->name, start, end, size);
			goto out;
		}

		drm_mm_for_each_node_safe(node, next, &mm)
			drm_mm_remove_node(node);
		DRM_MM_BUG_ON(!drm_mm_clean(&mm));
	}

	ret = 0;
out:
	drm_mm_for_each_node_safe(node, next, &mm)
		drm_mm_remove_node(node);
	drm_mm_takedown(&mm);
	vfree(nodes);
err:
	return ret;
}

static int insert_outside_range(void)
{
	struct drm_mm mm;
	const unsigned int start = 1024;
	const unsigned int end = 2048;
	const unsigned int size = end - start;

	drm_mm_init(&mm, start, size);

	if (!expect_insert_in_range_fail(&mm, 1, 0, start))
		return -EINVAL;

	if (!expect_insert_in_range_fail(&mm, size,
					 start - size/2, start + (size+1)/2))
		return -EINVAL;

	if (!expect_insert_in_range_fail(&mm, size,
					 end - (size+1)/2, end + size/2))
		return -EINVAL;

	if (!expect_insert_in_range_fail(&mm, 1, end, end + size))
		return -EINVAL;

	drm_mm_takedown(&mm);
	return 0;
}

static int igt_insert_range(void *ignored)
{
	const unsigned int count = min_t(unsigned int, BIT(13), max_iterations);
	unsigned int n;
	int ret;

	/* Check that requests outside the bounds of drm_mm are rejected. */
	ret = insert_outside_range();
	if (ret)
		return ret;

	for_each_prime_number_from(n, 1, 50) {
		const u64 size = BIT_ULL(n);
		const u64 max = count * size;

		ret = __igt_insert_range(count, size, 0, max);
		if (ret)
			return ret;

		ret = __igt_insert_range(count, size, 1, max);
		if (ret)
			return ret;

		ret = __igt_insert_range(count, size, 0, max - 1);
		if (ret)
			return ret;

		ret = __igt_insert_range(count, size, 0, max/2);
		if (ret)
			return ret;

		ret = __igt_insert_range(count, size, max/2, max);
		if (ret)
			return ret;

		ret = __igt_insert_range(count, size, max/4+1, 3*max/4-1);
		if (ret)
			return ret;
	}

	return 0;
}

static int igt_align(void *ignored)
{
	const struct insert_mode *mode;
	const unsigned int max_count = min(8192u, max_prime);
	struct drm_mm mm;
	struct drm_mm_node *nodes, *node, *next;
	unsigned int prime;
	int ret = -EINVAL;

	/* For each of the possible insertion modes, we pick a few
	 * arbitrary alignments and check that the inserted node
	 * meets our requirements.
	 */

	nodes = vzalloc(max_count * sizeof(*nodes));
	if (!nodes)
		goto err;

	drm_mm_init(&mm, 1, U64_MAX - 2);

	for (mode = insert_modes; mode->name; mode++) {
		unsigned int i = 0;

		for_each_prime_number_from(prime, 1, max_count) {
			u64 size = next_prime_number(prime);

			if (!expect_insert(&mm, &nodes[i],
					   size, prime, i,
					   mode)) {
				pr_err("%s insert failed with alignment=%d",
				       mode->name, prime);
				goto out;
			}

			i++;
		}

		drm_mm_for_each_node_safe(node, next, &mm)
			drm_mm_remove_node(node);
		DRM_MM_BUG_ON(!drm_mm_clean(&mm));
	}

	ret = 0;
out:
	drm_mm_for_each_node_safe(node, next, &mm)
		drm_mm_remove_node(node);
	drm_mm_takedown(&mm);
	vfree(nodes);
err:
	return ret;
}

static int igt_align_pot(int max)
{
	struct drm_mm mm;
	struct drm_mm_node *node, *next;
	int bit;
	int ret = -EINVAL;

	/* Check that we can align to the full u64 address space */

	drm_mm_init(&mm, 1, U64_MAX - 2);

	for (bit = max - 1; bit; bit--) {
		u64 align, size;

		node = kzalloc(sizeof(*node), GFP_KERNEL);
		if (!node) {
			ret = -ENOMEM;
			goto out;
		}

		align = BIT_ULL(bit);
		size = BIT_ULL(bit-1) + 1;
		if (!expect_insert(&mm, node,
				   size, align, bit,
				   &insert_modes[0])) {
			pr_err("insert failed with alignment=%llx [%d]",
			       align, bit);
			goto out;
		}
	}

	ret = 0;
out:
	drm_mm_for_each_node_safe(node, next, &mm) {
		drm_mm_remove_node(node);
		kfree(node);
	}
	drm_mm_takedown(&mm);
	return ret;
}

static int igt_align32(void *ignored)
{
	return igt_align_pot(32);
}

static int igt_align64(void *ignored)
{
	return igt_align_pot(64);
}

static void show_scan(const struct drm_mm_scan *scan)
{
	pr_info("scan: hit [%llx, %llx], size=%lld, align=%lld, color=%ld\n",
		scan->hit_start, scan->hit_end,
		scan->size, scan->alignment, scan->color);
}

static void show_holes(const struct drm_mm *mm, int count)
{
	u64 hole_start, hole_end;
	struct drm_mm_node *hole;

	drm_mm_for_each_hole(hole, mm, hole_start, hole_end) {
		struct drm_mm_node *next = list_next_entry(hole, node_list);
		const char *node1 = NULL, *node2 = NULL;

		if (hole->allocated)
			node1 = kasprintf(GFP_KERNEL,
					  "[%llx + %lld, color=%ld], ",
					  hole->start, hole->size, hole->color);

		if (next->allocated)
			node2 = kasprintf(GFP_KERNEL,
					  ", [%llx + %lld, color=%ld]",
					  next->start, next->size, next->color);

		pr_info("%sHole [%llx - %llx, size %lld]%s\n",
			node1,
			hole_start, hole_end, hole_end - hole_start,
			node2);

		kfree(node2);
		kfree(node1);

		if (!--count)
			break;
	}
}

struct evict_node {
	struct drm_mm_node node;
	struct list_head link;
};

static bool evict_nodes(struct drm_mm_scan *scan,
			struct evict_node *nodes,
			unsigned int *order,
			unsigned int count,
			bool use_color,
			struct list_head *evict_list)
{
	struct evict_node *e, *en;
	unsigned int i;

	for (i = 0; i < count; i++) {
		e = &nodes[order ? order[i] : i];
		list_add(&e->link, evict_list);
		if (drm_mm_scan_add_block(scan, &e->node))
			break;
	}
	list_for_each_entry_safe(e, en, evict_list, link) {
		if (!drm_mm_scan_remove_block(scan, &e->node))
			list_del(&e->link);
	}
	if (list_empty(evict_list)) {
		pr_err("Failed to find eviction: size=%lld [avail=%d], align=%lld (color=%lu)\n",
		       scan->size, count, scan->alignment, scan->color);
		return false;
	}

	list_for_each_entry(e, evict_list, link)
		drm_mm_remove_node(&e->node);

	if (use_color) {
		struct drm_mm_node *node;

		while ((node = drm_mm_scan_color_evict(scan))) {
			e = container_of(node, typeof(*e), node);
			drm_mm_remove_node(&e->node);
			list_add(&e->link, evict_list);
		}
	} else {
		if (drm_mm_scan_color_evict(scan)) {
			pr_err("drm_mm_scan_color_evict unexpectedly reported overlapping nodes!\n");
			return false;
		}
	}

	return true;
}

static bool evict_nothing(struct drm_mm *mm,
			  unsigned int total_size,
			  struct evict_node *nodes)
{
	struct drm_mm_scan scan;
	LIST_HEAD(evict_list);
	struct evict_node *e;
	struct drm_mm_node *node;
	unsigned int n;

	drm_mm_scan_init(&scan, mm, 1, 0, 0, 0);
	for (n = 0; n < total_size; n++) {
		e = &nodes[n];
		list_add(&e->link, &evict_list);
		drm_mm_scan_add_block(&scan, &e->node);
	}
	list_for_each_entry(e, &evict_list, link)
		drm_mm_scan_remove_block(&scan, &e->node);

	for (n = 0; n < total_size; n++) {
		e = &nodes[n];

		if (!drm_mm_node_allocated(&e->node)) {
			pr_err("node[%d] no longer allocated!\n", n);
			return false;
		}

		e->link.next = NULL;
	}

	drm_mm_for_each_node(node, mm) {
		e = container_of(node, typeof(*e), node);
		e->link.next = &e->link;
	}

	for (n = 0; n < total_size; n++) {
		e = &nodes[n];

		if (!e->link.next) {
			pr_err("node[%d] no longer connected!\n", n);
			return false;
		}
	}

	return assert_continuous(mm, nodes[0].node.size);
}

static bool evict_everything(struct drm_mm *mm,
			     unsigned int total_size,
			     struct evict_node *nodes)
{
	struct drm_mm_scan scan;
	LIST_HEAD(evict_list);
	struct evict_node *e;
	unsigned int n;
	int err;

	drm_mm_scan_init(&scan, mm, total_size, 0, 0, 0);
	for (n = 0; n < total_size; n++) {
		e = &nodes[n];
		list_add(&e->link, &evict_list);
		if (drm_mm_scan_add_block(&scan, &e->node))
			break;
	}

	err = 0;
	list_for_each_entry(e, &evict_list, link) {
		if (!drm_mm_scan_remove_block(&scan, &e->node)) {
			if (!err) {
				pr_err("Node %lld not marked for eviction!\n",
				       e->node.start);
				err = -EINVAL;
			}
		}
	}
	if (err)
		return false;

	list_for_each_entry(e, &evict_list, link)
		drm_mm_remove_node(&e->node);

	if (!assert_one_hole(mm, 0, total_size))
		return false;

	list_for_each_entry(e, &evict_list, link) {
		err = drm_mm_reserve_node(mm, &e->node);
		if (err) {
			pr_err("Failed to reinsert node after eviction: start=%llx\n",
			       e->node.start);
			return false;
		}
	}

	return assert_continuous(mm, nodes[0].node.size);
}

static int evict_something(struct drm_mm *mm,
			   u64 range_start, u64 range_end,
			   struct evict_node *nodes,
			   unsigned int *order,
			   unsigned int count,
			   unsigned int size,
			   unsigned int alignment,
			   const struct insert_mode *mode)
{
	struct drm_mm_scan scan;
	LIST_HEAD(evict_list);
	struct evict_node *e;
	struct drm_mm_node tmp;
	int err;

	drm_mm_scan_init_with_range(&scan, mm,
				    size, alignment, 0,
				    range_start, range_end,
				    mode->create_flags);
	if (!evict_nodes(&scan,
			 nodes, order, count, false,
			 &evict_list))
		return -EINVAL;

	memset(&tmp, 0, sizeof(tmp));
	err = drm_mm_insert_node_generic(mm, &tmp, size, alignment, 0,
					 mode->search_flags,
					 mode->create_flags);
	if (err) {
		pr_err("Failed to insert into eviction hole: size=%d, align=%d\n",
		       size, alignment);
		show_scan(&scan);
		show_holes(mm, 3);
		return err;
	}

	if (tmp.start < range_start || tmp.start + tmp.size > range_end) {
		pr_err("Inserted [address=%llu + %llu] did not fit into the request range [%llu, %llu]\n",
		       tmp.start, tmp.size, range_start, range_end);
		err = -EINVAL;
	}

	if (!assert_node(&tmp, mm, size, alignment, 0) ||
	    drm_mm_hole_follows(&tmp)) {
		pr_err("Inserted did not fill the eviction hole: size=%lld [%d], align=%d [rem=%lld], start=%llx, hole-follows?=%d\n",
		       tmp.size, size,
		       alignment, misalignment(&tmp, alignment),
		       tmp.start, drm_mm_hole_follows(&tmp));
		err = -EINVAL;
	}

	drm_mm_remove_node(&tmp);
	if (err)
		return err;

	list_for_each_entry(e, &evict_list, link) {
		err = drm_mm_reserve_node(mm, &e->node);
		if (err) {
			pr_err("Failed to reinsert node after eviction: start=%llx\n",
			       e->node.start);
			return err;
		}
	}

	if (!assert_continuous(mm, nodes[0].node.size)) {
		pr_err("range is no longer continuous\n");
		return -EINVAL;
	}

	return 0;
}

static int igt_evict(void *ignored)
{
	DRM_RND_STATE(prng, random_seed);
	const unsigned int size = 8192;
	const struct insert_mode *mode;
	struct drm_mm mm;
	struct evict_node *nodes;
	struct drm_mm_node *node, *next;
	unsigned int *order, n;
	int ret, err;

	/* Here we populate a full drm_mm and then try and insert a new node
	 * by evicting other nodes in a random order. The drm_mm_scan should
	 * pick the first matching hole it finds from the random list. We
	 * repeat that for different allocation strategies, alignments and
	 * sizes to try and stress the hole finder.
	 */

	ret = -ENOMEM;
	nodes = vzalloc(size * sizeof(*nodes));
	if (!nodes)
		goto err;

	order = drm_random_order(size, &prng);
	if (!order)
		goto err_nodes;

	ret = -EINVAL;
	drm_mm_init(&mm, 0, size);
	for (n = 0; n < size; n++) {
		err = drm_mm_insert_node(&mm, &nodes[n].node, 1, 0,
					 DRM_MM_SEARCH_DEFAULT);
		if (err) {
			pr_err("insert failed, step %d\n", n);
			ret = err;
			goto out;
		}
	}

	/* First check that using the scanner doesn't break the mm */
	if (!evict_nothing(&mm, size, nodes)) {
		pr_err("evict_nothing() failed\n");
		goto out;
	}
	if (!evict_everything(&mm, size, nodes)) {
		pr_err("evict_everything() failed\n");
		goto out;
	}

	for (mode = evict_modes; mode->name; mode++) {
		for (n = 1; n <= size; n <<= 1) {
			drm_random_reorder(order, size, &prng);
			err = evict_something(&mm, 0, U64_MAX,
					      nodes, order, size,
					      n, 1,
					      mode);
			if (err) {
				pr_err("%s evict_something(size=%u) failed\n",
				       mode->name, n);
				ret = err;
				goto out;
			}
		}

		for (n = 1; n < size; n <<= 1) {
			drm_random_reorder(order, size, &prng);
			err = evict_something(&mm, 0, U64_MAX,
					      nodes, order, size,
					      size/2, n,
					      mode);
			if (err) {
				pr_err("%s evict_something(size=%u, alignment=%u) failed\n",
				       mode->name, size/2, n);
				ret = err;
				goto out;
			}
		}

		for_each_prime_number_from(n, 1, min(size, max_prime)) {
			unsigned int nsize = (size - n + 1) / 2;

			DRM_MM_BUG_ON(!nsize);

			drm_random_reorder(order, size, &prng);
			err = evict_something(&mm, 0, U64_MAX,
					      nodes, order, size,
					      nsize, n,
					      mode);
			if (err) {
				pr_err("%s evict_something(size=%u, alignment=%u) failed\n",
				       mode->name, nsize, n);
				ret = err;
				goto out;
			}
		}
	}

	ret = 0;
out:
	drm_mm_for_each_node_safe(node, next, &mm)
		drm_mm_remove_node(node);
	drm_mm_takedown(&mm);
	kfree(order);
err_nodes:
	vfree(nodes);
err:
	return ret;
}

static int igt_evict_range(void *ignored)
{
	DRM_RND_STATE(prng, random_seed);
	const unsigned int size = 8192;
	const unsigned int range_size = size / 2;
	const unsigned int range_start = size / 4;
	const unsigned int range_end = range_start + range_size;
	const struct insert_mode *mode;
	struct drm_mm mm;
	struct evict_node *nodes;
	struct drm_mm_node *node, *next;
	unsigned int *order, n;
	int ret, err;

	/* Like igt_evict() but now we are limiting the search to a
	 * small portion of the full drm_mm.
	 */

	ret = -ENOMEM;
	nodes = vzalloc(size * sizeof(*nodes));
	if (!nodes)
		goto err;

	order = drm_random_order(size, &prng);
	if (!order)
		goto err_nodes;

	ret = -EINVAL;
	drm_mm_init(&mm, 0, size);
	for (n = 0; n < size; n++) {
		err = drm_mm_insert_node(&mm, &nodes[n].node, 1, 0,
					 DRM_MM_SEARCH_DEFAULT);
		if (err) {
			pr_err("insert failed, step %d\n", n);
			ret = err;
			goto out;
		}
	}

	for (mode = evict_modes; mode->name; mode++) {
		for (n = 1; n <= range_size; n <<= 1) {
			drm_random_reorder(order, size, &prng);
			err = evict_something(&mm, range_start, range_end,
					      nodes, order, size,
					      n, 1,
					      mode);
			if (err) {
				pr_err("%s evict_something(size=%u) failed with range [%u, %u]\n",
				       mode->name, n, range_start, range_end);
				goto out;
			}
		}

		for (n = 1; n <= range_size; n <<= 1) {
			drm_random_reorder(order, size, &prng);
			err = evict_something(&mm, range_start, range_end,
					      nodes, order, size,
					      range_size/2, n,
					      mode);
			if (err) {
				pr_err("%s evict_something(size=%u, alignment=%u) failed with range [%u, %u]\n",
				       mode->name, range_size/2, n, range_start, range_end);
				goto out;
			}
		}

		for_each_prime_number_from(n, 1, min(range_size, max_prime)) {
			unsigned int nsize = (range_size - n + 1) / 2;

			DRM_MM_BUG_ON(!nsize);

			drm_random_reorder(order, size, &prng);
			err = evict_something(&mm, range_start, range_end,
					      nodes, order, size,
					      nsize, n,
					      mode);
			if (err) {
				pr_err("%s evict_something(size=%u, alignment=%u) failed with range [%u, %u]\n",
				       mode->name, nsize, n, range_start, range_end);
				goto out;
			}
		}
	}

	ret = 0;
out:
	drm_mm_for_each_node_safe(node, next, &mm)
		drm_mm_remove_node(node);
	drm_mm_takedown(&mm);
	kfree(order);
err_nodes:
	vfree(nodes);
err:
	return ret;
}

static unsigned int node_index(const struct drm_mm_node *node)
{
	return div64_u64(node->start, node->size);
}

static int igt_topdown(void *ignored)
{
	const struct insert_mode *topdown = &insert_modes[TOPDOWN];
	DRM_RND_STATE(prng, random_seed);
	const unsigned int count = 8192;
	unsigned int size;
	unsigned long *bitmap = NULL;
	struct drm_mm mm;
	struct drm_mm_node *nodes, *node, *next;
	unsigned int *order, n, m, o = 0;
	int ret;

	/* When allocating top-down, we expect to be returned a node
	 * from a suitable hole at the top of the drm_mm. We check that
	 * the returned node does match the highest available slot.
	 */

	ret = -ENOMEM;
	nodes = vzalloc(count * sizeof(*nodes));
	if (!nodes)
		goto err;

	bitmap = kzalloc(count / BITS_PER_LONG * sizeof(unsigned long),
			 GFP_TEMPORARY);
	if (!bitmap)
		goto err_nodes;

	order = drm_random_order(count, &prng);
	if (!order)
		goto err_bitmap;

	ret = -EINVAL;
	for (size = 1; size <= 64; size <<= 1) {
		drm_mm_init(&mm, 0, size*count);
		for (n = 0; n < count; n++) {
			if (!expect_insert(&mm, &nodes[n],
					   size, 0, n,
					   topdown)) {
				pr_err("insert failed, size %u step %d\n", size, n);
				goto out;
			}

			if (drm_mm_hole_follows(&nodes[n])) {
				pr_err("hole after topdown insert %d, start=%llx\n, size=%u",
				       n, nodes[n].start, size);
				goto out;
			}

			if (!assert_one_hole(&mm, 0, size*(count - n - 1)))
				goto out;
		}

		if (!assert_continuous(&mm, size))
			goto out;

		drm_random_reorder(order, count, &prng);
		for_each_prime_number_from(n, 1, min(count, max_prime)) {
			for (m = 0; m < n; m++) {
				node = &nodes[order[(o + m) % count]];
				drm_mm_remove_node(node);
				__set_bit(node_index(node), bitmap);
			}

			for (m = 0; m < n; m++) {
				unsigned int last;

				node = &nodes[order[(o + m) % count]];
				if (!expect_insert(&mm, node,
						   size, 0, 0,
						   topdown)) {
					pr_err("insert failed, step %d/%d\n", m, n);
					goto out;
				}

				if (drm_mm_hole_follows(node)) {
					pr_err("hole after topdown insert %d/%d, start=%llx\n",
					       m, n, node->start);
					goto out;
				}

				last = find_last_bit(bitmap, count);
				if (node_index(node) != last) {
					pr_err("node %d/%d, size %d, not inserted into upmost hole, expected %d, found %d\n",
					       m, n, size, last, node_index(node));
					goto out;
				}

				__clear_bit(last, bitmap);
			}

			DRM_MM_BUG_ON(find_first_bit(bitmap, count) != count);

			o += n;
		}

		drm_mm_for_each_node_safe(node, next, &mm)
			drm_mm_remove_node(node);
		DRM_MM_BUG_ON(!drm_mm_clean(&mm));
	}

	ret = 0;
out:
	drm_mm_for_each_node_safe(node, next, &mm)
		drm_mm_remove_node(node);
	drm_mm_takedown(&mm);
	kfree(order);
err_bitmap:
	kfree(bitmap);
err_nodes:
	vfree(nodes);
err:
	return ret;
}

static void separate_adjacent_colors(const struct drm_mm_node *node,
				     unsigned long color,
				     u64 *start,
				     u64 *end)
{
	if (node->allocated && node->color != color)
		++*start;

	node = list_next_entry(node, node_list);
	if (node->allocated && node->color != color)
		--*end;
}

static bool colors_abutt(const struct drm_mm_node *node)
{
	if (!drm_mm_hole_follows(node) &&
	    list_next_entry(node, node_list)->allocated) {
		pr_err("colors abutt; %ld [%llx + %llx] is next to %ld [%llx + %llx]!\n",
		       node->color, node->start, node->size,
		       list_next_entry(node, node_list)->color,
		       list_next_entry(node, node_list)->start,
		       list_next_entry(node, node_list)->size);
		return true;
	}

	return false;
}

static int igt_color(void *ignored)
{
	const unsigned int count = min(4096u, max_iterations);
	const struct insert_mode *mode;
	struct drm_mm mm;
	struct drm_mm_node *node, *nn;
	unsigned int n;
	int ret = -EINVAL, err;

	/* Color adjustment complicates everything. First we just check
	 * that when we insert a node we apply any color_adjustment callback.
	 * The callback we use should ensure that there is a gap between
	 * any two nodes, and so after each insertion we check that those
	 * holes are inserted and that they are preserved.
	 */

	drm_mm_init(&mm, 0, U64_MAX);

	for (n = 1; n <= count; n++) {
		node = kzalloc(sizeof(*node), GFP_KERNEL);
		if (!node) {
			ret = -ENOMEM;
			goto out;
		}

		if (!expect_insert(&mm, node,
				   n, 0, n,
				   &insert_modes[0])) {
			pr_err("insert failed, step %d\n", n);
			kfree(node);
			goto out;
		}
	}

	drm_mm_for_each_node_safe(node, nn, &mm) {
		if (node->color != node->size) {
			pr_err("invalid color stored: expected %lld, found %ld\n",
			       node->size, node->color);

			goto out;
		}

		drm_mm_remove_node(node);
		kfree(node);
	}

	/* Now, let's start experimenting with applying a color callback */
	mm.color_adjust = separate_adjacent_colors;
	for (mode = insert_modes; mode->name; mode++) {
		u64 last;

		node = kzalloc(sizeof(*node), GFP_KERNEL);
		if (!node) {
			ret = -ENOMEM;
			goto out;
		}

		node->size = 1 + 2*count;
		node->color = node->size;

		err = drm_mm_reserve_node(&mm, node);
		if (err) {
			pr_err("initial reserve failed!\n");
			ret = err;
			goto out;
		}

		last = node->start + node->size;

		for (n = 1; n <= count; n++) {
			int rem;

			node = kzalloc(sizeof(*node), GFP_KERNEL);
			if (!node) {
				ret = -ENOMEM;
				goto out;
			}

			node->start = last;
			node->size = n + count;
			node->color = node->size;

			err = drm_mm_reserve_node(&mm, node);
			if (err != -ENOSPC) {
				pr_err("reserve %d did not report color overlap! err=%d\n",
				       n, err);
				goto out;
			}

			node->start += n + 1;
			rem = misalignment(node, n + count);
			node->start += n + count - rem;

			err = drm_mm_reserve_node(&mm, node);
			if (err) {
				pr_err("reserve %d failed, err=%d\n", n, err);
				ret = err;
				goto out;
			}

			last = node->start + node->size;
		}

		for (n = 1; n <= count; n++) {
			node = kzalloc(sizeof(*node), GFP_KERNEL);
			if (!node) {
				ret = -ENOMEM;
				goto out;
			}

			if (!expect_insert(&mm, node,
					   n, n, n,
					   mode)) {
				pr_err("%s insert failed, step %d\n",
				       mode->name, n);
				kfree(node);
				goto out;
			}
		}

		drm_mm_for_each_node_safe(node, nn, &mm) {
			u64 rem;

			if (node->color != node->size) {
				pr_err("%s invalid color stored: expected %lld, found %ld\n",
				       mode->name, node->size, node->color);

				goto out;
			}

			if (colors_abutt(node))
				goto out;

			div64_u64_rem(node->start, node->size, &rem);
			if (rem) {
				pr_err("%s colored node misaligned, start=%llx expected alignment=%lld [rem=%lld]\n",
				       mode->name, node->start, node->size, rem);
				goto out;
			}

			drm_mm_remove_node(node);
			kfree(node);
		}
	}

	ret = 0;
out:
	drm_mm_for_each_node_safe(node, nn, &mm) {
		drm_mm_remove_node(node);
		kfree(node);
	}
	drm_mm_takedown(&mm);
	return ret;
}

static int evict_color(struct drm_mm *mm,
		       u64 range_start, u64 range_end,
		       struct evict_node *nodes,
		       unsigned int *order,
		       unsigned int count,
		       unsigned int size,
		       unsigned int alignment,
		       unsigned long color,
		       const struct insert_mode *mode)
{
	struct drm_mm_scan scan;
	LIST_HEAD(evict_list);
	struct evict_node *e;
	struct drm_mm_node tmp;
	int err;

	drm_mm_scan_init_with_range(&scan, mm,
				    size, alignment, color,
				    range_start, range_end,
				    mode->create_flags);
	if (!evict_nodes(&scan,
			 nodes, order, count, true,
			 &evict_list))
		return -EINVAL;

	memset(&tmp, 0, sizeof(tmp));
	err = drm_mm_insert_node_generic(mm, &tmp, size, alignment, color,
					 mode->search_flags,
					 mode->create_flags);
	if (err) {
		pr_err("Failed to insert into eviction hole: size=%d, align=%d, color=%lu, err=%d\n",
		       size, alignment, color, err);
		show_scan(&scan);
		show_holes(mm, 3);
		return err;
	}

	if (tmp.start < range_start || tmp.start + tmp.size > range_end) {
		pr_err("Inserted [address=%llu + %llu] did not fit into the request range [%llu, %llu]\n",
		       tmp.start, tmp.size, range_start, range_end);
		err = -EINVAL;
	}

	if (colors_abutt(&tmp))
		err = -EINVAL;

	if (!assert_node(&tmp, mm, size, alignment, color)) {
		pr_err("Inserted did not fit the eviction hole: size=%lld [%d], align=%d [rem=%lld], start=%llx\n",
		       tmp.size, size,
		       alignment, misalignment(&tmp, alignment), tmp.start);
		err = -EINVAL;
	}

	drm_mm_remove_node(&tmp);
	if (err)
		return err;

	list_for_each_entry(e, &evict_list, link) {
		err = drm_mm_reserve_node(mm, &e->node);
		if (err) {
			pr_err("Failed to reinsert node after eviction: start=%llx\n",
			       e->node.start);
			return err;
		}
	}

	return 0;
}

static int igt_color_evict(void *ignored)
{
	DRM_RND_STATE(prng, random_seed);
	const unsigned int total_size = min(8192u, max_iterations);
	const struct insert_mode *mode;
	unsigned long color = 0;
	struct drm_mm mm;
	struct evict_node *nodes;
	struct drm_mm_node *node, *next;
	unsigned int *order, n;
	int ret, err;

	/* Check that the drm_mm_scan also honours color adjustment when
	 * choosing its victims to create a hole. Our color_adjust does not
	 * allow two nodes to be placed together without an intervening hole
	 * enlarging the set of victims that must be evicted.
	 */

	ret = -ENOMEM;
	nodes = vzalloc(total_size * sizeof(*nodes));
	if (!nodes)
		goto err;

	order = drm_random_order(total_size, &prng);
	if (!order)
		goto err_nodes;

	ret = -EINVAL;
	drm_mm_init(&mm, 0, 2*total_size - 1);
	mm.color_adjust = separate_adjacent_colors;
	for (n = 0; n < total_size; n++) {
		if (!expect_insert(&mm, &nodes[n].node,
				   1, 0, color++,
				   &insert_modes[0])) {
			pr_err("insert failed, step %d\n", n);
			goto out;
		}
	}

	for (mode = evict_modes; mode->name; mode++) {
		for (n = 1; n <= total_size; n <<= 1) {
			drm_random_reorder(order, total_size, &prng);
			err = evict_color(&mm, 0, U64_MAX,
					  nodes, order, total_size,
					  n, 1, color++,
					  mode);
			if (err) {
				pr_err("%s evict_color(size=%u) failed\n",
				       mode->name, n);
				goto out;
			}
		}

		for (n = 1; n < total_size; n <<= 1) {
			drm_random_reorder(order, total_size, &prng);
			err = evict_color(&mm, 0, U64_MAX,
					  nodes, order, total_size,
					  total_size/2, n, color++,
					  mode);
			if (err) {
				pr_err("%s evict_color(size=%u, alignment=%u) failed\n",
				       mode->name, total_size/2, n);
				goto out;
			}
		}

		for_each_prime_number_from(n, 1, min(total_size, max_prime)) {
			unsigned int nsize = (total_size - n + 1) / 2;

			DRM_MM_BUG_ON(!nsize);

			drm_random_reorder(order, total_size, &prng);
			err = evict_color(&mm, 0, U64_MAX,
					  nodes, order, total_size,
					  nsize, n, color++,
					  mode);
			if (err) {
				pr_err("%s evict_color(size=%u, alignment=%u) failed\n",
				       mode->name, nsize, n);
				goto out;
			}
		}
	}

	ret = 0;
out:
	if (ret)
		show_mm(&mm);
	drm_mm_for_each_node_safe(node, next, &mm)
		drm_mm_remove_node(node);
	drm_mm_takedown(&mm);
	kfree(order);
err_nodes:
	vfree(nodes);
err:
	return ret;
}

static int igt_color_evict_range(void *ignored)
{
	DRM_RND_STATE(prng, random_seed);
	const unsigned int total_size = 8192;
	const unsigned int range_size = total_size / 2;
	const unsigned int range_start = total_size / 4;
	const unsigned int range_end = range_start + range_size;
	const struct insert_mode *mode;
	unsigned long color = 0;
	struct drm_mm mm;
	struct evict_node *nodes;
	struct drm_mm_node *node, *next;
	unsigned int *order, n;
	int ret, err;

	/* Like igt_color_evict(), but limited to small portion of the full
	 * drm_mm range.
	 */

	ret = -ENOMEM;
	nodes = vzalloc(total_size * sizeof(*nodes));
	if (!nodes)
		goto err;

	order = drm_random_order(total_size, &prng);
	if (!order)
		goto err_nodes;

	ret = -EINVAL;
	drm_mm_init(&mm, 0, 2*total_size - 1);
	mm.color_adjust = separate_adjacent_colors;
	for (n = 0; n < total_size; n++) {
		if (!expect_insert(&mm, &nodes[n].node,
				   1, 0, color++,
				   &insert_modes[0])) {
			pr_err("insert failed, step %d\n", n);
			goto out;
		}
	}

	for (mode = evict_modes; mode->name; mode++) {
		for (n = 1; n <= range_size; n <<= 1) {
			drm_random_reorder(order, range_size, &prng);
			err = evict_color(&mm, range_start, range_end,
					  nodes, order, total_size,
					  n, 1, color++,
					  mode);
			if (err) {
				pr_err("%s evict_color(size=%u) failed for range [%x, %x]\n",
				       mode->name, n, range_start, range_end);
				goto out;
			}
		}

		for (n = 1; n < range_size; n <<= 1) {
			drm_random_reorder(order, total_size, &prng);
			err = evict_color(&mm, range_start, range_end,
					  nodes, order, total_size,
					  range_size/2, n, color++,
					  mode);
			if (err) {
				pr_err("%s evict_color(size=%u, alignment=%u) failed for range [%x, %x]\n",
				       mode->name, total_size/2, n, range_start, range_end);
				goto out;
			}
		}

		for_each_prime_number_from(n, 1, min(range_size, max_prime)) {
			unsigned int nsize = (range_size - n + 1) / 2;

			DRM_MM_BUG_ON(!nsize);

			drm_random_reorder(order, total_size, &prng);
			err = evict_color(&mm, range_start, range_end,
					  nodes, order, total_size,
					  nsize, n, color++,
					  mode);
			if (err) {
				pr_err("%s evict_color(size=%u, alignment=%u) failed for range [%x, %x]\n",
				       mode->name, nsize, n, range_start, range_end);
				goto out;
			}
		}
	}

	ret = 0;
out:
	if (ret)
		show_mm(&mm);
	drm_mm_for_each_node_safe(node, next, &mm)
		drm_mm_remove_node(node);
	drm_mm_takedown(&mm);
	kfree(order);
err_nodes:
	vfree(nodes);
err:
	return ret;
}

#include "drm_selftest.c"

static int __init test_drm_mm_init(void)
{
	int err;

	while (!random_seed)
		random_seed = get_random_int();

	pr_info("Testing DRM range manger (struct drm_mm), with random_seed=0x%x max_iterations=%u max_prime=%u\n",
		random_seed, max_iterations, max_prime);
	err = run_selftests(selftests, ARRAY_SIZE(selftests), NULL);

	return err > 0 ? 0 : err;
}

static void __exit test_drm_mm_exit(void)
{
}

module_init(test_drm_mm_init);
module_exit(test_drm_mm_exit);

module_param(random_seed, uint, 0400);
module_param(max_iterations, uint, 0400);
module_param(max_prime, uint, 0400);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL");
