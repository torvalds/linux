/*
 * Combined Ethernet driver for Motorola MPC8xx and MPC82xx.
 *
 * Copyright (c) 2003 Intracom S.A. 
 *  by Pantelis Antoniou <panto@intracom.gr>
 * 
 * 2005 (c) MontaVista Software, Inc. 
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * Heavily based on original FEC driver by Dan Malek <dan@embeddededge.com>
 * and modifications by Joakim Tjernlund <joakim.tjernlund@lumentis.se>
 *
 * This file is licensed under the terms of the GNU General Public License 
 * version 2. This program is licensed "as is" without any warranty of any 
 * kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
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
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/phy.h>

#include <linux/vmalloc.h>
#include <asm/pgtable.h>

#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#include "fs_enet.h"

/*************************************************/

static char version[] __devinitdata =
    DRV_MODULE_NAME ".c:v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")" "\n";

MODULE_AUTHOR("Pantelis Antoniou <panto@intracom.gr>");
MODULE_DESCRIPTION("Freescale Ethernet Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

int fs_enet_debug = -1;		/* -1 == use FS_ENET_DEF_MSG_ENABLE as value */
module_param(fs_enet_debug, int, 0);
MODULE_PARM_DESC(fs_enet_debug,
		 "Freescale bitmapped debugging message enable value");


static void fs_set_multicast_list(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	(*fep->ops->set_multicast_list)(dev);
}

/* NAPI receive function */
static int fs_enet_rx_napi(struct net_device *dev, int *budget)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	const struct fs_platform_info *fpi = fep->fpi;
	cbd_t *bdp;
	struct sk_buff *skb, *skbn, *skbt;
	int received = 0;
	u16 pkt_len, sc;
	int curidx;
	int rx_work_limit = 0;	/* pacify gcc */

	rx_work_limit = min(dev->quota, *budget);

	if (!netif_running(dev))
		return 0;

	/*
	 * First, grab all of the stats for the incoming packet.
	 * These get messed up if we get called due to a busy condition.
	 */
	bdp = fep->cur_rx;

	/* clear RX status bits for napi*/
	(*fep->ops->napi_clear_rx_event)(dev);

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

			skb = fep->rx_skbuff[curidx];

			dma_unmap_single(fep->dev, CBDR_BUFADDR(bdp),
				L1_CACHE_ALIGN(PKT_MAXBUF_SIZE),
				DMA_FROM_DEVICE);

			skbn = skb;

		} else {

			/* napi, got packet but no quota */
			if (--rx_work_limit < 0)
				break;

			skb = fep->rx_skbuff[curidx];

			dma_unmap_single(fep->dev, CBDR_BUFADDR(bdp),
				L1_CACHE_ALIGN(PKT_MAXBUF_SIZE),
				DMA_FROM_DEVICE);

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
					skb_copy_from_linear_data(skb,
						      skbn->data, pkt_len);
					/* swap */
					skbt = skb;
					skb = skbn;
					skbn = skbt;
				}
			} else
				skbn = dev_alloc_skb(ENET_RX_FRSIZE);

			if (skbn != NULL) {
				skb_put(skb, pkt_len);	/* Make room */
				skb->protocol = eth_type_trans(skb, dev);
				received++;
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
		CBDW_BUFADDR(bdp, dma_map_single(fep->dev, skbn->data,
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

		(*fep->ops->rx_bd_done)(dev);
	}

	fep->cur_rx = bdp;

	dev->quota -= received;
	*budget -= received;

	if (rx_work_limit < 0)
		return 1;	/* not done */

	/* done */
	netif_rx_complete(dev);

	(*fep->ops->napi_enable_rx)(dev);

	return 0;
}

/* non NAPI receive function */
static int fs_enet_rx_non_napi(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	const struct fs_platform_info *fpi = fep->fpi;
	cbd_t *bdp;
	struct sk_buff *skb, *skbn, *skbt;
	int received = 0;
	u16 pkt_len, sc;
	int curidx;
	/*
	 * First, grab all of the stats for the incoming packet.
	 * These get messed up if we get called due to a busy condition.
	 */
	bdp = fep->cur_rx;

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

			skb = fep->rx_skbuff[curidx];

			dma_unmap_single(fep->dev, CBDR_BUFADDR(bdp),
				L1_CACHE_ALIGN(PKT_MAXBUF_SIZE),
				DMA_FROM_DEVICE);

			skbn = skb;

		} else {

			skb = fep->rx_skbuff[curidx];

			dma_unmap_single(fep->dev, CBDR_BUFADDR(bdp),
				L1_CACHE_ALIGN(PKT_MAXBUF_SIZE),
				DMA_FROM_DEVICE);

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
					skb_copy_from_linear_data(skb,
						      skbn->data, pkt_len);
					/* swap */
					skbt = skb;
					skb = skbn;
					skbn = skbt;
				}
			} else
				skbn = dev_alloc_skb(ENET_RX_FRSIZE);

			if (skbn != NULL) {
				skb_put(skb, pkt_len);	/* Make room */
				skb->protocol = eth_type_trans(skb, dev);
				received++;
				netif_rx(skb);
			} else {
				printk(KERN_WARNING DRV_MODULE_NAME
				       ": %s Memory squeeze, dropping packet.\n",
				       dev->name);
				fep->stats.rx_dropped++;
				skbn = skb;
			}
		}

		fep->rx_skbuff[curidx] = skbn;
		CBDW_BUFADDR(bdp, dma_map_single(fep->dev, skbn->data,
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

		(*fep->ops->rx_bd_done)(dev);
	}

	fep->cur_rx = bdp;

	return 0;
}

