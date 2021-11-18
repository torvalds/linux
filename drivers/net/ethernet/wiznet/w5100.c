// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Ethernet driver for the WIZnet W5100 chip.
 *
 * Copyright (C) 2006-2008 WIZnet Co.,Ltd.
 * Copyright (C) 2012 Mike Sinkovsky <msink@permonline.ru>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/platform_data/wiznet.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>

#include "w5100.h"

#define DRV_NAME	"w5100"
#define DRV_VERSION	"2012-04-04"

MODULE_DESCRIPTION("WIZnet W5100 Ethernet driver v"DRV_VERSION);
MODULE_AUTHOR("Mike Sinkovsky <msink@permonline.ru>");
MODULE_ALIAS("platform:"DRV_NAME);
MODULE_LICENSE("GPL");

/*
 * W5100/W5200/W5500 common registers
 */
#define W5100_COMMON_REGS	0x0000
#define W5100_MR		0x0000 /* Mode Register */
#define   MR_RST		  0x80 /* S/W reset */
#define   MR_PB			  0x10 /* Ping block */
#define   MR_AI			  0x02 /* Address Auto-Increment */
#define   MR_IND		  0x01 /* Indirect mode */
#define W5100_SHAR		0x0009 /* Source MAC address */
#define W5100_IR		0x0015 /* Interrupt Register */
#define W5100_COMMON_REGS_LEN	0x0040

#define W5100_Sn_MR		0x0000 /* Sn Mode Register */
#define W5100_Sn_CR		0x0001 /* Sn Command Register */
#define W5100_Sn_IR		0x0002 /* Sn Interrupt Register */
#define W5100_Sn_SR		0x0003 /* Sn Status Register */
#define W5100_Sn_TX_FSR		0x0020 /* Sn Transmit free memory size */
#define W5100_Sn_TX_RD		0x0022 /* Sn Transmit memory read pointer */
#define W5100_Sn_TX_WR		0x0024 /* Sn Transmit memory write pointer */
#define W5100_Sn_RX_RSR		0x0026 /* Sn Receive free memory size */
#define W5100_Sn_RX_RD		0x0028 /* Sn Receive memory read pointer */

#define S0_REGS(priv)		((priv)->s0_regs)

#define W5100_S0_MR(priv)	(S0_REGS(priv) + W5100_Sn_MR)
#define   S0_MR_MACRAW		  0x04 /* MAC RAW mode */
#define   S0_MR_MF		  0x40 /* MAC Filter for W5100 and W5200 */
#define   W5500_S0_MR_MF	  0x80 /* MAC Filter for W5500 */
#define W5100_S0_CR(priv)	(S0_REGS(priv) + W5100_Sn_CR)
#define   S0_CR_OPEN		  0x01 /* OPEN command */
#define   S0_CR_CLOSE		  0x10 /* CLOSE command */
#define   S0_CR_SEND		  0x20 /* SEND command */
#define   S0_CR_RECV		  0x40 /* RECV command */
#define W5100_S0_IR(priv)	(S0_REGS(priv) + W5100_Sn_IR)
#define   S0_IR_SENDOK		  0x10 /* complete sending */
#define   S0_IR_RECV		  0x04 /* receiving data */
#define W5100_S0_SR(priv)	(S0_REGS(priv) + W5100_Sn_SR)
#define   S0_SR_MACRAW		  0x42 /* mac raw mode */
#define W5100_S0_TX_FSR(priv)	(S0_REGS(priv) + W5100_Sn_TX_FSR)
#define W5100_S0_TX_RD(priv)	(S0_REGS(priv) + W5100_Sn_TX_RD)
#define W5100_S0_TX_WR(priv)	(S0_REGS(priv) + W5100_Sn_TX_WR)
#define W5100_S0_RX_RSR(priv)	(S0_REGS(priv) + W5100_Sn_RX_RSR)
#define W5100_S0_RX_RD(priv)	(S0_REGS(priv) + W5100_Sn_RX_RD)

#define W5100_S0_REGS_LEN	0x0040

/*
 * W5100 and W5200 common registers
 */
#define W5100_IMR		0x0016 /* Interrupt Mask Register */
#define   IR_S0			  0x01 /* S0 interrupt */
#define W5100_RTR		0x0017 /* Retry Time-value Register */
#define   RTR_DEFAULT		  2000 /* =0x07d0 (2000) */

/*
 * W5100 specific register and memory
 */
#define W5100_RMSR		0x001a /* Receive Memory Size */
#define W5100_TMSR		0x001b /* Transmit Memory Size */

#define W5100_S0_REGS		0x0400

#define W5100_TX_MEM_START	0x4000
#define W5100_TX_MEM_SIZE	0x2000
#define W5100_RX_MEM_START	0x6000
#define W5100_RX_MEM_SIZE	0x2000

/*
 * W5200 specific register and memory
 */
#define W5200_S0_REGS		0x4000

#define W5200_Sn_RXMEM_SIZE(n)	(0x401e + (n) * 0x0100) /* Sn RX Memory Size */
#define W5200_Sn_TXMEM_SIZE(n)	(0x401f + (n) * 0x0100) /* Sn TX Memory Size */

