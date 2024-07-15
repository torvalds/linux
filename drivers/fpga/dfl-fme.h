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
 */
struct dfl_fme {
	struct platform_device *mgr;
	struct list_head region_list;
	struct list_head bridge_list;
};

extern const struct dfl_feature_ops fme_pr_mgmt_ops;
extern const struct dfl_feature_id fme_pr_mgmt_id_table[];
extern const struct dfl_feature_ops fme_global_err_ops;
extern const struct dfl_feature_id fme_global_err_id_table[];
extern const struct attribute_group fme_global_err_group;
extern const struct dfl_feature_ops fme_perf_ops;
extern const struct dfl_feature_id fme_perf_id_table[];

#endif /* __DFL_FME_H */
