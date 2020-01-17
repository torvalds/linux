// SPDX-License-Identifier: GPL-2.0-only
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
	BEST,
	BOTTOMUP,
	TOPDOWN,
	EVICT,
};

static const struct insert_mode {
	const char *name;
	enum drm_mm_insert_mode mode;
} insert_modes[] = {
	[BEST] = { "best", DRM_MM_INSERT_BEST },
	[BOTTOMUP] = { "bottom-up", DRM_MM_INSERT_LOW },
	[TOPDOWN] = { "top-down", DRM_MM_INSERT_HIGH },
	[EVICT] = { "evict", DRM_MM_INSERT_EVICT },
	{}
}, evict_modes[] = {
	{ "bottom-up", DRM_MM_INSERT_LOW },
	{ "top-down", DRM_MM_INSERT_HIGH },
	{}
};

static int igt_sanitycheck(void *igyesred)
{
	pr_info("%s - ok!\n", __func__);
	return 0;
}

static bool assert_yes_holes(const struct drm_mm *mm)
{
	struct drm_mm_yesde *hole;
	u64 hole_start, hole_end;
	unsigned long count;

	count = 0;
	drm_mm_for_each_hole(hole, mm, hole_start, hole_end)
		count++;
	if (count) {
		pr_err("Expected to find yes holes (after reserve), found %lu instead\n", count);
		return false;
	}

	drm_mm_for_each_yesde(hole, mm) {
		if (drm_mm_hole_follows(hole)) {
			pr_err("Hole follows yesde, expected yesne!\n");
			return false;
		}
	}

	return true;
}

static bool assert_one_hole(const struct drm_mm *mm, u64 start, u64 end)
{
	struct drm_mm_yesde *hole;
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
	struct drm_mm_yesde *yesde, *check, *found;
	unsigned long n;
	u64 addr;

	if (!assert_yes_holes(mm))
		return false;

	n = 0;
	addr = 0;
	drm_mm_for_each_yesde(yesde, mm) {
		if (yesde->start != addr) {
			pr_err("yesde[%ld] list out of order, expected %llx found %llx\n",
			       n, addr, yesde->start);
			return false;
		}

		if (yesde->size != size) {
			pr_err("yesde[%ld].size incorrect, expected %llx, found %llx\n",
			       n, size, yesde->size);
			return false;
		}

		if (drm_mm_hole_follows(yesde)) {
			pr_err("yesde[%ld] is followed by a hole!\n", n);
			return false;
		}

		found = NULL;
		drm_mm_for_each_yesde_in_range(check, mm, addr, addr + size) {
			if (yesde != check) {
				pr_err("lookup return wrong yesde, expected start %llx, found %llx\n",
				       yesde->start, check->start);
				return false;
			}
			found = check;
		}
		if (!found) {
			pr_err("lookup failed for yesde %llx + %llx\n",
			       addr, size);
			return false;
		}

		addr += size;
		n++;
	}

	return true;
}

static u64 misalignment(struct drm_mm_yesde *yesde, u64 alignment)
{
	u64 rem;

	if (!alignment)
		return 0;

	div64_u64_rem(yesde->start, alignment, &rem);
	return rem;
}

static bool assert_yesde(struct drm_mm_yesde *yesde, struct drm_mm *mm,
			u64 size, u64 alignment, unsigned long color)
{
	bool ok = true;

	if (!drm_mm_yesde_allocated(yesde) || yesde->mm != mm) {
		pr_err("yesde yest allocated\n");
		ok = false;
	}

	if (yesde->size != size) {
		pr_err("yesde has wrong size, found %llu, expected %llu\n",
		       yesde->size, size);
		ok = false;
	}

	if (misalignment(yesde, alignment)) {
		pr_err("yesde is misaligned, start %llx rem %llu, expected alignment %llu\n",
		       yesde->start, misalignment(yesde, alignment), alignment);
		ok = false;
	}

	if (yesde->color != color) {
		pr_err("yesde has wrong color, found %lu, expected %lu\n",
		       yesde->color, color);
		ok = false;
	}

	return ok;
}

#define show_mm(mm) do { \
	struct drm_printer __p = drm_debug_printer(__func__); \
	drm_mm_print((mm), &__p); } while (0)

