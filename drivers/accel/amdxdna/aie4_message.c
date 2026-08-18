// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#include <drm/amdxdna_accel.h>
#include <drm/drm_print.h>
#include <linux/mutex.h>

#include "aie.h"
#include "aie4_msg_priv.h"
#include "aie4_pci.h"
#include "amdxdna_mailbox.h"
#include "amdxdna_mailbox_helper.h"
#include "amdxdna_pci_drv.h"

int aie4_suspend_fw(struct amdxdna_dev_hdl *ndev)
{
	DECLARE_AIE_MSG(aie4_msg_suspend, AIE4_MSG_OP_SUSPEND);
	int ret;

	ret = aie_send_mgmt_msg_wait(&ndev->aie, &msg);
	if (ret)
		XDNA_ERR(ndev->aie.xdna, "Failed to suspend fw, ret %d", ret);

	return ret;
}

int aie4_query_aie_metadata(struct amdxdna_dev_hdl *ndev,
			    struct amdxdna_drm_query_aie_metadata *metadata)
{
	DECLARE_AIE_MSG(aie4_msg_aie4_tile_info, AIE4_MSG_OP_AIE_TILE_INFO);
	int ret;

	ret = aie_send_mgmt_msg_wait(&ndev->aie, &msg);
	if (ret)
		return ret;

	metadata->col_size = resp.info.size;
	metadata->cols = resp.info.cols;
	metadata->rows = resp.info.rows;

	metadata->version.major = resp.info.major;
	metadata->version.minor = resp.info.minor;

	metadata->core.row_count = resp.info.core_rows;
	metadata->core.row_start = resp.info.core_row_start;
	metadata->core.dma_channel_count = resp.info.core_dma_channels;
	metadata->core.lock_count = resp.info.core_locks;
	metadata->core.event_reg_count = resp.info.core_events;

	metadata->mem.row_count = resp.info.mem_rows;
	metadata->mem.row_start = resp.info.mem_row_start;
	metadata->mem.dma_channel_count = resp.info.mem_dma_channels;
	metadata->mem.lock_count = resp.info.mem_locks;
	metadata->mem.event_reg_count = resp.info.mem_events;

	metadata->shim.row_count = resp.info.shim_rows;
	metadata->shim.row_start = resp.info.shim_row_start;
	metadata->shim.dma_channel_count = resp.info.shim_dma_channels;
	metadata->shim.lock_count = resp.info.shim_locks;
	metadata->shim.event_reg_count = resp.info.shim_events;

	return 0;
}

int aie4_attach_work_buffer(struct amdxdna_dev_hdl *ndev)
{
	DECLARE_AIE_MSG(aie4_msg_attach_work_buffer, AIE4_MSG_OP_ATTACH_WORK_BUFFER);
	struct amdxdna_dev *xdna = ndev->aie.xdna;
	int ret;

	req.buff_addr = ndev->work_buf_addr;
	req.buff_size = AIE4_WORK_BUFFER_MIN_SIZE;

	ret = aie_send_mgmt_msg_wait(&ndev->aie, &msg);
	if (ret)
		XDNA_ERR(xdna, "Failed to attach work buffer, ret %d", ret);
	else
		XDNA_DBG(xdna, "Attached work buffer");

	return ret;
}
