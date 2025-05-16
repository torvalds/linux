/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU Ethernet driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#ifndef OTX2_COMMON_H
#define OTX2_COMMON_H

#include <linux/ethtool.h>
#include <linux/pci.h>
#include <linux/iommu.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/timecounter.h>
#include <linux/soc/marvell/octeontx2/asm.h>
#include <net/macsec.h>
#include <net/pkt_cls.h>
#include <net/devlink.h>
#include <linux/time64.h>
#include <linux/dim.h>
#include <uapi/linux/if_macsec.h>
#include <net/page_pool/helpers.h>

#include <mbox.h>
#include <npc.h>
#include "otx2_reg.h"
#include "otx2_txrx.h"
#include "otx2_devlink.h"
#include <rvu_trace.h>
#include "qos.h"
#include "rep.h"
#include "cn10k_ipsec.h"

/* IPv4 flag more fragment bit */
#define IPV4_FLAG_MORE				0x20

/* PCI device IDs */
#define PCI_DEVID_OCTEONTX2_RVU_PF              0xA063
#define PCI_DEVID_OCTEONTX2_RVU_VF		0xA064
#define PCI_DEVID_OCTEONTX2_RVU_AFVF		0xA0F8

#define PCI_SUBSYS_DEVID_96XX_RVU_PFVF		0xB200
#define PCI_SUBSYS_DEVID_CN10K_A_RVU_PFVF	0xB900
#define PCI_SUBSYS_DEVID_CN10K_B_RVU_PFVF	0xBD00

#define PCI_DEVID_OCTEONTX2_SDP_REP		0xA0F7

/* PCI BAR nos */
#define PCI_CFG_REG_BAR_NUM                     2
#define PCI_MBOX_BAR_NUM                        4

#define NAME_SIZE                               32

#ifdef CONFIG_DCB
/* Max priority supported for PFC */
#define NIX_PF_PFC_PRIO_MAX			8
#endif

/* Number of segments per SG structure */
#define MAX_SEGS_PER_SG 3

enum arua_mapped_qtypes {
	AURA_NIX_RQ,
	AURA_NIX_SQ,
};

/* NIX LF interrupts range*/
#define NIX_LF_QINT_VEC_START			0x00
#define NIX_LF_CINT_VEC_START			0x40
#define NIX_LF_GINT_VEC				0x80
#define NIX_LF_ERR_VEC				0x81
#define NIX_LF_POISON_VEC			0x82

/* Send skid of 2000 packets required for CQ size of 4K CQEs. */
#define SEND_CQ_SKID	2000

#define OTX2_GET_RX_STATS(reg) \
	otx2_read64(pfvf, NIX_LF_RX_STATX(reg))
#define OTX2_GET_TX_STATS(reg) \
	otx2_read64(pfvf, NIX_LF_TX_STATX(reg))

struct otx2_lmt_info {
	u64 lmt_addr;
	u16 lmt_id;
};
/* RSS configuration */
struct otx2_rss_ctx {
	u8  ind_tbl[MAX_RSS_INDIR_TBL_SIZE];
};

struct otx2_rss_info {
	u8 enable;
	u32 flowkey_cfg;
	u16 rss_size;
#define RSS_HASH_KEY_SIZE	44   /* 352 bit key */
	u8  key[RSS_HASH_KEY_SIZE];
	struct otx2_rss_ctx	*rss_ctx[MAX_RSS_GROUPS];
};

/* NIX (or NPC) RX errors */
enum otx2_errlvl {
	NPC_ERRLVL_RE,
	NPC_ERRLVL_LID_LA,
	NPC_ERRLVL_LID_LB,
	NPC_ERRLVL_LID_LC,
	NPC_ERRLVL_LID_LD,
	NPC_ERRLVL_LID_LE,
	NPC_ERRLVL_LID_LF,
	NPC_ERRLVL_LID_LG,
	NPC_ERRLVL_LID_LH,
	NPC_ERRLVL_NIX = 0x0F,
};

enum otx2_errcodes_re {
	/* NPC_ERRLVL_RE errcodes */
	ERRCODE_FCS = 0x7,
	ERRCODE_FCS_RCV = 0x8,
	ERRCODE_UNDERSIZE = 0x10,
	ERRCODE_OVERSIZE = 0x11,
	ERRCODE_OL2_LEN_MISMATCH = 0x12,
	/* NPC_ERRLVL_NIX errcodes */
	ERRCODE_OL3_LEN = 0x10,
	ERRCODE_OL4_LEN = 0x11,
	ERRCODE_OL4_CSUM = 0x12,
	ERRCODE_IL3_LEN = 0x20,
	ERRCODE_IL4_LEN = 0x21,
	ERRCODE_IL4_CSUM = 0x22,
};

enum otx2_xdp_action {
	OTX2_XDP_TX	  = BIT(0),
	OTX2_XDP_REDIRECT = BIT(1),
	OTX2_AF_XDP_FRAME = BIT(2),
};

struct otx2_dev_stats {
	u64 rx_bytes;
	u64 rx_frames;
	u64 rx_ucast_frames;
	u64 rx_bcast_frames;
	u64 rx_mcast_frames;
	u64 rx_drops;

	u64 tx_bytes;
	u64 tx_frames;
	u64 tx_ucast_frames;
	u64 tx_bcast_frames;
	u64 tx_mcast_frames;
	u64 tx_drops;
};

/* Driver counted stats */
struct otx2_drv_stats {
	atomic_t rx_fcs_errs;
	atomic_t rx_oversize_errs;
	atomic_t rx_undersize_errs;
	atomic_t rx_csum_errs;
	atomic_t rx_len_errs;
	atomic_t rx_other_errs;
};

struct mbox {
	struct otx2_mbox	mbox;
	struct work_struct	mbox_wrk;
	struct otx2_mbox	mbox_up;
	struct work_struct	mbox_up_wrk;
	struct otx2_nic		*pfvf;
	void			*bbuf_base; /* Bounce buffer for mbox memory */
	struct mutex		lock;	/* serialize mailbox access */
	int			num_msgs; /* mbox number of messages */
	int			up_num_msgs; /* mbox_up number of messages */
};

/* Egress rate limiting definitions */
#define MAX_BURST_EXPONENT		0x0FULL
#define MAX_BURST_MANTISSA		0xFFULL
#define MAX_BURST_SIZE			130816ULL
#define MAX_RATE_DIVIDER_EXPONENT	12ULL
#define MAX_RATE_EXPONENT		0x0FULL
#define MAX_RATE_MANTISSA		0xFFULL

