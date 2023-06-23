// SPDX-License-Identifier: GPL-2.0-only
/*
 * Dave DNET Ethernet Controller driver
 *
 * Copyright (C) 2008 Dave S.r.l. <www.dave.eu>
 * Copyright (C) 2009 Ilya Yanok, Emcraft Systems Ltd, <yanok@emcraft.com>
 */
#include <linux/io.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/phy.h>

#include "dnet.h"

#undef DEBUG

/* function for reading internal MAC register */
static u16 dnet_readw_mac(struct dnet *bp, u16 reg)
{
	u16 data_read;

	/* issue a read */
	dnet_writel(bp, reg, MACREG_ADDR);

	/* since a read/write op to the MAC is very slow,
	 * we must wait before reading the data */
	ndelay(500);

	/* read data read from the MAC register */
	data_read = dnet_readl(bp, MACREG_DATA);

	/* all done */
	return data_read;
}

/* function for writing internal MAC register */
static void dnet_writew_mac(struct dnet *bp, u16 reg, u16 val)
{
	/* load data to write */
	dnet_writel(bp, val, MACREG_DATA);

	/* issue a write */
	dnet_writel(bp, reg | DNET_INTERNAL_WRITE, MACREG_ADDR);

	/* since a read/write op to the MAC is very slow,
	 * we must wait before exiting */
	ndelay(500);
}

static void __dnet_set_hwaddr(struct dnet *bp)
{
	u16 tmp;

	tmp = be16_to_cpup((const __be16 *)bp->dev->dev_addr);
	dnet_writew_mac(bp, DNET_INTERNAL_MAC_ADDR_0_REG, tmp);
	tmp = be16_to_cpup((const __be16 *)(bp->dev->dev_addr + 2));
	dnet_writew_mac(bp, DNET_INTERNAL_MAC_ADDR_1_REG, tmp);
	tmp = be16_to_cpup((const __be16 *)(bp->dev->dev_addr + 4));
	dnet_writew_mac(bp, DNET_INTERNAL_MAC_ADDR_2_REG, tmp);
}

static void dnet_get_hwaddr(struct dnet *bp)
{
	u16 tmp;
	u8 addr[6];

	/*
	 * from MAC docs:
	 * "Note that the MAC address is stored in the registers in Hexadecimal
	 * form. For example, to set the MAC Address to: AC-DE-48-00-00-80
	 * would require writing 0xAC (octet 0) to address 0x0B (high byte of
	 * Mac_addr[15:0]), 0xDE (octet 1) to address 0x0A (Low byte of
	 * Mac_addr[15:0]), 0x48 (octet 2) to address 0x0D (high byte of
	 * Mac_addr[15:0]), 0x00 (octet 3) to address 0x0C (Low byte of
	 * Mac_addr[15:0]), 0x00 (octet 4) to address 0x0F (high byte of
	 * Mac_addr[15:0]), and 0x80 (octet 5) to address * 0x0E (Low byte of
	 * Mac_addr[15:0]).
	 */
	tmp = dnet_readw_mac(bp, DNET_INTERNAL_MAC_ADDR_0_REG);
	*((__be16 *)addr) = cpu_to_be16(tmp);
	tmp = dnet_readw_mac(bp, DNET_INTERNAL_MAC_ADDR_1_REG);
	*((__be16 *)(addr + 2)) = cpu_to_be16(tmp);
	tmp = dnet_readw_mac(bp, DNET_INTERNAL_MAC_ADDR_2_REG);
	*((__be16 *)(addr + 4)) = cpu_to_be16(tmp);

	if (is_valid_ether_addr(addr))
		eth_hw_addr_set(bp->dev, addr);
}

