/*
 * Ethernet driver for the WIZnet W5100 chip.
 *
 * Copyright (C) 2006-2008 WIZnet Co.,Ltd.
 * Copyright (C) 2012 Mike Sinkovsky <msink@permonline.ru>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kconfig.h>
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

#define DRV_NAME	"w5100"
#define DRV_VERSION	"2012-04-04"

MODULE_DESCRIPTION("WIZnet W5100 Ethernet driver v"DRV_VERSION);
MODULE_AUTHOR("Mike Sinkovsky <msink@permonline.ru>");
MODULE_ALIAS("platform:"DRV_NAME);
MODULE_LICENSE("GPL");

/*
 * Registers
 */
#define W5100_COMMON_REGS	0x0000
#define W5100_MR		0x0000 /* Mode Register */
#define   MR_RST		  0x80 /* S/W reset */
#define   MR_PB			  0x10 /* Ping block */
#define   MR_AI			  0x02 /* Address Auto-Increment */
#define   MR_IND		  0x01 /* Indirect mode */
#define W5100_SHAR		0x0009 /* Source MAC address */
#define W5100_IR		0x0015 /* Interrupt Register */
#define W5100_IMR		0x0016 /* Interrupt Mask Register */
#define   IR_S0			  0x01 /* S0 interrupt */
#define W5100_RTR		0x0017 /* Retry Time-value Register */
#define   RTR_DEFAULT		  2000 /* =0x07d0 (2000) */
#define W5100_RMSR		0x001a /* Receive Memory Size */
#define W5100_TMSR		0x001b /* Transmit Memory Size */
#define W5100_COMMON_REGS_LEN	0x0040

#define W5100_S0_REGS		0x0400
#define W5100_S0_MR		0x0400 /* S0 Mode Register */
#define   S0_MR_MACRAW		  0x04 /* MAC RAW mode (promiscous) */
#define   S0_MR_MACRAW_MF	  0x44 /* MAC RAW mode (filtered) */
#define W5100_S0_CR		0x0401 /* S0 Command Register */
#define   S0_CR_OPEN		  0x01 /* OPEN command */
#define   S0_CR_CLOSE		  0x10 /* CLOSE command */
#define   S0_CR_SEND		  0x20 /* SEND command */
#define   S0_CR_RECV		  0x40 /* RECV command */
#define W5100_S0_IR		0x0402 /* S0 Interrupt Register */
#define   S0_IR_SENDOK		  0x10 /* complete sending */
#define   S0_IR_RECV		  0x04 /* receiving data */
#define W5100_S0_SR		0x0403 /* S0 Status Register */
#define   S0_SR_MACRAW		  0x42 /* mac raw mode */
#define W5100_S0_TX_FSR		0x0420 /* S0 Transmit free memory size */
#define W5100_S0_TX_RD		0x0422 /* S0 Transmit memory read pointer */
#define W5100_S0_TX_WR		0x0424 /* S0 Transmit memory write pointer */
#define W5100_S0_RX_RSR		0x0426 /* S0 Receive free memory size */
#define W5100_S0_RX_RD		0x0428 /* S0 Receive memory read pointer */
#define W5100_S0_REGS_LEN	0x0040

#define W5100_TX_MEM_START	0x4000
#define W5100_TX_MEM_END	0x5fff
#define W5100_TX_MEM_MASK	0x1fff
#define W5100_RX_MEM_START	0x6000
#define W5100_RX_MEM_END	0x7fff
#define W5100_RX_MEM_MASK	0x1fff

/*
 * Device driver private data structure
 */
struct w5100_priv {
	void __iomem *base;
	spinlock_t reg_lock;
	bool indirect;
	u8   (*read)(struct w5100_priv *priv, u16 addr);
	void (*write)(struct w5100_priv *priv, u16 addr, u8 data);
	u16  (*read16)(struct w5100_priv *priv, u16 addr);
	void (*write16)(struct w5100_priv *priv, u16 addr, u16 data);
	void (*readbuf)(struct w5100_priv *priv, u16 addr, u8 *buf, int len);
	void (*writebuf)(struct w5100_priv *priv, u16 addr, u8 *buf, int len);
	int irq;
	int link_irq;
	int link_gpio;

	struct napi_struct napi;
	struct net_device *ndev;
	bool promisc;
	u32 msg_enable;
};

/************************************************************************
 *
 *  Lowlevel I/O functions
 *
 ***********************************************************************/

