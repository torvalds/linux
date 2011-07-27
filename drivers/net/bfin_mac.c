/*
 * Blackfin On-Chip MAC Driver
 *
 * Copyright 2004-2010 Analog Devices Inc.
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 */

#define DRV_VERSION	"1.1"
#define DRV_DESC	"Blackfin on-chip Ethernet MAC driver"

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/crc32.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/mii.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/platform_device.h>

#include <asm/dma.h>
#include <linux/dma-mapping.h>

#include <asm/div64.h>
#include <asm/dpmc.h>
#include <asm/blackfin.h>
#include <asm/cacheflush.h>
#include <asm/portmux.h>
#include <mach/pll.h>

#include "bfin_mac.h"

MODULE_AUTHOR("Bryan Wu, Luke Yang");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRV_DESC);
MODULE_ALIAS("platform:bfin_mac");

#if defined(CONFIG_BFIN_MAC_USE_L1)
# define bfin_mac_alloc(dma_handle, size, num)  l1_data_sram_zalloc(size*num)
# define bfin_mac_free(dma_handle, ptr, num)    l1_data_sram_free(ptr)
#else
# define bfin_mac_alloc(dma_handle, size, num) \
	dma_alloc_coherent(NULL, size*num, dma_handle, GFP_KERNEL)
# define bfin_mac_free(dma_handle, ptr, num) \
	dma_free_coherent(NULL, sizeof(*ptr)*num, ptr, dma_handle)
#endif

#define PKT_BUF_SZ 1580

#define MAX_TIMEOUT_CNT	500

/* pointers to maintain transmit list */
static struct net_dma_desc_tx *tx_list_head;
static struct net_dma_desc_tx *tx_list_tail;
static struct net_dma_desc_rx *rx_list_head;
static struct net_dma_desc_rx *rx_list_tail;
static struct net_dma_desc_rx *current_rx_ptr;
static struct net_dma_desc_tx *current_tx_ptr;
static struct net_dma_desc_tx *tx_desc;
static struct net_dma_desc_rx *rx_desc;

static void desc_list_free(void)
{
	struct net_dma_desc_rx *r;
	struct net_dma_desc_tx *t;
	int i;
#if !defined(CONFIG_BFIN_MAC_USE_L1)
	dma_addr_t dma_handle = 0;
#endif

	if (tx_desc) {
		t = tx_list_head;
		for (i = 0; i < CONFIG_BFIN_TX_DESC_NUM; i++) {
			if (t) {
				if (t->skb) {
					dev_kfree_skb(t->skb);
					t->skb = NULL;
				}
				t = t->next;
			}
		}
		bfin_mac_free(dma_handle, tx_desc, CONFIG_BFIN_TX_DESC_NUM);
	}

	if (rx_desc) {
		r = rx_list_head;
		for (i = 0; i < CONFIG_BFIN_RX_DESC_NUM; i++) {
			if (r) {
				if (r->skb) {
					dev_kfree_skb(r->skb);
					r->skb = NULL;
				}
				r = r->next;
			}
		}
		bfin_mac_free(dma_handle, rx_desc, CONFIG_BFIN_RX_DESC_NUM);
	}
}

static int desc_list_init(void)
{
	int i;
	struct sk_buff *new_skb;
#if !defined(CONFIG_BFIN_MAC_USE_L1)
	/*
	 * This dma_handle is useless in Blackfin dma_alloc_coherent().
	 * The real dma handler is the return value of dma_alloc_coherent().
	 */
	dma_addr_t dma_handle;
#endif

	tx_desc = bfin_mac_alloc(&dma_handle,
				sizeof(struct net_dma_desc_tx),
				CONFIG_BFIN_TX_DESC_NUM);
	if (tx_desc == NULL)
		goto init_error;

	rx_desc = bfin_mac_alloc(&dma_handle,
				sizeof(struct net_dma_desc_rx),
				CONFIG_BFIN_RX_DESC_NUM);
	if (rx_desc == NULL)
		goto init_error;

	/* init tx_list */
	tx_list_head = tx_list_tail = tx_desc;

	for (i = 0; i < CONFIG_BFIN_TX_DESC_NUM; i++) {
		struct net_dma_desc_tx *t = tx_desc + i;
		struct dma_descriptor *a = &(t->desc_a);
		struct dma_descriptor *b = &(t->desc_b);

		/*
		 * disable DMA
		 * read from memory WNR = 0
		 * wordsize is 32 bits
		 * 6 half words is desc size
		 * large desc flow
		 */
		a->config = WDSIZE_32 | NDSIZE_6 | DMAFLOW_LARGE;
		a->start_addr = (unsigned long)t->packet;
		a->x_count = 0;
		a->next_dma_desc = b;

		/*
		 * enabled DMA
		 * write to memory WNR = 1
		 * wordsize is 32 bits
		 * disable interrupt
		 * 6 half words is desc size
		 * large desc flow
		 */
		b->config = DMAEN | WNR | WDSIZE_32 | NDSIZE_6 | DMAFLOW_LARGE;
		b->start_addr = (unsigned long)(&(t->status));
		b->x_count = 0;

		t->skb = NULL;
		tx_list_tail->desc_b.next_dma_desc = a;
		tx_list_tail->next = t;
		tx_list_tail = t;
	}
	tx_list_tail->next = tx_list_head;	/* tx_list is a circle */
	tx_list_tail->desc_b.next_dma_desc = &(tx_list_head->desc_a);
	current_tx_ptr = tx_list_head;

	/* init rx_list */
	rx_list_head = rx_list_tail = rx_desc;

	for (i = 0; i < CONFIG_BFIN_RX_DESC_NUM; i++) {
		struct net_dma_desc_rx *r = rx_desc + i;
		struct dma_descriptor *a = &(r->desc_a);
		struct dma_descriptor *b = &(r->desc_b);

		/* allocate a new skb for next time receive */
		new_skb = dev_alloc_skb(PKT_BUF_SZ + NET_IP_ALIGN);
		if (!new_skb) {
			pr_notice("init: low on mem - packet dropped\n");
			goto init_error;
		}
		skb_reserve(new_skb, NET_IP_ALIGN);
		/* Invidate the data cache of skb->data range when it is write back
		 * cache. It will prevent overwritting the new data from DMA
		 */
		blackfin_dcache_invalidate_range((unsigned long)new_skb->head,
					 (unsigned long)new_skb->end);
		r->skb = new_skb;

		/*
		 * enabled DMA
		 * write to memory WNR = 1
		 * wordsize is 32 bits
		 * disable interrupt
		 * 6 half words is desc size
		 * large desc flow
		 */
		a->config = DMAEN | WNR | WDSIZE_32 | NDSIZE_6 | DMAFLOW_LARGE;
		/* since RXDWA is enabled */
		a->start_addr = (unsigned long)new_skb->data - 2;
		a->x_count = 0;
		a->next_dma_desc = b;

		/*
		 * enabled DMA
		 * write to memory WNR = 1
		 * wordsize is 32 bits
		 * enable interrupt
		 * 6 half words is desc size
		 * large desc flow
		 */
		b->config = DMAEN | WNR | WDSIZE_32 | DI_EN |
				NDSIZE_6 | DMAFLOW_LARGE;
		b->start_addr = (unsigned long)(&(r->status));
		b->x_count = 0;

		rx_list_tail->desc_b.next_dma_desc = a;
		rx_list_tail->next = r;
		rx_list_tail = r;
	}
	rx_list_tail->next = rx_list_head;	/* rx_list is a circle */
	rx_list_tail->desc_b.next_dma_desc = &(rx_list_head->desc_a);
	current_rx_ptr = rx_list_head;

	return 0;

init_error:
	desc_list_free();
	pr_err("kmalloc failed\n");
	return -ENOMEM;
}


