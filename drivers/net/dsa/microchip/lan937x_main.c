// SPDX-License-Identifier: GPL-2.0
/* Microchip LAN937X switch driver main logic
 * Copyright (C) 2019-2024 Microchip Technology Inc.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/iopoll.h>
#include <linux/phy.h>
#include <linux/of_net.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/math.h>
#include <net/dsa.h>
#include <net/switchdev.h>

#include "lan937x_reg.h"
#include "ksz_common.h"
#include "ksz9477.h"
#include "lan937x.h"

/* marker for ports without built-in PHY */
#define LAN937X_NO_PHY U8_MAX

/*
 * lan9370_phy_addr - Mapping of LAN9370 switch ports to PHY addresses.
 *
 * Each entry corresponds to a specific port on the LAN9370 switch,
 * where ports 1-4 are connected to integrated 100BASE-T1 PHYs, and
 * Port 5 is connected to an RGMII interface without a PHY. The values
 * are based on the documentation (DS00003108E, section 3.3).
 */
static const u8 lan9370_phy_addr[] = {
	[0] = 2, /* Port 1, T1 AFE0 */
	[1] = 3, /* Port 2, T1 AFE1 */
	[2] = 5, /* Port 3, T1 AFE3 */
	[3] = 6, /* Port 4, T1 AFE4 */
	[4] = LAN937X_NO_PHY, /* Port 5, RGMII 2 */
};

/*
 * lan9371_phy_addr - Mapping of LAN9371 switch ports to PHY addresses.
 *
 * The values are based on the documentation (DS00003109E, section 3.3).
 */
static const u8 lan9371_phy_addr[] = {
	[0] = 2, /* Port 1, T1 AFE0 */
	[1] = 3, /* Port 2, T1 AFE1 */
	[2] = 5, /* Port 3, T1 AFE3 */
	[3] = 8, /* Port 4, TX PHY */
	[4] = LAN937X_NO_PHY, /* Port 5, RGMII 2 */
	[5] = LAN937X_NO_PHY, /* Port 6, RGMII 1 */
};

/*
 * lan9372_phy_addr - Mapping of LAN9372 switch ports to PHY addresses.
 *
 * The values are based on the documentation (DS00003110F, section 3.3).
 */
static const u8 lan9372_phy_addr[] = {
	[0] = 2, /* Port 1, T1 AFE0 */
	[1] = 3, /* Port 2, T1 AFE1 */
	[2] = 5, /* Port 3, T1 AFE3 */
	[3] = 8, /* Port 4, TX PHY */
	[4] = LAN937X_NO_PHY, /* Port 5, RGMII 2 */
	[5] = LAN937X_NO_PHY, /* Port 6, RGMII 1 */
	[6] = 6, /* Port 7, T1 AFE4 */
	[7] = 4, /* Port 8, T1 AFE2 */
};

/*
 * lan9373_phy_addr - Mapping of LAN9373 switch ports to PHY addresses.
 *
 * The values are based on the documentation (DS00003110F, section 3.3).
 */
static const u8 lan9373_phy_addr[] = {
	[0] = 2, /* Port 1, T1 AFE0 */
	[1] = 3, /* Port 2, T1 AFE1 */
	[2] = 5, /* Port 3, T1 AFE3 */
	[3] = LAN937X_NO_PHY, /* Port 4, SGMII */
	[4] = LAN937X_NO_PHY, /* Port 5, RGMII 2 */
	[5] = LAN937X_NO_PHY, /* Port 6, RGMII 1 */
	[6] = 6, /* Port 7, T1 AFE4 */
	[7] = 4, /* Port 8, T1 AFE2 */
};

/*
 * lan9374_phy_addr - Mapping of LAN9374 switch ports to PHY addresses.
 *
 * The values are based on the documentation (DS00003110F, section 3.3).
 */
