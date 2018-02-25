/*
 *
 * (C) COPYRIGHT 2014 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */

/**
 * @file mali_kbase_mem_profile_debugfs_buf_size.h
 * Header file for the size of the buffer to accumulate the histogram report text in
 */

#ifndef _KBASE_MEM_PROFILE_DEBUGFS_BUF_SIZE_H_
#define _KBASE_MEM_PROFILE_DEBUGFS_BUF_SIZE_H_

/**
 * The size of the buffer to accumulate the histogram report text in
 * @see @ref CCTXP_HIST_BUF_SIZE_MAX_LENGTH_REPORT
 */
#define KBASE_MEM_PROFILE_MAX_BUF_SIZE ((size_t) (64 + ((80 + (56 * 64)) * 15) + 56))

#endif  /*_KBASE_MEM_PROFILE_DEBUGFS_BUF_SIZE_H_*/

