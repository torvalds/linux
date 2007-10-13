/*
 * Copyright (C) 2003 - 2006 NetXen, Inc.
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
 * NetXen,
 * 3965 Freedom Circle, Fourth floor,
 * Santa Clara, CA 95054
 *
 *
 * Provides access to the Network Interface Unit h/w block.
 *
 */

#include "netxen_nic.h"

#define NETXEN_GB_MAC_SOFT_RESET	0x80000000
#define NETXEN_GB_MAC_RESET_PROT_BLK   0x000F0000
#define NETXEN_GB_MAC_ENABLE_TX_RX     0x00000005
#define NETXEN_GB_MAC_PAUSED_FRMS      0x00000020

static long phy_lock_timeout = 100000000;

static inline int phy_lock(struct netxen_adapter *adapter)
{
	int i;
	int done = 0, timeout = 0;

	while (!done) {
		done =
		    readl(pci_base_offset
			  (adapter, NETXEN_PCIE_REG(PCIE_SEM3_LOCK)));
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

	writel(PHY_LOCK_DRIVER,
	       NETXEN_CRB_NORMALIZE(adapter, NETXEN_PHY_LOCK_ID));
	return 0;
}

static inline int phy_unlock(struct netxen_adapter *adapter)
{
	readl(pci_base_offset(adapter, NETXEN_PCIE_REG(PCIE_SEM3_UNLOCK)));

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
	long phy = physical_port[adapter->portnum];
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

	if (netxen_nic_hw_read_wx(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(0),
				  &mac_cfg0, 4))
		return -EIO;
	if (netxen_gb_get_soft_reset(mac_cfg0)) {
		__u32 temp;
		temp = 0;
		netxen_gb_tx_reset_pb(temp);
		netxen_gb_rx_reset_pb(temp);
		netxen_gb_tx_reset_mac(temp);
		netxen_gb_rx_reset_mac(temp);
		if (netxen_nic_hw_write_wx(adapter,
					   NETXEN_NIU_GB_MAC_CONFIG_0(0),
					   &temp, 4))
			return -EIO;
		restore = 1;
	}

	address = 0;
	netxen_gb_mii_mgmt_reg_addr(address, reg);
	netxen_gb_mii_mgmt_phy_addr(address, phy);
	if (netxen_nic_hw_write_wx(adapter, NETXEN_NIU_GB_MII_MGMT_ADDR(0),
				   &address, 4))
		return -EIO;
	command = 0;		/* turn off any prior activity */
	if (netxen_nic_hw_write_wx(adapter, NETXEN_NIU_GB_MII_MGMT_COMMAND(0),
				   &command, 4))
		return -EIO;
	/* send read command */
	netxen_gb_mii_mgmt_set_read_cycle(command);
	if (netxen_nic_hw_write_wx(adapter, NETXEN_NIU_GB_MII_MGMT_COMMAND(0),
				   &command, 4))
		return -EIO;

	status = 0;
	do {
		if (netxen_nic_hw_read_wx(adapter,
					  NETXEN_NIU_GB_MII_MGMT_INDICATE(0),
					  &status, 4))
			return -EIO;
		timeout++;
	} while ((netxen_get_gb_mii_mgmt_busy(status)
		  || netxen_get_gb_mii_mgmt_notvalid(status))
		 && (timeout++ < NETXEN_NIU_PHY_WAITMAX));

	if (timeout < NETXEN_NIU_PHY_WAITMAX) {
		if (netxen_nic_hw_read_wx(adapter,
					  NETXEN_NIU_GB_MII_MGMT_STATUS(0),
					  readval, 4))
			return -EIO;
		result = 0;
	} else
		result = -1;

	if (restore)
		if (netxen_nic_hw_write_wx(adapter,
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
	long phy = physical_port[adapter->portnum];
	__u32 address;
	__u32 command;
	__u32 status;
	__u32 mac_cfg0;

	/*
	 * MII mgmt all goes through port 0 MAC interface, so it
	 * cannot be in reset
	 */

	if (netxen_nic_hw_read_wx(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(0),
				  &mac_cfg0, 4))
		return -EIO;
	if (netxen_gb_get_soft_reset(mac_cfg0)) {
		__u32 temp;
		temp = 0;
		netxen_gb_tx_reset_pb(temp);
		netxen_gb_rx_reset_pb(temp);
		netxen_gb_tx_reset_mac(temp);
		netxen_gb_rx_reset_mac(temp);

		if (netxen_nic_hw_write_wx(adapter,
					   NETXEN_NIU_GB_MAC_CONFIG_0(0),
					   &temp, 4))
			return -EIO;
		restore = 1;
	}

	command = 0;		/* turn off any prior activity */
	if (netxen_nic_hw_write_wx(adapter, NETXEN_NIU_GB_MII_MGMT_COMMAND(0),
				   &command, 4))
		return -EIO;

	address = 0;
	netxen_gb_mii_mgmt_reg_addr(address, reg);
	netxen_gb_mii_mgmt_phy_addr(address, phy);
	if (netxen_nic_hw_write_wx(adapter, NETXEN_NIU_GB_MII_MGMT_ADDR(0),
				   &address, 4))
		return -EIO;

	if (netxen_nic_hw_write_wx(adapter, NETXEN_NIU_GB_MII_MGMT_CTRL(0),
				   &val, 4))
		return -EIO;

	status = 0;
	do {
		if (netxen_nic_hw_read_wx(adapter,
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
		if (netxen_nic_hw_write_wx(adapter,
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

int netxen_niu_xgbe_clear_phy_interrupts(struct netxen_adapter *adapter)
{
	netxen_crb_writelit_adapter(adapter, NETXEN_NIU_ACTIVE_INT, -1);
	return 0;
}

int netxen_niu_gbe_clear_phy_interrupts(struct netxen_adapter *adapter)
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
void netxen_niu_gbe_set_mii_mode(struct netxen_adapter *adapter,
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
		printk(KERN_ERR PFX "ERROR enabling PHY interrupts\n");
	if (netxen_niu_gbe_clear_phy_interrupts(adapter))
		printk(KERN_ERR PFX "ERROR clearing PHY interrupts\n");
}

/* 
 * netxen_niu_gbe_set_gmii_mode- Set GbE Mode for GbE MAC
 */
void netxen_niu_gbe_set_gmii_mode(struct netxen_adapter *adapter,
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
		printk(KERN_ERR PFX "ERROR enabling PHY interrupts\n");
	if (netxen_niu_gbe_clear_phy_interrupts(adapter))
		printk(KERN_ERR PFX "ERROR clearing PHY interrupts\n");
}

int netxen_niu_gbe_init_port(struct netxen_adapter *adapter, int port)
{
	int result = 0;
	__u32 status;
	if (adapter->disable_phy_interrupts)
		adapter->disable_phy_interrupts(adapter);
	mdelay(2);

	if (0 ==
	    netxen_niu_gbe_phy_read(adapter,
				    NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_STATUS,
				    &status)) {
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
				printk(KERN_ERR PFX
				       "ERROR clearing PHY interrupts\n");
			if (netxen_niu_gbe_enable_phy_interrupts(adapter))
				printk(KERN_ERR PFX
				       "ERROR enabling PHY interrupts\n");
			if (netxen_niu_gbe_clear_phy_interrupts(adapter))
				printk(KERN_ERR PFX
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
	u32 portnum = physical_port[adapter->portnum];

	netxen_crb_writelit_adapter(adapter,
		NETXEN_NIU_XGE_CONFIG_1+(0x10000*portnum), 0x1447);
	netxen_crb_writelit_adapter(adapter,
		NETXEN_NIU_XGE_CONFIG_0+(0x10000*portnum), 0x5);

	return 0;
}

/* 
 * netxen_niu_gbe_handle_phy_interrupt - Handles GbE PHY interrupts
 * @param enable 0 means don't enable the port
 *		 1 means enable (or re-enable) the port
 */
int netxen_niu_gbe_handle_phy_interrupt(struct netxen_adapter *adapter,
					int port, long enable)
{
	int result = 0;
	__u32 int_src;

	printk(KERN_INFO PFX "NETXEN: Handling PHY interrupt on port %d"
	       " (device enable = %d)\n", (int)port, (int)enable);

	/*
	 * The read of the PHY INT status will clear the pending
	 * interrupt status
	 */
	if (netxen_niu_gbe_phy_read(adapter,
				    NETXEN_NIU_GB_MII_MGMT_ADDR_INT_STATUS,
				    &int_src) != 0)
		result = -EINVAL;
	else {
		printk(KERN_INFO PFX "PHY Interrupt source = 0x%x \n", int_src);
		if (netxen_get_phy_int_jabber(int_src))
			printk(KERN_INFO PFX "jabber Interrupt ");
		if (netxen_get_phy_int_polarity_changed(int_src))
			printk(KERN_INFO PFX "polarity changed ");
		if (netxen_get_phy_int_energy_detect(int_src))
			printk(KERN_INFO PFX "energy detect \n");
		if (netxen_get_phy_int_downshift(int_src))
			printk(KERN_INFO PFX "downshift \n");
		if (netxen_get_phy_int_mdi_xover_changed(int_src))
			printk(KERN_INFO PFX "mdi_xover_changed ");
		if (netxen_get_phy_int_fifo_over_underflow(int_src))
			printk(KERN_INFO PFX "fifo_over_underflow ");
		if (netxen_get_phy_int_false_carrier(int_src))
			printk(KERN_INFO PFX "false_carrier ");
		if (netxen_get_phy_int_symbol_error(int_src))
			printk(KERN_INFO PFX "symbol_error ");
		if (netxen_get_phy_int_autoneg_completed(int_src))
			printk(KERN_INFO PFX "autoneg_completed ");
		if (netxen_get_phy_int_page_received(int_src))
			printk(KERN_INFO PFX "page_received ");
		if (netxen_get_phy_int_duplex_changed(int_src))
			printk(KERN_INFO PFX "duplex_changed ");
		if (netxen_get_phy_int_autoneg_error(int_src))
			printk(KERN_INFO PFX "autoneg_error ");
		if ((netxen_get_phy_int_speed_changed(int_src))
		    || (netxen_get_phy_int_link_status_changed(int_src))) {
			__u32 status;

			printk(KERN_INFO PFX
			       "speed_changed or link status changed");
			if (netxen_niu_gbe_phy_read
			    (adapter,
			     NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_STATUS,
			     &status) == 0) {
				if (netxen_get_phy_speed(status) == 2) {
					printk
					    (KERN_INFO PFX "Link speed changed"
					     " to 1000 Mbps\n");
					netxen_niu_gbe_set_gmii_mode(adapter,
								     port,
								     enable);
				} else if (netxen_get_phy_speed(status) == 1) {
					printk
					    (KERN_INFO PFX "Link speed changed"
					     " to 100 Mbps\n");
					netxen_niu_gbe_set_mii_mode(adapter,
								    port,
								    enable);
				} else if (netxen_get_phy_speed(status) == 0) {
					printk
					    (KERN_INFO PFX "Link speed changed"
					     " to 10 Mbps\n");
					netxen_niu_gbe_set_mii_mode(adapter,
								    port,
								    enable);
				} else {
					printk(KERN_ERR PFX "ERROR reading"
					       "PHY status. Illegal speed.\n");
					result = -1;
				}
			} else {
				printk(KERN_ERR PFX
				       "ERROR reading PHY status.\n");
				result = -1;
			}

		}
		printk(KERN_INFO "\n");
	}
	return result;
}

/*
 * Return the current station MAC address.
 * Note that the passed-in value must already be in network byte order.
 */
int netxen_niu_macaddr_get(struct netxen_adapter *adapter,
			   netxen_ethernet_macaddr_t * addr)
{
	u32 stationhigh;
	u32 stationlow;
	int phy = physical_port[adapter->portnum];
	u8 val[8];

	if (addr == NULL)
		return -EINVAL;
	if ((phy < 0) || (phy > 3))
		return -EINVAL;

	if (netxen_nic_hw_read_wx(adapter, NETXEN_NIU_GB_STATION_ADDR_0(phy),
				  &stationhigh, 4))
		return -EIO;
	if (netxen_nic_hw_read_wx(adapter, NETXEN_NIU_GB_STATION_ADDR_1(phy),
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
	int phy = physical_port[adapter->portnum];
	unsigned char mac_addr[6];
	int i;
	DECLARE_MAC_BUF(mac);

	for (i = 0; i < 10; i++) {
		temp[0] = temp[1] = 0;
		memcpy(temp + 2, addr, 2);
		val = le32_to_cpu(*(__le32 *)temp);
		if (netxen_nic_hw_write_wx
		    (adapter, NETXEN_NIU_GB_STATION_ADDR_1(phy), &val, 4))
			return -EIO;

		memcpy(temp, ((u8 *) addr) + 2, sizeof(__le32));
		val = le32_to_cpu(*(__le32 *)temp);
		if (netxen_nic_hw_write_wx
		    (adapter, NETXEN_NIU_GB_STATION_ADDR_0(phy), &val, 4))
			return -2;

		netxen_niu_macaddr_get(adapter, 
				       (netxen_ethernet_macaddr_t *) mac_addr);
		if (memcmp(mac_addr, addr, 6) == 0)
			break;
	}

	if (i == 10) {
		printk(KERN_ERR "%s: cannot set Mac addr for %s\n",
		       netxen_nic_driver_name, adapter->netdev->name);
		printk(KERN_ERR "MAC address set: %s.\n",
		       print_mac(mac, addr));
		printk(KERN_ERR "MAC address get: %s.\n",
		       print_mac(mac, mac_addr));
	}
	return 0;
}

/* Enable a GbE interface */
int netxen_niu_enable_gbe_port(struct netxen_adapter *adapter,
			       int port, netxen_niu_gbe_ifmode_t mode)
{
	__u32 mac_cfg0;
	__u32 mac_cfg1;
	__u32 mii_cfg;

	if ((port < 0) || (port > NETXEN_NIU_MAX_GBE_PORTS))
		return -EINVAL;

	mac_cfg0 = 0;
	netxen_gb_soft_reset(mac_cfg0);
	if (netxen_nic_hw_write_wx(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(port),
				   &mac_cfg0, 4))
		return -EIO;
	mac_cfg0 = 0;
	netxen_gb_enable_tx(mac_cfg0);
	netxen_gb_enable_rx(mac_cfg0);
	netxen_gb_unset_rx_flowctl(mac_cfg0);
	netxen_gb_tx_reset_pb(mac_cfg0);
	netxen_gb_rx_reset_pb(mac_cfg0);
	netxen_gb_tx_reset_mac(mac_cfg0);
	netxen_gb_rx_reset_mac(mac_cfg0);

	if (netxen_nic_hw_write_wx(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(port),
				   &mac_cfg0, 4))
		return -EIO;
	mac_cfg1 = 0;
	netxen_gb_set_preamblelen(mac_cfg1, 0xf);
	netxen_gb_set_duplex(mac_cfg1);
	netxen_gb_set_crc_enable(mac_cfg1);
	netxen_gb_set_padshort(mac_cfg1);
	netxen_gb_set_checklength(mac_cfg1);
	netxen_gb_set_hugeframes(mac_cfg1);

	if (mode == NETXEN_NIU_10_100_MB) {
		netxen_gb_set_intfmode(mac_cfg1, 1);
		if (netxen_nic_hw_write_wx(adapter,
					   NETXEN_NIU_GB_MAC_CONFIG_1(port),
					   &mac_cfg1, 4))
			return -EIO;

		/* set mii mode */
		netxen_crb_writelit_adapter(adapter, NETXEN_NIU_GB0_GMII_MODE +
					    (port << 3), 0);
		netxen_crb_writelit_adapter(adapter, NETXEN_NIU_GB0_MII_MODE +
					    (port << 3), 1);

	} else if (mode == NETXEN_NIU_1000_MB) {
		netxen_gb_set_intfmode(mac_cfg1, 2);
		if (netxen_nic_hw_write_wx(adapter,
					   NETXEN_NIU_GB_MAC_CONFIG_1(port),
					   &mac_cfg1, 4))
			return -EIO;
		/* set gmii mode */
		netxen_crb_writelit_adapter(adapter, NETXEN_NIU_GB0_MII_MODE +
					    (port << 3), 0);
		netxen_crb_writelit_adapter(adapter, NETXEN_NIU_GB0_GMII_MODE +
					    (port << 3), 1);
	}
	mii_cfg = 0;
	netxen_gb_set_mii_mgmt_clockselect(mii_cfg, 7);
	if (netxen_nic_hw_write_wx(adapter, NETXEN_NIU_GB_MII_MGMT_CONFIG(port),
				   &mii_cfg, 4))
		return -EIO;
	mac_cfg0 = 0;
	netxen_gb_enable_tx(mac_cfg0);
	netxen_gb_enable_rx(mac_cfg0);
	netxen_gb_unset_rx_flowctl(mac_cfg0);
	netxen_gb_unset_tx_flowctl(mac_cfg0);

	if (netxen_nic_hw_write_wx(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(port),
				   &mac_cfg0, 4))
		return -EIO;
	return 0;
}

/* Disable a GbE interface */
int netxen_niu_disable_gbe_port(struct netxen_adapter *adapter)
{
	__u32 mac_cfg0;
	u32 port = physical_port[adapter->portnum];

	if (port > NETXEN_NIU_MAX_GBE_PORTS)
		return -EINVAL;
	mac_cfg0 = 0;
	netxen_gb_soft_reset(mac_cfg0);
	if (netxen_nic_hw_write_wx(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(port),
				   &mac_cfg0, 4))
		return -EIO;
	return 0;
}

/* Disable an XG interface */
int netxen_niu_disable_xg_port(struct netxen_adapter *adapter)
{
	__u32 mac_cfg;
	u32 port = physical_port[adapter->portnum];

	if (port != 0)
		return -EINVAL;
	mac_cfg = 0;
	netxen_xg_soft_reset(mac_cfg);
	if (netxen_nic_hw_write_wx(adapter, NETXEN_NIU_XGE_CONFIG_0,
				   &mac_cfg, 4))
		return -EIO;
	return 0;
}

/* Set promiscuous mode for a GbE interface */
int netxen_niu_set_promiscuous_mode(struct netxen_adapter *adapter, 
				    netxen_niu_prom_mode_t mode)
{
	__u32 reg;
	u32 port = physical_port[adapter->portnum];

	if (port > NETXEN_NIU_MAX_GBE_PORTS)
		return -EINVAL;

	/* save previous contents */
	if (netxen_nic_hw_read_wx(adapter, NETXEN_NIU_GB_DROP_WRONGADDR,
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
	if (netxen_nic_hw_write_wx(adapter, NETXEN_NIU_GB_DROP_WRONGADDR,
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
	int phy = physical_port[adapter->portnum];
	u8 temp[4];
	u32 val;

	if ((phy < 0) || (phy > NETXEN_NIU_MAX_XG_PORTS))
		return -EIO;

	temp[0] = temp[1] = 0;
	switch (phy) {
	case 0:
	    memcpy(temp + 2, addr, 2);
	    val = le32_to_cpu(*(__le32 *)temp);
	    if (netxen_nic_hw_write_wx(adapter, NETXEN_NIU_XGE_STATION_ADDR_0_1,
				&val, 4))
		return -EIO;

	    memcpy(&temp, ((u8 *) addr) + 2, sizeof(__le32));
	    val = le32_to_cpu(*(__le32 *)temp);
	    if (netxen_nic_hw_write_wx(adapter, NETXEN_NIU_XGE_STATION_ADDR_0_HI,
				&val, 4))
		return -EIO;
	    break;

	case 1:
	    memcpy(temp + 2, addr, 2);
	    val = le32_to_cpu(*(__le32 *)temp);
	    if (netxen_nic_hw_write_wx(adapter, NETXEN_NIU_XG1_STATION_ADDR_0_1,
				&val, 4))
		return -EIO;

	    memcpy(&temp, ((u8 *) addr) + 2, sizeof(__le32));
	    val = le32_to_cpu(*(__le32 *)temp);
	    if (netxen_nic_hw_write_wx(adapter, NETXEN_NIU_XG1_STATION_ADDR_0_HI,
				&val, 4))
		return -EIO;
	    break;

	default:
	    printk(KERN_ERR "Unknown port %d\n", phy);
	    break;
	}

	return 0;
}

/*
 * Return the current station MAC address.
 * Note that the passed-in value must already be in network byte order.
 */
int netxen_niu_xg_macaddr_get(struct netxen_adapter *adapter,
			      netxen_ethernet_macaddr_t * addr)
{
	int phy = physical_port[adapter->portnum];
	u32 stationhigh;
	u32 stationlow;
	u8 val[8];

	if (addr == NULL)
		return -EINVAL;
	if (phy != 0)
		return -EINVAL;

	if (netxen_nic_hw_read_wx(adapter, NETXEN_NIU_XGE_STATION_ADDR_0_HI,
				  &stationhigh, 4))
		return -EIO;
	if (netxen_nic_hw_read_wx(adapter, NETXEN_NIU_XGE_STATION_ADDR_0_1,
				  &stationlow, 4))
		return -EIO;
	((__le32 *)val)[1] = cpu_to_le32(stationhigh);
	((__le32 *)val)[0] = cpu_to_le32(stationlow);

	memcpy(addr, val + 2, 6);

	return 0;
}

int netxen_niu_xg_set_promiscuous_mode(struct netxen_adapter *adapter,
				       netxen_niu_prom_mode_t mode)
{
	__u32 reg;
	u32 port = physical_port[adapter->portnum];

	if (port > NETXEN_NIU_MAX_XG_PORTS)
		return -EINVAL;

	if (netxen_nic_hw_read_wx(adapter,
		NETXEN_NIU_XGE_CONFIG_1 + (0x10000 * port), &reg, 4))
			return -EIO;
	if (mode == NETXEN_NIU_PROMISC_MODE)
		reg = (reg | 0x2000UL);
	else
		reg = (reg & ~0x2000UL);

	netxen_crb_writelit_adapter(adapter,
		NETXEN_NIU_XGE_CONFIG_1 + (0x10000 * port), reg);

	return 0;
}
