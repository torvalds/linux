/*
 * drivers/net/mv643xx_eth.c - Driver for MV643XX ethernet ports
 * Copyright (C) 2002 Matthew Dharm <mdharm@momenco.com>
 *
 * Based on the 64360 driver from:
 * Copyright (C) 2002 rabeeh@galileo.co.il
 *
 * Copyright (C) 2003 PMC-Sierra, Inc.,
 *	written by Manish Lachwani (lachwani@pmc-sierra.com)
 *
 * Copyright (C) 2003 Ralf Baechle <ralf@linux-mips.org>
 *
 * Copyright (C) 2004-2005 MontaVista Software, Inc.
 *			   Dale Farnsworth <dale@farnsworth.org>
 *
 * Copyright (C) 2004 Steven J. Hill <sjhill1@rockwellcollins.com>
 *				     <sjhill@realitydiluted.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/etherdevice.h>

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include <asm/types.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/delay.h>
#include "mv643xx_eth.h"

/*
 * The first part is the high level driver of the gigE ethernet ports.
 */

/* Constants */
#define VLAN_HLEN		4
#define FCS_LEN			4
#define WRAP			NET_IP_ALIGN + ETH_HLEN + VLAN_HLEN + FCS_LEN
#define RX_SKB_SIZE		((dev->mtu + WRAP + 7) & ~0x7)

#define INT_CAUSE_UNMASK_ALL		0x0007ffff
#define INT_CAUSE_UNMASK_ALL_EXT	0x0011ffff
#define INT_CAUSE_MASK_ALL		0x00000000
#define INT_CAUSE_MASK_ALL_EXT		0x00000000
#define INT_CAUSE_CHECK_BITS		INT_CAUSE_UNMASK_ALL
#define INT_CAUSE_CHECK_BITS_EXT	INT_CAUSE_UNMASK_ALL_EXT

#ifdef MV643XX_CHECKSUM_OFFLOAD_TX
#define MAX_DESCS_PER_SKB	(MAX_SKB_FRAGS + 1)
#else
#define MAX_DESCS_PER_SKB	1
#endif

#define PHY_WAIT_ITERATIONS	1000	/* 1000 iterations * 10uS = 10mS max */
#define PHY_WAIT_MICRO_SECONDS	10

/* Static function declarations */
static int eth_port_link_is_up(unsigned int eth_port_num);
static void eth_port_uc_addr_get(struct net_device *dev,
						unsigned char *MacAddr);
static int mv643xx_eth_real_open(struct net_device *);
static int mv643xx_eth_real_stop(struct net_device *);
static int mv643xx_eth_change_mtu(struct net_device *, int);
static struct net_device_stats *mv643xx_eth_get_stats(struct net_device *);
static void eth_port_init_mac_tables(unsigned int eth_port_num);
#ifdef MV643XX_NAPI
static int mv643xx_poll(struct net_device *dev, int *budget);
#endif
static void ethernet_phy_set(unsigned int eth_port_num, int phy_addr);
static int ethernet_phy_detect(unsigned int eth_port_num);
static struct ethtool_ops mv643xx_ethtool_ops;

static char mv643xx_driver_name[] = "mv643xx_eth";
static char mv643xx_driver_version[] = "1.0";

static void __iomem *mv643xx_eth_shared_base;

/* used to protect MV643XX_ETH_SMI_REG, which is shared across ports */
static DEFINE_SPINLOCK(mv643xx_eth_phy_lock);

static inline u32 mv_read(int offset)
{
	void __iomem *reg_base;

	reg_base = mv643xx_eth_shared_base - MV643XX_ETH_SHARED_REGS;

	return readl(reg_base + offset);
}

static inline void mv_write(int offset, u32 data)
{
	void __iomem *reg_base;

	reg_base = mv643xx_eth_shared_base - MV643XX_ETH_SHARED_REGS;
	writel(data, reg_base + offset);
}

/*
 * Changes MTU (maximum transfer unit) of the gigabit ethenret port
 *
 * Input :	pointer to ethernet interface network device structure
 *		new mtu size
 * Output :	0 upon success, -EINVAL upon failure
 */
static int mv643xx_eth_change_mtu(struct net_device *dev, int new_mtu)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&mp->lock, flags);

	if ((new_mtu > 9500) || (new_mtu < 64)) {
		spin_unlock_irqrestore(&mp->lock, flags);
		return -EINVAL;
	}

	dev->mtu = new_mtu;
	/*
	 * Stop then re-open the interface. This will allocate RX skb's with
	 * the new MTU.
	 * There is a possible danger that the open will not successed, due
	 * to memory is full, which might fail the open function.
	 */
	if (netif_running(dev)) {
		if (mv643xx_eth_real_stop(dev))
			printk(KERN_ERR
				"%s: Fatal error on stopping device\n",
				dev->name);
		if (mv643xx_eth_real_open(dev))
			printk(KERN_ERR
				"%s: Fatal error on opening device\n",
				dev->name);
	}

	spin_unlock_irqrestore(&mp->lock, flags);
	return 0;
}

/*
 * mv643xx_eth_rx_task
 *
 * Fills / refills RX queue on a certain gigabit ethernet port
 *
 * Input :	pointer to ethernet interface network device structure
 * Output :	N/A
 */
static void mv643xx_eth_rx_task(void *data)
{
	struct net_device *dev = (struct net_device *)data;
	struct mv643xx_private *mp = netdev_priv(dev);
	struct pkt_info pkt_info;
	struct sk_buff *skb;

	if (test_and_set_bit(0, &mp->rx_task_busy))
		panic("%s: Error in test_set_bit / clear_bit", dev->name);

	while (mp->rx_ring_skbs < (mp->rx_ring_size - 5)) {
		skb = dev_alloc_skb(RX_SKB_SIZE);
		if (!skb)
			break;
		mp->rx_ring_skbs++;
		pkt_info.cmd_sts = ETH_RX_ENABLE_INTERRUPT;
		pkt_info.byte_cnt = RX_SKB_SIZE;
		pkt_info.buf_ptr = dma_map_single(NULL, skb->data, RX_SKB_SIZE,
							DMA_FROM_DEVICE);
		pkt_info.return_info = skb;
		if (eth_rx_return_buff(mp, &pkt_info) != ETH_OK) {
			printk(KERN_ERR
				"%s: Error allocating RX Ring\n", dev->name);
			break;
		}
		skb_reserve(skb, 2);
	}
	clear_bit(0, &mp->rx_task_busy);
	/*
	 * If RX ring is empty of SKB, set a timer to try allocating
	 * again in a later time .
	 */
	if ((mp->rx_ring_skbs == 0) && (mp->rx_timer_flag == 0)) {
		printk(KERN_INFO "%s: Rx ring is empty\n", dev->name);
		/* After 100mSec */
		mp->timeout.expires = jiffies + (HZ / 10);
		add_timer(&mp->timeout);
		mp->rx_timer_flag = 1;
	}
#ifdef MV643XX_RX_QUEUE_FILL_ON_TASK
	else {
		/* Return interrupts */
		mv_write(MV643XX_ETH_INTERRUPT_MASK_REG(mp->port_num),
							INT_CAUSE_UNMASK_ALL);
	}
#endif
}

/*
 * mv643xx_eth_rx_task_timer_wrapper
 *
 * Timer routine to wake up RX queue filling task. This function is
 * used only in case the RX queue is empty, and all alloc_skb has
 * failed (due to out of memory event).
 *
 * Input :	pointer to ethernet interface network device structure
 * Output :	N/A
 */
static void mv643xx_eth_rx_task_timer_wrapper(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct mv643xx_private *mp = netdev_priv(dev);

	mp->rx_timer_flag = 0;
	mv643xx_eth_rx_task((void *)data);
}

/*
 * mv643xx_eth_update_mac_address
 *
 * Update the MAC address of the port in the address table
 *
 * Input :	pointer to ethernet interface network device structure
 * Output :	N/A
 */
static void mv643xx_eth_update_mac_address(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	unsigned int port_num = mp->port_num;

	eth_port_init_mac_tables(port_num);
	memcpy(mp->port_mac_addr, dev->dev_addr, 6);
	eth_port_uc_addr_set(port_num, mp->port_mac_addr);
}

/*
 * mv643xx_eth_set_rx_mode
 *
 * Change from promiscuos to regular rx mode
 *
 * Input :	pointer to ethernet interface network device structure
 * Output :	N/A
 */
static void mv643xx_eth_set_rx_mode(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);

	if (dev->flags & IFF_PROMISC)
		mp->port_config |= (u32) MV643XX_ETH_UNICAST_PROMISCUOUS_MODE;
	else
		mp->port_config &= ~(u32) MV643XX_ETH_UNICAST_PROMISCUOUS_MODE;

	mv_write(MV643XX_ETH_PORT_CONFIG_REG(mp->port_num), mp->port_config);
}

/*
 * mv643xx_eth_set_mac_address
 *
 * Change the interface's mac address.
 * No special hardware thing should be done because interface is always
 * put in promiscuous mode.
 *
 * Input :	pointer to ethernet interface network device structure and
 *		a pointer to the designated entry to be added to the cache.
 * Output :	zero upon success, negative upon failure
 */
static int mv643xx_eth_set_mac_address(struct net_device *dev, void *addr)
{
	int i;

	for (i = 0; i < 6; i++)
		/* +2 is for the offset of the HW addr type */
		dev->dev_addr[i] = ((unsigned char *)addr)[i + 2];
	mv643xx_eth_update_mac_address(dev);
	return 0;
}

/*
 * mv643xx_eth_tx_timeout
 *
 * Called upon a timeout on transmitting a packet
 *
 * Input :	pointer to ethernet interface network device structure.
 * Output :	N/A
 */
static void mv643xx_eth_tx_timeout(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);

	printk(KERN_INFO "%s: TX timeout  ", dev->name);

	/* Do the reset outside of interrupt context */
	schedule_work(&mp->tx_timeout_task);
}

/*
 * mv643xx_eth_tx_timeout_task
 *
 * Actual routine to reset the adapter when a timeout on Tx has occurred
 */
static void mv643xx_eth_tx_timeout_task(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);

	netif_device_detach(dev);
	eth_port_reset(mp->port_num);
	eth_port_start(mp);
	netif_device_attach(dev);
}

/*
 * mv643xx_eth_free_tx_queue
 *
 * Input :	dev - a pointer to the required interface
 *
 * Output :	0 if was able to release skb , nonzero otherwise
 */
static int mv643xx_eth_free_tx_queue(struct net_device *dev,
					unsigned int eth_int_cause_ext)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	struct net_device_stats *stats = &mp->stats;
	struct pkt_info pkt_info;
	int released = 1;

	if (!(eth_int_cause_ext & (BIT0 | BIT8)))
		return released;

	spin_lock(&mp->lock);

	/* Check only queue 0 */
	while (eth_tx_return_desc(mp, &pkt_info) == ETH_OK) {
		if (pkt_info.cmd_sts & BIT0) {
			printk("%s: Error in TX\n", dev->name);
			stats->tx_errors++;
		}

		/*
		 * If return_info is different than 0, release the skb.
		 * The case where return_info is not 0 is only in case
		 * when transmitted a scatter/gather packet, where only
		 * last skb releases the whole chain.
		 */
		if (pkt_info.return_info) {
			if (skb_shinfo(pkt_info.return_info)->nr_frags)
				dma_unmap_page(NULL, pkt_info.buf_ptr,
						pkt_info.byte_cnt,
						DMA_TO_DEVICE);
			else
				dma_unmap_single(NULL, pkt_info.buf_ptr,
						pkt_info.byte_cnt,
						DMA_TO_DEVICE);

			dev_kfree_skb_irq(pkt_info.return_info);
			released = 0;
		} else
			dma_unmap_page(NULL, pkt_info.buf_ptr,
					pkt_info.byte_cnt, DMA_TO_DEVICE);
	}

	spin_unlock(&mp->lock);

	return released;
}

/*
 * mv643xx_eth_receive
 *
 * This function is forward packets that are received from the port's
 * queues toward kernel core or FastRoute them to another interface.
 *
 * Input :	dev - a pointer to the required interface
 *		max - maximum number to receive (0 means unlimted)
 *
 * Output :	number of served packets
 */
