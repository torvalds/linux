/*******************************************************************************
 * Copyright © 2012, Shuge
 *		Author: shuge  <shugeLinux@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 ********************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/crc32.h>
#include <linux/mii.h>
#include <linux/if.h>
#include <linux/if_vlan.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/prefetch.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/ctype.h>
#include <linux/gpio.h>

#include <plat/sys_config.h>
#include <mach/gpio.h>
#include <mach/clock.h>

#ifdef CONFIG_GMAC_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif

#include "sunxi_gmac.h"
#include "gmac_desc.h"
#include "gmac_ethtool.h"

#undef GMAC_DEBUG
#ifdef GMAC_DEBUG
#define DBG(nlevel, klevel, fmt, args...) \
		((void)(netif_msg_##nlevel(priv) && \
		printk(KERN_##klevel fmt, ## args)))
#else
#define DBG(nlevel, klevel, fmt, args...) do { } while (0)
#endif

#undef RX_DEBUG
/*#define RX_DEBUG*/
#ifdef RX_DEBUG
#define RX_DBG(fmt, args...)  printk(fmt, ## args)
#else
#define RX_DBG(fmt, args...)  do { } while (0)
#endif

#undef XMIT_DEBUG
/*#define XMIT_DEBUG*/
#ifdef XMIT_DEBUG
#define TX_DBG(fmt, args...)  printk(fmt, ## args)
#else
#define TX_DBG(fmt, args...)  do { } while (0)
#endif

#define GMAC_ALIGN(x)	L1_CACHE_ALIGN(x)
#define JUMBO_LEN	9000