/* Bitfields in NIX_TLX_PIR register */
#define TLX_RATE_MANTISSA		GENMASK_ULL(8, 1)
#define TLX_RATE_EXPONENT		GENMASK_ULL(12, 9)
#define TLX_RATE_DIVIDER_EXPONENT	GENMASK_ULL(16, 13)
#define TLX_BURST_MANTISSA		GENMASK_ULL(36, 29)
#define TLX_BURST_EXPONENT		GENMASK_ULL(40, 37)

struct otx2_hw {
	struct pci_dev		*pdev;
	struct otx2_rss_info	rss_info;
	u16                     rx_queues;
	u16                     tx_queues;
	u16                     xdp_queues;
	u16			tc_tx_queues;
	u16                     non_qos_queues; /* tx queues plus xdp queues */
	u16			max_queues;
	u16			pool_cnt;
	u16			rqpool_cnt;
	u16			sqpool_cnt;

#define OTX2_DEFAULT_RBUF_LEN	2048
	u16			rbuf_len;
	u32			xqe_size;

	/* NPA */
	u32			stack_pg_ptrs;  /* No of ptrs per stack page */
	u32			stack_pg_bytes; /* Size of stack page */
	u16			sqb_size;

	/* NIX */
	u8			txschq_link_cfg_lvl;
	u8			txschq_cnt[NIX_TXSCH_LVL_CNT];
	u8			txschq_aggr_lvl_rr_prio;
	u16			txschq_list[NIX_TXSCH_LVL_CNT][MAX_TXSCHQ_PER_FUNC];
	u16			matchall_ipolicer;
	u32			dwrr_mtu;
	u32			max_mtu;
	u8			smq_link_type;

	/* HW settings, coalescing etc */
	u16			rx_chan_base;
	u16			tx_chan_base;
	u8			rx_chan_cnt;
	u8			tx_chan_cnt;
	u16			cq_qcount_wait;
	u16			cq_ecount_wait;
	u16			rq_skid;
	u8			cq_time_wait;

	/* Segmentation */
	u8			lso_tsov4_idx;
	u8			lso_tsov6_idx;
	u8			lso_udpv4_idx;
	u8			lso_udpv6_idx;

	/* RSS */
	u8			flowkey_alg_idx;

	/* MSI-X */
	u8			cint_cnt; /* CQ interrupt count */
	u16			npa_msixoff; /* Offset of NPA vectors */
	u16			nix_msixoff; /* Offset of NIX vectors */
	char			*irq_name;
	cpumask_var_t           *affinity_mask;

	/* Stats */
	struct otx2_dev_stats	dev_stats;
	struct otx2_drv_stats	drv_stats;
	u64			cgx_rx_stats[CGX_RX_STATS_COUNT];
	u64			cgx_tx_stats[CGX_TX_STATS_COUNT];
	u64			cgx_fec_corr_blks;
	u64			cgx_fec_uncorr_blks;
	u8			cgx_links;  /* No. of CGX links present in HW */
	u8			lbk_links;  /* No. of LBK links present in HW */
	u8			tx_link;    /* Transmit channel link number */
#define HW_TSO			0
#define CN10K_MBOX		1
#define CN10K_LMTST		2
#define CN10K_RPM		3
#define CN10K_PTP_ONESTEP	4
#define CN10K_HW_MACSEC		5
#define QOS_CIR_PIR_SUPPORT	6
	unsigned long		cap_flag;

#define LMT_LINE_SIZE		128
#define LMT_BURST_SIZE		32 /* 32 LMTST lines for burst SQE flush */
	u64			*lmt_base;
	struct otx2_lmt_info	__percpu *lmt_info;
};

enum vfperm {
	OTX2_RESET_VF_PERM,
	OTX2_TRUSTED_VF,
};

struct otx2_vf_config {
	struct otx2_nic *pf;
	struct delayed_work link_event_work;
	bool intf_down; /* interface was either configured or not */
	u8 mac[ETH_ALEN];
	u16 vlan;
	int tx_vtag_idx;
	bool trusted;
};

struct flr_work {
	struct work_struct work;
	struct otx2_nic *pf;
};

struct refill_work {
	struct delayed_work pool_refill_work;
	struct otx2_nic *pf;
	struct napi_struct *napi;
};

/* PTPv2 originTimestamp structure */
struct ptpv2_tstamp {
	__be16 seconds_msb; /* 16 bits + */
	__be32 seconds_lsb; /* 32 bits = 48 bits*/
	__be32 nanoseconds;
} __packed;

struct otx2_ptp {
	struct ptp_clock_info ptp_info;
	struct ptp_clock *ptp_clock;
	struct otx2_nic *nic;

	struct cyclecounter cycle_counter;
	struct timecounter time_counter;

	struct delayed_work extts_work;
	u64 last_extts;
	u64 thresh;

	struct ptp_pin_desc extts_config;
	u64 (*convert_rx_ptp_tstmp)(u64 timestamp);
	u64 (*convert_tx_ptp_tstmp)(u64 timestamp);
	u64 (*ptp_tstamp2nsec)(const struct timecounter *time_counter, u64 timestamp);
	struct delayed_work synctstamp_work;
	u64 tstamp;
	u32 base_ns;
};

#define OTX2_HW_TIMESTAMP_LEN	8

struct otx2_mac_table {
	u8 addr[ETH_ALEN];
	u16 mcam_entry;
	bool inuse;
};

struct otx2_flow_config {
	u16			*flow_ent;
	u16			*def_ent;
	u16			nr_flows;
#define OTX2_DEFAULT_FLOWCOUNT		16
#define OTX2_DEFAULT_UNICAST_FLOWS	4
#define OTX2_MAX_VLAN_FLOWS		1
#define OTX2_MAX_TC_FLOWS	OTX2_DEFAULT_FLOWCOUNT
	u16			unicast_offset;
	u16			rx_vlan_offset;
	u16			vf_vlan_offset;
#define OTX2_PER_VF_VLAN_FLOWS	2 /* Rx + Tx per VF */
#define OTX2_VF_VLAN_RX_INDEX	0
#define OTX2_VF_VLAN_TX_INDEX	1
	u32			*bmap_to_dmacindex;
	unsigned long		*dmacflt_bmap;
	struct list_head	flow_list;
	u32			dmacflt_max_flows;
	u16                     max_flows;
	refcount_t		mark_flows;
	struct list_head	flow_list_tc;
	u8			ucast_flt_cnt;
	bool			ntuple;
};