#ifdef MV643XX_NAPI
static int mv643xx_eth_receive_queue(struct net_device *dev, int budget)
#else
static int mv643xx_eth_receive_queue(struct net_device *dev)
#endif
{
	struct mv643xx_private *mp = netdev_priv(dev);
	struct net_device_stats *stats = &mp->stats;
	unsigned int received_packets = 0;
	struct sk_buff *skb;
	struct pkt_info pkt_info;

#ifdef MV643XX_NAPI
	while (budget-- > 0 && eth_port_receive(mp, &pkt_info) == ETH_OK) {
#else
	while (eth_port_receive(mp, &pkt_info) == ETH_OK) {
#endif
		mp->rx_ring_skbs--;
		received_packets++;

		/* Update statistics. Note byte count includes 4 byte CRC count */
		stats->rx_packets++;
		stats->rx_bytes += pkt_info.byte_cnt;
		skb = pkt_info.return_info;
		/*
		 * In case received a packet without first / last bits on OR
		 * the error summary bit is on, the packets needs to be dropeed.
		 */
		if (((pkt_info.cmd_sts
				& (ETH_RX_FIRST_DESC | ETH_RX_LAST_DESC)) !=
					(ETH_RX_FIRST_DESC | ETH_RX_LAST_DESC))
				|| (pkt_info.cmd_sts & ETH_ERROR_SUMMARY)) {
			stats->rx_dropped++;
			if ((pkt_info.cmd_sts & (ETH_RX_FIRST_DESC |
							ETH_RX_LAST_DESC)) !=
				(ETH_RX_FIRST_DESC | ETH_RX_LAST_DESC)) {
				if (net_ratelimit())
					printk(KERN_ERR
						"%s: Received packet spread "
						"on multiple descriptors\n",
						dev->name);
			}
			if (pkt_info.cmd_sts & ETH_ERROR_SUMMARY)
				stats->rx_errors++;

			dev_kfree_skb_irq(skb);
		} else {
			/*
			 * The -4 is for the CRC in the trailer of the
			 * received packet
			 */
			skb_put(skb, pkt_info.byte_cnt - 4);
			skb->dev = dev;

			if (pkt_info.cmd_sts & ETH_LAYER_4_CHECKSUM_OK) {
				skb->ip_summed = CHECKSUM_UNNECESSARY;
				skb->csum = htons(
					(pkt_info.cmd_sts & 0x0007fff8) >> 3);
			}
			skb->protocol = eth_type_trans(skb, dev);
#ifdef MV643XX_NAPI
			netif_receive_skb(skb);
#else
			netif_rx(skb);
#endif
		}
	}

	return received_packets;
}

/*
 * mv643xx_eth_int_handler
 *
 * Main interrupt handler for the gigbit ethernet ports
 *
 * Input :	irq	- irq number (not used)
 *		dev_id	- a pointer to the required interface's data structure
 *		regs	- not used
 * Output :	N/A
 */

static irqreturn_t mv643xx_eth_int_handler(int irq, void *dev_id,
							struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct mv643xx_private *mp = netdev_priv(dev);
	u32 eth_int_cause, eth_int_cause_ext = 0;
	unsigned int port_num = mp->port_num;

	/* Read interrupt cause registers */
	eth_int_cause = mv_read(MV643XX_ETH_INTERRUPT_CAUSE_REG(port_num)) &
						INT_CAUSE_UNMASK_ALL;

	if (eth_int_cause & BIT1)
		eth_int_cause_ext = mv_read(
			MV643XX_ETH_INTERRUPT_CAUSE_EXTEND_REG(port_num)) &
						INT_CAUSE_UNMASK_ALL_EXT;

#ifdef MV643XX_NAPI
	if (!(eth_int_cause & 0x0007fffd)) {
		/* Dont ack the Rx interrupt */
#endif
		/*
		 * Clear specific ethernet port intrerrupt registers by
		 * acknowleding relevant bits.
		 */
		mv_write(MV643XX_ETH_INTERRUPT_CAUSE_REG(port_num),
							~eth_int_cause);
		if (eth_int_cause_ext != 0x0)
			mv_write(MV643XX_ETH_INTERRUPT_CAUSE_EXTEND_REG
					(port_num), ~eth_int_cause_ext);

		/* UDP change : We may need this */
		if ((eth_int_cause_ext & 0x0000ffff) &&
		    (mv643xx_eth_free_tx_queue(dev, eth_int_cause_ext) == 0) &&
		    (mp->tx_ring_size > mp->tx_ring_skbs + MAX_DESCS_PER_SKB))
			netif_wake_queue(dev);
#ifdef MV643XX_NAPI
	} else {
		if (netif_rx_schedule_prep(dev)) {
			/* Mask all the interrupts */
			mv_write(MV643XX_ETH_INTERRUPT_MASK_REG(port_num), 0);
			mv_write(MV643XX_ETH_INTERRUPT_EXTEND_MASK_REG
								(port_num), 0);
			__netif_rx_schedule(dev);
		}
#else
		if (eth_int_cause & (BIT2 | BIT11))
			mv643xx_eth_receive_queue(dev, 0);

		/*
		 * After forwarded received packets to upper layer, add a task
		 * in an interrupts enabled context that refills the RX ring
		 * with skb's.
		 */
#ifdef MV643XX_RX_QUEUE_FILL_ON_TASK
		/* Unmask all interrupts on ethernet port */
		mv_write(MV643XX_ETH_INTERRUPT_MASK_REG(port_num),
							INT_CAUSE_MASK_ALL);
		queue_task(&mp->rx_task, &tq_immediate);
		mark_bh(IMMEDIATE_BH);
#else
		mp->rx_task.func(dev);
#endif
#endif
	}
	/* PHY status changed */
	if (eth_int_cause_ext & (BIT16 | BIT20)) {
		if (eth_port_link_is_up(port_num)) {
			netif_carrier_on(dev);
			netif_wake_queue(dev);
			/* Start TX queue */
			mv_write(MV643XX_ETH_TRANSMIT_QUEUE_COMMAND_REG
								(port_num), 1);
		} else {
			netif_carrier_off(dev);
			netif_stop_queue(dev);
		}
	}

	/*
	 * If no real interrupt occured, exit.
	 * This can happen when using gigE interrupt coalescing mechanism.
	 */
	if ((eth_int_cause == 0x0) && (eth_int_cause_ext == 0x0))
		return IRQ_NONE;

	return IRQ_HANDLED;
}

#ifdef MV643XX_COAL

/*
 * eth_port_set_rx_coal - Sets coalescing interrupt mechanism on RX path
 *
 * DESCRIPTION:
 *	This routine sets the RX coalescing interrupt mechanism parameter.
 *	This parameter is a timeout counter, that counts in 64 t_clk
 *	chunks ; that when timeout event occurs a maskable interrupt
 *	occurs.
 *	The parameter is calculated using the tClk of the MV-643xx chip
 *	, and the required delay of the interrupt in usec.
 *
 * INPUT:
 *	unsigned int eth_port_num	Ethernet port number
 *	unsigned int t_clk		t_clk of the MV-643xx chip in HZ units
 *	unsigned int delay		Delay in usec
 *
 * OUTPUT:
 *	Interrupt coalescing mechanism value is set in MV-643xx chip.
 *
 * RETURN:
 *	The interrupt coalescing value set in the gigE port.
 *
 */
static unsigned int eth_port_set_rx_coal(unsigned int eth_port_num,
					unsigned int t_clk, unsigned int delay)
{
	unsigned int coal = ((t_clk / 1000000) * delay) / 64;

	/* Set RX Coalescing mechanism */
	mv_write(MV643XX_ETH_SDMA_CONFIG_REG(eth_port_num),
		((coal & 0x3fff) << 8) |
		(mv_read(MV643XX_ETH_SDMA_CONFIG_REG(eth_port_num))
			& 0xffc000ff));

	return coal;
}
#endif

/*
 * eth_port_set_tx_coal - Sets coalescing interrupt mechanism on TX path
 *
 * DESCRIPTION:
 *	This routine sets the TX coalescing interrupt mechanism parameter.
 *	This parameter is a timeout counter, that counts in 64 t_clk
 *	chunks ; that when timeout event occurs a maskable interrupt
 *	occurs.
 *	The parameter is calculated using the t_cLK frequency of the
 *	MV-643xx chip and the required delay in the interrupt in uSec
 *
 * INPUT:
 *	unsigned int eth_port_num	Ethernet port number
 *	unsigned int t_clk		t_clk of the MV-643xx chip in HZ units
 *	unsigned int delay		Delay in uSeconds
 *
 * OUTPUT:
 *	Interrupt coalescing mechanism value is set in MV-643xx chip.
 *
 * RETURN:
 *	The interrupt coalescing value set in the gigE port.
 *
 */
static unsigned int eth_port_set_tx_coal(unsigned int eth_port_num,
					unsigned int t_clk, unsigned int delay)
{
	unsigned int coal;
	coal = ((t_clk / 1000000) * delay) / 64;
	/* Set TX Coalescing mechanism */
	mv_write(MV643XX_ETH_TX_FIFO_URGENT_THRESHOLD_REG(eth_port_num),
								coal << 4);
	return coal;
}

/*
 * mv643xx_eth_open
 *
 * This function is called when openning the network device. The function
 * should initialize all the hardware, initialize cyclic Rx/Tx
 * descriptors chain and buffers and allocate an IRQ to the network
 * device.
 *
 * Input :	a pointer to the network device structure
 *
 * Output :	zero of success , nonzero if fails.
 */

static int mv643xx_eth_open(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	unsigned int port_num = mp->port_num;
	int err;

	spin_lock_irq(&mp->lock);

	err = request_irq(dev->irq, mv643xx_eth_int_handler,
			SA_SHIRQ | SA_SAMPLE_RANDOM, dev->name, dev);

	if (err) {
		printk(KERN_ERR "Can not assign IRQ number to MV643XX_eth%d\n",
								port_num);
		err = -EAGAIN;
		goto out;
	}

	if (mv643xx_eth_real_open(dev)) {
		printk("%s: Error opening interface\n", dev->name);
		err = -EBUSY;
		goto out_free;
	}

	spin_unlock_irq(&mp->lock);

	return 0;

out_free:
	free_irq(dev->irq, dev);

out:
	spin_unlock_irq(&mp->lock);

	return err;
}

/*
 * ether_init_rx_desc_ring - Curve a Rx chain desc list and buffer in memory.
 *
 * DESCRIPTION:
 *	This function prepares a Rx chained list of descriptors and packet
 *	buffers in a form of a ring. The routine must be called after port
 *	initialization routine and before port start routine.
 *	The Ethernet SDMA engine uses CPU bus addresses to access the various
 *	devices in the system (i.e. DRAM). This function uses the ethernet
 *	struct 'virtual to physical' routine (set by the user) to set the ring
 *	with physical addresses.
 *
 * INPUT:
 *	struct mv643xx_private *mp	Ethernet Port Control srtuct.
 *
 * OUTPUT:
 *	The routine updates the Ethernet port control struct with information
 *	regarding the Rx descriptors and buffers.
 *
 * RETURN:
 *	None.
 */
static void ether_init_rx_desc_ring(struct mv643xx_private *mp)
{
	volatile struct eth_rx_desc *p_rx_desc;
	int rx_desc_num = mp->rx_ring_size;
	int i;

	/* initialize the next_desc_ptr links in the Rx descriptors ring */
	p_rx_desc = (struct eth_rx_desc *)mp->p_rx_desc_area;
	for (i = 0; i < rx_desc_num; i++) {
		p_rx_desc[i].next_desc_ptr = mp->rx_desc_dma +
			((i + 1) % rx_desc_num) * sizeof(struct eth_rx_desc);
	}

	/* Save Rx desc pointer to driver struct. */
	mp->rx_curr_desc_q = 0;
	mp->rx_used_desc_q = 0;

	mp->rx_desc_area_size = rx_desc_num * sizeof(struct eth_rx_desc);

	/* Add the queue to the list of RX queues of this port */
	mp->port_rx_queue_command |= 1;
}

/*
 * ether_init_tx_desc_ring - Curve a Tx chain desc list and buffer in memory.
 *
 * DESCRIPTION:
 *	This function prepares a Tx chained list of descriptors and packet
 *	buffers in a form of a ring. The routine must be called after port
 *	initialization routine and before port start routine.
 *	The Ethernet SDMA engine uses CPU bus addresses to access the various
 *	devices in the system (i.e. DRAM). This function uses the ethernet
 *	struct 'virtual to physical' routine (set by the user) to set the ring
 *	with physical addresses.
 *
 * INPUT:
 *	struct mv643xx_private *mp	Ethernet Port Control srtuct.
 *
 * OUTPUT:
 *	The routine updates the Ethernet port control struct with information
 *	regarding the Tx descriptors and buffers.
 *
 * RETURN:
 *	None.
 */
static void ether_init_tx_desc_ring(struct mv643xx_private *mp)
{
	int tx_desc_num = mp->tx_ring_size;
	struct eth_tx_desc *p_tx_desc;
	int i;

	/* Initialize the next_desc_ptr links in the Tx descriptors ring */
	p_tx_desc = (struct eth_tx_desc *)mp->p_tx_desc_area;
	for (i = 0; i < tx_desc_num; i++) {
		p_tx_desc[i].next_desc_ptr = mp->tx_desc_dma +
			((i + 1) % tx_desc_num) * sizeof(struct eth_tx_desc);
	}

	mp->tx_curr_desc_q = 0;
	mp->tx_used_desc_q = 0;
#ifdef MV643XX_CHECKSUM_OFFLOAD_TX
	mp->tx_first_desc_q = 0;
#endif

	mp->tx_desc_area_size = tx_desc_num * sizeof(struct eth_tx_desc);

	/* Add the queue to the list of Tx queues of this port */
	mp->port_tx_queue_command |= 1;
}

/* Helper function for mv643xx_eth_open */
static int mv643xx_eth_real_open(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	unsigned int port_num = mp->port_num;
	unsigned int size;

	/* Stop RX Queues */
	mv_write(MV643XX_ETH_RECEIVE_QUEUE_COMMAND_REG(port_num), 0x0000ff00);

	/* Clear the ethernet port interrupts */
	mv_write(MV643XX_ETH_INTERRUPT_CAUSE_REG(port_num), 0);
	mv_write(MV643XX_ETH_INTERRUPT_CAUSE_EXTEND_REG(port_num), 0);

	/* Unmask RX buffer and TX end interrupt */
	mv_write(MV643XX_ETH_INTERRUPT_MASK_REG(port_num),
						INT_CAUSE_UNMASK_ALL);

	/* Unmask phy and link status changes interrupts */
	mv_write(MV643XX_ETH_INTERRUPT_EXTEND_MASK_REG(port_num),
						INT_CAUSE_UNMASK_ALL_EXT);

	/* Set the MAC Address */
	memcpy(mp->port_mac_addr, dev->dev_addr, 6);

	eth_port_init(mp);

	INIT_WORK(&mp->rx_task, (void (*)(void *))mv643xx_eth_rx_task, dev);

	memset(&mp->timeout, 0, sizeof(struct timer_list));
	mp->timeout.function = mv643xx_eth_rx_task_timer_wrapper;
	mp->timeout.data = (unsigned long)dev;

	mp->rx_task_busy = 0;
	mp->rx_timer_flag = 0;

	/* Allocate RX and TX skb rings */
	mp->rx_skb = kmalloc(sizeof(*mp->rx_skb) * mp->rx_ring_size,
								GFP_KERNEL);
	if (!mp->rx_skb) {
		printk(KERN_ERR "%s: Cannot allocate Rx skb ring\n", dev->name);
		return -ENOMEM;
	}
	mp->tx_skb = kmalloc(sizeof(*mp->tx_skb) * mp->tx_ring_size,
								GFP_KERNEL);
	if (!mp->tx_skb) {
		printk(KERN_ERR "%s: Cannot allocate Tx skb ring\n", dev->name);
		kfree(mp->rx_skb);
		return -ENOMEM;
	}

	/* Allocate TX ring */
	mp->tx_ring_skbs = 0;
	size = mp->tx_ring_size * sizeof(struct eth_tx_desc);
	mp->tx_desc_area_size = size;

	if (mp->tx_sram_size) {
		mp->p_tx_desc_area = ioremap(mp->tx_sram_addr,
							mp->tx_sram_size);
		mp->tx_desc_dma = mp->tx_sram_addr;
	} else
		mp->p_tx_desc_area = dma_alloc_coherent(NULL, size,
							&mp->tx_desc_dma,
							GFP_KERNEL);

	if (!mp->p_tx_desc_area) {
		printk(KERN_ERR "%s: Cannot allocate Tx Ring (size %d bytes)\n",
							dev->name, size);
		kfree(mp->rx_skb);
		kfree(mp->tx_skb);
		return -ENOMEM;
	}
	BUG_ON((u32) mp->p_tx_desc_area & 0xf);	/* check 16-byte alignment */
	memset((void *)mp->p_tx_desc_area, 0, mp->tx_desc_area_size);

	ether_init_tx_desc_ring(mp);

	/* Allocate RX ring */
	mp->rx_ring_skbs = 0;
	size = mp->rx_ring_size * sizeof(struct eth_rx_desc);
	mp->rx_desc_area_size = size;

	if (mp->rx_sram_size) {
		mp->p_rx_desc_area = ioremap(mp->rx_sram_addr,
							mp->rx_sram_size);
		mp->rx_desc_dma = mp->rx_sram_addr;
	} else
		mp->p_rx_desc_area = dma_alloc_coherent(NULL, size,
							&mp->rx_desc_dma,
							GFP_KERNEL);

	if (!mp->p_rx_desc_area) {
		printk(KERN_ERR "%s: Cannot allocate Rx ring (size %d bytes)\n",
							dev->name, size);
		printk(KERN_ERR "%s: Freeing previously allocated TX queues...",
							dev->name);
		if (mp->rx_sram_size)
			iounmap(mp->p_rx_desc_area);
		else
			dma_free_coherent(NULL, mp->tx_desc_area_size,
					mp->p_tx_desc_area, mp->tx_desc_dma);
		kfree(mp->rx_skb);
		kfree(mp->tx_skb);
		return -ENOMEM;
	}
	memset((void *)mp->p_rx_desc_area, 0, size);

	ether_init_rx_desc_ring(mp);

	mv643xx_eth_rx_task(dev);	/* Fill RX ring with skb's */

	eth_port_start(mp);

	/* Interrupt Coalescing */

#ifdef MV643XX_COAL
	mp->rx_int_coal =
		eth_port_set_rx_coal(port_num, 133000000, MV643XX_RX_COAL);
#endif

	mp->tx_int_coal =
		eth_port_set_tx_coal(port_num, 133000000, MV643XX_TX_COAL);

	netif_start_queue(dev);

	return 0;
}

static void mv643xx_eth_free_tx_rings(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	unsigned int port_num = mp->port_num;
	unsigned int curr;

	/* Stop Tx Queues */
	mv_write(MV643XX_ETH_TRANSMIT_QUEUE_COMMAND_REG(port_num), 0x0000ff00);

	/* Free outstanding skb's on TX rings */
	for (curr = 0; mp->tx_ring_skbs && curr < mp->tx_ring_size; curr++) {
		if (mp->tx_skb[curr]) {
			dev_kfree_skb(mp->tx_skb[curr]);
			mp->tx_ring_skbs--;
		}
	}
	if (mp->tx_ring_skbs)
		printk("%s: Error on Tx descriptor free - could not free %d"
				" descriptors\n", dev->name, mp->tx_ring_skbs);

	/* Free TX ring */
	if (mp->tx_sram_size)
		iounmap(mp->p_tx_desc_area);
	else
		dma_free_coherent(NULL, mp->tx_desc_area_size,
				mp->p_tx_desc_area, mp->tx_desc_dma);
}

static void mv643xx_eth_free_rx_rings(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	unsigned int port_num = mp->port_num;
	int curr;

	/* Stop RX Queues */
	mv_write(MV643XX_ETH_RECEIVE_QUEUE_COMMAND_REG(port_num), 0x0000ff00);

	/* Free preallocated skb's on RX rings */
	for (curr = 0; mp->rx_ring_skbs && curr < mp->rx_ring_size; curr++) {
		if (mp->rx_skb[curr]) {
			dev_kfree_skb(mp->rx_skb[curr]);
			mp->rx_ring_skbs--;
		}
	}

	if (mp->rx_ring_skbs)
		printk(KERN_ERR
			"%s: Error in freeing Rx Ring. %d skb's still"
			" stuck in RX Ring - ignoring them\n", dev->name,
			mp->rx_ring_skbs);
	/* Free RX ring */
	if (mp->rx_sram_size)
		iounmap(mp->p_rx_desc_area);
	else
		dma_free_coherent(NULL, mp->rx_desc_area_size,
				mp->p_rx_desc_area, mp->rx_desc_dma);
}

/*
 * mv643xx_eth_stop
 *
 * This function is used when closing the network device.
 * It updates the hardware,
 * release all memory that holds buffers and descriptors and release the IRQ.
 * Input :	a pointer to the device structure
 * Output :	zero if success , nonzero if fails
 */

/* Helper function for mv643xx_eth_stop */

static int mv643xx_eth_real_stop(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	unsigned int port_num = mp->port_num;

	netif_carrier_off(dev);
	netif_stop_queue(dev);

	mv643xx_eth_free_tx_rings(dev);
	mv643xx_eth_free_rx_rings(dev);

	eth_port_reset(mp->port_num);

	/* Disable ethernet port interrupts */
	mv_write(MV643XX_ETH_INTERRUPT_CAUSE_REG(port_num), 0);
	mv_write(MV643XX_ETH_INTERRUPT_CAUSE_EXTEND_REG(port_num), 0);

	/* Mask RX buffer and TX end interrupt */
	mv_write(MV643XX_ETH_INTERRUPT_MASK_REG(port_num), 0);

	/* Mask phy and link status changes interrupts */
	mv_write(MV643XX_ETH_INTERRUPT_EXTEND_MASK_REG(port_num), 0);

	return 0;
}

static int mv643xx_eth_stop(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);

	spin_lock_irq(&mp->lock);

	mv643xx_eth_real_stop(dev);

	free_irq(dev->irq, dev);
	spin_unlock_irq(&mp->lock);

	return 0;
}

