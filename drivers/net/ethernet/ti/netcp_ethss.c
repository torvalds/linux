// SPDX-License-Identifier: GPL-2.0
/*
 * Keystone GBE and XGBE subsystem code
 *
 * Copyright (C) 2014 Texas Instruments Incorporated
 * Authors:	Sandeep Nair <sandeep_n@ti.com>
 *		Sandeep Paulraj <s-paulraj@ti.com>
 *		Cyril Chemparathy <cyril@ti.com>
 *		Santosh Shilimkar <santosh.shilimkar@ti.com>
 *		Wingman Kwok <w-kwok2@ti.com>
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_address.h>
#include <linux/if_vlan.h>
#include <linux/ptp_classify.h>
#include <linux/net_tstamp.h>
#include <linux/ethtool.h>

#include "cpsw.h"
#include "cpsw_ale.h"
#include "netcp.h"
#include "cpts.h"

#define NETCP_DRIVER_NAME		"TI KeyStone Ethernet Driver"
#define NETCP_DRIVER_VERSION		"v1.0"

#define GBE_IDENT(reg)			((reg >> 16) & 0xffff)
#define GBE_MAJOR_VERSION(reg)		(reg >> 8 & 0x7)
#define GBE_MINOR_VERSION(reg)		(reg & 0xff)
#define GBE_RTL_VERSION(reg)		((reg >> 11) & 0x1f)

/* 1G Ethernet SS defines */
#define GBE_MODULE_NAME			"netcp-gbe"
#define GBE_SS_VERSION_14		0x4ed2

#define GBE_SS_REG_INDEX		0
#define GBE_SGMII34_REG_INDEX		1
#define GBE_SM_REG_INDEX		2
/* offset relative to base of GBE_SS_REG_INDEX */
#define GBE13_SGMII_MODULE_OFFSET	0x100
/* offset relative to base of GBE_SM_REG_INDEX */
#define GBE13_HOST_PORT_OFFSET		0x34
#define GBE13_SLAVE_PORT_OFFSET		0x60
#define GBE13_EMAC_OFFSET		0x100
#define GBE13_SLAVE_PORT2_OFFSET	0x200
#define GBE13_HW_STATS_OFFSET		0x300
#define GBE13_CPTS_OFFSET		0x500
#define GBE13_ALE_OFFSET		0x600
#define GBE13_HOST_PORT_NUM		0
#define GBE13_NUM_ALE_ENTRIES		1024

/* 1G Ethernet NU SS defines */
#define GBENU_MODULE_NAME		"netcp-gbenu"
#define GBE_SS_ID_NU			0x4ee6
#define GBE_SS_ID_2U			0x4ee8

#define IS_SS_ID_MU(d) \
	((GBE_IDENT((d)->ss_version) == GBE_SS_ID_NU) || \
	 (GBE_IDENT((d)->ss_version) == GBE_SS_ID_2U))

#define IS_SS_ID_NU(d) \
	(GBE_IDENT((d)->ss_version) == GBE_SS_ID_NU)

#define IS_SS_ID_VER_14(d) \
	(GBE_IDENT((d)->ss_version) == GBE_SS_VERSION_14)
#define IS_SS_ID_2U(d) \
	(GBE_IDENT((d)->ss_version) == GBE_SS_ID_2U)

#define GBENU_SS_REG_INDEX		0
#define GBENU_SM_REG_INDEX		1
#define GBENU_SGMII_MODULE_OFFSET	0x100
#define GBENU_HOST_PORT_OFFSET		0x1000
#define GBENU_SLAVE_PORT_OFFSET		0x2000
#define GBENU_EMAC_OFFSET		0x2330
#define GBENU_HW_STATS_OFFSET		0x1a000
#define GBENU_CPTS_OFFSET		0x1d000
#define GBENU_ALE_OFFSET		0x1e000
#define GBENU_HOST_PORT_NUM		0
#define GBENU_SGMII_MODULE_SIZE		0x100

/* 10G Ethernet SS defines */
#define XGBE_MODULE_NAME		"netcp-xgbe"
#define XGBE_SS_VERSION_10		0x4ee4

#define XGBE_SS_REG_INDEX		0
#define XGBE_SM_REG_INDEX		1
#define XGBE_SERDES_REG_INDEX		2

/* offset relative to base of XGBE_SS_REG_INDEX */
#define XGBE10_SGMII_MODULE_OFFSET	0x100
#define IS_SS_ID_XGBE(d)		((d)->ss_version == XGBE_SS_VERSION_10)
/* offset relative to base of XGBE_SM_REG_INDEX */
#define XGBE10_HOST_PORT_OFFSET		0x34
#define XGBE10_SLAVE_PORT_OFFSET	0x64
#define XGBE10_EMAC_OFFSET		0x400
#define XGBE10_CPTS_OFFSET		0x600
#define XGBE10_ALE_OFFSET		0x700
#define XGBE10_HW_STATS_OFFSET		0x800
#define XGBE10_HOST_PORT_NUM		0
#define XGBE10_NUM_ALE_ENTRIES		2048

#define	GBE_TIMER_INTERVAL			(HZ / 2)

/* Soft reset register values */
#define SOFT_RESET_MASK				BIT(0)
#define SOFT_RESET				BIT(0)
#define DEVICE_EMACSL_RESET_POLL_COUNT		100
#define GMACSL_RET_WARN_RESET_INCOMPLETE	-2

#define MACSL_RX_ENABLE_CSF			BIT(23)
#define MACSL_ENABLE_EXT_CTL			BIT(18)
#define MACSL_XGMII_ENABLE			BIT(13)
#define MACSL_XGIG_MODE				BIT(8)
#define MACSL_GIG_MODE				BIT(7)
#define MACSL_GMII_ENABLE			BIT(5)
#define MACSL_FULLDUPLEX			BIT(0)

#define GBE_CTL_P0_ENABLE			BIT(2)
#define ETH_SW_CTL_P0_TX_CRC_REMOVE		BIT(13)
#define GBE13_REG_VAL_STAT_ENABLE_ALL		0xff
#define XGBE_REG_VAL_STAT_ENABLE_ALL		0xf
#define GBE_STATS_CD_SEL			BIT(28)

#define GBE_PORT_MASK(x)			(BIT(x) - 1)
#define GBE_MASK_NO_PORTS			0

#define GBE_DEF_1G_MAC_CONTROL					\
		(MACSL_GIG_MODE | MACSL_GMII_ENABLE |		\
		 MACSL_ENABLE_EXT_CTL |	MACSL_RX_ENABLE_CSF)

#define GBE_DEF_10G_MAC_CONTROL				\
		(MACSL_XGIG_MODE | MACSL_XGMII_ENABLE |		\
		 MACSL_ENABLE_EXT_CTL |	MACSL_RX_ENABLE_CSF)

#define GBE_STATSA_MODULE			0
#define GBE_STATSB_MODULE			1
#define GBE_STATSC_MODULE			2
#define GBE_STATSD_MODULE			3

#define GBENU_STATS0_MODULE			0
#define GBENU_STATS1_MODULE			1
#define GBENU_STATS2_MODULE			2
#define GBENU_STATS3_MODULE			3
#define GBENU_STATS4_MODULE			4
#define GBENU_STATS5_MODULE			5
#define GBENU_STATS6_MODULE			6
#define GBENU_STATS7_MODULE			7
#define GBENU_STATS8_MODULE			8

#define XGBE_STATS0_MODULE			0
#define XGBE_STATS1_MODULE			1
#define XGBE_STATS2_MODULE			2

/* s: 0-based slave_port */
#define SGMII_BASE(d, s) \
	(((s) < 2) ? (d)->sgmii_port_regs : (d)->sgmii_port34_regs)

#define GBE_TX_QUEUE				648
#define	GBE_TXHOOK_ORDER			0
#define	GBE_RXHOOK_ORDER			0
#define GBE_DEFAULT_ALE_AGEOUT			30
#define SLAVE_LINK_IS_XGMII(s) ((s)->link_interface >= XGMII_LINK_MAC_PHY)
#define SLAVE_LINK_IS_RGMII(s) \
	(((s)->link_interface >= RGMII_LINK_MAC_PHY) && \
	 ((s)->link_interface <= RGMII_LINK_MAC_PHY_NO_MDIO))
#define SLAVE_LINK_IS_SGMII(s) \
	((s)->link_interface <= SGMII_LINK_MAC_PHY_NO_MDIO)
#define NETCP_LINK_STATE_INVALID		-1

