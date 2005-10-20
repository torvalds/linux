/*
 * ibm_emac_core.c
 *
 * Ethernet driver for the built in ethernet on the IBM 4xx PowerPC
 * processors.
 * 
 * (c) 2003 Benjamin Herrenschmidt <benh@kernel.crashing.org>
 *
 * Based on original work by
 *
 *      Armin Kuster <akuster@mvista.com>
 * 	Johnnie Peters <jpeters@mvista.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 * TODO
 *       - Check for races in the "remove" code path
 *       - Add some Power Management to the MAC and the PHY
 *       - Audit remaining of non-rewritten code (--BenH)
 *       - Cleanup message display using msglevel mecanism
 *       - Address all errata
 *       - Audit all register update paths to ensure they
 *         are being written post soft reset if required.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/bitops.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/ocp.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/crc32.h>

#include "ibm_emac_core.h"

//#define MDIO_DEBUG(fmt) printk fmt
#define MDIO_DEBUG(fmt)

//#define LINK_DEBUG(fmt) printk fmt
#define LINK_DEBUG(fmt)

//#define PKT_DEBUG(fmt) printk fmt
#define PKT_DEBUG(fmt)

#define DRV_NAME        "emac"
#define DRV_VERSION     "2.0"
#define DRV_AUTHOR      "Benjamin Herrenschmidt <benh@kernel.crashing.org>"
#define DRV_DESC        "IBM EMAC Ethernet driver"

/*
 * When mdio_idx >= 0, contains a list of emac ocp_devs
 * that have had their initialization deferred until the
 * common MDIO controller has been initialized.
 */
LIST_HEAD(emac_init_list);

MODULE_AUTHOR(DRV_AUTHOR);
MODULE_DESCRIPTION(DRV_DESC);
MODULE_LICENSE("GPL");

static int skb_res = SKB_RES;
module_param(skb_res, int, 0444);
MODULE_PARM_DESC(skb_res, "Amount of data to reserve on skb buffs\n"
		 "The 405 handles a misaligned IP header fine but\n"
		 "this can help if you are routing to a tunnel or a\n"
		 "device that needs aligned data. 0..2");

#define RGMII_PRIV(ocpdev) ((struct ibm_ocp_rgmii*)ocp_get_drvdata(ocpdev))

static unsigned int rgmii_enable[] = {
	RGMII_RTBI,
	RGMII_RGMII,
	RGMII_TBI,
	RGMII_GMII
};

static unsigned int rgmii_speed_mask[] = {
	RGMII_MII2_SPDMASK,
	RGMII_MII3_SPDMASK
};

static unsigned int rgmii_speed100[] = {
	RGMII_MII2_100MB,
	RGMII_MII3_100MB
};

static unsigned int rgmii_speed1000[] = {
	RGMII_MII2_1000MB,
	RGMII_MII3_1000MB
};

#define ZMII_PRIV(ocpdev) ((struct ibm_ocp_zmii*)ocp_get_drvdata(ocpdev))

static unsigned int zmii_enable[][4] = {
	{ZMII_SMII0, ZMII_RMII0, ZMII_MII0,
	 ~(ZMII_MDI1 | ZMII_MDI2 | ZMII_MDI3)},
	{ZMII_SMII1, ZMII_RMII1, ZMII_MII1,
	 ~(ZMII_MDI0 | ZMII_MDI2 | ZMII_MDI3)},
	{ZMII_SMII2, ZMII_RMII2, ZMII_MII2,
	 ~(ZMII_MDI0 | ZMII_MDI1 | ZMII_MDI3)},
	{ZMII_SMII3, ZMII_RMII3, ZMII_MII3, ~(ZMII_MDI0 | ZMII_MDI1 | ZMII_MDI2)}
};

static unsigned int mdi_enable[] = {
	ZMII_MDI0,
	ZMII_MDI1,
	ZMII_MDI2,
	ZMII_MDI3
};

static unsigned int zmii_speed = 0x0;
static unsigned int zmii_speed100[] = {
	ZMII_MII0_100MB,
	ZMII_MII1_100MB,
	ZMII_MII2_100MB,
	ZMII_MII3_100MB
};

/* Since multiple EMACs share MDIO lines in various ways, we need
 * to avoid re-using the same PHY ID in cases where the arch didn't
 * setup precise phy_map entries
 */
static u32 busy_phy_map = 0;

/* If EMACs share a common MDIO device, this points to it */
static struct net_device *mdio_ndev = NULL;

struct emac_def_dev {
	struct list_head link;
	struct ocp_device *ocpdev;
	struct ibm_ocp_mal *mal;
};

static struct net_device_stats *emac_stats(struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	return &fep->stats;
};

static int
emac_init_rgmii(struct ocp_device *rgmii_dev, int input, int phy_mode)
{
	struct ibm_ocp_rgmii *rgmii = RGMII_PRIV(rgmii_dev);
	const char *mode_name[] = { "RTBI", "RGMII", "TBI", "GMII" };
	int mode = -1;

	if (!rgmii) {
		rgmii = kmalloc(sizeof(struct ibm_ocp_rgmii), GFP_KERNEL);

		if (rgmii == NULL) {
			printk(KERN_ERR
			       "rgmii%d: Out of memory allocating RGMII structure!\n",
			       rgmii_dev->def->index);
			return -ENOMEM;
		}

		memset(rgmii, 0, sizeof(*rgmii));

		rgmii->base =
		    (struct rgmii_regs *)ioremap(rgmii_dev->def->paddr,
						 sizeof(*rgmii->base));
		if (rgmii->base == NULL) {
			printk(KERN_ERR
			       "rgmii%d: Cannot ioremap bridge registers!\n",
			       rgmii_dev->def->index);

			kfree(rgmii);
			return -ENOMEM;
		}
		ocp_set_drvdata(rgmii_dev, rgmii);
	}

	if (phy_mode) {
		switch (phy_mode) {
		case PHY_MODE_GMII:
			mode = GMII;
			break;
		case PHY_MODE_TBI:
			mode = TBI;
			break;
		case PHY_MODE_RTBI:
			mode = RTBI;
			break;
		case PHY_MODE_RGMII:
		default:
			mode = RGMII;
		}
		rgmii->base->fer &= ~RGMII_FER_MASK(input);
		rgmii->base->fer |= rgmii_enable[mode] << (4 * input);
	} else {
		switch ((rgmii->base->fer & RGMII_FER_MASK(input)) >> (4 *
								       input)) {
		case RGMII_RTBI:
			mode = RTBI;
			break;
		case RGMII_RGMII:
			mode = RGMII;
			break;
		case RGMII_TBI:
			mode = TBI;
			break;
		case RGMII_GMII:
			mode = GMII;
		}
	}

	/* Set mode to RGMII if nothing valid is detected */
	if (mode < 0)
		mode = RGMII;

	printk(KERN_NOTICE "rgmii%d: input %d in %s mode\n",
	       rgmii_dev->def->index, input, mode_name[mode]);

	rgmii->mode[input] = mode;
	rgmii->users++;

	return 0;
}

static void
emac_rgmii_port_speed(struct ocp_device *ocpdev, int input, int speed)
{
	struct ibm_ocp_rgmii *rgmii = RGMII_PRIV(ocpdev);
	unsigned int rgmii_speed;

	rgmii_speed = in_be32(&rgmii->base->ssr);

	rgmii_speed &= ~rgmii_speed_mask[input];

	if (speed == 1000)
		rgmii_speed |= rgmii_speed1000[input];
	else if (speed == 100)
		rgmii_speed |= rgmii_speed100[input];

	out_be32(&rgmii->base->ssr, rgmii_speed);
}

static void emac_close_rgmii(struct ocp_device *ocpdev)
{
	struct ibm_ocp_rgmii *rgmii = RGMII_PRIV(ocpdev);
	BUG_ON(!rgmii || rgmii->users == 0);

	if (!--rgmii->users) {
		ocp_set_drvdata(ocpdev, NULL);
		iounmap((void *)rgmii->base);
		kfree(rgmii);
	}
}

