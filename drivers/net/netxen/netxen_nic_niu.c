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

#define NETXEN_GB_MAC_SOFT_RESET	0x80000000
#define NETXEN_GB_MAC_RESET_PROT_BLK   0x000F0000
#define NETXEN_GB_MAC_ENABLE_TX_RX     0x00000005
#define NETXEN_GB_MAC_PAUSED_FRMS      0x00000020

static long phy_lock_timeout = 100000000;

static int phy_lock(struct netxen_adapter *adapter)
{
	int i;
	int done = 0, timeout = 0;

	while (!done) {
		done = netxen_nic_reg_read(adapter,
				NETXEN_PCIE_REG(PCIE_SEM3_LOCK));
		if (done == 1)
			break;
		if (timeout >= phy_lock_timeout) {
			return -1;
		}
		timeout++;
		if (!in_atomic())
			schedule();
		else {
			for (i = 0; i < 20; i++)
				cpu_relax();
		}
	}

	netxen_crb_writelit_adapter(adapter,
			NETXEN_PHY_LOCK_ID, PHY_LOCK_DRIVER);
	return 0;
}

static int phy_unlock(struct netxen_adapter *adapter)
{
	adapter->pci_read_immediate(adapter, NETXEN_PCIE_REG(PCIE_SEM3_UNLOCK));

	return 0;
}

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

	if (phy_lock(adapter) != 0) {
		return -1;
	}

	/*
	 * MII mgmt all goes through port 0 MAC interface,
	 * so it cannot be in reset
	 */

	if (adapter->hw_read_wx(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(0),
				  &mac_cfg0, 4))
		return -EIO;
	if (netxen_gb_get_soft_reset(mac_cfg0)) {
		__u32 temp;
		temp = 0;
		netxen_gb_tx_reset_pb(temp);
		netxen_gb_rx_reset_pb(temp);
		netxen_gb_tx_reset_mac(temp);
		netxen_gb_rx_reset_mac(temp);
		if (adapter->hw_write_wx(adapter,
					   NETXEN_NIU_GB_MAC_CONFIG_0(0),
					   &temp, 4))
			return -EIO;
		restore = 1;
	}

	address = 0;
	netxen_gb_mii_mgmt_reg_addr(address, reg);
	netxen_gb_mii_mgmt_phy_addr(address, phy);
	if (adapter->hw_write_wx(adapter, NETXEN_NIU_GB_MII_MGMT_ADDR(0),
				   &address, 4))
		return -EIO;
	command = 0;		/* turn off any prior activity */
	if (adapter->hw_write_wx(adapter, NETXEN_NIU_GB_MII_MGMT_COMMAND(0),
				   &command, 4))
		return -EIO;
	/* send read command */
	netxen_gb_mii_mgmt_set_read_cycle(command);
	if (adapter->hw_write_wx(adapter, NETXEN_NIU_GB_MII_MGMT_COMMAND(0),
				   &command, 4))
		return -EIO;

	status = 0;
	do {
		if (adapter->hw_read_wx(adapter,
					  NETXEN_NIU_GB_MII_MGMT_INDICATE(0),
					  &status, 4))
			return -EIO;
		timeout++;
	} while ((netxen_get_gb_mii_mgmt_busy(status)
		  || netxen_get_gb_mii_mgmt_notvalid(status))
		 && (timeout++ < NETXEN_NIU_PHY_WAITMAX));

	if (timeout < NETXEN_NIU_PHY_WAITMAX) {
		if (adapter->hw_read_wx(adapter,
					  NETXEN_NIU_GB_MII_MGMT_STATUS(0),
					  readval, 4))
			return -EIO;
		result = 0;
	} else
		result = -1;

	if (restore)
		if (adapter->hw_write_wx(adapter,
					   NETXEN_NIU_GB_MAC_CONFIG_0(0),
					   &mac_cfg0, 4))
			return -EIO;
	phy_unlock(adapter);
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

	if (adapter->hw_read_wx(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(0),
				  &mac_cfg0, 4))
		return -EIO;
	if (netxen_gb_get_soft_reset(mac_cfg0)) {
		__u32 temp;
		temp = 0;
		netxen_gb_tx_reset_pb(temp);
		netxen_gb_rx_reset_pb(temp);
		netxen_gb_tx_reset_mac(temp);
		netxen_gb_rx_reset_mac(temp);

		if (adapter->hw_write_wx(adapter,
					   NETXEN_NIU_GB_MAC_CONFIG_0(0),
					   &temp, 4))
			return -EIO;
		restore = 1;
	}

	command = 0;		/* turn off any prior activity */
	if (adapter->hw_write_wx(adapter, NETXEN_NIU_GB_MII_MGMT_COMMAND(0),
				   &command, 4))
		return -EIO;

	address = 0;
	netxen_gb_mii_mgmt_reg_addr(address, reg);
	netxen_gb_mii_mgmt_phy_addr(address, phy);
	if (adapter->hw_write_wx(adapter, NETXEN_NIU_GB_MII_MGMT_ADDR(0),
				   &address, 4))
		return -EIO;

	if (adapter->hw_write_wx(adapter, NETXEN_NIU_GB_MII_MGMT_CTRL(0),
				   &val, 4))
		return -EIO;

	status = 0;
	do {
		if (adapter->hw_read_wx(adapter,
					  NETXEN_NIU_GB_MII_MGMT_INDICATE(0),
					  &status, 4))
			return -EIO;
		timeout++;
	} while ((netxen_get_gb_mii_mgmt_busy(status))
		 && (timeout++ < NETXEN_NIU_PHY_WAITMAX));

	if (timeout < NETXEN_NIU_PHY_WAITMAX)
		result = 0;
	else
		result = -EIO;

	/* restore the state of port 0 MAC in case we tampered with it */
	if (restore)
		if (adapter->hw_write_wx(adapter,
					   NETXEN_NIU_GB_MAC_CONFIG_0(0),
					   &mac_cfg0, 4))
			return -EIO;

	return result;
}

