/*********************************************************************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2007 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
**********************************************************************/
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/ratelimit.h>
#include <net/dst.h>

#include <asm/octeon/octeon.h>

#include "ethernet-defines.h"
#include "octeon-ethernet.h"
#include "ethernet-util.h"

#include "cvmx-helper.h"

#include <asm/octeon/cvmx-ipd-defs.h>
#include <asm/octeon/cvmx-npi-defs.h>
#include "cvmx-gmxx-defs.h"

DEFINE_SPINLOCK(global_register_lock);

static int number_rgmii_ports;

static void cvm_oct_rgmii_poll(struct net_device *dev)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	unsigned long flags = 0;
	cvmx_helper_link_info_t link_info;
	int use_global_register_lock = (priv->phydev == NULL);

	BUG_ON(in_interrupt());
	if (use_global_register_lock) {
		/*
		 * Take the global register lock since we are going to
		 * touch registers that affect more than one port.
		 */
		spin_lock_irqsave(&global_register_lock, flags);
	} else {
		mutex_lock(&priv->phydev->bus->mdio_lock);
	}

	link_info = cvmx_helper_link_get(priv->port);
	if (link_info.u64 == priv->link_info) {

		/*
		 * If the 10Mbps preamble workaround is supported and we're
		 * at 10Mbps we may need to do some special checking.
		 */
		if (USE_10MBPS_PREAMBLE_WORKAROUND && (link_info.s.speed == 10)) {

			/*
			 * Read the GMXX_RXX_INT_REG[PCTERR] bit and
			 * see if we are getting preamble errors.
			 */
			int interface = INTERFACE(priv->port);
			int index = INDEX(priv->port);
			union cvmx_gmxx_rxx_int_reg gmxx_rxx_int_reg;
			gmxx_rxx_int_reg.u64 =
			    cvmx_read_csr(CVMX_GMXX_RXX_INT_REG
					  (index, interface));
			if (gmxx_rxx_int_reg.s.pcterr) {

				/*
				 * We are getting preamble errors at
				 * 10Mbps.  Most likely the PHY is
				 * giving us packets with mis aligned
				 * preambles. In order to get these
				 * packets we need to disable preamble
				 * checking and do it in software.
				 */
				union cvmx_gmxx_rxx_frm_ctl gmxx_rxx_frm_ctl;
				union cvmx_ipd_sub_port_fcs ipd_sub_port_fcs;

				/* Disable preamble checking */
				gmxx_rxx_frm_ctl.u64 =
				    cvmx_read_csr(CVMX_GMXX_RXX_FRM_CTL
						  (index, interface));
				gmxx_rxx_frm_ctl.s.pre_chk = 0;
				cvmx_write_csr(CVMX_GMXX_RXX_FRM_CTL
					       (index, interface),
					       gmxx_rxx_frm_ctl.u64);

				/* Disable FCS stripping */
				ipd_sub_port_fcs.u64 =
				    cvmx_read_csr(CVMX_IPD_SUB_PORT_FCS);
				ipd_sub_port_fcs.s.port_bit &=
				    0xffffffffull ^ (1ull << priv->port);
				cvmx_write_csr(CVMX_IPD_SUB_PORT_FCS,
					       ipd_sub_port_fcs.u64);

				/* Clear any error bits */
				cvmx_write_csr(CVMX_GMXX_RXX_INT_REG
					       (index, interface),
					       gmxx_rxx_int_reg.u64);
				printk_ratelimited("%s: Using 10Mbps with software "
						   "preamble removal\n",
						   dev->name);
			}
		}

		if (use_global_register_lock)
			spin_unlock_irqrestore(&global_register_lock, flags);
		else
			mutex_unlock(&priv->phydev->bus->mdio_lock);
		return;
	}

	/* If the 10Mbps preamble workaround is allowed we need to on
	   preamble checking, FCS stripping, and clear error bits on
	   every speed change. If errors occur during 10Mbps operation
	   the above code will change this stuff */
	if (USE_10MBPS_PREAMBLE_WORKAROUND) {

		union cvmx_gmxx_rxx_frm_ctl gmxx_rxx_frm_ctl;
		union cvmx_ipd_sub_port_fcs ipd_sub_port_fcs;
		union cvmx_gmxx_rxx_int_reg gmxx_rxx_int_reg;
		int interface = INTERFACE(priv->port);
		int index = INDEX(priv->port);

		/* Enable preamble checking */
		gmxx_rxx_frm_ctl.u64 =
		    cvmx_read_csr(CVMX_GMXX_RXX_FRM_CTL(index, interface));
		gmxx_rxx_frm_ctl.s.pre_chk = 1;
		cvmx_write_csr(CVMX_GMXX_RXX_FRM_CTL(index, interface),
			       gmxx_rxx_frm_ctl.u64);
		/* Enable FCS stripping */
		ipd_sub_port_fcs.u64 = cvmx_read_csr(CVMX_IPD_SUB_PORT_FCS);
		ipd_sub_port_fcs.s.port_bit |= 1ull << priv->port;
		cvmx_write_csr(CVMX_IPD_SUB_PORT_FCS, ipd_sub_port_fcs.u64);
		/* Clear any error bits */
		gmxx_rxx_int_reg.u64 =
		    cvmx_read_csr(CVMX_GMXX_RXX_INT_REG(index, interface));
		cvmx_write_csr(CVMX_GMXX_RXX_INT_REG(index, interface),
			       gmxx_rxx_int_reg.u64);
	}
	if (priv->phydev == NULL) {
		link_info = cvmx_helper_link_autoconf(priv->port);
		priv->link_info = link_info.u64;
	}

	if (use_global_register_lock)
		spin_unlock_irqrestore(&global_register_lock, flags);
	else {
		mutex_unlock(&priv->phydev->bus->mdio_lock);
	}

	if (priv->phydev == NULL) {
		/* Tell core. */
		if (link_info.s.link_up) {
			if (!netif_carrier_ok(dev))
				netif_carrier_on(dev);
			if (priv->queue != -1)
				printk_ratelimited("%s: %u Mbps %s duplex, "
						   "port %2d, queue %2d\n",
						   dev->name, link_info.s.speed,
						   (link_info.s.full_duplex) ?
						   "Full" : "Half",
						   priv->port, priv->queue);
			else
				printk_ratelimited("%s: %u Mbps %s duplex, "
						   "port %2d, POW\n",
						   dev->name, link_info.s.speed,
						   (link_info.s.full_duplex) ?
						   "Full" : "Half",
						   priv->port);
		} else {
			if (netif_carrier_ok(dev))
				netif_carrier_off(dev);
			printk_ratelimited("%s: Link down\n", dev->name);
		}
	}
}

