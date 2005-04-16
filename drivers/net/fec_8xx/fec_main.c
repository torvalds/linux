/*
 * Fast Ethernet Controller (FEC) driver for Motorola MPC8xx.
 *
 * Copyright (c) 2003 Intracom S.A. 
 *  by Pantelis Antoniou <panto@intracom.gr>
 *
 * Heavily based on original FEC driver by Dan Malek <dan@embeddededge.com>
 * and modifications by Joakim Tjernlund <joakim.tjernlund@lumentis.se>
 *
 * Released under the GPL
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/bitops.h>

#include <asm/8xx_immap.h>
#include <asm/pgtable.h>
#include <asm/mpc8xx.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/commproc.h>
#include <asm/dma-mapping.h>

#include "fec_8xx.h"

/*************************************************/

#define FEC_MAX_MULTICAST_ADDRS	64

/*************************************************/

static char version[] __devinitdata =
    DRV_MODULE_NAME ".c:v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")" "\n";

MODULE_AUTHOR("Pantelis Antoniou <panto@intracom.gr>");
MODULE_DESCRIPTION("Motorola 8xx FEC ethernet driver");
MODULE_LICENSE("GPL");

MODULE_PARM(fec_8xx_debug, "i");
MODULE_PARM_DESC(fec_8xx_debug,
		 "FEC 8xx bitmapped debugging message enable value");

int fec_8xx_debug = -1;		/* -1 == use FEC_8XX_DEF_MSG_ENABLE as value */

/*************************************************/

/*
 * Delay to wait for FEC reset command to complete (in us) 
 */
#define FEC_RESET_DELAY		50

/*****************************************************************************************/

static void fec_whack_reset(fec_t * fecp)
{
	int i;

	/*
	 * Whack a reset.  We should wait for this.  
	 */
	FW(fecp, ecntrl, FEC_ECNTRL_PINMUX | FEC_ECNTRL_RESET);
	for (i = 0;
	     (FR(fecp, ecntrl) & FEC_ECNTRL_RESET) != 0 && i < FEC_RESET_DELAY;
	     i++)
		udelay(1);

	if (i == FEC_RESET_DELAY)
		printk(KERN_WARNING "FEC Reset timeout!\n");

}

/****************************************************************************/

/*
 * Transmitter timeout.  
 */
#define TX_TIMEOUT (2*HZ)

/****************************************************************************/

/*
 * Returns the CRC needed when filling in the hash table for
 * multicast group filtering
 * pAddr must point to a MAC address (6 bytes)
 */
static __u32 fec_mulicast_calc_crc(char *pAddr)
{
	u8 byte;
	int byte_count;
	int bit_count;
	__u32 crc = 0xffffffff;
	u8 msb;

	for (byte_count = 0; byte_count < 6; byte_count++) {
		byte = pAddr[byte_count];
		for (bit_count = 0; bit_count < 8; bit_count++) {
			msb = crc >> 31;
			crc <<= 1;
			if (msb ^ (byte & 0x1)) {
				crc ^= FEC_CRC_POLY;
			}
			byte >>= 1;
		}
	}
	return (crc);
}

/*
 * Set or clear the multicast filter for this adaptor.
 * Skeleton taken from sunlance driver.
 * The CPM Ethernet implementation allows Multicast as well as individual
 * MAC address filtering.  Some of the drivers check to make sure it is
 * a group multicast address, and discard those that are not.  I guess I
 * will do the same for now, but just remove the test if you want
 * individual filtering as well (do the upper net layers want or support
 * this kind of feature?).
 */
static void fec_set_multicast_list(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	fec_t *fecp = fep->fecp;
	struct dev_mc_list *pmc;
	__u32 crc;
	int temp;
	__u32 csrVal;
	int hash_index;
	__u32 hthi, htlo;
	unsigned long flags;


	if ((dev->flags & IFF_PROMISC) != 0) {

		spin_lock_irqsave(&fep->lock, flags);
		FS(fecp, r_cntrl, FEC_RCNTRL_PROM);
		spin_unlock_irqrestore(&fep->lock, flags);

		/*
		 * Log any net taps. 
		 */
		printk(KERN_WARNING DRV_MODULE_NAME
		       ": %s: Promiscuous mode enabled.\n", dev->name);
		return;

	}

	if ((dev->flags & IFF_ALLMULTI) != 0 ||
	    dev->mc_count > FEC_MAX_MULTICAST_ADDRS) {
		/*
		 * Catch all multicast addresses, set the filter to all 1's.
		 */
		hthi = 0xffffffffU;
		htlo = 0xffffffffU;
	} else {
		hthi = 0;
		htlo = 0;

		/*
		 * Now populate the hash table 
		 */
		for (pmc = dev->mc_list; pmc != NULL; pmc = pmc->next) {
			crc = fec_mulicast_calc_crc(pmc->dmi_addr);
			temp = (crc & 0x3f) >> 1;
			hash_index = ((temp & 0x01) << 4) |
				     ((temp & 0x02) << 2) |
				     ((temp & 0x04)) |
				     ((temp & 0x08) >> 2) |
				     ((temp & 0x10) >> 4);
			csrVal = (1 << hash_index);
			if (crc & 1)
				hthi |= csrVal;
			else
				htlo |= csrVal;
		}
	}

	spin_lock_irqsave(&fep->lock, flags);
	FC(fecp, r_cntrl, FEC_RCNTRL_PROM);
	FW(fecp, hash_table_high, hthi);
	FW(fecp, hash_table_low, htlo);
	spin_unlock_irqrestore(&fep->lock, flags);
}

