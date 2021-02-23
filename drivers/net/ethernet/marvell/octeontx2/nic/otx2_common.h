/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell OcteonTx2 RVU Ethernet driver
 *
 * Copyright (C) 2020 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#include <mbox.h>
#include <npc.h>
#include "otx2_reg.h"
#include "otx2_txrx.h"
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

/* RSS configuration */
struct otx2_rss_info {
	u8 enable;
	u32 flowkey_cfg;
	u16 rss_size;
	u8  ind_tbl[MAX_RSS_INDIR_TBL_SIZE];
#define RSS_HASH_KEY_SIZE	44   /* 352 bit key */
	u8  key[RSS_HASH_KEY_SIZE];
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
	u16			max_queues;
	u16			pool_cnt;
	u16			rqpool_cnt;
	u16			sqpool_cnt;

	/* NPA */
	u32			stack_pg_ptrs;  /* No of ptrs per stack page */
	u32			stack_pg_bytes; /* Size of stack page */
	u16			sqb_size;

	/* NIX */
	u16		txschq_list[NIX_TXSCH_LVL_CNT][MAX_TXSCHQ_PER_FUNC];

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
	u8			hw_tso;

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
	u8			cgx_links;  /* No. of CGX links present in HW */
	u8			lbk_links;  /* No. of LBK links present in HW */
};

struct otx2_vf_config {
	struct otx2_nic *pf;
	struct delayed_work link_event_work;
	bool intf_down; /* interface was either configured or not */
	u8 mac[ETH_ALEN];
	u16 vlan;
	int tx_vtag_idx;
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
};

#define OTX2_HW_TIMESTAMP_LEN	8

struct otx2_mac_table {
	u8 addr[ETH_ALEN];
	u16 mcam_entry;
	bool inuse;
};

struct otx2_flow_config {
	u16			entry[NPC_MAX_NONCONTIG_ENTRIES];
	u32			nr_flows;
#define OTX2_MAX_NTUPLE_FLOWS	32
#define OTX2_MAX_UNICAST_FLOWS	8
#define OTX2_MAX_VLAN_FLOWS	1
#define OTX2_MCAM_COUNT		(OTX2_MAX_NTUPLE_FLOWS + \
				 OTX2_MAX_UNICAST_FLOWS + \
				 OTX2_MAX_VLAN_FLOWS)
	u32			ntuple_offset;
	u32			unicast_offset;
	u32			rx_vlan_offset;
	u32			vf_vlan_offset;
#define OTX2_PER_VF_VLAN_FLOWS	2 /* rx+tx per VF */
#define OTX2_VF_VLAN_RX_INDEX	0
#define OTX2_VF_VLAN_TX_INDEX	1
	u32                     ntuple_max_flows;
	struct list_head	flow_list;
};

struct otx2_nic {
	void __iomem		*reg_base;
	struct net_device	*netdev;
	void			*iommu_domain;
	u16			max_frs;
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
	u64			flags;

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

	u64			reset_count;
	struct work_struct	reset_task;
	struct workqueue_struct	*flr_wq;
	struct flr_work		*flr_wrk;
	struct refill_work	*refill_wrk;
	struct workqueue_struct	*otx2_wq;
	struct work_struct	rx_mode_work;
	struct otx2_mac_table	*mac_table;

	/* Ethtool stuff */
	u32			msg_enable;

	/* Block address of NIX either BLKADDR_NIX0 or BLKADDR_NIX1 */
	int			nix_blkaddr;

	struct otx2_ptp		*ptp;
	struct hwtstamp_config	tstamp;

