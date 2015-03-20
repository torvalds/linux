/*
 * Keystone GBE and XGBE subsystem code
 *
 * Copyright (C) 2014 Texas Instruments Incorporated
 * Authors:	Sandeep Nair <sandeep_n@ti.com>
 *		Sandeep Paulraj <s-paulraj@ti.com>
 *		Cyril Chemparathy <cyril@ti.com>
 *		Santosh Shilimkar <santosh.shilimkar@ti.com>
 *		Wingman Kwok <w-kwok2@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_mdio.h>
#include <linux/of_address.h>
#include <linux/if_vlan.h>
#include <linux/ethtool.h>

#include "cpsw_ale.h"
#include "netcp.h"

#define NETCP_DRIVER_NAME		"TI KeyStone Ethernet Driver"
#define NETCP_DRIVER_VERSION		"v1.0"

#define GBE_IDENT(reg)			((reg >> 16) & 0xffff)
#define GBE_MAJOR_VERSION(reg)		(reg >> 8 & 0x7)
#define GBE_MINOR_VERSION(reg)		(reg & 0xff)
#define GBE_RTL_VERSION(reg)		((reg >> 11) & 0x1f)

/* 1G Ethernet SS defines */
#define GBE_MODULE_NAME			"netcp-gbe"
#define GBE_SS_VERSION_14		0x4ed21104

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
#define GBE13_ALE_OFFSET		0x600
#define GBE13_HOST_PORT_NUM		0
#define GBE13_NUM_SLAVES		4
#define GBE13_NUM_ALE_PORTS		(GBE13_NUM_SLAVES + 1)
#define GBE13_NUM_ALE_ENTRIES		1024

/* 10G Ethernet SS defines */
#define XGBE_MODULE_NAME		"netcp-xgbe"
#define XGBE_SS_VERSION_10		0x4ee42100

#define XGBE_SS_REG_INDEX		0
#define XGBE_SM_REG_INDEX		1
#define XGBE_SERDES_REG_INDEX		2

/* offset relative to base of XGBE_SS_REG_INDEX */
#define XGBE10_SGMII_MODULE_OFFSET	0x100
/* offset relative to base of XGBE_SM_REG_INDEX */
#define XGBE10_HOST_PORT_OFFSET		0x34
#define XGBE10_SLAVE_PORT_OFFSET	0x64
#define XGBE10_EMAC_OFFSET		0x400
#define XGBE10_ALE_OFFSET		0x700
#define XGBE10_HW_STATS_OFFSET		0x800
#define XGBE10_HOST_PORT_NUM		0
#define XGBE10_NUM_SLAVES		2
#define XGBE10_NUM_ALE_PORTS		(XGBE10_NUM_SLAVES + 1)
#define XGBE10_NUM_ALE_ENTRIES		1024

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
#define GBE_REG_VAL_STAT_ENABLE_ALL		0xff
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

#define XGBE_STATS0_MODULE			0
#define XGBE_STATS1_MODULE			1
#define XGBE_STATS2_MODULE			2

#define MAX_SLAVES				GBE13_NUM_SLAVES
/* s: 0-based slave_port */
#define SGMII_BASE(s) \
	(((s) < 2) ? gbe_dev->sgmii_port_regs : gbe_dev->sgmii_port34_regs)

#define GBE_TX_QUEUE				648
#define	GBE_TXHOOK_ORDER			0
#define GBE_DEFAULT_ALE_AGEOUT			30
#define SLAVE_LINK_IS_XGMII(s) ((s)->link_interface >= XGMII_LINK_MAC_PHY)
#define NETCP_LINK_STATE_INVALID		-1

