/*
 * ks8842.c timberdale KS8842 ethernet driver
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
 * The genuine Micrel KS8841/42 device with ISA 16/32bit bus interface
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/ks8842.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>

#define DRV_NAME "ks8842"

/* Timberdale specific Registers */
#define REG_TIMB_RST		0x1c
#define REG_TIMB_FIFO		0x20
#define REG_TIMB_ISR		0x24
#define REG_TIMB_IER		0x28
#define REG_TIMB_IAR		0x2C
#define REQ_TIMB_DMA_RESUME	0x30

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
/* When running via timberdale in DMA mode, the RX interrupt should be
   enabled in the KS8842, but not in the FPGA IP, since the IP handles
   RX DMA internally.
   TX interrupts are not needed it is handled by the FPGA the driver is
   notified via DMA callbacks.
*/
#define ENABLED_IRQS_DMA_IP	(IRQ_LINK_CHANGE | IRQ_RX_STOPPED | \
	IRQ_TX_STOPPED | IRQ_RX_OVERRUN | IRQ_RX_ERROR)
#define ENABLED_IRQS_DMA	(ENABLED_IRQS_DMA_IP | IRQ_RX)
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

/* flags passed by platform_device for configuration */
#define	MICREL_KS884X		0x01	/* 0=Timeberdale(FPGA), 1=Micrel */
#define	KS884X_16BIT		0x02	/*  1=16bit, 0=32bit */

#define DMA_BUFFER_SIZE		2048

struct ks8842_tx_dma_ctl {
	struct dma_chan *chan;
	struct dma_async_tx_descriptor *adesc;
	void *buf;
	struct scatterlist sg;
	int channel;
};

struct ks8842_rx_dma_ctl {
	struct dma_chan *chan;
	struct dma_async_tx_descriptor *adesc;
	struct sk_buff  *skb;
	struct scatterlist sg;
	struct tasklet_struct tasklet;
	int channel;
};

#define KS8842_USE_DMA(adapter) (((adapter)->dma_tx.channel != -1) && \
	 ((adapter)->dma_rx.channel != -1))

struct ks8842_adapter {
	void __iomem	*hw_addr;
	int		irq;
	unsigned long	conf_flags;	/* copy of platform_device config */
	struct tasklet_struct	tasklet;
	spinlock_t	lock; /* spinlock to be interrupt safe */
	struct work_struct timeout_work;
	struct net_device *netdev;
	struct device *dev;
	struct ks8842_tx_dma_ctl	dma_tx;
	struct ks8842_rx_dma_ctl	dma_rx;
};

static void ks8842_dma_rx_cb(void *data);
static void ks8842_dma_tx_cb(void *data);

static inline void ks8842_resume_dma(struct ks8842_adapter *adapter)
{
	iowrite32(1, adapter->hw_addr + REQ_TIMB_DMA_RESUME);
}

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
	if (adapter->conf_flags & MICREL_KS884X) {
		ks8842_write16(adapter, 3, 1, REG_GRR);
		msleep(10);
		iowrite16(0, adapter->hw_addr + REG_GRR);
	} else {
		/* The KS8842 goes haywire when doing softare reset
		* a work around in the timberdale IP is implemented to
		* do a hardware reset instead
		ks8842_write16(adapter, 3, 1, REG_GRR);
		msleep(10);
		iowrite16(0, adapter->hw_addr + REG_GRR);
		*/
		iowrite32(0x1, adapter->hw_addr + REG_TIMB_RST);
		msleep(20);
	}
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

	/* aggressive back off in half duplex */
	ks8842_enable_bits(adapter, 32, 1 << 8, REG_SGCR1);

	/* enable no excessive collison drop */
	ks8842_enable_bits(adapter, 32, 1 << 3, REG_SGCR2);

	/* Enable port 1 force flow control / back pressure / transmit / recv */
	ks8842_write16(adapter, 48, 0x1E07, REG_P1CR2);

	/* restart port auto-negotiation */
	ks8842_enable_bits(adapter, 49, 1 << 13, REG_P1CR4);

	/* Enable the transmitter */
	ks8842_enable_tx(adapter);

	/* Enable the receiver */
	ks8842_enable_rx(adapter);

	/* clear all interrupts */
	ks8842_write16(adapter, 18, 0xffff, REG_ISR);

	/* enable interrupts */
	if (KS8842_USE_DMA(adapter)) {
		/* When running in DMA Mode the RX interrupt is not enabled in
		   timberdale because RX data is received by DMA callbacks
		   it must still be enabled in the KS8842 because it indicates
		   to timberdale when there is RX data for it's DMA FIFOs */
		iowrite16(ENABLED_IRQS_DMA_IP, adapter->hw_addr + REG_TIMB_IER);
		ks8842_write16(adapter, 18, ENABLED_IRQS_DMA, REG_IER);
	} else {
		if (!(adapter->conf_flags & MICREL_KS884X))
			iowrite16(ENABLED_IRQS,
				adapter->hw_addr + REG_TIMB_IER);
		ks8842_write16(adapter, 18, ENABLED_IRQS, REG_IER);
	}
	/* enable the switch */
	ks8842_write16(adapter, 32, 0x1, REG_SW_ID_AND_ENABLE);
}

