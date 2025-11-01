/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2025, Advanced Micro Devices, Inc. */

#ifndef _IONIC_LIF_CFG_H_

#define IONIC_VERSION(a, b) (((a) << 16) + ((b) << 8))
#define IONIC_PAGE_SIZE_SUPPORTED	0x40201000 /* 4kb, 2Mb, 1Gb */

#define IONIC_EXPDB_64B_WQE	BIT(0)
#define IONIC_EXPDB_128B_WQE	BIT(1)
#define IONIC_EXPDB_256B_WQE	BIT(2)
#define IONIC_EXPDB_512B_WQE	BIT(3)

struct ionic_lif_cfg {
	struct device *hwdev;
	struct ionic_lif *lif;

	int lif_index;
	int lif_hw_index;

	u32 dbid;
	int dbid_count;
	u64 __iomem *dbpage;
	struct ionic_intr __iomem *intr_ctrl;
	phys_addr_t db_phys;

	u64 page_size_supported;
	u32 npts_per_lif;
	u32 nmrs_per_lif;
	u32 nahs_per_lif;

	u32 aq_base;
	u32 cq_base;
	u32 eq_base;

	int aq_count;
	int eq_count;
	int cq_count;
	int qp_count;

	u16 stats_type;
	u8 aq_qtype;
	u8 sq_qtype;
	u8 rq_qtype;
	u8 cq_qtype;
	u8 eq_qtype;

	u8 udma_count;
	u8 udma_qgrp_shift;

	u8 rdma_version;
	u8 qp_opcodes;
	u8 admin_opcodes;

	u8 max_stride;
	bool sq_expdb;
	bool rq_expdb;
	u8 expdb_mask;
};

void ionic_fill_lif_cfg(struct ionic_lif *lif, struct ionic_lif_cfg *cfg);
struct net_device *ionic_lif_netdev(struct ionic_lif *lif);
void ionic_lif_fw_version(struct ionic_lif *lif, char *str, size_t len);
u8 ionic_lif_asic_rev(struct ionic_lif *lif);

#endif /* _IONIC_LIF_CFG_H_ */