static void fs_enet_tx(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	cbd_t *bdp;
	struct sk_buff *skb;
	int dirtyidx, do_wake, do_restart;
	u16 sc;

	spin_lock(&fep->lock);
	bdp = fep->dirty_tx;

	do_wake = do_restart = 0;
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

			if (sc & (BD_ENET_TX_LC | BD_ENET_TX_RL | BD_ENET_TX_UN)) {
				fep->stats.tx_errors++;
				do_restart = 1;
			}
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

		/* unmap */
		dma_unmap_single(fep->dev, CBDR_BUFADDR(bdp),
				skb->len, DMA_TO_DEVICE);

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

	if (do_restart)
		(*fep->ops->tx_restart)(dev);

	spin_unlock(&fep->lock);

	if (do_wake)
		netif_wake_queue(dev);
}

/*
 * The interrupt handler.
 * This is called from the MPC core interrupt.
 */
static irqreturn_t
fs_enet_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct fs_enet_private *fep;
	const struct fs_platform_info *fpi;
	u32 int_events;
	u32 int_clr_events;
	int nr, napi_ok;
	int handled;

	fep = netdev_priv(dev);
	fpi = fep->fpi;

	nr = 0;
	while ((int_events = (*fep->ops->get_int_events)(dev)) != 0) {

		nr++;

		int_clr_events = int_events;
		if (fpi->use_napi)
			int_clr_events &= ~fep->ev_napi_rx;

		(*fep->ops->clear_int_events)(dev, int_clr_events);

		if (int_events & fep->ev_err)
			(*fep->ops->ev_error)(dev, int_events);

		if (int_events & fep->ev_rx) {
			if (!fpi->use_napi)
				fs_enet_rx_non_napi(dev);
			else {
				napi_ok = netif_rx_schedule_prep(dev);

				(*fep->ops->napi_disable_rx)(dev);
				(*fep->ops->clear_int_events)(dev, fep->ev_napi_rx);

				/* NOTE: it is possible for FCCs in NAPI mode    */
				/* to submit a spurious interrupt while in poll  */
				if (napi_ok)
					__netif_rx_schedule(dev);
			}
		}

		if (int_events & fep->ev_tx)
			fs_enet_tx(dev);
	}

	handled = nr > 0;
	return IRQ_RETVAL(handled);
}

