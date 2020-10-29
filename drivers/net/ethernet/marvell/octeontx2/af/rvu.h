/* SPDX-License-Identifier: GPL-2.0 */
/*  Marvell OcteonTx2 RVU Admin Function driver
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
#define MAX_NIX_BLKS				2

/* PF_FUNC */
#define RVU_PFVF_PF_SHIFT	10
#define RVU_PFVF_PF_MASK	0x3F
#define RVU_PFVF_FUNC_SHIFT	0
#define RVU_PFVF_FUNC_MASK	0x3FF

#ifdef CONFIG_DEBUG_FS
struct dump_ctx {
	int	lf;
	int	id;
	bool	all;
};

struct rvu_debugfs {
	struct dentry *root;
	struct dentry *cgx_root;
	struct dentry *cgx;
	struct dentry *lmac;
	struct dentry *npa;
	struct dentry *nix;
	struct dentry *npc;
	struct dump_ctx npa_aura_ctx;
	struct dump_ctx npa_pool_ctx;
	struct dump_ctx nix_cq_ctx;
	struct dump_ctx nix_rq_ctx;
	struct dump_ctx nix_sq_ctx;
	int npa_qsize_id;
	int nix_qsize_id;
};
#endif

struct rvu_work {
	struct	work_struct work;
	struct	rvu *rvu;
	int num_msgs;
	int up_num_msgs;
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
	u16     rx_miss_act_cntr; /* Counter for RX MISS action */
};

/* Structure for per RVU func info ie PF/VF */
struct rvu_pfvf {
	bool		npalf; /* Only one NPALF per RVU_FUNC */
	bool		nixlf; /* Only one NIXLF per RVU_FUNC */
	u16		sso;
	u16		ssow;
	u16		cptlfs;
	u16		timlfs;
	u16		cpt1_lfs;
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

	bool	cgx_in_use; /* this PF/VF using CGX? */
	int	cgx_users;  /* number of cgx users - used only by PFs */

	u8	nix_blkaddr; /* BLKADDR_NIX0/1 assigned to this PF */
	u8	nix_rx_intf; /* NIX0_RX/NIX1_RX interface to NPC */
	u8	nix_tx_intf; /* NIX0_TX/NIX1_TX interface to NPC */
};

struct nix_txsch {
	struct rsrc_bmap schq;
	u8   lvl;
#define NIX_TXSCHQ_FREE		      BIT_ULL(1)
#define NIX_TXSCHQ_CFG_DONE	      BIT_ULL(0)
#define TXSCH_MAP_FUNC(__pfvf_map)    ((__pfvf_map) & 0xFFFF)
#define TXSCH_MAP_FLAGS(__pfvf_map)   ((__pfvf_map) >> 16)
#define TXSCH_MAP(__func, __flags)    (((__func) & 0xFFFF) | ((__flags) << 16))
#define TXSCH_SET_FLAG(__pfvf_map, flag)    ((__pfvf_map) | ((flag) << 16))
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
	int blkaddr;
	struct rvu *rvu;
	struct nix_txsch txsch[NIX_TXSCH_LVL_CNT]; /* Tx schedulers */
	struct nix_mcast mcast;
	struct nix_flowkey flowkey;
	struct nix_mark_format mark_format;
	struct nix_lso lso;
};

/* RVU block's capabilities or functionality,
 * which vary by silicon version/skew.
 */
struct hw_cap {
	/* Transmit side supported functionality */
	u8	nix_tx_aggr_lvl; /* Tx link's traffic aggregation level */
	u16	nix_txsch_per_cgx_lmac; /* Max Q's transmitting to CGX LMAC */
	u16	nix_txsch_per_lbk_lmac; /* Max Q's transmitting to LBK LMAC */
	u16	nix_txsch_per_sdp_lmac; /* Max Q's transmitting to SDP LMAC */
	bool	nix_fixed_txschq_mapping; /* Schq mapping fixed or flexible */
	bool	nix_shaping;		 /* Is shaping and coloring supported */
	bool	nix_tx_link_bp;		 /* Can link backpressure TL queues ? */
	bool	nix_rx_multicast;	 /* Rx packet replication support */
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
	u8	npc_pkinds;        /* No of port kinds */
	u8	npc_intfs;         /* No of interfaces */
	u8	npc_kpu_entries;   /* No of KPU entries */
	u16	npc_counters;	   /* No of match stats counters */
	bool	npc_ext_set;	   /* Extended register set */

