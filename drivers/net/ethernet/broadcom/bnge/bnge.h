/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Broadcom */

#ifndef _BNGE_H_
#define _BNGE_H_

#define DRV_NAME	"bng_en"
#define DRV_SUMMARY	"Broadcom 800G Ethernet Linux Driver"

extern char bnge_driver_name[];

enum board_idx {
	BCM57708,
};

struct bnge_dev {
	struct device	*dev;
	struct pci_dev	*pdev;
	u64	dsn;
#define BNGE_VPD_FLD_LEN	32
	char		board_partno[BNGE_VPD_FLD_LEN];
	char		board_serialno[BNGE_VPD_FLD_LEN];

	void __iomem	*bar0;
};

#endif /* _BNGE_H_ */