struct dev_hw_ops {
	int	(*sq_aq_init)(void *dev, u16 qidx, u8 chan_offset,
			      u16 sqb_aura);
	void	(*sqe_flush)(void *dev, struct otx2_snd_queue *sq,
			     int size, int qidx);
	int	(*refill_pool_ptrs)(void *dev, struct otx2_cq_queue *cq);
	void	(*aura_freeptr)(void *dev, int aura, u64 buf);
};

#define CN10K_MCS_SA_PER_SC	4

/* Stats which need to be accumulated in software because
 * of shared counters in hardware.
 */
struct cn10k_txsc_stats {
	u64 InPktsUntagged;
	u64 InPktsNoTag;
	u64 InPktsBadTag;
	u64 InPktsUnknownSCI;
	u64 InPktsNoSCI;
	u64 InPktsOverrun;
};

struct cn10k_rxsc_stats {
	u64 InOctetsValidated;
	u64 InOctetsDecrypted;
	u64 InPktsUnchecked;
	u64 InPktsDelayed;
	u64 InPktsOK;
	u64 InPktsInvalid;
	u64 InPktsLate;
	u64 InPktsNotValid;
	u64 InPktsNotUsingSA;
	u64 InPktsUnusedSA;
};

struct cn10k_mcs_txsc {
	struct macsec_secy *sw_secy;
	struct cn10k_txsc_stats stats;
	struct list_head entry;
	enum macsec_validation_type last_validate_frames;
	bool last_replay_protect;
	u16 hw_secy_id_tx;
	u16 hw_secy_id_rx;
	u16 hw_flow_id;
	u16 hw_sc_id;
	u16 hw_sa_id[CN10K_MCS_SA_PER_SC];
	u8 sa_bmap;
	u8 sa_key[CN10K_MCS_SA_PER_SC][MACSEC_MAX_KEY_LEN];
	u8 encoding_sa;
	u8 salt[CN10K_MCS_SA_PER_SC][MACSEC_SALT_LEN];
	ssci_t ssci[CN10K_MCS_SA_PER_SC];
	bool vlan_dev; /* macsec running on VLAN ? */
};

struct cn10k_mcs_rxsc {
	struct macsec_secy *sw_secy;
	struct macsec_rx_sc *sw_rxsc;
	struct cn10k_rxsc_stats stats;
	struct list_head entry;
	u16 hw_flow_id;
	u16 hw_sc_id;
	u16 hw_sa_id[CN10K_MCS_SA_PER_SC];
	u8 sa_bmap;
	u8 sa_key[CN10K_MCS_SA_PER_SC][MACSEC_MAX_KEY_LEN];
	u8 salt[CN10K_MCS_SA_PER_SC][MACSEC_SALT_LEN];
	ssci_t ssci[CN10K_MCS_SA_PER_SC];
};

struct cn10k_mcs_cfg {
	struct list_head txsc_list;
	struct list_head rxsc_list;
};

struct otx2_nic {
	void __iomem		*reg_base;
	struct net_device	*netdev;
	struct dev_hw_ops	*hw_ops;
	void			*iommu_domain;
	u16			tx_max_pktlen;
	u16			rbsize; /* Receive buffer size */

#define OTX2_FLAG_RX_TSTAMP_ENABLED		BIT_ULL(0)
#define OTX2_FLAG_TX_TSTAMP_ENABLED		BIT_ULL(1)
#define OTX2_FLAG_INTF_DOWN			BIT_ULL(2)
#define OTX2_FLAG_MCAM_ENTRIES_ALLOC		BIT_ULL(3)
#define OTX2_FLAG_NTUPLE_SUPPORT		BIT_ULL(4)
#define OTX2_FLAG_UCAST_FLTR_SUPPORT		BIT_ULL(5)
#define OTX2_FLAG_RX_VLAN_SUPPORT		BIT_ULL(6)
#define OTX2_FLAG_VF_VLAN_SUPPORT		BIT_ULL(7)
#define OTX2_FLAG_PF_SHUTDOWN			BIT_ULL(8)
#define OTX2_FLAG_RX_PAUSE_ENABLED		BIT_ULL(9)
#define OTX2_FLAG_TX_PAUSE_ENABLED		BIT_ULL(10)
#define OTX2_FLAG_TC_FLOWER_SUPPORT		BIT_ULL(11)
#define OTX2_FLAG_TC_MATCHALL_EGRESS_ENABLED	BIT_ULL(12)
#define OTX2_FLAG_TC_MATCHALL_INGRESS_ENABLED	BIT_ULL(13)
#define OTX2_FLAG_DMACFLTR_SUPPORT		BIT_ULL(14)
#define OTX2_FLAG_PTP_ONESTEP_SYNC		BIT_ULL(15)
#define OTX2_FLAG_ADPTV_INT_COAL_ENABLED BIT_ULL(16)
#define OTX2_FLAG_TC_MARK_ENABLED		BIT_ULL(17)
#define OTX2_FLAG_REP_MODE_ENABLED		 BIT_ULL(18)
#define OTX2_FLAG_PORT_UP			BIT_ULL(19)
#define OTX2_FLAG_IPSEC_OFFLOAD_ENABLED		BIT_ULL(20)
	u64			flags;
	u64			*cq_op_addr;

	struct bpf_prog		*xdp_prog;
	struct otx2_qset	qset;
	struct otx2_hw		hw;
	struct pci_dev		*pdev;
	struct device		*dev;

	/* Mbox */
	struct mbox		mbox;
	struct mbox		*mbox_pfvf;
	struct workqueue_struct *mbox_wq;
	struct workqueue_struct *mbox_pfvf_wq;

	u8			total_vfs;
	u16			pcifunc; /* RVU PF_FUNC */
	u16			bpid[NIX_MAX_BPID_CHAN];
	struct otx2_vf_config	*vf_configs;
	struct cgx_link_user_info linfo;

	/* NPC MCAM */
	struct otx2_flow_config	*flow_cfg;
	struct otx2_mac_table	*mac_table;