/*---PHY CONTROL AND CONFIGURATION-----------------------------------------*/

/*
 * MII operations
 */
/* Wait until the previous MDC/MDIO transaction has completed */
static int bfin_mdio_poll(void)
{
	int timeout_cnt = MAX_TIMEOUT_CNT;

	/* poll the STABUSY bit */
	while ((bfin_read_EMAC_STAADD()) & STABUSY) {
		udelay(1);
		if (timeout_cnt-- < 0) {
			pr_err("wait MDC/MDIO transaction to complete timeout\n");
			return -ETIMEDOUT;
		}
	}

	return 0;
}

/* Read an off-chip register in a PHY through the MDC/MDIO port */
static int bfin_mdiobus_read(struct mii_bus *bus, int phy_addr, int regnum)
{
	int ret;

	ret = bfin_mdio_poll();
	if (ret)
		return ret;

	/* read mode */
	bfin_write_EMAC_STAADD(SET_PHYAD((u16) phy_addr) |
				SET_REGAD((u16) regnum) |
				STABUSY);

	ret = bfin_mdio_poll();
	if (ret)
		return ret;

	return (int) bfin_read_EMAC_STADAT();
}

/* Write an off-chip register in a PHY through the MDC/MDIO port */
static int bfin_mdiobus_write(struct mii_bus *bus, int phy_addr, int regnum,
			      u16 value)
{
	int ret;

	ret = bfin_mdio_poll();
	if (ret)
		return ret;

	bfin_write_EMAC_STADAT((u32) value);

	/* write mode */
	bfin_write_EMAC_STAADD(SET_PHYAD((u16) phy_addr) |
				SET_REGAD((u16) regnum) |
				STAOP |
				STABUSY);

	return bfin_mdio_poll();
}

static int bfin_mdiobus_reset(struct mii_bus *bus)
{
	return 0;
}

static void bfin_mac_adjust_link(struct net_device *dev)
{
	struct bfin_mac_local *lp = netdev_priv(dev);
	struct phy_device *phydev = lp->phydev;
	unsigned long flags;
	int new_state = 0;

	spin_lock_irqsave(&lp->lock, flags);
	if (phydev->link) {
		/* Now we make sure that we can be in full duplex mode.
		 * If not, we operate in half-duplex mode. */
		if (phydev->duplex != lp->old_duplex) {
			u32 opmode = bfin_read_EMAC_OPMODE();
			new_state = 1;

			if (phydev->duplex)
				opmode |= FDMODE;
			else
				opmode &= ~(FDMODE);

			bfin_write_EMAC_OPMODE(opmode);
			lp->old_duplex = phydev->duplex;
		}

		if (phydev->speed != lp->old_speed) {
			if (phydev->interface == PHY_INTERFACE_MODE_RMII) {
				u32 opmode = bfin_read_EMAC_OPMODE();
				switch (phydev->speed) {
				case 10:
					opmode |= RMII_10;
					break;
				case 100:
					opmode &= ~RMII_10;
					break;
				default:
					netdev_warn(dev,
						"Ack! Speed (%d) is not 10/100!\n",
						phydev->speed);
					break;
				}
				bfin_write_EMAC_OPMODE(opmode);
			}

			new_state = 1;
			lp->old_speed = phydev->speed;
		}

		if (!lp->old_link) {
			new_state = 1;
			lp->old_link = 1;
		}
	} else if (lp->old_link) {
		new_state = 1;
		lp->old_link = 0;
		lp->old_speed = 0;
		lp->old_duplex = -1;
	}

	if (new_state) {
		u32 opmode = bfin_read_EMAC_OPMODE();
		phy_print_status(phydev);
		pr_debug("EMAC_OPMODE = 0x%08x\n", opmode);
	}

	spin_unlock_irqrestore(&lp->lock, flags);
}

/* MDC  = 2.5 MHz */
#define MDC_CLK 2500000

static int mii_probe(struct net_device *dev, int phy_mode)
{
	struct bfin_mac_local *lp = netdev_priv(dev);
	struct phy_device *phydev = NULL;
	unsigned short sysctl;
	int i;
	u32 sclk, mdc_div;

	/* Enable PHY output early */
	if (!(bfin_read_VR_CTL() & CLKBUFOE))
		bfin_write_VR_CTL(bfin_read_VR_CTL() | CLKBUFOE);

	sclk = get_sclk();
	mdc_div = ((sclk / MDC_CLK) / 2) - 1;

	sysctl = bfin_read_EMAC_SYSCTL();
	sysctl = (sysctl & ~MDCDIV) | SET_MDCDIV(mdc_div);
	bfin_write_EMAC_SYSCTL(sysctl);

	/* search for connected PHY device */
	for (i = 0; i < PHY_MAX_ADDR; ++i) {
		struct phy_device *const tmp_phydev = lp->mii_bus->phy_map[i];

		if (!tmp_phydev)
			continue; /* no PHY here... */

		phydev = tmp_phydev;
		break; /* found it */
	}

	/* now we are supposed to have a proper phydev, to attach to... */
	if (!phydev) {
		netdev_err(dev, "no phy device found\n");
		return -ENODEV;
	}

	if (phy_mode != PHY_INTERFACE_MODE_RMII &&
		phy_mode != PHY_INTERFACE_MODE_MII) {
		netdev_err(dev, "invalid phy interface mode\n");
		return -EINVAL;
	}

	phydev = phy_connect(dev, dev_name(&phydev->dev), &bfin_mac_adjust_link,
			0, phy_mode);

	if (IS_ERR(phydev)) {
		netdev_err(dev, "could not attach PHY\n");
		return PTR_ERR(phydev);
	}

	/* mask with MAC supported features */
	phydev->supported &= (SUPPORTED_10baseT_Half
			      | SUPPORTED_10baseT_Full
			      | SUPPORTED_100baseT_Half
			      | SUPPORTED_100baseT_Full
			      | SUPPORTED_Autoneg
			      | SUPPORTED_Pause | SUPPORTED_Asym_Pause
			      | SUPPORTED_MII
			      | SUPPORTED_TP);

	phydev->advertising = phydev->supported;

	lp->old_link = 0;
	lp->old_speed = 0;
	lp->old_duplex = -1;
	lp->phydev = phydev;

	pr_info("attached PHY driver [%s] "
	        "(mii_bus:phy_addr=%s, irq=%d, mdc_clk=%dHz(mdc_div=%d)@sclk=%dMHz)\n",
	        phydev->drv->name, dev_name(&phydev->dev), phydev->irq,
	        MDC_CLK, mdc_div, sclk/1000000);

	return 0;
}

/*
 * Ethtool support
 */

/*
 * interrupt routine for magic packet wakeup
 */
static irqreturn_t bfin_mac_wake_interrupt(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}

static int
bfin_mac_ethtool_getsettings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct bfin_mac_local *lp = netdev_priv(dev);

	if (lp->phydev)
		return phy_ethtool_gset(lp->phydev, cmd);

	return -EINVAL;
}