#define W5200_TX_MEM_START	0x8000
#define W5200_TX_MEM_SIZE	0x4000
#define W5200_RX_MEM_START	0xc000
#define W5200_RX_MEM_SIZE	0x4000

/*
 * W5500 specific register and memory
 *
 * W5500 register and memory are organized by multiple blocks.  Each one is
 * selected by 16bits offset address and 5bits block select bits.  So we
 * encode it into 32bits address. (lower 16bits is offset address and
 * upper 16bits is block select bits)
 */
#define W5500_SIMR		0x0018 /* Socket Interrupt Mask Register */
#define W5500_RTR		0x0019 /* Retry Time-value Register */

#define W5500_S0_REGS		0x10000

#define W5500_Sn_RXMEM_SIZE(n)	\
		(0x1001e + (n) * 0x40000) /* Sn RX Memory Size */
#define W5500_Sn_TXMEM_SIZE(n)	\
		(0x1001f + (n) * 0x40000) /* Sn TX Memory Size */

#define W5500_TX_MEM_START	0x20000
#define W5500_TX_MEM_SIZE	0x04000
#define W5500_RX_MEM_START	0x30000
#define W5500_RX_MEM_SIZE	0x04000

/*
 * Device driver private data structure
 */

struct w5100_priv {
	const struct w5100_ops *ops;

	/* Socket 0 register offset address */
	u32 s0_regs;
	/* Socket 0 TX buffer offset address and size */
	u32 s0_tx_buf;
	u16 s0_tx_buf_size;
	/* Socket 0 RX buffer offset address and size */
	u32 s0_rx_buf;
	u16 s0_rx_buf_size;

	int irq;
	int link_irq;
	int link_gpio;

	struct napi_struct napi;
	struct net_device *ndev;
	bool promisc;
	u32 msg_enable;

	struct workqueue_struct *xfer_wq;
	struct work_struct rx_work;
	struct sk_buff *tx_skb;
	struct work_struct tx_work;
	struct work_struct setrx_work;
	struct work_struct restart_work;
};

/************************************************************************
 *
 *  Lowlevel I/O functions
 *
 ***********************************************************************/

struct w5100_mmio_priv {
	void __iomem *base;
	/* Serialize access in indirect address mode */
	spinlock_t reg_lock;
};

static inline struct w5100_mmio_priv *w5100_mmio_priv(struct net_device *dev)
{
	return w5100_ops_priv(dev);
}

static inline void __iomem *w5100_mmio(struct net_device *ndev)
{
	struct w5100_mmio_priv *mmio_priv = w5100_mmio_priv(ndev);

	return mmio_priv->base;
}

/*
 * In direct address mode host system can directly access W5100 registers
 * after mapping to Memory-Mapped I/O space.
 *
 * 0x8000 bytes are required for memory space.
 */
static inline int w5100_read_direct(struct net_device *ndev, u32 addr)
{
	return ioread8(w5100_mmio(ndev) + (addr << CONFIG_WIZNET_BUS_SHIFT));
}

static inline int __w5100_write_direct(struct net_device *ndev, u32 addr,
				       u8 data)
{
	iowrite8(data, w5100_mmio(ndev) + (addr << CONFIG_WIZNET_BUS_SHIFT));

	return 0;
}

static inline int w5100_write_direct(struct net_device *ndev, u32 addr, u8 data)
{
	__w5100_write_direct(ndev, addr, data);

	return 0;
}

static int w5100_read16_direct(struct net_device *ndev, u32 addr)
{
	u16 data;
	data  = w5100_read_direct(ndev, addr) << 8;
	data |= w5100_read_direct(ndev, addr + 1);
	return data;
}

static int w5100_write16_direct(struct net_device *ndev, u32 addr, u16 data)
{
	__w5100_write_direct(ndev, addr, data >> 8);
	__w5100_write_direct(ndev, addr + 1, data);

	return 0;
}

static int w5100_readbulk_direct(struct net_device *ndev, u32 addr, u8 *buf,
				 int len)
{
	int i;

	for (i = 0; i < len; i++, addr++)
		*buf++ = w5100_read_direct(ndev, addr);

	return 0;
}

static int w5100_writebulk_direct(struct net_device *ndev, u32 addr,
				  const u8 *buf, int len)
{
	int i;

	for (i = 0; i < len; i++, addr++)
		__w5100_write_direct(ndev, addr, *buf++);

	return 0;
}

static int w5100_mmio_init(struct net_device *ndev)
{
	struct platform_device *pdev = to_platform_device(ndev->dev.parent);
	struct w5100_mmio_priv *mmio_priv = w5100_mmio_priv(ndev);

	spin_lock_init(&mmio_priv->reg_lock);

	mmio_priv->base = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(mmio_priv->base))
		return PTR_ERR(mmio_priv->base);

	return 0;
}

static const struct w5100_ops w5100_mmio_direct_ops = {
	.chip_id = W5100,
	.read = w5100_read_direct,
	.write = w5100_write_direct,
	.read16 = w5100_read16_direct,
	.write16 = w5100_write16_direct,
	.readbulk = w5100_readbulk_direct,
	.writebulk = w5100_writebulk_direct,
	.init = w5100_mmio_init,
};

