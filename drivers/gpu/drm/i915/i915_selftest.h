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

#ifndef __I915_SELFTEST_H__
#define __I915_SELFTEST_H__

#include <linux/types.h>

struct pci_dev;
struct drm_i915_private;

struct i915_selftest {
	unsigned long timeout_jiffies;
	unsigned int timeout_ms;
	unsigned int random_seed;
	char *filter;
	int mock;
	int live;
	int perf;
};

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include <linux/fault-inject.h>

extern struct i915_selftest i915_selftest;

int i915_mock_selftests(void);
int i915_live_selftests(struct pci_dev *pdev);
int i915_perf_selftests(struct pci_dev *pdev);

/* We extract the function declarations from i915_mock_selftests.h and
 * i915_live_selftests.h Add your unit test declarations there!
 *
 * Mock unit tests are run very early upon module load, before the driver
 * is probed. All hardware interactions, as well as other subsystems, must
 * be "mocked".
 *
 * Live unit tests are run after the driver is loaded - all hardware
 * interactions are real.
 */
#define selftest(name, func) int func(void);
#include "selftests/i915_mock_selftests.h"
#undef selftest
#define selftest(name, func) int func(struct drm_i915_private *i915);
#include "selftests/i915_live_selftests.h"
#include "selftests/i915_perf_selftests.h"
#undef selftest

struct i915_subtest {
	int (*func)(void *data);
	const char *name;
};

int __i915_nop_setup(void *data);
int __i915_nop_teardown(int err, void *data);

int __i915_live_setup(void *data);
int __i915_live_teardown(int err, void *data);

int __intel_gt_live_setup(void *data);
int __intel_gt_live_teardown(int err, void *data);

int __i915_subtests(const char *caller,
		    int (*setup)(void *data),
		    int (*teardown)(int err, void *data),
		    const struct i915_subtest *st,
		    unsigned int count,
		    void *data);
#define i915_subtests(T, data) \
	__i915_subtests(__func__, \
			__i915_nop_setup, __i915_nop_teardown, \
			T, ARRAY_SIZE(T), data)
#define i915_live_subtests(T, data) ({ \
	typecheck(struct drm_i915_private *, data); \
	__i915_subtests(__func__, \
			__i915_live_setup, __i915_live_teardown, \
			T, ARRAY_SIZE(T), data); \
})
#define intel_gt_live_subtests(T, data) ({ \
	typecheck(struct intel_gt *, data); \
	__i915_subtests(__func__, \
			__intel_gt_live_setup, __intel_gt_live_teardown, \
			T, ARRAY_SIZE(T), data); \
})

#define SUBTEST(x) { x, #x }

#define I915_SELFTEST_DECLARE(x) x
#define I915_SELFTEST_ONLY(x) unlikely(x)

#else /* !IS_ENABLED(CONFIG_DRM_I915_SELFTEST) */

static inline int i915_mock_selftests(void) { return 0; }
static inline int i915_live_selftests(struct pci_dev *pdev) { return 0; }
static inline int i915_perf_selftests(struct pci_dev *pdev) { return 0; }

#define I915_SELFTEST_DECLARE(x)
#define I915_SELFTEST_ONLY(x) 0

#endif

/* Using the i915_selftest_ prefix becomes a little unwieldy with the helpers.
 * Instead we use the igt_ shorthand, in reference to the intel-gpu-tools
 * suite of uabi test cases (which includes a test runner for our selftests).
 */

#define IGT_TIMEOUT(name__) \
	unsigned long name__ = jiffies + i915_selftest.timeout_jiffies

__printf(2, 3)
bool __igt_timeout(unsigned long timeout, const char *fmt, ...);

#define igt_timeout(t, fmt, ...) \
	__igt_timeout((t), KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)

void igt_hexdump(const void *buf, size_t len);

#endif /* !__I915_SELFTEST_H__ */