#define GBE_SET_REG_OFS(p, rb, rn) p->rb##_ofs.rn = \
		offsetof(struct gbe##_##rb, rn)
#define GBENU_SET_REG_OFS(p, rb, rn) p->rb##_ofs.rn = \
		offsetof(struct gbenu##_##rb, rn)
#define XGBE_SET_REG_OFS(p, rb, rn) p->rb##_ofs.rn = \
		offsetof(struct xgbe##_##rb, rn)
#define GBE_REG_ADDR(p, rb, rn) (p->rb + p->rb##_ofs.rn)

#define HOST_TX_PRI_MAP_DEFAULT			0x00000000

#if IS_ENABLED(CONFIG_TI_CPTS)
/* Px_TS_CTL register fields */
#define TS_RX_ANX_F_EN				BIT(0)
#define TS_RX_VLAN_LT1_EN			BIT(1)
#define TS_RX_VLAN_LT2_EN			BIT(2)
#define TS_RX_ANX_D_EN				BIT(3)
#define TS_TX_ANX_F_EN				BIT(4)
#define TS_TX_VLAN_LT1_EN			BIT(5)
#define TS_TX_VLAN_LT2_EN			BIT(6)
#define TS_TX_ANX_D_EN				BIT(7)
#define TS_LT2_EN				BIT(8)
#define TS_RX_ANX_E_EN				BIT(9)
#define TS_TX_ANX_E_EN				BIT(10)
#define TS_MSG_TYPE_EN_SHIFT			16
#define TS_MSG_TYPE_EN_MASK			0xffff

/* Px_TS_SEQ_LTYPE register fields */
#define TS_SEQ_ID_OFS_SHIFT			16
#define TS_SEQ_ID_OFS_MASK			0x3f

/* Px_TS_CTL_LTYPE2 register fields */
#define TS_107					BIT(16)
#define TS_129					BIT(17)
#define TS_130					BIT(18)
#define TS_131					BIT(19)
#define TS_132					BIT(20)
#define TS_319					BIT(21)
#define TS_320					BIT(22)
#define TS_TTL_NONZERO				BIT(23)
#define TS_UNI_EN				BIT(24)
#define TS_UNI_EN_SHIFT				24

#define TS_TX_ANX_ALL_EN	 \
	(TS_TX_ANX_D_EN	| TS_TX_ANX_E_EN | TS_TX_ANX_F_EN)

#define TS_RX_ANX_ALL_EN	 \
	(TS_RX_ANX_D_EN	| TS_RX_ANX_E_EN | TS_RX_ANX_F_EN)

#define TS_CTL_DST_PORT				TS_319
#define TS_CTL_DST_PORT_SHIFT			21

#define TS_CTL_MADDR_ALL	\
	(TS_107 | TS_129 | TS_130 | TS_131 | TS_132)

#define TS_CTL_MADDR_SHIFT			16

/* The PTP event messages - Sync, Delay_Req, Pdelay_Req, and Pdelay_Resp. */
#define EVENT_MSG_BITS (BIT(0) | BIT(1) | BIT(2) | BIT(3))
#endif /* CONFIG_TI_CPTS */

struct xgbe_ss_regs {
	u32	id_ver;
	u32	synce_count;
	u32	synce_mux;
	u32	control;
};

struct xgbe_switch_regs {
	u32	id_ver;
	u32	control;
	u32	emcontrol;
	u32	stat_port_en;
	u32	ptype;
	u32	soft_idle;
	u32	thru_rate;
	u32	gap_thresh;
	u32	tx_start_wds;
	u32	flow_control;
	u32	cppi_thresh;
};

struct xgbe_port_regs {
	u32	blk_cnt;
	u32	port_vlan;
	u32	tx_pri_map;
	u32	sa_lo;
	u32	sa_hi;
	u32	ts_ctl;
	u32	ts_seq_ltype;
	u32	ts_vlan;
	u32	ts_ctl_ltype2;
	u32	ts_ctl2;
	u32	control;
};

struct xgbe_host_port_regs {
	u32	blk_cnt;
	u32	port_vlan;
	u32	tx_pri_map;
	u32	src_id;
	u32	rx_pri_map;
	u32	rx_maxlen;
};

struct xgbe_emac_regs {
	u32	id_ver;
	u32	mac_control;
	u32	mac_status;
	u32	soft_reset;
	u32	rx_maxlen;
	u32	__reserved_0;
	u32	rx_pause;
	u32	tx_pause;
	u32	em_control;
	u32	__reserved_1;
	u32	tx_gap;
	u32	rsvd[4];
};

struct xgbe_host_hw_stats {
	u32	rx_good_frames;
	u32	rx_broadcast_frames;
	u32	rx_multicast_frames;
	u32	__rsvd_0[3];
	u32	rx_oversized_frames;
	u32	__rsvd_1;
	u32	rx_undersized_frames;
	u32	__rsvd_2;
	u32	overrun_type4;
	u32	overrun_type5;
	u32	rx_bytes;
	u32	tx_good_frames;
	u32	tx_broadcast_frames;
	u32	tx_multicast_frames;
	u32	__rsvd_3[9];
	u32	tx_bytes;
	u32	tx_64byte_frames;
	u32	tx_65_to_127byte_frames;
	u32	tx_128_to_255byte_frames;
	u32	tx_256_to_511byte_frames;
	u32	tx_512_to_1023byte_frames;
	u32	tx_1024byte_frames;
	u32	net_bytes;
	u32	rx_sof_overruns;
	u32	rx_mof_overruns;
	u32	rx_dma_overruns;
};

struct xgbe_hw_stats {
	u32	rx_good_frames;
	u32	rx_broadcast_frames;
	u32	rx_multicast_frames;
	u32	rx_pause_frames;
	u32	rx_crc_errors;
	u32	rx_align_code_errors;
	u32	rx_oversized_frames;
	u32	rx_jabber_frames;
	u32	rx_undersized_frames;
	u32	rx_fragments;
	u32	overrun_type4;
	u32	overrun_type5;
	u32	rx_bytes;
	u32	tx_good_frames;
	u32	tx_broadcast_frames;
	u32	tx_multicast_frames;
	u32	tx_pause_frames;
	u32	tx_deferred_frames;
	u32	tx_collision_frames;
	u32	tx_single_coll_frames;
	u32	tx_mult_coll_frames;
	u32	tx_excessive_collisions;
	u32	tx_late_collisions;
	u32	tx_underrun;
	u32	tx_carrier_sense_errors;
	u32	tx_bytes;
	u32	tx_64byte_frames;
	u32	tx_65_to_127byte_frames;
	u32	tx_128_to_255byte_frames;
	u32	tx_256_to_511byte_frames;
	u32	tx_512_to_1023byte_frames;
	u32	tx_1024byte_frames;
	u32	net_bytes;
	u32	rx_sof_overruns;
	u32	rx_mof_overruns;
	u32	rx_dma_overruns;
};

struct gbenu_ss_regs {
	u32	id_ver;
	u32	synce_count;		/* NU */
	u32	synce_mux;		/* NU */
	u32	control;		/* 2U */
	u32	__rsvd_0[2];		/* 2U */
	u32	rgmii_status;		/* 2U */
	u32	ss_status;		/* 2U */
};

struct gbenu_switch_regs {
	u32	id_ver;
	u32	control;
	u32	__rsvd_0[2];
	u32	emcontrol;
	u32	stat_port_en;
	u32	ptype;			/* NU */
	u32	soft_idle;
	u32	thru_rate;		/* NU */
	u32	gap_thresh;		/* NU */
	u32	tx_start_wds;		/* NU */
	u32	eee_prescale;		/* 2U */
	u32	tx_g_oflow_thresh_set;	/* NU */
	u32	tx_g_oflow_thresh_clr;	/* NU */
	u32	tx_g_buf_thresh_set_l;	/* NU */
	u32	tx_g_buf_thresh_set_h;	/* NU */
	u32	tx_g_buf_thresh_clr_l;	/* NU */
	u32	tx_g_buf_thresh_clr_h;	/* NU */
};

struct gbenu_port_regs {
	u32	__rsvd_0;
	u32	control;
	u32	max_blks;		/* 2U */
	u32	mem_align1;
	u32	blk_cnt;
	u32	port_vlan;
	u32	tx_pri_map;		/* NU */
	u32	pri_ctl;		/* 2U */
	u32	rx_pri_map;
	u32	rx_maxlen;
	u32	tx_blks_pri;		/* NU */
	u32	__rsvd_1;
	u32	idle2lpi;		/* 2U */
	u32	lpi2idle;		/* 2U */
	u32	eee_status;		/* 2U */
	u32	__rsvd_2;
	u32	__rsvd_3[176];		/* NU: more to add */
	u32	__rsvd_4[2];
	u32	sa_lo;
	u32	sa_hi;
	u32	ts_ctl;
	u32	ts_seq_ltype;
	u32	ts_vlan;
	u32	ts_ctl_ltype2;
	u32	ts_ctl2;
};

struct gbenu_host_port_regs {
	u32	__rsvd_0;
	u32	control;
	u32	flow_id_offset;		/* 2U */
	u32	__rsvd_1;
	u32	blk_cnt;
	u32	port_vlan;
	u32	tx_pri_map;		/* NU */
	u32	pri_ctl;
	u32	rx_pri_map;
	u32	rx_maxlen;
	u32	tx_blks_pri;		/* NU */
	u32	__rsvd_2;
	u32	idle2lpi;		/* 2U */
	u32	lpi2wake;		/* 2U */
	u32	eee_status;		/* 2U */
	u32	__rsvd_3;
	u32	__rsvd_4[184];		/* NU */
	u32	host_blks_pri;		/* NU */
};

struct gbenu_emac_regs {
	u32	mac_control;
	u32	mac_status;
	u32	soft_reset;
	u32	boff_test;
	u32	rx_pause;
	u32	__rsvd_0[11];		/* NU */
	u32	tx_pause;
	u32	__rsvd_1[11];		/* NU */
	u32	em_control;
	u32	tx_gap;
};

/* Some hw stat regs are applicable to slave port only.
 * This is handled by gbenu_et_stats struct.  Also some
 * are for SS version NU and some are for 2U.
 */
struct gbenu_hw_stats {
	u32	rx_good_frames;
	u32	rx_broadcast_frames;
	u32	rx_multicast_frames;
	u32	rx_pause_frames;		/* slave */
	u32	rx_crc_errors;
	u32	rx_align_code_errors;		/* slave */
	u32	rx_oversized_frames;
	u32	rx_jabber_frames;		/* slave */
	u32	rx_undersized_frames;
	u32	rx_fragments;			/* slave */
	u32	ale_drop;
	u32	ale_overrun_drop;
	u32	rx_bytes;
	u32	tx_good_frames;
	u32	tx_broadcast_frames;
	u32	tx_multicast_frames;
	u32	tx_pause_frames;		/* slave */
	u32	tx_deferred_frames;		/* slave */
	u32	tx_collision_frames;		/* slave */
	u32	tx_single_coll_frames;		/* slave */
	u32	tx_mult_coll_frames;		/* slave */
	u32	tx_excessive_collisions;	/* slave */
	u32	tx_late_collisions;		/* slave */
	u32	rx_ipg_error;			/* slave 10G only */
	u32	tx_carrier_sense_errors;	/* slave */
	u32	tx_bytes;
	u32	tx_64B_frames;
	u32	tx_65_to_127B_frames;
	u32	tx_128_to_255B_frames;
	u32	tx_256_to_511B_frames;
	u32	tx_512_to_1023B_frames;
	u32	tx_1024B_frames;
	u32	net_bytes;
	u32	rx_bottom_fifo_drop;
	u32	rx_port_mask_drop;
	u32	rx_top_fifo_drop;
	u32	ale_rate_limit_drop;
	u32	ale_vid_ingress_drop;
	u32	ale_da_eq_sa_drop;
	u32	__rsvd_0[3];
	u32	ale_unknown_ucast;
	u32	ale_unknown_ucast_bytes;
	u32	ale_unknown_mcast;
	u32	ale_unknown_mcast_bytes;
	u32	ale_unknown_bcast;
	u32	ale_unknown_bcast_bytes;
	u32	ale_pol_match;
	u32	ale_pol_match_red;		/* NU */
	u32	ale_pol_match_yellow;		/* NU */
	u32	__rsvd_1[44];
	u32	tx_mem_protect_err;
	/* following NU only */
	u32	tx_pri0;
	u32	tx_pri1;
	u32	tx_pri2;
	u32	tx_pri3;
	u32	tx_pri4;
	u32	tx_pri5;
	u32	tx_pri6;
	u32	tx_pri7;
	u32	tx_pri0_bcnt;
	u32	tx_pri1_bcnt;
	u32	tx_pri2_bcnt;
	u32	tx_pri3_bcnt;
	u32	tx_pri4_bcnt;
	u32	tx_pri5_bcnt;
	u32	tx_pri6_bcnt;
	u32	tx_pri7_bcnt;
	u32	tx_pri0_drop;
	u32	tx_pri1_drop;
	u32	tx_pri2_drop;
	u32	tx_pri3_drop;
	u32	tx_pri4_drop;
	u32	tx_pri5_drop;
	u32	tx_pri6_drop;
	u32	tx_pri7_drop;
	u32	tx_pri0_drop_bcnt;
	u32	tx_pri1_drop_bcnt;
	u32	tx_pri2_drop_bcnt;
	u32	tx_pri3_drop_bcnt;
	u32	tx_pri4_drop_bcnt;
	u32	tx_pri5_drop_bcnt;
	u32	tx_pri6_drop_bcnt;
	u32	tx_pri7_drop_bcnt;
};

#define GBENU_HW_STATS_REG_MAP_SZ	0x200

struct gbe_ss_regs {
	u32	id_ver;
	u32	synce_count;
	u32	synce_mux;
};

struct gbe_ss_regs_ofs {
	u16	id_ver;
	u16	control;
	u16	rgmii_status; /* 2U */
};

struct gbe_switch_regs {
	u32	id_ver;
	u32	control;
	u32	soft_reset;
	u32	stat_port_en;
	u32	ptype;
	u32	soft_idle;
	u32	thru_rate;
	u32	gap_thresh;
	u32	tx_start_wds;
	u32	flow_control;
};

struct gbe_switch_regs_ofs {
	u16	id_ver;
	u16	control;
	u16	soft_reset;
	u16	emcontrol;
	u16	stat_port_en;
	u16	ptype;
	u16	flow_control;
};

struct gbe_port_regs {
	u32	max_blks;
	u32	blk_cnt;
	u32	port_vlan;
	u32	tx_pri_map;
	u32	sa_lo;
	u32	sa_hi;
	u32	ts_ctl;
	u32	ts_seq_ltype;
	u32	ts_vlan;
	u32	ts_ctl_ltype2;
	u32	ts_ctl2;
};

struct gbe_port_regs_ofs {
	u16	port_vlan;
	u16	tx_pri_map;
	u16     rx_pri_map;
	u16	sa_lo;
	u16	sa_hi;
	u16	ts_ctl;
	u16	ts_seq_ltype;
	u16	ts_vlan;
	u16	ts_ctl_ltype2;
	u16	ts_ctl2;
	u16	rx_maxlen;	/* 2U, NU */
};

struct gbe_host_port_regs {
	u32	src_id;
	u32	port_vlan;
	u32	rx_pri_map;
	u32	rx_maxlen;
};

struct gbe_host_port_regs_ofs {
	u16	port_vlan;
	u16	tx_pri_map;
	u16	rx_maxlen;
};

struct gbe_emac_regs {
	u32	id_ver;
	u32	mac_control;
	u32	mac_status;
	u32	soft_reset;
	u32	rx_maxlen;
	u32	__reserved_0;
	u32	rx_pause;
	u32	tx_pause;
	u32	__reserved_1;
	u32	rx_pri_map;
	u32	rsvd[6];
};

struct gbe_emac_regs_ofs {
	u16	mac_control;
	u16	soft_reset;
	u16	rx_maxlen;
};

struct gbe_hw_stats {
	u32	rx_good_frames;
	u32	rx_broadcast_frames;
	u32	rx_multicast_frames;
	u32	rx_pause_frames;
	u32	rx_crc_errors;
	u32	rx_align_code_errors;
	u32	rx_oversized_frames;
	u32	rx_jabber_frames;
	u32	rx_undersized_frames;
	u32	rx_fragments;
	u32	__pad_0[2];
	u32	rx_bytes;
	u32	tx_good_frames;
	u32	tx_broadcast_frames;
	u32	tx_multicast_frames;
	u32	tx_pause_frames;
	u32	tx_deferred_frames;
	u32	tx_collision_frames;
	u32	tx_single_coll_frames;
	u32	tx_mult_coll_frames;
	u32	tx_excessive_collisions;
	u32	tx_late_collisions;
	u32	tx_underrun;
	u32	tx_carrier_sense_errors;
	u32	tx_bytes;
	u32	tx_64byte_frames;
	u32	tx_65_to_127byte_frames;
	u32	tx_128_to_255byte_frames;
	u32	tx_256_to_511byte_frames;
	u32	tx_512_to_1023byte_frames;
	u32	tx_1024byte_frames;
	u32	net_bytes;
	u32	rx_sof_overruns;
	u32	rx_mof_overruns;
	u32	rx_dma_overruns;
};

#define GBE_MAX_HW_STAT_MODS			9
#define GBE_HW_STATS_REG_MAP_SZ			0x100

struct ts_ctl {
	int     uni;
	u8      dst_port_map;
	u8      maddr_map;
	u8      ts_mcast_type;
};

struct gbe_slave {
	void __iomem			*port_regs;
	void __iomem			*emac_regs;
	struct gbe_port_regs_ofs	port_regs_ofs;
	struct gbe_emac_regs_ofs	emac_regs_ofs;
	int				slave_num; /* 0 based logical number */
	int				port_num;  /* actual port number */
	atomic_t			link_state;
	bool				open;
	struct phy_device		*phy;
	u32				link_interface;
	u32				mac_control;
	u8				phy_port_t;
	struct device_node		*node;
	struct device_node		*phy_node;
	struct ts_ctl                   ts_ctl;
	struct list_head		slave_list;
};

struct gbe_priv {
	struct device			*dev;
	struct netcp_device		*netcp_device;
	struct timer_list		timer;
	u32				num_slaves;
	u32				ale_entries;
	u32				ale_ports;
	bool				enable_ale;
	u8				max_num_slaves;
	u8				max_num_ports; /* max_num_slaves + 1 */
	u8				num_stats_mods;
	struct netcp_tx_pipe		tx_pipe;

	int				host_port;
	u32				rx_packet_max;
	u32				ss_version;
	u32				stats_en_mask;

	void __iomem			*ss_regs;
	void __iomem			*switch_regs;
	void __iomem			*host_port_regs;
	void __iomem			*ale_reg;
	void __iomem                    *cpts_reg;
	void __iomem			*sgmii_port_regs;
	void __iomem			*sgmii_port34_regs;
	void __iomem			*xgbe_serdes_regs;
	void __iomem			*hw_stats_regs[GBE_MAX_HW_STAT_MODS];

	struct gbe_ss_regs_ofs		ss_regs_ofs;
	struct gbe_switch_regs_ofs	switch_regs_ofs;
	struct gbe_host_port_regs_ofs	host_port_regs_ofs;

	struct cpsw_ale			*ale;
	unsigned int			tx_queue_id;
	const char			*dma_chan_name;

	struct list_head		gbe_intf_head;
	struct list_head		secondary_slaves;
	struct net_device		*dummy_ndev;

	u64				*hw_stats;
	u32				*hw_stats_prev;
	const struct netcp_ethtool_stat *et_stats;
	int				num_et_stats;
	/*  Lock for updating the hwstats */
	spinlock_t			hw_stats_lock;

	int                             cpts_registered;
	struct cpts                     *cpts;
	int				rx_ts_enabled;
	int				tx_ts_enabled;
};

struct gbe_intf {
	struct net_device	*ndev;
	struct device		*dev;
	struct gbe_priv		*gbe_dev;
	struct netcp_tx_pipe	tx_pipe;
	struct gbe_slave	*slave;
	struct list_head	gbe_intf_list;
	unsigned long		active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
};

static struct netcp_module gbe_module;
static struct netcp_module xgbe_module;

/* Statistic management */
struct netcp_ethtool_stat {
	char desc[ETH_GSTRING_LEN];
	int type;
	u32 size;
	int offset;
};

#define GBE_STATSA_INFO(field)						\
{									\
	"GBE_A:"#field, GBE_STATSA_MODULE,				\
	FIELD_SIZEOF(struct gbe_hw_stats, field),			\
	offsetof(struct gbe_hw_stats, field)				\
}

#define GBE_STATSB_INFO(field)						\
{									\
	"GBE_B:"#field, GBE_STATSB_MODULE,				\
	FIELD_SIZEOF(struct gbe_hw_stats, field),			\
	offsetof(struct gbe_hw_stats, field)				\
}

#define GBE_STATSC_INFO(field)						\
{									\
	"GBE_C:"#field, GBE_STATSC_MODULE,				\
	FIELD_SIZEOF(struct gbe_hw_stats, field),			\
	offsetof(struct gbe_hw_stats, field)				\
}

#define GBE_STATSD_INFO(field)						\
{									\
	"GBE_D:"#field, GBE_STATSD_MODULE,				\
	FIELD_SIZEOF(struct gbe_hw_stats, field),			\
	offsetof(struct gbe_hw_stats, field)				\
}

static const struct netcp_ethtool_stat gbe13_et_stats[] = {
	/* GBE module A */
	GBE_STATSA_INFO(rx_good_frames),
	GBE_STATSA_INFO(rx_broadcast_frames),
	GBE_STATSA_INFO(rx_multicast_frames),
	GBE_STATSA_INFO(rx_pause_frames),
	GBE_STATSA_INFO(rx_crc_errors),
	GBE_STATSA_INFO(rx_align_code_errors),
	GBE_STATSA_INFO(rx_oversized_frames),
	GBE_STATSA_INFO(rx_jabber_frames),
	GBE_STATSA_INFO(rx_undersized_frames),
	GBE_STATSA_INFO(rx_fragments),
	GBE_STATSA_INFO(rx_bytes),
	GBE_STATSA_INFO(tx_good_frames),
	GBE_STATSA_INFO(tx_broadcast_frames),
	GBE_STATSA_INFO(tx_multicast_frames),
	GBE_STATSA_INFO(tx_pause_frames),
	GBE_STATSA_INFO(tx_deferred_frames),
	GBE_STATSA_INFO(tx_collision_frames),
	GBE_STATSA_INFO(tx_single_coll_frames),
	GBE_STATSA_INFO(tx_mult_coll_frames),
	GBE_STATSA_INFO(tx_excessive_collisions),
	GBE_STATSA_INFO(tx_late_collisions),
	GBE_STATSA_INFO(tx_underrun),
	GBE_STATSA_INFO(tx_carrier_sense_errors),
	GBE_STATSA_INFO(tx_bytes),
	GBE_STATSA_INFO(tx_64byte_frames),
	GBE_STATSA_INFO(tx_65_to_127byte_frames),
	GBE_STATSA_INFO(tx_128_to_255byte_frames),
	GBE_STATSA_INFO(tx_256_to_511byte_frames),
	GBE_STATSA_INFO(tx_512_to_1023byte_frames),
	GBE_STATSA_INFO(tx_1024byte_frames),
	GBE_STATSA_INFO(net_bytes),
	GBE_STATSA_INFO(rx_sof_overruns),
	GBE_STATSA_INFO(rx_mof_overruns),
	GBE_STATSA_INFO(rx_dma_overruns),
	/* GBE module B */
	GBE_STATSB_INFO(rx_good_frames),
	GBE_STATSB_INFO(rx_broadcast_frames),
	GBE_STATSB_INFO(rx_multicast_frames),
	GBE_STATSB_INFO(rx_pause_frames),
	GBE_STATSB_INFO(rx_crc_errors),
	GBE_STATSB_INFO(rx_align_code_errors),
	GBE_STATSB_INFO(rx_oversized_frames),
	GBE_STATSB_INFO(rx_jabber_frames),
	GBE_STATSB_INFO(rx_undersized_frames),
	GBE_STATSB_INFO(rx_fragments),
	GBE_STATSB_INFO(rx_bytes),
	GBE_STATSB_INFO(tx_good_frames),
	GBE_STATSB_INFO(tx_broadcast_frames),
	GBE_STATSB_INFO(tx_multicast_frames),
	GBE_STATSB_INFO(tx_pause_frames),
	GBE_STATSB_INFO(tx_deferred_frames),
	GBE_STATSB_INFO(tx_collision_frames),
	GBE_STATSB_INFO(tx_single_coll_frames),
	GBE_STATSB_INFO(tx_mult_coll_frames),
	GBE_STATSB_INFO(tx_excessive_collisions),
	GBE_STATSB_INFO(tx_late_collisions),
	GBE_STATSB_INFO(tx_underrun),
	GBE_STATSB_INFO(tx_carrier_sense_errors),
	GBE_STATSB_INFO(tx_bytes),
	GBE_STATSB_INFO(tx_64byte_frames),
	GBE_STATSB_INFO(tx_65_to_127byte_frames),
	GBE_STATSB_INFO(tx_128_to_255byte_frames),
	GBE_STATSB_INFO(tx_256_to_511byte_frames),
	GBE_STATSB_INFO(tx_512_to_1023byte_frames),
	GBE_STATSB_INFO(tx_1024byte_frames),
	GBE_STATSB_INFO(net_bytes),
	GBE_STATSB_INFO(rx_sof_overruns),
	GBE_STATSB_INFO(rx_mof_overruns),
	GBE_STATSB_INFO(rx_dma_overruns),
	/* GBE module C */
	GBE_STATSC_INFO(rx_good_frames),
	GBE_STATSC_INFO(rx_broadcast_frames),
	GBE_STATSC_INFO(rx_multicast_frames),
	GBE_STATSC_INFO(rx_pause_frames),
	GBE_STATSC_INFO(rx_crc_errors),
	GBE_STATSC_INFO(rx_align_code_errors),
	GBE_STATSC_INFO(rx_oversized_frames),
	GBE_STATSC_INFO(rx_jabber_frames),
	GBE_STATSC_INFO(rx_undersized_frames),
	GBE_STATSC_INFO(rx_fragments),
	GBE_STATSC_INFO(rx_bytes),
	GBE_STATSC_INFO(tx_good_frames),
	GBE_STATSC_INFO(tx_broadcast_frames),
	GBE_STATSC_INFO(tx_multicast_frames),
	GBE_STATSC_INFO(tx_pause_frames),
	GBE_STATSC_INFO(tx_deferred_frames),
	GBE_STATSC_INFO(tx_collision_frames),
	GBE_STATSC_INFO(tx_single_coll_frames),
	GBE_STATSC_INFO(tx_mult_coll_frames),
	GBE_STATSC_INFO(tx_excessive_collisions),
	GBE_STATSC_INFO(tx_late_collisions),
	GBE_STATSC_INFO(tx_underrun),
	GBE_STATSC_INFO(tx_carrier_sense_errors),
	GBE_STATSC_INFO(tx_bytes),
	GBE_STATSC_INFO(tx_64byte_frames),
	GBE_STATSC_INFO(tx_65_to_127byte_frames),
	GBE_STATSC_INFO(tx_128_to_255byte_frames),
	GBE_STATSC_INFO(tx_256_to_511byte_frames),
	GBE_STATSC_INFO(tx_512_to_1023byte_frames),
	GBE_STATSC_INFO(tx_1024byte_frames),
	GBE_STATSC_INFO(net_bytes),
	GBE_STATSC_INFO(rx_sof_overruns),
	GBE_STATSC_INFO(rx_mof_overruns),
	GBE_STATSC_INFO(rx_dma_overruns),
	/* GBE module D */
	GBE_STATSD_INFO(rx_good_frames),
	GBE_STATSD_INFO(rx_broadcast_frames),
	GBE_STATSD_INFO(rx_multicast_frames),
	GBE_STATSD_INFO(rx_pause_frames),
	GBE_STATSD_INFO(rx_crc_errors),
	GBE_STATSD_INFO(rx_align_code_errors),
	GBE_STATSD_INFO(rx_oversized_frames),
	GBE_STATSD_INFO(rx_jabber_frames),
	GBE_STATSD_INFO(rx_undersized_frames),
	GBE_STATSD_INFO(rx_fragments),
	GBE_STATSD_INFO(rx_bytes),
	GBE_STATSD_INFO(tx_good_frames),
	GBE_STATSD_INFO(tx_broadcast_frames),
	GBE_STATSD_INFO(tx_multicast_frames),
	GBE_STATSD_INFO(tx_pause_frames),
	GBE_STATSD_INFO(tx_deferred_frames),
	GBE_STATSD_INFO(tx_collision_frames),
	GBE_STATSD_INFO(tx_single_coll_frames),
	GBE_STATSD_INFO(tx_mult_coll_frames),
	GBE_STATSD_INFO(tx_excessive_collisions),
	GBE_STATSD_INFO(tx_late_collisions),
	GBE_STATSD_INFO(tx_underrun),
	GBE_STATSD_INFO(tx_carrier_sense_errors),
	GBE_STATSD_INFO(tx_bytes),
	GBE_STATSD_INFO(tx_64byte_frames),
	GBE_STATSD_INFO(tx_65_to_127byte_frames),
	GBE_STATSD_INFO(tx_128_to_255byte_frames),
	GBE_STATSD_INFO(tx_256_to_511byte_frames),
	GBE_STATSD_INFO(tx_512_to_1023byte_frames),
	GBE_STATSD_INFO(tx_1024byte_frames),
	GBE_STATSD_INFO(net_bytes),
	GBE_STATSD_INFO(rx_sof_overruns),
	GBE_STATSD_INFO(rx_mof_overruns),
	GBE_STATSD_INFO(rx_dma_overruns),
};

/* This is the size of entries in GBENU_STATS_HOST */
#define GBENU_ET_STATS_HOST_SIZE	52

#define GBENU_STATS_HOST(field)					\
{								\
	"GBE_HOST:"#field, GBENU_STATS0_MODULE,			\
	FIELD_SIZEOF(struct gbenu_hw_stats, field),		\
	offsetof(struct gbenu_hw_stats, field)			\
}

/* This is the size of entries in GBENU_STATS_PORT */
#define GBENU_ET_STATS_PORT_SIZE	65

#define GBENU_STATS_P1(field)					\
{								\
	"GBE_P1:"#field, GBENU_STATS1_MODULE,			\
	FIELD_SIZEOF(struct gbenu_hw_stats, field),		\
	offsetof(struct gbenu_hw_stats, field)			\
}

#define GBENU_STATS_P2(field)					\
{								\
	"GBE_P2:"#field, GBENU_STATS2_MODULE,			\
	FIELD_SIZEOF(struct gbenu_hw_stats, field),		\
	offsetof(struct gbenu_hw_stats, field)			\
}

#define GBENU_STATS_P3(field)					\
{								\
	"GBE_P3:"#field, GBENU_STATS3_MODULE,			\
	FIELD_SIZEOF(struct gbenu_hw_stats, field),		\
	offsetof(struct gbenu_hw_stats, field)			\
}

#define GBENU_STATS_P4(field)					\
{								\
	"GBE_P4:"#field, GBENU_STATS4_MODULE,			\
	FIELD_SIZEOF(struct gbenu_hw_stats, field),		\
	offsetof(struct gbenu_hw_stats, field)			\
}

#define GBENU_STATS_P5(field)					\
{								\
	"GBE_P5:"#field, GBENU_STATS5_MODULE,			\
	FIELD_SIZEOF(struct gbenu_hw_stats, field),		\
	offsetof(struct gbenu_hw_stats, field)			\
}

#define GBENU_STATS_P6(field)					\
{								\
	"GBE_P6:"#field, GBENU_STATS6_MODULE,			\
	FIELD_SIZEOF(struct gbenu_hw_stats, field),		\
	offsetof(struct gbenu_hw_stats, field)			\
}

#define GBENU_STATS_P7(field)					\
{								\
	"GBE_P7:"#field, GBENU_STATS7_MODULE,			\
	FIELD_SIZEOF(struct gbenu_hw_stats, field),		\
	offsetof(struct gbenu_hw_stats, field)			\
}

#define GBENU_STATS_P8(field)					\
{								\
	"GBE_P8:"#field, GBENU_STATS8_MODULE,			\
	FIELD_SIZEOF(struct gbenu_hw_stats, field),		\
	offsetof(struct gbenu_hw_stats, field)			\
}

static const struct netcp_ethtool_stat gbenu_et_stats[] = {
	/* GBENU Host Module */
	GBENU_STATS_HOST(rx_good_frames),
	GBENU_STATS_HOST(rx_broadcast_frames),
	GBENU_STATS_HOST(rx_multicast_frames),
	GBENU_STATS_HOST(rx_crc_errors),
	GBENU_STATS_HOST(rx_oversized_frames),
	GBENU_STATS_HOST(rx_undersized_frames),
	GBENU_STATS_HOST(ale_drop),
	GBENU_STATS_HOST(ale_overrun_drop),
	GBENU_STATS_HOST(rx_bytes),
	GBENU_STATS_HOST(tx_good_frames),
	GBENU_STATS_HOST(tx_broadcast_frames),
	GBENU_STATS_HOST(tx_multicast_frames),
	GBENU_STATS_HOST(tx_bytes),
	GBENU_STATS_HOST(tx_64B_frames),
	GBENU_STATS_HOST(tx_65_to_127B_frames),
	GBENU_STATS_HOST(tx_128_to_255B_frames),
	GBENU_STATS_HOST(tx_256_to_511B_frames),
	GBENU_STATS_HOST(tx_512_to_1023B_frames),
	GBENU_STATS_HOST(tx_1024B_frames),
	GBENU_STATS_HOST(net_bytes),
	GBENU_STATS_HOST(rx_bottom_fifo_drop),
	GBENU_STATS_HOST(rx_port_mask_drop),
	GBENU_STATS_HOST(rx_top_fifo_drop),
	GBENU_STATS_HOST(ale_rate_limit_drop),
	GBENU_STATS_HOST(ale_vid_ingress_drop),
	GBENU_STATS_HOST(ale_da_eq_sa_drop),
	GBENU_STATS_HOST(ale_unknown_ucast),
	GBENU_STATS_HOST(ale_unknown_ucast_bytes),
	GBENU_STATS_HOST(ale_unknown_mcast),
	GBENU_STATS_HOST(ale_unknown_mcast_bytes),
	GBENU_STATS_HOST(ale_unknown_bcast),
	GBENU_STATS_HOST(ale_unknown_bcast_bytes),
	GBENU_STATS_HOST(ale_pol_match),
	GBENU_STATS_HOST(ale_pol_match_red),
	GBENU_STATS_HOST(ale_pol_match_yellow),
	GBENU_STATS_HOST(tx_mem_protect_err),
	GBENU_STATS_HOST(tx_pri0_drop),
	GBENU_STATS_HOST(tx_pri1_drop),
	GBENU_STATS_HOST(tx_pri2_drop),
	GBENU_STATS_HOST(tx_pri3_drop),
	GBENU_STATS_HOST(tx_pri4_drop),
	GBENU_STATS_HOST(tx_pri5_drop),
	GBENU_STATS_HOST(tx_pri6_drop),
	GBENU_STATS_HOST(tx_pri7_drop),
	GBENU_STATS_HOST(tx_pri0_drop_bcnt),
	GBENU_STATS_HOST(tx_pri1_drop_bcnt),
	GBENU_STATS_HOST(tx_pri2_drop_bcnt),
	GBENU_STATS_HOST(tx_pri3_drop_bcnt),
	GBENU_STATS_HOST(tx_pri4_drop_bcnt),
	GBENU_STATS_HOST(tx_pri5_drop_bcnt),
	GBENU_STATS_HOST(tx_pri6_drop_bcnt),
	GBENU_STATS_HOST(tx_pri7_drop_bcnt),
	/* GBENU Module 1 */
	GBENU_STATS_P1(rx_good_frames),
	GBENU_STATS_P1(rx_broadcast_frames),
	GBENU_STATS_P1(rx_multicast_frames),
	GBENU_STATS_P1(rx_pause_frames),
	GBENU_STATS_P1(rx_crc_errors),
	GBENU_STATS_P1(rx_align_code_errors),
	GBENU_STATS_P1(rx_oversized_frames),
	GBENU_STATS_P1(rx_jabber_frames),
	GBENU_STATS_P1(rx_undersized_frames),
	GBENU_STATS_P1(rx_fragments),
	GBENU_STATS_P1(ale_drop),
	GBENU_STATS_P1(ale_overrun_drop),
	GBENU_STATS_P1(rx_bytes),
	GBENU_STATS_P1(tx_good_frames),
	GBENU_STATS_P1(tx_broadcast_frames),
	GBENU_STATS_P1(tx_multicast_frames),
	GBENU_STATS_P1(tx_pause_frames),
	GBENU_STATS_P1(tx_deferred_frames),
	GBENU_STATS_P1(tx_collision_frames),
	GBENU_STATS_P1(tx_single_coll_frames),
	GBENU_STATS_P1(tx_mult_coll_frames),
	GBENU_STATS_P1(tx_excessive_collisions),
	GBENU_STATS_P1(tx_late_collisions),
	GBENU_STATS_P1(rx_ipg_error),
	GBENU_STATS_P1(tx_carrier_sense_errors),
	GBENU_STATS_P1(tx_bytes),
	GBENU_STATS_P1(tx_64B_frames),
	GBENU_STATS_P1(tx_65_to_127B_frames),
	GBENU_STATS_P1(tx_128_to_255B_frames),
	GBENU_STATS_P1(tx_256_to_511B_frames),
	GBENU_STATS_P1(tx_512_to_1023B_frames),
	GBENU_STATS_P1(tx_1024B_frames),
	GBENU_STATS_P1(net_bytes),
	GBENU_STATS_P1(rx_bottom_fifo_drop),
	GBENU_STATS_P1(rx_port_mask_drop),
	GBENU_STATS_P1(rx_top_fifo_drop),
	GBENU_STATS_P1(ale_rate_limit_drop),
	GBENU_STATS_P1(ale_vid_ingress_drop),
	GBENU_STATS_P1(ale_da_eq_sa_drop),
	GBENU_STATS_P1(ale_unknown_ucast),
	GBENU_STATS_P1(ale_unknown_ucast_bytes),
	GBENU_STATS_P1(ale_unknown_mcast),
	GBENU_STATS_P1(ale_unknown_mcast_bytes),
	GBENU_STATS_P1(ale_unknown_bcast),
	GBENU_STATS_P1(ale_unknown_bcast_bytes),
	GBENU_STATS_P1(ale_pol_match),
	GBENU_STATS_P1(ale_pol_match_red),
	GBENU_STATS_P1(ale_pol_match_yellow),
	GBENU_STATS_P1(tx_mem_protect_err),
	GBENU_STATS_P1(tx_pri0_drop),
	GBENU_STATS_P1(tx_pri1_drop),
	GBENU_STATS_P1(tx_pri2_drop),
	GBENU_STATS_P1(tx_pri3_drop),
	GBENU_STATS_P1(tx_pri4_drop),
	GBENU_STATS_P1(tx_pri5_drop),
	GBENU_STATS_P1(tx_pri6_drop),
	GBENU_STATS_P1(tx_pri7_drop),
	GBENU_STATS_P1(tx_pri0_drop_bcnt),
	GBENU_STATS_P1(tx_pri1_drop_bcnt),
	GBENU_STATS_P1(tx_pri2_drop_bcnt),
	GBENU_STATS_P1(tx_pri3_drop_bcnt),
	GBENU_STATS_P1(tx_pri4_drop_bcnt),
	GBENU_STATS_P1(tx_pri5_drop_bcnt),
	GBENU_STATS_P1(tx_pri6_drop_bcnt),
	GBENU_STATS_P1(tx_pri7_drop_bcnt),
	/* GBENU Module 2 */
	GBENU_STATS_P2(rx_good_frames),
	GBENU_STATS_P2(rx_broadcast_frames),
	GBENU_STATS_P2(rx_multicast_frames),
	GBENU_STATS_P2(rx_pause_frames),
	GBENU_STATS_P2(rx_crc_errors),
	GBENU_STATS_P2(rx_align_code_errors),
	GBENU_STATS_P2(rx_oversized_frames),
	GBENU_STATS_P2(rx_jabber_frames),
	GBENU_STATS_P2(rx_undersized_frames),
	GBENU_STATS_P2(rx_fragments),
	GBENU_STATS_P2(ale_drop),
	GBENU_STATS_P2(ale_overrun_drop),
	GBENU_STATS_P2(rx_bytes),
	GBENU_STATS_P2(tx_good_frames),
	GBENU_STATS_P2(tx_broadcast_frames),
	GBENU_STATS_P2(tx_multicast_frames),
	GBENU_STATS_P2(tx_pause_frames),
	GBENU_STATS_P2(tx_deferred_frames),
	GBENU_STATS_P2(tx_collision_frames),
	GBENU_STATS_P2(tx_single_coll_frames),
	GBENU_STATS_P2(tx_mult_coll_frames),
	GBENU_STATS_P2(tx_excessive_collisions),
	GBENU_STATS_P2(tx_late_collisions),
	GBENU_STATS_P2(rx_ipg_error),
	GBENU_STATS_P2(tx_carrier_sense_errors),
	GBENU_STATS_P2(tx_bytes),
	GBENU_STATS_P2(tx_64B_frames),
	GBENU_STATS_P2(tx_65_to_127B_frames),
	GBENU_STATS_P2(tx_128_to_255B_frames),
	GBENU_STATS_P2(tx_256_to_511B_frames),
	GBENU_STATS_P2(tx_512_to_1023B_frames),
	GBENU_STATS_P2(tx_1024B_frames),
	GBENU_STATS_P2(net_bytes),
	GBENU_STATS_P2(rx_bottom_fifo_drop),
	GBENU_STATS_P2(rx_port_mask_drop),
	GBENU_STATS_P2(rx_top_fifo_drop),
	GBENU_STATS_P2(ale_rate_limit_drop),
	GBENU_STATS_P2(ale_vid_ingress_drop),
	GBENU_STATS_P2(ale_da_eq_sa_drop),
	GBENU_STATS_P2(ale_unknown_ucast),
	GBENU_STATS_P2(ale_unknown_ucast_bytes),
	GBENU_STATS_P2(ale_unknown_mcast),
	GBENU_STATS_P2(ale_unknown_mcast_bytes),
	GBENU_STATS_P2(ale_unknown_bcast),
	GBENU_STATS_P2(ale_unknown_bcast_bytes),
	GBENU_STATS_P2(ale_pol_match),
	GBENU_STATS_P2(ale_pol_match_red),
	GBENU_STATS_P2(ale_pol_match_yellow),
	GBENU_STATS_P2(tx_mem_protect_err),
	GBENU_STATS_P2(tx_pri0_drop),
	GBENU_STATS_P2(tx_pri1_drop),
	GBENU_STATS_P2(tx_pri2_drop),
	GBENU_STATS_P2(tx_pri3_drop),
	GBENU_STATS_P2(tx_pri4_drop),
	GBENU_STATS_P2(tx_pri5_drop),
	GBENU_STATS_P2(tx_pri6_drop),
	GBENU_STATS_P2(tx_pri7_drop),
	GBENU_STATS_P2(tx_pri0_drop_bcnt),
	GBENU_STATS_P2(tx_pri1_drop_bcnt),
	GBENU_STATS_P2(tx_pri2_drop_bcnt),
	GBENU_STATS_P2(tx_pri3_drop_bcnt),
	GBENU_STATS_P2(tx_pri4_drop_bcnt),
	GBENU_STATS_P2(tx_pri5_drop_bcnt),
	GBENU_STATS_P2(tx_pri6_drop_bcnt),
	GBENU_STATS_P2(tx_pri7_drop_bcnt),
	/* GBENU Module 3 */
	GBENU_STATS_P3(rx_good_frames),
	GBENU_STATS_P3(rx_broadcast_frames),
	GBENU_STATS_P3(rx_multicast_frames),
	GBENU_STATS_P3(rx_pause_frames),
	GBENU_STATS_P3(rx_crc_errors),
	GBENU_STATS_P3(rx_align_code_errors),
	GBENU_STATS_P3(rx_oversized_frames),
	GBENU_STATS_P3(rx_jabber_frames),
	GBENU_STATS_P3(rx_undersized_frames),
	GBENU_STATS_P3(rx_fragments),
	GBENU_STATS_P3(ale_drop),
	GBENU_STATS_P3(ale_overrun_drop),
	GBENU_STATS_P3(rx_bytes),
	GBENU_STATS_P3(tx_good_frames),
	GBENU_STATS_P3(tx_broadcast_frames),
	GBENU_STATS_P3(tx_multicast_frames),
	GBENU_STATS_P3(tx_pause_frames),
	GBENU_STATS_P3(tx_deferred_frames),
	GBENU_STATS_P3(tx_collision_frames),
	GBENU_STATS_P3(tx_single_coll_frames),
	GBENU_STATS_P3(tx_mult_coll_frames),
	GBENU_STATS_P3(tx_excessive_collisions),
	GBENU_STATS_P3(tx_late_collisions),
	GBENU_STATS_P3(rx_ipg_error),
	GBENU_STATS_P3(tx_carrier_sense_errors),
	GBENU_STATS_P3(tx_bytes),
	GBENU_STATS_P3(tx_64B_frames),
	GBENU_STATS_P3(tx_65_to_127B_frames),
	GBENU_STATS_P3(tx_128_to_255B_frames),
	GBENU_STATS_P3(tx_256_to_511B_frames),
	GBENU_STATS_P3(tx_512_to_1023B_frames),
	GBENU_STATS_P3(tx_1024B_frames),
	GBENU_STATS_P3(net_bytes),
	GBENU_STATS_P3(rx_bottom_fifo_drop),
	GBENU_STATS_P3(rx_port_mask_drop),
	GBENU_STATS_P3(rx_top_fifo_drop),
	GBENU_STATS_P3(ale_rate_limit_drop),
	GBENU_STATS_P3(ale_vid_ingress_drop),
	GBENU_STATS_P3(ale_da_eq_sa_drop),
	GBENU_STATS_P3(ale_unknown_ucast),
	GBENU_STATS_P3(ale_unknown_ucast_bytes),
	GBENU_STATS_P3(ale_unknown_mcast),
	GBENU_STATS_P3(ale_unknown_mcast_bytes),
	GBENU_STATS_P3(ale_unknown_bcast),
	GBENU_STATS_P3(ale_unknown_bcast_bytes),
	GBENU_STATS_P3(ale_pol_match),
	GBENU_STATS_P3(ale_pol_match_red),
	GBENU_STATS_P3(ale_pol_match_yellow),
	GBENU_STATS_P3(tx_mem_protect_err),
	GBENU_STATS_P3(tx_pri0_drop),
	GBENU_STATS_P3(tx_pri1_drop),
	GBENU_STATS_P3(tx_pri2_drop),
	GBENU_STATS_P3(tx_pri3_drop),
	GBENU_STATS_P3(tx_pri4_drop),
	GBENU_STATS_P3(tx_pri5_drop),
	GBENU_STATS_P3(tx_pri6_drop),
	GBENU_STATS_P3(tx_pri7_drop),
	GBENU_STATS_P3(tx_pri0_drop_bcnt),
	GBENU_STATS_P3(tx_pri1_drop_bcnt),
	GBENU_STATS_P3(tx_pri2_drop_bcnt),
	GBENU_STATS_P3(tx_pri3_drop_bcnt),
	GBENU_STATS_P3(tx_pri4_drop_bcnt),
	GBENU_STATS_P3(tx_pri5_drop_bcnt),
	GBENU_STATS_P3(tx_pri6_drop_bcnt),
	GBENU_STATS_P3(tx_pri7_drop_bcnt),
	/* GBENU Module 4 */
	GBENU_STATS_P4(rx_good_frames),
	GBENU_STATS_P4(rx_broadcast_frames),
	GBENU_STATS_P4(rx_multicast_frames),
	GBENU_STATS_P4(rx_pause_frames),
	GBENU_STATS_P4(rx_crc_errors),
	GBENU_STATS_P4(rx_align_code_errors),
	GBENU_STATS_P4(rx_oversized_frames),
	GBENU_STATS_P4(rx_jabber_frames),
	GBENU_STATS_P4(rx_undersized_frames),
	GBENU_STATS_P4(rx_fragments),
	GBENU_STATS_P4(ale_drop),
	GBENU_STATS_P4(ale_overrun_drop),
	GBENU_STATS_P4(rx_bytes),
	GBENU_STATS_P4(tx_good_frames),
	GBENU_STATS_P4(tx_broadcast_frames),
	GBENU_STATS_P4(tx_multicast_frames),
	GBENU_STATS_P4(tx_pause_frames),
	GBENU_STATS_P4(tx_deferred_frames),
	GBENU_STATS_P4(tx_collision_frames),
	GBENU_STATS_P4(tx_single_coll_frames),
	GBENU_STATS_P4(tx_mult_coll_frames),
	GBENU_STATS_P4(tx_excessive_collisions),
	GBENU_STATS_P4(tx_late_collisions),
	GBENU_STATS_P4(rx_ipg_error),
	GBENU_STATS_P4(tx_carrier_sense_errors),
	GBENU_STATS_P4(tx_bytes),
	GBENU_STATS_P4(tx_64B_frames),
	GBENU_STATS_P4(tx_65_to_127B_frames),
	GBENU_STATS_P4(tx_128_to_255B_frames),
	GBENU_STATS_P4(tx_256_to_511B_frames),
	GBENU_STATS_P4(tx_512_to_1023B_frames),
	GBENU_STATS_P4(tx_1024B_frames),
	GBENU_STATS_P4(net_bytes),
	GBENU_STATS_P4(rx_bottom_fifo_drop),
	GBENU_STATS_P4(rx_port_mask_drop),
	GBENU_STATS_P4(rx_top_fifo_drop),
	GBENU_STATS_P4(ale_rate_limit_drop),
	GBENU_STATS_P4(ale_vid_ingress_drop),
	GBENU_STATS_P4(ale_da_eq_sa_drop),
	GBENU_STATS_P4(ale_unknown_ucast),
	GBENU_STATS_P4(ale_unknown_ucast_bytes),
	GBENU_STATS_P4(ale_unknown_mcast),
	GBENU_STATS_P4(ale_unknown_mcast_bytes),
	GBENU_STATS_P4(ale_unknown_bcast),
	GBENU_STATS_P4(ale_unknown_bcast_bytes),
	GBENU_STATS_P4(ale_pol_match),
	GBENU_STATS_P4(ale_pol_match_red),
	GBENU_STATS_P4(ale_pol_match_yellow),
	GBENU_STATS_P4(tx_mem_protect_err),
	GBENU_STATS_P4(tx_pri0_drop),
	GBENU_STATS_P4(tx_pri1_drop),
	GBENU_STATS_P4(tx_pri2_drop),
	GBENU_STATS_P4(tx_pri3_drop),
	GBENU_STATS_P4(tx_pri4_drop),
	GBENU_STATS_P4(tx_pri5_drop),
	GBENU_STATS_P4(tx_pri6_drop),
	GBENU_STATS_P4(tx_pri7_drop),
	GBENU_STATS_P4(tx_pri0_drop_bcnt),
	GBENU_STATS_P4(tx_pri1_drop_bcnt),
	GBENU_STATS_P4(tx_pri2_drop_bcnt),
	GBENU_STATS_P4(tx_pri3_drop_bcnt),
	GBENU_STATS_P4(tx_pri4_drop_bcnt),
	GBENU_STATS_P4(tx_pri5_drop_bcnt),
	GBENU_STATS_P4(tx_pri6_drop_bcnt),
	GBENU_STATS_P4(tx_pri7_drop_bcnt),
	/* GBENU Module 5 */
	GBENU_STATS_P5(rx_good_frames),
	GBENU_STATS_P5(rx_broadcast_frames),
	GBENU_STATS_P5(rx_multicast_frames),
	GBENU_STATS_P5(rx_pause_frames),
	GBENU_STATS_P5(rx_crc_errors),
	GBENU_STATS_P5(rx_align_code_errors),
	GBENU_STATS_P5(rx_oversized_frames),
	GBENU_STATS_P5(rx_jabber_frames),
	GBENU_STATS_P5(rx_undersized_frames),
	GBENU_STATS_P5(rx_fragments),
	GBENU_STATS_P5(ale_drop),
	GBENU_STATS_P5(ale_overrun_drop),
	GBENU_STATS_P5(rx_bytes),
	GBENU_STATS_P5(tx_good_frames),
	GBENU_STATS_P5(tx_broadcast_frames),
	GBENU_STATS_P5(tx_multicast_frames),
	GBENU_STATS_P5(tx_pause_frames),
	GBENU_STATS_P5(tx_deferred_frames),
	GBENU_STATS_P5(tx_collision_frames),
	GBENU_STATS_P5(tx_single_coll_frames),
	GBENU_STATS_P5(tx_mult_coll_frames),
	GBENU_STATS_P5(tx_excessive_collisions),
	GBENU_STATS_P5(tx_late_collisions),
	GBENU_STATS_P5(rx_ipg_error),
	GBENU_STATS_P5(tx_carrier_sense_errors),
	GBENU_STATS_P5(tx_bytes),
	GBENU_STATS_P5(tx_64B_frames),
	GBENU_STATS_P5(tx_65_to_127B_frames),
	GBENU_STATS_P5(tx_128_to_255B_frames),
	GBENU_STATS_P5(tx_256_to_511B_frames),
	GBENU_STATS_P5(tx_512_to_1023B_frames),
	GBENU_STATS_P5(tx_1024B_frames),
	GBENU_STATS_P5(net_bytes),
	GBENU_STATS_P5(rx_bottom_fifo_drop),
	GBENU_STATS_P5(rx_port_mask_drop),
	GBENU_STATS_P5(rx_top_fifo_drop),
	GBENU_STATS_P5(ale_rate_limit_drop),
	GBENU_STATS_P5(ale_vid_ingress_drop),
	GBENU_STATS_P5(ale_da_eq_sa_drop),
	GBENU_STATS_P5(ale_unknown_ucast),
	GBENU_STATS_P5(ale_unknown_ucast_bytes),
	GBENU_STATS_P5(ale_unknown_mcast),
	GBENU_STATS_P5(ale_unknown_mcast_bytes),
	GBENU_STATS_P5(ale_unknown_bcast),
	GBENU_STATS_P5(ale_unknown_bcast_bytes),
	GBENU_STATS_P5(ale_pol_match),
	GBENU_STATS_P5(ale_pol_match_red),
	GBENU_STATS_P5(ale_pol_match_yellow),
	GBENU_STATS_P5(tx_mem_protect_err),
	GBENU_STATS_P5(tx_pri0_drop),
	GBENU_STATS_P5(tx_pri1_drop),
	GBENU_STATS_P5(tx_pri2_drop),
	GBENU_STATS_P5(tx_pri3_drop),
	GBENU_STATS_P5(tx_pri4_drop),
	GBENU_STATS_P5(tx_pri5_drop),
	GBENU_STATS_P5(tx_pri6_drop),
	GBENU_STATS_P5(tx_pri7_drop),
	GBENU_STATS_P5(tx_pri0_drop_bcnt),
	GBENU_STATS_P5(tx_pri1_drop_bcnt),
	GBENU_STATS_P5(tx_pri2_drop_bcnt),
	GBENU_STATS_P5(tx_pri3_drop_bcnt),
	GBENU_STATS_P5(tx_pri4_drop_bcnt),
	GBENU_STATS_P5(tx_pri5_drop_bcnt),
	GBENU_STATS_P5(tx_pri6_drop_bcnt),
	GBENU_STATS_P5(tx_pri7_drop_bcnt),
	/* GBENU Module 6 */
	GBENU_STATS_P6(rx_good_frames),
	GBENU_STATS_P6(rx_broadcast_frames),
	GBENU_STATS_P6(rx_multicast_frames),
	GBENU_STATS_P6(rx_pause_frames),
	GBENU_STATS_P6(rx_crc_errors),
	GBENU_STATS_P6(rx_align_code_errors),
	GBENU_STATS_P6(rx_oversized_frames),
	GBENU_STATS_P6(rx_jabber_frames),
	GBENU_STATS_P6(rx_undersized_frames),
	GBENU_STATS_P6(rx_fragments),
	GBENU_STATS_P6(ale_drop),
	GBENU_STATS_P6(ale_overrun_drop),
	GBENU_STATS_P6(rx_bytes),
	GBENU_STATS_P6(tx_good_frames),
	GBENU_STATS_P6(tx_broadcast_frames),
	GBENU_STATS_P6(tx_multicast_frames),
	GBENU_STATS_P6(tx_pause_frames),
	GBENU_STATS_P6(tx_deferred_frames),
	GBENU_STATS_P6(tx_collision_frames),
	GBENU_STATS_P6(tx_single_coll_frames),
	GBENU_STATS_P6(tx_mult_coll_frames),
	GBENU_STATS_P6(tx_excessive_collisions),
	GBENU_STATS_P6(tx_late_collisions),
	GBENU_STATS_P6(rx_ipg_error),
	GBENU_STATS_P6(tx_carrier_sense_errors),
	GBENU_STATS_P6(tx_bytes),
	GBENU_STATS_P6(tx_64B_frames),
	GBENU_STATS_P6(tx_65_to_127B_frames),
	GBENU_STATS_P6(tx_128_to_255B_frames),
	GBENU_STATS_P6(tx_256_to_511B_frames),
	GBENU_STATS_P6(tx_512_to_1023B_frames),
	GBENU_STATS_P6(tx_1024B_frames),
	GBENU_STATS_P6(net_bytes),
	GBENU_STATS_P6(rx_bottom_fifo_drop),
	GBENU_STATS_P6(rx_port_mask_drop),
	GBENU_STATS_P6(rx_top_fifo_drop),
	GBENU_STATS_P6(ale_rate_limit_drop),
	GBENU_STATS_P6(ale_vid_ingress_drop),
	GBENU_STATS_P6(ale_da_eq_sa_drop),
	GBENU_STATS_P6(ale_unknown_ucast),
	GBENU_STATS_P6(ale_unknown_ucast_bytes),
	GBENU_STATS_P6(ale_unknown_mcast),
	GBENU_STATS_P6(ale_unknown_mcast_bytes),
	GBENU_STATS_P6(ale_unknown_bcast),
	GBENU_STATS_P6(ale_unknown_bcast_bytes),
	GBENU_STATS_P6(ale_pol_match),
	GBENU_STATS_P6(ale_pol_match_red),
	GBENU_STATS_P6(ale_pol_match_yellow),
	GBENU_STATS_P6(tx_mem_protect_err),
	GBENU_STATS_P6(tx_pri0_drop),
	GBENU_STATS_P6(tx_pri1_drop),
	GBENU_STATS_P6(tx_pri2_drop),
	GBENU_STATS_P6(tx_pri3_drop),
	GBENU_STATS_P6(tx_pri4_drop),
	GBENU_STATS_P6(tx_pri5_drop),
	GBENU_STATS_P6(tx_pri6_drop),
	GBENU_STATS_P6(tx_pri7_drop),
	GBENU_STATS_P6(tx_pri0_drop_bcnt),
	GBENU_STATS_P6(tx_pri1_drop_bcnt),
	GBENU_STATS_P6(tx_pri2_drop_bcnt),
	GBENU_STATS_P6(tx_pri3_drop_bcnt),
	GBENU_STATS_P6(tx_pri4_drop_bcnt),
	GBENU_STATS_P6(tx_pri5_drop_bcnt),
	GBENU_STATS_P6(tx_pri6_drop_bcnt),
	GBENU_STATS_P6(tx_pri7_drop_bcnt),
	/* GBENU Module 7 */
	GBENU_STATS_P7(rx_good_frames),
	GBENU_STATS_P7(rx_broadcast_frames),
	GBENU_STATS_P7(rx_multicast_frames),
	GBENU_STATS_P7(rx_pause_frames),
	GBENU_STATS_P7(rx_crc_errors),
	GBENU_STATS_P7(rx_align_code_errors),
	GBENU_STATS_P7(rx_oversized_frames),
	GBENU_STATS_P7(rx_jabber_frames),
	GBENU_STATS_P7(rx_undersized_frames),
	GBENU_STATS_P7(rx_fragments),
	GBENU_STATS_P7(ale_drop),
	GBENU_STATS_P7(ale_overrun_drop),
	GBENU_STATS_P7(rx_bytes),
	GBENU_STATS_P7(tx_good_frames),
	GBENU_STATS_P7(tx_broadcast_frames),
	GBENU_STATS_P7(tx_multicast_frames),
	GBENU_STATS_P7(tx_pause_frames),
	GBENU_STATS_P7(tx_deferred_frames),
	GBENU_STATS_P7(tx_collision_frames),
	GBENU_STATS_P7(tx_single_coll_frames),
	GBENU_STATS_P7(tx_mult_coll_frames),
	GBENU_STATS_P7(tx_excessive_collisions),
	GBENU_STATS_P7(tx_late_collisions),
	GBENU_STATS_P7(rx_ipg_error),
	GBENU_STATS_P7(tx_carrier_sense_errors),
	GBENU_STATS_P7(tx_bytes),
	GBENU_STATS_P7(tx_64B_frames),
	GBENU_STATS_P7(tx_65_to_127B_frames),
	GBENU_STATS_P7(tx_128_to_255B_frames),
	GBENU_STATS_P7(tx_256_to_511B_frames),
	GBENU_STATS_P7(tx_512_to_1023B_frames),
	GBENU_STATS_P7(tx_1024B_frames),
	GBENU_STATS_P7(net_bytes),
	GBENU_STATS_P7(rx_bottom_fifo_drop),
	GBENU_STATS_P7(rx_port_mask_drop),
	GBENU_STATS_P7(rx_top_fifo_drop),
	GBENU_STATS_P7(ale_rate_limit_drop),
	GBENU_STATS_P7(ale_vid_ingress_drop),
	GBENU_STATS_P7(ale_da_eq_sa_drop),
	GBENU_STATS_P7(ale_unknown_ucast),
	GBENU_STATS_P7(ale_unknown_ucast_bytes),
	GBENU_STATS_P7(ale_unknown_mcast),
	GBENU_STATS_P7(ale_unknown_mcast_bytes),
	GBENU_STATS_P7(ale_unknown_bcast),
	GBENU_STATS_P7(ale_unknown_bcast_bytes),
	GBENU_STATS_P7(ale_pol_match),
	GBENU_STATS_P7(ale_pol_match_red),
	GBENU_STATS_P7(ale_pol_match_yellow),
	GBENU_STATS_P7(tx_mem_protect_err),
	GBENU_STATS_P7(tx_pri0_drop),
	GBENU_STATS_P7(tx_pri1_drop),
	GBENU_STATS_P7(tx_pri2_drop),
	GBENU_STATS_P7(tx_pri3_drop),
	GBENU_STATS_P7(tx_pri4_drop),
	GBENU_STATS_P7(tx_pri5_drop),
	GBENU_STATS_P7(tx_pri6_drop),
	GBENU_STATS_P7(tx_pri7_drop),
	GBENU_STATS_P7(tx_pri0_drop_bcnt),
	GBENU_STATS_P7(tx_pri1_drop_bcnt),
	GBENU_STATS_P7(tx_pri2_drop_bcnt),
	GBENU_STATS_P7(tx_pri3_drop_bcnt),
	GBENU_STATS_P7(tx_pri4_drop_bcnt),
	GBENU_STATS_P7(tx_pri5_drop_bcnt),
	GBENU_STATS_P7(tx_pri6_drop_bcnt),
	GBENU_STATS_P7(tx_pri7_drop_bcnt),
	/* GBENU Module 8 */
	GBENU_STATS_P8(rx_good_frames),
	GBENU_STATS_P8(rx_broadcast_frames),
	GBENU_STATS_P8(rx_multicast_frames),
	GBENU_STATS_P8(rx_pause_frames),
	GBENU_STATS_P8(rx_crc_errors),
	GBENU_STATS_P8(rx_align_code_errors),
	GBENU_STATS_P8(rx_oversized_frames),
	GBENU_STATS_P8(rx_jabber_frames),
	GBENU_STATS_P8(rx_undersized_frames),
	GBENU_STATS_P8(rx_fragments),
	GBENU_STATS_P8(ale_drop),
	GBENU_STATS_P8(ale_overrun_drop),
	GBENU_STATS_P8(rx_bytes),
	GBENU_STATS_P8(tx_good_frames),
	GBENU_STATS_P8(tx_broadcast_frames),
	GBENU_STATS_P8(tx_multicast_frames),
	GBENU_STATS_P8(tx_pause_frames),
	GBENU_STATS_P8(tx_deferred_frames),
	GBENU_STATS_P8(tx_collision_frames),
	GBENU_STATS_P8(tx_single_coll_frames),
	GBENU_STATS_P8(tx_mult_coll_frames),
	GBENU_STATS_P8(tx_excessive_collisions),
	GBENU_STATS_P8(tx_late_collisions),
	GBENU_STATS_P8(rx_ipg_error),
	GBENU_STATS_P8(tx_carrier_sense_errors),
	GBENU_STATS_P8(tx_bytes),
	GBENU_STATS_P8(tx_64B_frames),
	GBENU_STATS_P8(tx_65_to_127B_frames),
	GBENU_STATS_P8(tx_128_to_255B_frames),
	GBENU_STATS_P8(tx_256_to_511B_frames),
	GBENU_STATS_P8(tx_512_to_1023B_frames),
	GBENU_STATS_P8(tx_1024B_frames),
	GBENU_STATS_P8(net_bytes),
	GBENU_STATS_P8(rx_bottom_fifo_drop),
	GBENU_STATS_P8(rx_port_mask_drop),
	GBENU_STATS_P8(rx_top_fifo_drop),
	GBENU_STATS_P8(ale_rate_limit_drop),
	GBENU_STATS_P8(ale_vid_ingress_drop),
	GBENU_STATS_P8(ale_da_eq_sa_drop),
	GBENU_STATS_P8(ale_unknown_ucast),
	GBENU_STATS_P8(ale_unknown_ucast_bytes),
	GBENU_STATS_P8(ale_unknown_mcast),
	GBENU_STATS_P8(ale_unknown_mcast_bytes),
	GBENU_STATS_P8(ale_unknown_bcast),
	GBENU_STATS_P8(ale_unknown_bcast_bytes),
	GBENU_STATS_P8(ale_pol_match),
	GBENU_STATS_P8(ale_pol_match_red),
	GBENU_STATS_P8(ale_pol_match_yellow),
	GBENU_STATS_P8(tx_mem_protect_err),
	GBENU_STATS_P8(tx_pri0_drop),
	GBENU_STATS_P8(tx_pri1_drop),
	GBENU_STATS_P8(tx_pri2_drop),
	GBENU_STATS_P8(tx_pri3_drop),
	GBENU_STATS_P8(tx_pri4_drop),
	GBENU_STATS_P8(tx_pri5_drop),
	GBENU_STATS_P8(tx_pri6_drop),
	GBENU_STATS_P8(tx_pri7_drop),
	GBENU_STATS_P8(tx_pri0_drop_bcnt),
	GBENU_STATS_P8(tx_pri1_drop_bcnt),
	GBENU_STATS_P8(tx_pri2_drop_bcnt),
	GBENU_STATS_P8(tx_pri3_drop_bcnt),
	GBENU_STATS_P8(tx_pri4_drop_bcnt),
	GBENU_STATS_P8(tx_pri5_drop_bcnt),
	GBENU_STATS_P8(tx_pri6_drop_bcnt),
	GBENU_STATS_P8(tx_pri7_drop_bcnt),
};

#define XGBE_STATS0_INFO(field)				\
{							\
	"GBE_0:"#field, XGBE_STATS0_MODULE,		\
	FIELD_SIZEOF(struct xgbe_hw_stats, field),	\
	offsetof(struct xgbe_hw_stats, field)		\
}

#define XGBE_STATS1_INFO(field)				\
{							\
	"GBE_1:"#field, XGBE_STATS1_MODULE,		\
	FIELD_SIZEOF(struct xgbe_hw_stats, field),	\
	offsetof(struct xgbe_hw_stats, field)		\
}

#define XGBE_STATS2_INFO(field)				\
{							\
	"GBE_2:"#field, XGBE_STATS2_MODULE,		\
	FIELD_SIZEOF(struct xgbe_hw_stats, field),	\
	offsetof(struct xgbe_hw_stats, field)		\
}

static const struct netcp_ethtool_stat xgbe10_et_stats[] = {
	/* GBE module 0 */
	XGBE_STATS0_INFO(rx_good_frames),
	XGBE_STATS0_INFO(rx_broadcast_frames),
	XGBE_STATS0_INFO(rx_multicast_frames),
	XGBE_STATS0_INFO(rx_oversized_frames),
	XGBE_STATS0_INFO(rx_undersized_frames),
	XGBE_STATS0_INFO(overrun_type4),
	XGBE_STATS0_INFO(overrun_type5),
	XGBE_STATS0_INFO(rx_bytes),
	XGBE_STATS0_INFO(tx_good_frames),
	XGBE_STATS0_INFO(tx_broadcast_frames),
	XGBE_STATS0_INFO(tx_multicast_frames),
	XGBE_STATS0_INFO(tx_bytes),
	XGBE_STATS0_INFO(tx_64byte_frames),
	XGBE_STATS0_INFO(tx_65_to_127byte_frames),
	XGBE_STATS0_INFO(tx_128_to_255byte_frames),
	XGBE_STATS0_INFO(tx_256_to_511byte_frames),
	XGBE_STATS0_INFO(tx_512_to_1023byte_frames),
	XGBE_STATS0_INFO(tx_1024byte_frames),
	XGBE_STATS0_INFO(net_bytes),
	XGBE_STATS0_INFO(rx_sof_overruns),
	XGBE_STATS0_INFO(rx_mof_overruns),
	XGBE_STATS0_INFO(rx_dma_overruns),
	/* XGBE module 1 */
	XGBE_STATS1_INFO(rx_good_frames),
	XGBE_STATS1_INFO(rx_broadcast_frames),
	XGBE_STATS1_INFO(rx_multicast_frames),
	XGBE_STATS1_INFO(rx_pause_frames),
	XGBE_STATS1_INFO(rx_crc_errors),
	XGBE_STATS1_INFO(rx_align_code_errors),
	XGBE_STATS1_INFO(rx_oversized_frames),
	XGBE_STATS1_INFO(rx_jabber_frames),
	XGBE_STATS1_INFO(rx_undersized_frames),
	XGBE_STATS1_INFO(rx_fragments),
	XGBE_STATS1_INFO(overrun_type4),
	XGBE_STATS1_INFO(overrun_type5),
	XGBE_STATS1_INFO(rx_bytes),
	XGBE_STATS1_INFO(tx_good_frames),
	XGBE_STATS1_INFO(tx_broadcast_frames),
	XGBE_STATS1_INFO(tx_multicast_frames),
	XGBE_STATS1_INFO(tx_pause_frames),
	XGBE_STATS1_INFO(tx_deferred_frames),
	XGBE_STATS1_INFO(tx_collision_frames),
	XGBE_STATS1_INFO(tx_single_coll_frames),
	XGBE_STATS1_INFO(tx_mult_coll_frames),
	XGBE_STATS1_INFO(tx_excessive_collisions),
	XGBE_STATS1_INFO(tx_late_collisions),
	XGBE_STATS1_INFO(tx_underrun),
	XGBE_STATS1_INFO(tx_carrier_sense_errors),
	XGBE_STATS1_INFO(tx_bytes),
	XGBE_STATS1_INFO(tx_64byte_frames),
	XGBE_STATS1_INFO(tx_65_to_127byte_frames),
	XGBE_STATS1_INFO(tx_128_to_255byte_frames),
	XGBE_STATS1_INFO(tx_256_to_511byte_frames),
	XGBE_STATS1_INFO(tx_512_to_1023byte_frames),
	XGBE_STATS1_INFO(tx_1024byte_frames),
	XGBE_STATS1_INFO(net_bytes),
	XGBE_STATS1_INFO(rx_sof_overruns),
	XGBE_STATS1_INFO(rx_mof_overruns),
	XGBE_STATS1_INFO(rx_dma_overruns),
	/* XGBE module 2 */
	XGBE_STATS2_INFO(rx_good_frames),
	XGBE_STATS2_INFO(rx_broadcast_frames),
	XGBE_STATS2_INFO(rx_multicast_frames),
	XGBE_STATS2_INFO(rx_pause_frames),
	XGBE_STATS2_INFO(rx_crc_errors),
	XGBE_STATS2_INFO(rx_align_code_errors),
	XGBE_STATS2_INFO(rx_oversized_frames),
	XGBE_STATS2_INFO(rx_jabber_frames),
	XGBE_STATS2_INFO(rx_undersized_frames),
	XGBE_STATS2_INFO(rx_fragments),
	XGBE_STATS2_INFO(overrun_type4),
	XGBE_STATS2_INFO(overrun_type5),
	XGBE_STATS2_INFO(rx_bytes),
	XGBE_STATS2_INFO(tx_good_frames),
	XGBE_STATS2_INFO(tx_broadcast_frames),
	XGBE_STATS2_INFO(tx_multicast_frames),
	XGBE_STATS2_INFO(tx_pause_frames),
	XGBE_STATS2_INFO(tx_deferred_frames),
	XGBE_STATS2_INFO(tx_collision_frames),
	XGBE_STATS2_INFO(tx_single_coll_frames),
	XGBE_STATS2_INFO(tx_mult_coll_frames),
	XGBE_STATS2_INFO(tx_excessive_collisions),
	XGBE_STATS2_INFO(tx_late_collisions),
	XGBE_STATS2_INFO(tx_underrun),
	XGBE_STATS2_INFO(tx_carrier_sense_errors),
	XGBE_STATS2_INFO(tx_bytes),
	XGBE_STATS2_INFO(tx_64byte_frames),
	XGBE_STATS2_INFO(tx_65_to_127byte_frames),
	XGBE_STATS2_INFO(tx_128_to_255byte_frames),
	XGBE_STATS2_INFO(tx_256_to_511byte_frames),
	XGBE_STATS2_INFO(tx_512_to_1023byte_frames),
	XGBE_STATS2_INFO(tx_1024byte_frames),
	XGBE_STATS2_INFO(net_bytes),
	XGBE_STATS2_INFO(rx_sof_overruns),
	XGBE_STATS2_INFO(rx_mof_overruns),
	XGBE_STATS2_INFO(rx_dma_overruns),
};

#define for_each_intf(i, priv) \
	list_for_each_entry((i), &(priv)->gbe_intf_head, gbe_intf_list)

#define for_each_sec_slave(slave, priv) \
	list_for_each_entry((slave), &(priv)->secondary_slaves, slave_list)

#define first_sec_slave(priv)					\
	list_first_entry(&priv->secondary_slaves, \
			struct gbe_slave, slave_list)

static void keystone_get_drvinfo(struct net_device *ndev,
				 struct ethtool_drvinfo *info)
{
	strncpy(info->driver, NETCP_DRIVER_NAME, sizeof(info->driver));
	strncpy(info->version, NETCP_DRIVER_VERSION, sizeof(info->version));
}

static u32 keystone_get_msglevel(struct net_device *ndev)
{
	struct netcp_intf *netcp = netdev_priv(ndev);

	return netcp->msg_enable;
}

static void keystone_set_msglevel(struct net_device *ndev, u32 value)
{
	struct netcp_intf *netcp = netdev_priv(ndev);

	netcp->msg_enable = value;
}

static struct gbe_intf *keystone_get_intf_data(struct netcp_intf *netcp)
{
	struct gbe_intf *gbe_intf;

	gbe_intf = netcp_module_get_intf_data(&gbe_module, netcp);
	if (!gbe_intf)
		gbe_intf = netcp_module_get_intf_data(&xgbe_module, netcp);

	return gbe_intf;
}

static void keystone_get_stat_strings(struct net_device *ndev,
				      uint32_t stringset, uint8_t *data)
{
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct gbe_intf *gbe_intf;
	struct gbe_priv *gbe_dev;
	int i;

	gbe_intf = keystone_get_intf_data(netcp);
	if (!gbe_intf)
		return;
	gbe_dev = gbe_intf->gbe_dev;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < gbe_dev->num_et_stats; i++) {
			memcpy(data, gbe_dev->et_stats[i].desc,
			       ETH_GSTRING_LEN);
			data += ETH_GSTRING_LEN;
		}
		break;
	case ETH_SS_TEST:
		break;
	}
}

static int keystone_get_sset_count(struct net_device *ndev, int stringset)
{
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct gbe_intf *gbe_intf;
	struct gbe_priv *gbe_dev;

	gbe_intf = keystone_get_intf_data(netcp);
	if (!gbe_intf)
		return -EINVAL;
	gbe_dev = gbe_intf->gbe_dev;

	switch (stringset) {
	case ETH_SS_TEST:
		return 0;
	case ETH_SS_STATS:
		return gbe_dev->num_et_stats;
	default:
		return -EINVAL;
	}
}

static void gbe_reset_mod_stats(struct gbe_priv *gbe_dev, int stats_mod)
{
	void __iomem *base = gbe_dev->hw_stats_regs[stats_mod];
	u32  __iomem *p_stats_entry;
	int i;

	for (i = 0; i < gbe_dev->num_et_stats; i++) {
		if (gbe_dev->et_stats[i].type == stats_mod) {
			p_stats_entry = base + gbe_dev->et_stats[i].offset;
			gbe_dev->hw_stats[i] = 0;
			gbe_dev->hw_stats_prev[i] = readl(p_stats_entry);
		}
	}
}

static inline void gbe_update_hw_stats_entry(struct gbe_priv *gbe_dev,
					     int et_stats_entry)
{
	void __iomem *base = NULL;
	u32  __iomem *p_stats_entry;
	u32 curr, delta;

	/* The hw_stats_regs pointers are already
	 * properly set to point to the right base:
	 */
	base = gbe_dev->hw_stats_regs[gbe_dev->et_stats[et_stats_entry].type];
	p_stats_entry = base + gbe_dev->et_stats[et_stats_entry].offset;
	curr = readl(p_stats_entry);
	delta = curr - gbe_dev->hw_stats_prev[et_stats_entry];
	gbe_dev->hw_stats_prev[et_stats_entry] = curr;
	gbe_dev->hw_stats[et_stats_entry] += delta;
}

static void gbe_update_stats(struct gbe_priv *gbe_dev, uint64_t *data)
{
	int i;

	for (i = 0; i < gbe_dev->num_et_stats; i++) {
		gbe_update_hw_stats_entry(gbe_dev, i);

		if (data)
			data[i] = gbe_dev->hw_stats[i];
	}
}

static inline void gbe_stats_mod_visible_ver14(struct gbe_priv *gbe_dev,
					       int stats_mod)
{
	u32 val;

	val = readl(GBE_REG_ADDR(gbe_dev, switch_regs, stat_port_en));

	switch (stats_mod) {
	case GBE_STATSA_MODULE:
	case GBE_STATSB_MODULE:
		val &= ~GBE_STATS_CD_SEL;
		break;
	case GBE_STATSC_MODULE:
	case GBE_STATSD_MODULE:
		val |= GBE_STATS_CD_SEL;
		break;
	default:
		return;
	}

	/* make the stat module visible */
	writel(val, GBE_REG_ADDR(gbe_dev, switch_regs, stat_port_en));
}

static void gbe_reset_mod_stats_ver14(struct gbe_priv *gbe_dev, int stats_mod)
{
	gbe_stats_mod_visible_ver14(gbe_dev, stats_mod);
	gbe_reset_mod_stats(gbe_dev, stats_mod);
}

static void gbe_update_stats_ver14(struct gbe_priv *gbe_dev, uint64_t *data)
{
	u32 half_num_et_stats = (gbe_dev->num_et_stats / 2);
	int et_entry, j, pair;

	for (pair = 0; pair < 2; pair++) {
		gbe_stats_mod_visible_ver14(gbe_dev, (pair ?
						      GBE_STATSC_MODULE :
						      GBE_STATSA_MODULE));

		for (j = 0; j < half_num_et_stats; j++) {
			et_entry = pair * half_num_et_stats + j;
			gbe_update_hw_stats_entry(gbe_dev, et_entry);

			if (data)
				data[et_entry] = gbe_dev->hw_stats[et_entry];
		}
	}
}

static void keystone_get_ethtool_stats(struct net_device *ndev,
				       struct ethtool_stats *stats,
				       uint64_t *data)
{
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct gbe_intf *gbe_intf;
	struct gbe_priv *gbe_dev;

	gbe_intf = keystone_get_intf_data(netcp);
	if (!gbe_intf)
		return;

	gbe_dev = gbe_intf->gbe_dev;
	spin_lock_bh(&gbe_dev->hw_stats_lock);
	if (IS_SS_ID_VER_14(gbe_dev))
		gbe_update_stats_ver14(gbe_dev, data);
	else
		gbe_update_stats(gbe_dev, data);
	spin_unlock_bh(&gbe_dev->hw_stats_lock);
}

static int keystone_get_link_ksettings(struct net_device *ndev,
				       struct ethtool_link_ksettings *cmd)
{
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct phy_device *phy = ndev->phydev;
	struct gbe_intf *gbe_intf;

	if (!phy)
		return -EINVAL;

	gbe_intf = keystone_get_intf_data(netcp);
	if (!gbe_intf)
		return -EINVAL;

	if (!gbe_intf->slave)
		return -EINVAL;

	phy_ethtool_ksettings_get(phy, cmd);
	cmd->base.port = gbe_intf->slave->phy_port_t;

	return 0;
}

static int keystone_set_link_ksettings(struct net_device *ndev,
				       const struct ethtool_link_ksettings *cmd)
{
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct phy_device *phy = ndev->phydev;
	struct gbe_intf *gbe_intf;
	u8 port = cmd->base.port;
	u32 advertising, supported;
	u32 features;

	ethtool_convert_link_mode_to_legacy_u32(&advertising,
						cmd->link_modes.advertising);
	ethtool_convert_link_mode_to_legacy_u32(&supported,
						cmd->link_modes.supported);
	features = advertising & supported;

	if (!phy)
		return -EINVAL;

	gbe_intf = keystone_get_intf_data(netcp);
	if (!gbe_intf)
		return -EINVAL;

	if (!gbe_intf->slave)
		return -EINVAL;

	if (port != gbe_intf->slave->phy_port_t) {
		if ((port == PORT_TP) && !(features & ADVERTISED_TP))
			return -EINVAL;

		if ((port == PORT_AUI) && !(features & ADVERTISED_AUI))
			return -EINVAL;

		if ((port == PORT_BNC) && !(features & ADVERTISED_BNC))
			return -EINVAL;

		if ((port == PORT_MII) && !(features & ADVERTISED_MII))
			return -EINVAL;

		if ((port == PORT_FIBRE) && !(features & ADVERTISED_FIBRE))
			return -EINVAL;
	}

	gbe_intf->slave->phy_port_t = port;
	return phy_ethtool_ksettings_set(phy, cmd);
}

#if IS_ENABLED(CONFIG_TI_CPTS)
static int keystone_get_ts_info(struct net_device *ndev,
				struct ethtool_ts_info *info)
{
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct gbe_intf *gbe_intf;

	gbe_intf = netcp_module_get_intf_data(&gbe_module, netcp);
	if (!gbe_intf || !gbe_intf->gbe_dev->cpts)
		return -EINVAL;

	info->so_timestamping =
		SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_TX_SOFTWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RX_SOFTWARE |
		SOF_TIMESTAMPING_SOFTWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;
	info->phc_index = gbe_intf->gbe_dev->cpts->phc_index;
	info->tx_types =
		(1 << HWTSTAMP_TX_OFF) |
		(1 << HWTSTAMP_TX_ON);
	info->rx_filters =
		(1 << HWTSTAMP_FILTER_NONE) |
		(1 << HWTSTAMP_FILTER_PTP_V1_L4_EVENT) |
		(1 << HWTSTAMP_FILTER_PTP_V2_EVENT);
	return 0;
}
#else
static int keystone_get_ts_info(struct net_device *ndev,
				struct ethtool_ts_info *info)
{
	info->so_timestamping =
		SOF_TIMESTAMPING_TX_SOFTWARE |
		SOF_TIMESTAMPING_RX_SOFTWARE |
		SOF_TIMESTAMPING_SOFTWARE;
	info->phc_index = -1;
	info->tx_types = 0;
	info->rx_filters = 0;
	return 0;
}
#endif /* CONFIG_TI_CPTS */

static const struct ethtool_ops keystone_ethtool_ops = {
	.get_drvinfo		= keystone_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_msglevel		= keystone_get_msglevel,
	.set_msglevel		= keystone_set_msglevel,
	.get_strings		= keystone_get_stat_strings,
	.get_sset_count		= keystone_get_sset_count,
	.get_ethtool_stats	= keystone_get_ethtool_stats,
	.get_link_ksettings	= keystone_get_link_ksettings,
	.set_link_ksettings	= keystone_set_link_ksettings,
	.get_ts_info		= keystone_get_ts_info,
};

static void gbe_set_slave_mac(struct gbe_slave *slave,
			      struct gbe_intf *gbe_intf)
{
	struct net_device *ndev = gbe_intf->ndev;

	writel(mac_hi(ndev->dev_addr), GBE_REG_ADDR(slave, port_regs, sa_hi));
	writel(mac_lo(ndev->dev_addr), GBE_REG_ADDR(slave, port_regs, sa_lo));
}

static int gbe_get_slave_port(struct gbe_priv *priv, u32 slave_num)
{
	if (priv->host_port == 0)
		return slave_num + 1;

	return slave_num;
}

static void netcp_ethss_link_state_action(struct gbe_priv *gbe_dev,
					  struct net_device *ndev,
					  struct gbe_slave *slave,
					  int up)
{
	struct phy_device *phy = slave->phy;
	u32 mac_control = 0;

	if (up) {
		mac_control = slave->mac_control;
		if (phy && (phy->speed == SPEED_1000)) {
			mac_control |= MACSL_GIG_MODE;
			mac_control &= ~MACSL_XGIG_MODE;
		} else if (phy && (phy->speed == SPEED_10000)) {
			mac_control |= MACSL_XGIG_MODE;
			mac_control &= ~MACSL_GIG_MODE;
		}

		writel(mac_control, GBE_REG_ADDR(slave, emac_regs,
						 mac_control));

		cpsw_ale_control_set(gbe_dev->ale, slave->port_num,
				     ALE_PORT_STATE,
				     ALE_PORT_STATE_FORWARD);

		if (ndev && slave->open &&
		    ((slave->link_interface != SGMII_LINK_MAC_PHY) &&
		    (slave->link_interface != RGMII_LINK_MAC_PHY) &&
		    (slave->link_interface != XGMII_LINK_MAC_PHY)))
			netif_carrier_on(ndev);
	} else {
		writel(mac_control, GBE_REG_ADDR(slave, emac_regs,
						 mac_control));
		cpsw_ale_control_set(gbe_dev->ale, slave->port_num,
				     ALE_PORT_STATE,
				     ALE_PORT_STATE_DISABLE);
		if (ndev &&
		    ((slave->link_interface != SGMII_LINK_MAC_PHY) &&
		    (slave->link_interface != RGMII_LINK_MAC_PHY) &&
		    (slave->link_interface != XGMII_LINK_MAC_PHY)))
			netif_carrier_off(ndev);
	}

	if (phy)
		phy_print_status(phy);
}

static bool gbe_phy_link_status(struct gbe_slave *slave)
{
	 return !slave->phy || slave->phy->link;
}

#define RGMII_REG_STATUS_LINK	BIT(0)

static void netcp_2u_rgmii_get_port_link(struct gbe_priv *gbe_dev, bool *status)
{
	u32 val = 0;

	val = readl(GBE_REG_ADDR(gbe_dev, ss_regs, rgmii_status));
	*status = !!(val & RGMII_REG_STATUS_LINK);
}

static void netcp_ethss_update_link_state(struct gbe_priv *gbe_dev,
					  struct gbe_slave *slave,
					  struct net_device *ndev)
{
	bool sw_link_state = true, phy_link_state;
	int sp = slave->slave_num, link_state;

	if (!slave->open)
		return;

	if (SLAVE_LINK_IS_RGMII(slave))
		netcp_2u_rgmii_get_port_link(gbe_dev,
					     &sw_link_state);
	if (SLAVE_LINK_IS_SGMII(slave))
		sw_link_state =
		netcp_sgmii_get_port_link(SGMII_BASE(gbe_dev, sp), sp);

	phy_link_state = gbe_phy_link_status(slave);
	link_state = phy_link_state & sw_link_state;

	if (atomic_xchg(&slave->link_state, link_state) != link_state)
		netcp_ethss_link_state_action(gbe_dev, ndev, slave,
					      link_state);
}

static void xgbe_adjust_link(struct net_device *ndev)
{
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct gbe_intf *gbe_intf;

	gbe_intf = netcp_module_get_intf_data(&xgbe_module, netcp);
	if (!gbe_intf)
		return;

	netcp_ethss_update_link_state(gbe_intf->gbe_dev, gbe_intf->slave,
				      ndev);
}

static void gbe_adjust_link(struct net_device *ndev)
{
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct gbe_intf *gbe_intf;

	gbe_intf = netcp_module_get_intf_data(&gbe_module, netcp);
	if (!gbe_intf)
		return;

	netcp_ethss_update_link_state(gbe_intf->gbe_dev, gbe_intf->slave,
				      ndev);
}

static void gbe_adjust_link_sec_slaves(struct net_device *ndev)
{
	struct gbe_priv *gbe_dev = netdev_priv(ndev);
	struct gbe_slave *slave;

	for_each_sec_slave(slave, gbe_dev)
		netcp_ethss_update_link_state(gbe_dev, slave, NULL);
}

/* Reset EMAC
 * Soft reset is set and polled until clear, or until a timeout occurs
 */
static int gbe_port_reset(struct gbe_slave *slave)
{
	u32 i, v;

	/* Set the soft reset bit */
	writel(SOFT_RESET, GBE_REG_ADDR(slave, emac_regs, soft_reset));

	/* Wait for the bit to clear */
	for (i = 0; i < DEVICE_EMACSL_RESET_POLL_COUNT; i++) {
		v = readl(GBE_REG_ADDR(slave, emac_regs, soft_reset));
		if ((v & SOFT_RESET_MASK) != SOFT_RESET)
			return 0;
	}

	/* Timeout on the reset */
	return GMACSL_RET_WARN_RESET_INCOMPLETE;
}

/* Configure EMAC */
static void gbe_port_config(struct gbe_priv *gbe_dev, struct gbe_slave *slave,
			    int max_rx_len)
{
	void __iomem *rx_maxlen_reg;
	u32 xgmii_mode;

	if (max_rx_len > NETCP_MAX_FRAME_SIZE)
		max_rx_len = NETCP_MAX_FRAME_SIZE;

	/* Enable correct MII mode at SS level */
	if (IS_SS_ID_XGBE(gbe_dev) &&
	    (slave->link_interface >= XGMII_LINK_MAC_PHY)) {
		xgmii_mode = readl(GBE_REG_ADDR(gbe_dev, ss_regs, control));
		xgmii_mode |= (1 << slave->slave_num);
		writel(xgmii_mode, GBE_REG_ADDR(gbe_dev, ss_regs, control));
	}

	if (IS_SS_ID_MU(gbe_dev))
		rx_maxlen_reg = GBE_REG_ADDR(slave, port_regs, rx_maxlen);
	else
		rx_maxlen_reg = GBE_REG_ADDR(slave, emac_regs, rx_maxlen);

	writel(max_rx_len, rx_maxlen_reg);
	writel(slave->mac_control, GBE_REG_ADDR(slave, emac_regs, mac_control));
}

static void gbe_sgmii_rtreset(struct gbe_priv *priv,
			      struct gbe_slave *slave, bool set)
{
	if (SLAVE_LINK_IS_XGMII(slave))
		return;

	netcp_sgmii_rtreset(SGMII_BASE(priv, slave->slave_num),
			    slave->slave_num, set);
}

static void gbe_slave_stop(struct gbe_intf *intf)
{
	struct gbe_priv *gbe_dev = intf->gbe_dev;
	struct gbe_slave *slave = intf->slave;

	if (!IS_SS_ID_2U(gbe_dev))
		gbe_sgmii_rtreset(gbe_dev, slave, true);
	gbe_port_reset(slave);
	/* Disable forwarding */
	cpsw_ale_control_set(gbe_dev->ale, slave->port_num,
			     ALE_PORT_STATE, ALE_PORT_STATE_DISABLE);
	cpsw_ale_del_mcast(gbe_dev->ale, intf->ndev->broadcast,
			   1 << slave->port_num, 0, 0);

	if (!slave->phy)
		return;

	phy_stop(slave->phy);
	phy_disconnect(slave->phy);
	slave->phy = NULL;
}

static void gbe_sgmii_config(struct gbe_priv *priv, struct gbe_slave *slave)
{
	if (SLAVE_LINK_IS_XGMII(slave))
		return;

	netcp_sgmii_reset(SGMII_BASE(priv, slave->slave_num), slave->slave_num);
	netcp_sgmii_config(SGMII_BASE(priv, slave->slave_num), slave->slave_num,
			   slave->link_interface);
}

static int gbe_slave_open(struct gbe_intf *gbe_intf)
{
	struct gbe_priv *priv = gbe_intf->gbe_dev;
	struct gbe_slave *slave = gbe_intf->slave;
	phy_interface_t phy_mode;
	bool has_phy = false;

	void (*hndlr)(struct net_device *) = gbe_adjust_link;

	if (!IS_SS_ID_2U(priv))
		gbe_sgmii_config(priv, slave);
	gbe_port_reset(slave);
	if (!IS_SS_ID_2U(priv))
		gbe_sgmii_rtreset(priv, slave, false);
	gbe_port_config(priv, slave, priv->rx_packet_max);
	gbe_set_slave_mac(slave, gbe_intf);
	/* For NU & 2U switch, map the vlan priorities to zero
	 * as we only configure to use priority 0
	 */
	if (IS_SS_ID_MU(priv))
		writel(HOST_TX_PRI_MAP_DEFAULT,
		       GBE_REG_ADDR(slave, port_regs, rx_pri_map));

	/* enable forwarding */
	cpsw_ale_control_set(priv->ale, slave->port_num,
			     ALE_PORT_STATE, ALE_PORT_STATE_FORWARD);
	cpsw_ale_add_mcast(priv->ale, gbe_intf->ndev->broadcast,
			   1 << slave->port_num, 0, 0, ALE_MCAST_FWD_2);

	if (slave->link_interface == SGMII_LINK_MAC_PHY) {
		has_phy = true;
		phy_mode = PHY_INTERFACE_MODE_SGMII;
		slave->phy_port_t = PORT_MII;
	} else if (slave->link_interface == RGMII_LINK_MAC_PHY) {
		has_phy = true;
		phy_mode = of_get_phy_mode(slave->node);
		/* if phy-mode is not present, default to
		 * PHY_INTERFACE_MODE_RGMII
		 */
		if (phy_mode < 0)
			phy_mode = PHY_INTERFACE_MODE_RGMII;

		if (!phy_interface_mode_is_rgmii(phy_mode)) {
			dev_err(priv->dev,
				"Unsupported phy mode %d\n", phy_mode);
			return -EINVAL;
		}
		slave->phy_port_t = PORT_MII;
	} else if (slave->link_interface == XGMII_LINK_MAC_PHY) {
		has_phy = true;
		phy_mode = PHY_INTERFACE_MODE_NA;
		slave->phy_port_t = PORT_FIBRE;
	}

	if (has_phy) {
		if (IS_SS_ID_XGBE(priv))
			hndlr = xgbe_adjust_link;

		slave->phy = of_phy_connect(gbe_intf->ndev,
					    slave->phy_node,
					    hndlr, 0,
					    phy_mode);
		if (!slave->phy) {
			dev_err(priv->dev, "phy not found on slave %d\n",
				slave->slave_num);
			return -ENODEV;
		}
		dev_dbg(priv->dev, "phy found: id is: 0x%s\n",
			phydev_name(slave->phy));
		phy_start(slave->phy);
	}
	return 0;
}

static void gbe_init_host_port(struct gbe_priv *priv)
{
	int bypass_en = 1;

	/* Host Tx Pri */
	if (IS_SS_ID_NU(priv) || IS_SS_ID_XGBE(priv))
		writel(HOST_TX_PRI_MAP_DEFAULT,
		       GBE_REG_ADDR(priv, host_port_regs, tx_pri_map));

	/* Max length register */
	writel(NETCP_MAX_FRAME_SIZE, GBE_REG_ADDR(priv, host_port_regs,
						  rx_maxlen));

	cpsw_ale_start(priv->ale);

	if (priv->enable_ale)
		bypass_en = 0;

	cpsw_ale_control_set(priv->ale, 0, ALE_BYPASS, bypass_en);

	cpsw_ale_control_set(priv->ale, 0, ALE_NO_PORT_VLAN, 1);

	cpsw_ale_control_set(priv->ale, priv->host_port,
			     ALE_PORT_STATE, ALE_PORT_STATE_FORWARD);

	cpsw_ale_control_set(priv->ale, 0,
			     ALE_PORT_UNKNOWN_VLAN_MEMBER,
			     GBE_PORT_MASK(priv->ale_ports));

	cpsw_ale_control_set(priv->ale, 0,
			     ALE_PORT_UNKNOWN_MCAST_FLOOD,
			     GBE_PORT_MASK(priv->ale_ports - 1));

	cpsw_ale_control_set(priv->ale, 0,
			     ALE_PORT_UNKNOWN_REG_MCAST_FLOOD,
			     GBE_PORT_MASK(priv->ale_ports));

	cpsw_ale_control_set(priv->ale, 0,
			     ALE_PORT_UNTAGGED_EGRESS,
			     GBE_PORT_MASK(priv->ale_ports));
}

static void gbe_add_mcast_addr(struct gbe_intf *gbe_intf, u8 *addr)
{
	struct gbe_priv *gbe_dev = gbe_intf->gbe_dev;
	u16 vlan_id;

	cpsw_ale_add_mcast(gbe_dev->ale, addr,
			   GBE_PORT_MASK(gbe_dev->ale_ports), 0, 0,
			   ALE_MCAST_FWD_2);
	for_each_set_bit(vlan_id, gbe_intf->active_vlans, VLAN_N_VID) {
		cpsw_ale_add_mcast(gbe_dev->ale, addr,
				   GBE_PORT_MASK(gbe_dev->ale_ports),
				   ALE_VLAN, vlan_id, ALE_MCAST_FWD_2);
	}
}

static void gbe_add_ucast_addr(struct gbe_intf *gbe_intf, u8 *addr)
{
	struct gbe_priv *gbe_dev = gbe_intf->gbe_dev;
	u16 vlan_id;

	cpsw_ale_add_ucast(gbe_dev->ale, addr, gbe_dev->host_port, 0, 0);

	for_each_set_bit(vlan_id, gbe_intf->active_vlans, VLAN_N_VID)
		cpsw_ale_add_ucast(gbe_dev->ale, addr, gbe_dev->host_port,
				   ALE_VLAN, vlan_id);
}

static void gbe_del_mcast_addr(struct gbe_intf *gbe_intf, u8 *addr)
{
	struct gbe_priv *gbe_dev = gbe_intf->gbe_dev;
	u16 vlan_id;

	cpsw_ale_del_mcast(gbe_dev->ale, addr, 0, 0, 0);

	for_each_set_bit(vlan_id, gbe_intf->active_vlans, VLAN_N_VID) {
		cpsw_ale_del_mcast(gbe_dev->ale, addr, 0, ALE_VLAN, vlan_id);
	}
}

static void gbe_del_ucast_addr(struct gbe_intf *gbe_intf, u8 *addr)
{
	struct gbe_priv *gbe_dev = gbe_intf->gbe_dev;
	u16 vlan_id;

	cpsw_ale_del_ucast(gbe_dev->ale, addr, gbe_dev->host_port, 0, 0);

	for_each_set_bit(vlan_id, gbe_intf->active_vlans, VLAN_N_VID) {
		cpsw_ale_del_ucast(gbe_dev->ale, addr, gbe_dev->host_port,
				   ALE_VLAN, vlan_id);
	}
}

static int gbe_add_addr(void *intf_priv, struct netcp_addr *naddr)
{
	struct gbe_intf *gbe_intf = intf_priv;
	struct gbe_priv *gbe_dev = gbe_intf->gbe_dev;

	dev_dbg(gbe_dev->dev, "ethss adding address %pM, type %d\n",
		naddr->addr, naddr->type);

	switch (naddr->type) {
	case ADDR_MCAST:
	case ADDR_BCAST:
		gbe_add_mcast_addr(gbe_intf, naddr->addr);
		break;
	case ADDR_UCAST:
	case ADDR_DEV:
		gbe_add_ucast_addr(gbe_intf, naddr->addr);
		break;
	case ADDR_ANY:
		/* nothing to do for promiscuous */
	default:
		break;
	}

	return 0;
}

static int gbe_del_addr(void *intf_priv, struct netcp_addr *naddr)
{
	struct gbe_intf *gbe_intf = intf_priv;
	struct gbe_priv *gbe_dev = gbe_intf->gbe_dev;

	dev_dbg(gbe_dev->dev, "ethss deleting address %pM, type %d\n",
		naddr->addr, naddr->type);

	switch (naddr->type) {
	case ADDR_MCAST:
	case ADDR_BCAST:
		gbe_del_mcast_addr(gbe_intf, naddr->addr);
		break;
	case ADDR_UCAST:
	case ADDR_DEV:
		gbe_del_ucast_addr(gbe_intf, naddr->addr);
		break;
	case ADDR_ANY:
		/* nothing to do for promiscuous */
	default:
		break;
	}

	return 0;
}

static int gbe_add_vid(void *intf_priv, int vid)
{
	struct gbe_intf *gbe_intf = intf_priv;
	struct gbe_priv *gbe_dev = gbe_intf->gbe_dev;

	set_bit(vid, gbe_intf->active_vlans);

	cpsw_ale_add_vlan(gbe_dev->ale, vid,
			  GBE_PORT_MASK(gbe_dev->ale_ports),
			  GBE_MASK_NO_PORTS,
			  GBE_PORT_MASK(gbe_dev->ale_ports),
			  GBE_PORT_MASK(gbe_dev->ale_ports - 1));

	return 0;
}

static int gbe_del_vid(void *intf_priv, int vid)
{
	struct gbe_intf *gbe_intf = intf_priv;
	struct gbe_priv *gbe_dev = gbe_intf->gbe_dev;

	cpsw_ale_del_vlan(gbe_dev->ale, vid, 0);
	clear_bit(vid, gbe_intf->active_vlans);
	return 0;
}

#if IS_ENABLED(CONFIG_TI_CPTS)
#define HAS_PHY_TXTSTAMP(p) ((p)->drv && (p)->drv->txtstamp)
#define HAS_PHY_RXTSTAMP(p) ((p)->drv && (p)->drv->rxtstamp)

static void gbe_txtstamp(void *context, struct sk_buff *skb)
{
	struct gbe_intf *gbe_intf = context;
	struct gbe_priv *gbe_dev = gbe_intf->gbe_dev;

	cpts_tx_timestamp(gbe_dev->cpts, skb);
}

static bool gbe_need_txtstamp(struct gbe_intf *gbe_intf,
			      const struct netcp_packet *p_info)
{
	struct sk_buff *skb = p_info->skb;

	return cpts_can_timestamp(gbe_intf->gbe_dev->cpts, skb);
}

static int gbe_txtstamp_mark_pkt(struct gbe_intf *gbe_intf,
				 struct netcp_packet *p_info)
{
	struct phy_device *phydev = p_info->skb->dev->phydev;
	struct gbe_priv *gbe_dev = gbe_intf->gbe_dev;

	if (!(skb_shinfo(p_info->skb)->tx_flags & SKBTX_HW_TSTAMP) ||
	    !gbe_dev->tx_ts_enabled)
		return 0;

	/* If phy has the txtstamp api, assume it will do it.
	 * We mark it here because skb_tx_timestamp() is called
	 * after all the txhooks are called.
	 */
	if (phydev && HAS_PHY_TXTSTAMP(phydev)) {
		skb_shinfo(p_info->skb)->tx_flags |= SKBTX_IN_PROGRESS;
		return 0;
	}

	if (gbe_need_txtstamp(gbe_intf, p_info)) {
		p_info->txtstamp = gbe_txtstamp;
		p_info->ts_context = (void *)gbe_intf;
		skb_shinfo(p_info->skb)->tx_flags |= SKBTX_IN_PROGRESS;
	}

	return 0;
}

static int gbe_rxtstamp(struct gbe_intf *gbe_intf, struct netcp_packet *p_info)
{
	struct phy_device *phydev = p_info->skb->dev->phydev;
	struct gbe_priv *gbe_dev = gbe_intf->gbe_dev;

	if (p_info->rxtstamp_complete)
		return 0;

	if (phydev && HAS_PHY_RXTSTAMP(phydev)) {
		p_info->rxtstamp_complete = true;
		return 0;
	}

	if (gbe_dev->rx_ts_enabled)
		cpts_rx_timestamp(gbe_dev->cpts, p_info->skb);

	p_info->rxtstamp_complete = true;

	return 0;
}

static int gbe_hwtstamp_get(struct gbe_intf *gbe_intf, struct ifreq *ifr)
{
	struct gbe_priv *gbe_dev = gbe_intf->gbe_dev;
	struct cpts *cpts = gbe_dev->cpts;
	struct hwtstamp_config cfg;

	if (!cpts)
		return -EOPNOTSUPP;

	cfg.flags = 0;
	cfg.tx_type = gbe_dev->tx_ts_enabled ? HWTSTAMP_TX_ON : HWTSTAMP_TX_OFF;
	cfg.rx_filter = gbe_dev->rx_ts_enabled;

	return copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)) ? -EFAULT : 0;
}

