// SPDX-License-Identifier: GPL-2.0-only
/* drivers/net/ethernet/micrel/ks8851.c
 *
 * Copyright 2009 Simtec Electronics
 *	http://www.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/cache.h>
#include <linux/crc32.h>
#include <linux/mii.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <linux/of_mdio.h>
#include <linux/of_net.h>

#include "ks8851.h"

/**
 * ks8851_lock - register access lock
 * @ks: The chip state
 * @flags: Spinlock flags
 *
 * Claim chip register access lock
 */
static void ks8851_lock(struct ks8851_net *ks, unsigned long *flags)
{
	ks->lock(ks, flags);
}

/**
 * ks8851_unlock - register access unlock
 * @ks: The chip state
 * @flags: Spinlock flags
 *
 * Release chip register access lock
 */
static void ks8851_unlock(struct ks8851_net *ks, unsigned long *flags)
{
	ks->unlock(ks, flags);
}

/**
 * ks8851_wrreg16 - write 16bit register value to chip
 * @ks: The chip state
 * @reg: The register address
 * @val: The value to write
 *
 * Issue a write to put the value @val into the register specified in @reg.
 */
static void ks8851_wrreg16(struct ks8851_net *ks, unsigned int reg,
			   unsigned int val)
{
	ks->wrreg16(ks, reg, val);
}

/**
 * ks8851_rdreg16 - read 16 bit register from device
 * @ks: The chip information
 * @reg: The register address
 *
 * Read a 16bit register from the chip, returning the result
 */
static unsigned int ks8851_rdreg16(struct ks8851_net *ks,
				   unsigned int reg)
{
	return ks->rdreg16(ks, reg);
}

/**
 * ks8851_soft_reset - issue one of the soft reset to the device
 * @ks: The device state.
 * @op: The bit(s) to set in the GRR
 *
 * Issue the relevant soft-reset command to the device's GRR register
 * specified by @op.
 *
 * Note, the delays are in there as a caution to ensure that the reset
 * has time to take effect and then complete. Since the datasheet does
 * not currently specify the exact sequence, we have chosen something
 * that seems to work with our device.
 */
static void ks8851_soft_reset(struct ks8851_net *ks, unsigned op)
{
	ks8851_wrreg16(ks, KS_GRR, op);
	mdelay(1);	/* wait a short time to effect reset */
	ks8851_wrreg16(ks, KS_GRR, 0);
	mdelay(1);	/* wait for condition to clear */
}

/**
 * ks8851_set_powermode - set power mode of the device
 * @ks: The device state
 * @pwrmode: The power mode value to write to KS_PMECR.
 *
 * Change the power mode of the chip.
 */
static void ks8851_set_powermode(struct ks8851_net *ks, unsigned pwrmode)
{
	unsigned pmecr;

	netif_dbg(ks, hw, ks->netdev, "setting power mode %d\n", pwrmode);

	pmecr = ks8851_rdreg16(ks, KS_PMECR);
	pmecr &= ~PMECR_PM_MASK;
	pmecr |= pwrmode;

	ks8851_wrreg16(ks, KS_PMECR, pmecr);
}

/**
 * ks8851_write_mac_addr - write mac address to device registers
 * @dev: The network device
 *
 * Update the KS8851 MAC address registers from the address in @dev.
 *
 * This call assumes that the chip is not running, so there is no need to
 * shutdown the RXQ process whilst setting this.
*/
static int ks8851_write_mac_addr(struct net_device *dev)
{
	struct ks8851_net *ks = netdev_priv(dev);
	unsigned long flags;
	u16 val;
	int i;

	ks8851_lock(ks, &flags);

	/*
	 * Wake up chip in case it was powered off when stopped; otherwise,
	 * the first write to the MAC address does not take effect.
	 */
	ks8851_set_powermode(ks, PMECR_PM_NORMAL);

	for (i = 0; i < ETH_ALEN; i += 2) {
		val = (dev->dev_addr[i] << 8) | dev->dev_addr[i + 1];
		ks8851_wrreg16(ks, KS_MAR(i), val);
	}

	if (!netif_running(dev))
		ks8851_set_powermode(ks, PMECR_PM_SOFTDOWN);

	ks8851_unlock(ks, &flags);

	return 0;
}

/**
 * ks8851_read_mac_addr - read mac address from device registers
 * @dev: The network device
 *
 * Update our copy of the KS8851 MAC address from the registers of @dev.
*/
static void ks8851_read_mac_addr(struct net_device *dev)
{
	struct ks8851_net *ks = netdev_priv(dev);
	unsigned long flags;
	u8 addr[ETH_ALEN];
	u16 reg;
	int i;

	ks8851_lock(ks, &flags);

	for (i = 0; i < ETH_ALEN; i += 2) {
		reg = ks8851_rdreg16(ks, KS_MAR(i));
		addr[i] = reg >> 8;
		addr[i + 1] = reg & 0xff;
	}
	eth_hw_addr_set(dev, addr);

	ks8851_unlock(ks, &flags);
}