static int dnet_mdio_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct dnet *bp = bus->priv;
	u16 value;

	while (!(dnet_readw_mac(bp, DNET_INTERNAL_GMII_MNG_CTL_REG)
				& DNET_INTERNAL_GMII_MNG_CMD_FIN))
		cpu_relax();

	/* only 5 bits allowed for phy-addr and reg_offset */
	mii_id &= 0x1f;
	regnum &= 0x1f;

	/* prepare reg_value for a read */
	value = (mii_id << 8);
	value |= regnum;

	/* write control word */
	dnet_writew_mac(bp, DNET_INTERNAL_GMII_MNG_CTL_REG, value);

	/* wait for end of transfer */
	while (!(dnet_readw_mac(bp, DNET_INTERNAL_GMII_MNG_CTL_REG)
				& DNET_INTERNAL_GMII_MNG_CMD_FIN))
		cpu_relax();

	value = dnet_readw_mac(bp, DNET_INTERNAL_GMII_MNG_DAT_REG);

	pr_debug("mdio_read %02x:%02x <- %04x\n", mii_id, regnum, value);

	return value;
}

static int dnet_mdio_write(struct mii_bus *bus, int mii_id, int regnum,
			   u16 value)
{
	struct dnet *bp = bus->priv;
	u16 tmp;

	pr_debug("mdio_write %02x:%02x <- %04x\n", mii_id, regnum, value);

	while (!(dnet_readw_mac(bp, DNET_INTERNAL_GMII_MNG_CTL_REG)
				& DNET_INTERNAL_GMII_MNG_CMD_FIN))
		cpu_relax();

	/* prepare for a write operation */
	tmp = (1 << 13);

	/* only 5 bits allowed for phy-addr and reg_offset */
	mii_id &= 0x1f;
	regnum &= 0x1f;

	/* only 16 bits on data */
	value &= 0xffff;

	/* prepare reg_value for a write */
	tmp |= (mii_id << 8);
	tmp |= regnum;

	/* write data to write first */
	dnet_writew_mac(bp, DNET_INTERNAL_GMII_MNG_DAT_REG, value);

	/* write control word */
	dnet_writew_mac(bp, DNET_INTERNAL_GMII_MNG_CTL_REG, tmp);

	while (!(dnet_readw_mac(bp, DNET_INTERNAL_GMII_MNG_CTL_REG)
				& DNET_INTERNAL_GMII_MNG_CMD_FIN))
		cpu_relax();

	return 0;
}

static void dnet_handle_link_change(struct net_device *dev)
{
	struct dnet *bp = netdev_priv(dev);
	struct phy_device *phydev = dev->phydev;
	unsigned long flags;
	u32 mode_reg, ctl_reg;

	int status_change = 0;

	spin_lock_irqsave(&bp->lock, flags);

	mode_reg = dnet_readw_mac(bp, DNET_INTERNAL_MODE_REG);
	ctl_reg = dnet_readw_mac(bp, DNET_INTERNAL_RXTX_CONTROL_REG);

	if (phydev->link) {
		if (bp->duplex != phydev->duplex) {
			if (phydev->duplex)
				ctl_reg &=
				    ~(DNET_INTERNAL_RXTX_CONTROL_ENABLEHALFDUP);
			else
				ctl_reg |=
				    DNET_INTERNAL_RXTX_CONTROL_ENABLEHALFDUP;

			bp->duplex = phydev->duplex;
			status_change = 1;
		}

		if (bp->speed != phydev->speed) {
			status_change = 1;
			switch (phydev->speed) {
			case 1000:
				mode_reg |= DNET_INTERNAL_MODE_GBITEN;
				break;
			case 100:
			case 10:
				mode_reg &= ~DNET_INTERNAL_MODE_GBITEN;
				break;
			default:
				printk(KERN_WARNING
				       "%s: Ack!  Speed (%d) is not "
				       "10/100/1000!\n", dev->name,
				       phydev->speed);
				break;
			}
			bp->speed = phydev->speed;
		}
	}

	if (phydev->link != bp->link) {
		if (phydev->link) {
			mode_reg |=
			    (DNET_INTERNAL_MODE_RXEN | DNET_INTERNAL_MODE_TXEN);
		} else {
			mode_reg &=
			    ~(DNET_INTERNAL_MODE_RXEN |
			      DNET_INTERNAL_MODE_TXEN);
			bp->speed = 0;
			bp->duplex = -1;
		}
		bp->link = phydev->link;

		status_change = 1;
	}

	if (status_change) {
		dnet_writew_mac(bp, DNET_INTERNAL_RXTX_CONTROL_REG, ctl_reg);
		dnet_writew_mac(bp, DNET_INTERNAL_MODE_REG, mode_reg);
	}

	spin_unlock_irqrestore(&bp->lock, flags);

	if (status_change) {
		if (phydev->link)
			printk(KERN_INFO "%s: link up (%d/%s)\n",
			       dev->name, phydev->speed,
			       DUPLEX_FULL == phydev->duplex ? "Full" : "Half");
		else
			printk(KERN_INFO "%s: link down\n", dev->name);
	}
}

