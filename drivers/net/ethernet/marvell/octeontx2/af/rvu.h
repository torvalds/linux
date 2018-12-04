/* SPDX-License-Identifier: GPL-2.0
 * Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef RVU_H
#define RVU_H

#include <linux/pci.h>
#include "rvu_struct.h"
#include "common.h"
#include "mbox.h"

/* PCI device IDs */
#define	PCI_DEVID_OCTEONTX2_RVU_AF		0xA065

/* Subsystem Device ID */
#define PCI_SUBSYS_DEVID_96XX                  0xB200

/* PCI BAR nos */
#define	PCI_AF_REG_BAR_NUM			0
#define	PCI_PF_REG_BAR_NUM			2
#define	PCI_MBOX_BAR_NUM			4

#define NAME_SIZE				32

/* PF_FUNC */
#define RVU_PFVF_PF_SHIFT	10
#define RVU_PFVF_PF_MASK	0x3F
#define RVU_PFVF_FUNC_SHIFT	0
#define RVU_PFVF_FUNC_MASK	0x3FF

struct rvu_work {
	struct	work_struct work;
	struct	rvu *rvu;
};

struct rsrc_bmap {
	unsigned long *bmap;	/* Pointer to resource bitmap */
	u16  max;		/* Max resource id or count */
};

struct rvu_block {
	struct rsrc_bmap	lf;
	struct admin_queue	*aq; /* NIX/NPA AQ */
	u16  *fn_map; /* LF to pcifunc mapping */
	bool multislot;
	bool implemented;
	u8   addr;  /* RVU_BLOCK_ADDR_E */
	u8   type;  /* RVU_BLOCK_TYPE_E */
	u8   lfshift;
	u64  lookup_reg;
	u64  pf_lfcnt_reg;
	u64  vf_lfcnt_reg;
	u64  lfcfg_reg;
	u64  msixcfg_reg;
	u64  lfreset_reg;
	unsigned char name[NAME_SIZE];
};

struct nix_mcast {
	struct qmem	*mce_ctx;
	struct qmem	*mcast_buf;
	int		replay_pkind;
	int		next_free_mce;
	struct mutex	mce_lock; /* Serialize MCE updates */
};

struct nix_mce_list {
	struct hlist_head	head;
	int			count;
	int			max;
};

struct npc_mcam {
	struct rsrc_bmap counters;
	struct mutex	lock;	/* MCAM entries and counters update lock */
	unsigned long	*bmap;		/* bitmap, 0 => bmap_entries */
	unsigned long	*bmap_reverse;	/* Reverse bitmap, bmap_entries => 0 */
	u16	bmap_entries;	/* Number of unreserved MCAM entries */
	u16	bmap_fcnt;	/* MCAM entries free count */
	u16	*entry2pfvf_map;
	u16	*entry2cntr_map;
	u16	*cntr2pfvf_map;
	u16	*cntr_refcnt;
	u8	keysize;	/* MCAM keysize 112/224/448 bits */
	u8	banks;		/* Number of MCAM banks */
	u8	banks_per_entry;/* Number of keywords in key */
	u16	banksize;	/* Number of MCAM entries in each bank */
	u16	total_entries;	/* Total number of MCAM entries */
	u16	nixlf_offset;	/* Offset of nixlf rsvd uncast entries */
	u16	pf_offset;	/* Offset of PF's rsvd bcast, promisc entries */
	u16	lprio_count;
	u16	lprio_start;
	u16	hprio_count;
	u16	hprio_end;
};

/* Structure for per RVU func info ie PF/VF */
struct rvu_pfvf {
	bool		npalf; /* Only one NPALF per RVU_FUNC */
	bool		nixlf; /* Only one NIXLF per RVU_FUNC */
	u16		sso;
	u16		ssow;
	u16		cptlfs;
	u16		timlfs;
	u8		cgx_lmac;