	struct otx2_flow_config	*flow_cfg;
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

static inline void otx2_setup_dev_hw_settings(struct otx2_nic *pfvf)
{
	struct otx2_hw *hw = &pfvf->hw;

	pfvf->hw.cq_time_wait = CQ_TIMER_THRESH_DEFAULT;
	pfvf->hw.cq_ecount_wait = CQ_CQE_THRESH_DEFAULT;
	pfvf->hw.cq_qcount_wait = CQ_QCOUNT_DEFAULT;

	hw->hw_tso = true;

	if (is_96xx_A0(pfvf->pdev)) {
		hw->hw_tso = false;

		/* Time based irq coalescing is not supported */
		pfvf->hw.cq_qcount_wait = 0x0;

		/* Due to HW issue previous silicons required minimum
		 * 600 unused CQE to avoid CQ overflow.
		 */
		pfvf->hw.rq_skid = 600;
		pfvf->qset.rqe_cnt = Q_COUNT(Q_SIZE_1K);
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
#define otx2_write128(lo, hi, addr)
#define otx2_atomic64_add(incr, ptr)		({ *ptr += incr; })
#endif

/* Alloc pointer from pool/aura */
static inline u64 otx2_aura_allocptr(struct otx2_nic *pfvf, int aura)
{
	u64 *ptr = (u64 *)otx2_get_regaddr(pfvf,
			   NPA_LF_AURA_OP_ALLOCX(0));
	u64 incr = (u64)aura | BIT_ULL(63);

	return otx2_atomic64_add(incr, ptr);
}

/* Free pointer to a pool/aura */
static inline void otx2_aura_freeptr(struct otx2_nic *pfvf,
				     int aura, s64 buf)
{
	otx2_write128((u64)buf, (u64)aura | BIT_ULL(63),
		      otx2_get_regaddr(pfvf, NPA_LF_AURA_OP_FREE0));
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
dma_addr_t __otx2_alloc_rbuf(struct otx2_nic *pfvf, struct otx2_pool *pool);
int otx2_rxtx_enable(struct otx2_nic *pfvf, bool enable);
void otx2_ctx_disable(struct mbox *mbox, int type, bool npa);
int otx2_nix_config_bp(struct otx2_nic *pfvf, bool enable);
void otx2_cleanup_rx_cqes(struct otx2_nic *pfvf, struct otx2_cq_queue *cq);
void otx2_cleanup_tx_cqes(struct otx2_nic *pfvf, struct otx2_cq_queue *cq);

/* RSS configuration APIs*/
int otx2_rss_init(struct otx2_nic *pfvf);
int otx2_set_flowkey_cfg(struct otx2_nic *pfvf);
void otx2_set_rss_key(struct otx2_nic *pfvf);
int otx2_set_rss_table(struct otx2_nic *pfvf);

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
void mbox_handler_nix_bp_enable(struct otx2_nic *pfvf,
				struct nix_bp_cfg_rsp *rsp);

/* Device stats APIs */
void otx2_get_dev_stats(struct otx2_nic *pfvf);
void otx2_get_stats64(struct net_device *netdev,
		      struct rtnl_link_stats64 *stats);
void otx2_update_lmac_stats(struct otx2_nic *pfvf);
int otx2_update_rq_stats(struct otx2_nic *pfvf, int qidx);
int otx2_update_sq_stats(struct otx2_nic *pfvf, int qidx);
void otx2_set_ethtool_ops(struct net_device *netdev);
void otx2vf_set_ethtool_ops(struct net_device *netdev);

int otx2_open(struct net_device *netdev);
int otx2_stop(struct net_device *netdev);
int otx2_set_real_num_queues(struct net_device *netdev,
			     int tx_queues, int rx_queues);
/* MCAM filter related APIs */
int otx2_mcam_flow_init(struct otx2_nic *pf);
int otx2_alloc_mcam_entries(struct otx2_nic *pfvf);
void otx2_mcam_flow_del(struct otx2_nic *pf);
int otx2_destroy_ntuple_flows(struct otx2_nic *pf);
int otx2_destroy_mcam_flows(struct otx2_nic *pfvf);
int otx2_get_flow(struct otx2_nic *pfvf,
		  struct ethtool_rxnfc *nfc, u32 location);
int otx2_get_all_flows(struct otx2_nic *pfvf,
		       struct ethtool_rxnfc *nfc, u32 *rule_locs);
int otx2_add_flow(struct otx2_nic *pfvf,
		  struct ethtool_rx_flow_spec *fsp);
int otx2_remove_flow(struct otx2_nic *pfvf, u32 location);
int otx2_prepare_flow_request(struct ethtool_rx_flow_spec *fsp,
			      struct npc_install_flow_req *req);
int otx2_del_macfilter(struct net_device *netdev, const u8 *mac);
int otx2_add_macfilter(struct net_device *netdev, const u8 *mac);
int otx2_enable_rxvlan(struct otx2_nic *pf, bool enable);
int otx2_install_rxvlan_offload_flow(struct otx2_nic *pfvf);

#endif /* OTX2_COMMON_H */
