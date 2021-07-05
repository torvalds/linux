// SPDX-License-Identifier: GPL-2.0+
/*
 *  comedi/drivers/tests/comedi_example_test.c
 *  Example set of unit tests.
 *
 *  COMEDI - Linux Control and Measurement Device Interface
 *  Copyright (C) 2016 Spencer E. Olson <olsonse@umich.edu>
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

#include <linux/module.h>

#include "unittest.h"

/* *** BEGIN fake board data *** */
struct comedi_device {
	const char *board_name;
	int item;
};

static struct comedi_device dev = {
	.board_name = "fake_device",
};

/* *** END fake board data *** */

/* *** BEGIN fake data init *** */
static void init_fake(void)
{
	dev.item = 10;
}

/* *** END fake data init *** */

static void test0(void)
{
	init_fake();
	unittest(dev.item != 11, "negative result\n");
	unittest(dev.item == 10, "positive result\n");
}

/* **** BEGIN simple module entry/exit functions **** */
static int __init unittest_enter(void)
{
	static const unittest_fptr unit_tests[] = {
		test0,
		NULL,
	};

	exec_unittests("example", unit_tests);
	return 0;
}

static void __exit unittest_exit(void) { }

module_init(unittest_enter);
module_exit(unittest_exit);

MODULE_AUTHOR("Spencer Olson <olsonse@umich.edu>");
MODULE_DESCRIPTION("Comedi unit-tests example");
MODULE_LICENSE("GPL");
/* **** END simple module entry/exit functions **** */
