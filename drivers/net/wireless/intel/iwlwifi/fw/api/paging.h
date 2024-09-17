/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2005-2014 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#ifndef __iwl_fw_api_paging_h__
#define __iwl_fw_api_paging_h__

#define NUM_OF_FW_PAGING_BLOCKS	33 /* 32 for data and 1 block for CSS */

/**
 * struct iwl_fw_paging_cmd - paging layout
 *
 * Send to FW the paging layout in the driver.
 *
 * @flags: various flags for the command
 * @block_size: the block size in powers of 2
 * @block_num: number of blocks specified in the command.
 * @device_phy_addr: virtual addresses from device side
 */
struct iwl_fw_paging_cmd {
	__le32 flags;
	__le32 block_size;
	__le32 block_num;
	__le32 device_phy_addr[NUM_OF_FW_PAGING_BLOCKS];
} __packed; /* FW_PAGING_BLOCK_CMD_API_S_VER_1 */

#endif /* __iwl_fw_api_paging_h__ */