static int emac_init_zmii(struct ocp_device *zmii_dev, int input, int phy_mode)
{
	struct ibm_ocp_zmii *zmii = ZMII_PRIV(zmii_dev);
	const char *mode_name[] = { "SMII", "RMII", "MII" };
	int mode = -1;

	if (!zmii) {
		zmii = kmalloc(sizeof(struct ibm_ocp_zmii), GFP_KERNEL);
		if (zmii == NULL) {
			printk(KERN_ERR
			       "zmii%d: Out of memory allocating ZMII structure!\n",
			       zmii_dev->def->index);
			return -ENOMEM;
		}
		memset(zmii, 0, sizeof(*zmii));

		zmii->base =
		    (struct zmii_regs *)ioremap(zmii_dev->def->paddr,
						sizeof(*zmii->base));
		if (zmii->base == NULL) {
			printk(KERN_ERR
			       "zmii%d: Cannot ioremap bridge registers!\n",
			       zmii_dev->def->index);

			kfree(zmii);
			return -ENOMEM;
		}
		ocp_set_drvdata(zmii_dev, zmii);
	}

	if (phy_mode) {
		switch (phy_mode) {
		case PHY_MODE_MII:
			mode = MII;
			break;
		case PHY_MODE_RMII:
			mode = RMII;
			break;
		case PHY_MODE_SMII:
		default:
			mode = SMII;
		}
		zmii->base->fer &= ~ZMII_FER_MASK(input);
		zmii->base->fer |= zmii_enable[input][mode];
	} else {
		switch ((zmii->base->fer & ZMII_FER_MASK(input)) << (4 * input)) {
		case ZMII_MII0:
			mode = MII;
			break;
		case ZMII_RMII0:
			mode = RMII;
			break;
		case ZMII_SMII0:
			mode = SMII;
		}
	}

	/* Set mode to SMII if nothing valid is detected */
	if (mode < 0)
		mode = SMII;

	printk(KERN_NOTICE "zmii%d: input %d in %s mode\n",
	       zmii_dev->def->index, input, mode_name[mode]);

	zmii->mode[input] = mode;
	zmii->users++;

	return 0;
}

static void emac_enable_zmii_port(struct ocp_device *ocpdev, int input)
{
	u32 mask;
	struct ibm_ocp_zmii *zmii = ZMII_PRIV(ocpdev);

	mask = in_be32(&zmii->base->fer);
	mask &= zmii_enable[input][MDI];	/* turn all non enabled MDI's off */
	mask |= zmii_enable[input][zmii->mode[input]] | mdi_enable[input];
	out_be32(&zmii->base->fer, mask);
}

static void
emac_zmii_port_speed(struct ocp_device *ocpdev, int input, int speed)
{
	struct ibm_ocp_zmii *zmii = ZMII_PRIV(ocpdev);

	if (speed == 100)
		zmii_speed |= zmii_speed100[input];
	else
		zmii_speed &= ~zmii_speed100[input];

	out_be32(&zmii->base->ssr, zmii_speed);
}

static void emac_close_zmii(struct ocp_device *ocpdev)
{
	struct ibm_ocp_zmii *zmii = ZMII_PRIV(ocpdev);
	BUG_ON(!zmii || zmii->users == 0);

	if (!--zmii->users) {
		ocp_set_drvdata(ocpdev, NULL);
		iounmap((void *)zmii->base);
		kfree(zmii);
	}
}

int emac_phy_read(struct net_device *dev, int mii_id, int reg)
{
	int count;
	uint32_t stacr;
	struct ocp_enet_private *fep = dev->priv;
	emac_t *emacp = fep->emacp;

	MDIO_DEBUG(("%s: phy_read, id: 0x%x, reg: 0x%x\n", dev->name, mii_id,
		    reg));

	/* Enable proper ZMII port */
	if (fep->zmii_dev)
		emac_enable_zmii_port(fep->zmii_dev, fep->zmii_input);

	/* Use the EMAC that has the MDIO port */
	if (fep->mdio_dev) {
		dev = fep->mdio_dev;
		fep = dev->priv;
		emacp = fep->emacp;
	}

	count = 0;
	while ((((stacr = in_be32(&emacp->em0stacr)) & EMAC_STACR_OC) == 0)
					&& (count++ < MDIO_DELAY))
		udelay(1);
	MDIO_DEBUG((" (count was %d)\n", count));

	if ((stacr & EMAC_STACR_OC) == 0) {
		printk(KERN_WARNING "%s: PHY read timeout #1!\n", dev->name);
		return -1;
	}

	/* Clear the speed bits and make a read request to the PHY */
	stacr = ((EMAC_STACR_READ | (reg & 0x1f)) & ~EMAC_STACR_CLK_100MHZ);
	stacr |= ((mii_id & 0x1F) << 5);

	out_be32(&emacp->em0stacr, stacr);

	count = 0;
	while ((((stacr = in_be32(&emacp->em0stacr)) & EMAC_STACR_OC) == 0)
					&& (count++ < MDIO_DELAY))
		udelay(1);
	MDIO_DEBUG((" (count was %d)\n", count));

	if ((stacr & EMAC_STACR_OC) == 0) {
		printk(KERN_WARNING "%s: PHY read timeout #2!\n", dev->name);
		return -1;
	}

	/* Check for a read error */
	if (stacr & EMAC_STACR_PHYE) {
		MDIO_DEBUG(("EMAC MDIO PHY error !\n"));
		return -1;
	}

	MDIO_DEBUG((" -> 0x%x\n", stacr >> 16));

	return (stacr >> 16);
}

void emac_phy_write(struct net_device *dev, int mii_id, int reg, int data)
{
	int count;
	uint32_t stacr;
	struct ocp_enet_private *fep = dev->priv;
	emac_t *emacp = fep->emacp;

	MDIO_DEBUG(("%s phy_write, id: 0x%x, reg: 0x%x, data: 0x%x\n",
		    dev->name, mii_id, reg, data));

	/* Enable proper ZMII port */
	if (fep->zmii_dev)
		emac_enable_zmii_port(fep->zmii_dev, fep->zmii_input);

	/* Use the EMAC that has the MDIO port */
	if (fep->mdio_dev) {
		dev = fep->mdio_dev;
		fep = dev->priv;
		emacp = fep->emacp;
	}

	count = 0;
	while ((((stacr = in_be32(&emacp->em0stacr)) & EMAC_STACR_OC) == 0)
					&& (count++ < MDIO_DELAY))
		udelay(1);
	MDIO_DEBUG((" (count was %d)\n", count));

	if ((stacr & EMAC_STACR_OC) == 0) {
		printk(KERN_WARNING "%s: PHY write timeout #2!\n", dev->name);
		return;
	}

	/* Clear the speed bits and make a read request to the PHY */

	stacr = ((EMAC_STACR_WRITE | (reg & 0x1f)) & ~EMAC_STACR_CLK_100MHZ);
	stacr |= ((mii_id & 0x1f) << 5) | ((data & 0xffff) << 16);

	out_be32(&emacp->em0stacr, stacr);

	count = 0;
	while ((((stacr = in_be32(&emacp->em0stacr)) & EMAC_STACR_OC) == 0)
					&& (count++ < MDIO_DELAY))
		udelay(1);
	MDIO_DEBUG((" (count was %d)\n", count));

	if ((stacr & EMAC_STACR_OC) == 0)
		printk(KERN_WARNING "%s: PHY write timeout #2!\n", dev->name);

	/* Check for a write error */
	if ((stacr & EMAC_STACR_PHYE) != 0) {
		MDIO_DEBUG(("EMAC MDIO PHY error !\n"));
	}
}

static void emac_txeob_dev(void *param, u32 chanmask)
{
	struct net_device *dev = param;
	struct ocp_enet_private *fep = dev->priv;
	unsigned long flags;

	spin_lock_irqsave(&fep->lock, flags);

	PKT_DEBUG(("emac_txeob_dev() entry, tx_cnt: %d\n", fep->tx_cnt));

	while (fep->tx_cnt &&
	       !(fep->tx_desc[fep->ack_slot].ctrl & MAL_TX_CTRL_READY)) {

		if (fep->tx_desc[fep->ack_slot].ctrl & MAL_TX_CTRL_LAST) {
			/* Tell the system the transmit completed. */
			dma_unmap_single(&fep->ocpdev->dev,
					 fep->tx_desc[fep->ack_slot].data_ptr,
					 fep->tx_desc[fep->ack_slot].data_len,
					 DMA_TO_DEVICE);
			dev_kfree_skb_irq(fep->tx_skb[fep->ack_slot]);

			if (fep->tx_desc[fep->ack_slot].ctrl &
			    (EMAC_TX_ST_EC | EMAC_TX_ST_MC | EMAC_TX_ST_SC))
				fep->stats.collisions++;
		}

		fep->tx_skb[fep->ack_slot] = (struct sk_buff *)NULL;
		if (++fep->ack_slot == NUM_TX_BUFF)
			fep->ack_slot = 0;

		fep->tx_cnt--;
	}
	if (fep->tx_cnt < NUM_TX_BUFF)
		netif_wake_queue(dev);

	PKT_DEBUG(("emac_txeob_dev() exit, tx_cnt: %d\n", fep->tx_cnt));

	spin_unlock_irqrestore(&fep->lock, flags);
}

/*
  Fill/Re-fill the rx chain with valid ctrl/ptrs.
  This function will fill from rx_slot up to the parm end.
  So to completely fill the chain pre-set rx_slot to 0 and
  pass in an end of 0.
 */