static void ks8842_read_mac_addr(struct ks8842_adapter *adapter, u8 *dest)
{
	int i;
	u16 mac;

	for (i = 0; i < ETH_ALEN; i++)
		dest[ETH_ALEN - i - 1] = ks8842_read8(adapter, 2, REG_MARL + i);

	if (adapter->conf_flags & MICREL_KS884X) {
		/*
		the sequence of saving mac addr between MAC and Switch is
		different.
		*/

		mac = ks8842_read16(adapter, 2, REG_MARL);
		ks8842_write16(adapter, 39, mac, REG_MACAR3);
		mac = ks8842_read16(adapter, 2, REG_MARM);
		ks8842_write16(adapter, 39, mac, REG_MACAR2);
		mac = ks8842_read16(adapter, 2, REG_MARH);
		ks8842_write16(adapter, 39, mac, REG_MACAR1);
	} else {

		/* make sure the switch port uses the same MAC as the QMU */
		mac = ks8842_read16(adapter, 2, REG_MARL);
		ks8842_write16(adapter, 39, mac, REG_MACAR1);
		mac = ks8842_read16(adapter, 2, REG_MARM);
		ks8842_write16(adapter, 39, mac, REG_MACAR2);
		mac = ks8842_read16(adapter, 2, REG_MARH);
		ks8842_write16(adapter, 39, mac, REG_MACAR3);
	}
}

static void ks8842_write_mac_addr(struct ks8842_adapter *adapter, u8 *mac)
{
	unsigned long flags;
	unsigned i;

	spin_lock_irqsave(&adapter->lock, flags);
	for (i = 0; i < ETH_ALEN; i++) {
		ks8842_write8(adapter, 2, mac[ETH_ALEN - i - 1], REG_MARL + i);
		if (!(adapter->conf_flags & MICREL_KS884X))
			ks8842_write8(adapter, 39, mac[ETH_ALEN - i - 1],
				REG_MACAR1 + i);
	}

	if (adapter->conf_flags & MICREL_KS884X) {
		/*
		the sequence of saving mac addr between MAC and Switch is
		different.
		*/

		u16 mac;

		mac = ks8842_read16(adapter, 2, REG_MARL);
		ks8842_write16(adapter, 39, mac, REG_MACAR3);
		mac = ks8842_read16(adapter, 2, REG_MARM);
		ks8842_write16(adapter, 39, mac, REG_MACAR2);
		mac = ks8842_read16(adapter, 2, REG_MARH);
		ks8842_write16(adapter, 39, mac, REG_MACAR1);
	}
	spin_unlock_irqrestore(&adapter->lock, flags);
}

static inline u16 ks8842_tx_fifo_space(struct ks8842_adapter *adapter)
{
	return ks8842_read16(adapter, 16, REG_TXMIR) & 0x1fff;
}

