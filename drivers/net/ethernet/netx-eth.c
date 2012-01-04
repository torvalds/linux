/*
 * drivers/net/netx-eth.c
 *
 * Copyright (c) 2005 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>

#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/mii.h>

#include <asm/io.h>
#include <mach/hardware.h>
#include <mach/netx-regs.h>
#include <mach/pfifo.h>
#include <mach/xc.h>
#include <mach/eth.h>

/* XC Fifo Offsets */
#define EMPTY_PTR_FIFO(xcno)    (0 + ((xcno) << 3))	/* Index of the empty pointer FIFO */
#define IND_FIFO_PORT_HI(xcno)  (1 + ((xcno) << 3))	/* Index of the FIFO where received */
							/* Data packages are indicated by XC */
#define IND_FIFO_PORT_LO(xcno)  (2 + ((xcno) << 3))	/* Index of the FIFO where received */
							/* Data packages are indicated by XC */
#define REQ_FIFO_PORT_HI(xcno)  (3 + ((xcno) << 3))	/* Index of the FIFO where Data packages */
							/* have to be indicated by ARM which */
							/* shall be sent */
#define REQ_FIFO_PORT_LO(xcno)  (4 + ((xcno) << 3))	/* Index of the FIFO where Data packages */
							/* have to be indicated by ARM which shall */
							/* be sent */
#define CON_FIFO_PORT_HI(xcno)  (5 + ((xcno) << 3))	/* Index of the FIFO where sent Data packages */
							/* are confirmed */
#define CON_FIFO_PORT_LO(xcno)  (6 + ((xcno) << 3))	/* Index of the FIFO where sent Data */
							/* packages are confirmed */
#define PFIFO_MASK(xcno)        (0x7f << (xcno*8))

#define FIFO_PTR_FRAMELEN_SHIFT 0
#define FIFO_PTR_FRAMELEN_MASK  (0x7ff << 0)
#define FIFO_PTR_FRAMELEN(len)  (((len) << 0) & FIFO_PTR_FRAMELEN_MASK)
#define FIFO_PTR_TIMETRIG       (1<<11)
#define FIFO_PTR_MULTI_REQ
#define FIFO_PTR_ORIGIN         (1<<14)
#define FIFO_PTR_VLAN           (1<<15)
#define FIFO_PTR_FRAMENO_SHIFT  16
#define FIFO_PTR_FRAMENO_MASK   (0x3f << 16)
#define FIFO_PTR_FRAMENO(no)    (((no) << 16) & FIFO_PTR_FRAMENO_MASK)
#define FIFO_PTR_SEGMENT_SHIFT  22
#define FIFO_PTR_SEGMENT_MASK   (0xf << 22)
#define FIFO_PTR_SEGMENT(seg)   (((seg) & 0xf) << 22)
#define FIFO_PTR_ERROR_SHIFT    28
#define FIFO_PTR_ERROR_MASK     (0xf << 28)

#define ISR_LINK_STATUS_CHANGE (1<<4)
#define ISR_IND_LO             (1<<3)
#define ISR_CON_LO             (1<<2)
#define ISR_IND_HI             (1<<1)
#define ISR_CON_HI             (1<<0)

#define ETH_MAC_LOCAL_CONFIG 0x1560
#define ETH_MAC_4321         0x1564
#define ETH_MAC_65           0x1568

#define MAC_TRAFFIC_CLASS_ARRANGEMENT_SHIFT 16
#define MAC_TRAFFIC_CLASS_ARRANGEMENT_MASK (0xf<<MAC_TRAFFIC_CLASS_ARRANGEMENT_SHIFT)
#define MAC_TRAFFIC_CLASS_ARRANGEMENT(x) (((x)<<MAC_TRAFFIC_CLASS_ARRANGEMENT_SHIFT) & MAC_TRAFFIC_CLASS_ARRANGEMENT_MASK)
#define LOCAL_CONFIG_LINK_STATUS_IRQ_EN (1<<24)
#define LOCAL_CONFIG_CON_LO_IRQ_EN (1<<23)
#define LOCAL_CONFIG_CON_HI_IRQ_EN (1<<22)
#define LOCAL_CONFIG_IND_LO_IRQ_EN (1<<21)
#define LOCAL_CONFIG_IND_HI_IRQ_EN (1<<20)

#define CARDNAME "netx-eth"

/* LSB must be zero */
#define INTERNAL_PHY_ADR 0x1c

struct netx_eth_priv {
	void                    __iomem *sram_base, *xpec_base, *xmac_base;
	int                     id;
	struct mii_if_info      mii;
	u32                     msg_enable;
	struct xc               *xc;
	spinlock_t              lock;
};

static void netx_eth_set_multicast_list(struct net_device *ndev)
{
	/* implement me */
}