	/* Block LF's MSIX vector info */
	struct rsrc_bmap msix;      /* Bitmap for MSIX vector alloc */
#define MSIX_BLKLF(blkaddr, lf) (((blkaddr) << 8) | ((lf) & 0xFF))
	u16		 *msix_lfmap; /* Vector to block LF mapping */

	/* NPA contexts */
	struct qmem	*aura_ctx;
	struct qmem	*pool_ctx;
	struct qmem	*npa_qints_ctx;
	unsigned long	*aura_bmap;
	unsigned long	*pool_bmap;

	/* NIX contexts */
	struct qmem	*rq_ctx;
	struct qmem	*sq_ctx;
	struct qmem	*cq_ctx;
	struct qmem	*rss_ctx;
	struct qmem	*cq_ints_ctx;
	struct qmem	*nix_qints_ctx;
	unsigned long	*sq_bmap;
	unsigned long	*rq_bmap;
	unsigned long	*cq_bmap;

	u16		rx_chan_base;
	u16		tx_chan_base;
	u8              rx_chan_cnt; /* total number of RX channels */
	u8              tx_chan_cnt; /* total number of TX channels */
	u16		maxlen;
	u16		minlen;

	u8		mac_addr[ETH_ALEN]; /* MAC address of this PF/VF */

	/* Broadcast pkt replication info */
	u16			bcast_mce_idx;
	struct nix_mce_list	bcast_mce_list;

	/* VLAN offload */
	struct mcam_entry entry;
	int rxvlan_index;
	bool rxvlan;
};

struct nix_txsch {
	struct rsrc_bmap schq;
	u8   lvl;
#define NIX_TXSCHQ_TL1_CFG_DONE       BIT_ULL(0)
#define TXSCH_MAP_FUNC(__pfvf_map)    ((__pfvf_map) & 0xFFFF)
#define TXSCH_MAP_FLAGS(__pfvf_map)   ((__pfvf_map) >> 16)
#define TXSCH_MAP(__func, __flags)    (((__func) & 0xFFFF) | ((__flags) << 16))
	u32  *pfvf_map;
};

struct nix_mark_format {
	u8 total;
	u8 in_use;
	u32 *cfg;
};

struct npc_pkind {
	struct rsrc_bmap rsrc;
	u32	*pfchan_map;
};

struct nix_flowkey {
#define NIX_FLOW_KEY_ALG_MAX 32
	u32 flowkey[NIX_FLOW_KEY_ALG_MAX];
	int in_use;
};

struct nix_lso {
	u8 total;
	u8 in_use;
};

struct nix_hw {
	struct nix_txsch txsch[NIX_TXSCH_LVL_CNT]; /* Tx schedulers */
	struct nix_mcast mcast;
	struct nix_flowkey flowkey;
	struct nix_mark_format mark_format;
	struct nix_lso lso;
};

struct rvu_hwinfo {
	u8	total_pfs;   /* MAX RVU PFs HW supports */
	u16	total_vfs;   /* Max RVU VFs HW supports */
	u16	max_vfs_per_pf; /* Max VFs that can be attached to a PF */
	u8	cgx;
	u8	lmac_per_cgx;
	u8	cgx_links;
	u8	lbk_links;
	u8	sdp_links;
	u8	npc_kpus;          /* No of parser units */


	struct rvu_block block[BLK_COUNT]; /* Block info */
	struct nix_hw    *nix0;
	struct npc_pkind pkind;
	struct npc_mcam  mcam;
};

struct mbox_wq_info {
	struct otx2_mbox mbox;
	struct rvu_work *mbox_wrk;

	struct otx2_mbox mbox_up;
	struct rvu_work *mbox_wrk_up;

	struct workqueue_struct *mbox_wq;
};

struct rvu {
	void __iomem		*afreg_base;
	void __iomem		*pfreg_base;
	struct pci_dev		*pdev;
	struct device		*dev;
	struct rvu_hwinfo       *hw;
	struct rvu_pfvf		*pf;
	struct rvu_pfvf		*hwvf;
	struct mutex		rsrc_lock; /* Serialize resource alloc/free */
	int			vfs; /* Number of VFs attached to RVU */