/*
 * In indirect address mode host system indirectly accesses registers by
 * using Indirect Mode Address Register (IDM_AR) and Indirect Mode Data
 * Register (IDM_DR), which are directly mapped to Memory-Mapped I/O space.
 * Mode Register (MR) is directly accessible.
 *
 * Only 0x04 bytes are required for memory space.
 */
#define W5100_IDM_AR		0x01   /* Indirect Mode Address Register */
#define W5100_IDM_DR		0x03   /* Indirect Mode Data Register */

static int w5100_read_indirect(struct net_device *ndev, u32 addr)
{
	struct w5100_mmio_priv *mmio_priv = w5100_mmio_priv(ndev);
	unsigned long flags;
	u8 data;

	spin_lock_irqsave(&mmio_priv->reg_lock, flags);
	w5100_write16_direct(ndev, W5100_IDM_AR, addr);
	data = w5100_read_direct(ndev, W5100_IDM_DR);
	spin_unlock_irqrestore(&mmio_priv->reg_lock, flags);

	return data;
}

static int w5100_write_indirect(struct net_device *ndev, u32 addr, u8 data)
{
	struct w5100_mmio_priv *mmio_priv = w5100_mmio_priv(ndev);
	unsigned long flags;

	spin_lock_irqsave(&mmio_priv->reg_lock, flags);
	w5100_write16_direct(ndev, W5100_IDM_AR, addr);
	w5100_write_direct(ndev, W5100_IDM_DR, data);
	spin_unlock_irqrestore(&mmio_priv->reg_lock, flags);

	return 0;
}

static int w5100_read16_indirect(struct net_device *ndev, u32 addr)
{
	struct w5100_mmio_priv *mmio_priv = w5100_mmio_priv(ndev);
	unsigned long flags;
	u16 data;

	spin_lock_irqsave(&mmio_priv->reg_lock, flags);
	w5100_write16_direct(ndev, W5100_IDM_AR, addr);
	data  = w5100_read_direct(ndev, W5100_IDM_DR) << 8;
	data |= w5100_read_direct(ndev, W5100_IDM_DR);
	spin_unlock_irqrestore(&mmio_priv->reg_lock, flags);

	return data;
}

static int w5100_write16_indirect(struct net_device *ndev, u32 addr, u16 data)
{
	struct w5100_mmio_priv *mmio_priv = w5100_mmio_priv(ndev);
	unsigned long flags;

	spin_lock_irqsave(&mmio_priv->reg_lock, flags);
	w5100_write16_direct(ndev, W5100_IDM_AR, addr);
	__w5100_write_direct(ndev, W5100_IDM_DR, data >> 8);
	w5100_write_direct(ndev, W5100_IDM_DR, data);
	spin_unlock_irqrestore(&mmio_priv->reg_lock, flags);

	return 0;
}

static int w5100_readbulk_indirect(struct net_device *ndev, u32 addr, u8 *buf,
				   int len)
{
	struct w5100_mmio_priv *mmio_priv = w5100_mmio_priv(ndev);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&mmio_priv->reg_lock, flags);
	w5100_write16_direct(ndev, W5100_IDM_AR, addr);

	for (i = 0; i < len; i++)
		*buf++ = w5100_read_direct(ndev, W5100_IDM_DR);

	spin_unlock_irqrestore(&mmio_priv->reg_lock, flags);

	return 0;
}

static int w5100_writebulk_indirect(struct net_device *ndev, u32 addr,
				    const u8 *buf, int len)
{
	struct w5100_mmio_priv *mmio_priv = w5100_mmio_priv(ndev);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&mmio_priv->reg_lock, flags);
	w5100_write16_direct(ndev, W5100_IDM_AR, addr);

	for (i = 0; i < len; i++)
		__w5100_write_direct(ndev, W5100_IDM_DR, *buf++);

	spin_unlock_irqrestore(&mmio_priv->reg_lock, flags);

	return 0;
}

static int w5100_reset_indirect(struct net_device *ndev)
{
	w5100_write_direct(ndev, W5100_MR, MR_RST);
	mdelay(5);
	w5100_write_direct(ndev, W5100_MR, MR_PB | MR_AI | MR_IND);

	return 0;
}

static const struct w5100_ops w5100_mmio_indirect_ops = {
	.chip_id = W5100,
	.read = w5100_read_indirect,
	.write = w5100_write_indirect,
	.read16 = w5100_read16_indirect,
	.write16 = w5100_write16_indirect,
	.readbulk = w5100_readbulk_indirect,
	.writebulk = w5100_writebulk_indirect,
	.init = w5100_mmio_init,
	.reset = w5100_reset_indirect,
};

#if defined(CONFIG_WIZNET_BUS_DIRECT)

static int w5100_read(struct w5100_priv *priv, u32 addr)
{
	return w5100_read_direct(priv->ndev, addr);
}

static int w5100_write(struct w5100_priv *priv, u32 addr, u8 data)
{
	return w5100_write_direct(priv->ndev, addr, data);
}

static int w5100_read16(struct w5100_priv *priv, u32 addr)
{
	return w5100_read16_direct(priv->ndev, addr);
}