static int
bfin_mac_ethtool_setsettings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct bfin_mac_local *lp = netdev_priv(dev);

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (lp->phydev)
		return phy_ethtool_sset(lp->phydev, cmd);

	return -EINVAL;
}

static void bfin_mac_ethtool_getdrvinfo(struct net_device *dev,
					struct ethtool_drvinfo *info)
{
	strcpy(info->driver, KBUILD_MODNAME);
	strcpy(info->version, DRV_VERSION);
	strcpy(info->fw_version, "N/A");
	strcpy(info->bus_info, dev_name(&dev->dev));
}

static void bfin_mac_ethtool_getwol(struct net_device *dev,
	struct ethtool_wolinfo *wolinfo)
{
	struct bfin_mac_local *lp = netdev_priv(dev);

	wolinfo->supported = WAKE_MAGIC;
	wolinfo->wolopts = lp->wol;
}

static int bfin_mac_ethtool_setwol(struct net_device *dev,
	struct ethtool_wolinfo *wolinfo)
{
	struct bfin_mac_local *lp = netdev_priv(dev);
	int rc;

	if (wolinfo->wolopts & (WAKE_MAGICSECURE |
				WAKE_UCAST |
				WAKE_MCAST |
				WAKE_BCAST |
				WAKE_ARP))
		return -EOPNOTSUPP;

	lp->wol = wolinfo->wolopts;

	if (lp->wol && !lp->irq_wake_requested) {
		/* register wake irq handler */
		rc = request_irq(IRQ_MAC_WAKEDET, bfin_mac_wake_interrupt,
				 IRQF_DISABLED, "EMAC_WAKE", dev);
		if (rc)
			return rc;
		lp->irq_wake_requested = true;
	}

	if (!lp->wol && lp->irq_wake_requested) {
		free_irq(IRQ_MAC_WAKEDET, dev);
		lp->irq_wake_requested = false;
	}

	/* Make sure the PHY driver doesn't suspend */
	device_init_wakeup(&dev->dev, lp->wol);

	return 0;
}

static const struct ethtool_ops bfin_mac_ethtool_ops = {
	.get_settings = bfin_mac_ethtool_getsettings,
	.set_settings = bfin_mac_ethtool_setsettings,
	.get_link = ethtool_op_get_link,
	.get_drvinfo = bfin_mac_ethtool_getdrvinfo,
	.get_wol = bfin_mac_ethtool_getwol,
	.set_wol = bfin_mac_ethtool_setwol,
};

/**************************************************************************/
static void setup_system_regs(struct net_device *dev)
{
	struct bfin_mac_local *lp = netdev_priv(dev);
	int i;
	unsigned short sysctl;

	/*
	 * Odd word alignment for Receive Frame DMA word
	 * Configure checksum support and rcve frame word alignment
	 */
	sysctl = bfin_read_EMAC_SYSCTL();
	/*
	 * check if interrupt is requested for any PHY,
	 * enable PHY interrupt only if needed
	 */
	for (i = 0; i < PHY_MAX_ADDR; ++i)
		if (lp->mii_bus->irq[i] != PHY_POLL)
			break;
	if (i < PHY_MAX_ADDR)
		sysctl |= PHYIE;
	sysctl |= RXDWA;
#if defined(BFIN_MAC_CSUM_OFFLOAD)
	sysctl |= RXCKS;
#else
	sysctl &= ~RXCKS;
#endif
	bfin_write_EMAC_SYSCTL(sysctl);

	bfin_write_EMAC_MMC_CTL(RSTC | CROLL);

	/* Set vlan regs to let 1522 bytes long packets pass through */
	bfin_write_EMAC_VLAN1(lp->vlan1_mask);
	bfin_write_EMAC_VLAN2(lp->vlan2_mask);

	/* Initialize the TX DMA channel registers */
	bfin_write_DMA2_X_COUNT(0);
	bfin_write_DMA2_X_MODIFY(4);
	bfin_write_DMA2_Y_COUNT(0);
	bfin_write_DMA2_Y_MODIFY(0);

	/* Initialize the RX DMA channel registers */
	bfin_write_DMA1_X_COUNT(0);
	bfin_write_DMA1_X_MODIFY(4);
	bfin_write_DMA1_Y_COUNT(0);
	bfin_write_DMA1_Y_MODIFY(0);
}

static void setup_mac_addr(u8 *mac_addr)
{
	u32 addr_low = le32_to_cpu(*(__le32 *) & mac_addr[0]);
	u16 addr_hi = le16_to_cpu(*(__le16 *) & mac_addr[4]);

	/* this depends on a little-endian machine */
	bfin_write_EMAC_ADDRLO(addr_low);
	bfin_write_EMAC_ADDRHI(addr_hi);
}

static int bfin_mac_set_mac_address(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;
	if (netif_running(dev))
		return -EBUSY;
	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	setup_mac_addr(dev->dev_addr);
	return 0;
}

#ifdef CONFIG_BFIN_MAC_USE_HWSTAMP
#define bfin_mac_hwtstamp_is_none(cfg) ((cfg) == HWTSTAMP_FILTER_NONE)

static int bfin_mac_hwtstamp_ioctl(struct net_device *netdev,
		struct ifreq *ifr, int cmd)
{
	struct hwtstamp_config config;
	struct bfin_mac_local *lp = netdev_priv(netdev);
	u16 ptpctl;
	u32 ptpfv1, ptpfv2, ptpfv3, ptpfoff;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	pr_debug("%s config flag:0x%x, tx_type:0x%x, rx_filter:0x%x\n",
			__func__, config.flags, config.tx_type, config.rx_filter);

	/* reserved for future extensions */
	if (config.flags)
		return -EINVAL;

	if ((config.tx_type != HWTSTAMP_TX_OFF) &&
			(config.tx_type != HWTSTAMP_TX_ON))
		return -ERANGE;

	ptpctl = bfin_read_EMAC_PTP_CTL();

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		/*
		 * Dont allow any timestamping
		 */
		ptpfv3 = 0xFFFFFFFF;
		bfin_write_EMAC_PTP_FV3(ptpfv3);
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		/*
		 * Clear the five comparison mask bits (bits[12:8]) in EMAC_PTP_CTL)
		 * to enable all the field matches.
		 */
		ptpctl &= ~0x1F00;
		bfin_write_EMAC_PTP_CTL(ptpctl);
		/*
		 * Keep the default values of the EMAC_PTP_FOFF register.
		 */
		ptpfoff = 0x4A24170C;
		bfin_write_EMAC_PTP_FOFF(ptpfoff);
		/*
		 * Keep the default values of the EMAC_PTP_FV1 and EMAC_PTP_FV2
		 * registers.
		 */
		ptpfv1 = 0x11040800;
		bfin_write_EMAC_PTP_FV1(ptpfv1);
		ptpfv2 = 0x0140013F;
		bfin_write_EMAC_PTP_FV2(ptpfv2);
		/*
		 * The default value (0xFFFC) allows the timestamping of both
		 * received Sync messages and Delay_Req messages.
		 */
		ptpfv3 = 0xFFFFFFFC;
		bfin_write_EMAC_PTP_FV3(ptpfv3);

