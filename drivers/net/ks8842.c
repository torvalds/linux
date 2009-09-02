/*
 * ks8842_main.c timberdale KS8842 ethernet driver
 * Copyright (c) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* Supports:
 * The Micrel KS8842 behind the timberdale FPGA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>

#define DRV_NAME "ks8842"

/* Timberdale specific Registers */
#define REG_TIMB_RST	0x1c

/* KS8842 registers */

#define REG_SELECT_BANK 0x0e

/* bank 0 registers */
#define REG_QRFCR	0x04

/* bank 2 registers */
#define REG_MARL	0x00
#define REG_MARM	0x02
#define REG_MARH	0x04

/* bank 3 registers */
#define REG_GRR		0x06

/* bank 16 registers */
#define REG_TXCR	0x00
#define REG_TXSR	0x02
#define REG_RXCR	0x04
#define REG_TXMIR	0x08
#define REG_RXMIR	0x0A

/* bank 17 registers */
#define REG_TXQCR	0x00
#define REG_RXQCR	0x02
#define REG_TXFDPR	0x04
#define REG_RXFDPR	0x06
#define REG_QMU_DATA_LO 0x08
#define REG_QMU_DATA_HI 0x0A

/* bank 18 registers */
#define REG_IER		0x00
#define IRQ_LINK_CHANGE	0x8000
#define IRQ_TX		0x4000
#define IRQ_RX		0x2000
#define IRQ_RX_OVERRUN	0x0800
#define IRQ_TX_STOPPED	0x0200
#define IRQ_RX_STOPPED	0x0100
#define IRQ_RX_ERROR	0x0080
#define ENABLED_IRQS	(IRQ_LINK_CHANGE | IRQ_TX | IRQ_RX | IRQ_RX_STOPPED | \
		IRQ_TX_STOPPED | IRQ_RX_OVERRUN | IRQ_RX_ERROR)
#define REG_ISR		0x02
#define REG_RXSR	0x04
#define RXSR_VALID	0x8000
#define RXSR_BROADCAST	0x80
#define RXSR_MULTICAST	0x40
#define RXSR_UNICAST	0x20
#define RXSR_FRAMETYPE	0x08
#define RXSR_TOO_LONG	0x04
#define RXSR_RUNT	0x02
#define RXSR_CRC_ERROR	0x01
#define RXSR_ERROR	(RXSR_TOO_LONG | RXSR_RUNT | RXSR_CRC_ERROR)

/* bank 32 registers */
#define REG_SW_ID_AND_ENABLE	0x00
#define REG_SGCR1		0x02
#define REG_SGCR2		0x04
#define REG_SGCR3		0x06

/* bank 39 registers */
#define REG_MACAR1		0x00
#define REG_MACAR2		0x02
#define REG_MACAR3		0x04

/* bank 45 registers */
#define REG_P1MBCR		0x00
#define REG_P1MBSR		0x02

/* bank 46 registers */
#define REG_P2MBCR		0x00
#define REG_P2MBSR		0x02

/* bank 48 registers */
#define REG_P1CR2		0x02

/* bank 49 registers */
#define REG_P1CR4		0x02
#define REG_P1SR		0x04

struct ks8842_adapter {
	void __iomem	*hw_addr;
	int		irq;
	struct tasklet_struct	tasklet;
	spinlock_t	lock; /* spinlock to be interrupt safe */
	struct platform_device *pdev;
};

static inline void ks8842_select_bank(struct ks8842_adapter *adapter, u16 bank)
{
	iowrite16(bank, adapter->hw_addr + REG_SELECT_BANK);
}

static inline void ks8842_write8(struct ks8842_adapter *adapter, u16 bank,
	u8 value, int offset)
{
	ks8842_select_bank(adapter, bank);
	iowrite8(value, adapter->hw_addr + offset);
}

static inline void ks8842_write16(struct ks8842_adapter *adapter, u16 bank,
	u16 value, int offset)
{
	ks8842_select_bank(adapter, bank);
	iowrite16(value, adapter->hw_addr + offset);
}