int netxen_niu_xgbe_enable_phy_interrupts(struct netxen_adapter *adapter)
{
	netxen_crb_writelit_adapter(adapter, NETXEN_NIU_INT_MASK, 0x3f);
	return 0;
}

int netxen_niu_gbe_enable_phy_interrupts(struct netxen_adapter *adapter)
{
	int result = 0;
	__u32 enable = 0;
	netxen_set_phy_int_link_status_changed(enable);
	netxen_set_phy_int_autoneg_completed(enable);
	netxen_set_phy_int_speed_changed(enable);

	if (0 !=
	    netxen_niu_gbe_phy_write(adapter,
				     NETXEN_NIU_GB_MII_MGMT_ADDR_INT_ENABLE,
				     enable))
		result = -EIO;

	return result;
}

int netxen_niu_xgbe_disable_phy_interrupts(struct netxen_adapter *adapter)
{
	netxen_crb_writelit_adapter(adapter, NETXEN_NIU_INT_MASK, 0x7f);
	return 0;
}

int netxen_niu_gbe_disable_phy_interrupts(struct netxen_adapter *adapter)
{
	int result = 0;
	if (0 !=
	    netxen_niu_gbe_phy_write(adapter,
				     NETXEN_NIU_GB_MII_MGMT_ADDR_INT_ENABLE, 0))
		result = -EIO;

	return result;
}

static int netxen_niu_gbe_clear_phy_interrupts(struct netxen_adapter *adapter)
{
	int result = 0;
	if (0 !=
	    netxen_niu_gbe_phy_write(adapter,
				     NETXEN_NIU_GB_MII_MGMT_ADDR_INT_STATUS,
				     -EIO))
		result = -EIO;

	return result;
}

/*
 * netxen_niu_gbe_set_mii_mode- Set 10/100 Mbit Mode for GbE MAC
 *
 */