	/* Mbox */
	struct mbox_wq_info	afpf_wq_info;
	struct mbox_wq_info	afvf_wq_info;

	/* PF FLR */
	struct rvu_work		*flr_wrk;
	struct workqueue_struct *flr_wq;
	struct mutex		flr_lock; /* Serialize FLRs */

	/* MSI-X */
	u16			num_vec;
	char			*irq_name;
	bool			*irq_allocated;
	dma_addr_t		msix_base_iova;

	/* CGX */
#define PF_CGXMAP_BASE		1 /* PF 0 is reserved for RVU PF */
	u8			cgx_mapped_pfs;
	u8			cgx_cnt_max;	 /* CGX port count max */
	u8			*pf2cgxlmac_map; /* pf to cgx_lmac map */
	u16			*cgxlmac2pf_map; /* bitmap of mapped pfs for
						  * every cgx lmac port
						  */
	unsigned long		pf_notify_bmap; /* Flags for PF notification */
	void			**cgx_idmap; /* cgx id to cgx data map table */
	struct			work_struct cgx_evh_work;
	struct			workqueue_struct *cgx_evh_wq;
	spinlock_t		cgx_evq_lock; /* cgx event queue lock */
	struct list_head	cgx_evq_head; /* cgx event queue head */

	char mkex_pfl_name[MKEX_NAME_LEN]; /* Configured MKEX profile name */
};

static inline void rvu_write64(struct rvu *rvu, u64 block, u64 offset, u64 val)
{
	writeq(val, rvu->afreg_base + ((block << 28) | offset));
}

static inline u64 rvu_read64(struct rvu *rvu, u64 block, u64 offset)
{
	return readq(rvu->afreg_base + ((block << 28) | offset));
}

static inline void rvupf_write64(struct rvu *rvu, u64 offset, u64 val)
{
	writeq(val, rvu->pfreg_base + offset);
}

static inline u64 rvupf_read64(struct rvu *rvu, u64 offset)
{
	return readq(rvu->pfreg_base + offset);
}

static inline bool is_rvu_9xxx_A0(struct rvu *rvu)
{
	struct pci_dev *pdev = rvu->pdev;

	return (pdev->revision == 0x00) &&
		(pdev->subsystem_device == PCI_SUBSYS_DEVID_96XX);
}

/* Function Prototypes
 * RVU
 */
static inline int is_afvf(u16 pcifunc)
{
	return !(pcifunc & ~RVU_PFVF_FUNC_MASK);
}

int rvu_alloc_bitmap(struct rsrc_bmap *rsrc);
int rvu_alloc_rsrc(struct rsrc_bmap *rsrc);
void rvu_free_rsrc(struct rsrc_bmap *rsrc, int id);
int rvu_rsrc_free_count(struct rsrc_bmap *rsrc);
int rvu_alloc_rsrc_contig(struct rsrc_bmap *rsrc, int nrsrc);
bool rvu_rsrc_check_contig(struct rsrc_bmap *rsrc, int nrsrc);
int rvu_get_pf(u16 pcifunc);
struct rvu_pfvf *rvu_get_pfvf(struct rvu *rvu, int pcifunc);
void rvu_get_pf_numvfs(struct rvu *rvu, int pf, int *numvfs, int *hwvf);
bool is_block_implemented(struct rvu_hwinfo *hw, int blkaddr);
bool is_pffunc_map_valid(struct rvu *rvu, u16 pcifunc, int blktype);
int rvu_get_lf(struct rvu *rvu, struct rvu_block *block, u16 pcifunc, u16 slot);
int rvu_lf_reset(struct rvu *rvu, struct rvu_block *block, int lf);
int rvu_get_blkaddr(struct rvu *rvu, int blktype, u16 pcifunc);
int rvu_poll_reg(struct rvu *rvu, u64 block, u64 offset, u64 mask, bool zero);

/* RVU HW reg validation */
enum regmap_block {
	TXSCHQ_HWREGMAP = 0,
	MAX_HWREGMAP,
};