static int w5100_write16(struct w5100_priv *priv, u32 addr, u16 data)
{
	return w5100_write16_direct(priv->ndev, addr, data);
}

static int w5100_readbulk(struct w5100_priv *priv, u32 addr, u8 *buf, int len)
{
	return w5100_readbulk_direct(priv->ndev, addr, buf, len);
}

static int w5100_writebulk(struct w5100_priv *priv, u32 addr, const u8 *buf,
			   int len)
{
	return w5100_writebulk_direct(priv->ndev, addr, buf, len);
}

#elif defined(CONFIG_WIZNET_BUS_INDIRECT)

static int w5100_read(struct w5100_priv *priv, u32 addr)
{
	return w5100_read_indirect(priv->ndev, addr);
}

static int w5100_write(struct w5100_priv *priv, u32 addr, u8 data)
{
	return w5100_write_indirect(priv->ndev, addr, data);
}

static int w5100_read16(struct w5100_priv *priv, u32 addr)
{
	return w5100_read16_indirect(priv->ndev, addr);
}

static int w5100_write16(struct w5100_priv *priv, u32 addr, u16 data)
{
	return w5100_write16_indirect(priv->ndev, addr, data);
}

static int w5100_readbulk(struct w5100_priv *priv, u32 addr, u8 *buf, int len)
{
	return w5100_readbulk_indirect(priv->ndev, addr, buf, len);
}

static int w5100_writebulk(struct w5100_priv *priv, u32 addr, const u8 *buf,
			   int len)
{
	return w5100_writebulk_indirect(priv->ndev, addr, buf, len);
}

#else /* CONFIG_WIZNET_BUS_ANY */

static int w5100_read(struct w5100_priv *priv, u32 addr)
{
	return priv->ops->read(priv->ndev, addr);
}

static int w5100_write(struct w5100_priv *priv, u32 addr, u8 data)
{
	return priv->ops->write(priv->ndev, addr, data);
}

static int w5100_read16(struct w5100_priv *priv, u32 addr)
{
	return priv->ops->read16(priv->ndev, addr);
}

static int w5100_write16(struct w5100_priv *priv, u32 addr, u16 data)
{
	return priv->ops->write16(priv->ndev, addr, data);
}

static int w5100_readbulk(struct w5100_priv *priv, u32 addr, u8 *buf, int len)
{
	return priv->ops->readbulk(priv->ndev, addr, buf, len);
}

static int w5100_writebulk(struct w5100_priv *priv, u32 addr, const u8 *buf,
			   int len)
{
	return priv->ops->writebulk(priv->ndev, addr, buf, len);
}

#endif

static int w5100_readbuf(struct w5100_priv *priv, u16 offset, u8 *buf, int len)
{
	u32 addr;
	int remain = 0;
	int ret;
	const u32 mem_start = priv->s0_rx_buf;
	const u16 mem_size = priv->s0_rx_buf_size;

	offset %= mem_size;
	addr = mem_start + offset;

	if (offset + len > mem_size) {
		remain = (offset + len) % mem_size;
		len = mem_size - offset;
	}

	ret = w5100_readbulk(priv, addr, buf, len);
	if (ret || !remain)
		return ret;

	return w5100_readbulk(priv, mem_start, buf + len, remain);
}

static int w5100_writebuf(struct w5100_priv *priv, u16 offset, const u8 *buf,
			  int len)
{
	u32 addr;
	int ret;
	int remain = 0;
	const u32 mem_start = priv->s0_tx_buf;
	const u16 mem_size = priv->s0_tx_buf_size;

	offset %= mem_size;
	addr = mem_start + offset;

	if (offset + len > mem_size) {
		remain = (offset + len) % mem_size;
		len = mem_size - offset;
	}

	ret = w5100_writebulk(priv, addr, buf, len);
	if (ret || !remain)
		return ret;

	return w5100_writebulk(priv, mem_start, buf + len, remain);
}

static int w5100_reset(struct w5100_priv *priv)
{
	if (priv->ops->reset)
		return priv->ops->reset(priv->ndev);

	w5100_write(priv, W5100_MR, MR_RST);
	mdelay(5);
	w5100_write(priv, W5100_MR, MR_PB);

	return 0;
}

static int w5100_command(struct w5100_priv *priv, u16 cmd)
{
	unsigned long timeout;

	w5100_write(priv, W5100_S0_CR(priv), cmd);

	timeout = jiffies + msecs_to_jiffies(100);

	while (w5100_read(priv, W5100_S0_CR(priv)) != 0) {
		if (time_after(jiffies, timeout))
			return -EIO;
		cpu_relax();
	}

	return 0;
}

static void w5100_write_macaddr(struct w5100_priv *priv)
{
	struct net_device *ndev = priv->ndev;

	w5100_writebulk(priv, W5100_SHAR, ndev->dev_addr, ETH_ALEN);
}

static void w5100_socket_intr_mask(struct w5100_priv *priv, u8 mask)
{
	u32 imr;

	if (priv->ops->chip_id == W5500)
		imr = W5500_SIMR;
	else
		imr = W5100_IMR;

	w5100_write(priv, imr, mask);
}

static void w5100_enable_intr(struct w5100_priv *priv)
{
	w5100_socket_intr_mask(priv, IR_S0);
}

