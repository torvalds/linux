/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header file for FPGA Management Engine (FME) Driver
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

#ifndef __DFL_FME_H
#define __DFL_FME_H

/**
 * struct dfl_fme - dfl fme private data
 *
 * @mgr: FME's FPGA manager platform device.
 * @region_list: linked list of FME's FPGA regions.
 * @bridge_list: linked list of FME's FPGA bridges.
 * @pdata: fme platform device's pdata.
 */
struct dfl_fme {
	struct platform_device *mgr;
	struct list_head region_list;
	struct list_head bridge_list;
	struct dfl_feature_platform_data *pdata;
};

extern const struct dfl_feature_ops pr_mgmt_ops;

#endif /* __DFL_FME_H */