static int igt_init(void *igyesred)
{
	const unsigned int size = 4096;
	struct drm_mm mm;
	struct drm_mm_yesde tmp;
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
		pr_err("mm claims yest to be initialized\n");
		goto out;
	}

	if (!drm_mm_clean(&mm)) {
		pr_err("mm yest empty on creation\n");
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
	ret = drm_mm_reserve_yesde(&mm, &tmp);
	if (ret) {
		pr_err("failed to reserve whole drm_mm\n");
		goto out;
	}

	/* After filling the range entirely, there should be yes holes */
	if (!assert_yes_holes(&mm)) {
		ret = -EINVAL;
		goto out;
	}

	/* And then after emptying it again, the massive hole should be back */
	drm_mm_remove_yesde(&tmp);
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

static int igt_debug(void *igyesred)
{
	struct drm_mm mm;
	struct drm_mm_yesde yesdes[2];
	int ret;

	/* Create a small drm_mm with a couple of yesdes and a few holes, and
	 * check that the debug iterator doesn't explode over a trivial drm_mm.
	 */

	drm_mm_init(&mm, 0, 4096);

	memset(yesdes, 0, sizeof(yesdes));
	yesdes[0].start = 512;
	yesdes[0].size = 1024;
	ret = drm_mm_reserve_yesde(&mm, &yesdes[0]);
	if (ret) {
		pr_err("failed to reserve yesde[0] {start=%lld, size=%lld)\n",
		       yesdes[0].start, yesdes[0].size);
		return ret;
	}

	yesdes[1].size = 1024;
	yesdes[1].start = 4096 - 512 - yesdes[1].size;
	ret = drm_mm_reserve_yesde(&mm, &yesdes[1]);
	if (ret) {
		pr_err("failed to reserve yesde[1] {start=%lld, size=%lld)\n",
		       yesdes[1].start, yesdes[1].size);
		return ret;
	}

	show_mm(&mm);
	return 0;
}

static struct drm_mm_yesde *set_yesde(struct drm_mm_yesde *yesde,
				    u64 start, u64 size)
{
	yesde->start = start;
	yesde->size = size;
	return yesde;
}

static bool expect_reserve_fail(struct drm_mm *mm, struct drm_mm_yesde *yesde)
{
	int err;

	err = drm_mm_reserve_yesde(mm, yesde);
	if (likely(err == -ENOSPC))
		return true;

	if (!err) {
		pr_err("impossible reserve succeeded, yesde %llu + %llu\n",
		       yesde->start, yesde->size);
		drm_mm_remove_yesde(yesde);
	} else {
		pr_err("impossible reserve failed with wrong error %d [expected %d], yesde %llu + %llu\n",
		       err, -ENOSPC, yesde->start, yesde->size);
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
	struct drm_mm_yesde tmp = {};
	int n;

	for (n = 0; n < ARRAY_SIZE(boundaries); n++) {
		if (!expect_reserve_fail(mm,
					 set_yesde(&tmp,
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
	struct drm_mm_yesde tmp, *yesdes, *yesde, *next;
	unsigned int *order, n, m, o = 0;
	int ret, err;

	/* For exercising drm_mm_reserve_yesde(), we want to check that
	 * reservations outside of the drm_mm range are rejected, and to
	 * overlapping and otherwise already occupied ranges. Afterwards,
	 * the tree and yesdes should be intact.
	 */

	DRM_MM_BUG_ON(!count);
	DRM_MM_BUG_ON(!size);

	ret = -ENOMEM;
	order = drm_random_order(count, &prng);
	if (!order)
		goto err;

	yesdes = vzalloc(array_size(count, sizeof(*yesdes)));
	if (!yesdes)
		goto err_order;

	ret = -EINVAL;
	drm_mm_init(&mm, 0, count * size);

	if (!check_reserve_boundaries(&mm, count, size))
		goto out;

	for (n = 0; n < count; n++) {
		yesdes[n].start = order[n] * size;
		yesdes[n].size = size;

		err = drm_mm_reserve_yesde(&mm, &yesdes[n]);
		if (err) {
			pr_err("reserve failed, step %d, start %llu\n",
			       n, yesdes[n].start);
			ret = err;
			goto out;
		}

		if (!drm_mm_yesde_allocated(&yesdes[n])) {
			pr_err("reserved yesde yest allocated! step %d, start %llu\n",
			       n, yesdes[n].start);
			goto out;
		}

		if (!expect_reserve_fail(&mm, &yesdes[n]))
			goto out;
	}

	/* After random insertion the yesdes should be in order */
	if (!assert_continuous(&mm, size))
		goto out;

	/* Repeated use should then fail */
	drm_random_reorder(order, count, &prng);
	for (n = 0; n < count; n++) {
		if (!expect_reserve_fail(&mm,
					 set_yesde(&tmp, order[n] * size, 1)))
			goto out;

		/* Remove and reinsert should work */
		drm_mm_remove_yesde(&yesdes[order[n]]);
		err = drm_mm_reserve_yesde(&mm, &yesdes[order[n]]);
		if (err) {
			pr_err("reserve failed, step %d, start %llu\n",
			       n, yesdes[n].start);
			ret = err;
			goto out;
		}
	}

	if (!assert_continuous(&mm, size))
		goto out;

	/* Overlapping use should then fail */
	for (n = 0; n < count; n++) {
		if (!expect_reserve_fail(&mm, set_yesde(&tmp, 0, size*count)))
			goto out;
	}
	for (n = 0; n < count; n++) {
		if (!expect_reserve_fail(&mm,
					 set_yesde(&tmp,
						  size * n,
						  size * (count - n))))
			goto out;
	}

	/* Remove several, reinsert, check full */
	for_each_prime_number(n, min(max_prime, count)) {
		for (m = 0; m < n; m++) {
			yesde = &yesdes[order[(o + m) % count]];
			drm_mm_remove_yesde(yesde);
		}

		for (m = 0; m < n; m++) {
			yesde = &yesdes[order[(o + m) % count]];
			err = drm_mm_reserve_yesde(&mm, yesde);
			if (err) {
				pr_err("reserve failed, step %d/%d, start %llu\n",
				       m, n, yesde->start);
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
	drm_mm_for_each_yesde_safe(yesde, next, &mm)
		drm_mm_remove_yesde(yesde);
	drm_mm_takedown(&mm);
	vfree(yesdes);
err_order:
	kfree(order);
err:
	return ret;
}

static int igt_reserve(void *igyesred)
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

		cond_resched();
	}

	return 0;
}

static bool expect_insert(struct drm_mm *mm, struct drm_mm_yesde *yesde,
			  u64 size, u64 alignment, unsigned long color,
			  const struct insert_mode *mode)
{
	int err;

	err = drm_mm_insert_yesde_generic(mm, yesde,
					 size, alignment, color,
					 mode->mode);
	if (err) {
		pr_err("insert (size=%llu, alignment=%llu, color=%lu, mode=%s) failed with err=%d\n",
		       size, alignment, color, mode->name, err);
		return false;
	}

	if (!assert_yesde(yesde, mm, size, alignment, color)) {
		drm_mm_remove_yesde(yesde);
		return false;
	}

	return true;
}

static bool expect_insert_fail(struct drm_mm *mm, u64 size)
{
	struct drm_mm_yesde tmp = {};
	int err;

	err = drm_mm_insert_yesde(mm, &tmp, size);
	if (likely(err == -ENOSPC))
		return true;

	if (!err) {
		pr_err("impossible insert succeeded, yesde %llu + %llu\n",
		       tmp.start, tmp.size);
		drm_mm_remove_yesde(&tmp);
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
	struct drm_mm_yesde *yesdes, *yesde, *next;
	unsigned int *order, n, m, o = 0;
	int ret;

	/* Fill a range with lots of yesdes, check it doesn't fail too early */

	DRM_MM_BUG_ON(!count);
	DRM_MM_BUG_ON(!size);

	ret = -ENOMEM;
	yesdes = vmalloc(array_size(count, sizeof(*yesdes)));
	if (!yesdes)
		goto err;

	order = drm_random_order(count, &prng);
	if (!order)
		goto err_yesdes;

	ret = -EINVAL;
	drm_mm_init(&mm, 0, count * size);

	for (mode = insert_modes; mode->name; mode++) {
		for (n = 0; n < count; n++) {
			struct drm_mm_yesde tmp;

			yesde = replace ? &tmp : &yesdes[n];
			memset(yesde, 0, sizeof(*yesde));
			if (!expect_insert(&mm, yesde, size, 0, n, mode)) {
				pr_err("%s insert failed, size %llu step %d\n",
				       mode->name, size, n);
				goto out;
			}

			if (replace) {
				drm_mm_replace_yesde(&tmp, &yesdes[n]);
				if (drm_mm_yesde_allocated(&tmp)) {
					pr_err("replaced old-yesde still allocated! step %d\n",
					       n);
					goto out;
				}

				if (!assert_yesde(&yesdes[n], &mm, size, 0, n)) {
					pr_err("replaced yesde did yest inherit parameters, size %llu step %d\n",
					       size, n);
					goto out;
				}

				if (tmp.start != yesdes[n].start) {
					pr_err("replaced yesde mismatch location expected [%llx + %llx], found [%llx + %llx]\n",
					       tmp.start, size,
					       yesdes[n].start, yesdes[n].size);
					goto out;
				}
			}
		}

		/* After random insertion the yesdes should be in order */
		if (!assert_continuous(&mm, size))
			goto out;

		/* Repeated use should then fail */
		if (!expect_insert_fail(&mm, size))
			goto out;

		/* Remove one and reinsert, as the only hole it should refill itself */
		for (n = 0; n < count; n++) {
			u64 addr = yesdes[n].start;

			drm_mm_remove_yesde(&yesdes[n]);
			if (!expect_insert(&mm, &yesdes[n], size, 0, n, mode)) {
				pr_err("%s reinsert failed, size %llu step %d\n",
				       mode->name, size, n);
				goto out;
			}

			if (yesdes[n].start != addr) {
				pr_err("%s reinsert yesde moved, step %d, expected %llx, found %llx\n",
				       mode->name, n, addr, yesdes[n].start);
				goto out;
			}

			if (!assert_continuous(&mm, size))
				goto out;
		}

		/* Remove several, reinsert, check full */
		for_each_prime_number(n, min(max_prime, count)) {
			for (m = 0; m < n; m++) {
				yesde = &yesdes[order[(o + m) % count]];
				drm_mm_remove_yesde(yesde);
			}

			for (m = 0; m < n; m++) {
				yesde = &yesdes[order[(o + m) % count]];
				if (!expect_insert(&mm, yesde, size, 0, n, mode)) {
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

		drm_mm_for_each_yesde_safe(yesde, next, &mm)
			drm_mm_remove_yesde(yesde);
		DRM_MM_BUG_ON(!drm_mm_clean(&mm));

		cond_resched();
	}

	ret = 0;
out:
	drm_mm_for_each_yesde_safe(yesde, next, &mm)
		drm_mm_remove_yesde(yesde);
	drm_mm_takedown(&mm);
	kfree(order);
err_yesdes:
	vfree(yesdes);
err:
	return ret;
}

static int igt_insert(void *igyesred)
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
		if (ret)
			return ret;

		cond_resched();
	}

	return 0;
}

static int igt_replace(void *igyesred)
{
	const unsigned int count = min_t(unsigned int, BIT(10), max_iterations);
	unsigned int n;
	int ret;

	/* Reuse igt_insert to exercise replacement by inserting a dummy yesde,
	 * then replacing it with the intended yesde. We want to check that
	 * the tree is intact and all the information we need is carried
	 * across to the target yesde.
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
		if (ret)
			return ret;

		cond_resched();
	}

	return 0;
}

static bool expect_insert_in_range(struct drm_mm *mm, struct drm_mm_yesde *yesde,
				   u64 size, u64 alignment, unsigned long color,
				   u64 range_start, u64 range_end,
				   const struct insert_mode *mode)
{
	int err;

	err = drm_mm_insert_yesde_in_range(mm, yesde,
					  size, alignment, color,
					  range_start, range_end,
					  mode->mode);
	if (err) {
		pr_err("insert (size=%llu, alignment=%llu, color=%lu, mode=%s) nto range [%llx, %llx] failed with err=%d\n",
		       size, alignment, color, mode->name,
		       range_start, range_end, err);
		return false;
	}

	if (!assert_yesde(yesde, mm, size, alignment, color)) {
		drm_mm_remove_yesde(yesde);
		return false;
	}

	return true;
}

static bool expect_insert_in_range_fail(struct drm_mm *mm,
					u64 size,
					u64 range_start,
					u64 range_end)
{
	struct drm_mm_yesde tmp = {};
	int err;

	err = drm_mm_insert_yesde_in_range(mm, &tmp,
					  size, 0, 0,
					  range_start, range_end,
					  0);
	if (likely(err == -ENOSPC))
		return true;

	if (!err) {
		pr_err("impossible insert succeeded, yesde %llx + %llu, range [%llx, %llx]\n",
		       tmp.start, tmp.size, range_start, range_end);
		drm_mm_remove_yesde(&tmp);
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
	struct drm_mm_yesde *yesde;
	unsigned int n;

	if (!expect_insert_in_range_fail(mm, size, start, end))
		return false;

	n = div64_u64(start + size - 1, size);
	drm_mm_for_each_yesde(yesde, mm) {
		if (yesde->start < start || yesde->start + yesde->size > end) {
			pr_err("yesde %d out of range, address [%llx + %llu], range [%llx, %llx]\n",
			       n, yesde->start, yesde->start + yesde->size, start, end);
			return false;
		}

		if (yesde->start != n * size) {
			pr_err("yesde %d out of order, expected start %llx, found %llx\n",
			       n, n * size, yesde->start);
			return false;
		}

		if (yesde->size != size) {
			pr_err("yesde %d has wrong size, expected size %llx, found %llx\n",
			       n, size, yesde->size);
			return false;
		}

		if (drm_mm_hole_follows(yesde) &&
		    drm_mm_hole_yesde_end(yesde) < end) {
			pr_err("yesde %d is followed by a hole!\n", n);
			return false;
		}

		n++;
	}

	if (start > 0) {
		yesde = __drm_mm_interval_first(mm, 0, start - 1);
		if (drm_mm_yesde_allocated(yesde)) {
			pr_err("yesde before start: yesde=%llx+%llu, start=%llx\n",
			       yesde->start, yesde->size, start);
			return false;
		}
	}

	if (end < U64_MAX) {
		yesde = __drm_mm_interval_first(mm, end, U64_MAX);
		if (drm_mm_yesde_allocated(yesde)) {
			pr_err("yesde after end: yesde=%llx+%llu, end=%llx\n",
			       yesde->start, yesde->size, end);
			return false;
		}
	}

	return true;
}

static int __igt_insert_range(unsigned int count, u64 size, u64 start, u64 end)
{
	const struct insert_mode *mode;
	struct drm_mm mm;
	struct drm_mm_yesde *yesdes, *yesde, *next;
	unsigned int n, start_n, end_n;
	int ret;

	DRM_MM_BUG_ON(!count);
	DRM_MM_BUG_ON(!size);
	DRM_MM_BUG_ON(end <= start);

	/* Very similar to __igt_insert(), but yesw instead of populating the
	 * full range of the drm_mm, we try to fill a small portion of it.
	 */

	ret = -ENOMEM;
	yesdes = vzalloc(array_size(count, sizeof(*yesdes)));
	if (!yesdes)
		goto err;

	ret = -EINVAL;
	drm_mm_init(&mm, 0, count * size);

	start_n = div64_u64(start + size - 1, size);
	end_n = div64_u64(end - size, size);

	for (mode = insert_modes; mode->name; mode++) {
		for (n = start_n; n <= end_n; n++) {
			if (!expect_insert_in_range(&mm, &yesdes[n],
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
			pr_err("%s: range [%llx, %llx] yest full after initialisation, size=%llu\n",
			       mode->name, start, end, size);
			goto out;
		}

		/* Remove one and reinsert, it should refill itself */
		for (n = start_n; n <= end_n; n++) {
			u64 addr = yesdes[n].start;

			drm_mm_remove_yesde(&yesdes[n]);
			if (!expect_insert_in_range(&mm, &yesdes[n],
						    size, size, n,
						    start, end, mode)) {
				pr_err("%s reinsert failed, step %d\n", mode->name, n);
				goto out;
			}

			if (yesdes[n].start != addr) {
				pr_err("%s reinsert yesde moved, step %d, expected %llx, found %llx\n",
				       mode->name, n, addr, yesdes[n].start);
				goto out;
			}
		}

		if (!assert_contiguous_in_range(&mm, size, start, end)) {
			pr_err("%s: range [%llx, %llx] yest full after reinsertion, size=%llu\n",
			       mode->name, start, end, size);
			goto out;
		}

		drm_mm_for_each_yesde_safe(yesde, next, &mm)
			drm_mm_remove_yesde(yesde);
		DRM_MM_BUG_ON(!drm_mm_clean(&mm));

		cond_resched();
	}

	ret = 0;
out:
	drm_mm_for_each_yesde_safe(yesde, next, &mm)
		drm_mm_remove_yesde(yesde);
	drm_mm_takedown(&mm);
	vfree(yesdes);
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

static int igt_insert_range(void *igyesred)
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

		cond_resched();
	}

	return 0;
}

static int igt_align(void *igyesred)
{
	const struct insert_mode *mode;
	const unsigned int max_count = min(8192u, max_prime);
	struct drm_mm mm;
	struct drm_mm_yesde *yesdes, *yesde, *next;
	unsigned int prime;
	int ret = -EINVAL;

	/* For each of the possible insertion modes, we pick a few
	 * arbitrary alignments and check that the inserted yesde
	 * meets our requirements.
	 */

	yesdes = vzalloc(array_size(max_count, sizeof(*yesdes)));
	if (!yesdes)
		goto err;

	drm_mm_init(&mm, 1, U64_MAX - 2);

	for (mode = insert_modes; mode->name; mode++) {
		unsigned int i = 0;

		for_each_prime_number_from(prime, 1, max_count) {
			u64 size = next_prime_number(prime);

			if (!expect_insert(&mm, &yesdes[i],
					   size, prime, i,
					   mode)) {
				pr_err("%s insert failed with alignment=%d",
				       mode->name, prime);
				goto out;
			}

			i++;
		}

		drm_mm_for_each_yesde_safe(yesde, next, &mm)
			drm_mm_remove_yesde(yesde);
		DRM_MM_BUG_ON(!drm_mm_clean(&mm));

		cond_resched();
	}

	ret = 0;
out:
	drm_mm_for_each_yesde_safe(yesde, next, &mm)
		drm_mm_remove_yesde(yesde);
	drm_mm_takedown(&mm);
	vfree(yesdes);
err:
	return ret;
}

static int igt_align_pot(int max)
{
	struct drm_mm mm;
	struct drm_mm_yesde *yesde, *next;
	int bit;
	int ret = -EINVAL;

	/* Check that we can align to the full u64 address space */

	drm_mm_init(&mm, 1, U64_MAX - 2);

	for (bit = max - 1; bit; bit--) {
		u64 align, size;

		yesde = kzalloc(sizeof(*yesde), GFP_KERNEL);
		if (!yesde) {
			ret = -ENOMEM;
			goto out;
		}

		align = BIT_ULL(bit);
		size = BIT_ULL(bit-1) + 1;
		if (!expect_insert(&mm, yesde,
				   size, align, bit,
				   &insert_modes[0])) {
			pr_err("insert failed with alignment=%llx [%d]",
			       align, bit);
			goto out;
		}

		cond_resched();
	}

	ret = 0;
out:
	drm_mm_for_each_yesde_safe(yesde, next, &mm) {
		drm_mm_remove_yesde(yesde);
		kfree(yesde);
	}
	drm_mm_takedown(&mm);
	return ret;
}

static int igt_align32(void *igyesred)
{
	return igt_align_pot(32);
}

static int igt_align64(void *igyesred)
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
	struct drm_mm_yesde *hole;

	drm_mm_for_each_hole(hole, mm, hole_start, hole_end) {
		struct drm_mm_yesde *next = list_next_entry(hole, yesde_list);
		const char *yesde1 = NULL, *yesde2 = NULL;

		if (drm_mm_yesde_allocated(hole))
			yesde1 = kasprintf(GFP_KERNEL,
					  "[%llx + %lld, color=%ld], ",
					  hole->start, hole->size, hole->color);

		if (drm_mm_yesde_allocated(next))
			yesde2 = kasprintf(GFP_KERNEL,
					  ", [%llx + %lld, color=%ld]",
					  next->start, next->size, next->color);

		pr_info("%sHole [%llx - %llx, size %lld]%s\n",
			yesde1,
			hole_start, hole_end, hole_end - hole_start,
			yesde2);

		kfree(yesde2);
		kfree(yesde1);

		if (!--count)
			break;
	}
}

struct evict_yesde {
	struct drm_mm_yesde yesde;
	struct list_head link;
};

static bool evict_yesdes(struct drm_mm_scan *scan,
			struct evict_yesde *yesdes,
			unsigned int *order,
			unsigned int count,
			bool use_color,
			struct list_head *evict_list)
{
	struct evict_yesde *e, *en;
	unsigned int i;

	for (i = 0; i < count; i++) {
		e = &yesdes[order ? order[i] : i];
		list_add(&e->link, evict_list);
		if (drm_mm_scan_add_block(scan, &e->yesde))
			break;
	}
	list_for_each_entry_safe(e, en, evict_list, link) {
		if (!drm_mm_scan_remove_block(scan, &e->yesde))
			list_del(&e->link);
	}
	if (list_empty(evict_list)) {
		pr_err("Failed to find eviction: size=%lld [avail=%d], align=%lld (color=%lu)\n",
		       scan->size, count, scan->alignment, scan->color);
		return false;
	}

	list_for_each_entry(e, evict_list, link)
		drm_mm_remove_yesde(&e->yesde);

	if (use_color) {
		struct drm_mm_yesde *yesde;

		while ((yesde = drm_mm_scan_color_evict(scan))) {
			e = container_of(yesde, typeof(*e), yesde);
			drm_mm_remove_yesde(&e->yesde);
			list_add(&e->link, evict_list);
		}
	} else {
		if (drm_mm_scan_color_evict(scan)) {
			pr_err("drm_mm_scan_color_evict unexpectedly reported overlapping yesdes!\n");
			return false;
		}
	}

	return true;
}

static bool evict_yesthing(struct drm_mm *mm,
			  unsigned int total_size,
			  struct evict_yesde *yesdes)
{
	struct drm_mm_scan scan;
	LIST_HEAD(evict_list);
	struct evict_yesde *e;
	struct drm_mm_yesde *yesde;
	unsigned int n;

	drm_mm_scan_init(&scan, mm, 1, 0, 0, 0);
	for (n = 0; n < total_size; n++) {
		e = &yesdes[n];
		list_add(&e->link, &evict_list);
		drm_mm_scan_add_block(&scan, &e->yesde);
	}
	list_for_each_entry(e, &evict_list, link)
		drm_mm_scan_remove_block(&scan, &e->yesde);

	for (n = 0; n < total_size; n++) {
		e = &yesdes[n];

		if (!drm_mm_yesde_allocated(&e->yesde)) {
			pr_err("yesde[%d] yes longer allocated!\n", n);
			return false;
		}

		e->link.next = NULL;
	}

	drm_mm_for_each_yesde(yesde, mm) {
		e = container_of(yesde, typeof(*e), yesde);
		e->link.next = &e->link;
	}

	for (n = 0; n < total_size; n++) {
		e = &yesdes[n];

		if (!e->link.next) {
			pr_err("yesde[%d] yes longer connected!\n", n);
			return false;
		}
	}

	return assert_continuous(mm, yesdes[0].yesde.size);
}

static bool evict_everything(struct drm_mm *mm,
			     unsigned int total_size,
			     struct evict_yesde *yesdes)
{
	struct drm_mm_scan scan;
	LIST_HEAD(evict_list);
	struct evict_yesde *e;
	unsigned int n;
	int err;

	drm_mm_scan_init(&scan, mm, total_size, 0, 0, 0);
	for (n = 0; n < total_size; n++) {
		e = &yesdes[n];
		list_add(&e->link, &evict_list);
		if (drm_mm_scan_add_block(&scan, &e->yesde))
			break;
	}

	err = 0;
	list_for_each_entry(e, &evict_list, link) {
		if (!drm_mm_scan_remove_block(&scan, &e->yesde)) {
			if (!err) {
				pr_err("Node %lld yest marked for eviction!\n",
				       e->yesde.start);
				err = -EINVAL;
			}
		}
	}
	if (err)
		return false;

	list_for_each_entry(e, &evict_list, link)
		drm_mm_remove_yesde(&e->yesde);

	if (!assert_one_hole(mm, 0, total_size))
		return false;

	list_for_each_entry(e, &evict_list, link) {
		err = drm_mm_reserve_yesde(mm, &e->yesde);
		if (err) {
			pr_err("Failed to reinsert yesde after eviction: start=%llx\n",
			       e->yesde.start);
			return false;
		}
	}

	return assert_continuous(mm, yesdes[0].yesde.size);
}

static int evict_something(struct drm_mm *mm,
			   u64 range_start, u64 range_end,
			   struct evict_yesde *yesdes,
			   unsigned int *order,
			   unsigned int count,
			   unsigned int size,
			   unsigned int alignment,
			   const struct insert_mode *mode)
{
	struct drm_mm_scan scan;
	LIST_HEAD(evict_list);
	struct evict_yesde *e;
	struct drm_mm_yesde tmp;
	int err;

	drm_mm_scan_init_with_range(&scan, mm,
				    size, alignment, 0,
				    range_start, range_end,
				    mode->mode);
	if (!evict_yesdes(&scan,
			 yesdes, order, count, false,
			 &evict_list))
		return -EINVAL;

	memset(&tmp, 0, sizeof(tmp));
	err = drm_mm_insert_yesde_generic(mm, &tmp, size, alignment, 0,
					 DRM_MM_INSERT_EVICT);
	if (err) {
		pr_err("Failed to insert into eviction hole: size=%d, align=%d\n",
		       size, alignment);
		show_scan(&scan);
		show_holes(mm, 3);
		return err;
	}

	if (tmp.start < range_start || tmp.start + tmp.size > range_end) {
		pr_err("Inserted [address=%llu + %llu] did yest fit into the request range [%llu, %llu]\n",
		       tmp.start, tmp.size, range_start, range_end);
		err = -EINVAL;
	}

	if (!assert_yesde(&tmp, mm, size, alignment, 0) ||
	    drm_mm_hole_follows(&tmp)) {
		pr_err("Inserted did yest fill the eviction hole: size=%lld [%d], align=%d [rem=%lld], start=%llx, hole-follows?=%d\n",
		       tmp.size, size,
		       alignment, misalignment(&tmp, alignment),
		       tmp.start, drm_mm_hole_follows(&tmp));
		err = -EINVAL;
	}

	drm_mm_remove_yesde(&tmp);
	if (err)
		return err;

	list_for_each_entry(e, &evict_list, link) {
		err = drm_mm_reserve_yesde(mm, &e->yesde);
		if (err) {
			pr_err("Failed to reinsert yesde after eviction: start=%llx\n",
			       e->yesde.start);
			return err;
		}
	}

	if (!assert_continuous(mm, yesdes[0].yesde.size)) {
		pr_err("range is yes longer continuous\n");
		return -EINVAL;
	}

	return 0;
}

static int igt_evict(void *igyesred)
{
	DRM_RND_STATE(prng, random_seed);
	const unsigned int size = 8192;
	const struct insert_mode *mode;
	struct drm_mm mm;
	struct evict_yesde *yesdes;
	struct drm_mm_yesde *yesde, *next;
	unsigned int *order, n;
	int ret, err;

	/* Here we populate a full drm_mm and then try and insert a new yesde
	 * by evicting other yesdes in a random order. The drm_mm_scan should
	 * pick the first matching hole it finds from the random list. We
	 * repeat that for different allocation strategies, alignments and
	 * sizes to try and stress the hole finder.
	 */

	ret = -ENOMEM;
	yesdes = vzalloc(array_size(size, sizeof(*yesdes)));
	if (!yesdes)
		goto err;

	order = drm_random_order(size, &prng);
	if (!order)
		goto err_yesdes;

	ret = -EINVAL;
	drm_mm_init(&mm, 0, size);
	for (n = 0; n < size; n++) {
		err = drm_mm_insert_yesde(&mm, &yesdes[n].yesde, 1);
		if (err) {
			pr_err("insert failed, step %d\n", n);
			ret = err;
			goto out;
		}
	}

	/* First check that using the scanner doesn't break the mm */
	if (!evict_yesthing(&mm, size, yesdes)) {
		pr_err("evict_yesthing() failed\n");
		goto out;
	}
	if (!evict_everything(&mm, size, yesdes)) {
		pr_err("evict_everything() failed\n");
		goto out;
	}

	for (mode = evict_modes; mode->name; mode++) {
		for (n = 1; n <= size; n <<= 1) {
			drm_random_reorder(order, size, &prng);
			err = evict_something(&mm, 0, U64_MAX,
					      yesdes, order, size,
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
					      yesdes, order, size,
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
					      yesdes, order, size,
					      nsize, n,
					      mode);
			if (err) {
				pr_err("%s evict_something(size=%u, alignment=%u) failed\n",
				       mode->name, nsize, n);
				ret = err;
				goto out;
			}
		}

		cond_resched();
	}

	ret = 0;
out:
	drm_mm_for_each_yesde_safe(yesde, next, &mm)
		drm_mm_remove_yesde(yesde);
	drm_mm_takedown(&mm);
	kfree(order);
err_yesdes:
	vfree(yesdes);
err:
	return ret;
}

static int igt_evict_range(void *igyesred)
{
	DRM_RND_STATE(prng, random_seed);
	const unsigned int size = 8192;
	const unsigned int range_size = size / 2;
	const unsigned int range_start = size / 4;
	const unsigned int range_end = range_start + range_size;
	const struct insert_mode *mode;
	struct drm_mm mm;
	struct evict_yesde *yesdes;
	struct drm_mm_yesde *yesde, *next;
	unsigned int *order, n;
	int ret, err;

	/* Like igt_evict() but yesw we are limiting the search to a
	 * small portion of the full drm_mm.
	 */

	ret = -ENOMEM;
	yesdes = vzalloc(array_size(size, sizeof(*yesdes)));
	if (!yesdes)
		goto err;

	order = drm_random_order(size, &prng);
	if (!order)
		goto err_yesdes;

	ret = -EINVAL;
	drm_mm_init(&mm, 0, size);
	for (n = 0; n < size; n++) {
		err = drm_mm_insert_yesde(&mm, &yesdes[n].yesde, 1);
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
					      yesdes, order, size,
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
					      yesdes, order, size,
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
					      yesdes, order, size,
					      nsize, n,
					      mode);
			if (err) {
				pr_err("%s evict_something(size=%u, alignment=%u) failed with range [%u, %u]\n",
				       mode->name, nsize, n, range_start, range_end);
				goto out;
			}
		}

		cond_resched();
	}

	ret = 0;
out:
	drm_mm_for_each_yesde_safe(yesde, next, &mm)
		drm_mm_remove_yesde(yesde);
	drm_mm_takedown(&mm);
	kfree(order);
err_yesdes:
	vfree(yesdes);
err:
	return ret;
}

static unsigned int yesde_index(const struct drm_mm_yesde *yesde)
{
	return div64_u64(yesde->start, yesde->size);
}

static int igt_topdown(void *igyesred)
{
	const struct insert_mode *topdown = &insert_modes[TOPDOWN];
	DRM_RND_STATE(prng, random_seed);
	const unsigned int count = 8192;
	unsigned int size;
	unsigned long *bitmap;
	struct drm_mm mm;
	struct drm_mm_yesde *yesdes, *yesde, *next;
	unsigned int *order, n, m, o = 0;
	int ret;

	/* When allocating top-down, we expect to be returned a yesde
	 * from a suitable hole at the top of the drm_mm. We check that
	 * the returned yesde does match the highest available slot.
	 */

	ret = -ENOMEM;
	yesdes = vzalloc(array_size(count, sizeof(*yesdes)));
	if (!yesdes)
		goto err;

	bitmap = bitmap_zalloc(count, GFP_KERNEL);
	if (!bitmap)
		goto err_yesdes;

	order = drm_random_order(count, &prng);
	if (!order)
		goto err_bitmap;

	ret = -EINVAL;
	for (size = 1; size <= 64; size <<= 1) {
		drm_mm_init(&mm, 0, size*count);
		for (n = 0; n < count; n++) {
			if (!expect_insert(&mm, &yesdes[n],
					   size, 0, n,
					   topdown)) {
				pr_err("insert failed, size %u step %d\n", size, n);
				goto out;
			}

			if (drm_mm_hole_follows(&yesdes[n])) {
				pr_err("hole after topdown insert %d, start=%llx\n, size=%u",
				       n, yesdes[n].start, size);
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
				yesde = &yesdes[order[(o + m) % count]];
				drm_mm_remove_yesde(yesde);
				__set_bit(yesde_index(yesde), bitmap);
			}

			for (m = 0; m < n; m++) {
				unsigned int last;

				yesde = &yesdes[order[(o + m) % count]];
				if (!expect_insert(&mm, yesde,
						   size, 0, 0,
						   topdown)) {
					pr_err("insert failed, step %d/%d\n", m, n);
					goto out;
				}

				if (drm_mm_hole_follows(yesde)) {
					pr_err("hole after topdown insert %d/%d, start=%llx\n",
					       m, n, yesde->start);
					goto out;
				}

				last = find_last_bit(bitmap, count);
				if (yesde_index(yesde) != last) {
					pr_err("yesde %d/%d, size %d, yest inserted into upmost hole, expected %d, found %d\n",
					       m, n, size, last, yesde_index(yesde));
					goto out;
				}

				__clear_bit(last, bitmap);
			}

			DRM_MM_BUG_ON(find_first_bit(bitmap, count) != count);

			o += n;
		}

		drm_mm_for_each_yesde_safe(yesde, next, &mm)
			drm_mm_remove_yesde(yesde);
		DRM_MM_BUG_ON(!drm_mm_clean(&mm));
		cond_resched();
	}

	ret = 0;
out:
	drm_mm_for_each_yesde_safe(yesde, next, &mm)
		drm_mm_remove_yesde(yesde);
	drm_mm_takedown(&mm);
	kfree(order);
err_bitmap:
	bitmap_free(bitmap);
err_yesdes:
	vfree(yesdes);
err:
	return ret;
}

static int igt_bottomup(void *igyesred)
{
	const struct insert_mode *bottomup = &insert_modes[BOTTOMUP];
	DRM_RND_STATE(prng, random_seed);
	const unsigned int count = 8192;
	unsigned int size;
	unsigned long *bitmap;
	struct drm_mm mm;
	struct drm_mm_yesde *yesdes, *yesde, *next;
	unsigned int *order, n, m, o = 0;
	int ret;

	/* Like igt_topdown, but instead of searching for the last hole,
	 * we search for the first.
	 */

	ret = -ENOMEM;
	yesdes = vzalloc(array_size(count, sizeof(*yesdes)));
	if (!yesdes)
		goto err;

	bitmap = bitmap_zalloc(count, GFP_KERNEL);
	if (!bitmap)
		goto err_yesdes;

	order = drm_random_order(count, &prng);
	if (!order)
		goto err_bitmap;

	ret = -EINVAL;
	for (size = 1; size <= 64; size <<= 1) {
		drm_mm_init(&mm, 0, size*count);
		for (n = 0; n < count; n++) {
			if (!expect_insert(&mm, &yesdes[n],
					   size, 0, n,
					   bottomup)) {
				pr_err("bottomup insert failed, size %u step %d\n", size, n);
				goto out;
			}

			if (!assert_one_hole(&mm, size*(n + 1), size*count))
				goto out;
		}

		if (!assert_continuous(&mm, size))
			goto out;

		drm_random_reorder(order, count, &prng);
		for_each_prime_number_from(n, 1, min(count, max_prime)) {
			for (m = 0; m < n; m++) {
				yesde = &yesdes[order[(o + m) % count]];
				drm_mm_remove_yesde(yesde);
				__set_bit(yesde_index(yesde), bitmap);
			}

			for (m = 0; m < n; m++) {
				unsigned int first;

				yesde = &yesdes[order[(o + m) % count]];
				if (!expect_insert(&mm, yesde,
						   size, 0, 0,
						   bottomup)) {
					pr_err("insert failed, step %d/%d\n", m, n);
					goto out;
				}

				first = find_first_bit(bitmap, count);
				if (yesde_index(yesde) != first) {
					pr_err("yesde %d/%d yest inserted into bottom hole, expected %d, found %d\n",
					       m, n, first, yesde_index(yesde));
					goto out;
				}
				__clear_bit(first, bitmap);
			}

			DRM_MM_BUG_ON(find_first_bit(bitmap, count) != count);

			o += n;
		}

		drm_mm_for_each_yesde_safe(yesde, next, &mm)
			drm_mm_remove_yesde(yesde);
		DRM_MM_BUG_ON(!drm_mm_clean(&mm));
		cond_resched();
	}

	ret = 0;
out:
	drm_mm_for_each_yesde_safe(yesde, next, &mm)
		drm_mm_remove_yesde(yesde);
	drm_mm_takedown(&mm);
	kfree(order);
err_bitmap:
	bitmap_free(bitmap);
err_yesdes:
	vfree(yesdes);
err:
	return ret;
}

static int __igt_once(unsigned int mode)
{
	struct drm_mm mm;
	struct drm_mm_yesde rsvd_lo, rsvd_hi, yesde;
	int err;

	drm_mm_init(&mm, 0, 7);

	memset(&rsvd_lo, 0, sizeof(rsvd_lo));
	rsvd_lo.start = 1;
	rsvd_lo.size = 1;
	err = drm_mm_reserve_yesde(&mm, &rsvd_lo);
	if (err) {
		pr_err("Could yest reserve low yesde\n");
		goto err;
	}

	memset(&rsvd_hi, 0, sizeof(rsvd_hi));
	rsvd_hi.start = 5;
	rsvd_hi.size = 1;
	err = drm_mm_reserve_yesde(&mm, &rsvd_hi);
	if (err) {
		pr_err("Could yest reserve low yesde\n");
		goto err_lo;
	}

	if (!drm_mm_hole_follows(&rsvd_lo) || !drm_mm_hole_follows(&rsvd_hi)) {
		pr_err("Expected a hole after lo and high yesdes!\n");
		err = -EINVAL;
		goto err_hi;
	}

	memset(&yesde, 0, sizeof(yesde));
	err = drm_mm_insert_yesde_generic(&mm, &yesde,
					 2, 0, 0,
					 mode | DRM_MM_INSERT_ONCE);
	if (!err) {
		pr_err("Unexpectedly inserted the yesde into the wrong hole: yesde.start=%llx\n",
		       yesde.start);
		err = -EINVAL;
		goto err_yesde;
	}

	err = drm_mm_insert_yesde_generic(&mm, &yesde, 2, 0, 0, mode);
	if (err) {
		pr_err("Could yest insert the yesde into the available hole!\n");
		err = -EINVAL;
		goto err_hi;
	}

err_yesde:
	drm_mm_remove_yesde(&yesde);
err_hi:
	drm_mm_remove_yesde(&rsvd_hi);
err_lo:
	drm_mm_remove_yesde(&rsvd_lo);
err:
	drm_mm_takedown(&mm);
	return err;
}

static int igt_lowest(void *igyesred)
{
	return __igt_once(DRM_MM_INSERT_LOW);
}

static int igt_highest(void *igyesred)
{
	return __igt_once(DRM_MM_INSERT_HIGH);
}

static void separate_adjacent_colors(const struct drm_mm_yesde *yesde,
				     unsigned long color,
				     u64 *start,
				     u64 *end)
{
	if (drm_mm_yesde_allocated(yesde) && yesde->color != color)
		++*start;

	yesde = list_next_entry(yesde, yesde_list);
	if (drm_mm_yesde_allocated(yesde) && yesde->color != color)
		--*end;
}

static bool colors_abutt(const struct drm_mm_yesde *yesde)
{
	if (!drm_mm_hole_follows(yesde) &&
	    drm_mm_yesde_allocated(list_next_entry(yesde, yesde_list))) {
		pr_err("colors abutt; %ld [%llx + %llx] is next to %ld [%llx + %llx]!\n",
		       yesde->color, yesde->start, yesde->size,
		       list_next_entry(yesde, yesde_list)->color,
		       list_next_entry(yesde, yesde_list)->start,
		       list_next_entry(yesde, yesde_list)->size);
		return true;
	}

	return false;
}

static int igt_color(void *igyesred)
{
	const unsigned int count = min(4096u, max_iterations);
	const struct insert_mode *mode;
	struct drm_mm mm;
	struct drm_mm_yesde *yesde, *nn;
	unsigned int n;
	int ret = -EINVAL, err;

	/* Color adjustment complicates everything. First we just check
	 * that when we insert a yesde we apply any color_adjustment callback.
	 * The callback we use should ensure that there is a gap between
	 * any two yesdes, and so after each insertion we check that those
	 * holes are inserted and that they are preserved.
	 */

	drm_mm_init(&mm, 0, U64_MAX);

	for (n = 1; n <= count; n++) {
		yesde = kzalloc(sizeof(*yesde), GFP_KERNEL);
		if (!yesde) {
			ret = -ENOMEM;
			goto out;
		}

		if (!expect_insert(&mm, yesde,
				   n, 0, n,
				   &insert_modes[0])) {
			pr_err("insert failed, step %d\n", n);
			kfree(yesde);
			goto out;
		}
	}

	drm_mm_for_each_yesde_safe(yesde, nn, &mm) {
		if (yesde->color != yesde->size) {
			pr_err("invalid color stored: expected %lld, found %ld\n",
			       yesde->size, yesde->color);

			goto out;
		}

		drm_mm_remove_yesde(yesde);
		kfree(yesde);
	}

	/* Now, let's start experimenting with applying a color callback */
	mm.color_adjust = separate_adjacent_colors;
	for (mode = insert_modes; mode->name; mode++) {
		u64 last;

		yesde = kzalloc(sizeof(*yesde), GFP_KERNEL);
		if (!yesde) {
			ret = -ENOMEM;
			goto out;
		}

		yesde->size = 1 + 2*count;
		yesde->color = yesde->size;

		err = drm_mm_reserve_yesde(&mm, yesde);
		if (err) {
			pr_err("initial reserve failed!\n");
			ret = err;
			goto out;
		}

		last = yesde->start + yesde->size;

		for (n = 1; n <= count; n++) {
			int rem;

			yesde = kzalloc(sizeof(*yesde), GFP_KERNEL);
			if (!yesde) {
				ret = -ENOMEM;
				goto out;
			}

			yesde->start = last;
			yesde->size = n + count;
			yesde->color = yesde->size;

			err = drm_mm_reserve_yesde(&mm, yesde);
			if (err != -ENOSPC) {
				pr_err("reserve %d did yest report color overlap! err=%d\n",
				       n, err);
				goto out;
			}

			yesde->start += n + 1;
			rem = misalignment(yesde, n + count);
			yesde->start += n + count - rem;

			err = drm_mm_reserve_yesde(&mm, yesde);
			if (err) {
				pr_err("reserve %d failed, err=%d\n", n, err);
				ret = err;
				goto out;
			}

			last = yesde->start + yesde->size;
		}

		for (n = 1; n <= count; n++) {
			yesde = kzalloc(sizeof(*yesde), GFP_KERNEL);
			if (!yesde) {
				ret = -ENOMEM;
				goto out;
			}

			if (!expect_insert(&mm, yesde,
					   n, n, n,
					   mode)) {
				pr_err("%s insert failed, step %d\n",
				       mode->name, n);
				kfree(yesde);
				goto out;
			}
		}

		drm_mm_for_each_yesde_safe(yesde, nn, &mm) {
			u64 rem;

			if (yesde->color != yesde->size) {
				pr_err("%s invalid color stored: expected %lld, found %ld\n",
				       mode->name, yesde->size, yesde->color);

				goto out;
			}

			if (colors_abutt(yesde))
				goto out;

			div64_u64_rem(yesde->start, yesde->size, &rem);
			if (rem) {
				pr_err("%s colored yesde misaligned, start=%llx expected alignment=%lld [rem=%lld]\n",
				       mode->name, yesde->start, yesde->size, rem);
				goto out;
			}

			drm_mm_remove_yesde(yesde);
			kfree(yesde);
		}

		cond_resched();
	}

	ret = 0;
out:
	drm_mm_for_each_yesde_safe(yesde, nn, &mm) {
		drm_mm_remove_yesde(yesde);
		kfree(yesde);
	}
	drm_mm_takedown(&mm);
	return ret;
}

static int evict_color(struct drm_mm *mm,
		       u64 range_start, u64 range_end,
		       struct evict_yesde *yesdes,
		       unsigned int *order,
		       unsigned int count,
		       unsigned int size,
		       unsigned int alignment,
		       unsigned long color,
		       const struct insert_mode *mode)
{
	struct drm_mm_scan scan;
	LIST_HEAD(evict_list);
	struct evict_yesde *e;
	struct drm_mm_yesde tmp;
	int err;

	drm_mm_scan_init_with_range(&scan, mm,
				    size, alignment, color,
				    range_start, range_end,
				    mode->mode);
	if (!evict_yesdes(&scan,
			 yesdes, order, count, true,
			 &evict_list))
		return -EINVAL;

	memset(&tmp, 0, sizeof(tmp));
	err = drm_mm_insert_yesde_generic(mm, &tmp, size, alignment, color,
					 DRM_MM_INSERT_EVICT);
	if (err) {
		pr_err("Failed to insert into eviction hole: size=%d, align=%d, color=%lu, err=%d\n",
		       size, alignment, color, err);
		show_scan(&scan);
		show_holes(mm, 3);
		return err;
	}

	if (tmp.start < range_start || tmp.start + tmp.size > range_end) {
		pr_err("Inserted [address=%llu + %llu] did yest fit into the request range [%llu, %llu]\n",
		       tmp.start, tmp.size, range_start, range_end);
		err = -EINVAL;
	}

	if (colors_abutt(&tmp))
		err = -EINVAL;

	if (!assert_yesde(&tmp, mm, size, alignment, color)) {
		pr_err("Inserted did yest fit the eviction hole: size=%lld [%d], align=%d [rem=%lld], start=%llx\n",
		       tmp.size, size,
		       alignment, misalignment(&tmp, alignment), tmp.start);
		err = -EINVAL;
	}

	drm_mm_remove_yesde(&tmp);
	if (err)
		return err;

	list_for_each_entry(e, &evict_list, link) {
		err = drm_mm_reserve_yesde(mm, &e->yesde);
		if (err) {
			pr_err("Failed to reinsert yesde after eviction: start=%llx\n",
			       e->yesde.start);
			return err;
		}
	}

	cond_resched();
	return 0;
}

static int igt_color_evict(void *igyesred)
{
	DRM_RND_STATE(prng, random_seed);
	const unsigned int total_size = min(8192u, max_iterations);
	const struct insert_mode *mode;
	unsigned long color = 0;
	struct drm_mm mm;
	struct evict_yesde *yesdes;
	struct drm_mm_yesde *yesde, *next;
	unsigned int *order, n;
	int ret, err;

	/* Check that the drm_mm_scan also hoyesurs color adjustment when
	 * choosing its victims to create a hole. Our color_adjust does yest
	 * allow two yesdes to be placed together without an intervening hole
	 * enlarging the set of victims that must be evicted.
	 */

	ret = -ENOMEM;
	yesdes = vzalloc(array_size(total_size, sizeof(*yesdes)));
	if (!yesdes)
		goto err;

	order = drm_random_order(total_size, &prng);
	if (!order)
		goto err_yesdes;

	ret = -EINVAL;
	drm_mm_init(&mm, 0, 2*total_size - 1);
	mm.color_adjust = separate_adjacent_colors;
	for (n = 0; n < total_size; n++) {
		if (!expect_insert(&mm, &yesdes[n].yesde,
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
					  yesdes, order, total_size,
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
					  yesdes, order, total_size,
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
					  yesdes, order, total_size,
					  nsize, n, color++,
					  mode);
			if (err) {
				pr_err("%s evict_color(size=%u, alignment=%u) failed\n",
				       mode->name, nsize, n);
				goto out;
			}
		}

		cond_resched();
	}

	ret = 0;
out:
	if (ret)
		show_mm(&mm);
	drm_mm_for_each_yesde_safe(yesde, next, &mm)
		drm_mm_remove_yesde(yesde);
	drm_mm_takedown(&mm);
	kfree(order);
err_yesdes:
	vfree(yesdes);
err:
	return ret;
}

static int igt_color_evict_range(void *igyesred)
{
	DRM_RND_STATE(prng, random_seed);
	const unsigned int total_size = 8192;
	const unsigned int range_size = total_size / 2;
	const unsigned int range_start = total_size / 4;
	const unsigned int range_end = range_start + range_size;
	const struct insert_mode *mode;
	unsigned long color = 0;
	struct drm_mm mm;
	struct evict_yesde *yesdes;
	struct drm_mm_yesde *yesde, *next;
	unsigned int *order, n;
	int ret, err;

	/* Like igt_color_evict(), but limited to small portion of the full
	 * drm_mm range.
	 */

	ret = -ENOMEM;
	yesdes = vzalloc(array_size(total_size, sizeof(*yesdes)));
	if (!yesdes)
		goto err;

	order = drm_random_order(total_size, &prng);
	if (!order)
		goto err_yesdes;

	ret = -EINVAL;
	drm_mm_init(&mm, 0, 2*total_size - 1);
	mm.color_adjust = separate_adjacent_colors;
	for (n = 0; n < total_size; n++) {
		if (!expect_insert(&mm, &yesdes[n].yesde,
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
					  yesdes, order, total_size,
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
					  yesdes, order, total_size,
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
					  yesdes, order, total_size,
					  nsize, n, color++,
					  mode);
			if (err) {
				pr_err("%s evict_color(size=%u, alignment=%u) failed for range [%x, %x]\n",
				       mode->name, nsize, n, range_start, range_end);
				goto out;
			}
		}

		cond_resched();
	}

	ret = 0;
out:
	if (ret)
		show_mm(&mm);
	drm_mm_for_each_yesde_safe(yesde, next, &mm)
		drm_mm_remove_yesde(yesde);
	drm_mm_takedown(&mm);
	kfree(order);
err_yesdes:
	vfree(yesdes);
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
