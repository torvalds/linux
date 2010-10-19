/*
 * drivers/net/gianfar.c
 *
 * Gianfar Ethernet Driver
 * This driver is designed for the non-CPM ethernet controllers
 * on the 85xx and 83xx family of integrated processors
 * Based on 8260_io/fcc_enet.c
 *
 * Author: Andy Fleming
 * Maintainer: Kumar Gala
 *
 * Copyright (c) 2002-2006 Freescale Semiconductor, Inc.
 * Copyright (c) 2007 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 *  Gianfar:  AKA Lambda Draconis, "Dragon"
 *  RA 11 31 24.2
 *  Dec +69 19 52
 *  V 3.84
 *  B-V +1.62
 *
 *  Theory of operation
 *
 *  The driver is initialized through of_device. Configuration information
 *  is therefore conveyed through an OF-style device tree.
 *
 *  The Gianfar Ethernet Controller uses a ring of buffer
 *  descriptors.  The beginning is indicated by a register
 *  pointing to the physical address of the start of the ring.
 *  The end is determined by a "wrap" bit being set in the
 *  last descriptor of the ring.
 *
 *  When a packet is received, the RXF bit in the
 *  IEVENT register is set, triggering an interrupt when the
 *  corresponding bit in the IMASK register is also set (if
 *  interrupt coalescing is active, then the interrupt may not
 *  happen immediately, but will wait until either a set number
 *  of frames or amount of time have passed).  In NAPI, the
 *  interrupt handler will signal there is work to be done, and
 *  exit. This method will start at the last known empty
 *  descriptor, and process every subsequent descriptor until there
 *  are none left with data (NAPI will stop after a set number of
 *  packets to give time to other tasks, but will eventually
 *  process all the packets).  The data arrives inside a
 *  pre-allocated skb, and so after the skb is passed up to the
 *  stack, a new skb must be allocated, and the address field in
 *  the buffer descriptor must be updated to indicate this new
 *  skb.
 *
 *  When the kernel requests that a packet be transmitted, the
 *  driver starts where it left off last time, and points the
 *  descriptor at the buffer which was passed in.  The driver
 *  then informs the DMA engine that there are packets ready to
 *  be transmitted.  Once the controller is finished transmitting
 *  the packet, an interrupt may be triggered (under the same
 *  conditions as for reception, but depending on the TXF bit).
 *  The driver then cleans up the buffer.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_vlan.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/in.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/crc32.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/of.h>

#include "gianfar.h"
#include "fsl_pq_mdio.h"

#define TX_TIMEOUT      (1*HZ)
#undef BRIEF_GFAR_ERRORS
#undef VERBOSE_GFAR_ERRORS

const char gfar_driver_name[] = "Gianfar Ethernet";
const char gfar_driver_version[] = "1.3";

static int gfar_enet_open(struct net_device *dev);
static int gfar_start_xmit(struct sk_buff *skb, struct net_device *dev);
static void gfar_reset_task(struct work_struct *work);
static void gfar_timeout(struct net_device *dev);
static int gfar_close(struct net_device *dev);
struct sk_buff *gfar_new_skb(struct net_device *dev);
static void gfar_new_rxbdp(struct net_device *dev, struct rxbd8 *bdp,
		struct sk_buff *skb);
static int gfar_set_mac_address(struct net_device *dev);
static int gfar_change_mtu(struct net_device *dev, int new_mtu);
static irqreturn_t gfar_error(int irq, void *dev_id);
static irqreturn_t gfar_transmit(int irq, void *dev_id);
static irqreturn_t gfar_interrupt(int irq, void *dev_id);
static void adjust_link(struct net_device *dev);
static void init_registers(struct net_device *dev);
static int init_phy(struct net_device *dev);
static int gfar_probe(struct of_device *ofdev,
		const struct of_device_id *match);
static int gfar_remove(struct of_device *ofdev);
static void free_skb_resources(struct gfar_private *priv);
static void gfar_set_multi(struct net_device *dev);
static void gfar_set_hash_for_addr(struct net_device *dev, u8 *addr);
static void gfar_configure_serdes(struct net_device *dev);
static int gfar_poll(struct napi_struct *napi, int budget);
#ifdef CONFIG_NET_POLL_CONTROLLER
static void gfar_netpoll(struct net_device *dev);
#endif
int gfar_clean_rx_ring(struct net_device *dev, int rx_work_limit);
static int gfar_clean_tx_ring(struct net_device *dev);
static int gfar_process_frame(struct net_device *dev, struct sk_buff *skb,
			      int amount_pull);
static void gfar_vlan_rx_register(struct net_device *netdev,
		                struct vlan_group *grp);
void gfar_halt(struct net_device *dev);
static void gfar_halt_nodisable(struct net_device *dev);
void gfar_start(struct net_device *dev);
static void gfar_clear_exact_match(struct net_device *dev);
static void gfar_set_mac_for_addr(struct net_device *dev, int num, u8 *addr);
static int gfar_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);

MODULE_AUTHOR("Freescale Semiconductor, Inc");
MODULE_DESCRIPTION("Gianfar Ethernet Driver");
MODULE_LICENSE("GPL");

static const struct net_device_ops gfar_netdev_ops = {
	.ndo_open = gfar_enet_open,
	.ndo_start_xmit = gfar_start_xmit,
	.ndo_stop = gfar_close,
	.ndo_change_mtu = gfar_change_mtu,
	.ndo_set_multicast_list = gfar_set_multi,
	.ndo_tx_timeout = gfar_timeout,
	.ndo_do_ioctl = gfar_ioctl,
	.ndo_vlan_rx_register = gfar_vlan_rx_register,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = gfar_netpoll,
#endif
};

/* Returns 1 if incoming frames use an FCB */
static inline int gfar_uses_fcb(struct gfar_private *priv)
{
	return priv->vlgrp || priv->rx_csum_enable;
}

static int gfar_of_init(struct net_device *dev)
{
	const char *model;
	const char *ctype;
	const void *mac_addr;
	u64 addr, size;
	int err = 0;
	struct gfar_private *priv = netdev_priv(dev);
	struct device_node *np = priv->node;
	const u32 *stash;
	const u32 *stash_len;
	const u32 *stash_idx;

	if (!np || !of_device_is_available(np))
		return -ENODEV;

	/* get a pointer to the register memory */
	addr = of_translate_address(np, of_get_address(np, 0, &size, NULL));
	priv->regs = ioremap(addr, size);

	if (priv->regs == NULL)
		return -ENOMEM;

	priv->interruptTransmit = irq_of_parse_and_map(np, 0);

	model = of_get_property(np, "model", NULL);

	/* If we aren't the FEC we have multiple interrupts */
	if (model && strcasecmp(model, "FEC")) {
		priv->interruptReceive = irq_of_parse_and_map(np, 1);

		priv->interruptError = irq_of_parse_and_map(np, 2);

		if (priv->interruptTransmit < 0 ||
				priv->interruptReceive < 0 ||
				priv->interruptError < 0) {
			err = -EINVAL;
			goto err_out;
		}
	}

	stash = of_get_property(np, "bd-stash", NULL);

	if(stash) {
		priv->device_flags |= FSL_GIANFAR_DEV_HAS_BD_STASHING;
		priv->bd_stash_en = 1;
	}

	stash_len = of_get_property(np, "rx-stash-len", NULL);

	if (stash_len)
		priv->rx_stash_size = *stash_len;

	stash_idx = of_get_property(np, "rx-stash-idx", NULL);

	if (stash_idx)
		priv->rx_stash_index = *stash_idx;

	if (stash_len || stash_idx)
		priv->device_flags |= FSL_GIANFAR_DEV_HAS_BUF_STASHING;

	mac_addr = of_get_mac_address(np);
	if (mac_addr)
		memcpy(dev->dev_addr, mac_addr, MAC_ADDR_LEN);

	if (model && !strcasecmp(model, "TSEC"))
		priv->device_flags =
			FSL_GIANFAR_DEV_HAS_GIGABIT |
			FSL_GIANFAR_DEV_HAS_COALESCE |
			FSL_GIANFAR_DEV_HAS_RMON |
			FSL_GIANFAR_DEV_HAS_MULTI_INTR;
	if (model && !strcasecmp(model, "eTSEC"))
		priv->device_flags =
			FSL_GIANFAR_DEV_HAS_GIGABIT |
			FSL_GIANFAR_DEV_HAS_COALESCE |
			FSL_GIANFAR_DEV_HAS_RMON |
			FSL_GIANFAR_DEV_HAS_MULTI_INTR |
			FSL_GIANFAR_DEV_HAS_PADDING |
			FSL_GIANFAR_DEV_HAS_CSUM |
			FSL_GIANFAR_DEV_HAS_VLAN |
			FSL_GIANFAR_DEV_HAS_MAGIC_PACKET |
			FSL_GIANFAR_DEV_HAS_EXTENDED_HASH;

	ctype = of_get_property(np, "phy-connection-type", NULL);

	/* We only care about rgmii-id.  The rest are autodetected */
	if (ctype && !strcmp(ctype, "rgmii-id"))
		priv->interface = PHY_INTERFACE_MODE_RGMII_ID;
	else
		priv->interface = PHY_INTERFACE_MODE_MII;

	if (of_get_property(np, "fsl,magic-packet", NULL))
		priv->device_flags |= FSL_GIANFAR_DEV_HAS_MAGIC_PACKET;

	priv->phy_node = of_parse_phandle(np, "phy-handle", 0);

	/* Find the TBI PHY.  If it's not there, we don't support SGMII */
	priv->tbi_node = of_parse_phandle(np, "tbi-handle", 0);

	return 0;

err_out:
	iounmap(priv->regs);
	return err;
}

/* Ioctl MII Interface */
static int gfar_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct gfar_private *priv = netdev_priv(dev);

	if (!netif_running(dev))
		return -EINVAL;

	if (!priv->phydev)
		return -ENODEV;

	return phy_mii_ioctl(priv->phydev, if_mii(rq), cmd);
}

