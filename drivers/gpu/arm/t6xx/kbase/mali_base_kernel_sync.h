/*
 *
 * (C) COPYRIGHT 2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file
 * Base cross-proccess sync API.
 */

#ifndef _BASE_KERNEL_SYNC_H_
#define _BASE_KERNEL_SYNC_H_

#include <linux/ioctl.h>

#define STREAM_IOC_MAGIC '~'

/* Fence insert.
 *
 * Inserts a fence on the stream operated on.
 * Fence can be waited via a base fence wait soft-job
 * or triggered via a base fence trigger soft-job.
 *
 * Fences must be cleaned up with close when no longer needed.
 *
 * No input/output arguments.
 * Returns
 * >=0 fd
 * <0  error code
 */
#define STREAM_IOC_FENCE_INSERT _IO(STREAM_IOC_MAGIC, 0)

#endif /* _BASE_KERNEL_SYNC_H_ */

