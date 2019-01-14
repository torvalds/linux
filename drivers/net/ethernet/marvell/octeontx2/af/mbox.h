/* SPDX-License-Identifier: GPL-2.0
 * Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef MBOX_H
#define MBOX_H

#include <linux/etherdevice.h>
#include <linux/sizes.h>

#include "rvu_struct.h"
#include "common.h"

#define MBOX_SIZE		SZ_64K

/* AF/PF: PF initiated, PF/VF VF initiated */
#define MBOX_DOWN_RX_START	0
#define MBOX_DOWN_RX_SIZE	(46 * SZ_1K)
#define MBOX_DOWN_TX_START	(MBOX_DOWN_RX_START + MBOX_DOWN_RX_SIZE)
#define MBOX_DOWN_TX_SIZE	(16 * SZ_1K)
/* AF/PF: AF initiated, PF/VF PF initiated */
#define MBOX_UP_RX_START	(MBOX_DOWN_TX_START + MBOX_DOWN_TX_SIZE)
#define MBOX_UP_RX_SIZE		SZ_1K
#define MBOX_UP_TX_START	(MBOX_UP_RX_START + MBOX_UP_RX_SIZE)
#define MBOX_UP_TX_SIZE		SZ_1K

#if MBOX_UP_TX_SIZE + MBOX_UP_TX_START != MBOX_SIZE
# error "incorrect mailbox area sizes"
#endif

#define INTR_MASK(pfvfs) ((pfvfs < 64) ? (BIT_ULL(pfvfs) - 1) : (~0ull))

#define MBOX_RSP_TIMEOUT	1000 /* in ms, Time to wait for mbox response */

#define MBOX_MSG_ALIGN		16  /* Align mbox msg start to 16bytes */

/* Mailbox directions */
#define MBOX_DIR_AFPF		0  /* AF replies to PF */
#define MBOX_DIR_PFAF		1  /* PF sends messages to AF */
#define MBOX_DIR_PFVF		2  /* PF replies to VF */
#define MBOX_DIR_VFPF		3  /* VF sends messages to PF */
#define MBOX_DIR_AFPF_UP	4  /* AF sends messages to PF */
#define MBOX_DIR_PFAF_UP	5  /* PF replies to AF */
#define MBOX_DIR_PFVF_UP	6  /* PF sends messages to VF */
#define MBOX_DIR_VFPF_UP	7  /* VF replies to PF */

struct otx2_mbox_dev {
	void	    *mbase;   /* This dev's mbox region */
	spinlock_t  mbox_lock;
	u16         msg_size; /* Total msg size to be sent */
	u16         rsp_size; /* Total rsp size to be sure the reply is ok */
	u16         num_msgs; /* No of msgs sent or waiting for response */
	u16         msgs_acked; /* No of msgs for which response is received */
};

struct otx2_mbox {
	struct pci_dev *pdev;
	void   *hwbase;  /* Mbox region advertised by HW */
	void   *reg_base;/* CSR base for this dev */
	u64    trigger;  /* Trigger mbox notification */
	u16    tr_shift; /* Mbox trigger shift */
	u64    rx_start; /* Offset of Rx region in mbox memory */
	u64    tx_start; /* Offset of Tx region in mbox memory */
	u16    rx_size;  /* Size of Rx region */
	u16    tx_size;  /* Size of Tx region */
	u16    ndevs;    /* The number of peers */
	struct otx2_mbox_dev *dev;
};

/* Header which preceeds all mbox messages */
struct mbox_hdr {
	u16  num_msgs;   /* No of msgs embedded */
};

/* Header which preceeds every msg and is also part of it */
struct mbox_msghdr {
	u16 pcifunc;     /* Who's sending this msg */
	u16 id;          /* Mbox message ID */
#define OTX2_MBOX_REQ_SIG (0xdead)
#define OTX2_MBOX_RSP_SIG (0xbeef)
	u16 sig;         /* Signature, for validating corrupted msgs */
#define OTX2_MBOX_VERSION (0x0001)
	u16 ver;         /* Version of msg's structure for this ID */
	u16 next_msgoff; /* Offset of next msg within mailbox region */
	int rc;          /* Msg process'ed response code */
};

void otx2_mbox_reset(struct otx2_mbox *mbox, int devid);
void otx2_mbox_destroy(struct otx2_mbox *mbox);
int otx2_mbox_init(struct otx2_mbox *mbox, void __force *hwbase,
		   struct pci_dev *pdev, void __force *reg_base,
		   int direction, int ndevs);