static void gbe_hwtstamp(struct gbe_intf *gbe_intf)
{
	struct gbe_priv *gbe_dev = gbe_intf->gbe_dev;
	struct gbe_slave *slave = gbe_intf->slave;
	u32 ts_en, seq_id, ctl;

	if (!gbe_dev->rx_ts_enabled &&
	    !gbe_dev->tx_ts_enabled) {
		writel(0, GBE_REG_ADDR(slave, port_regs, ts_ctl));
		return;
	}

	seq_id = (30 << TS_SEQ_ID_OFS_SHIFT) | ETH_P_1588;
	ts_en = EVENT_MSG_BITS << TS_MSG_TYPE_EN_SHIFT;
	ctl = ETH_P_1588 | TS_TTL_NONZERO |
		(slave->ts_ctl.dst_port_map << TS_CTL_DST_PORT_SHIFT) |
		(slave->ts_ctl.uni ?  TS_UNI_EN :
			slave->ts_ctl.maddr_map << TS_CTL_MADDR_SHIFT);

	if (gbe_dev->tx_ts_enabled)
		ts_en |= (TS_TX_ANX_ALL_EN | TS_TX_VLAN_LT1_EN);

	if (gbe_dev->rx_ts_enabled)
		ts_en |= (TS_RX_ANX_ALL_EN | TS_RX_VLAN_LT1_EN);

	writel(ts_en,  GBE_REG_ADDR(slave, port_regs, ts_ctl));
	writel(seq_id, GBE_REG_ADDR(slave, port_regs, ts_seq_ltype));
	writel(ctl,    GBE_REG_ADDR(slave, port_regs, ts_ctl_ltype2));
}