static int fec_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *mac = addr;
	struct fec_enet_private *fep = netdev_priv(dev);
	struct fec *fecp = fep->fecp;
	int i;
	__u32 addrhi, addrlo;
	unsigned long flags;

	/* Get pointer to SCC area in parameter RAM. */
	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = mac->sa_data[i];

	/*
	 * Set station address. 
	 */
	addrhi = ((__u32) dev->dev_addr[0] << 24) |
		 ((__u32) dev->dev_addr[1] << 16) |
	   	 ((__u32) dev->dev_addr[2] <<  8) |
	    	  (__u32) dev->dev_addr[3];
	addrlo = ((__u32) dev->dev_addr[4] << 24) |
	    	 ((__u32) dev->dev_addr[5] << 16);

	spin_lock_irqsave(&fep->lock, flags);
	FW(fecp, addr_low, addrhi);
	FW(fecp, addr_high, addrlo);
	spin_unlock_irqrestore(&fep->lock, flags);

	return 0;
}

/*
 * This function is called to start or restart the FEC during a link
 * change.  This only happens when switching between half and full
 * duplex.
 */
void fec_restart(struct net_device *dev, int duplex, int speed)
{
#ifdef CONFIG_DUET
	immap_t *immap = (immap_t *) IMAP_ADDR;
	__u32 cptr;
#endif
	struct fec_enet_private *fep = netdev_priv(dev);
	struct fec *fecp = fep->fecp;
	const struct fec_platform_info *fpi = fep->fpi;
	cbd_t *bdp;
	struct sk_buff *skb;
	int i;
	__u32 addrhi, addrlo;

	fec_whack_reset(fep->fecp);

	/*
	 * Set station address. 
	 */
	addrhi = ((__u32) dev->dev_addr[0] << 24) |
		 ((__u32) dev->dev_addr[1] << 16) |
		 ((__u32) dev->dev_addr[2] <<  8) |
		 (__u32) dev->dev_addr[3];
	addrlo = ((__u32) dev->dev_addr[4] << 24) |
		 ((__u32) dev->dev_addr[5] << 16);
	FW(fecp, addr_low, addrhi);
	FW(fecp, addr_high, addrlo);

	/*
	 * Reset all multicast. 
	 */
	FW(fecp, hash_table_high, 0);
	FW(fecp, hash_table_low, 0);

	/*
	 * Set maximum receive buffer size. 
	 */
	FW(fecp, r_buff_size, PKT_MAXBLR_SIZE);
	FW(fecp, r_hash, PKT_MAXBUF_SIZE);

	/*
	 * Set receive and transmit descriptor base. 
	 */
	FW(fecp, r_des_start, iopa((__u32) (fep->rx_bd_base)));
	FW(fecp, x_des_start, iopa((__u32) (fep->tx_bd_base)));

	fep->dirty_tx = fep->cur_tx = fep->tx_bd_base;
	fep->tx_free = fep->tx_ring;
	fep->cur_rx = fep->rx_bd_base;

	/*
	 * Reset SKB receive buffers 
	 */
	for (i = 0; i < fep->rx_ring; i++) {
		if ((skb = fep->rx_skbuff[i]) == NULL)
			continue;
		fep->rx_skbuff[i] = NULL;
		dev_kfree_skb(skb);
	}

	/*
	 * Initialize the receive buffer descriptors. 
	 */
	for (i = 0, bdp = fep->rx_bd_base; i < fep->rx_ring; i++, bdp++) {
		skb = dev_alloc_skb(ENET_RX_FRSIZE);
		if (skb == NULL) {
			printk(KERN_WARNING DRV_MODULE_NAME
			       ": %s Memory squeeze, unable to allocate skb\n",
			       dev->name);
			fep->stats.rx_dropped++;
			break;
		}
		fep->rx_skbuff[i] = skb;
		skb->dev = dev;
		CBDW_BUFADDR(bdp, dma_map_single(NULL, skb->data,
					 L1_CACHE_ALIGN(PKT_MAXBUF_SIZE),
					 DMA_FROM_DEVICE));
		CBDW_DATLEN(bdp, 0);	/* zero */
		CBDW_SC(bdp, BD_ENET_RX_EMPTY |
			((i < fep->rx_ring - 1) ? 0 : BD_SC_WRAP));
	}
	/*
	 * if we failed, fillup remainder 
	 */
	for (; i < fep->rx_ring; i++, bdp++) {
		fep->rx_skbuff[i] = NULL;
		CBDW_SC(bdp, (i < fep->rx_ring - 1) ? 0 : BD_SC_WRAP);
	}

	/*
	 * Reset SKB transmit buffers.  
	 */
	for (i = 0; i < fep->tx_ring; i++) {
		if ((skb = fep->tx_skbuff[i]) == NULL)
			continue;
		fep->tx_skbuff[i] = NULL;
		dev_kfree_skb(skb);
	}

	/*
	 * ...and the same for transmit.  
	 */
	for (i = 0, bdp = fep->tx_bd_base; i < fep->tx_ring; i++, bdp++) {
		fep->tx_skbuff[i] = NULL;
		CBDW_BUFADDR(bdp, virt_to_bus(NULL));
		CBDW_DATLEN(bdp, 0);
		CBDW_SC(bdp, (i < fep->tx_ring - 1) ? 0 : BD_SC_WRAP);
	}

	/*
	 * Enable big endian and don't care about SDMA FC. 
	 */
	FW(fecp, fun_code, 0x78000000);

	/*
	 * Set MII speed. 
	 */
	FW(fecp, mii_speed, fep->fec_phy_speed);

	/*
	 * Clear any outstanding interrupt. 
	 */
	FW(fecp, ievent, 0xffc0);
	FW(fecp, ivec, (fpi->fec_irq / 2) << 29);

	/*
	 * adjust to speed (only for DUET & RMII) 
	 */
#ifdef CONFIG_DUET
	cptr = in_be32(&immap->im_cpm.cp_cptr);
	switch (fpi->fec_no) {
	case 0:
		/*
		 * check if in RMII mode 
		 */
		if ((cptr & 0x100) == 0)
			break;

		if (speed == 10)
			cptr |= 0x0000010;
		else if (speed == 100)
			cptr &= ~0x0000010;
		break;
	case 1:
		/*
		 * check if in RMII mode 
		 */
		if ((cptr & 0x80) == 0)
			break;

		if (speed == 10)
			cptr |= 0x0000008;
		else if (speed == 100)
			cptr &= ~0x0000008;
		break;
	default:
		break;
	}
	out_be32(&immap->im_cpm.cp_cptr, cptr);
#endif

	FW(fecp, r_cntrl, FEC_RCNTRL_MII_MODE);	/* MII enable */
	/*
	 * adjust to duplex mode 
	 */
	if (duplex) {
		FC(fecp, r_cntrl, FEC_RCNTRL_DRT);
		FS(fecp, x_cntrl, FEC_TCNTRL_FDEN);	/* FD enable */
	} else {
		FS(fecp, r_cntrl, FEC_RCNTRL_DRT);
		FC(fecp, x_cntrl, FEC_TCNTRL_FDEN);	/* FD disable */
	}

	/*
	 * Enable interrupts we wish to service. 
	 */
	FW(fecp, imask, FEC_ENET_TXF | FEC_ENET_TXB |
	   FEC_ENET_RXF | FEC_ENET_RXB);

	/*
	 * And last, enable the transmit and receive processing. 
	 */
	FW(fecp, ecntrl, FEC_ECNTRL_PINMUX | FEC_ECNTRL_ETHER_EN);
	FW(fecp, r_des_active, 0x01000000);
}

