/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef TEST_MODULES_H
#define TEST_MODULES_H

#define __REPEAT_10000_3(f, x) \
	f(x ## 0); \
	f(x ## 1); \
	f(x ## 2); \
	f(x ## 3); \
	f(x ## 4); \
	f(x ## 5); \
	f(x ## 6); \
	f(x ## 7); \
	f(x ## 8); \
	f(x ## 9)
#define __REPEAT_10000_2(f, x) \
	__REPEAT_10000_3(f, x ## 0); \
	__REPEAT_10000_3(f, x ## 1); \
	__REPEAT_10000_3(f, x ## 2); \
	__REPEAT_10000_3(f, x ## 3); \
	__REPEAT_10000_3(f, x ## 4); \
	__REPEAT_10000_3(f, x ## 5); \
	__REPEAT_10000_3(f, x ## 6); \
	__REPEAT_10000_3(f, x ## 7); \
	__REPEAT_10000_3(f, x ## 8); \
	__REPEAT_10000_3(f, x ## 9)
#define __REPEAT_10000_1(f, x) \
	__REPEAT_10000_2(f, x ## 0); \
	__REPEAT_10000_2(f, x ## 1); \
	__REPEAT_10000_2(f, x ## 2); \
	__REPEAT_10000_2(f, x ## 3); \
	__REPEAT_10000_2(f, x ## 4); \
	__REPEAT_10000_2(f, x ## 5); \
	__REPEAT_10000_2(f, x ## 6); \
	__REPEAT_10000_2(f, x ## 7); \
	__REPEAT_10000_2(f, x ## 8); \
	__REPEAT_10000_2(f, x ## 9)
#define REPEAT_10000(f) \
	__REPEAT_10000_1(f, 0); \
	__REPEAT_10000_1(f, 1); \
	__REPEAT_10000_1(f, 2); \
	__REPEAT_10000_1(f, 3); \
	__REPEAT_10000_1(f, 4); \
	__REPEAT_10000_1(f, 5); \
	__REPEAT_10000_1(f, 6); \
	__REPEAT_10000_1(f, 7); \
	__REPEAT_10000_1(f, 8); \
	__REPEAT_10000_1(f, 9)

#endif