static int dnet_mii_probe(struct net_device *dev)
{
	struct dnet *bp = netdev_priv(dev);
	struct phy_device *phydev = NULL;

	/* find the first phy */
	phydev = phy_find_first(bp->mii_bus);

	if (!phydev) {
		printk(KERN_ERR "%s: no PHY found\n", dev->name);
		return -ENODEV;
	}

	/* TODO : add pin_irq */

	/* attach the mac to the phy */
	if (bp->capabilities & DNET_HAS_RMII) {
		phydev = phy_connect(dev, phydev_name(phydev),
				     &dnet_handle_link_change,
				     PHY_INTERFACE_MODE_RMII);
	} else {
		phydev = phy_connect(dev, phydev_name(phydev),
				     &dnet_handle_link_change,
				     PHY_INTERFACE_MODE_MII);
	}

	if (IS_ERR(phydev)) {
		printk(KERN_ERR "%s: Could not attach to PHY\n", dev->name);
		return PTR_ERR(phydev);
	}

	/* mask with MAC supported features */
	if (bp->capabilities & DNET_HAS_GIGABIT)
		phy_set_max_speed(phydev, SPEED_1000);
	else
		phy_set_max_speed(phydev, SPEED_100);

	phy_support_asym_pause(phydev);

	bp->link = 0;
	bp->speed = 0;
	bp->duplex = -1;

	return 0;
}

static int dnet_mii_init(struct dnet *bp)
{
	int err;

	bp->mii_bus = mdiobus_alloc();
	if (bp->mii_bus == NULL)
		return -ENOMEM;

	bp->mii_bus->name = "dnet_mii_bus";
	bp->mii_bus->read = &dnet_mdio_read;
	bp->mii_bus->write = &dnet_mdio_write;

	snprintf(bp->mii_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		bp->pdev->name, bp->pdev->id);

	bp->mii_bus->priv = bp;

	if (mdiobus_register(bp->mii_bus)) {
		err = -ENXIO;
		goto err_out;
	}

	if (dnet_mii_probe(bp->dev) != 0) {
		err = -ENXIO;
		goto err_out_unregister_bus;
	}

	return 0;

err_out_unregister_bus:
	mdiobus_unregister(bp->mii_bus);
err_out:
	mdiobus_free(bp->mii_bus);
	return err;
}

/* For Neptune board: LINK1000 as Link LED and TX as activity LED */
static int dnet_phy_marvell_fixup(struct phy_device *phydev)
{
	return phy_write(phydev, 0x18, 0x4148);
}

static void dnet_update_stats(struct dnet *bp)
{
	u32 __iomem *reg = bp->regs + DNET_RX_PKT_IGNR_CNT;
	u32 *p = &bp->hw_stats.rx_pkt_ignr;
	u32 *end = &bp->hw_stats.rx_byte + 1;

	WARN_ON((unsigned long)(end - p - 1) !=
		(DNET_RX_BYTE_CNT - DNET_RX_PKT_IGNR_CNT) / 4);

	for (; p < end; p++, reg++)
		*p += readl(reg);

	reg = bp->regs + DNET_TX_UNICAST_CNT;
	p = &bp->hw_stats.tx_unicast;
	end = &bp->hw_stats.tx_byte + 1;

	WARN_ON((unsigned long)(end - p - 1) !=
		(DNET_TX_BYTE_CNT - DNET_TX_UNICAST_CNT) / 4);

	for (; p < end; p++, reg++)
		*p += readl(reg);
}