static void w5100_disable_intr(struct w5100_priv *priv)
{
	w5100_socket_intr_mask(priv, 0);
}

static void w5100_memory_configure(struct w5100_priv *priv)
{
	/* Configure 16K of internal memory
	 * as 8K RX buffer and 8K TX buffer
	 */
	w5100_write(priv, W5100_RMSR, 0x03);
	w5100_write(priv, W5100_TMSR, 0x03);
}

static void w5200_memory_configure(struct w5100_priv *priv)
{
	int i;

	/* Configure internal RX memory as 16K RX buffer and
	 * internal TX memory as 16K TX buffer
	 */
	w5100_write(priv, W5200_Sn_RXMEM_SIZE(0), 0x10);
	w5100_write(priv, W5200_Sn_TXMEM_SIZE(0), 0x10);

	for (i = 1; i < 8; i++) {
		w5100_write(priv, W5200_Sn_RXMEM_SIZE(i), 0);
		w5100_write(priv, W5200_Sn_TXMEM_SIZE(i), 0);
	}
}

static void w5500_memory_configure(struct w5100_priv *priv)
{
	int i;

	/* Configure internal RX memory as 16K RX buffer and
	 * internal TX memory as 16K TX buffer
	 */
	w5100_write(priv, W5500_Sn_RXMEM_SIZE(0), 0x10);
	w5100_write(priv, W5500_Sn_TXMEM_SIZE(0), 0x10);

	for (i = 1; i < 8; i++) {
		w5100_write(priv, W5500_Sn_RXMEM_SIZE(i), 0);
		w5100_write(priv, W5500_Sn_TXMEM_SIZE(i), 0);
	}
}

static int w5100_hw_reset(struct w5100_priv *priv)
{
	u32 rtr;

	w5100_reset(priv);

	w5100_disable_intr(priv);
	w5100_write_macaddr(priv);

	switch (priv->ops->chip_id) {
	case W5100:
		w5100_memory_configure(priv);
		rtr = W5100_RTR;
		break;
	case W5200:
		w5200_memory_configure(priv);
		rtr = W5100_RTR;
		break;
	case W5500:
		w5500_memory_configure(priv);
		rtr = W5500_RTR;
		break;
	default:
		return -EINVAL;
	}

	if (w5100_read16(priv, rtr) != RTR_DEFAULT)
		return -ENODEV;

	return 0;
}

static void w5100_hw_start(struct w5100_priv *priv)
{
	u8 mode = S0_MR_MACRAW;

	if (!priv->promisc) {
		if (priv->ops->chip_id == W5500)
			mode |= W5500_S0_MR_MF;
		else
			mode |= S0_MR_MF;
	}

	w5100_write(priv, W5100_S0_MR(priv), mode);
	w5100_command(priv, S0_CR_OPEN);
	w5100_enable_intr(priv);
}

static void w5100_hw_close(struct w5100_priv *priv)
{
	w5100_disable_intr(priv);
	w5100_command(priv, S0_CR_CLOSE);
}

/***********************************************************************
 *
 *   Device driver functions / callbacks
 *
 ***********************************************************************/

static void w5100_get_drvinfo(struct net_device *ndev,
			      struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, dev_name(ndev->dev.parent),
		sizeof(info->bus_info));
}

static u32 w5100_get_link(struct net_device *ndev)
{
	struct w5100_priv *priv = netdev_priv(ndev);

	if (gpio_is_valid(priv->link_gpio))
		return !!gpio_get_value(priv->link_gpio);

	return 1;
}

static u32 w5100_get_msglevel(struct net_device *ndev)
{
	struct w5100_priv *priv = netdev_priv(ndev);

	return priv->msg_enable;
}

static void w5100_set_msglevel(struct net_device *ndev, u32 value)
{
	struct w5100_priv *priv = netdev_priv(ndev);

	priv->msg_enable = value;
}

static int w5100_get_regs_len(struct net_device *ndev)
{
	return W5100_COMMON_REGS_LEN + W5100_S0_REGS_LEN;
}

static void w5100_get_regs(struct net_device *ndev,
			   struct ethtool_regs *regs, void *buf)
{
	struct w5100_priv *priv = netdev_priv(ndev);

	regs->version = 1;
	w5100_readbulk(priv, W5100_COMMON_REGS, buf, W5100_COMMON_REGS_LEN);
	buf += W5100_COMMON_REGS_LEN;
	w5100_readbulk(priv, S0_REGS(priv), buf, W5100_S0_REGS_LEN);
}

static void w5100_restart(struct net_device *ndev)
{
	struct w5100_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);
	w5100_hw_reset(priv);
	w5100_hw_start(priv);
	ndev->stats.tx_errors++;
	netif_trans_update(ndev);
	netif_wake_queue(ndev);
}

static void w5100_restart_work(struct work_struct *work)
{
	struct w5100_priv *priv = container_of(work, struct w5100_priv,
					       restart_work);

	w5100_restart(priv->ndev);
}

static void w5100_tx_timeout(struct net_device *ndev, unsigned int txqueue)
{
	struct w5100_priv *priv = netdev_priv(ndev);

	if (priv->ops->may_sleep)
		schedule_work(&priv->restart_work);
	else
		w5100_restart(ndev);
}

