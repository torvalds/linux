/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014-2015, 2017-2022 ARM Limited. All rights reserved.
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

/**
 * DOC: Base kernel property query backend APIs
 */

#ifndef _KBASE_HWACCESS_GPUPROPS_H_
#define _KBASE_HWACCESS_GPUPROPS_H_

/**
 * kbase_backend_gpuprops_get() - Fill @regdump with GPU properties read from
 *				  GPU
 * @kbdev:	Device pointer
 * @regdump:	Pointer to struct kbase_gpuprops_regdump structure
 *
 * The caller should ensure that GPU remains powered-on during this function.
 *
 * Return: Zero for succeess or a Linux error code
 */
int kbase_backend_gpuprops_get(struct kbase_device *kbdev,
					struct kbase_gpuprops_regdump *regdump);

/**
 * kbase_backend_gpuprops_get_curr_config() - Fill @curr_config_regdump with
 *                                            relevant GPU properties read from
 *                                            the GPU registers.
 * @kbdev:               Device pointer.
 * @curr_config_regdump: Pointer to struct kbase_current_config_regdump
 *                       structure.
 *
 * The caller should ensure that GPU remains powered-on during this function and
 * the caller must ensure this function returns success before using the values
 * returned in the curr_config_regdump in any part of the kernel.
 *
 * Return: Zero for succeess or a Linux error code
 */
int kbase_backend_gpuprops_get_curr_config(struct kbase_device *kbdev,
		struct kbase_current_config_regdump *curr_config_regdump);

/**
 * kbase_backend_gpuprops_get_features - Fill @regdump with GPU properties read
 *                                       from GPU
 * @kbdev:   Device pointer
 * @regdump: Pointer to struct kbase_gpuprops_regdump structure
 *
 * This function reads GPU properties that are dependent on the hardware
 * features bitmask. It will power-on the GPU if required.
 *
 * Return: Zero for succeess or a Linux error code
 */
int kbase_backend_gpuprops_get_features(struct kbase_device *kbdev,
					struct kbase_gpuprops_regdump *regdump);

/**
 * kbase_backend_gpuprops_get_l2_features - Fill @regdump with L2_FEATURES read
 *                                          from GPU
 * @kbdev:   Device pointer
 * @regdump: Pointer to struct kbase_gpuprops_regdump structure
 *
 * This function reads L2_FEATURES register that is dependent on the hardware
 * features bitmask. It will power-on the GPU if required.
 *
 * Return: Zero on success, Linux error code on failure
 */
int kbase_backend_gpuprops_get_l2_features(struct kbase_device *kbdev,
					struct kbase_gpuprops_regdump *regdump);


#endif /* _KBASE_HWACCESS_GPUPROPS_H_ */
