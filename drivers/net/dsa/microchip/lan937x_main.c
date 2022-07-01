// SPDX-License-Identifier: GPL-2.0
/* Microchip LAN937X switch driver main logic
 * Copyright (C) 2019-2022 Microchip Technology Inc.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/iopoll.h>
#include <linux/phy.h>
#include <linux/of_net.h>
#include <linux/if_bridge.h>
#include <linux/math.h>
#include <net/dsa.h>
#include <net/switchdev.h>

#include "lan937x_reg.h"
#include "ksz_common.h"
#include "lan937x.h"

static int lan937x_cfg(struct ksz_device *dev, u32 addr, u8 bits, bool set)
{
	return regmap_update_bits(dev->regmap[0], addr, bits, set ? bits : 0);
}

static int lan937x_port_cfg(struct ksz_device *dev, int port, int offset,
			    u8 bits, bool set)
{
	return regmap_update_bits(dev->regmap[0], PORT_CTRL_ADDR(port, offset),
				  bits, set ? bits : 0);
}

int lan937x_reset_switch(struct ksz_device *dev)
{
	u32 data32;
	int ret;

	/* reset switch */
	ret = lan937x_cfg(dev, REG_SW_OPERATION, SW_RESET, true);
	if (ret < 0)
		return ret;

	/* Enable Auto Aging */
	ret = lan937x_cfg(dev, REG_SW_LUE_CTRL_1, SW_LINK_AUTO_AGING, true);
	if (ret < 0)
		return ret;

	/* disable interrupts */
	ret = ksz_write32(dev, REG_SW_INT_MASK__4, SWITCH_INT_MASK);
	if (ret < 0)
		return ret;

	ret = ksz_write32(dev, REG_SW_PORT_INT_MASK__4, 0xFF);
	if (ret < 0)
		return ret;

	return ksz_read32(dev, REG_SW_PORT_INT_STATUS__4, &data32);
}

void lan937x_port_setup(struct ksz_device *dev, int port, bool cpu_port)
{
	struct dsa_switch *ds = dev->ds;
	u8 member;

	/* enable tag tail for host port */
	if (cpu_port)
		lan937x_port_cfg(dev, port, REG_PORT_CTRL_0,
				 PORT_TAIL_TAG_ENABLE, true);

	/* disable frame check length field */
	lan937x_port_cfg(dev, port, REG_PORT_MAC_CTRL_0, PORT_CHECK_LENGTH,
			 false);

	/* set back pressure for half duplex */
	lan937x_port_cfg(dev, port, REG_PORT_MAC_CTRL_1, PORT_BACK_PRESSURE,
			 true);

	/* enable 802.1p priority */
	lan937x_port_cfg(dev, port, P_PRIO_CTRL, PORT_802_1P_PRIO_ENABLE, true);

	if (!dev->info->internal_phy[port])
		lan937x_port_cfg(dev, port, REG_PORT_XMII_CTRL_0,
				 PORT_MII_TX_FLOW_CTRL | PORT_MII_RX_FLOW_CTRL,
				 true);

	if (cpu_port)
		member = dsa_user_ports(ds);
	else
		member = BIT(dsa_upstream_port(ds, port));

	dev->dev_ops->cfg_port_member(dev, port, member);
}

void lan937x_config_cpu_port(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;
	struct dsa_port *dp;

	dsa_switch_for_each_cpu_port(dp, ds) {
		if (dev->info->cpu_ports & (1 << dp->index)) {
			dev->cpu_port = dp->index;

			/* enable cpu port */
			lan937x_port_setup(dev, dp->index, true);
		}
	}

	dsa_switch_for_each_user_port(dp, ds) {
		ksz_port_stp_state_set(ds, dp->index, BR_STATE_DISABLED);
	}
}

int lan937x_setup(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;

	/* The VLAN aware is a global setting. Mixed vlan
	 * filterings are not supported.
	 */
	ds->vlan_filtering_is_global = true;

	/* Enable aggressive back off for half duplex & UNH mode */
	lan937x_cfg(dev, REG_SW_MAC_CTRL_0,
		    (SW_PAUSE_UNH_MODE | SW_NEW_BACKOFF | SW_AGGR_BACKOFF),
		    true);

	/* If NO_EXC_COLLISION_DROP bit is set, the switch will not drop
	 * packets when 16 or more collisions occur
	 */
	lan937x_cfg(dev, REG_SW_MAC_CTRL_1, NO_EXC_COLLISION_DROP, true);

	/* enable global MIB counter freeze function */
	lan937x_cfg(dev, REG_SW_MAC_CTRL_6, SW_MIB_COUNTER_FREEZE, true);

	/* disable CLK125 & CLK25, 1: disable, 0: enable */
	lan937x_cfg(dev, REG_SW_GLOBAL_OUTPUT_CTRL__1,
		    (SW_CLK125_ENB | SW_CLK25_ENB), true);

	return 0;
}

int lan937x_switch_init(struct ksz_device *dev)
{
	dev->port_mask = (1 << dev->info->port_cnt) - 1;

	return 0;
}

void lan937x_switch_exit(struct ksz_device *dev)
{
	lan937x_reset_switch(dev);
}

MODULE_AUTHOR("Arun Ramadoss <arun.ramadoss@microchip.com>");
MODULE_DESCRIPTION("Microchip LAN937x Series Switch DSA Driver");
MODULE_LICENSE("GPL");