	u64			reset_count;
	struct work_struct	reset_task;
	struct workqueue_struct	*flr_wq;
	struct flr_work		*flr_wrk;
	struct refill_work	*refill_wrk;
	struct workqueue_struct	*otx2_wq;
	struct work_struct	rx_mode_work;

	/* Ethtool stuff */
	u32			msg_enable;

	/* Block address of NIX either BLKADDR_NIX0 or BLKADDR_NIX1 */
	int			nix_blkaddr;
	/* LMTST Lines info */
	struct qmem		*dync_lmt;
	u16			tot_lmt_lines;
	u16			npa_lmt_lines;
	u32			nix_lmt_size;

	struct otx2_ptp		*ptp;
	struct hwtstamp_config	tstamp;

	unsigned long		rq_bmap;

	/* Devlink */
	struct otx2_devlink	*dl;
	/* PFC */
	u8			pfc_en;
#ifdef CONFIG_DCB
	u8			*queue_to_pfc_map;
	u16			pfc_schq_list[NIX_TXSCH_LVL_CNT][MAX_TXSCHQ_PER_FUNC];
	bool			pfc_alloc_status[NIX_PF_PFC_PRIO_MAX];
#endif
	/* qos */
	struct otx2_qos		qos;

	/* napi event count. It is needed for adaptive irq coalescing. */
	u32 napi_events;

#if IS_ENABLED(CONFIG_MACSEC)
	struct cn10k_mcs_cfg	*macsec_cfg;
#endif

#if IS_ENABLED(CONFIG_RVU_ESWITCH)
	struct rep_dev		**reps;
	int			rep_cnt;
	u16			rep_pf_map[RVU_MAX_REP];
	u16			esw_mode;
#endif

	/* Inline ipsec */
	struct cn10k_ipsec	ipsec;
	/* af_xdp zero-copy */
	unsigned long		*af_xdp_zc_qidx;
};

static inline bool is_otx2_lbkvf(struct pci_dev *pdev)
{
	return (pdev->device == PCI_DEVID_OCTEONTX2_RVU_AFVF) ||
		(pdev->device == PCI_DEVID_RVU_REP);
}

static inline bool is_96xx_A0(struct pci_dev *pdev)
{
	return (pdev->revision == 0x00) &&
		(pdev->subsystem_device == PCI_SUBSYS_DEVID_96XX_RVU_PFVF);
}

static inline bool is_96xx_B0(struct pci_dev *pdev)
{
	return (pdev->revision == 0x01) &&
		(pdev->subsystem_device == PCI_SUBSYS_DEVID_96XX_RVU_PFVF);
}

static inline bool is_otx2_sdp_rep(struct pci_dev *pdev)
{
	return pdev->device == PCI_DEVID_OCTEONTX2_SDP_REP;
}

/* REVID for PCIe devices.
 * Bits 0..1: minor pass, bit 3..2: major pass
 * bits 7..4: midr id
 */
#define PCI_REVISION_ID_96XX		0x00
#define PCI_REVISION_ID_95XX		0x10
#define PCI_REVISION_ID_95XXN		0x20
#define PCI_REVISION_ID_98XX		0x30
#define PCI_REVISION_ID_95XXMM		0x40
#define PCI_REVISION_ID_95XXO		0xE0

static inline bool is_dev_otx2(struct pci_dev *pdev)
{
	u8 midr = pdev->revision & 0xF0;

	return (midr == PCI_REVISION_ID_96XX || midr == PCI_REVISION_ID_95XX ||
		midr == PCI_REVISION_ID_95XXN || midr == PCI_REVISION_ID_98XX ||
		midr == PCI_REVISION_ID_95XXMM || midr == PCI_REVISION_ID_95XXO);
}

static inline bool is_dev_cn10kb(struct pci_dev *pdev)
{
	return pdev->subsystem_device == PCI_SUBSYS_DEVID_CN10K_B_RVU_PFVF;
}

static inline bool is_dev_cn10ka_b0(struct pci_dev *pdev)
{
	if (pdev->subsystem_device == PCI_SUBSYS_DEVID_CN10K_A_RVU_PFVF &&
	    (pdev->revision & 0xFF) == 0x54)
		return true;

	return false;
}

static inline void otx2_setup_dev_hw_settings(struct otx2_nic *pfvf)
{
	struct otx2_hw *hw = &pfvf->hw;

	pfvf->hw.cq_time_wait = CQ_TIMER_THRESH_DEFAULT;
	pfvf->hw.cq_ecount_wait = CQ_CQE_THRESH_DEFAULT;
	pfvf->hw.cq_qcount_wait = CQ_QCOUNT_DEFAULT;

	__set_bit(HW_TSO, &hw->cap_flag);

	if (is_96xx_A0(pfvf->pdev)) {
		__clear_bit(HW_TSO, &hw->cap_flag);

		/* Time based irq coalescing is not supported */
		pfvf->hw.cq_qcount_wait = 0x0;

		/* Due to HW issue previous silicons required minimum
		 * 600 unused CQE to avoid CQ overflow.
		 */
		pfvf->hw.rq_skid = 600;
		pfvf->qset.rqe_cnt = Q_COUNT(Q_SIZE_1K);
	}
	if (is_96xx_B0(pfvf->pdev))
		__clear_bit(HW_TSO, &hw->cap_flag);

	if (!is_dev_otx2(pfvf->pdev)) {
		__set_bit(CN10K_MBOX, &hw->cap_flag);
		__set_bit(CN10K_LMTST, &hw->cap_flag);
		__set_bit(CN10K_RPM, &hw->cap_flag);
		__set_bit(CN10K_PTP_ONESTEP, &hw->cap_flag);
		__set_bit(QOS_CIR_PIR_SUPPORT, &hw->cap_flag);
	}

	if (is_dev_cn10kb(pfvf->pdev))
		__set_bit(CN10K_HW_MACSEC, &hw->cap_flag);
}

/* Register read/write APIs */
static inline void __iomem *otx2_get_regaddr(struct otx2_nic *nic, u64 offset)
{
	u64 blkaddr;

	switch ((offset >> RVU_FUNC_BLKADDR_SHIFT) & RVU_FUNC_BLKADDR_MASK) {
	case BLKTYPE_NIX:
		blkaddr = nic->nix_blkaddr;
		break;
	case BLKTYPE_NPA:
		blkaddr = BLKADDR_NPA;
		break;
	case BLKTYPE_CPT:
		blkaddr = BLKADDR_CPT0;
		break;
	default:
		blkaddr = BLKADDR_RVUM;
		break;
	}

	offset &= ~(RVU_FUNC_BLKADDR_MASK << RVU_FUNC_BLKADDR_SHIFT);
	offset |= (blkaddr << RVU_FUNC_BLKADDR_SHIFT);

	return nic->reg_base + offset;
}