static irqreturn_t cvm_oct_rgmii_rml_interrupt(int cpl, void *dev_id)
{
	union cvmx_npi_rsl_int_blocks rsl_int_blocks;
	int index;
	irqreturn_t return_status = IRQ_NONE;

	rsl_int_blocks.u64 = cvmx_read_csr(CVMX_NPI_RSL_INT_BLOCKS);

	/* Check and see if this interrupt was caused by the GMX0 block */
	if (rsl_int_blocks.s.gmx0) {

		int interface = 0;
		/* Loop through every port of this interface */
		for (index = 0;
		     index < cvmx_helper_ports_on_interface(interface);
		     index++) {

			/* Read the GMX interrupt status bits */
			union cvmx_gmxx_rxx_int_reg gmx_rx_int_reg;
			gmx_rx_int_reg.u64 =
			    cvmx_read_csr(CVMX_GMXX_RXX_INT_REG
					  (index, interface));
			gmx_rx_int_reg.u64 &=
			    cvmx_read_csr(CVMX_GMXX_RXX_INT_EN
					  (index, interface));
			/* Poll the port if inband status changed */
			if (gmx_rx_int_reg.s.phy_dupx
			    || gmx_rx_int_reg.s.phy_link
			    || gmx_rx_int_reg.s.phy_spd) {

				struct net_device *dev =
				    cvm_oct_device[cvmx_helper_get_ipd_port
						   (interface, index)];
				struct octeon_ethernet *priv = netdev_priv(dev);

				if (dev && !atomic_read(&cvm_oct_poll_queue_stopping))
					queue_work(cvm_oct_poll_queue, &priv->port_work);

				gmx_rx_int_reg.u64 = 0;
				gmx_rx_int_reg.s.phy_dupx = 1;
				gmx_rx_int_reg.s.phy_link = 1;
				gmx_rx_int_reg.s.phy_spd = 1;
				cvmx_write_csr(CVMX_GMXX_RXX_INT_REG
					       (index, interface),
					       gmx_rx_int_reg.u64);
				return_status = IRQ_HANDLED;
			}
		}
	}

	/* Check and see if this interrupt was caused by the GMX1 block */
	if (rsl_int_blocks.s.gmx1) {

		int interface = 1;
		/* Loop through every port of this interface */
		for (index = 0;
		     index < cvmx_helper_ports_on_interface(interface);
		     index++) {

			/* Read the GMX interrupt status bits */
			union cvmx_gmxx_rxx_int_reg gmx_rx_int_reg;
			gmx_rx_int_reg.u64 =
			    cvmx_read_csr(CVMX_GMXX_RXX_INT_REG
					  (index, interface));
			gmx_rx_int_reg.u64 &=
			    cvmx_read_csr(CVMX_GMXX_RXX_INT_EN
					  (index, interface));
			/* Poll the port if inband status changed */
			if (gmx_rx_int_reg.s.phy_dupx
			    || gmx_rx_int_reg.s.phy_link
			    || gmx_rx_int_reg.s.phy_spd) {

				struct net_device *dev =
				    cvm_oct_device[cvmx_helper_get_ipd_port
						   (interface, index)];
				struct octeon_ethernet *priv = netdev_priv(dev);

				if (dev && !atomic_read(&cvm_oct_poll_queue_stopping))
					queue_work(cvm_oct_poll_queue, &priv->port_work);

				gmx_rx_int_reg.u64 = 0;
				gmx_rx_int_reg.s.phy_dupx = 1;
				gmx_rx_int_reg.s.phy_link = 1;
				gmx_rx_int_reg.s.phy_spd = 1;
				cvmx_write_csr(CVMX_GMXX_RXX_INT_REG
					       (index, interface),
					       gmx_rx_int_reg.u64);
				return_status = IRQ_HANDLED;
			}
		}
	}
	return return_status;
}

