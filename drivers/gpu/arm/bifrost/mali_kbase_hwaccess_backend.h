/*
 *
 * (C) COPYRIGHT 2014-2015 ARM Limited. All rights reserved.
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


/*
 * HW access backend common APIs
 */

#ifndef _KBASE_HWACCESS_BACKEND_H_
#define _KBASE_HWACCESS_BACKEND_H_

/**
 * kbase_backend_early_init - Perform any backend-specific initialization.
 * @kbdev:	Device pointer
 *
 * Return: 0 on success, or an error code on failure.
 */
int kbase_backend_early_init(struct kbase_device *kbdev);

/**
 * kbase_backend_late_init - Perform any backend-specific initialization.
 * @kbdev:	Device pointer
 *
 * Return: 0 on success, or an error code on failure.
 */
int kbase_backend_late_init(struct kbase_device *kbdev);

/**
 * kbase_backend_early_term - Perform any backend-specific termination.
 * @kbdev:	Device pointer
 */
void kbase_backend_early_term(struct kbase_device *kbdev);

/**
 * kbase_backend_late_term - Perform any backend-specific termination.
 * @kbdev:	Device pointer
 */
void kbase_backend_late_term(struct kbase_device *kbdev);

#endif /* _KBASE_HWACCESS_BACKEND_H_ */