/**
 * ks8851_init_mac - initialise the mac address
 * @ks: The device structure
 * @np: The device node pointer
 *
 * Get or create the initial mac address for the device and then set that
 * into the station address register. A mac address supplied in the device
 * tree takes precedence. Otherwise, if there is an EEPROM present, then
 * we try that. If no valid mac address is found we use eth_random_addr()
 * to create a new one.
 */
static void ks8851_init_mac(struct ks8851_net *ks, struct device_node *np)
{
	struct net_device *dev = ks->netdev;
	int ret;

	ret = of_get_ethdev_address(np, dev);
	if (!ret) {
		ks8851_write_mac_addr(dev);
		return;
	}

	if (ks->rc_ccr & CCR_EEPROM) {
		ks8851_read_mac_addr(dev);
		if (is_valid_ether_addr(dev->dev_addr))
			return;

		netdev_err(ks->netdev, "invalid mac address read %pM\n",
				dev->dev_addr);
	}

	eth_hw_addr_random(dev);
	ks8851_write_mac_addr(dev);
}

/**
 * ks8851_dbg_dumpkkt - dump initial packet contents to debug
 * @ks: The device state
 * @rxpkt: The data for the received packet
 *
 * Dump the initial data from the packet to dev_dbg().
 */
static void ks8851_dbg_dumpkkt(struct ks8851_net *ks, u8 *rxpkt)
{
	netdev_dbg(ks->netdev,
		   "pkt %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
		   rxpkt[4], rxpkt[5], rxpkt[6], rxpkt[7],
		   rxpkt[8], rxpkt[9], rxpkt[10], rxpkt[11],
		   rxpkt[12], rxpkt[13], rxpkt[14], rxpkt[15]);
}

/**
 * ks8851_rx_skb - receive skbuff
 * @ks: The device state.
 * @skb: The skbuff
 */
static void ks8851_rx_skb(struct ks8851_net *ks, struct sk_buff *skb)
{
	ks->rx_skb(ks, skb);
}

/**
 * ks8851_rx_pkts - receive packets from the host
 * @ks: The device information.
 *
 * This is called from the IRQ work queue when the system detects that there
 * are packets in the receive queue. Find out how many packets there are and
 * read them from the FIFO.
 */
static void ks8851_rx_pkts(struct ks8851_net *ks)
{
	struct sk_buff *skb;
	unsigned rxfc;
	unsigned rxlen;
	unsigned rxstat;
	u8 *rxpkt;

	rxfc = (ks8851_rdreg16(ks, KS_RXFCTR) >> 8) & 0xff;

	netif_dbg(ks, rx_status, ks->netdev,
		  "%s: %d packets\n", __func__, rxfc);

	/* Currently we're issuing a read per packet, but we could possibly
	 * improve the code by issuing a single read, getting the receive
	 * header, allocating the packet and then reading the packet data
	 * out in one go.
	 *
	 * This form of operation would require us to hold the SPI bus'
	 * chipselect low during the entie transaction to avoid any
	 * reset to the data stream coming from the chip.
	 */

	for (; rxfc != 0; rxfc--) {
		rxstat = ks8851_rdreg16(ks, KS_RXFHSR);
		rxlen = ks8851_rdreg16(ks, KS_RXFHBCR) & RXFHBCR_CNT_MASK;

		netif_dbg(ks, rx_status, ks->netdev,
			  "rx: stat 0x%04x, len 0x%04x\n", rxstat, rxlen);

		/* the length of the packet includes the 32bit CRC */

		/* set dma read address */
		ks8851_wrreg16(ks, KS_RXFDPR, RXFDPR_RXFPAI | 0x00);

		/* start DMA access */
		ks8851_wrreg16(ks, KS_RXQCR, ks->rc_rxqcr | RXQCR_SDA);

		if (rxlen > 4) {
			unsigned int rxalign;

			rxlen -= 4;
			rxalign = ALIGN(rxlen, 4);
			skb = netdev_alloc_skb_ip_align(ks->netdev, rxalign);
			if (skb) {

				/* 4 bytes of status header + 4 bytes of
				 * garbage: we put them before ethernet
				 * header, so that they are copied,
				 * but ignored.
				 */

				rxpkt = skb_put(skb, rxlen) - 8;

				ks->rdfifo(ks, rxpkt, rxalign + 8);

				if (netif_msg_pktdata(ks))
					ks8851_dbg_dumpkkt(ks, rxpkt);

				skb->protocol = eth_type_trans(skb, ks->netdev);
				ks8851_rx_skb(ks, skb);

				ks->netdev->stats.rx_packets++;
				ks->netdev->stats.rx_bytes += rxlen;
			}
		}

		/* end DMA access and dequeue packet */
		ks8851_wrreg16(ks, KS_RXQCR, ks->rc_rxqcr | RXQCR_RRXEF);
	}
}

/**
 * ks8851_irq - IRQ handler for dealing with interrupt requests
 * @irq: IRQ number
 * @_ks: cookie
 *
 * This handler is invoked when the IRQ line asserts to find out what happened.
 * As we cannot allow ourselves to sleep in HARDIRQ context, this handler runs
 * in thread context.
 *
 * Read the interrupt status, work out what needs to be done and then clear
 * any of the interrupts that are not needed.
 */
