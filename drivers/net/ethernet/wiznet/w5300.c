/*
 * Ethernet driver for the WIZnet W5300 chip.
 *
 * Copyright (C) 2008-2009 WIZnet Co.,Ltd.
 * Copyright (C) 2011 Taehun Kim <kth3321 <at> gmail.com>
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

#define DRV_NAME	"w5300"
#define DRV_VERSION	"2012-04-04"

MODULE_DESCRIPTION("WIZnet W5300 Ethernet driver v"DRV_VERSION);
MODULE_AUTHOR("Mike Sinkovsky <msink@permonline.ru>");
MODULE_ALIAS("platform:"DRV_NAME);
MODULE_LICENSE("GPL");

/*
 * Registers
 */
#define W5300_MR		0x0000	/* Mode Register */
#define   MR_DBW		  (1 << 15) /* Data bus width */
#define   MR_MPF		  (1 << 14) /* Mac layer pause frame */
#define   MR_WDF(n)		  (((n)&7)<<11) /* Write data fetch time */
#define   MR_RDH		  (1 << 10) /* Read data hold time */
#define   MR_FS			  (1 << 8)  /* FIFO swap */
#define   MR_RST		  (1 << 7)  /* S/W reset */
#define   MR_PB			  (1 << 4)  /* Ping block */
#define   MR_DBS		  (1 << 2)  /* Data bus swap */
#define   MR_IND		  (1 << 0)  /* Indirect mode */
#define W5300_IR		0x0002	/* Interrupt Register */
#define W5300_IMR		0x0004	/* Interrupt Mask Register */
#define   IR_S0			  0x0001  /* S0 interrupt */
#define W5300_SHARL		0x0008	/* Source MAC address (0123) */
#define W5300_SHARH		0x000c	/* Source MAC address (45) */
#define W5300_TMSRL		0x0020	/* Transmit Memory Size (0123) */
#define W5300_TMSRH		0x0024	/* Transmit Memory Size (4567) */
#define W5300_RMSRL		0x0028	/* Receive Memory Size (0123) */
#define W5300_RMSRH		0x002c	/* Receive Memory Size (4567) */
#define W5300_MTYPE		0x0030	/* Memory Type */
#define W5300_IDR		0x00fe	/* Chip ID register */
#define   IDR_W5300		  0x5300  /* =0x5300 for WIZnet W5300 */
#define W5300_S0_MR		0x0200	/* S0 Mode Register */
#define   S0_MR_CLOSED		  0x0000  /* Close mode */
#define   S0_MR_MACRAW		  0x0004  /* MAC RAW mode (promiscous) */
#define   S0_MR_MACRAW_MF	  0x0044  /* MAC RAW mode (filtered) */
#define W5300_S0_CR		0x0202	/* S0 Command Register */
#define   S0_CR_OPEN		  0x0001  /* OPEN command */
#define   S0_CR_CLOSE		  0x0010  /* CLOSE command */
#define   S0_CR_SEND		  0x0020  /* SEND command */
#define   S0_CR_RECV		  0x0040  /* RECV command */
#define W5300_S0_IMR		0x0204	/* S0 Interrupt Mask Register */
#define W5300_S0_IR		0x0206	/* S0 Interrupt Register */
#define   S0_IR_RECV		  0x0004  /* Receive interrupt */
#define   S0_IR_SENDOK		  0x0010  /* Send OK interrupt */
#define W5300_S0_SSR		0x0208	/* S0 Socket Status Register */
#define W5300_S0_TX_WRSR	0x0220	/* S0 TX Write Size Register */
#define W5300_S0_TX_FSR		0x0224	/* S0 TX Free Size Register */
#define W5300_S0_RX_RSR		0x0228	/* S0 Received data Size */
#define W5300_S0_TX_FIFO	0x022e	/* S0 Transmit FIFO */
#define W5300_S0_RX_FIFO	0x0230	/* S0 Receive FIFO */
#define W5300_REGS_LEN		0x0400

/*
 * Device driver private data structure
 */