static void netxen_niu_gbe_set_mii_mode(struct netxen_adapter *adapter,
					int port, long enable)
{
	netxen_crb_writelit_adapter(adapter, NETXEN_NIU_MODE, 0x2);
	netxen_crb_writelit_adapter(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(port),
				    0x80000000);
	netxen_crb_writelit_adapter(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(port),
				    0x0000f0025);
	netxen_crb_writelit_adapter(adapter, NETXEN_NIU_GB_MAC_CONFIG_1(port),
				    0xf1ff);
	netxen_crb_writelit_adapter(adapter,
				    NETXEN_NIU_GB0_GMII_MODE + (port << 3), 0);
	netxen_crb_writelit_adapter(adapter,
				    NETXEN_NIU_GB0_MII_MODE + (port << 3), 1);
	netxen_crb_writelit_adapter(adapter,
				    (NETXEN_NIU_GB0_HALF_DUPLEX + port * 4), 0);
	netxen_crb_writelit_adapter(adapter,
				    NETXEN_NIU_GB_MII_MGMT_CONFIG(port), 0x7);

	if (enable) {
		/*
		 * Do NOT enable flow control until a suitable solution for
		 *  shutting down pause frames is found.
		 */
		netxen_crb_writelit_adapter(adapter,
					    NETXEN_NIU_GB_MAC_CONFIG_0(port),
					    0x5);
	}

	if (netxen_niu_gbe_enable_phy_interrupts(adapter))
		printk(KERN_ERR "ERROR enabling PHY interrupts\n");
	if (netxen_niu_gbe_clear_phy_interrupts(adapter))
		printk(KERN_ERR "ERROR clearing PHY interrupts\n");
}

/*
 * netxen_niu_gbe_set_gmii_mode- Set GbE Mode for GbE MAC
 */
static void netxen_niu_gbe_set_gmii_mode(struct netxen_adapter *adapter,
					 int port, long enable)
{
	netxen_crb_writelit_adapter(adapter, NETXEN_NIU_MODE, 0x2);
	netxen_crb_writelit_adapter(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(port),
				    0x80000000);
	netxen_crb_writelit_adapter(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(port),
				    0x0000f0025);
	netxen_crb_writelit_adapter(adapter, NETXEN_NIU_GB_MAC_CONFIG_1(port),
				    0xf2ff);
	netxen_crb_writelit_adapter(adapter,
				    NETXEN_NIU_GB0_MII_MODE + (port << 3), 0);
	netxen_crb_writelit_adapter(adapter,
				    NETXEN_NIU_GB0_GMII_MODE + (port << 3), 1);
	netxen_crb_writelit_adapter(adapter,
				    (NETXEN_NIU_GB0_HALF_DUPLEX + port * 4), 0);
	netxen_crb_writelit_adapter(adapter,
				    NETXEN_NIU_GB_MII_MGMT_CONFIG(port), 0x7);

	if (enable) {
		/*
		 * Do NOT enable flow control until a suitable solution for
		 *  shutting down pause frames is found.
		 */
		netxen_crb_writelit_adapter(adapter,
					    NETXEN_NIU_GB_MAC_CONFIG_0(port),
					    0x5);
	}

	if (netxen_niu_gbe_enable_phy_interrupts(adapter))
		printk(KERN_ERR "ERROR enabling PHY interrupts\n");
	if (netxen_niu_gbe_clear_phy_interrupts(adapter))
		printk(KERN_ERR "ERROR clearing PHY interrupts\n");
}

int netxen_niu_gbe_init_port(struct netxen_adapter *adapter, int port)
{
	int result = 0;
	__u32 status;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		return 0;

	if (adapter->disable_phy_interrupts)
		adapter->disable_phy_interrupts(adapter);
	mdelay(2);

	if (0 == netxen_niu_gbe_phy_read(adapter,
			NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_STATUS, &status)) {
		if (netxen_get_phy_link(status)) {
			if (netxen_get_phy_speed(status) == 2) {
				netxen_niu_gbe_set_gmii_mode(adapter, port, 1);
			} else if ((netxen_get_phy_speed(status) == 1)
				   || (netxen_get_phy_speed(status) == 0)) {
				netxen_niu_gbe_set_mii_mode(adapter, port, 1);
			} else {
				result = -1;
			}

		} else {
			/*
			 * We don't have link. Cable  must be unconnected.
			 * Enable phy interrupts so we take action when
			 * plugged in.
			 */

			netxen_crb_writelit_adapter(adapter,
						    NETXEN_NIU_GB_MAC_CONFIG_0
						    (port),
						    NETXEN_GB_MAC_SOFT_RESET);
			netxen_crb_writelit_adapter(adapter,
						    NETXEN_NIU_GB_MAC_CONFIG_0
						    (port),
						    NETXEN_GB_MAC_RESET_PROT_BLK
						    | NETXEN_GB_MAC_ENABLE_TX_RX
						    |
						    NETXEN_GB_MAC_PAUSED_FRMS);
			if (netxen_niu_gbe_clear_phy_interrupts(adapter))
				printk(KERN_ERR
				       "ERROR clearing PHY interrupts\n");
			if (netxen_niu_gbe_enable_phy_interrupts(adapter))
				printk(KERN_ERR
				       "ERROR enabling PHY interrupts\n");
			if (netxen_niu_gbe_clear_phy_interrupts(adapter))
				printk(KERN_ERR
				       "ERROR clearing PHY interrupts\n");
			result = -1;
		}
	} else {
		result = -EIO;
	}
	return result;
}