static irqreturn_t ks8851_irq(int irq, void *_ks)
{
	struct ks8851_net *ks = _ks;
	unsigned handled = 0;
	unsigned long flags;
	unsigned int status;

	ks8851_lock(ks, &flags);

	status = ks8851_rdreg16(ks, KS_ISR);

	netif_dbg(ks, intr, ks->netdev,
		  "%s: status 0x%04x\n", __func__, status);

	if (status & IRQ_LCI)
		handled |= IRQ_LCI;

	if (status & IRQ_LDI) {
		u16 pmecr = ks8851_rdreg16(ks, KS_PMECR);
		pmecr &= ~PMECR_WKEVT_MASK;
		ks8851_wrreg16(ks, KS_PMECR, pmecr | PMECR_WKEVT_LINK);

		handled |= IRQ_LDI;
	}

	if (status & IRQ_RXPSI)
		handled |= IRQ_RXPSI;

	if (status & IRQ_TXI) {
		handled |= IRQ_TXI;

		/* no lock here, tx queue should have been stopped */

		/* update our idea of how much tx space is available to the
		 * system */
		ks->tx_space = ks8851_rdreg16(ks, KS_TXMIR);

		netif_dbg(ks, intr, ks->netdev,
			  "%s: txspace %d\n", __func__, ks->tx_space);
	}

	if (status & IRQ_RXI)
		handled |= IRQ_RXI;

	if (status & IRQ_SPIBEI) {
		netdev_err(ks->netdev, "%s: spi bus error\n", __func__);
		handled |= IRQ_SPIBEI;
	}

	ks8851_wrreg16(ks, KS_ISR, handled);

	if (status & IRQ_RXI) {
		/* the datasheet says to disable the rx interrupt during
		 * packet read-out, however we're masking the interrupt
		 * from the device so do not bother masking just the RX
		 * from the device. */

		ks8851_rx_pkts(ks);
	}

	/* if something stopped the rx process, probably due to wanting
	 * to change the rx settings, then do something about restarting
	 * it. */
	if (status & IRQ_RXPSI) {
		struct ks8851_rxctrl *rxc = &ks->rxctrl;

		/* update the multicast hash table */
		ks8851_wrreg16(ks, KS_MAHTR0, rxc->mchash[0]);
		ks8851_wrreg16(ks, KS_MAHTR1, rxc->mchash[1]);
		ks8851_wrreg16(ks, KS_MAHTR2, rxc->mchash[2]);
		ks8851_wrreg16(ks, KS_MAHTR3, rxc->mchash[3]);

		ks8851_wrreg16(ks, KS_RXCR2, rxc->rxcr2);
		ks8851_wrreg16(ks, KS_RXCR1, rxc->rxcr1);
	}

	ks8851_unlock(ks, &flags);

	if (status & IRQ_LCI)
		mii_check_link(&ks->mii);

	if (status & IRQ_TXI)
		netif_wake_queue(ks->netdev);

	return IRQ_HANDLED;
}

/**
 * ks8851_flush_tx_work - flush outstanding TX work
 * @ks: The device state
 */
static void ks8851_flush_tx_work(struct ks8851_net *ks)
{
	if (ks->flush_tx_work)
		ks->flush_tx_work(ks);
}

/**
 * ks8851_net_open - open network device
 * @dev: The network device being opened.
 *
 * Called when the network device is marked active, such as a user executing
 * 'ifconfig up' on the device.
 */
static int ks8851_net_open(struct net_device *dev)
{
	struct ks8851_net *ks = netdev_priv(dev);
	unsigned long flags;
	int ret;

	ret = request_threaded_irq(dev->irq, NULL, ks8851_irq,
				   IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				   dev->name, ks);
	if (ret < 0) {
		netdev_err(dev, "failed to get irq\n");
		return ret;
	}

	/* lock the card, even if we may not actually be doing anything
	 * else at the moment */
	ks8851_lock(ks, &flags);

	netif_dbg(ks, ifup, ks->netdev, "opening\n");

	/* bring chip out of any power saving mode it was in */
	ks8851_set_powermode(ks, PMECR_PM_NORMAL);

	/* issue a soft reset to the RX/TX QMU to put it into a known
	 * state. */
	ks8851_soft_reset(ks, GRR_QMU);

	/* setup transmission parameters */

	ks8851_wrreg16(ks, KS_TXCR, (TXCR_TXE | /* enable transmit process */
				     TXCR_TXPE | /* pad to min length */
				     TXCR_TXCRC | /* add CRC */
				     TXCR_TXFCE)); /* enable flow control */

	/* auto-increment tx data, reset tx pointer */
	ks8851_wrreg16(ks, KS_TXFDPR, TXFDPR_TXFPAI);

	/* setup receiver control */

	ks8851_wrreg16(ks, KS_RXCR1, (RXCR1_RXPAFMA | /*  from mac filter */
				      RXCR1_RXFCE | /* enable flow control */
				      RXCR1_RXBE | /* broadcast enable */
				      RXCR1_RXUE | /* unicast enable */
				      RXCR1_RXE)); /* enable rx block */

	/* transfer entire frames out in one go */
	ks8851_wrreg16(ks, KS_RXCR2, RXCR2_SRDBL_FRAME);

	/* set receive counter timeouts */
	ks8851_wrreg16(ks, KS_RXDTTR, 1000); /* 1ms after first frame to IRQ */
	ks8851_wrreg16(ks, KS_RXDBCTR, 4096); /* >4Kbytes in buffer to IRQ */
	ks8851_wrreg16(ks, KS_RXFCTR, 10);  /* 10 frames to IRQ */

	ks->rc_rxqcr = (RXQCR_RXFCTE |  /* IRQ on frame count exceeded */
			RXQCR_RXDBCTE | /* IRQ on byte count exceeded */
			RXQCR_RXDTTE);  /* IRQ on time exceeded */

	ks8851_wrreg16(ks, KS_RXQCR, ks->rc_rxqcr);

	/* clear then enable interrupts */
	ks8851_wrreg16(ks, KS_ISR, ks->rc_ier);
	ks8851_wrreg16(ks, KS_IER, ks->rc_ier);

	netif_start_queue(ks->netdev);

	netif_dbg(ks, ifup, ks->netdev, "network device up\n");

	ks8851_unlock(ks, &flags);
	mii_check_link(&ks->mii);
	return 0;
}