static inline void ks8842_enable_bits(struct ks8842_adapter *adapter, u16 bank,
	u16 bits, int offset)
{
	u16 reg;
	ks8842_select_bank(adapter, bank);
	reg = ioread16(adapter->hw_addr + offset);
	reg |= bits;
	iowrite16(reg, adapter->hw_addr + offset);
}

static inline void ks8842_clear_bits(struct ks8842_adapter *adapter, u16 bank,
	u16 bits, int offset)
{
	u16 reg;
	ks8842_select_bank(adapter, bank);
	reg = ioread16(adapter->hw_addr + offset);
	reg &= ~bits;
	iowrite16(reg, adapter->hw_addr + offset);
}

static inline void ks8842_write32(struct ks8842_adapter *adapter, u16 bank,
	u32 value, int offset)
{
	ks8842_select_bank(adapter, bank);
	iowrite32(value, adapter->hw_addr + offset);
}

static inline u8 ks8842_read8(struct ks8842_adapter *adapter, u16 bank,
	int offset)
{
	ks8842_select_bank(adapter, bank);
	return ioread8(adapter->hw_addr + offset);
}

static inline u16 ks8842_read16(struct ks8842_adapter *adapter, u16 bank,
	int offset)
{
	ks8842_select_bank(adapter, bank);
	return ioread16(adapter->hw_addr + offset);
}

static inline u32 ks8842_read32(struct ks8842_adapter *adapter, u16 bank,
	int offset)
{
	ks8842_select_bank(adapter, bank);
	return ioread32(adapter->hw_addr + offset);
}

static void ks8842_reset(struct ks8842_adapter *adapter)
{
	/* The KS8842 goes haywire when doing softare reset
	 * a work around in the timberdale IP is implemented to
	 * do a hardware reset instead
	ks8842_write16(adapter, 3, 1, REG_GRR);
	msleep(10);
	iowrite16(0, adapter->hw_addr + REG_GRR);
	*/
	iowrite16(32, adapter->hw_addr + REG_SELECT_BANK);
	iowrite32(0x1, adapter->hw_addr + REG_TIMB_RST);
	msleep(20);
}

static void ks8842_update_link_status(struct net_device *netdev,
	struct ks8842_adapter *adapter)
{
	/* check the status of the link */
	if (ks8842_read16(adapter, 45, REG_P1MBSR) & 0x4) {
		netif_carrier_on(netdev);
		netif_wake_queue(netdev);
	} else {
		netif_stop_queue(netdev);
		netif_carrier_off(netdev);
	}
}

static void ks8842_enable_tx(struct ks8842_adapter *adapter)
{
	ks8842_enable_bits(adapter, 16, 0x01, REG_TXCR);
}

static void ks8842_disable_tx(struct ks8842_adapter *adapter)
{
	ks8842_clear_bits(adapter, 16, 0x01, REG_TXCR);
}

static void ks8842_enable_rx(struct ks8842_adapter *adapter)
{
	ks8842_enable_bits(adapter, 16, 0x01, REG_RXCR);
}

static void ks8842_disable_rx(struct ks8842_adapter *adapter)
{
	ks8842_clear_bits(adapter, 16, 0x01, REG_RXCR);
}