static int
netx_eth_hard_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct netx_eth_priv *priv = netdev_priv(ndev);
	unsigned char *buf = skb->data;
	unsigned int len = skb->len;

	spin_lock_irq(&priv->lock);
	memcpy_toio(priv->sram_base + 1560, (void *)buf, len);
	if (len < 60) {
		memset_io(priv->sram_base + 1560 + len, 0, 60 - len);
		len = 60;
	}

	pfifo_push(REQ_FIFO_PORT_LO(priv->id),
	           FIFO_PTR_SEGMENT(priv->id) |
	           FIFO_PTR_FRAMENO(1) |
	           FIFO_PTR_FRAMELEN(len));

	ndev->stats.tx_packets++;
	ndev->stats.tx_bytes += skb->len;

	netif_stop_queue(ndev);
	spin_unlock_irq(&priv->lock);
	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

static void netx_eth_receive(struct net_device *ndev)
{
	struct netx_eth_priv *priv = netdev_priv(ndev);
	unsigned int val, frameno, seg, len;
	unsigned char *data;
	struct sk_buff *skb;

	val = pfifo_pop(IND_FIFO_PORT_LO(priv->id));

	frameno = (val & FIFO_PTR_FRAMENO_MASK) >> FIFO_PTR_FRAMENO_SHIFT;
	seg = (val & FIFO_PTR_SEGMENT_MASK) >> FIFO_PTR_SEGMENT_SHIFT;
	len = (val & FIFO_PTR_FRAMELEN_MASK) >> FIFO_PTR_FRAMELEN_SHIFT;

	skb = dev_alloc_skb(len);
	if (unlikely(skb == NULL)) {
		printk(KERN_NOTICE "%s: Low memory, packet dropped.\n",
			ndev->name);
		ndev->stats.rx_dropped++;
		return;
	}

	data = skb_put(skb, len);

	memcpy_fromio(data, priv->sram_base + frameno * 1560, len);

	pfifo_push(EMPTY_PTR_FIFO(priv->id),
		FIFO_PTR_SEGMENT(seg) | FIFO_PTR_FRAMENO(frameno));

	skb->protocol = eth_type_trans(skb, ndev);
	netif_rx(skb);
	ndev->stats.rx_packets++;
	ndev->stats.rx_bytes += len;
}

static irqreturn_t
netx_eth_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct netx_eth_priv *priv = netdev_priv(ndev);
	int status;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	status = readl(NETX_PFIFO_XPEC_ISR(priv->id));
	while (status) {
		int fill_level;
		writel(status, NETX_PFIFO_XPEC_ISR(priv->id));

		if ((status & ISR_CON_HI) || (status & ISR_IND_HI))
			printk("%s: unexpected status: 0x%08x\n",
			    __func__, status);

		fill_level =
		    readl(NETX_PFIFO_FILL_LEVEL(IND_FIFO_PORT_LO(priv->id)));
		while (fill_level--)
			netx_eth_receive(ndev);

		if (status & ISR_CON_LO)
			netif_wake_queue(ndev);

		if (status & ISR_LINK_STATUS_CHANGE)
			mii_check_media(&priv->mii, netif_msg_link(priv), 1);

		status = readl(NETX_PFIFO_XPEC_ISR(priv->id));
	}
	spin_unlock_irqrestore(&priv->lock, flags);
	return IRQ_HANDLED;
}

static int netx_eth_open(struct net_device *ndev)
{
	struct netx_eth_priv *priv = netdev_priv(ndev);

	if (request_irq
	    (ndev->irq, netx_eth_interrupt, IRQF_SHARED, ndev->name, ndev))
		return -EAGAIN;

	writel(ndev->dev_addr[0] |
	       ndev->dev_addr[1]<<8 |
	       ndev->dev_addr[2]<<16 |
	       ndev->dev_addr[3]<<24,
	       priv->xpec_base + NETX_XPEC_RAM_START_OFS + ETH_MAC_4321);
	writel(ndev->dev_addr[4] |
	       ndev->dev_addr[5]<<8,
	       priv->xpec_base + NETX_XPEC_RAM_START_OFS + ETH_MAC_65);

	writel(LOCAL_CONFIG_LINK_STATUS_IRQ_EN |
		LOCAL_CONFIG_CON_LO_IRQ_EN |
		LOCAL_CONFIG_CON_HI_IRQ_EN |
		LOCAL_CONFIG_IND_LO_IRQ_EN |
		LOCAL_CONFIG_IND_HI_IRQ_EN,
		priv->xpec_base + NETX_XPEC_RAM_START_OFS +
		ETH_MAC_LOCAL_CONFIG);

	mii_check_media(&priv->mii, netif_msg_link(priv), 1);
	netif_start_queue(ndev);

	return 0;
}