#ifdef MV643XX_NAPI
static void mv643xx_tx(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	struct pkt_info pkt_info;

	while (eth_tx_return_desc(mp, &pkt_info) == ETH_OK) {
		if (pkt_info.return_info) {
			if (skb_shinfo(pkt_info.return_info)->nr_frags)
				dma_unmap_page(NULL, pkt_info.buf_ptr,
						pkt_info.byte_cnt,
						DMA_TO_DEVICE);
			else
				dma_unmap_single(NULL, pkt_info.buf_ptr,
						pkt_info.byte_cnt,
						DMA_TO_DEVICE);

			dev_kfree_skb_irq(pkt_info.return_info);
		} else
			dma_unmap_page(NULL, pkt_info.buf_ptr,
					pkt_info.byte_cnt, DMA_TO_DEVICE);
	}

	if (netif_queue_stopped(dev) &&
			mp->tx_ring_size > mp->tx_ring_skbs + MAX_DESCS_PER_SKB)
		netif_wake_queue(dev);
}

/*
 * mv643xx_poll
 *
 * This function is used in case of NAPI
 */
static int mv643xx_poll(struct net_device *dev, int *budget)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	int done = 1, orig_budget, work_done;
	unsigned int port_num = mp->port_num;
	unsigned long flags;

#ifdef MV643XX_TX_FAST_REFILL
	if (++mp->tx_clean_threshold > 5) {
		spin_lock_irqsave(&mp->lock, flags);
		mv643xx_tx(dev);
		mp->tx_clean_threshold = 0;
		spin_unlock_irqrestore(&mp->lock, flags);
	}
#endif

	if ((mv_read(MV643XX_ETH_RX_CURRENT_QUEUE_DESC_PTR_0(port_num)))
						!= (u32) mp->rx_used_desc_q) {
		orig_budget = *budget;
		if (orig_budget > dev->quota)
			orig_budget = dev->quota;
		work_done = mv643xx_eth_receive_queue(dev, orig_budget);
		mp->rx_task.func(dev);
		*budget -= work_done;
		dev->quota -= work_done;
		if (work_done >= orig_budget)
			done = 0;
	}

	if (done) {
		spin_lock_irqsave(&mp->lock, flags);
		__netif_rx_complete(dev);
		mv_write(MV643XX_ETH_INTERRUPT_CAUSE_REG(port_num), 0);
		mv_write(MV643XX_ETH_INTERRUPT_CAUSE_EXTEND_REG(port_num), 0);
		mv_write(MV643XX_ETH_INTERRUPT_MASK_REG(port_num),
						INT_CAUSE_UNMASK_ALL);
		mv_write(MV643XX_ETH_INTERRUPT_EXTEND_MASK_REG(port_num),
						INT_CAUSE_UNMASK_ALL_EXT);
		spin_unlock_irqrestore(&mp->lock, flags);
	}

	return done ? 0 : 1;
}
#endif

/*
 * mv643xx_eth_start_xmit
 *
 * This function is queues a packet in the Tx descriptor for
 * required port.
 *
 * Input :	skb - a pointer to socket buffer
 *		dev - a pointer to the required port
 *
 * Output :	zero upon success
 */
static int mv643xx_eth_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	struct net_device_stats *stats = &mp->stats;
	ETH_FUNC_RET_STATUS status;
	unsigned long flags;
	struct pkt_info pkt_info;

	if (netif_queue_stopped(dev)) {
		printk(KERN_ERR
			"%s: Tried sending packet when interface is stopped\n",
			dev->name);
		return 1;
	}

	/* This is a hard error, log it. */
	if ((mp->tx_ring_size - mp->tx_ring_skbs) <=
					(skb_shinfo(skb)->nr_frags + 1)) {
		netif_stop_queue(dev);
		printk(KERN_ERR
			"%s: Bug in mv643xx_eth - Trying to transmit when"
			" queue full !\n", dev->name);
		return 1;
	}

	/* Paranoid check - this shouldn't happen */
	if (skb == NULL) {
		stats->tx_dropped++;
		printk(KERN_ERR "mv64320_eth paranoid check failed\n");
		return 1;
	}

	spin_lock_irqsave(&mp->lock, flags);

	/* Update packet info data structure -- DMA owned, first last */
