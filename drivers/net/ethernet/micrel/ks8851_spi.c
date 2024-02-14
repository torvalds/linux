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
#include <linux/regulator/consumer.h>

#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of_net.h>

#include "ks8851.h"

static int msg_enable;

/**
 * struct ks8851_net_spi - KS8851 SPI driver private data
 * @lock: Lock to ensure that the device is not accessed when busy.
 * @tx_work: Work queue for tx packets
 * @ks8851: KS8851 driver common private data
 * @spidev: The spi device we're bound to.
 * @spi_msg1: pre-setup SPI transfer with one message, @spi_xfer1.
 * @spi_msg2: pre-setup SPI transfer with two messages, @spi_xfer2.
 * @spi_xfer1: @spi_msg1 SPI transfer structure
 * @spi_xfer2: @spi_msg2 SPI transfer structure
 *
 * The @lock ensures that the chip is protected when certain operations are
 * in progress. When the read or write packet transfer is in progress, most
 * of the chip registers are not ccessible until the transfer is finished and
 * the DMA has been de-asserted.
 */
struct ks8851_net_spi {
	struct ks8851_net	ks8851;
	struct mutex		lock;
	struct work_struct	tx_work;
	struct spi_device	*spidev;
	struct spi_message	spi_msg1;
	struct spi_message	spi_msg2;
	struct spi_transfer	spi_xfer1;
	struct spi_transfer	spi_xfer2[2];
};

#define to_ks8851_spi(ks) container_of((ks), struct ks8851_net_spi, ks8851)

/* SPI frame opcodes */
#define KS_SPIOP_RD	0x00
#define KS_SPIOP_WR	0x40
#define KS_SPIOP_RXFIFO	0x80
#define KS_SPIOP_TXFIFO	0xC0

/* shift for byte-enable data */
#define BYTE_EN(_x)	((_x) << 2)

/* turn register number and byte-enable mask into data for start of packet */
#define MK_OP(_byteen, _reg)	\
	(BYTE_EN(_byteen) | (_reg) << (8 + 2) | (_reg) >> 6)

/**
 * ks8851_lock_spi - register access lock
 * @ks: The chip state
 * @flags: Spinlock flags
 *
 * Claim chip register access lock
 */
static void ks8851_lock_spi(struct ks8851_net *ks, unsigned long *flags)
{
	struct ks8851_net_spi *kss = to_ks8851_spi(ks);

	mutex_lock(&kss->lock);
}

/**
 * ks8851_unlock_spi - register access unlock
 * @ks: The chip state
 * @flags: Spinlock flags
 *
 * Release chip register access lock
 */
static void ks8851_unlock_spi(struct ks8851_net *ks, unsigned long *flags)
{
	struct ks8851_net_spi *kss = to_ks8851_spi(ks);

	mutex_unlock(&kss->lock);
}

/* SPI register read/write calls.
 *
 * All these calls issue SPI transactions to access the chip's registers. They
 * all require that the necessary lock is held to prevent accesses when the
 * chip is busy transferring packet data (RX/TX FIFO accesses).
 */

/**
 * ks8851_wrreg16_spi - write 16bit register value to chip via SPI
 * @ks: The chip state
 * @reg: The register address
 * @val: The value to write
 *
 * Issue a write to put the value @val into the register specified in @reg.
 */
static void ks8851_wrreg16_spi(struct ks8851_net *ks, unsigned int reg,
			       unsigned int val)
{
	struct ks8851_net_spi *kss = to_ks8851_spi(ks);
	struct spi_transfer *xfer = &kss->spi_xfer1;
	struct spi_message *msg = &kss->spi_msg1;
	__le16 txb[2];
	int ret;

	txb[0] = cpu_to_le16(MK_OP(reg & 2 ? 0xC : 0x03, reg) | KS_SPIOP_WR);
	txb[1] = cpu_to_le16(val);

	xfer->tx_buf = txb;
	xfer->rx_buf = NULL;
	xfer->len = 4;

	ret = spi_sync(kss->spidev, msg);
	if (ret < 0)
		netdev_err(ks->netdev, "spi_sync() failed\n");
}

