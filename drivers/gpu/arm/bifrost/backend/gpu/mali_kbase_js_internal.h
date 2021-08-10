/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014-2015, 2020-2021 ARM Limited. All rights reserved.
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

/*
 * Register-based HW access backend specific job scheduler APIs
 */

#ifndef _KBASE_JS_BACKEND_H_
#define _KBASE_JS_BACKEND_H_

/**
 * kbase_backend_timer_init() - Initialise the JS scheduling timer
 * @kbdev:	Device pointer
 *
 * This function should be called at driver initialisation
 *
 * Return: 0 on success
 */
int kbase_backend_timer_init(struct kbase_device *kbdev);

/**
 * kbase_backend_timer_term() - Terminate the JS scheduling timer
 * @kbdev:	Device pointer
 *
 * This function should be called at driver termination
 */
void kbase_backend_timer_term(struct kbase_device *kbdev);

/**
 * kbase_backend_timer_suspend - Suspend is happening, stop the JS scheduling
 *                               timer
 * @kbdev: Device pointer
 *
 * This function should be called on suspend, after the active count has reached
 * zero. This is required as the timer may have been started on job submission
 * to the job scheduler, but before jobs are submitted to the GPU.
 *
 * Caller must hold runpool_mutex.
 */
void kbase_backend_timer_suspend(struct kbase_device *kbdev);

/**
 * kbase_backend_timer_resume - Resume is happening, re-evaluate the JS
 *                              scheduling timer
 * @kbdev: Device pointer
 *
 * This function should be called on resume. Note that is is not guaranteed to
 * re-start the timer, only evalute whether it should be re-started.
 *
 * Caller must hold runpool_mutex.
 */
void kbase_backend_timer_resume(struct kbase_device *kbdev);

#endif /* _KBASE_JS_BACKEND_H_ */