void fec_stop(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	fec_t *fecp = fep->fecp;
	struct sk_buff *skb;
	int i;

	if ((FR(fecp, ecntrl) & FEC_ECNTRL_ETHER_EN) == 0)
		return;		/* already down */

	FW(fecp, x_cntrl, 0x01);	/* Graceful transmit stop */
	for (i = 0; ((FR(fecp, ievent) & 0x10000000) == 0) &&
	     i < FEC_RESET_DELAY; i++)
		udelay(1);

	if (i == FEC_RESET_DELAY)
		printk(KERN_WARNING DRV_MODULE_NAME
		       ": %s FEC timeout on graceful transmit stop\n",
		       dev->name);
	/*
	 * Disable FEC. Let only MII interrupts. 
	 */
	FW(fecp, imask, 0);
	FW(fecp, ecntrl, ~FEC_ECNTRL_ETHER_EN);

	/*
	 * Reset SKB transmit buffers.  
	 */
	for (i = 0; i < fep->tx_ring; i++) {
		if ((skb = fep->tx_skbuff[i]) == NULL)
			continue;
		fep->tx_skbuff[i] = NULL;
		dev_kfree_skb(skb);
	}

	/*
	 * Reset SKB receive buffers 
	 */
	for (i = 0; i < fep->rx_ring; i++) {
		if ((skb = fep->rx_skbuff[i]) == NULL)
			continue;
		fep->rx_skbuff[i] = NULL;
		dev_kfree_skb(skb);
	}
}

