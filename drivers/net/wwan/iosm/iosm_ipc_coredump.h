/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-2021 Intel Corporation.
 */

#ifndef _IOSM_IPC_COREDUMP_H_
#define _IOSM_IPC_COREDUMP_H_

#include "iosm_ipc_devlink.h"

/* Max number of bytes to receive for Coredump list structure */
#define MAX_CD_LIST_SIZE  0x1000

/* Max buffer allocated to receive coredump data */
#define MAX_DATA_SIZE 0x00010000

/* Max number of file entries */
#define MAX_NOF_ENTRY 256

/* Max length */
#define MAX_SIZE_LEN 32

/**
 * struct iosm_cd_list_entry - Structure to hold coredump file info.
 * @size:       Number of bytes for the entry
 * @filename:   Coredump filename to be generated on host
 */
struct iosm_cd_list_entry {
	__le32 size;
	char filename[IOSM_MAX_FILENAME_LEN];
} __packed;

/**
 * struct iosm_cd_list - Structure to hold list of coredump files
 *                      to be collected.
 * @num_entries:        Number of entries to be received
 * @entry:              Contains File info
 */
struct iosm_cd_list {
	__le32 num_entries;
	struct iosm_cd_list_entry entry[MAX_NOF_ENTRY];
} __packed;

/**
 * struct iosm_cd_table - Common Coredump table
 * @version:            Version of coredump structure
 * @list:               Coredump list structure
 */
struct iosm_cd_table {
	__le32 version;
	struct iosm_cd_list list;
} __packed;

int ipc_coredump_collect(struct iosm_devlink *devlink, u8 **data, int entry,
			 u32 region_size);

int ipc_coredump_get_list(struct iosm_devlink *devlink, u16 cmd);

#endif /* _IOSM_IPC_COREDUMP_H_ */
