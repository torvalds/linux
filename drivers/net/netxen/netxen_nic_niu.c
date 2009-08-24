/*
 * Copyright (C) 2003 - 2009 NetXen, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.
 *
 * Contact Information:
 *    info@netxen.com
 * NetXen Inc,
 * 18922 Forge Drive
 * Cupertino, CA 95014-0701
 *
 */

#include "netxen_nic.h"

/*
 * netxen_niu_gbe_phy_read - read a register from the GbE PHY via
 * mii management interface.
 *
 * Note: The MII management interface goes through port 0.
 *	Individual phys are addressed as follows:
 * @param phy  [15:8]  phy id
 * @param reg  [7:0]   register number
 *
 * @returns  0 on success
 *	  -1 on error
 *
 */
int netxen_niu_gbe_phy_read(struct netxen_adapter *adapter, long reg,
				__u32 * readval)
{
	long timeout = 0;
	long result = 0;
	long restore = 0;
	long phy = adapter->physical_port;
	__u32 address;
	__u32 command;
	__u32 status;
	__u32 mac_cfg0;

	if (netxen_phy_lock(adapter) != 0)
		return -1;

	/*
	 * MII mgmt all goes through port 0 MAC interface,
	 * so it cannot be in reset
	 */

	mac_cfg0 = NXRD32(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(0));
	if (netxen_gb_get_soft_reset(mac_cfg0)) {
		__u32 temp;
		temp = 0;
		netxen_gb_tx_reset_pb(temp);
		netxen_gb_rx_reset_pb(temp);
		netxen_gb_tx_reset_mac(temp);
		netxen_gb_rx_reset_mac(temp);
		if (NXWR32(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(0), temp))
			return -EIO;
		restore = 1;
	}

	address = 0;
	netxen_gb_mii_mgmt_reg_addr(address, reg);
	netxen_gb_mii_mgmt_phy_addr(address, phy);
	if (NXWR32(adapter, NETXEN_NIU_GB_MII_MGMT_ADDR(0), address))
		return -EIO;
	command = 0;		/* turn off any prior activity */
	if (NXWR32(adapter, NETXEN_NIU_GB_MII_MGMT_COMMAND(0), command))
		return -EIO;
	/* send read command */
	netxen_gb_mii_mgmt_set_read_cycle(command);
	if (NXWR32(adapter, NETXEN_NIU_GB_MII_MGMT_COMMAND(0), command))
		return -EIO;

	status = 0;
	do {
		status = NXRD32(adapter, NETXEN_NIU_GB_MII_MGMT_INDICATE(0));
		timeout++;
	} while ((netxen_get_gb_mii_mgmt_busy(status)
		  || netxen_get_gb_mii_mgmt_notvalid(status))
		 && (timeout++ < NETXEN_NIU_PHY_WAITMAX));

	if (timeout < NETXEN_NIU_PHY_WAITMAX) {
		*readval = NXRD32(adapter, NETXEN_NIU_GB_MII_MGMT_STATUS(0));
		result = 0;
	} else
		result = -1;

	if (restore)
		if (NXWR32(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(0), mac_cfg0))
			return -EIO;
	netxen_phy_unlock(adapter);
	return result;
}

/*
 * netxen_niu_gbe_phy_write - write a register to the GbE PHY via
 * mii management interface.
 *
 * Note: The MII management interface goes through port 0.
 *	Individual phys are addressed as follows:
 * @param phy      [15:8]  phy id
 * @param reg      [7:0]   register number
 *
 * @returns  0 on success
 *	  -1 on error
 *
 */