/* common receive function */
static int fec_enet_rx_common(struct net_device *dev, int *budget)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	fec_t *fecp = fep->fecp;
	const struct fec_platform_info *fpi = fep->fpi;
	cbd_t *bdp;
	struct sk_buff *skb, *skbn, *skbt;
	int received = 0;
	__u16 pkt_len, sc;
	int curidx;
	int rx_work_limit;

	if (fpi->use_napi) {
		rx_work_limit = min(dev->quota, *budget);

		if (!netif_running(dev))
			return 0;
	}

	/*
	 * First, grab all of the stats for the incoming packet.
	 * These get messed up if we get called due to a busy condition.
	 */
	bdp = fep->cur_rx;

	/* clear RX status bits for napi*/
	if (fpi->use_napi)
		FW(fecp, ievent, FEC_ENET_RXF | FEC_ENET_RXB);

	while (((sc = CBDR_SC(bdp)) & BD_ENET_RX_EMPTY) == 0) {

		curidx = bdp - fep->rx_bd_base;

		/*
		 * Since we have allocated space to hold a complete frame,
		 * the last indicator should be set.
		 */
		if ((sc & BD_ENET_RX_LAST) == 0)
			printk(KERN_WARNING DRV_MODULE_NAME
			       ": %s rcv is not +last\n",
			       dev->name);

		/*
		 * Check for errors. 
		 */
		if (sc & (BD_ENET_RX_LG | BD_ENET_RX_SH | BD_ENET_RX_CL |
			  BD_ENET_RX_NO | BD_ENET_RX_CR | BD_ENET_RX_OV)) {
			fep->stats.rx_errors++;
			/* Frame too long or too short. */
			if (sc & (BD_ENET_RX_LG | BD_ENET_RX_SH))
				fep->stats.rx_length_errors++;
			/* Frame alignment */
			if (sc & (BD_ENET_RX_NO | BD_ENET_RX_CL))
				fep->stats.rx_frame_errors++;
			/* CRC Error */
			if (sc & BD_ENET_RX_CR)
				fep->stats.rx_crc_errors++;
			/* FIFO overrun */
			if (sc & BD_ENET_RX_OV)
				fep->stats.rx_crc_errors++;

			skbn = fep->rx_skbuff[curidx];
			BUG_ON(skbn == NULL);

		} else {

			/* napi, got packet but no quota */
			if (fpi->use_napi && --rx_work_limit < 0)
				break;

			skb = fep->rx_skbuff[curidx];
			BUG_ON(skb == NULL);

			/*
			 * Process the incoming frame.
			 */
			fep->stats.rx_packets++;
			pkt_len = CBDR_DATLEN(bdp) - 4;	/* remove CRC */
			fep->stats.rx_bytes += pkt_len + 4;

			if (pkt_len <= fpi->rx_copybreak) {
				/* +2 to make IP header L1 cache aligned */
				skbn = dev_alloc_skb(pkt_len + 2);
				if (skbn != NULL) {
					skb_reserve(skbn, 2);	/* align IP header */
					memcpy(skbn->data, skb->data, pkt_len);
					/* swap */
					skbt = skb;
					skb = skbn;
					skbn = skbt;
				}
			} else
				skbn = dev_alloc_skb(ENET_RX_FRSIZE);

			if (skbn != NULL) {
				skb->dev = dev;
				skb_put(skb, pkt_len);	/* Make room */
				skb->protocol = eth_type_trans(skb, dev);
				received++;
				if (!fpi->use_napi)
					netif_rx(skb);
				else
					netif_receive_skb(skb);
			} else {
				printk(KERN_WARNING DRV_MODULE_NAME
				       ": %s Memory squeeze, dropping packet.\n",
				       dev->name);
				fep->stats.rx_dropped++;
				skbn = skb;
			}
		}

		fep->rx_skbuff[curidx] = skbn;
		CBDW_BUFADDR(bdp, dma_map_single(NULL, skbn->data,
						 L1_CACHE_ALIGN(PKT_MAXBUF_SIZE),
						 DMA_FROM_DEVICE));
		CBDW_DATLEN(bdp, 0);
		CBDW_SC(bdp, (sc & ~BD_ENET_RX_STATS) | BD_ENET_RX_EMPTY);

		/*
		 * Update BD pointer to next entry. 
		 */
		if ((sc & BD_ENET_RX_WRAP) == 0)
			bdp++;
		else
			bdp = fep->rx_bd_base;

		/*
		 * Doing this here will keep the FEC running while we process
		 * incoming frames.  On a heavily loaded network, we should be
		 * able to keep up at the expense of system resources.
		 */
		FW(fecp, r_des_active, 0x01000000);
	}

	fep->cur_rx = bdp;

	if (fpi->use_napi) {
		dev->quota -= received;
		*budget -= received;

		if (rx_work_limit < 0)
			return 1;	/* not done */

		/* done */
		netif_rx_complete(dev);

		/* enable RX interrupt bits */
		FS(fecp, imask, FEC_ENET_RXF | FEC_ENET_RXB);
	}

	return 0;
}

