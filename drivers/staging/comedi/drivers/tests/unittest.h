/* SPDX-License-Identifier: GPL-2.0+ */
/* vim: set ts=8 sw=8 noet tw=80 nowrap: */
/*
 *  comedi/drivers/tests/unittest.h
 *  Simple framework for unittests for comedi drivers.
 *
 *  COMEDI - Linux Control and Measurement Device Interface
 *  Copyright (C) 2016 Spencer E. Olson <olsonse@umich.edu>
 *  based of parts of drivers/of/unittest.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#ifndef _COMEDI_DRIVERS_TESTS_UNITTEST_H
#define _COMEDI_DRIVERS_TESTS_UNITTEST_H

static struct unittest_results {
	int passed;
	int failed;
} unittest_results;

typedef void (*unittest_fptr)(void);

#define unittest(result, fmt, ...) ({ \
	bool failed = !(result); \
	if (failed) { \
		++unittest_results.failed; \
		pr_err("FAIL %s():%i " fmt, __func__, __LINE__, \
		       ##__VA_ARGS__); \
	} else { \
		++unittest_results.passed; \
		pr_debug("pass %s():%i " fmt, __func__, __LINE__, \
			 ##__VA_ARGS__); \
	} \
	failed; \
})

/**
 * Execute an array of unit tests.
 * @name:	Name of set of unit tests--will be shown at INFO log level.
 * @unit_tests:	A null-terminated list of unit tests to execute.
 */
static inline void exec_unittests(const char *name,
				  const unittest_fptr *unit_tests)
{
	pr_info("begin comedi:\"%s\" unittests\n", name);

	for (; (*unit_tests) != NULL; ++unit_tests)
		(*unit_tests)();

	pr_info("end of comedi:\"%s\" unittests - %i passed, %i failed\n", name,
		unittest_results.passed, unittest_results.failed);
}

#endif /* _COMEDI_DRIVERS_TESTS_UNITTEST_H */
