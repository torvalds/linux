// SPDX-License-Identifier: MIT

/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __SELFTEST_H__
#define __SELFTEST_H__

#include <linux/compiler.h>

#define selftest(name, func) int func(void);
#include "selftests.h"
#undef selftest

struct subtest {
	int (*func)(void *data);
	const char *name;
};

int __subtests(const char *caller,
	       const struct subtest *st,
	       int count,
	       void *data);
#define subtests(T, data) \
	__subtests(__func__, T, ARRAY_SIZE(T), data)

#define SUBTEST(x) { x, #x }

#endif /* __SELFTEST_H__ */