static void w5100_tx_skb(struct net_device *ndev, struct sk_buff *skb)
{
	struct w5100_priv *priv = netdev_priv(ndev);
	u16 offset;

	offset = w5100_read16(priv, W5100_S0_TX_WR(priv));
	w5100_writebuf(priv, offset, skb->data, skb->len);
	w5100_write16(priv, W5100_S0_TX_WR(priv), offset + skb->len);
	ndev->stats.tx_bytes += skb->len;
	ndev->stats.tx_packets++;
	dev_kfree_skb(skb);

	w5100_command(priv, S0_CR_SEND);
}

static void w5100_tx_work(struct work_struct *work)
{
	struct w5100_priv *priv = container_of(work, struct w5100_priv,
					       tx_work);
	struct sk_buff *skb = priv->tx_skb;

	priv->tx_skb = NULL;

	if (WARN_ON(!skb))
		return;
	w5100_tx_skb(priv->ndev, skb);
}

static netdev_tx_t w5100_start_tx(struct sk_buff *skb, struct net_device *ndev)
{
	struct w5100_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);

	if (priv->ops->may_sleep) {
		WARN_ON(priv->tx_skb);
		priv->tx_skb = skb;
		queue_work(priv->xfer_wq, &priv->tx_work);
	} else {
		w5100_tx_skb(ndev, skb);
	}

	return NETDEV_TX_OK;
}

static struct sk_buff *w5100_rx_skb(struct net_device *ndev)
{
	struct w5100_priv *priv = netdev_priv(ndev);
	struct sk_buff *skb;
	u16 rx_len;
	u16 offset;
	u8 header[2];
	u16 rx_buf_len = w5100_read16(priv, W5100_S0_RX_RSR(priv));

	if (rx_buf_len == 0)
		return NULL;

	offset = w5100_read16(priv, W5100_S0_RX_RD(priv));
	w5100_readbuf(priv, offset, header, 2);
	rx_len = get_unaligned_be16(header) - 2;

	skb = netdev_alloc_skb_ip_align(ndev, rx_len);
	if (unlikely(!skb)) {
		w5100_write16(priv, W5100_S0_RX_RD(priv), offset + rx_buf_len);
		w5100_command(priv, S0_CR_RECV);
		ndev->stats.rx_dropped++;
		return NULL;
	}

	skb_put(skb, rx_len);
	w5100_readbuf(priv, offset + 2, skb->data, rx_len);
	w5100_write16(priv, W5100_S0_RX_RD(priv), offset + 2 + rx_len);
	w5100_command(priv, S0_CR_RECV);
	skb->protocol = eth_type_trans(skb, ndev);

	ndev->stats.rx_packets++;
	ndev->stats.rx_bytes += rx_len;

	return skb;
}

static void w5100_rx_work(struct work_struct *work)
{
	struct w5100_priv *priv = container_of(work, struct w5100_priv,
					       rx_work);
	struct sk_buff *skb;

	while ((skb = w5100_rx_skb(priv->ndev)))
		netif_rx_ni(skb);

	w5100_enable_intr(priv);
}

static int w5100_napi_poll(struct napi_struct *napi, int budget)
{
	struct w5100_priv *priv = container_of(napi, struct w5100_priv, napi);
	int rx_count;

	for (rx_count = 0; rx_count < budget; rx_count++) {
		struct sk_buff *skb = w5100_rx_skb(priv->ndev);

		if (skb)
			netif_receive_skb(skb);
		else
			break;
	}

	if (rx_count < budget) {
		napi_complete_done(napi, rx_count);
		w5100_enable_intr(priv);
	}

	return rx_count;
}

static irqreturn_t w5100_interrupt(int irq, void *ndev_instance)
{
	struct net_device *ndev = ndev_instance;
	struct w5100_priv *priv = netdev_priv(ndev);

	int ir = w5100_read(priv, W5100_S0_IR(priv));
	if (!ir)
		return IRQ_NONE;
	w5100_write(priv, W5100_S0_IR(priv), ir);

	if (ir & S0_IR_SENDOK) {
		netif_dbg(priv, tx_done, ndev, "tx done\n");
		netif_wake_queue(ndev);
	}

	if (ir & S0_IR_RECV) {
		w5100_disable_intr(priv);

		if (priv->ops->may_sleep)
			queue_work(priv->xfer_wq, &priv->rx_work);
		else if (napi_schedule_prep(&priv->napi))
			__napi_schedule(&priv->napi);
	}

	return IRQ_HANDLED;
}

static irqreturn_t w5100_detect_link(int irq, void *ndev_instance)
{
	struct net_device *ndev = ndev_instance;
	struct w5100_priv *priv = netdev_priv(ndev);

	if (netif_running(ndev)) {
		if (gpio_get_value(priv->link_gpio) != 0) {
			netif_info(priv, link, ndev, "link is up\n");
			netif_carrier_on(ndev);
		} else {
			netif_info(priv, link, ndev, "link is down\n");
			netif_carrier_off(ndev);
		}
	}

	return IRQ_HANDLED;
}