#ifdef MV643XX_CHECKSUM_OFFLOAD_TX
	if (!skb_shinfo(skb)->nr_frags) {
linear:
		if (skb->ip_summed != CHECKSUM_HW) {
			/* Errata BTS #50, IHL must be 5 if no HW checksum */
			pkt_info.cmd_sts = ETH_TX_ENABLE_INTERRUPT |
					   ETH_TX_FIRST_DESC |
					   ETH_TX_LAST_DESC |
					   5 << ETH_TX_IHL_SHIFT;
			pkt_info.l4i_chk = 0;
		} else {

			pkt_info.cmd_sts = ETH_TX_ENABLE_INTERRUPT |
					   ETH_TX_FIRST_DESC |
					   ETH_TX_LAST_DESC |
					   ETH_GEN_TCP_UDP_CHECKSUM |
					   ETH_GEN_IP_V_4_CHECKSUM |
					   skb->nh.iph->ihl << ETH_TX_IHL_SHIFT;
			/* CPU already calculated pseudo header checksum. */
			if (skb->nh.iph->protocol == IPPROTO_UDP) {
				pkt_info.cmd_sts |= ETH_UDP_FRAME;
				pkt_info.l4i_chk = skb->h.uh->check;
			} else if (skb->nh.iph->protocol == IPPROTO_TCP)
				pkt_info.l4i_chk = skb->h.th->check;
			else {
				printk(KERN_ERR
					"%s: chksum proto != TCP or UDP\n",
					dev->name);
				spin_unlock_irqrestore(&mp->lock, flags);
				return 1;
			}
		}
		pkt_info.byte_cnt = skb->len;
		pkt_info.buf_ptr = dma_map_single(NULL, skb->data, skb->len,
							DMA_TO_DEVICE);
		pkt_info.return_info = skb;
		status = eth_port_send(mp, &pkt_info);
		if ((status == ETH_ERROR) || (status == ETH_QUEUE_FULL))
			printk(KERN_ERR "%s: Error on transmitting packet\n",
								dev->name);
		stats->tx_bytes += pkt_info.byte_cnt;
	} else {
		unsigned int frag;

		/* Since hardware can't handle unaligned fragments smaller
		 * than 9 bytes, if we find any, we linearize the skb
		 * and start again.  When I've seen it, it's always been
		 * the first frag (probably near the end of the page),
		 * but we check all frags to be safe.
		 */
		for (frag = 0; frag < skb_shinfo(skb)->nr_frags; frag++) {
			skb_frag_t *fragp;

			fragp = &skb_shinfo(skb)->frags[frag];
			if (fragp->size <= 8 && fragp->page_offset & 0x7) {
				skb_linearize(skb, GFP_ATOMIC);
				printk(KERN_DEBUG "%s: unaligned tiny fragment"
						"%d of %d, fixed\n",
						dev->name, frag,
						skb_shinfo(skb)->nr_frags);
				goto linear;
			}
		}

		/* first frag which is skb header */
		pkt_info.byte_cnt = skb_headlen(skb);
		pkt_info.buf_ptr = dma_map_single(NULL, skb->data,
							skb_headlen(skb),
							DMA_TO_DEVICE);
		pkt_info.l4i_chk = 0;
		pkt_info.return_info = 0;

		if (skb->ip_summed != CHECKSUM_HW)
			/* Errata BTS #50, IHL must be 5 if no HW checksum */
			pkt_info.cmd_sts = ETH_TX_FIRST_DESC |
					   5 << ETH_TX_IHL_SHIFT;
		else {
			pkt_info.cmd_sts = ETH_TX_FIRST_DESC |
					   ETH_GEN_TCP_UDP_CHECKSUM |
					   ETH_GEN_IP_V_4_CHECKSUM |
					   skb->nh.iph->ihl << ETH_TX_IHL_SHIFT;
			/* CPU already calculated pseudo header checksum. */
			if (skb->nh.iph->protocol == IPPROTO_UDP) {
				pkt_info.cmd_sts |= ETH_UDP_FRAME;
				pkt_info.l4i_chk = skb->h.uh->check;
			} else if (skb->nh.iph->protocol == IPPROTO_TCP)
				pkt_info.l4i_chk = skb->h.th->check;
			else {
				printk(KERN_ERR
					"%s: chksum proto != TCP or UDP\n",
					dev->name);
				spin_unlock_irqrestore(&mp->lock, flags);
				return 1;
			}
		}

		status = eth_port_send(mp, &pkt_info);
		if (status != ETH_OK) {
			if ((status == ETH_ERROR))
				printk(KERN_ERR
					"%s: Error on transmitting packet\n",
					dev->name);
			if (status == ETH_QUEUE_FULL)
				printk("Error on Queue Full \n");
			if (status == ETH_QUEUE_LAST_RESOURCE)
				printk("Tx resource error \n");
		}
		stats->tx_bytes += pkt_info.byte_cnt;

		/* Check for the remaining frags */
		for (frag = 0; frag < skb_shinfo(skb)->nr_frags; frag++) {
			skb_frag_t *this_frag = &skb_shinfo(skb)->frags[frag];
			pkt_info.l4i_chk = 0x0000;
			pkt_info.cmd_sts = 0x00000000;

			/* Last Frag enables interrupt and frees the skb */
			if (frag == (skb_shinfo(skb)->nr_frags - 1)) {
				pkt_info.cmd_sts |= ETH_TX_ENABLE_INTERRUPT |
							ETH_TX_LAST_DESC;
				pkt_info.return_info = skb;
			} else {
				pkt_info.return_info = 0;
			}
			pkt_info.l4i_chk = 0;
			pkt_info.byte_cnt = this_frag->size;

			pkt_info.buf_ptr = dma_map_page(NULL, this_frag->page,
							this_frag->page_offset,
							this_frag->size,
							DMA_TO_DEVICE);

			status = eth_port_send(mp, &pkt_info);

			if (status != ETH_OK) {
				if ((status == ETH_ERROR))
					printk(KERN_ERR "%s: Error on "
							"transmitting packet\n",
							dev->name);

				if (status == ETH_QUEUE_LAST_RESOURCE)
					printk("Tx resource error \n");

				if (status == ETH_QUEUE_FULL)
					printk("Queue is full \n");
			}
			stats->tx_bytes += pkt_info.byte_cnt;
		}
	}
#else
	pkt_info.cmd_sts = ETH_TX_ENABLE_INTERRUPT | ETH_TX_FIRST_DESC |
							ETH_TX_LAST_DESC;
	pkt_info.l4i_chk = 0;
	pkt_info.byte_cnt = skb->len;
	pkt_info.buf_ptr = dma_map_single(NULL, skb->data, skb->len,
								DMA_TO_DEVICE);
	pkt_info.return_info = skb;
	status = eth_port_send(mp, &pkt_info);
	if ((status == ETH_ERROR) || (status == ETH_QUEUE_FULL))
		printk(KERN_ERR "%s: Error on transmitting packet\n",
								dev->name);
	stats->tx_bytes += pkt_info.byte_cnt;
#endif

	/* Check if TX queue can handle another skb. If not, then
	 * signal higher layers to stop requesting TX
	 */
	if (mp->tx_ring_size <= (mp->tx_ring_skbs + MAX_DESCS_PER_SKB))
		/*
		 * Stop getting skb's from upper layers.
		 * Getting skb's from upper layers will be enabled again after
		 * packets are released.
		 */
		netif_stop_queue(dev);

	/* Update statistics and start of transmittion time */
	stats->tx_packets++;
	dev->trans_start = jiffies;

	spin_unlock_irqrestore(&mp->lock, flags);

	return 0;		/* success */
}

/*
 * mv643xx_eth_get_stats
 *
 * Returns a pointer to the interface statistics.
 *
 * Input :	dev - a pointer to the required interface
 *
 * Output :	a pointer to the interface's statistics
 */

static struct net_device_stats *mv643xx_eth_get_stats(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);

	return &mp->stats;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static inline void mv643xx_enable_irq(struct mv643xx_private *mp)
{
	int port_num = mp->port_num;
	unsigned long flags;

	spin_lock_irqsave(&mp->lock, flags);
	mv_write(MV643XX_ETH_INTERRUPT_MASK_REG(port_num),
					INT_CAUSE_UNMASK_ALL);
	mv_write(MV643XX_ETH_INTERRUPT_EXTEND_MASK_REG(port_num),
					INT_CAUSE_UNMASK_ALL_EXT);
	spin_unlock_irqrestore(&mp->lock, flags);
}

static inline void mv643xx_disable_irq(struct mv643xx_private *mp)
{
	int port_num = mp->port_num;
	unsigned long flags;

	spin_lock_irqsave(&mp->lock, flags);
	mv_write(MV643XX_ETH_INTERRUPT_MASK_REG(port_num),
					INT_CAUSE_MASK_ALL);
	mv_write(MV643XX_ETH_INTERRUPT_EXTEND_MASK_REG(port_num),
					INT_CAUSE_MASK_ALL_EXT);
	spin_unlock_irqrestore(&mp->lock, flags);
}

static void mv643xx_netpoll(struct net_device *netdev)
{
	struct mv643xx_private *mp = netdev_priv(netdev);

	mv643xx_disable_irq(mp);
	mv643xx_eth_int_handler(netdev->irq, netdev, NULL);
	mv643xx_enable_irq(mp);
}
#endif

/*/
 * mv643xx_eth_probe
 *
 * First function called after registering the network device.
 * It's purpose is to initialize the device as an ethernet device,
 * fill the ethernet device structure with pointers * to functions,
 * and set the MAC address of the interface
 *
 * Input :	struct device *
 * Output :	-ENOMEM if failed , 0 if success
 */
static int mv643xx_eth_probe(struct platform_device *pdev)
{
	struct mv643xx_eth_platform_data *pd;
	int port_num = pdev->id;
	struct mv643xx_private *mp;
	struct net_device *dev;
	u8 *p;
	struct resource *res;
	int err;

	dev = alloc_etherdev(sizeof(struct mv643xx_private));
	if (!dev)
		return -ENOMEM;

	platform_set_drvdata(pdev, dev);

	mp = netdev_priv(dev);

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	BUG_ON(!res);
	dev->irq = res->start;

	mp->port_num = port_num;

	dev->open = mv643xx_eth_open;
	dev->stop = mv643xx_eth_stop;
	dev->hard_start_xmit = mv643xx_eth_start_xmit;
	dev->get_stats = mv643xx_eth_get_stats;
	dev->set_mac_address = mv643xx_eth_set_mac_address;
	dev->set_multicast_list = mv643xx_eth_set_rx_mode;

	/* No need to Tx Timeout */
	dev->tx_timeout = mv643xx_eth_tx_timeout;
#ifdef MV643XX_NAPI
	dev->poll = mv643xx_poll;
	dev->weight = 64;
#endif

#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = mv643xx_netpoll;
#endif

	dev->watchdog_timeo = 2 * HZ;
	dev->tx_queue_len = mp->tx_ring_size;
	dev->base_addr = 0;
	dev->change_mtu = mv643xx_eth_change_mtu;
	SET_ETHTOOL_OPS(dev, &mv643xx_ethtool_ops);

#ifdef MV643XX_CHECKSUM_OFFLOAD_TX
#ifdef MAX_SKB_FRAGS
	/*
	 * Zero copy can only work if we use Discovery II memory. Else, we will
	 * have to map the buffers to ISA memory which is only 16 MB
	 */
	dev->features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_HW_CSUM;
#endif
#endif

	/* Configure the timeout task */
	INIT_WORK(&mp->tx_timeout_task,
			(void (*)(void *))mv643xx_eth_tx_timeout_task, dev);

	spin_lock_init(&mp->lock);

	/* set default config values */
	eth_port_uc_addr_get(dev, dev->dev_addr);
	mp->port_config = MV643XX_ETH_PORT_CONFIG_DEFAULT_VALUE;
	mp->port_config_extend = MV643XX_ETH_PORT_CONFIG_EXTEND_DEFAULT_VALUE;
	mp->port_sdma_config = MV643XX_ETH_PORT_SDMA_CONFIG_DEFAULT_VALUE;
	mp->port_serial_control = MV643XX_ETH_PORT_SERIAL_CONTROL_DEFAULT_VALUE;
	mp->rx_ring_size = MV643XX_ETH_PORT_DEFAULT_RECEIVE_QUEUE_SIZE;
	mp->tx_ring_size = MV643XX_ETH_PORT_DEFAULT_TRANSMIT_QUEUE_SIZE;

	pd = pdev->dev.platform_data;
	if (pd) {
		if (pd->mac_addr != NULL)
			memcpy(dev->dev_addr, pd->mac_addr, 6);

		if (pd->phy_addr || pd->force_phy_addr)
			ethernet_phy_set(port_num, pd->phy_addr);

		if (pd->port_config || pd->force_port_config)
			mp->port_config = pd->port_config;

		if (pd->port_config_extend || pd->force_port_config_extend)
			mp->port_config_extend = pd->port_config_extend;

		if (pd->port_sdma_config || pd->force_port_sdma_config)
			mp->port_sdma_config = pd->port_sdma_config;

		if (pd->port_serial_control || pd->force_port_serial_control)
			mp->port_serial_control = pd->port_serial_control;

		if (pd->rx_queue_size)
			mp->rx_ring_size = pd->rx_queue_size;

		if (pd->tx_queue_size)
			mp->tx_ring_size = pd->tx_queue_size;

		if (pd->tx_sram_size) {
			mp->tx_sram_size = pd->tx_sram_size;
			mp->tx_sram_addr = pd->tx_sram_addr;
		}

		if (pd->rx_sram_size) {
			mp->rx_sram_size = pd->rx_sram_size;
			mp->rx_sram_addr = pd->rx_sram_addr;
		}
	}

	err = ethernet_phy_detect(port_num);
	if (err) {
		pr_debug("MV643xx ethernet port %d: "
					"No PHY detected at addr %d\n",
					port_num, ethernet_phy_get(port_num));
		return err;
	}

	err = register_netdev(dev);
	if (err)
		goto out;

	p = dev->dev_addr;
	printk(KERN_NOTICE
		"%s: port %d with MAC address %02x:%02x:%02x:%02x:%02x:%02x\n",
		dev->name, port_num, p[0], p[1], p[2], p[3], p[4], p[5]);

	if (dev->features & NETIF_F_SG)
		printk(KERN_NOTICE "%s: Scatter Gather Enabled\n", dev->name);

	if (dev->features & NETIF_F_IP_CSUM)
		printk(KERN_NOTICE "%s: TX TCP/IP Checksumming Supported\n",
								dev->name);

#ifdef MV643XX_CHECKSUM_OFFLOAD_TX
	printk(KERN_NOTICE "%s: RX TCP/UDP Checksum Offload ON \n", dev->name);
#endif

#ifdef MV643XX_COAL
	printk(KERN_NOTICE "%s: TX and RX Interrupt Coalescing ON \n",
								dev->name);
#endif

#ifdef MV643XX_NAPI
	printk(KERN_NOTICE "%s: RX NAPI Enabled \n", dev->name);
#endif

	if (mp->tx_sram_size > 0)
		printk(KERN_NOTICE "%s: Using SRAM\n", dev->name);

	return 0;

out:
	free_netdev(dev);

	return err;
}

static int mv643xx_eth_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);

	unregister_netdev(dev);
	flush_scheduled_work();

	free_netdev(dev);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static int mv643xx_eth_shared_probe(struct platform_device *pdev)
{
	struct resource *res;

	printk(KERN_NOTICE "MV-643xx 10/100/1000 Ethernet Driver\n");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL)
		return -ENODEV;

	mv643xx_eth_shared_base = ioremap(res->start,
						MV643XX_ETH_SHARED_REGS_SIZE);
	if (mv643xx_eth_shared_base == NULL)
		return -ENOMEM;

	return 0;

}

static int mv643xx_eth_shared_remove(struct platform_device *pdev)
{
	iounmap(mv643xx_eth_shared_base);
	mv643xx_eth_shared_base = NULL;

	return 0;
}

