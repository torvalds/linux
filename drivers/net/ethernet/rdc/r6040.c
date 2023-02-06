// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RDC R6040 Fast Ethernet MAC support
 *
 * Copyright (C) 2004 Sten Wang <sten.wang@rdc.com.tw>
 * Copyright (C) 2007
 *	Daniel Gimpelevich <daniel@gimpelevich.san-francisco.ca.us>
 * Copyright (C) 2007-2012 Florian Fainelli <f.fainelli@gmail.com>
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/crc32.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/uaccess.h>
#include <linux/phy.h>

#include <asm/processor.h>

#define DRV_NAME	"r6040"
#define DRV_VERSION	"0.29"
#define DRV_RELDATE	"04Jul2016"

/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT	(6000 * HZ / 1000)

/* RDC MAC I/O Size */
#define R6040_IO_SIZE	256

/* MAX RDC MAC */
#define MAX_MAC		2

/* MAC registers */
#define MCR0		0x00	/* Control register 0 */
#define  MCR0_RCVEN	0x0002	/* Receive enable */
#define  MCR0_PROMISC	0x0020	/* Promiscuous mode */
#define  MCR0_HASH_EN	0x0100	/* Enable multicast hash table function */
#define  MCR0_XMTEN	0x1000	/* Transmission enable */
#define  MCR0_FD	0x8000	/* Full/Half duplex */
#define MCR1		0x04	/* Control register 1 */
#define  MAC_RST	0x0001	/* Reset the MAC */
#define MBCR		0x08	/* Bus control */
#define MT_ICR		0x0C	/* TX interrupt control */
#define MR_ICR		0x10	/* RX interrupt control */
#define MTPR		0x14	/* TX poll command register */
#define  TM2TX		0x0001	/* Trigger MAC to transmit */
#define MR_BSR		0x18	/* RX buffer size */
#define MR_DCR		0x1A	/* RX descriptor control */
#define MLSR		0x1C	/* Last status */
#define  TX_FIFO_UNDR	0x0200	/* TX FIFO under-run */
#define	 TX_EXCEEDC	0x2000	/* Transmit exceed collision */
#define  TX_LATEC	0x4000	/* Transmit late collision */
#define MMDIO		0x20	/* MDIO control register */
#define  MDIO_WRITE	0x4000	/* MDIO write */
#define  MDIO_READ	0x2000	/* MDIO read */
#define MMRD		0x24	/* MDIO read data register */
#define MMWD		0x28	/* MDIO write data register */
#define MTD_SA0		0x2C	/* TX descriptor start address 0 */
#define MTD_SA1		0x30	/* TX descriptor start address 1 */
#define MRD_SA0		0x34	/* RX descriptor start address 0 */
#define MRD_SA1		0x38	/* RX descriptor start address 1 */
#define MISR		0x3C	/* Status register */
#define MIER		0x40	/* INT enable register */
#define  MSK_INT	0x0000	/* Mask off interrupts */
#define  RX_FINISH	0x0001  /* RX finished */
#define  RX_NO_DESC	0x0002  /* No RX descriptor available */
#define  RX_FIFO_FULL	0x0004  /* RX FIFO full */
#define  RX_EARLY	0x0008  /* RX early */
#define  TX_FINISH	0x0010  /* TX finished */
#define  TX_EARLY	0x0080  /* TX early */
#define  EVENT_OVRFL	0x0100  /* Event counter overflow */
#define  LINK_CHANGED	0x0200  /* PHY link changed */
#define ME_CISR		0x44	/* Event counter INT status */
#define ME_CIER		0x48	/* Event counter INT enable  */
#define MR_CNT		0x50	/* Successfully received packet counter */
#define ME_CNT0		0x52	/* Event counter 0 */
#define ME_CNT1		0x54	/* Event counter 1 */
#define ME_CNT2		0x56	/* Event counter 2 */
#define ME_CNT3		0x58	/* Event counter 3 */
#define MT_CNT		0x5A	/* Successfully transmit packet counter */
#define ME_CNT4		0x5C	/* Event counter 4 */
#define MP_CNT		0x5E	/* Pause frame counter register */
#define MAR0		0x60	/* Hash table 0 */
#define MAR1		0x62	/* Hash table 1 */
#define MAR2		0x64	/* Hash table 2 */
#define MAR3		0x66	/* Hash table 3 */
#define MID_0L		0x68	/* Multicast address MID0 Low */
#define MID_0M		0x6A	/* Multicast address MID0 Medium */
#define MID_0H		0x6C	/* Multicast address MID0 High */
#define MID_1L		0x70	/* MID1 Low */
#define MID_1M		0x72	/* MID1 Medium */
#define MID_1H		0x74	/* MID1 High */
#define MID_2L		0x78	/* MID2 Low */
#define MID_2M		0x7A	/* MID2 Medium */
#define MID_2H		0x7C	/* MID2 High */
#define MID_3L		0x80	/* MID3 Low */
#define MID_3M		0x82	/* MID3 Medium */
#define MID_3H		0x84	/* MID3 High */
#define PHY_CC		0x88	/* PHY status change configuration register */
#define  SCEN		0x8000	/* PHY status change enable */
#define  PHYAD_SHIFT	8	/* PHY address shift */
#define  TMRDIV_SHIFT	0	/* Timer divider shift */
#define PHY_ST		0x8A	/* PHY status register */
#define MAC_SM		0xAC	/* MAC status machine */
#define  MAC_SM_RST	0x0002	/* MAC status machine reset */
#define MD_CSC		0xb6	/* MDC speed control register */
#define  MD_CSC_DEFAULT	0x0030
#define MAC_ID		0xBE	/* Identifier register */