/* Set up the ethernet device structure, private data,
 * and anything else we need before we start */
static int gfar_probe(struct of_device *ofdev,
		const struct of_device_id *match)
{
	u32 tempval;
	struct net_device *dev = NULL;
	struct gfar_private *priv = NULL;
	int err = 0;
	int len_devname;

	/* Create an ethernet device instance */
	dev = alloc_etherdev(sizeof (*priv));

	if (NULL == dev)
		return -ENOMEM;

	priv = netdev_priv(dev);
	priv->ndev = dev;
	priv->ofdev = ofdev;
	priv->node = ofdev->node;
	SET_NETDEV_DEV(dev, &ofdev->dev);

	err = gfar_of_init(dev);

	if (err)
		goto regs_fail;

	spin_lock_init(&priv->txlock);
	spin_lock_init(&priv->rxlock);
	spin_lock_init(&priv->bflock);
	INIT_WORK(&priv->reset_task, gfar_reset_task);

	dev_set_drvdata(&ofdev->dev, priv);

	/* Stop the DMA engine now, in case it was running before */
	/* (The firmware could have used it, and left it running). */
	gfar_halt(dev);

	/* Reset MAC layer */
	gfar_write(&priv->regs->maccfg1, MACCFG1_SOFT_RESET);

	/* We need to delay at least 3 TX clocks */
	udelay(2);

	tempval = (MACCFG1_TX_FLOW | MACCFG1_RX_FLOW);
	gfar_write(&priv->regs->maccfg1, tempval);

	/* Initialize MACCFG2. */
	gfar_write(&priv->regs->maccfg2, MACCFG2_INIT_SETTINGS);

	/* Initialize ECNTRL */
	gfar_write(&priv->regs->ecntrl, ECNTRL_INIT_SETTINGS);

	/* Set the dev->base_addr to the gfar reg region */
	dev->base_addr = (unsigned long) (priv->regs);

	SET_NETDEV_DEV(dev, &ofdev->dev);

	/* Fill in the dev structure */
	dev->watchdog_timeo = TX_TIMEOUT;
	netif_napi_add(dev, &priv->napi, gfar_poll, GFAR_DEV_WEIGHT);
	dev->mtu = 1500;

	dev->netdev_ops = &gfar_netdev_ops;
	dev->ethtool_ops = &gfar_ethtool_ops;

	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_CSUM) {
		priv->rx_csum_enable = 1;
		dev->features |= NETIF_F_IP_CSUM | NETIF_F_SG | NETIF_F_HIGHDMA;
	} else
		priv->rx_csum_enable = 0;

	priv->vlgrp = NULL;

	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_VLAN)
		dev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;

	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_EXTENDED_HASH) {
		priv->extended_hash = 1;
		priv->hash_width = 9;

		priv->hash_regs[0] = &priv->regs->igaddr0;
		priv->hash_regs[1] = &priv->regs->igaddr1;
		priv->hash_regs[2] = &priv->regs->igaddr2;
		priv->hash_regs[3] = &priv->regs->igaddr3;
		priv->hash_regs[4] = &priv->regs->igaddr4;
		priv->hash_regs[5] = &priv->regs->igaddr5;
		priv->hash_regs[6] = &priv->regs->igaddr6;
		priv->hash_regs[7] = &priv->regs->igaddr7;
		priv->hash_regs[8] = &priv->regs->gaddr0;
		priv->hash_regs[9] = &priv->regs->gaddr1;
		priv->hash_regs[10] = &priv->regs->gaddr2;
		priv->hash_regs[11] = &priv->regs->gaddr3;
		priv->hash_regs[12] = &priv->regs->gaddr4;
		priv->hash_regs[13] = &priv->regs->gaddr5;
		priv->hash_regs[14] = &priv->regs->gaddr6;
		priv->hash_regs[15] = &priv->regs->gaddr7;

	} else {
		priv->extended_hash = 0;
		priv->hash_width = 8;

		priv->hash_regs[0] = &priv->regs->gaddr0;
		priv->hash_regs[1] = &priv->regs->gaddr1;
		priv->hash_regs[2] = &priv->regs->gaddr2;
		priv->hash_regs[3] = &priv->regs->gaddr3;
		priv->hash_regs[4] = &priv->regs->gaddr4;
		priv->hash_regs[5] = &priv->regs->gaddr5;
		priv->hash_regs[6] = &priv->regs->gaddr6;
		priv->hash_regs[7] = &priv->regs->gaddr7;
	}

	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_PADDING)
		priv->padding = DEFAULT_PADDING;
	else
		priv->padding = 0;

	if (dev->features & NETIF_F_IP_CSUM)
		dev->hard_header_len += GMAC_FCB_LEN;

	priv->rx_buffer_size = DEFAULT_RX_BUFFER_SIZE;
	priv->tx_ring_size = DEFAULT_TX_RING_SIZE;
	priv->rx_ring_size = DEFAULT_RX_RING_SIZE;
	priv->num_txbdfree = DEFAULT_TX_RING_SIZE;

	priv->txcoalescing = DEFAULT_TX_COALESCE;
	priv->txic = DEFAULT_TXIC;
	priv->rxcoalescing = DEFAULT_RX_COALESCE;
	priv->rxic = DEFAULT_RXIC;

	/* Enable most messages by default */
	priv->msg_enable = (NETIF_MSG_IFUP << 1 ) - 1;

	/* Carrier starts down, phylib will bring it up */
	netif_carrier_off(dev);

	err = register_netdev(dev);

	if (err) {
		printk(KERN_ERR "%s: Cannot register net device, aborting.\n",
				dev->name);
		goto register_fail;
	}

	device_init_wakeup(&dev->dev,
		priv->device_flags & FSL_GIANFAR_DEV_HAS_MAGIC_PACKET);

	/* fill out IRQ number and name fields */
	len_devname = strlen(dev->name);
	strncpy(&priv->int_name_tx[0], dev->name, len_devname);
	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_MULTI_INTR) {
		strncpy(&priv->int_name_tx[len_devname],
			"_tx", sizeof("_tx") + 1);

		strncpy(&priv->int_name_rx[0], dev->name, len_devname);
		strncpy(&priv->int_name_rx[len_devname],
			"_rx", sizeof("_rx") + 1);

		strncpy(&priv->int_name_er[0], dev->name, len_devname);
		strncpy(&priv->int_name_er[len_devname],
			"_er", sizeof("_er") + 1);
	} else
		priv->int_name_tx[len_devname] = '\0';

	/* Create all the sysfs files */
	gfar_init_sysfs(dev);

	/* Print out the device info */
	printk(KERN_INFO DEVICE_NAME "%pM\n", dev->name, dev->dev_addr);

	/* Even more device info helps when determining which kernel */
	/* provided which set of benchmarks. */
	printk(KERN_INFO "%s: Running with NAPI enabled\n", dev->name);
	printk(KERN_INFO "%s: %d/%d RX/TX BD ring size\n",
	       dev->name, priv->rx_ring_size, priv->tx_ring_size);

	return 0;

register_fail:
	iounmap(priv->regs);
regs_fail:
	if (priv->phy_node)
		of_node_put(priv->phy_node);
	if (priv->tbi_node)
		of_node_put(priv->tbi_node);
	free_netdev(dev);
	return err;
}

static int gfar_remove(struct of_device *ofdev)
{
	struct gfar_private *priv = dev_get_drvdata(&ofdev->dev);

	if (priv->phy_node)
		of_node_put(priv->phy_node);
	if (priv->tbi_node)
		of_node_put(priv->tbi_node);

	dev_set_drvdata(&ofdev->dev, NULL);

	unregister_netdev(priv->ndev);
	iounmap(priv->regs);
	free_netdev(priv->ndev);

	return 0;
}

#ifdef CONFIG_PM
static int gfar_suspend(struct of_device *ofdev, pm_message_t state)
{
	struct gfar_private *priv = dev_get_drvdata(&ofdev->dev);
	struct net_device *dev = priv->ndev;
	unsigned long flags;
	u32 tempval;

	int magic_packet = priv->wol_en &&
		(priv->device_flags & FSL_GIANFAR_DEV_HAS_MAGIC_PACKET);

	netif_device_detach(dev);

	if (netif_running(dev)) {
		spin_lock_irqsave(&priv->txlock, flags);
		spin_lock(&priv->rxlock);

		gfar_halt_nodisable(dev);

		/* Disable Tx, and Rx if wake-on-LAN is disabled. */
		tempval = gfar_read(&priv->regs->maccfg1);

		tempval &= ~MACCFG1_TX_EN;

		if (!magic_packet)
			tempval &= ~MACCFG1_RX_EN;

		gfar_write(&priv->regs->maccfg1, tempval);

		spin_unlock(&priv->rxlock);
		spin_unlock_irqrestore(&priv->txlock, flags);

		napi_disable(&priv->napi);

		if (magic_packet) {
			/* Enable interrupt on Magic Packet */
			gfar_write(&priv->regs->imask, IMASK_MAG);

			/* Enable Magic Packet mode */
			tempval = gfar_read(&priv->regs->maccfg2);
			tempval |= MACCFG2_MPEN;
			gfar_write(&priv->regs->maccfg2, tempval);
		} else {
			phy_stop(priv->phydev);
		}
	}

	return 0;
}