int netxen_niu_xg_init_port(struct netxen_adapter *adapter, int port)
{
	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		netxen_crb_writelit_adapter(adapter,
			NETXEN_NIU_XGE_CONFIG_1+(0x10000*port), 0x1447);
		netxen_crb_writelit_adapter(adapter,
			NETXEN_NIU_XGE_CONFIG_0+(0x10000*port), 0x5);
	}

	return 0;
}

/*
 * Return the current station MAC address.
 * Note that the passed-in value must already be in network byte order.
 */
static int netxen_niu_macaddr_get(struct netxen_adapter *adapter,
				  netxen_ethernet_macaddr_t * addr)
{
	u32 stationhigh;
	u32 stationlow;
	int phy = adapter->physical_port;
	u8 val[8];

	if (addr == NULL)
		return -EINVAL;
	if ((phy < 0) || (phy > 3))
		return -EINVAL;

	if (adapter->hw_read_wx(adapter, NETXEN_NIU_GB_STATION_ADDR_0(phy),
				  &stationhigh, 4))
		return -EIO;
	if (adapter->hw_read_wx(adapter, NETXEN_NIU_GB_STATION_ADDR_1(phy),
				  &stationlow, 4))
		return -EIO;
	((__le32 *)val)[1] = cpu_to_le32(stationhigh);
	((__le32 *)val)[0] = cpu_to_le32(stationlow);

	memcpy(addr, val + 2, 6);

	return 0;
}

/*
 * Set the station MAC address.
 * Note that the passed-in value must already be in network byte order.
 */
int netxen_niu_macaddr_set(struct netxen_adapter *adapter,
			   netxen_ethernet_macaddr_t addr)
{
	u8 temp[4];
	u32 val;
	int phy = adapter->physical_port;
	unsigned char mac_addr[6];
	int i;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		return 0;

	for (i = 0; i < 10; i++) {
		temp[0] = temp[1] = 0;
		memcpy(temp + 2, addr, 2);
		val = le32_to_cpu(*(__le32 *)temp);
		if (adapter->hw_write_wx(adapter,
				NETXEN_NIU_GB_STATION_ADDR_1(phy), &val, 4))
			return -EIO;

		memcpy(temp, ((u8 *) addr) + 2, sizeof(__le32));
		val = le32_to_cpu(*(__le32 *)temp);
		if (adapter->hw_write_wx(adapter,
				NETXEN_NIU_GB_STATION_ADDR_0(phy), &val, 4))
			return -2;

		netxen_niu_macaddr_get(adapter,
				       (netxen_ethernet_macaddr_t *) mac_addr);
		if (memcmp(mac_addr, addr, 6) == 0)
			break;
	}

	if (i == 10) {
		printk(KERN_ERR "%s: cannot set Mac addr for %s\n",
		       netxen_nic_driver_name, adapter->netdev->name);
		printk(KERN_ERR "MAC address set: %pM.\n", addr);
		printk(KERN_ERR "MAC address get: %pM.\n", mac_addr);
	}
	return 0;
}

/* Disable a GbE interface */
int netxen_niu_disable_gbe_port(struct netxen_adapter *adapter)
{
	__u32 mac_cfg0;
	u32 port = adapter->physical_port;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		return 0;

	if (port > NETXEN_NIU_MAX_GBE_PORTS)
		return -EINVAL;
	mac_cfg0 = 0;
	netxen_gb_soft_reset(mac_cfg0);
	if (adapter->hw_write_wx(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(port),
				   &mac_cfg0, 4))
		return -EIO;
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
	if (adapter->hw_write_wx(adapter,
		NETXEN_NIU_XGE_CONFIG_0 + (0x10000 * port), &mac_cfg, 4))
		return -EIO;
	return 0;
}