static void emac_rx_fill(struct net_device *dev, int end)
{
	int i;
	struct ocp_enet_private *fep = dev->priv;

	i = fep->rx_slot;
	do {
		/* We don't want the 16 bytes skb_reserve done by dev_alloc_skb,
		 * it breaks our cache line alignement. However, we still allocate
		 * +16 so that we end up allocating the exact same size as
		 * dev_alloc_skb() would do.
		 * Also, because of the skb_res, the max DMA size we give to EMAC
		 * is slighly wrong, causing it to potentially DMA 2 more bytes
		 * from a broken/oversized packet. These 16 bytes will take care
		 * that we don't walk on somebody else toes with that.
		 */
		fep->rx_skb[i] =
		    alloc_skb(fep->rx_buffer_size + 16, GFP_ATOMIC);

		if (fep->rx_skb[i] == NULL) {
			/* Keep rx_slot here, the next time clean/fill is called
			 * we will try again before the MAL wraps back here
			 * If the MAL tries to use this descriptor with
			 * the EMPTY bit off it will cause the
			 * rxde interrupt.  That is where we will
			 * try again to allocate an sk_buff.
			 */
			break;

		}

		if (skb_res)
			skb_reserve(fep->rx_skb[i], skb_res);

		/* We must NOT dma_map_single the cache line right after the
		 * buffer, so we must crop our sync size to account for the
		 * reserved space
		 */
		fep->rx_desc[i].data_ptr =
		    (unsigned char *)dma_map_single(&fep->ocpdev->dev,
						    (void *)fep->rx_skb[i]->
						    data,
						    fep->rx_buffer_size -
						    skb_res, DMA_FROM_DEVICE);

		/*
		 * Some 4xx implementations use the previously
		 * reserved bits in data_len to encode the MS
		 * 4-bits of a 36-bit physical address (ERPN)
		 * This must be initialized.
		 */
		fep->rx_desc[i].data_len = 0;
		fep->rx_desc[i].ctrl = MAL_RX_CTRL_EMPTY | MAL_RX_CTRL_INTR |
		    (i == (NUM_RX_BUFF - 1) ? MAL_RX_CTRL_WRAP : 0);

	} while ((i = (i + 1) % NUM_RX_BUFF) != end);

	fep->rx_slot = i;
}

static void
emac_rx_csum(struct net_device *dev, unsigned short ctrl, struct sk_buff *skb)
{
	struct ocp_enet_private *fep = dev->priv;

	/* Exit if interface has no TAH engine */
	if (!fep->tah_dev) {
		skb->ip_summed = CHECKSUM_NONE;
		return;
	}

	/* Check for TCP/UDP/IP csum error */
	if (ctrl & EMAC_CSUM_VER_ERROR) {
		/* Let the stack verify checksum errors */
		skb->ip_summed = CHECKSUM_NONE;
/*		adapter->hw_csum_err++; */
	} else {
		/* Csum is good */
		skb->ip_summed = CHECKSUM_UNNECESSARY;
/*		adapter->hw_csum_good++; */
	}
}

static int emac_rx_clean(struct net_device *dev)
{
	int i, b, bnum = 0, buf[6];
	int error, frame_length;
	struct ocp_enet_private *fep = dev->priv;
	unsigned short ctrl;

	i = fep->rx_slot;

	PKT_DEBUG(("emac_rx_clean() entry, rx_slot: %d\n", fep->rx_slot));

	do {
		if (fep->rx_skb[i] == NULL)
			continue;	/*we have already handled the packet but haved failed to alloc */
		/* 
		   since rx_desc is in uncached mem we don't keep reading it directly 
		   we pull out a local copy of ctrl and do the checks on the copy.
		 */
		ctrl = fep->rx_desc[i].ctrl;
		if (ctrl & MAL_RX_CTRL_EMPTY)
			break;	/*we don't have any more ready packets */

		if (EMAC_IS_BAD_RX_PACKET(ctrl)) {
			fep->stats.rx_errors++;
			fep->stats.rx_dropped++;

			if (ctrl & EMAC_RX_ST_OE)
				fep->stats.rx_fifo_errors++;
			if (ctrl & EMAC_RX_ST_AE)
				fep->stats.rx_frame_errors++;
			if (ctrl & EMAC_RX_ST_BFCS)
				fep->stats.rx_crc_errors++;
			if (ctrl & (EMAC_RX_ST_RP | EMAC_RX_ST_PTL |
				    EMAC_RX_ST_ORE | EMAC_RX_ST_IRE))
				fep->stats.rx_length_errors++;
		} else {
			if ((ctrl & (MAL_RX_CTRL_FIRST | MAL_RX_CTRL_LAST)) ==
			    (MAL_RX_CTRL_FIRST | MAL_RX_CTRL_LAST)) {
				/* Single descriptor packet */
				emac_rx_csum(dev, ctrl, fep->rx_skb[i]);
				/* Send the skb up the chain. */
				frame_length = fep->rx_desc[i].data_len - 4;
				skb_put(fep->rx_skb[i], frame_length);
				fep->rx_skb[i]->dev = dev;
				fep->rx_skb[i]->protocol =
				    eth_type_trans(fep->rx_skb[i], dev);
				error = netif_rx(fep->rx_skb[i]);

				if ((error == NET_RX_DROP) ||
				    (error == NET_RX_BAD)) {
					fep->stats.rx_dropped++;
				} else {
					fep->stats.rx_packets++;
					fep->stats.rx_bytes += frame_length;
				}
				fep->rx_skb[i] = NULL;
			} else {
				/* Multiple descriptor packet */
				if (ctrl & MAL_RX_CTRL_FIRST) {
					if (fep->rx_desc[(i + 1) % NUM_RX_BUFF].
					    ctrl & MAL_RX_CTRL_EMPTY)
						break;
					bnum = 0;
					buf[bnum] = i;
					++bnum;
					continue;
				}
				if (((ctrl & MAL_RX_CTRL_FIRST) !=
				     MAL_RX_CTRL_FIRST) &&
				    ((ctrl & MAL_RX_CTRL_LAST) !=
				     MAL_RX_CTRL_LAST)) {
					if (fep->rx_desc[(i + 1) %
							 NUM_RX_BUFF].ctrl &
					    MAL_RX_CTRL_EMPTY) {
						i = buf[0];
						break;
					}
					buf[bnum] = i;
					++bnum;
					continue;
				}
				if (ctrl & MAL_RX_CTRL_LAST) {
					buf[bnum] = i;
					++bnum;
					skb_put(fep->rx_skb[buf[0]],
						fep->rx_desc[buf[0]].data_len);
					for (b = 1; b < bnum; b++) {
						/*
						 * MAL is braindead, we need
						 * to copy the remainder
						 * of the packet from the
						 * latter descriptor buffers
						 * to the first skb. Then
						 * dispose of the source
						 * skbs.
						 *
						 * Once the stack is fixed
						 * to handle frags on most
						 * protocols we can generate
						 * a fragmented skb with
						 * no copies.
						 */
						memcpy(fep->rx_skb[buf[0]]->
						       data +
						       fep->rx_skb[buf[0]]->len,
						       fep->rx_skb[buf[b]]->
						       data,
						       fep->rx_desc[buf[b]].
						       data_len);
						skb_put(fep->rx_skb[buf[0]],
							fep->rx_desc[buf[b]].
							data_len);
						dma_unmap_single(&fep->ocpdev->
								 dev,
								 fep->
								 rx_desc[buf
									 [b]].
								 data_ptr,
								 fep->
								 rx_desc[buf
									 [b]].
								 data_len,
								 DMA_FROM_DEVICE);
						dev_kfree_skb(fep->
							      rx_skb[buf[b]]);
					}
					emac_rx_csum(dev, ctrl,
						     fep->rx_skb[buf[0]]);

					fep->rx_skb[buf[0]]->dev = dev;
					fep->rx_skb[buf[0]]->protocol =
					    eth_type_trans(fep->rx_skb[buf[0]],
							   dev);
					error = netif_rx(fep->rx_skb[buf[0]]);

					if ((error == NET_RX_DROP)
					    || (error == NET_RX_BAD)) {
						fep->stats.rx_dropped++;
					} else {
						fep->stats.rx_packets++;
						fep->stats.rx_bytes +=
						    fep->rx_skb[buf[0]]->len;
					}
					for (b = 0; b < bnum; b++)
						fep->rx_skb[buf[b]] = NULL;
				}
			}
		}
	} while ((i = (i + 1) % NUM_RX_BUFF) != fep->rx_slot);

	PKT_DEBUG(("emac_rx_clean() exit, rx_slot: %d\n", fep->rx_slot));

	return i;
}

static void emac_rxeob_dev(void *param, u32 chanmask)
{
	struct net_device *dev = param;
	struct ocp_enet_private *fep = dev->priv;
	unsigned long flags;
	int n;

	spin_lock_irqsave(&fep->lock, flags);
	if ((n = emac_rx_clean(dev)) != fep->rx_slot)
		emac_rx_fill(dev, n);
	spin_unlock_irqrestore(&fep->lock, flags);
}

/*
 * This interrupt should never occurr, we don't program
 * the MAL for contiunous mode.
 */
static void emac_txde_dev(void *param, u32 chanmask)
{
	struct net_device *dev = param;
	struct ocp_enet_private *fep = dev->priv;

	printk(KERN_WARNING "%s: transmit descriptor error\n", dev->name);

	emac_mac_dump(dev);
	emac_mal_dump(dev);

	/* Reenable the transmit channel */
	mal_enable_tx_channels(fep->mal, fep->commac.tx_chan_mask);
}