static int gfar_resume(struct of_device *ofdev)
{
	struct gfar_private *priv = dev_get_drvdata(&ofdev->dev);
	struct net_device *dev = priv->ndev;
	unsigned long flags;
	u32 tempval;
	int magic_packet = priv->wol_en &&
		(priv->device_flags & FSL_GIANFAR_DEV_HAS_MAGIC_PACKET);

	if (!netif_running(dev)) {
		netif_device_attach(dev);
		return 0;
	}

	if (!magic_packet && priv->phydev)
		phy_start(priv->phydev);

	/* Disable Magic Packet mode, in case something
	 * else woke us up.
	 */

	spin_lock_irqsave(&priv->txlock, flags);
	spin_lock(&priv->rxlock);

	tempval = gfar_read(&priv->regs->maccfg2);
	tempval &= ~MACCFG2_MPEN;
	gfar_write(&priv->regs->maccfg2, tempval);

	gfar_start(dev);

	spin_unlock(&priv->rxlock);
	spin_unlock_irqrestore(&priv->txlock, flags);

	netif_device_attach(dev);

	napi_enable(&priv->napi);

	return 0;
}
#else
#define gfar_suspend NULL
#define gfar_resume NULL
#endif

/* Reads the controller's registers to determine what interface
 * connects it to the PHY.
 */
static phy_interface_t gfar_get_interface(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	u32 ecntrl = gfar_read(&priv->regs->ecntrl);

	if (ecntrl & ECNTRL_SGMII_MODE)
		return PHY_INTERFACE_MODE_SGMII;

	if (ecntrl & ECNTRL_TBI_MODE) {
		if (ecntrl & ECNTRL_REDUCED_MODE)
			return PHY_INTERFACE_MODE_RTBI;
		else
			return PHY_INTERFACE_MODE_TBI;
	}

	if (ecntrl & ECNTRL_REDUCED_MODE) {
		if (ecntrl & ECNTRL_REDUCED_MII_MODE)
			return PHY_INTERFACE_MODE_RMII;
		else {
			phy_interface_t interface = priv->interface;

			/*
			 * This isn't autodetected right now, so it must
			 * be set by the device tree or platform code.
			 */
			if (interface == PHY_INTERFACE_MODE_RGMII_ID)
				return PHY_INTERFACE_MODE_RGMII_ID;

			return PHY_INTERFACE_MODE_RGMII;
		}
	}

	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_GIGABIT)
		return PHY_INTERFACE_MODE_GMII;

	return PHY_INTERFACE_MODE_MII;
}


/* Initializes driver's PHY state, and attaches to the PHY.
 * Returns 0 on success.
 */
static int init_phy(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	uint gigabit_support =
		priv->device_flags & FSL_GIANFAR_DEV_HAS_GIGABIT ?
		SUPPORTED_1000baseT_Full : 0;
	phy_interface_t interface;

	priv->oldlink = 0;
	priv->oldspeed = 0;
	priv->oldduplex = -1;

	interface = gfar_get_interface(dev);

	priv->phydev = of_phy_connect(dev, priv->phy_node, &adjust_link, 0,
				      interface);
	if (!priv->phydev)
		priv->phydev = of_phy_connect_fixed_link(dev, &adjust_link,
							 interface);
	if (!priv->phydev) {
		dev_err(&dev->dev, "could not attach to PHY\n");
		return -ENODEV;
	}

	if (interface == PHY_INTERFACE_MODE_SGMII)
		gfar_configure_serdes(dev);

	/* Remove any features not supported by the controller */
	priv->phydev->supported &= (GFAR_SUPPORTED | gigabit_support);
	priv->phydev->advertising = priv->phydev->supported;

	return 0;
}

/*
 * Initialize TBI PHY interface for communicating with the
 * SERDES lynx PHY on the chip.  We communicate with this PHY
 * through the MDIO bus on each controller, treating it as a
 * "normal" PHY at the address found in the TBIPA register.  We assume
 * that the TBIPA register is valid.  Either the MDIO bus code will set
 * it to a value that doesn't conflict with other PHYs on the bus, or the
 * value doesn't matter, as there are no other PHYs on the bus.
 */
static void gfar_configure_serdes(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct phy_device *tbiphy;

	if (!priv->tbi_node) {
		dev_warn(&dev->dev, "error: SGMII mode requires that the "
				    "device tree specify a tbi-handle\n");
		return;
	}

	tbiphy = of_phy_find_device(priv->tbi_node);
	if (!tbiphy) {
		dev_err(&dev->dev, "error: Could not get TBI device\n");
		return;
	}

	/*
	 * If the link is already up, we must already be ok, and don't need to
	 * configure and reset the TBI<->SerDes link.  Maybe U-Boot configured
	 * everything for us?  Resetting it takes the link down and requires
	 * several seconds for it to come back.
	 */
	if (phy_read(tbiphy, MII_BMSR) & BMSR_LSTATUS)
		return;

	/* Single clk mode, mii mode off(for serdes communication) */
	phy_write(tbiphy, MII_TBICON, TBICON_CLK_SELECT);

	phy_write(tbiphy, MII_ADVERTISE,
			ADVERTISE_1000XFULL | ADVERTISE_1000XPAUSE |
			ADVERTISE_1000XPSE_ASYM);

	phy_write(tbiphy, MII_BMCR, BMCR_ANENABLE |
			BMCR_ANRESTART | BMCR_FULLDPLX | BMCR_SPEED1000);
}

static void init_registers(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);

	/* Clear IEVENT */
	gfar_write(&priv->regs->ievent, IEVENT_INIT_CLEAR);

	/* Initialize IMASK */
	gfar_write(&priv->regs->imask, IMASK_INIT_CLEAR);

	/* Init hash registers to zero */
	gfar_write(&priv->regs->igaddr0, 0);
	gfar_write(&priv->regs->igaddr1, 0);
	gfar_write(&priv->regs->igaddr2, 0);
	gfar_write(&priv->regs->igaddr3, 0);
	gfar_write(&priv->regs->igaddr4, 0);
	gfar_write(&priv->regs->igaddr5, 0);
	gfar_write(&priv->regs->igaddr6, 0);
	gfar_write(&priv->regs->igaddr7, 0);

	gfar_write(&priv->regs->gaddr0, 0);
	gfar_write(&priv->regs->gaddr1, 0);
	gfar_write(&priv->regs->gaddr2, 0);
	gfar_write(&priv->regs->gaddr3, 0);
	gfar_write(&priv->regs->gaddr4, 0);
	gfar_write(&priv->regs->gaddr5, 0);
	gfar_write(&priv->regs->gaddr6, 0);
	gfar_write(&priv->regs->gaddr7, 0);

	/* Zero out the rmon mib registers if it has them */
	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_RMON) {
		memset_io(&(priv->regs->rmon), 0, sizeof (struct rmon_mib));

		/* Mask off the CAM interrupts */
		gfar_write(&priv->regs->rmon.cam1, 0xffffffff);
		gfar_write(&priv->regs->rmon.cam2, 0xffffffff);
	}

	/* Initialize the max receive buffer length */
	gfar_write(&priv->regs->mrblr, priv->rx_buffer_size);

	/* Initialize the Minimum Frame Length Register */
	gfar_write(&priv->regs->minflr, MINFLR_INIT_SETTINGS);
}


/* Halt the receive and transmit queues */
static void gfar_halt_nodisable(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct gfar __iomem *regs = priv->regs;
	u32 tempval;

	/* Mask all interrupts */
	gfar_write(&regs->imask, IMASK_INIT_CLEAR);

	/* Clear all interrupts */
	gfar_write(&regs->ievent, IEVENT_INIT_CLEAR);

	/* Stop the DMA, and wait for it to stop */
	tempval = gfar_read(&priv->regs->dmactrl);
	if ((tempval & (DMACTRL_GRS | DMACTRL_GTS))
	    != (DMACTRL_GRS | DMACTRL_GTS)) {
		tempval |= (DMACTRL_GRS | DMACTRL_GTS);
		gfar_write(&priv->regs->dmactrl, tempval);

		while (!(gfar_read(&priv->regs->ievent) &
			 (IEVENT_GRSC | IEVENT_GTSC)))
			cpu_relax();
	}
}

/* Halt the receive and transmit queues */
void gfar_halt(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct gfar __iomem *regs = priv->regs;
	u32 tempval;

	gfar_halt_nodisable(dev);

	/* Disable Rx and Tx */
	tempval = gfar_read(&regs->maccfg1);
	tempval &= ~(MACCFG1_RX_EN | MACCFG1_TX_EN);
	gfar_write(&regs->maccfg1, tempval);
}

void stop_gfar(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct gfar __iomem *regs = priv->regs;
	unsigned long flags;

	phy_stop(priv->phydev);

	/* Lock it down */
	spin_lock_irqsave(&priv->txlock, flags);
	spin_lock(&priv->rxlock);

	gfar_halt(dev);

	spin_unlock(&priv->rxlock);
	spin_unlock_irqrestore(&priv->txlock, flags);

	/* Free the IRQs */
	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_MULTI_INTR) {
		free_irq(priv->interruptError, dev);
		free_irq(priv->interruptTransmit, dev);
		free_irq(priv->interruptReceive, dev);
	} else {
		free_irq(priv->interruptTransmit, dev);
	}

	free_skb_resources(priv);

	dma_free_coherent(&priv->ofdev->dev,
			sizeof(struct txbd8)*priv->tx_ring_size
			+ sizeof(struct rxbd8)*priv->rx_ring_size,
			priv->tx_bd_base,
			gfar_read(&regs->tbase0));
}

/* If there are any tx skbs or rx skbs still around, free them.
 * Then free tx_skbuff and rx_skbuff */
