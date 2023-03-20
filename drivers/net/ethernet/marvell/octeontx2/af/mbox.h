/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell.
 *
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

#define MBOX_RSP_TIMEOUT	6000 /* Time(ms) to wait for mbox response */

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
	void	    *hwbase;
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

/* Header which precedes all mbox messages */
struct mbox_hdr {
	u64 msg_size;	/* Total msgs size embedded */
	u16  num_msgs;   /* No of msgs embedded */
};

/* Header which precedes every msg and is also part of it */
struct mbox_msghdr {
	u16 pcifunc;     /* Who's sending this msg */
	u16 id;          /* Mbox message ID */
#define OTX2_MBOX_REQ_SIG (0xdead)
#define OTX2_MBOX_RSP_SIG (0xbeef)
	u16 sig;         /* Signature, for validating corrupted msgs */
#define OTX2_MBOX_VERSION (0x000a)
	u16 ver;         /* Version of msg's structure for this ID */
	u16 next_msgoff; /* Offset of next msg within mailbox region */
	int rc;          /* Msg process'ed response code */
};

void otx2_mbox_reset(struct otx2_mbox *mbox, int devid);
void __otx2_mbox_reset(struct otx2_mbox *mbox, int devid);
void otx2_mbox_destroy(struct otx2_mbox *mbox);
int otx2_mbox_init(struct otx2_mbox *mbox, void __force *hwbase,
		   struct pci_dev *pdev, void __force *reg_base,
		   int direction, int ndevs);
int otx2_mbox_regions_init(struct otx2_mbox *mbox, void __force **hwbase,
			   struct pci_dev *pdev, void __force *reg_base,
			   int direction, int ndevs);
void otx2_mbox_msg_send(struct otx2_mbox *mbox, int devid);
int otx2_mbox_wait_for_rsp(struct otx2_mbox *mbox, int devid);
int otx2_mbox_busy_poll_for_rsp(struct otx2_mbox *mbox, int devid);
struct mbox_msghdr *otx2_mbox_alloc_msg_rsp(struct otx2_mbox *mbox, int devid,
					    int size, int size_rsp);
struct mbox_msghdr *otx2_mbox_get_rsp(struct otx2_mbox *mbox, int devid,
				      struct mbox_msghdr *msg);
int otx2_mbox_check_rsp_msgs(struct otx2_mbox *mbox, int devid);
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
M(READY,		0x001, ready, msg_req, ready_msg_rsp)		\
M(ATTACH_RESOURCES,	0x002, attach_resources, rsrc_attach, msg_rsp)	\
M(DETACH_RESOURCES,	0x003, detach_resources, rsrc_detach, msg_rsp)	\
M(FREE_RSRC_CNT,	0x004, free_rsrc_cnt, msg_req, free_rsrcs_rsp)	\
M(MSIX_OFFSET,		0x005, msix_offset, msg_req, msix_offset_rsp)	\
M(VF_FLR,		0x006, vf_flr, msg_req, msg_rsp)		\
M(PTP_OP,		0x007, ptp_op, ptp_req, ptp_rsp)		\
M(GET_HW_CAP,		0x008, get_hw_cap, msg_req, get_hw_cap_rsp)	\
M(LMTST_TBL_SETUP,	0x00a, lmtst_tbl_setup, lmtst_tbl_setup_req,    \
				msg_rsp)				\
M(SET_VF_PERM,		0x00b, set_vf_perm, set_vf_perm, msg_rsp)	\
/* CGX mbox IDs (range 0x200 - 0x3FF) */				\
M(CGX_START_RXTX,	0x200, cgx_start_rxtx, msg_req, msg_rsp)	\
M(CGX_STOP_RXTX,	0x201, cgx_stop_rxtx, msg_req, msg_rsp)		\
M(CGX_STATS,		0x202, cgx_stats, msg_req, cgx_stats_rsp)	\
M(CGX_MAC_ADDR_SET,	0x203, cgx_mac_addr_set, cgx_mac_addr_set_or_get,    \
				cgx_mac_addr_set_or_get)		\
M(CGX_MAC_ADDR_GET,	0x204, cgx_mac_addr_get, cgx_mac_addr_set_or_get,    \
				cgx_mac_addr_set_or_get)		\
M(CGX_PROMISC_ENABLE,	0x205, cgx_promisc_enable, msg_req, msg_rsp)	\
M(CGX_PROMISC_DISABLE,	0x206, cgx_promisc_disable, msg_req, msg_rsp)	\
M(CGX_START_LINKEVENTS, 0x207, cgx_start_linkevents, msg_req, msg_rsp)	\
M(CGX_STOP_LINKEVENTS,	0x208, cgx_stop_linkevents, msg_req, msg_rsp)	\
M(CGX_GET_LINKINFO,	0x209, cgx_get_linkinfo, msg_req, cgx_link_info_msg) \
M(CGX_INTLBK_ENABLE,	0x20A, cgx_intlbk_enable, msg_req, msg_rsp)	\
M(CGX_INTLBK_DISABLE,	0x20B, cgx_intlbk_disable, msg_req, msg_rsp)	\
M(CGX_PTP_RX_ENABLE,	0x20C, cgx_ptp_rx_enable, msg_req, msg_rsp)	\
M(CGX_PTP_RX_DISABLE,	0x20D, cgx_ptp_rx_disable, msg_req, msg_rsp)	\
M(CGX_CFG_PAUSE_FRM,	0x20E, cgx_cfg_pause_frm, cgx_pause_frm_cfg,	\
			       cgx_pause_frm_cfg)			\
M(CGX_FW_DATA_GET,	0x20F, cgx_get_aux_link_info, msg_req, cgx_fw_data) \
M(CGX_FEC_SET,		0x210, cgx_set_fec_param, fec_mode, fec_mode) \
M(CGX_MAC_ADDR_ADD,	0x211, cgx_mac_addr_add, cgx_mac_addr_add_req,    \
				cgx_mac_addr_add_rsp)		\
M(CGX_MAC_ADDR_DEL,	0x212, cgx_mac_addr_del, cgx_mac_addr_del_req,    \
			       msg_rsp)		\
M(CGX_MAC_MAX_ENTRIES_GET, 0x213, cgx_mac_max_entries_get, msg_req,    \
				  cgx_max_dmac_entries_get_rsp)		\
M(CGX_FEC_STATS,	0x217, cgx_fec_stats, msg_req, cgx_fec_stats_rsp) \
M(CGX_SET_LINK_MODE,	0x218, cgx_set_link_mode, cgx_set_link_mode_req,\
			       cgx_set_link_mode_rsp)	\
M(CGX_GET_PHY_FEC_STATS, 0x219, cgx_get_phy_fec_stats, msg_req, msg_rsp) \
M(CGX_FEATURES_GET,	0x21B, cgx_features_get, msg_req,		\
			       cgx_features_info_msg)			\
M(RPM_STATS,		0x21C, rpm_stats, msg_req, rpm_stats_rsp)	\
M(CGX_MAC_ADDR_RESET,	0x21D, cgx_mac_addr_reset, cgx_mac_addr_reset_req, \
							msg_rsp) \
M(CGX_MAC_ADDR_UPDATE,	0x21E, cgx_mac_addr_update, cgx_mac_addr_update_req, \
						    cgx_mac_addr_update_rsp) \
M(CGX_PRIO_FLOW_CTRL_CFG, 0x21F, cgx_prio_flow_ctrl_cfg, cgx_pfc_cfg,  \
				 cgx_pfc_rsp)                               \
/* NPA mbox IDs (range 0x400 - 0x5FF) */				\
M(NPA_LF_ALLOC,		0x400, npa_lf_alloc,				\
				npa_lf_alloc_req, npa_lf_alloc_rsp)	\
M(NPA_LF_FREE,		0x401, npa_lf_free, msg_req, msg_rsp)		\
M(NPA_AQ_ENQ,		0x402, npa_aq_enq, npa_aq_enq_req, npa_aq_enq_rsp)   \
M(NPA_HWCTX_DISABLE,	0x403, npa_hwctx_disable, hwctx_disable_req, msg_rsp)\
/* SSO/SSOW mbox IDs (range 0x600 - 0x7FF) */				\
/* TIM mbox IDs (range 0x800 - 0x9FF) */				\
/* CPT mbox IDs (range 0xA00 - 0xBFF) */				\
M(CPT_LF_ALLOC,		0xA00, cpt_lf_alloc, cpt_lf_alloc_req_msg,	\
			       msg_rsp)					\
M(CPT_LF_FREE,		0xA01, cpt_lf_free, msg_req, msg_rsp)		\
M(CPT_RD_WR_REGISTER,	0xA02, cpt_rd_wr_register,  cpt_rd_wr_reg_msg,	\
			       cpt_rd_wr_reg_msg)			\
M(CPT_INLINE_IPSEC_CFG,	0xA04, cpt_inline_ipsec_cfg,			\
			       cpt_inline_ipsec_cfg_msg, msg_rsp)	\
M(CPT_STATS,            0xA05, cpt_sts, cpt_sts_req, cpt_sts_rsp)	\
M(CPT_RXC_TIME_CFG,     0xA06, cpt_rxc_time_cfg, cpt_rxc_time_cfg_req,  \
			       msg_rsp)                                 \
M(CPT_CTX_CACHE_SYNC,   0xA07, cpt_ctx_cache_sync, msg_req, msg_rsp)    \
M(CPT_LF_RESET,         0xA08, cpt_lf_reset, cpt_lf_rst_req, msg_rsp)	\
M(CPT_FLT_ENG_INFO,     0xA09, cpt_flt_eng_info, cpt_flt_eng_info_req,	\
			       cpt_flt_eng_info_rsp)			\
/* SDP mbox IDs (range 0x1000 - 0x11FF) */				\
M(SET_SDP_CHAN_INFO, 0x1000, set_sdp_chan_info, sdp_chan_info_msg, msg_rsp) \
M(GET_SDP_CHAN_INFO, 0x1001, get_sdp_chan_info, msg_req, sdp_get_chan_info_msg) \
/* NPC mbox IDs (range 0x6000 - 0x7FFF) */				\
M(NPC_MCAM_ALLOC_ENTRY,	0x6000, npc_mcam_alloc_entry, npc_mcam_alloc_entry_req,\
				npc_mcam_alloc_entry_rsp)		\