#define TX_DCNT		0x80	/* TX descriptor count */
#define RX_DCNT		0x80	/* RX descriptor count */
#define MAX_BUF_SIZE	0x600
#define RX_DESC_SIZE	(RX_DCNT * sizeof(struct r6040_descriptor))
#define TX_DESC_SIZE	(TX_DCNT * sizeof(struct r6040_descriptor))
#define MBCR_DEFAULT	0x012A	/* MAC Bus Control Register */
#define MCAST_MAX	3	/* Max number multicast addresses to filter */

#define MAC_DEF_TIMEOUT	2048	/* Default MAC read/write operation timeout */

/* Descriptor status */
#define DSC_OWNER_MAC	0x8000	/* MAC is the owner of this descriptor */
#define DSC_RX_OK	0x4000	/* RX was successful */
#define DSC_RX_ERR	0x0800	/* RX PHY error */
#define DSC_RX_ERR_DRI	0x0400	/* RX dribble packet */
#define DSC_RX_ERR_BUF	0x0200	/* RX length exceeds buffer size */
#define DSC_RX_ERR_LONG	0x0100	/* RX length > maximum packet length */
#define DSC_RX_ERR_RUNT	0x0080	/* RX packet length < 64 byte */
#define DSC_RX_ERR_CRC	0x0040	/* RX CRC error */
#define DSC_RX_BCAST	0x0020	/* RX broadcast (no error) */
#define DSC_RX_MCAST	0x0010	/* RX multicast (no error) */
#define DSC_RX_MCH_HIT	0x0008	/* RX multicast hit in hash table (no error) */
#define DSC_RX_MIDH_HIT	0x0004	/* RX MID table hit (no error) */
#define DSC_RX_IDX_MID_MASK 3	/* RX mask for the index of matched MIDx */

MODULE_AUTHOR("Sten Wang <sten.wang@rdc.com.tw>,"
	"Daniel Gimpelevich <daniel@gimpelevich.san-francisco.ca.us>,"
	"Florian Fainelli <f.fainelli@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RDC R6040 NAPI PCI FastEthernet driver");
MODULE_VERSION(DRV_VERSION " " DRV_RELDATE);

/* RX and TX interrupts that we handle */
#define RX_INTS			(RX_FIFO_FULL | RX_NO_DESC | RX_FINISH)
#define TX_INTS			(TX_FINISH)
#define INT_MASK		(RX_INTS | TX_INTS)

struct r6040_descriptor {
	u16	status, len;		/* 0-3 */
	__le32	buf;			/* 4-7 */
	__le32	ndesc;			/* 8-B */
	u32	rev1;			/* C-F */
	char	*vbufp;			/* 10-13 */
	struct r6040_descriptor *vndescp;	/* 14-17 */
	struct sk_buff *skb_ptr;	/* 18-1B */
	u32	rev2;			/* 1C-1F */
} __aligned(32);

struct r6040_private {
	spinlock_t lock;		/* driver lock */
	struct pci_dev *pdev;
	struct r6040_descriptor *rx_insert_ptr;
	struct r6040_descriptor *rx_remove_ptr;
	struct r6040_descriptor *tx_insert_ptr;
	struct r6040_descriptor *tx_remove_ptr;
	struct r6040_descriptor *rx_ring;
	struct r6040_descriptor *tx_ring;
	dma_addr_t rx_ring_dma;
	dma_addr_t tx_ring_dma;
	u16	tx_free_desc;
	u16	mcr0;
	struct net_device *dev;
	struct mii_bus *mii_bus;
	struct napi_struct napi;
	void __iomem *base;
	int old_link;
	int old_duplex;
};

static char version[] = DRV_NAME
	": RDC R6040 NAPI net driver,"
	"version "DRV_VERSION " (" DRV_RELDATE ")";

/* Read a word data from PHY Chip */
static int r6040_phy_read(void __iomem *ioaddr, int phy_addr, int reg)
{
	int limit = MAC_DEF_TIMEOUT;
	u16 cmd;

	iowrite16(MDIO_READ + reg + (phy_addr << 8), ioaddr + MMDIO);
	/* Wait for the read bit to be cleared */
	while (limit--) {
		cmd = ioread16(ioaddr + MMDIO);
		if (!(cmd & MDIO_READ))
			break;
		udelay(1);
	}

	if (limit < 0)
		return -ETIMEDOUT;

	return ioread16(ioaddr + MMRD);
}

/* Write a word data from PHY Chip */
static int r6040_phy_write(void __iomem *ioaddr,
					int phy_addr, int reg, u16 val)
{
	int limit = MAC_DEF_TIMEOUT;
	u16 cmd;

	iowrite16(val, ioaddr + MMWD);
	/* Write the command to the MDIO bus */
	iowrite16(MDIO_WRITE + reg + (phy_addr << 8), ioaddr + MMDIO);
	/* Wait for the write bit to be cleared */
	while (limit--) {
		cmd = ioread16(ioaddr + MMDIO);
		if (!(cmd & MDIO_WRITE))
			break;
		udelay(1);
	}

	return (limit < 0) ? -ETIMEDOUT : 0;
}