struct w5300_priv {
	void __iomem *base;
	spinlock_t reg_lock;
	bool indirect;
	u16  (*read) (struct w5300_priv *priv, u16 addr);
	void (*write)(struct w5300_priv *priv, u16 addr, u16 data);
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
 * In direct address mode host system can directly access W5300 registers
 * after mapping to Memory-Mapped I/O space.
 *
 * 0x400 bytes are required for memory space.
 */
static inline u16 w5300_read_direct(struct w5300_priv *priv, u16 addr)
{
	return ioread16(priv->base + (addr << CONFIG_WIZNET_BUS_SHIFT));
}

static inline void w5300_write_direct(struct w5300_priv *priv,
				      u16 addr, u16 data)
{
	iowrite16(data, priv->base + (addr << CONFIG_WIZNET_BUS_SHIFT));
}

/*
 * In indirect address mode host system indirectly accesses registers by
 * using Indirect Mode Address Register (IDM_AR) and Indirect Mode Data
 * Register (IDM_DR), which are directly mapped to Memory-Mapped I/O space.
 * Mode Register (MR) is directly accessible.
 *
 * Only 0x06 bytes are required for memory space.
 */
#define W5300_IDM_AR		0x0002	 /* Indirect Mode Address */
#define W5300_IDM_DR		0x0004	 /* Indirect Mode Data */

static u16 w5300_read_indirect(struct w5300_priv *priv, u16 addr)
{
	unsigned long flags;
	u16 data;

	spin_lock_irqsave(&priv->reg_lock, flags);
	w5300_write_direct(priv, W5300_IDM_AR, addr);
	mmiowb();
	data = w5300_read_direct(priv, W5300_IDM_DR);
	spin_unlock_irqrestore(&priv->reg_lock, flags);

	return data;
}

static void w5300_write_indirect(struct w5300_priv *priv, u16 addr, u16 data)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->reg_lock, flags);
	w5300_write_direct(priv, W5300_IDM_AR, addr);
	mmiowb();
	w5300_write_direct(priv, W5300_IDM_DR, data);
	mmiowb();
	spin_unlock_irqrestore(&priv->reg_lock, flags);
}

#if defined(CONFIG_WIZNET_BUS_DIRECT)
#define w5300_read	w5300_read_direct
#define w5300_write	w5300_write_direct

#elif defined(CONFIG_WIZNET_BUS_INDIRECT)
#define w5300_read	w5300_read_indirect
#define w5300_write	w5300_write_indirect

#else /* CONFIG_WIZNET_BUS_ANY */
#define w5300_read	priv->read
#define w5300_write	priv->write
#endif

static u32 w5300_read32(struct w5300_priv *priv, u16 addr)
{
	u32 data;
	data  = w5300_read(priv, addr) << 16;
	data |= w5300_read(priv, addr + 2);
	return data;
}

static void w5300_write32(struct w5300_priv *priv, u16 addr, u32 data)
{
	w5300_write(priv, addr, data >> 16);
	w5300_write(priv, addr + 2, data);
}

static int w5300_command(struct w5300_priv *priv, u16 cmd)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(100);

	w5300_write(priv, W5300_S0_CR, cmd);
	mmiowb();

	while (w5300_read(priv, W5300_S0_CR) != 0) {
		if (time_after(jiffies, timeout))
			return -EIO;
		cpu_relax();
	}

	return 0;
}

static void w5300_read_frame(struct w5300_priv *priv, u8 *buf, int len)
{
	u16 fifo;
	int i;

	for (i = 0; i < len; i += 2) {
		fifo = w5300_read(priv, W5300_S0_RX_FIFO);
		*buf++ = fifo >> 8;
		*buf++ = fifo;
	}
	fifo = w5300_read(priv, W5300_S0_RX_FIFO);
	fifo = w5300_read(priv, W5300_S0_RX_FIFO);
}

static void w5300_write_frame(struct w5300_priv *priv, u8 *buf, int len)
{
	u16 fifo;
	int i;

	for (i = 0; i < len; i += 2) {
		fifo  = *buf++ << 8;
		fifo |= *buf++;
		w5300_write(priv, W5300_S0_TX_FIFO, fifo);
	}
	w5300_write32(priv, W5300_S0_TX_WRSR, len);
}

static void w5300_write_macaddr(struct w5300_priv *priv)
{
	struct net_device *ndev = priv->ndev;
	w5300_write32(priv, W5300_SHARL,
		      ndev->dev_addr[0] << 24 |
		      ndev->dev_addr[1] << 16 |
		      ndev->dev_addr[2] << 8 |
		      ndev->dev_addr[3]);
	w5300_write(priv, W5300_SHARH,
		      ndev->dev_addr[4] << 8 |
		      ndev->dev_addr[5]);
	mmiowb();
}