static int gbe_hwtstamp_set(struct gbe_intf *gbe_intf, struct ifreq *ifr)
{
	struct gbe_priv *gbe_dev = gbe_intf->gbe_dev;
	struct cpts *cpts = gbe_dev->cpts;
	struct hwtstamp_config cfg;

	if (!cpts)
		return -EOPNOTSUPP;

	if (copy_from_user(&cfg, ifr->ifr_data, sizeof(cfg)))
		return -EFAULT;

	/* reserved for future extensions */
	if (cfg.flags)
		return -EINVAL;

	switch (cfg.tx_type) {
	case HWTSTAMP_TX_OFF:
		gbe_dev->tx_ts_enabled = 0;
		break;
	case HWTSTAMP_TX_ON:
		gbe_dev->tx_ts_enabled = 1;
		break;
	default:
		return -ERANGE;
	}

	switch (cfg.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		gbe_dev->rx_ts_enabled = HWTSTAMP_FILTER_NONE;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		gbe_dev->rx_ts_enabled = HWTSTAMP_FILTER_PTP_V1_L4_EVENT;
		cfg.rx_filter = HWTSTAMP_FILTER_PTP_V1_L4_EVENT;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		gbe_dev->rx_ts_enabled = HWTSTAMP_FILTER_PTP_V2_EVENT;
		cfg.rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		break;
	default:
		return -ERANGE;
	}

	gbe_hwtstamp(gbe_intf);

	return copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)) ? -EFAULT : 0;
}