void otx2_mbox_msg_send(struct otx2_mbox *mbox, int devid);
int otx2_mbox_wait_for_rsp(struct otx2_mbox *mbox, int devid);
int otx2_mbox_busy_poll_for_rsp(struct otx2_mbox *mbox, int devid);
struct mbox_msghdr *otx2_mbox_alloc_msg_rsp(struct otx2_mbox *mbox, int devid,
					    int size, int size_rsp);
struct mbox_msghdr *otx2_mbox_get_rsp(struct otx2_mbox *mbox, int devid,
				      struct mbox_msghdr *msg);
int otx2_reply_invalid_msg(struct otx2_mbox *mbox, int devid,
			   u16 pcifunc, u16 id);
bool otx2_mbox_nonempty(struct otx2_mbox *mbox, int devid);
const char *otx2_mbox_id2name(u16 id);
static inline struct mbox_msghdr *otx2_mbox_alloc_msg(struct otx2_mbox *mbox,
						      int devid, int size)
{
	return otx2_mbox_alloc_msg_rsp(mbox, devid, size, 0);
}

/* Mailbox message types */
#define MBOX_MSG_MASK				0xFFFF
#define MBOX_MSG_INVALID			0xFFFE
#define MBOX_MSG_MAX				0xFFFF

#define MBOX_MESSAGES							\
/* Generic mbox IDs (range 0x000 - 0x1FF) */				\
M(READY,		0x001, msg_req, ready_msg_rsp)			\
M(ATTACH_RESOURCES,	0x002, rsrc_attach, msg_rsp)			\
M(DETACH_RESOURCES,	0x003, rsrc_detach, msg_rsp)			\
M(MSIX_OFFSET,		0x004, msg_req, msix_offset_rsp)		\
/* CGX mbox IDs (range 0x200 - 0x3FF) */				\
M(CGX_START_RXTX,	0x200, msg_req, msg_rsp)			\
M(CGX_STOP_RXTX,	0x201, msg_req, msg_rsp)			\
M(CGX_STATS,		0x202, msg_req, cgx_stats_rsp)			\
M(CGX_MAC_ADDR_SET,	0x203, cgx_mac_addr_set_or_get,			\
				cgx_mac_addr_set_or_get)		\
M(CGX_MAC_ADDR_GET,	0x204, cgx_mac_addr_set_or_get,			\
				cgx_mac_addr_set_or_get)		\
M(CGX_PROMISC_ENABLE,	0x205, msg_req, msg_rsp)			\
M(CGX_PROMISC_DISABLE,	0x206, msg_req, msg_rsp)			\
M(CGX_START_LINKEVENTS, 0x207, msg_req, msg_rsp)			\
M(CGX_STOP_LINKEVENTS,	0x208, msg_req, msg_rsp)			\
M(CGX_GET_LINKINFO,	0x209, msg_req, cgx_link_info_msg)		\
M(CGX_INTLBK_ENABLE,	0x20A, msg_req, msg_rsp)			\
M(CGX_INTLBK_DISABLE,	0x20B, msg_req, msg_rsp)			\
/* NPA mbox IDs (range 0x400 - 0x5FF) */				\
M(NPA_LF_ALLOC,		0x400, npa_lf_alloc_req, npa_lf_alloc_rsp)	\
M(NPA_LF_FREE,		0x401, msg_req, msg_rsp)			\
M(NPA_AQ_ENQ,		0x402, npa_aq_enq_req, npa_aq_enq_rsp)		\
M(NPA_HWCTX_DISABLE,	0x403, hwctx_disable_req, msg_rsp)		\
/* SSO/SSOW mbox IDs (range 0x600 - 0x7FF) */				\
/* TIM mbox IDs (range 0x800 - 0x9FF) */				\
/* CPT mbox IDs (range 0xA00 - 0xBFF) */				\
/* NPC mbox IDs (range 0x6000 - 0x7FFF) */				\
/* NIX mbox IDs (range 0x8000 - 0xFFFF) */				\
M(NIX_LF_ALLOC,		0x8000, nix_lf_alloc_req, nix_lf_alloc_rsp)	\
M(NIX_LF_FREE,		0x8001, msg_req, msg_rsp)			\
M(NIX_AQ_ENQ,		0x8002, nix_aq_enq_req, nix_aq_enq_rsp)		\
M(NIX_HWCTX_DISABLE,	0x8003, hwctx_disable_req, msg_rsp)		\
M(NIX_TXSCH_ALLOC,	0x8004, nix_txsch_alloc_req, nix_txsch_alloc_rsp) \
M(NIX_TXSCH_FREE,	0x8005, nix_txsch_free_req, msg_rsp)		\
M(NIX_TXSCHQ_CFG,	0x8006, nix_txschq_config, msg_rsp)		\
M(NIX_STATS_RST,	0x8007, msg_req, msg_rsp)			\
M(NIX_VTAG_CFG,	0x8008, nix_vtag_config, msg_rsp)		\
M(NIX_RSS_FLOWKEY_CFG,  0x8009, nix_rss_flowkey_cfg, msg_rsp)		\
M(NIX_SET_MAC_ADDR,	0x800a, nix_set_mac_addr, msg_rsp)		\
M(NIX_SET_RX_MODE,	0x800b, nix_rx_mode, msg_rsp)