static void w5300_hw_reset(struct w5300_priv *priv)
{
	w5300_write_direct(priv, W5300_MR, MR_RST);
	mmiowb();
	mdelay(5);
	w5300_write_direct(priv, W5300_MR, priv->indirect ?
				 MR_WDF(7) | MR_PB | MR_IND :
				 MR_WDF(7) | MR_PB);
	mmiowb();
	w5300_write(priv, W5300_IMR, 0);
	w5300_write_macaddr(priv);

	/* Configure 128K of internal memory
	 * as 64K RX fifo and 64K TX fifo
	 */
	w5300_write32(priv, W5300_RMSRL, 64 << 24);
	w5300_write32(priv, W5300_RMSRH, 0);
	w5300_write32(priv, W5300_TMSRL, 64 << 24);
	w5300_write32(priv, W5300_TMSRH, 0);
	w5300_write(priv, W5300_MTYPE, 0x00ff);
	mmiowb();
}

static void w5300_hw_start(struct w5300_priv *priv)
{
	w5300_write(priv, W5300_S0_MR, priv->promisc ?
			  S0_MR_MACRAW : S0_MR_MACRAW_MF);
	mmiowb();
	w5300_command(priv, S0_CR_OPEN);
	w5300_write(priv, W5300_S0_IMR, S0_IR_RECV | S0_IR_SENDOK);
	w5300_write(priv, W5300_IMR, IR_S0);
	mmiowb();
}

static void w5300_hw_close(struct w5300_priv *priv)
{
	w5300_write(priv, W5300_IMR, 0);
	mmiowb();
	w5300_command(priv, S0_CR_CLOSE);
}

/***********************************************************************
 *
 *   Device driver functions / callbacks
 *
 ***********************************************************************/

static void w5300_get_drvinfo(struct net_device *ndev,
			      struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, dev_name(ndev->dev.parent),
		sizeof(info->bus_info));
}

static u32 w5300_get_link(struct net_device *ndev)
{
	struct w5300_priv *priv = netdev_priv(ndev);

	if (gpio_is_valid(priv->link_gpio))
		return !!gpio_get_value(priv->link_gpio);

	return 1;
}

static u32 w5300_get_msglevel(struct net_device *ndev)
{
	struct w5300_priv *priv = netdev_priv(ndev);

	return priv->msg_enable;
}

static void w5300_set_msglevel(struct net_device *ndev, u32 value)
{
	struct w5300_priv *priv = netdev_priv(ndev);

	priv->msg_enable = value;
}

static int w5300_get_regs_len(struct net_device *ndev)
{
	return W5300_REGS_LEN;
}

static void w5300_get_regs(struct net_device *ndev,
			   struct ethtool_regs *regs, void *_buf)
{
	struct w5300_priv *priv = netdev_priv(ndev);
	u8 *buf = _buf;
	u16 addr;
	u16 data;

	regs->version = 1;
	for (addr = 0; addr < W5300_REGS_LEN; addr += 2) {
		switch (addr & 0x23f) {
		case W5300_S0_TX_FIFO: /* cannot read TX_FIFO */
		case W5300_S0_RX_FIFO: /* cannot read RX_FIFO */
			data = 0xffff;
			break;
		default:
			data = w5300_read(priv, addr);
			break;
		}
		*buf++ = data >> 8;
		*buf++ = data;
	}
}

static void w5300_tx_timeout(struct net_device *ndev)
{
	struct w5300_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);
	w5300_hw_reset(priv);
	w5300_hw_start(priv);
	ndev->stats.tx_errors++;
	ndev->trans_start = jiffies;
	netif_wake_queue(ndev);
}

static int w5300_start_tx(struct sk_buff *skb, struct net_device *ndev)
{
	struct w5300_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);

	w5300_write_frame(priv, skb->data, skb->len);
	mmiowb();
	ndev->stats.tx_packets++;
	ndev->stats.tx_bytes += skb->len;
	dev_kfree_skb(skb);
	netif_dbg(priv, tx_queued, ndev, "tx queued\n");

	w5300_command(priv, S0_CR_SEND);

	return NETDEV_TX_OK;
}