		config.rx_filter = HWTSTAMP_FILTER_PTP_V1_L4_EVENT;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		/* Clear all five comparison mask bits (bits[12:8]) in the
		 * EMAC_PTP_CTL register to enable all the field matches.
		 */
		ptpctl &= ~0x1F00;
		bfin_write_EMAC_PTP_CTL(ptpctl);
		/*
		 * Keep the default values of the EMAC_PTP_FOFF register, except set
		 * the PTPCOF field to 0x2A.
		 */
		ptpfoff = 0x2A24170C;
		bfin_write_EMAC_PTP_FOFF(ptpfoff);
		/*
		 * Keep the default values of the EMAC_PTP_FV1 and EMAC_PTP_FV2
		 * registers.
		 */
		ptpfv1 = 0x11040800;
		bfin_write_EMAC_PTP_FV1(ptpfv1);
		ptpfv2 = 0x0140013F;
		bfin_write_EMAC_PTP_FV2(ptpfv2);
		/*
		 * To allow the timestamping of Pdelay_Req and Pdelay_Resp, set
		 * the value to 0xFFF0.
		 */
		ptpfv3 = 0xFFFFFFF0;
		bfin_write_EMAC_PTP_FV3(ptpfv3);

		config.rx_filter = HWTSTAMP_FILTER_PTP_V2_L4_EVENT;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		/*
		 * Clear bits 8 and 12 of the EMAC_PTP_CTL register to enable only the
		 * EFTM and PTPCM field comparison.
		 */
		ptpctl &= ~0x1100;
		bfin_write_EMAC_PTP_CTL(ptpctl);
		/*
		 * Keep the default values of all the fields of the EMAC_PTP_FOFF
		 * register, except set the PTPCOF field to 0x0E.
		 */
		ptpfoff = 0x0E24170C;
		bfin_write_EMAC_PTP_FOFF(ptpfoff);
		/*
		 * Program bits [15:0] of the EMAC_PTP_FV1 register to 0x88F7, which
		 * corresponds to PTP messages on the MAC layer.
		 */
		ptpfv1 = 0x110488F7;
		bfin_write_EMAC_PTP_FV1(ptpfv1);
		ptpfv2 = 0x0140013F;
		bfin_write_EMAC_PTP_FV2(ptpfv2);
		/*
		 * To allow the timestamping of Pdelay_Req and Pdelay_Resp
		 * messages, set the value to 0xFFF0.
		 */
		ptpfv3 = 0xFFFFFFF0;
		bfin_write_EMAC_PTP_FV3(ptpfv3);

		config.rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
		break;
	default:
		return -ERANGE;
	}

	if (config.tx_type == HWTSTAMP_TX_OFF &&
	    bfin_mac_hwtstamp_is_none(config.rx_filter)) {
		ptpctl &= ~PTP_EN;
		bfin_write_EMAC_PTP_CTL(ptpctl);

		SSYNC();
	} else {
		ptpctl |= PTP_EN;
		bfin_write_EMAC_PTP_CTL(ptpctl);

		/*
		 * clear any existing timestamp
		 */
		bfin_read_EMAC_PTP_RXSNAPLO();
		bfin_read_EMAC_PTP_RXSNAPHI();

		bfin_read_EMAC_PTP_TXSNAPLO();
		bfin_read_EMAC_PTP_TXSNAPHI();

		/*
		 * Set registers so that rollover occurs soon to test this.
		 */
		bfin_write_EMAC_PTP_TIMELO(0x00000000);
		bfin_write_EMAC_PTP_TIMEHI(0xFF800000);

		SSYNC();

		lp->compare.last_update = 0;
		timecounter_init(&lp->clock,
				&lp->cycles,
				ktime_to_ns(ktime_get_real()));
		timecompare_update(&lp->compare, 0);
	}

	lp->stamp_cfg = config;
	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}

static void bfin_dump_hwtamp(char *s, ktime_t *hw, ktime_t *ts, struct timecompare *cmp)
{
	ktime_t sys = ktime_get_real();

	pr_debug("%s %s hardware:%d,%d transform system:%d,%d system:%d,%d, cmp:%lld, %lld\n",
			__func__, s, hw->tv.sec, hw->tv.nsec, ts->tv.sec, ts->tv.nsec, sys.tv.sec,
			sys.tv.nsec, cmp->offset, cmp->skew);
}

static void bfin_tx_hwtstamp(struct net_device *netdev, struct sk_buff *skb)
{
	struct bfin_mac_local *lp = netdev_priv(netdev);

	if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
		int timeout_cnt = MAX_TIMEOUT_CNT;

		/* When doing time stamping, keep the connection to the socket
		 * a while longer
		 */
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

		/*
		 * The timestamping is done at the EMAC module's MII/RMII interface
		 * when the module sees the Start of Frame of an event message packet. This
		 * interface is the closest possible place to the physical Ethernet transmission
		 * medium, providing the best timing accuracy.
		 */
		while ((!(bfin_read_EMAC_PTP_ISTAT() & TXTL)) && (--timeout_cnt))
			udelay(1);
		if (timeout_cnt == 0)
			netdev_err(netdev, "timestamp the TX packet failed\n");
		else {
			struct skb_shared_hwtstamps shhwtstamps;
			u64 ns;
			u64 regval;

			regval = bfin_read_EMAC_PTP_TXSNAPLO();
			regval |= (u64)bfin_read_EMAC_PTP_TXSNAPHI() << 32;
			memset(&shhwtstamps, 0, sizeof(shhwtstamps));
			ns = timecounter_cyc2time(&lp->clock,
					regval);
			timecompare_update(&lp->compare, ns);
			shhwtstamps.hwtstamp = ns_to_ktime(ns);
			shhwtstamps.syststamp =
				timecompare_transform(&lp->compare, ns);
			skb_tstamp_tx(skb, &shhwtstamps);

			bfin_dump_hwtamp("TX", &shhwtstamps.hwtstamp, &shhwtstamps.syststamp, &lp->compare);
		}
	}
}

static void bfin_rx_hwtstamp(struct net_device *netdev, struct sk_buff *skb)
{
	struct bfin_mac_local *lp = netdev_priv(netdev);
	u32 valid;
	u64 regval, ns;
	struct skb_shared_hwtstamps *shhwtstamps;

	if (bfin_mac_hwtstamp_is_none(lp->stamp_cfg.rx_filter))
		return;

	valid = bfin_read_EMAC_PTP_ISTAT() & RXEL;
	if (!valid)
		return;

	shhwtstamps = skb_hwtstamps(skb);

	regval = bfin_read_EMAC_PTP_RXSNAPLO();
	regval |= (u64)bfin_read_EMAC_PTP_RXSNAPHI() << 32;
	ns = timecounter_cyc2time(&lp->clock, regval);
	timecompare_update(&lp->compare, ns);
	memset(shhwtstamps, 0, sizeof(*shhwtstamps));
	shhwtstamps->hwtstamp = ns_to_ktime(ns);
	shhwtstamps->syststamp = timecompare_transform(&lp->compare, ns);

	bfin_dump_hwtamp("RX", &shhwtstamps->hwtstamp, &shhwtstamps->syststamp, &lp->compare);
}

/*
 * bfin_read_clock - read raw cycle counter (to be used by time counter)
 */
static cycle_t bfin_read_clock(const struct cyclecounter *tc)
{
	u64 stamp;

	stamp =  bfin_read_EMAC_PTP_TIMELO();
	stamp |= (u64)bfin_read_EMAC_PTP_TIMEHI() << 32ULL;

	return stamp;
}

#define PTP_CLK 25000000