static struct platform_driver mv643xx_eth_driver = {
	.probe = mv643xx_eth_probe,
	.remove = mv643xx_eth_remove,
	.driver = {
		.name = MV643XX_ETH_NAME,
	},
};

static struct platform_driver mv643xx_eth_shared_driver = {
	.probe = mv643xx_eth_shared_probe,
	.remove = mv643xx_eth_shared_remove,
	.driver = {
		.name = MV643XX_ETH_SHARED_NAME,
	},
};

/*
 * mv643xx_init_module
 *
 * Registers the network drivers into the Linux kernel
 *
 * Input :	N/A
 *
 * Output :	N/A
 */
static int __init mv643xx_init_module(void)
{
	int rc;

	rc = platform_driver_register(&mv643xx_eth_shared_driver);
	if (!rc) {
		rc = platform_driver_register(&mv643xx_eth_driver);
		if (rc)
			platform_driver_unregister(&mv643xx_eth_shared_driver);
	}
	return rc;
}

/*
 * mv643xx_cleanup_module
 *
 * Registers the network drivers into the Linux kernel
 *
 * Input :	N/A
 *
 * Output :	N/A
 */
static void __exit mv643xx_cleanup_module(void)
{
	platform_driver_unregister(&mv643xx_eth_driver);
	platform_driver_unregister(&mv643xx_eth_shared_driver);
}

module_init(mv643xx_init_module);
module_exit(mv643xx_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(	"Rabeeh Khoury, Assaf Hoffman, Matthew Dharm, Manish Lachwani"
		" and Dale Farnsworth");
MODULE_DESCRIPTION("Ethernet driver for Marvell MV643XX");

/*
 * The second part is the low level driver of the gigE ethernet ports.
 */

/*
 * Marvell's Gigabit Ethernet controller low level driver
 *
 * DESCRIPTION:
 *	This file introduce low level API to Marvell's Gigabit Ethernet
 *		controller. This Gigabit Ethernet Controller driver API controls
 *		1) Operations (i.e. port init, start, reset etc').
 *		2) Data flow (i.e. port send, receive etc').
 *		Each Gigabit Ethernet port is controlled via
 *		struct mv643xx_private.
 *		This struct includes user configuration information as well as
 *		driver internal data needed for its operations.
 *
 *		Supported Features:
 *		- This low level driver is OS independent. Allocating memory for
 *		  the descriptor rings and buffers are not within the scope of
 *		  this driver.
 *		- The user is free from Rx/Tx queue managing.
 *		- This low level driver introduce functionality API that enable
 *		  the to operate Marvell's Gigabit Ethernet Controller in a
 *		  convenient way.
 *		- Simple Gigabit Ethernet port operation API.
 *		- Simple Gigabit Ethernet port data flow API.
 *		- Data flow and operation API support per queue functionality.
 *		- Support cached descriptors for better performance.
 *		- Enable access to all four DRAM banks and internal SRAM memory
 *		  spaces.
 *		- PHY access and control API.
 *		- Port control register configuration API.
 *		- Full control over Unicast and Multicast MAC configurations.
 *
 *		Operation flow:
 *
 *		Initialization phase
 *		This phase complete the initialization of the the
 *		mv643xx_private struct.
 *		User information regarding port configuration has to be set
 *		prior to calling the port initialization routine.
 *
 *		In this phase any port Tx/Rx activity is halted, MIB counters
 *		are cleared, PHY address is set according to user parameter and
 *		access to DRAM and internal SRAM memory spaces.
 *
 *		Driver ring initialization
 *		Allocating memory for the descriptor rings and buffers is not
 *		within the scope of this driver. Thus, the user is required to
 *		allocate memory for the descriptors ring and buffers. Those
 *		memory parameters are used by the Rx and Tx ring initialization
 *		routines in order to curve the descriptor linked list in a form
 *		of a ring.
 *		Note: Pay special attention to alignment issues when using
 *		cached descriptors/buffers. In this phase the driver store
 *		information in the mv643xx_private struct regarding each queue
 *		ring.
 *
 *		Driver start
 *		This phase prepares the Ethernet port for Rx and Tx activity.
 *		It uses the information stored in the mv643xx_private struct to
 *		initialize the various port registers.
 *
 *		Data flow:
 *		All packet references to/from the driver are done using
 *		struct pkt_info.
 *		This struct is a unified struct used with Rx and Tx operations.
 *		This way the user is not required to be familiar with neither
 *		Tx nor Rx descriptors structures.
 *		The driver's descriptors rings are management by indexes.
 *		Those indexes controls the ring resources and used to indicate
 *		a SW resource error:
 *		'current'
 *		This index points to the current available resource for use. For
 *		example in Rx process this index will point to the descriptor
 *		that will be passed to the user upon calling the receive
 *		routine.  In Tx process, this index will point to the descriptor
 *		that will be assigned with the user packet info and transmitted.
 *		'used'
 *		This index points to the descriptor that need to restore its
 *		resources. For example in Rx process, using the Rx buffer return
 *		API will attach the buffer returned in packet info to the
 *		descriptor pointed by 'used'. In Tx process, using the Tx
 *		descriptor return will merely return the user packet info with
 *		the command status of the transmitted buffer pointed by the
 *		'used' index. Nevertheless, it is essential to use this routine
 *		to update the 'used' index.
 *		'first'
 *		This index supports Tx Scatter-Gather. It points to the first
 *		descriptor of a packet assembled of multiple buffers. For
 *		example when in middle of Such packet we have a Tx resource
 *		error the 'curr' index get the value of 'first' to indicate
 *		that the ring returned to its state before trying to transmit
 *		this packet.
 *
 *		Receive operation:
 *		The eth_port_receive API set the packet information struct,
 *		passed by the caller, with received information from the
 *		'current' SDMA descriptor.
 *		It is the user responsibility to return this resource back
 *		to the Rx descriptor ring to enable the reuse of this source.
 *		Return Rx resource is done using the eth_rx_return_buff API.
 *
 *		Transmit operation:
 *		The eth_port_send API supports Scatter-Gather which enables to
 *		send a packet spanned over multiple buffers. This means that
 *		for each packet info structure given by the user and put into
 *		the Tx descriptors ring, will be transmitted only if the 'LAST'
 *		bit will be set in the packet info command status field. This
 *		API also consider restriction regarding buffer alignments and
 *		sizes.
 *		The user must return a Tx resource after ensuring the buffer
 *		has been transmitted to enable the Tx ring indexes to update.
 *
 *		BOARD LAYOUT
 *		This device is on-board.  No jumper diagram is necessary.
 *
 *		EXTERNAL INTERFACE
 *
 *	Prior to calling the initialization routine eth_port_init() the user
 *	must set the following fields under mv643xx_private struct:
 *	port_num		User Ethernet port number.
 *	port_mac_addr[6]	User defined port MAC address.
 *	port_config		User port configuration value.
 *	port_config_extend	User port config extend value.
 *	port_sdma_config	User port SDMA config value.
 *	port_serial_control	User port serial control value.
 *
 *		This driver data flow is done using the struct pkt_info which
 *		is a unified struct for Rx and Tx operations:
 *
 *		byte_cnt	Tx/Rx descriptor buffer byte count.
 *		l4i_chk		CPU provided TCP Checksum. For Tx operation
 *				only.
 *		cmd_sts		Tx/Rx descriptor command status.
 *		buf_ptr		Tx/Rx descriptor buffer pointer.
 *		return_info	Tx/Rx user resource return information.
 */

/* defines */
/* SDMA command macros */
#define ETH_ENABLE_TX_QUEUE(eth_port) \
	mv_write(MV643XX_ETH_TRANSMIT_QUEUE_COMMAND_REG(eth_port), 1)

/* locals */

/* PHY routines */
static int ethernet_phy_get(unsigned int eth_port_num);
static void ethernet_phy_set(unsigned int eth_port_num, int phy_addr);

/* Ethernet Port routines */
static int eth_port_uc_addr(unsigned int eth_port_num, unsigned char uc_nibble,
								int option);

/*
 * eth_port_init - Initialize the Ethernet port driver
 *
 * DESCRIPTION:
 *	This function prepares the ethernet port to start its activity:
 *	1) Completes the ethernet port driver struct initialization toward port
 *		start routine.
 *	2) Resets the device to a quiescent state in case of warm reboot.
 *	3) Enable SDMA access to all four DRAM banks as well as internal SRAM.
 *	4) Clean MAC tables. The reset status of those tables is unknown.
 *	5) Set PHY address.
 *	Note: Call this routine prior to eth_port_start routine and after
 *	setting user values in the user fields of Ethernet port control
 *	struct.
 *
 * INPUT:
 *	struct mv643xx_private *mp	Ethernet port control struct
 *
 * OUTPUT:
 *	See description.
 *
 * RETURN:
 *	None.
 */
static void eth_port_init(struct mv643xx_private *mp)
{
	mp->port_rx_queue_command = 0;
	mp->port_tx_queue_command = 0;

	mp->rx_resource_err = 0;
	mp->tx_resource_err = 0;

	eth_port_reset(mp->port_num);

	eth_port_init_mac_tables(mp->port_num);

	ethernet_phy_reset(mp->port_num);
}

/*
 * eth_port_start - Start the Ethernet port activity.
 *
 * DESCRIPTION:
 *	This routine prepares the Ethernet port for Rx and Tx activity:
 *	 1. Initialize Tx and Rx Current Descriptor Pointer for each queue that
 *	    has been initialized a descriptor's ring (using
 *	    ether_init_tx_desc_ring for Tx and ether_init_rx_desc_ring for Rx)
 *	 2. Initialize and enable the Ethernet configuration port by writing to
 *	    the port's configuration and command registers.
 *	 3. Initialize and enable the SDMA by writing to the SDMA's
 *	    configuration and command registers.  After completing these steps,
 *	    the ethernet port SDMA can starts to perform Rx and Tx activities.
 *
 *	Note: Each Rx and Tx queue descriptor's list must be initialized prior
 *	to calling this function (use ether_init_tx_desc_ring for Tx queues
 *	and ether_init_rx_desc_ring for Rx queues).
 *
 * INPUT:
 *	struct mv643xx_private *mp	Ethernet port control struct
 *
 * OUTPUT:
 *	Ethernet port is ready to receive and transmit.
 *
 * RETURN:
 *	None.
 */
static void eth_port_start(struct mv643xx_private *mp)
{
	unsigned int port_num = mp->port_num;
	int tx_curr_desc, rx_curr_desc;

	/* Assignment of Tx CTRP of given queue */
	tx_curr_desc = mp->tx_curr_desc_q;
	mv_write(MV643XX_ETH_TX_CURRENT_QUEUE_DESC_PTR_0(port_num),
		(u32)((struct eth_tx_desc *)mp->tx_desc_dma + tx_curr_desc));

	/* Assignment of Rx CRDP of given queue */
	rx_curr_desc = mp->rx_curr_desc_q;
	mv_write(MV643XX_ETH_RX_CURRENT_QUEUE_DESC_PTR_0(port_num),
		(u32)((struct eth_rx_desc *)mp->rx_desc_dma + rx_curr_desc));

	/* Add the assigned Ethernet address to the port's address table */
	eth_port_uc_addr_set(port_num, mp->port_mac_addr);

	/* Assign port configuration and command. */
	mv_write(MV643XX_ETH_PORT_CONFIG_REG(port_num), mp->port_config);

	mv_write(MV643XX_ETH_PORT_CONFIG_EXTEND_REG(port_num),
						mp->port_config_extend);


	/* Increase the Rx side buffer size if supporting GigE */
	if (mp->port_serial_control & MV643XX_ETH_SET_GMII_SPEED_TO_1000)
		mv_write(MV643XX_ETH_PORT_SERIAL_CONTROL_REG(port_num),
			(mp->port_serial_control & 0xfff1ffff) | (0x5 << 17));
	else
		mv_write(MV643XX_ETH_PORT_SERIAL_CONTROL_REG(port_num),
						mp->port_serial_control);

	mv_write(MV643XX_ETH_PORT_SERIAL_CONTROL_REG(port_num),
		mv_read(MV643XX_ETH_PORT_SERIAL_CONTROL_REG(port_num)) |
						MV643XX_ETH_SERIAL_PORT_ENABLE);

	/* Assign port SDMA configuration */
	mv_write(MV643XX_ETH_SDMA_CONFIG_REG(port_num),
							mp->port_sdma_config);

	/* Enable port Rx. */
	mv_write(MV643XX_ETH_RECEIVE_QUEUE_COMMAND_REG(port_num),
						mp->port_rx_queue_command);

	/* Disable port bandwidth limits by clearing MTU register */
	mv_write(MV643XX_ETH_MAXIMUM_TRANSMIT_UNIT(port_num), 0);
}

/*
 * eth_port_uc_addr_set - This function Set the port Unicast address.
 *
 * DESCRIPTION:
 *		This function Set the port Ethernet MAC address.
 *
 * INPUT:
 *	unsigned int	eth_port_num	Port number.
 *	char *		p_addr		Address to be set
 *
 * OUTPUT:
 *	Set MAC address low and high registers. also calls eth_port_uc_addr()
 *	To set the unicast table with the proper information.
 *
 * RETURN:
 *	N/A.
 *
 */
static void eth_port_uc_addr_set(unsigned int eth_port_num,
							unsigned char *p_addr)
{
	unsigned int mac_h;
	unsigned int mac_l;

	mac_l = (p_addr[4] << 8) | (p_addr[5]);
	mac_h = (p_addr[0] << 24) | (p_addr[1] << 16) | (p_addr[2] << 8) |
							(p_addr[3] << 0);

	mv_write(MV643XX_ETH_MAC_ADDR_LOW(eth_port_num), mac_l);
	mv_write(MV643XX_ETH_MAC_ADDR_HIGH(eth_port_num), mac_h);

	/* Accept frames of this address */
	eth_port_uc_addr(eth_port_num, p_addr[5], ACCEPT_MAC_ADDR);

	return;
}

/*
 * eth_port_uc_addr_get - This function retrieves the port Unicast address
 * (MAC address) from the ethernet hw registers.
 *
 * DESCRIPTION:
 *		This function retrieves the port Ethernet MAC address.
 *
 * INPUT:
 *	unsigned int	eth_port_num	Port number.
 *	char		*MacAddr	pointer where the MAC address is stored
 *
 * OUTPUT:
 *	Copy the MAC address to the location pointed to by MacAddr
 *
 * RETURN:
 *	N/A.
 *
 */
static void eth_port_uc_addr_get(struct net_device *dev, unsigned char *p_addr)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	unsigned int mac_h;
	unsigned int mac_l;

	mac_h = mv_read(MV643XX_ETH_MAC_ADDR_HIGH(mp->port_num));
	mac_l = mv_read(MV643XX_ETH_MAC_ADDR_LOW(mp->port_num));

	p_addr[0] = (mac_h >> 24) & 0xff;
	p_addr[1] = (mac_h >> 16) & 0xff;
	p_addr[2] = (mac_h >> 8) & 0xff;
	p_addr[3] = mac_h & 0xff;
	p_addr[4] = (mac_l >> 8) & 0xff;
	p_addr[5] = mac_l & 0xff;
}

