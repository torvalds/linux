/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2019-2021 ARM Limited. All rights reserved.
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

#ifndef _KBASE_L2_MMU_CONFIG_H_
#define _KBASE_L2_MMU_CONFIG_H_
/**
 * kbase_set_mmu_quirks - Set the hw_quirks_mmu field of kbdev
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Use this function to initialise the hw_quirks_mmu field, for instance to set
 * the MAX_READS and MAX_WRITES to sane defaults for each GPU.
 *
 * Return: Zero for succeess or a Linux error code
 */
int kbase_set_mmu_quirks(struct kbase_device *kbdev);

#endif /* _KBASE_L2_MMU_CONFIG_H */
