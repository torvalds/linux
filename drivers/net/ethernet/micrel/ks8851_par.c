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
#include <linux/iopoll.h>
#include <linux/mii.h>

#include <linux/platform_device.h>
#include <linux/of_net.h>

#include "ks8851.h"

static int msg_enable;

#define BE3             0x8000      /* Byte Enable 3 */
#define BE2             0x4000      /* Byte Enable 2 */
#define BE1             0x2000      /* Byte Enable 1 */
#define BE0             0x1000      /* Byte Enable 0 */

/**
 * struct ks8851_net_par - KS8851 Parallel driver private data
 * @ks8851: KS8851 driver common private data
 * @lock: Lock to ensure that the device is not accessed when busy.
 * @hw_addr	: start address of data register.
 * @hw_addr_cmd	: start address of command register.
 * @cmd_reg_cache	: command register cached.
 *
 * The @lock ensures that the chip is protected when certain operations are
 * in progress. When the read or write packet transfer is in progress, most
 * of the chip registers are not accessible until the transfer is finished
 * and the DMA has been de-asserted.
 */
struct ks8851_net_par {
	struct ks8851_net	ks8851;
	spinlock_t		lock;
	void __iomem		*hw_addr;
	void __iomem		*hw_addr_cmd;
	u16			cmd_reg_cache;
};

#define to_ks8851_par(ks) container_of((ks), struct ks8851_net_par, ks8851)

/**
 * ks8851_lock_par - register access lock
 * @ks: The chip state
 * @flags: Spinlock flags
 *
 * Claim chip register access lock
 */
static void ks8851_lock_par(struct ks8851_net *ks, unsigned long *flags)
{
	struct ks8851_net_par *ksp = to_ks8851_par(ks);

	spin_lock_irqsave(&ksp->lock, *flags);
}

/**
 * ks8851_unlock_par - register access unlock
 * @ks: The chip state
 * @flags: Spinlock flags
 *
 * Release chip register access lock
 */
static void ks8851_unlock_par(struct ks8851_net *ks, unsigned long *flags)
{
	struct ks8851_net_par *ksp = to_ks8851_par(ks);

	spin_unlock_irqrestore(&ksp->lock, *flags);
}

/**
 * ks_check_endian - Check whether endianness of the bus is correct
 * @ks	  : The chip information
 *
 * The KS8851-16MLL EESK pin allows selecting the endianness of the 16bit
 * bus. To maintain optimum performance, the bus endianness should be set
 * such that it matches the endianness of the CPU.
 */
static int ks_check_endian(struct ks8851_net *ks)
{
	struct ks8851_net_par *ksp = to_ks8851_par(ks);
	u16 cider;

	/*
	 * Read CIDER register first, however read it the "wrong" way around.
	 * If the endian strap on the KS8851-16MLL in incorrect and the chip
	 * is operating in different endianness than the CPU, then the meaning
	 * of BE[3:0] byte-enable bits is also swapped such that:
	 *    BE[3,2,1,0] becomes BE[1,0,3,2]
	 *
	 * Luckily for us, the byte-enable bits are the top four MSbits of
	 * the address register and the CIDER register is at offset 0xc0.
	 * Hence, by reading address 0xc0c0, which is not impacted by endian
	 * swapping, we assert either BE[3:2] or BE[1:0] while reading the
	 * CIDER register.
	 *
	 * If the bus configuration is correct, reading 0xc0c0 asserts
	 * BE[3:2] and this read returns 0x0000, because to read register
	 * with bottom two LSbits of address set to 0, BE[1:0] must be
	 * asserted.
	 *
	 * If the bus configuration is NOT correct, reading 0xc0c0 asserts
	 * BE[1:0] and this read returns non-zero 0x8872 value.
	 */
	iowrite16(BE3 | BE2 | KS_CIDER, ksp->hw_addr_cmd);
	cider = ioread16(ksp->hw_addr);
	if (!cider)
		return 0;

	netdev_err(ks->netdev, "incorrect EESK endian strap setting\n");

	return -EINVAL;
}

