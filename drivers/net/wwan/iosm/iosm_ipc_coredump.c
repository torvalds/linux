// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2021 Intel Corporation.
 */

#include "iosm_ipc_coredump.h"

/**
 * ipc_coredump_collect - To collect coredump
 * @devlink:            Pointer to devlink instance.
 * @data:               Pointer to snapshot
 * @entry:              ID of requested snapshot
 * @region_size:        Region size
 *
 * Returns: 0 on success, error on failure
 */
int ipc_coredump_collect(struct iosm_devlink *devlink, u8 **data, int entry,
			 u32 region_size)
{
	int ret, bytes_to_read, bytes_read = 0, i = 0;
	s32 remaining;
	u8 *data_ptr;

	data_ptr = vmalloc(region_size);
	if (!data_ptr)
		return -ENOMEM;

	remaining = devlink->cd_file_info[entry].actual_size;
	ret = ipc_devlink_send_cmd(devlink, rpsi_cmd_coredump_get, entry);
	if (ret) {
		dev_err(devlink->dev, "Send coredump_get cmd failed");
		goto get_cd_fail;
	}
	while (remaining > 0) {
		bytes_to_read = min(remaining, MAX_DATA_SIZE);
		bytes_read = 0;
		ret = ipc_imem_sys_devlink_read(devlink, data_ptr + i,
						bytes_to_read, &bytes_read);
		if (ret) {
			dev_err(devlink->dev, "CD data read failed");
			goto get_cd_fail;
		}
		remaining -= bytes_read;
		i += bytes_read;
	}

	*data = data_ptr;

	return 0;

get_cd_fail:
	vfree(data_ptr);
	return ret;
}

/**
 * ipc_coredump_get_list - Get coredump list from modem
 * @devlink:         Pointer to devlink instance.
 * @cmd:             RPSI command to be sent
 *
 * Returns: 0 on success, error on failure
 */
int ipc_coredump_get_list(struct iosm_devlink *devlink, u16 cmd)
{
	u32 byte_read, num_entries, file_size;
	struct iosm_cd_table *cd_table;
	u8 size[MAX_SIZE_LEN], i;
	char *filename;
	int ret;

	cd_table = kzalloc(MAX_CD_LIST_SIZE, GFP_KERNEL);
	if (!cd_table) {
		ret = -ENOMEM;
		goto  cd_init_fail;
	}

	ret = ipc_devlink_send_cmd(devlink, cmd, MAX_CD_LIST_SIZE);
	if (ret) {
		dev_err(devlink->dev, "rpsi_cmd_coredump_start failed");
		goto cd_init_fail;
	}

	ret = ipc_imem_sys_devlink_read(devlink, (u8 *)cd_table,
					MAX_CD_LIST_SIZE, &byte_read);
	if (ret) {
		dev_err(devlink->dev, "Coredump data is invalid");
		goto cd_init_fail;
	}

	if (byte_read != MAX_CD_LIST_SIZE)
		goto cd_init_fail;

	if (cmd == rpsi_cmd_coredump_start) {
		num_entries = le32_to_cpu(cd_table->list.num_entries);
		if (num_entries == 0 || num_entries > IOSM_NOF_CD_REGION) {
			ret = -EINVAL;
			goto cd_init_fail;
		}

		for (i = 0; i < num_entries; i++) {
			file_size = le32_to_cpu(cd_table->list.entry[i].size);
			filename = cd_table->list.entry[i].filename;

			if (file_size > devlink->cd_file_info[i].default_size) {
				ret = -EINVAL;
				goto cd_init_fail;
			}

			devlink->cd_file_info[i].actual_size = file_size;
			dev_dbg(devlink->dev, "file: %s actual size %d",
				filename, file_size);
			devlink_flash_update_status_notify(devlink->devlink_ctx,
							   filename,
							   "FILENAME", 0, 0);
			snprintf(size, sizeof(size), "%d", file_size);
			devlink_flash_update_status_notify(devlink->devlink_ctx,
							   size, "FILE SIZE",
							   0, 0);
		}
	}

cd_init_fail:
	kfree(cd_table);
	return ret;
}