M(NPC_MCAM_FREE_ENTRY,	0x6001, npc_mcam_free_entry,			\
				 npc_mcam_free_entry_req, msg_rsp)	\
M(NPC_MCAM_WRITE_ENTRY,	0x6002, npc_mcam_write_entry,			\
				 npc_mcam_write_entry_req, msg_rsp)	\
M(NPC_MCAM_ENA_ENTRY,   0x6003, npc_mcam_ena_entry,			\
				 npc_mcam_ena_dis_entry_req, msg_rsp)	\
M(NPC_MCAM_DIS_ENTRY,   0x6004, npc_mcam_dis_entry,			\
				 npc_mcam_ena_dis_entry_req, msg_rsp)	\
M(NPC_MCAM_SHIFT_ENTRY, 0x6005, npc_mcam_shift_entry, npc_mcam_shift_entry_req,\
				npc_mcam_shift_entry_rsp)		\
M(NPC_MCAM_ALLOC_COUNTER, 0x6006, npc_mcam_alloc_counter,		\
					npc_mcam_alloc_counter_req,	\
					npc_mcam_alloc_counter_rsp)	\
M(NPC_MCAM_FREE_COUNTER,  0x6007, npc_mcam_free_counter,		\
				    npc_mcam_oper_counter_req, msg_rsp)	\
M(NPC_MCAM_UNMAP_COUNTER, 0x6008, npc_mcam_unmap_counter,		\
				   npc_mcam_unmap_counter_req, msg_rsp)	\
M(NPC_MCAM_CLEAR_COUNTER, 0x6009, npc_mcam_clear_counter,		\
				   npc_mcam_oper_counter_req, msg_rsp)	\
M(NPC_MCAM_COUNTER_STATS, 0x600a, npc_mcam_counter_stats,		\
				   npc_mcam_oper_counter_req,		\
				   npc_mcam_oper_counter_rsp)		\
M(NPC_MCAM_ALLOC_AND_WRITE_ENTRY, 0x600b, npc_mcam_alloc_and_write_entry,      \
					  npc_mcam_alloc_and_write_entry_req,  \
					  npc_mcam_alloc_and_write_entry_rsp)  \
M(NPC_GET_KEX_CFG,	  0x600c, npc_get_kex_cfg,			\
				   msg_req, npc_get_kex_cfg_rsp)	\
M(NPC_INSTALL_FLOW,	  0x600d, npc_install_flow,			       \
				  npc_install_flow_req, npc_install_flow_rsp)  \
M(NPC_DELETE_FLOW,	  0x600e, npc_delete_flow,			\
				  npc_delete_flow_req, msg_rsp)		\
M(NPC_MCAM_READ_ENTRY,	  0x600f, npc_mcam_read_entry,			\
				  npc_mcam_read_entry_req,		\
				  npc_mcam_read_entry_rsp)		\
M(NPC_SET_PKIND,        0x6010,   npc_set_pkind,                        \
				  npc_set_pkind, msg_rsp)               \
M(NPC_MCAM_READ_BASE_RULE, 0x6011, npc_read_base_steer_rule,            \
				   msg_req, npc_mcam_read_base_rule_rsp)  \
M(NPC_MCAM_GET_STATS, 0x6012, npc_mcam_entry_stats,                     \
				   npc_mcam_get_stats_req,              \
				   npc_mcam_get_stats_rsp)              \
M(NPC_GET_SECRET_KEY, 0x6013, npc_get_secret_key,                     \
				   npc_get_secret_key_req,              \
				   npc_get_secret_key_rsp)              \
M(NPC_GET_FIELD_STATUS, 0x6014, npc_get_field_status,                     \
				   npc_get_field_status_req,              \
				   npc_get_field_status_rsp)              \
/* NIX mbox IDs (range 0x8000 - 0xFFFF) */				\
M(NIX_LF_ALLOC,		0x8000, nix_lf_alloc,				\
				 nix_lf_alloc_req, nix_lf_alloc_rsp)	\
M(NIX_LF_FREE,		0x8001, nix_lf_free, nix_lf_free_req, msg_rsp)	\
M(NIX_AQ_ENQ,		0x8002, nix_aq_enq, nix_aq_enq_req, nix_aq_enq_rsp)  \
M(NIX_HWCTX_DISABLE,	0x8003, nix_hwctx_disable,			\
				 hwctx_disable_req, msg_rsp)		\
M(NIX_TXSCH_ALLOC,	0x8004, nix_txsch_alloc,			\
				 nix_txsch_alloc_req, nix_txsch_alloc_rsp)   \
M(NIX_TXSCH_FREE,	0x8005, nix_txsch_free, nix_txsch_free_req, msg_rsp) \
M(NIX_TXSCHQ_CFG,	0x8006, nix_txschq_cfg, nix_txschq_config,	\
				nix_txschq_config)			\
M(NIX_STATS_RST,	0x8007, nix_stats_rst, msg_req, msg_rsp)	\
M(NIX_VTAG_CFG,		0x8008, nix_vtag_cfg, nix_vtag_config,		\
				 nix_vtag_config_rsp)			\
M(NIX_RSS_FLOWKEY_CFG,  0x8009, nix_rss_flowkey_cfg,			\
				 nix_rss_flowkey_cfg,			\
				 nix_rss_flowkey_cfg_rsp)		\
M(NIX_SET_MAC_ADDR,	0x800a, nix_set_mac_addr, nix_set_mac_addr, msg_rsp) \
M(NIX_SET_RX_MODE,	0x800b, nix_set_rx_mode, nix_rx_mode, msg_rsp)	\
M(NIX_SET_HW_FRS,	0x800c, nix_set_hw_frs, nix_frs_cfg, msg_rsp)	\
M(NIX_LF_START_RX,	0x800d, nix_lf_start_rx, msg_req, msg_rsp)	\
M(NIX_LF_STOP_RX,	0x800e, nix_lf_stop_rx, msg_req, msg_rsp)	\
M(NIX_MARK_FORMAT_CFG,	0x800f, nix_mark_format_cfg,			\
				 nix_mark_format_cfg,			\
				 nix_mark_format_cfg_rsp)		\
M(NIX_SET_RX_CFG,	0x8010, nix_set_rx_cfg, nix_rx_cfg, msg_rsp)	\
M(NIX_LSO_FORMAT_CFG,	0x8011, nix_lso_format_cfg,			\
				 nix_lso_format_cfg,			\
				 nix_lso_format_cfg_rsp)		\
M(NIX_LF_PTP_TX_ENABLE, 0x8013, nix_lf_ptp_tx_enable, msg_req, msg_rsp)	\
M(NIX_LF_PTP_TX_DISABLE, 0x8014, nix_lf_ptp_tx_disable, msg_req, msg_rsp) \
M(NIX_BP_ENABLE,	0x8016, nix_bp_enable, nix_bp_cfg_req,	\
				nix_bp_cfg_rsp)	\
M(NIX_BP_DISABLE,	0x8017, nix_bp_disable, nix_bp_cfg_req, msg_rsp) \
M(NIX_GET_MAC_ADDR, 0x8018, nix_get_mac_addr, msg_req, nix_get_mac_addr_rsp) \
M(NIX_INLINE_IPSEC_CFG, 0x8019, nix_inline_ipsec_cfg,			\
				nix_inline_ipsec_cfg, msg_rsp)		\
M(NIX_INLINE_IPSEC_LF_CFG, 0x801a, nix_inline_ipsec_lf_cfg,		\
				nix_inline_ipsec_lf_cfg, msg_rsp)	\
M(NIX_CN10K_AQ_ENQ,	0x801b, nix_cn10k_aq_enq, nix_cn10k_aq_enq_req, \
				nix_cn10k_aq_enq_rsp)			\
M(NIX_GET_HW_INFO,	0x801c, nix_get_hw_info, msg_req, nix_hw_info)	\
M(NIX_BANDPROF_ALLOC,	0x801d, nix_bandprof_alloc, nix_bandprof_alloc_req, \
				nix_bandprof_alloc_rsp)			    \
M(NIX_BANDPROF_FREE,	0x801e, nix_bandprof_free, nix_bandprof_free_req,   \
				msg_rsp)				    \
M(NIX_BANDPROF_GET_HWINFO, 0x801f, nix_bandprof_get_hwinfo, msg_req,		\
				nix_bandprof_get_hwinfo_rsp)		    \
M(NIX_READ_INLINE_IPSEC_CFG, 0x8023, nix_read_inline_ipsec_cfg,		\
				msg_req, nix_inline_ipsec_cfg)		\
/* MCS mbox IDs (range 0xA000 - 0xBFFF) */					\
M(MCS_ALLOC_RESOURCES,	0xa000, mcs_alloc_resources, mcs_alloc_rsrc_req,	\
				mcs_alloc_rsrc_rsp)				\
M(MCS_FREE_RESOURCES,	0xa001, mcs_free_resources, mcs_free_rsrc_req, msg_rsp) \
M(MCS_FLOWID_ENTRY_WRITE, 0xa002, mcs_flowid_entry_write, mcs_flowid_entry_write_req,	\
				msg_rsp)					\
M(MCS_SECY_PLCY_WRITE,	0xa003, mcs_secy_plcy_write, mcs_secy_plcy_write_req,	\
				msg_rsp)					\
M(MCS_RX_SC_CAM_WRITE,	0xa004, mcs_rx_sc_cam_write, mcs_rx_sc_cam_write_req,	\
				msg_rsp)					\
M(MCS_SA_PLCY_WRITE,	0xa005, mcs_sa_plcy_write, mcs_sa_plcy_write_req,	\
				msg_rsp)					\
M(MCS_TX_SC_SA_MAP_WRITE, 0xa006, mcs_tx_sc_sa_map_write, mcs_tx_sc_sa_map,	\
				  msg_rsp)					\
M(MCS_RX_SC_SA_MAP_WRITE, 0xa007, mcs_rx_sc_sa_map_write, mcs_rx_sc_sa_map,	\
				  msg_rsp)					\
