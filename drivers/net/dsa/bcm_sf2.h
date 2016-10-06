/*
 * Broadcom Starfighter2 private context
 *
 * Copyright (C) 2014, Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __BCM_SF2_H
#define __BCM_SF2_H

#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/if_vlan.h>

#include <net/dsa.h>

#include "bcm_sf2_regs.h"

struct bcm_sf2_hw_params {
	u16	top_rev;
	u16	core_rev;
	u16	gphy_rev;
	u32	num_gphy;
	u8	num_acb_queue;
	u8	num_rgmii;
	u8	num_ports;
	u8	fcb_pause_override:1;
	u8	acb_packets_inflight:1;
};

#define BCM_SF2_REGS_NAME {\
	"core", "reg", "intrl2_0", "intrl2_1", "fcb", "acb" \
}

#define BCM_SF2_REGS_NUM	6

struct bcm_sf2_port_status {
	unsigned int link;

	struct ethtool_eee eee;

	u32 vlan_ctl_mask;
	u16 pvid;

	struct net_device *bridge_dev;
};

struct bcm_sf2_arl_entry {
	u8 port;
	u8 mac[ETH_ALEN];
	u16 vid;
	u8 is_valid:1;
	u8 is_age:1;
	u8 is_static:1;
};

struct bcm_sf2_vlan {
	u16 members;
	u16 untag;
};

static inline void bcm_sf2_mac_from_u64(u64 src, u8 *dst)
{
	unsigned int i;

	for (i = 0; i < ETH_ALEN; i++)
		dst[ETH_ALEN - 1 - i] = (src >> (8 * i)) & 0xff;
}

static inline u64 bcm_sf2_mac_to_u64(const u8 *src)
{
	unsigned int i;
	u64 dst = 0;

	for (i = 0; i < ETH_ALEN; i++)
		dst |= (u64)src[ETH_ALEN - 1 - i] << (8 * i);

	return dst;
}

static inline void bcm_sf2_arl_to_entry(struct bcm_sf2_arl_entry *ent,
					u64 mac_vid, u32 fwd_entry)
{
	memset(ent, 0, sizeof(*ent));
	ent->port = fwd_entry & PORTID_MASK;
	ent->is_valid = !!(fwd_entry & ARL_VALID);
	ent->is_age = !!(fwd_entry & ARL_AGE);
	ent->is_static = !!(fwd_entry & ARL_STATIC);
	bcm_sf2_mac_from_u64(mac_vid, ent->mac);
	ent->vid = mac_vid >> VID_SHIFT;
}

static inline void bcm_sf2_arl_from_entry(u64 *mac_vid, u32 *fwd_entry,
					  const struct bcm_sf2_arl_entry *ent)
{
	*mac_vid = bcm_sf2_mac_to_u64(ent->mac);
	*mac_vid |= (u64)(ent->vid & VID_MASK) << VID_SHIFT;
	*fwd_entry = ent->port & PORTID_MASK;
	if (ent->is_valid)
		*fwd_entry |= ARL_VALID;
	if (ent->is_static)
		*fwd_entry |= ARL_STATIC;
	if (ent->is_age)
		*fwd_entry |= ARL_AGE;
}

struct bcm_sf2_priv {
	/* Base registers, keep those in order with BCM_SF2_REGS_NAME */
	void __iomem			*core;
	void __iomem			*reg;
	void __iomem			*intrl2_0;
	void __iomem			*intrl2_1;
	void __iomem			*fcb;
	void __iomem			*acb;

	/* spinlock protecting access to the indirect registers */
	spinlock_t			indir_lock;

	int				irq0;
	int				irq1;
	u32				irq0_stat;
	u32				irq0_mask;
	u32				irq1_stat;
	u32				irq1_mask;

	/* Mutex protecting access to the MIB counters */
	struct mutex			stats_mutex;

	struct bcm_sf2_hw_params	hw_params;

	struct bcm_sf2_port_status	port_sts[DSA_MAX_PORTS];

	/* Mask of ports enabled for Wake-on-LAN */
	u32				wol_ports_mask;

	/* MoCA port location */
	int				moca_port;

	/* Bitmask of ports having an integrated PHY */
	unsigned int			int_phy_mask;

	/* Master and slave MDIO bus controller */
	unsigned int			indir_phy_mask;
	struct device_node		*master_mii_dn;
	struct mii_bus			*slave_mii_bus;
	struct mii_bus			*master_mii_bus;

	/* Cache of programmed VLANs */
	struct bcm_sf2_vlan		vlans[VLAN_N_VID];
};

struct bcm_sf2_hw_stats {
	const char	*string;
	u16		reg;
	u8		sizeof_stat;
};

#define SF2_IO_MACRO(name) \
static inline u32 name##_readl(struct bcm_sf2_priv *priv, u32 off)	\
{									\
	return __raw_readl(priv->name + off);				\
}									\
static inline void name##_writel(struct bcm_sf2_priv *priv,		\
				  u32 val, u32 off)			\
{									\
	__raw_writel(val, priv->name + off);				\
}									\

/* Accesses to 64-bits register requires us to latch the hi/lo pairs
 * using the REG_DIR_DATA_{READ,WRITE} ancillary registers. The 'indir_lock'
 * spinlock is automatically grabbed and released to provide relative
 * atomiticy with latched reads/writes.
 */
#define SF2_IO64_MACRO(name) \
static inline u64 name##_readq(struct bcm_sf2_priv *priv, u32 off)	\
{									\
	u32 indir, dir;							\
	spin_lock(&priv->indir_lock);					\
	dir = __raw_readl(priv->name + off);				\
	indir = reg_readl(priv, REG_DIR_DATA_READ);			\
	spin_unlock(&priv->indir_lock);					\
	return (u64)indir << 32 | dir;					\
}									\
static inline void name##_writeq(struct bcm_sf2_priv *priv, u64 val,	\
							u32 off)	\
{									\
	spin_lock(&priv->indir_lock);					\
	reg_writel(priv, upper_32_bits(val), REG_DIR_DATA_WRITE);	\
	__raw_writel(lower_32_bits(val), priv->name + off);		\
	spin_unlock(&priv->indir_lock);					\
}

#define SWITCH_INTR_L2(which)						\
static inline void intrl2_##which##_mask_clear(struct bcm_sf2_priv *priv, \
						u32 mask)		\
{									\
	priv->irq##which##_mask &= ~(mask);				\
	intrl2_##which##_writel(priv, mask, INTRL2_CPU_MASK_CLEAR);	\
}									\
static inline void intrl2_##which##_mask_set(struct bcm_sf2_priv *priv, \
						u32 mask)		\
{									\
	intrl2_## which##_writel(priv, mask, INTRL2_CPU_MASK_SET);	\
	priv->irq##which##_mask |= (mask);				\
}									\

SF2_IO_MACRO(core);
SF2_IO_MACRO(reg);
SF2_IO64_MACRO(core);
SF2_IO_MACRO(intrl2_0);
SF2_IO_MACRO(intrl2_1);
SF2_IO_MACRO(fcb);
SF2_IO_MACRO(acb);

SWITCH_INTR_L2(0);
SWITCH_INTR_L2(1);

#endif /* __BCM_SF2_H */
