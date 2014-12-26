/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
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
#define KBASE_MEM_PROFILE_MAX_BUF_SIZE ( ( size_t ) ( 64 + ( ( 80 + ( 56 * 64 ) ) * 15 ) + 56 ) )

#endif  /*_KBASE_MEM_PROFILE_DEBUGFS_BUF_SIZE_H_*/

