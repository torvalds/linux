// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2021 Microchip Technology Inc. and its subsidiaries.
 */

#include "sparx5_main_regs.h"
#include "sparx5_main.h"
#include "sparx5_port.h"

/* The IFH bit position of the first VSTAX bit. This is because the
 * VSTAX bit positions in Data sheet is starting from zero.
 */
#define VSTAX 73

static void ifh_encode_bitfield(void *ifh, u64 value, u32 pos, u32 width)
{
	u8 *ifh_hdr = ifh;
	/* Calculate the Start IFH byte position of this IFH bit position */
	u32 byte = (35 - (pos / 8));
	/* Calculate the Start bit position in the Start IFH byte */
	u32 bit  = (pos % 8);
	u64 encode = GENMASK(bit + width - 1, bit) & (value << bit);

	/* Max width is 5 bytes - 40 bits. In worst case this will
	 * spread over 6 bytes - 48 bits
	 */
	compiletime_assert(width <= 40, "Unsupported width, must be <= 40");

	/* The b0-b7 goes into the start IFH byte */
	if (encode & 0xFF)
		ifh_hdr[byte] |= (u8)((encode & 0xFF));
	/* The b8-b15 goes into the next IFH byte */
	if (encode & 0xFF00)
		ifh_hdr[byte - 1] |= (u8)((encode & 0xFF00) >> 8);
	/* The b16-b23 goes into the next IFH byte */
	if (encode & 0xFF0000)
		ifh_hdr[byte - 2] |= (u8)((encode & 0xFF0000) >> 16);
	/* The b24-b31 goes into the next IFH byte */
	if (encode & 0xFF000000)
		ifh_hdr[byte - 3] |= (u8)((encode & 0xFF000000) >> 24);
	/* The b32-b39 goes into the next IFH byte */
	if (encode & 0xFF00000000)
		ifh_hdr[byte - 4] |= (u8)((encode & 0xFF00000000) >> 32);
	/* The b40-b47 goes into the next IFH byte */
	if (encode & 0xFF0000000000)
		ifh_hdr[byte - 5] |= (u8)((encode & 0xFF0000000000) >> 40);
}

static void sparx5_set_port_ifh(void *ifh_hdr, u16 portno)
{
	/* VSTAX.RSV = 1. MSBit must be 1 */
	ifh_encode_bitfield(ifh_hdr, 1, VSTAX + 79,  1);
	/* VSTAX.INGR_DROP_MODE = Enable. Don't make head-of-line blocking */
	ifh_encode_bitfield(ifh_hdr, 1, VSTAX + 55,  1);
	/* MISC.CPU_MASK/DPORT = Destination port */
	ifh_encode_bitfield(ifh_hdr, portno,   29, 8);
	/* MISC.PIPELINE_PT */
	ifh_encode_bitfield(ifh_hdr, 16,       37, 5);
	/* MISC.PIPELINE_ACT */
	ifh_encode_bitfield(ifh_hdr, 1,        42, 3);
	/* FWD.SRC_PORT = CPU */
	ifh_encode_bitfield(ifh_hdr, SPX5_PORT_CPU, 46, 7);
	/* FWD.SFLOW_ID (disable SFlow sampling) */
	ifh_encode_bitfield(ifh_hdr, 124,      57, 7);
	/* FWD.UPDATE_FCS = Enable. Enforce update of FCS. */
	ifh_encode_bitfield(ifh_hdr, 1,        67, 1);
}

static int sparx5_port_open(struct net_device *ndev)
{
	struct sparx5_port *port = netdev_priv(ndev);
	int err = 0;

	sparx5_port_enable(port, true);
	err = phylink_of_phy_connect(port->phylink, port->of_node, 0);
	if (err) {
		netdev_err(ndev, "Could not attach to PHY\n");
		return err;
	}

	phylink_start(port->phylink);

	if (!ndev->phydev) {
		/* power up serdes */
		port->conf.power_down = false;
		if (port->conf.serdes_reset)
			err = sparx5_serdes_set(port->sparx5, port, &port->conf);
		else
			err = phy_power_on(port->serdes);
		if (err)
			netdev_err(ndev, "%s failed\n", __func__);
	}

	return err;
}

static int sparx5_port_stop(struct net_device *ndev)
{
	struct sparx5_port *port = netdev_priv(ndev);
	int err = 0;

	sparx5_port_enable(port, false);
	phylink_stop(port->phylink);
	phylink_disconnect_phy(port->phylink);

	if (!ndev->phydev) {
		/* power down serdes */
		port->conf.power_down = true;
		if (port->conf.serdes_reset)
			err = sparx5_serdes_set(port->sparx5, port, &port->conf);
		else
			err = phy_power_off(port->serdes);
		if (err)
			netdev_err(ndev, "%s failed\n", __func__);
	}
	return 0;
}