void fs_init_bds(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	cbd_t *bdp;
	struct sk_buff *skb;
	int i;

	fs_cleanup_bds(dev);

	fep->dirty_tx = fep->cur_tx = fep->tx_bd_base;
	fep->tx_free = fep->tx_ring;
	fep->cur_rx = fep->rx_bd_base;

	/*
	 * Initialize the receive buffer descriptors. 
	 */
	for (i = 0, bdp = fep->rx_bd_base; i < fep->rx_ring; i++, bdp++) {
		skb = dev_alloc_skb(ENET_RX_FRSIZE);
		if (skb == NULL) {
			printk(KERN_WARNING DRV_MODULE_NAME
			       ": %s Memory squeeze, unable to allocate skb\n",
			       dev->name);
			break;
		}
		fep->rx_skbuff[i] = skb;
		CBDW_BUFADDR(bdp,
			dma_map_single(fep->dev, skb->data,
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
	 * ...and the same for transmit.  
	 */
	for (i = 0, bdp = fep->tx_bd_base; i < fep->tx_ring; i++, bdp++) {
		fep->tx_skbuff[i] = NULL;
		CBDW_BUFADDR(bdp, 0);
		CBDW_DATLEN(bdp, 0);
		CBDW_SC(bdp, (i < fep->tx_ring - 1) ? 0 : BD_SC_WRAP);
	}
}

void fs_cleanup_bds(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	struct sk_buff *skb;
	cbd_t *bdp;
	int i;

	/*
	 * Reset SKB transmit buffers.  
	 */
	for (i = 0, bdp = fep->tx_bd_base; i < fep->tx_ring; i++, bdp++) {
		if ((skb = fep->tx_skbuff[i]) == NULL)
			continue;

		/* unmap */
		dma_unmap_single(fep->dev, CBDR_BUFADDR(bdp),
				skb->len, DMA_TO_DEVICE);

		fep->tx_skbuff[i] = NULL;
		dev_kfree_skb(skb);
	}

	/*
	 * Reset SKB receive buffers 
	 */
	for (i = 0, bdp = fep->rx_bd_base; i < fep->rx_ring; i++, bdp++) {
		if ((skb = fep->rx_skbuff[i]) == NULL)
			continue;

		/* unmap */
		dma_unmap_single(fep->dev, CBDR_BUFADDR(bdp),
			L1_CACHE_ALIGN(PKT_MAXBUF_SIZE),
			DMA_FROM_DEVICE);

		fep->rx_skbuff[i] = NULL;

		dev_kfree_skb(skb);
	}
}

/**********************************************************************************/

static int fs_enet_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	cbd_t *bdp;
	int curidx;
	u16 sc;
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
		return NETDEV_TX_BUSY;
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
	CBDW_BUFADDR(bdp, dma_map_single(fep->dev,
				skb->data, skb->len, DMA_TO_DEVICE));
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

	/* Trigger transmission start */
	sc = BD_ENET_TX_READY | BD_ENET_TX_INTR |
	     BD_ENET_TX_LAST | BD_ENET_TX_TC;

	/* note that while FEC does not have this bit
	 * it marks it as available for software use
	 * yay for hw reuse :) */
	if (skb->len <= 60)
		sc |= BD_ENET_TX_PAD;
	CBDS_SC(bdp, sc);

	(*fep->ops->tx_kickstart)(dev);

	spin_unlock_irqrestore(&fep->tx_lock, flags);

	return NETDEV_TX_OK;
}

static int fs_request_irq(struct net_device *dev, int irq, const char *name,
		irq_handler_t irqf)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	(*fep->ops->pre_request_irq)(dev, irq);
	return request_irq(irq, irqf, IRQF_SHARED, name, dev);
}

static void fs_free_irq(struct net_device *dev, int irq)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	free_irq(irq, dev);
	(*fep->ops->post_free_irq)(dev, irq);
}