static inline void otx2_write64(struct otx2_nic *nic, u64 offset, u64 val)
{
	void __iomem *addr = otx2_get_regaddr(nic, offset);

	writeq(val, addr);
}

static inline u64 otx2_read64(struct otx2_nic *nic, u64 offset)
{
	void __iomem *addr = otx2_get_regaddr(nic, offset);

	return readq(addr);
}

/* Mbox bounce buffer APIs */
static inline int otx2_mbox_bbuf_init(struct mbox *mbox, struct pci_dev *pdev)
{
	struct otx2_mbox *otx2_mbox;
	struct otx2_mbox_dev *mdev;

	mbox->bbuf_base = devm_kmalloc(&pdev->dev, MBOX_SIZE, GFP_KERNEL);
	if (!mbox->bbuf_base)
		return -ENOMEM;

	/* Overwrite mbox mbase to point to bounce buffer, so that PF/VF
	 * prepare all mbox messages in bounce buffer instead of directly
	 * in hw mbox memory.
	 */
	otx2_mbox = &mbox->mbox;
	mdev = &otx2_mbox->dev[0];
	mdev->mbase = mbox->bbuf_base;

	otx2_mbox = &mbox->mbox_up;
	mdev = &otx2_mbox->dev[0];
	mdev->mbase = mbox->bbuf_base;
	return 0;
}

static inline void otx2_sync_mbox_bbuf(struct otx2_mbox *mbox, int devid)
{
	u16 msgs_offset = ALIGN(sizeof(struct mbox_hdr), MBOX_MSG_ALIGN);
	void *hw_mbase = mbox->hwbase + (devid * MBOX_SIZE);
	struct otx2_mbox_dev *mdev = &mbox->dev[devid];
	struct mbox_hdr *hdr;
	u64 msg_size;

	if (mdev->mbase == hw_mbase)
		return;

	hdr = hw_mbase + mbox->rx_start;
	msg_size = hdr->msg_size;

	if (msg_size > mbox->rx_size - msgs_offset)
		msg_size = mbox->rx_size - msgs_offset;

	/* Copy mbox messages from mbox memory to bounce buffer */
	memcpy(mdev->mbase + mbox->rx_start,
	       hw_mbase + mbox->rx_start, msg_size + msgs_offset);
}

/* With the absence of API for 128-bit IO memory access for arm64,
 * implement required operations at place.
 */
#if defined(CONFIG_ARM64)
static inline void otx2_write128(u64 lo, u64 hi, void __iomem *addr)
{
	__asm__ volatile("stp %x[x0], %x[x1], [%x[p1],#0]!"
			 ::[x0]"r"(lo), [x1]"r"(hi), [p1]"r"(addr));
}

static inline u64 otx2_atomic64_add(u64 incr, u64 *ptr)
{
	u64 result;

	__asm__ volatile(".cpu   generic+lse\n"
			 "ldadd %x[i], %x[r], [%[b]]"
			 : [r]"=r"(result), "+m"(*ptr)
			 : [i]"r"(incr), [b]"r"(ptr)
			 : "memory");
	return result;
}

#else
#define otx2_write128(lo, hi, addr)		writeq((hi) | (lo), addr)
#define otx2_atomic64_add(incr, ptr)		({ *ptr += incr; })
#endif

static inline void __cn10k_aura_freeptr(struct otx2_nic *pfvf, u64 aura,
					u64 *ptrs, u64 num_ptrs)
{
	struct otx2_lmt_info *lmt_info;
	u64 size = 0, count_eot = 0;
	u64 tar_addr, val = 0;

	lmt_info = per_cpu_ptr(pfvf->hw.lmt_info, smp_processor_id());
	tar_addr = (__force u64)otx2_get_regaddr(pfvf, NPA_LF_AURA_BATCH_FREE0);
	/* LMTID is same as AURA Id */
	val = (lmt_info->lmt_id & 0x7FF) | BIT_ULL(63);
	/* Set if [127:64] of last 128bit word has a valid pointer */
	count_eot = (num_ptrs % 2) ? 0ULL : 1ULL;
	/* Set AURA ID to free pointer */
	ptrs[0] = (count_eot << 32) | (aura & 0xFFFFF);
	/* Target address for LMTST flush tells HW how many 128bit
	 * words are valid from NPA_LF_AURA_BATCH_FREE0.
	 *
	 * tar_addr[6:4] is LMTST size-1 in units of 128b.
	 */
	if (num_ptrs > 2) {
		size = (sizeof(u64) * num_ptrs) / 16;
		if (!count_eot)
			size++;
		tar_addr |=  ((size - 1) & 0x7) << 4;
	}
	dma_wmb();
	memcpy((u64 *)lmt_info->lmt_addr, ptrs, sizeof(u64) * num_ptrs);
	/* Perform LMTST flush */
	cn10k_lmt_flush(val, tar_addr);
}

static inline void cn10k_aura_freeptr(void *dev, int aura, u64 buf)
{
	struct otx2_nic *pfvf = dev;
	u64 ptrs[2];

	ptrs[1] = buf;
	get_cpu();
	/* Free only one buffer at time during init and teardown */
	__cn10k_aura_freeptr(pfvf, aura, ptrs, 2);
	put_cpu();
}

/* Alloc pointer from pool/aura */
static inline u64 otx2_aura_allocptr(struct otx2_nic *pfvf, int aura)
{
	u64 *ptr = (__force u64 *)otx2_get_regaddr(pfvf, NPA_LF_AURA_OP_ALLOCX(0));
	u64 incr = (u64)aura | BIT_ULL(63);

	return otx2_atomic64_add(incr, ptr);
}

/* Free pointer to a pool/aura */
static inline void otx2_aura_freeptr(void *dev, int aura, u64 buf)
{
	struct otx2_nic *pfvf = dev;
	void __iomem *addr = otx2_get_regaddr(pfvf, NPA_LF_AURA_OP_FREE0);

	otx2_write128(buf, (u64)aura | BIT_ULL(63), addr);
}