static void ks8842_reset_hw(struct ks8842_adapter *adapter)
{
	/* reset the HW */
	ks8842_reset(adapter);

	/* Enable QMU Transmit flow control / transmit padding / Transmit CRC */
	ks8842_write16(adapter, 16, 0x000E, REG_TXCR);

	/* enable the receiver, uni + multi + broadcast + flow ctrl
		+ crc strip */
	ks8842_write16(adapter, 16, 0x8 | 0x20 | 0x40 | 0x80 | 0x400,
		REG_RXCR);

	/* TX frame pointer autoincrement */
	ks8842_write16(adapter, 17, 0x4000, REG_TXFDPR);

	/* RX frame pointer autoincrement */
	ks8842_write16(adapter, 17, 0x4000, REG_RXFDPR);

	/* RX 2 kb high watermark */
	ks8842_write16(adapter, 0, 0x1000, REG_QRFCR);

	/* aggresive back off in half duplex */
	ks8842_enable_bits(adapter, 32, 1 << 8, REG_SGCR1);

	/* enable no excessive collison drop */
	ks8842_enable_bits(adapter, 32, 1 << 3, REG_SGCR2);

	/* Enable port 1 force flow control / back pressure / transmit / recv */
	ks8842_write16(adapter, 48, 0x1E07, REG_P1CR2);

	/* restart port auto-negotiation */
	ks8842_enable_bits(adapter, 49, 1 << 13, REG_P1CR4);
	/* only advertise 10Mbps */
	ks8842_clear_bits(adapter, 49, 3 << 2, REG_P1CR4);

	/* Enable the transmitter */
	ks8842_enable_tx(adapter);

	/* Enable the receiver */
	ks8842_enable_rx(adapter);

	/* clear all interrupts */
	ks8842_write16(adapter, 18, 0xffff, REG_ISR);

	/* enable interrupts */
	ks8842_write16(adapter, 18, ENABLED_IRQS, REG_IER);

	/* enable the switch */
	ks8842_write16(adapter, 32, 0x1, REG_SW_ID_AND_ENABLE);
}

static void ks8842_read_mac_addr(struct ks8842_adapter *adapter, u8 *dest)
{
	int i;
	u16 mac;

	for (i = 0; i < ETH_ALEN; i++)
		dest[ETH_ALEN - i - 1] = ks8842_read8(adapter, 2, REG_MARL + i);

	/* make sure the switch port uses the same MAC as the QMU */
	mac = ks8842_read16(adapter, 2, REG_MARL);
	ks8842_write16(adapter, 39, mac, REG_MACAR1);
	mac = ks8842_read16(adapter, 2, REG_MARM);
	ks8842_write16(adapter, 39, mac, REG_MACAR2);
	mac = ks8842_read16(adapter, 2, REG_MARH);
	ks8842_write16(adapter, 39, mac, REG_MACAR3);
}

static inline u16 ks8842_tx_fifo_space(struct ks8842_adapter *adapter)
{
	return ks8842_read16(adapter, 16, REG_TXMIR) & 0x1fff;
}

static int ks8842_tx_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct ks8842_adapter *adapter = netdev_priv(netdev);
	int len = skb->len;
	u32 *ptr = (u32 *)skb->data;
	u32 ctrl;

	dev_dbg(&adapter->pdev->dev,
		"%s: len %u head %p data %p tail %p end %p\n",
		__func__, skb->len, skb->head, skb->data,
		skb_tail_pointer(skb), skb_end_pointer(skb));

	/* check FIFO buffer space, we need space for CRC and command bits */
	if (ks8842_tx_fifo_space(adapter) < len + 8)
		return NETDEV_TX_BUSY;

	/* the control word, enable IRQ, port 1 and the length */
	ctrl = 0x8000 | 0x100 | (len << 16);
	ks8842_write32(adapter, 17, ctrl, REG_QMU_DATA_LO);

	netdev->stats.tx_bytes += len;

	/* copy buffer */
	while (len > 0) {
		iowrite32(*ptr, adapter->hw_addr + REG_QMU_DATA_LO);
		len -= sizeof(u32);
		ptr++;
	}

	/* enqueue packet */
	ks8842_write16(adapter, 17, 1, REG_TXQCR);

	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

static void ks8842_rx_frame(struct net_device *netdev,
	struct ks8842_adapter *adapter)
{
	u32 status = ks8842_read32(adapter, 17, REG_QMU_DATA_LO);
	int len = (status >> 16) & 0x7ff;

	status &= 0xffff;

	dev_dbg(&adapter->pdev->dev, "%s - rx_data: status: %x\n",
		__func__, status);

