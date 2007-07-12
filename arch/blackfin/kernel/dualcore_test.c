/*
 * File:         arch/blackfin/kernel/dualcore_test.c
 * Based on:
 * Author:
 *
 * Created:
 * Description:  Small test code for CoreB on a BF561
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/init.h>
#include <linux/module.h>

static int *testarg = (int *)0xfeb00000;

static int test_init(void)
{
	*testarg = 1;
	printk(KERN_INFO "Dual core test module inserted: set testarg = [%d]\n @ [%p]\n",
	       *testarg, testarg);
	return 0;
}

static void test_exit(void)
{
	printk(KERN_INFO "Dual core test module removed: testarg = [%d]\n", *testarg);
}

module_init(test_init);
module_exit(test_exit);