/*
 * In direct address mode host system can directly access W5100 registers
 * after mapping to Memory-Mapped I/O space.
 *
 * 0x8000 bytes are required for memory space.
 */
static inline u8 w5100_read_direct(struct w5100_priv *priv, u16 addr)
{
	return ioread8(priv->base + (addr << CONFIG_WIZNET_BUS_SHIFT));
}

static inline void w5100_write_direct(struct w5100_priv *priv,
				      u16 addr, u8 data)
{
	iowrite8(data, priv->base + (addr << CONFIG_WIZNET_BUS_SHIFT));
}

static u16 w5100_read16_direct(struct w5100_priv *priv, u16 addr)
{
	u16 data;
	data  = w5100_read_direct(priv, addr) << 8;
	data |= w5100_read_direct(priv, addr + 1);
	return data;
}

static void w5100_write16_direct(struct w5100_priv *priv, u16 addr, u16 data)
{
	w5100_write_direct(priv, addr, data >> 8);
	w5100_write_direct(priv, addr + 1, data);
}

static void w5100_readbuf_direct(struct w5100_priv *priv,
				 u16 offset, u8 *buf, int len)
{
	u16 addr = W5100_RX_MEM_START + (offset & W5100_RX_MEM_MASK);
	int i;

	for (i = 0; i < len; i++, addr++) {
		if (unlikely(addr > W5100_RX_MEM_END))
			addr = W5100_RX_MEM_START;
		*buf++ = w5100_read_direct(priv, addr);
	}
}

static void w5100_writebuf_direct(struct w5100_priv *priv,
				  u16 offset, u8 *buf, int len)
{
	u16 addr = W5100_TX_MEM_START + (offset & W5100_TX_MEM_MASK);
	int i;

	for (i = 0; i < len; i++, addr++) {
		if (unlikely(addr > W5100_TX_MEM_END))
			addr = W5100_TX_MEM_START;
		w5100_write_direct(priv, addr, *buf++);
	}
}

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

static u8 w5100_read_indirect(struct w5100_priv *priv, u16 addr)
{
	unsigned long flags;
	u8 data;

	spin_lock_irqsave(&priv->reg_lock, flags);
	w5100_write16_direct(priv, W5100_IDM_AR, addr);
	mmiowb();
	data = w5100_read_direct(priv, W5100_IDM_DR);
	spin_unlock_irqrestore(&priv->reg_lock, flags);

	return data;
}

static void w5100_write_indirect(struct w5100_priv *priv, u16 addr, u8 data)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->reg_lock, flags);
	w5100_write16_direct(priv, W5100_IDM_AR, addr);
	mmiowb();
	w5100_write_direct(priv, W5100_IDM_DR, data);
	mmiowb();
	spin_unlock_irqrestore(&priv->reg_lock, flags);
}

static u16 w5100_read16_indirect(struct w5100_priv *priv, u16 addr)
{
	unsigned long flags;
	u16 data;

	spin_lock_irqsave(&priv->reg_lock, flags);
	w5100_write16_direct(priv, W5100_IDM_AR, addr);
	mmiowb();
	data  = w5100_read_direct(priv, W5100_IDM_DR) << 8;
	data |= w5100_read_direct(priv, W5100_IDM_DR);
	spin_unlock_irqrestore(&priv->reg_lock, flags);

	return data;
}

static void w5100_write16_indirect(struct w5100_priv *priv, u16 addr, u16 data)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->reg_lock, flags);
	w5100_write16_direct(priv, W5100_IDM_AR, addr);
	mmiowb();
	w5100_write_direct(priv, W5100_IDM_DR, data >> 8);
	w5100_write_direct(priv, W5100_IDM_DR, data);
	mmiowb();
	spin_unlock_irqrestore(&priv->reg_lock, flags);
}

static void w5100_readbuf_indirect(struct w5100_priv *priv,
				   u16 offset, u8 *buf, int len)
{
	u16 addr = W5100_RX_MEM_START + (offset & W5100_RX_MEM_MASK);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&priv->reg_lock, flags);
	w5100_write16_direct(priv, W5100_IDM_AR, addr);
	mmiowb();

	for (i = 0; i < len; i++, addr++) {
		if (unlikely(addr > W5100_RX_MEM_END)) {
			addr = W5100_RX_MEM_START;
			w5100_write16_direct(priv, W5100_IDM_AR, addr);
			mmiowb();
		}
		*buf++ = w5100_read_direct(priv, W5100_IDM_DR);
	}
	mmiowb();
	spin_unlock_irqrestore(&priv->reg_lock, flags);
}

