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
 *  The driver is initialized through platform_device.  Structures which
 *  define the configuration needed by the board are defined in a
 *  board structure in arch/ppc/platforms (though I do not
 *  discount the possibility that other architectures could one
 *  day be supported.
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
 *  exit.  Without NAPI, the packet(s) will be handled
 *  immediately.  Both methods will start at the last known empty
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
#include <linux/platform_device.h>
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

#include "gianfar.h"
#include "gianfar_mii.h"

#define TX_TIMEOUT      (1*HZ)
#undef BRIEF_GFAR_ERRORS
#undef VERBOSE_GFAR_ERRORS

#ifdef CONFIG_GFAR_NAPI
#define RECEIVE(x) netif_receive_skb(x)
#else
#define RECEIVE(x) netif_rx(x)
#endif

const char gfar_driver_name[] = "Gianfar Ethernet";
const char gfar_driver_version[] = "1.3";

static int gfar_enet_open(struct net_device *dev);
static int gfar_start_xmit(struct sk_buff *skb, struct net_device *dev);
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
static int gfar_probe(struct platform_device *pdev);
static int gfar_remove(struct platform_device *pdev);
static void free_skb_resources(struct gfar_private *priv);
static void gfar_set_multi(struct net_device *dev);
static void gfar_set_hash_for_addr(struct net_device *dev, u8 *addr);
static void gfar_configure_serdes(struct net_device *dev);
extern int gfar_local_mdio_write(struct gfar_mii __iomem *regs, int mii_id, int regnum, u16 value);
extern int gfar_local_mdio_read(struct gfar_mii __iomem *regs, int mii_id, int regnum);
#ifdef CONFIG_GFAR_NAPI
static int gfar_poll(struct napi_struct *napi, int budget);
#endif
#ifdef CONFIG_NET_POLL_CONTROLLER
static void gfar_netpoll(struct net_device *dev);
#endif
int gfar_clean_rx_ring(struct net_device *dev, int rx_work_limit);
static int gfar_process_frame(struct net_device *dev, struct sk_buff *skb, int length);
static void gfar_vlan_rx_register(struct net_device *netdev,
		                struct vlan_group *grp);
void gfar_halt(struct net_device *dev);
void gfar_start(struct net_device *dev);
static void gfar_clear_exact_match(struct net_device *dev);
static void gfar_set_mac_for_addr(struct net_device *dev, int num, u8 *addr);

extern const struct ethtool_ops gfar_ethtool_ops;

MODULE_AUTHOR("Freescale Semiconductor, Inc");
MODULE_DESCRIPTION("Gianfar Ethernet Driver");
MODULE_LICENSE("GPL");

/* Returns 1 if incoming frames use an FCB */
static inline int gfar_uses_fcb(struct gfar_private *priv)
{
	return (priv->vlan_enable || priv->rx_csum_enable);
}

/* Set up the ethernet device structure, private data,
 * and anything else we need before we start */
static int gfar_probe(struct platform_device *pdev)
{
	u32 tempval;
	struct net_device *dev = NULL;
	struct gfar_private *priv = NULL;
	struct gianfar_platform_data *einfo;
	struct resource *r;
	int err = 0;
	DECLARE_MAC_BUF(mac);

	einfo = (struct gianfar_platform_data *) pdev->dev.platform_data;

	if (NULL == einfo) {
		printk(KERN_ERR "gfar %d: Missing additional data!\n",
		       pdev->id);

		return -ENODEV;
	}

	/* Create an ethernet device instance */
	dev = alloc_etherdev(sizeof (*priv));

	if (NULL == dev)
		return -ENOMEM;

	priv = netdev_priv(dev);
	priv->dev = dev;

	/* Set the info in the priv to the current info */
	priv->einfo = einfo;

	/* fill out IRQ fields */
	if (einfo->device_flags & FSL_GIANFAR_DEV_HAS_MULTI_INTR) {
		priv->interruptTransmit = platform_get_irq_byname(pdev, "tx");
		priv->interruptReceive = platform_get_irq_byname(pdev, "rx");
		priv->interruptError = platform_get_irq_byname(pdev, "error");
		if (priv->interruptTransmit < 0 || priv->interruptReceive < 0 || priv->interruptError < 0)
			goto regs_fail;
	} else {
		priv->interruptTransmit = platform_get_irq(pdev, 0);
		if (priv->interruptTransmit < 0)
			goto regs_fail;
	}

	/* get a pointer to the register memory */
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regs = ioremap(r->start, sizeof (struct gfar));

	if (NULL == priv->regs) {
		err = -ENOMEM;
		goto regs_fail;
	}

	spin_lock_init(&priv->txlock);
	spin_lock_init(&priv->rxlock);

	platform_set_drvdata(pdev, dev);

	/* Stop the DMA engine now, in case it was running before */
	/* (The firmware could have used it, and left it running). */
	/* To do this, we write Graceful Receive Stop and Graceful */
	/* Transmit Stop, and then wait until the corresponding bits */
	/* in IEVENT indicate the stops have completed. */
	tempval = gfar_read(&priv->regs->dmactrl);
	tempval &= ~(DMACTRL_GRS | DMACTRL_GTS);
	gfar_write(&priv->regs->dmactrl, tempval);

	tempval = gfar_read(&priv->regs->dmactrl);
	tempval |= (DMACTRL_GRS | DMACTRL_GTS);
	gfar_write(&priv->regs->dmactrl, tempval);

	while (!(gfar_read(&priv->regs->ievent) & (IEVENT_GRSC | IEVENT_GTSC)))
		cpu_relax();

	/* Reset MAC layer */
	gfar_write(&priv->regs->maccfg1, MACCFG1_SOFT_RESET);

	tempval = (MACCFG1_TX_FLOW | MACCFG1_RX_FLOW);
	gfar_write(&priv->regs->maccfg1, tempval);

	/* Initialize MACCFG2. */
	gfar_write(&priv->regs->maccfg2, MACCFG2_INIT_SETTINGS);

	/* Initialize ECNTRL */
	gfar_write(&priv->regs->ecntrl, ECNTRL_INIT_SETTINGS);

	/* Copy the station address into the dev structure, */
	memcpy(dev->dev_addr, einfo->mac_addr, MAC_ADDR_LEN);

	/* Set the dev->base_addr to the gfar reg region */
	dev->base_addr = (unsigned long) (priv->regs);

	SET_NETDEV_DEV(dev, &pdev->dev);

	/* Fill in the dev structure */
	dev->open = gfar_enet_open;
	dev->hard_start_xmit = gfar_start_xmit;
	dev->tx_timeout = gfar_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;
#ifdef CONFIG_GFAR_NAPI
	netif_napi_add(dev, &priv->napi, gfar_poll, GFAR_DEV_WEIGHT);
#endif
#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = gfar_netpoll;
#endif
	dev->stop = gfar_close;
	dev->change_mtu = gfar_change_mtu;
	dev->mtu = 1500;
	dev->set_multicast_list = gfar_set_multi;

	dev->ethtool_ops = &gfar_ethtool_ops;

	if (priv->einfo->device_flags & FSL_GIANFAR_DEV_HAS_CSUM) {
		priv->rx_csum_enable = 1;
		dev->features |= NETIF_F_IP_CSUM;
	} else
		priv->rx_csum_enable = 0;

	priv->vlgrp = NULL;

	if (priv->einfo->device_flags & FSL_GIANFAR_DEV_HAS_VLAN) {
		dev->vlan_rx_register = gfar_vlan_rx_register;

		dev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;

		priv->vlan_enable = 1;
	}

	if (priv->einfo->device_flags & FSL_GIANFAR_DEV_HAS_EXTENDED_HASH) {
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

	if (priv->einfo->device_flags & FSL_GIANFAR_DEV_HAS_PADDING)
		priv->padding = DEFAULT_PADDING;
	else
		priv->padding = 0;

	if (dev->features & NETIF_F_IP_CSUM)
		dev->hard_header_len += GMAC_FCB_LEN;

	priv->rx_buffer_size = DEFAULT_RX_BUFFER_SIZE;
	priv->tx_ring_size = DEFAULT_TX_RING_SIZE;
	priv->rx_ring_size = DEFAULT_RX_RING_SIZE;

	priv->txcoalescing = DEFAULT_TX_COALESCE;
	priv->txcount = DEFAULT_TXCOUNT;
	priv->txtime = DEFAULT_TXTIME;
	priv->rxcoalescing = DEFAULT_RX_COALESCE;
	priv->rxcount = DEFAULT_RXCOUNT;
	priv->rxtime = DEFAULT_RXTIME;

	/* Enable most messages by default */
	priv->msg_enable = (NETIF_MSG_IFUP << 1 ) - 1;

	err = register_netdev(dev);

	if (err) {
		printk(KERN_ERR "%s: Cannot register net device, aborting.\n",
				dev->name);
		goto register_fail;
	}

	/* Create all the sysfs files */
	gfar_init_sysfs(dev);

	/* Print out the device info */
	printk(KERN_INFO DEVICE_NAME "%s\n",
	       dev->name, print_mac(mac, dev->dev_addr));

	/* Even more device info helps when determining which kernel */
	/* provided which set of benchmarks. */
#ifdef CONFIG_GFAR_NAPI
	printk(KERN_INFO "%s: Running with NAPI enabled\n", dev->name);
#else
	printk(KERN_INFO "%s: Running with NAPI disabled\n", dev->name);
#endif
	printk(KERN_INFO "%s: %d/%d RX/TX BD ring size\n",
	       dev->name, priv->rx_ring_size, priv->tx_ring_size);

	return 0;

register_fail:
	iounmap(priv->regs);
regs_fail:
	free_netdev(dev);
	return err;
}

static int gfar_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct gfar_private *priv = netdev_priv(dev);

	platform_set_drvdata(pdev, NULL);

	iounmap(priv->regs);
	free_netdev(dev);

	return 0;
}


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
			phy_interface_t interface = priv->einfo->interface;

			/*
			 * This isn't autodetected right now, so it must
			 * be set by the device tree or platform code.
			 */
			if (interface == PHY_INTERFACE_MODE_RGMII_ID)
				return PHY_INTERFACE_MODE_RGMII_ID;

			return PHY_INTERFACE_MODE_RGMII;
		}
	}

	if (priv->einfo->device_flags & FSL_GIANFAR_DEV_HAS_GIGABIT)
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
		priv->einfo->device_flags & FSL_GIANFAR_DEV_HAS_GIGABIT ?
		SUPPORTED_1000baseT_Full : 0;
	struct phy_device *phydev;
	char phy_id[BUS_ID_SIZE];
	phy_interface_t interface;

	priv->oldlink = 0;
	priv->oldspeed = 0;
	priv->oldduplex = -1;

	snprintf(phy_id, BUS_ID_SIZE, PHY_ID_FMT, priv->einfo->bus_id, priv->einfo->phy_id);

	interface = gfar_get_interface(dev);

	phydev = phy_connect(dev, phy_id, &adjust_link, 0, interface);

	if (interface == PHY_INTERFACE_MODE_SGMII)
		gfar_configure_serdes(dev);

	if (IS_ERR(phydev)) {
		printk(KERN_ERR "%s: Could not attach to PHY\n", dev->name);
		return PTR_ERR(phydev);
	}

	/* Remove any features not supported by the controller */
	phydev->supported &= (GFAR_SUPPORTED | gigabit_support);
	phydev->advertising = phydev->supported;

	priv->phydev = phydev;

	return 0;
}