static inline int otx2_get_pool_idx(struct otx2_nic *pfvf, int type, int idx)
{
	if (type == AURA_NIX_SQ)
		return pfvf->hw.rqpool_cnt + idx;

	 /* AURA_NIX_RQ */
	return idx;
}

/* Mbox APIs */
static inline int otx2_sync_mbox_msg(struct mbox *mbox)
{
	int err;

	if (!otx2_mbox_nonempty(&mbox->mbox, 0))
		return 0;
	otx2_mbox_msg_send(&mbox->mbox, 0);
	err = otx2_mbox_wait_for_rsp(&mbox->mbox, 0);
	if (err)
		return err;

	return otx2_mbox_check_rsp_msgs(&mbox->mbox, 0);
}

static inline int otx2_sync_mbox_up_msg(struct mbox *mbox, int devid)
{
	int err;

	if (!otx2_mbox_nonempty(&mbox->mbox_up, devid))
		return 0;
	otx2_mbox_msg_send_up(&mbox->mbox_up, devid);
	err = otx2_mbox_wait_for_rsp(&mbox->mbox_up, devid);
	if (err)
		return err;

	return otx2_mbox_check_rsp_msgs(&mbox->mbox_up, devid);
}

/* Use this API to send mbox msgs in atomic context
 * where sleeping is not allowed
 */
static inline int otx2_sync_mbox_msg_busy_poll(struct mbox *mbox)
{
	int err;

	if (!otx2_mbox_nonempty(&mbox->mbox, 0))
		return 0;
	otx2_mbox_msg_send(&mbox->mbox, 0);
	err = otx2_mbox_busy_poll_for_rsp(&mbox->mbox, 0);
	if (err)
		return err;

	return otx2_mbox_check_rsp_msgs(&mbox->mbox, 0);
}

#define M(_name, _id, _fn_name, _req_type, _rsp_type)                   \
static struct _req_type __maybe_unused					\
*otx2_mbox_alloc_msg_ ## _fn_name(struct mbox *mbox)                    \
{									\
	struct _req_type *req;						\
									\
	req = (struct _req_type *)otx2_mbox_alloc_msg_rsp(		\
		&mbox->mbox, 0, sizeof(struct _req_type),		\
		sizeof(struct _rsp_type));				\
	if (!req)							\
		return NULL;						\
	req->hdr.sig = OTX2_MBOX_REQ_SIG;				\
	req->hdr.id = _id;						\
	trace_otx2_msg_alloc(mbox->mbox.pdev, _id, sizeof(*req));	\
	return req;							\
}

MBOX_MESSAGES
#undef M

#define M(_name, _id, _fn_name, _req_type, _rsp_type)			\
int									\
otx2_mbox_up_handler_ ## _fn_name(struct otx2_nic *pfvf,		\
				struct _req_type *req,			\
				struct _rsp_type *rsp);			\

MBOX_UP_CGX_MESSAGES
MBOX_UP_MCS_MESSAGES
#undef M

/* Time to wait before watchdog kicks off */
#define OTX2_TX_TIMEOUT		(100 * HZ)

#define	RVU_PFVF_PF_SHIFT	10
#define	RVU_PFVF_PF_MASK	0x3F
#define	RVU_PFVF_FUNC_SHIFT	0
#define	RVU_PFVF_FUNC_MASK	0x3FF

static inline bool is_otx2_vf(u16 pcifunc)
{
	return !!(pcifunc & RVU_PFVF_FUNC_MASK);
}

static inline int rvu_get_pf(u16 pcifunc)
{
	return (pcifunc >> RVU_PFVF_PF_SHIFT) & RVU_PFVF_PF_MASK;
}

static inline dma_addr_t otx2_dma_map_page(struct otx2_nic *pfvf,
					   struct page *page,
					   size_t offset, size_t size,
					   enum dma_data_direction dir)
{
	dma_addr_t iova;

	iova = dma_map_page_attrs(pfvf->dev, page,
				  offset, size, dir, DMA_ATTR_SKIP_CPU_SYNC);
	if (unlikely(dma_mapping_error(pfvf->dev, iova)))
		return (dma_addr_t)NULL;
	return iova;
}

static inline void otx2_dma_unmap_page(struct otx2_nic *pfvf,
				       dma_addr_t addr, size_t size,
				       enum dma_data_direction dir)
{
	dma_unmap_page_attrs(pfvf->dev, addr, size,
			     dir, DMA_ATTR_SKIP_CPU_SYNC);
}

static inline u16 otx2_get_smq_idx(struct otx2_nic *pfvf, u16 qidx)
{
	u16 smq;
	int idx;

#ifdef CONFIG_DCB
	if (qidx < NIX_PF_PFC_PRIO_MAX && pfvf->pfc_alloc_status[qidx])
		return pfvf->pfc_schq_list[NIX_TXSCH_LVL_SMQ][qidx];
#endif
	/* check if qidx falls under QOS queues */
	if (qidx >= pfvf->hw.non_qos_queues) {
		smq = pfvf->qos.qid_to_sqmap[qidx - pfvf->hw.non_qos_queues];
	} else {
		idx = qidx % pfvf->hw.txschq_cnt[NIX_TXSCH_LVL_SMQ];
		smq = pfvf->hw.txschq_list[NIX_TXSCH_LVL_SMQ][idx];
	}

	return smq;
}

static inline u16 otx2_get_total_tx_queues(struct otx2_nic *pfvf)
{
	return pfvf->hw.non_qos_queues + pfvf->hw.tc_tx_queues;
}

static inline u64 otx2_convert_rate(u64 rate)
{
	u64 converted_rate;

	/* Convert bytes per second to Mbps */
	converted_rate = rate * 8;
	converted_rate = max_t(u64, converted_rate / 1000000, 1);

	return converted_rate;
}

static inline int otx2_tc_flower_rule_cnt(struct otx2_nic *pfvf)
{
	/* return here if MCAM entries not allocated */
	if (!pfvf->flow_cfg)
		return 0;

	return pfvf->flow_cfg->nr_flows;
}