static void fs_timeout(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	unsigned long flags;
	int wake = 0;

	fep->stats.tx_errors++;

	spin_lock_irqsave(&fep->lock, flags);

	if (dev->flags & IFF_UP) {
		phy_stop(fep->phydev);
		(*fep->ops->stop)(dev);
		(*fep->ops->restart)(dev);
		phy_start(fep->phydev);
	}

	phy_start(fep->phydev);
	wake = fep->tx_free && !(CBDR_SC(fep->cur_tx) & BD_ENET_TX_READY);
	spin_unlock_irqrestore(&fep->lock, flags);

	if (wake)
		netif_wake_queue(dev);
}

/*-----------------------------------------------------------------------------
 *  generic link-change handler - should be sufficient for most cases
 *-----------------------------------------------------------------------------*/
static void generic_adjust_link(struct  net_device *dev)
{
       struct fs_enet_private *fep = netdev_priv(dev);
       struct phy_device *phydev = fep->phydev;
       int new_state = 0;

       if (phydev->link) {

               /* adjust to duplex mode */
               if (phydev->duplex != fep->oldduplex){
                       new_state = 1;
                       fep->oldduplex = phydev->duplex;
               }

               if (phydev->speed != fep->oldspeed) {
                       new_state = 1;
                       fep->oldspeed = phydev->speed;
               }

               if (!fep->oldlink) {
                       new_state = 1;
                       fep->oldlink = 1;
                       netif_schedule(dev);
                       netif_carrier_on(dev);
                       netif_start_queue(dev);
               }

               if (new_state)
                       fep->ops->restart(dev);

       } else if (fep->oldlink) {
               new_state = 1;
               fep->oldlink = 0;
               fep->oldspeed = 0;
               fep->oldduplex = -1;
               netif_carrier_off(dev);
               netif_stop_queue(dev);
       }

       if (new_state && netif_msg_link(fep))
               phy_print_status(phydev);
}


static void fs_adjust_link(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&fep->lock, flags);

	if(fep->ops->adjust_link)
		fep->ops->adjust_link(dev);
	else
		generic_adjust_link(dev);

	spin_unlock_irqrestore(&fep->lock, flags);
}

static int fs_init_phy(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	struct phy_device *phydev;

	fep->oldlink = 0;
	fep->oldspeed = 0;
	fep->oldduplex = -1;
	if(fep->fpi->bus_id)
		phydev = phy_connect(dev, fep->fpi->bus_id, &fs_adjust_link, 0,
				PHY_INTERFACE_MODE_MII);
	else {
		printk("No phy bus ID specified in BSP code\n");
		return -EINVAL;
	}
	if (IS_ERR(phydev)) {
		printk(KERN_ERR "%s: Could not attach to PHY\n", dev->name);
		return PTR_ERR(phydev);
	}

	fep->phydev = phydev;

	return 0;
}


static int fs_enet_open(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	int r;
	int err;

	/* Install our interrupt handler. */
	r = fs_request_irq(dev, fep->interrupt, "fs_enet-mac", fs_enet_interrupt);
	if (r != 0) {
		printk(KERN_ERR DRV_MODULE_NAME
		       ": %s Could not allocate FS_ENET IRQ!", dev->name);
		return -EINVAL;
	}

	err = fs_init_phy(dev);
	if(err)
		return err;

	phy_start(fep->phydev);

	return 0;
}

static int fs_enet_close(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	unsigned long flags;

	netif_stop_queue(dev);
	netif_carrier_off(dev);
	phy_stop(fep->phydev);

	spin_lock_irqsave(&fep->lock, flags);
	(*fep->ops->stop)(dev);
	spin_unlock_irqrestore(&fep->lock, flags);

	/* release any irqs */
	phy_disconnect(fep->phydev);
	fep->phydev = NULL;
	fs_free_irq(dev, fep->interrupt);

	return 0;
}

static struct net_device_stats *fs_enet_get_stats(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	return &fep->stats;
}