static void fec_enet_tx(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	cbd_t *bdp;
	struct sk_buff *skb;
	int dirtyidx, do_wake;
	__u16 sc;

	spin_lock(&fep->lock);
	bdp = fep->dirty_tx;

	do_wake = 0;
	while (((sc = CBDR_SC(bdp)) & BD_ENET_TX_READY) == 0) {

		dirtyidx = bdp - fep->tx_bd_base;

		if (fep->tx_free == fep->tx_ring)
			break;

		skb = fep->tx_skbuff[dirtyidx];

		/*
		 * Check for errors. 
		 */
		if (sc & (BD_ENET_TX_HB | BD_ENET_TX_LC |
			  BD_ENET_TX_RL | BD_ENET_TX_UN | BD_ENET_TX_CSL)) {
			fep->stats.tx_errors++;
			if (sc & BD_ENET_TX_HB)	/* No heartbeat */
				fep->stats.tx_heartbeat_errors++;
			if (sc & BD_ENET_TX_LC)	/* Late collision */
				fep->stats.tx_window_errors++;
			if (sc & BD_ENET_TX_RL)	/* Retrans limit */
				fep->stats.tx_aborted_errors++;
			if (sc & BD_ENET_TX_UN)	/* Underrun */
				fep->stats.tx_fifo_errors++;
			if (sc & BD_ENET_TX_CSL)	/* Carrier lost */
				fep->stats.tx_carrier_errors++;
		} else
			fep->stats.tx_packets++;

		if (sc & BD_ENET_TX_READY)
			printk(KERN_WARNING DRV_MODULE_NAME
			       ": %s HEY! Enet xmit interrupt and TX_READY.\n",
			       dev->name);

		/*
		 * Deferred means some collisions occurred during transmit,
		 * but we eventually sent the packet OK.
		 */
		if (sc & BD_ENET_TX_DEF)
			fep->stats.collisions++;

		/*
		 * Free the sk buffer associated with this last transmit. 
		 */
		dev_kfree_skb_irq(skb);
		fep->tx_skbuff[dirtyidx] = NULL;

		/*
		 * Update pointer to next buffer descriptor to be transmitted. 
		 */
		if ((sc & BD_ENET_TX_WRAP) == 0)
			bdp++;
		else
			bdp = fep->tx_bd_base;

		/*
		 * Since we have freed up a buffer, the ring is no longer
		 * full.
		 */
		if (!fep->tx_free++)
			do_wake = 1;
	}

	fep->dirty_tx = bdp;

	spin_unlock(&fep->lock);

	if (do_wake && netif_queue_stopped(dev))
		netif_wake_queue(dev);
}

/*
 * The interrupt handler.
 * This is called from the MPC core interrupt.
 */
static irqreturn_t
fec_enet_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = dev_id;
	struct fec_enet_private *fep;
	const struct fec_platform_info *fpi;
	fec_t *fecp;
	__u32 int_events;
	__u32 int_events_napi;

	if (unlikely(dev == NULL))
		return IRQ_NONE;

	fep = netdev_priv(dev);
	fecp = fep->fecp;
	fpi = fep->fpi;

	/*
	 * Get the interrupt events that caused us to be here.
	 */
	while ((int_events = FR(fecp, ievent) & FR(fecp, imask)) != 0) {

		if (!fpi->use_napi)
			FW(fecp, ievent, int_events);
		else {
			int_events_napi = int_events & ~(FEC_ENET_RXF | FEC_ENET_RXB);
			FW(fecp, ievent, int_events_napi);
		}

		if ((int_events & (FEC_ENET_HBERR | FEC_ENET_BABR |
				   FEC_ENET_BABT | FEC_ENET_EBERR)) != 0)
			printk(KERN_WARNING DRV_MODULE_NAME
			       ": %s FEC ERROR(s) 0x%x\n",
			       dev->name, int_events);

		if ((int_events & FEC_ENET_RXF) != 0) {
			if (!fpi->use_napi)
				fec_enet_rx_common(dev, NULL);
			else {
				if (netif_rx_schedule_prep(dev)) {
					/* disable rx interrupts */
					FC(fecp, imask, FEC_ENET_RXF | FEC_ENET_RXB);
					__netif_rx_schedule(dev);
				} else {
					printk(KERN_ERR DRV_MODULE_NAME
					       ": %s driver bug! interrupt while in poll!\n",
					       dev->name);
					FC(fecp, imask, FEC_ENET_RXF | FEC_ENET_RXB);
				}
			}
		}

		if ((int_events & FEC_ENET_TXF) != 0)
			fec_enet_tx(dev);
	}

	return IRQ_HANDLED;
}