/*
 * eth_port_uc_addr - This function Set the port unicast address table
 *
 * DESCRIPTION:
 *	This function locates the proper entry in the Unicast table for the
 *	specified MAC nibble and sets its properties according to function
 *	parameters.
 *
 * INPUT:
 *	unsigned int	eth_port_num	Port number.
 *	unsigned char	uc_nibble	Unicast MAC Address last nibble.
 *	int 		option		0 = Add, 1 = remove address.
 *
 * OUTPUT:
 *	This function add/removes MAC addresses from the port unicast address
 *	table.
 *
 * RETURN:
 *	true is output succeeded.
 *	false if option parameter is invalid.
 *
 */
static int eth_port_uc_addr(unsigned int eth_port_num, unsigned char uc_nibble,
								int option)
{
	unsigned int unicast_reg;
	unsigned int tbl_offset;
	unsigned int reg_offset;

	/* Locate the Unicast table entry */
	uc_nibble = (0xf & uc_nibble);
	tbl_offset = (uc_nibble / 4) * 4;	/* Register offset from unicast table base */
	reg_offset = uc_nibble % 4;	/* Entry offset within the above register */

	switch (option) {
	case REJECT_MAC_ADDR:
		/* Clear accepts frame bit at given unicast DA table entry */
		unicast_reg = mv_read((MV643XX_ETH_DA_FILTER_UNICAST_TABLE_BASE
						(eth_port_num) + tbl_offset));

		unicast_reg &= (0x0E << (8 * reg_offset));

		mv_write((MV643XX_ETH_DA_FILTER_UNICAST_TABLE_BASE
				(eth_port_num) + tbl_offset), unicast_reg);
		break;

	case ACCEPT_MAC_ADDR:
		/* Set accepts frame bit at unicast DA filter table entry */
		unicast_reg =
			mv_read((MV643XX_ETH_DA_FILTER_UNICAST_TABLE_BASE
						(eth_port_num) + tbl_offset));

		unicast_reg |= (0x01 << (8 * reg_offset));

		mv_write((MV643XX_ETH_DA_FILTER_UNICAST_TABLE_BASE
				(eth_port_num) + tbl_offset), unicast_reg);

		break;

	default:
		return 0;
	}

	return 1;
}

/*
 * eth_port_init_mac_tables - Clear all entrance in the UC, SMC and OMC tables
 *
 * DESCRIPTION:
 *	Go through all the DA filter tables (Unicast, Special Multicast &
 *	Other Multicast) and set each entry to 0.
 *
 * INPUT:
 *	unsigned int	eth_port_num	Ethernet Port number.
 *
 * OUTPUT:
 *	Multicast and Unicast packets are rejected.
 *
 * RETURN:
 *	None.
 */
static void eth_port_init_mac_tables(unsigned int eth_port_num)
{
	int table_index;

	/* Clear DA filter unicast table (Ex_dFUT) */
	for (table_index = 0; table_index <= 0xC; table_index += 4)
		mv_write((MV643XX_ETH_DA_FILTER_UNICAST_TABLE_BASE
					(eth_port_num) + table_index), 0);

	for (table_index = 0; table_index <= 0xFC; table_index += 4) {
		/* Clear DA filter special multicast table (Ex_dFSMT) */
		mv_write((MV643XX_ETH_DA_FILTER_SPECIAL_MULTICAST_TABLE_BASE
					(eth_port_num) + table_index), 0);
		/* Clear DA filter other multicast table (Ex_dFOMT) */
		mv_write((MV643XX_ETH_DA_FILTER_OTHER_MULTICAST_TABLE_BASE
					(eth_port_num) + table_index), 0);
	}
}

/*
 * eth_clear_mib_counters - Clear all MIB counters
 *
 * DESCRIPTION:
 *	This function clears all MIB counters of a specific ethernet port.
 *	A read from the MIB counter will reset the counter.
 *
 * INPUT:
 *	unsigned int	eth_port_num	Ethernet Port number.
 *
 * OUTPUT:
 *	After reading all MIB counters, the counters resets.
 *
 * RETURN:
 *	MIB counter value.
 *
 */
static void eth_clear_mib_counters(unsigned int eth_port_num)
{
	int i;

	/* Perform dummy reads from MIB counters */
	for (i = ETH_MIB_GOOD_OCTETS_RECEIVED_LOW; i < ETH_MIB_LATE_COLLISION;
									i += 4)
		mv_read(MV643XX_ETH_MIB_COUNTERS_BASE(eth_port_num) + i);
}

static inline u32 read_mib(struct mv643xx_private *mp, int offset)
{
	return mv_read(MV643XX_ETH_MIB_COUNTERS_BASE(mp->port_num) + offset);
}

static void eth_update_mib_counters(struct mv643xx_private *mp)
{
	struct mv643xx_mib_counters *p = &mp->mib_counters;
	int offset;

	p->good_octets_received +=
		read_mib(mp, ETH_MIB_GOOD_OCTETS_RECEIVED_LOW);
	p->good_octets_received +=
		(u64)read_mib(mp, ETH_MIB_GOOD_OCTETS_RECEIVED_HIGH) << 32;

	for (offset = ETH_MIB_BAD_OCTETS_RECEIVED;
			offset <= ETH_MIB_FRAMES_1024_TO_MAX_OCTETS;
			offset += 4)
		*(u32 *)((char *)p + offset) = read_mib(mp, offset);

	p->good_octets_sent += read_mib(mp, ETH_MIB_GOOD_OCTETS_SENT_LOW);
	p->good_octets_sent +=
		(u64)read_mib(mp, ETH_MIB_GOOD_OCTETS_SENT_HIGH) << 32;

	for (offset = ETH_MIB_GOOD_FRAMES_SENT;
			offset <= ETH_MIB_LATE_COLLISION;
			offset += 4)
		*(u32 *)((char *)p + offset) = read_mib(mp, offset);
}

/*
 * ethernet_phy_detect - Detect whether a phy is present
 *
 * DESCRIPTION:
 *	This function tests whether there is a PHY present on
 *	the specified port.
 *
 * INPUT:
 *	unsigned int	eth_port_num	Ethernet Port number.
 *
 * OUTPUT:
 *	None
 *
 * RETURN:
 *	0 on success
 *	-ENODEV on failure
 *
 */
static int ethernet_phy_detect(unsigned int port_num)
{
	unsigned int phy_reg_data0;
	int auto_neg;

	eth_port_read_smi_reg(port_num, 0, &phy_reg_data0);
	auto_neg = phy_reg_data0 & 0x1000;
	phy_reg_data0 ^= 0x1000;	/* invert auto_neg */
	eth_port_write_smi_reg(port_num, 0, phy_reg_data0);

	eth_port_read_smi_reg(port_num, 0, &phy_reg_data0);
	if ((phy_reg_data0 & 0x1000) == auto_neg)
		return -ENODEV;				/* change didn't take */

	phy_reg_data0 ^= 0x1000;
	eth_port_write_smi_reg(port_num, 0, phy_reg_data0);
	return 0;
}

/*
 * ethernet_phy_get - Get the ethernet port PHY address.
 *
 * DESCRIPTION:
 *	This routine returns the given ethernet port PHY address.
 *
 * INPUT:
 *	unsigned int	eth_port_num	Ethernet Port number.
 *
 * OUTPUT:
 *	None.
 *
 * RETURN:
 *	PHY address.
 *
 */
static int ethernet_phy_get(unsigned int eth_port_num)
{
	unsigned int reg_data;

	reg_data = mv_read(MV643XX_ETH_PHY_ADDR_REG);

	return ((reg_data >> (5 * eth_port_num)) & 0x1f);
}

/*
 * ethernet_phy_set - Set the ethernet port PHY address.
 *
 * DESCRIPTION:
 *	This routine sets the given ethernet port PHY address.
 *
 * INPUT:
 *	unsigned int	eth_port_num	Ethernet Port number.
 *	int		phy_addr	PHY address.
 *
 * OUTPUT:
 *	None.
 *
 * RETURN:
 *	None.
 *
 */
static void ethernet_phy_set(unsigned int eth_port_num, int phy_addr)
{
	u32 reg_data;
	int addr_shift = 5 * eth_port_num;

	reg_data = mv_read(MV643XX_ETH_PHY_ADDR_REG);
	reg_data &= ~(0x1f << addr_shift);
	reg_data |= (phy_addr & 0x1f) << addr_shift;
	mv_write(MV643XX_ETH_PHY_ADDR_REG, reg_data);
}

/*
 * ethernet_phy_reset - Reset Ethernet port PHY.
 *
 * DESCRIPTION:
 *	This routine utilizes the SMI interface to reset the ethernet port PHY.
 *
 * INPUT:
 *	unsigned int	eth_port_num	Ethernet Port number.
 *
 * OUTPUT:
 *	The PHY is reset.
 *
 * RETURN:
 *	None.
 *
 */
static void ethernet_phy_reset(unsigned int eth_port_num)
{
	unsigned int phy_reg_data;

	/* Reset the PHY */
	eth_port_read_smi_reg(eth_port_num, 0, &phy_reg_data);
	phy_reg_data |= 0x8000;	/* Set bit 15 to reset the PHY */
	eth_port_write_smi_reg(eth_port_num, 0, phy_reg_data);
}

/*
 * eth_port_reset - Reset Ethernet port
 *
 * DESCRIPTION:
 * 	This routine resets the chip by aborting any SDMA engine activity and
 *	clearing the MIB counters. The Receiver and the Transmit unit are in
 *	idle state after this command is performed and the port is disabled.
 *
 * INPUT:
 *	unsigned int	eth_port_num	Ethernet Port number.
 *
 * OUTPUT:
 *	Channel activity is halted.
 *
 * RETURN:
 *	None.
 *
 */
static void eth_port_reset(unsigned int port_num)
{
	unsigned int reg_data;

	/* Stop Tx port activity. Check port Tx activity. */
	reg_data = mv_read(MV643XX_ETH_TRANSMIT_QUEUE_COMMAND_REG(port_num));

	if (reg_data & 0xFF) {
		/* Issue stop command for active channels only */
		mv_write(MV643XX_ETH_TRANSMIT_QUEUE_COMMAND_REG(port_num),
							(reg_data << 8));

		/* Wait for all Tx activity to terminate. */
		/* Check port cause register that all Tx queues are stopped */
		while (mv_read(MV643XX_ETH_TRANSMIT_QUEUE_COMMAND_REG(port_num))
									& 0xFF)
			udelay(10);
	}

	/* Stop Rx port activity. Check port Rx activity. */
	reg_data = mv_read(MV643XX_ETH_RECEIVE_QUEUE_COMMAND_REG(port_num));

	if (reg_data & 0xFF) {
		/* Issue stop command for active channels only */
		mv_write(MV643XX_ETH_RECEIVE_QUEUE_COMMAND_REG(port_num),
							(reg_data << 8));

		/* Wait for all Rx activity to terminate. */
		/* Check port cause register that all Rx queues are stopped */
		while (mv_read(MV643XX_ETH_RECEIVE_QUEUE_COMMAND_REG(port_num))
									& 0xFF)
			udelay(10);
	}

	/* Clear all MIB counters */
	eth_clear_mib_counters(port_num);

	/* Reset the Enable bit in the Configuration Register */
	reg_data = mv_read(MV643XX_ETH_PORT_SERIAL_CONTROL_REG(port_num));
	reg_data &= ~MV643XX_ETH_SERIAL_PORT_ENABLE;
	mv_write(MV643XX_ETH_PORT_SERIAL_CONTROL_REG(port_num), reg_data);
}


static int eth_port_autoneg_supported(unsigned int eth_port_num)
{
	unsigned int phy_reg_data0;

	eth_port_read_smi_reg(eth_port_num, 0, &phy_reg_data0);

	return phy_reg_data0 & 0x1000;
}

static int eth_port_link_is_up(unsigned int eth_port_num)
{
	unsigned int phy_reg_data1;

	eth_port_read_smi_reg(eth_port_num, 1, &phy_reg_data1);

	if (eth_port_autoneg_supported(eth_port_num)) {
		if (phy_reg_data1 & 0x20)	/* auto-neg complete */
			return 1;
	} else if (phy_reg_data1 & 0x4)		/* link up */
		return 1;

	return 0;
}