static int w5300_napi_poll(struct napi_struct *napi, int budget)
{
	struct w5300_priv *priv = container_of(napi, struct w5300_priv, napi);
	struct net_device *ndev = priv->ndev;
	struct sk_buff *skb;
	int rx_count;
	u16 rx_len;

	for (rx_count = 0; rx_count < budget; rx_count++) {
		u32 rx_fifo_len = w5300_read32(priv, W5300_S0_RX_RSR);
		if (rx_fifo_len == 0)
			break;

		rx_len = w5300_read(priv, W5300_S0_RX_FIFO);

		skb = netdev_alloc_skb_ip_align(ndev, roundup(rx_len, 2));
		if (unlikely(!skb)) {
			u32 i;
			for (i = 0; i < rx_fifo_len; i += 2)
				w5300_read(priv, W5300_S0_RX_FIFO);
			ndev->stats.rx_dropped++;
			return -ENOMEM;
		}

		skb_put(skb, rx_len);
		w5300_read_frame(priv, skb->data, rx_len);
		skb->protocol = eth_type_trans(skb, ndev);

		netif_receive_skb(skb);
		ndev->stats.rx_packets++;
		ndev->stats.rx_bytes += rx_len;
	}

	if (rx_count < budget) {
		w5300_write(priv, W5300_IMR, IR_S0);
		mmiowb();
		napi_complete(napi);
	}

	return rx_count;
}