/* This interrupt occurs when the PHY detects a link change. */
static irqreturn_t
fec_mii_link_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = dev_id;
	struct fec_enet_private *fep;
	const struct fec_platform_info *fpi;

	if (unlikely(dev == NULL))
		return IRQ_NONE;

	fep = netdev_priv(dev);
	fpi = fep->fpi;

	if (!fpi->use_mdio)
		return IRQ_NONE;

	/*
	 * Acknowledge the interrupt if possible. If we have not
	 * found the PHY yet we can't process or acknowledge the
	 * interrupt now. Instead we ignore this interrupt for now,
	 * which we can do since it is edge triggered. It will be
	 * acknowledged later by fec_enet_open().
	 */
	if (!fep->phy)
		return IRQ_NONE;

	fec_mii_ack_int(dev);
	fec_mii_link_status_change_check(dev, 0);

	return IRQ_HANDLED;
}


/**********************************************************************************/

static int fec_enet_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	fec_t *fecp = fep->fecp;
	cbd_t *bdp;
	int curidx;
	unsigned long flags;

	spin_lock_irqsave(&fep->tx_lock, flags);

	/*
	 * Fill in a Tx ring entry 
	 */
	bdp = fep->cur_tx;

	if (!fep->tx_free || (CBDR_SC(bdp) & BD_ENET_TX_READY)) {
		netif_stop_queue(dev);
		spin_unlock_irqrestore(&fep->tx_lock, flags);

		/*
		 * Ooops.  All transmit buffers are full.  Bail out.
		 * This should not happen, since the tx queue should be stopped.
		 */
		printk(KERN_WARNING DRV_MODULE_NAME
		       ": %s tx queue full!.\n", dev->name);
		return 1;
	}

	curidx = bdp - fep->tx_bd_base;
	/*
	 * Clear all of the status flags. 
	 */
	CBDC_SC(bdp, BD_ENET_TX_STATS);

	/*
	 * Save skb pointer. 
	 */
	fep->tx_skbuff[curidx] = skb;

	fep->stats.tx_bytes += skb->len;

	/*
	 * Push the data cache so the CPM does not get stale memory data. 
	 */
	CBDW_BUFADDR(bdp, dma_map_single(NULL, skb->data,
					 skb->len, DMA_TO_DEVICE));
	CBDW_DATLEN(bdp, skb->len);

	dev->trans_start = jiffies;

	/*
	 * If this was the last BD in the ring, start at the beginning again. 
	 */
	if ((CBDR_SC(bdp) & BD_ENET_TX_WRAP) == 0)
		fep->cur_tx++;
	else
		fep->cur_tx = fep->tx_bd_base;

	if (!--fep->tx_free)
		netif_stop_queue(dev);

	/*
	 * Trigger transmission start 
	 */
	CBDS_SC(bdp, BD_ENET_TX_READY | BD_ENET_TX_INTR |
		BD_ENET_TX_LAST | BD_ENET_TX_TC);
	FW(fecp, x_des_active, 0x01000000);

	spin_unlock_irqrestore(&fep->tx_lock, flags);

	return 0;
}

static void fec_timeout(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);

	fep->stats.tx_errors++;

	if (fep->tx_free)
		netif_wake_queue(dev);

	/* check link status again */
	fec_mii_link_status_change_check(dev, 0);
}

static int fec_enet_open(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	const struct fec_platform_info *fpi = fep->fpi;
	unsigned long flags;

	/* Install our interrupt handler. */
	if (request_irq(fpi->fec_irq, fec_enet_interrupt, 0, "fec", dev) != 0) {
		printk(KERN_ERR DRV_MODULE_NAME
		       ": %s Could not allocate FEC IRQ!", dev->name);
		return -EINVAL;
	}

	/* Install our phy interrupt handler */
	if (fpi->phy_irq != -1 && 
		request_irq(fpi->phy_irq, fec_mii_link_interrupt, 0, "fec-phy",
				dev) != 0) {
		printk(KERN_ERR DRV_MODULE_NAME
		       ": %s Could not allocate PHY IRQ!", dev->name);
		free_irq(fpi->fec_irq, dev);
		return -EINVAL;
	}

	if (fpi->use_mdio) {
		fec_mii_startup(dev);
		netif_carrier_off(dev);
		fec_mii_link_status_change_check(dev, 1);
	} else {
		spin_lock_irqsave(&fep->lock, flags);
		fec_restart(dev, 1, 100);	/* XXX this sucks */
		spin_unlock_irqrestore(&fep->lock, flags);

		netif_carrier_on(dev);
		netif_start_queue(dev);
	}
	return 0;
}