static void free_skb_resources(struct gfar_private *priv)
{
	struct rxbd8 *rxbdp;
	struct txbd8 *txbdp;
	int i, j;

	/* Go through all the buffer descriptors and free their data buffers */
	txbdp = priv->tx_bd_base;

	for (i = 0; i < priv->tx_ring_size; i++) {
		if (!priv->tx_skbuff[i])
			continue;

		dma_unmap_single(&priv->ofdev->dev, txbdp->bufPtr,
				txbdp->length, DMA_TO_DEVICE);
		txbdp->lstatus = 0;
		for (j = 0; j < skb_shinfo(priv->tx_skbuff[i])->nr_frags; j++) {
			txbdp++;
			dma_unmap_page(&priv->ofdev->dev, txbdp->bufPtr,
					txbdp->length, DMA_TO_DEVICE);
		}
		txbdp++;
		dev_kfree_skb_any(priv->tx_skbuff[i]);
		priv->tx_skbuff[i] = NULL;
	}

	kfree(priv->tx_skbuff);

	rxbdp = priv->rx_bd_base;

	/* rx_skbuff is not guaranteed to be allocated, so only
	 * free it and its contents if it is allocated */
	if(priv->rx_skbuff != NULL) {
		for (i = 0; i < priv->rx_ring_size; i++) {
			if (priv->rx_skbuff[i]) {
				dma_unmap_single(&priv->ofdev->dev, rxbdp->bufPtr,
						priv->rx_buffer_size,
						DMA_FROM_DEVICE);

				dev_kfree_skb_any(priv->rx_skbuff[i]);
				priv->rx_skbuff[i] = NULL;
			}

			rxbdp->lstatus = 0;
			rxbdp->bufPtr = 0;

			rxbdp++;
		}

		kfree(priv->rx_skbuff);
	}
}

void gfar_start(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct gfar __iomem *regs = priv->regs;
	u32 tempval;

	/* Enable Rx and Tx in MACCFG1 */
	tempval = gfar_read(&regs->maccfg1);
	tempval |= (MACCFG1_RX_EN | MACCFG1_TX_EN);
	gfar_write(&regs->maccfg1, tempval);

	/* Initialize DMACTRL to have WWR and WOP */
	tempval = gfar_read(&priv->regs->dmactrl);
	tempval |= DMACTRL_INIT_SETTINGS;
	gfar_write(&priv->regs->dmactrl, tempval);

	/* Make sure we aren't stopped */
	tempval = gfar_read(&priv->regs->dmactrl);
	tempval &= ~(DMACTRL_GRS | DMACTRL_GTS);
	gfar_write(&priv->regs->dmactrl, tempval);

	/* Clear THLT/RHLT, so that the DMA starts polling now */
	gfar_write(&regs->tstat, TSTAT_CLEAR_THALT);
	gfar_write(&regs->rstat, RSTAT_CLEAR_RHALT);

	/* Unmask the interrupts we look for */
	gfar_write(&regs->imask, IMASK_DEFAULT);

	dev->trans_start = jiffies;
}

/* Bring the controller up and running */
int startup_gfar(struct net_device *dev)
{
	struct txbd8 *txbdp;
	struct rxbd8 *rxbdp;
	dma_addr_t addr = 0;
	unsigned long vaddr;
	int i;
	struct gfar_private *priv = netdev_priv(dev);
	struct gfar __iomem *regs = priv->regs;
	int err = 0;
	u32 rctrl = 0;
	u32 tctrl = 0;
	u32 attrs = 0;

	gfar_write(&regs->imask, IMASK_INIT_CLEAR);

	/* Allocate memory for the buffer descriptors */
	vaddr = (unsigned long) dma_alloc_coherent(&priv->ofdev->dev,
			sizeof (struct txbd8) * priv->tx_ring_size +
			sizeof (struct rxbd8) * priv->rx_ring_size,
			&addr, GFP_KERNEL);

	if (vaddr == 0) {
		if (netif_msg_ifup(priv))
			printk(KERN_ERR "%s: Could not allocate buffer descriptors!\n",
					dev->name);
		return -ENOMEM;
	}

	priv->tx_bd_base = (struct txbd8 *) vaddr;

	/* enet DMA only understands physical addresses */
	gfar_write(&regs->tbase0, addr);

	/* Start the rx descriptor ring where the tx ring leaves off */
	addr = addr + sizeof (struct txbd8) * priv->tx_ring_size;
	vaddr = vaddr + sizeof (struct txbd8) * priv->tx_ring_size;
	priv->rx_bd_base = (struct rxbd8 *) vaddr;
	gfar_write(&regs->rbase0, addr);

	/* Setup the skbuff rings */
	priv->tx_skbuff =
	    (struct sk_buff **) kmalloc(sizeof (struct sk_buff *) *
					priv->tx_ring_size, GFP_KERNEL);

	if (NULL == priv->tx_skbuff) {
		if (netif_msg_ifup(priv))
			printk(KERN_ERR "%s: Could not allocate tx_skbuff\n",
					dev->name);
		err = -ENOMEM;
		goto tx_skb_fail;
	}

	for (i = 0; i < priv->tx_ring_size; i++)
		priv->tx_skbuff[i] = NULL;

	priv->rx_skbuff =
	    (struct sk_buff **) kmalloc(sizeof (struct sk_buff *) *
					priv->rx_ring_size, GFP_KERNEL);

	if (NULL == priv->rx_skbuff) {
		if (netif_msg_ifup(priv))
			printk(KERN_ERR "%s: Could not allocate rx_skbuff\n",
					dev->name);
		err = -ENOMEM;
		goto rx_skb_fail;
	}

	for (i = 0; i < priv->rx_ring_size; i++)
		priv->rx_skbuff[i] = NULL;

	/* Initialize some variables in our dev structure */
	priv->num_txbdfree = priv->tx_ring_size;
	priv->dirty_tx = priv->cur_tx = priv->tx_bd_base;
	priv->cur_rx = priv->rx_bd_base;
	priv->skb_curtx = priv->skb_dirtytx = 0;
	priv->skb_currx = 0;

	/* Initialize Transmit Descriptor Ring */
	txbdp = priv->tx_bd_base;
	for (i = 0; i < priv->tx_ring_size; i++) {
		txbdp->lstatus = 0;
		txbdp->bufPtr = 0;
		txbdp++;
	}

	/* Set the last descriptor in the ring to indicate wrap */
	txbdp--;
	txbdp->status |= TXBD_WRAP;

	rxbdp = priv->rx_bd_base;
	for (i = 0; i < priv->rx_ring_size; i++) {
		struct sk_buff *skb;

		skb = gfar_new_skb(dev);

		if (!skb) {
			printk(KERN_ERR "%s: Can't allocate RX buffers\n",
					dev->name);

			goto err_rxalloc_fail;
		}

		priv->rx_skbuff[i] = skb;

		gfar_new_rxbdp(dev, rxbdp, skb);

		rxbdp++;
	}

	/* Set the last descriptor in the ring to wrap */
	rxbdp--;
	rxbdp->status |= RXBD_WRAP;

	/* If the device has multiple interrupts, register for
	 * them.  Otherwise, only register for the one */
	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_MULTI_INTR) {
		/* Install our interrupt handlers for Error,
		 * Transmit, and Receive */
		if (request_irq(priv->interruptError, gfar_error,
				0, priv->int_name_er, dev) < 0) {
			if (netif_msg_intr(priv))
				printk(KERN_ERR "%s: Can't get IRQ %d\n",
					dev->name, priv->interruptError);

			err = -1;
			goto err_irq_fail;
		}

		if (request_irq(priv->interruptTransmit, gfar_transmit,
				0, priv->int_name_tx, dev) < 0) {
			if (netif_msg_intr(priv))
				printk(KERN_ERR "%s: Can't get IRQ %d\n",
					dev->name, priv->interruptTransmit);

			err = -1;

			goto tx_irq_fail;
		}

		if (request_irq(priv->interruptReceive, gfar_receive,
				0, priv->int_name_rx, dev) < 0) {
			if (netif_msg_intr(priv))
				printk(KERN_ERR "%s: Can't get IRQ %d (receive0)\n",
						dev->name, priv->interruptReceive);

			err = -1;
			goto rx_irq_fail;
		}
	} else {
		if (request_irq(priv->interruptTransmit, gfar_interrupt,
				0, priv->int_name_tx, dev) < 0) {
			if (netif_msg_intr(priv))
				printk(KERN_ERR "%s: Can't get IRQ %d\n",
					dev->name, priv->interruptTransmit);

			err = -1;
			goto err_irq_fail;
		}
	}

	phy_start(priv->phydev);

	/* Configure the coalescing support */
	gfar_write(&regs->txic, 0);
	if (priv->txcoalescing)
		gfar_write(&regs->txic, priv->txic);

	gfar_write(&regs->rxic, 0);
	if (priv->rxcoalescing)
		gfar_write(&regs->rxic, priv->rxic);

	if (priv->rx_csum_enable)
		rctrl |= RCTRL_CHECKSUMMING;

	if (priv->extended_hash) {
		rctrl |= RCTRL_EXTHASH;

		gfar_clear_exact_match(dev);
		rctrl |= RCTRL_EMEN;
	}

	if (priv->padding) {
		rctrl &= ~RCTRL_PAL_MASK;
		rctrl |= RCTRL_PADDING(priv->padding);
	}

	/* keep vlan related bits if it's enabled */
	if (priv->vlgrp) {
		rctrl |= RCTRL_VLEX | RCTRL_PRSDEP_INIT;
		tctrl |= TCTRL_VLINS;
	}

	/* Init rctrl based on our settings */
	gfar_write(&priv->regs->rctrl, rctrl);

	if (dev->features & NETIF_F_IP_CSUM)
		tctrl |= TCTRL_INIT_CSUM;

	gfar_write(&priv->regs->tctrl, tctrl);

	/* Set the extraction length and index */
	attrs = ATTRELI_EL(priv->rx_stash_size) |
		ATTRELI_EI(priv->rx_stash_index);

	gfar_write(&priv->regs->attreli, attrs);

	/* Start with defaults, and add stashing or locking
	 * depending on the approprate variables */
	attrs = ATTR_INIT_SETTINGS;

	if (priv->bd_stash_en)
		attrs |= ATTR_BDSTASH;

	if (priv->rx_stash_size != 0)
		attrs |= ATTR_BUFSTASH;

	gfar_write(&priv->regs->attr, attrs);

	gfar_write(&priv->regs->fifo_tx_thr, priv->fifo_threshold);
	gfar_write(&priv->regs->fifo_tx_starve, priv->fifo_starve);
	gfar_write(&priv->regs->fifo_tx_starve_shutoff, priv->fifo_starve_off);

	/* Start the controller */
	gfar_start(dev);

	return 0;

