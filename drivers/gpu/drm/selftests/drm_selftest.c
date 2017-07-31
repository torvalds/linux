/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <linux/compiler.h>

#define selftest(name, func) __idx_##name,
enum {
#include TESTS
};
#undef selftest

#define selftest(n, f) [__idx_##n] = { .name = #n, .func = f },
static struct drm_selftest {
	bool enabled;
	const char *name;
	int (*func)(void *);
} selftests[] = {
#include TESTS
};
#undef selftest

/* Embed the line number into the parameter name so that we can order tests */
#define param(n) __PASTE(igt__, __PASTE(__PASTE(__LINE__, __), n))
#define selftest_0(n, func, id) \
module_param_named(id, selftests[__idx_##n].enabled, bool, 0400);
#define selftest(n, func) selftest_0(n, func, param(n))
#include TESTS
#undef selftest

static void set_default_test_all(struct drm_selftest *st, unsigned long count)
{
	unsigned long i;

	for (i = 0; i < count; i++)
		if (st[i].enabled)
			return;

	for (i = 0; i < count; i++)
		st[i].enabled = true;
}

static int run_selftests(struct drm_selftest *st,
			 unsigned long count,
			 void *data)
{
	int err = 0;

	set_default_test_all(st, count);

	/* Tests are listed in natural order in drm_*_selftests.h */
	for (; count--; st++) {
		if (!st->enabled)
			continue;

		pr_debug("drm: Running %s\n", st->name);
		err = st->func(data);
		if (err)
			break;
	}

	if (WARN(err > 0 || err == -ENOTTY,
		 "%s returned %d, conflicting with selftest's magic values!\n",
		 st->name, err))
		err = -1;

	rcu_barrier();
	return err;
}

static int __maybe_unused
__drm_subtests(const char *caller,
	       const struct drm_subtest *st,
	       int count,
	       void *data)
{
	int err;

	for (; count--; st++) {
		pr_debug("Running %s/%s\n", caller, st->name);
		err = st->func(data);
		if (err) {
			pr_err("%s: %s failed with error %d\n",
			       caller, st->name, err);
			return err;
		}
	}

	return 0;
}