M(MCS_FLOWID_ENA_ENTRY,	0xa008, mcs_flowid_ena_entry, mcs_flowid_ena_dis_entry,	\
				msg_rsp)					\
M(MCS_PN_TABLE_WRITE,	0xa009, mcs_pn_table_write, mcs_pn_table_write_req,	\
				msg_rsp)					\
M(MCS_SET_ACTIVE_LMAC,	0xa00a,	mcs_set_active_lmac, mcs_set_active_lmac,	\
				msg_rsp)					\
M(MCS_GET_HW_INFO,	0xa00b,	mcs_get_hw_info, msg_req, mcs_hw_info)		\
M(MCS_GET_FLOWID_STATS, 0xa00c, mcs_get_flowid_stats, mcs_stats_req,		\
				mcs_flowid_stats)				\
M(MCS_GET_SECY_STATS,	0xa00d, mcs_get_secy_stats, mcs_stats_req,		\
				mcs_secy_stats)					\
M(MCS_GET_SC_STATS,	0xa00e, mcs_get_sc_stats, mcs_stats_req, mcs_sc_stats)	\
M(MCS_GET_SA_STATS,	0xa00f, mcs_get_sa_stats, mcs_stats_req, mcs_sa_stats)	\
M(MCS_GET_PORT_STATS,	0xa010, mcs_get_port_stats, mcs_stats_req,		\
				mcs_port_stats)					\
M(MCS_CLEAR_STATS,	0xa011,	mcs_clear_stats, mcs_clear_stats, msg_rsp)	\
M(MCS_INTR_CFG,		0xa012, mcs_intr_cfg, mcs_intr_cfg, msg_rsp)		\
M(MCS_SET_LMAC_MODE,	0xa013, mcs_set_lmac_mode, mcs_set_lmac_mode, msg_rsp)	\
M(MCS_SET_PN_THRESHOLD, 0xa014, mcs_set_pn_threshold, mcs_set_pn_threshold,	\
				msg_rsp)					\
M(MCS_ALLOC_CTRL_PKT_RULE, 0xa015, mcs_alloc_ctrl_pkt_rule,			\
				   mcs_alloc_ctrl_pkt_rule_req,			\
				   mcs_alloc_ctrl_pkt_rule_rsp)			\
M(MCS_FREE_CTRL_PKT_RULE, 0xa016, mcs_free_ctrl_pkt_rule,			\
				  mcs_free_ctrl_pkt_rule_req, msg_rsp)		\
M(MCS_CTRL_PKT_RULE_WRITE, 0xa017, mcs_ctrl_pkt_rule_write,			\
				   mcs_ctrl_pkt_rule_write_req, msg_rsp)	\
M(MCS_PORT_RESET,	0xa018, mcs_port_reset, mcs_port_reset_req, msg_rsp)	\
M(MCS_PORT_CFG_SET,	0xa019, mcs_port_cfg_set, mcs_port_cfg_set_req, msg_rsp)\
M(MCS_PORT_CFG_GET,	0xa020, mcs_port_cfg_get, mcs_port_cfg_get_req,		\
				mcs_port_cfg_get_rsp)				\
M(MCS_CUSTOM_TAG_CFG_GET, 0xa021, mcs_custom_tag_cfg_get,			\
				  mcs_custom_tag_cfg_get_req,			\
				  mcs_custom_tag_cfg_get_rsp)

/* Messages initiated by AF (range 0xC00 - 0xEFF) */
#define MBOX_UP_CGX_MESSAGES						\
M(CGX_LINK_EVENT,	0xC00, cgx_link_event, cgx_link_info_msg, msg_rsp)

#define MBOX_UP_CPT_MESSAGES						\
M(CPT_INST_LMTST,	0xD00, cpt_inst_lmtst, cpt_inst_lmtst_req, msg_rsp)

#define MBOX_UP_MCS_MESSAGES						\
M(MCS_INTR_NOTIFY,	0xE00, mcs_intr_notify, mcs_intr_info, msg_rsp)

enum {
#define M(_name, _id, _1, _2, _3) MBOX_MSG_ ## _name = _id,
MBOX_MESSAGES
MBOX_UP_CGX_MESSAGES
MBOX_UP_CPT_MESSAGES
MBOX_UP_MCS_MESSAGES
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

/* Generic response msg used an ack or response for those mbox
 * messages which don't have a specific rsp msg format.
 */
struct msg_rsp {
	struct mbox_msghdr hdr;
};

/* RVU mailbox error codes
 * Range 256 - 300.
 */
enum rvu_af_status {
	RVU_INVALID_VF_ID           = -256,
};

struct ready_msg_rsp {
	struct mbox_msghdr hdr;
	u16    sclk_freq;	/* SCLK frequency (in MHz) */
	u16    rclk_freq;	/* RCLK frequency (in MHz) */
};

/* Structure for requesting resource provisioning.
 * 'modify' flag to be used when either requesting more
 * or to detach partial of a certain resource type.
 * Rest of the fields specify how many of what type to
 * be attached.
 * To request LFs from two blocks of same type this mailbox
 * can be sent twice as below:
 *      struct rsrc_attach *attach;
 *       .. Allocate memory for message ..
 *       attach->cptlfs = 3; <3 LFs from CPT0>
 *       .. Send message ..
 *       .. Allocate memory for message ..
 *       attach->modify = 1;
 *       attach->cpt_blkaddr = BLKADDR_CPT1;
 *       attach->cptlfs = 2; <2 LFs from CPT1>
 *       .. Send message ..
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
	int  cpt_blkaddr; /* BLKADDR_CPT0/BLKADDR_CPT1 or 0 for BLKADDR_CPT0 */
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

/* Number of resources available to the caller.
 * In reply to MBOX_MSG_FREE_RSRC_CNT.
 */
struct free_rsrcs_rsp {
	struct mbox_msghdr hdr;
	u16 schq[NIX_TXSCH_LVL_CNT];
	u16  sso;
	u16  tim;
	u16  ssow;
	u16  cpt;
	u8   npa;
	u8   nix;
	u16  schq_nix1[NIX_TXSCH_LVL_CNT];
	u8   nix1;
	u8   cpt1;
	u8   ree0;
	u8   ree1;
};

#define MSIX_VECTOR_INVALID	0xFFFF
#define MAX_RVU_BLKLF_CNT	256

struct msix_offset_rsp {
	struct mbox_msghdr hdr;
	u16  npa_msixoff;
	u16  nix_msixoff;
	u16  sso;
	u16  ssow;
	u16  timlfs;
	u16  cptlfs;
	u16  sso_msixoff[MAX_RVU_BLKLF_CNT];
	u16  ssow_msixoff[MAX_RVU_BLKLF_CNT];
	u16  timlf_msixoff[MAX_RVU_BLKLF_CNT];
	u16  cptlf_msixoff[MAX_RVU_BLKLF_CNT];
	u16  cpt1_lfs;
	u16  ree0_lfs;
	u16  ree1_lfs;
	u16  cpt1_lf_msixoff[MAX_RVU_BLKLF_CNT];
	u16  ree0_lf_msixoff[MAX_RVU_BLKLF_CNT];
	u16  ree1_lf_msixoff[MAX_RVU_BLKLF_CNT];
};

struct get_hw_cap_rsp {
	struct mbox_msghdr hdr;
	u8 nix_fixed_txschq_mapping; /* Schq mapping fixed or flexible */
	u8 nix_shaping;		     /* Is shaping and coloring supported */
	u8 npc_hash_extract;	/* Is hash extract supported */
};

/* CGX mbox message formats */

struct cgx_stats_rsp {
	struct mbox_msghdr hdr;
#define CGX_RX_STATS_COUNT	9
#define CGX_TX_STATS_COUNT	18
	u64 rx_stats[CGX_RX_STATS_COUNT];
	u64 tx_stats[CGX_TX_STATS_COUNT];
};

struct cgx_fec_stats_rsp {
	struct mbox_msghdr hdr;
	u64 fec_corr_blks;
	u64 fec_uncorr_blks;
};
/* Structure for requesting the operation for
 * setting/getting mac address in the CGX interface
 */
struct cgx_mac_addr_set_or_get {
	struct mbox_msghdr hdr;
	u8 mac_addr[ETH_ALEN];
	u32 index;
};

/* Structure for requesting the operation to
 * add DMAC filter entry into CGX interface
 */
struct cgx_mac_addr_add_req {
	struct mbox_msghdr hdr;
	u8 mac_addr[ETH_ALEN];
};

/* Structure for response against the operation to
 * add DMAC filter entry into CGX interface
 */
struct cgx_mac_addr_add_rsp {
	struct mbox_msghdr hdr;
	u32 index;
};

/* Structure for requesting the operation to
 * delete DMAC filter entry from CGX interface
 */
struct cgx_mac_addr_del_req {
	struct mbox_msghdr hdr;
	u32 index;
};

/* Structure for response against the operation to
 * get maximum supported DMAC filter entries
 */
struct cgx_max_dmac_entries_get_rsp {
	struct mbox_msghdr hdr;
	u32 max_dmac_filters;
};

struct cgx_link_user_info {
	uint64_t link_up:1;
	uint64_t full_duplex:1;
	uint64_t lmac_type_id:4;
	uint64_t speed:20; /* speed in Mbps */
	uint64_t an:1;		/* AN supported or not */
	uint64_t fec:2;	 /* FEC type if enabled else 0 */
#define LMACTYPE_STR_LEN 16
	char lmac_type[LMACTYPE_STR_LEN];
};

struct cgx_link_info_msg {
	struct mbox_msghdr hdr;
	struct cgx_link_user_info link_info;
};

struct cgx_pause_frm_cfg {
	struct mbox_msghdr hdr;
	u8 set;
	/* set = 1 if the request is to config pause frames */
	/* set = 0 if the request is to fetch pause frames config */
	u8 rx_pause;
	u8 tx_pause;
};

enum fec_type {
	OTX2_FEC_NONE,
	OTX2_FEC_BASER,
	OTX2_FEC_RS,
	OTX2_FEC_STATS_CNT = 2,
	OTX2_FEC_OFF,
};

struct fec_mode {
	struct mbox_msghdr hdr;
	int fec;
};

struct sfp_eeprom_s {
#define SFP_EEPROM_SIZE 256
	u16 sff_id;
	u8 buf[SFP_EEPROM_SIZE];
	u64 reserved;
};

struct phy_s {
	struct {
		u64 can_change_mod_type:1;
		u64 mod_type:1;
		u64 has_fec_stats:1;
	} misc;
	struct fec_stats_s {
		u32 rsfec_corr_cws;
		u32 rsfec_uncorr_cws;
		u32 brfec_corr_blks;
		u32 brfec_uncorr_blks;
	} fec_stats;
};

struct cgx_lmac_fwdata_s {
	u16 rw_valid;
	u64 supported_fec;
	u64 supported_an;
	u64 supported_link_modes;
	/* only applicable if AN is supported */
	u64 advertised_fec;
	u64 advertised_link_modes;
	/* Only applicable if SFP/QSFP slot is present */
	struct sfp_eeprom_s sfp_eeprom;
	struct phy_s phy;
#define LMAC_FWDATA_RESERVED_MEM 1021
	u64 reserved[LMAC_FWDATA_RESERVED_MEM];
};

struct cgx_fw_data {
	struct mbox_msghdr hdr;
	struct cgx_lmac_fwdata_s fwdata;
};

struct cgx_set_link_mode_args {
	u32 speed;
	u8 duplex;
	u8 an;
	u8 ports;
	u64 mode;
};

struct cgx_set_link_mode_req {
#define AUTONEG_UNKNOWN		0xff
	struct mbox_msghdr hdr;
	struct cgx_set_link_mode_args args;
};

struct cgx_set_link_mode_rsp {
	struct mbox_msghdr hdr;
	int status;
};

struct cgx_mac_addr_reset_req {
	struct mbox_msghdr hdr;
	u32 index;
};

struct cgx_mac_addr_update_req {
	struct mbox_msghdr hdr;
	u8 mac_addr[ETH_ALEN];
	u32 index;
};

struct cgx_mac_addr_update_rsp {
	struct mbox_msghdr hdr;
	u32 index;
};

#define RVU_LMAC_FEAT_FC		BIT_ULL(0) /* pause frames */
#define	RVU_LMAC_FEAT_HIGIG2		BIT_ULL(1)
			/* flow control from physical link higig2 messages */
#define RVU_LMAC_FEAT_PTP		BIT_ULL(2) /* precison time protocol */
#define RVU_LMAC_FEAT_DMACF		BIT_ULL(3) /* DMAC FILTER */
#define RVU_MAC_VERSION			BIT_ULL(4)
#define RVU_MAC_CGX			BIT_ULL(5)
#define RVU_MAC_RPM			BIT_ULL(6)

struct cgx_features_info_msg {
	struct mbox_msghdr hdr;
	u64    lmac_features;
};

struct rpm_stats_rsp {
	struct mbox_msghdr hdr;
#define RPM_RX_STATS_COUNT		43
#define RPM_TX_STATS_COUNT		34
	u64 rx_stats[RPM_RX_STATS_COUNT];
	u64 tx_stats[RPM_TX_STATS_COUNT];
};

struct cgx_pfc_cfg {
	struct mbox_msghdr hdr;
	u8 rx_pause;
	u8 tx_pause;
	u16 pfc_en; /*  bitmap indicating pfc enabled traffic classes */
};

struct cgx_pfc_rsp {
	struct mbox_msghdr hdr;
	u8 rx_pause;
	u8 tx_pause;
};