static int r6040_mdiobus_read(struct mii_bus *bus, int phy_addr, int reg)
{
	struct net_device *dev = bus->priv;
	struct r6040_private *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;

	return r6040_phy_read(ioaddr, phy_addr, reg);
}

static int r6040_mdiobus_write(struct mii_bus *bus, int phy_addr,
						int reg, u16 value)
{
	struct net_device *dev = bus->priv;
	struct r6040_private *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;

	return r6040_phy_write(ioaddr, phy_addr, reg, value);
}

static void r6040_free_txbufs(struct net_device *dev)
{
	struct r6040_private *lp = netdev_priv(dev);
	int i;

	for (i = 0; i < TX_DCNT; i++) {
		if (lp->tx_insert_ptr->skb_ptr) {
			dma_unmap_single(&lp->pdev->dev,
					 le32_to_cpu(lp->tx_insert_ptr->buf),
					 MAX_BUF_SIZE, DMA_TO_DEVICE);
			dev_kfree_skb(lp->tx_insert_ptr->skb_ptr);
			lp->tx_insert_ptr->skb_ptr = NULL;
		}
		lp->tx_insert_ptr = lp->tx_insert_ptr->vndescp;
	}
}

static void r6040_free_rxbufs(struct net_device *dev)
{
	struct r6040_private *lp = netdev_priv(dev);
	int i;

	for (i = 0; i < RX_DCNT; i++) {
		if (lp->rx_insert_ptr->skb_ptr) {
			dma_unmap_single(&lp->pdev->dev,
					 le32_to_cpu(lp->rx_insert_ptr->buf),
					 MAX_BUF_SIZE, DMA_FROM_DEVICE);
			dev_kfree_skb(lp->rx_insert_ptr->skb_ptr);
			lp->rx_insert_ptr->skb_ptr = NULL;
		}
		lp->rx_insert_ptr = lp->rx_insert_ptr->vndescp;
	}
}

static void r6040_init_ring_desc(struct r6040_descriptor *desc_ring,
				 dma_addr_t desc_dma, int size)
{
	struct r6040_descriptor *desc = desc_ring;
	dma_addr_t mapping = desc_dma;

	while (size-- > 0) {
		mapping += sizeof(*desc);
		desc->ndesc = cpu_to_le32(mapping);
		desc->vndescp = desc + 1;
		desc++;
	}
	desc--;
	desc->ndesc = cpu_to_le32(desc_dma);
	desc->vndescp = desc_ring;
}

static void r6040_init_txbufs(struct net_device *dev)
{
	struct r6040_private *lp = netdev_priv(dev);

	lp->tx_free_desc = TX_DCNT;

	lp->tx_remove_ptr = lp->tx_insert_ptr = lp->tx_ring;
	r6040_init_ring_desc(lp->tx_ring, lp->tx_ring_dma, TX_DCNT);
}

static int r6040_alloc_rxbufs(struct net_device *dev)
{
	struct r6040_private *lp = netdev_priv(dev);
	struct r6040_descriptor *desc;
	struct sk_buff *skb;
	int rc;

	lp->rx_remove_ptr = lp->rx_insert_ptr = lp->rx_ring;
	r6040_init_ring_desc(lp->rx_ring, lp->rx_ring_dma, RX_DCNT);

	/* Allocate skbs for the rx descriptors */
	desc = lp->rx_ring;
	do {
		skb = netdev_alloc_skb(dev, MAX_BUF_SIZE);
		if (!skb) {
			rc = -ENOMEM;
			goto err_exit;
		}
		desc->skb_ptr = skb;
		desc->buf = cpu_to_le32(dma_map_single(&lp->pdev->dev,
						       desc->skb_ptr->data,
						       MAX_BUF_SIZE,
						       DMA_FROM_DEVICE));
		desc->status = DSC_OWNER_MAC;
		desc = desc->vndescp;
	} while (desc != lp->rx_ring);

	return 0;

err_exit:
	/* Deallocate all previously allocated skbs */
	r6040_free_rxbufs(dev);
	return rc;
}

static void r6040_reset_mac(struct r6040_private *lp)
{
	void __iomem *ioaddr = lp->base;
	int limit = MAC_DEF_TIMEOUT;
	u16 cmd, md_csc;

	md_csc = ioread16(ioaddr + MD_CSC);
	iowrite16(MAC_RST, ioaddr + MCR1);
	while (limit--) {
		cmd = ioread16(ioaddr + MCR1);
		if (cmd & MAC_RST)
			break;
	}

	/* Reset internal state machine */
	iowrite16(MAC_SM_RST, ioaddr + MAC_SM);
	iowrite16(0, ioaddr + MAC_SM);
	mdelay(5);

	/* Restore MDIO clock frequency */
	if (md_csc != MD_CSC_DEFAULT)
		iowrite16(md_csc, ioaddr + MD_CSC);
}

