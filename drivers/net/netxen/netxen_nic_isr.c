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
 */

#include <linux/netdevice.h>
#include <linux/delay.h>

#include "netxen_nic.h"
#include "netxen_nic_hw.h"
#include "netxen_nic_phan_reg.h"

/*
 * netxen_nic_get_stats - Get System Network Statistics
 * @netdev: network interface device structure
 */
struct net_device_stats *netxen_nic_get_stats(struct net_device *netdev)
{
	struct netxen_port *port = netdev_priv(netdev);
	struct net_device_stats *stats = &port->net_stats;

	memset(stats, 0, sizeof(*stats));

	/* total packets received   */
	stats->rx_packets = port->stats.no_rcv;
	/* total packets transmitted    */
	stats->tx_packets = port->stats.xmitedframes + port->stats.xmitfinished;
	/* total bytes received     */
	stats->rx_bytes = port->stats.rxbytes;
	/* total bytes transmitted  */
	stats->tx_bytes = port->stats.txbytes;
	/* bad packets received     */
	stats->rx_errors = port->stats.rcvdbadskb;
	/* packet transmit problems */
	stats->tx_errors = port->stats.nocmddescriptor;
	/* no space in linux buffers    */
	stats->rx_dropped = port->stats.updropped;
	/* no space available in linux  */
	stats->tx_dropped = port->stats.txdropped;

	return stats;
}

void netxen_indicate_link_status(struct netxen_adapter *adapter, u32 portno,
				 u32 link)
{
	struct net_device *netdev = (adapter->port[portno])->netdev;

	if (link)
		netif_carrier_on(netdev);
	else
		netif_carrier_off(netdev);
}

void netxen_handle_port_int(struct netxen_adapter *adapter, u32 portno,
			    u32 enable)
{
	__le32 int_src;
	struct netxen_port *port;

	/*  This should clear the interrupt source */
	if (adapter->phy_read)
		adapter->phy_read(adapter, portno,
				  NETXEN_NIU_GB_MII_MGMT_ADDR_INT_STATUS,
				  &int_src);
	if (int_src == 0) {
		DPRINTK(INFO, "No phy interrupts for port #%d\n", portno);
		return;
	}
	if (adapter->disable_phy_interrupts)
		adapter->disable_phy_interrupts(adapter, portno);

	port = adapter->port[portno];

	if (netxen_get_phy_int_jabber(int_src))
		DPRINTK(INFO, "Jabber interrupt \n");

	if (netxen_get_phy_int_polarity_changed(int_src))
		DPRINTK(INFO, "POLARITY CHANGED int \n");

	if (netxen_get_phy_int_energy_detect(int_src))
		DPRINTK(INFO, "ENERGY DETECT INT \n");

	if (netxen_get_phy_int_downshift(int_src))
		DPRINTK(INFO, "DOWNSHIFT INT \n");
	/* write it down later.. */
	if ((netxen_get_phy_int_speed_changed(int_src))
	    || (netxen_get_phy_int_link_status_changed(int_src))) {
		__le32 status;

		DPRINTK(INFO, "SPEED CHANGED OR LINK STATUS CHANGED \n");

		if (adapter->phy_read
		    && adapter->phy_read(adapter, portno,
					 NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_STATUS,
					 &status) == 0) {
			if (netxen_get_phy_int_link_status_changed(int_src)) {
				if (netxen_get_phy_link(status)) {
					netxen_niu_gbe_init_port(adapter,
								 portno);
					printk("%s: %s Link UP\n",
					       netxen_nic_driver_name,
					       port->netdev->name);

				} else {
					printk("%s: %s Link DOWN\n",
					       netxen_nic_driver_name,
					       port->netdev->name);
				}
				netxen_indicate_link_status(adapter, portno,
							    netxen_get_phy_link
							    (status));
			}
		}
	}
	if (adapter->enable_phy_interrupts)
		adapter->enable_phy_interrupts(adapter, portno);
}

void netxen_nic_isr_other(struct netxen_adapter *adapter)
{
	u32 portno;
	u32 val, linkup, qg_linksup;

	/* verify the offset */
	val = readl(NETXEN_CRB_NORMALIZE(adapter, CRB_XG_STATE));
	if (val == adapter->ahw.qg_linksup)
		return;

	qg_linksup = adapter->ahw.qg_linksup;
	adapter->ahw.qg_linksup = val;
	DPRINTK(INFO, "link update 0x%08x\n", val);
	for (portno = 0; portno < NETXEN_NIU_MAX_GBE_PORTS; portno++) {
		linkup = val & 1;
		if (linkup != (qg_linksup & 1)) {
			printk(KERN_INFO "%s: PORT %d link %s\n",
			       netxen_nic_driver_name, portno,
			       ((linkup == 0) ? "down" : "up"));
			netxen_indicate_link_status(adapter, portno, linkup);
			if (linkup)
				netxen_nic_set_link_parameters(adapter->
							       port[portno]);

		}
		val = val >> 1;
		qg_linksup = qg_linksup >> 1;
	}

	adapter->stats.otherints++;

}

void netxen_nic_gbe_handle_phy_intr(struct netxen_adapter *adapter)
{
	netxen_nic_isr_other(adapter);
}

void netxen_nic_xgbe_handle_phy_intr(struct netxen_adapter *adapter)
{
	struct net_device *netdev = adapter->port[0]->netdev;
	u32 val;

	/* WINDOW = 1 */
	val = readl(NETXEN_CRB_NORMALIZE(adapter, CRB_XG_STATE));

	if (adapter->ahw.xg_linkup == 1 && val != XG_LINK_UP) {
		printk(KERN_INFO "%s: %s NIC Link is down\n",
		       netxen_nic_driver_name, netdev->name);
		adapter->ahw.xg_linkup = 0;
		/* read twice to clear sticky bits */
		/* WINDOW = 0 */
		netxen_nic_read_w0(adapter, NETXEN_NIU_XG_STATUS, &val);
		netxen_nic_read_w0(adapter, NETXEN_NIU_XG_STATUS, &val);

		if ((val & 0xffb) != 0xffb) {
			printk(KERN_INFO "%s ISR: Sync/Align BAD: 0x%08x\n",
			       netxen_nic_driver_name, val);
		}
	} else if (adapter->ahw.xg_linkup == 0 && val == XG_LINK_UP) {
		printk(KERN_INFO "%s: %s NIC Link is up\n",
		       netxen_nic_driver_name, netdev->name);
		adapter->ahw.xg_linkup = 1;
	}
}
