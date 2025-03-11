// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/devcoredump.h>
#include "hif.h"
#include "coredump.h"
#include "debug.h"

enum
ath12k_fw_crash_dump_type ath12k_coredump_get_dump_type(enum ath12k_qmi_target_mem type)
{
	enum ath12k_fw_crash_dump_type dump_type;

	switch (type) {
	case HOST_DDR_REGION_TYPE:
		dump_type = FW_CRASH_DUMP_REMOTE_MEM_DATA;
		break;
	case M3_DUMP_REGION_TYPE:
		dump_type = FW_CRASH_DUMP_M3_DUMP;
		break;
	case PAGEABLE_MEM_REGION_TYPE:
		dump_type = FW_CRASH_DUMP_PAGEABLE_DATA;
		break;
	case BDF_MEM_REGION_TYPE:
	case CALDB_MEM_REGION_TYPE:
		dump_type = FW_CRASH_DUMP_NONE;
		break;
	default:
		dump_type = FW_CRASH_DUMP_TYPE_MAX;
		break;
	}

	return dump_type;
}

void ath12k_coredump_upload(struct work_struct *work)
{
	struct ath12k_base *ab = container_of(work, struct ath12k_base, dump_work);

	ath12k_info(ab, "Uploading coredump\n");
	/* dev_coredumpv() takes ownership of the buffer */
	dev_coredumpv(ab->dev, ab->dump_data, ab->ath12k_coredump_len, GFP_KERNEL);
	ab->dump_data = NULL;
}

void ath12k_coredump_collect(struct ath12k_base *ab)
{
	ath12k_hif_coredump_download(ab);
}