/**
 * ks8851_rdreg - issue read register command and return the data
 * @ks: The device state
 * @op: The register address and byte enables in message format.
 * @rxb: The RX buffer to return the result into
 * @rxl: The length of data expected.
 *
 * This is the low level read call that issues the necessary spi message(s)
 * to read data from the register specified in @op.
 */
static void ks8851_rdreg(struct ks8851_net *ks, unsigned int op,
			 u8 *rxb, unsigned int rxl)
{
	struct ks8851_net_spi *kss = to_ks8851_spi(ks);
	struct spi_transfer *xfer;
	struct spi_message *msg;
	__le16 *txb = (__le16 *)ks->txd;
	u8 *trx = ks->rxd;
	int ret;

	txb[0] = cpu_to_le16(op | KS_SPIOP_RD);

	if (kss->spidev->master->flags & SPI_MASTER_HALF_DUPLEX) {
		msg = &kss->spi_msg2;
		xfer = kss->spi_xfer2;

		xfer->tx_buf = txb;
		xfer->rx_buf = NULL;
		xfer->len = 2;

		xfer++;
		xfer->tx_buf = NULL;
		xfer->rx_buf = trx;
		xfer->len = rxl;
	} else {
		msg = &kss->spi_msg1;
		xfer = &kss->spi_xfer1;

		xfer->tx_buf = txb;
		xfer->rx_buf = trx;
		xfer->len = rxl + 2;
	}

	ret = spi_sync(kss->spidev, msg);
	if (ret < 0)
		netdev_err(ks->netdev, "read: spi_sync() failed\n");
	else if (kss->spidev->master->flags & SPI_MASTER_HALF_DUPLEX)
		memcpy(rxb, trx, rxl);
	else
		memcpy(rxb, trx + 2, rxl);
}

/**
 * ks8851_rdreg16_spi - read 16 bit register from device via SPI
 * @ks: The chip information
 * @reg: The register address
 *
 * Read a 16bit register from the chip, returning the result
 */
static unsigned int ks8851_rdreg16_spi(struct ks8851_net *ks, unsigned int reg)
{
	__le16 rx = 0;

	ks8851_rdreg(ks, MK_OP(reg & 2 ? 0xC : 0x3, reg), (u8 *)&rx, 2);
	return le16_to_cpu(rx);
}

/**
 * ks8851_rdfifo_spi - read data from the receive fifo via SPI
 * @ks: The device state.
 * @buff: The buffer address
 * @len: The length of the data to read
 *
 * Issue an RXQ FIFO read command and read the @len amount of data from
 * the FIFO into the buffer specified by @buff.
 */
static void ks8851_rdfifo_spi(struct ks8851_net *ks, u8 *buff, unsigned int len)
{
	struct ks8851_net_spi *kss = to_ks8851_spi(ks);
	struct spi_transfer *xfer = kss->spi_xfer2;
	struct spi_message *msg = &kss->spi_msg2;
	u8 txb[1];
	int ret;

	netif_dbg(ks, rx_status, ks->netdev,
		  "%s: %d@%p\n", __func__, len, buff);

	/* set the operation we're issuing */
	txb[0] = KS_SPIOP_RXFIFO;

	xfer->tx_buf = txb;
	xfer->rx_buf = NULL;
	xfer->len = 1;

	xfer++;
	xfer->rx_buf = buff;
	xfer->tx_buf = NULL;
	xfer->len = len;

	ret = spi_sync(kss->spidev, msg);
	if (ret < 0)
		netdev_err(ks->netdev, "%s: spi_sync() failed\n", __func__);
}