static void w5100_setrx_work(struct work_struct *work)
{
	struct w5100_priv *priv = container_of(work, struct w5100_priv,
					       setrx_work);

	w5100_hw_start(priv);
}

static void w5100_set_rx_mode(struct net_device *ndev)
{
	struct w5100_priv *priv = netdev_priv(ndev);
	bool set_promisc = (ndev->flags & IFF_PROMISC) != 0;

	if (priv->promisc != set_promisc) {
		priv->promisc = set_promisc;

		if (priv->ops->may_sleep)
			schedule_work(&priv->setrx_work);
		else
			w5100_hw_start(priv);
	}
}

static int w5100_set_macaddr(struct net_device *ndev, void *addr)
{
	struct w5100_priv *priv = netdev_priv(ndev);
	struct sockaddr *sock_addr = addr;

	if (!is_valid_ether_addr(sock_addr->sa_data))
		return -EADDRNOTAVAIL;
	eth_hw_addr_set(ndev, sock_addr->sa_data);
	w5100_write_macaddr(priv);
	return 0;
}

static int w5100_open(struct net_device *ndev)
{
	struct w5100_priv *priv = netdev_priv(ndev);

	netif_info(priv, ifup, ndev, "enabling\n");
	w5100_hw_start(priv);
	napi_enable(&priv->napi);
	netif_start_queue(ndev);
	if (!gpio_is_valid(priv->link_gpio) ||
	    gpio_get_value(priv->link_gpio) != 0)
		netif_carrier_on(ndev);
	return 0;
}

static int w5100_stop(struct net_device *ndev)
{
	struct w5100_priv *priv = netdev_priv(ndev);

	netif_info(priv, ifdown, ndev, "shutting down\n");
	w5100_hw_close(priv);
	netif_carrier_off(ndev);
	netif_stop_queue(ndev);
	napi_disable(&priv->napi);
	return 0;
}

static const struct ethtool_ops w5100_ethtool_ops = {
	.get_drvinfo		= w5100_get_drvinfo,
	.get_msglevel		= w5100_get_msglevel,
	.set_msglevel		= w5100_set_msglevel,
	.get_link		= w5100_get_link,
	.get_regs_len		= w5100_get_regs_len,
	.get_regs		= w5100_get_regs,
};

static const struct net_device_ops w5100_netdev_ops = {
	.ndo_open		= w5100_open,
	.ndo_stop		= w5100_stop,
	.ndo_start_xmit		= w5100_start_tx,
	.ndo_tx_timeout		= w5100_tx_timeout,
	.ndo_set_rx_mode	= w5100_set_rx_mode,
	.ndo_set_mac_address	= w5100_set_macaddr,
	.ndo_validate_addr	= eth_validate_addr,
};

static int w5100_mmio_probe(struct platform_device *pdev)
{
	struct wiznet_platform_data *data = dev_get_platdata(&pdev->dev);
	const void *mac_addr = NULL;
	struct resource *mem;
	const struct w5100_ops *ops;
	int irq;

	if (data && is_valid_ether_addr(data->mac_addr))
		mac_addr = data->mac_addr;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem)
		return -EINVAL;
	if (resource_size(mem) < W5100_BUS_DIRECT_SIZE)
		ops = &w5100_mmio_indirect_ops;
	else
		ops = &w5100_mmio_direct_ops;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	return w5100_probe(&pdev->dev, ops, sizeof(struct w5100_mmio_priv),
			   mac_addr, irq, data ? data->link_gpio : -EINVAL);
}

static int w5100_mmio_remove(struct platform_device *pdev)
{
	w5100_remove(&pdev->dev);

	return 0;
}

void *w5100_ops_priv(const struct net_device *ndev)
{
	return netdev_priv(ndev) +
	       ALIGN(sizeof(struct w5100_priv), NETDEV_ALIGN);
}
EXPORT_SYMBOL_GPL(w5100_ops_priv);