 /* NPA mbox message formats */

struct npc_set_pkind {
	struct mbox_msghdr hdr;
#define OTX2_PRIV_FLAGS_DEFAULT  BIT_ULL(0)
#define OTX2_PRIV_FLAGS_CUSTOM   BIT_ULL(63)
	u64 mode;
#define PKIND_TX		BIT_ULL(0)
#define PKIND_RX		BIT_ULL(1)
	u8 dir;
	u8 pkind; /* valid only in case custom flag */
	u8 var_len_off; /* Offset of custom header length field.
			 * Valid only for pkind NPC_RX_CUSTOM_PRE_L2_PKIND
			 */
	u8 var_len_off_mask; /* Mask for length with in offset */
	u8 shift_dir; /* shift direction to get length of the header at var_len_off */
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
	u64 way_mask;
};

struct npa_lf_alloc_rsp {
	struct mbox_msghdr hdr;
	u32 stack_pg_ptrs;  /* No of ptrs per stack page */
	u32 stack_pg_bytes; /* Size of stack page */
	u16 qints; /* NPA_AF_CONST::QINTS */
	u8 cache_lines; /*BATCH ALLOC DMA */
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

/* NIX mbox message formats */

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
	NIX_AF_ERR_RSS_NOSPC_FIELD  = -415,
	NIX_AF_ERR_RSS_NOSPC_ALGO   = -416,
	NIX_AF_ERR_MARK_CFG_FAIL    = -417,
	NIX_AF_ERR_LSO_CFG_FAIL     = -418,
	NIX_AF_INVAL_NPA_PF_FUNC    = -419,
	NIX_AF_INVAL_SSO_PF_FUNC    = -420,
	NIX_AF_ERR_TX_VTAG_NOSPC    = -421,
	NIX_AF_ERR_RX_VTAG_INUSE    = -422,
	NIX_AF_ERR_PTP_CONFIG_FAIL  = -423,
	NIX_AF_ERR_NPC_KEY_NOT_SUPP = -424,
	NIX_AF_ERR_INVALID_NIXBLK   = -425,
	NIX_AF_ERR_INVALID_BANDPROF = -426,
	NIX_AF_ERR_IPOLICER_NOTSUPP = -427,
	NIX_AF_ERR_BANDPROF_INVAL_REQ  = -428,
	NIX_AF_ERR_CQ_CTX_WRITE_ERR  = -429,
	NIX_AF_ERR_AQ_CTX_RETRY_WRITE  = -430,
	NIX_AF_ERR_LINK_CREDITS  = -431,
};

/* For NIX RX vtag action  */
enum nix_rx_vtag0_type {
	NIX_AF_LFX_RX_VTAG_TYPE0, /* reserved for rx vlan offload */
	NIX_AF_LFX_RX_VTAG_TYPE1,
	NIX_AF_LFX_RX_VTAG_TYPE2,
	NIX_AF_LFX_RX_VTAG_TYPE3,
	NIX_AF_LFX_RX_VTAG_TYPE4,
	NIX_AF_LFX_RX_VTAG_TYPE5,
	NIX_AF_LFX_RX_VTAG_TYPE6,
	NIX_AF_LFX_RX_VTAG_TYPE7,
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
	u64 way_mask;
#define NIX_LF_RSS_TAG_LSB_AS_ADDER BIT_ULL(0)
#define NIX_LF_LBK_BLK_SEL	    BIT_ULL(1)
	u64 flags;
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
	u8	lf_rx_stats; /* NIX_AF_CONST1::LF_RX_STATS */
	u8	lf_tx_stats; /* NIX_AF_CONST1::LF_TX_STATS */
	u16	cints; /* NIX_AF_CONST2::CINTS */
	u16	qints; /* NIX_AF_CONST2::QINTS */
	u8	cgx_links;  /* No. of CGX links present in HW */
	u8	lbk_links;  /* No. of LBK links present in HW */
	u8	sdp_links;  /* No. of SDP links present in HW */
	u8	tx_link;    /* Transmit channel link number */
};

struct nix_lf_free_req {
	struct mbox_msghdr hdr;
#define NIX_LF_DISABLE_FLOWS		BIT_ULL(0)
#define NIX_LF_DONT_FREE_TX_VTAG	BIT_ULL(1)
	u64 flags;
};

/* CN10K NIX AQ enqueue msg */
struct nix_cn10k_aq_enq_req {
	struct mbox_msghdr hdr;
	u32  qidx;
	u8 ctype;
	u8 op;
	union {
		struct nix_cn10k_rq_ctx_s rq;
		struct nix_cn10k_sq_ctx_s sq;
		struct nix_cq_ctx_s cq;
		struct nix_rsse_s   rss;
		struct nix_rx_mce_s mce;
		struct nix_bandprof_s prof;
	};
	union {
		struct nix_cn10k_rq_ctx_s rq_mask;
		struct nix_cn10k_sq_ctx_s sq_mask;
		struct nix_cq_ctx_s cq_mask;
		struct nix_rsse_s   rss_mask;
		struct nix_rx_mce_s mce_mask;
		struct nix_bandprof_s prof_mask;
	};
};

struct nix_cn10k_aq_enq_rsp {
	struct mbox_msghdr hdr;
	union {
		struct nix_cn10k_rq_ctx_s rq;
		struct nix_cn10k_sq_ctx_s sq;
		struct nix_cq_ctx_s cq;
		struct nix_rsse_s   rss;
		struct nix_rx_mce_s mce;
		struct nix_bandprof_s prof;
	};
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
		u64 prof;
	};
	union {
		struct nix_rq_ctx_s rq_mask;
		struct nix_sq_ctx_s sq_mask;
		struct nix_cq_ctx_s cq_mask;
		struct nix_rsse_s   rss_mask;
		struct nix_rx_mce_s mce_mask;
		u64 prof_mask;
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
		struct nix_bandprof_s prof;
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
	u8  aggr_level; /* Traffic aggregation scheduler level */
	u8  aggr_lvl_rr_prio; /* Aggregation lvl's RR_PRIO config */
	u8  link_cfg_lvl; /* LINKX_CFG CSRs mapped to TL3 or TL2's index ? */
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
	u8 read;
#define TXSCHQ_IDX_SHIFT	16
#define TXSCHQ_IDX_MASK		(BIT_ULL(10) - 1)
#define TXSCHQ_IDX(reg, shift)	(((reg) >> (shift)) & TXSCHQ_IDX_MASK)
	u8 num_regs;
#define MAX_REGS_PER_MBOX_MSG	20
	u64 reg[MAX_REGS_PER_MBOX_MSG];
	u64 regval[MAX_REGS_PER_MBOX_MSG];
	/* All 0's => overwrite with new value */
	u64 regval_mask[MAX_REGS_PER_MBOX_MSG];
};

struct nix_vtag_config {
	struct mbox_msghdr hdr;
	/* '0' for 4 octet VTAG, '1' for 8 octet VTAG */
	u8 vtag_size;
	/* cfg_type is '0' for tx vlan cfg
	 * cfg_type is '1' for rx vlan cfg
	 */
	u8 cfg_type;
	union {
		/* valid when cfg_type is '0' */
		struct {
			u64 vtag0;
			u64 vtag1;