/**
 * ks8851_wrfifo_spi - write packet to TX FIFO via SPI
 * @ks: The device state.
 * @txp: The sk_buff to transmit.
 * @irq: IRQ on completion of the packet.
 *
 * Send the @txp to the chip. This means creating the relevant packet header
 * specifying the length of the packet and the other information the chip
 * needs, such as IRQ on completion. Send the header and the packet data to
 * the device.
 */
static void ks8851_wrfifo_spi(struct ks8851_net *ks, struct sk_buff *txp,
			      bool irq)
{
	struct ks8851_net_spi *kss = to_ks8851_spi(ks);
	struct spi_transfer *xfer = kss->spi_xfer2;
	struct spi_message *msg = &kss->spi_msg2;
	unsigned int fid = 0;
	int ret;

	netif_dbg(ks, tx_queued, ks->netdev, "%s: skb %p, %d@%p, irq %d\n",
		  __func__, txp, txp->len, txp->data, irq);

	fid = ks->fid++;
	fid &= TXFR_TXFID_MASK;

	if (irq)
		fid |= TXFR_TXIC;	/* irq on completion */

	/* start header at txb[1] to align txw entries */
	ks->txh.txb[1] = KS_SPIOP_TXFIFO;
	ks->txh.txw[1] = cpu_to_le16(fid);
	ks->txh.txw[2] = cpu_to_le16(txp->len);

	xfer->tx_buf = &ks->txh.txb[1];
	xfer->rx_buf = NULL;
	xfer->len = 5;

	xfer++;
	xfer->tx_buf = txp->data;
	xfer->rx_buf = NULL;
	xfer->len = ALIGN(txp->len, 4);

	ret = spi_sync(kss->spidev, msg);
	if (ret < 0)
		netdev_err(ks->netdev, "%s: spi_sync() failed\n", __func__);
}

/**
 * ks8851_rx_skb_spi - receive skbuff
 * @ks: The device state
 * @skb: The skbuff
 */
static void ks8851_rx_skb_spi(struct ks8851_net *ks, struct sk_buff *skb)
{
	netif_rx(skb);
}

/**
 * ks8851_tx_work - process tx packet(s)
 * @work: The work strucutre what was scheduled.
 *
 * This is called when a number of packets have been scheduled for
 * transmission and need to be sent to the device.
 */
static void ks8851_tx_work(struct work_struct *work)
{
	struct ks8851_net_spi *kss;
	struct ks8851_net *ks;
	unsigned long flags;
	struct sk_buff *txb;
	bool last;

	kss = container_of(work, struct ks8851_net_spi, tx_work);
	ks = &kss->ks8851;
	last = skb_queue_empty(&ks->txq);

	ks8851_lock_spi(ks, &flags);

	while (!last) {
		txb = skb_dequeue(&ks->txq);
		last = skb_queue_empty(&ks->txq);

		if (txb) {
			ks8851_wrreg16_spi(ks, KS_RXQCR,
					   ks->rc_rxqcr | RXQCR_SDA);
			ks8851_wrfifo_spi(ks, txb, last);
			ks8851_wrreg16_spi(ks, KS_RXQCR, ks->rc_rxqcr);
			ks8851_wrreg16_spi(ks, KS_TXQCR, TXQCR_METFE);

			ks8851_done_tx(ks, txb);
		}
	}

	ks8851_unlock_spi(ks, &flags);
}

/**
 * ks8851_flush_tx_work_spi - flush outstanding TX work
 * @ks: The device state
 */
static void ks8851_flush_tx_work_spi(struct ks8851_net *ks)
{
	struct ks8851_net_spi *kss = to_ks8851_spi(ks);

	flush_work(&kss->tx_work);
}

/**
 * calc_txlen - calculate size of message to send packet
 * @len: Length of data
 *
 * Returns the size of the TXFIFO message needed to send
 * this packet.
 */
static unsigned int calc_txlen(unsigned int len)
{
	return ALIGN(len + 4, 4);
}