static int fec_enet_close(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	const struct fec_platform_info *fpi = fep->fpi;
	unsigned long flags;

	netif_stop_queue(dev);
	netif_carrier_off(dev);

	if (fpi->use_mdio)
		fec_mii_shutdown(dev);

	spin_lock_irqsave(&fep->lock, flags);
	fec_stop(dev);
	spin_unlock_irqrestore(&fep->lock, flags);

	/* release any irqs */
	if (fpi->phy_irq != -1)
		free_irq(fpi->phy_irq, dev);
	free_irq(fpi->fec_irq, dev);

	return 0;
}

static struct net_device_stats *fec_enet_get_stats(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	return &fep->stats;
}

static int fec_enet_poll(struct net_device *dev, int *budget)
{
	return fec_enet_rx_common(dev, budget);
}

/*************************************************************************/

static void fec_get_drvinfo(struct net_device *dev,
			    struct ethtool_drvinfo *info)
{
	strcpy(info->driver, DRV_MODULE_NAME);
	strcpy(info->version, DRV_MODULE_VERSION);
}

static int fec_get_regs_len(struct net_device *dev)
{
	return sizeof(fec_t);
}

static void fec_get_regs(struct net_device *dev, struct ethtool_regs *regs,
			 void *p)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	unsigned long flags;

	if (regs->len < sizeof(fec_t))
		return;

	regs->version = 0;
	spin_lock_irqsave(&fep->lock, flags);
	memcpy_fromio(p, fep->fecp, sizeof(fec_t));
	spin_unlock_irqrestore(&fep->lock, flags);
}

static int fec_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&fep->lock, flags);
	rc = mii_ethtool_gset(&fep->mii_if, cmd);
	spin_unlock_irqrestore(&fep->lock, flags);

	return rc;
}

static int fec_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&fep->lock, flags);
	rc = mii_ethtool_sset(&fep->mii_if, cmd);
	spin_unlock_irqrestore(&fep->lock, flags);

	return rc;
}

static int fec_nway_reset(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	return mii_nway_restart(&fep->mii_if);
}

static __u32 fec_get_msglevel(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	return fep->msg_enable;
}

static void fec_set_msglevel(struct net_device *dev, __u32 value)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	fep->msg_enable = value;
}

static struct ethtool_ops fec_ethtool_ops = {
	.get_drvinfo = fec_get_drvinfo,
	.get_regs_len = fec_get_regs_len,
	.get_settings = fec_get_settings,
	.set_settings = fec_set_settings,
	.nway_reset = fec_nway_reset,
	.get_link = ethtool_op_get_link,
	.get_msglevel = fec_get_msglevel,
	.set_msglevel = fec_set_msglevel,
	.get_tx_csum = ethtool_op_get_tx_csum,
	.set_tx_csum = ethtool_op_set_tx_csum,	/* local! */
	.get_sg = ethtool_op_get_sg,
	.set_sg = ethtool_op_set_sg,
	.get_regs = fec_get_regs,
};

static int fec_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	struct mii_ioctl_data *mii = (struct mii_ioctl_data *)&rq->ifr_data;
	unsigned long flags;
	int rc;

	if (!netif_running(dev))
		return -EINVAL;

	spin_lock_irqsave(&fep->lock, flags);
	rc = generic_mii_ioctl(&fep->mii_if, mii, cmd, NULL);
	spin_unlock_irqrestore(&fep->lock, flags);
	return rc;
}

int fec_8xx_init_one(const struct fec_platform_info *fpi,
		     struct net_device **devp)
{
	immap_t *immap = (immap_t *) IMAP_ADDR;
	static int fec_8xx_version_printed = 0;
	struct net_device *dev = NULL;
	struct fec_enet_private *fep = NULL;
	fec_t *fecp = NULL;
	int i;
	int err = 0;
	int registered = 0;
	__u32 siel;

	*devp = NULL;

	switch (fpi->fec_no) {
	case 0:
		fecp = &((immap_t *) IMAP_ADDR)->im_cpm.cp_fec;
		break;
#ifdef CONFIG_DUET
	case 1:
		fecp = &((immap_t *) IMAP_ADDR)->im_cpm.cp_fec2;
		break;
#endif
	default:
		return -EINVAL;
	}

	if (fec_8xx_version_printed++ == 0)
		printk(KERN_INFO "%s", version);

	i = sizeof(*fep) + (sizeof(struct sk_buff **) *
			    (fpi->rx_ring + fpi->tx_ring));

	dev = alloc_etherdev(i);
	if (!dev) {
		err = -ENOMEM;
		goto err;
	}
	SET_MODULE_OWNER(dev);

	fep = netdev_priv(dev);

	/* partial reset of FEC */
	fec_whack_reset(fecp);

	/* point rx_skbuff, tx_skbuff */
	fep->rx_skbuff = (struct sk_buff **)&fep[1];
	fep->tx_skbuff = fep->rx_skbuff + fpi->rx_ring;

	fep->fecp = fecp;
	fep->fpi = fpi;