	/* check the status */
	if ((status & RXSR_VALID) && !(status & RXSR_ERROR)) {
		struct sk_buff *skb = netdev_alloc_skb(netdev, len + 2);

		dev_dbg(&adapter->pdev->dev, "%s, got package, len: %d\n",
			__func__, len);
		if (skb) {
			u32 *data;

			netdev->stats.rx_packets++;
			netdev->stats.rx_bytes += len;
			if (status & RXSR_MULTICAST)
				netdev->stats.multicast++;

			/* Align socket buffer in 4-byte boundary for
				 better performance. */
			skb_reserve(skb, 2);
			data = (u32 *)skb_put(skb, len);

			ks8842_select_bank(adapter, 17);
			while (len > 0) {
				*data++ = ioread32(adapter->hw_addr +
					REG_QMU_DATA_LO);
				len -= sizeof(u32);
			}

			skb->protocol = eth_type_trans(skb, netdev);
			netif_rx(skb);
		} else
			netdev->stats.rx_dropped++;
	} else {
		dev_dbg(&adapter->pdev->dev, "RX error, status: %x\n", status);
		netdev->stats.rx_errors++;
		if (status & RXSR_TOO_LONG)
			netdev->stats.rx_length_errors++;
		if (status & RXSR_CRC_ERROR)
			netdev->stats.rx_crc_errors++;
		if (status & RXSR_RUNT)
			netdev->stats.rx_frame_errors++;
	}

	/* set high watermark to 3K */
	ks8842_clear_bits(adapter, 0, 1 << 12, REG_QRFCR);

	/* release the frame */
	ks8842_write16(adapter, 17, 0x01, REG_RXQCR);

	/* set high watermark to 2K */
	ks8842_enable_bits(adapter, 0, 1 << 12, REG_QRFCR);
}

void ks8842_handle_rx(struct net_device *netdev, struct ks8842_adapter *adapter)
{
	u16 rx_data = ks8842_read16(adapter, 16, REG_RXMIR) & 0x1fff;
	dev_dbg(&adapter->pdev->dev, "%s Entry - rx_data: %d\n",
		__func__, rx_data);
	while (rx_data) {
		ks8842_rx_frame(netdev, adapter);
		rx_data = ks8842_read16(adapter, 16, REG_RXMIR) & 0x1fff;
	}
}

void ks8842_handle_tx(struct net_device *netdev, struct ks8842_adapter *adapter)
{
	u16 sr = ks8842_read16(adapter, 16, REG_TXSR);
	dev_dbg(&adapter->pdev->dev, "%s - entry, sr: %x\n", __func__, sr);
	netdev->stats.tx_packets++;
	if (netif_queue_stopped(netdev))
		netif_wake_queue(netdev);
}

void ks8842_handle_rx_overrun(struct net_device *netdev,
	struct ks8842_adapter *adapter)
{
	dev_dbg(&adapter->pdev->dev, "%s: entry\n", __func__);
	netdev->stats.rx_errors++;
	netdev->stats.rx_fifo_errors++;
}

void ks8842_tasklet(unsigned long arg)
{
	struct net_device *netdev = (struct net_device *)arg;
	struct ks8842_adapter *adapter = netdev_priv(netdev);
	u16 isr;
	unsigned long flags;
	u16 entry_bank;

	/* read current bank to be able to set it back */
	spin_lock_irqsave(&adapter->lock, flags);
	entry_bank = ioread16(adapter->hw_addr + REG_SELECT_BANK);
	spin_unlock_irqrestore(&adapter->lock, flags);

	isr = ks8842_read16(adapter, 18, REG_ISR);
	dev_dbg(&adapter->pdev->dev, "%s - ISR: 0x%x\n", __func__, isr);

	/* Ack */
	ks8842_write16(adapter, 18, isr, REG_ISR);

	if (!netif_running(netdev))
		return;

	if (isr & IRQ_LINK_CHANGE)
		ks8842_update_link_status(netdev, adapter);

	if (isr & (IRQ_RX | IRQ_RX_ERROR))
		ks8842_handle_rx(netdev, adapter);

	if (isr & IRQ_TX)
		ks8842_handle_tx(netdev, adapter);

	if (isr & IRQ_RX_OVERRUN)
		ks8842_handle_rx_overrun(netdev, adapter);

	if (isr & IRQ_TX_STOPPED) {
		ks8842_disable_tx(adapter);
		ks8842_enable_tx(adapter);
	}

	if (isr & IRQ_RX_STOPPED) {
		ks8842_disable_rx(adapter);
		ks8842_enable_rx(adapter);
	}

	/* re-enable interrupts, put back the bank selection register */
	spin_lock_irqsave(&adapter->lock, flags);
	ks8842_write16(adapter, 18, ENABLED_IRQS, REG_IER);
	iowrite16(entry_bank, adapter->hw_addr + REG_SELECT_BANK);
	spin_unlock_irqrestore(&adapter->lock, flags);
}

