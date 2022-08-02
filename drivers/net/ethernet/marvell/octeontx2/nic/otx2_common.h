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
#include <net/pkt_cls.h>
#include <net/devlink.h>
#include <linux/time64.h>
#include <linux/dim.h>

#include <mbox.h>
#include <npc.h>
#include "otx2_reg.h"
#include "otx2_txrx.h"
#include "otx2_devlink.h"
#include <rvu_trace.h>

/* PCI device IDs */
#define PCI_DEVID_OCTEONTX2_RVU_PF              0xA063
#define PCI_DEVID_OCTEONTX2_RVU_VF		0xA064
#define PCI_DEVID_OCTEONTX2_RVU_AFVF		0xA0F8

#define PCI_SUBSYS_DEVID_96XX_RVU_PFVF		0xB200

/* PCI BAR nos */
#define PCI_CFG_REG_BAR_NUM                     2
#define PCI_MBOX_BAR_NUM                        4

#define NAME_SIZE                               32

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

/* NIX TX stats */
enum nix_stat_lf_tx {
	TX_UCAST	= 0x0,
	TX_BCAST	= 0x1,
	TX_MCAST	= 0x2,
	TX_DROP		= 0x3,
	TX_OCTS		= 0x4,
	TX_STATS_ENUM_LAST,
};

/* NIX RX stats */
enum nix_stat_lf_rx {
	RX_OCTS		= 0x0,
	RX_UCAST	= 0x1,
	RX_BCAST	= 0x2,
	RX_MCAST	= 0x3,
	RX_DROP		= 0x4,
	RX_DROP_OCTS	= 0x5,
	RX_FCS		= 0x6,
	RX_ERR		= 0x7,
	RX_DRP_BCAST	= 0x8,
	RX_DRP_MCAST	= 0x9,
	RX_DRP_L3BCAST	= 0xa,
	RX_DRP_L3MCAST	= 0xb,
	RX_STATS_ENUM_LAST,
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

struct otx2_hw {
	struct pci_dev		*pdev;
	struct otx2_rss_info	rss_info;
	u16                     rx_queues;
	u16                     tx_queues;
	u16                     xdp_queues;
	u16                     tot_tx_queues;
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
	u16		txschq_list[NIX_TXSCH_LVL_CNT][MAX_TXSCHQ_PER_FUNC];
	u16			matchall_ipolicer;
	u32			dwrr_mtu;

	/* HW settings, coalescing etc */
	u16			rx_chan_base;
	u16			tx_chan_base;
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
};

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
#define OTX2_MAX_UNICAST_FLOWS		8
#define OTX2_MAX_VLAN_FLOWS		1
#define OTX2_MAX_TC_FLOWS	OTX2_DEFAULT_FLOWCOUNT
#define OTX2_MCAM_COUNT		(OTX2_DEFAULT_FLOWCOUNT + \
				 OTX2_MAX_UNICAST_FLOWS + \
				 OTX2_MAX_VLAN_FLOWS)
	u16			unicast_offset;
	u16			rx_vlan_offset;
	u16			vf_vlan_offset;
#define OTX2_PER_VF_VLAN_FLOWS	2 /* Rx + Tx per VF */
#define OTX2_VF_VLAN_RX_INDEX	0
#define OTX2_VF_VLAN_TX_INDEX	1
	u16			max_flows;
	u8			dmacflt_max_flows;
	u8			*bmap_to_dmacindex;
	unsigned long		dmacflt_bmap;
	struct list_head	flow_list;
};

struct otx2_tc_info {
	/* hash table to store TC offloaded flows */
	struct rhashtable		flow_table;
	struct rhashtable_params	flow_ht_params;
	unsigned long			*tc_entries_bitmap;
};

