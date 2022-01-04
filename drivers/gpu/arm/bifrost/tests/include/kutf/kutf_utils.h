/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014, 2017, 2020-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _KERNEL_UTF_UTILS_H_
#define _KERNEL_UTF_UTILS_H_

/* kutf_utils.h
 * Utilities for the kernel UTF test infrastructure.
 *
 * This collection of library functions are provided for use by kernel UTF
 * and users of kernel UTF which don't directly fit within the other
 * code modules.
 */

#include <kutf/kutf_mem.h>

/**
 * Maximum size of the message strings within kernel UTF, messages longer then
 * this will be truncated.
 */
#define KUTF_MAX_DSPRINTF_LEN	1024

/**
 * kutf_dsprintf() - dynamic sprintf
 * @pool:	memory pool to allocate from
 * @fmt:	The format string describing the string to document.
 * @...		The parameters to feed in to the format string.
 *
 * This function implements sprintf which dynamically allocates memory to store
 * the string. The library will free the memory containing the string when the
 * result set is cleared or destroyed.
 *
 * Note The returned string may be truncated to fit an internal temporary
 * buffer, which is KUTF_MAX_DSPRINTF_LEN bytes in length.
 *
 * Return: Returns pointer to allocated string, or NULL on error.
 */
const char *kutf_dsprintf(struct kutf_mempool *pool,
		const char *fmt, ...) __printf(2, 3);


#endif	/* _KERNEL_UTF_UTILS_H_ */