/**
 * ks8851_net_stop - close network device
 * @dev: The device being closed.
 *
 * Called to close down a network device which has been active. Cancell any
 * work, shutdown the RX and TX process and then place the chip into a low
 * power state whilst it is not being used.
 */
static int ks8851_net_stop(struct net_device *dev)
{
	struct ks8851_net *ks = netdev_priv(dev);
	unsigned long flags;

	netif_info(ks, ifdown, dev, "shutting down\n");

	netif_stop_queue(dev);

	ks8851_lock(ks, &flags);
	/* turn off the IRQs and ack any outstanding */
	ks8851_wrreg16(ks, KS_IER, 0x0000);
	ks8851_wrreg16(ks, KS_ISR, 0xffff);
	ks8851_unlock(ks, &flags);

	/* stop any outstanding work */
	ks8851_flush_tx_work(ks);
	flush_work(&ks->rxctrl_work);

	ks8851_lock(ks, &flags);
	/* shutdown RX process */
	ks8851_wrreg16(ks, KS_RXCR1, 0x0000);

	/* shutdown TX process */
	ks8851_wrreg16(ks, KS_TXCR, 0x0000);

	/* set powermode to soft power down to save power */
	ks8851_set_powermode(ks, PMECR_PM_SOFTDOWN);
	ks8851_unlock(ks, &flags);

	/* ensure any queued tx buffers are dumped */
	while (!skb_queue_empty(&ks->txq)) {
		struct sk_buff *txb = skb_dequeue(&ks->txq);

		netif_dbg(ks, ifdown, ks->netdev,
			  "%s: freeing txb %p\n", __func__, txb);

		dev_kfree_skb(txb);
	}

	free_irq(dev->irq, ks);

	return 0;
}

/**
 * ks8851_start_xmit - transmit packet
 * @skb: The buffer to transmit
 * @dev: The device used to transmit the packet.
 *
 * Called by the network layer to transmit the @skb. Queue the packet for
 * the device and schedule the necessary work to transmit the packet when
 * it is free.
 *
 * We do this to firstly avoid sleeping with the network device locked,
 * and secondly so we can round up more than one packet to transmit which
 * means we can try and avoid generating too many transmit done interrupts.
 */
static netdev_tx_t ks8851_start_xmit(struct sk_buff *skb,
				     struct net_device *dev)
{
	struct ks8851_net *ks = netdev_priv(dev);

	return ks->start_xmit(skb, dev);
}

/**
 * ks8851_rxctrl_work - work handler to change rx mode
 * @work: The work structure this belongs to.
 *
 * Lock the device and issue the necessary changes to the receive mode from
 * the network device layer. This is done so that we can do this without
 * having to sleep whilst holding the network device lock.
 *
 * Since the recommendation from Micrel is that the RXQ is shutdown whilst the
 * receive parameters are programmed, we issue a write to disable the RXQ and
 * then wait for the interrupt handler to be triggered once the RXQ shutdown is
 * complete. The interrupt handler then writes the new values into the chip.
 */
static void ks8851_rxctrl_work(struct work_struct *work)
{
	struct ks8851_net *ks = container_of(work, struct ks8851_net, rxctrl_work);
	unsigned long flags;

	ks8851_lock(ks, &flags);

	/* need to shutdown RXQ before modifying filter parameters */
	ks8851_wrreg16(ks, KS_RXCR1, 0x00);

	ks8851_unlock(ks, &flags);
}