static void gbe_register_cpts(struct gbe_priv *gbe_dev)
{
	if (!gbe_dev->cpts)
		return;

	if (gbe_dev->cpts_registered > 0)
		goto done;

	if (cpts_register(gbe_dev->cpts)) {
		dev_err(gbe_dev->dev, "error registering cpts device\n");
		return;
	}

done:
	++gbe_dev->cpts_registered;
}

static void gbe_unregister_cpts(struct gbe_priv *gbe_dev)
{
	if (!gbe_dev->cpts || (gbe_dev->cpts_registered <= 0))
		return;

	if (--gbe_dev->cpts_registered)
		return;

	cpts_unregister(gbe_dev->cpts);
}
#else
static inline int gbe_txtstamp_mark_pkt(struct gbe_intf *gbe_intf,
					struct netcp_packet *p_info)
{
	return 0;
}

static inline int gbe_rxtstamp(struct gbe_intf *gbe_intf,
			       struct netcp_packet *p_info)
{
	return 0;
}

static inline int gbe_hwtstamp(struct gbe_intf *gbe_intf,
			       struct ifreq *ifr, int cmd)
{
	return -EOPNOTSUPP;
}

static inline void gbe_register_cpts(struct gbe_priv *gbe_dev)
{
}

static inline void gbe_unregister_cpts(struct gbe_priv *gbe_dev)
{
}

