/*
 * Qualcomm Technologies HIDMA Management common header
 *
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

struct hidma_mgmt_dev {
	u8 hw_version_major;
	u8 hw_version_minor;

	u32 max_wr_xactions;
	u32 max_rd_xactions;
	u32 max_write_request;
	u32 max_read_request;
	u32 dma_channels;
	u32 chreset_timeout_cycles;
	u32 hw_version;
	u32 *priority;
	u32 *weight;

	/* Hardware device constants */
	void __iomem *virtaddr;
	resource_size_t addrsize;

	struct kobject **chroots;
	struct platform_device *pdev;
};

int hidma_mgmt_init_sys(struct hidma_mgmt_dev *dev);
int hidma_mgmt_setup(struct hidma_mgmt_dev *mgmtdev);