static void bfin_mac_hwtstamp_init(struct net_device *netdev)
{
	struct bfin_mac_local *lp = netdev_priv(netdev);
	u64 append;

	/* Initialize hardware timer */
	append = PTP_CLK * (1ULL << 32);
	do_div(append, get_sclk());
	bfin_write_EMAC_PTP_ADDEND((u32)append);

	memset(&lp->cycles, 0, sizeof(lp->cycles));
	lp->cycles.read = bfin_read_clock;
	lp->cycles.mask = CLOCKSOURCE_MASK(64);
	lp->cycles.mult = 1000000000 / PTP_CLK;
	lp->cycles.shift = 0;

	/* Synchronize our NIC clock against system wall clock */
	memset(&lp->compare, 0, sizeof(lp->compare));
	lp->compare.source = &lp->clock;
	lp->compare.target = ktime_get_real;
	lp->compare.num_samples = 10;

	/* Initialize hwstamp config */
	lp->stamp_cfg.rx_filter = HWTSTAMP_FILTER_NONE;
	lp->stamp_cfg.tx_type = HWTSTAMP_TX_OFF;
}

#else
# define bfin_mac_hwtstamp_is_none(cfg) 0
# define bfin_mac_hwtstamp_init(dev)
# define bfin_mac_hwtstamp_ioctl(dev, ifr, cmd) (-EOPNOTSUPP)
# define bfin_rx_hwtstamp(dev, skb)
# define bfin_tx_hwtstamp(dev, skb)
#endif

static inline void _tx_reclaim_skb(void)
{
	do {
		tx_list_head->desc_a.config &= ~DMAEN;
		tx_list_head->status.status_word = 0;
		if (tx_list_head->skb) {
			dev_kfree_skb(tx_list_head->skb);
			tx_list_head->skb = NULL;
		}
		tx_list_head = tx_list_head->next;

	} while (tx_list_head->status.status_word != 0);
}

static void tx_reclaim_skb(struct bfin_mac_local *lp)
{
	int timeout_cnt = MAX_TIMEOUT_CNT;

	if (tx_list_head->status.status_word != 0)
		_tx_reclaim_skb();

	if (current_tx_ptr->next == tx_list_head) {
		while (tx_list_head->status.status_word == 0) {
			/* slow down polling to avoid too many queue stop. */
			udelay(10);
			/* reclaim skb if DMA is not running. */
			if (!(bfin_read_DMA2_IRQ_STATUS() & DMA_RUN))
				break;
			if (timeout_cnt-- < 0)
				break;
		}

		if (timeout_cnt >= 0)
			_tx_reclaim_skb();
		else
			netif_stop_queue(lp->ndev);
	}

	if (current_tx_ptr->next != tx_list_head &&
		netif_queue_stopped(lp->ndev))
		netif_wake_queue(lp->ndev);

	if (tx_list_head != current_tx_ptr) {
		/* shorten the timer interval if tx queue is stopped */
		if (netif_queue_stopped(lp->ndev))
			lp->tx_reclaim_timer.expires =
				jiffies + (TX_RECLAIM_JIFFIES >> 4);
		else
			lp->tx_reclaim_timer.expires =
				jiffies + TX_RECLAIM_JIFFIES;

		mod_timer(&lp->tx_reclaim_timer,
			lp->tx_reclaim_timer.expires);
	}

	return;
}

static void tx_reclaim_skb_timeout(unsigned long lp)
{
	tx_reclaim_skb((struct bfin_mac_local *)lp);
}

static int bfin_mac_hard_start_xmit(struct sk_buff *skb,
				struct net_device *dev)
{
	struct bfin_mac_local *lp = netdev_priv(dev);
	u16 *data;
	u32 data_align = (unsigned long)(skb->data) & 0x3;

	current_tx_ptr->skb = skb;

	if (data_align == 0x2) {
		/* move skb->data to current_tx_ptr payload */
		data = (u16 *)(skb->data) - 1;
		*data = (u16)(skb->len);
		/*
		 * When transmitting an Ethernet packet, the PTP_TSYNC module requires
		 * a DMA_Length_Word field associated with the packet. The lower 12 bits
		 * of this field are the length of the packet payload in bytes and the higher
		 * 4 bits are the timestamping enable field.
		 */
		if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)
			*data |= 0x1000;

		current_tx_ptr->desc_a.start_addr = (u32)data;
		/* this is important! */
		blackfin_dcache_flush_range((u32)data,
				(u32)((u8 *)data + skb->len + 4));
	} else {
		*((u16 *)(current_tx_ptr->packet)) = (u16)(skb->len);
		/* enable timestamping for the sent packet */
		if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)
			*((u16 *)(current_tx_ptr->packet)) |= 0x1000;
		memcpy((u8 *)(current_tx_ptr->packet + 2), skb->data,
			skb->len);
		current_tx_ptr->desc_a.start_addr =
			(u32)current_tx_ptr->packet;
		blackfin_dcache_flush_range(
			(u32)current_tx_ptr->packet,
			(u32)(current_tx_ptr->packet + skb->len + 2));
	}

	/* make sure the internal data buffers in the core are drained
	 * so that the DMA descriptors are completely written when the
	 * DMA engine goes to fetch them below
	 */
	SSYNC();

	/* always clear status buffer before start tx dma */
	current_tx_ptr->status.status_word = 0;

	/* enable this packet's dma */
	current_tx_ptr->desc_a.config |= DMAEN;

	/* tx dma is running, just return */
	if (bfin_read_DMA2_IRQ_STATUS() & DMA_RUN)
		goto out;

	/* tx dma is not running */
	bfin_write_DMA2_NEXT_DESC_PTR(&(current_tx_ptr->desc_a));
	/* dma enabled, read from memory, size is 6 */
	bfin_write_DMA2_CONFIG(current_tx_ptr->desc_a.config);
	/* Turn on the EMAC tx */
	bfin_write_EMAC_OPMODE(bfin_read_EMAC_OPMODE() | TE);

out:
	bfin_tx_hwtstamp(dev, skb);

	current_tx_ptr = current_tx_ptr->next;
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += (skb->len);

	tx_reclaim_skb(lp);

	return NETDEV_TX_OK;
}

#define IP_HEADER_OFF  0
#define RX_ERROR_MASK (RX_LONG | RX_ALIGN | RX_CRC | RX_LEN | \
	RX_FRAG | RX_ADDR | RX_DMAO | RX_PHY | RX_LATE | RX_RANGE)