static irqreturn_t ks8842_irq(int irq, void *devid)
{
	struct ks8842_adapter *adapter = devid;
	u16 isr;
	u16 entry_bank = ioread16(adapter->hw_addr + REG_SELECT_BANK);
	irqreturn_t ret = IRQ_NONE;

	isr = ks8842_read16(adapter, 18, REG_ISR);
	dev_dbg(&adapter->pdev->dev, "%s - ISR: 0x%x\n", __func__, isr);

	if (isr) {
		/* disable IRQ */
		ks8842_write16(adapter, 18, 0x00, REG_IER);

		/* schedule tasklet */
		tasklet_schedule(&adapter->tasklet);

		ret = IRQ_HANDLED;
	}

	iowrite16(entry_bank, adapter->hw_addr + REG_SELECT_BANK);

	return ret;
}


/* Netdevice operations */

static int ks8842_open(struct net_device *netdev)
{
	struct ks8842_adapter *adapter = netdev_priv(netdev);
	int err;

	dev_dbg(&adapter->pdev->dev, "%s - entry\n", __func__);

	/* reset the HW */
	ks8842_reset_hw(adapter);

	ks8842_update_link_status(netdev, adapter);

	err = request_irq(adapter->irq, ks8842_irq, IRQF_SHARED, DRV_NAME,
		adapter);
	if (err) {
		printk(KERN_ERR "Failed to request IRQ: %d: %d\n",
			adapter->irq, err);
		return err;
	}

	return 0;
}

static int ks8842_close(struct net_device *netdev)
{
	struct ks8842_adapter *adapter = netdev_priv(netdev);

	dev_dbg(&adapter->pdev->dev, "%s - entry\n", __func__);

	/* free the irq */
	free_irq(adapter->irq, adapter);

	/* disable the switch */
	ks8842_write16(adapter, 32, 0x0, REG_SW_ID_AND_ENABLE);

	return 0;
}

static netdev_tx_t ks8842_xmit_frame(struct sk_buff *skb,
				     struct net_device *netdev)
{
	int ret;
	struct ks8842_adapter *adapter = netdev_priv(netdev);

	dev_dbg(&adapter->pdev->dev, "%s: entry\n", __func__);

	ret = ks8842_tx_frame(skb, netdev);

	if (ks8842_tx_fifo_space(adapter) <  netdev->mtu + 8)
		netif_stop_queue(netdev);

	return ret;
}

static int ks8842_set_mac(struct net_device *netdev, void *p)
{
	struct ks8842_adapter *adapter = netdev_priv(netdev);
	unsigned long flags;
	struct sockaddr *addr = p;
	char *mac = (u8 *)addr->sa_data;
	int i;

	dev_dbg(&adapter->pdev->dev, "%s: entry\n", __func__);

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(netdev->dev_addr, mac, netdev->addr_len);

	spin_lock_irqsave(&adapter->lock, flags);
	for (i = 0; i < ETH_ALEN; i++) {
		ks8842_write8(adapter, 2, mac[ETH_ALEN - i - 1], REG_MARL + i);
		ks8842_write8(adapter, 39, mac[ETH_ALEN - i - 1],
			REG_MACAR1 + i);
	}
	spin_unlock_irqrestore(&adapter->lock, flags);
	return 0;
}