static void r6040_init_mac_regs(struct net_device *dev)
{
	struct r6040_private *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;

	/* Mask Off Interrupt */
	iowrite16(MSK_INT, ioaddr + MIER);

	/* Reset RDC MAC */
	r6040_reset_mac(lp);

	/* MAC Bus Control Register */
	iowrite16(MBCR_DEFAULT, ioaddr + MBCR);

	/* Buffer Size Register */
	iowrite16(MAX_BUF_SIZE, ioaddr + MR_BSR);

	/* Write TX ring start address */
	iowrite16(lp->tx_ring_dma, ioaddr + MTD_SA0);
	iowrite16(lp->tx_ring_dma >> 16, ioaddr + MTD_SA1);

	/* Write RX ring start address */
	iowrite16(lp->rx_ring_dma, ioaddr + MRD_SA0);
	iowrite16(lp->rx_ring_dma >> 16, ioaddr + MRD_SA1);

	/* Set interrupt waiting time and packet numbers */
	iowrite16(0, ioaddr + MT_ICR);
	iowrite16(0, ioaddr + MR_ICR);

	/* Enable interrupts */
	iowrite16(INT_MASK, ioaddr + MIER);

	/* Enable TX and RX */
	iowrite16(lp->mcr0 | MCR0_RCVEN, ioaddr);

	/* Let TX poll the descriptors
	 * we may got called by r6040_tx_timeout which has left
	 * some unsent tx buffers */
	iowrite16(TM2TX, ioaddr + MTPR);
}

static void r6040_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct r6040_private *priv = netdev_priv(dev);
	void __iomem *ioaddr = priv->base;

	netdev_warn(dev, "transmit timed out, int enable %4.4x "
		"status %4.4x\n",
		ioread16(ioaddr + MIER),
		ioread16(ioaddr + MISR));

	dev->stats.tx_errors++;

	/* Reset MAC and re-init all registers */
	r6040_init_mac_regs(dev);
}

static struct net_device_stats *r6040_get_stats(struct net_device *dev)
{
	struct r6040_private *priv = netdev_priv(dev);
	void __iomem *ioaddr = priv->base;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	dev->stats.rx_crc_errors += ioread8(ioaddr + ME_CNT1);
	dev->stats.multicast += ioread8(ioaddr + ME_CNT0);
	spin_unlock_irqrestore(&priv->lock, flags);

	return &dev->stats;
}

/* Stop RDC MAC and Free the allocated resource */
static void r6040_down(struct net_device *dev)
{
	struct r6040_private *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;
	u16 *adrp;

	/* Stop MAC */
	iowrite16(MSK_INT, ioaddr + MIER);	/* Mask Off Interrupt */

	/* Reset RDC MAC */
	r6040_reset_mac(lp);

	/* Restore MAC Address to MIDx */
	adrp = (u16 *) dev->dev_addr;
	iowrite16(adrp[0], ioaddr + MID_0L);
	iowrite16(adrp[1], ioaddr + MID_0M);
	iowrite16(adrp[2], ioaddr + MID_0H);
}

static int r6040_close(struct net_device *dev)
{
	struct r6040_private *lp = netdev_priv(dev);
	struct pci_dev *pdev = lp->pdev;

	phy_stop(dev->phydev);
	napi_disable(&lp->napi);
	netif_stop_queue(dev);

	spin_lock_irq(&lp->lock);
	r6040_down(dev);

	/* Free RX buffer */
	r6040_free_rxbufs(dev);

	/* Free TX buffer */
	r6040_free_txbufs(dev);

	spin_unlock_irq(&lp->lock);

	free_irq(dev->irq, dev);

	/* Free Descriptor memory */
	if (lp->rx_ring) {
		dma_free_coherent(&pdev->dev, RX_DESC_SIZE, lp->rx_ring,
				  lp->rx_ring_dma);
		lp->rx_ring = NULL;
	}

	if (lp->tx_ring) {
		dma_free_coherent(&pdev->dev, TX_DESC_SIZE, lp->tx_ring,
				  lp->tx_ring_dma);
		lp->tx_ring = NULL;
	}

	return 0;
}

static int r6040_rx(struct net_device *dev, int limit)
{
	struct r6040_private *priv = netdev_priv(dev);
	struct r6040_descriptor *descptr = priv->rx_remove_ptr;
	struct sk_buff *skb_ptr, *new_skb;
	int count = 0;
	u16 err;

	/* Limit not reached and the descriptor belongs to the CPU */
	while (count < limit && !(descptr->status & DSC_OWNER_MAC)) {
		/* Read the descriptor status */
		err = descptr->status;
		/* Global error status set */
		if (err & DSC_RX_ERR) {
			/* RX dribble */
			if (err & DSC_RX_ERR_DRI)
				dev->stats.rx_frame_errors++;
			/* Buffer length exceeded */
			if (err & DSC_RX_ERR_BUF)
				dev->stats.rx_length_errors++;
			/* Packet too long */
			if (err & DSC_RX_ERR_LONG)
				dev->stats.rx_length_errors++;
			/* Packet < 64 bytes */
			if (err & DSC_RX_ERR_RUNT)
				dev->stats.rx_length_errors++;
			/* CRC error */
			if (err & DSC_RX_ERR_CRC) {
				spin_lock(&priv->lock);
				dev->stats.rx_crc_errors++;
				spin_unlock(&priv->lock);
			}
			goto next_descr;
		}

		/* Packet successfully received */
		new_skb = netdev_alloc_skb(dev, MAX_BUF_SIZE);
		if (!new_skb) {
			dev->stats.rx_dropped++;
			goto next_descr;
		}
		skb_ptr = descptr->skb_ptr;
		skb_ptr->dev = priv->dev;

		/* Do not count the CRC */
		skb_put(skb_ptr, descptr->len - 4);
		dma_unmap_single(&priv->pdev->dev, le32_to_cpu(descptr->buf),
				 MAX_BUF_SIZE, DMA_FROM_DEVICE);
		skb_ptr->protocol = eth_type_trans(skb_ptr, priv->dev);

		/* Send to upper layer */
		netif_receive_skb(skb_ptr);
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += descptr->len - 4;

		/* put new skb into descriptor */
		descptr->skb_ptr = new_skb;
		descptr->buf = cpu_to_le32(dma_map_single(&priv->pdev->dev,
							  descptr->skb_ptr->data,
							  MAX_BUF_SIZE,
							  DMA_FROM_DEVICE));

next_descr:
		/* put the descriptor back to the MAC */
		descptr->status = DSC_OWNER_MAC;
		descptr = descptr->vndescp;
		count++;
	}
	priv->rx_remove_ptr = descptr;

	return count;
}

