/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header file for FPGA Management Engine (FME) Partial Reconfiguration Driver
 *
 * Copyright (C) 2017-2018 Intel Corporation, Inc.
 *
 * Authors:
 *   Kang Luwei <luwei.kang@intel.com>
 *   Xiao Guangrong <guangrong.xiao@linux.intel.com>
 *   Wu Hao <hao.wu@intel.com>
 *   Joseph Grecco <joe.grecco@intel.com>
 *   Enno Luebbers <enno.luebbers@intel.com>
 *   Tim Whisonant <tim.whisonant@intel.com>
 *   Ananda Ravuri <ananda.ravuri@intel.com>
 *   Henry Mitchel <henry.mitchel@intel.com>
 */

#ifndef __DFL_FME_PR_H
#define __DFL_FME_PR_H

#include <linux/platform_device.h>

/**
 * struct dfl_fme_region - FME fpga region data structure
 *
 * @region: platform device of the FPGA region.
 * @node: used to link fme_region to a list.
 * @port_id: indicate which port this region connected to.
 */
struct dfl_fme_region {
	struct platform_device *region;
	struct list_head node;
	int port_id;
};

/**
 * struct dfl_fme_region_pdata - platform data for FME region platform device.
 *
 * @mgr: platform device of the FPGA manager.
 * @br: platform device of the FPGA bridge.
 * @region_id: region id (same as port_id).
 */
struct dfl_fme_region_pdata {
	struct platform_device *mgr;
	struct platform_device *br;
	int region_id;
};

/**
 * struct dfl_fme_bridge - FME fpga bridge data structure
 *
 * @br: platform device of the FPGA bridge.
 * @node: used to link fme_bridge to a list.
 */
struct dfl_fme_bridge {
	struct platform_device *br;
	struct list_head node;
};

/**
 * struct dfl_fme_bridge_pdata - platform data for FME bridge platform device.
 *
 * @cdev: container device.
 * @port_id: port id.
 */
struct dfl_fme_br_pdata {
	struct dfl_fpga_cdev *cdev;
	int port_id;
};

/**
 * struct dfl_fme_mgr_pdata - platform data for FME manager platform device.
 *
 * @ioaddr: mapped io address for FME manager platform device.
 */
struct dfl_fme_mgr_pdata {
	void __iomem *ioaddr;
};

#define DFL_FPGA_FME_MGR	"dfl-fme-mgr"
#define DFL_FPGA_FME_BRIDGE	"dfl-fme-bridge"
#define DFL_FPGA_FME_REGION	"dfl-fme-region"

#endif /* __DFL_FME_PR_H */