#define GMAC_MAC_ADDRESS "00:00:00:00:00:00"
static char *mac_str = GMAC_MAC_ADDRESS;
module_param(mac_str, charp, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(mac_str, "MAC Address String.(xx:xx:xx:xx:xx:xx)");

#define TX_TIMEO	5000
static int watchdog = TX_TIMEO;
module_param(watchdog, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(watchdog, "Transmit timeout in milliseconds");

static int debug = -1;		/* -1: default, 0: no output, 16:  all */
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Message Level (0: no output, 16: all)");

static int phyaddr = -1;
module_param(phyaddr, int, S_IRUGO);
MODULE_PARM_DESC(phyaddr, "Physical device address");

#define DMA_TX_SIZE 256
static int dma_txsize = DMA_TX_SIZE;
module_param(dma_txsize, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(dma_txsize, "Number of descriptors in the TX list");

#define DMA_RX_SIZE 256
static int dma_rxsize = DMA_RX_SIZE;
module_param(dma_rxsize, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(dma_rxsize, "Number of descriptors in the RX list");

static int flow_ctrl = FLOW_OFF;
module_param(flow_ctrl, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(flow_ctrl, "Flow control ability [on/off]");

static int pause = PAUSE_TIME;
module_param(pause, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(pause, "Flow Control Pause Time");

#define TC_DEFAULT 64
static int tc = TC_DEFAULT;
module_param(tc, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(tc, "DMA threshold control value");

/* Pay attention to tune this parameter; take care of both
 * hardware capability and network stabitily/performance impact.
 * Many tests showed that ~4ms latency seems to be good enough. */
#ifdef CONFIG_GMAC_TIMER
#define DEFAULT_PERIODIC_RATE	256
static int tmrate = DEFAULT_PERIODIC_RATE;
module_param(tmrate, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(tmrate, "External timer freq. (default: 256Hz)");
#endif

#define DMA_BUFFER_SIZE	BUF_SIZE_2KiB
static int buf_sz = DMA_BUFFER_SIZE;
module_param(buf_sz, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(buf_sz, "DMA buffer size");

static int gmac_used;

static const u32 default_msg_level = (NETIF_MSG_DRV | NETIF_MSG_PROBE |
				      NETIF_MSG_LINK | NETIF_MSG_IFUP |
				      NETIF_MSG_IFDOWN | NETIF_MSG_TIMER);

static irqreturn_t gmac_interrupt(int irq, void *dev_id);

#ifdef CONFIG_GMAC_DEBUG_FS
static int gmac_init_fs(struct net_device *dev);
static void gmac_exit_fs(void);
#endif

#if defined(XMIT_DEBUG) || defined(RX_DEBUG)
static void print_pkt(unsigned char *buf, int len)
{
	int j;
	printk("len = %d byte, buf addr: 0x%p", len, buf);
	for (j = 0; j < len; j++) {
		if ((j % 16) == 0)
			printk("\n %03x:", j);
		printk(" %02x", buf[j]);
	}
	printk("\n");
}
#endif

/* minimum number of free TX descriptors required to wake up TX process */
#define GMAC_TX_THRESH(x)	(x->dma_tx_size/4)

static inline u32 gmac_tx_avail(struct gmac_priv *priv)
{
	return priv->dirty_tx + priv->dma_tx_size - priv->cur_tx - 1;
}

/**
 * gmac_clk_ctl
 * @flag: 0--disable, 1--enable.
 * Description: enable and disable gmac clk.
 */
static void gmac_clk_ctl(struct gmac_priv *priv, unsigned int flag)
{
	int phy_interface = priv->plat->phy_interface;
	u32  priv_clk_reg;

#ifndef CONFIG_GMAC_CLK_SYS
	int reg_value;
	reg_value = readl(priv->clkbase + AHB1_GATING);
	flag ? (reg_value |= GMAC_AHB_BIT) : (reg_value &= ~GMAC_AHB_BIT);
	writel(reg_value, priv->clkbase + AHB1_GATING);
/*
	reg_value = readl(priv->clkbase + AHB1_MOD_RESET);
	flag ? (reg_value |= GMAC_RESET_BIT) : (reg_value &= ~GMAC_RESET_BIT);
	writel(reg_value, priv->clkbase + AHB1_MOD_RESET);
*/
#else
	if (flag) {
		clk_enable(priv->gmac_ahb_clk);
        /*
		clk_reset(priv->gmac_mod_clk, AW_CCU_CLK_NRESET);
        */
	} else {
		clk_disable(priv->gmac_ahb_clk);
        /*
		clk_reset(priv->gmac_mod_clk, AW_CCU_CLK_RESET);
        */
	}
#endif

	/* We should set the interface type. */
	priv_clk_reg = readl(priv->gmac_clk_reg + GMAC_CLK_REG);

	if (phy_interface == PHY_INTERFACE_MODE_RGMII)
		priv_clk_reg |= 0x00000004;
	else
		priv_clk_reg &= (~0x00000004);

	/* Set gmac transmit clock source. */
	priv_clk_reg &= (~0x00000003);
	if (phy_interface == PHY_INTERFACE_MODE_RGMII
			|| phy_interface == PHY_INTERFACE_MODE_GMII)
		priv_clk_reg |= 0x00000002;

	writel(priv_clk_reg, priv->gmac_clk_reg + GMAC_CLK_REG);
}

/**
 * gmac_adjust_link
 * @dev: net device structure
 * Description: it adjusts the link parameters.
 */
static void gmac_adjust_link(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;
	unsigned long flags;
	int new_state = 0;
	unsigned int fc = priv->flow_ctrl, pause_time = priv->pause;

	if (phydev == NULL)
		return;

	DBG(probe, DEBUG, "gmac_adjust_link: called.  address %d link %d\n",
	    phydev->addr, phydev->link);

	spin_lock_irqsave(&priv->lock, flags);
	if (phydev->link) {
		u32 ctrl = readl(priv->ioaddr + GMAC_CONTROL);

		/* Now we make sure that we can be in full duplex mode.
		 * If not, we operate in half-duplex mode. */
		if (phydev->duplex != priv->oldduplex) {
			new_state = 1;
			if (!(phydev->duplex))
				ctrl &= ~GMAC_CTL_DM;
			else
				ctrl |= GMAC_CTL_DM;
			priv->oldduplex = phydev->duplex;
		}
		/* Flow Control operation */
		if (phydev->pause)
			core_flow_ctrl(priv->ioaddr, phydev->duplex,
						 fc, pause_time);

		if (phydev->speed != priv->speed) {
			new_state = 1;
			switch (phydev->speed) {
			case 1000:
					ctrl &= ~GMAC_CTL_PS;
				break;
			case 100:
			case 10:
				ctrl |= GMAC_CTL_PS;
				if (phydev->speed == SPEED_100)
					ctrl |= GMAC_CTL_FES;
				 else
					ctrl &= ~GMAC_CTL_FES;
				break;
			default:
				if (netif_msg_link(priv))
					pr_warning("%s: Speed (%d) is not 10"
				       " or 100!\n", ndev->name, phydev->speed);
				break;
			}

			priv->speed = phydev->speed;
		}

		writel(ctrl, priv->ioaddr + GMAC_CONTROL);

		if (!priv->oldlink) {
			new_state = 1;
			priv->oldlink = 1;
		}
	} else if (priv->oldlink) {
		new_state = 1;
		priv->oldlink = 0;
		priv->speed = 0;
		priv->oldduplex = -1;
	}

	if (new_state && netif_msg_link(priv))
		phy_print_status(phydev);

	spin_unlock_irqrestore(&priv->lock, flags);

	DBG(probe, DEBUG, "gmac_adjust_link: exiting\n");
}

/**
 * gmac_init_phy - PHY initialization
 * @dev: net device structure
 * Description: it initializes the driver's PHY state, and attaches the PHY
 * to the mac driver.
 *  Return value:
 *  0 on success
 */
static int gmac_init_phy(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	struct phy_device *phydev;
	char phy_id[MII_BUS_ID_SIZE + 3];
	char bus_id[MII_BUS_ID_SIZE];
	int phy_interface = priv->plat->phy_interface;

	/* Initialize the information of phy state. */
	priv->oldlink = 0;
	priv->speed = 0;
	priv->oldduplex = -1;

	snprintf(bus_id, MII_BUS_ID_SIZE, "sunxi_gmac-%x", priv->plat->bus_id);
	snprintf(phy_id, MII_BUS_ID_SIZE + 3, PHY_ID_FMT, bus_id,
		 priv->plat->phy_addr);
	pr_debug("gmac_init_phy:  trying to attach to %s\n", phy_id);

	phydev = phy_connect(ndev, phy_id, &gmac_adjust_link, 0, phy_interface);

	if (IS_ERR(phydev)) {
		pr_err("%s: Could not attach to PHY\n", ndev->name);
		return PTR_ERR(phydev);
	}

	/* Stop Advertising 1000BASE Capability if interface is not GMII */
	if ((phy_interface == PHY_INTERFACE_MODE_MII) ||
	    (phy_interface == PHY_INTERFACE_MODE_RMII))
		phydev->advertising &= ~(SUPPORTED_1000baseT_Half |
					 SUPPORTED_1000baseT_Full);

	/*
	 * Broken HW is sometimes missing the pull-up resistor on the
	 * MDIO line, which results in reads to non-existent devices returning
	 * 0 rather than 0xffff. Catch this here and treat 0 as a non-existent
	 * device as well.
	 * Note: phydev->phy_id is the result of reading the UID PHY registers.
	 */
	if (phydev->phy_id == 0) {
		phy_disconnect(phydev);
		return -ENODEV;
	}
	pr_debug("gmac_init_phy:  %s: attached to PHY (UID 0x%x)"
		 " Link = %d\n", ndev->name, phydev->phy_id, phydev->link);

	return 0;
}

/**
 * display_ring
 * @p: pointer to the ring.
 * @size: size of the ring.
 * Description: display all the descriptors within the ring.
 */
static void display_ring(dma_desc_t *p, int size)
{
	struct tmp_s {
		u64 a;
		unsigned int b;
		unsigned int c;
	};
	int i;
	for (i = 0; i < size; i++) {
		struct tmp_s *x = (struct tmp_s *)(p + i);
		pr_info("\t%d [0x%x]: DES0=0x%x DES1=0x%x BUF1=0x%x BUF2=0x%x",
		       i, (unsigned int)virt_to_phys(&p[i]),
		       (unsigned int)(x->a), (unsigned int)((x->a) >> 32),
		       x->b, x->c);
		pr_info("\n");
	}
}

static int gmac_set_bfsize(int mtu, int bufsize)
{
	int ret = bufsize;

	if (mtu >= BUF_SIZE_4KiB)
		ret = BUF_SIZE_8KiB;
	else if (mtu >= BUF_SIZE_2KiB)
		ret = BUF_SIZE_4KiB;
	else if (mtu >= DMA_BUFFER_SIZE)
		ret = BUF_SIZE_2KiB;
	else
		ret = DMA_BUFFER_SIZE;

	return ret;
}

/**
 * init_dma_desc_rings - init the RX/TX descriptor rings
 * @dev: net device structure
 * Description:  this function initializes the DMA RX/TX descriptors
 * and allocates the socket buffers. It suppors the chained and ring
 * modes.
 */
static void init_dma_desc_rings(struct net_device *ndev)
{
	int i;
	struct gmac_priv *priv = netdev_priv(ndev);
	struct sk_buff *skb;
	unsigned int txsize = priv->dma_tx_size;
	unsigned int rxsize = priv->dma_rx_size;
	unsigned int bfsize;
	int dis_ic = 0;
	int des3_as_data_buf = 0;

	/* Set the max buffer size according to the DESC mode
	 * and the MTU. Note that RING mode allows 16KiB bsize. */
	bfsize = gmac_set_16kib_bfsize(ndev->mtu);

	if (bfsize == BUF_SIZE_16KiB)
		des3_as_data_buf = 1;
	else
		bfsize = gmac_set_bfsize(ndev->mtu, priv->dma_buf_sz);

#ifdef CONFIG_GMAC_TIMER
	/* Disable interrupts on completion for the reception if timer is on */
	if (likely(priv->tm->enable))
		dis_ic = 1;
#endif

	DBG(probe, INFO, "gmac: txsize %d, rxsize %d, bfsize %d\n",
	    txsize, rxsize, bfsize);

	priv->rx_skbuff_dma = kmalloc(rxsize * sizeof(dma_addr_t), GFP_KERNEL);
	priv->rx_skbuff		= kmalloc(sizeof(struct sk_buff *) * rxsize, GFP_KERNEL);
	priv->dma_rx =
	    (dma_desc_t *)dma_alloc_coherent(NULL,
						  rxsize * sizeof(dma_desc_t),
						  &priv->dma_rx_phy,
						  GFP_KERNEL);

	priv->tx_skbuff = kmalloc(sizeof(struct sk_buff *) * txsize,
				       GFP_KERNEL);
	priv->dma_tx =
	    (dma_desc_t *)dma_alloc_coherent(NULL,
						  txsize * sizeof(dma_desc_t),
						  &priv->dma_tx_phy,
						  GFP_KERNEL);

	if ((priv->dma_rx == NULL) || (priv->dma_tx == NULL)) {
		pr_err("%s:ERROR allocating the DMA Tx/Rx desc\n", __func__);
		return;
	}

	DBG(probe, INFO, "gmac (%s) DMA desc: virt addr (Rx %p, "
	    "Tx %p)\n\tDMA phy addr (Rx 0x%08x, Tx 0x%08x)\n",
	    ndev->name, priv->dma_rx, priv->dma_tx,
	    (unsigned int)priv->dma_rx_phy, (unsigned int)priv->dma_tx_phy);

	/* RX INITIALIZATION */
	DBG(probe, INFO, "gmac: SKB addresses:\n"
			 "skb\t\tskb data\tdma data\n");

	for (i = 0; i < rxsize; i++) {
		dma_desc_t *p = priv->dma_rx + i;

		skb = __netdev_alloc_skb(ndev, bfsize + NET_IP_ALIGN,
					 GFP_KERNEL);
		if (unlikely(skb == NULL)) {
			pr_err("%s: Rx init fails; skb is NULL\n", __func__);
			break;
		}
		skb_reserve(skb, NET_IP_ALIGN);
		priv->rx_skbuff[i] = skb;
		priv->rx_skbuff_dma[i] = dma_map_single(priv->device, skb->data,
						bfsize, DMA_FROM_DEVICE);

		p->desc2 = priv->rx_skbuff_dma[i];

		gmac_init_desc3(des3_as_data_buf, p);

		DBG(probe, INFO, "[%p]\t[%p]\t[%x]\n", priv->rx_skbuff[i],
			priv->rx_skbuff[i]->data, priv->rx_skbuff_dma[i]);
	}
	priv->cur_rx = 0;
	priv->dirty_rx = (unsigned int)(i - rxsize);
	priv->dma_buf_sz = bfsize;
	buf_sz = bfsize;

	/* TX INITIALIZATION */
	for (i = 0; i < txsize; i++) {
		priv->tx_skbuff[i] = NULL;
		priv->dma_tx[i].desc2 = 0;
	}

	/* In case of Chained mode this sets the des3 to the next
	 * element in the chain */
	gmac_init_dma_chain(priv->dma_rx, priv->dma_rx_phy, rxsize);
	gmac_init_dma_chain(priv->dma_tx, priv->dma_tx_phy, txsize);

	priv->dirty_tx = 0;
	priv->cur_tx = 0;

	/* Clear the Rx/Tx descriptors */
	desc_init_rx(priv->dma_rx, rxsize, dis_ic);
	desc_init_tx(priv->dma_tx, txsize);

	if (netif_msg_hw(priv)) {
		printk("RX descriptor ring:\n");
		display_ring(priv->dma_rx, rxsize);
		printk("TX descriptor ring:\n");
		display_ring(priv->dma_tx, txsize);
	}
}

static void dma_free_rx_skbufs(struct gmac_priv *priv)
{
	int i;

	for (i = 0; i < priv->dma_rx_size; i++) {
		if (priv->rx_skbuff[i]) {
			dma_unmap_single(priv->device, priv->rx_skbuff_dma[i],
					 priv->dma_buf_sz, DMA_FROM_DEVICE);
			dev_kfree_skb_any(priv->rx_skbuff[i]);
		}
		priv->rx_skbuff[i] = NULL;
	}
}

static void dma_free_tx_skbufs(struct gmac_priv *priv)
{
	int i;

	for (i = 0; i < priv->dma_tx_size; i++) {
		if (priv->tx_skbuff[i] != NULL) {
			dma_desc_t *p = priv->dma_tx + i;
			if (p->desc2)
				dma_unmap_single(priv->device, p->desc2,
						 desc_get_tx_len(p),
						 DMA_TO_DEVICE);
			dev_kfree_skb_any(priv->tx_skbuff[i]);
			priv->tx_skbuff[i] = NULL;
		}
	}
}

static void free_dma_desc_resources(struct gmac_priv *priv)
{
	/* Release the DMA TX/RX socket buffers */
	dma_free_rx_skbufs(priv);
	dma_free_tx_skbufs(priv);

	/* Free the region of consistent memory previously allocated for
	 * the DMA */
	dma_free_coherent(NULL,
			  priv->dma_tx_size * sizeof(dma_desc_t),
			  priv->dma_tx, priv->dma_tx_phy);
	dma_free_coherent(NULL,
			  priv->dma_rx_size * sizeof(dma_desc_t),
			  priv->dma_rx, priv->dma_rx_phy);
	kfree(priv->rx_skbuff_dma);
	kfree(priv->rx_skbuff);
	kfree(priv->tx_skbuff);
}

/**
 *  gmac_dma_operation_mode - HW DMA operation mode
 *  @priv : pointer to the private device structure.
 *  Description: it sets the DMA operation mode: tx/rx DMA thresholds
 *  or Store-And-Forward capability.
 */
static void gmac_dma_operation_mode(struct gmac_priv *priv)
{
	if (likely(priv->plat->force_sf_dma_mode ||
		((priv->plat->tx_coe) && (!priv->no_csum_insertion)))) {
		/*
		 * In case of GMAC, SF mode can be enabled
		 * to perform the TX COE in HW. This depends on:
		 * 1) TX COE if actually supported
		 * 2) There is no bugged Jumbo frame support
		 *    that needs to not insert csum in the TDES.
		 */
		dma_oper_mode(priv->ioaddr,
					SF_DMA_MODE, SF_DMA_MODE);
		tc = SF_DMA_MODE;
	} else
		dma_oper_mode(priv->ioaddr, tc, SF_DMA_MODE);
}

/**
 * gmac_tx:
 * @priv: private driver structure
 * Description: it reclaims resources after transmission completes.
 */
static void gmac_tx(struct gmac_priv *priv)
{
	unsigned int txsize = priv->dma_tx_size;

	spin_lock(&priv->tx_lock);

	while (priv->dirty_tx != priv->cur_tx) {
		int last;
		unsigned int entry = priv->dirty_tx % txsize;
		struct sk_buff *skb = priv->tx_skbuff[entry];
		dma_desc_t *p = priv->dma_tx + entry;

		/* Check if the descriptor is owned by the DMA. */
		if (desc_get_tx_own(p))
			break;

		/* Verify tx error by looking at the last segment */
		last = desc_get_tx_ls(p);
		if (likely(last)) {
			int tx_error =
				desc_get_tx_status(&priv->ndev->stats,
							  &priv->xstats, p,
							  priv->ioaddr);
			if (likely(tx_error == 0)) {
				priv->ndev->stats.tx_packets++;
				priv->xstats.tx_pkt_n++;
			} else
				priv->ndev->stats.tx_errors++;
		}
		TX_DBG("%s: curr %d, dirty %d\n", __func__,
			priv->cur_tx, priv->dirty_tx);

		if (likely(p->desc2))
			dma_unmap_single(priv->device, p->desc2,
					 desc_get_tx_len(p),
					 DMA_TO_DEVICE);
		gmac_clean_desc3(p);

		if (likely(skb != NULL)) {
			/*
			 * If there's room in the queue (limit it to size)
			 * we add this skb back into the pool,
			 * if it's the right size.
			 */
			if ((skb_queue_len(&priv->rx_recycle) <
				priv->dma_rx_size) &&
				skb_recycle_check(skb, priv->dma_buf_sz))
				__skb_queue_head(&priv->rx_recycle, skb);
			else
				dev_kfree_skb(skb);

			priv->tx_skbuff[entry] = NULL;
		}

		desc_release_tx(p);

		entry = (++priv->dirty_tx) % txsize;
	}
	if (unlikely(netif_queue_stopped(priv->ndev) &&
		     gmac_tx_avail(priv) > GMAC_TX_THRESH(priv))) {
		netif_tx_lock(priv->ndev);
		if (netif_queue_stopped(priv->ndev) &&
		     gmac_tx_avail(priv) > GMAC_TX_THRESH(priv)) {
			TX_DBG("%s: restart transmit\n", __func__);
			netif_wake_queue(priv->ndev);
		}
		netif_tx_unlock(priv->ndev);
	}
	spin_unlock(&priv->tx_lock);
}

static inline void gmac_enable_irq(struct gmac_priv *priv)
{
#ifdef CONFIG_GMAC_TIMER
	if (likely(priv->tm->enable))
		priv->tm->timer_start(tmrate);
	else
#endif
		dma_en_irq(priv->ioaddr);
}

static inline void gmac_disable_irq(struct gmac_priv *priv)
{
#ifdef CONFIG_GMAC_TIMER
	if (likely(priv->tm->enable))
		priv->tm->timer_stop();
	else
#endif
		dma_dis_irq(priv->ioaddr);
}

static int gmac_has_work(struct gmac_priv *priv)
{
	unsigned int has_work = 0;
	int rxret, tx_work = 0;

	rxret = desc_get_rx_own(priv->dma_rx + (priv->cur_rx % priv->dma_rx_size));

	if (priv->dirty_tx != priv->cur_tx)
		tx_work = 1;

	if (likely(!rxret || tx_work))
		has_work = 1;

	return has_work;
}

static inline void _gmac_schedule(struct gmac_priv *priv)
{
	if (likely(gmac_has_work(priv))) {
		gmac_disable_irq(priv);
		napi_schedule(&priv->napi);
	}
}

#ifdef CONFIG_GMAC_TIMER
void gmac_schedule(struct net_device *dev)
{
	struct gmac_priv *priv = netdev_priv(dev);

	priv->xstats.sched_timer_n++;

	_gmac_schedule(priv);
}

static void gmac_no_timer_started(unsigned int x)
{;
};

static void gmac_no_timer_stopped(void)
{;
};
#endif

/**
 * gmac_tx_err:
 * @priv: pointer to the private device structure
 * Description: it cleans the descriptors and restarts the transmission
 * in case of errors.
 */
static void gmac_tx_err(struct gmac_priv *priv)
{
	netif_stop_queue(priv->ndev);

	dma_stop_tx(priv->ioaddr);
	dma_free_tx_skbufs(priv);
	desc_init_tx(priv->dma_tx, priv->dma_tx_size);
	priv->dirty_tx = 0;
	priv->cur_tx = 0;
	dma_start_tx(priv->ioaddr);

	priv->ndev->stats.tx_errors++;
	netif_wake_queue(priv->ndev);
}


static void gmac_dma_interrupt(struct gmac_priv *priv)
{
	int status;

	status = dma_interrupt(priv->ioaddr, &priv->xstats);
	if (likely(status == handle_tx_rx))
		_gmac_schedule(priv);

	else if (unlikely(status == tx_hard_error_bump_tc)) {
		/* Try to bump up the dma threshold on this failure */
		if (unlikely(tc != SF_DMA_MODE) && (tc <= 256)) {
			tc += 64;
			dma_oper_mode(priv->ioaddr, tc, SF_DMA_MODE);
			priv->xstats.threshold = tc;
		}
	} else if (unlikely(status == tx_hard_error))
		gmac_tx_err(priv);
}

static void gmac_check_ether_addr(struct gmac_priv *priv)
{
	int i;
	char *p = mac_str;
	/* verify if the MAC address is valid, in case of failures it
	 * generates a random MAC address */
	if (!is_valid_ether_addr(priv->ndev->dev_addr)) {
		if  (!is_valid_ether_addr(priv->ndev->dev_addr)) {
			for (i=0; i<6; i++,p++)
				priv->ndev->dev_addr[i] = simple_strtoul(p, &p, 16);
		}

		if  (!is_valid_ether_addr(priv->ndev->dev_addr))
			random_ether_addr(priv->ndev->dev_addr);
	}
	printk(KERN_WARNING "%s: device MAC address %pM\n", priv->ndev->name,
						   priv->ndev->dev_addr);
}

/**
 *  gmac_open - open entry point of the driver
 *  @dev : pointer to the device structure.
 *  Description:
 *  This function is the open entry point of the driver.
 *  Return value:
 *  0 on success and an appropriate (-)ve integer as defined in errno.h
 *  file on failure.
 */
static int gmac_open(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	int ret;

	gmac_clk_ctl(priv, 1);
	//gmac_check_ether_addr(priv);

	/* MDIO bus Registration */
	ret = gmac_mdio_register(ndev);
	if (ret < 0) {
		pr_debug("%s: MDIO bus (id: %d) registration failed",
			 __func__, priv->plat->bus_id);
		goto out_err;
	}

	ret = gmac_init_phy(ndev);
	if (unlikely(ret)) {
		pr_err("%s: Cannot attach to PHY (error: %d)\n", __func__, ret);
		goto out_err;
	}

	/* Create and initialize the TX/RX descriptors chains. */
	priv->dma_tx_size = GMAC_ALIGN(dma_txsize);
	priv->dma_rx_size = GMAC_ALIGN(dma_rxsize);
	priv->dma_buf_sz = GMAC_ALIGN(buf_sz);
	init_dma_desc_rings(ndev);

	/* DMA initialization and SW reset */
	ret = gdma_init(priv->ioaddr, priv->plat->pbl,
				  priv->dma_tx_phy, priv->dma_rx_phy);
	if (ret < 0) {
		pr_err("%s: DMA initialization failed\n", __func__);
		goto open_error;
	}

	/* Copy the MAC addr into the HW  */
	gmac_set_umac_addr(priv->ioaddr, ndev->dev_addr, 0);

	/* Initialize the MAC Core */
	core_init(priv->ioaddr);

	/* Request the IRQ lines */
	ret = request_irq(ndev->irq, gmac_interrupt,
			 IRQF_SHARED, ndev->name, ndev);
	if (unlikely(ret < 0)) {
		pr_err("%s: ERROR: allocating the IRQ %d (error: %d)\n",
		       __func__, ndev->irq, ret);
		goto open_error;
	}

	/* Enable the MAC Rx/Tx */
	gmac_set_tx_rx(priv->ioaddr, true);

	/* Set the HW DMA mode and the COE */
	gmac_dma_operation_mode(priv);

	/* Extra statistics */
	memset(&priv->xstats, 0, sizeof(struct gmac_extra_stats));
	priv->xstats.threshold = tc;

#ifdef CONFIG_GMAC_DEBUG_FS
	ret = gmac_init_fs(ndev);
	if (ret < 0)
		pr_warning("%s: failed debugFS registration\n", __func__);
#endif
	/* Start the ball rolling... */
	DBG(probe, DEBUG, "%s: DMA RX/TX processes started...\n", ndev->name);
	dma_start_tx(priv->ioaddr);
	dma_start_rx(priv->ioaddr);

	/* Dump DMA/MAC registers */
	if (netif_msg_hw(priv)) {
		core_dump_regs(priv->ioaddr);
		dma_dump_regs(priv->ioaddr);
	}

	if (ndev->phydev)
		phy_start(ndev->phydev);

	napi_enable(&priv->napi);
	skb_queue_head_init(&priv->rx_recycle);
	netif_start_queue(ndev);

	return 0;

open_error:
	if (ndev->phydev)
		phy_disconnect(ndev->phydev);
	free_dma_desc_resources(priv);
out_err:
	gmac_clk_ctl(priv, 0);

	return ret;
}

/**
 *  gmac_release - close entry point of the driver
 *  @dev : device pointer.
 *  Description:
 *  This is the stop entry point of the driver.
 */
static int gmac_release(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);

	/* Stop and disconnect the PHY */
	if (ndev->phydev) {
		phy_stop(ndev->phydev);
		phy_disconnect(ndev->phydev);
		ndev->phydev = NULL;
	}

	netif_stop_queue(ndev);

#ifdef CONFIG_GMAC_TIMER
	/* Stop and release the timer */
	gmac_close_ext_timer();
	if (priv->tm != NULL)
		kfree(priv->tm);
#endif
	napi_disable(&priv->napi);
	skb_queue_purge(&priv->rx_recycle);

	/* Free the IRQ lines */
	free_irq(ndev->irq, ndev);

	/* Stop TX/RX DMA and clear the descriptors */
	dma_stop_tx(priv->ioaddr);
	dma_stop_rx(priv->ioaddr);

	/* Release and free the Rx/Tx resources */
	free_dma_desc_resources(priv);

	/* Disable the MAC Rx/Tx */
	gmac_set_tx_rx(priv->ioaddr, false);

	netif_carrier_off(ndev);

#ifdef CONFIG_GMAC_DEBUG_FS
	gmac_exit_fs();
#endif
	gmac_mdio_unregister(ndev);
	gmac_clk_ctl(priv, 0);

	return 0;
}

/**
 *  gmac_xmit:
 *  @skb : the socket buffer
 *  @dev : device pointer
 *  Description : Tx entry point of the driver.
 */
static netdev_tx_t gmac_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct gmac_priv *priv = netdev_priv(dev);
	unsigned int txsize = priv->dma_tx_size;
	unsigned int entry;
	int i, csum_insertion = 0;
	int nfrags = skb_shinfo(skb)->nr_frags;
	dma_desc_t *desc, *first;
	unsigned int nopaged_len = skb_headlen(skb);

	if (unlikely(gmac_tx_avail(priv) < nfrags + 1)) {
		if (!netif_queue_stopped(dev)) {
			netif_stop_queue(dev);
			/* This is a hard error, log it. */
			pr_err("%s: BUG! Tx Ring full when queue awake\n",
				__func__);
		}
		return NETDEV_TX_BUSY;
	}

	spin_lock(&priv->tx_lock);

	entry = priv->cur_tx % txsize;

#ifdef XMIT_DEBUG
	if ((skb->len > ETH_FRAME_LEN) || nfrags)
		pr_info("gmac xmit:\n"
		       "\tskb addr %p - len: %d - nopaged_len: %d\n"
		       "\tn_frags: %d - ip_summed: %d - %s gso\n",
		       skb, skb->len, nopaged_len, nfrags, skb->ip_summed,
		       !skb_is_gso(skb) ? "isn't" : "is");
#endif

	csum_insertion = (skb->ip_summed == CHECKSUM_PARTIAL);

	desc = priv->dma_tx + entry;
	first = desc;

#ifdef XMIT_DEBUG
	if ((nfrags > 0) || (skb->len > ETH_FRAME_LEN))
		pr_debug("gmac xmit: skb len: %d, nopaged_len: %d,\n"
		       "\t\tn_frags: %d, ip_summed: %d\n",
		       skb->len, nopaged_len, nfrags, skb->ip_summed);
#endif

	priv->tx_skbuff[entry] = skb;

	if (gmac_is_jumbo_frm(skb->len)) {
		entry = gmac_jumbo_frm(priv, skb, csum_insertion);
		desc = priv->dma_tx + entry;
	} else {
		desc->desc2 = dma_map_single(priv->device, skb->data,
								nopaged_len, DMA_TO_DEVICE);
		desc_prepare_tx(desc, 1, nopaged_len, csum_insertion);
	}

	for (i = 0; i < nfrags; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		int len = skb_frag_size(frag);

		entry = (++priv->cur_tx) % txsize;
		desc = priv->dma_tx + entry;

		TX_DBG("\t[entry %d] segment len: %d\n", entry, len);
		desc->desc2 = skb_frag_dma_map(priv->device, frag, 0,
									len, DMA_TO_DEVICE);
		priv->tx_skbuff[entry] = NULL;
		desc_prepare_tx(desc, 0, len, csum_insertion);
		wmb();
		desc_set_tx_own(desc);
	}

	/* Interrupt on completition only for the latest segment */
	desc_close_tx(desc);

#ifdef CONFIG_GMAC_TIMER
	/* Clean IC while using timer */
	if (likely(priv->tm->enable))
		desc_clear_tx_ic(desc);
#endif

	wmb();

	/* To avoid raise condition */
	desc_set_tx_own(first);

	priv->cur_tx++;

#ifdef XMIT_DEBUG
	if (netif_msg_pktdata(priv)) {
		pr_info("gmac xmit: current=%d, dirty=%d, entry=%d, "
		       "first=%p, nfrags=%d\n",
		       (priv->cur_tx % txsize), (priv->dirty_tx % txsize),
		       entry, first, nfrags);
		display_ring(priv->dma_tx, txsize);
		pr_info(">>> frame to be transmitted: ");
		print_pkt(skb->data, skb->len);
	}
#endif

	if (unlikely(gmac_tx_avail(priv) <= (MAX_SKB_FRAGS + 1))) {
		TX_DBG("%s: stop transmitted packets\n", __func__);
		netif_stop_queue(dev);
	}

	dev->stats.tx_bytes += skb->len;

	skb_tx_timestamp(skb);

	dma_en_tx(priv->ioaddr);

	spin_unlock(&priv->tx_lock);

	return NETDEV_TX_OK;
}

static inline void gmac_rx_refill(struct gmac_priv *priv)
{
	unsigned int rxsize = priv->dma_rx_size;
	int bfsize = priv->dma_buf_sz;
	dma_desc_t *p = priv->dma_rx;

	for (; priv->cur_rx - priv->dirty_rx > 0; priv->dirty_rx++) {
		unsigned int entry = priv->dirty_rx % rxsize;
		if (likely(priv->rx_skbuff[entry] == NULL)) {
			struct sk_buff *skb;

			skb = __skb_dequeue(&priv->rx_recycle);
			if (skb == NULL)
				skb = netdev_alloc_skb_ip_align(priv->ndev,
								bfsize);

			if (unlikely(skb == NULL))
				break;

			priv->rx_skbuff[entry] = skb;
			priv->rx_skbuff_dma[entry] =
			    dma_map_single(priv->device, skb->data, bfsize,
					   DMA_FROM_DEVICE);

			(p + entry)->desc2 = priv->rx_skbuff_dma[entry];

				gmac_refill_desc3(bfsize, p + entry);

			RX_DBG(KERN_INFO "\trefill entry #%d\n", entry);
		}
		wmb();
		desc_set_rx_own(p + entry);
	}
}

static int gmac_rx(struct gmac_priv *priv, int limit)
{
	unsigned int rxsize = priv->dma_rx_size;
	unsigned int entry = priv->cur_rx % rxsize;
	unsigned int next_entry;
	unsigned int count = 0;
	dma_desc_t *p = priv->dma_rx + entry;
	dma_desc_t *p_next;

#ifdef RX_DEBUG
	if (netif_msg_hw(priv)) {
		pr_debug(">>> gmac_rx: descriptor ring:\n");
		display_ring(priv->dma_rx, rxsize);
	}
#endif

	count = 0;
	while (!desc_get_rx_own(p)) {
		int status;

		if (count >= limit)
			break;

		count++;

		next_entry = (++priv->cur_rx) % rxsize;
		p_next = priv->dma_rx + next_entry;
		prefetch(p_next);

		/* read the status of the incoming frame */
		status = (desc_get_rx_status(&priv->ndev->stats,
						    &priv->xstats, p));
		if (unlikely(status == discard_frame))
			priv->ndev->stats.rx_errors++;
		else {
			struct sk_buff *skb;
			int frame_len;

			frame_len = desc_get_rx_frame_len(p);
			/* ACS is set; GMAC core strips PAD/FCS for IEEE 802.3
			 * Type frames (LLC/LLC-SNAP) */
			if (unlikely(status != llc_snap))
				frame_len -= ETH_FCS_LEN;
#ifdef RX_DEBUG
			if (frame_len > ETH_FRAME_LEN)
				pr_debug("\tRX frame size %d, COE status: %d\n",
					frame_len, status);

			if (netif_msg_hw(priv))
				pr_debug("\tdesc: %p [entry %d] buff=0x%x\n",
					p, entry, p->desc2);
#endif
			skb = priv->rx_skbuff[entry];
			if (unlikely(!skb)) {
				pr_err("%s: Inconsistent Rx descriptor chain\n",
					priv->ndev->name);
				priv->ndev->stats.rx_dropped++;
				break;
			}
			prefetch(skb->data - NET_IP_ALIGN);
			priv->rx_skbuff[entry] = NULL;

			skb_put(skb, frame_len);
			dma_unmap_single(priv->device,
					 priv->rx_skbuff_dma[entry],
					 priv->dma_buf_sz, DMA_FROM_DEVICE);
#ifdef RX_DEBUG
			if (netif_msg_pktdata(priv)) {
				pr_info(" frame received (%dbytes)", frame_len);
				print_pkt(skb->data, frame_len);
			}
#endif
			skb->protocol = eth_type_trans(skb, priv->ndev);

			if (unlikely(!priv->rx_coe)) {
				/* No RX COE for old mac10/100 devices */
				skb_checksum_none_assert(skb);
				netif_receive_skb(skb);
			} else {
				skb->ip_summed = CHECKSUM_UNNECESSARY;
				napi_gro_receive(&priv->napi, skb);
			}

			priv->ndev->stats.rx_packets++;
			priv->ndev->stats.rx_bytes += frame_len;
		}
		entry = next_entry;
		p = p_next;	/* use prefetched values */
	}

	gmac_rx_refill(priv);

	priv->xstats.rx_pkt_n += count;

	return count;
}

/**
 *  gmac_poll - gmac poll method (NAPI)
 *  @napi : pointer to the napi structure.
 *  @budget : maximum number of packets that the current CPU can receive from
 *	      all interfaces.
 *  Description :
 *   This function implements the the reception process.
 *   Also it runs the TX completion thread
 */
static int gmac_poll(struct napi_struct *napi, int budget)
{
	struct gmac_priv *priv = container_of(napi, struct gmac_priv, napi);
	int work_done = 0;

	priv->xstats.poll_n++;
	gmac_tx(priv);
	work_done = gmac_rx(priv, budget);

	if (work_done < budget) {
		napi_complete(napi);
		gmac_enable_irq(priv);
	}
	return work_done;
}

/**
 *  gmac_tx_timeout
 *  @dev : Pointer to net device structure
 *  Description: this function is called when a packet transmission fails to
 *   complete within a reasonable tmrate. The driver will mark the error in the
 *   netdev structure and arrange for the device to be reset to a sane state
 *   in order to transmit a new packet.
 */
static void gmac_tx_timeout(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);

	/* Clear Tx resources and restart transmitting again */
	gmac_tx_err(priv);
}

/* Configuration changes (passed on by ifconfig) */
static int gmac_config(struct net_device *ndev, struct ifmap *map)
{
	if (ndev->flags & IFF_UP)	/* can't act on a running interface */
		return -EBUSY;

	/* Don't allow changing the I/O address */
	if (map->base_addr != ndev->base_addr) {
		printk(KERN_WARNING "%s: can't change I/O address\n", ndev->name);
		return -EOPNOTSUPP;
	}

	/* Don't allow changing the IRQ */
	if (map->irq != ndev->irq) {
		printk(KERN_WARNING "%s: can't change IRQ number %d\n",
		       ndev->name, ndev->irq);
		return -EOPNOTSUPP;
	}

	/* ignore other fields */
	return 0;
}

/**
 *  gmac_set_rx_mode - entry point for multicast addressing
 *  @dev : pointer to the device structure
 *  Description:
 *  This function is a driver entry point which gets called by the kernel
 *  whenever multicast addresses must be enabled/disabled.
 *  Return value:
 *  void.
 */
static void gmac_set_rx_mode(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);

	spin_lock(&priv->lock);
	core_set_filter(ndev);
	spin_unlock(&priv->lock);
}

/**
 *  gmac_change_mtu - entry point to change MTU size for the device.
 *  @dev : device pointer.
 *  @new_mtu : the new MTU size for the device.
 *  Description: the Maximum Transfer Unit (MTU) is used by the network layer
 *  to drive packet transmission. Ethernet has an MTU of 1500 octets
 *  (ETH_DATA_LEN). This value can be changed with ifconfig.
 *  Return value:
 *  0 on success and an appropriate (-)ve integer as defined in errno.h
 *  file on failure.
 */
static int gmac_change_mtu(struct net_device *ndev, int new_mtu)
{
	int max_mtu;

	if (netif_running(ndev)) {
		pr_err("%s: must be stopped to change its MTU\n", ndev->name);
		return -EBUSY;
	}

	max_mtu = SKB_MAX_HEAD(NET_SKB_PAD + NET_IP_ALIGN);

	if ((new_mtu < 46) || (new_mtu > max_mtu)) {
		pr_err("%s: invalid MTU, max MTU is: %d\n", ndev->name, max_mtu);
		return -EINVAL;
	}

	ndev->mtu = new_mtu;
	netdev_update_features(ndev);

	return 0;
}

static netdev_features_t gmac_fix_features(struct net_device *ndev,
	netdev_features_t features)
{
	struct gmac_priv *priv = netdev_priv(ndev);

	if (!priv->rx_coe)
		features &= ~NETIF_F_RXCSUM;
	if (!priv->plat->tx_coe)
		features &= ~NETIF_F_ALL_CSUM;

	/* Some GMAC devices have a bugged Jumbo frame support that
	 * needs to have the Tx COE disabled for oversized frames
	 * (due to limited buffer sizes). In this case we disable
	 * the TX csum insertionin the TDES and not use SF. */
	if (priv->plat->bugged_jumbo && (ndev->mtu > ETH_DATA_LEN))
		features &= ~NETIF_F_ALL_CSUM;

	return features;
}

static irqreturn_t gmac_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct gmac_priv *priv = netdev_priv(dev);

	if (unlikely(!dev)) {
		pr_err("%s: invalid dev pointer\n", __func__);
		return IRQ_NONE;
	}

	/* To handle GMAC own interrupts */
	core_irq_status((void __iomem *) dev->base_addr);

	gmac_dma_interrupt(priv);

	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/* Polling receive - used by NETCONSOLE and other diagnostic tools
 * to allow network I/O with interrupts disabled. */
static void gmac_poll_controller(struct net_device *dev)
{
	disable_irq(dev->irq);
	gmac_interrupt(dev->irq, dev);
	enable_irq(dev->irq);
}
#endif

/**
 *  gmac_ioctl - Entry point for the Ioctl
 *  @dev: Device pointer.
 *  @rq: An IOCTL specefic structure, that can contain a pointer to
 *  a proprietary structure used to pass information to the driver.
 *  @cmd: IOCTL command
 *  Description:
 *  Currently there are no special functionality supported in IOCTL, just the
 *  phy_mii_ioctl(...) can be invoked.
 */
static int gmac_ioctl(struct net_device *ndev, struct ifreq *rq, int cmd)
{
	int ret;

	if (!netif_running(ndev))
		return -EINVAL;

	if (!ndev->phydev)
		return -EINVAL;

	ret = phy_mii_ioctl(ndev->phydev, rq, cmd);

	return ret;
}

#ifdef CONFIG_GMAC_DEBUG_FS
static struct dentry *gmac_fs_dir;
static struct dentry *gmac_rings_status;

static int gmac_sysfs_ring_read(struct seq_file *seq, void *v)
{
	struct tmp_s {
		u64 a;
		unsigned int b;
		unsigned int c;
	};
	int i;
	struct net_device *dev = seq->private;
	struct gmac_priv *priv = netdev_priv(dev);

	seq_printk(seq, "=======================\n");
	seq_printk(seq, " RX descriptor ring\n");
	seq_printk(seq, "=======================\n");

	for (i = 0; i < priv->dma_rx_size; i++) {
		struct tmp_s *x = (struct tmp_s *)(priv->dma_rx + i);
		seq_printk(seq, "[%d] DES0=0x%x DES1=0x%x BUF1=0x%x BUF2=0x%x",
			   i, (unsigned int)(x->a),
			   (unsigned int)((x->a) >> 32), x->b, x->c);
		seq_printk(seq, "\n");
	}

	seq_printk(seq, "\n");
	seq_printk(seq, "=======================\n");
	seq_printk(seq, "  TX descriptor ring\n");
	seq_printk(seq, "=======================\n");

	for (i = 0; i < priv->dma_tx_size; i++) {
		struct tmp_s *x = (struct tmp_s *)(priv->dma_tx + i);
		seq_printk(seq, "[%d] DES0=0x%x DES1=0x%x BUF1=0x%x BUF2=0x%x",
			   i, (unsigned int)(x->a),
			   (unsigned int)((x->a) >> 32), x->b, x->c);
		seq_printk(seq, "\n");
	}

	return 0;
}

static int gmac_sysfs_ring_open(struct inode *inode, struct file *file)
{
	return single_open(file, gmac_sysfs_ring_read, inode->i_private);
}

static const struct file_operations gmac_rings_status_fops = {
	.owner = THIS_MODULE,
	.open = gmac_sysfs_ring_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};


static int gmac_init_fs(struct net_device *dev)
{
	/* Create debugfs entries */
	gmac_fs_dir = debugfs_create_dir(GMAC_RESOURCE_NAME, NULL);

	if (!gmac_fs_dir || IS_ERR(gmac_fs_dir)) {
		pr_err("ERROR %s, debugfs create directory failed\n",
		       GMAC_RESOURCE_NAME);

		return -ENOMEM;
	}

	/* Entry to report DMA RX/TX rings */
	gmac_rings_status = debugfs_create_file("descriptors_status",
					   S_IRUGO, gmac_fs_dir, dev,
					   &gmac_rings_status_fops);

	if (!gmac_rings_status || IS_ERR(gmac_rings_status)) {
		pr_info("ERROR creating gmac ring debugfs file\n");
		debugfs_remove(gmac_fs_dir);

		return -ENOMEM;
	}

	return 0;
}

static void gmac_exit_fs(void)
{
	debugfs_remove(gmac_rings_status);
	debugfs_remove(gmac_fs_dir);
}
#endif /* CONFIG_GMAC_DEBUG_FS */

static const struct net_device_ops gmac_netdev_ops = {
	.ndo_open = gmac_open,
	.ndo_start_xmit = gmac_xmit,
	.ndo_stop = gmac_release,
	.ndo_change_mtu = gmac_change_mtu,
	.ndo_fix_features = gmac_fix_features,
	.ndo_set_rx_mode = gmac_set_rx_mode,
	.ndo_tx_timeout = gmac_tx_timeout,
	.ndo_do_ioctl = gmac_ioctl,
	.ndo_set_config = gmac_config,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = gmac_poll_controller,
#endif
	.ndo_set_mac_address = eth_mac_addr,
};

/**
 *  gmac_hw_init - Init the MAC device
 *  @priv : pointer to the private device structure.
 *  Description: this function detects which MAC device
 *  (GMAC/MAC10-100) has to attached, checks the HW capability
 *  (if supported) and sets the driver's features (for example
 *  to use the ring or chaine mode or support the normal/enh
 *  descriptor structure).
 */
static int gmac_hw_init(struct gmac_priv *priv)
{
	int ret = 0;

	priv->rx_coe = core_en_rx_coe(priv->ioaddr);

	return ret;
}

/**
 * gmac_dvr_probe
 * @device: device pointer
 * Description: this is the main probe function used to
 * call the alloc_etherdev, allocate the priv structure.
 */
struct gmac_priv *gmac_dvr_probe(struct device *device,
		void __iomem *addr, int irqnum)
{
	int ret = 0;
	struct net_device *ndev = NULL;
	struct gmac_priv *priv;

	ndev = alloc_etherdev(sizeof(struct gmac_priv));
	if (!ndev) {
		printk(KERN_ERR "ERROR: Allocating netdevice is failed!\n");
		return NULL;
	}

	SET_NETDEV_DEV(ndev, device);

	priv = netdev_priv(ndev);
	priv->device = device;
	priv->ndev = ndev;
	priv->plat = device->platform_data;

	ether_setup(ndev);

	gmac_set_ethtool_ops(ndev);
	priv->pause = pause;
	priv->ioaddr = addr;
	ndev->base_addr = (unsigned long)addr;

	/* Init MAC and get the capabilities */
	gmac_hw_init(priv);

	ndev->irq = irqnum;
	ndev->netdev_ops = &gmac_netdev_ops;

	ndev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM
						| NETIF_F_IPV6_CSUM | NETIF_F_RXCSUM;
	ndev->features |= ndev->hw_features | NETIF_F_HIGHDMA;
	ndev->watchdog_timeo = msecs_to_jiffies(watchdog);
#ifdef GMAC_VLAN_TAG_USED
	/* Gmac support receive VLAN tag detection */
	ndev->features |= NETIF_F_HW_VLAN_RX;
#endif
	priv->msg_enable = netif_msg_init(debug, default_msg_level);

	if (flow_ctrl)
		priv->flow_ctrl = FLOW_AUTO;	/* RX/TX pause on */

	netif_napi_add(ndev, &priv->napi, gmac_poll, 64);

	spin_lock_init(&priv->lock);
	spin_lock_init(&priv->tx_lock);

	gmac_check_ether_addr(priv);
	ret = register_netdev(ndev);
	if (ret) {
		printk(KERN_ERR "ERROR: %i registering the device\n", ret);
		goto error;
	}

	return priv;

error:
	netif_napi_del(&priv->napi);

	unregister_netdev(ndev);
	free_netdev(ndev);

	return NULL;
}

/**
 * gmac_dvr_remove
 * @ndev: net device pointer
 * Description: this function resets the TX/RX processes, disables the MAC RX/TX
 * changes the link status, releases the DMA descriptor rings.
 */
int gmac_dvr_remove(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);

	dma_stop_rx(priv->ioaddr);
	dma_stop_tx(priv->ioaddr);

	gmac_set_tx_rx(priv->ioaddr, false);
	netif_carrier_off(ndev);
	unregister_netdev(ndev);
	free_netdev(ndev);

	return 0;
}

#ifdef CONFIG_PM
int gmac_suspend(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	int dis_ic = 0;

	if (!ndev || !netif_running(ndev))
		return 0;

	if (ndev->phydev)
		phy_stop(ndev->phydev);

	spin_lock(&priv->lock);

	netif_device_detach(ndev);
	netif_stop_queue(ndev);

	napi_disable(&priv->napi);

	/* Stop TX/RX DMA */
	dma_stop_tx(priv->ioaddr);
	dma_stop_rx(priv->ioaddr);
	/* Clear the Rx/Tx descriptors */
	desc_init_rx(priv->dma_rx, priv->dma_rx_size,
				     dis_ic);
	desc_init_tx(priv->dma_tx, priv->dma_tx_size);

	gmac_set_tx_rx(priv->ioaddr, false);

	spin_unlock(&priv->lock);
	return 0;
}

int gmac_resume(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);

	if (!netif_running(ndev))
		return 0;

	spin_lock(&priv->lock);

	netif_device_attach(ndev);

	/* Enable the MAC and DMA */
	gmac_set_tx_rx(priv->ioaddr, true);
	dma_start_tx(priv->ioaddr);
	dma_start_rx(priv->ioaddr);

	napi_enable(&priv->napi);

	netif_start_queue(ndev);

	spin_unlock(&priv->lock);

	if (ndev->phydev)
		phy_start(ndev->phydev);

	return 0;
}

int gmac_freeze(struct net_device *ndev)
{
	if (!ndev || !netif_running(ndev))
		return 0;

	return gmac_release(ndev);
}

int gmac_restore(struct net_device *ndev)
{
	if (!ndev || !netif_running(ndev))
		return 0;

	return gmac_open(ndev);
}
#endif /* CONFIG_PM */

#ifndef MODULE
static int __init set_mac_addr(char *str)
{
	char *p = str;

	if (!p || !strlen(p))
		return 0;

	memcpy(mac_str, p, 18);

	return 0;
}
__setup("mac_addr=", set_mac_addr);
#endif

static int __init gmac_init(void)
{
#ifdef CONFIG_GMAC_SCRIPT_SYS
	if (SCRIPT_PARSER_OK != script_parser_fetch("gmac_para", "gmac_used", &gmac_used, 1))
		printk(KERN_WARNING "emac_init fetch emac using configuration failed\n");

	if (!gmac_used) {
		printk(KERN_INFO "gmac driver is disabled\n");
		return 0;
	}
#endif

	platform_device_register(&gmac_device);
	return platform_driver_register(&gmac_driver);
}

static void __exit gmac_remove(void)
{
	if (gmac_used != 1) {
		pr_info("gmac is disabled\n");
		return;
	}
	platform_driver_unregister(&gmac_driver);
	platform_device_unregister(&gmac_device);
}

module_init(gmac_init);
module_exit(gmac_remove);

MODULE_DESCRIPTION("SUN6I 10/100/1000Mbps Ethernet device driver");
MODULE_AUTHOR("Shuge <shugeLinux@gmail.com>");
MODULE_LICENSE("GPL v2");