static void w5100_writebuf_indirect(struct w5100_priv *priv,
				    u16 offset, u8 *buf, int len)
{
	u16 addr = W5100_TX_MEM_START + (offset & W5100_TX_MEM_MASK);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&priv->reg_lock, flags);
	w5100_write16_direct(priv, W5100_IDM_AR, addr);
	mmiowb();

	for (i = 0; i < len; i++, addr++) {
		if (unlikely(addr > W5100_TX_MEM_END)) {
			addr = W5100_TX_MEM_START;
			w5100_write16_direct(priv, W5100_IDM_AR, addr);
			mmiowb();
		}
		w5100_write_direct(priv, W5100_IDM_DR, *buf++);
	}
	mmiowb();
	spin_unlock_irqrestore(&priv->reg_lock, flags);
}

#if defined(CONFIG_WIZNET_BUS_DIRECT)
#define w5100_read	w5100_read_direct
#define w5100_write	w5100_write_direct
#define w5100_read16	w5100_read16_direct
#define w5100_write16	w5100_write16_direct
#define w5100_readbuf	w5100_readbuf_direct
#define w5100_writebuf	w5100_writebuf_direct

#elif defined(CONFIG_WIZNET_BUS_INDIRECT)
#define w5100_read	w5100_read_indirect
#define w5100_write	w5100_write_indirect
#define w5100_read16	w5100_read16_indirect
#define w5100_write16	w5100_write16_indirect
#define w5100_readbuf	w5100_readbuf_indirect
#define w5100_writebuf	w5100_writebuf_indirect

#else /* CONFIG_WIZNET_BUS_ANY */
#define w5100_read	priv->read
#define w5100_write	priv->write
#define w5100_read16	priv->read16
#define w5100_write16	priv->write16
#define w5100_readbuf	priv->readbuf
#define w5100_writebuf	priv->writebuf
#endif

static int w5100_command(struct w5100_priv *priv, u16 cmd)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(100);

	w5100_write(priv, W5100_S0_CR, cmd);
	mmiowb();

	while (w5100_read(priv, W5100_S0_CR) != 0) {
		if (time_after(jiffies, timeout))
			return -EIO;
		cpu_relax();
	}

	return 0;
}

static void w5100_write_macaddr(struct w5100_priv *priv)
{
	struct net_device *ndev = priv->ndev;
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		w5100_write(priv, W5100_SHAR + i, ndev->dev_addr[i]);
	mmiowb();
}

static void w5100_hw_reset(struct w5100_priv *priv)
{
	w5100_write_direct(priv, W5100_MR, MR_RST);
	mmiowb();
	mdelay(5);
	w5100_write_direct(priv, W5100_MR, priv->indirect ?
				  MR_PB | MR_AI | MR_IND :
				  MR_PB);
	mmiowb();
	w5100_write(priv, W5100_IMR, 0);
	w5100_write_macaddr(priv);

	/* Configure 16K of internal memory
	 * as 8K RX buffer and 8K TX buffer
	 */
	w5100_write(priv, W5100_RMSR, 0x03);
	w5100_write(priv, W5100_TMSR, 0x03);
	mmiowb();
}

static void w5100_hw_start(struct w5100_priv *priv)
{
	w5100_write(priv, W5100_S0_MR, priv->promisc ?
			  S0_MR_MACRAW : S0_MR_MACRAW_MF);
	mmiowb();
	w5100_command(priv, S0_CR_OPEN);
	w5100_write(priv, W5100_IMR, IR_S0);
	mmiowb();
}

static void w5100_hw_close(struct w5100_priv *priv)
{
	w5100_write(priv, W5100_IMR, 0);
	mmiowb();
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
			   struct ethtool_regs *regs, void *_buf)
{
	struct w5100_priv *priv = netdev_priv(ndev);
	u8 *buf = _buf;
	u16 i;

	regs->version = 1;
	for (i = 0; i < W5100_COMMON_REGS_LEN; i++)
		*buf++ = w5100_read(priv, W5100_COMMON_REGS + i);
	for (i = 0; i < W5100_S0_REGS_LEN; i++)
		*buf++ = w5100_read(priv, W5100_S0_REGS + i);
}

