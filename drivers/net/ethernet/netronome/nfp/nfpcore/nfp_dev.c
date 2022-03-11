// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2019 Netronome Systems, Inc. */

#include "nfp_dev.h"

const struct nfp_dev_info nfp_dev_info[NFP_DEV_CNT] = {
	[NFP_DEV_NFP6000] = {
		.chip_names		= "NFP4000/NFP5000/NFP6000",
		.pcie_cfg_expbar_offset	= 0x0400,
		.pcie_expl_offset	= 0x1000,
	},
};