/*
 * eth_port_read_smi_reg - Read PHY registers
 *
 * DESCRIPTION:
 *	This routine utilize the SMI interface to interact with the PHY in
 *	order to perform PHY register read.
 *
 * INPUT:
 *	unsigned int	port_num	Ethernet Port number.
 *	unsigned int	phy_reg		PHY register address offset.
 *	unsigned int	*value		Register value buffer.
 *
 * OUTPUT:
 *	Write the value of a specified PHY register into given buffer.
 *
 * RETURN:
 *	false if the PHY is busy or read data is not in valid state.
 *	true otherwise.
 *
 */
static void eth_port_read_smi_reg(unsigned int port_num,
				unsigned int phy_reg, unsigned int *value)
{
	int phy_addr = ethernet_phy_get(port_num);
	unsigned long flags;
	int i;

	/* the SMI register is a shared resource */
	spin_lock_irqsave(&mv643xx_eth_phy_lock, flags);

	/* wait for the SMI register to become available */
	for (i = 0; mv_read(MV643XX_ETH_SMI_REG) & ETH_SMI_BUSY; i++) {
		if (i == PHY_WAIT_ITERATIONS) {
			printk("mv643xx PHY busy timeout, port %d\n", port_num);
			goto out;
		}
		udelay(PHY_WAIT_MICRO_SECONDS);
	}

	mv_write(MV643XX_ETH_SMI_REG,
		(phy_addr << 16) | (phy_reg << 21) | ETH_SMI_OPCODE_READ);

	/* now wait for the data to be valid */
	for (i = 0; !(mv_read(MV643XX_ETH_SMI_REG) & ETH_SMI_READ_VALID); i++) {
		if (i == PHY_WAIT_ITERATIONS) {
			printk("mv643xx PHY read timeout, port %d\n", port_num);
			goto out;
		}
		udelay(PHY_WAIT_MICRO_SECONDS);
	}

	*value = mv_read(MV643XX_ETH_SMI_REG) & 0xffff;
out:
	spin_unlock_irqrestore(&mv643xx_eth_phy_lock, flags);
}

/*
 * eth_port_write_smi_reg - Write to PHY registers
 *
 * DESCRIPTION:
 *	This routine utilize the SMI interface to interact with the PHY in
 *	order to perform writes to PHY registers.
 *
 * INPUT:
 *	unsigned int	eth_port_num	Ethernet Port number.
 *	unsigned int	phy_reg		PHY register address offset.
 *	unsigned int	value		Register value.
 *
 * OUTPUT:
 *	Write the given value to the specified PHY register.
 *
 * RETURN:
 *	false if the PHY is busy.
 *	true otherwise.
 *
 */
static void eth_port_write_smi_reg(unsigned int eth_port_num,
				   unsigned int phy_reg, unsigned int value)
{
	int phy_addr;
	int i;
	unsigned long flags;

	phy_addr = ethernet_phy_get(eth_port_num);

	/* the SMI register is a shared resource */
	spin_lock_irqsave(&mv643xx_eth_phy_lock, flags);

	/* wait for the SMI register to become available */
	for (i = 0; mv_read(MV643XX_ETH_SMI_REG) & ETH_SMI_BUSY; i++) {
		if (i == PHY_WAIT_ITERATIONS) {
			printk("mv643xx PHY busy timeout, port %d\n",
								eth_port_num);
			goto out;
		}
		udelay(PHY_WAIT_MICRO_SECONDS);
	}

	mv_write(MV643XX_ETH_SMI_REG, (phy_addr << 16) | (phy_reg << 21) |
				ETH_SMI_OPCODE_WRITE | (value & 0xffff));
out:
	spin_unlock_irqrestore(&mv643xx_eth_phy_lock, flags);
}

/*
 * eth_port_send - Send an Ethernet packet
 *
 * DESCRIPTION:
 *	This routine send a given packet described by p_pktinfo parameter. It
 *	supports transmitting of a packet spaned over multiple buffers. The
 *	routine updates 'curr' and 'first' indexes according to the packet
 *	segment passed to the routine. In case the packet segment is first,
 *	the 'first' index is update. In any case, the 'curr' index is updated.
 *	If the routine get into Tx resource error it assigns 'curr' index as
 *	'first'. This way the function can abort Tx process of multiple
 *	descriptors per packet.
 *
 * INPUT:
 *	struct mv643xx_private	*mp		Ethernet Port Control srtuct.
 *	struct pkt_info		*p_pkt_info	User packet buffer.
 *
 * OUTPUT:
 *	Tx ring 'curr' and 'first' indexes are updated.
 *
 * RETURN:
 *	ETH_QUEUE_FULL in case of Tx resource error.
 *	ETH_ERROR in case the routine can not access Tx desc ring.
 *	ETH_QUEUE_LAST_RESOURCE if the routine uses the last Tx resource.
 *	ETH_OK otherwise.
 *
 */
#ifdef MV643XX_CHECKSUM_OFFLOAD_TX
/*
 * Modified to include the first descriptor pointer in case of SG
 */
static ETH_FUNC_RET_STATUS eth_port_send(struct mv643xx_private *mp,
					 struct pkt_info *p_pkt_info)
{
	int tx_desc_curr, tx_desc_used, tx_first_desc, tx_next_desc;
	struct eth_tx_desc *current_descriptor;
	struct eth_tx_desc *first_descriptor;
	u32 command;

	/* Do not process Tx ring in case of Tx ring resource error */
	if (mp->tx_resource_err)
		return ETH_QUEUE_FULL;

	/*
	 * The hardware requires that each buffer that is <= 8 bytes
	 * in length must be aligned on an 8 byte boundary.
	 */
	if (p_pkt_info->byte_cnt <= 8 && p_pkt_info->buf_ptr & 0x7) {
		printk(KERN_ERR
			"mv643xx_eth port %d: packet size <= 8 problem\n",
			mp->port_num);
		return ETH_ERROR;
	}

	mp->tx_ring_skbs++;
	BUG_ON(mp->tx_ring_skbs > mp->tx_ring_size);

	/* Get the Tx Desc ring indexes */
	tx_desc_curr = mp->tx_curr_desc_q;
	tx_desc_used = mp->tx_used_desc_q;

	current_descriptor = &mp->p_tx_desc_area[tx_desc_curr];

	tx_next_desc = (tx_desc_curr + 1) % mp->tx_ring_size;

	current_descriptor->buf_ptr = p_pkt_info->buf_ptr;
	current_descriptor->byte_cnt = p_pkt_info->byte_cnt;
	current_descriptor->l4i_chk = p_pkt_info->l4i_chk;
	mp->tx_skb[tx_desc_curr] = p_pkt_info->return_info;

	command = p_pkt_info->cmd_sts | ETH_ZERO_PADDING | ETH_GEN_CRC |
							ETH_BUFFER_OWNED_BY_DMA;
	if (command & ETH_TX_FIRST_DESC) {
		tx_first_desc = tx_desc_curr;
		mp->tx_first_desc_q = tx_first_desc;
		first_descriptor = current_descriptor;
		mp->tx_first_command = command;
	} else {
		tx_first_desc = mp->tx_first_desc_q;
		first_descriptor = &mp->p_tx_desc_area[tx_first_desc];
		BUG_ON(first_descriptor == NULL);
		current_descriptor->cmd_sts = command;
	}

	if (command & ETH_TX_LAST_DESC) {
		wmb();
		first_descriptor->cmd_sts = mp->tx_first_command;

		wmb();
		ETH_ENABLE_TX_QUEUE(mp->port_num);

		/*
		 * Finish Tx packet. Update first desc in case of Tx resource
		 * error */
		tx_first_desc = tx_next_desc;
		mp->tx_first_desc_q = tx_first_desc;
	}

	/* Check for ring index overlap in the Tx desc ring */
	if (tx_next_desc == tx_desc_used) {
		mp->tx_resource_err = 1;
		mp->tx_curr_desc_q = tx_first_desc;

		return ETH_QUEUE_LAST_RESOURCE;
	}

	mp->tx_curr_desc_q = tx_next_desc;

	return ETH_OK;
}
#else
static ETH_FUNC_RET_STATUS eth_port_send(struct mv643xx_private *mp,
					 struct pkt_info *p_pkt_info)
{
	int tx_desc_curr;
	int tx_desc_used;
	struct eth_tx_desc *current_descriptor;
	unsigned int command_status;

	/* Do not process Tx ring in case of Tx ring resource error */
	if (mp->tx_resource_err)
		return ETH_QUEUE_FULL;

	mp->tx_ring_skbs++;
	BUG_ON(mp->tx_ring_skbs > mp->tx_ring_size);

	/* Get the Tx Desc ring indexes */
	tx_desc_curr = mp->tx_curr_desc_q;
	tx_desc_used = mp->tx_used_desc_q;
	current_descriptor = &mp->p_tx_desc_area[tx_desc_curr];

	command_status = p_pkt_info->cmd_sts | ETH_ZERO_PADDING | ETH_GEN_CRC;
	current_descriptor->buf_ptr = p_pkt_info->buf_ptr;
	current_descriptor->byte_cnt = p_pkt_info->byte_cnt;
	mp->tx_skb[tx_desc_curr] = p_pkt_info->return_info;

	/* Set last desc with DMA ownership and interrupt enable. */
	wmb();
	current_descriptor->cmd_sts = command_status |
			ETH_BUFFER_OWNED_BY_DMA | ETH_TX_ENABLE_INTERRUPT;

	wmb();
	ETH_ENABLE_TX_QUEUE(mp->port_num);

	/* Finish Tx packet. Update first desc in case of Tx resource error */
	tx_desc_curr = (tx_desc_curr + 1) % mp->tx_ring_size;

	/* Update the current descriptor */
	mp->tx_curr_desc_q = tx_desc_curr;

	/* Check for ring index overlap in the Tx desc ring */
	if (tx_desc_curr == tx_desc_used) {
		mp->tx_resource_err = 1;
		return ETH_QUEUE_LAST_RESOURCE;
	}

	return ETH_OK;
}
#endif

/*
 * eth_tx_return_desc - Free all used Tx descriptors
 *
 * DESCRIPTION:
 *	This routine returns the transmitted packet information to the caller.
 *	It uses the 'first' index to support Tx desc return in case a transmit
 *	of a packet spanned over multiple buffer still in process.
 *	In case the Tx queue was in "resource error" condition, where there are
 *	no available Tx resources, the function resets the resource error flag.
 *
 * INPUT:
 *	struct mv643xx_private	*mp		Ethernet Port Control srtuct.
 *	struct pkt_info		*p_pkt_info	User packet buffer.
 *
 * OUTPUT:
 *	Tx ring 'first' and 'used' indexes are updated.
 *
 * RETURN:
 *	ETH_ERROR in case the routine can not access Tx desc ring.
 *	ETH_RETRY in case there is transmission in process.
 *	ETH_END_OF_JOB if the routine has nothing to release.
 *	ETH_OK otherwise.
 *
 */
static ETH_FUNC_RET_STATUS eth_tx_return_desc(struct mv643xx_private *mp,
						struct pkt_info *p_pkt_info)
{
	int tx_desc_used;
#ifdef MV643XX_CHECKSUM_OFFLOAD_TX
	int tx_busy_desc = mp->tx_first_desc_q;
#else
	int tx_busy_desc = mp->tx_curr_desc_q;
#endif
	struct eth_tx_desc *p_tx_desc_used;
	unsigned int command_status;

	/* Get the Tx Desc ring indexes */
	tx_desc_used = mp->tx_used_desc_q;

	p_tx_desc_used = &mp->p_tx_desc_area[tx_desc_used];

	/* Sanity check */
	if (p_tx_desc_used == NULL)
		return ETH_ERROR;

	/* Stop release. About to overlap the current available Tx descriptor */
	if (tx_desc_used == tx_busy_desc && !mp->tx_resource_err)
		return ETH_END_OF_JOB;

	command_status = p_tx_desc_used->cmd_sts;

	/* Still transmitting... */
	if (command_status & (ETH_BUFFER_OWNED_BY_DMA))
		return ETH_RETRY;

	/* Pass the packet information to the caller */
	p_pkt_info->cmd_sts = command_status;
	p_pkt_info->return_info = mp->tx_skb[tx_desc_used];
	mp->tx_skb[tx_desc_used] = NULL;

	/* Update the next descriptor to release. */
	mp->tx_used_desc_q = (tx_desc_used + 1) % mp->tx_ring_size;

	/* Any Tx return cancels the Tx resource error status */
	mp->tx_resource_err = 0;

	BUG_ON(mp->tx_ring_skbs == 0);
	mp->tx_ring_skbs--;

	return ETH_OK;
}

/*
 * eth_port_receive - Get received information from Rx ring.
 *
 * DESCRIPTION:
 * 	This routine returns the received data to the caller. There is no
 *	data copying during routine operation. All information is returned
 *	using pointer to packet information struct passed from the caller.
 *	If the routine exhausts Rx ring resources then the resource error flag
 *	is set.
 *
 * INPUT:
 *	struct mv643xx_private	*mp		Ethernet Port Control srtuct.
 *	struct pkt_info		*p_pkt_info	User packet buffer.
 *
 * OUTPUT:
 *	Rx ring current and used indexes are updated.
 *
 * RETURN:
 *	ETH_ERROR in case the routine can not access Rx desc ring.
 *	ETH_QUEUE_FULL if Rx ring resources are exhausted.
 *	ETH_END_OF_JOB if there is no received data.
 *	ETH_OK otherwise.
 */
static ETH_FUNC_RET_STATUS eth_port_receive(struct mv643xx_private *mp,
						struct pkt_info *p_pkt_info)
{
	int rx_next_curr_desc, rx_curr_desc, rx_used_desc;
	volatile struct eth_rx_desc *p_rx_desc;
	unsigned int command_status;