static void ks8851_set_rx_mode(struct net_device *dev)
{
	struct ks8851_net *ks = netdev_priv(dev);
	struct ks8851_rxctrl rxctrl;

	memset(&rxctrl, 0, sizeof(rxctrl));

	if (dev->flags & IFF_PROMISC) {
		/* interface to receive everything */

		rxctrl.rxcr1 = RXCR1_RXAE | RXCR1_RXINVF;
	} else if (dev->flags & IFF_ALLMULTI) {
		/* accept all multicast packets */

		rxctrl.rxcr1 = (RXCR1_RXME | RXCR1_RXAE |
				RXCR1_RXPAFMA | RXCR1_RXMAFMA);
	} else if (dev->flags & IFF_MULTICAST && !netdev_mc_empty(dev)) {
		struct netdev_hw_addr *ha;
		u32 crc;

		/* accept some multicast */

		netdev_for_each_mc_addr(ha, dev) {
			crc = ether_crc(ETH_ALEN, ha->addr);
			crc >>= (32 - 6);  /* get top six bits */

			rxctrl.mchash[crc >> 4] |= (1 << (crc & 0xf));
		}

		rxctrl.rxcr1 = RXCR1_RXME | RXCR1_RXPAFMA;
	} else {
		/* just accept broadcast / unicast */
		rxctrl.rxcr1 = RXCR1_RXPAFMA;
	}

	rxctrl.rxcr1 |= (RXCR1_RXUE | /* unicast enable */
			 RXCR1_RXBE | /* broadcast enable */
			 RXCR1_RXE | /* RX process enable */
			 RXCR1_RXFCE); /* enable flow control */

	rxctrl.rxcr2 |= RXCR2_SRDBL_FRAME;

	/* schedule work to do the actual set of the data if needed */

	spin_lock(&ks->statelock);

	if (memcmp(&rxctrl, &ks->rxctrl, sizeof(rxctrl)) != 0) {
		memcpy(&ks->rxctrl, &rxctrl, sizeof(ks->rxctrl));
		schedule_work(&ks->rxctrl_work);
	}

	spin_unlock(&ks->statelock);
}

static int ks8851_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *sa = addr;

	if (netif_running(dev))
		return -EBUSY;

	if (!is_valid_ether_addr(sa->sa_data))
		return -EADDRNOTAVAIL;

	eth_hw_addr_set(dev, sa->sa_data);
	return ks8851_write_mac_addr(dev);
}

static int ks8851_net_ioctl(struct net_device *dev, struct ifreq *req, int cmd)
{
	struct ks8851_net *ks = netdev_priv(dev);

	if (!netif_running(dev))
		return -EINVAL;

	return generic_mii_ioctl(&ks->mii, if_mii(req), cmd, NULL);
}

static const struct net_device_ops ks8851_netdev_ops = {
	.ndo_open		= ks8851_net_open,
	.ndo_stop		= ks8851_net_stop,
	.ndo_eth_ioctl		= ks8851_net_ioctl,
	.ndo_start_xmit		= ks8851_start_xmit,
	.ndo_set_mac_address	= ks8851_set_mac_address,
	.ndo_set_rx_mode	= ks8851_set_rx_mode,
	.ndo_validate_addr	= eth_validate_addr,
};

/* ethtool support */

static void ks8851_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *di)
{
	strscpy(di->driver, "KS8851", sizeof(di->driver));
	strscpy(di->version, "1.00", sizeof(di->version));
	strscpy(di->bus_info, dev_name(dev->dev.parent), sizeof(di->bus_info));
}

static u32 ks8851_get_msglevel(struct net_device *dev)
{
	struct ks8851_net *ks = netdev_priv(dev);
	return ks->msg_enable;
}

static void ks8851_set_msglevel(struct net_device *dev, u32 to)
{
	struct ks8851_net *ks = netdev_priv(dev);
	ks->msg_enable = to;
}

static int ks8851_get_link_ksettings(struct net_device *dev,
				     struct ethtool_link_ksettings *cmd)
{
	struct ks8851_net *ks = netdev_priv(dev);

	mii_ethtool_get_link_ksettings(&ks->mii, cmd);

	return 0;
}

static int ks8851_set_link_ksettings(struct net_device *dev,
				     const struct ethtool_link_ksettings *cmd)
{
	struct ks8851_net *ks = netdev_priv(dev);
	return mii_ethtool_set_link_ksettings(&ks->mii, cmd);
}

static u32 ks8851_get_link(struct net_device *dev)
{
	struct ks8851_net *ks = netdev_priv(dev);
	return mii_link_ok(&ks->mii);
}

static int ks8851_nway_reset(struct net_device *dev)
{
	struct ks8851_net *ks = netdev_priv(dev);
	return mii_nway_restart(&ks->mii);
}

/* EEPROM support */

static void ks8851_eeprom_regread(struct eeprom_93cx6 *ee)
{
	struct ks8851_net *ks = ee->data;
	unsigned val;

	val = ks8851_rdreg16(ks, KS_EEPCR);

	ee->reg_data_out = (val & EEPCR_EESB) ? 1 : 0;
	ee->reg_data_clock = (val & EEPCR_EESCK) ? 1 : 0;
	ee->reg_chip_select = (val & EEPCR_EECS) ? 1 : 0;
}

static void ks8851_eeprom_regwrite(struct eeprom_93cx6 *ee)
{
	struct ks8851_net *ks = ee->data;
	unsigned val = EEPCR_EESA;	/* default - eeprom access on */

	if (ee->drive_data)
		val |= EEPCR_EESRWA;
	if (ee->reg_data_in)
		val |= EEPCR_EEDO;
	if (ee->reg_data_clock)
		val |= EEPCR_EESCK;
	if (ee->reg_chip_select)
		val |= EEPCR_EECS;

	ks8851_wrreg16(ks, KS_EEPCR, val);
}

