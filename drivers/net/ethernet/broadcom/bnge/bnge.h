/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Broadcom */

#ifndef _BNGE_H_
#define _BNGE_H_

#define DRV_NAME	"bng_en"
#define DRV_SUMMARY	"Broadcom 800G Ethernet Linux Driver"

#include <linux/etherdevice.h>
#include "../bnxt/bnxt_hsi.h"
#include "bnge_rmem.h"

#define DRV_VER_MAJ	1
#define DRV_VER_MIN	15
#define DRV_VER_UPD	1

extern char bnge_driver_name[];

enum board_idx {
	BCM57708,
};

#define INVALID_HW_RING_ID      ((u16)-1)

enum {
	BNGE_FW_CAP_SHORT_CMD				= BIT_ULL(0),
	BNGE_FW_CAP_LLDP_AGENT				= BIT_ULL(1),
	BNGE_FW_CAP_DCBX_AGENT				= BIT_ULL(2),
	BNGE_FW_CAP_IF_CHANGE				= BIT_ULL(3),
	BNGE_FW_CAP_KONG_MB_CHNL			= BIT_ULL(4),
	BNGE_FW_CAP_ERROR_RECOVERY			= BIT_ULL(5),
	BNGE_FW_CAP_PKG_VER				= BIT_ULL(6),
	BNGE_FW_CAP_CFA_ADV_FLOW			= BIT_ULL(7),
	BNGE_FW_CAP_CFA_RFS_RING_TBL_IDX_V2		= BIT_ULL(8),
	BNGE_FW_CAP_PCIE_STATS_SUPPORTED		= BIT_ULL(9),
	BNGE_FW_CAP_EXT_STATS_SUPPORTED			= BIT_ULL(10),
	BNGE_FW_CAP_ERR_RECOVER_RELOAD			= BIT_ULL(11),
	BNGE_FW_CAP_HOT_RESET				= BIT_ULL(12),
	BNGE_FW_CAP_RX_ALL_PKT_TS			= BIT_ULL(13),
	BNGE_FW_CAP_VLAN_RX_STRIP			= BIT_ULL(14),
	BNGE_FW_CAP_VLAN_TX_INSERT			= BIT_ULL(15),
	BNGE_FW_CAP_EXT_HW_STATS_SUPPORTED		= BIT_ULL(16),
	BNGE_FW_CAP_LIVEPATCH				= BIT_ULL(17),
	BNGE_FW_CAP_HOT_RESET_IF			= BIT_ULL(18),
	BNGE_FW_CAP_RING_MONITOR			= BIT_ULL(19),
	BNGE_FW_CAP_DBG_QCAPS				= BIT_ULL(20),
	BNGE_FW_CAP_THRESHOLD_TEMP_SUPPORTED		= BIT_ULL(21),
	BNGE_FW_CAP_DFLT_VLAN_TPID_PCP			= BIT_ULL(22),
	BNGE_FW_CAP_VNIC_TUNNEL_TPA			= BIT_ULL(23),
	BNGE_FW_CAP_CFA_NTUPLE_RX_EXT_IP_PROTO		= BIT_ULL(24),
	BNGE_FW_CAP_CFA_RFS_RING_TBL_IDX_V3		= BIT_ULL(25),
	BNGE_FW_CAP_VNIC_RE_FLUSH			= BIT_ULL(26),
};

enum {
	BNGE_EN_ROCE_V1					= BIT_ULL(0),
	BNGE_EN_ROCE_V2					= BIT_ULL(1),
};

#define BNGE_EN_ROCE		(BNGE_EN_ROCE_V1 | BNGE_EN_ROCE_V2)

struct bnge_dev {
	struct device	*dev;
	struct pci_dev	*pdev;
	u64	dsn;
#define BNGE_VPD_FLD_LEN	32
	char		board_partno[BNGE_VPD_FLD_LEN];
	char		board_serialno[BNGE_VPD_FLD_LEN];

	void __iomem	*bar0;

	u16		chip_num;
	u8		chip_rev;

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

	struct hwrm_ver_get_output	ver_resp;
#define FW_VER_STR_LEN		32
	char			fw_ver_str[FW_VER_STR_LEN];
	char			hwrm_ver_supp[FW_VER_STR_LEN];
	char			nvm_cfg_ver[FW_VER_STR_LEN];
	u64			fw_ver_code;
#define BNGE_FW_VER_CODE(maj, min, bld, rsv)			\
	((u64)(maj) << 48 | (u64)(min) << 32 | (u64)(bld) << 16 | (rsv))

	unsigned long           state;
#define BNGE_STATE_DRV_REGISTERED      0

	u64			fw_cap;

	/* Backing stores */
	struct bnge_ctx_mem_info	*ctx;

	u64			flags;
};

static inline bool bnge_is_roce_en(struct bnge_dev *bd)
{
	return bd->flags & BNGE_EN_ROCE;
}

#endif /* _BNGE_H_ */