/**
 * ks8851_start_xmit_spi - transmit packet using SPI
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
static netdev_tx_t ks8851_start_xmit_spi(struct sk_buff *skb,
					 struct net_device *dev)
{
	unsigned int needed = calc_txlen(skb->len);
	struct ks8851_net *ks = netdev_priv(dev);
	netdev_tx_t ret = NETDEV_TX_OK;
	struct ks8851_net_spi *kss;

	kss = to_ks8851_spi(ks);

	netif_dbg(ks, tx_queued, ks->netdev,
		  "%s: skb %p, %d@%p\n", __func__, skb, skb->len, skb->data);

	spin_lock(&ks->statelock);

	if (needed > ks->tx_space) {
		netif_stop_queue(dev);
		ret = NETDEV_TX_BUSY;
	} else {
		ks->tx_space -= needed;
		skb_queue_tail(&ks->txq, skb);
	}

	spin_unlock(&ks->statelock);
	schedule_work(&kss->tx_work);

	return ret;
}

static int ks8851_probe_spi(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct ks8851_net_spi *kss;
	struct net_device *netdev;
	struct ks8851_net *ks;

	netdev = devm_alloc_etherdev(dev, sizeof(struct ks8851_net_spi));
	if (!netdev)
		return -ENOMEM;

	spi->bits_per_word = 8;

	kss = netdev_priv(netdev);
	ks = &kss->ks8851;

	ks->lock = ks8851_lock_spi;
	ks->unlock = ks8851_unlock_spi;
	ks->rdreg16 = ks8851_rdreg16_spi;
	ks->wrreg16 = ks8851_wrreg16_spi;
	ks->rdfifo = ks8851_rdfifo_spi;
	ks->wrfifo = ks8851_wrfifo_spi;
	ks->start_xmit = ks8851_start_xmit_spi;
	ks->rx_skb = ks8851_rx_skb_spi;
	ks->flush_tx_work = ks8851_flush_tx_work_spi;

#define STD_IRQ (IRQ_LCI |	/* Link Change */	\
		 IRQ_TXI |	/* TX done */		\
		 IRQ_RXI |	/* RX done */		\
		 IRQ_SPIBEI |	/* SPI bus error */	\
		 IRQ_TXPSI |	/* TX process stop */	\
		 IRQ_RXPSI)	/* RX process stop */
	ks->rc_ier = STD_IRQ;

	kss->spidev = spi;
	mutex_init(&kss->lock);
	INIT_WORK(&kss->tx_work, ks8851_tx_work);

	/* initialise pre-made spi transfer messages */
	spi_message_init(&kss->spi_msg1);
	spi_message_add_tail(&kss->spi_xfer1, &kss->spi_msg1);

	spi_message_init(&kss->spi_msg2);
	spi_message_add_tail(&kss->spi_xfer2[0], &kss->spi_msg2);
	spi_message_add_tail(&kss->spi_xfer2[1], &kss->spi_msg2);

	netdev->irq = spi->irq;

	return ks8851_probe_common(netdev, dev, msg_enable);
}

static void ks8851_remove_spi(struct spi_device *spi)
{
	ks8851_remove_common(&spi->dev);
}

static const struct of_device_id ks8851_match_table[] = {
	{ .compatible = "micrel,ks8851" },
	{ }
};
MODULE_DEVICE_TABLE(of, ks8851_match_table);

static struct spi_driver ks8851_driver = {
	.driver = {
		.name = "ks8851",
		.of_match_table = ks8851_match_table,
		.pm = &ks8851_pm_ops,
	},
	.probe = ks8851_probe_spi,
	.remove = ks8851_remove_spi,
};
module_spi_driver(ks8851_driver);

MODULE_DESCRIPTION("KS8851 Network driver");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_LICENSE("GPL");

module_param_named(message, msg_enable, int, 0);
MODULE_PARM_DESC(message, "Message verbosity level (0=none, 31=all)");
MODULE_ALIAS("spi:ks8851");