static const u8 lan9374_phy_addr[] = {
	[0] = 2, /* Port 1, T1 AFE0 */
	[1] = 3, /* Port 2, T1 AFE1 */
	[2] = 5, /* Port 3, T1 AFE3 */
	[3] = 7, /* Port 4, T1 AFE5 */
	[4] = LAN937X_NO_PHY, /* Port 5, RGMII 2 */
	[5] = LAN937X_NO_PHY, /* Port 6, RGMII 1 */
	[6] = 6, /* Port 7, T1 AFE4 */
	[7] = 4, /* Port 8, T1 AFE2 */
};

static int lan937x_cfg(struct ksz_device *dev, u32 addr, u8 bits, bool set)
{
	return regmap_update_bits(ksz_regmap_8(dev), addr, bits, set ? bits : 0);
}

static int lan937x_port_cfg(struct ksz_device *dev, int port, int offset,
			    u8 bits, bool set)
{
	return regmap_update_bits(ksz_regmap_8(dev), PORT_CTRL_ADDR(port, offset),
				  bits, set ? bits : 0);
}

/**
 * lan937x_create_phy_addr_map - Create port-to-PHY address map for MDIO bus.
 * @dev: Pointer to device structure.
 * @side_mdio: Boolean indicating if the PHYs are accessed over a side MDIO bus.
 *
 * This function sets up the PHY address mapping for the LAN937x switches,
 * which support two access modes for internal PHYs:
 * 1. **SPI Access**: A straightforward one-to-one port-to-PHY address
 *    mapping is applied.
 * 2. **MDIO Access**: The PHY address mapping varies based on chip variant
 *    and strap configuration. An offset is calculated based on strap settings
 *    to ensure correct PHY addresses are assigned. The offset calculation logic
 *    is based on Microchip's Article Number 000015828, available at:
 *    https://microchip.my.site.com/s/article/LAN9374-Virtual-PHY-PHY-Address-Mapping
 *
 * The function first checks if side MDIO access is disabled, in which case a
 * simple direct mapping (port number = PHY address) is applied. If side MDIO
 * access is enabled, it reads the strap configuration to determine the correct
 * offset for PHY addresses.
 *
 * The appropriate mapping table is selected based on the chip ID, and the
 * `phy_addr_map` is populated with the correct addresses for each port. Any
 * port with no PHY is assigned a `LAN937X_NO_PHY` marker.
 *
 * Return: 0 on success, error code on failure.
 */
int lan937x_create_phy_addr_map(struct ksz_device *dev, bool side_mdio)
{
	static const u8 *phy_addr_map;
	u32 strap_val;
	u8 offset = 0;
	size_t size;
	int ret, i;

	if (!side_mdio) {
		/* simple direct mapping */
		for (i = 0; i < dev->info->port_cnt; i++)
			dev->phy_addr_map[i] = i;

		return 0;
	}

	ret = ksz_read32(dev, REG_SW_CFG_STRAP_VAL, &strap_val);
	if (ret < 0)
		return ret;

	if (!(strap_val & SW_CASCADE_ID_CFG) && !(strap_val & SW_VPHY_ADD_CFG))
		offset = 0;
	else if (!(strap_val & SW_CASCADE_ID_CFG) && (strap_val & SW_VPHY_ADD_CFG))
		offset = 7;
	else if ((strap_val & SW_CASCADE_ID_CFG) && !(strap_val & SW_VPHY_ADD_CFG))
		offset = 15;
	else
		offset = 22;

	switch (dev->info->chip_id) {
	case LAN9370_CHIP_ID:
		phy_addr_map = lan9370_phy_addr;
		size = ARRAY_SIZE(lan9370_phy_addr);
		break;
	case LAN9371_CHIP_ID:
		phy_addr_map = lan9371_phy_addr;
		size = ARRAY_SIZE(lan9371_phy_addr);
		break;
	case LAN9372_CHIP_ID:
		phy_addr_map = lan9372_phy_addr;
		size = ARRAY_SIZE(lan9372_phy_addr);
		break;
	case LAN9373_CHIP_ID:
		phy_addr_map = lan9373_phy_addr;
		size = ARRAY_SIZE(lan9373_phy_addr);
		break;
	case LAN9374_CHIP_ID:
		phy_addr_map = lan9374_phy_addr;
		size = ARRAY_SIZE(lan9374_phy_addr);
		break;
	default:
		return -EINVAL;
	}

	if (size < dev->info->port_cnt)
		return -EINVAL;

	for (i = 0; i < dev->info->port_cnt; i++) {
		if (phy_addr_map[i] == LAN937X_NO_PHY)
			dev->phy_addr_map[i] = phy_addr_map[i];
		else
			dev->phy_addr_map[i] = phy_addr_map[i] + offset;
	}

	return 0;
}

