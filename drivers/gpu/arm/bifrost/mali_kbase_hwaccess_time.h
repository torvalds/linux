/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014, 2018-2021, 2023 ARM Limited. All rights reserved.
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

#ifndef _KBASE_BACKEND_TIME_H_
#define _KBASE_BACKEND_TIME_H_

#if MALI_USE_CSF
/**
 * struct kbase_backend_time - System timestamp attributes.
 *
 * @multiplier:		Numerator of the converter's fraction.
 * @divisor:		Denominator of the converter's fraction.
 * @offset:		Converter's offset term.
 *
 * According to Generic timer spec, system timer:
 * - Increments at a fixed frequency
 * - Starts operating from zero
 *
 * Hence CPU time is a linear function of System Time.
 *
 * CPU_ts = alpha * SYS_ts + beta
 *
 * Where
 * - alpha = 10^9/SYS_ts_freq
 * - beta is calculated by two timer samples taken at the same time:
 *   beta = CPU_ts_s - SYS_ts_s * alpha
 *
 * Since alpha is a rational number, we minimizing possible
 * rounding error by simplifying the ratio. Thus alpha is stored
 * as a simple `multiplier / divisor` ratio.
 *
 */
struct kbase_backend_time {
	u64 multiplier;
	u64 divisor;
	s64 offset;
};

/**
 * kbase_backend_time_convert_gpu_to_cpu() - Convert GPU timestamp to CPU timestamp.
 *
 * @kbdev:	Kbase device pointer
 * @gpu_ts:	System timestamp value to converter.
 *
 * Return: The CPU timestamp.
 */
u64 __maybe_unused kbase_backend_time_convert_gpu_to_cpu(struct kbase_device *kbdev, u64 gpu_ts);
#endif

/**
 * kbase_backend_get_gpu_time() - Get current GPU time
 * @kbdev:              Device pointer
 * @cycle_counter:      Pointer to u64 to store cycle counter in.
 * @system_time:        Pointer to u64 to store system time in
 * @ts:                 Pointer to struct timespec to store current monotonic
 *			time in
 */
void kbase_backend_get_gpu_time(struct kbase_device *kbdev, u64 *cycle_counter,
				u64 *system_time, struct timespec64 *ts);

/**
 * kbase_backend_get_gpu_time_norequest() - Get current GPU time without
 *                                          request/release cycle counter
 * @kbdev:		Device pointer
 * @cycle_counter:	Pointer to u64 to store cycle counter in
 * @system_time:	Pointer to u64 to store system time in
 * @ts:			Pointer to struct timespec to store current monotonic
 *			time in
 */
void kbase_backend_get_gpu_time_norequest(struct kbase_device *kbdev,
					  u64 *cycle_counter,
					  u64 *system_time,
					  struct timespec64 *ts);
/**
 * kbase_get_timeout_ms - Choose a timeout value to get a timeout scaled
 *                        GPU frequency, using a choice from
 *                        kbase_timeout_selector.
 *
 * @kbdev:	KBase device pointer.
 * @selector:	Value from kbase_scaled_timeout_selector enum.
 *
 * Return:	Timeout in milliseconds, as an unsigned integer.
 */
unsigned int kbase_get_timeout_ms(struct kbase_device *kbdev,
				  enum kbase_timeout_selector selector);

/**
 * kbase_backend_get_cycle_cnt - Reads the GPU cycle counter
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * Return: Snapshot of the GPU cycle count register.
 */
u64 kbase_backend_get_cycle_cnt(struct kbase_device *kbdev);

/**
 * kbase_backend_time_init() - Initialize system timestamp converter.
 *
 * @kbdev:	Kbase device pointer
 *
 * This function should only be called after GPU is powered-up and
 * L2 cached power-up has been initiated.
 *
 * Return: Zero on success, error code otherwise.
 */
int kbase_backend_time_init(struct kbase_device *kbdev);

#endif /* _KBASE_BACKEND_TIME_H_ */