bool rvu_check_valid_reg(int regmap, int regblk, u64 reg);

/* NPA/NIX AQ APIs */
int rvu_aq_alloc(struct rvu *rvu, struct admin_queue **ad_queue,
		 int qsize, int inst_size, int res_size);
void rvu_aq_free(struct rvu *rvu, struct admin_queue *aq);

/* CGX APIs */
static inline bool is_pf_cgxmapped(struct rvu *rvu, u8 pf)
{
	return (pf >= PF_CGXMAP_BASE && pf <= rvu->cgx_mapped_pfs);
}

static inline void rvu_get_cgx_lmac_id(u8 map, u8 *cgx_id, u8 *lmac_id)
{
	*cgx_id = (map >> 4) & 0xF;
	*lmac_id = (map & 0xF);
}

int rvu_cgx_init(struct rvu *rvu);
int rvu_cgx_exit(struct rvu *rvu);
void *rvu_cgx_pdata(u8 cgx_id, struct rvu *rvu);
int rvu_cgx_config_rxtx(struct rvu *rvu, u16 pcifunc, bool start);
int rvu_mbox_handler_cgx_start_rxtx(struct rvu *rvu, struct msg_req *req,
				    struct msg_rsp *rsp);
int rvu_mbox_handler_cgx_stop_rxtx(struct rvu *rvu, struct msg_req *req,
				   struct msg_rsp *rsp);
int rvu_mbox_handler_cgx_stats(struct rvu *rvu, struct msg_req *req,
			       struct cgx_stats_rsp *rsp);
int rvu_mbox_handler_cgx_mac_addr_set(struct rvu *rvu,
				      struct cgx_mac_addr_set_or_get *req,
				      struct cgx_mac_addr_set_or_get *rsp);
int rvu_mbox_handler_cgx_mac_addr_get(struct rvu *rvu,
				      struct cgx_mac_addr_set_or_get *req,
				      struct cgx_mac_addr_set_or_get *rsp);
int rvu_mbox_handler_cgx_promisc_enable(struct rvu *rvu, struct msg_req *req,
					struct msg_rsp *rsp);
int rvu_mbox_handler_cgx_promisc_disable(struct rvu *rvu, struct msg_req *req,
					 struct msg_rsp *rsp);
int rvu_mbox_handler_cgx_start_linkevents(struct rvu *rvu, struct msg_req *req,
					  struct msg_rsp *rsp);
int rvu_mbox_handler_cgx_stop_linkevents(struct rvu *rvu, struct msg_req *req,
					 struct msg_rsp *rsp);
int rvu_mbox_handler_cgx_get_linkinfo(struct rvu *rvu, struct msg_req *req,
				      struct cgx_link_info_msg *rsp);
int rvu_mbox_handler_cgx_intlbk_enable(struct rvu *rvu, struct msg_req *req,
				       struct msg_rsp *rsp);
int rvu_mbox_handler_cgx_intlbk_disable(struct rvu *rvu, struct msg_req *req,
					struct msg_rsp *rsp);

/* NPA APIs */
int rvu_npa_init(struct rvu *rvu);
void rvu_npa_freemem(struct rvu *rvu);
void rvu_npa_lf_teardown(struct rvu *rvu, u16 pcifunc, int npalf);
int rvu_mbox_handler_npa_aq_enq(struct rvu *rvu,
				struct npa_aq_enq_req *req,
				struct npa_aq_enq_rsp *rsp);
int rvu_mbox_handler_npa_hwctx_disable(struct rvu *rvu,
				       struct hwctx_disable_req *req,
				       struct msg_rsp *rsp);
int rvu_mbox_handler_npa_lf_alloc(struct rvu *rvu,
				  struct npa_lf_alloc_req *req,
				  struct npa_lf_alloc_rsp *rsp);
int rvu_mbox_handler_npa_lf_free(struct rvu *rvu, struct msg_req *req,
				 struct msg_rsp *rsp);