static void r6040_tx(struct net_device *dev)
{
	struct r6040_private *priv = netdev_priv(dev);
	struct r6040_descriptor *descptr;
	void __iomem *ioaddr = priv->base;
	struct sk_buff *skb_ptr;
	u16 err;

	spin_lock(&priv->lock);
	descptr = priv->tx_remove_ptr;
	while (priv->tx_free_desc < TX_DCNT) {
		/* Check for errors */
		err = ioread16(ioaddr + MLSR);

		if (err & TX_FIFO_UNDR)
			dev->stats.tx_fifo_errors++;
		if (err & (TX_EXCEEDC | TX_LATEC))
			dev->stats.tx_carrier_errors++;

		if (descptr->status & DSC_OWNER_MAC)
			break; /* Not complete */
		skb_ptr = descptr->skb_ptr;

		/* Statistic Counter */
		dev->stats.tx_packets++;
		dev->stats.tx_bytes += skb_ptr->len;

		dma_unmap_single(&priv->pdev->dev, le32_to_cpu(descptr->buf),
				 skb_ptr->len, DMA_TO_DEVICE);
		/* Free buffer */
		dev_kfree_skb(skb_ptr);
		descptr->skb_ptr = NULL;
		/* To next descriptor */
		descptr = descptr->vndescp;
		priv->tx_free_desc++;
	}
	priv->tx_remove_ptr = descptr;

	if (priv->tx_free_desc)
		netif_wake_queue(dev);
	spin_unlock(&priv->lock);
}

static int r6040_poll(struct napi_struct *napi, int budget)
{
	struct r6040_private *priv =
		container_of(napi, struct r6040_private, napi);
	struct net_device *dev = priv->dev;
	void __iomem *ioaddr = priv->base;
	int work_done;

	r6040_tx(dev);

	work_done = r6040_rx(dev, budget);

	if (work_done < budget) {
		napi_complete_done(napi, work_done);
		/* Enable RX/TX interrupt */
		iowrite16(ioread16(ioaddr + MIER) | RX_INTS | TX_INTS,
			  ioaddr + MIER);
	}
	return work_done;
}

/* The RDC interrupt handler. */
static irqreturn_t r6040_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct r6040_private *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;
	u16 misr, status;

	/* Save MIER */
	misr = ioread16(ioaddr + MIER);
	/* Mask off RDC MAC interrupt */
	iowrite16(MSK_INT, ioaddr + MIER);
	/* Read MISR status and clear */
	status = ioread16(ioaddr + MISR);

	if (status == 0x0000 || status == 0xffff) {
		/* Restore RDC MAC interrupt */
		iowrite16(misr, ioaddr + MIER);
		return IRQ_NONE;
	}

	/* RX interrupt request */
	if (status & (RX_INTS | TX_INTS)) {
		if (status & RX_NO_DESC) {
			/* RX descriptor unavailable */
			dev->stats.rx_dropped++;
			dev->stats.rx_missed_errors++;
		}
		if (status & RX_FIFO_FULL)
			dev->stats.rx_fifo_errors++;

		if (likely(napi_schedule_prep(&lp->napi))) {
			/* Mask off RX interrupt */
			misr &= ~(RX_INTS | TX_INTS);
			__napi_schedule_irqoff(&lp->napi);
		}
	}

	/* Restore RDC MAC interrupt */
	iowrite16(misr, ioaddr + MIER);

	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void r6040_poll_controller(struct net_device *dev)
{
	disable_irq(dev->irq);
	r6040_interrupt(dev->irq, dev);
	enable_irq(dev->irq);
}
#endif

/* Init RDC MAC */
static int r6040_up(struct net_device *dev)
{
	struct r6040_private *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;
	int ret;

	/* Initialise and alloc RX/TX buffers */
	r6040_init_txbufs(dev);
	ret = r6040_alloc_rxbufs(dev);
	if (ret)
		return ret;

	/* improve performance (by RDC guys) */
	r6040_phy_write(ioaddr, 30, 17,
			(r6040_phy_read(ioaddr, 30, 17) | 0x4000));
	r6040_phy_write(ioaddr, 30, 17,
			~((~r6040_phy_read(ioaddr, 30, 17)) | 0x2000));
	r6040_phy_write(ioaddr, 0, 19, 0x0000);
	r6040_phy_write(ioaddr, 0, 30, 0x01F0);

	/* Initialize all MAC registers */
	r6040_init_mac_regs(dev);

	phy_start(dev->phydev);

	return 0;
}


