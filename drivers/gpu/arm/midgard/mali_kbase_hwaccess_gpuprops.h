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
 * Base kernel property query backend APIs
 */

#ifndef _KBASE_HWACCESS_GPUPROPS_H_
#define _KBASE_HWACCESS_GPUPROPS_H_

/**
 * kbase_backend_gpuprops_get() - Fill @regdump with GPU properties read from
 *				  GPU
 * @kbdev:	Device pointer
 * @regdump:	Pointer to struct kbase_gpuprops_regdump structure
 */
void kbase_backend_gpuprops_get(struct kbase_device *kbdev,
					struct kbase_gpuprops_regdump *regdump);

#endif /* _KBASE_HWACCESS_GPUPROPS_H_ */