/* NIX APIs */
bool is_nixlf_attached(struct rvu *rvu, u16 pcifunc);
int rvu_nix_init(struct rvu *rvu);
int rvu_nix_reserve_mark_format(struct rvu *rvu, struct nix_hw *nix_hw,
				int blkaddr, u32 cfg);
void rvu_nix_freemem(struct rvu *rvu);
int rvu_get_nixlf_count(struct rvu *rvu);
void rvu_nix_lf_teardown(struct rvu *rvu, u16 pcifunc, int blkaddr, int npalf);
int rvu_mbox_handler_nix_lf_alloc(struct rvu *rvu,
				  struct nix_lf_alloc_req *req,
				  struct nix_lf_alloc_rsp *rsp);
int rvu_mbox_handler_nix_lf_free(struct rvu *rvu, struct msg_req *req,
				 struct msg_rsp *rsp);
int rvu_mbox_handler_nix_aq_enq(struct rvu *rvu,
				struct nix_aq_enq_req *req,
				struct nix_aq_enq_rsp *rsp);
int rvu_mbox_handler_nix_hwctx_disable(struct rvu *rvu,
				       struct hwctx_disable_req *req,
				       struct msg_rsp *rsp);
int rvu_mbox_handler_nix_txsch_alloc(struct rvu *rvu,
				     struct nix_txsch_alloc_req *req,
				     struct nix_txsch_alloc_rsp *rsp);
int rvu_mbox_handler_nix_txsch_free(struct rvu *rvu,
				    struct nix_txsch_free_req *req,
				    struct msg_rsp *rsp);
int rvu_mbox_handler_nix_txschq_cfg(struct rvu *rvu,
				    struct nix_txschq_config *req,
				    struct msg_rsp *rsp);
int rvu_mbox_handler_nix_stats_rst(struct rvu *rvu, struct msg_req *req,
				   struct msg_rsp *rsp);
int rvu_mbox_handler_nix_vtag_cfg(struct rvu *rvu,
				  struct nix_vtag_config *req,
				  struct msg_rsp *rsp);
int rvu_mbox_handler_nix_rxvlan_alloc(struct rvu *rvu, struct msg_req *req,
				      struct msg_rsp *rsp);
int rvu_mbox_handler_nix_rss_flowkey_cfg(struct rvu *rvu,
					 struct nix_rss_flowkey_cfg *req,
					 struct nix_rss_flowkey_cfg_rsp *rsp);
int rvu_mbox_handler_nix_set_mac_addr(struct rvu *rvu,
				      struct nix_set_mac_addr *req,
				      struct msg_rsp *rsp);
int rvu_mbox_handler_nix_set_rx_mode(struct rvu *rvu, struct nix_rx_mode *req,
				     struct msg_rsp *rsp);
int rvu_mbox_handler_nix_set_hw_frs(struct rvu *rvu, struct nix_frs_cfg *req,
				    struct msg_rsp *rsp);
int rvu_mbox_handler_nix_lf_start_rx(struct rvu *rvu, struct msg_req *req,
				     struct msg_rsp *rsp);
int rvu_mbox_handler_nix_lf_stop_rx(struct rvu *rvu, struct msg_req *req,
				    struct msg_rsp *rsp);
int rvu_mbox_handler_nix_mark_format_cfg(struct rvu *rvu,
					 struct nix_mark_format_cfg  *req,
					 struct nix_mark_format_cfg_rsp *rsp);
int rvu_mbox_handler_nix_set_rx_cfg(struct rvu *rvu, struct nix_rx_cfg *req,
				    struct msg_rsp *rsp);
int rvu_mbox_handler_nix_lso_format_cfg(struct rvu *rvu,
					struct nix_lso_format_cfg *req,
					struct nix_lso_format_cfg_rsp *rsp);