/**
 * ks8851_eeprom_claim - claim device EEPROM and activate the interface
 * @ks: The network device state.
 *
 * Check for the presence of an EEPROM, and then activate software access
 * to the device.
 */
static int ks8851_eeprom_claim(struct ks8851_net *ks)
{
	/* start with clock low, cs high */
	ks8851_wrreg16(ks, KS_EEPCR, EEPCR_EESA | EEPCR_EECS);
	return 0;
}

/**
 * ks8851_eeprom_release - release the EEPROM interface
 * @ks: The device state
 *
 * Release the software access to the device EEPROM
 */
static void ks8851_eeprom_release(struct ks8851_net *ks)
{
	unsigned val = ks8851_rdreg16(ks, KS_EEPCR);

	ks8851_wrreg16(ks, KS_EEPCR, val & ~EEPCR_EESA);
}

#define KS_EEPROM_MAGIC (0x00008851)

static int ks8851_set_eeprom(struct net_device *dev,
			     struct ethtool_eeprom *ee, u8 *data)
{
	struct ks8851_net *ks = netdev_priv(dev);
	int offset = ee->offset;
	unsigned long flags;
	int len = ee->len;
	u16 tmp;

	/* currently only support byte writing */
	if (len != 1)
		return -EINVAL;

	if (ee->magic != KS_EEPROM_MAGIC)
		return -EINVAL;

	if (!(ks->rc_ccr & CCR_EEPROM))
		return -ENOENT;

	ks8851_lock(ks, &flags);

	ks8851_eeprom_claim(ks);

	eeprom_93cx6_wren(&ks->eeprom, true);

	/* ethtool currently only supports writing bytes, which means
	 * we have to read/modify/write our 16bit EEPROMs */

	eeprom_93cx6_read(&ks->eeprom, offset/2, &tmp);

	if (offset & 1) {
		tmp &= 0xff;
		tmp |= *data << 8;
	} else {
		tmp &= 0xff00;
		tmp |= *data;
	}

	eeprom_93cx6_write(&ks->eeprom, offset/2, tmp);
	eeprom_93cx6_wren(&ks->eeprom, false);

	ks8851_eeprom_release(ks);
	ks8851_unlock(ks, &flags);

	return 0;
}

static int ks8851_get_eeprom(struct net_device *dev,
			     struct ethtool_eeprom *ee, u8 *data)
{
	struct ks8851_net *ks = netdev_priv(dev);
	int offset = ee->offset;
	unsigned long flags;
	int len = ee->len;

	/* must be 2 byte aligned */
	if (len & 1 || offset & 1)
		return -EINVAL;

	if (!(ks->rc_ccr & CCR_EEPROM))
		return -ENOENT;

	ks8851_lock(ks, &flags);

	ks8851_eeprom_claim(ks);

	ee->magic = KS_EEPROM_MAGIC;

	eeprom_93cx6_multiread(&ks->eeprom, offset/2, (__le16 *)data, len/2);
	ks8851_eeprom_release(ks);
	ks8851_unlock(ks, &flags);

	return 0;
}

static int ks8851_get_eeprom_len(struct net_device *dev)
{
	struct ks8851_net *ks = netdev_priv(dev);

	/* currently, we assume it is an 93C46 attached, so return 128 */
	return ks->rc_ccr & CCR_EEPROM ? 128 : 0;
}

static const struct ethtool_ops ks8851_ethtool_ops = {
	.get_drvinfo	= ks8851_get_drvinfo,
	.get_msglevel	= ks8851_get_msglevel,
	.set_msglevel	= ks8851_set_msglevel,
	.get_link	= ks8851_get_link,
	.nway_reset	= ks8851_nway_reset,
	.get_eeprom_len	= ks8851_get_eeprom_len,
	.get_eeprom	= ks8851_get_eeprom,
	.set_eeprom	= ks8851_set_eeprom,
	.get_link_ksettings = ks8851_get_link_ksettings,
	.set_link_ksettings = ks8851_set_link_ksettings,
};

/* MII interface controls */

/**
 * ks8851_phy_reg - convert MII register into a KS8851 register
 * @reg: MII register number.
 *
 * Return the KS8851 register number for the corresponding MII PHY register
 * if possible. Return zero if the MII register has no direct mapping to the
 * KS8851 register set.
 */
static int ks8851_phy_reg(int reg)
{
	switch (reg) {
	case MII_BMCR:
		return KS_P1MBCR;
	case MII_BMSR:
		return KS_P1MBSR;
	case MII_PHYSID1:
		return KS_PHY1ILR;
	case MII_PHYSID2:
		return KS_PHY1IHR;
	case MII_ADVERTISE:
		return KS_P1ANAR;
	case MII_LPA:
		return KS_P1ANLPR;
	}

	return -EOPNOTSUPP;
}