rx_irq_fail:
	free_irq(priv->interruptTransmit, dev);
tx_irq_fail:
	free_irq(priv->interruptError, dev);
err_irq_fail:
err_rxalloc_fail:
rx_skb_fail:
	free_skb_resources(priv);
tx_skb_fail:
	dma_free_coherent(&priv->ofdev->dev,
			sizeof(struct txbd8)*priv->tx_ring_size
			+ sizeof(struct rxbd8)*priv->rx_ring_size,
			priv->tx_bd_base,
			gfar_read(&regs->tbase0));

	return err;
}

/* Called when something needs to use the ethernet device */
/* Returns 0 for success. */
static int gfar_enet_open(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	int err;

	napi_enable(&priv->napi);

	skb_queue_head_init(&priv->rx_recycle);

	/* Initialize a bunch of registers */
	init_registers(dev);

	gfar_set_mac_address(dev);

	err = init_phy(dev);

	if(err) {
		napi_disable(&priv->napi);
		return err;
	}

	err = startup_gfar(dev);
	if (err) {
		napi_disable(&priv->napi);
		return err;
	}

	netif_start_queue(dev);

	device_set_wakeup_enable(&dev->dev, priv->wol_en);

	return err;
}

static inline struct txfcb *gfar_add_fcb(struct sk_buff *skb)
{
	struct txfcb *fcb = (struct txfcb *)skb_push(skb, GMAC_FCB_LEN);

	memset(fcb, 0, GMAC_FCB_LEN);

	return fcb;
}

static inline void gfar_tx_checksum(struct sk_buff *skb, struct txfcb *fcb)
{
	u8 flags = 0;

	/* If we're here, it's a IP packet with a TCP or UDP
	 * payload.  We set it to checksum, using a pseudo-header
	 * we provide
	 */
	flags = TXFCB_DEFAULT;

	/* Tell the controller what the protocol is */
	/* And provide the already calculated phcs */
	if (ip_hdr(skb)->protocol == IPPROTO_UDP) {
		flags |= TXFCB_UDP;
		fcb->phcs = udp_hdr(skb)->check;
	} else
		fcb->phcs = tcp_hdr(skb)->check;

	/* l3os is the distance between the start of the
	 * frame (skb->data) and the start of the IP hdr.
	 * l4os is the distance between the start of the
	 * l3 hdr and the l4 hdr */
	fcb->l3os = (u16)(skb_network_offset(skb) - GMAC_FCB_LEN);
	fcb->l4os = skb_network_header_len(skb);

	fcb->flags = flags;
}

void inline gfar_tx_vlan(struct sk_buff *skb, struct txfcb *fcb)
{
	fcb->flags |= TXFCB_VLN;
	fcb->vlctl = vlan_tx_tag_get(skb);
}

static inline struct txbd8 *skip_txbd(struct txbd8 *bdp, int stride,
			       struct txbd8 *base, int ring_size)
{
	struct txbd8 *new_bd = bdp + stride;

	return (new_bd >= (base + ring_size)) ? (new_bd - ring_size) : new_bd;
}

static inline struct txbd8 *next_txbd(struct txbd8 *bdp, struct txbd8 *base,
		int ring_size)
{
	return skip_txbd(bdp, 1, base, ring_size);
}

/* This is called by the kernel when a frame is ready for transmission. */
/* It is pointed to by the dev->hard_start_xmit function pointer */
static int gfar_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct txfcb *fcb = NULL;
	struct txbd8 *txbdp, *txbdp_start, *base;
	u32 lstatus;
	int i;
	u32 bufaddr;
	unsigned long flags;
	unsigned int nr_frags, length;

	base = priv->tx_bd_base;

	/* make space for additional header when fcb is needed */
	if (((skb->ip_summed == CHECKSUM_PARTIAL) ||
			(priv->vlgrp && vlan_tx_tag_present(skb))) &&
			(skb_headroom(skb) < GMAC_FCB_LEN)) {
		struct sk_buff *skb_new;

		skb_new = skb_realloc_headroom(skb, GMAC_FCB_LEN);
		if (!skb_new) {
			dev->stats.tx_errors++;
			kfree_skb(skb);
			return NETDEV_TX_OK;
		}
		kfree_skb(skb);
		skb = skb_new;
	}

	/* total number of fragments in the SKB */
	nr_frags = skb_shinfo(skb)->nr_frags;

	spin_lock_irqsave(&priv->txlock, flags);

	/* check if there is space to queue this packet */
	if ((nr_frags+1) > priv->num_txbdfree) {
		/* no space, stop the queue */
		netif_stop_queue(dev);
		dev->stats.tx_fifo_errors++;
		spin_unlock_irqrestore(&priv->txlock, flags);
		return NETDEV_TX_BUSY;
	}

	/* Update transmit stats */
	dev->stats.tx_bytes += skb->len;

	txbdp = txbdp_start = priv->cur_tx;

	if (nr_frags == 0) {
		lstatus = txbdp->lstatus | BD_LFLAG(TXBD_LAST | TXBD_INTERRUPT);
	} else {
		/* Place the fragment addresses and lengths into the TxBDs */
		for (i = 0; i < nr_frags; i++) {
			/* Point at the next BD, wrapping as needed */
			txbdp = next_txbd(txbdp, base, priv->tx_ring_size);

			length = skb_shinfo(skb)->frags[i].size;

			lstatus = txbdp->lstatus | length |
				BD_LFLAG(TXBD_READY);

			/* Handle the last BD specially */
			if (i == nr_frags - 1)
				lstatus |= BD_LFLAG(TXBD_LAST | TXBD_INTERRUPT);

			bufaddr = dma_map_page(&priv->ofdev->dev,
					skb_shinfo(skb)->frags[i].page,
					skb_shinfo(skb)->frags[i].page_offset,
					length,
					DMA_TO_DEVICE);

			/* set the TxBD length and buffer pointer */
			txbdp->bufPtr = bufaddr;
			txbdp->lstatus = lstatus;
		}

		lstatus = txbdp_start->lstatus;
	}

	/* Set up checksumming */
	if (CHECKSUM_PARTIAL == skb->ip_summed) {
		fcb = gfar_add_fcb(skb);
		lstatus |= BD_LFLAG(TXBD_TOE);
		gfar_tx_checksum(skb, fcb);
	}

	if (priv->vlgrp && vlan_tx_tag_present(skb)) {
		if (unlikely(NULL == fcb)) {
			fcb = gfar_add_fcb(skb);
			lstatus |= BD_LFLAG(TXBD_TOE);
		}

		gfar_tx_vlan(skb, fcb);
	}

	/* setup the TxBD length and buffer pointer for the first BD */
	priv->tx_skbuff[priv->skb_curtx] = skb;
	txbdp_start->bufPtr = dma_map_single(&priv->ofdev->dev, skb->data,
			skb_headlen(skb), DMA_TO_DEVICE);

	lstatus |= BD_LFLAG(TXBD_CRC | TXBD_READY) | skb_headlen(skb);

	/*
	 * The powerpc-specific eieio() is used, as wmb() has too strong
	 * semantics (it requires synchronization between cacheable and
	 * uncacheable mappings, which eieio doesn't provide and which we
	 * don't need), thus requiring a more expensive sync instruction.  At
	 * some point, the set of architecture-independent barrier functions
	 * should be expanded to include weaker barriers.
	 */
	eieio();

	txbdp_start->lstatus = lstatus;

	/* Update the current skb pointer to the next entry we will use
	 * (wrapping if necessary) */
	priv->skb_curtx = (priv->skb_curtx + 1) &
		TX_RING_MOD_MASK(priv->tx_ring_size);

	priv->cur_tx = next_txbd(txbdp, base, priv->tx_ring_size);

	/* reduce TxBD free count */
	priv->num_txbdfree -= (nr_frags + 1);

	dev->trans_start = jiffies;

	/* If the next BD still needs to be cleaned up, then the bds
	   are full.  We need to tell the kernel to stop sending us stuff. */
	if (!priv->num_txbdfree) {
		netif_stop_queue(dev);

		dev->stats.tx_fifo_errors++;
	}

	/* Tell the DMA to go go go */
	gfar_write(&priv->regs->tstat, TSTAT_CLEAR_THALT);

	/* Unlock priv */
	spin_unlock_irqrestore(&priv->txlock, flags);

	return NETDEV_TX_OK;
}

/* Stops the kernel queue, and halts the controller */
static int gfar_close(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);

	napi_disable(&priv->napi);

	skb_queue_purge(&priv->rx_recycle);
	cancel_work_sync(&priv->reset_task);
	stop_gfar(dev);

	/* Disconnect from the PHY */
	phy_disconnect(priv->phydev);
	priv->phydev = NULL;

	netif_stop_queue(dev);

	return 0;
}

/* Changes the mac address if the controller is not running. */
static int gfar_set_mac_address(struct net_device *dev)
{
	gfar_set_mac_for_addr(dev, 0, dev->dev_addr);

	return 0;
}


/* Enables and disables VLAN insertion/extraction */
static void gfar_vlan_rx_register(struct net_device *dev,
		struct vlan_group *grp)
{
	struct gfar_private *priv = netdev_priv(dev);
	unsigned long flags;
	u32 tempval;

	spin_lock_irqsave(&priv->rxlock, flags);

	priv->vlgrp = grp;