static void w5100_tx_timeout(struct net_device *ndev)
{
	struct w5100_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);
	w5100_hw_reset(priv);
	w5100_hw_start(priv);
	ndev->stats.tx_errors++;
	ndev->trans_start = jiffies;
	netif_wake_queue(ndev);
}

static int w5100_start_tx(struct sk_buff *skb, struct net_device *ndev)
{
	struct w5100_priv *priv = netdev_priv(ndev);
	u16 offset;

	netif_stop_queue(ndev);

	offset = w5100_read16(priv, W5100_S0_TX_WR);
	w5100_writebuf(priv, offset, skb->data, skb->len);
	w5100_write16(priv, W5100_S0_TX_WR, offset + skb->len);
	mmiowb();
	ndev->stats.tx_bytes += skb->len;
	ndev->stats.tx_packets++;
	dev_kfree_skb(skb);

	w5100_command(priv, S0_CR_SEND);

	return NETDEV_TX_OK;
}

static int w5100_napi_poll(struct napi_struct *napi, int budget)
{
	struct w5100_priv *priv = container_of(napi, struct w5100_priv, napi);
	struct net_device *ndev = priv->ndev;
	struct sk_buff *skb;
	int rx_count;
	u16 rx_len;
	u16 offset;
	u8 header[2];

	for (rx_count = 0; rx_count < budget; rx_count++) {
		u16 rx_buf_len = w5100_read16(priv, W5100_S0_RX_RSR);
		if (rx_buf_len == 0)
			break;

		offset = w5100_read16(priv, W5100_S0_RX_RD);
		w5100_readbuf(priv, offset, header, 2);
		rx_len = get_unaligned_be16(header) - 2;

		skb = netdev_alloc_skb_ip_align(ndev, rx_len);
		if (unlikely(!skb)) {
			w5100_write16(priv, W5100_S0_RX_RD,
					    offset + rx_buf_len);
			w5100_command(priv, S0_CR_RECV);
			ndev->stats.rx_dropped++;
			return -ENOMEM;
		}

		skb_put(skb, rx_len);
		w5100_readbuf(priv, offset + 2, skb->data, rx_len);
		w5100_write16(priv, W5100_S0_RX_RD, offset + 2 + rx_len);
		mmiowb();
		w5100_command(priv, S0_CR_RECV);
		skb->protocol = eth_type_trans(skb, ndev);

		netif_receive_skb(skb);
		ndev->stats.rx_packets++;
		ndev->stats.rx_bytes += rx_len;
	}

	if (rx_count < budget) {
		w5100_write(priv, W5100_IMR, IR_S0);
		mmiowb();
		napi_complete(napi);
	}

	return rx_count;
}

