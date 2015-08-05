/*
 *
 * (C) COPYRIGHT 2014 ARM Limited. All rights reserved.
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
 *
 */

#ifndef _KBASE_BACKEND_TIME_H_
#define _KBASE_BACKEND_TIME_H_

/**
 * kbase_backend_get_gpu_time() - Get current GPU time
 * @kbdev:		Device pointer
 * @cycle_counter:	Pointer to u64 to store cycle counter in
 * @system_time:	Pointer to u64 to store system time in
 * @ts:			Pointer to struct timespec to store current monotonic
 *			time in
 */
void kbase_backend_get_gpu_time(struct kbase_device *kbdev, u64 *cycle_counter,
				u64 *system_time, struct timespec *ts);

/**
 * kbase_wait_write_flush() -  Wait for GPU write flush
 * @kctx:	Context pointer
 *
 * Wait 1000 GPU clock cycles. This delay is known to give the GPU time to flush
 * its write buffer.
 *
 * If GPU resets occur then the counters are reset to zero, the delay may not be
 * as expected.
 *
 * This function is only in use for BASE_HW_ISSUE_6367
 */
#ifndef CONFIG_MALI_NO_MALI
void kbase_wait_write_flush(struct kbase_context *kctx);
#endif

#endif /* _KBASE_BACKEND_TIME_H_ */