/*
 * This interrupt should be very rare at best.  This occurs when
 * the hardware has a problem with the receive descriptors.  The manual
 * states that it occurs when the hardware cannot the receive descriptor
 * empty bit is not set.  The recovery mechanism will be to
 * traverse through the descriptors, handle any that are marked to be
 * handled and reinitialize each along the way.  At that point the driver
 * will be restarted.
 */
static void emac_rxde_dev(void *param, u32 chanmask)
{
	struct net_device *dev = param;
	struct ocp_enet_private *fep = dev->priv;
	unsigned long flags;

	if (net_ratelimit()) {
		printk(KERN_WARNING "%s: receive descriptor error\n",
		       fep->ndev->name);

		emac_mac_dump(dev);
		emac_mal_dump(dev);
		emac_desc_dump(dev);
	}

	/* Disable RX channel */
	spin_lock_irqsave(&fep->lock, flags);
	mal_disable_rx_channels(fep->mal, fep->commac.rx_chan_mask);

	/* For now, charge the error against all emacs */
	fep->stats.rx_errors++;

	/* so do we have any good packets still? */
	emac_rx_clean(dev);

	/* When the interface is restarted it resets processing to the
	 *  first descriptor in the table.
	 */

	fep->rx_slot = 0;
	emac_rx_fill(dev, 0);

	set_mal_dcrn(fep->mal, DCRN_MALRXEOBISR, fep->commac.rx_chan_mask);
	set_mal_dcrn(fep->mal, DCRN_MALRXDEIR, fep->commac.rx_chan_mask);

	/* Reenable the receive channels */
	mal_enable_rx_channels(fep->mal, fep->commac.rx_chan_mask);
	spin_unlock_irqrestore(&fep->lock, flags);
}

static irqreturn_t
emac_mac_irq(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct net_device *dev = dev_instance;
	struct ocp_enet_private *fep = dev->priv;
	emac_t *emacp = fep->emacp;
	unsigned long tmp_em0isr;

	/* EMAC interrupt */
	tmp_em0isr = in_be32(&emacp->em0isr);
	if (tmp_em0isr & (EMAC_ISR_TE0 | EMAC_ISR_TE1)) {
		/* This error is a hard transmit error - could retransmit */
		fep->stats.tx_errors++;

		/* Reenable the transmit channel */
		mal_enable_tx_channels(fep->mal, fep->commac.tx_chan_mask);

	} else {
		fep->stats.rx_errors++;
	}

	if (tmp_em0isr & EMAC_ISR_RP)
		fep->stats.rx_length_errors++;
	if (tmp_em0isr & EMAC_ISR_ALE)
		fep->stats.rx_frame_errors++;
	if (tmp_em0isr & EMAC_ISR_BFCS)
		fep->stats.rx_crc_errors++;
	if (tmp_em0isr & EMAC_ISR_PTLE)
		fep->stats.rx_length_errors++;
	if (tmp_em0isr & EMAC_ISR_ORE)
		fep->stats.rx_length_errors++;
	if (tmp_em0isr & EMAC_ISR_TE0)
		fep->stats.tx_aborted_errors++;

	emac_err_dump(dev, tmp_em0isr);

	out_be32(&emacp->em0isr, tmp_em0isr);

	return IRQ_HANDLED;
}

static int emac_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned short ctrl;
	unsigned long flags;
	struct ocp_enet_private *fep = dev->priv;
	emac_t *emacp = fep->emacp;
	int len = skb->len;
	unsigned int offset = 0, size, f, tx_slot_first;
	unsigned int nr_frags = skb_shinfo(skb)->nr_frags;

	spin_lock_irqsave(&fep->lock, flags);

	len -= skb->data_len;

	if ((fep->tx_cnt + nr_frags + len / DESC_BUF_SIZE + 1) > NUM_TX_BUFF) {
		PKT_DEBUG(("emac_start_xmit() stopping queue\n"));
		netif_stop_queue(dev);
		spin_unlock_irqrestore(&fep->lock, flags);
		return -EBUSY;
	}

	tx_slot_first = fep->tx_slot;

	while (len) {
		size = min(len, DESC_BUF_SIZE);

		fep->tx_desc[fep->tx_slot].data_len = (short)size;
		fep->tx_desc[fep->tx_slot].data_ptr =
		    (unsigned char *)dma_map_single(&fep->ocpdev->dev,
						    (void *)((unsigned int)skb->
							     data + offset),
						    size, DMA_TO_DEVICE);

		ctrl = EMAC_TX_CTRL_DFLT;
		if (fep->tx_slot != tx_slot_first)
			ctrl |= MAL_TX_CTRL_READY;
		if ((NUM_TX_BUFF - 1) == fep->tx_slot)
			ctrl |= MAL_TX_CTRL_WRAP;
		if (!nr_frags && (len == size)) {
			ctrl |= MAL_TX_CTRL_LAST;
			fep->tx_skb[fep->tx_slot] = skb;
		}
		if (skb->ip_summed == CHECKSUM_HW)
			ctrl |= EMAC_TX_CTRL_TAH_CSUM;

		fep->tx_desc[fep->tx_slot].ctrl = ctrl;

		len -= size;
		offset += size;

		/* Bump tx count */
		if (++fep->tx_cnt == NUM_TX_BUFF)
			netif_stop_queue(dev);

		/* Next descriptor */
		if (++fep->tx_slot == NUM_TX_BUFF)
			fep->tx_slot = 0;
	}

	for (f = 0; f < nr_frags; f++) {
		struct skb_frag_struct *frag;

		frag = &skb_shinfo(skb)->frags[f];
		len = frag->size;
		offset = 0;

		while (len) {
			size = min(len, DESC_BUF_SIZE);

			dma_map_page(&fep->ocpdev->dev,
				     frag->page,
				     frag->page_offset + offset,
				     size, DMA_TO_DEVICE);

			ctrl = EMAC_TX_CTRL_DFLT | MAL_TX_CTRL_READY;
			if ((NUM_TX_BUFF - 1) == fep->tx_slot)
				ctrl |= MAL_TX_CTRL_WRAP;
			if ((f == (nr_frags - 1)) && (len == size)) {
				ctrl |= MAL_TX_CTRL_LAST;
				fep->tx_skb[fep->tx_slot] = skb;
			}

			if (skb->ip_summed == CHECKSUM_HW)
				ctrl |= EMAC_TX_CTRL_TAH_CSUM;

			fep->tx_desc[fep->tx_slot].data_len = (short)size;
			fep->tx_desc[fep->tx_slot].data_ptr =
			    (char *)((page_to_pfn(frag->page) << PAGE_SHIFT) +
				     frag->page_offset + offset);
			fep->tx_desc[fep->tx_slot].ctrl = ctrl;

			len -= size;
			offset += size;

			/* Bump tx count */
			if (++fep->tx_cnt == NUM_TX_BUFF)
				netif_stop_queue(dev);

			/* Next descriptor */
			if (++fep->tx_slot == NUM_TX_BUFF)
				fep->tx_slot = 0;
		}
	}

	/*
	 * Deferred set READY on first descriptor of packet to
	 * avoid TX MAL race.
	 */
	fep->tx_desc[tx_slot_first].ctrl |= MAL_TX_CTRL_READY;

	/* Send the packet out. */
	out_be32(&emacp->em0tmr0, EMAC_TMR0_XMIT);

	fep->stats.tx_packets++;
	fep->stats.tx_bytes += skb->len;

	PKT_DEBUG(("emac_start_xmit() exitn"));

	spin_unlock_irqrestore(&fep->lock, flags);

	return 0;
}

