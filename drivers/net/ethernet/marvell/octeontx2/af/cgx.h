/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell OcteonTx2 CGX driver
 *
 * Copyright (C) 2018 Marvell.
 *
 */

#ifndef CGX_H
#define CGX_H

#include "mbox.h"
#include "cgx_fw_if.h"
#include "rpm.h"

 /* PCI device IDs */
#define	PCI_DEVID_OCTEONTX2_CGX		0xA059

/* PCI BAR nos */
#define PCI_CFG_REG_BAR_NUM		0

#define CGX_ID_MASK			0x7
#define MAX_LMAC_PER_CGX		4
#define MAX_DMAC_ENTRIES_PER_CGX	32
#define CGX_FIFO_LEN			65536 /* 64K for both Rx & Tx */
#define CGX_OFFSET(x)			((x) * MAX_LMAC_PER_CGX)

/* Registers */
#define CGXX_CMRX_CFG			0x00
#define CMR_P2X_SEL_MASK		GENMASK_ULL(61, 59)
#define CMR_P2X_SEL_SHIFT		59ULL
#define CMR_P2X_SEL_NIX0		1ULL
#define CMR_P2X_SEL_NIX1		2ULL
#define CMR_EN				BIT_ULL(55)
#define DATA_PKT_TX_EN			BIT_ULL(53)
#define DATA_PKT_RX_EN			BIT_ULL(54)
#define CGX_LMAC_TYPE_SHIFT		40
#define CGX_LMAC_TYPE_MASK		0xF
#define CGXX_CMRX_INT			0x040
#define FW_CGX_INT			BIT_ULL(1)
#define CGXX_CMRX_INT_ENA_W1S		0x058
#define CGXX_CMRX_RX_ID_MAP		0x060
#define CGXX_CMRX_RX_STAT0		0x070
#define CGXX_CMRX_RX_LMACS		0x128
#define CGXX_CMRX_RX_DMAC_CTL0		(0x1F8 + mac_ops->csr_offset)
#define CGX_DMAC_CTL0_CAM_ENABLE	BIT_ULL(3)
#define CGX_DMAC_CAM_ACCEPT		BIT_ULL(3)
#define CGX_DMAC_MCAST_MODE_CAM		BIT_ULL(2)
#define CGX_DMAC_MCAST_MODE		BIT_ULL(1)
#define CGX_DMAC_BCAST_MODE		BIT_ULL(0)
#define CGXX_CMRX_RX_DMAC_CAM0		(0x200 + mac_ops->csr_offset)
#define CGX_DMAC_CAM_ADDR_ENABLE	BIT_ULL(48)
#define CGX_DMAC_CAM_ENTRY_LMACID	GENMASK_ULL(50, 49)
#define CGXX_CMRX_RX_DMAC_CAM1		0x400
#define CGX_RX_DMAC_ADR_MASK		GENMASK_ULL(47, 0)
#define CGXX_CMRX_TX_STAT0		0x700
#define CGXX_SCRATCH0_REG		0x1050
#define CGXX_SCRATCH1_REG		0x1058
#define CGX_CONST			0x2000
#define CGX_CONST_RXFIFO_SIZE	        GENMASK_ULL(23, 0)
#define CGXX_SPUX_CONTROL1		0x10000
#define CGXX_SPUX_LNX_FEC_CORR_BLOCKS	0x10700
#define CGXX_SPUX_LNX_FEC_UNCORR_BLOCKS	0x10800
#define CGXX_SPUX_RSFEC_CORR		0x10088
#define CGXX_SPUX_RSFEC_UNCORR		0x10090

#define CGXX_SPUX_CONTROL1_LBK		BIT_ULL(14)
#define CGXX_GMP_PCS_MRX_CTL		0x30000
#define CGXX_GMP_PCS_MRX_CTL_LBK	BIT_ULL(14)

#define CGXX_SMUX_RX_FRM_CTL		0x20020
#define CGX_SMUX_RX_FRM_CTL_CTL_BCK	BIT_ULL(3)
#define CGX_SMUX_RX_FRM_CTL_PTP_MODE	BIT_ULL(12)
#define CGXX_GMP_GMI_RXX_FRM_CTL	0x38028
#define CGX_GMP_GMI_RXX_FRM_CTL_CTL_BCK	BIT_ULL(3)
#define CGX_GMP_GMI_RXX_FRM_CTL_PTP_MODE BIT_ULL(12)
#define CGXX_SMUX_TX_CTL		0x20178
#define CGXX_SMUX_TX_PAUSE_PKT_TIME	0x20110
#define CGXX_SMUX_TX_PAUSE_PKT_INTERVAL	0x20120
#define CGXX_SMUX_SMAC                        0x20108
#define CGXX_SMUX_CBFC_CTL                    0x20218
#define CGXX_SMUX_CBFC_CTL_RX_EN             BIT_ULL(0)
#define CGXX_SMUX_CBFC_CTL_TX_EN             BIT_ULL(1)
#define CGXX_SMUX_CBFC_CTL_DRP_EN            BIT_ULL(2)
#define CGXX_SMUX_CBFC_CTL_BCK_EN            BIT_ULL(3)
#define CGX_PFC_CLASS_MASK		     GENMASK_ULL(47, 32)
#define CGXX_GMP_GMI_TX_PAUSE_PKT_TIME	0x38230
#define CGXX_GMP_GMI_TX_PAUSE_PKT_INTERVAL	0x38248
#define CGX_SMUX_TX_CTL_L2P_BP_CONV	BIT_ULL(7)
#define CGXX_CMR_RX_OVR_BP		0x130
#define CGX_CMR_RX_OVR_BP_EN(X)		BIT_ULL(((X) + 8))
#define CGX_CMR_RX_OVR_BP_BP(X)		BIT_ULL(((X) + 4))

