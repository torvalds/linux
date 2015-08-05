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
 * Backend specific IRQ APIs
 */

#ifndef _KBASE_IRQ_INTERNAL_H_
#define _KBASE_IRQ_INTERNAL_H_

int kbase_install_interrupts(struct kbase_device *kbdev);

void kbase_release_interrupts(struct kbase_device *kbdev);

/**
 * kbase_synchronize_irqs - Ensure that all IRQ handlers have completed
 *                          execution
 * @kbdev: The kbase device
 */
void kbase_synchronize_irqs(struct kbase_device *kbdev);

int kbasep_common_test_interrupt_handlers(
					struct kbase_device * const kbdev);

#endif /* _KBASE_IRQ_INTERNAL_H_ */
