/* SPDX-License-Identifier: MIT */

/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>

#include "selftest.h"

enum {
#define selftest(n, func) __idx_##n,
#include "selftests.h"
#undef selftest
};

#define selftest(n, f) [__idx_##n] = { .name = #n, .func = f },
static struct selftest {
	bool enabled;
	const char *name;
	int (*func)(void);
} selftests[] = {
#include "selftests.h"
};
#undef selftest

/* Embed the line number into the parameter name so that we can order tests */
#define param(n) __PASTE(igt__, __PASTE(__PASTE(__LINE__, __), n))
#define selftest_0(n, func, id) \
module_param_named(id, selftests[__idx_##n].enabled, bool, 0400);
#define selftest(n, func) selftest_0(n, func, param(n))
#include "selftests.h"
#undef selftest

int __sanitycheck__(void)
{
	pr_debug("Hello World!\n");
	return 0;
}

static char *__st_filter;

static bool apply_subtest_filter(const char *caller, const char *name)
{
	char *filter, *sep, *tok;
	bool result = true;

	filter = kstrdup(__st_filter, GFP_KERNEL);
	for (sep = filter; (tok = strsep(&sep, ","));) {
		bool allow = true;
		char *sl;

		if (*tok == '!') {
			allow = false;
			tok++;
		}

		if (*tok == '\0')
			continue;

		sl = strchr(tok, '/');
		if (sl) {
			*sl++ = '\0';
			if (strcmp(tok, caller)) {
				if (allow)
					result = false;
				continue;
			}
			tok = sl;
		}

		if (strcmp(tok, name)) {
			if (allow)
				result = false;
			continue;
		}

		result = allow;
		break;
	}
	kfree(filter);

	return result;
}

int
__subtests(const char *caller, const struct subtest *st, int count, void *data)
{
	int err;

	for (; count--; st++) {
		cond_resched();
		if (signal_pending(current))
			return -EINTR;

		if (!apply_subtest_filter(caller, st->name))
			continue;

		pr_info("dma-buf: Running %s/%s\n", caller, st->name);

		err = st->func(data);
		if (err && err != -EINTR) {
			pr_err("dma-buf/%s: %s failed with error %d\n",
			       caller, st->name, err);
			return err;
		}
	}

	return 0;
}

static void set_default_test_all(struct selftest *st, unsigned long count)
{
	unsigned long i;

	for (i = 0; i < count; i++)
		if (st[i].enabled)
			return;

	for (i = 0; i < count; i++)
		st[i].enabled = true;
}

static int run_selftests(struct selftest *st, unsigned long count)
{
	int err = 0;

	set_default_test_all(st, count);

	/* Tests are listed in natural order in selftests.h */
	for (; count--; st++) {
		if (!st->enabled)
			continue;

		pr_info("dma-buf: Running %s\n", st->name);
		err = st->func();
		if (err)
			break;
	}

	if (WARN(err > 0 || err == -ENOTTY,
		 "%s returned %d, conflicting with selftest's magic values!\n",
		 st->name, err))
		err = -1;

	return err;
}

static int __init st_init(void)
{
	return run_selftests(selftests, ARRAY_SIZE(selftests));
}

static void __exit st_exit(void)
{
}

module_param_named(st_filter, __st_filter, charp, 0400);
module_init(st_init);
module_exit(st_exit);

MODULE_DESCRIPTION("Self-test harness for dma-buf");
MODULE_LICENSE("GPL and additional rights");