static void ks8842_tx_timeout(struct net_device *netdev)
{
	struct ks8842_adapter *adapter = netdev_priv(netdev);
	unsigned long flags;

	dev_dbg(&adapter->pdev->dev, "%s: entry\n", __func__);

	spin_lock_irqsave(&adapter->lock, flags);
	/* disable interrupts */
	ks8842_write16(adapter, 18, 0, REG_IER);
	ks8842_write16(adapter, 18, 0xFFFF, REG_ISR);
	spin_unlock_irqrestore(&adapter->lock, flags);

	ks8842_reset_hw(adapter);

	ks8842_update_link_status(netdev, adapter);
}

static const struct net_device_ops ks8842_netdev_ops = {
	.ndo_open		= ks8842_open,
	.ndo_stop		= ks8842_close,
	.ndo_start_xmit		= ks8842_xmit_frame,
	.ndo_set_mac_address	= ks8842_set_mac,
	.ndo_tx_timeout 	= ks8842_tx_timeout,
	.ndo_validate_addr	= eth_validate_addr
};

static const struct ethtool_ops ks8842_ethtool_ops = {
	.get_link		= ethtool_op_get_link,
};

static int __devinit ks8842_probe(struct platform_device *pdev)
{
	int err = -ENOMEM;
	struct resource *iomem;
	struct net_device *netdev;
	struct ks8842_adapter *adapter;
	u16 id;

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!request_mem_region(iomem->start, resource_size(iomem), DRV_NAME))
		goto err_mem_region;

	netdev = alloc_etherdev(sizeof(struct ks8842_adapter));
	if (!netdev)
		goto err_alloc_etherdev;

	SET_NETDEV_DEV(netdev, &pdev->dev);

	adapter = netdev_priv(netdev);
	adapter->hw_addr = ioremap(iomem->start, resource_size(iomem));
	if (!adapter->hw_addr)
		goto err_ioremap;

	adapter->irq = platform_get_irq(pdev, 0);
	if (adapter->irq < 0) {
		err = adapter->irq;
		goto err_get_irq;
	}

	adapter->pdev = pdev;

	tasklet_init(&adapter->tasklet, ks8842_tasklet, (unsigned long)netdev);
	spin_lock_init(&adapter->lock);

	netdev->netdev_ops = &ks8842_netdev_ops;
	netdev->ethtool_ops = &ks8842_ethtool_ops;

	ks8842_read_mac_addr(adapter, netdev->dev_addr);

	id = ks8842_read16(adapter, 32, REG_SW_ID_AND_ENABLE);

	strcpy(netdev->name, "eth%d");
	err = register_netdev(netdev);
	if (err)
		goto err_register;

	platform_set_drvdata(pdev, netdev);

	printk(KERN_INFO DRV_NAME
		" Found chip, family: 0x%x, id: 0x%x, rev: 0x%x\n",
		(id >> 8) & 0xff, (id >> 4) & 0xf, (id >> 1) & 0x7);

	return 0;

err_register:
err_get_irq:
	iounmap(adapter->hw_addr);
err_ioremap:
	free_netdev(netdev);
err_alloc_etherdev:
	release_mem_region(iomem->start, resource_size(iomem));
err_mem_region:
	return err;
}

static int __devexit ks8842_remove(struct platform_device *pdev)
{
	struct net_device *netdev = platform_get_drvdata(pdev);
	struct ks8842_adapter *adapter = netdev_priv(netdev);
	struct resource *iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	unregister_netdev(netdev);
	tasklet_kill(&adapter->tasklet);
	iounmap(adapter->hw_addr);
	free_netdev(netdev);
	release_mem_region(iomem->start, resource_size(iomem));
	platform_set_drvdata(pdev, NULL);
	return 0;
}


static struct platform_driver ks8842_platform_driver = {
	.driver = {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
	.probe		= ks8842_probe,
	.remove		= ks8842_remove,
};

static int __init ks8842_init(void)
{
	return platform_driver_register(&ks8842_platform_driver);
}

static void __exit ks8842_exit(void)
{
	platform_driver_unregister(&ks8842_platform_driver);
}

module_init(ks8842_init);
module_exit(ks8842_exit);

MODULE_DESCRIPTION("Timberdale KS8842 ethernet driver");
MODULE_AUTHOR("Mocean Laboratories <info@mocean-labs.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:ks8842");