static int ks8851_phy_read_common(struct net_device *dev, int phy_addr, int reg)
{
	struct ks8851_net *ks = netdev_priv(dev);
	unsigned long flags;
	int result;
	int ksreg;

	ksreg = ks8851_phy_reg(reg);
	if (ksreg < 0)
		return ksreg;

	ks8851_lock(ks, &flags);
	result = ks8851_rdreg16(ks, ksreg);
	ks8851_unlock(ks, &flags);

	return result;
}

/**
 * ks8851_phy_read - MII interface PHY register read.
 * @dev: The network device the PHY is on.
 * @phy_addr: Address of PHY (ignored as we only have one)
 * @reg: The register to read.
 *
 * This call reads data from the PHY register specified in @reg. Since the
 * device does not support all the MII registers, the non-existent values
 * are always returned as zero.
 *
 * We return zero for unsupported registers as the MII code does not check
 * the value returned for any error status, and simply returns it to the
 * caller. The mii-tool that the driver was tested with takes any -ve error
 * as real PHY capabilities, thus displaying incorrect data to the user.
 */
static int ks8851_phy_read(struct net_device *dev, int phy_addr, int reg)
{
	int ret;

	ret = ks8851_phy_read_common(dev, phy_addr, reg);
	if (ret < 0)
		return 0x0;	/* no error return allowed, so use zero */

	return ret;
}

static void ks8851_phy_write(struct net_device *dev,
			     int phy, int reg, int value)
{
	struct ks8851_net *ks = netdev_priv(dev);
	unsigned long flags;
	int ksreg;

	ksreg = ks8851_phy_reg(reg);
	if (ksreg >= 0) {
		ks8851_lock(ks, &flags);
		ks8851_wrreg16(ks, ksreg, value);
		ks8851_unlock(ks, &flags);
	}
}

static int ks8851_mdio_read(struct mii_bus *bus, int phy_id, int reg)
{
	struct ks8851_net *ks = bus->priv;

	if (phy_id != 0)
		return -EOPNOTSUPP;

	/* KS8851 PHY ID registers are swapped in HW, swap them back. */
	if (reg == MII_PHYSID1)
		reg = MII_PHYSID2;
	else if (reg == MII_PHYSID2)
		reg = MII_PHYSID1;

	return ks8851_phy_read_common(ks->netdev, phy_id, reg);
}

static int ks8851_mdio_write(struct mii_bus *bus, int phy_id, int reg, u16 val)
{
	struct ks8851_net *ks = bus->priv;

	ks8851_phy_write(ks->netdev, phy_id, reg, val);
	return 0;
}

/**
 * ks8851_read_selftest - read the selftest memory info.
 * @ks: The device state
 *
 * Read and check the TX/RX memory selftest information.
 */
static void ks8851_read_selftest(struct ks8851_net *ks)
{
	unsigned both_done = MBIR_TXMBF | MBIR_RXMBF;
	unsigned rd;

	rd = ks8851_rdreg16(ks, KS_MBIR);

	if ((rd & both_done) != both_done) {
		netdev_warn(ks->netdev, "Memory selftest not finished\n");
		return;
	}

	if (rd & MBIR_TXMBFA)
		netdev_err(ks->netdev, "TX memory selftest fail\n");

	if (rd & MBIR_RXMBFA)
		netdev_err(ks->netdev, "RX memory selftest fail\n");
}

/* driver bus management functions */

#ifdef CONFIG_PM_SLEEP