static void bfin_mac_rx(struct net_device *dev)
{
	struct sk_buff *skb, *new_skb;
	unsigned short len;
	struct bfin_mac_local *lp __maybe_unused = netdev_priv(dev);
#if defined(BFIN_MAC_CSUM_OFFLOAD)
	unsigned int i;
	unsigned char fcs[ETH_FCS_LEN + 1];
#endif

	/* check if frame status word reports an error condition
	 * we which case we simply drop the packet
	 */
	if (current_rx_ptr->status.status_word & RX_ERROR_MASK) {
		netdev_notice(dev, "rx: receive error - packet dropped\n");
		dev->stats.rx_dropped++;
		goto out;
	}

	/* allocate a new skb for next time receive */
	skb = current_rx_ptr->skb;

	new_skb = dev_alloc_skb(PKT_BUF_SZ + NET_IP_ALIGN);
	if (!new_skb) {
		netdev_notice(dev, "rx: low on mem - packet dropped\n");
		dev->stats.rx_dropped++;
		goto out;
	}
	/* reserve 2 bytes for RXDWA padding */
	skb_reserve(new_skb, NET_IP_ALIGN);
	/* Invidate the data cache of skb->data range when it is write back
	 * cache. It will prevent overwritting the new data from DMA
	 */
	blackfin_dcache_invalidate_range((unsigned long)new_skb->head,
					 (unsigned long)new_skb->end);

	current_rx_ptr->skb = new_skb;
	current_rx_ptr->desc_a.start_addr = (unsigned long)new_skb->data - 2;

	len = (unsigned short)((current_rx_ptr->status.status_word) & RX_FRLEN);
	/* Deduce Ethernet FCS length from Ethernet payload length */
	len -= ETH_FCS_LEN;
	skb_put(skb, len);

	skb->protocol = eth_type_trans(skb, dev);

	bfin_rx_hwtstamp(dev, skb);

#if defined(BFIN_MAC_CSUM_OFFLOAD)
	/* Checksum offloading only works for IPv4 packets with the standard IP header
	 * length of 20 bytes, because the blackfin MAC checksum calculation is
	 * based on that assumption. We must NOT use the calculated checksum if our
	 * IP version or header break that assumption.
	 */
	if (skb->data[IP_HEADER_OFF] == 0x45) {
		skb->csum = current_rx_ptr->status.ip_payload_csum;
		/*
		 * Deduce Ethernet FCS from hardware generated IP payload checksum.
		 * IP checksum is based on 16-bit one's complement algorithm.
		 * To deduce a value from checksum is equal to add its inversion.
		 * If the IP payload len is odd, the inversed FCS should also
		 * begin from odd address and leave first byte zero.
		 */
		if (skb->len % 2) {
			fcs[0] = 0;
			for (i = 0; i < ETH_FCS_LEN; i++)
				fcs[i + 1] = ~skb->data[skb->len + i];
			skb->csum = csum_partial(fcs, ETH_FCS_LEN + 1, skb->csum);
		} else {
			for (i = 0; i < ETH_FCS_LEN; i++)
				fcs[i] = ~skb->data[skb->len + i];
			skb->csum = csum_partial(fcs, ETH_FCS_LEN, skb->csum);
		}
		skb->ip_summed = CHECKSUM_COMPLETE;
	}
#endif

	netif_rx(skb);
	dev->stats.rx_packets++;
	dev->stats.rx_bytes += len;
out:
	current_rx_ptr->status.status_word = 0x00000000;
	current_rx_ptr = current_rx_ptr->next;
}

/* interrupt routine to handle rx and error signal */
static irqreturn_t bfin_mac_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	int number = 0;

get_one_packet:
	if (current_rx_ptr->status.status_word == 0) {
		/* no more new packet received */
		if (number == 0) {
			if (current_rx_ptr->next->status.status_word != 0) {
				current_rx_ptr = current_rx_ptr->next;
				goto real_rx;
			}
		}
		bfin_write_DMA1_IRQ_STATUS(bfin_read_DMA1_IRQ_STATUS() |
					   DMA_DONE | DMA_ERR);
		return IRQ_HANDLED;
	}

real_rx:
	bfin_mac_rx(dev);
	number++;
	goto get_one_packet;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void bfin_mac_poll(struct net_device *dev)
{
	struct bfin_mac_local *lp = netdev_priv(dev);

	disable_irq(IRQ_MAC_RX);
	bfin_mac_interrupt(IRQ_MAC_RX, dev);
	tx_reclaim_skb(lp);
	enable_irq(IRQ_MAC_RX);
}
#endif				/* CONFIG_NET_POLL_CONTROLLER */

static void bfin_mac_disable(void)
{
	unsigned int opmode;

	opmode = bfin_read_EMAC_OPMODE();
	opmode &= (~RE);
	opmode &= (~TE);
	/* Turn off the EMAC */
	bfin_write_EMAC_OPMODE(opmode);
}

/*
 * Enable Interrupts, Receive, and Transmit
 */
static int bfin_mac_enable(struct phy_device *phydev)
{
	int ret;
	u32 opmode;

	pr_debug("%s\n", __func__);

	/* Set RX DMA */
	bfin_write_DMA1_NEXT_DESC_PTR(&(rx_list_head->desc_a));
	bfin_write_DMA1_CONFIG(rx_list_head->desc_a.config);

	/* Wait MII done */
	ret = bfin_mdio_poll();
	if (ret)
		return ret;

	/* We enable only RX here */
	/* ASTP   : Enable Automatic Pad Stripping
	   PR     : Promiscuous Mode for test
	   PSF    : Receive frames with total length less than 64 bytes.
	   FDMODE : Full Duplex Mode
	   LB     : Internal Loopback for test
	   RE     : Receiver Enable */
	opmode = bfin_read_EMAC_OPMODE();
	if (opmode & FDMODE)
		opmode |= PSF;
	else
		opmode |= DRO | DC | PSF;
	opmode |= RE;

	if (phydev->interface == PHY_INTERFACE_MODE_RMII) {
		opmode |= RMII; /* For Now only 100MBit are supported */
#if defined(CONFIG_BF537) || defined(CONFIG_BF536)
		if (__SILICON_REVISION__ < 3) {
			/*
			 * This isn't publicly documented (fun times!), but in
			 * silicon <=0.2, the RX and TX pins are clocked together.
			 * So in order to recv, we must enable the transmit side
			 * as well.  This will cause a spurious TX interrupt too,
			 * but we can easily consume that.
			 */
			opmode |= TE;
		}
#endif
	}

	/* Turn on the EMAC rx */
	bfin_write_EMAC_OPMODE(opmode);

	return 0;
}

/* Our watchdog timed out. Called by the networking layer */
static void bfin_mac_timeout(struct net_device *dev)
{
	struct bfin_mac_local *lp = netdev_priv(dev);

	pr_debug("%s: %s\n", dev->name, __func__);

	bfin_mac_disable();

	del_timer(&lp->tx_reclaim_timer);

	/* reset tx queue and free skb */
	while (tx_list_head != current_tx_ptr) {
		tx_list_head->desc_a.config &= ~DMAEN;
		tx_list_head->status.status_word = 0;
		if (tx_list_head->skb) {
			dev_kfree_skb(tx_list_head->skb);
			tx_list_head->skb = NULL;
		}
		tx_list_head = tx_list_head->next;
	}

	if (netif_queue_stopped(lp->ndev))
		netif_wake_queue(lp->ndev);

	bfin_mac_enable(lp->phydev);

	/* We can accept TX packets again */
	dev->trans_start = jiffies; /* prevent tx timeout */
	netif_wake_queue(dev);
}

static void bfin_mac_multicast_hash(struct net_device *dev)
{
	u32 emac_hashhi, emac_hashlo;
	struct netdev_hw_addr *ha;
	u32 crc;

	emac_hashhi = emac_hashlo = 0;

	netdev_for_each_mc_addr(ha, dev) {
		crc = ether_crc(ETH_ALEN, ha->addr);
		crc >>= 26;

		if (crc & 0x20)
			emac_hashhi |= 1 << (crc & 0x1f);
		else
			emac_hashlo |= 1 << (crc & 0x1f);
	}

	bfin_write_EMAC_HASHHI(emac_hashhi);
	bfin_write_EMAC_HASHLO(emac_hashlo);
}

/*
 * This routine will, depending on the values passed to it,
 * either make it accept multicast packets, go into
 * promiscuous mode (for TCPDUMP and cousins) or accept
 * a select set of multicast packets
 */
