/*
 *
 * (C) COPYRIGHT 2014 ARM Limited. All rights reserved.
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
 * Backend-specific HW access device APIs
 */

#ifndef _KBASE_DEVICE_INTERNAL_H_
#define _KBASE_DEVICE_INTERNAL_H_

/**
 * kbase_reg_write - write to GPU register
 * @kbdev:  Kbase device pointer
 * @offset: Offset of register
 * @value:  Value to write
 * @kctx:   Kbase context pointer. May be NULL
 *
 * Caller must ensure the GPU is powered (@kbdev->pm.gpu_powered != false). If
 * @kctx is not NULL then the caller must ensure it is scheduled (@kctx->as_nr
 * != KBASEP_AS_NR_INVALID).
 */
void kbase_reg_write(struct kbase_device *kbdev, u16 offset, u32 value,
						struct kbase_context *kctx);

/**
 * kbase_reg_read - read from GPU register
 * @kbdev:  Kbase device pointer
 * @offset: Offset of register
 * @kctx:   Kbase context pointer. May be NULL
 *
 * Caller must ensure the GPU is powered (@kbdev->pm.gpu_powered != false). If
 * @kctx is not NULL then the caller must ensure it is scheduled (@kctx->as_nr
 * != KBASEP_AS_NR_INVALID).
 *
 * Return: Value in desired register
 */
u32 kbase_reg_read(struct kbase_device *kbdev, u16 offset,
						struct kbase_context *kctx);


/**
 * kbase_gpu_interrupt - GPU interrupt handler
 * @kbdev: Kbase device pointer
 * @val:   The value of the GPU IRQ status register which triggered the call
 *
 * This function is called from the interrupt handler when a GPU irq is to be
 * handled.
 */
void kbase_gpu_interrupt(struct kbase_device *kbdev, u32 val);

#endif /* _KBASE_DEVICE_INTERNAL_H_ */
