/*
 *
 * (C) COPYRIGHT 2014-2015 ARM Limited. All rights reserved.
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