static int dnet_poll(struct napi_struct *napi, int budget)
{
	struct dnet *bp = container_of(napi, struct dnet, napi);
	struct net_device *dev = bp->dev;
	int npackets = 0;
	unsigned int pkt_len;
	struct sk_buff *skb;
	unsigned int *data_ptr;
	u32 int_enable;
	u32 cmd_word;
	int i;

	while (npackets < budget) {
		/*
		 * break out of while loop if there are no more
		 * packets waiting
		 */
		if (!(dnet_readl(bp, RX_FIFO_WCNT) >> 16))
			break;

		cmd_word = dnet_readl(bp, RX_LEN_FIFO);
		pkt_len = cmd_word & 0xFFFF;

		if (cmd_word & 0xDF180000)
			printk(KERN_ERR "%s packet receive error %x\n",
			       __func__, cmd_word);

		skb = netdev_alloc_skb(dev, pkt_len + 5);
		if (skb != NULL) {
			/* Align IP on 16 byte boundaries */
			skb_reserve(skb, 2);
			/*
			 * 'skb_put()' points to the start of sk_buff
			 * data area.
			 */
			data_ptr = skb_put(skb, pkt_len);
			for (i = 0; i < (pkt_len + 3) >> 2; i++)
				*data_ptr++ = dnet_readl(bp, RX_DATA_FIFO);
			skb->protocol = eth_type_trans(skb, dev);
			netif_receive_skb(skb);
			npackets++;
		} else
			printk(KERN_NOTICE
			       "%s: No memory to allocate a sk_buff of "
			       "size %u.\n", dev->name, pkt_len);
	}

	if (npackets < budget) {
		/* We processed all packets available.  Tell NAPI it can
		 * stop polling then re-enable rx interrupts.
		 */
		napi_complete_done(napi, npackets);
		int_enable = dnet_readl(bp, INTR_ENB);
		int_enable |= DNET_INTR_SRC_RX_CMDFIFOAF;
		dnet_writel(bp, int_enable, INTR_ENB);
	}

	return npackets;
}

static irqreturn_t dnet_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct dnet *bp = netdev_priv(dev);
	u32 int_src, int_enable, int_current;
	unsigned long flags;
	unsigned int handled = 0;

	spin_lock_irqsave(&bp->lock, flags);

	/* read and clear the DNET irq (clear on read) */
	int_src = dnet_readl(bp, INTR_SRC);
	int_enable = dnet_readl(bp, INTR_ENB);
	int_current = int_src & int_enable;

	/* restart the queue if we had stopped it for TX fifo almost full */
	if (int_current & DNET_INTR_SRC_TX_FIFOAE) {
		int_enable = dnet_readl(bp, INTR_ENB);
		int_enable &= ~DNET_INTR_ENB_TX_FIFOAE;
		dnet_writel(bp, int_enable, INTR_ENB);
		netif_wake_queue(dev);
		handled = 1;
	}

	/* RX FIFO error checking */
	if (int_current &
	    (DNET_INTR_SRC_RX_CMDFIFOFF | DNET_INTR_SRC_RX_DATAFIFOFF)) {
		printk(KERN_ERR "%s: RX fifo error %x, irq %x\n", __func__,
		       dnet_readl(bp, RX_STATUS), int_current);
		/* we can only flush the RX FIFOs */
		dnet_writel(bp, DNET_SYS_CTL_RXFIFOFLUSH, SYS_CTL);
		ndelay(500);
		dnet_writel(bp, 0, SYS_CTL);
		handled = 1;
	}

	/* TX FIFO error checking */
	if (int_current &
	    (DNET_INTR_SRC_TX_FIFOFULL | DNET_INTR_SRC_TX_DISCFRM)) {
		printk(KERN_ERR "%s: TX fifo error %x, irq %x\n", __func__,
		       dnet_readl(bp, TX_STATUS), int_current);
		/* we can only flush the TX FIFOs */
		dnet_writel(bp, DNET_SYS_CTL_TXFIFOFLUSH, SYS_CTL);
		ndelay(500);
		dnet_writel(bp, 0, SYS_CTL);
		handled = 1;
	}

	if (int_current & DNET_INTR_SRC_RX_CMDFIFOAF) {
		if (napi_schedule_prep(&bp->napi)) {
			/*
			 * There's no point taking any more interrupts
			 * until we have processed the buffers
			 */
			/* Disable Rx interrupts and schedule NAPI poll */
			int_enable = dnet_readl(bp, INTR_ENB);
			int_enable &= ~DNET_INTR_SRC_RX_CMDFIFOAF;
			dnet_writel(bp, int_enable, INTR_ENB);
			__napi_schedule(&bp->napi);
		}
		handled = 1;
	}

	if (!handled)
		pr_debug("%s: irq %x remains\n", __func__, int_current);

	spin_unlock_irqrestore(&bp->lock, flags);

	return IRQ_RETVAL(handled);
}

