/*
 *
 * (C) COPYRIGHT 2019-2020 ARM Limited. All rights reserved.
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

#include <mali_kbase.h>

/**
 * kbase_device_get_list - get device list.
 *
 * Get access to device list.
 *
 * Return: Pointer to the linked list head.
 */
const struct list_head *kbase_device_get_list(void);

/**
 * kbase_device_put_list - put device list.
 *
 * @dev_list: head of linked list containing device list.
 *
 * Put access to the device list.
 */
void kbase_device_put_list(const struct list_head *dev_list);

/**
 * Kbase_increment_device_id - increment device id.
 *
 * Used to increment device id on successful initialization of the device.
 */
void kbase_increment_device_id(void);

/**
 * kbase_device_init - Device initialisation.
 *
 * This is called from device probe to initialise various other
 * components needed.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Return: 0 on success and non-zero value on failure.
 */
int kbase_device_init(struct kbase_device *kbdev);

/**
 * kbase_device_term - Device termination.
 *
 * This is called from device remove to terminate various components that
 * were initialised during kbase_device_init.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 */
void kbase_device_term(struct kbase_device *kbdev);