int netxen_niu_gbe_phy_write(struct netxen_adapter *adapter, long reg,
				__u32 val)
{
	long timeout = 0;
	long result = 0;
	long restore = 0;
	long phy = adapter->physical_port;
	__u32 address;
	__u32 command;
	__u32 status;
	__u32 mac_cfg0;

	/*
	 * MII mgmt all goes through port 0 MAC interface, so it
	 * cannot be in reset
	 */

	mac_cfg0 = NXRD32(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(0));
	if (netxen_gb_get_soft_reset(mac_cfg0)) {
		__u32 temp;
		temp = 0;
		netxen_gb_tx_reset_pb(temp);
		netxen_gb_rx_reset_pb(temp);
		netxen_gb_tx_reset_mac(temp);
		netxen_gb_rx_reset_mac(temp);

		if (NXWR32(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(0), temp))
			return -EIO;
		restore = 1;
	}

	command = 0;		/* turn off any prior activity */
	if (NXWR32(adapter, NETXEN_NIU_GB_MII_MGMT_COMMAND(0), command))
		return -EIO;

	address = 0;
	netxen_gb_mii_mgmt_reg_addr(address, reg);
	netxen_gb_mii_mgmt_phy_addr(address, phy);
	if (NXWR32(adapter, NETXEN_NIU_GB_MII_MGMT_ADDR(0), address))
		return -EIO;

	if (NXWR32(adapter, NETXEN_NIU_GB_MII_MGMT_CTRL(0), val))
		return -EIO;

	status = 0;
	do {
		status = NXRD32(adapter, NETXEN_NIU_GB_MII_MGMT_INDICATE(0));
		timeout++;
	} while ((netxen_get_gb_mii_mgmt_busy(status))
		 && (timeout++ < NETXEN_NIU_PHY_WAITMAX));

	if (timeout < NETXEN_NIU_PHY_WAITMAX)
		result = 0;
	else
		result = -EIO;

	/* restore the state of port 0 MAC in case we tampered with it */
	if (restore)
		if (NXWR32(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(0), mac_cfg0))
			return -EIO;

	return result;
}

int netxen_niu_xg_init_port(struct netxen_adapter *adapter, int port)
{
	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		NXWR32(adapter, NETXEN_NIU_XGE_CONFIG_1+(0x10000*port), 0x1447);
		NXWR32(adapter, NETXEN_NIU_XGE_CONFIG_0+(0x10000*port), 0x5);
	}

	return 0;
}

/* Disable an XG interface */
int netxen_niu_disable_xg_port(struct netxen_adapter *adapter)
{
	__u32 mac_cfg;
	u32 port = adapter->physical_port;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		return 0;

	if (port > NETXEN_NIU_MAX_XG_PORTS)
		return -EINVAL;

	mac_cfg = 0;
	if (NXWR32(adapter,
			NETXEN_NIU_XGE_CONFIG_0 + (0x10000 * port), mac_cfg))
		return -EIO;
	return 0;
}

int netxen_niu_xg_set_promiscuous_mode(struct netxen_adapter *adapter,
		u32 mode)
{
	__u32 reg;
	u32 port = adapter->physical_port;

	if (port > NETXEN_NIU_MAX_XG_PORTS)
		return -EINVAL;

	reg = NXRD32(adapter, NETXEN_NIU_XGE_CONFIG_1 + (0x10000 * port));
	if (mode == NETXEN_NIU_PROMISC_MODE)
		reg = (reg | 0x2000UL);
	else
		reg = (reg & ~0x2000UL);

	if (mode == NETXEN_NIU_ALLMULTI_MODE)
		reg = (reg | 0x1000UL);
	else
		reg = (reg & ~0x1000UL);

	NXWR32(adapter, NETXEN_NIU_XGE_CONFIG_1 + (0x10000 * port), reg);

	return 0;
}

int netxen_p2_nic_set_mac_addr(struct netxen_adapter *adapter, u8 *addr)
{
	u32 mac_hi, mac_lo;
	u32 reg_hi, reg_lo;

	u8 phy = adapter->physical_port;
	u8 phy_count = (adapter->ahw.port_type == NETXEN_NIC_XGBE) ?
		NETXEN_NIU_MAX_XG_PORTS : NETXEN_NIU_MAX_GBE_PORTS;

	if (phy >= phy_count)
		return -EINVAL;

	mac_lo = ((u32)addr[0] << 16) | ((u32)addr[1] << 24);
	mac_hi = addr[2] | ((u32)addr[3] << 8) |
		((u32)addr[4] << 16) | ((u32)addr[5] << 24);

	if (adapter->ahw.port_type == NETXEN_NIC_XGBE) {
		reg_lo = NETXEN_NIU_XGE_STATION_ADDR_0_1 + (0x10000 * phy);
		reg_hi = NETXEN_NIU_XGE_STATION_ADDR_0_HI + (0x10000 * phy);
	} else {
		reg_lo = NETXEN_NIU_GB_STATION_ADDR_1(phy);
		reg_hi = NETXEN_NIU_GB_STATION_ADDR_0(phy);
	}

	/* write twice to flush */
	if (NXWR32(adapter, reg_lo, mac_lo) || NXWR32(adapter, reg_hi, mac_hi))
		return -EIO;
	if (NXWR32(adapter, reg_lo, mac_lo) || NXWR32(adapter, reg_hi, mac_hi))
		return -EIO;

	return 0;
}
