/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
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

#ifndef _KBASE_CSF_FIRMWARE_LOG_H_
#define _KBASE_CSF_FIRMWARE_LOG_H_

#include <mali_kbase.h>

/** Offset of the last field of functions call list entry from the image header */
#define FUNC_CALL_LIST_ENTRY_NAME_OFFSET (0x8)

/*
 * Firmware log dumping buffer size.
 */
#define FIRMWARE_LOG_DUMP_BUF_SIZE PAGE_SIZE

/**
 * kbase_csf_firmware_log_init - Initialize firmware log handling.
 *
 * @kbdev: Pointer to the Kbase device
 *
 * Return: The initialization error code.
 */
int kbase_csf_firmware_log_init(struct kbase_device *kbdev);

/**
 * kbase_csf_firmware_log_term - Terminate firmware log handling.
 *
 * @kbdev: Pointer to the Kbase device
 */
void kbase_csf_firmware_log_term(struct kbase_device *kbdev);

/**
 * kbase_csf_firmware_log_dump_buffer - Read remaining data in the firmware log
 *                                  buffer and print it to dmesg.
 *
 * @kbdev: Pointer to the Kbase device
 */
void kbase_csf_firmware_log_dump_buffer(struct kbase_device *kbdev);

/**
 * kbase_csf_firmware_log_parse_logging_call_list_entry - Parse FW logging function call list entry.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 * @entry: Pointer to section.
 */
void kbase_csf_firmware_log_parse_logging_call_list_entry(struct kbase_device *kbdev,
							  const uint32_t *entry);
/**
 * kbase_csf_firmware_log_toggle_logging_calls - Enables/Disables FW logging function calls.
 *
 * @kbdev:  Instance of a GPU platform device that implements a CSF interface.
 * @val:    Configuration option value.
 *
 * Return: 0 if successful, negative error code on failure
 */
int kbase_csf_firmware_log_toggle_logging_calls(struct kbase_device *kbdev, u32 val);

#endif /* _KBASE_CSF_FIRMWARE_LOG_H_ */
