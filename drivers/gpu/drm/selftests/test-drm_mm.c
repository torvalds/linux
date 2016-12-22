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
		if (hole->hole_follows) {
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

		if (node->hole_follows) {
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
		drm_mm_debug_table(&mm, __func__);
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

	drm_mm_debug_table(&mm, __func__);
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
