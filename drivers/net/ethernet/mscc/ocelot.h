/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 */

#ifndef _MSCC_OCELOT_H_
#define _MSCC_OCELOT_H_

#include <linux/bitops.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/net_tstamp.h>
#include <linux/phy.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/regmap.h>

#include <soc/mscc/ocelot_qsys.h>
#include <soc/mscc/ocelot_sys.h>
#include <soc/mscc/ocelot_dev.h>
#include <soc/mscc/ocelot_ana.h>
#include <soc/mscc/ocelot.h>
#include "ocelot_rew.h"
#include "ocelot_qs.h"
#include "ocelot_tc.h"
#include "ocelot_ptp.h"

#define OCELOT_BUFFER_CELL_SZ 60

#define OCELOT_STATS_CHECK_DELAY (2 * HZ)

#define OCELOT_PTP_QUEUE_SZ	128

struct frame_info {
	u32 len;
	u16 port;
	u16 vid;
	u8 tag_type;
	u16 rew_op;
	u32 timestamp;	/* rew_val */
};

struct ocelot_multicast {
	struct list_head list;
	unsigned char addr[ETH_ALEN];
	u16 vid;
	u16 ports;
};

struct ocelot_port_private {
	struct ocelot_port port;
	struct net_device *dev;
	struct phy_device *phy;
	u8 chip_port;

	u8 vlan_aware;

	struct phy *serdes;

	struct ocelot_port_tc tc;
};

u32 ocelot_port_readl(struct ocelot_port *port, u32 reg);
void ocelot_port_writel(struct ocelot_port *port, u32 val, u32 reg);

#define ocelot_field_write(ocelot, reg, val) regmap_field_write((ocelot)->regfields[(reg)], (val))
#define ocelot_field_read(ocelot, reg, val) regmap_field_read((ocelot)->regfields[(reg)], (val))

int ocelot_chip_init(struct ocelot *ocelot, const struct ocelot_ops *ops);
int ocelot_probe_port(struct ocelot *ocelot, u8 port,
		      void __iomem *regs,
		      struct phy_device *phy);

void ocelot_set_cpu_port(struct ocelot *ocelot, int cpu,
			 enum ocelot_tag_prefix injection,
			 enum ocelot_tag_prefix extraction);

extern struct notifier_block ocelot_netdevice_nb;
extern struct notifier_block ocelot_switchdev_nb;
extern struct notifier_block ocelot_switchdev_blocking_nb;

#define ocelot_field_write(ocelot, reg, val) regmap_field_write((ocelot)->regfields[(reg)], (val))
#define ocelot_field_read(ocelot, reg, val) regmap_field_read((ocelot)->regfields[(reg)], (val))

#endif