static int netx_eth_close(struct net_device *ndev)
{
	struct netx_eth_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);

	writel(0,
	    priv->xpec_base + NETX_XPEC_RAM_START_OFS + ETH_MAC_LOCAL_CONFIG);

	free_irq(ndev->irq, ndev);

	return 0;
}

static void netx_eth_timeout(struct net_device *ndev)
{
	struct netx_eth_priv *priv = netdev_priv(ndev);
	int i;

	printk(KERN_ERR "%s: transmit timed out, resetting\n", ndev->name);

	spin_lock_irq(&priv->lock);

	xc_reset(priv->xc);
	xc_start(priv->xc);

	for (i=2; i<=18; i++)
		pfifo_push(EMPTY_PTR_FIFO(priv->id),
			FIFO_PTR_FRAMENO(i) | FIFO_PTR_SEGMENT(priv->id));

	spin_unlock_irq(&priv->lock);

	netif_wake_queue(ndev);
}

static int
netx_eth_phy_read(struct net_device *ndev, int phy_id, int reg)
{
	unsigned int val;

	val = MIIMU_SNRDY | MIIMU_PREAMBLE | MIIMU_PHYADDR(phy_id) |
	      MIIMU_REGADDR(reg) | MIIMU_PHY_NRES;

	writel(val, NETX_MIIMU);
	while (readl(NETX_MIIMU) & MIIMU_SNRDY);

	return readl(NETX_MIIMU) >> 16;

}

static void
netx_eth_phy_write(struct net_device *ndev, int phy_id, int reg, int value)
{
	unsigned int val;

	val = MIIMU_SNRDY | MIIMU_PREAMBLE | MIIMU_PHYADDR(phy_id) |
	      MIIMU_REGADDR(reg) | MIIMU_PHY_NRES | MIIMU_OPMODE_WRITE |
	      MIIMU_DATA(value);

	writel(val, NETX_MIIMU);
	while (readl(NETX_MIIMU) & MIIMU_SNRDY);
}

static const struct net_device_ops netx_eth_netdev_ops = {
	.ndo_open		= netx_eth_open,
	.ndo_stop		= netx_eth_close,
	.ndo_start_xmit		= netx_eth_hard_start_xmit,
	.ndo_tx_timeout		= netx_eth_timeout,
	.ndo_set_rx_mode	= netx_eth_set_multicast_list,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
};

static int netx_eth_enable(struct net_device *ndev)
{
	struct netx_eth_priv *priv = netdev_priv(ndev);
	unsigned int mac4321, mac65;
	int running, i;

	ether_setup(ndev);

	ndev->netdev_ops = &netx_eth_netdev_ops;
	ndev->watchdog_timeo = msecs_to_jiffies(5000);

	priv->msg_enable       = NETIF_MSG_LINK;
	priv->mii.phy_id_mask  = 0x1f;
	priv->mii.reg_num_mask = 0x1f;
	priv->mii.force_media  = 0;
	priv->mii.full_duplex  = 0;
	priv->mii.dev	     = ndev;
	priv->mii.mdio_read    = netx_eth_phy_read;
	priv->mii.mdio_write   = netx_eth_phy_write;
	priv->mii.phy_id = INTERNAL_PHY_ADR + priv->id;

	running = xc_running(priv->xc);
	xc_stop(priv->xc);

	/* if the xc engine is already running, assume the bootloader has
	 * loaded the firmware for us
	 */
	if (running) {
		/* get Node Address from hardware */
		mac4321 = readl(priv->xpec_base +
			NETX_XPEC_RAM_START_OFS + ETH_MAC_4321);
		mac65 = readl(priv->xpec_base +
			NETX_XPEC_RAM_START_OFS + ETH_MAC_65);

		ndev->dev_addr[0] = mac4321 & 0xff;
		ndev->dev_addr[1] = (mac4321 >> 8) & 0xff;
		ndev->dev_addr[2] = (mac4321 >> 16) & 0xff;
		ndev->dev_addr[3] = (mac4321 >> 24) & 0xff;
		ndev->dev_addr[4] = mac65 & 0xff;
		ndev->dev_addr[5] = (mac65 >> 8) & 0xff;
	} else {
		if (xc_request_firmware(priv->xc)) {
			printk(CARDNAME ": requesting firmware failed\n");
			return -ENODEV;
		}
	}

	xc_reset(priv->xc);
	xc_start(priv->xc);

	if (!is_valid_ether_addr(ndev->dev_addr))
		printk("%s: Invalid ethernet MAC address.  Please "
		       "set using ifconfig\n", ndev->name);

	for (i=2; i<=18; i++)
		pfifo_push(EMPTY_PTR_FIFO(priv->id),
			FIFO_PTR_FRAMENO(i) | FIFO_PTR_SEGMENT(priv->id));

	return register_netdev(ndev);

}

