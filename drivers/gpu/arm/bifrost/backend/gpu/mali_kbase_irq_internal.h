/*
 *
 * (C) COPYRIGHT 2014-2015 ARM Limited. All rights reserved.
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