	struct hw_cap    cap;
	struct rvu_block block[BLK_COUNT]; /* Block info */
	struct nix_hw    *nix;
	struct rvu	 *rvu;
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

struct rvu_fwdata {
#define RVU_FWDATA_HEADER_MAGIC	0xCFDA	/* Custom Firmware Data*/
#define RVU_FWDATA_VERSION	0x0001
	u32 header_magic;
	u32 version;		/* version id */

	/* MAC address */
#define PF_MACNUM_MAX	32
#define VF_MACNUM_MAX	256
	u64 pf_macs[PF_MACNUM_MAX];
	u64 vf_macs[VF_MACNUM_MAX];
	u64 sclk;
	u64 rclk;
	u64 mcam_addr;
	u64 mcam_sz;
	u64 msixtr_base;
#define FWDATA_RESERVED_MEM 1023
	u64 reserved[FWDATA_RESERVED_MEM];
};

struct ptp;

/* KPU profile adapter structure gathering all KPU configuration data and abstracting out the
 * source where it came from.
 */
struct npc_kpu_profile_adapter {
	const char			*name;
	u64				version;
	const struct npc_lt_def_cfg	*lt_def;
	const struct npc_kpu_profile_action	*ikpu; /* array[pkinds] */
	const struct npc_kpu_profile	*kpu; /* array[kpus] */
	struct npc_mcam_kex		*mkex;
	size_t				pkinds;
	size_t				kpus;
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
	int			nix_blkaddr[MAX_NIX_BLKS];

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
	u64			msixtr_base_phy; /* Register reset value */

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
	struct mutex		cgx_cfg_lock; /* serialize cgx configuration */

	char mkex_pfl_name[MKEX_NAME_LEN]; /* Configured MKEX profile name */

	/* Firmware data */
	struct rvu_fwdata	*fwdata;

	/* NPC KPU data */
	struct npc_kpu_profile_adapter kpu;

