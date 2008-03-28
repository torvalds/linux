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
	struct netxen_adapter *adapter = netdev_priv(netdev);
	struct net_device_stats *stats = &adapter->net_stats;

	memset(stats, 0, sizeof(*stats));

	/* total packets received   */
	stats->rx_packets = adapter->stats.no_rcv;
	/* total packets transmitted    */
	stats->tx_packets = adapter->stats.xmitedframes +
		adapter->stats.xmitfinished;
	/* total bytes received     */
	stats->rx_bytes = adapter->stats.rxbytes;
	/* total bytes transmitted  */
	stats->tx_bytes = adapter->stats.txbytes;
	/* bad packets received     */
	stats->rx_errors = adapter->stats.rcvdbadskb;
	/* packet transmit problems */
	stats->tx_errors = adapter->stats.nocmddescriptor;
	/* no space in linux buffers    */
	stats->rx_dropped = adapter->stats.rxdropped;
	/* no space available in linux  */
	stats->tx_dropped = adapter->stats.txdropped;

	return stats;
}

static void netxen_indicate_link_status(struct netxen_adapter *adapter,
					u32 link)
{
	struct net_device *netdev = adapter->netdev;

	if (link)
		netif_carrier_on(netdev);
	else
		netif_carrier_off(netdev);
}

#if 0
void netxen_handle_port_int(struct netxen_adapter *adapter, u32 enable)
{
	__u32 int_src;

	/*  This should clear the interrupt source */
	if (adapter->phy_read)
		adapter->phy_read(adapter,
				  NETXEN_NIU_GB_MII_MGMT_ADDR_INT_STATUS,
				  &int_src);
	if (int_src == 0) {
		DPRINTK(INFO, "No phy interrupts for port #%d\n", portno);
		return;
	}
	if (adapter->disable_phy_interrupts)
		adapter->disable_phy_interrupts(adapter);

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
		__u32 status;

		DPRINTK(INFO, "SPEED CHANGED OR LINK STATUS CHANGED \n");

		if (adapter->phy_read
		    && adapter->phy_read(adapter,
					 NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_STATUS,
					 &status) == 0) {
			if (netxen_get_phy_int_link_status_changed(int_src)) {
				if (netxen_get_phy_link(status)) {
					printk(KERN_INFO "%s: %s Link UP\n",
					       netxen_nic_driver_name,
					       adapter->netdev->name);

				} else {
					printk(KERN_INFO "%s: %s Link DOWN\n",
					       netxen_nic_driver_name,
					       adapter->netdev->name);
				}
				netxen_indicate_link_status(adapter,
							    netxen_get_phy_link
							    (status));
			}
		}
	}
	if (adapter->enable_phy_interrupts)
		adapter->enable_phy_interrupts(adapter);
}
#endif  /*  0  */

static void netxen_nic_isr_other(struct netxen_adapter *adapter)
{
	int portno = adapter->portnum;
	u32 val, linkup, qg_linksup;

	/* verify the offset */
	val = readl(NETXEN_CRB_NORMALIZE(adapter, CRB_XG_STATE));
	val = val >> physical_port[adapter->portnum];
	if (val == adapter->ahw.qg_linksup)
		return;

	qg_linksup = adapter->ahw.qg_linksup;
	adapter->ahw.qg_linksup = val;
	DPRINTK(INFO, "link update 0x%08x\n", val);

	linkup = val & 1;

	if (linkup != (qg_linksup & 1)) {
		printk(KERN_INFO "%s: %s PORT %d link %s\n",
		       adapter->netdev->name,
		       netxen_nic_driver_name, portno,
		       ((linkup == 0) ? "down" : "up"));
		netxen_indicate_link_status(adapter, linkup);
		if (linkup)
			netxen_nic_set_link_parameters(adapter);

	}
}

void netxen_nic_gbe_handle_phy_intr(struct netxen_adapter *adapter)
{
	netxen_nic_isr_other(adapter);
}

int netxen_nic_link_ok(struct netxen_adapter *adapter)
{
	switch (adapter->ahw.board_type) {
	case NETXEN_NIC_GBE:
		return ((adapter->ahw.qg_linksup) & 1);

	case NETXEN_NIC_XGBE:
		return ((adapter->ahw.xg_linkup) & 1);

	default:
		printk(KERN_ERR"%s: Function: %s, Unknown board type\n",
			netxen_nic_driver_name, __FUNCTION__);
		break;
	}

	return 0;
}

void netxen_nic_xgbe_handle_phy_intr(struct netxen_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	u32 val;

	/* WINDOW = 1 */
	val = readl(NETXEN_CRB_NORMALIZE(adapter, CRB_XG_STATE));
	val >>= (physical_port[adapter->portnum] * 8);
	val &= 0xff;

	if (adapter->ahw.xg_linkup == 1 && val != XG_LINK_UP) {
		printk(KERN_INFO "%s: %s NIC Link is down\n",
		       netxen_nic_driver_name, netdev->name);
		adapter->ahw.xg_linkup = 0;
		if (netif_running(netdev)) {
			netif_carrier_off(netdev);
			netif_stop_queue(netdev);
		}
	} else if (adapter->ahw.xg_linkup == 0 && val == XG_LINK_UP) {
		printk(KERN_INFO "%s: %s NIC Link is up\n",
		       netxen_nic_driver_name, netdev->name);
		adapter->ahw.xg_linkup = 1;
		netif_carrier_on(netdev);
		netif_wake_queue(netdev);
	}
}