			/* cfg_vtag0 & cfg_vtag1 fields are valid
			 * when free_vtag0 & free_vtag1 are '0's.
			 */
			/* cfg_vtag0 = 1 to configure vtag0 */
			u8 cfg_vtag0 :1;
			/* cfg_vtag1 = 1 to configure vtag1 */
			u8 cfg_vtag1 :1;

			/* vtag0_idx & vtag1_idx are only valid when
			 * both cfg_vtag0 & cfg_vtag1 are '0's,
			 * these fields are used along with free_vtag0
			 * & free_vtag1 to free the nix lf's tx_vlan
			 * configuration.
			 *
			 * Denotes the indices of tx_vtag def registers
			 * that needs to be cleared and freed.
			 */
			int vtag0_idx;
			int vtag1_idx;

			/* free_vtag0 & free_vtag1 fields are valid
			 * when cfg_vtag0 & cfg_vtag1 are '0's.
			 */
			/* free_vtag0 = 1 clears vtag0 configuration
			 * vtag0_idx denotes the index to be cleared.
			 */
			u8 free_vtag0 :1;
			/* free_vtag1 = 1 clears vtag1 configuration
			 * vtag1_idx denotes the index to be cleared.
			 */
			u8 free_vtag1 :1;
		} tx;

		/* valid when cfg_type is '1' */
		struct {
			/* rx vtag type index, valid values are in 0..7 range */
			u8 vtag_type;
			/* rx vtag strip */
			u8 strip_vtag :1;
			/* rx vtag capture */
			u8 capture_vtag :1;
		} rx;
	};
};

struct nix_vtag_config_rsp {
	struct mbox_msghdr hdr;
	int vtag0_idx;
	int vtag1_idx;
	/* Indices of tx_vtag def registers used to configure
	 * tx vtag0 & vtag1 headers, these indices are valid
	 * when nix_vtag_config mbox requested for vtag0 and/
	 * or vtag1 configuration.
	 */
};

struct nix_rss_flowkey_cfg {
	struct mbox_msghdr hdr;
	int	mcam_index;  /* MCAM entry index to modify */
#define NIX_FLOW_KEY_TYPE_PORT	BIT(0)
#define NIX_FLOW_KEY_TYPE_IPV4	BIT(1)
#define NIX_FLOW_KEY_TYPE_IPV6	BIT(2)
#define NIX_FLOW_KEY_TYPE_TCP	BIT(3)
#define NIX_FLOW_KEY_TYPE_UDP	BIT(4)
#define NIX_FLOW_KEY_TYPE_SCTP	BIT(5)
#define NIX_FLOW_KEY_TYPE_NVGRE    BIT(6)
#define NIX_FLOW_KEY_TYPE_VXLAN    BIT(7)
#define NIX_FLOW_KEY_TYPE_GENEVE   BIT(8)
#define NIX_FLOW_KEY_TYPE_ETH_DMAC BIT(9)
#define NIX_FLOW_KEY_TYPE_IPV6_EXT BIT(10)
#define NIX_FLOW_KEY_TYPE_GTPU       BIT(11)
#define NIX_FLOW_KEY_TYPE_INNR_IPV4     BIT(12)
#define NIX_FLOW_KEY_TYPE_INNR_IPV6     BIT(13)
#define NIX_FLOW_KEY_TYPE_INNR_TCP      BIT(14)
#define NIX_FLOW_KEY_TYPE_INNR_UDP      BIT(15)
#define NIX_FLOW_KEY_TYPE_INNR_SCTP     BIT(16)
#define NIX_FLOW_KEY_TYPE_INNR_ETH_DMAC BIT(17)
#define NIX_FLOW_KEY_TYPE_VLAN		BIT(20)
#define NIX_FLOW_KEY_TYPE_IPV4_PROTO	BIT(21)
#define NIX_FLOW_KEY_TYPE_AH		BIT(22)
#define NIX_FLOW_KEY_TYPE_ESP		BIT(23)
	u32	flowkey_cfg; /* Flowkey types selected */
	u8	group;       /* RSS context or group */
};

struct nix_rss_flowkey_cfg_rsp {
	struct mbox_msghdr hdr;
	u8	alg_idx; /* Selected algo index */
};

struct nix_set_mac_addr {
	struct mbox_msghdr hdr;
	u8 mac_addr[ETH_ALEN]; /* MAC address to be set for this pcifunc */
};

struct nix_get_mac_addr_rsp {
	struct mbox_msghdr hdr;
	u8 mac_addr[ETH_ALEN];
};

struct nix_mark_format_cfg {
	struct mbox_msghdr hdr;
	u8 offset;
	u8 y_mask;
	u8 y_val;
	u8 r_mask;
	u8 r_val;
};

struct nix_mark_format_cfg_rsp {
	struct mbox_msghdr hdr;
	u8 mark_format_idx;
};

struct nix_rx_mode {
	struct mbox_msghdr hdr;
#define NIX_RX_MODE_UCAST	BIT(0)
#define NIX_RX_MODE_PROMISC	BIT(1)
#define NIX_RX_MODE_ALLMULTI	BIT(2)
#define NIX_RX_MODE_USE_MCE	BIT(3)
	u16	mode;
};

struct nix_rx_cfg {
	struct mbox_msghdr hdr;
#define NIX_RX_OL3_VERIFY   BIT(0)
#define NIX_RX_OL4_VERIFY   BIT(1)
	u8 len_verify; /* Outer L3/L4 len check */
#define NIX_RX_CSUM_OL4_VERIFY  BIT(0)
	u8 csum_verify; /* Outer L4 checksum verification */
};

struct nix_frs_cfg {
	struct mbox_msghdr hdr;
	u8	update_smq;    /* Update SMQ's min/max lens */
	u8	update_minlen; /* Set minlen also */
	u8	sdp_link;      /* Set SDP RX link */
	u16	maxlen;
	u16	minlen;
};

struct nix_lso_format_cfg {
	struct mbox_msghdr hdr;
	u64 field_mask;
#define NIX_LSO_FIELD_MAX	8
	u64 fields[NIX_LSO_FIELD_MAX];
};

struct nix_lso_format_cfg_rsp {
	struct mbox_msghdr hdr;
	u8 lso_format_idx;
};

struct nix_bp_cfg_req {
	struct mbox_msghdr hdr;
	u16	chan_base; /* Starting channel number */
	u8	chan_cnt; /* Number of channels */
	u8	bpid_per_chan;
	/* bpid_per_chan = 0 assigns single bp id for range of channels */
	/* bpid_per_chan = 1 assigns separate bp id for each channel */
};

/* PF can be mapped to either CGX or LBK interface,
 * so maximum 64 channels are possible.
 */
#define NIX_MAX_BPID_CHAN	64
struct nix_bp_cfg_rsp {
	struct mbox_msghdr hdr;
	u16	chan_bpid[NIX_MAX_BPID_CHAN]; /* Channel and bpid mapping */
	u8	chan_cnt; /* Number of channel for which bpids are assigned */
};

/* Global NIX inline IPSec configuration */
struct nix_inline_ipsec_cfg {
	struct mbox_msghdr hdr;
	u32 cpt_credit;
	struct {
		u8 egrp;
		u16 opcode;
		u16 param1;
		u16 param2;
	} gen_cfg;
	struct {
		u16 cpt_pf_func;
		u8 cpt_slot;
	} inst_qsel;
	u8 enable;
	u16 bpid;
	u32 credit_th;
};

/* Per NIX LF inline IPSec configuration */
struct nix_inline_ipsec_lf_cfg {
	struct mbox_msghdr hdr;
	u64 sa_base_addr;
	struct {
		u32 tag_const;
		u16 lenm1_max;
		u8 sa_pow2_size;
		u8 tt;
	} ipsec_cfg0;
	struct {
		u32 sa_idx_max;
		u8 sa_idx_w;
	} ipsec_cfg1;
	u8 enable;
};

struct nix_hw_info {
	struct mbox_msghdr hdr;
	u16 rsvs16;
	u16 max_mtu;
	u16 min_mtu;
	u32 rpm_dwrr_mtu;
	u32 sdp_dwrr_mtu;
	u64 rsvd[16]; /* Add reserved fields for future expansion */
};

struct nix_bandprof_alloc_req {
	struct mbox_msghdr hdr;
	/* Count of profiles needed per layer */
	u16 prof_count[BAND_PROF_NUM_LAYERS];
};

struct nix_bandprof_alloc_rsp {
	struct mbox_msghdr hdr;
	u16 prof_count[BAND_PROF_NUM_LAYERS];

