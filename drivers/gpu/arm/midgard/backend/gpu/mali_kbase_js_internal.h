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

#endif /* _KBASE_JS_BACKEND_H_ */