static int emac_adjust_to_link(struct ocp_enet_private *fep)
{
	emac_t *emacp = fep->emacp;
	unsigned long mode_reg;
	int full_duplex, speed;

	full_duplex = 0;
	speed = SPEED_10;

	/* set mode register 1 defaults */
	mode_reg = EMAC_M1_DEFAULT;

	/* Read link mode on PHY */
	if (fep->phy_mii.def->ops->read_link(&fep->phy_mii) == 0) {
		/* If an error occurred, we don't deal with it yet */
		full_duplex = (fep->phy_mii.duplex == DUPLEX_FULL);
		speed = fep->phy_mii.speed;
	}


	/* set speed (default is 10Mb) */
	switch (speed) {
	case SPEED_1000:
		mode_reg |= EMAC_M1_RFS_16K;
		if (fep->rgmii_dev) {
			struct ibm_ocp_rgmii *rgmii = RGMII_PRIV(fep->rgmii_dev);

			if ((rgmii->mode[fep->rgmii_input] == RTBI)
			    || (rgmii->mode[fep->rgmii_input] == TBI))
				mode_reg |= EMAC_M1_MF_1000GPCS;
			else
				mode_reg |= EMAC_M1_MF_1000MBPS;

			emac_rgmii_port_speed(fep->rgmii_dev, fep->rgmii_input,
					      1000);
		}
		break;
	case SPEED_100:
		mode_reg |= EMAC_M1_MF_100MBPS | EMAC_M1_RFS_4K;
		if (fep->rgmii_dev)
			emac_rgmii_port_speed(fep->rgmii_dev, fep->rgmii_input,
					      100);
		if (fep->zmii_dev)
			emac_zmii_port_speed(fep->zmii_dev, fep->zmii_input,
					     100);
		break;
	case SPEED_10:
	default:
		mode_reg = (mode_reg & ~EMAC_M1_MF_100MBPS) | EMAC_M1_RFS_4K;
		if (fep->rgmii_dev)
			emac_rgmii_port_speed(fep->rgmii_dev, fep->rgmii_input,
					      10);
		if (fep->zmii_dev)
			emac_zmii_port_speed(fep->zmii_dev, fep->zmii_input,
					     10);
	}

	if (full_duplex)
		mode_reg |= EMAC_M1_FDE | EMAC_M1_EIFC | EMAC_M1_IST;
	else
		mode_reg &= ~(EMAC_M1_FDE | EMAC_M1_EIFC | EMAC_M1_ILE);

	LINK_DEBUG(("%s: adjust to link, speed: %d, duplex: %d, opened: %d\n",
		    fep->ndev->name, speed, full_duplex, fep->opened));

	printk(KERN_INFO "%s: Speed: %d, %s duplex.\n",
	       fep->ndev->name, speed, full_duplex ? "Full" : "Half");
	if (fep->opened)
		out_be32(&emacp->em0mr1, mode_reg);

	return 0;
}

static int emac_set_mac_address(struct net_device *ndev, void *p)
{
	struct ocp_enet_private *fep = ndev->priv;
	emac_t *emacp = fep->emacp;
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(ndev->dev_addr, addr->sa_data, ndev->addr_len);

	/* set the high address */
	out_be32(&emacp->em0iahr,
		 (fep->ndev->dev_addr[0] << 8) | fep->ndev->dev_addr[1]);

	/* set the low address */
	out_be32(&emacp->em0ialr,
		 (fep->ndev->dev_addr[2] << 24) | (fep->ndev->dev_addr[3] << 16)
		 | (fep->ndev->dev_addr[4] << 8) | fep->ndev->dev_addr[5]);

	return 0;
}

static int emac_change_mtu(struct net_device *dev, int new_mtu)
{
	struct ocp_enet_private *fep = dev->priv;
	int old_mtu = dev->mtu;
	unsigned long mode_reg;
	emac_t *emacp = fep->emacp;
	u32 em0mr0;
	int i, full;
	unsigned long flags;

	if ((new_mtu < EMAC_MIN_MTU) || (new_mtu > EMAC_MAX_MTU)) {
		printk(KERN_ERR
		       "emac: Invalid MTU setting, MTU must be between %d and %d\n",
		       EMAC_MIN_MTU, EMAC_MAX_MTU);
		return -EINVAL;
	}

	if (old_mtu != new_mtu && netif_running(dev)) {
		/* Stop rx engine */
		em0mr0 = in_be32(&emacp->em0mr0);
		out_be32(&emacp->em0mr0, em0mr0 & ~EMAC_M0_RXE);

		/* Wait for descriptors to be empty */
		do {
			full = 0;
			for (i = 0; i < NUM_RX_BUFF; i++)
				if (!(fep->rx_desc[i].ctrl & MAL_RX_CTRL_EMPTY)) {
					printk(KERN_NOTICE
					       "emac: RX ring is still full\n");
					full = 1;
				}
		} while (full);

		spin_lock_irqsave(&fep->lock, flags);

		mal_disable_rx_channels(fep->mal, fep->commac.rx_chan_mask);

		/* Destroy all old rx skbs */
		for (i = 0; i < NUM_RX_BUFF; i++) {
			dma_unmap_single(&fep->ocpdev->dev,
					 fep->rx_desc[i].data_ptr,
					 fep->rx_desc[i].data_len,
					 DMA_FROM_DEVICE);
			dev_kfree_skb(fep->rx_skb[i]);
			fep->rx_skb[i] = NULL;
		}

		/* Set new rx_buffer_size, jumbo cap, and advertise new mtu */
		mode_reg = in_be32(&emacp->em0mr1);
		if (new_mtu > ENET_DEF_MTU_SIZE) {
			mode_reg |= EMAC_M1_JUMBO_ENABLE;
			fep->rx_buffer_size = EMAC_MAX_FRAME;
		} else {
			mode_reg &= ~EMAC_M1_JUMBO_ENABLE;
			fep->rx_buffer_size = ENET_DEF_BUF_SIZE;
		}
		dev->mtu = new_mtu;
		out_be32(&emacp->em0mr1, mode_reg);

		/* Re-init rx skbs */
		fep->rx_slot = 0;
		emac_rx_fill(dev, 0);

		/* Restart the rx engine */
		mal_enable_rx_channels(fep->mal, fep->commac.rx_chan_mask);
		out_be32(&emacp->em0mr0, em0mr0 | EMAC_M0_RXE);

		spin_unlock_irqrestore(&fep->lock, flags);
	}

	return 0;
}

static void __emac_set_multicast_list(struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	emac_t *emacp = fep->emacp;
	u32 rmr = in_be32(&emacp->em0rmr);

	/* First clear all special bits, they can be set later */
	rmr &= ~(EMAC_RMR_PME | EMAC_RMR_PMME | EMAC_RMR_MAE);

	if (dev->flags & IFF_PROMISC) {
		rmr |= EMAC_RMR_PME;
	} else if (dev->flags & IFF_ALLMULTI || 32 < dev->mc_count) {
		/*
		 * Must be setting up to use multicast
		 * Now check for promiscuous multicast
		 */
		rmr |= EMAC_RMR_PMME;
	} else if (dev->flags & IFF_MULTICAST && 0 < dev->mc_count) {
		unsigned short em0gaht[4] = { 0, 0, 0, 0 };
		struct dev_mc_list *dmi;

		/* Need to hash on the multicast address. */
		for (dmi = dev->mc_list; dmi; dmi = dmi->next) {
			unsigned long mc_crc;
			unsigned int bit_number;

			mc_crc = ether_crc(6, (char *)dmi->dmi_addr);
			bit_number = 63 - (mc_crc >> 26);	/* MSB: 0 LSB: 63 */
			em0gaht[bit_number >> 4] |=
			    0x8000 >> (bit_number & 0x0f);
		}
		emacp->em0gaht1 = em0gaht[0];
		emacp->em0gaht2 = em0gaht[1];
		emacp->em0gaht3 = em0gaht[2];
		emacp->em0gaht4 = em0gaht[3];

		/* Turn on multicast addressing */
		rmr |= EMAC_RMR_MAE;
	}
	out_be32(&emacp->em0rmr, rmr);
}

static int emac_init_tah(struct ocp_enet_private *fep)
{
	tah_t *tahp;

	/* Initialize TAH and enable checksum verification */
	tahp = (tah_t *) ioremap(fep->tah_dev->def->paddr, sizeof(*tahp));

	if (tahp == NULL) {
		printk(KERN_ERR "tah%d: Cannot ioremap TAH registers!\n",
		       fep->tah_dev->def->index);

		return -ENOMEM;
	}

	out_be32(&tahp->tah_mr, TAH_MR_SR);

	/* wait for reset to complete */
	while (in_be32(&tahp->tah_mr) & TAH_MR_SR) ;

	/* 10KB TAH TX FIFO accomodates the max MTU of 9000 */
	out_be32(&tahp->tah_mr,
		 TAH_MR_CVR | TAH_MR_ST_768 | TAH_MR_TFS_10KB | TAH_MR_DTFP |
		 TAH_MR_DIG);

	iounmap(tahp);

	return 0;
}

static void emac_init_rings(struct net_device *dev)
{
	struct ocp_enet_private *ep = dev->priv;
	int loop;

	ep->tx_desc = (struct mal_descriptor *)((char *)ep->mal->tx_virt_addr +
						(ep->mal_tx_chan *
						 MAL_DT_ALIGN));
	ep->rx_desc =
	    (struct mal_descriptor *)((char *)ep->mal->rx_virt_addr +
				      (ep->mal_rx_chan * MAL_DT_ALIGN));

	/* Fill in the transmit descriptor ring. */
	for (loop = 0; loop < NUM_TX_BUFF; loop++) {
		if (ep->tx_skb[loop]) {
			dma_unmap_single(&ep->ocpdev->dev,
					 ep->tx_desc[loop].data_ptr,
					 ep->tx_desc[loop].data_len,
					 DMA_TO_DEVICE);
			dev_kfree_skb_irq(ep->tx_skb[loop]);
		}
		ep->tx_skb[loop] = NULL;
		ep->tx_desc[loop].ctrl = 0;
		ep->tx_desc[loop].data_len = 0;
		ep->tx_desc[loop].data_ptr = NULL;
	}
	ep->tx_desc[loop - 1].ctrl |= MAL_TX_CTRL_WRAP;

	/* Format the receive descriptor ring. */
	ep->rx_slot = 0;
	/* Default is MTU=1500 + Ethernet overhead */
	ep->rx_buffer_size = dev->mtu + ENET_HEADER_SIZE + ENET_FCS_SIZE;
	emac_rx_fill(dev, 0);
	if (ep->rx_slot != 0) {
		printk(KERN_ERR
		       "%s: Not enough mem for RxChain durning Open?\n",
		       dev->name);
		/*We couldn't fill the ring at startup?
		 *We could clean up and fail to open but right now we will try to
		 *carry on. It may be a sign of a bad NUM_RX_BUFF value
		 */
	}

	ep->tx_cnt = 0;
	ep->tx_slot = 0;
	ep->ack_slot = 0;
}