	struct ptp		*ptp;

#ifdef CONFIG_DEBUG_FS
	struct rvu_debugfs	rvu_dbg;
#endif
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

/* Silicon revisions */
static inline bool is_rvu_96xx_A0(struct rvu *rvu)
{
	struct pci_dev *pdev = rvu->pdev;

	return (pdev->revision == 0x00) &&
		(pdev->subsystem_device == PCI_SUBSYS_DEVID_96XX);
}

static inline bool is_rvu_96xx_B0(struct rvu *rvu)
{
	struct pci_dev *pdev = rvu->pdev;

	return ((pdev->revision == 0x00) || (pdev->revision == 0x01)) &&
		(pdev->subsystem_device == PCI_SUBSYS_DEVID_96XX);
}

/* Function Prototypes
 * RVU
 */
static inline int is_afvf(u16 pcifunc)
{
	return !(pcifunc & ~RVU_PFVF_FUNC_MASK);
}

static inline bool is_rvu_fwdata_valid(struct rvu *rvu)
{
	return (rvu->fwdata->header_magic == RVU_FWDATA_HEADER_MAGIC) &&
		(rvu->fwdata->version == RVU_FWDATA_VERSION);
}

int rvu_alloc_bitmap(struct rsrc_bmap *rsrc);
int rvu_alloc_rsrc(struct rsrc_bmap *rsrc);
void rvu_free_rsrc(struct rsrc_bmap *rsrc, int id);
int rvu_rsrc_free_count(struct rsrc_bmap *rsrc);
int rvu_alloc_rsrc_contig(struct rsrc_bmap *rsrc, int nrsrc);
bool rvu_rsrc_check_contig(struct rsrc_bmap *rsrc, int nrsrc);
u16 rvu_get_rsrc_mapcount(struct rvu_pfvf *pfvf, int blkaddr);
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

#define M(_name, _id, fn_name, req, rsp)				\
int rvu_mbox_handler_ ## fn_name(struct rvu *, struct req *, struct rsp *);
MBOX_MESSAGES
#undef M

int rvu_cgx_init(struct rvu *rvu);
int rvu_cgx_exit(struct rvu *rvu);
void *rvu_cgx_pdata(u8 cgx_id, struct rvu *rvu);
int rvu_cgx_config_rxtx(struct rvu *rvu, u16 pcifunc, bool start);
void rvu_cgx_enadis_rx_bp(struct rvu *rvu, int pf, bool enable);
int rvu_cgx_start_stop_io(struct rvu *rvu, u16 pcifunc, bool start);
int rvu_cgx_nix_cuml_stats(struct rvu *rvu, void *cgxd, int lmac_id, int index,
			   int rxtxflag, u64 *stat);
/* NPA APIs */
int rvu_npa_init(struct rvu *rvu);
void rvu_npa_freemem(struct rvu *rvu);
void rvu_npa_lf_teardown(struct rvu *rvu, u16 pcifunc, int npalf);
int rvu_npa_aq_enq_inst(struct rvu *rvu, struct npa_aq_enq_req *req,
			struct npa_aq_enq_rsp *rsp);

/* NIX APIs */
bool is_nixlf_attached(struct rvu *rvu, u16 pcifunc);
int rvu_nix_init(struct rvu *rvu);
int rvu_nix_reserve_mark_format(struct rvu *rvu, struct nix_hw *nix_hw,
				int blkaddr, u32 cfg);
void rvu_nix_freemem(struct rvu *rvu);
int rvu_get_nixlf_count(struct rvu *rvu);
void rvu_nix_lf_teardown(struct rvu *rvu, u16 pcifunc, int blkaddr, int npalf);
int nix_get_nixlf(struct rvu *rvu, u16 pcifunc, int *nixlf, int *nix_blkaddr);
int nix_update_bcast_mce_list(struct rvu *rvu, u16 pcifunc, bool add);
struct nix_hw *get_nix_hw(struct rvu_hwinfo *hw, int blkaddr);
int rvu_get_next_nix_blkaddr(struct rvu *rvu, int blkaddr);

/* NPC APIs */
int rvu_npc_init(struct rvu *rvu);
void rvu_npc_freemem(struct rvu *rvu);
int rvu_npc_get_pkind(struct rvu *rvu, u16 pf);
void rvu_npc_set_pkind(struct rvu *rvu, int pkind, struct rvu_pfvf *pfvf);
int npc_config_ts_kpuaction(struct rvu *rvu, int pf, u16 pcifunc, bool en);
void rvu_npc_install_ucast_entry(struct rvu *rvu, u16 pcifunc,
				 int nixlf, u64 chan, u8 *mac_addr);
void rvu_npc_install_promisc_entry(struct rvu *rvu, u16 pcifunc,
				   int nixlf, u64 chan, bool allmulti);
void rvu_npc_disable_promisc_entry(struct rvu *rvu, u16 pcifunc, int nixlf);
void rvu_npc_enable_promisc_entry(struct rvu *rvu, u16 pcifunc, int nixlf);
void rvu_npc_install_bcast_match_entry(struct rvu *rvu, u16 pcifunc,
				       int nixlf, u64 chan);
void rvu_npc_enable_bcast_entry(struct rvu *rvu, u16 pcifunc, bool enable);
int rvu_npc_update_rxvlan(struct rvu *rvu, u16 pcifunc, int nixlf);
void rvu_npc_disable_mcam_entries(struct rvu *rvu, u16 pcifunc, int nixlf);
void rvu_npc_disable_default_entries(struct rvu *rvu, u16 pcifunc, int nixlf);
void rvu_npc_enable_default_entries(struct rvu *rvu, u16 pcifunc, int nixlf);
void rvu_npc_update_flowkey_alg_idx(struct rvu *rvu, u16 pcifunc, int nixlf,
				    int group, int alg_idx, int mcam_index);
void rvu_npc_get_mcam_entry_alloc_info(struct rvu *rvu, u16 pcifunc,
				       int blkaddr, int *alloc_cnt,
				       int *enable_cnt);
void rvu_npc_get_mcam_counter_alloc_info(struct rvu *rvu, u16 pcifunc,
					 int blkaddr, int *alloc_cnt,
					 int *enable_cnt);
bool is_npc_intf_tx(u8 intf);
bool is_npc_intf_rx(u8 intf);
bool is_npc_interface_valid(struct rvu *rvu, u8 intf);
int rvu_npc_get_tx_nibble_cfg(struct rvu *rvu, u64 nibble_ena);

#ifdef CONFIG_DEBUG_FS
void rvu_dbg_init(struct rvu *rvu);
void rvu_dbg_exit(struct rvu *rvu);
#else
static inline void rvu_dbg_init(struct rvu *rvu) {}
static inline void rvu_dbg_exit(struct rvu *rvu) {}
#endif
#endif /* RVU_H */