#define GBE_SET_REG_OFS(p, rb, rn) p->rb##_ofs.rn = \
		offsetof(struct gbe##_##rb, rn)
#define XGBE_SET_REG_OFS(p, rb, rn) p->rb##_ofs.rn = \
		offsetof(struct xgbe##_##rb, rn)
#define GBE_REG_ADDR(p, rb, rn) (p->rb + p->rb##_ofs.rn)

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

#define XGBE10_NUM_STAT_ENTRIES (sizeof(struct xgbe_hw_stats)/sizeof(u32))

struct gbe_ss_regs {
	u32	id_ver;
	u32	synce_count;
	u32	synce_mux;
};

struct gbe_ss_regs_ofs {
	u16	id_ver;
	u16	control;
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
	u16	sa_lo;
	u16	sa_hi;
	u16	ts_ctl;
	u16	ts_seq_ltype;
	u16	ts_vlan;
	u16	ts_ctl_ltype2;
	u16	ts_ctl2;
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

#define GBE13_NUM_HW_STAT_ENTRIES (sizeof(struct gbe_hw_stats)/sizeof(u32))
#define GBE13_NUM_HW_STATS_MOD			2
#define XGBE10_NUM_HW_STATS_MOD			3
#define GBE_MAX_HW_STAT_MODS			3
#define GBE_HW_STATS_REG_MAP_SZ			0x100

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
	struct device_node		*phy_node;
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
	struct netcp_tx_pipe		tx_pipe;

	int				host_port;
	u32				rx_packet_max;
	u32				ss_version;

	void __iomem			*ss_regs;
	void __iomem			*switch_regs;
	void __iomem			*host_port_regs;
	void __iomem			*ale_reg;
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
	const struct netcp_ethtool_stat *et_stats;
	int				num_et_stats;
	/*  Lock for updating the hwstats */
	spinlock_t			hw_stats_lock;
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

static void keystone_get_stat_strings(struct net_device *ndev,
				      uint32_t stringset, uint8_t *data)
{
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct gbe_intf *gbe_intf;
	struct gbe_priv *gbe_dev;
	int i;

	gbe_intf = netcp_module_get_intf_data(&gbe_module, netcp);
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

	gbe_intf = netcp_module_get_intf_data(&gbe_module, netcp);
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

static void gbe_update_stats(struct gbe_priv *gbe_dev, uint64_t *data)
{
	void __iomem *base = NULL;
	u32  __iomem *p;
	u32 tmp = 0;
	int i;

	for (i = 0; i < gbe_dev->num_et_stats; i++) {
		base = gbe_dev->hw_stats_regs[gbe_dev->et_stats[i].type];
		p = base + gbe_dev->et_stats[i].offset;
		tmp = readl(p);
		gbe_dev->hw_stats[i] = gbe_dev->hw_stats[i] + tmp;
		if (data)
			data[i] = gbe_dev->hw_stats[i];
		/* write-to-decrement:
		 * new register value = old register value - write value
		 */
		writel(tmp, p);
	}
}

static void gbe_update_stats_ver14(struct gbe_priv *gbe_dev, uint64_t *data)
{
	void __iomem *gbe_statsa = gbe_dev->hw_stats_regs[0];
	void __iomem *gbe_statsb = gbe_dev->hw_stats_regs[1];
	u64 *hw_stats = &gbe_dev->hw_stats[0];
	void __iomem *base = NULL;
	u32  __iomem *p;
	u32 tmp = 0, val, pair_size = (gbe_dev->num_et_stats / 2);
	int i, j, pair;

	for (pair = 0; pair < 2; pair++) {
		val = readl(GBE_REG_ADDR(gbe_dev, switch_regs, stat_port_en));

		if (pair == 0)
			val &= ~GBE_STATS_CD_SEL;
		else
			val |= GBE_STATS_CD_SEL;

		/* make the stat modules visible */
		writel(val, GBE_REG_ADDR(gbe_dev, switch_regs, stat_port_en));

		for (i = 0; i < pair_size; i++) {
			j = pair * pair_size + i;
			switch (gbe_dev->et_stats[j].type) {
			case GBE_STATSA_MODULE:
			case GBE_STATSC_MODULE:
				base = gbe_statsa;
			break;
			case GBE_STATSB_MODULE:
			case GBE_STATSD_MODULE:
				base  = gbe_statsb;
			break;
			}

			p = base + gbe_dev->et_stats[j].offset;
			tmp = readl(p);
			hw_stats[j] += tmp;
			if (data)
				data[j] = hw_stats[j];
			/* write-to-decrement:
			 * new register value = old register value - write value
			 */
			writel(tmp, p);
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

	gbe_intf = netcp_module_get_intf_data(&gbe_module, netcp);
	if (!gbe_intf)
		return;

	gbe_dev = gbe_intf->gbe_dev;
	spin_lock_bh(&gbe_dev->hw_stats_lock);
	if (gbe_dev->ss_version == GBE_SS_VERSION_14)
		gbe_update_stats_ver14(gbe_dev, data);
	else
		gbe_update_stats(gbe_dev, data);
	spin_unlock_bh(&gbe_dev->hw_stats_lock);
}

static int keystone_get_settings(struct net_device *ndev,
				 struct ethtool_cmd *cmd)
{
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct phy_device *phy = ndev->phydev;
	struct gbe_intf *gbe_intf;
	int ret;

	if (!phy)
		return -EINVAL;

	gbe_intf = netcp_module_get_intf_data(&gbe_module, netcp);
	if (!gbe_intf)
		return -EINVAL;

	if (!gbe_intf->slave)
		return -EINVAL;

	ret = phy_ethtool_gset(phy, cmd);
	if (!ret)
		cmd->port = gbe_intf->slave->phy_port_t;

	return ret;
}

static int keystone_set_settings(struct net_device *ndev,
				 struct ethtool_cmd *cmd)
{
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct phy_device *phy = ndev->phydev;
	struct gbe_intf *gbe_intf;
	u32 features = cmd->advertising & cmd->supported;

	if (!phy)
		return -EINVAL;

	gbe_intf = netcp_module_get_intf_data(&gbe_module, netcp);
	if (!gbe_intf)
		return -EINVAL;

	if (!gbe_intf->slave)
		return -EINVAL;

	if (cmd->port != gbe_intf->slave->phy_port_t) {
		if ((cmd->port == PORT_TP) && !(features & ADVERTISED_TP))
			return -EINVAL;

		if ((cmd->port == PORT_AUI) && !(features & ADVERTISED_AUI))
			return -EINVAL;

		if ((cmd->port == PORT_BNC) && !(features & ADVERTISED_BNC))
			return -EINVAL;

		if ((cmd->port == PORT_MII) && !(features & ADVERTISED_MII))
			return -EINVAL;

		if ((cmd->port == PORT_FIBRE) && !(features & ADVERTISED_FIBRE))
			return -EINVAL;
	}

	gbe_intf->slave->phy_port_t = cmd->port;
	return phy_ethtool_sset(phy, cmd);
}

static const struct ethtool_ops keystone_ethtool_ops = {
	.get_drvinfo		= keystone_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_msglevel		= keystone_get_msglevel,
	.set_msglevel		= keystone_set_msglevel,
	.get_strings		= keystone_get_stat_strings,
	.get_sset_count		= keystone_get_sset_count,
	.get_ethtool_stats	= keystone_get_ethtool_stats,
	.get_settings		= keystone_get_settings,
	.set_settings		= keystone_set_settings,
};

#define mac_hi(mac)	(((mac)[0] << 0) | ((mac)[1] << 8) |	\
			 ((mac)[2] << 16) | ((mac)[3] << 24))
#define mac_lo(mac)	(((mac)[4] << 0) | ((mac)[5] << 8))

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

		if (ndev && slave->open)
			netif_carrier_on(ndev);
	} else {
		writel(mac_control, GBE_REG_ADDR(slave, emac_regs,
						 mac_control));
		cpsw_ale_control_set(gbe_dev->ale, slave->port_num,
				     ALE_PORT_STATE,
				     ALE_PORT_STATE_DISABLE);
		if (ndev)
			netif_carrier_off(ndev);
	}

	if (phy)
		phy_print_status(phy);
}

static bool gbe_phy_link_status(struct gbe_slave *slave)
{
	 return !slave->phy || slave->phy->link;
}

static void netcp_ethss_update_link_state(struct gbe_priv *gbe_dev,
					  struct gbe_slave *slave,
					  struct net_device *ndev)
{
	int sp = slave->slave_num;
	int phy_link_state, sgmii_link_state = 1, link_state;

	if (!slave->open)
		return;

	if (!SLAVE_LINK_IS_XGMII(slave))
		sgmii_link_state = netcp_sgmii_get_port_link(SGMII_BASE(sp),
							     sp);
	phy_link_state = gbe_phy_link_status(slave);
	link_state = phy_link_state & sgmii_link_state;

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
	u32 xgmii_mode;

	if (max_rx_len > NETCP_MAX_FRAME_SIZE)
		max_rx_len = NETCP_MAX_FRAME_SIZE;

	/* Enable correct MII mode at SS level */
	if ((gbe_dev->ss_version == XGBE_SS_VERSION_10) &&
	    (slave->link_interface >= XGMII_LINK_MAC_PHY)) {
		xgmii_mode = readl(GBE_REG_ADDR(gbe_dev, ss_regs, control));
		xgmii_mode |= (1 << slave->slave_num);
		writel(xgmii_mode, GBE_REG_ADDR(gbe_dev, ss_regs, control));
	}

	writel(max_rx_len, GBE_REG_ADDR(slave, emac_regs, rx_maxlen));
	writel(slave->mac_control, GBE_REG_ADDR(slave, emac_regs, mac_control));
}

static void gbe_slave_stop(struct gbe_intf *intf)
{
	struct gbe_priv *gbe_dev = intf->gbe_dev;
	struct gbe_slave *slave = intf->slave;

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
	void __iomem *sgmii_port_regs;

	sgmii_port_regs = priv->sgmii_port_regs;
	if ((priv->ss_version == GBE_SS_VERSION_14) && (slave->slave_num >= 2))
		sgmii_port_regs = priv->sgmii_port34_regs;

	if (!SLAVE_LINK_IS_XGMII(slave)) {
		netcp_sgmii_reset(sgmii_port_regs, slave->slave_num);
		netcp_sgmii_config(sgmii_port_regs, slave->slave_num,
				   slave->link_interface);
	}
}

static int gbe_slave_open(struct gbe_intf *gbe_intf)
{
	struct gbe_priv *priv = gbe_intf->gbe_dev;
	struct gbe_slave *slave = gbe_intf->slave;
	phy_interface_t phy_mode;
	bool has_phy = false;

	void (*hndlr)(struct net_device *) = gbe_adjust_link;

	gbe_sgmii_config(priv, slave);
	gbe_port_reset(slave);
	gbe_port_config(priv, slave, priv->rx_packet_max);
	gbe_set_slave_mac(slave, gbe_intf);
	/* enable forwarding */
	cpsw_ale_control_set(priv->ale, slave->port_num,
			     ALE_PORT_STATE, ALE_PORT_STATE_FORWARD);
	cpsw_ale_add_mcast(priv->ale, gbe_intf->ndev->broadcast,
			   1 << slave->port_num, 0, 0, ALE_MCAST_FWD_2);

	if (slave->link_interface == SGMII_LINK_MAC_PHY) {
		has_phy = true;
		phy_mode = PHY_INTERFACE_MODE_SGMII;
		slave->phy_port_t = PORT_MII;
	} else if (slave->link_interface == XGMII_LINK_MAC_PHY) {
		has_phy = true;
		phy_mode = PHY_INTERFACE_MODE_NA;
		slave->phy_port_t = PORT_FIBRE;
	}

	if (has_phy) {
		if (priv->ss_version == XGBE_SS_VERSION_10)
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
			dev_name(&slave->phy->dev));
		phy_start(slave->phy);
		phy_read_status(slave->phy);
	}
	return 0;
}

static void gbe_init_host_port(struct gbe_priv *priv)
{
	int bypass_en = 1;
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

static int gbe_ioctl(void *intf_priv, struct ifreq *req, int cmd)
{
	struct gbe_intf *gbe_intf = intf_priv;
	struct phy_device *phy = gbe_intf->slave->phy;
	int ret = -EOPNOTSUPP;

	if (phy)
		ret = phy_mii_ioctl(phy, req, cmd);

	return ret;
}

static void netcp_ethss_timer(unsigned long arg)
{
	struct gbe_priv *gbe_dev = (struct gbe_priv *)arg;
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

	spin_lock_bh(&gbe_dev->hw_stats_lock);

	if (gbe_dev->ss_version == GBE_SS_VERSION_14)
		gbe_update_stats_ver14(gbe_dev, NULL);
	else
		gbe_update_stats(gbe_dev, NULL);

	spin_unlock_bh(&gbe_dev->hw_stats_lock);

	gbe_dev->timer.expires	= jiffies + GBE_TIMER_INTERVAL;
	add_timer(&gbe_dev->timer);
}

static int gbe_tx_hook(int order, void *data, struct netcp_packet *p_info)
{
	struct gbe_intf *gbe_intf = data;

	p_info->tx_pipe = &gbe_intf->tx_pipe;
	return 0;
}

static int gbe_open(void *intf_priv, struct net_device *ndev)
{
	struct gbe_intf *gbe_intf = intf_priv;
	struct gbe_priv *gbe_dev = gbe_intf->gbe_dev;
	struct netcp_intf *netcp = netdev_priv(ndev);
	struct gbe_slave *slave = gbe_intf->slave;
	int port_num = slave->port_num;
	u32 reg;
	int ret;

	reg = readl(GBE_REG_ADDR(gbe_dev, switch_regs, id_ver));
	dev_dbg(gbe_dev->dev, "initializing gbe version %d.%d (%d) GBE identification value 0x%x\n",
		GBE_MAJOR_VERSION(reg), GBE_MINOR_VERSION(reg),
		GBE_RTL_VERSION(reg), GBE_IDENT(reg));

	/* For 10G use directed to port */
	if (gbe_dev->ss_version == XGBE_SS_VERSION_10)
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
	writel(GBE_CTL_P0_ENABLE, GBE_REG_ADDR(gbe_dev, switch_regs, control));

	/* All statistics enabled and STAT AB visible by default */
	writel(GBE_REG_VAL_STAT_ENABLE_ALL, GBE_REG_ADDR(gbe_dev, switch_regs,
							 stat_port_en));

	ret = gbe_slave_open(gbe_intf);
	if (ret)
		goto fail;

	netcp_register_txhook(netcp, GBE_TXHOOK_ORDER, gbe_tx_hook,
			      gbe_intf);

	slave->open = true;
	netcp_ethss_update_link_state(gbe_dev, slave, ndev);
	return 0;

fail:
	gbe_slave_stop(gbe_intf);
	return ret;
}

static int gbe_close(void *intf_priv, struct net_device *ndev)
{
	struct gbe_intf *gbe_intf = intf_priv;
	struct netcp_intf *netcp = netdev_priv(ndev);

	gbe_slave_stop(gbe_intf);
	netcp_unregister_txhook(netcp, GBE_TXHOOK_ORDER, gbe_tx_hook,
				gbe_intf);

	gbe_intf->slave->open = false;
	atomic_set(&gbe_intf->slave->link_state, NETCP_LINK_STATE_INVALID);
	return 0;
}

static int init_slave(struct gbe_priv *gbe_dev, struct gbe_slave *slave,
		      struct device_node *node)
{
	int port_reg_num;
	u32 port_reg_ofs, emac_reg_ofs;

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

	slave->open = false;
	slave->phy_node = of_parse_phandle(node, "phy-handle", 0);
	slave->port_num = gbe_get_slave_port(gbe_dev, slave->slave_num);

	if (slave->link_interface >= XGMII_LINK_MAC_PHY)
		slave->mac_control = GBE_DEF_10G_MAC_CONTROL;
	else
		slave->mac_control = GBE_DEF_1G_MAC_CONTROL;

	/* Emac regs memmap are contiguous but port regs are not */
	port_reg_num = slave->slave_num;
	if (gbe_dev->ss_version == GBE_SS_VERSION_14) {
		if (slave->slave_num > 1) {
			port_reg_ofs = GBE13_SLAVE_PORT2_OFFSET;
			port_reg_num -= 2;
		} else {
			port_reg_ofs = GBE13_SLAVE_PORT_OFFSET;
		}
	} else if (gbe_dev->ss_version == XGBE_SS_VERSION_10) {
		port_reg_ofs = XGBE10_SLAVE_PORT_OFFSET;
	} else {
		dev_err(gbe_dev->dev, "unknown ethss(0x%x)\n",
			gbe_dev->ss_version);
		return -EINVAL;
	}

	if (gbe_dev->ss_version == GBE_SS_VERSION_14)
		emac_reg_ofs = GBE13_EMAC_OFFSET;
	else if (gbe_dev->ss_version == XGBE_SS_VERSION_10)
		emac_reg_ofs = XGBE10_EMAC_OFFSET;

	slave->port_regs = gbe_dev->switch_regs + port_reg_ofs +
				(0x30 * port_reg_num);
	slave->emac_regs = gbe_dev->switch_regs + emac_reg_ofs +
				(0x40 * slave->slave_num);

	if (gbe_dev->ss_version == GBE_SS_VERSION_14) {
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

	} else if (gbe_dev->ss_version == XGBE_SS_VERSION_10) {
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
			dev_err(dev,
				"memomry alloc failed for secondary port(%s), skipping...\n",
				port->name);
			continue;
		}

		if (init_slave(gbe_dev, slave, port)) {
			dev_err(dev,
				"Failed to initialize secondary port(%s), skipping...\n",
				port->name);
			devm_kfree(dev, slave);
			continue;
		}

		gbe_sgmii_config(gbe_dev, slave);
		gbe_port_reset(slave);
		gbe_port_config(gbe_dev, slave, gbe_dev->rx_packet_max);
		list_add_tail(&slave->slave_list, &gbe_dev->secondary_slaves);
		gbe_dev->num_slaves++;
		if ((slave->link_interface == SGMII_LINK_MAC_PHY) ||
		    (slave->link_interface == XGMII_LINK_MAC_PHY))
			mac_phy_link = true;

		slave->open = true;
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
	} else {
		phy_mode = PHY_INTERFACE_MODE_NA;
		slave->phy_port_t = PORT_FIBRE;
	}

	for_each_sec_slave(slave, gbe_dev) {
		if ((slave->link_interface != SGMII_LINK_MAC_PHY) &&
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
			slave->phy = NULL;
		} else {
			dev_dbg(dev, "phy found: id is: 0x%s\n",
				dev_name(&slave->phy->dev));
			phy_start(slave->phy);
			phy_read_status(slave->phy);
		}
	}
}

static void free_secondary_ports(struct gbe_priv *gbe_dev)
{
	struct gbe_slave *slave;

	for (;;) {
		slave = first_sec_slave(gbe_dev);
		if (!slave)
			break;
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

	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		dev_err(gbe_dev->dev,
			"Can't xlate xgbe of node(%s) ss address at %d\n",
			node->name, XGBE_SS_REG_INDEX);
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
			"Can't xlate xgbe of node(%s) sm address at %d\n",
			node->name, XGBE_SM_REG_INDEX);
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
			"Can't xlate xgbe serdes of node(%s) address at %d\n",
			node->name, XGBE_SERDES_REG_INDEX);
		return ret;
	}

	regs = devm_ioremap_resource(gbe_dev->dev, &res);
	if (IS_ERR(regs)) {
		dev_err(gbe_dev->dev, "Failed to map xgbe serdes register base\n");
		return PTR_ERR(regs);
	}
	gbe_dev->xgbe_serdes_regs = regs;

	gbe_dev->hw_stats = devm_kzalloc(gbe_dev->dev,
					  XGBE10_NUM_STAT_ENTRIES *
					  (XGBE10_NUM_SLAVES + 1) * sizeof(u64),
					  GFP_KERNEL);
	if (!gbe_dev->hw_stats) {
		dev_err(gbe_dev->dev, "hw_stats memory allocation failed\n");
		return -ENOMEM;
	}

	gbe_dev->ss_version = XGBE_SS_VERSION_10;
	gbe_dev->sgmii_port_regs = gbe_dev->ss_regs +
					XGBE10_SGMII_MODULE_OFFSET;
	gbe_dev->host_port_regs = gbe_dev->ss_regs + XGBE10_HOST_PORT_OFFSET;

	for (i = 0; i < XGBE10_NUM_HW_STATS_MOD; i++)
		gbe_dev->hw_stats_regs[i] = gbe_dev->switch_regs +
			XGBE10_HW_STATS_OFFSET + (GBE_HW_STATS_REG_MAP_SZ * i);

	gbe_dev->ale_reg = gbe_dev->ss_regs + XGBE10_ALE_OFFSET;
	gbe_dev->ale_ports = XGBE10_NUM_ALE_PORTS;
	gbe_dev->host_port = XGBE10_HOST_PORT_NUM;
	gbe_dev->ale_entries = XGBE10_NUM_ALE_ENTRIES;
	gbe_dev->et_stats = xgbe10_et_stats;
	gbe_dev->num_et_stats = ARRAY_SIZE(xgbe10_et_stats);

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
			"Can't translate of node(%s) of gbe ss address at %d\n",
			node->name, GBE_SS_REG_INDEX);
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
			"Can't translate of gbe node(%s) address at index %d\n",
			node->name, GBE_SGMII34_REG_INDEX);
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
			"Can't translate of gbe node(%s) address at index %d\n",
			node->name, GBE_SM_REG_INDEX);
		return ret;
	}

	regs = devm_ioremap_resource(gbe_dev->dev, &res);
	if (IS_ERR(regs)) {
		dev_err(gbe_dev->dev,
			"Failed to map gbe switch module register base\n");
		return PTR_ERR(regs);
	}
	gbe_dev->switch_regs = regs;

	gbe_dev->hw_stats = devm_kzalloc(gbe_dev->dev,
					  GBE13_NUM_HW_STAT_ENTRIES *
					  GBE13_NUM_SLAVES * sizeof(u64),
					  GFP_KERNEL);
	if (!gbe_dev->hw_stats) {
		dev_err(gbe_dev->dev, "hw_stats memory allocation failed\n");
		return -ENOMEM;
	}

	gbe_dev->sgmii_port_regs = gbe_dev->ss_regs + GBE13_SGMII_MODULE_OFFSET;
	gbe_dev->host_port_regs = gbe_dev->switch_regs + GBE13_HOST_PORT_OFFSET;

	for (i = 0; i < GBE13_NUM_HW_STATS_MOD; i++) {
		gbe_dev->hw_stats_regs[i] =
			gbe_dev->switch_regs + GBE13_HW_STATS_OFFSET +
			(GBE_HW_STATS_REG_MAP_SZ * i);
	}

	gbe_dev->ale_reg = gbe_dev->switch_regs + GBE13_ALE_OFFSET;
	gbe_dev->ale_ports = GBE13_NUM_ALE_PORTS;
	gbe_dev->host_port = GBE13_HOST_PORT_NUM;
	gbe_dev->ale_entries = GBE13_NUM_ALE_ENTRIES;
	gbe_dev->et_stats = gbe13_et_stats;
	gbe_dev->num_et_stats = ARRAY_SIZE(gbe13_et_stats);

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