static void bfin_mac_set_multicast_list(struct net_device *dev)
{
	u32 sysctl;

	if (dev->flags & IFF_PROMISC) {
		netdev_info(dev, "set promisc mode\n");
		sysctl = bfin_read_EMAC_OPMODE();
		sysctl |= PR;
		bfin_write_EMAC_OPMODE(sysctl);
	} else if (dev->flags & IFF_ALLMULTI) {
		/* accept all multicast */
		sysctl = bfin_read_EMAC_OPMODE();
		sysctl |= PAM;
		bfin_write_EMAC_OPMODE(sysctl);
	} else if (!netdev_mc_empty(dev)) {
		/* set up multicast hash table */
		sysctl = bfin_read_EMAC_OPMODE();
		sysctl |= HM;
		bfin_write_EMAC_OPMODE(sysctl);
		bfin_mac_multicast_hash(dev);
	} else {
		/* clear promisc or multicast mode */
		sysctl = bfin_read_EMAC_OPMODE();
		sysctl &= ~(RAF | PAM);
		bfin_write_EMAC_OPMODE(sysctl);
	}
}

static int bfin_mac_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct bfin_mac_local *lp = netdev_priv(netdev);

	if (!netif_running(netdev))
		return -EINVAL;

	switch (cmd) {
	case SIOCSHWTSTAMP:
		return bfin_mac_hwtstamp_ioctl(netdev, ifr, cmd);
	default:
		if (lp->phydev)
			return phy_mii_ioctl(lp->phydev, ifr, cmd);
		else
			return -EOPNOTSUPP;
	}
}

/*
 * this puts the device in an inactive state
 */
static void bfin_mac_shutdown(struct net_device *dev)
{
	/* Turn off the EMAC */
	bfin_write_EMAC_OPMODE(0x00000000);
	/* Turn off the EMAC RX DMA */
	bfin_write_DMA1_CONFIG(0x0000);
	bfin_write_DMA2_CONFIG(0x0000);
}

/*
 * Open and Initialize the interface
 *
 * Set up everything, reset the card, etc..
 */
static int bfin_mac_open(struct net_device *dev)
{
	struct bfin_mac_local *lp = netdev_priv(dev);
	int ret;
	pr_debug("%s: %s\n", dev->name, __func__);

	/*
	 * Check that the address is valid.  If its not, refuse
	 * to bring the device up.  The user must specify an
	 * address using ifconfig eth0 hw ether xx:xx:xx:xx:xx:xx
	 */
	if (!is_valid_ether_addr(dev->dev_addr)) {
		netdev_warn(dev, "no valid ethernet hw addr\n");
		return -EINVAL;
	}

	/* initial rx and tx list */
	ret = desc_list_init();
	if (ret)
		return ret;

	phy_start(lp->phydev);
	phy_write(lp->phydev, MII_BMCR, BMCR_RESET);
	setup_system_regs(dev);
	setup_mac_addr(dev->dev_addr);

	bfin_mac_disable();
	ret = bfin_mac_enable(lp->phydev);
	if (ret)
		return ret;
	pr_debug("hardware init finished\n");

	netif_start_queue(dev);
	netif_carrier_on(dev);

	return 0;
}

/*
 * this makes the board clean up everything that it can
 * and not talk to the outside world.   Caused by
 * an 'ifconfig ethX down'
 */
static int bfin_mac_close(struct net_device *dev)
{
	struct bfin_mac_local *lp = netdev_priv(dev);
	pr_debug("%s: %s\n", dev->name, __func__);

	netif_stop_queue(dev);
	netif_carrier_off(dev);

	phy_stop(lp->phydev);
	phy_write(lp->phydev, MII_BMCR, BMCR_PDOWN);

	/* clear everything */
	bfin_mac_shutdown(dev);

	/* free the rx/tx buffers */
	desc_list_free();

	return 0;
}

static const struct net_device_ops bfin_mac_netdev_ops = {
	.ndo_open		= bfin_mac_open,
	.ndo_stop		= bfin_mac_close,
	.ndo_start_xmit		= bfin_mac_hard_start_xmit,
	.ndo_set_mac_address	= bfin_mac_set_mac_address,
	.ndo_tx_timeout		= bfin_mac_timeout,
	.ndo_set_multicast_list	= bfin_mac_set_multicast_list,
	.ndo_do_ioctl           = bfin_mac_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= eth_change_mtu,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= bfin_mac_poll,
#endif
};

static int __devinit bfin_mac_probe(struct platform_device *pdev)
{
	struct net_device *ndev;
	struct bfin_mac_local *lp;
	struct platform_device *pd;
	struct bfin_mii_bus_platform_data *mii_bus_data;
	int rc;

	ndev = alloc_etherdev(sizeof(struct bfin_mac_local));
	if (!ndev) {
		dev_err(&pdev->dev, "Cannot allocate net device!\n");
		return -ENOMEM;
	}

	SET_NETDEV_DEV(ndev, &pdev->dev);
	platform_set_drvdata(pdev, ndev);
	lp = netdev_priv(ndev);
	lp->ndev = ndev;

	/* Grab the MAC address in the MAC */
	*(__le32 *) (&(ndev->dev_addr[0])) = cpu_to_le32(bfin_read_EMAC_ADDRLO());
	*(__le16 *) (&(ndev->dev_addr[4])) = cpu_to_le16((u16) bfin_read_EMAC_ADDRHI());

	/* probe mac */
	/*todo: how to proble? which is revision_register */
	bfin_write_EMAC_ADDRLO(0x12345678);
	if (bfin_read_EMAC_ADDRLO() != 0x12345678) {
		dev_err(&pdev->dev, "Cannot detect Blackfin on-chip ethernet MAC controller!\n");
		rc = -ENODEV;
		goto out_err_probe_mac;
	}


	/*
	 * Is it valid? (Did bootloader initialize it?)
	 * Grab the MAC from the board somehow
	 * this is done in the arch/blackfin/mach-bfxxx/boards/eth_mac.c
	 */
	if (!is_valid_ether_addr(ndev->dev_addr))
		bfin_get_ether_addr(ndev->dev_addr);

	/* If still not valid, get a random one */
	if (!is_valid_ether_addr(ndev->dev_addr))
		random_ether_addr(ndev->dev_addr);

	setup_mac_addr(ndev->dev_addr);

	if (!pdev->dev.platform_data) {
		dev_err(&pdev->dev, "Cannot get platform device bfin_mii_bus!\n");
		rc = -ENODEV;
		goto out_err_probe_mac;
	}
	pd = pdev->dev.platform_data;
	lp->mii_bus = platform_get_drvdata(pd);
	if (!lp->mii_bus) {
		dev_err(&pdev->dev, "Cannot get mii_bus!\n");
		rc = -ENODEV;
		goto out_err_probe_mac;
	}
	lp->mii_bus->priv = ndev;
	mii_bus_data = pd->dev.platform_data;

	rc = mii_probe(ndev, mii_bus_data->phy_mode);
	if (rc) {
		dev_err(&pdev->dev, "MII Probe failed!\n");
		goto out_err_mii_probe;
	}

	lp->vlan1_mask = ETH_P_8021Q | mii_bus_data->vlan1_mask;
	lp->vlan2_mask = ETH_P_8021Q | mii_bus_data->vlan2_mask;

	/* Fill in the fields of the device structure with ethernet values. */
	ether_setup(ndev);

	ndev->netdev_ops = &bfin_mac_netdev_ops;
	ndev->ethtool_ops = &bfin_mac_ethtool_ops;

	init_timer(&lp->tx_reclaim_timer);
	lp->tx_reclaim_timer.data = (unsigned long)lp;
	lp->tx_reclaim_timer.function = tx_reclaim_skb_timeout;

	spin_lock_init(&lp->lock);

	/* now, enable interrupts */
	/* register irq handler */
	rc = request_irq(IRQ_MAC_RX, bfin_mac_interrupt,
			IRQF_DISABLED, "EMAC_RX", ndev);
	if (rc) {
		dev_err(&pdev->dev, "Cannot request Blackfin MAC RX IRQ!\n");
		rc = -EBUSY;
		goto out_err_request_irq;
	}

	rc = register_netdev(ndev);
	if (rc) {
		dev_err(&pdev->dev, "Cannot register net device!\n");
		goto out_err_reg_ndev;
	}

	bfin_mac_hwtstamp_init(ndev);

	/* now, print out the card info, in a short format.. */
	netdev_info(ndev, "%s, Version %s\n", DRV_DESC, DRV_VERSION);

	return 0;

out_err_reg_ndev:
	free_irq(IRQ_MAC_RX, ndev);
out_err_request_irq:
out_err_mii_probe:
	mdiobus_unregister(lp->mii_bus);
	mdiobus_free(lp->mii_bus);
out_err_probe_mac:
	platform_set_drvdata(pdev, NULL);
	free_netdev(ndev);

	return rc;
}

