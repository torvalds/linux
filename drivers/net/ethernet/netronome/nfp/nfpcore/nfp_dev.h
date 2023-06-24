/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2019 Netronome Systems, Inc. */

#ifndef _NFP_DEV_H_
#define _NFP_DEV_H_

#include <linux/types.h>

#define PCI_VENDOR_ID_CORIGINE	0x1da8
#define PCI_DEVICE_ID_NFP3800	0x3800
#define PCI_DEVICE_ID_NFP4000	0x4000
#define PCI_DEVICE_ID_NFP5000	0x5000
#define PCI_DEVICE_ID_NFP6000	0x6000
#define PCI_DEVICE_ID_NFP3800_VF	0x3803
#define PCI_DEVICE_ID_NFP6000_VF	0x6003

enum nfp_dev_id {
	NFP_DEV_NFP3800,
	NFP_DEV_NFP3800_VF,
	NFP_DEV_NFP6000,
	NFP_DEV_NFP6000_VF,
	NFP_DEV_CNT,
};

struct nfp_dev_info {
	/* Required fields */
	u64 dma_mask;
	u32 qc_idx_mask;
	u32 qc_addr_offset;
	u32 min_qc_size;
	u32 max_qc_size;

	/* PF-only fields */
	const char *chip_names;
	u32 pcie_cfg_expbar_offset;
	u32 pcie_expl_offset;
	u32 qc_area_sz;
};

extern const struct nfp_dev_info nfp_dev_info[NFP_DEV_CNT];

#endif