#ifdef DEBUG
static inline void dnet_print_skb(struct sk_buff *skb)
{
	int k;
	printk(KERN_DEBUG PFX "data:");
	for (k = 0; k < skb->len; k++)
		printk(" %02x", (unsigned int)skb->data[k]);
	printk("\n");
}
#else
#define dnet_print_skb(skb)	do {} while (0)
#endif

static netdev_tx_t dnet_start_xmit(struct sk_buff *skb, struct net_device *dev)
{

	struct dnet *bp = netdev_priv(dev);
	unsigned int i, tx_cmd, wrsz;
	unsigned long flags;
	unsigned int *bufp;
	u32 irq_enable;

	dnet_readl(bp, TX_STATUS);

	pr_debug("start_xmit: len %u head %p data %p\n",
	       skb->len, skb->head, skb->data);
	dnet_print_skb(skb);

	spin_lock_irqsave(&bp->lock, flags);

	dnet_readl(bp, TX_STATUS);

	bufp = (unsigned int *)(((unsigned long) skb->data) & ~0x3UL);
	wrsz = (u32) skb->len + 3;
	wrsz += ((unsigned long) skb->data) & 0x3;
	wrsz >>= 2;
	tx_cmd = ((((unsigned long)(skb->data)) & 0x03) << 16) | (u32) skb->len;

	/* check if there is enough room for the current frame */
	if (wrsz < (DNET_FIFO_SIZE - dnet_readl(bp, TX_FIFO_WCNT))) {
		for (i = 0; i < wrsz; i++)
			dnet_writel(bp, *bufp++, TX_DATA_FIFO);

		/*
		 * inform MAC that a packet's written and ready to be
		 * shipped out
		 */
		dnet_writel(bp, tx_cmd, TX_LEN_FIFO);
	}

	if (dnet_readl(bp, TX_FIFO_WCNT) > DNET_FIFO_TX_DATA_AF_TH) {
		netif_stop_queue(dev);
		dnet_readl(bp, INTR_SRC);
		irq_enable = dnet_readl(bp, INTR_ENB);
		irq_enable |= DNET_INTR_ENB_TX_FIFOAE;
		dnet_writel(bp, irq_enable, INTR_ENB);
	}

	skb_tx_timestamp(skb);

	/* free the buffer */
	dev_kfree_skb(skb);

	spin_unlock_irqrestore(&bp->lock, flags);

	return NETDEV_TX_OK;
}

static void dnet_reset_hw(struct dnet *bp)
{
	/* put ts_mac in IDLE state i.e. disable rx/tx */
	dnet_writew_mac(bp, DNET_INTERNAL_MODE_REG, DNET_INTERNAL_MODE_FCEN);

	/*
	 * RX FIFO almost full threshold: only cmd FIFO almost full is
	 * implemented for RX side
	 */
	dnet_writel(bp, DNET_FIFO_RX_CMD_AF_TH, RX_FIFO_TH);
	/*
	 * TX FIFO almost empty threshold: only data FIFO almost empty
	 * is implemented for TX side
	 */
	dnet_writel(bp, DNET_FIFO_TX_DATA_AE_TH, TX_FIFO_TH);

	/* flush rx/tx fifos */
	dnet_writel(bp, DNET_SYS_CTL_RXFIFOFLUSH | DNET_SYS_CTL_TXFIFOFLUSH,
			SYS_CTL);
	msleep(1);
	dnet_writel(bp, 0, SYS_CTL);
}