/**
 * lan937x_mdio_bus_preinit - Pre-initialize MDIO bus for accessing PHYs.
 * @dev: Pointer to device structure.
 * @side_mdio: Boolean indicating if the PHYs are accessed over a side MDIO bus.
 *
 * This function configures the LAN937x switch for PHY access either through
 * SPI or the side MDIO bus, unlocking the necessary registers for each access
 * mode.
 *
 * Operation Modes:
 * 1. **SPI Access**: Enables SPI indirect access to address clock domain
 *    crossing issues when SPI is used for PHY access.
 * 2. **MDIO Access**: Grants access to internal PHYs over the side MDIO bus,
 *    required when using the MDIO bus for PHY management.
 *
 * Return: 0 on success, error code on failure.
 */
int lan937x_mdio_bus_preinit(struct ksz_device *dev, bool side_mdio)
{
	u16 data16;
	int ret;

	/* Unlock access to the PHYs, needed for SPI and side MDIO access */
	ret = lan937x_cfg(dev, REG_GLOBAL_CTRL_0, SW_PHY_REG_BLOCK, false);
	if (ret < 0)
		goto print_error;

	if (side_mdio)
		/* Allow access to internal PHYs over MDIO bus */
		data16 = VPHY_MDIO_INTERNAL_ENABLE;
	else
		/* Enable SPI indirect access to address clock domain crossing
		 * issue
		 */
		data16 = VPHY_SPI_INDIRECT_ENABLE;

	ret = ksz_rmw16(dev, REG_VPHY_SPECIAL_CTRL__2,
			VPHY_SPI_INDIRECT_ENABLE | VPHY_MDIO_INTERNAL_ENABLE,
			data16);

print_error:
	if (ret < 0)
		dev_err(dev->dev, "failed to preinit the MDIO bus\n");

	return ret;
}

static int lan937x_vphy_ind_addr_wr(struct ksz_device *dev, int addr, int reg)
{
	u16 addr_base = REG_PORT_T1_PHY_CTRL_BASE;
	u16 temp;

	if (is_lan937x_tx_phy(dev, addr))
		addr_base = REG_PORT_TX_PHY_CTRL_BASE;

	/* get register address based on the logical port */
	temp = PORT_CTRL_ADDR(addr, (addr_base + (reg << 2)));

	return ksz_write16(dev, REG_VPHY_IND_ADDR__2, temp);
}

static int lan937x_internal_phy_write(struct ksz_device *dev, int addr, int reg,
				      u16 val)
{
	unsigned int value;
	int ret;

	/* Check for internal phy port */
	if (!dev->info->internal_phy[addr])
		return -EOPNOTSUPP;

	ret = lan937x_vphy_ind_addr_wr(dev, addr, reg);
	if (ret < 0)
		return ret;

	/* Write the data to be written to the VPHY reg */
	ret = ksz_write16(dev, REG_VPHY_IND_DATA__2, val);
	if (ret < 0)
		return ret;

	/* Write the Write En and Busy bit */
	ret = ksz_write16(dev, REG_VPHY_IND_CTRL__2,
			  (VPHY_IND_WRITE | VPHY_IND_BUSY));
	if (ret < 0)
		return ret;

	ret = regmap_read_poll_timeout(ksz_regmap_16(dev), REG_VPHY_IND_CTRL__2,
				       value, !(value & VPHY_IND_BUSY), 10,
				       1000);
	if (ret < 0) {
		dev_err(dev->dev, "Failed to write phy register\n");
		return ret;
	}

	return 0;
}