static inline int gbe_hwtstamp_get(struct gbe_intf *gbe_intf, struct ifreq *req)
{
	return -EOPNOTSUPP;
}

static inline int gbe_hwtstamp_set(struct gbe_intf *gbe_intf, struct ifreq *req)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_TI_CPTS */

static int gbe_set_rx_mode(void *intf_priv, bool promisc)
{
	struct gbe_intf *gbe_intf = intf_priv;
	struct gbe_priv *gbe_dev = gbe_intf->gbe_dev;
	struct cpsw_ale *ale = gbe_dev->ale;
	unsigned long timeout;
	int i, ret = -ETIMEDOUT;

	/* Disable(1)/Enable(0) Learn for all ports (host is port 0 and
	 * slaves are port 1 and up
	 */
	for (i = 0; i <= gbe_dev->num_slaves; i++) {
		cpsw_ale_control_set(ale, i,
				     ALE_PORT_NOLEARN, !!promisc);
		cpsw_ale_control_set(ale, i,
				     ALE_PORT_NO_SA_UPDATE, !!promisc);
	}

	if (!promisc) {
		/* Don't Flood All Unicast Packets to Host port */
		cpsw_ale_control_set(ale, 0, ALE_P0_UNI_FLOOD, 0);
		dev_vdbg(gbe_dev->dev, "promiscuous mode disabled\n");
		return 0;
	}

	timeout = jiffies + HZ;

	/* Clear All Untouched entries */
	cpsw_ale_control_set(ale, 0, ALE_AGEOUT, 1);
	do {
		cpu_relax();
		if (cpsw_ale_control_get(ale, 0, ALE_AGEOUT)) {
			ret = 0;
			break;
		}

	} while (time_after(timeout, jiffies));

	/* Make sure it is not a false timeout */
	if (ret && !cpsw_ale_control_get(ale, 0, ALE_AGEOUT))
		return ret;

	cpsw_ale_control_set(ale, 0, ALE_AGEOUT, 1);

	/* Clear all mcast from ALE */
	cpsw_ale_flush_multicast(ale,
				 GBE_PORT_MASK(gbe_dev->ale_ports),
				 -1);

	/* Flood All Unicast Packets to Host port */
	cpsw_ale_control_set(ale, 0, ALE_P0_UNI_FLOOD, 1);
	dev_vdbg(gbe_dev->dev, "promiscuous mode enabled\n");
	return ret;
}

static int gbe_ioctl(void *intf_priv, struct ifreq *req, int cmd)
{
	struct gbe_intf *gbe_intf = intf_priv;
	struct phy_device *phy = gbe_intf->slave->phy;

	if (!phy || !phy->drv->hwtstamp) {
		switch (cmd) {
		case SIOCGHWTSTAMP:
			return gbe_hwtstamp_get(gbe_intf, req);
		case SIOCSHWTSTAMP:
			return gbe_hwtstamp_set(gbe_intf, req);
		}
	}

	if (phy)
		return phy_mii_ioctl(phy, req, cmd);

	return -EOPNOTSUPP;
}

static void netcp_ethss_timer(struct timer_list *t)
{
	struct gbe_priv *gbe_dev = from_timer(gbe_dev, t, timer);
	struct gbe_intf *gbe_intf;
	struct gbe_slave *slave;

	/* Check & update SGMII link state of interfaces */
	for_each_intf(gbe_intf, gbe_dev) {
		if (!gbe_intf->slave->open)
			continue;
		netcp_ethss_update_link_state(gbe_dev, gbe_intf->slave,
					      gbe_intf->ndev);
	}

	/* Check & update SGMII link state of secondary ports */
	for_each_sec_slave(slave, gbe_dev) {
		netcp_ethss_update_link_state(gbe_dev, slave, NULL);
	}

	/* A timer runs as a BH, no need to block them */
	spin_lock(&gbe_dev->hw_stats_lock);

	if (IS_SS_ID_VER_14(gbe_dev))
		gbe_update_stats_ver14(gbe_dev, NULL);
	else
		gbe_update_stats(gbe_dev, NULL);

	spin_unlock(&gbe_dev->hw_stats_lock);

	gbe_dev->timer.expires	= jiffies + GBE_TIMER_INTERVAL;
	add_timer(&gbe_dev->timer);
}