/* Messages initiated by AF (range 0xC00 - 0xDFF) */
#define MBOX_UP_CGX_MESSAGES						\
M(CGX_LINK_EVENT,		0xC00, cgx_link_info_msg, msg_rsp)

enum {
#define M(_name, _id, _1, _2) MBOX_MSG_ ## _name = _id,
MBOX_MESSAGES
MBOX_UP_CGX_MESSAGES
#undef M
};

/* Mailbox message formats */

#define RVU_DEFAULT_PF_FUNC     0xFFFF

/* Generic request msg used for those mbox messages which
 * don't send any data in the request.
 */
struct msg_req {
	struct mbox_msghdr hdr;
};

/* Generic rsponse msg used a ack or response for those mbox
 * messages which doesn't have a specific rsp msg format.
 */
struct msg_rsp {
	struct mbox_msghdr hdr;
};

struct ready_msg_rsp {
	struct mbox_msghdr hdr;
	u16    sclk_feq;	/* SCLK frequency */
};

/* Structure for requesting resource provisioning.
 * 'modify' flag to be used when either requesting more
 * or to detach partial of a cetain resource type.
 * Rest of the fields specify how many of what type to
 * be attached.
 */
struct rsrc_attach {
	struct mbox_msghdr hdr;
	u8   modify:1;
	u8   npalf:1;
	u8   nixlf:1;
	u16  sso;
	u16  ssow;
	u16  timlfs;
	u16  cptlfs;
};

/* Structure for relinquishing resources.
 * 'partial' flag to be used when relinquishing all resources
 * but only of a certain type. If not set, all resources of all
 * types provisioned to the RVU function will be detached.
 */
struct rsrc_detach {
	struct mbox_msghdr hdr;
	u8 partial:1;
	u8 npalf:1;
	u8 nixlf:1;
	u8 sso:1;
	u8 ssow:1;
	u8 timlfs:1;
	u8 cptlfs:1;
};

#define MSIX_VECTOR_INVALID	0xFFFF
#define MAX_RVU_BLKLF_CNT	256

struct msix_offset_rsp {
	struct mbox_msghdr hdr;
	u16  npa_msixoff;
	u16  nix_msixoff;
	u8   sso;
	u8   ssow;
	u8   timlfs;
	u8   cptlfs;
	u16  sso_msixoff[MAX_RVU_BLKLF_CNT];
	u16  ssow_msixoff[MAX_RVU_BLKLF_CNT];
	u16  timlf_msixoff[MAX_RVU_BLKLF_CNT];
	u16  cptlf_msixoff[MAX_RVU_BLKLF_CNT];
};

/* CGX mbox message formats */

struct cgx_stats_rsp {
	struct mbox_msghdr hdr;
#define CGX_RX_STATS_COUNT	13
#define CGX_TX_STATS_COUNT	18
	u64 rx_stats[CGX_RX_STATS_COUNT];
	u64 tx_stats[CGX_TX_STATS_COUNT];
};

/* Structure for requesting the operation for
 * setting/getting mac address in the CGX interface
 */
struct cgx_mac_addr_set_or_get {
	struct mbox_msghdr hdr;
	u8 mac_addr[ETH_ALEN];
};

struct cgx_link_user_info {
	uint64_t link_up:1;
	uint64_t full_duplex:1;
	uint64_t lmac_type_id:4;
	uint64_t speed:20; /* speed in Mbps */
#define LMACTYPE_STR_LEN 16
	char lmac_type[LMACTYPE_STR_LEN];
};

struct cgx_link_info_msg {
	struct mbox_msghdr hdr;
	struct cgx_link_user_info link_info;
};

/* NPA mbox message formats */