static int lan937x_internal_phy_read(struct ksz_device *dev, int addr, int reg,
				     u16 *val)
{
	unsigned int value;
	int ret;

	/* Check for internal phy port, return 0xffff for non-existent phy */
	if (!dev->info->internal_phy[addr])
		return 0xffff;

	ret = lan937x_vphy_ind_addr_wr(dev, addr, reg);
	if (ret < 0)
		return ret;

	/* Write Read and Busy bit to start the transaction */
	ret = ksz_write16(dev, REG_VPHY_IND_CTRL__2, VPHY_IND_BUSY);
	if (ret < 0)
		return ret;

	ret = regmap_read_poll_timeout(ksz_regmap_16(dev), REG_VPHY_IND_CTRL__2,
				       value, !(value & VPHY_IND_BUSY), 10,
				       1000);
	if (ret < 0) {
		dev_err(dev->dev, "Failed to read phy register\n");
		return ret;
	}

	/* Read the VPHY register which has the PHY data */
	return ksz_read16(dev, REG_VPHY_IND_DATA__2, val);
}

int lan937x_r_phy(struct ksz_device *dev, u16 addr, u16 reg, u16 *data)
{
	return lan937x_internal_phy_read(dev, addr, reg, data);
}

int lan937x_w_phy(struct ksz_device *dev, u16 addr, u16 reg, u16 val)
{
	return lan937x_internal_phy_write(dev, addr, reg, val);
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

	ret = ksz_write32(dev, REG_SW_INT_STATUS__4, POR_READY_INT);
	if (ret < 0)
		return ret;

	ret = ksz_write32(dev, REG_SW_PORT_INT_MASK__4, 0xFF);
	if (ret < 0)
		return ret;

	return ksz_read32(dev, REG_SW_PORT_INT_STATUS__4, &data32);
}