/* MSI-X APIs */
void otx2_free_cints(struct otx2_nic *pfvf, int n);
void otx2_set_cints_affinity(struct otx2_nic *pfvf);
int otx2_set_mac_address(struct net_device *netdev, void *p);
int otx2_hw_set_mtu(struct otx2_nic *pfvf, int mtu);
void otx2_tx_timeout(struct net_device *netdev, unsigned int txq);
void otx2_get_mac_from_af(struct net_device *netdev);
void otx2_config_irq_coalescing(struct otx2_nic *pfvf, int qidx);
int otx2_config_pause_frm(struct otx2_nic *pfvf);
void otx2_setup_segmentation(struct otx2_nic *pfvf);
int otx2_reset_mac_stats(struct otx2_nic *pfvf);

/* RVU block related APIs */
int otx2_attach_npa_nix(struct otx2_nic *pfvf);
int otx2_detach_resources(struct mbox *mbox);
int otx2_config_npa(struct otx2_nic *pfvf);
int otx2_sq_aura_pool_init(struct otx2_nic *pfvf);
int otx2_rq_aura_pool_init(struct otx2_nic *pfvf);
void otx2_aura_pool_free(struct otx2_nic *pfvf);
void otx2_free_aura_ptr(struct otx2_nic *pfvf, int type);
void otx2_sq_free_sqbs(struct otx2_nic *pfvf);
int otx2_config_nix(struct otx2_nic *pfvf);
int otx2_config_nix_queues(struct otx2_nic *pfvf);
int otx2_txschq_config(struct otx2_nic *pfvf, int lvl, int prio, bool pfc_en);
int otx2_txsch_alloc(struct otx2_nic *pfvf);
void otx2_txschq_stop(struct otx2_nic *pfvf);
void otx2_txschq_free_one(struct otx2_nic *pfvf, u16 lvl, u16 schq);
void otx2_free_pending_sqe(struct otx2_nic *pfvf);
void otx2_sqb_flush(struct otx2_nic *pfvf);
int otx2_alloc_rbuf(struct otx2_nic *pfvf, struct otx2_pool *pool,
		    dma_addr_t *dma, int qidx, int idx);
int otx2_rxtx_enable(struct otx2_nic *pfvf, bool enable);
void otx2_ctx_disable(struct mbox *mbox, int type, bool npa);
int otx2_nix_config_bp(struct otx2_nic *pfvf, bool enable);
int otx2_nix_cpt_config_bp(struct otx2_nic *pfvf, bool enable);
void otx2_cleanup_rx_cqes(struct otx2_nic *pfvf, struct otx2_cq_queue *cq, int qidx);
void otx2_cleanup_tx_cqes(struct otx2_nic *pfvf, struct otx2_cq_queue *cq);
int otx2_sq_init(struct otx2_nic *pfvf, u16 qidx, u16 sqb_aura);
int otx2_sq_aq_init(void *dev, u16 qidx, u8 chan_offset, u16 sqb_aura);
int cn10k_sq_aq_init(void *dev, u16 qidx, u8 chan_offset, u16 sqb_aura);
int otx2_alloc_buffer(struct otx2_nic *pfvf, struct otx2_cq_queue *cq,
		      dma_addr_t *dma);
int otx2_pool_init(struct otx2_nic *pfvf, u16 pool_id,
		   int stack_pages, int numptrs, int buf_size, int type);
int otx2_aura_init(struct otx2_nic *pfvf, int aura_id,
		   int pool_id, int numptrs);
int otx2_init_rsrc(struct pci_dev *pdev, struct otx2_nic *pf);
void otx2_free_queue_mem(struct otx2_qset *qset);
int otx2_alloc_queue_mem(struct otx2_nic *pf);
int otx2_init_hw_resources(struct otx2_nic *pfvf);
void otx2_free_hw_resources(struct otx2_nic *pf);
int otx2_wq_init(struct otx2_nic *pf);
int otx2_check_pf_usable(struct otx2_nic *pf);
int otx2_pfaf_mbox_init(struct otx2_nic *pf);
int otx2_register_mbox_intr(struct otx2_nic *pf, bool probe_af);
int otx2_realloc_msix_vectors(struct otx2_nic *pf);
void otx2_pfaf_mbox_destroy(struct otx2_nic *pf);
void otx2_disable_mbox_intr(struct otx2_nic *pf);
void otx2_disable_napi(struct otx2_nic *pf);
irqreturn_t otx2_cq_intr_handler(int irq, void *cq_irq);
int otx2_rq_init(struct otx2_nic *pfvf, u16 qidx, u16 lpb_aura);
int otx2_cq_init(struct otx2_nic *pfvf, u16 qidx);

/* RSS configuration APIs*/
int otx2_rss_init(struct otx2_nic *pfvf);
int otx2_set_flowkey_cfg(struct otx2_nic *pfvf);
void otx2_set_rss_key(struct otx2_nic *pfvf);
int otx2_set_rss_table(struct otx2_nic *pfvf, int ctx_id);

/* Mbox handlers */
void mbox_handler_msix_offset(struct otx2_nic *pfvf,
			      struct msix_offset_rsp *rsp);
void mbox_handler_npa_lf_alloc(struct otx2_nic *pfvf,
			       struct npa_lf_alloc_rsp *rsp);
void mbox_handler_nix_lf_alloc(struct otx2_nic *pfvf,
			       struct nix_lf_alloc_rsp *rsp);
void mbox_handler_nix_txsch_alloc(struct otx2_nic *pf,
				  struct nix_txsch_alloc_rsp *rsp);
void mbox_handler_cgx_stats(struct otx2_nic *pfvf,
			    struct cgx_stats_rsp *rsp);
void mbox_handler_cgx_fec_stats(struct otx2_nic *pfvf,
				struct cgx_fec_stats_rsp *rsp);
void otx2_set_fec_stats_count(struct otx2_nic *pfvf);
void mbox_handler_nix_bp_enable(struct otx2_nic *pfvf,
				struct nix_bp_cfg_rsp *rsp);

/* Device stats APIs */
void otx2_get_dev_stats(struct otx2_nic *pfvf);
void otx2_get_stats64(struct net_device *netdev,
		      struct rtnl_link_stats64 *stats);
void otx2_update_lmac_stats(struct otx2_nic *pfvf);
void otx2_update_lmac_fec_stats(struct otx2_nic *pfvf);
int otx2_update_rq_stats(struct otx2_nic *pfvf, int qidx);
int otx2_update_sq_stats(struct otx2_nic *pfvf, int qidx);
void otx2_set_ethtool_ops(struct net_device *netdev);
void otx2vf_set_ethtool_ops(struct net_device *netdev);