static int ks8842_tx_frame_dma(struct sk_buff *skb, struct net_device *netdev)
{
	struct ks8842_adapter *adapter = netdev_priv(netdev);
	struct ks8842_tx_dma_ctl *ctl = &adapter->dma_tx;
	u8 *buf = ctl->buf;

	if (ctl->adesc) {
		netdev_dbg(netdev, "%s: TX ongoing\n", __func__);
		/* transfer ongoing */
		return NETDEV_TX_BUSY;
	}

	sg_dma_len(&ctl->sg) = skb->len + sizeof(u32);

	/* copy data to the TX buffer */
	/* the control word, enable IRQ, port 1 and the length */
	*buf++ = 0x00;
	*buf++ = 0x01; /* Port 1 */
	*buf++ = skb->len & 0xff;
	*buf++ = (skb->len >> 8) & 0xff;
	skb_copy_from_linear_data(skb, buf, skb->len);

	dma_sync_single_range_for_device(adapter->dev,
		sg_dma_address(&ctl->sg), 0, sg_dma_len(&ctl->sg),
		DMA_TO_DEVICE);

	/* make sure the length is a multiple of 4 */
	if (sg_dma_len(&ctl->sg) % 4)
		sg_dma_len(&ctl->sg) += 4 - sg_dma_len(&ctl->sg) % 4;

	ctl->adesc = dmaengine_prep_slave_sg(ctl->chan,
		&ctl->sg, 1, DMA_MEM_TO_DEV,
		DMA_PREP_INTERRUPT | DMA_COMPL_SKIP_SRC_UNMAP);
	if (!ctl->adesc)
		return NETDEV_TX_BUSY;

	ctl->adesc->callback_param = netdev;
	ctl->adesc->callback = ks8842_dma_tx_cb;
	ctl->adesc->tx_submit(ctl->adesc);

	netdev->stats.tx_bytes += skb->len;

	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

static int ks8842_tx_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct ks8842_adapter *adapter = netdev_priv(netdev);
	int len = skb->len;

	netdev_dbg(netdev, "%s: len %u head %p data %p tail %p end %p\n",
		__func__, skb->len, skb->head, skb->data,
		skb_tail_pointer(skb), skb_end_pointer(skb));

	/* check FIFO buffer space, we need space for CRC and command bits */
	if (ks8842_tx_fifo_space(adapter) < len + 8)
		return NETDEV_TX_BUSY;

	if (adapter->conf_flags & KS884X_16BIT) {
		u16 *ptr16 = (u16 *)skb->data;
		ks8842_write16(adapter, 17, 0x8000 | 0x100, REG_QMU_DATA_LO);
		ks8842_write16(adapter, 17, (u16)len, REG_QMU_DATA_HI);
		netdev->stats.tx_bytes += len;

		/* copy buffer */
		while (len > 0) {
			iowrite16(*ptr16++, adapter->hw_addr + REG_QMU_DATA_LO);
			iowrite16(*ptr16++, adapter->hw_addr + REG_QMU_DATA_HI);
			len -= sizeof(u32);
		}
	} else {

		u32 *ptr = (u32 *)skb->data;
		u32 ctrl;
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
	}

	/* enqueue packet */
	ks8842_write16(adapter, 17, 1, REG_TXQCR);

	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

static void ks8842_update_rx_err_counters(struct net_device *netdev, u32 status)
{
	netdev_dbg(netdev, "RX error, status: %x\n", status);

	netdev->stats.rx_errors++;
	if (status & RXSR_TOO_LONG)
		netdev->stats.rx_length_errors++;
	if (status & RXSR_CRC_ERROR)
		netdev->stats.rx_crc_errors++;
	if (status & RXSR_RUNT)
		netdev->stats.rx_frame_errors++;
}

static void ks8842_update_rx_counters(struct net_device *netdev, u32 status,
	int len)
{
	netdev_dbg(netdev, "RX packet, len: %d\n", len);

	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += len;
	if (status & RXSR_MULTICAST)
		netdev->stats.multicast++;
}

static int __ks8842_start_new_rx_dma(struct net_device *netdev)
{
	struct ks8842_adapter *adapter = netdev_priv(netdev);
	struct ks8842_rx_dma_ctl *ctl = &adapter->dma_rx;
	struct scatterlist *sg = &ctl->sg;
	int err;

	ctl->skb = netdev_alloc_skb(netdev, DMA_BUFFER_SIZE);
	if (ctl->skb) {
		sg_init_table(sg, 1);
		sg_dma_address(sg) = dma_map_single(adapter->dev,
			ctl->skb->data, DMA_BUFFER_SIZE, DMA_FROM_DEVICE);
		err = dma_mapping_error(adapter->dev, sg_dma_address(sg));
		if (unlikely(err)) {
			sg_dma_address(sg) = 0;
			goto out;
		}

		sg_dma_len(sg) = DMA_BUFFER_SIZE;

		ctl->adesc = dmaengine_prep_slave_sg(ctl->chan,
			sg, 1, DMA_DEV_TO_MEM,
			DMA_PREP_INTERRUPT | DMA_COMPL_SKIP_SRC_UNMAP);

		if (!ctl->adesc)
			goto out;

		ctl->adesc->callback_param = netdev;
		ctl->adesc->callback = ks8842_dma_rx_cb;
		ctl->adesc->tx_submit(ctl->adesc);
	} else {
		err = -ENOMEM;
		sg_dma_address(sg) = 0;
		goto out;
	}

	return err;
out:
	if (sg_dma_address(sg))
		dma_unmap_single(adapter->dev, sg_dma_address(sg),
			DMA_BUFFER_SIZE, DMA_FROM_DEVICE);
	sg_dma_address(sg) = 0;
	if (ctl->skb)
		dev_kfree_skb(ctl->skb);

	ctl->skb = NULL;

	printk(KERN_ERR DRV_NAME": Failed to start RX DMA: %d\n", err);
	return err;
}

static void ks8842_rx_frame_dma_tasklet(unsigned long arg)
{
	struct net_device *netdev = (struct net_device *)arg;
	struct ks8842_adapter *adapter = netdev_priv(netdev);
	struct ks8842_rx_dma_ctl *ctl = &adapter->dma_rx;
	struct sk_buff *skb = ctl->skb;
	dma_addr_t addr = sg_dma_address(&ctl->sg);
	u32 status;

	ctl->adesc = NULL;

	/* kick next transfer going */
	__ks8842_start_new_rx_dma(netdev);

	/* now handle the data we got */
	dma_unmap_single(adapter->dev, addr, DMA_BUFFER_SIZE, DMA_FROM_DEVICE);

	status = *((u32 *)skb->data);

	netdev_dbg(netdev, "%s - rx_data: status: %x\n",
		__func__, status & 0xffff);

	/* check the status */
	if ((status & RXSR_VALID) && !(status & RXSR_ERROR)) {
		int len = (status >> 16) & 0x7ff;

		ks8842_update_rx_counters(netdev, status, len);

		/* reserve 4 bytes which is the status word */
		skb_reserve(skb, 4);
		skb_put(skb, len);

		skb->protocol = eth_type_trans(skb, netdev);
		netif_rx(skb);
	} else {
		ks8842_update_rx_err_counters(netdev, status);
		dev_kfree_skb(skb);
	}
}

static void ks8842_rx_frame(struct net_device *netdev,
	struct ks8842_adapter *adapter)
{
	u32 status;
	int len;

	if (adapter->conf_flags & KS884X_16BIT) {
		status = ks8842_read16(adapter, 17, REG_QMU_DATA_LO);
		len = ks8842_read16(adapter, 17, REG_QMU_DATA_HI);
		netdev_dbg(netdev, "%s - rx_data: status: %x\n",
			   __func__, status);
	} else {
		status = ks8842_read32(adapter, 17, REG_QMU_DATA_LO);
		len = (status >> 16) & 0x7ff;
		status &= 0xffff;
		netdev_dbg(netdev, "%s - rx_data: status: %x\n",
			   __func__, status);
	}

	/* check the status */
	if ((status & RXSR_VALID) && !(status & RXSR_ERROR)) {
		struct sk_buff *skb = netdev_alloc_skb_ip_align(netdev, len + 3);

		if (skb) {

			ks8842_update_rx_counters(netdev, status, len);

			if (adapter->conf_flags & KS884X_16BIT) {
				u16 *data16 = (u16 *)skb_put(skb, len);
				ks8842_select_bank(adapter, 17);
				while (len > 0) {
					*data16++ = ioread16(adapter->hw_addr +
						REG_QMU_DATA_LO);
					*data16++ = ioread16(adapter->hw_addr +
						REG_QMU_DATA_HI);
					len -= sizeof(u32);
				}
			} else {
				u32 *data = (u32 *)skb_put(skb, len);

				ks8842_select_bank(adapter, 17);
				while (len > 0) {
					*data++ = ioread32(adapter->hw_addr +
						REG_QMU_DATA_LO);
					len -= sizeof(u32);
				}
			}
			skb->protocol = eth_type_trans(skb, netdev);
			netif_rx(skb);
		} else
			netdev->stats.rx_dropped++;
	} else
		ks8842_update_rx_err_counters(netdev, status);

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
	netdev_dbg(netdev, "%s Entry - rx_data: %d\n", __func__, rx_data);
	while (rx_data) {
		ks8842_rx_frame(netdev, adapter);
		rx_data = ks8842_read16(adapter, 16, REG_RXMIR) & 0x1fff;
	}
}

void ks8842_handle_tx(struct net_device *netdev, struct ks8842_adapter *adapter)
{
	u16 sr = ks8842_read16(adapter, 16, REG_TXSR);
	netdev_dbg(netdev, "%s - entry, sr: %x\n", __func__, sr);
	netdev->stats.tx_packets++;
	if (netif_queue_stopped(netdev))
		netif_wake_queue(netdev);
}

void ks8842_handle_rx_overrun(struct net_device *netdev,
	struct ks8842_adapter *adapter)
{
	netdev_dbg(netdev, "%s: entry\n", __func__);
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
	netdev_dbg(netdev, "%s - ISR: 0x%x\n", __func__, isr);

	/* when running in DMA mode, do not ack RX interrupts, it is handled
	   internally by timberdale, otherwise it's DMA FIFO:s would stop
	*/
	if (KS8842_USE_DMA(adapter))
		isr &= ~IRQ_RX;

	/* Ack */
	ks8842_write16(adapter, 18, isr, REG_ISR);

	if (!(adapter->conf_flags & MICREL_KS884X))
		/* Ack in the timberdale IP as well */
		iowrite32(0x1, adapter->hw_addr + REG_TIMB_IAR);

	if (!netif_running(netdev))
		return;

	if (isr & IRQ_LINK_CHANGE)
		ks8842_update_link_status(netdev, adapter);

	/* should not get IRQ_RX when running DMA mode */
	if (isr & (IRQ_RX | IRQ_RX_ERROR) && !KS8842_USE_DMA(adapter))
		ks8842_handle_rx(netdev, adapter);

	/* should only happen when in PIO mode */
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
	if (KS8842_USE_DMA(adapter))
		ks8842_write16(adapter, 18, ENABLED_IRQS_DMA, REG_IER);
	else
		ks8842_write16(adapter, 18, ENABLED_IRQS, REG_IER);
	iowrite16(entry_bank, adapter->hw_addr + REG_SELECT_BANK);

	/* Make sure timberdale continues DMA operations, they are stopped while
	   we are handling the ks8842 because we might change bank */
	if (KS8842_USE_DMA(adapter))
		ks8842_resume_dma(adapter);

	spin_unlock_irqrestore(&adapter->lock, flags);
}

static irqreturn_t ks8842_irq(int irq, void *devid)
{
	struct net_device *netdev = devid;
	struct ks8842_adapter *adapter = netdev_priv(netdev);
	u16 isr;
	u16 entry_bank = ioread16(adapter->hw_addr + REG_SELECT_BANK);
	irqreturn_t ret = IRQ_NONE;

	isr = ks8842_read16(adapter, 18, REG_ISR);
	netdev_dbg(netdev, "%s - ISR: 0x%x\n", __func__, isr);

	if (isr) {
		if (KS8842_USE_DMA(adapter))
			/* disable all but RX IRQ, since the FPGA relies on it*/
			ks8842_write16(adapter, 18, IRQ_RX, REG_IER);
		else
			/* disable IRQ */
			ks8842_write16(adapter, 18, 0x00, REG_IER);

		/* schedule tasklet */
		tasklet_schedule(&adapter->tasklet);

		ret = IRQ_HANDLED;
	}

	iowrite16(entry_bank, adapter->hw_addr + REG_SELECT_BANK);

	/* After an interrupt, tell timberdale to continue DMA operations.
	   DMA is disabled while we are handling the ks8842 because we might
	   change bank */
	ks8842_resume_dma(adapter);

	return ret;
}

static void ks8842_dma_rx_cb(void *data)
{
	struct net_device	*netdev = data;
	struct ks8842_adapter	*adapter = netdev_priv(netdev);

	netdev_dbg(netdev, "RX DMA finished\n");
	/* schedule tasklet */
	if (adapter->dma_rx.adesc)
		tasklet_schedule(&adapter->dma_rx.tasklet);
}

static void ks8842_dma_tx_cb(void *data)
{
	struct net_device		*netdev = data;
	struct ks8842_adapter		*adapter = netdev_priv(netdev);
	struct ks8842_tx_dma_ctl	*ctl = &adapter->dma_tx;

	netdev_dbg(netdev, "TX DMA finished\n");

	if (!ctl->adesc)
		return;

	netdev->stats.tx_packets++;
	ctl->adesc = NULL;

	if (netif_queue_stopped(netdev))
		netif_wake_queue(netdev);
}

static void ks8842_stop_dma(struct ks8842_adapter *adapter)
{
	struct ks8842_tx_dma_ctl *tx_ctl = &adapter->dma_tx;
	struct ks8842_rx_dma_ctl *rx_ctl = &adapter->dma_rx;

	tx_ctl->adesc = NULL;
	if (tx_ctl->chan)
		tx_ctl->chan->device->device_control(tx_ctl->chan,
			DMA_TERMINATE_ALL, 0);

	rx_ctl->adesc = NULL;
	if (rx_ctl->chan)
		rx_ctl->chan->device->device_control(rx_ctl->chan,
			DMA_TERMINATE_ALL, 0);

	if (sg_dma_address(&rx_ctl->sg))
		dma_unmap_single(adapter->dev, sg_dma_address(&rx_ctl->sg),
			DMA_BUFFER_SIZE, DMA_FROM_DEVICE);
	sg_dma_address(&rx_ctl->sg) = 0;

	dev_kfree_skb(rx_ctl->skb);
	rx_ctl->skb = NULL;
}

static void ks8842_dealloc_dma_bufs(struct ks8842_adapter *adapter)
{
	struct ks8842_tx_dma_ctl *tx_ctl = &adapter->dma_tx;
	struct ks8842_rx_dma_ctl *rx_ctl = &adapter->dma_rx;

	ks8842_stop_dma(adapter);

	if (tx_ctl->chan)
		dma_release_channel(tx_ctl->chan);
	tx_ctl->chan = NULL;

	if (rx_ctl->chan)
		dma_release_channel(rx_ctl->chan);
	rx_ctl->chan = NULL;

	tasklet_kill(&rx_ctl->tasklet);

	if (sg_dma_address(&tx_ctl->sg))
		dma_unmap_single(adapter->dev, sg_dma_address(&tx_ctl->sg),
			DMA_BUFFER_SIZE, DMA_TO_DEVICE);
	sg_dma_address(&tx_ctl->sg) = 0;

	kfree(tx_ctl->buf);
	tx_ctl->buf = NULL;
}

static bool ks8842_dma_filter_fn(struct dma_chan *chan, void *filter_param)
{
	return chan->chan_id == (long)filter_param;
}

static int ks8842_alloc_dma_bufs(struct net_device *netdev)
{
	struct ks8842_adapter *adapter = netdev_priv(netdev);
	struct ks8842_tx_dma_ctl *tx_ctl = &adapter->dma_tx;
	struct ks8842_rx_dma_ctl *rx_ctl = &adapter->dma_rx;
	int err;

	dma_cap_mask_t mask;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_cap_set(DMA_PRIVATE, mask);

	sg_init_table(&tx_ctl->sg, 1);

	tx_ctl->chan = dma_request_channel(mask, ks8842_dma_filter_fn,
					   (void *)(long)tx_ctl->channel);
	if (!tx_ctl->chan) {
		err = -ENODEV;
		goto err;
	}

	/* allocate DMA buffer */
	tx_ctl->buf = kmalloc(DMA_BUFFER_SIZE, GFP_KERNEL);
	if (!tx_ctl->buf) {
		err = -ENOMEM;
		goto err;
	}

	sg_dma_address(&tx_ctl->sg) = dma_map_single(adapter->dev,
		tx_ctl->buf, DMA_BUFFER_SIZE, DMA_TO_DEVICE);
	err = dma_mapping_error(adapter->dev,
		sg_dma_address(&tx_ctl->sg));
	if (err) {
		sg_dma_address(&tx_ctl->sg) = 0;
		goto err;
	}

	rx_ctl->chan = dma_request_channel(mask, ks8842_dma_filter_fn,
					   (void *)(long)rx_ctl->channel);
	if (!rx_ctl->chan) {
		err = -ENODEV;
		goto err;
	}

	tasklet_init(&rx_ctl->tasklet, ks8842_rx_frame_dma_tasklet,
		(unsigned long)netdev);

	return 0;
err:
	ks8842_dealloc_dma_bufs(adapter);
	return err;
}

/* Netdevice operations */

static int ks8842_open(struct net_device *netdev)
{
	struct ks8842_adapter *adapter = netdev_priv(netdev);
	int err;

	netdev_dbg(netdev, "%s - entry\n", __func__);

	if (KS8842_USE_DMA(adapter)) {
		err = ks8842_alloc_dma_bufs(netdev);

		if (!err) {
			/* start RX dma */
			err = __ks8842_start_new_rx_dma(netdev);
			if (err)
				ks8842_dealloc_dma_bufs(adapter);
		}

		if (err) {
			printk(KERN_WARNING DRV_NAME
				": Failed to initiate DMA, running PIO\n");
			ks8842_dealloc_dma_bufs(adapter);
			adapter->dma_rx.channel = -1;
			adapter->dma_tx.channel = -1;
		}
	}

	/* reset the HW */
	ks8842_reset_hw(adapter);

	ks8842_write_mac_addr(adapter, netdev->dev_addr);

	ks8842_update_link_status(netdev, adapter);

	err = request_irq(adapter->irq, ks8842_irq, IRQF_SHARED, DRV_NAME,
		netdev);
	if (err) {
		pr_err("Failed to request IRQ: %d: %d\n", adapter->irq, err);
		return err;
	}

	return 0;
}

static int ks8842_close(struct net_device *netdev)
{
	struct ks8842_adapter *adapter = netdev_priv(netdev);

	netdev_dbg(netdev, "%s - entry\n", __func__);

	cancel_work_sync(&adapter->timeout_work);

	if (KS8842_USE_DMA(adapter))
		ks8842_dealloc_dma_bufs(adapter);

	/* free the irq */
	free_irq(adapter->irq, netdev);

	/* disable the switch */
	ks8842_write16(adapter, 32, 0x0, REG_SW_ID_AND_ENABLE);

	return 0;
}

static netdev_tx_t ks8842_xmit_frame(struct sk_buff *skb,
				     struct net_device *netdev)
{
	int ret;
	struct ks8842_adapter *adapter = netdev_priv(netdev);

	netdev_dbg(netdev, "%s: entry\n", __func__);

	if (KS8842_USE_DMA(adapter)) {
		unsigned long flags;
		ret = ks8842_tx_frame_dma(skb, netdev);
		/* for now only allow one transfer at the time */
		spin_lock_irqsave(&adapter->lock, flags);
		if (adapter->dma_tx.adesc)
			netif_stop_queue(netdev);
		spin_unlock_irqrestore(&adapter->lock, flags);
		return ret;
	}

	ret = ks8842_tx_frame(skb, netdev);

	if (ks8842_tx_fifo_space(adapter) <  netdev->mtu + 8)
		netif_stop_queue(netdev);

	return ret;
}

static int ks8842_set_mac(struct net_device *netdev, void *p)
{
	struct ks8842_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *addr = p;
	char *mac = (u8 *)addr->sa_data;

	netdev_dbg(netdev, "%s: entry\n", __func__);

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	netdev->addr_assign_type &= ~NET_ADDR_RANDOM;
	memcpy(netdev->dev_addr, mac, netdev->addr_len);

	ks8842_write_mac_addr(adapter, mac);
	return 0;
}

static void ks8842_tx_timeout_work(struct work_struct *work)
{
	struct ks8842_adapter *adapter =
		container_of(work, struct ks8842_adapter, timeout_work);
	struct net_device *netdev = adapter->netdev;
	unsigned long flags;

	netdev_dbg(netdev, "%s: entry\n", __func__);

	spin_lock_irqsave(&adapter->lock, flags);

	if (KS8842_USE_DMA(adapter))
		ks8842_stop_dma(adapter);

	/* disable interrupts */
	ks8842_write16(adapter, 18, 0, REG_IER);
	ks8842_write16(adapter, 18, 0xFFFF, REG_ISR);

	netif_stop_queue(netdev);

	spin_unlock_irqrestore(&adapter->lock, flags);

	ks8842_reset_hw(adapter);

	ks8842_write_mac_addr(adapter, netdev->dev_addr);

	ks8842_update_link_status(netdev, adapter);

	if (KS8842_USE_DMA(adapter))
		__ks8842_start_new_rx_dma(netdev);
}

static void ks8842_tx_timeout(struct net_device *netdev)
{
	struct ks8842_adapter *adapter = netdev_priv(netdev);

	netdev_dbg(netdev, "%s: entry\n", __func__);

	schedule_work(&adapter->timeout_work);
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

static int ks8842_probe(struct platform_device *pdev)
{
	int err = -ENOMEM;
	struct resource *iomem;
	struct net_device *netdev;
	struct ks8842_adapter *adapter;
	struct ks8842_platform_data *pdata = pdev->dev.platform_data;
	u16 id;
	unsigned i;

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!request_mem_region(iomem->start, resource_size(iomem), DRV_NAME))
		goto err_mem_region;

	netdev = alloc_etherdev(sizeof(struct ks8842_adapter));
	if (!netdev)
		goto err_alloc_etherdev;

	SET_NETDEV_DEV(netdev, &pdev->dev);

	adapter = netdev_priv(netdev);
	adapter->netdev = netdev;
	INIT_WORK(&adapter->timeout_work, ks8842_tx_timeout_work);
	adapter->hw_addr = ioremap(iomem->start, resource_size(iomem));
	adapter->conf_flags = iomem->flags;

	if (!adapter->hw_addr)
		goto err_ioremap;

	adapter->irq = platform_get_irq(pdev, 0);
	if (adapter->irq < 0) {
		err = adapter->irq;
		goto err_get_irq;
	}

	adapter->dev = (pdev->dev.parent) ? pdev->dev.parent : &pdev->dev;

	/* DMA is only supported when accessed via timberdale */
	if (!(adapter->conf_flags & MICREL_KS884X) && pdata &&
		(pdata->tx_dma_channel != -1) &&
		(pdata->rx_dma_channel != -1)) {
		adapter->dma_rx.channel = pdata->rx_dma_channel;
		adapter->dma_tx.channel = pdata->tx_dma_channel;
	} else {
		adapter->dma_rx.channel = -1;
		adapter->dma_tx.channel = -1;
	}

	tasklet_init(&adapter->tasklet, ks8842_tasklet, (unsigned long)netdev);
	spin_lock_init(&adapter->lock);

	netdev->netdev_ops = &ks8842_netdev_ops;
	netdev->ethtool_ops = &ks8842_ethtool_ops;

	/* Check if a mac address was given */
	i = netdev->addr_len;
	if (pdata) {
		for (i = 0; i < netdev->addr_len; i++)
			if (pdata->macaddr[i] != 0)
				break;

		if (i < netdev->addr_len)
			/* an address was passed, use it */
			memcpy(netdev->dev_addr, pdata->macaddr,
				netdev->addr_len);
	}

	if (i == netdev->addr_len) {
		ks8842_read_mac_addr(adapter, netdev->dev_addr);

		if (!is_valid_ether_addr(netdev->dev_addr))
			eth_hw_addr_random(netdev);
	}

	id = ks8842_read16(adapter, 32, REG_SW_ID_AND_ENABLE);

	strcpy(netdev->name, "eth%d");
	err = register_netdev(netdev);
	if (err)
		goto err_register;

	platform_set_drvdata(pdev, netdev);

	pr_info("Found chip, family: 0x%x, id: 0x%x, rev: 0x%x\n",
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

static int ks8842_remove(struct platform_device *pdev)
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

module_platform_driver(ks8842_platform_driver);

MODULE_DESCRIPTION("Timberdale KS8842 ethernet driver");
MODULE_AUTHOR("Mocean Laboratories <info@mocean-labs.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:ks8842");

