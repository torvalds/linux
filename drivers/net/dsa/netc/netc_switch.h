/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Copyright 2025-2026 NXP
 */

#ifndef _NETC_SWITCH_H
#define _NETC_SWITCH_H

#include <linux/dsa/tag_netc.h>
#include <linux/fsl/netc_global.h>
#include <linux/fsl/ntmp.h>
#include <linux/of_device.h>
#include <linux/of_net.h>
#include <linux/pci.h>

#include "netc_switch_hw.h"

#define NETC_REGS_BAR			0
#define NETC_REGS_SIZE			0x80000
#define NETC_MSIX_TBL_BAR		2
#define NETC_REGS_PORT_BASE		0x4000
/* register block size per port  */
#define NETC_REGS_PORT_SIZE		0x4000
#define PORT_IOBASE(p)			(NETC_REGS_PORT_SIZE * (p))
#define NETC_REGS_GLOBAL_BASE		0x70000

#define NETC_SWITCH_REV_4_3		0x0403

#define NETC_TC_NUM			8
#define NETC_CBDR_NUM			2
#define NETC_IPV_NUM			8

#define NETC_MAX_FRAME_LEN		9600

#define NETC_STANDALONE_PVID		0
#define NETC_VLAN_UNAWARE_PVID(br_id)	(4096 - (br_id))

/* Threshold format: MANT (bits 11:4) * 2^EXP (bits 3:0)
 * Unit: Memory words (average of 20 bytes each)
 * NETC_BP_THRESH = 0x8c3, MANT = 0x8c, EXP = 3. Threshold: 1120 words
 * NETC_FC_THRESH_ON = 0x733, MANT = 0x73, EXP = 3. Threshold: 920 words
 * NETC_FC_THRESH_OFF = 0x263, MANT = 0x26, EXP = 3. Threshold: 304 words
 */
#define NETC_BP_THRESH			0x8c3
#define NETC_FC_THRESH_ON		0x733
#define NETC_FC_THRESH_OFF		0x263

/* PAUSE quanta: 0xFFFF = 65535 quanta (each quanta = 512 bit times) */
#define NETC_PAUSE_QUANTA		0xFFFF
/* PAUSE refresh threshold: send refresh when timer reaches this value */
#define NETC_PAUSE_THRESH		0x7FFF

#define NETC_FDBT_AGEING_DELAY		(3 * HZ)
#define NETC_FDBT_AGEING_THRESH		100

struct netc_switch;

struct netc_switch_info {
	u32 num_ports;
	void (*phylink_get_caps)(int port, struct phylink_config *config);
};

struct netc_port_caps {
	u32 half_duplex:1; /* indicates whether the port support half-duplex */
	u32 pmac:1;	  /* indicates whether the port has preemption MAC */
	u32 pseudo_link:1;
};

enum netc_host_reason {
	/* Software defined host reasons */
	NETC_HR_HOST_FLOOD = 8,
};

struct netc_port {
	void __iomem *iobase;
	struct netc_switch *switch_priv;
	struct netc_port_caps caps;
	struct dsa_port *dp;
	struct clk *ref_clk; /* RGMII/RMII reference clock */
	struct mii_bus *emdio;
	int ett_offset;

	u16 enable:1;
	u16 uc:1;
	u16 mc:1;
	u16 pvid;
	struct ipft_entry_data *host_flood;
};

struct netc_switch_regs {
	void __iomem *base;
	void __iomem *port;
	void __iomem *global;
};

struct netc_fdb_entry {
	u32 entry_id;
	struct fdbt_cfge_data cfge;
	struct fdbt_keye_data keye;
	struct hlist_node node;
};

struct netc_vlan_entry {
	u16 vid;
	u32 ect_gid;
	u32 untagged_port_bitmap;
	struct vft_cfge_data cfge;
	struct hlist_node node;
};

struct netc_port_stat {
	int reg;
	char name[ETH_GSTRING_LEN] __nonstring;
};

struct netc_switch {
	struct pci_dev *pdev;
	struct device *dev;
	struct dsa_switch *ds;
	u16 revision;

	const struct netc_switch_info *info;
	struct netc_switch_regs regs;
	struct netc_port **ports;
	u32 port_bitmap; /* bitmap of available ports */

	struct ntmp_user ntmp;
	struct hlist_head fdb_list;
	struct mutex fdbt_lock; /* FDB table lock */
	struct delayed_work fdbt_ageing_work;
	/* (fdbt_ageing_delay * NETC_FDBT_AGEING_THRESH) is ageing time */
	unsigned long fdbt_ageing_delay;
	atomic_t br_cnt;
	struct hlist_head vlan_list;
	struct mutex vft_lock; /* VLAN filter table lock */

	/* Switch hardware capabilities */
	u32 htmcapr_num_words;
	u32 num_bp;

	struct bpt_cfge_data *bpt_list;
};

#define NETC_PRIV(ds)			((struct netc_switch *)((ds)->priv))
#define NETC_PORT(ds, port_id)		(NETC_PRIV(ds)->ports[(port_id)])

/* Write/Read Switch base registers */
#define netc_base_rd(r, o)		netc_read((r)->base + (o))
#define netc_base_wr(r, o, v)		netc_write((r)->base + (o), v)

/* Write/Read registers of Switch Port (including pseudo MAC port) */
#define netc_port_rd(p, o)		netc_read((p)->iobase + (o))
#define netc_port_rd64(p, o)		netc_read64((p)->iobase + (o))
#define netc_port_wr(p, o, v)		netc_write((p)->iobase + (o), v)

/* Write/Read Switch global registers */
#define netc_glb_rd(r, o)		netc_read((r)->global + (o))
#define netc_glb_wr(r, o, v)		netc_write((r)->global + (o), v)

static inline bool is_netc_pseudo_port(struct netc_port *np)
{
	return np->caps.pseudo_link;
}

static inline void netc_add_fdb_entry(struct netc_switch *priv,
				      struct netc_fdb_entry *entry)
{
	hlist_add_head(&entry->node, &priv->fdb_list);
}

static inline void netc_del_fdb_entry(struct netc_fdb_entry *entry)
{
	hlist_del(&entry->node);
	kfree(entry);
}

static inline void netc_add_vlan_entry(struct netc_switch *priv,
				       struct netc_vlan_entry *entry)
{
	hlist_add_head(&entry->node, &priv->vlan_list);
}

static inline void netc_del_vlan_entry(struct netc_vlan_entry *entry)
{
	hlist_del(&entry->node);
	kfree(entry);
}

int netc_switch_platform_probe(struct netc_switch *priv);

/* ethtool APIs */
void netc_port_get_pause_stats(struct dsa_switch *ds, int port,
			       struct ethtool_pause_stats *pause_stats);
void netc_port_get_rmon_stats(struct dsa_switch *ds, int port,
			      struct ethtool_rmon_stats *rmon_stats,
			      const struct ethtool_rmon_hist_range **ranges);
void netc_port_get_eth_ctrl_stats(struct dsa_switch *ds, int port,
				  struct ethtool_eth_ctrl_stats *ctrl_stats);
void netc_port_get_eth_mac_stats(struct dsa_switch *ds, int port,
				 struct ethtool_eth_mac_stats *mac_stats);
int netc_port_get_sset_count(struct dsa_switch *ds, int port, int sset);
void netc_port_get_strings(struct dsa_switch *ds, int port,
			   u32 sset, u8 *data);
void netc_port_get_ethtool_stats(struct dsa_switch *ds, int port, u64 *data);

#endif
