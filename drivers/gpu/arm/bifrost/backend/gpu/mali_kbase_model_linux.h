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

/*
 * Model Linux Framework interfaces.
 *
 * This framework is used to provide generic Kbase Models interfaces.
 * Note: Backends cannot be used together; the selection is done at build time.
 *
 * - Without Model Linux Framework:
 * +-----------------------------+
 * | Kbase read/write/IRQ        |
 * +-----------------------------+
 * | HW interface definitions    |
 * +-----------------------------+
 *
 * - With Model Linux Framework:
 * +-----------------------------+
 * | Kbase read/write/IRQ        |
 * +-----------------------------+
 * | Model Linux Framework       |
 * +-----------------------------+
 * | Model interface definitions |
 * +-----------------------------+
 */

#ifndef _KBASE_MODEL_LINUX_H_
#define _KBASE_MODEL_LINUX_H_

/*
 * Include Model definitions
 */

#if IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
#include <backend/gpu/mali_kbase_model_dummy.h>
#endif /* IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI) */

#if !IS_ENABLED(CONFIG_MALI_REAL_HW)
/**
 * kbase_gpu_device_create() - Generic create function.
 *
 * @kbdev: Kbase device.
 *
 * Specific model hook is implemented by midgard_model_create()
 *
 * Return: 0 on success, error code otherwise.
 */
int kbase_gpu_device_create(struct kbase_device *kbdev);

/**
 * kbase_gpu_device_destroy() - Generic create function.
 *
 * @kbdev: Kbase device.
 *
 * Specific model hook is implemented by midgard_model_destroy()
 */
void kbase_gpu_device_destroy(struct kbase_device *kbdev);

/**
 * midgard_model_create() - Private create function.
 *
 * @kbdev: Kbase device.
 *
 * This hook is specific to the model built in Kbase.
 *
 * Return: Model handle.
 */
void *midgard_model_create(struct kbase_device *kbdev);

/**
 * midgard_model_destroy() - Private destroy function.
 *
 * @h: Model handle.
 *
 * This hook is specific to the model built in Kbase.
 */
void midgard_model_destroy(void *h);

/**
 * midgard_model_write_reg() - Private model write function.
 *
 * @h: Model handle.
 * @addr: Address at which to write.
 * @value: value to write.
 *
 * This hook is specific to the model built in Kbase.
 */
void midgard_model_write_reg(void *h, u32 addr, u32 value);

/**
 * midgard_model_read_reg() - Private model read function.
 *
 * @h: Model handle.
 * @addr: Address from which to read.
 * @value: Pointer where to store the read value.
 *
 * This hook is specific to the model built in Kbase.
 */
void midgard_model_read_reg(void *h, u32 addr, u32 *const value);

/**
 * gpu_device_raise_irq() - Private IRQ raise function.
 *
 * @model: Model handle.
 * @irq: IRQ type to raise.
 *
 * This hook is global to the model Linux framework.
 */
void gpu_device_raise_irq(void *model, enum model_linux_irqs irq);

/**
 * gpu_device_set_data() - Private model set data function.
 *
 * @model: Model handle.
 * @data: Data carried by model.
 *
 * This hook is global to the model Linux framework.
 */
void gpu_device_set_data(void *model, void *data);

/**
 * gpu_device_get_data() - Private model get data function.
 *
 * @model: Model handle.
 *
 * This hook is global to the model Linux framework.
 *
 * Return: Pointer to the data carried by model.
 */
void *gpu_device_get_data(void *model);
#endif /* !IS_ENABLED(CONFIG_MALI_REAL_HW) */

#endif /* _KBASE_MODEL_LINUX_H_ */