/**
 * ks8851_wrreg16_par - write 16bit register value to chip
 * @ks: The chip state
 * @reg: The register address
 * @val: The value to write
 *
 * Issue a write to put the value @val into the register specified in @reg.
 */
static void ks8851_wrreg16_par(struct ks8851_net *ks, unsigned int reg,
			       unsigned int val)
{
	struct ks8851_net_par *ksp = to_ks8851_par(ks);

	ksp->cmd_reg_cache = (u16)reg | ((BE1 | BE0) << (reg & 0x02));
	iowrite16(ksp->cmd_reg_cache, ksp->hw_addr_cmd);
	iowrite16(val, ksp->hw_addr);
}

/**
 * ks8851_rdreg16_par - read 16 bit register from chip
 * @ks: The chip information
 * @reg: The register address
 *
 * Read a 16bit register from the chip, returning the result
 */
static unsigned int ks8851_rdreg16_par(struct ks8851_net *ks, unsigned int reg)
{
	struct ks8851_net_par *ksp = to_ks8851_par(ks);

	ksp->cmd_reg_cache = (u16)reg | ((BE1 | BE0) << (reg & 0x02));
	iowrite16(ksp->cmd_reg_cache, ksp->hw_addr_cmd);
	return ioread16(ksp->hw_addr);
}

/**
 * ks8851_rdfifo_par - read data from the receive fifo
 * @ks: The device state.
 * @buff: The buffer address
 * @len: The length of the data to read
 *
 * Issue an RXQ FIFO read command and read the @len amount of data from
 * the FIFO into the buffer specified by @buff.
 */
static void ks8851_rdfifo_par(struct ks8851_net *ks, u8 *buff, unsigned int len)
{
	struct ks8851_net_par *ksp = to_ks8851_par(ks);

	netif_dbg(ks, rx_status, ks->netdev,
		  "%s: %d@%p\n", __func__, len, buff);

	ioread16_rep(ksp->hw_addr, (u16 *)buff + 1, len / 2);
}

/**
 * ks8851_wrfifo_par - write packet to TX FIFO
 * @ks: The device state.
 * @txp: The sk_buff to transmit.
 * @irq: IRQ on completion of the packet.
 *
 * Send the @txp to the chip. This means creating the relevant packet header
 * specifying the length of the packet and the other information the chip
 * needs, such as IRQ on completion. Send the header and the packet data to
 * the device.
 */
static void ks8851_wrfifo_par(struct ks8851_net *ks, struct sk_buff *txp,
			      bool irq)
{
	struct ks8851_net_par *ksp = to_ks8851_par(ks);
	unsigned int len = ALIGN(txp->len, 4);
	unsigned int fid = 0;

	netif_dbg(ks, tx_queued, ks->netdev, "%s: skb %p, %d@%p, irq %d\n",
		  __func__, txp, txp->len, txp->data, irq);

	fid = ks->fid++;
	fid &= TXFR_TXFID_MASK;

	if (irq)
		fid |= TXFR_TXIC;	/* irq on completion */

	iowrite16(fid, ksp->hw_addr);
	iowrite16(txp->len, ksp->hw_addr);

	iowrite16_rep(ksp->hw_addr, txp->data, len / 2);
}

/**
 * ks8851_rx_skb_par - receive skbuff
 * @ks: The device state.
 * @skb: The skbuff
 */
static void ks8851_rx_skb_par(struct ks8851_net *ks, struct sk_buff *skb)
{
	netif_rx(skb);
}

static unsigned int ks8851_rdreg16_par_txqcr(struct ks8851_net *ks)
{
	return ks8851_rdreg16_par(ks, KS_TXQCR);
}

