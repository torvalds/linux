// SPDX-License-Identifier: GPL-2.0

#include <generated/utsrelease.h>

#include <linux/efi.h>
#include <linux/unaligned.h>

#include "efistub.h"

static efi_guid_t loader_entry_guid = LINUX_EFI_LOADER_ENTRY_GUID;

static const struct efi_hd_dev_path *
efi_bli_find_hd_node(const struct efi_dev_path *path)
{
	const struct efi_dev_path *node;
	u16 node_len;

	for (node = path;
	     node->header.type != EFI_DEV_END_PATH &&
	     node->header.type != EFI_DEV_END_PATH2;
	     node = (const void *)node + node_len) {
		node_len = get_unaligned_le16(&node->header.length);

		if (node_len < sizeof(node->header))
			return NULL;

		if (node->header.type != EFI_DEV_MEDIA ||
		    node->header.sub_type != EFI_DEV_MEDIA_HARD_DRIVE)
			continue;

		if (node_len < sizeof(node->hd))
			return NULL;

		if (node->hd.partition_format != EFI_HD_PARTITION_FORMAT_GPT ||
		    node->hd.signature_type != EFI_HD_SIGNATURE_TYPE_GUID)
			continue;

		return &node->hd;
	}

	return NULL;
}

static void efi_bli_populate_loader_part_uuid(const efi_loaded_image_t *image)
{
	static efi_guid_t device_path_guid = EFI_DEVICE_PATH_PROTOCOL_GUID;
	efi_char16_t partuuid[UUID_STRING_LEN + 1];
	const struct efi_hd_dev_path *hd_node;
	const struct efi_dev_path *path;

	if (efi_bs_call(handle_protocol, efi_table_attr(image, device_handle),
			&device_path_guid, (void **)&path) != EFI_SUCCESS)
		return;

	hd_node = efi_bli_find_hd_node(path);
	if (!hd_node)
		return;

	if (efi_snprintf(partuuid, ARRAY_SIZE(partuuid), "%pUl",
			 &hd_node->signature) != UUID_STRING_LEN)
		return;

	set_efi_var(L"LoaderDevicePartUUID", &loader_entry_guid,
		    EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
		    sizeof(partuuid), partuuid);
}

void efi_bli_set_variables(const efi_loaded_image_t *image)
{
	static efi_char16_t loader_info[] = L"Linux EFI stub " UTS_RELEASE;
	unsigned long size = 0;

	if (!image)
		return;

	if (get_efi_var(L"LoaderInfo", &loader_entry_guid,
			NULL, &size, NULL) != EFI_NOT_FOUND)
		return;

	if (set_efi_var(L"LoaderInfo", &loader_entry_guid,
			EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
			sizeof(loader_info), loader_info) != EFI_SUCCESS)
		return;

	efi_bli_populate_loader_part_uuid(image);
}