int otx2_open(struct net_device *netdev);
int otx2_stop(struct net_device *netdev);
int otx2_set_real_num_queues(struct net_device *netdev,
			     int tx_queues, int rx_queues);
int otx2_ioctl(struct net_device *netdev, struct ifreq *req, int cmd);
int otx2_config_hwtstamp(struct net_device *netdev, struct ifreq *ifr);

/* MCAM filter related APIs */
int otx2_mcam_flow_init(struct otx2_nic *pf);
int otx2vf_mcam_flow_init(struct otx2_nic *pfvf);
int otx2_alloc_mcam_entries(struct otx2_nic *pfvf, u16 count);
void otx2_mcam_flow_del(struct otx2_nic *pf);
int otx2_destroy_ntuple_flows(struct otx2_nic *pf);
int otx2_destroy_mcam_flows(struct otx2_nic *pfvf);
int otx2_get_flow(struct otx2_nic *pfvf,
		  struct ethtool_rxnfc *nfc, u32 location);
int otx2_get_all_flows(struct otx2_nic *pfvf,
		       struct ethtool_rxnfc *nfc, u32 *rule_locs);
int otx2_add_flow(struct otx2_nic *pfvf,
		  struct ethtool_rxnfc *nfc);
int otx2_remove_flow(struct otx2_nic *pfvf, u32 location);
int otx2_get_maxflows(struct otx2_flow_config *flow_cfg);
void otx2_rss_ctx_flow_del(struct otx2_nic *pfvf, int ctx_id);
int otx2_del_macfilter(struct net_device *netdev, const u8 *mac);
int otx2_add_macfilter(struct net_device *netdev, const u8 *mac);
int otx2_enable_rxvlan(struct otx2_nic *pf, bool enable);
int otx2_install_rxvlan_offload_flow(struct otx2_nic *pfvf);
bool otx2_xdp_sq_append_pkt(struct otx2_nic *pfvf, struct xdp_frame *xdpf,
			    u64 iova, int len, u16 qidx, u16 flags);
u16 otx2_get_max_mtu(struct otx2_nic *pfvf);
int otx2_handle_ntuple_tc_features(struct net_device *netdev,
				   netdev_features_t features);
int otx2_smq_flush(struct otx2_nic *pfvf, int smq);
void otx2_free_bufs(struct otx2_nic *pfvf, struct otx2_pool *pool,
		    u64 iova, int size);
int otx2_mcam_entry_init(struct otx2_nic *pfvf);

/* tc support */
int otx2_init_tc(struct otx2_nic *nic);
void otx2_shutdown_tc(struct otx2_nic *nic);
int otx2_setup_tc(struct net_device *netdev, enum tc_setup_type type,
		  void *type_data);
void otx2_tc_apply_ingress_police_rules(struct otx2_nic *nic);

/* CGX/RPM DMAC filters support */
int otx2_dmacflt_get_max_cnt(struct otx2_nic *pf);
int otx2_dmacflt_add(struct otx2_nic *pf, const u8 *mac, u32 bit_pos);
int otx2_dmacflt_remove(struct otx2_nic *pf, const u8 *mac, u32 bit_pos);
int otx2_dmacflt_update(struct otx2_nic *pf, u8 *mac, u32 bit_pos);
void otx2_dmacflt_reinstall_flows(struct otx2_nic *pf);
void otx2_dmacflt_update_pfmac_flow(struct otx2_nic *pfvf);

#ifdef CONFIG_DCB
/* DCB support*/
void otx2_update_bpid_in_rqctx(struct otx2_nic *pfvf, int vlan_prio, int qidx, bool pfc_enable);
int otx2_config_priority_flow_ctrl(struct otx2_nic *pfvf);
int otx2_dcbnl_set_ops(struct net_device *dev);
/* PFC support */
int otx2_pfc_txschq_config(struct otx2_nic *pfvf);
int otx2_pfc_txschq_alloc(struct otx2_nic *pfvf);
int otx2_pfc_txschq_update(struct otx2_nic *pfvf);
int otx2_pfc_txschq_stop(struct otx2_nic *pfvf);
#endif

#if IS_ENABLED(CONFIG_MACSEC)
/* MACSEC offload support */
int cn10k_mcs_init(struct otx2_nic *pfvf);
void cn10k_mcs_free(struct otx2_nic *pfvf);
void cn10k_handle_mcs_event(struct otx2_nic *pfvf, struct mcs_intr_info *event);
#else
static inline int cn10k_mcs_init(struct otx2_nic *pfvf) { return 0; }
static inline void cn10k_mcs_free(struct otx2_nic *pfvf) {}
static inline void cn10k_handle_mcs_event(struct otx2_nic *pfvf,
					  struct mcs_intr_info *event)
{}
#endif /* CONFIG_MACSEC */

/* qos support */
static inline void otx2_qos_init(struct otx2_nic *pfvf, int qos_txqs)
{
	struct otx2_hw *hw = &pfvf->hw;

	hw->tc_tx_queues = qos_txqs;
	INIT_LIST_HEAD(&pfvf->qos.qos_tree);
	mutex_init(&pfvf->qos.qos_lock);
}

static inline void otx2_shutdown_qos(struct otx2_nic *pfvf)
{
	mutex_destroy(&pfvf->qos.qos_lock);
}

u16 otx2_select_queue(struct net_device *netdev, struct sk_buff *skb,
		      struct net_device *sb_dev);
int otx2_get_txq_by_classid(struct otx2_nic *pfvf, u16 classid);
void otx2_qos_config_txschq(struct otx2_nic *pfvf);
void otx2_clean_qos_queues(struct otx2_nic *pfvf);
int rvu_event_up_notify(struct otx2_nic *pf, struct rep_event *info);
int otx2_setup_tc_cls_flower(struct otx2_nic *nic,
			     struct flow_cls_offload *cls_flower);

static inline int mcam_entry_cmp(const void *a, const void *b)
{
	return *(u16 *)a - *(u16 *)b;
}

dma_addr_t otx2_dma_map_skb_frag(struct otx2_nic *pfvf,
				 struct sk_buff *skb, int seg, int *len);
void otx2_dma_unmap_skb_frags(struct otx2_nic *pfvf, struct sg_list *sg);
int otx2_read_free_sqe(struct otx2_nic *pfvf, u16 qidx);
#endif /* OTX2_COMMON_H */