/* Read/set MAC address routines */
static void r6040_mac_address(struct net_device *dev)
{
	struct r6040_private *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;
	u16 *adrp;

	/* Reset MAC */
	r6040_reset_mac(lp);

	/* Restore MAC Address */
	adrp = (u16 *) dev->dev_addr;
	iowrite16(adrp[0], ioaddr + MID_0L);
	iowrite16(adrp[1], ioaddr + MID_0M);
	iowrite16(adrp[2], ioaddr + MID_0H);
}

static int r6040_open(struct net_device *dev)
{
	struct r6040_private *lp = netdev_priv(dev);
	int ret;

	/* Request IRQ and Register interrupt handler */
	ret = request_irq(dev->irq, r6040_interrupt,
		IRQF_SHARED, dev->name, dev);
	if (ret)
		goto out;

	/* Set MAC address */
	r6040_mac_address(dev);

	/* Allocate Descriptor memory */
	lp->rx_ring =
		dma_alloc_coherent(&lp->pdev->dev, RX_DESC_SIZE,
				   &lp->rx_ring_dma, GFP_KERNEL);
	if (!lp->rx_ring) {
		ret = -ENOMEM;
		goto err_free_irq;
	}

	lp->tx_ring =
		dma_alloc_coherent(&lp->pdev->dev, TX_DESC_SIZE,
				   &lp->tx_ring_dma, GFP_KERNEL);
	if (!lp->tx_ring) {
		ret = -ENOMEM;
		goto err_free_rx_ring;
	}

	ret = r6040_up(dev);
	if (ret)
		goto err_free_tx_ring;

	napi_enable(&lp->napi);
	netif_start_queue(dev);

	return 0;

err_free_tx_ring:
	dma_free_coherent(&lp->pdev->dev, TX_DESC_SIZE, lp->tx_ring,
			  lp->tx_ring_dma);
err_free_rx_ring:
	dma_free_coherent(&lp->pdev->dev, RX_DESC_SIZE, lp->rx_ring,
			  lp->rx_ring_dma);
err_free_irq:
	free_irq(dev->irq, dev);
out:
	return ret;
}

static netdev_tx_t r6040_start_xmit(struct sk_buff *skb,
				    struct net_device *dev)
{
	struct r6040_private *lp = netdev_priv(dev);
	struct r6040_descriptor *descptr;
	void __iomem *ioaddr = lp->base;
	unsigned long flags;

	if (skb_put_padto(skb, ETH_ZLEN) < 0)
		return NETDEV_TX_OK;

	/* Critical Section */
	spin_lock_irqsave(&lp->lock, flags);

	/* TX resource check */
	if (!lp->tx_free_desc) {
		spin_unlock_irqrestore(&lp->lock, flags);
		netif_stop_queue(dev);
		netdev_err(dev, ": no tx descriptor\n");
		return NETDEV_TX_BUSY;
	}

	/* Set TX descriptor & Transmit it */
	lp->tx_free_desc--;
	descptr = lp->tx_insert_ptr;
	descptr->len = skb->len;
	descptr->skb_ptr = skb;
	descptr->buf = cpu_to_le32(dma_map_single(&lp->pdev->dev, skb->data,
						  skb->len, DMA_TO_DEVICE));
	descptr->status = DSC_OWNER_MAC;

	skb_tx_timestamp(skb);

	/* Trigger the MAC to check the TX descriptor */
	if (!netdev_xmit_more() || netif_queue_stopped(dev))
		iowrite16(TM2TX, ioaddr + MTPR);
	lp->tx_insert_ptr = descptr->vndescp;

	/* If no tx resource, stop */
	if (!lp->tx_free_desc)
		netif_stop_queue(dev);

	spin_unlock_irqrestore(&lp->lock, flags);

	return NETDEV_TX_OK;
}

