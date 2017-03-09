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

#include <linux/random.h>

#include "../i915_drv.h"
#include "../i915_selftest.h"

struct i915_selftest i915_selftest __read_mostly = {
	.timeout_ms = 1000,
};

int i915_mock_sanitycheck(void)
{
	pr_info(DRIVER_NAME ": %s() - ok!\n", __func__);
	return 0;
}

int i915_live_sanitycheck(struct drm_i915_private *i915)
{
	pr_info("%s: %s() - ok!\n", i915->drm.driver->name, __func__);
	return 0;
}

enum {
#define selftest(name, func) mock_##name,
#include "i915_mock_selftests.h"
#undef selftest
};

enum {
#define selftest(name, func) live_##name,
#include "i915_live_selftests.h"
#undef selftest
};

struct selftest {
	bool enabled;
	const char *name;
	union {
		int (*mock)(void);
		int (*live)(struct drm_i915_private *);
	};
};

#define selftest(n, f) [mock_##n] = { .name = #n, { .mock = f } },
static struct selftest mock_selftests[] = {
#include "i915_mock_selftests.h"
};
#undef selftest

#define selftest(n, f) [live_##n] = { .name = #n, { .live = f } },
static struct selftest live_selftests[] = {
#include "i915_live_selftests.h"
};
#undef selftest

/* Embed the line number into the parameter name so that we can order tests */
#define selftest(n, func) selftest_0(n, func, param(n))
#define param(n) __PASTE(igt__, __PASTE(__LINE__, __mock_##n))
#define selftest_0(n, func, id) \
module_param_named(id, mock_selftests[mock_##n].enabled, bool, 0400);
#include "i915_mock_selftests.h"
#undef selftest_0
#undef param

#define param(n) __PASTE(igt__, __PASTE(__LINE__, __live_##n))
#define selftest_0(n, func, id) \
module_param_named(id, live_selftests[live_##n].enabled, bool, 0400);
#include "i915_live_selftests.h"
#undef selftest_0
#undef param
#undef selftest

static void set_default_test_all(struct selftest *st, unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		if (st[i].enabled)
			return;

	for (i = 0; i < count; i++)
		st[i].enabled = true;
}

static int __run_selftests(const char *name,
			   struct selftest *st,
			   unsigned int count,
			   void *data)
{
	int err = 0;

	while (!i915_selftest.random_seed)
		i915_selftest.random_seed = get_random_int();

	i915_selftest.timeout_jiffies =
		i915_selftest.timeout_ms ?
		msecs_to_jiffies_timeout(i915_selftest.timeout_ms) :
		MAX_SCHEDULE_TIMEOUT;

	set_default_test_all(st, count);

	pr_info(DRIVER_NAME ": Performing %s selftests with st_random_seed=0x%x st_timeout=%u\n",
		name, i915_selftest.random_seed, i915_selftest.timeout_ms);

	/* Tests are listed in order in i915_*_selftests.h */
	for (; count--; st++) {
		if (!st->enabled)
			continue;

		cond_resched();
		if (signal_pending(current))
			return -EINTR;

		pr_debug(DRIVER_NAME ": Running %s\n", st->name);
		if (data)
			err = st->live(data);
		else
			err = st->mock();
		if (err == -EINTR && !signal_pending(current))
			err = 0;
		if (err)
			break;
	}

	if (WARN(err > 0 || err == -ENOTTY,
		 "%s returned %d, conflicting with selftest's magic values!\n",
		 st->name, err))
		err = -1;

	return err;
}

#define run_selftests(x, data) \
	__run_selftests(#x, x##_selftests, ARRAY_SIZE(x##_selftests), data)

int i915_mock_selftests(void)
{
	int err;

	if (!i915_selftest.mock)
		return 0;

	err = run_selftests(mock, NULL);
	if (err) {
		i915_selftest.mock = err;
		return err;
	}

	if (i915_selftest.mock < 0) {
		i915_selftest.mock = -ENOTTY;
		return 1;
	}

	return 0;
}

int i915_live_selftests(struct pci_dev *pdev)
{
	int err;

	if (!i915_selftest.live)
		return 0;

	err = run_selftests(live, to_i915(pci_get_drvdata(pdev)));
	if (err) {
		i915_selftest.live = err;
		return err;
	}

	if (i915_selftest.live < 0) {
		i915_selftest.live = -ENOTTY;
		return 1;
	}

	return 0;
}

int __i915_subtests(const char *caller,
		    const struct i915_subtest *st,
		    unsigned int count,
		    void *data)
{
	int err;

	for (; count--; st++) {
		cond_resched();
		if (signal_pending(current))
			return -EINTR;

		pr_debug(DRIVER_NAME ": Running %s/%s\n", caller, st->name);
		err = st->func(data);
		if (err && err != -EINTR) {
			pr_err(DRIVER_NAME "/%s: %s failed with error %d\n",
			       caller, st->name, err);
			return err;
		}
	}

	return 0;
}

bool __igt_timeout(unsigned long timeout, const char *fmt, ...)
{
	va_list va;

	if (!signal_pending(current)) {
		cond_resched();
		if (time_before(jiffies, timeout))
			return false;
	}

	if (fmt) {
		va_start(va, fmt);
		vprintk(fmt, va);
		va_end(va);
	}

	return true;
}

module_param_named(st_random_seed, i915_selftest.random_seed, uint, 0400);
module_param_named(st_timeout, i915_selftest.timeout_ms, uint, 0400);

module_param_named_unsafe(mock_selftests, i915_selftest.mock, int, 0400);
MODULE_PARM_DESC(mock_selftests, "Run selftests before loading, using mock hardware (0:disabled [default], 1:run tests then load driver, -1:run tests then exit module)");

module_param_named_unsafe(live_selftests, i915_selftest.live, int, 0400);
MODULE_PARM_DESC(live_selftests, "Run selftests after driver initialisation on the live system (0:disabled [default], 1:run tests then continue, -1:run tests then exit module)");