static void emac_reset_configure(struct ocp_enet_private *fep)
{
	emac_t *emacp = fep->emacp;
	int i;

	mal_disable_tx_channels(fep->mal, fep->commac.tx_chan_mask);
	mal_disable_rx_channels(fep->mal, fep->commac.rx_chan_mask);

	/*
	 * Check for a link, some PHYs don't provide a clock if
	 * no link is present.  Some EMACs will not come out of
	 * soft reset without a PHY clock present.
	 */
	if (fep->phy_mii.def->ops->poll_link(&fep->phy_mii)) {
		/* Reset the EMAC */
		out_be32(&emacp->em0mr0, EMAC_M0_SRST);
		udelay(20);
		for (i = 0; i < 100; i++) {
			if ((in_be32(&emacp->em0mr0) & EMAC_M0_SRST) == 0)
				break;
			udelay(10);
		}

		if (i >= 100) {
			printk(KERN_ERR "%s: Cannot reset EMAC\n",
			       fep->ndev->name);
			return;
		}
	}

	/* Switch IRQs off for now */
	out_be32(&emacp->em0iser, 0);

	/* Configure MAL rx channel */
	mal_set_rcbs(fep->mal, fep->mal_rx_chan, DESC_BUF_SIZE_REG);

	/* set the high address */
	out_be32(&emacp->em0iahr,
		 (fep->ndev->dev_addr[0] << 8) | fep->ndev->dev_addr[1]);

	/* set the low address */
	out_be32(&emacp->em0ialr,
		 (fep->ndev->dev_addr[2] << 24) | (fep->ndev->dev_addr[3] << 16)
		 | (fep->ndev->dev_addr[4] << 8) | fep->ndev->dev_addr[5]);

	/* Adjust to link */
	if (netif_carrier_ok(fep->ndev))
		emac_adjust_to_link(fep);

	/* enable broadcast/individual address and RX FIFO defaults */
	out_be32(&emacp->em0rmr, EMAC_RMR_DEFAULT);

	/* set transmit request threshold register */
	out_be32(&emacp->em0trtr, EMAC_TRTR_DEFAULT);

	/* Reconfigure multicast */
	__emac_set_multicast_list(fep->ndev);

	/* Set receiver/transmitter defaults */
	out_be32(&emacp->em0rwmr, EMAC_RWMR_DEFAULT);
	out_be32(&emacp->em0tmr0, EMAC_TMR0_DEFAULT);
	out_be32(&emacp->em0tmr1, EMAC_TMR1_DEFAULT);

	/* set frame gap */
	out_be32(&emacp->em0ipgvr, CONFIG_IBM_EMAC_FGAP);
	
	/* set VLAN Tag Protocol Identifier */
	out_be32(&emacp->em0vtpid, 0x8100);

	/* Init ring buffers */
	emac_init_rings(fep->ndev);
}

static void emac_kick(struct ocp_enet_private *fep)
{
	emac_t *emacp = fep->emacp;
	unsigned long emac_ier;

	emac_ier = EMAC_ISR_PP | EMAC_ISR_BP | EMAC_ISR_RP |
	    EMAC_ISR_SE | EMAC_ISR_PTLE | EMAC_ISR_ALE |
	    EMAC_ISR_BFCS | EMAC_ISR_ORE | EMAC_ISR_IRE;

	out_be32(&emacp->em0iser, emac_ier);

	/* enable all MAL transmit and receive channels */
	mal_enable_tx_channels(fep->mal, fep->commac.tx_chan_mask);
	mal_enable_rx_channels(fep->mal, fep->commac.rx_chan_mask);

	/* set transmit and receive enable */
	out_be32(&emacp->em0mr0, EMAC_M0_TXE | EMAC_M0_RXE);
}

static void
emac_start_link(struct ocp_enet_private *fep, struct ethtool_cmd *ep)
{
	u32 advertise;
	int autoneg;
	int forced_speed;
	int forced_duplex;

	/* Default advertise */
	advertise = ADVERTISED_10baseT_Half | ADVERTISED_10baseT_Full |
	    ADVERTISED_100baseT_Half | ADVERTISED_100baseT_Full |
	    ADVERTISED_1000baseT_Half | ADVERTISED_1000baseT_Full;
	autoneg = fep->want_autoneg;
	forced_speed = fep->phy_mii.speed;
	forced_duplex = fep->phy_mii.duplex;

	/* Setup link parameters */
	if (ep) {
		if (ep->autoneg == AUTONEG_ENABLE) {
			advertise = ep->advertising;
			autoneg = 1;
		} else {
			autoneg = 0;
			forced_speed = ep->speed;
			forced_duplex = ep->duplex;
		}
	}

	/* Configure PHY & start aneg */
	fep->want_autoneg = autoneg;
	if (autoneg) {
		LINK_DEBUG(("%s: start link aneg, advertise: 0x%x\n",
			    fep->ndev->name, advertise));
		fep->phy_mii.def->ops->setup_aneg(&fep->phy_mii, advertise);
	} else {
		LINK_DEBUG(("%s: start link forced, speed: %d, duplex: %d\n",
			    fep->ndev->name, forced_speed, forced_duplex));
		fep->phy_mii.def->ops->setup_forced(&fep->phy_mii, forced_speed,
						    forced_duplex);
	}
	fep->timer_ticks = 0;
	mod_timer(&fep->link_timer, jiffies + HZ);
}

static void emac_link_timer(unsigned long data)
{
	struct ocp_enet_private *fep = (struct ocp_enet_private *)data;
	int link;

	if (fep->going_away)
		return;

	spin_lock_irq(&fep->lock);

	link = fep->phy_mii.def->ops->poll_link(&fep->phy_mii);
	LINK_DEBUG(("%s: poll_link: %d\n", fep->ndev->name, link));

	if (link == netif_carrier_ok(fep->ndev)) {
		if (!link && fep->want_autoneg && (++fep->timer_ticks) > 10)
			emac_start_link(fep, NULL);
		goto out;
	}
	printk(KERN_INFO "%s: Link is %s\n", fep->ndev->name,
	       link ? "Up" : "Down");
	if (link) {
		netif_carrier_on(fep->ndev);
		/* Chip needs a full reset on config change. That sucks, so I
		 * should ultimately move that to some tasklet to limit
		 * latency peaks caused by this code
		 */
		emac_reset_configure(fep);
		if (fep->opened)
			emac_kick(fep);
	} else {
		fep->timer_ticks = 0;
		netif_carrier_off(fep->ndev);
	}
      out:
	mod_timer(&fep->link_timer, jiffies + HZ);
	spin_unlock_irq(&fep->lock);
}

static void emac_set_multicast_list(struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;

	spin_lock_irq(&fep->lock);
	__emac_set_multicast_list(dev);
	spin_unlock_irq(&fep->lock);
}

static int emac_get_settings(struct net_device *ndev, struct ethtool_cmd *cmd)
{
	struct ocp_enet_private *fep = ndev->priv;

	cmd->supported = fep->phy_mii.def->features;
	cmd->port = PORT_MII;
	cmd->transceiver = XCVR_EXTERNAL;
	cmd->phy_address = fep->mii_phy_addr;
	spin_lock_irq(&fep->lock);
	cmd->autoneg = fep->want_autoneg;
	cmd->speed = fep->phy_mii.speed;
	cmd->duplex = fep->phy_mii.duplex;
	spin_unlock_irq(&fep->lock);
	return 0;
}