	if (grp) {
		/* Enable VLAN tag insertion */
		tempval = gfar_read(&priv->regs->tctrl);
		tempval |= TCTRL_VLINS;

		gfar_write(&priv->regs->tctrl, tempval);

		/* Enable VLAN tag extraction */
		tempval = gfar_read(&priv->regs->rctrl);
		tempval |= (RCTRL_VLEX | RCTRL_PRSDEP_INIT);
		gfar_write(&priv->regs->rctrl, tempval);
	} else {
		/* Disable VLAN tag insertion */
		tempval = gfar_read(&priv->regs->tctrl);
		tempval &= ~TCTRL_VLINS;
		gfar_write(&priv->regs->tctrl, tempval);

		/* Disable VLAN tag extraction */
		tempval = gfar_read(&priv->regs->rctrl);
		tempval &= ~RCTRL_VLEX;
		/* If parse is no longer required, then disable parser */
		if (tempval & RCTRL_REQ_PARSER)
			tempval |= RCTRL_PRSDEP_INIT;
		else
			tempval &= ~RCTRL_PRSDEP_INIT;
		gfar_write(&priv->regs->rctrl, tempval);
	}

	gfar_change_mtu(dev, dev->mtu);

	spin_unlock_irqrestore(&priv->rxlock, flags);
}

static int gfar_change_mtu(struct net_device *dev, int new_mtu)
{
	int tempsize, tempval;
	struct gfar_private *priv = netdev_priv(dev);
	int oldsize = priv->rx_buffer_size;
	int frame_size = new_mtu + ETH_HLEN;

	if (priv->vlgrp)
		frame_size += VLAN_HLEN;

	if ((frame_size < 64) || (frame_size > JUMBO_FRAME_SIZE)) {
		if (netif_msg_drv(priv))
			printk(KERN_ERR "%s: Invalid MTU setting\n",
					dev->name);
		return -EINVAL;
	}

	if (gfar_uses_fcb(priv))
		frame_size += GMAC_FCB_LEN;

	frame_size += priv->padding;

	tempsize =
	    (frame_size & ~(INCREMENTAL_BUFFER_SIZE - 1)) +
	    INCREMENTAL_BUFFER_SIZE;

	/* Only stop and start the controller if it isn't already
	 * stopped, and we changed something */
	if ((oldsize != tempsize) && (dev->flags & IFF_UP))
		stop_gfar(dev);

	priv->rx_buffer_size = tempsize;

	dev->mtu = new_mtu;

	gfar_write(&priv->regs->mrblr, priv->rx_buffer_size);
	gfar_write(&priv->regs->maxfrm, priv->rx_buffer_size);

	/* If the mtu is larger than the max size for standard
	 * ethernet frames (ie, a jumbo frame), then set maccfg2
	 * to allow huge frames, and to check the length */
	tempval = gfar_read(&priv->regs->maccfg2);

	if (priv->rx_buffer_size > DEFAULT_RX_BUFFER_SIZE)
		tempval |= (MACCFG2_HUGEFRAME | MACCFG2_LENGTHCHECK);
	else
		tempval &= ~(MACCFG2_HUGEFRAME | MACCFG2_LENGTHCHECK);

	gfar_write(&priv->regs->maccfg2, tempval);

	if ((oldsize != tempsize) && (dev->flags & IFF_UP))
		startup_gfar(dev);

	return 0;
}

/* gfar_reset_task gets scheduled when a packet has not been
 * transmitted after a set amount of time.
 * For now, assume that clearing out all the structures, and
 * starting over will fix the problem.
 */
static void gfar_reset_task(struct work_struct *work)
{
	struct gfar_private *priv = container_of(work, struct gfar_private,
			reset_task);
	struct net_device *dev = priv->ndev;

	if (dev->flags & IFF_UP) {
		netif_stop_queue(dev);
		stop_gfar(dev);
		startup_gfar(dev);
		netif_start_queue(dev);
	}

	netif_tx_schedule_all(dev);
}

static void gfar_timeout(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);

	dev->stats.tx_errors++;
	schedule_work(&priv->reset_task);
}

/* Interrupt Handler for Transmit complete */
static int gfar_clean_tx_ring(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct txbd8 *bdp;
	struct txbd8 *lbdp = NULL;
	struct txbd8 *base = priv->tx_bd_base;
	struct sk_buff *skb;
	int skb_dirtytx;
	int tx_ring_size = priv->tx_ring_size;
	int frags = 0;
	int i;
	int howmany = 0;
	u32 lstatus;

	bdp = priv->dirty_tx;
	skb_dirtytx = priv->skb_dirtytx;

	while ((skb = priv->tx_skbuff[skb_dirtytx])) {
		frags = skb_shinfo(skb)->nr_frags;
		lbdp = skip_txbd(bdp, frags, base, tx_ring_size);

		lstatus = lbdp->lstatus;

		/* Only clean completed frames */
		if ((lstatus & BD_LFLAG(TXBD_READY)) &&
				(lstatus & BD_LENGTH_MASK))
			break;

		dma_unmap_single(&priv->ofdev->dev,
				bdp->bufPtr,
				bdp->length,
				DMA_TO_DEVICE);

		bdp->lstatus &= BD_LFLAG(TXBD_WRAP);
		bdp = next_txbd(bdp, base, tx_ring_size);

		for (i = 0; i < frags; i++) {
			dma_unmap_page(&priv->ofdev->dev,
					bdp->bufPtr,
					bdp->length,
					DMA_TO_DEVICE);
			bdp->lstatus &= BD_LFLAG(TXBD_WRAP);
			bdp = next_txbd(bdp, base, tx_ring_size);
		}

		/*
		 * If there's room in the queue (limit it to rx_buffer_size)
		 * we add this skb back into the pool, if it's the right size
		 */
		if (skb_queue_len(&priv->rx_recycle) < priv->rx_ring_size &&
				skb_recycle_check(skb, priv->rx_buffer_size +
					RXBUF_ALIGNMENT))
			skb_queue_head(&priv->rx_recycle, skb);
		else
			dev_kfree_skb_any(skb);

		priv->tx_skbuff[skb_dirtytx] = NULL;

		skb_dirtytx = (skb_dirtytx + 1) &
			TX_RING_MOD_MASK(tx_ring_size);

		howmany++;
		priv->num_txbdfree += frags + 1;
	}

	/* If we freed a buffer, we can restart transmission, if necessary */
	if (netif_queue_stopped(dev) && priv->num_txbdfree)
		netif_wake_queue(dev);

	/* Update dirty indicators */
	priv->skb_dirtytx = skb_dirtytx;
	priv->dirty_tx = bdp;

	dev->stats.tx_packets += howmany;

	return howmany;
}

static void gfar_schedule_cleanup(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&priv->txlock, flags);
	spin_lock(&priv->rxlock);

	if (napi_schedule_prep(&priv->napi)) {
		gfar_write(&priv->regs->imask, IMASK_RTX_DISABLED);
		__napi_schedule(&priv->napi);
	} else {
		/*
		 * Clear IEVENT, so interrupts aren't called again
		 * because of the packets that have already arrived.
		 */
		gfar_write(&priv->regs->ievent, IEVENT_RTX_MASK);
	}

	spin_unlock(&priv->rxlock);
	spin_unlock_irqrestore(&priv->txlock, flags);
}

/* Interrupt Handler for Transmit complete */
static irqreturn_t gfar_transmit(int irq, void *dev_id)
{
	gfar_schedule_cleanup((struct net_device *)dev_id);
	return IRQ_HANDLED;
}

static void gfar_new_rxbdp(struct net_device *dev, struct rxbd8 *bdp,
		struct sk_buff *skb)
{
	struct gfar_private *priv = netdev_priv(dev);
	u32 lstatus;

	bdp->bufPtr = dma_map_single(&priv->ofdev->dev, skb->data,
			priv->rx_buffer_size, DMA_FROM_DEVICE);

	lstatus = BD_LFLAG(RXBD_EMPTY | RXBD_INTERRUPT);

	if (bdp == priv->rx_bd_base + priv->rx_ring_size - 1)
		lstatus |= BD_LFLAG(RXBD_WRAP);

	eieio();

	bdp->lstatus = lstatus;
}


struct sk_buff * gfar_new_skb(struct net_device *dev)
{
	unsigned int alignamount;
	struct gfar_private *priv = netdev_priv(dev);
	struct sk_buff *skb = NULL;

	skb = skb_dequeue(&priv->rx_recycle);
	if (!skb)
		skb = netdev_alloc_skb(dev,
				priv->rx_buffer_size + RXBUF_ALIGNMENT);

	if (!skb)
		return NULL;

	alignamount = RXBUF_ALIGNMENT -
		(((unsigned long) skb->data) & (RXBUF_ALIGNMENT - 1));

	/* We need the data buffer to be aligned properly.  We will reserve
	 * as many bytes as needed to align the data properly
	 */
	skb_reserve(skb, alignamount);

	return skb;
}

static inline void count_errors(unsigned short status, struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct gfar_extra_stats *estats = &priv->extra_stats;

	/* If the packet was truncated, none of the other errors
	 * matter */
	if (status & RXBD_TRUNCATED) {
		stats->rx_length_errors++;

		estats->rx_trunc++;

		return;
	}
	/* Count the errors, if there were any */
	if (status & (RXBD_LARGE | RXBD_SHORT)) {
		stats->rx_length_errors++;

		if (status & RXBD_LARGE)
			estats->rx_large++;
		else
			estats->rx_short++;
	}
	if (status & RXBD_NONOCTET) {
		stats->rx_frame_errors++;
		estats->rx_nonoctet++;
	}
	if (status & RXBD_CRCERR) {
		estats->rx_crcerr++;
		stats->rx_crc_errors++;
	}
	if (status & RXBD_OVERRUN) {
		estats->rx_overrun++;
		stats->rx_crc_errors++;
	}
}

irqreturn_t gfar_receive(int irq, void *dev_id)
{
	gfar_schedule_cleanup((struct net_device *)dev_id);
	return IRQ_HANDLED;
}

static inline void gfar_rx_checksum(struct sk_buff *skb, struct rxfcb *fcb)
{
	/* If valid headers were found, and valid sums
	 * were verified, then we tell the kernel that no
	 * checksumming is necessary.  Otherwise, it is */
	if ((fcb->flags & RXFCB_CSUM_MASK) == (RXFCB_CIP | RXFCB_CTU))
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	else
		skb->ip_summed = CHECKSUM_NONE;
}