static int gbe_probe(struct netcp_device *netcp_device, struct device *dev,
		     struct device_node *node, void **inst_priv)
{
	struct device_node *interfaces, *interface;
	struct device_node *secondary_ports;
	struct cpsw_ale_params ale_params;
	struct gbe_priv *gbe_dev;
	u32 slave_num;
	int ret = 0;

	if (!node) {
		dev_err(dev, "device tree info unavailable\n");
		return -ENODEV;
	}

	gbe_dev = devm_kzalloc(dev, sizeof(struct gbe_priv), GFP_KERNEL);
	if (!gbe_dev)
		return -ENOMEM;

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
		ret = -ENODEV;
		goto quit;
	}

	if (!strcmp(node->name, "gbe")) {
		ret = get_gbe_resource_version(gbe_dev, node);
		if (ret)
			goto quit;

		ret = set_gbe_ethss14_priv(gbe_dev, node);
		if (ret)
			goto quit;
	} else if (!strcmp(node->name, "xgbe")) {
		ret = set_xgbe_ethss10_priv(gbe_dev, node);
		if (ret)
			goto quit;
		ret = netcp_xgbe_serdes_init(gbe_dev->xgbe_serdes_regs,
					     gbe_dev->ss_regs);
		if (ret)
			goto quit;
	} else {
		dev_err(dev, "unknown GBE node(%s)\n", node->name);
		ret = -ENODEV;
		goto quit;
	}

	interfaces = of_get_child_by_name(node, "interfaces");
	if (!interfaces)
		dev_err(dev, "could not find interfaces\n");

	ret = netcp_txpipe_init(&gbe_dev->tx_pipe, netcp_device,
				gbe_dev->dma_chan_name, gbe_dev->tx_queue_id);
	if (ret)
		goto quit;

	ret = netcp_txpipe_open(&gbe_dev->tx_pipe);
	if (ret)
		goto quit;

	/* Create network interfaces */
	INIT_LIST_HEAD(&gbe_dev->gbe_intf_head);
	for_each_child_of_node(interfaces, interface) {
		ret = of_property_read_u32(interface, "slave-port", &slave_num);
		if (ret) {
			dev_err(dev, "missing slave-port parameter, skipping interface configuration for %s\n",
				interface->name);
			continue;
		}
		gbe_dev->num_slaves++;
	}

	if (!gbe_dev->num_slaves)
		dev_warn(dev, "No network interface configured\n");

	/* Initialize Secondary slave ports */
	secondary_ports = of_get_child_by_name(node, "secondary-slave-ports");
	INIT_LIST_HEAD(&gbe_dev->secondary_slaves);
	if (secondary_ports)
		init_secondary_ports(gbe_dev, secondary_ports);
	of_node_put(secondary_ports);

	if (!gbe_dev->num_slaves) {
		dev_err(dev, "No network interface or secondary ports configured\n");
		ret = -ENODEV;
		goto quit;
	}

	memset(&ale_params, 0, sizeof(ale_params));
	ale_params.dev		= gbe_dev->dev;
	ale_params.ale_regs	= gbe_dev->ale_reg;
	ale_params.ale_ageout	= GBE_DEFAULT_ALE_AGEOUT;
	ale_params.ale_entries	= gbe_dev->ale_entries;
	ale_params.ale_ports	= gbe_dev->ale_ports;

	gbe_dev->ale = cpsw_ale_create(&ale_params);
	if (!gbe_dev->ale) {
		dev_err(gbe_dev->dev, "error initializing ale engine\n");
		ret = -ENODEV;
		goto quit;
	} else {
		dev_dbg(gbe_dev->dev, "Created a gbe ale engine\n");
	}

	/* initialize host port */
	gbe_init_host_port(gbe_dev);

	init_timer(&gbe_dev->timer);
	gbe_dev->timer.data	 = (unsigned long)gbe_dev;
	gbe_dev->timer.function = netcp_ethss_timer;
	gbe_dev->timer.expires	 = jiffies + GBE_TIMER_INTERVAL;
	add_timer(&gbe_dev->timer);
	*inst_priv = gbe_dev;
	return 0;

quit:
	if (gbe_dev->hw_stats)
		devm_kfree(dev, gbe_dev->hw_stats);
	cpsw_ale_destroy(gbe_dev->ale);
	if (gbe_dev->ss_regs)
		devm_iounmap(dev, gbe_dev->ss_regs);
	of_node_put(interfaces);
	devm_kfree(dev, gbe_dev);
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
	cpsw_ale_stop(gbe_dev->ale);
	cpsw_ale_destroy(gbe_dev->ale);
	netcp_txpipe_close(&gbe_dev->tx_pipe);
	free_secondary_ports(gbe_dev);

	if (!list_empty(&gbe_dev->gbe_intf_head))
		dev_alert(gbe_dev->dev, "unreleased ethss interfaces present\n");

	devm_kfree(gbe_dev->dev, gbe_dev->hw_stats);
	devm_iounmap(gbe_dev->dev, gbe_dev->ss_regs);
	memset(gbe_dev, 0x00, sizeof(*gbe_dev));
	devm_kfree(gbe_dev->dev, gbe_dev);
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