static void dnet_init_hw(struct dnet *bp)
{
	u32 config;

	dnet_reset_hw(bp);
	__dnet_set_hwaddr(bp);

	config = dnet_readw_mac(bp, DNET_INTERNAL_RXTX_CONTROL_REG);

	if (bp->dev->flags & IFF_PROMISC)
		/* Copy All Frames */
		config |= DNET_INTERNAL_RXTX_CONTROL_ENPROMISC;
	if (!(bp->dev->flags & IFF_BROADCAST))
		/* No BroadCast */
		config |= DNET_INTERNAL_RXTX_CONTROL_RXMULTICAST;

	config |= DNET_INTERNAL_RXTX_CONTROL_RXPAUSE |
	    DNET_INTERNAL_RXTX_CONTROL_RXBROADCAST |
	    DNET_INTERNAL_RXTX_CONTROL_DROPCONTROL |
	    DNET_INTERNAL_RXTX_CONTROL_DISCFXFCS;

	dnet_writew_mac(bp, DNET_INTERNAL_RXTX_CONTROL_REG, config);

	/* clear irq before enabling them */
	config = dnet_readl(bp, INTR_SRC);

	/* enable RX/TX interrupt, recv packet ready interrupt */
	dnet_writel(bp, DNET_INTR_ENB_GLOBAL_ENABLE | DNET_INTR_ENB_RX_SUMMARY |
			DNET_INTR_ENB_TX_SUMMARY | DNET_INTR_ENB_RX_FIFOERR |
			DNET_INTR_ENB_RX_ERROR | DNET_INTR_ENB_RX_FIFOFULL |
			DNET_INTR_ENB_TX_FIFOFULL | DNET_INTR_ENB_TX_DISCFRM |
			DNET_INTR_ENB_RX_PKTRDY, INTR_ENB);
}

static int dnet_open(struct net_device *dev)
{
	struct dnet *bp = netdev_priv(dev);

	/* if the phy is not yet register, retry later */
	if (!dev->phydev)
		return -EAGAIN;

	napi_enable(&bp->napi);
	dnet_init_hw(bp);

	phy_start_aneg(dev->phydev);

	/* schedule a link state check */
	phy_start(dev->phydev);

	netif_start_queue(dev);

	return 0;
}

static int dnet_close(struct net_device *dev)
{
	struct dnet *bp = netdev_priv(dev);

	netif_stop_queue(dev);
	napi_disable(&bp->napi);

	if (dev->phydev)
		phy_stop(dev->phydev);

	dnet_reset_hw(bp);
	netif_carrier_off(dev);

	return 0;
}