/* gfar_process_frame() -- handle one incoming packet if skb
 * isn't NULL.  */
static int gfar_process_frame(struct net_device *dev, struct sk_buff *skb,
			      int amount_pull)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct rxfcb *fcb = NULL;

	int ret;

	/* fcb is at the beginning if exists */
	fcb = (struct rxfcb *)skb->data;

	/* Remove the FCB from the skb */
	/* Remove the padded bytes, if there are any */
	if (amount_pull)
		skb_pull(skb, amount_pull);

	if (priv->rx_csum_enable)
		gfar_rx_checksum(skb, fcb);

	/* Tell the skb what kind of packet this is */
	skb->protocol = eth_type_trans(skb, dev);

	/* Send the packet up the stack */
	if (unlikely(priv->vlgrp && (fcb->flags & RXFCB_VLN)))
		ret = vlan_hwaccel_receive_skb(skb, priv->vlgrp, fcb->vlctl);
	else
		ret = netif_receive_skb(skb);

	if (NET_RX_DROP == ret)
		priv->extra_stats.kernel_dropped++;

	return 0;
}

/* gfar_clean_rx_ring() -- Processes each frame in the rx ring
 *   until the budget/quota has been reached. Returns the number
 *   of frames handled
 */
int gfar_clean_rx_ring(struct net_device *dev, int rx_work_limit)
{
	struct rxbd8 *bdp, *base;
	struct sk_buff *skb;
	int pkt_len;
	int amount_pull;
	int howmany = 0;
	struct gfar_private *priv = netdev_priv(dev);

	/* Get the first full descriptor */
	bdp = priv->cur_rx;
	base = priv->rx_bd_base;

	amount_pull = (gfar_uses_fcb(priv) ? GMAC_FCB_LEN : 0) +
		priv->padding;

	while (!((bdp->status & RXBD_EMPTY) || (--rx_work_limit < 0))) {
		struct sk_buff *newskb;
		rmb();

		/* Add another skb for the future */
		newskb = gfar_new_skb(dev);

		skb = priv->rx_skbuff[priv->skb_currx];

		dma_unmap_single(&priv->ofdev->dev, bdp->bufPtr,
				priv->rx_buffer_size, DMA_FROM_DEVICE);

		/* We drop the frame if we failed to allocate a new buffer */
		if (unlikely(!newskb || !(bdp->status & RXBD_LAST) ||
				 bdp->status & RXBD_ERR)) {
			count_errors(bdp->status, dev);

			if (unlikely(!newskb))
				newskb = skb;
			else if (skb) {
				/*
				 * We need to reset ->data to what it
				 * was before gfar_new_skb() re-aligned
				 * it to an RXBUF_ALIGNMENT boundary
				 * before we put the skb back on the
				 * recycle list.
				 */
				skb->data = skb->head + NET_SKB_PAD;
				skb_queue_head(&priv->rx_recycle, skb);
			}
		} else {
			/* Increment the number of packets */
			dev->stats.rx_packets++;
			howmany++;

			if (likely(skb)) {
				pkt_len = bdp->length - ETH_FCS_LEN;
				/* Remove the FCS from the packet length */
				skb_put(skb, pkt_len);
				dev->stats.rx_bytes += pkt_len;

				if (in_irq() || irqs_disabled())
					printk("Interrupt problem!\n");
				gfar_process_frame(dev, skb, amount_pull);

			} else {
				if (netif_msg_rx_err(priv))
					printk(KERN_WARNING
					       "%s: Missing skb!\n", dev->name);
				dev->stats.rx_dropped++;
				priv->extra_stats.rx_skbmissing++;
			}

		}

		priv->rx_skbuff[priv->skb_currx] = newskb;

		/* Setup the new bdp */
		gfar_new_rxbdp(dev, bdp, newskb);

		/* Update to the next pointer */
		bdp = next_bd(bdp, base, priv->rx_ring_size);

		/* update to point at the next skb */
		priv->skb_currx =
		    (priv->skb_currx + 1) &
		    RX_RING_MOD_MASK(priv->rx_ring_size);
	}

	/* Update the current rxbd pointer to be the next one */
	priv->cur_rx = bdp;

	return howmany;
}

static int gfar_poll(struct napi_struct *napi, int budget)
{
	struct gfar_private *priv = container_of(napi, struct gfar_private, napi);
	struct net_device *dev = priv->ndev;
	int tx_cleaned = 0;
	int rx_cleaned = 0;
	unsigned long flags;

	/* Clear IEVENT, so interrupts aren't called again
	 * because of the packets that have already arrived */
	gfar_write(&priv->regs->ievent, IEVENT_RTX_MASK);

	/* If we fail to get the lock, don't bother with the TX BDs */
	if (spin_trylock_irqsave(&priv->txlock, flags)) {
		tx_cleaned = gfar_clean_tx_ring(dev);
		spin_unlock_irqrestore(&priv->txlock, flags);
	}

	rx_cleaned = gfar_clean_rx_ring(dev, budget);

	if (tx_cleaned)
		return budget;

	if (rx_cleaned < budget) {
		napi_complete(napi);

		/* Clear the halt bit in RSTAT */
		gfar_write(&priv->regs->rstat, RSTAT_CLEAR_RHALT);

		gfar_write(&priv->regs->imask, IMASK_DEFAULT);

		/* If we are coalescing interrupts, update the timer */
		/* Otherwise, clear it */
		if (likely(priv->rxcoalescing)) {
			gfar_write(&priv->regs->rxic, 0);
			gfar_write(&priv->regs->rxic, priv->rxic);
		}
		if (likely(priv->txcoalescing)) {
			gfar_write(&priv->regs->txic, 0);
			gfar_write(&priv->regs->txic, priv->txic);
		}
	}

	return rx_cleaned;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 * Polling 'interrupt' - used by things like netconsole to send skbs
 * without having to re-enable interrupts. It's not called while
 * the interrupt routine is executing.
 */
static void gfar_netpoll(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);

	/* If the device has multiple interrupts, run tx/rx */
	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_MULTI_INTR) {
		disable_irq(priv->interruptTransmit);
		disable_irq(priv->interruptReceive);
		disable_irq(priv->interruptError);
		gfar_interrupt(priv->interruptTransmit, dev);
		enable_irq(priv->interruptError);
		enable_irq(priv->interruptReceive);
		enable_irq(priv->interruptTransmit);
	} else {
		disable_irq(priv->interruptTransmit);
		gfar_interrupt(priv->interruptTransmit, dev);
		enable_irq(priv->interruptTransmit);
	}
}
#endif

/* The interrupt handler for devices with one interrupt */
static irqreturn_t gfar_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct gfar_private *priv = netdev_priv(dev);

	/* Save ievent for future reference */
	u32 events = gfar_read(&priv->regs->ievent);

	/* Check for reception */
	if (events & IEVENT_RX_MASK)
		gfar_receive(irq, dev_id);

	/* Check for transmit completion */
	if (events & IEVENT_TX_MASK)
		gfar_transmit(irq, dev_id);

	/* Check for errors */
	if (events & IEVENT_ERR_MASK)
		gfar_error(irq, dev_id);

	return IRQ_HANDLED;
}

/* Called every time the controller might need to be made
 * aware of new link state.  The PHY code conveys this
 * information through variables in the phydev structure, and this
 * function converts those variables into the appropriate
 * register values, and can bring down the device if needed.
 */
static void adjust_link(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct gfar __iomem *regs = priv->regs;
	unsigned long flags;
	struct phy_device *phydev = priv->phydev;
	int new_state = 0;

	spin_lock_irqsave(&priv->txlock, flags);
	if (phydev->link) {
		u32 tempval = gfar_read(&regs->maccfg2);
		u32 ecntrl = gfar_read(&regs->ecntrl);

		/* Now we make sure that we can be in full duplex mode.
		 * If not, we operate in half-duplex mode. */
		if (phydev->duplex != priv->oldduplex) {
			new_state = 1;
			if (!(phydev->duplex))
				tempval &= ~(MACCFG2_FULL_DUPLEX);
			else
				tempval |= MACCFG2_FULL_DUPLEX;

			priv->oldduplex = phydev->duplex;
		}

		if (phydev->speed != priv->oldspeed) {
			new_state = 1;
			switch (phydev->speed) {
			case 1000:
				tempval =
				    ((tempval & ~(MACCFG2_IF)) | MACCFG2_GMII);

				ecntrl &= ~(ECNTRL_R100);
				break;
			case 100:
			case 10:
				tempval =
				    ((tempval & ~(MACCFG2_IF)) | MACCFG2_MII);

				/* Reduced mode distinguishes
				 * between 10 and 100 */
				if (phydev->speed == SPEED_100)
					ecntrl |= ECNTRL_R100;
				else
					ecntrl &= ~(ECNTRL_R100);
				break;
			default:
				if (netif_msg_link(priv))
					printk(KERN_WARNING
						"%s: Ack!  Speed (%d) is not 10/100/1000!\n",
						dev->name, phydev->speed);
				break;
			}

			priv->oldspeed = phydev->speed;
		}

		gfar_write(&regs->maccfg2, tempval);
		gfar_write(&regs->ecntrl, ecntrl);

		if (!priv->oldlink) {
			new_state = 1;
			priv->oldlink = 1;
		}
	} else if (priv->oldlink) {
		new_state = 1;
		priv->oldlink = 0;
		priv->oldspeed = 0;
		priv->oldduplex = -1;
	}

	if (new_state && netif_msg_link(priv))
		phy_print_status(phydev);

	spin_unlock_irqrestore(&priv->txlock, flags);
}

/* Update the hash table based on the current list of multicast
 * addresses we subscribe to.  Also, change the promiscuity of
 * the device based on the flags (this function is called
 * whenever dev->flags is changed */