int ks8851_suspend(struct device *dev)
{
	struct ks8851_net *ks = dev_get_drvdata(dev);
	struct net_device *netdev = ks->netdev;

	if (netif_running(netdev)) {
		netif_device_detach(netdev);
		ks8851_net_stop(netdev);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ks8851_suspend);

int ks8851_resume(struct device *dev)
{
	struct ks8851_net *ks = dev_get_drvdata(dev);
	struct net_device *netdev = ks->netdev;

	if (netif_running(netdev)) {
		ks8851_net_open(netdev);
		netif_device_attach(netdev);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ks8851_resume);
#endif

static int ks8851_register_mdiobus(struct ks8851_net *ks, struct device *dev)
{
	struct mii_bus *mii_bus;
	int ret;

	mii_bus = mdiobus_alloc();
	if (!mii_bus)
		return -ENOMEM;

	mii_bus->name = "ks8851_eth_mii";
	mii_bus->read = ks8851_mdio_read;
	mii_bus->write = ks8851_mdio_write;
	mii_bus->priv = ks;
	mii_bus->parent = dev;
	mii_bus->phy_mask = ~((u32)BIT(0));
	snprintf(mii_bus->id, MII_BUS_ID_SIZE, "%s", dev_name(dev));

	ret = mdiobus_register(mii_bus);
	if (ret)
		goto err_mdiobus_register;

	ks->mii_bus = mii_bus;

	return 0;

err_mdiobus_register:
	mdiobus_free(mii_bus);
	return ret;
}

static void ks8851_unregister_mdiobus(struct ks8851_net *ks)
{
	mdiobus_unregister(ks->mii_bus);
	mdiobus_free(ks->mii_bus);
}

int ks8851_probe_common(struct net_device *netdev, struct device *dev,
			int msg_en)
{
	struct ks8851_net *ks = netdev_priv(netdev);
	unsigned cider;
	int ret;

	ks->netdev = netdev;
	ks->tx_space = 6144;

	ks->gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	ret = PTR_ERR_OR_ZERO(ks->gpio);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "reset gpio request failed: %d\n", ret);
		return ret;
	}

	ret = gpiod_set_consumer_name(ks->gpio, "ks8851_rst_n");
	if (ret) {
		dev_err(dev, "failed to set reset gpio name: %d\n", ret);
		return ret;
	}

	ks->vdd_io = devm_regulator_get(dev, "vdd-io");
	if (IS_ERR(ks->vdd_io)) {
		ret = PTR_ERR(ks->vdd_io);
		goto err_reg_io;
	}

	ret = regulator_enable(ks->vdd_io);
	if (ret) {
		dev_err(dev, "regulator vdd_io enable fail: %d\n", ret);
		goto err_reg_io;
	}

	ks->vdd_reg = devm_regulator_get(dev, "vdd");
	if (IS_ERR(ks->vdd_reg)) {
		ret = PTR_ERR(ks->vdd_reg);
		goto err_reg;
	}

	ret = regulator_enable(ks->vdd_reg);
	if (ret) {
		dev_err(dev, "regulator vdd enable fail: %d\n", ret);
		goto err_reg;
	}

	if (ks->gpio) {
		usleep_range(10000, 11000);
		gpiod_set_value_cansleep(ks->gpio, 0);
	}

	spin_lock_init(&ks->statelock);

	INIT_WORK(&ks->rxctrl_work, ks8851_rxctrl_work);

	SET_NETDEV_DEV(netdev, dev);

	/* setup EEPROM state */
	ks->eeprom.data = ks;
	ks->eeprom.width = PCI_EEPROM_WIDTH_93C46;
	ks->eeprom.register_read = ks8851_eeprom_regread;
	ks->eeprom.register_write = ks8851_eeprom_regwrite;

	/* setup mii state */
	ks->mii.dev		= netdev;
	ks->mii.phy_id		= 1;
	ks->mii.phy_id_mask	= 1;
	ks->mii.reg_num_mask	= 0xf;
	ks->mii.mdio_read	= ks8851_phy_read;
	ks->mii.mdio_write	= ks8851_phy_write;

	dev_info(dev, "message enable is %d\n", msg_en);

	ret = ks8851_register_mdiobus(ks, dev);
	if (ret)
		goto err_mdio;

	/* set the default message enable */
	ks->msg_enable = netif_msg_init(msg_en, NETIF_MSG_DRV |
						NETIF_MSG_PROBE |
						NETIF_MSG_LINK);

	skb_queue_head_init(&ks->txq);

	netdev->ethtool_ops = &ks8851_ethtool_ops;

	dev_set_drvdata(dev, ks);

	netif_carrier_off(ks->netdev);
	netdev->if_port = IF_PORT_100BASET;
	netdev->netdev_ops = &ks8851_netdev_ops;

	/* issue a global soft reset to reset the device. */
	ks8851_soft_reset(ks, GRR_GSR);

	/* simple check for a valid chip being connected to the bus */
	cider = ks8851_rdreg16(ks, KS_CIDER);
	if ((cider & ~CIDER_REV_MASK) != CIDER_ID) {
		dev_err(dev, "failed to read device ID\n");
		ret = -ENODEV;
		goto err_id;
	}

	/* cache the contents of the CCR register for EEPROM, etc. */
	ks->rc_ccr = ks8851_rdreg16(ks, KS_CCR);

	ks8851_read_selftest(ks);
	ks8851_init_mac(ks, dev->of_node);

	ret = register_netdev(netdev);
	if (ret) {
		dev_err(dev, "failed to register network device\n");
		goto err_id;
	}

	netdev_info(netdev, "revision %d, MAC %pM, IRQ %d, %s EEPROM\n",
		    CIDER_REV_GET(cider), netdev->dev_addr, netdev->irq,
		    ks->rc_ccr & CCR_EEPROM ? "has" : "no");

	return 0;

err_id:
	ks8851_unregister_mdiobus(ks);
err_mdio:
	if (ks->gpio)
		gpiod_set_value_cansleep(ks->gpio, 1);
	regulator_disable(ks->vdd_reg);
err_reg:
	regulator_disable(ks->vdd_io);
err_reg_io:
	return ret;
}
EXPORT_SYMBOL_GPL(ks8851_probe_common);

void ks8851_remove_common(struct device *dev)
{
	struct ks8851_net *priv = dev_get_drvdata(dev);

	ks8851_unregister_mdiobus(priv);

	if (netif_msg_drv(priv))
		dev_info(dev, "remove\n");

	unregister_netdev(priv->netdev);
	if (priv->gpio)
		gpiod_set_value_cansleep(priv->gpio, 1);
	regulator_disable(priv->vdd_reg);
	regulator_disable(priv->vdd_io);
}
EXPORT_SYMBOL_GPL(ks8851_remove_common);

MODULE_DESCRIPTION("KS8851 Network driver");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_LICENSE("GPL");