static int gbe_txhook(int order, void *data, struct netcp_packet *p_info)
{
	struct gbe_intf *gbe_intf = data;

	p_info->tx_pipe = &gbe_intf->tx_pipe;

	return gbe_txtstamp_mark_pkt(gbe_intf, p_info);
}

static int gbe_rxhook(int order, void *data, struct netcp_packet *p_info)
{
	struct gbe_intf *gbe_intf = data;

	return gbe_rxtstamp(gbe_intf, p_info);
}

static int gbe_open(void *intf_priv, struct net_device *ndev)
{
	struct gbe_intf *gbe_intf = intf_priv;
	struct gbe_priv *gbe_dev = gbe_intf->gbe_dev;
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct gbe_slave *slave = gbe_intf->slave;
	int port_num = slave->port_num;
	u32 reg, val;
	int ret;

	reg = readl(GBE_REG_ADDR(gbe_dev, switch_regs, id_ver));
	dev_dbg(gbe_dev->dev, "initializing gbe version %d.%d (%d) GBE identification value 0x%x\n",
		GBE_MAJOR_VERSION(reg), GBE_MINOR_VERSION(reg),
		GBE_RTL_VERSION(reg), GBE_IDENT(reg));

	/* For 10G and on NetCP 1.5, use directed to port */
	if (IS_SS_ID_XGBE(gbe_dev) || IS_SS_ID_MU(gbe_dev))
		gbe_intf->tx_pipe.flags = SWITCH_TO_PORT_IN_TAGINFO;

	if (gbe_dev->enable_ale)
		gbe_intf->tx_pipe.switch_to_port = 0;
	else
		gbe_intf->tx_pipe.switch_to_port = port_num;

	dev_dbg(gbe_dev->dev,
		"opened TX channel %s: %p with to port %d, flags %d\n",
		gbe_intf->tx_pipe.dma_chan_name,
		gbe_intf->tx_pipe.dma_channel,
		gbe_intf->tx_pipe.switch_to_port,
		gbe_intf->tx_pipe.flags);

	gbe_slave_stop(gbe_intf);

	/* disable priority elevation and enable statistics on all ports */
	writel(0, GBE_REG_ADDR(gbe_dev, switch_regs, ptype));

	/* Control register */
	val = GBE_CTL_P0_ENABLE;
	if (IS_SS_ID_MU(gbe_dev)) {
		val |= ETH_SW_CTL_P0_TX_CRC_REMOVE;
		netcp->hw_cap = ETH_SW_CAN_REMOVE_ETH_FCS;
	}
	writel(val, GBE_REG_ADDR(gbe_dev, switch_regs, control));

	/* All statistics enabled and STAT AB visible by default */
	writel(gbe_dev->stats_en_mask, GBE_REG_ADDR(gbe_dev, switch_regs,
						    stat_port_en));

	ret = gbe_slave_open(gbe_intf);
	if (ret)
		goto fail;

	netcp_register_txhook(netcp, GBE_TXHOOK_ORDER, gbe_txhook, gbe_intf);
	netcp_register_rxhook(netcp, GBE_RXHOOK_ORDER, gbe_rxhook, gbe_intf);

	slave->open = true;
	netcp_ethss_update_link_state(gbe_dev, slave, ndev);

	gbe_register_cpts(gbe_dev);

	return 0;

fail:
	gbe_slave_stop(gbe_intf);
	return ret;
}

static int gbe_close(void *intf_priv, struct net_device *ndev)
{
	struct gbe_intf *gbe_intf = intf_priv;
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct gbe_priv *gbe_dev = gbe_intf->gbe_dev;

	gbe_unregister_cpts(gbe_dev);

	gbe_slave_stop(gbe_intf);

	netcp_unregister_rxhook(netcp, GBE_RXHOOK_ORDER, gbe_rxhook, gbe_intf);
	netcp_unregister_txhook(netcp, GBE_TXHOOK_ORDER, gbe_txhook, gbe_intf);

	gbe_intf->slave->open = false;
	atomic_set(&gbe_intf->slave->link_state, NETCP_LINK_STATE_INVALID);
	return 0;
}

#if IS_ENABLED(CONFIG_TI_CPTS)
static void init_slave_ts_ctl(struct gbe_slave *slave)
{
	slave->ts_ctl.uni = 1;
	slave->ts_ctl.dst_port_map =
		(TS_CTL_DST_PORT >> TS_CTL_DST_PORT_SHIFT) & 0x3;
	slave->ts_ctl.maddr_map =
		(TS_CTL_MADDR_ALL >> TS_CTL_MADDR_SHIFT) & 0x1f;
}

#else
static void init_slave_ts_ctl(struct gbe_slave *slave)
{
}
#endif /* CONFIG_TI_CPTS */

static int init_slave(struct gbe_priv *gbe_dev, struct gbe_slave *slave,
		      struct device_node *node)
{
	int port_reg_num;
	u32 port_reg_ofs, emac_reg_ofs;
	u32 port_reg_blk_sz, emac_reg_blk_sz;

	if (of_property_read_u32(node, "slave-port", &slave->slave_num)) {
		dev_err(gbe_dev->dev, "missing slave-port parameter\n");
		return -EINVAL;
	}

	if (of_property_read_u32(node, "link-interface",
				 &slave->link_interface)) {
		dev_warn(gbe_dev->dev,
			 "missing link-interface value defaulting to 1G mac-phy link\n");
		slave->link_interface = SGMII_LINK_MAC_PHY;
	}

	slave->node = node;
	slave->open = false;
	if ((slave->link_interface == SGMII_LINK_MAC_PHY) ||
	    (slave->link_interface == RGMII_LINK_MAC_PHY) ||
	    (slave->link_interface == XGMII_LINK_MAC_PHY))
		slave->phy_node = of_parse_phandle(node, "phy-handle", 0);
	slave->port_num = gbe_get_slave_port(gbe_dev, slave->slave_num);

	if (slave->link_interface >= XGMII_LINK_MAC_PHY)
		slave->mac_control = GBE_DEF_10G_MAC_CONTROL;
	else
		slave->mac_control = GBE_DEF_1G_MAC_CONTROL;

	/* Emac regs memmap are contiguous but port regs are not */
	port_reg_num = slave->slave_num;
	if (IS_SS_ID_VER_14(gbe_dev)) {
		if (slave->slave_num > 1) {
			port_reg_ofs = GBE13_SLAVE_PORT2_OFFSET;
			port_reg_num -= 2;
		} else {
			port_reg_ofs = GBE13_SLAVE_PORT_OFFSET;
		}
		emac_reg_ofs = GBE13_EMAC_OFFSET;
		port_reg_blk_sz = 0x30;
		emac_reg_blk_sz = 0x40;
	} else if (IS_SS_ID_MU(gbe_dev)) {
		port_reg_ofs = GBENU_SLAVE_PORT_OFFSET;
		emac_reg_ofs = GBENU_EMAC_OFFSET;
		port_reg_blk_sz = 0x1000;
		emac_reg_blk_sz = 0x1000;
	} else if (IS_SS_ID_XGBE(gbe_dev)) {
		port_reg_ofs = XGBE10_SLAVE_PORT_OFFSET;
		emac_reg_ofs = XGBE10_EMAC_OFFSET;
		port_reg_blk_sz = 0x30;
		emac_reg_blk_sz = 0x40;
	} else {
		dev_err(gbe_dev->dev, "unknown ethss(0x%x)\n",
			gbe_dev->ss_version);
		return -EINVAL;
	}

	slave->port_regs = gbe_dev->switch_regs + port_reg_ofs +
				(port_reg_blk_sz * port_reg_num);
	slave->emac_regs = gbe_dev->switch_regs + emac_reg_ofs +
				(emac_reg_blk_sz * slave->slave_num);

	if (IS_SS_ID_VER_14(gbe_dev)) {
		/* Initialize  slave port register offsets */
		GBE_SET_REG_OFS(slave, port_regs, port_vlan);
		GBE_SET_REG_OFS(slave, port_regs, tx_pri_map);
		GBE_SET_REG_OFS(slave, port_regs, sa_lo);
		GBE_SET_REG_OFS(slave, port_regs, sa_hi);
		GBE_SET_REG_OFS(slave, port_regs, ts_ctl);
		GBE_SET_REG_OFS(slave, port_regs, ts_seq_ltype);
		GBE_SET_REG_OFS(slave, port_regs, ts_vlan);
		GBE_SET_REG_OFS(slave, port_regs, ts_ctl_ltype2);
		GBE_SET_REG_OFS(slave, port_regs, ts_ctl2);

		/* Initialize EMAC register offsets */
		GBE_SET_REG_OFS(slave, emac_regs, mac_control);
		GBE_SET_REG_OFS(slave, emac_regs, soft_reset);
		GBE_SET_REG_OFS(slave, emac_regs, rx_maxlen);

	} else if (IS_SS_ID_MU(gbe_dev)) {
		/* Initialize  slave port register offsets */
		GBENU_SET_REG_OFS(slave, port_regs, port_vlan);
		GBENU_SET_REG_OFS(slave, port_regs, tx_pri_map);
		GBENU_SET_REG_OFS(slave, port_regs, rx_pri_map);
		GBENU_SET_REG_OFS(slave, port_regs, sa_lo);
		GBENU_SET_REG_OFS(slave, port_regs, sa_hi);
		GBENU_SET_REG_OFS(slave, port_regs, ts_ctl);
		GBENU_SET_REG_OFS(slave, port_regs, ts_seq_ltype);
		GBENU_SET_REG_OFS(slave, port_regs, ts_vlan);
		GBENU_SET_REG_OFS(slave, port_regs, ts_ctl_ltype2);
		GBENU_SET_REG_OFS(slave, port_regs, ts_ctl2);
		GBENU_SET_REG_OFS(slave, port_regs, rx_maxlen);

		/* Initialize EMAC register offsets */
		GBENU_SET_REG_OFS(slave, emac_regs, mac_control);
		GBENU_SET_REG_OFS(slave, emac_regs, soft_reset);

	} else if (IS_SS_ID_XGBE(gbe_dev)) {
		/* Initialize  slave port register offsets */
		XGBE_SET_REG_OFS(slave, port_regs, port_vlan);
		XGBE_SET_REG_OFS(slave, port_regs, tx_pri_map);
		XGBE_SET_REG_OFS(slave, port_regs, sa_lo);
		XGBE_SET_REG_OFS(slave, port_regs, sa_hi);
		XGBE_SET_REG_OFS(slave, port_regs, ts_ctl);
		XGBE_SET_REG_OFS(slave, port_regs, ts_seq_ltype);
		XGBE_SET_REG_OFS(slave, port_regs, ts_vlan);
		XGBE_SET_REG_OFS(slave, port_regs, ts_ctl_ltype2);
		XGBE_SET_REG_OFS(slave, port_regs, ts_ctl2);

		/* Initialize EMAC register offsets */
		XGBE_SET_REG_OFS(slave, emac_regs, mac_control);
		XGBE_SET_REG_OFS(slave, emac_regs, soft_reset);
		XGBE_SET_REG_OFS(slave, emac_regs, rx_maxlen);
	}

	atomic_set(&slave->link_state, NETCP_LINK_STATE_INVALID);

	init_slave_ts_ctl(slave);
	return 0;
}

static void init_secondary_ports(struct gbe_priv *gbe_dev,
				 struct device_node *node)
{
	struct device *dev = gbe_dev->dev;
	phy_interface_t phy_mode;
	struct gbe_priv **priv;
	struct device_node *port;
	struct gbe_slave *slave;
	bool mac_phy_link = false;

	for_each_child_of_node(node, port) {
		slave = devm_kzalloc(dev, sizeof(*slave), GFP_KERNEL);
		if (!slave) {
			dev_err(dev, "memory alloc failed for secondary port(%pOFn), skipping...\n",
				port);
			continue;
		}

		if (init_slave(gbe_dev, slave, port)) {
			dev_err(dev,
				"Failed to initialize secondary port(%pOFn), skipping...\n",
				port);
			devm_kfree(dev, slave);
			continue;
		}

		if (!IS_SS_ID_2U(gbe_dev))
			gbe_sgmii_config(gbe_dev, slave);
		gbe_port_reset(slave);
		gbe_port_config(gbe_dev, slave, gbe_dev->rx_packet_max);
		list_add_tail(&slave->slave_list, &gbe_dev->secondary_slaves);
		gbe_dev->num_slaves++;
		if ((slave->link_interface == SGMII_LINK_MAC_PHY) ||
		    (slave->link_interface == XGMII_LINK_MAC_PHY))
			mac_phy_link = true;

		slave->open = true;
		if (gbe_dev->num_slaves >= gbe_dev->max_num_slaves) {
			of_node_put(port);
			break;
		}
	}

	/* of_phy_connect() is needed only for MAC-PHY interface */
	if (!mac_phy_link)
		return;

	/* Allocate dummy netdev device for attaching to phy device */
	gbe_dev->dummy_ndev = alloc_netdev(sizeof(gbe_dev), "dummy",
					NET_NAME_UNKNOWN, ether_setup);
	if (!gbe_dev->dummy_ndev) {
		dev_err(dev,
			"Failed to allocate dummy netdev for secondary ports, skipping phy_connect()...\n");
		return;
	}
	priv = netdev_priv(gbe_dev->dummy_ndev);
	*priv = gbe_dev;

	if (slave->link_interface == SGMII_LINK_MAC_PHY) {
		phy_mode = PHY_INTERFACE_MODE_SGMII;
		slave->phy_port_t = PORT_MII;
	} else if (slave->link_interface == RGMII_LINK_MAC_PHY) {
		phy_mode = PHY_INTERFACE_MODE_RGMII;
		slave->phy_port_t = PORT_MII;
	} else {
		phy_mode = PHY_INTERFACE_MODE_NA;
		slave->phy_port_t = PORT_FIBRE;
	}

	for_each_sec_slave(slave, gbe_dev) {
		if ((slave->link_interface != SGMII_LINK_MAC_PHY) &&
		    (slave->link_interface != RGMII_LINK_MAC_PHY) &&
		    (slave->link_interface != XGMII_LINK_MAC_PHY))
			continue;
		slave->phy =
			of_phy_connect(gbe_dev->dummy_ndev,
				       slave->phy_node,
				       gbe_adjust_link_sec_slaves,
				       0, phy_mode);
		if (!slave->phy) {
			dev_err(dev, "phy not found for slave %d\n",
				slave->slave_num);
		} else {
			dev_dbg(dev, "phy found: id is: 0x%s\n",
				phydev_name(slave->phy));
			phy_start(slave->phy);
		}
	}
}

static void free_secondary_ports(struct gbe_priv *gbe_dev)
{
	struct gbe_slave *slave;

	while (!list_empty(&gbe_dev->secondary_slaves)) {
		slave = first_sec_slave(gbe_dev);

		if (slave->phy)
			phy_disconnect(slave->phy);
		list_del(&slave->slave_list);
	}
	if (gbe_dev->dummy_ndev)
		free_netdev(gbe_dev->dummy_ndev);
}

static int set_xgbe_ethss10_priv(struct gbe_priv *gbe_dev,
				 struct device_node *node)
{
	struct resource res;
	void __iomem *regs;
	int ret, i;

	ret = of_address_to_resource(node, XGBE_SS_REG_INDEX, &res);
	if (ret) {
		dev_err(gbe_dev->dev,
			"Can't xlate xgbe of node(%pOFn) ss address at %d\n",
			node, XGBE_SS_REG_INDEX);
		return ret;
	}

	regs = devm_ioremap_resource(gbe_dev->dev, &res);
	if (IS_ERR(regs)) {
		dev_err(gbe_dev->dev, "Failed to map xgbe ss register base\n");
		return PTR_ERR(regs);
	}
	gbe_dev->ss_regs = regs;

	ret = of_address_to_resource(node, XGBE_SM_REG_INDEX, &res);
	if (ret) {
		dev_err(gbe_dev->dev,
			"Can't xlate xgbe of node(%pOFn) sm address at %d\n",
			node, XGBE_SM_REG_INDEX);
		return ret;
	}

	regs = devm_ioremap_resource(gbe_dev->dev, &res);
	if (IS_ERR(regs)) {
		dev_err(gbe_dev->dev, "Failed to map xgbe sm register base\n");
		return PTR_ERR(regs);
	}
	gbe_dev->switch_regs = regs;

	ret = of_address_to_resource(node, XGBE_SERDES_REG_INDEX, &res);
	if (ret) {
		dev_err(gbe_dev->dev,
			"Can't xlate xgbe serdes of node(%pOFn) address at %d\n",
			node, XGBE_SERDES_REG_INDEX);
		return ret;
	}

	regs = devm_ioremap_resource(gbe_dev->dev, &res);
	if (IS_ERR(regs)) {
		dev_err(gbe_dev->dev, "Failed to map xgbe serdes register base\n");
		return PTR_ERR(regs);
	}
	gbe_dev->xgbe_serdes_regs = regs;

	gbe_dev->num_stats_mods = gbe_dev->max_num_ports;
	gbe_dev->et_stats = xgbe10_et_stats;
	gbe_dev->num_et_stats = ARRAY_SIZE(xgbe10_et_stats);

	gbe_dev->hw_stats = devm_kcalloc(gbe_dev->dev,
					 gbe_dev->num_et_stats, sizeof(u64),
					 GFP_KERNEL);
	if (!gbe_dev->hw_stats) {
		dev_err(gbe_dev->dev, "hw_stats memory allocation failed\n");
		return -ENOMEM;
	}

	gbe_dev->hw_stats_prev =
		devm_kcalloc(gbe_dev->dev,
			     gbe_dev->num_et_stats, sizeof(u32),
			     GFP_KERNEL);
	if (!gbe_dev->hw_stats_prev) {
		dev_err(gbe_dev->dev,
			"hw_stats_prev memory allocation failed\n");
		return -ENOMEM;
	}

	gbe_dev->ss_version = XGBE_SS_VERSION_10;
	gbe_dev->sgmii_port_regs = gbe_dev->ss_regs +
					XGBE10_SGMII_MODULE_OFFSET;
	gbe_dev->host_port_regs = gbe_dev->ss_regs + XGBE10_HOST_PORT_OFFSET;

	for (i = 0; i < gbe_dev->max_num_ports; i++)
		gbe_dev->hw_stats_regs[i] = gbe_dev->switch_regs +
			XGBE10_HW_STATS_OFFSET + (GBE_HW_STATS_REG_MAP_SZ * i);

	gbe_dev->ale_reg = gbe_dev->switch_regs + XGBE10_ALE_OFFSET;
	gbe_dev->cpts_reg = gbe_dev->switch_regs + XGBE10_CPTS_OFFSET;
	gbe_dev->ale_ports = gbe_dev->max_num_ports;
	gbe_dev->host_port = XGBE10_HOST_PORT_NUM;
	gbe_dev->ale_entries = XGBE10_NUM_ALE_ENTRIES;
	gbe_dev->stats_en_mask = (1 << (gbe_dev->max_num_ports)) - 1;

	/* Subsystem registers */
	XGBE_SET_REG_OFS(gbe_dev, ss_regs, id_ver);
	XGBE_SET_REG_OFS(gbe_dev, ss_regs, control);

	/* Switch module registers */
	XGBE_SET_REG_OFS(gbe_dev, switch_regs, id_ver);
	XGBE_SET_REG_OFS(gbe_dev, switch_regs, control);
	XGBE_SET_REG_OFS(gbe_dev, switch_regs, ptype);
	XGBE_SET_REG_OFS(gbe_dev, switch_regs, stat_port_en);
	XGBE_SET_REG_OFS(gbe_dev, switch_regs, flow_control);

	/* Host port registers */
	XGBE_SET_REG_OFS(gbe_dev, host_port_regs, port_vlan);
	XGBE_SET_REG_OFS(gbe_dev, host_port_regs, tx_pri_map);
	XGBE_SET_REG_OFS(gbe_dev, host_port_regs, rx_maxlen);
	return 0;
}

static int get_gbe_resource_version(struct gbe_priv *gbe_dev,
				    struct device_node *node)
{
	struct resource res;
	void __iomem *regs;
	int ret;

	ret = of_address_to_resource(node, GBE_SS_REG_INDEX, &res);
	if (ret) {
		dev_err(gbe_dev->dev,
			"Can't translate of node(%pOFn) of gbe ss address at %d\n",
			node, GBE_SS_REG_INDEX);
		return ret;
	}

	regs = devm_ioremap_resource(gbe_dev->dev, &res);
	if (IS_ERR(regs)) {
		dev_err(gbe_dev->dev, "Failed to map gbe register base\n");
		return PTR_ERR(regs);
	}
	gbe_dev->ss_regs = regs;
	gbe_dev->ss_version = readl(gbe_dev->ss_regs);
	return 0;
}