static int __devexit bfin_mac_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct bfin_mac_local *lp = netdev_priv(ndev);

	platform_set_drvdata(pdev, NULL);

	lp->mii_bus->priv = NULL;

	unregister_netdev(ndev);

	free_irq(IRQ_MAC_RX, ndev);

	free_netdev(ndev);

	return 0;
}

#ifdef CONFIG_PM
static int bfin_mac_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	struct net_device *net_dev = platform_get_drvdata(pdev);
	struct bfin_mac_local *lp = netdev_priv(net_dev);

	if (lp->wol) {
		bfin_write_EMAC_OPMODE((bfin_read_EMAC_OPMODE() & ~TE) | RE);
		bfin_write_EMAC_WKUP_CTL(MPKE);
		enable_irq_wake(IRQ_MAC_WAKEDET);
	} else {
		if (netif_running(net_dev))
			bfin_mac_close(net_dev);
	}

	return 0;
}

static int bfin_mac_resume(struct platform_device *pdev)
{
	struct net_device *net_dev = platform_get_drvdata(pdev);
	struct bfin_mac_local *lp = netdev_priv(net_dev);

	if (lp->wol) {
		bfin_write_EMAC_OPMODE(bfin_read_EMAC_OPMODE() | TE);
		bfin_write_EMAC_WKUP_CTL(0);
		disable_irq_wake(IRQ_MAC_WAKEDET);
	} else {
		if (netif_running(net_dev))
			bfin_mac_open(net_dev);
	}

	return 0;
}
#else
#define bfin_mac_suspend NULL
#define bfin_mac_resume NULL
#endif	/* CONFIG_PM */

static int __devinit bfin_mii_bus_probe(struct platform_device *pdev)
{
	struct mii_bus *miibus;
	struct bfin_mii_bus_platform_data *mii_bus_pd;
	const unsigned short *pin_req;
	int rc, i;

	mii_bus_pd = dev_get_platdata(&pdev->dev);
	if (!mii_bus_pd) {
		dev_err(&pdev->dev, "No peripherals in platform data!\n");
		return -EINVAL;
	}

	/*
	 * We are setting up a network card,
	 * so set the GPIO pins to Ethernet mode
	 */
	pin_req = mii_bus_pd->mac_peripherals;
	rc = peripheral_request_list(pin_req, KBUILD_MODNAME);
	if (rc) {
		dev_err(&pdev->dev, "Requesting peripherals failed!\n");
		return rc;
	}

	rc = -ENOMEM;
	miibus = mdiobus_alloc();
	if (miibus == NULL)
		goto out_err_alloc;
	miibus->read = bfin_mdiobus_read;
	miibus->write = bfin_mdiobus_write;
	miibus->reset = bfin_mdiobus_reset;

	miibus->parent = &pdev->dev;
	miibus->name = "bfin_mii_bus";
	miibus->phy_mask = mii_bus_pd->phy_mask;

	snprintf(miibus->id, MII_BUS_ID_SIZE, "0");
	miibus->irq = kmalloc(sizeof(int)*PHY_MAX_ADDR, GFP_KERNEL);
	if (!miibus->irq)
		goto out_err_irq_alloc;

	for (i = rc; i < PHY_MAX_ADDR; ++i)
		miibus->irq[i] = PHY_POLL;

	rc = clamp(mii_bus_pd->phydev_number, 0, PHY_MAX_ADDR);
	if (rc != mii_bus_pd->phydev_number)
		dev_err(&pdev->dev, "Invalid number (%i) of phydevs\n",
			mii_bus_pd->phydev_number);
	for (i = 0; i < rc; ++i) {
		unsigned short phyaddr = mii_bus_pd->phydev_data[i].addr;
		if (phyaddr < PHY_MAX_ADDR)
			miibus->irq[phyaddr] = mii_bus_pd->phydev_data[i].irq;
		else
			dev_err(&pdev->dev,
				"Invalid PHY address %i for phydev %i\n",
				phyaddr, i);
	}

	rc = mdiobus_register(miibus);
	if (rc) {
		dev_err(&pdev->dev, "Cannot register MDIO bus!\n");
		goto out_err_mdiobus_register;
	}

	platform_set_drvdata(pdev, miibus);
	return 0;

out_err_mdiobus_register:
	kfree(miibus->irq);
out_err_irq_alloc:
	mdiobus_free(miibus);
out_err_alloc:
	peripheral_free_list(pin_req);

	return rc;
}

static int __devexit bfin_mii_bus_remove(struct platform_device *pdev)
{
	struct mii_bus *miibus = platform_get_drvdata(pdev);
	struct bfin_mii_bus_platform_data *mii_bus_pd =
		dev_get_platdata(&pdev->dev);

	platform_set_drvdata(pdev, NULL);
	mdiobus_unregister(miibus);
	kfree(miibus->irq);
	mdiobus_free(miibus);
	peripheral_free_list(mii_bus_pd->mac_peripherals);

	return 0;
}

static struct platform_driver bfin_mii_bus_driver = {
	.probe = bfin_mii_bus_probe,
	.remove = __devexit_p(bfin_mii_bus_remove),
	.driver = {
		.name = "bfin_mii_bus",
		.owner	= THIS_MODULE,
	},
};

static struct platform_driver bfin_mac_driver = {
	.probe = bfin_mac_probe,
	.remove = __devexit_p(bfin_mac_remove),
	.resume = bfin_mac_resume,
	.suspend = bfin_mac_suspend,
	.driver = {
		.name = KBUILD_MODNAME,
		.owner	= THIS_MODULE,
	},
};

static int __init bfin_mac_init(void)
{
	int ret;
	ret = platform_driver_register(&bfin_mii_bus_driver);
	if (!ret)
		return platform_driver_register(&bfin_mac_driver);
	return -ENODEV;
}

module_init(bfin_mac_init);

static void __exit bfin_mac_cleanup(void)
{
	platform_driver_unregister(&bfin_mac_driver);
	platform_driver_unregister(&bfin_mii_bus_driver);
}

module_exit(bfin_mac_cleanup);