int w5100_probe(struct device *dev, const struct w5100_ops *ops,
		int sizeof_ops_priv, const void *mac_addr, int irq,
		int link_gpio)
{
	struct w5100_priv *priv;
	struct net_device *ndev;
	int err;
	size_t alloc_size;

	alloc_size = sizeof(*priv);
	if (sizeof_ops_priv) {
		alloc_size = ALIGN(alloc_size, NETDEV_ALIGN);
		alloc_size += sizeof_ops_priv;
	}
	alloc_size += NETDEV_ALIGN - 1;

	ndev = alloc_etherdev(alloc_size);
	if (!ndev)
		return -ENOMEM;
	SET_NETDEV_DEV(ndev, dev);
	dev_set_drvdata(dev, ndev);
	priv = netdev_priv(ndev);

	switch (ops->chip_id) {
	case W5100:
		priv->s0_regs = W5100_S0_REGS;
		priv->s0_tx_buf = W5100_TX_MEM_START;
		priv->s0_tx_buf_size = W5100_TX_MEM_SIZE;
		priv->s0_rx_buf = W5100_RX_MEM_START;
		priv->s0_rx_buf_size = W5100_RX_MEM_SIZE;
		break;
	case W5200:
		priv->s0_regs = W5200_S0_REGS;
		priv->s0_tx_buf = W5200_TX_MEM_START;
		priv->s0_tx_buf_size = W5200_TX_MEM_SIZE;
		priv->s0_rx_buf = W5200_RX_MEM_START;
		priv->s0_rx_buf_size = W5200_RX_MEM_SIZE;
		break;
	case W5500:
		priv->s0_regs = W5500_S0_REGS;
		priv->s0_tx_buf = W5500_TX_MEM_START;
		priv->s0_tx_buf_size = W5500_TX_MEM_SIZE;
		priv->s0_rx_buf = W5500_RX_MEM_START;
		priv->s0_rx_buf_size = W5500_RX_MEM_SIZE;
		break;
	default:
		err = -EINVAL;
		goto err_register;
	}

	priv->ndev = ndev;
	priv->ops = ops;
	priv->irq = irq;
	priv->link_gpio = link_gpio;

	ndev->netdev_ops = &w5100_netdev_ops;
	ndev->ethtool_ops = &w5100_ethtool_ops;
	netif_napi_add(ndev, &priv->napi, w5100_napi_poll, 16);

	/* This chip doesn't support VLAN packets with normal MTU,
	 * so disable VLAN for this device.
	 */
	ndev->features |= NETIF_F_VLAN_CHALLENGED;

	err = register_netdev(ndev);
	if (err < 0)
		goto err_register;

	priv->xfer_wq = alloc_workqueue("%s", WQ_MEM_RECLAIM, 0,
					netdev_name(ndev));
	if (!priv->xfer_wq) {
		err = -ENOMEM;
		goto err_wq;
	}

	INIT_WORK(&priv->rx_work, w5100_rx_work);
	INIT_WORK(&priv->tx_work, w5100_tx_work);
	INIT_WORK(&priv->setrx_work, w5100_setrx_work);
	INIT_WORK(&priv->restart_work, w5100_restart_work);

	if (mac_addr)
		eth_hw_addr_set(ndev, mac_addr);
	else
		eth_hw_addr_random(ndev);

	if (priv->ops->init) {
		err = priv->ops->init(priv->ndev);
		if (err)
			goto err_hw;
	}

	err = w5100_hw_reset(priv);
	if (err)
		goto err_hw;

	if (ops->may_sleep) {
		err = request_threaded_irq(priv->irq, NULL, w5100_interrupt,
					   IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					   netdev_name(ndev), ndev);
	} else {
		err = request_irq(priv->irq, w5100_interrupt,
				  IRQF_TRIGGER_LOW, netdev_name(ndev), ndev);
	}
	if (err)
		goto err_hw;

	if (gpio_is_valid(priv->link_gpio)) {
		char *link_name = devm_kzalloc(dev, 16, GFP_KERNEL);

		if (!link_name) {
			err = -ENOMEM;
			goto err_gpio;
		}
		snprintf(link_name, 16, "%s-link", netdev_name(ndev));
		priv->link_irq = gpio_to_irq(priv->link_gpio);
		if (request_any_context_irq(priv->link_irq, w5100_detect_link,
					    IRQF_TRIGGER_RISING |
					    IRQF_TRIGGER_FALLING,
					    link_name, priv->ndev) < 0)
			priv->link_gpio = -EINVAL;
	}

	return 0;

err_gpio:
	free_irq(priv->irq, ndev);
err_hw:
	destroy_workqueue(priv->xfer_wq);
err_wq:
	unregister_netdev(ndev);
err_register:
	free_netdev(ndev);
	return err;
}
EXPORT_SYMBOL_GPL(w5100_probe);

void w5100_remove(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct w5100_priv *priv = netdev_priv(ndev);

	w5100_hw_reset(priv);
	free_irq(priv->irq, ndev);
	if (gpio_is_valid(priv->link_gpio))
		free_irq(priv->link_irq, ndev);

	flush_work(&priv->setrx_work);
	flush_work(&priv->restart_work);
	destroy_workqueue(priv->xfer_wq);

	unregister_netdev(ndev);
	free_netdev(ndev);
}
EXPORT_SYMBOL_GPL(w5100_remove);

#ifdef CONFIG_PM_SLEEP
static int w5100_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct w5100_priv *priv = netdev_priv(ndev);

	if (netif_running(ndev)) {
		netif_carrier_off(ndev);
		netif_device_detach(ndev);

		w5100_hw_close(priv);
	}
	return 0;
}

static int w5100_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct w5100_priv *priv = netdev_priv(ndev);

	if (netif_running(ndev)) {
		w5100_hw_reset(priv);
		w5100_hw_start(priv);

		netif_device_attach(ndev);
		if (!gpio_is_valid(priv->link_gpio) ||
		    gpio_get_value(priv->link_gpio) != 0)
			netif_carrier_on(ndev);
	}
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

SIMPLE_DEV_PM_OPS(w5100_pm_ops, w5100_suspend, w5100_resume);
EXPORT_SYMBOL_GPL(w5100_pm_ops);

static struct platform_driver w5100_mmio_driver = {
	.driver		= {
		.name	= DRV_NAME,
		.pm	= &w5100_pm_ops,
	},
	.probe		= w5100_mmio_probe,
	.remove		= w5100_mmio_remove,
};
module_platform_driver(w5100_mmio_driver);