/**
 * ks8851_start_xmit_par - transmit packet
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
static netdev_tx_t ks8851_start_xmit_par(struct sk_buff *skb,
					 struct net_device *dev)
{
	struct ks8851_net *ks = netdev_priv(dev);
	netdev_tx_t ret = NETDEV_TX_OK;
	unsigned long flags;
	unsigned int txqcr;
	u16 txmir;
	int err;

	netif_dbg(ks, tx_queued, ks->netdev,
		  "%s: skb %p, %d@%p\n", __func__, skb, skb->len, skb->data);

	ks8851_lock_par(ks, &flags);

	txmir = ks8851_rdreg16_par(ks, KS_TXMIR) & 0x1fff;

	if (likely(txmir >= skb->len + 12)) {
		ks8851_wrreg16_par(ks, KS_RXQCR, ks->rc_rxqcr | RXQCR_SDA);
		ks8851_wrfifo_par(ks, skb, false);
		ks8851_wrreg16_par(ks, KS_RXQCR, ks->rc_rxqcr);
		ks8851_wrreg16_par(ks, KS_TXQCR, TXQCR_METFE);

		err = readx_poll_timeout_atomic(ks8851_rdreg16_par_txqcr, ks,
						txqcr, !(txqcr & TXQCR_METFE),
						5, 1000000);
		if (err)
			ret = NETDEV_TX_BUSY;

		ks8851_done_tx(ks, skb);
	} else {
		ret = NETDEV_TX_BUSY;
	}

	ks8851_unlock_par(ks, &flags);

	return ret;
}

static int ks8851_probe_par(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ks8851_net_par *ksp;
	struct net_device *netdev;
	struct ks8851_net *ks;
	int ret;

	netdev = devm_alloc_etherdev(dev, sizeof(struct ks8851_net_par));
	if (!netdev)
		return -ENOMEM;

	ks = netdev_priv(netdev);

	ks->lock = ks8851_lock_par;
	ks->unlock = ks8851_unlock_par;
	ks->rdreg16 = ks8851_rdreg16_par;
	ks->wrreg16 = ks8851_wrreg16_par;
	ks->rdfifo = ks8851_rdfifo_par;
	ks->wrfifo = ks8851_wrfifo_par;
	ks->start_xmit = ks8851_start_xmit_par;
	ks->rx_skb = ks8851_rx_skb_par;

#define STD_IRQ (IRQ_LCI |	/* Link Change */	\
		 IRQ_RXI |	/* RX done */		\
		 IRQ_RXPSI)	/* RX process stop */
	ks->rc_ier = STD_IRQ;

	ksp = to_ks8851_par(ks);
	spin_lock_init(&ksp->lock);

	ksp->hw_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ksp->hw_addr))
		return PTR_ERR(ksp->hw_addr);

	ksp->hw_addr_cmd = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(ksp->hw_addr_cmd))
		return PTR_ERR(ksp->hw_addr_cmd);

	ret = ks_check_endian(ks);
	if (ret)
		return ret;

	netdev->irq = platform_get_irq(pdev, 0);
	if (netdev->irq < 0)
		return netdev->irq;

	return ks8851_probe_common(netdev, dev, msg_enable);
}

static int ks8851_remove_par(struct platform_device *pdev)
{
	return ks8851_remove_common(&pdev->dev);
}

static const struct of_device_id ks8851_match_table[] = {
	{ .compatible = "micrel,ks8851-mll" },
	{ }
};
MODULE_DEVICE_TABLE(of, ks8851_match_table);

static struct platform_driver ks8851_driver = {
	.driver = {
		.name = "ks8851",
		.of_match_table = ks8851_match_table,
		.pm = &ks8851_pm_ops,
	},
	.probe = ks8851_probe_par,
	.remove = ks8851_remove_par,
};
module_platform_driver(ks8851_driver);

MODULE_DESCRIPTION("KS8851 Network driver");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_LICENSE("GPL");

module_param_named(message, msg_enable, int, 0);
MODULE_PARM_DESC(message, "Message verbosity level (0=none, 31=all)");