/*************************************************************************/

static void fs_get_drvinfo(struct net_device *dev,
			    struct ethtool_drvinfo *info)
{
	strcpy(info->driver, DRV_MODULE_NAME);
	strcpy(info->version, DRV_MODULE_VERSION);
}

static int fs_get_regs_len(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	return (*fep->ops->get_regs_len)(dev);
}

static void fs_get_regs(struct net_device *dev, struct ethtool_regs *regs,
			 void *p)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	unsigned long flags;
	int r, len;

	len = regs->len;

	spin_lock_irqsave(&fep->lock, flags);
	r = (*fep->ops->get_regs)(dev, p, &len);
	spin_unlock_irqrestore(&fep->lock, flags);

	if (r == 0)
		regs->version = 0;
}

static int fs_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	return phy_ethtool_gset(fep->phydev, cmd);
}

static int fs_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	phy_ethtool_sset(fep->phydev, cmd);
	return 0;
}

static int fs_nway_reset(struct net_device *dev)
{
	return 0;
}

static u32 fs_get_msglevel(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	return fep->msg_enable;
}

static void fs_set_msglevel(struct net_device *dev, u32 value)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fep->msg_enable = value;
}

static const struct ethtool_ops fs_ethtool_ops = {
	.get_drvinfo = fs_get_drvinfo,
	.get_regs_len = fs_get_regs_len,
	.get_settings = fs_get_settings,
	.set_settings = fs_set_settings,
	.nway_reset = fs_nway_reset,
	.get_link = ethtool_op_get_link,
	.get_msglevel = fs_get_msglevel,
	.set_msglevel = fs_set_msglevel,
	.get_tx_csum = ethtool_op_get_tx_csum,
	.set_tx_csum = ethtool_op_set_tx_csum,	/* local! */
	.get_sg = ethtool_op_get_sg,
	.set_sg = ethtool_op_set_sg,
	.get_regs = fs_get_regs,
};

static int fs_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	struct mii_ioctl_data *mii = (struct mii_ioctl_data *)&rq->ifr_data;
	unsigned long flags;
	int rc;

	if (!netif_running(dev))
		return -EINVAL;

	spin_lock_irqsave(&fep->lock, flags);
	rc = phy_mii_ioctl(fep->phydev, mii, cmd);
	spin_unlock_irqrestore(&fep->lock, flags);
	return rc;
}

extern int fs_mii_connect(struct net_device *dev);
extern void fs_mii_disconnect(struct net_device *dev);

static struct net_device *fs_init_instance(struct device *dev,
		struct fs_platform_info *fpi)
{
	struct net_device *ndev = NULL;
	struct fs_enet_private *fep = NULL;
	int privsize, i, r, err = 0, registered = 0;

	fpi->fs_no = fs_get_id(fpi);
	/* guard */
	if ((unsigned int)fpi->fs_no >= FS_MAX_INDEX)
		return ERR_PTR(-EINVAL);

	privsize = sizeof(*fep) + (sizeof(struct sk_buff **) *
			    (fpi->rx_ring + fpi->tx_ring));

	ndev = alloc_etherdev(privsize);
	if (!ndev) {
		err = -ENOMEM;
		goto err;
	}
	SET_MODULE_OWNER(ndev);

	fep = netdev_priv(ndev);
	memset(fep, 0, privsize);	/* clear everything */

	fep->dev = dev;
	dev_set_drvdata(dev, ndev);
	fep->fpi = fpi;
	if (fpi->init_ioports)
		fpi->init_ioports((struct fs_platform_info *)fpi);

#ifdef CONFIG_FS_ENET_HAS_FEC
	if (fs_get_fec_index(fpi->fs_no) >= 0)
		fep->ops = &fs_fec_ops;
#endif

#ifdef CONFIG_FS_ENET_HAS_SCC
	if (fs_get_scc_index(fpi->fs_no) >=0 )
		fep->ops = &fs_scc_ops;
#endif

#ifdef CONFIG_FS_ENET_HAS_FCC
	if (fs_get_fcc_index(fpi->fs_no) >= 0)
		fep->ops = &fs_fcc_ops;
#endif