static inline void dnet_print_pretty_hwstats(struct dnet_stats *hwstat)
{
	pr_debug("%s\n", __func__);
	pr_debug("----------------------------- RX statistics "
		 "-------------------------------\n");
	pr_debug("RX_PKT_IGNR_CNT %-8x\n", hwstat->rx_pkt_ignr);
	pr_debug("RX_LEN_CHK_ERR_CNT %-8x\n", hwstat->rx_len_chk_err);
	pr_debug("RX_LNG_FRM_CNT %-8x\n", hwstat->rx_lng_frm);
	pr_debug("RX_SHRT_FRM_CNT %-8x\n", hwstat->rx_shrt_frm);
	pr_debug("RX_IPG_VIOL_CNT %-8x\n", hwstat->rx_ipg_viol);
	pr_debug("RX_CRC_ERR_CNT %-8x\n", hwstat->rx_crc_err);
	pr_debug("RX_OK_PKT_CNT %-8x\n", hwstat->rx_ok_pkt);
	pr_debug("RX_CTL_FRM_CNT %-8x\n", hwstat->rx_ctl_frm);
	pr_debug("RX_PAUSE_FRM_CNT %-8x\n", hwstat->rx_pause_frm);
	pr_debug("RX_MULTICAST_CNT %-8x\n", hwstat->rx_multicast);
	pr_debug("RX_BROADCAST_CNT %-8x\n", hwstat->rx_broadcast);
	pr_debug("RX_VLAN_TAG_CNT %-8x\n", hwstat->rx_vlan_tag);
	pr_debug("RX_PRE_SHRINK_CNT %-8x\n", hwstat->rx_pre_shrink);
	pr_debug("RX_DRIB_NIB_CNT %-8x\n", hwstat->rx_drib_nib);
	pr_debug("RX_UNSUP_OPCD_CNT %-8x\n", hwstat->rx_unsup_opcd);
	pr_debug("RX_BYTE_CNT %-8x\n", hwstat->rx_byte);
	pr_debug("----------------------------- TX statistics "
		 "-------------------------------\n");
	pr_debug("TX_UNICAST_CNT %-8x\n", hwstat->tx_unicast);
	pr_debug("TX_PAUSE_FRM_CNT %-8x\n", hwstat->tx_pause_frm);
	pr_debug("TX_MULTICAST_CNT %-8x\n", hwstat->tx_multicast);
	pr_debug("TX_BRDCAST_CNT %-8x\n", hwstat->tx_brdcast);
	pr_debug("TX_VLAN_TAG_CNT %-8x\n", hwstat->tx_vlan_tag);
	pr_debug("TX_BAD_FCS_CNT %-8x\n", hwstat->tx_bad_fcs);
	pr_debug("TX_JUMBO_CNT %-8x\n", hwstat->tx_jumbo);
	pr_debug("TX_BYTE_CNT %-8x\n", hwstat->tx_byte);
}

static struct net_device_stats *dnet_get_stats(struct net_device *dev)
{

	struct dnet *bp = netdev_priv(dev);
	struct net_device_stats *nstat = &dev->stats;
	struct dnet_stats *hwstat = &bp->hw_stats;

	/* read stats from hardware */
	dnet_update_stats(bp);

	/* Convert HW stats into netdevice stats */
	nstat->rx_errors = (hwstat->rx_len_chk_err +
			    hwstat->rx_lng_frm + hwstat->rx_shrt_frm +
			    /* ignore IGP violation error
			    hwstat->rx_ipg_viol + */
			    hwstat->rx_crc_err +
			    hwstat->rx_pre_shrink +
			    hwstat->rx_drib_nib + hwstat->rx_unsup_opcd);
	nstat->tx_errors = hwstat->tx_bad_fcs;
	nstat->rx_length_errors = (hwstat->rx_len_chk_err +
				   hwstat->rx_lng_frm +
				   hwstat->rx_shrt_frm + hwstat->rx_pre_shrink);
	nstat->rx_crc_errors = hwstat->rx_crc_err;
	nstat->rx_frame_errors = hwstat->rx_pre_shrink + hwstat->rx_drib_nib;
	nstat->rx_packets = hwstat->rx_ok_pkt;
	nstat->tx_packets = (hwstat->tx_unicast +
			     hwstat->tx_multicast + hwstat->tx_brdcast);
	nstat->rx_bytes = hwstat->rx_byte;
	nstat->tx_bytes = hwstat->tx_byte;
	nstat->multicast = hwstat->rx_multicast;
	nstat->rx_missed_errors = hwstat->rx_pkt_ignr;

	dnet_print_pretty_hwstats(hwstat);

	return nstat;
}

static void dnet_get_drvinfo(struct net_device *dev,
			     struct ethtool_drvinfo *info)
{
	strscpy(info->driver, DRV_NAME, sizeof(info->driver));
	strscpy(info->bus_info, "0", sizeof(info->bus_info));
}

static const struct ethtool_ops dnet_ethtool_ops = {
	.get_drvinfo		= dnet_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_ts_info		= ethtool_op_get_ts_info,
	.get_link_ksettings     = phy_ethtool_get_link_ksettings,
	.set_link_ksettings     = phy_ethtool_set_link_ksettings,
};