	/* init locks */
	spin_lock_init(&fep->lock);
	spin_lock_init(&fep->tx_lock);

	/*
	 * Set the Ethernet address. 
	 */
	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = fpi->macaddr[i];

	fep->ring_base = dma_alloc_coherent(NULL,
					    (fpi->tx_ring + fpi->rx_ring) *
					    sizeof(cbd_t), &fep->ring_mem_addr,
					    GFP_KERNEL);
	if (fep->ring_base == NULL) {
		printk(KERN_ERR DRV_MODULE_NAME
		       ": %s dma alloc failed.\n", dev->name);
		err = -ENOMEM;
		goto err;
	}

	/*
	 * Set receive and transmit descriptor base.
	 */
	fep->rx_bd_base = fep->ring_base;
	fep->tx_bd_base = fep->rx_bd_base + fpi->rx_ring;

	/* initialize ring size variables */
	fep->tx_ring = fpi->tx_ring;
	fep->rx_ring = fpi->rx_ring;

	/* SIU interrupt */
	if (fpi->phy_irq != -1 &&
		(fpi->phy_irq >= SIU_IRQ0 && fpi->phy_irq < SIU_LEVEL7)) {

		siel = in_be32(&immap->im_siu_conf.sc_siel);
		if ((fpi->phy_irq & 1) == 0)
			siel |= (0x80000000 >> fpi->phy_irq);
		else
			siel &= ~(0x80000000 >> (fpi->phy_irq & ~1));
		out_be32(&immap->im_siu_conf.sc_siel, siel);
	}

	/*
	 * The FEC Ethernet specific entries in the device structure. 
	 */
	dev->open = fec_enet_open;
	dev->hard_start_xmit = fec_enet_start_xmit;
	dev->tx_timeout = fec_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;
	dev->stop = fec_enet_close;
	dev->get_stats = fec_enet_get_stats;
	dev->set_multicast_list = fec_set_multicast_list;
	dev->set_mac_address = fec_set_mac_address;
	if (fpi->use_napi) {
		dev->poll = fec_enet_poll;
		dev->weight = fpi->napi_weight;
	}
	dev->ethtool_ops = &fec_ethtool_ops;
	dev->do_ioctl = fec_ioctl;

	fep->fec_phy_speed =
	    ((((fpi->sys_clk + 4999999) / 2500000) / 2) & 0x3F) << 1;

	init_timer(&fep->phy_timer_list);

	/* partial reset of FEC so that only MII works */
	FW(fecp, mii_speed, fep->fec_phy_speed);
	FW(fecp, ievent, 0xffc0);
	FW(fecp, ivec, (fpi->fec_irq / 2) << 29);
	FW(fecp, imask, 0);
	FW(fecp, r_cntrl, FEC_RCNTRL_MII_MODE);	/* MII enable */
	FW(fecp, ecntrl, FEC_ECNTRL_PINMUX | FEC_ECNTRL_ETHER_EN);

	netif_carrier_off(dev);

	err = register_netdev(dev);
	if (err != 0)
		goto err;
	registered = 1;

	if (fpi->use_mdio) {
		fep->mii_if.dev = dev;
		fep->mii_if.mdio_read = fec_mii_read;
		fep->mii_if.mdio_write = fec_mii_write;
		fep->mii_if.phy_id_mask = 0x1f;
		fep->mii_if.reg_num_mask = 0x1f;
		fep->mii_if.phy_id = fec_mii_phy_id_detect(dev);
	}

	*devp = dev;

	return 0;

      err:
	if (dev != NULL) {
		if (fecp != NULL)
			fec_whack_reset(fecp);

		if (registered)
			unregister_netdev(dev);

		if (fep != NULL) {
			if (fep->ring_base)
				dma_free_coherent(NULL,
						  (fpi->tx_ring +
						   fpi->rx_ring) *
						  sizeof(cbd_t), fep->ring_base,
						  fep->ring_mem_addr);
		}
		free_netdev(dev);
	}
	return err;
}

int fec_8xx_cleanup_one(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	fec_t *fecp = fep->fecp;
	const struct fec_platform_info *fpi = fep->fpi;

	fec_whack_reset(fecp);

	unregister_netdev(dev);

	dma_free_coherent(NULL, (fpi->tx_ring + fpi->rx_ring) * sizeof(cbd_t),
			  fep->ring_base, fep->ring_mem_addr);

	free_netdev(dev);

	return 0;
}

/**************************************************************************************/
/**************************************************************************************/
/**************************************************************************************/

static int __init fec_8xx_init(void)
{
	return fec_8xx_platform_init();
}

static void __exit fec_8xx_cleanup(void)
{
	fec_8xx_platform_cleanup();
}

/**************************************************************************************/
/**************************************************************************************/
/**************************************************************************************/

module_init(fec_8xx_init);
module_exit(fec_8xx_cleanup);
