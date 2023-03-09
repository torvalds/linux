/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2019-2022 ARM Limited. All rights reserved.
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

#include <mali_kbase.h>

typedef int kbase_device_init_method(struct kbase_device *kbdev);
typedef void kbase_device_term_method(struct kbase_device *kbdev);

/**
 * struct kbase_device_init - Device init/term methods.
 * @init: Function pointer to a initialise method.
 * @term: Function pointer to a terminate method.
 * @err_mes: Error message to be printed when init method fails.
 */
struct kbase_device_init {
	kbase_device_init_method *init;
	kbase_device_term_method *term;
	char *err_mes;
};

int kbase_device_vinstr_init(struct kbase_device *kbdev);
void kbase_device_vinstr_term(struct kbase_device *kbdev);

int kbase_device_kinstr_prfcnt_init(struct kbase_device *kbdev);
void kbase_device_kinstr_prfcnt_term(struct kbase_device *kbdev);

int kbase_device_timeline_init(struct kbase_device *kbdev);
void kbase_device_timeline_term(struct kbase_device *kbdev);

int kbase_device_hwcnt_context_init(struct kbase_device *kbdev);
void kbase_device_hwcnt_context_term(struct kbase_device *kbdev);

int kbase_device_hwcnt_virtualizer_init(struct kbase_device *kbdev);
void kbase_device_hwcnt_virtualizer_term(struct kbase_device *kbdev);

int kbase_device_list_init(struct kbase_device *kbdev);
void kbase_device_list_term(struct kbase_device *kbdev);

int kbase_device_io_history_init(struct kbase_device *kbdev);
void kbase_device_io_history_term(struct kbase_device *kbdev);

int kbase_device_misc_register(struct kbase_device *kbdev);
void kbase_device_misc_deregister(struct kbase_device *kbdev);

void kbase_device_id_init(struct kbase_device *kbdev);

/**
 * kbase_device_early_init - Perform any device-specific initialization.
 * @kbdev:	Device pointer
 *
 * Return: 0 on success, or an error code on failure.
 */
int kbase_device_early_init(struct kbase_device *kbdev);

/**
 * kbase_device_early_term - Perform any device-specific termination.
 * @kbdev:	Device pointer
 */
void kbase_device_early_term(struct kbase_device *kbdev);

/**
 * kbase_device_late_init - Complete any device-specific initialization.
 * @kbdev:	Device pointer
 *
 * Return: 0 on success, or an error code on failure.
 */
int kbase_device_late_init(struct kbase_device *kbdev);

/**
 * kbase_device_late_term - Complete any device-specific termination.
 * @kbdev:	Device pointer
 */
void kbase_device_late_term(struct kbase_device *kbdev);

#if MALI_USE_CSF && !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
/**
 * kbase_is_register_accessible - Checks if register is accessible
 * @offset: Register offset
 *
 * Return: true if the register is accessible, false otherwise.
 */
bool kbase_is_register_accessible(u32 offset);
#endif /* MALI_USE_CSF && !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI) */