static const struct net_device_ops dnet_netdev_ops = {
	.ndo_open		= dnet_open,
	.ndo_stop		= dnet_close,
	.ndo_get_stats		= dnet_get_stats,
	.ndo_start_xmit		= dnet_start_xmit,
	.ndo_eth_ioctl		= phy_do_ioctl_running,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static int dnet_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct net_device *dev;
	struct dnet *bp;
	struct phy_device *phydev;
	int err;
	unsigned int irq;

	irq = platform_get_irq(pdev, 0);

	dev = alloc_etherdev(sizeof(*bp));
	if (!dev)
		return -ENOMEM;

	/* TODO: Actually, we have some interesting features... */
	dev->features |= 0;

	bp = netdev_priv(dev);
	bp->dev = dev;

	platform_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	spin_lock_init(&bp->lock);

	bp->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(bp->regs)) {
		err = PTR_ERR(bp->regs);
		goto err_out_free_dev;
	}

	dev->irq = irq;
	err = request_irq(dev->irq, dnet_interrupt, 0, DRV_NAME, dev);
	if (err) {
		dev_err(&pdev->dev, "Unable to request IRQ %d (error %d)\n",
		       irq, err);
		goto err_out_free_dev;
	}

	dev->netdev_ops = &dnet_netdev_ops;
	netif_napi_add(dev, &bp->napi, dnet_poll);
	dev->ethtool_ops = &dnet_ethtool_ops;

	dev->base_addr = (unsigned long)bp->regs;

	bp->capabilities = dnet_readl(bp, VERCAPS) & DNET_CAPS_MASK;

	dnet_get_hwaddr(bp);

	if (!is_valid_ether_addr(dev->dev_addr)) {
		/* choose a random ethernet address */
		eth_hw_addr_random(dev);
		__dnet_set_hwaddr(bp);
	}

	err = register_netdev(dev);
	if (err) {
		dev_err(&pdev->dev, "Cannot register net device, aborting.\n");
		goto err_out_free_irq;
	}

	/* register the PHY board fixup (for Marvell 88E1111) */
	err = phy_register_fixup_for_uid(0x01410cc0, 0xfffffff0,
					 dnet_phy_marvell_fixup);
	/* we can live without it, so just issue a warning */
	if (err)
		dev_warn(&pdev->dev, "Cannot register PHY board fixup.\n");

	err = dnet_mii_init(bp);
	if (err)
		goto err_out_unregister_netdev;

	dev_info(&pdev->dev, "Dave DNET at 0x%p (0x%08x) irq %d %pM\n",
	       bp->regs, (unsigned int)res->start, dev->irq, dev->dev_addr);
	dev_info(&pdev->dev, "has %smdio, %sirq, %sgigabit, %sdma\n",
	       (bp->capabilities & DNET_HAS_MDIO) ? "" : "no ",
	       (bp->capabilities & DNET_HAS_IRQ) ? "" : "no ",
	       (bp->capabilities & DNET_HAS_GIGABIT) ? "" : "no ",
	       (bp->capabilities & DNET_HAS_DMA) ? "" : "no ");
	phydev = dev->phydev;
	phy_attached_info(phydev);

	return 0;

err_out_unregister_netdev:
	unregister_netdev(dev);
err_out_free_irq:
	free_irq(dev->irq, dev);
err_out_free_dev:
	free_netdev(dev);
	return err;
}

static int dnet_remove(struct platform_device *pdev)
{

	struct net_device *dev;
	struct dnet *bp;

	dev = platform_get_drvdata(pdev);

	if (dev) {
		bp = netdev_priv(dev);
		if (dev->phydev)
			phy_disconnect(dev->phydev);
		mdiobus_unregister(bp->mii_bus);
		mdiobus_free(bp->mii_bus);
		unregister_netdev(dev);
		free_irq(dev->irq, dev);
		free_netdev(dev);
	}

	return 0;
}

static struct platform_driver dnet_driver = {
	.probe		= dnet_probe,
	.remove		= dnet_remove,
	.driver		= {
		.name		= "dnet",
	},
};

module_platform_driver(dnet_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Dave DNET Ethernet driver");
MODULE_AUTHOR("Ilya Yanok <yanok@emcraft.com>, "
	      "Matteo Vit <matteo.vit@dave.eu>");
