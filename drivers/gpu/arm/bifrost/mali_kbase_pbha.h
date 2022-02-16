/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2021-2022 ARM Limited. All rights reserved.
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

#ifndef _KBASE_PBHA_H
#define _KBASE_PBHA_H

#include <mali_kbase.h>

/**
 * kbasep_pbha_supported - check whether PBHA registers are
 * available
 *
 * @kbdev: Device pointer
 *
 * Should only be used in mali_kbase_pbha* files - thus the
 * kbase[p] prefix.
 *
 * Return: True if pbha is supported, false otherwise
 */
bool kbasep_pbha_supported(struct kbase_device *kbdev);

/**
 * kbase_pbha_record_settings - record PBHA settings to be applied when
 * L2 is powered down
 *
 * @kbdev: Device pointer
 * @runtime: true if it's called at runtime and false if it's called on init.
 * @id: memory access source ID
 * @read_setting: Read setting
 * @write_setting: Write setting
 *
 * Return: 0 on success, otherwise error code.
 */
int kbase_pbha_record_settings(struct kbase_device *kbdev, bool runtime,
			       unsigned int id, unsigned int read_setting,
			       unsigned int write_setting);

/**
 * kbase_pbha_write_settings - write recorded PBHA settings to GPU
 * registers
 *
 * @kbdev: Device pointer
 *
 * Only valid to call this function when L2 is powered down, otherwise
 * this will not affect PBHA settings.
 */
void kbase_pbha_write_settings(struct kbase_device *kbdev);

/**
 * kbase_pbha_read_dtb - read PBHA settings from DTB and record it to be
 * applied when L2 is powered down
 *
 * @kbdev: Device pointer
 *
 * Return: 0 on success, otherwise error code.
 */
int kbase_pbha_read_dtb(struct kbase_device *kbdev);

#endif /* _KBASE_PBHA_H */