static int netx_eth_drv_probe(struct platform_device *pdev)
{
	struct netx_eth_priv *priv;
	struct net_device *ndev;
	struct netxeth_platform_data *pdata;
	int ret;

	ndev = alloc_etherdev(sizeof (struct netx_eth_priv));
	if (!ndev) {
		printk("%s: could not allocate device.\n", CARDNAME);
		ret = -ENOMEM;
		goto exit;
	}
	SET_NETDEV_DEV(ndev, &pdev->dev);

	platform_set_drvdata(pdev, ndev);

	priv = netdev_priv(ndev);

	pdata = (struct netxeth_platform_data *)pdev->dev.platform_data;
	priv->xc = request_xc(pdata->xcno, &pdev->dev);
	if (!priv->xc) {
		dev_err(&pdev->dev, "unable to request xc engine\n");
		ret = -ENODEV;
		goto exit_free_netdev;
	}

	ndev->irq = priv->xc->irq;
	priv->id = pdev->id;
	priv->xpec_base = priv->xc->xpec_base;
	priv->xmac_base = priv->xc->xmac_base;
	priv->sram_base = priv->xc->sram_base;

	spin_lock_init(&priv->lock);

	ret = pfifo_request(PFIFO_MASK(priv->id));
	if (ret) {
		printk("unable to request PFIFO\n");
		goto exit_free_xc;
	}

	ret = netx_eth_enable(ndev);
	if (ret)
		goto exit_free_pfifo;

	return 0;
exit_free_pfifo:
	pfifo_free(PFIFO_MASK(priv->id));
exit_free_xc:
	free_xc(priv->xc);
exit_free_netdev:
	platform_set_drvdata(pdev, NULL);
	free_netdev(ndev);
exit:
	return ret;
}

static int netx_eth_drv_remove(struct platform_device *pdev)
{
	struct net_device *ndev = dev_get_drvdata(&pdev->dev);
	struct netx_eth_priv *priv = netdev_priv(ndev);

	platform_set_drvdata(pdev, NULL);

	unregister_netdev(ndev);
	xc_stop(priv->xc);
	free_xc(priv->xc);
	free_netdev(ndev);
	pfifo_free(PFIFO_MASK(priv->id));

	return 0;
}

static int netx_eth_drv_suspend(struct platform_device *pdev, pm_message_t state)
{
	dev_err(&pdev->dev, "suspend not implemented\n");
	return 0;
}

static int netx_eth_drv_resume(struct platform_device *pdev)
{
	dev_err(&pdev->dev, "resume not implemented\n");
	return 0;
}

static struct platform_driver netx_eth_driver = {
	.probe		= netx_eth_drv_probe,
	.remove		= netx_eth_drv_remove,
	.suspend	= netx_eth_drv_suspend,
	.resume		= netx_eth_drv_resume,
	.driver		= {
		.name	= CARDNAME,
		.owner	= THIS_MODULE,
	},
};

static int __init netx_eth_init(void)
{
	unsigned int phy_control, val;

	printk("NetX Ethernet driver\n");

	phy_control = PHY_CONTROL_PHY_ADDRESS(INTERNAL_PHY_ADR>>1) |
		      PHY_CONTROL_PHY1_MODE(PHY_MODE_ALL) |
		      PHY_CONTROL_PHY1_AUTOMDIX |
		      PHY_CONTROL_PHY1_EN |
		      PHY_CONTROL_PHY0_MODE(PHY_MODE_ALL) |
		      PHY_CONTROL_PHY0_AUTOMDIX |
		      PHY_CONTROL_PHY0_EN |
		      PHY_CONTROL_CLK_XLATIN;

	val = readl(NETX_SYSTEM_IOC_ACCESS_KEY);
	writel(val, NETX_SYSTEM_IOC_ACCESS_KEY);

	writel(phy_control | PHY_CONTROL_RESET, NETX_SYSTEM_PHY_CONTROL);
	udelay(100);

	val = readl(NETX_SYSTEM_IOC_ACCESS_KEY);
	writel(val, NETX_SYSTEM_IOC_ACCESS_KEY);

	writel(phy_control, NETX_SYSTEM_PHY_CONTROL);

	return platform_driver_register(&netx_eth_driver);
}

static void __exit netx_eth_cleanup(void)
{
	platform_driver_unregister(&netx_eth_driver);
}

module_init(netx_eth_init);
module_exit(netx_eth_cleanup);

MODULE_AUTHOR("Sascha Hauer, Pengutronix");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" CARDNAME);
MODULE_FIRMWARE("xc0.bin");
MODULE_FIRMWARE("xc1.bin");
MODULE_FIRMWARE("xc2.bin");