	/* There is no need to allocate morethan 1 bandwidth profile
	 * per RQ of a PF_FUNC's NIXLF. So limit the maximum
	 * profiles to 64 per PF_FUNC.
	 */
#define MAX_BANDPROF_PER_PFFUNC	64
	u16 prof_idx[BAND_PROF_NUM_LAYERS][MAX_BANDPROF_PER_PFFUNC];
};

struct nix_bandprof_free_req {
	struct mbox_msghdr hdr;
	u8 free_all;
	u16 prof_count[BAND_PROF_NUM_LAYERS];
	u16 prof_idx[BAND_PROF_NUM_LAYERS][MAX_BANDPROF_PER_PFFUNC];
};

struct nix_bandprof_get_hwinfo_rsp {
	struct mbox_msghdr hdr;
	u16 prof_count[BAND_PROF_NUM_LAYERS];
	u32 policer_timeunit;
};

/* NPC mbox message structs */

#define NPC_MCAM_ENTRY_INVALID	0xFFFF
#define NPC_MCAM_INVALID_MAP	0xFFFF

/* NPC mailbox error codes
 * Range 701 - 800.
 */
enum npc_af_status {
	NPC_MCAM_INVALID_REQ	= -701,
	NPC_MCAM_ALLOC_DENIED	= -702,
	NPC_MCAM_ALLOC_FAILED	= -703,
	NPC_MCAM_PERM_DENIED	= -704,
	NPC_FLOW_INTF_INVALID	= -707,
	NPC_FLOW_CHAN_INVALID	= -708,
	NPC_FLOW_NO_NIXLF	= -709,
	NPC_FLOW_NOT_SUPPORTED	= -710,
	NPC_FLOW_VF_PERM_DENIED	= -711,
	NPC_FLOW_VF_NOT_INIT	= -712,
	NPC_FLOW_VF_OVERLAP	= -713,
};

struct npc_mcam_alloc_entry_req {
	struct mbox_msghdr hdr;
#define NPC_MAX_NONCONTIG_ENTRIES	256
	u8  contig;   /* Contiguous entries ? */
#define NPC_MCAM_ANY_PRIO		0
#define NPC_MCAM_LOWER_PRIO		1
#define NPC_MCAM_HIGHER_PRIO		2
	u8  priority; /* Lower or higher w.r.t ref_entry */
	u16 ref_entry;
	u16 count;    /* Number of entries requested */
};

struct npc_mcam_alloc_entry_rsp {
	struct mbox_msghdr hdr;
	u16 entry; /* Entry allocated or start index if contiguous.
		    * Invalid incase of non-contiguous.
		    */
	u16 count; /* Number of entries allocated */
	u16 free_count; /* Number of entries available */
	u16 entry_list[NPC_MAX_NONCONTIG_ENTRIES];
};

struct npc_mcam_free_entry_req {
	struct mbox_msghdr hdr;
	u16 entry; /* Entry index to be freed */
	u8  all;   /* If all entries allocated to this PFVF to be freed */
};

struct mcam_entry {
#define NPC_MAX_KWS_IN_KEY	7 /* Number of keywords in max keywidth */
	u64	kw[NPC_MAX_KWS_IN_KEY];
	u64	kw_mask[NPC_MAX_KWS_IN_KEY];
	u64	action;
	u64	vtag_action;
};

struct npc_mcam_write_entry_req {
	struct mbox_msghdr hdr;
	struct mcam_entry entry_data;
	u16 entry;	 /* MCAM entry to write this match key */
	u16 cntr;	 /* Counter for this MCAM entry */
	u8  intf;	 /* Rx or Tx interface */
	u8  enable_entry;/* Enable this MCAM entry ? */
	u8  set_cntr;    /* Set counter for this entry ? */
};

/* Enable/Disable a given entry */
struct npc_mcam_ena_dis_entry_req {
	struct mbox_msghdr hdr;
	u16 entry;
};

struct npc_mcam_shift_entry_req {
	struct mbox_msghdr hdr;
#define NPC_MCAM_MAX_SHIFTS	64
	u16 curr_entry[NPC_MCAM_MAX_SHIFTS];
	u16 new_entry[NPC_MCAM_MAX_SHIFTS];
	u16 shift_count; /* Number of entries to shift */
};

struct npc_mcam_shift_entry_rsp {
	struct mbox_msghdr hdr;
	u16 failed_entry_idx; /* Index in 'curr_entry', not entry itself */
};

struct npc_mcam_alloc_counter_req {
	struct mbox_msghdr hdr;
	u8  contig;	/* Contiguous counters ? */
#define NPC_MAX_NONCONTIG_COUNTERS       64
	u16 count;	/* Number of counters requested */
};

struct npc_mcam_alloc_counter_rsp {
	struct mbox_msghdr hdr;
	u16 cntr;   /* Counter allocated or start index if contiguous.
		     * Invalid incase of non-contiguous.
		     */
	u16 count;  /* Number of counters allocated */
	u16 cntr_list[NPC_MAX_NONCONTIG_COUNTERS];
};

struct npc_mcam_oper_counter_req {
	struct mbox_msghdr hdr;
	u16 cntr;   /* Free a counter or clear/fetch it's stats */
};

struct npc_mcam_oper_counter_rsp {
	struct mbox_msghdr hdr;
	u64 stat;  /* valid only while fetching counter's stats */
};

struct npc_mcam_unmap_counter_req {
	struct mbox_msghdr hdr;
	u16 cntr;
	u16 entry; /* Entry and counter to be unmapped */
	u8  all;   /* Unmap all entries using this counter ? */
};

struct npc_mcam_alloc_and_write_entry_req {
	struct mbox_msghdr hdr;
	struct mcam_entry entry_data;
	u16 ref_entry;
	u8  priority;    /* Lower or higher w.r.t ref_entry */
	u8  intf;	 /* Rx or Tx interface */
	u8  enable_entry;/* Enable this MCAM entry ? */
	u8  alloc_cntr;  /* Allocate counter and map ? */
};

struct npc_mcam_alloc_and_write_entry_rsp {
	struct mbox_msghdr hdr;
	u16 entry;
	u16 cntr;
};

struct npc_get_kex_cfg_rsp {
	struct mbox_msghdr hdr;
	u64 rx_keyx_cfg;   /* NPC_AF_INTF(0)_KEX_CFG */
	u64 tx_keyx_cfg;   /* NPC_AF_INTF(1)_KEX_CFG */
#define NPC_MAX_INTF	2
#define NPC_MAX_LID	8
#define NPC_MAX_LT	16
#define NPC_MAX_LD	2
#define NPC_MAX_LFL	16
	/* NPC_AF_KEX_LDATA(0..1)_FLAGS_CFG */
	u64 kex_ld_flags[NPC_MAX_LD];
	/* NPC_AF_INTF(0..1)_LID(0..7)_LT(0..15)_LD(0..1)_CFG */
	u64 intf_lid_lt_ld[NPC_MAX_INTF][NPC_MAX_LID][NPC_MAX_LT][NPC_MAX_LD];
	/* NPC_AF_INTF(0..1)_LDATA(0..1)_FLAGS(0..15)_CFG */
	u64 intf_ld_flags[NPC_MAX_INTF][NPC_MAX_LD][NPC_MAX_LFL];
#define MKEX_NAME_LEN 128
	u8 mkex_pfl_name[MKEX_NAME_LEN];
};

struct flow_msg {
	unsigned char dmac[6];
	unsigned char smac[6];
	__be16 etype;
	__be16 vlan_etype;
	__be16 vlan_tci;
	union {
		__be32 ip4src;
		__be32 ip6src[4];
	};
	union {
		__be32 ip4dst;
		__be32 ip6dst[4];
	};
	u8 tos;
	u8 ip_ver;
	u8 ip_proto;
	u8 tc;
	__be16 sport;
	__be16 dport;
	union {
		u8 ip_flag;
		u8 next_header;
	};
};

struct npc_install_flow_req {
	struct mbox_msghdr hdr;
	struct flow_msg packet;
	struct flow_msg mask;
	u64 features;
	u16 entry;
	u16 channel;
	u16 chan_mask;
	u8 intf;
	u8 set_cntr; /* If counter is available set counter for this entry ? */
	u8 default_rule;
	u8 append; /* overwrite(0) or append(1) flow to default rule? */
	u16 vf;
	/* action */
	u32 index;
	u16 match_id;
	u8 flow_key_alg;
	u8 op;
	/* vtag rx action */
	u8 vtag0_type;
	u8 vtag0_valid;
	u8 vtag1_type;
	u8 vtag1_valid;
	/* vtag tx action */
	u16 vtag0_def;
	u8  vtag0_op;
	u16 vtag1_def;
	u8  vtag1_op;
};

struct npc_install_flow_rsp {
	struct mbox_msghdr hdr;
	int counter; /* negative if no counter else counter number */
};

struct npc_delete_flow_req {
	struct mbox_msghdr hdr;
	u16 entry;
	u16 start;/*Disable range of entries */
	u16 end;
	u8 all; /* PF + VFs */
};

struct npc_mcam_read_entry_req {
	struct mbox_msghdr hdr;
	u16 entry;	 /* MCAM entry to read */
};

struct npc_mcam_read_entry_rsp {
	struct mbox_msghdr hdr;
	struct mcam_entry entry_data;
	u8 intf;
	u8 enable;
};

struct npc_mcam_read_base_rule_rsp {
	struct mbox_msghdr hdr;
	struct mcam_entry entry;
};

struct npc_mcam_get_stats_req {
	struct mbox_msghdr hdr;
	u16 entry; /* mcam entry */
};

struct npc_mcam_get_stats_rsp {
	struct mbox_msghdr hdr;
	u64 stat;  /* counter stats */
	u8 stat_ena; /* enabled */
};

struct npc_get_secret_key_req {
	struct mbox_msghdr hdr;
	u8 intf;
};

struct npc_get_secret_key_rsp {
	struct mbox_msghdr hdr;
	u64 secret_key[3];
};

enum ptp_op {
	PTP_OP_ADJFINE = 0,
	PTP_OP_GET_CLOCK = 1,
	PTP_OP_GET_TSTMP = 2,
	PTP_OP_SET_THRESH = 3,
	PTP_OP_EXTTS_ON = 4,
};

struct ptp_req {
	struct mbox_msghdr hdr;
	u8 op;
	s64 scaled_ppm;
	u64 thresh;
	int extts_on;
};

struct ptp_rsp {
	struct mbox_msghdr hdr;
	u64 clk;
};

struct npc_get_field_status_req {
	struct mbox_msghdr hdr;
	u8 intf;
	u8 field;
};

struct npc_get_field_status_rsp {
	struct mbox_msghdr hdr;
	u8 enable;
};

struct set_vf_perm  {
	struct  mbox_msghdr hdr;
	u16	vf;
#define RESET_VF_PERM		BIT_ULL(0)
#define	VF_TRUSTED		BIT_ULL(1)
	u64	flags;
};

struct lmtst_tbl_setup_req {
	struct mbox_msghdr hdr;
	u64 dis_sched_early_comp :1;
	u64 sch_ena		 :1;
	u64 dis_line_pref	 :1;
	u64 ssow_pf_func	 :13;
	u16 base_pcifunc;
	u8  use_local_lmt_region;
	u64 lmt_iova;
	u64 rsvd[4];
};

/* CPT mailbox error codes
 * Range 901 - 1000.
 */
enum cpt_af_status {
	CPT_AF_ERR_PARAM		= -901,
	CPT_AF_ERR_GRP_INVALID		= -902,
	CPT_AF_ERR_LF_INVALID		= -903,
	CPT_AF_ERR_ACCESS_DENIED	= -904,
	CPT_AF_ERR_SSO_PF_FUNC_INVALID	= -905,
	CPT_AF_ERR_NIX_PF_FUNC_INVALID	= -906,
	CPT_AF_ERR_INLINE_IPSEC_INB_ENA	= -907,
	CPT_AF_ERR_INLINE_IPSEC_OUT_ENA	= -908
};

/* CPT mbox message formats */
struct cpt_rd_wr_reg_msg {
	struct mbox_msghdr hdr;
	u64 reg_offset;
	u64 *ret_val;
	u64 val;
	u8 is_write;
	int blkaddr;
};

struct cpt_lf_alloc_req_msg {
	struct mbox_msghdr hdr;
	u16 nix_pf_func;
	u16 sso_pf_func;
	u16 eng_grpmsk;
	int blkaddr;
	u8 ctx_ilen_valid : 1;
	u8 ctx_ilen : 7;
};

#define CPT_INLINE_INBOUND      0
#define CPT_INLINE_OUTBOUND     1

/* Mailbox message request format for CPT IPsec
 * inline inbound and outbound configuration.
 */
struct cpt_inline_ipsec_cfg_msg {
	struct mbox_msghdr hdr;
	u8 enable;
	u8 slot;
	u8 dir;
	u8 sso_pf_func_ovrd;
	u16 sso_pf_func; /* inbound path SSO_PF_FUNC */
	u16 nix_pf_func; /* outbound path NIX_PF_FUNC */
};

/* Mailbox message request and response format for CPT stats. */
struct cpt_sts_req {
	struct mbox_msghdr hdr;
	u8 blkaddr;
};

struct cpt_sts_rsp {
	struct mbox_msghdr hdr;
	u64 inst_req_pc;
	u64 inst_lat_pc;
	u64 rd_req_pc;
	u64 rd_lat_pc;
	u64 rd_uc_pc;
	u64 active_cycles_pc;
	u64 ctx_mis_pc;
	u64 ctx_hit_pc;
	u64 ctx_aop_pc;
	u64 ctx_aop_lat_pc;
	u64 ctx_ifetch_pc;
	u64 ctx_ifetch_lat_pc;
	u64 ctx_ffetch_pc;
	u64 ctx_ffetch_lat_pc;
	u64 ctx_wback_pc;
	u64 ctx_wback_lat_pc;
	u64 ctx_psh_pc;
	u64 ctx_psh_lat_pc;
	u64 ctx_err;
	u64 ctx_enc_id;
	u64 ctx_flush_timer;
	u64 rxc_time;
	u64 rxc_time_cfg;
	u64 rxc_active_sts;
	u64 rxc_zombie_sts;
	u64 busy_sts_ae;
	u64 free_sts_ae;
	u64 busy_sts_se;
	u64 free_sts_se;
	u64 busy_sts_ie;
	u64 free_sts_ie;
	u64 exe_err_info;
	u64 cptclk_cnt;
	u64 diag;
	u64 rxc_dfrg;
	u64 x2p_link_cfg0;
	u64 x2p_link_cfg1;
};

/* Mailbox message request format to configure reassembly timeout. */
struct cpt_rxc_time_cfg_req {
	struct mbox_msghdr hdr;
	int blkaddr;
	u32 step;
	u16 zombie_thres;
	u16 zombie_limit;
	u16 active_thres;
	u16 active_limit;
};

/* Mailbox message request format to request for CPT_INST_S lmtst. */
struct cpt_inst_lmtst_req {
	struct mbox_msghdr hdr;
	u64 inst[8];
	u64 rsvd;
};

/* Mailbox message format to request for CPT LF reset */
struct cpt_lf_rst_req {
	struct mbox_msghdr hdr;
	u32 slot;
	u32 rsvd;
};

/* Mailbox message format to request for CPT faulted engines */
struct cpt_flt_eng_info_req {
	struct mbox_msghdr hdr;
	int blkaddr;
	bool reset;
	u32 rsvd;
};

struct cpt_flt_eng_info_rsp {
	struct mbox_msghdr hdr;
	u64 flt_eng_map[CPT_10K_AF_INT_VEC_RVU];
	u64 rcvrd_eng_map[CPT_10K_AF_INT_VEC_RVU];
	u64 rsvd;
};

struct sdp_node_info {
	/* Node to which this PF belons to */
	u8 node_id;
	u8 max_vfs;
	u8 num_pf_rings;
	u8 pf_srn;
#define SDP_MAX_VFS	128
	u8 vf_rings[SDP_MAX_VFS];
};

struct sdp_chan_info_msg {
	struct mbox_msghdr hdr;
	struct sdp_node_info info;
};

struct sdp_get_chan_info_msg {
	struct mbox_msghdr hdr;
	u16 chan_base;
	u16 num_chan;
};

/* CGX mailbox error codes
 * Range 1101 - 1200.
 */
enum cgx_af_status {
	LMAC_AF_ERR_INVALID_PARAM	= -1101,
	LMAC_AF_ERR_PF_NOT_MAPPED	= -1102,
	LMAC_AF_ERR_PERM_DENIED		= -1103,
	LMAC_AF_ERR_PFC_ENADIS_PERM_DENIED       = -1104,
	LMAC_AF_ERR_8023PAUSE_ENADIS_PERM_DENIED = -1105,
	LMAC_AF_ERR_CMD_TIMEOUT = -1106,
	LMAC_AF_ERR_FIRMWARE_DATA_NOT_MAPPED = -1107,
	LMAC_AF_ERR_EXACT_MATCH_TBL_ADD_FAILED = -1108,
	LMAC_AF_ERR_EXACT_MATCH_TBL_DEL_FAILED = -1109,
	LMAC_AF_ERR_EXACT_MATCH_TBL_LOOK_UP_FAILED = -1110,
};

enum mcs_direction {
	MCS_RX,
	MCS_TX,
};

enum mcs_rsrc_type {
	MCS_RSRC_TYPE_FLOWID,
	MCS_RSRC_TYPE_SECY,
	MCS_RSRC_TYPE_SC,
	MCS_RSRC_TYPE_SA,
};

struct mcs_alloc_rsrc_req {
	struct mbox_msghdr hdr;
	u8 rsrc_type;
	u8 rsrc_cnt;	/* Resources count */
	u8 mcs_id;	/* MCS block ID	*/
	u8 dir;		/* Macsec ingress or egress side */
	u8 all;		/* Allocate all resource type one each */
	u64 rsvd;
};

struct mcs_alloc_rsrc_rsp {
	struct mbox_msghdr hdr;
	u8 flow_ids[128];	/* Index of reserved entries */
	u8 secy_ids[128];
	u8 sc_ids[128];
	u8 sa_ids[256];
	u8 rsrc_type;
	u8 rsrc_cnt;		/* No of entries reserved */
	u8 mcs_id;
	u8 dir;
	u8 all;
	u8 rsvd[256];		/* reserved fields for future expansion */
};

struct mcs_free_rsrc_req {
	struct mbox_msghdr hdr;
	u8 rsrc_id;		/* Index of the entry to be freed */
	u8 rsrc_type;
	u8 mcs_id;
	u8 dir;
	u8 all;			/* Free all the cam resources */
	u64 rsvd;
};

struct mcs_flowid_entry_write_req {
	struct mbox_msghdr hdr;
	u64 data[4];
	u64 mask[4];
	u64 sci;	/* CNF10K-B for tx_secy_mem_map */
	u8 flow_id;
	u8 secy_id;	/* secyid for which flowid is mapped */
	u8 sc_id;	/* Valid if dir = MCS_TX, SC_CAM id mapped to flowid */
	u8 ena;		/* Enable tcam entry */
	u8 ctrl_pkt;
	u8 mcs_id;
	u8 dir;
	u64 rsvd;
};

struct mcs_secy_plcy_write_req {
	struct mbox_msghdr hdr;
	u64 plcy;
	u8 secy_id;
	u8 mcs_id;
	u8 dir;
	u64 rsvd;
};

/* RX SC_CAM mapping */
struct mcs_rx_sc_cam_write_req {
	struct mbox_msghdr hdr;
	u64 sci;	/* SCI */
	u64 secy_id;	/* secy index mapped to SC */
	u8 sc_id;	/* SC CAM entry index */
	u8 mcs_id;
	u64 rsvd;
};

struct mcs_sa_plcy_write_req {
	struct mbox_msghdr hdr;
	u64 plcy[2][9];		/* Support 2 SA policy */
	u8 sa_index[2];
	u8 sa_cnt;
	u8 mcs_id;
	u8 dir;
	u64 rsvd;
};

struct mcs_tx_sc_sa_map {
	struct mbox_msghdr hdr;
	u8 sa_index0;
	u8 sa_index1;
	u8 rekey_ena;
	u8 sa_index0_vld;
	u8 sa_index1_vld;
	u8 tx_sa_active;
	u64 sectag_sci;
	u8 sc_id;	/* used as index for SA_MEM_MAP */
	u8 mcs_id;
	u64 rsvd;
};

struct mcs_rx_sc_sa_map {
	struct mbox_msghdr hdr;
	u8 sa_index;
	u8 sa_in_use;
	u8 sc_id;
	u8 an;		/* value range 0-3, sc_id + an used as index SA_MEM_MAP */
	u8 mcs_id;
	u64 rsvd;
};

struct mcs_flowid_ena_dis_entry {
	struct mbox_msghdr hdr;
	u8 flow_id;
	u8 ena;
	u8 mcs_id;
	u8 dir;
	u64 rsvd;
};

struct mcs_pn_table_write_req {
	struct mbox_msghdr hdr;
	u64 next_pn;
	u8 pn_id;
	u8 mcs_id;
	u8 dir;
	u64 rsvd;
};

struct mcs_hw_info {
	struct mbox_msghdr hdr;
	u8 num_mcs_blks;	/* Number of MCS blocks */
	u8 tcam_entries;	/* RX/TX Tcam entries per mcs block */
	u8 secy_entries;	/* RX/TX SECY entries per mcs block */
	u8 sc_entries;		/* RX/TX SC CAM entries per mcs block */
	u8 sa_entries;		/* PN table entries = SA entries */
	u64 rsvd[16];
};

struct mcs_set_active_lmac {
	struct mbox_msghdr hdr;
	u32 lmac_bmap;	/* bitmap of active lmac per mcs block */
	u8 mcs_id;
	u16 chan_base; /* MCS channel base */
	u64 rsvd;
};

struct mcs_set_lmac_mode {
	struct mbox_msghdr hdr;
	u8 mode;	/* 1:Bypass 0:Operational */
	u8 lmac_id;
	u8 mcs_id;
	u64 rsvd;
};

struct mcs_port_reset_req {
	struct mbox_msghdr hdr;
	u8 reset;
	u8 mcs_id;
	u8 port_id;
	u64 rsvd;
};

struct mcs_port_cfg_set_req {
	struct mbox_msghdr hdr;
	u8 cstm_tag_rel_mode_sel;
	u8 custom_hdr_enb;
	u8 fifo_skid;
	u8 port_mode;
	u8 port_id;
	u8 mcs_id;
	u64 rsvd;
};

struct mcs_port_cfg_get_req {
	struct mbox_msghdr hdr;
	u8 port_id;
	u8 mcs_id;
	u64 rsvd;
};

struct mcs_port_cfg_get_rsp {
	struct mbox_msghdr hdr;
	u8 cstm_tag_rel_mode_sel;
	u8 custom_hdr_enb;
	u8 fifo_skid;
	u8 port_mode;
	u8 port_id;
	u8 mcs_id;
	u64 rsvd;
};

struct mcs_custom_tag_cfg_get_req {
	struct mbox_msghdr hdr;
	u8 mcs_id;
	u8 dir;
	u64 rsvd;
};

struct mcs_custom_tag_cfg_get_rsp {
	struct mbox_msghdr hdr;
	u16 cstm_etype[8];
	u8 cstm_indx[8];
	u8 cstm_etype_en;
	u8 mcs_id;
	u8 dir;
	u64 rsvd;
};

/* MCS mailbox error codes
 * Range 1201 - 1300.
 */
enum mcs_af_status {
	MCS_AF_ERR_INVALID_MCSID        = -1201,
	MCS_AF_ERR_NOT_MAPPED           = -1202,
};

struct mcs_set_pn_threshold {
	struct mbox_msghdr hdr;
	u64 threshold;
	u8 xpn; /* '1' for setting xpn threshold */
	u8 mcs_id;
	u8 dir;
	u64 rsvd;
};

enum mcs_ctrl_pkt_rulew_type {
	MCS_CTRL_PKT_RULE_TYPE_ETH,
	MCS_CTRL_PKT_RULE_TYPE_DA,
	MCS_CTRL_PKT_RULE_TYPE_RANGE,
	MCS_CTRL_PKT_RULE_TYPE_COMBO,
	MCS_CTRL_PKT_RULE_TYPE_MAC,
};

struct mcs_alloc_ctrl_pkt_rule_req {
	struct mbox_msghdr hdr;
	u8 rule_type;
	u8 mcs_id;	/* MCS block ID	*/
	u8 dir;		/* Macsec ingress or egress side */
	u64 rsvd;
};

struct mcs_alloc_ctrl_pkt_rule_rsp {
	struct mbox_msghdr hdr;
	u8 rule_idx;
	u8 rule_type;
	u8 mcs_id;
	u8 dir;
	u64 rsvd;
};

struct mcs_free_ctrl_pkt_rule_req {
	struct mbox_msghdr hdr;
	u8 rule_idx;
	u8 rule_type;
	u8 mcs_id;
	u8 dir;
	u8 all;
	u64 rsvd;
};

struct mcs_ctrl_pkt_rule_write_req {
	struct mbox_msghdr hdr;
	u64 data0;
	u64 data1;
	u64 data2;
	u8 rule_idx;
	u8 rule_type;
	u8 mcs_id;
	u8 dir;
	u64 rsvd;
};

struct mcs_stats_req {
	struct mbox_msghdr hdr;
	u8 id;
	u8 mcs_id;
	u8 dir;
	u64 rsvd;
};

struct mcs_flowid_stats {
	struct mbox_msghdr hdr;
	u64 tcam_hit_cnt;
	u64 rsvd;
};

struct mcs_secy_stats {
	struct mbox_msghdr hdr;
	u64 ctl_pkt_bcast_cnt;
	u64 ctl_pkt_mcast_cnt;
	u64 ctl_pkt_ucast_cnt;
	u64 ctl_octet_cnt;
	u64 unctl_pkt_bcast_cnt;
	u64 unctl_pkt_mcast_cnt;
	u64 unctl_pkt_ucast_cnt;
	u64 unctl_octet_cnt;
	/* Valid only for RX */
	u64 octet_decrypted_cnt;
	u64 octet_validated_cnt;
	u64 pkt_port_disabled_cnt;
	u64 pkt_badtag_cnt;
	u64 pkt_nosa_cnt;
	u64 pkt_nosaerror_cnt;
	u64 pkt_tagged_ctl_cnt;
	u64 pkt_untaged_cnt;
	u64 pkt_ctl_cnt;	/* CN10K-B */
	u64 pkt_notag_cnt;	/* CNF10K-B */
	/* Valid only for TX */
	u64 octet_encrypted_cnt;
	u64 octet_protected_cnt;
	u64 pkt_noactivesa_cnt;
	u64 pkt_toolong_cnt;
	u64 pkt_untagged_cnt;
	u64 rsvd[4];
};

struct mcs_port_stats {
	struct mbox_msghdr hdr;
	u64 tcam_miss_cnt;
	u64 parser_err_cnt;
	u64 preempt_err_cnt;  /* CNF10K-B */
	u64 sectag_insert_err_cnt;
	u64 rsvd[4];
};

/* Only for CN10K-B */
struct mcs_sa_stats {
	struct mbox_msghdr hdr;
	/* RX */
	u64 pkt_invalid_cnt;
	u64 pkt_nosaerror_cnt;
	u64 pkt_notvalid_cnt;
	u64 pkt_ok_cnt;
	u64 pkt_nosa_cnt;
	/* TX */
	u64 pkt_encrypt_cnt;
	u64 pkt_protected_cnt;
	u64 rsvd[4];
};

struct mcs_sc_stats {
	struct mbox_msghdr hdr;
	/* RX */
	u64 hit_cnt;
	u64 pkt_invalid_cnt;
	u64 pkt_late_cnt;
	u64 pkt_notvalid_cnt;
	u64 pkt_unchecked_cnt;
	u64 pkt_delay_cnt;	/* CNF10K-B */
	u64 pkt_ok_cnt;		/* CNF10K-B */
	u64 octet_decrypt_cnt;	/* CN10K-B */
	u64 octet_validate_cnt;	/* CN10K-B */
	/* TX */
	u64 pkt_encrypt_cnt;
	u64 pkt_protected_cnt;
	u64 octet_encrypt_cnt;		/* CN10K-B */
	u64 octet_protected_cnt;	/* CN10K-B */
	u64 rsvd[4];
};

struct mcs_clear_stats {
	struct mbox_msghdr hdr;
#define MCS_FLOWID_STATS	0
#define MCS_SECY_STATS		1
#define MCS_SC_STATS		2
#define MCS_SA_STATS		3
#define MCS_PORT_STATS		4
	u8 type;	/* FLOWID, SECY, SC, SA, PORT */
	u8 id;		/* type = PORT, If id = FF(invalid) port no is derived from pcifunc */
	u8 mcs_id;
	u8 dir;
	u8 all;		/* All resources stats mapped to PF are cleared */
};

struct mcs_intr_cfg {
	struct mbox_msghdr hdr;
#define MCS_CPM_RX_SECTAG_V_EQ1_INT		BIT_ULL(0)
#define MCS_CPM_RX_SECTAG_E_EQ0_C_EQ1_INT	BIT_ULL(1)
#define MCS_CPM_RX_SECTAG_SL_GTE48_INT		BIT_ULL(2)
#define MCS_CPM_RX_SECTAG_ES_EQ1_SC_EQ1_INT	BIT_ULL(3)
#define MCS_CPM_RX_SECTAG_SC_EQ1_SCB_EQ1_INT	BIT_ULL(4)
#define MCS_CPM_RX_PACKET_XPN_EQ0_INT		BIT_ULL(5)
#define MCS_CPM_RX_PN_THRESH_REACHED_INT	BIT_ULL(6)
#define MCS_CPM_TX_PACKET_XPN_EQ0_INT		BIT_ULL(7)
#define MCS_CPM_TX_PN_THRESH_REACHED_INT	BIT_ULL(8)
#define MCS_CPM_TX_SA_NOT_VALID_INT		BIT_ULL(9)
#define MCS_BBE_RX_DFIFO_OVERFLOW_INT		BIT_ULL(10)
#define MCS_BBE_RX_PLFIFO_OVERFLOW_INT		BIT_ULL(11)
#define MCS_BBE_TX_DFIFO_OVERFLOW_INT		BIT_ULL(12)
#define MCS_BBE_TX_PLFIFO_OVERFLOW_INT		BIT_ULL(13)
#define MCS_PAB_RX_CHAN_OVERFLOW_INT		BIT_ULL(14)
#define MCS_PAB_TX_CHAN_OVERFLOW_INT		BIT_ULL(15)
	u64 intr_mask;		/* Interrupt enable mask */
	u8 mcs_id;
	u8 lmac_id;
	u64 rsvd;
};

struct mcs_intr_info {
	struct mbox_msghdr hdr;
	u64 intr_mask;
	int sa_id;
	u8 mcs_id;
	u8 lmac_id;
	u64 rsvd;
};

#endif /* MBOX_H */