static void r6040_multicast_list(struct net_device *dev)
{
	struct r6040_private *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;
	unsigned long flags;
	struct netdev_hw_addr *ha;
	int i;
	u16 *adrp;
	u16 hash_table[4] = { 0 };

	spin_lock_irqsave(&lp->lock, flags);

	/* Keep our MAC Address */
	adrp = (u16 *)dev->dev_addr;
	iowrite16(adrp[0], ioaddr + MID_0L);
	iowrite16(adrp[1], ioaddr + MID_0M);
	iowrite16(adrp[2], ioaddr + MID_0H);

	/* Clear AMCP & PROM bits */
	lp->mcr0 = ioread16(ioaddr + MCR0) & ~(MCR0_PROMISC | MCR0_HASH_EN);

	/* Promiscuous mode */
	if (dev->flags & IFF_PROMISC)
		lp->mcr0 |= MCR0_PROMISC;

	/* Enable multicast hash table function to
	 * receive all multicast packets. */
	else if (dev->flags & IFF_ALLMULTI) {
		lp->mcr0 |= MCR0_HASH_EN;

		for (i = 0; i < MCAST_MAX ; i++) {
			iowrite16(0, ioaddr + MID_1L + 8 * i);
			iowrite16(0, ioaddr + MID_1M + 8 * i);
			iowrite16(0, ioaddr + MID_1H + 8 * i);
		}

		for (i = 0; i < 4; i++)
			hash_table[i] = 0xffff;
	}
	/* Use internal multicast address registers if the number of
	 * multicast addresses is not greater than MCAST_MAX. */
	else if (netdev_mc_count(dev) <= MCAST_MAX) {
		i = 0;
		netdev_for_each_mc_addr(ha, dev) {
			u16 *adrp = (u16 *) ha->addr;
			iowrite16(adrp[0], ioaddr + MID_1L + 8 * i);
			iowrite16(adrp[1], ioaddr + MID_1M + 8 * i);
			iowrite16(adrp[2], ioaddr + MID_1H + 8 * i);
			i++;
		}
		while (i < MCAST_MAX) {
			iowrite16(0, ioaddr + MID_1L + 8 * i);
			iowrite16(0, ioaddr + MID_1M + 8 * i);
			iowrite16(0, ioaddr + MID_1H + 8 * i);
			i++;
		}
	}
	/* Otherwise, Enable multicast hash table function. */
	else {
		u32 crc;

		lp->mcr0 |= MCR0_HASH_EN;

		for (i = 0; i < MCAST_MAX ; i++) {
			iowrite16(0, ioaddr + MID_1L + 8 * i);
			iowrite16(0, ioaddr + MID_1M + 8 * i);
			iowrite16(0, ioaddr + MID_1H + 8 * i);
		}

		/* Build multicast hash table */
		netdev_for_each_mc_addr(ha, dev) {
			u8 *addrs = ha->addr;

			crc = ether_crc(ETH_ALEN, addrs);
			crc >>= 26;
			hash_table[crc >> 4] |= 1 << (crc & 0xf);
		}
	}

	iowrite16(lp->mcr0, ioaddr + MCR0);

	/* Fill the MAC hash tables with their values */
	if (lp->mcr0 & MCR0_HASH_EN) {
		iowrite16(hash_table[0], ioaddr + MAR0);
		iowrite16(hash_table[1], ioaddr + MAR1);
		iowrite16(hash_table[2], ioaddr + MAR2);
		iowrite16(hash_table[3], ioaddr + MAR3);
	}

	spin_unlock_irqrestore(&lp->lock, flags);
}

static void netdev_get_drvinfo(struct net_device *dev,
			struct ethtool_drvinfo *info)
{
	struct r6040_private *rp = netdev_priv(dev);

	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(rp->pdev), sizeof(info->bus_info));
}

static const struct ethtool_ops netdev_ethtool_ops = {
	.get_drvinfo		= netdev_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_ts_info		= ethtool_op_get_ts_info,
	.get_link_ksettings     = phy_ethtool_get_link_ksettings,
	.set_link_ksettings     = phy_ethtool_set_link_ksettings,
};

static const struct net_device_ops r6040_netdev_ops = {
	.ndo_open		= r6040_open,
	.ndo_stop		= r6040_close,
	.ndo_start_xmit		= r6040_start_xmit,
	.ndo_get_stats		= r6040_get_stats,
	.ndo_set_rx_mode	= r6040_multicast_list,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_do_ioctl		= phy_do_ioctl,
	.ndo_tx_timeout		= r6040_tx_timeout,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= r6040_poll_controller,
#endif
};

static void r6040_adjust_link(struct net_device *dev)
{
	struct r6040_private *lp = netdev_priv(dev);
	struct phy_device *phydev = dev->phydev;
	int status_changed = 0;
	void __iomem *ioaddr = lp->base;

	BUG_ON(!phydev);

	if (lp->old_link != phydev->link) {
		status_changed = 1;
		lp->old_link = phydev->link;
	}

	/* reflect duplex change */
	if (phydev->link && (lp->old_duplex != phydev->duplex)) {
		lp->mcr0 |= (phydev->duplex == DUPLEX_FULL ? MCR0_FD : 0);
		iowrite16(lp->mcr0, ioaddr);

		status_changed = 1;
		lp->old_duplex = phydev->duplex;
	}

	if (status_changed)
		phy_print_status(phydev);
}

static int r6040_mii_probe(struct net_device *dev)
{
	struct r6040_private *lp = netdev_priv(dev);
	struct phy_device *phydev = NULL;

	phydev = phy_find_first(lp->mii_bus);
	if (!phydev) {
		dev_err(&lp->pdev->dev, "no PHY found\n");
		return -ENODEV;
	}

	phydev = phy_connect(dev, phydev_name(phydev), &r6040_adjust_link,
			     PHY_INTERFACE_MODE_MII);

	if (IS_ERR(phydev)) {
		dev_err(&lp->pdev->dev, "could not attach to PHY\n");
		return PTR_ERR(phydev);
	}

	phy_set_max_speed(phydev, SPEED_100);

	lp->old_link = 0;
	lp->old_duplex = -1;

	phy_attached_info(phydev);

	return 0;
}