static int set_gbe_ethss14_priv(struct gbe_priv *gbe_dev,
				struct device_node *node)
{
	struct resource res;
	void __iomem *regs;
	int i, ret;

	ret = of_address_to_resource(node, GBE_SGMII34_REG_INDEX, &res);
	if (ret) {
		dev_err(gbe_dev->dev,
			"Can't translate of gbe node(%pOFn) address at index %d\n",
			node, GBE_SGMII34_REG_INDEX);
		return ret;
	}

	regs = devm_ioremap_resource(gbe_dev->dev, &res);
	if (IS_ERR(regs)) {
		dev_err(gbe_dev->dev,
			"Failed to map gbe sgmii port34 register base\n");
		return PTR_ERR(regs);
	}
	gbe_dev->sgmii_port34_regs = regs;

	ret = of_address_to_resource(node, GBE_SM_REG_INDEX, &res);
	if (ret) {
		dev_err(gbe_dev->dev,
			"Can't translate of gbe node(%pOFn) address at index %d\n",
			node, GBE_SM_REG_INDEX);
		return ret;
	}

	regs = devm_ioremap_resource(gbe_dev->dev, &res);
	if (IS_ERR(regs)) {
		dev_err(gbe_dev->dev,
			"Failed to map gbe switch module register base\n");
		return PTR_ERR(regs);
	}
	gbe_dev->switch_regs = regs;

	gbe_dev->num_stats_mods = gbe_dev->max_num_slaves;
	gbe_dev->et_stats = gbe13_et_stats;
	gbe_dev->num_et_stats = ARRAY_SIZE(gbe13_et_stats);

	gbe_dev->hw_stats = devm_kcalloc(gbe_dev->dev,
					 gbe_dev->num_et_stats, sizeof(u64),
					 GFP_KERNEL);
	if (!gbe_dev->hw_stats) {
		dev_err(gbe_dev->dev, "hw_stats memory allocation failed\n");
		return -ENOMEM;
	}

	gbe_dev->hw_stats_prev =
		devm_kcalloc(gbe_dev->dev,
			     gbe_dev->num_et_stats, sizeof(u32),
			     GFP_KERNEL);
	if (!gbe_dev->hw_stats_prev) {
		dev_err(gbe_dev->dev,
			"hw_stats_prev memory allocation failed\n");
		return -ENOMEM;
	}

	gbe_dev->sgmii_port_regs = gbe_dev->ss_regs + GBE13_SGMII_MODULE_OFFSET;
	gbe_dev->host_port_regs = gbe_dev->switch_regs + GBE13_HOST_PORT_OFFSET;

	/* K2HK has only 2 hw stats modules visible at a time, so
	 * module 0 & 2 points to one base and
	 * module 1 & 3 points to the other base
	 */
	for (i = 0; i < gbe_dev->max_num_slaves; i++) {
		gbe_dev->hw_stats_regs[i] =
			gbe_dev->switch_regs + GBE13_HW_STATS_OFFSET +
			(GBE_HW_STATS_REG_MAP_SZ * (i & 0x1));
	}

	gbe_dev->cpts_reg = gbe_dev->switch_regs + GBE13_CPTS_OFFSET;
	gbe_dev->ale_reg = gbe_dev->switch_regs + GBE13_ALE_OFFSET;
	gbe_dev->ale_ports = gbe_dev->max_num_ports;
	gbe_dev->host_port = GBE13_HOST_PORT_NUM;
	gbe_dev->ale_entries = GBE13_NUM_ALE_ENTRIES;
	gbe_dev->stats_en_mask = GBE13_REG_VAL_STAT_ENABLE_ALL;

	/* Subsystem registers */
	GBE_SET_REG_OFS(gbe_dev, ss_regs, id_ver);

	/* Switch module registers */
	GBE_SET_REG_OFS(gbe_dev, switch_regs, id_ver);
	GBE_SET_REG_OFS(gbe_dev, switch_regs, control);
	GBE_SET_REG_OFS(gbe_dev, switch_regs, soft_reset);
	GBE_SET_REG_OFS(gbe_dev, switch_regs, stat_port_en);
	GBE_SET_REG_OFS(gbe_dev, switch_regs, ptype);
	GBE_SET_REG_OFS(gbe_dev, switch_regs, flow_control);

	/* Host port registers */
	GBE_SET_REG_OFS(gbe_dev, host_port_regs, port_vlan);
	GBE_SET_REG_OFS(gbe_dev, host_port_regs, rx_maxlen);
	return 0;
}

static int set_gbenu_ethss_priv(struct gbe_priv *gbe_dev,
				struct device_node *node)
{
	struct resource res;
	void __iomem *regs;
	int i, ret;

	gbe_dev->num_stats_mods = gbe_dev->max_num_ports;
	gbe_dev->et_stats = gbenu_et_stats;

	if (IS_SS_ID_MU(gbe_dev))
		gbe_dev->num_et_stats = GBENU_ET_STATS_HOST_SIZE +
			(gbe_dev->max_num_slaves * GBENU_ET_STATS_PORT_SIZE);
	else
		gbe_dev->num_et_stats = GBENU_ET_STATS_HOST_SIZE +
					GBENU_ET_STATS_PORT_SIZE;

	gbe_dev->hw_stats = devm_kcalloc(gbe_dev->dev,
					 gbe_dev->num_et_stats, sizeof(u64),
					 GFP_KERNEL);
	if (!gbe_dev->hw_stats) {
		dev_err(gbe_dev->dev, "hw_stats memory allocation failed\n");
		return -ENOMEM;
	}

	gbe_dev->hw_stats_prev =
		devm_kcalloc(gbe_dev->dev,
			     gbe_dev->num_et_stats, sizeof(u32),
			     GFP_KERNEL);
	if (!gbe_dev->hw_stats_prev) {
		dev_err(gbe_dev->dev,
			"hw_stats_prev memory allocation failed\n");
		return -ENOMEM;
	}

	ret = of_address_to_resource(node, GBENU_SM_REG_INDEX, &res);
	if (ret) {
		dev_err(gbe_dev->dev,
			"Can't translate of gbenu node(%pOFn) addr at index %d\n",
			node, GBENU_SM_REG_INDEX);
		return ret;
	}

	regs = devm_ioremap_resource(gbe_dev->dev, &res);
	if (IS_ERR(regs)) {
		dev_err(gbe_dev->dev,
			"Failed to map gbenu switch module register base\n");
		return PTR_ERR(regs);
	}
	gbe_dev->switch_regs = regs;

	if (!IS_SS_ID_2U(gbe_dev))
		gbe_dev->sgmii_port_regs =
		       gbe_dev->ss_regs + GBENU_SGMII_MODULE_OFFSET;

	/* Although sgmii modules are mem mapped to one contiguous
	 * region on GBENU devices, setting sgmii_port34_regs allows
	 * consistent code when accessing sgmii api
	 */
	gbe_dev->sgmii_port34_regs = gbe_dev->sgmii_port_regs +
				     (2 * GBENU_SGMII_MODULE_SIZE);

	gbe_dev->host_port_regs = gbe_dev->switch_regs + GBENU_HOST_PORT_OFFSET;

	for (i = 0; i < (gbe_dev->max_num_ports); i++)
		gbe_dev->hw_stats_regs[i] = gbe_dev->switch_regs +
			GBENU_HW_STATS_OFFSET + (GBENU_HW_STATS_REG_MAP_SZ * i);

	gbe_dev->cpts_reg = gbe_dev->switch_regs + GBENU_CPTS_OFFSET;
	gbe_dev->ale_reg = gbe_dev->switch_regs + GBENU_ALE_OFFSET;
	gbe_dev->ale_ports = gbe_dev->max_num_ports;
	gbe_dev->host_port = GBENU_HOST_PORT_NUM;
	gbe_dev->stats_en_mask = (1 << (gbe_dev->max_num_ports)) - 1;

	/* Subsystem registers */
	GBENU_SET_REG_OFS(gbe_dev, ss_regs, id_ver);
	/* ok to set for MU, but used by 2U only */
	GBENU_SET_REG_OFS(gbe_dev, ss_regs, rgmii_status);

	/* Switch module registers */
	GBENU_SET_REG_OFS(gbe_dev, switch_regs, id_ver);
	GBENU_SET_REG_OFS(gbe_dev, switch_regs, control);
	GBENU_SET_REG_OFS(gbe_dev, switch_regs, stat_port_en);
	GBENU_SET_REG_OFS(gbe_dev, switch_regs, ptype);

	/* Host port registers */
	GBENU_SET_REG_OFS(gbe_dev, host_port_regs, port_vlan);
	GBENU_SET_REG_OFS(gbe_dev, host_port_regs, rx_maxlen);

	/* For NU only.  2U does not need tx_pri_map.
	 * NU cppi port 0 tx pkt streaming interface has (n-1)*8 egress threads
	 * while 2U has only 1 such thread
	 */
	GBENU_SET_REG_OFS(gbe_dev, host_port_regs, tx_pri_map);
	return 0;
}

static int gbe_probe(struct netcp_device *netcp_device, struct device *dev,
		     struct device_node *node, void **inst_priv)
{
	struct device_node *interfaces, *interface, *cpts_node;
	struct device_node *secondary_ports;
	struct cpsw_ale_params ale_params;
	struct gbe_priv *gbe_dev;
	u32 slave_num;
	int i, ret = 0;

	if (!node) {
		dev_err(dev, "device tree info unavailable\n");
		return -ENODEV;
	}

	gbe_dev = devm_kzalloc(dev, sizeof(struct gbe_priv), GFP_KERNEL);
	if (!gbe_dev)
		return -ENOMEM;

	if (of_device_is_compatible(node, "ti,netcp-gbe-5") ||
	    of_device_is_compatible(node, "ti,netcp-gbe")) {
		gbe_dev->max_num_slaves = 4;
	} else if (of_device_is_compatible(node, "ti,netcp-gbe-9")) {
		gbe_dev->max_num_slaves = 8;
	} else if (of_device_is_compatible(node, "ti,netcp-gbe-2")) {
		gbe_dev->max_num_slaves = 1;
		gbe_module.set_rx_mode = gbe_set_rx_mode;
	} else if (of_device_is_compatible(node, "ti,netcp-xgbe")) {
		gbe_dev->max_num_slaves = 2;
	} else {
		dev_err(dev, "device tree node for unknown device\n");
		return -EINVAL;
	}
	gbe_dev->max_num_ports = gbe_dev->max_num_slaves + 1;

	gbe_dev->dev = dev;
	gbe_dev->netcp_device = netcp_device;
	gbe_dev->rx_packet_max = NETCP_MAX_FRAME_SIZE;

	/* init the hw stats lock */
	spin_lock_init(&gbe_dev->hw_stats_lock);

	if (of_find_property(node, "enable-ale", NULL)) {
		gbe_dev->enable_ale = true;
		dev_info(dev, "ALE enabled\n");
	} else {
		gbe_dev->enable_ale = false;
		dev_dbg(dev, "ALE bypass enabled*\n");
	}

	ret = of_property_read_u32(node, "tx-queue",
				   &gbe_dev->tx_queue_id);
	if (ret < 0) {
		dev_err(dev, "missing tx_queue parameter\n");
		gbe_dev->tx_queue_id = GBE_TX_QUEUE;
	}

	ret = of_property_read_string(node, "tx-channel",
				      &gbe_dev->dma_chan_name);
	if (ret < 0) {
		dev_err(dev, "missing \"tx-channel\" parameter\n");
		return -EINVAL;
	}

	if (of_node_name_eq(node, "gbe")) {
		ret = get_gbe_resource_version(gbe_dev, node);
		if (ret)
			return ret;

		dev_dbg(dev, "ss_version: 0x%08x\n", gbe_dev->ss_version);

		if (IS_SS_ID_VER_14(gbe_dev))
			ret = set_gbe_ethss14_priv(gbe_dev, node);
		else if (IS_SS_ID_MU(gbe_dev))
			ret = set_gbenu_ethss_priv(gbe_dev, node);
		else
			ret = -ENODEV;

	} else if (of_node_name_eq(node, "xgbe")) {
		ret = set_xgbe_ethss10_priv(gbe_dev, node);
		if (ret)
			return ret;
		ret = netcp_xgbe_serdes_init(gbe_dev->xgbe_serdes_regs,
					     gbe_dev->ss_regs);
	} else {
		dev_err(dev, "unknown GBE node(%pOFn)\n", node);
		ret = -ENODEV;
	}

	if (ret)
		return ret;

	interfaces = of_get_child_by_name(node, "interfaces");
	if (!interfaces)
		dev_err(dev, "could not find interfaces\n");

	ret = netcp_txpipe_init(&gbe_dev->tx_pipe, netcp_device,
				gbe_dev->dma_chan_name, gbe_dev->tx_queue_id);
	if (ret) {
		of_node_put(interfaces);
		return ret;
	}

	ret = netcp_txpipe_open(&gbe_dev->tx_pipe);
	if (ret) {
		of_node_put(interfaces);
		return ret;
	}

	/* Create network interfaces */
	INIT_LIST_HEAD(&gbe_dev->gbe_intf_head);
	for_each_child_of_node(interfaces, interface) {
		ret = of_property_read_u32(interface, "slave-port", &slave_num);
		if (ret) {
			dev_err(dev, "missing slave-port parameter, skipping interface configuration for %pOFn\n",
				interface);
			continue;
		}
		gbe_dev->num_slaves++;
		if (gbe_dev->num_slaves >= gbe_dev->max_num_slaves) {
			of_node_put(interface);
			break;
		}
	}
	of_node_put(interfaces);

	if (!gbe_dev->num_slaves)
		dev_warn(dev, "No network interface configured\n");

	/* Initialize Secondary slave ports */
	secondary_ports = of_get_child_by_name(node, "secondary-slave-ports");
	INIT_LIST_HEAD(&gbe_dev->secondary_slaves);
	if (secondary_ports && (gbe_dev->num_slaves <  gbe_dev->max_num_slaves))
		init_secondary_ports(gbe_dev, secondary_ports);
	of_node_put(secondary_ports);

	if (!gbe_dev->num_slaves) {
		dev_err(dev,
			"No network interface or secondary ports configured\n");
		ret = -ENODEV;
		goto free_sec_ports;
	}

	memset(&ale_params, 0, sizeof(ale_params));
	ale_params.dev		= gbe_dev->dev;
	ale_params.ale_regs	= gbe_dev->ale_reg;
	ale_params.ale_ageout	= GBE_DEFAULT_ALE_AGEOUT;
	ale_params.ale_entries	= gbe_dev->ale_entries;
	ale_params.ale_ports	= gbe_dev->ale_ports;
	if (IS_SS_ID_MU(gbe_dev)) {
		ale_params.major_ver_mask = 0x7;
		ale_params.nu_switch_ale = true;
	}
	gbe_dev->ale = cpsw_ale_create(&ale_params);
	if (!gbe_dev->ale) {
		dev_err(gbe_dev->dev, "error initializing ale engine\n");
		ret = -ENODEV;
		goto free_sec_ports;
	} else {
		dev_dbg(gbe_dev->dev, "Created a gbe ale engine\n");
	}

	cpts_node = of_get_child_by_name(node, "cpts");
	if (!cpts_node)
		cpts_node = of_node_get(node);

	gbe_dev->cpts = cpts_create(gbe_dev->dev, gbe_dev->cpts_reg, cpts_node);
	of_node_put(cpts_node);
	if (IS_ENABLED(CONFIG_TI_CPTS) && IS_ERR(gbe_dev->cpts)) {
		ret = PTR_ERR(gbe_dev->cpts);
		goto free_sec_ports;
	}

	/* initialize host port */
	gbe_init_host_port(gbe_dev);

	spin_lock_bh(&gbe_dev->hw_stats_lock);
	for (i = 0; i < gbe_dev->num_stats_mods; i++) {
		if (IS_SS_ID_VER_14(gbe_dev))
			gbe_reset_mod_stats_ver14(gbe_dev, i);
		else
			gbe_reset_mod_stats(gbe_dev, i);
	}
	spin_unlock_bh(&gbe_dev->hw_stats_lock);

	timer_setup(&gbe_dev->timer, netcp_ethss_timer, 0);
	gbe_dev->timer.expires	 = jiffies + GBE_TIMER_INTERVAL;
	add_timer(&gbe_dev->timer);
	*inst_priv = gbe_dev;
	return 0;

free_sec_ports:
	free_secondary_ports(gbe_dev);
	return ret;
}

static int gbe_attach(void *inst_priv, struct net_device *ndev,
		      struct device_node *node, void **intf_priv)
{
	struct gbe_priv *gbe_dev = inst_priv;
	struct gbe_intf *gbe_intf;
	int ret;

	if (!node) {
		dev_err(gbe_dev->dev, "interface node not available\n");
		return -ENODEV;
	}

	gbe_intf = devm_kzalloc(gbe_dev->dev, sizeof(*gbe_intf), GFP_KERNEL);
	if (!gbe_intf)
		return -ENOMEM;

	gbe_intf->ndev = ndev;
	gbe_intf->dev = gbe_dev->dev;
	gbe_intf->gbe_dev = gbe_dev;

	gbe_intf->slave = devm_kzalloc(gbe_dev->dev,
					sizeof(*gbe_intf->slave),
					GFP_KERNEL);
	if (!gbe_intf->slave) {
		ret = -ENOMEM;
		goto fail;
	}

	if (init_slave(gbe_dev, gbe_intf->slave, node)) {
		ret = -ENODEV;
		goto fail;
	}

	gbe_intf->tx_pipe = gbe_dev->tx_pipe;
	ndev->ethtool_ops = &keystone_ethtool_ops;
	list_add_tail(&gbe_intf->gbe_intf_list, &gbe_dev->gbe_intf_head);
	*intf_priv = gbe_intf;
	return 0;

fail:
	if (gbe_intf->slave)
		devm_kfree(gbe_dev->dev, gbe_intf->slave);
	if (gbe_intf)
		devm_kfree(gbe_dev->dev, gbe_intf);
	return ret;
}

static int gbe_release(void *intf_priv)
{
	struct gbe_intf *gbe_intf = intf_priv;

	gbe_intf->ndev->ethtool_ops = NULL;
	list_del(&gbe_intf->gbe_intf_list);
	devm_kfree(gbe_intf->dev, gbe_intf->slave);
	devm_kfree(gbe_intf->dev, gbe_intf);
	return 0;
}

static int gbe_remove(struct netcp_device *netcp_device, void *inst_priv)
{
	struct gbe_priv *gbe_dev = inst_priv;

	del_timer_sync(&gbe_dev->timer);
	cpts_release(gbe_dev->cpts);
	cpsw_ale_stop(gbe_dev->ale);
	netcp_txpipe_close(&gbe_dev->tx_pipe);
	free_secondary_ports(gbe_dev);

	if (!list_empty(&gbe_dev->gbe_intf_head))
		dev_alert(gbe_dev->dev,
			  "unreleased ethss interfaces present\n");

	return 0;
}

static struct netcp_module gbe_module = {
	.name		= GBE_MODULE_NAME,
	.owner		= THIS_MODULE,
	.primary	= true,
	.probe		= gbe_probe,
	.open		= gbe_open,
	.close		= gbe_close,
	.remove		= gbe_remove,
	.attach		= gbe_attach,
	.release	= gbe_release,
	.add_addr	= gbe_add_addr,
	.del_addr	= gbe_del_addr,
	.add_vid	= gbe_add_vid,
	.del_vid	= gbe_del_vid,
	.ioctl		= gbe_ioctl,
};

static struct netcp_module xgbe_module = {
	.name		= XGBE_MODULE_NAME,
	.owner		= THIS_MODULE,
	.primary	= true,
	.probe		= gbe_probe,
	.open		= gbe_open,
	.close		= gbe_close,
	.remove		= gbe_remove,
	.attach		= gbe_attach,
	.release	= gbe_release,
	.add_addr	= gbe_add_addr,
	.del_addr	= gbe_del_addr,
	.add_vid	= gbe_add_vid,
	.del_vid	= gbe_del_vid,
	.ioctl		= gbe_ioctl,
};

static int __init keystone_gbe_init(void)
{
	int ret;

	ret = netcp_register_module(&gbe_module);
	if (ret)
		return ret;

	ret = netcp_register_module(&xgbe_module);
	if (ret)
		return ret;

	return 0;
}
module_init(keystone_gbe_init);

static void __exit keystone_gbe_exit(void)
{
	netcp_unregister_module(&gbe_module);
	netcp_unregister_module(&xgbe_module);
}
module_exit(keystone_gbe_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TI NETCP ETHSS driver for Keystone SOCs");
MODULE_AUTHOR("Sandeep Nair <sandeep_n@ti.com");