static void sparx5_set_rx_mode(struct net_device *dev)
{
	struct sparx5_port *port = netdev_priv(dev);
	struct sparx5 *sparx5 = port->sparx5;

	if (!test_bit(port->portno, sparx5->bridge_mask))
		__dev_mc_sync(dev, sparx5_mc_sync, sparx5_mc_unsync);
}

static int sparx5_port_get_phys_port_name(struct net_device *dev,
					  char *buf, size_t len)
{
	struct sparx5_port *port = netdev_priv(dev);
	int ret;

	ret = snprintf(buf, len, "p%d", port->portno);
	if (ret >= len)
		return -EINVAL;

	return 0;
}

static int sparx5_set_mac_address(struct net_device *dev, void *p)
{
	struct sparx5_port *port = netdev_priv(dev);
	struct sparx5 *sparx5 = port->sparx5;
	const struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	/* Remove current */
	sparx5_mact_forget(sparx5, dev->dev_addr,  port->pvid);

	/* Add new */
	sparx5_mact_learn(sparx5, PGID_CPU, addr->sa_data, port->pvid);

	/* Record the address */
	ether_addr_copy(dev->dev_addr, addr->sa_data);

	return 0;
}

static int sparx5_get_port_parent_id(struct net_device *dev,
				     struct netdev_phys_item_id *ppid)
{
	struct sparx5_port *sparx5_port = netdev_priv(dev);
	struct sparx5 *sparx5 = sparx5_port->sparx5;

	ppid->id_len = sizeof(sparx5->base_mac);
	memcpy(&ppid->id, &sparx5->base_mac, ppid->id_len);

	return 0;
}

static const struct net_device_ops sparx5_port_netdev_ops = {
	.ndo_open               = sparx5_port_open,
	.ndo_stop               = sparx5_port_stop,
	.ndo_start_xmit         = sparx5_port_xmit_impl,
	.ndo_set_rx_mode        = sparx5_set_rx_mode,
	.ndo_get_phys_port_name = sparx5_port_get_phys_port_name,
	.ndo_set_mac_address    = sparx5_set_mac_address,
	.ndo_validate_addr      = eth_validate_addr,
	.ndo_get_stats64        = sparx5_get_stats64,
	.ndo_get_port_parent_id = sparx5_get_port_parent_id,
};

bool sparx5_netdevice_check(const struct net_device *dev)
{
	return dev && (dev->netdev_ops == &sparx5_port_netdev_ops);
}

struct net_device *sparx5_create_netdev(struct sparx5 *sparx5, u32 portno)
{
	struct sparx5_port *spx5_port;
	struct net_device *ndev;
	u64 val;

	ndev = devm_alloc_etherdev(sparx5->dev, sizeof(struct sparx5_port));
	if (!ndev)
		return ERR_PTR(-ENOMEM);

	SET_NETDEV_DEV(ndev, sparx5->dev);
	spx5_port = netdev_priv(ndev);
	spx5_port->ndev = ndev;
	spx5_port->sparx5 = sparx5;
	spx5_port->portno = portno;
	sparx5_set_port_ifh(spx5_port->ifh, portno);

	ndev->netdev_ops = &sparx5_port_netdev_ops;
	ndev->ethtool_ops = &sparx5_ethtool_ops;

	val = ether_addr_to_u64(sparx5->base_mac) + portno + 1;
	u64_to_ether_addr(val, ndev->dev_addr);

	return ndev;
}

int sparx5_register_netdevs(struct sparx5 *sparx5)
{
	int portno;
	int err;

	for (portno = 0; portno < SPX5_PORTS; portno++)
		if (sparx5->ports[portno]) {
			err = register_netdev(sparx5->ports[portno]->ndev);
			if (err) {
				dev_err(sparx5->dev,
					"port: %02u: netdev registration failed\n",
					portno);
				return err;
			}
			sparx5_port_inj_timer_setup(sparx5->ports[portno]);
		}
	return 0;
}

void sparx5_destroy_netdevs(struct sparx5 *sparx5)
{
	struct sparx5_port *port;
	int portno;

	for (portno = 0; portno < SPX5_PORTS; portno++) {
		port = sparx5->ports[portno];
		if (port && port->phylink) {
			/* Disconnect the phy */
			rtnl_lock();
			sparx5_port_stop(port->ndev);
			phylink_disconnect_phy(port->phylink);
			rtnl_unlock();
			phylink_destroy(port->phylink);
			port->phylink = NULL;
		}
	}
}

void sparx5_unregister_netdevs(struct sparx5 *sparx5)
{
	int portno;

	for (portno = 0; portno < SPX5_PORTS; portno++)
		if (sparx5->ports[portno])
			unregister_netdev(sparx5->ports[portno]->ndev);
}