/* NPA mailbox error codes
 * Range 301 - 400.
 */
enum npa_af_status {
	NPA_AF_ERR_PARAM            = -301,
	NPA_AF_ERR_AQ_FULL          = -302,
	NPA_AF_ERR_AQ_ENQUEUE       = -303,
	NPA_AF_ERR_AF_LF_INVALID    = -304,
	NPA_AF_ERR_AF_LF_ALLOC      = -305,
	NPA_AF_ERR_LF_RESET         = -306,
};

/* For NPA LF context alloc and init */
struct npa_lf_alloc_req {
	struct mbox_msghdr hdr;
	int node;
	int aura_sz;  /* No of auras */
	u32 nr_pools; /* No of pools */
};

struct npa_lf_alloc_rsp {
	struct mbox_msghdr hdr;
	u32 stack_pg_ptrs;  /* No of ptrs per stack page */
	u32 stack_pg_bytes; /* Size of stack page */
	u16 qints; /* NPA_AF_CONST::QINTS */
};

/* NPA AQ enqueue msg */
struct npa_aq_enq_req {
	struct mbox_msghdr hdr;
	u32 aura_id;
	u8 ctype;
	u8 op;
	union {
		/* Valid when op == WRITE/INIT and ctype == AURA.
		 * LF fills the pool_id in aura.pool_addr. AF will translate
		 * the pool_id to pool context pointer.
		 */
		struct npa_aura_s aura;
		/* Valid when op == WRITE/INIT and ctype == POOL */
		struct npa_pool_s pool;
	};
	/* Mask data when op == WRITE (1=write, 0=don't write) */
	union {
		/* Valid when op == WRITE and ctype == AURA */
		struct npa_aura_s aura_mask;
		/* Valid when op == WRITE and ctype == POOL */
		struct npa_pool_s pool_mask;
	};
};

struct npa_aq_enq_rsp {
	struct mbox_msghdr hdr;
	union {
		/* Valid when op == READ and ctype == AURA */
		struct npa_aura_s aura;
		/* Valid when op == READ and ctype == POOL */
		struct npa_pool_s pool;
	};
};

/* Disable all contexts of type 'ctype' */
struct hwctx_disable_req {
	struct mbox_msghdr hdr;
	u8 ctype;
};

/* NIX mailbox error codes
 * Range 401 - 500.
 */
enum nix_af_status {
	NIX_AF_ERR_PARAM            = -401,
	NIX_AF_ERR_AQ_FULL          = -402,
	NIX_AF_ERR_AQ_ENQUEUE       = -403,
	NIX_AF_ERR_AF_LF_INVALID    = -404,
	NIX_AF_ERR_AF_LF_ALLOC      = -405,
	NIX_AF_ERR_TLX_ALLOC_FAIL   = -406,
	NIX_AF_ERR_TLX_INVALID      = -407,
	NIX_AF_ERR_RSS_SIZE_INVALID = -408,
	NIX_AF_ERR_RSS_GRPS_INVALID = -409,
	NIX_AF_ERR_FRS_INVALID      = -410,
	NIX_AF_ERR_RX_LINK_INVALID  = -411,
	NIX_AF_INVAL_TXSCHQ_CFG     = -412,
	NIX_AF_SMQ_FLUSH_FAILED     = -413,
	NIX_AF_ERR_LF_RESET         = -414,
};

/* For NIX LF context alloc and init */
struct nix_lf_alloc_req {
	struct mbox_msghdr hdr;
	int node;
	u32 rq_cnt;   /* No of receive queues */
	u32 sq_cnt;   /* No of send queues */
	u32 cq_cnt;   /* No of completion queues */
	u8  xqe_sz;
	u16 rss_sz;
	u8  rss_grps;
	u16 npa_func;
	u16 sso_func;
	u64 rx_cfg;   /* See NIX_AF_LF(0..127)_RX_CFG */
};

struct nix_lf_alloc_rsp {
	struct mbox_msghdr hdr;
	u16	sqb_size;
	u16	rx_chan_base;
	u16	tx_chan_base;
	u8      rx_chan_cnt; /* total number of RX channels */
	u8      tx_chan_cnt; /* total number of TX channels */
	u8	lso_tsov4_idx;
	u8	lso_tsov6_idx;
	u8      mac_addr[ETH_ALEN];
};