static int r6040_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *dev;
	struct r6040_private *lp;
	void __iomem *ioaddr;
	int err, io_size = R6040_IO_SIZE;
	static int card_idx = -1;
	int bar = 0;
	u16 *adrp;

	pr_info("%s\n", version);

	err = pci_enable_device(pdev);
	if (err)
		goto err_out;

	/* this should always be supported */
	err = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (err) {
		dev_err(&pdev->dev, "32-bit PCI DMA addresses not supported by the card\n");
		goto err_out_disable_dev;
	}
	err = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (err) {
		dev_err(&pdev->dev, "32-bit PCI DMA addresses not supported by the card\n");
		goto err_out_disable_dev;
	}

	/* IO Size check */
	if (pci_resource_len(pdev, bar) < io_size) {
		dev_err(&pdev->dev, "Insufficient PCI resources, aborting\n");
		err = -EIO;
		goto err_out_disable_dev;
	}

	pci_set_master(pdev);

	dev = alloc_etherdev(sizeof(struct r6040_private));
	if (!dev) {
		err = -ENOMEM;
		goto err_out_disable_dev;
	}
	SET_NETDEV_DEV(dev, &pdev->dev);
	lp = netdev_priv(dev);

	err = pci_request_regions(pdev, DRV_NAME);

	if (err) {
		dev_err(&pdev->dev, "Failed to request PCI regions\n");
		goto err_out_free_dev;
	}

	ioaddr = pci_iomap(pdev, bar, io_size);
	if (!ioaddr) {
		dev_err(&pdev->dev, "ioremap failed for device\n");
		err = -EIO;
		goto err_out_free_res;
	}

	/* If PHY status change register is still set to zero it means the
	 * bootloader didn't initialize it, so we set it to:
	 * - enable phy status change
	 * - enable all phy addresses
	 * - set to lowest timer divider */
	if (ioread16(ioaddr + PHY_CC) == 0)
		iowrite16(SCEN | PHY_MAX_ADDR << PHYAD_SHIFT |
				7 << TMRDIV_SHIFT, ioaddr + PHY_CC);

	/* Init system & device */
	lp->base = ioaddr;
	dev->irq = pdev->irq;

	spin_lock_init(&lp->lock);
	pci_set_drvdata(pdev, dev);

	/* Set MAC address */
	card_idx++;

	adrp = (u16 *)dev->dev_addr;
	adrp[0] = ioread16(ioaddr + MID_0L);
	adrp[1] = ioread16(ioaddr + MID_0M);
	adrp[2] = ioread16(ioaddr + MID_0H);

	/* Some bootloader/BIOSes do not initialize
	 * MAC address, warn about that */
	if (!(adrp[0] || adrp[1] || adrp[2])) {
		netdev_warn(dev, "MAC address not initialized, "
					"generating random\n");
		eth_hw_addr_random(dev);
	}

	/* Link new device into r6040_root_dev */
	lp->pdev = pdev;
	lp->dev = dev;

	/* Init RDC private data */
	lp->mcr0 = MCR0_XMTEN | MCR0_RCVEN;

	/* The RDC-specific entries in the device structure. */
	dev->netdev_ops = &r6040_netdev_ops;
	dev->ethtool_ops = &netdev_ethtool_ops;
	dev->watchdog_timeo = TX_TIMEOUT;

	netif_napi_add(dev, &lp->napi, r6040_poll, 64);

	lp->mii_bus = mdiobus_alloc();
	if (!lp->mii_bus) {
		dev_err(&pdev->dev, "mdiobus_alloc() failed\n");
		err = -ENOMEM;
		goto err_out_unmap;
	}

	lp->mii_bus->priv = dev;
	lp->mii_bus->read = r6040_mdiobus_read;
	lp->mii_bus->write = r6040_mdiobus_write;
	lp->mii_bus->name = "r6040_eth_mii";
	snprintf(lp->mii_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		dev_name(&pdev->dev), card_idx);

	err = mdiobus_register(lp->mii_bus);
	if (err) {
		dev_err(&pdev->dev, "failed to register MII bus\n");
		goto err_out_mdio;
	}

	err = r6040_mii_probe(dev);
	if (err) {
		dev_err(&pdev->dev, "failed to probe MII bus\n");
		goto err_out_mdio_unregister;
	}

	/* Register net device. After this dev->name assign */
	err = register_netdev(dev);
	if (err) {
		dev_err(&pdev->dev, "Failed to register net device\n");
		goto err_out_phy_disconnect;
	}
	return 0;

err_out_phy_disconnect:
	phy_disconnect(dev->phydev);
err_out_mdio_unregister:
	mdiobus_unregister(lp->mii_bus);
err_out_mdio:
	mdiobus_free(lp->mii_bus);
err_out_unmap:
	netif_napi_del(&lp->napi);
	pci_iounmap(pdev, ioaddr);
err_out_free_res:
	pci_release_regions(pdev);
err_out_free_dev:
	free_netdev(dev);
err_out_disable_dev:
	pci_disable_device(pdev);
err_out:
	return err;
}

static void r6040_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct r6040_private *lp = netdev_priv(dev);

	unregister_netdev(dev);
	phy_disconnect(dev->phydev);
	mdiobus_unregister(lp->mii_bus);
	mdiobus_free(lp->mii_bus);
	netif_napi_del(&lp->napi);
	pci_iounmap(pdev, lp->base);
	pci_release_regions(pdev);
	free_netdev(dev);
	pci_disable_device(pdev);
}


static const struct pci_device_id r6040_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_RDC, 0x6040) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, r6040_pci_tbl);

static struct pci_driver r6040_driver = {
	.name		= DRV_NAME,
	.id_table	= r6040_pci_tbl,
	.probe		= r6040_init_one,
	.remove		= r6040_remove_one,
};

module_pci_driver(r6040_driver);