	if (fep->ops == NULL) {
		printk(KERN_ERR DRV_MODULE_NAME
		       ": %s No matching ops found (%d).\n",
		       ndev->name, fpi->fs_no);
		err = -EINVAL;
		goto err;
	}

	r = (*fep->ops->setup_data)(ndev);
	if (r != 0) {
		printk(KERN_ERR DRV_MODULE_NAME
		       ": %s setup_data failed\n",
			ndev->name);
		err = r;
		goto err;
	}

	/* point rx_skbuff, tx_skbuff */
	fep->rx_skbuff = (struct sk_buff **)&fep[1];
	fep->tx_skbuff = fep->rx_skbuff + fpi->rx_ring;

	/* init locks */
	spin_lock_init(&fep->lock);
	spin_lock_init(&fep->tx_lock);

	/*
	 * Set the Ethernet address. 
	 */
	for (i = 0; i < 6; i++)
		ndev->dev_addr[i] = fpi->macaddr[i];
	
	r = (*fep->ops->allocate_bd)(ndev);
	
	if (fep->ring_base == NULL) {
		printk(KERN_ERR DRV_MODULE_NAME
		       ": %s buffer descriptor alloc failed (%d).\n", ndev->name, r);
		err = r;
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

	/*
	 * The FEC Ethernet specific entries in the device structure. 
	 */
	ndev->open = fs_enet_open;
	ndev->hard_start_xmit = fs_enet_start_xmit;
	ndev->tx_timeout = fs_timeout;
	ndev->watchdog_timeo = 2 * HZ;
	ndev->stop = fs_enet_close;
	ndev->get_stats = fs_enet_get_stats;
	ndev->set_multicast_list = fs_set_multicast_list;
	if (fpi->use_napi) {
		ndev->poll = fs_enet_rx_napi;
		ndev->weight = fpi->napi_weight;
	}
	ndev->ethtool_ops = &fs_ethtool_ops;
	ndev->do_ioctl = fs_ioctl;

	init_timer(&fep->phy_timer_list);

	netif_carrier_off(ndev);

	err = register_netdev(ndev);
	if (err != 0) {
		printk(KERN_ERR DRV_MODULE_NAME
		       ": %s register_netdev failed.\n", ndev->name);
		goto err;
	}
	registered = 1;


	return ndev;

      err:
	if (ndev != NULL) {

		if (registered)
			unregister_netdev(ndev);

		if (fep != NULL) {
			(*fep->ops->free_bd)(ndev);
			(*fep->ops->cleanup_data)(ndev);
		}

		free_netdev(ndev);
	}

	dev_set_drvdata(dev, NULL);

	return ERR_PTR(err);
}

static int fs_cleanup_instance(struct net_device *ndev)
{
	struct fs_enet_private *fep;
	const struct fs_platform_info *fpi;
	struct device *dev;

	if (ndev == NULL)
		return -EINVAL;

	fep = netdev_priv(ndev);
	if (fep == NULL)
		return -EINVAL;

	fpi = fep->fpi;

	unregister_netdev(ndev);

	dma_free_coherent(fep->dev, (fpi->tx_ring + fpi->rx_ring) * sizeof(cbd_t),
			  fep->ring_base, fep->ring_mem_addr);

	/* reset it */
	(*fep->ops->cleanup_data)(ndev);

	dev = fep->dev;
	if (dev != NULL) {
		dev_set_drvdata(dev, NULL);
		fep->dev = NULL;
	}

	free_netdev(ndev);

	return 0;
}

/**************************************************************************************/

/* handy pointer to the immap */
void *fs_enet_immap = NULL;

static int setup_immap(void)
{
	phys_addr_t paddr = 0;
	unsigned long size = 0;

#ifdef CONFIG_CPM1
	paddr = IMAP_ADDR;
	size = 0x10000;	/* map 64K */
#endif

#ifdef CONFIG_CPM2
	paddr = CPM_MAP_ADDR;
	size = 0x40000;	/* map 256 K */
#endif
	fs_enet_immap = ioremap(paddr, size);
	if (fs_enet_immap == NULL)
		return -EBADF;	/* XXX ahem; maybe just BUG_ON? */

	return 0;
}

static void cleanup_immap(void)
{
	if (fs_enet_immap != NULL) {
		iounmap(fs_enet_immap);
		fs_enet_immap = NULL;
	}
}

/**************************************************************************************/

static int __devinit fs_enet_probe(struct device *dev)
{
	struct net_device *ndev;

	/* no fixup - no device */
	if (dev->platform_data == NULL) {
		printk(KERN_INFO "fs_enet: "
				"probe called with no platform data; "
				"remove unused devices\n");
		return -ENODEV;
	}

	ndev = fs_init_instance(dev, dev->platform_data);
	if (IS_ERR(ndev))
		return PTR_ERR(ndev);
	return 0;
}

static int fs_enet_remove(struct device *dev)
{
	return fs_cleanup_instance(dev_get_drvdata(dev));
}

static struct device_driver fs_enet_fec_driver = {
	.name	  	= "fsl-cpm-fec",
	.bus		= &platform_bus_type,
	.probe		= fs_enet_probe,
	.remove		= fs_enet_remove,
#ifdef CONFIG_PM
/*	.suspend	= fs_enet_suspend,	TODO */
/*	.resume		= fs_enet_resume,	TODO */
#endif
};

static struct device_driver fs_enet_scc_driver = {
	.name	  	= "fsl-cpm-scc",
	.bus		= &platform_bus_type,
	.probe		= fs_enet_probe,
	.remove		= fs_enet_remove,
#ifdef CONFIG_PM
/*	.suspend	= fs_enet_suspend,	TODO */
/*	.resume		= fs_enet_resume,	TODO */
#endif
};

static struct device_driver fs_enet_fcc_driver = {
	.name	  	= "fsl-cpm-fcc",
	.bus		= &platform_bus_type,
	.probe		= fs_enet_probe,
	.remove		= fs_enet_remove,
#ifdef CONFIG_PM
/*	.suspend	= fs_enet_suspend,	TODO */
/*	.resume		= fs_enet_resume,	TODO */
#endif
};

static int __init fs_init(void)
{
	int r;

	printk(KERN_INFO
			"%s", version);

	r = setup_immap();
	if (r != 0)
		return r;

#ifdef CONFIG_FS_ENET_HAS_FCC
	/* let's insert mii stuff */
	r = fs_enet_mdio_bb_init();

	if (r != 0) {
		printk(KERN_ERR DRV_MODULE_NAME
			"BB PHY init failed.\n");
		return r;
	}
	r = driver_register(&fs_enet_fcc_driver);
	if (r != 0)
		goto err;
#endif

#ifdef CONFIG_FS_ENET_HAS_FEC
	r =  fs_enet_mdio_fec_init();
	if (r != 0) {
		printk(KERN_ERR DRV_MODULE_NAME
			"FEC PHY init failed.\n");
		return r;
	}

	r = driver_register(&fs_enet_fec_driver);
	if (r != 0)
		goto err;
#endif

#ifdef CONFIG_FS_ENET_HAS_SCC
	r = driver_register(&fs_enet_scc_driver);
	if (r != 0)
		goto err;
#endif

	return 0;
err:
	cleanup_immap();
	return r;
	
}

static void __exit fs_cleanup(void)
{
	driver_unregister(&fs_enet_fec_driver);
	driver_unregister(&fs_enet_fcc_driver);
	driver_unregister(&fs_enet_scc_driver);
	cleanup_immap();
}

/**************************************************************************************/

module_init(fs_init);
module_exit(fs_cleanup);