static void gfar_set_multi(struct net_device *dev)
{
	struct dev_mc_list *mc_ptr;
	struct gfar_private *priv = netdev_priv(dev);
	struct gfar __iomem *regs = priv->regs;
	u32 tempval;

	if(dev->flags & IFF_PROMISC) {
		/* Set RCTRL to PROM */
		tempval = gfar_read(&regs->rctrl);
		tempval |= RCTRL_PROM;
		gfar_write(&regs->rctrl, tempval);
	} else {
		/* Set RCTRL to not PROM */
		tempval = gfar_read(&regs->rctrl);
		tempval &= ~(RCTRL_PROM);
		gfar_write(&regs->rctrl, tempval);
	}

	if(dev->flags & IFF_ALLMULTI) {
		/* Set the hash to rx all multicast frames */
		gfar_write(&regs->igaddr0, 0xffffffff);
		gfar_write(&regs->igaddr1, 0xffffffff);
		gfar_write(&regs->igaddr2, 0xffffffff);
		gfar_write(&regs->igaddr3, 0xffffffff);
		gfar_write(&regs->igaddr4, 0xffffffff);
		gfar_write(&regs->igaddr5, 0xffffffff);
		gfar_write(&regs->igaddr6, 0xffffffff);
		gfar_write(&regs->igaddr7, 0xffffffff);
		gfar_write(&regs->gaddr0, 0xffffffff);
		gfar_write(&regs->gaddr1, 0xffffffff);
		gfar_write(&regs->gaddr2, 0xffffffff);
		gfar_write(&regs->gaddr3, 0xffffffff);
		gfar_write(&regs->gaddr4, 0xffffffff);
		gfar_write(&regs->gaddr5, 0xffffffff);
		gfar_write(&regs->gaddr6, 0xffffffff);
		gfar_write(&regs->gaddr7, 0xffffffff);
	} else {
		int em_num;
		int idx;

		/* zero out the hash */
		gfar_write(&regs->igaddr0, 0x0);
		gfar_write(&regs->igaddr1, 0x0);
		gfar_write(&regs->igaddr2, 0x0);
		gfar_write(&regs->igaddr3, 0x0);
		gfar_write(&regs->igaddr4, 0x0);
		gfar_write(&regs->igaddr5, 0x0);
		gfar_write(&regs->igaddr6, 0x0);
		gfar_write(&regs->igaddr7, 0x0);
		gfar_write(&regs->gaddr0, 0x0);
		gfar_write(&regs->gaddr1, 0x0);
		gfar_write(&regs->gaddr2, 0x0);
		gfar_write(&regs->gaddr3, 0x0);
		gfar_write(&regs->gaddr4, 0x0);
		gfar_write(&regs->gaddr5, 0x0);
		gfar_write(&regs->gaddr6, 0x0);
		gfar_write(&regs->gaddr7, 0x0);

		/* If we have extended hash tables, we need to
		 * clear the exact match registers to prepare for
		 * setting them */
		if (priv->extended_hash) {
			em_num = GFAR_EM_NUM + 1;
			gfar_clear_exact_match(dev);
			idx = 1;
		} else {
			idx = 0;
			em_num = 0;
		}

		if(dev->mc_count == 0)
			return;

		/* Parse the list, and set the appropriate bits */
		for(mc_ptr = dev->mc_list; mc_ptr; mc_ptr = mc_ptr->next) {
			if (idx < em_num) {
				gfar_set_mac_for_addr(dev, idx,
						mc_ptr->dmi_addr);
				idx++;
			} else
				gfar_set_hash_for_addr(dev, mc_ptr->dmi_addr);
		}
	}

	return;
}


/* Clears each of the exact match registers to zero, so they
 * don't interfere with normal reception */
static void gfar_clear_exact_match(struct net_device *dev)
{
	int idx;
	u8 zero_arr[MAC_ADDR_LEN] = {0,0,0,0,0,0};

	for(idx = 1;idx < GFAR_EM_NUM + 1;idx++)
		gfar_set_mac_for_addr(dev, idx, (u8 *)zero_arr);
}

/* Set the appropriate hash bit for the given addr */
/* The algorithm works like so:
 * 1) Take the Destination Address (ie the multicast address), and
 * do a CRC on it (little endian), and reverse the bits of the
 * result.
 * 2) Use the 8 most significant bits as a hash into a 256-entry
 * table.  The table is controlled through 8 32-bit registers:
 * gaddr0-7.  gaddr0's MSB is entry 0, and gaddr7's LSB is
 * gaddr7.  This means that the 3 most significant bits in the
 * hash index which gaddr register to use, and the 5 other bits
 * indicate which bit (assuming an IBM numbering scheme, which
 * for PowerPC (tm) is usually the case) in the register holds
 * the entry. */
static void gfar_set_hash_for_addr(struct net_device *dev, u8 *addr)
{
	u32 tempval;
	struct gfar_private *priv = netdev_priv(dev);
	u32 result = ether_crc(MAC_ADDR_LEN, addr);
	int width = priv->hash_width;
	u8 whichbit = (result >> (32 - width)) & 0x1f;
	u8 whichreg = result >> (32 - width + 5);
	u32 value = (1 << (31-whichbit));

	tempval = gfar_read(priv->hash_regs[whichreg]);
	tempval |= value;
	gfar_write(priv->hash_regs[whichreg], tempval);

	return;
}


/* There are multiple MAC Address register pairs on some controllers
 * This function sets the numth pair to a given address
 */
static void gfar_set_mac_for_addr(struct net_device *dev, int num, u8 *addr)
{
	struct gfar_private *priv = netdev_priv(dev);
	int idx;
	char tmpbuf[MAC_ADDR_LEN];
	u32 tempval;
	u32 __iomem *macptr = &priv->regs->macstnaddr1;

	macptr += num*2;

	/* Now copy it into the mac registers backwards, cuz */
	/* little endian is silly */
	for (idx = 0; idx < MAC_ADDR_LEN; idx++)
		tmpbuf[MAC_ADDR_LEN - 1 - idx] = addr[idx];

	gfar_write(macptr, *((u32 *) (tmpbuf)));

	tempval = *((u32 *) (tmpbuf + 4));

	gfar_write(macptr+1, tempval);
}

/* GFAR error interrupt handler */
static irqreturn_t gfar_error(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct gfar_private *priv = netdev_priv(dev);

	/* Save ievent for future reference */
	u32 events = gfar_read(&priv->regs->ievent);

	/* Clear IEVENT */
	gfar_write(&priv->regs->ievent, events & IEVENT_ERR_MASK);

	/* Magic Packet is not an error. */
	if ((priv->device_flags & FSL_GIANFAR_DEV_HAS_MAGIC_PACKET) &&
	    (events & IEVENT_MAG))
		events &= ~IEVENT_MAG;

	/* Hmm... */
	if (netif_msg_rx_err(priv) || netif_msg_tx_err(priv))
		printk(KERN_DEBUG "%s: error interrupt (ievent=0x%08x imask=0x%08x)\n",
		       dev->name, events, gfar_read(&priv->regs->imask));

	/* Update the error counters */
	if (events & IEVENT_TXE) {
		dev->stats.tx_errors++;

		if (events & IEVENT_LC)
			dev->stats.tx_window_errors++;
		if (events & IEVENT_CRL)
			dev->stats.tx_aborted_errors++;
		if (events & IEVENT_XFUN) {
			if (netif_msg_tx_err(priv))
				printk(KERN_DEBUG "%s: TX FIFO underrun, "
				       "packet dropped.\n", dev->name);
			dev->stats.tx_dropped++;
			priv->extra_stats.tx_underrun++;

			/* Reactivate the Tx Queues */
			gfar_write(&priv->regs->tstat, TSTAT_CLEAR_THALT);
		}
		if (netif_msg_tx_err(priv))
			printk(KERN_DEBUG "%s: Transmit Error\n", dev->name);
	}
	if (events & IEVENT_BSY) {
		dev->stats.rx_errors++;
		priv->extra_stats.rx_bsy++;

		gfar_receive(irq, dev_id);

		if (netif_msg_rx_err(priv))
			printk(KERN_DEBUG "%s: busy error (rstat: %x)\n",
			       dev->name, gfar_read(&priv->regs->rstat));
	}
	if (events & IEVENT_BABR) {
		dev->stats.rx_errors++;
		priv->extra_stats.rx_babr++;

		if (netif_msg_rx_err(priv))
			printk(KERN_DEBUG "%s: babbling RX error\n", dev->name);
	}
	if (events & IEVENT_EBERR) {
		priv->extra_stats.eberr++;
		if (netif_msg_rx_err(priv))
			printk(KERN_DEBUG "%s: bus error\n", dev->name);
	}
	if ((events & IEVENT_RXC) && netif_msg_rx_status(priv))
		printk(KERN_DEBUG "%s: control frame\n", dev->name);

	if (events & IEVENT_BABT) {
		priv->extra_stats.tx_babt++;
		if (netif_msg_tx_err(priv))
			printk(KERN_DEBUG "%s: babbling TX error\n", dev->name);
	}
	return IRQ_HANDLED;
}

static struct of_device_id gfar_match[] =
{
	{
		.type = "network",
		.compatible = "gianfar",
	},
	{},
};
MODULE_DEVICE_TABLE(of, gfar_match);

/* Structure for a device driver */
static struct of_platform_driver gfar_driver = {
	.name = "fsl-gianfar",
	.match_table = gfar_match,

	.probe = gfar_probe,
	.remove = gfar_remove,
	.suspend = gfar_suspend,
	.resume = gfar_resume,
};

static int __init gfar_init(void)
{
	return of_register_platform_driver(&gfar_driver);
}

static void __exit gfar_exit(void)
{
	of_unregister_platform_driver(&gfar_driver);
}

module_init(gfar_init);
module_exit(gfar_exit);