void lan937x_port_setup(struct ksz_device *dev, int port, bool cpu_port)
{
	const u32 *masks = dev->info->masks;
	const u16 *regs = dev->info->regs;
	struct dsa_switch *ds = dev->ds;
	u8 member;

	/* enable tag tail for host port */
	if (cpu_port)
		lan937x_port_cfg(dev, port, REG_PORT_CTRL_0,
				 PORT_TAIL_TAG_ENABLE, true);

	/* Enable the Port Queue split */
	ksz9477_port_queue_split(dev, port);

	/* set back pressure for half duplex */
	lan937x_port_cfg(dev, port, REG_PORT_MAC_CTRL_1, PORT_BACK_PRESSURE,
			 true);

	/* enable 802.1p priority */
	lan937x_port_cfg(dev, port, P_PRIO_CTRL, PORT_802_1P_PRIO_ENABLE, true);

	if (!dev->info->internal_phy[port])
		lan937x_port_cfg(dev, port, regs[P_XMII_CTRL_0],
				 masks[P_MII_TX_FLOW_CTRL] |
				 masks[P_MII_RX_FLOW_CTRL],
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

int lan937x_change_mtu(struct ksz_device *dev, int port, int new_mtu)
{
	struct dsa_switch *ds = dev->ds;
	int ret;

	new_mtu += VLAN_ETH_HLEN + ETH_FCS_LEN;

	if (dsa_is_cpu_port(ds, port))
		new_mtu += LAN937X_TAG_LEN;

	if (new_mtu >= FR_MIN_SIZE)
		ret = lan937x_port_cfg(dev, port, REG_PORT_MAC_CTRL_0,
				       PORT_JUMBO_PACKET, true);
	else
		ret = lan937x_port_cfg(dev, port, REG_PORT_MAC_CTRL_0,
				       PORT_JUMBO_PACKET, false);
	if (ret < 0) {
		dev_err(ds->dev, "failed to enable jumbo\n");
		return ret;
	}

	/* Write the frame size in PORT_MAX_FR_SIZE register */
	ret = ksz_pwrite16(dev, port, PORT_MAX_FR_SIZE, new_mtu);
	if (ret) {
		dev_err(ds->dev, "failed to update mtu for port %d\n", port);
		return ret;
	}

	return 0;
}

int lan937x_set_ageing_time(struct ksz_device *dev, unsigned int msecs)
{
	u8 data, mult, value8;
	bool in_msec = false;
	u32 max_val, value;
	u32 secs = msecs;
	int ret;

#define MAX_TIMER_VAL	((1 << 20) - 1)

	/* The aging timer comprises a 3-bit multiplier and a 20-bit second
	 * value.  Either of them cannot be zero.  The maximum timer is then
	 * 7 * 1048575 = 7340025 seconds.  As this value is too large for
	 * practical use it can be interpreted as microseconds, making the
	 * maximum timer 7340 seconds with finer control.  This allows for
	 * maximum 122 minutes compared to 29 minutes in KSZ9477 switch.
	 */
	if (msecs % 1000)
		in_msec = true;
	else
		secs /= 1000;
	if (!secs)
		secs = 1;

	/* Return error if too large. */
	else if (secs > 7 * MAX_TIMER_VAL)
		return -EINVAL;

	/* Configure how to interpret the number value. */
	ret = ksz_rmw8(dev, REG_SW_LUE_CTRL_2, SW_AGE_CNT_IN_MICROSEC,
		       in_msec ? SW_AGE_CNT_IN_MICROSEC : 0);
	if (ret < 0)
		return ret;

	ret = ksz_read8(dev, REG_SW_LUE_CTRL_0, &value8);
	if (ret < 0)
		return ret;

	/* Check whether there is need to update the multiplier. */
	mult = FIELD_GET(SW_AGE_CNT_M, value8);
	max_val = MAX_TIMER_VAL;
	if (mult > 0) {
		/* Try to use the same multiplier already in the register as
		 * the hardware default uses multiplier 4 and 75 seconds for
		 * 300 seconds.
		 */
		max_val = DIV_ROUND_UP(secs, mult);
		if (max_val > MAX_TIMER_VAL || max_val * mult != secs)
			max_val = MAX_TIMER_VAL;
	}

	data = DIV_ROUND_UP(secs, max_val);
	if (mult != data) {
		value8 &= ~SW_AGE_CNT_M;
		value8 |= FIELD_PREP(SW_AGE_CNT_M, data);
		ret = ksz_write8(dev, REG_SW_LUE_CTRL_0, value8);
		if (ret < 0)
			return ret;
	}

	secs = DIV_ROUND_UP(secs, data);

	value = FIELD_GET(SW_AGE_PERIOD_7_0_M, secs);

	ret = ksz_write8(dev, REG_SW_AGE_PERIOD__1, value);
	if (ret < 0)
		return ret;

	value = FIELD_GET(SW_AGE_PERIOD_19_8_M, secs);

	return ksz_write16(dev, REG_SW_AGE_PERIOD__2, value);
}

static void lan937x_set_tune_adj(struct ksz_device *dev, int port,
				 u16 reg, u8 val)
{
	u16 data16;

	ksz_pread16(dev, port, reg, &data16);

	/* Update tune Adjust */
	data16 |= FIELD_PREP(PORT_TUNE_ADJ, val);
	ksz_pwrite16(dev, port, reg, data16);

	/* write DLL reset to take effect */
	data16 |= PORT_DLL_RESET;
	ksz_pwrite16(dev, port, reg, data16);
}

static void lan937x_set_rgmii_tx_delay(struct ksz_device *dev, int port)
{
	u8 val;

	/* Apply different codes based on the ports as per characterization
	 * results
	 */
	val = (port == LAN937X_RGMII_1_PORT) ? RGMII_1_TX_DELAY_2NS :
		RGMII_2_TX_DELAY_2NS;

	lan937x_set_tune_adj(dev, port, REG_PORT_XMII_CTRL_5, val);
}

static void lan937x_set_rgmii_rx_delay(struct ksz_device *dev, int port)
{
	u8 val;

	val = (port == LAN937X_RGMII_1_PORT) ? RGMII_1_RX_DELAY_2NS :
		RGMII_2_RX_DELAY_2NS;

	lan937x_set_tune_adj(dev, port, REG_PORT_XMII_CTRL_4, val);
}

void lan937x_phylink_get_caps(struct ksz_device *dev, int port,
			      struct phylink_config *config)
{
	config->mac_capabilities = MAC_100FD;

	if (dev->info->supports_rgmii[port]) {
		/* MII/RMII/RGMII ports */
		config->mac_capabilities |= MAC_ASYM_PAUSE | MAC_SYM_PAUSE |
					    MAC_100HD | MAC_10 | MAC_1000FD;
	} else if (is_lan937x_tx_phy(dev, port)) {
		config->mac_capabilities |= MAC_ASYM_PAUSE | MAC_SYM_PAUSE |
					    MAC_100HD | MAC_10;
	}
}

void lan937x_setup_rgmii_delay(struct ksz_device *dev, int port)
{
	struct ksz_port *p = &dev->ports[port];

	if (p->rgmii_tx_val) {
		lan937x_set_rgmii_tx_delay(dev, port);
		dev_info(dev->dev, "Applied rgmii tx delay for the port %d\n",
			 port);
	}

	if (p->rgmii_rx_val) {
		lan937x_set_rgmii_rx_delay(dev, port);
		dev_info(dev->dev, "Applied rgmii rx delay for the port %d\n",
			 port);
	}
}

int lan937x_tc_cbs_set_cinc(struct ksz_device *dev, int port, u32 val)
{
	return ksz_pwrite32(dev, port, REG_PORT_MTI_CREDIT_INCREMENT, val);
}

int lan937x_switch_init(struct ksz_device *dev)
{
	dev->port_mask = (1 << dev->info->port_cnt) - 1;

	return 0;
}

int lan937x_setup(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;
	int ret;

	/* The VLAN aware is a global setting. Mixed vlan
	 * filterings are not supported.
	 */
	ds->vlan_filtering_is_global = true;

	/* Enable aggressive back off for half duplex & UNH mode */
	ret = lan937x_cfg(dev, REG_SW_MAC_CTRL_0, (SW_PAUSE_UNH_MODE |
						   SW_NEW_BACKOFF |
						   SW_AGGR_BACKOFF), true);
	if (ret < 0)
		return ret;

	/* If NO_EXC_COLLISION_DROP bit is set, the switch will not drop
	 * packets when 16 or more collisions occur
	 */
	ret = lan937x_cfg(dev, REG_SW_MAC_CTRL_1, NO_EXC_COLLISION_DROP, true);
	if (ret < 0)
		return ret;

	/* enable global MIB counter freeze function */
	ret = lan937x_cfg(dev, REG_SW_MAC_CTRL_6, SW_MIB_COUNTER_FREEZE, true);
	if (ret < 0)
		return ret;

	/* disable CLK125 & CLK25, 1: disable, 0: enable */
	ret = lan937x_cfg(dev, REG_SW_GLOBAL_OUTPUT_CTRL__1,
			  (SW_CLK125_ENB | SW_CLK25_ENB), true);
	if (ret < 0)
		return ret;

	/* Disable global VPHY support. Related to CPU interface only? */
	return ksz_rmw32(dev, REG_SW_CFG_STRAP_OVR, SW_VPHY_DISABLE,
			 SW_VPHY_DISABLE);
}

void lan937x_teardown(struct dsa_switch *ds)
{

}

void lan937x_switch_exit(struct ksz_device *dev)
{
	lan937x_reset_switch(dev);
}

MODULE_AUTHOR("Arun Ramadoss <arun.ramadoss@microchip.com>");
MODULE_DESCRIPTION("Microchip LAN937x Series Switch DSA Driver");
MODULE_LICENSE("GPL");