/* NIX AQ enqueue msg */
struct nix_aq_enq_req {
	struct mbox_msghdr hdr;
	u32  qidx;
	u8 ctype;
	u8 op;
	union {
		struct nix_rq_ctx_s rq;
		struct nix_sq_ctx_s sq;
		struct nix_cq_ctx_s cq;
		struct nix_rsse_s   rss;
		struct nix_rx_mce_s mce;
	};
	union {
		struct nix_rq_ctx_s rq_mask;
		struct nix_sq_ctx_s sq_mask;
		struct nix_cq_ctx_s cq_mask;
		struct nix_rsse_s   rss_mask;
		struct nix_rx_mce_s mce_mask;
	};
};

struct nix_aq_enq_rsp {
	struct mbox_msghdr hdr;
	union {
		struct nix_rq_ctx_s rq;
		struct nix_sq_ctx_s sq;
		struct nix_cq_ctx_s cq;
		struct nix_rsse_s   rss;
		struct nix_rx_mce_s mce;
	};
};

/* Tx scheduler/shaper mailbox messages */

#define MAX_TXSCHQ_PER_FUNC		128

struct nix_txsch_alloc_req {
	struct mbox_msghdr hdr;
	/* Scheduler queue count request at each level */
	u16 schq_contig[NIX_TXSCH_LVL_CNT]; /* No of contiguous queues */
	u16 schq[NIX_TXSCH_LVL_CNT]; /* No of non-contiguous queues */
};

struct nix_txsch_alloc_rsp {
	struct mbox_msghdr hdr;
	/* Scheduler queue count allocated at each level */
	u16 schq_contig[NIX_TXSCH_LVL_CNT];
	u16 schq[NIX_TXSCH_LVL_CNT];
	/* Scheduler queue list allocated at each level */
	u16 schq_contig_list[NIX_TXSCH_LVL_CNT][MAX_TXSCHQ_PER_FUNC];
	u16 schq_list[NIX_TXSCH_LVL_CNT][MAX_TXSCHQ_PER_FUNC];
};

struct nix_txsch_free_req {
	struct mbox_msghdr hdr;
#define TXSCHQ_FREE_ALL BIT_ULL(0)
	u16 flags;
	/* Scheduler queue level to be freed */
	u16 schq_lvl;
	/* List of scheduler queues to be freed */
	u16 schq;
};

struct nix_txschq_config {
	struct mbox_msghdr hdr;
	u8 lvl;	/* SMQ/MDQ/TL4/TL3/TL2/TL1 */
#define TXSCHQ_IDX_SHIFT	16
#define TXSCHQ_IDX_MASK		(BIT_ULL(10) - 1)
#define TXSCHQ_IDX(reg, shift)	(((reg) >> (shift)) & TXSCHQ_IDX_MASK)
	u8 num_regs;
#define MAX_REGS_PER_MBOX_MSG	20
	u64 reg[MAX_REGS_PER_MBOX_MSG];
	u64 regval[MAX_REGS_PER_MBOX_MSG];
};

struct nix_vtag_config {
	struct mbox_msghdr hdr;
	u8 vtag_size;
	/* cfg_type is '0' for tx vlan cfg
	 * cfg_type is '1' for rx vlan cfg
	 */
	u8 cfg_type;
	union {
		/* valid when cfg_type is '0' */
		struct {
			/* tx vlan0 tag(C-VLAN) */
			u64 vlan0;
			/* tx vlan1 tag(S-VLAN) */
			u64 vlan1;
			/* insert tx vlan tag */
			u8 insert_vlan :1;
			/* insert tx double vlan tag */
			u8 double_vlan :1;
		} tx;

		/* valid when cfg_type is '1' */
		struct {
			/* rx vtag type index */
			u8 vtag_type;
			/* rx vtag strip */
			u8 strip_vtag :1;
			/* rx vtag capture */
			u8 capture_vtag :1;
		} rx;
	};
};

struct nix_rss_flowkey_cfg {
	struct mbox_msghdr hdr;
	int	mcam_index;  /* MCAM entry index to modify */
	u32	flowkey_cfg; /* Flowkey types selected */
	u8	group;       /* RSS context or group */
};

struct nix_set_mac_addr {
	struct mbox_msghdr hdr;
	u8 mac_addr[ETH_ALEN]; /* MAC address to be set for this pcifunc */
};

struct nix_rx_mode {
	struct mbox_msghdr hdr;
#define NIX_RX_MODE_UCAST	BIT(0)
#define NIX_RX_MODE_PROMISC	BIT(1)
#define NIX_RX_MODE_ALLMULTI	BIT(2)
	u16	mode;
};

#endif /* MBOX_H */