/* Set promiscuous mode for a GbE interface */
int netxen_niu_set_promiscuous_mode(struct netxen_adapter *adapter,
		u32 mode)
{
	__u32 reg;
	u32 port = adapter->physical_port;

	if (port > NETXEN_NIU_MAX_GBE_PORTS)
		return -EINVAL;

	/* save previous contents */
	if (adapter->hw_read_wx(adapter, NETXEN_NIU_GB_DROP_WRONGADDR,
				  &reg, 4))
		return -EIO;
	if (mode == NETXEN_NIU_PROMISC_MODE) {
		switch (port) {
		case 0:
			netxen_clear_gb_drop_gb0(reg);
			break;
		case 1:
			netxen_clear_gb_drop_gb1(reg);
			break;
		case 2:
			netxen_clear_gb_drop_gb2(reg);
			break;
		case 3:
			netxen_clear_gb_drop_gb3(reg);
			break;
		default:
			return -EIO;
		}
	} else {
		switch (port) {
		case 0:
			netxen_set_gb_drop_gb0(reg);
			break;
		case 1:
			netxen_set_gb_drop_gb1(reg);
			break;
		case 2:
			netxen_set_gb_drop_gb2(reg);
			break;
		case 3:
			netxen_set_gb_drop_gb3(reg);
			break;
		default:
			return -EIO;
		}
	}
	if (adapter->hw_write_wx(adapter, NETXEN_NIU_GB_DROP_WRONGADDR,
				   &reg, 4))
		return -EIO;
	return 0;
}

/*
 * Set the MAC address for an XG port
 * Note that the passed-in value must already be in network byte order.
 */
int netxen_niu_xg_macaddr_set(struct netxen_adapter *adapter,
			      netxen_ethernet_macaddr_t addr)
{
	int phy = adapter->physical_port;
	u8 temp[4];
	u32 val;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		return 0;

	if ((phy < 0) || (phy > NETXEN_NIU_MAX_XG_PORTS))
		return -EIO;

	temp[0] = temp[1] = 0;
	switch (phy) {
	case 0:
	    memcpy(temp + 2, addr, 2);
	    val = le32_to_cpu(*(__le32 *)temp);
	    if (adapter->hw_write_wx(adapter, NETXEN_NIU_XGE_STATION_ADDR_0_1,
				&val, 4))
		return -EIO;

	    memcpy(&temp, ((u8 *) addr) + 2, sizeof(__le32));
	    val = le32_to_cpu(*(__le32 *)temp);
	    if (adapter->hw_write_wx(adapter, NETXEN_NIU_XGE_STATION_ADDR_0_HI,
				&val, 4))
		return -EIO;
	    break;

	case 1:
	    memcpy(temp + 2, addr, 2);
	    val = le32_to_cpu(*(__le32 *)temp);
	    if (adapter->hw_write_wx(adapter, NETXEN_NIU_XG1_STATION_ADDR_0_1,
				&val, 4))
		return -EIO;

	    memcpy(&temp, ((u8 *) addr) + 2, sizeof(__le32));
	    val = le32_to_cpu(*(__le32 *)temp);
	    if (adapter->hw_write_wx(adapter, NETXEN_NIU_XG1_STATION_ADDR_0_HI,
				&val, 4))
		return -EIO;
	    break;

	default:
	    printk(KERN_ERR "Unknown port %d\n", phy);
	    break;
	}

	return 0;
}

int netxen_niu_xg_set_promiscuous_mode(struct netxen_adapter *adapter,
		u32 mode)
{
	__u32 reg;
	u32 port = adapter->physical_port;

	if (port > NETXEN_NIU_MAX_XG_PORTS)
		return -EINVAL;

	if (adapter->hw_read_wx(adapter,
		NETXEN_NIU_XGE_CONFIG_1 + (0x10000 * port), &reg, 4))
			return -EIO;
	if (mode == NETXEN_NIU_PROMISC_MODE)
		reg = (reg | 0x2000UL);
	else
		reg = (reg & ~0x2000UL);

	if (mode == NETXEN_NIU_ALLMULTI_MODE)
		reg = (reg | 0x1000UL);
	else
		reg = (reg & ~0x1000UL);

	netxen_crb_writelit_adapter(adapter,
		NETXEN_NIU_XGE_CONFIG_1 + (0x10000 * port), reg);

	return 0;
}