int cvm_oct_rgmii_open(struct net_device *dev)
{
	union cvmx_gmxx_prtx_cfg gmx_cfg;
	struct octeon_ethernet *priv = netdev_priv(dev);
	int interface = INTERFACE(priv->port);
	int index = INDEX(priv->port);
	cvmx_helper_link_info_t link_info;

	gmx_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));
	gmx_cfg.s.en = 1;
	cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), gmx_cfg.u64);

	if (!octeon_is_simulation()) {
		link_info = cvmx_helper_link_get(priv->port);
		if (!link_info.s.link_up)
			netif_carrier_off(dev);
	}

	return 0;
}

int cvm_oct_rgmii_stop(struct net_device *dev)
{
	union cvmx_gmxx_prtx_cfg gmx_cfg;
	struct octeon_ethernet *priv = netdev_priv(dev);
	int interface = INTERFACE(priv->port);
	int index = INDEX(priv->port);

	gmx_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));
	gmx_cfg.s.en = 0;
	cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), gmx_cfg.u64);
	return 0;
}

static void cvm_oct_rgmii_immediate_poll(struct work_struct *work)
{
	struct octeon_ethernet *priv = container_of(work, struct octeon_ethernet, port_work);
	cvm_oct_rgmii_poll(cvm_oct_device[priv->port]);
}

int cvm_oct_rgmii_init(struct net_device *dev)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	int r;

	cvm_oct_common_init(dev);
	dev->netdev_ops->ndo_stop(dev);
	INIT_WORK(&priv->port_work, cvm_oct_rgmii_immediate_poll);
	/*
	 * Due to GMX errata in CN3XXX series chips, it is necessary
	 * to take the link down immediately when the PHY changes
	 * state. In order to do this we call the poll function every
	 * time the RGMII inband status changes.  This may cause
	 * problems if the PHY doesn't implement inband status
	 * properly.
	 */
	if (number_rgmii_ports == 0) {
		r = request_irq(OCTEON_IRQ_RML, cvm_oct_rgmii_rml_interrupt,
				IRQF_SHARED, "RGMII", &number_rgmii_ports);
		if (r != 0)
			return r;
	}
	number_rgmii_ports++;

	/*
	 * Only true RGMII ports need to be polled. In GMII mode, port
	 * 0 is really a RGMII port.
	 */
	if (((priv->imode == CVMX_HELPER_INTERFACE_MODE_GMII)
	     && (priv->port == 0))
	    || (priv->imode == CVMX_HELPER_INTERFACE_MODE_RGMII)) {

		if (!octeon_is_simulation()) {

			union cvmx_gmxx_rxx_int_en gmx_rx_int_en;
			int interface = INTERFACE(priv->port);
			int index = INDEX(priv->port);

			/*
			 * Enable interrupts on inband status changes
			 * for this port.
			 */
			gmx_rx_int_en.u64 =
			    cvmx_read_csr(CVMX_GMXX_RXX_INT_EN
					  (index, interface));
			gmx_rx_int_en.s.phy_dupx = 1;
			gmx_rx_int_en.s.phy_link = 1;
			gmx_rx_int_en.s.phy_spd = 1;
			cvmx_write_csr(CVMX_GMXX_RXX_INT_EN(index, interface),
				       gmx_rx_int_en.u64);
			priv->poll = cvm_oct_rgmii_poll;
		}
	}

	return 0;
}

void cvm_oct_rgmii_uninit(struct net_device *dev)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	cvm_oct_common_uninit(dev);

	/*
	 * Only true RGMII ports need to be polled. In GMII mode, port
	 * 0 is really a RGMII port.
	 */
	if (((priv->imode == CVMX_HELPER_INTERFACE_MODE_GMII)
	     && (priv->port == 0))
	    || (priv->imode == CVMX_HELPER_INTERFACE_MODE_RGMII)) {

		if (!octeon_is_simulation()) {

			union cvmx_gmxx_rxx_int_en gmx_rx_int_en;
			int interface = INTERFACE(priv->port);
			int index = INDEX(priv->port);

			/*
			 * Disable interrupts on inband status changes
			 * for this port.
			 */
			gmx_rx_int_en.u64 =
			    cvmx_read_csr(CVMX_GMXX_RXX_INT_EN
					  (index, interface));
			gmx_rx_int_en.s.phy_dupx = 0;
			gmx_rx_int_en.s.phy_link = 0;
			gmx_rx_int_en.s.phy_spd = 0;
			cvmx_write_csr(CVMX_GMXX_RXX_INT_EN(index, interface),
				       gmx_rx_int_en.u64);
		}
	}

	/* Remove the interrupt handler when the last port is removed. */
	number_rgmii_ports--;
	if (number_rgmii_ports == 0)
		free_irq(OCTEON_IRQ_RML, &number_rgmii_ports);
	cancel_work_sync(&priv->port_work);
}
