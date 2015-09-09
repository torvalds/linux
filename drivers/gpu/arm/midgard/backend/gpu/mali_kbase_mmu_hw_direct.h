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
 * Interface file for the direct implementation for MMU hardware access
 *
 * Direct MMU hardware interface
 *
 * This module provides the interface(s) that are required by the direct
 * register access implementation of the MMU hardware interface
 */

#ifndef _MALI_KBASE_MMU_HW_DIRECT_H_
#define _MALI_KBASE_MMU_HW_DIRECT_H_

#include <mali_kbase_defs.h>

/**
 * kbase_mmu_interrupt - Process an MMU interrupt.
 *
 * Process the MMU interrupt that was reported by the &kbase_device.
 *
 * @kbdev:          kbase context to clear the fault from.
 * @irq_stat:       Value of the MMU_IRQ_STATUS register
 */
void kbase_mmu_interrupt(struct kbase_device *kbdev, u32 irq_stat);

#endif	/* _MALI_KBASE_MMU_HW_DIRECT_H_ */