static irqreturn_t w5300_interrupt(int irq, void *ndev_instance)
{
	struct net_device *ndev = ndev_instance;
	struct w5300_priv *priv = netdev_priv(ndev);

	int ir = w5300_read(priv, W5300_S0_IR);
	if (!ir)
		return IRQ_NONE;
	w5300_write(priv, W5300_S0_IR, ir);
	mmiowb();

	if (ir & S0_IR_SENDOK) {
		netif_dbg(priv, tx_done, ndev, "tx done\n");
		netif_wake_queue(ndev);
	}

	if (ir & S0_IR_RECV) {
		if (napi_schedule_prep(&priv->napi)) {
			w5300_write(priv, W5300_IMR, 0);
			mmiowb();
			__napi_schedule(&priv->napi);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t w5300_detect_link(int irq, void *ndev_instance)
{
	struct net_device *ndev = ndev_instance;
	struct w5300_priv *priv = netdev_priv(ndev);

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

static void w5300_set_rx_mode(struct net_device *ndev)
{
	struct w5300_priv *priv = netdev_priv(ndev);
	bool set_promisc = (ndev->flags & IFF_PROMISC) != 0;

	if (priv->promisc != set_promisc) {
		priv->promisc = set_promisc;
		w5300_hw_start(priv);
	}
}

static int w5300_set_macaddr(struct net_device *ndev, void *addr)
{
	struct w5300_priv *priv = netdev_priv(ndev);
	struct sockaddr *sock_addr = addr;

	if (!is_valid_ether_addr(sock_addr->sa_data))
		return -EADDRNOTAVAIL;
	memcpy(ndev->dev_addr, sock_addr->sa_data, ETH_ALEN);
	w5300_write_macaddr(priv);
	return 0;
}

static int w5300_open(struct net_device *ndev)
{
	struct w5300_priv *priv = netdev_priv(ndev);

	netif_info(priv, ifup, ndev, "enabling\n");
	w5300_hw_start(priv);
	napi_enable(&priv->napi);
	netif_start_queue(ndev);
	if (!gpio_is_valid(priv->link_gpio) ||
	    gpio_get_value(priv->link_gpio) != 0)
		netif_carrier_on(ndev);
	return 0;
}

static int w5300_stop(struct net_device *ndev)
{
	struct w5300_priv *priv = netdev_priv(ndev);

	netif_info(priv, ifdown, ndev, "shutting down\n");
	w5300_hw_close(priv);
	netif_carrier_off(ndev);
	netif_stop_queue(ndev);
	napi_disable(&priv->napi);
	return 0;
}

static const struct ethtool_ops w5300_ethtool_ops = {
	.get_drvinfo		= w5300_get_drvinfo,
	.get_msglevel		= w5300_get_msglevel,
	.set_msglevel		= w5300_set_msglevel,
	.get_link		= w5300_get_link,
	.get_regs_len		= w5300_get_regs_len,
	.get_regs		= w5300_get_regs,
};

static const struct net_device_ops w5300_netdev_ops = {
	.ndo_open		= w5300_open,
	.ndo_stop		= w5300_stop,
	.ndo_start_xmit		= w5300_start_tx,
	.ndo_tx_timeout		= w5300_tx_timeout,
	.ndo_set_rx_mode	= w5300_set_rx_mode,
	.ndo_set_mac_address	= w5300_set_macaddr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= eth_change_mtu,
};

static int w5300_hw_probe(struct platform_device *pdev)
{
	struct wiznet_platform_data *data = pdev->dev.platform_data;
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct w5300_priv *priv = netdev_priv(ndev);
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
	if (!mem)
		return -ENXIO;
	mem_size = resource_size(mem);
	if (!devm_request_mem_region(&pdev->dev, mem->start, mem_size, name))
		return -EBUSY;
	priv->base = devm_ioremap(&pdev->dev, mem->start, mem_size);
	if (!priv->base)
		return -EBUSY;

	spin_lock_init(&priv->reg_lock);
	priv->indirect = mem_size < W5300_BUS_DIRECT_SIZE;
	if (priv->indirect) {
		priv->read  = w5300_read_indirect;
		priv->write = w5300_write_indirect;
	} else {
		priv->read  = w5300_read_direct;
		priv->write = w5300_write_direct;
	}

	w5300_hw_reset(priv);
	if (w5300_read(priv, W5300_IDR) != IDR_W5300)
		return -ENODEV;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	ret = request_irq(irq, w5300_interrupt,
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
		if (request_any_context_irq(priv->link_irq, w5300_detect_link,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				link_name, priv->ndev) < 0)
			priv->link_gpio = -EINVAL;
	}

	netdev_info(ndev, "at 0x%llx irq %d\n", (u64)mem->start, irq);
	return 0;
}

static int w5300_probe(struct platform_device *pdev)
{
	struct w5300_priv *priv;
	struct net_device *ndev;
	int err;

	ndev = alloc_etherdev(sizeof(*priv));
	if (!ndev)
		return -ENOMEM;
	SET_NETDEV_DEV(ndev, &pdev->dev);
	platform_set_drvdata(pdev, ndev);
	priv = netdev_priv(ndev);
	priv->ndev = ndev;

	ether_setup(ndev);
	ndev->netdev_ops = &w5300_netdev_ops;
	ndev->ethtool_ops = &w5300_ethtool_ops;
	ndev->watchdog_timeo = HZ;
	netif_napi_add(ndev, &priv->napi, w5300_napi_poll, 16);

	/* This chip doesn't support VLAN packets with normal MTU,
	 * so disable VLAN for this device.
	 */
	ndev->features |= NETIF_F_VLAN_CHALLENGED;

	err = register_netdev(ndev);
	if (err < 0)
		goto err_register;

	err = w5300_hw_probe(pdev);
	if (err < 0)
		goto err_hw_probe;

	return 0;

err_hw_probe:
	unregister_netdev(ndev);
err_register:
	free_netdev(ndev);
	platform_set_drvdata(pdev, NULL);
	return err;
}

static int w5300_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct w5300_priv *priv = netdev_priv(ndev);

	w5300_hw_reset(priv);
	free_irq(priv->irq, ndev);
	if (gpio_is_valid(priv->link_gpio))
		free_irq(priv->link_irq, ndev);

	unregister_netdev(ndev);
	free_netdev(ndev);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int w5300_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct w5300_priv *priv = netdev_priv(ndev);

	if (netif_running(ndev)) {
		netif_carrier_off(ndev);
		netif_device_detach(ndev);

		w5300_hw_close(priv);
	}
	return 0;
}

static int w5300_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct w5300_priv *priv = netdev_priv(ndev);

	if (!netif_running(ndev)) {
		w5300_hw_reset(priv);
		w5300_hw_start(priv);

		netif_device_attach(ndev);
		if (!gpio_is_valid(priv->link_gpio) ||
		    gpio_get_value(priv->link_gpio) != 0)
			netif_carrier_on(ndev);
	}
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(w5300_pm_ops, w5300_suspend, w5300_resume);

static struct platform_driver w5300_driver = {
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
		.pm	= &w5300_pm_ops,
	},
	.probe		= w5300_probe,
	.remove		= w5300_remove,
};

module_platform_driver(w5300_driver);
