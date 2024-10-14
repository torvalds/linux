/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __ASSERT_SUPPORT_H_INCLUDED__
#define __ASSERT_SUPPORT_H_INCLUDED__

/* Compile time assertion */
#ifndef CT_ASSERT
#define CT_ASSERT(cnd) ((void)sizeof(char[(cnd) ? 1 :  -1]))
#endif /* CT_ASSERT */

#include <linux/bug.h>

/* TODO: it would be cleaner to use this:
 * #define assert(cnd) BUG_ON(cnd)
 * but that causes many compiler warnings (==errors) under Android
 * because it seems that the BUG_ON() macro is not seen as a check by
 * gcc like the BUG() macro is.
 */
#define assert(cnd) \
	do { \
		if (!(cnd)) \
			BUG(); \
	} while (0)

#ifndef PIPE_GENERATION
/* Deprecated OP___assert, this is still used in ~1000 places
 * in the code. This will be removed over time.
 * The implementation for the pipe generation tool is in see support.isp.h
 */
#define OP___assert(cnd) assert(cnd)

static inline void compile_time_assert(unsigned int cond)
{
	/* Call undefined function if cond is false */
	void _compile_time_assert(void);
	if (!cond) _compile_time_assert();
}
#endif /* PIPE_GENERATION */

#endif /* __ASSERT_SUPPORT_H_INCLUDED__ */
