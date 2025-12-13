/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Broadcom */

#ifndef _BNGE_H_
#define _BNGE_H_

#define DRV_NAME	"bng_en"
#define DRV_SUMMARY	"Broadcom 800G Ethernet Linux Driver"

#include <linux/etherdevice.h>
#include <linux/bnxt/hsi.h>
#include "bnge_rmem.h"
#include "bnge_resc.h"

#define DRV_VER_MAJ	1
#define DRV_VER_MIN	15
#define DRV_VER_UPD	1

extern char bnge_driver_name[];

enum board_idx {
	BCM57708,
};

struct bnge_pf_info {
	u16	fw_fid;
	u16	port_id;
	u8	mac_addr[ETH_ALEN];
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
	BNGE_EN_STRIP_VLAN				= BIT_ULL(2),
	BNGE_EN_SHARED_CHNL				= BIT_ULL(3),
	BNGE_EN_UDP_GSO_SUPP				= BIT_ULL(4),
};

#define BNGE_EN_ROCE		(BNGE_EN_ROCE_V1 | BNGE_EN_ROCE_V2)

enum {
	BNGE_RSS_CAP_RSS_HASH_TYPE_DELTA		= BIT(0),
	BNGE_RSS_CAP_UDP_RSS_CAP			= BIT(1),
	BNGE_RSS_CAP_NEW_RSS_CAP			= BIT(2),
	BNGE_RSS_CAP_RSS_TCAM				= BIT(3),
	BNGE_RSS_CAP_AH_V4_RSS_CAP			= BIT(4),
	BNGE_RSS_CAP_AH_V6_RSS_CAP			= BIT(5),
	BNGE_RSS_CAP_ESP_V4_RSS_CAP			= BIT(6),
	BNGE_RSS_CAP_ESP_V6_RSS_CAP			= BIT(7),
};

#define BNGE_MAX_QUEUE		8
struct bnge_queue_info {
	u8      queue_id;
	u8      queue_profile;
};

struct bnge_dev {
	struct device	*dev;
	struct pci_dev	*pdev;
	struct net_device	*netdev;
	u64	dsn;
#define BNGE_VPD_FLD_LEN	32
	char		board_partno[BNGE_VPD_FLD_LEN];
	char		board_serialno[BNGE_VPD_FLD_LEN];

	void __iomem	*bar0;
	void __iomem	*bar1;

	u16		chip_num;
	u8		chip_rev;

#if BITS_PER_LONG == 32
	/* ensure atomic 64-bit doorbell writes on 32-bit systems. */
	spinlock_t	db_lock;
#endif
	int		db_offset; /* db_offset within db_size */
	int		db_size;

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

	struct bnge_pf_info	pf;

	unsigned long           state;
#define BNGE_STATE_DRV_REGISTERED      0
#define BNGE_STATE_OPEN			1

	u64			fw_cap;

	/* Backing stores */
	struct bnge_ctx_mem_info	*ctx;

	u64			flags;

	struct bnge_hw_resc	hw_resc;

	u16			tso_max_segs;

	int			max_fltr;
#define BNGE_L2_FLTR_MAX_FLTR	1024

	u32			*rss_indir_tbl;
#define BNGE_RSS_TABLE_ENTRIES	64
#define BNGE_RSS_TABLE_SIZE		(BNGE_RSS_TABLE_ENTRIES * 4)
#define BNGE_RSS_TABLE_MAX_TBL	8
#define BNGE_MAX_RSS_TABLE_SIZE				\
	(BNGE_RSS_TABLE_SIZE * BNGE_RSS_TABLE_MAX_TBL)
#define BNGE_MAX_RSS_TABLE_ENTRIES				\
	(BNGE_RSS_TABLE_ENTRIES * BNGE_RSS_TABLE_MAX_TBL)
	u16			rss_indir_tbl_entries;

	u32			rss_cap;
	u32			rss_hash_cfg;

	u16			rx_nr_rings;
	u16			tx_nr_rings;
	u16			tx_nr_rings_per_tc;
	/* Number of NQs */
	u16			nq_nr_rings;

	/* Aux device resources */
	u16			aux_num_msix;
	u16			aux_num_stat_ctxs;

	u16			max_mtu;
#define BNGE_MAX_MTU		9500

	u16			hw_ring_stats_size;
#define BNGE_NUM_RX_RING_STATS			8
#define BNGE_NUM_TX_RING_STATS			8
#define BNGE_NUM_TPA_RING_STATS			6
#define BNGE_RING_STATS_SIZE					\
	((BNGE_NUM_RX_RING_STATS + BNGE_NUM_TX_RING_STATS +	\
	  BNGE_NUM_TPA_RING_STATS) * 8)

	u16			max_tpa_v2;
#define BNGE_SUPPORTS_TPA(bd)	((bd)->max_tpa_v2)

	u8                      num_tc;
	u8			max_tc;
	u8			max_lltc;	/* lossless TCs */
	struct bnge_queue_info	q_info[BNGE_MAX_QUEUE];
	u8			tc_to_qidx[BNGE_MAX_QUEUE];
	u8			q_ids[BNGE_MAX_QUEUE];
	u8			max_q;
	u8			port_count;

	struct bnge_irq		*irq_tbl;
	u16			irqs_acquired;
};

static inline bool bnge_is_roce_en(struct bnge_dev *bd)
{
	return bd->flags & BNGE_EN_ROCE;
}

static inline bool bnge_is_agg_reqd(struct bnge_dev *bd)
{
	if (bd->netdev) {
		struct bnge_net *bn = netdev_priv(bd->netdev);

		if (bn->priv_flags & BNGE_NET_EN_TPA ||
		    bn->priv_flags & BNGE_NET_EN_JUMBO)
			return true;
		else
			return false;
	}

	return true;
}

static inline void bnge_writeq(struct bnge_dev *bd, u64 val,
			       void __iomem *addr)
{
#if BITS_PER_LONG == 32
	spin_lock(&bd->db_lock);
	lo_hi_writeq(val, addr);
	spin_unlock(&bd->db_lock);
#else
	writeq(val, addr);
#endif
}

/* For TX and RX ring doorbells */
static inline void bnge_db_write(struct bnge_dev *bd, struct bnge_db_info *db,
				 u32 idx)
{
	bnge_writeq(bd, db->db_key64 | DB_RING_IDX(db, idx),
		    db->doorbell);
}

bool bnge_aux_registered(struct bnge_dev *bd);
u16 bnge_aux_get_msix(struct bnge_dev *bd);

#endif /* _BNGE_H_ */