static void gfar_configure_serdes(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct gfar_mii __iomem *regs =
			(void __iomem *)&priv->regs->gfar_mii_regs;

	/* Initialise TBI i/f to communicate with serdes (lynx phy) */

	/* Single clk mode, mii mode off(for aerdes communication) */
	gfar_local_mdio_write(regs, TBIPA_VALUE, MII_TBICON, TBICON_CLK_SELECT);

	/* Supported pause and full-duplex, no half-duplex */
	gfar_local_mdio_write(regs, TBIPA_VALUE, MII_ADVERTISE,
			ADVERTISE_1000XFULL | ADVERTISE_1000XPAUSE |
			ADVERTISE_1000XPSE_ASYM);

	/* ANEG enable, restart ANEG, full duplex mode, speed[1] set */
	gfar_local_mdio_write(regs, TBIPA_VALUE, MII_BMCR, BMCR_ANENABLE |
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
	if (priv->einfo->device_flags & FSL_GIANFAR_DEV_HAS_RMON) {
		memset_io(&(priv->regs->rmon), 0, sizeof (struct rmon_mib));

		/* Mask off the CAM interrupts */
		gfar_write(&priv->regs->rmon.cam1, 0xffffffff);
		gfar_write(&priv->regs->rmon.cam2, 0xffffffff);
	}

	/* Initialize the max receive buffer length */
	gfar_write(&priv->regs->mrblr, priv->rx_buffer_size);

	/* Initialize the Minimum Frame Length Register */
	gfar_write(&priv->regs->minflr, MINFLR_INIT_SETTINGS);

	/* Assign the TBI an address which won't conflict with the PHYs */
	gfar_write(&priv->regs->tbipa, TBIPA_VALUE);
}


/* Halt the receive and transmit queues */
void gfar_halt(struct net_device *dev)
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
	if (priv->einfo->device_flags & FSL_GIANFAR_DEV_HAS_MULTI_INTR) {
		free_irq(priv->interruptError, dev);
		free_irq(priv->interruptTransmit, dev);
		free_irq(priv->interruptReceive, dev);
	} else {
 		free_irq(priv->interruptTransmit, dev);
	}

	free_skb_resources(priv);

	dma_free_coherent(&dev->dev,
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
	int i;

	/* Go through all the buffer descriptors and free their data buffers */
	txbdp = priv->tx_bd_base;

	for (i = 0; i < priv->tx_ring_size; i++) {

		if (priv->tx_skbuff[i]) {
			dma_unmap_single(&priv->dev->dev, txbdp->bufPtr,
					txbdp->length,
					DMA_TO_DEVICE);
			dev_kfree_skb_any(priv->tx_skbuff[i]);
			priv->tx_skbuff[i] = NULL;
		}
	}

	kfree(priv->tx_skbuff);

	rxbdp = priv->rx_bd_base;

	/* rx_skbuff is not guaranteed to be allocated, so only
	 * free it and its contents if it is allocated */
	if(priv->rx_skbuff != NULL) {
		for (i = 0; i < priv->rx_ring_size; i++) {
			if (priv->rx_skbuff[i]) {
				dma_unmap_single(&priv->dev->dev, rxbdp->bufPtr,
						priv->rx_buffer_size,
						DMA_FROM_DEVICE);

				dev_kfree_skb_any(priv->rx_skbuff[i]);
				priv->rx_skbuff[i] = NULL;
			}

			rxbdp->status = 0;
			rxbdp->length = 0;
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
	u32 attrs = 0;

	gfar_write(&regs->imask, IMASK_INIT_CLEAR);

	/* Allocate memory for the buffer descriptors */
	vaddr = (unsigned long) dma_alloc_coherent(&dev->dev,
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
	priv->dirty_tx = priv->cur_tx = priv->tx_bd_base;
	priv->cur_rx = priv->rx_bd_base;
	priv->skb_curtx = priv->skb_dirtytx = 0;
	priv->skb_currx = 0;

	/* Initialize Transmit Descriptor Ring */
	txbdp = priv->tx_bd_base;
	for (i = 0; i < priv->tx_ring_size; i++) {
		txbdp->status = 0;
		txbdp->length = 0;
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
	if (priv->einfo->device_flags & FSL_GIANFAR_DEV_HAS_MULTI_INTR) {
		/* Install our interrupt handlers for Error,
		 * Transmit, and Receive */
		if (request_irq(priv->interruptError, gfar_error,
				0, "enet_error", dev) < 0) {
			if (netif_msg_intr(priv))
				printk(KERN_ERR "%s: Can't get IRQ %d\n",
					dev->name, priv->interruptError);

			err = -1;
			goto err_irq_fail;
		}

		if (request_irq(priv->interruptTransmit, gfar_transmit,
				0, "enet_tx", dev) < 0) {
			if (netif_msg_intr(priv))
				printk(KERN_ERR "%s: Can't get IRQ %d\n",
					dev->name, priv->interruptTransmit);

			err = -1;

			goto tx_irq_fail;
		}

		if (request_irq(priv->interruptReceive, gfar_receive,
				0, "enet_rx", dev) < 0) {
			if (netif_msg_intr(priv))
				printk(KERN_ERR "%s: Can't get IRQ %d (receive0)\n",
						dev->name, priv->interruptReceive);

			err = -1;
			goto rx_irq_fail;
		}
	} else {
		if (request_irq(priv->interruptTransmit, gfar_interrupt,
				0, "gfar_interrupt", dev) < 0) {
			if (netif_msg_intr(priv))
				printk(KERN_ERR "%s: Can't get IRQ %d\n",
					dev->name, priv->interruptError);

			err = -1;
			goto err_irq_fail;
		}
	}

	phy_start(priv->phydev);

	/* Configure the coalescing support */
	if (priv->txcoalescing)
		gfar_write(&regs->txic,
			   mk_ic_value(priv->txcount, priv->txtime));
	else
		gfar_write(&regs->txic, 0);

	if (priv->rxcoalescing)
		gfar_write(&regs->rxic,
			   mk_ic_value(priv->rxcount, priv->rxtime));
	else
		gfar_write(&regs->rxic, 0);

	if (priv->rx_csum_enable)
		rctrl |= RCTRL_CHECKSUMMING;

	if (priv->extended_hash) {
		rctrl |= RCTRL_EXTHASH;

		gfar_clear_exact_match(dev);
		rctrl |= RCTRL_EMEN;
	}

	if (priv->vlan_enable)
		rctrl |= RCTRL_VLAN;

	if (priv->padding) {
		rctrl &= ~RCTRL_PAL_MASK;
		rctrl |= RCTRL_PADDING(priv->padding);
	}

	/* Init rctrl based on our settings */
	gfar_write(&priv->regs->rctrl, rctrl);

	if (dev->features & NETIF_F_IP_CSUM)
		gfar_write(&priv->regs->tctrl, TCTRL_INIT_CSUM);

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
	dma_free_coherent(&dev->dev,
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
#ifdef CONFIG_GFAR_NAPI
	struct gfar_private *priv = netdev_priv(dev);
#endif
	int err;

#ifdef CONFIG_GFAR_NAPI
	napi_enable(&priv->napi);
#endif

	/* Initialize a bunch of registers */
	init_registers(dev);

	gfar_set_mac_address(dev);

	err = init_phy(dev);

	if(err) {
#ifdef CONFIG_GFAR_NAPI
		napi_disable(&priv->napi);
#endif
		return err;
	}

	err = startup_gfar(dev);
	if (err) {
#ifdef CONFIG_GFAR_NAPI
		napi_disable(&priv->napi);
#endif
		return err;
	}

	netif_start_queue(dev);

	return err;
}

static inline struct txfcb *gfar_add_fcb(struct sk_buff *skb, struct txbd8 *bdp)
{
	struct txfcb *fcb = (struct txfcb *)skb_push (skb, GMAC_FCB_LEN);

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

/* This is called by the kernel when a frame is ready for transmission. */
/* It is pointed to by the dev->hard_start_xmit function pointer */
static int gfar_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct txfcb *fcb = NULL;
	struct txbd8 *txbdp;
	u16 status;
	unsigned long flags;

	/* Update transmit stats */
	dev->stats.tx_bytes += skb->len;

	/* Lock priv now */
	spin_lock_irqsave(&priv->txlock, flags);

	/* Point at the first free tx descriptor */
	txbdp = priv->cur_tx;

	/* Clear all but the WRAP status flags */
	status = txbdp->status & TXBD_WRAP;

	/* Set up checksumming */
	if (likely((dev->features & NETIF_F_IP_CSUM)
			&& (CHECKSUM_PARTIAL == skb->ip_summed))) {
		fcb = gfar_add_fcb(skb, txbdp);
		status |= TXBD_TOE;
		gfar_tx_checksum(skb, fcb);
	}

	if (priv->vlan_enable &&
			unlikely(priv->vlgrp && vlan_tx_tag_present(skb))) {
		if (unlikely(NULL == fcb)) {
			fcb = gfar_add_fcb(skb, txbdp);
			status |= TXBD_TOE;
		}

		gfar_tx_vlan(skb, fcb);
	}

	/* Set buffer length and pointer */
	txbdp->length = skb->len;
	txbdp->bufPtr = dma_map_single(&dev->dev, skb->data,
			skb->len, DMA_TO_DEVICE);

	/* Save the skb pointer so we can free it later */
	priv->tx_skbuff[priv->skb_curtx] = skb;

	/* Update the current skb pointer (wrapping if this was the last) */
	priv->skb_curtx =
	    (priv->skb_curtx + 1) & TX_RING_MOD_MASK(priv->tx_ring_size);

	/* Flag the BD as interrupt-causing */
	status |= TXBD_INTERRUPT;

	/* Flag the BD as ready to go, last in frame, and  */
	/* in need of CRC */
	status |= (TXBD_READY | TXBD_LAST | TXBD_CRC);

	dev->trans_start = jiffies;

	/* The powerpc-specific eieio() is used, as wmb() has too strong
	 * semantics (it requires synchronization between cacheable and
	 * uncacheable mappings, which eieio doesn't provide and which we
	 * don't need), thus requiring a more expensive sync instruction.  At
	 * some point, the set of architecture-independent barrier functions
	 * should be expanded to include weaker barriers.
	 */

	eieio();
	txbdp->status = status;

	/* If this was the last BD in the ring, the next one */
	/* is at the beginning of the ring */
	if (txbdp->status & TXBD_WRAP)
		txbdp = priv->tx_bd_base;
	else
		txbdp++;

	/* If the next BD still needs to be cleaned up, then the bds
	   are full.  We need to tell the kernel to stop sending us stuff. */
	if (txbdp == priv->dirty_tx) {
		netif_stop_queue(dev);

		dev->stats.tx_fifo_errors++;
	}

	/* Update the current txbd to the next one */
	priv->cur_tx = txbdp;

	/* Tell the DMA to go go go */
	gfar_write(&priv->regs->tstat, TSTAT_CLEAR_THALT);

	/* Unlock priv */
	spin_unlock_irqrestore(&priv->txlock, flags);

	return 0;
}

/* Stops the kernel queue, and halts the controller */
static int gfar_close(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);

#ifdef CONFIG_GFAR_NAPI
	napi_disable(&priv->napi);
#endif

	stop_gfar(dev);

	/* Disconnect from the PHY */
	phy_disconnect(priv->phydev);
	priv->phydev = NULL;

	netif_stop_queue(dev);

	return 0;
}

/* Changes the mac address if the controller is not running. */
int gfar_set_mac_address(struct net_device *dev)
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
		tempval |= RCTRL_VLEX;
		gfar_write(&priv->regs->rctrl, tempval);
	} else {
		/* Disable VLAN tag insertion */
		tempval = gfar_read(&priv->regs->tctrl);
		tempval &= ~TCTRL_VLINS;
		gfar_write(&priv->regs->tctrl, tempval);

		/* Disable VLAN tag extraction */
		tempval = gfar_read(&priv->regs->rctrl);
		tempval &= ~RCTRL_VLEX;
		gfar_write(&priv->regs->rctrl, tempval);
	}

	spin_unlock_irqrestore(&priv->rxlock, flags);
}

static int gfar_change_mtu(struct net_device *dev, int new_mtu)
{
	int tempsize, tempval;
	struct gfar_private *priv = netdev_priv(dev);
	int oldsize = priv->rx_buffer_size;
	int frame_size = new_mtu + ETH_HLEN;

	if (priv->vlan_enable)
		frame_size += VLAN_HLEN;

	if (gfar_uses_fcb(priv))
		frame_size += GMAC_FCB_LEN;

	frame_size += priv->padding;

	if ((frame_size < 64) || (frame_size > JUMBO_FRAME_SIZE)) {
		if (netif_msg_drv(priv))
			printk(KERN_ERR "%s: Invalid MTU setting\n",
					dev->name);
		return -EINVAL;
	}

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

/* gfar_timeout gets called when a packet has not been
 * transmitted after a set amount of time.
 * For now, assume that clearing out all the structures, and
 * starting over will fix the problem. */
static void gfar_timeout(struct net_device *dev)
{
	dev->stats.tx_errors++;

	if (dev->flags & IFF_UP) {
		stop_gfar(dev);
		startup_gfar(dev);
	}

	netif_schedule(dev);
}

/* Interrupt Handler for Transmit complete */
int gfar_clean_tx_ring(struct net_device *dev)
{
	struct txbd8 *bdp;
	struct gfar_private *priv = netdev_priv(dev);
	int howmany = 0;

	bdp = priv->dirty_tx;
	while ((bdp->status & TXBD_READY) == 0) {
		/* If dirty_tx and cur_tx are the same, then either the */
		/* ring is empty or full now (it could only be full in the beginning, */
		/* obviously).  If it is empty, we are done. */
		if ((bdp == priv->cur_tx) && (netif_queue_stopped(dev) == 0))
			break;

		howmany++;

		/* Deferred means some collisions occurred during transmit, */
		/* but we eventually sent the packet. */
		if (bdp->status & TXBD_DEF)
			dev->stats.collisions++;

		/* Free the sk buffer associated with this TxBD */
		dev_kfree_skb_irq(priv->tx_skbuff[priv->skb_dirtytx]);

		priv->tx_skbuff[priv->skb_dirtytx] = NULL;
		priv->skb_dirtytx =
		    (priv->skb_dirtytx +
		     1) & TX_RING_MOD_MASK(priv->tx_ring_size);

		/* Clean BD length for empty detection */
		bdp->length = 0;

		/* update bdp to point at next bd in the ring (wrapping if necessary) */
		if (bdp->status & TXBD_WRAP)
			bdp = priv->tx_bd_base;
		else
			bdp++;

		/* Move dirty_tx to be the next bd */
		priv->dirty_tx = bdp;

		/* We freed a buffer, so now we can restart transmission */
		if (netif_queue_stopped(dev))
			netif_wake_queue(dev);
	} /* while ((bdp->status & TXBD_READY) == 0) */

	dev->stats.tx_packets += howmany;

	return howmany;
}

/* Interrupt Handler for Transmit complete */
static irqreturn_t gfar_transmit(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct gfar_private *priv = netdev_priv(dev);

	/* Clear IEVENT */
	gfar_write(&priv->regs->ievent, IEVENT_TX_MASK);

	/* Lock priv */
	spin_lock(&priv->txlock);

	gfar_clean_tx_ring(dev);

	/* If we are coalescing the interrupts, reset the timer */
	/* Otherwise, clear it */
	if (likely(priv->txcoalescing)) {
		gfar_write(&priv->regs->txic, 0);
		gfar_write(&priv->regs->txic,
			   mk_ic_value(priv->txcount, priv->txtime));
	}

	spin_unlock(&priv->txlock);

	return IRQ_HANDLED;
}

static void gfar_new_rxbdp(struct net_device *dev, struct rxbd8 *bdp,
		struct sk_buff *skb)
{
	struct gfar_private *priv = netdev_priv(dev);
	u32 * status_len = (u32 *)bdp;
	u16 flags;

	bdp->bufPtr = dma_map_single(&dev->dev, skb->data,
			priv->rx_buffer_size, DMA_FROM_DEVICE);

	flags = RXBD_EMPTY | RXBD_INTERRUPT;

	if (bdp == priv->rx_bd_base + priv->rx_ring_size - 1)
		flags |= RXBD_WRAP;

	eieio();

	*status_len = (u32)flags << 16;
}


struct sk_buff * gfar_new_skb(struct net_device *dev)
{
	unsigned int alignamount;
	struct gfar_private *priv = netdev_priv(dev);
	struct sk_buff *skb = NULL;

	/* We have to allocate the skb, so keep trying till we succeed */
	skb = netdev_alloc_skb(dev, priv->rx_buffer_size + RXBUF_ALIGNMENT);

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
	struct net_device *dev = (struct net_device *) dev_id;
	struct gfar_private *priv = netdev_priv(dev);
#ifdef CONFIG_GFAR_NAPI
	u32 tempval;
#else
	unsigned long flags;
#endif

	/* support NAPI */
#ifdef CONFIG_GFAR_NAPI
	/* Clear IEVENT, so interrupts aren't called again
	 * because of the packets that have already arrived */
	gfar_write(&priv->regs->ievent, IEVENT_RTX_MASK);

	if (netif_rx_schedule_prep(dev, &priv->napi)) {
		tempval = gfar_read(&priv->regs->imask);
		tempval &= IMASK_RTX_DISABLED;
		gfar_write(&priv->regs->imask, tempval);

		__netif_rx_schedule(dev, &priv->napi);
	} else {
		if (netif_msg_rx_err(priv))
			printk(KERN_DEBUG "%s: receive called twice (%x)[%x]\n",
				dev->name, gfar_read(&priv->regs->ievent),
				gfar_read(&priv->regs->imask));
	}
#else
	/* Clear IEVENT, so rx interrupt isn't called again
	 * because of this interrupt */
	gfar_write(&priv->regs->ievent, IEVENT_RX_MASK);

	spin_lock_irqsave(&priv->rxlock, flags);
	gfar_clean_rx_ring(dev, priv->rx_ring_size);

	/* If we are coalescing interrupts, update the timer */
	/* Otherwise, clear it */
	if (likely(priv->rxcoalescing)) {
		gfar_write(&priv->regs->rxic, 0);
		gfar_write(&priv->regs->rxic,
			   mk_ic_value(priv->rxcount, priv->rxtime));
	}

	spin_unlock_irqrestore(&priv->rxlock, flags);
#endif

	return IRQ_HANDLED;
}

static inline int gfar_rx_vlan(struct sk_buff *skb,
		struct vlan_group *vlgrp, unsigned short vlctl)
{
#ifdef CONFIG_GFAR_NAPI
	return vlan_hwaccel_receive_skb(skb, vlgrp, vlctl);
#else
	return vlan_hwaccel_rx(skb, vlgrp, vlctl);
#endif
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


static inline struct rxfcb *gfar_get_fcb(struct sk_buff *skb)
{
	struct rxfcb *fcb = (struct rxfcb *)skb->data;

	/* Remove the FCB from the skb */
	skb_pull(skb, GMAC_FCB_LEN);

	return fcb;
}

/* gfar_process_frame() -- handle one incoming packet if skb
 * isn't NULL.  */
static int gfar_process_frame(struct net_device *dev, struct sk_buff *skb,
		int length)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct rxfcb *fcb = NULL;

	if (NULL == skb) {
		if (netif_msg_rx_err(priv))
			printk(KERN_WARNING "%s: Missing skb!!.\n", dev->name);
		dev->stats.rx_dropped++;
		priv->extra_stats.rx_skbmissing++;
	} else {
		int ret;

		/* Prep the skb for the packet */
		skb_put(skb, length);

		/* Grab the FCB if there is one */
		if (gfar_uses_fcb(priv))
			fcb = gfar_get_fcb(skb);

		/* Remove the padded bytes, if there are any */
		if (priv->padding)
			skb_pull(skb, priv->padding);

		if (priv->rx_csum_enable)
			gfar_rx_checksum(skb, fcb);

		/* Tell the skb what kind of packet this is */
		skb->protocol = eth_type_trans(skb, dev);

		/* Send the packet up the stack */
		if (unlikely(priv->vlgrp && (fcb->flags & RXFCB_VLN)))
			ret = gfar_rx_vlan(skb, priv->vlgrp, fcb->vlctl);
		else
			ret = RECEIVE(skb);

		if (NET_RX_DROP == ret)
			priv->extra_stats.kernel_dropped++;
	}

	return 0;
}

/* gfar_clean_rx_ring() -- Processes each frame in the rx ring
 *   until the budget/quota has been reached. Returns the number
 *   of frames handled
 */
int gfar_clean_rx_ring(struct net_device *dev, int rx_work_limit)
{
	struct rxbd8 *bdp;
	struct sk_buff *skb;
	u16 pkt_len;
	int howmany = 0;
	struct gfar_private *priv = netdev_priv(dev);

	/* Get the first full descriptor */
	bdp = priv->cur_rx;

	while (!((bdp->status & RXBD_EMPTY) || (--rx_work_limit < 0))) {
		struct sk_buff *newskb;
		rmb();

		/* Add another skb for the future */
		newskb = gfar_new_skb(dev);

		skb = priv->rx_skbuff[priv->skb_currx];

		/* We drop the frame if we failed to allocate a new buffer */
		if (unlikely(!newskb || !(bdp->status & RXBD_LAST) ||
				 bdp->status & RXBD_ERR)) {
			count_errors(bdp->status, dev);

			if (unlikely(!newskb))
				newskb = skb;

			if (skb) {
				dma_unmap_single(&priv->dev->dev,
						bdp->bufPtr,
						priv->rx_buffer_size,
						DMA_FROM_DEVICE);

				dev_kfree_skb_any(skb);
			}
		} else {
			/* Increment the number of packets */
			dev->stats.rx_packets++;
			howmany++;

			/* Remove the FCS from the packet length */
			pkt_len = bdp->length - 4;

			gfar_process_frame(dev, skb, pkt_len);

			dev->stats.rx_bytes += pkt_len;
		}

		dev->last_rx = jiffies;

		priv->rx_skbuff[priv->skb_currx] = newskb;

		/* Setup the new bdp */
		gfar_new_rxbdp(dev, bdp, newskb);

		/* Update to the next pointer */
		if (bdp->status & RXBD_WRAP)
			bdp = priv->rx_bd_base;
		else
			bdp++;

		/* update to point at the next skb */
		priv->skb_currx =
		    (priv->skb_currx + 1) &
		    RX_RING_MOD_MASK(priv->rx_ring_size);
	}

	/* Update the current rxbd pointer to be the next one */
	priv->cur_rx = bdp;

	return howmany;
}

#ifdef CONFIG_GFAR_NAPI
static int gfar_poll(struct napi_struct *napi, int budget)
{
	struct gfar_private *priv = container_of(napi, struct gfar_private, napi);
	struct net_device *dev = priv->dev;
	int howmany;
	unsigned long flags;

	/* If we fail to get the lock, don't bother with the TX BDs */
	if (spin_trylock_irqsave(&priv->txlock, flags)) {
		gfar_clean_tx_ring(dev);
		spin_unlock_irqrestore(&priv->txlock, flags);
	}

	howmany = gfar_clean_rx_ring(dev, budget);

	if (howmany < budget) {
		netif_rx_complete(dev, napi);

		/* Clear the halt bit in RSTAT */
		gfar_write(&priv->regs->rstat, RSTAT_CLEAR_RHALT);

		gfar_write(&priv->regs->imask, IMASK_DEFAULT);

		/* If we are coalescing interrupts, update the timer */
		/* Otherwise, clear it */
		if (likely(priv->rxcoalescing)) {
			gfar_write(&priv->regs->rxic, 0);
			gfar_write(&priv->regs->rxic,
				   mk_ic_value(priv->rxcount, priv->rxtime));
		}
	}

	return howmany;
}
#endif

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
	if (priv->einfo->device_flags & FSL_GIANFAR_DEV_HAS_MULTI_INTR) {
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
			netif_schedule(dev);
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
	gfar_write(&priv->regs->ievent, IEVENT_ERR_MASK);

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

#ifndef CONFIG_GFAR_NAPI
		/* Clear the halt bit in RSTAT */
		gfar_write(&priv->regs->rstat, RSTAT_CLEAR_RHALT);
#endif

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

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:fsl-gianfar");

/* Structure for a device driver */
static struct platform_driver gfar_driver = {
	.probe = gfar_probe,
	.remove = gfar_remove,
	.driver	= {
		.name = "fsl-gianfar",
		.owner = THIS_MODULE,
	},
};

static int __init gfar_init(void)
{
	int err = gfar_mdio_init();

	if (err)
		return err;

	err = platform_driver_register(&gfar_driver);

	if (err)
		gfar_mdio_exit();

	return err;
}

static void __exit gfar_exit(void)
{
	platform_driver_unregister(&gfar_driver);
	gfar_mdio_exit();
}

module_init(gfar_init);
module_exit(gfar_exit);