struct dev_hw_ops {
	int	(*sq_aq_init)(void *dev, u16 qidx, u16 sqb_aura);
	void	(*sqe_flush)(void *dev, struct otx2_snd_queue *sq,
			     int size, int qidx);
	void	(*refill_pool_ptrs)(void *dev, struct otx2_cq_queue *cq);
	void	(*aura_freeptr)(void *dev, int aura, u64 buf);
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
#define OTX2_FLAG_ADPTV_INT_COAL_ENABLED BIT_ULL(16)
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
	struct otx2_tc_info	tc_info;

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
#ifdef CONFIG_DCB
	/* PFC */
	u8			pfc_en;
	u8			*queue_to_pfc_map;
#endif

	/* napi event count. It is needed for adaptive irq coalescing. */
	u32 napi_events;
};

static inline bool is_otx2_lbkvf(struct pci_dev *pdev)
{
	return pdev->device == PCI_DEVID_OCTEONTX2_RVU_AFVF;
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
	}
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
	/* Free only one buffer at time during init and teardown */
	__cn10k_aura_freeptr(pfvf, aura, ptrs, 2);
}

/* Alloc pointer from pool/aura */
static inline u64 otx2_aura_allocptr(struct otx2_nic *pfvf, int aura)
{
	u64 *ptr = (u64 *)otx2_get_regaddr(pfvf,
			   NPA_LF_AURA_OP_ALLOCX(0));
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
	otx2_mbox_msg_send(&mbox->mbox_up, devid);
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
int otx2_txschq_config(struct otx2_nic *pfvf, int lvl);
int otx2_txsch_alloc(struct otx2_nic *pfvf);
int otx2_txschq_stop(struct otx2_nic *pfvf);
void otx2_sqb_flush(struct otx2_nic *pfvf);
int __otx2_alloc_rbuf(struct otx2_nic *pfvf, struct otx2_pool *pool,
		      dma_addr_t *dma);
int otx2_rxtx_enable(struct otx2_nic *pfvf, bool enable);
void otx2_ctx_disable(struct mbox *mbox, int type, bool npa);
int otx2_nix_config_bp(struct otx2_nic *pfvf, bool enable);
void otx2_cleanup_rx_cqes(struct otx2_nic *pfvf, struct otx2_cq_queue *cq);
void otx2_cleanup_tx_cqes(struct otx2_nic *pfvf, struct otx2_cq_queue *cq);
int otx2_sq_aq_init(void *dev, u16 qidx, u16 sqb_aura);
int cn10k_sq_aq_init(void *dev, u16 qidx, u16 sqb_aura);
int otx2_alloc_buffer(struct otx2_nic *pfvf, struct otx2_cq_queue *cq,
		      dma_addr_t *dma);

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
bool otx2_xdp_sq_append_pkt(struct otx2_nic *pfvf, u64 iova, int len, u16 qidx);
u16 otx2_get_max_mtu(struct otx2_nic *pfvf);
int otx2_handle_ntuple_tc_features(struct net_device *netdev,
				   netdev_features_t features);
/* tc support */
int otx2_init_tc(struct otx2_nic *nic);
void otx2_shutdown_tc(struct otx2_nic *nic);
int otx2_setup_tc(struct net_device *netdev, enum tc_setup_type type,
		  void *type_data);
int otx2_tc_alloc_ent_bitmap(struct otx2_nic *nic);
/* CGX/RPM DMAC filters support */
int otx2_dmacflt_get_max_cnt(struct otx2_nic *pf);
int otx2_dmacflt_add(struct otx2_nic *pf, const u8 *mac, u8 bit_pos);
int otx2_dmacflt_remove(struct otx2_nic *pf, const u8 *mac, u8 bit_pos);
int otx2_dmacflt_update(struct otx2_nic *pf, u8 *mac, u8 bit_pos);
void otx2_dmacflt_reinstall_flows(struct otx2_nic *pf);
void otx2_dmacflt_update_pfmac_flow(struct otx2_nic *pfvf);

#ifdef CONFIG_DCB
/* DCB support*/
void otx2_update_bpid_in_rqctx(struct otx2_nic *pfvf, int vlan_prio, int qidx, bool pfc_enable);
int otx2_config_priority_flow_ctrl(struct otx2_nic *pfvf);
int otx2_dcbnl_set_ops(struct net_device *dev);
#endif
#endif /* OTX2_COMMON_H */