#define CGX_COMMAND_REG			CGXX_SCRATCH1_REG
#define CGX_EVENT_REG			CGXX_SCRATCH0_REG
#define CGX_CMD_TIMEOUT			5000 /* msecs */
#define DEFAULT_PAUSE_TIME		0x7FF

#define CGX_LMAC_FWI			0

enum  cgx_nix_stat_type {
	NIX_STATS_RX,
	NIX_STATS_TX,
};

enum LMAC_TYPE {
	LMAC_MODE_SGMII		= 0,
	LMAC_MODE_XAUI		= 1,
	LMAC_MODE_RXAUI		= 2,
	LMAC_MODE_10G_R		= 3,
	LMAC_MODE_40G_R		= 4,
	LMAC_MODE_QSGMII	= 6,
	LMAC_MODE_25G_R		= 7,
	LMAC_MODE_50G_R		= 8,
	LMAC_MODE_100G_R	= 9,
	LMAC_MODE_USXGMII	= 10,
	LMAC_MODE_MAX,
};

struct cgx_link_event {
	struct cgx_link_user_info link_uinfo;
	u8 cgx_id;
	u8 lmac_id;
};

/**
 * struct cgx_event_cb
 * @notify_link_chg:	callback for link change notification
 * @data:	data passed to callback function
 */
struct cgx_event_cb {
	int (*notify_link_chg)(struct cgx_link_event *event, void *data);
	void *data;
};

extern struct pci_driver cgx_driver;

int cgx_get_cgxcnt_max(void);
int cgx_get_cgxid(void *cgxd);
int cgx_get_lmac_cnt(void *cgxd);
void *cgx_get_pdata(int cgx_id);
int cgx_set_pkind(void *cgxd, u8 lmac_id, int pkind);
int cgx_lmac_evh_register(struct cgx_event_cb *cb, void *cgxd, int lmac_id);
int cgx_lmac_evh_unregister(void *cgxd, int lmac_id);
int cgx_get_tx_stats(void *cgxd, int lmac_id, int idx, u64 *tx_stat);
int cgx_get_rx_stats(void *cgxd, int lmac_id, int idx, u64 *rx_stat);
int cgx_lmac_rx_tx_enable(void *cgxd, int lmac_id, bool enable);
int cgx_lmac_tx_enable(void *cgxd, int lmac_id, bool enable);
int cgx_lmac_addr_set(u8 cgx_id, u8 lmac_id, u8 *mac_addr);
int cgx_lmac_addr_reset(u8 cgx_id, u8 lmac_id);
u64 cgx_lmac_addr_get(u8 cgx_id, u8 lmac_id);
int cgx_lmac_addr_add(u8 cgx_id, u8 lmac_id, u8 *mac_addr);
int cgx_lmac_addr_del(u8 cgx_id, u8 lmac_id, u8 index);
int cgx_lmac_addr_max_entries_get(u8 cgx_id, u8 lmac_id);
void cgx_lmac_promisc_config(int cgx_id, int lmac_id, bool enable);
void cgx_lmac_enadis_rx_pause_fwding(void *cgxd, int lmac_id, bool enable);
int cgx_lmac_internal_loopback(void *cgxd, int lmac_id, bool enable);
int cgx_get_link_info(void *cgxd, int lmac_id,
		      struct cgx_link_user_info *linfo);
int cgx_lmac_linkup_start(void *cgxd);
int cgx_get_fwdata_base(u64 *base);
int cgx_lmac_get_pause_frm(void *cgxd, int lmac_id,
			   u8 *tx_pause, u8 *rx_pause);
int cgx_lmac_set_pause_frm(void *cgxd, int lmac_id,
			   u8 tx_pause, u8 rx_pause);
void cgx_lmac_ptp_config(void *cgxd, int lmac_id, bool enable);
u8 cgx_lmac_get_p2x(int cgx_id, int lmac_id);
int cgx_set_fec(u64 fec, int cgx_id, int lmac_id);
int cgx_get_fec_stats(void *cgxd, int lmac_id, struct cgx_fec_stats_rsp *rsp);
int cgx_get_phy_fec_stats(void *cgxd, int lmac_id);
int cgx_set_link_mode(void *cgxd, struct cgx_set_link_mode_args args,
		      int cgx_id, int lmac_id);
u64 cgx_features_get(void *cgxd);
struct mac_ops *get_mac_ops(void *cgxd);
int cgx_get_nr_lmacs(void *cgxd);
u8 cgx_get_lmacid(void *cgxd, u8 lmac_index);
unsigned long cgx_get_lmac_bmap(void *cgxd);
void cgx_lmac_write(int cgx_id, int lmac_id, u64 offset, u64 val);
u64 cgx_lmac_read(int cgx_id, int lmac_id, u64 offset);
int cgx_lmac_addr_update(u8 cgx_id, u8 lmac_id, u8 *mac_addr, u8 index);
u64 cgx_read_dmac_ctrl(void *cgxd, int lmac_id);
u64 cgx_read_dmac_entry(void *cgxd, int index);
int cgx_lmac_pfc_config(void *cgxd, int lmac_id, u8 tx_pause, u8 rx_pause,
			u16 pfc_en);
int cgx_lmac_get_pfc_frm_cfg(void *cgxd, int lmac_id, u8 *tx_pause,
			     u8 *rx_pause);
int verify_lmac_fc_cfg(void *cgxd, int lmac_id, u8 tx_pause, u8 rx_pause,
		       int pfvf_idx);
#endif /* CGX_H */