static irqreturn_t w5100_interrupt(int irq, void *ndev_instance)
{
	struct net_device *ndev = ndev_instance;
	struct w5100_priv *priv = netdev_priv(ndev);

	int ir = w5100_read(priv, W5100_S0_IR);
	if (!ir)
		return IRQ_NONE;
	w5100_write(priv, W5100_S0_IR, ir);
	mmiowb();

	if (ir & S0_IR_SENDOK) {
		netif_dbg(priv, tx_done, ndev, "tx done\n");
		netif_wake_queue(ndev);
	}

	if (ir & S0_IR_RECV) {
		if (napi_schedule_prep(&priv->napi)) {
			w5100_write(priv, W5100_IMR, 0);
			mmiowb();
			__napi_schedule(&priv->napi);
		}
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

static void w5100_set_rx_mode(struct net_device *ndev)
{
	struct w5100_priv *priv = netdev_priv(ndev);
	bool set_promisc = (ndev->flags & IFF_PROMISC) != 0;

	if (priv->promisc != set_promisc) {
		priv->promisc = set_promisc;
		w5100_hw_start(priv);
	}
}

static int w5100_set_macaddr(struct net_device *ndev, void *addr)
{
	struct w5100_priv *priv = netdev_priv(ndev);
	struct sockaddr *sock_addr = addr;

	if (!is_valid_ether_addr(sock_addr->sa_data))
		return -EADDRNOTAVAIL;
	memcpy(ndev->dev_addr, sock_addr->sa_data, ETH_ALEN);
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
	.ndo_change_mtu		= eth_change_mtu,
};

static int w5100_hw_probe(struct platform_device *pdev)
{
	struct wiznet_platform_data *data = dev_get_platdata(&pdev->dev);
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct w5100_priv *priv = netdev_priv(ndev);
	const char *name = netdev_name(ndev);
	struct resource *mem;
	int mem_size;
	int irq;
	int ret;

	if (data && is_valid_ether_addr(data->mac_addr)) {
		memcpy(ndev->dev_addr, data->mac_addr, ETH_ALEN);
	} else {
		eth_hw_addr_random(ndev);
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	mem_size = resource_size(mem);

	spin_lock_init(&priv->reg_lock);
	priv->indirect = mem_size < W5100_BUS_DIRECT_SIZE;
	if (priv->indirect) {
		priv->read     = w5100_read_indirect;
		priv->write    = w5100_write_indirect;
		priv->read16   = w5100_read16_indirect;
		priv->write16  = w5100_write16_indirect;
		priv->readbuf  = w5100_readbuf_indirect;
		priv->writebuf = w5100_writebuf_indirect;
	} else {
		priv->read     = w5100_read_direct;
		priv->write    = w5100_write_direct;
		priv->read16   = w5100_read16_direct;
		priv->write16  = w5100_write16_direct;
		priv->readbuf  = w5100_readbuf_direct;
		priv->writebuf = w5100_writebuf_direct;
	}

	w5100_hw_reset(priv);
	if (w5100_read16(priv, W5100_RTR) != RTR_DEFAULT)
		return -ENODEV;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	ret = request_irq(irq, w5100_interrupt,
			  IRQ_TYPE_LEVEL_LOW, name, ndev);
	if (ret < 0)
		return ret;
	priv->irq = irq;

	priv->link_gpio = data ? data->link_gpio : -EINVAL;
	if (gpio_is_valid(priv->link_gpio)) {
		char *link_name = devm_kzalloc(&pdev->dev, 16, GFP_KERNEL);
		if (!link_name)
			return -ENOMEM;
		snprintf(link_name, 16, "%s-link", name);
		priv->link_irq = gpio_to_irq(priv->link_gpio);
		if (request_any_context_irq(priv->link_irq, w5100_detect_link,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				link_name, priv->ndev) < 0)
			priv->link_gpio = -EINVAL;
	}

	netdev_info(ndev, "at 0x%llx irq %d\n", (u64)mem->start, irq);
	return 0;
}

static int w5100_probe(struct platform_device *pdev)
{
	struct w5100_priv *priv;
	struct net_device *ndev;
	int err;

	ndev = alloc_etherdev(sizeof(*priv));
	if (!ndev)
		return -ENOMEM;
	SET_NETDEV_DEV(ndev, &pdev->dev);
	platform_set_drvdata(pdev, ndev);
	priv = netdev_priv(ndev);
	priv->ndev = ndev;

	ndev->netdev_ops = &w5100_netdev_ops;
	ndev->ethtool_ops = &w5100_ethtool_ops;
	ndev->watchdog_timeo = HZ;
	netif_napi_add(ndev, &priv->napi, w5100_napi_poll, 16);

	/* This chip doesn't support VLAN packets with normal MTU,
	 * so disable VLAN for this device.
	 */
	ndev->features |= NETIF_F_VLAN_CHALLENGED;

	err = register_netdev(ndev);
	if (err < 0)
		goto err_register;

	err = w5100_hw_probe(pdev);
	if (err < 0)
		goto err_hw_probe;

	return 0;

err_hw_probe:
	unregister_netdev(ndev);
err_register:
	free_netdev(ndev);
	return err;
}

static int w5100_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct w5100_priv *priv = netdev_priv(ndev);

	w5100_hw_reset(priv);
	free_irq(priv->irq, ndev);
	if (gpio_is_valid(priv->link_gpio))
		free_irq(priv->link_irq, ndev);

	unregister_netdev(ndev);
	free_netdev(ndev);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int w5100_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct net_device *ndev = platform_get_drvdata(pdev);
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
	struct platform_device *pdev = to_platform_device(dev);
	struct net_device *ndev = platform_get_drvdata(pdev);
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

static SIMPLE_DEV_PM_OPS(w5100_pm_ops, w5100_suspend, w5100_resume);

static struct platform_driver w5100_driver = {
	.driver		= {
		.name	= DRV_NAME,
		.pm	= &w5100_pm_ops,
	},
	.probe		= w5100_probe,
	.remove		= w5100_remove,
};

module_platform_driver(w5100_driver);