static int emac_set_settings(struct net_device *ndev, struct ethtool_cmd *cmd)
{
	struct ocp_enet_private *fep = ndev->priv;
	unsigned long features = fep->phy_mii.def->features;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (cmd->autoneg != AUTONEG_ENABLE && cmd->autoneg != AUTONEG_DISABLE)
		return -EINVAL;
	if (cmd->autoneg == AUTONEG_ENABLE && cmd->advertising == 0)
		return -EINVAL;
	if (cmd->duplex != DUPLEX_HALF && cmd->duplex != DUPLEX_FULL)
		return -EINVAL;
	if (cmd->autoneg == AUTONEG_DISABLE)
		switch (cmd->speed) {
		case SPEED_10:
			if (cmd->duplex == DUPLEX_HALF &&
			    (features & SUPPORTED_10baseT_Half) == 0)
				return -EINVAL;
			if (cmd->duplex == DUPLEX_FULL &&
			    (features & SUPPORTED_10baseT_Full) == 0)
				return -EINVAL;
			break;
		case SPEED_100:
			if (cmd->duplex == DUPLEX_HALF &&
			    (features & SUPPORTED_100baseT_Half) == 0)
				return -EINVAL;
			if (cmd->duplex == DUPLEX_FULL &&
			    (features & SUPPORTED_100baseT_Full) == 0)
				return -EINVAL;
			break;
		case SPEED_1000:
			if (cmd->duplex == DUPLEX_HALF &&
			    (features & SUPPORTED_1000baseT_Half) == 0)
				return -EINVAL;
			if (cmd->duplex == DUPLEX_FULL &&
			    (features & SUPPORTED_1000baseT_Full) == 0)
				return -EINVAL;
			break;
		default:
			return -EINVAL;
	} else if ((features & SUPPORTED_Autoneg) == 0)
		return -EINVAL;
	spin_lock_irq(&fep->lock);
	emac_start_link(fep, cmd);
	spin_unlock_irq(&fep->lock);
	return 0;
}

static void
emac_get_drvinfo(struct net_device *ndev, struct ethtool_drvinfo *info)
{
	struct ocp_enet_private *fep = ndev->priv;

	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, DRV_VERSION);
	info->fw_version[0] = '\0';
	sprintf(info->bus_info, "IBM EMAC %d", fep->ocpdev->def->index);
	info->regdump_len = 0;
}

static int emac_nway_reset(struct net_device *ndev)
{
	struct ocp_enet_private *fep = ndev->priv;

	if (!fep->want_autoneg)
		return -EINVAL;
	spin_lock_irq(&fep->lock);
	emac_start_link(fep, NULL);
	spin_unlock_irq(&fep->lock);
	return 0;
}

static u32 emac_get_link(struct net_device *ndev)
{
	return netif_carrier_ok(ndev);
}

static struct ethtool_ops emac_ethtool_ops = {
	.get_settings = emac_get_settings,
	.set_settings = emac_set_settings,
	.get_drvinfo = emac_get_drvinfo,
	.nway_reset = emac_nway_reset,
	.get_link = emac_get_link
};

