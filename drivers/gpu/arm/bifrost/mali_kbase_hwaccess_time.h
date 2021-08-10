/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014, 2018-2021 ARM Limited. All rights reserved.
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

#endif /* _KBASE_BACKEND_TIME_H_ */
