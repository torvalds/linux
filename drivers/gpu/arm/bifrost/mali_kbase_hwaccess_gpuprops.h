/*
 *
 * (C) COPYRIGHT 2014-2015, 2018 ARM Limited. All rights reserved.
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


/**
 * Base kernel property query backend APIs
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
 */
void kbase_backend_gpuprops_get(struct kbase_device *kbdev,
					struct kbase_gpuprops_regdump *regdump);

/**
 * kbase_backend_gpuprops_get - Fill @regdump with GPU properties read from GPU
 * @kbdev:   Device pointer
 * @regdump: Pointer to struct kbase_gpuprops_regdump structure
 *
 * This function reads GPU properties that are dependent on the hardware
 * features bitmask. It will power-on the GPU if required.
 */
void kbase_backend_gpuprops_get_features(struct kbase_device *kbdev,
					struct kbase_gpuprops_regdump *regdump);


#endif /* _KBASE_HWACCESS_GPUPROPS_H_ */
