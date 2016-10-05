/*
 * This file is based on code from OCTEON SDK by Cavium Networks.
 *
 * Copyright (c) 2003-2007 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/phy.h>
#include <linux/ratelimit.h>
#include <net/dst.h>

#include <asm/octeon/octeon.h>

#include "ethernet-defines.h"
#include "octeon-ethernet.h"
#include "ethernet-util.h"
#include "ethernet-mdio.h"

#include <asm/octeon/cvmx-helper.h>

#include <asm/octeon/cvmx-ipd-defs.h>
#include <asm/octeon/cvmx-npi-defs.h>
#include <asm/octeon/cvmx-gmxx-defs.h>

static DEFINE_SPINLOCK(global_register_lock);

static void cvm_oct_set_hw_preamble(struct octeon_ethernet *priv, bool enable)
{
	union cvmx_gmxx_rxx_frm_ctl gmxx_rxx_frm_ctl;
	union cvmx_ipd_sub_port_fcs ipd_sub_port_fcs;
	union cvmx_gmxx_rxx_int_reg gmxx_rxx_int_reg;
	int interface = INTERFACE(priv->port);
	int index = INDEX(priv->port);

	/* Set preamble checking. */
	gmxx_rxx_frm_ctl.u64 = cvmx_read_csr(CVMX_GMXX_RXX_FRM_CTL(index,
								   interface));
	gmxx_rxx_frm_ctl.s.pre_chk = enable;
	cvmx_write_csr(CVMX_GMXX_RXX_FRM_CTL(index, interface),
		       gmxx_rxx_frm_ctl.u64);

	/* Set FCS stripping. */
	ipd_sub_port_fcs.u64 = cvmx_read_csr(CVMX_IPD_SUB_PORT_FCS);
	if (enable)
		ipd_sub_port_fcs.s.port_bit |= 1ull << priv->port;
	else
		ipd_sub_port_fcs.s.port_bit &=
					0xffffffffull ^ (1ull << priv->port);
	cvmx_write_csr(CVMX_IPD_SUB_PORT_FCS, ipd_sub_port_fcs.u64);

	/* Clear any error bits. */
	gmxx_rxx_int_reg.u64 = cvmx_read_csr(CVMX_GMXX_RXX_INT_REG(index,
								   interface));
	cvmx_write_csr(CVMX_GMXX_RXX_INT_REG(index, interface),
		       gmxx_rxx_int_reg.u64);
}

static void cvm_oct_check_preamble_errors(struct net_device *dev)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	cvmx_helper_link_info_t link_info;
	unsigned long flags;

	link_info.u64 = priv->link_info;

	/*
	 * Take the global register lock since we are going to
	 * touch registers that affect more than one port.
	 */
	spin_lock_irqsave(&global_register_lock, flags);

	if (link_info.s.speed == 10 && priv->last_speed == 10) {
		/*
		 * Read the GMXX_RXX_INT_REG[PCTERR] bit and see if we are
		 * getting preamble errors.
		 */
		int interface = INTERFACE(priv->port);
		int index = INDEX(priv->port);
		union cvmx_gmxx_rxx_int_reg gmxx_rxx_int_reg;

		gmxx_rxx_int_reg.u64 = cvmx_read_csr(CVMX_GMXX_RXX_INT_REG
							(index, interface));
		if (gmxx_rxx_int_reg.s.pcterr) {
			/*
			 * We are getting preamble errors at 10Mbps. Most
			 * likely the PHY is giving us packets with misaligned
			 * preambles. In order to get these packets we need to
			 * disable preamble checking and do it in software.
			 */
			cvm_oct_set_hw_preamble(priv, false);
			printk_ratelimited("%s: Using 10Mbps with software preamble removal\n",
					   dev->name);
		}
	} else {
		/*
		 * Since the 10Mbps preamble workaround is allowed we need to
		 * enable preamble checking, FCS stripping, and clear error
		 * bits on every speed change. If errors occur during 10Mbps
		 * operation the above code will change this stuff
		 */
		if (priv->last_speed != link_info.s.speed)
			cvm_oct_set_hw_preamble(priv, true);
		priv->last_speed = link_info.s.speed;
	}
	spin_unlock_irqrestore(&global_register_lock, flags);
}

static void cvm_oct_rgmii_poll(struct net_device *dev)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	cvmx_helper_link_info_t link_info;
	bool status_change;

	link_info = cvmx_helper_link_get(priv->port);
	if (priv->link_info != link_info.u64 &&
	    cvmx_helper_link_set(priv->port, link_info))
		link_info.u64 = priv->link_info;
	status_change = priv->link_info != link_info.u64;
	priv->link_info = link_info.u64;

	cvm_oct_check_preamble_errors(dev);

	if (likely(!status_change))
		return;

	/* Tell core. */
	if (link_info.s.link_up) {
		if (!netif_carrier_ok(dev))
			netif_carrier_on(dev);
	} else if (netif_carrier_ok(dev)) {
		netif_carrier_off(dev);
	}
	cvm_oct_note_carrier(priv, link_info);
}

int cvm_oct_rgmii_open(struct net_device *dev)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	int ret;

	ret = cvm_oct_common_open(dev, cvm_oct_rgmii_poll);
	if (ret)
		return ret;

	if (dev->phydev) {
		/*
		 * In phydev mode, we need still periodic polling for the
		 * preamble error checking, and we also need to call this
		 * function on every link state change.
		 *
		 * Only true RGMII ports need to be polled. In GMII mode, port
		 * 0 is really a RGMII port.
		 */
		if ((priv->imode == CVMX_HELPER_INTERFACE_MODE_GMII &&
		     priv->port  == 0) ||
		    (priv->imode == CVMX_HELPER_INTERFACE_MODE_RGMII)) {
			priv->poll = cvm_oct_check_preamble_errors;
			cvm_oct_check_preamble_errors(dev);
		}
	}

	return 0;
}