static int emac_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct ocp_enet_private *fep = dev->priv;
	uint16_t *data = (uint16_t *) & rq->ifr_ifru;

	switch (cmd) {
	case SIOCGMIIPHY:
		data[0] = fep->mii_phy_addr;
		/* Fall through */
	case SIOCGMIIREG:
		data[3] = emac_phy_read(dev, fep->mii_phy_addr, data[1]);
		return 0;
	case SIOCSMIIREG:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		emac_phy_write(dev, fep->mii_phy_addr, data[1], data[2]);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int emac_open(struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	int rc;

	spin_lock_irq(&fep->lock);

	fep->opened = 1;
	netif_carrier_off(dev);

	/* Reset & configure the chip */
	emac_reset_configure(fep);

	spin_unlock_irq(&fep->lock);

	/* Request our interrupt lines */
	rc = request_irq(dev->irq, emac_mac_irq, 0, "IBM EMAC MAC", dev);
	if (rc != 0) {
		printk("dev->irq %d failed\n", dev->irq);
		goto bail;
	}
	/* Kick the chip rx & tx channels into life */
	spin_lock_irq(&fep->lock);
	emac_kick(fep);
	spin_unlock_irq(&fep->lock);

	netif_start_queue(dev);
      bail:
	return rc;
}

static int emac_close(struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	emac_t *emacp = fep->emacp;

	/* XXX Stop IRQ emitting here */
	spin_lock_irq(&fep->lock);
	fep->opened = 0;
	mal_disable_tx_channels(fep->mal, fep->commac.tx_chan_mask);
	mal_disable_rx_channels(fep->mal, fep->commac.rx_chan_mask);
	netif_carrier_off(dev);
	netif_stop_queue(dev);

	/*
	 * Check for a link, some PHYs don't provide a clock if
	 * no link is present.  Some EMACs will not come out of
	 * soft reset without a PHY clock present.
	 */
	if (fep->phy_mii.def->ops->poll_link(&fep->phy_mii)) {
		out_be32(&emacp->em0mr0, EMAC_M0_SRST);
		udelay(10);

		if (emacp->em0mr0 & EMAC_M0_SRST) {
			/*not sure what to do here hopefully it clears before another open */
			printk(KERN_ERR
			       "%s: Phy SoftReset didn't clear, no link?\n",
			       dev->name);
		}
	}

	/* Free the irq's */
	free_irq(dev->irq, dev);

	spin_unlock_irq(&fep->lock);

	return 0;
}

static void emac_remove(struct ocp_device *ocpdev)
{
	struct net_device *dev = ocp_get_drvdata(ocpdev);
	struct ocp_enet_private *ep = dev->priv;

	/* FIXME: locking, races, ... */
	ep->going_away = 1;
	ocp_set_drvdata(ocpdev, NULL);
	if (ep->rgmii_dev)
		emac_close_rgmii(ep->rgmii_dev);
	if (ep->zmii_dev)
		emac_close_zmii(ep->zmii_dev);

	unregister_netdev(dev);
	del_timer_sync(&ep->link_timer);
	mal_unregister_commac(ep->mal, &ep->commac);
	iounmap((void *)ep->emacp);
	kfree(dev);
}

struct mal_commac_ops emac_commac_ops = {
	.txeob = &emac_txeob_dev,
	.txde = &emac_txde_dev,
	.rxeob = &emac_rxeob_dev,
	.rxde = &emac_rxde_dev,
};

#ifdef CONFIG_NET_POLL_CONTROLLER
static void emac_netpoll(struct net_device *ndev)
{
	emac_rxeob_dev((void *)ndev, 0);
	emac_txeob_dev((void *)ndev, 0);
}
#endif

static int emac_init_device(struct ocp_device *ocpdev, struct ibm_ocp_mal *mal)
{
	int deferred_init = 0;
	int rc = 0, i;
	struct net_device *ndev;
	struct ocp_enet_private *ep;
	struct ocp_func_emac_data *emacdata;
	int commac_reg = 0;
	u32 phy_map;

	emacdata = (struct ocp_func_emac_data *)ocpdev->def->additions;
	if (!emacdata) {
		printk(KERN_ERR "emac%d: Missing additional data!\n",
		       ocpdev->def->index);
		return -ENODEV;
	}

	/* Allocate our net_device structure */
	ndev = alloc_etherdev(sizeof(struct ocp_enet_private));
	if (ndev == NULL) {
		printk(KERN_ERR
		       "emac%d: Could not allocate ethernet device.\n",
		       ocpdev->def->index);
		return -ENOMEM;
	}
	ep = ndev->priv;
	ep->ndev = ndev;
	ep->ocpdev = ocpdev;
	ndev->irq = ocpdev->def->irq;
	ep->wol_irq = emacdata->wol_irq;
	if (emacdata->mdio_idx >= 0) {
		if (emacdata->mdio_idx == ocpdev->def->index) {
			/* Set the common MDIO net_device */
			mdio_ndev = ndev;
			deferred_init = 1;
		}
		ep->mdio_dev = mdio_ndev;
	} else {
		ep->mdio_dev = ndev;
	}

	ocp_set_drvdata(ocpdev, ndev);

	spin_lock_init(&ep->lock);

	/* Fill out MAL informations and register commac */
	ep->mal = mal;
	ep->mal_tx_chan = emacdata->mal_tx_chan;
	ep->mal_rx_chan = emacdata->mal_rx_chan;
	ep->commac.ops = &emac_commac_ops;
	ep->commac.dev = ndev;
	ep->commac.tx_chan_mask = MAL_CHAN_MASK(ep->mal_tx_chan);
	ep->commac.rx_chan_mask = MAL_CHAN_MASK(ep->mal_rx_chan);
	rc = mal_register_commac(ep->mal, &ep->commac);
	if (rc != 0)
		goto bail;
	commac_reg = 1;

	/* Map our MMIOs */
	ep->emacp = (emac_t *) ioremap(ocpdev->def->paddr, sizeof(emac_t));

	/* Check if we need to attach to a ZMII */
	if (emacdata->zmii_idx >= 0) {
		ep->zmii_input = emacdata->zmii_mux;
		ep->zmii_dev =
		    ocp_find_device(OCP_ANY_ID, OCP_FUNC_ZMII,
				    emacdata->zmii_idx);
		if (ep->zmii_dev == NULL)
			printk(KERN_WARNING
			       "emac%d: ZMII %d requested but not found !\n",
			       ocpdev->def->index, emacdata->zmii_idx);
		else if ((rc =
			  emac_init_zmii(ep->zmii_dev, ep->zmii_input,
					 emacdata->phy_mode)) != 0)
			goto bail;
	}

	/* Check if we need to attach to a RGMII */
	if (emacdata->rgmii_idx >= 0) {
		ep->rgmii_input = emacdata->rgmii_mux;
		ep->rgmii_dev =
		    ocp_find_device(OCP_ANY_ID, OCP_FUNC_RGMII,
				    emacdata->rgmii_idx);
		if (ep->rgmii_dev == NULL)
			printk(KERN_WARNING
			       "emac%d: RGMII %d requested but not found !\n",
			       ocpdev->def->index, emacdata->rgmii_idx);
		else if ((rc =
			  emac_init_rgmii(ep->rgmii_dev, ep->rgmii_input,
					  emacdata->phy_mode)) != 0)
			goto bail;
	}

	/* Check if we need to attach to a TAH */
	if (emacdata->tah_idx >= 0) {
		ep->tah_dev =
		    ocp_find_device(OCP_ANY_ID, OCP_FUNC_TAH,
				    emacdata->tah_idx);
		if (ep->tah_dev == NULL)
			printk(KERN_WARNING
			       "emac%d: TAH %d requested but not found !\n",
			       ocpdev->def->index, emacdata->tah_idx);
		else if ((rc = emac_init_tah(ep)) != 0)
			goto bail;
	}

	if (deferred_init) {
		if (!list_empty(&emac_init_list)) {
			struct list_head *entry;
			struct emac_def_dev *ddev;

			list_for_each(entry, &emac_init_list) {
				ddev =
				    list_entry(entry, struct emac_def_dev,
					       link);
				emac_init_device(ddev->ocpdev, ddev->mal);
			}
		}
	}

	/* Init link monitoring timer */
	init_timer(&ep->link_timer);
	ep->link_timer.function = emac_link_timer;
	ep->link_timer.data = (unsigned long)ep;
	ep->timer_ticks = 0;

	/* Fill up the mii_phy structure */
	ep->phy_mii.dev = ndev;
	ep->phy_mii.mdio_read = emac_phy_read;
	ep->phy_mii.mdio_write = emac_phy_write;
	ep->phy_mii.mode = emacdata->phy_mode;

	/* Find PHY */
	phy_map = emacdata->phy_map | busy_phy_map;
	for (i = 0; i <= 0x1f; i++, phy_map >>= 1) {
		if ((phy_map & 0x1) == 0) {
			int val = emac_phy_read(ndev, i, MII_BMCR);
			if (val != 0xffff && val != -1)
				break;
		}
	}
	if (i == 0x20) {
		printk(KERN_WARNING "emac%d: Can't find PHY.\n",
		       ocpdev->def->index);
		rc = -ENODEV;
		goto bail;
	}
	busy_phy_map |= 1 << i;
	ep->mii_phy_addr = i;
	rc = mii_phy_probe(&ep->phy_mii, i);
	if (rc) {
		printk(KERN_WARNING "emac%d: Failed to probe PHY type.\n",
		       ocpdev->def->index);
		rc = -ENODEV;
		goto bail;
	}
	
	/* Disable any PHY features not supported by the platform */
	ep->phy_mii.def->features &= ~emacdata->phy_feat_exc;

	/* Setup initial PHY config & startup aneg */
	if (ep->phy_mii.def->ops->init)
		ep->phy_mii.def->ops->init(&ep->phy_mii);
	netif_carrier_off(ndev);
	if (ep->phy_mii.def->features & SUPPORTED_Autoneg)
		ep->want_autoneg = 1;
	else {
		ep->want_autoneg = 0;
		
		/* Select highest supported speed/duplex */
		if (ep->phy_mii.def->features & SUPPORTED_1000baseT_Full) {
			ep->phy_mii.speed = SPEED_1000;
			ep->phy_mii.duplex = DUPLEX_FULL;
		} else if (ep->phy_mii.def->features & 
			   SUPPORTED_1000baseT_Half) {
			ep->phy_mii.speed = SPEED_1000;
			ep->phy_mii.duplex = DUPLEX_HALF;
		} else if (ep->phy_mii.def->features & 
			   SUPPORTED_100baseT_Full) {
			ep->phy_mii.speed = SPEED_100;
			ep->phy_mii.duplex = DUPLEX_FULL;
		} else if (ep->phy_mii.def->features & 
			   SUPPORTED_100baseT_Half) {
			ep->phy_mii.speed = SPEED_100;
			ep->phy_mii.duplex = DUPLEX_HALF;
		} else if (ep->phy_mii.def->features & 
			   SUPPORTED_10baseT_Full) {
			ep->phy_mii.speed = SPEED_10;
			ep->phy_mii.duplex = DUPLEX_FULL;
		} else {
			ep->phy_mii.speed = SPEED_10;
			ep->phy_mii.duplex = DUPLEX_HALF;
		}
	}
	emac_start_link(ep, NULL);

	/* read the MAC Address */
	for (i = 0; i < 6; i++)
		ndev->dev_addr[i] = emacdata->mac_addr[i];

	/* Fill in the driver function table */
	ndev->open = &emac_open;
	ndev->hard_start_xmit = &emac_start_xmit;
	ndev->stop = &emac_close;
	ndev->get_stats = &emac_stats;
	if (emacdata->jumbo)
		ndev->change_mtu = &emac_change_mtu;
	ndev->set_mac_address = &emac_set_mac_address;
	ndev->set_multicast_list = &emac_set_multicast_list;
	ndev->do_ioctl = &emac_ioctl;
	SET_ETHTOOL_OPS(ndev, &emac_ethtool_ops);
	if (emacdata->tah_idx >= 0)
		ndev->features = NETIF_F_IP_CSUM | NETIF_F_SG;
#ifdef CONFIG_NET_POLL_CONTROLLER
	ndev->poll_controller = emac_netpoll;
#endif

	SET_MODULE_OWNER(ndev);

	rc = register_netdev(ndev);
	if (rc != 0)
		goto bail;

	printk("%s: IBM emac, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
	       ndev->name,
	       ndev->dev_addr[0], ndev->dev_addr[1], ndev->dev_addr[2],
	       ndev->dev_addr[3], ndev->dev_addr[4], ndev->dev_addr[5]);
	printk(KERN_INFO "%s: Found %s PHY (0x%02x)\n",
	       ndev->name, ep->phy_mii.def->name, ep->mii_phy_addr);

      bail:
	if (rc && commac_reg)
		mal_unregister_commac(ep->mal, &ep->commac);
	if (rc && ndev)
		kfree(ndev);

	return rc;
}

static int emac_probe(struct ocp_device *ocpdev)
{
	struct ocp_device *maldev;
	struct ibm_ocp_mal *mal;
	struct ocp_func_emac_data *emacdata;

	emacdata = (struct ocp_func_emac_data *)ocpdev->def->additions;
	if (emacdata == NULL) {
		printk(KERN_ERR "emac%d: Missing additional datas !\n",
		       ocpdev->def->index);
		return -ENODEV;
	}

	/* Get the MAL device  */
	maldev = ocp_find_device(OCP_ANY_ID, OCP_FUNC_MAL, emacdata->mal_idx);
	if (maldev == NULL) {
		printk("No maldev\n");
		return -ENODEV;
	}
	/*
	 * Get MAL driver data, it must be here due to link order.
	 * When the driver is modularized, symbol dependencies will
	 * ensure the MAL driver is already present if built as a
	 * module.
	 */
	mal = (struct ibm_ocp_mal *)ocp_get_drvdata(maldev);
	if (mal == NULL) {
		printk("No maldrv\n");
		return -ENODEV;
	}

	/* If we depend on another EMAC for MDIO, wait for it to show up */
	if (emacdata->mdio_idx >= 0 &&
	    (emacdata->mdio_idx != ocpdev->def->index) && !mdio_ndev) {
		struct emac_def_dev *ddev;
		/* Add this index to the deferred init table */
		ddev = kmalloc(sizeof(struct emac_def_dev), GFP_KERNEL);
		ddev->ocpdev = ocpdev;
		ddev->mal = mal;
		list_add_tail(&ddev->link, &emac_init_list);
	} else {
		emac_init_device(ocpdev, mal);
	}

	return 0;
}

/* Structure for a device driver */
static struct ocp_device_id emac_ids[] = {
	{.vendor = OCP_ANY_ID,.function = OCP_FUNC_EMAC},
	{.vendor = OCP_VENDOR_INVALID}
};

static struct ocp_driver emac_driver = {
	.name = "emac",
	.id_table = emac_ids,

	.probe = emac_probe,
	.remove = emac_remove,
};

static int __init emac_init(void)
{
	printk(KERN_INFO DRV_NAME ": " DRV_DESC ", version " DRV_VERSION "\n");
	printk(KERN_INFO "Maintained by " DRV_AUTHOR "\n");

	if (skb_res > 2) {
		printk(KERN_WARNING "Invalid skb_res: %d, cropping to 2\n",
		       skb_res);
		skb_res = 2;
	}

	return ocp_register_driver(&emac_driver);
}

static void __exit emac_exit(void)
{
	ocp_unregister_driver(&emac_driver);
}

module_init(emac_init);
module_exit(emac_exit);