	/* Do not process Rx ring in case of Rx ring resource error */
	if (mp->rx_resource_err)
		return ETH_QUEUE_FULL;

	/* Get the Rx Desc ring 'curr and 'used' indexes */
	rx_curr_desc = mp->rx_curr_desc_q;
	rx_used_desc = mp->rx_used_desc_q;

	p_rx_desc = &mp->p_rx_desc_area[rx_curr_desc];

	/* The following parameters are used to save readings from memory */
	command_status = p_rx_desc->cmd_sts;
	rmb();

	/* Nothing to receive... */
	if (command_status & (ETH_BUFFER_OWNED_BY_DMA))
		return ETH_END_OF_JOB;

	p_pkt_info->byte_cnt = (p_rx_desc->byte_cnt) - RX_BUF_OFFSET;
	p_pkt_info->cmd_sts = command_status;
	p_pkt_info->buf_ptr = (p_rx_desc->buf_ptr) + RX_BUF_OFFSET;
	p_pkt_info->return_info = mp->rx_skb[rx_curr_desc];
	p_pkt_info->l4i_chk = p_rx_desc->buf_size;

	/* Clean the return info field to indicate that the packet has been */
	/* moved to the upper layers					    */
	mp->rx_skb[rx_curr_desc] = NULL;

	/* Update current index in data structure */
	rx_next_curr_desc = (rx_curr_desc + 1) % mp->rx_ring_size;
	mp->rx_curr_desc_q = rx_next_curr_desc;

	/* Rx descriptors exhausted. Set the Rx ring resource error flag */
	if (rx_next_curr_desc == rx_used_desc)
		mp->rx_resource_err = 1;

	return ETH_OK;
}

/*
 * eth_rx_return_buff - Returns a Rx buffer back to the Rx ring.
 *
 * DESCRIPTION:
 *	This routine returns a Rx buffer back to the Rx ring. It retrieves the
 *	next 'used' descriptor and attached the returned buffer to it.
 *	In case the Rx ring was in "resource error" condition, where there are
 *	no available Rx resources, the function resets the resource error flag.
 *
 * INPUT:
 *	struct mv643xx_private	*mp		Ethernet Port Control srtuct.
 *	struct pkt_info		*p_pkt_info	Information on returned buffer.
 *
 * OUTPUT:
 *	New available Rx resource in Rx descriptor ring.
 *
 * RETURN:
 *	ETH_ERROR in case the routine can not access Rx desc ring.
 *	ETH_OK otherwise.
 */
static ETH_FUNC_RET_STATUS eth_rx_return_buff(struct mv643xx_private *mp,
						struct pkt_info *p_pkt_info)
{
	int used_rx_desc;	/* Where to return Rx resource */
	volatile struct eth_rx_desc *p_used_rx_desc;

	/* Get 'used' Rx descriptor */
	used_rx_desc = mp->rx_used_desc_q;
	p_used_rx_desc = &mp->p_rx_desc_area[used_rx_desc];

	p_used_rx_desc->buf_ptr = p_pkt_info->buf_ptr;
	p_used_rx_desc->buf_size = p_pkt_info->byte_cnt;
	mp->rx_skb[used_rx_desc] = p_pkt_info->return_info;

	/* Flush the write pipe */

	/* Return the descriptor to DMA ownership */
	wmb();
	p_used_rx_desc->cmd_sts =
			ETH_BUFFER_OWNED_BY_DMA | ETH_RX_ENABLE_INTERRUPT;
	wmb();

	/* Move the used descriptor pointer to the next descriptor */
	mp->rx_used_desc_q = (used_rx_desc + 1) % mp->rx_ring_size;

	/* Any Rx return cancels the Rx resource error status */
	mp->rx_resource_err = 0;

	return ETH_OK;
}

/************* Begin ethtool support *************************/

struct mv643xx_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define MV643XX_STAT(m) sizeof(((struct mv643xx_private *)0)->m), \
		      offsetof(struct mv643xx_private, m)

static const struct mv643xx_stats mv643xx_gstrings_stats[] = {
	{ "rx_packets", MV643XX_STAT(stats.rx_packets) },
	{ "tx_packets", MV643XX_STAT(stats.tx_packets) },
	{ "rx_bytes", MV643XX_STAT(stats.rx_bytes) },
	{ "tx_bytes", MV643XX_STAT(stats.tx_bytes) },
	{ "rx_errors", MV643XX_STAT(stats.rx_errors) },
	{ "tx_errors", MV643XX_STAT(stats.tx_errors) },
	{ "rx_dropped", MV643XX_STAT(stats.rx_dropped) },
	{ "tx_dropped", MV643XX_STAT(stats.tx_dropped) },
	{ "good_octets_received", MV643XX_STAT(mib_counters.good_octets_received) },
	{ "bad_octets_received", MV643XX_STAT(mib_counters.bad_octets_received) },
	{ "internal_mac_transmit_err", MV643XX_STAT(mib_counters.internal_mac_transmit_err) },
	{ "good_frames_received", MV643XX_STAT(mib_counters.good_frames_received) },
	{ "bad_frames_received", MV643XX_STAT(mib_counters.bad_frames_received) },
	{ "broadcast_frames_received", MV643XX_STAT(mib_counters.broadcast_frames_received) },
	{ "multicast_frames_received", MV643XX_STAT(mib_counters.multicast_frames_received) },
	{ "frames_64_octets", MV643XX_STAT(mib_counters.frames_64_octets) },
	{ "frames_65_to_127_octets", MV643XX_STAT(mib_counters.frames_65_to_127_octets) },
	{ "frames_128_to_255_octets", MV643XX_STAT(mib_counters.frames_128_to_255_octets) },
	{ "frames_256_to_511_octets", MV643XX_STAT(mib_counters.frames_256_to_511_octets) },
	{ "frames_512_to_1023_octets", MV643XX_STAT(mib_counters.frames_512_to_1023_octets) },
	{ "frames_1024_to_max_octets", MV643XX_STAT(mib_counters.frames_1024_to_max_octets) },
	{ "good_octets_sent", MV643XX_STAT(mib_counters.good_octets_sent) },
	{ "good_frames_sent", MV643XX_STAT(mib_counters.good_frames_sent) },
	{ "excessive_collision", MV643XX_STAT(mib_counters.excessive_collision) },
	{ "multicast_frames_sent", MV643XX_STAT(mib_counters.multicast_frames_sent) },
	{ "broadcast_frames_sent", MV643XX_STAT(mib_counters.broadcast_frames_sent) },
	{ "unrec_mac_control_received", MV643XX_STAT(mib_counters.unrec_mac_control_received) },
	{ "fc_sent", MV643XX_STAT(mib_counters.fc_sent) },
	{ "good_fc_received", MV643XX_STAT(mib_counters.good_fc_received) },
	{ "bad_fc_received", MV643XX_STAT(mib_counters.bad_fc_received) },
	{ "undersize_received", MV643XX_STAT(mib_counters.undersize_received) },
	{ "fragments_received", MV643XX_STAT(mib_counters.fragments_received) },
	{ "oversize_received", MV643XX_STAT(mib_counters.oversize_received) },
	{ "jabber_received", MV643XX_STAT(mib_counters.jabber_received) },
	{ "mac_receive_error", MV643XX_STAT(mib_counters.mac_receive_error) },
	{ "bad_crc_event", MV643XX_STAT(mib_counters.bad_crc_event) },
	{ "collision", MV643XX_STAT(mib_counters.collision) },
	{ "late_collision", MV643XX_STAT(mib_counters.late_collision) },
};

#define MV643XX_STATS_LEN	\
	sizeof(mv643xx_gstrings_stats) / sizeof(struct mv643xx_stats)

static int
mv643xx_get_settings(struct net_device *netdev, struct ethtool_cmd *ecmd)
{
	struct mv643xx_private *mp = netdev->priv;
	int port_num = mp->port_num;
	int autoneg = eth_port_autoneg_supported(port_num);
	int mode_10_bit;
	int auto_duplex;
	int half_duplex = 0;
	int full_duplex = 0;
	int auto_speed;
	int speed_10 = 0;
	int speed_100 = 0;
	int speed_1000 = 0;

	u32 pcs = mv_read(MV643XX_ETH_PORT_SERIAL_CONTROL_REG(port_num));
	u32 psr = mv_read(MV643XX_ETH_PORT_STATUS_REG(port_num));

	mode_10_bit = psr & MV643XX_ETH_PORT_STATUS_MODE_10_BIT;

	if (mode_10_bit) {
		ecmd->supported = SUPPORTED_10baseT_Half;
	} else {
		ecmd->supported = (SUPPORTED_10baseT_Half		|
				   SUPPORTED_10baseT_Full		|
				   SUPPORTED_100baseT_Half		|
				   SUPPORTED_100baseT_Full		|
				   SUPPORTED_1000baseT_Full		|
				   (autoneg ? SUPPORTED_Autoneg : 0)	|
				   SUPPORTED_TP);

		auto_duplex = !(pcs & MV643XX_ETH_DISABLE_AUTO_NEG_FOR_DUPLX);
		auto_speed = !(pcs & MV643XX_ETH_DISABLE_AUTO_NEG_SPEED_GMII);

		ecmd->advertising = ADVERTISED_TP;

		if (autoneg) {
			ecmd->advertising |= ADVERTISED_Autoneg;

			if (auto_duplex) {
				half_duplex = 1;
				full_duplex = 1;
			} else {
				if (pcs & MV643XX_ETH_SET_FULL_DUPLEX_MODE)
					full_duplex = 1;
				else
					half_duplex = 1;
			}

			if (auto_speed) {
				speed_10 = 1;
				speed_100 = 1;
				speed_1000 = 1;
			} else {
				if (pcs & MV643XX_ETH_SET_GMII_SPEED_TO_1000)
					speed_1000 = 1;
				else if (pcs & MV643XX_ETH_SET_MII_SPEED_TO_100)
					speed_100 = 1;
				else
					speed_10 = 1;
			}

			if (speed_10 & half_duplex)
				ecmd->advertising |= ADVERTISED_10baseT_Half;
			if (speed_10 & full_duplex)
				ecmd->advertising |= ADVERTISED_10baseT_Full;
			if (speed_100 & half_duplex)
				ecmd->advertising |= ADVERTISED_100baseT_Half;
			if (speed_100 & full_duplex)
				ecmd->advertising |= ADVERTISED_100baseT_Full;
			if (speed_1000)
				ecmd->advertising |= ADVERTISED_1000baseT_Full;
		}
	}

	ecmd->port = PORT_TP;
	ecmd->phy_address = ethernet_phy_get(port_num);

	ecmd->transceiver = XCVR_EXTERNAL;

	if (netif_carrier_ok(netdev)) {
		if (mode_10_bit)
			ecmd->speed = SPEED_10;
		else {
			if (psr & MV643XX_ETH_PORT_STATUS_GMII_1000)
				ecmd->speed = SPEED_1000;
			else if (psr & MV643XX_ETH_PORT_STATUS_MII_100)
				ecmd->speed = SPEED_100;
			else
				ecmd->speed = SPEED_10;
		}

		if (psr & MV643XX_ETH_PORT_STATUS_FULL_DUPLEX)
			ecmd->duplex = DUPLEX_FULL;
		else
			ecmd->duplex = DUPLEX_HALF;
	} else {
		ecmd->speed = -1;
		ecmd->duplex = -1;
	}

	ecmd->autoneg = autoneg ? AUTONEG_ENABLE : AUTONEG_DISABLE;
	return 0;
}

static void
mv643xx_get_drvinfo(struct net_device *netdev,
                       struct ethtool_drvinfo *drvinfo)
{
	strncpy(drvinfo->driver,  mv643xx_driver_name, 32);
	strncpy(drvinfo->version, mv643xx_driver_version, 32);
	strncpy(drvinfo->fw_version, "N/A", 32);
	strncpy(drvinfo->bus_info, "mv643xx", 32);
	drvinfo->n_stats = MV643XX_STATS_LEN;
}

static int 
mv643xx_get_stats_count(struct net_device *netdev)
{
	return MV643XX_STATS_LEN;
}

static void 
mv643xx_get_ethtool_stats(struct net_device *netdev, 
		struct ethtool_stats *stats, uint64_t *data)
{
	struct mv643xx_private *mp = netdev->priv;
	int i;

	eth_update_mib_counters(mp);

	for(i = 0; i < MV643XX_STATS_LEN; i++) {
		char *p = (char *)mp+mv643xx_gstrings_stats[i].stat_offset;	
		data[i] = (mv643xx_gstrings_stats[i].sizeof_stat == 
			sizeof(uint64_t)) ? *(uint64_t *)p : *(uint32_t *)p;
	}
}

static void 
mv643xx_get_strings(struct net_device *netdev, uint32_t stringset, uint8_t *data)
{
	int i;

	switch(stringset) {
	case ETH_SS_STATS:
		for (i=0; i < MV643XX_STATS_LEN; i++) {
			memcpy(data + i * ETH_GSTRING_LEN, 
			mv643xx_gstrings_stats[i].stat_string,
			ETH_GSTRING_LEN);
		}
		break;
	}
}

static struct ethtool_ops mv643xx_ethtool_ops = {
	.get_settings           = mv643xx_get_settings,
	.get_drvinfo            = mv643xx_get_drvinfo,
	.get_link               = ethtool_op_get_link,
	.get_sg			= ethtool_op_get_sg,
	.set_sg			= ethtool_op_set_sg,
	.get_strings            = mv643xx_get_strings,
	.get_stats_count        = mv643xx_get_stats_count,
	.get_ethtool_stats      = mv643xx_get_ethtool_stats,
};

/************* End ethtool support *************************/
