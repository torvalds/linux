/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _EFI_EMBEDDED_FW_INTERNAL_H_
#define _EFI_EMBEDDED_FW_INTERNAL_H_

/*
 * This struct and efi_embedded_fw_list are private to the efi-embedded fw
 * implementation they only in separate header for use by lib/test_firmware.c.
 */
struct efi_embedded_fw {
	struct list_head list;
	const char *name;
	const u8 *data;
	size_t length;
};

#ifdef CONFIG_TEST_FIRMWARE
extern struct list_head efi_embedded_fw_list;
extern bool efi_embedded_fw_checked;
#endif

#endif /* _EFI_EMBEDDED_FW_INTERNAL_H_ */
