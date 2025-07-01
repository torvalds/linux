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

#define INVALID_HW_RING_ID      ((u16)-1)

struct bnge_dev {
	struct device	*dev;
	struct pci_dev	*pdev;
	u64	dsn;
#define BNGE_VPD_FLD_LEN	32
	char		board_partno[BNGE_VPD_FLD_LEN];
	char		board_serialno[BNGE_VPD_FLD_LEN];

	void __iomem	*bar0;

	/* HWRM members */
	u16			hwrm_cmd_seq;
	u16			hwrm_cmd_kong_seq;
	struct dma_pool		*hwrm_dma_pool;
	struct hlist_head	hwrm_pending_list;
	u16			hwrm_max_req_len;
	u16			hwrm_max_ext_req_len;
	unsigned int		hwrm_cmd_timeout;
	unsigned int		hwrm_cmd_max_timeout;
	struct mutex		hwrm_cmd_lock;	/* serialize hwrm messages */
};

#endif /* _BNGE_H_ */