/* NPC APIs */
int rvu_npc_init(struct rvu *rvu);
void rvu_npc_freemem(struct rvu *rvu);
int rvu_npc_get_pkind(struct rvu *rvu, u16 pf);
void rvu_npc_set_pkind(struct rvu *rvu, int pkind, struct rvu_pfvf *pfvf);
void rvu_npc_install_ucast_entry(struct rvu *rvu, u16 pcifunc,
				 int nixlf, u64 chan, u8 *mac_addr);
void rvu_npc_install_promisc_entry(struct rvu *rvu, u16 pcifunc,
				   int nixlf, u64 chan, bool allmulti);
void rvu_npc_disable_promisc_entry(struct rvu *rvu, u16 pcifunc, int nixlf);
void rvu_npc_enable_promisc_entry(struct rvu *rvu, u16 pcifunc, int nixlf);
void rvu_npc_install_bcast_match_entry(struct rvu *rvu, u16 pcifunc,
				       int nixlf, u64 chan);
int rvu_npc_update_rxvlan(struct rvu *rvu, u16 pcifunc, int nixlf);
void rvu_npc_disable_mcam_entries(struct rvu *rvu, u16 pcifunc, int nixlf);
void rvu_npc_disable_default_entries(struct rvu *rvu, u16 pcifunc, int nixlf);
void rvu_npc_enable_default_entries(struct rvu *rvu, u16 pcifunc, int nixlf);
void rvu_npc_update_flowkey_alg_idx(struct rvu *rvu, u16 pcifunc, int nixlf,
				    int group, int alg_idx, int mcam_index);
int rvu_mbox_handler_npc_mcam_alloc_entry(struct rvu *rvu,
					  struct npc_mcam_alloc_entry_req *req,
					  struct npc_mcam_alloc_entry_rsp *rsp);
int rvu_mbox_handler_npc_mcam_free_entry(struct rvu *rvu,
					 struct npc_mcam_free_entry_req *req,
					 struct msg_rsp *rsp);
int rvu_mbox_handler_npc_mcam_write_entry(struct rvu *rvu,
					  struct npc_mcam_write_entry_req *req,
					  struct msg_rsp *rsp);
int rvu_mbox_handler_npc_mcam_ena_entry(struct rvu *rvu,
					struct npc_mcam_ena_dis_entry_req *req,
					struct msg_rsp *rsp);
int rvu_mbox_handler_npc_mcam_dis_entry(struct rvu *rvu,
					struct npc_mcam_ena_dis_entry_req *req,
					struct msg_rsp *rsp);
int rvu_mbox_handler_npc_mcam_shift_entry(struct rvu *rvu,
					  struct npc_mcam_shift_entry_req *req,
					  struct npc_mcam_shift_entry_rsp *rsp);
int rvu_mbox_handler_npc_mcam_alloc_counter(struct rvu *rvu,
				struct npc_mcam_alloc_counter_req *req,
				struct npc_mcam_alloc_counter_rsp *rsp);
int rvu_mbox_handler_npc_mcam_free_counter(struct rvu *rvu,
		   struct npc_mcam_oper_counter_req *req, struct msg_rsp *rsp);
int rvu_mbox_handler_npc_mcam_clear_counter(struct rvu *rvu,
		struct npc_mcam_oper_counter_req *req, struct msg_rsp *rsp);
int rvu_mbox_handler_npc_mcam_unmap_counter(struct rvu *rvu,
		struct npc_mcam_unmap_counter_req *req, struct msg_rsp *rsp);
int rvu_mbox_handler_npc_mcam_counter_stats(struct rvu *rvu,
			struct npc_mcam_oper_counter_req *req,
			struct npc_mcam_oper_counter_rsp *rsp);
int rvu_mbox_handler_npc_mcam_alloc_and_write_entry(struct rvu *rvu,
			  struct npc_mcam_alloc_and_write_entry_req *req,
			  struct npc_mcam_alloc_and_write_entry_rsp *rsp);
int rvu_mbox_handler_npc_get_kex_cfg(struct rvu *rvu, struct msg_req *req,
				     struct npc_get_kex_cfg_rsp *rsp);
#endif /* RVU_H */
